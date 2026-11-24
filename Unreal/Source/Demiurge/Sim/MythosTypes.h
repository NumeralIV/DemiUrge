// DEMIURGE — Sim/MythosTypes.h : every persisted struct and enum of the mythos modules (SPEC-MYTHOS 1.2),
// frozen by the contracts track (M0 "mythos-contracts", 2026-11-20). FOUNDATION-CPP part 3 documents it.
//
//   World::mythos (State.h) is a MythosState. Each module writes only its own part (SPEC-MYTHOS 1.7):
//     presence, faith[*] (aspect .. grown, except death / bearer fields), factors, withdrawnUntil,
//     towns[*].blessing*, acts ............................................ Mythos
//     faith[*].death / deathSure, decrees, words, pacts, revelations ...... Doctrine
//     faith[*].bearer / superiority / charge, houses, rivals .............. Faiths
//     figures ............................................................. Characters
//     cards ............................................................... Cards
//     legends ............................................................. Legends
//     omens ............................................................... Omens
//     keepers ............................................................. Keepers
//     renewal, renewedYear, towns[*].remnantUntil ......................... Renewal
//     archetype (a copy of settings.archetype) ............................ Sim (at create)
//     num: any module, keys prefixed with its name ("mythos_", "cards_", "doctrine_", ...)
//     nextId: whoever creates an id'd record (Utterance, Pact, Figure, Card, Legend, RivalVoice, Omen, Keeper)
//   Every `data` bag is private to the writer of its struct (keys need no prefix there).
//
// Self-contained (standard headers only) so that Schema.h can include it for Ending::legacy. The enums'
// toStr / fromStr are defined in Schema.cpp; enumCount<E>() is specialised here.
//
// Additions to the shapes printed in SPEC-MYTHOS 1.2 (all additive; the printed fields are unchanged):
//   - default member initializers everywhere (ids -1, numbers 0) and a NumBag `data` on every record
//     (the headers are frozen, so each module keeps its private extras there);
//   - Decree::custom; Utterance::area; ArchetypeParams' mythic-seed fields (in Mythos.h);
//   - MythosState::towns (TownMyth: per-settlement blessings and the Remnant, SPEC-MYTHOS 1.4 "kept per
//     settlement in MythosState") and MythosState::acts (the god's recent acts recorded by Mythos's bus
//     handlers, persisted so that a save between two yearly passes replays identically);
//   - the small enums that name the uint8_t / int8_t fields (stances, terms, kinds, states).
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace dm {

// The same alias as Schema.h (a redeclaration naming the same type is valid C++).
using NumBag = std::map<std::string, double>;

// ------------------------------------------------------------------ enums of SPEC-MYTHOS 1.2

// How near the god comes (3.2). Walk is reserved (the mortal avatar, deferred by the owner):
// mythos::setPresence refuses it and no save may hold it.
enum class Presence : uint8_t { Wonders, Messengers, Unseen, Walk };
// How a culture sees the god (3.3).
enum class Aspect : uint8_t { Unknown, Parent, Ruler, Mystery, Threat };
// The kind of world (3.1). Balanced = today's world.
enum class Archetype : uint8_t { Balanced, Generous, Dangerous, Beautiful, Strange };
// The second verdict of the ending (4). None until mythos::legacy fills it.
enum class Legacy : uint8_t { None, ObedientEmpire, CompassionateCivilisation, TerrifiedWorld, ThrivingWilderness, Unneeded };
// Decree areas (3.6): Worship holds the Rite (3.13), Land the Decree of the Land (3.22).
enum class DecreeArea : uint8_t { Violence, Wealth, Worship, Honesty, Hospitality, Care, Land, Count };
// How clearly the god speaks (3.9), clearest first.
enum class Clarity : uint8_t { Exact, Parable, Dream, Sign };
// Through what the word reaches them (3.9).
enum class Channel : uint8_t { Messenger, Dream, Sky };
// Card kinds (2.3), in the catalogue's order; Situation is gameplay-v4's, if it is ever built.
enum class CardKind : uint8_t {
	ShapePeople, Presence, FirstDecree, DecreeConflict, Breach, MessengerChoice,
	CompetingPrayers, Judgement, PactOffer, PactBroken, Blessing, Warning, WarningExploited, Trial, Dispute,
	Institution, RivalVoice, LandClaim, Revelation, RevelationMisuse, DeathQuestion, Bearers, KeeperDispute,
	ComingOfAge, SilenceReport, Remnant, Situation, Count
};

// ------------------------------------------------------------------ enums naming the small fields

// Decree::stance per area (-1 = none / repealed). The Worship area's stance is the Rite.
enum class ViolenceStance : int8_t { Peace, Defence, Valour };
enum class WealthStance : int8_t { Share, Tithe, Gain };
enum class Rite : int8_t { None = -1, Prayer, Temples, Kindness, Art, World };
enum class HonestyStance : int8_t { Truth, Discretion, Cunning };
enum class HospitalityStance : int8_t { Open, Guarded, Closed };
enum class CareStance : int8_t { Shelter, Kin, Strength };
enum class LandStance : int8_t { Dominion, Stewardship, Equal };
// Stances of an area (Worship: 5 rites; the others 3; Count: 0).
constexpr int stanceCount(DecreeArea a) {
	return a == DecreeArea::Worship ? 5 : (uint8_t(a) < uint8_t(DecreeArea::Count) ? 3 : 0);
}
// "peace", "defence", "valour", "share", ..., "prayer", ..., "equal"; "none" for -1 or out of range.
const char* stanceName(DecreeArea a, int stance);
// The inverse; "none" parses as -1. False for an unknown name.
bool stanceFromName(DecreeArea a, std::string_view s, int& out);

// Utterance::content (3.9 "What will you say?").
enum class WordContent : uint8_t { Decree, Repeal, Warning, Promise, Answer, Revelation };
// Utterance::outcome (3.9 tables). Pending until the people have heard it.
enum class WordOutcome : uint8_t { Pending, Heard, Literal, Wisdom, Sect, Personal, Rival, HouseRead, Catastrophe };
// Pact terms and states (3.14).
enum class GodTerm : uint8_t { Rain, Shield, Healing, Victory, NoWrath };
enum class PeopleTerm : uint8_t { KeepDecree, Temples, NoWar, FirstFruits, CarryWord };
enum class PactState : uint8_t { Sworn, Kept, GodBroke, PeopleBroke, Forgiven, Lapsed, Ended };
// Figure kinds (3.8, 3.16, 3.21, 3.23, 4 Lives; story-v4's kinds after Life, built later by Story).
enum class FigureKind : uint8_t { Messenger, Candidate, Ruler, Healer, RivalVoice, Life, Founder, Hero, Voyager, Sage, Doubter, Captain };
// Figure::archetype of a messenger or candidate (3.8). None for other kinds.
enum class MessengerArchetype : uint8_t { None, Shepherd, Criminal, Child, Queen, Unbelieved };
// Figure::fate.
enum class FigureFate : uint8_t { Living, Died, Fallen, Martyred, Tragic };
// Figure::flags bits.
namespace FigureFlag {
enum Bits : uint16_t {
	HeartSeen = 1, Redeemed = 2, Discredited = 4, Vindicated = 8, Relapsed = 16, Exalted = 32,
	Tyrant = 64, Steadfast = 128, Broken = 256, Resisted = 512, Proud = 1024
};
}
// Trials (3.18); Figure::trial holds the kind while a trial runs (-1 none).
enum class TrialKind : uint8_t { Power, Hardship, Temptation, Impossible };
// Card::state (2.2 lifecycle).
enum class CardState : uint8_t { Open, Answered, Lapsed, Withdrawn };
// Legend morals and credit (3.15).
enum class LegendMoral : uint8_t { Mercy, Justice, Wrath, Wonder, Warning, Sacrifice };
enum class LegendCredit : uint8_t { God, Messenger, Rival, Nature, Ancestors };
// RivalVoice::state (3.21).
enum class RivalState : uint8_t { Rising, Exposed, Martyr, Faded, Split };
// Omens (3.17): what is coming, how it was told, how the people answered.
enum class OmenKind : uint8_t { Quake, Eruption, Drought, Plague, Tsunami, Judgement };
enum class WarningWay : uint8_t { None, Dreams, Stars, Harvest, Messenger, Nothing };
enum class OmenResponse : uint8_t { None, Repentance, Preparation, Exploitation, Dismissal };
// Keepers (3.26): Keeper::domain and Keeper::disposition.
enum class KeeperDomain : uint8_t { LastDoor, Deep, Scales, Sleep, People };
enum class Temper : uint8_t { Gentle, Stern };
// Revelations (3.25).
enum class RevelationKind : uint8_t { Agriculture, Writing, Medicine, WorldShape, DeathTruth };
enum class RevelationOutcome : uint8_t { Pending, Shared, Hoarded, Weaponised };
// Beliefs about death (3.27): Faith::death and Faith::deathSure.
enum class DeathBelief : int8_t { None, FurtherShore, Turning, Return, LongSleep };
enum class Certainty : int8_t { None, Uncertain, Glimpsed, Known };
// The Bearers' charge (3.24): Faith::charge.
enum class Charge : int8_t { None, CarryWord, Garden, Peace, Stars };
// Blessings (3.16) and their consequences.
enum class BlessingKind : uint8_t { None, Prosperity, Healer, Endurance };
enum class Consequence : uint8_t { None, Gratitude, Dependence, Envy, Conquest };
// Judgement (3.11, 3.12).
enum class JudgeBy : uint8_t { Deed, Heart };
enum class Sentence : uint8_t { Forgive, Warn, Remove, Disaster };
enum class Circumstance : uint8_t { None, Famine, War, Plague, Fear, Order };
enum class HeartKind : uint8_t { Unknown, Selfless, Fearful, Greedy, Zealous };
// Disputes (3.19, 3.27's "why did they not come back?").
enum class DisputeKind : uint8_t { WhyDied, WhyStruck, TooHeavy, HiddenFace, WhyThem, NoReturn };
enum class DisputeAnswer : uint8_t { Explain, Silence, ChangeMind, Demand };
// Renewal (3.30): MythosState::renewal holds the int value (0 none, 1 water, 2 burning).
enum class RenewalKind : uint8_t { None, Water, Burning };
// The ShapePeople card's three levels per trait (3.5): Low 0.25, Middling 0.5, High 0.8.
enum class TraitLevel : uint8_t { Low, Middling, High };
// Which module answers a card kind (Cards dispatches through a static table on this).
enum class CardSource : uint8_t { Mythos, Cards, Doctrine, Characters, Faiths, Omens, Keepers, Renewal, Situation };
// Why a card answer cannot be chosen now (CardOption::why).
enum class OptionWhy : uint8_t { Ok, Awe, Unavailable, NeedsMessenger, NeedsRuler, Locked };

// Number of enumerators (Schema.h declares the same primary template and specialises its own enums).
template <class E> constexpr int enumCount();
template <> constexpr int enumCount<Presence>() { return 4; }
template <> constexpr int enumCount<Aspect>() { return 5; }
template <> constexpr int enumCount<Archetype>() { return 5; }
template <> constexpr int enumCount<Legacy>() { return 6; }
template <> constexpr int enumCount<DecreeArea>() { return 7; }      // Count itself is not a value
template <> constexpr int enumCount<Clarity>() { return 4; }
template <> constexpr int enumCount<Channel>() { return 3; }
template <> constexpr int enumCount<CardKind>() { return 27; }       // Count itself is not a value
template <> constexpr int enumCount<WordContent>() { return 6; }
template <> constexpr int enumCount<WordOutcome>() { return 9; }
template <> constexpr int enumCount<GodTerm>() { return 5; }
template <> constexpr int enumCount<PeopleTerm>() { return 5; }
template <> constexpr int enumCount<PactState>() { return 7; }
template <> constexpr int enumCount<FigureKind>() { return 12; }
template <> constexpr int enumCount<MessengerArchetype>() { return 6; }
template <> constexpr int enumCount<FigureFate>() { return 5; }
template <> constexpr int enumCount<TrialKind>() { return 4; }
template <> constexpr int enumCount<CardState>() { return 4; }
template <> constexpr int enumCount<LegendMoral>() { return 6; }
template <> constexpr int enumCount<LegendCredit>() { return 5; }
template <> constexpr int enumCount<RivalState>() { return 5; }
template <> constexpr int enumCount<OmenKind>() { return 6; }
template <> constexpr int enumCount<WarningWay>() { return 6; }
template <> constexpr int enumCount<OmenResponse>() { return 5; }
template <> constexpr int enumCount<KeeperDomain>() { return 5; }
template <> constexpr int enumCount<Temper>() { return 2; }
template <> constexpr int enumCount<RevelationKind>() { return 5; }
template <> constexpr int enumCount<RevelationOutcome>() { return 4; }
template <> constexpr int enumCount<DeathBelief>() { return 5; }
template <> constexpr int enumCount<Certainty>() { return 4; }
template <> constexpr int enumCount<Charge>() { return 5; }
template <> constexpr int enumCount<BlessingKind>() { return 4; }
template <> constexpr int enumCount<Consequence>() { return 5; }
template <> constexpr int enumCount<JudgeBy>() { return 2; }
template <> constexpr int enumCount<Sentence>() { return 4; }
template <> constexpr int enumCount<Circumstance>() { return 6; }
template <> constexpr int enumCount<HeartKind>() { return 5; }
template <> constexpr int enumCount<DisputeKind>() { return 6; }
template <> constexpr int enumCount<DisputeAnswer>() { return 4; }
template <> constexpr int enumCount<RenewalKind>() { return 3; }
template <> constexpr int enumCount<TraitLevel>() { return 3; }
template <> constexpr int enumCount<CardSource>() { return 9; }
template <> constexpr int enumCount<OptionWhy>() { return 6; }

// Lower-case strings (multi-word values hyphenated: "obedient-empire", "shape-people", "further-shore",
// "last-door", "world-shape", "house-read"); fromStr parses exactly what toStr prints. Defined in Schema.cpp.
const char* toStr(Presence v);           bool fromStr(std::string_view s, Presence& out);
const char* toStr(Aspect v);             bool fromStr(std::string_view s, Aspect& out);
const char* toStr(Archetype v);          bool fromStr(std::string_view s, Archetype& out);
const char* toStr(Legacy v);             bool fromStr(std::string_view s, Legacy& out);
const char* toStr(DecreeArea v);         bool fromStr(std::string_view s, DecreeArea& out);
const char* toStr(Clarity v);            bool fromStr(std::string_view s, Clarity& out);
const char* toStr(Channel v);            bool fromStr(std::string_view s, Channel& out);
const char* toStr(CardKind v);           bool fromStr(std::string_view s, CardKind& out);
const char* toStr(WordContent v);        bool fromStr(std::string_view s, WordContent& out);
const char* toStr(WordOutcome v);        bool fromStr(std::string_view s, WordOutcome& out);
const char* toStr(GodTerm v);            bool fromStr(std::string_view s, GodTerm& out);
const char* toStr(PeopleTerm v);         bool fromStr(std::string_view s, PeopleTerm& out);
const char* toStr(PactState v);          bool fromStr(std::string_view s, PactState& out);
const char* toStr(FigureKind v);         bool fromStr(std::string_view s, FigureKind& out);
const char* toStr(MessengerArchetype v); bool fromStr(std::string_view s, MessengerArchetype& out);
const char* toStr(FigureFate v);         bool fromStr(std::string_view s, FigureFate& out);
const char* toStr(TrialKind v);          bool fromStr(std::string_view s, TrialKind& out);
const char* toStr(CardState v);          bool fromStr(std::string_view s, CardState& out);
const char* toStr(LegendMoral v);        bool fromStr(std::string_view s, LegendMoral& out);
const char* toStr(LegendCredit v);       bool fromStr(std::string_view s, LegendCredit& out);
const char* toStr(RivalState v);         bool fromStr(std::string_view s, RivalState& out);
const char* toStr(OmenKind v);           bool fromStr(std::string_view s, OmenKind& out);
const char* toStr(WarningWay v);         bool fromStr(std::string_view s, WarningWay& out);
const char* toStr(OmenResponse v);       bool fromStr(std::string_view s, OmenResponse& out);
const char* toStr(KeeperDomain v);       bool fromStr(std::string_view s, KeeperDomain& out);
const char* toStr(Temper v);             bool fromStr(std::string_view s, Temper& out);
const char* toStr(RevelationKind v);     bool fromStr(std::string_view s, RevelationKind& out);
const char* toStr(RevelationOutcome v);  bool fromStr(std::string_view s, RevelationOutcome& out);
const char* toStr(DeathBelief v);        bool fromStr(std::string_view s, DeathBelief& out);
const char* toStr(Certainty v);          bool fromStr(std::string_view s, Certainty& out);
const char* toStr(Charge v);             bool fromStr(std::string_view s, Charge& out);
const char* toStr(BlessingKind v);       bool fromStr(std::string_view s, BlessingKind& out);
const char* toStr(Consequence v);        bool fromStr(std::string_view s, Consequence& out);
const char* toStr(JudgeBy v);            bool fromStr(std::string_view s, JudgeBy& out);
const char* toStr(Sentence v);           bool fromStr(std::string_view s, Sentence& out);
const char* toStr(Circumstance v);       bool fromStr(std::string_view s, Circumstance& out);
const char* toStr(HeartKind v);          bool fromStr(std::string_view s, HeartKind& out);
const char* toStr(DisputeKind v);        bool fromStr(std::string_view s, DisputeKind& out);
const char* toStr(DisputeAnswer v);      bool fromStr(std::string_view s, DisputeAnswer& out);
const char* toStr(RenewalKind v);        bool fromStr(std::string_view s, RenewalKind& out);
const char* toStr(TraitLevel v);         bool fromStr(std::string_view s, TraitLevel& out);
const char* toStr(CardSource v);         bool fromStr(std::string_view s, CardSource& out);
const char* toStr(OptionWhy v);          bool fromStr(std::string_view s, OptionWhy& out);

// ------------------------------------------------------------------ persisted records (SPEC-MYTHOS 1.2)

// One per culture ever born (index = culture id). Defaults are the starting values of 3.4.
struct Faith {
	Aspect aspect = Aspect::Unknown; double aspectSince = 0;
	std::array<double, 4> image{};          // fading Parent, Ruler, Mystery, Threat impressions (3.3)
	double agency = 0.3, obedience = 0.5, dependence = 0.3, selfReliance = 0;
	double clarity = 0.5;                   // fading mean clarity of what the god has said (Exact 1 .. Sign 0)
	double visible = 0;                     // fading count of witnessed deeds (presence felt)
	double judgedBy = 0;                    // -1 deeds .. +1 hearts (3.11)
	double mercy = 0, sternness = 0;        // fading sentences: forgiven / punished (3.12)
	double survivorLove = 0, survivorFear = 0, survivorUntil = -1;   // the living generation's memory (3.12)
	int8_t death = 0, deathSure = 0;        // DeathBelief, Certainty (3.27)
	double bearer = -1, superiority = 0; int8_t charge = 0;   // the Bearers (3.24); bearer = year chosen, -1 none
	double grown = -1;                      // year they came of age (3.28), -1 not yet
	NumBag data;                            // the writers' private extras (Mythos; Doctrine / Faiths for their fields)
};

// How mythos reaches the rest of the sim (1.4): multipliers and offsets per culture, recomputed yearly by
// Mythos, read by each owner at the one place its rule needs it. The defaults are NEUTRAL: with them every
// owner reproduces v3.1 exactly.
struct Factors {
	// Economy (civ::yearly)
	double growthR = 1, K = 1, famineLoss = 1, plagueDeath = 1, research = 1, expandCooldown = 1;
	double farmConvert = 1;                 // farm[] growth rate on new cells
	bool   farmAncient = true;              // may old-growth cells be farmed (3.22)
	bool   claimAncient = true;             // do territories include old-growth cells (3.22)
	// Society (society::yearly / witness)
	double devotionBase = 0, loveTarget = 0, prayerChance = 1, answeredGain = 1, unansweredPenalty = 1;
	double aweS = 1;                        // the culture's weight in Awe income S
	double schism = 1;                      // estrangement and secession pace
	// Diplomacy (society::diplomacy / onArrive)
	double warV = 0;                        // added to DiplomacyTune::warV (negative = harder to start a war)
	double aggression = 0, battle = 1, defence = 1, relDrift = 0, plagueHop = 1, warStrength = 1, tradeEvery = 1;
	// Disasters (powers::natural) and Climate
	double naturalRate = 1, harmTaken = 1;  // harmTaken: damage from war, disaster and famine
	double co2Uptake = 1;                   // Climate: vegetation's CO2 uptake on the culture's land (the World rite, 3.13)
	std::array<double, 6> expect{ 1, 1, 1, 1, 1, 1 };   // prayer chance by PrayerKind (living legends, 3.15)
	bool operator==(const Factors&) const = default;
};

// Settlement-level effects (blessings, the Remnant, preparation), read through mythos::townFactors. Neutral = 1.
struct TownFactors {
	double K = 1, growthR = 1, harmTaken = 1, plagueDeath = 1;
	bool operator==(const TownFactors&) const = default;
};

// A culture's decree in one area (3.6), or its custom (custom = true: half strength, never breached).
struct Decree {
	int culture = -1;
	DecreeArea area = DecreeArea::Violence;
	int8_t stance = -1;
	Clarity clarity = Clarity::Exact;
	double since = 0, kept = 0;
	int by = -1;                            // the messenger figure it came through, -1 none
	bool custom = false;
	NumBag data;
};

// One word of the god (3.9). [last 64]
struct Utterance {
	int id = 0, culture = -1, figure = -1;
	uint8_t content = 0;                    // WordContent
	Clarity clarity = Clarity::Exact;
	Channel channel = Channel::Messenger;
	double year = 0, fidelity = 0;
	int8_t meant = -1, heard = -1;          // stances meant and heard (Decree / Repeal), -1 none
	uint8_t outcome = 0;                    // WordOutcome
	int8_t area = -1;                       // DecreeArea of a Decree / Repeal, -1 none
	NumBag data;
};

// A pact between the god and one culture (3.14). [24]
struct Pact {
	int id = 0, culture = -1;
	uint8_t godTerm = 0, peopleTerm = 0;    // GodTerm, PeopleTerm
	double sworn = 0, until = -1;           // until -1: while the people last
	double godKept = 0, peopleKept = 0;     // fading records (half-life DoctrineTune::pactHalfLife)
	uint8_t state = 0;                      // PactState
	int figure = -1;                        // the messenger who carried it
	NumBag data;
};

// A named person (3.8, 3.16, 3.18, 3.21, 3.23, 4). [160]
struct Figure {
	int id = 0;
	uint8_t kind = 0, archetype = 0;        // FigureKind, MessengerArchetype
	int culture = -1, home = -1;            // home = settlement id
	std::string name, byname;
	double born = 0, died = -1, credibility = 0, reach = 0, fame = 0;
	std::array<float, 4> person{};          // zeal, gentleness, cunning, pride
	std::array<float, 3> heart{};           // integrity, humility, faith: hidden until looked into
	uint8_t fate = 0;                       // FigureFate
	uint16_t flags = 0;                     // FigureFlag bits
	int trial = -1;                         // TrialKind while a trial runs, -1 none
	NumBag data;
};

// A card (2.2). choice / lapse are answer indices of the kind (cards::kCardKinds, SPEC-MYTHOS 2.3 order);
// lapse = -1: the source's own lapse rule ("the people decide"). [open 2, closed last 48]
struct Card {
	int id = 0;
	CardKind kind = CardKind::ShapePeople;
	uint8_t state = 0;                      // CardState
	int culture = -1, other = -1, settlement = -1, figure = -1, cell = -1;
	double opened = 0, deadline = -1, severity = 0;   // deadline -1: open until answered
	int8_t choice = -1, lapse = -1;
	std::array<int, 4> ref{ -1, -1, -1, -1 };
	NumBag data;
};

// A story of a witnessed deed (3.15). [12 per culture, 72]
struct Legend {
	int id = 0, culture = -1, settlement = -1, cell = -1;
	std::string deed;                       // what happened (a deed key owned by Legends, e.g. "rain", "flood")
	double year = 0, truth = 0, told = 0, vitality = 1;
	uint8_t moral = 0, credit = 0, versions = 0;   // LegendMoral, LegendCredit, retellings that changed it
	int source = -1;                        // chronicle entry id, -1 none
	NumBag data;
};

// The House of the God of one culture (3.20).
struct House {
	int culture = -1;
	double strength = 0, wealth = 0, authority = 0, corruption = 0;
	double since = 0;
	NumBag data;
};

// A rival voice and the Way it may found (3.21). [12]
struct RivalVoice {
	int id = 0, culture = -1, figure = -1;
	double following = 0, since = 0;
	int way = -1;                           // culture id after a split, -1 none
	uint8_t state = 0;                      // RivalState
	NumBag data;
};

// A foreseen danger (3.17). [16]
struct Omen {
	int id = 0;
	uint8_t kind = 0;                       // OmenKind
	int cell = -1, culture = -1;
	double due = 0, power = 0, ang = 0;
	uint8_t warned = 0, response = 0;       // WarningWay, OmenResponse
	int figure = -1;                        // the warner, -1 none
	NumBag data;
};

// A guardian (3.26). [5]
struct Keeper {
	int id = 0;
	uint8_t domain = 0, disposition = 0;    // KeeperDomain, Temper
	int culture = -1;                       // the Keeper of a People's people (others: the naming culture)
	std::string name;
	double appointed = 0, loyalty = 0.8, will = 0.3;
	double rogueUntil = -1;
	NumBag data;
};

// A revelation given to a culture (3.25). [7 x cultures]
struct Revelation {
	int culture = -1;
	uint8_t kind = 0, outcome = 0;          // RevelationKind, RevelationOutcome
	double year = 0;
	bool early = false;
	NumBag data;
};

// (M0 addition) Per-settlement mythos state: blessings (Mythos) and the Remnant (Renewal). One record per
// settlement that has any, ascending by settlement id.
struct TownMyth {
	int settlement = -1;
	BlessingKind blessing = BlessingKind::None;
	double blessedSince = 0, blessedUntil = -1;
	int healer = -1;                        // the Healer figure of A healer's gift, -1 none
	double remnantUntil = -1;               // Renewal: the Remnant's protection / memory
	NumBag data;
};

// (M0 addition) One act of the god (or a natural event) as Mythos's bus handlers recorded it (they only
// record, never act: 1.5). The yearly passes read and then drop what they used. [last 96]
struct MythAct {
	double year = 0;
	std::string kind;                       // power id, "nuke", or an event name ("quake", "prayer:answered", ...)
	int cell = -1, settlement = -1, culture = -1;
	double good = 0;
	bool natural = false;
};

// Everything mythos keeps in the World (World::mythos).
struct MythosState {
	Presence presence = Presence::Wonders; double presenceSince = 0; Archetype archetype = Archetype::Balanced;
	std::vector<Faith> faith; std::vector<Factors> factors;   // index = culture id
	std::vector<Decree> decrees; std::vector<Utterance> words; std::vector<Pact> pacts;
	std::vector<Figure> figures; std::vector<Card> cards; std::vector<Legend> legends;
	std::vector<House> houses; std::vector<RivalVoice> rivals; std::vector<Omen> omens;
	std::vector<Keeper> keepers; std::vector<Revelation> revelations;
	std::vector<TownMyth> towns;                              // (M0) ascending settlement id
	std::vector<MythAct> acts;                                    // (M0) the recent acts, oldest first
	double withdrawnUntil = -1; bool withdrawAnnounced = false;
	int renewal = 0; double renewedYear = -1;                 // 0 none, 1 water, 2 burning
	NumBag num;                                               // module counters, keys prefixed "mythos_", "cards_", ...
	int nextId = 1;
};

// ------------------------------------------------------------------ shared option and result types

// What the god says or gives with a Word power (powers::UseOpts::word; the chooser fills it). Fields that
// do not apply to the power or content are ignored.
struct WordOpts {
	// speak (3.9)
	WordContent content = WordContent::Decree;
	DecreeArea area = DecreeArea::Count;    // Decree / Repeal
	int stance = -1;                        // the area's stance (Decree)
	Clarity clarity = Clarity::Dream;
	Channel channel = Channel::Dream;
	int figure = -1;                        // through whom (a messenger figure id), -1 none
	int ref = -1;                           // what it answers or warns of: a settlement (Answer), an omen id (Warning) ...
	GodTerm godTerm = GodTerm::Rain;        // Promise (3.14)
	PeopleTerm peopleTerm = PeopleTerm::KeepDecree;
	double years = 0;                       // Promise: the pact's span (0 = while the people last)
	// bless (3.16), test (3.18), reveal (3.25), renew (3.30)
	BlessingKind blessing = BlessingKind::None;
	TrialKind trial = TrialKind::Power;
	RevelationKind revelation = RevelationKind::Agriculture;
	RenewalKind renewal = RenewalKind::None;
	std::array<int, 3> remnant{ -1, -1, -1 };   // settlement ids spared by a renewal
};

// One answer of an open card as the client shows it (cards::options).
struct CardOption {
	int choice = -1;
	double cost = 0;                        // Awe, paid through powers::pay when answered
	bool ok = false;
	OptionWhy why = OptionWhy::Unavailable;
};

} // namespace dm
