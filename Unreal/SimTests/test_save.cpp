// DEMIURGE — SimTests/test_save.cpp : Sim/Save.h (A15, key "save"): the container round trip, the
// rejection of bad bytes, files (atomic writes), slots and settings. It is the C++ form of the A23
// browser checks in docs/BUILD.md (round trip, meta, corrupt and missing saves, settings persistence
// and defaults, unusable storage), on top of the foundation's binary save (state::serialize).
// Every test builds its own worlds with the helper (no other module is needed: run.ps1 save stubs them).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "MapImg.h"
#include "Save.h"
#include "State.h"
#include "TestFramework.h"
#include "TestWorld.h"

using namespace dm;
using namespace dmt;
namespace mi = dm::mapimg;

namespace {

// ------------------------------------------------------------------ bytes and files

// CRC-32 (IEEE 802.3, reflected, as zlib), bit by bit: an independent reference for Save.cpp's table.
uint32_t crcRef(const uint8_t* p, size_t n) {
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i++) {
		c ^= p[i];
		for (int k = 0; k < 8; k++) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
	}
	return c ^ 0xFFFFFFFFu;
}

uint32_t fnv(const std::vector<uint8_t>& b) {
	uint32_t h = 2166136261u;
	for (uint8_t v : b) { h ^= v; h *= 16777619u; }
	return h;
}

uint32_t le32At(const std::vector<uint8_t>& b, size_t at) {
	return uint32_t(b[at]) | (uint32_t(b[at + 1]) << 8) | (uint32_t(b[at + 2]) << 16) | (uint32_t(b[at + 3]) << 24);
}

// Recomputes the trailing checksum after a deliberate edit, so the parser sees the edit itself.
void resign(std::vector<uint8_t>& b) {
	const uint32_t c = crcRef(b.data(), b.size() - 4);
	for (size_t k = 0; k < 4; k++) b[b.size() - 4 + k] = uint8_t(c >> (8 * k));
}

void setLe64(std::vector<uint8_t>& b, size_t at, uint64_t v) {
	for (size_t k = 0; k < 8; k++) b[at + k] = uint8_t(v >> (8 * k));
}

bool has(const std::string& s, const char* text) { return s.find(text) != std::string::npos; }

std::vector<uint8_t> readBytes(const std::string& path) {
	std::vector<uint8_t> b;
	std::ifstream f(std::filesystem::path(path), std::ios::binary);
	if (!f) return b;
	b.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return b;
}

bool writeBytes(const std::string& path, const std::vector<uint8_t>& b) {
	const std::filesystem::path p(path);
	std::error_code ec;
	if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
	std::ofstream f(p, std::ios::binary | std::ios::trunc);
	if (!f) return false;
	f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
	f.close();
	return !f.fail();
}

bool writeText(const std::string& path, const std::string& text) {
	return writeBytes(path, std::vector<uint8_t>(text.begin(), text.end()));
}

// A fresh folder under the system temp directory (never inside the project), removed on exit.
struct TempDir {
	std::filesystem::path path;
	explicit TempDir(const std::string& tag) {
		std::error_code ec;
		path = std::filesystem::temp_directory_path(ec) / ("dm-save-" + tag);
		std::filesystem::remove_all(path, ec);
		std::filesystem::create_directories(path, ec);
	}
	~TempDir() {
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
	}
	TempDir(const TempDir&) = delete;
	TempDir& operator=(const TempDir&) = delete;
	std::string str() const { return path.string(); }
	std::string file(const std::string& name) const { return (path / name).string(); }
};

// ------------------------------------------------------------------ calls that must succeed

std::vector<uint8_t> encodeOk(const World& w, const save::EncodeOpts& o = {}) {
	std::vector<uint8_t> b;
	std::string why;
	if (!save::encode(w, b, o, &why)) fail("encode refused: " + why);
	return b;
}

std::unique_ptr<World> decodeOk(const std::vector<uint8_t>& b) {
	std::string why;
	std::unique_ptr<World> w = save::decode(b.data(), b.size(), &why);
	if (!w) fail("decode refused: " + why);
	return w;
}

// ------------------------------------------------------------------ worlds

// Random values for every cell field inside its own range (owner is set by richWorld).
void scrambleCells(World& w, Rng& r) {
	for (int i = 0; i < state::kCellFieldCount; i++) {
		const CellField& f = state::kCellFields[i];
		if (std::strcmp(f.name, "owner") == 0) continue;
		const CellArray a = state::cellArray(w, i);
		for (size_t c = 0; c < a.size; c++) {
			if (f.save == SaveCodec::Q8 || f.save == SaveCodec::Q16) a.set(c, r.range(f.min, f.max));
			else if (f.type == FieldType::U8) a.set(c, r.integer(0, 255));
			else if (f.type == FieldType::I32) a.set(c, r.integer(-1, w.n - 1));
			else a.set(c, r.range(-50, 50));
		}
	}
}

