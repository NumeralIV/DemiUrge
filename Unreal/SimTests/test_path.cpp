// DEMIURGE — SimTests/test_path.cpp : Sim/Path (SPEC-SIM 3.6, BUILD.md A6). Owner: A6 path-units.
//
// Every expectation is derived here, independently of Path.cpp: the documented cost rule is written out again
// (refCost) and cheapest costs come from a plain Dijkstra over it (itself cross-checked against Bellman-Ford).
// Only the foundation and Path are needed; every other module may be a stub.
#include <algorithm>
#include <cmath>
#include <cstdio>
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

using namespace dm;
using namespace dmt;

namespace {

constexpr double kInfD = std::numeric_limits<double>::infinity();
constexpr double kEps = 1e-9;
const NavalMode kModes[3] = { NavalMode::Land, NavalMode::Coastal, NavalMode::Open };
const char* modeName(NavalMode m) { return m == NavalMode::Open ? "open" : m == NavalMode::Coastal ? "coastal" : "land"; }

bool seaC(const World& w, int c) { return double(w.elev[size_t(c)]) < w.seaLevel; }

// ------------------------------------------------------------------ the documented rule, written out again
double refCost(const World& w, int a, int b, NavalMode naval) {
	const double sea = w.seaLevel, ea = w.elev[size_t(a)], eb = w.elev[size_t(b)];
	if (w.lava[size_t(b)] > 0.05f) return kInfD;                     // lava is impassable
	const bool aSea = ea < sea, bSea = eb < sea;
	if (bSea) {
		if (naval == NavalMode::Land) return kInfD;                  // land only
		if (w.ice[size_t(b)] > 0.6f) return kInfD;                   // sea ice
		if (naval == NavalMode::Coastal && !(sea - eb < 0.6)) return kInfD;   // canoes keep to the shelves
		if (!aSea) return 3;                                         // embarking
		return naval == NavalMode::Open ? 0.35 : 0.6;
	}
	if (aSea) return 3;                                              // landing
	double c = 1 + 6 * std::fabs((eb + double(w.water[size_t(b)])) - (ea + double(w.water[size_t(a)])));
	if (w.veg[size_t(b)] >= 0.6f) c += 0.4;                          // dense forest
	if (w.ice[size_t(b)] > 0.5f) c += 3;
	if (w.water[size_t(b)] > 0.03f) c += 2;                          // lake cell
	c -= 0.25 * std::min(w.road[size_t(a)], w.road[size_t(b)]);
	return std::max(0.3, c);
}

std::vector<double> dijkstra(const World& w, int from, NavalMode naval) {
	const Geo& g = *w.geo;
	std::vector<double> dist(size_t(w.n), kInfD);
	std::vector<char> done(size_t(w.n), 0);
	using E = std::pair<double, int>;
	std::priority_queue<E, std::vector<E>, std::greater<E>> q;
	dist[size_t(from)] = 0;
	q.push({ 0, from });
	while (!q.empty()) {
		const E e = q.top();
		q.pop();
		const int c = e.second;
		if (done[size_t(c)]) continue;
		done[size_t(c)] = 1;
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
			const int b = g.nbr[size_t(6 * c + k)];
			const double d = e.first + refCost(w, c, b, naval);
			if (d < dist[size_t(b)]) { dist[size_t(b)] = d; q.push({ d, b }); }
		}
	}
	return dist;
}
std::vector<double> bellman(const World& w, int from, NavalMode naval) {
	const Geo& g = *w.geo;
	std::vector<double> dist(size_t(w.n), kInfD);
	dist[size_t(from)] = 0;
	for (bool changed = true; changed;) {
		changed = false;
		for (int c = 0; c < w.n; c++) {
			if (dist[size_t(c)] == kInfD) continue;
			for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
				const int b = g.nbr[size_t(6 * c + k)];
				const double d = dist[size_t(c)] + refCost(w, c, b, naval);
				if (d < dist[size_t(b)]) { dist[size_t(b)] = d; changed = true; }
			}
		}
	}
	return dist;
}
bool isNbr(const World& w, int a, int b) {
	const Geo& g = *w.geo;
	for (int k = 0; k < g.nbrN[size_t(a)]; k++) if (g.nbr[size_t(6 * a + k)] == b) return true;
	return false;
}
// a route is a chain of distinct neighbours from -> to; returns its cost by the reference rule (inf if not)
double routeCost(const World& w, const std::vector<int32_t>& p, int from, int to, NavalMode naval) {
	if (p.empty() || p.front() != from || p.back() != to) return kInfD;
	std::set<int> seen(p.begin(), p.end());
	if (seen.size() != p.size()) return kInfD;
	double sum = 0;
	for (size_t i = 1; i < p.size(); i++) {
		if (!w.valid(p[i]) || !isNbr(w, p[i - 1], p[i])) return kInfD;
		sum += refCost(w, p[i - 1], p[i], naval);
	}
	return sum;
}

