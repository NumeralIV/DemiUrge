// DEMIURGE — SimTests/test_core_util.cpp : Core/Util.h and Core/Rng.h (port of tests/util.test.js).
// The pinned values are the JS reference's: the C++ port reproduces DM.U bit for bit.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

#include "Core/Rng.h"
#include "Core/Util.h"
#include "TestFramework.h"

using namespace dm;
using namespace dmt;

namespace {

double chi2(const std::vector<double>& counts, double total) {
	const double e = total / double(counts.size());
	double c = 0;
	for (double v : counts) c += (v - e) * (v - e) / e;
	return c;
}
double chi2Max(int k) { return (k - 1) + 5 * std::sqrt(2.0 * (k - 1)); }
double corr(const std::vector<double>& a, const std::vector<double>& b) {
	const size_t n = std::min(a.size(), b.size());
	double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
	for (size_t i = 0; i < n; i++) { sa += a[i]; sb += b[i]; saa += a[i] * a[i]; sbb += b[i] * b[i]; sab += a[i] * b[i]; }
	const double ma = sa / n, mb = sb / n;
	return (sab / n - ma * mb) / std::sqrt((saa / n - ma * ma) * (sbb / n - mb * mb));
}
// Order-sensitive digest of the exact bit patterns of a sequence of doubles (as in the JS test).
struct BitDigest {
	uint32_t h = 0x811C9DC5u;
	void add(double v) {
		uint64_t b; std::memcpy(&b, &v, 8);
		h = (h ^ uint32_t(b)) * 0x01000193u;
		h = (h ^ uint32_t(b >> 32)) * 0x01000193u;
	}
};
int popcount(uint32_t x) { int c = 0; while (x) { x &= x - 1; c++; } return c; }

} // namespace

// ---------------------------------------------------------------- scalar helpers

DM_TEST("util: constants") {
	eq(kPlanetR, 100.0);
	near(kTau, 6.283185307179586, 1e-15);
	near(kPi * 2, kTau, 0);
}

DM_TEST("util: clamp / sat / lerp / smooth / sstep / remap / approach / damp") {
	eq(clamp(5, 0, 10), 5.0); eq(clamp(-5, 0, 10), 0.0); eq(clamp(15, 0, 10), 10.0); eq(clamp(-3, -8, -4), -4.0);
	eq(clamp(2, 3, 3), 3.0, "degenerate range");
	eq(sat(0.25), 0.25); eq(sat(-1e9), 0.0); eq(sat(7), 1.0); eq(sat(0.9999999), 0.9999999); eq(sat(1e-9), 1e-9);
	near(lerp(0, 10, 0.5), 5, 1e-12); eq(lerp(2, 8, 0), 2.0); eq(lerp(2, 8, 1), 8.0); near(lerp(2, 8, 1.5), 11, 1e-12);
	eq(smooth(0), 0.0); eq(smooth(1), 1.0); near(smooth(0.25), 0.15625, 1e-12); eq(smooth(1.5), 1.0); eq(smooth(-0.5), 0.0);
	eq(sstep(2, 6, 1), 0.0); eq(sstep(2, 6, 6), 1.0); near(sstep(2, 6, 3), 0.15625, 1e-12);
	eq(sstep(6, 2, 7), 0.0); eq(sstep(6, 2, 1), 1.0); near(sstep(6, 2, 5), 0.15625, 1e-12, "mirrored");
	eq(sstep(3, 3, 2.9), 0.0); eq(sstep(3, 3, 3), 1.0);
	near(remap(5, 0, 10, 100, 200), 150, 1e-12); eq(remap(-5, 0, 10, 100, 200), 100.0); eq(remap(50, 0, 10, 100, 200), 200.0);
	near(remap(2.5, 0, 10, 200, 100), 175, 1e-12); near(remap(7.5, 10, 0, 0, 1), 0.25, 1e-12);
	eq(remap(1, 3, 3, 5, 9), 5.0); eq(remap(4, 3, 3, 5, 9), 9.0); ok(!std::isnan(remap(3, 3, 3, 5, 9)));
	eq(approach(0, 10, 3), 3.0); eq(approach(9, 10, 3), 10.0); eq(approach(10, 0, 4), 6.0); eq(approach(1, 0, 4), 0.0);
	eq(approach(-2, -10, 3), -5.0); eq(approach(7, 3, 0), 7.0);
	eq(damp(3, 10, 5, 0), 3.0); near(damp(3, 10, 5, 100), 10, 1e-9); near(damp(0, 1, std::log(2.0), 1), 0.5, 1e-12);
	double many = 2;
	for (int i = 0; i < 10; i++) many = damp(many, -7, 3.3, 0.025);
	near(many, damp(2, -7, 3.3, 0.25), 1e-12, "frame-rate independent");
	eq(toInt32(5.9), 5); eq(toInt32(-5.9), -5); eq(toInt32(4294967296.0 + 5), 5); eq(toInt32(2147483648.0), -2147483647 - 1);
	eq(toInt32(std::nan("")), 0); eq(toInt32(INFINITY), 0);
	ok(isFinite(1.0) && !isFinite(std::nan("")) && !isFinite(INFINITY));
}

