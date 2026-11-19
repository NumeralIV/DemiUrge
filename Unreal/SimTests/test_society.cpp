// DEMIURGE — SimTests/test_society.cpp : dm::society belief half (SPEC-SIM 3.8 society.js, BUILD.md A9).
// No JS tests existed (the JS file was a placeholder): these are the A9 acceptance checks written
// fresh. Every other module is a stub in `run.ps1 society` (Names placeholder names, Civ honest
// reads only, Units / Diplomacy / Chronicle neutral): worlds are set up directly; where Society needs
// a collaborator to act (Units::spawn for prophets, Civ::setCulture for schisms) a hook does it.
// Maps: shots/ue-society-*.png (tests whose names start with "maps:").
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "Civ.h"
#include "MapImg.h"
#include "Powers.h"
#include "Society.h"
#include "State.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

namespace mi = dm::mapimg;

const SocietyTune& TS(const World& w) { return w.tune<SocietyTune>(); }
std::string f3(double x, int d = 3) { char b[64]; std::snprintf(b, sizeof b, "%.*f", d, x); return b; }
void say(const std::string& s) { std::printf("  %s\n", s.c_str()); std::fflush(stdout); }

bool isLand(const World& w, int c) { return w.elev[size_t(c)] >= w.seaLevel; }

// the land cell whose angle from `from` is closest to `ang` (ties: lower id), optionally excluding cells
int landAt(const World& w, int from, double ang, const std::vector<int>& avoid = {}, double minSep = 0) {
	int best = -1;
	double bd = 1e9;
	for (int c = 0; c < w.n; c++) {
		if (!isLand(w, c)) continue;
		bool bad = false;
		for (const int a : avoid) if (w.geo->angle(a, c) < minSep) { bad = true; break; }
		if (bad) continue;
		const double d = std::fabs(w.geo->angle(from, c) - ang);
		if (d < bd) { bd = d; best = c; }
	}
	return best;
}

int addCulture(World& w, int parent = -1) {
	const Culture* c = society::newCulture(w, { parent, -1, BirthReason::Awaken });
	return c ? c->id : -1;
}

int addTown(World& w, int cell, int cid, double pop, double devotion = 0.5, double love = 0) {
	Settlement& s = state::newSettlement(w, cell, cid);
	s.pop = pop;
	s.devotion = devotion;
	s.love = love;
	s.name = "Town" + std::to_string(s.id);
	const int id = s.id;
	if (Culture* c = state::culture(w, cid)) if (c->capital < 0) { c->capital = id; w.settlements[size_t(id)].capital = true; }
	return id;
}

template <class T> std::vector<T> eventsOf(const std::vector<Event>& evs) {
	std::vector<T> out;
	for (const Event& e : evs) if (const T* p = e.as<T>()) out.push_back(*p);
	return out;
}

// One-culture setup on the helper world: a capital at the best habitable cell.
struct Base {
	std::unique_ptr<World> w;
	int cid = -1, cap = -1, capCell = -1;
};
Base base(int L = 5, uint32_t seed = 1) {
	Base b;
	b.w = makeWorld({ .L = L, .seed = seed });
	b.cid = addCulture(*b.w);
	b.capCell = bestHab(*b.w);
	b.cap = addTown(*b.w, b.capCell, b.cid, 500, 0.5, 0);
	return b;
}

// owner[] for maps: every land cell within `hops`-ish angle of a live settlement goes to the nearest
void claimAll(World& w, double ang) {
	std::fill(w.owner.begin(), w.owner.end(), int16_t(-1));
	for (int c = 0; c < w.n; c++) {
		if (!isLand(w, c)) continue;
		double bd = ang;
		int bs = -1;
		for (const Settlement& s : w.settlements) {
			if (s.dead) continue;
			const double d = w.geo->angle(c, s.cell);
			if (d <= bd) { bd = d; bs = s.id; }
		}
		w.owner[size_t(c)] = int16_t(bs);
	}
	for (Settlement& s : w.settlements) s.cells.clear();
	for (int c = 0; c < w.n; c++) if (w.owner[size_t(c)] >= 0) w.settlements[size_t(w.owner[size_t(c)])].cells.push_back(c);
}

// yearly n times, advancing the year like the sim does
void years(World& w, int n, const std::function<void()>& each = nullptr) {
	for (int k = 0; k < n; k++) {
		w.year += 1;
		if (each) each();
		society::yearly(w);
	}
}

} // namespace

// ================================================================ newCulture

DM_TEST("society: newCulture is the factory (names, colour slot, traits, culture:born)") {
	auto w = makeWorld({ .L = 4 });
	const int cell = findLand(*w);
	std::vector<Event> evs = capture(*w, [&] {
		const Culture* a = society::newCulture(*w, { -1, cell, BirthReason::Awaken });
		ok(a != nullptr, "root culture");
	});
	if (!eq(int(w->cultures.size()), 1, "one culture")) return;
	const Culture& a = w->cultures[0];
	eq(a.id, 0, "id == index");
	if (stub::stubbed("names")) {
		eq(a.name, std::string("Folk"), "name from Names (stub)");
		eq(a.adj, std::string("Folkish"));
		eq(a.plural, std::string("the Folk"));
		eq(a.godName, std::string("Maker"), "godName from names::god");
		eq(a.lang, std::string("stub"), "lang from names::language");
	} else {
		ok(!a.name.empty() && !a.adj.empty() && !a.plural.empty() && !a.godName.empty() && !a.lang.empty(), "real names filled");
	}
	eq(a.parent, -1);
	ok(!a.dead);
	for (double t : { a.traits.piety, a.traits.aggression, a.traits.curiosity, a.traits.seafaring }) ok(t >= 0.05 && t <= 0.95, "trait in range");
	ok(a.color == kCultureColors[0], "colour from the first free slot");
	near(a.seen.count("awaken") ? a.seen.at("awaken") : 0, TS(*w).awakenSeen, 1e-12, "an awakened culture remembers its waking");
	auto born = eventsOf<EvCultureBorn>(evs);
	if (eq(int(born.size()), 1, "culture:born once")) {
		eq(born[0].id, 0); eq(born[0].parent, -1); eq(born[0].cell, cell);
		ok(born[0].reason == BirthReason::Awaken, "reason awaken");
	}
	// a second culture takes the next colour slot
	const int b = addCulture(*w);
	ok(w->cultures[size_t(b)].color == kCultureColors[1], "second culture: second colour");
	// invalid cell and dangling parent are tolerated
	const Culture* c = society::newCulture(*w, { 99, -7, BirthReason::Distance });
	if (ok(c != nullptr, "bad parent / cell tolerated")) eq(c->parent, -1, "dangling parent dropped");
	noNaN(*w, "after newCulture");
}

DM_TEST("society: daughter cultures take the parent's language, drift traits, inherit age and memories") {
	stub::Guard guard(true);
	auto w = makeWorld({ .L = 4 });
	const int p = addCulture(*w);
	Culture& pc = w->cultures[size_t(p)];
	pc.lang = "PARENT-LANG";
	pc.age = Age::IRON;
	pc.science = 123;
	pc.traits = { 0.9, 0.1, 0.5, 0.3 };
	pc.seen["rain"] = 2.0;
	w->year = 777;
	std::string langArg;
	if (stub::stubbed("names"))
		stub::hooks().names_language = [&](Rng&, const std::string& parent) { langArg = parent; return std::string("daughter"); };
	double dSum = 0;
	int nd = 0;
	for (int k = 0; k < 40; k++) {
		const Culture* d = society::newCulture(*w, { p, -1, BirthReason::Distance });
		if (!ok(d != nullptr, "daughter")) return;
		eq(d->parent, p, "parent id");
		eq(d->age, int(Age::IRON), "age inherited");
		near(d->science, 123, 1e-12, "science inherited");
		near(d->seen.count("rain") ? d->seen.at("rain") : 0, 2.0 * TS(*w).daughterSeen, 1e-12, "memories inherited (half)");
		near(d->seen.count("awaken") ? d->seen.at("awaken") : 0, TS(*w).awakenSeen * TS(*w).daughterSeen, 1e-12, "the waking is remembered (inherited, half)");
		dSum += std::fabs(d->traits.piety - 0.9) + std::fabs(d->traits.aggression - 0.1) + std::fabs(d->traits.curiosity - 0.5) + std::fabs(d->traits.seafaring - 0.3);
		nd += 4;
	}
	if (stub::stubbed("names")) {
		eq(langArg, std::string("PARENT-LANG"), "names::language got the parent's language");
		eq(w->cultures[1].lang, std::string("daughter"));
	}
	const double meanDrift = dSum / nd;
	say("daughter trait drift: mean |trait - parent| = " + f3(meanDrift) + " (drift sd " + f3(TS(*w).traitDrift) + ")");
	ok(meanDrift > 0.03 && meanDrift < 0.15, "daughters drift from the parent, but stay related");
	near(w->cultures[size_t(p)].lastSplit, 777, 1e-12, "parent.lastSplit = year of the birth");
	// root cultures spread wider
	double rSum = 0;
	for (int k = 0; k < 40; k++) {
		const Culture* r = society::newCulture(*w, {});
		if (!r) break;
		rSum += std::fabs(r->traits.piety - 0.5);
	}
	say("root trait spread: mean |piety - 0.5| = " + f3(rSum / 40));
	ok(rSum / 40 > 0.08, "root traits vary");
	// hard cap on culture ids
	while (society::newCulture(*w, {})) {}
	eq(int(w->cultures.size()), TS(*w).maxCultureIds, "newCulture refuses past maxCultureIds");
	noNaN(*w);
}

