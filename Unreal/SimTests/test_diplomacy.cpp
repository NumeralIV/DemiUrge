// DEMIURGE — SimTests/test_diplomacy.cpp : the relations half of Society (Sim/Diplomacy.cpp; SPEC-SIM 3.8,
// BUILD.md A10). Owner: A10 diplomacy.
//
// Runs with every other module stubbed. Where a collaborator must act, a test installs hooks: a fake Civ
// (capture / destroy / damage / infect), fake Units (spawn creates the unit; the test moves it), and a
// breadth-first path::find. Those tests return early when the module is linked for real (`run.ps1 all`).
// The long runs ("two aggressive neighbours", "six cultures") also write maps to shots/ue-diplomacy-*.png.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Civ.h"
#include "Core/Rng.h"
#include "MapImg.h"
#include "Path.h"
#include "Society.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "Units.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

namespace mi = dm::mapimg;

bool seaC(const World& w, int c) { return double(w.elev[size_t(c)]) < w.seaLevel; }

// hop distance from `from` over every cell (sea included)
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

// a habitable land cell exactly k hops from `from` whose ring of `rad` hops is all land
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

int addCulture(World& w, double aggression, int age = 0) {
	Culture& c = state::newCulture(w);
	c.traits.aggression = aggression;
	c.age = age;
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

int liveCount(const World& w, int cu) {
	int k = 0;
	for (const Settlement& s : w.settlements) if (!s.dead && s.culture == cu && s.cell >= 0) k++;
	return k;
}

int countEv(const std::vector<Event>& evs, Ev t) {
	int k = 0;
	for (const Event& e : evs) if (e.type == t) k++;
	return k;
}

// ---------------------------------------------------------------- fakes (hooks)

double speedOf(UnitType t) { return t == UnitType::Trader ? 7 : t == UnitType::Prophet ? 4 : 5; }

// breadth-first path over passable cells, cost = steps
std::vector<int32_t> bfsPath(const World& w, int from, int to, NavalMode naval, double maxCost) {
	if (!w.valid(from) || !w.valid(to)) return {};
	if (from == to) return { from };
	auto pass = [&](int c) {
		if (!seaC(w, c)) return true;
		if (naval == NavalMode::Open) return true;
		if (naval == NavalMode::Coastal) return double(w.elev[size_t(c)]) > w.seaLevel - 0.6;
		return false;
	};
	if (!pass(to)) return {};
	std::vector<int> prev(size_t(w.n), -2);
	std::vector<int> q{ from };
	prev[size_t(from)] = -1;
	std::vector<int> depth(size_t(w.n), 0);
	for (size_t h = 0; h < q.size(); h++) {
		const int c = q[h];
		if (c == to) break;
		if (depth[size_t(c)] >= int(maxCost)) continue;
		for (int k = 0; k < w.geo->nbrN[size_t(c)]; k++) {
			const int m = w.geo->nbr[size_t(6 * c + k)];
			if (m < 0 || prev[size_t(m)] != -2 || !pass(m)) continue;
			prev[size_t(m)] = c;
			depth[size_t(m)] = depth[size_t(c)] + 1;
			q.push_back(m);
		}
	}
	if (prev[size_t(to)] == -2) return {};
	std::vector<int32_t> p;
	for (int c = to; c != -1; c = prev[size_t(c)]) p.push_back(c);
	std::reverse(p.begin(), p.end());
	return p;
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
		std::vector<int32_t> p = o.path;
		if (p.empty()) p = { from->cell, o.dest };
		Unit& u = state::newUnit(w, o.type);
		u.culture = o.culture;
		u.from = o.from;
		u.to = o.to;
		u.dest = o.dest;
		u.path = p;
		u.size = o.size;
		u.naval = o.naval;
		u.speed = speedOf(o.type);
		u.data["arrive"] = w.year + std::max(1.0, std::ceil(double(p.size() - 1) / u.speed));
		return &u;
	};
	h.units_kill = [](World& w, int id, std::string_view) -> bool { return state::eraseUnit(w, id); };
	h.path_find = [](const World& w, int from, int to, const path::FindOpts& o, double* cost) -> std::vector<int32_t> {
		std::vector<int32_t> p = bfsPath(w, from, to, o.naval, std::isnan(o.maxCost) ? 400 : o.maxCost);
		if (cost) *cost = p.empty() ? path::kNoPath : double(p.size() - 1);
		return p;
	};
}

bool fakesOk() { return stub::stubbed("units") && stub::stubbed("civ") && stub::stubbed("path"); }

// move every unit whose travel time is over: armies whose war is over or whose target died stand down
void arrive(World& w, bool all = false) {
	std::vector<int> ids;
	for (const Unit& u : w.units) {
		const auto it = u.data.find("arrive");
		if (all || it == u.data.end() || it->second <= w.year) ids.push_back(u.id);
	}
	for (const int id : ids) {
		const Unit* u = state::findUnit(w, id);
		if (!u) continue;
		const Settlement* t = state::settlement(w, u->to);
		if ((u->type == UnitType::Army || u->type == UnitType::Trader) && (!t || t->dead)) { state::eraseUnit(w, id); continue; }
		if (u->type == UnitType::Army && !society::atWar(w, u->culture, t->culture)) { state::eraseUnit(w, id); continue; }
		society::onArrive(w, id);
		state::eraseUnit(w, id);
	}
}

// two cultures with one settlement each, centres `gap` hops apart, radius 2
struct Pair {
	std::unique_ptr<World> w;
	int a = -1, b = -1, sa = -1, sb = -1;
};
Pair makePair(int centres, double aggrA = 0.5, double aggrB = 0.5, int L = 6, uint32_t seed = 3) {
	Pair p;
	p.w = makeWorld({ .L = L, .seed = seed, .land = 0.45 });
	World& w = *p.w;
	const int c0 = bestHab(w);
	const int c1 = siteAt(w, c0, centres);
	p.a = addCulture(w, aggrA);
	p.b = addCulture(w, aggrB);
	p.sa = addTown(w, c0, p.a, 1000);
	p.sb = c1 >= 0 ? addTown(w, c1, p.b, 1000) : -1;
	return p;
}

} // namespace

// ================================================================ accessors

DM_TEST("diplomacy: rel / peek / atWar, symmetric key, peek never creates") {
	auto w = makeWorld({ .L = 3 });
	for (int k = 0; k < 3; k++) addCulture(*w, 0.5);
	ok(society::peek(*w, 0, 1) == nullptr, "nothing yet");
	eq(w->vars.rel.size(), size_t(0), "peek created nothing");
	Relation* r = society::rel(*w, 1, 0);
	ok(r != nullptr, "created on demand");
	ok(society::rel(*w, 0, 1) == r, "same object both ways");
	ok(w->vars.rel.count(std::make_pair(0, 1)) == 1, "stored under (low, high)");
	ok(society::peek(*w, 1, 0) == r && society::peek(*w, 0, 1) == r, "peek finds it");
	ok(society::rel(*w, 2, 2) == nullptr && society::rel(*w, 0, 9) == nullptr && society::rel(*w, -1, 0) == nullptr, "bad ids");
	ok(society::peek(*w, 2, 2) == nullptr && society::peek(*w, 0, 9) == nullptr, "peek bad ids");
	ok(!society::atWar(*w, 0, 1), "peace");
	r->war = true;
	ok(society::atWar(*w, 1, 0) && society::atWar(*w, 0, 1), "war both ways");
	ok(!society::atWar(*w, 0, 2) && society::peek(*w, 0, 2) == nullptr, "atWar never creates");
	eq(w->vars.rel.size(), size_t(1), "one relation");
}

// ================================================================ relations

