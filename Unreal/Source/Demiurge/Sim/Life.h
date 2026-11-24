// DEMIURGE — Sim/Life.h : biomes, vegetation, fauna, fire, fertility, habitability (SPEC-SIM 3.5, DM.Life).
// Owner: life engineer (A5, key "life"). Files: Life.h, Life.cpp, SimTests/test_life.cpp,
// SimTests/stubs/Life_stub.cpp. JS reference: js/sim/life.js (finished; port it, judging it
// against the spec). Header written by foundation-api; the owner may add, never rename.
//
// Writers (SPEC-SIM 1.1): veg (Civ also lowers veg on new farmland), fauna, fire, scorch, biome,
// hab, w.cache.fires, w.cache.landHab; wet (decay; Powers raises); fert (step / bloom; Terrain
// generates, Powers adds ash / floods); rad (decay; Society raises); farm / urban decay on cells
// with owner == -1 (Civ writes them otherwise). On sea cells veg, farm, urban, road, water, fire and
// scorch are forced to 0 by step.
#pragma once

#include "State.h"

namespace dm {

// Starting values from SPEC-SIM 3.5 plus the JS TUNE.life block (js/sim/life.js), ported by the
// life engineer. The first group are the foundation's names (kept); the rest are the JS names.
// Balance may change any of them per world: w.tune<LifeTune>().grow = ...
struct LifeTune {
	// ---- spec starting values (foundation names)
	double deepSea = 0.6;          // sea deeper than this: SEA, else SHALLOWS
	double iceBiome = 0.5;         // ice above this: SEAICE / ICE (nothing grows)
	double lakeWater = 0.03;       // lake cell threshold (= hydro::isLake)
	double regrowYears = 120;      // design target: a cleared forest is back in ~120 years at lifeBoost 1
	                               // (documentation only; the rate that achieves it is `grow`)
	int seedsPerYear = 8;          // drifting seeds: random land cells tried per year of the boosted clock (x lifeBoost)
	int seedHops = 12;             // ... each needs a seed source within this many hops (sea included)
	double seedSource = 0.3;       // ... a source: veg above this (or seedSourceRel of its own capacity)
	double seedVeg = 0.08;         // vegetation of a drifted seedling
	double seedMin = 0.35;         // Life.seed: veg >= seedMin on plantable land within ang
	double sproutNbr = 0.05;       // a bare cell sprouts only next to a neighbour with more veg than this (or estRel)
	int fireTicks = 2;             // a cell burns for exactly 2 ticks (structural: encoded in fire[], not read)
	double fireP = 0.55;           // ignition P = fireP x veg x dry, dry = sat(dryA - dryB x (moist + wet))
	double dryA = 1.2, dryB = 1.6;
	double fireMinVeg = 0.15;      // never onto veg < fireMinVeg (nor sea, ice, lake cells)
	int maxBurning = 1500;         // no new ignitions while this many cells burn
	double burnVeg = 0.8;          // share of veg a fire removes (and it sets scorch)
	double wetOut = 0.3;           // wet > wetOut cannot burn; rain puts fires out
	int fireEventsPerTick = 2;     // fire:start at most twice per tick
	double radDecay = 0.015;       // share lost per year
	double farmDecay = 0.05, urbanDecay = 0.01;   // share lost per year on unowned cells
	double habTLo = 8, habTHi = 26;  // best temperature band for habitability (suitability 1 inside)
	int habFreshHops = 2;          // fresh water within this many hops makes a desert liveable
	//                     SEA SHAL BEACH DESERT SAVAN GRASS FOREST RAINF TAIGA TUNDRA ICE ROCK SNOW WETL BARREN SEAICE
	double cap[16] = {     0,  0,   0.05,  0.05,  0.35, 0.3,  0.9,   1,    0.75, 0.15,  0,  0.03, 0,  0.7,  0,     0 };

