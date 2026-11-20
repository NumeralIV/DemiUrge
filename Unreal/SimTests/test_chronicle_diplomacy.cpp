// DEMIURGE — SimTests/test_chronicle_diplomacy.cpp : track chronicle-diplomacy (sim-findings-2 "for others"):
// the chronicle's lines for alliances made, ended and betrayed, allies answering a call to arms, a war the
// people blamed on their god ("the god struck us" apart from "the god favoured them"), a victory the god was
// thanked for, and the Omen of Peace that calms neighbours before a war. No god in a war nobody blamed on it.
// Owner: chronicle engineer. Runs with every other module stubbed (run.ps1 chronicle) and with the real
// ones (run.ps1 all).
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "Chronicle.h"
#include "Society.h"
#include "State.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

void say(const std::string& s) { std::printf("  %s\n", s.c_str()); std::fflush(stdout); }

struct R {
	std::unique_ptr<World> w;
	std::vector<int> sites;
	size_t next = 0;
};

std::vector<int> landSites(const World& w, double minSep, int want) {
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

const char* const kNames[] = { "Vornhal", "Tel Asha", "Qe-Luun", "Imbrek", "Sarnath", "Ulu Kesh", "Dorrow", "Mirathe" };

int culture(World& w, const char* name, const char* adj, const char* plural, const char* god) {
	Culture& c = state::newCulture(w, -1);
	c.name = name;
	c.adj = adj;
	c.plural = plural;
	c.godName = god;
	c.lang = "test";
	return c.id;
}

int town(R& v, int cid, double pop, int tier) {
	World& w = *v.w;
	const int cell = v.sites[v.next++ % v.sites.size()];
	Settlement& s = state::newSettlement(w, cell, cid);
	s.name = kNames[s.id % 8];
	s.pop = pop;
	s.tier = tier;
	const int id = s.id;
	if (Culture* c = state::culture(w, cid)) if (c->capital < 0) { c->capital = id; w.settlements[size_t(id)].capital = true; }
	return id;
}

R fresh(uint32_t seed) {
	R v;
	v.w = makeWorld({ .L = 5, .seed = seed, .land = 0.4 });
	v.sites = landSites(*v.w, 0.10, 20);
	v.w->tune<ChronicleTune>().maxEntries = 100000;
	v.w->phase = Phase::History;
	chronicle::attach(*v.w);
	// the stubbed Diplomacy has no oaths: read them from vars.num as the real one does
	if (stub::stubbed("diplomacy")) {
		stub::hooks().society_swornUntil = [](const World& w, int a, int b) -> double {
			const auto it = w.vars.num.find("diplomacy_sworn" + std::to_string(std::min(a, b)) + "_" + std::to_string(std::max(a, b)));
			return it != w.vars.num.end() && it->second > w.year ? it->second : -1.0;
		};
		stub::hooks().society_atWar = [](const World& w, int a, int b) -> bool {
			const auto it = w.vars.rel.find(state::relKey(a, b));
			return it != w.vars.rel.end() && it->second.war;
		};
	}
	return v;
}

void swear(World& w, int a, int b) {
	w.vars.num["diplomacy_sworn" + std::to_string(std::min(a, b)) + "_" + std::to_string(std::max(a, b))] = w.year + w.tune<DiplomacyTune>().omenYears;
}

int countType(const World& w, const std::string& type) {
	int n = 0;
	for (const ChronicleEntry& e : w.chronicle) n += e.type == type;
	return n;
}
const ChronicleEntry* lastOf(const World& w, const std::string& type) {
	for (size_t i = w.chronicle.size(); i-- > 0;) if (w.chronicle[i].type == type) return &w.chronicle[i];
	return nullptr;
}
bool has(const std::string& t, const char* s) { return t.find(s) != std::string::npos; }
bool hasAny(const std::string& t, std::initializer_list<const char*> l) {
	for (const char* s : l) if (has(t, s)) return true;
	return false;
}
// the god, by any of its names in these tests
// the god, by any of its names: "god", the peoples' names for it, their epithets and the world's
bool namesGod(const World& w, const std::string& t) {
	if (hasAny(t, { "god", "Ashvel", "Orro-Ka", "Imi", "the Maker" })) return true;
	auto named = [&](const std::string& ep) {
		if (ep.empty()) return false;
		if (has(t, ep.c_str())) return true;
		std::string cap = ep;
		cap[0] = char(std::toupper(static_cast<unsigned char>(cap[0])));
		return has(t, cap.c_str());
	};
	if (named(w.god.epithet)) return true;
	for (const Culture& c : w.cultures) if (named(c.epithet)) return true;
	return false;
}
bool clean(const std::string& t) { return t.find('{') == std::string::npos && t.find('[') == std::string::npos && t.find("  ") == std::string::npos; }

struct Three {
	R v;
	int um = -1, te = -1, va = -1, a = -1, b = -1, c = -1;
};
Three three(uint32_t seed) {
	Three t{ fresh(seed) };
	World& w = *t.v.w;
	t.um = culture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	t.te = culture(w, "Teshk", "Teshkan", "the Teshki", "Orro-Ka");
	t.va = culture(w, "Vael", "Vaelish", "the Vaeli", "Imi");
	t.a = town(t.v, t.um, 900, 2);
	t.b = town(t.v, t.te, 900, 2);
	t.c = town(t.v, t.va, 900, 2);
	return t;
}

} // namespace

