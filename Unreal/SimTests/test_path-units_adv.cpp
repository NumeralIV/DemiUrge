// DEMIURGE — SimTests/test_path-units_adv.cpp : adversarial verification of A6 path + units (SPEC-SIM 3.6, 3.9;
// BUILD.md A6). Written by the A6 verifier, independently of the builder's tests. Runs with the real Path and
// Units and every other module stubbed (hook-dependent tests return early in `run.ps1 all`).
//
// What it attacks: optimality at L 6 (not only L 4) against an independent Dijkstra, the maxCost boundary, worst-case
// whole-planet searches, reach order, damaged worlds (NaN in every field Path reads, NaN / infinite sea level,
// road 255), poles and pentagons, no-sea / no-land planets, determinism; Units pace at L 6, the cap, payloads,
// re-pathing honouring `naval`, damaged units, re-entrant event handlers, dead entities, dirOf across a pole,
// ownership (Units writes only units, joined settlements' pop and the bus) and a re-path storm at L 6.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <queue>
#include <set>
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

constexpr double kInfA = std::numeric_limits<double>::infinity();
const NavalMode kAllModes[3] = { NavalMode::Land, NavalMode::Coastal, NavalMode::Open };
const char* mname(NavalMode m) { return m == NavalMode::Open ? "open" : m == NavalMode::Coastal ? "coastal" : "land"; }

// SPEC-SIM 3.6, read independently (thresholds as documented in Path.h: dense forest veg >= 0.6, land ice > 0.5,
// lake water > 0.03, road = the lower level of the two ends, lava > 0.05 blocks).
double specCost(const World& w, int a, int b, NavalMode m) {
	const double sl = w.seaLevel;
	const double ea = w.elev[size_t(a)], eb = w.elev[size_t(b)];
	if (w.lava[size_t(b)] > 0.05f) return kInfA;
	if (eb < sl) {
		if (m == NavalMode::Land) return kInfA;
		if (w.ice[size_t(b)] > 0.6f) return kInfA;
		if (m == NavalMode::Coastal && sl - eb >= 0.6) return kInfA;
		if (ea >= sl) return 3;
		return m == NavalMode::Open ? 0.35 : 0.6;
	}
	if (ea < sl) return 3;
	const double sa = ea + w.water[size_t(a)], sb = eb + w.water[size_t(b)];
	double c = 1 + 6 * std::fabs(sb - sa);
	if (w.veg[size_t(b)] >= 0.6f) c += 0.4;
	if (w.ice[size_t(b)] > 0.5f) c += 3;
	if (w.water[size_t(b)] > 0.03f) c += 2;
	c -= 0.25 * double(std::min(w.road[size_t(a)], w.road[size_t(b)]));
	return c < 0.3 ? 0.3 : c;
}

std::vector<double> refDijkstra(const World& w, int from, NavalMode m) {
	const Geo& g = *w.geo;
	std::vector<double> d(size_t(w.n), kInfA);
	using E = std::pair<double, int>;
	std::priority_queue<E, std::vector<E>, std::greater<E>> q;
	d[size_t(from)] = 0;
	q.push({ 0, from });
	while (!q.empty()) {
		const E e = q.top();
		q.pop();
		if (e.first > d[size_t(e.second)]) continue;
		for (int k = 0; k < g.nbrN[size_t(e.second)]; k++) {
			const int b = g.nbr[size_t(6 * e.second + k)];
			const double nd = e.first + specCost(w, e.second, b, m);
			if (nd < d[size_t(b)]) { d[size_t(b)] = nd; q.push({ nd, b }); }
		}
	}
	return d;
}

bool nbrOf(const World& w, int a, int b) {
	for (int k = 0; k < w.geo->nbrN[size_t(a)]; k++) if (w.geo->nbr[size_t(6 * a + k)] == b) return true;
	return false;
}
// cost of a route by the spec rule; inf if it is not a simple chain of neighbours from -> to
double chainCost(const World& w, const std::vector<int32_t>& p, int from, int to, NavalMode m) {
	if (p.empty() || p.front() != from || p.back() != to) return kInfA;
	std::set<int> seen(p.begin(), p.end());
	if (seen.size() != p.size()) return kInfA;
	double s = 0;
	for (size_t i = 1; i < p.size(); i++) {
		if (!w.valid(p[i]) || !nbrOf(w, p[i - 1], p[i])) return kInfA;
		s += specCost(w, p[i - 1], p[i], m);
	}
	return s;
}

// a helper planet with road networks, lakes, lava, forest, land ice and pack ice
void decorate(World& w, uint32_t seed) {
	const Geo& g = *w.geo;
	Rng r(7000 + seed);
	std::vector<int32_t> cells;
	for (int c = 0; c < w.n; c++) {
		const bool sea = double(w.elev[size_t(c)]) < w.seaLevel;
		const double la = std::fabs(double(g.lat[size_t(c)]));
		if (sea) {
			if (la > 1.1) w.ice[size_t(c)] = 0.9f;
			else if (la > 0.95) w.ice[size_t(c)] = 0.55f;
		} else {
			if (la > 1.15) w.ice[size_t(c)] = 0.8f;
			w.veg[size_t(c)] = float(r.f());
		}
	}
	const int scale = w.n / 2562 + 1;   // L4: 1, L6: 16
	for (int k = 0; k < 12 * scale; k++) {          // roads: random walks over land, level 1..3
		int c = r.integer(0, w.n - 1);
		const uint8_t lvl = uint8_t(r.integer(1, 3));
		for (int s = 0; s < 40; s++) {
			if (double(w.elev[size_t(c)]) < w.seaLevel) break;
			w.road[size_t(c)] = std::max(w.road[size_t(c)], lvl);
			c = g.nbr[size_t(6 * c + r.integer(0, g.nbrN[size_t(c)] - 1))];
		}
	}
	for (int k = 0; k < 10 * scale; k++) {          // lakes
		const int c = r.integer(0, w.n - 1);
		if (double(w.elev[size_t(c)]) < w.seaLevel) continue;
		g.ring(c, 1, cells);
		for (int x : cells) if (double(w.elev[size_t(x)]) >= w.seaLevel) w.water[size_t(x)] = float(0.05 + 0.5 * r.f());
	}
	for (int k = 0; k < 8 * scale; k++) {           // lava
		const int c = r.integer(0, w.n - 1);
		g.ring(c, r.integer(0, 2), cells);
		for (int x : cells) w.lava[size_t(x)] = float(r.f() < 0.8 ? 0.5 : 0.04);
	}
}

std::unique_ptr<World> flatAll(int L, float e = 0.5f) {
	auto w = state::create({ .seed = 3, .L = L, .geo = sharedGeo(L) });
	watch(*w);
	std::fill(w->elev.begin(), w->elev.end(), e);
	w->seaLevel = 0;
	w->heat = 0;
	w->phase = Phase::History;
	return w;
}

