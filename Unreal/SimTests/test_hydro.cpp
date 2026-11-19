// DEMIURGE — SimTests/test_hydro.cpp : tests for Sim/Hydro (A4, key "hydro"). Owner: hydro engineer.
// Acceptance (BUILD.md A4): on helper worlds, 10 seeds: following down[] from every land cell reaches a sea
// cell without cycles; flow never decreases downstream and equals the sum of upstream moist; water >= 0, only
// in depressions of >= 3 cells or depth >= 0.25, never on sea cells; one-cell pits make no lake; river entries
// are { cells, flow } of equal length >= 2, chains of neighbours, ending on the first sea cell, lake cell or
// confluence; fresh equals a brute-force BFS. Works with no sea and with no land. Sets need.life. update
// <= 15 ms at L = 6. Maps: shots/ue-hydro-*.png.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Hydro.h"
#include "MapImg.h"
#include "TestFramework.h"
#include "TestWorld.h"

using namespace dm;
using namespace dmt;

namespace {

std::string sd(uint32_t seed) { return "seed " + std::to_string(seed); }

// Brute-force filled surface: land F = max(elev, min over neighbours F), sea = -inf (an outlet at any level),
// iterated to convergence. With no sea, the lowest cell (ties: lowest id) is the outlet.
std::vector<float> bruteFilled(const World& w) {
	const Geo& g = *w.geo;
	const int n = w.n;
	const float inf = std::numeric_limits<float>::infinity();
	std::vector<float> F(size_t(n), inf);
	bool anySea = false;
	for (int c = 0; c < n; c++) if (state::isSea(w, c)) { F[size_t(c)] = -inf; anySea = true; }
	int outlet = -1;
	if (!anySea) {
		outlet = 0;
		for (int c = 1; c < n; c++) if (w.elev[size_t(c)] < w.elev[size_t(outlet)]) outlet = c;
		F[size_t(outlet)] = w.elev[size_t(outlet)];
	}
	for (bool changed = true; changed;) {
		changed = false;
		for (int c = 0; c < n; c++) {
			if (state::isSea(w, c) || c == outlet) continue;
			float m = inf;
			for (int k = 0; k < g.nbrN[size_t(c)]; k++) m = std::min(m, F[size_t(g.nbr[size_t(6 * c + k)])]);
			const float v = std::max(w.elev[size_t(c)], m);
			if (v < F[size_t(c)]) { F[size_t(c)] = v; changed = true; }
		}
	}
	return F;
}

bool isNbr(const Geo& g, int a, int b) {
	for (int k = 0; k < g.nbrN[size_t(a)]; k++) if (g.nbr[size_t(6 * a + k)] == b) return true;
	return false;
}

double rainOf(const World& w, int c) {
	const float m = w.moist[size_t(c)];
	return (std::isfinite(m) && m > 0) ? m : 0.0;
}

struct HStats {
	int land = 0, riverCells = 0, lakeCells = 0, lakes = 0, rivers = 0, mouths = 0, confluences = 0, longest = 0, long10 = 0;
	double fresh2 = 0;     // share of land within 2 hops of fresh water
	double ridge = 0;      // share of river cells higher than the mean of their neighbours
};

// Every documented invariant of hydro::update on world w (already updated). Returns stats.
HStats checkAll(const World& w, const std::string& tag) {
	HStats st;
	const Geo& g = *w.geo;
	const int n = w.n;
	const HydroTune& T = w.tune<HydroTune>();
	const double RT = hydro::riverT(w);
	const std::vector<float> F = bruteFilled(w);
	bool anySea = false;
	for (int c = 0; c < n; c++) if (state::isSea(w, c)) anySea = true;
	if (!noNaN(w, tag)) return st;

	// ---- down: sea -1; land -> a neighbour not filled above it; reaches the sea (or the outlet) without cycles
	int bad = 0, outlets = 0;
	for (int c = 0; c < n && bad < 5; c++) {
		const int d = w.down[size_t(c)];
		if (state::isSea(w, c)) { if (!ok(d == -1, tag + ": down on a sea cell")) bad++; continue; }
		st.land++;
		if (d < 0) { outlets++; continue; }
		if (!ok(isNbr(g, c, d), tag + ": down is not a neighbour")) { bad++; continue; }
		if (!ok(F[size_t(d)] <= F[size_t(c)], tag + ": down is filled above the cell")) bad++;
		int cur = c, steps = 0;
		while (w.down[size_t(cur)] >= 0 && steps <= n) { cur = w.down[size_t(cur)]; steps++; }
		if (!ok(steps <= n, tag + ": down[] cycles")) { bad++; continue; }
		if (anySea) { if (!ok(state::isSea(w, cur), tag + ": down[] chain ends on land")) bad++; }
	}
	if (anySea) ok(outlets == 0, tag + ": a land cell with no down although there is sea");
	else if (st.land > 0) eq(outlets, 1, tag + ": exactly one outlet without sea");

	// ---- flow = rain + sum of inflow (land), sum of inflow (sea); never decreases downstream
	std::vector<double> inflow(size_t(n), 0.0);
	for (int c = 0; c < n; c++) if (w.down[size_t(c)] >= 0) inflow[size_t(w.down[size_t(c)])] += w.flow[size_t(c)];
	bad = 0;
	for (int c = 0; c < n && bad < 5; c++) {
		const double want = (state::isSea(w, c) ? 0.0 : rainOf(w, c)) + inflow[size_t(c)];
		if (!near(w.flow[size_t(c)], want, 1e-4 * std::max(1.0, want), tag + ": flow != moist + inflow at " + std::to_string(c))) bad++;
		const int d = w.down[size_t(c)];
		if (d >= 0 && !ok(w.flow[size_t(d)] >= w.flow[size_t(c)], tag + ": flow decreases downstream")) bad++;
	}

	// ---- water: >= 0, 0 on sea, = min(2.5, F - elev) on lakes, only in depressions
	bad = 0;
	for (int c = 0; c < n && bad < 5; c++) {
		const float wa = w.water[size_t(c)];
		if (!ok(wa >= 0, tag + ": negative water")) bad++;
		if (state::isSea(w, c)) { if (!ok(wa == 0, tag + ": water on a sea cell")) bad++; continue; }
		if (wa > 0) {
			const double d = double(F[size_t(c)]) - double(w.elev[size_t(c)]);
			if (!ok(d > 0, tag + ": water outside a depression")) bad++;
			if (!near(wa, std::min(T.lakeMaxWater, d), 1e-5, tag + ": water != min(2.5, filled - elev)")) bad++;
		}
		if (hydro::isLake(w, c)) st.lakeCells++;
	}
	// lakes are whole depressions that are big or deep enough and wet enough (or fed by a river);
	// depressions without water are small, shallow, or dry
	{
		std::vector<int> comp(size_t(n), -1);
		std::vector<int> stack;
		for (int c0 = 0; c0 < n; c0++) {
			if (state::isSea(w, c0) || comp[size_t(c0)] >= 0 || !(F[size_t(c0)] > w.elev[size_t(c0)])) continue;
			comp[size_t(c0)] = c0;
			stack.assign(1, c0);
			std::vector<int> cells;
			while (!stack.empty()) {
				const int a = stack.back();
				stack.pop_back();
				cells.push_back(a);
				for (int k = 0; k < g.nbrN[size_t(a)]; k++) {
					const int b = g.nbr[size_t(6 * a + k)];
					if (!state::isSea(w, b) && comp[size_t(b)] < 0 && F[size_t(b)] > w.elev[size_t(b)]) { comp[size_t(b)] = c0; stack.push_back(b); }
				}
			}
			int wet = 0, withWater = 0;
			double depth = 0, moist = 0, maxFlow = 0;
			for (int c : cells) {
				const double d = double(F[size_t(c)]) - double(w.elev[size_t(c)]);
				depth = std::max(depth, d);
				if (float(std::min(d, T.lakeMaxWater)) > float(T.lakeCell)) wet++;
				if (w.water[size_t(c)] > 0) withWater++;
				moist += rainOf(w, c);
				maxFlow = std::max(maxFlow, double(w.flow[size_t(c)]));
			}
			const bool big = wet >= T.lakeMinCells || depth >= T.lakeMinDepth;
			const bool wetEnough = moist >= T.lakeMinMoist * double(cells.size());
			if (withWater > 0) {
				st.lakes++;
				ok(withWater == int(cells.size()), tag + ": a lake covers only part of its depression");
				ok(big, tag + ": a lake of fewer than 3 cells and shallower than 0.25");
				ok(wetEnough || maxFlow > RT, tag + ": a dry lake that no river feeds");
			} else {
				ok(!(big && wetEnough), tag + ": a big, wet depression without a lake");
			}
		}
	}

	// ---- rivers
	std::vector<int> nonFinal(size_t(n), 0);     // how often a cell is a non-final river cell
	std::vector<int> owner(size_t(n), -1);        // river index of a non-final cell
	bad = 0;
	for (size_t r = 0; r < w.rivers.size() && bad < 5; r++) {
		const River& rv = w.rivers[r];
		if (!ok(rv.cells.size() == rv.flow.size() && rv.cells.size() >= 2, tag + ": river shape")) { bad++; continue; }
		const size_t len = rv.cells.size();
		for (size_t i = 0; i < len; i++) {
			const int c = rv.cells[i];
			if (!ok(w.valid(c), tag + ": river cell out of range")) { bad++; break; }
			if (!ok(rv.flow[i] == w.flow[size_t(c)], tag + ": river flow != world flow")) bad++;
			if (i + 1 < len) {
				const int d = rv.cells[i + 1];
				if (!ok(w.down[size_t(c)] == d && isNbr(g, c, d), tag + ": river is not a downstream chain of neighbours")) { bad++; break; }
				if (!ok(!state::isSea(w, c) && !hydro::isLake(w, c), tag + ": a non-final river cell on sea or lake")) bad++;
				if (!ok(w.flow[size_t(c)] > T.riverMin, tag + ": river cell carries less than riverMin")) bad++;
				if (!ok(w.fresh[size_t(c)] == 0 && hydro::isRiver(w, c), tag + ": river cell is not isRiver")) bad++;
				nonFinal[size_t(c)]++;
				owner[size_t(c)] = int(r);
			}
		}
		if (r > 0) {
			const River& pr = w.rivers[r - 1];
			ok(pr.flow[pr.flow.size() - 2] >= rv.flow[len - 2], tag + ": rivers not sorted by discharge");
		}
	}
	for (int c = 0; c < n; c++) {
		if (nonFinal[size_t(c)] > 1) { fail(tag + ": a cell on two rivers"); break; }
		if (nonFinal[size_t(c)] == 1) st.riverCells++;
		if (hydro::isRiver(w, c) && nonFinal[size_t(c)] != 1) { fail(tag + ": isRiver cell on no river"); break; }
	}
	for (size_t r = 0; r < w.rivers.size(); r++) {
		const River& rv = w.rivers[r];
		if (rv.cells.size() < 2) continue;
		const int last = rv.cells.back(), prev = rv.cells[rv.cells.size() - 2];
		const bool seaEnd = state::isSea(w, last), lakeEnd = hydro::isLake(w, last);
		const bool outletEnd = !anySea && w.down[size_t(last)] < 0;
		const bool confl = nonFinal[size_t(last)] == 1;
		if (!ok(seaEnd || lakeEnd || confl || outletEnd, tag + ": river ends on dry ground")) break;
		if (seaEnd) st.mouths++;
		if (confl && !seaEnd && !lakeEnd) {
			st.confluences++;
			// the continuing branch carries at least as much as the one that ends
			const River& mainR = w.rivers[size_t(owner[size_t(last)])];
			int up = -1;
			for (size_t i = 1; i < mainR.cells.size(); i++) if (mainR.cells[i] == last) up = mainR.cells[i - 1];
			if (up >= 0) ok(w.flow[size_t(up)] >= w.flow[size_t(prev)], tag + ": the smaller branch continues");
			// (a river leaving its source at a confluence cell is the main stem's own start: no upstream)
		}
		// no river ends on a cell of the same river
		for (size_t i = 0; i + 1 < rv.cells.size(); i++) if (rv.cells[i] == last) { fail(tag + ": river loops"); break; }
		st.longest = std::max(st.longest, int(rv.cells.size()));
		if (rv.cells.size() >= 10) st.long10++;
	}
	st.rivers = int(w.rivers.size());

	// ---- fresh = brute-force BFS from river and lake cells (through any cell), cap 255
	{
		std::vector<int> dist(size_t(n), 255), q;
		for (int c = 0; c < n; c++) if (nonFinal[size_t(c)] == 1 || hydro::isLake(w, c)) { dist[size_t(c)] = 0; q.push_back(c); }
		for (size_t h = 0; h < q.size(); h++) {
			const int c = q[h];
			if (dist[size_t(c)] + 1 >= 255) continue;
			for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
				const int b = g.nbr[size_t(6 * c + k)];
				if (dist[size_t(b)] == 255) { dist[size_t(b)] = dist[size_t(c)] + 1; q.push_back(b); }
			}
		}
		int wrong = 0, near2 = 0;
		for (int c = 0; c < n; c++) {
			if (int(w.fresh[size_t(c)]) != dist[size_t(c)]) wrong++;
			if (!state::isSea(w, c) && dist[size_t(c)] <= 2) near2++;
		}
		eq(wrong, 0, tag + ": fresh cells that differ from the brute-force BFS");
		st.fresh2 = st.land > 0 ? double(near2) / st.land : 0;
	}

