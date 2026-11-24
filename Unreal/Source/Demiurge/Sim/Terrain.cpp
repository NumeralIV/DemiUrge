// DEMIURGE — Sim/Terrain.cpp : dm::terrain (SPEC-SIM 3.2). The only writer of w.elev.
// Port of js/sim/terrain.js. Standard C++20 only (rules: FOUNDATION-CPP 1).
//
// How generate works (all in elevation units of the finished world; sea level 0):
//   1. Plates. 10-14 seed points on the sphere, each a plate with a size weight, a crust bias
//      (continental or oceanic), a rotation pole and a rank. Cells are assigned by domain-warped
//      distance, so plate edges are curved. Relative plate motion across an edge gives convergence
//      (mountain belts, island arcs, trenches) or divergence (mid-ocean ridges, rift valleys). A
//      second, coarser set of "fossil" plates marks old sutures: long worn ranges across continents.
//   2. Continents. Domain-warped fBm plus the plate bias (blended across edges) plus coastline
//      detail whose amplitude varies by region. Several plate layouts are scored on the level-4
//      cells (cheap) and the best is built: 3-6 real landmasses, no all-swallowing supercontinent.
//      The finished world is rated the same way; if its relief bridged the continents, the next
//      best layout is built (at most `rebuilds` more) and the best world is kept. The field is
//      rank-mapped: the top `land` share of cells is land, and a cell's rank inside land or sea
//      drives a hypsometric base curve (lowlands, interior swells, tablelands; shelf, slope, abyss).
//   3. Relief. Ridged belts along convergent edges, the old ranges, beaded island arcs and
//      trenches, mid-ocean ridges, rifts, abyssal hills, hotspot island chains, hills that grow
//      with altitude. Every noise is band-limited to what the cells can carry.
//   4. Tidying. Puddle-sized enclosed seas become land basins (lakes for Hydro), some specks sink.
//   5. Erosion. A priority flood from the sea gives every land cell a drainage path and a
//      catchment; an implicit stream-power pass cuts valleys headward, a talus limit removes
//      cliffs, and the lowlands are smoothed into plains.
//   6. Normalisation. The shoreline is put back on the exact quantile, then monotone remaps
//      guarantee the lowland share, the alpine share, the peak height and the deepest trench.
//
// generate uses only + - * /, sqrt, floor, dm::noise3 / hashf and dm::Rng (all deterministic to
// the bit), so a seed gives the same planet in every build.
//
// (v4 mythos, track M6 economy-world-v4, SPEC-MYTHOS 3.1) World archetypes. generate reads
// mythos::archetypeParams(w) (the row of settings.archetype) once and builds from an adjusted COPY of
// TerrainTune; Balanced changes nothing (every adjustment is skipped at its neutral value, so a
// Balanced world is bit-identical to v3.1):
//   ridgeK (Dangerous 1.3): more mountains and volcanic belts: belts, plateaus, old sutures and arcs are
//     ridgeK wider, arcs ridgeK higher, fewer fossil edges are left without a range (oldCut / ridgeK),
//     weaker convergence already raises a belt (convMin / ridgeK); per 0.3 of ridgeK one more hotspot chain
//     and two more fossil sutures; the normalisation's upland and alpine shares (1 - midQ, 1 - highQ) x ridgeK.
//   shelfK (Beautiful 1.3): more coast: more convoluted continents (warpCont x shelfK), ragged coastlines
//     (coastLo / coastHi x shelfK) and wide shallow shelves (shelfLim / shelfK).
//   islandK (Beautiful 1.3): more islands: more hotspot chains (x islandK), more one-cell islands survive
//     (speckKeep x islandK), island arcs islandK higher.
//   fert (Generous +0.15, Dangerous -0.05): added to the initial fertility before its 0.15..1 clamp
//     (Life adds the same offset to the natural level, so it lasts).
//   marvels (Strange 3..6): placed after the elevation is final, on land 0.5..4.5 above the shore with at
//     least 4 land neighbours, never within 7 hops of another (forked stream "terrain-marvels"); their
//     fert is at least 0.8. Written to vars.num (the only keys Terrain writes there; erased first by every
//     generate): "terrain_marvels" = count, "terrain_marvel_<i>" = cell id, "terrain_marvel_kind_<i>" =
//     0 singing stones, 1 warm springs in the ice, 2 a lake that glows, 3 a wood that whispers.
//     Life keeps them unburnt and their fert >= 0.8; Mythos / Legends / the client read the keys.
#include "Terrain.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "Mythos.h"

