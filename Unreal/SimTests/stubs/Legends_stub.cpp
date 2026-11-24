// DEMIURGE — SimTests/stubs/Legends_stub.cpp : neutral stand-in for Sim/Legends.cpp (see StubHooks.h). Owner: legends track (M10)
// (keep it matching Legends.h). Written by the contracts track (M0, 2026-11-20).
// Neutral: every mutator does nothing or refuses, every query returns the neutral value; honest reads: none.
#include "StubHooks.h"

DM_STUB_MODULE(legends);

namespace dm::legends {

void yearly(World& w) {
	DM_STUB(legends_yearly, w);
	(void)w;
}
int add(World& w, const Legend& seed) {
	DM_STUB(legends_add, w, seed);
	(void)w; (void)seed;
	return -1;
}
std::vector<int> strongest(const World& w, int cultureId, int count) {
	DM_STUB(legends_strongest, w, cultureId, count);
	(void)w; (void)cultureId; (void)count;
	return {};
}
int importOld(World& fresh, const World& old, int count) {
	DM_STUB(legends_importOld, fresh, old, count);
	(void)fresh; (void)old; (void)count;
	return 0;
}

} // namespace dm::legends
