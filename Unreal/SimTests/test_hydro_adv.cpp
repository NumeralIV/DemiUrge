// DEMIURGE — SimTests/test_hydro_adv.cpp : adversarial verification of Sim/Hydro (A4, key "hydro").
// Owner: hydro verifier. Independent of test_hydro.cpp (own brute-force checker, own worlds).
//
// Runs in `run.ps1 hydro` (every other module a stub) and in `run.ps1 all`. The tests on the game's real
// planets (real terrain::generate + climate::update) run only when Terrain and Climate are linked for real
// (`run.ps1 all`, or a build that links Terrain.cpp + Climate.cpp + Hydro.cpp); with stubs they pass empty.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Climate.h"
#include "Hydro.h"
#include "MapImg.h"
#include "Terrain.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

const float kInfF = std::numeric_limits<float>::infinity();

bool adjacent(const Geo& g, int a, int b) {
	for (int k = 0; k < g.nbrN[size_t(a)]; k++) if (g.nbr[size_t(6 * a + k)] == b) return true;
	return false;
}

// Filled surface by plain relaxation (no heap, no queue): sea = -inf, F(land) = max(elev, min F(nbr)).
// Without sea the lowest cell (ties: lowest id) is the outlet. Elev taken as Hydro sanitizes it (NaN -> 0).
std::vector<float> relaxFilled(const World& w, int* outletOut = nullptr) {
	const Geo& g = *w.geo;
	const int n = w.n;
	auto el = [&](int c) { const float e = w.elev[size_t(c)]; return std::isfinite(e) ? e : 0.0f; };
	std::vector<float> F(size_t(n), kInfF);
	bool sea = false;
	for (int c = 0; c < n; c++) if (state::isSea(w, c)) { F[size_t(c)] = -kInfF; sea = true; }
	int outlet = -1;
	if (!sea && n > 0) {
		outlet = 0;
		for (int c = 1; c < n; c++) if (el(c) < el(outlet)) outlet = c;
		F[size_t(outlet)] = el(outlet);
	}
	if (outletOut) *outletOut = outlet;
	// alternate forward / backward sweeps converge much faster than one direction
	for (int pass = 0, changed = 1; changed; pass++) {
		changed = 0;
		for (int i = 0; i < n; i++) {
			const int c = (pass & 1) ? n - 1 - i : i;
			if (state::isSea(w, c) || c == outlet) continue;
			float m = kInfF;
			for (int k = 0; k < g.nbrN[size_t(c)]; k++) m = std::min(m, F[size_t(g.nbr[size_t(6 * c + k)])]);
			const float v = std::max(el(c), m);
			if (v < F[size_t(c)]) { F[size_t(c)] = v; changed = 1; }
		}
	}
	return F;
}

double rainAt(const World& w, int c) {
	const float m = w.moist[size_t(c)];
	return (std::isfinite(m) && m > 0) ? double(m) : 0.0;
}

struct Audit {
	int land = 0, sea = 0, riverCells = 0, lakeCells = 0, lakes = 0, rivers = 0, mouths = 0, intoLakes = 0, confl = 0;
	int longest = 0, long10 = 0, long20 = 0, systems = 0;   // systems: distinct sea mouths
	double riverShare = 0, ridge = 0, fresh2 = 0, minRiverFlow = 0;
	int failures = 0;
};

