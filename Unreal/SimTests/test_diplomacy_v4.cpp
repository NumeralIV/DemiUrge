// DEMIURGE — SimTests/test_diplomacy_v4.cpp : the relations half's v4 mythos reads (SPEC-MYTHOS 1.4, 3.5, 3.6, 3.8;
// track M5 "society-v4"). Diplomacy reads each culture's Factors (aggression, warV, warStrength, relDrift, battle,
// defence, harmTaken, plagueHop, tradeEvery), the town's townFactors.harmTaken and a messenger's credibility.
//
// Factors are set as Mythos stores them (w.mythos.factors at MythosTune::phase 1: mythos::factors is an honest read
// in the stub and the real module). Tests that need a collaborator to act install the fakes of test_diplomacy.cpp
// (Civ, Units, Path) and return early when those modules are real (run.ps1 all).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "Civ.h"
#include "Core/Rng.h"
#include "Mythos.h"
#include "Path.h"
#include "Society.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "Units.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

bool seaC(const World& w, int c) { return double(w.elev[size_t(c)]) < w.seaLevel; }

std::vector<int> hops(const World& w, int from) {
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

int siteAt(const World& w, int from, int k, int rad = 2) {
	const std::vector<int> d = hops(w, from);
	std::vector<int32_t> tmp;
	for (int c = 0; c < w.n; c++) {
		if (d[size_t(c)] != k || seaC(w, c) || w.hab[size_t(c)] < 0.1f) continue;
		w.geo->ring(c, rad, tmp);
		bool land = true;
		for (const int m : tmp) if (seaC(w, m)) { land = false; break; }
		if (land) return c;
	}
	return -1;
}

int addCulture(World& w, double aggression) {
	Culture& c = state::newCulture(w);
	c.traits.aggression = aggression;
	c.name = "C" + std::to_string(c.id);
	return c.id;
}

int addTown(World& w, int cell, int cu, double pop, int radius = 2, int tier = 0) {
	Settlement& s = state::newSettlement(w, cell, cu);
	s.pop = pop;
	s.tier = tier;
	s.radius = radius;
	s.name = "T" + std::to_string(s.id);
	const int id = s.id;
	std::vector<int32_t> tmp;
	w.geo->ring(cell, radius, tmp);
	for (const int c : tmp) {
		if (seaC(w, c) || w.owner[size_t(c)] >= 0) continue;
		w.owner[size_t(c)] = int16_t(id);
		w.settlements[size_t(id)].cells.push_back(c);
	}
	return id;
}

int countEv(const std::vector<Event>& evs, Ev t) {
	int k = 0;
	for (const Event& e : evs) if (e.type == t) k++;
	return k;
}

void fakeDestroy(World& w, int sid, Cause cause) {
	Settlement* s = state::settlement(w, sid);
	if (!s || s->dead) return;
	s->dead = true;
	s->cause = cause;
	s->deadYear = w.year;
	for (int c = 0; c < w.n; c++) if (w.owner[size_t(c)] == sid) w.owner[size_t(c)] = -1;
	s->cells.clear();
}

void installFakes() {
	auto& h = stub::hooks();
	h.civ_capture = [](World& w, int sid, int cu) {
		if (Settlement* s = state::settlement(w, sid)) if (!s->dead) s->culture = cu;
	};
	h.civ_destroy = [](World& w, int sid, Cause cause) { fakeDestroy(w, sid, cause); };
	h.civ_damage = [](World& w, int sid, double frac, Cause cause) -> double {
		Settlement* s = state::settlement(w, sid);
		if (!s || s->dead) return 0.0;
		const double killed = s->pop * std::clamp(frac, 0.0, 1.0);
		s->pop -= killed;
		if (s->pop < 3) fakeDestroy(w, sid, cause);
		return killed;
	};
	h.civ_infect = [](World& w, int sid, double sev) -> bool {
		Settlement* s = state::settlement(w, sid);
		if (!s || s->dead || s->immune > 0) return false;
		s->plague = std::max(s->plague, sev);
		return true;
	};
	h.units_spawn = [](World& w, const units::SpawnOpts& o) -> Unit* {
		const Settlement* from = state::settlement(w, o.from);
		if (!from || from->dead || w.units.size() >= 300) return nullptr;
		Unit& u = state::newUnit(w, o.type);
		u.culture = o.culture;
		u.from = o.from;
		u.to = o.to;
		u.dest = o.dest;
		u.path = o.path.empty() ? std::vector<int32_t>{ from->cell, o.dest } : o.path;
		u.size = o.size;
		u.naval = o.naval;
		return &u;
	};
	h.units_kill = [](World& w, int id, std::string_view) -> bool { return state::eraseUnit(w, id); };
	h.path_find = [](const World& w, int from, int to, const path::FindOpts& o, double* cost) -> std::vector<int32_t> {
		(void)o;
		if (!w.valid(from) || !w.valid(to)) return {};
		if (cost) *cost = 1;
		return { from, to };
	};
}

bool fakesOk() { return stub::stubbed("units") && stub::stubbed("civ") && stub::stubbed("path"); }

struct Pair {
	std::unique_ptr<World> w;
	int a = -1, b = -1, sa = -1, sb = -1;
};
Pair makePair(int centres, double aggrA = 0.5, double aggrB = 0.5) {
	Pair p;
	p.w = makeWorld({ .L = 6, .seed = 3, .land = 0.45 });
	World& w = *p.w;
	const int c0 = bestHab(w);
	const int c1 = siteAt(w, c0, centres);
	p.a = addCulture(w, aggrA);
	p.b = addCulture(w, aggrB);
	p.sa = addTown(w, c0, p.a, 1000);
	p.sb = c1 >= 0 ? addTown(w, c1, p.b, 1000) : -1;
	return p;
}

// Store Factors the way Mythos does (one record per culture; phase 1 so that mythos::factors reads them).
Factors& factorsOf(World& w, int cid) {
	w.tune<MythosTune>().phase = std::max(1, w.tune<MythosTune>().phase);
	if (w.mythos.factors.size() < w.cultures.size()) w.mythos.factors.resize(w.cultures.size());
	return w.mythos.factors[size_t(cid)];
}
bool readsStored(const World& w, int cid) {
	return cid >= 0 && size_t(cid) < w.mythos.factors.size() && mythos::factors(w, cid) == w.mythos.factors[size_t(cid)];
}

} // namespace

