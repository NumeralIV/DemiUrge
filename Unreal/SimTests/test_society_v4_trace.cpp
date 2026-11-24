// DEMIURGE — SimTests/test_society_v4_trace.cpp : the M5 "society-v4" acceptance trace for the belief half
// (SPEC-MYTHOS 9, track M5: "neutral Factors = identical yearly results (a recorded 300-year trace)").
//
// Three peoples on a synthetic world run society::yearly for 300 years with the god acting now and then (rain,
// bloom, heal, lightning on a town, a prophet), famines, a plague and a drought. Every year the belief-half state
// is folded into one 64-bit chain (traceHash below: devotion, love, jaded, prayers, estrangement, Awe, memory and
// favour sums, cultures, the rng state). The v4 traits (compassion, ambition, courage) are left out on purpose:
// Society rolls them at phase >= 1 from a fork, which moves no other state.
//   - run.ps1 society (every collaborator stubbed): the chain equals the value RECORDED from the pre-M5 Society.cpp
//     (v3.1 + M0) at MythosTune::phase 0, and phase 1 with neutral Factors gives the same chain.
//   - run.ps1 all: phase 0 and phase 1 with neutral Factors give the same chain (no constant: see the diplomacy
//     trace).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "Mythos.h"
#include "Society.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

// Recorded with the pre-M5 Society.cpp (2026-11-20) under run.ps1 society.
constexpr uint64_t kRecordedSociety = 0x23000e9330d597fcull;

struct Fnv {
	uint64_t h = 1469598103934665603ull;
	void u(uint64_t x) { for (int i = 0; i < 8; i++) { h ^= (x >> (8 * i)) & 0xffu; h *= 1099511628211ull; } }
	void i(int64_t x) { u(uint64_t(x)); }
	void d(double x) { uint64_t b = 0; std::memcpy(&b, &x, 8); u(b); }
	void s(const std::string& t) { for (const char c : t) { h ^= uint8_t(c); h *= 1099511628211ull; } u(t.size()); }
};

uint64_t traceHash(const World& w) {
	Fnv f;
	f.d(w.year);
	f.u(w.rng.s);
	f.i(w.stats.wars);
	f.d(w.god.awe); f.d(w.god.aweMax); f.d(w.god.mercy); f.d(w.god.wrath);
	for (const Culture& c : w.cultures) {
		f.i(c.id); f.i(c.dead); f.i(c.parent); f.i(c.capital); f.d(c.pop); f.i(c.towns); f.d(c.exhaustion); f.d(c.lastSplit);
		f.i(int(c.edict)); f.d(c.edictSince); f.i(c.age); f.d(c.science); f.s(c.name);
		f.d(c.traits.piety); f.d(c.traits.aggression); f.d(c.traits.curiosity); f.d(c.traits.seafaring);
		f.i(c.prophet.has_value());
		if (c.prophet) { f.s(c.prophet->name); f.i(c.prophet->settlement); f.d(c.prophet->until); }
		for (const auto& kv : c.seen) { f.s(kv.first); f.d(kv.second); }
	}
	for (const Settlement& s : w.settlements) {
		f.i(s.id); f.i(s.dead); f.i(s.culture); f.d(s.pop); f.d(s.devotion); f.d(s.love); f.d(s.jaded); f.i(s.apart);
		f.i(s.tier); f.d(s.plague); f.d(s.blessed); f.i(s.famine); f.i(s.capital); f.d(s.ark);
		for (const uint8_t b : s.b) f.i(b);
		f.i(s.prayer.has_value());
		if (s.prayer) { f.i(int(s.prayer->kind)); f.d(s.prayer->since); }
	}
	for (const auto& kv : w.vars.rel) {
		f.i(kv.first.first); f.i(kv.first.second);
		f.d(kv.second.v); f.i(kv.second.war); f.d(kv.second.since); f.d(kv.second.truce); f.d(kv.second.trade);
		f.i(kv.second.contact); f.i(kv.second.aggressor);
	}
	for (const auto& kv : w.vars.num) { f.s(kv.first); f.d(kv.second); }
	for (const Unit& u : w.units) { f.i(u.id); f.i(int(u.type)); f.i(u.culture); f.i(u.from); f.i(u.to); f.d(u.size); }
	return f.h;
}

bool isLand(const World& w, int c) { return w.elev[size_t(c)] >= w.seaLevel; }

int landAt(const World& w, int from, double ang) {
	int best = -1;
	double bd = 1e9;
	for (int c = 0; c < w.n; c++) {
		if (!isLand(w, c)) continue;
		const double d = std::fabs(w.geo->angle(from, c) - ang);
		if (d < bd) { bd = d; best = c; }
	}
	return best;
}

int addTown(World& w, int cell, int cid, double pop, double devotion, double love) {
	Settlement& s = state::newSettlement(w, cell, cid);
	s.pop = pop;
	s.devotion = devotion;
	s.love = love;
	s.tier = pop >= 1500 ? 3 : pop >= 400 ? 2 : pop >= 60 ? 1 : 0;
	s.name = "Town" + std::to_string(s.id);
	const int id = s.id;
	if (Culture* c = state::culture(w, cid)) if (c->capital < 0) { c->capital = id; w.settlements[size_t(id)].capital = true; }
	return id;
}

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

