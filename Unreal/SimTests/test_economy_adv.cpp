// DEMIURGE — SimTests/test_economy_adv.cpp : adversarial verification of the economy half of Civ
// (SPEC-SIM 3.7, BUILD.md A8). Owner: the A8 verifier. Runs with `run.ps1 economy` (everything else
// stubbed; small spec-written collaborators are installed as hooks for the stubbed modules only) and in
// `run.ps1 all` (real modules; hook-only checks return early there).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include "Civ.h"
#include "Climate.h"
#include "Hydro.h"
#include "MapImg.h"
#include "Path.h"
#include "Society.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "Units.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

// ------------------------------------------------------------------ spec-written collaborators

void advClaim(World& w, int sid) {
	if (sid < 0 || size_t(sid) >= w.settlements.size()) return;
	Settlement& s = w.settlements[size_t(sid)];
	if (s.dead || !w.valid(s.cell)) return;
	for (const int32_t c : s.cells) if (w.owner[size_t(c)] == sid) w.owner[size_t(c)] = -1;
	s.cells.clear();
	std::vector<int32_t> ring;
	w.geo->ring(s.cell, s.radius, ring);
	for (const int32_t c : ring) {
		if (state::isSea(w, c)) continue;
		const int o = w.owner[size_t(c)];
		if (o >= 0 && o != sid && !w.settlements[size_t(o)].dead) continue;
		w.owner[size_t(c)] = int16_t(sid);
		s.cells.push_back(c);
	}
	std::sort(s.cells.begin(), s.cells.end());
	w.dirty.color = true;
}
civ::SettleCheck advCanSettle(const World& w, int c) {
	if (!w.valid(c) || state::isSea(w, c) || hydro::isLake(w, c)) return { false, civ::SettleReason::Sea };
	if (!(w.hab[size_t(c)] > 0.05f)) return { false, civ::SettleReason::Barren };
	std::vector<int32_t> ring;
	w.geo->ring(c, 7, ring);
	for (const int32_t m : ring) if (w.owner[size_t(m)] >= 0) return { false, civ::SettleReason::Crowded };
	return { true, civ::SettleReason::Ok };
}
double advSiteScore(const World& w, int c) { return w.valid(c) ? double(w.hab[size_t(c)]) : 0.0; }
int advLive(const World& w) {
	int k = 0;
	for (const Settlement& s : w.settlements) k += s.dead ? 0 : 1;
	return k;
}
Settlement* advFound(World& w, int cell, int cu, double pop, FoundedBy by, const civ::FoundOpts& o) {
	if (!advCanSettle(w, cell).ok || advLive(w) >= w.tune<CivTune>().maxSettlements) return nullptr;
	Settlement& s = state::newSettlement(w, cell, cu);
	const int id = s.id;
	s.pop = pop;
	s.devotion = o.devotion.value_or(by == FoundedBy::Awaken ? 0.6 : 0.5);
	s.love = o.love.value_or(by == FoundedBy::Awaken ? 0.3 : 0.0);
	s.radius = 2;
	advClaim(w, id);
	w.settlements[size_t(id)].K = civ::capacity(w, w.settlements[size_t(id)]);
	if (Culture* c = state::culture(w, cu); c && c->capital < 0) { c->capital = id; w.settlements[size_t(id)].capital = true; }
	w.bus.emit(EvSettlementFounded{ id, cell, cu, by });
	return &w.settlements[size_t(id)];
}
void advDestroy(World& w, int sid, Cause cause) {
	if (sid < 0 || size_t(sid) >= w.settlements.size() || w.settlements[size_t(sid)].dead) return;
	Settlement& s = w.settlements[size_t(sid)];
	s.dead = true; s.deadYear = w.year; s.cause = cause;
	for (const int32_t c : s.cells) if (w.owner[size_t(c)] == sid) w.owner[size_t(c)] = -1;
	s.cells.clear();
	w.bus.emit(EvSettlementDestroyed{ sid, s.cell, s.culture, cause });
}
bool advInfect(World& w, int sid, double sev) {
	if (sid < 0 || size_t(sid) >= w.settlements.size()) return false;
	Settlement& s = w.settlements[size_t(sid)];
	if (s.dead || s.immune > 0) return false;
	const bool was = s.plague > 0;
	s.plague = std::max(s.plague, sev);
	if (!was) w.bus.emit(EvSettlementPlague{ sid, true });
	return true;
}
bool advCure(World& w, int sid, double imm) {
	if (sid < 0 || size_t(sid) >= w.settlements.size()) return false;
	Settlement& s = w.settlements[size_t(sid)];
	if (s.dead) return false;
	const bool was = s.plague > 0;
	s.plague = 0; s.immune = imm;
	if (was) w.bus.emit(EvSettlementPlague{ sid, false });
	return true;
}
int advReach(const World& w, int from, double maxCost, NavalMode naval, const path::Visit& visit) {
	if (!w.valid(from)) return 0;
	const Geo& g = *w.geo;
	std::vector<double> best(size_t(w.n), 1e300);
	std::vector<uint8_t> done(size_t(w.n), 0);
	using Q = std::pair<double, int>;
	std::priority_queue<Q, std::vector<Q>, std::greater<Q>> q;
	best[size_t(from)] = 0;
	q.push({ 0, from });
	int visited = 0;
	while (!q.empty()) {
		const auto [d, c] = q.top();
		q.pop();
		if (done[size_t(c)]) continue;
		done[size_t(c)] = 1;
		visited++;
		if (!visit(c, d)) break;
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
			const int m = g.nbr[size_t(6 * c + k)];
			const bool sa = state::isSea(w, c), sb = state::isSea(w, m);
			double step;
			if (!sb) step = 1 + 6 * std::fabs(double(w.elev[size_t(m)]) - double(w.elev[size_t(c)]));
			else if (naval == NavalMode::Land) continue;
			else if (naval == NavalMode::Coastal) { if (w.seaLevel - w.elev[size_t(m)] >= 0.6) continue; step = 0.6; }
			else step = 0.35;
			if (sa != sb) step += 3;
			const double nd = d + step;
			if (nd <= maxCost && nd < best[size_t(m)]) { best[size_t(m)] = nd; q.push({ nd, m }); }
		}
	}
	return visited;
}
Unit* advTeleport(World& w, const units::SpawnOpts& o) {
	const Settlement* from = state::settlement(w, o.from);
	if (!from || from->dead) return nullptr;
	if (o.to >= 0) {
		Settlement* t = state::settlement(w, o.to);
		if (!t || t->dead) return nullptr;
		t->pop += o.size;
	} else {
		civ::FoundOpts fo;
		fo.devotion = from->devotion;
		fo.love = from->love;
		if (!civ::found(w, o.dest, o.culture, o.size, FoundedBy::Settlers, fo)) return nullptr;
	}
	Unit& u = state::newUnit(w, o.type);
	u.culture = o.culture; u.from = o.from; u.to = o.to; u.dest = o.dest; u.size = o.size; u.naval = o.naval;
	return &u;
}
void advInstall(bool withUnits = true, bool withPath = true) {
	auto& h = stub::hooks();
	if (stub::stubbed("civ")) {
		h.civ_claim = advClaim;
		h.civ_canSettle = advCanSettle;
		h.civ_siteScore = advSiteScore;
		h.civ_found = advFound;
		h.civ_destroy = advDestroy;
		h.civ_infect = advInfect;
		h.civ_cure = advCure;
	}
	if (withPath && stub::stubbed("path")) h.path_reach = advReach;
	if (withUnits && stub::stubbed("units")) h.units_spawn = advTeleport;
}
bool advCanTeleport() { return stub::stubbed("units") && stub::stubbed("path") && stub::stubbed("civ"); }