// Independent check of every rule of SPEC-SIM 3.4 / BUILD A4 on an updated world.
Audit audit(const World& w, const std::string& tag) {
	Audit a;
	const Geo& g = *w.geo;
	const int n = w.n;
	int outlet = -1;
	const std::vector<float> F = relaxFilled(w, &outlet);
	const HydroTune& T = w.tune<HydroTune>();
	const double RT = hydro::riverT(w);
	auto bad = [&](bool cond, const std::string& msg) { if (!cond) { a.failures++; if (a.failures <= 8) fail(tag + ": " + msg); } return cond; };
	auto el = [&](int c) { const float e = w.elev[size_t(c)]; return std::isfinite(e) ? double(e) : 0.0; };

	for (int c = 0; c < n; c++) {
		const size_t i = size_t(c);
		if (!std::isfinite(w.flow[i]) || !std::isfinite(w.water[i])) { bad(false, "non-finite flow / water at " + std::to_string(c)); break; }
	}
	// down
	for (int c = 0; c < n; c++) {
		const int d = w.down[size_t(c)];
		if (state::isSea(w, c)) { a.sea++; bad(d == -1, "down on sea cell " + std::to_string(c)); continue; }
		a.land++;
		if (d < 0) { bad(c == outlet, "land cell without down: " + std::to_string(c)); continue; }
		if (!bad(adjacent(g, c, d), "down not a neighbour at " + std::to_string(c))) continue;
		bad(F[size_t(d)] <= F[size_t(c)], "down climbs (filled) at " + std::to_string(c));
	}
	{   // no cycles, every chain ends on sea (or the outlet): colour by "known to terminate"
		std::vector<uint8_t> st(size_t(n), 0);   // 0 unknown, 1 on stack, 2 ok
		std::vector<int> path;
		for (int c0 = 0; c0 < n; c0++) {
			if (st[size_t(c0)]) continue;
			path.clear();
			int c = c0;
			bool cyc = false;
			while (c >= 0 && st[size_t(c)] == 0) { st[size_t(c)] = 1; path.push_back(c); c = w.down[size_t(c)]; }
			if (c >= 0 && st[size_t(c)] == 1) cyc = true;
			if (c < 0 && !path.empty()) {
				const int last = path.back();
				bad(state::isSea(w, last) || last == outlet, "down chain ends on land " + std::to_string(last));
			}
			for (int x : path) st[size_t(x)] = 2;
			if (!bad(!cyc, "down[] cycle through " + std::to_string(c))) break;
		}
	}
	// flow
	std::vector<double> in(size_t(n), 0.0);
	for (int c = 0; c < n; c++) { const int d = w.down[size_t(c)]; if (d >= 0) in[size_t(d)] += w.flow[size_t(c)]; }
	for (int c = 0; c < n; c++) {
		const double want = (state::isSea(w, c) ? 0.0 : rainAt(w, c)) + in[size_t(c)];
		if (!bad(std::fabs(w.flow[size_t(c)] - want) <= 1e-4 * std::max(1.0, want), "flow != moist + inflow at " + std::to_string(c))) break;
		const int d = w.down[size_t(c)];
		if (d >= 0 && !bad(w.flow[size_t(d)] >= w.flow[size_t(c)], "flow decreases downstream at " + std::to_string(c))) break;
	}
	// water and lakes
	std::vector<int> comp(size_t(n), -1);
	for (int c0 = 0; c0 < n; c0++) {
		const float wa = w.water[size_t(c0)];
		if (!bad(wa >= 0 && wa <= T.lakeMaxWater + 1e-6, "water out of 0..2.5 at " + std::to_string(c0))) break;
		if (state::isSea(w, c0)) { if (!bad(wa == 0, "water on sea")) break; continue; }
		if (wa > 0) {
			const double d = double(F[size_t(c0)]) - el(c0);
			if (!bad(d > 0, "water outside a depression at " + std::to_string(c0))) break;
			if (!bad(std::fabs(wa - std::min(T.lakeMaxWater, d)) <= 1e-5, "water != min(2.5, filled - elev)")) break;
		}
		if (hydro::isLake(w, c0)) a.lakeCells++;
	}
	for (int c0 = 0; c0 < n; c0++) {
		if (state::isSea(w, c0) || comp[size_t(c0)] >= 0 || !(F[size_t(c0)] > el(c0))) continue;
		std::vector<int> cells{ c0 };
		comp[size_t(c0)] = c0;
		for (size_t h = 0; h < cells.size(); h++) {
			const int c = cells[h];
			for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
				const int b = g.nbr[size_t(6 * c + k)];
				if (!state::isSea(w, b) && comp[size_t(b)] < 0 && F[size_t(b)] > el(b)) { comp[size_t(b)] = c0; cells.push_back(b); }
			}
		}
		int withWater = 0, deep = 0;
		double depth = 0, moist = 0;
		for (int c : cells) {
			const double d = double(F[size_t(c)]) - el(c);
			depth = std::max(depth, d);
			if (std::min(d, T.lakeMaxWater) > T.lakeCell) deep++;
			if (w.water[size_t(c)] > 0) withWater++;
			moist += rainAt(w, c);
		}
		if (withWater == 0) continue;
		a.lakes++;
		bad(withWater == int(cells.size()), "lake covers part of its depression");
		bad(int(cells.size()) >= 3 || depth >= 0.25, "lake in a depression of < 3 cells shallower than 0.25");
		bad(deep >= 1, "a lake with no lake cell");
		(void)moist;
	}
	// rivers
	std::vector<int> nonFinal(size_t(n), 0);
	std::set<int> mouths;
	a.minRiverFlow = 1e30;
	for (size_t r = 0; r < w.rivers.size(); r++) {
		const River& rv = w.rivers[r];
		if (!bad(rv.cells.size() == rv.flow.size() && rv.cells.size() >= 2, "river shape")) continue;
		const size_t len = rv.cells.size();
		bool okChain = true;
		for (size_t i = 0; i < len && okChain; i++) {
			const int c = rv.cells[i];
			if (!bad(w.valid(c), "river cell out of range")) { okChain = false; break; }
			bad(rv.flow[i] == w.flow[size_t(c)] && std::isfinite(rv.flow[i]), "river flow != world flow");
			if (i + 1 < len) {
				okChain = bad(w.down[size_t(c)] == rv.cells[i + 1], "river does not follow down[]");
				bad(!state::isSea(w, c) && !hydro::isLake(w, c), "non-final river cell on sea / lake");
				bad(hydro::isRiver(w, c), "non-final river cell is not isRiver");
				nonFinal[size_t(c)]++;
				a.minRiverFlow = std::min(a.minRiverFlow, double(w.flow[size_t(c)]));
				if (i + 1 < len - 1) bad(!state::isSea(w, rv.cells[i + 1]) && !hydro::isLake(w, rv.cells[i + 1]), "river runs on past the sea / a lake");
			}
		}
		if (!okChain) continue;
		const int last = rv.cells.back();
		if (state::isSea(w, last)) { a.mouths++; mouths.insert(last); }
		else if (hydro::isLake(w, last)) a.intoLakes++;
		a.longest = std::max(a.longest, int(len));
		if (len >= 10) a.long10++;
		if (len >= 20) a.long20++;
	}
	for (size_t r = 0; r < w.rivers.size(); r++) {   // ends: sea, lake, outlet or a cell of another river
		const River& rv = w.rivers[r];
		if (rv.cells.size() < 2) continue;
		const int last = rv.cells.back();
		const bool conf = nonFinal[size_t(last)] == 1;
		if (conf && !state::isSea(w, last) && !hydro::isLake(w, last)) a.confl++;
		bad(state::isSea(w, last) || hydro::isLake(w, last) || last == outlet || conf, "river ends on dry ground");
		for (size_t i = 0; i + 1 < rv.cells.size(); i++) if (rv.cells[i] == last) { bad(false, "river ends on itself"); break; }
	}
	for (int c = 0; c < n; c++) {
		bad(nonFinal[size_t(c)] <= 1, "cell on two rivers");
		if (nonFinal[size_t(c)] == 1) a.riverCells++;
		if (hydro::isRiver(w, c)) bad(nonFinal[size_t(c)] == 1, "isRiver cell on no river");
	}
	// fresh: BFS from river and lake cells through every cell, cap 255
	{
		std::vector<int> dist(size_t(n), 255), q;
		for (int c = 0; c < n; c++) if (nonFinal[size_t(c)] || hydro::isLake(w, c)) { dist[size_t(c)] = 0; q.push_back(c); }
		for (size_t h = 0; h < q.size(); h++) {
			const int c = q[h];
			for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
				const int b = g.nbr[size_t(6 * c + k)];
				if (dist[size_t(b)] == 255 && dist[size_t(c)] + 1 < 255) { dist[size_t(b)] = dist[size_t(c)] + 1; q.push_back(b); }
			}
		}
		int wrong = 0, n2 = 0;
		for (int c = 0; c < n; c++) {
			if (int(w.fresh[size_t(c)]) != dist[size_t(c)]) wrong++;
			if (!state::isSea(w, c) && dist[size_t(c)] <= 2) n2++;
		}
		bad(wrong == 0, "fresh differs from BFS at " + std::to_string(wrong) + " cells");
		a.fresh2 = a.land ? double(n2) / a.land : 0;
	}
	// rivers on the low ground: share of river cells higher than their neighbours' mean
	int hi = 0;
	for (int c = 0; c < n; c++) {
		if (!nonFinal[size_t(c)]) continue;
		double m = 0;
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) m += el(g.nbr[size_t(6 * c + k)]);
		if (el(c) > m / std::max(1, int(g.nbrN[size_t(c)]))) hi++;
	}
	a.ridge = a.riverCells ? double(hi) / a.riverCells : 0;
	a.riverShare = a.land ? double(a.riverCells) / a.land : 0;
	a.rivers = int(w.rivers.size());
	a.systems = int(mouths.size());
	(void)RT;
	return a;
}

