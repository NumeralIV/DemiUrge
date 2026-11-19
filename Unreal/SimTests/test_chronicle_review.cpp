// DEMIURGE — SimTests/test_chronicle_review.cpp : the second voice review (2026-11-19). Two 4,000-year
// chronicles (kind seed 3, cruel seed 1) were read end to end; these tests pin what that reading fixed.
// Owner: chronicle engineer. Runs with every other module stubbed (run.ps1 chronicle) and with the real
// ones (run.ps1 all).
#include <algorithm>
#include <cmath>
#include <cstdio>
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

const char* const kNames[] = { "Vornhal", "Tel Asha", "Qe-Luun", "Imbrek", "Sarnath", "Ulu Kesh", "Dorrow", "Mirathe", "Kesh Amun", "Oduya",
	"Brennick", "Tamsin Var", "Ilo", "Haddath", "Zuneri", "Penkarrow", "Asel", "Umbrin", "Garrowmere", "Nish" };

int culture(World& w, const char* name, const char* adj, const char* plural, const char* god, int parent = -1) {
	Culture& c = state::newCulture(w, parent);
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
	s.name = kNames[s.id % 20];
	s.pop = pop;
	s.tier = tier;
	const int id = s.id;
	if (Culture* c = state::culture(w, cid)) if (c->capital < 0) { c->capital = id; w.settlements[size_t(id)].capital = true; }
	return id;
}

R fresh(uint32_t seed) {
	R v;
	v.w = makeWorld({ .L = 5, .seed = seed, .land = 0.4 });
	v.sites = landSites(*v.w, 0.10, 40);
	v.w->tune<ChronicleTune>().maxEntries = 100000;
	v.w->phase = Phase::History;
	chronicle::attach(*v.w);
	return v;
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

} // namespace

DM_TEST("chronicle review: a Heal that ends a plague tells it once; a natural end waits for the year's close") {
	R v = fresh(41);
	World& w = *v.w;
	const int um = culture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int a = town(v, um, 800, 2), b = town(v, um, 700, 2), c = town(v, um, 600, 2);
	auto cellOf = [&](int sid) { return w.settlements[size_t(sid)].cell; };
	// healed: Powers emits the cure (plague end) first, then the witness, in one tick
	w.year = 100; w.tick = 400;
	w.bus.emit(EvSettlementPlague{ a, true });
	ok(countType(w, "plague") == 1, "the plague's coming is told");
	w.year = 110; w.tick = 440;
	w.settlements[size_t(a)].plague = 0;
	w.bus.emit(EvSettlementPlague{ a, false });
	w.bus.emit(EvMiracleWitnessed{ cellOf(a), "heal", 1.0, { a } });
	chronicle::updateEpithets(w);
	eq(countType(w, "plague-end"), 0, "a plague the god healed does not also 'burn itself out'");
	eq(countType(w, "miracle-heal"), 1, "the heal tells it");
	// a natural end (Economy, in the yearly group): written by the year's updateEpithets, same tick and year
	w.year = 120; w.tick = 480;
	w.bus.emit(EvSettlementPlague{ b, true });
	w.year = 135; w.tick = 540;
	w.bus.emit(EvSettlementPlague{ b, false });
	eq(countType(w, "plague-end"), 0, "held back while the tick runs");
	chronicle::updateEpithets(w);
	eq(countType(w, "plague-end"), 1, "written at the year's close");
	if (const ChronicleEntry* e = lastOf(w, "plague-end")) eq(e->year, 135.0, "with its own year");
	// or by the next event of a later tick
	w.year = 140; w.tick = 560;
	w.bus.emit(EvSettlementPlague{ c, true });
	w.year = 152; w.tick = 608;
	w.bus.emit(EvSettlementPlague{ c, false });
	w.tick = 609;
	w.bus.emit(EvStorm{ cellOf(c), 0.2, true });
	eq(countType(w, "plague-end"), 2, "written before the next tick's entries");
	noNaN(w, "plague endings");
}