int advCount(const std::vector<Event>& ev, Ev t) {
	int k = 0;
	for (const Event& e : ev) k += e.type == t ? 1 : 0;
	return k;
}

// ------------------------------------------------------------------ worlds

// Flat land everywhere (elev 1, sea 0): temp 17, moist 0.336 (suit 0.8), fert 0.6, hab 0.5.
std::unique_ptr<World> advUniform(int L, double elev = 1) {
	auto w = state::create({ .seed = 11, .L = L, .geo = sharedGeo(L) });
	watch(*w);
	w->phase = Phase::History;
	w->heat = 0;
	w->seaLevel = 0;
	w->vars.seaBase = 0;
	for (int c = 0; c < w->n; c++) {
		const size_t i = size_t(c);
		w->elev[i] = float(elev); w->temp[i] = 17; w->moist[i] = 0.336f; w->fert[i] = 0.6f; w->hab[i] = 0.5f;
		w->veg[i] = 0; w->fauna[i] = 0; w->fresh[i] = 255; w->coast[i] = 255; w->biome[i] = Biome::GRASSLAND;
		w->slope[i] = 0;
	}
	return w;
}
int advLatLon(const World& w, double latDeg, double lonDeg) {
	const double la = latDeg * kPi / 180, lo = lonDeg * kPi / 180;
	return w.geo->cellAt(std::cos(la) * std::cos(lo), std::sin(la), std::cos(la) * std::sin(lo));
}
int advCulture(World& w, int age, double curiosity = 0.5) {
	Culture& cu = state::newCulture(w);
	cu.age = age;
	cu.traits.curiosity = curiosity;
	cu.name = "Folk";
	return cu.id;
}
// A settlement placed directly (no civ::found), territory claimed through civ::claim (hook or real).
int advSettle(World& w, int cell, int cu, double pop, int tier = 0) {
	Settlement& s = state::newSettlement(w, cell, cu);
	s.pop = pop;
	s.tier = tier;
	s.devotion = 0.2;
	s.radius = w.tune<EconomyTune>().radius[size_t(tier)];
	const int id = s.id;
	if (stub::stubbed("civ")) advClaim(w, id); else civ::claim(w, id);
	return id;
}
std::vector<int> advHops(const World& w, const std::vector<int>& src, int cap) {
	std::vector<int> d(size_t(w.n), cap);
	std::vector<int> q;
	for (const int c : src) { d[size_t(c)] = 0; q.push_back(c); }
	for (size_t h = 0; h < q.size(); h++) {
		const int c = q[h];
		if (d[size_t(c)] + 1 >= cap) continue;
		for (int k = 0; k < w.geo->nbrN[size_t(c)]; k++) {
			const int m = w.geo->nbr[size_t(6 * c + k)];
			if (d[size_t(m)] > d[size_t(c)] + 1) { d[size_t(m)] = d[size_t(c)] + 1; q.push_back(m); }
		}
	}
	return d;
}
void advYear(World& w) {
	w.year += 1;
	w.units.clear();
	civ::yearly(w);
}
double advPop(const World& w) {
	double p = 0;
	for (const Settlement& s : w.settlements) if (!s.dead) p += s.pop;
	return p;
}

} // namespace

// ================================================================== plague radius in hops

DM_TEST("economy adv: plague spreads to every settlement within 8 hops and none at 9+") {
	stub::Guard guard;
	if (!stub::stubbed("civ")) return;   // needs the controllable infect / claim
	advInstall(false, false);
	auto w = advUniform(6);
	EconomyTune& T = w->tune<EconomyTune>();
	T.searchesPerYear = 0;
	T.plagueSpreadP = 2;   // certain
	const int a = advLatLon(*w, 20, 40);
	const std::vector<int> d = advHops(*w, { a }, 40);
	const int cu = advCulture(*w, Age::STONE);
	const int src = advSettle(*w, a, cu, 1000, 0);
	w->settlements[size_t(src)].plague = 1.0;
	std::vector<int> at8, at9, at10;
	for (int c = 0; c < w->n; c++) {
		if (d[size_t(c)] == 8) at8.push_back(c);
		if (d[size_t(c)] == 9) at9.push_back(c);
		if (d[size_t(c)] == 10) at10.push_back(c);
	}
	// settlements on every ring cell (no territory: only the centres matter for the spread)
	std::vector<std::pair<int, int>> placed;   // (hops, sid)
	for (const auto* ring : { &at8, &at9, &at10 })
		for (const int c : *ring) {
			Settlement& s = state::newSettlement(*w, c, cu);
			s.pop = 200; s.devotion = 0.2;
			placed.emplace_back(d[size_t(c)], s.id);
		}
	// the spread check (one generation): run only the economy's year
	advYear(*w);
	int in8 = 0, in9 = 0, in10 = 0;
	for (const auto& [h, sid] : placed) {
		const bool ill = w->settlements[size_t(sid)].plague > 0;
		if (h == 8) in8 += ill ? 1 : 0;
		if (h == 9) in9 += ill ? 1 : 0;
		if (h == 10) in10 += ill ? 1 : 0;
	}
	std::printf("    infected: 8 hops %d / %zu, 9 hops %d / %zu, 10 hops %d / %zu\n", in8, at8.size(), in9, at9.size(), in10, at10.size());
	eq(in8, int(at8.size()), "every settlement 8 hops away can catch it");
	eq(in9, 0, "none 9 hops away");
	eq(in10, 0, "none 10 hops away");
	noNaN(*w);
}

// ================================================================== NaN and odd inputs

