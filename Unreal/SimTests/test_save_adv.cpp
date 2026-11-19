// DEMIURGE — SimTests/test_save_adv.cpp : adversarial verification of Sim/Save (A15, key "save").
// Written by the save verifier. Everything here runs in `run.ps1 save` (all other modules stubbed);
// the tests whose names start with "save adv: real sim" need the real modules and run only in
// `run.ps1 all` (they return early when Sim is a stub). DM_SAVE_LONG=1 adds a long L 6 history run
// (realistic time-lapse frames, size and timing numbers for the report).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "MapImg.h"
#include "Save.h"
#include "Sim.h"
#include "State.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;
namespace mi = dm::mapimg;

namespace {

// ------------------------------------------------------------------ helpers

uint32_t advCrc(const uint8_t* p, size_t n) {
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i++) {
		c ^= p[i];
		for (int k = 0; k < 8; k++) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
	}
	return c ^ 0xFFFFFFFFu;
}
void advReseal(std::vector<uint8_t>& b) {
	const uint32_t c = advCrc(b.data(), b.size() - 4);
	for (size_t k = 0; k < 4; k++) b[b.size() - 4 + k] = uint8_t(c >> (8 * k));
}
bool contains(const std::string& s, const char* t) { return s.find(t) != std::string::npos; }

std::vector<uint8_t> advRead(const std::string& path) {
	std::vector<uint8_t> b;
	std::ifstream f(std::filesystem::path(path), std::ios::binary);
	if (!f) return b;
	b.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return b;
}
bool advWriteText(const std::string& path, const std::string& t) {
	std::ofstream f(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
	if (!f) return false;
	f.write(t.data(), std::streamsize(t.size()));
	f.close();
	return !f.fail();
}

struct AdvDir {
	std::filesystem::path path;
	explicit AdvDir(const std::string& tag) {
		std::error_code ec;
		path = std::filesystem::temp_directory_path(ec) / ("dm-saveadv-" + tag);
		std::filesystem::remove_all(path, ec);
		std::filesystem::create_directories(path, ec);
	}
	~AdvDir() {
		std::error_code ec;
		std::filesystem::permissions(path, std::filesystem::perms::all, std::filesystem::perm_options::add, ec);
		for (std::filesystem::recursive_directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec)) {
			std::error_code ec2;
			std::filesystem::permissions(it->path(), std::filesystem::perms::all, std::filesystem::perm_options::add, ec2);
		}
		std::filesystem::remove_all(path, ec);
	}
	AdvDir(const AdvDir&) = delete;
	AdvDir& operator=(const AdvDir&) = delete;
	std::string str() const { return path.string(); }
	std::string file(const std::string& nm) const { return (path / nm).string(); }
};

std::vector<uint8_t> enc(const World& w, const save::EncodeOpts& o = {}) {
	std::vector<uint8_t> b;
	std::string why;
	if (!save::encode(w, b, o, &why)) fail("encode refused: " + why);
	return b;
}
std::unique_ptr<World> dec(const std::vector<uint8_t>& b) {
	std::string why;
	std::unique_ptr<World> w = save::decode(b.data(), b.size(), &why);
	if (!w) fail("decode refused: " + why);
	return w;
}

// A frame the way chronicle::snapshot builds one (biome, culture of the owner, urban * 255).
Snapshot chronicleFrame(const World& w) {
	Snapshot f;
	f.year = w.year;
	f.heat = w.heat;
	f.seaLevel = w.seaLevel;
	const size_t n = size_t(w.n);
	f.biome.assign(n, 0);
	f.owner.assign(n, int8_t(-1));
	f.urban.assign(n, 0);
	for (size_t c = 0; c < n; c++) {
		f.biome[c] = w.biome[c];
		const int o = w.owner[c];
		if (o >= 0 && o < int(w.settlements.size())) {
			const int cu = w.settlements[size_t(o)].culture;
			if (cu >= 0) f.owner[c] = int8_t(cu > 127 ? 127 : cu);
		}
		f.urban[c] = uint8_t(std::lround(std::clamp(double(w.urban[c]), 0.0, 1.0) * 255.0));
	}
	return f;
}