// ================================================================ witness

DM_TEST("society: witness applies the documented formulas exactly") {
	auto b = base(5);
	World& w = *b.w;
	Settlement& s0 = w.settlements[size_t(b.cap)];
	s0.devotion = 0.4; s0.jaded = 0.2; s0.love = 0.1;
	const double ang = 0.2;
	const int c1 = landAt(w, b.capCell, 0.1);
	const int s1 = addTown(w, c1, b.cid, 100, 0.3, -0.4);
	w.settlements[size_t(s1)].jaded = 0.5;
	const int cFar = landAt(w, b.capCell, 0.5);
	const int sFar = addTown(w, cFar, b.cid, 100, 0.3, 0);
	const double d1 = w.geo->angle(b.capCell, c1);
	const double f1 = 1 - smooth(d1 / ang);
	say("witness falloff: d = " + f3(d1, 4) + " rad of " + f3(ang) + " -> f = " + f3(f1, 4));
	std::vector<Event> evs = capture(w, [&] {
		const int n = society::witness(w, b.capCell, ang, { "heal", 0.1, 0.5, {}, 0 });
		eq(n, 2, "two witnesses gained");
	});
	const Settlement& a = w.settlements[size_t(b.cap)];
	near(a.devotion, 0.4 + 0.1 * 1 * (1 - 0.2) * (1 - 0.4), 1e-12, "centre devotion");
	near(a.jaded, 0.2 + 2 * 0.1, 1e-12, "centre jaded");
	near(a.love, 0.1 + 0.5 * 0.5 * 1 * (1 - 0.1), 1e-12, "centre love");
	const Settlement& m = w.settlements[size_t(s1)];
	near(m.devotion, 0.3 + 0.1 * f1 * (1 - 0.5) * (1 - 0.3), 1e-12, "falloff devotion");
	near(m.jaded, 0.5 + 2 * 0.1 * f1, 1e-12, "falloff jaded");
	near(m.love, -0.4 + 0.5 * 0.5 * f1 * (1 - 0.4), 1e-12, "falloff love");
	near(w.settlements[size_t(sFar)].devotion, 0.3, 0, "outside ang: untouched");
	near(w.cultures[size_t(b.cid)].seen["heal"], 0.1 * 1 + 0.1 * f1, 1e-12, "culture.seen[kind] += faith x falloff");
	near(w.god.mercy, 0.5 * 0.1, 1e-12, "mercy += max(0, good) x faith");
	near(w.god.wrath, 0, 0, "no wrath for a kind act");
	auto mw = eventsOf<EvMiracleWitnessed>(evs);
	if (eq(int(mw.size()), 1, "miracle:witnessed once")) {
		eq(mw[0].ids, std::vector<int>{ b.cap, s1 }, "ids: nearest first");
		eq(mw[0].kind, std::string("heal"));
		near(mw[0].good, 0.5, 1e-12);
	}
	// saturation: devotion 1 or jaded 1 gain nothing (jaded saturates at 1)
	Settlement& z = w.settlements[size_t(b.cap)];
	z.devotion = 1; z.jaded = 0.95;
	society::witness(w, b.capCell, ang, { "heal", 0.1, 0, {}, 0 });
	near(w.settlements[size_t(b.cap)].devotion, 1, 0, "devotion 1 stays 1");
	near(w.settlements[size_t(b.cap)].jaded, 1, 0, "jaded capped at 1");
	w.settlements[size_t(b.cap)].devotion = 0.2;
	society::witness(w, b.capCell, ang, { "heal", 0.1, 0, {}, 0 });
	near(w.settlements[size_t(b.cap)].devotion, 0.2, 0, "jaded 1: no gain");
	noNaN(w);
}

DM_TEST("society: witness of harm: half faith to the unharmed, full to the harmed, wrath") {
	auto b = base(5);
	World& w = *b.w;
	const int c1 = landAt(w, b.capCell, 0.05);
	const int s1 = addTown(w, c1, b.cid, 100, 0.3, 0.2);
	w.settlements[size_t(b.cap)].devotion = 0.3;
	w.settlements[size_t(b.cap)].love = 0.2;
	const double ang = 0.2;
	const double f1 = 1 - smooth(w.geo->angle(b.capCell, c1) / ang);
	// quake: -0.8 damaged (the capital), -0.2 others
	society::witness(w, b.capCell, ang, { "quake", 0.1, -0.2, { b.cap }, -0.8 });
	const Settlement& h = w.settlements[size_t(b.cap)];
	const Settlement& u = w.settlements[size_t(s1)];
	near(h.devotion, 0.3 + 0.1 * 1 * 0.7, 1e-12, "harmed: full faith");
	near(h.love, 0.2 + 0.5 * -0.8 * 1 * 0.8, 1e-12, "harmed: love by goodHarmed");
	near(u.devotion, 0.3 + 0.05 * f1 * 0.7, 1e-12, "unharmed, good <= 0: half faith");
	near(u.love, 0.2 + 0.5 * -0.2 * f1 * 0.8, 1e-12, "unharmed: love by good");
	near(u.jaded, 2 * 0.05 * f1, 1e-12, "jaded from the faith they got");
	near(w.god.wrath, 0.8 * 0.1, 1e-12, "wrath += max(0, -good) x faith (the harm)");
	near(w.god.mercy, 0, 0);
	// meteor that hurt nobody: good 0 -> no wrath, still awe (half faith) for the witnesses
	const double wr = w.god.wrath;
	const double dv = w.settlements[size_t(s1)].devotion;
	society::witness(w, c1, 0.45, { "meteor", 0.25, 0, {}, -1 });
	near(w.god.wrath, wr, 1e-12, "a harmless meteor adds no wrath");
	ok(w.settlements[size_t(s1)].devotion > dv, "a harmless meteor still awes");
	noNaN(w);
}

DM_TEST("society: an answered prayer doubles faith, turns good to +1, clears the prayer, prayer:answered") {
	auto b = base(5);
	World& w = *b.w;
	Settlement& s = w.settlements[size_t(b.cap)];
	s.devotion = 0.3; s.love = -0.2; s.prayer = Prayer{ PrayerKind::Rain, 0 };
	const int c1 = landAt(w, b.capCell, 0.06);
	const int s1 = addTown(w, c1, b.cid, 50, 0.3, -0.2);
	w.settlements[size_t(s1)].prayer = Prayer{ PrayerKind::Heal, 0 };   // praying for something else
	std::vector<Event> evs = capture(w, [&] { society::witness(w, b.capCell, 0.2, { "rain", 0.02, 0.1, {}, 0 }); });
	const Settlement& a = w.settlements[size_t(b.cap)];
	near(a.devotion, 0.3 + 0.04 * 0.7, 1e-12, "double faith");
	near(a.love, -0.2 + 0.5 * 1 * 1 * 0.8, 1e-12, "good = +1");
	ok(!a.prayer.has_value(), "prayer cleared");
	ok(w.settlements[size_t(s1)].prayer.has_value(), "a different prayer stays");
	auto pa = eventsOf<EvPrayerAnswered>(evs);
	if (eq(int(pa.size()), 1, "prayer:answered once")) { eq(pa[0].id, b.cap); ok(pa[0].kind == PrayerKind::Rain); }
	noNaN(w);
}

