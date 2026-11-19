// DEMIURGE — Sim/Hydro.cpp : lakes, drainage, discharge, rivers, distance to fresh water (SPEC-SIM 3.4).
// Owner: hydro engineer (A4). See Hydro.h for the contract other modules rely on.
//
// How it works.
//  1. Priority-flood from the sea over the land (a binary min-heap keyed by (filled level, cell id), plus a
//     FIFO for cells at or below the level being flooded, so water crosses flats and depressions by the
//     shortest way to their outlet). Every land cell gets a filled level (how high water must stand there
//     before it can run off) and a drainage parent. Coastal land drains into its lowest sea neighbour; with
//     no sea, the lowest cell is the outlet.
//  2. From the highest filled level down, every cell hands its rain (moist) plus everything it received to its
//     downstream cell: the discharge flow[]. On slopes, a lower neighbour that already drains a stream counts
//     as steeper than it is (tune.pull), so parallel flow lines on a smooth slope merge into valleys instead of
//     running side by side to the coast. The pull is measured in cells drained, not in rain, so the drainage
//     tree depends on elev and seaLevel only: when the climate changes, rivers swell or shrink in their beds.
//  3. River systems (everything reaching the sea through one mouth) count only if the mouth carries at least
//     creek x riverT and the catchment is not a desert (tune.arid): otherwise every coast is ringed with
//     one-cell stubs and a dry planet's washes would count as rivers. While the sea stands above its datum
//     (vars.seaBase; tune.estuary), a land cell also counts when its system counts on the drainage tree of
//     the datum sea (steps 1-3 run a second time for that sea level): a rising sea drowns the lower course of
//     a river, it does not erase the tributaries that used to join it.
//  4. Lakes: each connected depression (filled > elev) that is big or deep enough and wet enough (or fed by a
//     counted river) holds water = min(2.5, filled - elev).
//  5. River cells: land, not lake, flow > riverT, in a counted system. Headwaters extend each source upstream
//     along its largest inflow (tune.head), adding length but never a branch. One polyline per source.
//  6. fresh[]: multi-source BFS from river and lake cells.
// Everything is deterministic: ties break on cell id, no rng, no unordered containers. Buffers live in the
// world's HydroScratch (sized by n).
#include "Hydro.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dm {

namespace {

constexpr double kRefCells = 40962;   // the reference world (L = 6)

// Cell classes after the flood (HydroScratch::cls).
enum CellClass : uint8_t { ClsSea = 0, ClsLand = 1, ClsDep = 2, ClsLake = 3, ClsRiver = 4 };

// The buffers of one drainage pass (steps 1-2) for one sea level.
struct DrainBuf {
	std::vector<float> filled;      // filled surface level of each cell
	std::vector<double> acc;        // discharge accumulator
	std::vector<double> area;       // land cells drained through each cell (itself included)
	std::vector<int32_t> down;      // drainage parent (-1 on sea / the outlet)
	std::vector<int32_t> order;     // land cells in flood order (non-decreasing filled level, parents first)
	std::vector<uint8_t> sea;       // 1 on sea cells of this pass
	std::vector<uint8_t> counted;   // 1 on land cells whose river system counts (step 3)
	int cnt = 0;                    // land cells in order
	int outlet = -1;                // the lowest cell of a world without sea

	void fit(size_t sz) {
		filled.assign(sz, 0.0f);
		acc.assign(sz, 0.0);
		area.assign(sz, 0.0);
		down.assign(sz, -1);
		order.assign(sz, 0);
		sea.assign(sz, 0);
		counted.assign(sz, 0);
	}
};

struct HydroScratch {
	int n = -1;
	std::vector<float> key;         // sanitized elevation (NaN -> 0)
	std::vector<float> rain;        // sanitized moist (NaN, negative, infinite -> 0)
	std::vector<int32_t> heap;      // binary min-heap of cell ids keyed by (filled, id)
	std::vector<int32_t> queue;     // FIFO of the flood; later lake labelling, polylines, BFS
	std::vector<int32_t> mainUp;    // per river cell: the upstream river cell that carries the most water, or -1
	std::vector<uint8_t> cls;       // flood: visited flag; afterwards CellClass
	DrainBuf now;                   // the current sea level
	DrainBuf base;                  // the datum sea level (only while the sea is above it)