// ------------------------------------------------------------------ world builders
// level land everywhere at +0.5, nothing on it: every step costs exactly 1
std::unique_ptr<World> flat(int L = 4) {
	auto w = state::create({ .seed = 1, .L = L, .geo = sharedGeo(L) });
	watch(*w);
	std::fill(w->elev.begin(), w->elev.end(), 0.5f);
	w->seaLevel = 0;
	w->heat = 0;
	w->phase = Phase::History;
	return w;
}
// a helper planet carrying every kind of obstacle the rule knows
std::unique_ptr<World> decorated(uint32_t seed, int L = 4) {
	auto w = makeWorld({ .L = L, .seed = seed });
	const Geo& g = *w->geo;
	Rng r(1000 + seed);
	std::vector<int32_t> cells;
	for (int c = 0; c < w->n; c++) {
		if (!seaC(*w, c)) continue;
		const double la = std::fabs(g.lat[size_t(c)]);
		if (la > 1.05) w->ice[size_t(c)] = 0.8f;                     // polar pack ice
		else if (la > 0.9) w->ice[size_t(c)] = 0.4f;                 // thin ice: still navigable
	}
	for (int k = 0; k < 14; k++) {                                   // lakes: small ponds
		const int c = r.integer(0, w->n - 1);
		if (seaC(*w, c)) continue;
		g.ring(c, 1, cells);
		for (int x : cells) if (!seaC(*w, x)) w->water[size_t(x)] = float(0.1 + 0.3 * r.f());
	}
	for (int k = 0; k < 10; k++) {                                   // lava fields
		g.ring(r.integer(0, w->n - 1), 1, cells);
		for (int x : cells) if (r.chance(0.7)) w->lava[size_t(x)] = float(0.06 + r.f());
	}
	for (int k = 0; k < 10; k++) {                                   // roads of every level
		int c = r.integer(0, w->n - 1);
		const int lvl = 1 + (k % 3);
		for (int s = 0; s < 40; s++) {
			if (!seaC(*w, c)) w->road[size_t(c)] = uint8_t(std::max<int>(w->road[size_t(c)], lvl));
			c = g.nbr[size_t(6 * c + r.integer(0, g.nbrN[size_t(c)] - 1))];
		}
	}
	for (int c = 0; c < w->n; c++)                                   // some forest, some ice on land
		if (!seaC(*w, c) && hashf(uint32_t(c), 7) < 0.15) w->ice[size_t(c)] = 0.7f;
	return w;
}
// level land with a sea channel: cells with |x| < half are sea of the given depth
std::unique_ptr<World> channel(double depth, double half, int L = 4) {
	auto w = flat(L);
	for (int c = 0; c < w->n; c++)
		if (std::fabs(w->geo->pos[size_t(3 * c)]) < half) w->elev[size_t(c)] = float(-depth);
	return w;
}
// the cell nearest to a direction
int at(const World& w, double x, double y, double z) { return w.geo->cellAt(x, y, z); }

} // namespace

// ------------------------------------------------------------------ the cost rule

DM_TEST("path: PathTune carries the documented numbers") {
	const PathTune T;
	eq(T.land, 1.0); eq(T.slopeK, 6.0); eq(T.forest, 0.4); eq(T.ice, 3.0); eq(T.lake, 2.0);
	eq(T.roadPerLevel, 0.25); eq(T.roadFloor, 0.3); eq(T.coastalDepth, 0.6); eq(T.coastal, 0.6);
	eq(T.open, 0.35); eq(T.embark, 3.0); eq(T.seaIceBlock, 0.6); eq(T.maxCost, 400.0);
	eq(path::FindOpts{}.maxCost, 400.0);
	ok(path::FindOpts{}.naval == NavalMode::Land, "default naval = land only");
	ok(std::isinf(path::kNoPath) && path::kNoPath > 0, "kNoPath is +infinity");
}

DM_TEST("path: stepCost on land (level, slope, forest, ice, lake, road, floor)") {
	auto w = flat();
	const int a = 100, b = w->geo->nbr[600];
	near(path::stepCost(*w, a, b, NavalMode::Land), 1, kEps, "level land costs 1");
	eq(path::stepCost(*w, a, a, NavalMode::Land), 0.0, "a == b costs 0");
	int far = -1;
	for (int c = 0; c < w->n; c++) if (!isNbr(*w, a, c) && c != a) { far = c; break; }
	ok(std::isinf(path::stepCost(*w, a, far, NavalMode::Land)), "not a neighbour: kNoPath");
	ok(std::isinf(path::stepCost(*w, -1, b, NavalMode::Land)) && std::isinf(path::stepCost(*w, a, w->n, NavalMode::Land)), "bad cells: kNoPath");
	w->elev[size_t(b)] = 0.75f;
	near(path::stepCost(*w, a, b, NavalMode::Land), 1 + 6 * 0.25, kEps, "uphill: +6 per unit");
	near(path::stepCost(*w, b, a, NavalMode::Land), 1 + 6 * 0.25, kEps, "downhill costs the same");
	w->elev[size_t(b)] = 0.5f;
	w->veg[size_t(b)] = 0.7f;
	near(path::stepCost(*w, a, b, NavalMode::Land), 1.4, kEps, "dense forest +0.4");
	w->veg[size_t(b)] = 0.5f;
	near(path::stepCost(*w, a, b, NavalMode::Land), 1, kEps, "light woods cost nothing");
	w->veg[size_t(b)] = 0;
	w->ice[size_t(b)] = 0.8f;
	near(path::stepCost(*w, a, b, NavalMode::Land), 4, kEps, "ice +3");
	w->ice[size_t(b)] = 0;
	w->water[size_t(b)] = 0.2f;
	near(path::stepCost(*w, a, b, NavalMode::Land), 1 + 6 * 0.2 + 2, 1e-6, "lake cell: +2, slope to its surface");
	w->water[size_t(a)] = 0.2f;
	near(path::stepCost(*w, a, b, NavalMode::Land), 3, 1e-6, "across a lake: level surface, +2");
	w->water[size_t(a)] = 0;
	w->water[size_t(b)] = 0;
	w->road[size_t(a)] = 2;
	near(path::stepCost(*w, a, b, NavalMode::Land), 1, kEps, "a road at one end only does not help");
	w->road[size_t(b)] = 1;
	near(path::stepCost(*w, a, b, NavalMode::Land), 0.75, kEps, "road: -0.25 per level (lower end)");
	w->road[size_t(b)] = 3;
	near(path::stepCost(*w, a, b, NavalMode::Land), 0.5, kEps, "road level 2");
	w->road[size_t(a)] = 3;
	near(path::stepCost(*w, a, b, NavalMode::Land), 0.3, kEps, "road 3: floor 0.3");
	for (NavalMode m : kModes) near(path::stepCost(*w, a, b, m), 0.3, kEps, "land steps do not depend on the naval mode");
}