	// ---- ridges: share of river cells higher than the mean of their neighbours
	{
		int hi = 0;
		for (int c = 0; c < n; c++) {
			if (nonFinal[size_t(c)] != 1) continue;
			double m = 0;
			for (int k = 0; k < g.nbrN[size_t(c)]; k++) m += w.elev[size_t(g.nbr[size_t(6 * c + k)])];
			m /= std::max(1, int(g.nbrN[size_t(c)]));
			if (w.elev[size_t(c)] > m) hi++;
		}
		st.ridge = st.riverCells > 0 ? double(hi) / st.riverCells : 0;
	}
	return st;
}

// A zoomed equirectangular view (lon0..lon1, lat0..lat1 in degrees) with shaded relief, lakes and the river
// polylines drawn as lines between cell centres, width by discharge. Test-only (for looking at the rivers).
bool zoomMap(const World& w, const std::string& file, double lon0, double lon1, double lat0, double lat1, int W = 1024) {
	const Geo& g = *w.geo;
	const double kDeg = kPi / 180;
	const int H = std::max(16, int(W * (lat1 - lat0) / (lon1 - lon0)));
	std::vector<uint8_t> rgb(size_t(W) * size_t(H) * 3);
	auto px = [&](double lon, double lat, double& x, double& y) {
		x = (lon - lon0) / (lon1 - lon0) * W;
		y = (lat1 - lat) / (lat1 - lat0) * H;
	};
	auto put = [&](int x, int y, double r, double gg, double b, double a) {
		if (x < 0 || y < 0 || x >= W || y >= H) return;
		uint8_t* p = &rgb[(size_t(y) * size_t(W) + size_t(x)) * 3];
		p[0] = uint8_t(p[0] + (r - p[0]) * a); p[1] = uint8_t(p[1] + (gg - p[1]) * a); p[2] = uint8_t(p[2] + (b - p[2]) * a);
	};
	int hint = -1;
	for (int y = 0; y < H; y++) {
		const double lat = (lat1 - (y + 0.5) / H * (lat1 - lat0)) * kDeg;
		for (int x = 0; x < W; x++) {
			const double lon = (lon0 + (x + 0.5) / W * (lon1 - lon0)) * kDeg;
			const double cl = std::cos(lat);
			const double vx = cl * std::cos(lon), vy = std::sin(lat), vz = cl * std::sin(lon);
			const int c = g.cellAt(vx, vy, vz, hint);
			hint = c;
			// interpolated elevation from the 3 nearest (cell + its two closest neighbours) for smooth shading
			double e = w.elev[size_t(c)];
			const double s = w.seaLevel;
			double r, gg, b;
			if (e < s) { const double d = std::min(1.0, (s - e) / 4); r = 30 * (1 - d) + 10; gg = 110 * (1 - d) + 30; b = 190 * (1 - d) + 90; }
			else if (hydro::isLake(w, c)) { r = 60; gg = 120; b = 210; }
			else {
				const double h = std::min(1.0, (e - s) / 6);
				r = 90 + 120 * h; gg = 150 - 10 * h; b = 80 + 60 * h;
				// hillshade from the neighbour gradient (light from the north-west)
				Vec3 east, north;
				g.basis(c, east, north);
				double gx = 0, gy = 0;
				for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
					const int m = g.nbr[size_t(6 * c + k)];
					const Vec3 d = g.dir(m) - g.dir(c);
					const double de = w.elev[size_t(m)] - e;
					gx += de * v3::dot(d, east);
					gy += de * v3::dot(d, north);
				}
				const double shade = std::clamp(1 + 1.2 * (gx * -0.7 + gy * 0.7) / (g.step * 6), 0.7, 1.25);
				r *= shade; gg *= shade; b *= shade;
			}
			put(x, y, std::min(255.0, r), std::min(255.0, gg), std::min(255.0, b), 1);
		}
	}
	float maxFlow = 1;
	for (const River& rv : w.rivers) for (float f : rv.flow) maxFlow = std::max(maxFlow, f);
	for (const River& rv : w.rivers) {
		for (size_t i = 0; i + 1 < rv.cells.size(); i++) {
			const Vec3 a = g.dir(rv.cells[i]), b = g.dir(rv.cells[i + 1]);
			const double wid = 1.0 + 3.0 * std::sqrt(rv.flow[i] / maxFlow);
			for (int sstep = 0; sstep <= 24; sstep++) {
				const Vec3 p = v3::norm(v3::lerp(a, b, sstep / 24.0));
				const double lat = std::asin(std::clamp(p.y, -1.0, 1.0)) / kDeg, lon = std::atan2(p.z, p.x) / kDeg;
				double x, y;
				px(lon, lat, x, y);
				const int R = int(std::ceil(wid));
				for (int dy = -R; dy <= R; dy++)
					for (int dx = -R; dx <= R; dx++)
						if (dx * dx + dy * dy <= wid * wid) put(int(x) + dx, int(y) + dy, 40, 110, 255, 0.9);
			}
		}
	}
	return mapimg::writePng(file, W, H, rgb);
}

