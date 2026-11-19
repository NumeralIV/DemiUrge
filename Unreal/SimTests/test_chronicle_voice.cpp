// DEMIURGE — SimTests/test_chronicle_voice.cpp : the approved chronicle voice (docs/content/chronicle-voice.md)
// is wired to its events. Owner: chronicle engineer. Every sample entry 1-60, its variants, the two
// extras, the people:lost line and the six endings must be reachable from the event the voice maps
// it to, with its placeholders filled from real data; every "In the Nth year" must be the entry's own
// year as a correct English ordinal; the voice's epithet groups follow the god's behaviour.
// Runs with every other module stubbed (run.ps1 chronicle) and with the real ones (run.ps1 all).
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
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

struct V {
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

int town(V& v, int cid, double pop, int tier, int cell = -1) {
	World& w = *v.w;
	if (cell < 0) cell = v.sites[v.next++ % v.sites.size()];
	Settlement& s = state::newSettlement(w, cell, cid);
	s.name = kNames[s.id % 20];
	s.pop = pop;
	s.tier = tier;
	const int id = s.id;
	if (Culture* c = state::culture(w, cid)) if (c->capital < 0) { c->capital = id; w.settlements[size_t(id)].capital = true; }
	return id;
}

int cellNear(const World& w, int c, double ang) {
	int best = -1;
	double bd = 1e9;
	for (int m = 0; m < w.n; m++) {
		if (state::isSea(w, m) || m == c) continue;
		const double d = std::fabs(w.geo->angle(c, m) - ang);
		if (d < bd) { bd = d; best = m; }
	}
	return best;
}

std::string ordinal(long long n) {
	if (n < 1) return "first";
	const long long t = n % 100, u = n % 10;
	return std::to_string(n) + ((t >= 11 && t <= 13) ? "th" : u == 1 ? "st" : u == 2 ? "nd" : u == 3 ? "rd" : "th");
}

// "" when the text reads as prose and every "In the Nth year" is the entry's year
std::string fault(const ChronicleEntry& e) {
	const std::string& t = e.text;
	if (t.size() < 12) return "too short";
	if (!(std::isupper(static_cast<unsigned char>(t[0])) || t[0] == '"')) return "no capital";
	if (t.back() != '.' && t.back() != '!' && t.back() != '?' && t.back() != '"') return "no final stop";
	for (char ch : t) if (ch == '{' || ch == '}' || ch == '[' || ch == ']' || ch == '|' || ch == '_') return "template syntax";
	// digits only as "<the entry's year as an ordinal> year"
	const std::string ord = ordinal(static_cast<long long>(std::floor(e.year))) + " year";
	std::string rest = t;
	for (size_t p; (p = rest.find(ord)) != std::string::npos;) rest.erase(p, ord.size());
	for (char ch : rest) if (std::isdigit(static_cast<unsigned char>(ch))) return "digit that is not this entry's year ordinal";
	// articles and doubled words
	const std::string sp = " " + t + " ";
	for (const char* v : { " a a", " a e", " a i", " a o", " a u", " A a", " A e", " A i", " A o", " A u" })
		if (sp.find(v) != std::string::npos) return std::string("article before a vowel ('") + v + "')";
	for (const char* d : { "the", "a", "of", "at", "in", "to", "and" })
		if (sp.find(std::string(" ") + d + " " + d + " ") != std::string::npos) return std::string("doubled '") + d + "'";
	if (t.find("  ") != std::string::npos || t.find(" ,") != std::string::npos || t.find(" .") != std::string::npos) return "spacing";
	return "";
}

} // namespace