// A planet with every kind of entity and value the save carries: 3 cultures (one dead, one a
// daughter), 5 settlements (one dead, one praying, one with a prophet's town), 3 units, 4 disasters,
// 3 chronicle entries, relations, vars, god, ending and 2 time-lapse frames. With `scramble` every cell
// field gets random values (quantization and derived-field tests); without it the planet is the
// helper's terrain, which is what the maps show. Entity references are all valid (refsOk).
std::unique_ptr<World> richWorld(int L, uint32_t seed, bool scramble = true) {
	MakeOpts mo;
	mo.L = L;
	mo.seed = seed;
	mo.land = 0.4;
	mo.phase = Phase::History;
	std::unique_ptr<World> wp = makeWorld(mo);
	World& w = *wp;
	if (scramble) {
		Rng r(seed * 7919u + 11u);
		scrambleCells(w, r);
	}
	w.year = 1234.25 + seed;
	w.tick = 98765 + seed;
	w.heat = 0;
	w.seaLevel = 0.37;
	w.globalTemp = 1.5;
	w.co2 = 77.5;
	w.dust = 0.125;
	w.name = "\xC3\x96lmar " + std::to_string(seed);
	w.rng = Rng(777 + seed);

	{
		Culture& c0 = state::newCulture(w);
		c0.name = "Umar"; c0.adj = "Umaran"; c0.plural = "the Umari"; c0.lang = "v1|C=ptk|V=aiu";
		c0.age = 3; c0.science = 321.5; c0.godName = "Ashu"; c0.epithet = "the Green Hand";
		c0.traits.piety = 0.9; c0.traits.aggression = 0.2; c0.traits.curiosity = 0.55; c0.traits.seafaring = 0.125;
		c0.prophet = Prophet{ "Dovak", 1, 1290 };
		c0.edict = Edict::Build; c0.edictSince = 1250; c0.pop = 5000.5; c0.towns = 2;
		c0.exhaustion = 0.25; c0.seen["rain"] = 0.75; c0.lastSplit = 900;
	}
	{
		Culture& c1 = state::newCulture(w, 0);
		c1.name = "Vorn"; c1.adj = "Vornic"; c1.plural = "the Vorn"; c1.lang = "v1|C=kdl|V=oe";
		c1.born = 1000; c1.age = 1; c1.science = 12.25; c1.godName = "Ilo";
		c1.traits.piety = 0.25; c1.pop = 800; c1.lastSplit = 1100;
	}
	{
		Culture& c2 = state::newCulture(w);
		c2.name = "Ghost"; c2.dead = true; c2.born = 500;
	}

	const int stride = std::max(1, w.n / 9);
	for (int k = 0; k < 5; k++) {
		const int cell = (3 + k * stride) % w.n;
		Settlement& s = state::newSettlement(w, cell, k % 2);
		s.name = "Tel Asha " + std::to_string(k);
		s.founded = 100 + k; s.pop = 1000.5 + k; s.tier = k % 5; s.K = 1500.25; s.growth = -12.5; s.radius = 2 + k;
		s.b[Building::SHRINE] = 1;
		s.b[Building::WONDER] = uint8_t(k == 1);
		s.b[Building::LAUNCH] = uint8_t(k == 3);
		s.devotion = 0.6; s.love = -0.25; s.jaded = 0.1; s.plague = 0.3; s.immune = 5; s.inspire = 2; s.blessed = 7;
		s.famine = k; s.apart = 3; s.ark = 0.5; s.lastExpand = 1200;
		s.capital = k == 0;
		if (k == 1) s.prayer = Prayer{ PrayerKind::Heal, 1199.5 };
		if (k == 4) { s.dead = true; s.deadYear = 1100.75; s.cause = Cause::Quake; }
		const int sid = s.id;
		const bool dead = s.dead;
		if (!dead) {
			std::vector<int32_t> ring;
			w.geo->ring(cell, 5, ring);             // wide enough to read on the map
			for (int32_t m : ring) w.owner[size_t(m)] = int16_t(sid);
		}
	}
	w.cultures[0].capital = 0;

	{
		Unit& u = state::newUnit(w, UnitType::Settler);
		u.culture = 0; u.from = 0; u.to = 2; u.dest = w.n - 1; u.path = { 0, 1, 2, 3 }; u.i = 2; u.t = 0.375;
		u.speed = 5.5; u.size = 40; u.born = 1200.25; u.naval = NavalMode::Coastal; u.data["lost"] = 1.5;
	}
	{
		Unit& u = state::newUnit(w, UnitType::Army);
		u.culture = 1; u.from = 2; u.to = -1; u.dest = 7; u.path = { 5 }; u.i = 0; u.t = 0.5; u.speed = 4;
		u.size = 300.75; u.naval = NavalMode::Open; u.sea = true; u.born = 1201;
	}
	{
		Unit& u = state::newUnit(w, UnitType::Prophet);
		u.culture = 0; u.from = 0; u.to = 1; u.dest = 9; u.speed = 2; u.size = 1; u.born = 1250.5;
		u.data["fame"] = 0.5;
	}
	{
		Disaster& d = state::newDisaster(w, DisasterType::Volcano, 5);
		d.t = 3.5; d.dur = 12; d.power = 0.75; d.ang = 0.04; d.natural = true; d.cells = { 1, 2, 3 };
		d.data["ash"] = 2.25;
	}
	{
		Disaster& d = state::newDisaster(w, DisasterType::Comet, 9);
		d.t = 0.25; d.dur = 1; d.power = 1; d.ang = 0.03; d.eta = 3.25; d.flight = 8.5;
	}
	{
		Disaster& d = state::newDisaster(w, DisasterType::Tsunami, 11);
		d.front = 0.02; d.power = 4.5; d.ang = 0.5;
	}
	{
		Disaster& d = state::newDisaster(w, DisasterType::Storm, -1);
		d.power = 0.5; d.ang = 0.1; d.natural = true;
	}
	for (int k = 0; k < 3; k++) {
		ChronicleEntry e;
		e.id = w.nextId.entry++;
		e.year = 1200 + 4.25 * k;
		e.type = k == 0 ? "founded" : "war";
		e.text = "Entry " + std::to_string(k) + " \xC3\xA9";
		e.cell = k == 2 ? -1 : 5 + k;
		e.culture = k % 2;
		e.weight = k + 1;
		w.chronicle.push_back(e);
	}
	{
		Relation r01;
		r01.v = -0.5; r01.war = true; r01.since = 1210; r01.truce = 1190.5; r01.trade = 0.25; r01.contact = true;
		r01.aggressor = 0;
		w.vars.rel[state::relKey(0, 1)] = r01;
		Relation r12;
		r12.v = 0.75; r12.trade = 0.5; r12.aggressor = -1;
		w.vars.rel[state::relKey(2, 1)] = r12;
	}
	w.vars.ark = ArkRecord{ 0, 0, 1290.5 };
	w.vars.lastNuke = 1260.25;
	w.vars.snapEvery = 0.25;
	w.vars.snapNext = 1300.5;
	w.vars.seaBase = 0.2;
	w.vars.iceRef = 0.4;
	w.vars.lifeBoost = 1.5;
	w.vars.t0 = 4000;
	w.vars.num["civ_x"] = 2.5;
	w.vars.num["powers_y"] = -7.125;
	w.vars.str["hist_note"] = "tide \xC3\xB8";

	w.god.awe = 55.5; w.god.aweMax = 300; w.god.mercy = 3.25; w.god.wrath = 1.75; w.god.epithet = "the Rain-Giver";
	w.god.deeds["rain"] = 12; w.god.deeds["meteor"] = 1;
	w.stats.pop = 4200.5; w.stats.peakPop = 5100.25; w.stats.believers = 812.5; w.stats.wars = 2; w.stats.miracles = 9;
	w.settings.calamity = 0.5;
	w.ending = Ending{ EndingType::Ark, Mood::Beloved, 0, 0, 1290.5 };

	const size_t n = size_t(w.n);
	for (int f = 0; f < 2; f++) {
		Snapshot snap;
		snap.year = 1200 + 40 * f;
		snap.heat = 0.25 * f;
		snap.seaLevel = 0.37 - f;
		snap.pop = 4000 + f;
		snap.biome.assign(n, 0);
		snap.owner.assign(n, -1);
		snap.urban.assign(n, 0);
		for (size_t c = 0; c < n; c++) {
			snap.biome[c] = uint8_t((c / 37 + size_t(f)) % size_t(Biome::COUNT));
			const int16_t o = w.owner[c];
			snap.owner[c] = o >= 0 ? int8_t(w.settlements[size_t(o)].culture) : int8_t(-1);
			snap.urban[c] = uint8_t((c * 7 + size_t(f) * 3) % 256);
		}
		w.snapshots.push_back(std::move(snap));
	}
	return wp;
}

// ------------------------------------------------------------------ comparisons

#define SAVE_SAME(field) eq(a.field, b.field, where + "." #field)