void printStats(const std::string& tag, const World& w, const HStats& s, double ms = -1) {
	std::printf("  %-14s riverT %.2f  rivers %d (>=10 cells: %d, longest %d)  river cells %d (%.1f %% of land)  mouths %d  confluences %d"
	            "  lakes %d (%d cells)  fresh<=2 %.0f %%  ridge %.0f %%%s\n",
		tag.c_str(), hydro::riverT(w), s.rivers, s.long10, s.longest, s.riverCells, s.land ? 100.0 * s.riverCells / s.land : 0.0,
		s.mouths, s.confluences, s.lakes, s.lakeCells, 100 * s.fresh2, 100 * s.ridge,
		ms >= 0 ? ("  " + std::to_string(ms) + " ms").c_str() : "");
}

// A world on a shared geo with every cell set by the callback (elev), moist m on land, sea level sea.
std::unique_ptr<World> shaped(int L, const std::function<double(double, double, double)>& elevAt, double m = 0.5, double sea = 0) {
	state::CreateOpts co;
	co.L = L;
	co.geo = sharedGeo(L);
	auto w = state::create(co);
	const Geo& g = *w->geo;
	w->seaLevel = sea;
	w->phase = Phase::History;
	for (int c = 0; c < w->n; c++) {
		const size_t i = size_t(c);
		w->elev[i] = float(elevAt(g.pos[3 * i], g.pos[3 * i + 1], g.pos[3 * i + 2]));
		w->moist[i] = float(w->elev[i] < sea ? 1.0 : m);
	}
	watch(*w);
	return w;
}

