// DEMIURGE — SimTests/test_society_v4.cpp : the belief half's v4 mythos rules and reads (SPEC-MYTHOS 3.2, 3.3,
// 3.5, 3.8, 3.10, 1.4; track M5 "society-v4"): the new traits and shapeTraits, nudge, the Factors reads (devotion
// baseline, love target, prayer chance, unanswered penalty, answered gain, Awe weight, schism pace, the archetype's
// income), presence through witnessScale, the fear rule by aspect and courage, the Victory and Sun prayers,
// answerPrayer and remember, and the messenger archetypes Society reads (a Child's years, a Shepherd's journeys).
//
// Mythos records are set as Mythos stores them (w.mythos.factors / faith / figures at MythosTune::phase 1: the reads
// are honest in the stubs and in the real modules). Tests that need a hook (witnessScale, civ::addScience) return
// early when the module is real (run.ps1 all).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "Civ.h"
#include "Mythos.h"
#include "Society.h"
#include "State.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

bool isLand(const World& w, int c) { return w.elev[size_t(c)] >= w.seaLevel; }

int landAt(const World& w, int from, double ang, const std::vector<int>& avoid = {}) {
	int best = -1;
	double bd = 1e9;
	for (int c = 0; c < w.n; c++) {
		if (!isLand(w, c) || std::find(avoid.begin(), avoid.end(), c) != avoid.end()) continue;
		const double d = std::fabs(w.geo->angle(from, c) - ang);
		if (d < bd) { bd = d; best = c; }
	}
	return best;
}

int addCulture(World& w, int parent = -1, BirthReason why = BirthReason::Awaken) {
	const Culture* c = society::newCulture(w, { parent, -1, why });
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

void quietAwe(World& w) {
	w.tune<SocietyTune>().hoardDrift = 0;
	w.tune<SocietyTune>().aweCapAge.fill(0);
}

struct Base {
	std::unique_ptr<World> w;
	int cid = -1, cap = -1, capCell = -1;
};
Base base(int phase = 1, uint32_t seed = 1) {
	Base b;
	b.w = makeWorld({ .L = 5, .seed = seed });
	quietAwe(*b.w);
	b.w->tune<MythosTune>().phase = phase;
	b.cid = addCulture(*b.w);
	b.capCell = bestHab(*b.w);
	b.cap = addTown(*b.w, b.capCell, b.cid, 500, 0.5, 0);
	return b;
}

// Store Factors the way Mythos does (phase >= 1 so that they are read).
Factors& factorsOf(World& w, int cid) {
	w.tune<MythosTune>().phase = std::max(1, w.tune<MythosTune>().phase);
	if (w.mythos.factors.size() < w.cultures.size()) w.mythos.factors.resize(w.cultures.size());
	return w.mythos.factors[size_t(cid)];
}
bool readsStored(const World& w, int cid) {
	return cid >= 0 && size_t(cid) < w.mythos.factors.size() && mythos::factors(w, cid) == w.mythos.factors[size_t(cid)];
}
Faith& faithOf(World& w, int cid) {
	if (w.mythos.faith.size() < w.cultures.size()) w.mythos.faith.resize(w.cultures.size());
	return w.mythos.faith[size_t(cid)];
}

void years(World& w, int n, const std::function<void()>& each = nullptr) {
	for (int k = 0; k < n; k++) {
		w.year += 1;
		if (each) each();
		society::yearly(w);
	}
}

double var(const World& w, const std::string& k) {
	const auto it = w.vars.num.find(k);
	return it == w.vars.num.end() ? 0.0 : it->second;
}
std::string key(const char* what, int id) { return std::string("society_") + what + "_" + std::to_string(id); }

void setWet(World& w, const Settlement& s, double wet) {
	if (w.wet.size() != size_t(w.n)) return;
	w.wet[size_t(s.cell)] = float(wet);
	for (const int32_t c : s.cells) w.wet[size_t(c)] = float(wet);
}

} // namespace

// ================================================================ traits (3.5)

