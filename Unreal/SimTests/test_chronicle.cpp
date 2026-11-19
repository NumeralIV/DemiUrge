// DEMIURGE — SimTests/test_chronicle.cpp : dm::chronicle (SPEC-SIM 3.11, BUILD.md A13).
// No JS tests existed (chronicle.js was a placeholder): these are the A13 acceptance checks written
// fresh. In `run.ps1 chronicle` every other module is a stub (Civ honest reads, Society regard
// zeros / Forgotten, Names placeholders): the tests build a helper world with hand-named cultures and
// settlements through the factories and emit the sim events themselves, with realistic payloads,
// after changing the world the way the real emitter would (a destroyed settlement is dead before
// settlement:destroyed, a captured one already belongs to its new culture, ...).
// Output: "chronicle: sample ..." prints a few hundred entries of a scripted 2,000-year history;
// maps shots/ue-chronicle-*.png (time-lapse frames rendered from w.snapshots, entry cells marked).
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Chronicle.h"
#include "Civ.h"
#include "MapImg.h"
#include "Society.h"
#include "State.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

namespace mi = dm::mapimg;

void say(const std::string& s) { std::printf("  %s\n", s.c_str()); std::fflush(stdout); }

// ------------------------------------------------------------------ the fixture

const char* const kTownNames[] = {
	"Vornhal", "Tel Asha", "Qe-Luun", "Imbrek", "Sarnath", "Ulu Kesh", "Dorrow", "Mirathe", "Kesh Amun", "Oduya",
	"Brennick", "Tamsin Var", "Ilo", "Haddath", "Zuneri", "Penkarrow", "Asel", "Umbrin", "Garrowmere", "Nish",
	"Ostra", "Bel Thuun", "Ravik", "Saphal", "Ennor", "Kaddo", "Ytheth", "Murrow", "Avalun", "Tsoreh",
	"Lumbe", "Querrin", "Hask", "Daro Vel", "Inishte", "Kolm", "Varrah", "Esk", "Tobru", "Wenmarrow",
};
constexpr int kTownCount = int(sizeof(kTownNames) / sizeof(kTownNames[0]));

struct CultureDef { const char* name; const char* adj; const char* plural; const char* god; };
const CultureDef kCultureDefs[] = {
	{ "Umar", "Umaran", "the Umari", "Ashvel" },
	{ "Teshk", "Teshkan", "the Teshki", "Orro-Ka" },
	{ "Vael", "Vaelish", "the Vaeli", "Ashvel" },
	{ "Oru", "Oruni", "the Orun", "Mbele" },
	{ "Hsar", "Hsari", "the Hsar", "Ka-Imen" },
};

struct Fx {
	std::unique_ptr<World> w;
	std::vector<int> sites;              // spread land cells (first kTownCount usable)
	int nextSite = 0;
};

// Land cells spread at least minSep apart (deterministic: ascending hab score, ties by id).
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

int makeCulture(World& w, int def, int parent = -1) {
	Culture& c = state::newCulture(w, parent);
	const CultureDef& d = kCultureDefs[def % 5];
	c.name = d.name;
	c.adj = d.adj;
	c.plural = d.plural;
	c.godName = d.god;
	c.lang = "test";
	return c.id;
}

int makeTown(Fx& f, int cid, double pop, int tier, int cell = -1) {
	World& w = *f.w;
	if (cell < 0) {
		if (f.nextSite >= int(f.sites.size())) return -1;
		cell = f.sites[size_t(f.nextSite++)];
	}
	Settlement& s = state::newSettlement(w, cell, cid);
	s.name = kTownNames[s.id % kTownCount];
	s.pop = pop;
	s.tier = tier;
	const int id = s.id;
	if (Culture* c = state::culture(w, cid)) if (c->capital < 0) { c->capital = id; w.settlements[size_t(id)].capital = true; }
	// a little territory for the snapshots
	std::vector<int> ring;
	w.geo->ring(cell, 1 + tier / 2, ring);
	for (int m : ring) if (!state::isSea(w, m) && w.owner[size_t(m)] < 0) w.owner[size_t(m)] = int16_t(id);
	w.urban[size_t(cell)] = float(0.1 + 0.2 * tier);
	return id;
}

Fx fixture(uint32_t seed = 3, bool attach = true) {
	Fx f;
	f.w = makeWorld({ .L = 5, .seed = seed, .land = 0.4 });
	f.w->name = "Erethe";
	f.sites = spreadSites(*f.w, 0.11, 60);
	if (attach) chronicle::attach(*f.w);
	return f;
}

// The standard cast: the Umari (3 towns), the Teshki (3 towns) and the Vaeli (daughter of the Umari, 2).
struct Cast { int umari = -1, teshki = -1, vaeli = -1; std::vector<int> towns; };
Cast cast(Fx& f) {
	Cast k;
	World& w = *f.w;
	k.umari = makeCulture(w, 0);
	k.teshki = makeCulture(w, 1);
	k.vaeli = makeCulture(w, 2, k.umari);
	k.towns.push_back(makeTown(f, k.umari, 1800, 3));
	k.towns.push_back(makeTown(f, k.teshki, 900, 2));
	k.towns.push_back(makeTown(f, k.umari, 300, 1));
	k.towns.push_back(makeTown(f, k.teshki, 120, 1));
	k.towns.push_back(makeTown(f, k.vaeli, 500, 2));
	k.towns.push_back(makeTown(f, k.umari, 45, 0));
	k.towns.push_back(makeTown(f, k.teshki, 60, 1));
	k.towns.push_back(makeTown(f, k.vaeli, 80, 1));
	return k;
}

// a land cell at about `ang` from cell c (closest match, ties by id); -1 if none
int cellAt(const World& w, int c, double ang, bool sea = false) {
	int best = -1;
	double bd = 1e9;
	for (int m = 0; m < w.n; m++) {
		if (state::isSea(w, m) != sea) continue;
		const double d = std::fabs(w.geo->angle(c, m) - ang);
		if (d < bd) { bd = d; best = m; }
	}
	return best;
}
// a cell far (> minAng) from every live settlement
int remoteCell(const World& w, double minAng, bool sea) {
	for (int m = 0; m < w.n; m++) {
		if (state::isSea(w, m) != sea) continue;
		bool far = true;
		for (const Settlement& s : w.settlements) if (!s.dead && w.geo->angle(s.cell, m) < minAng) { far = false; break; }
		if (far) return m;
	}
	return -1;
}

// ------------------------------------------------------------------ prose checks


// (voice) "In the 312th year": the only digits an entry may hold are a year written as an English
// ordinal right before " year" (312th, 1st, 22nd, 103rd, 111th). Returns the text with each such
// ordinal masked as "Nth"; a wrong suffix ("312st") is reported in `fault`.
std::string maskOrdinals(const std::string& t, std::string& fault) {
	std::string out;
	size_t i = 0;
	while (i < t.size()) {
		if (!std::isdigit(static_cast<unsigned char>(t[i]))) { out.push_back(t[i++]); continue; }
		size_t j = i;
		while (j < t.size() && std::isdigit(static_cast<unsigned char>(t[j]))) j++;
		const bool prevOk = i == 0 || !std::isalnum(static_cast<unsigned char>(t[i - 1]));
		if (!prevOk || j + 2 > t.size() || t.compare(j + 2, 5, " year") != 0) { out.append(t, i, j - i); i = j; continue; }
		const std::string num = t.substr(i, j - i), sfx = t.substr(j, 2);
		const long long n = std::stoll(num);
		const long long tt = n % 100, u = n % 10;
		const char* want = (tt >= 11 && tt <= 13) ? "th" : u == 1 ? "st" : u == 2 ? "nd" : u == 3 ? "rd" : "th";
		if (sfx != want || num[0] == '0') { if (fault.empty()) fault = "bad ordinal '" + num + sfx + "'"; out.append(t, i, j + 2 - i); i = j + 2; continue; }
		out += "Nth";
		i = j + 2;
	}
	return out;
}

std::string badText(const std::string& t0) {
	std::string ordFault;
	const std::string t = maskOrdinals(t0, ordFault);
	if (!ordFault.empty()) return ordFault;
	if (t.size() < 12) return "too short";
	if (t.size() > 600) return "too long";        // (voice) the ending passages are one paragraph
	if (!(std::isupper(static_cast<unsigned char>(t[0])) || t[0] == '"')) return "no capital";
	const char last = t.back();
	if (last != '.' && last != '!' && last != '?' && last != '"') return "no end punctuation";
	for (char ch : t) {
		if (std::isdigit(static_cast<unsigned char>(ch))) return "digit (raw id or number)";
		if (ch == '{' || ch == '}' || ch == '[' || ch == ']' || ch == '|' || ch == '_') return "template syntax";
	}
	static const char* const kBadSub[] = { "  ", " ,", " .", "..", ",,", ", .", " the the ", " a a ", " of of ", " at at " };
	for (const char* b : kBadSub) if (t.find(b) != std::string::npos) return std::string("bad sequence '") + b + "'";
	if (t.rfind("The the ", 0) == 0 || t.find(". The the ") != std::string::npos) return "bad sequence 'The the'";
	// whole-word junk
	std::string word;
	for (size_t i = 0; i <= t.size(); i++) {
		const char ch = i < t.size() ? t[i] : ' ';
		if (std::isalpha(static_cast<unsigned char>(ch))) { word.push_back(ch); continue; }
		if (word == "undefined" || word == "NaN" || word == "nan" || word == "null" || word == "object" || word == "Object" ||
			word == "inf" || word == "Infinity")
			return "junk word '" + word + "'";
		word.clear();
	}
	return "";
}

// Does the text name something real from this world?
bool namesSomething(const World& w, const std::string& t) {
	if (t.find(w.name) != std::string::npos) return true;
	for (const Culture& c : w.cultures) {
		if (!c.name.empty() && t.find(c.name) != std::string::npos) return true;
		if (!c.godName.empty() && t.find(c.godName) != std::string::npos) return true;
		if (!c.epithet.empty() && t.find(c.epithet) != std::string::npos) return true;
	}
	for (const Settlement& s : w.settlements) if (!s.name.empty() && t.find(s.name) != std::string::npos) return true;
	return false;
}

int entriesSince(const World& w, int firstId, const std::string& type = "") {
	int n = 0;
	for (const ChronicleEntry& e : w.chronicle) if (e.id >= firstId && (type.empty() || e.type == type)) n++;
	return n;
}

// ------------------------------------------------------------------ a scripted emitter
// Changes the world the way the real emitter would, then emits.

struct Play {
	Fx& f;
	World& w;
	explicit Play(Fx& fx) : f(fx), w(*fx.w) {}

