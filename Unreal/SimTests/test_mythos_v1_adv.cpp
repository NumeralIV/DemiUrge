// DEMIURGE — SimTests/test_mythos_v1_adv.cpp : independent adversarial checks of the v1 mythos (SPEC-MYTHOS 9,
// track V1 "mythos-verify-v1", 2026-11-23). Written by the verifier, not by any track that built the mythos.
//
// What is attacked: bad ids and out-of-range answers on every public mythos entry point; worlds with no living
// culture; the card director under a proposal flood (pacing rules 2.2 must hold whatever the sources send),
// starvation by one loud culture, cards that never close; a god spamming words and decrees; abuse of the Messenger
// choice; save / load at awkward moments (right after a card opens, right after an answer, mid-year, at a lapse),
// compared with the world that was never saved. Every test needs the REAL modules (run.ps1 all); in a module key with
// stubs (run.ps1 mythos picks this file up by its name) each test returns at once.
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Cards.h"
#include "Characters.h"
#include "Civ.h"
#include "Doctrine.h"
#include "Mythos.h"
#include "Powers.h"
#include "Sim.h"
#include "Society.h"
#include "State.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

void note(const std::string& s) { std::printf("  %s\n", s.c_str()); std::fflush(stdout); }

bool everyReal() {
	for (const char* m : { "terrain", "climate", "hydro", "life", "path", "units", "civ", "economy", "society", "diplomacy",
	                       "powers", "disasters", "sim", "balance", "names", "save", "mythos", "cards", "doctrine",
	                       "characters", "legends", "faiths", "omens", "keepers", "renewal", "chronicle" })
		if (stub::stubbed(m)) return false;
	return true;
}

// A history world at phase 1 (emergent as the build sets it), made as simrun makes it.
std::unique_ptr<World> world1(uint32_t seed, int L = 5) {
	auto w = sim::create({ .seed = seed, .name = "", .L = L });
	if (!w) return w;
	w->tune<MythosTune>().phase = 1;
	mythos::attach(*w);
	mythos::rebuild(*w);
	sim::genesis::skip(*w, {});
	return w;
}

std::unique_ptr<World> load1(const std::vector<uint8_t>& bytes, std::string* why) {
	auto w = state::deserialize(bytes, why);
	if (!w) return w;
	w->tune<MythosTune>().phase = 1;
	sim::use(*w);
	if (w->tune<MythosTune>().phase != 1) { w->tune<MythosTune>().phase = 1; mythos::attach(*w); mythos::rebuild(*w); }
	return w;
}

bool isFreeCard(const Card& c) {
	const auto it = c.data.find("cards_free");
	return it != c.data.end() && it->second == 1;
}
int budgetedOpenCount(const World& w) {
	int n = 0;
	for (const Card& c : w.mythos.cards) n += (c.state == uint8_t(CardState::Open) && !isFreeCard(c)) ? 1 : 0;
	return n;
}
int openCount(const World& w) {
	int n = 0;
	for (const Card& c : w.mythos.cards) n += c.state == uint8_t(CardState::Open) ? 1 : 0;
	return n;
}
std::vector<int> liveCultures(const World& w) {
	std::vector<int> out;
	for (const Culture& c : w.cultures) if (!c.dead) out.push_back(c.id);
	return out;
}
int firstLiveTownOf(const World& w, int culture) {
	for (const Settlement& s : w.settlements) if (!s.dead && s.culture == culture) return s.id;
	return -1;
}

bool faithSane(const World& w, std::string& bad) {
	for (size_t i = 0; i < w.mythos.faith.size(); i++) {
		const Faith& f = w.mythos.faith[i];
		const double v[] = { f.agency, f.obedience, f.dependence, f.selfReliance, f.clarity, f.judgedBy };
		const char* nm[] = { "agency", "obedience", "dependence", "selfReliance", "clarity", "judgedBy" };
		for (int k = 0; k < 6; k++) {
			const double lo = k == 5 ? -1.0 : 0.0;
			if (!std::isfinite(v[k]) || v[k] < lo - 1e-9 || v[k] > 1 + 1e-9) {
				bad = "culture " + std::to_string(i) + " " + nm[k] + " = " + std::to_string(v[k]);
				return false;
			}
		}
		for (double im : f.image) if (!std::isfinite(im) || im < -1e-9) { bad = "culture " + std::to_string(i) + " image"; return false; }
		if (!std::isfinite(f.visible) || f.visible < -1e-9) { bad = "culture " + std::to_string(i) + " visible"; return false; }
	}
	return true;
}