int townAt(World& w, int cell, int culture, double pop = 100) {
	Settlement& s = state::newSettlement(w, cell, culture);
	s.pop = pop;
	return s.id;
}

// a copy of everything Units must not write (cells, scalars, rng, cultures, vars, other settlement fields)
struct Frozen {
	std::vector<std::vector<double>> fields;
	uint32_t rngS = 0;
	double year = 0, seaLevel = 0, heat = 0, awe = 0;
	int nextDis = 0, nextEntry = 0;
	size_t nSettlements = 0, nCultures = 0, nVars = 0, nRel = 0, nChron = 0, nDis = 0;
	std::vector<double> sFields;   // every settlement field but pop
	bool dirty[6] = {}, need[4] = {};
};
Frozen freeze(const World& w) {
	Frozen f;
	for (int i = 0; i < state::kCellFieldCount; i++) {
		const CellArray a = state::cellArray(w, i);
		std::vector<double> v(a.size);
		for (size_t c = 0; c < a.size; c++) v[c] = a.get(c);
		f.fields.push_back(std::move(v));
	}
	f.rngS = w.rng.s; f.year = w.year; f.seaLevel = w.seaLevel; f.heat = w.heat; f.awe = w.god.awe;
	f.nextDis = w.nextId.disaster; f.nextEntry = w.nextId.entry;
	f.nSettlements = w.settlements.size(); f.nCultures = w.cultures.size(); f.nVars = w.vars.num.size() + w.vars.str.size();
	f.nRel = w.vars.rel.size(); f.nChron = w.chronicle.size(); f.nDis = w.disasters.size();
	for (const Settlement& s : w.settlements) {
		f.sFields.insert(f.sFields.end(), { double(s.cell), double(s.culture), double(s.dead), s.K, double(s.tier), s.devotion, s.love, s.jaded,
			s.plague, double(s.radius), double(s.capital), s.lastExpand });
	}
	f.dirty[0] = w.dirty.elev; f.dirty[1] = w.dirty.color; f.dirty[2] = w.dirty.water; f.dirty[3] = w.dirty.objects;
	f.dirty[4] = w.dirty.rivers; f.dirty[5] = w.dirty.roads;
	f.need[0] = w.need.derive; f.need[1] = w.need.climate; f.need[2] = w.need.hydro; f.need[3] = w.need.life;
	return f;
}
std::string thawDiff(const Frozen& a, const Frozen& b) {
	for (size_t i = 0; i < a.fields.size(); i++) {
		for (size_t c = 0; c < a.fields[i].size(); c++) {
			const double x = a.fields[i][c], y = b.fields[i][c];
			if (!(x == y) && !(std::isnan(x) && std::isnan(y))) return std::string(state::kCellFields[i].name) + "[" + std::to_string(c) + "]";
		}
	}
	if (a.rngS != b.rngS) return "rng";
	if (a.year != b.year || a.seaLevel != b.seaLevel || a.heat != b.heat || a.awe != b.awe) return "scalars";
	if (a.nextDis != b.nextDis || a.nextEntry != b.nextEntry) return "nextId";
	if (a.nSettlements != b.nSettlements || a.nCultures != b.nCultures || a.nVars != b.nVars || a.nRel != b.nRel || a.nChron != b.nChron || a.nDis != b.nDis) return "entity counts / vars";
	if (a.sFields != b.sFields) return "settlement fields other than pop";
	for (int k = 0; k < 6; k++) if (a.dirty[k] != b.dirty[k]) return "dirty flag " + std::to_string(k);
	for (int k = 0; k < 4; k++) if (a.need[k] != b.need[k]) return "need flag " + std::to_string(k);
	return "";
}

} // namespace

// ==================================================================================================== Path

DM_TEST("adv path: find is optimal at L 6 on a decorated planet (roads, lakes, lava, ice), every mode, vs Dijkstra") {
	auto w = makeWorld({ .L = 6, .seed = 5 });
	decorate(*w, 5);
	w->need.derive = true;   // no landmass shortcut: the search itself must decide
	Rng r(99);
	int pairs = 0, reachable = 0, bad = 0;
	double worst = 0;
	for (NavalMode m : kAllModes) {
		for (int s = 0; s < 8; s++) {
			const int from = r.integer(0, w->n - 1);
			const std::vector<double> d = refDijkstra(*w, from, m);
			for (int k = 0; k < 6; k++) {
				const int to = r.integer(0, w->n - 1);
				double cost = -1;
				const std::vector<int32_t> p = path::find(*w, from, to, { .naval = m, .maxCost = 1e9 }, &cost);
				pairs++;
				const double best = d[size_t(to)];
				if (std::isinf(best)) {
					if (!p.empty() || !std::isinf(cost)) { if (bad++ < 6) fail(std::string(mname(m)) + ": route where Dijkstra has none"); }
					continue;
				}
				reachable++;
				const double rc = chainCost(*w, p, from, to, m);
				const double err = std::fabs(rc - best);
				worst = std::max(worst, err);
				if (!(err <= 1e-7 * (1 + best)) || !(std::fabs(cost - best) <= 1e-7 * (1 + best))) {
					if (bad++ < 6) fail(std::string(mname(m)) + ": cost " + std::to_string(cost) + " / route " + std::to_string(rc) + " vs optimal " + std::to_string(best));
				}
			}
		}
	}
	std::printf("    L6 optimality: %d pairs, %d reachable, worst |route - optimal| %.3g\n", pairs, reachable, worst);
	eq(bad, 0, "non-optimal or invalid routes");
	ok(reachable > 40, "enough reachable pairs to mean something");
}

DM_TEST("adv path: maxCost is exact at the boundary (cost C: maxCost C finds it, C - 1e-6 does not)") {
	int checked = 0;
	for (uint32_t seed = 1; seed <= 3; seed++) {
		auto w = makeWorld({ .L = 4, .seed = seed });
		decorate(*w, seed);
		Rng r(seed * 31);
		for (NavalMode m : kAllModes) {
			for (int s = 0; s < 6; s++) {
				const int from = r.integer(0, w->n - 1);
				const std::vector<double> d = refDijkstra(*w, from, m);
				for (int k = 0; k < 10; k++) {
					const int to = r.integer(0, w->n - 1);
					const double C = d[size_t(to)];
					if (std::isinf(C) || to == from) continue;
					checked++;
					double got = 0;
					const auto p1 = path::find(*w, from, to, { .naval = m, .maxCost = C + 1e-9 }, &got);
					if (!ok(!p1.empty() && std::fabs(got - C) < 1e-9, "a route costing exactly the budget is found")) return;
					const auto p2 = path::find(*w, from, to, { .naval = m, .maxCost = C - 1e-6 });
					if (!ok(p2.empty(), "a budget just under the cheapest route finds nothing")) return;
				}
			}
		}
	}
	std::printf("    maxCost boundary: %d pairs\n", checked);
	ok(checked > 100);
}

