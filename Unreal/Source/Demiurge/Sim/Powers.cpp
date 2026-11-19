// DEMIURGE — Sim/Powers.cpp : the god's powers: table rules, cost, can, use and the instant effects
// (SPEC-SIM 3.10, first half: powers.js). Owner: powers engineer (A11, key "powers").
// The disasters half (start, update, tick, natural) lives in Disasters.cpp.
//
// What each power does to world state at once (the player's hands):
//   comet / meteor / volcano  hand off to powers::start (a Disaster entity in w.disasters); refused -> false
//   seed      life::seed (veg)                        awaken   society::newCulture + civ::found(30, Awaken)
//   raise / lower  terrain::sculpt (elev) at 1.5 u/s in genesis, 0.2 u/s in history; drowned centres destroyed
//   rain      wet toward 1 and fert up within the brush (fires go out at the next life::burn: wet > 0.3);
//             need.climate, dirty.color
//   bloom     life::bloom (veg, fert)
//   lightning lightning event, life::ignite, settlements at / next to the cell lose 5-12 %, units there die
//   quake     quake event, settlements within 0.08 rad lose 15-35 %, terrain::roughen, units at the
//             epicentre die, under the sea powers::start(Tsunami)
//   plague    civ::infect(0.6)      heal  civ::cure + blessed = 30      inspire  inspire = 40
//   prophet   society::raiseProphet          peace  society::makePeace (80-year truce) + relations +0.4
//
// Real time: brush pacing counts only the dt passed to use(); the awe:short clock counts those dt
// plus elapsed ticks at the fastest game speed (no clock is ever read). Clocks live in scratch.
#include "Powers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "Civ.h"
#include "Life.h"
#include "Sim.h"
#include "Society.h"
#include "Terrain.h"
#include "Units.h"