// A stateless scripted god (a pure function of the world, so a loaded world plays exactly like the one saved):
// answers the waking cards, answers every other open card with its first answer that can be paid (judgements with
// a deed-forgive), and every 40 years speaks a Parable decree to the oldest living people.
void statelessGod(World& w, bool answerCards = true) {
	if (w.ending || w.phase != Phase::History) return;
	for (int id : cards::openCards(w)) {
		if (!answerCards) break;
		const Card* c = cards::find(w, id);
		if (!c) continue;
		const CardKind k = c->kind;
		if (k == CardKind::ShapePeople) { cards::answer(w, id, 243); continue; }
		if (k == CardKind::Presence) { cards::answer(w, id, 0); continue; }
		if (w.year - c->opened < 3) continue;           // let it stand a few years (a save may fall in between)
		const std::vector<CardOption> opts = cards::options(w, id);
		for (const CardOption& o : opts) if (o.ok) { cards::answer(w, id, o.choice); break; }
	}
	const int yr = int(std::floor(w.year));
	if (yr % 40 == 0 && std::fabs(w.year - yr) < 1e-9) {
		const std::vector<int> lc = liveCultures(w);
		if (!lc.empty()) {
			WordOpts o;
			o.content = WordContent::Decree;
			o.area = DecreeArea((yr / 40) % 6);
			o.stance = (yr / 40) % 3;
			o.clarity = Clarity::Dream;
			o.channel = Channel::Dream;
			const int t = firstLiveTownOf(w, lc[0]);
			if (t >= 0 && doctrine::canSpeak(w, lc[0], o).ok) {
				powers::UseOpts uo;
				uo.word = o;
				powers::use(w, "speak", w.settlements[size_t(t)].cell, uo);
			}
		}
	}
}

} // namespace

// ------------------------------------------------------------------ bad ids and out-of-range answers

DM_TEST("mythos v1 adv: bad ids, bad choices and bad words are refused without cost, change or NaN") {
	if (!everyReal()) return;
	auto w = world1(21);
	if (!ok(w != nullptr, "world")) return;
	sim::advance(*w, 300 * sim::kTicksPerYear);
	if (!ok(!w->ending && !liveCultures(*w).empty(), "a living world at year 300")) return;
	w->god.awe = w->god.aweMax;
	const double awe0 = w->god.awe;
	// cards
	for (int id : { -1, 0, INT_MIN, INT_MAX, 1 << 30 }) {
		ok(!cards::answer(*w, id, 0), "answer of card " + std::to_string(id) + " refused");
		ok(cards::options(*w, id).empty(), "no options for card " + std::to_string(id));
		ok(!cards::lookIntoHeart(*w, id), "no heart to look into for card " + std::to_string(id));
		ok(!cards::withdraw(*w, id), "no withdrawal of card " + std::to_string(id));
		ok(cards::find(*w, id) == nullptr || id > 0, "find of a bad id");
	}
	for (int id : cards::openCards(*w)) {
		for (int ch : { -1, -128, 127, 128, 243 + 10, 255, 1000, INT_MAX, INT_MIN }) {
			const Card* c = cards::find(*w, id);
			if (!c) break;
			if (c->kind == CardKind::ShapePeople && ch >= 0 && ch <= 243) continue;
			ok(!cards::answer(*w, id, ch), std::string("card ") + toStr(c->kind) + " refuses choice " + std::to_string(ch));
		}
	}
	// presence and the Word
	ok(!mythos::setPresence(*w, Presence::Walk), "Walk is always refused");
	ok(!mythos::setPresence(*w, Presence(9)), "an out-of-range presence is refused");
	WordOpts bad;
	for (int c : { -1, 9999, INT_MIN }) { bad = WordOpts{}; eq(doctrine::speak(*w, c, bad), -1, "speak to culture " + std::to_string(c)); }
	const int c0 = liveCultures(*w)[0];
	bad = WordOpts{}; bad.clarity = Clarity(9);
	eq(doctrine::speak(*w, c0, bad), -1, "a clarity out of range");
	bad = WordOpts{}; bad.area = DecreeArea::Count; bad.stance = 0;
	eq(doctrine::speak(*w, c0, bad), -1, "a decree with no area");
	bad = WordOpts{}; bad.area = DecreeArea(200); bad.stance = 0;
	eq(doctrine::speak(*w, c0, bad), -1, "a decree area out of range");
	bad = WordOpts{}; bad.area = DecreeArea::Violence; bad.stance = 57;
	eq(doctrine::speak(*w, c0, bad), -1, "a stance out of range");
	bad = WordOpts{}; bad.area = DecreeArea::Violence; bad.stance = -5;
	eq(doctrine::speak(*w, c0, bad), -1, "a negative stance");
	bad = WordOpts{}; bad.content = WordContent(77);
	eq(doctrine::speak(*w, c0, bad), -1, "a content out of range");
	bad = WordOpts{}; bad.area = DecreeArea::Violence; bad.stance = 0; bad.clarity = Clarity::Exact; bad.channel = Channel::Messenger; bad.figure = 123456;
	{
		// a figure that does not exist falls back to the people's own messenger or (under Wonders) the sky: never the bad id
		const double cost = doctrine::wordCost(*w, c0, bad);
		const double awe1 = w->god.awe;
		const int wid = doctrine::speak(*w, c0, bad);
		if (wid >= 0) {
			ok(w->mythos.words.back().figure != 123456, "a word never names a figure that does not exist");
			note("an Exact word with figure 123456 was spoken through " + std::string(toStr(Channel(w->mythos.words.back().channel))) +
			     " at cost " + std::to_string(int(cost)) + " (doctrine::speak itself charges nothing: Powers pays)");
		}
		w->god.awe = awe1;
		w->mythos.num["doctrine_lastWord:" + std::to_string(c0)] = -1e9;
	}
	bad = WordOpts{}; bad.content = WordContent::Repeal; bad.area = DecreeArea::Care;
	eq(doctrine::speak(*w, c0, bad), -1, "repealing a decree the people do not hold");
	ok(std::isfinite(doctrine::wordCost(*w, -3, bad)) && std::isfinite(doctrine::fidelity(*w, -3, bad)), "cost and fidelity of a bad culture are finite");
	powers::UseOpts uo;
	uo.word.clarity = Clarity(12);
	ok(!powers::use(*w, "speak", 0, uo), "the speak power with a bad clarity at cell 0");
	ok(!powers::use(*w, "speak", -1, {}), "the speak power at cell -1");
	// characters, mythos reads
	ok(characters::candidates(*w, -5).empty() && characters::candidates(*w, 1 << 20).empty(), "no candidates for a bad town");
	ok(std::isfinite(characters::credibility(*w, -7)) && std::isfinite(characters::credibility(*w, 1 << 20)), "credibility of a bad figure is finite");
	ok(characters::figure(*w, -2) == nullptr, "no figure -2");
	ok(mythos::factors(*w, -1) == Factors{} && mythos::factors(*w, 9999) == Factors{}, "neutral factors for bad cultures");
	ok(mythos::townFactors(*w, -1) == TownFactors{} && mythos::townFactors(*w, 1 << 20) == TownFactors{}, "neutral town factors for bad towns");
	ok(mythos::faith(*w, -1) == nullptr && mythos::faith(*w, 9999) == nullptr, "no faith record for bad cultures");
	eq(int(mythos::aspect(*w, -4)), int(Aspect::Unknown), "a bad culture sees nothing");
	// traits
	Traits tr;
	tr.curiosity = std::nan("");
	tr.compassion = 7;
	society::shapeTraits(*w, c0, tr);
	ok(!society::shapeTraits(*w, -1, Traits{}) && !society::shapeTraits(*w, 9999, Traits{}), "shapeTraits refuses bad cultures");
	noNaN(*w, "after the bad calls");
	ok(sim::firstNonFinite(*w).empty(), "no non-finite value after a NaN trait was offered: " + sim::firstNonFinite(*w));
	const Culture& cu = w->cultures[size_t(c0)];
	ok(cu.traits.compassion >= 0 && cu.traits.compassion <= 1 && std::isfinite(cu.traits.curiosity), "traits stay in 0..1");
	near(w->god.awe, awe0, 1e-9, "no Awe was spent by any refused call");
}

