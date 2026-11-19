// DEMIURGE — SimTests/test_units.cpp : Sim/Units (SPEC-SIM 3.9, BUILD.md A6). Owner: A6 path-units.
//
// Runs with the real Path and every other module stubbed: Civ.found and Society.onArrive are replaced by test
// hooks (StubHooks.h) where a collaborator must act; those tests return early in `run.ps1 all`. Movement is
// checked against a reference written out here from the documented rule.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "Core/Rng.h"
#include "MapImg.h"
#include "Path.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "Units.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

constexpr double kInfD = std::numeric_limits<double>::infinity();

bool seaC(const World& w, int c) { return double(w.elev[size_t(c)]) < w.seaLevel; }

std::unique_ptr<World> flat(int L = 4) {
	auto w = state::create({ .seed = 1, .L = L, .geo = sharedGeo(L) });
	watch(*w);
	std::fill(w->elev.begin(), w->elev.end(), 0.5f);
	w->seaLevel = 0;
	w->heat = 0;
	w->phase = Phase::History;
	return w;
}
int at(const World& w, double x, double y, double z) { return w.geo->cellAt(x, y, z); }
int newCulture(World& w) { return state::newCulture(w).id; }
int newTown(World& w, int cell, int culture, double pop = 100) {
	Settlement& s = state::newSettlement(w, cell, culture);
	s.pop = pop;
	return s.id;
}
const Unit* unitOf(const World& w, int id) { return state::findUnit(w, id); }
double progress(const Unit& u) { return double(u.i) + u.t; }

// time of the step a -> b in open-land steps, written out from the documented rule
double refSeg(const World& w, int a, int b, NavalMode naval) {
	const double sc = path::stepCost(w, a, b, naval);
	if (!(sc < kInfD)) return kInfD;
	const bool sa = seaC(w, a), sb = seaC(w, b);
	if (sa && sb) return 1 / 2.5;
	if (sa || sb) return 1;
	return std::min(3.0, sc);
}
// where a unit that left path[0] at time 0 is after `years` at `speed` (fractional path index)
double refProgress(const World& w, const std::vector<int32_t>& p, NavalMode naval, double speed, double years) {
	double left = speed * years;
	for (size_t i = 0; i + 1 < p.size(); i++) {
		const double s = refSeg(w, p[i], p[i + 1], naval);
		if (left >= s) { left -= s; continue; }
		return double(i) + left / s;
	}
	return double(p.size() - 1);
}

struct Log {
	std::vector<std::string> seq;
	EventBus::Token tok = 0;
	void attach(World& w) { tok = w.bus.onAll([this](const Event& e) { seq.push_back(e.name()); }); }
	int count(const std::string& s) const { return int(std::count(seq.begin(), seq.end(), s)); }
};

} // namespace

DM_TEST("units: UnitsTune carries the documented numbers") {
	const UnitsTune T;
	eq(T.maxUnits, 300); eq(T.speedSettler, 5.0); eq(T.speedTrader, 7.0); eq(T.speedArmy, 5.0); eq(T.speedProphet, 4.0);
	eq(T.seaMul, 2.5); eq(T.lavaKill, 0.2); eq(T.maxCost, 400.0);
	const units::SpawnOpts o;
	ok(o.type == UnitType::Settler && o.to == -1 && o.naval == NavalMode::Land && o.path.empty(), "SpawnOpts defaults");
}

DM_TEST("units: spawn makes a unit through state::newUnit with every field, and emits unit:spawned") {
	auto w = flat();
	w->year = 12;
	const int cu = newCulture(*w);
	const int a = at(*w, 1, 0, 0), b = at(*w, 0, 0, 1);
	const int s0 = newTown(*w, a, cu);
	const int nextBefore = w->nextId.unit;
	units::SpawnOpts o;
	o.type = UnitType::Settler; o.culture = cu; o.from = s0; o.dest = b; o.size = 30; o.data["x"] = 4;
	int id = -1;
	auto ev = capture(*w, [&] { Unit* u = units::spawn(*w, o); if (u) id = u->id; });
	const Unit* u = unitOf(*w, id);
	if (!ok(u != nullptr, "spawned")) return;
	eq(u->id, nextBefore, "id from nextId"); eq(w->nextId.unit, nextBefore + 1);
	ok(u->type == UnitType::Settler); eq(u->culture, cu); eq(u->from, s0); eq(u->to, -1); eq(u->dest, b);
	eq(u->path.front(), a, "path starts at the origin"); eq(u->path.back(), b, "path ends at dest");
	eq(u->i, 0); eq(u->t, 0.0); eq(u->speed, 5.0); eq(u->size, 30.0); ok(u->naval == NavalMode::Land); ok(!u->sea);
	eq(u->born, 12.0, "born = year"); eq(u->data.at("x"), 4.0, "data copied");
	near(path::cost(*w, u->path, NavalMode::Land), double(u->path.size() - 1), 1e-9, "flat land: an optimal route");
	if (ok(ev.size() == 1 && ev[0].type == Ev::UnitSpawned, "one unit:spawned")) {
		const EvUnitSpawned* e = ev[0].as<EvUnitSpawned>();
		eq(e->id, id); ok(e->type == UnitType::Settler); eq(e->culture, cu); eq(e->cell, a, "cell = start");
	}
	// speed by type; dest defaults to the centre of `to`; ids ascend
	const int cu2 = newCulture(*w);
	const int s1 = newTown(*w, b, cu2);
	const UnitType types[4] = { UnitType::Settler, UnitType::Trader, UnitType::Army, UnitType::Prophet };
	const double speeds[4] = { 5, 7, 5, 4 };
	int last = id;
	for (int k = 0; k < 4; k++) {
		Unit* v = units::spawn(*w, { .type = types[k], .culture = cu, .from = s0, .to = s1, .size = 10, .naval = NavalMode::Coastal });
		if (!ok(v != nullptr, "spawn type " + std::to_string(k))) continue;
		eq(v->speed, speeds[k], "speed by type"); eq(v->dest, b, "dest = centre of to"); eq(v->to, s1);
		ok(v->naval == NavalMode::Coastal, "naval carried"); ok(v->id > last, "ids ascend");
		last = v->id;
	}
	ok(std::is_sorted(w->units.begin(), w->units.end(), [](const Unit& x, const Unit& y) { return x.id < y.id; }), "w.units ascending by id");
}