void sameSettlement(const Settlement& a, const Settlement& b, const std::string& where) {
	SAVE_SAME(id); SAVE_SAME(cell); SAVE_SAME(name); SAVE_SAME(culture); SAVE_SAME(founded); SAVE_SAME(dead);
	SAVE_SAME(deadYear); SAVE_SAME(cause); SAVE_SAME(pop); SAVE_SAME(tier); SAVE_SAME(K); SAVE_SAME(growth);
	SAVE_SAME(radius); SAVE_SAME(b); SAVE_SAME(devotion); SAVE_SAME(love); SAVE_SAME(jaded); SAVE_SAME(plague);
	SAVE_SAME(immune); SAVE_SAME(inspire); SAVE_SAME(blessed); SAVE_SAME(famine); SAVE_SAME(prayer);
	SAVE_SAME(apart); SAVE_SAME(capital); SAVE_SAME(ark); SAVE_SAME(lastExpand);
}

void sameCulture(const Culture& a, const Culture& b, const std::string& where) {
	SAVE_SAME(id); SAVE_SAME(name); SAVE_SAME(adj); SAVE_SAME(plural); SAVE_SAME(color); SAVE_SAME(lang);
	SAVE_SAME(parent); SAVE_SAME(born); SAVE_SAME(dead); SAVE_SAME(age); SAVE_SAME(science); SAVE_SAME(capital);
	SAVE_SAME(godName); SAVE_SAME(epithet); SAVE_SAME(prophet); SAVE_SAME(edict); SAVE_SAME(edictSince);
	SAVE_SAME(pop); SAVE_SAME(towns); SAVE_SAME(exhaustion); SAVE_SAME(seen); SAVE_SAME(lastSplit);
	eq(a.traits.piety, b.traits.piety, where + ".traits.piety");
	eq(a.traits.aggression, b.traits.aggression, where + ".traits.aggression");
	eq(a.traits.curiosity, b.traits.curiosity, where + ".traits.curiosity");
	eq(a.traits.seafaring, b.traits.seafaring, where + ".traits.seafaring");
}

void sameUnit(const Unit& a, const Unit& b, const std::string& where) {
	SAVE_SAME(id); SAVE_SAME(type); SAVE_SAME(culture); SAVE_SAME(from); SAVE_SAME(to); SAVE_SAME(dest);
	SAVE_SAME(path); SAVE_SAME(i); SAVE_SAME(t); SAVE_SAME(speed); SAVE_SAME(size); SAVE_SAME(naval);
	SAVE_SAME(sea); SAVE_SAME(born); SAVE_SAME(data);
}

void sameDisaster(const Disaster& a, const Disaster& b, const std::string& where) {
	SAVE_SAME(id); SAVE_SAME(type); SAVE_SAME(cell); SAVE_SAME(t); SAVE_SAME(dur); SAVE_SAME(power); SAVE_SAME(ang);
	SAVE_SAME(natural); SAVE_SAME(eta); SAVE_SAME(flight); SAVE_SAME(front); SAVE_SAME(cells); SAVE_SAME(data);
}

// Cell fields: persisted ones hold the value the save stores (quantized as the format says), derived
// ones are back at their defaults (they are recomputed after a load, never stored). Returns the count
// of mismatches.
int sameCells(const World& a, const World& b) {
	int bad = 0;
	for (int i = 0; i < state::kCellFieldCount; i++) {
		const CellField& f = state::kCellFields[i];
		const CellArray ca = state::cellArray(a, i);
		const CellArray cb = state::cellArray(b, i);
		if (ca.size != cb.size) {
			fail(std::string(f.name) + ": array size differs");
			bad++;
			continue;
		}
		for (size_t c = 0; c < ca.size; c++) {
			double want = f.def;
			if (f.save == SaveCodec::Q8 || f.save == SaveCodec::Q16) want = state::dequantize(f, state::quantize(f, ca.get(c)));
			else if (f.save != SaveCodec::None) want = ca.get(c);
			if (ca.type == FieldType::F32) want = double(float(want));
			if (cb.get(c) != want) {
				if (bad < 4) fail(std::string(f.name) + "[" + std::to_string(c) + "]: loaded " + std::to_string(cb.get(c)) + ", want " + std::to_string(want));
				bad++;
			}
		}
	}
	eq(bad, 0, "cell fields: persisted values stored, derived values at their defaults");
	return bad;
}

// Everything a save carries, compared exactly (f64 fields) or to the stored code (quantized fields).
void sameWorld(const World& a, const World& b) {
	eq(a.seed, b.seed, "seed");
	eq(a.name, b.name, "name");
	eq(a.L, b.L, "L");
	eq(a.n, b.n, "n");
	eq(a.year, b.year, "year");
	eq(a.tick, b.tick, "tick");
	eq(a.phase, b.phase, "phase");
	eq(a.heat, b.heat, "heat");
	eq(a.seaLevel, b.seaLevel, "seaLevel");
	eq(a.globalTemp, b.globalTemp, "globalTemp");
	eq(a.co2, b.co2, "co2");
	eq(a.dust, b.dust, "dust");
	eq(a.age, b.age, "age");
	ok(a.ending == b.ending, "ending");
	eq(a.god.awe, b.god.awe, "god.awe");
	eq(a.god.aweMax, b.god.aweMax, "god.aweMax");
	eq(a.god.mercy, b.god.mercy, "god.mercy");
	eq(a.god.wrath, b.god.wrath, "god.wrath");
	eq(a.god.epithet, b.god.epithet, "god.epithet");
	ok(a.god.deeds == b.god.deeds, "god.deeds");
	eq(a.stats.pop, b.stats.pop, "stats.pop");
	eq(a.stats.peakPop, b.stats.peakPop, "stats.peakPop");
	eq(a.stats.believers, b.stats.believers, "stats.believers");
	eq(a.stats.wars, b.stats.wars, "stats.wars");
	eq(a.stats.miracles, b.stats.miracles, "stats.miracles");
	eq(a.settings.calamity, b.settings.calamity, "settings.calamity");
	eq(a.nextId.unit, b.nextId.unit, "nextId.unit");
	eq(a.nextId.disaster, b.nextId.disaster, "nextId.disaster");
	eq(a.nextId.entry, b.nextId.entry, "nextId.entry");
	ok(a.vars.seaBase == b.vars.seaBase, "vars.seaBase");
	ok(a.vars.iceRef == b.vars.iceRef, "vars.iceRef");
	eq(a.vars.lifeBoost, b.vars.lifeBoost, "vars.lifeBoost");
	eq(a.vars.t0, b.vars.t0, "vars.t0");
	ok(a.vars.rel == b.vars.rel, "vars.rel");
	ok(a.vars.ark == b.vars.ark, "vars.ark");
	ok(a.vars.lastNuke == b.vars.lastNuke, "vars.lastNuke");
	ok(a.vars.snapEvery == b.vars.snapEvery && a.vars.snapNext == b.vars.snapNext, "vars.snapEvery / snapNext");
	ok(a.vars.num == b.vars.num, "vars.num");
	ok(a.vars.str == b.vars.str, "vars.str");

	Rng ra = a.rng;
	Rng rb = b.rng;
	int drawDiff = 0;
	for (int k = 0; k < 64; k++) if (ra.next() != rb.next()) drawDiff++;
	eq(drawDiff, 0, "rng continues the same stream (64 draws)");

	eq(a.settlements.size(), b.settlements.size(), "settlement count");
	for (size_t i = 0; i < std::min(a.settlements.size(), b.settlements.size()); i++)
		sameSettlement(a.settlements[i], b.settlements[i], "settlement " + std::to_string(i));
	eq(a.cultures.size(), b.cultures.size(), "culture count");
	for (size_t i = 0; i < std::min(a.cultures.size(), b.cultures.size()); i++)
		sameCulture(a.cultures[i], b.cultures[i], "culture " + std::to_string(i));
	eq(a.units.size(), b.units.size(), "unit count");
	for (size_t i = 0; i < std::min(a.units.size(), b.units.size()); i++)
		sameUnit(a.units[i], b.units[i], "unit " + std::to_string(i));
	eq(a.disasters.size(), b.disasters.size(), "disaster count");
	for (size_t i = 0; i < std::min(a.disasters.size(), b.disasters.size()); i++)
		sameDisaster(a.disasters[i], b.disasters[i], "disaster " + std::to_string(i));
	ok(a.chronicle == b.chronicle, "chronicle entries");

	sameCells(a, b);
}

