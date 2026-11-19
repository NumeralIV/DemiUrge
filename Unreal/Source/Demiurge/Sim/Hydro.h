// DEMIURGE — Sim/Hydro.h : lakes, drainage, rivers, distance to fresh water (SPEC-SIM 3.4, DM.Hydro).
// Owner: hydro engineer (A4, key "hydro"). Files: Hydro.h, Hydro.cpp, SimTests/test_hydro.cpp,
// SimTests/stubs/Hydro_stub.cpp. Built fresh from the spec (no JS base). Header written by
// foundation-api; the owner may add, never rename.
//
// Writers (SPEC-SIM 1.1): water, flow, down, fresh, w.rivers. Nothing else (no rng draws, no events).
//
// What other modules can rely on after hydro::update (until elev, seaLevel or moist change):
//   down[c]   -1 on every sea cell and on the outlet of a world with no sea (its lowest cell, ties: lowest id);
//             otherwise a neighbour of c whose filled level is not above c's. Following down[] from any land cell
//             ends on a sea cell (or that outlet) and never cycles. A land cell next to the sea drains into its
//             lowest sea neighbour.
//   flow[c]   land: moist[c] + sum of flow over the cells that drain into c (rainfall in "moist-cells"), so it
//             never decreases downstream. Sea: sum of flow of the land cells draining straight into it (the
//             discharge of a river mouth), else 0. NaN / negative / infinite moist counts as no rain.
//   water[c]  min(2.5, filled - elev) on every cell of a lake's depression, 0 everywhere else (sea included).
//             A lake has >= 3 cells deeper than 0.03 or is >= 0.25 deep at its deepest point. A lake cell
//             (hydro::isLake: land, water > 0.03) is never a river cell.
//   rivers    one River { cells, flow } per source, largest discharge first (ties: source cell id);
//             cells run downstream along down[], flow[i] == w.flow[cells[i]], length >= 2. Every cell but the
//             last is a river cell; the last is the first sea cell, the first lake cell, or the confluence
//             cell on a bigger river (where two rivers meet, the one carrying more water continues). On a
//             world with no sea the main river ends on the outlet. A river leaving a lake starts on the dry
//             outlet cell next to it. Every river cell is a non-final cell of exactly one river.
//   fresh[c]  hops (through any cells, sea included) to the nearest river cell or lake cell, 0 exactly on
//             them; 255 if there is none within 254 hops.
#pragma once

#include "State.h"

namespace dm {

// Starting values from SPEC-SIM 3.4 (first block) and the owner's additions (second block).
struct HydroTune {
	int lakeMinCells = 3;          // a depression becomes a lake with >= this many cells (deeper than lakeCell) ...
	double lakeMinDepth = 0.25;    // ... or a maximum depth >= this ...
	double lakeMinMoist = 0.15;    // ... and its cells' mean moist >= this (or a counted river runs into it: fedLakes)
	double lakeMaxWater = 2.5;     // water[c] = min(lakeMaxWater, filled - elev[c])
	double lakeCell = 0.03;        // a lake cell is a land cell with water > lakeCell
	int freshCap = 255;            // fresh[] cap (hops)
	double budgetMs = 15;          // update cost at L = 6

	// riverT = max(riverMin, riverRef * (n / 40962) ^ riverExp): discharge (sum of upstream moist) that
	// makes a river cell. riverRef is the value on the reference world (L = 6).
	double riverRef = 8;
	double riverExp = 0.5;
	double riverMin = 1.5;
	// A river system (every cell that reaches the sea through the same mouth) counts only if its mouth
	// carries at least creek * riverT, and its catchment's mean moist is at least arid (desert washes
	// and coastal stubs are no rivers). creek = 1 and arid = 0 keep every stream.
	double creek = 3;
	double arid = 0.12;
	// Drowned trunks (verifier's addition): while seaLevel is above vars.seaBase, a land cell also counts when its
	// system counts on the drainage tree of the datum sea (a second drainage pass, only then): a rising sea
	// drowns the lower course of a river instead of erasing the tributaries that joined it. false = off.
	bool estuary = true;
	// Headwaters: from every source the course is followed upstream along the largest inflow while it
	// still carries more than head * riverT (never below riverMin). 0 = off.
	double head = 0.3;
	// Slope cells prefer a lower neighbour that already drains a stream: its drop counts up to
	// (1 + pull) times, half of that at pullAt reference cells drained, so neighbouring flow lines merge
	// into valleys. Water still only runs to a strictly lower (filled) neighbour. 0 = the plain flood.
	double pull = 4;
	double pullAt = 4;
	// Micro-relief: each slope choice is weighted by 1 +- jitter/2 (a fixed hash of the two cells), so flow lines
	// on perfectly smooth slopes do not run side by side forever (0 = off).
	double jitter = 0.4;
	// A dry depression that a counted river runs into fills up as a lake (the river does not climb out).
	bool fedLakes = true;
};

namespace hydro {

// Hydro.update: priority-flood from all sea cells over the land (binary heap, reused buffers) gives a
// filled surface and a drainage parent per land cell: down[c] = parent (-1 on sea cells). Lakes per
// HydroTune; smaller pits drain through (water = 0). Discharge from the highest filled level down:
// flow[c] = moist[c] + sum of upstream flow. Rivers: cells with flow > riverT; w.rivers = one
// { cells, flow } per river from its source downstream, ending (inclusive) on the first sea cell,
// lake cell or confluence. fresh[c] = hops to the nearest river or lake cell (multi-source BFS,
// cap 255). No sea: the lowest cell is the outlet. Clears need.hydro; sets need.life, dirty.rivers,
// dirty.water. Budget <= 15 ms at L = 6.
void update(World& w);
// Hydro.riverT: discharge above which a cell counts as a river (scales with cell count). Read-only.
double riverT(const World& w);
// (JS Hydro.isRiver) a river cell of the last update: a land cell, not a lake cell, with fresh == 0
// (on a river of w.rivers as a non-final cell; flow > riverT). Read-only.
bool isRiver(const World& w, int c);

// (JS Hydro.isLake) A lake cell is a land cell with water > 0.03 (SPEC-SIM 3.4). This is the
// definition every module uses (Life, Path, Civ...), so it is inline and works without Hydro.
inline bool isLake(const World& w, int c) {
	return w.valid(c) && !state::isSea(w, c) && w.water[size_t(c)] > 0.03f;
}

} // namespace hydro
} // namespace dm
