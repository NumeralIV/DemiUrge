// DEMIURGE — Sim/Terrain.h : planet relief (SPEC-SIM 3.2, DM.Terrain).
// Owner: terrain engineer (A2, key "terrain"). Files: Terrain.h, Terrain.cpp, SimTests/test_terrain.cpp,
// SimTests/stubs/Terrain_stub.cpp. Port of js/sim/terrain.js (plates, continents, relief, erosion).
//
// Writers (SPEC-SIM 1.1): Terrain is the ONLY writer of elev (Powers calls Terrain for craters,
// cones, sculpting and lava rock); derive writes slope, coast and w.cache.landId / landCount.
// Every operation clamps elev to state::kElevMin .. kElevMax and never writes NaN.
// Terrain emits no events and never draws from w.rng (generate forks its own streams from w.seed;
// roughen draws only from the Rng passed in).
//
// Flags: sculpt / crater / cone / roughen call touched() themselves when they changed at least one
// cell (as the JS reference and BUILD.md A2 "sets the flags" require); calling touched() again is
// harmless (flags are only ever set to true). addElev does NOT call touched (lava cools cell by
// cell; the caller calls touched once per batch). A bad cell, a non-finite or zero amount, or a
// non-positive depth / height changes nothing and sets no flag.
#pragma once

#include <array>

#include "State.h"

