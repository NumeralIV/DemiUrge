// DEMIURGE — Sim/Climate.h : temperature, moisture, ice, CO2, dust, sea level (SPEC-SIM 3.3, DM.Climate).
// Owner: climate engineer (A3, key "climate"). Files: Climate.h, Climate.cpp, SimTests/test_climate.cpp,
// SimTests/stubs/Climate_stub.cpp. JS reference: js/sim/climate.js (ported; see Climate.cpp for the model).
// Header written by foundation-api; the owner may add, never rename.
//
// Writers (SPEC-SIM 1.1): temp, moist, ice, co2, dust, globalTemp, the upwind cache (scratch);
// seaLevel in History / Ended (Climate::yearly; calibrate only repairs a non-finite one);
// vars.seaBase, vars.iceRef (calibrate); its own vars.num "climate_ice0", "climate_seaMark",
// "climate_ms". Sets need.life / need.hydro / need.derive and dirty.color / water / objects as below.
// Never draws from w.rng: the same world always gets the same climate.
// Cross-module amounts (binding starting values, applied by the callers): a factory settlement
// emits 0.0025 x sqrt(pop / 1000) per year (x 0.5 in the Space age) [Economy]; meteor dust +0.25
// [Disasters]; volcano +0.03 per year while erupting [Disasters]; each nuclear strike +0.10
// [Diplomacy].
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "State.h"

