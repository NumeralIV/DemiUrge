// DEMIURGE — SimTests/test_integration_review_adv.cpp : adversarial checks of the design review's
// changes (wave-3 track "gameplay", verified by "gameplay-verify"). Every test needs the REAL modules
// (run.ps1 all, simrun.ps1 --tests integration); with stubs each returns at once.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "Balance.h"
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

std::unique_ptr<World> historyWorld(uint32_t seed, int L = 5) {
	auto w = sim::create({ .seed = seed, .name = "", .L = L });
	dmt::watch(*w);
	sim::genesis::skip(*w, {});
	return w;
}

// One press of a brush power held for `seconds` with frames of `frame` seconds; frame `hitchAt`
// (0-based, -1 none) arrives `hitch` seconds late, as after a long game-thread stall.
bool holdWithHitch(World& w, const char* id, int cell, double seconds, double ang, double frame, int hitchAt, double hitch) {
	powers::UseOpts o;
	o.ang = ang;
	o.dt = 0;
	if (!powers::use(w, id, cell, o)) return false;
	int k = 0;
	for (double t = 0; t < seconds - 1e-9; k++) {
		o.dt = k == hitchAt ? hitch : frame;
		t += o.dt;
		if (!powers::use(w, id, cell, o)) return false;
	}
	return true;
}

} // namespace

DM_TEST("review-adv 10: a frame hitch in the middle of one Lower Land press does not drown the town") {
	if (!allReal()) return;
	auto w = historyWorld(13);
	w->god.aweMax = 1e5;
	w->god.awe = 1e5;
	const int sid = civ::live(*w)[0];
	const int cell = w->settlements[size_t(sid)].cell;
	int warnings = 0;
	w->bus.on<EvDrownWarning>([&](const EvDrownWarning& e) { if (e.id == sid) warnings++; });
	const double h0 = double(w->elev[size_t(cell)]) - w->seaLevel;
	const double secs = (h0 + 1.0) / w->tune<PowersTune>().sculptHistory;
	// hold long enough to reach the floor, then one 0.8 s stall (> strokeGap), then keep holding
	const int frames = int(std::ceil(secs / 0.05));
	holdWithHitch(*w, "lower", cell, secs + 2.0, 0.04, 0.05, frames + 4, 0.8);
	std::printf("  town %s, centre %.3f above the sea, %d drown:warning\n", w->settlements[size_t(sid)].dead ? "DROWNED" : "alive",
		double(w->elev[size_t(cell)]) - w->seaLevel, warnings);
	ok(!w->settlements[size_t(sid)].dead, "still one press: the town survives a stalled frame");
	eq(warnings, 1, "one drown:warning for the press");
	// the deliberate second press still drowns it
	powers::UseOpts o;
	o.ang = 0.04;
	o.dt = 0;
	powers::use(*w, "lower", cell, o);
	o.dt = 0.125;
	for (int i = 0; i < 16; i++) powers::use(*w, "lower", cell, o);
	ok(w->settlements[size_t(sid)].dead && w->settlements[size_t(sid)].cause == Cause::Drowned, "a second press drowns it");
}

DM_TEST("review-adv 10: one wide press of Lower Land over an inland town does not drown it in a lake") {
	if (!allReal()) return;
	// the guard keeps the centre above the SEA; a pit dug around an inland town could still fill with
	// lake water (Economy: water > drownWater -> drowned). Look for the highest inland town.
	auto w = historyWorld(21);
	w->god.aweMax = 1e5;
	w->god.awe = 1e5;
	int best = -1;
	double bestH = -1e9;
	for (int id : civ::live(*w)) {
		const double h = double(w->elev[size_t(w->settlements[size_t(id)].cell)]) - w->seaLevel;
		if (h > bestH) { bestH = h; best = id; }
	}
	if (best < 0) return;
	const int cell = w->settlements[size_t(best)].cell;
	Cause cause = Cause::None;
	w->bus.on<EvSettlementDestroyed>([&](const EvSettlementDestroyed& e) { if (e.id == best) cause = e.cause; });
	const double secs = (bestH + 1.0) / w->tune<PowersTune>().sculptHistory;
	holdWithHitch(*w, "lower", cell, secs, 0.10, 0.125, -1, 0);
	const double hAfter = double(w->elev[size_t(cell)]) - w->seaLevel;
	sim::advance(*w, 8 * 30);   // 30 years: hydro fills what it fills, the economy judges
	std::printf("  town %d: %.3f -> %.3f above the sea; after 30 years water %.3f, %s (%s)\n", best, bestH, hAfter,
		double(w->water[size_t(cell)]), w->settlements[size_t(best)].dead ? "destroyed" : "alive", toStr(cause));
	ok(!(w->settlements[size_t(best)].dead && cause == Cause::Drowned), "one press never drowns a town, not even in a lake");
}

