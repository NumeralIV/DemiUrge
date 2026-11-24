// DEMIURGE — SimTests/test_characters_rulers.cpp : rulers (SPEC-MYTHOS 3.23), the registry cap, names, save and
// determinism (track M4). Key: run.ps1 characters, and run.ps1 all.
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

#include <map>
#include <set>
#include <string>
#include <vector>

using namespace dm;
using namespace dmt;

namespace {

int freeLandCell(const World& w, const std::vector<int>& taken) {
	return findLand(w, [&](int c) {
		for (int t : taken) if (t == c) return false;
		return true;
	});
}

// n cultures, each with a capital city (and a village for the first), at age `age`.
std::unique_ptr<World> realm(int n, int age = Age::BRONZE, uint32_t seed = 1) {
	auto w = makeWorld({ .L = 3, .seed = seed });
	w->tune<MythosTune>().phase = 1;
	w->year = 500;
	std::vector<int> cells;
	for (int i = 0; i < n; i++) {
		Culture& c = state::newCulture(*w);
		c.age = age; c.born = 0;
		const int cell = freeLandCell(*w, cells);
		cells.push_back(cell);
		Settlement& s = state::newSettlement(*w, cell, i);
		s.capital = true; s.tier = 3; s.pop = 4000;
		w->cultures[size_t(i)].capital = s.id;
		w->owner[size_t(cell)] = int16_t(s.id);
	}
	return w;
}

int livingRulers(const World& w, int cid) {
	int k = 0;
	for (const Figure& f : w.mythos.figures)
		if (f.culture == cid && f.kind == uint8_t(FigureKind::Ruler) && f.fate == uint8_t(FigureFate::Living)) k++;
	return k;
}

} // namespace

DM_TEST("characters rulers: exactly one living ruler per Bronze+ culture with a capital, every year; reigns 15-45") {
	stub::Guard g;
	auto w = realm(4);
	w->cultures[3].age = Age::STONE;                         // a Stone people has no ruler
	std::map<int, double> reigns;                              // ruler id -> first year
	int checked = 0;
	for (int y = 0; y < 800; y++) {
		w->year = 500 + y;
		characters::yearly(*w);
		for (int c = 0; c < 3; c++) {
			const int r = characters::ruler(*w, c);
			if (!ok(livingRulers(*w, c) == 1 && r >= 0, "one ruler, culture " + std::to_string(c) + " year " + std::to_string(w->year))) return;
			const Figure* f = characters::figure(*w, r);
			ok(!f->name.empty() && !f->byname.empty(), "a ruler has a name");
			eq(f->home, w->cultures[size_t(c)].capital, "rules from the capital");
			if (!reigns.count(r)) reigns[r] = w->year;
			checked++;
		}
		eq(livingRulers(*w, 3), 0, "no ruler in the Stone age");
	}
	ok(checked == 2400, "checked every year");
	int ended = 0;
	for (const Figure& f : w->mythos.figures) {
		if (f.kind != uint8_t(FigureKind::Ruler)) continue;
		const double from = f.data.at("from"), until = f.data.at("until");
		ok(until - from >= 15 - 1e-9 && until - from <= 45 + 1e-9, "reign length in 15..45: " + std::to_string(until - from));
		if (f.fate != uint8_t(FigureFate::Living)) {
			ended++;
			ok(f.died >= until - 1e-9 && f.died < until + 1, "a reign ends at its term");
		}
	}
	ok(ended >= 40, "many reigns ended: " + std::to_string(ended));
	ok(int(w->mythos.figures.size()) <= w->tune<CharactersTune>().maxFigures, "within the cap");
	noNaN(*w, "after 800 years of rulers");
}

