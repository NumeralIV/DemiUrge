// DEMIURGE — SimTests/test_core_geo.cpp : Core/Geo.h (port of tests/geo.test.js and geo.adv.test.js).
#include <algorithm>
#include <cmath>
#include <set>

#include "Core/Geo.h"
#include "Core/Rng.h"
#include "TestFramework.h"
#include "TestWorld.h"

using namespace dm;
using namespace dmt;

namespace {

uint32_t fnvU(const std::vector<uint32_t>& a) { uint32_t h = 2166136261u; for (uint32_t v : a) { h ^= v; h *= 16777619u; } return h; }
uint32_t fnvI(const std::vector<int32_t>& a) { uint32_t h = 2166136261u; for (int32_t v : a) { h ^= uint32_t(v); h *= 16777619u; } return h; }
double dotQ(const Geo& g, double x, double y, double z, int c) {
	return x * g.pos[size_t(3 * c)] + y * g.pos[size_t(3 * c + 1)] + z * g.pos[size_t(3 * c + 2)];
}
// brute-force nearest cell (largest dot product, first wins a tie)
int brute(const Geo& g, double x, double y, double z, double* bestDot = nullptr) {
	double best = -INFINITY;
	int bi = -1;
	for (int i = 0; i < g.n; i++) { const double d = dotQ(g, x, y, z, i); if (d > best) { best = d; bi = i; } }
	if (bestDot) *bestDot = best;
	return bi;
}
Vec3 randDir(Rng& r) {           // not normalized: length 0.2 .. 1
	double x, y, z, d;
	do { x = r.f() * 2 - 1; y = r.f() * 2 - 1; z = r.f() * 2 - 1; d = x * x + y * y + z * z; } while (d > 1 || d < 0.04);
	return { x, y, z };
}
std::vector<int32_t> bruteWithin(const Geo& g, int c, double ang) {
	std::vector<int32_t> res;
	for (int i = 0; i < g.n; i++) if (g.angle(c, i) <= ang) res.push_back(i);
	return res;
}
std::vector<int32_t> sortedCopy(std::vector<int32_t> v) { std::sort(v.begin(), v.end()); return v; }
std::vector<int> hopDist(const Geo& g, int c) {
	std::vector<int> dist(size_t(g.n), -1);
	std::vector<int> frontier = { c };
	dist[size_t(c)] = 0;
	while (!frontier.empty()) {
		std::vector<int> next;
		for (int a : frontier) for (int j = 0; j < g.nbrN[size_t(a)]; j++) {
			const int m = g.nbr[size_t(6 * a + j)];
			if (dist[size_t(m)] < 0) { dist[size_t(m)] = dist[size_t(a)] + 1; next.push_back(m); }
		}
		frontier.swap(next);
	}
	return dist;
}

} // namespace

DM_TEST("geo: n, nTri, prefix and array sizes for L = 0..7; level clamping") {
	for (int L = 0; L <= 7; L++) {
		const auto g = sharedGeo(L);
		const int n = Geo::prefix(L);
		eq(g->L, L); eq(g->n, n); eq(g->nTri, 20 * (1 << (2 * L)));
		ok(g->pos.size() == size_t(3 * n) && g->lat.size() == size_t(n) && g->lon.size() == size_t(n), "pos / lat / lon sizes");
		ok(g->nbr.size() == size_t(6 * n) && g->nbrN.size() == size_t(n) && g->tris.size() == size_t(3 * g->nTri), "nbr / tris sizes");
		ok(g->step > 0 && g->step < 1.2, "step");
	}
	eq(Geo::prefix(6), 40962); eq(Geo::prefix(7), 163842);
	eq(Geo::build(-3)->L, 0, "clamped below"); eq(Geo::build(9)->L, 7, "clamped above");
	near(sharedGeo(6)->step, 0.0189, 0.0005, "L = 6 step about 0.0189");
}

