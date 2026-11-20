// DEMIURGE — Sim/Sim.h : world creation, phases, the tick scheduler, endings, score, and the hooks
// the headless runner needs (SPEC-SIM 3.12, DM.Sim).
// Owner: sim engineer (A14, key "sim"). Files: Sim.h, Sim.cpp, Balance.h, Balance.cpp,
// Unreal/Tools/simrun (C++ headless runner with policies passive / kind / cruel / chaotic),
// SimTests/test_sim.cpp, SimTests/stubs/Sim_stub.cpp (and Balance_stub.cpp). Build fresh from the
// spec (no JS base). Header written by foundation-api; the owner may add, never rename.
//
// Writers (SPEC-SIM 1.1): w.phase (setPhase ONLY), year, tick, heat, ending; vars.lifeBoost,
// vars.t0 (setPhase); stats.pop, peakPop, believers (refreshStats); seaLevel before history
// (genesis::skip).
//
// There is no active world (JS DM.world): every call names its world, and handlers subscribed to
// w.bus only ever see events of w.
//
// Tick schedule (sim::tick; C++ has no "undefined namespace": in a partial build the other modules
// are neutral stubs, so every step can always be called):
//   Molten:              heat -= 1/60; at 0 -> setPhase(Water). Nothing else.
//   Water, Life, People: powers::tick -> life::burn -> life::step(tick % P, P) (P = 2 in Life, 8
//                        otherwise) -> climate::stepIce when tick % 20 == 10 -> one heavy pass.
//   History, Ended:      year += kTick -> powers::tick -> life::burn -> units::step(kTick) ->
//                        life::step(tick % 8, 8) -> when (tick - vars.t0) % 4 == 0 the yearly group:
//                        civ::yearly, society::yearly, climate::yearly, powers::natural,
//                        sim::refreshStats, chronicle::snapshot, sim::checkEnding -> climate::stepIce
//                        when tick % 20 == 10 -> on ticks that are NOT yearly ticks, one heavy pass.
//   Heavy pass (at most ONE per tick, the first that is due): (1) need.derive -> terrain::derive;
//   (2) (need.climate and >= 8 ticks since the last) or >= 40 ticks since the last ->
//   climate::update; (3) need.hydro and >= 8 ticks since the last -> hydro::update; (4) need.life ->
//   life::classify on this tick and life::habitability on the next.
// Budget at L = 6: mean tick <= 2.5 ms, any single tick <= 30 ms.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "State.h"

