// DEMIURGE — Sim/Mythos.h : the mythos core (SPEC-MYTHOS 1.1, 3.1-3.5, 3.16, 3.19, 3.28, 3.29, 4).
// Owner: mythos-core track (M1 / M1b / M1c, key "mythos"): Sim/Mythos.cpp, SimTests/test_mythos*.cpp,
// SimTests/stubs/Mythos_stub.cpp. Header written and FROZEN by the contracts track (M0, 2026-11-20); a missing
// declaration is reported to the lead, never added by a later track.
//
// Owns (MythosTypes.h): mythos.presence / presenceSince, faith[*] (aspect, image, agency, obedience, dependence,
// selfReliance, clarity, visible, judgedBy, mercy, sternness, survivor*, grown), factors, towns[*].blessing*,
// withdrawnUntil / withdrawAnnounced, acts. Mythos never writes a settlement, culture, relation or cell field:
// every effect goes through Factors (read by the owner) or an owner API (society::nudge / remember / shapeTraits,
// powers::pay, civ::cure ...).
//
// Gates: MythosTune::phase 0 (the default) = every function below is neutral and every Factors record is
// Factors{} (v3.1 bit for bit); phase 1 / 2 / 3 = the v1 / v2 / v3 systems. MythosTune::emergent (default false)
// gates the rules that move a world whose god never touches the mythos systems (SPEC-MYTHOS 1.6, 5.1).
//
// Schedule (1.5): sim::use calls attach (before chronicle::attach) and rebuild (after sim::rederive);
// the yearly group calls mythos::yearly after society::yearly and before cards::yearly; mythos::yearly calls
// doctrine, characters, faiths, omens, legends, keepers, renewal ::yearly (only for modules whose phase is on),
// then its own pass (faith, factors, consequences). sim::checkEnding fills ending.legacy from legacy().
// Randomness: forks only, w.rng.fork("myth:<module>:<what>:<id>:<year>"); never w.rng itself (1.10).
#pragma once

#include <array>
#include <string_view>
#include <vector>

#include "State.h"