DM_TEST("adv path: worst-case searches at L 6 (whole planet, no shortcut, enclosed targets) stay within budget") {
	auto w = makeWorld({ .L = 6, .seed = 2 });
	const Geo& g = *w->geo;
	// the two cells of the largest landmass (id 0) farthest apart (approx.: farthest from a cell of it, twice)
	int a = findLand(*w, [&](int c) { return w->cache.landId[size_t(c)] == 0; });
	int b = a;
	for (int rep = 0; rep < 2; rep++) {
		int far = a;
		double fa = -1;
		for (int c = 0; c < w->n; c++) {
			if (w->cache.landId[size_t(c)] != 0) continue;
			const double an = g.angle(a, c);
			if (an > fa) { fa = an; far = c; }
		}
		if (rep == 0) a = far; else b = far;
	}
	// an enclosed land target on the same landmass: a lava ring around it (the target itself is fine)
	const int enclosed = findLand(*w, [&](int c) { return w->cache.landId[size_t(c)] == 0 && g.angle(a, c) > 0.5; });
	std::vector<int32_t> ring;
	if (enclosed >= 0) {
		g.ring(enclosed, 2, ring);
		for (int c : ring) if (c != enclosed) w->lava[size_t(c)] = 0.9f;
	}
	int seaStart = -1, shelfStart = -1;
	for (int c = 0; c < w->n && (seaStart < 0 || shelfStart < 0); c++) {
		if (!state::isSea(*w, c)) continue;
		if (seaStart < 0 && w->elev[size_t(c)] < -1.0f) seaStart = c;
		if (shelfStart < 0 && w->elev[size_t(c)] > -0.3f) shelfStart = c;
	}
	if (!ok(a >= 0 && b >= 0 && enclosed >= 0 && seaStart >= 0 && shelfStart >= 0, "scenario cells")) return;
	int landCells = 0;
	for (int c = 0; c < w->n; c++) if (w->cache.landId[size_t(c)] == 0) landCells++;
	std::printf("    largest landmass: %d cells\n", landCells);
	struct Case { const char* name; int from, to; NavalMode m; double maxCost; bool derive; };
	const Case cases[] = {
		{ "land, far ends of the largest continent, default budget", a, b, NavalMode::Land, 400, false },
		{ "land, far ends, unlimited budget", a, b, NavalMode::Land, 1e9, false },
		{ "land, enclosed target, unlimited budget", a, enclosed, NavalMode::Land, 1e9, false },
		{ "open, enclosed target, unlimited (whole planet)", seaStart, enclosed, NavalMode::Open, 1e9, false },
		{ "coastal, enclosed target, unlimited", shelfStart, enclosed, NavalMode::Coastal, 1e9, false },
		{ "land, other landmass, need.derive set (no shortcut)", a, -2, NavalMode::Land, 1e9, true },
	};
	double worst = 0;
	for (const Case& cs : cases) {
		int to = cs.to;
		if (to == -2) to = findLand(*w, [&](int c) { return w->cache.landId[size_t(c)] != w->cache.landId[size_t(a)]; });
		w->need.derive = cs.derive;
		double best = 1e30, cost = 0;
		size_t len = 0;
		for (int rep = 0; rep < 3; rep++) {
			const double t0 = nowMs();
			const auto p = path::find(*w, cs.from, to, { .naval = cs.m, .maxCost = cs.maxCost }, &cost);
			best = std::min(best, nowMs() - t0);
			len = p.size();
		}
		std::printf("    worst case: %-58s %6.2f ms (%zu cells, cost %.1f)\n", cs.name, best, len, std::isinf(cost) ? -1.0 : cost);
		worst = std::max(worst, best);
	}
	w->need.derive = false;
	{
		int visited = 0;
		const double t0 = nowMs();
		visited = path::reach(*w, seaStart, 1e9, NavalMode::Open, [](int, double) { return true; });
		const double ms = nowMs() - t0;
		std::printf("    whole-planet reach (open, unlimited): %d cells %.2f ms\n", visited, ms);
		worst = std::max(worst, ms);
	}
	ok(worst <= 125, "worst case within 5x the 25 ms target");
}

DM_TEST("adv path: reach matches Dijkstra in every mode, in non-decreasing order, with a maxCost cut and early stop") {
	auto w = makeWorld({ .L = 4, .seed = 8 });
	decorate(*w, 8);
	Rng r(5);
	for (NavalMode m : kAllModes) {
		for (int s = 0; s < 5; s++) {
			const int from = r.integer(0, w->n - 1);
			const std::vector<double> d = refDijkstra(*w, from, m);
			const double cut = 20 + 30 * s;
			std::vector<int> seen(size_t(w->n), 0);
			double last = -1;
			int prevCell = -1, order = 0, bad = 0;
			const int n = path::reach(*w, from, cut, m, [&](int c, double cost) {
				if (order++ == 0 && (c != from || cost != 0)) bad++;
				if (cost < last || (cost == last && c < prevCell)) bad++;
				if (!(std::fabs(cost - d[size_t(c)]) <= 1e-9)) bad++;
				if (seen[size_t(c)]++) bad++;
				last = cost; prevCell = c;
				return true;
			});
			int expect = 0;
			for (int c = 0; c < w->n; c++) if (d[size_t(c)] <= cut) expect++;
			if (!eq(bad, 0, std::string(mname(m)) + " reach order / cost / once")) return;
			if (!eq(n, expect, std::string(mname(m)) + " reach visits exactly the cells within the cut")) return;
			int calls = 0;
			const int stopped = path::reach(*w, from, cut, m, [&](int, double) { return ++calls < 7; });
			if (expect >= 7) { eq(stopped, 7, "stops at the 7th visit"); eq(calls, 7); }
		}
	}
}

