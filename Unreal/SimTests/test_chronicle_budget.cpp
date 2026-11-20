// DEMIURGE — SimTests/test_chronicle_budget.cpp : the chronicle budget (wave 3, design review finding 8) and
// whose drowning it was (the epithet side of finding 7). Owner: chronicle engineer. Runs with every other module
// stubbed (run.ps1 chronicle) and with the real ones (run.ps1 all).
//
//   - routine notables over the notable bucket become minor; repeats of one kind and people in a decade fold into
//     one digest entry, written by the next decade, its counts in words
//   - story entries are never dropped; wars, peace and destroyed towns only lose weight; once-a-world moments stay
//     epochal; a city's fall is epochal about once a real minute
//   - template rotation: no line of a foldable family is written more than templateCap times; a spent family folds
//   - an answered prayer is notable the first time a people is answered in each age
//   - a town the sea covers is the god's drowning only near a recent Lower Land / quake / meteor stroke (Powers
//     drowns before it emits power:used); otherwise the sea's own rise or the engines', and it never names the god
//     the Drowner; waves count only after the god shook or struck the world
//   - with budget = false the old chronicle comes back; with it the chronicle is deterministic and never draws
//     from the sim stream
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "Chronicle.h"
#include "State.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

void say(const std::string& s) { std::printf("  %s\n", s.c_str()); std::fflush(stdout); }

struct B {
	std::unique_ptr<World> w;
	std::vector<int> land, sea;
	size_t nextLand = 0, nextSea = 0;
};

std::vector<int> spaced(const World& w, bool wantSea, double minSep, int want) {
	std::vector<int> cand;
	for (int c = 0; c < w.n; c++) {
		const bool s = state::isSea(w, c);
		if (s != wantSea) continue;
		if (!wantSea && w.hab[size_t(c)] <= 0.05f) continue;
		cand.push_back(c);
	}
	std::sort(cand.begin(), cand.end());
	std::vector<int> out;
	for (int c : cand) {
		bool okc = true;
		for (int o : out) if (w.geo->angle(o, c) < minSep) { okc = false; break; }
		if (okc) out.push_back(c);
		if (int(out.size()) >= want) break;
	}
	return out;
}

const char* const kNames[] = { "Vornhal", "Tel Asha", "Qe-Luun", "Imbrek", "Sarnath", "Ulu Kesh", "Dorrow", "Mirathe", "Kesh Amun", "Oduya",
	"Brennick", "Tamsin Var", "Ilo", "Haddath", "Zuneri", "Penkarrow", "Asel", "Umbrin", "Garrowmere", "Nish" };

int culture(World& w, const char* name, const char* adj, const char* plural, const char* god) {
	Culture& c = state::newCulture(w, -1);
	c.name = name;
	c.adj = adj;
	c.plural = plural;
	c.godName = god;
	c.lang = "test";
	return c.id;
}

int town(B& v, int cid, double pop, int tier, bool onSea = false) {
	World& w = *v.w;
	const int cell = onSea ? v.sea[v.nextSea++ % v.sea.size()] : v.land[v.nextLand++ % v.land.size()];
	Settlement& s = state::newSettlement(w, cell, cid);
	s.name = kNames[s.id % 20];
	s.pop = pop;
	s.tier = tier;
	const int id = s.id;
	if (Culture* c = state::culture(w, cid)) if (c->capital < 0) { c->capital = id; w.settlements[size_t(id)].capital = true; }
	return id;
}

B fresh(uint32_t seed) {
	B v;
	v.w = makeWorld({ .L = 5, .seed = seed, .land = 0.4 });
	v.land = spaced(*v.w, false, 0.08, 60);
	v.sea = spaced(*v.w, true, 0.08, 60);
	v.w->tune<ChronicleTune>().maxEntries = 100000;
	v.w->phase = Phase::History;
	v.w->year = 100;
	chronicle::attach(*v.w);
	return v;
}