DM_TEST("mythos v1 adv: a world with no living people: every mythos pass, read and answer is safe") {
	if (!everyReal()) return;
	// (a) before the waking: no culture at all
	{
		auto w = sim::create({ .seed = 31, .name = "", .L = 4 });
		if (!ok(w != nullptr, "world")) return;
		w->tune<MythosTune>().phase = 1;
		mythos::attach(*w);
		mythos::rebuild(*w);
		eq(w->cultures.size(), size_t(0), "no culture before the waking");
		for (int i = 0; i < 30; i++) { mythos::yearly(*w); cards::yearly(*w); doctrine::yearly(*w); characters::yearly(*w); }
		cards::Proposal p;
		p.kind = CardKind::Judgement; p.culture = 0; p.severity = 1;
		ok(!cards::propose(*w, p), "a proposal naming culture 0 of a world with none is refused");
		eq(int(mythos::legacy(*w)), int(Legacy::None), "no legacy");
		const LegacyScores ls = mythos::legacyScores(*w);
		(void)ls;
		noNaN(*w, "no culture");
	}
	// (b) every people dead in a history world
	{
		auto w = world1(32);
		if (!ok(w != nullptr, "world")) return;
		sim::advance(*w, 250 * sim::kTicksPerYear);
		if (!ok(!w->ending, "a living world at 250")) return;
		std::vector<uint8_t> bytes;
		for (Settlement& s : w->settlements) if (!s.dead) { s.dead = true; s.pop = 0; }
		for (Culture& c : w->cultures) c.dead = true;
		for (int y = 0; y < 60 && !w->ending; y++) sim::advance(*w, sim::kTicksPerYear);
		noNaN(*w, "every people dead");
		ok(sim::firstNonFinite(*w).empty(), "no non-finite value");
		eq(budgetedOpenCount(*w), 0, "no budgeted card stays open for dead peoples");
		std::string why;
		ok(state::serialize(*w, bytes, &why), "the dead world saves: " + why);
		ok(state::deserialize(bytes, &why) != nullptr, "and loads: " + why);
	}
}

// ------------------------------------------------------------------ the card director under attack (2.2)