DM_TEST("society v4: compassion, ambition, courage: 0.5 at phase 0, forked rolls at phase 1, the sim stream untouched") {
	auto a = makeWorld({ .L = 3, .seed = 5 });
	auto b = makeWorld({ .L = 3, .seed = 5 });
	b->tune<MythosTune>().phase = 1;
	for (int k = 0; k < 3; k++) { addCulture(*a); addCulture(*b); }
	ok(a->rng.s == b->rng.s, "w.rng advanced exactly as at phase 0");
	for (size_t k = 0; k < 3; k++) {
		const Traits& ta = a->cultures[k].traits;
		const Traits& tb = b->cultures[k].traits;
		eq(ta.compassion, 0.5, "phase 0: compassion 0.5");
		eq(ta.ambition, 0.5, "phase 0: ambition 0.5");
		eq(ta.courage, 0.5, "phase 0: courage 0.5");
		ok(ta.piety == tb.piety && ta.aggression == tb.aggression && ta.curiosity == tb.curiosity && ta.seafaring == tb.seafaring,
			"the four old traits are the same draws");
		for (const double x : { tb.compassion, tb.ambition, tb.courage }) ok(x >= 0.05 && x <= 0.95, "rolled in 0.05..0.95");
	}
	ok(state::extensionsDefault(*a), "phase 0 leaves every v4 field at its default");
	// many roots: centred on 0.5 with the spread of the others; deterministic per culture id
	auto many = makeWorld({ .L = 3, .seed = 6 });
	many->tune<MythosTune>().phase = 1;
	double sum = 0, lo = 1, hi = 0;
	for (int k = 0; k < 60; k++) {
		const int id = addCulture(*many);
		if (id < 0) break;
		const double c = many->cultures[size_t(id)].traits.courage;
		sum += c; lo = std::min(lo, c); hi = std::max(hi, c);
	}
	std::printf("    60 roots: courage mean %.3f, range %.2f..%.2f\n", sum / 60, lo, hi);
	near(sum / 60, 0.5, 0.06, "centred on 0.5");
	ok(hi - lo > 0.3, "spread like the others");
	// daughters drift from their mother
	const int mom = 0;
	many->cultures[size_t(mom)].traits.compassion = 0.9;
	double dsum = 0;
	for (int k = 0; k < 20; k++) {
		const int d = addCulture(*many, mom, BirthReason::Schism);
		if (d < 0) break;
		dsum += many->cultures[size_t(d)].traits.compassion;
		ok(std::fabs(many->cultures[size_t(d)].traits.compassion - 0.9) < 5 * many->tune<SocietyTune>().schismDrift + 0.06, "near the mother");
	}
	ok(dsum / 20 > 0.7, "daughters of a compassionate people are compassionate (mean " + std::to_string(dsum / 20) + ")");
}

DM_TEST("society v4: shapeTraits sets the five shaped traits; later peoples inherit the shaping") {
	Base b = base(1);
	World& w = *b.w;
	Traits t;
	t.curiosity = 0.8; t.compassion = 0.25; t.ambition = 0.5; t.courage = 1.4; t.aggression = -0.2;
	t.piety = 0.99; t.seafaring = 0.01;
	const Traits before = w.cultures[0].traits;
	ok(!society::shapeTraits(w, -1, t) && !society::shapeTraits(w, 9, t), "bad culture");
	Traits bad = t;
	bad.ambition = std::nan("");
	ok(!society::shapeTraits(w, b.cid, bad), "a NaN trait is refused");
	ok(w.cultures[0].traits == before, "a refusal changes nothing");
	ok(society::shapeTraits(w, b.cid, t), "shaped");
	const Traits& s = w.cultures[0].traits;
	eq(s.curiosity, 0.8, "curiosity");
	eq(s.compassion, 0.25, "compassion");
	eq(s.ambition, 0.5, "ambition");
	eq(s.courage, 1.0, "clamped to 1");
	eq(s.aggression, 0.0, "clamped to 0");
	eq(s.piety, before.piety, "piety is not shaped");
	eq(s.seafaring, before.seafaring, "seafaring is not shaped");
	// the second band and later awakenings inherit the choice (+- traitDrift)
	double sc = 0, sp = 0, sg = 0;
	const int n = 30;
	for (int k = 0; k < n; k++) {
		const int id = addCulture(w);
		if (!ok(id >= 0, "culture")) return;
		sc += w.cultures[size_t(id)].traits.curiosity;
		sp += w.cultures[size_t(id)].traits.compassion;
		sg += w.cultures[size_t(id)].traits.courage;
	}
	std::printf("    later roots: curiosity %.3f, compassion %.3f, courage %.3f\n", sc / n, sp / n, sg / n);
	near(sc / n, 0.8, 0.06, "curiosity inherited");
	near(sp / n, 0.25, 0.06, "compassion inherited");
	near(sg / n, 0.93, 0.06, "courage inherited (clamped at 0.95)");
	// a dead people cannot be shaped
	w.cultures[1].dead = true;
	ok(!society::shapeTraits(w, 1, t), "dead culture");
}

// ================================================================ nudge

