// DEMIURGE — SimTests/test_diplomacy_findings.cpp : the v3.1 relations rules of design review findings 11
// and 13 (track sim-findings-2; SPEC-SIM 3.8 "(v3.1, design review 11/13)"): the god in the relations
// (envy, shared wrath, plague blame, prophets' zeal and kinship), alliances (form, call to arms, deterrence,
// break), the Omen's sworn peace and society::tense, blame and thanks, feuds, weariness, stalled wars and
// the feared bomb. Runs with every other module stubbed (no collaborator is needed: only relations, the
// culture geometry and vars.num); the TUNE defaults keep every v3.1 rule off, so each test switches on
// the rule it checks.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Core/Rng.h"
#include "Society.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

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

// a habitable land cell k hops from `from`, on the same landmass, whose ring of 2 hops is all land; with
// `away` >= 0 the one farthest (in hops) from `away`
int siteNear(const World& w, int from, int k, int away = -1) {
	const std::vector<int> d = hopsFrom(w, from);
	const std::vector<int> da = away >= 0 ? hopsFrom(w, away) : std::vector<int>();
	const bool ids = w.cache.landId.size() == size_t(w.n);
	std::vector<int32_t> tmp;
	int best = -1;
	for (int c = 0; c < w.n; c++) {
		if (d[size_t(c)] != k || seaCell(w, c) || w.hab[size_t(c)] < 0.1f) continue;
		if (ids && w.cache.landId[size_t(c)] != w.cache.landId[size_t(from)]) continue;
		w.geo->ring(c, 2, tmp);
		bool land = true;
		for (const int m : tmp) if (seaCell(w, m)) { land = false; break; }
		if (!land) continue;
		if (away < 0) return c;
		if (best < 0 || da[size_t(c)] > da[size_t(best)]) best = c;
	}
	return best;
}

int culture(World& w, double aggression = 0.5) {
	Culture& c = state::newCulture(w);
	c.traits.aggression = aggression;
	c.name = "C" + std::to_string(c.id);
	return c.id;
}

int town(World& w, int cell, int cu, double pop) {
	Settlement& s = state::newSettlement(w, cell, cu);
	s.pop = pop;
	s.radius = 2;
	s.name = "T" + std::to_string(s.id);
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

int count(const std::vector<Event>& evs, Ev t) {
	int k = 0;
	for (const Event& e : evs) if (e.type == t) k++;
	return k;
}

// Three peoples on one landmass: A at the best site, B 7 hops away, C 7 hops beyond B (A and C 14 apart:
// in contact, no border). Every v3.1 rule is off (the TUNE defaults) and there is no noise.
struct Three {
	std::unique_ptr<World> w;
	int a = -1, b = -1, c = -1, sa = -1, sb = -1, sc = -1;
	bool ok = false;
};
Three three(double popA = 1000, double popB = 1000, double popC = 1000) {
	Three t;
	t.w = makeWorld({ .L = 6, .seed = 3, .land = 0.45 });
	World& w = *t.w;
	w.tune<DiplomacyTune>().noise = 0;
	const int c0 = bestHab(w);
	const int c1 = siteNear(w, c0, 7);
	const int c2 = c1 >= 0 ? siteNear(w, c1, 7, c0) : -1;
	if (c1 < 0 || c2 < 0 || hopsFrom(w, c0)[size_t(c2)] < 12) return t;
	t.a = culture(w);
	t.b = culture(w);
	t.c = culture(w);
	t.sa = town(w, c0, t.a, popA);
	t.sb = town(w, c1, t.b, popB);
	t.sc = town(w, c2, t.c, popC);
	t.ok = true;
	return t;
}

// The favour sums Society keeps (vars.num society_fgrace_<id> / fwrath_<id>) for a wanted rate (impulse a year).
void setFavour(World& w, const char* what, int cid, double rate) {
	const double hl = w.tune<SocietyTune>().favourHalfLife;
	w.vars.num[std::string("society_") + what + "_" + std::to_string(cid)] = rate / (1 - std::pow(0.5, 1.0 / hl));
}

bool finiteWorld(const World& w) { return checkFinite(w).empty(); }

} // namespace