// ---------------------------------------------------------------- hashes

DM_TEST("hash32 / hash2 / hashf: pinned values (same as JS)") {
	const int32_t in[] = { 0, 1, 2, -1, 12345, 0x7fffffff };
	const uint32_t want[] = { 33350994u, 2672842292u, 127880910u, 2767685996u, 1084875187u, 3623524728u };
	for (int i = 0; i < 6; i++) eq(hash32(uint32_t(in[i])), want[i], "hash32");
	const int32_t pa[][2] = { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 }, { -5, 77 }, { 40961, 3 } };
	const uint32_t want2[] = { 2685500393u, 999151060u, 1156163646u, 1007823597u, 996928262u, 232623060u };
	for (int i = 0; i < 6; i++) eq(hash2(uint32_t(pa[i][0]), uint32_t(pa[i][1])), want2[i], "hash2");
	eq(hashf(7), 0.4849326182156801);
	eq(hashf(7, 1), 0.31403061165474355);
	eq(hash32(0xFFFFFFFFu), hash32(uint32_t(-1)));
}

DM_TEST("hash32: bijective on a range, avalanche") {
	std::set<uint32_t> seen;
	for (int i = -50000; i < 150000; i++) seen.insert(hash32(uint32_t(i)));
	eq(seen.size(), size_t(200000), "no collisions");
	const int N = 2000;
	std::vector<double> flips(32 * 32, 0.0);
	double total = 0;
	for (int n = 0; n < N; n++) {
		const uint32_t a = (uint32_t(n) * 0x9E3779B1u) ^ uint32_t(n * 7919);
		const uint32_t h = hash32(a);
		for (int b = 0; b < 32; b++) {
			const uint32_t d = h ^ hash32(a ^ (1u << b));
			total += popcount(d);
			for (int o = 0; o < 32; o++) if (d & (1u << o)) flips[size_t(b * 32 + o)]++;
		}
	}
	near(total / (N * 32.0), 16, 0.2, "mean flipped bits");
	double worst = 0;
	for (double f : flips) worst = std::max(worst, std::fabs(f / N - 0.5));
	ok(worst < 0.07, "worst bias " + std::to_string(worst));
}

DM_TEST("hash2: ordered, bijective per argument; hashf uniform and uncorrelated") {
	ok(hash2(1, 2) != hash2(2, 1), "order matters");
	std::set<uint32_t> sa, sb;
	for (uint32_t i = 0; i < 100000; i++) { sa.insert(hash2(i, 17)); sb.insert(hash2(17, i)); }
	eq(sa.size(), size_t(100000)); eq(sb.size(), size_t(100000));
	const int N = 200;
	std::vector<double> a, right, down, bins(32, 0.0);
	double sum = 0, mn = 1, mx = 0;
	for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) {
		const double v = hashf(uint32_t(i), uint32_t(j));
		mn = std::min(mn, v); mx = std::max(mx, v); sum += v;
		bins[size_t(v * 32)]++;
		a.push_back(v); right.push_back(hashf(uint32_t(i + 1), uint32_t(j))); down.push_back(hashf(uint32_t(i), uint32_t(j + 1)));
	}
	ok(mn >= 0 && mx < 1, "range");
	near(sum / (N * N), 0.5, 5 * 0.2887 / N, "mean");
	ok(chi2(bins, N * N) < chi2Max(32), "uniform");
	ok(std::fabs(corr(a, right)) < 5.0 / N, "neighbours in a uncorrelated");
	ok(std::fabs(corr(a, down)) < 5.0 / N, "neighbours in b uncorrelated");
	eq(hashStr(""), 0x811C9DC5u);
	ok(hashStr("terrain") != hashStr("Terrain"));
}

// ---------------------------------------------------------------- Rng