	void fit(int cells) {
		if (n == cells) return;
		n = cells;
		const size_t sz = size_t(cells > 0 ? cells : 0);
		key.assign(sz, 0.0f);
		rain.assign(sz, 0.0f);
		heap.assign(sz, 0);
		queue.assign(sz, 0);
		mainUp.assign(sz, -1);
		cls.assign(sz, 0);
		now.fit(sz);
		base.fit(sz);
	}
};

// ---- binary min-heap of cell ids ordered by (keyOf[c], c) -------------------------------------------------
struct CellHeap {
	int32_t* data;
	const float* keyOf;
	int count = 0;

	bool before(int32_t a, int32_t b) const {
		const float ka = keyOf[a], kb = keyOf[b];
		return ka < kb || (ka == kb && a < b);
	}
	void push(int32_t c) {
		int i = count++;
		while (i > 0) {
			const int p = (i - 1) >> 1;
			if (before(data[p], c)) break;
			data[i] = data[p];
			i = p;
		}
		data[i] = c;
	}
	int32_t pop() {
		const int32_t top = data[0];
		const int32_t last = data[--count];
		if (count > 0) {
			int i = 0;
			for (;;) {
				int l = 2 * i + 1;
				if (l >= count) break;
				const int r = l + 1;
				if (r < count && before(data[r], data[l])) l = r;
				if (before(last, data[l])) break;
				data[i] = data[l];
				i = l;
			}
			data[i] = last;
		}
		return top;
	}
};

double scaledT(const HydroTune& t, int cells) {
	const double r = double(cells > 0 ? cells : kRefCells) / kRefCells;
	const double v = t.riverRef * std::pow(r, t.riverExp);
	return std::isfinite(v) ? v : t.riverRef;
}

// Steps 1-2 for the sea cells marked in B.sea: filled levels, drainage parents, discharge (B.acc) and drained
// area. Uses S.heap, S.queue and S.cls as temporaries. Returns false when there is no land.
bool drain(const Geo& g, int n, const HydroTune& t, HydroScratch& S, DrainBuf& B) {
	const int32_t* nbr = g.nbr.data();
	const uint8_t* nbrN = g.nbrN.data();
	const float* key = S.key.data();
	const float* rain = S.rain.data();
	const uint8_t* sea = B.sea.data();
	float* filled = B.filled.data();
	double* acc = B.acc.data();
	double* area = B.area.data();
	int32_t* down = B.down.data();
	int32_t* order = B.order.data();
	int32_t* queue = S.queue.data();
	uint8_t* seen = S.cls.data();
	int nLand = 0;
	for (int c = 0; c < n; c++) {
		down[c] = -1;
		filled[c] = key[c];
		if (sea[c]) {
			seen[c] = 1;
			acc[c] = 0;
			area[c] = 0;
		} else {
			seen[c] = 0;
			acc[c] = rain[c];
			area[c] = 1;
			nLand++;
		}
	}
	B.cnt = 0;
	B.outlet = -1;
	if (nLand == 0) return false;

	// ---- 1. priority-flood ------------------------------------------------------------------------------
	CellHeap heap{ S.heap.data(), filled };
	if (nLand == n) {
		int outlet = 0;                                   // no sea: the lowest cell is the outlet
		for (int c = 1; c < n; c++) if (key[c] < key[outlet]) outlet = c;
		B.outlet = outlet;
		seen[outlet] = 1;
		heap.push(outlet);
	} else {
		for (int c = 0; c < n; c++) {
			if (seen[c]) continue;
			int best = -1;
			float be = 0;
			for (int k = 0; k < nbrN[c]; k++) {
				const int b = nbr[6 * c + k];
				if (sea[b] && (best < 0 || key[b] < be || (key[b] == be && b < best))) { best = b; be = key[b]; }
			}
			if (best >= 0) { seen[c] = 1; down[c] = best; heap.push(c); }
		}
	}
	int cnt = 0, qh = 0, qt = 0;
	while (qh < qt || heap.count > 0) {
		int32_t c;
		if (qh < qt && !(heap.count > 0 && filled[heap.data[0]] <= filled[queue[qh]])) c = queue[qh++];
		else c = heap.pop();
		order[cnt++] = c;
		const float f = filled[c];
		for (int k = 0; k < nbrN[c]; k++) {
			const int b = nbr[6 * c + k];
			if (seen[b]) continue;
			seen[b] = 1;
			down[b] = c;
			if (key[b] <= f) { filled[b] = f; queue[qt++] = b; }
			else { filled[b] = key[b]; heap.push(b); }
		}
	}
	B.cnt = cnt;

	// ---- 2. flow directions (pull) and discharge, from the highest filled level down ----------------------
	// A cell's downstream cell always precedes it in flood order (the flood's parent by construction; any
	// strictly lower neighbour because levels are taken in order), so one reverse sweep accumulates
	// everything and no cycle is possible. When a slope cell's turn comes, area[] of a lower neighbour is the
	// catchment that already drains through it from higher ground.
	// The jitter (a fixed hash per pair of cells, standing in for micro-relief) breaks the symmetry of
	// perfectly smooth slopes, where every flow line would otherwise run beside the next one forever; the
	// pull then gathers them.
	const double pull = (std::isfinite(t.pull) && t.pull > 0) ? t.pull : 0.0;
	const double pullRef = ((std::isfinite(t.pullAt) && t.pullAt > 0) ? t.pullAt : 1.0) * double(n) / kRefCells;
	const double jitter = (std::isfinite(t.jitter) && t.jitter > 0) ? std::min(t.jitter, 0.9) : 0.0;
	for (int i = cnt - 1; i >= 0; i--) {
		const int c = order[i];
		int p = down[c];
		if (p < 0) continue;
		if ((pull > 0 || jitter > 0) && !sea[p]) {
			const double fc = filled[c];
			if (fc - double(filled[p]) > 0) {              // a slope cell: its parent is strictly lower
				double best = -1;
				for (int k = 0; k < nbrN[c]; k++) {
					const int b = nbr[6 * c + k];
					if (sea[b]) continue;
					const double d = fc - double(filled[b]);
					if (!(d > 0)) continue;
					const double a = area[b];
					double sc = d * (1 + pull * a / (a + pullRef));
					if (jitter > 0) sc *= 1 + jitter * (hashf(uint32_t(c), uint32_t(b)) - 0.5);
					if (sc > best || (sc == best && b < p)) { best = sc; p = b; }
				}
				down[c] = p;
			}
		}
		acc[p] += acc[c];
		area[p] += area[c];
	}
	return true;
}

// Step 3 for one drainage pass: B.counted = 1 on land cells whose system's mouth carries at least creekT and
// whose catchment is not a desert (flow >= arid x drained cells at the mouth).
void countSystems(DrainBuf& B, double creekT, double arid) {
	const int32_t* order = B.order.data();
	const int32_t* down = B.down.data();
	const uint8_t* sea = B.sea.data();
	uint8_t* counted = B.counted.data();
	for (int i = 0; i < B.cnt; i++) {                    // parents first
		const int c = order[i];
		const int p = down[c];
		if (p < 0 || sea[p]) {
			const double fl = float(B.acc[size_t(c)]);   // the stored (float) discharge
			counted[c] = (fl >= creekT && fl >= arid * B.area[size_t(c)]) ? 1 : 0;
		} else {
			counted[c] = counted[p];
		}
	}
}

} // namespace