DM_TEST("path: stepCost at sea (embark, landing, coastal shelves, open sea, sea ice, lava)") {
	auto w = flat();
	const int a = 100, b = w->geo->nbr[600], c2 = w->geo->nbr[size_t(6 * b + 2)] == a ? w->geo->nbr[size_t(6 * b + 3)] : w->geo->nbr[size_t(6 * b + 2)];
	w->elev[size_t(b)] = -0.3f;                                      // shallow sea
	ok(std::isinf(path::stepCost(*w, a, b, NavalMode::Land)), "on foot: sea impassable");
	near(path::stepCost(*w, a, b, NavalMode::Coastal), 3, kEps, "embark 3 (coastal)");
	near(path::stepCost(*w, a, b, NavalMode::Open), 3, kEps, "embark 3 (open)");
	for (NavalMode m : kModes) near(path::stepCost(*w, b, a, m), 3, kEps, "landing 3 in every mode (also on foot)");
	w->elev[size_t(c2)] = -0.4f;
	near(path::stepCost(*w, b, c2, NavalMode::Coastal), 0.6, kEps, "coastal sea step 0.6");
	near(path::stepCost(*w, b, c2, NavalMode::Open), 0.35, kEps, "open sea step 0.35");
	w->elev[size_t(c2)] = -0.6f;
	ok(std::isinf(path::stepCost(*w, b, c2, NavalMode::Coastal)), "coastal: depth 0.6 is too deep");
	near(path::stepCost(*w, b, c2, NavalMode::Open), 0.35, kEps, "open: any depth");
	w->elev[size_t(c2)] = -5;
	near(path::stepCost(*w, b, c2, NavalMode::Open), 0.35, kEps, "open: deep ocean");
	w->ice[size_t(c2)] = 0.7f;
	ok(std::isinf(path::stepCost(*w, b, c2, NavalMode::Open)), "sea ice > 0.6 impassable");
	w->ice[size_t(c2)] = 0.5f;
	near(path::stepCost(*w, b, c2, NavalMode::Open), 0.35, kEps, "thin sea ice is navigable");
	w->lava[size_t(a)] = 0.5f;
	ok(std::isinf(path::stepCost(*w, b, a, NavalMode::Open)), "lava impassable (landing)");
	near(path::stepCost(*w, a, b, NavalMode::Open), 3, kEps, "the start cell is never tested: leaving lava is allowed");
	w->lava[size_t(a)] = 0.04f;
	near(path::stepCost(*w, b, a, NavalMode::Open), 3, kEps, "a trace of lava (<= 0.05) does not block");
	ok(std::isinf(path::stepCost(*w, b, c2, NavalMode(7))) || path::stepCost(*w, b, c2, NavalMode(7)) >= 0, "bad mode never crashes");
}

DM_TEST("path: stepCost equals the reference rule on every edge of 3 decorated planets, all modes") {
	int edges = 0, bad = 0;
	for (uint32_t seed = 1; seed <= 3; seed++) {
		auto w = decorated(seed);
		const Geo& g = *w->geo;
		for (NavalMode m : kModes)
			for (int c = 0; c < w->n; c++)
				for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
					const int b = g.nbr[size_t(6 * c + k)];
					const double e = refCost(*w, c, b, m), got = path::stepCost(*w, c, b, m);
					edges++;
					if (!((std::isinf(e) && std::isinf(got)) || std::fabs(e - got) <= 1e-12)) {
						if (bad++ < 5) fail("edge " + std::to_string(c) + "->" + std::to_string(b) + " " + modeName(m) + ": expected " + std::to_string(e) + ", got " + std::to_string(got));
					}
				}
	}
	eq(bad, 0, std::to_string(edges) + " edges compared");
}

DM_TEST("path: the reference Dijkstra matches Bellman-Ford") {
	auto w = decorated(2);
	for (NavalMode m : kModes) {
		const int from = findLand(*w);
		const auto d1 = dijkstra(*w, from, m), d2 = bellman(*w, from, m);
		int diff = 0;
		for (int c = 0; c < w->n; c++)
			if (!((std::isinf(d1[size_t(c)]) && std::isinf(d2[size_t(c)])) || std::fabs(d1[size_t(c)] - d2[size_t(c)]) < 1e-9)) diff++;
		eq(diff, 0, std::string("dijkstra vs bellman, ") + modeName(m));
	}
}

// ------------------------------------------------------------------ find