DM_TEST("society: miracle:witnessed carries at most 16 ids of those who gained; brush events at most every 2nd call") {
	auto b = base(5);
	World& w = *b.w;
	std::vector<int> placed{ b.capCell };
	for (int k = 0; k < 24; k++) {
		const int c = landAt(w, b.capCell, 0.02 + 0.006 * k, placed, 0.004);
		if (c < 0) break;
		placed.push_back(c);
		addTown(w, c, b.cid, 20, 0.2, 0);
	}
	w.settlements[size_t(b.cap)].devotion = 1;   // gains no devotion, but love still moves
	std::vector<Event> evs = capture(w, [&] {
		const int n = society::witness(w, b.capCell, 0.3, { "heal", 0.1, 0.4, {}, 0 });
		say("witnesses that gained: " + std::to_string(n));
		ok(n > 16, "more than 16 gained");
	});
	auto mw = eventsOf<EvMiracleWitnessed>(evs);
	if (eq(int(mw.size()), 1)) {
		eq(int(mw[0].ids.size()), 16, "ids capped at 16");
		eq(mw[0].ids[0], b.cap, "the capital (love moved) counts as gaining");
	}
	// nobody there: no event
	const int sea = [&] { for (int c = 0; c < w.n; c++) if (!isLand(w, c) && w.geo->angle(c, b.capCell) > 1.2) return c; return -1; }();
	auto none = capture(w, [&] { eq(society::witness(w, sea, 0.05, { "lightning", 0.05, 0, {}, 0 }), 0, "nobody saw it"); });
	eq(int(eventsOf<EvMiracleWitnessed>(none).size()), 0, "no witnesses, no event");
	// brush power (rain): Powers calls 4 times per real second -> 2 events per second
	auto brush = capture(w, [&] { for (int k = 0; k < 8; k++) society::witness(w, b.capCell, 0.2, { "rain", 0.005, 0.1, {}, 0 }); });
	eq(int(eventsOf<EvMiracleWitnessed>(brush).size()), 4, "8 brush calls (2 s) -> 4 events");
	auto click = capture(w, [&] { for (int k = 0; k < 3; k++) society::witness(w, b.capCell, 0.1, { "heal", 0.01, 0.5, {}, 0 }); });
	eq(int(eventsOf<EvMiracleWitnessed>(click).size()), 3, "click powers: every call");
	// bad input
	eq(society::witness(w, -1, 0.1, { "heal", 0.1, 1, {}, 0 }), 0, "bad cell");
	eq(society::witness(w, b.capCell, std::nan(""), { "heal", 0.1, 1, {}, 0 }), 0, "NaN ang");
	society::witness(w, b.capCell, 0.1, { "heal", std::nan(""), std::nan(""), { -5, 9999 }, std::nan("") });
	society::witness(w, b.capCell, 0, { "", 0.1, 0.5, {}, 0 });
	noNaN(w, "after bad witness input");
}

DM_TEST("society: brush love follows elapsed seconds, not calls (frame-rate independent)") {
	auto run = [](int calls, double secondsEach) {
		auto b = base(5);
		World& w = *b.w;
		w.settlements[size_t(b.cap)].love = 0;
		w.settlements[size_t(b.cap)].devotion = 0.3;
		for (int k = 0; k < calls; k++) society::witness(w, b.capCell, 0.2, { "rain", 0.02 * secondsEach, 0.6, {}, 0 });
		return std::pair<double, double>(w.settlements[size_t(b.cap)].love, w.settlements[size_t(b.cap)].devotion);
	};
	const auto a = run(4, 0.25), c = run(8, 0.125), one = run(1, 1.0);
	say("1 s of rain: 4 calls -> love " + f3(a.first, 4) + " devotion " + f3(a.second, 4) + "; 8 calls -> love " + f3(c.first, 4)
		+ " devotion " + f3(c.second, 4) + "; 1 call of 1 s -> love " + f3(one.first, 4));
	near(one.first, 0.5 * 0.6, 1e-12, "one second: love += 0.5 x good x (1 - |love|)");
	near(a.first, c.first, 0.05 * a.first, "same love per second at 4 or 8 calls per second (+-5 %)");
	near(a.second, c.second, 0.05 * (a.second - 0.3), "same devotion per second (+-5 %)");
	const auto ten = run(40, 0.25);
	say("10 s of rain: love " + f3(ten.first, 3) + " (was 1.000 before scaling love per second)");
	ok(ten.first > 0.9 && ten.first < 1.0, "sustained rain is adored, but not instantly");
}

DM_TEST("society: every power the player uses registers in belief (devotion, love, seen, mercy / wrath)") {
	// the table's wAng / faith / good, as Powers passes them (brush: faith per s x 0.25 s)
	struct Act { const char* id; double good, goodHarmed; bool hurts; };
	const Act acts[] = {
		{ "raise", 0, -0.5, true }, { "lower", 0, -0.5, true }, { "rain", 0.6, 0, false }, { "bloom", 0.4, 0, false },
		{ "lightning", 0, -0.6, true }, { "quake", -0.2, -0.8, true }, { "plague", -0.7, -0.7, true },
		{ "meteor", 0, -1, true }, { "volcano", 0, -1, true }, { "heal", 1, 0, false }, { "inspire", 0.6, 0, false },
		{ "prophet", 0.5, 0, false }, { "peace", 0.8, 0, false },
	};
	int registered = 0;
	for (const Act& a : acts) {
		auto b = base(5);
		World& w = *b.w;
		const powers::PowerDef* def = powers::byId(a.id);
		if (!ok(def != nullptr, std::string("power ") + a.id)) continue;
		const double faith = def->brush ? def->faith * 0.25 : def->faith;
		const double ang = def->wScope == powers::WScope::BrushRel ? def->wAng * def->ang : def->wScope == powers::WScope::Radius ? def->wAng : 0.1;
		Settlement& s = w.settlements[size_t(b.cap)];
		s.devotion = 0.3; s.love = 0; s.jaded = 0;
		std::vector<int> harmed;
		if (a.hurts) harmed.push_back(b.cap);
		const double m0 = w.god.mercy, wr0 = w.god.wrath;
		auto evs = capture(w, [&] { society::witness(w, b.capCell, ang, { a.id, faith, a.good, harmed, a.goodHarmed }); });
		const Settlement& t = w.settlements[size_t(b.cap)];
		const double seen = w.cultures[size_t(b.cid)].seen.count(a.id) ? w.cultures[size_t(b.cid)].seen.at(a.id) : 0;
		const double gLocal = a.hurts ? a.goodHarmed : a.good;
		const bool devUp = t.devotion > 0.3;
		const bool loveOk = gLocal > 0 ? t.love > 0 : gLocal < 0 ? t.love < 0 : t.love == 0;
		const double actG = a.hurts && std::fabs(a.goodHarmed) > std::fabs(a.good) ? a.goodHarmed : a.good;
		const bool godOk = actG > 0 ? w.god.mercy > m0 : actG < 0 ? w.god.wrath > wr0 : (w.god.mercy == m0 && w.god.wrath == wr0);
		const bool evOk = !eventsOf<EvMiracleWitnessed>(evs).empty();
		say(std::string(a.id) + ": devotion " + f3(0.3) + " -> " + f3(t.devotion, 4) + ", love " + f3(t.love, 3) + ", seen " + f3(seen, 4)
			+ ", mercy +" + f3(w.god.mercy - m0, 4) + ", wrath +" + f3(w.god.wrath - wr0, 4) + (evOk ? ", event" : ", NO EVENT"));
		ok(devUp && seen > 0 && loveOk && godOk && evOk, std::string("power registers in belief: ") + a.id);
		if (devUp && seen > 0 && loveOk && godOk && evOk) registered++;
	}
	eq(registered, int(sizeof(acts) / sizeof(acts[0])), "every witnessed power registered");
	// genesis powers: the awakened people remember their waking (seen["awaken"])
	auto w = makeWorld({ .L = 4 });
	const int cid = addCulture(*w);
	ok(w->cultures[size_t(cid)].seen["awaken"] > 0, "awaken registers in the culture's memory");
}

// ================================================================ belief

DM_TEST("society: devotion drifts 2 %/yr to the baseline by age and piety") {
	const SocietyTune T0{};
	for (int age = 0; age < Age::COUNT; age++) {
		for (const double piety : { 0.0, 0.5, 1.0 }) {
			auto b = base(4);
			World& w = *b.w;
			w.cultures[size_t(b.cid)].age = age;
			w.cultures[size_t(b.cid)].traits.piety = piety;
			w.settlements[size_t(b.cap)].devotion = 0.9;
			years(w, 400);
			const double want = T0.baseline[size_t(age)] * (0.6 + 0.8 * piety);
			const double got = w.settlements[size_t(b.cap)].devotion;
			if (piety == 0.5) say(std::string("age ") + kAgeNames[age] + " piety 0.5: devotion after 400 yr " + f3(got) + " (baseline " + f3(want) + ")");
			near(got, want, 0.9 * std::pow(0.98, 400) + 1e-6, std::string("baseline ") + kAgeNames[age] + " piety " + f3(piety, 1));
		}
	}
	// one exact step with buildings, a prophet and fear
	auto b = base(4);
	World& w = *b.w;
	Culture& c = w.cultures[size_t(b.cid)];
	c.traits.piety = 0.5;
	c.age = Age::BRONZE;
	Settlement& s = w.settlements[size_t(b.cap)];
	s.b[Building::SHRINE] = 1; s.b[Building::TEMPLE] = 1;
	s.devotion = 0.5; s.love = -0.6;
	c.prophet = Prophet{ "Seer", b.cap, 9999 };
	w.year = 1;
	society::yearly(w);
	const double baseB = 0.6 * 1.0 + 0.05 + 0.10 + 0.15 + 0.1;   // fear before Atomic: +0.1
	near(w.settlements[size_t(b.cap)].devotion, 0.5 + 0.02 * (baseB - 0.5), 1e-12, "shrine + temple + prophet + fear (Bronze)");
	c.age = Age::ATOMIC;
	w.settlements[size_t(b.cap)].devotion = 0.5;
	w.settlements[size_t(b.cap)].love = -0.6;
	w.year = 2;
	society::yearly(w);
	const double baseA = 0.18 * 1.0 + 0.05 + 0.10 + 0.15 - 0.1;   // defiance from Atomic: -0.1
	near(w.settlements[size_t(b.cap)].devotion, 0.5 + 0.02 * (baseA - 0.5), 1e-12, "fear becomes defiance (Atomic)");
	noNaN(w);
}