namespace hydro {

double riverT(const World& w) {
	const HydroTune& t = w.tune<HydroTune>();
	const double v = scaledT(t, w.n);
	const double floorT = std::isfinite(t.riverMin) ? t.riverMin : 0.0;
	return v > floorT ? v : floorT;
}

bool isRiver(const World& w, int c) {
	if (!w.valid(c) || size_t(w.n) != w.fresh.size()) return false;
	return w.fresh[size_t(c)] == 0 && !state::isSea(w, c) && !(w.water[size_t(c)] > 0.03f);
}

void update(World& w) {
	const int n = w.n;
	w.rivers.clear();
	w.need.hydro = false;
	w.need.life = true;
	w.dirty.rivers = true;
	w.dirty.water = true;
	if (n <= 0 || !w.geo || w.geo->n != n) return;
	const size_t sz = size_t(n);
	if (w.elev.size() != sz || w.moist.size() != sz || w.water.size() != sz || w.flow.size() != sz || w.down.size() != sz ||
	    w.fresh.size() != sz)
		return;

	const HydroTune& t = w.tune<HydroTune>();
	HydroScratch& S = w.scratch<HydroScratch>();
	S.fit(n);
	const Geo& g = *w.geo;
	const int32_t* nbr = g.nbr.data();
	const uint8_t* nbrN = g.nbrN.data();
	DrainBuf& B = S.now;
	float* key = S.key.data();
	float* rain = S.rain.data();
	int32_t* queue = S.queue.data();
	int32_t* mainUp = S.mainUp.data();
	uint8_t* cls = S.cls.data();
	float* water = w.water.data();
	float* flow = w.flow.data();
	int32_t* down = w.down.data();
	uint8_t* fresh = w.fresh.data();
	const double rivT = riverT(w);

	// ---- 0. reset, sanitize -----------------------------------------------------------------------------
	for (int c = 0; c < n; c++) {
		const size_t i = size_t(c);
		const float e = w.elev[i];
		key[c] = std::isfinite(e) ? e : 0.0f;
		const float m = w.moist[i];
		rain[c] = (std::isfinite(m) && m > 0) ? m : 0.0f;
		water[c] = 0;
		mainUp[c] = -1;
		B.sea[i] = state::isSea(w, c) ? 1 : 0;
	}

	// ---- 1-2. drainage at the current sea level ------------------------------------------------------------
	if (!drain(g, n, t, S, B)) {
		for (int c = 0; c < n; c++) { down[c] = -1; flow[c] = 0; fresh[c] = 255; }
		return;
	}
	const int cnt = B.cnt;
	const int outlet = B.outlet;
	const int32_t* order = B.order.data();
	const float* filled = B.filled.data();
	uint8_t* counted = B.counted.data();
	for (int c = 0; c < n; c++) {
		down[c] = B.down[size_t(c)];
		flow[c] = float(B.acc[size_t(c)]);
	}

	// ---- 3. river systems that count ---------------------------------------------------------------------
	{
		const double creekRaw = (std::isfinite(t.creek) && t.creek > 0 ? t.creek : 0.0) * rivT;
		const double creekT = creekRaw > rivT ? creekRaw : rivT;
		const double arid = (std::isfinite(t.arid) && t.arid > 0) ? t.arid : 0.0;
		countSystems(B, creekT, arid);
		// The datum tree: while the sea stands above vars.seaBase, a cell that is land at both levels also counts
		// when its system counts with the sea at seaBase (the tributaries of a drowned trunk stay rivers).
		const bool above = t.estuary && w.vars.seaBase.has_value() && std::isfinite(*w.vars.seaBase) && std::isfinite(w.seaLevel) &&
		                   w.seaLevel > *w.vars.seaBase;
		if (above) {
			DrainBuf& D = S.base;
			const double base = *w.vars.seaBase;
			int drowned = 0;
			for (int c = 0; c < n; c++) {
				D.sea[size_t(c)] = w.elev[size_t(c)] < base ? 1 : 0;
				if (B.sea[size_t(c)] && !D.sea[size_t(c)]) drowned++;
			}
			if (drowned > 0 && drain(g, n, t, S, D)) {
				countSystems(D, creekT, arid);
				for (int i = 0; i < cnt; i++) {              // parents first, so a counted cell passes it upstream
					const int c = order[i];
					const int p = down[c];
					if (D.counted[size_t(c)] || (p >= 0 && !B.sea[size_t(p)] && counted[p])) counted[c] = 1;
				}
			}
		}
	}
	for (int c = 0; c < n; c++) cls[c] = B.sea[size_t(c)] ? ClsSea : ClsLand;

	// ---- 4. lakes ------------------------------------------------------------------------------------------
	{
		const double lakeMax = t.lakeMaxWater, lakeEps = t.lakeCell, lakeDepth = t.lakeMinDepth;
		for (int i = 0; i < cnt; i++) {
			const int c = order[i];
			if (filled[c] > key[c]) cls[c] = ClsDep;
		}
		for (int i = 0; i < cnt; i++) {
			const int c0 = order[i];
			if (cls[c0] != ClsDep) continue;
			int m = 0, wetCells = 0;
			double depth = 0, sumMoist = 0;
			bool fed = false;
			cls[c0] = ClsLand;
			queue[m++] = c0;
			for (int h = 0; h < m; h++) {
				const int c = queue[h];
				const double d = double(filled[c]) - double(key[c]);
				if (d > depth) depth = d;
				if (float(std::min(d, lakeMax)) > float(lakeEps)) wetCells++;
				sumMoist += rain[c];
				if (counted[c] && flow[c] > rivT) fed = true;
				for (int k = 0; k < nbrN[c]; k++) {
					const int b = nbr[6 * c + k];
					if (cls[b] == ClsDep) { cls[b] = ClsLand; queue[m++] = b; }
				}
			}
			const bool bigEnough = wetCells >= t.lakeMinCells || depth >= lakeDepth;
			const bool wetEnough = sumMoist >= t.lakeMinMoist * m || (t.fedLakes && fed);
			if (bigEnough && wetEnough) {
				for (int h = 0; h < m; h++) {
					const int c = queue[h];
					const double d = double(filled[c]) - double(key[c]);
					water[c] = float(d < lakeMax ? d : lakeMax);
					if (water[c] > float(lakeEps)) cls[c] = ClsLake;
				}
			}
		}
	}

	// ---- 5. river cells, main branches, headwaters --------------------------------------------------------
	for (int i = 0; i < cnt; i++) {
		const int c = order[i];
		if (cls[c] == ClsLand && counted[c] && flow[c] > rivT && c != outlet) cls[c] = ClsRiver;   // the outlet ends rivers, like the sea
	}
	for (int c = 0; c < n; c++) {                       // the upstream river cell bringing the most water (ties: lowest id)
		if (cls[c] != ClsRiver) continue;
		const int d = down[c];
		if (d < 0 || cls[d] != ClsRiver) continue;
		const int m = mainUp[d];
		if (m < 0 || flow[c] > flow[m]) mainUp[d] = c;
	}
	{
		double headT = (std::isfinite(t.head) && t.head > 0 ? t.head : 0.0) * rivT;
		if (headT < t.riverMin) headT = t.riverMin;
		if (t.head > 0 && headT < rivT) {
			int m = 0;
			for (int c = 0; c < n; c++) if (cls[c] == ClsRiver && mainUp[c] < 0) queue[m++] = c;
			for (int i = 0; i < m; i++) {
				int cur = queue[i];
				for (int guard = 0; guard < n; guard++) {
					int up = -1;
					float uf = float(headT);
					bool lakeAbove = false;
					for (int k = 0; k < nbrN[cur]; k++) {
						const int b = nbr[6 * cur + k];
						if (down[b] != cur) continue;
						if (cls[b] == ClsLake) { lakeAbove = true; break; }
						if (cls[b] != ClsLand) continue;
						const float fb = flow[b];
						if (fb > uf || (fb == uf && up >= 0 && b < up)) { up = b; uf = fb; }
					}
					if (lakeAbove || up < 0) break;
					if (double(filled[up]) - double(key[up]) >= t.lakeMinDepth) break;   // would climb out of a basin
					cls[up] = ClsRiver;
					mainUp[cur] = up;
					cur = up;
				}
			}
		}
	}

	// ---- 6. one polyline per source -----------------------------------------------------------------------
	for (int c = 0; c < n; c++) {
		if (cls[c] != ClsRiver || mainUp[c] >= 0) continue;
		int len = 0, cur = c;
		queue[len++] = c;
		for (int guard = 0; guard < n; guard++) {
			const int d = down[cur];
			if (d < 0) break;                            // the outlet of a world with no sea
			queue[len++] = d;
			if (cls[d] != ClsRiver || mainUp[d] != cur) break;   // sea, lake, or a confluence with a bigger branch
			cur = d;
		}
		if (len < 2) continue;                            // (cannot happen: the outlet is never a source, below)
		River r;
		r.cells.assign(queue, queue + len);
		r.flow.resize(size_t(len));
		for (int i = 0; i < len; i++) r.flow[size_t(i)] = flow[queue[i]];
		w.rivers.push_back(std::move(r));
	}
	// largest first, by the discharge entering the final cell; ties by source cell id
	std::sort(w.rivers.begin(), w.rivers.end(), [](const River& a, const River& b) {
		const float fa = a.flow[a.flow.size() - 2], fb = b.flow[b.flow.size() - 2];
		if (fa != fb) return fa > fb;
		return a.cells[0] < b.cells[0];
	});

	// ---- 7. hops to the nearest fresh water ---------------------------------------------------------------
	{
		const int cap = std::clamp(t.freshCap, 1, 255);
		int qh = 0, qt = 0;
		for (int c = 0; c < n; c++) {
			if (cls[c] == ClsLake || cls[c] == ClsRiver) { fresh[c] = 0; queue[qt++] = c; }
			else fresh[c] = 255;
		}
		while (qh < qt) {
			const int c = queue[qh++];
			const int d = fresh[c] + 1;
			if (d >= cap) break;                           // BFS order: everything still queued is at least this far
			for (int k = 0; k < nbrN[c]; k++) {
				const int b = nbr[6 * c + k];
				if (fresh[b] == 255) { fresh[b] = uint8_t(d); queue[qt++] = b; }
			}
		}
	}
}

} // namespace hydro
} // namespace dm