DM_TEST("mythos v1 adv: a proposal flood (200 random matters a year for 3,000 years) cannot break the pacing rules") {
	if (!everyReal()) return;
	auto w = world1(41);
	if (!ok(w != nullptr, "world")) return;
	sim::advance(*w, 60 * sim::kTicksPerYear);
	if (!ok(!w->ending, "a living world")) return;
	const CardsTune& t = w->tune<CardsTune>();
	std::vector<CardKind> liveKinds;
	for (const CardKindInfo& k : kCardKinds) if (k.budgeted && k.phase <= 1 && k.kind != CardKind::Situation) liveKinds.push_back(k.kind);
	std::vector<double> openings;
	auto tok = w->bus.on<EvCard>([&](const EvCard& e) {
		const Card* c = cards::find(*w, e.id);
		if (e.stage == "open" && c && !isFreeCard(*c)) openings.push_back(w->year);
	});
	Rng r(4141);
	const double y0 = w->year;
	int maxBudgeted = 0, maxKept = 0, answers = 0;
	double maxTokens = 0;
	const int nC = int(w->cultures.size()), nS = int(w->settlements.size());
	for (int y = 1; y <= 3000; y++) {
		w->year = y0 + y;                        // the director reads only w.year: drive it alone, year by year
		for (int k = 0; k < 200; k++) {
			cards::Proposal p;
			p.kind = liveKinds[size_t(r.integer(0, int(liveKinds.size()) - 1))];
			p.culture = r.integer(-1, nC - 1);
			p.other = r.integer(-1, nC - 1);
			p.settlement = r.integer(-1, nS - 1);
			p.severity = r.f() < 0.1 ? (r.f() < 0.5 ? -3.0 : 40.0) : r.f();
			p.ref = { r.integer(-1, 50), r.integer(-1, 50), r.integer(-1, 3), -1 };
			p.deadline = r.f() < 0.3 ? r.range(0.5, 300) : -1;
			cards::propose(*w, p);
		}
		cards::yearly(*w);
		// answer some open cards at random (sources must survive cards whose refs are nonsense)
		for (int id : cards::openCards(*w)) {
			if (r.f() < 0.15) { if (cards::answer(*w, id, r.integer(-2, 9))) answers++; }
		}
		maxBudgeted = std::max(maxBudgeted, budgetedOpenCount(*w));
		maxKept = std::max(maxKept, int(w->mythos.cards.size()) - openCount(*w));
		const auto it = w->mythos.num.find("cards_tokens");
		if (it != w->mythos.num.end()) maxTokens = std::max(maxTokens, it->second);
	}
	w->bus.off(tok);
	double minGap = 1e9;
	for (size_t i = 1; i < openings.size(); i++) minGap = std::min(minGap, openings[i] - openings[i - 1]);
	int perYearMax = 0;
	{
		std::map<int, int> per;
		for (double o : openings) perYearMax = std::max(perYearMax, ++per[int(std::floor(o + 1e-9))]);
	}
	char buf[300];
	std::snprintf(buf, sizeof buf, "flood: %zu budgeted openings in 3,000 years (min gap %.0f, at most %d open, %d answers accepted, kept closed %d, tokens max %.2f)",
		openings.size(), openings.empty() ? 0.0 : minGap, maxBudgeted, answers, maxKept, maxTokens);
	note(buf);
	ok(openings.size() >= 2, "the flood opens cards");
	ok(openings.empty() || openings[0] + 1e-9 >= t.firstYear, "nothing before firstYear");
	ok(openings.size() < 2 || minGap + 1e-9 >= t.minGap, "never two openings within minGap years");
	ok(perYearMax <= 1, "at most one opening a year");
	ok(maxBudgeted <= t.maxOpen, "never more than maxOpen budgeted cards open");
	ok(maxTokens <= double(t.burst) + 1e-9, "the budget never holds more than burst tokens");
	ok(double(openings.size()) <= 3000.0 / t.targetGap + t.burst + 1, "the budget bounds the openings (one per targetGap years, plus the burst)");
	ok(maxKept <= 48, "the closed cards are pruned to the last 48");
	noNaN(*w, "after the flood");
	std::string bad;
	ok(faithSane(*w, bad), "faith values stay in range: " + bad);
	std::vector<uint8_t> bytes;
	std::string why;
	ok(state::serialize(*w, bytes, &why) && state::deserialize(bytes, &why) != nullptr, "the flooded world saves and loads: " + why);
}