DM_TEST("characters rulers: heirs about 75 % of natural successions; a new house otherwise") {
	stub::Guard g;
	auto w = realm(8, Age::IRON, 2);
	std::map<int, int> last;                                   // culture -> last ruler id
	int heirs = 0, houses = 0;
	for (int y = 0; y < 1500; y++) {
		w->year = 500 + y;
		characters::yearly(*w);
		for (int c = 0; c < 8; c++) {
			const int r = characters::ruler(*w, c);
			auto it = last.find(c);
			if (it != last.end() && it->second != r) {
				const Figure* prev = characters::figure(*w, it->second);
				const Figure* now = characters::figure(*w, r);
				if (prev && now) {
					if (prev->data.at("house") == now->data.at("house")) heirs++;
					else houses++;
				}
			}
			last[c] = r;
		}
	}
	const int n = heirs + houses;
	ok(n > 250, "successions: " + std::to_string(n));
	const double share = double(heirs) / std::max(1, n);
	ok(share > 0.67 && share < 0.83, "heir share " + std::to_string(share));
}

DM_TEST("characters rulers: a removed ruler is succeeded the same year by a new house; losing the capital ends the reign") {
	stub::Guard g;
	auto w = realm(2, Age::BRONZE, 3);
	characters::yearly(*w);
	const int r = characters::ruler(*w, 0);
	if (!ok(r >= 0, "a ruler")) return;
	const double house = characters::figure(*w, r)->data.at("house");
	std::vector<Event> ev = capture(*w, [&] { ok(characters::fell(*w, r, FigureFate::Fallen), "removed"); });
	const int r2 = characters::ruler(*w, 0);
	ok(r2 >= 0 && r2 != r, "succeeded at once");
	eq(characters::figure(*w, r)->died, w->year, "the removed ruler died this year");
	eq(int(characters::figure(*w, r)->fate), int(FigureFate::Fallen), "fate Fallen");
	ok(characters::figure(*w, r2)->data.at("house") != house, "a new house");
	eq(characters::figure(*w, r2)->born < w->year, true, "born before the reign");
	int fallen = 0, rise = 0;
	for (const Event& e : ev) if (const EvFigure* f = e.as<EvFigure>()) { fallen += f->what == "fallen"; rise += f->what == "rise"; }
	ok(fallen == 1 && rise == 1, "figure fallen, then rise");
	ok(!characters::fell(*w, r, FigureFate::Fallen), "a dead ruler cannot fall twice");
	ok(!characters::fell(*w, r2, FigureFate::Living), "Living is not an end");
	ok(!characters::fell(*w, 9999, FigureFate::Died), "bad id");
	// the capital moves (the old one was taken): the reign ends, a new house rules from the new capital
	const int cell = freeLandCell(*w, { w->settlements[0].cell, w->settlements[1].cell });
	Settlement& n = state::newSettlement(*w, cell, 0); n.tier = 2; n.pop = 900;
	w->settlements[0].culture = 1; w->settlements[0].capital = false;
	n.capital = true; w->cultures[0].capital = n.id;
	w->year += 1;
	characters::yearly(*w);
	const int r3 = characters::ruler(*w, 0);
	ok(r3 >= 0 && r3 != r2, "a new ruler");
	eq(characters::figure(*w, r3)->home, n.id, "at the new capital");
	ok(characters::figure(*w, r3)->data.at("house") != characters::figure(*w, r2)->data.at("house"), "a new house when the capital was taken");
	// no capital: no ruler
	w->cultures[0].capital = -1;
	w->year += 1;
	characters::yearly(*w);
	eq(livingRulers(*w, 0), 0, "no capital, no ruler");
	// a dead culture: no ruler
	w->cultures[1].dead = true;
	w->year += 1;
	characters::yearly(*w);
	eq(livingRulers(*w, 1), 0, "a dead people has no ruler");
}