DM_TEST("society v4: nudge moves devotion and love, clamped, never NaN") {
	Base b = base(1);
	World& w = *b.w;
	Settlement& s = w.settlements[size_t(b.cap)];
	s.devotion = 0.5; s.love = 0.1;
	society::nudge(w, b.cap, 0.1, -0.2);
	near(w.settlements[size_t(b.cap)].devotion, 0.6, 1e-12, "devotion +0.1");
	near(w.settlements[size_t(b.cap)].love, -0.1, 1e-12, "love -0.2");
	society::nudge(w, b.cap, 5, -5);
	eq(w.settlements[size_t(b.cap)].devotion, 1.0, "clamped at 1");
	eq(w.settlements[size_t(b.cap)].love, -1.0, "clamped at -1");
	society::nudge(w, b.cap, std::nan(""), INFINITY);
	eq(w.settlements[size_t(b.cap)].devotion, 1.0, "NaN ignored");
	eq(w.settlements[size_t(b.cap)].love, -1.0, "infinity ignored");
	society::nudge(w, -1, 0.1, 0.1);
	society::nudge(w, 99, 0.1, 0.1);
	w.settlements[size_t(b.cap)].dead = true;
	society::nudge(w, b.cap, -0.5, 0.5);
	eq(w.settlements[size_t(b.cap)].devotion, 1.0, "the dead are not nudged");
	noNaN(w);
}

// ================================================================ Factors in belief

DM_TEST("society v4: devotionBase and loveTarget move belief exactly; phase 0 ignores stored records") {
	auto run = [](int phase, double devBase, double loveT) {
		Base b = base(phase);
		World& w = *b.w;
		w.year = 10;
		if (devBase != 0 || loveT != 0) {
			w.mythos.factors.assign(w.cultures.size(), Factors{});
			w.mythos.factors[0].devotionBase = devBase;
			w.mythos.factors[0].loveTarget = loveT;
		}
		w.settlements[size_t(b.cap)].devotion = 0.3;
		w.settlements[size_t(b.cap)].love = 0.1;
		years(w, 1);
		return std::make_pair(w.settlements[size_t(b.cap)].devotion, w.settlements[size_t(b.cap)].love);
	};
	const auto plain = run(1, 0, 0);
	const auto moved = run(1, 0.1, 0.3);
	const auto phase0 = run(0, 0.1, 0.3);
	const SocietyTune T{};
	near(moved.first - plain.first, T.devotionDrift * 0.1, 1e-12, "devotion: + drift x devotionBase");
	near(plain.second, 0.1 * (1 - T.loveDrift), 1e-15, "love drifts toward 0 (v3.1)");
	near(moved.second, 0.1 + T.loveDrift * (0.3 - 0.1), 1e-12, "love drifts toward the target");
	ok(phase0 == run(0, 0, 0), "phase 0: the records are not read");
}

DM_TEST("society v4: the prayer chance: above 1 lowers the gate, below 1 a forked roll, 0 silences; expect by kind") {
	auto prays = [](double chance, double rainExpect, double devotion, int towns, uint32_t seed = 1) {
		auto w = makeWorld({ .L = 5, .seed = seed });
		quietAwe(*w);
		w->tune<SocietyTune>().maxPrayers = 1000;
		const int cid = addCulture(*w);
		Factors& f = factorsOf(*w, cid);
		f.prayerChance = chance;
		f.expect[size_t(PrayerKind::Rain)] = rainExpect;
		const int home = bestHab(*w);
		std::vector<int> used;
		for (int k = 0; k < towns; k++) {
			const int cell = landAt(*w, home, 0.02 * k, used);
			used.push_back(cell);
			const int sid = addTown(*w, cell, cid, 300, devotion);
			w->settlements[size_t(sid)].famine = 3;
		}
		w->year = 50;
		society::yearly(*w);
		int n = 0;
		for (const Settlement& s : w->settlements) n += s.prayer && s.prayer->kind == PrayerKind::Rain ? 1 : 0;
		return n;
	};
	{
		auto probe = makeWorld({ .L = 3 });
		addCulture(*probe);
		factorsOf(*probe, 0).prayerChance = 2;
		if (!readsStored(*probe, 0)) return;
	}
	// devotion 0.1 drifts to ~0.108 in belief(): below 0.15, above 0.15 / 2
	eq(prays(1, 1, 0.1, 1), 0, "chance 1: devotion below the gate, no prayer (v3.1)");
	eq(prays(2, 1, 0.1, 1), 1, "chance 2: the gate halves (0.075)");
	eq(prays(1, 2, 0.1, 1), 1, "expect[rain] 2 lowers it the same way");
	eq(prays(0, 1, 0.9, 5), 0, "chance 0: nobody asks");
	eq(prays(1, 0, 0.9, 5), 0, "expect[rain] 0: no rain prayers");
	const int half = prays(0.5, 1, 0.6, 80);
	std::printf("    chance 0.5: %d of 80 towns pray\n", half);
	ok(half >= 25 && half <= 55, "about half pray");
	eq(prays(0.5, 1, 0.6, 80), half, "the roll is a fork: the same world prays the same");
	eq(prays(1, 1, 0.6, 20), 20, "chance 1: every hungry town prays");
}