DM_TEST("path: find is optimal: 200 random pairs per mode on 4 decorated planets (L 4) vs plain Dijkstra") {
	int pairs = 0, reachable = 0, budgetCut = 0, bad = 0;
	for (uint32_t seed = 1; seed <= 4; seed++) {
		auto w = decorated(seed);
		Rng r(77 + seed);
		for (NavalMode m : kModes) {
			for (int k = 0; k < 200; k++) {
				int from = r.integer(0, w->n - 1);
				if (k % 2 == 0) { for (int t = 0; t < 50 && seaC(*w, from); t++) from = r.integer(0, w->n - 1); }
				const int to = r.integer(0, w->n - 1);
				const double maxCost = (k % 5 == 4) ? 5 + 30 * r.f() : 400;
				const auto dist = dijkstra(*w, from, m);
				const double best = dist[size_t(to)];
				double cost = -1;
				const auto p = path::find(*w, from, to, { .naval = m, .maxCost = maxCost }, &cost);
				pairs++;
				const bool expect = best <= maxCost;
				if (expect) reachable++;
				if (!expect && best < kInfD) budgetCut++;
				std::string tag = std::string(modeName(m)) + " seed " + std::to_string(seed) + " " + std::to_string(from) + "->" + std::to_string(to);
				if (expect != !p.empty()) { if (bad++ < 8) fail(tag + ": reachable " + std::to_string(expect) + " but find gave " + std::to_string(p.size()) + " cells"); continue; }
				if (!expect) { if (!std::isinf(cost) && bad++ < 8) fail(tag + ": cost must be kNoPath"); continue; }
				const double rc = routeCost(*w, p, from, to, m);
				if (!(std::fabs(rc - best) <= 1e-9) || !(std::fabs(cost - best) <= 1e-9)) {
					if (bad++ < 8) fail(tag + ": optimal " + std::to_string(best) + ", route " + std::to_string(rc) + ", reported " + std::to_string(cost));
				}
				if (!(std::fabs(path::cost(*w, p, m) - best) <= 1e-9) && bad++ < 8) fail(tag + ": path::cost disagrees");
			}
		}
	}
	eq(bad, 0, std::to_string(pairs) + " pairs, " + std::to_string(reachable) + " reachable, " + std::to_string(budgetCut) + " cut by maxCost");
	ok(reachable > 300 && budgetCut > 20, "the sample exercises both outcomes");
	if (verbose()) std::printf("    find optimality: %d pairs, %d reachable, %d over budget\n", pairs, reachable, budgetCut);
}

DM_TEST("path: find takes a road at road prices (the bound respects roads)") {
	auto w = flat();
	const int a = at(*w, 1, 0, 0), b = at(*w, 0, 0, 1);
	auto plain = path::find(*w, a, b);
	ok(plain.size() > 5, "a long route exists");
	for (int c : plain) w->road[size_t(c)] = 3;
	double cost = 0;
	auto p = path::find(*w, a, b, {}, &cost);
	near(cost, 0.3 * double(plain.size() - 1), 1e-9, "the road route costs the floor per step");
	near(cost, dijkstra(*w, a, NavalMode::Land)[size_t(b)], 1e-9, "and that is optimal");
}

DM_TEST("path: find gives {from} when from == to, empty for bad cells, never crashes") {
	auto w = flat();
	double cost = -1;
	auto p = path::find(*w, 5, 5, {}, &cost);
	eq(p.size(), size_t(1)); eq(p[0], 5); eq(cost, 0.0, "cost 0");
	ok(path::find(*w, -1, 5, {}, &cost).empty() && std::isinf(cost), "bad from");
	ok(path::find(*w, 5, w->n, {}, &cost).empty() && std::isinf(cost), "bad to");
	ok(path::find(*w, 5, 9, { .naval = NavalMode::Land, .maxCost = -1 }).empty(), "negative budget");
	ok(!path::find(*w, 5, 9, { .naval = NavalMode::Land, .maxCost = std::nan("") }).empty(), "NaN budget = default");
	ok(!path::find(*w, 5, 9, { .naval = NavalMode::Land, .maxCost = kInfD }).empty(), "infinite budget");
	auto empty = state::create({ .seed = 1, .L = 2 });
	empty->geo = nullptr;
	ok(path::find(*empty, 0, 1).empty() && std::isinf(path::stepCost(*empty, 0, 1, NavalMode::Land)), "a world without geo");
	ok(std::isinf(path::cost(*w, {}, NavalMode::Land)), "cost of an empty route");
	eq(path::cost(*w, { 5 }, NavalMode::Land), 0.0, "cost of a one-cell route");
	ok(std::isinf(path::cost(*w, { 5, 6000 }, NavalMode::Land)), "cost with a bad cell");
}