	int cellOf(int sid) const { const Settlement* s = state::settlement(w, sid); return s ? s->cell : -1; }
	void tier(int sid, int t) {
		Settlement& s = w.settlements[size_t(sid)];
		const int prev = s.tier;
		s.tier = t;
		w.bus.emit(EvSettlementTier{ sid, t, prev });
	}
	void build(int sid, Building::Id b) {
		w.settlements[size_t(sid)].b[b] = 1;
		w.bus.emit(EvSettlementBuilding{ sid, b });
	}
	void destroy(int sid, Cause cause) {
		Settlement& s = w.settlements[size_t(sid)];
		s.dead = true;
		s.deadYear = w.year;
		s.cause = cause;
		for (int c = 0; c < w.n; c++) if (w.owner[size_t(c)] == sid) w.owner[size_t(c)] = -1;
		w.bus.emit(EvSettlementDestroyed{ sid, s.cell, s.culture, cause });
	}
	void capture(int sid, int to, CaptureHow how) {
		const int from = w.settlements[size_t(sid)].culture;
		w.settlements[size_t(sid)].culture = to;
		w.bus.emit(EvSettlementCaptured{ sid, from, to, how });
	}
	int found(int cid, double pop, FoundedBy by) {
		const int sid = makeTown(f, cid, pop, 0);
		if (sid >= 0) w.bus.emit(EvSettlementFounded{ sid, cellOf(sid), cid, by });
		return sid;
	}
	void miracle(int sid, const char* kind, double good, double faith) {
		const Settlement* s = state::settlement(w, sid);
		if (!s) return;
		if (Culture* c = state::culture(w, s->culture)) c->seen[kind] += faith;
		w.bus.emit(EvMiracleWitnessed{ s->cell, kind, good, { sid } });
	}
	void ageUp(int cid) {
		Culture& c = w.cultures[size_t(cid)];
		const int prev = c.age;
		c.age = std::min(prev + 1, int(Age::COUNT) - 1);
		w.bus.emit(EvCultureAge{ cid, c.age, prev });
		if (c.age > w.age) { w.age = c.age; w.bus.emit(EvWorldAge{ w.age }); }
	}
	int born(int parent, int def, int cell, BirthReason why) {
		const int cid = makeCulture(w, def, parent);
		w.bus.emit(EvCultureBorn{ cid, parent, cell, why });
		return cid;
	}
};

// Emits every sim event of SPEC-SIM 2 at least once (most several ways). Returns, per event name, how
// many entries it produced.
std::map<std::string, int> emitEverything(Fx& f, Cast& k) {
	World& w = *f.w;
	Play p(f);
	std::map<std::string, int> made;
	auto run = [&](const std::string& label, const std::function<void()>& fn) {
		w.year += 13;                                    // past the per-culture rate limits of the god's acts
		const int before = w.nextId.entry;
		fn();
		made[label] += w.nextId.entry - before;
	};
	const int capCell = p.cellOf(k.towns[0]);
	const int t1 = k.towns[1], t2 = k.towns[2];
	const int nearLand = cellAt(w, capCell, 0.05);
	const int nearSea = cellAt(w, capCell, 0.10, true);
	const int farLand = remoteCell(w, 0.4, false);
	const int farSea = remoteCell(w, 0.4, true);

	// genesis
	w.year = 0;
	run("phase water", [&] { w.phase = Phase::Water; w.bus.emit(EvPhase{ Phase::Water, Phase::Molten }); });
	run("meteor:impact comet", [&] { for (int i = 0; i < 9; i++) w.bus.emit(EvMeteorImpact{ farSea, 0.03, ImpactKind::Comet, true }); });
	run("phase life", [&] { w.phase = Phase::Life; w.bus.emit(EvPhase{ Phase::Life, Phase::Water }); });
	run("phase people", [&] { w.phase = Phase::People; w.bus.emit(EvPhase{ Phase::People, Phase::Life }); });
	run("phase history", [&] { w.phase = Phase::History; w.bus.emit(EvPhase{ Phase::History, Phase::People }); });
	// the people
	run("culture:born awaken", [&] { w.bus.emit(EvCultureBorn{ k.umari, -1, capCell, BirthReason::Awaken }); });
	run("settlement:founded awaken", [&] { w.bus.emit(EvSettlementFounded{ k.towns[0], capCell, k.umari, FoundedBy::Awaken }); });
	run("settlement:founded settlers", [&] { p.found(k.umari, 30, FoundedBy::Settlers); });
	run("settlement:founded settlers", [&] { p.found(k.teshki, 30, FoundedBy::Settlers); });
	run("settlement:tier village", [&] { p.tier(k.towns[5], 1); });
	run("settlement:tier town", [&] { p.tier(k.towns[3], 2); });
	run("settlement:tier city", [&] { p.tier(t1, 3); });
	run("settlement:tier metropolis", [&] { p.tier(k.towns[0], 4); });
	run("settlement:tier down", [&] { p.tier(k.towns[3], 1); });
	for (int b = 0; b < Building::COUNT; b++) {
		const auto bid = Building::Id(b);
		run(std::string("settlement:building ") + kBuildingNames[b], [&] { p.build(t2, bid); });
	}
	run("settlement:building second shrine", [&] { p.build(k.towns[5], Building::SHRINE); });
	run("settlement:famine", [&] { w.settlements[size_t(t2)].famine = 3; w.bus.emit(EvSettlementFamine{ t2 }); });
	run("settlement:plague start", [&] { w.settlements[size_t(t1)].plague = 0.6; w.bus.emit(EvSettlementPlague{ t1, true }); });
	// (voice review 2) a plague's end and a drawn war are held back until the tick is over (a Heal or an Omen of
	// Peace in the same tick tells them instead); the yearly group's updateEpithets writes them
	run("settlement:plague end", [&] { w.settlements[size_t(t1)].plague = 0; w.bus.emit(EvSettlementPlague{ t1, false }); chronicle::updateEpithets(w); });
	run("prayer start", [&] { w.bus.emit(EvPrayer{ t2, PrayerKind::Rain, true }); });
	run("prayer:answered", [&] { w.bus.emit(EvPrayerAnswered{ t2, PrayerKind::Rain }); });
	run("prayer start", [&] { w.bus.emit(EvPrayer{ t1, PrayerKind::Heal, true }); });
	w.year += 6;
	run("prayer end unanswered", [&] { w.bus.emit(EvPrayer{ t1, PrayerKind::Heal, false }); });
	run("prayer start", [&] { w.bus.emit(EvPrayer{ k.towns[3], PrayerKind::Peace, true }); });
	w.year -= 8;                                         // ends 3 years after it began: the cause went away
	run("prayer end early", [&] { w.bus.emit(EvPrayer{ k.towns[3], PrayerKind::Peace, false }); });
	run("prayer:answered", [&] { w.bus.emit(EvPrayerAnswered{ k.towns[4], PrayerKind::Bloom }); });
	run("prayer:answered", [&] { w.bus.emit(EvPrayerAnswered{ t1, PrayerKind::Heal }); });
	run("prayer:answered", [&] { w.bus.emit(EvPrayerAnswered{ k.towns[3], PrayerKind::Peace }); });
	// the god's acts (miracle:witnessed carries the brush and grace powers)
	run("miracle rain thirst", [&] { p.miracle(t2, "rain", 0.6, 0.1); });
	w.year += 20;                                        // (voice review 2) one brush miracle per town per brushTownGap (30) years
	run("miracle rain soft", [&] { p.miracle(t2, "rain", 0.1, 0.1); });
	run("miracle bloom", [&] { p.miracle(k.towns[4], "bloom", 0.4, 0.1); });
	run("miracle raise harm", [&] { p.miracle(t1, "raise", -0.5, 0.05); });
	w.year += 20;                                        // (voice review 2) one brush miracle per town per brushTownGap (30) years
	run("miracle raise", [&] { p.miracle(t1, "raise", 0, 0.05); });
	w.year += 20;                                        // (voice review 2) one brush miracle per town per brushTownGap (30) years
	run("miracle lower", [&] { p.miracle(t1, "lower", 0, 0.05); });
	run("miracle heal", [&] { p.miracle(t1, "heal", 1, 0.12); });
	run("miracle inspire", [&] { p.miracle(k.towns[0], "inspire", 0.6, 0.08); });
	run("miracle peace", [&] { p.miracle(k.towns[1], "peace", 0.8, 0.1); });
	run("miracle plague", [&] { p.miracle(k.towns[6], "plague", -0.7, 0.05); });
	run("miracle silent kinds", [&] {
		for (const char* kind : { "lightning", "quake", "meteor", "volcano", "prophet", "nuke" })
			w.bus.emit(EvMiracleWitnessed{ capCell, kind, -0.5, { k.towns[0] } });
		w.bus.emit(EvMiracleWitnessed{ capCell, "rain", 0.6, {} });
	});
	run("power:used", [&] { w.bus.emit(EvPowerUsed{ "rain", capCell, 0.1, true }); });
	run("awe:short", [&] { w.bus.emit(EvAweShort{ "meteor" }); });
	run("lightning god", [&] { w.bus.emit(EvLightning{ nearLand, false }); });
	run("lightning natural", [&] { w.bus.emit(EvLightning{ nearLand, true }); });
	run("lightning far", [&] { w.bus.emit(EvLightning{ farLand, true }); });
	run("meteor:launched", [&] { w.bus.emit(EvMeteorLaunched{ nearLand, 1.6, ImpactKind::Meteor, 7 }); });
	run("meteor:impact land", [&] { w.bus.emit(EvMeteorImpact{ nearLand, 0.05, ImpactKind::Meteor, false }); });
	run("meteor:impact sea", [&] { w.bus.emit(EvMeteorImpact{ nearSea, 0.05, ImpactKind::Meteor, true }); });
	run("meteor:impact far", [&] { w.bus.emit(EvMeteorImpact{ farLand, 0.05, ImpactKind::Meteor, false }); });
	run("tsunami:start", [&] { w.bus.emit(EvTsunamiStart{ nearSea }); });
	run("tsunami:hit", [&] { w.bus.emit(EvTsunamiHit{ capCell, 1.4 }); });
	run("volcano:start god", [&] { w.bus.emit(EvVolcanoStart{ nearLand, 3, false }); });
	run("volcano:start natural", [&] { w.bus.emit(EvVolcanoStart{ p.cellOf(t1), 4, true }); });
	run("volcano:start far", [&] { w.bus.emit(EvVolcanoStart{ farLand, 5, true }); });
	run("volcano:end", [&] { w.bus.emit(EvVolcanoEnd{ nearLand, 3 }); });
	run("quake god", [&] { w.bus.emit(EvQuake{ nearLand, 0.08, 1, false }); });
	run("quake natural", [&] { w.bus.emit(EvQuake{ p.cellOf(t1), 0.08, 0.7, true }); });
	run("fire:start", [&] { w.bus.emit(EvFireStart{ nearLand }); });
	run("storm start", [&] { w.bus.emit(EvStorm{ p.cellOf(t2), 0.15, true }); });
	run("storm end", [&] { w.bus.emit(EvStorm{ p.cellOf(t2), 0.15, false }); });
	run("drought start", [&] { w.bus.emit(EvDrought{ p.cellOf(t2), 0.2, true }); });
	run("drought end", [&] { w.bus.emit(EvDrought{ p.cellOf(t2), 0.2, false }); });
	// peoples, wars, trade
	run("culture:age bronze..space", [&] { for (int a = 0; a < 6; a++) p.ageUp(k.umari); });
	run("culture:age second people", [&] { p.ageUp(k.teshki); });
	run("war:declared", [&] { w.bus.emit(EvWarDeclared{ k.teshki, k.umari }); });
	run("battle lost", [&] { w.bus.emit(EvBattle{ p.cellOf(k.towns[2]), k.teshki, k.umari, false, k.towns[2] }); });
	run("battle won", [&] { w.bus.emit(EvBattle{ p.cellOf(k.towns[5]), k.teshki, k.umari, true, k.towns[5] }); });
	run("settlement:captured war", [&] { p.capture(k.towns[5], k.teshki, CaptureHow::War); });
	run("war:ended victor", [&] { w.bus.emit(EvWarEnded{ k.teshki, k.umari, k.teshki }); });
	run("war:declared", [&] { w.bus.emit(EvWarDeclared{ k.umari, k.vaeli }); });
	run("war:ended draw", [&] { w.bus.emit(EvWarEnded{ k.umari, k.vaeli, -1 }); chronicle::updateEpithets(w); });
	run("trade:route same", [&] { w.bus.emit(EvTradeRoute{ k.towns[0], k.towns[2] }); });
	run("trade:route foreign", [&] { w.bus.emit(EvTradeRoute{ k.towns[0], k.towns[1] }); });
	run("prophet:risen", [&] {
		w.cultures[size_t(k.umari)].prophet = Prophet{ "Ishmavel", k.towns[0], w.year + 60 };
		w.bus.emit(EvProphetRisen{ k.umari, k.towns[0], "Ishmavel" });
	});
	for (Edict ed : { Edict::Peace, Edict::Expand, Edict::Build, Edict::Crusade }) {
		run(std::string("edict ") + toStr(ed), [&] { w.cultures[size_t(k.umari)].edict = ed; w.bus.emit(EvEdict{ k.umari, ed }); });
	}
	run("wonder:built", [&] {
		w.settlements[size_t(k.towns[0])].b[Building::WONDER] = 1;
		w.bus.emit(EvSettlementBuilding{ k.towns[0], Building::WONDER });
		w.bus.emit(EvWonderBuilt{ k.towns[0], "the Hanging Stair" });
	});
	run("prophet:died", [&] { w.cultures[size_t(k.umari)].prophet.reset(); w.bus.emit(EvProphetDied{ k.umari, "Ishmavel" }); });
	run("culture:born schism (+ its first capture)", [&] {
		const int cid = p.born(k.teshki, 3, p.cellOf(k.towns[6]), BirthReason::Schism);
		p.capture(k.towns[6], cid, CaptureHow::Schism);
	});
	run("settlement:captured schism (later)", [&] { p.capture(k.towns[3], int(w.cultures.size()) - 1, CaptureHow::Schism); });
	run("culture:born distance", [&] { p.born(k.umari, 4, p.cellOf(k.towns[4]), BirthReason::Distance); });
	// settlers lost and other units
	run("unit:spawned", [&] { w.bus.emit(EvUnitSpawned{ 9, UnitType::Settler, k.umari, capCell }); });
	run("unit:arrived", [&] { w.bus.emit(EvUnitArrived{ 9, UnitType::Settler, k.umari, nearLand }); });
	run("unit:died settler", [&] { w.bus.emit(EvUnitDied{ 10, UnitType::Settler, k.umari, nearLand, "lava" }); });
	run("unit:died army", [&] { w.bus.emit(EvUnitDied{ 11, UnitType::Army, k.umari, nearLand, "war" }); });
	run("unit:died disbanded", [&] { w.bus.emit(EvUnitDied{ 12, UnitType::Settler, k.umari, nearLand, "disbanded" }); });
	// the late ages
	run("climate warming", [&] { for (double v : { 1.0, 2.0, 3.0 }) w.bus.emit(EvClimateMilestone{ MilestoneKind::Warming, v }); });
	run("climate cooling", [&] { w.bus.emit(EvClimateMilestone{ MilestoneKind::Cooling, -2 }); });
	run("climate sea-rise", [&] { w.bus.emit(EvClimateMilestone{ MilestoneKind::SeaRise, 0.2 }); });
	run("climate sea-fall", [&] { w.bus.emit(EvClimateMilestone{ MilestoneKind::SeaFall, -0.2 }); });
	run("climate ice-age", [&] { w.bus.emit(EvClimateMilestone{ MilestoneKind::IceAge, -4 }); });
	run("climate thaw", [&] { w.bus.emit(EvClimateMilestone{ MilestoneKind::Thaw, 0 }); });
	run("nuke", [&] { w.bus.emit(EvNuke{ p.cellOf(t1), k.umari, k.teshki }); p.destroy(t1, Cause::Nuke); });
	run("nuke second", [&] { w.bus.emit(EvNuke{ p.cellOf(k.towns[0]), k.teshki, k.umari }); });
	run("ark:started", [&] { w.bus.emit(EvArkStarted{ k.umari, k.towns[0] }); });
	run("ark:launched", [&] { w.bus.emit(EvArkLaunched{ k.umari, k.towns[0] }); });
	// destruction, every cause
	const Cause causes[] = { Cause::Drowned, Cause::Meteor, Cause::Lava, Cause::War, Cause::Famine, Cause::Plague,
		Cause::Quake, Cause::Abandoned, Cause::Wave, Cause::Lightning };
	for (Cause c : causes) {
		run(std::string("settlement:destroyed ") + toStr(c), [&] {
			const int sid = p.found(k.vaeli, 200, FoundedBy::Settlers);
			w.settlements[size_t(sid)].tier = 2;
			p.destroy(sid, c);
		});
	}
	run("culture:died", [&] {
		for (const Settlement& s : w.settlements) if (s.culture == k.vaeli && !s.dead) p.destroy(s.id, Cause::Abandoned);
		w.cultures[size_t(k.vaeli)].dead = true;
		w.bus.emit(EvCultureDied{ k.vaeli });
	});
	run("chronicle:entry (foreign)", [&] { ChronicleEntry e; e.text = "x"; w.bus.emit(EvChronicleEntry{ e }); });
	run("people:lost", [&] { w.bus.emit(EvPeopleLost{}); });
	for (Mood m : { Mood::Beloved, Mood::Feared, Mood::Forgotten, Mood::Parting }) {
		run(std::string("ending ark ") + toStr(m), [&] { w.bus.emit(EvEnding{ EndingType::Ark, m, k.umari, k.towns[0], w.year }); });
	}
	for (Mood m : { Mood::Ash, Mood::Quiet }) {
		run(std::string("ending silence ") + toStr(m), [&] { w.bus.emit(EvEnding{ EndingType::Silence, m, -1, -1, w.year }); });
	}
	return made;
}