DM_TEST("rng: pinned streams (same as JS mulberry32)") {
	auto take = [](Rng r, int n) { std::vector<uint32_t> v; for (int i = 0; i < n; i++) v.push_back(r.next()); return v; };
	Rng r0(0);
	eq(take(r0, 5), std::vector<uint32_t>{ 1144304738u, 1416247u, 958946056u, 627933444u, 2007157716u });
	for (int i = 0; i < 5; i++) r0.next();
	eq(r0.s, 567894473u);
	eq(take(Rng(1), 5), std::vector<uint32_t>{ 2693262067u, 11749833u, 2265367787u, 4213581821u, 4159151403u });
	eq(take(Rng(12345), 5), std::vector<uint32_t>{ 4207900869u, 1317490944u, 2079646450u, 3513001552u, 2187978186u });
	eq(take(Rng(0xFFFFFFFFu), 3), std::vector<uint32_t>{ 3850105811u, 813802916u, 3073704848u });
	eq(sizeof(Rng), sizeof(uint32_t), "the state is exactly one uint32");
	std::set<uint32_t> seen;
	Rng r(0);
	for (int i = 0; i < 1000; i++) seen.insert(r.next());
	eq(seen.size(), size_t(1000), "seed 0 is not degenerate");
}

DM_TEST("rng: assigning s restores the stream exactly, for every method") {
	Rng r(987654321u);
	for (int i = 0; i < 37; i++) r.next();
	const uint32_t saved = r.s;
	std::vector<int> arr = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	auto run = [&]() {
		std::vector<double> v;
		v.push_back(r.next()); v.push_back(r.f()); v.push_back(r.range(-3, 9)); v.push_back(r.integer(-5, 5));
		v.push_back(r.chance(0.5)); v.push_back(*r.pick(arr)); v.push_back(*r.weighted(arr, [](int x) { return double(x); }));
		v.push_back(r.gauss()); v.push_back(r.gauss());
		std::vector<int> c = arr; r.shuffle(c); v.push_back(c[0] * 10 + c[8]);
		v.push_back(r.fork(3).next()); v.push_back(r.fork("abc").f()); v.push_back(r.next()); v.push_back(r.s);
		return v;
	};
	const auto first = run();
	r.s = saved;
	eq(run(), first, "same stream after restoring s");
}

DM_TEST("rng: f, range, integer, chance, pick") {
	Rng r(2024);
	const int N = 200000;
	std::vector<double> a(N), bins(64, 0.0);
	double s = 0, s2 = 0, mn = 1, mx = 0;
	for (int i = 0; i < N; i++) {
		const double v = r.f();
		a[size_t(i)] = v; s += v; s2 += v * v; mn = std::min(mn, v); mx = std::max(mx, v);
		bins[size_t(v * 64)]++;
	}
	ok(mn >= 0 && mx < 1, "[0,1)");
	near(s / N, 0.5, 5 * 0.2887 / std::sqrt(double(N)), "mean");
	near(s2 / N - (s / N) * (s / N), 1.0 / 12, 0.002, "variance");
	ok(chi2(bins, N) < chi2Max(64), "uniform");
	std::vector<double> lag(a.begin() + 1, a.end()), base(a.begin(), a.end() - 1);
	ok(std::fabs(corr(base, lag)) < 5 / std::sqrt(double(N)), "no serial correlation");
	for (int i = 0; i < 20000; i++) { const double v = r.range(-3, 7); if (!(v >= -3 && v < 7)) { fail("range"); break; } }
	eq(r.range(5, 5), 5.0, "empty range");
	std::vector<double> ib(11, 0.0);
	for (int i = 0; i < 110000; i++) {
		const int v = r.integer(-5, 5);
		if (v < -5 || v > 5) { fail("integer out of range"); break; }
		ib[size_t(v + 5)]++;
	}
	ok(chi2(ib, 110000) < chi2Max(11), "integer uniform, both ends inclusive");
	for (int i = 0; i < 1000; i++) { const int v = r.integer(9, 7); if (v < 7 || v > 9) { fail("integer: reversed args"); break; } }
	eq(r.integer(4, 4), 4);
	int hits = 0;
	for (int i = 0; i < 100000; i++) hits += r.chance(0.3) ? 1 : 0;
	near(hits / 100000.0, 0.3, 5 * std::sqrt(0.21 / 100000));
	{
		Rng q(5), twin(5);
		q.chance(0); twin.next();
		eq(q.s, twin.s, "chance always consumes exactly one draw");
	}
	std::vector<int> empty;
	const uint32_t s0 = r.s;
	ok(r.pick(empty) == nullptr, "pick of empty");
	eq(r.pickIndex(0), -1);
	eq(r.s, s0, "no draw for an empty pick");
	std::vector<double> pb(7, 0.0);
	std::vector<int> seven = { 0, 1, 2, 3, 4, 5, 6 };
	for (int i = 0; i < 70000; i++) pb[size_t(*r.pick(seven))]++;
	ok(chi2(pb, 70000) < chi2Max(7), "pick uniform");
}

