// DEMIURGE — SimTests/test_mythos_integration.cpp : the v4 mythos as one system (SPEC-MYTHOS 1.5, 4, 5.1, 5.3;
// track M9 "mythos-tools", 2026-11-23).
//
// Every test here needs the REAL modules: they run in `run.ps1 all` and in `Tools/simrun/simrun.ps1 --tests` (every
// real module, no stubs). In `run.ps1 mythos` (the key whose file pattern test_mythos*.cpp also picks this file up,
// with every other module stubbed) each test returns at once.
//
// What is checked: Sim's schedule lines (sim::use attaches Mythos exactly once and rebuilds its Faith records; the
// yearly group runs mythos::yearly before cards::yearly, so the submodules' proposals reach the director the same
// year; checkEnding fills ending.legacy and the `ending` event carries it); guardrail 1 (phase 0 records nothing) and
// guardrail 2 (phase 1, emergent off: a god who ignores every card leaves the v3.1 view of the world unchanged,
// through Sim's own schedule now, not a hand-called mythos::yearly); the v1 scripted gods of Tools/simrun (compiled
// into this file) play real histories without NaN and with valid saves; determinism with them acting; a save /
// load at a card opening replays identically. The long runs (8 seeds x every policy x 4,500 years, --hash 3x,
// the mythos summary table) are Tools/simrun's: simrun.ps1 --seeds 1-8 --policy myth --years 4500 --summary.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Cards.h"
#include "Characters.h"
#include "Civ.h"
#include "Doctrine.h"
#include "Mythos.h"
#include "Powers.h"
#include "Sim.h"
#include "Society.h"
#include "State.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

// The scripted gods themselves (Tools/simrun/Policies.cpp is not one of the SimTests sources: it is compiled here,
// once, so the tests drive the very players simrun uses).
#include "simrun/Policies.cpp"

using namespace dm;
using namespace dmt;