	// ---- biomes (JS TUNE.life)
	double lavaBarren = 0.05;      // lava thicker than this: BARREN, nothing grows
	double scorchBarren = 0.7;     // scorch above this with veg below vegBarren: BARREN
	double vegBarren = 0.02;
	double snowH = 3.0;            // height above the datum from which a cold cell is a SNOWPEAK ...
	double snowT = -3;             // ... when colder than this (or iced over)
	double rockSlope = 1.6;        // slope (at the reference cell size) above which a cell is ROCK
	double rockH = 4.6;            // ... or height above the datum above which it is ROCK
	double beachH = 0.1;           // BEACH: next to the sea, lower than this above sea level,
	double beachSlope = 0.45;      // gentler than this,
	double beachT = 1;             // and warmer than this
	double wetlandM = 0.5;         // WETLAND: moist at least this,
	double wetlandH = 0.6;         // lower than this above the datum,
	double wetlandSlope = 0.3;     // flatter than this,
	double wetlandT = 3;           // warmer than this, and next to fresh water or the sea
	double evap = 0.004;           // effective moisture = moist - evap x max(0, temp - 10)
	double tundraT = -5;           // colder than this: TUNDRA
	double borealT = 3;            // colder than this: TAIGA when effective moisture >= taigaM, else TUNDRA
	double taigaM = 0.25;
	double coolT = 7;              // colder than this (and not dry): TAIGA rather than FOREST
	double hotT = 20;              // warmer than this: SAVANNA / RAINFOREST replace GRASSLAND / wet FOREST
	double desertM = 0.10;         // effective moisture below this: DESERT
	double grassM = 0.36;          // below this: GRASSLAND
	double savannaM = 0.40;        // hot and below this: SAVANNA
	double rainM = 0.62;           // hot and at least this: RAINFOREST
	double jitterM = 0.12;         // the table reads moist + jitterM (patch - 0.5) + jitterG (grain - 0.5)
	double jitterG = 0.03;         // and temp + jitterT (regional - 0.5): borders wander and interfinger
	double jitterT = 3;
	double refStep = 0.0189;       // geo step of the reference world (L = 6); slopes scale by refStep / step

	// ---- vegetation
	double plantMin = 0.005;       // a cell is plantable when its capacity exceeds this
	double grow = 0.045;           // logistic growth rate per year (cleared forest ~90 % back in ~120 years)
	double die = 0.03;             // die-back per year toward a capacity below the standing vegetation
	double kill = 0.6;             // die-back per year where nothing can grow (ice, lake, lava)
	double establishedRel = 0.5;   // a neighbour holding this share of its own capacity also counts as established
	double sprout = 0.06;          // vegetation of a new sprout
	double spread = 0.08;          // sprouting hazard per year of a bare cell with two green neighbours
	double spreadBoost = 0.9;      // hazard scales with lifeBoost ^ this (paces the genesis wave)
	double spreadCap0 = 0.3;       // hazard factor on ground with no capacity, rising to 1 at spreadCapRef
	double spreadCapRef = 0.6;
	double spreadK[7] = { 0, 0.8, 1, 1.2, 1.6, 2.2, 3 };   // hazard factor by established neighbours
	double spreadFresh[2] = { 2.2, 1.6 };                  // on a river / lake shore cell, and one hop from one
	double spreadSlope = 1.2;      // hazard / (1 + spreadSlope x slope)
	double spreadNoise = 2.4;      // hazard x exp(spreadNoise (grain - 0.5) + spreadPatch (patch - 0.5))
	double spreadPatch = 3.2;
	double airVeg = 0.02;          // dead ground next to vegetation above this carries seed pressure 1 ...
	double airDecay = 0.96;        // ... handed on from dead cell to dead cell, x this per hop (< 1)
	double seedSourceRel = 0.6;    // drift source: veg > sproutNbr and >= this share of its own capacity
	double driftBoost = 0.3;       // while lifeBoost > 1 an overland pick takes with chance boost ^ (driftBoost - 1)
	double driftSeaBoost = 1;      // ... and a pick across water with boost ^ (driftSeaBoost - 1)
	int driftMax = 400;            // most drift picks per call
	double forestVeg = 0.5;        // cover().forest counts land cells with at least this much vegetation
	double maxBoost = 400;         // clamp for vars.lifeBoost

	// ---- fire
	double burnFauna = 0.5;        // share of the wildlife a fire kills
	double burnFert = 0.08;        // fertility gained per unit of vegetation burnt (ash)

	// ---- wildlife
	//                        SEA SHAL BEACH DESERT SAVAN GRASS FOREST RAINF TAIGA TUNDRA ICE ROCK SNOW WETL BARREN SEAICE
	double faunaCap[16] = {   0,  0,   0.15,  0.08,  0.75, 0.7,  0.8,   0.9,  0.5,  0.25,  0, 0.1,  0,  0.85, 0,     0 };
	double faunaGrow = 0.3;        // logistic rate per year
	double faunaDie = 0.25;        // decline per year toward a lower capacity
	double faunaSeed = 0.06;       // density of the first arrivals
	double faunaSrc = 0.05;        // a sea / lake cell is colonised from a neighbour with more wildlife than this
	double fishShallow = 0.9;      // fish capacity in the shallows ...
	double fishDeep = 0.25;        // ... falling to this at fishDeepAt below the surface
	double fishDeepAt = 3;
	double fishLake = 0.6;