DM_TEST("diplomacy: relations follow the documented formula exactly (contact, no border)") {
	Pair p = makePair(12);
	World& w = *p.w;
	if (!ok(p.sb >= 0, "site found")) return;
	w.cultures[size_t(p.a)].traits.aggression = 0.7;
	w.cultures[size_t(p.b)].traits.aggression = 0.6;
	w.settlements[size_t(p.sa)].love = 0.3;
	w.settlements[size_t(p.sb)].love = -0.1;
	Relation* r = society::rel(w, p.a, p.b);
	r->v = 0.2;
	r->trade = 0.5;
	Rng copy = w.rng;
	const double g = copy.gauss();
	society::diplomacy(w);
	const double expect = 0.2 + 0.03 * (0.1 - 0.2) + 0.04 * 0.5 - 0.02 * 0 - 0.03 * (0.7 + 0.6 - 0.9) - 0.03 * 0.4 + 0.03 * g;
	r = society::rel(w, p.a, p.b);
	near(r->v, expect, 1e-12, "v");
	near(r->trade, 0.5 * (1 - w.tune<DiplomacyTune>().tradeDecay), 1e-12, "trade decays");
	ok(r->contact, "contact within 10 hops");
	ok(!r->war, "no war");
	ok(w.rng.s == copy.s, "exactly one gauss drawn");
	std::printf("    formula: v 0.2 -> %.6f (gauss %.4f)\n", r->v, g);
	noNaN(w);
}

DM_TEST("diplomacy: border term, contact range, peace edict and common-enemy terms") {
	auto dvFor = [](int centres, Edict edict, bool common, double* border) -> double {
		Pair p = makePair(centres);
		World& w = *p.w;
		w.tune<DiplomacyTune>().noise = 0;
		if (p.sb < 0) return 99;
		if (common) {
			const int c = addCulture(w, 0.5);
			const int far = siteAt(w, w.settlements[size_t(p.sa)].cell, 30);
			if (far >= 0) addTown(w, far, c, 500);
			society::rel(w, p.a, c)->war = true;
			society::rel(w, p.b, c)->war = true;
		}
		w.cultures[size_t(p.a)].edict = edict;
		Relation* r = society::rel(w, p.a, p.b);
		r->v = 0.1;
		society::diplomacy(w);
		r = society::rel(w, p.a, p.b);
		if (!r->contact) return 100;
		const double dv = r->v - 0.1 - 0.0;   // relax term is 0 at v = 0.1; aggression 0.5 + 0.5 - 0.9 = 0.1 -> -0.003
		if (border) *border = -(dv + 0.003) / 0.02;
		return dv;
	};
	double bNear = 0, bFar = 0, bMid = 0;
	const double dvTouch = dvFor(5, Edict::None, false, &bNear);
	const double dvMid = dvFor(7, Edict::None, false, &bMid);
	const double dvFar = dvFor(12, Edict::None, false, &bFar);
	std::printf("    border: touching %.3f, 3 hops apart %.3f, 8 apart %.3f\n", bNear, bMid, bFar);
	ok(bNear > 0.3 && bNear <= 1.0 + 1e-9, "touching territories share a large frontier");
	ok(bMid > 0 && bMid < bNear, "a gap of 3 hops still counts, less");
	near(bFar, 0, 1e-9, "8 hops apart: no border");
	ok(dvTouch < dvMid && dvMid < dvFar, "closer is worse");
	// out of contact (gap > 10): no relation is created
	{
		Pair p = makePair(20);
		society::diplomacy(*p.w);
		ok(society::peek(*p.w, p.a, p.b) == nullptr, "no contact, no relation");
		Pair q = makePair(14);   // gap 10
		society::diplomacy(*q.w);
		ok(society::peek(*q.w, q.a, q.b) != nullptr && society::peek(*q.w, q.a, q.b)->contact, "gap 10: contact");
	}
	const double dvPeace = dvFor(12, Edict::Peace, false, nullptr);
	near(dvPeace - dvFar, 0.02, 1e-9, "peace edict +0.02/yr");
	const double dvCommon = dvFor(12, Edict::None, true, nullptr);
	near(dvCommon - dvFar, 0.02, 1e-9, "common enemy +0.02/yr");
}

// ================================================================ war

DM_TEST("diplomacy: war is declared under the documented conditions") {
	struct Case { const char* name; double aggrA, aggrB, popA, popB; int ageA; double truce; Edict edB; bool war; int agg; };
	const Case cases[] = {
		{ "hostile, A more aggressive", 0.8, 0.3, 1000, 1000, 0, 0, Edict::None, true, 0 },
		{ "truce not over", 0.8, 0.3, 1000, 1000, 0, 50, Edict::None, false, -1 },
		{ "peace edict on the other side", 0.8, 0.3, 1000, 1000, 0, 0, Edict::Peace, false, -1 },
		{ "aggressor too weak", 0.8, 0.3, 500, 1000, 0, 0, Edict::None, false, -1 },
		{ "weak in people, strong in tech", 0.8, 0.3, 500, 1000, Age::IRON, 0, Edict::None, true, 0 },
		{ "B more aggressive and strong enough", 0.3, 0.8, 1000, 700, 0, 0, Edict::None, true, 1 },
	};
	for (const Case& k : cases) {
		Pair p = makePair(5, k.aggrA, k.aggrB);
		World& w = *p.w;
		w.year = 10;
		w.tune<DiplomacyTune>().noise = 0;
		w.settlements[size_t(p.sa)].pop = k.popA;
		w.settlements[size_t(p.sb)].pop = k.popB;
		w.cultures[size_t(p.a)].age = k.ageA;
		w.cultures[size_t(p.b)].edict = k.edB;
		Relation* r = society::rel(w, p.a, p.b);
		r->v = -0.9;
		r->truce = k.truce;
		const auto evs = capture(w, [&] { society::diplomacy(w); });
		r = society::rel(w, p.a, p.b);
		const std::string tag = k.name;
		eq(r->war, k.war, tag);
		eq(countEv(evs, Ev::WarDeclared), k.war ? 1 : 0, tag + ": event");
		eq(w.stats.wars, k.war ? 1 : 0, tag + ": stats.wars");
		if (k.war) {
			eq(r->aggressor, k.agg, tag + ": aggressor");
			near(r->since, 10, 0, tag + ": since");
			for (const Event& e : evs)
				if (const EvWarDeclared* d = e.as<EvWarDeclared>()) { eq(d->a, k.agg, tag + ": a = aggressor"); eq(d->b, 1 - k.agg, tag + ": b"); }
		}
	}
	// v above -0.5: no war
	Pair p = makePair(5, 0.9, 0.9);
	p.w->tune<DiplomacyTune>().noise = 0;
	society::rel(*p.w, p.a, p.b)->v = -0.3;
	society::diplomacy(*p.w);
	ok(!society::atWar(*p.w, p.a, p.b), "v -0.3 (after drift above -0.5): no war");
}

