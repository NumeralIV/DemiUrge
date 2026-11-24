// DEMIURGE — Sim/Legends.cpp : the NEUTRAL initial legends module (written by the contracts track M0, 2026-11-20).
// Every function does nothing / refuses / returns the neutral value, so the game is v3.1 bit for bit and the
// Unreal build links before the module exists. The owner (legends track (M10)) replaces this file entirely;
// keep SimTests/stubs/Legends_stub.cpp matching Legends.h. Honest reads (real answers from world data): none.
#include "Legends.h"


namespace dm::legends {

void yearly(World& w) {
	(void)w;
}
int add(World& w, const Legend& seed) {
	(void)w; (void)seed;
	return -1;
}
std::vector<int> strongest(const World& w, int cultureId, int count) {
	(void)w; (void)cultureId; (void)count;
	return {};
}
int importOld(World& fresh, const World& old, int count) {
	(void)fresh; (void)old; (void)count;
	return 0;
}

} // namespace dm::legends