// labels that must stay silent (told by another event, or not news)
bool silentLabel(const std::string& l) {
	static const char* const kSilent[] = { "power:used", "awe:short", "meteor:launched", "tsunami:start", "storm end",
		"prayer end early", "miracle silent kinds", "battle won", "culture:born awaken",
		"settlement:building wonder", "unit:spawned", "unit:arrived", "unit:died army", "unit:died disbanded",
		"chronicle:entry (foreign)", "lightning far" };
	for (const char* s : kSilent) if (l == s) return true;
	return false;
}

// ------------------------------------------------------------------ a scripted history (sample output)

struct DramaStats { int years = 0; int events = 0; };

// A believable 2,000-year history driven by a TEST rng (never w.rng). Each year: growth and tier
// changes, foundings, buildings, ages, and a random mix of the god's acts, disasters, prayers, wars,
// trade, prophets, schisms and the climate, then updateEpithets and snapshot like Sim's yearly group.
// `simDraws` makes the "sim" draw from w.rng every year (stream-separation test).
DramaStats drama(Fx& f, uint32_t seed, int years, bool simDraws = true) {
	World& w = *f.w;
	Play p(f);
	Rng dr(seed);
	DramaStats st;
	auto liveOf = [&](int cid) {
		std::vector<int> v;
		for (const Settlement& s : w.settlements) if (!s.dead && (cid < 0 || s.culture == cid)) v.push_back(s.id);
		return v;
	};
	auto pickLive = [&](int cid) -> int {
		const std::vector<int> v = liveOf(cid);
		const int i = dr.pickIndex(int(v.size()));
		return i < 0 ? -1 : v[size_t(i)];
	};
	auto liveCultures = [&] {
		std::vector<int> v;
		for (const Culture& c : w.cultures) if (!c.dead) v.push_back(c.id);
		return v;
	};
	// genesis
	w.year = 0;
	w.phase = Phase::Water; w.bus.emit(EvPhase{ Phase::Water, Phase::Molten }); chronicle::snapshot(w, true);
	const int farSea = remoteCell(w, 0.0, true);
	for (int i = 0; i < 12; i++) w.bus.emit(EvMeteorImpact{ farSea, 0.03, ImpactKind::Comet, true });
	w.phase = Phase::Life; w.bus.emit(EvPhase{ Phase::Life, Phase::Water }); chronicle::snapshot(w, true);
	w.phase = Phase::People; w.bus.emit(EvPhase{ Phase::People, Phase::Life }); chronicle::snapshot(w, true);
	const int first = makeCulture(w, 0);
	w.cultures[size_t(first)].seen["awaken"] = 0.3;
	w.bus.emit(EvCultureBorn{ first, -1, f.sites[0], BirthReason::Awaken });
	const int home = makeTown(f, first, 30, 0);
	w.settlements[size_t(home)].devotion = 0.6;
	w.bus.emit(EvSettlementFounded{ home, p.cellOf(home), first, FoundedBy::Awaken });
	w.phase = Phase::History; w.bus.emit(EvPhase{ Phase::History, Phase::People }); chronicle::snapshot(w, true);
	chronicle::updateEpithets(w);

	std::map<std::pair<int, int>, double> wars;   // (a, b) -> year declared
	int prophetOf = -1;
	double prophetUntil = 0;
	bool arkStarted = false;
	int awoken = 1;
	for (int y = 1; y <= years; y++) {
		w.year = y;
		st.years = y;
		if (simDraws) w.vars.num["test_draw"] = w.rng.f();     // the "sim" uses its stream every year
		const int eraAge = std::min(6, y / 320);
		// ages
		for (int cid : liveCultures()) {
			Culture& c = w.cultures[size_t(cid)];
			if (c.age < eraAge && dr.chance(0.04)) { p.ageUp(cid); st.events++; }
		}
		// a second people wakes, a third later
		if ((y == 260 || y == 700) && awoken < 3) {
			const int cid = makeCulture(w, awoken == 1 ? 1 : 3);
			awoken++;
			w.cultures[size_t(cid)].seen["awaken"] = 0.3;
			w.bus.emit(EvCultureBorn{ cid, -1, -1, BirthReason::Awaken });
			const int sid = makeTown(f, cid, 30, 0);
			if (sid >= 0) w.bus.emit(EvSettlementFounded{ sid, p.cellOf(sid), cid, FoundedBy::Awaken });
			st.events++;
		}
		// growth, tiers, buildings
		static const double kGate[5] = { 0, 60, 400, 1500, 12000 };
		for (int sid : liveOf(-1)) {
			Settlement& s = w.settlements[size_t(sid)];
			const int age = w.cultures[size_t(s.culture)].age;
			const int maxTier = age >= 4 ? 4 : age >= 2 ? 3 : age >= 1 ? 2 : 1;
			static const double kCap[5] = { 50, 300, 1400, 11000, 80000 };
			s.pop = std::min(kCap[maxTier] * (0.8 + 0.4 * hashf(uint32_t(sid))), s.pop * (1.0 + 0.004 + 0.006 * dr.f()));
			if (s.tier < maxTier && s.pop >= kGate[s.tier + 1]) { p.tier(sid, s.tier + 1); st.events++; }
			struct Gate { Building::Id b; int age, tier; };
			static const Gate kB[] = { { Building::SHRINE, 0, 0 }, { Building::GRANARY, 1, 1 }, { Building::TEMPLE, 1, 2 },
				{ Building::PORT, 2, 1 }, { Building::MARKET, 2, 2 }, { Building::MINE, 2, 1 }, { Building::ACADEMY, 3, 3 },
				{ Building::FACTORY, 4, 3 }, { Building::PLANT, 5, 3 } };
			for (const Gate& g : kB) {
				if (!s.has(g.b) && age >= g.age && s.tier >= g.tier && s.pop >= 30 && dr.chance(0.03)) { p.build(sid, g.b); st.events++; break; }
			}
		}
		// settlers
		for (int cid : liveCultures()) {
			if (dr.chance(0.05 + 0.002 * double(liveOf(cid).size()))) { if (p.found(cid, 30 + 20 * dr.f(), FoundedBy::Settlers) >= 0) st.events++; }
		}
		// the god (a moderately active, mixed player)
		const int s0 = pickLive(-1);
		if (s0 >= 0) {
			const double r = dr.f();
			if (r < 0.05) { p.miracle(s0, "rain", dr.chance(0.5) ? 0.6 : 0.1, 0.08); st.events++; }
			else if (r < 0.08) { p.miracle(s0, "bloom", 0.4, 0.08); st.events++; }
			else if (r < 0.09) { p.miracle(s0, "heal", 1, 0.12); st.events++; }
			else if (r < 0.10 && eraAge >= 1) { p.miracle(s0, "inspire", 0.6, 0.08); st.events++; }
			else if (r < 0.11) { p.miracle(s0, dr.chance(0.5) ? "raise" : "lower", dr.chance(0.5) ? -0.5 : 0, 0.04); st.events++; }
			else if (r < 0.125) {
				w.cultures[size_t(w.settlements[size_t(s0)].culture)].seen["lightning"] += 0.05;
				w.bus.emit(EvLightning{ cellAt(w, p.cellOf(s0), 0.04), false }); st.events++;
			} else if (r < 0.13 && y > 600) {
				const int c = cellAt(w, p.cellOf(s0), 0.06);
				w.cultures[size_t(w.settlements[size_t(s0)].culture)].seen["meteor"] += 0.25;
				w.bus.emit(EvMeteorImpact{ c, 0.05, ImpactKind::Meteor, false }); st.events++;
				if (dr.chance(0.4)) p.destroy(s0, Cause::Meteor);
			} else if (r < 0.133 && y > 900) {
				w.cultures[size_t(w.settlements[size_t(s0)].culture)].seen["volcano"] += 0.25;
				const int c = cellAt(w, p.cellOf(s0), 0.08);
				w.bus.emit(EvVolcanoStart{ c, y, false }); st.events++;
			} else if (r < 0.138 && y > 300) {
				w.cultures[size_t(w.settlements[size_t(s0)].culture)].seen["quake"] += 0.1;
				w.bus.emit(EvQuake{ cellAt(w, p.cellOf(s0), 0.03), 0.08, 1, false }); st.events++;
			} else if (r < 0.142 && y > 400) {
				p.miracle(s0, "plague", -0.7, 0.05);
				w.bus.emit(EvSettlementPlague{ s0, true }); st.events++;
			}
		}
		// nature
		const int s1 = pickLive(-1);
		if (s1 >= 0) {
			const double r = dr.f();
			const int c1 = p.cellOf(s1);
			if (r < 0.12) { w.bus.emit(EvFireStart{ cellAt(w, c1, 0.05 + 0.05 * dr.f()) }); st.events++; }
			else if (r < 0.15) { w.bus.emit(EvLightning{ cellAt(w, c1, 0.06), true }); st.events++; }
			else if (r < 0.17) { w.bus.emit(EvStorm{ c1, 0.15, true }); st.events++; }
			else if (r < 0.185) { w.bus.emit(EvDrought{ c1, 0.2, true }); w.bus.emit(EvPrayer{ s1, PrayerKind::Rain, true }); st.events += 2; }
			else if (r < 0.195) { w.bus.emit(EvDrought{ c1, 0.2, false }); st.events++; }
			else if (r < 0.2) { w.bus.emit(EvQuake{ c1, 0.08, 0.6, true }); st.events++; }
			else if (r < 0.215) { w.settlements[size_t(s1)].famine = 3; w.bus.emit(EvSettlementFamine{ s1 }); st.events++; }
			else if (r < 0.222) { w.bus.emit(EvSettlementPlague{ s1, true }); w.bus.emit(EvPrayer{ s1, PrayerKind::Heal, true }); st.events += 2; }
			else if (r < 0.235) { w.bus.emit(EvPrayerAnswered{ s1, dr.chance(0.5) ? PrayerKind::Rain : PrayerKind::Heal }); st.events++; }
			else if (r < 0.25) { w.bus.emit(EvPrayer{ s1, PrayerKind::Rain, false }); st.events++; }
			else if (r < 0.256 && w.settlements[size_t(s1)].tier <= 1 && liveOf(-1).size() > 4) { p.destroy(s1, dr.chance(0.5) ? Cause::Famine : Cause::Abandoned); st.events++; }
			else if (r < 0.259) { w.bus.emit(EvTsunamiHit{ c1, 1.2 }); if (dr.chance(0.5) && liveOf(-1).size() > 4) p.destroy(s1, Cause::Wave); st.events++; }
			else if (r < 0.262 && y > 900) {
				const int sp = pickLive(-1);
				if (sp >= 0) w.bus.emit(EvUnitDied{ 1000 + y, UnitType::Settler, w.settlements[size_t(sp)].culture, cellAt(w, p.cellOf(sp), 0.1), "lava" });
				st.events++;
			}
		}
		// trade
		if (y > 400 && dr.chance(0.03)) {
			const int a = pickLive(-1), b = pickLive(-1);
			if (a >= 0 && b >= 0 && a != b) { w.bus.emit(EvTradeRoute{ a, b }); st.events++; }
		}
		// prophets and edicts
		if (prophetOf < 0 && y > 150 && dr.chance(0.01)) {
			const int sid = pickLive(-1);
			if (sid >= 0) {
				prophetOf = w.settlements[size_t(sid)].culture;
				static const char* const kProphets[] = { "Ishmavel", "Tor the Lame", "Anuket", "Brannoch", "Seyle", "Quill" };
				const std::string nm = kProphets[dr.pickIndex(6)];
				w.cultures[size_t(prophetOf)].prophet = Prophet{ nm, sid, y + 60.0 };
				w.cultures[size_t(prophetOf)].seen["prophet"] += 0.15;
				prophetUntil = y + 40 + 30 * dr.f();
				w.bus.emit(EvProphetRisen{ prophetOf, sid, nm });
				if (dr.chance(0.7)) {
					const Edict ed = Edict(1 + dr.pickIndex(4));
					w.cultures[size_t(prophetOf)].edict = ed;
					w.bus.emit(EvEdict{ prophetOf, ed });
					if (ed == Edict::Build) {
						const int cap = w.cultures[size_t(prophetOf)].capital;
						if (cap >= 0 && !w.settlements[size_t(cap)].dead && !w.settlements[size_t(cap)].has(Building::WONDER)) {
							static const char* const kWonders[] = { "the Hanging Stair", "the Mirror of Ashvel", "the Thousand Steps", "the Bell Tower" };
							w.settlements[size_t(cap)].b[Building::WONDER] = 1;
							w.bus.emit(EvSettlementBuilding{ cap, Building::WONDER });
							w.bus.emit(EvWonderBuilt{ cap, kWonders[dr.pickIndex(4)] });
						}
					}
				}
				st.events++;
			}
		}
		if (prophetOf >= 0 && y >= prophetUntil) {
			Culture& c = w.cultures[size_t(prophetOf)];
			const std::string nm = c.prophet ? c.prophet->name : std::string("Ishmavel");
			c.prophet.reset();
			c.edict = Edict::None;
			w.bus.emit(EvProphetDied{ prophetOf, nm });
			prophetOf = -1;
			st.events++;
		}
		// schisms
		if ((y == 900 || y == 1400) && int(liveCultures().size()) < 5) {
			const int src = pickLive(-1);
			if (src >= 0) {
				const int parent = w.settlements[size_t(src)].culture;
				const int cid = p.born(parent, y == 900 ? 2 : 4, p.cellOf(src), y == 900 ? BirthReason::Distance : BirthReason::Schism);
				p.capture(src, cid, CaptureHow::Schism);
				for (int sid : liveOf(parent)) if (sid != src && w.geo->angle(p.cellOf(sid), p.cellOf(src)) < 0.28) p.capture(sid, cid, CaptureHow::Schism);
				st.events++;
			}
		}
		// wars
		const std::vector<int> lc = liveCultures();
		if (y > 350 && lc.size() >= 2 && wars.empty() && dr.chance(0.012)) {
			const int a = lc[size_t(dr.pickIndex(int(lc.size())))];
			int b = lc[size_t(dr.pickIndex(int(lc.size())))];
			if (a != b) { wars[{ a, b }] = y; w.stats.wars++; w.bus.emit(EvWarDeclared{ a, b }); st.events++; }
		}
		for (auto it = wars.begin(); it != wars.end();) {
			const int a = it->first.first, b = it->first.second;
			if (w.cultures[size_t(a)].dead || w.cultures[size_t(b)].dead) { it = wars.erase(it); continue; }
			if (dr.chance(0.15)) {
				const int target = pickLive(b);
				if (target >= 0) {
					const bool won = dr.chance(0.45);
					w.bus.emit(EvBattle{ p.cellOf(target), a, b, won, target });
					if (won) {
						if (w.settlements[size_t(target)].pop < 100) p.destroy(target, Cause::War);
						else p.capture(target, a, CaptureHow::War);
					}
					st.events++;
				}
			}
			if (eraAge >= 5 && dr.chance(0.03)) {
				const int target = pickLive(b);
				if (target >= 0) {
					w.bus.emit(EvNuke{ p.cellOf(target), a, b });
					w.vars.lastNuke = double(y);
					p.destroy(target, Cause::Nuke);
					st.events++;
				}
			}
			if (y - it->second > 15 && dr.chance(0.06)) {
				const double v = dr.f();
				w.bus.emit(EvWarEnded{ a, b, v < 0.4 ? a : v < 0.7 ? b : -1 });
				it = wars.erase(it);
				st.events++;
				continue;
			}
			++it;
		}
		// the climate
		if (y == 1300) w.bus.emit(EvClimateMilestone{ MilestoneKind::Warming, 1 });
		if (y == 1650) w.bus.emit(EvClimateMilestone{ MilestoneKind::Warming, 2 });
		if (y == 1700) w.bus.emit(EvClimateMilestone{ MilestoneKind::SeaRise, 0.2 });
		if (y == 1900) w.bus.emit(EvClimateMilestone{ MilestoneKind::Warming, 3 });
		// drowned by the rising sea
		if (y > 1700 && dr.chance(0.01)) { const int sid = pickLive(-1); if (sid >= 0 && liveOf(-1).size() > 4) { p.destroy(sid, Cause::Drowned); st.events++; } }
		// dead cultures
		for (int cid : liveCultures()) if (liveOf(cid).empty()) { w.cultures[size_t(cid)].dead = true; w.bus.emit(EvCultureDied{ cid }); st.events++; }
		// the Ark
		if (y > years - 120 && !arkStarted && eraAge >= 6) {
			const int cid = liveCultures().empty() ? -1 : liveCultures()[0];
			if (cid >= 0 && w.cultures[size_t(cid)].capital >= 0) {
				arkStarted = true;
				w.bus.emit(EvArkStarted{ cid, w.cultures[size_t(cid)].capital });
			}
		}
		chronicle::updateEpithets(w);
		chronicle::snapshot(w, false);
	}
	if (arkStarted) {
		const int cid = liveCultures().empty() ? -1 : liveCultures()[0];
		if (cid >= 0) {
			w.vars.ark = ArkRecord{ cid, w.cultures[size_t(cid)].capital, w.year };
			w.bus.emit(EvArkLaunched{ cid, w.cultures[size_t(cid)].capital });
			w.ending = Ending{ EndingType::Ark, Mood::Parting, cid, w.cultures[size_t(cid)].capital, w.year };
			w.phase = Phase::Ended;
			w.bus.emit(EvPhase{ Phase::Ended, Phase::History });
			chronicle::snapshot(w, true);
			w.bus.emit(EvEnding{ EndingType::Ark, Mood::Parting, cid, w.cultures[size_t(cid)].capital, w.year });
		}
	}
	return st;
}