DM_TEST("diplomacy: armies through units::spawn every 8 years; people leave only on success") {
	stub::Guard guard(true);
	if (!fakesOk()) return;
	installFakes();
	auto w = makeWorld({ .L = 6, .seed = 2, .land = 0.45 });
	const int c0 = bestHab(*w);
	const int a = addCulture(*w, 0.8), b = addCulture(*w, 0.3);
	const int big = addTown(*w, c0, a, 2000);
	const int e1 = addTown(*w, siteAt(*w, c0, 9), b, 800);
	const int e2 = addTown(*w, siteAt(*w, c0, 20), b, 3000);
	if (!ok(w->settlements[size_t(e1)].cell >= 0 && w->settlements[size_t(e2)].cell >= 0, "sites")) return;
	// the biggest town of A lies beyond 75 hops of every enemy town: out of an army's reach
	int farSite = -1;
	{
		const std::vector<int> d1 = hops(*w, w->settlements[size_t(e1)].cell), d2 = hops(*w, w->settlements[size_t(e2)].cell);
		for (int c = 0; c < w->n && farSite < 0; c++)
			if (!seaC(*w, c) && w->hab[size_t(c)] > 0.1f && d1[size_t(c)] >= 75 && d2[size_t(c)] >= 75) farSite = c;
	}
	const int bigFar = farSite >= 0 ? addTown(*w, farSite, a, 5000) : -1;
	if (!ok(bigFar >= 0, "far site")) return;
	society::declareWar(*w, a, b);
	std::map<int, std::vector<double>> sent;   // culture -> years
	std::map<int, double> sizes;
	for (int y = 0; y <= 24; y++) {
		w->year = y;
		w->cultures[size_t(a)].exhaustion = 0;
		w->cultures[size_t(b)].exhaustion = 0;
		const double popBig = w->settlements[size_t(big)].pop, popE2 = w->settlements[size_t(e2)].pop;
		const size_t before = w->units.size();
		society::diplomacy(*w);
		for (size_t k = before; k < w->units.size(); k++) {
			const Unit& u = w->units[k];
			if (u.type != UnitType::Army) continue;
			sent[u.culture].push_back(y);
			if (u.culture == a) {
				eq(u.from, big, "A: largest settlement within reach (the bigger one is 70 hops away)");
				eq(u.to, e1, "A: nearest enemy");
				near(u.size, 0.06 * popBig, 1e-9, "A: 6 %");
				near(w->settlements[size_t(big)].pop, popBig - u.size, 1e-9, "A: people removed");
			} else {
				eq(u.from, e2, "B: its largest settlement");
				near(w->settlements[size_t(e2)].pop, popE2 - u.size, 1e-9, "B: people removed");
			}
			eq(u.path.front(), w->settlements[size_t(u.from)].cell, "route starts at home");
			eq(u.path.back(), w->settlements[size_t(u.to)].cell, "route ends at the target");
		}
	}
	const std::vector<double> want{ 0, 8, 16, 24 };
	ok(sent[a] == want, "A sends at 0, 8, 16, 24: got " + show(sent[a]));
	ok(sent[b] == want, "B sends at 0, 8, 16, 24: got " + show(sent[b]));
	(void)bigFar;
	// spawn refuses: nobody leaves, and the side tries again next year
	stub::hooks().units_spawn = [](World&, const units::SpawnOpts&) -> Unit* { return nullptr; };
	const double pBig = w->settlements[size_t(big)].pop;
	const int calls0 = stub::called("units_spawn");
	w->year = 32;
	society::diplomacy(*w);
	near(w->settlements[size_t(big)].pop, pBig, 0, "refused: nobody removed");
	const int calls1 = stub::called("units_spawn");
	ok(calls1 > calls0, "spawn was asked");
	w->year = 33;
	society::diplomacy(*w);
	ok(stub::called("units_spawn") > calls1, "retried the next year");
	noNaN(*w);
}

DM_TEST("diplomacy: onArrive battles both ways with the documented strengths") {
	stub::Guard guard(true);
	if (!fakesOk()) return;
	installFakes();
	struct Case { const char* name; double armySize; double defPop; bool walls; int ageA; };
	const Case cases[] = {
		{ "strong army", 100, 1000, false, 0 },
		{ "weak army", 10, 1000, false, 0 },
		{ "walls double the defence", 100, 1000, true, 0 },
		{ "small town razed", 100, 80, false, 0 },
		{ "iron army", 30, 1000, false, Age::IRON },
	};
	int wins = 0, losses = 0;
	for (const Case& k : cases) {
		for (uint32_t trial = 0; trial < 12; trial++) {
			Pair p = makePair(5, 0.8, 0.3, 6, 3);
			World& w = *p.w;
			w.rng = Rng(100 + trial);
			w.cultures[size_t(p.a)].age = k.ageA;
			Settlement& d = w.settlements[size_t(p.sb)];
			d.pop = k.defPop;
			d.b[Building::WALLS] = k.walls ? 1 : 0;
			society::declareWar(w, p.a, p.b);
			Unit& u = state::newUnit(w, UnitType::Army);
			u.culture = p.a; u.from = p.sa; u.to = p.sb; u.dest = d.cell; u.size = k.armySize;
			u.path = { w.settlements[size_t(p.sa)].cell, d.cell };
			const int uid = u.id;
			Rng copy = w.rng;
			const double attack = k.armySize * DiplomacyTune{}.tech[size_t(k.ageA)] * copy.range(0.7, 1.3);
			const double defence = 0.05 * k.defPop * (k.walls ? 2 : 1) * 1.0;
			const bool expectWin = attack > defence;
			stub::calls().clear();
			const auto evs = capture(w, [&] { society::onArrive(w, uid); });
			const std::string tag = std::string(k.name) + " #" + std::to_string(trial);
			if (!eq(countEv(evs, Ev::Battle), 1, tag + ": one battle")) continue;
			for (const Event& e : evs)
				if (const EvBattle* bt = e.as<EvBattle>()) {
					eq(bt->won, expectWin, tag + ": won");
					eq(bt->attacker, p.a, tag); eq(bt->defender, p.b, tag); eq(bt->settlement, p.sb, tag);
					eq(bt->cell, w.settlements[size_t(p.sb)].cell, tag + ": cell");
				}
			const Settlement& after = w.settlements[size_t(p.sb)];
			if (expectWin) {
				wins++;
				near(w.cultures[size_t(p.b)].exhaustion, 0.08 + 0.15, 1e-12, tag + ": defender +0.08 +0.15");
				near(w.cultures[size_t(p.a)].exhaustion, 0, 0, tag + ": attacker unchanged");
				ok(state::findUnit(w, uid) != nullptr, tag + ": the winning army is left for Units to erase");
				if (k.defPop < 100) {
					eq(stub::called("civ_destroy"), 1, tag + ": razed");
					ok(after.dead && after.cause == Cause::War, tag + ": dead, cause war");
					eq(stub::called("civ_capture"), 0, tag + ": not captured");
				} else {
					eq(stub::called("civ_capture"), 1, tag + ": captured");
					eq(after.culture, p.a, tag + ": now the attacker's");
					near(after.pop, k.defPop * 0.85, 1e-9, tag + ": defender lost 15 %");
				}
			} else {
				losses++;
				near(w.cultures[size_t(p.a)].exhaustion, 0.08, 1e-12, tag + ": attacker +0.08");
				ok(state::findUnit(w, uid) == nullptr && stub::called("units_kill") == 1, tag + ": the army dies");
				eq(after.culture, p.b, tag + ": still the defender's");
				near(after.pop, k.defPop, 0, tag + ": no losses");
			}
			noNaN(w);
		}
	}
	std::printf("    battles: %d won, %d lost (as computed from the documented strengths)\n", wins, losses);
	ok(wins > 10 && losses > 10, "both outcomes covered");
	// an army whose cultures made peace stands down
	Pair p = makePair(5);
	World& w = *p.w;
	Unit& u = state::newUnit(w, UnitType::Army);
	u.culture = p.a; u.from = p.sa; u.to = p.sb; u.size = 100;
	const int uid = u.id;
	const auto evs = capture(w, [&] { society::onArrive(w, uid); });
	eq(countEv(evs, Ev::Battle), 0, "no war, no battle");
	ok(state::findUnit(w, uid) == nullptr, "disbanded");
}

