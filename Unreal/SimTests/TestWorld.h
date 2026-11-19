// DEMIURGE — SimTests/TestWorld.h : synthetic worlds for module tests (port of tests/helpers/world.js).
//
// Sim modules are built in parallel, so a module's tests must not depend on other modules being
// real or correct (in `run.ps1 <module>` every other module is a neutral stub). This helper builds a
// plausible planet using only the foundation and fills simple stand-ins for every derived field.
//
//   auto w = dmt::makeWorld();                               // L 4, seed 1, 40 % land, sea level 0, basics
//   auto w = dmt::makeWorld({ .L = 5, .seed = 3, .basics = false });   // terrain only
//   dmt::fillBasics(*w);          // (re)compute temp, moist, biome, ice, veg, fauna, fert, hab, slope, coast, fresh,
//                                 // and w.cache.landId / landCount / landHab (landmasses by size, hab > 0.05 per landmass)
//   dmt::findLand(*w, pred)       // first land cell with pred(c), or -1
//   dmt::bestHab(*w)              // cell with the highest hab
//   dmt::checkFinite(*w)          // "" or "field[c]" / scalar / entity ("unit 7") of the first NaN / infinity
//   dmt::capture(*w, fn)          // events emitted on w while fn runs
//   dmt::hash(*w)                 // order-sensitive fingerprint of the persisted state (determinism checks)
//   dmt::watch(*w)                // check every event on w against kEventTable (makeWorld does this)
//   dmt::sharedGeo(L)             // one cached Geo per level for tests (tests may have statics)
//   dmt::checkEvent(e, n)         // "" or what is wrong with one event (n = world cell count)
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "State.h"
#include "TestFramework.h"

namespace dmt {

struct MakeOpts {
	int L = 4;
	uint32_t seed = 1;
	double land = 0.4;                   // target land fraction
	bool basics = true;                  // fill the derived stand-ins
	double sea = 0;                      // sea level
	dm::Phase phase = dm::Phase::History;
	bool watch = true;                   // check every emitted event against the event table
	std::shared_ptr<const dm::Geo> geo;  // default: sharedGeo(L)
};

std::shared_ptr<const dm::Geo> sharedGeo(int L);
std::unique_ptr<dm::World> makeWorld(const MakeOpts& o = {});
dm::World& fillBasics(dm::World& w);
int findLand(const dm::World& w, const std::function<bool(int)>& pred = nullptr);
int bestHab(const dm::World& w);
std::string checkFinite(const dm::World& w);
std::vector<dm::Event> capture(dm::World& w, const std::function<void()>& fn);
uint32_t hash(const dm::World& w);
void watch(dm::World& w);
std::string checkEvent(const dm::Event& e, int n);

} // namespace dmt