DM_TEST("rng.weighted: proportional, zero weights never, edge cases, no draw when nothing can be chosen") {
	Rng r(9);
	struct It { char k; double w; };
	const std::vector<It> items = { { 'a', 0 }, { 'b', 1 }, { 'c', 0 }, { 'd', 3 }, { 'e', 0 } };
	std::map<char, int> counts;
	const int N = 80000;
	for (int i = 0; i < N; i++) counts[r.weighted(items, [](const It& it) { return it.w; })->k]++;
	eq(counts['a'] + counts['c'] + counts['e'], 0, "zero weights are never chosen");
	near(counts['b'] / double(N), 0.25, 5 * std::sqrt(0.25 * 0.75 / N));
	std::vector<int> three = { 1, 2, 3 };
	const uint32_t s0 = r.s;
	ok(r.weighted(std::vector<int>{}, [](int) { return 1.0; }) == nullptr, "empty");
	ok(r.weighted(three, [](int) { return 0.0; }) == nullptr, "all zero");
	ok(r.weighted(three, [](int) { return -1.0; }) == nullptr, "negative counts as zero");
	ok(r.weighted(three, [](int) { return std::nan(""); }) == nullptr, "NaN counts as zero");
	eq(r.s, s0, "no draw consumed");
	Rng twin(0); twin.s = r.s;
	r.weighted(three, [](int x) { return double(x); }); twin.next();
	eq(r.s, twin.s, "exactly one draw");
	bool good = true;
	for (int i = 0; i < 2000 && good; i++) {
		good &= *r.weighted(std::vector<int>{ 10, 20, 30 }, [](int x) { return x == 20 ? 1e-9 : 0.0; }) == 20;
		good &= *r.weighted(std::vector<int>{ 10, 20, 30 }, [](int x) { return x == 10 ? INFINITY : 1.0; }) == 10;
	}
	ok(good, "tiny / infinite weights");
	std::vector<std::string> calls;
	std::vector<std::string> xyz = { "x", "y", "z" };
	r.weighted(xyz, [&](const std::string& s) { calls.push_back(s); return 1.0; });
	eq(calls, xyz, "wfn called once per element, in order");
	std::vector<double> hit(2, 0.0);
	std::vector<int> big(5000);
	for (int i = 0; i < 5000; i++) big[size_t(i)] = i;
	for (int i = 0; i < 4000; i++) { const int v = *r.weighted(big, [](int x) { return (x == 123 || x == 4999) ? 1.0 : 0.0; }); hit[v == 123 ? 0 : 1]++; }
	ok(chi2(hit, 4000) < chi2Max(2), "large arrays");
	std::vector<double> inf(5, 0.0);
	std::vector<int> five = { 0, 1, 2, 3, 4 };
	for (int i = 0; i < 9000; i++) inf[size_t(*r.weighted(five, [](int x) { return (x == 1 || x == 2 || x == 4) ? INFINITY : 1e300; }))]++;
	eq(inf[0] + inf[3], 0.0, "finite weights lose to infinite ones");
	ok(chi2({ inf[1], inf[2], inf[4] }, 9000) < chi2Max(3), "infinite weights equally likely");
	int over = 0;
	for (int i = 0; i < 3000; i++) over += *r.weighted(five, [](int x) { return x < 2 ? 1e308 : 0.0; }) < 2 ? 1 : 0;
	eq(over, 3000, "overflowing finite sums still pick a positive weight");
}

DM_TEST("rng.weighted: exact interval boundaries (driven with chosen draws)") {
	const double one4[] = { 1, 1, 1, 1 };
	std::string got;
	for (double f : { 0.0, 0.2499, 0.25, 0.4999, 0.5, 0.7499, 0.75, 0.9999 }) got += char('a' + Rng::selectWeightedWith(one4, 4, 4, f));
	eq(got, std::string("aabbccdd"));
	const double w0[] = { 0, 1, 1 };
	eq(Rng::selectWeightedWith(w0, 3, 2, 0), 1, "a draw of 0 skips leading zero weights");
	const double w123[] = { 1, 2, 3 };
	eq(Rng::selectWeightedWith(w123, 3, 6, 0.75), 2);
	eq(Rng::selectWeightedWith(w123, 3, 6, 0.49), 1);
	eq(Rng::selectWeightedWith(w123, 3, 6, 0.16), 0);
	const double tail[] = { 0.3, 0.3, 0, 0 };
	eq(Rng::selectWeightedWith(tail, 4, 0.6, 1), 1, "round-off returns the last positive weight");
}