int countType(const World& w, const std::string& type, int weight = 0, int firstId = 0) {
	int n = 0;
	for (const ChronicleEntry& e : w.chronicle) n += e.id >= firstId && e.type == type && (weight == 0 || e.weight == weight);
	return n;
}
const ChronicleEntry* lastOf(const World& w, const std::string& type) {
	for (size_t i = w.chronicle.size(); i-- > 0;) if (w.chronicle[i].type == type) return &w.chronicle[i];
	return nullptr;
}
bool has(const std::string& t, const char* s) { return t.find(s) != std::string::npos; }
bool hasAny(const std::string& t, std::initializer_list<const char*> v) {
	for (const char* s : v) if (has(t, s)) return true;
	return false;
}
double num(const World& w, const std::string& k) {
	const auto it = w.vars.num.find(k);
	return it == w.vars.num.end() ? 0.0 : it->second;
}
void tick(World& w, double years) { w.year += years; w.tick += int64_t(std::lround(years * 4)) + 1; }

// prose faults the voice tests look for: template syntax, digits other than the year's ordinal, spacing
std::string fault(const ChronicleEntry& e) {
	const std::string& t = e.text;
	if (t.empty()) return "empty";
	if (!std::isupper(static_cast<unsigned char>(t[0])) && t[0] != '"') return "no capital";
	if (t.back() != '.' && t.back() != '!' && t.back() != '?' && t.back() != '"') return "no final stop";
	for (char ch : t) if (ch == '{' || ch == '}' || ch == '[' || ch == ']' || ch == '|' || ch == '_') return "template syntax";
	std::string rest = t;
	const long long y = static_cast<long long>(std::floor(e.year));
	for (const char* sfx : { "st year", "nd year", "rd year", "th year" }) {
		const std::string ord = std::to_string(y) + sfx;
		for (size_t p; (p = rest.find(ord)) != std::string::npos;) rest.erase(p, ord.size());
	}
	for (char ch : rest) if (std::isdigit(static_cast<unsigned char>(ch))) return "digit";
	if (t.find("  ") != std::string::npos || t.find(" ,") != std::string::npos || t.find(" .") != std::string::npos) return "spacing";
	return "";
}

const char* const kDrowner[] = { "the Drowner", "the Sea's Elbow", "the Deep" };
bool drownerName(const std::string& e) { for (const char* n : kDrowner) if (e == n) return true; return false; }

} // namespace

DM_TEST("chronicle budget: routine notables over budget become minor; repeats fold into one digest per decade") {
	B v = fresh(71);
	World& w = *v.w;
	const ChronicleTune& T = w.tune<ChronicleTune>();
	const int um = culture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int va = culture(w, "Vael", "Vaelish", "the Vaeli", "Orro-Ka");
	std::vector<int> towns;
	for (int k = 0; k < 14; k++) towns.push_back(town(v, um, 600, 2));
	town(v, va, 900, 3);
	// twelve towns go over to the Vaeli in the war, in five years: captures are routine notables
	const int first = w.nextId.entry;
	for (int k = 0; k < 12; k++) {
		const int sid = towns[size_t(k)];
		w.settlements[size_t(sid)].culture = va;
		w.bus.emit(EvSettlementCaptured{ sid, um, va, CaptureHow::War });
		tick(w, 0.4);
	}
	const int n2 = countType(w, "captured", 2, first), n1 = countType(w, "captured", 1, first);
	say("12 captures in 5 years: " + std::to_string(n2) + " notable, " + std::to_string(n1) + " minor");
	eq(n2, int(T.notableBurst), "notable while the notable bucket lasts (a full bucket)");
	eq(n1, 1, "then one minor entry: the first of its kind for the Vaeli this decade");
	eq(num(w, "chronicle_dg." + std::to_string(va) + ".take"), double(12 - n2 - n1), "the rest are folded into the decade's ledger");
	eq(countType(w, "digest"), 0, "no digest before the decade is over");
	// the next decade: the yearly updateEpithets writes the digest
	w.year = 111;
	w.tick += 40;
	chronicle::updateEpithets(w);
	const ChronicleEntry* d = lastOf(w, "digest");
	if (!ok(d != nullptr, "the decade's digest was written")) return;
	say("digest: " + d->text);
	eq(d->weight, 1, "a digest is a minor entry");
	eq(d->culture, va, "about the Vaeli");
	ok(has(d->text, "eight towns went over to the Vaeli"), "the count in words and the people: " + d->text);
	eq(fault(*d), std::string(), "the digest reads as prose");
	eq(num(w, "chronicle_dg." + std::to_string(va) + ".take"), 0.0, "the ledger is cleared");
	// a later decade with no folds writes no digest
	w.year = 125;
	w.tick += 60;
	chronicle::updateEpithets(w);
	eq(countType(w, "digest"), 1, "one digest only");
}