DM_TEST("units: spawn returns nullptr, and changes nothing, without a path, at the cap, or for bad input") {
	auto w = flat();
	const int cu = newCulture(*w);
	const int a = at(*w, 1, 0, 0), b = at(*w, -1, 0, 0);
	const int s0 = newTown(*w, a, cu);
	const int dead = newTown(*w, at(*w, 0, 1, 0), cu);
	w->settlements[size_t(dead)].dead = true;
	for (int c = 0; c < w->n; c++) if (std::fabs(w->geo->pos[size_t(3 * c)]) < 0.15) w->elev[size_t(c)] = -2;   // a deep channel
	const uint32_t h0 = hash(*w);
	const int next0 = w->nextId.unit;
	auto tryIt = [&](const units::SpawnOpts& o, const std::string& why) {
		Unit* u = nullptr;
		auto ev = capture(*w, [&] { u = units::spawn(*w, o); });
		ok(u == nullptr, why + ": nullptr");
		ok(ev.empty() && w->units.empty() && w->nextId.unit == next0 && hash(*w) == h0, why + ": nothing changed");
	};
	tryIt({ .culture = cu, .from = s0, .dest = b }, "no route on foot");
	tryIt({ .culture = cu, .from = s0, .dest = b, .naval = NavalMode::Coastal }, "no route for canoes over deep water");
	tryIt({ .culture = cu, .from = dead, .dest = a }, "dead origin");
	tryIt({ .culture = cu, .from = 99, .dest = a }, "unknown origin");
	tryIt({ .culture = cu, .from = -1, .dest = a }, "no origin");
	tryIt({ .culture = 7, .from = s0, .dest = a }, "unknown culture");
	tryIt({ .culture = cu, .from = s0, .to = 42, .dest = a }, "unknown target");
	tryIt({ .culture = cu, .from = s0, .dest = w->n }, "bad dest");
	tryIt({ .culture = cu, .from = s0, .dest = -1 }, "no dest and no target");
	tryIt({ .type = UnitType(9), .culture = cu, .from = s0, .dest = a }, "bad type");
	tryIt({ .culture = cu, .from = s0, .dest = b, .naval = NavalMode::Open, .maxCost = 2 }, "over its maxCost");
	Unit* sea = units::spawn(*w, { .culture = cu, .from = s0, .dest = b, .naval = NavalMode::Open });
	ok(sea != nullptr, "by open sea the channel is crossed");
	// the cap
	w->units.clear();
	int made = 0;
	for (int k = 0; k < 310; k++)
		if (units::spawn(*w, { .culture = cu, .from = s0, .dest = w->geo->nbr[size_t(6 * a)], .size = 1 })) made++;
	eq(made, 300, "at most 300 units alive");
	eq(int(w->units.size()), 300);
	units::kill(*w, w->units[0].id, "test");
	ok(units::spawn(*w, { .culture = cu, .from = s0, .dest = a }) != nullptr, "room again after one died");
	Unit* nanSize = nullptr;
	w->units.clear();
	nanSize = units::spawn(*w, { .culture = cu, .from = s0, .dest = a, .size = std::nan("") });
	ok(nanSize && nanSize->size == 0, "a NaN size becomes 0");
	noNaN(*w);
}

DM_TEST("units: spawn with a ready path (no search) and with a route budget") {
	auto w = flat();
	const int cu = newCulture(*w);
	const int a = at(*w, 1, 0, 0);
	const int s0 = newTown(*w, a, cu);
	// a detour: a, n1, n2 where n2 is a neighbour of n1
	const int n1 = w->geo->nbr[size_t(6 * a)], n2 = w->geo->nbr[size_t(6 * n1 + 3)];
	Unit* u = units::spawn(*w, { .culture = cu, .from = s0, .dest = n2, .path = { a, n1, n2 } });
	if (ok(u != nullptr, "a valid chain is taken as is")) ok(u->path == std::vector<int32_t>({ a, n1, n2 }), "verbatim");
	const int farCell = at(*w, -1, 0, 0);
	ok(units::spawn(*w, { .culture = cu, .from = s0, .dest = farCell, .path = { a, farCell } }) == nullptr, "not a chain of neighbours");
	ok(units::spawn(*w, { .culture = cu, .from = s0, .dest = n2, .path = { n1, n2 } }) == nullptr, "does not start at the origin");
	ok(units::spawn(*w, { .culture = cu, .from = s0, .dest = n1, .path = { a, n1, n2 } }) == nullptr, "does not end at dest");
	w->lava[size_t(n1)] = 1;
	ok(units::spawn(*w, { .culture = cu, .from = s0, .dest = n2, .path = { a, n1, n2 } }) == nullptr, "an impassable chain");
}

DM_TEST("units: pace on open land: settler 5, trader 7, army 5, prophet 4 cells per year, whatever the tick") {
	for (double dt : { 0.25, 0.1, 1.0 }) {
		auto w = flat(5);
		const int cuA = newCulture(*w), cuB = newCulture(*w);
		const int a = at(*w, 0, 1, 0), b = at(*w, 0, -1, 0);
		const int s0 = newTown(*w, a, cuA), s1 = newTown(*w, b, cuB);
		w->vars.rel[state::relKey(cuA, cuB)].war = true;
		const UnitType types[4] = { UnitType::Settler, UnitType::Trader, UnitType::Army, UnitType::Prophet };
		const double speeds[4] = { 5, 7, 5, 4 };
		std::vector<int> ids;
		for (UnitType t : types) {
			Unit* u = units::spawn(*w, { .type = t, .culture = cuA, .from = s0, .to = s1, .size = 20 });
			ids.push_back(u ? u->id : -1);
		}
		const int ticks = int(std::lround(2.0 / dt));
		for (int k = 0; k < ticks; k++) units::step(*w, dt);
		for (int k = 0; k < 4; k++) {
			const Unit* u = unitOf(*w, ids[size_t(k)]);
			if (!ok(u != nullptr, "still travelling")) continue;
			near(progress(*u), 2 * speeds[k], 1e-9, "2 years at " + std::to_string(speeds[k]) + " cells/yr, dt " + std::to_string(dt));
			ok(!u->sea, "on land");
		}
	}
}