DM_TEST("economy adv: a NaN cell never zeroes or poisons K; NaN fields everywhere stay finite") {
	auto w = advUniform(5);
	const int c = advLatLon(*w, 0, 50);
	std::vector<int32_t> r3;
	w->geo->ring(c, 3, r3);
	for (const int32_t m : r3) if (m != c) { w->elev[size_t(m)] = -1; w->fauna[size_t(m)] = 0.5f; }
	w->hab[size_t(c)] = 0.1f;
	w->veg[size_t(c)] = 0.5f; w->fauna[size_t(c)] = 0.25f;
	const int cu = advCulture(*w, Age::STONE);
	const int sid = advSettle(*w, c, cu, 10, 0);
	const double K0 = civ::capacity(*w, w->settlements[size_t(sid)]);
	const int sea0 = w->geo->nbr[size_t(6 * c)];
	const double one = 4 * 0.5;   // one bordering sea cell's fish at Stone
	w->fauna[size_t(sea0)] = std::nanf("");
	const double K1 = civ::capacity(*w, w->settlements[size_t(sid)]);
	std::printf("    K %.3f; with one NaN-fauna sea cell %.3f (expected %.3f)\n", K0, K1, K0 - one);
	ok(std::isfinite(K1), "finite");
	near(K1, K0 - one, 1e-4, "only that cell's fish is lost");
	// NaN in every input field of the territory and its sea
	auto w2 = advUniform(5);
	const int c2 = advLatLon(*w2, 10, 10);
	const int cu2 = advCulture(*w2, Age::IRON);
	const int s2 = advSettle(*w2, c2, cu2, 100, 1);
	std::vector<int32_t> r4;
	w2->geo->ring(c2, 4, r4);
	const float nan = std::nanf("");
	for (size_t k = 0; k < r4.size(); k++) {
		const size_t i = size_t(r4[k]);
		if (k % 7 == 3) w2->elev[i] = -1;
		switch (k % 6) {
		case 0: w2->temp[i] = nan; break;
		case 1: w2->moist[i] = nan; break;
		case 2: w2->fert[i] = nan; break;
		case 3: w2->veg[i] = nan; w2->fauna[i] = nan; break;
		case 4: w2->hab[i] = nan; break;
		default: w2->slope[i] = nan; break;
		}
	}
	const double K2 = civ::capacity(*w2, w2->settlements[size_t(s2)]);
	ok(std::isfinite(K2) && K2 > 0, "K finite and positive: " + std::to_string(K2));
	stub::Guard guard;
	advInstall(false, false);
	for (int y = 0; y < 20; y++) advYear(*w2);
	// the NaN inputs are not the economy's to clean, but nothing it writes may be NaN
	for (int m = 0; m < w2->n; m++) {
		const size_t i = size_t(m);
		if (!std::isfinite(w2->farm[i]) || !std::isfinite(w2->urban[i])) { fail("farm / urban NaN at " + std::to_string(m)); break; }
		if (!std::isnan(w2->veg[i]) && !std::isfinite(w2->veg[i])) { fail("veg inf"); break; }
	}
	const Settlement& s = w2->settlements[size_t(s2)];
	ok(std::isfinite(s.pop) && std::isfinite(s.K) && std::isfinite(s.growth), "settlement numbers finite");
	ok(std::isfinite(w2->cultures[size_t(cu2)].science), "science finite");
}

DM_TEST("economy adv: poles, no sea, no land, every phase, L = 4 and 6") {
	stub::Guard guard;
	advInstall();
	for (const int L : { 4, 6 }) {
		// settlements on both poles of an all-land world (no sea at all)
		auto w = advUniform(L);
		const int north = w->geo->cellAt(0, 1, 0), south = w->geo->cellAt(0, -1, 0);
		const int cu = advCulture(*w, Age::BRONZE);
		const int a = advSettle(*w, north, cu, 300, 1), b = advSettle(*w, south, cu, 300, 1);
		w->settlements[size_t(a)].lastExpand = -100; w->settlements[size_t(b)].lastExpand = -100;
		for (int y = 0; y < 60; y++) advYear(*w);
		const std::string tag = "L " + std::to_string(L);
		ok(!w->settlements[size_t(a)].dead && !w->settlements[size_t(b)].dead, tag + ": polar towns live");
		ok(w->settlements[size_t(a)].K > 0 && w->settlements[size_t(b)].K > 0, tag + ": polar K > 0");
		std::printf("    %s poles: K %.0f / %.0f, pop %.0f / %.0f, live %d\n", tag.c_str(), w->settlements[size_t(a)].K,
			w->settlements[size_t(b)].K, w->settlements[size_t(a)].pop, w->settlements[size_t(b)].pop, advLive(*w));
		noNaN(*w, tag + " no sea");
		ok(checkFinite(*w).empty(), tag + ": " + checkFinite(*w));
		// no land: a settlement whose centre is sea (the whole world drowned): evacuation impossible
		auto ws = advUniform(L, -2);
		const int cus = advCulture(*ws, Age::IRON);
		Settlement& sea = state::newSettlement(*ws, advLatLon(*ws, 0, 0), cus);
		sea.pop = 500;
		const int seaId = sea.id;
		std::vector<Event> ev = capture(*ws, [&] { advYear(*ws); });
		ok(ws->settlements[size_t(seaId)].dead && ws->settlements[size_t(seaId)].cause == Cause::Drowned, tag + ": drowned");
		eq(advCount(ev, Ev::SettlementDestroyed), 1, tag + ": one destroyed event");
		noNaN(*ws, tag + " no land");
	}
	// every phase: yearly must not break anything (Sim only calls it in history / ended)
	for (const Phase ph : { Phase::Molten, Phase::Water, Phase::Life, Phase::People, Phase::History, Phase::Ended }) {
		auto w = makeWorld({ .L = 4, .seed = 5, .phase = ph });
		const int cu = advCulture(*w, Age::STONE);
		const int c = bestHab(*w);
		const int sid = advSettle(*w, c, cu, 50, 0);
		for (int y = 0; y < 10; y++) advYear(*w);
		ok(checkFinite(*w).empty(), std::string("phase ") + toStr(ph) + ": " + checkFinite(*w));
		ok(w->phase == ph, "phase untouched");
		(void)sid;
	}
	// yearly on worlds without geo-sized arrays or settlements
	auto blank = state::create({ .seed = 3, .L = 2 });
	watch(*blank);
	civ::yearly(*blank);
	noNaN(*blank);
}

// ================================================================== ownership (writers table)

namespace {
struct CultureView {
	std::string name, adj, plural, godName, epithet;
	int parent, capitalUnused;
	bool dead;
	double pop, exhaustion, lastSplit, edictSince, born;
	int towns;
	Edict edict;
	bool prophet;
	double piety, aggression, curiosity, seafaring;
	size_t seen;
	bool operator==(const CultureView&) const = default;
};
CultureView viewOf(const Culture& c) {
	return { c.name, c.adj, c.plural, c.godName, c.epithet, c.parent, 0, c.dead, c.pop, c.exhaustion, c.lastSplit, c.edictSince,
		c.born, c.towns, c.edict, c.prophet.has_value(), c.traits.piety, c.traits.aggression, c.traits.curiosity, c.traits.seafaring, c.seen.size() };
}
struct BeliefView {
	double devotion, love, jaded, ark, founded;
	int apart;
	bool prayer;
	uint8_t wonder;
	std::string name;
	int cell;
	bool operator==(const BeliefView&) const = default;
};
BeliefView beliefOf(const Settlement& s) {
	return { s.devotion, s.love, s.jaded, s.ark, s.founded, s.apart, s.prayer.has_value(), s.b[Building::WONDER], s.name, s.cell };
}
} // namespace

