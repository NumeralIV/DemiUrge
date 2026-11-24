// DEMIURGE — Sim/Faiths.h : the House of the God, rival voices and the Ways they found, the Bearers
// (SPEC-MYTHOS 3.20, 3.21, 3.24).
// Owner: faiths track (M12 / M12b, key "faiths"): Sim/Faiths.cpp, SimTests/test_faiths*.cpp,
// SimTests/stubs/Faiths_stub.cpp. Header written and FROZEN by the contracts track (M0, 2026-11-20).
//
// Owns: mythos.houses, mythos.rivals, faith[*].bearer / superiority / charge. Rival voices' figures are created
// through characters::spawn; a split goes through society::secede. Other modules move a House only through
// adjustHouse and start a rival voice only through seedRival. [12 rival voices]
// Events: house, rival, bearers. Phase < 2: everything neutral.
#pragma once

#include <vector>

#include "State.h"

namespace dm {

struct FaithsTune {
	// ---- the House (3.20)
	double strengthDrift = 0.02, authorityStep = 0.01, wealthStep = 0.02, wealthCost = 0.01, corruptK = 0.006;
	double corruptReform = 0.004, corruptCriminal = 0.01, corruptLowWealth = 0.002, corruptCard = 0.5;
	double houseAwe = 0.3, houseSteady = 0.3, houseObey = 0.15, corruptLove = -0.04, corruptK2 = 0.05, corruptRival = 2;
	double cleanseCorrupt = -0.4, cleanseAuthority = -0.3, reformYears = 20, endorseAuthority = 0.2, abandonDevotion = -0.1;
	double cleanseWealth = -0.5, cleanseLove = -0.04, endorseCorrupt = 0.1, endorseObey = 0.05, endorseDevotion = 0.03;   // (M0) 3.20 answers
	double abandonAgency = 0.15, abandonShrineYears = 100, reformCred = 0.3, reformCost = 50, cleanseCost = 15;          // (M0) 3.20 answers
	// ---- rival voices (3.21)
	double rivalRoll = 0.02, rivalTownShare = 0.08, rivalGrow = 0.02, rivalNear = 0.3, rivalDevotion = 0.01;
	double rivalSplit = 0.5, rivalCard = 0.15, exposeCost = 60, exposeBase = 0.5, exposeK = 0.4, martyrF = 0.15;
	double contestEvery = 10, contestK = 0.1, trustBase = 0.08, trustAgency = 0.12, trustTruth = 0.05, rivalLife = 80;
	double successor = 0.3, wayDevotion = 0.5, wayYears = 300; int maxRivals = 12;
	double cunningGrow = 1.3, truthGrow = 0.7, exposeF = -0.3, martyrDevotion = -0.05, wayThreat = 0.3, trustAgencyGain = 0.03;   // (M0) 3.21
	// ---- the Bearers (3.24)
	double bearersCost = 300, bearerDevotion = 0.1, bearerAwe = 1.3, carryEvery = 20, gardenUptake = 1.2;
	double peaceOmen = 0.5, starsResearch = 1.1, starsArk = 1.15, supGrow = 0.004, supCompassion = 0.6, supClosed = 1.5;
	double supRel = -0.02, supCrusade = 0.6, supCard = 0.5, rebuke = -0.3, renew = 0.05, jealousy = -0.2;
};

namespace faiths {

// The yearly pass (House drift, rival voices rise / grow / contest / split / die, the Bearers' superiority and
// charge); proposes Institution, RivalVoice, Bearers cards. Called by mythos::yearly.
void yearly(World& w);
// The culture's House, or nullptr.
const House* house(const World& w, int cultureId);
// Move a culture's House (Signs read by the House, exploited warnings, hoarded revelations, a tyrant's trial,
// the Queen's authority ...): each value clamped 0..1. Creates the House if missing. False for a bad culture.
bool adjustHouse(World& w, int cultureId, double dStrength, double dWealth, double dAuthority, double dCorruption);
// Start (or strengthen) a rival voice in settlement sid of the culture with that following (a sect or rival
// reading of the Word, a revelation's secret school). The rival voice id, or -1.
int seedRival(World& w, int cultureId, int sid, double following);
// Choose the Bearers and their charge (300 Awe through powers::pay, once per world age). False when refused.
bool chooseBearers(World& w, int cultureId, Charge charge);
// The culture that bears the charge now, or -1.
int bearers(const World& w);

// ---- card handlers (Institution, RivalVoice, Bearers)
bool answer(World& w, int cardId, int choice);
void lapse(World& w, int cardId);
std::vector<CardOption> options(const World& w, int cardId);

} // namespace faiths
} // namespace dm
