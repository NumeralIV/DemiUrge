// DEMIURGE — Sim/Core/Rng.h : the deterministic random number generator (DM.U.rng / Rng).
//
// mulberry32: the whole state is the single uint32 `s`, so it serializes trivially and assigning
// `s` restores the stream exactly. Every method reproduces the JS reference bit for bit.
//
//   Rng r(seed);  r.s                    state (read / write)
//   r.next()        -> uint32
//   r.f()           -> [0,1)             32 bits of resolution
//   r.range(a, b)   -> [a,b)             float
//   r.integer(a, b) -> [a,b]             both ends inclusive, either order   (JS: rng.int)
//   r.chance(p)     -> bool              always exactly one draw
//   r.pickIndex(n)  -> 0..n-1, -1 if n <= 0 (no draw)
//   r.pick(vec)     -> const T* (nullptr for an empty vector, no draw)
//   r.weightedIndex(n, wfn(i)) / r.weighted(vec, wfn(el)) -> index / const T*; weights <= 0 or NaN
//                    are never chosen; nothing positive -> -1 / nullptr and no draw; else one draw
//   r.gauss()       -> standard normal (Box-Muller, exactly two draws, no cached value)
//   r.shuffle(vec)  Fisher-Yates in place (n - 1 draws)
//   r.fork(tag)     -> Rng   independent stream from (state, tag); does NOT advance the parent.
//                    tag: an integer (taken as int32 like JS `tag | 0`) or a string.
//
// Simulation randomness comes ONLY from world.rng or a fork of it (rule SPEC-SIM 0.9). Use a
// string-keyed fork for anything that must not disturb the main stream, e.g.
// `Rng prose = w.rng.fork("chron:" + std::to_string(entryId));`
#pragma once

#include <cmath>
#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dm {

class Rng {
public:
	uint32_t s = 0;

	Rng() = default;
	explicit Rng(uint32_t seed) : s(seed) {}

	uint32_t next() {
		uint32_t t = s = s + 0x6D2B79F5u;
		t = (t ^ (t >> 15)) * (t | 1u);
		t ^= t + (t ^ (t >> 7)) * (t | 61u);
		return t ^ (t >> 14);
	}
	double f() { return double(next()) / 4294967296.0; }
	double range(double a, double b) {
		const double v = a + (b - a) * f();
		return (v >= b && b > a) ? a : v;
	}
	int integer(int a, int b) {
		if (b < a) { const int t = a; a = b; b = t; }
		return int(double(a) + std::floor(f() * (double(b) - double(a) + 1.0)));
	}
	bool chance(double p) { return f() < p; }
	int pickIndex(int n) { return n > 0 ? int(std::floor(f() * n)) : -1; }
	template <class T> const T* pick(const std::vector<T>& arr) {
		const int i = pickIndex(int(arr.size()));
		return i < 0 ? nullptr : &arr[size_t(i)];
	}
	template <class T> T* pick(std::vector<T>& arr) {
		const int i = pickIndex(int(arr.size()));
		return i < 0 ? nullptr : &arr[size_t(i)];
	}

	// wfn(i) -> weight of element i; called exactly once per element, in order.
	template <class WFn> int weightedIndex(int n, WFn&& wfn) {
		if (n <= 0) return -1;
		double local[64];
		std::vector<double> heap;
		double* w = local;
		if (n > 64) { heap.resize(size_t(n)); w = heap.data(); }
		for (int i = 0; i < n; i++) w[i] = double(wfn(i));
		return selectWeighted(w, n);
	}
	template <class T, class WFn> const T* weighted(const std::vector<T>& arr, WFn&& wfn) {
		const int i = weightedIndex(int(arr.size()), [&](int k) { return wfn(arr[size_t(k)]); });
		return i < 0 ? nullptr : &arr[size_t(i)];
	}
	// Choose an index with probability proportional to w[i] (sanitized in place: <= 0 / NaN -> 0;
	// if any weight is infinite, the infinite ones are chosen uniformly). One draw if any weight is
	// positive, else returns -1 with no draw.
	int selectWeighted(double* w, int n);
	// The pure selection step used by selectWeighted: u in [0,1) is the draw.
	static int selectWeightedWith(const double* w, int n, double total, double u);

	double gauss() { const uint32_t a = next(); const uint32_t b = next(); return gaussFrom(a, b); }
	// gauss() for chosen draws: radius from a (u = 1 - a / 2^32), angle from b (b / 2^32 turns).
	static double gaussFrom(uint32_t a, uint32_t b);

	template <class Vec> Vec& shuffle(Vec& arr) {
		for (int i = int(arr.size()) - 1; i > 0; i--) {
			const int j = int(std::floor(f() * (i + 1)));
			auto t = arr[size_t(i)]; arr[size_t(i)] = arr[size_t(j)]; arr[size_t(j)] = t;
		}
		return arr;
	}

	template <std::integral I> Rng fork(I tag) const { return forkInt(int64_t(tag)); }
	Rng forkInt(int64_t tag) const;                // tag taken modulo 2^32 (JS `tag | 0`)
	Rng fork(std::string_view tag) const;
	Rng fork(const std::string& tag) const { return fork(std::string_view(tag)); }
	Rng fork(const char* tag) const { return fork(std::string_view(tag)); }

	bool operator==(const Rng& o) const { return s == o.s; }
};

// Engine-independent ln(k / 2^32) for 1 <= k < 2^32 and cos(2 pi j / 2^32); used by gauss().
double lnUnit(uint32_t k);
double cosTurn(uint32_t j);

} // namespace dm
