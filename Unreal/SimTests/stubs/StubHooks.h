// DEMIURGE — SimTests/stubs/StubHooks.h : what link-time stubs can do for a test (C++ T.stub).
//
// In `run.ps1 <key>` every module other than the key's own is linked as its neutral stub
// (SimTests/stubs/<Module>_stub.cpp). Every stub function
//   1. records its call (when recording is on) as its hook name, e.g. "civ_found", "sim_genesis_skip";
//   2. if the test installed a hook for it, returns the hook's result instead;
//   3. otherwise does its neutral thing (no-op, false, nullptr, -1, 0, empty; a few pure reads of
//      world state answer honestly, see docs/FOUNDATION-CPP.md 12).
//
//   #include "stubs/StubHooks.h"
//   DM_TEST("economy: settlers found a town (teleporting Units::spawn)") {
//       dmt::stub::Guard g(true);                        // clears hooks and the log now and at scope exit; true = record
//       if (!dmt::stub::stubbed("units")) return;        // in `run.ps1 all` the real Units is linked: hooks do nothing
//       dmt::stub::hooks().units_spawn = [](dm::World& w, const dm::units::SpawnOpts& o) -> dm::Unit* {
//           dm::civ::found(w, o.dest, o.culture, o.size);        // teleport
//           return &dm::state::newUnit(w, o.type);
//       };
//       ...
//       ok(dmt::stub::called("units_spawn") == 1);
//   }
//
// Hooks and the log are process-wide test state (tests run one at a time). ALWAYS use a Guard so a
// hook never leaks into the next test. A hook has exactly the signature of the function it replaces.
// The list below is the complete list of module functions declared in Sim/<Module>.h: when you add a
// function to your header, add its row here and its DM_STUB line to your stub (test_link checks
// that every row is callable and records its call).
#pragma once

#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include "Balance.h"
#include "Chronicle.h"
#include "Civ.h"
#include "Climate.h"
#include "Hydro.h"
#include "Life.h"
#include "Names.h"
#include "Path.h"
#include "Powers.h"
#include "Save.h"
#include "Sim.h"
#include "Society.h"
#include "Terrain.h"
#include "Units.h"

// X(stub module, hook member, function qualified below dm::)
#define DM_STUBS_NAMES(X) \
	X(names, names_language, names::language) X(names, names_word, names::word) X(names, names_place, names::place) \
	X(names, names_person, names::person) X(names, names_culture, names::culture) X(names, names_god, names::god) \
	X(names, names_planet, names::planet) X(names, names_cap, names::cap)
#define DM_STUBS_TERRAIN(X) \
	X(terrain, terrain_generate, terrain::generate) X(terrain, terrain_sculpt, terrain::sculpt) \
	X(terrain, terrain_crater, terrain::crater) X(terrain, terrain_cone, terrain::cone) \
	X(terrain, terrain_roughen, terrain::roughen) X(terrain, terrain_addElev, terrain::addElev) \
	X(terrain, terrain_touched, terrain::touched) X(terrain, terrain_derive, terrain::derive)
#define DM_STUBS_CLIMATE(X) \
	X(climate, climate_update, climate::update) X(climate, climate_stepIce, climate::stepIce) \
	X(climate, climate_yearly, climate::yearly) X(climate, climate_calibrate, climate::calibrate) \
	X(climate, climate_datum, climate::datum) X(climate, climate_anomaly, climate::anomaly) \
	X(climate, climate_emit, climate::emit) X(climate, climate_addDust, climate::addDust) \
	X(climate, climate_landIce, climate::landIce)
#define DM_STUBS_HYDRO(X) \
	X(hydro, hydro_update, hydro::update) X(hydro, hydro_riverT, hydro::riverT) X(hydro, hydro_isRiver, hydro::isRiver)
#define DM_STUBS_LIFE(X) \
	X(life, life_classify, life::classify) X(life, life_capacity, life::capacity) X(life, life_step, life::step) \
	X(life, life_burn, life::burn) X(life, life_ignite, life::ignite) X(life, life_seed, life::seed) \
	X(life, life_bloom, life::bloom) X(life, life_fill, life::fill) X(life, life_habitability, life::habitability) \
	X(life, life_rebuild, life::rebuild) X(life, life_cover, life::cover)
#define DM_STUBS_PATH(X) \
	X(path, path_find, path::find) X(path, path_stepCost, path::stepCost) X(path, path_reach, path::reach) \
	X(path, path_cost, path::cost)
