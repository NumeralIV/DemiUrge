// DEMIURGE — Sim/Save.cpp : the save container, the slots and the settings (see Save.h).
// Owner: save engineer (A15, key "save").
//
// Container, version 1 (little-endian; str = u32 length + bytes):
//   'D' 'M' 'S' 'V'     u32 container version (1)     u32 flags (bit 0: snapshots present)
//   u64 total bytes of the file, checksum included
//   meta:               str name, u32 seed, f64 year, u8 phase, i32 age, f64 pop, i64 savedAt
//   u32 byte count, then the state::serialize blob (its layout belongs to State.cpp)
//   [bit 0] u32 count, then per snapshot: f64 year, f64 heat, f64 seaLevel, f64 pop, and three
//           arrays of n cells (biome u8, owner i8, urban u8), each stored as its byte-wise XOR with
//           the same array of the previous frame (the first frame: as is), as runs: u32 runs, then
//           per run u8 value and u16 length (>= 1); the lengths of each array add up to exactly n
//   u32 CRC-32 (IEEE 802.3, reflected, as zlib) of every byte before it
//
// encode() decodes its own bytes before returning them, so a container that cannot be loaded is
// never produced. Files are written to "<file>.tmp" first and renamed over the target, so a failed
// write keeps the old file. Settings are text: one "key=value" per line, '#' starts a comment line,
// unknown keys are ignored; flashes and shake are written only when they are not 1 (see saveSettings). Paths are UTF-8; on Windows they are converted to UTF-16 here (no char8_t, no
// deprecated std::filesystem::u8path).
#include "Save.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