DM_TEST("review-adv 6: Consecrate's Wonder costs 400 and is once an age; a temple costs 150") {
	if (!allReal()) return;
	auto w = historyWorld(22);
	w->age = Age::IRON;
	for (Culture& c : w->cultures) c.age = Age::IRON;
	w->god.aweMax = 1200;
	const int sid = civ::live(*w)[0];
	Settlement& s = w->settlements[size_t(sid)];
	const int cid = s.culture;
	w->cultures[size_t(cid)].capital = sid;
	s.b[Building::SHRINE] = 1;
	s.b[Building::TEMPLE] = 0;
	s.b[Building::WONDER] = 0;
	const int cell = s.cell;
	near(powers::cost(*w, "consecrate", -1, cell), 150.0, 1e-9, "a shrine: the temple rite costs 150");
	w->god.awe = 149;
	ok(!powers::can(*w, "consecrate", cell).ok, "149 Awe cannot pay it");
	w->god.awe = 150;
	ok(powers::use(*w, "consecrate", cell, {}), "150 Awe raises the temple");
	ok(w->settlements[size_t(sid)].has(Building::TEMPLE), "the shrine is a temple");
	near(powers::cost(*w, "consecrate", -1, cell), 400.0, 1e-9, "a capital's temple: the Wonder rite costs 400");
	w->god.awe = 399;
	ok(!powers::can(*w, "consecrate", cell).ok && !powers::use(*w, "consecrate", cell, {}), "399 Awe cannot raise a Wonder");
	ok(!w->settlements[size_t(sid)].has(Building::WONDER), "no Wonder for 399");
	w->god.awe = 400;
	ok(powers::use(*w, "consecrate", cell, {}), "400 Awe raises the Wonder");
	ok(w->settlements[size_t(sid)].has(Building::WONDER), "a Wonder");
	near(w->god.awe, 0.0, 1e-6, "all 400 paid");
	// another capital in the same age: refused (once per age of the world)
	int other = -1;
	for (int id : civ::live(*w))
		if (id != sid) { other = id; break; }
	if (other >= 0) {
		Settlement& o = w->settlements[size_t(other)];
		o.b[Building::TEMPLE] = 1;
		o.b[Building::SHRINE] = 1;
		o.b[Building::WONDER] = 0;
		w->cultures[size_t(o.culture)].capital = other;
		w->god.awe = 1200;
		ok(!powers::use(*w, "consecrate", o.cell, {}), "a second Wonder in the same age is refused");
		w->age = Age::SAIL;
		for (Culture& c : w->cultures) c.age = Age::SAIL;
		ok(powers::use(*w, "consecrate", o.cell, {}), "the next age allows one more");
	}
}

DM_TEST("review-adv 1: the memory is finite and bounded under a flood of deeds; lore stays in -1..1") {
	if (!allReal()) return;
	auto w = historyWorld(23);
	w->god.aweMax = 1e9;
	w->god.awe = 1e9;
	const int sid = civ::live(*w)[0];
	const int cid = w->settlements[size_t(sid)].culture;
	const int cell = w->settlements[size_t(sid)].cell;
	for (int i = 0; i < 400; i++) {
		powers::use(*w, "heal", cell, {});
		powers::use(*w, "lightning", cell, {});
	}
	const society::Memory m = society::memory(*w, cid);
	std::printf("  grace %.2f wrath %.2f lore %+.3f verdict %+.3f mood %s\n", m.grace, m.wrath, m.lore, m.verdict, toStr(m.mood));
	ok(std::isfinite(m.grace) && std::isfinite(m.wrath) && std::isfinite(m.lore) && std::isfinite(m.verdict), "finite");
	ok(m.lore >= -1 && m.lore <= 1 && m.verdict >= -1 && m.verdict <= 1, "bounded");
	ok(m.mixed && m.mood == Mood::Parting, "both hands in a flood: the Two-Handed (parting)");
	// poisoned memory values are read as nothing
	w->vars.num["society_grace_" + std::to_string(cid)] = std::nan("");
	w->vars.num["society_wrath_" + std::to_string(cid)] = -5;
	const society::Memory p = society::memory(*w, cid);
	ok(std::isfinite(p.lore) && std::isfinite(p.verdict), "a NaN / negative memory reads finite");
}

