// DEMIURGE — SimTests/stubs/Characters_stub.cpp : neutral stand-in for Sim/Characters.cpp (see StubHooks.h). Owner: characters track (M4 / M4b / M4c)
// (keep it matching Characters.h). Written by the contracts track (M0, 2026-11-20).
// Neutral: every mutator does nothing or refuses, every query returns the neutral value; honest reads: ruler, messenger, credibility, figure.
#include "StubHooks.h"

DM_STUB_MODULE(characters);

namespace dm::characters {

void yearly(World& w) {
	DM_STUB(characters_yearly, w);
	(void)w;
}
std::vector<Figure> candidates(const World& w, int sid) {
	DM_STUB(characters_candidates, w, sid);
	(void)w; (void)sid;
	return {};
}
int choose(World& w, int sid, int index) {
	DM_STUB(characters_choose, w, sid, index);
	(void)w; (void)sid; (void)index;
	return -1;
}
int spawn(World& w, const Figure& proto) {
	DM_STUB(characters_spawn, w, proto);
	(void)w; (void)proto;
	return -1;
}
bool fell(World& w, int figureId, FigureFate fate) {
	DM_STUB(characters_fell, w, figureId, fate);
	(void)w; (void)figureId; (void)fate;
	return false;
}
int ruler(const World& w, int cultureId) {
	DM_STUB(characters_ruler, w, cultureId);
	for (const Figure& f : w.mythos.figures)
		if (f.culture == cultureId && f.kind == uint8_t(FigureKind::Ruler) && f.fate == uint8_t(FigureFate::Living)) return f.id;
	return -1;
}
int messenger(const World& w, int cultureId) {
	DM_STUB(characters_messenger, w, cultureId);
	for (const Figure& f : w.mythos.figures)
		if (f.culture == cultureId && f.kind == uint8_t(FigureKind::Messenger) && f.fate == uint8_t(FigureFate::Living)) return f.id;
	return -1;
}
double credibility(const World& w, int figureId) {
	DM_STUB(characters_credibility, w, figureId);
	for (const Figure& f : w.mythos.figures) if (f.id == figureId) return f.credibility;
	return 0.5;
}
bool adjustCredibility(World& w, int figureId, double delta) {
	DM_STUB(characters_adjustCredibility, w, figureId, delta);
	(void)w; (void)figureId; (void)delta;
	return false;
}
const Figure* figure(const World& w, int id) {
	DM_STUB(characters_figure, w, id);
	for (const Figure& f : w.mythos.figures) if (f.id == id) return &f;
	return nullptr;
}
bool test(World& w, int figureId, TrialKind kind) {
	DM_STUB(characters_test, w, figureId, kind);
	(void)w; (void)figureId; (void)kind;
	return false;
}
double trialOdds(const World& w, int figureId, TrialKind kind) {
	DM_STUB(characters_trialOdds, w, figureId, kind);
	(void)w; (void)figureId; (void)kind;
	return 0.0;
}
bool answer(World& w, int cardId, int choice) {
	DM_STUB(characters_answer, w, cardId, choice);
	(void)w; (void)cardId; (void)choice;
	return false;
}
void lapse(World& w, int cardId) {
	DM_STUB(characters_lapse, w, cardId);
	(void)w; (void)cardId;
}
std::vector<CardOption> options(const World& w, int cardId) {
	DM_STUB(characters_options, w, cardId);
	(void)w; (void)cardId;
	return {};
}

} // namespace dm::characters