void sameSnapshots(const World& a, const World& b) {
	eq(a.snapshots.size(), b.snapshots.size(), "snapshot count");
	for (size_t i = 0; i < std::min(a.snapshots.size(), b.snapshots.size()); i++) {
		const Snapshot& x = a.snapshots[i];
		const Snapshot& y = b.snapshots[i];
		const std::string where = "snapshot " + std::to_string(i);
		eq(x.year, y.year, where + ".year");
		eq(x.heat, y.heat, where + ".heat");
		eq(x.seaLevel, y.seaLevel, where + ".seaLevel");
		eq(x.pop, y.pop, where + ".pop");
		ok(x.biome == y.biome, where + ".biome bytes");
		ok(x.owner == y.owner, where + ".owner bytes");
		ok(x.urban == y.urban, where + ".urban bytes");
	}
}

// ------------------------------------------------------------------ fuzzing helpers

// Edits the payload of a valid container in random ways and re-seals the checksum, so the parser
// (not the checksum) has to cope. Every edit must either load (and then save again) or be refused
// with a reason; nothing may crash.
void editedPayloads(const std::vector<uint8_t>& good, int iterations, uint32_t seed, int& accepted, int& rejected) {
	Rng r(seed);
	accepted = 0;
	rejected = 0;
	const size_t body = good.size() - 4;
	for (int it = 0; it < iterations; it++) {
		std::vector<uint8_t> m = good;
		const size_t at = 20 + size_t(r.integer(0, int(body - 21)));
		const int kind = r.integer(0, 3);
		if (kind == 0) {
			m[at] = uint8_t(r.integer(0, 255));
		} else if (kind == 1) {
			m[at] ^= uint8_t(1u << r.integer(0, 7));
		} else if (kind == 2) {
			for (size_t k = 0; k < 4 && at + k < body; k++) m[at + k] = 0xFF;
		} else {
			m.resize(at + 4);                 // cut the payload short and say so in the size field
			setLe64(m, 12, uint64_t(m.size()));
		}
		resign(m);
		std::string why;
		std::unique_ptr<World> w = save::decode(m.data(), m.size(), &why);
		if (w) {
			accepted++;
			std::vector<uint8_t> again;
			std::string why2;
			if (!save::encode(*w, again, {}, &why2)) fail("an accepted payload cannot be saved again: " + why2);
		} else {
			rejected++;
			if (why.empty()) fail("a rejected payload gave no reason");
		}
	}
}

// Writes the settings text, then loads it (reduced motion off) and returns the result.
save::UserSettings settingsFrom(const TempDir& t, const std::string& text, bool reducedMotion = false) {
	const std::string file = t.file("settings-in.txt");
	writeText(file, text);
	return save::loadSettings(file, reducedMotion);
}

} // namespace

// ================================================================== container

DM_TEST("save: round trip is a fixed point (same hash and bytes after the load)") {
	auto raw = richWorld(4, 3);
	const std::vector<uint8_t> firstLoad = encodeOk(*raw);
	if (firstLoad.empty()) return;
	// The first load quantizes the cell fields once; after that save and load are exact.
	auto saved = decodeOk(firstLoad);
	if (!saved) return;
	const uint32_t hashBefore = hash(*saved);
	const std::vector<uint8_t> bytesBefore = encodeOk(*saved);
	auto loaded = decodeOk(bytesBefore);
	if (!loaded) return;
	eq(hash(*loaded), hashBefore, "dmt::hash of the world before the save == after the load");
	const std::vector<uint8_t> bytesAfter = encodeOk(*loaded);
	ok(bytesAfter == bytesBefore, "container bytes identical after the load (" + std::to_string(bytesBefore.size()) + " bytes)");
	eq(fnv(bytesAfter), fnv(bytesBefore), "fnv-1a of the container before == after");
	eq(bytesBefore.size(), firstLoad.size(), "quantizing once does not change the size");
	noNaN(*loaded, "after the load");
	std::printf("  round trip L4: %zu bytes, hash %08x before and after\n", bytesBefore.size(), hashBefore);
}

DM_TEST("save: the loaded world matches the saved one entity by entity") {
	auto raw = richWorld(4, 5);
	auto loaded = decodeOk(encodeOk(*raw));
	if (!loaded) return;
	sameWorld(*raw, *loaded);
	sameSnapshots(*raw, *loaded);
	noNaN(*loaded, "after the load");
}

DM_TEST("save: quantized cell fields come back within half a quantization step") {
	auto raw = richWorld(4, 8);
	auto loaded = decodeOk(encodeOk(*raw));
	if (!loaded) return;
	int checked = 0;
	for (int i = 0; i < state::kCellFieldCount; i++) {
		const CellField& f = state::kCellFields[i];
		if (f.save != SaveCodec::Q8 && f.save != SaveCodec::Q16) continue;
		const double top = f.save == SaveCodec::Q16 ? 65535.0 : 255.0;
		const double half = 0.5 * (f.max - f.min) / top;
		const CellArray ca = state::cellArray(*raw, i);
		const CellArray cb = state::cellArray(*loaded, i);
		double worst = 0;
		for (size_t c = 0; c < ca.size; c++) worst = std::max(worst, std::fabs(ca.get(c) - cb.get(c)));
		ok(worst <= half + 1e-6, std::string(f.name) + ": worst error " + std::to_string(worst) + " > half step " + std::to_string(half));
		std::printf("  %-6s %s: worst error %.6f (half step %.6f)\n", f.name, f.save == SaveCodec::Q16 ? "q16" : "q8 ", worst, half);
		checked++;
	}
	eq(checked, 13, "quantized fields checked (elev, water, wet, veg, fert, ice, fire, scorch, lava, fauna, rad, farm, urban)");
}

DM_TEST("save: snapshots are stored exactly and are optional") {
	auto raw = richWorld(4, 6);
	const std::vector<uint8_t> with = encodeOk(*raw);
	const std::vector<uint8_t> without = encodeOk(*raw, save::EncodeOpts{ .snapshots = false });
	if (with.empty() || without.empty()) return;
	auto wWith = decodeOk(with);
	auto wWithout = decodeOk(without);
	if (!wWith || !wWithout) return;
	sameSnapshots(*raw, *wWith);
	eq(wWithout->snapshots.size(), size_t(0), "no snapshots when the save leaves them out");
	ok(without.size() < with.size(), "a save without frames is smaller (" + std::to_string(without.size()) + " < " + std::to_string(with.size()) + ")");
	eq(hash(*wWith), hash(*wWithout), "the world itself is the same with or without frames");
	std::printf("  frames: %zu bytes with two frames, %zu without\n", with.size(), without.size());
}