DM_TEST("units: terrain sets the pace (slopes, forest, ice, lakes slow; roads help; capped at 3) as documented") {
	auto w = makeWorld({ .L = 5, .seed = 2 });
	Rng r(3);
	std::vector<int32_t> cells;
	for (int k = 0; k < 40; k++) {                                   // roads, ponds, forest
		w->geo->ring(r.integer(0, w->n - 1), 2, cells);
		const int kind = k % 3;
		for (int c : cells) {
			if (seaC(*w, c)) continue;
			if (kind == 0) w->road[size_t(c)] = uint8_t(1 + r.integer(0, 2));
			else if (kind == 1) w->water[size_t(c)] = 0.2f;
			else w->veg[size_t(c)] = 0.8f;
		}
	}
	const int cu = newCulture(*w);
	int checked = 0, bad = 0;
	double fastest = 1e9, slowest = 0;
	for (int k = 0; k < 12; k++) {
		const int a = findLand(*w, [&](int c) { return hashf(uint32_t(c), uint32_t(k)) < 0.01; });
		const int b = findLand(*w, [&](int c) { return hashf(uint32_t(c), uint32_t(k + 50)) < 0.01 && w->cache.landId[size_t(c)] == w->cache.landId[size_t(a)] && c != a; });
		if (a < 0 || b < 0) continue;
		const int s = newTown(*w, a, cu);
		Unit* u = units::spawn(*w, { .culture = cu, .from = s, .dest = b, .maxCost = 1e9 });
		if (!u) continue;
		const int id = u->id;
		const std::vector<int32_t> route = u->path;
		for (int t = 1; t <= 12; t++) {
			units::step(*w, 0.25);
			const Unit* v = unitOf(*w, id);
			const double expect = refProgress(*w, route, NavalMode::Land, 5, 0.25 * t);
			if (!v) { if (expect < double(route.size() - 1) - 1e-9) bad++; break; }
			checked++;
			if (std::fabs(progress(*v) - expect) > 1e-9) { if (bad++ < 4) fail("progress " + std::to_string(progress(*v)) + " vs reference " + std::to_string(expect)); }
		}
		for (size_t i = 0; i + 1 < route.size(); i++) { const double s2 = refSeg(*w, route[i], route[i + 1], NavalMode::Land); fastest = std::min(fastest, s2); slowest = std::max(slowest, s2); }
	}
	eq(bad, 0, std::to_string(checked) + " positions compared with the reference");
	ok(checked > 50, "enough journeys");
	ok(fastest < 1 && slowest > 1 && slowest <= 3, "roads made steps faster (" + std::to_string(fastest) + "), terrain slower (" + std::to_string(slowest) + "), never beyond 3");
}

DM_TEST("units: at sea 2.5 times faster; `sea` is true exactly while the current cell is a sea cell") {
	auto w = flat(5);
	for (int c = 0; c < w->n; c++) if (std::fabs(w->geo->pos[size_t(3 * c)]) < 0.4) w->elev[size_t(c)] = -2;   // a wide strait
	const int cu = newCulture(*w);
	const int a = at(*w, -1, 0, 0.1), b = at(*w, 1, 0, -0.1);
	const int s0 = newTown(*w, a, cu);
	Unit* u = units::spawn(*w, { .culture = cu, .from = s0, .dest = b, .size = 9, .naval = NavalMode::Open });
	if (!ok(u != nullptr)) return;
	const int id = u->id;
	const std::vector<int32_t> route = u->path;
	int seaSteps = 0, shore = 0;
	for (size_t i = 0; i + 1 < route.size(); i++) {
		const bool sa = seaC(*w, route[i]), sb = seaC(*w, route[i + 1]);
		if (sa && sb) seaSteps++;
		if (sa != sb) shore++;
	}
	ok(seaSteps > 10 && shore == 2, "the route sails " + std::to_string(seaSteps) + " sea steps with one embark and one landing");
	int mism = 0, ticks = 0, seaTicks = 0;
	for (; ticks < 400; ticks++) {
		const Unit* v = unitOf(*w, id);
		if (!v) break;
		if (v->sea != seaC(*w, units::cellOf(*w, *v))) mism++;
		if (v->sea) seaTicks++;
		near(progress(*v), refProgress(*w, route, NavalMode::Open, 5, 0.1 * ticks), 1e-9, "follows the reference");
		units::step(*w, 0.1);
	}
	eq(mism, 0, "sea flag = current cell is sea");
	ok(seaTicks > 0, "it was a ship for a while");
	// whole journey time = land steps / 5 + 2 x 1 / 5 + sea steps x 0.4 / 5
	double years = 0;
	for (size_t i = 0; i + 1 < route.size(); i++) years += refSeg(*w, route[i], route[i + 1], NavalMode::Open) / 5;
	near(ticks * 0.1, std::ceil(years / 0.1 - 1e-9) * 0.1, 0.1 + 1e-9, "arrives when the reference says (" + std::to_string(years) + " years)");
	ok(years < double(route.size() - 1) / 5, "faster than walking the same number of cells");
}