DM_TEST("diplomacy: exhaustion rises as documented and peace follows (exhaustion, 60 years, 40 %, empty side)") {
	stub::Guard guard(true);
	if (!fakesOk()) return;
	installFakes();
	// weariness 0.015 per year, peace at 1 with the other side as victor
	{
		Pair p = makePair(5, 0.8, 0.3);
		World& w = *p.w;
		society::declareWar(w, p.a, p.b);
		for (int y = 1; y <= 10; y++) { w.year = y; society::diplomacy(w); }   // armies never arrive here
		ok(society::atWar(w, p.a, p.b), "still at war after 10 years");
		const double exA = w.cultures[size_t(p.a)].exhaustion, exB = w.cultures[size_t(p.b)].exhaustion;
		near(exA, 0.15, 1e-9, "A: 0.015 per year");
		near(exB, 0.15, 1e-9, "B: 0.015 per year");
		// remove the armies' effect: set A just below 1
		w.cultures[size_t(p.a)].exhaustion = 0.99;
		w.cultures[size_t(p.b)].exhaustion = 0.2;
		w.year = 11;
		const auto evs = capture(w, [&] { society::diplomacy(w); });
		const Relation* r = society::peek(w, p.a, p.b);
		ok(!r->war, "peace at exhaustion 1");
		bool seen = false;
		for (const Event& e : evs)
			if (const EvWarEnded* we = e.as<EvWarEnded>()) { seen = true; eq(we->a, p.a, "a = aggressor"); eq(we->b, p.b, "b"); eq(we->victor, p.b, "victor = the unwearied side"); }
		ok(seen, "war:ended");
		near(r->v, -0.1, 1e-12, "v = -0.1");
		near(r->truce, 51, 1e-9, "truce 40 years");
		// weariness fades 0.03 per year in peace
		const double ex0 = w.cultures[size_t(p.b)].exhaustion;
		w.year = 12;
		society::diplomacy(w);
		near(w.cultures[size_t(p.b)].exhaustion, ex0 - 0.03, 1e-12, "decay 0.03/yr");
		// no new war during the truce even when hostile
		for (int y = 13; y < 51; y++) { w.year = y; society::rel(w, p.a, p.b)->v = -0.9; society::diplomacy(w); arrive(w, true); }
		eq(w.stats.wars, 1, "no war before the truce ends");
		w.year = 51;
		society::rel(w, p.a, p.b)->v = -0.9;
		society::diplomacy(w);
		eq(w.stats.wars, 2, "war again once the truce is over");
	}
	// the 60-year cap: a draw when neither took anything
	{
		Pair p = makePair(5, 0.8, 0.3);
		World& w = *p.w;
		society::declareWar(w, p.a, p.b);
		w.year = 60;
		const auto evs = capture(w, [&] { society::diplomacy(w); });
		ok(!society::atWar(w, p.a, p.b), "60 years: peace");
		for (const Event& e : evs) if (const EvWarEnded* we = e.as<EvWarEnded>()) eq(we->victor, -1, "draw");
	}
	// a side that lost 40 % of its settlements
	{
		Pair p = makePair(5, 0.8, 0.3);
		World& w = *p.w;
		std::vector<int32_t> tmp;
		for (int k = 0; k < 4; k++) {   // B gets 5 settlements
			const int c = siteAt(w, w.settlements[size_t(p.sb)].cell, 6 + 5 * k, 1);
			if (c >= 0) addTown(w, c, p.b, 300, 1);
		}
		int nb = 0;
		for (const Settlement& s : w.settlements) if (s.culture == p.b) nb++;
		society::declareWar(w, p.a, p.b);
		w.year = 1;
		w.vars.num["diplomacy_w0_1_lost1"] = std::ceil(0.4 * nb) - 1;
		society::diplomacy(w);
		ok(society::atWar(w, p.a, p.b), "below 40 %: war goes on");
		w.year = 2;
		w.vars.num["diplomacy_w0_1_lost1"] = std::ceil(0.4 * nb);
		const auto evs = capture(w, [&] { society::diplomacy(w); });
		ok(!society::atWar(w, p.a, p.b), "40 % lost: peace (" + std::to_string(nb) + " settlements at war start)");
		for (const Event& e : evs) if (const EvWarEnded* we = e.as<EvWarEnded>()) eq(we->victor, p.a, "victor A");
		ok(w.vars.num.find("diplomacy_w0_1_lost1") == w.vars.num.end(), "war numbers cleared");
	}
	// the other side has no live settlement left
	{
		Pair p = makePair(5, 0.8, 0.3);
		World& w = *p.w;
		society::declareWar(w, p.a, p.b);
		fakeDestroy(w, p.sb, Cause::War);
		w.year = 3;
		const auto evs = capture(w, [&] { society::diplomacy(w); });
		ok(!society::atWar(w, p.a, p.b), "empty side: peace");
		for (const Event& e : evs) if (const EvWarEnded* we = e.as<EvWarEnded>()) eq(we->victor, p.a, "victor A");
	}
}

DM_TEST("diplomacy: alliances (friends of the victim sour on the aggressor) and the crusade edict") {
	Pair p = makePair(5, 0.4, 0.6);
	World& w = *p.w;
	const int c = addCulture(w, 0.5);
	const int far = siteAt(w, w.settlements[size_t(p.sa)].cell, 12);
	if (!ok(far >= 0, "site")) return;
	addTown(w, far, c, 1000);
	society::rel(w, c, p.b)->v = 0.6;     // C is B's friend
	society::rel(w, c, p.a)->v = 0.2;
	society::declareWar(w, p.a, p.b);
	near(society::rel(w, c, p.a)->v, 0.05, 1e-12, "the victim's friend sours on the aggressor (-0.15)");
	near(society::rel(w, c, p.b)->v, 0.6, 0, "and keeps its friendship");
	society::makePeace(w, p.a, p.b);
	// crusade: A (aggression 0.4) turns on the least devout culture in contact (B, devotion 0.1; C 0.9)
	Pair q = makePair(5, 0.4, 0.6);
	World& x = *q.w;
	x.tune<DiplomacyTune>().noise = 0;
	const int cc = addCulture(x, 0.1);
	const int f2 = siteAt(x, x.settlements[size_t(q.sa)].cell, 12);
	if (f2 < 0) return;
	addTown(x, f2, cc, 1000);
	x.settlements[size_t(q.sb)].devotion = 0.1;
	x.settlements[size_t(x.settlements.size() - 1)].devotion = 0.9;
	x.settlements[size_t(q.sa)].devotion = 0.5;
	society::rel(x, q.a, q.b)->v = -0.9;
	society::rel(x, q.a, cc)->v = 0.5;
	society::rel(x, q.b, cc)->v = 0.5;
	x.cultures[size_t(q.a)].edict = Edict::Crusade;
	society::diplomacy(x);
	ok(society::atWar(x, q.a, q.b), "war");
	eq(society::peek(x, q.a, q.b)->aggressor, q.a, "the crusader attacks (0.4 + 0.4 > 0.6)");
	ok(!society::atWar(x, q.a, cc), "not the devout neighbour");
	// without the edict the more aggressive B is the aggressor
	Pair r = makePair(5, 0.4, 0.6);
	r.w->tune<DiplomacyTune>().noise = 0;
	society::rel(*r.w, r.a, r.b)->v = -0.9;
	society::diplomacy(*r.w);
	eq(society::peek(*r.w, r.a, r.b)->aggressor, r.b, "no edict: B attacks");
}

DM_TEST("diplomacy: no war without the means to wage it (deep water between them)") {
	Pair p = makePair(5, 0.9, 0.9);
	World& w = *p.w;
	w.tune<DiplomacyTune>().noise = 0;
	for (int c = 0; c < w.n; c++) if (w.owner[size_t(c)] == p.sb) w.cache.landId[size_t(c)] = 999;   // pretend: another landmass
	w.cache.landId[size_t(w.settlements[size_t(p.sb)].cell)] = 999;
	society::rel(w, p.a, p.b)->v = -0.9;
	society::diplomacy(w);
	ok(!society::atWar(w, p.a, p.b), "Stone age, another landmass: no war");
	w.cultures[size_t(p.a)].age = Age::BRONZE;
	w.cultures[size_t(p.b)].age = Age::BRONZE;
	society::diplomacy(w);
	ok(society::atWar(w, p.a, p.b), "Bronze age, a narrow strait (touching territories): war");
}

DM_TEST("diplomacy: makePeace works at any time and sets the truce") {
	Pair p = makePair(5);
	World& w = *p.w;
	w.year = 100;
	Relation* r = society::rel(w, p.a, p.b);
	r->v = 0.4;
	auto evs = capture(w, [&] { society::makePeace(w, p.a, p.b); });
	r = society::rel(w, p.a, p.b);
	eq(countEv(evs, Ev::WarEnded), 0, "not at war: no event");
	near(r->truce, 140, 1e-9, "truce set");
	near(r->v, 0.4, 0, "a friendly relation keeps its v");
	society::declareWar(w, p.b, p.a);
	eq(society::rel(w, p.a, p.b)->aggressor, p.b, "aggressor");
	w.year = 105;
	evs = capture(w, [&] { society::makePeace(w, p.a, p.b, 80, 77); });
	r = society::rel(w, p.a, p.b);
	ok(!r->war, "ended");
	near(r->truce, 185, 1e-9, "custom truce");
	near(r->v, -0.1, 1e-12, "v -0.1");
	for (const Event& e : evs) if (const EvWarEnded* we = e.as<EvWarEnded>()) { eq(we->a, p.b, "a = the aggressor"); eq(we->victor, -1, "bad victor -> draw"); }
	eq(countEv(evs, Ev::WarEnded), 1, "one event");
	// bad input
	society::makePeace(w, -1, 3);
	society::makePeace(w, p.a, p.a);
	society::makePeace(w, p.a, p.b, std::nan(""), 0);
	near(society::rel(w, p.a, p.b)->truce, 185, 1e-9, "NaN truce years -> default 40 (145 < 185 keeps 185)");
	society::declareWar(w, p.a, p.a);
	society::declareWar(w, -1, 0);
	society::declareWar(w, p.a, 42);
	eq(w.stats.wars, 1, "bad declarations do nothing");
	noNaN(w);
}

