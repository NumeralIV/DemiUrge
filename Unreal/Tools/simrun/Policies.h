// DEMIURGE — Tools/simrun/Policies.h : scripted players for the headless runner (JS tools/policies.js).
// Owner: sim engineer (A14). Tool code (not Sim/): it may use anything, but it only ever acts on the
// world through powers::can / powers::use and society::setEdict, exactly like a player, and draws
// its own randomness from a private Rng (never w.rng), so runs stay deterministic.
//
// Policies (called before EVERY tick; decisions about once a year, brush strokes over several ticks
// with dt = sim::kRealPerTick, the first call of a press with dt = 0):
//   passive   does nothing (an undisturbed world)
//   kind      answers prayers (rain / heal / peace / bloom), heals plagues, ends wars with the Omen
//             of Peace (sim-findings-2 verifier: one Omen on a war comes first, before the two prayers of the year)
//             (v3.1: and, with Awe to spare, calms a people on the brink of war), keeps a prophet with the leading culture (edict peace while at war, else
//             build; other cultures only with Awe to spare), every year blooms (rains, before
//             Bloom; half a second) over the least loved of the leading culture's 24 largest towns and inspires
//             one with spare Awe, and wakes a new people for free when everyone is gone
//   cruel     a tyrant who stops short of extinction (never acts while fewer than 4 settlements
//             live) and rules by fear: earthquakes on the least frightened of the leading
//             culture's 12 largest towns whenever affordable, lightning while saving up, a volcano
//             or meteor only when the Awe would otherwise be wasted at its cap
//   chaotic   about every other year a random unlocked power on a random target (settlement
//             targets on settlements, brush strokes of random size and length); re-awakens
//   doom      no restraint: lets the people pass the 1,000 a silence needs while hoarding Awe,
//             then drowns their towns with Lower Land (lowest first) and strikes up to 3 of the
//             largest a year with every great wrath it can pay for
// The design review's new players (docs/reviews/claude-design-review.md, ported from its driver):
//   curious      small blessings until year 900 (answers 2 prayers, heals a plague, a dab of rain or
//                bloom on a random town about every other year, now and then an Inspire), then a burst
//                of wrath until 1100 (saves for the greatest wrath its Awe can hold, lightning while
//                waiting), then neglect to the end
//   blessonly    the same small blessings until year 1000, then nothing
//   prayers      answers every prayer it can (up to 3 a year) and nothing else; re-awakens
//   repent       the wrath burst until year 800, then answers prayers and blesses
//   sculptor     about every 15 years holds Raise or Lower Land for 1 s on a random town; re-awakens
//   late-kind    nothing until an Ark is being built, then the kind player
//   late-cruel   nothing until an Ark is being built, then the cruel player
//   inspire-spam Inspire on the leading culture's capital whenever affordable; re-awakens
//   hoarder      answers prayers (like `prayers`) and never buys anything durable
//   builder      answers prayers and consecrates (shrine -> temple, capital -> Wonder) whenever it can
// The verifier's (wave3-gameplay-verify) late players: the review's late-kind / late-cruel, but waking
// with the Atomic age (about 500-700 years, 4-6 real minutes, before the launch) instead of the Ark:
//   atomic-kind  nothing until the world reaches the Atomic age, then the kind player
//   atomic-cruel nothing until the world reaches the Atomic age, then the cruel player
// The findings-11/13 verifier's (sim-findings-2) adversarial players, which push the new diplomacy rules:
//   peacemaker   only the Omen of Peace, on every people at war or on the brink whenever it can pay (up
//                to 3 a year): can a player hold the world in permanent peace, and what does it buy?
//   favourite    blesses one people only (the largest when it starts; a new one when it dies): its
//                prayers, plagues, a bloom on its least loved town and an Inspire with Awe to spare.
//                The envy rule's war spiral against a favourite
//   scourge      strikes one small people only (the smallest of at least 2, never the last): lightning,
//                a quake when affordable. The scourged blame the spared: extinction cascades, spirals
//
// (v4 mythos, track M9 "mythos-tools") the v1 scripted gods of SPEC-MYTHOS 5.3. They need MythosTune::phase >= 1
// (simrun raises the build's phase to 1 for them unless --mythos-phase says otherwise). They speak only through
// powers::use("speak", cell, { word }), arm the Messenger with UseOpts::choose (the MessengerChoice card), and
// answer cards only through cards::answer / cards::lookIntoHeart, exactly like a player. Decrees go to every
// living people with a town, at most two words a year, keeping Awe for the base player's own acts.
//   responder-myth  kind, and answers every card with its first answer that can be paid (ShapePeople: "leave it to
//                   chance"; Presence: Wonders) and casts what an answer arms (a competing prayer's rain or wrath)
//   random-myth     chaotic (Word powers included, with random words), and answers every card at random
//   lawgiver        Peace, Share, Temples, Truth, Guarded, Shelter (+ Stewardship from phase 2), Exact words through a
//                   messenger (a chosen one in the leading people's capital; from the sky until there is one), punishes
//                   breaches (by the deed, with a disaster when it can pay, else a warning); otherwise passive
//   valour          Valour, Gain, Closed, Strength, Exact through a messenger; otherwise passive
//   exact-only      words only Exact (the people's own leanings as decrees, then answers), as often as allowed
//   parable-only    the same, only Parables through a messenger (none before one is chosen)
//   sign-only       the same, only Signs in the sky
//   unseen-kind     kind under Unseen from the waking (the Presence card)
//   msg-kind        kind under Messengers, keeping a chosen messenger in the leading people's capital
//   judge-deeds     the lawgiver's decrees as Dreams; every Judgement by the deed, with a warning; otherwise passive
//   judge-hearts    the same, by the heart
//   merciful        kind, the lawgiver's decrees as Dreams, forgives every judgement (by the heart)
//   stern           the lawgiver's decrees as Dreams, punishes every judgement justly (by the deed, with a disaster,
//                   else removing the ruler, else a warning); otherwise passive
//   kingmaker       stands with the people of the best verdict (society::memory) in every competing prayer and casts
//                   what that arms; otherwise passive
//   gentle          kind, Kindness and Shelter as Parables (Dreams until a messenger is chosen), forgives by the heart
// Every mythos policy answers the MessengerChoice card it armed (the most credible candidate; Shepherds first for
// Parables, Queens first for Exact words). Cards a policy has no rule for follow --cards (default lapse); the
// existing policies answer no card unless --cards / --shape / --presence ask for it.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Rng.h"
#include "State.h"

