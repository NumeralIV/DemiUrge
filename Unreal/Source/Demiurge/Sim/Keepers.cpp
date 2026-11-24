// DEMIURGE — Sim/Keepers.cpp : the NEUTRAL initial keepers module (written by the contracts track M0, 2026-11-20).
// Every function does nothing / refuses / returns the neutral value, so the game is v3.1 bit for bit and the
// Unreal build links before the module exists. The owner (keepers track (M13)) replaces this file entirely;
// keep SimTests/stubs/Keepers_stub.cpp matching Keepers.h. Honest reads (real answers from world data): none.
#include "Keepers.h"


namespace dm::keepers {

void yearly(World& w) {
	(void)w;
}
int appoint(World& w, KeeperDomain domain, Temper temper, int cultureId) {
	(void)w; (void)domain; (void)temper; (void)cultureId;
	return -1;
}
bool dismiss(World& w, int id) {
	(void)w; (void)id;
	return false;
}
bool setTemper(World& w, int id, Temper temper) {
	(void)w; (void)id; (void)temper;
	return false;
}
int slots(const World& w) {
	(void)w;
	return 0;
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

} // namespace dm::keepers