DM_TEST("chronicle budget: story is never dropped; wars and fallen cities lose weight; once-a-world moments stay epochal") {
	B v = fresh(72);
	World& w = *v.w;
	const ChronicleTune& T = w.tune<ChronicleTune>();
	const int um = culture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int te = culture(w, "Teshk", "Teshkan", "the Teshki", "Orro-Ka");
	town(v, um, 2000, 3);
	town(v, te, 1500, 3);
	// twenty wars declared in two years: every one written, notable only while the story bucket lasts
	int first = w.nextId.entry;
	for (int k = 0; k < 20; k++) { w.bus.emit(EvWarDeclared{ k % 2 ? um : te, k % 2 ? te : um }); tick(w, 0.1); }
	eq(countType(w, "war", 0, first), 20, "wars are never dropped");
	eq(countType(w, "war", 2, first), int(T.storyBurst), "notable while the story bucket lasts");
	// eight cities struck down by meteors in one decade (a fresh world: full buckets)
	B v2 = fresh(73);
	World& w2 = *v2.w;
	const int a = culture(w2, "Umar", "Umaran", "the Umari", "Ashvel");
	std::vector<int> cities;
	for (int k = 0; k < 8; k++) cities.push_back(town(v2, a, 3000, 3));
	town(v2, a, 500, 2);
	first = w2.nextId.entry;
	for (int sid : cities) {
		w2.settlements[size_t(sid)].dead = true;
		w2.bus.emit(EvSettlementDestroyed{ sid, w2.settlements[size_t(sid)].cell, a, Cause::Meteor });
		tick(w2, 1);
	}
	const int d3 = countType(w2, "destroyed", 3, first), d2 = countType(w2, "destroyed", 2, first), d1 = countType(w2, "destroyed", 1, first);
	say("8 cities destroyed in 8 years: weight 3/2/1 = " + std::to_string(d3) + "/" + std::to_string(d2) + "/" + std::to_string(d1));
	eq(d3 + d2 + d1, 8, "every fallen city is written");
	eq(d3, int(T.epochalBurst), "epochal while the epochal bucket lasts");
	eq(d2, int(T.storyBurst), "then notable while the story bucket lasts");
	// the epochal bucket is empty: a wonder is still epochal; (verify) a new people is told, as notable
	first = w2.nextId.entry;
	w2.bus.emit(EvWonderBuilt{ cities.back() + 1, "the Hanging Stair" });
	const int born = state::newCulture(w2, a).id;
	w2.cultures[size_t(born)].name = "Vael";
	w2.cultures[size_t(born)].plural = "the Vaeli";
	w2.cultures[size_t(born)].adj = "Vaelish";
	w2.bus.emit(EvCultureBorn{ born, a, v2.land[30], BirthReason::Distance });
	eq(countType(w2, "wonder", 3, first), 1, "a wonder stays epochal");
	eq(countType(w2, "culture-born", 0, first), 1, "a new people is always written");
	eq(countType(w2, "culture-born", 2, first), 1, "over the epochal bucket a new people is notable, never minor");
	// later the epochal bucket has refilled (one token per 1 / epochalPerDecade decades)
	tick(w2, 10.0 / T.epochalPerDecade + 10);
	first = w2.nextId.entry;
	const int late = town(v2, a, 2500, 3);
	w2.settlements[size_t(late)].dead = true;
	w2.bus.emit(EvSettlementDestroyed{ late, w2.settlements[size_t(late)].cell, a, Cause::Meteor });
	eq(countType(w2, "destroyed", 3, first), 1, "later a fallen city is epochal again");
}