// The lowest neighbour elevation of a set of cells (outside the set).
double rimOf(const World& w, const std::vector<int>& cells) {
	const Geo& g = *w.geo;
	std::set<int> in(cells.begin(), cells.end());
	double rim = 1e9;
	for (int c : cells)
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
			const int b = g.nbr[size_t(6 * c + k)];
			if (!in.count(b)) rim = std::min(rim, double(w.elev[size_t(b)]));
		}
	return rim;
}

// A slope world: sea below y = -0.4, land rising smoothly to the north pole.
std::unique_ptr<World> slopeWorld(int L, double m = 0.5) {
	return shaped(L, [](double, double y, double) { return y < -0.4 ? -2.0 : 0.3 + 3.0 * (y + 0.4); }, m);
}

// A land cell of the slope world near the given latitude direction.
int cellNear(const World& w, double x, double y, double z) { return w.geo->cellAt(x, y, z); }

std::vector<int> otherFields(const World& w, std::vector<std::vector<double>>& out) {
	out.clear();
	std::vector<int> idx;
	for (int i = 0; i < state::kCellFieldCount; i++) {
		const std::string nm = state::kCellFields[i].name;
		if (nm == "water" || nm == "flow" || nm == "down" || nm == "fresh") continue;
		const CellArray a = state::cellArray(w, i);
		std::vector<double> v(a.size);
		for (size_t c = 0; c < a.size; c++) v[c] = a.get(c);
		out.push_back(std::move(v));
		idx.push_back(i);
	}
	return idx;
}

} // namespace

// ============================================================================================================

DM_TEST("hydro: riverT scales with the cell count, has a floor, reads the tuning at call time") {
	auto w6 = makeWorld({ .L = 6, .basics = false });
	auto w5 = makeWorld({ .L = 5, .basics = false });
	auto w4 = makeWorld({ .L = 4, .basics = false });
	const HydroTune& T = w6->tune<HydroTune>();
	near(hydro::riverT(*w6), std::max(T.riverMin, T.riverRef), 1e-9, "reference world: riverRef");
	ok(hydro::riverT(*w5) < hydro::riverT(*w6) && hydro::riverT(*w4) <= hydro::riverT(*w5), "coarser planets: lower riverT");
	ok(hydro::riverT(*w4) >= T.riverMin, "floor riverMin");
	w6->tune<HydroTune>().riverRef = 30;
	near(hydro::riverT(*w6), 30, 1e-9, "tuning read at call time");
	auto w0 = state::create({ .L = 0 });
	ok(std::isfinite(hydro::riverT(*w0)) && hydro::riverT(*w0) >= T.riverMin, "L 0: finite, floored");
}

DM_TEST("hydro: helper worlds, 10 seeds at L 4: every documented invariant") {
	for (uint32_t seed = 1; seed <= 10; seed++) {
		auto w = makeWorld({ .L = 4, .seed = seed });
		hydro::update(*w);
		const HStats s = checkAll(*w, "L4 " + sd(seed));
		ok(s.rivers >= 1, "L4 " + sd(seed) + ": at least one river");
		if (seed <= 3) printStats("L4 " + sd(seed), *w, s);
	}
}