// A populated helper world: k cultures, settlements with territory and some urban cells.
std::unique_ptr<World> populated(int L, uint32_t seed, int cultures, int towns, int ringHops) {
	MakeOpts mo;
	mo.L = L;
	mo.seed = seed;
	auto wp = makeWorld(mo);
	World& w = *wp;
	for (int k = 0; k < cultures; k++) {
		Culture& c = state::newCulture(w);
		c.name = "C" + std::to_string(k);
	}
	std::vector<int32_t> ring;
	int made = 0;
	const int stride = std::max(1, w.n / std::max(1, towns));
	int nextAt = 0;
	for (int c = 0; c < w.n && made < towns; c++) {
		if (c < nextAt || state::isSea(w, c)) continue;
		nextAt = c + stride;
		// the last towns go to the highest culture ids (frames clamp ids above 127)
		const int cu = cultures <= 0 ? -1 : (made % 2 == 0 ? made % cultures : cultures - 1 - (made % cultures));
		Settlement& s = state::newSettlement(w, c, cu);
		s.pop = 100 + made;
		const int sid = s.id;
		w.geo->ring(c, ringHops, ring);
		for (int32_t m : ring) {
			if (state::isSea(w, m)) continue;
			w.owner[size_t(m)] = int16_t(sid);
			if (w.geo->angle(c, m) < 1.5 * w.geo->step) w.urban[size_t(m)] = 0.6f;
		}
		made++;
	}
	return wp;
}

} // namespace

// ================================================================== determinism and purity

DM_TEST("save adv: encode is deterministic and pure (same bytes, world untouched, no events)") {
	auto a = populated(4, 3, 3, 8, 3);
	auto b = populated(4, 3, 3, 8, 3);
	a->snapshots.push_back(chronicleFrame(*a));
	b->snapshots.push_back(chronicleFrame(*b));
	const uint32_t h0 = hash(*a);
	const uint32_t rngBefore = a->rng.s;
	a->dirty = Dirty{ false, false, false, false, false, false };
	std::vector<uint8_t> first;
	const std::vector<Event> evs = capture(*a, [&] { first = enc(*a, { .savedAt = 42 }); });
	eq(evs.size(), size_t(0), "encode emits no events");
	eq(hash(*a), h0, "encode leaves the world's persisted state unchanged");
	eq(a->rng.s, rngBefore, "encode draws nothing from w.rng");
	ok(!a->dirty.elev && !a->dirty.color && !a->dirty.objects, "encode sets no dirty flags");
	const std::vector<uint8_t> second = enc(*a, { .savedAt = 42 });
	ok(first == second, "two encodes of one world give identical bytes");
	ok(enc(*b, { .savedAt = 42 }) == first, "two worlds built by the same calls give identical bytes");
	const std::vector<uint8_t> other = enc(*a, { .savedAt = 43 });
	ok(other != first && other.size() == first.size(), "savedAt changes the bytes but not the size");

	auto x = dec(first);
	auto y = dec(first);
	if (!x || !y) return;
	eq(hash(*x), hash(*y), "decoding the same bytes twice gives the same world");
	ok(enc(*x, { .savedAt = 42 }) == enc(*y, { .savedAt = 42 }), "and the same bytes again");
	ok(x->dirty.elev && x->dirty.color && x->dirty.water && x->dirty.objects && x->dirty.rivers && x->dirty.roads, "a loaded world has every dirty flag set");
	ok(x->need.derive && x->need.climate && x->need.hydro && x->need.life, "a loaded world has every need flag set");
	eq(x->bus.count(), size_t(0), "a loaded world has no bus subscribers (the caller runs sim::use)");
	ok(x->rivers.empty(), "rivers are not saved (derived)");
	bool cellsEmpty = true;
	for (const Settlement& s : x->settlements) cellsEmpty = cellsEmpty && s.cells.empty();
	ok(cellsEmpty, "settlement territory caches are not saved");
}

// ================================================================== every phase, every level

DM_TEST("save adv: every phase round-trips, meta and peek follow it") {
	for (int p = 0; p < enumCount<Phase>(); p++) {
		MakeOpts mo;
		mo.L = 3;
		mo.seed = 5 + uint32_t(p);
		mo.phase = Phase(p);
		auto w = makeWorld(mo);
		w->heat = p == 0 ? 0.75 : 0;
		w->year = p >= 4 ? 321.5 : 0;
		const std::vector<uint8_t> b = enc(*w);
		auto back = dec(b);
		if (!back) return;
		eq(int(back->phase), p, "phase " + std::string(toStr(Phase(p))));
		eq(back->heat, w->heat, "heat in phase " + std::string(toStr(Phase(p))));
		const std::optional<save::Meta> m = save::peek(b.data(), b.size());
		ok(m && int(m->phase) == p && m->year == w->year, "peek phase / year for " + std::string(toStr(Phase(p))));
	}
}