DM_TEST("diplomacy v3.1: the defaults keep every new rule off (v3 relations unchanged)") {
	const DiplomacyTune T;
	near(T.envyK + T.sharedWrathK + T.plagueK + T.zealK + T.faithK + T.feudK + T.warStall + T.warredMax, 0, 0, "relation terms, feuds, stalls off");
	ok(T.allyForm > 1 && !T.allyCall && !T.allyDeter && !T.omenSworn && !T.omenCalm, "no alliances, no sworn or calming Omen");
	near(T.nukeRepeatMul, 1, 0, "the bomb as before");
	near(T.thankDevotion + T.blameLove, 0, 0, "thanks change no feelings");
	near(T.warWeary, 1, 0, "weariness never stops a war");
	for (double m : T.borderAge) near(m, 1, 0, "frontier friction x1 in every age");
}

DM_TEST("diplomacy v3.1: the god in the relations (envy, shared wrath, plague blame, zeal, kinship) follow their terms exactly") {
	// v after one diplomacy() of the pair (A, B) with the given setup, minus the same without it
	auto dvWith = [](const std::function<void(Three&)>& setup) -> double {
		Three t = three();
		if (!t.ok) return 99;
		World& w = *t.w;
		society::rel(w, t.a, t.b)->v = 0.1;
		society::rel(w, t.a, t.b)->trade = 0.5;
		setup(t);
		society::diplomacy(w);
		return society::rel(w, t.a, t.b)->v;
	};
	const double base = dvWith([](Three&) {});
	if (!ok(base < 50, "three peoples placed")) return;
	// envy: A is the god's favourite (grace rate 0.01 = favourRef): v -= envyK x 1
	const double envy = dvWith([](Three& t) {
		t.w->tune<DiplomacyTune>().envyK = 0.004;
		t.w->tune<DiplomacyTune>().favourRef = 0.01;
		setFavour(*t.w, "fgrace", t.a, 0.02);
	});
	near(envy - base, -0.004, 1e-9, "a favourite is envied: -envyK (saturated)");
	// the scourged blame the spared the same way (wrath counts as negative favour)
	const double blame = dvWith([](Three& t) {
		t.w->tune<DiplomacyTune>().envyK = 0.004;
		t.w->tune<DiplomacyTune>().favourRef = 0.01;
		setFavour(*t.w, "fwrath", t.a, 0.005);
	});
	near(blame - base, -0.002, 1e-9, "half the reference in wrath on A only: -envyK x 0.5");
	// both struck alike: no envy, and the shared wrath draws them together
	const double shared = dvWith([](Three& t) {
		DiplomacyTune& T = t.w->tune<DiplomacyTune>();
		T.envyK = 0.004;
		T.favourRef = 0.01;
		T.sharedWrathK = 0.006;
		setFavour(*t.w, "fwrath", t.a, 0.005);
		setFavour(*t.w, "fwrath", t.b, 0.005);
	});
	near(shared - base, 0.003, 1e-9, "struck together, they stand together: +sharedWrathK x 0.5");
	// plague: half of A's people sick, trade 0.5 (+ the touching frontier, saturated at 1)
	double border = 0;
	{
		const double noTrade = dvWith([](Three& t) { society::rel(*t.w, t.a, t.b)->trade = 0; });
		const double noTradeSick = dvWith([](Three& t) {
			society::rel(*t.w, t.a, t.b)->trade = 0;
			t.w->tune<DiplomacyTune>().plagueK = 0.02;
			t.w->settlements[size_t(t.sa)].plague = 0.5;
		});
		border = (noTradeSick - noTrade) / (-0.02 * 1.0);   // the sick share is 1 (one town): -plagueK x border
		ok(border > 0 && border <= 1, "the pair shares a frontier");
	}
	const double sick = dvWith([](Three& t) {
		t.w->tune<DiplomacyTune>().plagueK = 0.02;
		t.w->settlements[size_t(t.sa)].plague = 0.5;
	});
	near(sick - base, -0.02 * std::min(1.0, 0.5 + border), 1e-9, "the fever came from over there: -plagueK x sick x sat(trade + border)");
	// a prophet's zeal: A devout (0.6) with a prophet, B lukewarm (0.3): -zealK x sat(0.3 / 0.3)
	const double zeal = dvWith([](Three& t) {
		t.w->tune<DiplomacyTune>().zealK = 0.01;
		t.w->settlements[size_t(t.sa)].devotion = 0.6;
		t.w->settlements[size_t(t.sb)].devotion = 0.3;
		t.w->cultures[size_t(t.a)].prophet = Prophet{ "P", t.sa, 1e9 };
	});
	const double zealBase = dvWith([](Three& t) {
		t.w->settlements[size_t(t.sa)].devotion = 0.6;
		t.w->settlements[size_t(t.sb)].devotion = 0.3;
		t.w->cultures[size_t(t.a)].prophet = Prophet{ "P", t.sa, 1e9 };
	});
	near(zeal - zealBase, -0.01, 1e-9, "zeal against the less devout");
	// kinship: both devout (>= 0.4), one with a prophet: +faithK
	auto kin = [](double k) {
		return [k](Three& t) {
			t.w->tune<DiplomacyTune>().faithK = k;
			t.w->settlements[size_t(t.sa)].devotion = 0.5;
			t.w->settlements[size_t(t.sb)].devotion = 0.5;
			t.w->cultures[size_t(t.b)].prophet = Prophet{ "P", t.sb, 1e9 };
		};
	};
	near(dvWith(kin(0.006)) - dvWith(kin(0)), 0.006, 1e-9, "two devout peoples and a prophet: +faithK");
	// frontier friction by age: borderK x borderAge[the older side's age]
	const double old = dvWith([](Three& t) {
		t.w->tune<DiplomacyTune>().borderAge = { 1, 1, 2, 2, 2, 2, 2 };
		t.w->cultures[size_t(t.b)].age = Age::IRON;
	});
	const double oldBase = dvWith([](Three& t) { t.w->cultures[size_t(t.b)].age = Age::IRON; });
	near(old - oldBase, -DiplomacyTune{}.borderK * border, 1e-9, "Iron age frontier x2: one more borderK x border");
}

