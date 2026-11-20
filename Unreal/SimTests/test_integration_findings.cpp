// DEMIURGE — SimTests/test_integration_findings.cpp : the v3.1 rules of design review findings 11 and 13
// (track sim-findings-2) across the real modules: the god's favour Society keeps for Diplomacy, the Omen
// of Peace calming a neighbour (Powers + Diplomacy), late peoples praying for bread only when the hunger
// lasts, the hoarding floor, and a whole balanced history that is deterministic and survives a save with
// alliances, oaths and feuds in it. Every test needs the REAL modules (run.ps1 all); with stubs each
// returns at once.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "Civ.h"
#include "Powers.h"
#include "Sim.h"
#include "Society.h"
#include "State.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

bool allReal() {
	for (const char* m : { "terrain", "climate", "hydro", "life", "path", "units", "civ", "economy", "society",
	                       "diplomacy", "powers", "disasters", "chronicle", "sim", "balance", "names", "save" })
		if (stub::stubbed(m)) return false;
	return true;
}

std::unique_ptr<World> history(uint32_t seed, int L = 5) {
	auto w = sim::create({ .seed = seed, .name = "", .L = L });
	dmt::watch(*w);
	sim::genesis::skip(*w, {});
	return w;
}

void years(World& w, double y) {
	const double until = w.year + y;
	while (w.year < until && !w.ending) sim::advance(w, 1);
}

double num(const World& w, const std::string& k) {
	const auto it = w.vars.num.find(k);
	return it == w.vars.num.end() ? 0.0 : it->second;
}

int firstLive(const World& w) {
	for (const Settlement& s : w.settlements) if (!s.dead) return s.id;
	return -1;
}

} // namespace

DM_TEST("findings 11/13: a witnessed deed of the god feeds its people's recent favour, which fades with favourHalfLife") {
	if (!allReal()) return;
	auto w = history(5);
	const int sid = firstLive(*w);
	if (!ok(sid >= 0, "a town")) return;
	const int cu = w->settlements[size_t(sid)].culture;
	const std::string g = "society_fgrace_" + std::to_string(cu), x = "society_fwrath_" + std::to_string(cu);
	society::WitnessOpts nat;
	nat.kind = "rain";
	nat.faith = 0.1;
	nat.good = 1;
	nat.natural = true;
	society::witness(*w, w->settlements[size_t(sid)].cell, 0.05, nat);
	near(num(*w, g), 0, 0, "a natural event is no deed of the god");
	society::WitnessOpts wo = nat;
	wo.natural = false;
	society::witness(*w, w->settlements[size_t(sid)].cell, 0.05, wo);
	const double g0 = num(*w, g);
	ok(g0 > 0, "grace remembered as favour");
	wo.good = -1;
	wo.kind = "lightning";
	society::witness(*w, w->settlements[size_t(sid)].cell, 0.05, wo);
	ok(num(*w, x) > 0, "wrath too");
	const double hl = w->tune<SocietyTune>().favourHalfLife;
	const double y0 = w->year;
	years(*w, hl);
	const double ratio = num(*w, g) / g0;
	std::printf("    favour after %.0f years: x %.3f (half-life %.0f)\n", w->year - y0, ratio, hl);
	near(ratio, 0.5, 0.06, "halved after one half-life (no further deeds)");
}

DM_TEST("findings 11/13: the Omen of Peace calms a neighbour on the brink of war and swears the peace") {
	if (!allReal()) return;
	auto w = history(3, 6);
	years(*w, 900);
	while (w->age < Age::IRON && w->year < 2000) years(*w, 10);   // the Omen wakes with the Iron age
	// a pair of living peoples in contact, at peace
	int a = -1, b = -1;
	for (const auto& kv : w->vars.rel) {
		const Culture* ca = state::culture(*w, kv.first.first);
		const Culture* cb = state::culture(*w, kv.first.second);
		if (ca && cb && !ca->dead && !cb->dead && kv.second.contact && !kv.second.war) { a = kv.first.first; b = kv.first.second; break; }
	}
	if (!ok(a >= 0, "two peoples at peace in contact by the Iron age")) return;
	// end every war of a so the Omen calms rather than ends one
	for (const Culture& o : w->cultures) if (o.id != a && society::atWar(*w, a, o.id)) society::makePeace(*w, a, o.id);
	const DiplomacyTune& T = w->tune<DiplomacyTune>();
	society::rel(*w, a, b)->v = T.omenTension - 0.1;
	int sid = -1;
	for (const Settlement& s : w->settlements) if (!s.dead && s.culture == a) { sid = s.id; break; }
	if (!ok(sid >= 0, "a town of a")) return;
	w->god.aweMax = 1e5;
	w->god.awe = 1e5;
	const int cell = w->settlements[size_t(sid)].cell;
	if (!ok(powers::can(*w, "peace", cell).ok, "the Omen may be cast without a war")) return;
	const double v0 = society::peek(*w, a, b)->v;
	const auto evs = capture(*w, [&] { powers::use(*w, "peace", cell); });
	const Relation* r = society::peek(*w, a, b);
	near(r->v, std::min(1.0, v0 + w->tune<PowersTune>().peaceRel), 1e-9, "relations + peaceRel");
	ok(r->truce >= w->year + T.omenCalmTruce - 1e-9, "a truce");
	ok(num(*w, "diplomacy_sworn" + std::to_string(std::min(a, b)) + "_" + std::to_string(std::max(a, b))) > w->year, "a sworn peace");
	bool felt = false;
	for (const Event& e : evs)
		if (const EvMiracleWitnessed* m = e.as<EvMiracleWitnessed>(); m && m->kind == "peace") { felt = true; near(m->good, T.omenCalmGood, 1e-12, "felt less than a war ended"); }
	ok(felt, "witnessed");
	// no war, no brink: nothing to calm
	for (const Culture& o : w->cultures) if (o.id != a) if (Relation* q = society::rel(*w, a, o.id)) { q->war = false; q->v = 0.5; }
	ok(!powers::can(*w, "peace", cell).ok, "no war and no brink: the Omen has nothing to do");
}

