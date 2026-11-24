// DEMIURGE - Sim/Cards.cpp : the card director (SPEC-MYTHOS 2.2-2.3) and the competing prayers (3.10).
// Owner: cards track (M2, key "cards"). Header Cards.h (frozen by M0); stub SimTests/stubs/Cards_stub.cpp.
//
// What lives where:
//   mythos.cards                 the card records (open ones, and the last kClosedKept closed ones)
//   mythos.num "cards_..."       the director's persisted counters (budget tokens, last openings per kind and
//                                per culture, totals, the withdrawal digest)
//   w.scratch<CardsScratch>()    this year's proposals (made and consumed inside one yearly group)
//
// The director (2.2), once a year in cards::yearly:
//   1. refill the budget (one token per targetGap years, at most `burst`);
//   2. close what has to close: dead cultures' cards are withdrawn, due cards lapse (all lapsable cards at once
//      while the god has withdrawn, 3.29), stale competing-prayer cards are withdrawn;
//   3. pair this year's competing prayers (3.10) into proposals of its own;
//   4. while withdrawn: count this year's matters into the digest and open nothing;
//   5. else open at most one budgeted proposal: year >= firstYear, a token, year - lastOpen >= minGap, fewer than
//      maxOpen budgeted cards open, the culture's last card >= cultureGap years ago (unless severity >=
//      cultureGapSeverity), and priority >= minPriority (or year - lastOpen >= maxGap: the best one even if weak).
//      Priority = severity x weight[kind] x novelty x kindGap factor x ageFit; ties by kind order, then culture,
//      then ref[0], then proposal order;
//   6. drop the proposals and keep the closed cards to the last kClosedKept.
//
// Decisions where the spec leaves the C++ open (reported in docs/handoff/done-ue/mythos-m2.built.txt):
//   - A proposal is consumed only by the cards::yearly of the same year (propose stamps w.year): a proposal made
//     between two yearly groups (an answer handler) is dropped, so a save never loses a live proposal.
//   - Only budgeted cards count toward maxOpen; cards opened with cards::open (genesis, player-armed, reports)
//     carry data["cards_free"] = 1 and never block the budget. A budgeted kind with no deadline from the tune or
//     the proposal lapses after kFallbackDeadline years; a non-budgeted one waits for an answer (deadline -1).
//   - cards::answer validates the choice against the source's options (ok, or only short of Awe), checks the
//     Awe, calls the source's answer, and pays through powers::pay only when the source accepted (a refused
//     answer costs nothing); short of Awe it calls powers::pay so the god hears awe:short, and the card stays open.
//   - Card::choice / lapse are int8: a packed ShapePeople choice (0..243) is stored as 127 with the full value in
//     data["cards_choice"]; a default lapse above 127 is stored as -1 (the source's own rule) with the value in
//     data["cards_lapse"]. Events always carry the full value.
//   - Every lapse is dispatched to its source; the sources gate their own emergent-family lapse effects
//     (MythosTune::emergent). Cards gates its own: a lapsed competing-prayer card does nothing unless emergent.
//   - Competing prayers: every answer costs 0 Awe at the card; the act a side answer arms (rain, lightning, heal,
//     the Omen of Peace) is recorded in the card's data (arm, armCell, armAng, armCost) for the client and is paid
//     when cast. Answering a side answers its prayer through society::answerPrayer (full, or competeMiddleEffect
//     for the middle way) and makes the other's an unanswered prayer with competePenalty x the penalty (the extra
//     (competePenalty - 1) x SocietyTune::prayerPenalty through society::nudge now; Society's own penalty follows
//     when the prayer runs out); "neither" gives both back half the penalty now (they lapse at half). Envy and the
//     Mystery impression are not Cards' to write: Society's favour sums and Mythos (reading card { answered }) own them.
#include "Cards.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "Characters.h"
#include "Doctrine.h"
#include "Faiths.h"
#include "Keepers.h"
#include "Mythos.h"
#include "Omens.h"
#include "Powers.h"
#include "Renewal.h"
#include "Society.h"