DM_TEST("chronicle budget: no line of a foldable family more than templateCap times; a spent family folds") {
	B v = fresh(74);
	World& w = *v.w;
	const ChronicleTune& T = w.tune<ChronicleTune>();
	const int um = culture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int te = culture(w, "Teshk", "Teshkan", "the Teshki", "Orro-Ka");
	town(v, um, 3000, 3);
	town(v, te, 2000, 3);
	// one abandoned village every 11 years for 1,100 years (each in a decade of its own, past every gap)
	const int first = w.nextId.entry;
	int events = 0;
	for (int k = 0; k < 100; k++) {
		const int sid = town(v, um, 30, 1);
		w.settlements[size_t(sid)].dead = true;
		w.bus.emit(EvSettlementDestroyed{ sid, w.settlements[size_t(sid)].cell, um, Cause::Abandoned });
		events++;
		tick(w, 11);
	}
	const int told = countType(w, "destroyed", 0, first);
	// the five abandonment lines, by a fragment of each
	const char* const frags[] = { "out of habit", "was abandoned", "grass grew in its streets", " gave up ", "was left empty" };
	int most = 0;
	for (const char* f : frags) {
		int n = 0;
		for (const ChronicleEntry& e : w.chronicle) if (e.id >= first && e.type == "destroyed" && has(e.text, f)) n++;
		say(std::string("'") + f + "': " + std::to_string(n));
		most = std::max(most, n);
	}
	say(std::to_string(events) + " abandonments, " + std::to_string(told) + " told");
	ok(most <= T.templateCap, "no line more than templateCap (" + std::to_string(T.templateCap) + ") times: " + std::to_string(most));
	ok(told <= 5 * T.templateCap && told >= 5 * T.templateCap - 2, "the family is told until every line is spent, then folds (" + std::to_string(told) + ")");
	// story families keep rotating past the cap (least used first)
	std::map<std::string, int> warTexts;
	for (int k = 0; k < 60; k++) {
		const int f0 = w.nextId.entry;
		w.bus.emit(EvWarDeclared{ k % 2 ? um : te, k % 2 ? te : um });
		for (const ChronicleEntry& e : w.chronicle) if (e.id >= f0 && e.type == "war") warTexts[e.text]++;
		tick(w, 11);
	}
	int warN = 0;
	for (const auto& kv : warTexts) warN += kv.second;
	eq(warN, 60, "wars past the cap are still told");
}

DM_TEST("chronicle budget: an answered prayer is notable the first time a people is answered in each age") {
	B v = fresh(75);
	World& w = *v.w;
	const int um = culture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int t = town(v, um, 800, 2);
	w.cultures[size_t(um)].age = 1;
	std::vector<int> weights;
	for (int k = 0; k < 4; k++) {
		if (k == 2) w.cultures[size_t(um)].age = 2;
		const int f0 = w.nextId.entry;
		w.bus.emit(EvPrayerAnswered{ t, PrayerKind::Rain });
		int wt = 0;
		for (const ChronicleEntry& e : w.chronicle) if (e.id >= f0 && e.type == "prayer-answered") wt = e.weight;
		weights.push_back(wt);
		tick(w, 25);
	}
	say("answers in ages 1, 1, 2, 2: weights " + std::to_string(weights[0]) + " " + std::to_string(weights[1]) + " " + std::to_string(weights[2]) + " " + std::to_string(weights[3]));
	eq(weights[0], 2, "the first answer of an age is notable");
	eq(weights[1], 1, "the next in the same age is minor");
	eq(weights[2], 2, "a new age: notable again");
	eq(weights[3], 1, "and minor after that");
}