DM_TEST("path: a channel: on foot no route; coastal only over shelves; open over deep water; embark and land once") {
	for (double depth : { 0.3, 2.0 }) {
		auto w = channel(depth, 0.12);
		const int a = at(*w, -1, 0.1, 0.2), b = at(*w, 1, -0.1, -0.2);
		ok(!seaC(*w, a) && !seaC(*w, b), "both ends on land");
		ok(path::find(*w, a, b).empty(), "on foot there is no route (depth " + std::to_string(depth) + ")");
		double cost = 0;
		auto pc = path::find(*w, a, b, { .naval = NavalMode::Coastal }, &cost);
		if (depth < 0.6) {
			ok(!pc.empty(), "coastal crosses a shallow channel");
			int embark = 0;
			for (size_t i = 1; i < pc.size(); i++) if (seaC(*w, pc[i]) != seaC(*w, pc[i - 1])) embark++;
			eq(embark, 2, "one embark, one landing");
			near(cost, dijkstra(*w, a, NavalMode::Coastal)[size_t(b)], 1e-9, "coastal optimal");
		} else {
			ok(pc.empty(), "coastal cannot cross deep water");
		}
		auto po = path::find(*w, a, b, { .naval = NavalMode::Open }, &cost);
		ok(!po.empty(), "open crosses any channel");
		int embark = 0;
		for (size_t i = 1; i < po.size(); i++) if (seaC(*w, po[i]) != seaC(*w, po[i - 1])) embark++;
		eq(embark, 2, "open: one embark, one landing");
		near(cost, dijkstra(*w, a, NavalMode::Open)[size_t(b)], 1e-9, "open optimal");
	}
}

DM_TEST("path: lava walls, lakes (+2 for wading) and the landmass shortcut") {
	auto w = flat();
	const Geo& g = *w->geo;
	// a lava wall along the equator with one gap
	std::vector<int> wall;
	for (int c = 0; c < w->n; c++) if (std::fabs(g.lat[size_t(c)]) < 0.08) wall.push_back(c);
	for (int c : wall) w->lava[size_t(c)] = 1;
	const int n0 = at(*w, 0.3, 0.9, 0.3), s0 = at(*w, 0.3, -0.9, 0.3);
	ok(!wall.empty(), "the wall exists");
	ok(std::isinf(dijkstra(*w, n0, NavalMode::Open)[size_t(s0)]), "the wall is closed (reference)");
	ok(path::find(*w, n0, s0).empty(), "a closed lava wall blocks every mode");
	ok(path::find(*w, n0, s0, { .naval = NavalMode::Open }).empty(), "even at sea (no sea here)");
	const int gap = at(*w, -1, 0, 0);
	std::vector<int32_t> gapCells;
	g.ring(gap, 3, gapCells);
	for (int c : gapCells) w->lava[size_t(c)] = 0;
	double cost = 0;
	auto p = path::find(*w, n0, s0, {}, &cost);
	ok(!p.empty(), "a gap lets the route through");
	bool through = false;
	for (int c : p) { ok(w->lava[size_t(c)] <= 0.05f, "never on lava"); if (std::find(gapCells.begin(), gapCells.end(), c) != gapCells.end()) through = true; }
	ok(through, "the route uses the gap");
	near(cost, dijkstra(*w, n0, NavalMode::Land)[size_t(s0)], 1e-9, "optimal around the wall");
	// a lake belt around the north: wading costs +2 per cell, so a short lake crossing beats a long detour only when cheaper
	auto w2 = flat();
	const int x0 = at(*w2, 0, 1, 0);
	std::vector<int32_t> belt;
	for (int c = 0; c < w2->n; c++) { const double la = w2->geo->lat[size_t(c)]; if (la > 0.9 && la < 1.0) belt.push_back(c); }
	for (int c : belt) w2->water[size_t(c)] = 0.2f;
	const int y0 = at(*w2, 1, 0, 0);
	auto q = path::find(*w2, x0, y0, {}, &cost);
	int wade = 0;
	for (int c : q) if (w2->water[size_t(c)] > 0.03f) wade++;
	ok(wade >= 1, "the pole is ringed by the lake: the route must wade");
	near(cost, dijkstra(*w2, x0, NavalMode::Land)[size_t(y0)], 1e-9, "lake costs are optimal");
	// the landmass shortcut: labels from fillBasics, two islands
	auto w3 = makeWorld({ .L = 4, .seed = 5, .land = 0.3 });
	int a3 = -1, b3 = -1;
	for (int c = 0; c < w3->n; c++) {
		const int id = w3->cache.landId[size_t(c)];
		if (id == 0 && a3 < 0) a3 = c;
		if (id == 1 && b3 < 0) b3 = c;
	}
	if (ok(a3 >= 0 && b3 >= 0, "two landmasses")) {
		ok(path::find(*w3, a3, b3).empty(), "on foot, different landmasses never connect");
		w3->need.derive = true;                                      // labels stale: full search, same answer
		ok(path::find(*w3, a3, b3).empty(), "same answer without the shortcut");
		ok(!path::find(*w3, a3, b3, { .naval = NavalMode::Open, .maxCost = 4000 }).empty(), "by sea they connect");
	}
}

DM_TEST("path: an impassable target gives no route at once; an impassable start can be left") {
	auto w = flat();
	const int a = at(*w, 1, 0, 0), b = at(*w, 0, 0, 1);
	w->lava[size_t(b)] = 1;
	double t0 = nowMs();
	ok(path::find(*w, a, b).empty(), "lava target");
	const double ms = nowMs() - t0;
	ok(ms < 5, "answered without a search");
	w->lava[size_t(b)] = 0;
	w->lava[size_t(a)] = 1;
	ok(!path::find(*w, a, b).empty(), "standing on lava: can still walk away");
	w->lava[size_t(a)] = 0;
	w->elev[size_t(b)] = -1;
	ok(path::find(*w, a, b).empty(), "on foot: a sea target");
	ok(path::find(*w, a, b, { .naval = NavalMode::Coastal }).empty(), "coastal: a deep target");
	ok(!path::find(*w, a, b, { .naval = NavalMode::Open }).empty(), "open: reaches it");
	w->elev[size_t(a)] = -1;                                        // a party on a cell that just drowned
	ok(!path::find(*w, a, at(*w, -1, 0, 0)).empty(), "a drowned start can still land and walk");
}

