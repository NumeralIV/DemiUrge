// DEMIURGE — SimTests/test_diplomacy_omen.cpp : track chronicle-diplomacy (sim-findings-2 "open for the lead"):
// war:declared.envied tells "the god favoured them" from "the god struck us", society::swornUntil names the
// Omen's oaths for the chronicle, and DiplomacyTune::omenNoEnvy keeps a sworn pair from envying each other.
// Runs with every other module stubbed (run.ps1 diplomacy) and with the real ones (run.ps1 all); the TUNE
// defaults keep the new rules off, so each test switches on the rule it checks.
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Society.h"
#include "State.h"
#include "TestFramework.h"
#include "TestWorld.h"

using namespace dm;
using namespace dmt;

namespace {

bool seaCell(const World& w, int c) { return double(w.elev[size_t(c)]) < w.seaLevel; }

std::vector<int> hopsFrom(const World& w, int from) {
	std::vector<int> d(size_t(w.n), -1);
	std::vector<int> q{ from };
	d[size_t(from)] = 0;
	for (size_t h = 0; h < q.size(); h++) {
		const int c = q[h];
		for (int k = 0; k < w.geo->nbrN[size_t(c)]; k++) {
			const int m = w.geo->nbr[size_t(6 * c + k)];
			if (m >= 0 && d[size_t(m)] < 0) { d[size_t(m)] = d[size_t(c)] + 1; q.push_back(m); }
		}
	}
	return d;
}

// a habitable land cell k hops from `from` on the same landmass whose ring of 2 hops is all land
int siteNear(const World& w, int from, int k) {
	const std::vector<int> d = hopsFrom(w, from);
	const bool ids = w.cache.landId.size() == size_t(w.n);
	std::vector<int32_t> tmp;
	for (int c = 0; c < w.n; c++) {
		if (d[size_t(c)] != k || seaCell(w, c) || w.hab[size_t(c)] < 0.1f) continue;
		if (ids && w.cache.landId[size_t(c)] != w.cache.landId[size_t(from)]) continue;
		w.geo->ring(c, 2, tmp);
		bool land = true;
		for (const int m : tmp) if (seaCell(w, m)) { land = false; break; }
		if (land) return c;
	}
	return -1;
}

int culture(World& w) {
	Culture& c = state::newCulture(w);
	c.traits.aggression = 0.5;
	c.name = "C" + std::to_string(c.id);
	return c.id;
}

int town(World& w, int cell, int cu, double pop) {
	Settlement& s = state::newSettlement(w, cell, cu);
	s.pop = pop;
	s.radius = 2;
	const int id = s.id;
	std::vector<int32_t> tmp;
	w.geo->ring(cell, 2, tmp);
	for (const int c : tmp) {
		if (seaCell(w, c) || w.owner[size_t(c)] >= 0) continue;
		w.owner[size_t(c)] = int16_t(id);
		w.settlements[size_t(id)].cells.push_back(c);
	}
	return id;
}

struct Two {
	std::unique_ptr<World> w;
	int a = -1, b = -1;
	bool ok = false;
};
Two two() {
	Two t;
	t.w = makeWorld({ .L = 6, .seed = 3, .land = 0.45 });
	World& w = *t.w;
	w.tune<DiplomacyTune>().noise = 0;
	const int c0 = bestHab(w);
	const int c1 = siteNear(w, c0, 7);
	if (c1 < 0) return t;
	t.a = culture(w);
	t.b = culture(w);
	town(w, c0, t.a, 1000);
	town(w, c1, t.b, 1000);
	t.ok = true;
	return t;
}

void setFavour(World& w, const char* what, int cid, double rate) {
	const double hl = w.tune<SocietyTune>().favourHalfLife;
	w.vars.num[std::string("society_") + what + "_" + std::to_string(cid)] = rate / (1 - std::pow(0.5, 1.0 / hl));
}

// the war:declared of a declareWar(a, b) on a fresh pair with the given favour
EvWarDeclared declared(const std::function<void(Two&)>& setup) {
	Two t = two();
	EvWarDeclared out;
	out.a = -99;
	if (!t.ok) return out;
	setup(t);
	const auto evs = capture(*t.w, [&] { society::declareWar(*t.w, t.a, t.b); });
	for (const Event& e : evs) if (const EvWarDeclared* d = e.as<EvWarDeclared>()) out = *d;
	return out;
}

} // namespace

