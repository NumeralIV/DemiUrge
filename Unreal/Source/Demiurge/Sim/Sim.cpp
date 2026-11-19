// DEMIURGE — Sim/Sim.cpp : world creation, phases, the tick scheduler, genesis, endings, score and
// the headless runner hooks (SPEC-SIM 3.12; FOUNDATION-CPP 15). Owner: sim engineer (A14).
//
// Choices where the spec is silent (all reported):
// - w.tick is incremented at the START of sim::tick, so the first history tick is t0 + 1 and the
//   yearly group runs when (tick - t0) % 4 == 0, i.e. exactly when w.year reaches 1, 2, 3, ...
// - Heavy-pass bookkeeping (ticks of the last climate / hydro pass, a pending habitability pass)
//   is transient (SimScratch). rederive resets it (both passes have just run). A world without it
//   (loaded, scratch cleared) treats climate and hydro as long overdue.
// - "classify this tick, habitability the next": a pending habitability pass is the first heavy
//   pass of the next tick, before derive / climate / hydro (so the pair is always on consecutive
//   ticks). In History a classify is only started on a tick whose next tick is NOT a yearly tick
//   (yearly ticks run no heavy pass), so the habitability half never lands on a yearly tick.
// - setPhase only moves forward (Molten < Water < Life < People < History < Ended); entering any
//   phase from Water on also sets heat = 0 (a jump past Water must not leave the world molten).
// - Silence bookkeeping is persisted in vars.num: "sim_empty" (year the people vanished) and
//   "sim_lost" (people:lost already emitted for this extinction).
// - genesis::skip: heat 0, sea (clamped -6 .. +1.5), rederive, climate::calibrate (ice caps settle
//   before anything grows), life::fill(1), classify + habitability, setPhase(People), then wakes the
//   first tribe the way a player does (powers::use "awaken" at opts.cell, then at civ::bestSite; its
//   first success calls setPhase(History)); if Powers refuses, society::newCulture + civ::found
//   directly. Always ends in History (awaken is free there); returns false if nobody woke.
#include "Sim.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "Balance.h"
#include "Chronicle.h"
#include "Civ.h"
#include "Climate.h"
#include "Hydro.h"
#include "Life.h"
#include "Names.h"
#include "Powers.h"
#include "Society.h"
#include "Terrain.h"
#include "Units.h"