DM_TEST("characters registry: at most 160 figures, ids unique, names never empty, references kept, save round trip") {
	stub::Guard g;
	auto w = realm(10, Age::IRON, 4);
	w->tune<CharactersTune>().reignMin = 1;
	w->tune<CharactersTune>().reignMax = 2;
	characters::yearly(*w);
	const int early = characters::ruler(*w, 0);
	// a closed card still names that ruler: it must survive pruning
	Card c; c.id = w->mythos.nextId++; c.kind = CardKind::Judgement; c.state = uint8_t(CardState::Lapsed); c.culture = 0; c.figure = early;
	w->mythos.cards.push_back(c);
	int created = 0;
	for (int y = 1; y < 600; y++) {
		w->year = 500 + y;
		std::vector<Event> ev = capture(*w, [&] { characters::yearly(*w); });
		for (const Event& e : ev) if (const EvFigure* f = e.as<EvFigure>()) created += f->what == "rise";
		if (!ok(int(w->mythos.figures.size()) <= 160, "cap at year " + std::to_string(w->year))) return;
	}
	ok(created > 1500, "thousands of reigns came and went: " + std::to_string(created));
	ok(characters::figure(*w, early) != nullptr, "a referenced dead figure is kept");
	std::set<int> ids;
	int prev = 0;
	for (const Figure& f : w->mythos.figures) {
		ok(!f.name.empty(), "name");
		ok(f.id > prev, "ascending ids");
		prev = f.id;
		ids.insert(f.id);
		ok(f.id < w->mythos.nextId, "below nextId");
	}
	for (int cid = 0; cid < 10; cid++) eq(livingRulers(*w, cid), 1, "still one ruler each");
	std::vector<uint8_t> bytes;
	std::string why;
	if (!ok(state::serialize(*w, bytes, &why), "serialize: " + why)) return;
	auto back = state::deserialize(bytes, &why);
	if (!ok(back != nullptr, "deserialize: " + why)) return;
	eq(int(back->mythos.figures.size()), int(w->mythos.figures.size()), "figures survive the save");
	std::vector<uint8_t> again;
	ok(state::serialize(*back, again, &why), "re-serialize");
	ok(again == bytes, "re-saves byte for byte");
	bool same = true;
	for (size_t i = 0; i < w->mythos.figures.size(); i++) {
		const Figure& x = w->mythos.figures[i]; const Figure& y = back->mythos.figures[i];
		same = same && x.id == y.id && x.name == y.name && x.byname == y.byname && x.data == y.data && x.heart == y.heart && x.fate == y.fate;
	}
	ok(same, "every figure survives the save");
	// the save-size bound of 1.3 (the whole MYTH block under 80 KB): a full registry is a fraction of it
	const size_t full = bytes.size();
	w->mythos.figures.clear();
	w->mythos.cards.clear();
	std::vector<uint8_t> none;
	state::serialize(*w, none);
	const size_t figBytes = full - none.size();
	ok(figBytes < 32 * 1024, "160 figures take " + std::to_string(figBytes) + " bytes");
	if (verbose()) std::printf("    160 figures: %zu bytes\n", figBytes);
	noNaN(*w, "registry");
}

DM_TEST("characters: yearly never draws from w.rng and two identical worlds stay identical") {
	stub::Guard g;
	auto a = realm(5, Age::IRON, 5);
	auto b = realm(5, Age::IRON, 5);
	const uint32_t s0 = a->rng.s;
	for (int y = 0; y < 300; y++) {
		a->year = b->year = 500 + y;
		characters::yearly(*a);
		characters::yearly(*b);
	}
	eq(a->rng.s, s0, "w.rng untouched (forks only)");
	eq(hash(*a), hash(*b), "deterministic");
	eq(int(a->mythos.figures.size()), int(b->mythos.figures.size()), "same registry");
}

