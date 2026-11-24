// DEMIURGE — Sim/Cards.h : the card director (SPEC-MYTHOS 2.2-2.3, 3.10 pairing).
// Owner: cards track (M2, key "cards"): Sim/Cards.cpp, SimTests/test_cards*.cpp, SimTests/stubs/Cards_stub.cpp.
// Header written and FROZEN by the contracts track (M0, 2026-11-20).
//
// Owns: mythos.cards and the director's counters in mythos.num ("cards_..."). Submodules only PROPOSE during
// their yearly passes; cards::yearly (after mythos::yearly) opens at most one card a year against one attention
// budget; answers go through cards::answer, which pays through powers::pay and dispatches by kCardKinds[kind]
// .source to that module's answer / lapse / options (a static const table of function pointers: no globals).
// No module calls Cards except to propose, open (not budgeted kinds) or answer. Proposals live one year
// (w.scratch: they are made and consumed inside one yearly group, never across a save).
// Events: card { id, kind, stage, culture, choice }.
#pragma once

#include <array>
#include <vector>

#include "State.h"

namespace dm {

// Contract data of each card kind (2.3): index = CardKind.
struct CardKindInfo {
	CardKind kind;
	CardSource source;                       // who answers it
	int phase;                               // MythosTune::phase at which the kind exists
	int minAge, maxAge;                      // the age band (Age::Id) in which it may open (ageFit)
	bool budgeted;                           // false: opened at once by cards::open (genesis, player-initiated, reports)
	int answers;                             // choices 0 .. answers-1, in the order of the 2.3 answers column
	int lapse;                               // the default Card::lapse (-1: the source's "the people decide" rule)
};
// Choice encodings beyond "index in the answers column":
//   ShapePeople: choice = curiosity + 3 compassion + 9 ambition + 27 courage + 81 aggression (TraitLevel each,
//                0..242); 243 = "leave it to chance".
//   Judgement:   choice = JudgeBy x 4 + Sentence (0..7).
//   DeathQuestion: choice = (DeathBelief - 1) x 4 + Clarity (0..15); 16 = silence.
inline constexpr CardKindInfo kCardKinds[] = {
	{ CardKind::ShapePeople,      CardSource::Mythos,     1, 0, 0, false, 244, 243 },
	{ CardKind::Presence,         CardSource::Mythos,     1, 0, 6, false,   3,   0 },
	{ CardKind::FirstDecree,      CardSource::Doctrine,   1, 0, 1, true,    2,   1 },
	{ CardKind::DecreeConflict,   CardSource::Doctrine,   1, 0, 6, true,    3,  -1 },
	{ CardKind::Breach,           CardSource::Doctrine,   1, 0, 6, true,    1,   0 },
	{ CardKind::MessengerChoice,  CardSource::Characters, 1, 1, 6, true,    4,   3 },
	{ CardKind::CompetingPrayers, CardSource::Cards,      1, 0, 6, true,    4,   3 },
	{ CardKind::Judgement,        CardSource::Doctrine,   1, 1, 6, true,    8,  -1 },
	{ CardKind::PactOffer,        CardSource::Doctrine,   2, 1, 6, true,    3,   2 },
	{ CardKind::PactBroken,       CardSource::Doctrine,   2, 0, 6, true,    4,   3 },
	{ CardKind::Blessing,         CardSource::Mythos,     2, 1, 6, true,    4,   3 },
	{ CardKind::Warning,          CardSource::Omens,      2, 0, 6, true,    5,   4 },
	{ CardKind::WarningExploited, CardSource::Omens,      2, 1, 6, true,    3,   1 },
	{ CardKind::Trial,            CardSource::Characters, 2, 2, 6, true,    5,   4 },
	{ CardKind::Dispute,          CardSource::Mythos,     2, 1, 6, true,    4,   1 },
	{ CardKind::Institution,      CardSource::Faiths,     2, 2, 6, true,    4,   2 },
	{ CardKind::RivalVoice,       CardSource::Faiths,     2, 2, 6, true,    3,   2 },
	{ CardKind::LandClaim,        CardSource::Doctrine,   2, 1, 6, true,    3,  -1 },
	{ CardKind::Revelation,       CardSource::Doctrine,   3, 0, 6, true,    3,   1 },
	{ CardKind::RevelationMisuse, CardSource::Doctrine,   3, 0, 6, true,    3,   2 },
	{ CardKind::DeathQuestion,    CardSource::Doctrine,   3, 0, 6, true,   17,  16 },
	{ CardKind::Bearers,          CardSource::Faiths,     3, 0, 6, true,    4,   1 },
	{ CardKind::KeeperDispute,    CardSource::Keepers,    3, 0, 6, true,    4,   1 },
	{ CardKind::ComingOfAge,      CardSource::Mythos,     3, 0, 6, true,    3,   0 },
	{ CardKind::SilenceReport,    CardSource::Mythos,     3, 0, 6, false,   3,   0 },
	{ CardKind::Remnant,          CardSource::Renewal,    3, 0, 6, false,   3,   1 },
	{ CardKind::Situation,        CardSource::Situation,  1, 0, 6, true,    4,  -1 },
};
static_assert(sizeof(kCardKinds) / sizeof(kCardKinds[0]) == size_t(CardKind::Count), "one row per card kind");

struct CardsTune {
	double firstYear = 90;           // no card (but genesis' ShapePeople / Presence) before this year of history
	double minGap = 70;              // years between two card openings (35 s at 1x)
	double targetGap = 200;          // the budget refills one card per this many years (100 s at 1x)
	double maxGap = 420;             // past this many years without a card, open the best proposal even if weak
	int    burst = 2;                // budget bucket size
	int    maxOpen = 2;              // open cards at once
	double cultureGap = 150;         // one culture gets a new card at most this often (unless severity >= 0.8)
	double cultureGapSeverity = 0.8; // (M0) the "unless" of cultureGap
	double kindGap = 400;            // the same kind again within this many years: priority x 0.5
	double kindGapFactor = 0.5;      // (M0) that x 0.5
	double novelty = 1.5;            // a kind never seen in this world: priority x this
	double minPriority = 0.25;       // below this a proposal waits (unless maxGap forces it)
	double withdrawnHold = 1;        // while the god has withdrawn: cards open as SilenceReport digests only
	// years open before lapse, per kind (table 2.3; 0 = until answered or the source's own deadline)
	std::array<double, int(CardKind::Count)> deadline{ 0, 0, 80, 50, 50, 60, 15, 60, 80, 60, 60, 0, 40, 80, 50, 80, 80,
		50, 80, 60, 60, 80, 60, 80, 0, 0, 60 };
	std::array<double, int(CardKind::Count)> weight = filled(1.0);   // priority weight per kind
	// competing prayers (3.10, CardsTune pairs)
	double competeAng = 0.2, competePenalty = 1.5, competeMiddleCost = 1.5, competeMiddleEffect = 0.5;
	double lookCost = 20;            // Look into the heart (3.11)