DM_TEST("path: maxCost is a hard budget") {
	auto w = flat();
	const int a = at(*w, 1, 0, 0), b = at(*w, 0, 0, 1);
	double best = 0;
	auto p = path::find(*w, a, b, {}, &best);
	ok(!p.empty());
	ok(!path::find(*w, a, b, { .naval = NavalMode::Land, .maxCost = best }).empty(), "a budget of exactly the cost is enough");
	ok(path::find(*w, a, b, { .naval = NavalMode::Land, .maxCost = best - 1e-6 }).empty(), "a hair below is not");
	// on a big world, 400 is the default: a route costing more than 400 is refused
	auto big = flat(6);
	const int n0 = at(*big, 0, 1, 0), s0 = at(*big, 0, -1, 0);   // ~166 hops on flat land
	for (int c = 0; c < big->n; c++) big->veg[size_t(c)] = 0.7f; // forest everywhere: 1.4 per step
	for (int c = 0; c < big->n; c++) big->ice[size_t(c)] = 0.8f; // and ice: 4.4 per step
	double cost = 0;
	ok(path::find(*big, n0, s0, {}, &cost).empty(), "pole to pole through ice is over the default 400");
	ok(!path::find(*big, n0, s0, { .naval = NavalMode::Land, .maxCost = 2000 }, &cost).empty(), "allowed with a bigger budget");
	ok(cost > 400, "it costs " + std::to_string(cost));
}

DM_TEST("path: poles, pentagons, an all-sea planet; results repeat exactly") {
	auto w = flat();
	for (int a = 0; a < 12; a++)
		for (int b = 0; b < 12; b++) {
			double cost = 0;
			auto p = path::find(*w, a, b, {}, &cost);
			if (!ok(!p.empty(), "pentagon " + std::to_string(a) + "->" + std::to_string(b))) return;
			near(cost, dijkstra(*w, a, NavalMode::Land)[size_t(b)], 1e-9);
		}
	auto sea = flat();
	std::fill(sea->elev.begin(), sea->elev.end(), -3.0f);
	ok(path::find(*sea, 0, 11).empty(), "all sea, on foot: nothing");
	ok(path::find(*sea, 0, 11, { .naval = NavalMode::Coastal }).empty(), "all deep sea, coastal: nothing");
	double cost = 0;
	auto p = path::find(*sea, 0, 11, { .naval = NavalMode::Open }, &cost);
	ok(!p.empty(), "all sea, open: pole to pole");
	near(cost, 0.35 * double(p.size() - 1), 1e-9, "0.35 per sea step");
	auto d = decorated(3);
	Rng r(5);
	for (int k = 0; k < 20; k++) {
		const int a = r.integer(0, d->n - 1), b = r.integer(0, d->n - 1);
		ok(path::find(*d, a, b, { .naval = NavalMode::Open }) == path::find(*d, a, b, { .naval = NavalMode::Open }), "repeatable");
	}
}

DM_TEST("path: no NaN leaks from a damaged world") {
	auto w = decorated(4);
	const float nan = std::numeric_limits<float>::quiet_NaN();
	for (int c = 0; c < w->n; c += 37) { w->elev[size_t(c)] = nan; w->water[size_t(c)] = nan; w->veg[size_t(c)] = nan; }
	const uint32_t before = hash(*w);
	Rng r(9);
	for (int k = 0; k < 200; k++) {
		const int a = r.integer(0, w->n - 1), b = r.integer(0, w->n - 1);
		for (NavalMode m : kModes) {
			double cost = 0;
			auto p = path::find(*w, a, b, { .naval = m }, &cost);
			if (!p.empty()) { if (!ok(std::isfinite(cost) && cost >= 0, "finite cost")) return; }
			else if (!ok(std::isinf(cost), "kNoPath")) return;
			const double sc = path::stepCost(*w, a, w->geo->nbr[size_t(6 * a)], m);
			if (!ok(std::isinf(sc) || (std::isfinite(sc) && sc >= 0.3), "stepCost finite >= floor or kNoPath")) return;
		}
	}
	eq(hash(*w), before, "path writes nothing into the world");
}

// ------------------------------------------------------------------ reach

DM_TEST("path: reach visits every reachable cell once, in non-decreasing cost (ties by id), with the cheapest cost") {
	for (uint32_t seed = 1; seed <= 3; seed++) {
		auto w = decorated(seed);
		for (NavalMode m : kModes) {
			const int from = findLand(*w);
			for (double maxCost : { 12.0, 400.0 }) {
				const auto dist = dijkstra(*w, from, m);
				std::vector<int> order;
				std::vector<double> costs;
				const int count = path::reach(*w, from, maxCost, m, [&](int c, double cost) { order.push_back(c); costs.push_back(cost); return true; });
				int expect = 0;
				for (int c = 0; c < w->n; c++) if (dist[size_t(c)] <= maxCost) expect++;
				const std::string tag = std::string(modeName(m)) + " seed " + std::to_string(seed) + " max " + std::to_string(maxCost);
				eq(count, expect, tag + ": count");
				eq(int(order.size()), expect, tag + ": visits");
				ok(!order.empty() && order[0] == from && costs[0] == 0, tag + ": from first at cost 0");
				std::set<int> seen(order.begin(), order.end());
				eq(seen.size(), order.size(), tag + ": each cell once");
				int bad = 0;
				for (size_t k = 0; k < order.size(); k++) {
					if (std::fabs(costs[k] - dist[size_t(order[k])]) > 1e-9) bad++;
					if (k > 0 && (costs[k] < costs[k - 1] || (costs[k] == costs[k - 1] && order[k] < order[k - 1]))) bad++;
				}
				eq(bad, 0, tag + ": cheapest costs, sorted by (cost, id)");
			}
		}
	}
}