DM_TEST("characters: spawn fills names and ids, refuses bad cultures; reads tolerate bad ids; no NaN") {
	stub::Guard g;
	auto w = realm(1, Age::IRON, 6);
	Figure p; p.kind = uint8_t(FigureKind::RivalVoice); p.culture = 0; p.home = 0; p.person = { std::nanf(""), 2.0f, -1.0f, 0.3f };
	p.credibility = std::nan("");
	std::vector<Event> ev;
	int id = -1;
	ev = capture(*w, [&] { id = characters::spawn(*w, p); });
	if (!ok(id >= 1, "spawned")) return;
	const Figure* f = characters::figure(*w, id);
	ok(!f->name.empty(), "a name from Names");
	eq(int(f->fate), int(FigureFate::Living), "living");
	ok(f->person[0] == 0.5f && f->person[1] == 1.0f && f->person[2] == 0.0f, "person sanitised");
	eq(f->credibility, 0.0, "NaN credibility sanitised");
	int rise = 0;
	for (const Event& e : ev) if (const EvFigure* x = e.as<EvFigure>()) rise += x->what == "rise";
	eq(rise, 1, "figure rise");
	Figure bad = p; bad.culture = 7;
	eq(characters::spawn(*w, bad), -1, "bad culture");
	bad.culture = -1;
	eq(characters::spawn(*w, bad), -1, "no culture");
	bad = p; bad.home = 99;
	eq(characters::spawn(*w, bad), -1, "bad home");
	eq(characters::ruler(*w, -1), -1, "ruler of nobody");
	eq(characters::messenger(*w, 42), -1, "messenger of nobody");
	ok(characters::figure(*w, -5) == nullptr, "figure of nobody");
	eq(characters::credibility(*w, 12345), 0.5, "neutral credibility for a bad id");
	ok(!characters::test(*w, id, TrialKind::Power), "trials are v2");
	eq(characters::trialOdds(*w, id, TrialKind::Power), 0.0, "no odds yet");
	// an empty world
	auto e = makeWorld({ .L = 2, .seed = 7 });
	e->tune<MythosTune>().phase = 1;
	characters::yearly(*e);
	ok(e->mythos.figures.empty(), "nothing in a world without people");
	noNaN(*w, "spawn");
}

DM_TEST("characters guardrail 2: at phase 1 a god who ignores every card leaves the sim as it was (real history)") {
	if (stub::stubbed("sim") || stub::stubbed("terrain") || stub::stubbed("society") || stub::stubbed("civ")) return;
	auto a = sim::create({ .seed = 4, .name = "", .L = 5 });
	auto b = sim::create({ .seed = 4, .name = "", .L = 5 });
	if (!ok(a && b, "worlds")) return;
	for (World* x : { a.get(), b.get() }) { x->tune<MythosTune>().phase = 1; sim::genesis::skip(*x, {}); }
	int rulers = 0;
	for (int y = 0; y < 500; y++) {
		sim::advance(*a, sim::kTicksPerYear);
		sim::advance(*b, sim::kTicksPerYear);
		characters::yearly(*a);                  // the extra pass (Mythos will call it; here by hand)
		for (const Culture& c : a->cultures) rulers += characters::ruler(*a, c.id) >= 0;
	}
	eq(a->rng.s, b->rng.s, "the rng stream is untouched");
	eq(a->year, b->year, "year");
	eq(int(a->settlements.size()), int(b->settlements.size()), "settlements");
	eq(int(a->cultures.size()), int(b->cultures.size()), "cultures");
	eq(int(a->units.size()), int(b->units.size()), "units");
	bool same = true;
	for (size_t i = 0; i < a->settlements.size() && i < b->settlements.size(); i++)
		same = same && a->settlements[i].pop == b->settlements[i].pop && a->settlements[i].devotion == b->settlements[i].devotion
			&& a->settlements[i].love == b->settlements[i].love && a->settlements[i].dead == b->settlements[i].dead;
	for (size_t i = 0; i < a->cultures.size() && i < b->cultures.size(); i++)
		same = same && a->cultures[i].prophet == b->cultures[i].prophet && a->cultures[i].traits == b->cultures[i].traits;
	ok(same, "every town and people identical");
	ok(a->vars.rel == b->vars.rel, "relations identical");
	ok(a->god.awe == b->god.awe, "Awe identical");
	ok(rulers > 0 || a->age < Age::BRONZE, "rulers reigned once the Bronze age came (" + std::to_string(rulers) + " culture-years)");
	ok(int(a->mythos.figures.size()) <= 160, "cap");
	noNaN(*a, "phase 1 with characters");
}
