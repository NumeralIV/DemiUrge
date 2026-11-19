// DEMIURGE — SimTests/stubs/Hydro_stub.cpp : neutral stand-in for Sim/Hydro.cpp (see StubHooks.h).
// Owner: hydro engineer (keep it matching Hydro.h). Written by foundation-api.
// Neutral: update does nothing; riverT is huge (no cell is a river); isRiver is false.
// (hydro::isLake is inline in Hydro.h and always real.)
#include "StubHooks.h"

DM_STUB_MODULE(hydro);

namespace dm::hydro {

void update(World& w) {
	DM_STUB(hydro_update, w);
}
double riverT(const World& w) {
	DM_STUB(hydro_riverT, w);
	return 1e30;
}
bool isRiver(const World& w, int c) {
	DM_STUB(hydro_isRiver, w, c);
	return false;
}

} // namespace dm::hydro