DM_TEST("diplomacy v3.1: alliances form, answer a call to arms, deter, end together and break") {
	Three t = three(1000, 1000, 1000);
	if (!ok(t.ok, "three peoples placed")) return;
	World& w = *t.w;
	DiplomacyTune& T = w.tune<DiplomacyTune>();
	T.allyForm = 0.55;
	T.allyBreak = 0.15;
	T.allyCall = true;
	T.allyDeter = true;
	T.relax = 0;
	society::rel(w, t.b, t.c)->v = 0.8;
	society::rel(w, t.a, t.b)->v = 0.0;
	society::rel(w, t.a, t.c)->v = 0.0;
	auto evs = capture(w, [&] { society::diplomacy(w); });
	eq(count(evs, Ev::Alliance), 1, "B and C ally");
	for (const Event& e : evs)
		if (const EvAlliance* al = e.as<EvAlliance>()) { eq(al->a, t.b, "a"); eq(al->b, t.c, "b"); ok(al->formed, "formed"); }
	ok(w.vars.num.count("diplomacy_ally" + std::to_string(t.b) + "_" + std::to_string(t.c)) == 1, "persisted in vars.num");
	// deterrence: A (1000 people) cannot match B + C (2000 x 0.6 = 1200): no war however bitter
	society::rel(w, t.a, t.b)->v = -0.95;
	society::diplomacy(w);
	ok(!society::atWar(w, t.a, t.b), "an aggressor must match the alliance");
	// a stronger A attacks B: C answers the call
	w.settlements[size_t(t.sa)].pop = 5000;
	society::rel(w, t.a, t.b)->v = -0.95;
	evs = capture(w, [&] { society::diplomacy(w); });
	ok(society::atWar(w, t.a, t.b), "A attacks B");
	ok(society::atWar(w, t.c, t.a), "C joins against A");
	int joined = 0;
	for (const Event& e : evs)
		if (const EvWarDeclared* d = e.as<EvWarDeclared>(); d && d->ally == t.b) { joined++; eq(d->a, t.c, "the ally declares"); eq(d->b, t.a, "on the aggressor"); }
	eq(joined, 1, "one war:declared with ally = B");
	// the league makes peace together
	w.year += 5;
	evs = capture(w, [&] { society::makePeace(w, t.a, t.b, 40, t.b); });
	ok(!society::atWar(w, t.a, t.b) && !society::atWar(w, t.c, t.a), "both wars end");
	for (const Event& e : evs)
		if (const EvWarEnded* we = e.as<EvWarEnded>(); we && (we->a == t.c || we->b == t.c)) eq(we->victor, t.c, "B won: so did its ally");
	// the alliance breaks when the friendship cools
	society::rel(w, t.b, t.c)->v = 0.1;
	evs = capture(w, [&] { society::diplomacy(w); });
	int broke = 0;
	for (const Event& e : evs) if (const EvAlliance* al = e.as<EvAlliance>(); al && !al->formed) broke++;
	eq(broke, 1, "alliance:formed false once");
	ok(w.vars.num.count("diplomacy_ally" + std::to_string(t.b) + "_" + std::to_string(t.c)) == 0, "key gone");
	// at most allyMax allies each
	Three u = three();
	if (!u.ok) return;
	u.w->tune<DiplomacyTune>().allyForm = 0.55;
	u.w->tune<DiplomacyTune>().allyMax = 1;
	u.w->tune<DiplomacyTune>().relax = 0;
	society::rel(*u.w, u.a, u.b)->v = 0.9;
	society::rel(*u.w, u.b, u.c)->v = 0.7;
	society::diplomacy(*u.w);
	ok(society::peek(*u.w, u.a, u.b) && u.w->vars.num.count("diplomacy_ally" + std::to_string(u.a) + "_" + std::to_string(u.b)), "the warmest pair allies");
	ok(!u.w->vars.num.count("diplomacy_ally" + std::to_string(u.b) + "_" + std::to_string(u.c)), "B has no room for a second ally");
	ok(finiteWorld(w) && finiteWorld(*u.w), "finite");
}