DM_TEST("save: a world that would not load is refused and nothing is written") {
	TempDir t("refuse");
	const std::string file = t.file("keep.dmsave");
	std::string why;
	auto good = richWorld(2, 71);
	ok(save::writeFile(*good, file, {}, &why), "the first save: " + why);
	const std::vector<uint8_t> before = readBytes(file);
	std::vector<uint8_t> out;

	auto badAge = richWorld(2, 71);
	badAge->age = 9;                                    // not an Age
	ok(!save::encode(*badAge, out, {}, &why), "encode refuses an age outside the Age list");
	ok(out.empty(), "nothing is produced for it");
	ok(has(why, "would not read back"), "the reason says the world would not load: " + why);
	ok(!save::writeFile(*badAge, file, {}, &why), "writeFile refuses it too");
	eq(readBytes(file), before, "the previous save is kept");

	auto danglingCulture = richWorld(2, 71);
	danglingCulture->settlements[1].culture = 7;        // names a culture that does not exist
	ok(!save::encode(*danglingCulture, out, {}, &why), "encode refuses a settlement whose culture does not exist");
	ok(out.empty(), "nothing is produced for it either");

	auto badFrame = richWorld(2, 71);
	badFrame->snapshots[0].owner[0] = 9;                // a frame naming a culture that does not exist
	ok(!save::encode(*badFrame, out, {}, &why), "encode refuses a frame that names no culture");
	ok(has(why, "snapshot"), "the reason names the frame: " + why);

	auto longName = richWorld(2, 71);
	longName->name.assign(70000, 'n');                  // longer than the 64 KB the format allows
	ok(!save::encode(*longName, out, {}, &why), "encode refuses a world name longer than 64 KB");
	ok(has(why, "longer"), "the reason says why: " + why);
}

DM_TEST("save: meta reads back without loading the world") {
	auto w = richWorld(3, 2);
	const save::EncodeOpts opts{ .savedAt = 1700000000 };
	const std::vector<uint8_t> b = encodeOk(*w, opts);
	if (b.empty()) return;
	const save::Meta want = save::metaOf(*w, 1700000000);
	const std::optional<save::Meta> got = save::peek(b.data(), b.size());
	ok(got.has_value() && *got == want, "peek() equals metaOf() of the saved world");
	eq(want.name, w->name, "meta name");
	eq(want.seed, w->seed, "meta seed");
	eq(want.year, w->year, "meta year");
	eq(int(want.phase), int(w->phase), "meta phase");
	eq(want.age, w->age, "meta age");
	eq(want.pop, w->stats.pop, "meta pop");
	eq(want.savedAt, int64_t(1700000000), "meta savedAt");
	ok(!save::peek(b.data(), 10).has_value(), "peek() refuses a truncated header");
	TempDir t("meta");
	std::string why;
	ok(save::writeFile(*w, t.file("m.dmsave"), opts, &why), "write for readMeta: " + why);
	const std::optional<save::Meta> fromFile = save::readMeta(t.file("m.dmsave"));
	ok(fromFile.has_value() && *fromFile == want, "readMeta() of the file equals the same meta");
}

DM_TEST("save: every truncation is rejected (L 2 at every length, L 4 sampled)") {
	for (int L : { 2, 4 }) {
		auto w = richWorld(L, 5 + uint32_t(L));
		const std::vector<uint8_t> b = encodeOk(*w);
		if (b.empty()) return;
		const size_t step = L == 2 ? 1 : 97;
		int accepted = 0, tried = 0;
		for (size_t len = 0; len < b.size(); len += step) {
			std::string why;
			if (save::decode(b.data(), len, &why)) accepted++;
			if (save::peek(b.data(), len).has_value()) accepted++;
			if (why.empty()) fail("a truncated save gave no reason at length " + std::to_string(len));
			tried++;
		}
		eq(accepted, 0, "L " + std::to_string(L) + ": truncated saves accepted");
		ok(tried > 50, "L " + std::to_string(L) + ": truncations tried: " + std::to_string(tried));
		std::printf("  L%d: %d truncations of a %zu-byte save, all refused\n", L, tried, b.size());
	}
}

DM_TEST("save: header and envelope edits are each refused with a reason") {
	auto w = richWorld(2, 9);
	const std::vector<uint8_t> good = encodeOk(*w);
	if (good.size() < 64) { fail("container unexpectedly small"); return; }
	const uint64_t size = uint64_t(good.size());

	struct Case { const char* name; std::vector<uint8_t> bytes; const char* reason; };
	std::vector<Case> cases;
	{ std::vector<uint8_t> m = good; m[0] = 'X'; resign(m); cases.push_back({ "wrong magic", m, "magic" }); }
	{ std::vector<uint8_t> m = good; m[4] = 2; resign(m); cases.push_back({ "container version 2", m, "version" }); }
	{ std::vector<uint8_t> m = good; m[8] |= 0x02; resign(m); cases.push_back({ "unknown flag bit", m, "flags" }); }
	{ std::vector<uint8_t> m = good; setLe64(m, 12, size + 1); resign(m); cases.push_back({ "size field one too large", m, "size" }); }
	{ std::vector<uint8_t> m = good; setLe64(m, 12, size - 1); resign(m); cases.push_back({ "size field one too small", m, "size" }); }
	{ std::vector<uint8_t> m = good; m.push_back(0); cases.push_back({ "one trailing byte (size not updated)", m, "size" }); }
	{ std::vector<uint8_t> m = good; m.push_back(0); setLe64(m, 12, size + 1); resign(m); cases.push_back({ "trailing byte after the last section", m, "trailing" }); }
	{ std::vector<uint8_t> m = good; m.back() ^= 0xFF; cases.push_back({ "checksum corrupted", m, "checksum" }); }
	{ std::vector<uint8_t> m = good; m[20] ^= 0x01; cases.push_back({ "meta byte changed, checksum kept", m, "checksum" }); }
	{ std::vector<uint8_t> m(good.begin(), good.begin() + 23); cases.push_back({ "shorter than a header", m, "short" }); }
	// The world blob inside the container has its own magic and version (State.cpp), checked too.
	const size_t blobAt = 20 + (4 + w->name.size() + 4 + 8 + 1 + 4 + 8 + 8) + 4;
	if (blobAt + 8 < good.size()) {
		{ std::vector<uint8_t> m = good; m[blobAt] = 'X'; resign(m); cases.push_back({ "world blob magic changed", m, "format" }); }
		{ std::vector<uint8_t> m = good; m[blobAt + 4] = 2; resign(m); cases.push_back({ "world blob version 2", m, "version" }); }
	}

	for (const Case& c : cases) {
		std::string why;
		std::unique_ptr<World> got = save::decode(c.bytes.data(), c.bytes.size(), &why);
		ok(!got, std::string(c.name) + ": accepted");
		if (!has(why, c.reason)) fail(std::string(c.name) + ": reason \"" + why + "\" does not mention \"" + c.reason + "\"");
	}
	std::printf("  %zu header and envelope edits refused\n", cases.size());
}