DM_TEST("chronicle review: the Omen of Peace that ends a war is one entry naming both armies") {
	R v = fresh(43);
	World& w = *v.w;
	const int um = culture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int te = culture(w, "Teshk", "Teshkan", "the Teshki", "Orro-Ka");
	const int a = town(v, um, 900, 2), b = town(v, te, 900, 2);
	auto cellOf = [&](int sid) { return w.settlements[size_t(sid)].cell; };
	w.year = 300; w.tick = 1200;
	w.bus.emit(EvWarDeclared{ um, te });
	// Powers.use("peace"): makePeace (war:ended, draw) for every foe, then one witness per settlement of both
	w.year = 306; w.tick = 1224;
	w.bus.emit(EvWarEnded{ um, te, -1 });
	w.bus.emit(EvMiracleWitnessed{ cellOf(a), "peace", 0.8, { a } });
	w.bus.emit(EvMiracleWitnessed{ cellOf(b), "peace", 0.8, { b } });
	chronicle::updateEpithets(w);
	eq(countType(w, "miracle-peace"), 1, "one omen for both armies");
	eq(countType(w, "peace"), 0, "no 'neither could say it had won' beside it");
	if (const ChronicleEntry* e = lastOf(w, "miracle-peace")) {
		say(e->text);
		ok(has(e->text, "the Umari") && has(e->text, "the Teshki"), "the omen names both peoples: " + e->text);
	}
	// a draw with no omen is told when the tick is over
	w.year = 400; w.tick = 1600;
	w.bus.emit(EvWarDeclared{ te, um });
	w.year = 420; w.tick = 1680;
	w.bus.emit(EvWarEnded{ te, um, -1 });
	w.tick = 1681;
	w.bus.emit(EvStorm{ cellOf(a), 0.2, true });
	eq(countType(w, "peace"), 1, "a plain draw is still told");
	// a war won over a people that died with it
	w.year = 500; w.tick = 2000;
	w.bus.emit(EvWarDeclared{ um, te });
	w.cultures[size_t(te)].dead = true;
	w.year = 510; w.tick = 2040;
	w.bus.emit(EvWarEnded{ um, te, um });
	if (const ChronicleEntry* e = lastOf(w, "peace")) {
		say(e->text);
		ok(!has(e->text, "asked for peace, and") && !has(e->text, "paid tribute for") && !has(e->text, "swore to remember") &&
			!has(e->text, "counted what they had lost") && !has(e->text, "were beaten"), "nobody of a dead people asks for peace: " + e->text);
	}
}

DM_TEST("chronicle review: an answered prayer quiets the town's new prayers and the rain over it") {
	R v = fresh(47);
	World& w = *v.w;
	const int um = culture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int a = town(v, um, 900, 2);
	auto cellOf = [&](int sid) { return w.settlements[size_t(sid)].cell; };
	w.year = 100; w.tick = 400;
	w.bus.emit(EvPrayer{ a, PrayerKind::Rain, true });
	eq(countType(w, "prayer"), 1, "the prayer is told");
	w.year = 101; w.tick = 404;
	w.bus.emit(EvPrayerAnswered{ a, PrayerKind::Rain });
	eq(countType(w, "prayer-answered"), 1, "the answer is told");
	w.year = 101.5; w.tick = 406;                          // the brush goes on over the town
	w.bus.emit(EvMiracleWitnessed{ cellOf(a), "rain", 0.6, { a } });
	eq(countType(w, "miracle-rain"), 0, "the answer already told this rain");
	w.year = 103; w.tick = 412;
	w.bus.emit(EvPrayer{ a, PrayerKind::Rain, true });
	eq(countType(w, "prayer"), 1, "no new prayer right after the answer");
	w.year = 109; w.tick = 436;
	w.bus.emit(EvPrayer{ a, PrayerKind::Rain, true });
	eq(countType(w, "prayer"), 1, "one prayer per town per prayerTownGap years");
	w.year = 120; w.tick = 480;
	w.bus.emit(EvPrayer{ a, PrayerKind::Rain, true });
	eq(countType(w, "prayer"), 1, "a people the god has been answering expects the answer: not news");
	w.year = 130; w.tick = 520;
	w.bus.emit(EvPrayer{ a, PrayerKind::Rain, true });
	eq(countType(w, "prayer"), 2, "a later prayer is told");
	// soft rain over a town that has prayed: it was asked for
	for (int k = 0; k < 8; k++) {
		w.year = 140 + 31 * k; w.tick = 560 + 124 * k;
		w.bus.emit(EvMiracleWitnessed{ cellOf(a), "rain", 0.1, { a } });
	}
	int rains = 0;
	for (const ChronicleEntry& e : w.chronicle) {
		if (e.type != "miracle-rain") continue;
		rains++;
		ok(!has(e.text, "not been asked for"), "a town with a prayer pending asked for its rain: " + e.text);
	}
	ok(rains >= 4, "soft rains are told (" + std::to_string(rains) + ")");
}