DM_TEST("geo: cell numbering and topology are pinned (identical to the JS build)") {
	const auto g0 = sharedGeo(0);
	eq(g0->tris, std::vector<uint32_t>{ 0, 2, 1, 0, 3, 2, 0, 4, 3, 0, 5, 4, 0, 1, 5, 1, 2, 6, 2, 3, 7, 3, 4, 8, 4, 5, 9, 5, 1, 10,
		6, 2, 7, 7, 3, 8, 8, 4, 9, 9, 5, 10, 10, 1, 6, 6, 7, 11, 7, 8, 11, 8, 9, 11, 9, 10, 11, 10, 6, 11 }, "level 0 triangles");
	eq(g0->nbr, std::vector<int32_t>{ 2, 1, 5, 4, 3, -1, 0, 2, 6, 10, 5, -1, 1, 0, 3, 7, 6, -1, 2, 0, 4, 8, 7, -1, 3, 0, 5, 9, 8, -1,
		4, 0, 1, 10, 9, -1, 1, 2, 7, 11, 10, -1, 2, 3, 8, 11, 6, -1, 3, 4, 9, 11, 7, -1, 4, 5, 10, 11, 8, -1,
		5, 1, 6, 11, 9, -1, 6, 7, 8, 9, 10, -1 }, "level 0 neighbours");
	eq(fnvU(sharedGeo(2)->tris), 2367549661u, "level 2 triangle hash");
	eq(fnvI(sharedGeo(2)->nbr), 110849663u, "level 2 neighbour hash");
	eq(fnvU(sharedGeo(6)->tris), 3449796213u, "level 6 triangle hash");
	eq(fnvI(sharedGeo(6)->nbr), 3473565537u, "level 6 neighbour hash");
	const auto g1 = sharedGeo(1);
	near(g1->pos[36], 0.16245985, 1e-7); near(g1->pos[37], 0.85065081, 1e-7); near(g1->pos[38], 0.5, 1e-7);
	const auto a = Geo::build(4), b = Geo::build(4);
	ok(a->pos == b->pos && a->nbr == b->nbr && a->tris == b->tris && a->lat == b->lat && a->step == b->step, "build is deterministic");
}

DM_TEST("geo: orientation, unit positions, lat / lon") {
	const double ringLat = std::atan(0.5);
	for (int L : { 0, 3, 6, 7 }) {
		const auto g = sharedGeo(L);
		ok(g->pos[0] == 0 && g->pos[1] == 1 && g->pos[2] == 0, "vertex 0 is the north pole");
		ok(g->pos[33] == 0 && g->pos[34] == -1 && g->pos[35] == 0, "vertex 11 is the south pole");
		for (int k = 0; k < 5; k++) {
			near(g->lat[size_t(1 + k)], ringLat, 1e-6, "upper ring latitude");
			near(g->lat[size_t(6 + k)], -ringLat, 1e-6, "lower ring latitude");
			const double up = std::fmod(std::fmod(g->lon[size_t(1 + k)] * 180 / kPi - 72 * k, 360) + 540, 360) - 180;
			const double lo = std::fmod(std::fmod(g->lon[size_t(6 + k)] * 180 / kPi - (36 + 72 * k), 360) + 540, 360) - 180;
			near(up, 0, 1e-4, "upper ring longitude"); near(lo, 0, 1e-4, "lower ring longitude");
		}
		double worst = 0, worstLat = 0;
		bool inRange = true;
		for (int i = 0; i < g->n; i++) {
			const Vec3 d = g->dir(i);
			worst = std::max(worst, std::fabs(v3::len(d) - 1));
			const double la = g->lat[size_t(i)], lo = g->lon[size_t(i)];
			inRange &= la >= -kPi / 2 && la <= kPi / 2 && lo >= -kPi && lo <= kPi;
			worstLat = std::max(worstLat, std::fabs(std::sin(la) - d.y));
		}
		ok(worst < 1e-7, "unit length (float32): " + std::to_string(worst));
		ok(inRange, "lat / lon in range");
		ok(worstLat < 1e-6, "lat = asin(y)");
	}
}