DM_TEST("units: a settler arrives: unit:arrived, then civ::found with dest, people and its origin's devotion and love") {
	stub::Guard guard(true);
	if (!stub::stubbed("civ")) return;
	auto w = flat();
	const int cu = newCulture(*w);
	const int a = at(*w, 1, 0, 0), b = at(*w, 0.8, 0.3, 0.4);
	const int s0 = newTown(*w, a, cu, 200);
	w->settlements[size_t(s0)].devotion = 0.71;
	w->settlements[size_t(s0)].love = -0.23;
	Log log;
	log.attach(*w);
	struct Got { int cell = -1, culture = -1; double pop = -1; FoundedBy by = FoundedBy::Awaken; std::optional<double> dev, love; bool unitThere = false; } got;
	stub::hooks().civ_found = [&](World& ww, int cell, int culture, double pop, FoundedBy by, const civ::FoundOpts& o) -> Settlement* {
		log.seq.push_back("found");
		got = { cell, culture, pop, by, o.devotion, o.love, false };
		got.unitThere = !ww.units.empty();
		Settlement& s = state::newSettlement(ww, cell, culture);
		s.pop = pop;
		return &s;
	};
	Unit* u = units::spawn(*w, { .culture = cu, .from = s0, .dest = b, .size = 24 });
	if (!ok(u != nullptr)) return;
	const int id = u->id;
	for (int k = 0; k < 40 && unitOf(*w, id); k++) units::step(*w, 0.25);
	ok(unitOf(*w, id) == nullptr, "the unit is gone after arriving");
	eq(stub::called("civ_found"), 1, "civ::found called once");
	eq(got.cell, b); eq(got.culture, cu); eq(got.pop, 24.0); ok(got.by == FoundedBy::Settlers, "by settlers");
	ok(got.dev && *got.dev == 0.71 && got.love && *got.love == -0.23, "origin's devotion and love passed");
	ok(got.unitThere, "the unit is still in w.units during the call");
	const auto arrivedAt = std::find(log.seq.begin(), log.seq.end(), "unit:arrived");
	const auto foundAt = std::find(log.seq.begin(), log.seq.end(), "found");
	ok(arrivedAt != log.seq.end() && foundAt != log.seq.end() && arrivedAt < foundAt, "unit:arrived comes first");
	eq(log.count("unit:died"), 0, "no unit:died on a normal arrival");
	eq(int(w->settlements.size()), 2, "a new settlement exists");
}

DM_TEST("units: a settler whose site is no longer valid joins the nearest live settlement of its culture; else it is lost") {
	stub::Guard guard(true);
	if (!stub::stubbed("civ")) return;
	auto w = flat();
	const int cu = newCulture(*w), other = newCulture(*w);
	const int dest = at(*w, 0, 0, 1);
	const int s0 = newTown(*w, at(*w, 1, 0, 0), cu, 300);
	const int near1 = newTown(*w, at(*w, 0.3, 0, 1), cu, 50);           // nearest of its culture
	const int deadOne = newTown(*w, at(*w, 0.05, 0, 1), cu, 50);        // nearer but dead
	const int foreign = newTown(*w, at(*w, -0.05, 0.05, 1), other, 50); // nearer but foreign
	newTown(*w, at(*w, -1, 0, 0.2), cu, 50);                            // far
	w->settlements[size_t(deadOne)].dead = true;
	stub::hooks().civ_found = [](World&, int, int, double, FoundedBy, const civ::FoundOpts&) -> Settlement* { return nullptr; };
	Unit* u = units::spawn(*w, { .culture = cu, .from = s0, .dest = dest, .size = 33 });
	if (!ok(u != nullptr)) return;
	const int id = u->id;
	auto ev = capture(*w, [&] { for (int k = 0; k < 60 && unitOf(*w, id); k++) units::step(*w, 0.25); });
	ok(unitOf(*w, id) == nullptr, "gone");
	near(w->settlements[size_t(near1)].pop, 83, 1e-9, "joined the nearest live settlement of its culture");
	near(w->settlements[size_t(foreign)].pop, 50, 1e-9, "not the foreign one");
	near(w->settlements[size_t(deadOne)].pop, 50, 1e-9, "not the dead one");
	int died = 0;
	for (const Event& e : ev) if (e.type == Ev::UnitDied) died++;
	eq(died, 0, "no unit:died when it found a home");
	// no settlement of its culture left at all: the people are lost
	for (Settlement& s : w->settlements) if (s.culture == cu && s.id != s0) s.dead = true;
	Unit* v = units::spawn(*w, { .culture = cu, .from = s0, .dest = dest, .size = 5 });
	if (!ok(v != nullptr)) return;
	const int vid = v->id;
	w->settlements[size_t(s0)].dead = true;                         // the origin dies behind them
	auto ev2 = capture(*w, [&] { for (int k = 0; k < 60 && unitOf(*w, vid); k++) units::step(*w, 0.25); });
	std::vector<std::string> names;
	for (const Event& e : ev2) names.push_back(e.name());
	ok(names == std::vector<std::string>({ "unit:arrived", "unit:died" }), "arrived, then died");
	for (const Event& e : ev2) if (const EvUnitDied* d = e.as<EvUnitDied>()) { eq(d->cause, std::string("lost")); eq(d->cell, dest); }
}

DM_TEST("units: a settler sent to a settlement (to >= 0) of its culture joins it instead of founding") {
	stub::Guard guard(true);
	if (!stub::stubbed("civ")) return;
	auto w = flat();
	const int cu = newCulture(*w);
	const int s0 = newTown(*w, at(*w, 1, 0, 0), cu, 300);
	const int s1 = newTown(*w, at(*w, 0, 0, 1), cu, 10);
	Unit* u = units::spawn(*w, { .culture = cu, .from = s0, .to = s1, .size = 7 });
	if (!ok(u != nullptr)) return;
	const int id = u->id;
	for (int k = 0; k < 60 && unitOf(*w, id); k++) units::step(*w, 0.25);
	eq(stub::called("civ_found"), 0, "no founding");
	near(w->settlements[size_t(s1)].pop, 17, 1e-9, "joined");
}