// ================================================================ trade

DM_TEST("diplomacy: trade raises trade, relations and roads; trade:route once per pair; plague may ride along") {
	stub::Guard guard(true);
	if (!fakesOk()) return;
	installFakes();
	Pair p = makePair(14, 0.5, 0.5, 6, 3);
	World& w = *p.w;
	if (!ok(p.sb >= 0, "sites")) return;
	Settlement& m = w.settlements[size_t(p.sa)];
	m.tier = 2;
	m.b[Building::MARKET] = 1;
	w.settlements[size_t(p.sb)].tier = 1;
	std::fill(w.road.begin(), w.road.end(), uint8_t(0));
	w.dirty.roads = false;
	// cadence: one caravan about every 5 years
	std::vector<int> years;
	for (int y = 1; y <= 31; y++) {
		w.year = y;
		const size_t before = w.units.size();
		society::diplomacy(w);
		for (size_t k = before; k < w.units.size(); k++)
			if (w.units[k].type == UnitType::Trader) { years.push_back(y); eq(w.units[k].from, p.sa, "from the market town"); eq(w.units[k].to, p.sb, "to the partner"); }
	}
	std::printf("    caravans left in years %s\n", show(years).c_str());
	ok(years.size() == 6, "six caravans in 31 years");
	for (size_t k = 1; k < years.size(); k++) eq(years[k] - years[k - 1], 5, "every 5 years");
	// arrivals
	Relation* r = society::rel(w, p.a, p.b);
	r->trade = 0;
	const double v0 = r->v;
	const int tid = w.units[0].id;
	const std::vector<int32_t> route = w.units[0].path;
	auto evs = capture(w, [&] { society::onArrive(w, tid); });
	r = society::rel(w, p.a, p.b);
	near(r->trade, w.tune<DiplomacyTune>().tradeStep, 1e-12, "trade toward 1");
	near(r->v, v0 + 0.02, 1e-12, "relations up");
	eq(countEv(evs, Ev::TradeRoute), 1, "first caravan: trade:route");
	for (const Event& e : evs) if (const EvTradeRoute* tr = e.as<EvTradeRoute>()) { eq(tr->a, p.sa, "a"); eq(tr->b, p.sb, "b"); }
	state::eraseUnit(w, tid);
	int caravans = 1;
	for (; caravans < 40; caravans++) {
		Unit& u = state::newUnit(w, UnitType::Trader);
		u.culture = p.a; u.from = p.sa; u.to = p.sb; u.path = route;
		const int id = u.id;
		evs = capture(w, [&] { society::onArrive(w, id); });
		state::eraseUnit(w, id);
		if (countEv(evs, Ev::TradeRoute) != 0) { fail("trade:route only once"); break; }
	}
	r = society::rel(w, p.a, p.b);
	ok(r->trade > 0.99, "trade saturates toward 1");
	double sum = 0;
	int land = 0, seaRoad = 0, capped = 0;
	for (const int c : route) {
		if (seaC(w, c)) { if (w.road[size_t(c)] != 0) seaRoad++; continue; }
		land++;
		sum += w.road[size_t(c)];
		if (w.road[size_t(c)] > 3) capped++;
	}
	std::printf("    roads after 40 caravans: %d land cells, mean level %.2f\n", land, land ? sum / land : 0.0);
	ok(land > 0 && sum / land > 1.5 && sum / land <= 3, "roads rise with traffic");
	eq(capped, 0, "cap 3");
	eq(seaRoad, 0, "no roads at sea");
	ok(w.dirty.roads, "dirty.roads");
	// plague: P = 0.5 x origin plague
	w.settlements[size_t(p.sa)].plague = 1.0;
	int infected = 0;
	for (int k = 0; k < 200; k++) {
		w.settlements[size_t(p.sb)].plague = 0;
		Unit& u = state::newUnit(w, UnitType::Trader);
		u.culture = p.a; u.from = p.sa; u.to = p.sb; u.path = route;
		const int id = u.id;
		const int before = stub::called("civ_infect");
		society::onArrive(w, id);
		state::eraseUnit(w, id);
		if (stub::called("civ_infect") > before) infected++;
	}
	std::printf("    plague hopped on %d of 200 caravans (expect ~100)\n", infected);
	ok(infected > 70 && infected < 130, "plague may hop");
	// a domestic caravan: no relation change, but a route event for the new pair
	const int own = addTown(w, siteAt(w, w.settlements[size_t(p.sa)].cell, 8, 1), p.a, 300, 1);
	const double tradeBefore = society::rel(w, p.a, p.b)->trade;
	Unit& u = state::newUnit(w, UnitType::Trader);
	u.culture = p.a; u.from = p.sa; u.to = own; u.path = { w.settlements[size_t(p.sa)].cell };
	const int id = u.id;
	evs = capture(w, [&] { society::onArrive(w, id); });
	eq(countEv(evs, Ev::TradeRoute), 1, "domestic pair: route event");
	near(society::rel(w, p.a, p.b)->trade, tradeBefore, 0, "no foreign trade change");
	noNaN(w);
}

DM_TEST("diplomacy: no caravans to enemies; trade links count as contact") {
	stub::Guard guard(true);
	if (!fakesOk()) return;
	installFakes();
	Pair p = makePair(14);
	World& w = *p.w;
	if (p.sb < 0) return;
	w.settlements[size_t(p.sa)].tier = 2;
	w.settlements[size_t(p.sa)].b[Building::MARKET] = 1;
	w.settlements[size_t(p.sb)].tier = 1;
	society::declareWar(w, p.a, p.b);
	w.cultures[size_t(p.a)].exhaustion = 0;
	for (int y = 1; y <= 12; y++) {
		w.year = y;
		society::diplomacy(w);
		w.cultures[size_t(p.a)].exhaustion = 0;
		w.cultures[size_t(p.b)].exhaustion = 0;
	}
	int traders = 0;
	for (const Unit& u : w.units) if (u.type == UnitType::Trader) traders++;
	eq(traders, 0, "no caravans to the enemy");
	// a far pair (out of territorial contact) linked by trade stays in contact
	Pair q = makePair(24);
	if (q.sb < 0) return;
	society::rel(*q.w, q.a, q.b)->trade = 0.5;
	society::diplomacy(*q.w);
	ok(society::peek(*q.w, q.a, q.b)->contact, "trade link: contact");
}

// ================================================================ nukes

