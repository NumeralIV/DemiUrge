// DEMIURGE — SimTests/stubs/Life_stub.cpp : neutral stand-in for Sim/Life.cpp (see StubHooks.h).
// Owner: life engineer (keep it matching Life.h). Written by foundation-api.
// Neutral: nothing grows, burns or is classified; capacity 0; ignite false; cover all zeros.
#include "StubHooks.h"

DM_STUB_MODULE(life);

namespace dm::life {

void classify(World& w) {
	DM_STUB(life_classify, w);
}
double capacity(const World& w, int c) {
	DM_STUB(life_capacity, w, c);
	return 0.0;
}
void step(World& w, int part, int parts) {
	DM_STUB(life_step, w, part, parts);
}
void burn(World& w) {
	DM_STUB(life_burn, w);
}
bool ignite(World& w, int c, double intensity) {
	DM_STUB(life_ignite, w, c, intensity);
	return false;
}
void seed(World& w, int c, double ang) {
	DM_STUB(life_seed, w, c, ang);
}
void bloom(World& w, int c, double ang, double amount) {
	DM_STUB(life_bloom, w, c, ang, amount);
}
void fill(World& w, double frac) {
	DM_STUB(life_fill, w, frac);
}
void habitability(World& w) {
	DM_STUB(life_habitability, w);
}
void rebuild(World& w) {
	DM_STUB(life_rebuild, w);
}
Cover cover(const World& w) {
	DM_STUB(life_cover, w);
	return Cover{};
}

} // namespace dm::life
