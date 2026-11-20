// DEMIURGE — SimTests/test_powers.cpp : Sim/Powers.cpp, the powers half (SPEC-SIM 3.10, BUILD.md A11).
// Owner: A11 powers.
//
// Runs with the real Powers and every other module stubbed (the disasters half too). Where a
// collaborator must act (civ::damage, civ::infect, society::witness, powers::start, ...) a test hook
// stands in for it and records what Powers asked for; those tests return early when the module is
// real (`run.ps1 all`). Tests that only read Powers' own writes (wet, fert, blessed, inspire, awe,
// deeds, events) run in both builds.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "Civ.h"
#include "Life.h"
#include "MapImg.h"
#include "Powers.h"
#include "Sim.h"
#include "Society.h"
#include "Terrain.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "Units.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;
namespace pw = dm::powers;
namespace mi = dm::mapimg;

namespace {

// ------------------------------------------------------------------ helpers

int town(World& w, int cell, int culture, double pop = 200, int hops = 2) {
	Settlement& s0 = state::newSettlement(w, cell, culture);
	s0.pop = pop;
	const int sid = s0.id;
	std::vector<int32_t> r;
	w.geo->ring(cell, hops, r);
	std::vector<int32_t> cells;
	for (int c : r)
		if (!state::isSea(w, c) && w.owner[size_t(c)] < 0) { w.owner[size_t(c)] = int16_t(sid); cells.push_back(c); }
	w.settlements[size_t(sid)].cells = cells;
	return sid;
}
int culture(World& w) { return state::newCulture(w).id; }

// a land cell at least `apart` rad from every given cell (and from the poles' wrap trouble)
int landAway(const World& w, const std::vector<int>& from, double apart, int coastMin = 3) {
	return findLand(w, [&](int c) {
		if (w.coast[size_t(c)] < coastMin) return false;
		for (int f : from) if (w.geo->angle(c, f) < apart) return false;
		return true;
	});
}
int seaCell(const World& w, double deeper = -1.0) {
	for (int c = 0; c < w.n; c++) if (w.elev[size_t(c)] < w.seaLevel + deeper) return c;
	return -1;
}

std::unique_ptr<World> histWorld(int L = 5, uint32_t seed = 3) {
	auto w = makeWorld({ .L = L, .seed = seed });
	w->god.awe = 1000;
	w->god.aweMax = 1200;
	// (v3, design review) every power is unlocked from Industry; the Atomic / Space upgrades and the
	// off-territory price of Rain and Bloom are tested on their own (end of file, test_integration_review)
	w->age = Age::INDUSTRY;
	w->tune<PowersTune>().offTerritoryMul = 1;
	return w;
}

int countEv(const std::vector<Event>& ev, Ev t) {
	return int(std::count_if(ev.begin(), ev.end(), [&](const Event& e) { return e.type == t; }));
}

struct WCall { int cell; double ang; society::WitnessOpts o; };
std::vector<WCall>& wcalls() { static std::vector<WCall> v; return v; }
void hookWitness() {
	wcalls().clear();
	stub::hooks().society_witness = [](World&, int cell, double ang, const society::WitnessOpts& o) {
		wcalls().push_back({ cell, ang, o });
		return 1;
	};
}

struct DCall { int sid; double frac; Cause cause; };
std::vector<DCall>& dcalls() { static std::vector<DCall> v; return v; }
// civ::damage that really kills (frac of the people), as Civ will
void hookDamage() {
	dcalls().clear();
	stub::hooks().civ_damage = [](World& w, int sid, double frac, Cause cause) {
		dcalls().push_back({ sid, frac, cause });
		Settlement* s = state::settlement(w, sid);
		if (!s || s->dead) return 0.0;
		const double k = s->pop * frac;
		s->pop -= k;
		return k;
	};
}

struct SCall { DisasterType type; int cell; pw::StartOpts o; };
std::vector<SCall>& scalls() { static std::vector<SCall> v; return v; }
// powers::start that creates the disaster (as Disasters will)
void hookStart() {
	scalls().clear();
	stub::hooks().powers_start = [](World& w, DisasterType type, int cell, const pw::StartOpts& o) -> Disaster* {
		scalls().push_back({ type, cell, o });
		Disaster& d = state::newDisaster(w, type, cell);
		d.ang = o.ang.value_or(0.05);
		d.natural = o.natural;
		return &d;
	};
}

double sumField(const std::vector<float>& v) {
	double s = 0;
	for (float x : v) s += x;
	return s;
}

// ------------------------------------------------------------------ the spec table, written out

struct Row {
	const char* id; const char* name; char key; pw::Group group; bool brush; double cost, ang; pw::Target target;
	Phase phase; int age; double awe; bool phaseOnly; pw::WScope scope; double wAng, faith;
};
using G = pw::Group;
using Tg = pw::Target;
using WS = pw::WScope;
const Row kSpec[] = {
	{ "comet", "Comet", 'C', G::Genesis, false, 0, 0.03, Tg::Any, Phase::Water, 0, 0, true, WS::None, 0, 0 },
	{ "seed", "Seed Life", 'G', G::Genesis, false, 0, 0.12, Tg::Land, Phase::Life, 0, 0, true, WS::None, 0, 0 },
	{ "awaken", "Awaken", 'K', G::Genesis, false, 0, 0, Tg::Land, Phase::People, Age::SAIL, 450, false, WS::None, 0, 0 },
	{ "raise", "Raise Land", 'U', G::Shape, true, 6, 0.06, Tg::Any, Phase::Water, 0, 0, false, WS::BrushRel, 2, 0.02 },
	{ "lower", "Lower Land", 'J', G::Shape, true, 6, 0.06, Tg::Any, Phase::Water, 0, 0, false, WS::BrushRel, 2, 0.02 },
	{ "rain", "Rain", 'N', G::Sky, true, 5, 0.10, Tg::Any, Phase::History, 0, 0, false, WS::BrushRel, 2, 0.02 },
	{ "bloom", "Bloom", 'B', G::Sky, true, 6, 0.08, Tg::Land, Phase::History, Age::BRONZE, 150, false, WS::BrushRel, 2, 0.02 },
	{ "lightning", "Lightning", 'L', G::Wrath, false, 15, 0, Tg::Any, Phase::History, 0, 0, false, WS::Radius, 0.10, 0.05 },
	{ "quake", "Earthquake", 'Z', G::Wrath, false, 60, 0.08, Tg::Any, Phase::History, Age::BRONZE, 200, false, WS::Radius, 0.20, 0.10 },
	// (v3, design review finding 5) every power past Bronze unlocks by age only (awe 0)
	{ "plague", "Plague", 'X', G::Wrath, false, 80, 0, Tg::Settlement, Phase::History, Age::IRON, 0, false, WS::Radius, 0.12, 0.05 },
	{ "meteor", "Meteor", 'M', G::Wrath, false, 150, 0.05, Tg::Any, Phase::History, Age::SAIL, 0, false, WS::Radius, 0.45, 0.25 },
	{ "volcano", "Volcano", 'V', G::Wrath, false, 220, 0.04, Tg::Land, Phase::History, Age::INDUSTRY, 0, false, WS::Radius, 0.40, 0.25 },
	{ "heal", "Heal", 'T', G::Grace, false, 40, 0, Tg::Settlement, Phase::History, 0, 0, false, WS::Radius, 0.10, 0.12 },
	{ "inspire", "Inspire", 'I', G::Grace, false, 60, 0, Tg::Settlement, Phase::History, Age::BRONZE, 200, false, WS::Radius, 0.10, 0.08 },
	{ "prophet", "Prophet", 'P', G::Grace, false, 120, 0, Tg::Settlement, Phase::History, Age::IRON, 0, false, WS::Radius, 0.15, 0.15 },
	{ "peace", "Omen of Peace", 'O', G::Grace, false, 100, 0, Tg::Settlement, Phase::History, Age::IRON, 0, false, WS::Cultures, 0, 0.10 },
	// (v3, design review finding 6) a durable thing to buy
	{ "consecrate", "Consecrate", 'Y', G::Grace, false, 150, 0, Tg::Settlement, Phase::History, Age::BRONZE, 0, false, WS::Radius, 0.10, 0.10 },
};
const Phase kPhases[] = { Phase::Molten, Phase::Water, Phase::Life, Phase::People, Phase::History, Phase::Ended };

// SPEC-SIM 3.10 "Unlocked", written out independently of Powers.cpp
bool specUnlocked(const World& w, const Row& r, bool nobody) {
	if (r.phaseOnly) return w.phase == r.phase;
	if (int(w.phase) < int(r.phase)) return false;
	if (std::string(r.id) == "awaken") {
		if (w.phase == Phase::People) return true;
		if (nobody) return true;
	}
	return w.age >= r.age || (r.awe > 0 && w.god.aweMax >= r.awe);
}

} // namespace

// ================================================================== the table

