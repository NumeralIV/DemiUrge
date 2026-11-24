// DEMIURGE — SimTests/test_civ_v4.cpp : the v4 mythos half of Civ's lifecycle (SPEC-MYTHOS 1.4, 3.9, 3.16, 3.27;
// track M6 economy-world-v4): civ::damage x harmTaken (the culture's Factors and the town's TownFactors) and
// civ::exodus. Runs with every other module stubbed (`run.ps1 civ`). Where Units must act, a recording
// units::spawn hook is installed while Units is a stub; in `run.ps1 all` the real Units / Path run and the
// tests check the contract either way (people leave only with a unit). TownFactors come from a hook while
// Mythos is a stub (the stub answers neutral), so those checks run only then.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "Civ.h"
#include "Mythos.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "Units.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

struct World3 {
	std::unique_ptr<World> w;
	int cu = -1, sid = -1;
};

World3 town(double pop = 400) {
	World3 r;
	r.w = makeWorld({ .L = 5, .seed = 4, .land = 0.45, .basics = true, .sea = 0, .phase = Phase::History });
	World& w = *r.w;
	Culture& cu = state::newCulture(w);
	cu.name = "Folk";
	cu.age = Age::BRONZE;
	r.cu = cu.id;
	const int c = bestHab(w);
	Settlement* s = civ::found(w, c, r.cu, pop, FoundedBy::Awaken);
	r.sid = s ? s->id : -1;
	return r;
}

std::vector<units::SpawnOpts>& spawned() { static std::vector<units::SpawnOpts> v; return v; }

// a recording units::spawn: a unit record, nothing else (only while Units is a stub)
bool hookSpawn(bool succeed = true) {
	if (!stub::stubbed("units")) return false;
	spawned().clear();
	stub::hooks().units_spawn = [succeed](World& w, const units::SpawnOpts& o) -> Unit* {
		spawned().push_back(o);
		if (!succeed) return nullptr;
		Unit& u = state::newUnit(w, o.type);
		u.culture = o.culture; u.from = o.from; u.to = o.to; u.dest = o.dest; u.size = o.size; u.naval = o.naval; u.data = o.data;
		return &u;
	};
	return true;
}

} // namespace

DM_TEST("civ v4: damage is v3.1 to the bit with neutral (stored or absent) Factors") {
	World3 a = town(), b = town();
	if (!ok(a.sid >= 0 && b.sid >= 0, "towns")) return;
	b.w->tune<MythosTune>().phase = 1; b.w->mythos.factors.assign(b.w->cultures.size(), Factors{});
	const double ka = civ::damage(*a.w, a.sid, 0.37, Cause::Quake), kb = civ::damage(*b.w, b.sid, 0.37, Cause::Quake);
	ok(std::memcmp(&ka, &kb, sizeof ka) == 0, "the same number killed");
	const double pa = a.w->settlements[size_t(a.sid)].pop, pb = b.w->settlements[size_t(b.sid)].pop;
	ok(std::memcmp(&pa, &pb, sizeof pa) == 0, "the same people left");
	near(ka, 400 * 0.37, 1e-9, "the v3.1 rule");
}

