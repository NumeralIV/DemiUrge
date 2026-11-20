// DEMIURGE — SimTests/test_integration_review.cpp : the design review's changes in the simulation
// (docs/reviews/claude-design-review.md findings 1-7, 9, 10; SPEC-SIM sections marked "v3, design
// review"). Owner: wave-3 track "gameplay".
//
// Every test needs the REAL modules (run.ps1 all, simrun.ps1 --tests integration); with stubs each
// returns at once. The long checks (8 seeds x every policy x 4,500 years) are simrun's targets:
//   simrun.ps1 --seeds 1-8 --years 4500 --policy all --summary --jobs 12
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

std::vector<int> liveIds(const World& w) { return civ::live(w); }

double num(const World& w, const std::string& k) {
	auto it = w.vars.num.find(k);
	return it == w.vars.num.end() ? 0.0 : it->second;
}

// Hold a brush power for `seconds` (one press: the first call has dt = 0).
bool hold(World& w, const char* id, int cell, double seconds, double ang = -1) {
	powers::UseOpts o;
	o.ang = ang;
	o.dt = 0;
	if (!powers::use(w, id, cell, o)) return false;
	o.dt = 0.125;
	for (double t = 0; t < seconds - 1e-9; t += o.dt)
		if (!powers::use(w, id, cell, o)) return false;
	return true;
}

void rich(World& w, int age) {
	w.age = age;
	for (Culture& c : w.cultures) c.age = age;
	w.god.aweMax = 1200;
	w.god.awe = 1200;
}

} // namespace

DM_TEST("review 4: an awakening wakes two bands of one people, 4-6 hops apart") {
	if (!allReal()) return;
	for (uint32_t seed : { 1u, 2u, 3u }) {
		auto w = historyWorld(seed);
		const std::vector<int> ids = liveIds(*w);
		if (!eq(int(ids.size()), 2, "two camps at year 0")) continue;
		const Settlement& a = w->settlements[size_t(ids[0])];
		const Settlement& b = w->settlements[size_t(ids[1])];
		ok(a.culture == b.culture, "one people");
		ok(a.pop == 30 && b.pop == 20, "30 + 20 people");
		const double hops = w->geo->angle(a.cell, b.cell) / w->geo->step;
		ok(hops >= 3 && hops <= 7.5, "the second band camps a few days' walk away (" + std::to_string(hops) + " hops)");
	}
}

DM_TEST("review 4: the first trial: a drought over the first camp by year 50 and an early rain prayer") {
	if (!allReal()) return;
	auto w = historyWorld(4);
	double drought = -1, prayer = -1;
	w->bus.onAll([&](const Event& e) {
		if (const EvDrought* d = e.as<EvDrought>(); d && d->start && drought < 0) drought = w->year;
		if (const EvPrayer* p = e.as<EvPrayer>(); p && p->start && prayer < 0) prayer = w->year;
	});
	sim::advance(*w, 4 * 80);
	ok(drought >= 40 && drought <= 50, "the trial drought begins in year 45 (" + std::to_string(drought) + ")");
	ok(prayer >= 40 && prayer <= 60, "a rain prayer follows (" + std::to_string(prayer) + ")");
}

