// DEMIURGE — Sim/Core/Util.cpp : see Util.h. A line-by-line port of js/core/util.js; the order of
// every floating-point operation is kept so results match the JS reference bit for bit.
#include "Core/Util.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>

namespace dm {

// ------------------------------------------------------------------ scalar helpers

double sstep(double e0, double e1, double x) {
	if (e0 == e1) return x < e0 ? 0.0 : 1.0;
	double t = (x - e0) / (e1 - e0);
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	return t * t * (3 - 2 * t);
}

double remap(double x, double a, double b, double c, double d) {
	if (a == b) return x < a ? c : d;
	double t = (x - a) / (b - a);
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	return c + (d - c) * t;
}

double approach(double x, double target, double maxDelta) {
	if (x < target) { x += maxDelta; return x > target ? target : x; }
	x -= maxDelta;
	return x < target ? target : x;
}

double damp(double x, double target, double lambda, double dt) {
	return target + (x - target) * std::exp(-lambda * dt);
}

int32_t toInt32(double x) {
	if (!std::isfinite(x)) return 0;
	double t = std::trunc(x);
	if (t >= -2147483648.0 && t <= 2147483647.0) return int32_t(t);
	double m = std::fmod(t, 4294967296.0);              // exact
	if (m < 0) m += 4294967296.0;
	return int32_t(uint32_t(m));
}

// ------------------------------------------------------------------ hashes

uint32_t hash32(uint32_t a) {
	a = a + 0x9E3779B9u;
	a ^= a >> 16; a *= 0x7FEB352Du;
	a ^= a >> 15; a *= 0x846CA68Bu;
	a ^= a >> 16;
	return a;
}

uint32_t hash2(uint32_t a, uint32_t b) { return hash32(hash32(a) ^ (b * 0x9E3779B1u)); }

double hashf(uint32_t a, uint32_t b) { return double(hash2(a, b)) / 4294967296.0; }

uint32_t hashStr(std::string_view s) {
	uint32_t h = 0x811C9DC5u;
	for (char ch : s) h = (h ^ uint32_t(uint8_t(ch))) * 0x01000193u;
	return h;
}

// ------------------------------------------------------------------ gradient noise

namespace {

constexpr uint32_t NPX = 0x9E3779B1u, NPY = 0x85EBCA77u, NPZ = 0xC2B2AE3Du;
constexpr double NOISE_SCALE = 11.524;
constexpr double OCT_X = 31.416, OCT_Y = -47.853, OCT_Z = 12.679;

struct GradTable { double g[32 * 4]; };

// 32 gradient directions: icosahedron + dodecahedron vertices. Built once, immutable afterwards.
const GradTable& grads() {
	static const GradTable table = [] {
		GradTable t{};
		const double phi = (1 + std::sqrt(5.0)) / 2, inv = 1 / phi;
		int n = 0;
		auto add = [&](double x, double y, double z) {
			const double l = std::sqrt(x * x + y * y + z * z);
			t.g[n] = x / l; t.g[n + 1] = y / l; t.g[n + 2] = z / l; t.g[n + 3] = 0;
			n += 4;
		};
		for (int a = -1; a <= 1; a += 2) {
			for (int b = -1; b <= 1; b += 2) {
				add(0, a, b * phi); add(a, b * phi, 0); add(b * phi, 0, a);
				add(0, a * inv, b * phi); add(a * inv, b * phi, 0); add(b * phi, 0, a * inv);
				add(a, b, 1); add(a, b, -1);
			}
		}
		return t;
	}();
	return table;
}

inline double corner(const double* G, double t, uint32_t h, double dx, double dy, double dz) {
	h = (h ^ (h >> 15)) * 0x2C1B3C6Du;
	h = (h ^ (h >> 12)) * 0x297A2D39u;
	const uint32_t g = (h >> 25) & 124u;
	t *= t;
	return t * t * (G[g] * dx + G[g + 1] * dy + G[g + 2] * dz);
}

} // namespace

double noise3(double x, double y, double z) {
	const double* G = grads().g;
	const double r = (x + y + z) * (2.0 / 3.0);
	const double xr = r - x, yr = r - y, zr = r - z;
	const double fi = std::floor(xr), fj = std::floor(yr), fk = std::floor(zr);
	const double fx = xr - fi, fy = yr - fj, fz = zr - fk;

	// (fx + 0.5) | 0 in JS: 0 or 1 for fx in [0,1), 0 for NaN
	const int hx = (fx + 0.5 >= 1.0) ? 1 : 0, hy = (fy + 0.5 >= 1.0) ? 1 : 0, hz = (fz + 0.5 >= 1.0) ? 1 : 0;
	const int sx = 1 - (hx << 1), sy = 1 - (hy << 1), sz = 1 - (hz << 1);
	const double dx = fx - hx, dy = fy - hy, dz = fz - hz;
	const double ex = fx - 0.5, ey = fy - 0.5, ez = fz - 0.5;
	const double ax = (dx < 0 ? -dx : dx) * 2, ay = (dy < 0 ? -dy : dy) * 2, az = (dz < 0 ? -dz : dz) * 2;

	const uint32_t i2 = uint32_t(toInt32(fi)) << 1, j2 = uint32_t(toInt32(fj)) << 1, k2 = uint32_t(toInt32(fk)) << 1;
	const uint32_t uhx = uint32_t(hx) << 1, uhy = uint32_t(hy) << 1, uhz = uint32_t(hz) << 1;
	double n = 0;

	// ---- lattice A: cell corners
	const uint32_t X0 = (i2 + uhx) * NPX, X1 = (i2 + 2u - uhx) * NPX;
	const uint32_t Y0 = (j2 + uhy) * NPY, Y1 = (j2 + 2u - uhy) * NPY;
	const uint32_t Z0 = (k2 + uhz) * NPZ, Z1 = (k2 + 2u - uhz) * NPZ;
	const double tA = 0.75 - dx * dx - dy * dy - dz * dz;
	if (tA > 0) n += corner(G, tA, X0 ^ Y0 ^ Z0, dx, dy, dz);
	const double tAx = tA - 1 + ax, tAy = tA - 1 + ay, tAz = tA - 1 + az;
	if (tAx > 0) {
		n += corner(G, tAx, X1 ^ Y0 ^ Z0, dx - sx, dy, dz);
		if (tAy > 0) { const double t = tAx - 1 + ay; if (t > 0) n += corner(G, t, X1 ^ Y1 ^ Z0, dx - sx, dy - sy, dz); }
		if (tAz > 0) { const double t = tAx - 1 + az; if (t > 0) n += corner(G, t, X1 ^ Y0 ^ Z1, dx - sx, dy, dz - sz); }
	}
	if (tAy > 0) {
		n += corner(G, tAy, X0 ^ Y1 ^ Z0, dx, dy - sy, dz);
		if (tAz > 0) { const double t = tAy - 1 + az; if (t > 0) n += corner(G, t, X0 ^ Y1 ^ Z1, dx, dy - sy, dz - sz); }
	}
	if (tAz > 0) n += corner(G, tAz, X0 ^ Y0 ^ Z1, dx, dy, dz - sz);

	// ---- lattice B: cell centres
	const uint32_t usx = uint32_t(sx * 2), usy = uint32_t(sy * 2), usz = uint32_t(sz * 2);
	const uint32_t BX0 = (i2 + 1u) * NPX, BX1 = (i2 + 1u - usx) * NPX;
	const uint32_t BY0 = (j2 + 1u) * NPY, BY1 = (j2 + 1u - usy) * NPY;
	const uint32_t BZ0 = (k2 + 1u) * NPZ, BZ1 = (k2 + 1u - usz) * NPZ;
	const double tB = 0.75 - ex * ex - ey * ey - ez * ez;
	if (tB > 0) n += corner(G, tB, BX0 ^ BY0 ^ BZ0, ex, ey, ez);
	const double tBx = tB - ax, tBy = tB - ay, tBz = tB - az;
	if (tBx > 0) {
		n += corner(G, tBx, BX1 ^ BY0 ^ BZ0, ex + sx, ey, ez);
		if (tBy > 0) { const double t = tBx - ay; if (t > 0) n += corner(G, t, BX1 ^ BY1 ^ BZ0, ex + sx, ey + sy, ez); }
		if (tBz > 0) { const double t = tBx - az; if (t > 0) n += corner(G, t, BX1 ^ BY0 ^ BZ1, ex + sx, ey, ez + sz); }
	}
	if (tBy > 0) {
		n += corner(G, tBy, BX0 ^ BY1 ^ BZ0, ex, ey + sy, ez);
		if (tBz > 0) { const double t = tBy - az; if (t > 0) n += corner(G, t, BX0 ^ BY1 ^ BZ1, ex, ey + sy, ez + sz); }
	}
	if (tBz > 0) n += corner(G, tBz, BX0 ^ BY0 ^ BZ1, ex, ey, ez + sz);

	return NOISE_SCALE * n;
}

double fbm3(double x, double y, double z, int oct, double lac, double gain) {
	double sum = 0, norm = 0, amp = 1, f = 1;
	for (int o = 0; o < oct; o++) {
		sum += amp * noise3(x * f + o * OCT_X, y * f + o * OCT_Y, z * f + o * OCT_Z);
		norm += amp;
		amp *= gain;
		f *= lac;
	}
	return norm > 0 ? sum / norm : 0;
}

double ridged3(double x, double y, double z, int oct, double lac, double gain) {
	double sum = 0, norm = 0, amp = 1, f = 1, w = 1;
	for (int o = 0; o < oct; o++) {
		double v = noise3(x * f + o * OCT_X, y * f + o * OCT_Y, z * f + o * OCT_Z);
		v = 1 - (v < 0 ? -v : v);
		v *= v;
		sum += amp * w * v;
		norm += amp;
		w = v * 2;
		if (w > 1) w = 1;
		amp *= gain;
		f *= lac;
	}
	return norm > 0 ? sum / norm : 0;
}

// ------------------------------------------------------------------ base64

namespace {
constexpr char B64_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
// -1 invalid, -2 whitespace, -3 padding
constexpr std::array<int8_t, 128> makeB64Dec() {
	std::array<int8_t, 128> d{};
	for (auto& v : d) v = -1;
	for (int i = 0; i < 64; i++) d[size_t(uint8_t(B64_CHARS[i]))] = int8_t(i);
	d[45] = 62; d[95] = 63;
	d[9] = d[10] = d[13] = d[32] = -2;
	d[61] = -3;
	return d;
}
constexpr std::array<int8_t, 128> B64_DEC = makeB64Dec();
} // namespace

std::string b64enc(const uint8_t* bytes, size_t n) {
	std::string out;
	out.resize((n + 2) / 3 * 4);
	const size_t full = n - n % 3;
	size_t o = 0, i = 0;
	for (; i < full; i += 3) {
		const uint32_t v = (uint32_t(bytes[i]) << 16) | (uint32_t(bytes[i + 1]) << 8) | bytes[i + 2];
		out[o] = B64_CHARS[v >> 18];
		out[o + 1] = B64_CHARS[(v >> 12) & 63];
		out[o + 2] = B64_CHARS[(v >> 6) & 63];
		out[o + 3] = B64_CHARS[v & 63];
		o += 4;
	}
	if (n - full == 1) {
		const uint32_t v = bytes[i];
		out[o] = B64_CHARS[v >> 2]; out[o + 1] = B64_CHARS[(v & 3) << 4]; out[o + 2] = '='; out[o + 3] = '=';
	} else if (n - full == 2) {
		const uint32_t v = (uint32_t(bytes[i]) << 8) | bytes[i + 1];
		out[o] = B64_CHARS[v >> 10]; out[o + 1] = B64_CHARS[(v >> 4) & 63]; out[o + 2] = B64_CHARS[(v & 15) << 2]; out[o + 3] = '=';
	}
	return out;
}

bool b64dec(std::string_view str, std::vector<uint8_t>& out) {
	out.clear();
	out.reserve(str.size() / 4 * 3 + 3);
	uint32_t acc = 0;
	int have = 0;
	bool padded = false;
	for (char ch : str) {
		const uint8_t code = uint8_t(ch);
		const int v = code < 128 ? B64_DEC[code] : -1;
		if (v == -2) continue;
		if (v == -3) { padded = true; continue; }
		if (v < 0 || padded) { out.clear(); return false; }
		acc = (acc << 6) | uint32_t(v);
		if (++have == 4) {
			out.push_back(uint8_t(acc >> 16)); out.push_back(uint8_t(acc >> 8)); out.push_back(uint8_t(acc));
			acc = 0; have = 0;
		}
	}
	if (have == 1) { out.clear(); return false; }
	if (have == 2) out.push_back(uint8_t(acc >> 4));
	else if (have == 3) { out.push_back(uint8_t(acc >> 10)); out.push_back(uint8_t(acc >> 2)); }
	return true;
}

// ------------------------------------------------------------------ number formatting

namespace {
std::string nonFinite(double n) { return std::isnan(n) ? "NaN" : n > 0 ? "Infinity" : "-Infinity"; }

// JS String(r) for a non-negative integer-valued double: plain digits below 1e21, else exponent form.
std::string jsIntString(double r) {
	char buf[64];
	auto res = std::to_chars(buf, buf + sizeof buf, r, std::chars_format::scientific);
	const std::string s(buf, res.ptr);           // shortest round-trip digits: "1.2345678901234568e+20"
	if (r >= 1e21) return s;                     // "1e+21": JS writes the same
	const size_t e = s.find('e');
	std::string digits;
	for (size_t i = 0; i < e; i++) if (s[i] != '.') digits.push_back(s[i]);
	const int exp10 = std::atoi(s.c_str() + e + 1);
	while (int(digits.size()) < exp10 + 1) digits.push_back('0');   // JS pads the shortest digits with zeros
	digits.resize(size_t(exp10 + 1));
	return digits;
}
} // namespace

std::string fmtInt(double n) {
	if (!std::isfinite(n)) return nonFinite(n);
	const double r = std::round(std::fabs(n));
	if (r == 0) return "0";
	const std::string s = jsIntString(r);
	const char* sign = n < 0 ? "-" : "";
	if (s.find('e') != std::string::npos) return sign + s;
	std::string out;
	const size_t len = s.size();
	for (size_t i = 0; i < len; i++) {
		if (i > 0 && (len - i) % 3 == 0) out.push_back(',');
		out.push_back(s[i]);
	}
	return sign + out;
}

std::string fmtBig(double n) {
	static constexpr const char* SUFFIX[] = { "k", "M", "B", "T" };
	if (!std::isfinite(n)) return nonFinite(n);
	const std::string sign = n < 0 ? "-" : "";
	double a = std::fabs(n);
	if (a < 999.5) {
		const double r = std::round(a);
		return r == 0 ? std::string("0") : sign + jsIntString(r);
	}
	int u = -1;
	while (a >= 999.5 && u < 3) { a /= 1000; u++; }
	if (a >= 999.5) return sign + fmtInt(a) + SUFFIX[u];
	const int prec = a < 9.995 ? 2 : a < 99.95 ? 1 : 0;
	char buf[64];
	auto res = std::to_chars(buf, buf + sizeof buf, a, std::chars_format::fixed, prec);
	std::string s(buf, res.ptr);
	if (s.find('.') != std::string::npos) {
		while (!s.empty() && s.back() == '0') s.pop_back();
		if (!s.empty() && s.back() == '.') s.pop_back();
	}
	return sign + s + SUFFIX[u];
}

// ------------------------------------------------------------------ ByteReader

const uint8_t* ByteReader::take(size_t n) {
	if (!ok_ || n > n_ - pos_) { fail(); return nullptr; }
	const uint8_t* q = p_ + pos_;
	pos_ += n;
	return q;
}
uint8_t ByteReader::u8() { const uint8_t* q = take(1); return q ? q[0] : 0; }
uint16_t ByteReader::u16() { const uint8_t* q = take(2); return q ? uint16_t(q[0] | (q[1] << 8)) : 0; }
uint32_t ByteReader::u32() {
	const uint8_t* q = take(4);
	return q ? uint32_t(q[0]) | (uint32_t(q[1]) << 8) | (uint32_t(q[2]) << 16) | (uint32_t(q[3]) << 24) : 0;
}
uint64_t ByteReader::u64() {
	const uint8_t* q = take(8);
	if (!q) return 0;
	uint64_t v = 0;
	for (int k = 7; k >= 0; k--) v = (v << 8) | q[k];
	return v;
}
float ByteReader::f32() { const uint32_t b = u32(); float v; std::memcpy(&v, &b, 4); return v; }
double ByteReader::f64() { const uint64_t b = u64(); double v; std::memcpy(&v, &b, 8); return v; }
bool ByteReader::boolean() {
	const uint8_t v = u8();
	if (v > 1) fail();
	return v == 1;
}
std::string ByteReader::str(size_t maxLen) {
	const uint32_t len = u32();
	if (!ok_ || len > maxLen) { fail(); return std::string(); }
	const uint8_t* q = take(len);
	return q ? std::string(reinterpret_cast<const char*>(q), len) : std::string();
}

} // namespace dm
