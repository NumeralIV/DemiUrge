// DEMIURGE — SimTests/stubs/Society_stub.cpp : neutral stand-in for the belief half of the Society
// namespace (Sim/Society.cpp; see StubHooks.h). Owner: society engineer (keep it matching the
// [society] declarations of Society.h). Written by foundation-api.
// Neutral: newCulture refuses (nullptr), yearly does nothing, witness reaches nobody (0),
// regard is zeros with mood Forgotten, raiseProphet nullptr, setEdict false, aweIncome 0.
#include "StubHooks.h"

DM_STUB_MODULE(society);

namespace dm::society {

Culture* newCulture(World& w, const NewCultureOpts& opts) {
	DM_STUB(society_newCulture, w, opts);
	return nullptr;
}
void yearly(World& w) {
	DM_STUB(society_yearly, w);
}
int witness(World& w, int cell, double ang, const WitnessOpts& opts) {
	DM_STUB(society_witness, w, cell, ang, opts);
	return 0;
}
Regard regard(const World& w, int cultureId) {
	DM_STUB(society_regard, w, cultureId);
	return Regard{};
}
Prophet* raiseProphet(World& w, int sid, const RaiseOpts& opts) {
	DM_STUB(society_raiseProphet, w, sid, opts);
	(void)opts;
	return nullptr;
}
bool setEdict(World& w, int cultureId, Edict edict) {
	DM_STUB(society_setEdict, w, cultureId, edict);
	return false;
}
double aweIncome(const World& w) {
	DM_STUB(society_aweIncome, w);
	return 0.0;
}
Memory memory(const World& w, int cultureId) {
	DM_STUB(society_memory, w, cultureId);
	return Memory{};
}
bool raiseWonder(World& w, int sid) {
	DM_STUB(society_raiseWonder, w, sid);
	return false;
}
// (v4 mythos, M0) neutral: nothing changes
bool shapeTraits(World& w, int cultureId, const Traits& t) {
	DM_STUB(society_shapeTraits, w, cultureId, t);
	return false;
}
void nudge(World& w, int sid, double dDevotion, double dLove) {
	DM_STUB(society_nudge, w, sid, dDevotion, dLove);
}
void remember(World& w, int cultureId, double grace, double wrath) {
	DM_STUB(society_remember, w, cultureId, grace, wrath);
}
Culture* secede(World& w, const std::vector<int>& towns, BirthReason reason, int rivalId) {
	DM_STUB(society_secede, w, towns, reason, rivalId);
	return nullptr;
}
bool answerPrayer(World& w, int sid, double scale) {
	DM_STUB(society_answerPrayer, w, sid, scale);
	return false;
}

} // namespace dm::society