namespace {

void say(const std::string& s) { std::printf("  %s\n", s.c_str()); std::fflush(stdout); }

// True when every module a whole mythos history needs is the real one (Chronicle only tells: these tests run with it
// stubbed too, e.g. simrun.ps1 --tests --stub Chronicle while its track is mid-edit).
bool allReal() {
	for (const char* m : { "terrain", "climate", "hydro", "life", "path", "units", "civ", "economy", "society", "diplomacy",
	                       "powers", "disasters", "sim", "balance", "names", "save", "mythos", "cards", "doctrine",
	                       "characters", "legends", "faiths", "omens", "keepers", "renewal" })
		if (stub::stubbed(m)) return false;
	return true;
}

// The v3.1 view of a world (SPEC-MYTHOS 5.1 rule 2: year, tick, rng state, towns, people, sea, co2, cultures, units,
// relations): the version-1 cell fields and save bytes without the narration (chronicle, its counters, the epithet),
// year, tick, rng, units, rivers. Tools/simrun's coreHash.
uint32_t coreHash(World& w) {
	// the narration is set aside while hashing (the chronicle, its vars.num keys, the epithet, the entry ids): Chronicle
	// tells the mythos (cards opening, rulers rising) at phase >= 1, and only tells, so it is not part of the sim's view
	std::vector<ChronicleEntry> chron;
	std::swap(chron, w.chronicle);
	NumBag told;
	for (auto it = w.vars.num.begin(); it != w.vars.num.end();) {
		if (it->first.rfind("chronicle_", 0) == 0) { told.insert(*it); it = w.vars.num.erase(it); }
		else ++it;
	}
	std::string epithet;
	std::swap(epithet, w.god.epithet);
	std::vector<std::string> named(w.cultures.size());   // each people's name for the god (Chronicle's, narration only)
	for (size_t i = 0; i < w.cultures.size(); i++) std::swap(named[i], w.cultures[i].epithet);
	const int entry = w.nextId.entry;
	w.nextId.entry = 1;
	uint32_t h = 2166136261u;
	auto bytes = [&](const void* p, size_t n) {
		const uint8_t* b = static_cast<const uint8_t*>(p);
		for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
	};
	for (int i = 0; i < state::kCellFieldCount; i++) {
		if (state::fieldSince(i) > 1) continue;
		const CellArray a = state::cellArray(w, i);
		if (!a.data) continue;
		size_t esz = 4;
		switch (a.type) {
		case FieldType::F32: esz = 4; break;
		case FieldType::U8: esz = 1; break;
		case FieldType::I16: esz = 2; break;
		case FieldType::I32: esz = 4; break;
		}
		bytes(a.data, a.size * esz);
	}
	std::vector<uint8_t> b;
	if (state::serialize(w, b, nullptr, 1)) bytes(b.data(), b.size());
	const double y = w.year;
	const uint32_t t = uint32_t(w.tick), r = w.rng.s, u = uint32_t(w.units.size()), rv = uint32_t(w.rivers.size());
	bytes(&y, 8); bytes(&t, 4); bytes(&r, 4); bytes(&u, 4); bytes(&rv, 4);
	for (const River& river : w.rivers) bytes(river.cells.data(), river.cells.size() * sizeof(river.cells[0]));
	std::swap(chron, w.chronicle);
	for (const auto& kv : told) w.vars.num.insert(kv);
	std::swap(epithet, w.god.epithet);
	for (size_t i = 0; i < w.cultures.size(); i++) std::swap(named[i], w.cultures[i].epithet);
	w.nextId.entry = entry;
	return h;
}

// A history world at the given mythos phase, made the way simrun makes it: sim::create (whose sim::use saw phase 0),
// then the tuning, then the two mythos calls sim::use makes at phase >= 1 (no rederive: the world's bits stay).
std::unique_ptr<World> historyWorld(uint32_t seed, int L, int phase, bool emergent = false) {
	auto w = sim::create({ .seed = seed, .name = "", .L = L });
	if (!w) return w;
	w->tune<MythosTune>().phase = phase;
	w->tune<MythosTune>().emergent = emergent;
	mythos::attach(*w);                       // (B1) at phase 0 this drops what sim::create's use attached (Balance: phase 1)
	if (phase >= 1) mythos::rebuild(*w);
	sim::genesis::skip(*w, {});
	return w;
}

// Load bytes as a world at the given phase: the tuning is never saved, so it is set before sim::use (which then
// attaches Mythos before Chronicle and rebuilds after the rederive, SPEC-MYTHOS 1.5).
std::unique_ptr<World> loadAt(const std::vector<uint8_t>& bytes, int phase, std::string* why = nullptr) {
	auto w = state::deserialize(bytes, why);
	if (!w) return w;
	w->tune<MythosTune>().phase = phase;
	sim::use(*w);
	if (w->tune<MythosTune>().phase != phase) {   // (B1) sim::use re-applies Balance.cpp (phase 1): set the phase again
		w->tune<MythosTune>().phase = phase;
		mythos::attach(*w);
		if (phase >= 1) mythos::rebuild(*w);
	}
	return w;
}

void play(World& w, simrun::Policy& p, int years) {
	for (int i = 0; i < years * sim::kTicksPerYear && !w.ending; i++) {
		p.before(w);
		sim::advance(w, 1);
	}
}

int budgetedOpen(const World& w) {
	int n = 0;
	for (const Card& c : w.mythos.cards) {
		const auto it = c.data.find("cards_free");
		if (c.state == uint8_t(CardState::Open) && !(it != c.data.end() && it->second == 1)) n++;
	}
	return n;
}

} // namespace

// ------------------------------------------------------------------ the schedule lines (1.5)

