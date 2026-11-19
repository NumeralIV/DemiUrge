// DEMIURGE — Sim/Path.h : pathfinding on the cell graph (SPEC-SIM 3.6, DM.Path).
// Owner: path-units engineer (A6, key "path-units"). Files: Path.h, Path.cpp, Units.h, Units.cpp,
// SimTests/test_path.cpp, test_units.cpp, SimTests/stubs/Path_stub.cpp, Units_stub.cpp. Build fresh
// from the spec (no JS base). Header written by foundation-api; the owner may add, never rename.
//
// Path writes nothing in the world (its heap and stamp arrays live in w.scratch<...>(), reachable
// from const World&). Deterministic: ties broken on cell id. A reach() visitor may itself call
// find / reach / stepCost / cost (each nesting depth has its own buffers).
#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

#include "State.h"

namespace dm {

// Starting values from SPEC-SIM 3.6.
struct PathTune {
	double land = 1;               // land step
	double slopeK = 6;             // + slopeK per unit of height difference
	double forest = 0.4;           // + entering dense forest ...
	double forestVeg = 0.6;        // ... a cell with veg at or above this
	double ice = 3;                // + entering an iced-over land cell ...
	double iceLand = 0.5;          // ... ice above this
	double lake = 2;               // + entering a lake cell ...
	double lakeWater = 0.03;       // ... water above this (hydro::isLake)
	double roadPerLevel = 0.25;    // - per road level (the lower level of the two cells) ...
	double roadFloor = 0.3;        // ... never below this
	double lavaBlock = 0.05;       // a cell with more lava than this cannot be entered
	double coastalDepth = 0.6;     // Coastal: sea shallower than this ...
	double coastal = 0.6;          // ... costs this per cell
	double open = 0.35;            // Open: any sea cell costs this
	double embark = 3;             // embarking or landing
	double seaIceBlock = 0.6;      // sea ice above this is impassable
	double maxCost = 400;          // find() default
	double budgetMs = 25;          // worst case (whole planet) at L = 6
};

namespace path {

struct FindOpts {
	NavalMode naval = NavalMode::Land;   // JS naval: false | 'coastal' | true
	double maxCost = 400;
};
// visit(cell, cost) in non-decreasing cost; return false to stop the flood.
using Visit = std::function<bool(int cell, double cost)>;

inline constexpr double kNoPath = std::numeric_limits<double>::infinity();   // stepCost of a blocked move (never stored in the world)

// Path.find(world, from, to, { naval, maxCost }) -> [from, ..., to] | null. A* with a reused heap
// and stamp arrays (no per-call allocation except the result). Returns {from} when from == to and
// an EMPTY vector for "null" (unreachable, over maxCost, invalid cells). If cost is not null it
// receives the path cost (kNoPath when there is none). A NaN maxCost means PathTune::maxCost. The
// route is optimal (consistent A* bound); equal keys pop by lower cell id. On foot, start and target
// on different landmasses (w.cache.landId, trusted only while need.derive is clear) answer at once.
// Embarking / landing cost a flat `embark` and land steps measure height on the surface (elev +
// water, so lakes are level). The start cell is never tested (a party may leave lava or a drowned cell).
std::vector<int32_t> find(const World& w, int from, int to, const FindOpts& opts = {}, double* cost = nullptr);
// Path.stepCost(world, a, b, naval): cost of moving from cell a to its neighbour b, or kNoPath.
// Land ~1 + slope x 6 per unit of height difference, dense forest +0.4, ice +3, lake +2,
// road -0.25 per level (floor 0.3), lava impassable; Coastal also crosses sea shallower than 0.6
// (0.6 each), Open any sea (0.35 each); embarking / landing +3; sea ice > 0.6 impassable.
double stepCost(const World& w, int a, int b, NavalMode naval);
// Path.reach(world, from, maxCost, naval, visit): Dijkstra flood from `from`; visit(cell, cost) in
// non-decreasing cost (ties by cell id), `from` first at 0, only cells with cost <= maxCost (NaN:
// PathTune::maxCost; negative: nothing); returns the number of cells visited (including the one
// whose visitor returned false).
int reach(const World& w, int from, double maxCost, NavalMode naval, const Visit& visit);
// (JS Path.cost) total cost of following `cells` (kNoPath if any step is blocked or not a neighbour).
double cost(const World& w, const std::vector<int32_t>& cells, NavalMode naval);

} // namespace path
} // namespace dm