DM_TEST("review 1: witnessed deeds enter the culture's memory; natural events and the Ark years do not") {
	if (!allReal()) return;
	auto w = historyWorld(5);
	const std::vector<int> ids = liveIds(*w);
	const int cid = w->settlements[size_t(ids[0])].culture;
	const int cell = w->settlements[size_t(ids[0])].cell;
	const std::string g = "society_grace_" + std::to_string(cid), wr = "society_wrath_" + std::to_string(cid);
	society::WitnessOpts nat;
	nat.kind = "lightning"; nat.faith = 0.05; nat.good = -1; nat.natural = true;
	society::witness(*w, cell, 0.1, nat);
	eq(num(*w, g) + num(*w, wr), 0.0, "a natural event is not a deed of the god");
	society::WitnessOpts heal;
	heal.kind = "heal"; heal.faith = 0.12; heal.good = 1;
	society::witness(*w, cell, 0.1, heal);
	const double g1 = num(*w, g);
	ok(g1 > 0.3, "a heal at the camp is remembered as grace (" + std::to_string(g1) + ")");
	eq(num(*w, wr), 0.0, "no wrath");
	near(num(*w, "society_heard_" + std::to_string(cid)), w->year, 1e-9, "heard this year");
	// it fades with a half-life of 1,000 years (yearly housekeeping)
	society::yearly(*w);
	near(num(*w, g), g1 * std::pow(0.5, 1.0 / 1000), 1e-9, "memory fades 1/1000 of a half-life a year");
	// nothing is bought while the Ark is being built
	Settlement& cap = w->settlements[size_t(w->cultures[size_t(cid)].capital)];
	cap.ark = 0.3;
	const double g2 = num(*w, g);
	society::witness(*w, cap.cell, 0.1, heal);
	near(num(*w, g), g2, 1e-12, "a deed during the Ark years changes no memory");
	const society::Memory m = society::memory(*w, cid);
	ok(m.grace > 0 && m.lore > 0 && m.silent < 1, "memory(): grace, positive lore, just heard");
}

DM_TEST("review 2: the moods by what happened (silent, two-handed, feared, beloved, prayers)") {
	if (!allReal()) return;
	auto w = historyWorld(6);
	const int cid = w->settlements[size_t(liveIds(*w)[0])].culture;
	const std::string id = std::to_string(cid);
	auto set = [&](double grace, double wrath, double heard, int pray = 0, int answer = 0) {
		w->vars.num["society_grace_" + id] = grace;
		w->vars.num["society_wrath_" + id] = wrath;
		w->vars.num["society_heard_" + id] = heard;
		w->vars.num["society_pray_" + id] = pray;
		w->vars.num["society_answer_" + id] = answer;
	};
	for (Settlement& s : w->settlements) { s.love = 0; s.devotion = 0.5; }
	w->year = 3000;
	set(0, 0, 0);
	eq(int(society::memory(*w, cid).mood), int(Mood::Forgotten), "silent and nothing remembered: forgotten");
	set(3, 0, 0);
	eq(int(society::memory(*w, cid).mood), int(Mood::Parting), "silent but remembered: parting");
	set(3, 2, 2990);
	eq(int(society::memory(*w, cid).mood), int(Mood::Parting), "both hands remembered: parting");
	set(0, 4, 2990);
	eq(int(society::memory(*w, cid).mood), int(Mood::Feared), "wrath remembered: feared");
	set(4, 0, 2990);
	eq(int(society::memory(*w, cid).mood), int(Mood::Beloved), "grace remembered, devout: beloved");
	for (Settlement& s : w->settlements) s.devotion = 0.27;
	set(0.2, 0, 2990, 40, 30);
	eq(int(society::memory(*w, cid).mood), int(Mood::Beloved), "75 % of prayers answered: beloved by its prayers");
	set(0.05, 0, 2990, 40, 5);
	eq(int(society::memory(*w, cid).mood), int(Mood::Parting), "a little remembered, a few prayers: parting");
	for (Settlement& s : w->settlements) s.devotion = 0.1;
	eq(int(society::memory(*w, cid).mood), int(Mood::Forgotten), "no devotion left: forgotten");
	eq(int(society::regard(*w, cid).mood), int(society::memory(*w, cid).mood), "regard() shows the same mood");
}

