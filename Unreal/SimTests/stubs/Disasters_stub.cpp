// DEMIURGE — SimTests/stubs/Disasters_stub.cpp : neutral stand-in for the disasters half of the
// Powers namespace (Sim/Disasters.cpp; see StubHooks.h). Owner: disasters engineer (keep it
// matching the [disasters] declarations of Powers.h). Written by foundation-api.
// Neutral: start refuses (nullptr); update, tick and natural do nothing.
#include "StubHooks.h"

DM_STUB_MODULE(disasters);

namespace dm::powers {

Disaster* start(World& w, DisasterType type, int cell, const StartOpts& opts) {
	DM_STUB(powers_start, w, type, cell, opts);
	return nullptr;
}
void update(World& w, double realDt) {
	DM_STUB(powers_update, w, realDt);
}
void tick(World& w) {
	DM_STUB(powers_tick, w);
}
void natural(World& w) {
	DM_STUB(powers_natural, w);
}

} // namespace dm::powers