DM_TEST("rng.weighted: re-entrant (wfn may itself call weighted)") {
	Rng outer(21), inner(22);
	struct Grp { int g; std::vector<int> m; };
	std::vector<Grp> groups;
	for (int g = 0; g < 40; g++) { Grp x; x.g = g; for (int i = 0; i < 300; i++) x.m.push_back(i == g ? 1 : 0); groups.push_back(x); }
	bool good = true;
	for (int rep = 0; rep < 50; rep++) {
		const Grp* el = outer.weighted(groups, [&](const Grp& grp) {
			const int idx = inner.weightedIndex(int(grp.m.size()), [&](int i) { return double(grp.m[size_t(i)]); });
			good &= idx == grp.g;
			return grp.g == 7 ? 1.0 : 0.0;
		});
		good &= el && el->g == 7;
	}
	ok(good, "nested picks are correct");
}

DM_TEST("rng.gauss: moments, two draws, pinned values (same as JS)") {
	Rng r(31);
	const int N = 200000;
	double s = 0, s2 = 0, s3 = 0, s4 = 0, in1 = 0, in2 = 0, mx = 0;
	for (int i = 0; i < N; i++) {
		const double v = r.gauss();
		s += v; s2 += v * v; s3 += v * v * v; s4 += v * v * v * v;
		const double a = std::fabs(v);
		if (a < 1) in1++;
		if (a < 2) in2++;
		mx = std::max(mx, a);
	}
	const double mean = s / N;
	near(mean, 0, 5 / std::sqrt(double(N)), "mean");
	near(s2 / N - mean * mean, 1, 5 * std::sqrt(2.0 / N), "variance");
	near(s3 / N, 0, 5 * std::sqrt(15.0 / N), "skew");
	near(s4 / N, 3, 5 * std::sqrt(96.0 / N), "fourth moment");
	near(in1 / N, 0.682689, 0.006); near(in2 / N, 0.9545, 0.003);
	ok(mx > 3.5 && mx < 7, "tails");
	Rng a(77), b(77);
	a.gauss(); b.next(); b.next();
	eq(a.s, b.s, "exactly two draws");
	Rng p(77);
	std::vector<double> got;
	for (int i = 0; i < 6; i++) got.push_back(p.gauss());
	eq(got, std::vector<double>{ 1.0161914987439675, -1.5371570923207079, 0.22827116512377127, 0.43924356944125464,
		0.14306319778741486, -1.893097504621608 }, "pinned");
	eq(p.s, 503953353u);
	eq(Rng::gaussFrom(0x12345678u, 0x9ABCDEF0u), -0.30431793207672686);
	eq(Rng::gaussFrom(0, 0x12345678u), 0.0, "u = 1");
	near(Rng::gaussFrom(0xFFFFFFFFu, 0), std::sqrt(64 * std::log(2.0)), 1e-14, "largest radius");
	ok(Rng::gaussFrom(0x80000000u, 0x40000000u) == 0 && Rng::gaussFrom(0x80000000u, 0xC0000000u) == 0, "exact zeros at quarter turns");
	Rng q(4321), q2(4321);
	double worst = 0;
	for (int i = 0; i < 100000; i++) {
		const double u = 1 - q2.f(), v = q2.f();
		worst = std::max(worst, std::fabs(q.gauss() - std::sqrt(-2 * std::log(u)) * std::cos(kTau * v)));
	}
	ok(worst < 1e-14, "agrees with sqrt(-2 ln u) cos(2 pi v): " + std::to_string(worst));
}

DM_TEST("rng.shuffle: permutation, uniform, pinned, n - 1 draws") {
	Rng r(41);
	std::vector<int> arr(500);
	for (int i = 0; i < 500; i++) arr[size_t(i)] = i;
	r.shuffle(arr);
	std::vector<int> sorted = arr;
	std::sort(sorted.begin(), sorted.end());
	bool perm = true;
	for (int i = 0; i < 500; i++) perm &= sorted[size_t(i)] == i;
	ok(perm, "a permutation");
	std::map<std::string, double> counts;
	for (int i = 0; i < 48000; i++) {
		std::vector<int> f = { 0, 1, 2, 3 };
		r.shuffle(f);
		counts[std::to_string(f[0]) + std::to_string(f[1]) + std::to_string(f[2]) + std::to_string(f[3])]++;
	}
	eq(counts.size(), size_t(24));
	std::vector<double> cv;
	for (auto& kv : counts) cv.push_back(kv.second);
	ok(chi2(cv, 48000) < chi2Max(24), "uniform over permutations");
	std::vector<int> p8 = { 1, 2, 3, 4, 5, 6, 7, 8 };
	Rng(5).shuffle(p8);
	Rng five(5);
	five.shuffle(p8 = { 1, 2, 3, 4, 5, 6, 7, 8 });
	eq(p8, std::vector<int>{ 5, 3, 7, 1, 4, 2, 8, 6 }, "pinned (same as JS)");
	for (int n : { 0, 1, 2, 3, 10, 257 }) {
		Rng x(uint32_t(900 + n)), y(uint32_t(900 + n));
		std::vector<int> z(size_t(n), 0);
		x.shuffle(z);
		for (int i = 0; i < n - 1; i++) y.next();
		eq(x.s, y.s, std::to_string(n) + " elements");
	}
}