void show(const std::string& tag, const World& w, const Audit& a, double ms = -1, double mx = -1) {
	std::printf("  %-18s rivers %4d (>=10: %3d, >=20: %3d, longest %3d)  river cells %5d (%.1f %% land)  sea mouths %3d (%d systems)"
	            "  into lakes %3d  confluences %3d  lakes %3d (%4d cells)  fresh<=2 %.0f %%  ridge %.0f %%",
		tag.c_str(), a.rivers, a.long10, a.long20, a.longest, a.riverCells, 100 * a.riverShare, a.mouths, a.systems, a.intoLakes,
		a.confl, a.lakes, a.lakeCells, 100 * a.fresh2, 100 * a.ridge);
	if (ms >= 0) std::printf("  update mean %.2f ms max %.2f ms", ms, mx);
	std::printf("  (riverT %.2f)\n", hydro::riverT(w));
}

// Everything hydro::update must leave alone, as raw bytes.
struct Frozen {
	std::vector<std::vector<double>> cells;
	uint32_t rngS = 0;
	double year = 0, seaLevel = 0, heat = 0, co2 = 0;
	int64_t tick = 0;
	size_t nSet = 0, nCul = 0, nUnit = 0, nDis = 0, nChron = 0, nNum = 0;
	std::vector<int32_t> landId;
	bool needDerive = false, needClimate = false, dElev = false, dColor = false, dObjects = false, dRoads = false;
};
Frozen freeze(const World& w) {
	Frozen f;
	for (int i = 0; i < state::kCellFieldCount; i++) {
		const std::string nm = state::kCellFields[i].name;
		if (nm == "water" || nm == "flow" || nm == "down" || nm == "fresh") continue;
		const CellArray arr = state::cellArray(w, i);
		std::vector<double> v(arr.size);
		for (size_t c = 0; c < arr.size; c++) v[c] = arr.get(c);
		f.cells.push_back(std::move(v));
	}
	f.rngS = w.rng.s; f.year = w.year; f.seaLevel = w.seaLevel; f.heat = w.heat; f.co2 = w.co2; f.tick = w.tick;
	f.nSet = w.settlements.size(); f.nCul = w.cultures.size(); f.nUnit = w.units.size(); f.nDis = w.disasters.size();
	f.nChron = w.chronicle.size(); f.nNum = w.vars.num.size(); f.landId = w.cache.landId;
	f.needDerive = w.need.derive; f.needClimate = w.need.climate;
	f.dElev = w.dirty.elev; f.dColor = w.dirty.color; f.dObjects = w.dirty.objects; f.dRoads = w.dirty.roads;
	return f;
}
bool sameFrozen(const Frozen& a, const Frozen& b) {
	// NaN-safe comparison of the cell arrays (a NaN stays a NaN)
	if (a.cells.size() != b.cells.size()) return false;
	for (size_t i = 0; i < a.cells.size(); i++) {
		if (a.cells[i].size() != b.cells[i].size()) return false;
		for (size_t c = 0; c < a.cells[i].size(); c++) {
			const double x = a.cells[i][c], y = b.cells[i][c];
			if (!(x == y || (std::isnan(x) && std::isnan(y)))) return false;
		}
	}
	return a.rngS == b.rngS && a.year == b.year && (a.seaLevel == b.seaLevel || (std::isnan(a.seaLevel) && std::isnan(b.seaLevel))) &&
	       a.heat == b.heat && a.co2 == b.co2 && a.tick == b.tick && a.nSet == b.nSet && a.nCul == b.nCul && a.nUnit == b.nUnit &&
	       a.nDis == b.nDis && a.nChron == b.nChron && a.nNum == b.nNum && a.landId == b.landId && a.needDerive == b.needDerive &&
	       a.needClimate == b.needClimate && a.dElev == b.dElev && a.dColor == b.dColor && a.dObjects == b.dObjects && a.dRoads == b.dRoads;
}

bool sameHydro(const World& a, const World& b) {
	if (a.water != b.water || a.flow != b.flow || a.down != b.down || a.fresh != b.fresh || a.rivers.size() != b.rivers.size()) return false;
	for (size_t r = 0; r < a.rivers.size(); r++)
		if (a.rivers[r].cells != b.rivers[r].cells || a.rivers[r].flow != b.rivers[r].flow) return false;
	return true;
}