DM_TEST("geo: neighbours: 5 for the 12 pentagons with -1 padding, 6 otherwise, symmetric, CCW, consecutive ones adjacent") {
	for (int L : { 0, 1, 2, 4, 6 }) {
		const auto gp = sharedGeo(L);
		const Geo& g = *gp;
		bool good = true;
		for (int c = 0; c < g.n && good; c++) {
			const int k = g.nbrN[size_t(c)];
			good &= ok(k == (c < 12 ? 5 : 6), "degree");
			if (c < 12) good &= ok(g.nbr[size_t(6 * c + 5)] == -1, "-1 padding");
			const Vec3 up = g.dir(c);
			double turn = 0;
			for (int j = 0; j < k; j++) {
				const int m = g.nbr[size_t(6 * c + j)], m2 = g.nbr[size_t(6 * c + (j + 1) % k)];
				bool back = false;
				for (int q = 0; q < g.nbrN[size_t(m)]; q++) back |= g.nbr[size_t(6 * m + q)] == c;
				good &= ok(back, "symmetric");
				bool adj = false;
				for (int q = 0; q < g.nbrN[size_t(m)]; q++) adj |= g.nbr[size_t(6 * m + q)] == m2;
				good &= ok(adj, "consecutive neighbours are adjacent");
				// signed angle from m to m2 about up (counter-clockwise seen from outside is positive)
				const Vec3 a = g.dir(m) - up * v3::dot(g.dir(m), up), b = g.dir(m2) - up * v3::dot(g.dir(m2), up);
				turn += std::atan2(v3::dot(v3::cross(a, b), up), v3::dot(a, b));
			}
			good &= near(turn, kTau, 1e-3, "one full counter-clockwise turn");
		}
	}
}

DM_TEST("geo: triangles are CCW seen from outside, a closed 2-manifold, Euler characteristic 2") {
	for (int L : { 0, 2, 5 }) {
		const auto gp = sharedGeo(L);
		const Geo& g = *gp;
		std::set<std::pair<int, int>> directed;
		bool good = true;
		for (int t = 0; t < g.nTri && good; t++) {
			const int a = int(g.tris[size_t(3 * t)]), b = int(g.tris[size_t(3 * t + 1)]), c = int(g.tris[size_t(3 * t + 2)]);
			const Vec3 A = g.dir(a), B = g.dir(b), C = g.dir(c);
			good &= ok(v3::dot(v3::cross(B - A, C - A), A + B + C) > 0, "CCW from outside");
			for (auto e : { std::make_pair(a, b), std::make_pair(b, c), std::make_pair(c, a) }) good &= ok(directed.insert(e).second, "each directed edge once");
		}
		for (const auto& e : directed) if (!directed.count({ e.second, e.first })) { fail("an edge without its twin"); break; }
		const long long V = g.n, E = (long long)directed.size() / 2, F = g.nTri;
		eq(V - E + F, 2LL, "Euler characteristic");
	}
}

DM_TEST("geo: prefix property and midpoints; neighbour slots line up across levels") {
	const auto g5 = sharedGeo(5), g6 = sharedGeo(6);
	bool good = true;
	for (int c = 0; c < g5->n && good; c++) {
		good &= ok(g5->pos[size_t(3 * c)] == g6->pos[size_t(3 * c)] && g5->pos[size_t(3 * c + 1)] == g6->pos[size_t(3 * c + 1)] &&
		           g5->pos[size_t(3 * c + 2)] == g6->pos[size_t(3 * c + 2)], "prefix property (bitwise)");
		for (int j = 0; j < g5->nbrN[size_t(c)]; j++) {
			// slot j at level 6 is the midpoint of the edge toward slot j at level 5
			const int fine = g6->nbr[size_t(6 * c + j)], coarse = g5->nbr[size_t(6 * c + j)];
			const Vec3 mid = v3::norm(g5->dir(c) + g5->dir(coarse));
			good &= ok(v3::dist(mid, g6->dir(fine)) < 1e-6, "slot j lines up across levels");
		}
	}
}