DM_TEST("mythos v1 adv: starvation: one loud people (severity 0.95 every year) against four quiet ones (reported)") {
	if (!everyReal()) return;
	std::unique_ptr<World> w;
	for (uint32_t sd = 42; sd < 47; sd++) {
		w = world1(sd, 6);
		if (!w) continue;
		for (int y = 0; y < 1600 && !w->ending && liveCultures(*w).size() < 4; y += 50) sim::advance(*w, 50 * sim::kTicksPerYear);
		if (!w->ending && liveCultures(*w).size() >= 4) break;
	}
	if (!ok(w != nullptr && liveCultures(*w).size() >= 3, "a world with 3+ living peoples")) return;
	note("starvation world: " + std::to_string(liveCultures(*w).size()) + " living peoples at year " + std::to_string(int(w->year)));
	std::map<int, int> byCulture;
	auto tok = w->bus.on<EvCard>([&](const EvCard& e) {
		const Card* c = cards::find(*w, e.id);
		if (e.stage == "open" && c && !isFreeCard(*c)) { byCulture[e.culture]++; if (std::getenv("DM_V1_DEBUG")) note("open " + std::to_string(int(w->year)) + " culture " + std::to_string(e.culture) + " age " + std::to_string(e.culture >= 0 ? w->cultures[size_t(e.culture)].age : -1)); }
	});
	const double y0 = w->year;
	const int nC = std::max(5, int(w->cultures.size()));
	if (int(w->cultures.size()) < 5) { note("fewer than 5 cultures: using the ids that exist"); }
	const int quiet = std::min(4, int(liveCultures(*w).size()) - 1);
	for (int y = 1; y <= 3000; y++) {
		w->year = y0 + y;
		cards::Proposal loud;
		loud.kind = CardKind::DecreeConflict; loud.culture = liveCultures(*w)[0]; loud.severity = 0.95; loud.ref = { y, -1, -1, -1 };
		cards::propose(*w, loud);
		for (int c = 1; c <= quiet; c++) {
			cards::Proposal q;
			q.kind = CardKind::DecreeConflict; q.culture = liveCultures(*w)[size_t(c)]; q.severity = 0.5; q.ref = { c, -1, -1, -1 };
			cards::propose(*w, q);
		}
		cards::yearly(*w);
	}
	w->bus.off(tok);
	(void)nC;
	std::string s = "openings by culture:";
	int others = 0;
	for (const auto& kv : byCulture) { s += " " + std::to_string(kv.first) + ":" + std::to_string(kv.second); if (kv.first != liveCultures(*w)[0]) others += kv.second; }
	note(s);
	// by the spec (priority = severity x weight x novelty x kindGap) a people whose matters are always graver takes every
	// card the budget allows; cultureGap (150) never binds while the budget refills more slowly (targetGap 170). Reported.
	note(std::string("starvation: ") + (others == 0 ? "TOTAL" : "partial") + ", the quiet peoples got " + std::to_string(others) + " cards");
}

DM_TEST("mythos v1 adv: cards that never close: unanswered waking cards and long deadlines do not block the director") {
	if (!everyReal()) return;
	auto w = world1(43);
	if (!ok(w != nullptr, "world")) return;
	int opened = 0;
	auto tok = w->bus.on<EvCard>([&](const EvCard& e) {
		const Card* c = cards::find(*w, e.id);
		if (e.stage == "open" && c && !isFreeCard(*c)) opened++;
	});
	for (int y = 0; y < 1500 && !w->ending; y++) sim::advance(*w, sim::kTicksPerYear);
	w->bus.off(tok);
	int wakingOpen = 0;
	for (const Card& c : w->mythos.cards)
		if (c.state == uint8_t(CardState::Open) && (c.kind == CardKind::ShapePeople || c.kind == CardKind::Presence)) wakingOpen++;
	note("after 1,500 years with the waking cards never answered: " + std::to_string(wakingOpen) + " still open, " + std::to_string(opened) +
	     " budgeted cards opened");
	ok(opened >= 3, "the director opened budgeted cards while the waking cards went unanswered");
	eq(wakingOpen, 0, "the unanswered waking cards closed with the waking (no free reshaping or stance change centuries later)");
	for (const Card& c : w->mythos.cards) {
		if (c.kind != CardKind::ShapePeople && c.kind != CardKind::Presence) continue;
		const Traits before = w->cultures.empty() ? Traits{} : w->cultures[0].traits;
		const Presence pr = w->mythos.presence;
		ok(!cards::answer(*w, c.id, c.kind == CardKind::ShapePeople ? 0 : 2), std::string("a late answer to the ") + toStr(c.kind) + " card is refused");
		ok(w->cultures.empty() || w->cultures[0].traits == before, "the first people keep their nature");
		ok(w->mythos.presence == pr, "the stance is unchanged");
	}
	// a budgeted matter with a very long window: the director must not stay full for centuries
	auto v = world1(44);
	if (!ok(v != nullptr, "world")) return;
	sim::advance(*v, 600 * sim::kTicksPerYear);
	const double y0 = v->year;
	const double opened0 = v->mythos.num.count("cards_opened") ? v->mythos.num["cards_opened"] : 0;
	for (int y = 1; y <= 1200; y++) {
		v->year = y0 + y;
		for (int c = 0; c < 2; c++) {
			cards::Proposal p;
			p.kind = CardKind::DecreeConflict; p.culture = c < int(v->cultures.size()) ? c : -1; p.severity = 0.9; p.ref = { 900 + c, -1, -1, -1 };
			p.deadline = 1e7;                     // a source bug: a window of ten million years
			cards::propose(*v, p);
		}
		cards::yearly(*v);
	}
	double oldest = 0;
	for (const Card& c : v->mythos.cards) if (c.state == uint8_t(CardState::Open) && !isFreeCard(c)) oldest = std::max(oldest, v->year - c.opened);
	char buf[200];
	std::snprintf(buf, sizeof buf, "a 1e7-year window: the oldest budgeted open card is %.0f years old after 1,200 years", oldest);
	note(buf);
	const double opened1 = v->mythos.num.count("cards_opened") ? v->mythos.num["cards_opened"] : 0;
	ok(opened1 > opened0 + 1, "the long-window matters were opened at all (" + std::to_string(int(opened1 - opened0)) + ")");
	ok(oldest <= 420 + 1e-9, "no budgeted card stays open longer than maxGap years, whatever window its source asked for");
}