namespace dm {

// The archetype's parameters for the owners' generators plus its mythic seed (3.1). Neutral = Balanced.
// (M0: the mythic-seed fields after `marvels` are additions to the struct printed in 3.1.)
struct ArchetypeParams {
	double fert = 0, moistBias = 0, capacityK = 1, ridgeK = 1, shelfK = 1, islandK = 1,
		noiseK = 1, natural = 1, quakeVolcano = 1, drought = 1, foresightLead = 0, aweIncome = 1; int marvels = 0;
	int marvelsMax = 0;                      // Strange: 3-6 marvels (marvels .. marvelsMax)
	double legendBorn = 1;                   // Generous: legends of plenty born at x this threshold
	double dependence = 0, agency = 0;       // added to the will targets (3.4)
	double threatSeed = 0, mysterySeed = 0;  // impressions at the waking (3.3)
	double devotion = 0;                     // devotion baseline offset (Dangerous +0.03)
	double legendVitality = 0;               // Beautiful +0.1
	double marvelFidelity = 1, marvelAng = 0.2;   // Strange: Sign / Dream fidelity x this within marvelAng of a marvel
	double rivals = 1;                       // Strange: rival voices x 1.5
	bool artCustom = false;                  // Beautiful: the Art rite is the first custom
	bool stormRing = false;                  // Strange: a permanent storm ring over one ocean
};

// Society's witness multipliers by presence (3.2): all 1 under Wonders.
struct WitnessScale {
	double faith = 1, memory = 1, favour = 1, jaded = 1;
	bool operator==(const WitnessScale&) const = default;
};

// The legacy scores of section 4 (index = Legacy; [None] unused) and the winner.
struct LegacyScores {
	std::array<double, 6> score{};
	Legacy best = Legacy::None;
};

struct MythosTune {
	// ---- gates (1.6)
	int phase = 0;                           // 0 off (v3.1), 1 v1 systems, 2 v2, 3 v3
	bool emergent = false;                   // rules that move a passive world (balance owner, 5.1)
	// (B1 mythos-balance-v1) the emergent rule families of 5.1 rule 3, switched on one at a time by the balance
	// owner. A family acts only while `emergent` is on AND its bit is set; the default (every bit) keeps the meaning
	// `emergent` had before B1. Tuning only (never saved).
	enum Family : unsigned {
		FamCustoms = 1u << 0,                // the people's own customs (year 200, first shrine, a lapsed FirstDecree)
		FamAspect = 1u << 1,                 // aspect effects on every people (3.3), the fear rule by aspect
		FamAgency = 1u << 2,                 // agency's research and schism terms (3.4)
		FamDependence = 1u << 3,             // dependence's prayer, unanswered-penalty and research terms (3.4)
		FamTraits = 1u << 4,                 // the rolled traits of unshaped peoples folded into Factors (3.5)
		FamLapse = 1u << 5,                  // lapse effects of cards nobody answered (2.2: a competing-prayer lapse)
		FamPrayers = 1u << 6,                // the Victory and Sun prayers (3.10)
		FamAll = ~0u
	};
	unsigned families = FamAll;
	bool emergentOn(unsigned family) const { return emergent && (families & family) != 0; }
	// ---- archetypes (3.1), index = Archetype
	std::array<ArchetypeParams, 5> archetypes = makeArchetypes();
	// ---- presence (3.2)
	double msgFaithNear = 1.5, msgFaithFar = 0.6, msgNearAng = 0.15, msgCred = 0.05, msgWordCost = 0.6;
	double unseenFaith = 0.25, unseenMemory = 0.4, unseenAgency = 0.15, msgAgency = 0.05;
	double presenceCost = 100, exactSkyCost = 2;
	// ---- aspect (3.3)
	double imageHalfLife = 300, aspectMin = 0.15, aspectLead = 1.2, aspectHold = 40;
	double parentPray = 1.25, parentLove = 0.08, parentDepend = 0.1, rulerObey = 0.1, rulerHouse = 1.5,
		rulerAgency = -0.05, mysteryAgency = 0.08, mysteryJaded = 1.5, mysteryDevotion = -0.03,
		threatObey = 0.08, threatLove = -0.05;
	// (B1 mythos-balance-v1) the silence rule and presence felt, as balance knobs. Defaults = the M1 behaviour.
	double silenceYears = 1;                 // Mystery from silence (0.2 a century) once this long without a deed
	double wordVisible = 0;                  // a heard word adds clarityValue[clarity] x this to `visible`
	bool actDeed = false;                    // the god's words, decrees, judgements, pacts and answered cards are deeds
	// ---- will (3.4)
	double agencyDrift = 0.01, obedienceDrift = 0.02, dependenceDrift = 0.015;   // share of the gap per year
	double visibleHalfLife = 100, visibleRef = 20, clarityHalfLife = 200;
	std::array<double, 4> clarityValue{ 1.0, 0.65, 0.4, 0.15 };                  // Exact, Parable, Dream, Sign
	double researchAgency = 0.12, schismAgency = 0.6, dependPray = 0.5, dependPenalty = 0.8, dependResearch = 0.1;
	// (M0) the constants of the three target formulas of 3.4, as starting values
	double agencyBase = 0.25, agencyClarity = 0.35, agencyHidden = 0.25, agencyCurious = 0.10, agencyLiteracy = 0.15,
		agencyDepend = 0.15, agencyMin = 0.05, agencyMax = 0.95;
	double obedienceBase = 0.30, obedienceDevotion = 0.35, obedienceHouse = 0.15, obedienceBurden = 0.45,
		obedienceDecrees = 0.04, obedienceFreeDecrees = 3, obedienceMin = 0.05, obedienceMax = 0.95;
	double dependBase = 0.15, dependAnswered = 0.35, dependAnsweredRef = 8, dependBlessing = 0.15, dependParent = 0.10,
		dependAgency = 0.30, dependSelf = 0.30;
	// ---- the first people (3.5)
	std::array<double, 3> traitLevel{ 0.25, 0.5, 0.8 };                          // Low, Middling, High
	int maxHigh = 2;
	// ---- blessings (3.16)
	double prosperityCost = 80, healerCost = 120, enduranceCost = 100, prosperityK = 1.25, prosperityR = 1.2;
	double prosperityYears = 40, enduranceYears = 60, enduranceHarm = 0.5, healerRange = 8, healerDeath = 0.5;
	double consequenceEvery = 10, envyV = -0.01, conquestWar = 1.5, dependCard = 0.7, envyCard = -0.4;
	// ---- disputes (3.19)
	double disputeAgency = 0.4, disputeDeaths = 0.15, heavyStrict = 0.7, heavyBurden = 0.35;
	double explainCost = 30, demandCost = 20, explainAgency = 0.05, explainLove = 0.05, silenceLove = -0.03;
	double changeLove = 0.08, movedK = 1.5, demandObey = 0.12, demandLove = -0.06, demandAgency = -0.05;
	// ---- coming of age (3.28)
	double growK = 0.004, growDepend = 0.002, growPray = 0.6, growResearch = 0.1, growAwe = 0.3, growCard = 0.5;
	double blessGoing = 0.2, remindDepend = 0.1, holdDepend = 0.2, holdAgency = -0.1;
	// ---- withdrawal (3.29)
	double withdrawYears = 100, announceCost = 30, withdrawAgency = 0.004, neglectAnnounced = 0.5, withdrawMystery = 0.5;
	double keeperHalf = 0.5, gentleLove = 0.03, gloryDevotion = 0.1, gloryJaded = 0.3, gloryDepend = 0.1;