DM_TEST("rng.fork: does not advance the parent, deterministic, pinned (same as JS)") {
	Rng p(2024);
	eq(std::vector<uint32_t>{ p.fork(0).s, p.fork(1).s, p.fork(-1).s, p.fork("terrain").s, p.fork("climate").s, p.fork("").s, p.fork("1").s },
		std::vector<uint32_t>{ 2505585638u, 960821982u, 494661753u, 4095389963u, 3280744687u, 1043667120u, 4084727250u });
	eq(p.s, 2024u, "parent untouched");
	std::set<uint32_t> states;
	Rng q(31337);
	for (int t = -2000; t < 2000; t++) states.insert(q.fork(t).s);
	for (int t = 0; t < 2000; t++) states.insert(q.fork("tag" + std::to_string(t)).s);
	states.insert(q.s);
	eq(states.size(), size_t(6001), "no two tags share a state");
	const int N = 20000;
	auto stream = [&](Rng x) { std::vector<double> v(N); for (int i = 0; i < N; i++) v[size_t(i)] = x.f(); return v; };
	const auto par = stream(q), f0 = stream(q.fork(0)), f1 = stream(q.fork(1)), fa = stream(q.fork("a")), fb = stream(q.fork("b"));
	const double lim = 5 / std::sqrt(double(N));
	ok(std::fabs(corr(par, f0)) < lim && std::fabs(corr(f0, f1)) < lim && std::fabs(corr(fa, fb)) < lim && std::fabs(corr(f1, fa)) < lim, "decorrelated");
	ok(q.fork("a").fork("b").s != q.fork("b").fork("a").s, "fork order matters");
	std::vector<double> bins(32, 0.0);
	Rng pp(8675309);
	for (int t = 0; t < 40000; t++) bins[size_t(pp.fork(t).f() * 32)]++;
	ok(chi2(bins, 40000) < chi2Max(32), "first outputs over many tags are uniform");
}

DM_TEST("bit-exact digests of long streams (identical to the JS reference)") {
	BitDigest d;
	Rng g(20261117u);
	for (int i = 0; i < 100000; i++) d.add(g.gauss());
	eq(d.h, 1538461678u, "gauss digest"); eq(g.s, 4012119869u);

	d = BitDigest();
	Rng q(1234567);
	for (int i = 0; i < 200000; i++) {
		const double s = i % 4 == 0 ? 2 : i % 4 == 1 ? 40 : i % 4 == 2 ? 3000 : 1e6;
		const double x = q.range(-s, s), y = q.range(-s, s), z = q.range(-s, s);
		d.add(noise3(x, y, z));
	}
	eq(d.h, 2413778205u, "noise3 digest"); eq(q.s, 3386876231u);

	d = BitDigest();
	Rng p(7654321);
	for (int i = 0; i < 30000; i++) {
		const double x = p.range(-9, 9), y = p.range(-9, 9), z = p.range(-9, 9);
		d.add(fbm3(x, y, z, 6)); d.add(fbm3(x, y, z, 3, 2.3, 0.45));
		d.add(ridged3(x, y, z, 5)); d.add(ridged3(x, y, z, 4, 1.9, 0.6));
	}
	eq(d.h, 570148942u, "fbm3 / ridged3 digest"); eq(p.s, 86003841u);

	d = BitDigest();
	for (int i = 0; i < 50000; i++) {
		d.add(hashf(uint32_t(i), uint32_t(i * 7 - 3)));
		d.add(double(hash32(uint32_t(toInt32(double(i) * 2654435761.0)))));
		d.add(double(hash2(uint32_t(-i), uint32_t(i))));
	}
	eq(d.h, 2925838791u, "hash digest");

	d = BitDigest();
	Rng w(99);
	const std::vector<int> pk = { 3, 1, 4, 1, 5, 9, 2, 6 };
	const std::vector<int> wt = { 1, 2, 3, 4, 5 };
	for (int i = 0; i < 20000; i++) {
		d.add(w.f()); d.add(w.range(-3, 11)); d.add(w.integer(-7, 7)); d.add(w.chance(0.37) ? 1 : 0);
		d.add(*w.pick(pk)); d.add(*w.weighted(wt, [](int x) { return double(x * x); }));
		d.add(w.fork(i).next()); d.add(w.fork("s" + std::to_string(i)).next());
		if (i % 100 == 0) { std::vector<int> sh = { 1, 2, 3, 4, 5, 6, 7 }; w.shuffle(sh); d.add(sh[3]); }
	}
	eq(d.h, 1318555029u, "every other Rng method"); eq(w.s, 391841939u);
}