DM_TEST("mythos integration: phase 0 keeps the pre-mythos game: no subscription, no record, no card, legacy none") {
	if (!allReal()) return;
	auto w = historyWorld(4, 5, 0);
	if (!ok(w != nullptr, "world")) return;
	const int subs = w->bus.count();
	simrun::Policy kind(simrun::PolicyKind::Kind, 4);
	play(*w, kind, 250);
	ok(state::extensionsDefault(*w), "every mythos extension stays at its default");
	eq(w->bus.count(), subs, "nobody subscribed to the bus on the way (Mythos is not attached at phase 0)");
	ok(w->mythos.cards.empty() && w->mythos.faith.empty() && w->mythos.acts.empty(), "no card, no faith record, no act");
	w->vars.ark = ArkRecord{ 0, w->cultures.empty() ? -1 : w->cultures[0].capital, w->year };
	const std::optional<Ending> e = sim::checkEnding(*w);
	if (ok(e.has_value(), "an Ark ends the world")) eq(int(e->legacy), int(Legacy::None), "no legacy at phase 0");
}

DM_TEST("mythos integration: sim::use at phase 1 attaches Mythos exactly once and rebuilds its Faith records") {
	if (!allReal()) return;
	auto w = historyWorld(6, 5, 0);
	if (!ok(w != nullptr, "world")) return;
	sim::advance(*w, 4 * 120);
	std::vector<uint8_t> bytes;
	std::string why;
	if (!ok(state::serialize(*w, bytes, &why), "serialize: " + why)) return;
	auto l0 = loadAt(bytes, 0, &why);
	auto l1 = loadAt(bytes, 1, &why);
	if (!ok(l0 && l1, "load: " + why)) return;
	const int base = l0->bus.count(), withMythos = l1->bus.count();
	ok(withMythos > base, "phase 1: Mythos's recorders are on the bus beside Chronicle's");
	sim::use(*l1);
	eq(l1->bus.count(), withMythos, "a second sim::use re-subscribes nothing twice");
	eq(l1->mythos.faith.size(), l1->cultures.size(), "rebuild made one Faith record per culture ever born");
	// a witnessed act is recorded once per witnessing settlement (one recorder, not two)
	const std::vector<int> live = civ::live(*l1);
	if (ok(!live.empty(), "a live town")) {
		const Settlement& s = l1->settlements[size_t(live[0])];
		const size_t before = l1->mythos.acts.size();
		l1->bus.emit(EvMiracleWitnessed{ s.cell, "heal", 0.8, { s.id } });
		eq(l1->mythos.acts.size(), before + 1, "one act recorded for one witness");
	}
	// lowering the phase on a live world drops the subscriptions again ((B1) sim::use re-applies Balance.cpp, which
	// turns the mythos on, so the lowered phase is attached directly, as simrun --mythos-phase 0 does it)
	l1->tune<MythosTune>().phase = 0;
	mythos::attach(*l1);
	eq(l1->bus.count(), base, "back at phase 0, attach leaves only the pre-mythos subscriptions");
}

DM_TEST("mythos integration: the yearly group runs mythos then cards: the waking cards and a budgeted card open on a real world") {
	if (!allReal()) return;
	auto w = historyWorld(2, 5, 1);
	if (!ok(w != nullptr, "world")) return;
	sim::advance(*w, 4);                       // the first yearly group
	bool shape = false, presence = false;
	for (const Card& c : w->mythos.cards) {
		shape = shape || c.kind == CardKind::ShapePeople;
		presence = presence || c.kind == CardKind::Presence;
	}
	ok(shape && presence, "the first yearly pass opened the ShapePeople and Presence cards (mythos::yearly, then the director)");
	int opened = 0;
	double first = -1;
	auto tok = w->bus.on<EvCard>([&](const EvCard& e) {
		if (e.stage != "open") return;
		const Card* c = cards::find(*w, e.id);
		const auto it = c ? c->data.find("cards_free") : NumBag::const_iterator();
		if (c && !(it != c->data.end() && it->second == 1)) { opened++; if (first < 0) first = w->year; }
	});
	int maxOpen = 0;
	for (int y = 0; y < 900; y++) { sim::advance(*w, sim::kTicksPerYear); maxOpen = std::max(maxOpen, budgetedOpen(*w)); }
	w->bus.off(tok);
	say("budgeted openings in 900 years: " + std::to_string(opened) + ", first in year " + std::to_string(int(first)) + ", at most " +
	    std::to_string(maxOpen) + " open");
	ok(opened >= 1, "a submodule's proposal reached the director in its own year (cards::yearly runs after mythos::yearly)");
	ok(first < 0 || first + 1e-9 >= w->tune<CardsTune>().firstYear, "no budgeted card before CardsTune::firstYear");
	ok(maxOpen <= w->tune<CardsTune>().maxOpen, "never more budgeted cards open than CardsTune::maxOpen");
	noNaN(*w, "900 years at phase 1");
}