DM_TEST("chronicle diplomacy: alliances made and ended, allies who answer a call to arms, allies who turn on each other") {
	stub::Guard guard;   // the hooks fresh() sets never leak into the next test
	Three t = three(61);
	World& w = *t.v.w;
	w.year = 300; w.tick = 1200;
	w.bus.emit(EvAlliance{ t.um, t.te, true });
	if (eq(countType(w, "alliance"), 1, "a new alliance is told")) {
		const ChronicleEntry* e = lastOf(w, "alliance");
		say(e->text);
		ok(has(e->text, "Umari") && has(e->text, "Teshki") && clean(e->text), "it names both peoples: " + e->text);
		ok(e->weight >= 1, "written");
	}
	// it ends in peace: held to the year's close, then told (minor)
	w.year = 340; w.tick = 1360;
	w.bus.emit(EvAlliance{ t.um, t.te, false });
	eq(countType(w, "alliance-end"), 0, "held back while the tick runs");
	chronicle::updateEpithets(w);
	if (eq(countType(w, "alliance-end"), 1, "an ended alliance is told at the year's close")) {
		const ChronicleEntry* e = lastOf(w, "alliance-end");
		say(e->text);
		ok(has(e->text, "Umari") && has(e->text, "Teshki") && e->weight == 1 && clean(e->text), "minor, naming both: " + e->text);
	}
	// a pair that makes up again within allianceGap is not told twice
	w.year = 350; w.tick = 1400;
	w.bus.emit(EvAlliance{ t.um, t.te, true });
	eq(countType(w, "alliance"), 1, "the same peoples allying again soon: not told again");
	// allies who come to blows: the break comes just before the war, in one tick, and is told as a betrayal
	w.year = 420; w.tick = 1680;
	w.bus.emit(EvAlliance{ t.um, t.te, false });
	w.vars.rel[state::relKey(t.um, t.te)].war = true;
	w.bus.emit(EvWarDeclared{ t.um, t.te });
	chronicle::updateEpithets(w);
	eq(countType(w, "alliance-end"), 1, "no 'the alliance lapsed' for a betrayal");
	if (const ChronicleEntry* e = lastOf(w, "war")) {
		say(e->text);
		ok(hasAny(e->text, { "allies", "alliance", "oaths" }) && has(e->text, "Umari") && has(e->text, "Teshki"), "a betrayal: " + e->text);
		ok(!namesGod(w, e->text), "no god in a betrayal nobody blamed on it: " + e->text);
	}
	// an ally answers the call: the Vaeli join the war of the Teshki (whom the Umari attacked)
	w.year = 421; w.tick = 1684;
	w.vars.rel[state::relKey(t.va, t.um)].war = true;
	w.bus.emit(EvWarDeclared{ t.va, t.um, t.te });
	if (const ChronicleEntry* e = lastOf(w, "war")) {
		say(e->text);
		ok(has(e->text, "Vaeli") && has(e->text, "Umari") && has(e->text, "Teshki") && clean(e->text), "the ally, the enemy and the friend: " + e->text);
	}
	// betrayed and blamed: the suffix names the god
	w.year = 600; w.tick = 2400;
	w.vars.rel[state::relKey(t.um, t.te)].war = false;
	w.bus.emit(EvAlliance{ t.um, t.te, false });
	w.vars.rel[state::relKey(t.um, t.te)].war = true;
	w.bus.emit(EvWarDeclared{ t.um, t.te, -1, true, true });
	if (const ChronicleEntry* e = lastOf(w, "war")) {
		say(e->text);
		ok(namesGod(w, e->text) && hasAny(e->text, { "allies", "alliance", "oaths" }), "a betrayal blamed on the god's favour: " + e->text);
	}
	noNaN(w, "alliances");
}