namespace dm::save {

namespace {

// ------------------------------------------------------------------ constants

constexpr uint32_t kFlagSnapshots = 1u << 0;
constexpr uint32_t kKnownFlags = kFlagSnapshots;
constexpr size_t kPrefixBytes = 4 + 4 + 4 + 8;         // magic, container version, flags, total size
constexpr size_t kTrailerBytes = 4;                    // CRC-32 of everything before it
constexpr size_t kMaxNameBytes = size_t(1) << 16;      // a world name longer than this is refused
constexpr size_t kMaxFileBytes = size_t(1) << 30;      // larger save files are refused, not read
constexpr size_t kMaxSettingsBytes = size_t(1) << 16;  // a settings file is a few hundred bytes
constexpr size_t kMinSnapshotBytes = 4 * 8 + 3 * 4;    // four f64 and three empty run lists
constexpr size_t kMaxSlotName = 64;

// Reflected CRC-32 table, built at compile time (a constant, not state).
constexpr std::array<uint32_t, 256> makeCrcTable() {
	std::array<uint32_t, 256> t{};
	for (uint32_t i = 0; i < 256; i++) {
		uint32_t c = i;
		for (int k = 0; k < 8; k++) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
		t[i] = c;
	}
	return t;
}
constexpr std::array<uint32_t, 256> kCrcTable = makeCrcTable();

uint32_t crc32(const uint8_t* p, size_t n) {
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i++) c = kCrcTable[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

uint32_t le32(const uint8_t* p) {
	return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint64_t le64(const uint8_t* p) { return uint64_t(le32(p)) | (uint64_t(le32(p + 4)) << 32); }
void patch64(std::vector<uint8_t>& v, size_t at, uint64_t x) {
	for (size_t k = 0; k < 8; k++) v[at + k] = uint8_t(x >> (8 * k));
}

// Error text: internal helpers return false and set `why`; the public functions copy it out.
bool setWhy(std::string& why, std::string text) {
	why = std::move(text);
	return false;
}
void report(std::string* why, const std::string& text) {
	if (why) *why = text;
}
double zeroIfNotFinite(double v) { return std::isfinite(v) ? v : 0.0; }

// ------------------------------------------------------------------ UTF-8 paths

#if defined(_WIN32)
bool utf8ToWide(const std::string& s, std::wstring& out) {
	out.clear();
	size_t i = 0;
	while (i < s.size()) {
		const uint8_t c = uint8_t(s[i]);
		uint32_t cp = 0;
		size_t more = 0;
		if (c < 0x80) cp = c;
		else if (c >= 0xC2 && c <= 0xDF) { cp = c & 0x1Fu; more = 1; }
		else if (c >= 0xE0 && c <= 0xEF) { cp = c & 0x0Fu; more = 2; }
		else if (c >= 0xF0 && c <= 0xF4) { cp = c & 0x07u; more = 3; }
		else return false;
		if (s.size() - i - 1 < more) return false;
		for (size_t k = 1; k <= more; k++) {
			const uint8_t d = uint8_t(s[i + k]);
			if ((d & 0xC0u) != 0x80u) return false;
			cp = (cp << 6) | (d & 0x3Fu);
		}
		if ((more == 2 && cp < 0x800) || (more == 3 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
		if (cp >= 0x10000) {
			cp -= 0x10000;
			out.push_back(wchar_t(0xD800 + (cp >> 10)));
			out.push_back(wchar_t(0xDC00 + (cp & 0x3FFu)));
		} else {
			out.push_back(wchar_t(cp));
		}
		i += more + 1;
	}
	return true;
}

std::string wideToUtf8(const std::wstring& w) {
	std::string out;
	for (size_t i = 0; i < w.size(); i++) {
		uint32_t cp = uint32_t(w[i]);
		if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size()) {
			const uint32_t lo = uint32_t(w[i + 1]);
			if (lo >= 0xDC00 && lo <= 0xDFFF) {
				cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
				i++;
			}
		}
		if (cp < 0x80) {
			out += static_cast<char>(cp);
		} else if (cp < 0x800) {
			out += static_cast<char>(0xC0 | (cp >> 6));
			out += static_cast<char>(0x80 | (cp & 0x3Fu));
		} else if (cp < 0x10000) {
			out += static_cast<char>(0xE0 | (cp >> 12));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3Fu));
			out += static_cast<char>(0x80 | (cp & 0x3Fu));
		} else {
			out += static_cast<char>(0xF0 | (cp >> 18));
			out += static_cast<char>(0x80 | ((cp >> 12) & 0x3Fu));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3Fu));
			out += static_cast<char>(0x80 | (cp & 0x3Fu));
		}
	}
	return out;
}
#endif

// An empty path or one holding a NUL byte is refused: the OS would silently cut the name at the NUL
// (writing "dir/a" for "dir/a\0b.dmsave"), and an empty folder would mean the working directory.
bool toPath(const std::string& utf8, std::filesystem::path& out) {
	if (utf8.empty() || utf8.find('\0') != std::string::npos) return false;
#if defined(_WIN32)
	std::wstring w;
	if (!utf8ToWide(utf8, w)) return false;
	out = std::filesystem::path(w);
#else
	out = std::filesystem::path(utf8);
#endif
	return true;
}

std::string fromPath(const std::filesystem::path& p) {
#if defined(_WIN32)
	return wideToUtf8(p.native());
#else
	return p.string();
#endif
}

bool endsWith(const std::string& s, const char* suffix) {
	const size_t k = std::strlen(suffix);
	return s.size() >= k && s.compare(s.size() - k, k, suffix) == 0;
}

// ------------------------------------------------------------------ file I/O

bool readWhole(const std::string& path, size_t maxBytes, std::vector<uint8_t>& out, std::string& why) {
	out.clear();
	std::filesystem::path p;
	if (path.empty() || !toPath(path, p)) return setWhy(why, "save: bad path");
	std::error_code ec;
	if (!std::filesystem::is_regular_file(p, ec)) return setWhy(why, "save: no file at " + path);
	const uintmax_t size = std::filesystem::file_size(p, ec);
	if (ec || size > uintmax_t(maxBytes)) return setWhy(why, "save: cannot read " + path);
	std::ifstream f(p, std::ios::binary);
	if (!f) return setWhy(why, "save: cannot open " + path);
	out.resize(size_t(size));
	if (size > 0) {
		f.read(reinterpret_cast<char*>(out.data()), std::streamsize(size));
		if (f.gcount() != std::streamsize(size)) {
			out.clear();
			return setWhy(why, "save: short read of " + path);
		}
	}
	return true;
}

// Writes to "<path>.tmp" and renames it over <path>: a crash or a failed write never leaves a
// half-written file under the real name, and a failed write keeps the previous file.
bool writeAtomic(const std::string& path, const std::vector<uint8_t>& bytes, std::string& why) {
	std::filesystem::path target;
	if (path.empty() || !toPath(path, target)) return setWhy(why, "save: bad path");
	std::error_code ec;
	if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), ec);   // a failure shows in the open
	std::filesystem::path tmp = target;
	tmp += ".tmp";
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		if (!f) return setWhy(why, "save: cannot write " + path);
		f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
		f.close();
		if (f.fail()) {
			std::filesystem::remove(tmp, ec);
			return setWhy(why, "save: write failed for " + path);
		}
	}
	std::filesystem::rename(tmp, target, ec);
	if (ec) {
		std::filesystem::remove(tmp, ec);
		return setWhy(why, "save: could not replace " + path);
	}
	return true;
}

