// DEMIURGE — SimTests/stubs/Balance_stub.cpp : neutral stand-in for Sim/Balance.cpp (see StubHooks.h).
// Owner: sim engineer, later the balance owner (keep it matching Balance.h). Written by foundation-api.
// Neutral: no overrides; set accepts no key; keys is empty.
#include "StubHooks.h"

DM_STUB_MODULE(balance);

namespace dm::balance {

void apply(World& w) {
	DM_STUB(balance_apply, w);
}
bool set(World& w, std::string_view key, double value) {
	DM_STUB(balance_set, w, key, value);
	return false;
}
std::vector<std::string> keys() {
	DM_STUB(balance_keys);
	return {};
}

} // namespace dm::balance