DM_TEST("geo: step, angle, dir, basis") {
	const auto gp = sharedGeo(4);
	const Geo& g = *gp;
	double sum = 0;
	long long edges = 0;
	for (int c = 0; c < g.n; c++) for (int j = 0; j < g.nbrN[size_t(c)]; j++) { const int m = g.nbr[size_t(6 * c + j)]; if (m > c) { sum += g.angle(c, m); edges++; } }
	near(g.step, sum / double(edges), 1e-9, "step is the mean neighbour angle");
	eq(edges, (long long)(30 * (1 << 8)), "edge count 30 * 4^L");
	eq(g.angle(5, 5), 0.0, "exact 0 on the diagonal");
	near(g.angle(0, 11), kPi, 1e-7, "pi at antipodes");
	near(g.angle(3, 700), g.angle(700, 3), 0, "symmetric");
	bool good = true;
	for (int c = 0; c < g.n && good; c += 7) {
		Vec3 e, n, ve, vn;
		g.basis(c, e, n);
		v3::basis(v3::norm(g.dir(c)), ve, vn);
		good &= ok(v3::dist(e, ve) < 1e-6 && v3::dist(n, vn) < 1e-6, "basis agrees with v3::basis");
		good &= near(v3::dot(e, n), 0, 1e-9) && near(v3::len(e), 1, 1e-9) && near(v3::len(n), 1, 1e-9);
		good &= ok(v3::dist(v3::cross(e, n), v3::norm(g.dir(c))) < 1e-6, "right-handed");
	}
	Vec3 e, n;
	g.basis(0, e, n);
	ok(e == Vec3{ 1, 0, 0 } && n == Vec3{ 0, 0, -1 }, "north pole frame");
	g.basis(11, e, n);
	ok(e == Vec3{ 1, 0, 0 } && n == Vec3{ 0, 0, 1 }, "south pole frame");
}

DM_TEST("geo: cellAt is exact (brute force) on random directions, with and without hints, at every level") {
	Rng r(77);
	for (int L : { 0, 1, 2, 3, 4, 5, 6, 7 }) {
		const auto gp = sharedGeo(L);
		const Geo& g = *gp;
		const int N = L == 7 ? 600 : L == 6 ? 4000 : 2000;
		int bad = 0;
		for (int i = 0; i < N; i++) {
			const Vec3 d = randDir(r);
			double bd;
			brute(g, d.x, d.y, d.z, &bd);
			const int got = g.cellAt(d);
			if (dotQ(g, d.x, d.y, d.z, got) != bd) bad++;
			const int hint = r.integer(0, g.n - 1);
			if (dotQ(g, d.x, d.y, d.z, g.cellAt(d, hint)) != bd) bad++;
			if (dotQ(g, d.x, d.y, d.z, g.cellAt(d, got)) != bd) bad++;
			for (const int hb : { -1, -7, g.n, g.n + 5 }) if (g.cellAt(d, hb) != got) bad++;
			if (g.cellAt(d * 1e-160) != got || g.cellAt(d * 1e160) != got || g.cellAt(d * 1e-300) != got) bad++;
		}
		eq(bad, 0, "L = " + std::to_string(L) + " wrong answers");
		bool self = true;
		for (int c = 0; c < g.n; c += (L >= 6 ? 3 : 1)) self &= g.cellAt(g.dir(c)) == c;
		ok(self, "every cell direction maps to itself, L = " + std::to_string(L));
	}
}

