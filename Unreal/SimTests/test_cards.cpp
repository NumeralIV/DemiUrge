// DEMIURGE - SimTests/test_cards.cpp : the card director (SPEC-MYTHOS 2.2-2.3) and competing prayers (3.10).
// Key: run.ps1 cards (Cards real, every other module a stub; also in all). Owner: cards track (M2).
//
// The cadence tests drive the director with synthetic proposal streams (no other module acts), year by year,
// and check the rules of 2.2 exactly: first year, min gap, the budget, max open, the culture gap, the max gap,
// priority (novelty, kind gap, age fit, phase), ties, merging, the one-year life of a proposal; then the
// lifecycle (answer, lapse, withdraw, look into the heart), save / load of open cards and determinism.
// Tests that need a collaborator to act install stub hooks and skip themselves in `run.ps1 all`.
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

#include "Cards.h"
#include "Mythos.h"
#include "Society.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace dm;
using namespace dmt;
using namespace dmt::stub;

namespace {

// A History world at mythos phase 1 with `n` cultures, each with one settlement (its capital) on land.
std::unique_ptr<World> mythWorld(int n = 6, int phase = 1) {
	auto w = makeWorld({ .L = 4, .seed = 3 });
	w->tune<MythosTune>().phase = phase;
	int cell = 0;
	for (int i = 0; i < n; i++) {
		Culture& c = state::newCulture(*w);
		c.name = "C" + std::to_string(i);
		cell = findLand(*w, [&](int x) { return x > cell; });
		Settlement& s = state::newSettlement(*w, cell, c.id);
		s.pop = 1000;
		s.capital = true;
		w->cultures[size_t(i)].capital = s.id;
		w->cultures[size_t(i)].pop = 1000;
	}
	w->year = 0;
	return w;
}

struct Opening { int id; double year; CardKind kind; int culture; };

// Records every card event of a world.
struct Log {
	std::vector<Opening> opens;
	std::vector<EvCard> all;
	explicit Log(World& w) {
		w.bus.on<EvCard>([this, &w](const EvCard& e) {
			all.push_back(e);
			if (e.stage == "open") {
				CardKind k = CardKind::Count;
				fromStr(e.kind, k);
				opens.push_back({ e.id, w.year, k, e.culture });
			}
		});
	}
};

int openBudgeted(const World& w) {
	int n = 0;
	for (const Card& c : w.mythos.cards)
		if (c.state == uint8_t(CardState::Open) && kCardKinds[size_t(c.kind)].budgeted && !c.data.count("cards_free")) n++;
	return n;
}

cards::Proposal prop(CardKind k, int culture, double severity, int ref0 = -1) {
	cards::Proposal p;
	p.kind = k;
	p.culture = culture;
	p.severity = severity;
	p.ref[0] = ref0;
	return p;
}

// Years from..to (inclusive): each year `each` proposes, then the director runs. Checks the invariants that must
// hold every year whatever the stream.
bool drive(World& w, int from, int to, const std::function<void(World&, int)>& each) {
	const int maxOpen = w.tune<CardsTune>().maxOpen;
	for (int y = from; y <= to; y++) {
		w.year = y;
		each(w, y);
		cards::yearly(w);
		if (!ok(openBudgeted(w) <= maxOpen, "never more than maxOpen open (year " + std::to_string(y) + ")")) return false;
		const double tokens = w.mythos.num.count("cards_tokens") ? w.mythos.num.at("cards_tokens") : 0;
		if (!ok(tokens >= 0 && tokens <= w.tune<CardsTune>().burst + 1e-9, "tokens within 0..burst")) return false;
	}
	return true;
}

std::vector<double> gaps(const std::vector<Opening>& o) {
	std::vector<double> g;
	for (size_t i = 1; i < o.size(); i++) g.push_back(o[i].year - o[i - 1].year);
	return g;
}
double median(std::vector<double> v) {
	if (v.empty()) return 0;
	std::sort(v.begin(), v.end());
	const size_t m = v.size() / 2;
	return v.size() % 2 ? v[m] : 0.5 * (v[m - 1] + v[m]);
}

// The three phase-1 budgeted kinds open in every age (no other module needed to answer them).
const CardKind kStream[3] = { CardKind::DecreeConflict, CardKind::Breach, CardKind::CompetingPrayers };

} // namespace

// ---------------------------------------------------------------- guardrails

DM_TEST("cards: phase 0 is a no-op (no proposal, no card, no counter, nothing opened)") {
	auto w = mythWorld(3, 0);
	const uint32_t before = hash(*w);
	ok(!cards::propose(*w, prop(CardKind::Breach, 0, 0.9)), "propose refused at phase 0");
	for (int y = 1; y <= 600; y++) { w->year = y; cards::yearly(*w); }
	ok(cards::open(*w, prop(CardKind::Presence, 0, 1)) == -1, "open refused at phase 0");
	ok(!cards::answer(*w, 1, 0) && !cards::lookIntoHeart(*w, 1) && cards::options(*w, 1).empty(), "nothing to answer");
	eq(cards::priority(*w, prop(CardKind::Breach, 0, 0.9)), 0.0, "priority 0 at phase 0");
	ok(w->mythos.cards.empty() && w->mythos.num.empty(), "no state written");
	w->year = 0;
	eq(hash(*w), before, "world unchanged");
}

DM_TEST("cards: a phase-1 director touches only mythos.cards / num, never the rng or the world") {
	auto w = mythWorld(4);
	const uint32_t rng = w->rng.s;
	const double awe = w->god.awe;
	std::vector<double> pops;
	for (const Settlement& s : w->settlements) pops.push_back(s.pop);
	drive(*w, 1, 1500, [](World& x, int y) { cards::propose(x, prop(kStream[y % 3], y % 4, 0.6, y % 4)); });
	eq(w->rng.s, rng, "w.rng never drawn");
	eq(w->god.awe, awe, "no Awe spent");
	for (size_t i = 0; i < pops.size(); i++) eq(w->settlements[i].pop, pops[i], "settlements untouched");
	ok(!w->mythos.cards.empty(), "cards opened and lapsed (records)");
	noNaN(*w, "after 1,500 years");
}