DM_TEST("civ v4: damage x the culture's harmTaken x the town's harmTaken (Endurance, preparation)") {
	stub::Guard guard;
	World3 r = town(400);
	World& w = *r.w;
	if (!ok(r.sid >= 0, "town")) return;
	Factors f{};
	f.harmTaken = 0.5;
	w.tune<MythosTune>().phase = 1; w.mythos.factors.assign(w.cultures.size(), f);
	near(civ::damage(w, r.sid, 0.4, Cause::War), 400 * 0.2, 1e-9, "the culture's half");
	near(w.settlements[size_t(r.sid)].pop, 320, 1e-9, "320 left");
	if (stub::stubbed("mythos")) {
		const int sid = r.sid;
		stub::hooks().mythos_townFactors = [sid](const World&, int s) { TownFactors t{}; if (s == sid) t.harmTaken = 0.5; return t; };
		near(civ::damage(w, r.sid, 0.4, Cause::Meteor), 320 * 0.1, 1e-9, "x the town's half: a quarter of the share");
		// a factor above 1 is allowed; the share stays at most 1 (the town is destroyed)
		f.harmTaken = 10;
		w.tune<MythosTune>().phase = 1; w.mythos.factors.assign(w.cultures.size(), f);
		stub::hooks().mythos_townFactors = nullptr;
		const double before = w.settlements[size_t(r.sid)].pop;
		near(civ::damage(w, r.sid, 0.5, Cause::Quake), before, 1e-9, "x 10 -> all of them");
		ok(w.settlements[size_t(r.sid)].dead, "destroyed");
	}
	// non-finite / negative factors are neutral
	World3 q = town(400);
	Factors bad{};
	bad.harmTaken = std::nan("");
	q.w->tune<MythosTune>().phase = 1; q.w->mythos.factors.assign(q.w->cultures.size(), bad);
	near(civ::damage(*q.w, q.sid, 0.25, Cause::Wave), 100, 1e-9, "NaN harmTaken = 1");
	bad.harmTaken = -3;
	q.w->tune<MythosTune>().phase = 1; q.w->mythos.factors.assign(q.w->cultures.size(), bad);
	near(civ::damage(*q.w, q.sid, 0.25, Cause::Wave), 75, 1e-9, "negative harmTaken = 1");
	// a zero factor spares them entirely
	bad.harmTaken = 0;
	q.w->tune<MythosTune>().phase = 1; q.w->mythos.factors.assign(q.w->cultures.size(), bad);
	eq(civ::damage(*q.w, q.sid, 0.9, Cause::Wave), 0.0, "harmTaken 0: nobody dies");
}

DM_TEST("civ v4: exodus refuses bad input and changes nothing") {
	stub::Guard guard(true);
	const bool hooked = hookSpawn();
	World3 r = town(400);
	World& w = *r.w;
	if (!ok(r.sid >= 0, "town")) return;
	const uint32_t h0 = hash(w);
	eq(civ::exodus(w, -1, 0.3), -1, "bad id");
	eq(civ::exodus(w, 999, 0.3), -1, "missing id");
	eq(civ::exodus(w, r.sid, 0), -1, "share 0");
	eq(civ::exodus(w, r.sid, -0.5), -1, "negative share");
	eq(civ::exodus(w, r.sid, std::nan("")), -1, "NaN share");
	w.settlements[size_t(r.sid)].pop = 3.5;                                // fewer than minPeople + 1 can leave
	eq(civ::exodus(w, r.sid, 0.5), -1, "too few people");
	w.settlements[size_t(r.sid)].pop = 400;
	eq(hash(w), h0, "nothing changed");
	if (hooked) eq(int(spawned().size()), 0, "no unit was asked for");
	civ::destroy(w, r.sid, Cause::War);
	eq(civ::exodus(w, r.sid, 0.3), -1, "a dead town");
}