bool realWorldsAvailable() { return !stub::stubbed("terrain") && !stub::stubbed("climate"); }

// The game's planet: real terrain and climate at sea level `sea` (History, calibrated as at the start of history).
std::unique_ptr<World> realPlanet(uint32_t seed, int L = 6, double sea = 0) {
	state::CreateOpts co;
	co.seed = seed;
	co.L = L;
	co.geo = sharedGeo(L);
	auto w = state::create(co);
	watch(*w);
	terrain::generate(*w);
	w->seaLevel = sea;
	w->phase = Phase::History;
	w->heat = 0;
	terrain::derive(*w);
	climate::calibrate(*w);
	climate::update(*w);
	return w;
}

// A shaped world (no other module): elev from a function of the cell direction, moist m on land.
std::unique_ptr<World> shapedWorld(int L, const std::function<double(double, double, double)>& f, double m, double sea = 0) {
	state::CreateOpts co;
	co.L = L;
	co.geo = sharedGeo(L);
	auto w = state::create(co);
	watch(*w);
	w->seaLevel = sea;
	w->phase = Phase::History;
	const Geo& g = *w->geo;
	for (int c = 0; c < w->n; c++) {
		const size_t i = size_t(c);
		w->elev[i] = float(f(g.pos[3 * i], g.pos[3 * i + 1], g.pos[3 * i + 2]));
		w->moist[i] = float(w->elev[i] < sea ? 1.0 : m);
	}
	return w;
}

double lowestOutside(const World& w, const std::vector<int>& cells) {
	const Geo& g = *w.geo;
	const std::set<int> in(cells.begin(), cells.end());
	double r = 1e9;
	for (int c : cells)
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
			const int b = g.nbr[size_t(6 * c + k)];
			if (!in.count(b)) r = std::min(r, double(w.elev[size_t(b)]));
		}
	return r;
}

} // namespace

// ============================================================================================================
// Helper worlds (always run)
// ============================================================================================================

DM_TEST("hydro adv: helper worlds L 4 x 10 seeds and L 6 x 4 more seeds: independent audit") {
	for (uint32_t seed = 1; seed <= 10; seed++) {
		auto w = makeWorld({ .L = 4, .seed = seed });
		hydro::update(*w);
		const Audit a = audit(*w, "L4 s" + std::to_string(seed));
		if (a.failures) break;
	}
	for (uint32_t seed = 4; seed <= 7; seed++) {
		auto w = makeWorld({ .L = 6, .seed = seed, .land = 0.3 + 0.05 * seed });
		hydro::update(*w);
		const Audit a = audit(*w, "L6 s" + std::to_string(seed));
		show("helper L6 s" + std::to_string(seed), *w, a);
		if (a.failures) break;
	}
}

DM_TEST("hydro adv: ownership: only water / flow / down / fresh / rivers and need.hydro, need.life, dirty.rivers, dirty.water") {
	auto w = makeWorld({ .L = 5, .seed = 11 });
	state::newCulture(*w);
	state::newSettlement(*w, findLand(*w), 0);
	w->vars.num["hydro_probe"] = 1;
	w->need = Need{ true, true, true, false };
	w->dirty = Dirty{ false, false, false, false, false, false };
	const Frozen before = freeze(*w);
	const auto evs = capture(*w, [&] { hydro::update(*w); });
	eq(evs.size(), size_t(0), "events emitted");
	ok(sameFrozen(before, freeze(*w)), "something outside Hydro's fields changed");
	ok(!w->need.hydro && w->need.life && w->need.derive && w->need.climate, "need flags");
	ok(w->dirty.water && w->dirty.rivers, "dirty flags");
	// every derived array hydro writes is sized n and unchanged in size
	ok(w->water.size() == size_t(w->n) && w->flow.size() == size_t(w->n) && w->down.size() == size_t(w->n) && w->fresh.size() == size_t(w->n), "sizes");
}

DM_TEST("hydro adv: determinism: same inputs -> same world hash and rivers; no stale scratch across inputs or tuning") {
	auto a = makeWorld({ .L = 6, .seed = 3 });
	auto b = makeWorld({ .L = 6, .seed = 3, .geo = Geo::build(6) });
	hydro::update(*a);
	hydro::update(*b);
	eq(hash(*a), hash(*b), "hash of two identical worlds");
	ok(sameHydro(*a, *b), "identical hydro output");
	// b goes through other states first: different sea levels, a flood of NaN, other tuning, then back
	const std::vector<float> elev0 = b->elev, moist0 = b->moist;
	for (double sea : { -6.0, 2.5, -20.0, 50.0 }) { b->seaLevel = sea; hydro::update(*b); }
	b->seaLevel = 0;
	b->tune<HydroTune>().pull = 0;
	b->tune<HydroTune>().creek = 1;
	hydro::update(*b);
	b->tune<HydroTune>() = HydroTune{};
	for (int c = 0; c < b->n; c += 7) b->elev[size_t(c)] = std::numeric_limits<float>::quiet_NaN();
	hydro::update(*b);
	b->elev = elev0;
	b->moist = moist0;
	hydro::update(*b);
	ok(sameHydro(*a, *b), "same output after a detour through other inputs (stale scratch?)");
	eq(hash(*a), hash(*b), "same hash after the detour");
	// interleaving two worlds of different sizes
	auto c4 = makeWorld({ .L = 4, .seed = 3 });
	auto c4b = makeWorld({ .L = 4, .seed = 3 });
	hydro::update(*c4);
	hydro::update(*a);
	hydro::update(*c4b);
	ok(sameHydro(*c4, *c4b), "L4 world unaffected by an L6 update in between");
}

