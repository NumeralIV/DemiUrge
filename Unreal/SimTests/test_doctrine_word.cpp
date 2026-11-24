// DEMIURGE — SimTests/test_doctrine_word.cpp : the Word and how clearly it is spoken (SPEC-MYTHOS 3.9). Track M3.
#include "test_doctrine_fixture.h"

#include <map>

using namespace dm;
using namespace dmt;
using namespace dmt::doc;

namespace {
WordOpts decreeWord(DecreeArea a, int stance, Clarity c, Channel ch, int ref = -1) {
	WordOpts o; o.content = WordContent::Decree; o.area = a; o.stance = stance; o.clarity = c; o.channel = ch; o.ref = ref;
	return o;
}
}

DM_TEST("doctrine word: costs by clarity, presence and the sky (3.2, 3.9)") {
	DocWorld d = docWorld();
	World& w = *d.w;
	const WordOpts ex = decreeWord(DecreeArea::Care, 0, Clarity::Exact, Channel::Sky);
	near(doctrine::wordCost(w, d.vai, ex), 120, 1e-9, "Exact from the sky under Wonders: x 2");
	addMessenger(w, d.vai, d.towns[0]);
	near(doctrine::wordCost(w, d.vai, decreeWord(DecreeArea::Care, 0, Clarity::Exact, Channel::Messenger)), 60, 1e-9, "Exact through a messenger");
	near(doctrine::wordCost(w, d.vai, decreeWord(DecreeArea::Care, 0, Clarity::Parable, Channel::Messenger)), 50, 1e-9);
	near(doctrine::wordCost(w, d.vai, decreeWord(DecreeArea::Care, 0, Clarity::Dream, Channel::Dream)), 30, 1e-9);
	near(doctrine::wordCost(w, d.vai, decreeWord(DecreeArea::Care, 0, Clarity::Sign, Channel::Sky)), 90, 1e-9);
	w.mythos.presence = Presence::Messengers;
	near(doctrine::wordCost(w, d.vai, decreeWord(DecreeArea::Care, 0, Clarity::Dream, Channel::Dream)), 18, 1e-9, "Messengers: x 0.6");
	eq(doctrine::wordCost(w, 99, ex), 0.0, "bad culture");
	near(doctrine::canSpeak(w, d.vai, decreeWord(DecreeArea::Care, 0, Clarity::Dream, Channel::Dream)).cost, 18.0, 1e-9, "canSpeak reports the cost");
}

DM_TEST("doctrine word: one word per culture per 10 years, and every refusal") {
	DocWorld d = docWorld();
	World& w = *d.w;
	const WordOpts dream = decreeWord(DecreeArea::Care, 0, Clarity::Dream, Channel::Dream, d.towns[1]);
	ok(doctrine::canSpeak(w, d.vai, dream).ok, "ok at first");
	if (!ok(doctrine::speak(w, d.vai, dream) > 0)) return;
	const size_t nw = w.mythos.words.size();
	w.year += 5;
	eq(int(doctrine::canSpeak(w, d.vai, dream).why), int(doctrine::WordWhy::Gap), "five years later: gap");
	eq(doctrine::speak(w, d.vai, dream), -1);
	eq(w.mythos.words.size(), nw, "nothing recorded when refused");
	ok(doctrine::canSpeak(w, d.oru, dream).ok, "another people may be spoken to");
	w.year += 5;
	ok(doctrine::canSpeak(w, d.vai, dream).ok, "ten years: ok again");
	const auto why = [&](const WordOpts& o, int c = 0) { return int(doctrine::canSpeak(w, c, o).why); };
	eq(why(decreeWord(DecreeArea::Care, 0, Clarity::Parable, Channel::Messenger)), int(doctrine::WordWhy::NoMessenger), "a parable needs a messenger");
	eq(why(decreeWord(DecreeArea::Care, 0, Clarity::Exact, Channel::Messenger)), int(doctrine::WordWhy::Ok), "Exact from the sky under Wonders");
	eq(why(decreeWord(DecreeArea::Care, 3, Clarity::Dream, Channel::Dream)), int(doctrine::WordWhy::Content), "bad stance");
	eq(why(decreeWord(DecreeArea::Land, 0, Clarity::Dream, Channel::Dream)), int(doctrine::WordWhy::Content), "the Land decree is v2");
	WordOpts rep; rep.content = WordContent::Repeal; rep.area = DecreeArea::Wealth; rep.clarity = Clarity::Dream;
	eq(why(rep), int(doctrine::WordWhy::Content), "nothing to repeal");
	WordOpts warn; warn.content = WordContent::Warning; warn.clarity = Clarity::Dream;
	eq(why(warn), int(doctrine::WordWhy::Content), "warnings are v2");
	eq(why(dream, 99), int(doctrine::WordWhy::Target), "bad culture");
	w.mythos.presence = Presence::Messengers;
	eq(why(decreeWord(DecreeArea::Care, 0, Clarity::Exact, Channel::Sky)), int(doctrine::WordWhy::NoMessenger), "Messengers: no Exact from the sky");
	w.mythos.presence = Presence::Unseen;
	eq(why(decreeWord(DecreeArea::Care, 0, Clarity::Exact, Channel::Sky)), int(doctrine::WordWhy::Content), "Unseen: Dreams and Signs only");
	ok(doctrine::canSpeak(w, d.vai, decreeWord(DecreeArea::Care, 0, Clarity::Sign, Channel::Sky)).ok, "Unseen: a Sign");
	w.god.awe = 10;
	eq(why(decreeWord(DecreeArea::Care, 0, Clarity::Sign, Channel::Sky)), int(doctrine::WordWhy::Awe), "too little Awe");
	w.tune<MythosTune>().phase = 0;
	eq(why(dream), int(doctrine::WordWhy::Phase));
}