// ================================================================ relations

DM_TEST("diplomacy v4: Factors aggression and relDrift enter the relations formula exactly") {
	Pair p = makePair(12);
	World& w = *p.w;
	if (!ok(p.sb >= 0, "site found")) return;
	w.cultures[size_t(p.a)].traits.aggression = 0.7;
	w.cultures[size_t(p.b)].traits.aggression = 0.6;
	w.settlements[size_t(p.sa)].love = 0.3;
	w.settlements[size_t(p.sb)].love = -0.1;
	factorsOf(w, p.a).aggression = 0.2;
	factorsOf(w, p.a).relDrift = 0.01;
	factorsOf(w, p.b).relDrift = -0.004;
	if (!readsStored(w, p.a)) return;          // a real Mythos that recomputes its records: nothing to check here
	Relation* r = society::rel(w, p.a, p.b);
	r->v = 0.2;
	r->trade = 0.5;
	Rng copy = w.rng;
	const double g = copy.gauss();
	society::diplomacy(w);
	// the documented v3.1 formula (test_diplomacy.cpp) + 0.2 more aggression on one side + both drifts
	const double expect = 0.2 + 0.03 * (0.1 - 0.2) + 0.04 * 0.5 - 0.02 * 0 - 0.03 * (0.7 + 0.2 + 0.6 - 0.9) - 0.03 * 0.4 + 0.03 * g
		+ 0.01 - 0.004;
	r = society::rel(w, p.a, p.b);
	near(r->v, expect, 1e-12, "v with the Factors terms");
	ok(w.rng.s == copy.s, "still exactly one gauss drawn");
}

