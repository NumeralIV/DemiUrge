// DEMIURGE — SimTests/test_integration_findings2.cpp : the sim-findings-2 verifier's checks of the v3.1 rules
// (design review findings 11 and 13) across the real modules: a save made at the very tick a war is
// declared, and at the tick the world enters a new age, keeps every relation and diplomacy number exactly
// and replays deterministically from the loaded bytes. Every test needs the REAL modules
// (run.ps1 all); with stubs each returns at once.
#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Save.h"
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

std::unique_ptr<World> history(uint32_t seed, int L = 6) {
	auto w = sim::create({ .seed = seed, .name = "", .L = L });
	dmt::watch(*w);
	sim::genesis::skip(*w, {});
	return w;
}

// Advance tick by tick until `hit` says so (true) or `maxYear`.
bool until(World& w, double maxYear, const std::function<bool(const std::vector<Event>&)>& hit) {
	while (w.year < maxYear && !w.ending) {
		const auto evs = capture(w, [&] { sim::advance(w, 1); });
		if (hit(evs)) return true;
	}
	return false;
}

// Save w now; the loaded world must hold the same relations and diplomacy / society numbers, and two
// worlds loaded from the same bytes must replay the next `more` years identically.
void saveHere(World& w, const std::string& what, double more) {
	std::vector<uint8_t> bytes;
	std::string why;
	if (!ok(state::serialize(w, bytes, &why), what + ": saved " + why)) return;
	auto l1 = state::deserialize(bytes, &why);
	auto l2 = state::deserialize(bytes, &why);
	if (!ok(l1 && l2, what + ": loaded " + why)) return;
	ok(l1->vars.rel == w.vars.rel, what + ": every relation (war, since, truce, aggressor, v, trade) survives exactly");
	ok(l1->vars.num == w.vars.num, what + ": every number (war tallies, alliances, oaths, feuds, favour) survives exactly");
	int wars = 0;
	for (const auto& kv : w.vars.rel) wars += kv.second.war ? 1 : 0;
	sim::use(*l1);
	sim::use(*l2);
	const double until1 = l1->year + more;
	while (l1->year < until1 && !l1->ending) sim::advance(*l1, 1);
	while (l2->year < until1 && !l2->ending) sim::advance(*l2, 1);
	eq(hash(*l1), hash(*l2), what + ": two worlds loaded from the same save replay identically");
	ok(checkFinite(*l1).empty(), what + ": finite after the replay");
	std::printf("    %s at year %.2f: %zu bytes, %d wars open; replayed %.0f years, %d wars declared in all\n", what.c_str(), w.year, bytes.size(), wars,
		more, l1->stats.wars);
}

} // namespace

DM_TEST("findings 11/13 verify: a save at the tick a war is declared, and at an age change, keeps diplomacy whole") {
	if (!allReal()) return;
	for (const uint32_t seed : { 303u, 306u }) {
		auto w = history(seed);
		const std::string s = "seed " + std::to_string(seed);
		const bool war = until(*w, 2500, [](const std::vector<Event>& evs) {
			for (const Event& e : evs) if (e.as<EvWarDeclared>()) return true;
			return false;
		});
		if (!ok(war, s + ": a war by year 2,500")) continue;
		saveHere(*w, s + " war:declared", 300);
		const int age0 = w->age;
		const bool aged = until(*w, 4000, [&](const std::vector<Event>& evs) {
			for (const Event& e : evs) if (const EvWorldAge* a = e.as<EvWorldAge>(); a && a->age > age0) return true;
			return false;
		});
		if (!ok(aged, s + ": a new world age")) continue;
		saveHere(*w, s + " world:age " + std::to_string(w->age), 300);
	}
}