DM_TEST("economy adv: yearly writes only what the writers table gives Civ / Economy") {
	stub::Guard guard;
	advInstall();
	auto w = makeWorld({ .L = 5, .seed = 3 });
	const int cuA = advCulture(*w, Age::IRON), cuB = advCulture(*w, Age::SAIL);
	w->vars.rel[state::relKey(cuA, cuB)].war = true;
	w->vars.rel[state::relKey(cuA, cuB)].contact = true;
	w->cultures[size_t(cuA)].prophet = Prophet{ "Seer", -1, 1e9 };
	w->cultures[size_t(cuA)].edict = Edict::Expand;
	w->cultures[size_t(cuA)].seen["rain"] = 0.4;
	int placed = 0;
	std::vector<int32_t> ring;
	for (int c = 0; c < w->n && placed < 14; c += 5) {
		if (state::isSea(*w, c) || w->hab[size_t(c)] < 0.2f) continue;
		w->geo->ring(c, 8, ring);
		bool free = true;
		for (const int32_t m : ring) if (w->owner[size_t(m)] >= 0) { free = false; break; }
		if (!free) continue;
		const int sid = advSettle(*w, c, placed % 2 ? cuA : cuB, 100, placed % 4);
		Settlement& s = w->settlements[size_t(sid)];
		s.pop = std::max(60.0, 0.95 * civ::capacity(*w, s));
		s.lastExpand = -100;
		s.devotion = 0.4; s.love = 0.1 * (placed % 3); s.jaded = 0.2; s.apart = placed; s.ark = 0.01 * placed;
		if (placed == 3) { s.plague = 0.7; }
		if (placed == 5) s.blessed = 10;
		if (placed == 6) s.prayer = Prayer{ PrayerKind::Rain, 0 };
		if (placed == 7) s.b[Building::WONDER] = 1;
		placed++;
	}
	ok(placed >= 8, "settlements placed: " + std::to_string(placed));
	w->settlements[0].capital = true; w->cultures[size_t(w->settlements[0].culture)].capital = 0;
	// snapshot
	std::vector<std::vector<double>> cells(state::kCellFieldCount);
	for (int f = 0; f < state::kCellFieldCount; f++) {
		const auto arr = state::cellArray(*w, f);
		cells[size_t(f)].resize(size_t(w->n));
		for (int c = 0; c < w->n; c++) cells[size_t(f)][size_t(c)] = arr.get(c);
	}
	std::vector<CultureView> cv;
	for (const Culture& c : w->cultures) cv.push_back(viewOf(c));
	std::vector<BeliefView> bv;
	for (const Settlement& s : w->settlements) bv.push_back(beliefOf(s));
	const double sea = w->seaLevel, co2 = w->co2, dust = w->dust, gt = w->globalTemp, heat = w->heat;
	const God god = w->god;
	const auto relBefore = w->vars.rel;
	const size_t chron = w->chronicle.size();
	const Phase ph = w->phase;
	std::set<std::string> numBefore;
	for (const auto& kv : w->vars.num) numBefore.insert(kv.first);
	std::vector<float> vegBefore(w->veg);
	int vegUp = 0, vegOffFarm = 0;
	for (int y = 0; y < 30; y++) {
		const std::vector<float> v0(w->veg);
		advYear(*w);
		for (int c = 0; c < w->n; c++) {
			const size_t i = size_t(c);
			if (w->veg[i] > v0[i]) vegUp++;
			if (w->veg[i] < v0[i] && !(w->farm[i] > 0)) vegOffFarm++;   // lowered in a year it was farmed
		}
	}
	// cells
	const std::set<std::string> allowed = { "owner", "farm", "urban", "veg" };
	for (int f = 0; f < state::kCellFieldCount; f++) {
		const std::string nm = state::kCellFields[f].name;
		if (allowed.count(nm)) continue;
		const auto arr = state::cellArray(*w, f);
		int changed = 0;
		for (int c = 0; c < w->n; c++) {
			const double a = cells[size_t(f)][size_t(c)], b = arr.get(c);
			if (!(a == b) && !(std::isnan(a) && std::isnan(b))) changed++;
		}
		eq(changed, 0, "cell field '" + nm + "' untouched");
	}
	int lowered = 0;
	for (int c = 0; c < w->n; c++) lowered += w->veg[size_t(c)] < vegBefore[size_t(c)] ? 1 : 0;
	std::printf("    veg lowered on %d cells (all of them farmed in the year it happened)\n", lowered);
	eq(vegUp, 0, "veg never raised");
	eq(vegOffFarm, 0, "veg lowered only on farmland");
	// entities
	for (size_t k = 0; k < cv.size(); k++) ok(viewOf(w->cultures[k]) == cv[k], "culture " + std::to_string(k) + ": only age / science / capital may change");
	for (size_t k = 0; k < bv.size(); k++) ok(beliefOf(w->settlements[k]) == bv[k], "settlement " + std::to_string(k) + ": belief half untouched");
	ok(w->seaLevel == sea && w->co2 == co2 && w->dust == dust && w->globalTemp == gt && w->heat == heat, "world scalars untouched (Climate is a stub here)");
	ok(w->god.awe == god.awe && w->god.aweMax == god.aweMax && w->god.mercy == god.mercy && w->god.wrath == god.wrath && w->god.deeds == god.deeds, "god untouched");
	ok(w->phase == ph && w->chronicle.size() == chron, "phase, chronicle untouched");
	ok(w->vars.rel.size() == relBefore.size(), "no relation created");
	for (const auto& kv : w->vars.num)
		if (!numBefore.count(kv.first)) ok(kv.first.rfind("civ_", 0) == 0, "vars.num key prefixed civ_: " + kv.first);
	int live = 0, dead = 0;
	for (const Settlement& s : w->settlements) (s.dead ? dead : live)++;
	std::printf("    after 30 years: %d live, %d dead settlements, %zu vars.num keys\n", live, dead, w->vars.num.size());
	noNaN(*w);
}

// ================================================================== tiers

