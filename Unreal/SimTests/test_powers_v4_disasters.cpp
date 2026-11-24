// DEMIURGE — SimTests/test_powers_v4_disasters.cpp : Sim/Disasters.cpp, the v4 mythos hooks (SPEC-MYTHOS 3.1, 1.4;
// build plan 9 track M7 "powers-v4").
// Owner: M7 powers-v4.
//
// What is tested: the archetype's natural rates (Balanced x 1 bit for bit; Generous x 0.6; Dangerous x 1.6 with quakes
// and eruptions x 2 more and droughts x 1.3 more), Factors::naturalRate per culture (read only at MythosTune::phase
// >= 1; a culture at rate 0 is never struck; the main rng stream is the same as at phase 0 while every rate is 1), and
// the Strange archetype's storm ring (one long storm held over the open ocean, no draw while it lives, re-formed when
// it ends, not a calamity).
//
// These need the real Disasters: in `run.ps1 powers` (Disasters stubbed) they return at once; they run in `run.ps1 all`.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Civ.h"
#include "Mythos.h"
#include "Powers.h"
#include "Sim.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;
namespace pw = dm::powers;

namespace {

bool realDisasters() { return !stub::stubbed("disasters"); }

std::unique_ptr<World> natWorld(Archetype a, uint32_t seed = 5, int L = 4) {
	auto w = makeWorld({ .L = L, .seed = seed });
	w->settings.archetype = a;
	w->mythos.archetype = a;
	w->settings.calamity = 1;
	w->year = 200;
	w->age = Age::BRONZE;
	w->stats.pop = 100000;                                // past the young-world protection
	w->tune<DisastersTune>().trialYear = 0;               // no first trial (it is not a rate)
	return w;
}

struct Counts { int lightning = 0, storm = 0, drought = 0, quake = 0, eruption = 0; };
// natural() once a year for `years` years; disasters are cleared each year (only the rolls are counted)
Counts runYears(World& w, int years, std::vector<int>* lightningCells = nullptr) {
	Counts n;
	for (int y = 0; y < years; y++) {
		std::vector<Event> ev = capture(w, [&] { pw::natural(w); });
		for (const Event& e : ev) {
			if (const EvLightning* l = e.as<EvLightning>()) { if (l->natural) { n.lightning++; if (lightningCells) lightningCells->push_back(l->cell); } }
			else if (const EvStorm* s = e.as<EvStorm>()) { if (s->start) n.storm++; }
			else if (const EvDrought* d = e.as<EvDrought>()) { if (d->start) n.drought++; }
			else if (const EvQuake* q = e.as<EvQuake>()) { if (q->natural) n.quake++; }
			else if (const EvVolcanoStart* v = e.as<EvVolcanoStart>()) { if (v->natural) n.eruption++; }
		}
		w.disasters.clear();
		w.year += 1;
	}
	return n;
}

bool within(double got, double want, double rel, double abs) { return std::fabs(got - want) <= std::max(rel * want, abs); }

int town(World& w, int cell, int culture, double pop, int hops) {
	Settlement& s0 = state::newSettlement(w, cell, culture);
	s0.pop = pop;
	const int sid = s0.id;
	std::vector<int32_t> r;
	w.geo->ring(cell, hops, r);
	std::vector<int32_t> cells;
	for (int c : r)
		if (!state::isSea(w, c) && w.owner[size_t(c)] < 0) { w.owner[size_t(c)] = int16_t(sid); cells.push_back(c); }
	w.settlements[size_t(sid)].cells = cells;
	return sid;
}

const Disaster* ringOf(const World& w) {
	for (const Disaster& d : w.disasters) {
		const auto it = d.data.find("ring");
		if (d.type == DisasterType::Storm && it != d.data.end() && it->second > 0) return &d;
	}
	return nullptr;
}

} // namespace