	static std::array<double, int(CardKind::Count)> filled(double v) {
		std::array<double, int(CardKind::Count)> a{};
		a.fill(v);
		return a;
	}
};

namespace cards {

// A candidate matter from a source module (2.2). Identical (kind, culture, ref[0]) proposals of one year merge.
struct Proposal {
	CardKind kind = CardKind::Dispute;
	int culture = -1, other = -1, settlement = -1, figure = -1, cell = -1;
	double severity = 0;                     // 0..1, the source's measure
	std::array<int, 4> ref{ -1, -1, -1, -1 };
	NumBag data;
	int lapse = -2;                          // -2 = kCardKinds[kind].lapse; else the Card::lapse to store
	double deadline = -1;                    // > 0: years open (overrides CardsTune::deadline, e.g. Warning)
};

// Offer a matter for this year's opening (budgeted kinds). False if the kind is not live at this phase or the
// proposal is malformed.
bool propose(World& w, const Proposal& p);
// The director: lapse due cards, then open at most one proposal (budget, gaps, priority; ties by kind order then
// culture id), then drop this year's proposals.
void yearly(World& w);
// Open a card now, outside the budget (not budgeted kinds: ShapePeople, Presence, a player-armed MessengerChoice,
// SilenceReport, Remnant). The card id, or -1.
int open(World& w, const Proposal& p);
// The player's answer: validates, pays through powers::pay (refused: awe:short, the card stays open), dispatches
// to the source, marks it answered, emits card { answered }, counts god.deeds["card:<kind>"].
bool answer(World& w, int id, int choice);
// Close a card without an answer (its matter went away): card { withdrawn }.
bool withdraw(World& w, int id);
// Look into the heart (3.11): pays CardsTune::lookCost, marks the card's data["heart"] seen. Nothing witnessed.
bool lookIntoHeart(World& w, int id);
// The answers of an open card (dispatches to the source); empty for a bad id.
std::vector<CardOption> options(const World& w, int id);
// The card, or nullptr.
const Card* find(const World& w, int id);
// Ids of the open cards, ascending.
std::vector<int> openCards(const World& w);
// The priority 2.2 would give this proposal now (tests; 0 for a dead kind).
double priority(const World& w, const Proposal& p);

// The Cards-sourced kind (CompetingPrayers): the director answers it itself.
bool answerOwn(World& w, int id, int choice);
void lapseOwn(World& w, int id);
std::vector<CardOption> optionsOwn(const World& w, int id);

} // namespace cards
} // namespace dm
