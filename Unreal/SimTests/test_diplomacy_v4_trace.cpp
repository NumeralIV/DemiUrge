// DEMIURGE — SimTests/test_diplomacy_v4_trace.cpp : the M5 "society-v4" acceptance trace for the relations half
// (SPEC-MYTHOS 9, track M5: "neutral Factors = identical yearly results (a recorded 300-year trace)").
//
// A synthetic four-people world runs society::diplomacy for 300 years (wars, armies and battles through onArrive,
// caravans and plague hops, prophet visits, envy and thanks from the god's favour sums, alliances). Every year the
// relations-half state is folded into one 64-bit chain (traceHash below: relations, exhaustion, settlements, units,
// vars.num, the rng state).
//   - run.ps1 diplomacy (fake Civ / Units / Path as in test_diplomacy.cpp): the chain equals the value RECORDED from
//     the pre-M5 Diplomacy.cpp (v3.1 + M0), at MythosTune::phase 0 and at phase 1 with neutral Factors records.
//   - run.ps1 all (every module real): phase 0 and phase 1 with neutral Factors give the same chain (no constant:
//     other tracks may legitimately change what the real collaborators do; the guardrail is simrun --hash).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Mythos.h"
#include "Path.h"
#include "Society.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "Units.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

// Recorded with the pre-M5 Diplomacy.cpp (2026-11-20) under run.ps1 diplomacy.
constexpr uint64_t kRecordedDiplomacy = 0x70d34757a17ae42aull;

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
		f.i(c.id); f.i(c.dead); f.i(c.capital); f.d(c.pop); f.i(c.towns); f.d(c.exhaustion); f.d(c.lastSplit);
		f.i(int(c.edict)); f.d(c.edictSince); f.i(c.age); f.d(c.science);
		f.d(c.traits.piety); f.d(c.traits.aggression); f.d(c.traits.curiosity); f.d(c.traits.seafaring);
		f.i(c.prophet.has_value());
		if (c.prophet) { f.s(c.prophet->name); f.i(c.prophet->settlement); f.d(c.prophet->until); }
		for (const auto& kv : c.seen) { f.s(kv.first); f.d(kv.second); }
	}
	for (const Settlement& s : w.settlements) {
		f.i(s.id); f.i(s.dead); f.i(s.culture); f.d(s.pop); f.d(s.devotion); f.d(s.love); f.d(s.jaded); f.i(s.apart);
		f.i(s.tier); f.d(s.plague); f.d(s.blessed); f.i(s.famine); f.i(s.capital); f.d(s.ark);
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
	uint64_t roads = 0;
	for (const uint8_t r : w.road) roads = roads * 31 + r;
	f.u(roads);
	return f.h;
}

bool seaC(const World& w, int c) { return double(w.elev[size_t(c)]) < w.seaLevel; }

std::vector<int32_t> bfsPath(const World& w, int from, int to, NavalMode naval, double maxCost) {
	if (!w.valid(from) || !w.valid(to)) return {};
	if (from == to) return { from };
	auto pass = [&](int c) {
		if (!seaC(w, c)) return true;
		if (naval == NavalMode::Open) return true;
		if (naval == NavalMode::Coastal) return double(w.elev[size_t(c)]) > w.seaLevel - 0.6;
		return false;
	};
	if (!pass(to)) return {};
	std::vector<int> prev(size_t(w.n), -2), depth(size_t(w.n), 0);
	std::vector<int> q{ from };
	prev[size_t(from)] = -1;
	for (size_t h = 0; h < q.size(); h++) {
		const int c = q[h];
		if (c == to) break;
		if (depth[size_t(c)] >= int(maxCost)) continue;
		for (int k = 0; k < w.geo->nbrN[size_t(c)]; k++) {
			const int m = w.geo->nbr[size_t(6 * c + k)];
			if (m < 0 || prev[size_t(m)] != -2 || !pass(m)) continue;
			prev[size_t(m)] = c;
			depth[size_t(m)] = depth[size_t(c)] + 1;
			q.push_back(m);
		}
	}
	if (prev[size_t(to)] == -2) return {};
	std::vector<int32_t> p;
	for (int c = to; c != -1; c = prev[size_t(c)]) p.push_back(c);
	std::reverse(p.begin(), p.end());
	return p;
}