DM_TEST("doctrine word: the fidelity formula (3.9)") {
	DocWorld d = docWorld();
	World& w = *d.w;
	const WordOpts sky = decreeWord(DecreeArea::Care, 0, Clarity::Exact, Channel::Sky);
	near(doctrine::fidelity(w, d.vai, sky), 0.95 * (0.6 + 0.4 * 0.5), 1e-12, "no messenger: credibility 0.5");
	near(doctrine::fidelity(w, d.vai, decreeWord(DecreeArea::Care, 0, Clarity::Dream, Channel::Dream)), 0.55 * 0.8, 1e-12);
	near(doctrine::fidelity(w, d.vai, decreeWord(DecreeArea::Care, 0, Clarity::Sign, Channel::Sky)), 0.35 * 0.8, 1e-12);
	const int m = addMessenger(w, d.vai, d.towns[0], MessengerArchetype::None, 1.0);
	const WordOpts par = decreeWord(DecreeArea::Care, 0, Clarity::Parable, Channel::Messenger);
	near(doctrine::fidelity(w, d.vai, par), 0.70 * 1.0, 1e-12, "credibility 1");
	setDecree(w, d.vai, DecreeArea::Honesty, int(HonestyStance::Truth));
	near(doctrine::fidelity(w, d.vai, par), 0.70 * 1.1, 1e-12, "Truth + 0.1");
	setDecree(w, d.vai, DecreeArea::Honesty, int(HonestyStance::Discretion));
	near(doctrine::fidelity(w, d.vai, par), 0.70 * 1.05, 1e-12, "Discretion + 0.05");
	for (Figure& f : w.mythos.figures) if (f.id == m) f.archetype = uint8_t(MessengerArchetype::Child);
	near(doctrine::fidelity(w, d.vai, par), 0.70 * (1 + 0.05 + 0.15), 1e-12, "the Child + 0.15");
	setDecree(w, d.vai, DecreeArea::Honesty, int(HonestyStance::Truth));
	near(doctrine::fidelity(w, d.vai, decreeWord(DecreeArea::Care, 0, Clarity::Exact, Channel::Messenger)), 0.98, 1e-12, "capped at 0.98");
	for (Figure& f : w.mythos.figures) if (f.id == m) { f.archetype = uint8_t(MessengerArchetype::Unbelieved); f.credibility = 0.5; }
	near(doctrine::fidelity(w, d.vai, par), 0.70 * 0.8 * 1.1 * 0.4, 1e-12, "the Unbelieved: x 0.4 until vindicated");
	for (Figure& f : w.mythos.figures) if (f.id == m) f.flags |= FigureFlag::Vindicated;
	near(doctrine::fidelity(w, d.vai, par), 0.70 * 0.8 * (1.1 + 0.05), 1e-12, "vindicated: + 0.05 to every later word");
	for (Figure& f : w.mythos.figures) if (f.id == m) f.flags = FigureFlag::Discredited;
	eq(doctrine::fidelity(w, d.vai, par), 0.0, "a discredited messenger's words fail");
	eq(doctrine::fidelity(w, 99, par), 0.0, "bad culture");
}