namespace dm {

// TUNE.terrain of the JS reference (starting values; Balance may override them).
struct TerrainTune {
	// ---- generation: layout
	double land = 0.39;                  // land fraction at sea level 0 (target 36-42 %)
	int candidates = 12;                 // plate layouts scored on the level-4 cells (stops at the first with full marks)
	double scoreExtra = 0.04;            // extra land used when judging whether landmasses are separate
	int rebuilds = 2;                    // further layouts built when the finished world lost a continent to a land bridge
	double rateSecond = 0.034, rateThird = 0.02, rateTop = 0.62;   // a finished world is kept at once when its 2nd / 3rd
	                                     // landmasses hold these shares of the planet and the largest at most rateTop of the land
	int platesMin = 10, platesMax = 14;
	double contShare = 0.42;             // share of plates with continental crust
	double warpF = 1.15;                 // domain-warp frequency
	double warpPlate = 0.24;             // warp amplitude for plate edges
	double warpCont = 0.30;              // warp amplitude for the continent noise
	double contF = 1.35;                 // base frequency of the continent fBm
	int contOct = 5;
	double plateAmp = 0.85;              // weight of the plate crust bias against unit-variance continent noise
	double biasW = 0.12;                 // half-width (rad) over which the crust bias blends across a plate edge
	double coastF = 8.5;                 // coastline detail frequency
	double coastLo = 0.05, coastHi = 0.24;   // its amplitude on smooth and on ragged coasts
	// ---- generation: plate edges
	double convMin = 0.12, convSpan = 0.75;  // relative normal speed -> edge strength 0..1
	double beltC = 0.045, beltW = 0.085;     // mountain belt: centre distance from the edge, half-width (rad)
	double beltUnder = 0.55;                 // belt strength on the subducting side
	double wideW = 0.17;                     // plateau behind the belt
	double arcC = 0.065, arcW = 0.045;       // island arc (overriding side)
	double trenchC = 0.02, trenchW = 0.04;   // trench (subducting side)
	double riftW = 0.07, riftN = 0.028;      // spreading ridge half-width and its axial valley
	// ---- generation: fossil sutures (old worn ranges)
	int fossilMin = 7, fossilMax = 10;
	double warpFossil = 0.34;
	double oldW = 0.058;                     // half-width (rad)
	double oldCut = 0.5;                     // share of fossil edges that carry no range
	// ---- generation: relief amplitudes (world units)
	double lowSlope = 0.5, upland = 1.25;    // base land curve: 0.03 + lowSlope u + upland u^2 basin
	double swellF = 2.6, swellH = 0.9;       // broad interior swells and basins
	double tabF = 2.3, tabCut = 0.36, tabEdge = 0.07, tabH = 0.95;   // tablelands with crisp escarpments
	double beltH = 6.4, plateauH = 0.9, strikeF = 3.3;
	double oldH = 2.7;
	double ridgeF = 4.6, ridgeStretch = 3;
	double hillF = 7.5, hillLow = 0.04, hillHigh = 0.42, hillBelt = 0.5, hillRegion = 0.22;
	double shelfV = 0.16, shelfDepth = 0.5, slopeV = 0.31, slopeDepth = 3.4, abyssDepth = 3.0;
	double arcH = 5.4, arcSwell = 1.3, trenchH = 2.0, ridgeH = 1.2, riftH = 0.6;
	int chainsMin = 3, chainsMax = 5;        // hotspot island chains
	double cellsPerWave = 3;                 // noise octaves finer than this many cells per wavelength fade out
	// ---- generation: tidying and erosion
	double minSea = 0.0005;                  // enclosed seas smaller than this share of the planet become land basins
	std::array<double, 3> shelfLim = { 0.3, 0.95, 2.4 };   // deepest floor 1, 2, 3 hops from land on the widest shelves
	double shelfActive = 4;                  // ... times 1 + this at an active margin
	double speckKeep = 0.5;                  // share of one-cell islands that survive
	double erodeK = 0.10;                    // headward reach (rad) of a river draining erodeRef of the planet
	double erodeRef = 0.01;
	double erodeMin = 0.00006;               // catchments smaller than this share of the planet do not cut
	double erodeMax = 12;                    // cap on the reach, in cells
	double erodeSide = 0.45;                 // share of a river's reach passed on to the cells that drain into it
	double talus = 1.3;                      // steepest slope left along a drainage path (rise per unit of distance)
	double smoothTop = 1.0;                  // land below this height is smoothed (fully at the shore, not at the top)
	double smoothK = 0.55;
	// ---- generation: guarantees
	double lowQ = 0.32, lowE = 0.55;         // at least lowQ of the land ends below lowE (spec: >= 1/4 below +0.6)
	double midQ = 0.86;                      // land below this quantile is never stretched
	double highQ = 0.965, highE = 3.6;       // at least 1 - highQ of the land ends above highE
	double peakMin = 6.4, peakMax = 8.2;     // the highest summit (spec: +6 .. +8.5)
	double deepMin = 6.2, deepMax = 6.6;     // the deepest trench (spec: about -6.5)
	double deepKnee = 5.4;                   // only depths beyond this are rescaled to get there
	// ---- operations
	double minBrush = 0.8;                   // craters and cones are at least this many cell steps in radius
	double craterRim = 0.22;                 // rim height as a share of the depth (rim crest at 1.0-1.3 x ang)
	double craterFlat = 0.5;                 // how far the bowl is levelled toward the mean ground before digging
	double craterMinDrop = 0.5;              // every bowl cell ends at least this share of depth x bowl below its old level
	                                         // (verifier fix: the levelling may no longer fill a pit or a trench so far that
	                                         // an impact leaves its centre higher than before; 0 = the JS reference)
	double conePit = 0.25;                   // summit crater radius as a share of the cone radius (at least one cell)
	double conePitDepth = 0.25;              // its depth as a share of the cone height
	double coneLevel = 1;                    // how far a cone buries the ground under it toward the vent's level
	int roughPasses = 3;                     // smoothing passes over the random field of roughen
};

namespace terrain {

// Terrain.generate(world): fill elev (and a varied initial fert), deterministic from w.seed alone
// (w.rng is not advanced; the world's name, earlier calls and L do not change the layout).
// Targets at seaLevel 0: 36-42 % land; 3-6 major landmasses plus islands; mountain belts along
// convergent plate edges and old sutures; broad coastal lowlands; shelves; trenches and island
// arcs; floor to about -6.5; peaks +6.4 .. +8.2. Leaves every other field (and seaLevel) at its
// value and sets every dirty and need flag.
void generate(World& w);
// Terrain.sculpt(world, c, ang, amount) -> n: add amount x (1 - t^2)^2 (t = distance / ang) within
// ang of cell c (amount may be negative; ang <= 0 or NaN moves c alone; ang > pi is pi); returns
// the number of cells changed and calls touched() if it is > 0.
int sculpt(World& w, int c, double ang, double amount);
// Terrain.crater: the ground inside ang is half levelled toward its mean, then a bowl `depth` deep
// is dug (centre exactly depth below its old level on level ground; on rough ground every bowl cell
// still ends at least craterMinDrop x depth x (1 - t^2) below its old level, so the centre always
// drops by >= depth / 2 and an impact never raises it); a rim 0.22 x depth high
// crests at about 1.1 x ang; nothing moves beyond 1.5 x ang. Calls touched() when it changed cells.
void crater(World& w, int c, double ang, double depth);
// Terrain.cone: a volcanic cone built on the vent's level: crest ring `height` above elev[c],
// summit pit a quarter of that deep; never lowers a cell, nothing changes beyond ang. A cone
// narrower than ~1.5 cells is a plain one-cell peak. Calls touched() when it changed cells.
void cone(World& w, int c, double ang, double height);
// Terrain.roughen: smooth random +- displacement within ang (largest |change| = |amount|, fading to
// 0 at ang), then fills any NEW one-cell pit to its lowest neighbour (pits that existed before are
// kept). Draws only from `rng`. Calls touched() when it changed cells.
void roughen(World& w, int c, double ang, double amount, Rng& rng);
// C++ addition (needed by Disasters for lava cooling into rock, which SPEC-SIM routes "via
// Terrain"): add amount to elev[c] alone, clamped; returns the new elevation (0 for a bad cell; the
// old elevation for a non-finite amount). Does NOT call touched.
double addElev(World& w, int c, double amount);
// Terrain.touched: call after any elevation change: dirty.elev / water / color = true,
// need.derive / climate / hydro = true. Nothing else.
void touched(World& w);
// Terrain.derive: slope (largest |elev difference| to a neighbour), coast (hops to the nearest sea
// cell; 0 on sea, 255 cap / no sea) and landmass labels (w.cache.landId: -1 on sea, 0..k-1 by
// descending size, equal sizes by lowest cell id; w.cache.landCount = k); clears need.derive and
// sets need.life. Sea = elev < w.seaLevel.
void derive(World& w);

} // namespace terrain
} // namespace dm
