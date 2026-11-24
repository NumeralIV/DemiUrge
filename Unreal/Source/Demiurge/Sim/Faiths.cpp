// DEMIURGE — Sim/Faiths.cpp : the NEUTRAL initial faiths module (written by the contracts track M0, 2026-11-20).
// Every function does nothing / refuses / returns the neutral value, so the game is v3.1 bit for bit and the
// Unreal build links before the module exists. The owner (faiths track (M12 / M12b)) replaces this file entirely;
// keep SimTests/stubs/Faiths_stub.cpp matching Faiths.h. Honest reads (real answers from world data): house, bearers.
#include "Faiths.h"


namespace dm::faiths {

void yearly(World& w) {
	(void)w;
}
const House* house(const World& w, int cultureId) {
	for (const House& h : w.mythos.houses) if (h.culture == cultureId) return &h;
	return nullptr;
}
bool adjustHouse(World& w, int cultureId, double dStrength, double dWealth, double dAuthority, double dCorruption) {
	(void)w; (void)cultureId; (void)dStrength; (void)dWealth; (void)dAuthority; (void)dCorruption;
	return false;
}
int seedRival(World& w, int cultureId, int sid, double following) {
	(void)w; (void)cultureId; (void)sid; (void)following;
	return -1;
}
bool chooseBearers(World& w, int cultureId, Charge charge) {
	(void)w; (void)cultureId; (void)charge;
	return false;
}
int bearers(const World& w) {
	for (size_t i = 0; i < w.mythos.faith.size(); i++) if (w.mythos.faith[i].bearer >= 0) return int(i);
	return -1;
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

} // namespace dm::faiths