// ------------------------------------------------------------------ meta

void putMeta(ByteWriter& o, const Meta& m) {
	o.str(m.name);
	o.u32(m.seed);
	o.f64(m.year);
	o.u8(uint8_t(m.phase));
	o.i32(int32_t(m.age));
	o.f64(m.pop);
	o.i64(m.savedAt);
}

bool getMeta(ByteReader& r, Meta& m) {
	m.name = r.str(kMaxNameBytes);
	m.seed = r.u32();
	m.year = r.f64();
	const uint8_t ph = r.u8();
	m.age = int(r.i32());
	m.pop = r.f64();
	m.savedAt = r.i64();
	if (ph < uint8_t(enumCount<Phase>())) m.phase = Phase(ph);
	else r.fail();
	if (!std::isfinite(m.year) || !std::isfinite(m.pop) || m.age < 0 || m.age >= int(Age::COUNT)) r.fail();
	return r.ok();
}

// Everything the header says about the world must equal what the state blob holds.
bool sameMeta(const Meta& a, const Meta& b) {
	return a.name == b.name && a.seed == b.seed && a.year == b.year && a.phase == b.phase && a.age == b.age && a.pop == b.pop;
}

// ------------------------------------------------------------------ snapshots

// Every snapshot array has n cells, each biome is a Biome id and each owner names a culture (or -1).
bool snapshotsFit(const World& w, std::string& why) {
	const size_t n = size_t(w.n);
	const int nc = int(w.cultures.size());
	for (size_t i = 0; i < w.snapshots.size(); i++) {
		const Snapshot& s = w.snapshots[i];
		const std::string where = "save: snapshot " + std::to_string(i);
		if (s.biome.size() != n || s.owner.size() != n || s.urban.size() != n) return setWhy(why, where + " has arrays of the wrong length");
		for (uint8_t b : s.biome) if (int(b) >= int(Biome::COUNT)) return setWhy(why, where + " has a biome out of range");
		for (int8_t o : s.owner) if (int(o) < -1 || int(o) >= nc) return setWhy(why, where + " names a culture that does not exist");
	}
	return true;
}

// Runs of equal values: u32 runs, then per run u8 value, u16 length. Lengths are at least 1.
void putRuns(ByteWriter& o, const uint8_t* v, size_t n) {
	size_t count = 0;
	for (size_t i = 0; i < n; count++) {
		size_t j = i + 1;
		while (j < n && v[j] == v[i] && j - i < 65535) j++;
		i = j;
	}
	o.u32(uint32_t(count));
	for (size_t i = 0; i < n;) {
		size_t j = i + 1;
		while (j < n && v[j] == v[i] && j - i < 65535) j++;
		o.u8(v[i]);
		o.u16(uint16_t(j - i));
		i = j;
	}
}

// Frames are stored as the difference to the frame before (byte XOR; the first frame against
// zeros), then as runs: consecutive frames differ in a few hundred cells, so this is about 15 times
// smaller than runs of the frames themselves (measured on a real L 6 history).
void putDelta(ByteWriter& o, const uint8_t* v, const uint8_t* prev, size_t n, std::vector<uint8_t>& tmp) {
	tmp.assign(v, v + n);
	if (prev) for (size_t c = 0; c < n; c++) tmp[c] ^= prev[c];
	putRuns(o, tmp.data(), n);
}

void putSnapshots(ByteWriter& o, const World& w) {
	o.u32(uint32_t(w.snapshots.size()));
	std::vector<uint8_t> tmp;
	for (size_t i = 0; i < w.snapshots.size(); i++) {
		const Snapshot& s = w.snapshots[i];
		const Snapshot* p = i > 0 ? &w.snapshots[i - 1] : nullptr;
		o.f64(zeroIfNotFinite(s.year));
		o.f64(zeroIfNotFinite(s.heat));
		o.f64(zeroIfNotFinite(s.seaLevel));
		o.f64(zeroIfNotFinite(s.pop));
		const size_t n = s.biome.size();
		putDelta(o, s.biome.data(), p ? p->biome.data() : nullptr, n, tmp);
		putDelta(o, reinterpret_cast<const uint8_t*>(s.owner.data()), p ? reinterpret_cast<const uint8_t*>(p->owner.data()) : nullptr, n, tmp);
		putDelta(o, s.urban.data(), p ? p->urban.data() : nullptr, n, tmp);
	}
}

