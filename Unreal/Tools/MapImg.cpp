// DEMIURGE — Unreal/Tools/MapImg.cpp : see MapImg.h.
#include "MapImg.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace dm::mapimg {

// ------------------------------------------------------------------ PNG

namespace {

uint32_t crcTable(int i) {
	uint32_t c = uint32_t(i);
	for (int k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
	return c;
}

struct Crc {
	uint32_t t[256];
	Crc() { for (int i = 0; i < 256; i++) t[i] = crcTable(i); }
	uint32_t of(const uint8_t* p, size_t n) const {
		uint32_t c = 0xffffffffu;
		for (size_t i = 0; i < n; i++) c = t[(c ^ p[i]) & 255] ^ (c >> 8);
		return c ^ 0xffffffffu;
	}
};

void be32(std::vector<uint8_t>& o, uint32_t v) {
	o.push_back(uint8_t(v >> 24)); o.push_back(uint8_t(v >> 16)); o.push_back(uint8_t(v >> 8)); o.push_back(uint8_t(v));
}

void chunk(std::vector<uint8_t>& out, const Crc& crc, const char* type, const std::vector<uint8_t>& data) {
	be32(out, uint32_t(data.size()));
	const size_t start = out.size();
	out.insert(out.end(), type, type + 4);
	out.insert(out.end(), data.begin(), data.end());
	be32(out, crc.of(out.data() + start, out.size() - start));
}

} // namespace

std::vector<uint8_t> encodePng(int w, int h, const std::vector<uint8_t>& rgb) {
	const Crc crc;
	const size_t row = size_t(w) * 3 + 1;
	std::vector<uint8_t> raw(row * size_t(h));
	for (int y = 0; y < h; y++) {
		raw[size_t(y) * row] = 0;                                  // filter: none
		std::copy(rgb.begin() + std::ptrdiff_t(size_t(y) * size_t(w) * 3), rgb.begin() + std::ptrdiff_t(size_t(y + 1) * size_t(w) * 3),
		          raw.begin() + std::ptrdiff_t(size_t(y) * row + 1));
	}
	// zlib stream of stored deflate blocks
	std::vector<uint8_t> z;
	z.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
	z.push_back(0x78); z.push_back(0x01);
	size_t pos = 0;
	do {
		const size_t len = std::min<size_t>(65535, raw.size() - pos);
		const bool last = pos + len >= raw.size();
		z.push_back(last ? 1 : 0);
		z.push_back(uint8_t(len)); z.push_back(uint8_t(len >> 8));
		z.push_back(uint8_t(~len)); z.push_back(uint8_t((~len) >> 8));
		z.insert(z.end(), raw.begin() + std::ptrdiff_t(pos), raw.begin() + std::ptrdiff_t(pos + len));
		pos += len;
	} while (pos < raw.size());
	uint32_t a = 1, b = 0;
	for (uint8_t v : raw) { a = (a + v) % 65521u; b = (b + a) % 65521u; }
	be32(z, (b << 16) | a);

	std::vector<uint8_t> out = { 137, 80, 78, 71, 13, 10, 26, 10 };
	std::vector<uint8_t> ihdr;
	be32(ihdr, uint32_t(w)); be32(ihdr, uint32_t(h));
	ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
	chunk(out, crc, "IHDR", ihdr);
	chunk(out, crc, "IDAT", z);
	chunk(out, crc, "IEND", {});
	return out;
}

bool writePng(const std::string& file, int w, int h, const std::vector<uint8_t>& rgb) {
	if (w <= 0 || h <= 0 || rgb.size() != size_t(w) * size_t(h) * 3) return false;
	const std::vector<uint8_t> png = encodePng(w, h, rgb);
	std::error_code ec;
	const std::filesystem::path p(file);
	if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
	std::ofstream f(p, std::ios::binary | std::ios::trunc);
	if (!f) return false;
	f.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
	return bool(f);
}

// ------------------------------------------------------------------ colours

namespace {
uint8_t to255(double v) { return v < 0 ? 0 : v > 255 ? 255 : uint8_t(v); }
Rgb mix(const Rgb& a, const Rgb& b, double t) {
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
}
const std::vector<std::pair<double, Rgb>> SEA_RAMP = { { 0, { 150, 215, 235 } }, { 0.6, { 60, 140, 200 } }, { 2.5, { 25, 70, 150 } }, { 6.5, { 8, 20, 70 } } };
const std::vector<std::pair<double, Rgb>> LAND_RAMP = { { 0, { 88, 150, 80 } }, { 0.6, { 140, 175, 95 } }, { 1.8, { 200, 190, 120 } },
	{ 3.5, { 150, 120, 95 } }, { 5.5, { 200, 200, 205 } }, { 8, { 255, 255, 255 } } };
const std::vector<std::pair<double, Rgb>> HEAT = { { 0, { 20, 30, 120 } }, { 0.25, { 40, 140, 210 } }, { 0.5, { 240, 235, 150 } },
	{ 0.75, { 235, 130, 50 } }, { 1, { 150, 20, 30 } } };
const Rgb LAKE = { 60, 120, 210 };
const Rgb RIVER = { 70, 150, 255 };
} // namespace

const Rgb kBiomeColors[Biome::COUNT] = {
	{ 20, 60, 140 }, { 70, 150, 200 }, { 230, 215, 160 }, { 225, 195, 120 }, { 190, 180, 90 }, { 130, 185, 90 }, { 45, 125, 60 }, { 15, 90, 45 },
	{ 60, 100, 85 }, { 160, 165, 140 }, { 240, 245, 250 }, { 120, 110, 105 }, { 255, 255, 255 }, { 70, 120, 110 }, { 70, 55, 50 }, { 200, 225, 240 },
};

Rgb ramp(const std::vector<std::pair<double, Rgb>>& stops, double t) {
	if (stops.empty()) return {};
	if (!(t > stops[0].first)) return stops[0].second;
	for (size_t i = 1; i < stops.size(); i++) {
		if (t <= stops[i].first) return mix(stops[i - 1].second, stops[i].second, (t - stops[i - 1].first) / (stops[i].first - stops[i - 1].first));
	}
	return stops.back().second;
}

Rgb heat(double t01) { return ramp(HEAT, t01); }

// ------------------------------------------------------------------ maps

bool custom(const World& world, const std::string& file, const std::function<Rgb(int)>& colorFn, const Opts& o) {
	const int W = o.w, H = o.h, n = world.n;
	const Geo& geo = *world.geo;
	std::vector<uint8_t> cols(size_t(n) * 3);
	for (int c = 0; c < n; c++) {
		const Rgb k = colorFn(c);
		cols[size_t(3 * c)] = to255(k.r); cols[size_t(3 * c + 1)] = to255(k.g); cols[size_t(3 * c + 2)] = to255(k.b);
	}
	if (o.rivers) {
		for (const River& r : world.rivers) {
			for (int32_t c : r.cells) {
				if (c < 0 || c >= n) continue;
				cols[size_t(3 * c)] = uint8_t(RIVER.r); cols[size_t(3 * c + 1)] = uint8_t(RIVER.g); cols[size_t(3 * c + 2)] = uint8_t(RIVER.b);
			}
		}
	}
	for (int c : o.marks) {
		if (c < 0 || c >= n) continue;
		cols[size_t(3 * c)] = 255; cols[size_t(3 * c + 1)] = 255; cols[size_t(3 * c + 2)] = 255;
	}
	std::vector<uint8_t> rgb(size_t(W) * size_t(H) * 3);
	int hint = -1;
	for (int y = 0; y < H; y++) {
		const double lat = (0.5 - (y + 0.5) / H) * kPi, cl = std::cos(lat), sl = std::sin(lat);
		for (int x = 0; x < W; x++) {
			const double lon = ((x + 0.5) / W - 0.5) * 2 * kPi;
			const int c = geo.cellAt(cl * std::cos(lon), sl, cl * std::sin(lon), hint);
			hint = c;
			const size_t px = (size_t(y) * size_t(W) + size_t(x)) * 3;
			for (int k = 0; k < 3; k++) rgb[px + size_t(k)] = cols[size_t(3 * c + k)];
			if (o.grid && (x % std::max(1, W / 12) == 0 || y % std::max(1, H / 6) == 0)) {
				for (int k = 0; k < 3; k++) rgb[px + size_t(k)] = uint8_t((rgb[px + size_t(k)] + 255) >> 1);
			}
		}
	}
	return writePng(file, W, H, rgb);
}

namespace {
bool isLake(const World& w, int c) { return w.elev[size_t(c)] >= w.seaLevel && w.water[size_t(c)] > 0.03f; }
} // namespace

bool elev(const World& world, const std::string& file, const Opts& o) {
	const double sea = world.seaLevel;
	return custom(world, file, [&](int c) {
		if (o.lakes && isLake(world, c)) return LAKE;
		const double e = world.elev[size_t(c)];
		return e < sea ? ramp(SEA_RAMP, sea - e) : ramp(LAND_RAMP, e - sea);
	}, o);
}

bool biome(const World& world, const std::string& file, const Opts& o) {
	return custom(world, file, [&](int c) {
		if (o.lakes && isLake(world, c)) return LAKE;
		const uint8_t b = world.biome[size_t(c)];
		return b < Biome::COUNT ? kBiomeColors[b] : Rgb{ 255, 0, 255 };
	}, o);
}

namespace {
template <class GetFn>
bool fieldImpl(const World& world, size_t count, GetFn get, const std::string& file, double mn, double mx, const Opts& o) {
	if (count != size_t(world.n)) return false;
	if (!std::isfinite(mn) || !std::isfinite(mx)) {
		double lo = std::numeric_limits<double>::infinity(), hi = -lo;
		for (size_t c = 0; c < count; c++) { const double v = get(c); if (v < lo) lo = v; if (v > hi) hi = v; }
		if (!std::isfinite(mn)) mn = std::isfinite(lo) ? lo : 0;
		if (!std::isfinite(mx)) mx = std::isfinite(hi) ? hi : 1;
	}
	const double span = (mx - mn) != 0 ? (mx - mn) : 1;
	const double sea = world.seaLevel;
	return custom(world, file, [&](int c) {
		const double v = get(size_t(c));
		Rgb k = std::isfinite(v) ? heat((v - mn) / span) : Rgb{ 255, 0, 255 };        // NaN in magenta
		if (o.dimSea && world.elev[size_t(c)] < sea) k = { k.r * 0.45, k.g * 0.45, k.b * 0.55 };
		return k;
	}, o);
}
} // namespace

bool field(const World& world, const std::string& name, const std::string& file, double mn, double mx, const Opts& o) {
	const int idx = state::fieldIndex(name);
	if (idx < 0) return false;
	const CellArray a = state::cellArray(world, idx);
	return fieldImpl(world, a.size, [&](size_t c) { return a.get(c); }, file, mn, mx, o);
}

bool field(const World& world, const std::vector<float>& values, const std::string& file, double mn, double mx, const Opts& o) {
	return fieldImpl(world, values.size(), [&](size_t c) { return double(values[c]); }, file, mn, mx, o);
}

bool field(const World& world, const std::vector<double>& values, const std::string& file, double mn, double mx, const Opts& o) {
	return fieldImpl(world, values.size(), [&](size_t c) { return values[c]; }, file, mn, mx, o);
}

bool owner(const World& world, const std::string& file, const Opts& o) {
	Opts oo = o;
	for (const Settlement& s : world.settlements) if (!s.dead) oo.marks.push_back(s.cell);
	const double sea = world.seaLevel;
	return custom(world, file, [&](int c) {
		if (world.elev[size_t(c)] < sea) return Rgb{ 18, 40, 90 };
		const int ow = world.owner[size_t(c)];
		if (ow < 0) return Rgb{ 70, 75, 70 };
		const Settlement* s = state::settlement(world, ow);
		if (!s) return Rgb{ 255, 0, 255 };
		const Culture* cu = state::culture(world, s->culture);
		const std::array<double, 3> col = cu ? cu->color : std::array<double, 3>{ 1, 1, 1 };
		const double f = 0.55 + 0.45 * std::min(1.0, double(world.farm[size_t(c)]) + world.urban[size_t(c)]);
		return Rgb{ col[0] * 255 * f, col[1] * 255 * f, col[2] * 255 * f };
	}, oo);
}

} // namespace dm::mapimg
