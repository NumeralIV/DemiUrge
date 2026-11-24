// DEMIURGE — SimTests/stubs/Keepers_stub.cpp : neutral stand-in for Sim/Keepers.cpp (see StubHooks.h). Owner: keepers track (M13)
// (keep it matching Keepers.h). Written by the contracts track (M0, 2026-11-20).
// Neutral: every mutator does nothing or refuses, every query returns the neutral value; honest reads: none.
#include "StubHooks.h"

DM_STUB_MODULE(keepers);

namespace dm::keepers {

void yearly(World& w) {
	DM_STUB(keepers_yearly, w);
	(void)w;
}
int appoint(World& w, KeeperDomain domain, Temper temper, int cultureId) {
	DM_STUB(keepers_appoint, w, domain, temper, cultureId);
	(void)w; (void)domain; (void)temper; (void)cultureId;
	return -1;
}
bool dismiss(World& w, int id) {
	DM_STUB(keepers_dismiss, w, id);
	(void)w; (void)id;
	return false;
}
bool setTemper(World& w, int id, Temper temper) {
	DM_STUB(keepers_setTemper, w, id, temper);
	(void)w; (void)id; (void)temper;
	return false;
}
int slots(const World& w) {
	DM_STUB(keepers_slots, w);
	(void)w;
	return 0;
}
bool answer(World& w, int cardId, int choice) {
	DM_STUB(keepers_answer, w, cardId, choice);
	(void)w; (void)cardId; (void)choice;
	return false;
}
void lapse(World& w, int cardId) {
	DM_STUB(keepers_lapse, w, cardId);
	(void)w; (void)cardId;
}
std::vector<CardOption> options(const World& w, int cardId) {
	DM_STUB(keepers_options, w, cardId);
	(void)w; (void)cardId;
	return {};
}

} // namespace dm::keepers