DM_TEST("hydro adv: robustness: NaN / infinite elev, NaN sea level, NaN moist, wrong-sized arrays, bad cells") {
	// NaN and infinite elevations on some cells
	{
		auto w = makeWorld({ .L = 5, .seed = 4 });
		const float nanf = std::numeric_limits<float>::quiet_NaN();
		for (int c = 0; c < w->n; c += 13) w->elev[size_t(c)] = nanf;
		for (int c = 5; c < w->n; c += 29) w->elev[size_t(c)] = (c & 1) ? kInfF : -kInfF;
		for (int c = 3; c < w->n; c += 17) w->moist[size_t(c)] = nanf;
		hydro::update(*w);
		bool fin = true;
		for (int c = 0; c < w->n; c++) fin = fin && std::isfinite(w->water[size_t(c)]) && std::isfinite(w->flow[size_t(c)]) && w->down[size_t(c)] >= -1 && w->down[size_t(c)] < w->n;
		ok(fin, "finite output with bad elev / moist");
		for (const River& r : w->rivers) for (float f : r.flow) if (!std::isfinite(f)) { fail("non-finite river flow"); break; }
		// down chains still terminate
		int stuck = 0;
		for (int c = 0; c < w->n; c++) { int x = c, s = 0; while (x >= 0 && s <= w->n) { x = w->down[size_t(x)]; s++; } if (s > w->n) stuck++; }
		eq(stuck, 0, "cycles with bad elev");
	}
	// NaN sea level: no cell is sea (elev < NaN is false), so the world is all land with an outlet
	{
		auto w = makeWorld({ .L = 4, .seed = 2 });
		w->seaLevel = std::numeric_limits<double>::quiet_NaN();
		hydro::update(*w);
		int outlets = 0;
		for (int c = 0; c < w->n; c++) outlets += w->down[size_t(c)] < 0 ? 1 : 0;
		eq(outlets, 1, "NaN sea: one outlet");
		for (int c = 0; c < w->n; c++) if (!std::isfinite(w->flow[size_t(c)]) || !std::isfinite(w->water[size_t(c)])) { fail("NaN sea: non-finite"); break; }
	}
	// wrong-sized arrays: no crash, nothing written out of range
	{
		auto w = makeWorld({ .L = 4, .seed = 2 });
		w->moist.resize(10);
		hydro::update(*w);
		ok(w->rivers.empty(), "bad moist size: no rivers");
		ok(!w->need.hydro, "bad moist size: need.hydro still cleared (no retry loop)");
	}
	// bad cell ids on the queries
	{
		auto w = makeWorld({ .L = 4, .seed = 2 });
		hydro::update(*w);
		ok(!hydro::isRiver(*w, -1) && !hydro::isRiver(*w, w->n) && !hydro::isRiver(*w, 1 << 30), "isRiver bad ids");
		ok(!hydro::isLake(*w, -1) && !hydro::isLake(*w, w->n), "isLake bad ids");
		// a world whose derived arrays were never filled (fresh default 255): isRiver false everywhere
		auto v = state::create({ .L = 3 });
		bool any = false;
		for (int c = 0; c < v->n; c++) any = any || hydro::isRiver(*v, c);
		ok(!any, "isRiver before any update");
	}
	// absurd tuning (Balance could set anything): finite, no crash
	{
		auto w = makeWorld({ .L = 4, .seed = 5 });
		HydroTune& T = w->tune<HydroTune>();
		T.riverRef = std::numeric_limits<double>::quiet_NaN(); T.riverMin = -5; T.pull = 1e30; T.pullAt = 0; T.jitter = 7;
		T.creek = std::numeric_limits<double>::infinity(); T.head = -1; T.lakeMinCells = -3; T.freshCap = 9999; T.arid = std::numeric_limits<double>::quiet_NaN();
		hydro::update(*w);
		ok(std::isfinite(hydro::riverT(*w)), "riverT finite with NaN riverRef");
		ok(checkFinite(*w).empty(), "finite world with absurd tuning: " + checkFinite(*w));
		int stuck = 0;
		for (int c = 0; c < w->n; c++) { int x = c, s = 0; while (x >= 0 && s <= w->n) { x = w->down[size_t(x)]; s++; } if (s > w->n) stuck++; }
		eq(stuck, 0, "cycles with absurd tuning");
	}
}

DM_TEST("hydro adv: L 0..3 tiny planets, a flat all-land wet planet, poles") {
	for (int L = 0; L <= 3; L++) {
		auto w = makeWorld({ .L = L, .seed = 1 });
		hydro::update(*w);
		audit(*w, "L" + std::to_string(L));
	}
	// perfectly flat, all land, wet: everything drains to cell 0 across the flat (no lakes: no depression)
	{
		auto w = shapedWorld(4, [](double, double, double) { return 1.0; }, 0.5, -5);
		hydro::update(*w);
		const Audit a = audit(*w, "flat");
		eq(a.lakeCells, 0, "flat: no lakes");
		near(w->flow[0], 0.5 * w->n, 1e-3 * w->n, "flat: all rain at the outlet (cell 0)");
		ok(a.rivers >= 1, "flat: rivers to the outlet");
	}
	// a cone peaked at the north pole and a pit exactly on the south pole cell
	{
		auto w = shapedWorld(5, [](double, double y, double) { return y > -0.2 ? 0.2 + 4.0 * (y + 0.2) : -2.0; }, 0.6);
		const int np = w->geo->cellAt(0, 1, 0);
		hydro::update(*w);
		Audit a = audit(*w, "north pole cone");
		ok(w->down[size_t(np)] >= 0, "the pole drains");
		ok(a.mouths >= 3, "rivers run off the cone in several directions: " + std::to_string(a.mouths));
		auto v = shapedWorld(5, [](double, double y, double) { return y > 0.6 ? -2.0 : 1.0 - y; }, 0.5);
		const int sp = v->geo->cellAt(0, -1, 0);
		v->elev[size_t(sp)] = float(lowestOutside(*v, { sp }) - 0.6);
		hydro::update(*v);
		audit(*v, "south pole pit");
		ok(hydro::isLake(*v, sp), "a deep wet pit on the pole is a lake");
	}
}

