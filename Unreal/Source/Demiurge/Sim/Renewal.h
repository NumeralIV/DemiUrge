// DEMIURGE — Sim/Renewal.h : the Great Water and the Great Burning, the Remnant, and a new game from this world
// (SPEC-MYTHOS 3.30).
// Owner: renewal track (M14, key "renewal"): Sim/Renewal.cpp, SimTests/test_renewal*.cpp,
// SimTests/stubs/Renewal_stub.cpp. Header written and FROZEN by the contracts track (M0, 2026-11-20).
//
// Owns: mythos.renewal, mythos.renewedYear, towns[*].remnantUntil. The disasters themselves are Disasters'
// (DisasterType Deluge / Burning, added by the powers-v4c track); the damage goes through civ::damage / destroy
// (Cause::Deluge / Burning), the science floor through civ::addScience, the fires through Life.
// Events: renewal. Phase < 3: everything neutral.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "State.h"

namespace dm {

struct RenewalTune {
	double minAwe = 600, delugeHeight = 1.2, delugeYears = 6, delugeLoss = 0.6, burnDryYears = 4, burnMoist = 0.3;
	double burnShare = 0.6, burnLoss = 0.7, ashFert = 0.3, remnantPop = 50, remnantYears = 150, remnantObey = 0.2;
	int maxRemnant = 3, oldStories = 6;
	double burnDestroyBelow = 3, siltFert = 0.3, remnantLoveWarned = 0.1, remnantLoveUnwarned = -0.1;   // (M0) 3.30
	double threatAll = 1.0, rulerRemnant = 0.5;                                                        // (M0) 3.30
};

namespace renewal {

// Options of "Begin again from this world" (renewal::reseed).
struct ReseedOpts {
	std::string name;                        // the new world's name ("" = a fresh name from Names)
	bool ruins = true;                       // keep the old urban field as ruins
	int oldStories = -1;                     // legends to import (-1 = RenewalTune::oldStories)
};

// The yearly pass during and after a renewal (the Remnant's protection and memory, the end). Called by
// mythos::yearly.
void yearly(World& w);
// Power `renew` (once per world; all Awe, at least minAwe): the kind and up to maxRemnant settlements spared.
// False when refused (nothing is spent).
bool begin(World& w, RenewalKind kind, const std::vector<int>& remnant);
// A renewal is under way (its disaster has not ended).
bool active(const World& w);
// Settlement sid is (or was) of the Remnant and still protected.
bool remnant(const World& w, int sid);
// A fresh world for "Begin again from this world": the same seed and terrain (elev), the current sea level as
// the datum, veg filled, old urban as ruins, the old name kept, Old Stories imported, phase People, year 0. The
// old world is not changed. nullptr when refused (phase < 3, no history).
std::unique_ptr<World> reseed(const World& old, const ReseedOpts& opts);

// ---- card handlers (Remnant)
bool answer(World& w, int cardId, int choice);
void lapse(World& w, int cardId);
std::vector<CardOption> options(const World& w, int cardId);

} // namespace renewal
} // namespace dm