DM_TEST("units: traders, armies and prophets go to society::onArrive while still in w.units; onArrive may kill") {
	stub::Guard guard(true);
	if (!stub::stubbed("diplomacy")) return;
	auto w = flat();
	const int cuA = newCulture(*w), cuB = newCulture(*w);
	const int s0 = newTown(*w, at(*w, 1, 0, 0), cuA), s1 = newTown(*w, at(*w, 0.6, 0.2, 0.7), cuB);
	w->vars.rel[state::relKey(cuA, cuB)].war = true;
	Log log;
	log.attach(*w);
	std::vector<int> seen;
	bool present = true;
	bool killIt = false;
	stub::hooks().society_onArrive = [&](World& ww, int unitId) {
		log.seq.push_back("onArrive");
		seen.push_back(unitId);
		present = present && state::findUnit(ww, unitId) != nullptr;
		if (killIt) units::kill(ww, unitId, "battle");
	};
	for (UnitType t : { UnitType::Trader, UnitType::Army, UnitType::Prophet }) {
		log.seq.clear();
		Unit* u = units::spawn(*w, { .type = t, .culture = cuA, .from = s0, .to = s1, .size = 12 });
		if (!ok(u != nullptr)) continue;
		const int id = u->id;
		for (int k = 0; k < 60 && unitOf(*w, id); k++) units::step(*w, 0.25);
		ok(unitOf(*w, id) == nullptr, "removed after onArrive");
		ok(!seen.empty() && seen.back() == id, "onArrive got the unit id");
		std::vector<std::string> expect = { "unit:spawned", "unit:arrived", "onArrive" };
		ok(log.seq == expect, "spawned, arrived, onArrive, and no unit:died");
	}
	ok(present, "the unit was in w.units during every onArrive");
	// a beaten army: onArrive kills it
	killIt = true;
	log.seq.clear();
	Unit* army = units::spawn(*w, { .type = UnitType::Army, .culture = cuA, .from = s0, .to = s1, .size = 12 });
	if (!ok(army != nullptr)) return;
	const int aid = army->id;
	auto ev = capture(*w, [&] { for (int k = 0; k < 60 && unitOf(*w, aid); k++) units::step(*w, 0.25); });
	int died = 0;
	for (const Event& e : ev) if (const EvUnitDied* d = e.as<EvUnitDied>()) { died++; eq(d->cause, std::string("battle")); eq(d->cell, w->settlements[size_t(s1)].cell); }
	eq(died, 1, "one unit:died (battle)");
	ok(w->units.empty(), "gone, erased once");
}

DM_TEST("units: armies and traders disband when the target dies; armies when the war is over") {
	auto w = flat();
	const int cuA = newCulture(*w), cuB = newCulture(*w);
	const int s0 = newTown(*w, at(*w, 1, 0, 0), cuA);
	const int s1 = newTown(*w, at(*w, -1, 0, 0), cuB);
	const int s2 = newTown(*w, at(*w, 0, 0, -1), cuB);
	w->vars.rel[state::relKey(cuA, cuB)].war = true;
	Unit* tr = units::spawn(*w, { .type = UnitType::Trader, .culture = cuA, .from = s0, .to = s1, .size = 5 });
	const int trId = tr ? tr->id : -1;
	Unit* ar = units::spawn(*w, { .type = UnitType::Army, .culture = cuA, .from = s0, .to = s1, .size = 50 });
	const int arId = ar ? ar->id : -1;
	Unit* ar2 = units::spawn(*w, { .type = UnitType::Army, .culture = cuA, .from = s0, .to = s2, .size = 50 });
	const int ar2Id = ar2 ? ar2->id : -1;
	Unit* pr = units::spawn(*w, { .type = UnitType::Prophet, .culture = cuA, .from = s0, .to = s1, .size = 1 });
	const int prId = pr ? pr->id : -1;
	ok(trId > 0 && arId > 0 && ar2Id > 0 && prId > 0, "four units");
	units::step(*w, 0.25);
	ok(unitOf(*w, trId) && unitOf(*w, arId) && unitOf(*w, ar2Id), "at war, target alive: they march");
	w->settlements[size_t(s1)].dead = true;
	auto ev = capture(*w, [&] { units::step(*w, 0.25); });
	ok(!unitOf(*w, trId) && !unitOf(*w, arId), "target dead: trader and army disband");
	ok(unitOf(*w, ar2Id) != nullptr, "the army against a living target marches on");
	ok(unitOf(*w, prId) != nullptr, "a prophet is not disbanded by the rule");
	int disb = 0;
	for (const Event& e : ev) if (const EvUnitDied* d = e.as<EvUnitDied>()) { if (d->cause == "disbanded") disb++; }
	eq(disb, 2, "two unit:died 'disbanded'");
	w->vars.rel[state::relKey(cuA, cuB)].war = false;
	auto ev2 = capture(*w, [&] { units::step(*w, 0.25); });
	ok(!unitOf(*w, ar2Id), "peace: the army disbands");
	ok(ev2.size() == 1 && ev2[0].as<EvUnitDied>() && ev2[0].as<EvUnitDied>()->cause == "disbanded", "unit:died disbanded");
}

