// DEMIURGE — Sim/Units.h : settlers, traders, armies, prophets on the move (SPEC-SIM 3.9, DM.Units).
// Owner: path-units engineer (A6, key "path-units"); see Path.h. Header written by foundation-api;
// the owner may add, never rename.
//
// Writers (SPEC-SIM 1.1): w.units (created only through state::newUnit inside spawn; removed only
// through kill or after an arrival). Units emits unit:spawned / unit:arrived / unit:died.
//
// Arrival contract (C++ detail of SPEC-SIM 3.9): when a unit reaches the last cell of its path,
// Units emits unit:arrived, then
//   settler -> civ::found(w, dest, culture, size, FoundedBy::Settlers, { origin devotion, love })
//              (if that returns nullptr: add its people to the nearest live settlement of its
//              culture, if any);
//   trader / army / prophet -> society::onArrive(w, unitId) while the unit is still in w.units
//              (onArrive may call units::kill, e.g. a beaten army);
// and finally erases the unit if it is still present, WITHOUT a unit:died event.
#pragma once

#include <string_view>
#include <vector>

#include "Core/Vec.h"
#include "State.h"

namespace dm {

// Starting values from SPEC-SIM 3.9.
struct UnitsTune {
	int maxUnits = 300;            // spawn returns nullptr at the cap
	double speedSettler = 5, speedTrader = 7, speedArmy = 5, speedProphet = 4;   // cells / year on plain land
	double seaMul = 2.5;           // x on sea (a step between two sea cells takes 1 / seaMul)
	double shoreTime = 1;          // the step that embarks or lands takes this many open-land steps
	double slowMax = 3;            // a land step takes path::stepCost open-land steps, at most this many
	double lavaKill = 0.2;         // units die on lava > this
	double maxCost = 400;          // route budget of spawn (default of SpawnOpts::maxCost) and of the re-path
	                               // (the re-path budget is at least twice the cost of the rest of the old route)
};

namespace units {

struct SpawnOpts {
	UnitType type = UnitType::Settler;
	int culture = -1;
	int from = -1;                       // origin settlement id (the path starts at its cell)
	int to = -1;                         // target settlement id, -1 when heading for an empty site
	int dest = -1;                       // destination cell
	double size = 0;                     // people or strength
	NavalMode naval = NavalMode::Land;   // what it may cross (Path)
	NumBag data;                         // copied into Unit::data
	// (C++ additions, owner A6)
	double maxCost = 400;                // route budget of the search (NaN: UnitsTune::maxCost)
	std::vector<int32_t> path;           // a ready route [origin cell, ..., dest] (chain of passable
	                                     // neighbours): no search is run; refused if it is not one
};

// Units.spawn: path from the origin's cell to dest (path::find honouring naval); nullptr (and
// nothing changed) when there is no path, the origin is dead / invalid, culture or `to` names no
// entity, or maxUnits units are alive. dest < 0 with a valid `to`: the centre of `to`. Speed by
// type (UnitsTune). Callers remove people from the source only after a non-null return. The
// pointer is valid until the next unit is created: keep unit->id. Emits unit:spawned (cell = start).
Unit* spawn(World& w, const SpawnOpts& opts);
// Units.step(world, dtYears): move every unit (ascending id; `speed` open-land steps per year; a land
// step takes min(slowMax, path::stepCost) of them, so slopes / forest / ice / lakes slow and roads
// help; a sea step 1 / seaMul; embarking / landing shoreTime); sea flag = current cell is sea;
// re-path once in the unit's life when the next cell became impassable (honouring naval), then die
// ('lost'); armies / traders whose target settlement is dead, or (army) whose cultures are no longer
// at war, die 'disbanded'; lava > 0.2 kills ('lava'); arrivals per the contract above. A settler
// with to >= 0 naming a live settlement of its culture joins it (pop += size) instead of founding;
// a settler whose people find no home at all (no found, no live settlement of its culture) dies
// 'lost' after unit:arrived. Units spawned during the step move from the next step.
void step(World& w, double dtYears);
// Units.kill(world, unit, cause): removes it and emits unit:died with its last cell; false if absent.
bool kill(World& w, int unitId, std::string_view cause);
// Units.near(world, cell, ang): ids of live units whose current cell is within ang (ascending id).
std::vector<int> near(const World& w, int cell, double ang);
// (JS Units.cellOf) the unit's current cell: the nearer end of the step it is on (path[i] while
// t < 0.5, else path[i + 1]; the last cell once i reaches it); dest when the path is empty; -1 if none.
int cellOf(const World& w, const Unit& u);
// Units.dirOf(world, unit, out) -> current cell; writes the unit-length interpolated direction
// (path[i] -> path[i + 1] by t) into out. Continuous between cells.
int dirOf(const World& w, const Unit& u, Vec3& out);

} // namespace units
} // namespace dm