DM_TEST("diplomacy v3.1: the Omen's sworn peace rests the pair at omenTarget; tense() names the brink") {
	Three t = three();
	if (!ok(t.ok, "placed")) return;
	World& w = *t.w;
	DiplomacyTune& T = w.tune<DiplomacyTune>();
	ok(!society::swearPeace(w, t.a, t.b), "off by default");
	ok(society::tense(w, t.a).empty(), "calming off by default");
	T.omenSworn = true;
	T.omenCalm = true;
	T.omenTension = -0.3;
	society::rel(w, t.a, t.b)->v = -0.4;
	society::rel(w, t.a, t.c)->v = -0.2;
	society::diplomacy(w);                                   // contact is set here
	const std::vector<int> brink = society::tense(w, t.a);
	eq(brink.size(), size_t(1), "one neighbour on the brink");
	if (!brink.empty()) eq(brink[0], t.b, "B (v -0.4 <= -0.3), not C (-0.2)");
	w.year = 100;
	ok(society::swearPeace(w, t.a, t.b), "sworn");
	ok(!society::swearPeace(w, t.a, t.a) && !society::swearPeace(w, -1, t.a), "bad pairs refused");
	const double v0 = society::rel(w, t.a, t.b)->v, c0 = society::rel(w, t.a, t.c)->v;
	society::diplomacy(w);
	const double vA = society::rel(w, t.a, t.b)->v - v0, vC = society::rel(w, t.a, t.c)->v - c0;
	// the same terms but the resting point: relax x (omenTarget - relTarget) more than an unsworn pair would get
	near((vA - vC) - T.relax * ((T.omenTarget - v0) - (T.relTarget - c0)), 0, 0.02, "sworn pair pulled toward +0.3");
	w.year = 100 + T.omenYears + 1;
	society::diplomacy(w);
	ok(!w.vars.num.count("diplomacy_sworn" + std::to_string(t.a) + "_" + std::to_string(t.b)), "the oath ends after omenYears");
}