DM_TEST("review 2: a daughter people inherits its mother's memory") {
	if (!allReal()) return;
	auto w = historyWorld(7);
	const int cid = w->settlements[size_t(liveIds(*w)[0])].culture;
	w->vars.num["society_grace_" + std::to_string(cid)] = 2.5;
	w->vars.num["society_pray_" + std::to_string(cid)] = 12;
	society::NewCultureOpts o;
	o.parent = cid;
	o.reason = BirthReason::Schism;
	const Culture* d = society::newCulture(*w, o);
	if (!ok(d != nullptr, "daughter born")) return;
	near(num(*w, "society_grace_" + std::to_string(d->id)), 2.5, 1e-12, "grace inherited");
	near(num(*w, "society_pray_" + std::to_string(d->id)), 12, 1e-12, "prayers inherited");
}

DM_TEST("review 3: Inspire teaches, answered prayers teach, wrath on a capital sets research back") {
	if (!allReal()) return;
	auto w = historyWorld(8);
	rich(*w, Age::IRON);
	const std::vector<int> ids = liveIds(*w);
	const int cid = w->settlements[size_t(ids[0])].culture;
	Culture& c = w->cultures[size_t(cid)];
	c.science = 100;
	const int capCell = w->settlements[size_t(c.capital)].cell;
	if (!ok(powers::use(*w, "inspire", capCell), "inspire used")) return;
	near(w->cultures[size_t(cid)].science, 102, 1e-9, "Inspire: +2 progress-years");
	if (!ok(powers::use(*w, "quake", capCell), "quake used")) return;
	near(w->cultures[size_t(cid)].science, 102 - w->tune<PowersTune>().setbackQuake, 1e-9, "a quake on the capital: research set back");
	w->cultures[size_t(cid)].science = 5;
	if (!ok(powers::use(*w, "plague", capCell), "plague used")) return;
	near(w->cultures[size_t(cid)].science, 0, 1e-9, "a setback never undoes an age (science floors at 0)");
	eq(w->cultures[size_t(cid)].age, int(Age::IRON), "the age stays");
	// an answered prayer: +0.5, and a rain / bloom one blesses the town
	Settlement& s = w->settlements[size_t(ids[0])];
	s.prayer = Prayer{ PrayerKind::Rain, w->year };
	s.famine = 2;
	s.blessed = 0;
	const double sc0 = w->cultures[size_t(cid)].science;
	society::WitnessOpts o;
	o.kind = "bloom"; o.faith = 0.01; o.good = 0.4;
	society::witness(*w, s.cell, 0.05, o);
	ok(!w->settlements[size_t(ids[0])].prayer.has_value(), "Bloom answers a famine's rain prayer (bread)");
	ok(w->settlements[size_t(ids[0])].blessed >= 20, "the answered town is blessed for 20 years");
	near(w->cultures[size_t(cid)].science, sc0 + w->tune<SocietyTune>().prayerScience, 1e-9, "the priests learn: +1 progress-year");
}

DM_TEST("review 9: a blessed town feeds more and does not count famine years") {
	if (!allReal()) return;
	auto w = historyWorld(9);
	const int sid = liveIds(*w)[0];
	Settlement& s = w->settlements[size_t(sid)];
	s.blessed = 0;
	const double k0 = civ::capacity(*w, s);
	s.blessed = 10;
	const double k1 = civ::capacity(*w, s);
	near(k1, k0 * w->tune<EconomyTune>().blessedK, 1e-6 * k0, "K x blessedK while blessed");
	// over capacity while blessed: no famine years
	w->settlements[size_t(sid)].pop = 5 * std::max(10.0, k1);
	w->settlements[size_t(sid)].famine = 0;
	civ::yearly(*w);
	eq(w->settlements[size_t(sid)].famine, 0, "no famine counted while blessed");
}