DM_TEST("society: housekeeping: jaded decays 5 %/yr, love drifts 1 %/yr toward 0, culture pop / towns") {
	auto b = base(4);
	World& w = *b.w;
	Settlement& s = w.settlements[size_t(b.cap)];
	s.jaded = 0.8; s.love = 0.5;
	const int c2 = landAt(w, b.capCell, 0.1);
	addTown(w, c2, b.cid, 250, 0.5, -0.5);
	w.year = 1;
	society::yearly(w);
	near(w.settlements[size_t(b.cap)].jaded, 0.8 * 0.95, 1e-12, "jaded x 0.95");
	near(w.settlements[size_t(b.cap)].love, 0.5 * 0.99, 1e-12, "love x 0.99");
	near(w.settlements[1].love, -0.5 * 0.99, 1e-12, "negative love too");
	near(w.cultures[size_t(b.cid)].pop, 750, 1e-9, "culture pop");
	eq(w.cultures[size_t(b.cid)].towns, 2, "culture towns");
}

// ================================================================ Awe

DM_TEST("society: aweMax and Awe income match the formula") {
	auto w = makeWorld({ .L = 4 });
	const int cid = addCulture(*w);
	near(society::aweIncome(*w), 0, 0, "no believers: no income");
	const int c0 = bestHab(*w);
	const int camp = addTown(*w, c0, cid, 30, 0.6, 0);
	const double one = society::aweIncome(*w);
	say("one camp, devotion 0.6: income " + f3(one) + " Awe/yr (spec ~0.70)");
	near(one, 0.6 + 0.9 * std::log(1 + 0.6 * 0.2), 1e-12, "one camp");
	near(one, 0.70, 0.01, "one camp ~0.70");
	// three shrined villages at devotion 0.5
	w->settlements[size_t(camp)].dead = true;
	std::vector<int> placed{ c0 };
	for (int k = 0; k < 3; k++) {
		const int c = landAt(*w, c0, 0.1 + 0.1 * k, placed, 0.05);
		placed.push_back(c);
		const int sid = addTown(*w, c, cid, 100, 0.5, 0);
		w->settlements[size_t(sid)].tier = 1;
		w->settlements[size_t(sid)].b[Building::SHRINE] = 1;
	}
	const double three = society::aweIncome(*w);
	say("three shrined villages, devotion 0.5: income " + f3(three) + " (spec ~1.4)");
	near(three, 0.6 + 0.9 * std::log(1 + 3 * 0.5 * 0.7 * 1.5), 1e-12, "three villages");
	// twenty shrined villages, half of them with a temple, devotion 0.5
	double S20 = 3 * 0.5 * 0.7 * 1.5;
	for (int k = 0; k < 17; k++) {
		const int c = landAt(*w, c0, 0.05 + 0.02 * k, placed, 0.02);
		placed.push_back(c);
		const int sid = addTown(*w, c, cid, 300, 0.5, 0);
		w->settlements[size_t(sid)].tier = 1;
		w->settlements[size_t(sid)].b[Building::SHRINE] = 1;
		w->settlements[size_t(sid)].b[Building::TEMPLE] = uint8_t(k < 10);
		S20 += 0.5 * (k < 10 ? 1.9 : 0.7) * 1.5;
	}
	const double twenty = society::aweIncome(*w);
	say("twenty shrined villages (10 with temples), devotion 0.5: income " + f3(twenty) + " (spec ~3.3)");
	near(twenty, 0.6 + 0.9 * std::log(1 + S20), 1e-12, "twenty villages formula");
	ok(twenty > 3.0 && twenty < 3.6, "twenty settlements ~3.3");
	// late game: 90 cities with temples at devotion 0.25
	auto late = makeWorld({ .L = 5 });
	const int lc = addCulture(*late);
	double S = 0;
	int placedLate = 0;
	for (int c = 0; c < late->n && placedLate < 90; c += 37) {
		if (!isLand(*late, c)) continue;
		const int sid = addTown(*late, c, lc, 5000, 0.25, 0);
		late->settlements[size_t(sid)].tier = 3;
		late->settlements[size_t(sid)].b[Building::SHRINE] = 1;
		late->settlements[size_t(sid)].b[Building::TEMPLE] = 1;
		S += 0.25 * 1.9 * 2.5;
		placedLate++;
	}
	const double lateInc = society::aweIncome(*late);
	say("late game, " + std::to_string(placedLate) + " templed cities at devotion 0.25: income " + f3(lateInc) + " (spec ~4.5)");
	near(lateInc, 0.6 + 0.9 * std::log(1 + S), 1e-9, "late game formula");
	// aweMax: 100 + 25 shrines + 60 temples + 150 wonders, cap 1200
	World& v = *w;
	v.settlements[1].b[Building::TEMPLE] = 1;
	v.settlements[2].b[Building::WONDER] = 1;
	int shrines = 0;
	for (const Settlement& s : v.settlements) if (!s.dead && s.has(Building::SHRINE)) shrines++;
	v.god.awe = 10;
	v.year = 1;
	society::yearly(v);
	const double inc = society::aweIncome(v);   // income after this year's belief drift (no prayers here)
	int temples = 0;
	for (const Settlement& s : v.settlements) if (!s.dead && s.has(Building::TEMPLE)) temples++;
	near(v.god.aweMax, std::min(1200.0, 100 + 25.0 * shrines + 60.0 * temples + 150), 1e-12, "aweMax formula (capped)");
	{
		auto u = makeWorld({ .L = 4 });
		const int uc = addCulture(*u);
		const int a0 = bestHab(*u);
		const int s0 = addTown(*u, a0, uc, 100, 0.5, 0);
		const int s1 = addTown(*u, landAt(*u, a0, 0.2), uc, 100, 0.5, 0);
		u->settlements[size_t(s0)].b[Building::SHRINE] = 1;
		u->settlements[size_t(s1)].b[Building::SHRINE] = 1;
		u->settlements[size_t(s1)].b[Building::TEMPLE] = 1;
		u->settlements[size_t(s0)].b[Building::WONDER] = 1;
		u->year = 1;
		society::yearly(*u);
		say("aweMax with 2 shrines, 1 temple, 1 wonder: " + f3(u->god.aweMax, 0) + " (100 + 50 + 60 + 150 = 360)");
		near(u->god.aweMax, 360, 1e-12, "aweMax formula");
	}
	near(v.god.awe, 10 + inc, 1e-9, "yearly adds the income");
	v.god.awe = v.god.aweMax - 0.1;
	v.year = 2;
	society::yearly(v);
	near(v.god.awe, v.god.aweMax, 1e-12, "clamped to aweMax");
	late->year = 1;
	society::yearly(*late);
	near(late->god.aweMax, 1200, 0, "aweMax capped at 1200");
}

// ================================================================ prayers