DM_TEST("diplomacy v3.1: blame and thanks (war:declared.blamed, war:ended.thanked and their feelings)") {
	Three t = three();
	if (!ok(t.ok, "placed")) return;
	World& w = *t.w;
	DiplomacyTune& T = w.tune<DiplomacyTune>();
	T.thankDevotion = 0.05;
	T.blameLove = 0.05;
	auto evs = capture(w, [&] { society::declareWar(w, t.a, t.b); });
	for (const Event& e : evs) if (const EvWarDeclared* d = e.as<EvWarDeclared>()) ok(!d->blamed && d->ally == -1, "no deed of the god: not blamed");
	society::makePeace(w, t.a, t.b);
	setFavour(w, "fwrath", t.a, 0.01);                       // the god struck A lately
	w.year = 200;
	evs = capture(w, [&] { society::declareWar(w, t.a, t.b); });
	for (const Event& e : evs) if (const EvWarDeclared* d = e.as<EvWarDeclared>()) ok(d->blamed, "A blames the god's anger on B");
	w.settlements[size_t(t.sa)].devotion = 0.4;
	w.settlements[size_t(t.sb)].love = 0.2;
	setFavour(w, "fgrace", t.a, 0.01);                       // ... and blessed it while it fought
	w.year = 230;
	evs = capture(w, [&] { society::makePeace(w, t.a, t.b, 40, t.a); });
	bool thanked = false;
	for (const Event& e : evs) if (const EvWarEnded* we = e.as<EvWarEnded>()) thanked = we->thanked;
	ok(thanked, "the victor thanks the god");
	near(w.settlements[size_t(t.sa)].devotion, 0.45, 1e-12, "the victor's devotion + thankDevotion");
	near(w.settlements[size_t(t.sb)].love, 0.15, 1e-12, "the loser's love - blameLove");
	// a draw thanks nobody
	society::declareWar(w, t.a, t.b);
	evs = capture(w, [&] { society::makePeace(w, t.a, t.b, 40, -1); });
	for (const Event& e : evs) if (const EvWarEnded* we = e.as<EvWarEnded>()) ok(!we->thanked, "a draw");
}