DM_TEST("powers: LIST equals the spec table field for field") {
	if (!eq(pw::kPowerCount, int(std::size(kSpec)), "17 powers (v3: + consecrate)")) return;
	for (int i = 0; i < pw::kPowerCount; i++) {
		const pw::PowerDef& d = pw::kPowers[i];
		const Row& r = kSpec[i];
		const std::string tag = std::string("row ") + std::to_string(i) + " " + r.id;
		eq(std::string(d.id), std::string(r.id), tag + " id (spec order)");
		eq(std::string(d.name), std::string(r.name), tag + " name");
		eq(int(d.key), int(r.key), tag + " key");
		eq(int(d.group), int(r.group), tag + " group");
		eq(d.brush, r.brush, tag + " kind");
		eq(d.cost, r.cost, tag + " cost");
		eq(d.ang, r.ang, tag + " ang");
		eq(int(d.target), int(r.target), tag + " target");
		eq(int(d.unlock.phase), int(r.phase), tag + " unlock phase");
		eq(d.unlock.age, r.age, tag + " unlock age");
		eq(d.unlock.awe, r.awe, tag + " unlock awe");
		eq(d.unlock.phaseOnly, r.phaseOnly, tag + " phase only");
		eq(int(d.wScope), int(r.scope), tag + " witness scope");
		eq(d.wAng, r.wAng, tag + " wAng");
		eq(d.faith, r.faith, tag + " faith");
		ok(pw::byId(r.id) == &d, tag + " byId");
		ok(d.desc && *d.desc, tag + " desc");
	}
	ok(pw::byId("nuke") == nullptr && pw::byId("") == nullptr && pw::byId("Rain") == nullptr, "byId unknown -> nullptr");
	// hotkeys are unique
	std::string keys;
	for (const auto& d : pw::kPowers) keys += d.key;
	std::string sorted = keys;
	std::sort(sorted.begin(), sorted.end());
	ok(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end(), "hotkeys unique: " + keys);
}

// ================================================================== unlocked

DM_TEST("powers: unlocked by phase / age / aweMax; genesis only in their phase (every power x phase x age x aweMax)") {
	auto w = makeWorld({ .L = 4, .seed = 2 });
	const int cu = culture(*w);
	int checked = 0;
	const double awes[] = { 100, 149, 150, 200, 300, 450, 600, 1200 };
	for (Phase ph : kPhases)
		for (int age = 0; age < Age::COUNT; age++)
			for (double am : awes)
				for (int people = 0; people < 2; people++) {
					w->settlements.clear();
					std::fill(w->owner.begin(), w->owner.end(), int16_t(-1));
					if (people) town(*w, findLand(*w), cu, 50);
					w->phase = ph;
					w->age = age;
					w->god.aweMax = am;
					for (const Row& r : kSpec) {
						const bool want = specUnlocked(*w, r, people == 0);
						if (!eq(pw::unlocked(*w, r.id), want, std::string(r.id) + " phase " + toStr(ph) + " age " + std::to_string(age) + " aweMax " + std::to_string(am) + " people " + std::to_string(people))) return;
						checked++;
					}
				}
	ok(!pw::unlocked(*w, "nope") && !pw::unlocked(*w, ""), "unknown ids are never unlocked");
	std::printf("    unlocked: %d (power, phase, age, aweMax, people) combinations match the spec\n", checked);
	// spot checks in words
	w->settlements.clear();
	w->phase = Phase::Water; w->age = 0; w->god.aweMax = 100;
	ok(pw::unlocked(*w, "comet") && pw::unlocked(*w, "raise") && pw::unlocked(*w, "lower"), "water: comet, raise, lower");
	ok(!pw::unlocked(*w, "seed") && !pw::unlocked(*w, "awaken") && !pw::unlocked(*w, "rain"), "water: no seed / awaken / rain");
	w->phase = Phase::Life;
	ok(pw::unlocked(*w, "seed") && !pw::unlocked(*w, "comet"), "life: seed, comet gone");
	w->phase = Phase::People;
	ok(pw::unlocked(*w, "awaken") && !pw::unlocked(*w, "seed") && !pw::unlocked(*w, "lightning"), "people: awaken only (+ sculpt)");
	w->phase = Phase::History;
	ok(pw::unlocked(*w, "awaken"), "history: awaken free when nobody is alive");
	ok(pw::unlocked(*w, "lightning") && pw::unlocked(*w, "heal") && pw::unlocked(*w, "rain"), "history age 0: lightning, heal, rain");
	ok(!pw::unlocked(*w, "bloom") && !pw::unlocked(*w, "meteor"), "history age 0 aweMax 100: no bloom, no meteor");
	w->god.aweMax = 150;
	ok(pw::unlocked(*w, "bloom") && !pw::unlocked(*w, "quake"), "aweMax 150 unlocks bloom (not quake)");
	w->god.aweMax = 100; w->age = Age::BRONZE;
	ok(pw::unlocked(*w, "bloom") && pw::unlocked(*w, "quake") && pw::unlocked(*w, "inspire") && !pw::unlocked(*w, "plague"), "Bronze: bloom, quake, inspire");
}

DM_TEST("powers: awaken free when nobody is alive; a settler on the move counts as alive") {
	auto w = histWorld(4, 2);
	w->god.aweMax = 100;
	w->age = 0;
	const int cu = culture(*w);
	ok(pw::unlocked(*w, "awaken"), "empty history: unlocked");
	eq(pw::cost(*w, "awaken"), 0.0, "empty history: free");
	Unit& u = state::newUnit(*w, UnitType::Settler);
	u.culture = cu;
	ok(!pw::unlocked(*w, "awaken"), "a settler is alive: locked (age 0, aweMax 100)");
	w->units.clear();
	Unit& tr = state::newUnit(*w, UnitType::Trader);
	tr.culture = cu;
	ok(pw::unlocked(*w, "awaken"), "a trader is not a settler: still free");
	w->units.clear();
	const int sid = town(*w, findLand(*w), cu, 40);
	ok(!pw::unlocked(*w, "awaken"), "a settlement lives: locked");
	w->god.aweMax = 450;
	ok(pw::unlocked(*w, "awaken"), "aweMax 450 unlocks");
	eq(pw::cost(*w, "awaken"), 250.0, "cost min(250, aweMax) = 250");
	w->god.aweMax = 200; w->age = Age::SAIL;
	ok(pw::unlocked(*w, "awaken"), "Sail unlocks");
	eq(pw::cost(*w, "awaken"), 200.0, "cost min(250, 200) = 200");
	w->settlements[size_t(sid)].dead = true;
	eq(pw::cost(*w, "awaken"), 0.0, "everyone dead: free again");
	w->phase = Phase::People;
	eq(pw::cost(*w, "awaken"), 0.0, "people phase: free");
}

// ================================================================== cost

DM_TEST("powers: cost scales with brush area, x4 for sculpting on owned land in history, free in genesis") {
	auto w = histWorld(5);
	const int cu = culture(*w);
	const int home = findLand(*w, [&](int c) { return w->coast[size_t(c)] >= 3; });
	town(*w, home, cu, 300);
	const int wild = landAway(*w, { home }, 0.5);
	ok(home >= 0 && wild >= 0, "cells");
	near(pw::cost(*w, "rain"), 5, 1e-12, "rain at def ang");
	near(pw::cost(*w, "rain", 0.05), 5 * 0.25, 1e-12, "rain half radius = quarter cost");
	near(pw::cost(*w, "rain", 0.15), 5 * 2.25, 1e-12, "rain 0.15");
	near(pw::cost(*w, "rain", 0.5), 5 * 2.25, 1e-12, "ang clamped to 0.15");
	near(pw::cost(*w, "rain", 0.001), 5 * 0.09, 1e-12, "ang clamped to 0.03");
	near(pw::cost(*w, "bloom", 0.16), 6 * (0.15 / 0.08) * (0.15 / 0.08), 1e-12, "bloom: clamp 0.15 -> (0.15/0.08)^2");
	near(pw::cost(*w, "bloom", 0.04), 6 * 0.25, 1e-12, "bloom 0.04");
	near(pw::cost(*w, "raise", -1, wild), 6, 1e-12, "raise on wild land");
	near(pw::cost(*w, "raise", -1, home), 24, 1e-12, "raise on owned land x4");
	near(pw::cost(*w, "lower", 0.12, home), 6 * 4 * 4, 1e-12, "lower 0.12 on owned land: area x4, owned x4");
	near(pw::cost(*w, "lower", 0.12, -1), 24, 1e-12, "no cell: not owned");
	near(pw::cost(*w, "raise", std::nan(""), wild), 6, 1e-12, "NaN ang = def ang");
	for (Phase ph : { Phase::Water, Phase::Life, Phase::People }) {
		w->phase = ph;
		eq(pw::cost(*w, "raise", 0.15, home), 0.0, std::string("sculpting free in ") + toStr(ph));
		eq(pw::cost(*w, "lower", 0.15, home), 0.0, std::string("sculpting free in ") + toStr(ph));
	}
	w->phase = Phase::History;
	for (const Row& r : kSpec) if (!r.brush && std::string(r.id) != "awaken") eq(pw::cost(*w, r.id), r.cost, std::string(r.id) + " click cost");
	eq(pw::cost(*w, "lightning", 0.3), 15.0, "click cost ignores ang");
	eq(pw::cost(*w, "nope"), 0.0, "unknown id costs 0");
}

// ================================================================== can

