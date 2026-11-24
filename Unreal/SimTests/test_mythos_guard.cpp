// DEMIURGE — SimTests/test_mythos_guard.cpp : the mythos core's guardrails (SPEC-MYTHOS 5.1 rules 1, 2, 5, 6):
// phase 0 does nothing; phase 1 with emergent off leaves a real history unchanged for a god who ignores every card
// and Word power; determinism and save / load between two yearly passes; the cost of mythos::yearly (own share).
// Track M1 "mythos-core". Key: run.ps1 mythos; the real-history test runs only in all (it needs the real modules).
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace dm;
using namespace dmt;

namespace {

std::unique_ptr<World> mythWorld(int L, int cultures, int towns, uint32_t seed = 9) {
	auto w = makeWorld({ .L = L, .seed = seed });
	w->tune<MythosTune>().phase = 1;
	std::vector<int> land;
	for (int c = 0; c < w->n; c++) if (w->elev[size_t(c)] >= w->seaLevel) land.push_back(c);
	const size_t stride = land.size() / size_t(cultures * towns + 1);
	int k = 1;
	for (int c = 0; c < cultures; c++) {
		state::newCulture(*w);
		for (int t = 0; t < towns; t++, k++) {
			Settlement& s = state::newSettlement(*w, land[size_t(k) * stride], c);
			s.pop = 50 + 10 * t; s.devotion = 0.4 + 0.01 * t; s.tier = 2;
			if (t % 5 == 0) s.b[Building::ACADEMY] = 1;
			if (t == 0) { s.capital = true; w->cultures[size_t(c)].capital = s.id; }
		}
	}
	w->mythos.decrees.push_back(Decree{ 0, DecreeArea::Care, int8_t(CareStance::Shelter), Clarity::Parable, 0, 0.8, -1, false, {} });
	mythos::attach(*w);
	mythos::rebuild(*w);
	return w;
}

// One year of a scripted god: acts of every kind Mythos records.
void script(World& w, int y) {
	const int ns = int(w.settlements.size());
	for (int i = 0; i < 6; i++) {
		const Settlement& s = w.settlements[size_t((y * 7 + i * 13) % ns)];
		const bool wrath = (y + i) % 4 == 0;
		w.bus.emit(EvMiracleWitnessed{ s.cell, wrath ? "lightning" : "heal", wrath ? -1.0 : 0.8, { s.id } });
	}
	const Settlement& p = w.settlements[size_t((y * 3) % ns)];
	if (y % 3 == 0) w.bus.emit(EvPrayerAnswered{ p.id, PrayerKind::Rain });
	if (y % 5 == 0) w.bus.emit(EvBreach{ p.culture, "care", p.id, -1, 0.2 });
	if (y % 11 == 0) w.bus.emit(EvWord{ y, p.culture, "decree", y % 2 ? "parable" : "sign", "messenger", "heard" });
	if (y % 17 == 0) w.bus.emit(EvJudgement{ 3, p.culture, y % 2 ? "heart" : "deed", y % 3 ? "forgive" : "warn" });
}

std::vector<uint8_t> v1Bytes(const World& w) {
	std::vector<uint8_t> b;
	state::serialize(w, b, nullptr, 1);
	return b;
}

} // namespace

DM_TEST("mythos guard: phase 0 subscribes nothing, records nothing, changes nothing") {
	auto w = mythWorld(4, 2, 4);
	w->tune<MythosTune>().phase = 0;
	w->mythos = MythosState{};
	mythos::attach(*w);                      // drops the phase-1 subscriptions
	const int subs = w->bus.count();
	mythos::attach(*w);
	eq(w->bus.count(), subs, "no subscription at phase 0");
	const uint32_t h0 = hash(*w);
	for (int y = 0; y < 30; y++) { script(*w, y); w->year += 1; mythos::yearly(*w); mythos::rebuild(*w); }
	w->year -= 30;
	eq(hash(*w), h0, "the world is unchanged");
	ok(state::extensionsDefault(*w), "every extension default");
	ok(!mythos::setPresence(*w, Presence::Unseen), "no stance change");
	eq(int(mythos::legacy(*w)), int(Legacy::None), "no legacy");
}

