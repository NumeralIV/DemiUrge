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
//
// v3 (design review, docs/reviews/claude-design-review.md): every power past Bronze unlocks by age
// only, and the late ages upgrade one (Monsoon, Star-fall); Inspire teaches at once (+2
// progress-years) and wrath that hits a capital or an academy sets research back; an awakening wakes
// two bands; Consecrate buys durable things (a temple; a Wonder in a capital, once an age); Rain and
// Bloom cost double off every territory; one press of Lower Land cannot drown a town (drown:warning).
//
// v4 mythos (track M7 "powers-v4", SPEC-MYTHOS 2.1, 3.8, 3.9, 9): the Word group (speak, bless, reveal, test, renew)
// is Locked while MythosTune::phase < its row's unlock.mythos (phase 0: all five, v3.1 bit for bit). speak hands
// UseOpts::word to doctrine::canSpeak / speak; bless, test, reveal and renew to mythos::bless, characters::test,
// doctrine::reveal and renewal::begin. Powers pays each one itself, only after the module accepted it (nothing is
// spent on a refused word), then emits power:used and counts the deed. can() of a Word power checks the target
// (speak: the people's 10-year gap -> Target / None; test: a living ruler, messenger, healer or hero at the town;
// renew: once per world) and the Awe of its cheapest choice; cost() shows that cheapest choice. prophet with
// UseOpts::choose at phase >= 1 opens the MessengerChoice card instead (3.8; nothing spent until it is answered).
// powers::pay spends Awe for the other mythos acts (cards, Look into the heart, presence), awe:short per act.
// While the god has withdrawn (3.29, phase >= 3) every power refuses with Reason::Withdrawn.
#include "Powers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "Cards.h"
#include "Characters.h"
#include "Civ.h"
#include "Doctrine.h"
#include "Life.h"
#include "Mythos.h"
#include "Renewal.h"
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
	// (v3) Lower Land in history: settlements this press has reached, and whether it may drown each
	// (only if its centre was already at the floor when the press first reached it); warned ones
	std::vector<std::pair<int, bool>> reached;
	std::vector<int> warned;
};

struct PowersScratch {
	double clock = 0;                                   // real seconds as far as Powers can tell
	bool haveTick = false;
	int64_t lastTick = 0;
	int lastPower = -1;                                 // index of the power of the previous use() call
	std::array<double, kPowerCount> lastShort{};        // clock at the last awe:short, per power
	std::array<BrushPace, kPowerCount> brush{};
	std::vector<int32_t> cells;                         // geo.within output
	std::map<std::string, double, std::less<>> payShort; // (v4 mythos, M7) clock at the last awe:short of pay(), per act
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
	// (v4 mythos, M0) the Word powers need their mythos phase (MythosTune::phase 0 = locked, as before mythos)
	if (u.mythos > 0 && w.tune<MythosTune>().phase < u.mythos) return Reason::Locked;
	if (is(d, "awaken")) {
		if (w.phase == Phase::People) return Reason::Ok;
		if (nobodyAlive(w)) return Reason::Ok;
	}
	int needAge = u.age;
	// (chronicle-diplomacy) DiplomacyTune::omenAge: the Omen of Peace may wake earlier than its table's age
	if (is(d, "peace") && w.tune<DiplomacyTune>().omenAge >= 0) needAge = w.tune<DiplomacyTune>().omenAge;
	const bool ageOk = w.age >= needAge;
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

const Upgrade* upgradeOf(const World& w, std::string_view id);
int consecrateRite(const World& w, int sid);

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
		// (v3.1, design review 11) an Omen may also calm a neighbour on the brink of war (society::tense)
		if (!s || (enemiesOf(w, s->culture).empty() && society::tense(w, s->culture).empty())) { r.why = Why::None; return r; }
	}
	if (is(d, "consecrate") && consecrateRite(w, r.sid) == 0) { r.why = Why::None; return r; }
	r.ok = true;
	return r;
}