DM_TEST("society: prayers start for their causes, end when the cause is gone, expire with penalties") {
	auto b = base(5);
	World& w = *b.w;
	const int cid2 = addCulture(w);
	std::vector<int> placed{ b.capCell };
	auto town = [&](double pop, int cu) {
		const int c = landAt(w, b.capCell, 0.08 + 0.05 * double(placed.size()), placed, 0.04);
		placed.push_back(c);
		return addTown(w, c, cu, pop, 0.5, 0);
	};
	const int rain = b.cap;                 w.settlements[size_t(rain)].famine = 1;
	const int heal = town(400, b.cid);      w.settlements[size_t(heal)].plague = 0.4;
	const int peace = town(300, cid2);
	society::rel(w, b.cid, cid2)->war = true;
	w.cultures[size_t(cid2)].exhaustion = 0.5;
	const int bloom = town(200, b.cid);
	{ Settlement& s = w.settlements[size_t(bloom)]; w.scorch[size_t(s.cell)] = 0.6f; }
	const int drought = town(150, b.cid);
	{ Disaster& d = state::newDisaster(w, DisasterType::Drought, w.settlements[size_t(drought)].cell); d.ang = 0.01; d.dur = 10; }
	const int faithless = town(1000, b.cid);   w.settlements[size_t(faithless)].famine = 2; w.settlements[size_t(faithless)].devotion = 0.1;
	const int calm = town(100, b.cid);
	w.year = 10;
	auto evs = capture(w, [&] { society::yearly(w); });
	auto pr = eventsOf<EvPrayer>(evs);
	auto kindOf = [&](int sid) { const auto& p = w.settlements[size_t(sid)].prayer; return p ? int(p->kind) : -1; };
	eq(kindOf(rain), int(PrayerKind::Rain), "famine -> rain");
	eq(kindOf(heal), int(PrayerKind::Heal), "plague -> heal");
	eq(kindOf(peace), int(PrayerKind::Peace), "war and exhaustion -> peace");
	eq(kindOf(bloom), int(PrayerKind::Bloom), "scorched territory -> bloom");
	eq(kindOf(drought), int(PrayerKind::Rain), "inside a drought -> rain");
	eq(kindOf(faithless), -1, "devotion < 0.15 does not pray");
	eq(kindOf(calm), -1, "no cause, no prayer");
	eq(int(pr.size()), 5, "five prayer:start events");
	for (const EvPrayer& e : pr) ok(e.start, "start: true");
	// cause gone: plague cured
	w.settlements[size_t(heal)].plague = 0;
	w.year = 11;
	auto ev2 = capture(w, [&] { society::yearly(w); });
	eq(kindOf(heal), -1, "cured: prayer ends");
	auto p2 = eventsOf<EvPrayer>(ev2);
	ok(p2.size() == 1 && p2[0].id == heal && !p2[0].start, "prayer { start: false } for the cured");
	// expiry after 15 unanswered years: devotion -0.05, love -0.05
	for (int y = 12; y < 25; y++) { w.year = y; society::yearly(w); }
	ok(kindOf(rain) == int(PrayerKind::Rain), "still praying at year 24");
	const Settlement before = w.settlements[size_t(rain)];
	const Culture& cu = w.cultures[size_t(b.cid)];
	double baseR = TS(w).baseline[size_t(cu.age)] * (0.6 + 0.8 * cu.traits.piety);
	if (before.love * 0.99 < -0.3) baseR += 0.1;
	w.year = 25;
	auto ev3 = capture(w, [&] { society::yearly(w); });
	const Settlement& after = w.settlements[size_t(rain)];
	ok(!after.prayer.has_value(), "expired after 15 years");
	near(after.love, before.love * 0.99 - 0.05, 1e-12, "love -0.05 on expiry");
	near(after.devotion, before.devotion + 0.02 * (baseR - before.devotion) - 0.05, 1e-12, "devotion -0.05 on expiry");
	bool sawEnd = false;
	for (const EvPrayer& e : eventsOf<EvPrayer>(ev3)) if (e.id == rain && !e.start) sawEnd = true;
	ok(sawEnd, "prayer { start: false } on expiry");
	// the next year it may pray again (famine persists)
	w.year = 26;
	society::yearly(w);
	eq(kindOf(rain), int(PrayerKind::Rain), "prays again the year after");
	noNaN(w);
}

DM_TEST("society: at most 6 prayers at once, largest settlements first") {
	auto b = base(5);
	World& w = *b.w;
	std::vector<int> placed{ b.capCell };
	std::vector<int> ids{ b.cap };
	w.settlements[size_t(b.cap)].famine = 1;
	w.settlements[size_t(b.cap)].pop = 5;
	for (int k = 0; k < 9; k++) {
		const int c = landAt(w, b.capCell, 0.05 + 0.03 * k, placed, 0.02);
		placed.push_back(c);
		const int sid = addTown(w, c, b.cid, 100 + 10 * k, 0.5, 0);
		w.settlements[size_t(sid)].famine = 1;
		ids.push_back(sid);
	}
	w.year = 1;
	society::yearly(w);
	int praying = 0;
	for (const Settlement& s : w.settlements) praying += s.prayer ? 1 : 0;
	eq(praying, 6, "at most 6");
	for (int k = 4; k < 10; k++) ok(w.settlements[size_t(ids[size_t(k)])].prayer.has_value(), "the six largest pray");
	ok(!w.settlements[size_t(b.cap)].prayer.has_value(), "the smallest waits");
}

// ================================================================ housekeeping: dead cultures

DM_TEST("society: a culture with no settlement and no settler dies once and is cleaned up") {
	stub::Guard guard(true);
	auto b = base(5);
	World& w = *b.w;
	const int foe = addCulture(w);
	const int fcell = landAt(w, b.capCell, 0.3);
	const int fs = addTown(w, fcell, foe, 300, 0.5, 0);
	society::rel(w, b.cid, foe)->war = true;
	const int lone = addCulture(w);      // a culture with only a settler on the road survives
	Unit& settler = state::newUnit(w, UnitType::Settler);
	settler.culture = lone;
	Unit& army = state::newUnit(w, UnitType::Army);
	army.culture = foe;
	const int armyId = army.id;
	w.cultures[size_t(foe)].prophet = Prophet{ "Doomed", fs, 999 };
	w.cultures[size_t(foe)].edict = Edict::Crusade;
	std::vector<std::pair<int, std::string>> kills;
	std::vector<std::vector<double>> peaces;
	if (stub::stubbed("units")) stub::hooks().units_kill = [&](World& ww, int uid, std::string_view cause) {
		kills.emplace_back(uid, std::string(cause));
		return state::eraseUnit(ww, uid);
	};
	if (stub::stubbed("diplomacy")) stub::hooks().society_makePeace = [&](World&, int a, int bb, double truce, int victor) {
		peaces.push_back({ double(a), double(bb), truce, double(victor) });
	};
	w.settlements[size_t(fs)].dead = true;   // the foe's last settlement falls
	w.year = 1;
	auto evs = capture(w, [&] { society::yearly(w); });
	ok(w.cultures[size_t(foe)].dead, "foe culture dead");
	ok(!w.cultures[size_t(lone)].dead, "a culture with a settler on the road lives");
	ok(!w.cultures[size_t(b.cid)].dead, "the living culture lives");
	eq(int(eventsOf<EvCultureDied>(evs).size()), 1, "culture:died once");
	ok(!w.cultures[size_t(foe)].prophet, "prophet cleared");
	ok(w.cultures[size_t(foe)].edict == Edict::None, "edict cleared");
	eq(int(eventsOf<EvProphetDied>(evs).size()), 1, "prophet:died for its prophet");
	if (stub::stubbed("units")) {
		ok(kills.size() == 1 && kills[0].first == armyId && kills[0].second == "disbanded", "its units killed 'disbanded'");
		ok(state::findUnit(w, armyId) == nullptr, "army gone");
	}
	if (stub::stubbed("diplomacy")) {
		if (eq(int(peaces.size()), 1, "makePeace on its war")) {
			eq(int(peaces[0][3]), b.cid, "victor = the other side");
			near(peaces[0][2], w.tune<DiplomacyTune>().truceYears, 0, "default truce");
		}
	}
	w.year = 2;
	auto ev2 = capture(w, [&] { society::yearly(w); });
	eq(int(eventsOf<EvCultureDied>(ev2).size()), 0, "never twice");
	near(w.cultures[size_t(foe)].pop, 0, 0, "dead culture pop 0");
	noNaN(w);
}

// ================================================================ prophets and edicts