DM_TEST("save adv: L 0, 1 and 7 round-trip with frames (runs split past 65,535 cells at L 7)") {
	for (int L : { 0, 1, 7 }) {
		state::CreateOpts co;
		co.L = L;
		co.seed = 9;
		auto w = state::create(co);
		const size_t n = size_t(w->n);
		for (size_t c = 0; c < n; c++) w->elev[c] = float(std::sin(double(c) * 0.37) * 5.0);
		Culture& cu = state::newCulture(*w);
		cu.name = "Solo";
		// frame 0: constant arrays (one run per 65,535 cells); frame 1: alternating (one run per cell)
		for (int f = 0; f < 2; f++) {
			Snapshot s;
			s.year = f;
			s.biome.assign(n, uint8_t(f == 0 ? 3 : 0));
			s.owner.assign(n, int8_t(f == 0 ? 0 : -1));
			s.urban.assign(n, 0);
			if (f == 1) for (size_t c = 0; c < n; c++) { s.biome[c] = uint8_t(c % 2); s.owner[c] = int8_t(c % 2 ? 0 : -1); s.urban[c] = uint8_t(c); }
			w->snapshots.push_back(std::move(s));
		}
		const std::vector<uint8_t> b = enc(*w);
		auto back = dec(b);
		if (!back) return;
		eq(back->n, w->n, "L " + std::to_string(L) + " cell count");
		bool same = back->snapshots.size() == 2;
		for (size_t f = 0; same && f < 2; f++)
			same = back->snapshots[f].biome == w->snapshots[f].biome && back->snapshots[f].owner == w->snapshots[f].owner &&
				back->snapshots[f].urban == w->snapshots[f].urban;
		ok(same, "L " + std::to_string(L) + ": frames come back byte-identical");
		double worst = 0;
		for (size_t c = 0; c < n; c++) worst = std::max(worst, std::fabs(double(back->elev[c]) - double(w->elev[c])));
		ok(worst <= 0.5 * 32.0 / 65535.0 + 1e-6, "L " + std::to_string(L) + ": elev within half a step (worst " + std::to_string(worst) + ")");
		std::printf("  L%d: n %zu, container %zu bytes\n", L, n, b.size());
	}
}

// ================================================================== degenerate worlds

DM_TEST("save adv: no sea, no land, empty and all-dead worlds round-trip") {
	{
		auto w = makeWorld({ .L = 3, .seed = 2 });
		w->seaLevel = -12;                                  // every cell land
		fillBasics(*w);
		auto back = dec(enc(*w));
		ok(back && back->seaLevel == -12.0, "a world with no sea");
	}
	{
		auto w = makeWorld({ .L = 3, .seed = 2 });
		w->seaLevel = 20;                                   // every cell sea
		fillBasics(*w);
		auto back = dec(enc(*w));
		ok(back && back->seaLevel == 20.0, "a world with no land");
	}
	{
		auto w = state::create({ .seed = 0, .name = "", .L = 2 });
		auto back = dec(enc(*w));
		ok(back && back->name.empty() && back->settlements.empty() && back->cultures.empty() && back->units.empty() &&
			back->disasters.empty() && back->chronicle.empty() && back->snapshots.empty() && !back->ending,
			"a bare world with an empty name and no entities");
	}
	{
		auto w = populated(3, 4, 2, 4, 1);
		for (Settlement& s : w->settlements) { s.dead = true; s.deadYear = 10; s.cause = Cause::Famine; }
		for (Culture& c : w->cultures) c.dead = true;
		std::fill(w->owner.begin(), w->owner.end(), int16_t(-1));
		auto back = dec(enc(*w));
		bool allDead = back && !back->settlements.empty();
		if (back) for (const Settlement& s : back->settlements) allDead = allDead && s.dead && s.cause == Cause::Famine && s.deadYear == 10.0;
		ok(allDead, "a world where every settlement and culture is dead");
	}
	{
		// entities on the poles
		auto w = makeWorld({ .L = 4, .seed = 6 });
		int north = 0, south = 0;
		for (int c = 0; c < w->n; c++) {
			if (w->geo->lat[size_t(c)] > w->geo->lat[size_t(north)]) north = c;
			if (w->geo->lat[size_t(c)] < w->geo->lat[size_t(south)]) south = c;
		}
		Culture& cu = state::newCulture(*w);
		cu.name = "Polar";
		state::newSettlement(*w, north, 0).name = "North";
		state::newSettlement(*w, south, 0).name = "South";
		Unit& u = state::newUnit(*w, UnitType::Trader);
		u.culture = 0; u.from = 0; u.to = 1; u.dest = south; u.path = { int32_t(north), int32_t(south) };
		state::newDisaster(*w, DisasterType::Storm, north).ang = 0.2;
		auto back = dec(enc(*w));
		ok(back && back->settlements[0].cell == north && back->settlements[1].cell == south && back->units.size() == 1 &&
			back->disasters.size() == 1 && back->disasters[0].cell == north, "settlements, units and storms on the poles");
	}
}

// ================================================================== NaN and bad values

