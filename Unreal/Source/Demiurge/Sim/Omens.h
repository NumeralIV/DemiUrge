// DEMIURGE — Sim/Omens.h : the foresight queue of coming disasters, warnings and the people's responses
// (SPEC-MYTHOS 3.17).
// Owner: omens track (M11, key "omens"): Sim/Omens.cpp, SimTests/test_omens*.cpp, SimTests/stubs/Omens_stub.cpp.
// Header written and FROZEN by the contracts track (M0, 2026-11-20).
//
// Owns: mythos.omens. Disasters reports each large natural event it schedules ahead through foresee and fires it
// at `due` (its own schedule), then reports the strike through struck; with foresightOn false (phase < 2, or the
// emergent "foresight" family off) Disasters fires at once exactly as today. [16 omens]
// Events: omen. Phase < 2: everything neutral.
#pragma once

#include <vector>

#include "State.h"

namespace dm {

struct OmensTune {
	double leadMin = 20, leadMax = 40, cardMinLead = 25, cultureGap = 300; int maxOmens = 16;
	double dreamCost = 20, starsCost = 60, harvestCost = 40, harvestK = 0.9, harvestYears = 10, harvestBelief = 0.2;
	double respondAfter = 10, prepareLoss = 0.4, prepareKeep = 30, exploitAuthority = 0.15, exploitCorrupt = 0.1;
	double exploitWar = 2, dismissCred = 0.3;
	double cardUntil = 10, repentObey = 0.1, repentDevotion = 0.05, prepareWeight = 0.8, exploitWeight = 0.6;   // (M0) 3.17
	double rebukeAuthority = -0.2, rebukeLove = -0.03, exploitWarYears = 20, spareParent = 0.3, strikeThreat = 0.5; // (M0) 3.17
};

namespace omens {

// The yearly pass: responses 10 years after a warning, card proposals for coming dangers, expiry. Called by
// mythos::yearly.
void yearly(World& w);
// Disasters: a natural event of `kind` will strike cell at year `due` with that power / radius. The omen id,
// or -1 (cap, phase, bad cell).
int foresee(World& w, OmenKind kind, int cell, double due, double power, double ang);
// The god's own planned judgement on settlement sid at year due, foretold by a word (speak, content Warning).
int foretell(World& w, int sid, double due, int utteranceId);
// Disasters: the foreseen event struck (or the god's judgement fell): resolves the omen (omen { struck }).
bool struck(World& w, int omenId);
// Send a warning (the Warning card's answers, or a Keeper of Sleep's dream). False when refused.
bool warn(World& w, int omenId, WarningWay way);
// Should Disasters schedule its large natural events ahead (foresight)? False at phase < 2 or with the
// foresight family off: then natural events fire at once, as today.
bool foresightOn(const World& w);

// ---- card handlers (Warning, WarningExploited)
bool answer(World& w, int cardId, int choice);
void lapse(World& w, int cardId);
std::vector<CardOption> options(const World& w, int cardId);

} // namespace omens
} // namespace dm