// ---------------------------------------------------------------- cadence (2.2) on synthetic streams

DM_TEST("cards: a strong stream opens at 90, 160, 290 and then every 200 years (budget, minGap)") {
	auto w = mythWorld(6);
	Log log(*w);
	ok(drive(*w, 1, 3600, [](World& x, int y) {
		for (int c = 0; c < 6; c++) cards::propose(x, prop(kStream[(y + c) % 3], c, 0.7, c));
	}), "invariants");
	const std::vector<Opening>& o = log.opens;
	if (!ok(o.size() >= 3, "openings")) return;
	eq(o[0].year, 90.0, "first at firstYear");
	eq(o[1].year, 160.0, "second minGap later (burst of 2)");
	eq(o[2].year, 290.0, "third when the token refilled");
	for (size_t i = 3; i < o.size(); i++) eq(o[i].year - o[i - 1].year, 200.0, "then one per targetGap");
	eq(int(o.size()), 19, "19 cards in 3,600 years");
	const std::vector<double> g = gaps(o);
	ok(*std::min_element(g.begin(), g.end()) >= 70, "never two openings within 70 years");
	const double med = median(g);
	ok(med >= 150 && med <= 260, "median gap 150-260 (" + std::to_string(med) + ")");
	int within = 0;
	for (double x : g) within += x <= 420 ? 1 : 0;
	ok(within >= 0.9 * double(g.size()), ">= 90 % of gaps <= 420 years");
	ok(o.size() >= 15 && o.size() <= 28, "15-28 cards per history");
	std::map<double, int> perYear;
	for (const Opening& x : o) perYear[x.year]++;
	for (const auto& [yr, n] : perYear) ok(n == 1, "at most one card a year");
}

DM_TEST("cards: a weak stream opens only at maxGap (420 years after firstYear, then every 420)") {
	auto w = mythWorld(6);
	Log log(*w);
	// severity 0.05 x novelty 1.5 = 0.075 < minPriority 0.25
	drive(*w, 1, 2000, [](World& x, int y) { cards::propose(x, prop(CardKind::Breach, y % 6, 0.05, y % 6)); });
	const std::vector<Opening>& o = log.opens;
	if (!ok(o.size() == 4, "4 forced openings in 2,000 years (" + std::to_string(o.size()) + ")")) return;
	eq(o[0].year, 510.0, "firstYear + maxGap");
	eq(o[1].year, 930.0, "then maxGap");
	eq(o[2].year, 1350.0, "then maxGap");
	eq(o[3].year, 1770.0, "then maxGap");
}

DM_TEST("cards: one culture gets a card at most every cultureGap years unless severity >= 0.8") {
	auto w = mythWorld(2);
	Log log(*w);
	drive(*w, 1, 2500, [](World& x, int y) { cards::propose(x, prop(kStream[y % 3], 0, 0.7, 0)); });
	const std::vector<double> g = gaps(log.opens);
	ok(!g.empty(), "cards opened");
	for (double x : g) ok(x >= 150, "culture gap held (" + std::to_string(x) + ")");
	// severe matters ignore the culture gap (but never the budget or minGap)
	auto w2 = mythWorld(2);
	Log log2(*w2);
	drive(*w2, 1, 2500, [](World& x, int y) { cards::propose(x, prop(kStream[y % 3], 0, 0.85, 0)); });
	const std::vector<double> g2 = gaps(log2.opens);
	ok(!g2.empty() && *std::min_element(g2.begin(), g2.end()) < 150, "severity >= 0.8 opens inside the culture gap");
	for (double x : g2) ok(x >= 70, "minGap still held");
}

DM_TEST("cards: never more than maxOpen budgeted cards open; a closed card frees the slot") {
	auto w = mythWorld(6);
	Log log(*w);
	drive(*w, 1, 400, [](World& x, int y) {          // (V1) 400 years: a director card holds its slot at most maxGap (420) years
		cards::Proposal p = prop(kStream[y % 3], y % 6, 0.7, y % 6);
		p.deadline = 5000;                               // nothing lapses (in these 400 years)
		cards::propose(x, p);
	});
	eq(int(log.opens.size()), 2, "two open, the third waits");
	eq(openBudgeted(*w), 2, "both still open");
	const std::vector<int> ids = cards::openCards(*w);
	ok(cards::withdraw(*w, ids[0]), "withdraw one");
	drive(*w, 401, 401, [](World& x, int) { cards::propose(x, prop(CardKind::Breach, 3, 0.7, 77)); });
	eq(int(log.opens.size()), 3, "the slot is used again");
}

DM_TEST("cards: lapsed cards keep the history going: open, lapse after their window, open again") {
	auto w = mythWorld(6);
	Log log(*w);
	drive(*w, 1, 700, [](World& x, int y) { cards::propose(x, prop(CardKind::DecreeConflict, y % 6, 0.7, y % 6)); });
	int lapsed = 0;
	for (const EvCard& e : log.all) lapsed += e.stage == "lapsed" ? 1 : 0;
	ok(lapsed >= 3, "lapsed cards (" + std::to_string(lapsed) + ")");
	for (const Card& c : w->mythos.cards) {
		if (c.state != uint8_t(CardState::Lapsed)) continue;
		eq(c.deadline - c.opened, w->tune<CardsTune>().deadline[size_t(CardKind::DecreeConflict)], "window from the tune");
		eq(int(c.choice), int(kCardKinds[size_t(CardKind::DecreeConflict)].lapse), "choice = the default lapse");
	}
}