DM_TEST("chronicle budget: the sea covers a town: the god's drowning only near a Lower Land stroke, else the sea's or the engines'") {
	B v = fresh(76);
	World& w = *v.w;
	const ChronicleTune& T = w.tune<ChronicleTune>();
	const int um = culture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int br = culture(w, "Brann", "Brannish", "the Branni", "Kell");
	town(v, um, 3000, 3);
	const int factoryTown = town(v, br, 4000, 3);
	const std::string dKey = "chronicle_d." + std::to_string(um);
	auto drown = [&](int sid) {
		w.settlements[size_t(sid)].dead = true;
		w.bus.emit(EvSettlementDestroyed{ sid, w.settlements[size_t(sid)].cell, um, Cause::Drowned });
	};
	// 1. the sea rose (no god's hand anywhere): told once the tick is over, as the sea's doing, not remembered
	int sid = town(v, um, 400, 2, true);
	int f0 = w.nextId.entry;
	drown(sid);
	eq(countType(w, "destroyed", 0, f0), 0, "held until the tick is over (Powers emits power:used after the drowning)");
	tick(w, 0.25);
	chronicle::updateEpithets(w);
	const ChronicleEntry* e = lastOf(w, "destroyed");
	if (!ok(e != nullptr && e->id >= f0, "the drowning is told")) return;
	say("sea rose: " + e->text);
	ok(hasAny(e->text, { "hand's width", "uphill", "given back to the sea" }), "the sea's own rise: " + e->text);
	ok(!hasAny(e->text, { "lowered", "pressed", "the touch of the god", "leaned on the land" }), "not the god's hand: " + e->text);
	eq(num(w, dKey), 0.0, "the people do not remember it as the god's drowning");
	// 2. Powers: Lower Land drowns the town, then emits power:used, in the same tick
	sid = town(v, um, 400, 2, true);
	f0 = w.nextId.entry;
	drown(sid);
	w.bus.emit(EvPowerUsed{ "lower", w.settlements[size_t(sid)].cell, 0.08, true });
	tick(w, 0.25);
	chronicle::updateEpithets(w);
	e = lastOf(w, "destroyed");
	if (!ok(e != nullptr && e->id >= f0, "the lowered town is told")) return;
	say("lowered: " + e->text);
	ok(hasAny(e->text, { "lowered", "pressed", "the touch of the god", "leaned on the land" }), "the god's hand: " + e->text);
	ok(!hasAny(e->text, { "hand's width a year", "engines", "chimneys" }), "no sea rise for a god's drowning: " + e->text);
	eq(num(w, dKey), 1.0, "remembered as the god's drowning");
	// 3. years later, with the Branni burning coal: the engines' sea, the burners named
	tick(w, T.strokeYears + 5);
	chronicle::updateEpithets(w);
	w.co2 = 150;
	w.settlements[size_t(factoryTown)].b[Building::FACTORY] = 1;
	w.settlements[size_t(factoryTown)].b[Building::PLANT] = 1;
	sid = town(v, um, 400, 2, true);
	f0 = w.nextId.entry;
	drown(sid);
	tick(w, 0.25);
	chronicle::updateEpithets(w);
	e = lastOf(w, "destroyed");
	if (!ok(e != nullptr && e->id >= f0, "the engines' drowning is told")) return;
	say("engines: " + e->text);
	ok(hasAny(e->text, { "Branni", "Brannish" }), "the burners are named: " + e->text);
	ok(hasAny(e->text, { "engines", "chimneys", "coal" }), "the engines are blamed: " + e->text);
	eq(num(w, dKey), 1.0, "still only the one god's drowning remembered");
	eq(fault(*e), std::string(), "prose");
	// the sea-rise milestone names the engines too
	f0 = w.nextId.entry;
	w.bus.emit(EvClimateMilestone{ MilestoneKind::SeaRise, 0.2 });
	e = lastOf(w, "climate");
	if (ok(e != nullptr && e->id >= f0, "sea-rise milestone told")) {
		say("milestone: " + e->text);
		ok(has(e->text, "the Branni") && hasAny(e->text, { "chimneys", "engines" }), "the milestone names the burners: " + e->text);
	}
	// 4. waves: only after the god shook the world
	const int land = town(v, um, 800, 2);
	const double before = num(w, dKey);
	w.bus.emit(EvTsunamiHit{ w.settlements[size_t(land)].cell, 1.4 });
	eq(num(w, dKey), before, "a wave from nowhere is the sea's");
	w.bus.emit(EvQuake{ v.land[40], 0.08, 1.0, false });
	tick(w, 1);
	w.bus.emit(EvTsunamiHit{ w.settlements[size_t(land)].cell, 1.4 });
	ok(num(w, dKey) > before, "a wave after the god's quake is the god's");
}