DM_TEST("chronicle diplomacy: blamed wars tell 'the god struck us' from 'the god favoured them'; unblamed wars never name the god") {
	stub::Guard guard;   // the hooks fresh() sets never leak into the next test
	Three t = three(62);
	World& w = *t.v.w;
	const char* const envyWords[] = { "smiled", "gifts", "jealous", "Envy", "Why them", "loved", "love better", "bless" };
	auto envious = [&](const std::string& s) { for (const char* k : envyWords) if (has(s, k)) return true; return false; };
	int plain = 0, struck = 0, envied = 0, plainGod = 0, struckGod = 0, envyGod = 0, struckEnvy = 0, envyEnvy = 0;
	std::vector<std::string> seen;
	for (int i = 0; i < 60; i++) {
		const int kind = i % 3;
		w.year = 100 + 30 * i; w.tick = int64_t(w.year * 4);
		const int a = i % 2 ? t.um : t.te, b = i % 2 ? t.te : t.um;
		w.vars.rel[state::relKey(a, b)].war = true;
		const size_t n0 = w.chronicle.size();
		w.bus.emit(EvWarDeclared{ a, b, -1, kind > 0, kind == 2 });
		if (w.chronicle.size() == n0 || w.chronicle.back().type != "war") continue;
		const std::string& s = w.chronicle.back().text;
		if (std::find(seen.begin(), seen.end(), s) == seen.end()) { seen.push_back(s); say(s); }
		ok(clean(s), "clean: " + s);
		if (kind == 0) { plain++; plainGod += namesGod(w, s); }
		if (kind == 1) { struck++; struckGod += namesGod(w, s); struckEnvy += envious(s); }
		if (kind == 2) { envied++; envyGod += namesGod(w, s); envyEnvy += envious(s); }
		w.year += 10; w.tick = int64_t(w.year * 4);
		w.vars.rel[state::relKey(a, b)].war = false;
		w.bus.emit(EvWarEnded{ a, b, -1 });
		chronicle::updateEpithets(w);
	}
	ok(plain >= 15 && struck >= 15 && envied >= 15, "every kind of war told (" + std::to_string(plain) + "/" + std::to_string(struck) + "/" + std::to_string(envied) + ")");
	eq(plainGod, 0, "a war nobody blamed on the god never names it");
	eq(struckGod, struck, "every war blamed on the god's anger names it");
	eq(envyGod, envied, "every war of envy names the god");
	eq(struckEnvy, 0, "'the god struck us' never speaks of envy");
	eq(envyEnvy, envied, "'the god favoured them' always does");
	noNaN(w, "blame");
}

