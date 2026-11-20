// DEMIURGE — SimTests/test_economy.cpp : the economy half of Civ (SPEC-SIM 3.7, BUILD.md A8). Owner: A8 economy.
//
// Runs with every other module stubbed (`run.ps1 economy`). Where a collaborator must ACT (Civ.claim / found /
// destroy / infect / cure, Path.reach, Units.spawn teleporting settlers) a small reference implementation written
// out here from the spec is installed as a stub hook, but only for modules that are actually stubs: in
// `run.ps1 all` the real modules are used. Tests that need the teleporting Units.spawn or a controllable
// Path.reach return early when those modules are real.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <queue>
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

// ------------------------------------------------------------------ reference collaborators (hooks)

std::vector<int32_t>& tmpRing() { static std::vector<int32_t> r; return r; }

void refClaim(World& w, int sid) {
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
	w.dirty.color = true;
}
civ::SettleCheck refCanSettle(const World& w, int c) {
	if (!w.valid(c) || state::isSea(w, c) || hydro::isLake(w, c)) return { false, civ::SettleReason::Sea };
	if (!(w.hab[size_t(c)] > 0.05f)) return { false, civ::SettleReason::Barren };
	std::vector<int32_t> ring;
	w.geo->ring(c, 7, ring);
	for (const int32_t m : ring) if (w.owner[size_t(m)] >= 0) return { false, civ::SettleReason::Crowded };
	return { true, civ::SettleReason::Ok };
}
double refSiteScore(const World& w, int c) {
	if (!w.valid(c)) return 0;
	return w.hab[size_t(c)] + (w.fresh[size_t(c)] <= 1 ? 0.1 : 0.0) + (w.coast[size_t(c)] <= 2 ? 0.05 : 0.0);
}
Settlement* refFound(World& w, int cell, int cu, double pop, FoundedBy by, const civ::FoundOpts& o) {
	if (!refCanSettle(w, cell).ok) return nullptr;
	int liveN = 0;
	for (const Settlement& s : w.settlements) liveN += s.dead ? 0 : 1;
	if (liveN >= 160) return nullptr;
	Settlement& s = state::newSettlement(w, cell, cu);
	const int id = s.id;
	s.pop = pop;
	s.devotion = o.devotion.value_or(by == FoundedBy::Awaken ? 0.6 : 0.5);
	s.love = o.love.value_or(by == FoundedBy::Awaken ? 0.3 : 0.0);
	s.radius = 2;
	refClaim(w, id);
	w.settlements[size_t(id)].K = civ::capacity(w, w.settlements[size_t(id)]);
	if (Culture* c = state::culture(w, cu); c && c->capital < 0) { c->capital = id; w.settlements[size_t(id)].capital = true; }
	w.dirty.objects = true;
	w.bus.emit(EvSettlementFounded{ id, cell, cu, by });
	return &w.settlements[size_t(id)];
}
void refDestroy(World& w, int sid, Cause cause) {
	if (sid < 0 || size_t(sid) >= w.settlements.size() || w.settlements[size_t(sid)].dead) return;
	Settlement& s = w.settlements[size_t(sid)];
	s.dead = true; s.deadYear = w.year; s.cause = cause;
	for (const int32_t c : s.cells) if (w.owner[size_t(c)] == sid) w.owner[size_t(c)] = -1;
	s.cells.clear();
	w.bus.emit(EvSettlementDestroyed{ sid, s.cell, s.culture, cause });
}
bool refInfect(World& w, int sid, double sev) {
	if (sid < 0 || size_t(sid) >= w.settlements.size()) return false;
	Settlement& s = w.settlements[size_t(sid)];
	if (s.dead || s.immune > 0) return false;
	const bool was = s.plague > 0;
	s.plague = std::max(s.plague, sev);
	if (!was) w.bus.emit(EvSettlementPlague{ sid, true });
	return true;
}
bool refCure(World& w, int sid, double imm) {
	if (sid < 0 || size_t(sid) >= w.settlements.size()) return false;
	Settlement& s = w.settlements[size_t(sid)];
	if (s.dead) return false;
	const bool was = s.plague > 0;
	s.plague = 0; s.immune = imm;
	if (was) w.bus.emit(EvSettlementPlague{ sid, false });
	return true;
}
// Dijkstra flood with the Path rule's main terms (land 1 + 6 x height difference; coastal 0.6 on shallow
// sea, open 0.35 on any sea; embarking / landing 3).
int refReach(const World& w, int from, double maxCost, NavalMode naval, const path::Visit& visit) {
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
// Settlers arrive at once: found at dest (or join `to`), then a unit record is returned.
Unit* teleport(World& w, const units::SpawnOpts& o) {
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

// Installs the reference collaborators for every module that is a stub in this build.
void installRefs(bool withUnits = true, bool withPath = true) {
	auto& h = stub::hooks();
	if (stub::stubbed("civ")) {
		h.civ_claim = refClaim;
		h.civ_canSettle = refCanSettle;
		h.civ_siteScore = refSiteScore;
		h.civ_found = refFound;
		h.civ_destroy = refDestroy;
		h.civ_infect = refInfect;
		h.civ_cure = refCure;
	}
	if (withPath && stub::stubbed("path")) h.path_reach = refReach;
	if (withUnits && stub::stubbed("units")) h.units_spawn = teleport;
}
bool canTeleport() { return stub::stubbed("units") && stub::stubbed("path"); }

int countEv(const std::vector<Event>& ev, Ev t) {
	int k = 0;
	for (const Event& e : ev) k += e.type == t ? 1 : 0;
	return k;
}

// ------------------------------------------------------------------ synthetic worlds

// Flat land everywhere (elev 1, sea level 0): temp 17, moist 0.336 (suit = 0.8), fert 0.6, hab 0.5, no
// vegetation or wildlife, no fresh water, far from the coast.
std::unique_ptr<World> uniform(int L) {
	auto w = state::create({ .seed = 7, .L = L, .geo = sharedGeo(L) });
	watch(*w);
	w->phase = Phase::History;
	w->heat = 0;
	w->seaLevel = 0;
	w->vars.seaBase = 0;
	for (int c = 0; c < w->n; c++) {
		const size_t i = size_t(c);
		w->elev[i] = 1; w->temp[i] = 17; w->moist[i] = 0.336f; w->fert[i] = 0.6f; w->hab[i] = 0.5f;
		w->veg[i] = 0; w->fauna[i] = 0; w->fresh[i] = 255; w->coast[i] = 255; w->biome[i] = Biome::GRASSLAND;
		w->slope[i] = 0;
	}
	return w;
}
int cellLatLon(const World& w, double latDeg, double lonDeg) {
	const double la = latDeg * kPi / 180, lo = lonDeg * kPi / 180;
	return w.geo->cellAt(std::cos(la) * std::cos(lo), std::sin(la), std::cos(la) * std::sin(lo));
}
// Exactly round(0.65 N) of the cells within `hops` of c are arable (hab 0.5), the rest hab 0.1; ranking by hash.
int makeArable(World& w, int c, int hops, double frac) {
	std::vector<int32_t> ring;
	w.geo->ring(c, hops, ring);
	std::vector<std::pair<double, int>> order;
	for (const int32_t m : ring) order.emplace_back(hashf(uint32_t(m), 77), m);
	std::sort(order.begin(), order.end());
	const int nA = int(std::lround(frac * double(ring.size())));
	for (size_t k = 0; k < order.size(); k++) w.hab[size_t(order[k].second)] = int(k) < nA ? 0.5f : 0.1f;
	return nA;
}
int newCultureAt(World& w, int age, double curiosity = 0.5) {
	Culture& cu = state::newCulture(w);
	cu.age = age;
	cu.traits.curiosity = curiosity;
	cu.name = "Folk";
	return cu.id;
}
int settle(World& w, int cell, int cu, double pop, int tier = 0) {
	Settlement& s = state::newSettlement(w, cell, cu);
	s.pop = pop;
	s.tier = tier;
	s.radius = w.tune<EconomyTune>().radius[size_t(tier)];
	const int id = s.id;
	refClaim(w, id);
	return id;
}
std::vector<int> bfsHops(const World& w, const std::vector<int>& src, int cap) {
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
double worldPop(const World& w) {
	double p = 0;
	for (const Settlement& s : w.settlements) if (!s.dead) p += s.pop;
	return p;
}
int liveCount(const World& w) {
	int k = 0;
	for (const Settlement& s : w.settlements) k += s.dead ? 0 : 1;
	return k;
}
// Equirectangular crop (span x span/2 degrees) centred on a cell, 1024 x 512.
void zoomPng(const World& w, const std::string& file, int centre, double span, const std::function<mapimg::Rgb(int)>& col) {
	const double lat0 = w.geo->lat[size_t(centre)] * 180 / kPi, lon0 = w.geo->lon[size_t(centre)] * 180 / kPi;
	const int W = 1024, H = 512;
	std::vector<uint8_t> rgb(size_t(W) * H * 3);
	int hint = centre;
	for (int py = 0; py < H; py++) for (int px = 0; px < W; px++) {
		const double la = (lat0 + span / 4 - (span / 2) * (py + 0.5) / H) * kPi / 180, lo = (lon0 - span / 2 + span * (px + 0.5) / W) * kPi / 180;
		hint = w.geo->cellAt(std::cos(la) * std::cos(lo), std::sin(la), std::cos(la) * std::sin(lo), hint);
		const mapimg::Rgb c = col(hint);
		const size_t o3 = (size_t(py) * W + size_t(px)) * 3;
		rgb[o3] = uint8_t(clamp(c.r, 0, 255)); rgb[o3 + 1] = uint8_t(clamp(c.g, 0, 255)); rgb[o3 + 2] = uint8_t(clamp(c.b, 0, 255));
	}
	mapimg::writePng(file, W, H, rgb);
}
// One year as Sim would schedule it for the economy (settlers have arrived: no units remain).
void year(World& w) {
	w.year += 1;
	w.units.clear();
	civ::yearly(w);
}

} // namespace

// ================================================================== capacity

DM_TEST("economy: gate checks on a uniform territory (fert 0.6, suit 0.8, 65 % arable)") {
	auto w = uniform(6);
	const int c = cellLatLon(*w, 10, 30);
	std::vector<int32_t> r6;
	w->geo->ring(c, 6, r6);
	if (!eq(int(r6.size()), 127, "no pentagon within 6 hops")) return;
	struct Case { const char* name; int tier, age; double expect; };
	const Case cases[] = { { "camp / Stone", 0, Age::STONE, 131 }, { "village / Bronze", 1, Age::BRONZE, 638 },
		{ "town / Iron", 2, Age::IRON, 2104 }, { "city / Industry", 3, Age::INDUSTRY, 15700 },
		{ "metropolis / Space", 4, Age::SPACE, 105000 } };
	const EconomyTune& T = w->tune<EconomyTune>();
	for (const Case& k : cases) {
		const int hops = T.radius[size_t(k.tier)];
		const int nA = makeArable(*w, c, hops, 0.65);
		const int cu = newCultureAt(*w, k.age);
		Settlement cand;
		cand.cell = c; cand.culture = cu; cand.tier = k.tier;
		const double est = civ::capacity(*w, cand);   // estimated territory (no cells)
		const int sid = settle(*w, c, cu, 10, k.tier);
		const double K = civ::capacity(*w, w->settlements[size_t(sid)]);
		const double exact = nA * 22 * 0.6 * 0.8 * T.techYield[size_t(k.age)];
		std::printf("    %-20s cells %3zu arable %3d  K %9.1f  (spec ~%.0f, %+.1f %%)\n", k.name,
			w->settlements[size_t(sid)].cells.size(), nA, K, k.expect, 100 * (K / k.expect - 1));
		near(K, exact, 1e-6 * exact, std::string(k.name) + ": K = arable x farmPot");
		near(est, K, 1e-6 * K, std::string(k.name) + ": estimate = claimed territory");
		ok(std::fabs(K / k.expect - 1) <= 0.15, std::string(k.name) + " within 15 % of the gate value");
		// K never depends on farm[] or on the population
		for (const int32_t m : w->settlements[size_t(sid)].cells) w->farm[size_t(m)] = 1;
		w->settlements[size_t(sid)].pop = 1e6;
		near(civ::capacity(*w, w->settlements[size_t(sid)]), K, 1e-9 * K, std::string(k.name) + ": independent of farm and pop");
		refDestroy(*w, sid, Cause::Abandoned);
		for (int m = 0; m < w->n; m++) w->farm[size_t(m)] = 0;
	}
	noNaN(*w);
}

DM_TEST("economy: forage, fishing, port, granary and plant terms") {
	auto w = uniform(5);
	const int c = cellLatLon(*w, 0, 50);
	std::vector<int32_t> r3;
	w->geo->ring(c, 3, r3);
	for (const int32_t m : r3) if (m != c) { w->elev[size_t(m)] = -1; w->fauna[size_t(m)] = 0.5f; }
	w->hab[size_t(c)] = 0.1f;          // not arable: forage only
	w->veg[size_t(c)] = 0.5f; w->fauna[size_t(c)] = 0.25f;
	const int deg = w->geo->degree(c);
	const int cu = newCultureAt(*w, Age::STONE);
	const int sid = settle(*w, c, cu, 10, 0);
	eq(int(w->settlements[size_t(sid)].cells.size()), 1, "an island of one cell");
	const double forage = 3 * (0.6 * 0.5 + 0.4 * 0.25);
	const double fish0 = deg * 4 * 0.5;
	near(civ::capacity(*w, w->settlements[size_t(sid)]), forage + fish0, 1e-5, "Stone: forage + fish");
	w->settlements[size_t(sid)].b[Building::PORT] = 1;
	near(civ::capacity(*w, w->settlements[size_t(sid)]), forage + 2 * fish0, 1e-5, "port doubles fish");
	w->cultures[size_t(cu)].age = Age::INDUSTRY;   // techYield 25 -> fish x sqrt(min(25, 10))
	near(civ::capacity(*w, w->settlements[size_t(sid)]), forage + 2 * fish0 * std::sqrt(10.0), 1e-4, "fish tech capped at 10");
	w->settlements[size_t(sid)].b[Building::GRANARY] = 1;
	w->settlements[size_t(sid)].b[Building::PLANT] = 1;
	near(civ::capacity(*w, w->settlements[size_t(sid)]), (forage + 2 * fish0 * std::sqrt(10.0)) * 1.15 * 1.25, 1e-4, "granary x 1.15, plant x 1.25");
	// a lake cell is never arable; farmPot vs forage takes the max
	w->hab[size_t(c)] = 0.5f; w->water[size_t(c)] = 0.5f;
	ok(hydro::isLake(*w, c), "centre is a lake cell");
	const double kLake = civ::capacity(*w, w->settlements[size_t(sid)]);
	near(kLake, (forage + 2 * fish0 * std::sqrt(10.0)) * 1.15 * 1.25, 1e-4, "lake: no farm potential");
	// bad input
	Settlement bad;
	ok(civ::capacity(*w, bad) == 0, "invalid cell -> 0");
	bad.cell = c; bad.tier = 99; bad.culture = 1234;
	ok(std::isfinite(civ::capacity(*w, bad)), "odd tier / culture tolerated");
}

// ================================================================== growth, famine, plague, countdowns

DM_TEST("economy: logistic growth, its rate terms and the famine counter / event") {
	stub::Guard guard;
	installRefs(false, false);
	auto w = uniform(6);
	w->tune<EconomyTune>().searchesPerYear = 0;
	const int c = cellLatLon(*w, 10, 30);
	makeArable(*w, c, 2, 0.65);
	const int cu = newCultureAt(*w, Age::STONE);
	const int sid = settle(*w, c, cu, 30, 0);
	w->settlements[size_t(sid)].love = 0.3;
	year(*w);
	const Settlement& s = w->settlements[size_t(sid)];
	const double K = s.K;
	near(K, 12 * 22 * 0.6 * 0.8, 1e-6 * K, "K (fert is stored as float)");
	const double r = 0.03 * (1 + 0.2 * 0.3);
	near(s.pop, 30 + r * 30 * (1 - 30 / K), 1e-9, "logistic step with love 0.3");
	near(s.growth, s.pop - 30, 1e-12, "growth = change");
	eq(s.famine, 0, "no famine");
	// blessed + granary
	w->settlements[size_t(sid)].pop = 50; w->settlements[size_t(sid)].blessed = 3; w->settlements[size_t(sid)].b[Building::GRANARY] = 1;
	year(*w);
	const double K2 = w->settlements[size_t(sid)].K;
	// (v3, design review) blessed 3 years left: K x (1 + 0.2 x 3 / 10) as the blessing fades
	near(K2, K * 1.15 * (1 + 0.2 * 0.3), 1e-6, "granary raises K (and the fading blessing)");
	const double r2 = 0.03 * (1 + 0.3 + 0.3 + 0.06);
	near(w->settlements[size_t(sid)].pop, 50 + r2 * 50 * (1 - 50 / K2), 1e-9, "granary + blessed rate");
	near(w->settlements[size_t(sid)].blessed, 2, 0, "blessed counts down");
	// famine: as a village (Stone age: no town however many people), 2.5 x K -> 12 % per year, counter, one event at 3
	w->settlements[size_t(sid)].blessed = 0;   // (v3: a blessed town counts no famine years)
	w->settlements[size_t(sid)].tier = 1;
	w->settlements[size_t(sid)].pop = 100;
	year(*w);
	const double K3 = w->settlements[size_t(sid)].K;
	ok(K3 > K2 * 1.5, "village territory");
	w->settlements[size_t(sid)].pop = 2.5 * K3;
	std::vector<Event> ev = capture(*w, [&] { for (int y = 0; y < 5; y++) year(*w); });
	eq(w->settlements[size_t(sid)].famine, 5, "famine counter");
	eq(countEv(ev, Ev::SettlementFamine), 1, "settlement:famine once (at 3)");
	near(w->settlements[size_t(sid)].pop, 2.5 * K3 * std::pow(0.88, 5), 1e-6 * K3, "decline 12 %/yr while far above K");
	noNaN(*w);
}

DM_TEST("economy: halving K declines at most 12 %/yr and stabilises; K = 0 never NaN") {
	stub::Guard guard;
	installRefs(false, false);
	auto w = uniform(6);
	w->tune<EconomyTune>().searchesPerYear = 0;   // no settlers (with the real Units they would leave)
	const int c = cellLatLon(*w, 10, 30);         // every cell arable: any territory shape yields the same K
	const int cu = newCultureAt(*w, Age::STONE);
	const int sid = settle(*w, c, cu, 300, 1);   // village (radius 3)
	year(*w);
	const double K = w->settlements[size_t(sid)].K;
	w->settlements[size_t(sid)].pop = K;
	year(*w);
	near(w->settlements[size_t(sid)].pop, K, 1e-6, "at K the population holds");
	for (int m = 0; m < w->n; m++) w->fert[size_t(m)] = 0.3f;   // farmPot halves
	double prev = w->settlements[size_t(sid)].pop, worst = 0;
	int lastFamine = 0;
	for (int y = 0; y < 40; y++) {
		year(*w);
		const double p = w->settlements[size_t(sid)].pop;
		worst = std::max(worst, 1 - p / prev);
		prev = p;
		lastFamine = w->settlements[size_t(sid)].famine;
	}
	const double K2 = w->settlements[size_t(sid)].K;
	std::printf("    K %.1f -> %.1f; worst yearly decline %.2f %%; final pop %.2f; famine counter %d\n", K, K2, 100 * worst, prev, lastFamine);
	for (int m = 0; m < w->n; m++) w->fert[size_t(m)] = 0.6f;   // same territory at full fertility
	const double Kfull = civ::capacity(*w, w->settlements[size_t(sid)]);
	for (int m = 0; m < w->n; m++) w->fert[size_t(m)] = 0.3f;
	near(K2, Kfull / 2, 1e-6 * K, "K halved (on the current territory)");
	ok(worst <= 0.12 + 1e-12, "decline <= 12 %/yr");
	ok(std::fabs(prev / K2 - 1) < 0.01, "stabilised at the new K");
	ok(!w->settlements[size_t(sid)].dead, "no death spiral");
	eq(lastFamine, 0, "famine over once settled");
	// K = 0: nothing to eat at all
	for (int m = 0; m < w->n; m++) w->hab[size_t(m)] = 0.1f;
	int years = 0;
	while (!w->settlements[size_t(sid)].dead && years < 200) { year(*w); years++; }
	std::printf("    K = 0: abandoned after %d years (cause %s)\n", years, toStr(w->settlements[size_t(sid)].cause));
	eq(w->settlements[size_t(sid)].K, 0.0, "K = 0");
	ok(w->settlements[size_t(sid)].dead, "destroyed below 3 people");
	eq(int(w->settlements[size_t(sid)].cause), int(Cause::Famine), "cause famine");
	noNaN(*w);
}

DM_TEST("economy: plague deaths, fading, cure below 0.05, spread within 8 hops via civ::infect") {
	stub::Guard guard(true);
	installRefs(false, false);
	auto w = uniform(6);
	w->tune<EconomyTune>().searchesPerYear = 0;
	w->tune<EconomyTune>().plagueSpreadP = 2;   // chance 2 x plague > 1: the spread is certain for the test
	const int a = cellLatLon(*w, 10, 30);
	const double hop = w->geo->step;
	const int b = cellLatLon(*w, 10, 30 + 6 * hop * 180 / kPi / std::cos(10 * kPi / 180));     // ~6 hops east
	const int far = cellLatLon(*w, 10, 30 + 13 * hop * 180 / kPi / std::cos(10 * kPi / 180)); // ~13 hops east
	const int cu = newCultureAt(*w, Age::STONE);
	const int sa = settle(*w, a, cu, 1000, 1), sb = settle(*w, b, cu, 200, 1), sf = settle(*w, far, cu, 200, 1);
	const int hb = bfsHops(*w, { a }, 99)[size_t(b)], hf = bfsHops(*w, { a }, 99)[size_t(far)];
	std::printf("    hops a->b %d, a->far %d\n", hb, hf);
	ok(hb <= 8 && hf > 9, "geometry");
	w->settlements[size_t(sa)].plague = 1.0;
	const double p0 = w->settlements[size_t(sa)].pop;
	std::vector<Event> ev = capture(*w, [&] { year(*w); });
	const Settlement& s = w->settlements[size_t(sa)];
	const double Keff = std::max(s.K, 1.0);
	double grown = p0;
	if (p0 <= Keff) grown = p0 + 0.03 * p0 * (1 - p0 / Keff); else grown = p0 - std::min(0.12 * p0, 0.5 * (p0 - Keff));
	near(s.pop, grown * (1 - 0.08), 1e-6, "deaths = plague x 8 %");
	near(s.plague, 0.85, 1e-12, "fades 15 %/yr");
	ok(w->settlements[size_t(sb)].plague > 0, "spread to the settlement within 8 hops");
	near(w->settlements[size_t(sb)].plague, 0.8 * 0.85, 1e-12, "spread severity 0.8 x the source's (faded) plague");
	eq(w->settlements[size_t(sf)].plague, 0.0, "not beyond 8 hops");
	if (stub::stubbed("civ")) ok(stub::called("civ_infect") >= 1, "through civ::infect");
	eq(countEv(ev, Ev::SettlementPlague), 1, "one plague start");
	int y = 1;
	while (w->settlements[size_t(sa)].plague > 0 && y < 60) { year(*w); y++; }
	std::printf("    plague 1.0 cured after %d years (0.85^n < 0.05 -> n = 19)\n", y);
	eq(y, 19, "cured in the year it fell below 0.05");
	if (stub::stubbed("civ")) ok(stub::called("civ_cure") >= 1, "through civ::cure");
	near(w->settlements[size_t(sa)].immune, 30, 0, "immune 30 years");
	year(*w);
	near(w->settlements[size_t(sa)].immune, 29, 0, "immunity counts down");
	noNaN(*w);
}

DM_TEST("economy: blessed, inspire, immune count down to 0") {
	stub::Guard guard;
	installRefs(false, false);
	auto w = uniform(5);
	w->tune<EconomyTune>().searchesPerYear = 0;
	const int cu = newCultureAt(*w, Age::STONE);
	const int sid = settle(*w, cellLatLon(*w, 0, 0), cu, 30, 0);
	Settlement& s = w->settlements[size_t(sid)];
	s.blessed = 5; s.inspire = 2.5; s.immune = 1;
	year(*w); year(*w); year(*w);
	near(w->settlements[size_t(sid)].blessed, 2, 0, "blessed");
	near(w->settlements[size_t(sid)].inspire, 0, 0, "inspire floors at 0");
	near(w->settlements[size_t(sid)].immune, 0, 0, "immune floors at 0");
}

// ================================================================== tiers, buildings, land use

DM_TEST("economy: tier gates by age, hysteresis (no flapping around 60), radius and re-claim") {
	stub::Guard guard(true);
	installRefs(false, false);
	auto w = uniform(6);
	w->tune<EconomyTune>().searchesPerYear = 0;
	const int c = cellLatLon(*w, 10, 30);
	const int cu = newCultureAt(*w, Age::STONE);
	const int sid = settle(*w, c, cu, 58, 0);
	std::vector<Event> ev = capture(*w, [&] {
		year(*w);
		ok(w->settlements[size_t(sid)].tier == 0, "58 -> still a camp");
		w->settlements[size_t(sid)].pop = 61;
		year(*w);
		ok(w->settlements[size_t(sid)].tier == 1, "61 -> village");
		eq(w->settlements[size_t(sid)].radius, 3, "village radius 3");
		ok(w->settlements[size_t(sid)].cells.size() > 19, "re-claimed at radius 3");
		for (int y = 0; y < 30; y++) { w->settlements[size_t(sid)].pop = (y % 2) ? 62 : 45; year(*w); }
		ok(w->settlements[size_t(sid)].tier == 1, "oscillating 45..62 stays a village");
	});
	eq(countEv(ev, Ev::SettlementTier), 1, "one tier event");
	// town needs Bronze
	w->settlements[size_t(sid)].pop = 500;
	year(*w);
	eq(w->settlements[size_t(sid)].tier, 1, "500 people in the Stone age: still a village");
	w->cultures[size_t(cu)].age = Age::BRONZE;
	w->settlements[size_t(sid)].pop = 500;
	ev = capture(*w, [&] { year(*w); });
	eq(w->settlements[size_t(sid)].tier, 2, "Bronze: town");
	eq(countEv(ev, Ev::SettlementTier), 1, "settlement:tier");
	if (!ev.empty()) {
		for (const Event& e : ev) if (const EvSettlementTier* t = e.as<EvSettlementTier>()) { eq(t->tier, 2); eq(t->prev, 1); }
	}
	// demotion below half the current gate (200 for a town)
	w->settlements[size_t(sid)].pop = 210; year(*w);
	eq(w->settlements[size_t(sid)].tier, 2, "210 >= 200 keeps the town");
	w->settlements[size_t(sid)].pop = 150; year(*w);
	eq(w->settlements[size_t(sid)].tier, 1, "150 < 200 demotes");
	// staggered re-claim every 10 years
	const int before = stub::called("civ_claim");
	for (int y = 0; y < 20; y++) { w->settlements[size_t(sid)].pop = 300; year(*w); }
	if (stub::stubbed("civ")) eq(stub::called("civ_claim") - before, 2, "re-claimed twice in 20 years");
}

namespace {
struct BCase {
	const char* name;
	int age, tier;
	double pop, devotion;
	bool war, coast, rock, capital;
	std::vector<Building::Id> expect;
};
} // namespace

DM_TEST("economy: buildings appear when their conditions hold, one event each") {
	stub::Guard guard;
	installRefs(false, false);
	using B = Building::Id;
	const std::vector<BCase> cases = {
		{ "Stone camp, pop 30, devotion 0.3", Age::STONE, 0, 30, 0.3, false, false, false, false, { B::SHRINE } },
		{ "Stone camp, devotion 0.29", Age::STONE, 0, 30, 0.29, false, false, false, false, {} },
		{ "Stone camp, pop 20", Age::STONE, 0, 20, 0.6, false, false, false, false, {} },
		{ "Bronze village", Age::BRONZE, 1, 100, 0.2, false, false, false, false, { B::GRANARY } },
		{ "Bronze camp", Age::BRONZE, 0, 40, 0.2, false, false, false, false, {} },
		{ "Bronze town, devotion 0.35", Age::BRONZE, 2, 600, 0.35, false, false, false, false, { B::SHRINE, B::TEMPLE, B::GRANARY } },
		{ "Bronze town at war", Age::BRONZE, 2, 600, 0.2, true, false, false, false, { B::GRANARY, B::WALLS } },
		{ "Stone village at war", Age::STONE, 1, 100, 0.2, true, false, false, false, {} },
		{ "Iron coastal village", Age::IRON, 1, 100, 0.2, false, true, false, false, { B::GRANARY, B::PORT } },
		{ "Bronze coastal village", Age::BRONZE, 1, 100, 0.2, false, true, false, false, { B::GRANARY } },
		{ "Iron town", Age::IRON, 2, 600, 0.2, false, false, false, false, { B::GRANARY, B::MARKET } },
		{ "Iron camp with rock", Age::IRON, 0, 40, 0.2, false, false, true, false, { B::MINE } },
		{ "Bronze camp with rock", Age::BRONZE, 0, 40, 0.2, false, false, true, false, {} },
		{ "Sail town", Age::SAIL, 2, 600, 0.2, false, false, false, false, { B::GRANARY, B::MARKET } },
		{ "Sail city", Age::SAIL, 3, 2000, 0.2, false, false, false, false, { B::GRANARY, B::MARKET, B::ACADEMY } },
		{ "Industry city", Age::INDUSTRY, 3, 2000, 0.2, false, false, false, false, { B::GRANARY, B::MARKET, B::FACTORY, B::ACADEMY } },
		{ "Atomic city", Age::ATOMIC, 3, 2000, 0.2, false, false, false, false, { B::GRANARY, B::MARKET, B::FACTORY, B::ACADEMY, B::PLANT } },
		{ "Space city, not the capital", Age::SPACE, 3, 2000, 0.2, false, false, false, false, { B::GRANARY, B::MARKET, B::FACTORY, B::ACADEMY, B::PLANT } },
	};
	for (const BCase& k : cases) {
		auto w = uniform(5);
		w->tune<EconomyTune>().searchesPerYear = 0;
		const int c = cellLatLon(*w, 0, 40);
		if (k.coast) { const int sea = w->geo->nbr[size_t(6 * c)]; w->elev[size_t(sea)] = -1; }
		if (k.rock) { const int rk = w->geo->nbr[size_t(6 * c + 1)]; w->biome[size_t(rk)] = Biome::ROCK; }
		const int cu = newCultureAt(*w, k.age);
		if (k.war) {
			const int other = newCultureAt(*w, k.age);
			w->vars.rel[state::relKey(cu, other)].war = true;
		}
		const int sid = settle(*w, c, cu, k.pop, k.tier);
		w->settlements[size_t(sid)].devotion = k.devotion;
		std::vector<Event> ev = capture(*w, [&] { year(*w); });
		std::vector<Building::Id> got;
		for (int b = 0; b < Building::COUNT; b++) if (w->settlements[size_t(sid)].b[size_t(b)]) got.push_back(Building::Id(b));
		std::vector<Building::Id> want = k.expect;
		std::sort(want.begin(), want.end());
		eq(got, want, k.name);
		eq(countEv(ev, Ev::SettlementBuilding), int(want.size()), std::string(k.name) + ": one event per building");
		ev = capture(*w, [&] { year(*w); });
		eq(countEv(ev, Ev::SettlementBuilding), 0, std::string(k.name) + ": no repeats");
	}
}

DM_TEST("economy: launch site 20 years after the capital's culture reached Space; factory CO2") {
	stub::Guard guard;
	installRefs(false, false);
	std::vector<double> emitted;
	if (stub::stubbed("climate")) stub::hooks().climate_emit = [&](World&, double a) { emitted.push_back(a); };
	auto w = uniform(5);
	w->tune<EconomyTune>().searchesPerYear = 0;
	const int cu = newCultureAt(*w, Age::ATOMIC);
	const int sid = settle(*w, cellLatLon(*w, 0, 40), cu, 4000, 3);
	w->settlements[size_t(sid)].capital = true;
	w->cultures[size_t(cu)].capital = sid;
	w->settlements[size_t(sid)].b[Building::FACTORY] = 1;
	w->year = 99;
	year(*w);                                         // year 100, Atomic
	if (stub::stubbed("climate")) {
		if (eq(int(emitted.size()), 1, "one emission per factory per year"))
			near(emitted[0], 0.0025 * std::sqrt(w->settlements[size_t(sid)].pop / 1000), 1e-12, "0.0025 x sqrt(pop / 1000)");
	}
	w->cultures[size_t(cu)].science = 519.9;          // Space next year
	year(*w);                                         // year 101: reaches Space
	eq(w->cultures[size_t(cu)].age, int(Age::SPACE), "reached Space");
	for (int y = 0; y < 19; y++) year(*w);            // year 120
	eq(int(w->settlements[size_t(sid)].b[Building::LAUNCH]), 0, "not yet after 19 years");
	year(*w);                                         // year 121 = 20 years
	eq(int(w->settlements[size_t(sid)].b[Building::LAUNCH]), 1, "launch site after 20 years");
	if (stub::stubbed("climate")) {
		near(emitted.back(), 0.5 * 0.0025 * std::sqrt(w->settlements[size_t(sid)].pop / 1000), 1e-12, "x 0.5 in the Space age");
	}
}

DM_TEST("economy: farms take the best cells until 1.15 x pop, lakes never; others relax; urban rings") {
	stub::Guard guard;
	installRefs(false, false);
	auto w = uniform(6);
	w->tune<EconomyTune>().searchesPerYear = 0;
	const int c = cellLatLon(*w, 10, 30);
	std::vector<int32_t> r2;
	w->geo->ring(c, 2, r2);
	for (size_t k = 0; k < r2.size(); k++) { w->fert[size_t(r2[k])] = float(0.3 + 0.04 * double(k)); w->veg[size_t(r2[k])] = 0.9f; }
	const int lake = r2[18];                               // the most fertile cell is a lake
	w->water[size_t(lake)] = 0.4f;
	w->farm[size_t(lake)] = 0.3f;
	const int other = r2[3];
	w->farm[size_t(other)] = 0.5f;                         // a poor cell farmed earlier
	const int cu = newCultureAt(*w, Age::STONE);
	const int sid = settle(*w, c, cu, 40, 0);
	w->settlements[size_t(sid)].devotion = 0.2;
	const double K0 = civ::capacity(*w, w->settlements[size_t(sid)]);
	year(*w);
	// expected set: arable non-lake cells by farmPot (fert) descending until the sum reaches 46
	std::vector<std::pair<double, int>> cand;
	for (const int32_t m : r2) if (m != lake) cand.emplace_back(-(22 * std::min(double(w->fert[size_t(m)]), 1.2) * 0.8), m);
	std::sort(cand.begin(), cand.end());
	double acc = 0;
	std::vector<int> want;
	for (const auto& e : cand) { if (acc >= 1.15 * 40) break; acc += -e.first; want.push_back(e.second); }
	std::sort(want.begin(), want.end());
	std::vector<int> got;
	for (const int32_t m : r2) if (w->farm[size_t(m)] > 0.2f && m != lake && m != other) got.push_back(m);
	if (std::find(want.begin(), want.end(), other) != want.end()) got.push_back(other);
	std::sort(got.begin(), got.end());
	std::printf("    farmed %zu cells for 46 people (summed farmPot %.1f)\n", want.size(), acc);
	eq(got, want, "farmed cells");
	for (const int m : want) near(w->farm[size_t(m)], 0.25, 1e-6, "farm +0.25 / yr");
	for (const int m : want) ok(w->veg[size_t(m)] <= 0.75f + 1e-6f, "veg lowered on farmland");
	near(w->farm[size_t(lake)], 0.2, 1e-6, "lake: never farmed, relaxes");
	if (std::find(want.begin(), want.end(), other) == want.end()) near(w->farm[size_t(other)], 0.4, 1e-6, "unused farm relaxes 0.1 / yr");
	near(civ::capacity(*w, w->settlements[size_t(sid)]), K0, 1e-9 * K0, "farming did not change K");
	for (int y = 0; y < 4; y++) { w->settlements[size_t(sid)].pop = 40; year(*w); }
	for (const int m : want) near(w->farm[size_t(m)], 1.0, 1e-6, "farm reaches 1");
	near(w->urban[size_t(c)], 0.1, 1e-6, "camp centre urban 0.1");
	// city and metropolis rings
	auto w2 = uniform(6);
	w2->tune<EconomyTune>().searchesPerYear = 0;
	const int cu2 = newCultureAt(*w2, Age::IRON);
	const int city = settle(*w2, c, cu2, 2000, 3);
	year(*w2);
	near(w2->urban[size_t(c)], 0.8, 1e-6, "city centre 0.8");
	for (int k = 0; k < w2->geo->nbrN[size_t(c)]; k++) near(w2->urban[size_t(w2->geo->nbr[size_t(6 * c + k)])], 0.5, 1e-6, "city ring 0.5");
	w2->cultures[size_t(cu2)].age = Age::INDUSTRY;
	w2->settlements[size_t(city)].pop = 20000;
	year(*w2);
	eq(w2->settlements[size_t(city)].tier, 4, "metropolis");
	year(*w2);
	near(w2->urban[size_t(c)], 1.0, 1e-6, "metropolis centre 1");
	std::vector<int32_t> rr;
	w2->geo->ring(c, 2, rr);
	for (size_t k = 1; k < rr.size(); k++) {
		const bool first = k <= size_t(w2->geo->nbrN[size_t(c)]);
		near(w2->urban[size_t(rr[k])], first ? 0.8 : 0.4, 1e-6, first ? "metropolis ring 1: 0.8" : "metropolis ring 2: 0.4");
	}
	noNaN(*w);
	noNaN(*w2);
}

// ================================================================== expansion

DM_TEST("economy: at most 2 searches per year, longest-waiting first; party size; cooldowns") {
	if (!canTeleport()) return;
	stub::Guard guard(true);
	installRefs();
	std::vector<units::SpawnOpts> spawns;
	std::vector<double> popAtSpawn;
	stub::hooks().units_spawn = [&](World& w, const units::SpawnOpts& o) -> Unit* {
		spawns.push_back(o);
		popAtSpawn.push_back(w.settlements[size_t(o.from)].pop);
		return teleport(w, o);
	};
	auto w = uniform(5);
	w->year = 100;
	const int cu = newCultureAt(*w, Age::STONE);
	std::vector<int> ids;
	for (int k = 0; k < 5; k++) {
		const int sid = settle(*w, cellLatLon(*w, 0, 72.0 * k), cu, 380, 1);
		w->settlements[size_t(sid)].lastExpand = 60 + 3 * k;   // 0 waited longest
		w->settlements[size_t(sid)].devotion = 0.2;
		ids.push_back(sid);
	}
	year(*w);   // year 101
	eq(stub::called("path_reach"), 2, "two searches");
	eq(int(spawns.size()), 2, "two parties");
	near(w->settlements[size_t(ids[0])].lastExpand, 101, 0, "longest-waiting searched");
	near(w->settlements[size_t(ids[1])].lastExpand, 101, 0, "second-longest searched");
	near(w->settlements[size_t(ids[2])].lastExpand, 66, 0, "third waits");
	for (size_t k = 0; k < spawns.size(); k++) {
		near(spawns[k].size, clamp(0.12 * popAtSpawn[k], 12, 500), 1e-9, "party = clamp(0.12 pop, 12, 500)");
		eq(int(spawns[k].naval), int(NavalMode::Land), "Stone age: on foot");
		near(w->settlements[size_t(spawns[k].from)].pop, popAtSpawn[k] - spawns[k].size, 1e-9, "people removed after spawn");
	}
	eq(liveCount(*w), 7, "two new camps");
	// the new camps were founded by settlers with the origin's devotion
	near(w->settlements[size_t(w->settlements.size() - 1)].devotion, 0.2, 1e-12, "devotion carried");
	year(*w);
	eq(stub::called("path_reach"), 4, "next year the next two");
	// party size bounds
	near(clamp(0.12 * 50, 12, 500), 12, 0);
	noNaN(*w);
}

DM_TEST("economy: sites respect owned-land distance >= 7 and reserved settler destinations >= 9 hops") {
	if (!canTeleport()) return;
	stub::Guard guard(true);
	installRefs();
	std::vector<int> dests;
	stub::hooks().units_spawn = [&](World& w, const units::SpawnOpts& o) -> Unit* { dests.push_back(o.dest); return teleport(w, o); };
	auto w = uniform(6);
	w->year = 200;
	const int cu = newCultureAt(*w, Age::STONE);
	const int home = cellLatLon(*w, 10, 30);
	const int sid = settle(*w, home, cu, 380, 1);
	w->settlements[size_t(sid)].lastExpand = 100;
	w->settlements[size_t(sid)].devotion = 0.2;
	// a neighbour settlement and a settler already on its way
	const double hop = w->geo->step * 180 / kPi;
	const int nb = settle(*w, cellLatLon(*w, 10 + 14 * hop, 30), cu, 50, 0);
	w->settlements[size_t(nb)].lastExpand = 200;
	Unit& u = state::newUnit(*w, UnitType::Settler);
	u.culture = cu; u.from = sid; u.dest = cellLatLon(*w, 10 - 11 * hop, 30);
	const int reserved = u.dest;
	std::vector<int> owned;
	for (int c = 0; c < w->n; c++) if (w->owner[size_t(c)] >= 0) owned.push_back(c);
	const std::vector<int> dOwn = bfsHops(*w, owned, 99), dRes = bfsHops(*w, { reserved }, 99);
	w->year += 1;
	civ::yearly(*w);   // keep the unit this time
	if (!eq(int(dests.size()), 1, "one party")) return;
	std::printf("    chosen site: %d hops from owned land, %d from the reserved destination, %d from home\n",
		dOwn[size_t(dests[0])], dRes[size_t(dests[0])], bfsHops(*w, { home }, 99)[size_t(dests[0])]);
	ok(dOwn[size_t(dests[0])] >= 7, "at least 7 hops from owned land");
	ok(dRes[size_t(dests[0])] >= 9, "at least 9 hops from a settler's destination");
	noNaN(*w);
}

DM_TEST("economy: failed search or failed spawn: nobody leaves, retry in 10 years") {
	if (!canTeleport()) return;
	stub::Guard guard(true);
	installRefs();
	int spawnCalls = 0;
	stub::hooks().units_spawn = [&](World&, const units::SpawnOpts&) -> Unit* { spawnCalls++; return nullptr; };
	auto w = uniform(5);
	w->year = 100;
	const int cu = newCultureAt(*w, Age::STONE);
	const int sid = settle(*w, cellLatLon(*w, 0, 0), cu, 380, 1);
	w->settlements[size_t(sid)].lastExpand = 50;
	w->settlements[size_t(sid)].devotion = 0.2;
	year(*w);
	eq(spawnCalls, 1, "a site was found and spawn tried");
	near(w->settlements[size_t(sid)].lastExpand, 101 - 15, 0, "spawn refused: retry like a failed search");
	ok(w->settlements[size_t(sid)].pop > 300, "nobody removed");
	// no site at all: the land around is barren
	for (int c = 0; c < w->n; c++) if (w->owner[size_t(c)] < 0) w->hab[size_t(c)] = 0.01f;
	stub::hooks().units_spawn = teleport;
	// a search is visible as a change of lastExpand (since the verifier's fix a search with no candidate
	// cell anywhere skips the Path flood, so path_reach is not called for it)
	int searches = 0;
	for (int y = 0; y < 21; y++) {   // years 102..122
		const double le = w->settlements[size_t(sid)].lastExpand;
		w->settlements[size_t(sid)].pop = 380;
		year(*w);
		searches += w->settlements[size_t(sid)].lastExpand != le ? 1 : 0;
	}
	std::printf("    barren surroundings: %d searches in 21 years, lastExpand %.0f\n", searches, w->settlements[size_t(sid)].lastExpand);
	eq(searches, 2, "searches at 111 and 121 (every 10 years)");
	near(w->settlements[size_t(sid)].lastExpand, 121 - 15, 0, "lastExpand = year - 15");
	eq(liveCount(*w), 1, "nobody left");
}

DM_TEST("economy: naval mode and reach: land 45; coastal from Bronze; open sea with a port from Sail (120); expand edict") {
	if (!canTeleport()) return;
	struct NCase { const char* name; int age; bool coast, port; NavalMode want; double cost; };
	const NCase cases[] = {
		{ "Stone coastal", Age::STONE, true, false, NavalMode::Land, 45 },
		{ "Bronze inland", Age::BRONZE, false, false, NavalMode::Land, 45 },
		{ "Bronze coastal", Age::BRONZE, true, false, NavalMode::Coastal, 45 },
		{ "Sail with port", Age::SAIL, true, true, NavalMode::Open, 120 },
		{ "Iron with port", Age::IRON, true, true, NavalMode::Coastal, 45 },
	};
	for (const NCase& k : cases) {
		stub::Guard guard(true);
		installRefs();
		NavalMode seen = NavalMode::Land;
		double seenCost = -1;
		stub::hooks().path_reach = [&](const World& w, int from, double maxCost, NavalMode naval, const path::Visit& v) {
			seen = naval; seenCost = maxCost; return refReach(w, from, maxCost, naval, v);
		};
		auto w = uniform(5);
		w->year = 100;
		const int c = cellLatLon(*w, 0, 0);
		if (k.coast) w->elev[size_t(w->geo->nbr[size_t(6 * c)])] = -0.3f;
		const int cu = newCultureAt(*w, k.age);
		const int sid = settle(*w, c, cu, 380, 1);
		w->settlements[size_t(sid)].lastExpand = 0;
		w->settlements[size_t(sid)].devotion = 0.2;
		if (k.port) w->settlements[size_t(sid)].b[Building::PORT] = 1;
		w->settlements[size_t(sid)].pop = 0.95 * civ::capacity(*w, w->settlements[size_t(sid)]);   // eligible at any tech
		year(*w);
		eq(stub::called("path_reach"), 1, k.name);
		eq(int(seen), int(k.want), std::string(k.name) + ": naval");
		near(seenCost, k.cost, 0, std::string(k.name) + ": maxCost");
	}
	// cooldown 25 years, 12 under the expand edict
	for (const bool edict : { false, true }) {
		stub::Guard guard(true);
		installRefs();
		auto w = uniform(5);
		w->year = 100;
		const int cu = newCultureAt(*w, Age::STONE);
		if (edict) w->cultures[size_t(cu)].edict = Edict::Expand;
		const int sid = settle(*w, cellLatLon(*w, 0, 0), cu, 380, 1);
		w->settlements[size_t(sid)].devotion = 0.2;
		w->settlements[size_t(sid)].lastExpand = 100;
		int first = -1;
		for (int y = 0; y < 30 && first < 0; y++) { w->settlements[size_t(sid)].pop = 380; year(*w); if (stub::called("path_reach") > 0) first = int(w->year); }
		eq(first, edict ? 112 : 125, edict ? "expand edict: 12 years" : "cooldown 25 years");
	}
}

DM_TEST("economy: hazards: sea rise evacuates 60 % then drowned; flooded centre; lava") {
	stub::Guard guard(true);
	installRefs();
	std::vector<units::SpawnOpts> spawns;
	const bool hookUnits = stub::stubbed("units");
	if (hookUnits) stub::hooks().units_spawn = [&](World& w, const units::SpawnOpts& o) -> Unit* {
		spawns.push_back(o);
		Unit& u = state::newUnit(w, o.type);
		u.culture = o.culture; u.from = o.from; u.dest = o.dest; u.size = o.size;
		return &u;
	};
	auto w = uniform(6);
	const int c = cellLatLon(*w, 10, 30);
	w->elev[size_t(c)] = 0.2f;
	const int cu = newCultureAt(*w, Age::IRON);
	const int sid = settle(*w, c, cu, 1000, 2);
	const int other = settle(*w, cellLatLon(*w, 10, 30 + 20 * w->geo->step * 180 / kPi), cu, 300, 1);
	w->seaLevel = 0.3;   // the sea rose over the centre (and nothing else)
	std::vector<Event> ev = capture(*w, [&] { year(*w); });
	ok(w->settlements[size_t(sid)].dead, "destroyed");
	eq(int(w->settlements[size_t(sid)].cause), int(Cause::Sea), "the risen sea took it (v3: cause 'sea', not the god's drowning)");
	if (hookUnits) {
		if (eq(int(spawns.size()), 1, "one evacuation party")) {
			near(spawns[0].size, 600, 1e-9, "60 % of the people");
			eq(int(spawns[0].type), int(UnitType::Settler), "settlers");
			eq(int(spawns[0].naval), int(NavalMode::Open), "by boat");
			const int hops = bfsHops(*w, { c }, 99)[size_t(spawns[0].dest)];
			std::printf("    evacuation site %d hops away (other town at ~20)\n", hops);
			ok(hops <= 12 && !state::isSea(*w, spawns[0].dest), "a land site within 12 hops");
			ok(spawns[0].to == -1, "a new site");
		}
	}
	eq(countEv(ev, Ev::SettlementDestroyed), 1, "settlement:destroyed");
	// no site within 12 hops: joins the nearest settlement of its culture
	spawns.clear();
	auto w2 = uniform(6);
	w2->elev[size_t(c)] = 0.2f;
	for (int m = 0; m < w2->n; m++) w2->hab[size_t(m)] = 0.01f;
	const int cu2 = newCultureAt(*w2, Age::IRON);
	const int s2 = settle(*w2, c, cu2, 1000, 2);
	const int o2 = settle(*w2, cellLatLon(*w2, 10, 30 + 20 * w2->geo->step * 180 / kPi), cu2, 300, 1);
	w2->seaLevel = 0.3;
	year(*w2);
	ok(w2->settlements[size_t(s2)].dead, "destroyed");
	if (hookUnits && eq(int(spawns.size()), 1, "one party")) { eq(spawns[0].to, o2, "joins its sister town"); near(spawns[0].size, 600, 1e-9); }
	(void)other;
	// flooded centre (a lake rose) and lava
	auto w3 = uniform(5);
	const int cu3 = newCultureAt(*w3, Age::IRON);
	const int a = settle(*w3, cellLatLon(*w3, 0, 0), cu3, 500, 1), b = settle(*w3, cellLatLon(*w3, 0, 90), cu3, 500, 1);
	w3->water[size_t(w3->settlements[size_t(a)].cell)] = 0.5f;
	w3->lava[size_t(w3->settlements[size_t(b)].cell)] = 1.0f;
	year(*w3);
	ok(w3->settlements[size_t(a)].dead && w3->settlements[size_t(a)].cause == Cause::Drowned, "water > 0.3: drowned");
	ok(w3->settlements[size_t(b)].dead && w3->settlements[size_t(b)].cause == Cause::Lava, "lava: lava");
	noNaN(*w); noNaN(*w2); noNaN(*w3);
}

// ================================================================== research

namespace {
// 12 settlements far apart on a uniform world; no tiers and no expansion so nothing but research moves.
std::unique_ptr<World> researchWorld(int nSettle, double curiosity, int& cu) {
	auto w = uniform(5);
	EconomyTune& T = w->tune<EconomyTune>();
	T.gate = { 0, 1e12, 1e12, 1e12, 1e12 };
	T.searchesPerYear = 0;
	cu = newCultureAt(*w, Age::STONE, curiosity);
	for (int k = 0; k < nSettle; k++) {
		const double lat = -50 + 100.0 * ((k * 7) % 12) / 11.0, lon = 30.0 * k;
		const int c = cellLatLon(*w, lat, lon);
		if (w->owner[size_t(c)] >= 0) continue;
		settle(*w, c, cu, 150, 0);
	}
	return w;
}
} // namespace

DM_TEST("economy: research: a nominal culture (12 settlements, curiosity 0.5) crosses the ages") {
	stub::Guard guard;
	installRefs(false, false);
	int cu = -1;
	auto w = researchWorld(12, 0.5, cu);
	eq(liveCount(*w), 12, "12 settlements");
	std::vector<int> cross;
	int worldAgeEvents = 0, cultureAgeEvents = 0;
	auto t1 = w->bus.on<EvCultureAge>([&](const EvCultureAge& e) { cultureAgeEvents++; cross.push_back(int(w->year)); eq(e.prev + 1, e.age, "one age at a time"); });
	auto t2 = w->bus.on<EvWorldAge>([&](const EvWorldAge&) { worldAgeEvents++; });
	const double t0 = nowMs();
	for (int y = 0; y < 3500; y++) year(*w);
	const double ms = nowMs() - t0;
	w->bus.off(t1); w->bus.off(t2);
	const int want[6] = { 420, 920, 1520, 2270, 2890, 3410 };
	const int nominal[6] = { 520, 1000, 1550, 2200, 2750, 3200 };
	std::printf("    no buildings, rate 1.0:");
	for (size_t k = 0; k < cross.size(); k++) std::printf(" %s %d (nominal %d)", kAgeNames[k + 1], cross[k], nominal[k]);
	std::printf("  [%.0f ms for 3500 years]\n", ms);
	if (eq(int(cross.size()), 6, "six ages")) for (int k = 0; k < 6; k++) ok(std::abs(cross[size_t(k)] - want[k]) <= 1, std::string("crossing ") + kAgeNames[k + 1]);
	eq(cultureAgeEvents, 6, "culture:age x 6");
	eq(worldAgeEvents, 6, "world:age x 6");
	eq(w->age, int(Age::SPACE), "world age Space");
	noNaN(*w);
}

DM_TEST("economy: research along the reference curve (settlements 1 -> 90, buildings by age) near the nominal years") {
	stub::Guard guard;
	installRefs(false, false);
	auto w = uniform(5);
	EconomyTune& T = w->tune<EconomyTune>();
	T.gate = { 0, 1e12, 1e12, 1e12, 1e12 };
	T.searchesPerYear = 0;
	const int cu = newCultureAt(*w, Age::STONE, 0.5);
	// SPEC-SIM 4 reference: live settlements ~1, 6, 20, 45, 70, 90 at years 0, 500, 1000, 2000, 3000, 3500
	const double ky[6] = { 0, 500, 1000, 2000, 3000, 3500 }, kn[6] = { 1, 6, 20, 45, 70, 90 };
	auto target = [&](double y) { for (int k = 0; k < 5; k++) if (y <= ky[k + 1]) return kn[k] + (kn[k + 1] - kn[k]) * (y - ky[k]) / (ky[k + 1] - ky[k]); return 90.0; };
	Rng place(99);
	std::vector<int> cross;
	auto tok = w->bus.on<EvCultureAge>([&](const EvCultureAge&) { cross.push_back(int(w->year)); });
	for (int y = 1; y <= 3600 && cross.size() < 6; y++) {
		while (liveCount(*w) < int(std::lround(target(y)))) {
			const int c = int(place.next() % uint32_t(w->n));
			if (w->owner[size_t(c)] >= 0) continue;
			const int sid = settle(*w, c, cu, 150, 0);
			w->settlements[size_t(sid)].devotion = 0.2;
		}
		// buildings arrive with the ages (illustrative shares): markets in half the settlements from Iron,
		// academies in a quarter from Sail, factories in a quarter from Industry
		const int age = w->cultures[size_t(cu)].age;
		for (Settlement& s : w->settlements) {
			if (s.dead) continue;
			if (age >= Age::IRON && s.id % 2 == 0) s.b[Building::MARKET] = 1;
			if (age >= Age::SAIL && s.id % 4 == 1) s.b[Building::ACADEMY] = 1;
			if (age >= Age::INDUSTRY && s.id % 4 == 3) s.b[Building::FACTORY] = 1;
		}
		year(*w);
	}
	w->bus.off(tok);
	const int nominal[6] = { 520, 1000, 1550, 2200, 2750, 3200 };
	std::printf("    reference curve:");
	for (size_t k = 0; k < cross.size(); k++) std::printf(" %s %d (nominal %d, %+.0f %%)", kAgeNames[k + 1], cross[k], nominal[k], 100.0 * (cross[k] - nominal[k]) / nominal[k]);
	std::printf("\n");
	if (eq(int(cross.size()), 6, "six ages"))
		for (int k = 0; k < 6; k++) ok(std::fabs(double(cross[size_t(k)]) / nominal[k] - 1) <= 0.15, std::string("near nominal: ") + kAgeNames[k + 1]);
}

DM_TEST("economy: research factors: size, buildings, curiosity, inspiration, catch-up, clamp") {
	stub::Guard guard;
	installRefs(false, false);
	auto rateOf = [&](int nSettle, double curiosity, const std::function<void(World&, int)>& setup) {
		int cu = -1;
		auto w = researchWorld(nSettle, curiosity, cu);
		setup(*w, cu);
		const double s0 = w->cultures[size_t(cu)].science;
		year(*w);
		return w->cultures[size_t(cu)].science - s0;
	};
	auto none = [](World&, int) {};
	near(rateOf(12, 0.5, none), 1.0, 1e-9, "nominal 1.0");
	near(rateOf(3, 0.5, none), 0.5 + 0.5 * std::sqrt(3.0 / 12), 1e-9, "sizeF 3 settlements");
	near(rateOf(1, 0.0, none), (0.5 + 0.5 * std::sqrt(1.0 / 12)) * 0.85, 1e-9, "1 settlement, curiosity 0");
	near(rateOf(12, 1.0, none), 1.15, 1e-9, "curiosity 1");
	near(rateOf(12, 0.5, [](World& w, int) { for (Settlement& s : w.settlements) s.b[Building::ACADEMY] = 1; }), 1.15, 1e-9, "academies everywhere");
	near(rateOf(12, 0.5, [](World& w, int) { for (Settlement& s : w.settlements) if (s.id % 2 == 0) { s.b[Building::MARKET] = 1; s.b[Building::FACTORY] = 1; } }), 1.10, 1e-9, "markets + factories in half");
	near(rateOf(12, 0.5, [](World& w, int) { for (Settlement& s : w.settlements) s.inspire = 10; }), 1.25, 1e-9, "everyone inspired");
	near(rateOf(12, 0.5, [](World& w, int cu) {
		Culture& o = state::newCulture(w); o.age = Age::BRONZE;
		w.vars.rel[state::relKey(cu, o.id)].contact = true;
	}), 1.5, 1e-9, "catch-up: contact with a culture an age ahead");
	near(rateOf(12, 0.5, [](World& w, int) { Culture& o = state::newCulture(w); o.age = Age::BRONZE; }), 1.0, 1e-9, "no contact: no catch-up");
	near(rateOf(12, 1.0, [](World& w, int cu) {
		for (Settlement& s : w.settlements) { s.b[Building::ACADEMY] = 1; s.b[Building::MARKET] = 1; s.b[Building::FACTORY] = 1; s.inspire = 5; }
		Culture& o = state::newCulture(w); o.age = Age::SAIL;
		w.vars.rel[state::relKey(cu, o.id)].contact = true;
	}), 2.0, 1e-9, "clamped to 2.0");
	near(rateOf(0, 0.5, none), 0.0, 0, "no settlements: no research");
}

// ================================================================== whole-economy runs

namespace {
struct RunResult { int year60 = -1; int settlementsAt300 = 0; double popAt300 = 0; uint32_t hash = 0; std::string nan; };
RunResult campRun(int years, bool maps) {
	auto w = makeWorld({ .L = 6, .seed = 1 });
	// the best site on the largest landmass (the rule of Civ.bestSite, written out with the test score)
	int best = -1;
	double bs = -1;
	for (int c = 0; c < w->n; c++) {
		if (w->cache.landId.empty() || w->cache.landId[size_t(c)] != 0 || !refCanSettle(*w, c).ok) continue;
		const double sc = refSiteScore(*w, c);
		if (sc > bs) { bs = sc; best = c; }
	}
	Culture& cuRef = state::newCulture(*w);
	cuRef.name = "Folk";
	const int cu = cuRef.id;
	RunResult r;
	if (!civ::found(*w, best, cu, 30, FoundedBy::Awaken)) { r.nan = "found failed"; return r; }
	if (maps) {
		const Settlement& s0 = w->settlements[0];
		int arable = 0;
		for (const int32_t c : s0.cells) arable += (w->hab[size_t(c)] > 0.15f && !hydro::isLake(*w, c)) ? 1 : 0;
		std::printf("    first camp: %zu cells, %d arable, K %.1f\n", s0.cells.size(), arable, s0.K);
	}
	std::vector<double> popCurve;
	for (int y = 1; y <= years; y++) {
		year(*w);
		const double p = worldPop(*w);
		popCurve.push_back(p);
		if (r.year60 < 0 && p >= 60) r.year60 = y;
		if (y % 50 == 0 && r.nan.empty()) r.nan = checkFinite(*w);
		if (maps && (y == 150 || y == 300)) {
			namespace mi = dm::mapimg;
			const std::string tag = std::to_string(y);
			mi::owner(*w, shot("ue-economy-owner-" + tag + ".png"));
			mi::field(*w, "farm", shot("ue-economy-farm-" + tag + ".png"), 0, 1, { .dimSea = true });
			mi::field(*w, "urban", shot("ue-economy-urban-" + tag + ".png"), 0, 1, { .dimSea = true });
		}
		if (maps && y % 25 == 0) {
			int vil = 0;
			for (const Settlement& s : w->settlements) vil += (!s.dead && s.tier >= 1) ? 1 : 0;
			std::printf("    year %3d: pop %7.1f  settlements %2d (villages %d)\n", y, p, liveCount(*w), vil);
		}
	}
	r.settlementsAt300 = liveCount(*w);
	r.popAt300 = worldPop(*w);
	r.hash = hash(*w);
	if (maps) {
		namespace mi = dm::mapimg;
		// zoomed-in view: territory colours, farms darker, town centres white
		mi::custom(*w, shot("ue-economy-land.png"), [&](int c) {
			if (state::isSea(*w, c)) return mi::Rgb{ 0.05, 0.1, 0.25 };
			const int o = w->owner[size_t(c)];
			const double f = w->farm[size_t(c)], u = w->urban[size_t(c)];
			mi::Rgb base{ 0.35 * w->hab[size_t(c)] + 0.1, 0.45 * w->hab[size_t(c)] + 0.15, 0.1 };
			if (o >= 0) { const auto& col = kCultureColors[size_t(w->settlements[size_t(o)].culture) % 6]; base = { col[0] * 0.7, col[1] * 0.7, col[2] * 0.7 }; }
			base = { base.r * (1 - 0.5 * f) + 0.5 * f * 0.9, base.g * (1 - 0.5 * f) + 0.5 * f * 0.8, base.b * (1 - 0.5 * f) + 0.5 * f * 0.2 };
			return mi::Rgb{ 255 * (base.r + (1 - base.r) * u), 255 * (base.g + (1 - base.g) * u), 255 * (base.b + (1 - base.b) * u) };
		});
		// zooms: 50 x 25 degrees around the first camp
		const int home = w->settlements[0].cell;
		const World& wr = *w;
		zoomPng(wr, shot("ue-economy-zoom-owner.png"), home, 50, [&](int c) {
			if (state::isSea(wr, c)) return mi::Rgb{ 15, 30, 75 };
			const int o = wr.owner[size_t(c)];
			const double h = wr.hab[size_t(c)];
			if (o < 0) return mi::Rgb{ 60 + 60 * h, 70 + 80 * h, 40 };
			if (wr.settlements[size_t(o)].cell == c) return mi::Rgb{ 255, 255, 255 };
			const uint32_t x = hash32(uint32_t(o) * 2654435761u + 7u);
			return mi::Rgb{ 80.0 + double(x & 127), 80.0 + double((x >> 8) & 127), 80.0 + double((x >> 16) & 127) };
		});
		zoomPng(wr, shot("ue-economy-zoom-farm.png"), home, 50, [&](int c) {
			if (state::isSea(wr, c)) return mi::Rgb{ 15, 30, 75 };
			if (hydro::isLake(wr, c)) return mi::Rgb{ 60, 110, 170 };
			const double f = wr.farm[size_t(c)], u = wr.urban[size_t(c)], v = wr.veg[size_t(c)];
			// vegetation green, farmland gold, built-up white; territory edges darker
			double cr = 40 + 30 * (1 - v), cg = 60 + 110 * v, cb = 30;
			cr = cr * (1 - f) + 235 * f; cg = cg * (1 - f) + 195 * f; cb = cb * (1 - f) + 60 * f;
			cr += (255 - cr) * u; cg += (255 - cg) * u; cb += (255 - cb) * u;
			if (wr.owner[size_t(c)] < 0) { cr *= 0.6; cg *= 0.6; cb *= 0.6; }
			return mi::Rgb{ cr, cg, cb };
		});
	}
	return r;
}
} // namespace

DM_TEST("economy: 300 years from one camp on the helper world (teleporting settlers)") {
	if (!canTeleport()) return;
	stub::Guard guard;
	installRefs();
	const double t0 = nowMs();
	const RunResult r = campRun(300, true);
	std::printf("    world pop reached 60 in year %d; at 300: %d settlements, %.0f people [%.0f ms]\n", r.year60, r.settlementsAt300, r.popAt300, nowMs() - t0);
	ok(r.nan.empty(), "no NaN: " + r.nan);
	ok(r.year60 >= 1 && r.year60 <= 200, "population 60 within 200 years");
	ok(r.settlementsAt300 >= 3, "several settlements by year 300");
}

DM_TEST("economy: with the real Civ, Path and Units: 300 years, settlers walk to their sites") {
	if (stub::stubbed("civ") || stub::stubbed("path") || stub::stubbed("units")) return;   // `run.ps1 all` only
	auto w = makeWorld({ .L = 6, .seed = 1 });
	Culture& cuRef = state::newCulture(*w);
	cuRef.name = "Folk";
	const int cu = cuRef.id;
	const int site = civ::bestSite(*w);
	if (!ok(site >= 0 && civ::found(*w, site, cu, 30, FoundedBy::Awaken) != nullptr, "first camp")) return;
	int spawned = 0, arrived = 0, year60 = -1;
	auto t1 = w->bus.on<EvUnitSpawned>([&](const EvUnitSpawned&) { spawned++; });
	auto t2 = w->bus.on<EvUnitArrived>([&](const EvUnitArrived&) { arrived++; });
	for (int y = 1; y <= 300; y++) {
		for (int q = 0; q < 4; q++) { w->year += 0.25; units::step(*w, 0.25); }
		civ::yearly(*w);
		if (year60 < 0 && worldPop(*w) >= 60) year60 = y;
	}
	w->bus.off(t1); w->bus.off(t2);
	std::printf("    real modules: pop 60 in year %d; at 300: %d settlements, %.0f people; %d settler parties, %d arrived\n",
		year60, liveCount(*w), worldPop(*w), spawned, arrived);
	ok(liveCount(*w) >= 3, "several settlements");
	ok(spawned >= 2 && arrived >= 2, "settlers spawned and arrived");
	ok(checkFinite(*w).empty(), "no NaN: " + checkFinite(*w));
	mapimg::owner(*w, shot("ue-economy-owner-real-300.png"));
}

DM_TEST("economy: deterministic (two identical runs, same hash)") {
	if (!canTeleport()) return;
	stub::Guard guard;
	installRefs();
	const RunResult a = campRun(150, false), b = campRun(150, false);
	eq(a.hash, b.hash, "hash");
	eq(a.settlementsAt300, b.settlementsAt300, "settlements");
}

DM_TEST("economy: yearly <= 3 ms with 100 settlements at L = 6") {
	stub::Guard guard;
	installRefs();
	auto w = makeWorld({ .L = 6, .seed = 2 });
	const int cu1 = newCultureAt(*w, Age::IRON), cu2 = newCultureAt(*w, Age::SAIL);
	std::vector<int32_t> ring;
	int placed = 0;
	for (int c = 0; c < w->n && placed < 100; c += 7) {
		if (state::isSea(*w, c) || w->hab[size_t(c)] < 0.2f || w->owner[size_t(c)] >= 0) continue;
		w->geo->ring(c, 4, ring);
		bool free = true;
		for (const int32_t m : ring) if (w->owner[size_t(m)] >= 0) { free = false; break; }
		if (!free) continue;
		const int tier = placed % 10 == 0 ? 3 : placed % 3 == 0 ? 2 : 1;
		const int sid = settle(*w, c, placed % 2 ? cu1 : cu2, 100, tier);
		w->settlements[size_t(sid)].pop = std::max(80.0, 0.95 * civ::capacity(*w, w->settlements[size_t(sid)]));
		w->settlements[size_t(sid)].lastExpand = -100;
		placed++;
	}
	eq(placed, 100, "100 settlements placed");
	// time spent inside the test's stand-ins (claim / reach / canSettle / found), to report the economy alone
	double hookMs = 0;
	auto& h = stub::hooks();
	if (h.civ_claim) h.civ_claim = [&](World& ww, int sidx) { const double t = nowMs(); refClaim(ww, sidx); hookMs += nowMs() - t; };
	if (h.path_reach) h.path_reach = [&](const World& ww, int from, double mc, NavalMode nv, const path::Visit& v) {
		const double t = nowMs();
		double inside = 0;
		const int rv = refReach(ww, from, mc, nv, [&](int c, double cost) { const double t2 = nowMs(); const bool go = v(c, cost); inside += nowMs() - t2; return go; });
		hookMs += nowMs() - t - inside;
		return rv;
	};
	if (h.civ_canSettle) h.civ_canSettle = [&](const World& ww, int c) { const double t = nowMs(); const civ::SettleCheck r = refCanSettle(ww, c); hookMs += nowMs() - t; return r; };
	if (h.civ_found) h.civ_found = [&](World& ww, int cell, int cuX, double pop, FoundedBy by, const civ::FoundOpts& o) {
		const double t = nowMs(); Settlement* r = refFound(ww, cell, cuX, pop, by, o); hookMs += nowMs() - t; return r; };
	w->year = 500;
	double total = 0, worst = 0, worstOwn = 0;
	const int years = 40;
	for (int y = 0; y < years; y++) {
		const double t0 = nowMs(), h0 = hookMs;
		year(*w);
		const double dt = nowMs() - t0;
		total += dt;
		worst = std::max(worst, dt);
		worstOwn = std::max(worstOwn, dt - (hookMs - h0));
	}
	std::printf("    yearly with %d live settlements: mean %.3f ms, worst %.3f ms; test stand-ins %.3f ms/yr -> economy alone %.3f ms/yr, worst %.3f ms\n",
		liveCount(*w), total / years, worst, hookMs / years, (total - hookMs) / years, worstOwn);
	ok((total - hookMs) / years <= 3, "economy alone within the 3 ms budget");
	ok(total / years <= 3 * 5, "with the stand-ins within 5x the budget");
	noNaN(*w);
	mapimg::owner(*w, shot("ue-economy-owner-100.png"));
}

DM_TEST("economy: robustness: empty worlds, bad ids, dead cultures, NaN people") {
	stub::Guard guard;
	installRefs();
	auto w = makeWorld({ .L = 4 });
	civ::yearly(*w);                                   // nobody at all
	Settlement& bad = state::newSettlement(*w, -1, -1);
	bad.pop = 100;
	Settlement& odd = state::newSettlement(*w, findLand(*w), 77);
	odd.pop = std::nan("");
	odd.tier = 9;
	const int cu = newCultureAt(*w, Age::STONE);
	w->cultures[size_t(cu)].dead = true;
	Settlement& orphan = state::newSettlement(*w, findLand(*w, [&](int c) { return w->owner[size_t(c)] < 0 && w->hab[size_t(c)] > 0.2f; }), cu);
	orphan.pop = 50;
	for (int y = 0; y < 30; y++) { year(*w); }
	ok(checkFinite(*w).empty(), "no NaN: " + checkFinite(*w));
	auto empty = state::create({ .seed = 1, .L = 3 });
	watch(*empty);
	civ::yearly(*empty);
	Settlement probe;
	probe.cell = 5;
	ok(civ::capacity(*empty, probe) >= 0, "capacity on a blank world");
	noNaN(*empty);
}