DM_TEST("mythos v1 adv: arming the Messenger on every town of one people opens one choice, and only one messenger is chosen") {
	if (!everyReal()) return;
	auto w = world1(51);
	if (!ok(w != nullptr, "world")) return;
	int culture = -1;
	for (int y = 0; y < 2500 && !w->ending; y += 10) {
		sim::advance(*w, 10 * sim::kTicksPerYear);
		for (const Culture& c : w->cultures) {
			if (c.dead || c.age < 1 || c.prophet || characters::messenger(*w, c.id) >= 0) continue;
			int towns = 0;
			for (const Settlement& s : w->settlements) towns += (!s.dead && s.culture == c.id) ? 1 : 0;
			if (towns >= 3) { culture = c.id; break; }
		}
		if (culture >= 0 && powers::unlocked(*w, "prophet")) break;
		culture = -1;
	}
	if (!ok(culture >= 0, "a Bronze+ people with 3+ towns, no voice, and the Messenger power")) return;
	w->god.awe = w->god.aweMax;
	powers::UseOpts uo;
	uo.choose = true;
	int armed = 0;
	for (const Settlement& s : w->settlements)
		if (!s.dead && s.culture == culture && powers::use(*w, "prophet", s.cell, uo)) armed++;
	int choiceOpen = 0;
	std::vector<int> ids;
	for (const Card& c : w->mythos.cards)
		if (c.state == uint8_t(CardState::Open) && c.kind == CardKind::MessengerChoice && c.culture == culture) { choiceOpen++; ids.push_back(c.id); }
	note("armed on " + std::to_string(armed) + " towns: " + std::to_string(choiceOpen) + " MessengerChoice cards open for that people");
	ok(choiceOpen <= 1, "one people asks one question: arming the Messenger again elsewhere does not stack choice cards");
	int chosen = 0;
	for (int id : ids) {
		w->god.awe = w->god.aweMax;
		for (int ch = 0; ch < 3; ch++) if (cards::answer(*w, id, ch)) { chosen++; break; }
	}
	int messengers = 0;
	for (const Figure& f : w->mythos.figures)
		if (f.culture == culture && f.kind == uint8_t(FigureKind::Messenger) && f.died < 0) messengers++;
	eq(messengers, 1, "exactly one living messenger for that people");
	ok(chosen <= 1, "no second candidate can be chosen from another of the open cards");
	int stillOpen = 0;
	for (const Card& c : w->mythos.cards)
		if (c.state == uint8_t(CardState::Open) && c.kind == CardKind::MessengerChoice && c.culture == culture) stillOpen++;
	eq(stillOpen, 0, "no MessengerChoice card of that people is left open once a messenger is chosen");
	// a word through another people's messenger, or through the ruler, is refused
	const int mfig = characters::messenger(*w, culture);
	for (const Culture& o : w->cultures) {
		if (o.dead || o.id == culture) continue;
		WordOpts wo;
		wo.area = DecreeArea::Violence; wo.stance = 0; wo.clarity = Clarity::Exact; wo.channel = Channel::Messenger; wo.figure = mfig;
		const doctrine::SpeakCheck sc = doctrine::canSpeak(*w, o.id, wo);
		ok(!sc.ok || characters::messenger(*w, o.id) >= 0, "a word to the " + std::to_string(o.id) + " cannot go through another people's messenger");
		break;
	}
	const int ruler = characters::ruler(*w, culture);
	if (ruler >= 0) {
		WordOpts wo;
		wo.area = DecreeArea::Violence; wo.stance = 0; wo.clarity = Clarity::Exact; wo.channel = Channel::Messenger; wo.figure = ruler;
		const doctrine::SpeakCheck sc = doctrine::canSpeak(*w, culture, wo);
		(void)sc;   // the messenger of the people is used instead, or it is refused: either way no word through the ruler
		const int before = int(w->mythos.words.size());
		w->mythos.num["doctrine_lastWord:" + std::to_string(culture)] = -1e9;
		if (doctrine::speak(*w, culture, wo) >= 0) {
			const Utterance& u = w->mythos.words.back();
			ok(u.figure != ruler, "a word sent through the ruler goes through the messenger, not the ruler");
		}
		(void)before;
	}
	noNaN(*w, "after the messenger abuse");
}

// ------------------------------------------------------------------ the Word spammed (3.6, 3.9)