DM_TEST("economy adv: big jumps climb and fall one tier per year with consistent events and radius") {
	stub::Guard guard;
	advInstall(false, false);
	auto w = advUniform(6);
	w->tune<EconomyTune>().searchesPerYear = 0;
	const int c = advLatLon(*w, 10, 30);
	const int cu = advCulture(*w, Age::INDUSTRY);
	const int sid = advSettle(*w, c, cu, 30, 0);
	std::vector<std::pair<int, int>> seen;   // (prev, tier)
	auto tok = w->bus.on<EvSettlementTier>([&](const EvSettlementTier& e) { seen.emplace_back(e.prev, e.tier); });
	for (int y = 0; y < 6; y++) { w->settlements[size_t(sid)].pop = 30000; advYear(*w); }
	eq(w->settlements[size_t(sid)].tier, 4, "metropolis");
	eq(w->settlements[size_t(sid)].radius, 6, "radius 6");
	for (int y = 0; y < 6; y++) { w->settlements[size_t(sid)].pop = 20; advYear(*w); }
	w->bus.off(tok);
	eq(w->settlements[size_t(sid)].tier, 0, "back to a camp");
	eq(w->settlements[size_t(sid)].radius, 2, "radius 2");
	const std::vector<std::pair<int, int>> want = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 3 }, { 3, 2 }, { 2, 1 }, { 1, 0 } };
	eq(seen, want, "one step per year, prev / tier consistent");
	// at the gate exactly: promote; just under half of it: demote; exactly half: keep
	auto w2 = advUniform(5);
	w2->tune<EconomyTune>().searchesPerYear = 0;
	const int cu2 = advCulture(*w2, Age::IRON);
	const int s2 = advSettle(*w2, advLatLon(*w2, 0, 0), cu2, 400, 1);
	w2->tune<EconomyTune>().growthR = 0;   // freeze the population
	advYear(*w2);
	eq(w2->settlements[size_t(s2)].tier, 2, "400 -> town");
	w2->settlements[size_t(s2)].pop = 200; advYear(*w2);
	eq(w2->settlements[size_t(s2)].tier, 2, "exactly half the gate keeps it");
	w2->settlements[size_t(s2)].pop = 199.9; advYear(*w2);
	eq(w2->settlements[size_t(s2)].tier, 1, "below half demotes");
}

// ================================================================== famine

DM_TEST("economy adv: famine counter resets once fed, the event fires once per famine, again for a new one") {
	stub::Guard guard;
	advInstall(false, false);
	auto w = advUniform(6);
	w->tune<EconomyTune>().searchesPerYear = 0;
	w->tune<EconomyTune>().gate = { 0, 1e12, 1e12, 1e12, 1e12 };   // a camp for ever: K stays put
	const int cu = advCulture(*w, Age::STONE);
	const int sid = advSettle(*w, advLatLon(*w, 10, 30), cu, 30, 0);
	advYear(*w);
	const double K = w->settlements[size_t(sid)].K;
	std::vector<Event> ev = capture(*w, [&] {
		w->settlements[size_t(sid)].pop = 3 * K;
		for (int y = 0; y < 8; y++) advYear(*w);
		w->settlements[size_t(sid)].pop = 0.5 * K;   // fed again
		advYear(*w);
	});
	eq(w->settlements[size_t(sid)].famine, 0, "reset when fed");
	eq(advCount(ev, Ev::SettlementFamine), 1, "one event for the first famine");
	ev = capture(*w, [&] { w->settlements[size_t(sid)].pop = 3 * K; for (int y = 0; y < 4; y++) advYear(*w); });
	eq(advCount(ev, Ev::SettlementFamine), 1, "a new famine emits again");
	// converging from above: the counter must not run for ever
	w->settlements[size_t(sid)].pop = 1.3 * K;
	for (int y = 0; y < 40; y++) advYear(*w);
	const Settlement& s = w->settlements[size_t(sid)];
	std::printf("    after 40 years from 1.3 K: pop/K %.4f, famine %d\n", s.pop / s.K, s.famine);
	ok(s.famine == 0, "no endless famine on a settled population");
	ok(s.pop >= s.K * 0.99, "never undershoots K by more than 1 %");
}

// ================================================================== expansion

DM_TEST("economy adv: no settler searches while the 160-settlement cap is reached") {
	if (!advCanTeleport()) return;
	stub::Guard guard(true);
	advInstall();
	auto w = advUniform(6);
	w->year = 500;
	const int cu = advCulture(*w, Age::STONE);
	int placed = 0;
	for (int c = 0; c < w->n && placed < 160; c += 13) {
		if (w->owner[size_t(c)] >= 0) continue;
		bool free = true;
		std::vector<int32_t> r;
		w->geo->ring(c, 2, r);
		for (const int32_t m : r) if (w->owner[size_t(m)] >= 0) { free = false; break; }
		if (!free) continue;
		const int sid = advSettle(*w, c, cu, 300, 1);
		w->settlements[size_t(sid)].lastExpand = 0;
		placed++;
	}
	eq(placed, 160, "160 settlements");
	for (Settlement& s : w->settlements) s.pop = std::max(41.0, 0.95 * civ::capacity(*w, s));
	advYear(*w);
	std::printf("    at the cap: %d searches, %d spawns\n", stub::called("path_reach"), stub::called("units_spawn"));
	eq(stub::called("units_spawn"), 0, "no settlers sent to sites that cannot be founded");
	eq(stub::called("path_reach"), 0, "no searches");
	// one below the cap: searches resume (at most 2)
	advDestroy(*w, 0, Cause::Abandoned);
	for (Settlement& s : w->settlements) if (!s.dead) s.lastExpand = 0;
	const int before = stub::called("path_reach");
	advYear(*w);
	ok(stub::called("path_reach") - before >= 1 && stub::called("path_reach") - before <= 2, "below the cap the searches resume");
}