#define DM_STUBS_UNITS(X) \
	X(units, units_spawn, units::spawn) X(units, units_step, units::step) X(units, units_kill, units::kill) \
	X(units, units_near, units::near) X(units, units_cellOf, units::cellOf) X(units, units_dirOf, units::dirOf)
#define DM_STUBS_CIV(X) \
	X(civ, civ_found, civ::found) X(civ, civ_claim, civ::claim) X(civ, civ_rebuild, civ::rebuild) \
	X(civ, civ_destroy, civ::destroy) X(civ, civ_damage, civ::damage) X(civ, civ_capture, civ::capture) \
	X(civ, civ_setCulture, civ::setCulture) X(civ, civ_electCapital, civ::electCapital) \
	X(civ, civ_infect, civ::infect) X(civ, civ_cure, civ::cure) X(civ, civ_at, civ::at) X(civ, civ_near, civ::near) \
	X(civ, civ_live, civ::live) X(civ, civ_canSettle, civ::canSettle) X(civ, civ_siteScore, civ::siteScore) \
	X(civ, civ_bestSite, civ::bestSite) X(civ, civ_ageProgress, civ::ageProgress)
#define DM_STUBS_ECONOMY(X) \
	X(economy, civ_yearly, civ::yearly) X(economy, civ_capacity, civ::capacity)
#define DM_STUBS_SOCIETY(X) \
	X(society, society_newCulture, society::newCulture) X(society, society_yearly, society::yearly) \
	X(society, society_witness, society::witness) X(society, society_regard, society::regard) \
	X(society, society_raiseProphet, society::raiseProphet) X(society, society_setEdict, society::setEdict) \
	X(society, society_aweIncome, society::aweIncome)
#define DM_STUBS_DIPLOMACY(X) \
	X(diplomacy, society_rel, society::rel) X(diplomacy, society_peek, society::peek) \
	X(diplomacy, society_atWar, society::atWar) X(diplomacy, society_declareWar, society::declareWar) \
	X(diplomacy, society_makePeace, society::makePeace) X(diplomacy, society_onArrive, society::onArrive) \
	X(diplomacy, society_diplomacy, society::diplomacy)
#define DM_STUBS_POWERS(X) \
	X(powers, powers_unlocked, powers::unlocked) X(powers, powers_cost, powers::cost) X(powers, powers_can, powers::can) \
	X(powers, powers_use, powers::use)
#define DM_STUBS_DISASTERS(X) \
	X(disasters, powers_start, powers::start) X(disasters, powers_update, powers::update) \
	X(disasters, powers_tick, powers::tick) X(disasters, powers_natural, powers::natural)
#define DM_STUBS_CHRONICLE(X) \
	X(chronicle, chronicle_attach, chronicle::attach) X(chronicle, chronicle_add, chronicle::add) \
	X(chronicle, chronicle_yearStr, chronicle::yearStr) X(chronicle, chronicle_epithet, chronicle::epithet) \
	X(chronicle, chronicle_updateEpithets, chronicle::updateEpithets) X(chronicle, chronicle_snapshot, chronicle::snapshot) \
	X(chronicle, chronicle_tale, chronicle::tale)
#define DM_STUBS_SIM(X) \
	X(sim, sim_create, sim::create) X(sim, sim_use, sim::use) X(sim, sim_rederive, sim::rederive) X(sim, sim_tick, sim::tick) \
	X(sim, sim_advance, sim::advance) X(sim, sim_setPhase, sim::setPhase) X(sim, sim_refreshStats, sim::refreshStats) \
	X(sim, sim_checkEnding, sim::checkEnding) X(sim, sim_score, sim::score) X(sim, sim_mood, sim::mood) \
	X(sim, sim_genesis_progress, sim::genesis::progress) X(sim, sim_genesis_next, sim::genesis::next) \
	X(sim, sim_genesis_skip, sim::genesis::skip) X(sim, sim_sample, sim::sample) X(sim, sim_fingerprint, sim::fingerprint) \
	X(sim, sim_firstNonFinite, sim::firstNonFinite) X(sim, sim_isYearlyTick, sim::isYearlyTick) X(sim, sim_runTicks, sim::runTicks)
#define DM_STUBS_BALANCE(X) \
	X(balance, balance_apply, balance::apply) X(balance, balance_set, balance::set) X(balance, balance_keys, balance::keys)
