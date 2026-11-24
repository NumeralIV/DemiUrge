// DEMIURGE — Sim/Doctrine.h : decrees, customs, breaches and conflicts, the Word and its interpretation,
// judgement and sentences, the rite, pacts, the Land decree, revelations, beliefs about death (SPEC-MYTHOS
// 3.6, 3.7, 3.9, 3.11-3.14, 3.22, 3.25, 3.27).
// Owner: doctrine track (M3 / M3b / M3c, key "doctrine"): Sim/Doctrine.cpp, SimTests/test_doctrine*.cpp,
// SimTests/stubs/Doctrine_stub.cpp. Header written and FROZEN by the contracts track (M0, 2026-11-20).
//
// Owns: faith[*].death / deathSure, mythos.decrees, words, pacts, revelations. Effects reach the sim only through
// Mythos's Factors (Mythos folds decrees, rite, judgement doctrine and survivor memory in) or owner APIs.
// Events: decree, breach, word, pact, judgement, revelation. Phase 0: everything neutral.
#pragma once

#include <array>
#include <vector>

#include "State.h"

namespace dm {

struct DoctrineTune {
	// ---- decrees (3.6)
	double breachBase = 0.02, breachKept = -0.1, breachYears = 20, breachCard = 0.5, pressureIdle = 0.1;
	double customYear = 200, customStrength = 0.5, keptDrift = 0.05;
	// strictness per area and stance (3.6 table; index = DecreeArea, then the area's stance; the Worship rites
	// and unused slots are not in the spec: 0.5 until the balance owner sets them)
	std::array<std::array<double, 5>, 7> strictness{ {
		{ 0.9, 0.5, 0.2, 0, 0 },          // Violence: Peace, Defence, Valour
		{ 0.7, 0.5, 0.2, 0, 0 },          // Wealth: Share, Tithe, Gain
		{ 0.5, 0.5, 0.5, 0.5, 0.5 },      // Worship: Prayer, Temples, Kindness, Art, World
		{ 0.8, 0.4, 0.1, 0, 0 },          // Honesty: Truth, Discretion, Cunning
		{ 0.8, 0.5, 0.2, 0, 0 },          // Hospitality: Open, Guarded, Closed
		{ 0.8, 0.4, 0.2, 0, 0 },          // Care: Shelter, Kin, Strength
		{ 0.1, 0.5, 0.9, 0, 0 },          // Land: Dominion, Stewardship, Equal standing
	} };
	// ---- conflicts (3.7)
	double conflictKept = 0.4, conflictGap = 150, lapseKept = -0.1, lapseObey = -0.05;
	// ---- the word (3.9)
	std::array<double, 4> wordCost{ 60, 50, 30, 90 }, fidelityBase{ 0.95, 0.70, 0.55, 0.35 };
	double wordGap = 10, fidelityCap = 0.98, credFloor = 0.6, credK = 0.4, noMessengerCred = 0.5;
	double literalShare = 0.7, parableWisdom = 0.6, dreamPersonal = 0.5, dreamRival = 0.4, signHouse = 0.5, signRival = 0.3;
	double exactCat = 0.3, dreamCat = 0.1, signCat = 0.2, sectFollowing = 0.1, dreamFollowing = 0.15, signFollowing = 0.2;
	double exodusShare = 0.3;
	double truthFidelity = 0.1, discretionFidelity = 0.05, writingFidelity = 0.1, childFidelity = 0.15;   // (M0) 3.9 F terms
	double catastropheThreat = 0.2, heardCred = 0.05;                                                     // (M0) 3.9
	// ---- judgement (3.11)
	double lookCost = 20, judgedStep = 0.25, judgedHalfLife = 300, judgedLine = 0.3;
	double deedObey = 0.05, deedHouse = 1.2, deedAgency = -0.03, deedBreach = 0.9, heartAgency = 0.04, heartBreach = 1.1;
	// ---- sentences (3.12)
	double warnCost = 20, removeCost = 60, survivorYears = 70, mercyHalfLife = 500;
	double forgiveLove = 0.08, forgiveRepeat = -0.03, warnObey = 0.05, removeObey = 0.1, removeFear = -0.04;
	double survForgive = 0.05, survWarn = 0.04, survRemove = 0.06, survJust = 0.08, survJustLove = -0.04, survUnjust = -0.1;
	// ---- pacts (3.14)
	double pactMinYears = 100, pactMaxYears = 400, pactHalfLife = 300, godBrokeLove = -0.15, godBrokeThreat = 0.6;
	double godBrokeWrath = 0.5, grievanceYears = 600, keptLove = 0.1, keptDevotion = 0.05; int maxPacts = 24;
	// ---- the Land (3.22)
	double dominionFarm = 1.2, dominionK = 1.05, stewardK = 0.97, stewardHunger = 1.05, equalK = 0.9;
	double refuseCost = 30, groveCost = 10, refuseLove = -0.04;
	// ---- revelations (3.25)
	std::array<double, 5> revealCost{ 150, 150, 200, 200, 250 };
	std::array<int, 5> naturalAge{ 1, 1, 2, 3, 0 };   // Age::BRONZE, BRONZE, IRON, SAIL, any
	double earlyScience = 30, earlyMisuse = 2, outcomeAfter = 10, agriK = 1.12, writeResearch = 1.08, medicineDeath = 0.6;
	double worldAgency = 0.15, worldDepend = -0.1, worldResearch = 1.05, shameFlip = 0.5;
	// ---- death (3.27)
	double dyingShare = 0.2; std::array<double, 3> certainty{ 0.3, 0.6, 1.0 };
	double shoreDefence = 0.1, shoreObey = 0.08, shoreAggr = 0.05, turnFarm = 0.15, turnLove = 0.03, turnResearch = 0.03;
	double turnWeary = 0.2, returnDevotion = 0.05, returnDepend = 0.1, returnDispute = 0.5, sleepCompassion = 0.02;
	double sleepResearch = 0.05, sleepDevotion = -0.05, sleepFlight = 0.1, sleepFlightPlague = 0.4;
	// ---- caps (1.2)
	int maxWords = 64;
};

namespace doctrine {

// Why a word cannot be spoken now (canSpeak).
enum class WordWhy : uint8_t { Ok, Phase, Gap, NoMessenger, Content, Target, Awe };
inline const char* toStr(WordWhy y) {
	switch (y) {
	case WordWhy::Ok: return "ok"; case WordWhy::Phase: return "phase"; case WordWhy::Gap: return "gap";
	case WordWhy::NoMessenger: return "no-messenger"; case WordWhy::Content: return "content";
	case WordWhy::Target: return "target"; default: return "awe";
	}
}
struct SpeakCheck {
	bool ok = false;
	WordWhy why = WordWhy::Phase;
	double cost = 0;
};

// The yearly pass (decrees kept, customs, breaches, conflicts, words heard, judgement fading, pacts, revelations'
// outcomes, death beliefs); proposes cards. Called by mythos::yearly.
void yearly(World& w);
// The god speaks to a culture (power `speak`, 3.9): records the Utterance, decides the people's reading with a
// fork, applies it. The utterance id, or -1 when refused (nothing is spent: Powers pays only on success).
int speak(World& w, int cultureId, const WordOpts& o);
// Can that word be spoken now, and what does it cost (the one-word-per-10-years gate, channel needs, presence)?
SpeakCheck canSpeak(const World& w, int cultureId, const WordOpts& o);
// Its cost in Awe (3.9 table x presence, Exact from the sky x 2); 0 for a bad culture.
double wordCost(const World& w, int cultureId, const WordOpts& o);
// The fidelity F of 3.9 for that word (the chooser's "almost all will hear it"); 0 for a bad culture.
double fidelity(const World& w, int cultureId, const WordOpts& o);
// The culture's decree (or custom) in an area, or nullptr.
const Decree* decree(const World& w, int cultureId, DecreeArea area);
// How well it is kept (0 when none).
double kept(const World& w, int cultureId, DecreeArea area);
// Mean strictness of the culture's decrees (3.4 burden); 0 when none.
double burden(const World& w, int cultureId);
// DoctrineTune::strictness of an area's stance; 0 for a bad pair.
double strictness(const World& w, DecreeArea area, int stance);
// The custom the culture would take in an area from its traits (3.6, 3.13), or -1. Pure (no rng).
int customStance(const World& w, int cultureId, DecreeArea area);
// Swear a pact (PactOffer accepted, or speak Promise): the pact id, or -1.
int swearPact(World& w, int cultureId, GodTerm godTerm, PeopleTerm peopleTerm, double years, int figure);
// Give knowledge (power `reveal`, 3.25). False when refused.
bool reveal(World& w, int cultureId, RevelationKind kind, Clarity clarity);
// The culture's belief about death (3.27); None for a bad id.
DeathBelief deathBelief(const World& w, int cultureId);

// ---- card handlers (FirstDecree, DecreeConflict, Breach, Judgement, PactOffer, PactBroken, LandClaim,
// Revelation, RevelationMisuse, DeathQuestion)
bool answer(World& w, int cardId, int choice);
void lapse(World& w, int cardId);
std::vector<CardOption> options(const World& w, int cardId);

} // namespace doctrine
} // namespace dm