DM_TEST("economy adv: candidate camps need estimated K >= 60; poor land is never chosen") {
	if (!advCanTeleport()) return;
	stub::Guard guard(true);
	advInstall();
	std::vector<int> dests;
	std::vector<double> destK;
	stub::hooks().units_spawn = [&](World& ww, const units::SpawnOpts& o) -> Unit* {
		dests.push_back(o.dest);
		Settlement pr;
		pr.cell = o.dest; pr.culture = o.culture;
		destK.push_back(civ::capacity(ww, pr));   // the estimate before anyone lives there
		return advTeleport(ww, o);
	};
	auto w = advUniform(5);
	w->year = 100;
	const int home = advLatLon(*w, 0, 0);
	// everything outside the home area: habitable (0.3) but not arable -> forage only, K < 60
	const std::vector<int> d = advHops(*w, { home }, 99);
	for (int c = 0; c < w->n; c++) if (d[size_t(c)] > 3) { w->hab[size_t(c)] = 0.12f; w->veg[size_t(c)] = 0.5f; w->fauna[size_t(c)] = 0.5f; }
	// one rich patch 12 hops east
	const int rich = advLatLon(*w, 0, 12 * w->geo->step * 180 / kPi);
	std::vector<int32_t> rr;
	w->geo->ring(rich, 2, rr);
	for (const int32_t m : rr) { w->hab[size_t(m)] = 0.5f; }
	const int cu = advCulture(*w, Age::STONE);
	const int sid = advSettle(*w, home, cu, 380, 1);
	w->settlements[size_t(sid)].lastExpand = 0;
	w->settlements[size_t(sid)].pop = std::max(41.0, 0.95 * civ::capacity(*w, w->settlements[size_t(sid)]));
	Settlement probe;
	probe.cell = advLatLon(*w, 30, 0);
	probe.culture = cu;
	const double poorK = civ::capacity(*w, probe);
	probe.cell = rich;
	const double richK = civ::capacity(*w, probe);
	std::printf("    poor camp K %.1f, rich camp K %.1f\n", poorK, richK);
	advYear(*w);
	if (eq(int(dests.size()), 1, "one party")) {
		std::printf("    chosen site K estimate %.1f, %d hops from home\n", destK[0], advHops(*w, { home }, 99)[size_t(dests[0])]);
		ok(destK[0] >= 60, "estimated camp K >= 60");
		ok(std::find(rr.begin(), rr.end(), dests[0]) != rr.end(), "inside the only rich patch");
	}
	// every forage-only site: K below 60 -> no site at all
	for (int c = 0; c < w->n; c++) if (w->owner[size_t(c)] != sid && d[size_t(c)] > 3) w->hab[size_t(c)] = 0.12f;
	for (Settlement& s : w->settlements) if (!s.dead) { s.lastExpand = 0; s.pop = std::max(41.0, 0.95 * s.K); }
	dests.clear();
	advYear(*w);
	eq(int(dests.size()), 0, "no party when no camp reaches K 60");
	near(w->settlements[size_t(sid)].lastExpand, w->year - 15, 0, "failed search: retry in 10 years");
}

DM_TEST("economy adv: evacuation: spawn refused -> people stay and die with the town; 60 % exactly otherwise") {
	stub::Guard guard(true);
	if (!stub::stubbed("units") || !stub::stubbed("civ")) return;
	advInstall();
	int calls = 0;
	stub::hooks().units_spawn = [&](World&, const units::SpawnOpts&) -> Unit* { calls++; return nullptr; };
	auto w = advUniform(6);
	const int c = advLatLon(*w, 10, 30);
	w->elev[size_t(c)] = 0.2f;
	const int cu = advCulture(*w, Age::IRON);
	const int sid = advSettle(*w, c, cu, 1000, 2);
	const int sister = advSettle(*w, advLatLon(*w, 10, 30 + 20 * w->geo->step * 180 / kPi), cu, 300, 1);
	w->seaLevel = 0.3;
	advYear(*w);
	ok(w->settlements[size_t(sid)].dead && w->settlements[size_t(sid)].cause == Cause::Drowned, "drowned");
	eq(calls, 2, "tried a new site, then the sister town");
	near(w->settlements[size_t(sid)].pop, 1000, 1e-9, "nobody removed when nobody could leave");
	ok(!w->settlements[size_t(sister)].dead, "the sister lives");
	// a successful evacuation removes exactly 60 % (destroy keeps the rest as the loss)
	std::vector<double> sizes;
	stub::hooks().units_spawn = [&](World& ww, const units::SpawnOpts& o) -> Unit* {
		sizes.push_back(o.size);
		Unit& u = state::newUnit(ww, o.type);
		u.culture = o.culture; u.from = o.from; u.dest = o.dest; u.size = o.size;
		return &u;
	};
	auto w2 = advUniform(6);
	w2->elev[size_t(c)] = 0.2f;
	const int cu2 = advCulture(*w2, Age::STONE);
	const int s2 = advSettle(*w2, c, cu2, 777, 1);
	w2->seaLevel = 0.3;
	advYear(*w2);
	if (eq(int(sizes.size()), 1, "one party")) near(sizes[0], 0.6 * 777, 1e-9, "60 %");
	near(w2->settlements[size_t(s2)].pop, 0.4 * 777, 1e-9, "the remaining 40 % are the loss");
}

// ================================================================== determinism

DM_TEST("economy adv: two identical runs with plague, two cultures and expansion hash the same") {
	if (!advCanTeleport()) return;
	auto run = [](uint32_t seed) {
		stub::Guard guard;
		advInstall();
		auto w = makeWorld({ .L = 5, .seed = seed });
		const int cuA = advCulture(*w, Age::BRONZE), cuB = advCulture(*w, Age::IRON);
		w->vars.rel[state::relKey(cuA, cuB)].contact = true;
		int placed = 0;
		std::vector<int32_t> ring;
		for (int c = 0; c < w->n && placed < 10; c += 11) {
			if (state::isSea(*w, c) || w->hab[size_t(c)] < 0.2f) continue;
			w->geo->ring(c, 9, ring);
			bool free = true;
			for (const int32_t m : ring) if (w->owner[size_t(m)] >= 0) { free = false; break; }
			if (!free) continue;
			const int sid = advSettle(*w, c, placed % 2 ? cuA : cuB, 200, 1);
			w->settlements[size_t(sid)].lastExpand = -50;
			if (placed % 3 == 0) w->settlements[size_t(sid)].plague = 0.9;
			placed++;
		}
		for (int y = 0; y < 120; y++) advYear(*w);
		return std::make_pair(hash(*w), advLive(*w));
	};
	const auto a = run(4), b = run(4), c = run(9);
	std::printf("    hashes %08x %08x (other seed %08x); live %d / %d / %d\n", a.first, b.first, c.first, a.second, b.second, c.second);
	eq(a.first, b.first, "same inputs, same hash");
	ok(a.first != c.first, "a different world differs");
}

// ================================================================== a longer look (maps)

