// DEMIURGE — SimTests/stubs/Powers_stub.cpp : neutral stand-in for the powers half of the Powers
// namespace (Sim/Powers.cpp; see StubHooks.h). Owner: powers engineer (keep it matching the
// [powers] declarations of Powers.h). Written by foundation-api.
// Neutral: nothing is unlocked, everything costs 0, can is { false, Locked }, use does nothing and
// returns false. (The power table kPowers and byId are inline data in Powers.h, always real.)
#include "StubHooks.h"

DM_STUB_MODULE(powers);

namespace dm::powers {

bool unlocked(const World& w, std::string_view id) {
	DM_STUB(powers_unlocked, w, id);
	return false;
}
double cost(const World& w, std::string_view id, double ang, int cell) {
	DM_STUB(powers_cost, w, id, ang, cell);
	return 0.0;
}
CanResult can(const World& w, std::string_view id, int cell) {
	DM_STUB(powers_can, w, id, cell);
	return CanResult{};
}
bool use(World& w, std::string_view id, int cell, const UseOpts& opts) {
	DM_STUB(powers_use, w, id, cell, opts);
	return false;
}
const Upgrade* upgrade(const World& w, std::string_view id) {
	DM_STUB(powers_upgrade, w, id);
	return nullptr;
}
int drownRisk(const World& w, int cell, double ang) {
	DM_STUB(powers_drownRisk, w, cell, ang);
	return -1;
}

} // namespace dm::powers