namespace dm::powers {

namespace {

// ------------------------------------------------------------------ private state (never saved)

struct BrushPace {
	double pending = 0;          // real seconds of faith not yet witnessed
	double pace = 0;             // rate-limit clock toward the next power:used (seconds)
	double lastEmit = -1e9;      // PowersScratch::clock at the last power:used of this brush
	bool shortInStroke = false;  // awe:short already emitted during this press
	std::vector<int> harmed;     // sorted unique settlement ids for the special good of the window
};

struct PowersScratch {
	double clock = 0;                                   // real seconds as far as Powers can tell
	bool haveTick = false;
	int64_t lastTick = 0;
	int lastPower = -1;                                 // index of the power of the previous use() call
	std::array<double, kPowerCount> lastShort{};        // clock at the last awe:short, per power
	std::array<BrushPace, kPowerCount> brush{};
	std::vector<int32_t> cells;                         // geo.within output
	bool init = false;
};

PowersScratch& scr(const World& w) {
	PowersScratch& S = w.scratch<PowersScratch>();
	if (!S.init) {
		S.lastShort.fill(-1e9);
		S.init = true;
	}
	return S;
}

int indexOf(const PowerDef* d) { return d ? int(d - kPowers) : -1; }
bool is(const PowerDef& d, const char* id) { return std::string_view(d.id) == id; }
double finiteOr(double x, double alt) { return std::isfinite(x) ? x : alt; }

bool sea(const World& w, int c) { return state::isSea(w, c); }

bool reached(const World& w, Phase p) { return int(w.phase) >= int(p); }

// no live settlement and no settler on the move (awaken is free then)
bool nobodyAlive(const World& w) {
	if (!civ::live(w).empty()) return false;
	for (const Unit& u : w.units) if (u.type == UnitType::Settler) return false;
	return true;
}

// Phase / Locked / Ok for a definition (the unlock rules of SPEC-SIM 3.10)
Reason phaseLock(const World& w, const PowerDef& d) {
	const Unlock& u = d.unlock;
	if (u.phaseOnly) return w.phase == u.phase ? Reason::Ok : Reason::Phase;
	if (!reached(w, u.phase)) return Reason::Phase;
	if (is(d, "awaken")) {
		if (w.phase == Phase::People) return Reason::Ok;
		if (nobodyAlive(w)) return Reason::Ok;
	}
	const bool ageOk = w.age >= u.age;
	const bool aweOk = u.awe > 0 && w.god.aweMax >= u.awe;
	return (ageOk || aweOk) ? Reason::Ok : Reason::Locked;
}

// cultures at war with cultureId, ascending
std::vector<int> enemiesOf(const World& w, int cultureId) {
	std::vector<int> out;
	for (const auto& kv : w.vars.rel) {
		if (!kv.second.war) continue;
		if (kv.first.first == cultureId && kv.first.second != cultureId) out.push_back(kv.first.second);
		else if (kv.first.second == cultureId && kv.first.first != cultureId) out.push_back(kv.first.first);
	}
	std::sort(out.begin(), out.end());
	out.erase(std::unique(out.begin(), out.end()), out.end());
	return out;
}

struct TargetCheck {
	bool ok = false;
	Why why = Why::None;
	int sid = -1;                // the target settlement (Target::Settlement)
};

TargetCheck checkTarget(const World& w, const PowerDef& d, int cell) {
	TargetCheck r;
	if (!w.valid(cell) || !w.geo) return r;
	switch (d.target) {
		case Target::Any: break;
		case Target::Land: if (sea(w, cell)) { r.why = Why::Sea; return r; } break;
		case Target::Sea: if (!sea(w, cell)) { r.why = Why::Land; return r; } break;
		case Target::Settlement: {
			r.sid = civ::at(w, cell);
			const Settlement* s = r.sid >= 0 ? state::settlement(w, r.sid) : nullptr;
			if (!s || s->dead) { r.sid = -1; r.why = Why::Settlement; return r; }
			break;
		}
	}
	if (is(d, "awaken")) {
		const civ::SettleCheck sc = civ::canSettle(w, cell);
		if (!sc.ok) {
			switch (sc.reason) {
				case civ::SettleReason::Sea: r.why = Why::Sea; break;
				case civ::SettleReason::Barren: r.why = Why::Barren; break;
				case civ::SettleReason::Crowded: r.why = Why::Crowded; break;
				default: r.why = Why::Sea; break;
			}
			return r;
		}
		// civ::found refuses past CivTune::maxSettlements live settlements; refuse here, before
		// society::newCulture, or every click would leave a culture without people (A11 verifier fix)
		if (int(civ::live(w).size()) >= w.tune<CivTune>().maxSettlements) { r.why = Why::Crowded; return r; }
		const auto& ids = w.cache.landId;
		if (ids.size() == size_t(w.n)) {
			const int lm = ids[size_t(cell)];
			if (lm >= 0 && size_t(lm) < w.cache.landHab.size() &&
				w.cache.landHab[size_t(lm)] < w.tune<PowersTune>().awakenSmall)
				r.why = Why::Small;
		}
	}
	if (is(d, "peace")) {
		const Settlement* s = state::settlement(w, r.sid);
		if (!s || enemiesOf(w, s->culture).empty()) { r.why = Why::None; return r; }
	}
	r.ok = true;
	return r;
}

// the effective radius of a use
double effAng(const World& w, const PowerDef& d, double ang) {
	const PowersTune& T = w.tune<PowersTune>();
	if (d.brush) {
		if (!(ang > 0) || !std::isfinite(ang)) return d.ang;
		return clamp(ang, T.brushMinAng, T.brushMaxAng);
	}
	if (is(d, "seed") && ang > 0 && std::isfinite(ang)) return clamp(ang, T.seedMinAng, T.seedMaxAng);
	return d.ang;
}

double payable(const World& w) { return finiteOr(w.god.awe, 0); }

void spend(World& w, double price) {
	if (!(price > 0)) return;
	const double a = payable(w) - price;
	w.god.awe = a > 0 ? a : 0;
}

void countDeed(World& w, const PowerDef& d) {
	w.god.deeds[std::string(d.id)]++;
	w.stats.miracles++;
}

// awe:short, at most once per press and once per aweShortEvery seconds of the Powers clock per power
void aweShort(World& w, PowersScratch& S, const PowerDef& d, int idx) {
	const PowersTune& T = w.tune<PowersTune>();
	BrushPace& B = S.brush[size_t(idx)];
	if (d.brush && B.shortInStroke) return;
	if (S.clock - S.lastShort[size_t(idx)] < T.aweShortEvery - 1e-9) return;
	S.lastShort[size_t(idx)] = S.clock;
	if (d.brush) B.shortInStroke = true;
	w.bus.emit(EvAweShort{ std::string(d.id) });
}

void addSorted(std::vector<int>& v, int id) {
	auto it = std::lower_bound(v.begin(), v.end(), id);
	if (it == v.end() || *it != id) v.insert(it, id);
}

std::vector<int> sortedNear(const World& w, int cell, double ang) {
	std::vector<int> v = civ::near(w, cell, ang);
	std::sort(v.begin(), v.end());
	return v;
}

void killUnitsNear(World& w, int cell, double ang, const char* cause) {
	const std::vector<int> ids = units::near(w, cell, ang);
	for (int id : ids) units::kill(w, id, cause);
}

// settlements whose centre the sea now covers are drowned
void drownCovered(World& w, int cell, double ang) {
	for (int sid : sortedNear(w, cell, ang)) {
		const Settlement* s = state::settlement(w, sid);
		if (s && !s->dead && w.valid(s->cell) && sea(w, s->cell)) civ::destroy(w, sid, Cause::Drowned);
	}
}

// ------------------------------------------------------------------ brush effects

void sculptBrush(World& w, PowersScratch& S, const PowerDef& d, int idx, int cell, double ang, double dt) {
	const PowersTune& T = w.tune<PowersTune>();
	const double rate = w.phase < Phase::History ? T.sculptGenesis : T.sculptHistory;
	const double amount = rate * dt * (is(d, "lower") ? -1.0 : 1.0);
	if (!(std::fabs(amount) > 0)) return;
	// whose land the brush touches (good -0.5 for them)
	const int cnt = w.geo->within(cell, ang, S.cells);
	BrushPace& B = S.brush[size_t(idx)];
	for (int j = 0; j < cnt; j++) {
		const int o = w.owner[size_t(S.cells[size_t(j)])];
		if (o >= 0) addSorted(B.harmed, o);
	}
	terrain::sculpt(w, cell, ang, amount);
	drownCovered(w, cell, ang);
}

void rainBrush(World& w, PowersScratch& S, int cell, double ang, double dt) {
	const PowersTune& T = w.tune<PowersTune>();
	if (!(dt > 0)) return;
	const Geo& g = *w.geo;
	const int cnt = g.within(cell, ang, S.cells);
	bool any = false;
	for (int j = 0; j < cnt; j++) {
		const int m = S.cells[size_t(j)];
		if (sea(w, m)) continue;
		const double t = ang > 0 ? g.angle(cell, m) / ang : 0;
		const double fall = t >= 1 ? 0 : (1 - t * t) * (1 - t * t);
		if (!(fall > 0)) continue;
		const size_t mi = size_t(m);
		const double k = std::min(1.0, T.rainRate * fall * dt);
		const double wt = finiteOr(w.wet[mi], 0);
		w.wet[mi] = float(clamp(wt + (T.rainWet - wt) * k, 0.0, 1.0));
		const double f = finiteOr(w.fert[mi], 0.5);
		if (f < T.rainFertCap) w.fert[mi] = float(std::min(T.rainFertCap, f + T.rainFert * fall * dt));
		any = true;
	}
	if (any) {
		w.dirty.color = true;
		w.need.climate = true;
	}
}

bool inDrought(const World& w, int c) {
	for (const Disaster& z : w.disasters)
		if (z.type == DisasterType::Drought && w.valid(z.cell) && w.geo->angle(z.cell, c) <= z.ang) return true;
	return false;
}

// rain witnesses that get the big good: dry, burning, drought-struck or praying for rain
std::vector<int> rainGlad(const World& w, int cell, double wAng) {
	const PowersTune& T = w.tune<PowersTune>();
	std::vector<int> out;
	for (int sid : sortedNear(w, cell, wAng)) {
		const Settlement* s = state::settlement(w, sid);
		if (!s || s->dead || !w.valid(s->cell)) continue;
		bool glad = w.moist[size_t(s->cell)] < T.rainDry || w.fire[size_t(s->cell)] > 0 || inDrought(w, s->cell) ||
			(s->prayer && s->prayer->kind == PrayerKind::Rain);
		if (!glad) for (int c : s->cells) if (w.valid(c) && w.fire[size_t(c)] > 0) { glad = true; break; }
		if (glad) out.push_back(sid);
	}
	return out;
}

// ------------------------------------------------------------------ click effects

struct Act {
	bool ok = false;
	bool witness = true;
	society::WitnessOpts wo;
	std::vector<int> cultureWitness;   // Omen of Peace: every live settlement of these cultures witnesses
};

Act clickEffect(World& w, const PowerDef& d, int cell, double ang, int sid) {
	const PowersTune& T = w.tune<PowersTune>();
	const DisastersTune& D = w.tune<DisastersTune>();
	Act a;
	a.wo.kind = d.id;
	a.wo.faith = d.faith;
	a.witness = d.wScope != WScope::None;
	const std::string_view id = d.id;

	if (id == "comet" || id == "meteor" || id == "volcano") {
		const DisasterType type = id == "comet" ? DisasterType::Comet : id == "meteor" ? DisasterType::Meteor : DisasterType::Volcano;
		// the volcano is witnessed now (harmed: settlements in the ash zone); the meteor defers its
		// witness to the impact (Disasters' data["witness"] = 1 contract: exact harmed list then)
		std::vector<int> harmed;
		if (type == DisasterType::Volcano) harmed = sortedNear(w, cell, D.ashAngMul * ang);
		StartOpts so;
		so.ang = ang;
		so.natural = false;
		if (type == DisasterType::Meteor) so.data["witness"] = 1;
		Disaster* z = start(w, type, cell, so);
		if (!z) return a;
		if (type != DisasterType::Volcano) a.witness = false;
		a.wo.good = 0;
		a.wo.harmed = std::move(harmed);
		a.wo.goodHarmed = -1;
		a.ok = true;
		return a;
	}
	if (id == "seed") {
		life::seed(w, cell, ang);
		a.ok = true;
		return a;
	}
	if (id == "awaken") {
		society::NewCultureOpts no;
		no.parent = -1;
		no.cell = cell;
		no.reason = BirthReason::Awaken;
		Culture* cu = society::newCulture(w, no);
		if (!cu) return a;
		const int cid = cu->id;
		Settlement* s = civ::found(w, cell, cid, T.awakenPop, FoundedBy::Awaken);
		if (!s) return a;
		a.ok = true;
		return a;
	}
	if (id == "lightning") {
		w.bus.emit(EvLightning{ cell, false });
		life::ignite(w, cell, 1);
		// settlements centred at or next to the cell
		const Geo& g = *w.geo;
		std::vector<int> hit;
		for (int s2 : sortedNear(w, cell, 3 * g.step)) {
			const Settlement* s = state::settlement(w, s2);
			if (!s || s->dead) continue;
			bool adj = s->cell == cell;
			for (int k = 0; !adj && k < g.nbrN[size_t(cell)]; k++) adj = g.nbr[size_t(6 * cell + k)] == s->cell;
			if (adj) hit.push_back(s2);
		}
		for (int s2 : hit) civ::damage(w, s2, w.rng.range(T.lightningLo, T.lightningHi), Cause::Lightning);
		killUnitsNear(w, cell, 0.5 * g.step, "lightning");
		a.wo.good = 0;
		a.wo.harmed = hit;
		a.wo.goodHarmed = -0.6;
		a.ok = true;
		return a;
	}
	if (id == "quake") {
		const bool underSea = sea(w, cell);
		w.bus.emit(EvQuake{ cell, ang, 1.0, false });
		const std::vector<int> hit = sortedNear(w, cell, ang);
		for (int s2 : hit) civ::damage(w, s2, w.rng.range(T.quakeLo, T.quakeHi), Cause::Quake);
		terrain::roughen(w, cell, ang, T.quakeRough, w.rng);
		killUnitsNear(w, cell, T.quakeUnitAng * ang, "quake");
		if (underSea) start(w, DisasterType::Tsunami, cell, StartOpts{});
		drownCovered(w, cell, ang);
		a.wo.good = -0.2;
		a.wo.harmed = hit;
		a.wo.goodHarmed = -0.8;
		a.ok = true;
		return a;
	}
	if (id == "plague") {
		if (!civ::infect(w, sid, T.plagueSeverity)) return a;
		a.wo.good = -0.7;
		a.wo.harmed = { sid };
		a.wo.goodHarmed = -0.7;
		a.ok = true;
		return a;
	}
	if (id == "heal") {
		civ::cure(w, sid, 30);
		Settlement* s = state::settlement(w, sid);
		if (!s || s->dead) return a;
		s->blessed = std::max(s->blessed, T.blessYears);
		a.wo.good = 1;
		a.ok = true;
		return a;
	}
	if (id == "inspire") {
		Settlement* s = state::settlement(w, sid);
		if (!s || s->dead) return a;
		s->inspire = std::max(s->inspire, T.inspireYears);
		a.wo.good = 0.6;
		a.ok = true;
		return a;
	}
	if (id == "prophet") {
		if (!society::raiseProphet(w, sid)) return a;
		a.wo.good = 0.5;
		a.ok = true;
		return a;
	}
	if (id == "peace") {
		const Settlement* s = state::settlement(w, sid);
		if (!s) return a;
		const int cu = s->culture;
		const std::vector<int> foes = enemiesOf(w, cu);
		if (foes.empty()) return a;
		for (int other : foes) {
			society::makePeace(w, cu, other, T.peaceTruce, -1);
			if (Relation* r = society::rel(w, cu, other)) r->v = clamp(finiteOr(r->v, 0) + T.peaceRel, -1.0, 1.0);
		}
		a.wo.good = 0.8;
		a.cultureWitness = foes;
		a.cultureWitness.push_back(cu);
		std::sort(a.cultureWitness.begin(), a.cultureWitness.end());
		a.ok = true;
		return a;
	}
	return a;
}

void witnessClick(World& w, const PowerDef& d, int cell, Act& a) {
	if (!a.witness) return;
	if (d.wScope == WScope::Cultures) {
		// every live settlement of the cultures involved witnesses at its own centre
		const double tiny = 0.5 * w.geo->step;
		for (int sid : civ::live(w)) {
			const Settlement* s = state::settlement(w, sid);
			if (!s || s->dead) continue;
			if (!std::binary_search(a.cultureWitness.begin(), a.cultureWitness.end(), s->culture)) continue;
			society::witness(w, s->cell, tiny, a.wo);
		}
		return;
	}
	society::witness(w, cell, d.wAng, a.wo);
}

} // namespace

// ================================================================== public API

bool unlocked(const World& w, std::string_view id) {
	const PowerDef* d = byId(id);
	return d && phaseLock(w, *d) == Reason::Ok;
}

double cost(const World& w, std::string_view id, double ang, int cell) {
	const PowerDef* d = byId(id);
	if (!d) return 0;
	const PowersTune& T = w.tune<PowersTune>();
	if (d->brush) {
		const double a = effAng(w, *d, ang);
		double c = d->ang > 0 ? d->cost * (a / d->ang) * (a / d->ang) : d->cost;
		if (is(*d, "raise") || is(*d, "lower")) {
			if (w.phase < Phase::History) return 0;
			if (w.valid(cell) && w.owner[size_t(cell)] >= 0) c *= T.ownedCostMul;
		}
		return finiteOr(c, 0);
	}
	if (is(*d, "awaken")) {
		if (w.phase < Phase::History) return 0;
		if (nobodyAlive(w)) return 0;
		return std::max(0.0, std::min(T.awakenMaxCost, finiteOr(w.god.aweMax, 0)));
	}
	return d->cost;
}

CanResult can(const World& w, std::string_view id, int cell) {
	CanResult r;
	const PowerDef* d = byId(id);
	if (!d) return r;                                   // { false, Locked }
	const Reason pl = phaseLock(w, *d);
	if (pl != Reason::Ok) { r.reason = pl; return r; }
	const TargetCheck tc = checkTarget(w, *d, cell);
	r.why = tc.why;
	if (!tc.ok) { r.reason = Reason::Target; return r; }
	const double c = cost(w, id, -1, cell);
	const double need = d->brush ? c * w.tune<PowersTune>().brushMinPay : c;
	if (payable(w) + 1e-9 < need) { r.reason = Reason::Awe; return r; }
	r.ok = true;
	r.reason = Reason::Ok;
	return r;
}

bool use(World& w, std::string_view id, int cell, const UseOpts& opts) {
	const PowerDef* dp = byId(id);
	if (!dp || !w.geo) return false;
	const PowerDef& d = *dp;
	const int idx = indexOf(dp);
	const PowersTune& T = w.tune<PowersTune>();
	PowersScratch& S = scr(w);
	BrushPace& B = S.brush[size_t(idx)];

	// ---- the clocks (real seconds only from dt; ticks at the fastest speed as a floor)
	const double rawDt = finiteOr(opts.dt, 0);
	const double dt = d.brush ? clamp(rawDt, 0.0, T.brushDtMax) : 0.0;
	double elapsed = clamp(rawDt, 0.0, 10.0);
	if (S.haveTick && w.tick > S.lastTick) {
		const double speed = T.maxGameSpeed > 1 ? T.maxGameSpeed : 1;
		elapsed = std::max(elapsed, double(w.tick - S.lastTick) * sim::kRealPerTick / speed);
	}
	S.clock += elapsed;
	S.haveTick = true;
	S.lastTick = w.tick;
	if (d.brush) {
		const bool newPress = S.lastPower != idx || !(rawDt > 0) || rawDt > T.strokeGap;
		if (newPress) {
			// faith of the last (< 1 / brushRate s) of the previous press is not carried to a new place
			B.pending = 0;
			B.harmed.clear();
			B.shortInStroke = false;
			if (S.clock - B.lastEmit >= 1 / T.brushRate - 1e-9) B.pace = 1 / T.brushRate;   // first frame shows at once
		}
	}
	S.lastPower = idx;

	// ---- may it be used here?
	if (phaseLock(w, d) != Reason::Ok) return false;
	const TargetCheck tc = checkTarget(w, d, cell);
	if (!tc.ok) return false;
	const double ang = effAng(w, d, opts.ang);
	const double perUse = d.brush ? cost(w, id, ang, cell) : cost(w, id, -1, cell);
	const double price = d.brush ? perUse * dt : perUse;
	// (a brush frame that costs nothing because dt is 0 still needs some Awe if the power is not free)
	const bool broke = payable(w) + 1e-9 < price || (d.brush && perUse > 0 && !(payable(w) > 0));
	if (broke) {
		aweShort(w, S, d, idx);
		return false;
	}

	if (d.brush) {
		spend(w, price);
		if (is(d, "raise") || is(d, "lower")) sculptBrush(w, S, d, idx, cell, ang, dt);
		else if (is(d, "rain")) rainBrush(w, S, cell, ang, dt);
		else if (is(d, "bloom")) { if (dt > 0) life::bloom(w, cell, ang, T.bloomRate * dt); }
		// pacing: at most brushRate power:used / deeds / witnesses per real second, with the faith of
		// every second held since the last one
		B.pending += dt;
		B.pace += dt;
		const double quantum = 1 / T.brushRate;
		if (B.pace >= quantum - 1e-9 && B.pending > 0) {
			w.bus.emit(EvPowerUsed{ std::string(d.id), cell, ang, true });
			countDeed(w, d);
			const double wAng = d.wAng * ang;
			society::WitnessOpts wo;
			wo.kind = d.id;
			wo.faith = d.faith * B.pending;
			if (is(d, "rain")) {
				wo.good = T.rainGood;
				wo.harmed = rainGlad(w, cell, wAng);
				wo.goodHarmed = T.rainGoodDry;
			} else if (is(d, "bloom")) {
				wo.good = 0.4;
			} else {
				wo.good = 0;
				wo.harmed = B.harmed;
				wo.goodHarmed = T.sculptHarm;
			}
			society::witness(w, cell, wAng, wo);
			B.pending = 0;
			B.harmed.clear();
			B.pace = 0;               // no carry: consecutive power:used are always >= 1 / brushRate apart
			B.lastEmit = S.clock;
		}
		return true;
	}

	// ---- click powers: effect, then pay, power:used, deed, witness
	const bool firstAwaken = is(d, "awaken") && w.phase == Phase::People;
	Act a = clickEffect(w, d, cell, ang, tc.sid);
	if (!a.ok) return false;
	spend(w, price);
	if (firstAwaken) {
		sim::setPhase(w, Phase::History);
		w.god.awe = std::max(payable(w), T.awakenAwe);
	}
	w.bus.emit(EvPowerUsed{ std::string(d.id), cell, ang, false });
	countDeed(w, d);
	witnessClick(w, d, cell, a);
	return true;
}

} // namespace dm::powers