DM_TEST("hydro adv: lake rule edges: moist 0.15 threshold, 3 cells vs 2, depth 0.25, a big dry basin, nested basins") {
	// a basin on a gentle slope far from the sea; the basin cells get `bm` moist, the rest 0.5
	auto slope = [](double, double y, double) { return y < -0.5 ? -2.0 : 0.3 + 2.0 * (y + 0.5); };
	auto make = [&](const std::vector<int>& cellsIn, double depth, double bm, std::unique_ptr<World>& out) {
		out = shapedWorld(5, slope, 0.5);
		const double rim = lowestOutside(*out, cellsIn);
		for (int x : cellsIn) { out->elev[size_t(x)] = float(rim - depth); out->moist[size_t(x)] = float(bm); }
		hydro::update(*out);
	};
	auto probe = shapedWorld(5, slope, 0.5);
	const Geo& g = *probe->geo;
	const int c = g.cellAt(0.3, 0.6, 0.74);
	int a1 = g.nbr[size_t(6 * c)], a2 = -1;
	for (int k = 0; k < g.nbrN[size_t(c)]; k++) { const int m = g.nbr[size_t(6 * c + k)]; if (m != a1 && adjacent(g, a1, m)) { a2 = m; break; } }
	if (!ok(a2 >= 0, "triangle")) return;
	std::unique_ptr<World> w;
	// small catchments: these basins collect only their own rain plus a little slope above them
	make({ c, a1, a2 }, 0.1, 0.16, w);
	audit(*w, "3 cells moist 0.16");
	ok(hydro::isLake(*w, c), "3 shallow cells, mean moist 0.16: a lake");
	make({ c, a1, a2 }, 0.1, 0.14, w);
	audit(*w, "3 cells moist 0.14");
	const bool dry3 = hydro::isLake(*w, c);
	std::printf("  3 shallow cells at mean moist 0.14: lake = %d, flow into the basin %.2f (riverT %.2f)\n", int(dry3), double(w->flow[size_t(c)]), hydro::riverT(*w));
	if (dry3) ok(w->flow[size_t(c)] > hydro::riverT(*w), "a dry basin is a lake only when a river feeds it");
	make({ c, a1 }, 0.2, 0.5, w);
	audit(*w, "2 cells 0.2 deep");
	ok(!hydro::isLake(*w, c) && w->water[size_t(c)] == 0, "2 cells, 0.2 deep: no lake");
	make({ c, a1 }, 0.25, 0.5, w);
	audit(*w, "2 cells 0.25 deep");
	ok(hydro::isLake(*w, c) && hydro::isLake(*w, a1), "2 cells, 0.25 deep: a lake");
	make({ c }, 0.249, 0.5, w);
	ok(!hydro::isLake(*w, c), "1 cell 0.249 deep: no lake");
	// a big dry basin (mean moist 0.05) with a small dry catchment: no lake
	{
		std::vector<int> big;
		g.ring(c, 3, big);
		make(big, 0.8, 0.05, w);
		audit(*w, "big dry basin");
		int lakeCells = 0;
		for (int x : big) lakeCells += hydro::isLake(*w, x) ? 1 : 0;
		std::printf("  big dry basin (%zu cells, 0.8 deep, moist 0.05): lake cells %d, inflow %.2f\n", big.size(), lakeCells, double(w->flow[size_t(c)]));
	}
	// nested: a deep pit inside a shallow wide basin: one lake, water = filled - elev everywhere
	{
		std::vector<int> big;
		g.ring(c, 2, big);
		auto v = shapedWorld(5, slope, 0.5);
		const double rim = lowestOutside(*v, big);
		for (int x : big) v->elev[size_t(x)] = float(rim - 0.1);
		v->elev[size_t(c)] = float(rim - 1.5);
		hydro::update(*v);
		audit(*v, "nested");
		near(v->water[size_t(c)], 1.5, 1e-4, "nested: the pit holds the full depth");
		int lakeCells = 0;
		for (int x : big) lakeCells += hydro::isLake(*v, x) ? 1 : 0;
		eq(lakeCells, int(big.size()), "nested: the whole basin is one lake");
	}
}

DM_TEST("hydro adv: a river entering a lake ends there, a river leaves at the outlet, and a lake 'spills' into the sea") {
	// a long valley running down to the sea at -z, with a deep basin half way
	auto f = [](double x, double y, double z) {
		(void)x;
		if (z < -0.6) return -2.0;
		const double valley = 1.5 * std::min(1.0, std::fabs(y) / 0.25);
		return 0.2 + 1.5 * (z + 0.6) + valley;
	};
	auto w = shapedWorld(5, f, 0.5);
	const Geo& g = *w->geo;
	const int mid = g.cellAt(0.8, 0.0, 0.2);
	std::vector<int> basin;
	g.ring(mid, 1, basin);
	const double rim = lowestOutside(*w, basin);
	for (int x : basin) w->elev[size_t(x)] = float(rim - 0.5);
	hydro::update(*w);
	const Audit a = audit(*w, "valley lake");
	show("valley lake", *w, a);
	ok(hydro::isLake(*w, mid), "the basin is a lake");
	bool into = false, outOf = false;
	for (const River& r : w->rivers) {
		if (hydro::isLake(*w, r.cells.back()) && std::count(basin.begin(), basin.end(), r.cells.back())) into = true;
		const int s = r.cells[0];
		for (int k = 0; k < g.nbrN[size_t(s)]; k++) {
			const int b = g.nbr[size_t(6 * s + k)];
			if (hydro::isLake(*w, b) && w->down[size_t(b)] == s) outOf = true;
		}
	}
	ok(into, "a river ends in the lake");
	ok(outOf, "a river starts on the lake's outlet");
}

