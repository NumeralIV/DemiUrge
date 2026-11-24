// DEMIURGE — Sim/Keepers.h : guardians with domains, tempers, loyalty and will (SPEC-MYTHOS 3.26).
// Owner: keepers track (M13, key "keepers"): Sim/Keepers.cpp, SimTests/test_keepers*.cpp,
// SimTests/stubs/Keepers_stub.cpp. Header written and FROZEN by the contracts track (M0, 2026-11-20).
//
// Owns: mythos.keepers. Keepers act each year by their domain and temper (budgeted, witnessed as the god's at
// half faith), pay their upkeep through powers::pay, compare the god's recent acts (mythos.acts) with their
// temper and disagree through KeeperDispute cards. No figures: each Keeper is a phenomenon. [5]
// Events: keeper. Phase < 3: everything neutral.
#pragma once

#include <array>
#include <vector>

#include "State.h"

namespace dm {

struct KeepersTune {
	double appointCost = 200, upkeep = 2, loyaltyStart = 0.8, willStart = 0.3, willGrow = 0.002;
	std::array<int, 7> slots{ 0, 0, 1, 3, 5, 5, 5 };
	double clash = -0.15, mend = 0.01, disputeBelow = 0.7, rogueBelow = 0.2, rogueYears = 50, grantYears = 100;
	double grantLoyalty = 0.2, refuseLoyalty = -0.1, temperCost = 100, temperLoyalty = -0.2, dismissGap = 100;
	double gentleDeath = 0.85, sternPlague = 0.1, gentleSea = 0.6, sternFleet = 0.3, actFaith = 0.5;
	double temperWill = -0.1, refuseRuler = 0.1, dismissMystery = 0.2, rogueThreat = 0.1, peopleAnswer = 0.5;   // (M0) 3.26
};

namespace keepers {

// The yearly pass (upkeep, acts by domain and temper, will, loyalty, clashes, rogue periods). Called by
// mythos::yearly.
void yearly(World& w);
// Appoint a Keeper (200 Awe through powers::pay, a free slot of the age). cultureId: the Keeper of a People's
// people (else the culture whose language names it, -1 = the oldest living). The keeper id, or -1.
int appoint(World& w, KeeperDomain domain, Temper temper, int cultureId);
// Dismiss a Keeper (its slot frees after dismissGap years). False for a bad id.
bool dismiss(World& w, int id);
// Change a Keeper's temper (temperCost Awe; loyalty and will fall). False when refused.
bool setTemper(World& w, int id, Temper temper);
// Keeper slots of the world's age (KeepersTune::slots[w.age]); 0 at phase < 3.
int slots(const World& w);

// ---- card handlers (KeeperDispute)
bool answer(World& w, int cardId, int choice);
void lapse(World& w, int cardId);
std::vector<CardOption> options(const World& w, int cardId);

} // namespace keepers
} // namespace dm