DM_TEST("units: a blocked unit re-paths once (honouring naval), turns round smoothly, then dies 'lost'") {
	// 1. lava ahead: re-path around it and arrive
	{
		auto w = flat();
		const int cu = newCulture(*w);
		const int s0 = newTown(*w, at(*w, 1, 0, 0), cu);
		const int dest = at(*w, -1, 0, 0);
		Unit* u = units::spawn(*w, { .culture = cu, .from = s0, .dest = dest });
		if (!ok(u != nullptr)) return;
		const int id = u->id;
		units::step(*w, 0.25);                                      // 1.25 cells along: i = 1, t = 0.25
		const Unit* v = unitOf(*w, id);
		ok(v->i == 1 && std::fabs(v->t - 0.25) < 1e-9, "part-way along a step");
		Vec3 before;
		units::dirOf(*w, *v, before);
		const int ahead = v->path[size_t(v->i + 1)];
		w->lava[size_t(ahead)] = 0.1f;                              // impassable, not deadly
		units::step(*w, 0.01);
		v = unitOf(*w, id);
		if (!ok(v != nullptr, "re-pathed, alive")) return;
		ok(v->data.count("units_repathed") && v->data.at("units_repathed") == 1, "marked as re-pathed");
		ok(std::find(v->path.begin() + 1, v->path.end(), ahead) == v->path.end(), "the new route avoids the lava");
		Vec3 after;
		units::dirOf(*w, *v, after);
		ok(v3::angle(before, after) < 0.01, "no jump: it turned round on the spot (" + std::to_string(v3::angle(before, after)) + " rad)");
		for (int k = 0; k < 80 && unitOf(*w, id); k++) units::step(*w, 0.25);
		ok(unitOf(*w, id) == nullptr, "arrived after the detour");
	}
	// 2. blocked twice: dies 'lost'
	{
		auto w = flat();
		const int cu = newCulture(*w);
		const int s0 = newTown(*w, at(*w, 1, 0, 0), cu);
		Unit* u = units::spawn(*w, { .culture = cu, .from = s0, .dest = at(*w, -1, 0, 0) });
		const int id = u->id;
		for (int pass = 0; pass < 2; pass++) {
			const Unit* v = unitOf(*w, id);
			if (!ok(v != nullptr, "alive before block " + std::to_string(pass))) return;
			w->lava[size_t(v->path[size_t(v->i + 1)])] = 0.1f;
			auto ev = capture(*w, [&] { units::step(*w, 0.05); });
			if (pass == 0) ok(unitOf(*w, id) != nullptr, "first block: re-pathed");
			else {
				ok(unitOf(*w, id) == nullptr, "second block: dead");
				ok(ev.size() == 1 && ev[0].as<EvUnitDied>() && ev[0].as<EvUnitDied>()->cause == "lost", "unit:died 'lost'");
			}
		}
	}
	// 3. the sea rises across the route: on foot the re-path fails; canoes re-path over the shallows
	for (NavalMode m : { NavalMode::Land, NavalMode::Coastal }) {
		auto w = flat();
		const int cu = newCulture(*w);
		const int s0 = newTown(*w, at(*w, 1, 0, 0), cu);
		Unit* u = units::spawn(*w, { .culture = cu, .from = s0, .dest = at(*w, -1, 0, 0), .naval = m });
		const int id = u->id;
		units::step(*w, 0.25);
		const Unit* v = unitOf(*w, id);
		const int here = v->path[size_t(v->i)];
		for (int c = 0; c < w->n; c++)                              // a shallow band right in front of it
			if (std::fabs(w->geo->pos[size_t(3 * c)] - w->geo->pos[size_t(3 * here)] + 0.12) < 0.06) w->elev[size_t(c)] = -0.3f;
		ok(!seaC(*w, here), "it still stands on land");
		for (int k = 0; k < 120 && unitOf(*w, id); k++) units::step(*w, 0.25);
		if (m == NavalMode::Land) ok(unitOf(*w, id) == nullptr && w->units.empty(), "on foot: dies (no route over the water)");
		else ok(unitOf(*w, id) == nullptr, "canoes cross the shallows and arrive");
	}
	// 4. already more than half way into the cell: finishes the step instead
	{
		auto w = flat();
		const int cu = newCulture(*w);
		const int s0 = newTown(*w, at(*w, 1, 0, 0), cu);
		Unit* u = units::spawn(*w, { .culture = cu, .from = s0, .dest = at(*w, -1, 0, 0) });
		const int id = u->id;
		units::step(*w, 0.35);                                      // 1.75 cells
		const Unit* v = unitOf(*w, id);
		const int inside = v->path[size_t(v->i + 1)];
		eq(units::cellOf(*w, *v), inside, "its current cell is the one ahead");
		w->lava[size_t(inside)] = 0.1f;
		units::step(*w, 0.1);
		v = unitOf(*w, id);
		ok(v && !v->data.count("units_repathed"), "no re-path: it walks out of the cell");
	}
}

DM_TEST("units: lava kills (lava > 0.2 on the current cell)") {
	auto w = flat();
	const int cu = newCulture(*w);
	const int s0 = newTown(*w, at(*w, 1, 0, 0), cu);
	Unit* u = units::spawn(*w, { .culture = cu, .from = s0, .dest = at(*w, -1, 0, 0) });
	const int id = u->id;
	units::step(*w, 0.25);
	const int here = units::cellOf(*w, *unitOf(*w, id));
	w->lava[size_t(here)] = 0.15f;
	units::step(*w, 0.01);
	ok(unitOf(*w, id) != nullptr, "0.15 does not kill");
	const int here2 = units::cellOf(*w, *unitOf(*w, id));
	w->lava[size_t(here2)] = 0.3f;
	auto ev = capture(*w, [&] { units::step(*w, 0.01); });
	ok(unitOf(*w, id) == nullptr, "0.3 kills");
	ok(ev.size() == 1 && ev[0].as<EvUnitDied>() && ev[0].as<EvUnitDied>()->cause == "lava" && ev[0].as<EvUnitDied>()->cell == here2, "unit:died 'lava' at its cell");
}

DM_TEST("units: kill removes the unit, reports its last cell and the cause, once") {
	auto w = flat();
	const int cu = newCulture(*w);
	const int s0 = newTown(*w, at(*w, 1, 0, 0), cu);
	std::vector<int> ids;
	for (int k = 0; k < 3; k++) ids.push_back(units::spawn(*w, { .type = UnitType::Prophet, .culture = cu, .from = s0, .dest = at(*w, -1, 0, 0) })->id);
	units::step(*w, 0.3);
	const int cell = units::cellOf(*w, *unitOf(*w, ids[1]));
	auto ev = capture(*w, [&] { ok(units::kill(*w, ids[1], "meteor"), "killed"); });
	ok(ev.size() == 1, "one event");
	if (const EvUnitDied* d = ev[0].as<EvUnitDied>()) {
		eq(d->id, ids[1]); ok(d->type == UnitType::Prophet); eq(d->culture, cu); eq(d->cell, cell, "last cell"); eq(d->cause, std::string("meteor"));
	}
	ok(!units::kill(*w, ids[1], "meteor"), "a second kill does nothing");
	ok(!units::kill(*w, 999, "x"), "unknown id");
	eq(int(w->units.size()), 2); ok(w->units[0].id == ids[0] && w->units[1].id == ids[2], "the others keep their order");
	auto ev2 = capture(*w, [&] { units::kill(*w, ids[0], ""); });
	ok(ev2.size() == 1 && ev2[0].as<EvUnitDied>()->cause == "lost", "an empty cause reads 'lost'");
}