// Strip everything Chronicle owns, so two worlds can be compared on the rest.
void stripChronicle(World& w) {
	w.chronicle.clear();
	w.snapshots.clear();
	w.nextId.entry = 1;
	w.god.epithet.clear();
	for (Culture& c : w.cultures) { c.epithet.clear(); }
	w.vars.snapEvery.reset();
	w.vars.snapNext.reset();
	for (auto it = w.vars.num.begin(); it != w.vars.num.end();) {
		if (it->first.rfind("chronicle_", 0) == 0) it = w.vars.num.erase(it);
		else ++it;
	}
}

std::string epithetOf(const World& w, int cid) { return w.cultures[size_t(cid)].epithet; }

} // namespace

// ===================================================================== add / yearStr / attach

DM_TEST("chronicle: add assigns ids, clamps fields and emits chronicle:entry") {
	Fx f = fixture();
	World& w = *f.w;
	const int cid = makeCulture(w, 0);
	w.year = 312.75;
	std::vector<ChronicleEntry> seen;
	const auto tok = w.bus.on<EvChronicleEntry>([&](const EvChronicleEntry& e) { seen.push_back(e.entry); });
	ChronicleEntry* e1 = chronicle::add(w, { "note", "The Umari counted their goats.", 5, cid, 2 });
	if (!ok(e1 != nullptr, "add returns the entry")) return;
	const int id1 = e1->id;
	eq(e1->year, 312.75, "year = w.year");
	eq(e1->cell, 5);
	eq(e1->culture, cid);
	eq(e1->weight, 2);
	ChronicleEntry* e2 = chronicle::add(w, { "", "A thing happened.", 999999, 77, 9 });
	if (!ok(e2 != nullptr)) return;
	eq(e2->id, id1 + 1, "ids ascend");
	eq(e2->cell, -1, "invalid cell -> -1");
	eq(e2->culture, -1, "unknown culture -> -1");
	eq(e2->weight, 3, "weight clamped to 3");
	eq(e2->type, std::string("note"), "empty type -> note");
	ok(chronicle::add(w, { "note", "", -1, -1, 1 }) == nullptr, "empty text refused");
	eq(int(w.chronicle.size()), 2);
	eq(int(seen.size()), 2, "chronicle:entry per entry");
	if (seen.size() == 2) eq(seen[1].text, std::string("A thing happened."), "payload is a copy of the entry");
	w.bus.off(tok);
	eq(chronicle::yearStr(w), std::string("Year 312"));
	eq(chronicle::yearStr(w, 0.0), std::string("Year 0"));
	eq(chronicle::yearStr(w, 4499.99), std::string("Year 4499"));
	eq(chronicle::yearStr(w, std::nan("")), std::string("Year 0"), "NaN year never printed");
}