DM_TEST("save adv: NaN handling (cells and entities are cleaned, scalars refuse, frames cleaned)") {
	const double nan = std::numeric_limits<double>::quiet_NaN();
	const double inf = std::numeric_limits<double>::infinity();
	{
		auto w = populated(3, 8, 2, 3, 1);
		w->elev[5] = std::numeric_limits<float>::quiet_NaN();
		w->veg[6] = std::numeric_limits<float>::infinity();
		w->settlements[0].pop = nan;
		w->cultures[0].science = inf;
		Snapshot s = chronicleFrame(*w);
		s.year = nan;
		s.pop = -inf;
		w->snapshots.push_back(s);
		std::string why;
		std::vector<uint8_t> b;
		const bool saved = save::encode(*w, b, {}, &why);
		ok(saved, "a world with NaN in a cell, an entity and a frame still saves: " + why);
		if (saved) {
			auto back = dec(b);
			if (back) {
				noNaN(*back, "the loaded world has no NaN");
				ok(std::isfinite(back->elev[5]) && std::isfinite(back->veg[6]), "the NaN cells load finite");
				eq(back->settlements[0].pop, 0.0, "NaN pop loads as 0");
				ok(back->snapshots.size() == 1 && back->snapshots[0].year == 0.0 && back->snapshots[0].pop == 0.0, "the NaN frame numbers load as 0");
			}
		}
	}
	for (int k = 0; k < 4; k++) {
		auto w = populated(3, 8, 2, 3, 1);
		const char* what = "";
		if (k == 0) { w->heat = nan; what = "heat NaN"; }
		if (k == 1) { w->year = inf; what = "year infinite"; }
		if (k == 2) { w->stats.pop = nan; what = "stats.pop NaN"; }
		if (k == 3) { w->seaLevel = -inf; what = "seaLevel -infinite"; }
		AdvDir t(std::string("nan") + std::to_string(k));
		auto good = populated(3, 8, 2, 3, 1);
		const std::string file = t.file("keep.dmsave");
		ok(save::writeFile(*good, file), "the good save");
		const std::vector<uint8_t> before = advRead(file);
		std::string why;
		std::vector<uint8_t> b;
		ok(!save::encode(*w, b, {}, &why) && b.empty() && !why.empty(), std::string(what) + ": refused with a reason (" + why + ")");
		ok(!save::writeFile(*w, file, {}, &why), std::string(what) + ": writeFile refuses");
		ok(advRead(file) == before, std::string(what) + ": the previous file is kept");
		ok(!std::filesystem::exists(file + ".tmp"), std::string(what) + ": no temporary file");
	}
}

DM_TEST("save adv: frames from more than 128 cultures (chronicle clamps owner to 127) still save") {
	auto w = populated(3, 12, 200, 40, 1);
	Snapshot s = chronicleFrame(*w);
	int clamped = 0;
	for (int8_t o : s.owner) if (o == 127) clamped++;
	w->snapshots.push_back(s);
	auto back = dec(enc(*w));
	ok(back && back->snapshots.size() == 1 && back->snapshots[0].owner == s.owner, "frame with owner 127 (clamped) loads");
	ok(back && back->cultures.size() == 200, "200 cultures round-trip");
	ok(clamped > 0, "the frame really holds clamped owners");
	std::printf("  200 cultures: %d frame cells clamped to 127\n", clamped);
}

DM_TEST("save adv: world name length limit is exact (65,536 accepted, 65,537 refused); NUL inside a name") {
	auto w = makeWorld({ .L = 2, .seed = 3 });
	w->name.assign(65536, 'x');
	std::string why;
	std::vector<uint8_t> b;
	ok(save::encode(*w, b, {}, &why), "65,536-byte name: " + why);
	w->name.push_back('x');
	ok(!save::encode(*w, b, {}, &why) && b.empty(), "65,537-byte name refused: " + why);
	w->name = std::string("A\0B", 3);
	auto back = dec(enc(*w));
	ok(back && back->name == std::string("A\0B", 3), "a NUL byte inside a name survives");
}

// ================================================================== crafted containers

