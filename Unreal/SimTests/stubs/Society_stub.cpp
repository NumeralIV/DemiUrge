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
Prophet* raiseProphet(World& w, int sid) {
	DM_STUB(society_raiseProphet, w, sid);
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

} // namespace dm::society