DM_TEST("diplomacy v4: at phase 0 stored Factors are never read (v3.1 exactly)") {
	auto run = [](bool store) {
		Pair p = makePair(12);
		World& w = *p.w;
		if (p.sb < 0) return std::make_pair(0.0, 0.0);
		if (store) {
			w.mythos.factors.assign(w.cultures.size(), Factors{});
			for (Factors& f : w.mythos.factors) { f.aggression = 0.5; f.relDrift = 0.1; f.warV = -1; f.tradeEvery = 0.1; }
		}
		eq(w.tune<MythosTune>().phase, 0, "phase 0");
		Relation* r = society::rel(w, p.a, p.b);
		r->v = 0.2;
		for (int y = 1; y <= 5; y++) { w.year = y; society::diplomacy(w); }
		return std::make_pair(society::rel(w, p.a, p.b)->v, double(w.rng.s));
	};
	const auto plain = run(false), stored = run(true);
	ok(plain.first == stored.first && plain.second == stored.second, "identical relations and rng with and without records");
}

// ================================================================ war

DM_TEST("diplomacy v4: the aggressor's warV and warStrength gate a war") {
	struct Case { const char* name; double popA, warV, warStrength; bool war; };
	const Case cases[] = {
		{ "neutral: hostile and strong enough", 1000, 0, 1, true },
		{ "a Peace decree and a pact: warV -0.5 (threshold -1)", 1000, -0.5, 1, false },
		{ "the other side's warV does not hold the aggressor", 1000, 0, 1, true },
		{ "too weak (0.5 of the defender)", 500, 0, 1, false },
		{ "courage: warStrength x 0.8 (gate 0.48)", 500, 0, 0.8, true },
	};
	int idx = 0;
	for (const Case& k : cases) {
		Pair p = makePair(5, 0.8, 0.3);
		World& w = *p.w;
		if (!ok(p.sb >= 0, "site")) return;
		w.year = 10;
		w.tune<DiplomacyTune>().noise = 0;
		w.settlements[size_t(p.sa)].pop = k.popA;
		factorsOf(w, p.a).warV = k.warV;
		factorsOf(w, p.a).warStrength = k.warStrength;
		if (idx == 2) factorsOf(w, p.b).warV = -0.9;       // the defender's own offset is not the aggressor's
		idx++;
		if (!readsStored(w, p.a)) return;
		Relation* r = society::rel(w, p.a, p.b);
		r->v = -0.9;
		society::diplomacy(w);
		r = society::rel(w, p.a, p.b);
		eq(r->war, k.war, k.name);
	}
	// the Peace case precisely: v -0.6 after drift < -0.5 (a war) but not < -0.8
	Pair p = makePair(5, 0.8, 0.3);
	World& w = *p.w;
	if (p.sb < 0) return;
	w.tune<DiplomacyTune>().noise = 0;
	w.tune<DiplomacyTune>().relax = 0;
	w.tune<DiplomacyTune>().borderK = 0;
	w.tune<DiplomacyTune>().aggrK = 0;
	w.tune<DiplomacyTune>().loveK = 0;
	society::rel(w, p.a, p.b)->v = -0.6;
	factorsOf(w, p.a).warV = -0.3;
	if (!readsStored(w, p.a)) return;
	society::diplomacy(w);
	ok(!society::atWar(w, p.a, p.b), "v -0.6 with warV -0.3: no war");
	factorsOf(w, p.a).warV = -0.05;
	society::diplomacy(w);
	ok(society::atWar(w, p.a, p.b), "v -0.6 with warV -0.05: war");
}

// ================================================================ battles