DM_TEST("chronicle voice: every sample entry, variant, extra and ending is reachable from its event, names filled") {
	V v;
	v.w = makeWorld({ .L = 5, .seed = 17, .land = 0.4 });
	World& w = *v.w;
	w.name = "Erethe";
	v.sites = landSites(w, 0.10, 80);
	w.tune<ChronicleTune>().maxEntries = 100000;          // keep every entry for the search below
	chronicle::attach(w);
	const int um = culture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int te = culture(w, "Teshk", "Teshkan", "the Teshki", "Orro-Ka");
	const int capital = town(v, um, 2000, 3);
	const int t1 = town(v, um, 600, 2);
	const int t2 = town(v, um, 300, 1);
	const int t3 = town(v, te, 900, 2);
	const int t4 = town(v, te, 400, 1);
	const int t5 = town(v, um, 250, 1);
	for (int b : { Building::GRANARY, Building::SHRINE, Building::TEMPLE, Building::WALLS, Building::LAUNCH }) w.settlements[size_t(t5)].b[size_t(b)] = 1;
	auto cellOf = [&](int sid) { return w.settlements[size_t(sid)].cell; };
	auto tick = [&](double dy) { w.year += dy; w.tick += 4; };
	// a forest beside t1 for the wildfire line
	const int forest = cellNear(w, cellOf(t1), 0.05);
	if (forest >= 0 && size_t(forest) < w.biome.size()) w.biome[size_t(forest)] = Biome::FOREST;
	// the first awakening, at the first year
	w.year = 0;
	w.phase = Phase::People;
	for (int i = 0; i < 12; i++) w.bus.emit(EvSettlementFounded{ capital, cellOf(capital), um, FoundedBy::Awaken });
	w.phase = Phase::History;
	w.year = 100;
	w.vars.rel[state::relKey(um, te)] = Relation{};
	const int rounds = 64;
	for (int r = 0; r < rounds; r++) {
		tick(40);
		// genesis, retold each round (the voice's once-a-world lines)
		w.vars.num["chronicle_comets"] = 0;
		w.bus.emit(EvPhase{ Phase::People, Phase::Life });
		w.bus.emit(EvMeteorImpact{ cellOf(capital), 0.03, ImpactKind::Comet, true });
		tick(1);
		// a settler band from t2 founds a town (origin known from the arriving unit)
		{
			const int cell = v.sites[(20 + size_t(r)) % v.sites.size()];
			Unit& u = state::newUnit(w, UnitType::Settler);
			u.culture = um;
			u.from = t2;
			u.to = -1;
			u.path = { cellOf(t2), cell };
			u.i = 1;
			Settlement& s = state::newSettlement(w, cell, um);
			s.name = "Ravik";
			s.pop = 40;
			const int sid = s.id;
			w.bus.emit(EvSettlementFounded{ sid, cell, um, FoundedBy::Settlers });
			w.units.clear();
			w.settlements[size_t(sid)].dead = true;   // keep the live map small
			w.settlements[size_t(sid)].deadYear = w.year;
		}
		tick(1);
		// growth and decline
		w.settlements[size_t(t2)].tier = 1;
		w.settlements[size_t(t2)].tier = 2;
		w.bus.emit(EvSettlementTier{ t2, 2, 1 });
		tick(1);
		w.settlements[size_t(t2)].tier = 1;
		w.bus.emit(EvSettlementTier{ t2, 1, 2 });
		tick(1);
		// buildings (none the first of its kind: t5 has them all); ordinary ones are rate-limited, so one
		// a round in turn, and a launch tower (never limited)
		{
			const Building::Id kinds[] = { Building::GRANARY, Building::SHRINE, Building::TEMPLE, Building::WALLS };
			for (Building::Id b : { kinds[size_t(r) % 4], Building::LAUNCH }) {
				w.settlements[size_t(t1)].b[size_t(b)] = 1;
				w.bus.emit(EvSettlementBuilding{ t1, b });
				tick(0.25);
			}
		}
		// famine, plague and its end
		w.bus.emit(EvSettlementFamine{ t2 });
		tick(1);
		w.bus.emit(EvSettlementPlague{ t4, true });
		tick(8);
		w.bus.emit(EvSettlementPlague{ t4, false });
		tick(1);
		// ruin, by cause (fresh towns of the Teshki)
		for (Cause c : { Cause::Drowned, Cause::Meteor, Cause::Lava, Cause::Wave, Cause::Abandoned, Cause::Quake }) {
			const int sid = town(v, te, 300, 2);
			if (c == Cause::Quake && size_t(cellOf(sid)) < w.coast.size()) w.coast[size_t(cellOf(sid))] = 0;
			w.settlements[size_t(sid)].dead = true;
			w.settlements[size_t(sid)].deadYear = w.year;
			w.settlements[size_t(sid)].cause = c;
			w.bus.emit(EvSettlementDestroyed{ sid, cellOf(sid), te, c });
			tick(0.25);
		}
		// razed by an army: the battle that took it names the razers
		{
			const int sid = town(v, te, 120, 2);
			w.bus.emit(EvBattle{ cellOf(sid), um, te, true, sid });
			w.settlements[size_t(sid)].dead = true;
			w.settlements[size_t(sid)].deadYear = w.year;
			w.settlements[size_t(sid)].cause = Cause::War;
			w.bus.emit(EvSettlementDestroyed{ sid, cellOf(sid), te, Cause::War });
		}
		tick(1);
		// taken in war
		{
			const int sid = town(v, te, 500, 2);
			w.settlements[size_t(sid)].culture = um;
			w.bus.emit(EvSettlementCaptured{ sid, te, um, CaptureHow::War });
			w.settlements[size_t(sid)].dead = true;
			w.settlements[size_t(sid)].deadYear = w.year;
		}
		tick(1);
		// new peoples, and one that passes
		{
			const int a = culture(w, "Vael", "Vaelish", "the Vaeli", "Ashvel", te);
			w.bus.emit(EvCultureBorn{ a, te, cellOf(t4), BirthReason::Schism });
			tick(0.25);
			const int b = culture(w, "Oru", "Oruni", "the Orun", "Mbele", um);
			w.bus.emit(EvCultureBorn{ b, um, cellOf(t2), BirthReason::Distance });
			tick(0.25);
			w.cultures[size_t(a)].dead = true;
			w.bus.emit(EvCultureDied{ a });
			w.cultures[size_t(b)].dead = true;
		}
		tick(1);
		// ages: the world's first (the Umari, every other people at 0) and a later one (the Teshki)
		w.cultures[size_t(um)].age = 1;
		w.bus.emit(EvCultureAge{ um, 1, 0 });
		tick(0.25);
		w.cultures[size_t(um)].age = 6;
		w.cultures[size_t(te)].age = 1;
		w.bus.emit(EvCultureAge{ te, 1, 0 });
		w.cultures[size_t(te)].age = 0;
		w.cultures[size_t(um)].age = 0;
		tick(1);
		// war: declared, a siege that held, peace with a victor, then a draw
		w.vars.rel[state::relKey(um, te)].war = true;
		w.bus.emit(EvWarDeclared{ te, um });
		tick(3);
		w.bus.emit(EvBattle{ cellOf(t1), te, um, false, t1 });
		tick(1);
		w.bus.emit(EvMiracleWitnessed{ cellOf(t3), "peace", 0.8, { t3 } });   // an omen over a people at war
		tick(4);
		w.vars.rel[state::relKey(um, te)].war = false;
		w.bus.emit(EvWarEnded{ te, um, um });
		tick(1);
		w.bus.emit(EvWarDeclared{ um, te });
		tick(2);
		w.bus.emit(EvWarEnded{ um, te, -1 });
		tick(1);
		// trade: within a people, and the first road between two peoples
		for (auto it = w.vars.num.begin(); it != w.vars.num.end();) {
			if (it->first.rfind("chronicle_tp.", 0) == 0) it = w.vars.num.erase(it); else ++it;
		}
		w.bus.emit(EvTradeRoute{ capital, t2 });
		tick(1);
		w.bus.emit(EvTradeRoute{ capital, t3 });
		tick(1);
		// prophets and edicts (the crusade's mark: the Teshki, in contact)
		w.vars.rel[state::relKey(um, te)].contact = true;
		w.cultures[size_t(um)].prophet = Prophet{ "Ishmavel", capital, w.year + 50 };
		w.bus.emit(EvProphetRisen{ um, capital, "Ishmavel" });
		for (Edict ed : { Edict::Peace, Edict::Expand, Edict::Build, Edict::Crusade }) {
			tick(0.25);
			w.cultures[size_t(um)].edict = ed;
			w.bus.emit(EvEdict{ um, ed });
		}
		tick(1);
		w.cultures[size_t(um)].prophet.reset();
		w.bus.emit(EvProphetDied{ um, "Ishmavel" });
		tick(1);
		w.bus.emit(EvWonderBuilt{ capital, "the Hanging Stair" });
		tick(1);
		// prayers: begun, answered, and one unanswered for twenty years
		w.bus.emit(EvPrayer{ t2, PrayerKind::Rain, true });
		tick(1);
		w.bus.emit(EvPrayerAnswered{ t3, PrayerKind::Rain });
		w.bus.emit(EvPrayer{ t1, PrayerKind::Heal, true });
		tick(1);
		// the god's acts
		const int nearCap = cellNear(w, cellOf(capital), 0.04);
		struct M { int sid; const char* kind; double good; };
		for (const M& m : { M{ t2, "rain", 0.7 }, M{ t2, "bloom", 0.4 }, M{ t1, "raise", 0.0 }, M{ t3, "heal", 1.0 }, M{ t3, "inspire", 0.6 }, M{ t4, "plague", -0.7 } }) {
			if (Culture* c = state::culture(w, w.settlements[size_t(m.sid)].culture)) c->seen[m.kind] += 0.05;
			w.bus.emit(EvMiracleWitnessed{ cellOf(m.sid), m.kind, m.good, { m.sid } });
			tick(0.25);
		}
		w.bus.emit(EvLightning{ nearCap, false });
		tick(0.25);
		w.bus.emit(EvMeteorImpact{ nearCap, 0.05, ImpactKind::Meteor, false });
		tick(0.25);
		w.bus.emit(EvVolcanoStart{ nearCap, 3, false });
		tick(0.25);
		w.bus.emit(EvQuake{ nearCap, 0.08, 1.0, false });
		tick(1);
		// the world
		w.bus.emit(EvStorm{ cellOf(t1), 0.15, true });
		tick(0.25);
		w.bus.emit(EvDrought{ cellOf(t1), 0.2, true });
		tick(0.25);
		if (forest >= 0) w.bus.emit(EvFireStart{ forest });
		tick(0.25);
		w.bus.emit(EvTsunamiHit{ cellOf(capital), 1.5 });
		tick(0.25);
		w.bus.emit(EvClimateMilestone{ MilestoneKind::Warming, 1.0 });
		w.bus.emit(EvClimateMilestone{ MilestoneKind::SeaRise, 0.2 });
		tick(0.25);
		w.bus.emit(EvClimateMilestone{ MilestoneKind::Cooling, -2.0 });
		w.bus.emit(EvClimateMilestone{ MilestoneKind::IceAge, -4.0 });
		tick(1);
		w.vars.num["chronicle_nukes"] = 0;
		w.bus.emit(EvNuke{ cellOf(t3), um, te });
		tick(1);
		w.bus.emit(EvArkStarted{ um, capital });
		tick(1);
		w.bus.emit(EvArkLaunched{ um, capital });
		tick(1);
		w.bus.emit(EvPeopleLost{});
		tick(1);
		// the people change their name for the god: a new, larger deed each round
		w.cultures[size_t(um)].seen[r % 2 ? "lightning" : "rain"] += 4.0 * (r + 1);
		chronicle::updateEpithets(w);
		tick(12);
		w.bus.emit(EvPrayer{ t1, PrayerKind::Heal, false });  // twenty years unanswered
	}
	// the endings, by ending id
	w.phase = Phase::Ended;
	for (Mood m : { Mood::Beloved, Mood::Feared, Mood::Forgotten, Mood::Parting }) { tick(1); w.bus.emit(EvEnding{ EndingType::Ark, m, um, capital, w.year }); }
	for (Mood m : { Mood::Ash, Mood::Quiet }) { tick(1); w.bus.emit(EvEnding{ EndingType::Silence, m, -1, -1, w.year }); }

	// every voice line, by a fragment of its own words
	struct F { const char* id; const char* text; };
	const F frags[] = {
		{ "v1", "before there was counting" }, { "v2", "that was the first sea" }, { "v3", "as if it were not sure it was allowed" },
		{ "v4a", "seed corn and quarrels" }, { "v4b", "neighbours' faces" }, { "v4c", "all founders swear this" },
		{ "v5a", "could no longer call itself" }, { "v5b", "the old boundary stones" }, { "v5c", "gave up learning their names" },
		{ "v6", "too wide for the people left" }, { "v7a", "the usual length of pride" }, { "v7b", "as is the way of all building" },
		{ "v7c", "mud and opinion" }, { "v8", "did not complain" }, { "v9", "rest twice on the stair" }, { "v10", "No one said against whom" },
		{ "v11", "a tower of iron and a tower of fire" }, { "v12a", "rang hollow when struck" }, { "v12b", "sat down at every table" },
		{ "v12c", "and the days were long" }, { "v13", "by the river road" }, { "v14", "as it had come, without asking" },
		{ "v15", "by a hand's width a year" }, { "v16", "a round hollow" }, { "v17", "ran like honey" }, { "v18", "The crows kept no lists" },
		{ "v19", "What the shaking spared" }, { "v20", "out of habit" }, { "v21", "having run out of other ideas" },
		{ "v22", "stopped coming to be laughed at" }, { "v23", "as a branch comes from a tree" }, { "v24", "for cursing and for the names of rivers" },
		{ "v25a", "over the noise" }, { "v25b", "A new thing was learned at" }, { "v25c", "the tools of their mothers" },
		{ "v26", "the world turned a page" }, { "v27a", "an old insult" }, { "v27b", "share the river" }, { "v27c", "which prayer was heard" },
		{ "v28a", "and to the birds" }, { "v28b", "which no one has asked to read" }, { "v28c", "like a wave on rock" },
		{ "v29", "pay for the asking" }, { "v30", "run out of sons" }, { "v31a", "the gossip sold best" }, { "v31b", "and, later, fevers" },
		{ "v31c", "were also people" }, { "v32", "moved their stalls" }, { "v33", "buried facing the sunrise" }, { "v34", "Lay down the spear" },
		{ "v35", "took this as permission" }, { "v36", "seen from the sky" }, { "v37", "pray badly" }, { "v38", "complained of the dust" },
		{ "v39", "They prayed loudly" }, { "v40", "as surprised as anyone" }, { "v41", "The shrine grew moss" }, { "v42", "the dry fields drank" },
		{ "v43", "heard a rumour" }, { "v44", "the way a sleeper moves a blanket" }, { "v45", "to be safe" }, { "v46", "it was not a kind one" },
		{ "v47", "roaring like a calf" }, { "v48", "which was itself an answer" }, { "v49", "they punished everything" },
		{ "v50", "asked what there was to eat" }, { "v51", "claimed them as their own" }, { "v52", "neither remembered why they had come" },
		{ "v53", "the roofs learned to fly" }, { "v54", "and the people counted them" }, { "v56", "the stranded fish" },
		{ "v57", "the ice agreed with them" }, { "v58", "the summers forgot to come" }, { "v59", "did not set gently" }, { "v60", "a star among stars" },
		{ "extra ark:started", "would not sail on water" }, { "extra epithet", "in the market and at the hearth" },
		{ "people:lost", "the grass forgot them too" },
		{ "ark-beloved", "a shrine before there is a house" }, { "ark-feared", "build their houses low" }, { "ark-forgotten", "no one remembered to" },
		{ "ark-parting", "a careful goodbye" }, { "silence-ash", "learned at last to make suns" }, { "silence-quiet", "no last battle and no last prayer" },
	};
	int missing = 0;
	for (const F& fr : frags) {
		const ChronicleEntry* hit = nullptr;
		for (const ChronicleEntry& e : w.chronicle) if (e.text.find(fr.text) != std::string::npos) { hit = &e; break; }
		if (!ok(hit != nullptr, std::string("voice line ") + fr.id + " never written ('" + fr.text + "')")) { missing++; continue; }
		if (std::string(fr.id) == "v4a" || std::string(fr.id) == "v4b" || std::string(fr.id) == "v4c")
			ok(hit->text.find("Imbrek") != std::string::npos || hit->text.find(w.settlements[size_t(t2)].name) != std::string::npos,
				std::string(fr.id) + ": {origin} is the settler band's home town: " + hit->text);
		if (std::string(fr.id) == "v18") ok(hit->text.find("the Umari") != std::string::npos, "v18: the razers are the attackers: " + hit->text);
		if (std::string(fr.id) == "v37") ok(hit->text.find("\"The Teshki pray badly.\"") != std::string::npos, "v37: the crusade's mark: " + hit->text);
		if (std::string(fr.id) == "v52") ok(hit->text.find("the Umari") != std::string::npos && hit->text.find("the Teshki") != std::string::npos, "v52: both armies named: " + hit->text);
		if (std::string(fr.id) == "v29") ok(hit->text.find("After ") == 0 && hit->text.find(" years ") != std::string::npos, "v29: the war's length in words: " + hit->text);
	}
	if (forest >= 0) {
		bool v55 = false;
		for (const ChronicleEntry& e : w.chronicle) if (e.text.find("the forest burned for a season") != std::string::npos) v55 = true;
		ok(v55, "voice line v55 never written (a forest fire)");
	}
	// prose, ordinals, placeholders
	int faults = 0;
	for (const ChronicleEntry& e : w.chronicle) {
		const std::string f = fault(e);
		if (!f.empty()) { if (faults < 8) say(f + ": [" + chronicle::yearStr(w, e.year) + "] " + e.text); faults++; }
	}
	eq(faults, 0, "voice entries read as prose, every ordinal is the entry's year");
	say(std::to_string(w.chronicle.size()) + " entries over " + std::to_string(rounds) + " rounds; " +
		std::to_string(int(sizeof(frags) / sizeof(frags[0])) - missing) + " of " + std::to_string(int(sizeof(frags) / sizeof(frags[0]))) + " voice lines found");
	noNaN(w, "voice run");
}