DM_TEST("hydro: helper worlds, 4 seeds at L 5 and other sea levels: invariants") {
	for (uint32_t seed = 1; seed <= 4; seed++) {
		auto w = makeWorld({ .L = 5, .seed = seed });
		hydro::update(*w);
		const HStats s = checkAll(*w, "L5 " + sd(seed));
		ok(s.rivers >= 5 && s.long10 + s.confluences >= 1, "L5 " + sd(seed) + ": several rivers");
		printStats("L5 " + sd(seed), *w, s);
	}
	for (double sea : { -1.5, -0.4, 0.6, 2.0 }) {
		auto w = makeWorld({ .L = 5, .seed = 7 });
		w->seaLevel = sea;
		fillBasics(*w);
		hydro::update(*w);
		checkAll(*w, "L5 sea " + std::to_string(sea));
	}
}

DM_TEST("hydro: update clears need.hydro, sets need.life, dirty.rivers, dirty.water and no other flag") {
	auto w = makeWorld({ .L = 4 });
	w->need = Need{ false, false, true, false };
	w->dirty = Dirty{ false, false, false, false, false, false };
	hydro::update(*w);
	ok(!w->need.hydro && w->need.life && !w->need.derive && !w->need.climate, "need flags");
	ok(w->dirty.rivers && w->dirty.water && !w->dirty.elev && !w->dirty.color && !w->dirty.objects && !w->dirty.roads, "dirty flags");
}

DM_TEST("hydro: deterministic, idempotent, no rng draws, no events, writes only its own fields") {
	auto a = makeWorld({ .L = 5, .seed = 3 });
	auto b = makeWorld({ .L = 5, .seed = 3 });
	std::vector<std::vector<double>> before, after;
	otherFields(*a, before);
	const uint32_t rs = a->rng.s;
	const auto evs = capture(*a, [&] { hydro::update(*a); });
	eq(evs.size(), size_t(0), "no events");
	eq(a->rng.s, rs, "no rng draws");
	otherFields(*a, after);
	ok(before == after, "only water / flow / down / fresh change");
	hydro::update(*b);
	hydro::update(*b);     // idempotent
	ok(a->water == b->water && a->flow == b->flow && a->down == b->down && a->fresh == b->fresh, "same fields");
	bool same = a->rivers.size() == b->rivers.size();
	for (size_t r = 0; same && r < a->rivers.size(); r++) same = a->rivers[r].cells == b->rivers[r].cells && a->rivers[r].flow == b->rivers[r].flow;
	ok(same, "same rivers");
	// a world on its own geo gives the same answer as one on the shared geo
	auto c = makeWorld({ .L = 5, .seed = 3, .geo = Geo::build(5) });
	hydro::update(*c);
	ok(c->down == a->down && c->water == a->water, "independent of geo sharing");
}

DM_TEST("hydro: the drainage tree depends on the terrain only (rain swells rivers, never moves their beds)") {
	auto a = makeWorld({ .L = 5, .seed = 5 });
	hydro::update(*a);
	const std::vector<int32_t> d0 = a->down;
	for (int c = 0; c < a->n; c++) a->moist[size_t(c)] = a->moist[size_t(c)] * 0.5f + 0.3f;
	hydro::update(*a);
	ok(a->down == d0, "same down[] after a rain change");
	checkAll(*a, "rewetted");
}

DM_TEST("hydro: bad rainfall (NaN, negative, infinite) counts as none") {
	auto w = makeWorld({ .L = 4, .seed = 2 });
	int k = 0;
	for (int c = 0; c < w->n; c++) {
		if (state::isSea(*w, c)) continue;
		const int r = k++ % 7;
		if (r == 0) w->moist[size_t(c)] = std::numeric_limits<float>::quiet_NaN();
		else if (r == 1) w->moist[size_t(c)] = -3;
		else if (r == 2) w->moist[size_t(c)] = std::numeric_limits<float>::infinity();
	}
	hydro::update(*w);
	for (int c = 0; c < w->n; c++) if (!std::isfinite(w->flow[size_t(c)])) { fail("non-finite flow"); break; }
	for (int c = 0; c < w->n; c++) if (!std::isfinite(w->moist[size_t(c)])) w->moist[size_t(c)] = 0;   // for checkAll's noNaN
	checkAll(*w, "bad rain");
}

DM_TEST("hydro: works in every phase and on the default (flat, all-land, rainless) world") {
	auto w = state::create({ .seed = 4, .L = 4 });
	watch(*w);
	hydro::update(*w);
	checkAll(*w, "default world");
	ok(w->rivers.empty(), "no rain: no rivers");
	for (Phase p : { Phase::Molten, Phase::Water, Phase::Life, Phase::People, Phase::History, Phase::Ended }) {
		auto h = makeWorld({ .L = 4, .seed = 9, .phase = p });
		hydro::update(*h);
		checkAll(*h, std::string("phase ") + std::string(toStr(p)));
	}
}

DM_TEST("hydro: no sea: the lowest cell is the outlet and everything drains to it") {
	auto w = makeWorld({ .L = 4, .seed = 6 });
	w->seaLevel = -20;
	for (int c = 0; c < w->n; c++) w->moist[size_t(c)] = 0.4f;
	hydro::update(*w);
	int outlet = 0;
	for (int c = 1; c < w->n; c++) if (w->elev[size_t(c)] < w->elev[size_t(outlet)]) outlet = c;
	eq(w->down[size_t(outlet)], -1, "outlet has no down");
	near(w->flow[size_t(outlet)], 0.4 * w->n, 1e-3 * w->n, "all rain reaches the outlet");
	const HStats s = checkAll(*w, "no sea");
	ok(s.rivers >= 1, "a river reaches the outlet");
	bool toOutlet = false;
	for (const River& r : w->rivers) if (r.cells.back() == outlet) toOutlet = true;
	ok(toOutlet, "the main river ends on the outlet");
	// no sea and no rain: flow directions only
	for (int c = 0; c < w->n; c++) w->moist[size_t(c)] = 0;
	hydro::update(*w);
	checkAll(*w, "no sea, no rain");
	ok(w->rivers.empty(), "no rivers without rain");
	for (int c = 0; c < w->n; c++) if (w->water[size_t(c)] != 0) { fail("water without rain"); break; }
}