DM_TEST("path: reach stops when the visitor returns false; bad arguments reach nothing") {
	auto w = flat();
	int calls = 0;
	const int count = path::reach(*w, 7, 400, NavalMode::Land, [&](int, double) { return ++calls < 10; });
	eq(count, 10, "count includes the stopping cell"); eq(calls, 10);
	eq(path::reach(*w, -1, 400, NavalMode::Land, [](int, double) { return true; }), 0, "bad from");
	eq(path::reach(*w, 7, -1, NavalMode::Land, [](int, double) { return true; }), 0, "negative budget");
	eq(path::reach(*w, 7, 400, NavalMode::Land, path::Visit{}), 0, "no visitor");
	eq(path::reach(*w, 7, 0, NavalMode::Land, [](int, double) { return true; }), 1, "budget 0: only the start");
	int all = path::reach(*w, 7, std::nan(""), NavalMode::Land, [](int, double) { return true; });
	eq(all, w->n, "NaN budget = default 400: the whole flat L4 planet");
}

DM_TEST("path: a reach visitor may itself search (find, reach, stepCost) without disturbing the flood") {
	auto w = decorated(2);
	const int from = findLand(*w);
	std::vector<int> plain;
	path::reach(*w, from, 30, NavalMode::Coastal, [&](int c, double) { plain.push_back(c); return true; });
	std::vector<int> nested;
	int inner = 0, innerOk = 0;
	path::reach(*w, from, 30, NavalMode::Coastal, [&](int c, double cost) {
		nested.push_back(c);
		if (nested.size() % 7 == 0) {
			double fc = 0;
			auto p = path::find(*w, from, c, { .naval = NavalMode::Coastal }, &fc);
			inner++;
			if (!p.empty() && std::fabs(fc - cost) < 1e-9) innerOk++;
			int sub = path::reach(*w, c, 3, NavalMode::Land, [&](int, double) { return true; });
			if (sub < 1) innerOk--;
		}
		return true;
	});
	ok(plain == nested, "the outer flood is unchanged");
	eq(innerOk, inner, "nested finds agree with the flood's costs");
}

DM_TEST("path: buffers are per world (L 3, L 5, L 3) and worlds do not share state") {
	auto a = flat(3), b = flat(5), c = flat(3);
	for (int k = 0; k < 3; k++) {
		ok(path::find(*a, 0, 11).size() > 1);
		ok(path::find(*b, 0, 11).size() > 1);
		ok(path::find(*c, 0, 11) == path::find(*a, 0, 11), "same answer on equal worlds");
	}
	eq(path::reach(*b, 0, 1e9, NavalMode::Land, [](int, double) { return true; }), b->n, "L 5 flood covers its world");
}

// ------------------------------------------------------------------ performance and maps

DM_TEST("path: performance at L 6 (worst-case find <= 25 ms target; asserted at 5x)") {
	auto w = makeWorld({ .L = 6, .seed = 3 });
	const Geo& g = *w->geo;
	// the two land cells of the largest landmass farthest apart (sampled)
	int la = -1, lb = -1;
	double far = -1;
	std::vector<int> land0;
	for (int c = 0; c < w->n; c++) if (w->cache.landId[size_t(c)] == 0) land0.push_back(c);
	for (size_t i = 0; i < land0.size(); i += 97)
		for (size_t j = 0; j < land0.size(); j += 89) {
			const double d = g.angle(land0[i], land0[j]);
			if (d > far) { far = d; la = land0[i]; lb = land0[j]; }
		}
	auto timeIt = [&](auto&& fn) { const double t0 = nowMs(); fn(); return nowMs() - t0; };
	size_t len = 0;
	double cost = 0;
	path::find(*w, la, lb, { .naval = NavalMode::Land, .maxCost = 1e9 });   // warm the buffers
	const double tLand = timeIt([&] { len = path::find(*w, la, lb, { .naval = NavalMode::Land, .maxCost = 1e9 }, &cost).size(); });
	const double tLand400 = timeIt([&] { path::find(*w, la, lb); });
	const int p0 = at(*w, 0, 1, 0), p1 = at(*w, 0, -1, 0);
	size_t lenOpen = 0;
	const double tOpen = timeIt([&] { lenOpen = path::find(*w, p0, p1, { .naval = NavalMode::Open, .maxCost = 1e9 }).size(); });
	// unreachable on foot with the landmass shortcut off: floods the whole start landmass
	int other = -1;
	for (int c = 0; c < w->n; c++) if (w->cache.landId[size_t(c)] == 1) { other = c; break; }
	w->need.derive = true;
	const double tNone = timeIt([&] { path::find(*w, la, other, { .naval = NavalMode::Land, .maxCost = 1e9 }); });
	w->need.derive = false;
	const double tNoneFast = timeIt([&] { path::find(*w, la, other); });
	int visited = 0;
	const double tReach = timeIt([&] { visited = path::reach(*w, p0, 1e9, NavalMode::Open, [](int, double) { return true; }); });
	// a batch of typical searches (economy / diplomacy sizes)
	Rng r(4);
	double tBatch = 0, worst = 0;
	for (int k = 0; k < 50; k++) {
		const int a = land0[size_t(r.integer(0, int(land0.size()) - 1))], b = land0[size_t(r.integer(0, int(land0.size()) - 1))];
		const double t = timeIt([&] { path::find(*w, a, b, { .naval = NavalMode::Coastal }); });
		tBatch += t;
		worst = std::max(worst, t);
	}
	std::printf("    L6 n=%d: land find across the largest continent %.2f ms (%zu cells, cost %.1f; default budget %.2f ms), "
		"open-sea pole to pole %.2f ms (%zu cells), unreachable on foot %.2f ms (shortcut %.3f ms), whole-planet reach %.2f ms (%d cells), "
		"50 coastal finds mean %.3f ms max %.2f ms\n",
		w->n, tLand, len, cost, tLand400, tOpen, lenOpen, tNone, tNoneFast, tReach, visited, tBatch / 50, worst);
	ok(len > 50, "a long continental route");
	eq(visited, w->n, "open sea reaches every cell on the helper planet (no pack ice)");
	ok(tLand <= 125 && tOpen <= 125 && tNone <= 125 && tReach <= 125, "inside 5x the 25 ms budget");
}