namespace dm {

// Starting values from SPEC-SIM 3.12.
struct SimTune {
	double lifeBoost = 25;               // vars.lifeBoost in phase Life
	double heatPerTick = 1.0 / 60;       // Molten cooling
	int lifeParts = 2, otherParts = 8;   // life::step slices
	int iceEvery = 20, iceAt = 10;       // climate::stepIce when tick % 20 == 10
	int climateMin = 8, climateMax = 40, hydroMin = 8;   // heavy-pass spacing (ticks)
	double waterGate = -3.5;             // genesis next: Water -> Life needs seaLevel >= this
	double lifeGate = 0.25;              // Life -> People needs cover.veg >= this
	double silenceYears = 50, silencePeak = 1000, ashYears = 200;
	double silenceMinYear = 1000;        // (v3, design review finding 10) no silence before this year
	double startAwe = 60;                // genesis skip / first awaken
	// --- added by the sim engineer (A14) ---
	double startPop = 30;                // people of the tribe genesis::skip wakes (fallback path; Powers uses its own)
	double skipSeaMin = -6, skipSeaMax = 1.5;   // genesis::skip clamps its sea level here
	double waterValueLo = -6.5, waterValueSpan = 3;   // progress value in Water: sat((seaLevel - lo) / span)
	double moodBelovedDevotion = 0.35, moodBelovedLove = 0.2, moodFearedLove = -0.2, moodForgottenDevotion = 0.2;
	// score: total = round(base x mult), base = the parts below (each rounded)
	double scorePop = 100;               // x log10(1 + peak population)
	double scoreAge = 150;               // per age reached (w.age)
	double scoreCulture = 60;            // per living culture
	double scoreWonder = 120;            // per standing wonder
	double scoreYear = 0.1, scoreYearCap = 5000;   // per year survived, up to the cap
	double scoreLife = 400;              // x life::cover(w).veg
	double multBeloved = 2, multParting = 1.6, multForgotten = 1.4, multFeared = 1.3, multQuiet = 0.6, multAsh = 0.4;
};

namespace sim {

inline constexpr double kTick = 0.25;            // Sim.TICK: years per tick
inline constexpr double kRealPerTick = 0.125;    // Sim.REAL_PER_TICK: real seconds per tick at 1x
inline constexpr int kTicksPerYear = 4;

// create() options (JS { seed, name, L = 6, settings }); an empty name -> names::planet.
struct WorldOpts {
	uint32_t seed = 0;
	std::string name;
	int L = 6;
	Settings settings;
};
// genesis::progress hint keys ('cooling' | 'water-more' | 'water-ok' | 'life-more' | 'life-ok' | 'people')
enum class Hint : uint8_t { Cooling, WaterMore, WaterOk, LifeMore, LifeOk, People };
inline const char* toStr(Hint h) {
	switch (h) { case Hint::Cooling: return "cooling"; case Hint::WaterMore: return "water-more"; case Hint::WaterOk: return "water-ok"; case Hint::LifeMore: return "life-more"; case Hint::LifeOk: return "life-ok"; default: return "people"; }
}
struct Score {
	double total = 0;
	NumBag parts;                        // peak population, ages reached, cultures alive, wonders, years survived, living world, ending multiplier
};

// Sim.create: state::create + terrain::generate; phase Molten; balance::apply; ends with use(w).
std::unique_ptr<World> create(const WorldOpts& opts = {});
// Sim.use: make a new or loaded world ready: balance::apply, chronicle::attach, rederive.
void use(World& w);
// Sim.rederive: terrain::derive -> climate::update -> hydro::update -> life::classify ->
// life::habitability -> life::rebuild -> civ::rebuild (and climate::calibrate first if history has
// begun and vars.seaBase is missing).
void rederive(World& w);
// Sim.tick: advance one tick (any phase) per the schedule above. Never calls powers::update.
void tick(World& w);
// Sim.advance: for each tick: powers::update(w, kRealPerTick); tick(w). The ONLY way headless code
// (simrun, policies, tests) advances time.
void advance(World& w, int ticks = 1);
// Sim.setPhase: the ONLY writer of w.phase. Entry actions (Water: heat = 0; Life: vars.lifeBoost =
// SimTune.lifeBoost; People: lifeBoost = 1; History: lifeBoost = 1, climate::calibrate, year = 0,
// vars.t0 = tick; Ended: none), then emits phase, then chronicle::snapshot(w, true). False if
// already in that phase or the change is not allowed.
bool setPhase(World& w, Phase phase);
// Sim.refreshStats: stats.pop, peakPop, believers (= sum of pop x devotion).
void refreshStats(World& w);
// Sim.checkEnding: nullopt once w.ending is set. Ark: when vars.ark exists -> { Ark, mood from
// society::regard(vars.ark.culture) }. Silence: no live settlement and no settler unit for 50
// years and stats.peakPop >= 1000 -> { Silence, Ash if vars.lastNuke within 200 years else Quiet };
// below that population emit people:lost once per extinction instead. Setting an ending: write
// w.ending, setPhase(Ended), emit ending.
std::optional<Ending> checkEnding(World& w);
// Sim.score. Read-only.
Score score(const World& w);
// (JS Sim.mood) the ending mood for devotion / love: Beloved (devotion >= 0.35 and love >= 0.2),
// Feared (love <= -0.2), Forgotten (devotion < 0.2), else Parting.
Mood mood(double devotion, double love);

namespace genesis {
struct Progress {
	Phase phase = Phase::Molten;
	double value = 0;                    // Molten 1 - heat; Water sat((seaLevel + 6.5) / 3); Life sat(cover.veg / 0.25); People 0
	bool canAdvance = false;
	Hint hint = Hint::Cooling;
	double ocean = 0;                    // stats.ocean: share of cells that are sea
	double veg = 0;                      // stats.veg: life::cover(w).veg
};
struct SkipOpts {
	double sea = 0;
	int cell = -1;                       // where people wake (-1: civ::bestSite)
};
// Sim.genesis.progress. Read-only.
Progress progress(const World& w);
// Sim.genesis.next: Water -> Life needs seaLevel >= -3.5; Life -> People needs cover.veg >= 0.25.
bool next(World& w);
// Sim.genesis.skip: straight to History: heat 0, set the sea, rederive, life::fill, rederive biomes,
// wake people at opts.cell (or civ::bestSite): one settlement, calibrated climate, Awe 60.
// create + skip <= 800 ms at L = 6. False if it could not wake anyone.
bool skip(World& w, const SkipOpts& opts = {});
} // namespace genesis

// ---------------------------------------------------------------- headless runner hooks
// (Tools/simrun, Tools/policies, the integration tests). Real-time measurement (tick ms) is the
// runner's job: Sim code never reads a clock.

// One timeline row (simrun prints one every 100 years).
struct Sample {
	double year = 0;  int64_t tick = 0;  Phase phase = Phase::Molten;
	double pop = 0;  int settlements = 0, cultures = 0, age = 0;
	double anomaly = 0, seaLevel = 0, co2 = 0, dust = 0;
	double awe = 0, aweMax = 0, aweIncome = 0;
	int units = 0, disasters = 0, wars = 0, miracles = 0;
};
// Called around every tick by runTicks (any may be empty). `before` is where a policy calls
// powers::use (brush powers with dt = kRealPerTick).
struct RunHooks {
	std::function<void(World&)> before;
	std::function<void(World&)> after;
	std::function<void(World&)> yearly;  // after a tick that ran the yearly group
};

// Read-only summary of the world now.
Sample sample(const World& w);
// Fingerprint of the whole persisted state (and the derived arrays) for determinism checks: two
// runs driven by the same calls give the same value.
uint32_t fingerprint(const World& w);
// "" or the first non-finite value ("field[c]", scalar or entity field): the runner's NaN check.
std::string firstNonFinite(const World& w);
// Whether the tick sim::tick would run next is a yearly-group tick.
bool isYearlyTick(const World& w);
// advance(w, 1) `ticks` times with the hooks; stops early once the world has ended if stopAtEnding.
// Returns the number of ticks run.
int64_t runTicks(World& w, int64_t ticks, const RunHooks& hooks = {}, bool stopAtEnding = false);

} // namespace sim
} // namespace dm