DM_TEST("chronicle voice: the 'In the Nth year' opener never drums and the tale drops it when it gives the year") {
	V v;
	v.w = makeWorld({ .L = 4, .seed = 23, .land = 0.4 });
	World& w = *v.w;
	w.name = "Erethe";
	v.sites = landSites(w, 0.12, 20);
	chronicle::attach(w);
	const int a = culture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int b = culture(w, "Teshk", "Teshkan", "the Teshki", "Orro-Ka");
	town(v, a, 900, 2);
	town(v, b, 900, 2);
	w.phase = Phase::History;
	w.year = 10;
	for (int i = 0; i < 200; i++) {
		w.year += 1;
		w.bus.emit(EvWarDeclared{ i % 2 ? a : b, i % 2 ? b : a });
	}
	int openers = 0, runs = 0;
	for (size_t i = 0; i < w.chronicle.size(); i++) {
		const bool in = w.chronicle[i].text.rfind("In the ", 0) == 0;
		openers += in;
		if (in && i >= 1 && w.chronicle[i - 1].text.rfind("In the ", 0) == 0) runs++;
	}
	say("200 wars: " + std::to_string(openers) + " entries open with 'In the', " + std::to_string(runs) + " right after another");
	ok(openers > 0, "the opener is used");
	eq(runs, 0, "never two 'In the' openers in a row");
	// the tale quotes epochal entries after its own "In the year N," and drops the entry's opener
	w.year = 700;
	chronicle::add(w, { "wonder", "In the 700th year the Umari finished the Hanging Stair at Vornhal.", -1, a, 3 });
	w.year = 720;
	chronicle::add(w, { "destroyed", "The sea rose at Ilo by a hand's width a year, and then by another. In the 720th year the last of its people rowed away.", -1, a, 3 });
	const chronicle::Tale t = chronicle::tale(w);
	std::string all;
	for (const std::string& p : t.paragraphs) all += p + " ";
	ok(all.find("In the year 700, the Umari finished the Hanging Stair") != std::string::npos, "tale: opener dropped after its own year");
	ok(all.find("700th") == std::string::npos, "tale: no doubled year");
	ok(all.find("and then by another. In that year the last of its people rowed away") != std::string::npos, "tale: a year inside a quoted entry is 'that year'");
	ok(all.find("720th") == std::string::npos, "tale: no ordinal of a quoted year");
}