DM_TEST("powers: can reasons and why for every target type (incl. small)") {
	stub::Guard guard(true);
	auto w = histWorld(5);
	const int cu = culture(*w);
	const int land = findLand(*w, [&](int c) { return w->coast[size_t(c)] >= 3; });
	const int sid = town(*w, land, cu, 300);
	const int edge = w->settlements[size_t(sid)].cells.back();   // owned, not the centre
	const int wild = landAway(*w, { land }, 0.5);
	const int ocean = seaCell(*w);
	ok(land >= 0 && wild >= 0 && ocean >= 0 && edge != land, "cells");

	auto chk = [&](const char* id, int cell, bool okv, pw::Reason r, pw::Why y, const std::string& tag) {
		const pw::CanResult c = pw::can(*w, id, cell);
		const bool good = c.ok == okv && c.reason == r && c.why == y;
		ok(good, tag + ": " + id + " got {" + (c.ok ? "true" : "false") + ", " + pw::toStr(c.reason) + ", " + pw::toStr(c.why) + "}");
	};
	using R = pw::Reason;
	using Y = pw::Why;
	// bad cells for every power
	for (const Row& r : kSpec) {
		for (int bad : { -1, w->n, 1 << 30 }) {
			const pw::CanResult c = pw::can(*w, r.id, bad);
			if (pw::unlocked(*w, r.id)) ok(!c.ok && c.reason == R::Target && c.why == Y::None, std::string(r.id) + " bad cell");
			else ok(!c.ok, std::string(r.id) + " bad cell (locked)");
		}
	}
	chk("nope", land, false, R::Locked, Y::None, "unknown id");
	// any
	chk("lightning", ocean, true, R::Ok, Y::None, "any on sea");
	chk("lightning", wild, true, R::Ok, Y::None, "any on land");
	chk("meteor", ocean, true, R::Ok, Y::None, "meteor any");
	// land
	chk("volcano", ocean, false, R::Target, Y::Sea, "land target on sea");
	chk("bloom", ocean, false, R::Target, Y::Sea, "land target on sea");
	chk("volcano", wild, true, R::Ok, Y::None, "land target on land");
	// settlement
	chk("heal", wild, false, R::Target, Y::Settlement, "settlement target on empty land");
	chk("heal", ocean, false, R::Target, Y::Settlement, "settlement target on sea");
	chk("heal", land, true, R::Ok, Y::None, "settlement centre");
	chk("plague", edge, true, R::Ok, Y::None, "settlement territory");
	// peace needs a war
	chk("peace", land, false, R::Target, Y::None, "peace without a war");
	const int cu2 = culture(*w);
	society::rel(*w, cu, cu2)->war = true;
	chk("peace", land, true, R::Ok, Y::None, "peace at war");
	// awaken: civ::canSettle decides (hooked), then the landmass size
	w->settlements[size_t(sid)].dead = true;   // nobody alive: free
	if (stub::stubbed("civ")) {
		civ::SettleReason why = civ::SettleReason::Ok;
		stub::hooks().civ_canSettle = [&](const World&, int) { return civ::SettleCheck{ why == civ::SettleReason::Ok, why }; };
		why = civ::SettleReason::Sea;     chk("awaken", wild, false, R::Target, Y::Sea, "awaken canSettle sea");
		why = civ::SettleReason::Barren;  chk("awaken", wild, false, R::Target, Y::Barren, "awaken canSettle barren");
		why = civ::SettleReason::Crowded; chk("awaken", wild, false, R::Target, Y::Crowded, "awaken canSettle crowded");
		why = civ::SettleReason::Ok;
		const int lm = w->cache.landId[size_t(wild)];
		const int keep = w->cache.landHab[size_t(lm)];
		w->cache.landHab[size_t(lm)] = 600;  chk("awaken", wild, true, R::Ok, Y::None, "awaken 600 habitable cells");
		w->cache.landHab[size_t(lm)] = 599;  chk("awaken", wild, true, R::Ok, Y::Small, "awaken 599 habitable cells: small");
		w->cache.landHab[size_t(lm)] = keep;
		std::printf("    awaken: landmass %d holds %d habitable cells (L 5)\n", lm, keep);
		auto ids = w->cache.landId;
		w->cache.landId.clear();
		chk("awaken", wild, true, R::Ok, Y::None, "awaken without landmass caches: ok, no why");
		w->cache.landId = ids;
		stub::hooks().civ_canSettle = nullptr;
	}
	w->settlements[size_t(sid)].dead = false;
	// phase and locked
	w->phase = Phase::People;
	chk("lightning", wild, false, R::Phase, Y::None, "history power in people");
	chk("comet", wild, false, R::Phase, Y::None, "comet outside water");
	w->phase = Phase::Molten;
	chk("raise", wild, false, R::Phase, Y::None, "raise in molten");
	w->phase = Phase::History;
	w->age = 0;
	w->god.aweMax = 100;
	chk("meteor", wild, false, R::Locked, Y::None, "meteor locked at age 0 / aweMax 100");
	chk("volcano", ocean, false, R::Locked, Y::None, "locked before target");
	w->phase = Phase::Water;
	chk("rain", ocean, false, R::Phase, Y::None, "phase before locked");
	w->phase = Phase::History;
	// awe
	w->god.awe = 14.9;
	chk("lightning", wild, false, R::Awe, Y::None, "lightning with 14.9 Awe");
	w->god.awe = 15;
	chk("lightning", wild, true, R::Ok, Y::None, "lightning with 15 Awe");
	w->god.awe = 1.2;
	chk("rain", wild, false, R::Awe, Y::None, "brush needs a quarter second (1.25)");
	w->god.awe = 1.3;
	chk("rain", wild, true, R::Ok, Y::None, "brush with 1.3");
	w->god.awe = 0;
	chk("heal", wild, false, R::Target, Y::Settlement, "target before awe");
	w->god.awe = std::nan("");
	chk("lightning", wild, false, R::Awe, Y::None, "NaN awe cannot pay");
	w->god.awe = 1000;
}

// ================================================================== use: Awe, events, deeds

DM_TEST("powers: use spends Awe once per success; power:used, deed and witness in that order") {
	stub::Guard guard(true);
	auto w = histWorld(5);
	const int cu = culture(*w);
	const int land = findLand(*w, [&](int c) { return w->coast[size_t(c)] >= 3; });
	const int sid = town(*w, land, cu, 300);
	if (stub::stubbed("society")) hookWitness();
	std::vector<std::string> order;
	w->bus.onAll([&](const Event& e) { order.push_back(e.name()); });
	w->god.awe = 100;
	const bool r1 = pw::use(*w, "inspire", land);
	ok(r1, "inspire used");
	near(w->god.awe, 40, 1e-9, "60 Awe spent");
	eq(w->settlements[size_t(sid)].inspire, 40.0, "inspire = 40");
	eq(w->god.deeds["inspire"], 1, "one deed");
	eq(w->stats.miracles, 1, "stats.miracles");
	if (stub::stubbed("society")) ok(!order.empty() && order.back() == "power:used", "power:used emitted last (witness stub emits nothing)");
	else ok(order.size() >= 2 && order[order.size() - 2] == "power:used" && order.back() == "miracle:witnessed", "power:used, then miracle:witnessed (real Society)");
	if (stub::stubbed("society")) {
		if (eq(int(wcalls().size()), 1, "one witness call")) {
			const WCall& c = wcalls()[0];
			eq(c.cell, land, "witness at the cell");
			near(c.ang, 0.10, 1e-12, "wAng 0.10");
			eq(c.o.kind, std::string("inspire"), "kind");
			near(c.o.faith, 0.08, 1e-12, "faith 0.08");
			near(c.o.good, 0.6, 1e-12, "good +0.6");
		}
		eq(stub::calls().back(), std::string("society_witness"), "witness after the deed");
	}
	// cannot pay: false, nothing spent, no power:used, awe:short
	w->god.awe = 59;
	auto ev = capture(*w, [&] { ok(!pw::use(*w, "inspire", land), "59 Awe: refused"); });
	near(w->god.awe, 59, 1e-12, "nothing spent");
	eq(countEv(ev, Ev::PowerUsed), 0, "no power:used");
	eq(countEv(ev, Ev::AweShort), 1, "awe:short");
	if (const EvAweShort* a = ev.empty() ? nullptr : ev.back().as<EvAweShort>()) eq(a->id, std::string("inspire"), "awe:short id");
	eq(w->god.deeds["inspire"], 1, "no deed");
	// refused target: false, no awe:short
	w->god.awe = 1000;
	ev = capture(*w, [&] { ok(!pw::use(*w, "inspire", seaCell(*w)), "sea: refused"); });
	eq(int(ev.size()), 0, "no events for a refused target");
	near(w->god.awe, 1000, 1e-12, "nothing spent");
	noNaN(*w, "after use");
}