DM_TEST("units: dirOf is unit length, on the cell at t = 0, continuous along a whole journey; cellOf the nearer end") {
	auto w = makeWorld({ .L = 4, .seed = 3 });
	const int cu = newCulture(*w);
	const int a = findLand(*w), b = findLand(*w, [&](int c) { return w->cache.landId[size_t(c)] == w->cache.landId[size_t(a)] && w->geo->angle(a, c) > 0.6; });
	const int s0 = newTown(*w, a, cu);
	Unit* u = units::spawn(*w, { .culture = cu, .from = s0, .dest = b, .naval = NavalMode::Open, .maxCost = 1e9 });
	if (!ok(u != nullptr)) return;
	const int id = u->id;
	Vec3 prev;
	eq(units::dirOf(*w, *u, prev), a, "returns the current cell");
	near(v3::angle(prev, w->geo->dir(a)), 0, 1e-9, "at t = 0 on the cell");
	double maxJump = 0, maxLenErr = 0;
	int steps = 0, cellErr = 0;
	while (unitOf(*w, id) && steps < 20000) {
		units::step(*w, 0.01);
		steps++;
		const Unit* v = unitOf(*w, id);
		if (!v) break;
		Vec3 d;
		const int c = units::dirOf(*w, *v, d);
		maxLenErr = std::max(maxLenErr, std::fabs(v3::len(d) - 1));
		maxJump = std::max(maxJump, v3::angle(prev, d));
		const int expect = v->t >= 0.5 ? v->path[size_t(v->i + 1)] : v->path[size_t(v->i)];
		if (c != expect || units::cellOf(*w, *v) != expect) cellErr++;
		prev = d;
	}
	ok(unitOf(*w, id) == nullptr, "arrived");
	ok(maxLenErr < 1e-9, "unit length");
	ok(maxJump < 0.03, "continuous: largest move per 0.01 yr " + std::to_string(maxJump) + " rad over " + std::to_string(steps) + " steps");
	eq(cellErr, 0, "current cell = nearer end");
	Unit bad;
	bad.path = { -5, 7 };
	Vec3 d;
	ok(units::dirOf(*w, bad, d) == 7 && std::fabs(v3::len(d) - 1) < 1e-9, "bad cells skipped");
	Unit none;
	ok(units::dirOf(*w, none, d) == -1 && d.y == 1, "no path, no dest: -1 and (0,1,0)");
	none.dest = 5;
	eq(units::cellOf(*w, none), 5, "an empty path reads its dest");
}

DM_TEST("units: near lists units whose current cell is within the angle, ascending id") {
	auto w = flat();
	const int cu = newCulture(*w);
	const int a = at(*w, 1, 0, 0);
	const int s0 = newTown(*w, a, cu);
	std::vector<int> ids;
	for (int k = 0; k < 5; k++) ids.push_back(units::spawn(*w, { .type = UnitType::Prophet, .culture = cu, .from = s0, .dest = at(*w, -1, 0, 0) })->id);
	for (int k = 0; k < 3; k++) units::step(*w, 0.1);                 // all at the same place
	const int c = units::cellOf(*w, *unitOf(*w, ids[0]));
	ok(units::near(*w, c, 0) == ids, "all five at that cell, ascending");
	ok(units::near(*w, at(*w, -1, 0, 0), 0.3).empty(), "none far away");
	ok(units::near(*w, -1, 1).empty() && units::near(*w, c, std::nan("")).empty() && units::near(*w, c, -1).empty(), "bad arguments");
	eq(int(units::near(*w, c, kInfD).size()), 5, "infinite angle: all");
}

DM_TEST("units: step tolerates bad dt and damaged units; units spawned during a step move from the next") {
	stub::Guard guard(true);
	auto w = flat();
	const int cu = newCulture(*w);
	const int s0 = newTown(*w, at(*w, 1, 0, 0), cu);
	const int id = units::spawn(*w, { .culture = cu, .from = s0, .dest = at(*w, -1, 0, 0) })->id;
	const uint32_t h0 = hash(*w);
	for (double dt : { 0.0, -1.0, std::nan(""), kInfD }) units::step(*w, dt);
	eq(hash(*w), h0, "bad dt: nothing moves");
	// damaged units
	Unit& d1 = state::newUnit(*w, UnitType::Settler);
	d1.path.clear();                                                // empty path
	Unit& d2 = state::newUnit(*w, UnitType::Trader);
	d2.path = { 3, 99999 };                                         // a bad cell
	Unit& d3 = state::newUnit(*w, UnitType::Army);
	d3.path = { 3, w->geo->nbr[18] };
	d3.i = -4; d3.t = std::nan(""); d3.speed = std::nan(""); d3.to = 77;   // nonsense numbers, a missing target
	const int d3id = d3.id;
	auto ev = capture(*w, [&] { units::step(*w, 0.25); });
	ok(unitOf(*w, id) != nullptr, "the healthy unit lives");
	ok(w->units.size() == 1, "damaged units were removed");
	int died = 0;
	for (const Event& e : ev) if (e.type == Ev::UnitDied) died++;
	eq(died, 3, "each with unit:died");
	(void)d3id;
	noNaN(*w, "no NaN after damaged units");
	// a unit spawned by an arrival handler does not move in that step
	if (stub::stubbed("diplomacy")) {
		const int cu2 = newCulture(*w);
		const int s1 = newTown(*w, w->geo->nbr[size_t(6 * w->settlements[size_t(s0)].cell)], cu2);
		int spawnedId = -1;
		stub::hooks().society_onArrive = [&](World& ww, int) {
			Unit* nu = units::spawn(ww, { .type = UnitType::Prophet, .culture = cu, .from = s0, .dest = at(ww, -1, 0, 0) });
			if (nu) spawnedId = nu->id;
		};
		const int tid = units::spawn(*w, { .type = UnitType::Trader, .culture = cu, .from = s0, .to = s1 })->id;
		units::step(*w, 0.25);
		ok(!unitOf(*w, tid), "the trader arrived");
		const Unit* nu = unitOf(*w, spawnedId);
		ok(nu && nu->i == 0 && nu->t == 0, "the unit spawned on arrival has not moved yet");
	}
}