DM_TEST("economy adv: 600 years from one camp at L = 6 (teleporting settlers): growth, spread, maps") {
	if (!advCanTeleport()) return;
	stub::Guard guard;
	advInstall();
	auto w = makeWorld({ .L = 6, .seed = 4 });
	int best = -1;
	double bs = -1;
	for (int c = 0; c < w->n; c++) {
		if (w->cache.landId.empty() || w->cache.landId[size_t(c)] != 0 || !advCanSettle(*w, c).ok) continue;
		if (w->hab[size_t(c)] > bs) { bs = w->hab[size_t(c)]; best = c; }
	}
	const int cu = advCulture(*w, Age::STONE);
	if (!ok(civ::found(*w, best, cu, 30, FoundedBy::Awaken) != nullptr, "first camp")) return;
	int year60 = -1;
	double worst = 0;
	for (int y = 1; y <= 600; y++) {
		const double t0 = nowMs();
		advYear(*w);
		worst = std::max(worst, nowMs() - t0);
		if (year60 < 0 && advPop(*w) >= 60) year60 = y;
		if (y % 100 == 0) {
			int tiers[5] = { 0, 0, 0, 0, 0 };
			for (const Settlement& s : w->settlements) if (!s.dead) tiers[std::min(4, std::max(0, s.tier))]++;
			std::printf("    year %3d: pop %8.0f, %3d settlements (camps %d, villages %d, towns %d), age %s, science %.0f\n", y, advPop(*w),
				advLive(*w), tiers[0], tiers[1], tiers[2], kAgeNames[w->cultures[size_t(cu)].age], w->cultures[size_t(cu)].science);
		}
	}
	std::printf("    pop 60 in year %d; worst yearly %.2f ms (stand-ins included)\n", year60, worst);
	ok(year60 >= 1 && year60 <= 200, "60 people within 200 years");
	ok(advLive(*w) >= 10, "settlements spread");
	noNaN(*w);
	namespace mi = dm::mapimg;
	mi::owner(*w, shot("ue-economy-adv-owner-600.png"));
	mi::field(*w, "farm", shot("ue-economy-adv-farm-600.png"), 0, 1, { .dimSea = true });
	mi::custom(*w, shot("ue-economy-adv-K-600.png"), [&](int c) {
		if (state::isSea(*w, c)) return mi::Rgb{ 15, 30, 70 };
		const int o = w->owner[size_t(c)];
		if (o < 0) return mi::Rgb{ 60, 60, 60 };
		const Settlement& s = w->settlements[size_t(o)];
		const double t = clamp(std::log10(std::max(1.0, s.K)) / 4.0, 0.0, 1.0);
		return mi::Rgb{ 255 * t, 255 * (1 - std::fabs(t - 0.5) * 2), 255 * (1 - t) };
	});
}

// ================================================================== performance

DM_TEST("economy adv: worst-case yearly at L = 6: 158 settlements, Sail with ports, all eligible") {
	stub::Guard guard;
	advInstall();
	auto w = makeWorld({ .L = 6, .seed = 2 });
	const int cuA = advCulture(*w, Age::SAIL), cuB = advCulture(*w, Age::INDUSTRY);
	w->vars.rel[state::relKey(cuA, cuB)].war = true;
	std::vector<int32_t> ring;
	int placed = 0;
	for (int c = 0; c < w->n && placed < 158; c += 3) {
		if (state::isSea(*w, c) || w->hab[size_t(c)] < 0.1f || w->owner[size_t(c)] >= 0) continue;
		w->geo->ring(c, 3, ring);
		bool free = true;
		for (const int32_t m : ring) if (w->owner[size_t(m)] >= 0) { free = false; break; }
		if (!free) continue;
		const int tier = placed % 8 == 0 ? 3 : placed % 3 == 0 ? 2 : 1;
		const int sid = advSettle(*w, c, placed % 2 ? cuA : cuB, 100, tier);
		Settlement& s = w->settlements[size_t(sid)];
		s.b[Building::PORT] = 1;
		if (placed % 5 == 0) s.plague = 0.5;
		s.pop = std::max(80.0, 0.95 * civ::capacity(*w, s));
		placed++;
	}
	std::printf("    placed %d settlements\n", placed);
	double hookMs = 0;
	auto& h = stub::hooks();
	if (h.civ_claim) h.civ_claim = [&](World& ww, int x) { const double t = nowMs(); advClaim(ww, x); hookMs += nowMs() - t; };
	if (h.path_reach) h.path_reach = [&](const World& ww, int from, double mc, NavalMode nv, const path::Visit& v) {
		const double t = nowMs();
		double inside = 0;
		const int rv = advReach(ww, from, mc, nv, [&](int c, double cost) { const double t2 = nowMs(); const bool go = v(c, cost); inside += nowMs() - t2; return go; });
		hookMs += nowMs() - t - inside;
		return rv;
	};
	if (h.civ_canSettle) h.civ_canSettle = [&](const World& ww, int c) { const double t = nowMs(); const auto r = advCanSettle(ww, c); hookMs += nowMs() - t; return r; };
	if (h.civ_found) h.civ_found = [&](World& ww, int cell, int cx, double pop, FoundedBy by, const civ::FoundOpts& o) {
		const double t = nowMs(); Settlement* r = advFound(ww, cell, cx, pop, by, o); hookMs += nowMs() - t; return r; };
	w->year = 1000;
	// breakdown: the year without searches, and one bare naval flood (maxCost 120) from a port town
	{
		EconomyTune& T = w->tune<EconomyTune>();
		const int keep = T.searchesPerYear;
		T.searchesPerYear = 0;
		double base = 0;
		for (int y = 0; y < 5; y++) { const double t0 = nowMs(); advYear(*w); base += nowMs() - t0; }
		T.searchesPerYear = keep;
		int visited = 0;
		const double t1 = nowMs();
		const int origin = w->settlements[1].cell;
		if (!stub::stubbed("path")) path::reach(*w, origin, 120, NavalMode::Open, [&](int, double) { visited++; return true; });
		const double flood = nowMs() - t1;
		int validSites = 0;
		const double t2 = nowMs();
		for (int c = 0; c < w->n; c++) validSites += civ::canSettle(*w, c).ok ? 1 : 0;
		const double scan = nowMs() - t2;
		std::printf("    breakdown: year without searches %.3f ms; bare naval flood %.3f ms (%d cells); %d valid sites, canSettle %.4f ms/cell\n",
			base / 5, flood, visited, validSites, scan / w->n);
	}
	double total = 0, worst = 0, worstOwn = 0;
	const int years = 30;
	for (int y = 0; y < years; y++) {
		for (Settlement& s : w->settlements) if (!s.dead) s.lastExpand = -1000;   // everybody wants to expand
		const double t0 = nowMs(), h0 = hookMs;
		advYear(*w);
		const double dt = nowMs() - t0;
		total += dt;
		worst = std::max(worst, dt);
		worstOwn = std::max(worstOwn, dt - (hookMs - h0));
	}
	std::printf("    %s: %d live; yearly mean %.3f ms, worst %.3f ms; stand-ins %.3f ms/yr -> economy alone %.3f ms/yr (worst %.3f)\n",
		stub::stubbed("path") ? "stubs" : "real modules", advLive(*w), total / years, worst, hookMs / years, (total - hookMs) / years, worstOwn);
	ok((total - hookMs) / years <= 3 * 5, "within 5x the 3 ms budget");
	noNaN(*w);
}

// ================================================================== more rules