DM_TEST("powers: brush pacing: <= 4 power:used per second, deeds and witness with accumulated faith; 60 fps == 144 fps +- 5 %") {
	stub::Guard guard(true);
	struct Tot { int used = 0, deeds = 0, witness = 0; double faith = 0, spent = 0, wet = 0; double maxRate = 0; };
	auto run = [&](const char* id, int fps, double secs) {
		Tot t;
		auto w = histWorld(5);
		const int cu = culture(*w);
		const int land = findLand(*w, [&](int c) { return w->coast[size_t(c)] >= 4; });
		town(*w, landAway(*w, { land }, 0.05, 1), cu, 300);
		if (stub::stubbed("society")) hookWitness();
		wcalls().clear();
		const double wet0 = sumField(w->wet);
		const double awe0 = w->god.awe;
		std::vector<double> times;
		double now = 0;
		const int frames = int(std::lround(fps * secs));
		auto ev = capture(*w, [&] {
			for (int f = 0; f < frames; f++) {
				const double dt = f == 0 ? 0.0 : 1.0 / fps;
				now += dt;
				const size_t before = w->god.deeds[id];
				pw::use(*w, id, land, { .ang = -1, .dt = dt });
				if (size_t(w->god.deeds[id]) > before) times.push_back(now);
			}
		});
		t.used = countEv(ev, Ev::PowerUsed);
		t.deeds = w->god.deeds[id];
		t.witness = int(wcalls().size());
		for (const WCall& c : wcalls()) t.faith += c.o.faith;
		t.spent = awe0 - w->god.awe;
		t.wet = sumField(w->wet) - wet0;
		// largest number of power:used inside any half-open 1 s window
		for (size_t i = 0; i < times.size(); i++) {
			int k = 0;
			for (size_t j = i; j < times.size() && times[j] < times[i] + 1 - 1e-9; j++) k++;
			t.maxRate = std::max(t.maxRate, double(k));
		}
		noNaN(*w, std::string(id) + " brush");
		return t;
	};
	for (const char* id : { "rain", "raise", "bloom" }) {
		const Tot a = run(id, 60, 3), b = run(id, 144, 3), c = run(id, 30, 3);
		std::printf("    %-5s 3 s: 60 fps used %d faith %.5f spent %.3f wet %.3f | 144 fps used %d faith %.5f spent %.3f wet %.3f | 30 fps used %d faith %.5f\n",
			id, a.used, a.faith, a.spent, a.wet, b.used, b.faith, b.spent, b.wet, c.used, c.faith);
		for (const Tot* t : { &a, &b, &c }) {
			ok(t->used >= 11 && t->used <= 13, std::string(id) + " about 4 power:used per second: " + std::to_string(t->used));
			ok(t->maxRate <= 4, std::string(id) + " at most 4 in any 1 s window: " + std::to_string(t->maxRate));
			eq(t->deeds, t->used, std::string(id) + " one deed per power:used");
			if (stub::stubbed("society")) eq(t->witness, t->used, std::string(id) + " one witness per power:used");
		}
		ok(std::abs(a.used - b.used) <= 1, std::string(id) + " same count of power:used");
		const double cost = pw::cost(*histWorld(4), id) * (3 - 1.0 / 60);
		near(a.spent, cost, cost * 0.01, std::string(id) + " Awe per second x seconds held (60 fps)");
		near(b.spent, a.spent, a.spent * 0.05, std::string(id) + " Awe 144 vs 60");
		if (stub::stubbed("society")) {
			near(b.faith, a.faith, a.faith * 0.05, std::string(id) + " faith 144 vs 60");
			// every fps: witnessed faith = 0.02/s x held time, less at most one unwitnessed pacing window
			for (const auto& [t, fps] : { std::pair{ &a, 60 }, std::pair{ &b, 144 }, std::pair{ &c, 30 } }) {
				const double held = 3 - 1.0 / fps;
				ok(t->faith <= 0.02 * held + 1e-9 && t->faith >= 0.02 * (held - 0.27), std::string(id) + " faith = 0.02/s x held at " + std::to_string(fps) + " fps: " + std::to_string(t->faith));
			}
		}
		if (std::string(id) == "rain") near(b.wet, a.wet, std::max(1e-6, a.wet * 0.05), "rain wet 144 vs 60");
	}
}

DM_TEST("powers: awe:short is rate-limited (once per press, once per 1.5 s per power)") {
	stub::Guard guard(true);
	auto w = histWorld(5);
	const int land = findLand(*w, [&](int c) { return w->coast[size_t(c)] >= 3; });
	w->god.awe = 0;
	// click spam with no time passing: once
	auto ev = capture(*w, [&] { for (int i = 0; i < 20; i++) pw::use(*w, "lightning", land); });
	eq(countEv(ev, Ev::AweShort), 1, "20 clicks at the same instant: one awe:short");
	// click dt feeds the clock: 0.5 s per click -> one every 3 clicks
	ev = capture(*w, [&] { for (int i = 0; i < 12; i++) pw::use(*w, "lightning", land, { .ang = -1, .dt = 0.5 }); });
	eq(countEv(ev, Ev::AweShort), 4, "12 clicks 0.5 s apart (6 s): 4");
	// another power has its own clock
	ev = capture(*w, [&] { pw::use(*w, "quake", land); });
	eq(countEv(ev, Ev::AweShort), 1, "quake has its own limit");
	// ticks count at the fastest speed: 96 ticks = 1.5 s at 8x
	ev = capture(*w, [&] {
		pw::use(*w, "lightning", land);     // too soon
		w->tick += 95;
		pw::use(*w, "lightning", land);     // 1.48 s: too soon
		w->tick += 2;
		pw::use(*w, "lightning", land);     // ok
	});
	eq(countEv(ev, Ev::AweShort), 1, "ticks as time at 8x");
	// brush held 6 s with no Awe: once per press
	ev = capture(*w, [&] { for (int f = 0; f < 360; f++) pw::use(*w, "rain", land, { .ang = -1, .dt = f ? 1.0 / 60 : 0.0 }); });
	eq(countEv(ev, Ev::AweShort), 1, "brush held 6 s: one awe:short per press");
	ev = capture(*w, [&] { for (int f = 0; f < 60; f++) pw::use(*w, "rain", land, { .ang = -1, .dt = f ? 1.0 / 60 : 0.0 }); });
	eq(countEv(ev, Ev::AweShort), 1, "a new press (1.5 s later): one more");
	ev = capture(*w, [&] { for (int f = 0; f < 30; f++) pw::use(*w, "rain", land, { .ang = -1, .dt = f ? 1.0 / 60 : 0.0 }); });
	eq(countEv(ev, Ev::AweShort), 0, "a new press only 1 s after the last: none");
	eq(countEv(ev, Ev::PowerUsed), 0, "no power:used without Awe");
}

DM_TEST("powers: never fails hard on bad cells, ids, angles or dt; no NaN") {
	stub::Guard guard(false);
	auto w = histWorld(4);
	const int cu = culture(*w);
	town(*w, findLand(*w), cu, 100);
	const double nan = std::nan("");
	const double inf = std::numeric_limits<double>::infinity();
	int calls = 0, accepted = 0;
	for (Phase ph : kPhases) {
		w->phase = ph;
		for (const Row& r : kSpec) {
			for (int c : { -7, -1, 0, w->n - 1, w->n, 1 << 30, findLand(*w), seaCell(*w) }) {
				for (double a : { -1.0, 0.0, nan, inf, -inf, 1e9 }) {
					for (double dt : { 0.0, 1.0 / 60, nan, inf, -1.0, 1e9 }) {
						if (pw::use(*w, r.id, c, { .ang = a, .dt = dt })) accepted++;
						(void)pw::can(*w, r.id, c);
						(void)pw::cost(*w, r.id, a, c);
						calls++;
					}
				}
			}
		}
	}
	ok(!pw::use(*w, "nope", 0) && !pw::use(*w, "", -1), "unknown ids refused");
	std::printf("    %d hostile calls, %d accepted, no crash\n", calls, accepted);
	eq(checkFinite(*w), std::string(""), "finite world");
	ok(std::isfinite(w->god.awe) && w->god.awe >= 0, "awe finite and >= 0");
	// a world with no geo-backed caches and an empty one
	auto e = state::create({ .seed = 1, .L = 3, .geo = sharedGeo(3) });
	watch(*e);
	e->phase = Phase::History;
	e->god.awe = 500;
	for (const Row& r : kSpec) for (int c = 0; c < 5; c++) pw::use(*e, r.id, c, { .ang = -1, .dt = 0.1 });
	eq(checkFinite(*e), std::string(""), "bare world stays finite");
}

// ================================================================== instant effects