DM_TEST("hydro adv: sea level sweep (genesis comets to a risen sea): invariants at every level, rivers move with the coast") {
	auto w = makeWorld({ .L = 5, .seed = 8 });
	for (double sea : { -9.0, -6.0, -4.5, -3.5, -2.0, -1.0, 0.0, 0.3, 0.6, 1.5 }) {
		w->seaLevel = sea;
		fillBasics(*w);
		hydro::update(*w);
		const Audit a = audit(*w, "sea " + std::to_string(sea));
		if (a.failures) break;
	}
	// a slow rise like Climate.yearly (0.005 / yr) re-run every 8 ticks: nothing accumulates
	w->seaLevel = 0;
	fillBasics(*w);
	hydro::update(*w);
	const size_t r0 = w->rivers.size();
	for (int i = 0; i < 40; i++) {
		w->seaLevel += 0.01;
		hydro::update(*w);
	}
	const Audit a = audit(*w, "after rise");
	std::printf("  rivers at sea 0: %zu; after +0.4: %zu\n", r0, w->rivers.size());
	ok(w->rivers.size() < 2 * r0 + 10 && w->rivers.size() + 10 > r0 / 2, "river count stays in proportion");
	(void)a;
}

DM_TEST("hydro adv: drowned trunks: above vars.seaBase the tributaries of a drowned river stay rivers; at the datum nothing changes") {
	for (uint32_t seed : { 2u, 5u, 9u }) {
		auto on = makeWorld({ .L = 5, .seed = seed });
		auto off = makeWorld({ .L = 5, .seed = seed });
		off->tune<HydroTune>().estuary = false;
		hydro::update(*on);
		hydro::update(*off);
		ok(sameHydro(*on, *off), "at the datum the rule changes nothing");
		// no datum (genesis): the rule is off
		on->vars.seaBase.reset();
		on->seaLevel = 0.3;
		off->seaLevel = 0.3;
		hydro::update(*on);
		hydro::update(*off);
		ok(sameHydro(*on, *off), "no seaBase: the rule is off");
		on->vars.seaBase = 0.0;
		on->seaLevel = 0.3;
		hydro::update(*on);
		const Audit a = audit(*on, "drowned s" + std::to_string(seed));
		const Audit b = audit(*off, "plain s" + std::to_string(seed));
		ok(a.riverCells >= b.riverCells, "the rule only adds rivers");
		int lost = 0;   // every river cell of the plain rule is still a river cell
		for (int c = 0; c < on->n; c++) if (hydro::isRiver(*off, c) && !hydro::isRiver(*on, c)) lost++;
		eq(lost, 0, "river cells the rule removed");
		ok(on->down == off->down && on->flow == off->flow && on->water.size() == off->water.size(), "drainage and discharge unchanged by the rule");
		auto fresh = makeWorld({ .L = 5, .seed = seed });
		fresh->seaLevel = 0.3;
		hydro::update(*fresh);
		ok(sameHydro(*fresh, *on) && hash(*fresh) == hash(*on), "the two-pass path is deterministic and keeps no state");
		std::printf("  L5 s%u sea +0.3 over seaBase 0: river cells %d with the rule, %d without\n", seed, a.riverCells, b.riverCells);
	}
}

// ============================================================================================================
// The game's real planets (real terrain + climate; `run.ps1 all` or a build linking them)
// ============================================================================================================

DM_TEST("hydro adv: real planets L 6, seeds 1-6: audit, budget, river numbers, maps") {
	if (!realWorldsAvailable()) { std::printf("  (terrain / climate stubbed: real-planet test skipped)\n"); return; }
	for (uint32_t seed = 1; seed <= 6; seed++) {
		auto w = realPlanet(seed);
		const auto evs = capture(*w, [&] { hydro::update(*w); });
		eq(evs.size(), size_t(0), "no events");
		double sum = 0, mx = 0;
		const int reps = 12;
		for (int i = 0; i < reps; i++) {
			const double t0 = nowMs();
			hydro::update(*w);
			const double dt = nowMs() - t0;
			sum += dt;
			mx = std::max(mx, dt);
		}
		const Audit a = audit(*w, "real s" + std::to_string(seed));
		show("real L6 s" + std::to_string(seed), *w, a, sum / reps, mx);
		ok(sum / reps <= 15, "update mean within the 15 ms budget: " + std::to_string(sum / reps));
		ok(a.long10 >= 10, "real s" + std::to_string(seed) + ": long rivers " + std::to_string(a.long10));
		ok(a.mouths >= 15, "real s" + std::to_string(seed) + ": mouths " + std::to_string(a.mouths));
		ok(a.confl >= 10, "real s" + std::to_string(seed) + ": confluences " + std::to_string(a.confl));
		ok(a.lakes >= 1, "real s" + std::to_string(seed) + ": lakes");
		ok(a.ridge < 0.2, "real s" + std::to_string(seed) + ": low ground " + std::to_string(a.ridge));
		ok(a.riverShare > 0.015 && a.riverShare < 0.12, "real s" + std::to_string(seed) + ": river share " + std::to_string(a.riverShare));
		if (seed <= 3) mapimg::elev(*w, shot("ue-hydro-adv-real-s" + std::to_string(seed) + ".png"));
		if (seed == 1) {
			mapimg::field(*w, "moist", shot("ue-hydro-adv-real-s1-moist.png"), 0, 1, { .dimSea = true });
		}
	}
}