DM_TEST("review 5: past Bronze powers wake by age only; Atomic and Space upgrade Rain and Meteor") {
	if (!allReal()) return;
	auto w = historyWorld(10);
	rich(*w, Age::BRONZE);
	ok(powers::unlocked(*w, "quake") && powers::unlocked(*w, "consecrate"), "Bronze: quake, consecrate");
	ok(!powers::unlocked(*w, "plague") && !powers::unlocked(*w, "prophet") && !powers::unlocked(*w, "meteor"), "aweMax 1200 no longer unlocks Iron / Sail powers");
	rich(*w, Age::IRON);
	ok(powers::unlocked(*w, "plague") && !powers::unlocked(*w, "meteor") && !powers::unlocked(*w, "volcano"), "Iron: plague, not meteor / volcano");
	rich(*w, Age::INDUSTRY);
	ok(powers::unlocked(*w, "volcano") && powers::upgrade(*w, "rain") == nullptr, "Industry: volcano, no upgrade yet");
	rich(*w, Age::ATOMIC);
	const powers::Upgrade* up = powers::upgrade(*w, "rain");
	ok(up && std::string(up->name) == "Monsoon", "Atomic: Monsoon");
	const int cell = w->settlements[size_t(liveIds(*w)[0])].cell;
	ok(powers::cost(*w, "rain", 0.10, cell) < 5 * 0.75, "Monsoon rain costs half");
	rich(*w, Age::SPACE);
	ok(powers::upgrade(*w, "meteor") && std::string(powers::upgrade(*w, "meteor")->name) == "Star-fall", "Space: Star-fall");
	// the Awe cap by age
	for (int a = 0; a < Age::COUNT; a++) {
		w->age = a;
		w->god.awe = 0;
		society::yearly(*w);
		ok(w->god.aweMax <= w->tune<SocietyTune>().aweCapAge[size_t(a)] + 1e-9, "aweMax within the cap of age " + std::to_string(a));
	}
}

DM_TEST("review 6: hoarding Awe costs devotion; Consecrate buys a temple, then a Wonder once an age") {
	if (!allReal()) return;
	auto a = historyWorld(11), b = historyWorld(11);
	a->god.awe = a->god.aweMax;            // hoarding
	b->god.awe = 0;
	society::yearly(*a);
	society::yearly(*b);
	ok(society::regard(*a).devotion < society::regard(*b).devotion, "the god who keeps everything loses devotion");

	auto w = historyWorld(12);
	rich(*w, Age::BRONZE);
	const int sid = liveIds(*w)[0];
	Settlement& s = w->settlements[size_t(sid)];
	s.b[Building::SHRINE] = 1;
	s.b[Building::TEMPLE] = 0;
	const int cap = w->cultures[size_t(s.culture)].capital;
	if (!ok(cap == sid, "the first camp is the capital")) return;
	near(powers::cost(*w, "consecrate", -1, s.cell), 150, 1e-9, "a temple costs 150");
	const double awe0 = w->god.awe;
	if (!ok(powers::use(*w, "consecrate", s.cell), "consecrated")) return;
	ok(w->settlements[size_t(sid)].has(Building::TEMPLE), "the shrine is a temple");
	near(w->god.awe, awe0 - 150, 1e-9, "paid 150");
	near(powers::cost(*w, "consecrate", -1, s.cell), w->tune<PowersTune>().wonderCost, 1e-9, "a capital's temple: a Wonder costs 400");
	if (!ok(powers::use(*w, "consecrate", s.cell), "a Wonder raised")) return;
	ok(w->settlements[size_t(sid)].has(Building::WONDER), "the capital has a Wonder");
	// once per age: another capital's temple cannot raise a second one in the same age
	const int other = liveIds(*w)[1];
	w->settlements[size_t(other)].b[Building::SHRINE] = 1;
	w->settlements[size_t(other)].b[Building::TEMPLE] = 1;
	ok(!powers::can(*w, "consecrate", w->settlements[size_t(other)].cell).ok, "a non-capital temple: nothing to consecrate");
	// rain off every territory costs double
	int far = -1;
	for (int c = 0; c < w->n && far < 0; c++) {
		bool clear = true;
		for (int id : liveIds(*w)) if (w->geo->angle(c, w->settlements[size_t(id)].cell) < 0.4) clear = false;
		if (clear) far = c;
	}
	if (ok(far >= 0, "a cell far from every town")) near(powers::cost(*w, "rain", 0.1, far), 2 * powers::cost(*w, "rain", 0.1, s.cell), 1e-9, "rain off territory x2");
}