DM_TEST("findings 11/13: late peoples pray for bread only after two hungry years; droughts still at once") {
	if (!allReal()) return;
	auto w = history(4);
	years(*w, 20);
	const int sid = firstLive(*w);
	if (!ok(sid >= 0, "a town")) return;
	Settlement& s0 = w->settlements[size_t(sid)];
	const int cu = s0.culture;
	const SocietyTune& tn = w->tune<SocietyTune>();
	for (const auto& [age, need] : std::vector<std::pair<int, int>>{ { Age::STONE, tn.rainFamineAge[0] }, { Age::INDUSTRY, tn.rainFamineAge[4] } }) {
		w->cultures[size_t(cu)].age = age;
		for (Settlement& s : w->settlements) { s.prayer.reset(); s.devotion = 0.6; s.famine = 0; }
		w->disasters.clear();
		w->settlements[size_t(sid)].famine = 1;
		society::yearly(*w);
		const bool prays1 = w->settlements[size_t(sid)].prayer.has_value();
		w->settlements[size_t(sid)].prayer.reset();
		w->settlements[size_t(sid)].famine = 2;
		society::yearly(*w);
		const bool prays2 = w->settlements[size_t(sid)].prayer.has_value();
		eq(prays1, need <= 1, std::string(kAgeNames[age]) + ": one hungry year");
		ok(prays2, std::string(kAgeNames[age]) + ": two hungry years");
	}
}

DM_TEST("findings 11/13: hoarding never drags devotion below hoardFloor") {
	if (!allReal()) return;
	auto w = history(6);
	years(*w, 10);
	const int sid = firstLive(*w);
	if (!ok(sid >= 0, "a town")) return;
	const SocietyTune& tn = w->tune<SocietyTune>();
	if (!ok(tn.hoardFloor > 0, "balance sets a floor")) return;
	for (Culture& c : w->cultures) c.traits.piety = 0;
	w->settlements[size_t(sid)].devotion = tn.hoardFloor + 0.002;
	w->god.aweMax = 150;
	w->god.awe = 150;
	society::yearly(*w);
	ok(w->settlements[size_t(sid)].devotion >= tn.hoardFloor - 1e-12, "at or above the floor");
}

DM_TEST("findings 11/13: a balanced history with alliances, oaths and feuds is deterministic and saves whole") {
	if (!allReal()) return;
	auto run = [](double y) {
		auto w = history(2, 6);
		years(*w, y);
		return w;
	};
	auto w1 = run(1200);
	auto w2 = run(1200);
	eq(hash(*w1), hash(*w2), "same seed, same history");
	int pacts = 0, feuds = 0;
	for (const auto& kv : w1->vars.num) {
		if (kv.first.rfind("diplomacy_ally", 0) == 0) pacts++;
		if (kv.first.rfind("diplomacy_feud", 0) == 0) feuds++;
	}
	std::printf("    year %.0f: %zu cultures, %d alliances, %d feuding pairs, %d wars so far\n", w1->year, w1->cultures.size(), pacts, feuds, w1->stats.wars);
	ok(w1->cultures.size() >= 2 && w1->stats.wars >= 1, "peoples and wars by year 1,200 (L 6)");
	std::vector<uint8_t> a, b;
	std::string why;
	if (!ok(state::serialize(*w1, a, &why), why)) return;
	auto loaded = state::deserialize(a, &why);
	if (!ok(loaded != nullptr, why)) return;
	state::serialize(*loaded, b);
	ok(a == b, "re-saved identical");
	eq(loaded->vars.num.size(), w1->vars.num.size(), "every alliance / oath / feud / favour key survives");
	ok(checkFinite(*w1).empty(), "finite");
}
