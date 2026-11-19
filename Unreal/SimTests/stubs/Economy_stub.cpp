// DEMIURGE — SimTests/stubs/Economy_stub.cpp : neutral stand-in for the economy half of the Civ
// namespace (Sim/Economy.cpp; see StubHooks.h). Owner: economy engineer (keep it matching the
// [economy] declarations of Civ.h). Written by foundation-api.
// Neutral: yearly does nothing; capacity is 0.
#include "StubHooks.h"

DM_STUB_MODULE(economy);

namespace dm::civ {

void yearly(World& w) {
	DM_STUB(civ_yearly, w);
}
double capacity(const World& w, const Settlement& s) {
	DM_STUB(civ_capacity, w, s);
	return 0.0;
}

} // namespace dm::civ