DM_TEST("adv path: damaged worlds (NaN in every field Path reads, NaN / infinite sea level, road 255) never yield NaN or hang") {
	const double t0 = nowMs();
	for (int variant = 0; variant < 4; variant++) {
		auto w = makeWorld({ .L = 4, .seed = 4, .watch = false });
		decorate(*w, 4);
		Rng r(variant + 1);
		const float qn = std::numeric_limits<float>::quiet_NaN();
		for (int k = 0; k < 60; k++) {
			const int c = r.integer(0, w->n - 1);
			switch (k % 6) {
			case 0: w->elev[size_t(c)] = qn; break;
			case 1: w->water[size_t(c)] = qn; break;
			case 2: w->veg[size_t(c)] = qn; break;
			case 3: w->ice[size_t(c)] = qn; break;
			case 4: w->lava[size_t(c)] = qn; break;
			default: w->road[size_t(c)] = 255; break;
			}
		}
		if (variant == 1) w->seaLevel = std::numeric_limits<double>::quiet_NaN();
		if (variant == 2) w->seaLevel = kInfA;     // everything is sea
		if (variant == 3) w->seaLevel = -kInfA;    // everything is land
		int bad = 0;
		for (NavalMode m : kAllModes) {
			for (int k = 0; k < 40; k++) {
				const int from = r.integer(0, w->n - 1), to = r.integer(0, w->n - 1);
				double cost = 0;
				const auto p = path::find(*w, from, to, { .naval = m, .maxCost = (k % 3 == 0) ? kInfA : 400 }, &cost);
				if (std::isnan(cost)) bad++;
				if (!p.empty()) {
					if (p.front() != from || p.back() != to) bad++;
					for (size_t i = 1; i < p.size(); i++) if (!nbrOf(*w, p[i - 1], p[i])) bad++;
					const double pc = path::cost(*w, p, m);
					if (!(std::isfinite(pc) && pc >= 0)) bad++;
				}
				for (int j = 0; j < w->geo->nbrN[size_t(from)]; j++) {
					const double sc = path::stepCost(*w, from, w->geo->nbr[size_t(6 * from + j)], m);
					if (std::isnan(sc) || sc < 0) bad++;
				}
				path::reach(*w, from, 50, m, [&](int, double c) { if (!std::isfinite(c) || c < 0) bad++; return true; });
			}
		}
		eq(bad, 0, "variant " + std::to_string(variant) + ": NaN / negative costs or broken routes");
		// bad arguments
		ok(path::find(*w, -1, 3).empty() && path::find(*w, 3, w->n).empty(), "bad cells: empty");
		ok(path::find(*w, 3, 5, { .maxCost = -1 }).empty(), "negative budget: empty");
		ok(path::reach(*w, 3, -1, NavalMode::Open, [](int, double) { return true; }) == 0, "negative budget: nothing");
		ok(path::reach(*w, 3, 10, NavalMode::Open, nullptr) == 0, "no visitor: nothing");
		ok(std::isinf(path::cost(*w, {}, NavalMode::Land)), "empty route: no cost");
	}
	std::printf("    damaged worlds: %.1f ms\n", nowMs() - t0);
}

DM_TEST("adv path: poles and pentagons on an all-land flat planet: cost == BFS hops at L 0..6") {
	for (int L = 0; L <= 6; L++) {
		auto w = flatAll(L);
		const Geo& g = *w->geo;
		const int np = g.cellAt(0, 1, 0), sp = g.cellAt(0, -1, 0);
		std::vector<int32_t> ring;
		g.ring(np, 1000, ring);   // BFS order from the north pole
		std::vector<int> hop(size_t(w->n), -1);
		hop[size_t(np)] = 0;
		std::vector<int> q{ np };
		for (size_t h = 0; h < q.size(); h++) {
			const int c = q[h];
			for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
				const int b = g.nbr[size_t(6 * c + k)];
				if (hop[size_t(b)] < 0) { hop[size_t(b)] = hop[size_t(c)] + 1; q.push_back(b); }
			}
		}
		const int targets[3] = { sp, 0, 11 };   // the south pole and two icosahedron vertices (pentagons)
		for (int to : targets) {
			for (NavalMode m : kAllModes) {
				double cost = 0;
				const auto p = path::find(*w, np, to, { .naval = m, .maxCost = 1e9 }, &cost);
				if (!eq(double(p.size()) - 1, double(hop[size_t(to)]), "L" + std::to_string(L) + " hops")) return;
				if (!near(cost, double(hop[size_t(to)]), 1e-9, "flat land: one per hop")) return;
			}
		}
	}
}

DM_TEST("adv path: an all-sea planet and an all-land planet behave per mode (L 4 and L 6)") {
	for (int L : { 4, 6 }) {
		auto sea = flatAll(L, -0.3f);        // shallow everywhere
		const int a = 0, b = sea->n / 2;
		ok(path::find(*sea, a, b).empty(), "all sea: nothing on foot");
		eq(path::find(*sea, a, a).size(), size_t(1), "from == to: {from} even at sea on foot");
		double c1 = 0, c2 = 0;
		const auto pc = path::find(*sea, a, b, { .naval = NavalMode::Coastal, .maxCost = 1e9 }, &c1);
		const auto po = path::find(*sea, a, b, { .naval = NavalMode::Open, .maxCost = 1e9 }, &c2);
		ok(!pc.empty() && !po.empty(), "all shallow sea: coastal and open sail");
		near(c1, 0.6 * double(pc.size() - 1), 1e-6, "coastal 0.6 per step");
		near(c2, 0.35 * double(po.size() - 1), 1e-6, "open 0.35 per step");
		std::fill(sea->elev.begin(), sea->elev.end(), -3.0f);   // deep everywhere
		ok(path::find(*sea, a, b, { .naval = NavalMode::Coastal, .maxCost = 1e9 }).empty(), "deep sea: no canoes");
		ok(!path::find(*sea, a, b, { .naval = NavalMode::Open, .maxCost = 1e9 }).empty(), "deep sea: ships");
		auto land = flatAll(L, 0.5f);
		double l0 = 0, l1 = 0, l2 = 0;
		const auto q0 = path::find(*land, a, b, { .naval = NavalMode::Land, .maxCost = 1e9 }, &l0);
		const auto q1 = path::find(*land, a, b, { .naval = NavalMode::Coastal, .maxCost = 1e9 }, &l1);
		const auto q2 = path::find(*land, a, b, { .naval = NavalMode::Open, .maxCost = 1e9 }, &l2);
		ok(q0 == q1 && q1 == q2 && l0 == l1 && l1 == l2, "no sea: every mode walks the same route");
	}
}

DM_TEST("adv path: deterministic: same route twice, across worlds sharing a geo, and after other levels' searches") {
	auto w1 = makeWorld({ .L = 5, .seed = 3 });
	auto w2 = makeWorld({ .L = 5, .seed = 3 });
	auto w4 = makeWorld({ .L = 4, .seed = 3 });
	decorate(*w1, 3);
	decorate(*w2, 3);
	Rng r(17);
	int diff = 0;
	for (int k = 0; k < 60; k++) {
		const int from = r.integer(0, w1->n - 1), to = r.integer(0, w1->n - 1);
		const NavalMode m = kAllModes[k % 3];
		const auto p1 = path::find(*w1, from, to, { .naval = m });
		path::find(*w4, from % w4->n, to % w4->n, { .naval = m });   // interleave another world / level
		const auto p2 = path::find(*w2, from, to, { .naval = m });
		const auto p3 = path::find(*w1, from, to, { .naval = m });
		if (p1 != p2 || p1 != p3) diff++;
	}
	eq(diff, 0, "identical routes");
	eq(hash(*w1), hash(*w2), "searching wrote nothing into either world");
}

// ==================================================================================================== Units

