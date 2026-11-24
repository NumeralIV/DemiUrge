// DEMIURGE — SimTests/stubs/Doctrine_stub.cpp : neutral stand-in for Sim/Doctrine.cpp (see StubHooks.h). Owner: doctrine track (M3 / M3b / M3c)
// (keep it matching Doctrine.h). Written by the contracts track (M0, 2026-11-20).
// Neutral: every mutator does nothing or refuses, every query returns the neutral value; honest reads: decree, kept, strictness, deathBelief.
#include "StubHooks.h"

DM_STUB_MODULE(doctrine);

namespace dm::doctrine {

void yearly(World& w) {
	DM_STUB(doctrine_yearly, w);
	(void)w;
}
int speak(World& w, int cultureId, const WordOpts& o) {
	DM_STUB(doctrine_speak, w, cultureId, o);
	(void)w; (void)cultureId; (void)o;
	return -1;
}
SpeakCheck canSpeak(const World& w, int cultureId, const WordOpts& o) {
	DM_STUB(doctrine_canSpeak, w, cultureId, o);
	(void)w; (void)cultureId; (void)o;
	return SpeakCheck{};
}
double wordCost(const World& w, int cultureId, const WordOpts& o) {
	DM_STUB(doctrine_wordCost, w, cultureId, o);
	(void)w; (void)cultureId; (void)o;
	return 0.0;
}
double fidelity(const World& w, int cultureId, const WordOpts& o) {
	DM_STUB(doctrine_fidelity, w, cultureId, o);
	(void)w; (void)cultureId; (void)o;
	return 0.0;
}
const Decree* decree(const World& w, int cultureId, DecreeArea area) {
	DM_STUB(doctrine_decree, w, cultureId, area);
	for (const Decree& d : w.mythos.decrees) if (d.culture == cultureId && d.area == area) return &d;
	return nullptr;
}
double kept(const World& w, int cultureId, DecreeArea area) {
	DM_STUB(doctrine_kept, w, cultureId, area);
	for (const Decree& d : w.mythos.decrees) if (d.culture == cultureId && d.area == area) return d.kept;
	return 0.0;
}
double burden(const World& w, int cultureId) {
	DM_STUB(doctrine_burden, w, cultureId);
	(void)w; (void)cultureId;
	return 0.0;
}
double strictness(const World& w, DecreeArea area, int stance) {
	DM_STUB(doctrine_strictness, w, area, stance);
	const DoctrineTune& T = w.tune<DoctrineTune>();
	const size_t a = size_t(area);
	if (a >= T.strictness.size() || stance < 0 || stance >= stanceCount(area)) return 0.0;
	return T.strictness[a][size_t(stance)];
}
int customStance(const World& w, int cultureId, DecreeArea area) {
	DM_STUB(doctrine_customStance, w, cultureId, area);
	(void)w; (void)cultureId; (void)area;
	return -1;
}
int swearPact(World& w, int cultureId, GodTerm godTerm, PeopleTerm peopleTerm, double years, int figure) {
	DM_STUB(doctrine_swearPact, w, cultureId, godTerm, peopleTerm, years, figure);
	(void)w; (void)cultureId; (void)godTerm; (void)peopleTerm; (void)years; (void)figure;
	return -1;
}
bool reveal(World& w, int cultureId, RevelationKind kind, Clarity clarity) {
	DM_STUB(doctrine_reveal, w, cultureId, kind, clarity);
	(void)w; (void)cultureId; (void)kind; (void)clarity;
	return false;
}
DeathBelief deathBelief(const World& w, int cultureId) {
	DM_STUB(doctrine_deathBelief, w, cultureId);
	if (cultureId < 0 || size_t(cultureId) >= w.mythos.faith.size()) return DeathBelief::None;
	const int d = w.mythos.faith[size_t(cultureId)].death;
	return d >= 0 && d < enumCount<DeathBelief>() ? DeathBelief(d) : DeathBelief::None;
}
bool answer(World& w, int cardId, int choice) {
	DM_STUB(doctrine_answer, w, cardId, choice);
	(void)w; (void)cardId; (void)choice;
	return false;
}
void lapse(World& w, int cardId) {
	DM_STUB(doctrine_lapse, w, cardId);
	(void)w; (void)cardId;
}
std::vector<CardOption> options(const World& w, int cardId) {
	DM_STUB(doctrine_options, w, cardId);
	(void)w; (void)cardId;
	return {};
}

} // namespace dm::doctrine
