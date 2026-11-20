// DEMIURGE — SimTests/stubs/Diplomacy_stub.cpp : neutral stand-in for the relations half of the
// Society namespace (Sim/Diplomacy.cpp; see StubHooks.h). Owner: diplomacy engineer (keep it
// matching the [diplomacy] declarations of Society.h). Written by foundation-api.
// Neutral: declareWar / makePeace / onArrive / diplomacy do nothing. The data accessors rel
// (create on demand in vars.rel), peek and atWar answer honestly: they are plumbing, not rules,
// and other modules' tests set relations up through them.
#include "StubHooks.h"

DM_STUB_MODULE(diplomacy);

namespace dm::society {

Relation* rel(World& w, int a, int b) {
	DM_STUB(society_rel, w, a, b);
	if (a == b || a < 0 || b < 0 || size_t(a) >= w.cultures.size() || size_t(b) >= w.cultures.size()) return nullptr;
	return &w.vars.rel[state::relKey(a, b)];
}
const Relation* peek(const World& w, int a, int b) {
	DM_STUB(society_peek, w, a, b);
	if (a == b) return nullptr;
	const auto it = w.vars.rel.find(state::relKey(a, b));
	return it == w.vars.rel.end() ? nullptr : &it->second;
}
bool atWar(const World& w, int a, int b) {
	DM_STUB(society_atWar, w, a, b);
	if (a == b) return false;
	const auto it = w.vars.rel.find(state::relKey(a, b));
	return it != w.vars.rel.end() && it->second.war;
}
void declareWar(World& w, int a, int b) {
	DM_STUB(society_declareWar, w, a, b);
}
void makePeace(World& w, int a, int b, double truceYears, int victor) {
	DM_STUB(society_makePeace, w, a, b, truceYears, victor);
}
void onArrive(World& w, int unitId) {
	DM_STUB(society_onArrive, w, unitId);
}
void diplomacy(World& w) {
	DM_STUB(society_diplomacy, w);
}
bool swearPeace(World& w, int a, int b) {
	DM_STUB(society_swearPeace, w, a, b);
	return false;
}
std::vector<int> tense(const World& w, int cultureId) {
	DM_STUB(society_tense, w, cultureId);
	return {};
}
double swornUntil(const World& w, int a, int b) {
	DM_STUB(society_swornUntil, w, a, b);
	return -1;
}

} // namespace dm::society