DM_TEST("powers v4 disasters: Balanced, and neutral Factors at phase 1, draw exactly what phase 0 draws") {
	if (!realDisasters()) return;
	auto a = natWorld(Archetype::Balanced), b = natWorld(Archetype::Balanced), c = natWorld(Archetype::Balanced);
	b->tune<MythosTune>().phase = 1;
	// phase 0 ignores a culture's Factors entirely (its record is not even read)
	const int cu = state::newCulture(*c).id;
	c->mythos.factors.resize(size_t(cu) + 1);
	c->mythos.factors[size_t(cu)].naturalRate = 3;
	state::newCulture(*a);
	state::newCulture(*b);
	b->mythos.factors.resize(1);                          // Factors{}: rate 1
	const Counts na = runYears(*a, 400), nb = runYears(*b, 400), nc = runYears(*c, 400);
	eq(a->rng.s, b->rng.s, "phase 1 with neutral Factors: the same rng stream");
	eq(a->rng.s, c->rng.s, "phase 0 with a Factors record: the same rng stream");
	eq(nb.lightning + nb.storm + nb.drought + nb.quake, na.lightning + na.storm + na.drought + na.quake, "the same events");
	eq(nc.quake, na.quake, "the same quakes");
	c->mythos.factors.clear();
	b->mythos.factors.clear();
	eq(dmt::hash(*b), dmt::hash(*a), "the same world (phase 1)");
	eq(dmt::hash(*c), dmt::hash(*a), "the same world (phase 0)");
}

DM_TEST("powers v4 disasters: Generous and Dangerous scale the natural rates (3.1)") {
	if (!realDisasters()) return;
	const int years = 3000;
	auto bal = natWorld(Archetype::Balanced), gen = natWorld(Archetype::Generous), dan = natWorld(Archetype::Dangerous);
	const Counts nb = runYears(*bal, years), ng = runYears(*gen, years), nd = runYears(*dan, years);
	const DisastersTune& T = bal->tune<DisastersTune>();
	const double cent = years / 100.0;
	// Balanced: the table (the jittered schedule keeps 3,000-year counts close)
	ok(within(nb.lightning, T.lightningPerCentury * cent, 0.1, 4), "Balanced lightning " + std::to_string(nb.lightning));
	ok(within(nb.drought, T.droughtsPerCentury * cent, 0.1, 4), "Balanced droughts " + std::to_string(nb.drought));
	ok(within(nb.quake, T.quakesPerCentury * cent, 0.12, 4), "Balanced quakes " + std::to_string(nb.quake));
	const ArchetypeParams G = mythos::archetypeParams(*gen), D = mythos::archetypeParams(*dan);
	near(G.natural, 0.6, 1e-12, "Generous natural x 0.6");
	near(D.natural, 1.6, 1e-12, "Dangerous natural x 1.6");
	ok(within(ng.lightning, T.lightningPerCentury * cent * G.natural, 0.1, 4), "Generous lightning " + std::to_string(ng.lightning));
	ok(within(ng.drought, T.droughtsPerCentury * cent * G.natural, 0.12, 4), "Generous droughts " + std::to_string(ng.drought));
	ok(within(ng.quake, T.quakesPerCentury * cent * G.natural, 0.15, 4), "Generous quakes " + std::to_string(ng.quake));
	ok(within(nd.lightning, T.lightningPerCentury * cent * D.natural, 0.1, 4), "Dangerous lightning " + std::to_string(nd.lightning));
	ok(within(nd.drought, T.droughtsPerCentury * cent * D.natural * D.drought, 0.1, 4), "Dangerous droughts " + std::to_string(nd.drought));
	ok(within(nd.quake, T.quakesPerCentury * cent * D.natural * D.quakeVolcano, 0.1, 4), "Dangerous quakes " + std::to_string(nd.quake));
	ok(nd.eruption > nb.eruption, "Dangerous erupts more (" + std::to_string(nd.eruption) + " vs " + std::to_string(nb.eruption) + ")");
	ok(ng.storm < nb.storm && nb.storm < nd.storm, "storms Generous < Balanced < Dangerous");
	eq(checkFinite(*dan), std::string(), "finite");
}