DM_TEST("save adv: crafted containers under a valid checksum are refused with the right reason") {
	auto w = populated(3, 15, 2, 3, 1);
	w->snapshots.push_back(chronicleFrame(*w));
	const std::vector<uint8_t> good = enc(*w);
	const size_t metaAt = 20;
	const size_t nameLen = w->name.size();
	const size_t phaseAt = metaAt + 4 + nameLen + 4 + 8;
	const size_t ageAt = phaseAt + 1;
	const size_t popAt = ageAt + 4;
	const size_t blobLenAt = popAt + 8 + 8;
	struct Case { const char* name; std::vector<uint8_t> b; const char* reason; bool peekOk; };
	std::vector<Case> cases;
	{ auto m = good; m[phaseAt] = 9; advReseal(m); cases.push_back({ "meta phase 9", m, "header", false }); }
	{ auto m = good; m[ageAt] = 7; advReseal(m); cases.push_back({ "meta age 7", m, "header", false }); }
	{ auto m = good; m[ageAt] = 2; advReseal(m); cases.push_back({ "meta age disagrees with the world", m, "disagrees", true }); }
	{ auto m = good; m[metaAt + 4] ^= 0x20; advReseal(m); cases.push_back({ "meta name disagrees", m, "disagrees", true }); }
	{ auto m = good; m[popAt + 7] = 0x7F; m[popAt + 6] = 0xF8; advReseal(m); cases.push_back({ "meta pop NaN", m, "header", false }); }
	{ auto m = good; m[blobLenAt + 3] = 0x7F; advReseal(m); cases.push_back({ "world blob length past the end", m, "truncated", true }); }
	{ auto m = good; m[8] = 0; advReseal(m); cases.push_back({ "frames present but flag cleared", m, "trailing", true }); }
	{
		// the frame count lies: one more frame than the bytes hold
		auto m = good;
		const uint32_t blobLen = uint32_t(m[blobLenAt]) | (uint32_t(m[blobLenAt + 1]) << 8) | (uint32_t(m[blobLenAt + 2]) << 16) | (uint32_t(m[blobLenAt + 3]) << 24);
		const size_t countAt = blobLenAt + 4 + blobLen;
		m[countAt] = 2;
		advReseal(m);
		cases.push_back({ "frame count 2 for one frame", m, "snapshot", true });
		// first run of the biome list made longer than the map
		auto m2 = good;
		const size_t firstRunLen = countAt + 4 + 32 + 4 + 1;
		m2[firstRunLen] = 0xFF; m2[firstRunLen + 1] = 0xFF;
		advReseal(m2);
		cases.push_back({ "a run longer than the map", m2, "snapshot", true });
		auto m3 = good;
		m3[firstRunLen] = 0; m3[firstRunLen + 1] = 0;
		advReseal(m3);
		cases.push_back({ "a run of length 0", m3, "snapshot", true });
		auto m4 = good;
		const size_t yearAt = countAt + 4;
		m4[yearAt + 7] = 0x7F; m4[yearAt + 6] = 0xF0;     // +infinity
		for (size_t k = 0; k < 6; k++) m4[yearAt + k] = 0;
		advReseal(m4);
		cases.push_back({ "frame year infinite", m4, "finite", true });
	}
	for (const Case& c : cases) {
		std::string why;
		const bool loaded = save::decode(c.b.data(), c.b.size(), &why) != nullptr;
		ok(!loaded, std::string(c.name) + ": refused");
		ok(contains(why, c.reason), std::string(c.name) + ": reason \"" + why + "\" mentions \"" + c.reason + "\"");
		eq(save::peek(c.b.data(), c.b.size()).has_value(), c.peekOk, std::string(c.name) + ": peek");
	}
	std::printf("  %zu crafted containers refused\n", cases.size());
}

// ================================================================== files and slots

DM_TEST("save adv: paths with a NUL byte or invalid UTF-8 are refused; an empty folder is not the working directory") {
	AdvDir t("nul");
	auto w = makeWorld({ .L = 2, .seed = 4 });
	std::string why;
	const std::string nulPath = t.file("a") + std::string("\0b.dmsave", 9);
	ok(!save::writeFile(*w, nulPath, {}, &why), "a path with a NUL byte is refused: " + why);
	ok(!std::filesystem::exists(t.file("a")), "and nothing is written at the truncated name");
	ok(!save::readFile(nulPath), "readFile refuses it");
	ok(!save::writeFile(*w, t.file("bad\xFF.dmsave"), {}, &why), "invalid UTF-8 refused: " + why);
	ok(save::slotPath("", "auto").empty(), "slotPath with an empty folder is refused");
	const bool there = std::filesystem::exists("auto.dmsave");
	ok(!save::writeSlot(*w, "", "auto", {}, &why), "writeSlot to an empty folder is refused");
	if (!there && std::filesystem::exists("auto.dmsave")) {
		fail("a slot landed in the working directory");
		std::error_code ec;
		std::filesystem::remove("auto.dmsave", ec);              // keep the tree clean
	}
	ok(save::slotPath(std::string("x\0y", 3), "auto").empty(), "slotPath with a NUL in the folder is refused");
	ok(!save::saveSettings(save::defaultSettings(), t.file("s") + std::string("\0.txt", 5)), "saveSettings refuses a NUL path");
}