DM_TEST("diplomacy: nuclear exchange (two strikes per side, rad, dust, lastNuke, nuke events, 100-year truce)") {
	stub::Guard guard(true);
	if (!fakesOk()) return;
	installFakes();
	auto w = makeWorld({ .L = 6, .seed = 2, .land = 0.45 });
	const int c0 = bestHab(*w);
	const int a = addCulture(*w, 0.8, Age::ATOMIC), b = addCulture(*w, 0.3, Age::ATOMIC);
	std::vector<int> sa, sb;
	for (int k = 0; k < 3; k++) sa.push_back(addTown(*w, k == 0 ? c0 : siteAt(*w, c0, 12 * k, 2), a, 5000.0 + 1000 * k));
	for (int k = 0; k < 3; k++) sb.push_back(addTown(*w, siteAt(*w, c0, 9 + 12 * k, 2), b, 3000.0 + 2000 * k));
	for (const int s : sa) if (!ok(w->settlements[size_t(s)].cell >= 0, "site")) return;
	for (const int s : sb) if (!ok(w->settlements[size_t(s)].cell >= 0, "site")) return;
	w->tune<DiplomacyTune>().nukeP = 1;
	w->year = 2000;
	society::declareWar(*w, a, b);
	std::fill(w->rad.begin(), w->rad.end(), 0.0f);
	stub::calls().clear();
	const auto evs = capture(*w, [&] { society::diplomacy(*w); });
	eq(countEv(evs, Ev::Nuke), 4, "4 strikes");
	std::vector<int> hitA, hitB;
	for (const Event& e : evs)
		if (const EvNuke* nk = e.as<EvNuke>()) {
			const int sid = civ::at(*w, nk->cell);
			if (nk->from == a) { eq(nk->to, b, "to"); hitB.push_back(sid); } else { eq(nk->from, b, "from"); eq(nk->to, a, "to"); hitA.push_back(sid); }
		}
	std::sort(hitA.begin(), hitA.end());
	std::sort(hitB.begin(), hitB.end());
	ok(hitB == std::vector<int>{ sb[1], sb[2] }, "A strikes B's two largest: " + show(hitB));
	ok(hitA == std::vector<int>{ sa[1], sa[2] }, "B strikes A's two largest: " + show(hitA));
	eq(stub::called("civ_damage"), 4, "civ::damage x4");
	eq(stub::called("climate_addDust"), 4, "dust x4");
	eq(stub::called("society_witness"), 4, "witness x4");
	ok(stub::called("life_ignite") >= 4, "fires");
	near(w->settlements[size_t(sb[2])].pop, 7000 * 0.3, 1e-6, "70 % killed");
	std::vector<int32_t> tmp;
	w->geo->ring(w->settlements[size_t(sb[2])].cell, 2, tmp);
	bool hot = true;
	for (const int c : tmp) if (w->rad[size_t(c)] < 0.8f - 1e-6f) hot = false;
	ok(hot, "rad 0.8 within 2 hops");
	ok(w->rad[size_t(w->settlements[size_t(sa[0])].cell)] == 0.0f, "the spared town stays clean");
	ok(w->vars.lastNuke && *w->vars.lastNuke == 2000, "vars.lastNuke");
	const Relation* r = society::peek(*w, a, b);
	ok(!r->war, "immediate peace");
	near(r->truce, 2100, 1e-9, "100-year truce");
	mi::field(*w, "rad", shot("ue-diplomacy-rad.png"), 0, 1, { .dimSea = true });
	// only one side Atomic: never
	auto w2 = makeWorld({ .L = 5, .seed = 2 });
	const int x = addCulture(*w2, 0.8, Age::ATOMIC), y = addCulture(*w2, 0.3, Age::INDUSTRY);
	addTown(*w2, bestHab(*w2), x, 1000);
	addTown(*w2, siteAt(*w2, bestHab(*w2), 5), y, 1000);
	w2->tune<DiplomacyTune>().nukeP = 1;
	society::declareWar(*w2, x, y);
	const auto evs2 = capture(*w2, [&] { society::diplomacy(*w2); });
	eq(countEv(evs2, Ev::Nuke), 0, "Industry side: no bomb");
	noNaN(*w);
}

DM_TEST("diplomacy: nuclear risk is 1.5 % per war-year and at most once per war") {
	stub::Guard guard(false);
	if (!fakesOk()) return;
	installFakes();
	Pair p = makePair(5, 0.8, 0.3);
	World& w = *p.w;
	w.cultures[size_t(p.a)].age = Age::ATOMIC;
	w.cultures[size_t(p.b)].age = Age::SPACE;
	int exchanges = 0, warYears = 0, wars = 0, maxPerWar = 0, thisWar = 0;
	for (int y = 0; y < 4000; y++) {
		w.year = y;
		if (!society::atWar(w, p.a, p.b)) {
			Relation* r = society::rel(w, p.a, p.b);
			r->truce = 0;
			society::declareWar(w, p.a, p.b);
			wars++;
			maxPerWar = std::max(maxPerWar, thisWar);
			thisWar = 0;
		}
		society::rel(w, p.a, p.b)->since = y;               // never the 60-year cap
		w.cultures[size_t(p.a)].exhaustion = 0;
		w.cultures[size_t(p.b)].exhaustion = 0;
		for (Settlement& s : w.settlements) { s.pop = 1000; s.dead = false; }
		warYears++;
		const auto evs = capture(w, [&] { society::diplomacy(w); });
		const int n = countEv(evs, Ev::Nuke);
		if (n) { exchanges++; thisWar++; eq(n, 2, "one city each side here"); }
		w.units.clear();                                     // no battles: only the bomb is measured
	}
	std::printf("    %d exchanges in %d war-years (expect %.0f), %d wars\n", exchanges, warYears, 0.015 * warYears, wars);
	ok(exchanges >= 38 && exchanges <= 85, "about 1.5 % per year");
	ok(maxPerWar <= 1, "at most once per war");
}

// ================================================================ budget, determinism, robustness

DM_TEST("diplomacy: at most 4 path searches per year (armies first, then traders)") {
	stub::Guard guard(true);
	if (!fakesOk()) return;
	installFakes();
	auto w = makeWorld({ .L = 6, .seed = 5, .land = 0.45 });
	Rng tr(9);
	std::vector<int> cultures;
	for (int k = 0; k < 6; k++) cultures.push_back(addCulture(*w, 0.5, Age::IRON));
	std::vector<int32_t> tmp;
	int placed = 0;
	for (int c = 0; c < w->n && placed < 40; c += 37) {
		if (seaC(*w, c) || w->hab[size_t(c)] < 0.2f) continue;
		w->geo->ring(c, 5, tmp);
		bool free = true;
		for (const int m : tmp) if (w->owner[size_t(m)] >= 0) { free = false; break; }
		if (!free) continue;
		const int s = addTown(*w, c, cultures[size_t(placed % 6)], 500 + 50 * placed, 2, 2);
		w->settlements[size_t(s)].b[Building::MARKET] = 1;
		placed++;
	}
	for (int a = 0; a < 6; a++) for (int b = a + 1; b < 6; b++) if ((a + b) % 2) society::declareWar(*w, a, b);
	int maxCalls = 0, armies = 0, traders = 0;
	for (int y = 1; y <= 40; y++) {
		w->year = y;
		for (Culture& c : w->cultures) c.exhaustion = 0;
		for (auto& kv : w->vars.rel) kv.second.since = y;
		stub::calls().clear();
		const size_t before = w->units.size();
		society::diplomacy(*w);
		maxCalls = std::max(maxCalls, stub::called("path_find"));
		for (size_t k = before; k < w->units.size(); k++) (w->units[k].type == UnitType::Army ? armies : traders)++;
		arrive(*w, true);
	}
	std::printf("    %d settlements, 40 years: %d armies, %d caravans, at most %d searches in a year\n", placed, armies, traders, maxCalls);
	ok(maxCalls <= 4, "never more than 4 searches");
	ok(armies + traders > 4 * 40 / 2, "the route cache carries most of the traffic");
	ok(traders > 0 && armies > 0, "both kinds sent");
}

DM_TEST("diplomacy: deterministic, no NaN, tolerant of bad input") {
	stub::Guard guard(false);
	auto run = [](uint32_t seed) -> uint32_t {
		installFakes();
		auto w = makeWorld({ .L = 5, .seed = seed, .land = 0.45 });
		const int c0 = bestHab(*w);
		const int a = addCulture(*w, 0.8), b = addCulture(*w, 0.7);
		addTown(*w, c0, a, 1500);
		addTown(*w, siteAt(*w, c0, 5), b, 1200);
		addTown(*w, siteAt(*w, c0, 10), b, 600);
		for (int y = 0; y < 300; y++) {
			w->year = y;
			society::diplomacy(*w);
			arrive(*w);
			for (Settlement& s : w->settlements) if (!s.dead) s.pop += 0.03 * s.pop * (1 - s.pop / 1500);
		}
		noNaN(*w, "300 years");
		eq(checkFinite(*w), std::string(""), "finite");
		return hash(*w);
	};
	if (fakesOk()) {
		const uint32_t h1 = run(4), h2 = run(4), h3 = run(5);
		eq(h1, h2, "same seed, same world");
		ok(h1 != h3, "another seed differs");
	}
	// bad input on a bare world
	auto w = state::create({ .seed = 1, .L = 2 });
	watch(*w);
	society::diplomacy(*w);
	society::onArrive(*w, -5);
	society::onArrive(*w, 12345);
	addCulture(*w, 0.5);
	addCulture(*w, 0.5);
	society::diplomacy(*w);
	Unit& u = state::newUnit(*w, UnitType::Army);
	u.culture = 7; u.to = 99;
	society::onArrive(*w, u.id);
	Unit& v = state::newUnit(*w, UnitType::Settler);
	society::onArrive(*w, v.id);
	Unit& t = state::newUnit(*w, UnitType::Trader);
	t.culture = 0; t.from = -1; t.to = 3;
	society::onArrive(*w, t.id);
	w->cultures[1].dead = true;
	society::rel(*w, 0, 1)->war = true;
	society::diplomacy(*w);
	ok(!society::atWar(*w, 0, 1), "a war with a dead culture ends");
	noNaN(*w);
}