	// ---- soil and decay
	double fertRate = 0.004;       // share of the gap to the natural level closed per year
	double fertBase = 0.32;        // natural fertility = fertBase + fertVar x regional noise ...
	double fertVar = 0.4;
	double fertFresh[4] = { 0.3, 0.22, 0.12, 0.05 };   // ... + by hops to fresh water (0..3)
	double fertLow = 0.1;          // + on lowlands (fading out by 1.2 above the datum)
	double fertHigh = 0.3;         // - on highlands (from 2.2, full by 5.2 above the datum)
	double fertAsh = 0.25;         // + scorch x this
	double fertMin = 0.15, fertMax = 1.2;   // clamp of the natural level
	double fertCap = 1.5;          // hard cap for bloom and ash
	double wetDecay = 0.10;        // per year (exponential)
	double scorchDecay = 0.06;     // per year
	double tiny = 0.004;           // wet, scorch, rad, farm, urban, dying veg below this become 0

	// ---- habitability
	double habColdZero = -6;       // temperature suitability 0 at or below this, 1 from habTLo ...
	double habHotZero = 40;        // ... and back to 0 from habTHi to this
	double habRain[2] = { 0.06, 0.40 };        // water from rainfall: 0 at the first value, 1 at the second
	double habFresh[3] = { 0.8, 0.8, 0.6 };    // water from fresh water 0, 1, 2 hops away (up to habFreshHops)
	double habFert = 0.55;         // fertility factor = habFert + (1 - habFert) x sat(fert)
	double habSlope[2] = { 0.25, 1.6 };        // slope factor falls from 1 to 0 across this range
	double habCoast[3] = { 1, 1.12, 1.06 };    // coastal bonus by hops to the sea
	double habMin = 0.05;          // a cell counts as habitable (w.cache.landHab) above this
	// ---- (v4 mythos, M0) old growth (SPEC-MYTHOS 3.22): wild[c] counts decades of unbroken forest cover
	// (veg >= wildVeg, farm < wildFarm, urban < wildUrban), reset when cleared, burned below wildBurn or farmed;
	// old growth at wild >= wildAncient; 255 = a sacred grove (never farmed). Written by the economy-life-v4b track.
	double wildVeg = 0.6, wildBurn = 0.3, wildFarm = 0.1, wildUrban = 0.05; int wildAncient = 30;
};

namespace life {

// Life.cover result: land fraction; mean veg / capacity over plantable land; forest fraction.
struct Cover {
	double land = 0, veg = 0, forest = 0;
};

// Life.classify: biome for every cell (sea: SEA / SHALLOWS / SEAICE only; land: BARREN, ICE,
// SNOWPEAK, ROCK, BEACH, then a Whittaker table on temp and moist incl. WETLAND); dirty.color and
// dirty.objects. Budget <= 6 ms at L = 6.
void classify(World& w);
// Life.capacity: vegetation the cell could support right now, 0..1:
// biomeCapacity x (0.6 + 0.4 fert) x (1 - farm) x (1 - urban) x (1 - 0.8 rad); 0 on lake, sea, ice.
double capacity(const World& w, int c);
// Life.step(part, parts): advance veg, fauna, fert, wet, scorch, rad (and farm / urban decay on
// unowned cells) for cells with c % parts == part, by dt = parts x Sim TICK years. Logistic growth x
// vars.lifeBoost; drifting seeds; dirty.objects on kVegT crossings. Budget <= 0.6 ms per slice of 8.
void step(World& w, int part, int parts);
// Life.burn: advance fires; called every tick. Spread per the fire rule; fire:start at most twice
// per tick; keeps w.cache.fires == the cells with fire > 0.
void burn(World& w);
// Life.ignite: start a fire if the cell can burn; returns whether it did.
bool ignite(World& w, int c, double intensity = 1);
// Life.seed: plant starter vegetation (veg >= seedMin on plantable land within ang).
void seed(World& w, int c, double ang);
// Life.bloom: raise veg toward capacity and fert by amount x falloff within ang.
void bloom(World& w, int c, double ang, double amount);
// Life.fill: veg = frac x capacity and fauna to capacity everywhere (Sim genesis skip).
void fill(World& w, double frac = 1);
// Life.habitability: hab for every cell (0 on sea, lake, ice, rock, snow peaks, lava, barren;
// otherwise temperature x water x fertility x gentle slope x small coastal bonus x (1 - rad)) and
// w.cache.landHab (habitable cells per landmass id); clears need.life.
void habitability(World& w);
// Life.rebuild: rebuild transient caches from persisted state (w.cache.fires from fire[]).
void rebuild(World& w);
// Life.cover: { land, veg, forest }; cached per tick (scratch). Read-only.
Cover cover(const World& w);

} // namespace life
} // namespace dm