// the effective radius of a use
double effAng(const World& w, const PowerDef& d, double ang) {
	const PowersTune& T = w.tune<PowersTune>();
	if (d.brush) {
		if (!(ang > 0) || !std::isfinite(ang)) return d.ang;
		const double hi = is(d, "rain") && upgradeOf(w, "rain") ? std::max(T.brushMaxAng, T.monsoonMaxAng) : T.brushMaxAng;
		return clamp(ang, T.brushMinAng, hi);
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

// (v3) the age upgrade of a power, or nullptr
const Upgrade* upgradeOf(const World& w, std::string_view id) {
	for (const Upgrade& u : kUpgrades)
		if (id == u.id && w.age >= u.age && w.phase >= Phase::History) return &u;
	return nullptr;
}

// (v3) does a brush of ang at cell cover any settlement's territory?
bool coversTerritory(const World& w, int cell, double ang) {
	PowersScratch& S = scr(w);
	const int cnt = w.geo->within(cell, ang, S.cells);
	for (int j = 0; j < cnt; j++)
		if (w.owner[size_t(S.cells[size_t(j)])] >= 0) return true;
	return false;
}

// (v3) Consecrate on a settlement: 1 = a temple (it has a shrine, no temple), 2 = a Wonder (a capital
// with a temple and no Wonder, no Wonder raised by the god in this age yet), 0 = nothing to do
int consecrateRite(const World& w, int sid) {
	const Settlement* s = state::settlement(w, sid);
	if (!s || s->dead) return 0;
	if (s->has(Building::SHRINE) && !s->has(Building::TEMPLE)) return 1;
	const Culture* c = state::culture(w, s->culture);
	if (s->has(Building::TEMPLE) && !s->has(Building::WONDER) && c && c->capital == sid) {
		const auto it = w.vars.num.find("powers_wonderAge");
		if (it == w.vars.num.end() || !(it->second >= w.age)) return 2;
	}
	return 0;
}

// (v3) the research setback of a wrath act that hurt these settlements
void setBack(World& w, const char* id, const std::vector<int>& harmed) {
	const PowersTune& T = w.tune<PowersTune>();
	const std::string_view k = id;
	const double years = k == "lightning" ? T.setbackLightning : k == "quake" ? T.setbackQuake : k == "plague" ? T.setbackPlague
		: k == "meteor" ? T.setbackMeteor : k == "volcano" ? T.setbackVolcano : 0.0;
	if (years > 0) civ::strikeKnowledge(w, harmed, years);
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
	double amt = amount;
	// (v3, design review finding 10) in history one press of Lower Land stops a settlement's centre at
	// sea level + drownFloor (drown:warning); only a press that reached it already there may drown it
	// (verifier fix) only a town that was already warned (drown:warning, remembered in vars.num
	// "powers_drownWarned_<sid>") and still sits at the floor when a press reaches it may drown: a town
	// the rising sea brought to the floor is warned by its first press too, not drowned
	std::vector<std::pair<int, double>> guard;           // settlement, its centre's height before this frame
	if (w.phase >= Phase::History && amount < 0) {
		const double floorH = w.seaLevel + T.drownFloor;
		for (int sid : sortedNear(w, cell, ang)) {
			const Settlement* s = state::settlement(w, sid);
			if (!s || s->dead || !w.valid(s->cell)) continue;
			auto it = std::find_if(B.reached.begin(), B.reached.end(), [&](const std::pair<int, bool>& p) { return p.first == sid; });
			if (it == B.reached.end()) {
				const bool atFloor = double(w.elev[size_t(s->cell)]) <= floorH + 1e-3;
				B.reached.push_back({ sid, atFloor && w.vars.num.count("powers_drownWarned_" + std::to_string(sid)) > 0 });
				it = B.reached.end() - 1;
			}
			if (it->second) continue;                    // a deliberate second press: it may drown
			const double h0 = double(w.elev[size_t(s->cell)]);
			guard.push_back({ sid, h0 });
			const double t = ang > 0 ? w.geo->angle(cell, s->cell) / ang : 0;
			const double f = t >= 1 ? 0 : (1 - t * t) * (1 - t * t);
			const double room = h0 - floorH;
			if (f > 1e-9) amt = std::max(amt, room > 0 ? -room / f : 0.0);
		}
	}
	if (amt < 0 || amt > 0) terrain::sculpt(w, cell, ang, amt);
	for (const auto& [sid, h0] : guard) {
		const Settlement* s = state::settlement(w, sid);
		if (!s || s->dead) continue;
		const double floorH = w.seaLevel + T.drownFloor;
		const double keep = std::min(floorH, h0);        // never raised by Lower Land, never sunk below the floor
		const double h = w.elev[size_t(s->cell)];
		if (h < keep) terrain::addElev(w, s->cell, keep - h);   // the chord falloff of sculpt may differ a hair
		if (double(w.elev[size_t(s->cell)]) <= floorH + 1e-3 && std::find(B.warned.begin(), B.warned.end(), sid) == B.warned.end()) {
			B.warned.push_back(sid);
			w.vars.num["powers_drownWarned_" + std::to_string(sid)] = 1;
			w.bus.emit(EvDrownWarning{ sid, s->cell });
		}
	}
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
		if (type == DisasterType::Volcano) setBack(w, "volcano", harmed);   // (the meteor's comes at impact)
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
		// (v3, design review finding 4) a second band wakes a few days' walk away
		const int site = civ::bandSite(w, cell);
		if (site >= 0) {
			civ::FoundOpts fo;
			fo.band = true;
			civ::found(w, site, cid, w.tune<CivTune>().bandPop, FoundedBy::Awaken, fo);
		}
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
		setBack(w, "lightning", hit);   // (before the damage: a capital it destroys still counts)
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
		setBack(w, "quake", hit);
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
		setBack(w, "plague", { sid });
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
		civ::addScience(w, s->culture, T.inspireScience);   // (v3, design review finding 3)
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
	if (id == "consecrate") {
		const int rite = consecrateRite(w, sid);
		if (rite == 1 && !civ::consecrate(w, sid)) return a;
		if (rite == 2) {
			if (!society::raiseWonder(w, sid)) return a;
			w.vars.num["powers_wonderAge"] = w.age;
		}
		if (rite == 0) return a;
		a.wo.good = 0.5;
		a.ok = true;
		return a;
	}
	if (id == "peace") {
		const Settlement* s = state::settlement(w, sid);
		if (!s) return a;
		const int cu = s->culture;
		const std::vector<int> foes = enemiesOf(w, cu);
		const DiplomacyTune& DT = w.tune<DiplomacyTune>();
		// (v3.1, design review 11) no war yet: the Omen calms the neighbours on the brink of one;
		// (chronicle-diplomacy, omenWide) an Omen that ends wars calms the people's other tense neighbours too
		const std::vector<int> calm = (foes.empty() || DT.omenWide) ? society::tense(w, cu) : std::vector<int>{};
		if (foes.empty() && calm.empty()) return a;
		for (int other : foes) {
			society::makePeace(w, cu, other, T.peaceTruce, -1);
			if (Relation* r = society::rel(w, cu, other)) r->v = clamp(finiteOr(r->v, 0) + T.peaceRel, -1.0, 1.0);
			society::swearPeace(w, cu, other);       // (v3.1) a sworn peace (omenSworn)
		}
		for (int other : calm) {
			society::makePeace(w, cu, other, DT.omenCalmTruce, -1);   // (at peace: only the truce)
			if (Relation* r = society::rel(w, cu, other)) r->v = clamp(finiteOr(r->v, 0) + T.peaceRel, -1.0, 1.0);
			society::swearPeace(w, cu, other);
		}
		a.wo.good = foes.empty() ? DT.omenCalmGood : 0.8;   // (v3.1) a war averted is felt less than a war ended
		a.wo.favour = !DT.omenNoEnvy;                       // (chronicle-diplomacy) a peace no neighbour envies
		a.cultureWitness = foes;
		a.cultureWitness.insert(a.cultureWitness.end(), calm.begin(), calm.end());
		a.cultureWitness.push_back(cu);
		std::sort(a.cultureWitness.begin(), a.cultureWitness.end());
		a.cultureWitness.erase(std::unique(a.cultureWitness.begin(), a.cultureWitness.end()), a.cultureWitness.end());
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

// ------------------------------------------------------------------ (v4 mythos, M7) the Word powers

// What Powers knows of the mythos systems (SPEC-MYTHOS 2.1, 3.8, 3.9, 3.16, 3.18, 3.25, 3.29, 3.30). Every Word power
// is Locked while MythosTune::phase < its unlock.mythos (phaseLock), so at phase 0 nothing below is ever reached and
// no mythos module is ever called from here (v3.1 bit for bit). Powers pays every Word power itself, after the owning
// module accepted the act (doctrine::speak, mythos::bless, characters::test, doctrine::reveal, renewal::begin must
// NOT spend Awe themselves): a refused word costs nothing.

int mythPhase(const World& w) { return w.tune<MythosTune>().phase; }

// (3.29) a century of silence: every power refuses with Reason::Withdrawn (a v3 system: phase >= 3 only)
bool withdrawnNow(const World& w) {
	if (mythPhase(w) < 3) return false;
	const double u = w.mythos.withdrawnUntil;
	return std::isfinite(u) && u > w.year + 1e-9;
}

int cultureAt(const World& w, int sid) {
	const Settlement* s = state::settlement(w, sid);
	return s && !s->dead ? s->culture : -1;
}

// the probe word of can() / cost(): an Answer by Dream is valid content for every culture, so canSpeak only reports
// what does not depend on the chooser (phase, the people, the 10-year gap)
WordOpts probeWord(Clarity c = Clarity::Dream) {
	WordOpts o;
	o.content = WordContent::Answer;
	o.clarity = c;
	o.channel = c == Clarity::Dream ? Channel::Dream : c == Clarity::Sign ? Channel::Sky : Channel::Messenger;
	return o;
}

// the cheapest word for this culture (3.9 table x presence); 0 when Doctrine names no price
double cheapestWord(const World& w, int cid) {
	double best = std::numeric_limits<double>::infinity();
	for (int k = 0; k < enumCount<Clarity>(); k++) {
		const double c = doctrine::wordCost(w, cid, probeWord(Clarity(k)));
		if (std::isfinite(c) && c > 0 && c < best) best = c;
	}
	return std::isfinite(best) ? best : 0.0;
}

double blessCost(const World& w, BlessingKind k) {
	const MythosTune& M = w.tune<MythosTune>();
	switch (k) {
	case BlessingKind::Prosperity: return M.prosperityCost;
	case BlessingKind::Healer: return M.healerCost;
	case BlessingKind::Endurance: return M.enduranceCost;
	default: return -1;                                  // no blessing chosen: refused
	}
}
double trialCost(const World& w, TrialKind k) {
	const auto& a = w.tune<CharactersTune>().trialCost;
	return size_t(k) < a.size() ? a[size_t(k)] : -1;
}
double revealCost(const World& w, RevelationKind k) {
	const auto& a = w.tune<DoctrineTune>().revealCost;
	return size_t(k) < a.size() ? a[size_t(k)] : -1;
}
template <class A> double cheapestOf(const A& a) {
	double best = std::numeric_limits<double>::infinity();
	for (double x : a) if (std::isfinite(x) && x >= 0 && x < best) best = x;
	return std::isfinite(best) ? best : -1;
}

// (3.18) a figure the god may test at this town: a living ruler, messenger, healer or hero whose home it is
bool testable(const Figure& f, int sid) {
	if (f.home != sid || f.died >= 0 || f.fate != uint8_t(FigureFate::Living)) return false;
	const FigureKind k = FigureKind(f.kind);
	return k == FigureKind::Ruler || k == FigureKind::Messenger || k == FigureKind::Healer || k == FigureKind::Hero;
}
// the figure a test at sid is for: `wanted` when it is testable there, else (wanted -1) the lowest testable id
int testFigure(const World& w, int sid, int wanted) {
	int first = -1;
	for (const Figure& f : w.mythos.figures) {
		if (!testable(f, sid)) continue;
		if (f.id == wanted) return f.id;
		if (first < 0 || f.id < first) first = f.id;
	}
	return wanted >= 0 ? -1 : first;                     // a named figure that is not here: refused
}

// a renewal was begun in this world (once per world, 3.30)
bool renewedOnce(const World& w) { return w.mythos.renewal != 0 || w.mythos.renewedYear >= 0 || renewal::active(w); }

// The Word-specific part of can()'s Target check (after the table's own target rule). why None = "not now".
TargetCheck wordTarget(const World& w, const PowerDef& d, TargetCheck tc) {
	if (!tc.ok) return tc;
	if (is(d, "speak")) {
		const doctrine::SpeakCheck k = doctrine::canSpeak(w, cultureAt(w, tc.sid), probeWord());
		// Awe is Powers' own check (the cheapest word); Content / NoMessenger cannot happen for the probe
		if (k.ok || k.why == doctrine::WordWhy::Awe) return tc;
		tc.ok = false;
		tc.why = k.why == doctrine::WordWhy::Target ? Why::Settlement : Why::None;   // Gap: one word per 10 years
		return tc;
	}
	if (is(d, "test")) {
		if (testFigure(w, tc.sid, -1) < 0) { tc.ok = false; tc.why = Why::None; }
		return tc;
	}
	if (is(d, "renew")) {
		if (renewedOnce(w)) { tc.ok = false; tc.why = Why::None; }
		return tc;
	}
	return tc;
}

// The price a Word power shows without a choice (cost()): its cheapest choice.
double wordShownCost(const World& w, const PowerDef& d, int cell) {
	if (mythPhase(w) < d.unlock.mythos) return d.cost;
	if (is(d, "speak")) {
		const int cid = w.valid(cell) ? cultureAt(w, civ::at(w, cell)) : -1;
		const double c = cid >= 0 ? cheapestWord(w, cid) : 0.0;
		return c > 0 ? c : d.cost;
	}
	if (is(d, "bless")) {
		const MythosTune& M = w.tune<MythosTune>();
		const double c = cheapestOf(std::array<double, 3>{ M.prosperityCost, M.healerCost, M.enduranceCost });
		return c >= 0 ? c : d.cost;
	}
	if (is(d, "test")) { const double c = cheapestOf(w.tune<CharactersTune>().trialCost); return c >= 0 ? c : d.cost; }
	if (is(d, "reveal")) { const double c = cheapestOf(w.tune<DoctrineTune>().revealCost); return c >= 0 ? c : d.cost; }
	if (is(d, "renew")) return std::max(finiteOr(w.tune<RenewalTune>().minAwe, d.cost), payable(w));   // all Awe, at least minAwe
	return d.cost;
}

// use() of a Word power once the table's checks passed (phase, target). The act first, then the price, power:used,
// the deed and (bless only: the table's Radius) the witness. False with nothing spent when refused.
bool useWord(World& w, PowersScratch& S, const PowerDef& d, int idx, int cell, const TargetCheck& tc, const UseOpts& opts) {
	const int sid = tc.sid;
	// (3.9) a Dream is dreamed in one town: the town the god clicked, unless the chooser named another
	WordOpts o = opts.word;
	if (is(d, "speak") && o.clarity == Clarity::Dream && o.ref < 0) o.ref = sid;
	const int cid = cultureAt(w, sid);
	double price = -1;
	bool witness = false;
	society::WitnessOpts wo;
	wo.kind = d.id;
	wo.faith = d.faith;
	if (is(d, "speak")) {
		const doctrine::SpeakCheck k = doctrine::canSpeak(w, cid, o);
		if (!k.ok) {
			if (k.why == doctrine::WordWhy::Awe) aweShort(w, S, d, idx);
			return false;
		}
		price = std::max(0.0, finiteOr(k.cost, 0));
		if (payable(w) + 1e-9 < price) { aweShort(w, S, d, idx); return false; }
		if (doctrine::speak(w, cid, o) < 0) return false;
	} else if (is(d, "bless")) {
		price = blessCost(w, o.blessing);
		if (!(price >= 0)) return false;
		if (payable(w) + 1e-9 < price) { aweShort(w, S, d, idx); return false; }
		if (!mythos::bless(w, sid, o.blessing)) return false;
		witness = d.wScope != WScope::None;
		wo.good = 0.8;
	} else if (is(d, "test")) {
		const int fig = testFigure(w, sid, o.figure);
		price = trialCost(w, o.trial);
		if (fig < 0 || !(price >= 0)) return false;
		if (payable(w) + 1e-9 < price) { aweShort(w, S, d, idx); return false; }
		if (!characters::test(w, fig, o.trial)) return false;
	} else if (is(d, "reveal")) {
		price = revealCost(w, o.revelation);
		if (cid < 0 || !(price >= 0) || uint8_t(o.clarity) >= uint8_t(enumCount<Clarity>())) return false;
		if (payable(w) + 1e-9 < price) { aweShort(w, S, d, idx); return false; }
		if (!doctrine::reveal(w, cid, o.revelation, o.clarity)) return false;
	} else if (is(d, "renew")) {
		if (o.renewal == RenewalKind::None || uint8_t(o.renewal) >= uint8_t(enumCount<RenewalKind>())) return false;
		const double need = finiteOr(w.tune<RenewalTune>().minAwe, d.cost);
		if (payable(w) + 1e-9 < need) { aweShort(w, S, d, idx); return false; }
		std::vector<int> remnant;
		for (int r : o.remnant) if (r >= 0 && std::find(remnant.begin(), remnant.end(), r) == remnant.end()) remnant.push_back(r);
		if (!renewal::begin(w, o.renewal, remnant)) return false;
		price = payable(w);                                // it costs all the Awe the god has
	} else {
		return false;
	}
	spend(w, price);
	w.bus.emit(EvPowerUsed{ std::string(d.id), cell, 0.0, false });
	countDeed(w, d);
	if (witness) society::witness(w, cell, d.wAng, wo);
	return true;
}

// (3.8) prophet at MythosTune::phase >= 1 with UseOpts::choose: the MessengerChoice card on the town (three
// candidates, characters::candidates). Not budgeted (cards::open); re-arming the same town shows the same card.
// Nothing is spent, nothing is witnessed: the card's answer pays the power's cost (Characters' choiceCost falls back to
// the prophet row) and "none" is the refund. False when the people already have a voice or no candidate exists.
bool openMessengerChoice(World& w, int sid) {
	const Settlement* s = state::settlement(w, sid);
	if (!s || s->dead) return false;
	const int cid = s->culture;
	const Culture* cu = state::culture(w, cid);
	if (!cu || cu->dead || cu->prophet || characters::messenger(w, cid) >= 0) return false;
	if (characters::candidates(w, sid).empty()) return false;
	double world = 0;
	for (const Settlement& t : w.settlements) if (!t.dead && std::isfinite(t.pop) && t.pop > 0) world += t.pop;
	cards::Proposal p;
	p.kind = CardKind::MessengerChoice;
	p.culture = cid;
	p.settlement = sid;
	p.cell = s->cell;
	p.severity = world > 0 ? clamp(finiteOr(s->pop, 0) / world * (s->capital ? 1.5 : 1.0), 0.0, 1.0) : 0.0;
	p.ref = { sid, -1, -1, -1 };
	p.data["decade"] = double(int64_t(std::floor(std::isfinite(w.year) ? w.year : 0)) / 10);
	p.data["armed"] = 1;                                 // the god armed it (not "they seek a voice")
	return cards::open(w, p) >= 0;
}

// pay()'s clock: like use(), elapsed ticks count at the fastest game speed (pay has no real dt)
void advanceClock(const World& w, PowersScratch& S) {
	const PowersTune& T = w.tune<PowersTune>();
	if (S.haveTick && w.tick > S.lastTick) {
		const double speed = T.maxGameSpeed > 1 ? T.maxGameSpeed : 1;
		S.clock += double(w.tick - S.lastTick) * sim::kRealPerTick / speed;
	}
	S.haveTick = true;
	S.lastTick = w.tick;
}

} // namespace

// ================================================================== public API

bool unlocked(const World& w, std::string_view id) {
	const PowerDef* d = byId(id);
	return d && phaseLock(w, *d) == Reason::Ok;
}

const Upgrade* upgrade(const World& w, std::string_view id) { return upgradeOf(w, id); }

int drownRisk(const World& w, int cell, double ang) {
	if (!w.valid(cell) || !w.geo || w.phase < Phase::History) return -1;
	const PowerDef* d = byId("lower");
	if (!d) return -1;
	const double a = effAng(w, *d, ang);
	const double floorH = w.seaLevel + w.tune<PowersTune>().drownFloor;
	for (int sid : sortedNear(w, cell, a)) {
		const Settlement* s = state::settlement(w, sid);
		if (s && !s->dead && w.valid(s->cell) && double(w.elev[size_t(s->cell)]) <= floorH + 1e-3 &&
			w.vars.num.count("powers_drownWarned_" + std::to_string(sid)))
			return sid;
	}
	return -1;
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
		if (is(*d, "rain") && upgradeOf(w, "rain")) c *= T.monsoonCostMul;
		// (v3, design review finding 6) painting the empty world is not free
		if ((is(*d, "rain") || is(*d, "bloom")) && w.phase >= Phase::History && w.valid(cell) && w.geo && !coversTerritory(w, cell, a))
			c *= T.offTerritoryMul;
		return finiteOr(c, 0);
	}
	if (is(*d, "consecrate") && w.valid(cell) && consecrateRite(w, civ::at(w, cell)) == 2) return T.wonderCost;
	if (is(*d, "awaken")) {
		if (w.phase < Phase::History) return 0;
		if (nobodyAlive(w)) return 0;
		return std::max(0.0, std::min(T.awakenMaxCost, finiteOr(w.god.aweMax, 0)));
	}
	// (v4 mythos, M7) a Word power shows its cheapest choice (the chooser asks the owning module for the rest)
	if (d->group == Group::Word) return std::max(0.0, finiteOr(wordShownCost(w, *d, cell), d->cost));
	return d->cost;
}

CanResult can(const World& w, std::string_view id, int cell) {
	CanResult r;
	const PowerDef* d = byId(id);
	if (!d) return r;                                   // { false, Locked }
	const Reason pl = phaseLock(w, *d);
	if (pl != Reason::Ok) { r.reason = pl; return r; }
	if (withdrawnNow(w)) { r.reason = Reason::Withdrawn; return r; }   // (v4 mythos, 3.29)
	TargetCheck tc = checkTarget(w, *d, cell);
	if (d->group == Group::Word) tc = wordTarget(w, *d, tc);           // (v4 mythos, M7)
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
		const bool explicitPress = S.lastPower != idx || !(rawDt > 0);
		const bool newPress = explicitPress || rawDt > T.strokeGap;
		if (newPress) {
			// faith of the last (< 1 / brushRate s) of the previous press is not carried to a new place
			B.pending = 0;
			B.harmed.clear();
			// (v3, verifier fix) a long stalled frame (dt > strokeGap) while the button is held is still
			// the same press for Lower Land's drown guard: only a real new press (dt <= 0, or another
			// power in between) may drown a town the last press stopped at the floor
			if (explicitPress) {
				B.reached.clear();
				B.warned.clear();
			}
			B.shortInStroke = false;
			if (S.clock - B.lastEmit >= 1 / T.brushRate - 1e-9) B.pace = 1 / T.brushRate;   // first frame shows at once
		}
	}
	S.lastPower = idx;

	// ---- may it be used here?
	if (phaseLock(w, d) != Reason::Ok) return false;
	if (withdrawnNow(w)) return false;                                 // (v4 mythos, 3.29)
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
	// (v4 mythos, M7) the Word powers: the owning module acts, then Powers pays (nothing is spent on a refused word)
	if (d.group == Group::Word) return useWord(w, S, d, idx, cell, tc, opts);
	// (v4 mythos, M7, 3.8) the Messenger choice, when the caller asks for it: a card, nothing spent yet
	if (is(d, "prophet") && opts.choose && mythPhase(w) >= 1) return openMessengerChoice(w, tc.sid);

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

// (v4 mythos, M7) the god pays for a mythos act (a card answer, Look into the heart, a presence change, a Keeper's
// upkeep ...). Only mythos code calls it (never at MythosTune::phase 0). Never a deed, never witnessed, no power:used.
// A non-finite amount is refused (false, no event); amount <= 0 is free. Short of Awe: awe:short { what }, at most
// once per PowersTune::aweShortEvery seconds of the Powers clock per `what` (ticks count at the fastest speed).
bool pay(World& w, std::string_view what, double amount) {
	if (!std::isfinite(amount)) return false;
	if (amount <= 0) return true;
	PowersScratch& S = scr(w);
	advanceClock(w, S);
	if (payable(w) + 1e-9 < amount) {
		const PowersTune& T = w.tune<PowersTune>();
		auto it = S.payShort.find(what);
		if (it == S.payShort.end() || S.clock - it->second >= T.aweShortEvery - 1e-9) {
			S.payShort[std::string(what)] = S.clock;
			w.bus.emit(EvAweShort{ std::string(what) });
		}
		return false;
	}
	spend(w, amount);
	return true;
}

} // namespace dm::powers