DM_TEST("geo: cellAt at the poles, axes, cube-map seams, edge midpoints, triangle centres, degenerate input") {
	std::vector<Vec3> special;
	for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++) for (int z = -1; z <= 1; z++) if (x || y || z) special.push_back({ double(x), double(y), double(z) });
	for (const Vec3& v : { Vec3{ 0, 5, 0 }, Vec3{ 0, -0.001, 0 }, Vec3{ 1e-9, 1, 0 }, Vec3{ 0, -1, 1e-9 }, Vec3{ 1, 1, 1e-12 },
	                       Vec3{ 1, 0.999999, 0 }, Vec3{ 0.5, 0.5, -0.5000001 }, Vec3{ 1e-300, 0, 1e-300 }, Vec3{ 1e150, -1e150, 1e150 } }) special.push_back(v);
	for (int L : { 0, 1, 2, 4, 6, 7 }) {
		const auto gp = sharedGeo(L);
		const Geo& g = *gp;
		eq(g.cellAt(0, 1, 0), 0, "north pole"); eq(g.cellAt(0, -1, 0), 11, "south pole");
		eq(g.cellAt(0, 250, 0, 11), 0, "north pole with the south pole as hint");
		int bad = 0;
		for (const Vec3& v : special) { double bd; brute(g, v.x, v.y, v.z, &bd); if (dotQ(g, v.x, v.y, v.z, g.cellAt(v)) != bd) bad++; }
		const int stride = L <= 3 ? 1 : L == 6 ? 211 : 2111;
		for (int t = 0; t < g.nTri; t += stride) {
			const int a = int(g.tris[size_t(3 * t)]), b = int(g.tris[size_t(3 * t + 1)]), c = int(g.tris[size_t(3 * t + 2)]);
			const Vec3 m = g.dir(a) + g.dir(b);
			double bd;
			brute(g, m.x, m.y, m.z, &bd);
			if (dotQ(g, m.x, m.y, m.z, g.cellAt(m)) != bd || dotQ(g, m.x, m.y, m.z, g.cellAt(m, c)) != bd) bad++;
			const Vec3 tc = m + g.dir(c);
			brute(g, tc.x, tc.y, tc.z, &bd);
			if (dotQ(g, tc.x, tc.y, tc.z, g.cellAt(tc)) != bd) bad++;
			if (g.cellAt(g.dir(a) * 1.05 + g.dir(b)) != a || g.cellAt(g.dir(a) + g.dir(b) * 1.05) != b) bad++;
		}
		eq(bad, 0, "L = " + std::to_string(L));
	}
	const auto gp = sharedGeo(5);
	bool valid = true;
	for (const Vec3& v : { Vec3{ 0, 0, 0 }, Vec3{ std::nan(""), 0, 1 }, Vec3{ std::nan(""), std::nan(""), std::nan("") }, Vec3{ INFINITY, 0, 0 }, Vec3{ -INFINITY, INFINITY, 1 } }) {
		const int c1 = gp->cellAt(v), c2 = gp->cellAt(v, 17);
		valid &= c1 >= 0 && c1 < gp->n && c2 >= 0 && c2 < gp->n;
	}
	ok(valid, "degenerate input still returns a cell id");
}