	// The table of 3.1 as defaults.
	static std::array<ArchetypeParams, 5> makeArchetypes() {
		std::array<ArchetypeParams, 5> a{};
		ArchetypeParams& g = a[size_t(Archetype::Generous)];
		g.fert = 0.15; g.moistBias = 0.05; g.capacityK = 1.1; g.natural = 0.6; g.legendBorn = 0.5;
		g.dependence = 0.05; g.agency = -0.03;
		ArchetypeParams& d = a[size_t(Archetype::Dangerous)];
		d.ridgeK = 1.3; d.fert = -0.05; d.natural = 1.6; d.quakeVolcano = 2; d.drought = 1.3; d.foresightLead = 10;
		d.threatSeed = 0.1; d.aweIncome = 1.1; d.devotion = 0.03;
		ArchetypeParams& b = a[size_t(Archetype::Beautiful)];
		b.shelfK = 1.3; b.islandK = 1.3; b.noiseK = 1.3; b.artCustom = true; b.legendVitality = 0.1;
		ArchetypeParams& s = a[size_t(Archetype::Strange)];
		s.marvels = 3; s.marvelsMax = 6; s.stormRing = true; s.mysterySeed = 0.1; s.marvelFidelity = 1.2; s.rivals = 1.5;
		return a;
	}
};

namespace mythos {

// ---- lifecycle (1.5)
// Subscribes Mythos's recorders to w.bus (tokens in w.scratch, re-subscribed exactly once per world; handlers
// only append to mythos.acts / counters, never act). Phase 0: subscribes nothing.
void attach(World& w);
// After a load or sim::use: one Faith (and one neutral Factors) per culture ever born, caches. Phase 0: nothing.
void rebuild(World& w);
// The yearly pass (submodules in the order of 1.5, then faith, factors, consequences). Phase 0: nothing.
void yearly(World& w);
// The legacy of the ending (4) for the Ark culture, or everyone for a silence. None at phase 0 / without an ending.
Legacy legacy(const World& w);
LegacyScores legacyScores(const World& w);

// ---- reads for the owners (read-only; never draw from w.rng; neutral for bad ids)
// The culture's Factors (1.4): the stored record, or Factors{} for a bad id or before the first yearly pass.
Factors factors(const World& w, int cultureId);
// Settlement-level factors (blessings, the Remnant, preparation): neutral TownFactors{} where none apply.
TownFactors townFactors(const World& w, int sid);
// Society's witness multipliers for an act of `kind` (power id) at cell under the presence stance (3.2).
WitnessScale witnessScale(const World& w, int cell, std::string_view kind);
// The culture's aspect (3.3); Unknown for a bad id.
Aspect aspect(const World& w, int cultureId);
// The settlement the culture's armies covet first (a blessed town, 3.16), or -1.
int warGoal(const World& w, int cultureId);
// Multiplier on the culture's neglect accrual (withdrawal 3.29, coming of age 3.28); 1 = neutral.
double neglectScale(const World& w, int cultureId);
// The world's archetype parameters (the table entry of settings.archetype); Balanced = neutral.
ArchetypeParams archetypeParams(const World& w);
// The culture's rite (its Worship decree or custom), Rite::None if it has none.
Rite rite(const World& w, int cultureId);
// The culture's Faith record, or nullptr.
const Faith* faith(const World& w, int cultureId);

// ---- the god's choices
// Change the presence stance (3.2): once per world age, presenceCost Awe through powers::pay; Walk is always
// refused. False when refused (nothing changes).
bool setPresence(World& w, Presence p);
// A century of silence (3.29); announced = a Sign or Dream first (announceCost). False when refused.
bool withdraw(World& w, bool announced);
// Bless a town (power `bless`, 3.16). False when refused (nothing is spent).
bool bless(World& w, int sid, BlessingKind kind);

// ---- card handlers (Cards dispatches the kinds whose source is Mythos: ShapePeople, Presence, Blessing,
// Dispute, ComingOfAge, SilenceReport). answer: apply `choice` (cards::kCardKinds order); false = refused (the
// card stays open). lapse: apply the card's lapse rule. options: the answers as the client shows them.
bool answer(World& w, int cardId, int choice);
void lapse(World& w, int cardId);
std::vector<CardOption> options(const World& w, int cardId);

} // namespace mythos
} // namespace dm
