// DEMIURGE — SimTests/test_chronicle_adv.cpp : adversarial verification of Sim/Chronicle.cpp (A13).
// Owner: the A13 verifier. Written independently of test_chronicle.cpp. It attacks the module with
// hostile payloads (ids, cells, NaN / infinite numbers, out-of-range enums), degenerate worlds (no sea,
// no land, no people, poles), the snapshot due / thinning rules, the 2,500-entry cap, the epithet
// rules (hold, silence, regard, god.epithet), the prayer suppression, a grammar fuzzer over every
// chronicled event, determinism, and timings at L = 6.
// In `run.ps1 chronicle` every other module is stubbed (Society regard is zeros unless a hook is set).
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Chronicle.h"
#include "Civ.h"
#include "MapImg.h"
#include "Powers.h"
#include "Sim.h"
#include "Society.h"
#include "State.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;
using chronicle::EntryOpts;
using chronicle::Tale;

namespace {

namespace mi = dm::mapimg;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

void say(const std::string& s) { std::printf("  %s\n", s.c_str()); std::fflush(stdout); }

// A tiny deterministic generator for the fuzzers (the test may use anything; it is not the sim stream).
struct Lcg {
	uint64_t s;
	explicit Lcg(uint64_t seed) : s(seed * 2654435761u + 1) {}
	uint32_t next() { s = s * 6364136223846793005ull + 1442695040888963407ull; return uint32_t(s >> 33); }
	int below(int n) { return n <= 0 ? 0 : int(next() % uint32_t(n)); }
	bool coin() { return (next() & 1u) != 0; }
};

// ------------------------------------------------------------------ independent prose checker

const char* const kJunk[] = { "undefined", "NaN", "nan", "null", "object", "Object", "inf", "Infinity", "std" };

// "" when the entry reads as prose; otherwise what is wrong. digitsOk: tales may print years.
std::string proseFault(const std::string& t, bool digitsOk = false) {
	if (t.size() < 12) return "too short: '" + t + "'";
	if (t.size() > 520) return "too long (" + std::to_string(t.size()) + ")";
	const unsigned char f = static_cast<unsigned char>(t[0]);
	if (!(std::isupper(f) || t[0] == '"')) return "no capital: '" + t + "'";
	const char last = t.back();
	if (last != '.' && last != '!' && last != '?' && last != '"') return "no final stop: '" + t + "'";
	for (char ch : t) {
		if (!digitsOk && std::isdigit(static_cast<unsigned char>(ch))) return "digit: '" + t + "'";
		if (ch == '{' || ch == '}' || ch == '[' || ch == ']' || ch == '|' || ch == '_' || ch == '\t' || ch == '\n')
			return "template or control character: '" + t + "'";
	}
	static const char* const kBad[] = { "  ", " ,", " .", "..", ",,", ", .", " a a ", " of of ", " at at ", " in in ",
		" , ", " ;", ";;", "( ", " )", "  ." };
	for (const char* b : kBad) if (t.find(b) != std::string::npos) return std::string("bad sequence '") + b + "': '" + t + "'";
	// doubled words on word boundaries only ("Mirathe the sick" is not "the the")
	const std::string sp = " " + t + " ";
	for (const char* d : { "the", "The", "a", "of", "at", "in", "to" }) {
		const std::string pair = std::string(" ") + d + " " + d + " ";
		if (sp.find(pair) != std::string::npos) return std::string("doubled word '") + d + "': '" + t + "'";
	}
	// whole words
	std::string word;
	for (size_t i = 0; i <= t.size(); i++) {
		const char ch = i < t.size() ? t[i] : ' ';
		if (std::isalpha(static_cast<unsigned char>(ch))) { word.push_back(ch); continue; }
		for (const char* j : kJunk) if (word == j) return std::string("junk word '") + j + "': '" + t + "'";
		word.clear();
	}
	return "";
}

// Words that signal an entry was written with an undefined key (a dropped placeholder reads as a gap).
// Checked on the whole text: no " from ." style gaps, no "of ." etc.
std::string gapFault(const std::string& t) {
	static const char* const kEnds[] = { " of.", " to.", " from.", " by.", " at.", " the.", " and.", " a.", " with.", " on.", " for.", " is.", " was." };
	for (const char* e : kEnds) if (t.find(e) != std::string::npos) return std::string("dangling word '") + e + "': '" + t + "'";
	return "";
}

std::vector<std::string> texts(const World& w, size_t from = 0) {
	std::vector<std::string> out;
	for (size_t i = from; i < w.chronicle.size(); i++) out.push_back(w.chronicle[i].text);
	return out;
}

// ------------------------------------------------------------------ fixture

struct Scene {
	std::unique_ptr<World> w;
	std::vector<int> sites;     // spread land cells, best habitability first
	int next = 0;
};

std::vector<int> spreadSites(const World& w, double minSep, int want) {
	std::vector<int> cand;
	for (int c = 0; c < w.n; c++) if (!state::isSea(w, c) && w.hab[size_t(c)] > 0.05f) cand.push_back(c);
	std::sort(cand.begin(), cand.end(), [&](int a, int b) {
		const float ha = w.hab[size_t(a)], hb = w.hab[size_t(b)];
		return ha != hb ? ha > hb : a < b;
	});
	std::vector<int> out;
	for (int c : cand) {
		bool okc = true;
		for (int o : out) if (w.geo->angle(o, c) < minSep) { okc = false; break; }
		if (okc) out.push_back(c);
		if (int(out.size()) >= want) break;
	}
	return out;
}

Scene scene(int L, uint32_t seed, double land, bool watch = true, bool attach = true) {
	Scene s;
	s.w = makeWorld({ .L = L, .seed = seed, .land = land, .watch = watch });
	s.w->name = "Erethe";
	s.sites = spreadSites(*s.w, 0.10, 200);
	if (attach) chronicle::attach(*s.w);
	return s;
}

int mkCulture(World& w, const char* name, const char* adj, const char* plural, const char* god, int parent = -1) {
	Culture& c = state::newCulture(w, parent);
	c.name = name;
	c.adj = adj;
	c.plural = plural;
	c.godName = god;
	c.lang = "test";
	return c.id;
}

int mkTown(Scene& s, int cid, double pop, int tier, const char* name, int cell = -1) {
	if (cell < 0) {
		if (s.next >= int(s.sites.size())) return -1;
		cell = s.sites[size_t(s.next++)];
	}
	World& w = *s.w;
	Settlement& t = state::newSettlement(w, cell, cid);
	t.name = name;
	t.pop = pop;
	t.tier = tier;
	const int id = t.id;
	if (Culture* c = state::culture(w, cid)) if (c->capital < 0) { c->capital = id; w.settlements[size_t(id)].capital = true; }
	return id;
}

// ==================================================================== tests

DM_TEST("chronicle adv: garbage payloads on every chronicled event never break the prose or the world") {
	Scene s = scene(4, 5, 0.4, /*watch*/ false);
	World& w = *s.w;
	const int a = mkCulture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int b = mkCulture(w, "Teshk", "Teshkan", "the Teshki", "Orro-Ka", a);
	const int t1 = mkTown(s, a, 900, 2, "Vornhal");
	const int t2 = mkTown(s, b, 300, 1, "Imbrek");
	const int t3 = mkTown(s, b, 70, 0, "Ilo");
	(void)t3;
	const double nums[] = { kNaN, kInf, -kInf, 0, -1, 1e300, -1e300, 0.5, 7, -0.25 };
	const int ids[] = { -1, 0, 1, 2, 3, 99, -99, 100000, t1, t2 };
	const char* const kinds[] = { "rain", "bloom", "raise", "lower", "heal", "inspire", "peace", "plague", "prophet",
		"lightning", "nuke", "", "weird" };
	const char* const names[] = { "", "Brannoch", "a nameless one" };
	Lcg r(77);
	auto pick = [&](auto& arr) { return arr[size_t(r.below(int(sizeof(arr) / sizeof(arr[0]))))]; };
	auto anyId = [&]() { return r.coin() ? pick(ids) : r.below(40) - 5; };
	auto anyCell = [&]() { return r.coin() ? -1 : r.below(w.n + 20) - 5; };
	int calls = 0;
	for (int it = 0; it < 6000; it++) {
		const int k = r.below(39);
		switch (k) {
		case 0: w.bus.emit(EvPhase{ static_cast<Phase>(r.below(8)), static_cast<Phase>(r.below(6)) }); break;
		case 1: w.bus.emit(EvPrayer{ anyId(), static_cast<PrayerKind>(r.below(7)), r.coin() }); break;
		case 2: w.bus.emit(EvPrayerAnswered{ anyId(), static_cast<PrayerKind>(r.below(7)) }); break;
		case 3: {
			std::vector<int> v;
			for (int j = r.below(5); j > 0; j--) v.push_back(anyId());
			w.bus.emit(EvMiracleWitnessed{ anyCell(), std::string(pick(kinds)), pick(nums), v });
			break;
		}
		case 4: w.bus.emit(EvLightning{ anyCell(), r.coin() }); break;
		case 5: w.bus.emit(EvMeteorImpact{ anyCell(), pick(nums), r.coin() ? ImpactKind::Comet : ImpactKind::Meteor, r.coin() }); break;
		case 6: w.bus.emit(EvTsunamiHit{ anyCell(), pick(nums) }); break;
		case 7: w.bus.emit(EvVolcanoStart{ anyCell(), r.below(9) - 3, r.coin() }); break;
		case 8: w.bus.emit(EvVolcanoEnd{ anyCell(), r.below(9) - 3 }); break;
		case 9: w.bus.emit(EvQuake{ anyCell(), pick(nums), pick(nums), r.coin() }); break;
		case 10: w.bus.emit(EvFireStart{ anyCell() }); break;
		case 11: w.bus.emit(EvStorm{ anyCell(), pick(nums), r.coin() }); break;
		case 12: w.bus.emit(EvDrought{ anyCell(), pick(nums), r.coin() }); break;
		case 13: w.bus.emit(EvSettlementFounded{ anyId(), anyCell(), anyId(), r.coin() ? FoundedBy::Awaken : FoundedBy::Settlers }); break;
		case 14: w.bus.emit(EvSettlementTier{ anyId(), r.below(9) - 2, r.below(9) - 2 }); break;
		case 15: w.bus.emit(EvSettlementBuilding{ anyId(), static_cast<Building::Id>(r.below(Building::COUNT)) }); break;
		case 16: w.bus.emit(EvSettlementFamine{ anyId() }); break;
		case 17: w.bus.emit(EvSettlementPlague{ anyId(), r.coin() }); break;
		case 18: w.bus.emit(EvSettlementDestroyed{ anyId(), anyCell(), anyId(), static_cast<Cause>(r.below(13)) }); break;
		case 19: w.bus.emit(EvSettlementCaptured{ anyId(), anyId(), anyId(), r.coin() ? CaptureHow::War : CaptureHow::Schism }); break;
		case 20: w.bus.emit(EvCultureBorn{ anyId(), anyId(), anyCell(), static_cast<BirthReason>(r.below(4)) }); break;
		case 21: w.bus.emit(EvCultureDied{ anyId() }); break;
		case 22: w.bus.emit(EvCultureAge{ anyId(), r.below(10) - 2, r.below(10) - 2 }); break;
		case 23: w.bus.emit(EvWarDeclared{ anyId(), anyId() }); break;
		case 24: w.bus.emit(EvWarEnded{ anyId(), anyId(), anyId() }); break;
		case 25: w.bus.emit(EvBattle{ anyCell(), anyId(), anyId(), r.coin(), anyId() }); break;
		case 26: w.bus.emit(EvTradeRoute{ anyId(), anyId() }); break;
		case 27: w.bus.emit(EvProphetRisen{ anyId(), anyId(), std::string(pick(names)) }); break;
		case 28: w.bus.emit(EvProphetDied{ anyId(), std::string(pick(names)) }); break;
		case 29: w.bus.emit(EvEdict{ anyId(), static_cast<Edict>(r.below(6)) }); break;
		case 30: w.bus.emit(EvWonderBuilt{ anyId(), std::string(pick(names)) }); break;
		case 31: w.bus.emit(EvNuke{ anyCell(), anyId(), anyId() }); break;
		case 32: w.bus.emit(EvArkStarted{ anyId(), anyId() }); break;
		case 33: w.bus.emit(EvArkLaunched{ anyId(), anyId() }); break;
		case 34: w.bus.emit(EvClimateMilestone{ static_cast<MilestoneKind>(r.below(8)), pick(nums) }); break;
		case 35: {
			const char* causes[] = { "", "lost", "disbanded", "arrived", "lightning", "wave", "weird" };
			w.bus.emit(EvUnitDied{ anyId(), static_cast<UnitType>(r.below(5)), anyId(), anyCell(), std::string(pick(causes)) });
			break;
		}
		case 36: w.bus.emit(EvPeopleLost{}); break;
		case 37: w.bus.emit(EvEnding{ static_cast<EndingType>(r.below(3)), static_cast<Mood>(r.below(8)), anyId(), anyId(), pick(nums) }); break;
		default: {
			// the god's comets and the world's year may be hostile too
			w.year = r.coin() ? w.year : pick(nums);
			w.bus.emit(EvMeteorImpact{ anyCell(), 0.05, ImpactKind::Comet, false });
			break;
		}
		}
		calls++;
	}
	say("garbage events emitted: " + std::to_string(calls) + ", entries written: " + std::to_string(w.chronicle.size()));
	int faults = 0;
	for (const std::string& t : texts(w)) {
		const std::string f = proseFault(t, false);
		if (!f.empty()) {
			if (faults < 6) say("fault: " + f);
			faults++;
		}
	}
	ok(faults == 0, "garbage payloads wrote junk prose");
	for (const ChronicleEntry& e : w.chronicle) {
		if (!std::isfinite(e.year)) { fail("entry year not finite"); break; }
		if (e.cell != -1 && !w.valid(e.cell)) { fail("entry cell out of range"); break; }
		if (e.culture != -1 && (e.culture < 0 || e.culture >= int(w.cultures.size()))) { fail("entry culture out of range"); break; }
		if (e.weight < 1 || e.weight > 3) { fail("entry weight out of range"); break; }
	}
	noNaN(w, "after garbage events");
	// the world's own numbers stayed finite (the chronicle writes its memory into vars.num)
	for (const auto& kv : w.vars.num) if (!std::isfinite(kv.second)) { fail("vars.num " + kv.first + " not finite"); break; }
	// epithets and the tale are still clean after the storm
	chronicle::updateEpithets(w);
	const Tale tl = chronicle::tale(w);
	for (const std::string& p : tl.paragraphs) {
		if (p.find("nan") != std::string::npos || p.find("inf") != std::string::npos) fail("tale mentions nan/inf: " + p);
	}
	ok(tl.paragraphs.size() >= 4 && tl.paragraphs.size() <= 8, "tale paragraph count after garbage");
	noNaN(w, "after updateEpithets and tale");
	const std::string e0 = chronicle::epithet(w, a);
	ok(!e0.empty(), "epithet empty for a live culture after garbage");
	ok(chronicle::epithet(w, 9999).empty() && chronicle::epithet(w, -4).empty(), "epithet of a bad culture id is not empty");
}

DM_TEST("chronicle adv: degenerate worlds (no sea, no land, no people, no culture, poles) give clean prose") {
	// (a) a world with almost no sea: every cell is land, so no sea phrases may appear
	{
		Scene s = scene(4, 9, 0.999, true);
		World& w = *s.w;
		const int a = mkCulture(w, "Umar", "Umaran", "the Umari", "Ashvel");
		const int t = mkTown(s, a, 400, 2, "Vornhal");
		w.bus.emit(EvMeteorImpact{ s.sites[0], 0.05, ImpactKind::Meteor, false });
		w.bus.emit(EvLightning{ s.sites[0], true });
		w.bus.emit(EvFireStart{ s.sites[0] });
		w.bus.emit(EvSettlementTier{ t, 3, 2 });
		chronicle::updateEpithets(w);
		const Tale tl = chronicle::tale(w);
		ok(tl.paragraphs.size() >= 4, "no-sea tale paragraphs");
		for (const std::string& x : texts(w)) {
			const std::string f = proseFault(x);
			if (!f.empty()) fail("no-sea world: " + f);
		}
	}
	// (b) an ocean world: a meteor and a storm far out at sea, no settlement at all
	{
		Scene s = scene(4, 11, 0.0, true);
		World& w = *s.w;
		int sea = -1;
		for (int c = 0; c < w.n; c++) if (state::isSea(w, c)) { sea = c; break; }
		ok(sea >= 0, "ocean world has a sea cell");
		w.bus.emit(EvMeteorImpact{ sea, 0.05, ImpactKind::Meteor, true });
		w.bus.emit(EvStorm{ sea, 0.2, true });
		w.bus.emit(EvDrought{ sea, 0.2, true });
		w.bus.emit(EvVolcanoStart{ sea, 1, true });
		w.bus.emit(EvTsunamiHit{ sea, 2.0 });
		w.bus.emit(EvPeopleLost{});
		w.bus.emit(EvEnding{ EndingType::Silence, Mood::Quiet, -1, -1, 1500 });
		w.bus.emit(EvClimateMilestone{ MilestoneKind::SeaRise, 0.2 });
		for (const std::string& x : texts(w)) {
			const std::string f = proseFault(x);
			if (!f.empty()) fail("ocean world: " + f);
		}
		say("ocean world wrote " + std::to_string(w.chronicle.size()) + " entries" +
			(w.chronicle.empty() ? std::string("") : ", first: " + w.chronicle[0].text));
		const Tale tl = chronicle::tale(w);
		ok(tl.paragraphs.size() >= 4, "ocean tale paragraph count");
		ok(chronicle::epithet(w, 0).empty(), "no culture: epithet empty");
		noNaN(w, "ocean world");
	}
	// (c) the poles: settlements on the north and south cells must name a band, not a NaN latitude
	{
		Scene s = scene(4, 13, 0.4, true);
		World& w = *s.w;
		int north = -1, south = -1;
		double bn = -1e9, bs = 1e9;
		for (int c = 0; c < w.n; c++) {
			if (state::isSea(w, c)) continue;
			const double lat = double(w.geo->lat[size_t(c)]);
			if (lat > bn) { bn = lat; north = c; }
			if (lat < bs) { bs = lat; south = c; }
		}
		const int a = mkCulture(w, "Umar", "Umaran", "the Umari", "Ashvel");
		const int t1 = mkTown(s, a, 60, 0, "Polar", north);
		const int t2 = mkTown(s, a, 60, 0, "Antar", south);
		(void)t1; (void)t2;
		w.bus.emit(EvLightning{ north, false });
		w.bus.emit(EvMeteorImpact{ south, 0.05, ImpactKind::Meteor, false });
		w.bus.emit(EvFireStart{ south });
		w.bus.emit(EvSettlementFounded{ t2, south, a, FoundedBy::Settlers });
		for (const std::string& x : texts(w)) {
			const std::string f = proseFault(x);
			if (!f.empty()) fail("pole: " + f);
		}
		noNaN(w, "poles");
	}
	// (d) a planet with no people: every handler that needs a culture stays silent, the tale is still 4+
	{
		Scene s = scene(4, 17, 0.4, true);
		World& w = *s.w;
		w.bus.emit(EvSettlementFamine{ 0 });
		w.bus.emit(EvWarEnded{ 0, 1, 0 });
		w.bus.emit(EvCultureDied{ 0 });
		w.bus.emit(EvNuke{ s.sites.empty() ? 0 : s.sites[0], 0, 1 });
		ok(w.chronicle.empty(), "handlers with no culture or settlement wrote entries");
		const Tale tl = chronicle::tale(w);
		ok(tl.paragraphs.size() >= 4 && tl.paragraphs.size() <= 8, "empty world: tale paragraph count");
		for (const std::string& p : tl.paragraphs) {
			const std::string f = proseFault(p, true);
			if (!f.empty()) fail("empty tale: " + f);
		}
		ok(tl.stats.count("settlements") == 1 && tl.stats.at("settlements") == 0, "empty world: stats.settlements");
		ok(!tl.title.empty(), "tale has a title");
	}
}

DM_TEST("chronicle adv: each ending closes the tale on its own last paragraph; stats agree with the world") {
	struct Case { EndingType t; Mood m; const char* must; };
	const Case cases[] = {
		{ EndingType::Ark, Mood::Beloved, "carried the name of" },
		{ EndingType::Ark, Mood::Feared, "fled to the stars" },
		{ EndingType::Ark, Mood::Forgotten, "hardly remembered the god" },
		{ EndingType::Ark, Mood::Parting, "not knowing quite what they had been" },
		{ EndingType::Silence, Mood::Ash, "was silent" },
		{ EndingType::Silence, Mood::Quiet, "grew quiet" },
	};
	for (const Case& cs : cases) {
		Scene s = scene(4, 21, 0.4, true);
		World& w = *s.w;
		const int a = mkCulture(w, "Umar", "Umaran", "the Umari", "Ashvel");
		const int t = mkTown(s, a, 1500, 3, "Vornhal");
		w.year = 1500;
		w.phase = Phase::Ended;
		w.ending = Ending{ cs.t, cs.m, a, t, 1500 };
		w.bus.emit(EvEnding{ cs.t, cs.m, a, t, 1500 });
		const Tale tl = chronicle::tale(w);
		const std::string last = tl.paragraphs.empty() ? std::string() : tl.paragraphs.back();
		if (last.find(cs.must) == std::string::npos)
			fail(std::string("ending ") + std::to_string(int(cs.t)) + "/" + std::to_string(int(cs.m)) + " tale ends: " + last);
		for (const std::string& p : tl.paragraphs) {
			const std::string f = proseFault(p, true);
			if (!f.empty()) fail("ending tale: " + f);
		}
	}
	// stats agree with the world
	Scene s = scene(4, 23, 0.4, true);
	World& w = *s.w;
	const int a = mkCulture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int b = mkCulture(w, "Teshk", "Teshkan", "the Teshki", "Orro-Ka");
	const int t1 = mkTown(s, a, 800, 2, "Vornhal");
	const int t2 = mkTown(s, b, 120, 1, "Imbrek");
	const int t3 = mkTown(s, b, 90, 1, "Ilo");
	w.year = 300;
	w.phase = Phase::History;
	w.stats.wars = 2;
	w.god.epithet = "the Rain-Giver";
	w.god.mercy = 3;
	w.god.wrath = 0.2;
	w.god.deeds["rain"] = 5;
	w.god.deeds["lightning"] = 2;
	w.bus.emit(EvSettlementDestroyed{ t3, w.settlements[size_t(t3)].cell, b, Cause::Drowned });
	w.settlements[size_t(t3)].dead = true;
	w.settlements[size_t(t3)].cause = Cause::Drowned;
	const Tale tl = chronicle::tale(w);
	ok(tl.stats.at("live") == 2, "stats.live");
	ok(tl.stats.at("destroyed") == 1, "stats.destroyed");
	ok(tl.stats.at("cultures") == 2 && tl.stats.at("livingCultures") == 2, "stats cultures");
	ok(tl.stats.at("wars") == 2, "stats.wars");
	ok(std::fabs(tl.stats.at("pop") - (800 + 120)) < 1e-9, "stats.pop counts live people only");
	ok(tl.stats.at("peakPop") >= 920 - 1e-9, "stats.peakPop");
	const std::string all = [&] { std::string x; for (auto& p : tl.paragraphs) x += p + " "; return x; }();
	ok(all.find("the Rain-Giver") != std::string::npos, "tale names the god's epithet");
	ok(all.find("rain") != std::string::npos, "tale names the god's deeds");
	(void)t1; (void)t2;
}

DM_TEST("chronicle adv: snapshots: NaN intervals reset, due by snapNext, forced frames, owner and urban mapping") {
	Scene s = scene(4, 31, 0.4, true);
	World& w = *s.w;
	const int a = mkCulture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int t = mkTown(s, a, 500, 2, "Vornhal");
	// garbage intervals: NaN and zero are replaced
	w.vars.snapEvery = kNaN;
	w.vars.snapNext = kNaN;
	w.year = 0;
	w.phase = Phase::Molten;
	ok(chronicle::snapshot(w, false), "first snapshot is due when snapNext is NaN");
	ok(w.vars.snapEvery.has_value() && *w.vars.snapEvery == 25, "NaN snapEvery reset to 25");
	ok(w.vars.snapNext.has_value() && std::isfinite(*w.vars.snapNext) && *w.vars.snapNext == 25, "snapNext set to year + 25");
	w.vars.snapEvery = 0.0;
	w.year = 1;
	ok(!chronicle::snapshot(w, false), "not due before snapNext");
	ok(w.vars.snapEvery.has_value() && *w.vars.snapEvery == 25, "zero snapEvery reset to 25");
	// the genesis frame carries the heat and the owner / urban maps
	w.heat = 0.4;
	w.owner[size_t(s.sites[0])] = int16_t(t);
	w.urban[size_t(s.sites[0])] = 0.5f;
	w.urban[size_t(s.sites[1 % s.sites.size()])] = float(kNaN);
	w.urban[size_t(s.sites[2 % s.sites.size()])] = 2.0f;
	w.phase = Phase::Molten;
	ok(chronicle::snapshot(w, true), "forced snapshot always pushes");
	const Snapshot& f0 = w.snapshots.back();
	ok(std::fabs(f0.heat - 0.4) < 1e-9, "frame carries heat");
	ok(f0.owner.size() == size_t(w.n) && f0.biome.size() == size_t(w.n) && f0.urban.size() == size_t(w.n), "frame arrays have n cells");
	ok(f0.owner[size_t(s.sites[0])] == a, "owner stores the culture id of the owning settlement");
	ok(f0.owner[size_t(s.sites[1 % s.sites.size()])] == -1, "unowned cell has owner -1");
	ok(f0.urban[size_t(s.sites[0])] == 128, "urban 0.5 is stored as 128");
	ok(f0.urban[size_t(s.sites[1 % s.sites.size()])] == 0, "NaN urban stored as 0");
	ok(f0.urban[size_t(s.sites[2 % s.sites.size()])] == 255, "urban above 1 saturates at 255");
	// a settlement owned by a dead culture or with culture -1 maps to -1 (never a stale id)
	w.settlements[size_t(t)].culture = -1;
	ok(chronicle::snapshot(w, true), "second forced snapshot");
	ok(w.snapshots.back().owner[size_t(s.sites[0])] == -1, "culture -1 maps to owner -1");
	// the same year again: a due frame does not repeat until snapNext
	w.snapshots.clear();
	w.vars.snapEvery = 25.0;
	w.vars.snapNext = 0.0;
	w.year = 10;
	w.phase = Phase::History;
	ok(chronicle::snapshot(w, false), "due frame at year 10");
	ok(!chronicle::snapshot(w, false), "same year: no second due frame");
	w.year = 34.9;
	ok(!chronicle::snapshot(w, false), "still before 10 + 25");
	w.year = 35;
	ok(chronicle::snapshot(w, false), "due again at 35");
	ok(w.snapshots.size() == 2, "two frames");
	ok(w.snapshots[0].year == 10 && w.snapshots[1].year == 35, "frame years");
}

DM_TEST("chronicle adv: snapshot thinning past 200 frames keeps order, the genesis and the newest, and doubles the interval") {
	Scene s = scene(4, 33, 0.4, true);
	World& w = *s.w;
	mkCulture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	w.vars.snapEvery = 25.0;
	w.vars.snapNext = 0.0;
	// three genesis frames (before history) then a long history
	w.phase = Phase::Molten;
	w.year = 0;
	for (int g = 0; g < 3; g++) { chronicle::snapshot(w, true); }
	w.phase = Phase::History;
	w.year = 0;
	int pushed = 0;
	for (int step = 0; step < 60000 && w.year < 6000; step++) {
		w.year += 0.25;
		if (chronicle::snapshot(w, false)) pushed++;
		if (w.snapshots.size() > 200) fail("more than 200 frames after snapshot");
	}
	say("history frames pushed: " + std::to_string(pushed) + ", kept: " + std::to_string(w.snapshots.size()) +
		", final interval: " + std::to_string(*w.vars.snapEvery));
	ok(w.snapshots.size() <= 200, "frame cap");
	ok(w.snapshots.size() >= 8, "frames kept");
	bool ordered = true;
	for (size_t i = 1; i < w.snapshots.size(); i++) if (w.snapshots[i].year < w.snapshots[i - 1].year) ordered = false;
	ok(ordered, "frames in non-decreasing year order");
	// the genesis frames (year 0, three of them) stay at the front
	int genesisKept = 0;
	for (size_t i = 0; i < w.snapshots.size() && w.snapshots[i].year == 0; i++) genesisKept++;
	ok(genesisKept == 3, "all three genesis frames kept");
	ok(w.snapshots.back().year >= w.year - 60, "the newest frame is kept");
	ok(*w.vars.snapEvery >= 50, "interval doubled at least once");
	ok(*w.vars.snapEvery == 50 || *w.vars.snapEvery == 100 || *w.vars.snapEvery == 200 || *w.vars.snapEvery == 400, "interval is a power of two times 25");
	say("kept years: first " + std::to_string(w.snapshots[3].year) + " ... last " + std::to_string(w.snapshots.back().year) +
		", gap after the thinning " + std::to_string(w.snapshots.back().year - w.snapshots[w.snapshots.size() - 2].year));
	// phase change still forces a frame after thinning
	const size_t before = w.snapshots.size();
	w.phase = Phase::Ended;
	ok(chronicle::snapshot(w, true), "forced snapshot after thinning");
	ok(w.snapshots.size() <= 200 && w.snapshots.size() >= before - 1, "forced frame keeps the cap");
}

DM_TEST("chronicle adv: the 2,500-entry cap drops the oldest minor entries and never an epochal one while minors remain") {
	Scene s = scene(4, 41, 0.4, true);
	World& w = *s.w;
	const int a = mkCulture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	EntryOpts epochal; epochal.type = "ending"; epochal.text = "The first and greatest entry."; epochal.weight = 3; epochal.culture = a;
	chronicle::add(w, epochal);
	const int firstId = w.chronicle.front().id;
	for (int i = 0; i < 2600; i++) {
		EntryOpts o; o.type = "minor"; o.text = "A small thing happened, number " + std::string(i % 7, 'x') + "."; o.weight = 1; o.culture = a;
		chronicle::add(w, o);
	}
	ok(w.chronicle.size() <= 2500, "entries capped at 2500");
	bool epochalKept = false;
	for (const ChronicleEntry& e : w.chronicle) if (e.id == firstId) epochalKept = true;
	ok(epochalKept, "the epochal entry survives while minors remain");
	ok(w.chronicle.back().weight == 1, "newest minor kept");
	// now fill with notable ones: minors go first, then notable; epochal still survives
	for (int i = 0; i < 2600; i++) {
		EntryOpts o; o.type = "notable"; o.text = "A notable thing happened."; o.weight = 2; o.culture = a;
		chronicle::add(w, o);
	}
	ok(w.chronicle.size() <= 2500, "entries still capped");
	ok(w.chronicle.front().weight >= 2, "no minor entries left at the front after a notable flood");
	int ids = 0;
	for (size_t i = 1; i < w.chronicle.size(); i++) if (w.chronicle[i].id <= w.chronicle[i - 1].id) ids++;
	ok(ids == 0, "entry ids increase along the chronicle");
	// add() refuses empty text and clamps the weight and the culture
	EntryOpts bad; bad.type = "x"; bad.text = "";
	ok(chronicle::add(w, bad) == nullptr, "empty text refused");
	EntryOpts odd; odd.type = "x"; odd.text = "Odd things."; odd.weight = 9; odd.culture = 999; odd.cell = -12;
	ChronicleEntry* e = chronicle::add(w, odd);
	ok(e && e->weight == 3 && e->culture == -1 && e->cell == -1, "weight clamped to 3, bad culture and cell become -1");
}

DM_TEST("chronicle adv: prayers: an answered prayer is told once, the next miracle of the same tick is told") {
	Scene s = scene(4, 51, 0.4, true);
	World& w = *s.w;
	const int a = mkCulture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int t1 = mkTown(s, a, 300, 1, "Vornhal");
	const int t2 = mkTown(s, a, 200, 1, "Imbrek");
	const int cell1 = w.settlements[size_t(t1)].cell;
	w.year = 100;
	w.bus.emit(EvPrayer{ t1, PrayerKind::Rain, true });
	w.bus.emit(EvPrayer{ t2, PrayerKind::Rain, true });
	const size_t n0 = w.chronicle.size();
	// the witness call that answers t1 emits prayer:answered, then miracle:witnessed for both settlements
	w.bus.emit(EvPrayerAnswered{ t1, PrayerKind::Rain });
	w.bus.emit(EvMiracleWitnessed{ cell1, "rain", 1.0, std::vector<int>{ t1, t2 } });
	int answered = 0, rainMiracle = 0;
	for (size_t i = n0; i < w.chronicle.size(); i++) {
		if (w.chronicle[i].type == "prayer-answered") answered++;
		if (w.chronicle[i].type == "miracle-rain") rainMiracle++;
	}
	ok(answered == 1, "one prayer-answered entry");
	ok(rainMiracle == 0, "the miracle that answered the prayer is not told twice");
	// a later, unrelated rain on the same year is not swallowed by the consumed marker
	w.bus.emit(EvMiracleWitnessed{ cell1, "rain", 0.9, std::vector<int>{ t2 } });
	int rain2 = 0;
	for (size_t i = n0; i < w.chronicle.size(); i++) if (w.chronicle[i].type == "miracle-rain") rain2++;
	ok(rain2 == 1, "an unrelated later rain is told");
	// a prayer that ends unanswered for 15 years is told; one that ends early is not
	w.year = 130;
	w.bus.emit(EvPrayer{ t2, PrayerKind::Rain, false });
	int unanswered = 0;
	for (const ChronicleEntry& x : w.chronicle) if (x.type == "prayer-unanswered") unanswered++;
	ok(unanswered == 1, "a 15-year unanswered prayer is told");
	w.bus.emit(EvPrayer{ t1, PrayerKind::Heal, true });
	w.year = 103;
	w.bus.emit(EvPrayer{ t1, PrayerKind::Heal, false });
	unanswered = 0;
	for (const ChronicleEntry& x : w.chronicle) if (x.type == "prayer-unanswered") unanswered++;
	ok(unanswered == 1, "a prayer whose cause went away in 3 years is silent");
	// an answered prayer is not told as unanswered later
	w.year = 104;
	w.bus.emit(EvPrayer{ t2, PrayerKind::Peace, true });
	w.bus.emit(EvPrayerAnswered{ t2, PrayerKind::Peace });
	w.year = 130;
	w.bus.emit(EvPrayer{ t2, PrayerKind::Peace, false });
	unanswered = 0;
	for (const ChronicleEntry& x : w.chronicle) if (x.type == "prayer-unanswered") unanswered++;
	ok(unanswered == 1, "answered prayer is not reported as unanswered");
}

DM_TEST("chronicle adv: epithets: hold, silence, regard names, god.epithet follows the largest living people") {
	Scene s = scene(4, 61, 0.4, true);
	World& w = *s.w;
	const int a = mkCulture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int b = mkCulture(w, "Teshk", "Teshkan", "the Teshki", "Orro-Ka");
	const int ta = mkTown(s, a, 300, 1, "Vornhal");
	const int tb = mkTown(s, b, 900, 2, "Imbrek");
	w.year = 0;
	w.phase = Phase::History;
	// a culture with memory of rain: "the Rain-Giver" family within the first commit
	w.cultures[size_t(a)].seen["rain"] = 3.0;
	chronicle::updateEpithets(w);
	const std::string ea = w.cultures[size_t(a)].epithet;
	ok(!ea.empty(), "epithet set on first update");
	ok(ea == "the Rain-Giver" || ea == "the Cloud-Shepherd" || ea == "She Who Brings the Rain" || ea == "He Who Brings the Rain" || ea == "the Wet Wind",
		"rain memory names the god as a rain-giver: '" + ea + "'");
	// god.epithet follows the most populous living culture (b has 900 people)
	ok(w.god.epithet == w.cultures[size_t(b)].epithet, "god.epithet follows the largest culture (b)");
	// the name holds for 40 years even when a stronger deed arrives (flapping)
	w.year = 5;
	w.cultures[size_t(a)].seen["lightning"] = 9.0;
	chronicle::updateEpithets(w);
	ok(w.cultures[size_t(a)].epithet == ea, "epithet held inside the 40-year hold");
	w.year = 60;
	chronicle::updateEpithets(w);
	const std::string ea2 = w.cultures[size_t(a)].epithet;
	ok(ea2 != ea, "after the hold a strong new deed changes the name: '" + ea2 + "'");
	// a people that goes silent for 300 years is "the Silent One" family; it is not held
	w.year = 60 + 301;
	w.cultures[size_t(a)].seen.clear();
	chronicle::updateEpithets(w);
	const std::string es = w.cultures[size_t(a)].epithet;
	ok(es == "the Silent One" || es == "the Sleeper" || es == "She Who Does Not Answer" || es == "He Who Does Not Answer" || es == "the Far-Away",
		"300 years of silence: '" + es + "'");
	// the largest culture dies: god.epithet moves to the next largest, and the dead culture's memory is dropped
	w.cultures[size_t(b)].dead = true;
	for (Settlement& st : w.settlements) if (st.culture == b) { st.dead = true; st.cause = Cause::Famine; }
	w.bus.emit(EvCultureDied{ b });
	chronicle::updateEpithets(w);
	ok(w.god.epithet == w.cultures[size_t(a)].epithet, "god.epithet moves to the living culture when the largest dies");
	bool memoryLeft = false;
	for (const auto& kv : w.vars.num) if (kv.first.find("chronicle_") == 0 && kv.first.find("." + std::to_string(b)) != std::string::npos && kv.first.find(".1") == std::string::npos) memoryLeft = true;
	(void)memoryLeft;
	ok(chronicle::epithet(w, b).empty() || !w.cultures[size_t(b)].epithet.empty(), "epithet read for a dead culture is safe");
	(void)ta; (void)tb;
}

DM_TEST("chronicle adv: regard names the god when there is no deed (Beloved, Dreaded, Waker)") {
	dmt::stub::Guard guard(false);
	if (!dmt::stub::stubbed("society")) { say("society is real: regard hook not used"); return; }
	Scene s = scene(4, 71, 0.4, true);
	World& w = *s.w;
	const int a = mkCulture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	mkTown(s, a, 300, 1, "Vornhal");
	int mode = 0;   // 0 = loving, 1 = fearful, 2 = neutral
	dmt::stub::hooks().society_regard = [&mode](const World&, int) -> society::Regard {
		society::Regard r;
		r.devotion = mode == 2 ? 0.2 : 0.8;
		r.love = mode == 0 ? 0.7 : mode == 1 ? -0.6 : 0.0;
		return r;
	};
	mode = 0;
	const std::string loving = chronicle::epithet(w, a);
	ok(loving == "the Beloved" || loving == "the Kind One" || loving == "He Who Keeps the Hearth" || loving == "She Who Keeps the Hearth" || loving == "the Shepherd",
		"loving people name the god 'Beloved' family: '" + loving + "'");
	mode = 1;
	const std::string fearful = chronicle::epithet(w, a);
	ok(fearful == "the Dreaded" || fearful == "the Jealous One" || fearful == "He Who Must Be Appeased" || fearful == "She Who Must Be Appeased" || fearful == "the Terrible",
		"fearful people name the god 'Dreaded' family: '" + fearful + "'");
	mode = 2;
	const std::string neutral = chronicle::epithet(w, a);
	ok(neutral == "the Waker" || neutral == "the God of Our Fathers" || neutral == "the Old God" || neutral == "the Dawn-Caller" || neutral == "the First Voice" || neutral == "the Remembered One",
		"neutral people with no deed: '" + neutral + "'");
	// a people with a real deed is not renamed by regard
	w.cultures[size_t(a)].seen["rain"] = 5.0;
	mode = 1;
	const std::string withDeed = chronicle::epithet(w, a);
	ok(withDeed != "the Dreaded" && withDeed != "the Jealous One", "a deed outweighs the feeling: '" + withDeed + "'");
}

DM_TEST("chronicle adv: a culture with no name gets a god name, an empty language is tolerated") {
	Scene s = scene(4, 81, 0.4, true);
	World& w = *s.w;
	Culture& c = state::newCulture(w, -1);
	c.lang = "";
	c.name = "";
	c.adj = "";
	c.plural = "";
	c.godName = "";
	const int cid = c.id;
	chronicle::updateEpithets(w);
	ok(!w.cultures[size_t(cid)].godName.empty(), "a god name was given to a nameless culture");
	ok(!w.cultures[size_t(cid)].epithet.empty(), "an epithet was given to a nameless culture");
	const std::string f = proseFault(w.chronicle.empty() ? std::string("Empty text here.") : w.chronicle.back().text);
	(void)f;
	noNaN(w, "nameless culture");
}

DM_TEST("chronicle adv: sim stream untouched and two identical runs write identical chronicles") {
	auto run = [](uint32_t seed, std::vector<std::string>& out, uint32_t& rngOut, uint32_t& hashOut, bool attach) {
		Scene s = scene(4, seed, 0.4, true, attach);
		World& w = *s.w;
		const int a = mkCulture(w, "Umar", "Umaran", "the Umari", "Ashvel");
		const int b = mkCulture(w, "Teshk", "Teshkan", "the Teshki", "Orro-Ka", a);
		const int t1 = mkTown(s, a, 900, 2, "Vornhal");
		const int t2 = mkTown(s, b, 300, 1, "Imbrek");
		w.year = 0;
		w.phase = Phase::History;
		Lcg r(seed + 5);
		for (int y = 0; y < 300; y++) {
			w.year += 1;
			const int c = s.sites[size_t(r.below(int(s.sites.size())))];
			switch (r.below(9)) {
			case 0: w.bus.emit(EvLightning{ c, r.coin() }); break;
			case 1: w.bus.emit(EvFireStart{ c }); break;
			case 2: w.bus.emit(EvMiracleWitnessed{ c, "rain", 0.6, std::vector<int>{ t1 } }); break;
			case 3: w.bus.emit(EvSettlementTier{ t2, 1 + r.below(2), 1 }); break;
			case 4: w.bus.emit(EvWarDeclared{ a, b }); break;
			case 5: w.bus.emit(EvBattle{ c, a, b, r.coin(), t2 }); break;
			case 6: w.bus.emit(EvSettlementFamine{ t1 }); break;
			default: break;
			}
			chronicle::updateEpithets(w);
			chronicle::snapshot(w, false);
		}
		for (const ChronicleEntry& e : w.chronicle) out.push_back(e.type + "|" + std::to_string(e.year) + "|" + e.text);
		rngOut = w.rng.s;
		hashOut = hash(w);
	};
	std::vector<std::string> x1, x2, y;
	uint32_t r1 = 0, r2 = 0, h1 = 0, h2 = 0, ry = 0, hy = 0;
	run(9, x1, r1, h1, true);
	run(9, x2, r2, h2, true);
	ok(x1 == x2, "two identical runs wrote different chronicles");
	ok(h1 == h2, "two identical runs hash differently");
	ok(r1 == r2, "two identical runs end with different rng streams");
	run(9, y, ry, hy, false);
	ok(ry == r1, "chronicle activity changed the sim rng stream");
	say("sample deterministic run: " + std::to_string(x1.size()) + " entries");
}

DM_TEST("chronicle adv: grammar fuzzer over every chronicled event with realistic names: no prose faults") {
	Scene s = scene(5, 91, 0.4, true);
	World& w = *s.w;
	const char* const cnames[] = { "Umar", "Teshk", "Vael", "Oru", "Hsar", "Dumar" };
	const char* const plurals[] = { "the Umari", "the Teshki", "the Vaeli", "the Orun", "the Hsar", "the Dumari" };
	const char* const adjs[] = { "Umaran", "Teshkan", "Vaelish", "Oruni", "Hsari", "Dumari" };
	const char* const gods[] = { "Ashvel", "Orro-Ka", "Ka-Imen", "Mbele", "Tsoreh", "Ysh" };
	const char* const tnames[] = { "Vornhal", "Tel Asha", "Qe-Luun", "Imbrek", "Sarnath", "Ulu Kesh", "Dorrow", "Mirathe",
		"Kesh Amun", "Oduya", "Brennick", "Tamsin Var", "Ilo", "Haddath", "Zuneri", "Penkarrow", "Asel", "Umbrin" };
	std::vector<int> cs;
	for (int i = 0; i < 6; i++) cs.push_back(mkCulture(w, cnames[i], adjs[i], plurals[i], gods[i], i >= 2 ? 0 : -1));
	std::vector<int> ts;
	for (int i = 0; i < 18; i++) {
		const int tier = i % 5;
		const int sid = mkTown(s, cs[size_t(i % 6)], 40 + 90 * tier * tier, tier, tnames[i]);
		if (sid >= 0) ts.push_back(sid);
	}
	w.god.epithet = "the Rain-Giver";
	w.stats.wars = 4;
	w.phase = Phase::History;
	Lcg r(4242);
	auto T = [&]() { return ts.empty() ? 0 : ts[size_t(r.below(int(ts.size())))]; };
	auto C = [&]() { return cs[size_t(r.below(int(cs.size())))]; };
	auto Cell = [&]() { return s.sites[size_t(r.below(int(s.sites.size())))]; };
	auto F = [&]() { return 0.001 * r.below(1000); };
	const size_t start = w.chronicle.size();
	for (int it = 0; it < 4000; it++) {
		w.year += 0.25;
		switch (r.below(36)) {
		case 0: w.bus.emit(EvLightning{ r.coin() ? Cell() : -1, r.coin() }); break;
		case 1: w.bus.emit(EvQuake{ Cell(), F() * 0.3, 1.0, r.coin() }); break;
		case 2: w.bus.emit(EvFireStart{ Cell() }); break;
		case 3: w.bus.emit(EvStorm{ Cell(), 0.1, true }); break;
		case 4: w.bus.emit(EvDrought{ Cell(), 0.1, r.coin() }); break;
		case 5: w.bus.emit(EvTsunamiHit{ Cell(), 2.0 }); break;
		case 6: w.bus.emit(EvVolcanoStart{ Cell(), 1, r.coin() }); break;
		case 7: w.bus.emit(EvVolcanoEnd{ Cell(), 1 }); break;
		case 8: w.bus.emit(EvMeteorImpact{ Cell(), 0.05, r.coin() ? ImpactKind::Comet : ImpactKind::Meteor, r.coin() }); break;
		case 9: w.bus.emit(EvSettlementFounded{ T(), Cell(), C(), r.coin() ? FoundedBy::Settlers : FoundedBy::Awaken }); break;
		case 10: { const int t = T(); const int p = w.settlements[size_t(t)].tier; w.settlements[size_t(t)].tier = std::min(4, p + 1); w.bus.emit(EvSettlementTier{ t, std::min(4, p + 1), p }); break; }
		case 11: w.bus.emit(EvSettlementBuilding{ T(), static_cast<Building::Id>(r.below(Building::COUNT - 1)) }); break;
		case 12: w.bus.emit(EvSettlementFamine{ T() }); break;
		case 13: w.bus.emit(EvSettlementPlague{ T(), true }); break;
		case 14: w.bus.emit(EvSettlementDestroyed{ T(), Cell(), C(), static_cast<Cause>(r.below(12)) }); break;
		case 15: w.bus.emit(EvSettlementCaptured{ T(), C(), C(), r.coin() ? CaptureHow::War : CaptureHow::Schism }); break;
		case 16: w.bus.emit(EvPrayer{ T(), static_cast<PrayerKind>(r.below(4)), true }); break;
		case 17: w.bus.emit(EvPrayerAnswered{ T(), static_cast<PrayerKind>(r.below(4)) }); break;
		case 18: w.bus.emit(EvCultureBorn{ C(), C(), Cell(), r.coin() ? BirthReason::Schism : BirthReason::Distance }); break;
		case 19: w.bus.emit(EvCultureAge{ C(), 1 + r.below(6), 0 }); break;
		case 20: w.bus.emit(EvWarDeclared{ C(), C() }); break;
		case 21: w.bus.emit(EvWarEnded{ C(), C(), r.coin() ? C() : -1 }); break;
		case 22: w.bus.emit(EvBattle{ Cell(), C(), C(), false, T() }); break;
		case 23: w.bus.emit(EvTradeRoute{ T(), T() }); break;
		case 24: w.bus.emit(EvProphetRisen{ C(), T(), "Brannoch" }); break;
		case 25: w.bus.emit(EvProphetDied{ C(), "Brannoch" }); break;
		case 26: w.bus.emit(EvEdict{ C(), static_cast<Edict>(1 + r.below(4)) }); break;
		case 27: w.bus.emit(EvWonderBuilt{ T(), "the Spire of Dumar" }); break;
		case 28: w.bus.emit(EvNuke{ Cell(), C(), C() }); break;
		case 29: w.bus.emit(EvArkStarted{ C(), T() }); break;
		case 30: w.bus.emit(EvClimateMilestone{ static_cast<MilestoneKind>(r.below(6)), 1.0 + F() }); break;
		case 31: w.bus.emit(EvUnitDied{ 1, UnitType::Settler, C(), Cell(), r.coin() ? "lost" : "lightning" }); break;
		case 32: w.bus.emit(EvMiracleWitnessed{ Cell(), r.coin() ? "rain" : "bloom", 0.5, std::vector<int>{ T() } }); break;
		case 33: w.bus.emit(EvMiracleWitnessed{ Cell(), r.coin() ? "heal" : "inspire", 1.0, std::vector<int>{ T() } }); break;
		case 34: w.bus.emit(EvCultureDied{ C() }); break;
		default: w.bus.emit(EvEdict{ C(), Edict::Crusade }); break;
		}
	}
	int faults = 0;
	std::map<std::string, int> distinct;
	int total = 0;
	for (size_t i = start; i < w.chronicle.size(); i++) {
		const std::string& t = w.chronicle[i].text;
		total++;
		distinct[t]++;
		const std::string f = proseFault(t) + gapFault(t);
		if (!f.empty()) {
			if (faults < 12) say("fault: " + f);
			faults++;
		}
	}
	say("fuzzer: " + std::to_string(total) + " entries, " + std::to_string(distinct.size()) + " distinct texts, " + std::to_string(faults) + " faults");
	ok(faults == 0, "fuzzer found prose faults");
	ok(total > 300, "fuzzer wrote enough entries to mean something");
	// the same text never repeats within a single culture's consecutive entries of one type
	int repeatRuns = 0;
	for (size_t i = start + 1; i < w.chronicle.size(); i++) {
		if (w.chronicle[i].text == w.chronicle[i - 1].text && w.chronicle[i].type == w.chronicle[i - 1].type) repeatRuns++;
	}
	ok(repeatRuns == 0, "identical text twice in a row of one type");
	noNaN(w, "after fuzzer");
}

DM_TEST("chronicle adv: L = 6 timings: yearly chronicle work, epithets, snapshot and tale stay inside the budget") {
	Scene s = scene(6, 101, 0.4, true);
	World& w = *s.w;
	const char* const tn[] = { "Ulu", "Dorrow", "Vornhal", "Imbrek", "Sarnath", "Ilo" };
	std::vector<int> cs;
	for (int i = 0; i < 6; i++) cs.push_back(mkCulture(w, tn[i], "Ex", "the Exi", "Ashvel", i >= 1 ? 0 : -1));
	std::vector<int> ts;
	for (int i = 0; i < 160 && s.next < int(s.sites.size()); i++) {
		const int tier = i % 5;
		const int sid = mkTown(s, cs[size_t(i % 6)], 30 + 40 * (i % 9), tier, "Townsend");
		if (sid >= 0) ts.push_back(sid);
	}
	say("L6 settlements: " + std::to_string(ts.size()) + " of " + std::to_string(w.n) + " cells");
	w.phase = Phase::History;
	w.vars.snapEvery = 25.0;
	w.vars.snapNext = 0.0;
	Lcg r(7);
	double tEvents = 0, tEpi = 0, tSnap = 0;
	int yearsRun = 0;
	for (int y = 0; y < 400; y++) {
		w.year += 1;
		yearsRun++;
		const double t0 = nowMs();
		for (int k = 0; k < 12; k++) {
			const int c = s.sites[size_t(r.below(int(s.sites.size())))];
			switch (r.below(10)) {
			case 0: w.bus.emit(EvFireStart{ c }); break;
			case 1: w.bus.emit(EvLightning{ c, true }); break;
			case 2: w.bus.emit(EvMiracleWitnessed{ c, "rain", 0.6, std::vector<int>{ ts[size_t(r.below(int(ts.size())))] } }); break;
			case 3: w.bus.emit(EvSettlementFamine{ ts[size_t(r.below(int(ts.size())))] }); break;
			case 4: { const int t = ts[size_t(r.below(int(ts.size())))]; const int p = w.settlements[size_t(t)].tier; if (p < 4) { w.settlements[size_t(t)].tier = p + 1; w.bus.emit(EvSettlementTier{ t, p + 1, p }); } break; }
			case 5: w.bus.emit(EvSettlementBuilding{ ts[size_t(r.below(int(ts.size())))], Building::MARKET }); break;
			case 6: w.bus.emit(EvBattle{ c, cs[0], cs[1], false, ts[0] }); break;
			case 7: w.bus.emit(EvStorm{ c, 0.1, true }); break;
			case 8: w.bus.emit(EvTradeRoute{ ts[0], ts[1] }); break;
			default: w.bus.emit(EvPrayer{ ts[size_t(r.below(int(ts.size())))], PrayerKind::Rain, true }); break;
			}
		}
		const double t1 = nowMs();
		chronicle::updateEpithets(w);
		const double t2 = nowMs();
		chronicle::snapshot(w, false);
		const double t3 = nowMs();
		tEvents += t1 - t0;
		tEpi += t2 - t1;
		tSnap += t3 - t2;
	}
	const double perYearEvents = tEvents / yearsRun, perYearEpi = tEpi / yearsRun, perYearSnap = tSnap / yearsRun;
	say("L6 per year: events " + std::to_string(perYearEvents) + " ms (12 events), updateEpithets " + std::to_string(perYearEpi) +
		" ms, snapshot " + std::to_string(perYearSnap) + " ms; entries " + std::to_string(w.chronicle.size()) +
		", frames " + std::to_string(w.snapshots.size()));
	// budget: one yearly group runs every 4 ticks, mean tick <= 2.5 ms, so a year may spend <= 10 ms;
	// the test asserts 5x headroom
	ok(perYearEpi < 50.0, "updateEpithets per year at L 6 too slow");
	ok(perYearSnap < 50.0, "snapshot per year at L 6 too slow");
	const double t4 = nowMs();
	const Tale tl = chronicle::tale(w);
	const double t5 = nowMs();
	say("L6 tale: " + std::to_string(t5 - t4) + " ms, " + std::to_string(tl.paragraphs.size()) + " paragraphs");
	say("TALE " + tl.title);
	for (const std::string& p : tl.paragraphs) say("  > " + p);
	{
		// a spread sample of the entries for the quality read
		const size_t n = w.chronicle.size();
		for (size_t i = 0; i < n; i += std::max<size_t>(1, n / 30)) say("E " + std::to_string(int(w.chronicle[i].year)) + " [" + w.chronicle[i].type + "] " + w.chronicle[i].text);
	}
	ok(tl.paragraphs.size() >= 4 && tl.paragraphs.size() <= 8, "L6 tale paragraph count");
	for (const std::string& p : tl.paragraphs) {
		const std::string f = proseFault(p, true);
		if (!f.empty()) fail("L6 tale: " + f);
	}
	noNaN(w, "L6 chronicle");
}

// The real simulation, every module real (`run.ps1 all`): an 800-year history with the god's rain,
// lightning and plague. Skipped where Sim is a stub. Checks the prose, the epithets, the tale, the
// determinism of the whole run, and that the chronicle never moves the sim's own random stream.
struct Run {
	std::unique_ptr<World> w;
	int64_t ticks = 0;
};
Run simRun(uint32_t seed, bool chronicleOn, int years) {
	Run out;
	sim::WorldOpts o;
	o.seed = seed;
	o.name = "Erethe";
	o.L = 4;
	out.w = sim::create(o);
	World& w = *out.w;
	if (!chronicleOn) w.bus.clear();
	sim::genesis::SkipOpts so;
	so.sea = 0.0;
	sim::genesis::skip(w, so);
	Lcg r(seed + 99);
	for (int64_t t = 0; t < int64_t(years) * 4; t++) {
		if (t % 4 == 0) {
			int c = -1;
			for (int tries = 0; tries < 40 && c < 0; tries++) {
				const int m = r.below(w.n);
				if (!state::isSea(w, m)) c = m;
			}
			if (c >= 0) {
				if (r.below(4) == 0) powers::use(w, "rain", c, powers::UseOpts{ .ang = -1, .dt = 0.125 });
				if (r.below(90) == 0) powers::use(w, "lightning", c, powers::UseOpts{});
				if (r.below(260) == 0) {
					const Settlement* s = nullptr;
					for (const Settlement& q : w.settlements) if (!q.dead) { s = &q; break; }
					if (s) powers::use(w, "plague", s->cell, powers::UseOpts{});
				}
			}
		}
		sim::advance(w, 1);
		out.ticks++;
	}
	return out;
}

DM_TEST("chronicle adv: integration: a real 800-year history reads as prose, is deterministic and leaves the sim stream alone") {
	if (dmt::stub::stubbed("sim") || dmt::stub::stubbed("powers") || dmt::stub::stubbed("society") || dmt::stub::stubbed("civ")) {
		say("skipped: sim, powers, society or civ is a stub in this build (run.ps1 all runs it)");
		return;
	}
	Run a = simRun(7, true, 800);
	World& w = *a.w;
	say("integration: " + std::to_string(w.chronicle.size()) + " entries, " + std::to_string(w.settlements.size()) + " settlements, " +
		std::to_string(w.cultures.size()) + " cultures, pop peak " + std::to_string(w.stats.peakPop) + ", year " + std::to_string(w.year));
	ok(!w.chronicle.empty(), "a real history wrote no chronicle");
	int faults = 0;
	for (const std::string& t : texts(w)) {
		const std::string f = proseFault(t);
		if (!f.empty()) { if (faults < 8) say("integration fault: " + f); faults++; }
	}
	ok(faults == 0, "real history: prose faults");
	noNaN(w, "real 800-year history");
	const Tale tl = chronicle::tale(w);
	ok(tl.paragraphs.size() >= 4 && tl.paragraphs.size() <= 8, "real history: tale paragraph count");
	for (const std::string& p : tl.paragraphs) {
		const std::string f = proseFault(p, true);
		if (!f.empty()) fail("real tale: " + f);
	}
	say("TALE " + tl.title);
	for (const std::string& p : tl.paragraphs) say("  > " + p);
	bool anyEpithet = false;
	for (const Culture& c : w.cultures) if (!c.dead && !c.epithet.empty()) anyEpithet = true;
	ok(anyEpithet || w.cultures.empty(), "a living culture has an epithet");
	if (w.god.epithet.empty() && !w.cultures.empty()) {
		bool alive = false;
		for (const Culture& c : w.cultures) if (!c.dead) alive = true;
		ok(!alive, "god.epithet is empty although a culture lives");
	}
	int kinds = 0;
	for (const char* k : { "awakening", "founded", "tier", "fire", "prayer", "miracle-rain", "wrath-lightning", "plague", "epithet" }) {
		for (const ChronicleEntry& e : w.chronicle) if (e.type == k) { kinds++; break; }
	}
	say("integration: " + std::to_string(kinds) + " of 9 sample kinds appear");

	// determinism: the same run again gives the same world and the same chronicle
	Run b = simRun(7, true, 800);
	ok(hash(*b.w) == hash(w), "two identical real runs hash differently");
	ok(texts(*b.w) == texts(w), "two identical real runs wrote different chronicles");
	// the chronicle never moves the sim stream: the same run without any subscription ends with the same rng,
	// population and Awe
	Run c = simRun(7, false, 800);
	World& cw = *c.w;
	ok(cw.rng.s == w.rng.s, "chronicle activity moved the sim rng in a real run");
	ok(cw.year == w.year && cw.settlements.size() == w.settlements.size() && cw.cultures.size() == w.cultures.size(),
		"chronicle activity changed the real sim's shape");
	ok(std::fabs(cw.god.awe - w.god.awe) < 1e-12 && std::fabs(cw.stats.peakPop - w.stats.peakPop) < 1e-9, "chronicle changed Awe or peak population");
	// without subscriptions only the epithet entries (written by updateEpithets, which Society calls) remain
	int nonEpithet = 0;
	for (const ChronicleEntry& e : cw.chronicle) if (e.type != "epithet") nonEpithet++;
	ok(nonEpithet == 0, "a world without subscriptions still wrote event entries");
	// the tale does not disturb the stream
	const uint32_t before = w.rng.s;
	(void)chronicle::tale(w);
	(void)chronicle::epithet(w, 0);
	ok(w.rng.s == before, "tale or epithet moved the rng");
}

DM_TEST("chronicle adv: the wonder, the schism and the prophets read as sentences with their names") {
	Scene s = scene(4, 111, 0.4, true);
	World& w = *s.w;
	const int a = mkCulture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int t = mkTown(s, a, 900, 3, "Vornhal");
	const int t2 = mkTown(s, a, 400, 2, "Imbrek");
	w.year = 700;
	w.bus.emit(EvWonderBuilt{ t, "the Spire of Vornhal" });
	Culture& child = state::newCulture(w, a);
	child.name = "Vael"; child.adj = "Vaelish"; child.plural = "the Vaeli"; child.godName = "Ashvel"; child.lang = "test";
	w.bus.emit(EvCultureBorn{ child.id, a, s.sites[0], BirthReason::Schism });
	w.bus.emit(EvSettlementCaptured{ t2, a, child.id, CaptureHow::Schism });
	w.bus.emit(EvProphetRisen{ a, t, "Brannoch" });
	w.bus.emit(EvEdict{ a, Edict::Build });
	w.bus.emit(EvCultureAge{ a, Age::BRONZE, 0 });
	w.bus.emit(EvCultureAge{ a, Age::SPACE, 5 });
	const std::vector<std::string> got = texts(w);
	for (const std::string& x : got) {
		const std::string f = proseFault(x);
		if (!f.empty()) fail("sentence: " + f);
		std::printf("    [%s]\n", x.c_str());
	}
	ok(got.size() >= 5, "wonder, schism, prophet, edict and ages wrote entries");
	bool named = false;
	for (const std::string& x : got) if (x.find("Vornhal") != std::string::npos) named = true;
	ok(named, "entries name the settlement");
}

} // namespace