DM_TEST("hydro: no land: every field at rest, no rivers") {
	auto w = makeWorld({ .L = 4, .seed = 2 });
	w->seaLevel = 50;
	for (int c = 0; c < w->n; c++) { w->water[size_t(c)] = 1; w->flow[size_t(c)] = 3; w->down[size_t(c)] = 0; w->fresh[size_t(c)] = 0; }
	hydro::update(*w);
	bool rest = true;
	for (int c = 0; c < w->n; c++)
		rest = rest && w->water[size_t(c)] == 0 && w->flow[size_t(c)] == 0 && w->down[size_t(c)] == -1 && w->fresh[size_t(c)] == 255;
	ok(rest, "water 0, flow 0, down -1, fresh 255");
	ok(w->rivers.empty(), "no rivers");
	ok(!w->need.hydro && w->need.life, "flags still set");
}

DM_TEST("hydro: a one-cell island and a one-cell sea") {
	// one-cell island
	auto w = shaped(4, [](double, double, double) { return -1.0; });
	const int top = w->geo->cellAt(0, 1, 0);
	w->elev[size_t(top)] = 1;
	w->moist[size_t(top)] = 0.5f;
	hydro::update(*w);
	checkAll(*w, "one-cell island");
	ok(w->down[size_t(top)] >= 0 && state::isSea(*w, w->down[size_t(top)]), "island drains into the sea");
	ok(w->rivers.empty(), "no river");
	// one-cell sea
	auto v = shaped(4, [](double, double y, double) { return 1.0 + y; }, 0.5);
	const int hole = v->geo->cellAt(0, -1, 0);
	v->elev[size_t(hole)] = -1;
	hydro::update(*v);
	const HStats s = checkAll(*v, "one-cell sea");
	ok(s.mouths >= 1, "the main river ends in the one-cell sea");
	near(v->flow[size_t(hole)], 0.5 * (v->n - 1), 1e-3 * v->n, "the one-cell sea receives all the rain");
}

DM_TEST("hydro: one-cell pits: shallower than 0.25 no lake, at least 0.25 deep a one-cell lake; capped at 2.5") {
	for (double depth : { 0.1, 0.24, 0.3, 2.7 }) {
		auto w = slopeWorld(5);
		const int c = cellNear(*w, 0.5, 0.5, 0.707);
		const double rim = rimOf(*w, { c });
		w->elev[size_t(c)] = float(rim - depth);
		hydro::update(*w);
		const std::string tag = "pit " + std::to_string(depth);
		checkAll(*w, tag);
		if (depth < 0.25) {
			ok(w->water[size_t(c)] == 0 && !hydro::isLake(*w, c), tag + ": no lake");
			ok(w->down[size_t(c)] >= 0, tag + ": drains through");
		} else {
			ok(hydro::isLake(*w, c), tag + ": a lake");
			near(w->water[size_t(c)], std::min(2.5, depth), 1e-4, tag + ": water = min(2.5, depth)");
		}
	}
}

DM_TEST("hydro: shallow basins: 3 cells make a lake, 2 do not; a dry basin stays dry") {
	auto base = slopeWorld(5);
	const Geo& g = *base->geo;
	const int c = cellNear(*base, -0.4, 0.55, 0.7);
	int a = g.nbr[size_t(6 * c)], b = -1;
	for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
		const int m = g.nbr[size_t(6 * c + k)];
		if (m != a && isNbr(g, a, m)) { b = m; break; }
	}
	if (!ok(b >= 0, "found a triangle of cells")) return;
	struct Case { std::vector<int> cells; double moist; bool lake; const char* name; };
	const Case cases[] = { { { c, a, b }, 0.5, true, "3 cells, wet" }, { { c, a }, 0.5, false, "2 cells, wet" },
		{ { c, a, b }, 0.1, false, "3 cells, dry" } };
	for (const Case& k : cases) {
		auto w = slopeWorld(5, k.moist);
		const double rim = rimOf(*w, k.cells);
		for (int x : k.cells) w->elev[size_t(x)] = float(rim - 0.1);
		hydro::update(*w);
		checkAll(*w, k.name);
		int lakeCells = 0;
		for (int x : k.cells) lakeCells += hydro::isLake(*w, x) ? 1 : 0;
		eq(lakeCells, k.lake ? int(k.cells.size()) : 0, std::string(k.name) + ": lake cells");
		for (int x = 0; x < w->n; x++) {
			const bool inBasin = std::find(k.cells.begin(), k.cells.end(), x) != k.cells.end();
			if (!inBasin && w->water[size_t(x)] > 0) { fail(std::string(k.name) + ": water outside the basin"); break; }
		}
	}
}