DM_TEST("path: maps of routes and reach costs (shots/ue-path-units-*.png)") {
	auto w = makeWorld({ .L = 6, .seed = 3 });
	const Geo& g = *w->geo;
	// decorate: a lava field and a road on the big continent
	int la = -1, lb = -1;
	double far = -1;
	std::vector<int> land0;
	for (int c = 0; c < w->n; c++) if (w->cache.landId[size_t(c)] == 0) land0.push_back(c);
	for (size_t i = 0; i < land0.size(); i += 97)
		for (size_t j = 0; j < land0.size(); j += 89) {
			const double d = g.angle(land0[i], land0[j]);
			if (d > far) { far = d; la = land0[i]; lb = land0[j]; }
		}
	std::vector<int> other;                                         // a start on another landmass
	for (int c = 0; c < w->n; c++) if (w->cache.landId[size_t(c)] == 1) other.push_back(c);
	const auto pLand = path::find(*w, la, lb, { .naval = NavalMode::Land, .maxCost = 1e9 });
	const int ob = other.empty() ? lb : other[other.size() / 2];
	const auto pCoast = path::find(*w, la, ob, { .naval = NavalMode::Coastal, .maxCost = 1e9 });
	const auto pOpen = path::find(*w, la, ob, { .naval = NavalMode::Open, .maxCost = 1e9 });
	int wet = 0;
	for (int c : pLand) if (seaC(*w, c)) wet++;
	eq(wet, 0, "the land route never touches the sea");
	ok(!pLand.empty() && !pCoast.empty() && !pOpen.empty(), "three routes");
	double cl = 0, cc = 0, co = 0;
	cl = path::cost(*w, pLand, NavalMode::Land);
	cc = path::cost(*w, pCoast, NavalMode::Coastal);
	co = path::cost(*w, pOpen, NavalMode::Open);
	ok(co <= cc + 1e-9, "open sea is never dearer than coastal for the same pair");
	std::printf("    route costs: land %.1f, coastal %.1f, open %.1f\n", cl, cc, co);
	std::vector<uint8_t> tag(size_t(w->n), 0);
	for (int c : pOpen) tag[size_t(c)] = 3;
	for (int c : pCoast) tag[size_t(c)] = 2;
	for (int c : pLand) tag[size_t(c)] = 1;
	namespace mi = dm::mapimg;
	const bool wrote = mi::custom(*w, shot("ue-path-units-routes.png"), [&](int c) {
		switch (tag[size_t(c)]) {
		case 1: return mi::Rgb{ 255, 40, 40 };     // land: red
		case 2: return mi::Rgb{ 255, 230, 0 };     // coastal: yellow
		case 3: return mi::Rgb{ 255, 0, 255 };     // open sea: magenta
		default: break;
		}
		const double e = w->elev[size_t(c)];
		if (e < w->seaLevel) { const double d = std::min(1.0, (w->seaLevel - e) / 6); return mi::Rgb{ 20, 60 + 60 * (1 - d), 120 + 100 * (1 - d) }; }
		const double h = std::min(1.0, e / 6);
		return mi::Rgb{ 70 + 120 * h, 130 + 60 * h, 60 + 120 * h };
	}, { .rivers = false });
	ok(wrote, "routes map written");
	std::vector<double> reachCost(size_t(w->n), -1);
	path::reach(*w, la, 400, NavalMode::Coastal, [&](int c, double cost) { reachCost[size_t(c)] = cost; return true; });
	double mx = 0;
	for (double v : reachCost) mx = std::max(mx, v);
	ok(mi::field(*w, reachCost, shot("ue-path-units-reach.png"), 0, mx, { .rivers = false, .marks = { la }, .dimSea = true }), "reach map written");
	std::printf("    maps: land route %zu cells, coastal %zu, open %zu; coastal reach max cost %.1f\n", pLand.size(), pCoast.size(), pOpen.size(), mx);
}