DM_TEST("review-adv 10: a town already within 0.05 of the sea is warned, not drowned, by its first press") {
	if (!allReal()) return;
	// late in a history the rising sea brings many towns to the floor; the first press of Lower Land
	// over such a town must still only warn (one press never drowns a town)
	auto w = historyWorld(24);
	w->god.aweMax = 1e5;
	w->god.awe = 1e5;
	const int sid = civ::live(*w)[0];
	const int cell = w->settlements[size_t(sid)].cell;
	const double floorH = w->seaLevel + w->tune<PowersTune>().drownFloor;
	w->elev[size_t(cell)] = float(w->seaLevel + 0.02);       // the sea came up to its door
	int warnings = 0;
	w->bus.on<EvDrownWarning>([&](const EvDrownWarning& e) { if (e.id == sid) warnings++; });
	eq(powers::drownRisk(*w, cell, 0.04), -1, "not warned yet: a press here would not drown it");
	holdWithHitch(*w, "lower", cell, 3.0, 0.04, 0.125, -1, 0);
	eq(powers::drownRisk(*w, cell, 0.04), sid, "warned: the hover tip names it now");
	ok(!w->settlements[size_t(sid)].dead, "the first press does not drown it");
	eq(warnings, 1, "it gets its drown:warning");
	ok(double(w->elev[size_t(cell)]) <= floorH + 1e-6 && double(w->elev[size_t(cell)]) >= w->seaLevel + 0.02 - 1e-6,
		"its centre is not raised, nor lowered below where it stood");
	holdWithHitch(*w, "lower", cell, 2.0, 0.04, 0.125, -1, 0);
	ok(w->settlements[size_t(sid)].dead && w->settlements[size_t(sid)].cause == Cause::Drowned, "the second press drowns it");
}

DM_TEST("review-adv 1: an absent god's late kindness is held lightly (neglect); wrath is remembered whole") {
	if (!allReal()) return;
	auto w = historyWorld(25);
	const int sid = civ::live(*w)[0];
	const int cid = w->settlements[size_t(sid)].culture;
	const std::string id = std::to_string(cid);
	for (Settlement& s : w->settlements) { s.love = 0.3; s.devotion = 0.5; }
	w->year = 3600;
	w->vars.num["society_heard_" + id] = 3599;
	w->vars.num["society_grace_" + id] = 1.5;
	w->vars.num["society_wrath_" + id] = 0;
	w->vars.num["society_neglect_" + id] = 0;
	const society::Memory present = society::memory(*w, cid);
	eq(int(present.mood), int(Mood::Beloved), "a god that was always there: beloved");
	w->vars.num["society_neglect_" + id] = 780;   // about 2,900 silent years before the kindness
	const society::Memory absent = society::memory(*w, cid);
	std::printf("  present: lore %+.3f verdict %+.3f %s; absent (neglect 780, presence %.3f): lore %+.3f verdict %+.3f %s\n",
		present.lore, present.verdict, toStr(present.mood), absent.presence, absent.lore, absent.verdict, toStr(absent.mood));
	ok(absent.mood != Mood::Beloved, "a long-absent god's late kindness is not beloved");
	w->vars.num["society_grace_" + id] = 0;
	w->vars.num["society_wrath_" + id] = 1.5;
	for (Settlement& s : w->settlements) s.love = -0.3;
	eq(int(society::memory(*w, cid).mood), int(Mood::Feared), "wrath is remembered whole, neglect or not");
	// neglect grows only after neglectAfter silent years, fades, and daughters inherit it
	auto v = historyWorld(26);
	const int c2 = v->settlements[size_t(civ::live(*v)[0])].culture;
	const std::string k2 = "society_neglect_" + std::to_string(c2);
	const double after = v->tune<SocietyTune>().neglectAfter;
	sim::advance(*v, int(4 * (after - 5)));   // 4 ticks a year
	ok(!v->vars.num.count(k2) || v->vars.num[k2] == 0, "no neglect before neglectAfter silent years");
	sim::advance(*v, 4 * 60);
	ok(v->vars.num.count(k2) && v->vars.num[k2] > 30 && v->vars.num[k2] < 70, "about a year of neglect per silent year after that");
}