DM_TEST("powers: comet, meteor and volcano hand off to powers::start; with the disasters stub they fail gracefully") {
	stub::Guard guard(true);
	auto w = histWorld(5);
	const int cu = culture(*w);
	const int land = findLand(*w, [&](int c) { return w->coast[size_t(c)] >= 3; });
	const int sid = town(*w, land, cu, 300);
	const int far = landAway(*w, { land }, 0.6);
	const int ocean = seaCell(*w);
	if (!stub::stubbed("disasters")) return;
	// no hook: neutral start -> nullptr
	w->god.awe = 1000;
	auto ev = capture(*w, [&] {
		ok(!pw::use(*w, "meteor", land), "meteor refused by the stub");
		ok(!pw::use(*w, "volcano", land), "volcano refused by the stub");
	});
	near(w->god.awe, 1000, 1e-12, "nothing spent");
	eq(countEv(ev, Ev::PowerUsed), 0, "no power:used");
	eq(int(w->god.deeds.size()), 0, "no deed");
	w->phase = Phase::Water;
	ok(!pw::use(*w, "comet", ocean), "comet refused by the stub");
	eq(stub::called("powers_start"), 3, "start asked three times");
	// with a start hook: Disaster created, spend, events, witness at launch with the harmed ring
	hookStart();
	if (stub::stubbed("society")) hookWitness();
	ok(pw::use(*w, "comet", ocean), "comet");
	if (eq(int(scalls().size()), 1, "start called")) {
		eq(int(scalls()[0].type), int(DisasterType::Comet), "comet type");
		near(scalls()[0].o.ang.value_or(-1), 0.03, 1e-12, "comet ang 0.03");
		ok(!scalls()[0].o.natural, "divine");
	}
	eq(int(w->disasters.size()), 1, "a comet in w.disasters");
	eq(wcalls().size(), size_t(0), "comet: no witness");
	w->phase = Phase::History;
	ok(pw::use(*w, "meteor", land), "meteor");
	near(w->god.awe, 850, 1e-9, "150 spent");
	ok(pw::use(*w, "volcano", far), "volcano");
	near(w->god.awe, 630, 1e-9, "220 spent");
	eq(int(w->disasters.size()), 3, "three disasters");
	if (scalls().size() == 3) {
		eq(int(scalls()[1].type), int(DisasterType::Meteor), "meteor type");
		near(scalls()[1].o.ang.value_or(-1), 0.05, 1e-12, "meteor ang 0.05");
		eq(int(scalls()[2].type), int(DisasterType::Volcano), "volcano type");
		near(scalls()[2].o.ang.value_or(-1), 0.04, 1e-12, "volcano ang 0.04");
	}
	if (scalls().size() >= 2) {
		const auto& dd = scalls()[1].o.data;
		ok(dd.count("witness") && dd.at("witness") == 1, "meteor asks Disasters to witness it at impact (data witness = 1)");
		if (scalls().size() >= 3) ok(!scalls()[2].o.data.count("witness"), "volcano does not (Powers witnesses it)");
	}
	if (stub::stubbed("society") && eq(int(wcalls().size()), 1, "only the volcano is witnessed at launch")) {
		const WCall& v = wcalls()[0];
		near(v.ang, 0.40, 1e-12, "volcano wAng");
		near(v.o.faith, 0.25, 1e-12, "volcano faith");
		near(v.o.good, 0, 1e-12, "volcano good 0 for others");
		near(v.o.goodHarmed, -1, 1e-12, "volcano -1 damaged");
		ok(v.o.harmed.empty(), "nobody near the far vent");
	}
	eq(w->god.deeds["meteor"], 1, "meteor deed");
	eq(w->god.deeds["volcano"], 1, "volcano deed");
	// a vent beside the town: the town is in the harmed group
	if (stub::stubbed("society")) {
		wcalls().clear();
		ok(pw::use(*w, "volcano", w->settlements[size_t(sid)].cells.back()), "volcano beside the town");
		ok(!wcalls().empty() && wcalls()[0].o.harmed == std::vector<int>{ sid }, "the town in the ash zone is harmed");
		w->disasters.pop_back();
		scalls().pop_back();
	}
	// volcano on the sea is a target error, not a start call
	ok(!pw::use(*w, "volcano", ocean), "volcano on sea refused");
	eq(int(scalls().size()), 3, "no start for a bad target");
}

DM_TEST("powers: seed, bloom and rain") {
	stub::Guard guard(true);
	auto w = histWorld(5);
	const int land = findLand(*w, [&](int c) { return w->coast[size_t(c)] >= 4; });
	// seed in phase Life, with the given ang
	w->phase = Phase::Life;
	int seedCell = -2;
	double seedAng = -1;
	stub::hooks().life_seed = [&](World&, int c, double a) { seedCell = c; seedAng = a; };
	ok(pw::use(*w, "seed", land), "seed (def ang)");
	if (stub::stubbed("life")) { eq(seedCell, land, "life::seed at the cell"); near(seedAng, 0.12, 1e-12, "def ang 0.12"); }
	ok(pw::use(*w, "seed", land, { .ang = 0.2 }), "seed (ang 0.2)");
	if (stub::stubbed("life")) near(seedAng, 0.2, 1e-12, "seed honours ang");
	ok(!pw::use(*w, "seed", seaCell(*w)), "seed on sea refused");
	eq(w->stats.miracles, 2, "two seed miracles");
	stub::hooks().life_seed = nullptr;
	// bloom: life::bloom with 0.25 per second
	w->phase = Phase::History;
	double bloomAmt = 0, bloomAng = 0;
	int bloomCalls = 0;
	stub::hooks().life_bloom = [&](World&, int, double a, double amount) { bloomAmt += amount; bloomAng = a; bloomCalls++; };
	for (int f = 0; f < 60; f++) pw::use(*w, "bloom", land, { .ang = 0.1, .dt = f ? 1.0 / 60 : 0.0 });
	if (stub::stubbed("life")) {
		near(bloomAmt, 0.25 * 59 / 60.0, 1e-9, "bloom amount 0.25 per second held");
		near(bloomAng, 0.1, 1e-12, "bloom brush ang");
		eq(bloomCalls, 59, "one bloom call per frame with dt > 0");
	}
	stub::hooks().life_bloom = nullptr;
	// rain: wet toward 1, fert up, need.climate, fires get wet enough to go out
	std::vector<int32_t> disk;
	w->geo->within(land, 0.1, disk);
	for (int c : disk) { w->fire[size_t(c)] = 0.8f; w->veg[size_t(c)] = 0.6f; }
	const std::vector<float> fert0 = w->fert;
	w->need.climate = false;
	w->dirty.color = false;
	for (int f = 0; f < 60; f++) pw::use(*w, "rain", land, { .ang = 0.1, .dt = f ? 1.0 / 60 : 0.0 });
	ok(w->wet[size_t(land)] > 0.9, "centre wet after 1 s: " + std::to_string(w->wet[size_t(land)]));
	int wetFires = 0, fires = 0;
	double fertUp = 0;
	for (int c : disk) {
		if (w->geo->angle(land, c) < 0.07) { fires++; if (w->wet[size_t(c)] > 0.3) wetFires++; }
		fertUp += w->fert[size_t(c)] - fert0[size_t(c)];
	}
	eq(wetFires, fires, "every burning cell inside 0.7 x ang is wetter than 0.3 (Life puts it out)");
	ok(fertUp > 0, "fertility up: " + std::to_string(fertUp));
	near(w->fert[size_t(land)] - fert0[size_t(land)], std::min(0.02 * 59 / 60.0, std::max(0.0, 1.2 - fert0[size_t(land)])), 1e-4, "fert +0.02/s at the centre");
	ok(w->need.climate && w->dirty.color, "need.climate and dirty.color");
	int seaWet = 0;
	for (int c = 0; c < w->n; c++) if (state::isSea(*w, c) && w->wet[size_t(c)] > 0) seaWet++;
	eq(seaWet, 0, "rain never wets the sea");
	if (!stub::stubbed("life")) {
		life::burn(*w);
		int still = 0;
		for (int c : disk) if (w->geo->angle(land, c) < 0.07 && w->fire[size_t(c)] > 0) still++;
		eq(still, 0, "real Life: the fires went out");
	}
	noNaN(*w, "seed bloom rain");
}

DM_TEST("powers: raise / lower sculpt at 1.5 u/s in genesis and 0.2 u/s in history; drowned centres destroyed") {
	stub::Guard guard(true);
	if (!stub::stubbed("terrain") || !stub::stubbed("civ")) return;
	auto w = histWorld(5);
	const int cu = culture(*w);
	const int land = findLand(*w, [&](int c) { return w->coast[size_t(c)] >= 3; });
	double total = 0, lastAng = 0;
	int lastCell = -1;
	stub::hooks().terrain_sculpt = [&](World&, int c, double a, double amount) { total += amount; lastAng = a; lastCell = c; return 1; };
	auto hold = [&](const char* id, double secs) {
		total = 0;
		const int frames = int(std::lround(secs * 60));
		for (int f = 0; f <= frames; f++) pw::use(*w, id, land, { .ang = 0.05, .dt = f ? 1.0 / 60 : 0.0 });
		return total;
	};
	w->phase = Phase::Water;
	w->god.awe = 0;
	near(hold("raise", 2), 3.0, 1e-9, "genesis raise: 1.5 u/s x 2 s, free");
	near(hold("lower", 1), -1.5, 1e-9, "genesis lower: -1.5 u/s");
	eq(lastCell, land, "at the brush centre");
	near(lastAng, 0.05, 1e-12, "brush ang");
	near(w->god.awe, 0, 1e-12, "genesis sculpting is free");
	w->phase = Phase::History;
	w->god.awe = 1000;
	near(hold("raise", 2), 0.4, 1e-9, "history raise: 0.2 u/s");
	near(1000 - w->god.awe, 6 * (0.05 / 0.06) * (0.05 / 0.06) * 2, 1e-6, "history cost 6/s x area");
	// owned land: x4 and good -0.5 for the owner
	const int sid = town(*w, land, cu, 200);
	if (stub::stubbed("society")) hookWitness();
	w->god.awe = 1000;
	hold("lower", 1);
	near(1000 - w->god.awe, 4 * 6 * (0.05 / 0.06) * (0.05 / 0.06), 1e-6, "owned land x4");
	bool harmedOk = !wcalls().empty();
	for (const WCall& c : wcalls()) {
		harmedOk = harmedOk && c.o.harmed == std::vector<int>{ sid } && std::fabs(c.o.goodHarmed + 0.5) < 1e-12 && c.o.good == 0;
		near(c.ang, 0.1, 1e-12, "sculpt witness radius 2 x ang");
	}
	if (stub::stubbed("society")) ok(harmedOk, "sculpting owned land: -0.5 for its owner, 0 for others (" + std::to_string(wcalls().size()) + " calls, first harmed " + (wcalls().empty() ? std::string("-") : show(wcalls()[0].o.harmed)) + ")");
	// (v3, design review finding 10) one press stops the centre at sea level + 0.05; a second press
	// drowns it -> destroy(drowned)
	int destroyed = -1;
	Cause why = Cause::None;
	stub::hooks().civ_destroy = [&](World& ww, int s, Cause c) { destroyed = s; why = c; ww.settlements[size_t(s)].dead = true; };
	stub::hooks().terrain_sculpt = [&](World& ww, int c, double, double amount) { ww.elev[size_t(c)] += float(amount * 40); return 1; };
	w->elev[size_t(land)] = float(w->seaLevel + 0.3);
	int warned = 0;
	w->bus.on<EvDrownWarning>([&](const EvDrownWarning& e) { if (e.id == sid) warned++; });
	for (int f = 0; f < 300 && destroyed < 0; f++) pw::use(*w, "lower", land, { .ang = 0.05, .dt = f ? 1.0 / 60 : 0.0 });
	eq(destroyed, -1, "one press does not drown the town");   // (the floor itself: test_integration_review, real Terrain)
	eq(warned, 1, "drown:warning once");
	for (int f = 0; f < 300 && destroyed < 0; f++) pw::use(*w, "lower", land, { .ang = 0.05, .dt = f ? 1.0 / 60 : 0.0 });
	eq(destroyed, sid, "the town was destroyed when its centre sank (second press)");
	eq(int(why), int(Cause::Drowned), "cause drowned");
	ok(state::isSea(*w, land), "the centre is sea");
}