DM_TEST("mythos integration: checkEnding fills ending.legacy from mythos::legacy, and the ending event carries it") {
	if (!allReal()) return;
	auto w = historyWorld(5, 5, 1);
	if (!ok(w != nullptr, "world")) return;
	simrun::Policy p(simrun::PolicyKind::Lawgiver, 5);
	play(*w, p, 400);
	if (!ok(!w->ending && !w->cultures.empty(), "a living world")) return;
	int culture = -1, town = -1;
	for (const Culture& c : w->cultures) if (!c.dead && c.capital >= 0) { culture = c.id; town = c.capital; break; }
	if (!ok(culture >= 0, "a culture with a capital")) return;
	w->vars.ark = ArkRecord{ culture, town, w->year };
	Legacy heard = Legacy::None;
	bool got = false;
	auto tok = w->bus.on<EvEnding>([&](const EvEnding& e) { heard = e.legacy; got = true; });
	const std::optional<Ending> e = sim::checkEnding(*w);
	w->bus.off(tok);
	if (!ok(e.has_value() && w->ending.has_value(), "the Ark ends the world")) return;
	ok(e->legacy != Legacy::None, std::string("a legacy is given at phase 1: ") + toStr(e->legacy));
	eq(int(w->ending->legacy), int(mythos::legacy(*w)), "ending.legacy is mythos::legacy of the ended world");
	ok(got && heard == e->legacy, "the ending event carries the same legacy");
	eq(int(w->phase), int(Phase::Ended), "the world has ended");
}

// ------------------------------------------------------------------ the guardrails (5.1)

DM_TEST("mythos integration: phase 1, emergent off: passive and kind (cards lapse) keep the v3.1 view through Sim's schedule") {
	if (!allReal()) return;
	for (simrun::PolicyKind pk : { simrun::PolicyKind::Passive, simrun::PolicyKind::Kind }) {
		auto a = historyWorld(3, 5, 0);
		auto b = historyWorld(3, 5, 1);
		if (!ok(a && b, "worlds")) return;
		simrun::Policy pa(pk, 3), pb(pk, 3);
		play(*a, pa, 500);
		play(*b, pb, 500);
		const std::string nm = simrun::toStr(pk);
		ok(!b->mythos.faith.empty() && !b->mythos.cards.empty(), nm + ": phase 1 keeps faith records and opens (and lapses) cards");
		for (const Culture& c : b->cultures) ok(mythos::factors(*b, c.id) == Factors{}, nm + ": neutral Factors for culture " + std::to_string(c.id));
		eq(coreHash(*b), coreHash(*a), nm + ": the v3.1 view (year, tick, rng, towns, people, sea, co2, cultures, units, relations) is identical");
	}
}

// ------------------------------------------------------------------ the scripted gods (5.3)