DM_TEST("save: the checksum is zlib's CRC-32 over every byte before it") {
	const char* check = "123456789";
	eq(crcRef(reinterpret_cast<const uint8_t*>(check), 9), 0xCBF43926u, "CRC-32 check value of \"123456789\"");
	auto w = richWorld(2, 4);
	const std::vector<uint8_t> b = encodeOk(*w);
	if (b.size() < 8) return;
	eq(le32At(b, b.size() - 4), crcRef(b.data(), b.size() - 4), "trailer equals CRC-32 of the rest");
}

DM_TEST("save: every single-byte corruption is caught by the checksum") {
	auto w = richWorld(2, 10);
	const std::vector<uint8_t> b = encodeOk(*w);
	if (b.empty()) return;
	int accepted = 0;
	for (size_t i = 0; i < b.size(); i++) {
		std::vector<uint8_t> m = b;
		m[i] ^= 0x5A;
		if (save::decode(m.data(), m.size())) accepted++;
	}
	eq(accepted, 0, "corrupted saves accepted (of " + std::to_string(b.size()) + " single-byte corruptions)");
}

DM_TEST("save: payloads edited under a valid checksum never crash the loader (L 2)") {
	auto w = richWorld(2, 12);
	const std::vector<uint8_t> good = encodeOk(*w);
	if (good.size() < 64) return;
	int accepted = 0, rejected = 0;
	editedPayloads(good, 1500, 2024, accepted, rejected);
	eq(accepted + rejected, 1500, "every edit either loads or is refused");
	std::printf("  L2 payload edits: %d loaded (and saved again), %d refused with a reason\n", accepted, rejected);
}

DM_TEST("save: payloads edited under a valid checksum never crash the loader (L 4)") {
	auto w = richWorld(4, 13);
	const std::vector<uint8_t> good = encodeOk(*w);
	if (good.size() < 64) return;
	int accepted = 0, rejected = 0;
	editedPayloads(good, 300, 4048, accepted, rejected);
	eq(accepted + rejected, 300, "every edit either loads or is refused");
	std::printf("  L4 payload edits: %d loaded (and saved again), %d refused with a reason\n", accepted, rejected);
}

DM_TEST("save: garbage, empty and null input is refused without crashing") {
	std::string why;
	ok(!save::decode(nullptr, 0, &why) && !why.empty(), "null input refused with a reason");
	ok(!save::peek(nullptr, 0).has_value(), "peek(null) is nullopt");
	const uint8_t dummy = 0;
	ok(!save::decode(&dummy, 0, &why), "zero length refused");
	Rng r(77);
	int accepted = 0;
	for (int it = 0; it < 500; it++) {
		const size_t len = size_t(r.integer(0, 3000));
		std::vector<uint8_t> junk(len);
		for (size_t k = 0; k < len; k++) junk[k] = uint8_t(r.integer(0, 255));
		if (save::decode(junk.data(), junk.size())) accepted++;
	}
	eq(accepted, 0, "random buffers accepted");
	// Right magic, version, flags and length, a valid checksum, and a random body.
	auto w = richWorld(2, 14);
	const std::vector<uint8_t> good = encodeOk(*w);
	int wrapped = 0;
	for (int it = 0; it < 200; it++) {
		std::vector<uint8_t> m(good.begin(), good.begin() + 20);
		const size_t bodyLen = size_t(r.integer(0, 2000));
		for (size_t k = 0; k < bodyLen; k++) m.push_back(uint8_t(r.integer(0, 255)));
		m.resize(m.size() + 4);
		setLe64(m, 12, uint64_t(m.size()));
		resign(m);
		if (save::decode(m.data(), m.size())) wrapped++;
	}
	eq(wrapped, 0, "random bodies with a valid envelope accepted");
}

// ================================================================== files

DM_TEST("save: files are written atomically and read back") {
	TempDir t("file");
	auto a = richWorld(3, 21);
	std::string why;
	const std::string file = t.file("a.dmsave");
	ok(save::writeFile(*a, file, {}, &why), "writeFile: " + why);
	ok(std::filesystem::exists(file), "the save file exists");
	ok(!std::filesystem::exists(t.file("a.dmsave.tmp")), "no temporary file is left behind");
	eq(readBytes(file), encodeOk(*a), "file bytes equal encode()");

	auto back = save::readFile(file, &why);
	if (!back) { fail("readFile: " + why); return; }
	auto normalized = decodeOk(encodeOk(*a));
	if (!normalized) return;
	eq(hash(*back), hash(*normalized), "readFile gives the saved world");
	sameWorld(*normalized, *back);

	auto b = richWorld(3, 22);
	ok(save::writeFile(*b, file, {}, &why), "overwrite: " + why);
	auto second = save::readFile(file, &why);
	if (!second) { fail("readFile after overwrite: " + why); return; }
	eq(second->name, b->name, "the file now holds the second save");
	ok(save::readMeta(file).has_value(), "readMeta works on the file");

	std::string missingWhy;
	ok(!save::readFile(t.file("missing.dmsave"), &missingWhy), "a missing save is nullptr");
	ok(!missingWhy.empty(), "a missing save gives a reason");
	ok(!save::readMeta(t.file("missing.dmsave")).has_value(), "readMeta of a missing file is nullopt");
	writeText(t.file("junk.dmsave"), "not a save at all");
	ok(!save::readFile(t.file("junk.dmsave")), "a garbage file is nullptr");
	ok(!save::readMeta(t.file("junk.dmsave")).has_value(), "readMeta of a garbage file is nullopt");
	writeText(t.file("empty.dmsave"), "");
	ok(!save::readFile(t.file("empty.dmsave")), "an empty file is nullptr");
	ok(!save::readMeta(t.file("empty.dmsave")).has_value(), "readMeta of an empty file is nullopt");
}

DM_TEST("save: a failed write keeps the previous save") {
	TempDir t("replace");
	auto a = richWorld(3, 31);
	auto b = richWorld(3, 32);
	const std::string file = t.file("slot.dmsave");
	std::string why;
	if (!ok(save::writeFile(*a, file, {}, &why), "first write: " + why)) return;
	const std::vector<uint8_t> before = readBytes(file);
	// A folder where the temporary file must go makes the write fail before anything is replaced.
	std::error_code ec;
	std::filesystem::create_directories(file + ".tmp", ec);
	ok(!save::writeFile(*b, file, {}, &why), "a blocked write is refused");
	ok(!why.empty(), "the refusal gives a reason");
	eq(readBytes(file), before, "the previous save is untouched");
	std::filesystem::remove_all(file + ".tmp", ec);
	ok(save::writeFile(*b, file, {}, &why), "the write succeeds once the way is clear: " + why);
	auto back = save::readFile(file);
	if (back) eq(back->name, b->name, "the new save replaced the old one");
}