namespace dm::cards {
namespace {

constexpr double kEps = 1e-9;
constexpr int kClosedKept = 48;                  // closed cards kept (1.2: [open 2, closed last 48])
constexpr double kFallbackDeadline = 60;         // a budgeted card the tune and the source left without a window
constexpr int kInt8Max = 127;

// Competing-prayer pairs (3.10): Card::ref = { a, b, pair, -1 }.
enum class Pair : int { RainSun = 0, VictoryVictory = 1, VictoryPeace = 2, HealVictory = 3 };
// The source's severity scale for a pair: base + kShareK x the mean stake (town share of its people, x 1.5 for
// a capital), at most 1. A war between two praying capitals weighs more than two towns arguing about rain.
constexpr double kPairBase[4] = { 0.30, 0.50, 0.45, 0.45 };
constexpr double kShareK = 0.5, kCapitalK = 1.5;

// ---------------------------------------------------------------- scratch (this year's proposals)

struct Entry {
	Proposal p;
	double year = 0;
	int order = 0;
};
struct CardsScratch {
	std::vector<Entry> props;
	int order = 0;
};

// ---------------------------------------------------------------- the source table (no globals: const data)

struct SourceFns {
	bool (*answer)(World&, int, int);
	void (*lapse)(World&, int);
	std::vector<CardOption> (*options)(const World&, int);
};
const SourceFns kSourceFns[] = {
	{ &mythos::answer, &mythos::lapse, &mythos::options },             // Mythos
	{ &answerOwn, &lapseOwn, &optionsOwn },                            // Cards (CompetingPrayers)
	{ &doctrine::answer, &doctrine::lapse, &doctrine::options },       // Doctrine
	{ &characters::answer, &characters::lapse, &characters::options }, // Characters
	{ &faiths::answer, &faiths::lapse, &faiths::options },             // Faiths
	{ &omens::answer, &omens::lapse, &omens::options },                // Omens
	{ &keepers::answer, &keepers::lapse, &keepers::options },          // Keepers
	{ &renewal::answer, &renewal::lapse, &renewal::options },          // Renewal
	{ nullptr, nullptr, nullptr },                                     // Situation (gameplay-v4: not built)
};
static_assert(sizeof(kSourceFns) / sizeof(kSourceFns[0]) == size_t(enumCount<CardSource>()), "one row per card source");

const SourceFns& sourceOf(CardKind k) { return kSourceFns[size_t(kCardKinds[size_t(k)].source)]; }

// ---------------------------------------------------------------- small helpers

double fin(double v, double def = 0) { return std::isfinite(v) ? v : def; }

double num(const World& w, const std::string& key, double def) {
	const auto it = w.mythos.num.find(key);
	return it == w.mythos.num.end() || !std::isfinite(it->second) ? def : it->second;
}
bool hasNum(const World& w, const std::string& key) { return w.mythos.num.count(key) > 0; }
void setNum(World& w, const std::string& key, double v) { w.mythos.num[key] = fin(v); }
void addNum(World& w, const std::string& key, double v) { setNum(w, key, num(w, key, 0) + v); }

std::string kindKey(CardKind k) { return std::string("cards_kind_") + toStr(k); }
std::string cultureKey(int c) { return "cards_culture_" + std::to_string(c); }

int phaseOf(const World& w) { return w.tune<MythosTune>().phase; }
bool validKind(CardKind k) { return uint8_t(k) < uint8_t(CardKind::Count); }
bool live(const World& w, CardKind k) {
	const int ph = phaseOf(w);
	return ph >= 1 && validKind(k) && kCardKinds[size_t(k)].phase <= ph;
}
bool withdrawn(const World& w) {
	return w.tune<CardsTune>().withdrawnHold > 0 && std::isfinite(w.mythos.withdrawnUntil) && w.mythos.withdrawnUntil > w.year + kEps;
}

int findIndex(const World& w, int id) {
	for (size_t i = 0; i < w.mythos.cards.size(); i++) if (w.mythos.cards[i].id == id) return int(i);
	return -1;
}
bool isOpen(const Card& c) { return c.state == uint8_t(CardState::Open); }
bool isFree(const Card& c) { return c.data.count("cards_free") > 0; }
bool countsOpen(const Card& c) { return isOpen(c) && kCardKinds[size_t(c.kind)].budgeted && !isFree(c); }

bool cultureOk(const World& w, int c) { return c >= -1 && c < int(w.cultures.size()); }
bool settlementOk(const World& w, int s) { return s >= -1 && s < int(w.settlements.size()); }
bool figureOk(const World& w, int f) {
	if (f == -1) return true;
	for (const Figure& g : w.mythos.figures) if (g.id == f) return true;
	return false;
}
bool wellFormed(const World& w, const Proposal& p) {
	if (!validKind(p.kind) || !std::isfinite(p.severity) || !std::isfinite(p.deadline)) return false;
	if (!cultureOk(w, p.culture) || !cultureOk(w, p.other) || !settlementOk(w, p.settlement)) return false;
	if (p.cell != -1 && !w.valid(p.cell)) return false;
	if (p.lapse < -2) return false;
	return figureOk(w, p.figure);
}

int ageOf(const World& w, int culture) {
	if (culture >= 0 && culture < int(w.cultures.size())) return w.cultures[size_t(culture)].age;
	return w.age;
}
bool ageFits(const World& w, const Proposal& p) {
	const CardKindInfo& info = kCardKinds[size_t(p.kind)];
	const int a = ageOf(w, p.culture);
	return a >= info.minAge && a <= info.maxAge;
}

double priorityOf(const World& w, const Proposal& p) {
	if (!validKind(p.kind) || !live(w, p.kind)) return 0;
	const CardsTune& t = w.tune<CardsTune>();
	const double sev = std::clamp(fin(p.severity), 0.0, 1.0);
	const double weight = std::max(0.0, fin(t.weight[size_t(p.kind)]));
	const std::string kk = kindKey(p.kind);
	double factor = 1;
	if (!hasNum(w, kk)) factor *= fin(t.novelty, 1);
	else if (w.year - num(w, kk, -1e18) < t.kindGap - kEps) factor *= fin(t.kindGapFactor, 1);
	const double fit = ageFits(w, p) ? 1.0 : 0.0;
	return std::max(0.0, fin(sev * weight * factor * fit));
}

// An open card already holds this matter (same kind, culture and ref[0]).
bool heldOpen(const World& w, const Proposal& p) {
	for (const Card& c : w.mythos.cards)
		if (isOpen(c) && c.kind == p.kind && c.culture == p.culture && c.ref[0] == p.ref[0]) return true;
	return false;
}
int heldOpenId(const World& w, const Proposal& p) {
	for (const Card& c : w.mythos.cards)
		if (isOpen(c) && c.kind == p.kind && c.culture == p.culture && c.ref[0] == p.ref[0]) return c.id;
	return -1;
}

void emitCard(World& w, int id, CardKind k, const char* stage, int culture, int choice) {
	w.bus.emit(EvCard{ id, toStr(k), stage, culture, choice });
}

// Keep every open card and the last kClosedKept closed ones (oldest closed removed first).
void prune(World& w) {
	std::vector<Card>& cs = w.mythos.cards;
	int closed = 0;
	for (const Card& c : cs) closed += isOpen(c) ? 0 : 1;
	if (closed <= kClosedKept) return;
	std::vector<int> closedIds;
	for (const Card& c : cs) if (!isOpen(c)) closedIds.push_back(c.id);
	std::sort(closedIds.begin(), closedIds.end());
	const size_t drop = size_t(closed - kClosedKept);
	std::vector<int> gone(closedIds.begin(), closedIds.begin() + std::ptrdiff_t(drop));
	cs.erase(std::remove_if(cs.begin(), cs.end(), [&](const Card& c) {
		return !isOpen(c) && std::binary_search(gone.begin(), gone.end(), c.id);
	}), cs.end());
}

// Create the card (2.2 "Opening"). budgeted: a director opening (token, gaps); else cards::open's.
int openCard(World& w, const Proposal& p, bool budgeted) {
	const CardsTune& t = w.tune<CardsTune>();
	const CardKindInfo& info = kCardKinds[size_t(p.kind)];
	Card c;
	c.id = std::max(1, w.mythos.nextId);
	w.mythos.nextId = c.id + 1;
	c.kind = p.kind;
	c.state = uint8_t(CardState::Open);
	c.culture = p.culture; c.other = p.other; c.settlement = p.settlement; c.figure = p.figure; c.cell = p.cell;
	c.opened = w.year;
	c.severity = std::clamp(fin(p.severity), 0.0, 1.0);
	double years = p.deadline > 0 ? p.deadline : fin(t.deadline[size_t(p.kind)]);
	if (!(years > 0) && info.budgeted && budgeted) years = kFallbackDeadline;
	// (V1) a director card never holds a budget slot for longer than maxGap years, whatever window its source asked
	// for (the v1 windows are 15-80 years; a source bug asking for 1e7 years would otherwise block maxOpen for good)
	if (budgeted && years > 0 && t.maxGap > 0 && years > t.maxGap) years = t.maxGap;
	c.deadline = years > 0 ? w.year + years : -1;
	c.choice = -1;
	c.ref = p.ref;
	c.data = p.data;
	const int lapse = p.lapse == -2 ? info.lapse : p.lapse;
	if (lapse > kInt8Max) { c.lapse = -1; c.data["cards_lapse"] = lapse; }
	else c.lapse = int8_t(std::max(-1, lapse));
	if (!budgeted) c.data["cards_free"] = 1;
	const int id = c.id;
	const int culture = c.culture;
	w.mythos.cards.push_back(std::move(c));
	setNum(w, kindKey(p.kind), w.year);
	addNum(w, "cards_opened", 1);
	if (budgeted) {
		setNum(w, "cards_tokens", std::max(0.0, num(w, "cards_tokens", 0) - 1));
		setNum(w, "cards_lastOpen", w.year);
		if (culture >= 0) setNum(w, cultureKey(culture), w.year);
	}
	emitCard(w, id, p.kind, "open", culture, -1);
	prune(w);
	return id;
}

// The event value of a closed card's choice (full value, beyond int8).
int choiceValue(const Card& c, const char* key, int8_t field) {
	const auto it = c.data.find(key);
	return it != c.data.end() && std::isfinite(it->second) ? int(it->second) : int(field);
}

void lapseCard(World& w, int id) {
	int i = findIndex(w, id);
	if (i < 0 || !isOpen(w.mythos.cards[size_t(i)])) return;
	const CardKind k = w.mythos.cards[size_t(i)].kind;
	const SourceFns& fns = sourceOf(k);
	if (fns.lapse) fns.lapse(w, id);                     // the source applies its default (it may withdraw it)
	i = findIndex(w, id);
	if (i < 0) return;
	Card& c = w.mythos.cards[size_t(i)];
	if (!isOpen(c)) return;
	c.state = uint8_t(CardState::Lapsed);
	c.choice = c.lapse;
	const int culture = c.culture;
	const int choice = choiceValue(c, "cards_lapse", c.lapse);
	addNum(w, "cards_lapsed", 1);
	emitCard(w, id, k, "lapsed", culture, choice);
}

void refill(World& w) {
	const CardsTune& t = w.tune<CardsTune>();
	const double burst = std::max(0, t.burst);
	if (!hasNum(w, "cards_tokens") || !hasNum(w, "cards_tokensYear")) {
		setNum(w, "cards_tokens", burst);
		setNum(w, "cards_tokensYear", w.year);
		return;
	}
	const double dt = w.year - num(w, "cards_tokensYear", w.year);
	double tokens = num(w, "cards_tokens", 0);
	if (dt > 0 && t.targetGap > 0) tokens += dt / t.targetGap;
	setNum(w, "cards_tokens", std::clamp(tokens, 0.0, burst));
	setNum(w, "cards_tokensYear", w.year);
}

// Figures can leave the registry (Characters' cap): a card never names a missing one (the save would refuse it).
void cleanRefs(World& w) {
	if (w.mythos.cards.empty()) return;
	std::vector<int> figs;
	figs.reserve(w.mythos.figures.size());
	for (const Figure& f : w.mythos.figures) figs.push_back(f.id);
	std::sort(figs.begin(), figs.end());
	for (Card& c : w.mythos.cards)
		if (c.figure != -1 && !std::binary_search(figs.begin(), figs.end(), c.figure)) c.figure = -1;
}

// ---------------------------------------------------------------- competing prayers (3.10)

bool aliveTown(const World& w, int sid) {
	return sid >= 0 && sid < int(w.settlements.size()) && !w.settlements[size_t(sid)].dead;
}
bool prays(const World& w, int sid, PrayerKind k) {
	return aliveTown(w, sid) && w.settlements[size_t(sid)].prayer && w.settlements[size_t(sid)].prayer->kind == k;
}
bool aliveCulture(const World& w, int c) { return c >= 0 && c < int(w.cultures.size()) && !w.cultures[size_t(c)].dead; }
int capitalOf(const World& w, int c) {
	if (!aliveCulture(w, c)) return -1;
	const int cap = w.cultures[size_t(c)].capital;
	return aliveTown(w, cap) && w.settlements[size_t(cap)].culture == c ? cap : -1;
}
double cultureShare(const World& w, int sid) {
	const Settlement& s = w.settlements[size_t(sid)];
	double total = 0;
	if (aliveCulture(w, s.culture)) total = fin(w.cultures[size_t(s.culture)].pop);
	if (!(total > 0)) {
		total = 0;
		for (const Settlement& o : w.settlements) if (!o.dead && o.culture == s.culture) total += std::max(0.0, fin(o.pop));
	}
	const double share = total > 0 ? std::max(0.0, fin(s.pop)) / total : 0;
	return std::clamp(share * (s.capital ? kCapitalK : 1.0), 0.0, kCapitalK);
}
double townAngle(const World& w, int a, int b) {
	if (!w.geo) return 1e9;
	const int ca = w.settlements[size_t(a)].cell, cb = w.settlements[size_t(b)].cell;
	if (!w.valid(ca) || !w.valid(cb)) return 1e9;
	return fin(w.geo->angle(ca, cb), 1e9);
}
// The live town of culture c praying k with the most people (ties: lower id), or -1.
int bestPraying(const World& w, int c, PrayerKind k) {
	int best = -1;
	double bestPop = -1;
	for (const Settlement& s : w.settlements) {
		if (s.dead || s.culture != c || !s.prayer || s.prayer->kind != k) continue;
		const double pop = fin(s.pop);
		if (pop > bestPop + kEps) { best = s.id; bestPop = pop; }
	}
	return best;
}
// The live town of culture c nearest to settlement `to` (ties: lower id), or -1.
int nearestTown(const World& w, int c, int to) {
	int best = -1;
	double bestAng = 1e18;
	for (const Settlement& s : w.settlements) {
		if (s.dead || s.culture != c) continue;
		const double a = townAngle(w, s.id, to);
		if (a < bestAng - kEps) { best = s.id; bestAng = a; }
	}
	return best;
}
bool pairAvailable(int pair, int choice) {
	if (choice < 0 || choice > 3) return false;
	if (choice == 2) return pair == int(Pair::RainSun) || pair == int(Pair::VictoryVictory);
	return pair >= 0 && pair <= 3;
}
bool inOpenCompete(const World& w, int sid) {
	for (const Card& c : w.mythos.cards)
		if (isOpen(c) && c.kind == CardKind::CompetingPrayers && (c.ref[0] == sid || c.ref[1] == sid)) return true;
	return false;
}
// Withdraw competing-prayer cards whose prayers ended (answered, lapsed, or their town gone).
void refreshCompete(World& w) {
	std::vector<int> stale;
	for (const Card& c : w.mythos.cards) {
		if (!isOpen(c) || c.kind != CardKind::CompetingPrayers || !c.data.count("pair")) continue;
		const auto still = [&](int sid, const char* kindKeyName, const char* sinceKey) {
			if (!aliveTown(w, sid) || !w.settlements[size_t(sid)].prayer) return false;
			const Prayer& p = *w.settlements[size_t(sid)].prayer;
			const auto ik = c.data.find(kindKeyName);
			const auto is = c.data.find(sinceKey);
			if (ik == c.data.end() || is == c.data.end()) return false;
			return int(p.kind) == int(ik->second) && std::fabs(fin(p.since) - is->second) < 1e-6;
		};
		if (!still(c.ref[0], "kindA", "sinceA") || !still(c.ref[1], "kindB", "sinceB")) stale.push_back(c.id);
	}
	for (int id : stale) withdraw(w, id);
}
void proposePair(World& w, Pair pair, int a, int b) {
	if (a < 0 || b < 0 || a == b || inOpenCompete(w, a) || inOpenCompete(w, b)) return;
	const Settlement& sa = w.settlements[size_t(a)];
	const Settlement& sb = w.settlements[size_t(b)];
	if (!sa.prayer || !sb.prayer) return;
	const CardsTune& t = w.tune<CardsTune>();
	const SocietyTune& st = w.tune<SocietyTune>();
	Proposal p;
	p.kind = CardKind::CompetingPrayers;
	p.culture = cultureOk(w, sa.culture) ? sa.culture : -1;
	p.other = cultureOk(w, sb.culture) ? sb.culture : -1;
	p.settlement = a;
	p.cell = w.valid(sa.cell) ? sa.cell : -1;
	p.ref = { a, b, int(pair), -1 };
	const double stake = 0.5 * (cultureShare(w, a) + cultureShare(w, b));
	p.severity = std::clamp(kPairBase[int(pair)] + kShareK * stake, 0.0, 1.0);
	// the prayers' own window (at most CardsTune::deadline), never under a year
	const double remA = fin(st.prayerYears) - (w.year - fin(sa.prayer->since));
	const double remB = fin(st.prayerYears) - (w.year - fin(sb.prayer->since));
	double years = fin(t.deadline[size_t(CardKind::CompetingPrayers)]);
	if (!(years > 0)) years = fin(st.prayerYears, 15);
	p.deadline = std::max(1.0, std::min({ years, remA, remB }));
	p.data["pair"] = int(pair);
	p.data["kindA"] = int(sa.prayer->kind); p.data["kindB"] = int(sb.prayer->kind);
	p.data["sinceA"] = fin(sa.prayer->since); p.data["sinceB"] = fin(sb.prayer->since);
	propose(w, p);
}
// Pair this year's prayers that cannot both be granted (3.10), ascending ids throughout.
void pairPrayers(World& w) {
	const CardsTune& t = w.tune<CardsTune>();
	const int nS = int(w.settlements.size()), nC = int(w.cultures.size());
	// Rain / Sun: each rain town with its nearest sun town within competeAng
	for (int a = 0; a < nS; a++) {
		if (!prays(w, a, PrayerKind::Rain)) continue;
		int best = -1;
		double bestAng = 1e18;
		for (int b = 0; b < nS; b++) {
			if (b == a || !prays(w, b, PrayerKind::Sun)) continue;
			const double ang = townAngle(w, a, b);
			if (ang <= t.competeAng + kEps && ang < bestAng - kEps) { best = b; bestAng = ang; }
		}
		if (best >= 0) proposePair(w, Pair::RainSun, a, best);
	}
	// the wars: Victory / Victory, Victory / Peace, Heal / Victory
	for (int ca = 0; ca < nC; ca++) {
		const int capA = capitalOf(w, ca);
		if (capA < 0 || !prays(w, capA, PrayerKind::Victory)) continue;
		for (int cb = 0; cb < nC; cb++) {
			if (cb == ca || !aliveCulture(w, cb) || !society::atWar(w, ca, cb)) continue;
			const int capB = capitalOf(w, cb);
			if (cb > ca && capB >= 0 && prays(w, capB, PrayerKind::Victory)) proposePair(w, Pair::VictoryVictory, capA, capB);
			const Relation* r = society::peek(w, ca, cb);
			if (r && r->aggressor == ca) {
				const int peace = bestPraying(w, cb, PrayerKind::Peace);
				if (peace >= 0) proposePair(w, Pair::VictoryPeace, capA, peace);
			}
			const int sick = bestPraying(w, cb, PrayerKind::Heal);
			if (sick >= 0 && w.settlements[size_t(sick)].plague > 0) proposePair(w, Pair::HealVictory, sick, capA);
		}
	}
}

void arm(Card& c, const char* power, int sid, const World& w, double angMul, double costMul) {
	const powers::PowerDef* def = powers::byId(power);
	if (!def || !aliveTown(w, sid)) return;
	c.data["arm"] = double(def - powers::kPowers);
	c.data["armTown"] = sid;
	c.data["armCell"] = w.settlements[size_t(sid)].cell;
	c.data["armAng"] = angMul;
	c.data["armCost"] = costMul;
}

// ---------------------------------------------------------------- the withdrawal digest (3.29)

void digest(World& w, const std::vector<Entry>& props) {
	const double until = w.mythos.withdrawnUntil;
	if (std::fabs(num(w, "cards_digestUntil", -1e18) - until) > kEps) {
		for (auto it = w.mythos.num.begin(); it != w.mythos.num.end();) {
			if (it->first.rfind("cards_digest", 0) == 0) it = w.mythos.num.erase(it);
			else ++it;
		}
		setNum(w, "cards_digestUntil", until);
	}
	for (const Entry& e : props) {
		if (!live(w, e.p.kind)) continue;
		addNum(w, "cards_digest", 1);
		addNum(w, std::string("cards_digest_") + toStr(e.p.kind), 1);
	}
}

} // namespace

// ================================================================ the API

bool propose(World& w, const Proposal& p) {
	if (!validKind(p.kind) || !live(w, p.kind) || !kCardKinds[size_t(p.kind)].budgeted) return false;
	if (!wellFormed(w, p)) return false;
	CardsScratch& sc = w.scratch<CardsScratch>();
	for (Entry& e : sc.props) {
		if (e.p.kind == p.kind && e.p.culture == p.culture && e.p.ref[0] == p.ref[0]) {
			// identical matters merge: this year's stronger one stands
			if (std::fabs(e.year - w.year) > kEps || p.severity > e.p.severity + kEps) { e.p = p; e.year = w.year; }
			return true;
		}
	}
	sc.props.push_back(Entry{ p, w.year, sc.order++ });
	return true;
}

void yearly(World& w) {
	if (phaseOf(w) < 1) return;
	const CardsTune& t = w.tune<CardsTune>();
	CardsScratch& sc = w.scratch<CardsScratch>();
	refill(w);
	cleanRefs(w);
	// 1. close: dead cultures' cards, due cards (every lapsable card while withdrawn)
	{
		std::vector<int> gone, due;
		const bool away = withdrawn(w);
		for (const Card& c : w.mythos.cards) {
			if (!isOpen(c)) continue;
			if (c.culture >= 0 && c.culture < int(w.cultures.size()) && w.cultures[size_t(c.culture)].dead) gone.push_back(c.id);
			else if (c.deadline >= 0 && (away || w.year + kEps >= c.deadline)) due.push_back(c.id);
		}
		std::sort(gone.begin(), gone.end());
		std::sort(due.begin(), due.end());
		for (int id : gone) withdraw(w, id);
		for (int id : due) lapseCard(w, id);
	}
	// 2. competing prayers (the director is their source)
	if (live(w, CardKind::CompetingPrayers)) {
		refreshCompete(w);
		pairPrayers(w);
	}
	// 3. this year's proposals
	std::vector<Entry> props;
	for (const Entry& e : sc.props) if (std::fabs(e.year - w.year) <= kEps) props.push_back(e);
	sc.props.clear();
	sc.order = 0;
	if (withdrawn(w)) {
		digest(w, props);
		prune(w);
		return;
	}
	// 4. the gates of an opening
	const bool hasLast = hasNum(w, "cards_lastOpen");
	const double lastOpen = num(w, "cards_lastOpen", 0);
	int openNow = 0;
	for (const Card& c : w.mythos.cards) openNow += countsOpen(c) ? 1 : 0;
	const bool gates = w.year + kEps >= t.firstYear && num(w, "cards_tokens", 0) + 1e-6 >= 1 &&
		(!hasLast || w.year - lastOpen + kEps >= t.minGap) && openNow < t.maxOpen;
	if (gates && !props.empty()) {
		const double since = w.year - (hasLast ? lastOpen : t.firstYear);
		const bool forced = since + kEps >= t.maxGap;
		int best = -1;
		double bestP = 0;
		for (size_t i = 0; i < props.size(); i++) {
			const Proposal& p = props[i].p;
			if (!live(w, p.kind) || heldOpen(w, p)) continue;
			if (p.culture >= 0) {
				const std::string ck = cultureKey(p.culture);
				if (hasNum(w, ck) && w.year - num(w, ck, 0) + kEps < t.cultureGap && p.severity + kEps < t.cultureGapSeverity) continue;
			}
			const double pr = priorityOf(w, p);
			if (!(pr > 0)) continue;
			bool better = best < 0 || pr > bestP + kEps;
			if (!better && std::fabs(pr - bestP) <= kEps) {
				const Proposal& q = props[size_t(best)].p;
				if (p.kind != q.kind) better = uint8_t(p.kind) < uint8_t(q.kind);
				else if (p.culture != q.culture) better = p.culture < q.culture;
				else if (p.ref[0] != q.ref[0]) better = p.ref[0] < q.ref[0];
				else better = props[i].order < props[size_t(best)].order;
			}
			if (better) { best = int(i); bestP = pr; }
		}
		if (best >= 0 && (bestP + kEps >= t.minPriority || forced)) openCard(w, props[size_t(best)].p, true);
	}
	prune(w);
}

int open(World& w, const Proposal& p) {
	if (!validKind(p.kind) || !live(w, p.kind) || !wellFormed(w, p)) return -1;
	if (withdrawn(w) && p.kind != CardKind::SilenceReport) return -1;
	const int held = heldOpenId(w, p);
	if (held >= 0) return held;                          // re-arming shows the same card
	// (V1) one people asks one question: arming the Messenger on another town of the same people replaces the armed
	// choice that is still open there (the latest arming wins) instead of stacking one card per town
	if (p.kind == CardKind::MessengerChoice && p.culture >= 0) {
		std::vector<int> stale;
		for (const Card& c : w.mythos.cards)
			if (isOpen(c) && isFree(c) && c.kind == CardKind::MessengerChoice && c.culture == p.culture) stale.push_back(c.id);
		for (int id : stale) withdraw(w, id);
	}
	return openCard(w, p, false);
}

bool answer(World& w, int id, int choice) {
	if (phaseOf(w) < 1) return false;
	const int i = findIndex(w, id);
	if (i < 0) return false;
	const CardKind k = w.mythos.cards[size_t(i)].kind;
	if (!isOpen(w.mythos.cards[size_t(i)]) || !live(w, k)) return false;
	const CardKindInfo& info = kCardKinds[size_t(k)];
	if (choice < 0 || choice >= info.answers) return false;
	const SourceFns& fns = sourceOf(k);
	if (!fns.answer) return false;
	double cost = 0;
	if (fns.options) {
		const std::vector<CardOption> opts = fns.options(w, id);
		for (const CardOption& o : opts) {
			if (o.choice != choice) continue;
			if (!o.ok && o.why != OptionWhy::Awe) return false;
			cost = std::max(0.0, fin(o.cost));
			break;
		}
	}
	const std::string what = std::string("card:") + toStr(k);
	bool paid = false;
	if (cost > 0 && !(fin(w.god.awe) + kEps >= cost)) {
		if (!powers::pay(w, what, cost)) return false;   // awe:short; the card stays open
		paid = true;
	}
	if (!fns.answer(w, id, choice)) return false;        // refused by its source: nothing paid
	if (cost > 0 && !paid && !powers::pay(w, what, cost)) addNum(w, "cards_unpaid", cost);
	const int j = findIndex(w, id);
	int culture = -1;
	if (j >= 0) {
		Card& c = w.mythos.cards[size_t(j)];
		culture = c.culture;
		if (isOpen(c)) {
			c.state = uint8_t(CardState::Answered);
			if (choice > kInt8Max) { c.choice = int8_t(kInt8Max); c.data["cards_choice"] = choice; }
			else c.choice = int8_t(choice);
		}
	}
	w.god.deeds[what]++;
	addNum(w, "cards_answered", 1);
	emitCard(w, id, k, "answered", culture, choice);
	// (V1) a messenger was chosen: any other MessengerChoice card of that people (a "seek a voice" card beside an
	// armed one) has lost its matter and is withdrawn, so no unanswerable card waits out its window
	if (k == CardKind::MessengerChoice && culture >= 0 && characters::messenger(w, culture) >= 0) {
		std::vector<int> stale;
		for (const Card& c : w.mythos.cards)
			if (isOpen(c) && c.kind == CardKind::MessengerChoice && c.culture == culture && c.id != id) stale.push_back(c.id);
		for (int sid : stale) withdraw(w, sid);
	}
	return true;
}

bool withdraw(World& w, int id) {
	const int i = findIndex(w, id);
	if (i < 0) return false;
	Card& c = w.mythos.cards[size_t(i)];
	if (!isOpen(c)) return false;
	c.state = uint8_t(CardState::Withdrawn);
	const CardKind k = c.kind;
	const int culture = c.culture;
	addNum(w, "cards_withdrawn", 1);
	emitCard(w, id, k, "withdrawn", culture, -1);
	return true;
}

bool lookIntoHeart(World& w, int id) {
	if (phaseOf(w) < 1) return false;
	const int i = findIndex(w, id);
	if (i < 0) return false;
	{
		const Card& c = w.mythos.cards[size_t(i)];
		if (!isOpen(c) || !live(w, c.kind) || (c.kind != CardKind::Judgement && c.kind != CardKind::Trial)) return false;
		if (c.data.count("heartSeen")) return true;      // already seen: nothing more to pay
	}
	const double cost = std::max(0.0, fin(w.tune<CardsTune>().lookCost));
	if (cost > 0 && !powers::pay(w, "look", cost)) return false;
	const int j = findIndex(w, id);
	if (j < 0) return false;
	w.mythos.cards[size_t(j)].data["heartSeen"] = 1;
	return true;
}

std::vector<CardOption> options(const World& w, int id) {
	const int i = findIndex(w, id);
	if (i < 0) return {};
	const Card& c = w.mythos.cards[size_t(i)];
	if (!isOpen(c) || !live(w, c.kind)) return {};
	const SourceFns& fns = sourceOf(c.kind);
	return fns.options ? fns.options(w, id) : std::vector<CardOption>{};
}

const Card* find(const World& w, int id) {
	for (const Card& c : w.mythos.cards) if (c.id == id) return &c;
	return nullptr;
}

std::vector<int> openCards(const World& w) {
	std::vector<int> out;
	for (const Card& c : w.mythos.cards) if (c.state == uint8_t(CardState::Open)) out.push_back(c.id);
	std::sort(out.begin(), out.end());
	return out;
}

double priority(const World& w, const Proposal& p) { return priorityOf(w, p); }

// ---------------------------------------------------------------- the Cards-sourced kind (CompetingPrayers)

bool answerOwn(World& w, int id, int choice) {
	const int i = findIndex(w, id);
	if (i < 0) return false;
	const Card& c0 = w.mythos.cards[size_t(i)];
	if (!isOpen(c0) || c0.kind != CardKind::CompetingPrayers) return false;
	const int a = c0.ref[0], b = c0.ref[1], pair = c0.ref[2];
	if (!pairAvailable(pair, choice)) return false;
	const CardsTune& t = w.tune<CardsTune>();
	const SocietyTune& st = w.tune<SocietyTune>();
	const double extra = std::max(0.0, fin(t.competePenalty, 1) - 1) * std::max(0.0, fin(st.prayerPenalty));
	const double half = 0.5 * std::max(0.0, fin(st.prayerPenalty));
	const auto win = [&](int sid, double scale) { if (aliveTown(w, sid)) society::answerPrayer(w, sid, scale); };
	const auto lose = [&](int sid) { if (aliveTown(w, sid) && extra > 0) society::nudge(w, sid, -extra, -extra); };
	const int ca = aliveTown(w, a) ? w.settlements[size_t(a)].culture : -1;
	const int cb = aliveTown(w, b) ? w.settlements[size_t(b)].culture : -1;
	// what the answer arms for the client (the act is paid when it is cast)
	const char* armPower = nullptr;
	int armAt = -1;
	double armAng = 1, armCost = 1;
	switch (choice) {
	case 0:                                              // the first side: rain on the dry / A / the aggressor / heal A
		win(a, 1); lose(b);
		if (pair == int(Pair::RainSun)) { armPower = "rain"; armAt = a; }
		else if (pair == int(Pair::VictoryVictory)) { armPower = "lightning"; armAt = nearestTown(w, cb, a); }
		else if (pair == int(Pair::VictoryPeace)) { armPower = "lightning"; armAt = b; }
		else { armPower = "heal"; armAt = a; }
		break;
	case 1:                                              // the other side: hold the rain / B / the Omen / let it run
		win(b, 1); lose(a);
		if (pair == int(Pair::VictoryVictory)) { armPower = "lightning"; armAt = nearestTown(w, ca, b); }
		else if (pair == int(Pair::VictoryPeace)) { armPower = "peace"; armAt = b; }
		break;
	case 2:
		if (pair == int(Pair::RainSun)) {                // a middle way: light rain on the dry fields only
			const double eff = std::clamp(fin(t.competeMiddleEffect, 0.5), 0.0, 1.0);
			win(a, eff); win(b, eff);
			armPower = "rain"; armAt = a; armAng = 0.5; armCost = std::max(0.0, fin(t.competeMiddleCost, 1));
		} else {                                         // the Omen of Peace: both prayers fail
			armPower = "peace"; armAt = a;
		}
		break;
	default:                                             // neither: both lapse at half the penalty
		if (aliveTown(w, a) && half > 0) society::nudge(w, a, half, half);
		if (aliveTown(w, b) && half > 0) society::nudge(w, b, half, half);
		break;
	}
	const int j = findIndex(w, id);
	if (j >= 0 && armPower) arm(w.mythos.cards[size_t(j)], armPower, armAt, w, armAng, armCost);
	return true;
}

void lapseOwn(World& w, int id) {
	const int i = findIndex(w, id);
	if (i < 0) return;
	const Card& c = w.mythos.cards[size_t(i)];
	if (!isOpen(c) || c.kind != CardKind::CompetingPrayers) return;
	if (!w.tune<MythosTune>().emergentOn(MythosTune::FamLapse)) return;   // lapse effects are an emergent family (5.1 rule 2)
	const double half = 0.5 * std::max(0.0, fin(w.tune<SocietyTune>().prayerPenalty));
	const int a = c.ref[0], b = c.ref[1];
	if (half > 0 && aliveTown(w, a)) society::nudge(w, a, half, half);
	if (half > 0 && aliveTown(w, b)) society::nudge(w, b, half, half);
}

std::vector<CardOption> optionsOwn(const World& w, int id) {
	const int i = findIndex(w, id);
	if (i < 0) return {};
	const Card& c = w.mythos.cards[size_t(i)];
	if (!isOpen(c) || c.kind != CardKind::CompetingPrayers) return {};
	std::vector<CardOption> out;
	for (int k = 0; k < kCardKinds[size_t(CardKind::CompetingPrayers)].answers; k++) {
		CardOption o;
		o.choice = k;
		o.cost = 0;
		o.ok = pairAvailable(c.ref[2], k);
		o.why = o.ok ? OptionWhy::Ok : OptionWhy::Unavailable;
		out.push_back(o);
	}
	return out;
}

} // namespace dm::cards