void fakeDestroy(World& w, int sid, Cause cause) {
	Settlement* s = state::settlement(w, sid);
	if (!s || s->dead) return;
	s->dead = true;
	s->cause = cause;
	s->deadYear = w.year;
	for (int c = 0; c < w.n; c++) if (w.owner[size_t(c)] == sid) w.owner[size_t(c)] = -1;
	s->cells.clear();
}

void installFakes() {
	auto& h = stub::hooks();
	h.civ_capture = [](World& w, int sid, int cu) {
		if (Settlement* s = state::settlement(w, sid)) if (!s->dead) s->culture = cu;
	};
	h.civ_destroy = [](World& w, int sid, Cause cause) { fakeDestroy(w, sid, cause); };
	h.civ_damage = [](World& w, int sid, double frac, Cause cause) -> double {
		Settlement* s = state::settlement(w, sid);
		if (!s || s->dead) return 0.0;
		const double killed = s->pop * std::clamp(frac, 0.0, 1.0);
		s->pop -= killed;
		if (s->pop < 3) fakeDestroy(w, sid, cause);
		return killed;
	};
	h.civ_infect = [](World& w, int sid, double sev) -> bool {
		Settlement* s = state::settlement(w, sid);
		if (!s || s->dead || s->immune > 0) return false;
		s->plague = std::max(s->plague, sev);
		return true;
	};
	h.units_spawn = [](World& w, const units::SpawnOpts& o) -> Unit* {
		const Settlement* from = state::settlement(w, o.from);
		if (!from || from->dead || w.units.size() >= 300) return nullptr;
		std::vector<int32_t> p = o.path;
		if (p.empty()) p = { from->cell, o.dest };
		Unit& u = state::newUnit(w, o.type);
		u.culture = o.culture;
		u.from = o.from;
		u.to = o.to;
		u.dest = o.dest;
		u.path = p;
		u.size = o.size;
		u.naval = o.naval;
		u.data["arrive"] = w.year + std::max(1.0, std::ceil(double(p.size() - 1) / 5.0));
		return &u;
	};
	h.units_kill = [](World& w, int id, std::string_view) -> bool { return state::eraseUnit(w, id); };
	h.path_find = [](const World& w, int from, int to, const path::FindOpts& o, double* cost) -> std::vector<int32_t> {
		std::vector<int32_t> p = bfsPath(w, from, to, o.naval, std::isnan(o.maxCost) ? 400 : o.maxCost);
		if (cost) *cost = p.empty() ? path::kNoPath : double(p.size() - 1);
		return p;
	};
}

bool fakeMode() { return stub::stubbed("units") && stub::stubbed("civ") && stub::stubbed("path") && stub::stubbed("society"); }

void arrive(World& w) {
	std::vector<int> ids;
	for (const Unit& u : w.units) {
		const auto it = u.data.find("arrive");
		if (it == u.data.end() || it->second <= w.year) ids.push_back(u.id);
	}
	for (const int id : ids) {
		const Unit* u = state::findUnit(w, id);
		if (!u) continue;
		const Settlement* t = state::settlement(w, u->to);
		if ((u->type == UnitType::Army || u->type == UnitType::Trader) && (!t || t->dead)) { state::eraseUnit(w, id); continue; }
		if (u->type == UnitType::Army && !society::atWar(w, u->culture, t->culture)) { state::eraseUnit(w, id); continue; }
		society::onArrive(w, id);
		state::eraseUnit(w, id);
	}
}

void reclaim(World& w) {
	std::fill(w.owner.begin(), w.owner.end(), int16_t(-1));
	std::vector<int32_t> tmp;
	for (Settlement& s : w.settlements) {
		s.cells.clear();
		if (s.dead) continue;
		s.radius = 2 + std::clamp(s.tier, 0, 4);
		w.geo->ring(s.cell, s.radius, tmp);
		for (const int c : tmp) {
			if (seaC(w, c) || w.owner[size_t(c)] >= 0) continue;
			w.owner[size_t(c)] = int16_t(s.id);
			s.cells.push_back(c);
		}
	}
}