namespace dm::simrun {

enum class PolicyKind : uint8_t { Passive, Kind, Cruel, Chaotic, Doom, Curious, BlessOnly, Prayers, Repent, Sculptor, LateKind, LateCruel,
	InspireSpam, Hoarder, Builder, AtomicKind, AtomicCruel, Peacemaker, Favourite, Scourge,
	// (v4 mythos, M9) the v1 scripted gods (SPEC-MYTHOS 5.3)
	ResponderMyth, RandomMyth, Lawgiver, Valour, ExactOnly, ParableOnly, SignOnly, UnseenKind, MsgKind, JudgeDeeds, JudgeHearts,
	Merciful, Stern, Kingmaker, Gentle, Count };
inline constexpr int kFirstMythPolicy = int(PolicyKind::ResponderMyth);   // the pre-mythos set is 0 .. this - 1 ("all")
const char* toStr(PolicyKind k);
bool policyFromStr(std::string_view s, PolicyKind& out);
// A v4 mythos policy (needs MythosTune::phase >= 1).
inline bool isMythos(PolicyKind k) { return int(k) >= kFirstMythPolicy && k != PolicyKind::Count; }

// How cards are answered when the policy has no rule for a kind (simrun --cards, SPEC-MYTHOS 5.2).
enum class CardMode : uint8_t { Lapse, First, Random, Policy };
const char* toStr(CardMode m);
bool cardModeFromStr(std::string_view s, CardMode& out);

// The run's card options (simrun --cards, --shape, --presence).
struct MythosPlay {
	CardMode cards = CardMode::Lapse;
	int shape = -1;                       // the ShapePeople answer (0 .. 243), -1 = no rule
	int presence = -1;                    // the Presence answer (0 Wonders, 1 Messengers, 2 Unseen), -1 = no rule
};
// A --shape preset ("curious-brave", "kind-meek", "chance", "middling", ...) or a five-letter code of L / M / H for
// curiosity, compassion, ambition, courage, aggression (e.g. "HLMHL") -> the ShapePeople choice; -1 if unknown.
int shapeFromStr(std::string_view s);
// The preset names, for --help.
const char* shapePresets();

struct PolicyStats {
	int clicks = 0;                       // successful click powers
	int strokes = 0;                      // brush strokes started
	int refused = 0;                      // powers::use returned false after can() said ok
	std::map<std::string, int> byPower;   // successful uses (clicks) / strokes per power id
	int edicts = 0;
	// (v4 mythos, M9)
	int cardsAnswered = 0, cardsRefused = 0, hearts = 0;   // cards::answer true / false; Look into the heart
	std::map<std::string, int> answers;   // "<kind>:<choice>" answered
	int words = 0, wordsRefused = 0;      // speak used / refused after doctrine::canSpeak said ok
	std::map<std::string, int> wordsBy;   // "<clarity>:<content>"
	int armed = 0;                        // answers whose armed act (rain, lightning ...) was then cast
	int messengers = 0;                   // MessengerChoice cards this policy armed and answered
	std::map<int, int> favoured;          // kingmaker: culture -> competing-prayer cards it stood with
};

class Policy {
public:
	Policy(PolicyKind kind, uint32_t seed, const MythosPlay& play = {});
	// Call before every sim::advance(w, 1).
	void before(World& w);
	PolicyKind kind() const { return kind_; }
	const PolicyStats& stats() const { return stats_; }

private:
	// (v4 mythos, M9) cards and words
	bool cardsOn() const;                                 // answers cards at all (a mythos policy, or --cards / --shape / --presence)
	void playCards(World& w);
	int ruleFor(World& w, const Card& c);                 // the policy's answer, -1 none (then the card mode), -2 leave it
	int modeChoice(World& w, const Card& c, CardMode m);  // first / random answer that can be paid, -1 none
	void castArmed(World& w, int cardId);
	void decideMythos(World& w);                          // the words and messengers of the mythos policies
	bool speakTo(World& w, int culture, const WordOpts& o);
	void giveDecrees(World& w, const std::vector<std::pair<DecreeArea, int>>& set, Clarity clarity, int maxWords);
	void wordsOnly(World& w, Clarity clarity);
	void keepMessenger(World& w, int culture);
	void randomWord(World& w, int cell);
	bool chooseMessenger() const;                         // prophet arms the MessengerChoice card (every mythos policy)
	bool armMessenger(World& w, int cell);                // prophet with UseOpts::choose, then the card's answer
	int messengerPick(World& w, const Card& c);           // the candidate this policy chooses, -1 none payable
	MythosPlay play_;
	std::map<int, double> cardTried_;                     // card id -> year of the last refused answer
	std::map<std::pair<int, int>, double> decreeTried_;   // (culture, area) -> year of the last decree word
	bool click(World& w, const char* id, int cell);
	bool startStroke(World& w, const char* id, int cell, int ticks, double ang = -1);
	void continueStroke(World& w);
	bool reawaken(World& w);
	void decideKind(World& w);
	void decideCruel(World& w, bool restrained);
	void decideChaotic(World& w);
	// the design review's players
	void answerPrayers(World& w, int maxN);
	void smallBlessings(World& w);
	void wrathBurst(World& w);
	bool consecrate(World& w);
	// the verifier's adversarial players (sim-findings-2)
	void decidePeacemaker(World& w);
	void decideFavourite(World& w);
	void decideScourge(World& w);
	int chosen_ = -1;                     // favourite / scourge: the people chosen (-1 none yet)

	double lateFrom_ = -1;                // late-kind / late-cruel: year the first Ark began; atomic-*: the Atomic age (-1: not yet)

	PolicyKind kind_;
	Rng rng_;
	Rng cardRng_;                         // (M9) card and word choices: never touches rng_, so the base players keep their draws
	PolicyStats stats_;
	double nextYear_ = 0;                 // next decision year (history)
	// the brush stroke in progress
	std::string strokeId_;
	int strokeCell_ = -1;
	int strokeLeft_ = 0;
	bool strokeFirst_ = true;
	double strokeAng_ = -1;
};

} // namespace dm::simrun