DM_TEST("chronicle budget: towns lost to the rising sea do not name the god the Drowner; lowered towns do") {
	auto career = [](uint32_t seed, bool lowered) {
		B v = fresh(seed);
		World& w = *v.w;
		const int um = culture(w, "Umar", "Umaran", "the Umari", "Ashvel");
		town(v, um, 3000, 3);
		chronicle::updateEpithets(w);
		std::vector<std::string> names;
		for (int y = 0; y < 400; y++) {
			if (y > 40 && y % 20 == 0) {
				const int sid = town(v, um, 300, 1, true);
				w.settlements[size_t(sid)].dead = true;
				w.bus.emit(EvSettlementDestroyed{ sid, w.settlements[size_t(sid)].cell, um, Cause::Drowned });
				if (lowered) w.bus.emit(EvPowerUsed{ "lower", w.settlements[size_t(sid)].cell, 0.08, true });
			}
			tick(w, 1);
			chronicle::updateEpithets(w);
			const std::string ep = w.cultures[size_t(um)].epithet;
			if (names.empty() || names.back() != ep) names.push_back(ep);
		}
		return names;
	};
	const std::vector<std::string> sea = career(81, false), god = career(81, true);
	std::string a, b;
	for (const std::string& n : sea) a += n + " / ";
	for (const std::string& n : god) b += n + " / ";
	say("risen sea: " + a);
	say("lowered land: " + b);
	bool seaDrowner = false;
	for (const std::string& n : sea) seaDrowner |= drownerName(n);
	ok(!seaDrowner, "nineteen towns lost to the rising sea never name the god the Drowner");
	ok(drownerName(god.back()), "nineteen towns the god lowered into the sea do: " + god.back());
}