DM_TEST("adv units: exact pace at L 6: 5 cells a year on level land, 12.5 at sea, whatever the dt") {
	for (const double dt : { 0.25, 0.1, 1.0 }) {
		auto w = flatAll(6);
		const int cu = state::newCulture(*w).id;
		const Geo& g = *w->geo;
		const int a = g.cellAt(1, 0, 0), b = g.cellAt(-1, 0.01, 0);   // half way round
		const int s0 = townAt(*w, a, cu);
		Unit* u = units::spawn(*w, { .type = UnitType::Settler, .culture = cu, .from = s0, .dest = b, .size = 20, .maxCost = 1e9 });
		if (!ok(u != nullptr, "spawn at L6")) return;
		const int id = u->id;
		const int steps = int(std::lround(2.0 / dt));
		for (int k = 0; k < steps; k++) units::step(*w, dt);
		const Unit* v = state::findUnit(*w, id);
		if (!ok(v != nullptr, "still travelling")) return;
		near(double(v->i) + v->t, 10.0, 1e-9, "2 years x 5 cells (dt " + std::to_string(dt) + ")");
	}
	// at sea: an all-shallow world, an open-sea prophet: 4 x 2.5 = 10 cells a year
	auto w = flatAll(6, -0.2f);
	const Geo& g = *w->geo;
	const int a = g.cellAt(1, 0, 0), b = g.cellAt(-1, 0.01, 0);
	// a one-cell island to start from (settlements stand on land)
	w->elev[size_t(a)] = 0.5f;
	const int cu = state::newCulture(*w).id;
	const int s0 = townAt(*w, a, cu);
	Unit* u = units::spawn(*w, { .type = UnitType::Prophet, .culture = cu, .from = s0, .dest = b, .size = 1, .naval = NavalMode::Open, .maxCost = 1e9 });
	if (!ok(u != nullptr, "prophet at sea")) return;
	const int id = u->id;
	units::step(*w, 0.25);   // embark (1 land step = 0.25 yr at 4 cells/yr)
	const Unit* v = state::findUnit(*w, id);
	if (!ok(v != nullptr)) return;
	near(double(v->i) + v->t, 1.0, 1e-9, "embarking takes one land step");
	for (int k = 0; k < 4; k++) units::step(*w, 0.25);
	v = state::findUnit(*w, id);
	if (!ok(v != nullptr)) return;
	near(double(v->i) + v->t, 11.0, 1e-9, "then 10 sea cells a year");
	ok(v->sea, "sea flag at sea");
}

DM_TEST("adv units: the cap is exactly 300 and a refused spawn changes nothing") {
	auto w = flatAll(4);
	const int cu = state::newCulture(*w).id;
	const int s0 = townAt(*w, 0, cu);
	int made = 0;
	for (int k = 0; k < 300; k++) if (units::spawn(*w, { .culture = cu, .from = s0, .dest = 100 + k, .size = 1 })) made++;
	eq(made, 300, "300 units");
	const int next = w->nextId.unit;
	const uint32_t h = hash(*w);
	auto ev = capture(*w, [&] { ok(units::spawn(*w, { .culture = cu, .from = s0, .dest = 5, .size = 1 }) == nullptr, "301st refused"); });
	eq(ev.size(), size_t(0), "no event");
	eq(w->nextId.unit, next, "no id spent");
	eq(hash(*w), h, "world unchanged");
	units::kill(*w, w->units[0].id, "test");
	ok(units::spawn(*w, { .culture = cu, .from = s0, .dest = 5, .size = 1 }) != nullptr, "room again after a death");
}

DM_TEST("adv units: payloads of unit:spawned / arrived / died carry exactly id, type, culture, cell (+ cause)") {
	stub::Guard guard;
	if (!stub::stubbed("diplomacy")) return;   // the real onArrive fights battles (unit:died 'war')
	auto w = flatAll(4);
	const int cu = state::newCulture(*w).id;
	const int cu2 = state::newCulture(*w).id;
	const int a = w->geo->cellAt(1, 0, 0), b = w->geo->cellAt(0, 0, 1);
	const int s0 = townAt(*w, a, cu), s1 = townAt(*w, b, cu2);
	society::rel(*w, cu, cu2)->war = true;
	std::vector<Event> ev = capture(*w, [&] {
		const Unit* t = units::spawn(*w, { .type = UnitType::Trader, .culture = cu, .from = s0, .to = s1, .size = 3 });
		const Unit* m = units::spawn(*w, { .type = UnitType::Army, .culture = cu, .from = s0, .to = s1, .size = 9 });
		ok(t && m, "spawned");
		for (int k = 0; k < 200 && !w->units.empty(); k++) units::step(*w, 0.25);
	});
	int spawned = 0, arrived = 0;
	for (const Event& e : ev) {
		ok(checkEvent(e, w->n).empty(), "event contract: " + checkEvent(e, w->n));
		if (const EvUnitSpawned* x = e.as<EvUnitSpawned>()) { spawned++; eq(x->cell, a, "spawned at the origin"); eq(x->culture, cu); }
		if (const EvUnitArrived* x = e.as<EvUnitArrived>()) { arrived++; eq(x->cell, b, "arrived at dest"); eq(x->culture, cu); }
		if (e.as<EvUnitDied>()) fail("nobody died: arrivals are erased silently");
	}
	eq(spawned, 2); eq(arrived, 2, "trader and army arrive (onArrive stub keeps nothing)");
	eq(w->units.size(), size_t(0), "erased after onArrive");
	// died: lava on the cell it stands on
	const Unit* u = units::spawn(*w, { .type = UnitType::Settler, .culture = cu, .from = s0, .dest = b, .size = 3 });
	if (!ok(u != nullptr)) return;
	const int id = u->id;
	units::step(*w, 0.25);
	const int here = units::cellOf(*w, *state::findUnit(*w, id));
	w->lava[size_t(here)] = 0.5f;
	ev = capture(*w, [&] { units::step(*w, 0.25); });
	if (!ok(ev.size() == 1 && ev[0].as<EvUnitDied>(), "one unit:died")) return;
	const EvUnitDied* d = ev[0].as<EvUnitDied>();
	eq(d->id, id); ok(d->type == UnitType::Settler); eq(d->culture, cu); eq(d->cell, here, "died where it stood"); eq(d->cause, std::string("lava"));
}