DM_TEST("geo: within equals the brute-force set, radius inclusive, c first, bad input") {
	const std::pair<int, std::vector<int>> cases[] = { { 2, { 0, 5, 11, 12, 100, 161 } }, { 4, { 0, 7, 11, 12, 1300, 2561 } }, { 6, { 0, 11, 12, 1000, 20481, 40961 } } };
	std::vector<int32_t> out;
	for (const auto& cs : cases) {
		const auto gp = sharedGeo(cs.first);
		const Geo& g = *gp;
		const double radii[] = { 0, 1e-9, 0.5 * g.step, g.step, 1.3 * g.step, 2.5 * g.step, 0.1, 0.3, 1.2, kPi / 2, 2.0, 3.0, 3.14, kPi, 4, INFINITY };
		for (int c : cs.second) {
			for (double ang : radii) {
				if (cs.first == 6 && ang > 0.4 && ang < kPi && c != 0) continue;     // keep the brute force affordable
				const int count = g.within(c, ang, out);
				const auto want = bruteWithin(g, c, ang);
				if (!eq(size_t(count), want.size(), "count L=" + std::to_string(cs.first) + " c=" + std::to_string(c) + " ang=" + std::to_string(ang))) break;
				if (!ok(out[0] == c && out.size() == size_t(count), "c first, out sized")) break;
				if (!ok(sortedCopy(out) == want, "same set")) break;
			}
			eq(g.within(c, 0, out), 1, "radius 0"); eq(g.within(c, kPi, out), g.n, "radius pi");
			eq(g.within(c, 0.5 * g.step, out), 1, "half a step reaches no neighbour");
		}
	}
	const auto gp = sharedGeo(6);
	const Geo& g = *gp;
	for (int c : { 0, 12, 4242, 30000 }) {
		for (int far : { int(g.nbr[size_t(6 * c)]), int(g.nbr[size_t(6 * c + 3)]), (c + 5000) % g.n, (c + 20011) % g.n }) {
			const double ang = g.angle(c, far);
			g.within(c, ang, out);
			ok(std::find(out.begin(), out.end(), far) != out.end(), "a cell exactly at the radius is included");
			ok(sortedCopy(out) == bruteWithin(g, c, ang), "inclusive radius = brute force");
			const double less = std::nextafter(ang, 0.0);
			g.within(c, less, out);
			ok(std::find(out.begin(), out.end(), far) == out.end(), "one ulp below: excluded");
			ok(sortedCopy(out) == bruteWithin(g, c, less), "one ulp below = brute force");
		}
	}
	for (double ang : std::vector<double>{ -1.0, -0.0, std::nan(""), -double(INFINITY) }) { eq(g.within(77, ang, out), 1, "degenerate radius"); eq(out[0], 77); }
	for (int c : { -1, g.n, g.n + 100 }) { eq(g.within(c, 0.3, out), 0, "not a cell"); eq(g.ring(c, 2, out), 0, "ring: not a cell"); }
	g.within(500, 0.2, out);
	eq(std::set<int32_t>(out.begin(), out.end()).size(), out.size(), "no duplicates");
}

DM_TEST("geo: within and ring are re-entrant (nested calls while iterating a result)") {
	const auto gp = sharedGeo(5);
	const Geo& g = *gp;
	std::vector<int32_t> A, B, C, D;
	const int nA = g.within(4000, 0.25, A);
	const auto keep = A;
	ok(nA > 100, "outer result size");
	bool good = true;
	for (int i = 0; i < nA && good; i++) {
		const int c = A[size_t(i)];
		g.within(c, 0.09, B);
		good &= ok(B[0] == c, "inner result starts with its centre");
		for (int32_t m : B) good &= ok(g.angle(c, m) <= 0.09, "inner result within radius");
		if (i % 37 == 0) good &= ok(sortedCopy(B) == bruteWithin(g, c, 0.09), "inner = brute force");
		const int nC = g.ring(c, 2, C);
		good &= ok(C[0] == c && nC >= 16 && nC <= 19, "inner ring");
		for (int k = 0; k < nC; k += 5) g.within(C[size_t(k)], 0.05, D);
	}
	ok(A == keep, "outer result undisturbed");
	g.within(4000, 0.25, B);
	ok(B == keep, "repeatable");
}