DM_TEST("society: raiseProphet, setEdict rules, travel as a unit, death clears the edict") {
	stub::Guard guard(true);
	auto b = base(5);
	World& w = *b.w;
	const int c1 = landAt(w, b.capCell, 0.15);
	const int s1 = addTown(w, c1, b.cid, 200, 0.2, 0);   // the least devout: his destination
	const int c2 = landAt(w, b.capCell, 0.25, { c1 }, 0.05);
	addTown(w, c2, b.cid, 200, 0.6, 0);
	w.year = 100;
	ok(!society::setEdict(w, b.cid, Edict::Peace), "no edict without a prophet");
	std::vector<Event> evs = capture(w, [&] {
		const Prophet* p = society::raiseProphet(w, b.cap);
		if (ok(p != nullptr, "prophet raised")) {
			eq(p->settlement, b.cap);
			ok(p->until >= 100 + 60 * 0.85 - 1e-9 && p->until <= 100 + 60 * 1.15 + 1e-9, "term ~60 years");
			ok(stub::stubbed("names") ? p->name == "Person" : !p->name.empty(), "name from names::person");
		}
	});
	auto risen = eventsOf<EvProphetRisen>(evs);
	ok(risen.size() == 1 && risen[0].culture == b.cid && risen[0].settlement == b.cap, "prophet:risen");
	ok(society::raiseProphet(w, b.cap) == nullptr, "one prophet per culture");
	ok(society::raiseProphet(w, -3) == nullptr, "bad id");
	auto ed = capture(w, [&] { ok(society::setEdict(w, b.cid, Edict::Peace), "edict with a prophet"); });
	ok(eventsOf<EvEdict>(ed).size() == 1 && eventsOf<EvEdict>(ed)[0].edict == Edict::Peace, "edict event");
	ok(!society::setEdict(w, b.cid, Edict::Build), "no change within 20 years");
	ok(!society::setEdict(w, b.cid, Edict::None), "None is not an edict");
	w.year = 120;
	ok(society::setEdict(w, b.cid, Edict::Expand), "change after 20 years");
	eq(int(w.cultures[size_t(b.cid)].edict), int(Edict::Expand));
	// travel: Units::spawn hooked to create the unit
	std::vector<units::SpawnOpts> spawns;
	int unitId = -1;
	if (stub::stubbed("units")) stub::hooks().units_spawn = [&](World& ww, const units::SpawnOpts& o) -> Unit* {
		spawns.push_back(o);
		Unit& u = state::newUnit(ww, o.type);
		u.culture = o.culture; u.from = o.from; u.to = o.to; u.dest = o.dest; u.size = o.size;
		unitId = u.id;
		return &u;
	};
	if (!stub::stubbed("units")) return;
	for (int y = 101; y <= 110; y++) { w.year = y; society::yearly(w); }
	if (!eq(int(spawns.size()), 1, "travels after ~10 years")) return;
	ok(spawns[0].type == UnitType::Prophet && spawns[0].to == s1 && spawns[0].from == b.cap && spawns[0].culture == b.cid, "prophet unit to the least devout");
	// arrival (Diplomacy's onArrive moves him; simulate it) -> he lives on there
	w.cultures[size_t(b.cid)].prophet->settlement = s1;
	state::eraseUnit(w, unitId);
	w.year = 111;
	society::yearly(w);
	ok(w.cultures[size_t(b.cid)].prophet.has_value(), "arrived: alive");
	eq(w.cultures[size_t(b.cid)].prophet->settlement, s1, "lives at the destination");
	// next journey: the unit dies on the road
	for (int y = 112; y <= 121 && spawns.size() < 2; y++) { w.year = y; society::yearly(w); }
	if (!eq(int(spawns.size()), 2, "second journey")) return;
	state::eraseUnit(w, unitId);    // killed (no move)
	w.year += 1;
	auto evd = capture(w, [&] { society::yearly(w); });
	ok(!w.cultures[size_t(b.cid)].prophet.has_value(), "died on the road");
	ok(w.cultures[size_t(b.cid)].edict == Edict::None, "edict cleared with him");
	eq(int(eventsOf<EvProphetDied>(evd).size()), 1, "prophet:died");
	// term end
	w.year = 200;
	society::raiseProphet(w, b.cap);
	const double until = w.cultures[size_t(b.cid)].prophet->until;
	stub::hooks().units_spawn = nullptr;
	while (w.year < until - 1) { w.year += 1; society::yearly(w); }
	ok(w.cultures[size_t(b.cid)].prophet.has_value(), "alive before the term ends");
	w.year = std::floor(until) + 1;
	auto evt = capture(w, [&] { society::yearly(w); });
	ok(!w.cultures[size_t(b.cid)].prophet.has_value() && eventsOf<EvProphetDied>(evt).size() == 1, "term over: prophet:died");
	noNaN(w);
}

DM_TEST("society: the build edict raises a Wonder in the capital after 30 years, once") {
	auto b = base(5);
	World& w = *b.w;
	w.year = 50;
	society::raiseProphet(w, b.cap);
	w.cultures[size_t(b.cid)].prophet->until = 1e6;
	ok(society::setEdict(w, b.cid, Edict::Build), "build edict");
	std::vector<Event> all;
	for (int y = 51; y <= 100; y++) {
		w.year = y;
		auto evs = capture(w, [&] { society::yearly(w); });
		all.insert(all.end(), evs.begin(), evs.end());
		if (y == 79) ok(!w.settlements[size_t(b.cap)].has(Building::WONDER), "not before 30 years");
		if (y == 80) ok(w.settlements[size_t(b.cap)].has(Building::WONDER), "wonder at 30 years");
	}
	auto wb = eventsOf<EvWonderBuilt>(all);
	if (eq(int(wb.size()), 1, "wonder:built once")) {
		eq(wb[0].settlement, b.cap);
		say("wonder: " + wb[0].name);
		ok(!stub::stubbed("names") || wb[0].name.find("Place") != std::string::npos, "named after a place");
	}
	int bev = 0;
	for (const auto& e : eventsOf<EvSettlementBuilding>(all)) if (e.building == Building::WONDER && e.id == b.cap) bev++;
	eq(bev, 1, "settlement:building { wonder } once");
	ok(w.god.aweMax >= 250 - 1e-9, "a wonder raises aweMax by 150");
	// the other edicts are state their readers act on (Diplomacy: peace, crusade; Economy: expand)
	w.year = 200;
	ok(society::setEdict(w, b.cid, Edict::Crusade), "change to crusade");
	eq(int(w.cultures[size_t(b.cid)].edict), int(Edict::Crusade));
	near(w.cultures[size_t(b.cid)].edictSince, 200, 0, "edictSince");
}

DM_TEST("society: a living prophet raises the devotion baseline by 0.15") {
	auto b = base(4);
	World& w = *b.w;
	w.cultures[size_t(b.cid)].traits.piety = 0.5;
	w.settlements[size_t(b.cap)].devotion = 0.3;
	w.cultures[size_t(b.cid)].prophet = Prophet{ "Seer", b.cap, 1e9 };
	years(w, 300);
	near(w.settlements[size_t(b.cap)].devotion, 0.55 + 0.15, 0.01, "baseline + prophet");
}

// ================================================================ schisms

DM_TEST("society: distant settlements secede together after 120 estranged years (distance)") {
	stub::Guard guard(true);
	auto b = base(5);
	World& w = *b.w;
	const SocietyTune& tn = TS(w);
	// two towns far from the capital, close to each other
	const int fa = landAt(w, b.capCell, 0.75);
	const int fb = landAt(w, fa, 0.1, {}, 0);
	ok(w.geo->angle(fb, b.capCell) > tn.apartAng, "both far from the capital");
	const int sa = addTown(w, fa, b.cid, 100, 0.5, 0);
	const int sb = addTown(w, fb, b.cid, 80, 0.5, 0);
	const int near1 = landAt(w, b.capCell, 0.1);
	const int sn = addTown(w, near1, b.cid, 90, 0.5, 0);
	std::vector<std::pair<int, int>> moved;
	if (stub::stubbed("civ")) stub::hooks().civ_setCulture = [&](World& ww, int sid, int cu) {
		moved.emplace_back(sid, cu);
		ww.settlements[size_t(sid)].culture = cu;
	};
	w.year = 200;
	std::vector<Event> all;
	for (int y = 0; y < 125; y++) {
		w.year += 1;
		auto evs = capture(w, [&] { society::yearly(w); });
		all.insert(all.end(), evs.begin(), evs.end());
		if (y == 118) eq(int(w.cultures.size()), 1, "no birth before 120 years apart");
	}
	auto born = eventsOf<EvCultureBorn>(all);
	if (!eq(int(born.size()), 1, "one daughter culture")) return;
	ok(born[0].reason == BirthReason::Distance, "reason distance");
	eq(born[0].parent, b.cid, "parent");
	const int nc = born[0].id;
	if (stub::stubbed("civ")) {
		eq(int(moved.size()), 2, "two settlements change culture via civ::setCulture");
		eq(w.settlements[size_t(sa)].culture, nc);
		eq(w.settlements[size_t(sb)].culture, nc);
		eq(w.settlements[size_t(sn)].culture, b.cid, "the near town stays");
		eq(w.settlements[size_t(sa)].apart, 0, "apart reset");
	}
	eq(w.settlements[size_t(sn)].apart, 0, "near town never estranged");
	if (stub::stubbed("civ")) ok(stub::called("civ_electCapital") >= 1, "capitals re-elected");   // (the call log only sees stubs)
	say("secession at year " + f3(w.cultures[size_t(nc)].born, 0) + "; daughter '" + w.cultures[size_t(nc)].name + "' parent " + std::to_string(b.cid));
	noNaN(w);
}