DM_TEST("adv units: re-path honours naval (on foot a sea detour is no detour; canoes cross the shelf; never deep water)") {
	stub::Guard guard;
	// land everywhere except an equatorial band of sea, bridged at one place near lon 0
	auto build = [](float bandDepth) {
		auto w = flatAll(4);
		const Geo& g = *w->geo;
		for (int c = 0; c < w->n; c++) {
			const double la = g.lat[size_t(c)], lo = g.lon[size_t(c)];
			if (std::fabs(la) < 0.2 && std::fabs(lo) > 0.25) w->elev[size_t(c)] = bandDepth;
		}
		return w;
	};
	for (int variant = 0; variant < 3; variant++) {
		const float depth = variant == 2 ? -3.0f : -0.3f;
		auto w = build(depth);
		const Geo& g = *w->geo;
		const int cu = state::newCulture(*w).id;
		const int a = g.cellAt(std::cos(0.6), std::sin(0.6), 0), b = g.cellAt(std::cos(-0.6), std::sin(-0.6), 0);
		const int s0 = townAt(*w, a, cu);
		const NavalMode m = variant == 0 ? NavalMode::Land : NavalMode::Coastal;
		Unit* u = units::spawn(*w, { .type = UnitType::Settler, .culture = cu, .from = s0, .dest = b, .size = 9, .naval = m });
		if (!ok(u != nullptr, "spawned over the bridge")) return;
		const int id = u->id;
		bool crossedSea = false;
		for (int c : u->path) if (state::isSea(*w, c)) crossedSea = true;
		ok(!crossedSea, "the first route takes the bridge (cheapest)");
		// burn the bridge
		for (int c = 0; c < w->n; c++) if (std::fabs(g.lat[size_t(c)]) < 0.2 && std::fabs(g.lon[size_t(c)]) <= 0.25) w->lava[size_t(c)] = 0.15f;
		std::string died;
		const auto tok = w->bus.on<EvUnitDied>([&](const EvUnitDied& e) { died = e.cause; });
		bool arrived = false;
		const auto tok2 = w->bus.on<EvUnitArrived>([&](const EvUnitArrived&) { arrived = true; });
		int deepCells = 0;
		for (int k = 0; k < 400 && state::findUnit(*w, id); k++) {
			units::step(*w, 0.25);
			if (const Unit* v = state::findUnit(*w, id)) {
				const int c = units::cellOf(*w, *v);
				if (state::isSea(*w, c) && w->seaLevel - w->elev[size_t(c)] >= 0.6) deepCells++;
				if (m == NavalMode::Land && state::isSea(*w, c)) fail("a walker went to sea");
			}
		}
		w->bus.off(tok); w->bus.off(tok2);
		if (variant == 0) { eq(died, std::string("lost"), "on foot: no route, lost"); ok(!arrived); }
		if (variant == 1) { ok(arrived && died.empty(), "canoes cross the shallow band after the re-path"); }
		if (variant == 2) { eq(died, std::string("lost"), "deep band: canoes cannot cross: lost"); ok(!arrived); }
		eq(deepCells, 0, "never on deep water in coastal mode");
	}
}

DM_TEST("adv units: damaged units, absurd dt and bad arguments never crash or write NaN") {
	auto w = flatAll(4);
	const int cu = state::newCulture(*w).id;
	const int s0 = townAt(*w, 5, cu);
	for (int k = 0; k < 6; k++) units::spawn(*w, { .culture = cu, .from = s0, .dest = 600 + 100 * k, .size = 4 });
	if (!eq(w->units.size(), size_t(6))) return;
	w->units[0].t = std::numeric_limits<double>::quiet_NaN();
	w->units[1].i = 100000;
	w->units[2].path.clear();
	w->units[3].path.push_back(w->n + 5);         // an invalid last cell
	w->units[4].speed = std::numeric_limits<double>::quiet_NaN();
	w->units[5].i = -7;
	units::step(*w, std::numeric_limits<double>::quiet_NaN());
	units::step(*w, -1);
	units::step(*w, kInfA);
	units::step(*w, 0);
	units::step(*w, 0.25);
	units::step(*w, 1e6);                          // everything left arrives or dies in one call
	noNaN(*w, "after damaged steps");
	eq(w->units.size(), size_t(0), "nothing left after a million years");
	Unit ghost;
	ghost.path = { 1, 2 };
	Vec3 d{};
	eq(units::cellOf(*w, ghost), 1, "cellOf of a unit not in the world still answers from its fields");
	units::dirOf(*w, ghost, d);
	near(v3::len(d), 1, 1e-12, "unit length");
	ok(!units::kill(*w, 12345, "x"), "kill of an absent id");
	ok(units::near(*w, -1, 1).empty() && units::near(*w, 3, std::numeric_limits<double>::quiet_NaN()).empty(), "near: bad args");
	ok(units::spawn(*w, { .culture = cu, .from = 999, .dest = 3 }) == nullptr, "unknown origin");
	ok(units::spawn(*w, { .culture = 77, .from = s0, .dest = 3 }) == nullptr, "unknown culture");
	ok(units::spawn(*w, { .culture = cu, .from = s0, .dest = w->n }) == nullptr, "bad dest");
	ok(units::spawn(*w, { .culture = cu, .from = s0, .to = 55, .dest = 3 }) == nullptr, "unknown target settlement");
	ok(units::spawn(*w, { .culture = cu, .from = s0, .dest = 9, .path = { 5, 9 } }) == nullptr || nbrOf(*w, 5, 9), "a ready path that is not a chain is refused");
	w->settlements[size_t(s0)].dead = true;
	ok(units::spawn(*w, { .culture = cu, .from = s0, .dest = 3 }) == nullptr, "a dead origin sends nobody");
}

DM_TEST("adv units: re-entrant handlers (kill others, spawn, nested step) during a step") {
	stub::Guard guard;
	auto w = flatAll(4);
	const int cu = state::newCulture(*w).id;
	const int s0 = townAt(*w, 5, cu);
	const int s1 = townAt(*w, 40, cu);
	// one unit arrives at once (dest = its origin cell), the others travel
	units::spawn(*w, { .culture = cu, .from = s0, .dest = 5, .size = 4 });
	for (int k = 0; k < 5; k++) units::spawn(*w, { .culture = cu, .from = s1, .dest = 1500 + 50 * k, .size = 4 });
	std::vector<int> fresh;
	int nested = 0;
	const auto t1 = w->bus.on<EvUnitArrived>([&](const EvUnitArrived& e) {
		std::vector<int> others;
		for (const Unit& u : w->units) if (u.id != e.id) others.push_back(u.id);
		for (size_t k = 0; k < others.size() && k < 2; k++) units::kill(*w, others[k], "handler");
		for (int k = 0; k < 2; k++) if (const Unit* u = units::spawn(*w, { .culture = cu, .from = s1, .dest = 900 + k, .size = 2 })) fresh.push_back(u->id);
	});
	const auto t2 = w->bus.on<EvUnitDied>([&](const EvUnitDied&) { if (nested++ == 0) units::step(*w, 0.25); });
	units::step(*w, 0.25);
	w->bus.off(t1); w->bus.off(t2);
	ok(nested >= 1, "a died handler stepped the units again");
	ok(fresh.size() >= 2, "spawned inside the handler");
	for (int id : fresh) {
		const Unit* u = state::findUnit(*w, id);
		if (!u) continue;   // the nested step may have moved / finished them; it must not crash
		ok(u->i >= 0 && u->t >= 0 && u->t < 1, "sane state");
	}
	noNaN(*w, "after re-entrant handlers");
	for (int k = 0; k < 400 && !w->units.empty(); k++) units::step(*w, 0.25);
	eq(w->units.size(), size_t(0), "everyone finishes");
}