DM_TEST("chronicle diplomacy: a victory the god was thanked for names the god; an unthanked one does not") {
	stub::Guard guard;   // the hooks fresh() sets never leak into the next test
	Three t = three(63);
	World& w = *t.v.w;
	int thanked = 0, thankedGod = 0, plain = 0, plainGod = 0;
	for (int i = 0; i < 24; i++) {
		w.year = 100 + 40 * i; w.tick = int64_t(w.year * 4);
		w.vars.rel[state::relKey(t.um, t.te)].war = true;
		w.bus.emit(EvWarDeclared{ t.te, t.um });
		w.year += 12; w.tick = int64_t(w.year * 4);
		w.vars.rel[state::relKey(t.um, t.te)].war = false;
		const size_t n0 = w.chronicle.size();
		const bool th = i % 2 == 0;
		w.bus.emit(EvWarEnded{ t.te, t.um, t.um, th });
		if (w.chronicle.size() == n0 || w.chronicle.back().type != "peace") continue;
		const std::string& s = w.chronicle.back().text;
		if (i < 6) say(s);
		ok(clean(s) && has(s, "Umari"), "the victors: " + s);
		if (th) { thanked++; thankedGod += namesGod(w, s); } else { plain++; plainGod += namesGod(w, s); }
	}
	ok(thanked >= 8 && plain >= 8, "both told");
	eq(thankedGod, thanked, "the thanked victory names the god");
	eq(plainGod, 0, "an unthanked victory does not");
}

DM_TEST("chronicle diplomacy: the calming Omen has its own line naming the peoples it calmed; the war Omen names its other calmed neighbours") {
	stub::Guard guard;   // the hooks fresh() sets never leak into the next test
	Three t = three(64);
	World& w = *t.v.w;
	auto cellOf = [&](int sid) { return w.settlements[size_t(sid)].cell; };
	const int um2 = town(t.v, t.um, 400, 1);
	// no war: the Omen over the Umari calms the Teshki and the Vaeli (Powers: effect, power:used, one witness per town)
	w.year = 500; w.tick = 2000;
	swear(w, t.um, t.te);
	swear(w, t.um, t.va);
	w.bus.emit(EvPowerUsed{ "peace", cellOf(t.a), 0, false });
	for (int sid : { t.a, t.b, t.c, um2 }) w.bus.emit(EvMiracleWitnessed{ cellOf(sid), "peace", 0.3, { sid } });
	chronicle::updateEpithets(w);
	if (eq(countType(w, "miracle-peace"), 1, "one entry for the calming Omen")) {
		const ChronicleEntry* e = lastOf(w, "miracle-peace");
		say(e->text);
		ok(has(e->text, "Umari") && has(e->text, "Teshki") && has(e->text, "Vaeli") && clean(e->text), "names the people and both calmed neighbours: " + e->text);
		ok(!has(e->text, "laid down their spears"), "its own line, not the war's: " + e->text);
		eq(e->culture, t.um, "about the people it was cast on");
	}
	// one calmed neighbour only
	w.year = 600; w.tick = 2400;
	swear(w, t.te, t.va);
	w.bus.emit(EvPowerUsed{ "peace", cellOf(t.b), 0, false });
	for (int sid : { t.b, t.c }) w.bus.emit(EvMiracleWitnessed{ cellOf(sid), "peace", 0.3, { sid } });
	chronicle::updateEpithets(w);
	if (eq(countType(w, "miracle-peace"), 2, "the second calming Omen")) {
		const ChronicleEntry* e = lastOf(w, "miracle-peace");
		say(e->text);
		ok(has(e->text, "Teshki") && has(e->text, "Vaeli") && !has(e->text, "Umari") && clean(e->text), "names the pair: " + e->text);
	}
	// a war Omen that also calmed a neighbour (omenWide): one entry, both armies and the calmed people, no draw
	w.year = 700; w.tick = 2800;
	w.vars.rel[state::relKey(t.um, t.te)].war = true;
	w.bus.emit(EvWarDeclared{ t.um, t.te });
	w.year = 708; w.tick = 2832;
	w.vars.rel[state::relKey(t.um, t.te)].war = false;
	w.bus.emit(EvWarEnded{ t.um, t.te, -1 });
	swear(w, t.um, t.te);
	swear(w, t.um, t.va);
	const int peace0 = countType(w, "peace");
	w.bus.emit(EvPowerUsed{ "peace", cellOf(t.a), 0, false });
	for (int sid : { t.a, t.b, t.c, um2 }) w.bus.emit(EvMiracleWitnessed{ cellOf(sid), "peace", 0.8, { sid } });
	chronicle::updateEpithets(w);
	eq(countType(w, "peace"), peace0, "no 'neither could say it had won' beside the Omen");
	if (eq(countType(w, "miracle-peace"), 3, "one entry for the war Omen")) {
		const ChronicleEntry* e = lastOf(w, "miracle-peace");
		say(e->text);
		ok(has(e->text, "Umari") && has(e->text, "Teshki") && has(e->text, "Vaeli") && clean(e->text), "both armies and the calmed neighbour: " + e->text);
	}
	noNaN(w, "omens");
}