DM_TEST("chronicle: entries are capped (oldest minor first)") {
	Fx f = fixture();
	World& w = *f.w;
	w.tune<ChronicleTune>().maxEntries = 50;
	chronicle::add(w, { "epochal", "The first epochal thing.", -1, -1, 3 });
	for (int i = 0; i < 120; i++) chronicle::add(w, { "note", "A small thing happened.", -1, -1, (i % 7 == 0) ? 2 : 1 });
	eq(int(w.chronicle.size()), 50, "never more than maxEntries");
	ok(w.chronicle.front().weight == 3, "the epochal entry survives");
	int minor = 0;
	for (const ChronicleEntry& e : w.chronicle) minor += e.weight == 1;
	ok(minor < 50, "notable entries kept over minor ones");
	ok(std::is_sorted(w.chronicle.begin(), w.chronicle.end(), [](const ChronicleEntry& a, const ChronicleEntry& b) { return a.id < b.id; }), "order kept");
}

DM_TEST("chronicle: attach works after bus.clear and never double-subscribes") {
	Fx f = fixture(3, false);
	World& w = *f.w;
	Cast k = cast(f);
	const int base = w.bus.count();
	chronicle::attach(w);
	const int one = w.bus.count() - base;
	ok(one >= 30, "attach subscribes one handler per chronicled event (" + std::to_string(one) + ")");
	chronicle::attach(w);
	chronicle::attach(w);
	eq(w.bus.count() - base, one, "repeated attach keeps exactly one set");
	auto declare = [&] {
		const int before = int(w.chronicle.size());
		w.year += 1;
		w.bus.emit(EvWarDeclared{ k.umari, k.teshki });
		return int(w.chronicle.size()) - before;
	};
	eq(declare(), 1, "one entry per event after three attaches");
	w.bus.clear();
	dmt::watch(w);
	eq(declare(), 0, "nothing after bus.clear()");
	chronicle::attach(w);
	eq(declare(), 1, "attach after bus.clear() subscribes again, once");
	eq(w.bus.count(), one + 1, "only the chronicle set plus the watcher");
}

// ===================================================================== prose

DM_TEST("chronicle: every sim event becomes sensible prose with real names") {
	Fx f = fixture();
	World& w = *f.w;
	Cast k = cast(f);
	const std::map<std::string, int> made = emitEverything(f, k);
	int problems = 0;
	for (const auto& kv : made) {
		if (silentLabel(kv.first)) {
			if (!eq(kv.second, 0, "silent by design: " + kv.first)) problems++;
		} else if (kv.first == "culture:born schism (+ its first capture)") {
			if (!eq(kv.second, 1, "a schism is one entry: the birth tells the capture")) problems++;
		} else if (kv.first == "meteor:impact comet") {
			ok(kv.second >= 2 && kv.second <= 4, "comets: the first, then every 4th (" + std::to_string(kv.second) + " of 9)");
		} else {
			if (!ok(kv.second >= 1, "no entry for " + kv.first)) problems++;
		}
	}
	say(std::to_string(made.size()) + " event cases, " + std::to_string(w.chronicle.size()) + " entries");
	std::set<std::string> types;
	int named = 0, withCulture = 0;
	for (const ChronicleEntry& e : w.chronicle) {
		types.insert(e.type);
		const std::string why = badText(e.text);
		if (!ok(why.empty(), "[" + e.type + "] " + why + ": " + e.text)) problems++;
		ok(e.weight >= 1 && e.weight <= 3, "weight");
		ok(e.cell >= -1 && e.cell < w.n, "cell");
		if (e.culture >= 0) {
			withCulture++;
			if (namesSomething(w, e.text)) named++;
			else say("unnamed: [" + e.type + "] " + e.text);
		}
	}
	ok(named >= withCulture - 2, "entries about a people name a people, a god or a place (" + std::to_string(named) + " of " + std::to_string(withCulture) + ")");
	ok(types.size() >= 35, "many entry types (" + std::to_string(types.size()) + ")");
	noNaN(w, "after every event");
	eq(problems, 0, "problems");
	for (const ChronicleEntry& e : w.chronicle) say(chronicle::yearStr(w, e.year) + " [" + e.type + "/" + std::to_string(e.weight) + "] " + e.text);
}