// ---------------------------------------------------------------- noise

DM_TEST("noise3 / fbm3 / ridged3: pinned values (same as JS)") {
	eq(noise3(0, 0, 0), 0.0);
	eq(noise3(0.5, 0.5, 0.5), 0.0);
	eq(noise3(0.3, 0.7, 1.9), 0.22144954838254213);
	eq(noise3(-3.25, 8.125, 100.0625), 0.3912439635911244);
	eq(noise3(12.34, -56.78, 9.1), 0.2819660259977534);
	eq(noise3(1000.5, -2000.25, 3000.125), 0.006423964719195348);
	eq(noise3(1e-3, 2e-3, -3e-3), -0.005470782167853851);
	eq(fbm3(1.1, 2.2, 3.3, 5), 0.2251045471662534);
	eq(fbm3(1.1, 2.2, 3.3, 4, 2.5, 0.6), 0.2987020207204446);
	eq(ridged3(1.1, 2.2, 3.3, 5), 0.3785580604897896);
	eq(ridged3(-4.5, 0.25, 9, 3, 2.1, 0.45), 0.7104932525278534);
	eq(fbm3(1, 2, 3, 0), 0.0, "zero octaves");
	eq(noise3(std::nan(""), 0, 0), 0.0, "NaN in -> 0 out");
	ok(std::isfinite(noise3(INFINITY, 1, 2)), "infinite input is finite");
}

DM_TEST("noise3: range, mean, sd, continuity") {
	Rng r(102);
	const int N = 300000;
	double mn = 1, mx = -1, s = 0, s2 = 0;
	for (int n = 0; n < N; n++) {
		const double v = noise3(r.range(-300, 300), r.range(-300, 300), r.range(-300, 300));
		mn = std::min(mn, v); mx = std::max(mx, v); s += v; s2 += v * v;
	}
	ok(mn >= -1 && mx <= 1, "within [-1,1]");
	ok(mn < -0.85 && mx > 0.85, "uses the range: " + std::to_string(mn) + " .. " + std::to_string(mx));
	const double mean = s / N, sd = std::sqrt(s2 / N - mean * mean);
	near(mean, 0, 0.005, "mean");
	ok(sd > 0.32 && sd < 0.38, "sd " + std::to_string(sd));
	double maxSlope = 0;
	for (int n = 0; n < 40000; n++) {
		const double x = r.range(-20, 20), y = r.range(-20, 20), z = r.range(-20, 20), h = 1e-6;
		maxSlope = std::max(maxSlope, std::fabs(noise3(x + h, y, z) - noise3(x, y, z)) / h);
	}
	ok(maxSlope < 7, "continuous: max slope " + std::to_string(maxSlope));
	double rmn = 1, rmx = 0;
	for (int n = 0; n < 50000; n++) {
		const double v = ridged3(r.range(-50, 50), r.range(-50, 50), r.range(-50, 50), 5);
		rmn = std::min(rmn, v); rmx = std::max(rmx, v);
	}
	ok(rmn >= 0 && rmx <= 1 && rmn < 0.05 && rmx > 0.9, "ridged3 in [0,1] using the range");
}

// ---------------------------------------------------------------- base64

DM_TEST("b64enc / b64dec: RFC 4648 vectors, round trips, tolerance, rejection") {
	const char* vec[][2] = { { "", "" }, { "f", "Zg==" }, { "fo", "Zm8=" }, { "foo", "Zm9v" }, { "foob", "Zm9vYg==" }, { "fooba", "Zm9vYmE=" }, { "foobar", "Zm9vYmFy" } };
	for (auto& v : vec) {
		const std::string plain = v[0];
		eq(b64enc(reinterpret_cast<const uint8_t*>(plain.data()), plain.size()), std::string(v[1]), "encode " + plain);
		std::vector<uint8_t> d;
		ok(b64dec(v[1], d), "decode ok");
		eq(std::string(d.begin(), d.end()), plain, "decode");
	}
	const uint8_t sym[] = { 0xFB, 0xFF, 0xFE };
	eq(b64enc(sym, 3), std::string("+//+"));
	Rng r(1000);
	for (int n = 0; n <= 300; n++) {
		std::vector<uint8_t> b(static_cast<size_t>(n));
		for (auto& x : b) x = uint8_t(r.next() >> 24);
		const std::string s = b64enc(b);
		if (!eq(s.size(), size_t((n + 2) / 3 * 4), "length")) break;
		std::vector<uint8_t> d;
		if (!ok(b64dec(s, d) && d == b, "round trip n = " + std::to_string(n))) break;
	}
	std::vector<uint8_t> d;
	ok(b64dec("Zm9vYg", d) && std::string(d.begin(), d.end()) == "foob", "no padding");
	ok(b64dec("Zm9v\r\nYmE=\n", d) && std::string(d.begin(), d.end()) == "fooba", "whitespace");
	ok(b64dec("-__-", d) && d == std::vector<uint8_t>{ 0xFB, 0xFF, 0xFE }, "URL-safe alphabet");
	ok(b64dec("====", d) && d.empty(), "padding only");
	ok(!b64dec("Zm9v!A==", d) && d.empty(), "invalid character");
	ok(!b64dec("Zm9vY", d), "impossible length");
	ok(!b64dec("Zg==Zg==", d), "data after padding");
	ok(!b64dec("Zm9v\xc3\xa9", d), "non-ASCII");
}