namespace {
struct Shares { std::map<std::string, int> n; int total = 0; double F = 0;
	double of(const std::string& k) const { const auto it = n.find(k); return total > 0 && it != n.end() ? double(it->second) / total : 0.0; } };

// Speak `rolls` words at one clarity to a fresh Vai (agency a), resetting everything the readings change.
Shares roll(Clarity c, double agency, int rolls) {
	DocWorld d = docWorld();
	World& w = *d.w;
	w.mythos.faith[0].agency = agency;
	const int m = addMessenger(w, d.vai, d.towns[0], MessengerArchetype::None, 0.5);
	const Channel ch = c == Clarity::Exact ? Channel::Sky : c == Clarity::Parable ? Channel::Messenger : c == Clarity::Dream ? Channel::Dream : Channel::Sky;
	const WordOpts o = decreeWord(DecreeArea::Care, int(CareStance::Kin), c, ch, d.towns[1]);
	Shares s;
	s.F = doctrine::fidelity(w, d.vai, o);
	(void)w.bus.on<EvWord>([&](const EvWord& e) { s.n[e.outcome]++; s.total++; });
	std::vector<double> pops;
	for (const Settlement& x : w.settlements) pops.push_back(x.pop);
	for (int i = 0; i < rolls; i++) {
		w.year += 10;
		if (doctrine::speak(w, d.vai, o) < 0) { fail("refused at roll " + std::to_string(i)); break; }
		for (Figure& f : w.mythos.figures) if (f.id == m) f.credibility = 0.5;          // heard words raise it
		for (size_t k = 0; k < w.settlements.size(); k++) { w.settlements[k].pop = pops[k]; w.settlements[k].dead = false; }
		w.mythos.houses.clear(); w.mythos.rivals.clear();
		w.units.clear();
		if (w.mythos.figures.size() > 8) w.mythos.figures.resize(1);
		w.vars.rel.clear();
	}
	return s;
}
}

DM_TEST("doctrine word: outcome shares over 10,000 forked rolls within 2 % of the table") {
	const int N = 10000;
	{
		const Shares s = roll(Clarity::Exact, 0, N);
		const double F = s.F, mis = 1 - F, pc = 0.3 * 1 * (0.5 + 0);     // House authority 0
		near(F, 0.76, 1e-9, "Exact from the sky");
		near(s.of("heard"), F, 0.02, "Exact heard");
		near(s.of("literal"), mis * (1 - pc), 0.02, "Exact literal");
		near(s.of("catastrophe"), mis * pc, 0.02, "Exact catastrophe");
	}
	{
		const Shares s = roll(Clarity::Parable, 0, N);
		const double F = s.F, mis = 1 - F;
		near(F, 0.56, 1e-9);
		near(s.of("heard"), F, 0.02, "Parable heard");
		near(s.of("wisdom"), mis * 0.6, 0.02, "Parable wisdom");
		near(s.of("sect"), mis * 0.4, 0.02, "Parable sect");
		eq(s.of("catastrophe"), 0.0, "never a catastrophe from a parable");
	}
	{
		const Shares s = roll(Clarity::Dream, 0, N);
		const double F = s.F, mis = 1 - F;
		near(F, 0.44, 1e-9);
		near(s.of("heard"), F, 0.02, "Dream heard");
		near(s.of("personal"), mis * 0.5, 0.02, "Dream personal");
		near(s.of("rival"), mis * 0.4, 0.02, "Dream rival");
		near(s.of("catastrophe"), mis * 0.1, 0.02, "Dream catastrophe");
	}
	{
		const Shares s = roll(Clarity::Sign, 0, N);
		const double F = s.F, mis = 1 - F;
		near(F, 0.28, 1e-9);
		near(s.of("heard"), F, 0.02, "Sign heard");
		near(s.of("house-read"), mis * 0.5, 0.02, "Sign read by the House");
		near(s.of("rival"), mis * 0.3, 0.02, "Sign rival");
		near(s.of("catastrophe"), mis * 0.2, 0.02, "Sign catastrophe");
	}
}

DM_TEST("doctrine word: catastrophes need low agency") {
	for (Clarity c : { Clarity::Exact, Clarity::Dream, Clarity::Sign }) {
		const Shares s = roll(c, 1.0, 2000);
		eq(s.of("catastrophe"), 0.0, std::string("agency 1: no catastrophe from ") + toStr(c));
		ok(s.total == 2000, "all spoken");
	}
}