DM_TEST("diplomacy v4: battle, defence and harmTaken decide a battle (strength / harmTaken)") {
	stub::Guard guard(true);
	if (!fakesOk()) return;
	installFakes();
	struct Case { const char* name; double battle, defence, harm, townHarm; };
	const Case cases[] = {
		{ "neutral", 1, 1, 1, 1 },
		{ "Valour: battle x 1.1", 1.1, 1, 1, 1 },
		{ "Defence decree: defence x 1.15", 1, 1.15, 1, 1 },
		{ "Endurance (the town): harm x 0.5 = capture needs x 2", 1, 1, 1, 0.5 },
		{ "the culture's harmTaken x 0.8", 1, 1, 0.8, 1 },
	};
	int wins = 0, losses = 0;
	for (const Case& k : cases) {
		for (uint32_t trial = 0; trial < 10; trial++) {
			Pair p = makePair(5, 0.8, 0.3);
			World& w = *p.w;
			if (p.sb < 0) return;
			w.rng = Rng(200 + trial);
			factorsOf(w, p.a).battle = k.battle;
			factorsOf(w, p.b).defence = k.defence;
			factorsOf(w, p.b).harmTaken = k.harm;
			stub::hooks().mythos_townFactors = [&](const World&, int sid) {
				TownFactors t;
				if (sid == p.sb) t.harmTaken = k.townHarm;
				return t;
			};
			Settlement& d = w.settlements[size_t(p.sb)];
			d.pop = 1000;
			society::declareWar(w, p.a, p.b);
			Unit& u = state::newUnit(w, UnitType::Army);
			const double size = 40 + 4 * trial;
			u.culture = p.a; u.from = p.sa; u.to = p.sb; u.dest = d.cell; u.size = size;
			const int uid = u.id;
			Rng copy = w.rng;
			const double attack = size * 1.0 * copy.range(0.7, 1.3) * k.battle;
			const double defence = 0.05 * 1000 * 1.0 * k.defence / (k.harm * k.townHarm);
			const bool expectWin = attack > defence;
			bool won = false;
			const auto evs = capture(w, [&] { society::onArrive(w, uid); });
			for (const Event& e : evs) if (const EvBattle* bt = e.as<EvBattle>()) won = bt->won;
			eq(countEv(evs, Ev::Battle), 1, std::string(k.name) + ": one battle");
			eq(won, expectWin, std::string(k.name) + " #" + std::to_string(trial) + ": the documented strengths");
			(expectWin ? wins : losses)++;
		}
	}
	std::printf("    v4 battles: %d won, %d lost\n", wins, losses);
	ok(wins > 5 && losses > 5, "both outcomes covered");
}

// ================================================================ trade and plague

DM_TEST("diplomacy v4: the receiving people's plagueHop (Open x 1.3, Closed x 0.7, sealed 0)") {
	stub::Guard guard(true);
	if (!fakesOk()) return;
	installFakes();
	auto infections = [](double hop) {
		int n = 0;
		for (uint32_t t = 0; t < 40; t++) {
			Pair p = makePair(8);
			World& w = *p.w;
			if (p.sb < 0) return -1;
			w.rng = Rng(500 + t);
			w.settlements[size_t(p.sa)].plague = 0.6;
			factorsOf(w, p.b).plagueHop = hop;
			factorsOf(w, p.a).plagueHop = 5;                   // the sender's own gates do not matter
			Unit& u = state::newUnit(w, UnitType::Trader);
			u.culture = p.a; u.from = p.sa; u.to = p.sb; u.dest = w.settlements[size_t(p.sb)].cell; u.size = 4;
			u.path = { w.settlements[size_t(p.sa)].cell, u.dest };
			society::onArrive(w, u.id);
			if (w.settlements[size_t(p.sb)].plague > 0) n++;
		}
		return n;
	};
	const int none = infections(0), open = infections(1.3), plain = infections(1), closed = infections(0.7);
	std::printf("    plague hops in 40 caravans (P = 0.5 x 0.6 x hop): sealed %d, closed %d, neutral %d, open %d\n", none, closed, plain, open);
	eq(none, 0, "plagueHop 0: never");
	ok(closed <= plain && plain <= open, "monotone in the receiving people's factor");
	ok(plain >= 5 && plain <= 20, "neutral about 0.3 x 40");
}

