// DEMIURGE — SimTests/stubs/Mythos_stub.cpp : neutral stand-in for Sim/Mythos.cpp (see StubHooks.h). Owner: mythos-core track (M1 / M1b / M1c)
// (keep it matching Mythos.h). Written by the contracts track (M0, 2026-11-20).
// Neutral: every mutator does nothing or refuses, every query returns the neutral value; honest reads: factors, aspect, archetypeParams, rite, faith.
#include "StubHooks.h"

DM_STUB_MODULE(mythos);

namespace dm::mythos {

void attach(World& w) {
	DM_STUB(mythos_attach, w);
	(void)w;
}
void rebuild(World& w) {
	DM_STUB(mythos_rebuild, w);
	(void)w;
}
void yearly(World& w) {
	DM_STUB(mythos_yearly, w);
	(void)w;
}
Legacy legacy(const World& w) {
	DM_STUB(mythos_legacy, w);
	(void)w;
	return Legacy::None;
}
LegacyScores legacyScores(const World& w) {
	DM_STUB(mythos_legacyScores, w);
	(void)w;
	return LegacyScores{};
}
Factors factors(const World& w, int cultureId) {
	DM_STUB(mythos_factors, w, cultureId);
	if (cultureId < 0 || size_t(cultureId) >= w.mythos.factors.size()) return Factors{};
	return w.mythos.factors[size_t(cultureId)];
}
TownFactors townFactors(const World& w, int sid) {
	DM_STUB(mythos_townFactors, w, sid);
	(void)w; (void)sid;
	return TownFactors{};
}
WitnessScale witnessScale(const World& w, int cell, std::string_view kind) {
	DM_STUB(mythos_witnessScale, w, cell, kind);
	(void)w; (void)cell; (void)kind;
	return WitnessScale{};
}
Aspect aspect(const World& w, int cultureId) {
	DM_STUB(mythos_aspect, w, cultureId);
	if (cultureId < 0 || size_t(cultureId) >= w.mythos.faith.size()) return Aspect::Unknown;
	return w.mythos.faith[size_t(cultureId)].aspect;
}
int warGoal(const World& w, int cultureId) {
	DM_STUB(mythos_warGoal, w, cultureId);
	(void)w; (void)cultureId;
	return -1;
}
double neglectScale(const World& w, int cultureId) {
	DM_STUB(mythos_neglectScale, w, cultureId);
	(void)w; (void)cultureId;
	return 1.0;
}
ArchetypeParams archetypeParams(const World& w) {
	DM_STUB(mythos_archetypeParams, w);
	const size_t a = size_t(w.settings.archetype);
	const MythosTune& T = w.tune<MythosTune>();
	return a < T.archetypes.size() ? T.archetypes[a] : ArchetypeParams{};
}
Rite rite(const World& w, int cultureId) {
	DM_STUB(mythos_rite, w, cultureId);
	for (const Decree& d : w.mythos.decrees)
		if (d.culture == cultureId && d.area == DecreeArea::Worship && d.stance >= 0 && d.stance < stanceCount(DecreeArea::Worship))
			return Rite(d.stance);
	return Rite::None;
}
const Faith* faith(const World& w, int cultureId) {
	DM_STUB(mythos_faith, w, cultureId);
	if (cultureId < 0 || size_t(cultureId) >= w.mythos.faith.size()) return nullptr;
	return &w.mythos.faith[size_t(cultureId)];
}
bool setPresence(World& w, Presence p) {
	DM_STUB(mythos_setPresence, w, p);
	(void)w; (void)p;
	return false;
}
bool withdraw(World& w, bool announced) {
	DM_STUB(mythos_withdraw, w, announced);
	(void)w; (void)announced;
	return false;
}
bool bless(World& w, int sid, BlessingKind kind) {
	DM_STUB(mythos_bless, w, sid, kind);
	(void)w; (void)sid; (void)kind;
	return false;
}
bool answer(World& w, int cardId, int choice) {
	DM_STUB(mythos_answer, w, cardId, choice);
	(void)w; (void)cardId; (void)choice;
	return false;
}
void lapse(World& w, int cardId) {
	DM_STUB(mythos_lapse, w, cardId);
	(void)w; (void)cardId;
}
std::vector<CardOption> options(const World& w, int cardId) {
	DM_STUB(mythos_options, w, cardId);
	(void)w; (void)cardId;
	return {};
}

} // namespace dm::mythos