DM_TEST("powers v4 disasters: a culture's Factors::naturalRate (phase 1): rate 0 is never struck, rate 2 is struck more") {
	if (!realDisasters()) return;
	auto run = [](double rate, int phase, int& inside, int& total) {
		auto w = natWorld(Archetype::Balanced, 7);
		w->tune<MythosTune>().phase = phase;
		w->tune<DisastersTune>().protectAng = 0;
		w->tune<DisastersTune>().protectAngFew = 0;
		const int cu = state::newCulture(*w).id;
		const int cell = findLand(*w, [&](int c) { return w->coast[size_t(c)] >= 4; });
		town(*w, cell, cu, 500, 6);
		w->mythos.factors.resize(size_t(cu) + 1);
		w->mythos.factors[size_t(cu)].naturalRate = rate;
		std::vector<int> cells;
		const Counts n = runYears(*w, 2500, &cells);
		inside = 0;
		for (int c : cells) if (w->owner[size_t(c)] >= 0) inside++;
		total = n.lightning;
		return w->rng.s;
	};
	int in1 = 0, t1 = 0, in0 = 0, t0 = 0, in2 = 0, t2 = 0, inP = 0, tP = 0;
	run(1, 1, in1, t1);
	run(0, 1, in0, t0);
	run(2, 1, in2, t2);
	run(0, 0, inP, tP);
	ok(in1 > 0, "rate 1: its land is struck (" + std::to_string(in1) + " of " + std::to_string(t1) + ")");
	eq(in0, 0, "rate 0: never struck");
	ok(t0 > 0, "rate 0: the rest of the world still is");
	ok(double(in2) / std::max(1, t2) > 1.3 * double(in1) / std::max(1, t1), "rate 2: a larger share of the strikes (" + std::to_string(in2) + " of " + std::to_string(t2) + ")");
	ok(inP > 0, "phase 0 ignores the rate");
}

DM_TEST("powers v4 disasters: the Strange storm ring holds over the open ocean, draws nothing, and re-forms") {
	if (!realDisasters()) return;
	auto bal = natWorld(Archetype::Balanced);
	pw::natural(*bal);
	ok(ringOf(*bal) == nullptr, "Balanced: no ring");
	ok(!mythos::archetypeParams(*bal).stormRing, "Balanced has none");

	auto w = natWorld(Archetype::Strange);
	ok(mythos::archetypeParams(*w).stormRing, "Strange has a ring");
	w->settings.calamity = 0;                             // the ring is not a calamity
	std::vector<Event> ev = capture(*w, [&] { pw::natural(*w); });
	const Disaster* r = ringOf(*w);
	if (!ok(r != nullptr, "the ring formed at calamity 0")) return;
	const int id = r->id, cell = r->cell;
	const double power = r->power, ang = r->ang;
	ok(state::isSea(*w, cell), "over the sea");
	ok(r->natural, "a natural storm");
	ok(r->dur >= 100, "a long storm");
	ok(ang > 0 && ang <= 0.25 + 1e-12, "a bounded radius");
	int startEv = 0;
	for (const Event& e : ev) if (const EvStorm* s = e.as<EvStorm>()) if (s->start && s->cell == cell) startEv++;
	eq(startEv, 1, "storm { start } once");
	// the open ocean: no land within its radius
	{
		std::vector<int32_t> cs;
		w->geo->within(cell, ang, cs);
		int land = 0;
		for (int c : cs) if (!state::isSea(*w, c)) land++;
		eq(land, 0, "no land under the ring");
	}
	// a second natural() while it lives adds nothing
	pw::natural(*w);
	int rings = 0;
	for (const Disaster& d : w->disasters) { const auto it = d.data.find("ring"); if (it != d.data.end() && it->second > 0) rings++; }
	eq(rings, 1, "one ring at a time");
	// a century of ticks: it stays, keeps its power, and the main rng stream does not move
	const uint32_t s0 = w->rng.s;
	for (int t = 0; t < 400; t++) pw::tick(*w);
	r = ringOf(*w);
	if (!ok(r != nullptr && r->id == id, "still there after 100 years")) return;
	eq(r->cell, cell, "it does not drift");
	near(r->power, power, 1e-12, "it does not weaken");
	eq(w->rng.s, s0, "no draw while it lives");
	// it ends after its span and the next year's natural() forms it again at the same place
	const double left = r->dur - r->t;
	for (int t = 0; t < int(std::ceil(left / sim::kTick)) + 2; t++) pw::tick(*w);
	ok(ringOf(*w) == nullptr, "it ended");
	pw::natural(*w);
	r = ringOf(*w);
	if (!ok(r != nullptr, "re-formed")) return;
	eq(r->cell, cell, "at the same place");
	ok(r->id != id, "a new storm");
	eq(checkFinite(*w), std::string(), "finite");
}