DM_TEST("diplomacy (chronicle-diplomacy): war:declared.envied tells the god's favour of the enemy from its anger") {
	const EvWarDeclared none = declared([](Two&) {});
	if (!ok(none.a != -99, "two peoples placed")) return;
	ok(!none.blamed && !none.envied, "no deed of the god: neither blamed nor envied");
	const EvWarDeclared struck = declared([](Two& t) { setFavour(*t.w, "fwrath", t.a, 0.01); });
	ok(struck.blamed && !struck.envied, "the god struck A lately: blamed, not envy (\"the god struck us\")");
	const EvWarDeclared favoured = declared([](Two& t) { setFavour(*t.w, "fgrace", t.b, 0.01); });
	ok(favoured.blamed && favoured.envied, "the god blessed B over A: envy (\"the god favoured them\")");
	// both: the larger reason names it
	const EvWarDeclared moreWrath = declared([](Two& t) { setFavour(*t.w, "fwrath", t.a, 0.02); setFavour(*t.w, "fgrace", t.b, 0.006); });
	ok(moreWrath.blamed && !moreWrath.envied, "more anger than envy: struck");
	const EvWarDeclared moreEnvy = declared([](Two& t) { setFavour(*t.w, "fwrath", t.a, 0.005); setFavour(*t.w, "fgrace", t.b, 0.02); });
	ok(moreEnvy.blamed && moreEnvy.envied, "more envy than anger: envied");
	// A blessed as much as B: nothing to envy
	const EvWarDeclared even = declared([](Two& t) { setFavour(*t.w, "fgrace", t.a, 0.01); setFavour(*t.w, "fgrace", t.b, 0.01); });
	ok(!even.blamed && !even.envied, "both blessed alike: no blame");
	// the payload contract
	eq(kEventTable[int(Ev::WarDeclared)].nKeys, 5, "war:declared has five keys");
	eq(std::string(kEventTable[int(Ev::WarDeclared)].keys[4]), std::string("envied"), "... the fifth is envied");
}

DM_TEST("diplomacy (chronicle-diplomacy): swornUntil names the Omen's oaths; a sworn pair does not envy each other (omenNoEnvy)") {
	Two t = two();
	if (!ok(t.ok, "placed")) return;
	World& w = *t.w;
	DiplomacyTune& T = w.tune<DiplomacyTune>();
	ok(!T.omenNoEnvy, "omenNoEnvy off by default");
	near(society::swornUntil(w, t.a, t.b), -1, 0, "not sworn");
	near(society::swornUntil(w, t.a, t.a) + society::swornUntil(w, -1, t.b) + society::swornUntil(w, t.a, 999), -3, 0, "bad pairs: -1");
	T.omenSworn = true;
	w.year = 100;
	ok(society::swearPeace(w, t.a, t.b), "sworn");
	near(society::swornUntil(w, t.a, t.b), 100 + T.omenYears, 1e-12, "until year + omenYears");
	near(society::swornUntil(w, t.b, t.a), 100 + T.omenYears, 1e-12, "either order");
	// v after one diplomacy() of the sworn pair, A the god's favourite, with or without the envy rule
	auto vAfter = [](bool envy, bool noEnvy, bool sworn) {
		Two u = two();
		if (!u.ok) return 99.0;
		World& x = *u.w;
		DiplomacyTune& D = x.tune<DiplomacyTune>();
		D.omenSworn = true;
		D.omenNoEnvy = noEnvy;
		D.envyK = envy ? 0.004 : 0;
		D.favourRef = 0.01;
		x.year = 100;
		society::rel(x, u.a, u.b)->v = 0.0;
		if (sworn) society::swearPeace(x, u.a, u.b);
		setFavour(x, "fgrace", u.a, 0.02);
		society::diplomacy(x);
		return society::rel(x, u.a, u.b)->v;
	};
	const double swornEnvy = vAfter(true, false, true), swornNoEnvy = vAfter(true, true, true), swornBase = vAfter(false, true, true);
	if (!ok(swornEnvy < 50, "placed again")) return;
	near(swornEnvy - swornBase, -0.004, 1e-9, "sworn, the rule off: the favourite is still envied (-envyK)");
	near(swornNoEnvy - swornBase, 0, 1e-12, "sworn, omenNoEnvy: no envy between them");
	near(vAfter(true, true, false) - vAfter(false, true, false), -0.004, 1e-9, "not sworn: envy as before");
	w.year = 100 + T.omenYears + 1;
	near(society::swornUntil(w, t.a, t.b), -1, 0, "the oath has ended");
}