namespace dm {
namespace {

// ------------------------------------------------------------------ scratch (one per world)

struct TerrainScratch {
	int n = -1;
	std::vector<int32_t> list, queue, tmpI, ringOut;
	std::vector<float> val, val2;
	std::vector<int32_t> stamp;
	int32_t stampId = 0;
	std::vector<int32_t> landSize;          // cell count per landmass id (written by derive)
	std::shared_ptr<const Geo> geo4;        // the level-4 grid used to score plate layouts
};

TerrainScratch& scratchOf(const World& w) {
	TerrainScratch& s = w.scratch<TerrainScratch>();
	if (s.n != w.n) {
		const size_t n = size_t(w.n);
		s.n = w.n;
		s.list.assign(n, 0);
		s.queue.assign(n, 0);
		s.tmpI.assign(n, 0);
		s.val.assign(n, 0.0f);
		s.val2.assign(n, 0.0f);
		s.stamp.assign(n, 0);
		s.stampId = 0;
		s.list.clear();
		s.ringOut.clear();
	}
	return s;
}
int32_t nextStamp(TerrainScratch& s) {
	if (s.stampId >= 0x7ffffff0) {
		std::fill(s.stamp.begin(), s.stamp.end(), 0);
		s.stampId = 0;
	}
	return ++s.stampId;
}

bool validCell(const World& w, int c) {
	return w.geo && c >= 0 && c < w.n && size_t(w.n) == w.elev.size();
}
float clampElev(double v, float old) {
	if (!std::isfinite(v)) return old;                          // never write NaN / infinity
	if (v < state::kElevMin) return float(state::kElevMin);
	if (v > state::kElevMax) return float(state::kElevMax);
	return float(v);
}

// Squared angle between cells a and b, from the chord (exact to 1e-4 below 0.5 rad; acos beyond).
double angle2(const float* pos, int a, int b) {
	const size_t ia = size_t(3 * a), ib = size_t(3 * b);
	const double dx = double(pos[ia]) - pos[ib], dy = double(pos[ia + 1]) - pos[ib + 1], dz = double(pos[ia + 2]) - pos[ib + 2];
	const double ch2 = dx * dx + dy * dy + dz * dz;
	if (ch2 < 0.25) return ch2 * (1 + ch2 * (1.0 / 12 + ch2 * (1.0 / 90)));
	const double d = 1 - 0.5 * ch2;
	const double ac = std::acos(d < -1 ? -1 : d > 1 ? 1 : d);
	return ac * ac;
}

// Is cell m a one-cell pit: strictly below every neighbour?
bool isPit(const Geo& g, const std::vector<float>& elev, int m) {
	const float e = elev[size_t(m)];
	const int deg = g.nbrN[size_t(m)];
	for (int j = 0; j < deg; j++) {
		const int q = g.nbr[size_t(6 * m + j)];
		if (!(elev[size_t(q)] > e)) return false;
	}
	return deg > 0;
}
double lowestNbr(const Geo& g, const std::vector<float>& elev, int m) {
	double low = std::numeric_limits<double>::infinity();
	const int deg = g.nbrN[size_t(m)];
	for (int j = 0; j < deg; j++) {
		const int q = g.nbr[size_t(6 * m + j)];
		if (elev[size_t(q)] < low) low = elev[size_t(q)];
	}
	return low;
}

// ------------------------------------------------------------------ generation: helpers

constexpr double kOX = 31.416, kOY = -47.853, kOZ = 12.679;   // per-octave offsets, so octaves never line up

// fBm in about [-1, 1] whose octaves fade out as their frequency passes what the cells can carry
// (full below fmax, gone at 2 fmax): a coarse planet is the same planet with less detail.
double fbmB(double x, double y, double z, double f, int oct, double lac, double gain, double fmax, double ox, double oy, double oz) {
	double sum = 0, tot = 0, amp = 1;
	for (int o = 0; o < oct; o++) {
		tot += amp;
		if (f < 2 * fmax) {
			const double a = f <= fmax ? amp : amp * (2 - f / fmax);
			sum += a * noise3(x * f + ox + o * kOX, y * f + oy + o * kOY, z * f + oz + o * kOZ);
		}
		amp *= gain;
		f *= lac;
	}
	return sum / tot;
}
// Ridged multifractal in [0, 1] (sharp crests near 1), band-limited the same way.
double ridgeB(double x, double y, double z, double f, int oct, double lac, double gain, double fmax, double ox, double oy, double oz) {
	double sum = 0, tot = 0, amp = 1, wgt = 1;
	for (int o = 0; o < oct; o++) {
		tot += amp;
		if (f < 2 * fmax) {
			double v = noise3(x * f + ox + o * kOX, y * f + oy + o * kOY, z * f + oz + o * kOZ);
			v = 1 - (v < 0 ? -v : v);
			v *= v;
			sum += (f <= fmax ? amp : amp * (2 - f / fmax)) * wgt * v;
			wgt = v * 2;
			if (wgt > 1) wgt = 1;
		}
		amp *= gain;
		f *= lac;
	}
	return sum / tot;
}

double bump(double x, double c, double width) {   // smooth bump of half-width `width` centred on c
	const double t = (x - c) / width, u = 1 - t * t;
	return u > 0 ? u * u : 0;
}
void unitVec(Rng& rng, double* out) {
	const double x = rng.gauss(), y = rng.gauss(), z = rng.gauss();
	const double l = std::sqrt(x * x + y * y + z * z);
	if (l < 1e-9) { out[0] = 0; out[1] = 1; out[2] = 0; return; }
	out[0] = x / l; out[1] = y / l; out[2] = z / l;
}
double roundHalfUp(double x) { return std::floor(x + 0.5); }   // JS Math.round

// continuous rank of v in an ascending array (index + fraction)
double rankOf(const std::vector<float>& sorted, int n, double v) {
	int lo = 0, hi = n - 1;
	if (v <= sorted[0]) return 0;
	if (v >= sorted[size_t(hi)]) return hi;
	while (hi - lo > 1) {
		const int mid = (lo + hi) >> 1;
		if (sorted[size_t(mid)] <= v) lo = mid; else hi = mid;
	}
	const double a = sorted[size_t(lo)], b = sorted[size_t(hi)];
	return b > a ? lo + (v - a) / (b - a) : lo;
}
int landCountOf(const TerrainTune& P, int n) {
	const int k = int(roundHalfUp(P.land * n));
	return std::max(1, std::min(n - 1, k));
}
// the value that leaves exactly nLand cells at or above it (first n cells of E)
double shoreOf(const std::vector<float>& E, std::vector<float>& sorted, int n, int nLand) {
	std::copy(E.begin(), E.begin() + n, sorted.begin());
	std::sort(sorted.begin(), sorted.begin() + n);
	const int k = std::max(1, std::min(n - 1, nLand));
	return sorted[size_t(n - k)];
}
// Sizes of the connected components of { f >= thr } over the first n cells, largest first.
std::vector<int> components(const Geo& g, int n, const std::vector<float>& f, double thr, std::vector<uint8_t>& lab, std::vector<int32_t>& q) {
	std::vector<int> sizes;
	std::fill(lab.begin(), lab.begin() + n, uint8_t(0));
	for (int c0 = 0; c0 < n; c0++) {
		if (lab[size_t(c0)] || !(f[size_t(c0)] >= thr)) continue;
		int head = 0, end = 0;
		q[size_t(end++)] = c0;
		lab[size_t(c0)] = 1;
		while (head < end) {
			const int c = q[size_t(head++)];
			const int deg = g.nbrN[size_t(c)];
			for (int j = 0; j < deg; j++) {
				const int m = g.nbr[size_t(6 * c + j)];
				if (!lab[size_t(m)] && f[size_t(m)] >= thr) { lab[size_t(m)] = 1; q[size_t(end++)] = m; }
			}
		}
		sizes.push_back(end);
	}
	std::sort(sizes.begin(), sizes.end(), [](int a, int b) { return a > b; });
	return sizes;
}

// ------------------------------------------------------------------ generation: plates and continents

struct Plates {
	int K = 0;
	std::vector<double> seed, rot, wt, bias, prank, dist;
	int K2 = 0;
	std::vector<double> fseed, fstr;
	double o[27] = {};
};

// Every per-cell buffer generate needs (the JS `G` object).
struct Gen {
	std::vector<float> cont, u, belt, wide, arc, trench, riftW, riftN, anX, anY, anZ, old, volc, sorted, acc, val, hk;
	std::vector<int32_t> hc, par, order, queue, ringOut;
	std::vector<uint8_t> lab;
	explicit Gen(int nCells) {
		const size_t n = size_t(nCells);
		for (std::vector<float>* v : { &cont, &u, &belt, &wide, &arc, &trench, &riftW, &riftN, &anX, &anY, &anZ, &old, &volc, &sorted, &acc, &val, &hk })
			v->assign(n, 0.0f);
		for (std::vector<int32_t>* v : { &hc, &par, &order, &queue }) v->assign(n, 0);
		lab.assign(n, 0);
	}
};

// Step 1: a plate layout (everything random about the large-scale shape of the world).
Plates makePlates(const TerrainTune& P, Rng rng) {
	Plates PL;
	// at least two plates and two fossil plates whatever the tuning says (0 would index empty tables;
	// the clamp draws nothing extra, so the default 10-14 / 7-10 layouts are unchanged)
	const int K = std::max(2, rng.integer(P.platesMin, P.platesMax));
	PL.K = K;
	PL.seed.assign(size_t(3 * K), 0);
	PL.rot.assign(size_t(3 * K), 0);
	PL.wt.assign(size_t(K), 0);
	PL.bias.assign(size_t(K), 0);
	PL.prank.assign(size_t(K), 0);
	PL.dist.assign(size_t(K), 0);
	double cand[3];
	for (int i = 0; i < K; i++) {
		double best = -1;
		for (int t = 0; t < (i == 0 ? 1 : 6); t++) {                 // best of six candidates: spread out, not regular
			unitVec(rng, cand);
			double md = 4;
			for (int j = 0; j < i; j++) {
				const double d = 2 - 2 * (cand[0] * PL.seed[size_t(3 * j)] + cand[1] * PL.seed[size_t(3 * j + 1)] + cand[2] * PL.seed[size_t(3 * j + 2)]);
				if (d < md) md = d;
			}
			if (md > best) {
				best = md;
				PL.seed[size_t(3 * i)] = cand[0]; PL.seed[size_t(3 * i + 1)] = cand[1]; PL.seed[size_t(3 * i + 2)] = cand[2];
			}
		}
		PL.wt[size_t(i)] = rng.range(0, 0.11);
		unitVec(rng, &PL.rot[size_t(3 * i)]);
		const double rate = rng.range(0.45, 1);
		PL.rot[size_t(3 * i)] *= rate; PL.rot[size_t(3 * i + 1)] *= rate; PL.rot[size_t(3 * i + 2)] *= rate;
		PL.bias[size_t(i)] = -rng.range(0.6, 1);
		PL.prank[size_t(i)] = rng.f();
	}
	// continental plates: each pick is the better-separated of two random candidates
	const int nCont = std::max(3, int(roundHalfUp(K * P.contShare)));
	std::vector<uint8_t> isCont(size_t(K), 0);
	for (int i = 0; i < nCont && i < K; i++) {
		int pick = -1;
		double pickD = -1;
		for (int t = 0; t < 2; t++) {
			int k = rng.integer(0, K - 1), guard = 0;
			while (isCont[size_t(k)] && guard++ < K) k = (k + 1) % K;
			double md = 4;
			for (int j = 0; j < K; j++) {
				if (!isCont[size_t(j)]) continue;
				const double d = 2 - 2 * (PL.seed[size_t(3 * k)] * PL.seed[size_t(3 * j)] + PL.seed[size_t(3 * k + 1)] * PL.seed[size_t(3 * j + 1)] +
				                          PL.seed[size_t(3 * k + 2)] * PL.seed[size_t(3 * j + 2)]);
				if (d < md) md = d;
			}
			if (md > pickD) { pickD = md; pick = k; }
		}
		isCont[size_t(pick)] = 1;
		PL.bias[size_t(pick)] = rng.range(0.55, 1);
		PL.prank[size_t(pick)] += 1;                                 // continental crust overrides oceanic crust
	}
	// fossil plates: their edges are the old sutures; each edge has its own strength (half carry nothing)
	const int K2 = std::max(2, rng.integer(P.fossilMin, P.fossilMax));
	PL.K2 = K2;
	PL.fseed.assign(size_t(3 * K2), 0);
	PL.fstr.assign(size_t(K2 * K2), 0);
	for (int i = 0; i < K2; i++) {
		double best = -1;
		for (int t = 0; t < (i == 0 ? 1 : 3); t++) {
			unitVec(rng, cand);
			double md = 4;
			for (int j = 0; j < i; j++) {
				const double d = 2 - 2 * (cand[0] * PL.fseed[size_t(3 * j)] + cand[1] * PL.fseed[size_t(3 * j + 1)] + cand[2] * PL.fseed[size_t(3 * j + 2)]);
				if (d < md) md = d;
			}
			if (md > best) {
				best = md;
				PL.fseed[size_t(3 * i)] = cand[0]; PL.fseed[size_t(3 * i + 1)] = cand[1]; PL.fseed[size_t(3 * i + 2)] = cand[2];
			}
		}
	}
	for (int i = 0; i < K2; i++) {
		for (int j = i + 1; j < K2; j++) {
			const double v = (rng.f() - P.oldCut) / (1 - P.oldCut);
			PL.fstr[size_t(i * K2 + j)] = PL.fstr[size_t(j * K2 + i)] = v > 0 ? 0.45 + 0.55 * v : 0;
		}
	}
	for (int i = 0; i < 27; i++) PL.o[i] = rng.range(-400, 400);
	return PL;
}

// Step 1 + 2 for the first n cells: the continent field G.cont and, when `full`, the plate-edge
// envelopes (belt, wide, arc, trench, riftW, riftN, the across-belt vector an*) and the old ranges.
void contField(const TerrainTune& P, const float* pos, int n, Plates& PL, Gen& G, bool full, double fmax) {
	const int K = PL.K, K2 = PL.K2;
	const double* seed = PL.seed.data();
	const double* rot = PL.rot.data();
	const double* o = PL.o;
	double* dist = PL.dist.data();
	const double wf = P.warpF, wf2 = wf * 2.07;
	const double edgeMax = full ? std::max({ P.wideW, P.biasW, P.beltC + P.beltW, P.arcC + P.arcW }) * 1.02 : P.biasW;
	const double contNorm = 1 / 0.2;                               // fBm of 5 octaves has a standard deviation near 0.2

	for (int c = 0; c < n; c++) {
		const double x = pos[3 * c], y = pos[3 * c + 1], z = pos[3 * c + 2];
		// domain warp (two octaves of vector noise)
		const double wx = noise3(x * wf + o[0], y * wf + o[1], z * wf + o[2]) + 0.5 * noise3(x * wf2 + o[3], y * wf2 + o[4], z * wf2 + o[5]);
		const double wy = noise3(x * wf + o[6], y * wf + o[7], z * wf + o[8]) + 0.5 * noise3(x * wf2 + o[9], y * wf2 + o[10], z * wf2 + o[11]);
		const double wz = noise3(x * wf + o[12], y * wf + o[13], z * wf + o[14]) + 0.5 * noise3(x * wf2 + o[15], y * wf2 + o[16], z * wf2 + o[17]);

		// nearest plates in the warped frame
		double qx = x + P.warpPlate * wx, qy = y + P.warpPlate * wy, qz = z + P.warpPlate * wz;
		const double ql = 1 / std::sqrt(qx * qx + qy * qy + qz * qz);
		qx *= ql; qy *= ql; qz *= ql;
		double d1 = 9, d2 = 9;
		int i1 = 0, i2 = 0;
		for (int k = 0; k < K; k++) {
			const double dot = qx * seed[3 * k] + qy * seed[3 * k + 1] + qz * seed[3 * k + 2];
			const double d = std::sqrt(dot < 1 ? 2 - 2 * dot : 0) - PL.wt[size_t(k)];
			dist[k] = d;
			if (d < d1) { d2 = d1; i2 = i1; d1 = d; i1 = k; } else if (d < d2) { d2 = d; i2 = k; }
		}

		// crust bias blended across edges; edge envelopes from every plate near enough
		double bsum = PL.bias[size_t(i1)], bw = 1;
		double eBelt = 0, eWide = 0, eArc = 0, eTrench = 0, eRiftW = 0, eRiftN = 0;
		double ax = 0, ay = 0, az = 0;
		const double s1x = seed[3 * i1], s1y = seed[3 * i1 + 1], s1z = seed[3 * i1 + 2];
		for (int k = 0; k < K; k++) {
			if (k == i1) continue;
			const double e = 0.5 * (dist[k] - d1);                    // distance to the edge with plate k
			if (e >= edgeMax) continue;
			if (e < P.biasW) { const double t = 1 - e / P.biasW, wb = t * t; bsum += wb * PL.bias[size_t(k)]; bw += wb; }
			if (!full) continue;
			// unit tangent pointing at plate k, and the relative velocity of plate i1 along it
			const double dx = seed[3 * k] - s1x, dy = seed[3 * k + 1] - s1y, dz = seed[3 * k + 2] - s1z;
			const double dp = dx * x + dy * y + dz * z;
			double nx = dx - dp * x, ny = dy - dp * y, nz = dz - dp * z;
			const double nl = std::sqrt(nx * nx + ny * ny + nz * nz);
			if (nl < 1e-6) continue;
			nx /= nl; ny /= nl; nz /= nl;
			const double rx = rot[3 * i1] - rot[3 * k], ry = rot[3 * i1 + 1] - rot[3 * k + 1], rz = rot[3 * i1 + 2] - rot[3 * k + 2];
			const double conv = (ry * z - rz * y) * nx + (rz * x - rx * z) * ny + (rx * y - ry * x) * nz;
			if (conv > 0) {
				double st = (conv - P.convMin) / P.convSpan;
				if (st <= 0) continue;
				if (st > 1) st = 1;
				const bool over = PL.prank[size_t(i1)] > PL.prank[size_t(k)];
				eBelt += st * bump(e, P.beltC, P.beltW) * (over ? 1 : P.beltUnder);
				eWide += st * bump(e, 0, P.wideW) * (over ? 1 : 0.5);
				if (over) eArc += st * bump(e, P.arcC, P.arcW); else eTrench += st * bump(e, P.trenchC, P.trenchW);
			} else {
				double st = (-conv - P.convMin) / P.convSpan;
				if (st <= 0) continue;
				if (st > 1) st = 1;
				eRiftW += st * bump(e, 0, P.riftW);
				eRiftN += st * bump(e, 0, P.riftN);
			}
			if (k == i2) { ax = -e * nx; ay = -e * ny; az = -e * nz; }   // across-belt coordinate (continuous over the edge)
		}

		// continent field
		const double base = fbmB(x + P.warpCont * wx, y + P.warpCont * wy, z + P.warpCont * wz, P.contF, P.contOct, 2.03, 0.5, fmax,
		                         o[18], o[19], o[20]) * contNorm;
		const double rag = sat(0.5 + 1.6 * noise3(x * 1.7 + o[21], y * 1.7 + o[22], z * 1.7 + o[23]));
		const double det = fbmB(x, y, z, P.coastF, 3, 2.1, 0.55, fmax, o[24], o[25], o[26]);
		G.cont[size_t(c)] = float(base + P.plateAmp * (bsum / bw) + det * (P.coastLo + (P.coastHi - P.coastLo) * rag * rag) * contNorm * 0.5);
		if (!full) continue;

		const size_t cc = size_t(c);
		G.belt[cc] = float(eBelt > 1 ? 1 : eBelt);
		G.wide[cc] = float(eWide > 1 ? 1 : eWide);
		G.arc[cc] = float(eArc > 1 ? 1 : eArc);
		G.trench[cc] = float(eTrench > 1 ? 1 : eTrench);
		G.riftW[cc] = float(eRiftW > 1 ? 1 : eRiftW);
		G.riftN[cc] = float(eRiftN > 1 ? 1 : eRiftN);
		G.anX[cc] = float(ax); G.anY[cc] = float(ay); G.anZ[cc] = float(az);

		// old sutures: the edges of the fossil plates (their own, stronger warp)
		qx = x + P.warpFossil * wy; qy = y + P.warpFossil * wz; qz = z + P.warpFossil * wx;
		const double fl = 1 / std::sqrt(qx * qx + qy * qy + qz * qz);
		qx *= fl; qy *= fl; qz *= fl;
		double f1 = 9, f2 = 9;
		int j1 = 0, j2 = 0;
		for (int k = 0; k < K2; k++) {
			const double dot = qx * PL.fseed[size_t(3 * k)] + qy * PL.fseed[size_t(3 * k + 1)] + qz * PL.fseed[size_t(3 * k + 2)];
			const double d = dot < 1 ? 2 - 2 * dot : 0;                // squared chord: enough to order them
			if (d < f1) { f2 = f1; j2 = j1; f1 = d; j1 = k; } else if (d < f2) { f2 = d; j2 = k; }
		}
		const double fe = 0.5 * (std::sqrt(f2) - std::sqrt(f1));
		G.old[cc] = float(fe < P.oldW ? bump(fe, 0, P.oldW) * PL.fstr[size_t(j1 * K2 + j2)] : 0);
	}
}

// How good is a plate layout? Judged on the level-4 cells: at least two real continents, 3-6
// major landmasses, no supercontinent. 5.5 = full marks.
double scoreLayout(const TerrainTune& P, const float* pos, Plates& PL, Gen& S4, const Geo& g4, double fmax) {
	const int n4 = g4.n;
	contField(P, pos, n4, PL, S4, false, fmax);
	// judged with a little extra land, so that landmasses count as separate only when the strait
	// between them survives the relief that is still to come
	const int nLand = std::min(n4 - 1, int(roundHalfUp((P.land + P.scoreExtra) * n4)));
	const double thr = shoreOf(S4.cont, S4.sorted, n4, nLand);
	const std::vector<int> sizes = components(g4, n4, S4.cont, thr, S4.lab, S4.queue);
	int major = 0;
	for (const int sz : sizes) if (sz >= 0.02 * n4) major++;
	const double s0 = !sizes.empty() ? double(sizes[0]) / n4 : 0;
	const double s1 = sizes.size() > 1 ? double(sizes[1]) / n4 : 0;
	const double s2 = sizes.size() > 2 ? double(sizes[2]) / n4 : 0;
	const double top = nLand > 0 ? s0 * n4 / nLand : 1;
	double score = 0;
	if (s1 >= 0.045) score += 2; else if (s1 >= 0.035) score += 1;
	if (major >= 3 && major <= 6) score += 2; else if (major > 6) score += 1;
	if (top <= 0.5) score += 1; else if (top <= 0.62) score += 0.5;
	if (s2 >= 0.03) score += 0.5;
	return score;
}

// Rank map: G.u in (0, 1] on land (1 = the most continental cell), [-1, 0) at sea.
void rankMap(const TerrainTune& P, Gen& G, int n) {
	std::copy(G.cont.begin(), G.cont.begin() + n, G.sorted.begin());
	std::sort(G.sorted.begin(), G.sorted.begin() + n);
	const int nLand = landCountOf(P, n), ks = n - nLand;         // ks = index of the first land value
	for (int c = 0; c < n; c++) {
		const double r = rankOf(G.sorted, n, G.cont[size_t(c)]);
		double v = r >= ks ? (r - ks + 0.5) / nLand : (r - ks + 0.5) / ks;
		if (v > 1) v = 1; else if (v < -1) v = -1;
		G.u[size_t(c)] = float(v);
	}
}

// ------------------------------------------------------------------ generation: relief

void relief(const TerrainTune& P, const World& w, Rng& rng, Gen& G, std::vector<float>& E, double fmax) {
	const int n = w.n;
	const float* pos = w.geo->pos.data();
	double p[39];
	for (int i = 0; i < 39; i++) p[i] = rng.range(-400, 400);
	const double rs = P.ridgeStretch, tf = P.tabF, tf2 = tf * 2.3, sf = P.strikeF;

	for (int c = 0; c < n; c++) {
		const size_t cc = size_t(c);
		const double x = pos[3 * c], y = pos[3 * c + 1], z = pos[3 * c + 2];
		const double uu = G.u[cc];
		const double landness = sstep(-0.11, 0.07, uu);            // 1 on land, fading over the inner shelf
		const double b = G.belt[cc], od = G.old[cc];

		// shared noises
		const double ridge = ridgeB(x + G.anX[cc] * rs, y + G.anY[cc] * rs, z + G.anZ[cc] * rs, P.ridgeF, 3, 2.1, 0.55, 2 * fmax, p[0], p[1], p[2]);
		const double hill = fbmB(x, y, z, P.hillF, 3, 2.1, 0.5, fmax, p[3], p[4], p[5]);
		const double basin = 0.5 + 0.5 * noise3(x * 1.6 + p[6], y * 1.6 + p[7], z * 1.6 + p[8]);

		double e;
		if (uu >= 0) {
			// land base: broad lowlands near the coast; interiors that swell into uplands here and sag into basins there
			const double swell = fbmB(x, y, z, P.swellF, 2, 2.2, 0.5, fmax, p[9], p[10], p[11]);
			e = 0.03 + P.lowSlope * uu + P.upland * uu * uu * (0.3 + 1.4 * basin * basin) + P.swellH * uu * swell;
			const double floorE = 0.03 + 0.3 * P.lowSlope * uu;
			if (e < floorE) e = floorE;
			// tablelands: flat-topped uplands with a crisp escarpment
			const double tn = noise3(x * tf + p[12], y * tf + p[13], z * tf + p[14]) + 0.4 * noise3(x * tf2 + p[15], y * tf2 + p[16], z * tf2 + p[17]);
			if (tn > P.tabCut) e += P.tabH * sstep(P.tabCut, P.tabCut + P.tabEdge, tn) * sstep(0.1, 0.3, uu) * (0.6 + 0.6 * basin);
		} else {
			// sea base: shelf, continental slope, abyssal plain. Shelves are narrow at subduction edges.
			const double shelfN = 0.5 + 0.5 * noise3(x * 2.2 + p[18], y * 2.2 + p[19], z * 2.2 + p[20]);
			double v = -uu * (0.55 + 0.9 * shelfN + 2.2 * (G.trench[cc] + 0.6 * b));
			if (v > 1) v = 1;
			if (v < P.shelfV) {
				const double t = v / P.shelfV;
				e = -0.03 - P.shelfDepth * t * std::sqrt(t);
			} else if (v < P.slopeV) {
				e = -0.03 - P.shelfDepth - (P.slopeDepth - P.shelfDepth) * sstep(P.shelfV, P.slopeV, v);
			} else {
				const double t = (v - P.slopeV) / (1 - P.slopeV);
				const double ab = fbmB(x, y, z, 3.1, 3, 2.1, 0.5, fmax, p[21], p[22], p[23]);
				e = -0.03 - P.slopeDepth - P.abyssDepth * std::sqrt(t) * (0.2 + 0.8 * basin) + ab * 1.1 * sat(t * 4);
			}
		}

		// mountains (on land, and running out to sea as peninsulas and coastal islands)
		if (landness > 0) {
			// massifs and saddles along the strike of a belt
			const double strike = sat(0.62 + 0.8 * noise3(x * sf + p[24], y * sf + p[25], z * sf + p[26]));
			const double mtn = P.beltH * b * (0.4 + 0.6 * strike) * (0.4 + 0.8 * ridge) + P.plateauH * G.wide[cc] * (0.6 + 0.4 * basin) +
			                   P.oldH * od * (0.35 + 0.65 * strike) * (0.45 + 0.75 * ridge);
			e += mtn * landness;
			// rift valleys on land
			e -= P.riftH * G.riftN[cc] * landness * (1 - b);
		}
		// hills: quiet on the plains, rougher with altitude and inside the belts
		double rough = P.hillLow + P.hillHigh * sat((e - 0.25) / 1.6) + P.hillBelt * std::min(1.0, b + od);
		if (uu > 0) rough += P.hillRegion * sstep(0.05, 0.5, noise3(x * 1.9 + p[30], y * 1.9 + p[31], z * 1.9 + p[32])) * sat(uu * 6);   // hill country
		e += hill * rough * (0.25 + 0.75 * landness) * 1.6;

		// the deep sea: arcs, trenches, spreading ridges
		const double deep = 1 - landness;
		if (deep > 0) {
			const double off = sstep(0.12, 0.35, -uu);                // 0 on the shelf, 1 in open ocean
			const double bead = sat(0.25 + 1.5 * fbmB(x, y, z, 13, 1, 2, 0.5, fmax, p[27], p[28], p[29]));
			const double a = G.arc[cc];
			e += deep * off * (P.arcSwell * a + P.arcH * a * a * bead * (0.55 + 0.6 * ridge));
			e -= deep * off * P.trenchH * G.trench[cc];
			e += deep * off * (P.ridgeH * G.riftW[cc] * (0.7 + 0.6 * ridge) - 0.45 * G.riftN[cc]);
			G.volc[cc] = float(a * bead * off);
		} else {
			G.volc[cc] = 0;
		}
		G.volc[cc] = float(G.volc[cc] + 0.5 * b);
		E[cc] = float(e);
	}
}

// Hotspot chains: a line of volcanoes of decreasing age across the deep ocean; the young ones are islands.
void hotspots(const TerrainTune& P, const World& w, Rng& rng, Gen& G, std::vector<float>& E) {
	const Geo& geo = *w.geo;
	const float* pos = geo.pos.data();
	const double step = geo.step;
	const int chains = rng.integer(P.chainsMin, P.chainsMax);
	double v[6];
	for (int ch = 0; ch < chains; ch++) {
		// start over deep water
		int c0 = -1;
		for (int t = 0; t < 30 && c0 < 0; t++) {
			unitVec(rng, v);
			const int c = geo.cellAt(v[0], v[1], v[2]);
			if (c >= 0 && E[size_t(c)] < -3) c0 = c;
		}
		const int len = rng.integer(4, 7);
		const double gap = rng.range(0.07, 0.105), top = rng.range(0.9, 2.4);
		unitVec(rng, v + 3);
		if (c0 < 0) continue;
		double px = pos[3 * c0], py = pos[3 * c0 + 1], pz = pos[3 * c0 + 2];
		// tangent heading
		double dp = v[3] * px + v[4] * py + v[5] * pz;
		double tx = v[3] - dp * px, ty = v[4] - dp * py, tz = v[5] - dp * pz;
		double tl = std::sqrt(tx * tx + ty * ty + tz * tz);
		if (tl < 1e-6) continue;
		tx /= tl; ty /= tl; tz /= tl;
		for (int i = 0; i < len; i++) {
			const int c = geo.cellAt(px, py, pz);
			const double r = std::max(rng.range(0.034, 0.056), 1.05 * step);
			const double summit = top - i * rng.range(0.45, 0.9) + rng.range(-0.25, 0.25);
			if (c >= 0 && E[size_t(c)] < -1.6) {
				const double h = summit - E[size_t(c)];
				const int cnt = geo.ring(c, int(std::ceil(3.2 * r / step)) + 1, G.ringOut);
				for (int k = 0; k < cnt; k++) {
					const int m = G.ringOut[size_t(k)];
					const double d = std::sqrt(angle2(pos, c, m));
					if (d < r) { const double q = 1 - d / r; E[size_t(m)] = float(E[size_t(m)] + h * q * std::sqrt(q)); G.volc[size_t(m)] = float(G.volc[size_t(m)] + q); }
					if (d < 3 * r) { const double q = 1 - d / (3 * r); E[size_t(m)] = float(E[size_t(m)] + 0.9 * q * q); }
				}
			}
			// step along the great circle (curving a little)
			const double g = gap * rng.range(0.8, 1.25);
			px += tx * g; py += ty * g; pz += tz * g;
			const double pl = 1 / std::sqrt(px * px + py * py + pz * pz);
			px *= pl; py *= pl; pz *= pl;
			const double bend = rng.range(-0.25, 0.25);
			const double sxx = py * tz - pz * ty, syy = pz * tx - px * tz, szz = px * ty - py * tx;   // sideways
			tx += bend * sxx; ty += bend * syy; tz += bend * szz;
			dp = tx * px + ty * py + tz * pz;
			tx -= dp * px; ty -= dp * py; tz -= dp * pz;
			tl = std::sqrt(tx * tx + ty * ty + tz * tz);
			if (tl < 1e-6) break;
			tx /= tl; ty /= tl; tz /= tl;
		}
	}
}

// ------------------------------------------------------------------ generation: tidying and erosion

// Step 4. Enclosed seas smaller than minSea become land basins just above the shore (hollows that
// Hydro turns into lakes where it rains), and some one-cell islands sink into shoals. Returns the
// net number of cells that turn into land; with apply = false nothing is written.
int tidy(const TerrainTune& P, const World& w, Gen& G, std::vector<float>& E, double shore, bool apply) {
	const int n = w.n;
	const Geo& g = *w.geo;
	const uint32_t seed = w.seed;
	const int minSea = std::max(2, int(roundHalfUp(P.minSea * n)));
	int net = 0;
	// one-cell islands (decided before anything is written)
	std::vector<int32_t>& specks = G.queue;
	int nSpeck = 0;
	for (int c = 0; c < n; c++) {
		if (E[size_t(c)] < shore) continue;
		bool alone = true;
		const int deg = g.nbrN[size_t(c)];
		for (int j = 0; j < deg; j++) {
			const int m = g.nbr[size_t(6 * c + j)];
			if (E[size_t(m)] >= shore) { alone = false; break; }
		}
		if (alone && hashf(uint32_t(c), seed) >= P.speckKeep) specks[size_t(nSpeck++)] = c;
	}
	// sea components, each a contiguous run of q; the largest is the ocean
	std::vector<uint8_t>& lab = G.lab;
	std::vector<int32_t>& q = G.order;
	std::vector<int32_t>& starts = G.par;                          // start of each component in q
	std::fill(lab.begin(), lab.end(), uint8_t(0));
	int end = 0, nComp = 0, big = 0;
	for (int c0 = 0; c0 < n; c0++) {
		if (lab[size_t(c0)] || E[size_t(c0)] >= shore) continue;
		const int start = end;
		int head = end;
		q[size_t(end++)] = c0;
		lab[size_t(c0)] = 1;
		while (head < end) {
			const int c = q[size_t(head++)];
			const int deg = g.nbrN[size_t(c)];
			for (int j = 0; j < deg; j++) {
				const int m = g.nbr[size_t(6 * c + j)];
				if (!lab[size_t(m)] && E[size_t(m)] < shore) { lab[size_t(m)] = 1; q[size_t(end++)] = m; }
			}
		}
		starts[size_t(nComp++)] = start;
		if (end - start > big) big = end - start;
	}
	for (int k = 0; k < nComp; k++) {
		const int a = starts[size_t(k)], zEnd = k + 1 < nComp ? starts[size_t(k + 1)] : end, size = zEnd - a;
		if (size >= minSea || size == big) continue;
		net += size;
		if (apply) {
			for (int i = a; i < zEnd; i++) {
				const int c = q[size_t(i)];
				E[size_t(c)] = float(shore + 0.04 + 0.03 * hashf(uint32_t(c), seed + 7u));
			}
		}
	}
	net -= nSpeck;
	if (apply) {
		for (int i = 0; i < nSpeck; i++) {
			const int c = specks[size_t(i)];
			E[size_t(c)] = float(shore - 0.12 - 0.1 * hashf(uint32_t(c), seed + 11u));
		}
	}
	return net;
}

// Shelves: near land the sea floor may only be so deep per hop from the shore (deeper at an active
// margin, where a trench or a mountain belt runs along the coast), so coasts shelve into shallows.
void shelve(const TerrainTune& P, const World& w, Gen& G, std::vector<float>& E, double shore, const double* o) {
	const int n = w.n;
	const Geo& g = *w.geo;
	const float* pos = g.pos.data();
	std::vector<int32_t>& q = G.order;
	std::vector<uint8_t>& hop = G.lab;
	const int maxH = int(P.shelfLim.size());
	std::fill(hop.begin(), hop.end(), uint8_t(0));
	int end = 0;
	for (int c = 0; c < n; c++) {
		if (E[size_t(c)] >= shore) continue;
		const int deg = g.nbrN[size_t(c)];
		for (int j = 0; j < deg; j++) {
			const int m = g.nbr[size_t(6 * c + j)];
			if (E[size_t(m)] >= shore) { hop[size_t(c)] = 1; q[size_t(end++)] = c; break; }
		}
	}
	for (int head = 0; head < end; head++) {
		const int c = q[size_t(head)], h = hop[size_t(c)];
		double act = G.trench[size_t(c)] + 0.6 * G.belt[size_t(c)];
		if (act > 1) act = 1;
		const double wideN = 0.7 + 0.3 * noise3(pos[3 * c] * 2.2 + o[21], pos[3 * c + 1] * 2.2 + o[22], pos[3 * c + 2] * 2.2 + o[23]);   // 0.4 ... 1
		const double floorE = shore - P.shelfLim[size_t(h - 1)] * (1 + P.shelfActive * act) / wideN;
		if (E[size_t(c)] < floorE) E[size_t(c)] = float(floorE);
		if (h >= maxH) continue;
		const int deg = g.nbrN[size_t(c)];
		for (int j = 0; j < deg; j++) {
			const int m = g.nbr[size_t(6 * c + j)];
			if (!hop[size_t(m)] && E[size_t(m)] < shore) { hop[size_t(m)] = uint8_t(h + 1); q[size_t(end++)] = m; }
		}
	}
}

// binary min-heap of (key, cell) in two parallel arrays
int heapPush(std::vector<float>& hk, std::vector<int32_t>& hc, int size, float key, int cell) {
	int i = size;
	while (i > 0) {
		const int pi = (i - 1) >> 1;
		if (hk[size_t(pi)] <= key) break;
		hk[size_t(i)] = hk[size_t(pi)];
		hc[size_t(i)] = hc[size_t(pi)];
		i = pi;
	}
	hk[size_t(i)] = key;
	hc[size_t(i)] = cell;
	return size + 1;
}
int heapPop(std::vector<float>& hk, std::vector<int32_t>& hc, int size) {   // removes the root; the caller has read it
	size--;
	if (size <= 0) return 0;
	const float key = hk[size_t(size)];
	const int cell = hc[size_t(size)];
	int i = 0;
	for (;;) {
		int ch = 2 * i + 1;
		if (ch >= size) break;
		if (ch + 1 < size && hk[size_t(ch + 1)] < hk[size_t(ch)]) ch++;
		if (hk[size_t(ch)] >= key) break;
		hk[size_t(i)] = hk[size_t(ch)];
		hc[size_t(i)] = hc[size_t(ch)];
		i = ch;
	}
	hk[size_t(i)] = key;
	hc[size_t(i)] = cell;
	return size;
}

// Step 5. Drainage by priority flood from the sea, then valleys cut along it. Fills G.acc
// (catchment size in cells, 0 at sea) for the fertility of the flood plains.
void erode(const TerrainTune& P, const World& w, Gen& G, std::vector<float>& E, double shore) {
	const int n = w.n;
	const Geo& g = *w.geo;
	const double step = g.step;
	std::vector<float>& hk = G.hk;
	std::vector<int32_t>& hc = G.hc;
	std::vector<int32_t>& par = G.par;
	std::vector<int32_t>& order = G.order;
	std::vector<float>& acc = G.acc;
	std::vector<uint8_t>& st = G.lab;
	std::vector<float>& val = G.val;
	int size = 0, cnt = 0;
	// the flood starts from every land cell that touches the sea
	for (int c = 0; c < n; c++) {
		acc[size_t(c)] = 0;
		if (E[size_t(c)] < shore) { st[size_t(c)] = 2; continue; }
		st[size_t(c)] = 0;
		const int deg = g.nbrN[size_t(c)];
		for (int j = 0; j < deg; j++) {
			const int m = g.nbr[size_t(6 * c + j)];
			if (E[size_t(m)] < shore) { st[size_t(c)] = 1; par[size_t(c)] = -1; size = heapPush(hk, hc, size, E[size_t(c)], c); break; }
		}
	}
	while (size > 0) {
		const float lvl = hk[0];
		const int c = hc[0];
		size = heapPop(hk, hc, size);
		order[size_t(cnt++)] = c;
		const int deg = g.nbrN[size_t(c)];
		for (int j = 0; j < deg; j++) {
			const int m = g.nbr[size_t(6 * c + j)];
			if (st[size_t(m)] != 0) continue;
			st[size_t(m)] = 1;
			par[size_t(m)] = c;
			size = heapPush(hk, hc, size, E[size_t(m)] > lvl ? E[size_t(m)] : lvl, m);
		}
	}
	// catchments: upstream first
	for (int i = 0; i < cnt; i++) acc[size_t(order[size_t(i)])] = 1;
	for (int i = cnt - 1; i >= 0; i--) {
		const int c = order[size_t(i)], pp = par[size_t(c)];
		if (pp >= 0) acc[size_t(pp)] += acc[size_t(c)];
	}

	// Implicit stream power, downstream first: a cell relaxes toward its (already final) downstream
	// neighbour by F / (1 + F), where F, the headward reach in cells, grows with the square root of
	// the catchment. Hollows (cells below their way out) are left alone.
	const double kF = P.erodeK / step, aMin = P.erodeMin * n, aRef = P.erodeRef * n, side = P.erodeSide;
	const double talus = P.talus * step * kPlanetR, floorE = shore + 0.02;
	for (int i = 0; i < cnt; i++) {
		const int c = order[size_t(i)], pp = par[size_t(c)];
		const double base = pp >= 0 ? double(E[size_t(pp)]) : shore;
		const double e0 = E[size_t(c)];
		// reach: the cell's own river, or a share of the river it drains into (valley sides and flood plains)
		double F = 0;
		const double a = acc[size_t(c)];
		if (a > aMin) {
			F = kF * std::sqrt((a - aMin) / aRef);
			if (F > P.erodeMax) F = P.erodeMax;
		}
		if (pp >= 0 && side * val[size_t(pp)] > F) F = side * val[size_t(pp)];
		val[size_t(c)] = float(F);
		if (e0 <= base) continue;
		double e = F > 0 ? (e0 + F * base) / (1 + F) : e0;
		if (e > base + talus) e = base + talus;
		if (e < floorE) e = e0 < floorE ? e0 : floorE;
		E[size_t(c)] = float(e);
	}

	// plains: smooth the lowlands (sea neighbours count as the shore, so coasts shelve gently)
	const double top = P.smoothTop;
	for (int i = 0; i < cnt; i++) {
		const int c = order[size_t(i)];
		const double h = E[size_t(c)] - shore;
		if (h >= top) { val[size_t(c)] = E[size_t(c)]; continue; }
		double sum = 0;
		int k = 0;
		const int deg = g.nbrN[size_t(c)];
		for (int j = 0; j < deg; j++) {
			const int m = g.nbr[size_t(6 * c + j)];
			sum += E[size_t(m)] < shore ? shore : double(E[size_t(m)]);
			k++;
		}
		val[size_t(c)] = k > 0 ? float(E[size_t(c)] + (sum / k - E[size_t(c)]) * P.smoothK * (1 - h / top)) : E[size_t(c)];
	}
	for (int i = 0; i < cnt; i++) {
		const int c = order[size_t(i)];
		E[size_t(c)] = val[size_t(c)];
	}
}

// Step 6: exact shoreline, then monotone remaps that guarantee the documented statistics. The
// generator comes close by itself; the remaps only finish the job, and they stretch nothing but
// the mountains (land above the midQ quantile), so plains and hills keep their eroded slopes.
void normalise(const TerrainTune& P, const World& w, Rng& rng, Gen& G, std::vector<float>& E) {
	const int n = w.n;
	std::vector<float>& sorted = G.sorted;
	const int nLand = landCountOf(P, n);
	const double shore = shoreOf(E, sorted, n, nLand);           // the lowest land value -> 0
	const int ks = n - nLand;
	auto qv = [&](double qq) {
		return double(sorted[size_t(ks + std::min(nLand - 1, int(std::floor(qq * nLand))))]) - shore;
	};
	// land quantiles (values relative to the shore)
	const double a = qv(P.lowQ), m = qv(P.midQ), b = qv(P.highQ), top = double(sorted[size_t(n - 1)]) - shore;
	const double peak = rng.range(P.peakMin, P.peakMax);
	const double deep = rng.range(P.deepMin, P.deepMax);
	const double drop = a > P.lowE ? a - P.lowE : 0;               // lowlands: squeeze [0, a] and lower the rest
	const double m2 = m - drop;
	double b2 = b - drop;
	if (b2 < P.highE) b2 = P.highE;
	if (b2 > peak - 1.2) b2 = peak - 1.2;
	if (b2 < m2) b2 = m2;
	const double deepest = shore - double(sorted[0]);
	const double knee = P.deepKnee;                                // depths beyond this are rescaled to reach `deep`
	for (int c = 0; c < n; c++) {
		double e = E[size_t(c)] - shore;
		if (e >= 0) {
			if (e <= a) { if (drop > 0) e = e * P.lowE / a; }
			else if (e <= m) e -= drop;
			else if (e <= b) e = b > m ? m2 + (e - m) * (b2 - m2) / (b - m) : m2;
			else e = top > b ? b2 + (e - b) * (peak - b2) / (top - b) : b2;
		} else if (-e > knee && deepest > knee) {
			const double t = (-e - knee) / (deepest - knee);          // 0 at the knee ... 1 at the deepest point
			e = -knee - (deep - knee) * t * (2 - t);                  // concave: the deep basins stay deep
		}
		E[size_t(c)] = float(e);
	}
}

// ------------------------------------------------------------------ generation: entry point

// Steps 1-6 for one plate layout. Leaves the finished heights in G.cont, with G.volc and G.acc.
void build(const TerrainTune& P, const World& w, Plates& PL, Gen& G, const Rng& root, double fmax) {
	const int n = w.n;
	contField(P, w.geo->pos.data(), n, PL, G, true, fmax);
	rankMap(P, G, n);
	std::vector<float>& E = G.cont;                                // reuse: the raw field is no longer needed
	Rng rng = root.fork("terrain-relief");
	relief(P, w, rng, G, E, fmax);
	hotspots(P, w, rng, G, E);
	const int nLand = landCountOf(P, n);
	double shore = shoreOf(E, G.sorted, n, nLand);
	const int net = tidy(P, w, G, E, shore, false);
	if (net != 0) shore = shoreOf(E, G.sorted, n, nLand - net);   // the basins about to be filled leave room for less land elsewhere
	tidy(P, w, G, E, shore, true);
	shelve(P, w, G, E, shore, PL.o);
	erode(P, w, G, E, shore);
	normalise(P, w, rng, G, E);
}

// Did the finished world keep its continents apart? 3 = yes: a second landmass of at least
// rateSecond of the planet, a third of at least rateThird, and none holding more than rateTop of the land.
double rate(const TerrainTune& P, const World& w, Gen& G) {
	const int n = w.n;
	const std::vector<int> sizes = components(*w.geo, n, G.cont, 0, G.lab, G.queue);
	const double s1 = sizes.size() > 1 ? sizes[1] / (P.rateSecond * n) : 0;
	const double s2 = sizes.size() > 2 ? sizes[2] / (P.rateThird * n) : 0;
	const double top = !sizes.empty() ? double(sizes[0]) / landCountOf(P, n) : 1;
	const double t = top <= P.rateTop ? 1 : (1 - top) / (1 - P.rateTop);
	return (s1 > 1 ? 1 : s1) + (s2 > 1 ? 1 : s2) + (t > 0 ? t : 0);
}

struct Candidate {
	Plates PL;
	double sc = 0;
	int t = 0;
};

// ------------------------------------------------------------------ (v4 mythos) world archetypes

// a multiplier from the archetype row: 1 unless it is a finite positive number
double kOf(double k) { return std::isfinite(k) && k > 0 ? k : 1.0; }

// The generation tuning of this world's archetype (SPEC-MYTHOS 3.1): an adjusted copy of TerrainTune.
// Every adjustment is skipped at its neutral value, so Balanced returns the tuning unchanged.
TerrainTune archetypeTune(const TerrainTune& base, const ArchetypeParams& ap) {
	TerrainTune P = base;
	const double rk = kOf(ap.ridgeK), sk = kOf(ap.shelfK), ik = kOf(ap.islandK);
	if (rk != 1) {
		P.beltW *= rk;
		P.wideW *= rk;
		P.oldW *= rk;
		P.arcW *= rk;
		P.arcH *= rk;
		P.oldCut /= rk;
		P.convMin /= rk;
		const int more = rk > 1 ? int(std::floor((rk - 1) / 0.3 + 0.5)) : 0;
		P.chainsMin += more;
		P.chainsMax += more;
		P.fossilMin += 2 * more;
		P.fossilMax += 2 * more;
		// and the normalisation lets ridgeK more of the land rise into uplands and alpine heights
		P.midQ = std::max(0.5, 1 - (1 - P.midQ) * rk);
		P.highQ = std::max(P.midQ, 1 - (1 - P.highQ) * rk);
	}
	if (sk != 1) {
		P.warpCont *= sk;
		P.coastLo *= sk;
		P.coastHi *= sk;
		for (double& s : P.shelfLim) s /= sk;
	}
	if (ik != 1) {
		P.chainsMin = std::max(0, int(std::floor(P.chainsMin * ik + 0.5)));
		P.chainsMax = std::max(P.chainsMin, int(std::floor(P.chainsMax * ik + 0.5)));
		P.speckKeep = P.speckKeep * ik > 1 ? 1.0 : P.speckKeep * ik;
		P.arcH *= ik;
	}
	return P;
}

// Strange worlds: 3-6 marvels on land, never within 7 hops of each other (see the top comment).
void placeMarvels(World& w, const ArchetypeParams& ap, const Rng& root) {
	// drop the marvels of an earlier generate of this world (erasing absent keys changes nothing)
	for (auto it = w.vars.num.lower_bound("terrain_marvel"); it != w.vars.num.end() && it->first.rfind("terrain_marvel", 0) == 0;)
		it = w.vars.num.erase(it);
	const int lo = std::clamp(ap.marvels, 0, 16), hi = std::clamp(std::max(ap.marvels, ap.marvelsMax), 0, 16);
	if (hi <= 0) return;
	const Geo& g = *w.geo;
	const int n = w.n;
	Rng r = root.fork("terrain-marvels");
	const int want = lo >= hi ? hi : r.integer(lo, hi);
	std::vector<int32_t> cand;
	for (int c = 0; c < n; c++) {
		const double e = w.elev[size_t(c)];
		if (!(e >= 0.5 && e <= 4.5)) continue;                        // well above the shore (the sea may settle a little high)
		int landNbr = 0;
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) landNbr += w.elev[size_t(g.nbr[size_t(6 * c + k)])] >= 0 ? 1 : 0;
		if (landNbr >= 4) cand.push_back(c);
	}
	if (cand.empty()) return;
	std::vector<uint8_t> blocked(size_t(n), 0);
	std::vector<int32_t> ringOut;
	int placed = 0;
	for (int tries = 0; tries < 64 * want && placed < want; tries++) {
		const int c = cand[size_t(r.pickIndex(int(cand.size())))];
		if (blocked[size_t(c)]) continue;
		g.ring(c, 7, ringOut);
		for (const int32_t m : ringOut) blocked[size_t(m)] = 1;
		const double lat = std::fabs(double(g.lat[size_t(c)]));
		const int kind = lat > 0.87 ? 1 : w.elev[size_t(c)] > 1.6 ? 0 : (hashf(uint32_t(c), w.seed + 31u) < 0.5 ? 2 : 3);
		w.vars.num["terrain_marvel_" + std::to_string(placed)] = c;
		w.vars.num["terrain_marvel_kind_" + std::to_string(placed)] = kind;
		if (w.fert[size_t(c)] < 0.8f) w.fert[size_t(c)] = 0.8f;
		placed++;
	}
	w.vars.num["terrain_marvels"] = placed;
}

} // namespace