// ---------------------------------------------------------------- formatting

DM_TEST("fmtInt / fmtBig (same strings as JS)") {
	const std::pair<double, const char*> ints[] = {
		{ 12345, "12,345" }, { 0, "0" }, { 7, "7" }, { 999, "999" }, { 1000, "1,000" }, { 1234567, "1,234,567" },
		{ 1234567890123.0, "1,234,567,890,123" }, { 12345.4, "12,345" }, { 12345.5, "12,346" }, { 999.5, "1,000" },
		{ -12345, "-12,345" }, { -1000, "-1,000" }, { -1234.5, "-1,235" }, { -0.4, "0" }, { 0.49, "0" }, { 0.5, "1" },
		{ -0.6, "-1" }, { -123456, "-123,456" }, { 1e21, "1e+21" }, { 123456789012345680000.0, "123,456,789,012,345,680,000" },
	};
	for (auto& p : ints) eq(fmtInt(p.first), std::string(p.second), "fmtInt(" + std::to_string(p.first) + ")");
	eq(fmtInt(std::nan("")), std::string("NaN"));
	eq(fmtInt(INFINITY), std::string("Infinity"));
	const std::pair<double, const char*> bigs[] = {
		{ 950, "950" }, { 12345, "12.3k" }, { 4560000, "4.56M" }, { 1.2e9, "1.2B" }, { 0, "0" }, { 12.7, "13" },
		{ 999.4, "999" }, { 999.5, "1k" }, { 1001, "1k" }, { 1049, "1.05k" }, { 1500, "1.5k" }, { 9994, "9.99k" },
		{ 9995, "10k" }, { 10050, "10.1k" }, { 99949, "99.9k" }, { 99950, "100k" }, { 123456, "123k" }, { 999500, "1M" },
		{ 999999999, "1B" }, { 7.25e9, "7.25B" }, { 1.5e12, "1.5T" }, { 2.5e15, "2,500T" }, { 999.7e12, "1,000T" },
		{ 0.7, "1" }, { -0.7, "-1" }, { -1.5e6, "-1.5M" }, { -12345, "-12.3k" }, { -0.2, "0" },
	};
	for (auto& p : bigs) eq(fmtBig(p.first), std::string(p.second), "fmtBig(" + std::to_string(p.first) + ")");
}

// ---------------------------------------------------------------- byte buffers

DM_TEST("ByteWriter / ByteReader: little-endian round trip, sticky failure") {
	ByteWriter w;
	w.u8(0xAB); w.u16(0x1234); w.u32(0xDEADBEEFu); w.i32(-5); w.i64(-1234567890123LL); w.f32(1.5f); w.f64(-2.25);
	w.boolean(true); w.str("héllo");
	eq(w.buf[1], uint8_t(0x34), "little-endian");
	ByteReader r(w.buf.data(), w.buf.size());
	eq(r.u8(), uint8_t(0xAB)); eq(r.u16(), uint16_t(0x1234)); eq(r.u32(), 0xDEADBEEFu); eq(r.i32(), -5);
	eq(r.i64(), -1234567890123LL); eq(r.f32(), 1.5f); eq(r.f64(), -2.25); eq(r.boolean(), true); eq(r.str(), std::string("héllo"));
	ok(r.ok() && r.atEnd());
	eq(r.u32(), 0u, "past the end reads 0");
	ok(!r.ok(), "and fails");
	const uint8_t two[] = { 2 };
	ByteReader rb(two, 1);
	rb.boolean();
	ok(!rb.ok(), "a bool byte other than 0/1 fails");
	const uint8_t lie[] = { 0xFF, 0xFF, 0xFF, 0x7F, 'a' };
	ByteReader rs(lie, 5);
	eq(rs.str(), std::string());
	ok(!rs.ok(), "an absurd string length fails");
}