DM_TEST("diplomacy v4: tradeEvery sets the caravan cadence of a culture's towns") {
	stub::Guard guard(true);
	if (!fakesOk()) return;
	installFakes();
	auto caravans = [](double every) {
		Pair p = makePair(8);
		World& w = *p.w;
		if (p.sb < 0) return -1;
		for (const int s : { p.sa, p.sb }) { w.settlements[size_t(s)].tier = 2; w.settlements[size_t(s)].b[Building::MARKET] = 1; }
		factorsOf(w, p.a).tradeEvery = every;
		int n = 0;
		for (int y = 1; y <= 60; y++) {
			w.year = y;
			society::diplomacy(w);
			for (const Unit& u : w.units) if (u.type == UnitType::Trader && u.culture == p.a) n++;
			std::vector<int> ids;
			for (const Unit& u : w.units) ids.push_back(u.id);
			for (const int id : ids) state::eraseUnit(w, id);
		}
		return n;
	};
	const int plain = caravans(1), fast = caravans(0.6), slow = caravans(2);
	std::printf("    caravans in 60 years from one town: every x 0.6 %d, neutral %d, x 2 %d\n", fast, plain, slow);
	ok(plain >= 10 && plain <= 13, "neutral: one every 5 years");
	ok(fast >= 18 && fast <= 21, "x 0.6: one every 3 years");
	ok(slow >= 5 && slow <= 7, "x 2: one every 10 years");
}

// ================================================================ messengers

DM_TEST("diplomacy v4: a messenger's visit raises devotion by prophetDevotion x (0.5 + credibility)") {
	struct Case { const char* name; int figure; double cred; double gain; };
	const Case cases[] = {
		{ "an unnamed prophet (v3.1)", -1, 0, 0.15 },
		{ "a believed messenger (0.9)", 41, 0.9, 0.15 * 1.4 },
		{ "the Unbelieved (0.1)", 42, 0.1, 0.15 * 0.6 },
		{ "a neutral 0.5", 43, 0.5, 0.15 },
	};
	for (const Case& k : cases) {
		Pair p = makePair(5);
		World& w = *p.w;
		if (p.sb < 0) return;
		w.tune<MythosTune>().phase = 1;
		const int host = addTown(w, siteAt(w, w.settlements[size_t(p.sa)].cell, 9, 1), p.a, 400, 1);
		if (k.figure >= 0) {
			Figure f;
			f.id = k.figure; f.kind = uint8_t(FigureKind::Messenger); f.culture = p.a; f.home = p.sa; f.name = "Ossa"; f.credibility = k.cred;
			w.mythos.figures.push_back(f);
			w.mythos.nextId = k.figure + 1;
		}
		w.cultures[size_t(p.a)].prophet = Prophet{ "Ossa", p.sa, 300, k.figure };
		w.settlements[size_t(host)].devotion = 0.4;
		Unit& u = state::newUnit(w, UnitType::Prophet);
		u.culture = p.a; u.from = p.sa; u.to = host;
		society::onArrive(w, u.id);
		near(w.settlements[size_t(host)].devotion, 0.4 + k.gain, 1e-12, k.name);
		eq(w.cultures[size_t(p.a)].prophet->settlement, host, std::string(k.name) + ": moved");
	}
}

// ================================================================ robustness

DM_TEST("diplomacy v4: non-finite Factors never poison relations, wars or battles") {
	Pair p = makePair(5, 0.8, 0.3);
	World& w = *p.w;
	if (p.sb < 0) return;
	const double nan = std::nan("");
	for (const int c : { p.a, p.b }) {
		Factors& f = factorsOf(w, c);
		f.aggression = nan; f.warV = nan; f.relDrift = INFINITY; f.warStrength = nan; f.battle = nan; f.defence = -INFINITY;
		f.harmTaken = nan; f.plagueHop = nan; f.tradeEvery = nan;
	}
	society::rel(w, p.a, p.b)->v = -0.9;
	for (int y = 1; y <= 30; y++) { w.year = y; society::diplomacy(w); }
	const Relation* r = society::peek(w, p.a, p.b);
	ok(r && std::isfinite(r->v), "relations finite");
	w.mythos.factors.clear();                       // the bad records themselves are not the sim's state
	noNaN(w, "after 30 years of bad records");
}