DM_TEST("save adv: stale temp files, read-only targets, folders named like slots") {
	AdvDir t("files");
	auto a = populated(3, 21, 1, 2, 1);
	auto b = populated(3, 22, 1, 2, 1);
	std::string why;
	const std::string file = t.file("s.dmsave");
	advWriteText(file + ".tmp", "left over from a crash");
	ok(save::writeFile(*a, file, {}, &why), "a stale .tmp is overwritten: " + why);
	ok(!std::filesystem::exists(file + ".tmp"), "no .tmp afterwards");
	auto back = save::readFile(file);
	ok(back && back->seed == a->seed, "the save is the new one");

	// read-only target: the replace fails, the old save stays, no temp file is left
	const std::vector<uint8_t> before = advRead(file);
	std::error_code ec;
	std::filesystem::permissions(file, std::filesystem::perms::owner_write | std::filesystem::perms::group_write | std::filesystem::perms::others_write,
		std::filesystem::perm_options::remove, ec);
	const bool wrote = save::writeFile(*b, file, {}, &why);
	ok(!wrote, "writing over a read-only save is refused");
	ok(advRead(file) == before, "the read-only save is unchanged");
	ok(!std::filesystem::exists(file + ".tmp"), "no .tmp left after the refused replace");
	std::filesystem::permissions(file, std::filesystem::perms::owner_write, std::filesystem::perm_options::add, ec);

	// a folder named like a slot
	std::filesystem::create_directories(t.file("ghost.dmsave"), ec);
	ok(!save::readSlot(t.str(), "ghost"), "a folder is not a slot");
	ok(!save::removeSlot(t.str(), "ghost"), "removeSlot does not delete a folder");
	ok(std::filesystem::is_directory(t.file("ghost.dmsave")), "the folder is still there");
	const std::vector<save::SlotInfo> list = save::listSlots(t.str());
	ok(list.size() == 1 && list[0].slot == "s", "listSlots shows only the real slot");
	ok(save::listSlots(t.file("nowhere")).empty(), "listSlots of a missing folder is empty");
}

DM_TEST("save adv: a save written in one call is fully replaced by the next (sizes shrink and grow)") {
	AdvDir t("sizes");
	auto big = populated(4, 31, 2, 6, 2);
	for (int k = 0; k < 6; k++) big->snapshots.push_back(chronicleFrame(*big));
	auto small = makeWorld({ .L = 2, .seed = 32 });
	std::string why;
	ok(save::writeSlot(*big, t.str(), "auto", {}, &why), "big: " + why);
	ok(save::writeSlot(*small, t.str(), "auto", {}, &why), "small: " + why);
	auto back = save::readSlot(t.str(), "auto", &why);
	ok(back && back->n == small->n && back->snapshots.empty(), "the smaller save replaced the bigger one completely: " + why);
	ok(save::writeSlot(*big, t.str(), "auto", {}, &why), "big again: " + why);
	back = save::readSlot(t.str(), "auto", &why);
	ok(back && back->snapshots.size() == 6, "and the bigger one replaced it again");
}

// ================================================================== settings

DM_TEST("save adv: settings cannot be corrupted through string or non-finite values") {
	AdvDir t("settings");
	const std::string file = t.file("settings.txt");
	save::UserSettings s = save::defaultSettings();
	s.sun = "orbit\nmaster=0";                              // a bad value must not inject a line
	s.music = std::numeric_limits<double>::quiet_NaN();
	s.uiScale = std::numeric_limits<double>::infinity();
	ok(save::saveSettings(s, file), "saveSettings");
	const save::UserSettings back = save::loadSettings(file);
	eq(back.master, 0.8, "master was not injected");
	eq(back.sun, std::string("orbit"), "sun is a valid value");
	eq(back.music, 0.7, "NaN music loads as the default");
	eq(back.uiScale, 1.0, "infinite uiScale loads as the default");
	ok(std::isfinite(back.tilt) && std::isfinite(back.flashes), "every number finite");

	// every value at its extremes round-trips
	save::UserSettings e;
	e.master = 0; e.music = 1; e.sfx = 0; e.ambience = 1; e.quality = 0; e.sun = "follow"; e.tilt = 0;
	e.flashes = 0; e.shake = 0; e.uiScale = 0.5; e.labels = false; e.hints = true; e.autoSlow = false;
	ok(save::saveSettings(e, file), "save extremes");
	ok(save::loadSettings(file, true) == e, "extremes round-trip exactly (also under reduced motion)");
	// determinism of the text
	const std::vector<uint8_t> t1 = advRead(file);
	ok(save::saveSettings(e, file), "save again");
	ok(advRead(file) == t1, "the settings text is deterministic");
}

// ================================================================== size and speed with realistic frames