DM_TEST("cards: priority = severity x weight x novelty x kind gap x age fit; dead kinds 0") {
	auto w = mythWorld(3);
	const CardsTune& t = w->tune<CardsTune>();
	w->year = 100;
	near(cards::priority(*w, prop(CardKind::Breach, 0, 0.4)), 0.4 * t.novelty, 1e-12, "a kind never seen: x novelty");
	w->tune<CardsTune>().weight[size_t(CardKind::Breach)] = 2;
	near(cards::priority(*w, prop(CardKind::Breach, 0, 0.4)), 0.8 * t.novelty, 1e-12, "x weight");
	w->tune<CardsTune>().weight[size_t(CardKind::Breach)] = 1;
	ok(cards::propose(*w, prop(CardKind::Breach, 0, 0.9)), "propose");
	cards::yearly(*w);
	ok(cards::openCards(*w).size() == 1, "opened");
	w->year = 300;
	near(cards::priority(*w, prop(CardKind::Breach, 1, 0.4)), 0.4 * t.kindGapFactor, 1e-12, "same kind within kindGap: x 0.5");
	w->year = 100 + t.kindGap;
	near(cards::priority(*w, prop(CardKind::Breach, 1, 0.4)), 0.4, 1e-12, "after kindGap: x 1");
	// age bands: MessengerChoice is Bronze+, FirstDecree Stone-Bronze
	eq(cards::priority(*w, prop(CardKind::MessengerChoice, 0, 0.9)), 0.0, "MessengerChoice in the Stone age: 0");
	w->cultures[0].age = Age::BRONZE;
	ok(cards::priority(*w, prop(CardKind::MessengerChoice, 0, 0.9)) > 0, "MessengerChoice in Bronze");
	w->cultures[0].age = Age::IRON;
	eq(cards::priority(*w, prop(CardKind::FirstDecree, 0, 0.9)), 0.0, "FirstDecree after Bronze: 0");
	// phases: Dispute is v2
	eq(cards::priority(*w, prop(CardKind::Dispute, 1, 0.9)), 0.0, "a v2 kind at phase 1: 0");
	ok(!cards::propose(*w, prop(CardKind::Dispute, 1, 0.9)), "and refused");
	w->tune<MythosTune>().phase = 2;
	ok(cards::priority(*w, prop(CardKind::Dispute, 0, 0.9)) > 0, "live at phase 2 (Bronze+)");
	ok(cards::propose(*w, prop(CardKind::Dispute, 0, 0.9)), "accepted at phase 2");
}

DM_TEST("cards: the best proposal opens; ties by kind order, then culture id") {
	auto w = mythWorld(4);
	Log log(*w);
	w->year = 100;
	cards::propose(*w, prop(CardKind::Judgement, 2, 0.5));
	cards::propose(*w, prop(CardKind::Breach, 3, 0.5));
	cards::propose(*w, prop(CardKind::Breach, 1, 0.5));
	w->cultures[1].age = w->cultures[2].age = w->cultures[3].age = Age::BRONZE;
	cards::yearly(*w);
	if (!ok(log.opens.size() == 1, "one card a year")) return;
	ok(log.opens[0].kind == CardKind::Breach && log.opens[0].culture == 1, "Breach (kind order) of culture 1 (lowest id)");
	// Breach again within kindGap: 0.95 x 0.5 = 0.475 beats a new DecreeConflict 0.3 x 1.5 = 0.45
	w->year = 200;
	cards::propose(*w, prop(CardKind::DecreeConflict, 2, 0.3));
	cards::propose(*w, prop(CardKind::Breach, 3, 0.95));
	cards::yearly(*w);
	ok(log.opens.size() == 2 && log.opens[1].kind == CardKind::Breach && log.opens[1].culture == 3, "higher priority beats kind order");
	// an exact tie of priorities: kind order wins (0.3 x 1.5 = 0.45 = 0.9 x 0.5)
	w->year = 300;
	cards::propose(*w, prop(CardKind::Breach, 0, 0.9));
	cards::propose(*w, prop(CardKind::CompetingPrayers, 0, 0.3));
	cards::propose(*w, prop(CardKind::FirstDecree, 2, 0.3));
	w->cultures[2].age = Age::STONE;
	cards::yearly(*w);
	ok(log.opens.size() == 3 && log.opens[2].kind == CardKind::FirstDecree, "ties: FirstDecree before CompetingPrayers");
}

DM_TEST("cards: identical proposals merge; a proposal lives one year; open matters are not reopened") {
	auto w = mythWorld(3);
	Log log(*w);
	w->year = 100;
	cards::Proposal a = prop(CardKind::Breach, 0, 0.4, 7); a.data["x"] = 1; a.deadline = 5000;
	cards::Proposal b = prop(CardKind::Breach, 0, 0.6, 7); b.data["x"] = 2; b.deadline = 5000;
	cards::Proposal weak = prop(CardKind::Breach, 0, 0.5, 7); weak.deadline = 5000;
	ok(cards::propose(*w, a) && cards::propose(*w, b) && cards::propose(*w, weak), "all accepted");
	cards::yearly(*w);
	if (!ok(log.opens.size() == 1, "one card")) return;
	const Card* c = cards::find(*w, log.opens[0].id);
	ok(c->severity == 0.6 && c->data.at("x") == 2, "the strongest of the identical ones");
	// the same matter while its card is open: not a second card
	w->year = 200;
	cards::propose(*w, prop(CardKind::Breach, 0, 0.9, 7));
	cards::yearly(*w);
	eq(int(log.opens.size()), 1, "held open: not reopened");
	// a proposal made before this year's group is dropped
	auto w2 = mythWorld(3);
	Log log2(*w2);
	w2->year = 99.5;
	cards::propose(*w2, prop(CardKind::Breach, 1, 0.9));
	w2->year = 100;
	cards::yearly(*w2);
	ok(log2.opens.empty(), "last year's proposal was dropped");
	w2->year = 101;
	cards::yearly(*w2);
	ok(log2.opens.empty(), "and never comes back");
}