DM_TEST("mythos integration: every v1 scripted god plays 700 years on an L5 world without NaN, its world saves and loads") {
	if (!allReal()) return;
	for (int k = simrun::kFirstMythPolicy; k < int(simrun::PolicyKind::Count); k++) {
		const simrun::PolicyKind pk = simrun::PolicyKind(k);
		const std::string nm = simrun::toStr(pk);
		auto w = historyWorld(uint32_t(10 + k), 5, 1);
		if (!ok(w != nullptr, nm + ": world")) return;
		dmt::watch(*w);
		simrun::Policy p(pk, uint32_t(10 + k));
		play(*w, p, 700);
		noNaN(*w, nm + ": 700 years");
		ok(sim::firstNonFinite(*w).empty(), nm + ": no non-finite value");
		std::vector<uint8_t> bytes;
		std::string why;
		ok(state::serialize(*w, bytes, &why), nm + ": the world serializes: " + why);
		auto back = state::deserialize(bytes, &why);
		ok(back != nullptr, nm + ": and loads (the MYTH block validates): " + why);
		const simrun::PolicyStats& st = p.stats();
		say(nm + ": cards answered " + std::to_string(st.cardsAnswered) + " (refused " + std::to_string(st.cardsRefused) + "), words " +
		    std::to_string(st.words) + ", messengers " + std::to_string(st.messengers) + ", decrees held " + std::to_string(w->mythos.decrees.size()) +
		    ", cards kept " + std::to_string(w->mythos.cards.size()) + ", save " + std::to_string(bytes.size()) + " bytes");
		if (pk == simrun::PolicyKind::ResponderMyth || pk == simrun::PolicyKind::RandomMyth)
			ok(st.cardsAnswered >= 2, nm + ": answers the waking cards at least");
		if (pk == simrun::PolicyKind::Lawgiver || pk == simrun::PolicyKind::ExactOnly || pk == simrun::PolicyKind::SignOnly ||
		    pk == simrun::PolicyKind::JudgeDeeds || pk == simrun::PolicyKind::Valour)
			ok(st.words >= 1, nm + ": speaks through the speak power");
		if (pk == simrun::PolicyKind::UnseenKind) eq(int(w->mythos.presence), int(Presence::Unseen), nm + ": chose Unseen at the waking");
		if (pk == simrun::PolicyKind::MsgKind) eq(int(w->mythos.presence), int(Presence::Messengers), nm + ": chose Messengers at the waking");
	}
}

DM_TEST("mythos integration: determinism with mythos gods acting: two worlds advanced alternately stay identical") {
	if (!allReal()) return;
	for (simrun::PolicyKind pk : { simrun::PolicyKind::ResponderMyth, simrun::PolicyKind::RandomMyth }) {
		auto a = historyWorld(8, 5, 1);
		auto b = historyWorld(8, 5, 1);
		if (!ok(a && b, "worlds")) return;
		simrun::Policy pa(pk, 8), pb(pk, 8);
		for (int i = 0; i < 500 * sim::kTicksPerYear; i++) {
			pa.before(*a); sim::advance(*a, 1);
			pb.before(*b); sim::advance(*b, 1);
		}
		const std::string nm = simrun::toStr(pk);
		eq(sim::fingerprint(*a), sim::fingerprint(*b), nm + ": same fingerprint after 500 years");
		eq(a->mythos.cards.size(), b->mythos.cards.size(), nm + ": same cards");
		eq(pa.stats().cardsAnswered, pb.stats().cardsAnswered, nm + ": same answers");
	}
}