DM_TEST("powers: lightning ignites, hurts the town at or next to the cell (5-12 %), kills a unit there") {
	stub::Guard guard(true);
	if (!stub::stubbed("civ") || !stub::stubbed("life") || !stub::stubbed("units") || !stub::stubbed("society")) return;
	auto w = histWorld(5);
	const int cu = culture(*w);
	const int a = findLand(*w, [&](int c) { return w->coast[size_t(c)] >= 3; });
	const int sa = town(*w, a, cu, 1000);
	const int b = landAway(*w, { a }, 0.3);
	const int sb = town(*w, b, cu, 1000);
	hookDamage();
	hookWitness();
	int ignited = -1;
	stub::hooks().life_ignite = [&](World&, int c, double) { ignited = c; return true; };
	std::vector<int> killed;
	stub::hooks().units_kill = [&](World&, int id, std::string_view cause) { killed.push_back(id); ok(cause == "lightning", "unit cause lightning"); return true; };
	Unit& u = state::newUnit(*w, UnitType::Army);
	u.culture = cu; u.path = { a }; u.dest = a;
	const int uid = u.id;
	const int nb = w->geo->nbr[size_t(6 * a)];
	auto ev = capture(*w, [&] { ok(pw::use(*w, "lightning", nb), "strike next to the town"); });
	eq(countEv(ev, Ev::Lightning), 1, "lightning event");
	if (const EvLightning* e = ev.empty() ? nullptr : ev.front().as<EvLightning>()) { eq(e->cell, nb, "event cell"); ok(!e->natural, "divine"); }
	eq(ignited, nb, "life::ignite at the cell");
	if (eq(int(dcalls().size()), 1, "one town hurt")) {
		eq(dcalls()[0].sid, sa, "the adjacent town");
		ok(dcalls()[0].frac >= 0.05 && dcalls()[0].frac <= 0.12, "5-12 %: " + std::to_string(dcalls()[0].frac));
		eq(int(dcalls()[0].cause), int(Cause::Lightning), "cause lightning");
	}
	ok(w->settlements[size_t(sa)].pop < 1000 && w->settlements[size_t(sb)].pop == 1000, "only the near town lost people");
	ok(killed.empty(), "the unit is on the centre, not under the strike");
	if (eq(int(wcalls().size()), 1, "witness")) {
		near(wcalls()[0].ang, 0.10, 1e-12, "wAng 0.10");
		near(wcalls()[0].o.faith, 0.05, 1e-12, "faith 0.05");
		ok(wcalls()[0].o.harmed == std::vector<int>{ sa } && wcalls()[0].o.goodHarmed == -0.6 && wcalls()[0].o.good == 0, "-0.6 if it hurt them, else 0");
	}
	pw::use(*w, "lightning", a);
	ok(killed == std::vector<int>{ uid }, "a unit at the cell dies");
	// many strikes: the loss range
	double lo = 1, hi = 0;
	for (int i = 0; i < 200; i++) {
		dcalls().clear();
		pw::use(*w, "lightning", b);
		for (const DCall& d : dcalls()) { lo = std::min(lo, d.frac); hi = std::max(hi, d.frac); }
	}
	ok(lo >= 0.05 && hi <= 0.12 && hi - lo > 0.05, "200 strikes: losses " + std::to_string(lo) + " .. " + std::to_string(hi));
	std::printf("    lightning losses over 200 strikes: %.4f .. %.4f\n", lo, hi);
}

DM_TEST("powers: quake damages 15-35 % within 0.08 rad, roughens, starts a tsunami under the sea") {
	stub::Guard guard(true);
	if (!stub::stubbed("civ") || !stub::stubbed("terrain") || !stub::stubbed("society") || !stub::stubbed("disasters")) return;
	auto w = histWorld(5);
	const int cu = culture(*w);
	const int a = findLand(*w, [&](int c) { return w->coast[size_t(c)] >= 4; });
	const int sa = town(*w, a, cu, 1000);
	std::vector<int32_t> ring2;
	w->geo->ring(a, 2, ring2);
	const int b = ring2.back();             // 2 hops away: within 0.08 rad at L 5
	const int sb = town(*w, b, cu, 1000, 0);
	const int c = landAway(*w, { a }, 0.25);
	const int sc = town(*w, c, cu, 1000);
	hookDamage();
	hookWitness();
	hookStart();
	int rough = -1;
	double roughAng = 0, roughAmt = 0;
	stub::hooks().terrain_roughen = [&](World&, int cc, double ang, double amount, Rng&) { rough = cc; roughAng = ang; roughAmt = amount; };
	auto ev = capture(*w, [&] { ok(pw::use(*w, "quake", a), "quake"); });
	eq(countEv(ev, Ev::Quake), 1, "quake event");
	if (const EvQuake* e = ev.front().as<EvQuake>()) { eq(e->cell, a, "cell"); near(e->ang, 0.08, 1e-12, "ang"); ok(!e->natural, "divine"); }
	eq(rough, a, "terrain::roughen at the cell");
	near(roughAng, 0.08, 1e-12, "roughen over 0.08");
	ok(roughAmt > 0, "roughen amount");
	std::vector<int> hurt;
	for (const DCall& d : dcalls()) {
		hurt.push_back(d.sid);
		ok(d.frac >= 0.15 && d.frac <= 0.35, "15-35 %: " + std::to_string(d.frac));
		eq(int(d.cause), int(Cause::Quake), "cause quake");
	}
	std::sort(hurt.begin(), hurt.end());
	ok(hurt == (std::vector<int>{ sa, sb }), "both towns within 0.08 rad are hurt (angle " + std::to_string(w->geo->angle(a, b)) + ")");
	eq(w->settlements[size_t(sc)].pop, 1000.0, "far town untouched");
	eq(int(scalls().size()), 0, "no tsunami on land");
	if (eq(int(wcalls().size()), 1, "witness")) {
		const WCall& k = wcalls()[0];
		near(k.ang, 0.20, 1e-12, "wAng 0.20");
		near(k.o.faith, 0.10, 1e-12, "faith 0.10");
		ok(k.o.goodHarmed == -0.8 && k.o.good == -0.2, "-0.8 damaged, -0.2 others");
		std::vector<int> hs = k.o.harmed;
		std::sort(hs.begin(), hs.end());
		ok(hs == hurt, "harmed = the damaged towns");
	}
	const int ocean = seaCell(*w, -2.0);
	ok(pw::use(*w, "quake", ocean), "quake at sea");
	if (eq(int(scalls().size()), 1, "tsunami started")) {
		eq(int(scalls()[0].type), int(DisasterType::Tsunami), "type tsunami");
		eq(scalls()[0].cell, ocean, "at the epicentre");
	}
}

DM_TEST("powers: plague / heal / inspire go through civ::infect / civ::cure; blessed 30, inspire 40") {
	stub::Guard guard(true);
	auto w = histWorld(5);
	const int cu = culture(*w);
	const int a = findLand(*w, [&](int c) { return w->coast[size_t(c)] >= 3; });
	const int sid = town(*w, a, cu, 500);
	if (stub::stubbed("society")) hookWitness();
	if (stub::stubbed("civ")) {
		double sev = -1;
		bool immune = false;
		stub::hooks().civ_infect = [&](World& ww, int s, double severity) {
			sev = severity;
			Settlement& t = ww.settlements[size_t(s)];
			if (immune || t.immune > 0) return false;
			t.plague = std::max(t.plague, severity);
			return true;
		};
		ok(pw::use(*w, "plague", a), "plague");
		near(sev, 0.6, 1e-12, "severity 0.6");
		near(w->settlements[size_t(sid)].plague, 0.6, 1e-12, "plague 0.6");
		near(w->god.awe, 920, 1e-9, "80 spent");
		if (stub::stubbed("society") && eq(int(wcalls().size()), 1, "witness")) {
			near(wcalls()[0].ang, 0.12, 1e-12, "plague wAng");
			near(wcalls()[0].o.faith, 0.05, 1e-12, "plague faith");
			near(wcalls()[0].o.good, -0.7, 1e-12, "plague good -0.7");
		}
		immune = true;
		ok(!pw::use(*w, "plague", a), "plague on an immune town refused");
		near(w->god.awe, 920, 1e-9, "nothing spent");
		double imm = -1;
		stub::hooks().civ_cure = [&](World& ww, int s, double years) {
			imm = years;
			ww.settlements[size_t(s)].plague = 0;
			ww.settlements[size_t(s)].immune = years;
			return true;
		};
		ok(pw::use(*w, "heal", a), "heal");
		near(imm, 30, 1e-12, "cure with 30 years of immunity");
		eq(w->settlements[size_t(sid)].plague, 0.0, "plague gone");
	} else {
		ok(pw::use(*w, "heal", a), "heal (real Civ)");
	}
	eq(w->settlements[size_t(sid)].blessed, 30.0, "blessed = 30");
	ok(pw::use(*w, "inspire", a), "inspire");
	eq(w->settlements[size_t(sid)].inspire, 40.0, "inspire = 40");
	w->settlements[size_t(sid)].dead = true;
	ok(!pw::use(*w, "heal", a) && !pw::use(*w, "inspire", a), "dead town: refused");
}