DM_TEST("review 10: one press of Lower Land stops above the sea (drown:warning); a second press drowns") {
	if (!allReal()) return;
	auto w = historyWorld(13);
	rich(*w, Age::STONE);
	w->god.awe = 1e5;
	const int sid = liveIds(*w)[0];
	const int cell = w->settlements[size_t(sid)].cell;
	int warnings = 0;
	w->bus.on<EvDrownWarning>([&](const EvDrownWarning& e) { if (e.id == sid) warnings++; });
	const double h0 = double(w->elev[size_t(cell)]) - w->seaLevel;
	const double secs = (h0 + 1.0) / w->tune<PowersTune>().sculptHistory;   // more than enough to sink it
	hold(*w, "lower", cell, secs, 0.04);
	ok(!w->settlements[size_t(sid)].dead, "the town survives the first press");
	near(double(w->elev[size_t(cell)]) - w->seaLevel, w->tune<PowersTune>().drownFloor, 2e-3, "its centre stops at the floor");
	eq(warnings, 1, "one drown:warning");
	eq(powers::drownRisk(*w, cell, 0.04), sid, "the hover tip names it now");
	hold(*w, "lower", cell, 2, 0.04);
	ok(w->settlements[size_t(sid)].dead && w->settlements[size_t(sid)].cause == Cause::Drowned, "the second press drowns it");
}

DM_TEST("review 7 / 10: the rising sea's towns die of 'sea'; no silence before year 1,000") {
	if (!allReal()) return;
	auto w = historyWorld(14);
	const int sid = liveIds(*w)[0];
	Cause cause = Cause::None;
	w->bus.on<EvSettlementDestroyed>([&](const EvSettlementDestroyed& e) { if (e.id == sid) cause = e.cause; });
	w->elev[size_t(w->settlements[size_t(sid)].cell)] = float(w->seaLevel - 0.05);
	civ::yearly(*w);
	eq(std::string(toStr(cause)), std::string("sea"), "the sea the people raised took it: cause 'sea'");

	auto v = historyWorld(15);
	int lost = 0;
	v->bus.on<EvPeopleLost>([&](const EvPeopleLost&) { lost++; });
	for (int id : liveIds(*v)) civ::destroy(*v, id, Cause::Famine);
	v->units.clear();
	v->stats.peakPop = 5000;
	sim::advance(*v, 4 * 100);
	ok(!v->ending.has_value(), "no silence at year 100 although the peak passed 1,000");
	eq(lost, 1, "people:lost instead (the player may wake others)");
	v->year = 1000;
	sim::advance(*v, 8);
	ok(v->ending && v->ending->type == EndingType::Silence, "silence once year 1,000 is reached");
}

DM_TEST("review: the memory survives save / load and the run stays deterministic") {
	if (!allReal()) return;
	auto w = historyWorld(16);
	const int cell = w->settlements[size_t(liveIds(*w)[0])].cell;
	society::WitnessOpts heal;
	heal.kind = "heal"; heal.faith = 0.12; heal.good = 1;
	society::witness(*w, cell, 0.1, heal);
	sim::advance(*w, 40);
	std::vector<uint8_t> bytes;
	if (!ok(state::serialize(*w, bytes), "saved")) return;
	auto l = state::deserialize(bytes);
	if (!ok(l != nullptr, "loaded")) return;
	sim::use(*l);
	const society::Memory a = society::memory(*w, -1), b = society::memory(*l, -1);
	near(a.grace, b.grace, 1e-12, "grace kept");
	near(a.silent, b.silent, 1e-9, "heard kept");
	auto x = historyWorld(17), y = historyWorld(17);
	sim::advance(*x, 4 * 120);
	sim::advance(*y, 4 * 120);
	eq(dmt::hash(*x), dmt::hash(*y), "two runs match");
}