DM_TEST("economy adv: gate checks through the linked civ::claim (hop ring with stubs, Civ's round disc in all)") {
	stub::Guard guard;
	advInstall(false, false);
	auto w = advUniform(6);
	const int c = advLatLon(*w, 10, 30);
	struct Case { const char* name; int tier, age; double expect; };
	const Case cases[] = { { "camp / Stone", 0, Age::STONE, 131 }, { "village / Bronze", 1, Age::BRONZE, 638 },
		{ "town / Iron", 2, Age::IRON, 2104 }, { "city / Industry", 3, Age::INDUSTRY, 15700 }, { "metropolis / Space", 4, Age::SPACE, 105000 } };
	std::vector<int32_t> disc;
	for (const Case& k : cases) {
		// 65 % of the cells within 8 hops are arable (by hash), the rest hab 0.1
		w->geo->ring(c, 8, disc);
		std::vector<std::pair<double, int>> order;
		for (const int32_t m : disc) order.emplace_back(hashf(uint32_t(m), 5), m);
		std::sort(order.begin(), order.end());
		for (size_t j = 0; j < order.size(); j++) w->hab[size_t(order[j].second)] = (j % 20) < 13 ? 0.5f : 0.1f;
		const int cu = advCulture(*w, k.age);
		const int sid = advSettle(*w, c, cu, 10, k.tier);
		const Settlement& s = w->settlements[size_t(sid)];
		const double K = civ::capacity(*w, s);
		int arable = 0;
		for (const int32_t m : s.cells) arable += w->hab[size_t(m)] > 0.15f ? 1 : 0;
		std::printf("    %-20s %s claim: %3zu cells, %3d arable (%.0f %%), K %9.1f (spec %.0f, %+.1f %%)\n", k.name, stub::stubbed("civ") ? "test" : "Civ",
			s.cells.size(), arable, 100.0 * arable / std::max<size_t>(1, s.cells.size()), K, k.expect, 100 * (K / k.expect - 1));
		ok(std::fabs(K / k.expect - 1) <= 0.15, std::string(k.name) + " within 15 %");
		if (stub::stubbed("civ")) advDestroy(*w, sid, Cause::Abandoned); else civ::destroy(*w, sid, Cause::Abandoned);
	}
}

DM_TEST("economy adv: walls remember a past war; catch-up ignores dead cultures; world:age once per rise") {
	stub::Guard guard;
	advInstall(false, false);
	auto w = advUniform(5);
	w->tune<EconomyTune>().searchesPerYear = 0;
	const int cu = advCulture(*w, Age::BRONZE), foe = advCulture(*w, Age::BRONZE);
	const int sid = advSettle(*w, advLatLon(*w, 0, 0), cu, 100, 1);
	w->vars.rel[state::relKey(cu, foe)].war = true;
	advYear(*w);
	eq(int(w->settlements[size_t(sid)].b[Building::WALLS]), 0, "a village gets no walls");
	w->vars.rel[state::relKey(cu, foe)].war = false;   // peace
	for (int y = 0; y < 3; y++) advYear(*w);
	w->settlements[size_t(sid)].pop = 450;
	advYear(*w);
	eq(w->settlements[size_t(sid)].tier, 2, "town");
	advYear(*w);
	eq(int(w->settlements[size_t(sid)].b[Building::WALLS]), 1, "walls: the culture WAS at war");
	// catch-up: a dead culture an age ahead in contact gives nothing
	auto w2 = advUniform(5);
	w2->tune<EconomyTune>().searchesPerYear = 0;
	w2->tune<EconomyTune>().gate = { 0, 1e12, 1e12, 1e12, 1e12 };
	const int a = advCulture(*w2, Age::STONE), b = advCulture(*w2, Age::IRON), d = advCulture(*w2, Age::IRON);
	w2->cultures[size_t(d)].dead = true;
	w2->vars.rel[state::relKey(a, d)].contact = true;
	for (int k = 0; k < 12; k++) advSettle(*w2, advLatLon(*w2, -50 + 9 * k, 25 * k), a, 150, 0);
	const int sb = advSettle(*w2, advLatLon(*w2, 70, 180), b, 150, 0);
	(void)sb;
	const double s0 = w2->cultures[size_t(a)].science;
	advYear(*w2);
	const double gain = w2->cultures[size_t(a)].science - s0;
	std::printf("    research with only a dead culture ahead in contact: %.4f / yr\n", gain);
	near(gain, 1.0, 1e-9, "no catch-up from the dead");
	// two cultures crossing into Bronze in the same year: one world:age
	auto w3 = advUniform(5);
	w3->tune<EconomyTune>().searchesPerYear = 0;
	const int x = advCulture(*w3, Age::STONE), y2 = advCulture(*w3, Age::STONE);
	advSettle(*w3, advLatLon(*w3, 0, 0), x, 150, 0);
	advSettle(*w3, advLatLon(*w3, 0, 120), y2, 150, 0);
	w3->cultures[size_t(x)].science = 419.9; w3->cultures[size_t(y2)].science = 419.9;
	std::vector<Event> ev = capture(*w3, [&] { advYear(*w3); });
	eq(advCount(ev, Ev::CultureAge), 2, "two culture:age");
	eq(advCount(ev, Ev::WorldAge), 1, "one world:age");
	eq(w3->age, int(Age::BRONZE), "world age Bronze");
}

DM_TEST("economy adv: a saved and loaded world (no territory caches, no scratch) carries on with the same K") {
	stub::Guard guard;
	advInstall(false, false);
	auto w = makeWorld({ .L = 5, .seed = 6 });
	w->tune<EconomyTune>().searchesPerYear = 0;
	const int cu = advCulture(*w, Age::IRON);
	std::vector<int> ids;
	std::vector<int32_t> ring;
	for (int c = 0; c < w->n && ids.size() < 6; c += 17) {
		if (state::isSea(*w, c) || w->hab[size_t(c)] < 0.25f) continue;
		w->geo->ring(c, 6, ring);
		bool free = true;
		for (const int32_t m : ring) if (w->owner[size_t(m)] >= 0) { free = false; break; }
		if (free) ids.push_back(advSettle(*w, c, cu, 300, 1));
	}
	for (int y = 0; y < 5; y++) advYear(*w);
	std::vector<uint8_t> bytes;
	if (!ok(state::serialize(*w, bytes), "serialize")) return;
	std::string why;
	auto back = state::deserialize(bytes, &why);
	if (!ok(back != nullptr, "deserialize: " + why)) return;
	back->geo = w->geo;
	fillBasics(*back);   // the derived fields, as Sim.rederive would
	watch(*back);
	for (const Settlement& s : back->settlements) ok(s.cells.empty(), "cells are transient");
	advYear(*w);
	advYear(*back);
	for (const int sid : ids) {
		const Settlement& a = w->settlements[size_t(sid)];
		const Settlement& b = back->settlements[size_t(sid)];
		ok(!b.dead && !b.cells.empty(), "re-claimed after load");
		near(b.K, a.K, 0.02 * a.K + 1, "K after load (fields quantized)");
		near(b.pop, a.pop, 0.01 * a.pop + 0.5, "pop after load");
	}
	noNaN(*back);
}