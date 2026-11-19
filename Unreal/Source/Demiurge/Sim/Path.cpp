// DEMIURGE — Sim/Path.cpp : pathfinding on the cell graph (SPEC-SIM 3.6, DM.Path). Owner: A6 path-units.
//
// Cost of a step a -> b (b a neighbour of a), PathTune values:
//   lava[b] > lavaBlock                          impassable (whatever the mode)
//   b sea:  Land mode                            impassable
//           ice[b] > seaIceBlock                 impassable (pack ice)
//           Coastal and depth(b) >= coastalDepth impassable (canoes keep to the shelves)
//           a land                               embark (flat, whatever the terrain)
//           a sea                                Open: open; Coastal: coastal
//   b land: a sea                                embark (landing; allowed in every mode, so a party standing on a
//                                                cell that has just drowned can still walk out of it)
//           a land                               land + slopeK x |surface(b) - surface(a)|   (surface = elev + water,
//                                                so a lake is level) + forest (veg[b] >= forestVeg) + ice (ice[b] >
//                                                iceLand) + lake (water[b] > lakeWater) - roadPerLevel x min(road[a],
//                                                road[b]), never below roadFloor
// The start cell itself is never tested.
//
// find: A* with a consistent lower bound: (arc to the target / longest grid edge) x the cheapest step that can
// exist in this world and mode, so the route returned has the lowest possible cost. reach: Dijkstra. Both pop by
// (key, cell id), so equal keys resolve on the lower cell id: fully deterministic. Buffers live in the world's
// PathScratch (one search set per nesting depth: a reach visitor may itself call find / reach / stepCost); nothing
// is allocated per call except the route find returns.
#include "Path.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace dm {
namespace {

enum Mode : int { kModeLand = 0, kModeCoastal = 1, kModeOpen = 2 };
inline int modeOf(NavalMode m) { return m == NavalMode::Open ? kModeOpen : m == NavalMode::Coastal ? kModeCoastal : kModeLand; }

// One search's buffers (sized by the world's n).
struct Search {
	uint32_t stamp = 0;
	std::vector<uint32_t> mark;    // == stamp: reached in the current search
	std::vector<double> g;         // cost from the start
	std::vector<double> f;         // g + bound (find)
	std::vector<int32_t> hpos;     // index in the heap; -1 once settled
	std::vector<int32_t> prev;     // predecessor (find)
	std::vector<int32_t> heap;
	int hn = 0;
	const double* key = nullptr;   // f (find) or g (reach)

	void begin(int n) {
		if (mark.size() != size_t(n)) {
			mark.assign(size_t(n), 0u);
			g.assign(size_t(n), 0.0);
			f.assign(size_t(n), 0.0);
			hpos.assign(size_t(n), -1);
			prev.assign(size_t(n), -1);
			heap.assign(size_t(n), 0);
			stamp = 0;
		}
		if (stamp >= 0xfffffff0u) { std::fill(mark.begin(), mark.end(), 0u); stamp = 0; }
		stamp++;
		hn = 0;
	}
	bool before(int a, int b) const {   // heap order: (key, cell id)
		const double ka = key[a], kb = key[b];
		return ka < kb || (ka == kb && a < b);
	}
	void up(int i, int c) {             // put c at slot i and sift it up
		while (i > 0) {
			const int p = (i - 1) >> 1, pc = heap[size_t(p)];
			if (before(pc, c)) break;
			heap[size_t(i)] = pc;
			hpos[size_t(pc)] = i;
			i = p;
		}
		heap[size_t(i)] = c;
		hpos[size_t(c)] = i;
	}
	void push(int c) { up(hn++, c); }
	int pop() {
		const int top = heap[0];
		const int m = --hn;
		if (m > 0) {
			const int c = heap[size_t(m)];
			int i = 0;
			for (;;) {
				int k = 2 * i + 1;
				if (k >= m) break;
				int kc = heap[size_t(k)];
				if (k + 1 < m) {
					const int rc = heap[size_t(k + 1)];
					if (before(rc, kc)) { k++; kc = rc; }
				}
				if (before(c, kc)) break;
				heap[size_t(i)] = kc;
				hpos[size_t(kc)] = i;
				i = k;
			}
			heap[size_t(i)] = c;
			hpos[size_t(c)] = i;
		}
		hpos[size_t(top)] = -1;
		return top;
	}
};

struct PathScratch {
	const Geo* geo = nullptr;      // maxEdge belongs to this grid
	double maxEdge = 1;            // longest edge of the grid, as an arc (radians)
	int depth = 0;                 // > 0 while reach visitors run
	std::vector<std::unique_ptr<Search>> pool;   // one set per depth (unique_ptr: outer sets never move)
};

Search& searchAt(PathScratch& S, int depth) {
	while (int(S.pool.size()) <= depth) S.pool.push_back(std::make_unique<Search>());
	return *S.pool[size_t(depth)];
}

inline bool usable(const World& w) {
	return w.geo && w.n > 0 && w.geo->n == w.n && w.elev.size() == size_t(w.n);
}

// The cost model bound to one world and mode.
struct Model {
	const World& w;
	const PathTune& T;
	int mode;
	double sea;
	Model(const World& world, NavalMode naval) : w(world), T(world.tune<PathTune>()), mode(modeOf(naval)), sea(world.seaLevel) {}