DM_TEST("powers: prophet through society::raiseProphet; Omen of Peace ends every war of the culture") {
	stub::Guard guard(true);
	if (!stub::stubbed("society") || !stub::stubbed("diplomacy")) return;
	auto w = histWorld(5);
	const int c0 = culture(*w), c1 = culture(*w), c2 = culture(*w), c3 = culture(*w);
	const int a = findLand(*w, [&](int c) { return w->coast[size_t(c)] >= 3; });
	const int s0 = town(*w, a, c0, 500);
	const int b = landAway(*w, { a }, 0.3);
	const int s1 = town(*w, b, c1, 500);
	const int d = landAway(*w, { a, b }, 0.3);
	const int s2 = town(*w, d, c2, 500);
	const int e = landAway(*w, { a, b, d }, 0.3);
	town(*w, e, c3, 500);
	const int a2 = landAway(*w, { a, b, d, e }, 0.3);
	const int s0b = town(*w, a2, c0, 300);
	// prophet
	int asked = -1;
	stub::hooks().society_raiseProphet = [&](World& ww, int sid) -> Prophet* {
		asked = sid;
		Culture& cu = ww.cultures[size_t(ww.settlements[size_t(sid)].culture)];
		if (cu.prophet) return nullptr;
		cu.prophet = Prophet{ "Ama", sid, ww.year + 60 };
		return &*cu.prophet;
	};
	hookWitness();
	ok(pw::use(*w, "prophet", a), "prophet");
	eq(asked, s0, "raiseProphet(the target)");
	ok(w->cultures[size_t(c0)].prophet.has_value(), "the culture has a prophet");
	if (eq(int(wcalls().size()), 1, "witness")) { near(wcalls()[0].ang, 0.15, 1e-12, "wAng 0.15"); near(wcalls()[0].o.faith, 0.15, 1e-12, "faith 0.15"); near(wcalls()[0].o.good, 0.5, 1e-12, "good 0.5"); }
	const double aw = w->god.awe;
	ok(!pw::use(*w, "prophet", a), "a second prophet refused");
	near(w->god.awe, aw, 1e-12, "nothing spent");
	// peace: c0 at war with c1 and c2; c2 also at war with c3 (not ended)
	society::rel(*w, c0, c1)->war = true;
	society::rel(*w, c0, c1)->v = -0.8;
	society::rel(*w, c2, c0)->war = true;
	society::rel(*w, c2, c3)->war = true;
	struct P { int a, b; double truce; };
	std::vector<P> peaces;
	stub::hooks().society_makePeace = [&](World& ww, int x, int y, double truce, int) {
		peaces.push_back({ x, y, truce });
		Relation* r = society::rel(ww, x, y);
		r->war = false; r->v = -0.1; r->truce = ww.year + truce;
	};
	hookWitness();
	ok(pw::use(*w, "peace", b), "peace from a c1 town");      // c1 at war with c0 only
	if (eq(int(peaces.size()), 1, "one war ended")) { eq(peaces[0].b, c0, "with c0"); near(peaces[0].truce, 80, 1e-12, "80-year truce"); }
	near(society::peek(*w, c0, c1)->v, 0.3, 1e-9, "relations -0.1 + 0.4");
	std::vector<int> seen;
	for (const WCall& k : wcalls()) {
		seen.push_back(civ::at(*w, k.cell));
		near(k.o.faith, 0.10, 1e-12, "peace faith");
		near(k.o.good, 0.8, 1e-12, "peace good 0.8");
		ok(k.ang < w->geo->step, "each witness sees only its own town");
	}
	std::sort(seen.begin(), seen.end());
	ok(seen == (std::vector<int>{ s0, s1, s0b }), "witnesses: every town of both cultures (" + show(seen) + ")");
	peaces.clear();
	ok(pw::use(*w, "peace", a), "peace from c0 (still at war with c2)");
	eq(int(peaces.size()), 1, "c0-c2 ended");
	ok(society::atWar(*w, c2, c3), "c2-c3 war untouched");
	ok(!pw::use(*w, "peace", a), "c0 at peace now: the omen is refused");
	(void)s2;
}

DM_TEST("powers: awaken founds culture + camp of 30; first use in people calls setPhase(History) and sets Awe 60; never writes phase") {
	stub::Guard guard(true);
	if (!stub::stubbed("society") || !stub::stubbed("civ") || !stub::stubbed("sim")) return;
	auto w = histWorld(5);
	w->phase = Phase::People;
	w->god.awe = 5;
	w->god.aweMax = 100;
	w->age = 0;
	const int land = findLand(*w, [&](int c) { return w->coast[size_t(c)] >= 3; });
	stub::hooks().civ_canSettle = [](const World& ww, int c) { return civ::SettleCheck{ !state::isSea(ww, c), state::isSea(ww, c) ? civ::SettleReason::Sea : civ::SettleReason::Ok }; };
	int cultureAsked = 0, target = land;
	stub::hooks().society_newCulture = [&](World& ww, const society::NewCultureOpts& o) -> Culture* {
		cultureAsked++;
		ok(o.parent == -1 && o.cell == target && o.reason == BirthReason::Awaken, "newCulture { -1, cell, awaken }");
		return &state::newCulture(ww, o.parent);
	};
	struct F { int cell, cu; double pop; FoundedBy by; };
	std::vector<F> founds;
	stub::hooks().civ_found = [&](World& ww, int c, int cu, double pop, FoundedBy by, const civ::FoundOpts&) -> Settlement* {
		founds.push_back({ c, cu, pop, by });
		Settlement& s = state::newSettlement(ww, c, cu);
		s.pop = pop;
		return &s;
	};
	std::vector<Phase> asked;
	stub::hooks().sim_setPhase = [&](World&, Phase p) { asked.push_back(p); return true; };
	ok(!pw::use(*w, "awaken", seaCell(*w)), "awaken on sea refused");
	eq(cultureAsked, 0, "no culture for a refused target");
	ok(pw::use(*w, "awaken", land), "awaken");
	eq(cultureAsked, 1, "one culture");
	if (eq(int(founds.size()), 1, "one camp")) {
		eq(founds[0].cell, land, "at the cell");
		eq(founds[0].cu, 0, "of the new culture");
		eq(founds[0].pop, 30.0, "30 people");
		eq(int(founds[0].by), int(FoundedBy::Awaken), "by awaken");
	}
	ok(asked == std::vector<Phase>{ Phase::History }, "sim::setPhase(History) called once");
	eq(int(w->phase), int(Phase::People), "Powers never writes w.phase (the stub did not either)");
	eq(w->god.awe, 60.0, "awe = max(5, 60)");
	eq(w->god.deeds["awaken"], 1, "deed");
	// in history, with a town alive: costs min(250, aweMax), needs Sail or 450
	w->phase = Phase::History;
	asked.clear();
	const int land2 = landAway(*w, { land }, 0.4);
	ok(!pw::use(*w, "awaken", land2), "history with a town at age 0 / aweMax 100: locked");
	w->god.aweMax = 450;
	w->god.awe = 300;
	target = land2;
	ok(pw::use(*w, "awaken", land2), "aweMax 450: awaken again");
	near(w->god.awe, 50, 1e-9, "250 spent");
	ok(asked.empty(), "no setPhase in history");
	// a refused found: nothing spent
	stub::hooks().civ_found = [](World&, int, int, double, FoundedBy, const civ::FoundOpts&) -> Settlement* { return nullptr; };
	w->god.awe = 300;
	target = landAway(*w, { land, land2 }, 0.4);
	ok(!pw::use(*w, "awaken", target), "found refused -> false");
	near(w->god.awe, 300, 1e-12, "nothing spent");
	// a high Awe is kept by the first awaken
	auto w2 = histWorld(5);
	w2->phase = Phase::People;
	w2->god.awe = 90;
	stub::hooks().civ_found = [&](World& ww, int c, int cu, double pop, FoundedBy, const civ::FoundOpts&) -> Settlement* {
		Settlement& s = state::newSettlement(ww, c, cu); s.pop = pop; return &s;
	};
	target = findLand(*w2);
	ok(pw::use(*w2, "awaken", target), "awaken on w2");
	eq(w2->god.awe, 90.0, "awe = max(90, 60)");
}