DM_TEST("save: unusable storage fails cleanly (folder under a file, folder as file)") {
	TempDir t("storage");
	const std::string blocker = t.file("blocker.txt");
	writeText(blocker, "x");
	const std::string dir = blocker + "/sub";
	auto w = richWorld(2, 41);
	std::string why;
	ok(!save::writeSlot(*w, dir, "auto", {}, &why), "writeSlot under a file is refused");
	ok(!why.empty(), "the refusal gives a reason");
	ok(!save::writeFile(*w, dir + "/x.dmsave"), "writeFile under a file is refused");
	ok(!save::readSlot(dir, "auto"), "readSlot from an unusable folder is nullptr");
	eq(save::listSlots(dir).size(), size_t(0), "listSlots of an unusable folder is empty");
	ok(!save::removeSlot(dir, "auto"), "removeSlot of an unusable folder is false");
	ok(!save::saveSettings(save::defaultSettings(false), dir + "/settings.txt"), "saveSettings under a file is refused");
	const save::UserSettings got = save::loadSettings(dir + "/settings.txt", true);
	ok(got == save::defaultSettings(true), "loadSettings of an unusable path gives the defaults");
	ok(!save::writeFile(*w, t.str()), "a folder is not a save file");
	ok(!std::filesystem::exists(t.str() + ".tmp"), "the refused write left no temporary file");
}

// ================================================================== slots

DM_TEST("save: slot names are checked") {
	TempDir t("slotnames");
	const std::string dir = t.str();
	for (const std::string bad : { std::string(""), std::string("../x"), std::string("a b"), std::string("a/b"),
			std::string("a.b"), std::string("\xC3\xA9t\xC3\xA9"), std::string(65, 'a') }) {
		ok(save::slotPath(dir, bad).empty(), "slotPath refuses \"" + bad + "\"");
	}
	const std::string longest = save::slotPath(dir, std::string(64, 'a'));
	ok(!longest.empty() && has(longest, ".dmsave"), "a 64-character name is accepted");
	const std::string named = save::slotPath(dir, "auto");
	ok(has(named, "auto.dmsave") && has(named, dir.c_str()), "slotPath joins folder, name and extension");
	auto w = richWorld(2, 42);
	std::string why;
	ok(!save::writeSlot(*w, dir, "../x", {}, &why), "writeSlot refuses a path-like name");
	ok(!save::writeSlot(*w, dir, "", {}, &why), "writeSlot refuses an empty name");
	eq(save::listSlots(dir).size(), size_t(0), "refused names write nothing");
}

DM_TEST("save: slots are listed in name order; junk files are skipped") {
	TempDir t("slots");
	const std::string dir = t.str();
	auto a = richWorld(2, 51);
	auto b = richWorld(2, 52);
	auto c = richWorld(2, 53);
	std::string why;
	ok(save::writeSlot(*a, dir, "auto", {}, &why), "write auto: " + why);
	ok(save::writeSlot(*b, dir, "slot-2", {}, &why), "write slot-2: " + why);
	ok(save::writeSlot(*c, dir, "Slot_3", {}, &why), "write Slot_3: " + why);
	writeText(t.file("junk.dmsave"), "garbage, not a save");
	writeText(t.file("notes.txt"), "not a slot");
	writeText(t.file("bad name.dmsave"), "invalid slot name");
	writeText(t.file("half.dmsave.tmp"), "left over");

	const std::vector<save::SlotInfo> slots = save::listSlots(dir);
	std::vector<std::string> names;
	for (const save::SlotInfo& s : slots) names.push_back(s.slot);
	const std::vector<std::string> want = { "Slot_3", "auto", "slot-2" };
	ok(names == want, "listing is sorted by slot name and skips junk");
	if (slots.size() == 3) {
		ok(slots[0].meta == save::metaOf(*c), "meta of Slot_3");
		ok(slots[1].meta == save::metaOf(*a), "meta of auto");
		ok(slots[2].meta == save::metaOf(*b), "meta of slot-2");
	}

	ok(save::removeSlot(dir, "auto"), "removeSlot of an existing slot");
	ok(!save::removeSlot(dir, "auto"), "removeSlot of a missing slot is false");
	ok(!save::readSlot(dir, "auto"), "a removed slot reads as nullptr");
	eq(save::listSlots(dir).size(), size_t(2), "two slots remain");
	auto back = save::readSlot(dir, "Slot_3", &why);
	if (back) eq(hash(*back), hash(*decodeOk(encodeOk(*c))), "readSlot gives the saved world");
	else fail("readSlot Slot_3: " + why);
}

DM_TEST("save: a UTF-8 folder name works") {
	TempDir t("utf8");
	const std::string dir = t.str() + "/dm-" + "\xC3\xA9t\xC3\xA9";
	auto w = richWorld(2, 61);
	std::string why;
	ok(save::writeSlot(*w, dir, "one", {}, &why), "write into a UTF-8 folder: " + why);
	auto back = save::readSlot(dir, "one", &why);
	ok(back != nullptr, "read back from the UTF-8 folder: " + why);
	eq(save::listSlots(dir).size(), size_t(1), "listed from the UTF-8 folder");
}

// ================================================================== settings

DM_TEST("save: settings defaults follow the spec, and reduced motion lowers flashes and shake") {
	const save::UserSettings d = save::defaultSettings(false);
	eq(d.master, 0.8, "master");
	eq(d.music, 0.7, "music");
	eq(d.sfx, 0.9, "sfx");
	eq(d.ambience, 0.8, "ambience");
	eq(d.quality, -1, "quality (auto)");
	eq(d.sun, std::string("orbit"), "sun");
	eq(d.tilt, 1.0, "tilt");
	eq(d.flashes, 1.0, "flashes");
	eq(d.shake, 1.0, "shake");
	eq(d.uiScale, 1.0, "uiScale");
	ok(d.labels && d.hints && d.autoSlow, "labels, hints, autoSlow on");

	save::UserSettings r = save::defaultSettings(true);
	eq(r.flashes, 0.3, "reduced motion: flashes");
	eq(r.shake, 0.3, "reduced motion: shake");
	r.flashes = d.flashes;
	r.shake = d.shake;
	ok(r == d, "reduced motion changes nothing else");
}

DM_TEST("save: settings round trip exactly and clamp out-of-range values") {
	TempDir t("settings");
	save::UserSettings s;
	s.master = 0.33; s.music = 0.125; s.sfx = 0.5; s.ambience = 0.75; s.quality = 2; s.sun = "follow";
	s.tilt = 0.5; s.flashes = 0.4; s.shake = 0.6; s.uiScale = 1.25; s.labels = false; s.hints = false; s.autoSlow = false;
	const std::string file = t.file("settings.txt");
	std::string why;
	ok(save::saveSettings(s, file), "saveSettings");
	ok(!std::filesystem::exists(file + ".tmp"), "no temporary settings file left behind");
	const save::UserSettings back = save::loadSettings(file, false);
	ok(back == s, "every setting survives save and load exactly");

	const save::UserSettings clamped = settingsFrom(t,
		"master=3\nmusic=-2\nuiScale=9\nquality=7\nsun=moon\nflashes=0.25\ntilt=0.5\n");
	eq(clamped.master, 1.0, "master clamped to 1");
	eq(clamped.music, 0.0, "music clamped to 0");
	eq(clamped.uiScale, 2.0, "uiScale clamped to 2");
	eq(clamped.quality, -1, "quality 7 refused (keeps the default)");
	eq(clamped.sun, std::string("orbit"), "sun \"moon\" refused");
	eq(clamped.flashes, 0.25, "flashes kept");
	eq(clamped.tilt, 0.5, "tilt kept");
	const save::UserSettings follow = settingsFrom(t, "sun=follow\n");
	eq(follow.sun, std::string("follow"), "sun \"follow\" accepted");
}