// (chronicle-diplomacy verify) the Omen's oath is saved state: a world saved under a sworn peace loads with the
// same oath (swornUntil), keeps the envy exemption while it lasts and lets it end in the same year as the original
DM_TEST("diplomacy (chronicle-diplomacy verify): a sworn peace survives save / load and ends in the same year") {
	Two t = two();
	if (!ok(t.ok, "placed")) return;
	World& w = *t.w;
	auto tuneUp = [](World& x) {
		DiplomacyTune& D = x.tune<DiplomacyTune>();
		D.noise = 0;
		D.omenSworn = true;
		D.omenNoEnvy = true;
		D.envyK = 0.004;
		D.favourRef = 0.01;
	};
	tuneUp(w);
	w.year = 100;
	society::rel(w, t.a, t.b)->v = 0.0;
	if (!ok(society::swearPeace(w, t.a, t.b), "sworn")) return;
	setFavour(w, "fgrace", t.a, 0.02);   // A is the god's favourite: B would envy it, but for the oath
	society::diplomacy(w);
	std::vector<uint8_t> bytes;
	if (!ok(state::serialize(w, bytes), "save under the oath")) return;
	auto l = state::deserialize(bytes);
	if (!ok(l != nullptr, "load")) return;
	tuneUp(*l);                                        // (tuning is never saved: Balance re-applies it)
	near(society::swornUntil(*l, t.a, t.b), society::swornUntil(w, t.a, t.b), 0, "the loaded world keeps the oath's year");
	near(society::rel(*l, t.a, t.b)->v, society::rel(w, t.a, t.b)->v, 1e-6, "and the relation");
	const double until = society::swornUntil(w, t.a, t.b);
	double endW = -1, endL = -1, worst = 0;
	for (double y = 101; y <= until + 3; y += 1) {
		w.year = y;
		l->year = y;
		society::diplomacy(w);
		society::diplomacy(*l);
		const Relation* rw = society::peek(w, t.a, t.b);
		const Relation* rl = society::peek(*l, t.a, t.b);
		if (!ok(rw && rl, "relations kept")) return;
		worst = std::max(worst, std::fabs(rw->v - rl->v));
		if (endW < 0 && society::swornUntil(w, t.a, t.b) < 0) endW = y;
		if (endL < 0 && society::swornUntil(*l, t.a, t.b) < 0) endL = y;
	}
	near(worst, 0, 1e-6, "original and loaded relations move together year by year");
	ok(endW > 0, "the oath ends");
	near(endL, endW, 0, "... in the same year in the loaded world");
	ok(!l->vars.num.count("diplomacy_sworn" + std::to_string(std::min(t.a, t.b)) + "_" + std::to_string(std::max(t.a, t.b))),
		"the ended oath's key is gone from the loaded world");
}