DM_TEST("chronicle: repeated events vary in wording") {
	Fx f = fixture();
	World& w = *f.w;
	Cast k = cast(f);
	Play p(f);
	struct Case { const char* label; std::function<void()> fn; };
	const int capCell = p.cellOf(k.towns[0]);
	const std::vector<Case> cases = {
		{ "war", [&] { w.bus.emit(EvWarDeclared{ k.umari, k.teshki }); } },
		{ "founded", [&] { p.found(k.umari, 30, FoundedBy::Settlers); } },
		{ "meteor", [&] { w.bus.emit(EvMeteorImpact{ cellAt(w, capCell, 0.05), 0.05, ImpactKind::Meteor, false }); } },
		{ "famine", [&] { w.bus.emit(EvSettlementFamine{ k.towns[1] }); } },
		{ "prayer-answered", [&] { w.bus.emit(EvPrayerAnswered{ k.towns[2], PrayerKind::Rain }); } },
		{ "fire", [&] { w.bus.emit(EvFireStart{ cellAt(w, capCell, 0.05) }); } },
		{ "destroyed-war", [&] { const int sid = p.found(k.teshki, 200, FoundedBy::Settlers); p.destroy(sid, Cause::War); } },
	};
	for (const Case& c : cases) {
		std::vector<std::string> texts;
		for (int i = 0; i < 12; i++) {
			w.year += 25;
			const int before = w.nextId.entry;
			c.fn();
			for (const ChronicleEntry& e : w.chronicle) if (e.id >= before && e.type != "founded") texts.push_back(e.text);
			if (std::string(c.label) == "founded") for (const ChronicleEntry& e : w.chronicle) if (e.id >= before) texts.push_back(e.text);
		}
		std::set<std::string> distinct(texts.begin(), texts.end());
		int sameInRow = 0;
		for (size_t i = 1; i < texts.size(); i++) sameInRow += texts[i] == texts[i - 1];
		say(std::string(c.label) + ": " + std::to_string(distinct.size()) + " distinct of " + std::to_string(texts.size()) + ", " + std::to_string(sameInRow) + " repeats in a row");
		ok(texts.size() >= 12, std::string(c.label) + ": an entry each time");
		ok(distinct.size() * 10 >= texts.size() * 7, std::string(c.label) + ": at least 70 % distinct");
		eq(sameInRow, 0, std::string(c.label) + ": never the same text twice in a row");
	}
}

DM_TEST("chronicle: minor entries are rate-limited, notable ones are not") {
	Fx f = fixture();
	World& w = *f.w;
	Cast k = cast(f);
	Play p(f);
	const int nearLand = cellAt(w, p.cellOf(k.towns[0]), 0.05);
	int first = w.nextId.entry;
	for (int i = 0; i < 40; i++) w.bus.emit(EvFireStart{ nearLand });
	eq(entriesSince(w, first, "fire"), 1, "40 fires in one year near one people -> one entry");
	first = w.nextId.entry;
	for (int y = 0; y < 100; y++) {
		w.year += 1;
		for (int i = 0; i < 3; i++) w.bus.emit(EvFireStart{ nearLand });
	}
	const int fires = entriesSince(w, first, "fire");
	say("fires over 100 years (300 events): " + std::to_string(fires) + " entries (fireGap " + std::to_string(w.tune<ChronicleTune>().fireGap) + ")");
	const double gap = w.tune<ChronicleTune>().fireGap;
	ok(fires >= int(100 / gap) - 1 && fires <= int(100 / gap) + 1, "about one per fireGap years");
	first = w.nextId.entry;
	for (int i = 0; i < 30; i++) p.miracle(k.towns[1], "rain", 0.6, 0.02);
	eq(entriesSince(w, first, "miracle-rain"), 1, "brush rain at 2 events per second -> one entry");
	first = w.nextId.entry;
	for (int i = 0; i < 10; i++) w.bus.emit(EvWarDeclared{ k.umari, k.teshki });
	eq(entriesSince(w, first, "war"), 10, "wars are never dropped");
	first = w.nextId.entry;
	for (int i = 0; i < 6; i++) w.bus.emit(EvSettlementFamine{ k.towns[2] });
	eq(entriesSince(w, first, "famine"), 1, "famine in a village: minor, once");
	first = w.nextId.entry;
	for (int i = 0; i < 3; i++) w.bus.emit(EvSettlementFamine{ k.towns[0] });
	eq(entriesSince(w, first, "famine"), 3, "famine in a city is notable, every time");
	first = w.nextId.entry;
	const int remote = remoteCell(w, 0.4, false);
	for (int i = 0; i < 5; i++) { w.year += 20; w.bus.emit(EvFireStart{ remote }); w.bus.emit(EvLightning{ remote, true }); }
	eq(entriesSince(w, first), 0, "fires and lightning no one saw are not written");
}

// ===================================================================== stream separation

DM_TEST("chronicle: wording never draws from w.rng (sim state identical with or without it)") {
	Fx a = fixture(5, true);
	Fx b = fixture(5, false);
	const uint32_t s0 = a.w->rng.s;
	eq(b.w->rng.s, s0, "same start");
	drama(a, 77, 900);
	drama(b, 77, 900);
	ok(a.w->chronicle.size() > 100, "the attached world wrote a chronicle (" + std::to_string(a.w->chronicle.size()) + ")");
	int detachedOther = 0;
	for (const ChronicleEntry& e : b.w->chronicle) detachedOther += e.type != "epithet";
	eq(detachedOther, 0, "the detached world wrote only what updateEpithets writes");
	eq(a.w->rng.s, b.w->rng.s, "the sim stream is the same");
	// chronicle calls on their own never touch the stream
	const uint32_t before = a.w->rng.s;
	chronicle::updateEpithets(*a.w);
	chronicle::snapshot(*a.w, true);
	const chronicle::Tale t = chronicle::tale(*a.w);
	chronicle::epithet(*a.w, 0);
	eq(a.w->rng.s, before, "updateEpithets / snapshot / tale / epithet leave w.rng alone");
	const uint32_t ha = dmt::hash(*a.w);
	chronicle::tale(*a.w);
	chronicle::epithet(*a.w, 0);
	chronicle::yearStr(*a.w);
	eq(dmt::hash(*a.w), ha, "tale / epithet / yearStr are read-only (hash unchanged)");
	stripChronicle(*a.w);
	stripChronicle(*b.w);
	eq(dmt::hash(*a.w), dmt::hash(*b.w), "everything Chronicle does not own is identical");
	// deterministic prose: the same history twice gives the same chronicle
	Fx c = fixture(5, true);
	Fx d = fixture(5, true);
	drama(c, 91, 600);
	drama(d, 91, 600);
	eq(c.w->chronicle.size(), d.w->chronicle.size(), "same length");
	ok(c.w->chronicle == d.w->chronicle, "same entries, word for word");
	eq(dmt::hash(*c.w), dmt::hash(*d.w), "same hash");
}

// ===================================================================== epithets

namespace {
// One culture with a capital, attached; `career(year)` is called each year before updateEpithets.
struct Career {
	std::string first, last;
	std::vector<std::string> history;    // each change "Year N: epithet"
	int entries = 0;
};
Career runCareer(uint32_t seed, int years, const std::function<void(Fx&, int, int, int)>& act, int* cidOut = nullptr, Fx* keep = nullptr) {
	Fx f = fixture(seed);
	World& w = *f.w;
	const int cid = makeCulture(w, 0);
	w.cultures[size_t(cid)].seen["awaken"] = 0.3;
	const int sid = makeTown(f, cid, 400, 2);
	makeTown(f, cid, 200, 1);
	Career c;
	for (int y = 0; y <= years; y++) {
		w.year = y;
		act(f, cid, sid, y);
		const std::string before = epithetOf(w, cid);
		chronicle::updateEpithets(w);
		const std::string after = epithetOf(w, cid);
		if (y == 0) c.first = after;
		if (after != before) c.history.push_back(chronicle::yearStr(w) + ": " + after);
	}
	c.last = epithetOf(w, cid);
	for (const ChronicleEntry& e : w.chronicle) c.entries += e.type == "epithet";
	if (cidOut) *cidOut = cid;
	if (keep) *keep = std::move(f);
	return c;
}
std::string joined(const std::vector<std::string>& v) {
	std::string s;
	for (const std::string& x : v) s += (s.empty() ? "" : " | ") + x;
	return s;
}
bool among(const std::string& e, std::initializer_list<const char*> opts) {
	for (const char* o : opts) if (e == o) return true;
	return false;
}
} // namespace

DM_TEST("chronicle: epithets follow what a culture has seen") {
	// only rain and bloom
	const Career green = runCareer(11, 400, [](Fx& f, int cid, int, int y) {
		if (y % 6 == 0) f.w->cultures[size_t(cid)].seen[(y / 6) % 3 == 0 ? "bloom" : "rain"] += 0.06;
	});
	say("rain and bloom: " + joined(green.history));
	ok(among(green.last, { "the Rain-Giver", "the Cloud-Shepherd", "She Who Opens the Sky", "the Well-Filler",
		"the Green Hand", "the Sower", "Lord of the Second Spring" }), "rain / green epithet: " + green.last);
	// only meteors
	const Career star = runCareer(12, 400, [](Fx& f, int cid, int, int y) {
		if (y > 50 && y % 30 == 0) f.w->cultures[size_t(cid)].seen["meteor"] += 0.25;
	});
	say("meteors: " + joined(star.history));
	ok(among(star.last, { "She Who Burns the Sky", "the Hammer", "the Thrower of Stars" }), "meteor epithet: " + star.last);
	// floods: towns lost to the sea and waves
	const Career flood = runCareer(13, 400, [](Fx& f, int cid, int sid, int y) {
		World& w = *f.w;
		Play p(f);
		if (y > 50 && y % 40 == 0) {
			const int t = p.found(cid, 150, FoundedBy::Settlers);
			if (t >= 0) p.destroy(t, y % 80 == 0 ? Cause::Drowned : Cause::Wave);
			w.bus.emit(EvTsunamiHit{ p.cellOf(sid), 1.5 });
		}
	});
	say("floods: " + joined(flood.history));
	ok(among(flood.last, { "the Drowner", "the Sea's Elbow", "the Deep" }), "drowner epithet: " + flood.last);
	// silence for 800 years
	const Career silent = runCareer(14, 800, [](Fx&, int, int, int) {});
	say("silence: " + joined(silent.history));
	ok(among(silent.last, { "the Silent One", "the Sleeper", "the Empty Chair", "the Rumour" }), "silent epithet: " + silent.last);
	ok(silent.history.size() == 2, "the waker, then silence (" + std::to_string(silent.history.size()) + " names)");
	// mixed: blessings and blows alike
	const Career mixed = runCareer(15, 400, [](Fx& f, int cid, int, int y) {
		Culture& c = f.w->cultures[size_t(cid)];
		if (y % 10 == 0) c.seen["rain"] += 0.05;
		if (y % 10 == 5) c.seen["heal"] += 0.04;
		if (y % 20 == 0) c.seen["lightning"] += 0.05;
		if (y % 40 == 0) c.seen["quake"] += 0.08;
	});
	say("mixed: " + joined(mixed.history));
	ok(among(mixed.last, { "the Two-Handed", "the Laughing Storm", "the Coin-Tosser" }), "two-sided epithet: " + mixed.last);
	// a career that changes: kind for 300 years, then 300 years of wrath
	const Career turn = runCareer(16, 1000, [](Fx& f, int cid, int, int y) {
		Culture& c = f.w->cultures[size_t(cid)];
		if (y < 300 && y % 5 == 0) c.seen["heal"] += 0.06;
		if (y >= 300 && y % 8 == 0) c.seen["plague"] += 0.08;
	});
	say("healer turned wrathful: " + joined(turn.history));
	ok(turn.history.size() >= 3, "the epithet changed as the god changed");
	ok(among(turn.last, { "the Sick Wind", "the Grey Breath", "the Bringer of Fevers" }), "ends as the fever-bringer: " + turn.last);
	// every change was chronicled, once
	eq(green.entries, int(green.history.size()), "one epithet entry per change (rain)");
	eq(turn.entries, int(turn.history.size()), "one epithet entry per change (turn)");
	// stable: no flapping for a steady god
	ok(green.history.size() <= 3, "a steady god keeps its name (" + std::to_string(green.history.size()) + ")");
	// a name, once given after the waking, is held at least epithetHold years
	const double hold = ChronicleTune{}.epithetHold;
	for (const Career* cr : { &green, &star, &flood, &mixed, &turn }) {
		for (size_t i = 2; i < cr->history.size(); i++) {
			const int y0 = std::stoi(cr->history[i - 1].substr(5)), y1 = std::stoi(cr->history[i].substr(5));
			ok(y1 - y0 >= hold, "held " + std::to_string(y1 - y0) + " years: " + joined(cr->history));
		}
	}
}