DM_TEST("save: settings merge over the defaults and ignore garbage lines") {
	TempDir t("merge");
	const save::UserSettings got = settingsFrom(t,
		"# DEMIURGE settings\r\n"
		"  master = 0.4  \r\n"
		"music=banana\n"
		"sfx=1e999\n"
		"ambience=nan\n"
		"junk line without an equals sign\n"
		"=no key\n"
		"unknown_key=5\n"
		"quality=2.5\n"
		"labels=maybe\n"
		"hints=0\n"
		"autoSlow=false\n");
	eq(got.master, 0.4, "master read past spaces and a carriage return");
	eq(got.music, 0.7, "music: garbage kept the default");
	eq(got.sfx, 0.9, "sfx: 1e999 is not finite, default kept");
	eq(got.ambience, 0.8, "ambience: nan kept the default");
	eq(got.quality, -1, "quality: 2.5 is not an integer, default kept");
	ok(got.labels, "labels: \"maybe\" kept the default");
	ok(!got.hints, "hints: 0 read as false");
	ok(!got.autoSlow, "autoSlow: false read as false");
	eq(got.tilt, 1.0, "a key the file lacks keeps its default");
	const save::UserSettings bom = settingsFrom(t, "\xEF\xBB\xBFmaster=0.5\n");
	eq(bom.master, 0.5, "a UTF-8 byte-order mark before the first key is ignored");
	const save::UserSettings binary = [&] {
		std::vector<uint8_t> junk(4096);
		Rng r(9);
		for (uint8_t& v : junk) v = uint8_t(r.integer(0, 255));
		writeBytes(t.file("junk.txt"), junk);
		return save::loadSettings(t.file("junk.txt"), true);
	}();
	ok(binary == save::defaultSettings(true), "a binary file gives the defaults (reduced motion applied)");
}

DM_TEST("save: the reduced-motion default survives an unchanged settings file") {
	TempDir t("reduced");
	const std::string file = t.file("settings.txt");
	ok(save::saveSettings(save::defaultSettings(false), file), "save the untouched defaults");
	const std::vector<uint8_t> text = readBytes(file);
	const std::string asText(text.begin(), text.end());
	ok(!has(asText, "flashes=") && !has(asText, "shake="), "an untouched flashes / shake is not written");
	const save::UserSettings reduced = save::loadSettings(file, true);
	eq(reduced.flashes, 0.3, "under reduced motion flashes is 0.3");
	eq(reduced.shake, 0.3, "under reduced motion shake is 0.3");

	save::UserSettings chosen = save::defaultSettings(false);
	chosen.flashes = 0.6;
	ok(save::saveSettings(chosen, file), "save an explicit flashes value");
	const save::UserSettings kept = save::loadSettings(file, true);
	eq(kept.flashes, 0.6, "an explicit flashes value wins over the reduced-motion default");
	eq(kept.shake, 0.3, "shake, never set, still takes the reduced-motion default");
}

DM_TEST("save: missing, oversized settings files give the defaults") {
	TempDir t("missingsettings");
	eq(save::loadSettings(t.file("nothere.txt"), false) == save::defaultSettings(false), true, "missing file: defaults");
	std::string big = "master=0.1\n";
	big.append(70000, '#');
	big.push_back('\n');
	writeText(t.file("big.txt"), big);
	eq(save::loadSettings(t.file("big.txt"), false).master, 0.8, "a 70 KB settings file is ignored (master kept at 0.8)");
}

// ================================================================== size and speed

DM_TEST("save: stored size and speed (numbers for the report)") {
	state::CreateOpts co;
	co.L = 6;
	auto empty6 = state::create(co);
	const size_t blob = state::sizeOf(*empty6);
	const std::vector<uint8_t> bEmpty = encodeOk(*empty6);
	// prefix 20 + meta (4 + name + 4 + 8 + 1 + 4 + 8 + 8) + blob length 4 + frame count 4 + checksum 4
	const size_t overhead = 20 + (4 + empty6->name.size() + 4 + 8 + 1 + 4 + 8 + 8) + 4 + 4 + 4;
	eq(bEmpty.size(), blob + overhead, "container = state blob + fixed overhead (empty L 6 world)");
	std::printf("  empty L6 world: state blob %zu bytes, container %zu bytes\n", blob, bEmpty.size());

	auto rich6 = richWorld(6, 2, false);
	const double t0 = nowMs();
	const std::vector<uint8_t> b6 = encodeOk(*rich6);
	const double t1 = nowMs();
	auto back = decodeOk(b6);
	const double t2 = nowMs();
	const std::vector<uint8_t> b6n = encodeOk(*rich6, save::EncodeOpts{ .snapshots = false });
	std::printf("  L6 populated: container %zu bytes (%zu without frames); encode %.1f ms (includes its read-back), decode %.1f ms\n",
		b6.size(), b6n.size(), t1 - t0, t2 - t1);
	ok(back != nullptr, "L6 populated save loads");
	ok(t1 - t0 < 2000 && t2 - t1 < 2000, "encode and decode stay well under two seconds at L 6");
}

// ================================================================== maps

DM_TEST("save: the loaded planet is the saved one (maps: elev, owner, quantization error)") {
	auto raw = richWorld(6, 4, false);
	const std::vector<uint8_t> b = encodeOk(*raw);
	auto loaded = decodeOk(b);
	if (!loaded) return;
	ok(mi::elev(*loaded, dmt::shot("ue-save-elev-loaded.png")), "write the elevation map of the loaded world");
	ok(mi::owner(*loaded, dmt::shot("ue-save-owner-loaded.png")), "write the territory map of the loaded world");
	const double half = 0.5 * (20.0 - (-12.0)) / 65535.0;
	std::vector<double> err(size_t(raw->n));
	double worst = 0;
	for (size_t c = 0; c < err.size(); c++) {
		err[c] = std::fabs(double(raw->elev[c]) - double(loaded->elev[c]));
		worst = std::max(worst, err[c]);
	}
	ok(mi::field(*loaded, err, dmt::shot("ue-save-elev-error.png"), 0.0, half), "write the quantization error map");
	ok(worst <= half + 1e-6, "elevation error within half a step everywhere (worst " + std::to_string(worst) + ")");
	std::printf("  maps: elev error worst %.6f of half step %.6f\n", worst, half);
	sameCells(*raw, *loaded);                       // reports its own mismatches
	ok(loaded->settlements.size() == raw->settlements.size(), "settlements shown are the saved ones");
	noNaN(*loaded, "after the load");
}