// (chronicle-diplomacy verify) an Omen that ends a war also ends the wars allies joined in it (Diplomacy's makePeace
// ends the league's wars together): one entry naming the allies, no "both had run out of sons" draw beside it.
// And a peace prayer the Omen answered, told this tick, is not followed by an Omen line naming no enemy.
DM_TEST("chronicle diplomacy (verify): an Omen tells the allies' wars it ended, and does not repeat an answered peace prayer") {
	stub::Guard guard;
	Three t = three(65);
	World& w = *t.v.w;
	auto cellOf = [&](int sid) { return w.settlements[size_t(sid)].cell; };
	// the Umari at war with the Teshki, the Vaeli marched as the Umari's allies (war:declared.ally)
	w.year = 700; w.tick = 2800;
	w.vars.rel[state::relKey(t.um, t.te)].war = true;
	w.bus.emit(EvWarDeclared{ t.um, t.te });
	w.vars.rel[state::relKey(t.va, t.te)].war = true;
	w.bus.emit(EvWarDeclared{ t.va, t.te, t.um });
	// the Omen over the Umari: Powers ends both wars (the principal, then the joined one), then power:used, witnesses
	w.year = 712; w.tick = 2848;
	w.vars.rel[state::relKey(t.um, t.te)].war = false;
	w.bus.emit(EvWarEnded{ t.um, t.te, -1 });
	w.vars.rel[state::relKey(t.va, t.te)].war = false;
	w.bus.emit(EvWarEnded{ t.va, t.te, -1 });
	swear(w, t.um, t.te);
	const int peace0 = countType(w, "peace"), omen0 = countType(w, "miracle-peace");
	w.bus.emit(EvPowerUsed{ "peace", cellOf(t.a), 0, false });
	for (int sid : { t.a, t.b, t.c }) w.bus.emit(EvMiracleWitnessed{ cellOf(sid), "peace", 0.8, { sid } });
	chronicle::updateEpithets(w);
	eq(countType(w, "peace"), peace0, "no draw for the allies' war beside the Omen");
	if (eq(countType(w, "miracle-peace"), omen0 + 1, "one entry for the Omen")) {
		const ChronicleEntry* e = lastOf(w, "miracle-peace");
		say(e->text);
		ok(has(e->text, "Umari") && has(e->text, "Teshki") && has(e->text, "Vaeli") && clean(e->text), "both armies and the allies: " + e->text);
	}
	// a peace prayer of the Umari answered by the Omen this tick (Society: the witness answers it first)
	w.year = 900; w.tick = 3600;
	w.vars.rel[state::relKey(t.um, t.te)].war = true;
	w.bus.emit(EvWarDeclared{ t.um, t.te });
	w.year = 905; w.tick = 3620;
	w.vars.rel[state::relKey(t.um, t.te)].war = false;
	w.bus.emit(EvWarEnded{ t.um, t.te, -1 });
	swear(w, t.um, t.te);
	const int omen1 = countType(w, "miracle-peace"), ans1 = countType(w, "prayer-answered"), peace1 = countType(w, "peace");
	w.bus.emit(EvPowerUsed{ "peace", cellOf(t.a), 0, false });
	w.bus.emit(EvPrayerAnswered{ t.a, PrayerKind::Peace });
	for (int sid : { t.a, t.b }) w.bus.emit(EvMiracleWitnessed{ cellOf(sid), "peace", 0.8, { sid } });
	chronicle::updateEpithets(w);
	if (eq(countType(w, "prayer-answered"), ans1 + 1, "the answered prayer is told")) {
		say(lastOf(w, "prayer-answered")->text);
		eq(countType(w, "miracle-peace"), omen1, "no second Omen line naming no enemy");
	}
	eq(countType(w, "peace"), peace1, "and no draw");
	noNaN(w, "allied omens");
}