DM_TEST("cards: malformed proposals are refused") {
	auto w = mythWorld(2);
	cards::Proposal p = prop(CardKind::Breach, 0, std::nan(""));
	ok(!cards::propose(*w, p), "NaN severity");
	ok(!cards::propose(*w, prop(CardKind::Breach, 9, 0.5)), "bad culture");
	p = prop(CardKind::Breach, 0, 0.5); p.settlement = 99; ok(!cards::propose(*w, p), "bad settlement");
	p = prop(CardKind::Breach, 0, 0.5); p.cell = w->n; ok(!cards::propose(*w, p), "bad cell");
	p = prop(CardKind::Breach, 0, 0.5); p.figure = 5; ok(!cards::propose(*w, p), "missing figure");
	p = prop(CardKind::Breach, 0, 0.5); p.kind = CardKind::Count; ok(!cards::propose(*w, p), "bad kind");
	ok(!cards::propose(*w, prop(CardKind::ShapePeople, 0, 1)), "a non-budgeted kind is opened, never proposed");
	ok(cards::open(*w, prop(CardKind::Dispute, 0, 1)) == -1, "open: a v2 kind at phase 1");
	ok(!cards::answer(*w, 12345, 0) && !cards::withdraw(*w, -1) && cards::options(*w, 7).empty(), "bad ids");
	ok(cards::find(*w, 3) == nullptr, "find: none");
}

// ---------------------------------------------------------------- not budgeted: cards::open

DM_TEST("cards: open() is outside the budget: no token, no gap, never blocks maxOpen, re-arming returns the card") {
	auto w = mythWorld(3);
	Log log(*w);
	w->year = 0;
	const int shape = cards::open(*w, prop(CardKind::ShapePeople, 0, 1));
	const int pres = cards::open(*w, prop(CardKind::Presence, 0, 1));
	ok(shape > 0 && pres > shape, "genesis cards open at year 0");
	const Card* c = cards::find(*w, shape);
	eq(c->deadline, -1.0, "until answered");
	eq(int(c->lapse), -1, "ShapePeople's packed lapse (243) goes to the source's own rule");
	eq(c->data.at("cards_lapse"), 243.0, "with its value kept");
	eq(cards::open(*w, prop(CardKind::ShapePeople, 0, 1)), shape, "the same matter: the same card");
	w->year = 300;
	cards::Proposal m = prop(CardKind::MessengerChoice, 1, 0.5, 4);
	w->cultures[1].age = Age::BRONZE;
	const int msg = cards::open(*w, m);
	ok(msg > 0, "a player-armed MessengerChoice");
	eq(cards::find(*w, msg)->deadline, 300.0 + w->tune<CardsTune>().deadline[size_t(CardKind::MessengerChoice)], "its window");
	ok(!w->mythos.num.count("cards_lastOpen"), "no budget used");
	// the budget still opens two cards beside the three free ones
	drive(*w, 301, 600, [](World& x, int y) {
		cards::Proposal p = prop(kStream[y % 3], 2, 0.9, y);
		p.deadline = 5000;
		cards::propose(x, p);
	});
	eq(openBudgeted(*w), 2, "two budgeted cards open beside the free ones");
	ok(cards::find(*w, shape)->state == uint8_t(CardState::Open), "ShapePeople still waits for its answer");
}

// ---------------------------------------------------------------- answer, lapse, withdraw, look

DM_TEST("cards: answer validates, checks Awe, dispatches to the source, pays only when accepted") {
	Guard guard(true);
	if (!stubbed("mythos") || !stubbed("powers")) return;
	auto w = mythWorld(2);
	Log log(*w);
	w->god.awe = 50;
	int handled = 0;
	bool accept = true;
	hooks().mythos_options = [](const World&, int) {
		std::vector<CardOption> v;
		for (int k = 0; k < 3; k++) v.push_back({ k, k == 1 ? 40.0 : 0.0, k != 2, k == 2 ? OptionWhy::NeedsMessenger : OptionWhy::Ok });
		return v;
	};
	hooks().mythos_answer = [&](World&, int, int) { handled++; return accept; };
	hooks().powers_pay = [](World& x, std::string_view what, double amount) {
		if (x.god.awe < amount) { x.bus.emit(EvAweShort{ std::string(what) }); return false; }
		x.god.awe -= amount;
		return true;
	};
	w->year = 0;
	const int id = cards::open(*w, prop(CardKind::Presence, 0, 1));
	ok(!cards::answer(*w, id, 3), "choice out of range");
	ok(!cards::answer(*w, id, 2), "an unavailable answer");
	eq(handled, 0, "the source was not asked");
	accept = false;
	ok(!cards::answer(*w, id, 1), "refused by its source");
	eq(w->god.awe, 50.0, "a refused answer costs nothing");
	accept = true;
	w->god.awe = 30;
	const int asked = handled;
	ok(!cards::answer(*w, id, 1), "short of Awe");
	eq(handled, asked, "not dispatched");
	ok(called("powers_pay") >= 1, "awe:short through powers::pay");
	ok(cards::find(*w, id)->state == uint8_t(CardState::Open), "the card stays open");
	w->god.awe = 50;
	ok(cards::answer(*w, id, 1), "answered");
	eq(w->god.awe, 10.0, "paid 40");
	const Card* c = cards::find(*w, id);
	ok(c->state == uint8_t(CardState::Answered) && c->choice == 1, "state and choice");
	eq(w->god.deeds["card:presence"], 1, "a deed: card:presence");
	ok(!log.all.empty() && log.all.back().stage == "answered" && log.all.back().choice == 1, "card { answered, choice }");
	ok(!cards::answer(*w, id, 0), "an answered card cannot be answered again");
	ok(cards::options(*w, id).empty(), "no options once closed");
}