// Reads one run list of exactly n cells; anything else is an error.
bool getRuns(ByteReader& r, size_t n, std::vector<uint8_t>& out, std::string& why) {
	const uint32_t runs = r.u32();
	if (!r.ok() || size_t(runs) > r.remaining() / 3) return setWhy(why, "save: bad snapshot runs");
	out.assign(n, 0);
	size_t pos = 0;
	for (uint32_t k = 0; k < runs; k++) {
		const uint8_t v = r.u8();
		const uint16_t len = r.u16();
		if (!r.ok() || len == 0 || size_t(len) > n - pos) return setWhy(why, "save: snapshot runs do not fit the map");
		std::fill_n(out.data() + pos, len, v);
		pos += len;
	}
	if (pos != n) return setWhy(why, "save: snapshot runs do not cover the map");
	return true;
}

// One delta-coded array: runs, then XOR with the previous frame's array (if any).
bool getDelta(ByteReader& r, size_t n, const uint8_t* prev, std::vector<uint8_t>& out, std::string& why) {
	if (!getRuns(r, n, out, why)) return false;
	if (prev) for (size_t c = 0; c < n; c++) out[c] ^= prev[c];
	return true;
}

bool getSnapshots(ByteReader& r, World& w, std::string& why) {
	const uint32_t count = r.u32();
	if (!r.ok() || size_t(count) > r.remaining() / kMinSnapshotBytes) return setWhy(why, "save: bad snapshot count");
	const size_t n = size_t(w.n);
	const int nc = int(w.cultures.size());
	w.snapshots.clear();
	w.snapshots.reserve(count);
	std::vector<uint8_t> owner, prevOwner;
	for (uint32_t i = 0; i < count; i++) {
		Snapshot s;
		s.year = r.f64();
		s.heat = r.f64();
		s.seaLevel = r.f64();
		s.pop = r.f64();
		if (!r.ok()) return setWhy(why, "save: truncated snapshot");
		if (!std::isfinite(s.year) || !std::isfinite(s.heat) || !std::isfinite(s.seaLevel) || !std::isfinite(s.pop))
			return setWhy(why, "save: snapshot with a number that is not finite");
		const Snapshot* p = i > 0 ? &w.snapshots.back() : nullptr;
		if (!getDelta(r, n, p ? p->biome.data() : nullptr, s.biome, why) || !getDelta(r, n, p ? prevOwner.data() : nullptr, owner, why) ||
			!getDelta(r, n, p ? p->urban.data() : nullptr, s.urban, why))
			return false;
		s.owner.resize(n);
		for (size_t c = 0; c < n; c++) {
			if (int(s.biome[c]) >= int(Biome::COUNT)) return setWhy(why, "save: snapshot biome out of range");
			const int o = int(int8_t(owner[c]));
			if (o < -1 || o >= nc) return setWhy(why, "save: snapshot owner names no culture");
			s.owner[c] = int8_t(o);
		}
		prevOwner.swap(owner);
		w.snapshots.push_back(std::move(s));
	}
	return true;
}

// ------------------------------------------------------------------ envelope

// Magic, container version, known flags, total size and checksum. Runs before anything is parsed.
bool checkEnvelope(const uint8_t* d, size_t n, std::string& why) {
	if (!d || n < kPrefixBytes + kTrailerBytes) return setWhy(why, "save: too short to be a save");
	if (std::memcmp(d, kMagic, 4) != 0) return setWhy(why, "save: not a DEMIURGE save (bad magic)");
	const uint32_t version = le32(d + 4);
	if (version != kVersion)
		return setWhy(why, "save: unsupported version " + std::to_string(version) + " (this build reads " + std::to_string(kVersion) + ")");
	if ((le32(d + 8) & ~kKnownFlags) != 0) return setWhy(why, "save: unknown flags");
	if (le64(d + 12) != uint64_t(n)) return setWhy(why, "save: size mismatch (truncated or trailing bytes)");
	if (le32(d + n - kTrailerBytes) != crc32(d, n - kTrailerBytes)) return setWhy(why, "save: checksum mismatch (corrupt file)");
	return true;
}