DM_TEST("adv units: dead entities: armies to a dead town disband, settlers of a vanished culture are lost") {
	stub::Guard guard;
	if (!stub::stubbed("civ")) return;
	auto w = flatAll(4);
	const int cu = state::newCulture(*w).id, cu2 = state::newCulture(*w).id;
	const int s0 = townAt(*w, 5, cu), s1 = townAt(*w, 900, cu2), s2 = townAt(*w, 1800, cu);
	society::rel(*w, cu, cu2)->war = true;
	const Unit* army = units::spawn(*w, { .type = UnitType::Army, .culture = cu, .from = s0, .to = s1, .size = 9 });
	if (!ok(army != nullptr)) return;
	const int armyId = army->id;
	w->settlements[size_t(s1)].dead = true;
	std::vector<std::string> causes;
	const auto tok = w->bus.on<EvUnitDied>([&](const EvUnitDied& e) { causes.push_back(e.cause); });
	units::step(*w, 0.25);
	ok(!state::findUnit(*w, armyId), "army gone");
	ok(causes.size() == 1 && causes[0] == "disbanded", "disbanded");
	// a settler whose site is invalid (civ::found stub refuses) joins the nearest live town of its culture
	const Unit* st = units::spawn(*w, { .type = UnitType::Settler, .culture = cu, .from = s0, .dest = 1700, .size = 25 });
	if (!ok(st != nullptr)) return;
	const double before = w->settlements[size_t(s2)].pop;
	for (int k = 0; k < 400 && !w->units.empty(); k++) units::step(*w, 0.25);
	near(w->settlements[size_t(s2)].pop, before + 25, 1e-9, "joined the nearest live town (s2 near the site)");
	// now every town of the culture is dead: the settler is lost
	const Unit* st2 = units::spawn(*w, { .type = UnitType::Settler, .culture = cu, .from = s0, .dest = 1700, .size = 25 });
	if (!ok(st2 != nullptr)) return;
	w->settlements[size_t(s0)].dead = true;
	w->settlements[size_t(s2)].dead = true;
	causes.clear();
	for (int k = 0; k < 400 && !w->units.empty(); k++) units::step(*w, 0.25);
	w->bus.off(tok);
	ok(causes.size() == 1 && causes[0] == "lost", "no home: lost");
	noNaN(*w);
}

DM_TEST("adv units: dirOf is unit length and continuous on a journey over the north pole (L 6)") {
	auto w = flatAll(6);
	const Geo& g = *w->geo;
	const int cu = state::newCulture(*w).id;
	const int a = g.cellAt(std::cos(1.2), std::sin(1.2), 0), b = g.cellAt(-std::cos(1.2), std::sin(1.2), 0.01);
	const int s0 = townAt(*w, a, cu);
	const Unit* u = units::spawn(*w, { .type = UnitType::Prophet, .culture = cu, .from = s0, .dest = b, .size = 1 });
	if (!ok(u != nullptr)) return;
	const int id = u->id;
	bool sawPole = false;
	for (int c : u->path) if (g.lat[size_t(c)] > 1.5) sawPole = true;
	ok(sawPole, "the route crosses the polar cap");
	Vec3 prev{};
	units::dirOf(*w, *u, prev);
	double maxJump = 0, maxLenErr = 0;
	const double dt = 0.01;   // 0.04 cells per sample
	for (int k = 0; k < 4000; k++) {
		units::step(*w, dt);
		const Unit* v = state::findUnit(*w, id);
		if (!v) break;
		Vec3 d{};
		const int c = units::dirOf(*w, *v, d);
		maxLenErr = std::max(maxLenErr, std::fabs(v3::len(d) - 1));
		maxJump = std::max(maxJump, v3::angle(prev, d));
		ok(c == units::cellOf(*w, *v), "dirOf returns the current cell");
		prev = d;
	}
	std::printf("    dirOf over the pole: max jump %.5f rad per 0.04 cells (step %.5f), |len - 1| %.2g\n", maxJump, g.step, maxLenErr);
	ok(maxLenErr < 1e-12, "unit length");
	ok(maxJump < 0.06 * g.step * 1.6, "continuous: no jump bigger than the distance travelled");
}

DM_TEST("adv units: ownership: journeys write only w.units, joined settlements' pop and the bus") {
	stub::Guard guard;
	if (!stub::stubbed("civ") || !stub::stubbed("diplomacy")) return;   // real collaborators own what they write
	auto w = makeWorld({ .L = 5, .seed = 6 });
	decorate(*w, 6);
	const int cu = state::newCulture(*w).id, cu2 = state::newCulture(*w).id;
	std::vector<int> towns;
	for (int k = 0; k < 6; k++) {
		const int c = findLand(*w, [&](int x) { return w->lava[size_t(x)] < 0.05f && x > k * w->n / 6; });
		if (c >= 0) towns.push_back(townAt(*w, c, k % 2 ? cu2 : cu));
	}
	society::rel(*w, cu, cu2)->war = true;
	if (!ok(towns.size() == 6)) return;
	int spawned = 0;
	const UnitType types[4] = { UnitType::Settler, UnitType::Trader, UnitType::Army, UnitType::Prophet };
	for (int k = 0; k < 24; k++) {
		const int from = towns[size_t(k % 6)], to = towns[size_t((k + 1 + k / 6) % 6)];
		const Settlement& sf = w->settlements[size_t(from)];
		const Settlement& st = w->settlements[size_t(to)];
		const UnitType t = types[k % 4];
		units::SpawnOpts o{ .type = t, .culture = sf.culture, .from = from, .to = (t == UnitType::Settler && k % 3) ? -1 : to, .dest = st.cell, .size = 10, .naval = NavalMode::Open };
		if (units::spawn(*w, o)) spawned++;
	}
	ok(spawned >= 12, "enough journeys (" + std::to_string(spawned) + ")");
	const Frozen before = freeze(*w);
	for (int k = 0; k < 800 && !w->units.empty(); k++) units::step(*w, 0.25);
	const Frozen after = freeze(*w);
	eq(thawDiff(before, after), std::string(""), "Units wrote something it does not own");
	eq(w->units.size(), size_t(0), "all journeys ended");
}