uint64_t history(int phase, bool storeNeutral) {
	auto w = makeWorld({ .L = 5, .seed = 1 });
	w->tune<MythosTune>().phase = phase;
	w->tune<SocietyTune>().hoardDrift = 0.01;
	std::vector<int> cu;
	for (int k = 0; k < 3; k++) {
		const Culture* c = society::newCulture(*w, { -1, -1, BirthReason::Awaken });
		if (c) cu.push_back(c->id);
	}
	if (cu.size() < 3) { fail("three cultures"); return 0; }
	const int home = bestHab(*w);
	std::vector<int> towns;
	const double angs[9] = { 0, 0.12, 0.25, 0.45, 0.55, 0.7, 0.9, 1.0, 0.8 };
	for (int k = 0; k < 9; k++) {
		const int cell = landAt(*w, home, angs[k] + 0.003 * k);
		const int cid = cu[size_t(k < 4 ? 0 : k < 7 ? 1 : 2)];
		towns.push_back(addTown(*w, cell, cid, 200.0 + 150.0 * k, 0.3 + 0.05 * k, 0.1 * (k % 3) - 0.1));
	}
	w->settlements[size_t(towns[0])].b[Building::SHRINE] = 1;
	w->settlements[size_t(towns[4])].b[Building::TEMPLE] = 1;
	w->settlements[size_t(towns[4])].b[Building::SHRINE] = 1;
	claimAll(*w, 0.08);
	if (storeNeutral) w->mythos.factors.assign(w->cultures.size(), Factors{});
	uint64_t chain = 0;
	for (int y = 1; y <= 300; y++) {
		w->year = y;
		for (Culture& c : w->cultures) c.age = std::min(int(Age::INDUSTRY), y / 60);
		// famines and a plague come and go; a drought sits over the second people for a while
		for (size_t k = 0; k < towns.size(); k++) {
			Settlement& s = w->settlements[size_t(towns[k])];
			s.famine = (y + int(k) * 7) % 37 < 4 ? s.famine + 1 : 0;
			if (y % 53 == int(k)) s.plague = 0.4;
			if (y % 53 == int(k) + 12) s.plague = 0;
		}
		if (y == 120) { Disaster d; d.id = 1; d.type = DisasterType::Drought; d.cell = w->settlements[size_t(towns[5])].cell; d.ang = 0.15; d.dur = 30; w->disasters.push_back(d); }
		if (y == 150) w->disasters.clear();
		// the god now and then
		if (y % 9 == 0) {
			society::WitnessOpts o; o.kind = "rain"; o.faith = 0.1; o.good = 0.6;
			society::witness(*w, w->settlements[size_t(towns[size_t(y / 9) % towns.size()])].cell, 0.1, o);
		}
		if (y % 23 == 0) {
			const int t = towns[size_t(4 + (y / 23) % 3)];
			society::WitnessOpts o; o.kind = "lightning"; o.faith = 0.05; o.good = -0.2; o.harmed = { t }; o.goodHarmed = -0.8;
			society::witness(*w, w->settlements[size_t(t)].cell, 0.1, o);
		}
		if (y % 31 == 0) {
			society::WitnessOpts o; o.kind = "heal"; o.faith = 0.12; o.good = 0.8;
			society::witness(*w, w->settlements[size_t(towns[size_t(y / 31) % towns.size()])].cell, 0.1, o);
		}
		if (y % 47 == 0) {
			society::WitnessOpts o; o.kind = "bloom"; o.faith = 0.08; o.good = 0.5;
			society::witness(*w, w->settlements[size_t(towns[7])].cell, 0.12, o);
		}
		if (y == 40) society::raiseProphet(*w, towns[1]);
		if (y == 45) society::setEdict(*w, cu[0], Edict::Build);
		society::yearly(*w);
		chain = chain * 1099511628211ull ^ traceHash(*w);
	}
	noNaN(*w, "society trace");
	if (phase == 0) {
		double pray = 0, answered = 0;
		for (const auto& kv : w->vars.num) {
			if (kv.first.rfind("society_pray_", 0) == 0) pray += kv.second;
			if (kv.first.rfind("society_answer_", 0) == 0) answered += kv.second;
		}
		int live = 0;
		for (const Settlement& s : w->settlements) live += s.dead ? 0 : 1;
		std::printf("    society trace world: %d prayers (%d answered), %zu cultures, %d live towns, wars %d, awe %.1f\n", int(pray),
			int(answered), w->cultures.size(), live, w->stats.wars, w->god.awe);
	}
	return chain;
}

} // namespace

DM_TEST("society v4: neutral Factors give the recorded 300-year trace (M5 acceptance)") {
	stub::Guard guard(false);
	const uint64_t p0 = history(0, false);
	const uint64_t p1 = history(1, true);
	const bool stubbedMode = stub::stubbed("diplomacy") && stub::stubbed("civ") && stub::stubbed("mythos");
	std::printf("    society trace: phase 0 %016llx, phase 1 neutral %016llx (%s)\n", (unsigned long long)p0,
		(unsigned long long)p1, stubbedMode ? "stubs" : "real modules");
	if (!stubbedMode) {
		auto w = makeWorld({ .L = 3 });
		w->tune<MythosTune>().phase = 1;
		state::newCulture(*w);
		w->mythos.factors.assign(1, Factors{});
		if (!(mythos::factors(*w, 0) == Factors{}) || !(mythos::witnessScale(*w, 0, "rain") == WitnessScale{})) return;
	}
	eq(p1, p0, "phase 1 with neutral Factors = phase 0, year by year");
	if (stubbedMode) eq(p0, kRecordedSociety, "phase 0 = the trace recorded from the pre-M5 Society.cpp");
}