// ------------------------------------------------------------------ settings text

std::string_view trimmed(std::string_view s) {
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
	return s;
}

bool parseDouble(std::string_view s, double& out) {
	if (s.empty()) return false;
	double v = 0;
	const char* last = s.data() + s.size();
	const std::from_chars_result res = std::from_chars(s.data(), last, v);
	if (res.ec != std::errc() || res.ptr != last || !std::isfinite(v)) return false;
	out = v;
	return true;
}

bool parseInt(std::string_view s, int& out) {
	if (s.empty()) return false;
	int v = 0;
	const char* last = s.data() + s.size();
	const std::from_chars_result res = std::from_chars(s.data(), last, v);
	if (res.ec != std::errc() || res.ptr != last) return false;
	out = v;
	return true;
}

bool parseBool(std::string_view s, bool& out) {
	if (s == "1" || s == "true") { out = true; return true; }
	if (s == "0" || s == "false") { out = false; return true; }
	return false;
}

// Shortest text that reads back as the same double; "" (the line is left out) for NaN / infinity.
std::string numText(double v) {
	if (!std::isfinite(v)) return std::string();
	char buf[64];
	const std::to_chars_result r = std::to_chars(buf, buf + sizeof(buf), v);
	return std::string(buf, r.ptr);
}

// Each value is checked and clamped to its range; a garbage value leaves the default in place.
void applySetting(UserSettings& s, std::string_view key, std::string_view val) {
	double d = 0;
	int i = 0;
	bool b = false;
	auto unit = [&](double& dst) {
		if (parseDouble(val, d)) dst = dm::clamp(d, 0.0, 1.0);
	};
	if (key == "master") unit(s.master);
	else if (key == "music") unit(s.music);
	else if (key == "sfx") unit(s.sfx);
	else if (key == "ambience") unit(s.ambience);
	else if (key == "tilt") unit(s.tilt);
	else if (key == "flashes") unit(s.flashes);
	else if (key == "shake") unit(s.shake);
	else if (key == "uiScale") { if (parseDouble(val, d)) s.uiScale = dm::clamp(d, 0.5, 2.0); }
	else if (key == "quality") { if (parseInt(val, i) && i >= -1 && i <= 2) s.quality = i; }
	else if (key == "sun") { if (val == "orbit" || val == "follow") s.sun = std::string(val); }
	else if (key == "labels") { if (parseBool(val, b)) s.labels = b; }
	else if (key == "hints") { if (parseBool(val, b)) s.hints = b; }
	else if (key == "autoSlow") { if (parseBool(val, b)) s.autoSlow = b; }
}

void addLine(std::string& text, const char* key, const std::string& value) {
	// A value that could not be read back (a line break, NaN, infinity) is left out, so the key keeps
	// its default on load and no other line can be injected through it.
	if (value.empty() || value.find_first_of("\r\n") != std::string::npos) return;
	text += key;
	text += '=';
	text += value;
	text += '\n';
}

// ------------------------------------------------------------------ public API: container

} // namespace

Meta metaOf(const World& w, int64_t savedAt) {
	Meta m;
	m.name = w.name;
	m.seed = w.seed;
	m.year = w.year;
	m.phase = w.phase;
	m.age = w.age;
	m.pop = w.stats.pop;
	m.savedAt = savedAt;
	return m;
}

bool encode(const World& w, std::vector<uint8_t>& out, const EncodeOpts& opts, std::string* why) {
	out.clear();
	std::string err;
	if (w.name.size() > kMaxNameBytes) {
		report(why, "save: the world name is longer than 65536 bytes");
		return false;
	}
	std::vector<uint8_t> blob;
	if (!state::serialize(w, blob, &err)) {
		report(why, "save: " + err);
		return false;
	}
	const bool snaps = opts.snapshots;
	if (snaps && !snapshotsFit(w, err)) {
		report(why, err);
		return false;
	}
	ByteWriter o;
	o.bytes(reinterpret_cast<const uint8_t*>(kMagic), 4);
	o.u32(kVersion);
	o.u32(snaps ? kFlagSnapshots : 0u);
	o.u64(0);                                              // total size, patched below
	putMeta(o, metaOf(w, opts.savedAt));
	o.u32(uint32_t(blob.size()));
	o.bytes(blob.data(), blob.size());
	if (snaps) putSnapshots(o, w);
	patch64(o.buf, 12, uint64_t(o.size() + kTrailerBytes));
	o.u32(crc32(o.buf.data(), o.buf.size()));
	// Never write a save that would not load: read our own bytes back before returning them.
	std::unique_ptr<World> readBack = decode(o.buf.data(), o.buf.size(), &err);
	if (!readBack) {
		report(why, "save: the world would not read back (" + err + ")");
		return false;
	}
	out = std::move(o.buf);
	return true;
}