	// false when b cannot be entered from a; else true with the cost in out
	bool step(int a, int b, double& out) const {
		const size_t ia = size_t(a), ib = size_t(b);
		if (double(w.lava[ib]) > T.lavaBlock) return false;
		const double ea = w.elev[ia], eb = w.elev[ib];
		if (eb < sea) {                                     // entering the sea
			if (mode == kModeLand || double(w.ice[ib]) > T.seaIceBlock) return false;
			if (mode == kModeCoastal && !(sea - eb < T.coastalDepth)) return false;
			out = ea >= sea ? T.embark : (mode == kModeOpen ? T.open : T.coastal);
			return true;
		}
		if (ea < sea) { out = T.embark; return true; }      // landing
		const double wa = w.water[ia], wb = w.water[ib];
		double c = T.land + T.slopeK * std::fabs((eb + wb) - (ea + wa));
		if (double(w.veg[ib]) >= T.forestVeg) c += T.forest;
		if (double(w.ice[ib]) > T.iceLand) c += T.ice;
		if (wb > T.lakeWater) c += T.lake;                  // a lake cell (hydro::isLake: b is land here)
		const int ra = w.road[ia], rb = w.road[ib];
		const int r = ra < rb ? ra : rb;
		if (r > 0) c -= T.roadPerLevel * r;
		out = c > T.roadFloor ? c : T.roadFloor;            // also turns a NaN into the floor
		return true;
	}
	// true when no step can ever enter c
	bool shut(int c) const {
		const size_t ic = size_t(c);
		if (double(w.lava[ic]) > T.lavaBlock) return true;
		const double e = w.elev[ic];
		if (e < sea) {
			if (mode == kModeLand || double(w.ice[ic]) > T.seaIceBlock) return true;
			if (mode == kModeCoastal && !(sea - e < T.coastalDepth)) return true;
		}
		return false;
	}
	// the cheapest step that can exist in this world and mode (for the A* lower bound); >= 0
	double minStep() const {
		double land = T.land;
		const bool surchargesPositive = T.slopeK >= 0 && T.forest >= 0 && T.ice >= 0 && T.lake >= 0;
		if (!surchargesPositive) land = T.roadFloor;
		else if (T.roadPerLevel > 0 && land > T.roadFloor) {
			const double enough = (land - T.roadFloor) / T.roadPerLevel;
			int mx = 0;
			for (const uint8_t r : w.road) {
				if (r > mx) { mx = r; if (mx >= enough) break; }
			}
			land -= T.roadPerLevel * mx;
		}
		if (!(land > T.roadFloor)) land = T.roadFloor;
		double m = land < T.embark ? land : T.embark;
		if (mode == kModeCoastal && T.coastal < m) m = T.coastal;
		if (mode == kModeOpen && T.open < m) m = T.open;
		return m > 0 ? m : 0;
	}
};

inline double chordArc(const float* p, double tx, double ty, double tz) {
	const double dx = double(p[0]) - tx, dy = double(p[1]) - ty, dz = double(p[2]) - tz;
	const double h = std::sqrt(dx * dx + dy * dy + dz * dz) * 0.5;
	return 2.0 * std::asin(h < 1 ? h : 1.0);
}

void ensureEdge(PathScratch& S, const Geo& g) {
	if (S.geo == &g) return;
	double mx = 0;
	for (int c = 0; c < g.n; c++) {
		const float* pc = &g.pos[size_t(3 * c)];
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
			const int b = g.nbr[size_t(6 * c + k)];
			if (b <= c) continue;
			const float* pb = &g.pos[size_t(3 * b)];
			const double a = chordArc(pb, pc[0], pc[1], pc[2]);
			if (a > mx) mx = a;
		}
	}
	S.maxEdge = mx > 0 ? mx * (1 + 1e-9) : 1;
	S.geo = &g;
}

