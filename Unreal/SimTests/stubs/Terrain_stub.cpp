// DEMIURGE — SimTests/stubs/Terrain_stub.cpp : neutral stand-in for Sim/Terrain.cpp (see StubHooks.h).
// Owner: terrain engineer (keep it matching Terrain.h). Written by foundation-api.
// Neutral: every operation is a no-op (elev is never touched); sculpt changes 0 cells; addElev
// returns the unchanged elevation.
#include "StubHooks.h"

DM_STUB_MODULE(terrain);

namespace dm::terrain {

void generate(World& w) {
	DM_STUB(terrain_generate, w);
}
int sculpt(World& w, int c, double ang, double amount) {
	DM_STUB(terrain_sculpt, w, c, ang, amount);
	return 0;
}
void crater(World& w, int c, double ang, double depth) {
	DM_STUB(terrain_crater, w, c, ang, depth);
}
void cone(World& w, int c, double ang, double height) {
	DM_STUB(terrain_cone, w, c, ang, height);
}
void roughen(World& w, int c, double ang, double amount, Rng& rng) {
	DM_STUB(terrain_roughen, w, c, ang, amount, rng);
}
double addElev(World& w, int c, double amount) {
	DM_STUB(terrain_addElev, w, c, amount);
	return w.valid(c) ? double(w.elev[size_t(c)]) : 0.0;
}
void touched(World& w) {
	DM_STUB(terrain_touched, w);
}
void derive(World& w) {
	DM_STUB(terrain_derive, w);
}

} // namespace dm::terrain