DM_TEST("chronicle review: a new people's first town: one 'first of its kind' line, and no waking of the daughters") {
	R v = fresh(53);
	World& w = *v.w;
	const int um = culture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int da = culture(w, "Vael", "Vaelish", "the Vaeli", "Ashvel", um);
	const int db = culture(w, "Oru", "Oruni", "the Orun", "Mbele", um);
	town(v, um, 2000, 3);
	const int t = town(v, da, 300, 1);
	const int u = town(v, db, 300, 1);
	w.year = 600; w.tick = 2400;
	chronicle::updateEpithets(w);
	for (const ChronicleEntry& e : w.chronicle)
		if (e.type == "epithet" && (e.culture == da || e.culture == db)) ok(!has(e.text, "who had woken them"), "a daughter people was not woken: " + e.text);
	const Building::Id kinds[] = { Building::SHRINE, Building::GRANARY, Building::PORT, Building::TEMPLE, Building::MARKET, Building::MINE };
	for (Building::Id b : kinds) {
		w.settlements[size_t(t)].b[size_t(b)] = 1;
		w.bus.emit(EvSettlementBuilding{ t, b });
		w.year += 0.25; w.tick++;
	}
	int firsts = 0;
	for (const ChronicleEntry& e : w.chronicle) {
		if (e.type != "building" || e.culture != da) continue;
		say(e.text);
		ok(!has(e.text, "who had woken them"), "a daughter's first shrine: " + e.text);
		firsts += has(e.text, "It was the first") || has(e.text, "had built");
	}
	eq(firsts, 1, "one 'first of its kind' line for six firsts in two years");
	w.year += 6; w.tick += 24;
	w.settlements[size_t(t)].b[Building::ACADEMY] = 1;
	w.bus.emit(EvSettlementBuilding{ t, Building::ACADEMY });
	const ChronicleEntry* ac = lastOf(w, "building");
	ok(ac && (has(ac->text, "It was the first") || has(ac->text, "had built")), "a first some years later is told as one");
	// the Orun call the god by their own name
	w.settlements[size_t(u)].b[Building::SHRINE] = 1;
	w.bus.emit(EvSettlementBuilding{ u, Building::SHRINE });
	if (const ChronicleEntry* e = lastOf(w, "building")) ok(!has(e->text, "who had woken them"), e->text);
}

DM_TEST("chronicle review: the approved names are each epithet group's primary choice") {
	R v = fresh(59);
	World& w = *v.w;
	const int um = culture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	town(v, um, 900, 2);
	w.year = 50;
	struct Case { const char* kind; std::vector<std::string> approved, invented; };
	const Case cases[] = {
		{ "lightning", { "the Lightning-Handed" }, { "the Thunderer", "the Spear of Heaven" } },
		{ "peace", { "the Breaker of Spears", "the Quiet Between Armies" }, { "the Peacemaker" } },
		{ "volcano", { "the Mountain-Maker" }, { "the Fire-Mountain", "the Ash-Maker" } },
		{ "rain", { "the Rain-Giver", "the Cloud-Shepherd", "She Who Opens the Sky", "the Well-Filler" }, {} },
	};
	const uint32_t seed0 = w.seed;
	for (const Case& c : cases) {
		w.cultures[size_t(um)].seen.clear();
		w.cultures[size_t(um)].seen[c.kind] = 2.0;
		int nA = 0, nI = 0, other = 0;
		for (uint32_t s = 1; s <= 300; s++) {
			w.seed = s;
			const std::string e = chronicle::epithet(w, um);
			if (std::find(c.approved.begin(), c.approved.end(), e) != c.approved.end()) nA++;
			else if (std::find(c.invented.begin(), c.invented.end(), e) != c.invented.end()) nI++;
			else other++;
		}
		say(std::string(c.kind) + ": approved " + std::to_string(nA) + ", invented " + std::to_string(nI) + ", other " + std::to_string(other));
		eq(other, 0, std::string("every name from its group: ") + c.kind);
		ok(nA >= 210, std::string("approved names are the primary choice: ") + c.kind);
		if (!c.invented.empty()) ok(nI >= 20, std::string("invented names that fit the voice are kept: ") + c.kind);
	}
	w.seed = seed0;
}