// One 300-year history; phase / neutral = MythosTune::phase and whether neutral Factors records are stored.
uint64_t history(int phase, bool storeNeutral) {
	auto w = makeWorld({ .L = 6, .seed = 7, .land = 0.42 });
	w->tune<MythosTune>().phase = phase;
	DiplomacyTune& T = w->tune<DiplomacyTune>();
	T.envyK = 0.01; T.sharedWrathK = 0.01; T.plagueK = 0.01; T.zealK = 0.01; T.faithK = 0.005;
	T.allyForm = 0.5; T.allyCall = true; T.allyDeter = true; T.warStall = 25; T.feudK = 0.5; T.warredMax = 40;
	T.thankDevotion = 0.02; T.blameLove = 0.02;
	Rng tr(31);
	std::vector<int> land;
	for (int c = 0; c < w->n; c++) if (!seaC(*w, c) && w->hab[size_t(c)] > 0.15f) land.push_back(c);
	std::vector<int> seeds;
	for (int guard = 0; seeds.size() < 4 && !land.empty() && guard < 5000; guard++) {
		const int c = land[size_t(tr.integer(0, int(land.size()) - 1))];
		bool farEnough = true;
		for (const int s : seeds) if (w->geo->angle(s, c) < 0.3) farEnough = false;
		if (farEnough) seeds.push_back(c);
	}
	std::vector<int> cu;
	for (size_t k = 0; k < seeds.size(); k++) {
		Culture& c = state::newCulture(*w);
		c.traits.aggression = std::clamp(0.55 + 0.4 * (tr.f() - tr.f()), 0.0, 1.0);
		c.name = "C" + std::to_string(c.id);
		cu.push_back(c.id);
	}
	std::map<int, double> K;
	std::vector<int32_t> tmp;
	for (int c = 0; c < w->n; c += 7) {
		if (seaC(*w, c) || w->hab[size_t(c)] < 0.25f) continue;
		w->geo->ring(c, 6, tmp);
		bool free = true;
		for (const int m : tmp) if (w->owner[size_t(m)] >= 0) { free = false; break; }
		if (!free) continue;
		size_t best = 0;
		for (size_t k = 1; k < seeds.size(); k++) if (w->geo->angle(c, seeds[k]) < w->geo->angle(c, seeds[best])) best = k;
		if (w->geo->angle(c, seeds[best]) > 0.6) continue;
		Settlement& s = state::newSettlement(*w, c, cu[best]);
		s.pop = 300;
		s.radius = 3;
		s.devotion = 0.2 + 0.6 * tr.f();
		s.love = std::clamp(0.3 * (tr.f() - tr.f()) + (best % 3 == 0 ? 0.2 : -0.1), -1.0, 1.0);
		const int sid = s.id;
		Culture& owner = w->cultures[size_t(cu[best])];
		if (owner.capital < 0) { owner.capital = sid; w->settlements[size_t(sid)].capital = true; }
		K[sid] = 400 + 2600 * tr.f();
		w->geo->ring(c, 3, tmp);
		for (const int m : tmp) if (!seaC(*w, m) && w->owner[size_t(m)] < 0) { w->owner[size_t(m)] = int16_t(sid); w->settlements[size_t(sid)].cells.push_back(m); }
	}
	if (storeNeutral) w->mythos.factors.assign(w->cultures.size(), Factors{});
	// culture 0 has a prophet whose visits come as Prophet units
	if (!cu.empty() && w->cultures[size_t(cu[0])].capital >= 0) w->cultures[size_t(cu[0])].prophet = Prophet{ "Ossa", w->cultures[size_t(cu[0])].capital, 1e9, -1 };
	uint64_t chain = 0;
	int battles = 0, won = 0;
	for (int y = 1; y <= 300; y++) {
		w->year = y;
		const int age = std::min(int(Age::SAIL), y / 80);
		for (Culture& c : w->cultures) c.age = age;
		for (Settlement& s : w->settlements) {
			if (s.dead) continue;
			s.tier = s.pop >= 1500 ? 3 : s.pop >= 400 ? 2 : s.pop >= 60 ? 1 : 0;
			if (age >= Age::IRON && s.tier >= 2) s.b[Building::MARKET] = 1;
			if (age >= Age::BRONZE && s.tier >= 2) s.b[Building::WALLS] = 1;
			if (age >= Age::SAIL && s.tier >= 2 && s.id % 3 == 0) s.b[Building::PORT] = 1;
		}
		if (y % 10 == 1) reclaim(*w);
		// the god's favour and wrath sums (Society writes them; stubbed here) and a sickness now and then
		if (y % 17 == 0 && !cu.empty()) w->vars.num["society_fgrace_" + std::to_string(cu[size_t(y / 17) % cu.size()])] += 0.2;
		if (y % 29 == 0 && !cu.empty()) w->vars.num["society_fwrath_" + std::to_string(cu[size_t(y / 29) % cu.size()])] += 0.2;
		if (y % 41 == 0) for (Settlement& s : w->settlements) if (!s.dead && s.id % 4 == 1) s.plague = 0.5;
		if (y % 41 == 12) for (Settlement& s : w->settlements) s.plague = 0;
		// a prophet visit every 10 years (Units' contract: a Prophet unit arriving at `to`)
		if (y % 10 == 5 && !cu.empty() && w->cultures[size_t(cu[0])].prophet) {
			int target = -1;
			for (const Settlement& s : w->settlements) if (!s.dead && s.culture == cu[0] && s.id != w->cultures[size_t(cu[0])].prophet->settlement) { target = s.id; if (s.id % 2 == y % 2) break; }
			if (target >= 0) {
				Unit& u = state::newUnit(*w, UnitType::Prophet);
				u.culture = cu[0];
				u.from = w->cultures[size_t(cu[0])].prophet->settlement;
				u.to = target;
				u.dest = w->settlements[size_t(target)].cell;
				u.data["arrive"] = w->year;
			}
		}
		if (y == 2 || y == 150) for (auto& kv : w->vars.rel) kv.second.v = -0.6;   // old grudges: wars to trace
		const std::vector<Event> evs = capture(*w, [&] { society::diplomacy(*w); arrive(*w); });
		for (const Event& e : evs) if (const EvBattle* b = e.as<EvBattle>()) { battles++; won += b->won ? 1 : 0; }
		for (Settlement& s : w->settlements) {
			if (s.dead) continue;
			const auto it = K.find(s.id);
			const double k = it == K.end() ? 1000 : it->second;
			s.pop = std::max(3.0, s.pop + 0.03 * s.pop * (1 - s.pop / k));
		}
		chain = chain * 1099511628211ull ^ traceHash(*w);
	}
	noNaN(*w, "diplomacy trace");
	if (phase == 0) {
		int live = 0, contact = 0;
		for (const Settlement& s : w->settlements) live += s.dead ? 0 : 1;
		for (const auto& kv : w->vars.rel) contact += kv.second.contact ? 1 : 0;
		std::printf("    diplomacy trace world: %zu cultures, %zu towns (%d live), %d wars, %d battles (%d won), %d pairs in contact, devotion of town 0 %.3f\n",
			w->cultures.size(), w->settlements.size(), live, w->stats.wars, battles, won, contact,w->settlements.empty() ? 0.0 : w->settlements[0].devotion);
	}
	return chain;
}

} // namespace

DM_TEST("diplomacy v4: neutral Factors give the recorded 300-year trace (M5 acceptance)") {
	stub::Guard guard(false);
	const bool fakes = fakeMode();
	if (fakes) installFakes();
	if (!fakes && (stub::stubbed("units") || stub::stubbed("civ") || stub::stubbed("path"))) return;   // a partial mix
	const uint64_t p0 = history(0, false);
	const uint64_t p1 = history(1, true);
	std::printf("    diplomacy trace: phase 0 %016llx, phase 1 neutral %016llx (%s)\n", (unsigned long long)p0,
		(unsigned long long)p1, fakes ? "fakes" : "real modules");
	if (!fakes) {
		// the real Mythos: only meaningful while it returns neutral records for this hand-made world
		auto w = makeWorld({ .L = 3 });
		w->tune<MythosTune>().phase = 1;
		state::newCulture(*w);
		w->mythos.factors.assign(1, Factors{});
		if (!(mythos::factors(*w, 0) == Factors{})) return;
	}
	eq(p1, p0, "phase 1 with neutral Factors = phase 0, year by year");
	if (fakes) eq(p0, kRecordedDiplomacy, "phase 0 = the trace recorded from the pre-M5 Diplomacy.cpp");
}