std::unique_ptr<World> decode(const uint8_t* data, size_t size, std::string* why) {
	std::string err;
	if (!checkEnvelope(data, size, err)) {
		report(why, err);
		return nullptr;
	}
	ByteReader r(data, size - kTrailerBytes);
	r.take(kPrefixBytes);
	const uint32_t flags = le32(data + 8);
	Meta m;
	if (!getMeta(r, m)) {
		report(why, "save: bad header");
		return nullptr;
	}
	const uint32_t stateBytes = r.u32();
	const uint8_t* blob = r.take(stateBytes);
	if (!r.ok() || !blob) {
		report(why, "save: truncated world data");
		return nullptr;
	}
	std::string stateWhy;
	std::unique_ptr<World> w = state::deserialize(blob, stateBytes, &stateWhy);
	if (!w) {
		report(why, "save: " + stateWhy);
		return nullptr;
	}
	if (!sameMeta(m, metaOf(*w, m.savedAt))) {
		report(why, "save: the header disagrees with the world it carries");
		return nullptr;
	}
	if ((flags & kFlagSnapshots) != 0 && !getSnapshots(r, *w, err)) {
		report(why, err);
		return nullptr;
	}
	if (!r.ok() || !r.atEnd()) {
		report(why, "save: trailing bytes after the last section");
		return nullptr;
	}
	return w;
}

std::optional<Meta> peek(const uint8_t* data, size_t size) {
	std::string err;
	if (!checkEnvelope(data, size, err)) return std::nullopt;
	ByteReader r(data, size - kTrailerBytes);
	r.take(kPrefixBytes);
	Meta m;
	if (!getMeta(r, m)) return std::nullopt;
	return m;
}

// ------------------------------------------------------------------ public API: files

bool writeFile(const World& w, const std::string& path, const EncodeOpts& opts, std::string* why) {
	std::vector<uint8_t> bytes;
	std::string err;
	if (!encode(w, bytes, opts, &err)) {
		report(why, err);
		return false;
	}
	if (!writeAtomic(path, bytes, err)) {
		report(why, err);
		return false;
	}
	return true;
}

std::unique_ptr<World> readFile(const std::string& path, std::string* why) {
	std::vector<uint8_t> bytes;
	std::string err;
	if (!readWhole(path, kMaxFileBytes, bytes, err)) {
		report(why, err);
		return nullptr;
	}
	return decode(bytes.data(), bytes.size(), why);
}

std::optional<Meta> readMeta(const std::string& path) {
	std::vector<uint8_t> bytes;
	std::string err;
	if (!readWhole(path, kMaxFileBytes, bytes, err)) return std::nullopt;
	return peek(bytes.data(), bytes.size());
}

// ------------------------------------------------------------------ public API: slots

namespace {
bool validSlotName(std::string_view s) {
	if (s.empty() || s.size() > kMaxSlotName) return false;
	for (char ch : s) {
		const bool letter = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
		const bool digit = ch >= '0' && ch <= '9';
		if (!letter && !digit && ch != '_' && ch != '-') return false;
	}
	return true;
}
} // namespace

std::string slotPath(const std::string& dir, std::string_view slot) {
	if (!validSlotName(slot)) return std::string();
	std::filesystem::path base;
	if (!toPath(dir, base)) return std::string();
	return fromPath(base / (std::string(slot) + kExt));
}

bool writeSlot(const World& w, const std::string& dir, std::string_view slot, const EncodeOpts& opts, std::string* why) {
	const std::string path = slotPath(dir, slot);
	if (path.empty()) {
		report(why, "save: bad slot name or folder");
		return false;
	}
	return writeFile(w, path, opts, why);
}