DM_TEST("hydro: valley continent: long rivers down the valley floors to the coast, dry ridge crests, a fed lake") {
	// Land where z > 0 (sea elsewhere), rising toward +z; ridges at |y| = 0.35, valleys at y = 0 and |y| = 0.7.
	auto ridgeAt = [](double y) { const double u = std::fabs(std::fabs(y) - 0.35) / 0.35; return 2.5 * std::max(0.0, 1 - u); };
	auto elevAt = [&](double, double y, double z) { return z < 0 ? -3.0 : 0.2 + 3.0 * z + ridgeAt(y); };
	auto w = shaped(5, elevAt, 0.5);
	hydro::update(*w);
	const HStats s = checkAll(*w, "valleys");
	printStats("valleys", *w, s);
	const Geo& g = *w->geo;
	int onCrest = 0;
	for (int c = 0; c < w->n; c++) if (hydro::isRiver(*w, c) && std::fabs(std::fabs(g.pos[size_t(3 * c + 1)]) - 0.35) < 0.03) onCrest++;
	eq(onCrest, 0, "river cells on the ridge crests");
	if (ok(!w->rivers.empty(), "rivers")) {
		const River& big = w->rivers[0];
		const int mouth = big.cells.back();
		const double my = std::fabs(g.pos[size_t(3 * mouth + 1)]);
		ok(state::isSea(*w, mouth), "the biggest river reaches the sea");
		ok(my < 0.08 || std::fabs(my - 0.7) < 0.08, "the biggest river runs down a valley floor (|y| " + std::to_string(my) + ")");
		ok(big.cells.size() >= 20, "the biggest river is long (" + std::to_string(big.cells.size()) + " cells)");
	}
	ok(s.confluences >= 4, "tributaries join the main rivers");
	mapimg::elev(*w, shot("ue-hydro-valleys.png"));
	// a dry basin dug into the valley floor upstream of the coast: the river fills it as a lake
	// instead of climbing out of it, even though its own rain is below 0.15
	const int pit = g.cellAt(0.5, 0.0, 0.866);
	std::vector<int> basin = { pit };
	for (int k = 0; k < g.nbrN[size_t(pit)]; k++) basin.push_back(g.nbr[size_t(6 * pit + k)]);
	const double rim = rimOf(*w, basin);
	for (int x : basin) { w->elev[size_t(x)] = float(rim - 0.6); w->moist[size_t(x)] = 0.05f; }
	hydro::update(*w);
	checkAll(*w, "fed lake");
	if (ok(w->flow[size_t(pit)] > hydro::riverT(*w), "the valley river passes the basin")) {
		ok(hydro::isLake(*w, pit), "a dry basin on a river's course fills up");
		bool endsInLake = false, leaves = false;
		for (const River& r : w->rivers) {
			if (hydro::isLake(*w, r.cells.back()) && std::find(basin.begin(), basin.end(), r.cells.back()) != basin.end()) endsInLake = true;
			for (int x : basin) if (hydro::isLake(*w, x) && w->down[size_t(x)] == r.cells[0]) leaves = true;   // starts at the outlet
		}
		ok(endsInLake, "the river ends in the lake");
		ok(leaves, "a river leaves the lake at its outlet");
	}
	mapimg::elev(*w, shot("ue-hydro-valleys-lake.png"));
}

DM_TEST("hydro: tuning knobs: pull 0 is the plain flood; creek 1 and arid 0 keep every stream; head 0 starts at riverT") {
	auto w = makeWorld({ .L = 5, .seed = 2 });
	HydroTune& T = w->tune<HydroTune>();
	hydro::update(*w);
	const HStats s0 = checkAll(*w, "default tune");
	T.pull = 0;
	T.jitter = 0;
	hydro::update(*w);
	checkAll(*w, "pull 0");
	// plain flood: every slope cell with no sea neighbour drains to its lowest-filled neighbour
	{
		const std::vector<float> F = bruteFilled(*w);
		const Geo& g = *w->geo;
		int bad = 0;
		for (int c = 0; c < w->n; c++) {
			const int d = w->down[size_t(c)];
			if (d < 0 || state::isSea(*w, d)) continue;
			float lo = std::numeric_limits<float>::infinity();
			for (int k = 0; k < g.nbrN[size_t(c)]; k++) lo = std::min(lo, F[size_t(g.nbr[size_t(6 * c + k)])]);
			if (F[size_t(d)] != lo) bad++;
		}
		eq(bad, 0, "pull 0: down is a lowest neighbour");
	}
	T.pull = 4;
	T.jitter = 0.4;
	T.creek = 1;
	T.arid = 0;
	hydro::update(*w);
	const HStats s1 = checkAll(*w, "creek 1, arid 0");
	ok(s1.rivers > s0.rivers, "every stream counts: more rivers (" + std::to_string(s1.rivers) + " vs " + std::to_string(s0.rivers) + ")");
	T.creek = 3;
	T.arid = 0.12;
	T.head = 0;
	hydro::update(*w);
	const HStats s2 = checkAll(*w, "head 0");
	eq(s2.rivers, s0.rivers, "headwaters add no branch");
	ok(s2.riverCells < s0.riverCells, "headwaters add length");
	for (int c = 0; c < w->n; c++)
		if (hydro::isRiver(*w, c) && !(w->flow[size_t(c)] > hydro::riverT(*w))) { fail("head 0: a river cell at or below riverT"); break; }
}

DM_TEST("hydro: a nearly dry planet has no rivers and no lakes") {
	auto w = makeWorld({ .L = 5, .seed = 4 });
	for (int c = 0; c < w->n; c++) if (!state::isSea(*w, c)) w->moist[size_t(c)] = 0.05f;
	hydro::update(*w);
	const HStats s = checkAll(*w, "dry planet");
	eq(s.rivers, 0, "no rivers in a desert");
	eq(s.lakeCells, 0, "no lakes in a desert");
}