DM_TEST("cards: a packed ShapePeople choice survives the int8 field") {
	Guard guard(true);
	if (!stubbed("mythos")) return;
	auto w = mythWorld(1);
	Log log(*w);
	hooks().mythos_answer = [](World&, int, int) { return true; };
	const int id = cards::open(*w, prop(CardKind::ShapePeople, 0, 1));
	ok(cards::answer(*w, id, 200), "answered with a packed choice");
	const Card* c = cards::find(*w, id);
	eq(c->data.at("cards_choice"), 200.0, "full value in data");
	eq(log.all.back().choice, 200, "and in the event");
	ok(!cards::answer(*w, cards::open(*w, prop(CardKind::ShapePeople, 0, 1)), 244), "244 is out of range");
}

DM_TEST("cards: lapse dispatches to the source at the deadline; withdraw closes without an answer") {
	Guard guard(true);
	if (!stubbed("doctrine")) return;
	auto w = mythWorld(3);
	Log log(*w);
	int lapses = 0;
	hooks().doctrine_lapse = [&](World& x, int id) {
		lapses++;
		ok(cards::find(x, id)->state == uint8_t(CardState::Open), "the source sees the card still open");
	};
	w->year = 100;
	cards::propose(*w, prop(CardKind::Breach, 0, 0.9));
	cards::yearly(*w);
	const int id = cards::openCards(*w).at(0);
	for (int y = 101; y <= 149; y++) { w->year = y; cards::yearly(*w); }
	eq(lapses, 0, "not before its window ends");
	w->year = 150;
	cards::yearly(*w);
	eq(lapses, 1, "lapsed at opened + 50");
	ok(cards::find(*w, id)->state == uint8_t(CardState::Lapsed), "state lapsed");
	ok(log.all.back().stage == "lapsed" && log.all.back().choice == 0, "card { lapsed, choice = lapse }");
	// withdraw
	w->year = 300;
	cards::propose(*w, prop(CardKind::Breach, 1, 0.9));
	cards::yearly(*w);
	const int id2 = cards::openCards(*w).at(0);
	ok(cards::withdraw(*w, id2) && !cards::withdraw(*w, id2), "withdrawn once");
	ok(log.all.back().stage == "withdrawn", "card { withdrawn }");
	// a dead culture's card is withdrawn by the director
	w->year = 450;
	cards::propose(*w, prop(CardKind::Breach, 2, 0.9));
	cards::yearly(*w);
	const int id3 = cards::openCards(*w).at(0);
	w->cultures[2].dead = true;
	w->year = 451;
	cards::yearly(*w);
	ok(cards::find(*w, id3)->state == uint8_t(CardState::Withdrawn), "its people died: withdrawn");
	eq(lapses, 1, "a withdrawn card does not lapse");
}

DM_TEST("cards: look into the heart pays once, only on a judgement or trial") {
	Guard guard(true);
	if (!stubbed("powers")) return;
	auto w = mythWorld(2);
	w->god.awe = 30;
	int pays = 0;
	hooks().powers_pay = [&](World& x, std::string_view, double amount) {
		if (x.god.awe < amount) return false;
		x.god.awe -= amount; pays++;
		return true;
	};
	w->cultures[0].age = Age::BRONZE;
	w->year = 100;
	cards::propose(*w, prop(CardKind::Judgement, 0, 0.9));
	cards::yearly(*w);
	const int id = cards::openCards(*w).at(0);
	ok(cards::lookIntoHeart(*w, id), "looked");
	eq(w->god.awe, 10.0, "paid 20");
	ok(cards::lookIntoHeart(*w, id), "again");
	eq(pays, 1, "but paid once");
	eq(cards::find(*w, id)->data.at("heartSeen"), 1.0, "data heartSeen");
	w->year = 200;
	cards::propose(*w, prop(CardKind::Breach, 1, 0.9));
	cards::yearly(*w);
	const int other = cards::openCards(*w).back();
	ok(!cards::lookIntoHeart(*w, other), "a breach card has no heart to see");
	w->god.awe = 5;
	w->year = 400;
	cards::propose(*w, prop(CardKind::Judgement, 1, 0.9));
	w->cultures[1].age = Age::BRONZE;
	cards::yearly(*w);
	const int poor = cards::openCards(*w).back();
	ok(!cards::lookIntoHeart(*w, poor), "cannot pay");
	ok(!cards::find(*w, poor)->data.count("heartSeen"), "still veiled");
}

// ---------------------------------------------------------------- withdrawal (3.29)

DM_TEST("cards: while the god has withdrawn: open cards lapse at once, nothing opens, the digest counts") {
	auto w = mythWorld(4);
	Log log(*w);
	w->year = 100;
	cards::propose(*w, prop(CardKind::Breach, 0, 0.9));
	cards::yearly(*w);
	const int id = cards::openCards(*w).at(0);
	const int pres = cards::open(*w, prop(CardKind::Presence, 0, 1));
	w->mythos.withdrawnUntil = 300;
	drive(*w, 101, 299, [](World& x, int y) { cards::propose(x, prop(kStream[y % 3], y % 4, 0.9, y)); });
	ok(cards::find(*w, id)->state == uint8_t(CardState::Lapsed), "the open card lapsed at once");
	ok(cards::find(*w, pres)->state == uint8_t(CardState::Open), "a card without a window waits");
	eq(int(log.opens.size()), 2, "nothing opened while away");
	eq(w->mythos.num.at("cards_digest"), 199.0, "every year's matter counted");
	ok(cards::open(*w, prop(CardKind::Remnant, 0, 1)) == -1, "only the SilenceReport opens while away");
	w->tune<MythosTune>().phase = 3;
	ok(cards::open(*w, prop(CardKind::SilenceReport, -1, 1)) > 0, "the SilenceReport");
	drive(*w, 300, 400, [](World& x, int y) { cards::propose(x, prop(kStream[y % 3], y % 4, 0.9, y)); });
	ok(log.opens.size() >= 4, "cards open again after the silence");
}