DM_TEST("save adv: L 6 with 200 chronicle-style frames (size and time)") {
	auto w = populated(6, 7, 5, 24, 4);
	// 200 frames as Chronicle would take them while territory and towns grow
	std::vector<int32_t> ring;
	for (int f = 0; f < 200; f++) {
		w->year = 25.0 * f;
		if (f % 10 == 0) {
			const int sid = (f / 10) % int(w->settlements.size());
			w->geo->ring(w->settlements[size_t(sid)].cell, 4 + f / 40, ring);
			for (int32_t m : ring) if (!state::isSea(*w, m)) w->owner[size_t(m)] = int16_t(sid);
		}
		w->snapshots.push_back(chronicleFrame(*w));
	}
	std::vector<uint8_t> b;
	std::string why;
	const double t0 = nowMs();
	const bool okEnc = save::encode(*w, b, {}, &why);
	const double t1 = nowMs();
	ok(okEnc, "encode: " + why);
	std::unique_ptr<World> back;
	if (okEnc) back = save::decode(b.data(), b.size(), &why);
	const double t2 = nowMs();
	ok(back != nullptr, "decode: " + why);
	if (back) {
		bool same = back->snapshots.size() == w->snapshots.size();
		for (size_t f = 0; same && f < w->snapshots.size(); f++) {
			const Snapshot& x = w->snapshots[f];
			const Snapshot& y = back->snapshots[f];
			same = x.year == y.year && x.biome == y.biome && x.owner == y.owner && x.urban == y.urban;
		}
		ok(same, "all 200 frames come back byte-identical");
	}
	const std::vector<uint8_t> noFrames = enc(*w, { .snapshots = false });
	const double perFrame = double(b.size() - noFrames.size()) / 200.0;
	std::printf("  L6, 200 frames: %zu bytes (%zu without frames, %.0f bytes per frame); encode %.1f ms (with read-back), decode %.1f ms\n",
		b.size(), noFrames.size(), perFrame, t1 - t0, t2 - t1);
	ok(t1 - t0 < 1000 && t2 - t1 < 1000, "encode and decode with 200 frames under a second each");
}

// ================================================================== the real simulation (run.ps1 all only)

DM_TEST("save adv: real sim: a loaded world continues deterministically and stays sane") {
	if (stub::stubbed("sim") || stub::stubbed("chronicle")) return;     // needs the real modules
	sim::WorldOpts wo;
	wo.seed = 11;
	wo.L = 4;
	wo.name = "Probe";
	auto w = sim::create(wo);
	if (!ok(w != nullptr, "sim::create")) return;
	watch(*w);
	if (!ok(sim::genesis::skip(*w), "genesis skip")) return;
	sim::advance(*w, 4 * 300);                              // 300 years of history
	const std::vector<uint8_t> b = enc(*w, { .savedAt = 7 });
	if (b.empty()) return;
	auto x = dec(b);
	auto y = dec(b);
	if (!x || !y) return;
	watch(*x);
	watch(*y);
	sim::use(*x);
	sim::use(*y);
	eq(sim::fingerprint(*x), sim::fingerprint(*y), "two loads of one save are identical after sim::use");
	sim::advance(*x, 4 * 100);
	sim::advance(*y, 4 * 100);
	eq(sim::fingerprint(*x), sim::fingerprint(*y), "and stay identical 100 years later");
	sim::advance(*w, 4 * 100);
	noNaN(*x, "the loaded world after 100 years");
	const sim::Sample sw = sim::sample(*w);
	const sim::Sample sx = sim::sample(*x);
	std::printf("  real sim L4: save %zu bytes with %zu frames; 100 y later original pop %.0f in %d settlements, loaded pop %.0f in %d\n",
		b.size(), w->snapshots.size(), sw.pop, sw.settlements, sx.pop, sx.settlements);
	ok(sx.settlements > 0 || sw.settlements == 0, "the loaded world still has people when the original does");
	ok(x->snapshots.size() >= y->snapshots.size() && !x->snapshots.empty(), "chronicle keeps taking frames after the load");
	// a second save of the continued world is a fixed point
	auto z = dec(enc(*x));
	if (z) eq(hash(*z), hash(*dec(enc(*z))), "save of the continued world is a fixed point");
}

