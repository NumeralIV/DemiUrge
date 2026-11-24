// DEMIURGE — Sim/Omens.cpp : the NEUTRAL initial omens module (written by the contracts track M0, 2026-11-20).
// Every function does nothing / refuses / returns the neutral value, so the game is v3.1 bit for bit and the
// Unreal build links before the module exists. The owner (omens track (M11)) replaces this file entirely;
// keep SimTests/stubs/Omens_stub.cpp matching Omens.h. Honest reads (real answers from world data): none.
#include "Omens.h"


namespace dm::omens {

void yearly(World& w) {
	(void)w;
}
int foresee(World& w, OmenKind kind, int cell, double due, double power, double ang) {
	(void)w; (void)kind; (void)cell; (void)due; (void)power; (void)ang;
	return -1;
}
int foretell(World& w, int sid, double due, int utteranceId) {
	(void)w; (void)sid; (void)due; (void)utteranceId;
	return -1;
}
bool struck(World& w, int omenId) {
	(void)w; (void)omenId;
	return false;
}
bool warn(World& w, int omenId, WarningWay way) {
	(void)w; (void)omenId; (void)way;
	return false;
}
bool foresightOn(const World& w) {
	(void)w;
	return false;
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

} // namespace dm::omens
