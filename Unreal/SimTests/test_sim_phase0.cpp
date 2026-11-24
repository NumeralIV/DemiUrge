// DEMIURGE — SimTests/test_sim_phase0.cpp : the fast half of the phase-0 guardrail of the v4 mythos build
// (contracts track M0; SPEC-MYTHOS 5.1 rule 1). The binding half is Tools/simrun/phase0-guardrail.ps1: simrun
// --hash on 8 seeds x passive, kind, cruel, chaotic equal to the pre-mythos build (Tools/simrun/phase0-hashes.txt).
// Here, with every module real (run.ps1 all, simrun.ps1 --tests): a real history at MythosTune::phase 0 leaves
// every v4 extension at its default, so sim::fingerprint hashes exactly what the pre-mythos build hashed.
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

DM_TEST("phase 0: a real 400-year history keeps every mythos extension at its default") {
	if (stub::stubbed("sim") || stub::stubbed("terrain") || stub::stubbed("society")) return;   // needs the real modules
	auto w = sim::create({ .seed = 3, .name = "", .L = 5 });
	if (!ok(w != nullptr, "world")) return;
	watch(*w);
	// (B1) the header default is phase 0 (v3.1); Balance.cpp turns the mythos on, so this world is set back to phase 0
	// the way simrun --mythos-phase 0 does it (attach at phase 0 drops the subscriptions sim::create's use made)
	eq(MythosTune{}.phase, 0, "MythosTune::phase defaults to 0");
	ok(!MythosTune{}.emergent, "emergent defaults to off");
	w->tune<MythosTune>().phase = 0;
	mythos::attach(*w);
	sim::genesis::skip(*w, {});
	sim::advance(*w, 400 * sim::kTicksPerYear);
	ok(w->year >= 399, "history ran: year " + std::to_string(w->year));
	ok(state::extensionsDefault(*w), "wild, BASE fields and MythosState untouched at phase 0");
	ok(!w->settlements.empty(), "people lived");
	for (const powers::PowerDef& d : powers::kPowers)
		if (d.group == powers::Group::Word) ok(!powers::unlocked(*w, d.id), std::string("Word power locked at phase 0: ") + d.id);
	for (const Culture& c : w->cultures) ok(mythos::factors(*w, c.id) == Factors{}, "neutral Factors for culture " + std::to_string(c.id));
	ok(cards::openCards(*w).empty(), "no card at phase 0");
	noNaN(*w, "after 400 years");
}

DM_TEST("phase 0: sim::fingerprint is the legacy hash while extensions are default, and covers them otherwise") {
	if (stub::stubbed("sim")) return;
	auto w = makeWorld({ .L = 3, .seed = 2 });
	const uint32_t f0 = sim::fingerprint(*w);
	ok(state::extensionsDefault(*w), "helper world is default");
	w->mythos.num["mythos_probe"] = 1;
	const uint32_t f1 = sim::fingerprint(*w);
	ok(f1 != f0, "a mythos value changes the fingerprint");
	w->mythos.num.clear();
	eq(sim::fingerprint(*w), f0, "and back");
	w->wild[0] = 4;
	ok(sim::fingerprint(*w) != f0, "wild changes the fingerprint");
	w->wild[0] = 0;
	eq(sim::fingerprint(*w), f0, "and back");
	state::newCulture(*w).traits.courage = 0.9;
	const uint32_t f2 = sim::fingerprint(*w);
	w->cultures.back().traits.courage = 0.5;
	ok(f2 != sim::fingerprint(*w), "a v4 trait changes the fingerprint");
}