DM_TEST("units: deterministic, and a saved journey continues exactly") {
	auto run = [](bool saveHalfway) {
		auto w = flat();
		const int cu = newCulture(*w);
		const int s0 = newTown(*w, at(*w, 1, 0, 0), cu);
		for (int k = 0; k < 6; k++) units::spawn(*w, { .type = UnitType(k % 4 == 2 ? 0 : k % 4), .culture = cu, .from = s0, .dest = at(*w, -1, 0.2 * k - 0.5, 0.3), .size = 3 });
		w->units[1].data["units_repathed"] = 0;
		for (int t = 0; t < 10; t++) units::step(*w, 0.25);
		if (saveHalfway) {
			std::vector<uint8_t> bytes;
			std::string why;
			if (!state::serialize(*w, bytes, &why)) { fail("serialize: " + why); return std::vector<double>(); }
			auto back = state::deserialize(bytes, &why);
			if (!back) { fail("deserialize: " + why); return std::vector<double>(); }
			w = std::move(back);
		}
		for (int t = 0; t < 10; t++) units::step(*w, 0.25);
		std::vector<double> out;
		for (const Unit& u : w->units) { out.push_back(u.id); out.push_back(progress(u)); out.push_back(u.sea); out.push_back(double(u.path.size())); }
		return out;
	};
	const auto a = run(false), b = run(false), c = run(true);
	ok(!a.empty() && a == b, "two runs agree");
	ok(a == c, "a save / load half way changes nothing on level land");
}

DM_TEST("units: performance: 300 units on an L 6 planet") {
	auto w = makeWorld({ .L = 6, .seed = 4 });
	const int cu = newCulture(*w);
	std::vector<int> towns;
	Rng r(8);
	for (int k = 0; k < 30; k++) {
		const int c = findLand(*w, [&](int x) { return w->cache.landId[size_t(x)] == 0 && hashf(uint32_t(x), uint32_t(k)) < 0.002; });
		if (c >= 0) towns.push_back(newTown(*w, c, cu));
	}
	const double t0 = nowMs();
	int made = 0;
	for (int k = 0; made < 300 && k < 2000; k++) {
		const int from = towns[size_t(r.integer(0, int(towns.size()) - 1))];
		const int to = towns[size_t(r.integer(0, int(towns.size()) - 1))];
		if (from == to) continue;
		if (units::spawn(*w, { .type = UnitType::Prophet, .culture = cu, .from = from, .to = to, .naval = NavalMode::Coastal, .maxCost = 1e9 })) made++;
	}
	const double tSpawn = nowMs() - t0;
	eq(made, 300, "300 units");
	double worst = 0;
	const double t1 = nowMs();
	for (int k = 0; k < 40; k++) { const double s = nowMs(); units::step(*w, 0.25); worst = std::max(worst, nowMs() - s); }
	const double tStep = (nowMs() - t1) / 40;
	std::printf("    300 units at L6: spawn (A* each) %.2f ms total, step mean %.3f ms, max %.3f ms, %zu still travelling\n", tSpawn, tStep, worst, w->units.size());
	ok(tStep < 2.5, "a step costs well under a tick's budget");
	noNaN(*w);
}

DM_TEST("units: map of journeys (shots/ue-path-units-journeys.png)") {
	stub::Guard guard(true);
	auto w = makeWorld({ .L = 6, .seed = 3 });
	const int cu = newCulture(*w);
	std::vector<uint8_t> trail(size_t(w->n), 0);
	std::vector<int> starts, ids;
	Rng r(12);
	std::vector<int> land0;
	for (int c = 0; c < w->n; c++) if (w->cache.landId[size_t(c)] >= 0 && w->cache.landId[size_t(c)] <= 2) land0.push_back(c);
	for (int k = 0; k < 18; k++) {
		const int a = land0[size_t(r.integer(0, int(land0.size()) - 1))];
		const int b = land0[size_t(r.integer(0, int(land0.size()) - 1))];
		const int s = newTown(*w, a, cu);
		const NavalMode m = k % 3 == 0 ? NavalMode::Land : k % 3 == 1 ? NavalMode::Coastal : NavalMode::Open;
		Unit* u = units::spawn(*w, { .type = UnitType::Prophet, .culture = cu, .from = s, .dest = b, .naval = m, .maxCost = 1e9 });
		if (u) { ids.push_back(u->id); starts.push_back(a); }
	}
	int seaTicks = 0, landTicks = 0;
	for (int t = 0; t < 2000 && !w->units.empty(); t++) {
		units::step(*w, 0.25);
		for (const Unit& u : w->units) {
			const int c = units::cellOf(*w, u);
			trail[size_t(c)] = u.sea ? 2 : 1;
			(u.sea ? seaTicks : landTicks)++;
		}
	}
	namespace mi = dm::mapimg;
	ok(mi::custom(*w, shot("ue-path-units-journeys.png"), [&](int c) {
		if (trail[size_t(c)] == 1) return mi::Rgb{ 255, 60, 30 };  // walking
		if (trail[size_t(c)] == 2) return mi::Rgb{ 255, 255, 255 }; // ship
		const double e = w->elev[size_t(c)];
		if (e < w->seaLevel) return mi::Rgb{ 20, 70, 140 };
		return mi::Rgb{ 80 + 100 * std::min(1.0, e / 6), 130, 70 };
	}, { .rivers = false, .marks = {} }), "journeys map written");
	std::printf("    journeys: %zu units, %d walking and %d sailing unit-ticks, %zu left after 500 years\n", ids.size(), landTicks, seaTicks, w->units.size());
	ok(seaTicks > 0 && landTicks > 0, "both walking and sailing happened");
	ok(w->units.empty(), "every journey ended");
}