#define DM_STUBS_SAVE(X) \
	X(save, save_metaOf, save::metaOf) X(save, save_encode, save::encode) X(save, save_decode, save::decode) \
	X(save, save_peek, save::peek) X(save, save_writeFile, save::writeFile) X(save, save_readFile, save::readFile) \
	X(save, save_readMeta, save::readMeta) X(save, save_slotPath, save::slotPath) X(save, save_writeSlot, save::writeSlot) \
	X(save, save_readSlot, save::readSlot) X(save, save_listSlots, save::listSlots) X(save, save_removeSlot, save::removeSlot) \
	X(save, save_defaultSettings, save::defaultSettings) X(save, save_loadSettings, save::loadSettings) \
	X(save, save_saveSettings, save::saveSettings)

#define DM_STUBS_ALL(X) \
	DM_STUBS_NAMES(X) DM_STUBS_TERRAIN(X) DM_STUBS_CLIMATE(X) DM_STUBS_HYDRO(X) DM_STUBS_LIFE(X) DM_STUBS_PATH(X) \
	DM_STUBS_UNITS(X) DM_STUBS_CIV(X) DM_STUBS_ECONOMY(X) DM_STUBS_SOCIETY(X) DM_STUBS_DIPLOMACY(X) DM_STUBS_POWERS(X) \
	DM_STUBS_DISASTERS(X) DM_STUBS_CHRONICLE(X) DM_STUBS_SIM(X) DM_STUBS_BALANCE(X) DM_STUBS_SAVE(X)

namespace dmt::stub {

// One std::function per module function, with exactly its signature (default arguments are not
// part of it: a hook always receives every argument).
struct Hooks {
#define DM_HOOK_MEMBER(mod, member, qual) std::function<std::remove_pointer_t<decltype(&dm::qual)>> member;
	DM_STUBS_ALL(DM_HOOK_MEMBER)
#undef DM_HOOK_MEMBER
};

struct StubFn { const char* module; const char* hook; };
inline constexpr StubFn kStubFns[] = {
#define DM_STUB_ROW(mod, member, qual) { #mod, #member },
	DM_STUBS_ALL(DM_STUB_ROW)
#undef DM_STUB_ROW
};
inline constexpr int kStubFnCount = int(sizeof(kStubFns) / sizeof(kStubFns[0]));
// The stub modules (one per SimTests/stubs/<Module>_stub.cpp), lower case.
inline constexpr const char* kStubModules[] = { "names", "terrain", "climate", "hydro", "life", "path", "units", "civ",
	"economy", "society", "diplomacy", "powers", "disasters", "chronicle", "sim", "balance", "save" };

inline Hooks& hooks() { static Hooks h; return h; }
inline bool& recording() { static bool on = false; return on; }
inline std::vector<std::string>& calls() { static std::vector<std::string> v; return v; }
inline std::set<std::string>& linked() { static std::set<std::string> s; return s; }

// Stubs call this (through DM_STUB).
inline void record(const char* hook) { if (recording()) calls().emplace_back(hook); }
// Number of recorded calls of a hook name ("civ_found").
inline int called(const std::string& hook) { return int(std::count(calls().begin(), calls().end(), hook)); }
// True when module `mod` ("units", "economy", ...) is linked as its stub in this build.
inline bool stubbed(const std::string& mod) { return linked().count(mod) != 0; }
// Clear every hook and the call log.
inline void reset() { hooks() = Hooks{}; calls().clear(); recording() = false; }

// RAII: resets now and at scope exit; record = turn the call log on for the scope.
struct Guard {
	explicit Guard(bool record = false) { reset(); recording() = record; }
	~Guard() { reset(); }
	Guard(const Guard&) = delete;
	Guard& operator=(const Guard&) = delete;
};

// Each stub file registers its module once (static object).
struct Mark {
	explicit Mark(const char* mod) { linked().insert(mod); }
};

} // namespace dmt::stub

// First statement of every stub function: record the call; if a hook is installed, return its
// result (also valid in void functions: `return voidCall();`).
#define DM_STUB(member, ...)                                                       \
	do {                                                                           \
		::dmt::stub::record(#member);                                              \
		if (::dmt::stub::hooks().member) return ::dmt::stub::hooks().member(__VA_ARGS__); \
	} while (0)
#define DM_STUB_MODULE(mod) static ::dmt::stub::Mark dmStubMark_##mod(#mod)