DM_TEST("chronicle voice: kindness or wrath of many kinds names the god Kind or Cruel; one deed keeps its own name") {
	dmt::stub::Guard guard(false);
	if (!dmt::stub::stubbed("society")) { say("society is real: regard hook not used"); return; }
	V v;
	v.w = makeWorld({ .L = 4, .seed = 29, .land = 0.4 });
	World& w = *v.w;
	v.sites = landSites(w, 0.12, 20);
	chronicle::attach(w);
	double love = 0.6;
	dmt::stub::hooks().society_regard = [&love](const World&, int) -> society::Regard {
		society::Regard r;
		r.devotion = 0.6;
		r.love = love;
		return r;
	};
	const int kind = culture(w, "Umar", "Umaran", "the Umari", "Ashvel");
	const int cruel = culture(w, "Teshk", "Teshkan", "the Teshki", "Orro-Ka");
	const int one = culture(w, "Vael", "Vaelish", "the Vaeli", "Ka-Imen");
	town(v, kind, 500, 2);
	town(v, cruel, 500, 2);
	town(v, one, 500, 2);
	w.phase = Phase::History;
	for (const char* k : { "rain", "bloom", "heal", "inspire", "peace" }) w.cultures[size_t(kind)].seen[k] = 1.0;
	for (const char* k : { "lightning", "quake", "plague", "meteor", "volcano" }) w.cultures[size_t(cruel)].seen[k] = 1.0;
	w.cultures[size_t(one)].seen["rain"] = 3.0;
	auto among = [](const std::string& e, std::initializer_list<const char*> l) { for (const char* o : l) if (e == o) return true; return false; };
	love = 0.6;
	const std::string k = chronicle::epithet(w, kind);
	ok(among(k, { "the Gentle Hand", "the Kind Sky", "Mother of Harvests", "the Patient One", "He Who Keeps the Lamp Lit" }), "many kindnesses, loved: Kind (" + k + ")");
	const std::string o = chronicle::epithet(w, one);
	ok(among(o, { "the Rain-Giver", "the Cloud-Shepherd", "She Who Opens the Sky", "the Well-Filler" }), "one deed keeps its name (" + o + ")");
	love = -0.5;
	const std::string c = chronicle::epithet(w, cruel);
	ok(among(c, { "the Unkind Sky", "the Hungry One", "the Tyrant Above", "the One We Do Not Name" }), "many blows, feared: Cruel (" + c + ")");
	love = 0.0;
	const std::string kn = chronicle::epithet(w, kind);
	ok(!among(kn, { "the Gentle Hand", "the Kind Sky", "Mother of Harvests", "the Patient One", "He Who Keeps the Lamp Lit" }), "not loved: not Kind (" + kn + ")");
}
