// DEMIURGE — SimTests/stubs/Renewal_stub.cpp : neutral stand-in for Sim/Renewal.cpp (see StubHooks.h). Owner: renewal track (M14)
// (keep it matching Renewal.h). Written by the contracts track (M0, 2026-11-20).
// Neutral: every mutator does nothing or refuses, every query returns the neutral value; honest reads: none.
#include "StubHooks.h"

DM_STUB_MODULE(renewal);

namespace dm::renewal {

void yearly(World& w) {
	DM_STUB(renewal_yearly, w);
	(void)w;
}
bool begin(World& w, RenewalKind kind, const std::vector<int>& remnant) {
	DM_STUB(renewal_begin, w, kind, remnant);
	(void)w; (void)kind; (void)remnant;
	return false;
}
bool active(const World& w) {
	DM_STUB(renewal_active, w);
	(void)w;
	return false;
}
bool remnant(const World& w, int sid) {
	DM_STUB(renewal_remnant, w, sid);
	(void)w; (void)sid;
	return false;
}
std::unique_ptr<World> reseed(const World& old, const ReseedOpts& opts) {
	DM_STUB(renewal_reseed, old, opts);
	(void)old; (void)opts;
	return nullptr;
}
bool answer(World& w, int cardId, int choice) {
	DM_STUB(renewal_answer, w, cardId, choice);
	(void)w; (void)cardId; (void)choice;
	return false;
}
void lapse(World& w, int cardId) {
	DM_STUB(renewal_lapse, w, cardId);
	(void)w; (void)cardId;
}
std::vector<CardOption> options(const World& w, int cardId) {
	DM_STUB(renewal_options, w, cardId);
	(void)w; (void)cardId;
	return {};
}

} // namespace dm::renewal