DM_TEST("society: love schism, landmass estrangement, 150-year and 6-culture limits") {
	stub::Guard guard(true);
	if (!stub::stubbed("civ")) return;
	stub::hooks().civ_setCulture = [](World& ww, int sid, int cu) { ww.settlements[size_t(sid)].culture = cu; };
	// love: two towns near the capital whose love differs from the culture's mean by > 0.6
	{
		auto b = base(5);
		World& w = *b.w;
		w.settlements[size_t(b.cap)].pop = 5000;
		const int ca = landAt(w, b.capCell, 0.1);
		const int cb = landAt(w, ca, 0.05);
		const int sa = addTown(w, ca, b.cid, 50, 0.5, -0.9);
		const int sb = addTown(w, cb, b.cid, 50, 0.5, -0.9);
		std::vector<Event> all;
		w.year = 300;
		for (int y = 0; y < 121; y++) {
			w.year += 1;
			// the god keeps terrorising them (love would otherwise drift back toward 0)
			w.settlements[size_t(sa)].love = -0.9; w.settlements[size_t(sb)].love = -0.9;
			auto evs = capture(w, [&] { society::yearly(w); });
			all.insert(all.end(), evs.begin(), evs.end());
		}
		auto born = eventsOf<EvCultureBorn>(all);
		ok(born.size() == 1 && born[0].reason == BirthReason::Schism, "love estrangement -> 'schism'");
		// within 150 years no second birth from the same parent
		const int nc = int(w.cultures.size()) - 1;
		const int cc = landAt(w, b.capCell, 0.15, { ca, cb }, 0.04);
		const int cd = landAt(w, cc, 0.04, { ca, cb }, 0.04);
		const int sc = addTown(w, cc, b.cid, 50, 0.5, 0.9);
		const int sd = addTown(w, cd, b.cid, 50, 0.5, 0.9);
		w.settlements[size_t(sc)].apart = 200; w.settlements[size_t(sd)].apart = 200;
		w.year += 1;
		w.settlements[size_t(sc)].love = 0.9; w.settlements[size_t(sd)].love = 0.9;
		society::yearly(w);
		eq(int(w.cultures.size()) - 1, nc, "no second birth within 150 years of the last split");
		w.year = w.cultures[size_t(b.cid)].lastSplit + 151;
		w.settlements[size_t(sc)].love = 0.9; w.settlements[size_t(sd)].love = 0.9;
		society::yearly(w);
		eq(int(w.cultures.size()) - 1, nc + 1, "after 150 years the parent may split again");
	}
	// landmass: towns on another landmass are estranged even when near
	{
		auto b = base(5);
		World& w = *b.w;
		const int myLand = w.cache.landId[size_t(b.capCell)];
		int ia = -1, ib = -1;
		for (int c = 0; c < w.n && ib < 0; c++) {
			if (!isLand(w, c) || w.cache.landId[size_t(c)] == myLand || w.cache.landId[size_t(c)] < 0) continue;
			if (ia < 0) { ia = c; continue; }
			if (w.cache.landId[size_t(c)] == w.cache.landId[size_t(ia)] && w.geo->angle(c, ia) < 0.2 && c != ia) ib = c;
		}
		if (ok(ia >= 0 && ib >= 0, "another landmass")) {
			addTown(w, ia, b.cid, 50, 0.5, 0);
			addTown(w, ib, b.cid, 50, 0.5, 0);
			w.year = 500;
			auto evs = capture(w, [&] { years(w, 121); });
			auto born = eventsOf<EvCultureBorn>(evs);
			ok(born.size() == 1 && born[0].reason == BirthReason::Distance, "another landmass -> 'distance'");
		}
	}
	// at most 6 living cultures
	{
		auto b = base(5);
		World& w = *b.w;
		for (int k = 0; k < 5; k++) {
			const int cu = addCulture(w);
			addTown(w, landAt(w, b.capCell, 0.3 + 0.1 * k), cu, 50, 0.5, 0);
		}
		const int fa = landAt(w, b.capCell, 0.8);
		const int fb = landAt(w, fa, 0.05);
		addTown(w, fa, b.cid, 50, 0.5, 0);
		addTown(w, fb, b.cid, 50, 0.5, 0);
		w.year = 500;
		auto evs = capture(w, [&] { years(w, 130); });
		eq(int(eventsOf<EvCultureBorn>(evs).size()), 0, "six living cultures: no seventh");
		int living = 0;
		for (const Culture& c : w.cultures) living += c.dead ? 0 : 1;
		eq(living, 6);
	}
}

// ================================================================ the Ark

DM_TEST("society: the Ark progresses only in a Space-age capital with a launch site, launches once") {
	auto b = base(5);
	World& w = *b.w;
	Culture& c = w.cultures[size_t(b.cid)];
	c.age = Age::ATOMIC;
	Settlement& cap = w.settlements[size_t(b.cap)];
	cap.pop = 1.5e6;
	cap.b[Building::LAUNCH] = 1;
	const int other = addTown(w, landAt(w, b.capCell, 0.2), b.cid, 1000, 0.5, 0);
	w.settlements[size_t(other)].b[Building::LAUNCH] = 1;     // not the capital
	years(w, 10);
	near(w.settlements[size_t(b.cap)].ark, 0, 0, "no Ark before Space");
	w.cultures[size_t(b.cid)].age = Age::SPACE;
	w.settlements[size_t(b.cap)].b[Building::LAUNCH] = 0;
	years(w, 10);
	near(w.settlements[size_t(b.cap)].ark, 0, 0, "no Ark without a launch site");
	w.settlements[size_t(b.cap)].b[Building::LAUNCH] = 1;
	std::vector<Event> all;
	int launchYear = -1;
	for (int k = 0; k < 300; k++) {
		w.year += 1;
		auto evs = capture(w, [&] { society::yearly(w); });
		all.insert(all.end(), evs.begin(), evs.end());
		if (k == 0) near(w.settlements[size_t(b.cap)].ark, 1.0 / 250 * std::clamp((1.5e6 + 1000) / 1.5e6, 0.8, 1.25), 1e-12, "first year: 1/250 x clamp(pop / 1.5M)");
		if (launchYear < 0 && w.vars.ark) launchYear = k + 1;
	}
	near(w.settlements[size_t(other)].ark, 0, 0, "a non-capital builds no Ark");
	eq(int(eventsOf<EvArkStarted>(all).size()), 1, "ark:started once");
	auto la = eventsOf<EvArkLaunched>(all);
	if (eq(int(la.size()), 1, "ark:launched exactly once")) { eq(la[0].culture, b.cid); eq(la[0].settlement, b.cap); }
	say("Ark launched after " + std::to_string(launchYear) + " years at pop 1.5M (spec 250)");
	ok(launchYear >= 249 && launchYear <= 251, "~250 years at 1.5 million people");
	if (ok(w.vars.ark.has_value(), "vars.ark")) { eq(w.vars.ark->culture, b.cid); eq(w.vars.ark->settlement, b.cap); }
	// pop scaling clamp
	auto b2 = base(5);
	b2.w->cultures[size_t(b2.cid)].age = Age::SPACE;
	b2.w->settlements[size_t(b2.cap)].b[Building::LAUNCH] = 1;
	b2.w->settlements[size_t(b2.cap)].pop = 100;
	years(*b2.w, 1);
	near(b2.w->settlements[size_t(b2.cap)].ark, 0.8 / 250, 1e-12, "small culture: x 0.8");
	b2.w->settlements[size_t(b2.cap)].pop = 1e8;
	years(*b2.w, 1);
	near(b2.w->settlements[size_t(b2.cap)].ark, 0.8 / 250 + 1.25 / 250, 1e-12, "huge culture: x 1.25");
}

// ================================================================ regard

DM_TEST("society: regard is population-weighted and gives the right mood at each threshold") {
	auto b = base(4);
	World& w = *b.w;
	const int c2 = addCulture(w);
	w.settlements[size_t(b.cap)].pop = 300; w.settlements[size_t(b.cap)].devotion = 0.6; w.settlements[size_t(b.cap)].love = 0.5;
	addTown(w, landAt(w, b.capCell, 0.1), b.cid, 100, 0.2, -0.3);
	addTown(w, landAt(w, b.capCell, 0.3), c2, 100, 0.1, -0.9);
	const society::Regard r = society::regard(w, b.cid);
	near(r.devotion, (300 * 0.6 + 100 * 0.2) / 400, 1e-12, "weighted devotion");
	near(r.love, (300 * 0.5 - 100 * 0.3) / 400, 1e-12, "weighted love");
	const society::Regard all = society::regard(w, -1);
	near(all.devotion, (180 + 20 + 10) / 500.0, 1e-12, "everyone");
	const society::Regard none = society::regard(w, 42);
	ok(none.devotion == 0 && none.love == 0 && none.mood == Mood::Forgotten, "unknown culture: zeros, forgotten");
	// thresholds on a single settlement
	auto moodOf = [&](double dv, double lv) {
		w.settlements[size_t(b.cap)].devotion = dv; w.settlements[size_t(b.cap)].love = lv;
		for (Settlement& s : w.settlements) if (s.id != b.cap) s.pop = 0;
		return society::regard(w, b.cid).mood;
	};
	ok(moodOf(0.35, 0.2) == Mood::Beloved, "beloved at devotion 0.35, love 0.2");
	ok(moodOf(0.349, 0.2) == Mood::Parting, "just below: parting");
	ok(moodOf(0.6, 0.199) == Mood::Parting, "love just below 0.2: parting");
	ok(moodOf(0.6, -0.2) == Mood::Feared, "feared at love -0.2");
	ok(moodOf(0.1, -0.2) == Mood::Feared, "fear outranks forgetting");
	ok(moodOf(0.199, 0) == Mood::Forgotten, "forgotten below devotion 0.2");
	ok(moodOf(0.2, 0) == Mood::Parting, "parting at 0.2");
	// zero population: plain means
	w.settlements[size_t(b.cap)].pop = 0;
	const society::Regard z = society::regard(w, b.cid);
	ok(std::isfinite(z.devotion) && std::isfinite(z.love), "zero population: finite");
}

// ================================================================ robustness and determinism