DM_TEST("save adv: real sim: long L 6 run, realistic frames (DM_SAVE_LONG=1)") {
	if (stub::stubbed("sim") || stub::stubbed("chronicle")) return;
	if (!std::getenv("DM_SAVE_LONG")) return;
	sim::WorldOpts wo;
	wo.seed = 3;
	wo.L = 6;
	auto w = sim::create(wo);
	if (!w || !sim::genesis::skip(*w)) { fail("create / skip"); return; }
	const int years[] = { 500, 1500, 3000 };
	int done = 0;
	for (int y : years) {
		sim::advance(*w, 4 * (y - done));
		done = y;
		std::vector<uint8_t> b;
		std::string why;
		const double t0 = nowMs();
		const bool okEnc = save::encode(*w, b, { .savedAt = 1 }, &why);
		const double t1 = nowMs();
		auto back = okEnc ? save::decode(b.data(), b.size(), &why) : nullptr;
		const double t2 = nowMs();
		ok(okEnc && back, "year " + std::to_string(y) + ": " + why);
		const std::vector<uint8_t> nf = enc(*w, { .snapshots = false });
		std::printf("  real L6 year %d: %zu frames, %zu bytes (%zu without frames, %.0f per frame); encode %.1f ms, decode %.1f ms; pop %.0f\n",
			y, w->snapshots.size(), b.size(), nf.size(), w->snapshots.empty() ? 0.0 : double(b.size() - nf.size()) / double(w->snapshots.size()),
			t1 - t0, t2 - t1, w->stats.pop);
		{
			// measurement: run counts of the frames as stored, and of each frame XOR the previous one
			size_t plainRuns = 0, deltaRuns = 0, changed = 0;
			auto runsOf = [](const std::vector<uint8_t>& v) {
				size_t r = 0;
				for (size_t i = 0; i < v.size(); i++) if (i == 0 || v[i] != v[i - 1]) r++;
				return r;
			};
			for (size_t f = 0; f < w->snapshots.size(); f++) {
				const Snapshot& s = w->snapshots[f];
				std::vector<uint8_t> o(s.owner.size());
				for (size_t c = 0; c < o.size(); c++) o[c] = uint8_t(s.owner[c]);
				plainRuns += runsOf(s.biome) + runsOf(o) + runsOf(s.urban);
				std::vector<uint8_t> db = s.biome, dob = o, du = s.urban;
				if (f > 0) {
					const Snapshot& p = w->snapshots[f - 1];
					for (size_t c = 0; c < db.size(); c++) {
						db[c] ^= p.biome[c]; dob[c] ^= uint8_t(p.owner[c]); du[c] ^= p.urban[c];
						changed += (db[c] != 0) + (dob[c] != 0) + (du[c] != 0);
					}
				}
				deltaRuns += runsOf(db) + runsOf(dob) + runsOf(du);
			}
			std::printf("    runs: plain %zu (%.0f KB), delta %zu (%.0f KB); changed array cells per frame %.0f\n", plainRuns,
				plainRuns * 3 / 1024.0, deltaRuns, deltaRuns * 3 / 1024.0, w->snapshots.size() > 1 ? double(changed) / double(w->snapshots.size() - 1) : 0.0);
		}
		if (back && y == 3000) {
			// the time-lapse frames as loaded: the first history frame and the last one, owner by culture colour
			bool framesSame = back->snapshots.size() == w->snapshots.size();
			for (size_t f = 0; framesSame && f < w->snapshots.size(); f++)
				framesSame = back->snapshots[f].biome == w->snapshots[f].biome && back->snapshots[f].owner == w->snapshots[f].owner &&
					back->snapshots[f].urban == w->snapshots[f].urban && back->snapshots[f].year == w->snapshots[f].year;
			ok(framesSame, "every real frame comes back byte-identical");
			for (size_t pick : { size_t(1), back->snapshots.size() - 1 }) {
				if (pick >= back->snapshots.size()) continue;
				const Snapshot& s = back->snapshots[pick];
				const std::string file = shot("ue-save-adv-frame-y" + std::to_string(int(s.year)) + ".png");
				mi::custom(*back, file, [&](int c) {
					const int o = s.owner[size_t(c)];
					if (o >= 0) {
						const auto& col = kCultureColors[size_t(o) % kCultureColors.size()];
						const double u = s.urban[size_t(c)] / 255.0;
						return mi::Rgb{ 255 * (col[0] + (1 - col[0]) * u), 255 * (col[1] + (1 - col[1]) * u), 255 * (col[2] + (1 - col[2]) * u) };
					}
					const bool sea = s.biome[size_t(c)] == Biome::SEA || s.biome[size_t(c)] == Biome::SHALLOWS || s.biome[size_t(c)] == Biome::SEAICE;
					return sea ? mi::Rgb{ 20, 30, 90 } : mi::Rgb{ 76, 82, 76 };   // mapimg colours are 0..255
				}, mi::Opts{ .rivers = false, .lakes = false });
				std::printf("    frame %zu (year %.0f) written to %s\n", pick, s.year, file.c_str());
			}
			sim::use(*back);
			mi::owner(*back, shot("ue-save-adv-owner-loaded-y3000.png"));
			mi::biome(*back, shot("ue-save-adv-biome-loaded-y3000.png"));
			mi::owner(*w, shot("ue-save-adv-owner-orig-y3000.png"));
		}
	}
}