DM_TEST("geo: ring is a breadth-first search by hops (c first, neighbour order, closed forms)") {
	std::vector<int32_t> out;
	for (int L : { 0, 2, 4, 6 }) {
		const auto gp = sharedGeo(L);
		const Geo& g = *gp;
		for (int c : { 0, 11, 5, 12, g.n / 2, g.n - 1 }) {
			if (c >= g.n) continue;
			const auto dist = hopDist(g, c);
			for (int steps : { 0, 1, 2, 3, 7, 20 }) {
				const int count = g.ring(c, steps, out);
				int want = 0;
				for (int dd : dist) if (dd <= steps) want++;
				if (!eq(count, want, "ring count")) break;
				bool order = out[0] == c;
				for (int i = 1; i < count; i++) order &= dist[size_t(out[size_t(i)])] >= dist[size_t(out[size_t(i - 1)])] && dist[size_t(out[size_t(i)])] <= steps;
				ok(order, "BFS order within the hop limit");
			}
			eq(g.ring(c, 1, out), 1 + g.nbrN[size_t(c)], "one hop");
			bool slots = true;
			for (int j = 0; j < g.nbrN[size_t(c)]; j++) slots &= out[size_t(1 + j)] == g.nbr[size_t(6 * c + j)];
			ok(slots, "first hop in neighbour order");
			eq(g.ring(c, 1000000000, out), g.n, "enough hops reach the whole sphere");
			eq(g.ring(c, -3, out), 1, "negative steps");
		}
	}
	const auto gp = sharedGeo(6);
	const Geo& g = *gp;
	for (int k = 0; k <= 6; k++) eq(g.ring(0, k, out), 1 + 5 * k * (k + 1) / 2, "pentagon ring " + std::to_string(k));
	const int hex = g.nbr[size_t(6 * g.nbr[size_t(6 * g.nbr[size_t(6 * g.nbr[size_t(6 * 12 + 1)] + 1)] + 1)] + 1)];
	const auto dp = hopDist(g, hex);
	int nearestPent = 1 << 30;
	for (int i = 0; i < 12; i++) nearestPent = std::min(nearestPent, dp[size_t(i)]);
	const int kMax = std::min(6, nearestPent - 1);
	ok(kMax >= 1, "test cell away from pentagons");
	for (int k = 0; k <= kMax; k++) eq(g.ring(hex, k, out), 1 + 3 * k * (k + 1), "hexagon ring " + std::to_string(k));
}

DM_TEST("geo: timings at L = 6 (reported; asserted only at 5x the targets)") {
	double buildMs = 1e9;
	for (int i = 0; i < 3; i++) { const double t = nowMs(); Geo::build(6); buildMs = std::min(buildMs, nowMs() - t); }
	const auto gp = sharedGeo(6);
	const Geo& g = *gp;
	Rng r(99);
	const int N = 200000;
	std::vector<Vec3> dirs(static_cast<size_t>(N));
	for (auto& d : dirs) d = randDir(r);
	std::vector<int> ans(static_cast<size_t>(N));
	double t = nowMs();
	for (int i = 0; i < N; i++) ans[size_t(i)] = g.cellAt(dirs[size_t(i)]);
	const double cold = (nowMs() - t) / N * 1000;
	long long sink = 0;
	t = nowMs();
	for (int i = 0; i < N; i++) sink += g.cellAt(dirs[size_t(i)], g.nbr[size_t(6 * ans[size_t(i)] + i % 5)]);
	const double hinted = (nowMs() - t) / N * 1000;
	std::vector<int32_t> out;
	t = nowMs();
	long long cells = 0;
	for (int i = 0; i < 2000; i++) cells += g.within(i * 17 % g.n, 0.1, out);
	const double withinUs = (nowMs() - t) / 2000 * 1000;
	std::printf("       geo L=6: build %.1f ms, cellAt %.3f us cold / %.3f us hinted, within(0.1 rad, %lld cells) %.1f us  [%lld]\n",
		buildMs, cold, hinted, cells / 2000, withinUs, sink % 7);
	ok(buildMs < 5 * 400, "build < 5 x 400 ms");
	ok(cold < 5 * 10, "cellAt < 5 x 10 us");
	ok(hinted < 5 * 2, "hinted cellAt < 5 x 2 us");
}