namespace {
uint32_t stressRun(uint32_t seed, bool print) {
	stub::Guard guard(false);
	if (stub::stubbed("civ")) stub::hooks().civ_setCulture = [](World& ww, int sid, int cu) { ww.settlements[size_t(sid)].culture = cu; };
	if (stub::stubbed("units")) stub::hooks().units_spawn = [](World& ww, const units::SpawnOpts& o) -> Unit* {
		Unit& u = state::newUnit(ww, o.type);
		u.culture = o.culture; u.from = o.from; u.to = o.to; u.dest = o.dest;
		return &u;
	};
	auto w = makeWorld({ .L = 5, .seed = seed });
	Rng r(seed * 7919u + 1);
	const int c0 = addCulture(*w);
	std::vector<int> land;
	for (int c = 0; c < w->n; c++) if (isLand(*w, c)) land.push_back(c);
	for (int k = 0; k < 40; k++) {
		const int cell = land[size_t(r.integer(0, int(land.size()) - 1))];
		const int sid = addTown(*w, cell, c0, r.range(10, 3000), r.f(), r.range(-1, 1));
		Settlement& s = w->settlements[size_t(sid)];
		s.tier = r.integer(0, 4);
		s.famine = r.chance(0.2) ? 1 : 0;
		s.plague = r.chance(0.1) ? 0.5 : 0;
		for (int bb = 0; bb < Building::COUNT; bb++) s.b[size_t(bb)] = uint8_t(r.chance(0.2));
	}
	w->cultures[size_t(c0)].age = Age::SPACE;
	const char* kinds[] = { "rain", "heal", "meteor", "quake", "bloom", "lightning", "nuke" };
	for (int y = 0; y < 600; y++) {
		w->year += 1;
		for (int k = 0; k < 3; k++) {
			const int cell = land[size_t(r.integer(0, int(land.size()) - 1))];
			const int kd = r.integer(0, 6);
			society::witness(*w, cell, r.range(0, 0.4), { kinds[kd], r.range(0, 0.3), r.range(-1, 1), { r.integer(0, 39) }, r.range(-1, 0) });
		}
		if (y % 50 == 0) {
			const int sid = r.integer(0, int(w->settlements.size()) - 1);
			society::raiseProphet(*w, sid);
			society::setEdict(*w, w->settlements[size_t(sid)].culture, Edict(r.integer(1, 4)));
		}
		if (y % 97 == 13) { const int sid = r.integer(0, int(w->settlements.size()) - 1); w->settlements[size_t(sid)].dead = true; }
		if (!w->units.empty() && r.chance(0.3)) state::eraseUnit(*w, w->units[0].id);
		society::yearly(*w);
	}
	if (print) {
		int living = 0;
		for (const Culture& c : w->cultures) living += c.dead ? 0 : 1;
		const society::Regard rg = society::regard(*w);
		say("stress seed " + std::to_string(seed) + ": 600 yr, cultures " + std::to_string(w->cultures.size()) + " (" + std::to_string(living)
			+ " living), awe " + f3(w->god.awe, 1) + "/" + f3(w->god.aweMax, 0) + ", mercy " + f3(w->god.mercy, 2) + ", wrath " + f3(w->god.wrath, 2)
			+ ", regard dev " + f3(rg.devotion) + " love " + f3(rg.love) + " mood " + toStr(rg.mood) + (w->vars.ark ? ", ark launched" : ""));
	}
	noNaN(*w, "stress run");
	const std::string bad = checkFinite(*w);
	ok(bad.empty(), "checkFinite: " + bad);
	return hash(*w);
}
} // namespace

DM_TEST("society: 600-year stress run: no NaN, deterministic") {
	const double t0 = nowMs();
	const uint32_t h1 = stressRun(3, true);
	const double t1 = nowMs();
	const uint32_t h2 = stressRun(3, false);
	eq(h1, h2, "same calls, same world");
	ok(stressRun(4, true) != h1, "another seed differs");
	say("600 yearly calls with 40 settlements: " + f3(t1 - t0, 1) + " ms including setup");
}

DM_TEST("society: yearly cost with 160 settlements at L = 6") {
	auto w = makeWorld({ .L = 6, .seed = 2 });
	std::vector<int> cids;
	for (int k = 0; k < 6; k++) cids.push_back(addCulture(*w));
	int placed = 0;
	for (int c = 0; c < w->n && placed < 160; c += 97) {
		if (!isLand(*w, c)) continue;
		addTown(*w, c, cids[size_t(placed % 6)], 500 + placed, 0.4, 0);
		placed++;
	}
	claimAll(*w, 0.03);
	const double t0 = nowMs();
	for (int k = 0; k < 100; k++) { w->year += 1; society::yearly(*w); }
	const double ms = (nowMs() - t0) / 100;
	say("society::yearly with " + std::to_string(placed) + " settlements, 6 cultures, L = 6: " + f3(ms, 3) + " ms per call");
	ok(ms < 3 * 5, "yearly well within the tick budget");
}

// ================================================================ maps

DM_TEST("maps: devotion and love after kind and cruel acts, culture owners after a schism") {
	stub::Guard guard(false);
	if (stub::stubbed("civ")) stub::hooks().civ_setCulture = [](World& ww, int sid, int cu) { ww.settlements[size_t(sid)].culture = cu; };
	auto w = makeWorld({ .L = 6, .seed = 1 });
	const int c0 = addCulture(*w);
	// settlements on a jittered lattice over the land
	Rng r(11);
	std::vector<int> cells;
	for (int c = 0; c < w->n; c++) {
		if (!isLand(*w, c) || w->hab[size_t(c)] < 0.1) continue;
		bool far = true;
		for (const int o : cells) if (w->geo->angle(o, c) < 0.11) { far = false; break; }
		if (far && r.chance(0.5)) cells.push_back(c);
	}
	for (const int c : cells) addTown(*w, c, c0, 300, 0.4, 0);
	claimAll(*w, 0.07);
	say("map world: " + std::to_string(cells.size()) + " settlements");
	// the god: rain on one region (kind), meteors on another (cruel), quake with harm
	const int kind = cells[0];
	int cruel = cells[0];
	for (const int c : cells) if (w->geo->angle(c, kind) > w->geo->angle(cruel, kind)) cruel = c;
	for (int k = 0; k < 40; k++) society::witness(*w, kind, 0.5, { "rain", 0.02 * 0.25, 0.6, {}, 0 });
	for (int k = 0; k < 6; k++) {
		std::vector<int> hit;
		for (const int sid : civ::near(*w, cruel, 0.08)) hit.push_back(sid);
		society::witness(*w, cruel, 0.45, { "meteor", 0.25, 0, hit, -1 });
		w->year += 5;
	}
	auto colorBy = [&](const std::function<double(const Settlement&)>& val, double lo, double hi) {
		return [&w, val, lo, hi](int c) -> mi::Rgb {
			const int o = w->owner[size_t(c)];
			if (w->elev[size_t(c)] < w->seaLevel) return mi::Rgb{ 20, 30, 60 };
			if (o < 0) return mi::Rgb{ 60, 60, 55 };
			const Settlement& s = w->settlements[size_t(o)];
			if (s.cell == c) return mi::Rgb{ 255, 255, 255 };
			const mi::Rgb h = mi::heat(sat((val(s) - lo) / (hi - lo)));
			return h;
		};
	};
	const std::string fDev = shot("ue-society-devotion.png");
	const std::string fLove = shot("ue-society-love.png");
	ok(mi::custom(*w, fDev, colorBy([](const Settlement& s) { return s.devotion; }, 0, 1)), "devotion map written");
	ok(mi::custom(*w, fLove, colorBy([](const Settlement& s) { return s.love; }, -1, 1)), "love map written");
	double dk = w->settlements[size_t(w->owner[size_t(kind)])].devotion, lk = w->settlements[size_t(w->owner[size_t(kind)])].love;
	double dc = w->settlements[size_t(w->owner[size_t(cruel)])].devotion, lc = w->settlements[size_t(w->owner[size_t(cruel)])].love;
	say("rain centre: devotion " + f3(dk) + " love " + f3(lk) + "; meteor centre: devotion " + f3(dc) + " love " + f3(lc));
	ok(lk > 0.3 && lc < -0.3, "the kind region loves, the struck region fears");
	ok(dk > 0.4 && dc > 0.4, "both believe more: the god is seen either way");
	// schism map: the far region secedes from the capital (cells[0])
	w->cultures[size_t(c0)].capital = w->settlements[0].id;
	w->year = 1000;
	years(*w, 125);
	int born = int(w->cultures.size()) - 1;
	for (Settlement& s : w->settlements) w->owner[size_t(s.cell)] = int16_t(s.id);
	claimAll(*w, 0.07);
	const std::string fOwn = shot("ue-society-schism.png");
	ok(mi::owner(*w, fOwn), "owner map written");
	say("cultures after 125 years: " + std::to_string(w->cultures.size()) + " (" + std::to_string(born) + " born by secession)");
	ok(born >= 1 && born <= 5, "distant regions seceded, at most 6 living cultures");
	noNaN(*w);
}