DM_TEST("mythos guard: determinism and a save between two yearly passes replays identically") {
	auto a = mythWorld(4, 3, 4);
	auto b = mythWorld(4, 3, 4);
	for (int y = 0; y < 120; y++) {
		script(*a, y); a->year += 1; mythos::yearly(*a);
		script(*b, y); b->year += 1; mythos::yearly(*b);
	}
	eq(hash(*a), hash(*b), "two runs of the same script are identical");
	// acts pending in the save (between two yearly passes)
	script(*a, 120);
	ok(!a->mythos.acts.empty(), "acts are pending");
	std::vector<uint8_t> bytes;
	std::string why;
	ok(state::serialize(*a, bytes, &why), "saved: " + why);
	auto c = state::deserialize(bytes, &why);
	if (!ok(c != nullptr, "loaded: " + why)) return;
	c->geo = a->geo;
	c->tune<MythosTune>().phase = 1;
	mythos::attach(*c);
	mythos::rebuild(*c);
	eq(int(c->mythos.acts.size()), int(a->mythos.acts.size()), "the pending acts were saved");
	for (World* x : { a.get(), c.get() }) {
		x->year += 1; mythos::yearly(*x);
		for (int y = 121; y < 160; y++) { script(*x, y); x->year += 1; mythos::yearly(*x); }
	}
	std::vector<uint8_t> ba, bc;
	state::serialize(*a, ba); state::serialize(*c, bc);
	ok(ba == bc, "the loaded world replays to the same bytes");
	eq(checkFinite(*a), std::string(""), "finite");
}

DM_TEST("mythos guard: mythos::yearly own share at L = 6 with 120 settlements and 6 cultures") {
	auto w = mythWorld(6, 6, 20);
	const bool own = stub::stubbed("doctrine") && stub::stubbed("characters");
	for (int y = 0; y < 20; y++) { script(*w, y); w->year += 1; mythos::yearly(*w); }
	const int years = 300;
	double total = 0, worst = 0;
	for (int y = 20; y < 20 + years; y++) {
		script(*w, y);
		w->year += 1;
		const double t0 = nowMs();
		mythos::yearly(*w);
		const double dt = nowMs() - t0;
		total += dt;
		if (dt > worst) worst = dt;
	}
	const double mean = total / years;
	std::printf("    mythos::yearly %s: mean %.4f ms, worst %.4f ms (%d settlements, %d cultures)\n",
		own ? "own share" : "with the real submodules", mean, worst, int(w->settlements.size()), int(w->cultures.size()));
	if (own) {
		ok(mean <= 0.5, "own share mean <= 5 x the 0.1 ms target: " + std::to_string(mean));
		ok(worst <= 15, "any yearly <= 5 x 3 ms: " + std::to_string(worst));
	}
	noNaN(*w, "after the timing run");
}

DM_TEST("mythos guard: phase 1, emergent off: a real history is unchanged for a god who ignores the cards") {
	if (stub::stubbed("sim") || stub::stubbed("terrain") || stub::stubbed("society") || stub::stubbed("powers")) return;
	std::vector<uint8_t> ref;
	for (int phase = 0; phase < 2; phase++) {
		auto w = sim::create({ .seed = 3, .name = "", .L = 5 });
		if (!ok(w != nullptr, "world")) return;
		// (B1) the phase is set before the waking: Balance.cpp turns the mythos on, so sim::create's use attached it
		w->tune<MythosTune>().phase = phase;
		w->tune<MythosTune>().emergent = false;
		mythos::attach(*w);
		sim::genesis::skip(*w, {});
		mythos::rebuild(*w);
		for (int y = 0; y < 300; y++) {
			sim::advance(*w, sim::kTicksPerYear);
			// an ordinary kind god now and then: grace and wrath, never a card or a Word power
			if (y % 9 == 0 && !w->settlements.empty()) {
				const Settlement& s = w->settlements[size_t(y % int(w->settlements.size()))];
				if (!s.dead) { w->god.awe = std::max(w->god.awe, 500.0); powers::use(*w, y % 2 ? "heal" : "lightning", s.cell); }
			}
			mythos::yearly(*w);
		}
		const std::vector<uint8_t> b = v1Bytes(*w);
		if (phase == 0) { ref = b; ok(state::extensionsDefault(*w), "phase 0 keeps every extension default"); continue; }
		ok(!w->mythos.faith.empty(), "phase 1 keeps faith records");
		for (const Culture& c : w->cultures) ok(mythos::factors(*w, c.id) == Factors{}, "neutral Factors for culture " + std::to_string(c.id));
		ok(b == ref, "the sim (everything but the mythos extensions) is identical to phase 0");
		noNaN(*w, "after 300 years at phase 1");
	}
}
