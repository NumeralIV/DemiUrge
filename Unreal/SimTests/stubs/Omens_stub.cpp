// DEMIURGE — SimTests/stubs/Omens_stub.cpp : neutral stand-in for Sim/Omens.cpp (see StubHooks.h). Owner: omens track (M11)
// (keep it matching Omens.h). Written by the contracts track (M0, 2026-11-20).
// Neutral: every mutator does nothing or refuses, every query returns the neutral value; honest reads: none.
#include "StubHooks.h"

DM_STUB_MODULE(omens);

namespace dm::omens {

void yearly(World& w) {
	DM_STUB(omens_yearly, w);
	(void)w;
}
int foresee(World& w, OmenKind kind, int cell, double due, double power, double ang) {
	DM_STUB(omens_foresee, w, kind, cell, due, power, ang);
	(void)w; (void)kind; (void)cell; (void)due; (void)power; (void)ang;
	return -1;
}
int foretell(World& w, int sid, double due, int utteranceId) {
	DM_STUB(omens_foretell, w, sid, due, utteranceId);
	(void)w; (void)sid; (void)due; (void)utteranceId;
	return -1;
}
bool struck(World& w, int omenId) {
	DM_STUB(omens_struck, w, omenId);
	(void)w; (void)omenId;
	return false;
}
bool warn(World& w, int omenId, WarningWay way) {
	DM_STUB(omens_warn, w, omenId, way);
	(void)w; (void)omenId; (void)way;
	return false;
}
bool foresightOn(const World& w) {
	DM_STUB(omens_foresightOn, w);
	(void)w;
	return false;
}
bool answer(World& w, int cardId, int choice) {
	DM_STUB(omens_answer, w, cardId, choice);
	(void)w; (void)cardId; (void)choice;
	return false;
}
void lapse(World& w, int cardId) {
	DM_STUB(omens_lapse, w, cardId);
	(void)w; (void)cardId;
}
std::vector<CardOption> options(const World& w, int cardId) {
	DM_STUB(omens_options, w, cardId);
	(void)w; (void)cardId;
	return {};
}

} // namespace dm::omens