DM_TEST("chronicle budget: deterministic, never touches the sim stream; budget = false brings the old chronicle back") {
	auto drama = [](bool budget, uint32_t seed) {
		B v = fresh(seed);
		World& w = *v.w;
		w.tune<ChronicleTune>().budget = budget;
		const int um = culture(w, "Umar", "Umaran", "the Umari", "Ashvel");
		const int te = culture(w, "Teshk", "Teshkan", "the Teshki", "Orro-Ka");
		std::vector<int> towns;
		for (int k = 0; k < 24; k++) towns.push_back(town(v, k % 3 ? um : te, 300 + 100 * (k % 5), 1 + k % 3));
		Rng dr(seed * 7 + 1);
		const uint32_t s0 = w.rng.s;
		for (int y = 0; y < 600; y++) {
			for (int k = 0; k < 6; k++) {
				const int sid = towns[size_t(dr.pickIndex(int(towns.size())))];
				const int cid = w.settlements[size_t(sid)].culture;
				switch (dr.pickIndex(8)) {
				case 0: w.bus.emit(EvSettlementFamine{ sid }); break;
				case 1: w.bus.emit(EvPrayerAnswered{ sid, PrayerKind::Rain }); break;
				case 2: w.bus.emit(EvPrayer{ sid, PrayerKind::Rain, true }); break;
				case 3: w.bus.emit(EvTradeRoute{ sid, towns[size_t(dr.pickIndex(int(towns.size())))] }); break;
				case 4: w.bus.emit(EvSettlementTier{ sid, 2, 1 }); break;
				case 5: w.bus.emit(EvFireStart{ w.settlements[size_t(sid)].cell }); break;
				case 6: if (dr.chance(0.1)) w.bus.emit(EvWarDeclared{ cid, cid == um ? te : um }); break;
				default: w.bus.emit(EvSettlementBuilding{ sid, Building::Id(Building::GRANARY + dr.pickIndex(4)) }); break;
				}
			}
			tick(w, 1);
			chronicle::updateEpithets(w);
		}
		struct Out { std::vector<ChronicleEntry> c; uint32_t s0, s1; uint32_t hash; };
		return Out{ w.chronicle, s0, w.rng.s, dmt::hash(w) };
	};
	const auto a = drama(true, 5), b = drama(true, 5), off = drama(false, 5);
	ok(a.c == b.c, "the same history twice: the same chronicle, word for word");
	eq(a.hash, b.hash, "same hash");
	eq(a.s1, a.s0, "the sim stream is never drawn from");
	int digests = 0, faults = 0;
	for (const ChronicleEntry& e : a.c) {
		digests += e.type == "digest";
		const std::string f = fault(e);
		if (!f.empty()) { if (faults < 6) say(f + ": " + e.text); faults++; }
	}
	say("600 busy years: budget on " + std::to_string(a.c.size()) + " entries (" + std::to_string(digests) + " digests), off " + std::to_string(off.c.size()));
	eq(faults, 0, "every budgeted entry reads as prose");
	ok(digests > 0, "repeats were folded into digests");
	ok(a.c.size() * 10 < off.c.size() * 8, "the budget writes at least a fifth fewer entries");
	int offDigests = 0;
	for (const ChronicleEntry& e : off.c) offDigests += e.type == "digest";
	eq(offDigests, 0, "budget = false: no digests");
}

// ---------------------------------------------------------------- independent verification (wave 3 verify)

DM_TEST("chronicle budget (verify): a kind god's busy centuries: one digest in digestEvery years, rotated, the god named once") {
	B v = fresh(81);
	World& w = *v.w;
	const ChronicleTune& T = w.tune<ChronicleTune>();
	const int um = culture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int va = culture(w, "Vael", "Vaelish", "the Vaeli", "Ashvel");
	std::vector<int> towns;
	for (int k = 0; k < 20; k++) towns.push_back(town(v, k % 3 ? um : va, 400 + 50 * (k % 7), 1 + k % 3));
	Rng dr(4242);
	const int first = w.nextId.entry;
	// six hundred years of a kind god answering a town or two every year, while the peoples build and grow
	for (int y = 0; y < 600; y++) {
		for (int k = 0; k < 3; k++) {
			const int sid = towns[size_t(dr.pickIndex(int(towns.size())))];
			switch (dr.pickIndex(4)) {
			case 0: case 1: w.bus.emit(EvPrayerAnswered{ sid, dr.chance(0.5) ? PrayerKind::Rain : PrayerKind::Bloom }); break;
			case 2: w.bus.emit(EvSettlementBuilding{ sid, Building::Id(Building::GRANARY + dr.pickIndex(4)) }); break;
			default: w.bus.emit(EvSettlementTier{ sid, 2, 1 }); break;
			}
		}
		tick(w, 1);
		chronicle::updateEpithets(w);
	}
	std::vector<const ChronicleEntry*> dg;
	for (const ChronicleEntry& e : w.chronicle) if (e.id >= first && e.type == "digest") dg.push_back(&e);
	say(std::to_string(dg.size()) + " digests in 600 years");
	ok(dg.size() >= 8, "the folds were told");
	ok(double(dg.size()) <= 600.0 / T.digestEvery + 1, "at most one digest in digestEvery years");
	double minGap = 1e9;
	for (size_t i = 1; i < dg.size(); i++) minGap = std::min(minGap, dg[i]->year - dg[i - 1]->year);
	ok(minGap >= T.digestEvery - 1e-9, "consecutive digests at least digestEvery apart (closest " + std::to_string(int(minGap)) + " years)");
	std::map<std::string, int> openers;
	int faults = 0, twice = 0, decade = 0;
	for (const ChronicleEntry* e : dg) {
		const std::string f = fault(*e);
		if (!f.empty()) { if (faults < 4) say(f + ": " + e->text); faults++; }
		size_t p = e->text.find("Ashvel");
		if (p != std::string::npos && e->text.find("Ashvel", p + 1) != std::string::npos) { if (twice < 3) say("god named twice: " + e->text); twice++; }
		decade += has(e->text, "decade") || has(e->text, "ten years past");
		std::string op;
		int words = 0;
		for (char ch : e->text) { if (ch == ' ' && ++words == 3) break; op.push_back(ch); }
		openers[op]++;
	}
	int worst = 0;
	std::string worstOp;
	for (auto& kv : openers) if (kv.second > worst) { worst = kv.second; worstOp = kv.first; }
	say("most used opening: '" + worstOp + "' x" + std::to_string(worst) + "; e.g. " + (dg.empty() ? std::string() : dg.back()->text));
	eq(faults, 0, "every digest reads as prose");
	eq(twice, 0, "a digest never names the same god twice");
	eq(decade, 0, "a digest spanning decades never calls them 'that decade'");
	ok(worst * 4 <= int(dg.size()) + 4, "the frames rotate: no opening in more than about a quarter of the digests");
}