DM_TEST("society v4: unansweredPenalty and answeredGain") {
	// silence: the unanswered penalty x the factor
	auto silence = [](double pen) {
		Base b = base(1);
		World& w = *b.w;
		factorsOf(w, b.cid).unansweredPenalty = pen;
		w.year = 40;
		Settlement& s = w.settlements[size_t(b.cap)];
		s.famine = 2;
		s.prayer = Prayer{ PrayerKind::Rain, 24 };
		years(w, 1);
		return std::make_pair(w.settlements[size_t(b.cap)].devotion, w.settlements[size_t(b.cap)].love);
	};
	const auto p1 = silence(1), p2 = silence(2);
	near(p1.first - p2.first, 0.05, 1e-12, "devotion: twice the penalty");
	near(p1.second - p2.second, 0.05, 1e-12, "love: twice the penalty");
	// an answered prayer: faith x answeredGain, and its lesson x answeredGain
	stub::Guard guard(true);
	auto answered = [](double gain, double* sci) {
		Base b = base(1);
		World& w = *b.w;
		factorsOf(w, b.cid).answeredGain = gain;
		Settlement& s = w.settlements[size_t(b.cap)];
		s.devotion = 0.3; s.jaded = 0; s.plague = 0.5;
		s.prayer = Prayer{ PrayerKind::Heal, 0 };
		if (stub::stubbed("economy")) stub::hooks().civ_addScience = [sci](World&, int, double amount) { *sci = amount; return amount; };
		society::WitnessOpts o;
		o.kind = "heal"; o.faith = 0.1; o.good = 0.5;     // a click act: no brush scaling
		society::witness(w, s.cell, 0.1, o);
		return w.settlements[size_t(b.cap)].devotion - 0.3;
	};
	double sci1 = 0, sci2 = 0;
	const double g1 = answered(1, &sci1), g2 = answered(1.5, &sci2);
	near(g2 / g1, 1.5, 1e-9, "devotion gain x 1.5");
	if (stub::stubbed("economy")) {
		near(sci1, SocietyTune{}.prayerScience, 1e-12, "the lesson (v3.1)");
		near(sci2, 1.5 * SocietyTune{}.prayerScience, 1e-12, "the lesson x 1.5");
	}
}

DM_TEST("society v4: Awe: each people's weight in S, and the archetype's income") {
	Base b = base(1);
	World& w = *b.w;
	const int other = addCulture(w);
	const int t2 = addTown(w, landAt(w, b.capCell, 0.3), other, 800, 0.6);
	w.settlements[size_t(t2)].tier = 2;
	w.settlements[size_t(b.cap)].b[Building::SHRINE] = 1;
	const SocietyTune T{};
	auto sOf = [&](const Settlement& s) {
		const double weight = T.sCamp + (s.has(Building::SHRINE) ? T.sShrine : 0) + (s.has(Building::TEMPLE) ? T.sTemple : 0);
		return s.devotion * weight * (1 + s.tier / 2.0);
	};
	const double s0 = sOf(w.settlements[size_t(b.cap)]), s1 = sOf(w.settlements[size_t(t2)]);
	near(society::aweIncome(w), T.incomeBase + T.incomeK * std::log(1 + s0 + s1), 1e-12, "neutral (v3.1)");
	factorsOf(w, b.cid).aweS = 2;
	factorsOf(w, other).aweS = 0.5;
	if (!readsStored(w, b.cid)) return;
	near(society::aweIncome(w), T.incomeBase + T.incomeK * std::log(1 + 2 * s0 + 0.5 * s1), 1e-12, "x each culture's aweS");
	w.mythos.factors.clear();
	w.settings.archetype = Archetype::Dangerous;
	const double k = mythos::archetypeParams(w).aweIncome;
	near(society::aweIncome(w), (T.incomeBase + T.incomeK * std::log(1 + s0 + s1)) * k, 1e-12, "x the archetype's aweIncome");
	if (stub::stubbed("mythos")) near(k, 1.1, 1e-12, "Dangerous: they pray harder (x 1.1)");
	w.tune<MythosTune>().phase = 0;
	near(society::aweIncome(w), T.incomeBase + T.incomeK * std::log(1 + s0 + s1), 1e-12, "phase 0: no archetype term");
}