std::unique_ptr<World> readSlot(const std::string& dir, std::string_view slot, std::string* why) {
	const std::string path = slotPath(dir, slot);
	if (path.empty()) {
		report(why, "save: bad slot name or folder");
		return nullptr;
	}
	return readFile(path, why);
}

std::vector<SlotInfo> listSlots(const std::string& dir) {
	std::vector<SlotInfo> out;
	std::filesystem::path folder;
	if (!toPath(dir, folder)) return out;
	std::vector<std::string> names;
	std::error_code ec;
	for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
		std::error_code ec2;
		const std::filesystem::path entry = it->path();
		if (!std::filesystem::is_regular_file(entry, ec2)) continue;
		const std::string file = fromPath(entry.filename());
		if (!endsWith(file, kExt)) continue;
		const std::string stem = file.substr(0, file.size() - std::strlen(kExt));
		if (validSlotName(stem)) names.push_back(stem);
	}
	std::sort(names.begin(), names.end());
	for (const std::string& name : names) {
		const std::optional<Meta> m = readMeta(slotPath(dir, name));
		if (m) out.push_back(SlotInfo{ name, *m });
	}
	return out;
}

bool removeSlot(const std::string& dir, std::string_view slot) {
	const std::string path = slotPath(dir, slot);
	std::filesystem::path p;
	if (path.empty() || !toPath(path, p)) return false;
	std::error_code ec;
	if (!std::filesystem::is_regular_file(p, ec)) return false;   // never a folder that happens to carry the name
	const bool removed = std::filesystem::remove(p, ec);
	return removed && !ec;
}

// ------------------------------------------------------------------ public API: settings

UserSettings defaultSettings(bool reducedMotion) {
	UserSettings s;
	if (reducedMotion) {
		s.flashes = 0.3;
		s.shake = 0.3;
	}
	return s;
}

UserSettings loadSettings(const std::string& path, bool reducedMotion) {
	UserSettings s = defaultSettings(reducedMotion);
	std::vector<uint8_t> bytes;
	std::string err;
	if (!readWhole(path, kMaxSettingsBytes, bytes, err)) return s;
	std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	// A UTF-8 byte-order mark (an editor may add one) is not part of the first key.
	if (text.size() >= 3 && uint8_t(text[0]) == 0xEF && uint8_t(text[1]) == 0xBB && uint8_t(text[2]) == 0xBF) text.remove_prefix(3);
	size_t pos = 0;
	while (pos < text.size()) {
		size_t end = text.find('\n', pos);
		if (end == std::string_view::npos) end = text.size();
		const std::string_view line = trimmed(text.substr(pos, end - pos));
		pos = end + 1;
		if (line.empty() || line.front() == '#') continue;
		const size_t eq = line.find('=');
		if (eq == std::string_view::npos) continue;
		applySetting(s, trimmed(line.substr(0, eq)), trimmed(line.substr(eq + 1)));
	}
	return s;
}

bool saveSettings(const UserSettings& s, const std::string& path) {
	std::string t;
	t += "# DEMIURGE settings: key=value per line; unknown keys are ignored\n";
	addLine(t, "master", numText(s.master));
	addLine(t, "music", numText(s.music));
	addLine(t, "sfx", numText(s.sfx));
	addLine(t, "ambience", numText(s.ambience));
	addLine(t, "quality", std::to_string(s.quality));
	if (s.sun == "orbit" || s.sun == "follow") addLine(t, "sun", s.sun);   // anything else: the default on load
	addLine(t, "tilt", numText(s.tilt));
	// flashes / shake are written only when they are not the full-strength default: a player who
	// never touched them keeps the reduced-motion default (0.3) when the file is loaded under
	// prefers-reduced-motion. Writing 1 would pin them and silently disable that default.
	if (s.flashes != 1.0) addLine(t, "flashes", numText(s.flashes));
	if (s.shake != 1.0) addLine(t, "shake", numText(s.shake));
	addLine(t, "uiScale", numText(s.uiScale));
	addLine(t, "labels", s.labels ? "1" : "0");
	addLine(t, "hints", s.hints ? "1" : "0");
	addLine(t, "autoSlow", s.autoSlow ? "1" : "0");
	std::vector<uint8_t> bytes(t.begin(), t.end());
	std::string err;
	return writeAtomic(path, bytes, err);
}

} // namespace dm::save