// ---------------------------------------------------------------- save / load and determinism

DM_TEST("cards: open cards and the director survive save / load (the loaded world continues identically)") {
	auto run = [](World& x, int from, int to) {
		drive(x, from, to, [](World& y, int yr) {
			cards::Proposal p = prop(kStream[yr % 3], yr % 5, 0.3 + 0.1 * (yr % 7), yr % 5);
			p.data["yr"] = yr;
			cards::propose(y, p);
		});
	};
	auto w = mythWorld(5);
	run(*w, 1, 1200);
	int at = 1200;
	while (cards::openCards(*w).empty() && at < 1600) { at++; run(*w, at, at); }
	if (!ok(!cards::openCards(*w).empty(), "a card is open at the save")) return;
	std::vector<uint8_t> bytes;
	std::string why;
	if (!ok(state::serialize(*w, bytes, &why), "serialize: " + why)) return;
	auto w2 = state::deserialize(bytes, &why);
	if (!ok(w2 != nullptr, "deserialize: " + why)) return;
	watch(*w2);
	w2->tune<MythosTune>().phase = 1;
	eq(int(w2->mythos.cards.size()), int(w->mythos.cards.size()), "same cards");
	ok(w2->mythos.num == w->mythos.num, "same counters");
	run(*w, at + 1, 3000);
	run(*w2, at + 1, 3000);
	if (!eq(int(w2->mythos.cards.size()), int(w->mythos.cards.size()), "same card count after 1,766 more years")) return;
	for (size_t i = 0; i < w->mythos.cards.size(); i++) {
		const Card& a = w->mythos.cards[i];
		const Card& b = w2->mythos.cards[i];
		ok(a.id == b.id && a.kind == b.kind && a.state == b.state && a.culture == b.culture && a.opened == b.opened &&
			a.deadline == b.deadline && a.choice == b.choice && a.ref == b.ref && a.data == b.data, "card " + std::to_string(i));
	}
	ok(w2->mythos.num == w->mythos.num && w2->mythos.nextId == w->mythos.nextId, "same director state");
	std::vector<uint8_t> again;
	ok(state::serialize(*w2, again) && state::deserialize(again) != nullptr, "the continued world saves and loads");
}

DM_TEST("cards: two runs give identical card lists; the list stays within its caps") {
	auto build = [] {
		auto w = mythWorld(6);
		drive(*w, 1, 4500, [](World& x, int y) {
			for (int c = 0; c < 6; c++)
				if ((y * 7 + c * 13) % 5 == 0) cards::propose(x, prop(kStream[(y + c) % 3], c, 0.1 * ((y + c) % 10), (y + c) % 3));
		});
		return w;
	};
	auto a = build();
	auto b = build();
	eq(hash(*a), hash(*b), "identical worlds");
	if (!eq(a->mythos.cards.size(), b->mythos.cards.size(), "same number of cards")) return;
	for (size_t i = 0; i < a->mythos.cards.size(); i++) {
		const Card& x = a->mythos.cards[i];
		const Card& y = b->mythos.cards[i];
		ok(x.id == y.id && x.kind == y.kind && x.state == y.state && x.culture == y.culture && x.opened == y.opened &&
			x.severity == y.severity && x.ref == y.ref, "card " + std::to_string(i));
	}
	ok(!a->mythos.cards.empty(), "cards in the history");
	noNaN(*a, "after 4,500 years");
}

DM_TEST("cards: the list keeps every open card and the last 48 closed (the save bound)") {
	auto w = mythWorld(3);
	w->cultures[1].age = Age::BRONZE;
	w->cultures[2].age = Age::BRONZE;
	std::vector<int> made;
	for (int i = 0; i < 70; i++) {
		w->year = 100 + i;
		// (V1) the last one for another people: arming the Messenger again for one people replaces its open choice
		cards::Proposal p = prop(CardKind::MessengerChoice, i == 69 ? 2 : 1, 0.5, i);
		const int id = cards::open(*w, p);
		made.push_back(id);
		if (i < 68) cards::withdraw(*w, id);
	}
	int nClosed = 0, nOpen = 0;
	std::vector<int> ids;
	for (const Card& c : w->mythos.cards) { (c.state == uint8_t(CardState::Open) ? nOpen : nClosed)++; ids.push_back(c.id); }
	eq(nClosed, 48, "48 closed kept");
	eq(nOpen, 2, "both open ones kept");
	ok(cards::find(*w, made[0]) == nullptr && cards::find(*w, made[19]) == nullptr && cards::find(*w, made[20]) != nullptr,
		"the oldest closed went first");
	ok(std::is_sorted(ids.begin(), ids.end()) && std::adjacent_find(ids.begin(), ids.end()) == ids.end(), "ascending unique ids");
	ok(ids.back() < w->mythos.nextId, "ids below nextId");
	std::vector<uint8_t> bytes;
	std::string why;
	ok(state::serialize(*w, bytes, &why) && state::deserialize(bytes, &why) != nullptr, "the MYTH block validates: " + why);
}