DM_TEST("society v4: the schism pace (Factors schism)") {
	auto bornAfter = [](double pace) -> int {
		auto w = makeWorld({ .L = 5, .seed = 1 });
		quietAwe(*w);
		const int cid = addCulture(*w);
		w->cultures[size_t(cid)].age = Age::IRON;
		w->cultures[size_t(cid)].lastSplit = -1000;
		const int home = bestHab(*w);
		addTown(*w, home, cid, 900);
		const int far1 = landAt(*w, home, 0.8), far2 = landAt(*w, far1, 0.04, { far1 });
		if (w->geo->angle(home, far2) < 0.6) { fail("sites"); return -2; }
		addTown(*w, far1, cid, 300);
		addTown(*w, far2, cid, 300);
		if (pace != 1) factorsOf(*w, cid).schism = pace;
		else factorsOf(*w, cid);
		for (int y = 1; y <= 200; y++) {
			w->year = y;
			for (Culture& c : w->cultures) c.age = Age::IRON;
			const auto evs = capture(*w, [&] { society::yearly(*w); });
			if (!eventsOf<EvCultureBorn>(evs).empty()) return y;
		}
		return -1;
	};
	const int plain = bornAfter(1), fast = bornAfter(2), never = bornAfter(0);
	std::printf("    a far province secedes in year %d (neutral), %d (schism x 2), %d (schism 0)\n", plain, fast, never);
	ok(plain >= 119 && plain <= 122, "neutral: after apartYears (120)");
	ok(fast >= 59 && fast <= 62, "x 2: after 60 years");
	eq(never, -1, "0: no secession");
}

// ================================================================ presence (3.2)

DM_TEST("society v4: presence reaches witness through witnessScale (faith, jaded, memory, favour)") {
	stub::Guard guard(true);
	const WitnessScale unseen{ 0.25, 0.4, 0, 0 };
	auto act = [&](bool scaled, bool natural) {
		Base b = base(1);
		World& w = *b.w;
		if (scaled) {
			if (stub::stubbed("mythos")) stub::hooks().mythos_witnessScale = [&](const World&, int, std::string_view) { return unseen; };
			else w.mythos.presence = Presence::Unseen;
		}
		Settlement& s = w.settlements[size_t(b.cap)];
		s.devotion = 0.3; s.jaded = 0.1; s.love = 0;
		society::WitnessOpts o;
		o.kind = "heal"; o.faith = 0.1; o.good = 0.5; o.natural = natural;
		const WitnessScale ws = mythos::witnessScale(w, s.cell, o.kind);
		society::witness(w, s.cell, 0.1, o);
		struct R { double dev, jaded, grace, fgrace; WitnessScale ws; };
		return R{ w.settlements[size_t(b.cap)].devotion - 0.3, w.settlements[size_t(b.cap)].jaded - 0.1,
			var(w, key("grace", b.cid)), var(w, key("fgrace", b.cid)), ws };
	};
	const auto plain = act(false, false), scaled = act(true, false), nat = act(true, true);
	if (scaled.ws == WitnessScale{}) return;                 // a real Mythos without a stance effect
	near(scaled.dev, plain.dev * scaled.ws.faith, 1e-12, "devotion gain x faith");
	near(scaled.jaded, plain.jaded * scaled.ws.faith * scaled.ws.jaded, 1e-12, "jaded x faith x jaded");
	near(scaled.grace, plain.grace * scaled.ws.memory, 1e-12, "long memory x memory");
	near(scaled.fgrace, plain.fgrace * scaled.ws.favour, 1e-12, "favour sums x favour (Unseen: no envy)");
	ok(nat.dev == act(false, true).dev, "a natural event is never scaled");
	std::printf("    unseen heal: devotion +%.4f (wonders +%.4f), grace %.4f (%.4f)\n", scaled.dev, plain.dev, scaled.grace, plain.grace);
}

// ================================================================ aspect and courage (3.3, 3.5)

DM_TEST("society v4: a Threat people fears whatever its love, softened by courage, only where mythos acts") {
	auto devAfter = [](bool threat, bool emergent, bool acted, double courage, double love) {
		Base b = base(1);
		World& w = *b.w;
		w.tune<MythosTune>().emergent = emergent;
		w.cultures[size_t(b.cid)].traits.courage = courage;
		w.cultures[size_t(b.cid)].age = Age::BRONZE;
		if (threat) faithOf(w, b.cid).aspect = Aspect::Threat;
		if (acted) factorsOf(w, b.cid).research = 1.0001;   // some mythos act on them (any non-neutral record)
		Settlement& s = w.settlements[size_t(b.cap)];
		s.devotion = 0.3; s.love = love;
		w.year = 10;
		society::yearly(w);
		return w.settlements[size_t(b.cap)].devotion;
	};
	const SocietyTune T{};
	const double plain = devAfter(false, false, false, 0.5, 0);
	near(devAfter(true, false, false, 0.5, 0), plain, 1e-15, "emergent off, no act: the aspect changes nothing (5.1 rule 2)");
	near(devAfter(true, true, false, 0.6, 0) - plain, T.devotionDrift * T.fearBonus * (1 - 0.5 * 0.6), 1e-12, "Threat: fear's bonus x (1 - 0.5 courage)");
	near(devAfter(true, false, true, 0, 0) - plain, T.devotionDrift * T.fearBonus, 1e-12, "a god who acted on them: the rule applies (courage 0: full bonus)");
	// the v3 fear rule (love < -0.3) keeps working, softened by courage once mythos acts
	const double fear0 = devAfter(false, false, false, 1.0, -0.5), fearPlain = devAfter(false, false, false, 0.5, -0.5);
	near(fear0, fearPlain, 1e-15, "the v3 rule unchanged where mythos does not act");
	const double brave = devAfter(false, true, false, 1.0, -0.5);
	near(fearPlain - brave, T.devotionDrift * T.fearBonus * 0.5, 1e-9, "courage 1 halves it");
}