DM_TEST("hydro: L 6: update within budget, long branching rivers that reach the coast, lakes, dry ridges; maps") {
	for (uint32_t seed = 1; seed <= 3; seed++) {
		auto w = makeWorld({ .L = 6, .seed = seed });
		hydro::update(*w);   // warm the scratch
		double sum = 0, mx = 0;
		const int reps = 10;
		for (int i = 0; i < reps; i++) {
			const double t0 = nowMs();
			hydro::update(*w);
			const double dt = nowMs() - t0;
			sum += dt;
			mx = std::max(mx, dt);
		}
		const HStats s = checkAll(*w, "L6 " + sd(seed));
		printStats("L6 " + sd(seed), *w, s, sum / reps);
		std::printf("  L6 %s: update mean %.2f ms, max %.2f ms (target 15)\n", sd(seed).c_str(), sum / reps, mx);
		ok(sum / reps <= 5 * w->tune<HydroTune>().budgetMs, "update within 5x the 15 ms budget");
		ok(s.long10 >= 8, "L6 " + sd(seed) + ": long rivers (>= 10 cells): " + std::to_string(s.long10));
		ok(s.mouths >= 15, "L6 " + sd(seed) + ": rivers reach the coast: " + std::to_string(s.mouths));
		ok(s.confluences >= 8, "L6 " + sd(seed) + ": branching: " + std::to_string(s.confluences));
		ok(s.lakes >= 1, "L6 " + sd(seed) + ": lakes in basins");
		ok(s.ridge < 0.3, "L6 " + sd(seed) + ": rivers keep to the low ground");
		const double share = s.land ? double(s.riverCells) / s.land : 0;
		ok(share > 0.02 && share < 0.15, "L6 " + sd(seed) + ": river cells 2-15 % of land: " + std::to_string(share));
		mapimg::elev(*w, shot("ue-hydro-rivers-s" + std::to_string(seed) + ".png"));
		if (seed == 1) {
			std::vector<float> lf(size_t(w->n));
			for (int c = 0; c < w->n; c++) lf[size_t(c)] = float(std::log10(1 + double(w->flow[size_t(c)])));
			mapimg::field(*w, lf, shot("ue-hydro-flow-s1.png"), 0, 3, { .rivers = false, .dimSea = true });
			mapimg::field(*w, "fresh", shot("ue-hydro-fresh-s1.png"), 0, 12, { .rivers = false, .dimSea = true });
			zoomMap(*w, shot("ue-hydro-zoom-s1-ne.png"), 30, 150, 15, 75);
			zoomMap(*w, shot("ue-hydro-zoom-s1-sw.png"), -60, 60, -60, 0);
		}
	}
}

DM_TEST("hydro: L 6 worst case: an all-land planet (genesis), wet or dry, within budget") {
	for (double m : { 0.6, 0.0 }) {
		auto w = makeWorld({ .L = 6, .seed = 2 });
		w->seaLevel = -20;
		for (int c = 0; c < w->n; c++) w->moist[size_t(c)] = float(m);
		hydro::update(*w);
		const double t0 = nowMs();
		hydro::update(*w);
		const double dt = nowMs() - t0;
		std::printf("  L6 all land, moist %.1f: update %.2f ms\n", m, dt);
		ok(dt <= 5 * w->tune<HydroTune>().budgetMs, "within 5x budget");
		checkAll(*w, "all land " + std::to_string(m));
	}
	// first comet: sea at -6 pools in the deepest basins
	auto w = makeWorld({ .L = 6, .seed = 2 });
	w->seaLevel = -6;
	fillBasics(*w);
	hydro::update(*w);
	checkAll(*w, "sea -6");
}

// Optional look-test on the game's real planets: set DM_HYDRO_REAL to a folder holding world<seed>.bin
// (n float32 elev, then n float32 moist, at L = 6 and sea level 0; dumped from the finished JS terrain and
// climate, whose cell numbering is identical). Skipped (passes) when the variable is not set.
DM_TEST("hydro: real planets (DM_HYDRO_REAL): invariants, numbers and maps") {
	const char* dir = std::getenv("DM_HYDRO_REAL");
	if (!dir || !*dir) return;
	const char* ref = std::getenv("DM_HYDRO_REF");
	for (int seed = 1; seed <= 6; seed++) {
		const std::string file = std::string(dir) + "/world" + std::to_string(seed) + ".bin";
		FILE* f = std::fopen(file.c_str(), "rb");
		if (!f) continue;
		auto w = state::create({ .seed = uint32_t(seed), .L = 6, .geo = sharedGeo(6) });
		watch(*w);
		std::vector<float> buf(size_t(2 * w->n));
		const size_t got = std::fread(buf.data(), sizeof(float), buf.size(), f);
		std::fclose(f);
		if (!ok(got == buf.size(), "read " + file)) continue;
		for (int c = 0; c < w->n; c++) { w->elev[size_t(c)] = buf[size_t(c)]; w->moist[size_t(c)] = buf[size_t(w->n + c)]; }
		w->seaLevel = 0;
		w->phase = Phase::History;
		if (ref) w->tune<HydroTune>().riverRef = std::atof(ref);
		hydro::update(*w);
		const double t0 = nowMs();
		hydro::update(*w);
		const double dt = nowMs() - t0;
		const HStats s = checkAll(*w, "real " + sd(uint32_t(seed)));
		printStats("real " + sd(uint32_t(seed)), *w, s, dt);
		if (!ref) {
			const double share = s.land ? double(s.riverCells) / s.land : 0;
			ok(s.long10 >= 15 && s.mouths >= 15 && s.confluences >= 15 && s.lakes >= 5, "real " + sd(uint32_t(seed)) + ": long branching rivers to the coast, lakes");
			ok(share > 0.02 && share < 0.10 && s.ridge < 0.15, "real " + sd(uint32_t(seed)) + ": river density and low ground");
		}
		const std::string tag = std::string(ref ? ref : "") + "s" + std::to_string(seed);
		mapimg::elev(*w, shot("ue-hydro-real-" + tag + ".png"));
		if (seed <= 2) {
			zoomMap(*w, shot("ue-hydro-real-" + tag + "-zA.png"), -180, -60, -60, 60);
			zoomMap(*w, shot("ue-hydro-real-" + tag + "-zB.png"), -60, 60, -60, 60);
			zoomMap(*w, shot("ue-hydro-real-" + tag + "-zC.png"), 60, 180, -60, 60);
		}
	}
}