DM_TEST("powers v4 disasters: guardrail 2 for a real history: phase 1 changes nothing the sim keeps (Strange too)") {
	if (!realDisasters() || stub::stubbed("sim") || stub::stubbed("terrain") || stub::stubbed("society") || stub::stubbed("powers")) return;
	for (Archetype a : { Archetype::Balanced, Archetype::Strange }) {
		struct Print { uint32_t rng = 0; double year = 0, pop = 0, sea = 0, co2 = 0, awe = 0; int towns = 0, cultures = 0, units = 0, disasters = 0, wars = 0; };
		Print p[2];
		for (int phase = 0; phase < 2; phase++) {
			sim::WorldOpts wo;
			wo.seed = 4;
			wo.L = 5;
			wo.name = "";
			wo.settings.archetype = a;
			auto w = sim::create(wo);
			if (!ok(w != nullptr, "world")) return;
			sim::genesis::skip(*w, {});
			w->tune<MythosTune>().phase = phase;
			sim::use(*w);
			for (int y = 0; y < 250; y++) {
				sim::advance(*w, sim::kTicksPerYear);
				if (y % 7 == 0 && !w->settlements.empty()) {      // a god who uses the old powers, never the Word
					const Settlement& s = w->settlements[size_t(y % int(w->settlements.size()))];
					if (!s.dead) { w->god.awe = std::max(w->god.awe, 500.0); pw::use(*w, y % 2 ? "heal" : "lightning", s.cell); }
				}
			}
			Print& q = p[phase];
			q.rng = w->rng.s; q.year = w->year; q.pop = w->stats.pop; q.sea = w->seaLevel; q.co2 = w->co2; q.awe = w->god.awe;
			q.towns = int(civ::live(*w).size()); q.cultures = int(w->cultures.size()); q.units = int(w->units.size());
			q.disasters = int(w->disasters.size()); q.wars = w->stats.wars;
			if (a == Archetype::Strange) ok(ringOf(*w) != nullptr, "the Strange world keeps its ring");
		}
		const std::string tag = std::string(" (") + toStr(a) + ")";
		eq(p[1].rng, p[0].rng, "rng state" + tag);
		near(p[1].year, p[0].year, 0, "year" + tag);
		near(p[1].pop, p[0].pop, 0, "people" + tag);
		near(p[1].sea, p[0].sea, 0, "sea" + tag);
		near(p[1].co2, p[0].co2, 0, "co2" + tag);
		near(p[1].awe, p[0].awe, 0, "awe" + tag);
		eq(p[1].towns, p[0].towns, "towns" + tag);
		eq(p[1].cultures, p[0].cultures, "cultures" + tag);
		eq(p[1].units, p[0].units, "units" + tag);
		eq(p[1].disasters, p[0].disasters, "disasters" + tag);
		eq(p[1].wars, p[0].wars, "wars" + tag);
	}
}