DM_TEST("chronicle: a blessed people and a struck people name the god differently; god.epithet follows the largest") {
	Fx f = fixture(21);
	World& w = *f.w;
	const int blessed = makeCulture(w, 0);
	const int struck = makeCulture(w, 1);
	const int sb = makeTown(f, blessed, 300, 1);
	const int ss = makeTown(f, struck, 5000, 3);
	for (int y = 0; y <= 200; y++) {
		w.year = y;
		if (y % 7 == 0) w.cultures[size_t(blessed)].seen["rain"] += 0.05;
		if (y % 9 == 0) w.cultures[size_t(blessed)].seen["bloom"] += 0.04;
		if (y % 25 == 0) w.cultures[size_t(struck)].seen["meteor"] += 0.25;
		if (y % 30 == 0) w.cultures[size_t(struck)].seen["volcano"] += 0.2;
		chronicle::updateEpithets(w);
	}
	const std::string eb = epithetOf(w, blessed), es = epithetOf(w, struck);
	say("blessed (" + w.cultures[size_t(blessed)].plural + "): " + eb + "; struck (" + w.cultures[size_t(struck)].plural + "): " + es + "; god: " + w.god.epithet);
	ok(eb != es, "different names for the same god");
	eq(chronicle::epithet(w, blessed), eb, "epithet() agrees with the stored one");
	eq(chronicle::epithet(w, struck), es);
	eq(w.god.epithet, es, "god.epithet = the most populous culture's (the struck one, 5,000 people)");
	// the blessed people grow past the struck one
	w.settlements[size_t(sb)].pop = 20000;
	w.year += 1;
	chronicle::updateEpithets(w);
	eq(w.god.epithet, eb, "god.epithet follows the most populous culture");
	// a dead culture is ignored
	w.settlements[size_t(sb)].dead = true;
	w.cultures[size_t(blessed)].dead = true;
	w.year += 1;
	chronicle::updateEpithets(w);
	eq(w.god.epithet, es, "dead cultures do not name the god");
	static_cast<void>(ss);
	eq(chronicle::epithet(w, 9999), std::string(), "bad culture id -> empty");
}

DM_TEST("chronicle: with no deed to remember, regard names the god (beloved / dreaded)") {
	dmt::stub::Guard guard(false);
	Fx f = fixture(22);
	World& w = *f.w;
	const int calm = makeCulture(w, 0);
	const int loving = makeCulture(w, 1);
	const int fearful = makeCulture(w, 3);
	makeTown(f, calm, 300, 1);
	makeTown(f, loving, 300, 1);
	makeTown(f, fearful, 300, 1);
	if (dmt::stub::stubbed("society")) {
		dmt::stub::hooks().society_regard = [loving, fearful](const World&, int cid) -> society::Regard {
			society::Regard r;
			r.devotion = 0.5;
			if (cid == loving) { r.love = 0.6; r.mood = Mood::Beloved; }
			if (cid == fearful) { r.love = -0.6; r.mood = Mood::Feared; }
			return r;
		};
	} else {
		// the real Society: set the settlements' feelings directly
		for (Settlement& s : w.settlements) {
			s.devotion = 0.5;
			s.love = s.culture == loving ? 0.6 : s.culture == fearful ? -0.6 : 0;
		}
	}
	w.year = 10;
	chronicle::updateEpithets(w);
	say("calm: " + epithetOf(w, calm) + "; loving: " + epithetOf(w, loving) + "; fearful: " + epithetOf(w, fearful));
	ok(among(epithetOf(w, calm), { "the Maker", "the Waker", "the First Voice", "the Dawn-Caller" }), "calm: waker");
	ok(among(epithetOf(w, loving), { "the Gentle Hand", "the Kind Sky", "Mother of Harvests", "the Patient One", "He Who Keeps the Lamp Lit" }), "loving: beloved");
	ok(among(epithetOf(w, fearful), { "the Unkind Sky", "the Hungry One", "the Tyrant Above", "the One We Do Not Name" }), "fearful: dreaded");
	// a daughter culture with nothing of its own remembers the god of its fathers
	const int daughter = makeCulture(w, 2, calm);
	makeTown(f, daughter, 100, 1);
	w.year = 11;
	chronicle::updateEpithets(w);
	ok(among(epithetOf(w, daughter), { "the God of Our Fathers", "the Old God", "the Remembered One", "the Elder" }), "daughter: ancestral (" + epithetOf(w, daughter) + ")");
}

DM_TEST("chronicle: epithet memory survives save and load without re-announcing") {
	Fx f = fixture(23);
	World& w = *f.w;
	const int cid = makeCulture(w, 0);
	makeTown(f, cid, 400, 2);
	for (int y = 0; y <= 120; y++) {
		w.year = y;
		if (y % 10 == 0) w.cultures[size_t(cid)].seen["meteor"] += 0.2;
		chronicle::updateEpithets(w);
	}
	const std::string before = epithetOf(w, cid);
	std::vector<uint8_t> bytes;
	if (!ok(state::serialize(w, bytes), "serialize")) return;
	std::string why;
	auto w2 = state::deserialize(bytes, &why);
	if (!ok(w2 != nullptr, "deserialize: " + why)) return;
	dmt::watch(*w2);
	chronicle::attach(*w2);
	const int n0 = int(w2->chronicle.size());
	w2->year += 1;
	chronicle::updateEpithets(*w2);
	eq(epithetOf(*w2, cid), before, "same epithet after load");
	eq(int(w2->chronicle.size()), n0, "no new epithet entry after load");
	eq(w2->chronicle.back().text, w.chronicle.back().text, "the chronicle itself is saved");
}

// ===================================================================== snapshots

DM_TEST("chronicle: snapshots are due by vars.snapNext, forced on phase change, thinned past 200") {
	Fx f = fixture(31);
	World& w = *f.w;
	Cast k = cast(f);
	w.heat = 0.4;
	w.phase = Phase::Water;
	w.year = 0;
	ok(chronicle::snapshot(w, true), "forced frame in genesis");
	eq(int(w.snapshots.size()), 1);
	if (w.snapshots.size() == 1) near(w.snapshots[0].heat, 0.4, 1e-12, "frame carries heat");
	ok(w.vars.snapEvery && *w.vars.snapEvery == 25, "snapEvery starts at 25");
	ok(w.vars.snapNext && *w.vars.snapNext == 25, "next frame at year 25");
	w.heat = 0;
	w.phase = Phase::History;
	ok(chronicle::snapshot(w, true), "forced on the phase change");
	ok(!chronicle::snapshot(w, false), "not due at year 0");
	w.year = 24.75;
	ok(!chronicle::snapshot(w, false), "not due at 24.75");
	w.year = 25;
	ok(chronicle::snapshot(w, false), "due at 25");
	ok(*w.vars.snapNext == 50, "then at 50");
	ok(!chronicle::snapshot(w, false), "not twice in a year");
	// content
	w.urban[size_t(w.settlements[size_t(k.towns[0])].cell)] = 0.5f;
	w.year = 50;
	chronicle::snapshot(w, false);
	const Snapshot& fr = w.snapshots.back();
	eq(int(fr.biome.size()), w.n);
	eq(int(fr.owner.size()), w.n);
	eq(int(fr.urban.size()), w.n);
	const int cc = w.settlements[size_t(k.towns[0])].cell;
	eq(int(fr.urban[size_t(cc)]), 128, "urban 0.5 -> round(127.5) = 128");
	eq(int(fr.owner[size_t(cc)]), k.umari, "owner = the culture id of the owning settlement");
	int unowned = 0;
	for (int c = 0; c < w.n; c++) if (w.owner[size_t(c)] < 0) { unowned++; if (fr.owner[size_t(c)] != -1) { fail("unowned cell not -1"); break; } }
	ok(unowned > 0, "unowned cells exist");
	double pop = 0;
	for (const Settlement& s : w.settlements) if (!s.dead) pop += s.pop;
	near(fr.pop, pop, 1e-6, "pop of live settlements");
	eq(fr.seaLevel, w.seaLevel);
	eq(fr.year, 50.0);
	// thinning: run frames until it passes 200
	int thinned = 0;
	double lastEvery = *w.vars.snapEvery;
	for (int y = 51; y <= 25 * 260; y++) {
		w.year = y;
		const size_t before = w.snapshots.size();
		chronicle::snapshot(w, false);
		if (w.snapshots.size() < before) {
			thinned++;
			say("year " + std::to_string(y) + ": " + std::to_string(before + 1) + " frames -> " + std::to_string(w.snapshots.size()) +
				", interval " + std::to_string(int(*w.vars.snapEvery)));
			eq(*w.vars.snapEvery, lastEvery * 2, "interval doubled");
			lastEvery = *w.vars.snapEvery;
			eq(w.snapshots.front().heat, 0.4, "the genesis frame is kept");
			eq(w.snapshots.back().year, double(y), "the newest frame is kept");
		}
		ok(int(w.snapshots.size()) <= 200, "never more than 200 frames kept");
		if (int(w.snapshots.size()) > 200) break;
	}
	ok(thinned >= 1, "thinned at least once (" + std::to_string(thinned) + ")");
	ok(std::is_sorted(w.snapshots.begin(), w.snapshots.end(), [](const Snapshot& a, const Snapshot& b) { return a.year < b.year; }), "frames stay in time order");
	ok(w.snapshots.size() >= 90, "about half kept (" + std::to_string(w.snapshots.size()) + ")");
}

// ===================================================================== tale