namespace terrain {

// ------------------------------------------------------------------ flags

void touched(World& w) {
	w.dirty.elev = true;
	w.dirty.water = true;
	w.dirty.color = true;
	w.need.derive = true;
	w.need.climate = true;
	w.need.hydro = true;
}

// ------------------------------------------------------------------ sculpting operations

int sculpt(World& w, int c, double ang, double amount) {
	if (!validCell(w, c) || !std::isfinite(amount) || amount == 0) return 0;
	if (!(ang > 0)) ang = 0;
	if (ang > kPi) ang = kPi;
	const Geo& g = *w.geo;
	const float* pos = g.pos.data();
	TerrainScratch& s = scratchOf(w);
	const int cnt = g.within(c, ang, s.list);
	const double inv = ang > 0 ? 1 / (ang * ang) : 0;
	int changed = 0;
	for (int i = 0; i < cnt; i++) {
		const int m = s.list[size_t(i)];
		const double t2 = m == c ? 0 : angle2(pos, c, m) * inv;
		if (t2 >= 1) continue;
		const double f = (1 - t2) * (1 - t2);
		const float old = w.elev[size_t(m)];
		w.elev[size_t(m)] = clampElev(old + amount * f, old);
		changed++;
	}
	if (changed) touched(w);
	return changed;
}

void crater(World& w, int c, double ang, double depth) {
	if (!validCell(w, c) || !std::isfinite(depth) || !(depth > 0)) return;
	const TerrainTune& P = w.tune<TerrainTune>();
	const Geo& g = *w.geo;
	const float* pos = g.pos.data();
	TerrainScratch& s = scratchOf(w);
	if (!(ang > 0)) ang = 0;
	if (ang > 2) ang = 2;
	const double R = std::max(ang, P.minBrush * g.step);
	const int cnt = g.within(c, R * 1.5, s.list);
	const double inv = 1 / (R * R);
	// mean ground level inside the bowl: the blast levels the ground before it digs
	double ref = 0;
	int k = 0;
	for (int i = 0; i < cnt; i++) {
		const int m = s.list[size_t(i)];
		if (m == c || angle2(pos, c, m) * inv < 1) { ref += w.elev[size_t(m)]; k++; }
	}
	ref = k ? ref / k : double(w.elev[size_t(c)]);
	const double rimH = P.craterRim * depth;
	int changed = 0;
	for (int i = 0; i < cnt; i++) {
		const int m = s.list[size_t(i)];
		const double t2 = m == c ? 0 : angle2(pos, c, m) * inv;
		const double t = std::sqrt(t2);
		const float old = w.elev[size_t(m)];
		double v = old;
		if (t2 < 1) {
			const double bowl = 1 - t2;
			v += (ref - old) * P.craterFlat * bowl - depth * bowl;
			// never let the levelling fill a hollow past the dig: the bowl is always a bowl
			const double cap = double(old) - P.craterMinDrop * depth * bowl;
			if (P.craterMinDrop > 0 && v > cap) v = cap;
		}
		if (t > 0.8 && t < 1.5) v += rimH * sstep(0.8, 1.12, t) * (1 - sstep(1.12, 1.5, t));
		if (v != double(old)) {
			const float nv = clampElev(v, old);
			if (nv != old) { w.elev[size_t(m)] = nv; changed++; }
		}
	}
	if (changed) touched(w);
}

void cone(World& w, int c, double ang, double height) {
	if (!validCell(w, c) || !std::isfinite(height) || !(height > 0)) return;
	const TerrainTune& P = w.tune<TerrainTune>();
	const Geo& g = *w.geo;
	const float* pos = g.pos.data();
	TerrainScratch& s = scratchOf(w);
	if (!(ang > 0)) ang = 0;
	if (ang > 2) ang = 2;
	const double R = std::max(ang, P.minBrush * g.step);
	// Crest ring: a share of the radius, but just beyond the vent's farthest neighbour, so that on a
	// small cone the vent cell is the pit and every one of its neighbours stands on the crest.
	double farD2 = 0;
	const int deg = g.nbrN[size_t(c)];
	for (int j = 0; j < deg; j++) {
		const int q = g.nbr[size_t(6 * c + j)];
		const double d2 = angle2(pos, c, q);
		if (d2 > farD2) farD2 = d2;
	}
	double rc = std::max(P.conePit * R, 1.02 * std::sqrt(farD2));
	const bool pit = rc < 0.8 * R;                                 // room for a pit and a flank? If not: a plain peak
	if (!pit) rc = 0;
	const int cnt = g.within(c, R, s.list);
	// The cone is built on the vent's own level: the old ground is buried in proportion to how close
	// it is to the summit (never dug away), so the crest stands `height` above the vent.
	const double ref = w.elev[size_t(c)], level = P.coneLevel;
	int changed = 0;
	for (int i = 0; i < cnt; i++) {
		const int m = s.list[size_t(i)];
		const double d = m == c ? 0 : std::sqrt(angle2(pos, c, m));
		if (d >= R) continue;
		double h, wgt;
		if (d < rc) {
			const double q = d / rc;
			h = 1 - P.conePitDepth * (1 - q * q);
			wgt = 1;
		} else {
			const double q = 1 - (d - rc) / (R - rc);                  // 1 at the crest, 0 at the foot
			h = q * std::sqrt(q);
			wgt = q * q;
		}
		if (!(h > 0)) continue;
		const float old = w.elev[size_t(m)];
		const double add = height * h + (ref - old) * wgt * level;
		if (!(add > 0)) continue;
		const float nv = clampElev(old + add, old);
		if (nv != old) { w.elev[size_t(m)] = nv; changed++; }
	}
	if (changed) touched(w);
}

void roughen(World& w, int c, double ang, double amount, Rng& rng) {
	if (!validCell(w, c) || !std::isfinite(amount) || amount == 0) return;
	if (!(ang > 0)) ang = 0;
	if (ang > kPi) ang = kPi;
	const TerrainTune& P = w.tune<TerrainTune>();
	const Geo& g = *w.geo;
	const float* pos = g.pos.data();
	std::vector<float>& elev = w.elev;
	TerrainScratch& s = scratchOf(w);
	std::vector<int32_t>& list = s.list;
	std::vector<int32_t>& ring = s.tmpI;
	std::vector<int32_t>& stamp = s.stamp;
	const int cnt = g.within(c, ang, list);
	const int32_t id = nextStamp(s);
	for (int i = 0; i < cnt; i++) {
		const int m = list[size_t(i)];
		stamp[size_t(m)] = id;
		s.val[size_t(m)] = float(rng.f() * 2 - 1);
	}

	// the cells one hop outside the patch (their neighbours move, so they can become pits too)
	const int32_t idRing = nextStamp(s);
	int nRing = 0;
	for (int i = 0; i < cnt; i++) {
		const int m = list[size_t(i)];
		const int deg = g.nbrN[size_t(m)];
		for (int j = 0; j < deg; j++) {
			const int q = g.nbr[size_t(6 * m + j)];
			if (stamp[size_t(q)] != id && stamp[size_t(q)] != idRing) { stamp[size_t(q)] = idRing; ring[size_t(nRing++)] = q; }
		}
	}
	// pits that are there already (a crater floor, a volcano's summit) are not ours to fill
	std::vector<int32_t>& wasPit = s.queue;
	int nPit = 0;
	for (int i = 0; i < cnt + nRing; i++) {
		const int m = i < cnt ? list[size_t(i)] : ring[size_t(i - cnt)];
		if (isPit(g, elev, m)) wasPit[size_t(nPit++)] = m;
	}

	// smooth the random field inside the patch so that neighbours move together
	std::vector<float>* a = &s.val;
	std::vector<float>* bb = &s.val2;
	for (int pass = 0; pass < P.roughPasses; pass++) {
		for (int i = 0; i < cnt; i++) {
			const int m = list[size_t(i)];
			double sum = 2.0 * (*a)[size_t(m)], wsum = 2;
			const int deg = g.nbrN[size_t(m)];
			for (int j = 0; j < deg; j++) {
				const int q = g.nbr[size_t(6 * m + j)];
				if (stamp[size_t(q)] == id) { sum += (*a)[size_t(q)]; wsum++; }
			}
			(*bb)[size_t(m)] = float(sum / wsum);
		}
		std::swap(a, bb);
	}
	double peak = 0;
	const double inv = ang > 0 ? 1 / (ang * ang) : 0;
	for (int i = 0; i < cnt; i++) {
		const int m = list[size_t(i)];
		const double t2 = m == c ? 0 : angle2(pos, c, m) * inv;
		const double f = t2 >= 1 ? 0 : (1 - t2) * (1 - t2);
		const double v = (*a)[size_t(m)] * f;
		(*bb)[size_t(m)] = float(v);
		if (v > peak) peak = v; else if (-v > peak) peak = -v;
	}
	// apply (the old heights are kept in `a`, which is free now, to count what really moved)
	const double k = peak > 0 ? amount / peak : 0;
	for (int i = 0; i < cnt; i++) {
		const int m = list[size_t(i)];
		const float old = elev[size_t(m)];
		const double v = double((*bb)[size_t(m)]) * k;
		(*a)[size_t(m)] = old;
		if (v != 0) elev[size_t(m)] = clampElev(old + v, old);
	}
	for (int i = 0; i < nRing; i++) (*a)[size_t(ring[size_t(i)])] = elev[size_t(ring[size_t(i)])];
	// Fill the one-cell pits the shaking made, up to their lowest neighbour. Raising a pit to that
	// level cannot make a neighbour a pit (the neighbour is then not strictly below it), so one sweep
	// is enough. A filled cell still ends within `amount` of where it was.
	const int32_t idPit = nextStamp(s);
	for (int i = 0; i < nPit; i++) stamp[size_t(wasPit[size_t(i)])] = idPit;
	int changed = 0;
	for (int i = 0; i < cnt + nRing; i++) {
		const int m = i < cnt ? list[size_t(i)] : ring[size_t(i - cnt)];
		if (stamp[size_t(m)] != idPit && isPit(g, elev, m)) {
			const double lvl = lowestNbr(g, elev, m);
			if (std::isfinite(lvl)) elev[size_t(m)] = clampElev(lvl, elev[size_t(m)]);
		}
		if (elev[size_t(m)] != (*a)[size_t(m)]) changed++;
	}
	if (changed) touched(w);
}

double addElev(World& w, int c, double amount) {
	if (!validCell(w, c)) return 0.0;
	const float old = w.elev[size_t(c)];
	if (!std::isfinite(amount)) return old;
	w.elev[size_t(c)] = clampElev(old + amount, old);
	return w.elev[size_t(c)];
}

// ------------------------------------------------------------------ derived fields

void derive(World& w) {
	if (!w.geo || w.n <= 0 || w.elev.size() != size_t(w.n)) {
		w.need.derive = false;
		w.need.life = true;
		return;
	}
	const Geo& g = *w.geo;
	const int n = w.n;
	const double sea = w.seaLevel;
	TerrainScratch& s = scratchOf(w);
	std::vector<int32_t>& q = s.queue;
	std::vector<int32_t>& sizes = s.tmpI;
	std::vector<int32_t>& lid = w.cache.landId;
	if (lid.size() != size_t(n)) lid.assign(size_t(n), -1);
	if (w.slope.size() != size_t(n)) w.slope.assign(size_t(n), 0.0f);
	if (w.coast.size() != size_t(n)) w.coast.assign(size_t(n), uint8_t(255));

	// slope, and seed the coast search with every sea cell
	int tail = 0;
	for (int c = 0; c < n; c++) {
		const float e = w.elev[size_t(c)];
		float sl = 0;
		const int deg = g.nbrN[size_t(c)];
		for (int j = 0; j < deg; j++) {
			const int m = g.nbr[size_t(6 * c + j)];
			float d = w.elev[size_t(m)] - e;
			if (d < 0) d = -d;
			if (d > sl) sl = d;
		}
		w.slope[size_t(c)] = sl;
		if (e < sea) { w.coast[size_t(c)] = 0; lid[size_t(c)] = -1; q[size_t(tail++)] = c; }
		else { w.coast[size_t(c)] = 255; lid[size_t(c)] = -2; }
	}

	// coast: breadth-first hops from the sea, capped at 255
	for (int head = 0; head < tail; head++) {
		const int c = q[size_t(head)];
		const int d = w.coast[size_t(c)] + 1;
		if (d >= 255) continue;
		const int deg = g.nbrN[size_t(c)];
		for (int j = 0; j < deg; j++) {
			const int m = g.nbr[size_t(6 * c + j)];
			if (w.coast[size_t(m)] > d) { w.coast[size_t(m)] = uint8_t(d); q[size_t(tail++)] = m; }
		}
	}

	// landmasses: flood fill, then renumber by descending size (ties: the one found first = lowest cell id)
	int k = 0;
	for (int c0 = 0; c0 < n; c0++) {
		if (lid[size_t(c0)] != -2) continue;
		const int tag = -3 - k;                                    // temporary labels -3, -4, ...
		int head = 0, end = 0;
		q[size_t(end++)] = c0;
		lid[size_t(c0)] = tag;
		while (head < end) {
			const int c = q[size_t(head++)];
			const int deg = g.nbrN[size_t(c)];
			for (int j = 0; j < deg; j++) {
				const int m = g.nbr[size_t(6 * c + j)];
				if (lid[size_t(m)] == -2) { lid[size_t(m)] = tag; q[size_t(end++)] = m; }
			}
		}
		sizes[size_t(k++)] = end;
	}
	std::vector<int32_t> orderIdx(static_cast<size_t>(k), 0);
	for (int i = 0; i < k; i++) orderIdx[size_t(i)] = i;
	std::sort(orderIdx.begin(), orderIdx.end(), [&](int32_t a, int32_t b) {
		if (sizes[size_t(a)] != sizes[size_t(b)]) return sizes[size_t(a)] > sizes[size_t(b)];
		return a < b;
	});
	std::vector<int32_t> finalId(static_cast<size_t>(k), 0);
	s.landSize.assign(size_t(k), 0);
	for (int i = 0; i < k; i++) {
		finalId[size_t(orderIdx[size_t(i)])] = i;
		s.landSize[size_t(i)] = sizes[size_t(orderIdx[size_t(i)])];
	}
	for (int c = 0; c < n; c++) {
		const int t = lid[size_t(c)];
		if (t <= -3) lid[size_t(c)] = finalId[size_t(-3 - t)];
	}
	w.cache.landCount = k;

	w.need.derive = false;
	w.need.life = true;
}

// ------------------------------------------------------------------ generate

void generate(World& w) {
	if (!w.geo || w.n <= 0 || w.elev.size() != size_t(w.n)) return;
	// (v4 mythos) the archetype's tuning; Balanced = w.tune<TerrainTune>() exactly
	const ArchetypeParams AP = mythos::archetypeParams(w);
	const TerrainTune P = archetypeTune(w.tune<TerrainTune>(), AP);
	const double fertAdd = std::isfinite(AP.fert) ? AP.fert : 0.0;
	const Geo& geo = *w.geo;
	const int n = w.n;
	const float* pos = geo.pos.data();
	Gen G(n);
	const double fmax = 1 / (P.cellsPerWave * geo.step);
	const Rng root(w.seed);

	// Layout: score candidates on the level-4 cells (the first cells of every finer level, and
	// band-limited for that level, so a seed has the same layout at every level), best first.
	TerrainScratch& s = scratchOf(w);
	const Geo* g4 = &geo;
	if (geo.L > 4) {
		if (!s.geo4 || s.geo4->L != 4) s.geo4 = Geo::build(4);
		g4 = s.geo4.get();
	}
	const double fmax4 = 1 / (P.cellsPerWave * g4->step);
	Gen S4(g4->n);
	// Candidates are scored in order until one gets full marks; the pool is kept best first.
	std::vector<Candidate> pool;
	const int tries = std::max(1, P.candidates);
	int tNext = 0;
	auto scoreMore = [&]() {
		while (tNext < tries) {
			Candidate cand;
			cand.PL = makePlates(P, root.fork("terrain-layout-" + std::to_string(tNext)));
			cand.sc = scoreLayout(P, pos, cand.PL, S4, *g4, fmax4);
			cand.t = tNext++;
			const double sc = cand.sc;
			pool.push_back(std::move(cand));
			if (sc >= 5.5) break;
		}
		std::sort(pool.begin(), pool.end(), [](const Candidate& a, const Candidate& b) {
			if (a.sc != b.sc) return a.sc > b.sc;
			return a.t < b.t;
		});
	};

	// Build the best layout. Relief can still bridge a strait the scoring thought safe: then build the
	// next best layout, scoring further candidates first, and keep whichever finished world rates best.
	Plates best;
	bool haveBest = false, inG = false;                            // inG: the best build is the one G holds
	double bestRate = -1;
	std::vector<float> keepE, keepVolc, keepAcc;
	const int builds = 1 + std::max(0, P.rebuilds);
	for (int b = 0; b < builds; b++) {
		if (pool.empty() || pool[0].sc < 5.5) scoreMore();         // look for full marks before settling for less
		if (pool.empty()) break;                                   // every candidate has been built
		if (inG) {                                                 // the next build overwrites G: copy the best first
			keepE = G.cont;
			keepVolc = G.volc;
			keepAcc = G.acc;
			inG = false;
		}
		Candidate cand = std::move(pool.front());
		pool.erase(pool.begin());
		build(P, w, cand.PL, G, root, fmax);
		const double r = rate(P, w, G);
		if (r > bestRate) {
			bestRate = r;
			best = std::move(cand.PL);
			haveBest = true;
			inG = true;
			keepE.clear(); keepVolc.clear(); keepAcc.clear();
			if (r >= 3) break;
		}
	}
	if (!haveBest) return;                                         // cannot happen (tries >= 1)
	if (!inG) {
		G.cont = std::move(keepE);
		G.volc = std::move(keepVolc);
		G.acc = std::move(keepAcc);
	}

	// write elevation and an initial fertility: regional variety, rich lowlands, flood plains and
	// volcanic soils, thin mountain soils
	const double* o = best.o;
	const double accRef = 0.004 * n;
	for (int c = 0; c < n; c++) {
		const size_t cc = size_t(c);
		double e = G.cont[cc];
		if (!std::isfinite(e)) e = 0;
		w.elev[cc] = float(clamp(e, state::kElevMin, state::kElevMax));
		const double x = pos[3 * c], y = pos[3 * c + 1], z = pos[3 * c + 2];
		double f = 0.48 + 0.62 * fbmB(x, y, z, 2.7, 3, 2.1, 0.5, fmax, o[9], o[4], o[14]);
		if (e >= 0) {
			f += 0.12 * (1 - sat(e / 1.2)) - 0.3 * sat((e - 2.2) / 3);
			if (e < 1.5) f += 0.2 * sat(std::sqrt(G.acc[cc] / accRef)) * (1 - e / 1.5);
		}
		f += 0.18 * (G.volc[cc] > 1 ? 1.0 : double(G.volc[cc]));
		if (fertAdd != 0) f += fertAdd;                            // (v4) Generous / Dangerous soils
		if (!std::isfinite(f)) f = 0.5;
		w.fert[cc] = float(f < 0.15 ? 0.15 : f > 1 ? 1 : f);
	}
	placeMarvels(w, AP, root);                                     // (v4) Strange worlds; nothing for the others

	w.dirty = Dirty{};                                             // every dirty flag true
	w.need = Need{};                                               // every need flag true
}

} // namespace terrain
} // namespace dm