DM_TEST("diplomacy: a prophet's arrival raises devotion and moves him") {
	Pair p = makePair(5);
	World& w = *p.w;
	const int home2 = addTown(w, siteAt(w, w.settlements[size_t(p.sa)].cell, 9, 1), p.a, 400, 1);
	Culture& c = w.cultures[size_t(p.a)];
	c.prophet = Prophet{ "Asha", p.sa, 300 };
	w.settlements[size_t(home2)].devotion = 0.4;
	Unit& u = state::newUnit(w, UnitType::Prophet);
	u.culture = p.a; u.from = p.sa; u.to = home2;
	society::onArrive(w, u.id);
	near(w.settlements[size_t(home2)].devotion, 0.55, 1e-12, "+0.15");
	eq(w.cultures[size_t(p.a)].prophet->settlement, home2, "he lives there now");
	Unit& f = state::newUnit(w, UnitType::Prophet);
	f.culture = p.a; f.to = p.sb;
	const double before = w.settlements[size_t(p.sb)].devotion;
	society::onArrive(w, f.id);
	near(w.settlements[size_t(p.sb)].devotion, before, 0, "a foreign town is not his flock");
}

// ================================================================ long runs

namespace {

struct Timeline {
	std::vector<std::string> lines;
	int declared = 0, ended = 0, battles = 0, won = 0, routes = 0, nukes = 0;
	void take(const World& w, const std::vector<Event>& evs) {
		char buf[200];
		for (const Event& e : evs) {
			if (const EvWarDeclared* d = e.as<EvWarDeclared>()) {
				declared++;
				std::snprintf(buf, sizeof buf, "  year %4.0f  war: C%d attacks C%d", w.year, d->a, d->b);
				lines.push_back(buf);
			} else if (const EvWarEnded* x = e.as<EvWarEnded>()) {
				ended++;
				std::snprintf(buf, sizeof buf, "  year %4.0f  peace: C%d / C%d, victor %d", w.year, x->a, x->b, x->victor);
				lines.push_back(buf);
			} else if (const EvBattle* bt = e.as<EvBattle>()) {
				battles++;
				if (bt->won) won++;
				std::snprintf(buf, sizeof buf, "  year %4.0f    battle at T%d: C%d vs C%d, %s", w.year, bt->settlement, bt->attacker, bt->defender, bt->won ? "taken" : "repulsed");
				lines.push_back(buf);
			} else if (e.as<EvTradeRoute>()) {
				routes++;
			} else if (e.as<EvNuke>()) {
				nukes++;
			}
		}
	}
};

void growAll(World& w, const std::map<int, double>& K) {
	for (Settlement& s : w.settlements) {
		if (s.dead) continue;
		const auto it = K.find(s.id);
		const double k = it == K.end() ? 1000 : it->second;
		s.pop = std::max(3.0, s.pop + 0.03 * s.pop * (1 - s.pop / k));
	}
}

// a land cell exactly k hops from `from` (ring of rad all land) at least minAvoid hops from every cell in avoid
int siteNear(const World& w, int from, int k, const std::vector<int>& avoid, int minAvoid, int rad = 3) {
	if (from < 0) return -1;
	std::vector<std::vector<int>> dav;
	for (const int c : avoid) dav.push_back(hops(w, c));
	const std::vector<int> d = hops(w, from);
	std::vector<int32_t> tmp;
	for (int c = 0; c < w.n; c++) {
		if (d[size_t(c)] != k || seaC(w, c) || w.hab[size_t(c)] < 0.1f) continue;
		bool okc = true;
		for (const auto& da : dav) if (da[size_t(c)] < minAvoid) { okc = false; break; }
		if (!okc) continue;
		w.geo->ring(c, rad, tmp);
		for (const int m : tmp) if (seaC(w, m)) { okc = false; break; }
		if (okc) return c;
	}
	return -1;
}

// the fake Civ's territory rule: radius by tier (2..6), claimed in id order
void reclaim(World& w) {
	std::fill(w.owner.begin(), w.owner.end(), int16_t(-1));
	std::vector<int32_t> tmp;
	for (Settlement& s : w.settlements) {
		s.cells.clear();
		if (s.dead) continue;
		s.radius = 2 + std::clamp(s.tier, 0, 4);
		w.geo->ring(s.cell, s.radius, tmp);
		for (const int c : tmp) {
			if (seaC(w, c) || w.owner[size_t(c)] >= 0) continue;
			w.owner[size_t(c)] = int16_t(s.id);
			s.cells.push_back(c);
		}
	}
}

} // namespace

DM_TEST("diplomacy: two aggressive neighbours over 600 years (timeline)") {
	stub::Guard guard(false);
	if (!fakesOk()) return;
	installFakes();
	auto w = makeWorld({ .L = 6, .seed = 2, .land = 0.45 });
	const int c0 = bestHab(*w);
	const int a = addCulture(*w, 0.8), b = addCulture(*w, 0.75);
	w->cultures[size_t(a)].name = "Umar";
	w->cultures[size_t(b)].name = "Vesh";
	std::map<int, double> K;
	// a chain of six towns 9 hops apart (radius 4: neighbours touch): Umar, Umar, Umar, Vesh, Vesh, Vesh
	std::vector<int> chain{ c0 };
	while (chain.size() < 6) {
		std::vector<int> avoid(chain.begin(), chain.end() - 1);
		const int c = siteNear(*w, chain.back(), 9, avoid, 12, 2);
		if (c < 0) break;
		chain.push_back(c);
	}
	if (!ok(chain.size() == 6, "a chain of six sites")) return;
	for (size_t k = 0; k < chain.size(); k++) { const int s = addTown(*w, chain[k], k < 3 ? a : b, 800, 4); K[s] = 1200; }
	mi::owner(*w, shot("ue-diplomacy-pair-0.png"));
	Timeline tl;
	int warYears = 0;
	double minV = 1, maxV = -1;
	for (int y = 1; y <= 600; y++) {
		w->year = y;
		const auto evs = capture(*w, [&] { society::diplomacy(*w); arrive(*w); });
		tl.take(*w, evs);
		growAll(*w, K);
		const Relation* r = society::peek(*w, a, b);
		if (r) { minV = std::min(minV, r->v); maxV = std::max(maxV, r->v); if (r->war) warYears++; }
		if (y == 300) mi::owner(*w, shot("ue-diplomacy-pair-300.png"));
	}
	mi::owner(*w, shot("ue-diplomacy-pair-600.png"));
	std::printf("    timeline (two aggressive neighbours, aggression 0.8 / 0.75):\n");
	for (const std::string& l : tl.lines) if (l.find("battle") == std::string::npos) std::printf("%s\n", l.c_str());
	int townsA = 0, townsB = 0;
	for (const Settlement& s : w->settlements) if (!s.dead) (s.culture == a ? townsA : townsB)++;
	std::printf("    %d wars declared, %d ended, %d battles (%d taken), %d of 600 years at war, v in [%.2f, %.2f]; towns now %d / %d\n",
		tl.declared, tl.ended, tl.battles, tl.won, warYears, minV, maxV, townsA, townsB);
	ok(tl.declared >= 1 && tl.ended >= 1, "at least one war starts and ends");
	ok(tl.battles >= 1, "armies met");
	ok(warYears < 450, "not one endless war");
	noNaN(*w);
}