DM_TEST("hydro adv: real planets: genesis sea levels (molten -9, first comet -6, gate -3.5) and a risen sea; budget") {
	if (!realWorldsAvailable()) return;
	for (double sea : { -9.0, -6.0, -3.5, 0.6 }) {
		auto w = realPlanet(2, 6, sea);
		w->phase = sea < 0 ? Phase::Water : Phase::History;
		hydro::update(*w);
		double mx = 0, sum = 0;
		for (int i = 0; i < 6; i++) { const double t0 = nowMs(); hydro::update(*w); const double dt = nowMs() - t0; sum += dt; mx = std::max(mx, dt); }
		const std::string tag = "real s2 sea " + std::to_string(sea).substr(0, 5);
		const Audit a = audit(*w, tag);
		show(tag, *w, a, sum / 6, mx);
		ok(mx <= 5 * 15, tag + ": within 5x budget");
		if (sea == -6.0) mapimg::elev(*w, shot("ue-hydro-adv-real-s2-sea-6.png"));
	}
}

DM_TEST("hydro adv: real planets L 4 and 5: the river network scales with the cell count") {
	if (!realWorldsAvailable()) return;
	for (int L : { 4, 5 }) {
		auto w = realPlanet(1, L);
		hydro::update(*w);
		const Audit a = audit(*w, "real L" + std::to_string(L));
		show("real L" + std::to_string(L) + " s1", *w, a);
		ok(a.rivers >= 3, "rivers at L " + std::to_string(L));
		ok(a.riverShare < 0.2, "river share at L " + std::to_string(L));
		if (L == 4) mapimg::elev(*w, shot("ue-hydro-adv-real-L4.png"));
	}
}

DM_TEST("hydro adv: real planet: determinism (hash) and a crater on a river makes a lake") {
	if (!realWorldsAvailable()) return;
	auto a = realPlanet(4);
	auto b = realPlanet(4);
	hydro::update(*a);
	hydro::update(*b);
	eq(hash(*a), hash(*b), "hash");
	ok(sameHydro(*a, *b), "same rivers");
	// pick the cell half way down the largest river and dig a crater there with the real Terrain
	if (!ok(!a->rivers.empty(), "rivers")) return;
	// (high enough that the 1.6-deep bowl stays above the sea: a crater below sea level is an inlet, not a lake)
	int c = -1;
	for (const River& r : a->rivers)
		for (size_t i = 0; i + 1 < r.cells.size(); i++)
			if (a->elev[size_t(r.cells[i])] > 2.2f && (c < 0 || a->flow[size_t(r.cells[i])] > a->flow[size_t(c)])) c = r.cells[i];
	if (!ok(c >= 0, "a river cell above 2.2")) return;
	const double e0 = a->elev[size_t(c)];
	terrain::crater(*a, c, 0.03, 1.6);
	std::printf("  crater: elev %.2f -> %.2f, sea %.2f, rim %.2f\n", e0, double(a->elev[size_t(c)]), a->seaLevel, lowestOutside(*a, { c }));
	terrain::derive(*a);
	hydro::update(*a);
	audit(*a, "crater");
	std::printf("  crater on the main river at cell %d: water %.2f, lake %d\n", c, double(a->water[size_t(c)]), int(hydro::isLake(*a, c)));
	ok(hydro::isLake(*a, c), "a fresh crater on a river fills as a lake");
}

DM_TEST("hydro adv: real planets: a rising sea (seaBase 0, as Climate.yearly moves it): audit, river density, upland rivers, budget") {
	if (!realWorldsAvailable()) return;
	for (uint32_t seed : { 1u, 2u, 3u }) {
		auto w = realPlanet(seed);
		double share0 = 0;
		int upland0 = 0;
		for (double sea : { 0.0, 0.2, 0.4, 0.6, -0.3 }) {
			w->seaLevel = sea;
			terrain::derive(*w);
			climate::update(*w);
			hydro::update(*w);
			double sum = 0, mx = 0;
			for (int i = 0; i < 6; i++) { const double t0 = nowMs(); hydro::update(*w); const double dt = nowMs() - t0; sum += dt; mx = std::max(mx, dt); }
			const Audit a = audit(*w, "rise s" + std::to_string(seed) + " sea " + std::to_string(sea));
			int onIce = 0, low = 0;
			for (int c = 0; c < w->n; c++) {
				if (!hydro::isRiver(*w, c)) continue;
				if (w->ice[size_t(c)] > 0.5f) onIce++;
				if (w->elev[size_t(c)] < 0.6f) low++;
			}
			const int upland = a.riverCells - low;
			std::printf("  s%u sea %+.1f: land %5d  rivers %3d  river cells %4d (%.1f %% of land; %4d below +0.6, %3d on ice > 0.5)  lakes %2d"
			            "  update mean %.2f max %.2f ms\n", seed, sea, a.land, a.rivers, a.riverCells, 100 * a.riverShare, low, onIce, a.lakes, sum / 6, mx);
			ok(sum / 6 <= 15, "update within the 15 ms budget with the sea off its datum");
			ok(onIce <= a.riverCells / 8 + 2, "few river cells on ice sheets");
			if (sea == 0) { share0 = a.riverShare; upland0 = upland; }
			// the sea drowns the lower courses; above +0.6 it is the same water in the same beds
			if (sea == 0.2) ok(a.riverShare >= 0.7 * share0, "sea +0.2 keeps the river density: " + std::to_string(a.riverShare) + " vs " + std::to_string(share0));
			if (sea == 0.2 || sea == 0.4) ok(upland >= 0.85 * upland0, "the upland rivers survive a rise: " + std::to_string(upland) + " vs " + std::to_string(upland0));
			if (seed == 1 && sea == 0.4) mapimg::elev(*w, shot("ue-hydro-adv-real-s1-sea+0.4.png"));
			if (a.failures) break;
		}
	}
	// the drowned-trunk rule off: the old behaviour (reported for comparison)
	auto w = realPlanet(1);
	w->tune<HydroTune>().estuary = false;
	w->seaLevel = 0.2;
	terrain::derive(*w);
	climate::update(*w);
	hydro::update(*w);
	const Audit a = audit(*w, "estuary off");
	std::printf("  s1 sea +0.2 with tune.estuary = false: rivers %d, river cells %d (%.1f %% of land)\n", a.rivers, a.riverCells, 100 * a.riverShare);
}
