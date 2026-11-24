// DEMIURGE — SimTests/stubs/Cards_stub.cpp : neutral stand-in for Sim/Cards.cpp (see StubHooks.h). Owner: cards track (M2)
// (keep it matching Cards.h). Written by the contracts track (M0, 2026-11-20).
// Neutral: every mutator does nothing or refuses, every query returns the neutral value; honest reads: find, openCards.
#include "StubHooks.h"
#include <algorithm>

DM_STUB_MODULE(cards);

namespace dm::cards {

bool propose(World& w, const Proposal& p) {
	DM_STUB(cards_propose, w, p);
	(void)w; (void)p;
	return false;
}
void yearly(World& w) {
	DM_STUB(cards_yearly, w);
	(void)w;
}
int open(World& w, const Proposal& p) {
	DM_STUB(cards_open, w, p);
	(void)w; (void)p;
	return -1;
}
bool answer(World& w, int id, int choice) {
	DM_STUB(cards_answer, w, id, choice);
	(void)w; (void)id; (void)choice;
	return false;
}
bool withdraw(World& w, int id) {
	DM_STUB(cards_withdraw, w, id);
	(void)w; (void)id;
	return false;
}
bool lookIntoHeart(World& w, int id) {
	DM_STUB(cards_lookIntoHeart, w, id);
	(void)w; (void)id;
	return false;
}
std::vector<CardOption> options(const World& w, int id) {
	DM_STUB(cards_options, w, id);
	(void)w; (void)id;
	return {};
}
const Card* find(const World& w, int id) {
	DM_STUB(cards_find, w, id);
	for (const Card& c : w.mythos.cards) if (c.id == id) return &c;
	return nullptr;
}
std::vector<int> openCards(const World& w) {
	DM_STUB(cards_openCards, w);
	std::vector<int> out;
	for (const Card& c : w.mythos.cards) if (c.state == uint8_t(CardState::Open)) out.push_back(c.id);
	std::sort(out.begin(), out.end());
	return out;
}
double priority(const World& w, const Proposal& p) {
	DM_STUB(cards_priority, w, p);
	(void)w; (void)p;
	return 0.0;
}
bool answerOwn(World& w, int id, int choice) {
	DM_STUB(cards_answerOwn, w, id, choice);
	(void)w; (void)id; (void)choice;
	return false;
}
void lapseOwn(World& w, int id) {
	DM_STUB(cards_lapseOwn, w, id);
	(void)w; (void)id;
}
std::vector<CardOption> optionsOwn(const World& w, int id) {
	DM_STUB(cards_optionsOwn, w, id);
	(void)w; (void)id;
	return {};
}

} // namespace dm::cards