DM_TEST("mythos integration: a save / load at a card opening replays identically") {
	if (!allReal()) return;
	auto w = historyWorld(7, 5, 1);
	if (!ok(w != nullptr, "world")) return;
	simrun::Policy p(simrun::PolicyKind::ResponderMyth, 7);
	bool opened = false;
	auto tok = w->bus.on<EvCard>([&](const EvCard& e) {
		const Card* c = cards::find(*w, e.id);
		const auto it = c ? c->data.find("cards_free") : NumBag::const_iterator();
		if (e.stage == "open" && c && !(it != c->data.end() && it->second == 1)) opened = true;
	});
	for (int i = 0; i < 1500 * sim::kTicksPerYear && !opened; i++) {
		sim::advance(*w, 1);                   // the card opens inside the yearly group: save right after it,
	}                                          // before any player answers it
	w->bus.off(tok);
	if (!ok(opened, "a budgeted card opened within 1,500 years")) return;
	const std::vector<int> openIds = cards::openCards(*w);
	std::vector<uint8_t> bytes, again;
	std::string why;
	if (!ok(state::serialize(*w, bytes, &why), "serialize: " + why)) return;
	auto x = loadAt(bytes, 1, &why);
	auto y = loadAt(bytes, 1, &why);
	if (!ok(x && y, "load: " + why)) return;
	state::serialize(*state::deserialize(bytes), again);
	ok(again == bytes, "re-saving the loaded world reproduces the bytes exactly");
	eq(cards::openCards(*x), openIds, "the open cards survive the save");
	for (int id : openIds) {
		const Card* a = cards::find(*w, id);
		const Card* b = cards::find(*x, id);
		if (!ok(a && b, "card " + std::to_string(id))) continue;
		ok(a->kind == b->kind && a->deadline == b->deadline && a->opened == b->opened && a->ref == b->ref && a->data == b->data,
		   std::string("card ") + std::to_string(id) + " (" + toStr(a->kind) + ") is the same card after the load");
	}
	eq(x->mythos.num, w->mythos.num, "the director's counters survive the save");
	eq(x->mythos.nextId, w->mythos.nextId, "the next id survives the save");
	simrun::Policy px(simrun::PolicyKind::ResponderMyth, 7), py(simrun::PolicyKind::ResponderMyth, 7);
	for (int i = 0; i < 400 * sim::kTicksPerYear; i++) {
		px.before(*x); sim::advance(*x, 1);
		py.before(*y); sim::advance(*y, 1);
	}
	eq(sim::fingerprint(*x), sim::fingerprint(*y), "two loads of the card-opening save continue identically, cards answered");
	ok(px.stats().cardsAnswered >= int(openIds.size()), "the card open at the save was answered after the load");
	noNaN(*x, "400 years after the load");
}

// ------------------------------------------------------------------ cost (5.1 rule 5, reported)

DM_TEST("mythos integration: cost of mythos::yearly and cards::yearly on a real L6 world with a mythos god (reported)") {
	if (!allReal()) return;
	auto w = historyWorld(1, 6, 1);
	if (!ok(w != nullptr, "world")) return;
	simrun::Policy p(simrun::PolicyKind::Lawgiver, 1);
	play(*w, p, 1200);
	std::vector<uint8_t> bytes;
	std::string why;
	if (!ok(state::serialize(*w, bytes, &why), "serialize: " + why)) return;
	// time the two passes on a loaded copy (an extra pass a year there; the measured world is thrown away)
	auto c = loadAt(bytes, 1, &why);
	if (!ok(c != nullptr, "load: " + why)) return;
	double sum = 0, worst = 0;
	int n = 0;
	for (int y = 0; y < 120; y++) {
		sim::advance(*c, sim::kTicksPerYear);
		const double t0 = nowMs();
		mythos::yearly(*c);
		cards::yearly(*c);
		const double dt = nowMs() - t0;
		sum += dt;
		worst = std::max(worst, dt);
		n++;
	}
	int live = 0;
	for (const Settlement& s : c->settlements) live += s.dead ? 0 : 1;
	char buf[256];
	std::snprintf(buf, sizeof buf, "mythos::yearly + cards::yearly at year %.0f (%d towns, %zu cultures, %zu figures, %zu cards): mean %.3f ms, worst %.3f ms",
		c->year, live, c->cultures.size(), c->mythos.figures.size(), c->mythos.cards.size(), sum / std::max(1, n), worst);
	say(buf);
	ok(sum / std::max(1, n) <= 3.0, "the mythos passes stay far below a tick's budget (target 0.35 ms mean, SPEC-MYTHOS 1.10)");
	noNaN(*c, "after the timed passes");
}