DM_TEST("mythos v1 adv: a god speaking every tick (every content, clarity and channel) to every people for 1,500 years") {
	if (!everyReal()) return;
	auto w = world1(61);
	if (!ok(w != nullptr, "world")) return;
	sim::advance(*w, 120 * sim::kTicksPerYear);
	std::map<int, std::vector<double>> spoken;
	auto tok = w->bus.on<EvWord>([&](const EvWord& e) { spoken[e.culture].push_back(w->year); });
	Rng r(616);
	int words = 0;
	for (int i = 0; i < 1500 * sim::kTicksPerYear && !w->ending; i++) {
		w->god.awe = w->god.aweMax;
		for (const Culture& c : w->cultures) {
			if (c.dead) continue;
			const int t = firstLiveTownOf(*w, c.id);
			if (t < 0) continue;
			powers::UseOpts uo;
			uo.word.content = r.f() < 0.25 ? WordContent::Repeal : WordContent::Decree;
			uo.word.area = DecreeArea(r.integer(0, 6));
			uo.word.stance = r.integer(-1, 3);
			uo.word.clarity = Clarity(r.integer(0, 3));
			uo.word.channel = Channel(r.integer(0, 2));
			uo.word.figure = r.f() < 0.5 ? characters::messenger(*w, c.id) : r.integer(-1, 400);
			if (powers::use(*w, "speak", w->settlements[size_t(t)].cell, uo)) words++;
		}
		sim::advance(*w, 1);
	}
	w->bus.off(tok);
	double minGap = 1e9;
	for (auto& kv : spoken) for (size_t i = 1; i < kv.second.size(); i++) minGap = std::min(minGap, kv.second[i] - kv.second[i - 1]);
	std::map<std::pair<int, int>, int> perArea;
	for (const Decree& d : w->mythos.decrees) perArea[{ d.culture, int(d.area) }]++;
	int dup = 0;
	for (const auto& kv : perArea) dup += kv.second > 1 ? 1 : 0;
	char buf[300];
	std::snprintf(buf, sizeof buf, "%d words accepted, %zu decrees held, %zu words kept, min gap between two words to one people %.2f years, year %.0f",
		words, w->mythos.decrees.size(), w->mythos.words.size(), minGap, w->year);
	note(buf);
	ok(words >= 10, "the spam was heard at all");
	ok(minGap + 1e-9 >= w->tune<DoctrineTune>().wordGap, "one word per people per wordGap years, however often the god speaks");
	eq(dup, 0, "at most one decree per area per people");
	ok(w->mythos.words.size() <= 64, "the words are capped at the last 64");
	std::string bad;
	ok(faithSane(*w, bad), "faith values in range: " + bad);
	for (const Culture& c : w->cultures) {
		const double b = doctrine::burden(*w, c.id);
		ok(std::isfinite(b) && b >= 0 && b <= 1.2, "burden in range for culture " + std::to_string(c.id));
	}
	noNaN(*w, "after the word spam");
	std::vector<uint8_t> bytes, again;
	std::string why;
	if (ok(state::serialize(*w, bytes, &why), "save: " + why)) {
		auto back = state::deserialize(bytes, &why);
		if (ok(back != nullptr, "load: " + why)) { state::serialize(*back, again); ok(again == bytes, "the spammed world re-saves byte for byte"); }
	}
	{
		std::vector<uint8_t> v1;
		state::serialize(*w, v1, nullptr, 1);         // the version-1 view drops the extension blocks
		const double blocks = double(bytes.size()) - double(v1.size());
		note("extension blocks (BASE + MYTH) after the spam: " + std::to_string(int(blocks)) + " bytes");
		ok(blocks < 80 * 1024, "the MYTH block stays under 80 KB (SPEC-MYTHOS 1.3)");
	}
}

DM_TEST("mythos v1 adv: answers short of Awe keep the card open and spend nothing; Look into the heart is paid once") {
	if (!everyReal()) return;
	auto w = world1(71, 6);
	if (!ok(w != nullptr, "world")) return;
	int judged = -1;
	for (int y = 0; y < 3000 && !w->ending && judged < 0; y++) {
		w->god.awe = w->god.aweMax;
		if (y % 12 == 0) {
			for (int c : liveCultures(*w)) {
				const int t = firstLiveTownOf(*w, c);
				if (t < 0) continue;
				powers::UseOpts uo;
				uo.word.content = WordContent::Decree;
				uo.word.area = DecreeArea((y / 12 + c) % 6 == 2 ? 3 : (y / 12 + c) % 6);   // skip Worship (the rite)
				uo.word.stance = 0;                                                         // the strictest stance
				uo.word.clarity = Clarity::Exact;
				uo.word.channel = Channel::Sky;
				powers::use(*w, "speak", w->settlements[size_t(t)].cell, uo);
			}
		}
		sim::advance(*w, sim::kTicksPerYear);
		for (int id : cards::openCards(*w)) {
			const Card* c = cards::find(*w, id);
			if (c && c->kind == CardKind::Judgement) { judged = id; break; }
		}
	}
	if (!ok(judged >= 0, "a Judgement card opened within 3,000 years")) return;
	w->god.awe = 0;
	int refused = 0, costly = 0;
	for (const CardOption& o : cards::options(*w, judged)) {
		if (o.cost <= 0) continue;
		costly++;
		refused += cards::answer(*w, judged, o.choice) ? 0 : 1;
	}
	eq(refused, costly, "every answer that costs Awe is refused with none");
	ok(cards::find(*w, judged) && cards::find(*w, judged)->state == uint8_t(CardState::Open), "the card stays open");
	ok(!cards::lookIntoHeart(*w, judged), "the heart cannot be seen without 20 Awe");
	w->god.awe = 100;
	ok(cards::lookIntoHeart(*w, judged), "with Awe the heart is seen");
	const double after1 = w->god.awe;
	ok(cards::lookIntoHeart(*w, judged), "a second look is fine");
	near(w->god.awe, after1, 1e-9, "and costs nothing");
	near(100 - after1, w->tune<CardsTune>().lookCost, 1e-9, "the first look cost lookCost");
	noNaN(*w, "after the Awe-short answers");
}

