// DEMIURGE — SimTests/test_doctrine_fixture.h : a small synthetic world for the doctrine tests (track M3).
// Two living cultures (Vai = 0 with three towns, Oru = 1 with one), Faith records at their defaults, phase 1,
// plenty of Awe, history year 300. Everything is set directly (FOUNDATION-CPP 6.4): no other module acts.
#pragma once

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

namespace dmt::doc {

struct DocWorld {
	std::unique_ptr<dm::World> w;
	int vai = 0, oru = 1;
	std::vector<int> towns;          // Vai's towns: [0] the capital
	int oruTown = -1;
};

inline int farLand(const dm::World& w, const std::vector<int>& taken, double minAng) {
	return findLand(w, [&](int c) {
		for (int t : taken) if (w.geo->angle(c, t) < minAng) return false;
		return true;
	});
}

inline DocWorld docWorld(int phase = 1, bool emergent = false, uint32_t seed = 7) {
	DocWorld d;
	d.w = makeWorld({ .L = 4, .seed = seed });
	dm::World& w = *d.w;
	w.tune<dm::MythosTune>().phase = phase;
	w.tune<dm::MythosTune>().emergent = emergent;
	w.year = 300;
	w.god.awe = 5000;
	w.god.aweMax = 5000;
	for (int i = 0; i < 2; i++) {
		dm::Culture& c = dm::state::newCulture(w);
		c.name = i == 0 ? "Vai" : "Oru";
		c.born = 0;
	}
	std::vector<int> cells;
	for (int i = 0; i < 4; i++) {
		const int cell = farLand(w, cells, 0.35);
		cells.push_back(cell);
		dm::Settlement& s = dm::state::newSettlement(w, cell, i < 3 ? 0 : 1);
		s.pop = i == 0 ? 400 : 200;
		s.tier = i == 0 ? 3 : 1;
		s.capital = i == 0 || i == 3;
		s.name = "Town" + std::to_string(i);
		w.owner[size_t(cell)] = int16_t(s.id);
		if (i < 3) d.towns.push_back(s.id); else d.oruTown = s.id;
	}
	w.cultures[0].capital = d.towns[0];
	w.cultures[1].capital = d.oruTown;
	w.mythos.faith.assign(w.cultures.size(), dm::Faith{});
	w.mythos.factors.assign(w.cultures.size(), dm::Factors{});
	return d;
}

// A living messenger of culture c (archetype a), credibility cred. The figure id.
inline int addMessenger(dm::World& w, int c, int home, dm::MessengerArchetype a = dm::MessengerArchetype::None, double cred = 0.5) {
	dm::Figure f;
	f.id = w.mythos.nextId++;
	f.kind = uint8_t(dm::FigureKind::Messenger);
	f.archetype = uint8_t(a);
	f.culture = c; f.home = home;
	f.name = "Ossa"; f.born = w.year; f.credibility = cred;
	w.mythos.figures.push_back(f);
	return f.id;
}
inline int addRuler(dm::World& w, int c, int home) {
	dm::Figure f;
	f.id = w.mythos.nextId++;
	f.kind = uint8_t(dm::FigureKind::Ruler);
	f.culture = c; f.home = home; f.name = "Teodo"; f.born = w.year;
	w.mythos.figures.push_back(f);
	return f.id;
}

// A god-given decree set directly (kept, fidelity 1).
inline dm::Decree& setDecree(dm::World& w, int c, dm::DecreeArea a, int stance, double kept = 0.8, bool custom = false) {
	for (dm::Decree& d : w.mythos.decrees)
		if (d.culture == c && d.area == a) { d.stance = int8_t(stance); d.kept = kept; d.custom = custom; return d; }
	dm::Decree d;
	d.culture = c; d.area = a; d.stance = int8_t(stance); d.kept = kept; d.custom = custom; d.since = w.year;
	d.data["fidelity"] = 1;
	w.mythos.decrees.push_back(d);
	std::sort(w.mythos.decrees.begin(), w.mythos.decrees.end(), [](const dm::Decree& x, const dm::Decree& y) {
		return x.culture != y.culture ? x.culture < y.culture : uint8_t(x.area) < uint8_t(y.area);
	});
	for (dm::Decree& e : w.mythos.decrees) if (e.culture == c && e.area == a) return e;
	return w.mythos.decrees.back();
}

// a and b at war, `aggressor` started it; in contact.
inline void setWar(dm::World& w, int a, int b, int aggressor) {
	dm::Relation& r = w.vars.rel[dm::state::relKey(a, b)];
	r.war = true; r.contact = true; r.since = w.year; r.aggressor = aggressor; r.v = -0.6;
}

// An open card pushed straight into the records (Cards' honest find sees it).
inline int pushCard(dm::World& w, dm::CardKind kind, int culture, int settlement = -1, std::array<int, 4> ref = { -1, -1, -1, -1 },
	int figure = -1, int other = -1, dm::CardState st = dm::CardState::Open, double opened = -1) {
	dm::Card k;
	k.id = w.mythos.nextId++;
	k.kind = kind; k.culture = culture; k.settlement = settlement; k.ref = ref; k.figure = figure; k.other = other;
	k.state = uint8_t(st);
	k.opened = opened < 0 ? w.year : opened;
	if (settlement >= 0) k.cell = w.settlements[size_t(settlement)].cell;
	w.mythos.cards.push_back(k);
	return k.id;
}

inline int countEvents(const std::vector<dm::Event>& ev, dm::Ev type) {
	int n = 0;
	for (const dm::Event& e : ev) if (e.type == type) n++;
	return n;
}

// Captures cards::propose while Cards is stubbed (returns false when Cards is real: hook-free tests only then).
struct ProposalLog {
	std::vector<dm::cards::Proposal> got;
	bool install() {
		if (!stub::stubbed("cards")) return false;
		stub::hooks().cards_propose = [this](dm::World&, const dm::cards::Proposal& p) { got.push_back(p); return true; };
		return true;
	}
	int count(dm::CardKind k) const { int n = 0; for (const auto& p : got) if (p.kind == k) n++; return n; }
	const dm::cards::Proposal* last(dm::CardKind k) const {
		for (auto it = got.rbegin(); it != got.rend(); ++it) if (it->kind == k) return &*it;
		return nullptr;
	}
};

} // namespace dmt::doc