DM_TEST("powers: brush witnesses carry 2 x ang, 0.02/s and the table's good (rain +0.6 dry / +0.1, bloom +0.4)") {
	stub::Guard guard(true);
	if (!stub::stubbed("society")) return;
	auto w = histWorld(5);
	const int cu = culture(*w);
	const int a = findLand(*w, [&](int c) { return w->coast[size_t(c)] >= 4; });
	std::vector<int32_t> r3;
	w->geo->ring(a, 3, r3);
	const int dryCell = r3.back();
	const int sDry = town(*w, dryCell, cu, 300, 0);
	const int wetCell = r3[r3.size() / 2 + 3];
	const int sWet = town(*w, wetCell, cu, 300, 0);
	w->moist[size_t(dryCell)] = 0.1f;
	w->moist[size_t(wetCell)] = 0.8f;
	const int pray = landAway(*w, { a }, 0.15, 1);
	(void)pray;
	hookWitness();
	for (int f = 0; f < 32; f++) pw::use(*w, "rain", a, { .ang = 0.12, .dt = f ? 1.0 / 60 : 0.0 });
	if (!ok(!wcalls().empty(), "rain witnessed")) return;
	const WCall& k = wcalls()[0];
	near(k.ang, 0.24, 1e-12, "rain witness radius 2 x 0.12");
	near(k.o.good, 0.1, 1e-12, "+0.1 for others");
	near(k.o.goodHarmed, 0.6, 1e-12, "+0.6 for the dry");
	ok(std::find(k.o.harmed.begin(), k.o.harmed.end(), sDry) != k.o.harmed.end(), "the dry town is in the +0.6 group");
	ok(std::find(k.o.harmed.begin(), k.o.harmed.end(), sWet) == k.o.harmed.end(), "the wet town is not");
	double faith = 0;
	for (const WCall& c : wcalls()) faith += c.o.faith;
	near(faith, 0.02 * 31 / 60.0, 1e-9, "faith 0.02/s for the 31/60 s held (witnessed at frames 1, 16 and 31)");
	// praying for rain counts as dry
	w->settlements[size_t(sWet)].prayer = Prayer{ PrayerKind::Rain, 0 };
	wcalls().clear();
	for (int f = 0; f < 31; f++) pw::use(*w, "rain", a, { .ang = 0.12, .dt = f ? 1.0 / 60 : 0.0 });
	ok(!wcalls().empty() && std::find(wcalls()[0].o.harmed.begin(), wcalls()[0].o.harmed.end(), sWet) != wcalls()[0].o.harmed.end(), "a rain prayer gets +0.6");
	wcalls().clear();
	for (int f = 0; f < 31; f++) pw::use(*w, "bloom", a, { .ang = 0.08, .dt = f ? 1.0 / 60 : 0.0 });
	ok(!wcalls().empty(), "bloom witnessed");
	if (!wcalls().empty()) { near(wcalls()[0].o.good, 0.4, 1e-12, "bloom +0.4"); near(wcalls()[0].ang, 0.16, 1e-12, "bloom 2 x ang"); eq(wcalls()[0].o.kind, std::string("bloom"), "kind"); }
}

DM_TEST("powers: determinism: the same calls give the same world") {
	stub::Guard guard(false);
	auto play = [](void) {
		auto w = histWorld(5, 7);
		const int cu = culture(*w);
		const int a = findLand(*w, [&](int c) { return w->coast[size_t(c)] >= 3; });
		town(*w, a, cu, 800);
		for (int i = 0; i < 40; i++) {
			pw::use(*w, "lightning", (a + i * 37) % w->n);
			pw::use(*w, "quake", (a + i * 53) % w->n);
			for (int f = 0; f < 10; f++) pw::use(*w, "rain", (a + i) % w->n, { .ang = 0.1, .dt = 1.0 / 60 });
			for (int f = 0; f < 10; f++) pw::use(*w, "raise", (a + i * 3) % w->n, { .ang = 0.08, .dt = 1.0 / 30 });
			pw::use(*w, "heal", a);
			w->god.awe = 1000;
		}
		return std::make_pair(hash(*w), w->rng.s);
	};
	const auto h1 = play(), h2 = play();
	eq(h1.first, h2.first, "world hash");
	eq(h1.second, h2.second, "rng state");
	std::printf("    hash %08x rng %08x\n", h1.first, h1.second);
}

// ================================================================== maps

DM_TEST("powers: maps of brush strokes (rain wet, fertility, sculpt amounts) at L 6") {
	stub::Guard guard(false);
	auto w = histWorld(6, 5);
	const int cu = culture(*w);
	std::vector<float> elev0 = w->elev;
	const std::vector<float> fert0 = w->fert;
	// a sculpt hook that applies Terrain's documented falloff (1 - t^2)^2, so the map shows what
	// Powers asked Terrain for (the real Terrain is used in `run.ps1 all`)
	std::vector<float> asked(size_t(w->n), 0.0f);
	if (stub::stubbed("terrain")) {
		stub::hooks().terrain_sculpt = [&](World& ww, int c, double ang, double amount) {
			std::vector<int32_t> out;
			const int k = ww.geo->within(c, ang, out);
			for (int j = 0; j < k; j++) {
				const int m = out[size_t(j)];
				const double t = ww.geo->angle(c, m) / ang;
				const double f = (1 - t * t) * (1 - t * t);
				ww.elev[size_t(m)] += float(amount * f);
				asked[size_t(m)] += float(amount * f);
			}
			return k;
		};
	}
	// strokes on the most inland cell of the planet: two rain strokes, a raise and a lower stroke
	int a = 0;
	for (int c = 0; c < w->n; c++) if (w->coast[size_t(c)] > w->coast[size_t(a)]) a = c;
	const Vec3 p0 = w->geo->dir(a);
	Vec3 e, nn;
	w->geo->basis(a, e, nn);
	std::vector<int> marks;
	auto stroke = [&](const char* id, double ang, double dv, double len, double secs) {
		const int frames = int(secs * 60);
		int used = 0;
		for (int f = 0; f < frames; f++) {
			const double s = len * (double(f) / frames - 0.5);
			const int c = w->geo->cellAt(v3::norm(p0 + e * s + nn * dv));
			if (f % 30 == 0) marks.push_back(c);
			if (pw::use(*w, id, c, { .ang = ang, .dt = f ? 1.0 / 60 : 0.0 })) used++;
		}
		return used;
	};
	town(*w, a, cu, 500);
	w->phase = Phase::History;
	int used = stroke("rain", 0.10, 0.0, 0.35, 4);
	used += stroke("rain", 0.04, -0.16, 0.30, 2);
	w->phase = Phase::Water;       // genesis sculpting: 1.5 u/s
	used += stroke("raise", 0.06, 0.18, 0.20, 4);
	used += stroke("lower", 0.12, -0.36, 0.25, 3);
	w->phase = Phase::History;
	eq(used, 4 * 60 + 2 * 60 + 4 * 60 + 3 * 60, "every frame of every stroke was accepted");
	std::printf("    strokes centred on cell %d (coast %d hops, lat %.1f, lon %.1f)\n", a, int(w->coast[size_t(a)]),
		w->geo->lat[size_t(a)] * 180 / kPi, w->geo->lon[size_t(a)] * 180 / kPi);
	noNaN(*w, "after the strokes");
	const std::string f1 = shot("ue-powers-rain-wet.png"), f2 = shot("ue-powers-fert-delta.png"), f3 = shot("ue-powers-sculpt.png");
	// wet over a plain relief: land grey-green by height, sea dark blue, wet toward bright cyan
	ok(mi::custom(*w, f1, [&](int c) {
		const double el = w->elev[size_t(c)];
		mi::Rgb base = state::isSea(*w, c) ? mi::Rgb{ 20, 30, 70 } : mi::Rgb{ 90 + 14 * el, 110 + 10 * el, 70 + 8 * el };
		const double t = sat(w->wet[size_t(c)]);
		return mi::Rgb{ base.r + (40 - base.r) * t, base.g + (170 - base.g) * t, base.b + (255 - base.b) * t };
	}, { .w = 2048, .h = 1024, .marks = marks }), "wet map");
	std::vector<float> dF(size_t(w->n));
	for (int c = 0; c < w->n; c++) dF[size_t(c)] = w->fert[size_t(c)] - fert0[size_t(c)];
	ok(mi::field(*w, dF, f2, 0, 0.08, { .w = 2048, .h = 1024, .dimSea = true }), "fert map");
	std::vector<float> dE(size_t(w->n));
	for (int c = 0; c < w->n; c++) dE[size_t(c)] = w->elev[size_t(c)] - elev0[size_t(c)];
	ok(mi::field(*w, dE, f3, -3, 3, { .w = 2048, .h = 1024 }), "sculpt map");
	double wetSum = 0, maxWet = 0, maxUp = 0, maxDown = 0, maxFert = 0;
	int wetCells = 0, seaWet = 0;
	for (int c = 0; c < w->n; c++) {
		wetSum += w->wet[size_t(c)];
		maxWet = std::max(maxWet, double(w->wet[size_t(c)]));
		if (w->wet[size_t(c)] > 0.3) wetCells++;
		if (state::isSea(*w, c) && w->wet[size_t(c)] > 0) seaWet++;
		maxUp = std::max(maxUp, double(dE[size_t(c)]));
		maxDown = std::min(maxDown, double(dE[size_t(c)]));
		maxFert = std::max(maxFert, double(dF[size_t(c)]));
	}
	std::printf("    maps: %s, %s, %s\n    wet > 0.3 on %d cells, max wet %.3f, sum %.1f, sea cells wet %d; fert up max %.4f; sculpt max +%.2f / %.2f\n",
		f1.c_str(), f2.c_str(), f3.c_str(), wetCells, maxWet, wetSum, seaWet, maxFert, maxUp, maxDown);
	ok(wetCells > 100, "a broad wet swath");
	eq(seaWet, 0, "no rain on the sea");
	ok(maxUp > 1 && maxDown < -1, "genesis strokes moved the ground");
}
