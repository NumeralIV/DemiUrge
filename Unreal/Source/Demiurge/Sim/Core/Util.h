// DEMIURGE — Sim/Core/Util.h : constants, scalar helpers, integer hashes, gradient noise, base64,
// number formatting, byte buffers. Port of js/core/util.js (DM.U, DM.R). Standard C++20 only.
//
//   dm::kPlanetR = 100, dm::kPi, dm::kTau
//   clamp sat lerp smooth sstep remap approach damp                 scalar helpers (double)
//   hash32(a) hash2(a, b) -> uint32     hashf(a, b = 0) -> [0,1)    integer hashes (bit-identical to JS)
//   hashStr(s) -> uint32                FNV-1a over the bytes of s (the hash rng.fork uses for string tags)
//   noise3(x, y, z) -> [-1,1]           smooth isotropic gradient noise (BCC lattice), seedless
//   fbm3(x, y, z, oct, lac = 2, gain = 0.5) -> [-1,1]
//   ridged3(x, y, z, oct, lac = 2, gain = 0.5) -> [0,1]
//   b64enc(bytes) -> string             b64dec(str, out) -> bool (false = "bad base64")
//   fmtInt(n) -> "12,345"               fmtBig(n) -> "950", "12.3k", "4.56M", "1.2B"
//   isFinite(x), toInt32(x)             JS-compatible helpers
//   ByteWriter / ByteReader             little-endian binary encoding (State::serialize, Save)
//
// Everything here except damp() is deterministic to the last bit on every IEEE-754 platform: only
// integer ops, + - * /, floor and sqrt are used (gauss in Rng.h carries its own ln / cos for the
// same reason). The hashes, noise3/fbm3/ridged3 and every Rng method reproduce the JS reference
// bit for bit (the C++ tests pin the same values as tests/util.test.js).
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace dm {

// ------------------------------------------------------------------ constants

inline constexpr double kPlanetR = 100.0;                      // DM.R: datum radius in world units
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTau = 6.28318530717958647692;         // DM.U.TAU

// ------------------------------------------------------------------ scalar helpers

inline double clamp(double x, double a, double b) { return x < a ? a : x > b ? b : x; }
inline double sat(double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
// Smoothstep of an input already in 0..1 (clamped).
inline double smooth(double t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return t * t * (3 - 2 * t); }
// GLSL smoothstep(e0, e1, x). A zero-width edge is a step; e0 > e1 gives the mirrored ramp.
double sstep(double e0, double e1, double x);
// Map x from a..b to c..d linearly, clamped to c..d. Works with a > b and/or c > d.
double remap(double x, double a, double b, double c, double d);
// Move x toward target by at most maxDelta (never overshoots).
double approach(double x, double target, double maxDelta);
// Frame-rate independent exponential approach (uses std::exp: for visual smoothing only).
double damp(double x, double target, double lambda, double dt);

// True for every value except NaN and +-infinity.
inline bool isFinite(double x) { return std::isfinite(x); }
// JavaScript ToInt32: truncate toward zero, wrap modulo 2^32; NaN and infinities give 0.
int32_t toInt32(double x);

// ------------------------------------------------------------------ hashes

// Bijective 32-bit mixer; hash32(0) != 0. Pass any int: it is taken modulo 2^32.
uint32_t hash32(uint32_t a);
// Hash of an ordered pair. For a fixed a it is a bijection of b, and vice versa.
uint32_t hash2(uint32_t a, uint32_t b);
// Float hash in [0,1) of a pair of ints.
double hashf(uint32_t a, uint32_t b = 0);
// 32-bit FNV-1a over the bytes of s (for ASCII identical to DM.U's hash over UTF-16 code units).
uint32_t hashStr(std::string_view s);

// ------------------------------------------------------------------ gradient noise

// Smooth gradient noise on a body-centred cubic lattice, kernel (3/4 - d^2)^4, 32 gradients.
// Seedless: offset the inputs to get different fields. |noise3| <= 0.99997, mean 0, sd ~0.35,
// features about one unit across. NaN input gives 0 (never NaN).
double noise3(double x, double y, double z);
// Fractal sum of `oct` octaves normalized by the amplitude sum, so the result stays in [-1,1].
double fbm3(double x, double y, double z, int oct, double lac = 2.0, double gain = 0.5);
// Ridged multifractal in [0,1]: octaves of (1 - |noise|)^2, each weighted by the previous one.
double ridged3(double x, double y, double z, int oct, double lac = 2.0, double gain = 0.5);

// ------------------------------------------------------------------ base64

// Standard base64 with padding.
std::string b64enc(const uint8_t* bytes, size_t n);
inline std::string b64enc(const std::vector<uint8_t>& bytes) { return b64enc(bytes.data(), bytes.size()); }
// Inverse of b64enc. Whitespace is skipped, padding is optional, the URL-safe alphabet (- _) is
// accepted. Returns false ("bad base64") on any other character, on data after the padding, or
// on an impossible length; out is then left empty.
bool b64dec(std::string_view str, std::vector<uint8_t>& out);

// ------------------------------------------------------------------ number formatting

// Rounded integer with thousands separators: 12345.4 -> "12,345", -1234.5 -> "-1,235".
std::string fmtInt(double n);
// Three significant digits with a k / M / B / T suffix: 950 -> "950", 12345 -> "12.3k".
std::string fmtBig(double n);

// ------------------------------------------------------------------ byte buffers

// Little-endian binary writer (independent of the host byte order).
class ByteWriter {
public:
	std::vector<uint8_t> buf;
	void u8(uint8_t v) { buf.push_back(v); }
	void u16(uint16_t v) { buf.push_back(uint8_t(v)); buf.push_back(uint8_t(v >> 8)); }
	void u32(uint32_t v) { for (int k = 0; k < 4; k++) buf.push_back(uint8_t(v >> (8 * k))); }
	void u64(uint64_t v) { for (int k = 0; k < 8; k++) buf.push_back(uint8_t(v >> (8 * k))); }
	void i16(int16_t v) { u16(uint16_t(v)); }
	void i32(int32_t v) { u32(uint32_t(v)); }
	void i64(int64_t v) { u64(uint64_t(v)); }
	void f32(float v) { uint32_t b; std::memcpy(&b, &v, 4); u32(b); }
	void f64(double v) { uint64_t b; std::memcpy(&b, &v, 8); u64(b); }
	void boolean(bool v) { u8(v ? 1 : 0); }
	void str(std::string_view s) { u32(uint32_t(s.size())); buf.insert(buf.end(), s.begin(), s.end()); }
	void bytes(const uint8_t* p, size_t n) { buf.insert(buf.end(), p, p + n); }
	size_t size() const { return buf.size(); }
};

// Little-endian reader with sticky failure: once a read runs past the end (or a length is
// absurd) every later read returns 0 / empty and ok() is false. Check ok() at the end.
class ByteReader {
public:
	ByteReader(const uint8_t* data, size_t size) : p_(data), n_(size) {}
	bool ok() const { return ok_; }
	bool atEnd() const { return pos_ == n_; }
	size_t pos() const { return pos_; }
	size_t remaining() const { return n_ - pos_; }
	void fail() { ok_ = false; pos_ = n_; }
	uint8_t u8();
	uint16_t u16();
	uint32_t u32();
	uint64_t u64();
	int16_t i16() { return int16_t(u16()); }
	int32_t i32() { return int32_t(u32()); }
	int64_t i64() { return int64_t(u64()); }
	float f32();
	double f64();
	bool boolean();                    // fails on any byte other than 0 or 1
	std::string str(size_t maxLen = 1u << 26);
	const uint8_t* take(size_t n);     // pointer to the next n bytes, or nullptr (and fail)
private:
	const uint8_t* p_;
	size_t n_;
	size_t pos_ = 0;
	bool ok_ = true;
};

} // namespace dm