DM_TEST("cards: a card naming a figure that left the registry is cleaned before the save") {
	auto w = mythWorld(2);
	Figure f; f.id = w->mythos.nextId++; f.culture = 0; f.home = 0; f.name = "Ossa";
	w->mythos.figures.push_back(f);
	w->year = 100;
	cards::Proposal p = prop(CardKind::Breach, 0, 0.9);
	p.figure = f.id;
	ok(cards::propose(*w, p), "a proposal naming a figure");
	cards::yearly(*w);
	const int id = cards::openCards(*w).at(0);
	eq(cards::find(*w, id)->figure, f.id, "named");
	w->mythos.figures.clear();
	w->year = 101;
	cards::yearly(*w);
	eq(cards::find(*w, id)->figure, -1, "cleaned");
	std::vector<uint8_t> bytes;
	ok(state::serialize(*w, bytes) && state::deserialize(bytes) != nullptr, "saves and loads");
}

// ---------------------------------------------------------------- competing prayers (3.10)

namespace {

// Two towns of two cultures whose cells are `close` (or far) apart.
struct PrayWorld {
	std::unique_ptr<World> w;
	int a = -1, b = -1;
};
PrayWorld prayWorld(bool close) {
	PrayWorld pw;
	pw.w = mythWorld(2);
	World& w = *pw.w;
	const int ca = w.settlements[0].cell;
	int cb = -1;
	double best = close ? 1e9 : -1;
	for (int c = 0; c < w.n; c++) {
		if (c == ca || state::isSea(w, c)) continue;
		const double ang = w.geo->angle(ca, c);
		if (close && ang > 0.05 && ang < best) { best = ang; cb = c; }
		if (!close && ang > best) { best = ang; cb = c; }
	}
	w.settlements[1].cell = cb;
	pw.a = 0; pw.b = 1;
	return pw;
}

} // namespace

DM_TEST("cards: Rain and Sun within competeAng pair into a card; far apart they do not") {
	PrayWorld pw = prayWorld(true);
	World& w = *pw.w;
	Log log(w);
	w.year = 100;
	w.settlements[0].prayer = Prayer{ PrayerKind::Rain, 95 };
	w.settlements[1].prayer = Prayer{ PrayerKind::Sun, 98 };
	cards::yearly(w);
	if (!ok(log.opens.size() == 1 && log.opens[0].kind == CardKind::CompetingPrayers, "a competing-prayer card")) return;
	const Card* c = cards::find(w, log.opens[0].id);
	ok(c->ref[0] == 0 && c->ref[1] == 1 && c->ref[2] == 0, "ref { rain town, sun town, Rain/Sun }");
	ok(c->culture == 0 && c->other == 1 && c->settlement == 0 && c->cell == w.settlements[0].cell, "who and where");
	eq(c->deadline, 100.0 + 10.0, "the prayers' own window (the rain prayer has 10 years left)");
	ok(c->severity > 0 && c->severity <= 1, "severity 0..1");
	const std::vector<CardOption> opts = cards::options(w, c->id);
	ok(opts.size() == 4 && opts[2].ok && opts[3].ok, "four answers, the middle way available");
	PrayWorld far = prayWorld(false);
	Log flog(*far.w);
	far.w->year = 100;
	far.w->settlements[0].prayer = Prayer{ PrayerKind::Rain, 95 };
	far.w->settlements[1].prayer = Prayer{ PrayerKind::Sun, 98 };
	cards::yearly(*far.w);
	ok(flog.opens.empty(), "too far apart: no pair");
}

DM_TEST("cards: a competing-prayer card is withdrawn when one of its prayers ends") {
	PrayWorld pw = prayWorld(true);
	World& w = *pw.w;
	Log log(w);
	w.year = 100;
	w.settlements[0].prayer = Prayer{ PrayerKind::Rain, 99 };
	w.settlements[1].prayer = Prayer{ PrayerKind::Sun, 99 };
	cards::yearly(w);
	const int id = cards::openCards(w).at(0);
	w.year = 101;
	cards::yearly(w);
	ok(cards::find(w, id)->state == uint8_t(CardState::Open), "still open while both pray");
	w.settlements[1].prayer.reset();
	w.year = 102;
	cards::yearly(w);
	ok(cards::find(w, id)->state == uint8_t(CardState::Withdrawn), "the sun prayer ended: withdrawn");
}

DM_TEST("cards: two capitals at war praying Victory pair; the answers answer one and fail the other") {
	Guard guard(true);
	if (!stubbed("society")) return;
	auto w = mythWorld(2);
	Log log(*w);
	std::vector<std::pair<int, double>> answered, nudged;
	hooks().society_answerPrayer = [&](World&, int sid, double scale) { answered.push_back({ sid, scale }); return true; };
	hooks().society_nudge = [&](World&, int sid, double dDev, double dLove) { nudged.push_back({ sid, dDev }); ok(dDev == dLove, "devotion and love alike"); };
	society::rel(*w, 0, 1)->war = true;
	society::rel(*w, 0, 1)->aggressor = 0;
	w->year = 200;
	w->settlements[0].prayer = Prayer{ PrayerKind::Victory, 199 };
	w->settlements[1].prayer = Prayer{ PrayerKind::Victory, 199 };
	cards::yearly(*w);
	if (!ok(log.opens.size() == 1, "a card")) return;
	const int id = log.opens[0].id;
	eq(cards::find(*w, id)->ref[2], 1, "Victory / Victory");
	ok(cards::answerOwn(*w, id, 0), "stand with A");
	const double penalty = w->tune<SocietyTune>().prayerPenalty;
	const double extra = (w->tune<CardsTune>().competePenalty - 1) * penalty;
	ok(answered.size() == 1 && answered[0].first == 0 && answered[0].second == 1.0, "A answered in full");
	ok(nudged.size() == 1 && nudged[0].first == 1, "B fails");
	near(nudged[0].second, -extra, 1e-12, "with x 1.5 the penalty (the extra half now)");
	const Card* c = cards::find(*w, id);
	ok(c->data.count("arm") && powers::kPowers[int(c->data.at("arm"))].id == std::string("lightning") && c->data.at("armTown") == 1.0,
		"arms a wrath power on B's nearest town");
	answered.clear(); nudged.clear();
	ok(cards::answerOwn(*w, id, 3), "neither");
	ok(answered.empty() && nudged.size() == 2 && std::fabs(nudged[0].second - 0.5 * penalty) < 1e-12, "both lapse at half the penalty");
	answered.clear(); nudged.clear();
	ok(cards::answerOwn(*w, id, 2), "the Omen of Peace");
	ok(answered.empty() && nudged.empty(), "both prayers fail, nothing else");
	eq(powers::kPowers[int(cards::find(*w, id)->data.at("arm"))].id, std::string("peace"), "arms the Omen");
	ok(!cards::answerOwn(*w, id, 4), "no fifth answer");
}