// ================================================================ Victory and Sun (3.10)

namespace {
struct War {
	std::unique_ptr<World> w;
	int a = -1, b = -1, c = -1, capA = -1, capB = -1, capC = -1;
};
War war(int phase, bool emergent) {
	War r;
	r.w = makeWorld({ .L = 5, .seed = 1 });
	World& w = *r.w;
	quietAwe(w);
	w.tune<MythosTune>().phase = phase;
	w.tune<MythosTune>().emergent = emergent;
	r.a = addCulture(w); r.b = addCulture(w); r.c = addCulture(w);
	const int home = bestHab(w);
	r.capA = addTown(w, home, r.a, 800, 0.5);
	r.capB = addTown(w, landAt(w, home, 0.4), r.b, 800, 0.5);
	r.capC = addTown(w, landAt(w, home, 0.8), r.c, 800, 0.5);
	society::rel(w, r.a, r.b)->war = true;
	w.year = 30;
	return r;
}
} // namespace

DM_TEST("society v4: Victory and Sun prayers start only with mythos on and emergent") {
	for (const auto& gate : { std::make_pair(0, true), std::make_pair(1, false) }) {
		War r = war(gate.first, gate.second);
		years(*r.w, 3);
		for (const Settlement& s : r.w->settlements) ok(!s.prayer || (s.prayer->kind != PrayerKind::Victory && s.prayer->kind != PrayerKind::Sun), "no new prayers when gated");
	}
	War r = war(1, true);
	World& w = *r.w;
	std::vector<Event> evs = capture(w, [&] { years(w, 1); });
	ok(w.settlements[size_t(r.capA)].prayer && w.settlements[size_t(r.capA)].prayer->kind == PrayerKind::Victory, "A's capital prays for victory");
	ok(w.settlements[size_t(r.capB)].prayer && w.settlements[size_t(r.capB)].prayer->kind == PrayerKind::Victory, "and so does B's");
	ok(!w.settlements[size_t(r.capC)].prayer, "C is at peace");
	int starts = 0;
	for (const EvPrayer& p : eventsOf<EvPrayer>(evs)) if (p.kind == PrayerKind::Victory && p.start) starts++;
	eq(starts, 2, "two prayer events");
	// a weary people prays for peace instead (exhaustion >= 0.4)
	{
		War q = war(1, true);
		q.w->cultures[size_t(q.a)].exhaustion = 0.5;
		years(*q.w, 1);
		const auto& pr = q.w->settlements[size_t(q.capA)].prayer;
		ok(pr && pr->kind == PrayerKind::Peace, "weary: Peace, not Victory");
	}
	// peace ends the prayer
	society::rel(w, r.a, r.b)->war = false;
	evs = capture(w, [&] { years(w, 1); });
	ok(!w.settlements[size_t(r.capA)].prayer, "peace: the prayer ends");
	// Sun: a sodden territory for sunYears (3) years
	Settlement& sc = w.settlements[size_t(r.capC)];
	setWet(w, sc, 0.85);
	years(w, 2);
	ok(!w.settlements[size_t(r.capC)].prayer, "two wet years: not yet");
	years(w, 1);
	ok(w.settlements[size_t(r.capC)].prayer && w.settlements[size_t(r.capC)].prayer->kind == PrayerKind::Sun, "the third: they pray for the rain to stop");
	setWet(w, w.settlements[size_t(r.capC)], 0.3);
	years(w, 1);
	ok(!w.settlements[size_t(r.capC)].prayer, "dry again: it ends");
	// a storm over the town: at once
	Disaster d;
	d.id = 9; d.type = DisasterType::Storm; d.cell = w.settlements[size_t(r.capC)].cell; d.ang = 0.1; d.dur = 5;
	w.disasters.push_back(d);
	years(w, 1);
	ok(w.settlements[size_t(r.capC)].prayer && w.settlements[size_t(r.capC)].prayer->kind == PrayerKind::Sun, "a storm over it: Sun at once");
	// sunYears 0 switches Sun off
	w.disasters.clear();
	w.settlements[size_t(r.capC)].prayer.reset();
	w.tune<SocietyTune>().sunYears = 0;
	w.disasters.push_back(d);
	years(w, 1);
	ok(!w.settlements[size_t(r.capC)].prayer, "sunYears 0: no Sun prayers");
	noNaN(w);
}