namespace dm {
namespace {

// Transient scheduler state (one per world, never saved).
struct SimScratch {
	bool init = false;
	int64_t lastClimate = 0;
	int64_t lastHydro = 0;
	bool habPending = false;
};

SimScratch& sched(const World& w) {
	SimScratch& s = w.scratch<SimScratch>();
	if (!s.init) {
		s.init = true;
		s.lastClimate = w.tick - 1000000;
		s.lastHydro = w.tick - 1000000;
		s.habPending = false;
	}
	return s;
}

bool historic(Phase p) { return p == Phase::History || p == Phase::Ended; }

int64_t posMod(int64_t a, int64_t m) {
	if (m <= 0) return 0;
	const int64_t r = a % m;
	return r < 0 ? r + m : r;
}

// Yearly-group test for a tick number (after the increment).
bool yearlyAt(const World& w, int64_t t) {
	return historic(w.phase) && posMod(t - w.vars.t0, sim::kTicksPerYear) == 0;
}

double finiteOr(double v, double fallback) { return std::isfinite(v) ? v : fallback; }

Mood moodWith(const SimTune& T, double devotion, double love) {
	const double d = finiteOr(devotion, 0), l = finiteOr(love, 0);
	if (d >= T.moodBelovedDevotion && l >= T.moodBelovedLove) return Mood::Beloved;
	if (l <= T.moodFearedLove) return Mood::Feared;
	if (d < T.moodForgottenDevotion) return Mood::Forgotten;
	return Mood::Parting;
}

bool anyoneAlive(const World& w) {
	for (const Settlement& s : w.settlements) if (!s.dead) return true;
	for (const Unit& u : w.units) if (u.type == UnitType::Settler) return true;
	return false;
}

int liveSettlements(const World& w) {
	int k = 0;
	for (const Settlement& s : w.settlements) if (!s.dead) k++;
	return k;
}

// One heavy pass (SPEC-SIM 3.12): at most one per tick, the first that is due.
void heavyPass(World& w) {
	const SimTune& T = w.tune<SimTune>();
	SimScratch& S = sched(w);
	if (S.habPending) {
		S.habPending = false;
		life::habitability(w);
		return;
	}
	if (w.need.derive) {
		terrain::derive(w);
		return;
	}
	const int64_t sinceClimate = w.tick - S.lastClimate;
	if ((w.need.climate && sinceClimate >= T.climateMin) || sinceClimate >= T.climateMax) {
		climate::update(w);
		S.lastClimate = w.tick;
		return;
	}
	if (w.need.hydro && w.tick - S.lastHydro >= T.hydroMin) {
		hydro::update(w);
		S.lastHydro = w.tick;
		return;
	}
	if (w.need.life) {
		// the habitability half runs next tick: never start the pair right before a yearly tick
		if (yearlyAt(w, w.tick + 1)) return;
		life::classify(w);
		S.habPending = true;
	}
}

void yearlyGroup(World& w) {
	civ::yearly(w);
	society::yearly(w);     // ends with society::diplomacy and chronicle::updateEpithets
	climate::yearly(w);
	powers::natural(w);
	sim::refreshStats(w);
	chronicle::snapshot(w, false);
	sim::checkEnding(w);
}

void setEnding(World& w, const Ending& e) {
	w.ending = e;
	sim::setPhase(w, Phase::Ended);
	w.bus.emit(EvEnding{ e.type, e.mood, e.culture, e.settlement, e.year });
}

// FNV-1a over raw bytes.
struct Fnv {
	uint32_t h = 2166136261u;
	void bytes(const void* p, size_t n) {
		const uint8_t* b = static_cast<const uint8_t*>(p);
		for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
	}
	void u32(uint32_t v) { bytes(&v, 4); }
	void f64(double v) { bytes(&v, 8); }
};

} // namespace

namespace sim {

// ---------------------------------------------------------------- creation

std::unique_ptr<World> create(const WorldOpts& opts) {
	state::CreateOpts co;
	co.seed = opts.seed;
	co.L = opts.L;
	co.name = opts.name.empty() ? std::string("Unnamed") : opts.name;
	std::unique_ptr<World> w = state::create(co);
	if (!w) return w;
	if (opts.name.empty()) {
		Rng nameRng = w->rng.fork("sim:planet");      // never disturbs w.rng
		std::string nm = names::planet(nameRng);
		if (!nm.empty()) w->name = nm;
	}
	w->settings = opts.settings;
	if (!std::isfinite(w->settings.calamity) || w->settings.calamity < 0) w->settings.calamity = 1;
	balance::apply(*w);          // terrain tuning may be balanced: apply before generating
	terrain::generate(*w);
	use(*w);
	return w;
}

void use(World& w) {
	balance::apply(w);
	chronicle::attach(w);
	rederive(w);
}

void rederive(World& w) {
	if (historic(w.phase) && !w.vars.seaBase.has_value()) climate::calibrate(w);
	terrain::derive(w);
	climate::update(w);
	hydro::update(w);
	life::classify(w);
	life::habitability(w);
	life::rebuild(w);
	civ::rebuild(w);
	SimScratch& S = sched(w);
	S.lastClimate = w.tick;
	S.lastHydro = w.tick;
	S.habPending = false;
}

// ---------------------------------------------------------------- the tick

void tick(World& w) {
	const SimTune& T = w.tune<SimTune>();
	w.tick++;
	const int64_t t = w.tick;
	const int64_t iceEvery = std::max(1, T.iceEvery);
	switch (w.phase) {
	case Phase::Molten: {
		double h = finiteOr(w.heat, 0) - finiteOr(T.heatPerTick, 1.0 / 60);
		if (h <= 1e-9) h = 0;
		w.heat = h;
		if (h <= 0) setPhase(w, Phase::Water);
		return;
	}
	case Phase::Water:
	case Phase::Life:
	case Phase::People: {
		const int parts = std::max(1, w.phase == Phase::Life ? T.lifeParts : T.otherParts);
		powers::tick(w);
		life::burn(w);
		life::step(w, int(posMod(t, parts)), parts);
		if (posMod(t, iceEvery) == T.iceAt) climate::stepIce(w);
		heavyPass(w);
		return;
	}
	case Phase::History:
	case Phase::Ended: {
		const int parts = std::max(1, T.otherParts);
		w.year += kTick;
		powers::tick(w);
		life::burn(w);
		units::step(w, kTick);
		life::step(w, int(posMod(t, parts)), parts);
		const bool yearly = yearlyAt(w, t);
		if (yearly) yearlyGroup(w);
		if (posMod(t, iceEvery) == T.iceAt) climate::stepIce(w);
		if (!yearly) heavyPass(w);
		return;
	}
	}
}

void advance(World& w, int ticks) {
	for (int i = 0; i < ticks; i++) {
		powers::update(w, kRealPerTick);
		tick(w);
	}
}

// ---------------------------------------------------------------- phases

bool setPhase(World& w, Phase phase) {
	const Phase prev = w.phase;
	if (int(phase) <= int(prev)) return false;          // forward only (also refuses "already there")
	if (int(phase) > int(Phase::Ended)) return false;
	const SimTune& T = w.tune<SimTune>();
	w.phase = phase;
	if (phase != Phase::Molten) w.heat = 0;
	switch (phase) {
	case Phase::Water: break;
	case Phase::Life: w.vars.lifeBoost = std::isfinite(T.lifeBoost) && T.lifeBoost > 0 ? T.lifeBoost : 1; break;
	case Phase::People: w.vars.lifeBoost = 1; break;
	case Phase::History:
		w.vars.lifeBoost = 1;
		climate::calibrate(w);
		w.year = 0;
		w.vars.t0 = w.tick;
		break;
	default: break;
	}
	w.bus.emit(EvPhase{ phase, prev });
	chronicle::snapshot(w, true);
	return true;
}

// ---------------------------------------------------------------- stats, endings, score

void refreshStats(World& w) {
	double pop = 0, believers = 0;
	for (const Settlement& s : w.settlements) {
		if (s.dead) continue;
		const double p = std::isfinite(s.pop) && s.pop > 0 ? s.pop : 0;
		const double d = std::isfinite(s.devotion) ? std::clamp(s.devotion, 0.0, 1.0) : 0;
		pop += p;
		believers += p * d;
	}
	w.stats.pop = pop;
	w.stats.believers = believers;
	const double peak = std::isfinite(w.stats.peakPop) ? w.stats.peakPop : 0;
	w.stats.peakPop = std::max(peak, pop);
}

std::optional<Ending> checkEnding(World& w) {
	if (w.ending.has_value() || w.phase != Phase::History) return std::nullopt;
	const SimTune& T = w.tune<SimTune>();
	if (w.vars.ark.has_value()) {
		const ArkRecord ark = *w.vars.ark;
		const society::Regard r = society::regard(w, ark.culture);
		Ending e;
		e.type = EndingType::Ark;
		e.mood = moodWith(T, r.devotion, r.love);
		e.culture = ark.culture;
		e.settlement = ark.settlement;
		e.year = w.year;
		setEnding(w, e);
		return e;
	}
	NumBag& num = w.vars.num;
	if (anyoneAlive(w)) {
		num.erase("sim_empty");
		num.erase("sim_lost");
		return std::nullopt;
	}
	auto it = num.find("sim_empty");
	if (it == num.end() || !std::isfinite(it->second)) {
		num["sim_empty"] = w.year;
		it = num.find("sim_empty");
	}
	const double emptySince = it->second;
	if (w.stats.peakPop >= T.silencePeak) {
		if (w.year - emptySince >= T.silenceYears) {
			const bool ash = w.vars.lastNuke.has_value() && std::isfinite(*w.vars.lastNuke) && w.year - *w.vars.lastNuke <= T.ashYears;
			Ending e;
			e.type = EndingType::Silence;
			e.mood = ash ? Mood::Ash : Mood::Quiet;
			e.culture = -1;
			e.settlement = -1;
			e.year = w.year;
			setEnding(w, e);
			return e;
		}
	} else if (num.find("sim_lost") == num.end()) {
		num["sim_lost"] = 1;
		w.bus.emit(EvPeopleLost{});
	}
	return std::nullopt;
}

Score score(const World& w) {
	const SimTune& T = w.tune<SimTune>();
	Score sc;
	const double peak = std::isfinite(w.stats.peakPop) && w.stats.peakPop > 0 ? w.stats.peakPop : 0;
	int cultures = 0, wonders = 0;
	for (const Culture& c : w.cultures) if (!c.dead) cultures++;
	for (const Settlement& s : w.settlements) if (!s.dead && s.has(Building::WONDER)) wonders++;
	double yearsLived = w.ending.has_value() ? w.ending->year : w.year;
	yearsLived = std::clamp(finiteOr(yearsLived, 0), 0.0, T.scoreYearCap);
	const double vegCover = std::clamp(finiteOr(life::cover(w).veg, 0), 0.0, 1.0);
	// every part finite even with poisoned tuning (a NaN / infinite weight scores that part 0)
	auto part = [](double v) { return std::isfinite(v) ? std::round(v) : 0.0; };
	const double pPop = part(T.scorePop * std::log10(1 + peak));
	const double pAges = part(T.scoreAge * std::max(0, w.age));
	const double pCult = part(T.scoreCulture * cultures);
	const double pWond = part(T.scoreWonder * wonders);
	const double pYears = part(T.scoreYear * yearsLived);
	const double pLife = part(T.scoreLife * vegCover);
	const double base = pPop + pAges + pCult + pWond + pYears + pLife;
	double mult = 1;
	if (w.ending.has_value()) {
		switch (w.ending->mood) {
		case Mood::Beloved: mult = T.multBeloved; break;
		case Mood::Parting: mult = T.multParting; break;
		case Mood::Forgotten: mult = T.multForgotten; break;
		case Mood::Feared: mult = T.multFeared; break;
		case Mood::Quiet: mult = T.multQuiet; break;
		case Mood::Ash: mult = T.multAsh; break;
		}
	}
	mult = finiteOr(mult, 1);
	sc.total = part(base * mult);
	sc.parts["pop"] = pPop;
	sc.parts["ages"] = pAges;
	sc.parts["cultures"] = pCult;
	sc.parts["wonders"] = pWond;
	sc.parts["years"] = pYears;
	sc.parts["life"] = pLife;
	sc.parts["ending"] = sc.total - base;
	sc.parts["base"] = base;
	sc.parts["mult"] = mult;
	return sc;
}

Mood mood(double devotion, double love) {
	const SimTune defaults{};     // the spec thresholds (no world to read balanced ones from)
	return moodWith(defaults, devotion, love);
}

// ---------------------------------------------------------------- genesis

namespace genesis {

Progress progress(const World& w) {
	const SimTune& T = w.tune<SimTune>();
	Progress p;
	p.phase = w.phase;
	int sea = 0;
	for (int c = 0; c < w.n; c++) if (state::isSea(w, c)) sea++;
	p.ocean = w.n > 0 ? double(sea) / double(w.n) : 0;
	p.veg = std::clamp(finiteOr(life::cover(w).veg, 0), 0.0, 1.0);
	switch (w.phase) {
	case Phase::Molten:
		p.value = sat(1 - finiteOr(w.heat, 0));
		p.canAdvance = false;
		p.hint = Hint::Cooling;
		break;
	case Phase::Water:
		p.value = T.waterValueSpan > 0 ? sat((w.seaLevel - T.waterValueLo) / T.waterValueSpan) : 0;
		p.canAdvance = w.seaLevel >= T.waterGate;
		p.hint = p.canAdvance ? Hint::WaterOk : Hint::WaterMore;
		break;
	case Phase::Life:
		p.value = T.lifeGate > 0 ? sat(p.veg / T.lifeGate) : 1;
		p.canAdvance = p.veg >= T.lifeGate;
		p.hint = p.canAdvance ? Hint::LifeOk : Hint::LifeMore;
		break;
	case Phase::People:
		p.value = 0;
		p.canAdvance = false;        // only an awakening moves on
		p.hint = Hint::People;
		break;
	default:
		p.value = 1;
		p.canAdvance = false;
		p.hint = Hint::People;
		break;
	}
	if (!std::isfinite(p.value)) p.value = 0;
	return p;
}

bool next(World& w) {
	const Progress p = progress(w);
	if (!p.canAdvance) return false;
	if (w.phase == Phase::Water) return setPhase(w, Phase::Life);
	if (w.phase == Phase::Life) return setPhase(w, Phase::People);
	return false;
}

namespace {
bool tryAwaken(World& w, int cell) {
	if (!w.valid(cell)) return false;
	const int before = liveSettlements(w);
	powers::use(w, "awaken", cell, {});
	return liveSettlements(w) > before;
}
} // namespace

bool skip(World& w, const SkipOpts& opts) {
	if (historic(w.phase)) return false;
	const SimTune& T = w.tune<SimTune>();
	w.heat = 0;
	w.seaLevel = std::clamp(finiteOr(opts.sea, 0), T.skipSeaMin, T.skipSeaMax);
	w.need = Need{};             // everything stale
	w.dirty = Dirty{};           // everything to redraw (only ever true)
	rederive(w);
	climate::calibrate(w);       // ice caps and the sea datum before anything grows (the sea is final now)
	life::fill(w, 1);
	life::classify(w);
	life::habitability(w);
	if (int(w.phase) < int(Phase::People)) setPhase(w, Phase::People);
	// wake the first tribe the way a player would; fall back to the lifecycle calls themselves
	bool woke = tryAwaken(w, opts.cell);
	if (!woke) {
		const int best = civ::bestSite(w);
		if (best != opts.cell) woke = tryAwaken(w, best);
	}
	if (!woke) {
		int c = w.valid(opts.cell) && civ::canSettle(w, opts.cell).ok ? opts.cell : civ::bestSite(w);
		if (w.valid(c)) {
			society::NewCultureOpts no;
			no.parent = -1;
			no.cell = c;
			no.reason = BirthReason::Awaken;
			Culture* cu = society::newCulture(w, no);
			if (cu) {
				const int cid = cu->id;
				if (civ::found(w, c, cid, T.startPop, FoundedBy::Awaken)) {
					w.god.awe = std::max(finiteOr(w.god.awe, 0), T.startAwe);
					woke = true;
				}
			}
		}
	}
	// History even when nobody could wake (the player may awaken for free there), but report it
	if (w.phase != Phase::History) setPhase(w, Phase::History);
	if (woke) w.god.awe = std::max(finiteOr(w.god.awe, 0), T.startAwe);
	refreshStats(w);
	return woke && w.phase == Phase::History;
}

} // namespace genesis

// ---------------------------------------------------------------- runner hooks

Sample sample(const World& w) {
	Sample s;
	s.year = w.year;
	s.tick = w.tick;
	s.phase = w.phase;
	s.pop = w.stats.pop;
	for (const Settlement& st : w.settlements) if (!st.dead) s.settlements++;
	for (const Culture& cu : w.cultures) if (!cu.dead) s.cultures++;
	s.age = w.age;
	s.anomaly = finiteOr(climate::anomaly(w), 0);
	s.seaLevel = w.seaLevel;
	s.co2 = w.co2;
	s.dust = w.dust;
	s.awe = w.god.awe;
	s.aweMax = w.god.aweMax;
	s.aweIncome = finiteOr(society::aweIncome(w), 0);
	s.units = int(w.units.size());
	s.disasters = int(w.disasters.size());
	s.wars = w.stats.wars;
	s.miracles = w.stats.miracles;
	return s;
}

uint32_t fingerprint(const World& w) {
	Fnv f;
	for (int i = 0; i < state::kCellFieldCount; i++) {
		const CellArray a = state::cellArray(w, i);
		if (!a.data) continue;
		size_t esz = 4;
		switch (a.type) {
		case FieldType::F32: esz = 4; break;
		case FieldType::U8: esz = 1; break;
		case FieldType::I16: esz = 2; break;
		case FieldType::I32: esz = 4; break;
		}
		f.bytes(a.data, a.size * esz);
	}
	std::vector<uint8_t> bytes;
	if (state::serialize(w, bytes)) f.bytes(bytes.data(), bytes.size());
	else f.u32(0xdeadbeefu);
	f.f64(w.year);
	f.u32(uint32_t(w.tick));
	f.u32(w.rng.s);
	f.u32(uint32_t(w.units.size()));
	f.u32(uint32_t(w.rivers.size()));
	for (const River& r : w.rivers) f.bytes(r.cells.data(), r.cells.size() * sizeof(r.cells[0]));
	return f.h;
}

std::string firstNonFinite(const World& w) {
	for (int i = 0; i < state::kCellFieldCount; i++) {
		if (state::kCellFields[i].type != FieldType::F32) continue;
		const CellArray a = state::cellArray(w, i);
		const float* p = static_cast<const float*>(a.data);
		if (!p) continue;
		for (size_t c = 0; c < a.size; c++)
			if (!std::isfinite(p[c])) return std::string(state::kCellFields[i].name) + "[" + std::to_string(c) + "]";
	}
	const struct { const char* nm; double v; } sc[] = {
		{ "year", w.year }, { "seaLevel", w.seaLevel }, { "globalTemp", w.globalTemp }, { "co2", w.co2 },
		{ "dust", w.dust }, { "heat", w.heat }, { "god.awe", w.god.awe }, { "god.aweMax", w.god.aweMax },
		{ "god.mercy", w.god.mercy }, { "god.wrath", w.god.wrath }, { "stats.pop", w.stats.pop },
		{ "stats.peakPop", w.stats.peakPop }, { "stats.believers", w.stats.believers },
		{ "settings.calamity", w.settings.calamity }, { "vars.lifeBoost", w.vars.lifeBoost },
	};
	for (const auto& s : sc) if (!std::isfinite(s.v)) return s.nm;
	auto badOpt = [](const std::optional<double>& v) { return v.has_value() && !std::isfinite(*v); };
	auto badBag = [](const NumBag& b) {
		for (const auto& kv : b) if (!std::isfinite(kv.second)) return true;
		return false;
	};
	for (const Settlement& s : w.settlements) {
		const double v[] = { s.pop, s.K, s.growth, s.devotion, s.love, s.jaded, s.plague, s.immune, s.inspire, s.blessed, s.ark,
			s.founded, s.lastExpand };
		bool bad = badOpt(s.deadYear) || (s.prayer && !std::isfinite(s.prayer->since));
		for (double x : v) if (!std::isfinite(x)) bad = true;
		if (bad) return "settlement " + std::to_string(s.id);
	}
	for (const Culture& c : w.cultures) {
		const double v[] = { c.science, c.pop, c.exhaustion, c.traits.piety, c.traits.aggression, c.traits.curiosity,
			c.traits.seafaring, c.born, c.edictSince, c.lastSplit, c.color[0], c.color[1], c.color[2] };
		bool bad = badBag(c.seen) || (c.prophet && !std::isfinite(c.prophet->until));
		for (double x : v) if (!std::isfinite(x)) bad = true;
		if (bad) return "culture " + std::to_string(c.id);
	}
	for (const Unit& u : w.units) {
		const double v[] = { u.t, u.speed, u.size, u.born };
		bool bad = badBag(u.data);
		for (double x : v) if (!std::isfinite(x)) bad = true;
		if (bad) return "unit " + std::to_string(u.id);
	}
	for (const Disaster& d : w.disasters) {
		const double v[] = { d.t, d.dur, d.power, d.ang, d.eta, d.flight, d.front };
		bool bad = badBag(d.data);
		for (double x : v) if (!std::isfinite(x)) bad = true;
		if (bad) return "disaster " + std::to_string(d.id);
	}
	for (const auto& kv : w.vars.rel) {
		const Relation& r = kv.second;
		if (!std::isfinite(r.v) || !std::isfinite(r.since) || !std::isfinite(r.truce) || !std::isfinite(r.trade))
			return "vars.rel " + std::to_string(kv.first.first) + "|" + std::to_string(kv.first.second);
	}
	if (badOpt(w.vars.seaBase) || badOpt(w.vars.iceRef) || badOpt(w.vars.lastNuke) || badOpt(w.vars.snapEvery) ||
	    badOpt(w.vars.snapNext) || badBag(w.vars.num))
		return "vars";
	if (w.vars.ark && !std::isfinite(w.vars.ark->year)) return "vars.ark";
	if (w.ending && !std::isfinite(w.ending->year)) return "ending";
	for (const ChronicleEntry& e : w.chronicle) if (!std::isfinite(e.year)) return "chronicle " + std::to_string(e.id);
	for (size_t r = 0; r < w.rivers.size(); r++)
		for (float fl : w.rivers[r].flow) if (!std::isfinite(fl)) return "rivers[" + std::to_string(r) + "]";
	return std::string();
}

bool isYearlyTick(const World& w) {
	return yearlyAt(w, w.tick + 1);
}

int64_t runTicks(World& w, int64_t ticks, const RunHooks& hooks, bool stopAtEnding) {
	int64_t done = 0;
	for (int64_t i = 0; i < ticks; i++) {
		if (stopAtEnding && w.ending.has_value()) break;
		if (hooks.before) hooks.before(w);
		const bool yearly = isYearlyTick(w);
		advance(w, 1);
		done++;
		if (hooks.after) hooks.after(w);
		if (yearly && hooks.yearly) hooks.yearly(w);
	}
	return done;
}

} // namespace sim
} // namespace dm
