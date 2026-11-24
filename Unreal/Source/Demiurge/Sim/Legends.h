// DEMIURGE — Sim/Legends.h : stories of witnessed deeds, their drift, retelling and fading (SPEC-MYTHOS 3.15),
// and the Old Stories a reseeded world inherits (3.30).
// Owner: legends track (M10, key "legends"): Sim/Legends.cpp, SimTests/test_legends*.cpp,
// SimTests/stubs/Legends_stub.cpp. Header written and FROZEN by the contracts track (M0, 2026-11-20).
//
// Owns: mythos.legends (other modules ask for a legend through legends::add). Mythos folds the legends'
// effects into Factors (devotion, expect[], obedience, love). [12 living per culture, 72 in all]
// Events: legend. Phase < 2: everything neutral.
#pragma once

#include <vector>

#include "State.h"

namespace dm {

struct LegendsTune {
	double bornMin = 0.15; int perCentury = 3, perCulture = 12, maxLegends = 72;
	double retellYears = 50, fade = 0.08, fadeArt = 0.7, fadeHouse = 0.8, fadeWriting = 0.5, renew = 0.25, forget = 0.1;
	double grow = 0.12, growCap = 10, creditMessenger = 0.2, creditRival = 0.3, creditNature = 0.15, creditAncestor = 0.1;
	double devotionPer = 0.02, devotionCap = 0.08, expect = 1.2, obeyPer = 0.03, obeyCap = 0.09, lovePer = 0.02, loveCap = 0.06;
	double strongHouse = 0.5, ancestorYears = 1000, renewalVitality = 2, renewalFloor = 0.5, grewLineYears = 300;   // (M0) 3.15 / 3.30
};

namespace legends {

// The yearly pass: birth from Mythos's recorded acts, the 50-year retelling (fade, growth, credit shifts, moral
// drift), forgetting, inheritance at schisms. Called by mythos::yearly.
void yearly(World& w);
// A legend asked for by another module (a dramatic card answer, a martyr, a vindication, a pact, a renewal):
// seed.culture / deed / truth / moral / credit / cell / settlement / source are used, the rest is filled. The id,
// or -1 (caps, phase, bad culture).
int add(World& w, const Legend& seed);
// The culture's strongest living legends (told x vitality, ties by id), at most count; -1 = every culture.
std::vector<int> strongest(const World& w, int cultureId, int count);
// Copy up to count of the old world's strongest legends into a fresh world as Old Stories (credit Ancestors,
// vitality 1, culture -1 until a culture wakes and inherits them; 3.30). Returns how many.
int importOld(World& fresh, const World& old, int count);

} // namespace legends
} // namespace dm