DM_TEST("society v4: Victory is answered by the god's wrath on the enemy, and only the enemy") {
	auto strike = [](int target, bool natural, double good, int& answeredCount) {
		War r = war(1, true);
		World& w = *r.w;
		w.settlements[size_t(r.capA)].prayer = Prayer{ PrayerKind::Victory, 29 };
		w.settlements[size_t(r.capA)].devotion = 0.4;
		const int sid = target == 0 ? r.capB : r.capC;
		society::WitnessOpts o;
		o.kind = "lightning"; o.faith = 0.05; o.good = good; o.natural = natural;
		if (good < 0) { o.harmed = { sid }; o.goodHarmed = -0.8; }
		const auto evs = capture(w, [&] { society::witness(w, w.settlements[size_t(sid)].cell, 0.08, o); });
		answeredCount = int(var(w, key("answer", r.a)));
		bool ans = false;
		for (const EvPrayerAnswered& e : eventsOf<EvPrayerAnswered>(evs)) if (e.id == r.capA && e.kind == PrayerKind::Victory) ans = true;
		return std::make_pair(ans, w.settlements[size_t(r.capA)].devotion);
	};
	int n = 0;
	const auto onEnemy = strike(0, false, -0.2, n);
	ok(onEnemy.first, "lightning on the enemy answers it");
	eq(n, 1, "counted as answered");
	ok(onEnemy.second > 0.4, "they are grateful (devotion +)");
	ok(!strike(1, false, -0.2, n).first, "lightning on a people they are not at war with: no");
	ok(!strike(0, true, -0.2, n).first, "a natural disaster on the enemy: no (not the god's)");
	ok(!strike(0, false, 0.5, n).first, "a kindness to the enemy: no");
}

DM_TEST("society v4: Raise Land answers a Sun prayer over a flooded town") {
	War r = war(1, true);
	World& w = *r.w;
	Settlement& s = w.settlements[size_t(r.capC)];
	s.prayer = Prayer{ PrayerKind::Sun, 29 };
	society::WitnessOpts o;
	o.kind = "raise"; o.faith = 0.06; o.good = 0.2;
	setWet(w, s, 0.2);
	society::witness(w, s.cell, 0.05, o);
	ok(w.settlements[size_t(r.capC)].prayer.has_value(), "dry ground: raising it answers nothing");
	setWet(w, w.settlements[size_t(r.capC)], 0.97);
	const auto evs = capture(w, [&] { society::witness(w, w.settlements[size_t(r.capC)].cell, 0.05, o); });
	ok(!w.settlements[size_t(r.capC)].prayer, "flooded: Raise Land answers it");
	eq(int(eventsOf<EvPrayerAnswered>(evs).size()), 1, "prayer:answered");
}

// ================================================================ answerPrayer, remember (owner APIs)