DM_TEST("cards: Rain / Sun middle way, Victory / Peace and Heal / Victory pairs") {
	Guard guard(true);
	if (!stubbed("society")) return;
	std::vector<std::pair<int, double>> answered;
	hooks().society_answerPrayer = [&](World&, int sid, double scale) { answered.push_back({ sid, scale }); return true; };
	{
		PrayWorld pw = prayWorld(true);
		World& w = *pw.w;
		w.year = 100;
		w.settlements[0].prayer = Prayer{ PrayerKind::Rain, 99 };
		w.settlements[1].prayer = Prayer{ PrayerKind::Sun, 99 };
		cards::yearly(w);
		const int id = cards::openCards(w).at(0);
		ok(cards::answerOwn(w, id, 2), "a middle way");
		ok(answered.size() == 2 && answered[0].second == w.tune<CardsTune>().competeMiddleEffect &&
			answered[1].second == w.tune<CardsTune>().competeMiddleEffect, "both answered at half effect");
		const Card* c = cards::find(w, id);
		ok(c->data.at("armAng") == 0.5 && c->data.at("armCost") == w.tune<CardsTune>().competeMiddleCost, "rain at half radius, cost x 1.5");
	}
	{
		auto w = mythWorld(2);
		Log log(*w);
		society::rel(*w, 0, 1)->war = true;
		society::rel(*w, 0, 1)->aggressor = 0;
		w->year = 200;
		w->settlements[0].prayer = Prayer{ PrayerKind::Victory, 199 };
		w->settlements[1].prayer = Prayer{ PrayerKind::Peace, 199 };
		cards::yearly(*w);
		if (!ok(log.opens.size() == 1, "Victory / Peace card")) return;
		const int id = log.opens[0].id;
		eq(cards::find(*w, id)->ref[2], 2, "pair 2");
		const std::vector<CardOption> opts = cards::options(*w, id);
		ok(opts.size() == 4 && !opts[2].ok && opts[2].why == OptionWhy::Unavailable, "no middle way");
		ok(!cards::answerOwn(*w, id, 2), "refused");
		answered.clear();
		ok(cards::answerOwn(*w, id, 1), "the Omen");
		ok(answered.size() == 1 && answered[0].first == 1, "the defender is answered");
	}
	{
		auto w = mythWorld(2);
		Log log(*w);
		society::rel(*w, 0, 1)->war = true;
		w->year = 200;
		w->settlements[0].prayer = Prayer{ PrayerKind::Victory, 199 };
		w->settlements[1].prayer = Prayer{ PrayerKind::Heal, 199 };
		w->settlements[1].plague = 0.3;
		cards::yearly(*w);
		if (!ok(log.opens.size() == 1, "Heal / Victory card")) return;
		const Card* c = cards::find(*w, log.opens[0].id);
		ok(c->ref[0] == 1 && c->ref[1] == 0 && c->ref[2] == 3, "A = the sick city, B = the capital praying Victory over it");
	}
}

DM_TEST("cards: a lapsed competing-prayer card acts only with emergent on (5.1 rule 2)") {
	Guard guard(true);
	if (!stubbed("society")) return;
	int nudges = 0;
	hooks().society_nudge = [&](World&, int, double, double) { nudges++; };
	for (int emergent = 0; emergent < 2; emergent++) {
		PrayWorld pw = prayWorld(true);
		World& w = *pw.w;
		w.tune<MythosTune>().emergent = emergent == 1;
		w.year = 100;
		w.settlements[0].prayer = Prayer{ PrayerKind::Rain, 99 };
		w.settlements[1].prayer = Prayer{ PrayerKind::Sun, 99 };
		cards::yearly(w);
		const int id = cards::openCards(w).at(0);
		nudges = 0;
		for (int y = 101; y <= 114; y++) { w.year = y; cards::yearly(w); }
		ok(cards::find(w, id)->state == uint8_t(CardState::Lapsed), "lapsed at the prayers' end");
		eq(nudges, emergent ? 2 : 0, emergent ? "emergent: both at half the penalty" : "emergent off: nothing moves");
	}
}

DM_TEST("cards: answering a competing-prayer card through cards::answer (the Cards source)") {
	PrayWorld pw = prayWorld(true);
	World& w = *pw.w;
	Log log(w);
	w.year = 100;
	w.settlements[0].prayer = Prayer{ PrayerKind::Rain, 99 };
	w.settlements[1].prayer = Prayer{ PrayerKind::Sun, 99 };
	cards::yearly(w);
	const int id = cards::openCards(w).at(0);
	ok(cards::answer(w, id, 1), "hold the rain");
	ok(cards::find(w, id)->state == uint8_t(CardState::Answered), "answered");
	eq(w.god.deeds["card:competing-prayers"], 1, "deed counted");
	ok(log.all.back().stage == "answered" && log.all.back().kind == "competing-prayers", "card { answered }");
	noNaN(w, "after the answer");
}
