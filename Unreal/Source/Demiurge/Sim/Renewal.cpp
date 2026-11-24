// DEMIURGE — Sim/Renewal.cpp : the NEUTRAL initial renewal module (written by the contracts track M0, 2026-11-20).
// Every function does nothing / refuses / returns the neutral value, so the game is v3.1 bit for bit and the
// Unreal build links before the module exists. The owner (renewal track (M14)) replaces this file entirely;
// keep SimTests/stubs/Renewal_stub.cpp matching Renewal.h. Honest reads (real answers from world data): none.
#include "Renewal.h"


namespace dm::renewal {

void yearly(World& w) {
	(void)w;
}
bool begin(World& w, RenewalKind kind, const std::vector<int>& remnant) {
	(void)w; (void)kind; (void)remnant;
	return false;
}
bool active(const World& w) {
	(void)w;
	return false;
}
bool remnant(const World& w, int sid) {
	(void)w; (void)sid;
	return false;
}
std::unique_ptr<World> reseed(const World& old, const ReseedOpts& opts) {
	(void)old; (void)opts;
	return nullptr;
}
bool answer(World& w, int cardId, int choice) {
	(void)w; (void)cardId; (void)choice;
	return false;
}
void lapse(World& w, int cardId) {
	(void)w; (void)cardId;
}
std::vector<CardOption> options(const World& w, int cardId) {
	(void)w; (void)cardId;
	return {};
}

} // namespace dm::renewal