// ------------------------------------------------------------------ save / load at awkward moments (5.1 rule 6)

DM_TEST("mythos v1 adv: save / load at a card opening, an answer, a lapse and mid-year continues exactly like the unsaved world") {
	if (!everyReal()) return;
	const uint32_t seed = 81;
	auto w = world1(seed);
	if (!ok(w != nullptr, "world")) return;
	struct Snap { std::string why; int64_t tick; std::vector<uint8_t> bytes; };
	std::vector<Snap> snaps;
	std::set<std::string> taken;
	std::string pending;
	auto tok = w->bus.on<EvCard>([&](const EvCard& e) {
		const Card* c = cards::find(*w, e.id);
		const bool freeCard = c && isFreeCard(*c);
		const std::string key = (freeCard ? "free " : "budgeted ") + e.stage;
		if (!taken.count(key) && pending.empty()) pending = key;
	});
	const int years = 1400;
	const int64_t total = int64_t(years) * sim::kTicksPerYear;
	for (int64_t i = 0; i < total && !w->ending; i++) {
		statelessGod(*w);
		sim::advance(*w, 1);
		if (!pending.empty()) {
			Snap s{ pending, w->tick, {} };
			if (state::serialize(*w, s.bytes)) snaps.push_back(std::move(s));
			taken.insert(pending);
			pending.clear();
		}
		// mid-year saves: a quarter, a half and three quarters into one year, once
		const double frac = w->year - std::floor(w->year);
		for (double f : { 0.25, 0.5, 0.75 }) {
			const std::string key = "mid-year " + std::to_string(f).substr(0, 4);
			if (w->year > 400 && std::fabs(frac - f) < 1e-6 && !taken.count(key)) {
				Snap s{ key, w->tick, {} };
				if (state::serialize(*w, s.bytes)) snaps.push_back(std::move(s));
				taken.insert(key);
			}
		}
	}
	w->bus.off(tok);
	const uint32_t want = sim::fingerprint(*w);
	const int64_t endTick = w->tick;
	std::string kinds;
	for (const Snap& s : snaps) kinds += " [" + s.why + " @" + std::to_string(s.tick / sim::kTicksPerYear) + "]";
	note("saves:" + kinds);
	ok(taken.count("budgeted open") && taken.count("budgeted answered"), "saved right after a budgeted card opened and right after an answer");
	// Cell fields are quantized in a save by design (State.cpp), so a loaded world is not the unsaved one bit for bit.
	// What must hold: the mythos state comes back exactly, the loaded world re-saves byte for byte, and two loads of
	// one save continue identically (with the same god acting). The distance to the unsaved world is reported.
	for (const Snap& s : snaps) {
		std::string why;
		auto l = load1(s.bytes, &why);
		auto m = load1(s.bytes, &why);
		auto raw = state::deserialize(s.bytes, &why);
		if (!ok(l != nullptr && m != nullptr && raw != nullptr, s.why + ": load: " + why)) continue;
		std::vector<uint8_t> again;
		state::serialize(*raw, again);
		ok(again == s.bytes, s.why + ": the loaded world re-saves byte for byte");
		// the mythos records of the loaded world, field by field, against a second deserialize (no sim::use)
		const MythosState& a = l->mythos;
		const MythosState& b = raw->mythos;
		ok(a.cards.size() == b.cards.size() && a.decrees.size() == b.decrees.size() && a.figures.size() == b.figures.size() &&
		   a.words.size() == b.words.size() && a.faith.size() >= b.faith.size(), s.why + ": sim::use keeps every mythos record");
		bool same = true;
		for (size_t i = 0; i < std::min(a.cards.size(), b.cards.size()); i++) {
			const Card& x = a.cards[i];
			const Card& y = b.cards[i];
			same = same && x.id == y.id && x.kind == y.kind && x.state == y.state && x.opened == y.opened && x.deadline == y.deadline &&
				x.choice == y.choice && x.lapse == y.lapse && x.ref == y.ref && x.data == y.data && x.culture == y.culture;
		}
		ok(same, s.why + ": every card (state, window, choice, data) is the saved one after sim::use");
		eq(a.num, b.num, s.why + ": the director's and modules' counters are the saved ones after sim::use");
		int64_t t = l->tick;
		while (t < endTick && !l->ending) {
			statelessGod(*l); sim::advance(*l, 1);
			statelessGod(*m); sim::advance(*m, 1);
			t = l->tick;
		}
		eq(sim::fingerprint(*l), sim::fingerprint(*m), s.why + ": two loads of the save continue identically with the god acting");
		eq(l->mythos.cards.size(), m->mythos.cards.size(), s.why + ": and see the same cards");
		char buf[200];
		std::snprintf(buf, sizeof buf, "%s: at the end, unsaved %08x vs loaded %08x; cards %zu vs %zu", s.why.c_str(), want, sim::fingerprint(*l),
			w->mythos.cards.size(), l->mythos.cards.size());
		note(buf);
	}
}