inline bool isNeighbour(const Geo& g, int a, int b) {
	const int32_t* nb = g.nbrs(a);
	for (int k = 0; k < g.nbrN[size_t(a)]; k++)
		if (nb[k] == b) return true;
	return false;
}

} // namespace

namespace path {

double stepCost(const World& w, int a, int b, NavalMode naval) {
	if (!usable(w) || !w.valid(a) || !w.valid(b)) return kNoPath;
	if (a == b) return 0;
	if (!isNeighbour(*w.geo, a, b)) return kNoPath;
	const Model M(w, naval);
	double c = 0;
	return M.step(a, b, c) ? c : kNoPath;
}

double cost(const World& w, const std::vector<int32_t>& cells, NavalMode naval) {
	if (!usable(w) || cells.empty() || !w.valid(cells[0])) return kNoPath;
	double sum = 0;
	for (size_t k = 1; k < cells.size(); k++) {
		const double s = stepCost(w, cells[k - 1], cells[k], naval);
		if (!(s < kNoPath)) return kNoPath;
		sum += s;
	}
	return sum;
}

std::vector<int32_t> find(const World& w, int from, int to, const FindOpts& opts, double* cost) {
	if (cost) *cost = kNoPath;
	std::vector<int32_t> out;
	if (!usable(w) || !w.valid(from) || !w.valid(to)) return out;
	const PathTune& T = w.tune<PathTune>();
	double maxCost = opts.maxCost;
	if (std::isnan(maxCost)) maxCost = T.maxCost;
	if (from == to) {
		if (cost) *cost = 0;
		out.push_back(from);
		return out;
	}
	if (!(maxCost >= 0)) return out;
	const Model M(w, opts.naval);
	if (M.shut(to)) return out;
	// On foot, two different landmasses never connect (labels are current while need.derive is clear).
	if (M.mode == kModeLand && !w.need.derive && w.cache.landId.size() == size_t(w.n)) {
		const int la = w.cache.landId[size_t(from)], lb = w.cache.landId[size_t(to)];
		if (la >= 0 && lb >= 0 && la != lb) return out;
	}

	const Geo& g = *w.geo;
	PathScratch& PS = w.scratch<PathScratch>();
	ensureEdge(PS, g);
	Search& S = searchAt(PS, PS.depth);
	S.begin(w.n);
	S.key = S.f.data();
	const uint32_t stamp = S.stamp;
	const float* P = g.pos.data();
	const double tx = P[3 * to], ty = P[3 * to + 1], tz = P[3 * to + 2];
	const double scale = M.minStep() / PS.maxEdge * (1 - 1e-9);

	const double h0 = scale * chordArc(P + 3 * from, tx, ty, tz);
	if (h0 > maxCost) return out;
	S.g[size_t(from)] = 0;
	S.f[size_t(from)] = h0;
	S.mark[size_t(from)] = stamp;
	S.prev[size_t(from)] = -1;
	S.push(from);
	while (S.hn > 0) {
		const int c = S.pop();
		if (c == to) {
			size_t len = 1;
			for (int x = to; x != from; x = S.prev[size_t(x)]) len++;
			out.resize(len);
			int x = to;
			for (size_t i = len; i-- > 0;) { out[i] = x; x = S.prev[size_t(x)]; }
			if (cost) *cost = S.g[size_t(to)];
			return out;
		}
		const double gc = S.g[size_t(c)];
		const int32_t* nb = g.nbrs(c);
		const int deg = g.nbrN[size_t(c)];
		for (int k = 0; k < deg; k++) {
			const int b = nb[k];
			double sc = 0;
			if (S.mark[size_t(b)] == stamp) {
				if (S.hpos[size_t(b)] < 0 || !M.step(c, b, sc)) continue;   // settled, or closed from this side
				const double ng = gc + sc;
				if (ng < S.g[size_t(b)]) {
					S.f[size_t(b)] += ng - S.g[size_t(b)];
					S.g[size_t(b)] = ng;
					S.prev[size_t(b)] = c;
					S.up(S.hpos[size_t(b)], b);
				}
			} else {
				if (!M.step(c, b, sc)) continue;
				const double ng = gc + sc;
				const double fb = ng + scale * chordArc(P + 3 * b, tx, ty, tz);
				if (!(fb <= maxCost)) continue;                      // cannot lie on a route within the budget
				S.mark[size_t(b)] = stamp;
				S.g[size_t(b)] = ng;
				S.f[size_t(b)] = fb;
				S.prev[size_t(b)] = c;
				S.push(b);
			}
		}
	}
	return out;
}

int reach(const World& w, int from, double maxCost, NavalMode naval, const Visit& visit) {
	if (!usable(w) || !w.valid(from) || !visit) return 0;
	if (std::isnan(maxCost)) maxCost = w.tune<PathTune>().maxCost;
	if (!(maxCost >= 0)) return 0;
	const Geo& g = *w.geo;
	const Model M(w, naval);
	PathScratch& PS = w.scratch<PathScratch>();
	const int depth = PS.depth;
	Search& S = searchAt(PS, depth);   // reference stays valid: the pool holds unique_ptrs
	S.begin(w.n);
	S.key = S.g.data();
	const uint32_t stamp = S.stamp;
	S.g[size_t(from)] = 0;
	S.mark[size_t(from)] = stamp;
	S.push(from);
	int count = 0;
	while (S.hn > 0) {
		const int c = S.pop();
		const double gc = S.g[size_t(c)];
		count++;
		PS.depth = depth + 1;                  // searches started by the visitor use their own set
		const bool go = visit(c, gc);
		PS.depth = depth;
		if (!go) break;
		const int32_t* nb = g.nbrs(c);
		const int deg = g.nbrN[size_t(c)];
		for (int k = 0; k < deg; k++) {
			const int b = nb[k];
			double sc = 0;
			if (S.mark[size_t(b)] == stamp) {
				if (S.hpos[size_t(b)] < 0 || !M.step(c, b, sc)) continue;
				const double ng = gc + sc;
				if (ng < S.g[size_t(b)]) {
					S.g[size_t(b)] = ng;
					S.up(S.hpos[size_t(b)], b);
				}
			} else {
				if (!M.step(c, b, sc)) continue;
				const double ng = gc + sc;
				if (!(ng <= maxCost)) continue;
				S.mark[size_t(b)] = stamp;
				S.g[size_t(b)] = ng;
				S.push(b);
			}
		}
	}
	return count;
}

} // namespace path
} // namespace dm
