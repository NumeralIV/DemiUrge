// DEMIURGE — Sim/Core/Rng.cpp : see Rng.h (port of the Rng class in js/core/util.js).
#include "Core/Rng.h"

#include <bit>
#include "Core/Util.h"

namespace dm {

namespace {
// ln(k / 2^32): k = m * 2^(31 - z), m in [1,2), halved above sqrt 2, ln m = 2 atanh(s).
constexpr double LN2_HI = 0.6931471803691238, LN2_LO = 1.9082149292705877e-10;
constexpr double A3 = 1.0 / 3, A5 = 1.0 / 5, A7 = 1.0 / 7, A9 = 1.0 / 9, A11 = 1.0 / 11, A13 = 1.0 / 13,
	A15 = 1.0 / 15, A17 = 1.0 / 17, A19 = 1.0 / 19, A21 = 1.0 / 21;
// cos(2 pi j / 2^32): quadrant from the top two bits, the rest folded into [0, pi/4].
constexpr double QUARTER = kPi / 2 / 1073741824.0;
constexpr double S3 = -1.0 / 6, S5 = 1.0 / 120, S7 = -1.0 / 5040, S9 = 1.0 / 362880, S11 = -1.0 / 39916800,
	S13 = 1.0 / 6227020800.0, S15 = -1.0 / 1307674368000.0, S17 = 1.0 / 355687428096000.0;
constexpr double C2 = -1.0 / 2, C4 = 1.0 / 24, C6 = -1.0 / 720, C8 = 1.0 / 40320, C10 = -1.0 / 3628800,
	C12 = 1.0 / 479001600, C14 = -1.0 / 87178291200.0, C16 = 1.0 / 20922789888000.0, C18 = -1.0 / 6402373705728000.0;
} // namespace

double lnUnit(uint32_t k) {
	const int z = std::countl_zero(k);
	double m = double(uint32_t(k << z)) / 2147483648.0;
	double e = double(-1 - z);
	if (m > 1.4142135623730951) { m *= 0.5; e++; }
	const double s = (m - 1) / (m + 1), w = s * s;
	const double p = (((((((((A21 * w + A19) * w + A17) * w + A15) * w + A13) * w + A11) * w + A9) * w + A7) * w + A5) * w + A3) * w + 1;
	return e * LN2_HI + (2 * s * p + e * LN2_LO);
}

double cosTurn(uint32_t j) {
	const uint32_t q = j >> 30;
	uint32_t r = j & 0x3FFFFFFFu;
	uint32_t useSin = q & 1u;
	if (r > 0x20000000u) { r = 0x40000000u - r; useSin ^= 1u; }
	const double a = double(r) * QUARTER, w = a * a;
	const double v = useSin
		? a * ((((((((S17 * w + S15) * w + S13) * w + S11) * w + S9) * w + S7) * w + S5) * w + S3) * w + 1)
		: ((((((((C18 * w + C16) * w + C14) * w + C12) * w + C10) * w + C8) * w + C6) * w + C4) * w + C2) * w + 1;
	return ((q + 1u) & 2u) ? -v : v;
}

double Rng::gaussFrom(uint32_t a, uint32_t b) {
	if (a == 0) return 0;
	return std::sqrt(-2 * lnUnit(0u - a)) * cosTurn(b);
}

int Rng::selectWeightedWith(const double* w, int n, double total, double u) {
	double r = u * total;
	int last = -1;
	for (int i = 0; i < n; i++) {
		const double wi = w[i];
		if (wi > 0) {
			last = i;
			if ((r -= wi) < 0) return i;
		}
	}
	return last;                                   // round-off fell off the end
}

int Rng::selectWeighted(double* w, int n) {
	double total = 0;
	for (int i = 0; i < n; i++) {
		if (!(w[i] > 0)) w[i] = 0;
		total += w[i];
	}
	if (!(total > 0)) return -1;
	if (std::isinf(total)) {
		// infinite weights: choose uniformly among them
		double inf = 0;
		for (int i = 0; i < n; i++) if (std::isinf(w[i])) inf += 1;
		if (inf > 0) {
			for (int i = 0; i < n; i++) w[i] = std::isinf(w[i]) ? 1.0 : 0.0;
			total = inf;
		} else {
			// finite weights whose sum overflowed: rescale by the largest one
			double mx = 0;
			for (int i = 0; i < n; i++) if (w[i] > mx) mx = w[i];
			total = 0;
			for (int i = 0; i < n; i++) { w[i] /= mx; total += w[i]; }
		}
	}
	return selectWeightedWith(w, n, total, f());
}

Rng Rng::forkInt(int64_t tag) const {
	const uint32_t t = uint32_t(tag);              // tag | 0
	return Rng(hash2(s ^ 0x2545F491u, t));
}

Rng Rng::fork(std::string_view tag) const {
	const uint32_t t = hashStr(tag) ^ 0x5F356495u;
	return Rng(hash2(s ^ 0x2545F491u, t));
}

} // namespace dm