DM_TEST("civ v4: exodus sends share x pop as settlers to the best free site within 12 hops; people leave with the unit") {
	stub::Guard guard(true);
	const bool hooked = hookSpawn();
	World3 r = town(400);
	World& w = *r.w;
	if (!ok(r.sid >= 0, "town")) return;
	const int from = w.settlements[size_t(r.sid)].cell;
	const int uid = civ::exodus(w, r.sid, 0.3);
	const double left = w.settlements[size_t(r.sid)].pop;
	if (uid < 0) {                                                         // (real Units: no route) nothing may change
		near(left, 400, 1e-9, "no unit, nobody left");
		return;
	}
	const Unit* u = state::findUnit(w, uid);
	if (!ok(u != nullptr, "the unit exists")) return;
	near(u->size, 120, 1e-9, "a party of 0.3 x 400");
	near(left, 280, 1e-9, "the town keeps 280");
	eq(u->type == UnitType::Settler, true, "a settler party");
	eq(u->culture, r.cu, "of their people");
	ok(u->data.count("civ_exodus") == 1, "marked as an exodus");
	if (hooked) {
		if (!eq(int(spawned().size()), 1, "one spawn")) return;
		const units::SpawnOpts& o = spawned()[0];
		eq(o.to, -1, "to a new site");
		eq(o.from, r.sid, "from the town");
		ok(civ::canSettle(w, o.dest).ok, "the site is free");
		std::vector<int32_t> ring;
		w.geo->ring(from, 12, ring);
		ok(std::find(ring.begin(), ring.end(), o.dest) != ring.end(), "within 12 hops");
		double best = -1e300;
		int bestC = -1;
		for (const int32_t c : ring) {
			if (!civ::canSettle(w, c).ok) continue;
			const double sc = civ::siteScore(w, c);
			if (sc > best || (sc == best && c < bestC)) { best = sc; bestC = c; }
		}
		eq(o.dest, bestC, "the best siteScore (ties: the lower cell)");
		eq(o.naval == NavalMode::Land || o.naval == NavalMode::Coastal, true, "Bronze: by land or along the coast");
	}
	// share 1 leaves minPeople behind
	World3 q = town(50);
	if (hooked) spawned().clear();
	const int u2 = civ::exodus(*q.w, q.sid, 1.0);
	if (u2 >= 0) near(q.w->settlements[size_t(q.sid)].pop, q.w->tune<CivTune>().minPeople, 1e-9, "share 1: minPeople stay");
	noNaN(w, "finite");
}

DM_TEST("civ v4: exodus with no free site joins the nearest town of the same people; a failed spawn changes nothing") {
	stub::Guard guard(true);
	if (!hookSpawn()) return;                                              // needs the recording spawn
	World3 r = town(400);
	World& w = *r.w;
	if (!ok(r.sid >= 0, "town")) return;
	const int from = w.settlements[size_t(r.sid)].cell;
	// a sister town of the same people 9-11 hops away, and a nearer town of another people
	std::vector<int32_t> ring9, ring8, ring12;
	w.geo->ring(from, 11, ring9);
	w.geo->ring(from, 8, ring8);
	int sister = -1;
	for (const int32_t c : ring9) {
		if (std::find(ring8.begin(), ring8.end(), c) != ring8.end()) continue;
		if (!civ::canSettle(w, c).ok) continue;
		Settlement* s = civ::found(w, c, r.cu, 100, FoundedBy::Settlers);
		if (s) { sister = s->id; break; }
	}
	if (!ok(sister >= 0, "a sister town")) return;
	// nothing else is settleable within 12 hops
	w.geo->ring(from, 12, ring12);
	for (const int32_t c : ring12) if (w.owner[size_t(c)] < 0) w.hab[size_t(c)] = 0;
	const int uid = civ::exodus(w, r.sid, 0.25);
	if (!ok(uid >= 0, "a party left")) return;
	if (!eq(int(spawned().size()), 1, "one spawn")) return;
	eq(spawned()[0].to, sister, "to the sister town");
	eq(spawned()[0].dest, w.settlements[size_t(sister)].cell, "at its centre");
	near(w.settlements[size_t(r.sid)].pop, 300, 1e-9, "100 left");
	// a spawn that fails: nothing changes
	hookSpawn(false);
	const uint32_t h0 = hash(w);
	eq(civ::exodus(w, r.sid, 0.25), -1, "no unit");
	eq(hash(w), h0, "nothing changed");
	// no other town at all: -1
	civ::destroy(w, sister, Cause::War);
	for (const int32_t c : ring12) if (w.owner[size_t(c)] < 0) w.hab[size_t(c)] = 0;   // (its released land too)
	hookSpawn(true);
	eq(civ::exodus(w, r.sid, 0.25), -1, "nowhere to go");
	eq(int(spawned().size()), 0, "and no unit asked for");
}
