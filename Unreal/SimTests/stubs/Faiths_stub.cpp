// DEMIURGE — SimTests/stubs/Faiths_stub.cpp : neutral stand-in for Sim/Faiths.cpp (see StubHooks.h). Owner: faiths track (M12 / M12b)
// (keep it matching Faiths.h). Written by the contracts track (M0, 2026-11-20).
// Neutral: every mutator does nothing or refuses, every query returns the neutral value; honest reads: house, bearers.
#include "StubHooks.h"

DM_STUB_MODULE(faiths);

namespace dm::faiths {

void yearly(World& w) {
	DM_STUB(faiths_yearly, w);
	(void)w;
}
const House* house(const World& w, int cultureId) {
	DM_STUB(faiths_house, w, cultureId);
	for (const House& h : w.mythos.houses) if (h.culture == cultureId) return &h;
	return nullptr;
}
bool adjustHouse(World& w, int cultureId, double dStrength, double dWealth, double dAuthority, double dCorruption) {
	DM_STUB(faiths_adjustHouse, w, cultureId, dStrength, dWealth, dAuthority, dCorruption);
	(void)w; (void)cultureId; (void)dStrength; (void)dWealth; (void)dAuthority; (void)dCorruption;
	return false;
}
int seedRival(World& w, int cultureId, int sid, double following) {
	DM_STUB(faiths_seedRival, w, cultureId, sid, following);
	(void)w; (void)cultureId; (void)sid; (void)following;
	return -1;
}
bool chooseBearers(World& w, int cultureId, Charge charge) {
	DM_STUB(faiths_chooseBearers, w, cultureId, charge);
	(void)w; (void)cultureId; (void)charge;
	return false;
}
int bearers(const World& w) {
	DM_STUB(faiths_bearers, w);
	for (size_t i = 0; i < w.mythos.faith.size(); i++) if (w.mythos.faith[i].bearer >= 0) return int(i);
	return -1;
}
bool answer(World& w, int cardId, int choice) {
	DM_STUB(faiths_answer, w, cardId, choice);
	(void)w; (void)cardId; (void)choice;
	return false;
}
void lapse(World& w, int cardId) {
	DM_STUB(faiths_lapse, w, cardId);
	(void)w; (void)cardId;
}
std::vector<CardOption> options(const World& w, int cardId) {
	DM_STUB(faiths_options, w, cardId);
	(void)w; (void)cardId;
	return {};
}

} // namespace dm::faiths