namespace dm {

// Starting values from SPEC-SIM 3.3 plus the JS TUNE.climate block. Rates are per reference cell
// (L = 6, geo.step 0.0189), so coarser grids get the same climate. Balance may change any of these;
// the wind / latitude-profile ones rebuild the wind table on the next update.
struct ClimateTune {
	// temperature (deg C)
	double tEquator = 29, tPole = -24;      // sea-level mean at the equator / poles (smooth in |lat|)
	double tShape = 1.3;                    // T = tPole + (tEquator - tPole) x cos(lat)^tShape (30: 20, 45: 10, 60: -2.5)
	double tNoise = 8, tNoiseFreq = 1.3;    // low-frequency wobble (+-5 at most, ~2 typical) so isotherms are not circles
	double lapse = 5.5;                     // per unit of LAND height above datum
	double albedo = 4;                      // cooling of a fully iced cell
	double dustCool = 9;                    // anomaly = globalTemp - dustCool x dust
	double seaTempMin = -2;                 // stored temp of a sea cell is clamped here
	// ice
	double landIce0 = -4, landIce1 = -9;    // land ice 0 at landIce0 ramping to 1 at landIce1
	double seaIce0 = -2.5, seaIce1 = -6;    // sea ice on the unclamped sea temperature
	double iceRate = 0.06, iceRateGenesis = 0.30;   // share of the gap closed per stepIce call
	double iceSnap = 0.004;                 // closer than this to the equilibrium: land on it
	double iceNeedsSea = 0.05;              // land ice needs snow: full once this share of cells is sea, none without a sea
	double iceRefMin = 50;                  // floor of vars.iceRef
	// winds
	double band1 = 30, band2 = 60;          // trades | westerlies | polar easterlies (degrees)
	double bandBlend = 5;                   // half-width of the blend at the band edges
	double merid = 0.45;                    // drift toward the equator (trades) / pole (westerlies) relative to east-west
	double windMix = 0.2;                   // share of a cell's air arriving from the two sides (sideways mixing)
	double windAgree = 0.4;                 // an upwind link counts fully when the neighbour's wind points at the cell this well
	double subsHum = 0.2, frontHum = 0.8;   // humidity of the local air where links fail: 30-degree calm line / 60-degree one
	// humidity sources (sea cells)
	double srcT0 = -2, srcT1 = 29, srcCold = 0.5;   // source humidity srcCold at srcT0 rising linearly to 1 at srcT1
	double srcMin = 0.25, srcMax = 1.2;
	double srcIce = 0.75;                   // share of the source sealed off by full sea ice
	double beltEq = 0.15, beltEqW = 9;      // wet equatorial belt (gaussian half-width in degrees)
	double dryLat = 28, dryW = 8, beltDry = 0.15;   // dry subtropical belt
	double evap = 0.14;                     // share of the gap to the source closed per reference cell of sea
	// rain
	double rainout = 0.05;                  // share of the air's moisture falling per reference cell where lift = 1
	double liftBase = 0.3, liftEq = 0.75, liftEqW = 14;            // lift by latitude: base + equatorial trough
	double liftMid = 0.75, midLat = 48, midW = 14;                 // + mid-latitude storm belt
	double liftDry = 0.08, liftMin = 0.08;                         // - subtropical high; floor
	double coastLift = 0.5, coastLen = 10, coastPasses = 3;        // extra lift in air fresh off the sea
	double rainNoise = 0.35;                // +- seed-dependent variation of the lift
	double reliefSmooth = 2;                // smoothing passes over the relief
	double upK = 2.5, upMax = 0.8;          // forced uphill: rain-out x (1 + upK x min(climb, upMax))
	double desc = 0.8, descK = 1.5;         // sinking air: rain-out x (1 - desc x sat(descK x drop))
	double capK = 1.2, capH = 3, capDump = 0.6;   // what air over high ground can carry; capDump of the excess falls per cell
	double seep = 0.007, seepSea = 0.3;     // moisture mixed down from aloft over land (none on a sea-less world)
	double recycleCold = 0.42, recycleHot = 0.82, recycleT0 = 0, recycleT1 = 24;   // share of rain evaporating again
	double passes = 10;                     // humidity sweeps (alternately downwind in longitude and latitude)
	double rainRef = 0.06;                  // RAIN_REF: FIXED (never the planet's current maximum)
	double rainKnee = 0.7;                  // rain / RAIN_REF above this is squeezed smoothly toward 1
	double droughtDry = 0.25;               // moist -= droughtDry x drought power
	double droughtRim = 0.3;                // outer share of a drought's radius over which it fades out
	double hydroDelta = 0.08;               // a land cell's moist moved this much since the last need.hydro -> need.hydro
	// yearly
	double dustDecay = 0.93;
	double uptake = 0.005;                  // co2 -= co2 x uptake x (0.5 + Life::cover(w).veg)
	double vegDefault = 0.5;                // veg assumed when Life::cover answers a non-finite number
	double co2Max = 300;
	double sens = 3, lag = 30;              // globalTemp -> sens x log2(1 + co2 / 100), ~lag-year lag
	double seaIceGain = 0.35, seaIceLoss = 0.25;     // iceTerm for r <= 1 / r > 1
	double seaTempK = 0.04;                 // + seaTempK x globalTemp
	double seaLo = -0.3, seaHi = 0.6;       // target within seaBase + [seaLo, seaHi]
	double seaRate = 0.005;                 // max sea-level change per year
	double seaMoved = 0.03;                 // re-derive after the sea moved this much
	double anomalyNeed = 0.25;              // need.climate when the anomaly moved this much
	double warm[4] = { 1, 2, 3, 4 };        // 'warming' milestones (anomaly >=)
	double cool[2] = { -2, -4 };            // 'cooling' (cool[0]) and 'ice-age' (cool[1]) milestones (anomaly <=)
	double thawAt = -1;                     // 'thaw': anomaly back above this after a cooling / ice-age (re-arms them)
	double seaMile[2] = { 0.2, 0.4 };       // 'sea-rise' / 'sea-fall' milestones (relative to seaBase)
};

// Climate's private transient caches (w.scratch<ClimateScratch>(); never saved). Public only so
// tests can look at the wind table; nobody else reads or writes it.
struct ClimateScratch {
	// wind table, per geo and wind tuning (JS world._up and friends)
	const Geo* gridGeo = nullptr;
	std::array<double, 21> gridSig{};
	int gridBuilds = 0;                     // how often the wind table was built (tests)
	std::vector<int32_t> up, up2, up3;      // up = strongest upwind link (JS world._up); up2 / up3 weaker ones
	std::vector<float> w1, w2, w3, wl, locH; // link weights; share of local air; its humidity relative to sea air
	std::vector<int32_t> orderLon, orderLat; // visiting orders, downwind in longitude / latitude
	std::vector<float> belt, lift;          // latitude profiles: sea-source factor, rain-making lift
	double scale = 1;                       // geo.step / reference step
	// per-world cache (JS world._clim)
	bool cacheOk = false;
	int cacheN = 0;
	uint32_t cacheSeed = 0;
	std::array<double, 6> cacheSig{};
	std::vector<float> base, wob, t0, mh;   // base temp; rain-lift wobble; temp without ice / sea clamp; moist at last need.hydro
	bool hasAnom = false;                   // an update has run (anom valid)
	double anom = 0;                        // anomaly the last temperature pass used
	double wetWorld = 0, aloft = 0;         // 0..1: enough sea for snow / to keep the upper air moist
	// buffers
	std::vector<float> z, z2, mar, hum, a, b, li, cap, rc, rain, dr;
	std::vector<int32_t> list;
};

namespace climate {

// Climate.update: temp and moist for every cell; clears need.climate; sets need.life and
// dirty.color (and need.hydro when some land cell's moist moved by more than hydroDelta since the
// rivers were last asked to follow). Does not move ice (only repairs out-of-range values).
// temp: ~+29 at the equator to -24 at the poles, minus lapse x max(0, elev - datum) on land, plus
// anomaly, minus albedo x ice, plus low-frequency noise; sea cells stored >= -2.
// moist = sat(squeeze(rain / RAIN_REF) + wet - droughtDry x drought), winds by latitude band, sea
// sources, downwind sweeps with orographic rain and rain shadows, one smoothing pass. No sea ->
// bone dry. Budget <= 12 ms at L = 6.
void update(World& w);
// Climate.stepIce: ice toward its equilibrium (with the albedo term settled) by iceRate in History /
// Ended, iceRateGenesis otherwise; keeps temp in step; dirty.color / dirty.water when anything
// changed. Sim calls it every 20 ticks. Works before any update (computes temperatures first).
void stepIce(World& w);
// Climate.yearly: dust decay always. Only in History / Ended with a finite vars.seaBase: CO2
// uptake, globalTemp lag, sea level toward its ice / temperature target (<= seaRate per year, within
// seaBase + [seaLo, seaHi]), need / dirty flags on change, climate:milestone events once per
// threshold. A world with a seaBase but no iceRef (hand-made) is calibrated on the first call (ice
// to equilibrium, iceRef). need.climate when the anomaly moved by anomalyNeed since the last update.
void yearly(World& w);
// Climate.calibrate: vars.seaBase = seaLevel (a non-finite sea level is repaired to 0 first),
// temperatures, ice set to its equilibrium directly, vars.iceRef = max(iceRefMin, landIce(w)),
// rainfall. Sim calls it when history begins (and from Sim::use when history has begun and
// vars.seaBase is missing). Idempotent.
void calibrate(World& w);
// Climate.datum: max(0, vars.seaBase) when set and finite, else max(0, seaLevel) (0 if that is not
// finite). The height lapse is measured from here, never from the live sea level. Read-only.
double datum(const World& w);
// Climate.anomaly: globalTemp - dustCool x dust (the number the HUD shows). Read-only; always finite.
double anomaly(const World& w);
// Climate.emit: co2 += amount, clamped to 0..co2Max (NaN amounts ignored).
void emit(World& w, double amount);
// Climate.addDust: dust += amount, clamped to 0..1 (NaN amounts ignored); sets need.climate.
void addDust(World& w, double amount);
// (JS Climate.landIce) sum of ice over land cells (elev >= seaLevel). Read-only.
double landIce(const World& w);

} // namespace climate
} // namespace dm