DM_TEST("chronicle: tale retells the actual history in 4-8 paragraphs without touching the sim stream") {
	Fx f = fixture(41);
	World& w = *f.w;
	drama(f, 5, 2000);
	w.god.deeds["rain"] = 40;
	w.god.deeds["meteor"] = 6;
	w.god.deeds["heal"] = 12;
	w.god.mercy = 3;
	w.god.wrath = 1;
	const uint32_t rs = w.rng.s;
	const uint32_t h0 = dmt::hash(w);
	const chronicle::Tale t = chronicle::tale(w);
	eq(w.rng.s, rs, "rng untouched");
	eq(dmt::hash(w), h0, "world untouched");
	ok(!t.title.empty() && t.title.find(w.name) != std::string::npos, "title names the world: " + t.title);
	ok(t.paragraphs.size() >= 4 && t.paragraphs.size() <= 8, "4-8 paragraphs (" + std::to_string(t.paragraphs.size()) + ")");
	std::string all;
	for (const std::string& p : t.paragraphs) {
		all += p + "\n";
		ok(!p.empty() && p.find("undefined") == std::string::npos && p.find("nan") == std::string::npos && p.find('{') == std::string::npos &&
			p.find('[') == std::string::npos, "clean paragraph: " + p);
	}
	ok(all.find(w.cultures[0].plural) != std::string::npos, "mentions the first people");
	bool town = false;
	for (const Settlement& s : w.settlements) if (all.find(s.name) != std::string::npos) town = true;
	ok(town, "mentions a real place");
	ok(all.find("falling stars") != std::string::npos || all.find("rain") != std::string::npos, "mentions what the god did");
	ok(all.find(w.god.epithet) != std::string::npos, "mentions the god's epithet");
	ok(t.stats.count("peakPop") && t.stats.at("peakPop") > 0, "stats.peakPop");
	ok(t.stats.count("entries") && t.stats.at("entries") == double(w.chronicle.size()), "stats.entries");
	std::printf("  --- tale: %s ---\n", t.title.c_str());
	for (const std::string& p : t.paragraphs) std::printf("  %s\n\n", p.c_str());
	// the same world twice: the same tale
	const chronicle::Tale t2 = chronicle::tale(w);
	ok(t2.title == t.title && t2.paragraphs == t.paragraphs, "deterministic");
	// an empty world and a world with no people still get a tale
	auto e = makeWorld({ .L = 4, .seed = 2 });
	const chronicle::Tale te = chronicle::tale(*e);
	ok(te.paragraphs.size() >= 4 && te.paragraphs.size() <= 8, "empty world: 4-8 paragraphs (" + std::to_string(te.paragraphs.size()) + ")");
	for (const std::string& p : te.paragraphs) std::printf("  [empty] %s\n", p.c_str());
}

// ===================================================================== robustness

DM_TEST("chronicle: bad ids, dead entities and empty worlds never break the prose") {
	Fx f = fixture(51);
	World& w = *f.w;
	// events naming nothing
	w.year = 5;
	w.bus.emit(EvSettlementFounded{ 77, 3, 9, FoundedBy::Settlers });
	w.bus.emit(EvSettlementTier{ -1, 2, 1 });
	w.bus.emit(EvSettlementBuilding{ 500, Building::TEMPLE });
	w.bus.emit(EvSettlementDestroyed{ 3, -1, -1, Cause::War });
	w.bus.emit(EvSettlementCaptured{ 3, 1, 2, CaptureHow::War });
	w.bus.emit(EvWarDeclared{ 4, 5 });
	w.bus.emit(EvWarEnded{ 4, 5, 4 });
	w.bus.emit(EvBattle{ -1, 1, 2, false, 3 });
	w.bus.emit(EvTradeRoute{ 1, 2 });
	w.bus.emit(EvProphetRisen{ 8, 2, "" });
	w.bus.emit(EvEdict{ 8, Edict::Peace });
	w.bus.emit(EvWonderBuilt{ 9, "" });
	w.bus.emit(EvNuke{ -1, 1, 2 });
	w.bus.emit(EvArkLaunched{ 3, 3 });
	w.bus.emit(EvCultureDied{ 4 });
	w.bus.emit(EvCultureAge{ 4, 2, 1 });
	w.bus.emit(EvPrayerAnswered{ 6, PrayerKind::Heal });
	w.bus.emit(EvMiracleWitnessed{ -1, "rain", 0.5, { 4, 5 } });
	w.bus.emit(EvMeteorImpact{ -1, 0.05, ImpactKind::Meteor, false });
	w.bus.emit(EvVolcanoStart{ -1, 1, true });
	w.bus.emit(EvQuake{ -1, 0.1, 1, true });
	w.bus.emit(EvUnitDied{ 1, UnitType::Settler, 3, -1, "lost" });
	w.bus.emit(EvEnding{ EndingType::Silence, Mood::Quiet, -1, -1, 5 });
	w.bus.emit(EvPeopleLost{});
	for (const ChronicleEntry& e : w.chronicle) {
		const std::string why = badText(e.text);
		ok(why.empty(), "[" + e.type + "] " + why + ": " + e.text);
		say("[" + e.type + "] " + e.text);
	}
	// dead and unnamed entities
	Cast k = cast(f);
	w.settlements[size_t(k.towns[1])].name.clear();
	w.cultures[size_t(k.teshki)].plural.clear();
	w.cultures[size_t(k.teshki)].name.clear();
	w.cultures[size_t(k.teshki)].godName.clear();
	w.year = 40;
	Play p(f);
	p.destroy(k.towns[1], Cause::Plague);
	w.bus.emit(EvWarDeclared{ k.teshki, k.umari });
	w.bus.emit(EvSettlementFamine{ k.towns[1] });
	for (const ChronicleEntry& e : w.chronicle) {
		if (e.year < 40) continue;
		const std::string why = badText(e.text);
		ok(why.empty(), "[" + e.type + "] " + why + ": " + e.text);
		say("[" + e.type + "] " + e.text);
	}
	chronicle::updateEpithets(w);
	for (const Culture& c : w.cultures) ok(!c.epithet.empty(), "every living culture gets an epithet");
	ok(!w.cultures[size_t(k.teshki)].godName.empty(), "a culture without a god-name is given one (fork of w.rng)");
	noNaN(w, "after bad events");
	// worlds with no cultures, no settlements, no biome
	auto e = makeWorld({ .L = 3, .seed = 4, .basics = false });
	chronicle::attach(*e);
	chronicle::updateEpithets(*e);
	ok(chronicle::snapshot(*e, true), "snapshot of an empty world");
	e->bus.emit(EvClimateMilestone{ MilestoneKind::Warming, 1 });
	e->bus.emit(EvPhase{ Phase::Water, Phase::Molten });
	ok(e->chronicle.size() == 2, "world-level entries still written (" + std::to_string(e->chronicle.size()) + ")");
	eq(e->god.epithet, std::string(), "no culture, no god epithet");
	noNaN(*e, "empty world");
}

// ===================================================================== sample + maps

DM_TEST("chronicle: sample of a scripted 2,000-year history (entries printed, maps written)") {
	Fx f = fixture(61);
	World& w = *f.w;
	const double t0 = nowMs();
	const DramaStats st = drama(f, 2024, 2000);
	const double ms = nowMs() - t0;
	std::map<std::string, int> byType;
	std::map<int, int> byWeight;
	std::set<std::string> distinct;
	int bad = 0;
	for (const ChronicleEntry& e : w.chronicle) {
		byType[e.type]++;
		byWeight[e.weight]++;
		distinct.insert(e.text);
		const std::string why = badText(e.text);
		if (!why.empty()) { bad++; say("BAD (" + why + "): " + e.text); }
	}
	say(std::to_string(st.years) + " years, " + std::to_string(st.events) + " scripted events -> " + std::to_string(w.chronicle.size()) +
		" entries (" + std::to_string(distinct.size()) + " distinct), weights 1/2/3 = " + std::to_string(byWeight[1]) + "/" +
		std::to_string(byWeight[2]) + "/" + std::to_string(byWeight[3]) + ", " + std::to_string(int(ms)) + " ms");
	std::string types;
	for (const auto& kv : byType) types += kv.first + " " + std::to_string(kv.second) + ", ";
	say("types: " + types);
	eq(bad, 0, "clean prose");
	ok(distinct.size() * 100 >= w.chronicle.size() * 85, "at least 85 % of entries are distinct texts");
	ok(w.chronicle.size() >= 250 && w.chronicle.size() <= 1500, "a readable length (" + std::to_string(w.chronicle.size()) + ")");
	// identical consecutive texts never
	int rep = 0;
	for (size_t i = 1; i < w.chronicle.size(); i++) rep += w.chronicle[i].text == w.chronicle[i - 1].text;
	eq(rep, 0, "no entry repeats the one before it");
	noNaN(w, "after the sample history");
	// the sample itself
	std::printf("  --- the chronicle of %s ---\n", w.name.c_str());
	for (const ChronicleEntry& e : w.chronicle)
		std::printf("  %-10s %s%s\n", chronicle::yearStr(w, e.year).c_str(), e.weight >= 3 ? "** " : e.weight == 2 ? "*  " : "   ", e.text.c_str());
	std::printf("  --- epithets: ");
	for (const Culture& c : w.cultures) std::printf("%s: %s%s; ", c.plural.c_str(), c.epithet.c_str(), c.dead ? " (dead)" : "");
	std::printf("god: %s\n", w.god.epithet.c_str());
	// maps: time-lapse frames (owner over biome, urban white) and the places the chronicle speaks of
	if (!ok(w.snapshots.size() >= 10, "frames taken (" + std::to_string(w.snapshots.size()) + ")")) return;
	const size_t nf = w.snapshots.size();
	const size_t pick[3] = { std::min<size_t>(nf - 1, 8), nf / 2, nf - 1 };
	const char* const label[3] = { "early", "mid", "late" };
	for (int i = 0; i < 3; i++) {
		const Snapshot& fr = w.snapshots[pick[i]];
		const std::string file = shot(std::string("ue-chronicle-frame-") + label[i] + ".png");
		const bool wrote = mi::custom(w, file, [&](int c) {
			const int b = fr.biome[size_t(c)] < Biome::COUNT ? fr.biome[size_t(c)] : 0;
			mi::Rgb col = mi::kBiomeColors[b];
			const int o = fr.owner[size_t(c)];
			if (o >= 0) {
				const auto& cc = w.cultures[size_t(o)].color;
				col = mi::Rgb{ 0.35 * col.r + 0.65 * 255 * cc[0], 0.35 * col.g + 0.65 * 255 * cc[1], 0.35 * col.b + 0.65 * 255 * cc[2] };
			}
			const double u = fr.urban[size_t(c)] / 255.0;
			col = mi::Rgb{ col.r + (255 - col.r) * u, col.g + (255 - col.g) * u, col.b + (255 - col.b) * u };
			return col;
		}, { .rivers = false });
		ok(wrote, "map written: " + file);
		say(file + " (frame of year " + std::to_string(int(fr.year)) + ", pop " + std::to_string(int(fr.pop)) + ")");
	}
	std::vector<int> marks;
	for (const ChronicleEntry& e : w.chronicle) if (e.cell >= 0) marks.push_back(e.cell);
	const std::string file = shot("ue-chronicle-entries.png");
	ok(mi::biome(w, file, { .rivers = false, .marks = marks }), "entries map");
	say(file + " (" + std::to_string(marks.size()) + " entries with a place)");
}