DM_TEST("chronicle budget (verify): a natural eruption loses weight over the story bucket, the god's does not; later Arks are notable") {
	B v = fresh(82);
	World& w = *v.w;
	const int um = culture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int te = culture(w, "Teshk", "Teshkan", "the Teshki", "Orro-Ka");
	const int cap = town(v, um, 3000, 3);
	town(v, te, 1500, 3);
	// empty the story bucket with wars
	for (int k = 0; k < 8; k++) { w.bus.emit(EvWarDeclared{ um, te }); tick(w, 0.1); }
	const int first = w.nextId.entry;
	const int cell = w.settlements[size_t(cap)].cell;
	w.bus.emit(EvVolcanoStart{ cell, 1, true });
	tick(w, 0.1);
	w.bus.emit(EvVolcanoStart{ cell, 2, false });
	const ChronicleEntry* nat = nullptr;
	const ChronicleEntry* god = nullptr;
	for (const ChronicleEntry& e : w.chronicle) if (e.id >= first && e.type == "volcano") (nat ? god : nat) = &e;
	if (!ok(nat && god, "both eruptions were written")) return;
	eq(nat->weight, 1, "the mountain's own fire, over the story bucket, is minor");
	eq(god->weight, 2, "the god's eruption is story: notable");
	// the Arks: the world's first is epochal, another people's after it notable
	const int a0 = w.nextId.entry;
	w.bus.emit(EvArkStarted{ um, cap });
	tick(w, 3);
	w.bus.emit(EvArkStarted{ te, -1 });
	eq(countType(w, "ark", 3, a0), 1, "the first Ark begun is epochal");
	eq(countType(w, "ark", 2, a0), 1, "a second people's Ark is notable");
	// with budget = false both Arks stay epochal
	B v2 = fresh(83);
	World& w2 = *v2.w;
	w2.tune<ChronicleTune>().budget = false;
	const int u2 = culture(w2, "Umar", "Umaran", "the Umari", "Ashvel");
	const int c2 = town(v2, u2, 3000, 3);
	w2.bus.emit(EvArkStarted{ u2, c2 });
	tick(w2, 3);
	w2.bus.emit(EvArkStarted{ u2, c2 });
	eq(countType(w2, "ark", 3), 2, "budget = false: every Ark begun is epochal, as before");
}