DM_TEST("adv units: determinism: same calls, same world hash and the same event stream (L 5)") {
	auto run = [](std::vector<std::string>& log) {
		auto w = makeWorld({ .L = 5, .seed = 9 });
		decorate(*w, 9);
		const int cu = state::newCulture(*w).id;
		std::vector<int> towns;
		Rng r(4);
		for (int k = 0; k < 8; k++) {
			const int c = findLand(*w, [&](int x) { return w->lava[size_t(x)] < 0.05f && x > r.integer(0, w->n - 1); });
			if (c >= 0) towns.push_back(townAt(*w, c, cu));
		}
		w->bus.onAll([&](const Event& e) { std::string s = e.name(); e.fields([&](const char* k, const auto& v) { (void)k; if constexpr (std::is_arithmetic_v<std::decay_t<decltype(v)>>) s += " " + std::to_string(double(v)); }); log.push_back(s); });
		for (int k = 0; k < 30; k++) {
			units::spawn(*w, { .type = UnitType(k % 4), .culture = cu, .from = towns[size_t(k % towns.size())], .dest = r.integer(0, w->n - 1), .size = 5, .naval = NavalMode(k % 3) });
			for (int j = 0; j < 7; j++) units::step(*w, 0.25);
			if (k % 5 == 0) w->lava[size_t(r.integer(0, w->n - 1))] = 0.6f;
		}
		return hash(*w);
	};
	std::vector<std::string> l1, l2;
	const uint32_t h1 = run(l1), h2 = run(l2);
	eq(h1, h2, "same hash");
	ok(l1 == l2 && !l1.empty(), "same event stream (" + std::to_string(l1.size()) + " events)");
}

DM_TEST("adv units: performance at L 6: 300 units, then a re-path storm (every unit blocked at once)") {
	auto w = makeWorld({ .L = 6, .seed = 2 });
	const Geo& g = *w->geo;
	const int cu = state::newCulture(*w).id;
	// towns on the largest landmass; settlers walk to random cells on it
	std::vector<int> land;
	for (int c = 0; c < w->n; c++) if (w->cache.landId[size_t(c)] == 0) land.push_back(c);
	Rng r(12);
	std::vector<int> towns;
	for (int k = 0; k < 20; k++) towns.push_back(townAt(*w, land[size_t(r.integer(0, int(land.size()) - 1))], cu));
	double spawnMs = 0;
	for (int k = 0; k < 400 && int(w->units.size()) < 300; k++) {
		const double t0 = nowMs();
		units::spawn(*w, { .culture = cu, .from = towns[size_t(k % 20)], .dest = land[size_t(r.integer(0, int(land.size()) - 1))], .size = 5, .maxCost = 1e9 });
		spawnMs += nowMs() - t0;
	}
	eq(int(w->units.size()), 300, "300 units");
	double mean = 0, mx = 0;
	for (int k = 0; k < 40; k++) {
		const double t0 = nowMs();
		units::step(*w, 0.25);
		const double ms = nowMs() - t0;
		mean += ms / 40; mx = std::max(mx, ms);
	}
	// block every unit's next cell with lava (> 0.05 blocks, < 0.2 does not kill)
	int blocked = 0;
	for (Unit& u : w->units) {
		if (u.t >= 0.5 && size_t(u.i) + 2 < u.path.size()) { u.i++; u.t = 0; }   // stand on the nearer cell
		if (size_t(u.i) + 1 < u.path.size() && u.t < 0.5) {
			w->lava[size_t(u.path[size_t(u.i) + 1])] = 0.1f;
			blocked++;
		}
	}
	const size_t alive = w->units.size();
	int lost = 0;
	const auto tok = w->bus.on<EvUnitDied>([&](const EvUnitDied& e) { if (e.cause == "lost") lost++; });
	const double t0 = nowMs();
	units::step(*w, 0.25);
	const double storm = nowMs() - t0;
	w->bus.off(tok);
	std::printf("    re-path storm: %d lost (a one-cell lava block on a continent always has a detour)\n", lost);
	ok(lost <= blocked / 20, "almost every blocked unit finds its detour (long journeys keep a long enough budget)");
	std::printf("    L6: 300 spawns %.1f ms (%.2f each); step mean %.3f ms, max %.3f ms; re-path storm (%d blocked of %zu): %.1f ms, %zu alive after\n",
		spawnMs, spawnMs / 300, mean, mx, blocked, alive, storm, w->units.size());
	(void)g;
	ok(mean < 2.5 * 5, "step mean well inside the tick budget");
	// 285 simultaneous re-paths is far beyond anything the game produces (lava fields block a few units); the
	// number is reported, and only a pathological blow-up fails (the machine is shared with parallel builds)
	ok(storm < 600, "a re-path storm stays bounded (about 0.45 ms per blocked unit)");
	noNaN(*w);
}

DM_TEST("adv path-units: maps (routes per mode on a decorated L 6 planet; units mid-journey)") {
	auto w = makeWorld({ .L = 6, .seed = 4 });
	decorate(*w, 4);
	const Geo& g = *w->geo;
	int from = -1, to = -1;
	double far = -1;
	for (int c = 0; c < w->n; c += 7) {          // two far-apart cells of the largest landmass
		if (w->cache.landId[size_t(c)] != 0 || w->lava[size_t(c)] > 0.05f) continue;
		if (from < 0) { from = c; continue; }
		const double an = g.angle(from, c);
		if (an > far) { far = an; to = c; }
	}
	if (!ok(from >= 0 && to >= 0)) return;
	std::vector<std::vector<int32_t>> routes;
	for (NavalMode m : kAllModes) {
		double c = 0;
		routes.push_back(path::find(*w, from, to, { .naval = m, .maxCost = 1e9 }, &c));
		std::printf("    map route %s: %zu cells, cost %.1f\n", mname(m), routes.back().size(), std::isinf(c) ? -1.0 : c);
	}
	std::vector<int> col(size_t(w->n), -1);
	for (int k = 0; k < 3; k++) for (int c : routes[size_t(k)]) col[size_t(c)] = k;
	namespace mi = dm::mapimg;
	ok(mi::custom(*w, shot("ue-path-units-adv-routes.png"), [&](int c) {
		if (col[size_t(c)] == 0) return mi::Rgb{ 255, 40, 40 };
		if (col[size_t(c)] == 1) return mi::Rgb{ 255, 230, 0 };
		if (col[size_t(c)] == 2) return mi::Rgb{ 255, 0, 255 };
		if (w->lava[size_t(c)] > 0.05f) return mi::Rgb{ 20, 20, 20 };
		if (w->road[size_t(c)] > 0) return mi::Rgb{ 230, 150, 60 };
		const bool sea = state::isSea(*w, c);
		if (sea) return w->ice[size_t(c)] > 0.6f ? mi::Rgb{ 235, 245, 255 } : (w->seaLevel - w->elev[size_t(c)] < 0.6 ? mi::Rgb{ 70, 140, 210 } : mi::Rgb{ 20, 50, 120 });
		if (w->water[size_t(c)] > 0.03f) return mi::Rgb{ 90, 170, 230 };
		const double e = w->elev[size_t(c)];
		return mi::Rgb{ 60 + 25 * e, 120 + 15 * e, 50 + 10 * e };
	}), "routes map written");
}