DM_TEST("chronicle review: a kind god whose coasts drown is not two-handed") {
	dmt::stub::Guard guard(false);
	R v = fresh(61);
	World& w = *v.w;
	const int um = culture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	town(v, um, 900, 2);
	w.year = 500;
	Culture& c = w.cultures[size_t(um)];
	c.seen["rain"] = 2.0;
	c.seen["bloom"] = 1.0;
	w.vars.num["chronicle_d." + std::to_string(um)] = 10;  // ten towns lost to the sea (x drownWeight 0.3)
	const std::string e = chronicle::epithet(w, um);
	say("rain, bloom and a drowning coast: " + e);
	for (const char* m : { "the Two-Handed", "the Laughing Storm", "the Coin-Tosser" }) ok(e != m, "not two-handed for the sea's work (" + e + ")");
	if (dmt::stub::stubbed("society")) {
		w.vars.num.erase("chronicle_d." + std::to_string(um));
		c.seen["lightning"] = 3.0;
		const std::string m = chronicle::epithet(w, um);
		say("rain, bloom and the god's own lightning: " + m);
		ok(m == "the Two-Handed" || m == "the Laughing Storm" || m == "the Coin-Tosser", "the god's own blows still make it two-handed (" + m + ")");
	}
}

DM_TEST("chronicle review: captures, settlers lost at home, fires lit by a bolt, the tale's quoted years") {
	R v = fresh(67);
	World& w = *v.w;
	const int um = culture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int da = culture(w, "Vael", "Vaelish", "the Vaeli", "Ashvel", um);
	std::vector<int> towns;
	for (int k = 0; k < 12; k++) towns.push_back(town(v, um, 500, 2));
	auto cellOf = [&](int sid) { return w.settlements[size_t(sid)].cell; };
	// the same god's name on both sides: the temple keeps it
	for (size_t k = 0; k < towns.size(); k++) {
		w.year = 100 + 10.0 * double(k); w.tick = 400 + 40 * int64_t(k);
		const int sid = towns[k];
		w.settlements[size_t(sid)].b[Building::TEMPLE] = 1;
		const int from = w.settlements[size_t(sid)].culture, to = from == um ? da : um;
		w.settlements[size_t(sid)].culture = to;
		w.bus.emit(EvSettlementCaptured{ sid, from, to, CaptureHow::War });
	}
	for (const ChronicleEntry& e : w.chronicle) if (e.type == "captured") ok(!has(e.text, "new god's name"), "same god, same name: " + e.text);
	// settlers that die at the town they left
	for (int k = 0; k < 6; k++) {
		w.year = 300 + 7.0 * k; w.tick = 1200 + 28 * k;
		w.bus.emit(EvUnitDied{ 50 + k, UnitType::Settler, da, cellOf(towns[0]), "lava" });
	}
	for (const ChronicleEntry& e : w.chronicle) {
		if (e.type != "settlers-lost") continue;
		say(e.text);
		ok(!has(e.text, "caught at") && !has(e.text, "lost at") && !has(e.text, "upon"), "the home town is not the place they were lost: " + e.text);
	}
	// a fire right after the bolt that lit it
	for (int k = 0; k < 10; k++) {
		w.year = 400 + 25.0 * k; w.tick = 1600 + 100 * k;
		const int at = cellOf(towns[size_t(k) % towns.size()]);
		w.bus.emit(EvLightning{ at, true });
		w.bus.emit(EvFireStart{ at });
	}
	int fires = 0;
	for (const ChronicleEntry& e : w.chronicle) {
		if (e.type != "fire") continue;
		fires++;
		ok(!has(e.text, "Lightning found") && !has(e.text, "started by"), "no second cause for a fire the bolt lit: " + e.text);
	}
	ok(fires >= 5, "fires told (" + std::to_string(fires) + ")");
	// the tale quotes no present tense after "In the year N,"
	chronicle::EntryOpts o;
	o.type = "culture-died"; o.weight = 3; o.culture = da;
	o.text = "The Vaeli are no more. Their words live on in the mouths of their neighbours, who use them for cursing and for the names of rivers.";
	w.year = 700;
	chronicle::add(w, o);
	o.type = "destroyed"; o.text = "Where Nish stood, fish now swim.";
	w.year = 710;
	chronicle::add(w, o);
	o.type = "wonder"; o.text = "The Umari finished the Hanging Stair at Vornhal.";
	w.year = 720;
	chronicle::add(w, o);
	const chronicle::Tale tl = chronicle::tale(w);
	std::string all;
	for (const std::string& p : tl.paragraphs) all += p + " ";
	ok(!has(all.c_str(), " are no more") && !has(all.c_str(), "now swim"), "no present tense in the remembered years");
	ok(has(all.c_str(), "the Umari finished the Hanging Stair"), "the past-tense entry is quoted");
	noNaN(w, "review");
}