DM_TEST("society v4: answerPrayer answers at a scale; remember adds to the long memory") {
	Base b = base(1);
	World& w = *b.w;
	w.year = 50;
	Settlement& s = w.settlements[size_t(b.cap)];
	s.devotion = 0.3; s.love = 0; s.famine = 1;
	ok(!society::answerPrayer(w, b.cap, 1), "not praying");
	s.prayer = Prayer{ PrayerKind::Rain, 45 };
	ok(!society::answerPrayer(w, b.cap, std::nan("")) && !society::answerPrayer(w, b.cap, 0) && !society::answerPrayer(w, -1, 1), "bad input");
	const auto evs = capture(w, [&] { ok(society::answerPrayer(w, b.cap, 0.5), "answered at half"); });
	const Settlement& t = w.settlements[size_t(b.cap)];
	ok(!t.prayer, "the prayer is cleared");
	eq(int(eventsOf<EvPrayerAnswered>(evs).size()), 1, "prayer:answered");
	near(t.blessed, SocietyTune{}.prayerBlessed * 0.5, 1e-12, "rain blesses the town for half the years");
	ok(t.devotion > 0.3 && t.love > 0, "devotion and love rise");
	near(var(w, key("answer", b.cid)), 1, 0, "counted");
	ok(var(w, key("grace", b.cid)) > 0, "remembered as grace");
	near(var(w, key("heard", b.cid)), 50, 0, "heard this year");
	// remember: a deed without a place (a pact the god broke, a renewal)
	const double g0 = var(w, key("grace", b.cid)), w0 = var(w, key("wrath", b.cid)), f0 = var(w, key("fwrath", b.cid));
	w.year = 60;
	society::remember(w, b.cid, 0, 0.5);
	near(var(w, key("wrath", b.cid)) - w0, 0.5, 1e-12, "wrath +0.5");
	near(var(w, key("fwrath", b.cid)) - f0, 0.5, 1e-12, "the neighbours notice too");
	near(var(w, key("grace", b.cid)), g0, 0, "grace unchanged");
	near(var(w, key("heard", b.cid)), 60, 0, "heard");
	society::remember(w, b.cid, -1, std::nan(""));
	society::remember(w, 77, 1, 1);
	near(var(w, key("wrath", b.cid)) - w0, 0.5, 1e-12, "negative, NaN and bad ids ignored");
	// while the culture builds its Ark nothing new enters the memory (v3 rule)
	w.settlements[size_t(b.cap)].ark = 0.3;
	society::remember(w, b.cid, 1, 0);
	near(var(w, key("grace", b.cid)), g0, 0, "building the Ark: no memory");
	ok(var(w, key("fgrace", b.cid)) >= 1, "but favour, yes");
	noNaN(w);
}

// ================================================================ messengers (3.8)

DM_TEST("society v4: a Child messenger lives childYears, a Shepherd travels every shepherdTravel years") {
	auto raise = [](int archetype) {
		Base b = base(1);
		World& w = *b.w;
		w.year = 100;
		int fid = -1;
		if (archetype >= 0) {
			Figure f;
			f.id = 5; f.kind = uint8_t(FigureKind::Messenger); f.archetype = uint8_t(archetype); f.culture = b.cid; f.home = b.cap; f.name = "Ossa";
			f.credibility = 0.5;
			w.mythos.figures.push_back(f);
			w.mythos.nextId = 6;
			fid = 5;
		}
		const Prophet* p = society::raiseProphet(w, b.cap, { fid });
		if (!p) return std::make_tuple(-1.0, -1.0, -1);
		return std::make_tuple(p->until - 100, var(w, key("prophetNext", b.cid)) - 100, p->figure);
	};
	const SocietyTune T{};
	const CharactersTune C{};
	const auto plain = raise(-1), child = raise(int(MessengerArchetype::Child)), shepherd = raise(int(MessengerArchetype::Shepherd));
	ok(std::get<0>(plain) >= T.prophetYears * (1 - T.prophetJitter) - 1e-9 && std::get<0>(plain) <= T.prophetYears * (1 + T.prophetJitter) + 1e-9, "unnamed: 60 years +- 15 %");
	ok(std::get<0>(child) >= C.childYears * (1 - T.prophetJitter) - 1e-9 && std::get<0>(child) <= C.childYears * (1 + T.prophetJitter) + 1e-9, "a Child: 80 years +- 15 %");
	near(std::get<0>(child) / C.childYears, std::get<0>(plain) / T.prophetYears, 1e-12, "the same draw");
	near(std::get<1>(plain), T.prophetTravel, 0, "unnamed: travels every 10 years");
	near(std::get<1>(child), T.prophetTravel, 0, "a Child: every 10");
	near(std::get<1>(shepherd), C.shepherdTravel, 0, "a Shepherd: every 7");
	eq(std::get<2>(shepherd), 5, "the figure id is kept");
}

// ================================================================ robustness

DM_TEST("society v4: non-finite Factors never poison belief, prayers, Awe or schisms") {
	Base b = base(1);
	World& w = *b.w;
	w.tune<MythosTune>().emergent = true;
	const int far = addTown(w, landAt(w, b.capCell, 0.3), b.cid, 400, 0.2, -0.5);
	w.settlements[size_t(far)].famine = 3;
	const double nan = std::nan("");
	Factors& f = factorsOf(w, b.cid);
	f.devotionBase = nan; f.loveTarget = INFINITY; f.prayerChance = nan; f.answeredGain = nan; f.unansweredPenalty = -INFINITY;
	f.aweS = nan; f.schism = nan; f.expect.fill(nan);
	faithOf(w, b.cid).aspect = Aspect::Threat;
	years(w, 40, [&] {
		society::WitnessOpts o; o.kind = "rain"; o.faith = 0.1; o.good = 0.5;
		society::witness(w, b.capCell, 0.2, o);
	});
	ok(std::isfinite(society::aweIncome(w)), "Awe income finite");
	w.mythos.factors.clear();
	w.mythos.faith.clear();
	noNaN(w, "after 40 years of bad records");
}