DM_TEST("diplomacy v3.1: feuds lengthen truces; weary peoples start no war; a war of nothing peters out; the feared bomb") {
	Three t = three();
	if (!ok(t.ok, "placed")) return;
	World& w = *t.w;
	DiplomacyTune& T = w.tune<DiplomacyTune>();
	T.feudK = 1;
	T.feudMax = 2;
	w.year = 10;
	society::declareWar(w, t.a, t.b);
	society::makePeace(w, t.a, t.b, 40);
	near(society::peek(w, t.a, t.b)->truce, 50, 1e-9, "first war: the plain truce");
	for (int k = 0; k < 3; k++) { society::declareWar(w, t.a, t.b); society::makePeace(w, t.a, t.b, 40); }
	near(society::peek(w, t.a, t.b)->truce, 10 + 40 * 3, 1e-9, "fourth war: x (1 + feudK x min(feudMax, 3))");
	// weariness: an exhausted aggressor (0.5 > warWeary 0.3) starts nothing
	Three u = three();
	if (!u.ok) return;
	World& x = *u.w;
	x.tune<DiplomacyTune>().warWeary = 0.3;
	x.cultures[size_t(u.a)].exhaustion = 0.5;
	x.cultures[size_t(u.a)].traits.aggression = 0.9;
	x.cultures[size_t(u.b)].traits.aggression = 0.1;
	x.cultures[size_t(u.b)].exhaustion = 0.5;
	society::rel(x, u.a, u.b)->v = -0.95;
	x.year = 50;
	society::diplomacy(x);
	ok(!society::atWar(x, u.a, u.b), "weary: no war");
	x.cultures[size_t(u.a)].exhaustion = 0.0;
	x.cultures[size_t(u.b)].exhaustion = 0.0;
	x.tune<DiplomacyTune>().warredMax = 5;
	x.vars.num["diplomacy_warred" + std::to_string(u.a)] = 6;
	society::rel(x, u.a, u.b)->v = -0.95;
	society::diplomacy(x);
	ok(!society::atWar(x, u.a, u.b), "a century of war remembered (6 > warredMax 5): no war");
	x.vars.num["diplomacy_warred" + std::to_string(u.a)] = 0;
	society::rel(x, u.a, u.b)->v = -0.95;
	society::diplomacy(x);
	ok(society::atWar(x, u.a, u.b), "rested: war");
	// a stalled war: no army from either side (the stubs send none) for warStall years ends in a draw
	if (!stub::stubbed("units")) return;                     // run.ps1 all: real armies march, the war is not idle
	x.tune<DiplomacyTune>().warStall = 25;
	const double since = society::peek(x, u.a, u.b)->since;
	int ended = 0;
	for (int y = 0; y < 30 && society::atWar(x, u.a, u.b); y++) {
		x.year += 1;
		const auto evs = capture(x, [&] { society::diplomacy(x); });
		ended += count(evs, Ev::WarEnded);
	}
	ok(!society::atWar(x, u.a, u.b), "the war petered out");
	ok(x.year - since >= 25 && x.year - since <= 26, "after warStall years");
	eq(ended, 1, "war:ended once");
	ok(x.vars.num["diplomacy_warred" + std::to_string(u.a)] > 15, "the war years are remembered");
	// the feared bomb: after a first exchange (vars.lastNuke) and nukeRepeatMul 0, no Atomic war ever nukes
	Three v = three();
	if (!v.ok) return;
	World& z = *v.w;
	z.tune<DiplomacyTune>().nukeRepeatMul = 0;
	z.tune<DiplomacyTune>().nukeP = 1;
	z.tune<DiplomacyTune>().warMaxYears = 1e9;
	z.tune<DiplomacyTune>().exhaustYear = 0;
	z.vars.lastNuke = 1;
	for (int c : { v.a, v.b }) z.cultures[size_t(c)].age = Age::ATOMIC;
	society::declareWar(z, v.a, v.b);
	int nukes = 0;
	for (int y = 0; y < 50; y++) {
		z.year += 1;
		const auto evs = capture(z, [&] { society::diplomacy(z); });
		nukes += count(evs, Ev::Nuke);
	}
	eq(nukes, 0, "never again");
	ok(finiteWorld(w) && finiteWorld(x) && finiteWorld(z), "finite");
}

DM_TEST("diplomacy v3.1: deterministic with every rule on") {
	auto run = []() {
		Three t = three();
		if (!t.ok) return uint32_t(0);
		World& w = *t.w;
		DiplomacyTune& T = w.tune<DiplomacyTune>();
		T.noise = 0.05;
		T.envyK = 0.004; T.sharedWrathK = 0.006; T.plagueK = 0.02; T.zealK = 0.004; T.faithK = 0.006;
		T.allyForm = 0.55; T.allyCall = true; T.allyDeter = true; T.omenSworn = true; T.omenCalm = true;
		T.feudK = 1; T.warWeary = 0.3; T.warredMax = 18; T.warStall = 25; T.nukeRepeatMul = 0.1;
		T.relTarget = -0.2;
		setFavour(w, "fgrace", t.a, 0.01);
		for (int y = 0; y < 300; y++) { w.year += 1; society::diplomacy(w); }
		return hash(w);
	};
	const uint32_t h1 = run(), h2 = run();
	ok(h1 != 0, "ran");
	eq(h1, h2, "same world, same history");
}