DM_TEST("diplomacy: six cultures over 2,000 years (wars rare enough to matter, trade, alliances)") {
	stub::Guard guard(false);
	if (!fakesOk()) return;
	installFakes();
	auto w = makeWorld({ .L = 6, .seed = 7, .land = 0.42 });
	Rng tr(31);
	// six culture seeds spread over the land, settlements to the nearest seed
	std::vector<int> land;
	for (int c = 0; c < w->n; c++) if (!seaC(*w, c) && w->hab[size_t(c)] > 0.15f) land.push_back(c);
	std::vector<int> seeds;
	while (seeds.size() < 6 && !land.empty()) {
		const int c = land[size_t(tr.integer(0, int(land.size()) - 1))];
		bool farEnough = true;
		for (const int s : seeds) if (w->geo->angle(s, c) < 0.5) farEnough = false;
		if (farEnough) seeds.push_back(c);
		if (tr.f() < 0.001) break;
	}
	std::vector<int> cu;
	for (size_t k = 0; k < seeds.size(); k++) {
		const double aggr = std::clamp(0.5 + 0.35 * (tr.f() - tr.f()), 0.0, 1.0);
		cu.push_back(addCulture(*w, aggr));
	}
	std::map<int, double> K;
	std::vector<int32_t> tmp;
	for (int c = 0; c < w->n; c += 7) {
		if (seaC(*w, c) || w->hab[size_t(c)] < 0.25f) continue;
		w->geo->ring(c, 6, tmp);
		bool free = true;
		for (const int m : tmp) if (w->owner[size_t(m)] >= 0) { free = false; break; }
		if (!free) continue;
		size_t best = 0;
		for (size_t k = 1; k < seeds.size(); k++) if (w->geo->angle(c, seeds[k]) < w->geo->angle(c, seeds[best])) best = k;
		if (w->geo->angle(c, seeds[best]) > 0.6) continue;
		const int s = addTown(*w, c, cu[best], 300, 3);
		K[s] = 400 + 2600 * tr.f();
		w->settlements[size_t(s)].love = std::clamp(0.3 * (tr.f() - tr.f()) + (best % 3 == 0 ? 0.2 : -0.1), -1.0, 1.0);
	}
	std::printf("    %zu cultures, %zu settlements; aggression:", cu.size(), w->settlements.size());
	for (const int c : cu) std::printf(" %.2f", w->cultures[size_t(c)].traits.aggression);
	std::printf("\n");
	mi::owner(*w, shot("ue-diplomacy-six-0.png"));
	Timeline tl;
	int contactPairYears = 0, warPairYears = 0, allyPairYears = 0;
	std::map<std::pair<int, int>, int> warLen;
	std::vector<int> lengths;
	std::printf("    century  wars  armies  battles  routes  allied  hostile  meanV  towns\n");
	int lastBattles = 0, lastRoutes = 0, armiesSent = 0, lastArmies = 0;
	for (int y = 1; y <= 2000; y++) {
		w->year = y;
		const int age = std::min(int(Age::SAIL), y / 500);
		for (Culture& c : w->cultures) c.age = age;
		for (Settlement& s : w->settlements) {
			if (s.dead) continue;
			s.tier = s.pop >= 1500 ? 3 : s.pop >= 400 ? 2 : s.pop >= 60 ? 1 : 0;
			if (age >= Age::IRON && s.tier >= 2) s.b[Building::MARKET] = 1;
			if (age >= Age::BRONZE && s.tier >= 2) s.b[Building::WALLS] = 1;
		}
		if (y % 10 == 1) reclaim(*w);   // territories grow with the towns
		const auto evs = capture(*w, [&] { society::diplomacy(*w); for (const Unit& u : w->units) if (u.type == UnitType::Army && u.born == w->year) armiesSent++; arrive(*w); });
		tl.take(*w, evs);
		for (const Event& e : evs) if (const EvWarEnded* x = e.as<EvWarEnded>()) {
			const auto key = state::relKey(x->a, x->b);
			lengths.push_back(warLen[key]);
			warLen[key] = 0;
		}
		growAll(*w, K);
		int wars = 0, allied = 0, hostile = 0, pairs = 0;
		double sumV = 0;
		for (const auto& kv : w->vars.rel) {
			if (!kv.second.contact) continue;
			pairs++;
			sumV += kv.second.v;
			if (kv.second.war) { wars++; warLen[kv.first]++; }
			if (kv.second.v >= 0.5) allied++;
			if (kv.second.v < -0.5) hostile++;
		}
		contactPairYears += pairs;
		warPairYears += wars;
		allyPairYears += allied;
		if (y % 100 == 0) {
			int towns = 0;
			for (const Settlement& s : w->settlements) if (!s.dead) towns++;
			std::printf("    %7d  %4d  %7d  %7d  %6d  %6d  %7d  %5.2f  %5d\n", y, wars, armiesSent - lastArmies, tl.battles - lastBattles, tl.routes - lastRoutes, allied, hostile, pairs ? sumV / pairs : 0.0, towns);
			lastArmies = armiesSent;
			lastBattles = tl.battles;
			lastRoutes = tl.routes;
		}
		if (y == 1000) mi::owner(*w, shot("ue-diplomacy-six-1000.png"));
	}
	mi::owner(*w, shot("ue-diplomacy-six-2000.png"));
	mi::custom(*w, shot("ue-diplomacy-six-roads.png"), [&](int c) {
		if (seaC(*w, c)) return mi::Rgb{ 20, 40, 80 };
		const int r = w->road[size_t(c)];
		if (r > 0) return mi::Rgb{ 120.0 + 45 * r, 90.0 + 30 * r, 40 };
		const int o = w->owner[size_t(c)];
		if (o >= 0) { const auto& col = w->cultures[size_t(w->settlements[size_t(o)].culture)].color; return mi::Rgb{ 60 + 80 * col[0], 60 + 80 * col[1], 60 + 80 * col[2] }; }
		return mi::Rgb{ 70, 80, 60 };
	});
	double meanLen = 0;
	for (const int l : lengths) meanLen += l;
	if (!lengths.empty()) meanLen /= double(lengths.size());
	const double warShare = contactPairYears ? double(warPairYears) / contactPairYears : 0;
	std::printf("    summary: %d wars (%d ended, mean length %.0f years), %d battles (%d taken), %d trade routes, %d nukes\n",
		tl.declared, tl.ended, meanLen, tl.battles, tl.won, tl.routes, tl.nukes);
	std::printf("    contacted pair-years at war %.1f %%, allied (v >= 0.5) %.1f %%\n", 100 * warShare, contactPairYears ? 100.0 * allyPairYears / contactPairYears : 0.0);
	for (const std::string& l : tl.lines) if (l.find("battle") == std::string::npos) std::printf("%s\n", l.c_str());
	ok(tl.declared >= 2, "wars happen");
	ok(warShare < 0.35, "wars are not the normal state");
	ok(tl.routes >= 5, "trade routes appear");
	noNaN(*w);
}

DM_TEST("diplomacy: cost of a yearly diplomacy() at L = 6 (6 cultures, ~120 settlements)") {
	stub::Guard guard(false);
	if (!fakesOk()) return;
	installFakes();
	auto w = makeWorld({ .L = 6, .seed = 5, .land = 0.45 });
	for (int k = 0; k < 6; k++) addCulture(*w, 0.6, Age::IRON);
	std::vector<int32_t> tmp;
	int placed = 0;
	for (int c = 0; c < w->n && placed < 120; c += 5) {
		if (seaC(*w, c) || w->hab[size_t(c)] < 0.15f) continue;
		w->geo->ring(c, 4, tmp);
		bool free = true;
		for (const int m : tmp) if (w->owner[size_t(m)] >= 0) { free = false; break; }
		if (!free) continue;
		const int s = addTown(*w, c, placed % 6, 1000, 3, 2);
		w->settlements[size_t(s)].b[Building::MARKET] = 1;
		placed++;
	}
	double total = 0, worst = 0;
	for (int y = 1; y <= 50; y++) {
		w->year = y;
		const double t0 = nowMs();
		society::diplomacy(*w);
		const double dt = nowMs() - t0;
		total += dt;
		worst = std::max(worst, dt);
		arrive(*w, true);
	}
	std::printf("    %d settlements: diplomacy() mean %.3f ms, worst %.3f ms (BFS path stand-in included)\n", placed, total / 50, worst);
	ok(total / 50 < 25, "cheap enough for the yearly group");
}