DM_TEST("doctrine word: what they heard is what is decreed") {
	DocWorld d = docWorld();
	World& w = *d.w;
	w.mythos.faith[0].agency = 0.1;
	const int m = addMessenger(w, d.vai, d.towns[0], MessengerArchetype::None, 0.0);
	std::map<int, int> seen;
	for (int i = 0; i < 600; i++) {
		w.year += 10;
		const Clarity c = Clarity(i % 4);
		const Channel ch = c == Clarity::Parable || c == Clarity::Exact ? Channel::Messenger : c == Clarity::Dream ? Channel::Dream : Channel::Sky;
		const DecreeArea area = DecreeArea::Violence;
		const int meant = c == Clarity::Sign ? int(ViolenceStance::Defence) : int(ViolenceStance::Peace);
		const auto ev = capture(w, [&] { doctrine::speak(w, d.vai, decreeWord(area, meant, c, ch, d.towns[2])); });
		for (Figure& f : w.mythos.figures) if (f.id == m) f.credibility = 0.0;
		const Utterance& u = w.mythos.words.back();
		const WordOutcome out = WordOutcome(u.outcome);
		seen[int(out)]++;
		const Decree* dc = doctrine::decree(w, d.vai, area);
		const int got = dc ? dc->stance : -1;
		switch (out) {
		case WordOutcome::Heard: eq(got, meant, "heard as meant"); break;
		case WordOutcome::Literal: ok(dc && dc->data.count("literal"), "kept by the letter"); eq(got, meant); break;
		case WordOutcome::Wisdom: eq(got, int(ViolenceStance::Defence), "the milder neighbour of Peace"); break;
		case WordOutcome::Sect: eq(got, meant, "two readings: the decree stands"); break;
		case WordOutcome::Personal: ok(dc && dc->data.count("personal") && dc->data.at("dreamer") == d.towns[2], "only the dreamer's town"); break;
		case WordOutcome::HouseRead: eq(got, int(ViolenceStance::Peace), "the House reads the stricter stance"); break;
		case WordOutcome::Catastrophe:
			if (c == Clarity::Exact) ok(dc && dc->data.count("literal"), "the letter turned on someone: still literal");
			break;
		default: break;
		}
		eq(int(u.heard), out == WordOutcome::Catastrophe && c != Clarity::Exact ? -1 : got, "the utterance records what they heard");
		eq(countEvents(ev, Ev::Word), 1, "one word event");
		w.mythos.decrees.clear();
		w.settlements[size_t(d.towns[2])].pop = 200;
	}
	for (WordOutcome o : { WordOutcome::Heard, WordOutcome::Literal, WordOutcome::Wisdom, WordOutcome::Sect, WordOutcome::Personal,
		WordOutcome::Rival, WordOutcome::HouseRead, WordOutcome::Catastrophe })
		ok(seen[int(o)] > 0, std::string("outcome seen: ") + toStr(o));
}

DM_TEST("doctrine word: a repeal removes the decree; the last 64 words are kept") {
	DocWorld d = docWorld();
	World& w = *d.w;
	w.mythos.faith[0].agency = 1;                            // no catastrophe
	setDecree(w, d.vai, DecreeArea::Wealth, int(WealthStance::Tithe));
	WordOpts rep; rep.content = WordContent::Repeal; rep.area = DecreeArea::Wealth; rep.clarity = Clarity::Sign; rep.channel = Channel::Sky;
	std::string stance, prev;
	const auto ev = capture(w, [&] { ok(doctrine::speak(w, d.vai, rep) > 0, "repeal spoken"); });
	for (const Event& e : ev) if (const EvDecree* x = e.as<EvDecree>()) { stance = x->stance; prev = x->prev; }
	ok(doctrine::decree(w, d.vai, DecreeArea::Wealth) == nullptr, "repealed");
	eq(stance, std::string("repealed")); eq(prev, std::string("tithe"));
	WordOpts ans; ans.content = WordContent::Answer; ans.clarity = Clarity::Dream; ans.channel = Channel::Dream;
	for (int i = 0; i < 100; i++) { w.year += 10; ok(doctrine::speak(w, d.vai, ans) > 0); }
	eq(int(w.mythos.words.size()), 64, "the last 64");
	for (size_t i = 1; i < w.mythos.words.size(); i++) ok(w.mythos.words[i].id > w.mythos.words[i - 1].id, "ascending ids");
	ok(w.mythos.words.back().id < w.mythos.nextId, "ids below nextId");
	noNaN(w);
}
