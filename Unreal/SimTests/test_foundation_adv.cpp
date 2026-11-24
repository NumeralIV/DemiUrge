// DEMIURGE — SimTests/test_foundation_adv.cpp : the foundation verifier's adversarial tests
// (key "foundation": `run.ps1 foundation`, every module a stub; they also run in `all`).
//
// They try to break what fifteen module engineers build on: geometry, determinism (no hidden
// statics, forks, worlds of different sizes), robustness (NaN, no sea, no land, empty lists), the
// event bus and the event contract of SPEC-SIM 2, the save format (corrupt input, cross-references),
// per-world tuning / scratch, the honest stub reads, and the power table of SPEC-SIM 3.10.
#include "TestFramework.h"
#include "TestWorld.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "MapImg.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

// brute-force nearest cell: the largest dot product (ties: any cell whose dot is within 1e-12 of the best)
double bestDot(const Geo& g, double x, double y, double z) {
	double best = -2;
	for (int c = 0; c < g.n; c++) {
		const double d = g.pos[size_t(3 * c)] * x + g.pos[size_t(3 * c + 1)] * y + g.pos[size_t(3 * c + 2)] * z;
		if (d > best) best = d;
	}
	return best;
}
double dotOf(const Geo& g, int c, double x, double y, double z) {
	return g.pos[size_t(3 * c)] * x + g.pos[size_t(3 * c + 1)] * y + g.pos[size_t(3 * c + 2)] * z;
}

struct AdvTuneA { int a = 1; double b = 2; };
struct AdvTuneB { int a = 10; };
struct AdvScratch { std::vector<int> buf; int calls = 0; };

} // namespace

// ------------------------------------------------------------------ geometry

DM_TEST("foundation-adv: geo L=4 and L=6 topology (cell count, 12 pentagons, symmetric neighbours, Euler)") {
	for (int L : { 4, 6 }) {
		const auto gp = Geo::build(L);
		const Geo& g = *gp;
		const std::string tag = "L=" + std::to_string(L);
		const int expect = 10 * (1 << (2 * L)) + 2;
		eq(g.n, expect, tag + " n");
		eq(Geo::prefix(L), expect, tag + " prefix");
		eq(g.nTri, 20 * (1 << (2 * L)), tag + " nTri");
		eq(g.pos.size(), size_t(3 * g.n), tag + " pos size");
		int pent = 0, edges = 0, bad = 0, asym = 0, dup = 0, notUnit = 0, latLon = 0;
		for (int c = 0; c < g.n; c++) {
			const int k = g.nbrN[size_t(c)];
			if (k == 5) { pent++; if (c > 11) bad++; if (g.nbr[size_t(6 * c + 5)] != -1) bad++; }
			else if (k != 6) bad++;
			edges += k;
			std::set<int> seen;
			for (int j = 0; j < k; j++) {
				const int m = g.nbr[size_t(6 * c + j)];
				if (!g.valid(m) || m == c) { bad++; continue; }
				if (!seen.insert(m).second) dup++;
				bool back = false;
				for (int q = 0; q < g.nbrN[size_t(m)]; q++) if (g.nbr[size_t(6 * m + q)] == c) back = true;
				if (!back) asym++;
			}
			const Vec3 d = g.dir(c);
			if (std::fabs(v3::len(d) - 1) > 1e-6) notUnit++;
			if (std::fabs(std::asin(std::clamp(d.y, -1.0, 1.0)) - g.lat[size_t(c)]) > 1e-5) latLon++;
			if (std::fabs(d.y) < 0.9999 && std::fabs(std::remainder(std::atan2(d.z, d.x) - g.lon[size_t(c)], kTau)) > 1e-5) latLon++;
		}
		eq(pent, 12, tag + " pentagons");
		eq(bad, 0, tag + " bad degree / invalid neighbour / pentagon outside 0..11");
		eq(asym, 0, tag + " asymmetric neighbour pairs");
		eq(dup, 0, tag + " duplicate neighbours");
		eq(notUnit, 0, tag + " non-unit directions");
		eq(latLon, 0, tag + " lat / lon disagree with pos");
		eq(edges % 2, 0, tag + " odd neighbour sum");
		eq(g.n - edges / 2 + g.nTri, 2, tag + " Euler characteristic");
		ok(g.pos[1] > 0.999999 && g.pos[size_t(3 * 11 + 1)] < -0.999999, tag + " cell 0 = north pole, 11 = south pole");
		// step = mean angle between neighbours
		double sum = 0;
		int cnt = 0;
		for (int c = 0; c < g.n; c++) for (int j = 0; j < g.nbrN[size_t(c)]; j++) { sum += g.angle(c, g.nbr[size_t(6 * c + j)]); cnt++; }
		near(g.step, sum / cnt, 1e-9, tag + " step = mean neighbour angle");
		if (L == 6) near(g.step, 0.0189, 0.0006, "L=6 step ~ 0.0189 rad (SPEC-SIM 0.4)");
		if (L == 4) near(g.step, 0.0189 * 4, 0.003, "L=4 step ~ 4x the L=6 step");
	}
}

DM_TEST("foundation-adv: geo cellAt is the exact nearest cell near poles, seams, at any scale and with any hint") {
	for (int L : { 4, 6 }) {
		const auto gp = Geo::build(L);
		const Geo& g = *gp;
		Rng r(4242u + uint32_t(L));
		int wrong = 0, invalid = 0, tried = 0;
		auto probe = [&](double x, double y, double z, int hint) {
			tried++;
			const int c = g.cellAt(x, y, z, hint);
			if (!g.valid(c)) { invalid++; return; }
			const double l = std::sqrt(x * x + y * y + z * z);
			if (!(l > 0)) return;
			if (dotOf(g, c, x / l, y / l, z / l) < bestDot(g, x / l, y / l, z / l) - 1e-12) wrong++;
		};
		const int nTry = L == 6 ? 150 : 400;
		for (int i = 0; i < nTry; i++) {
			// near the poles
			const double lat = (r.chance(0.5) ? 1 : -1) * (kPi / 2 - r.f() * 0.05);
			const double lon = r.range(-kPi, kPi);
			const int hint = r.chance(0.3) ? -1 : r.integer(-5, g.n + 5);
			probe(std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon), hint);
			// at the longitude seam (+-180) and at 0
			const double la2 = r.range(-1.5, 1.5);
			const double lo2 = (r.chance(0.5) ? kPi : 0.0) + r.range(-1e-7, 1e-7);
			probe(std::cos(la2) * std::cos(lo2), std::sin(la2), std::cos(la2) * std::sin(lo2), r.integer(0, g.n - 1));
			// random directions at absurd scales, antipodal hint
			const double s = std::pow(10.0, r.range(-150, 150));
			Vec3 d = v3::norm(Vec3{ r.gauss(), r.gauss(), r.gauss() });
			const int anti = g.cellAt(-d.x, -d.y, -d.z);
			probe(d.x * s, d.y * s, d.z * s, anti);
			// exactly on a cell and exactly between two neighbours
			const int c0 = r.integer(0, g.n - 1);
			const Vec3 p0 = g.dir(c0);
			tried++;
			if (g.cellAt(p0.x, p0.y, p0.z, r.integer(0, g.n - 1)) != c0) wrong++;
			const int m0 = g.nbr[size_t(6 * c0)];
			const Vec3 mid = v3::norm(p0 + g.dir(m0));
			probe(mid.x, mid.y, mid.z, -1);
		}
		// degenerate inputs must still give a valid cell, never crash
		for (double v : { 0.0, kNaN, kInf, -kInf }) {
			const int c = g.cellAt(v, v, v);
			if (!g.valid(c)) invalid++;
			const int c2 = g.cellAt(v, 1, 0, 7);
			if (!g.valid(c2)) invalid++;
		}
		eq(wrong, 0, "L=" + std::to_string(L) + " cellAt not nearest (" + std::to_string(tried) + " probes)");
		eq(invalid, 0, "L=" + std::to_string(L) + " cellAt returned an invalid cell");
	}
}

DM_TEST("foundation-adv: geo within / ring agree with brute force; edge arguments") {
	const auto gp = Geo::build(4);
	const Geo& g = *gp;
	Rng r(99);
	std::vector<int32_t> out, out2;
	int bad = 0;
	for (int i = 0; i < 60; i++) {
		const int c = r.integer(0, g.n - 1);
		const double ang = r.range(0, 0.8);
		g.within(c, ang, out);
		std::set<int> got(out.begin(), out.end());
		if (out.empty() || out[0] != c || got.size() != out.size()) bad++;
		for (int m = 0; m < g.n; m++) if ((g.angle(c, m) <= ang) != (got.count(m) == 1)) bad++;
		// nested use: the caller's vectors are independent
		for (size_t k = 0; k < std::min<size_t>(out.size(), 5); k++) { g.within(out[k], 0.1, out2); if (out2.empty()) bad++; }
		const int steps = r.integer(0, 6);
		g.ring(c, steps, out);
		std::vector<int> hop(size_t(g.n), -1);
		std::vector<int> q{ c };
		hop[size_t(c)] = 0;
		for (size_t h = 0; h < q.size(); h++) {
			const int a = q[h];
			if (hop[size_t(a)] == steps) continue;
			for (int k = 0; k < g.nbrN[size_t(a)]; k++) { const int b = g.nbr[size_t(6 * a + k)]; if (hop[size_t(b)] < 0) { hop[size_t(b)] = hop[size_t(a)] + 1; q.push_back(b); } }
		}
		if (out.size() != q.size() || out[0] != c) bad++;
		for (int32_t m : out) if (hop[size_t(m)] < 0) bad++;
	}
	eq(bad, 0, "within / ring mismatches");
	eq(g.within(5, -1, out), 1, "within ang < 0 -> c alone");
	eq(g.within(5, kNaN, out), 1, "within NaN -> c alone");
	eq(g.within(5, kPi, out), g.n, "within pi -> every cell");
	eq(g.within(-1, 0.3, out), 0, "within invalid cell -> 0");
	eq(g.within(g.n, 0.3, out), 0, "within cell n -> 0");
	eq(g.ring(5, 0, out), 1, "ring 0 steps");
	eq(g.ring(5, 1000, out), g.n, "ring many steps");
	eq(g.ring(-3, 2, out), 0, "ring invalid cell");
	eq(g.ring(5, -2, out), 1, "ring negative steps");
}

// ------------------------------------------------------------------ determinism, statics, sizes

DM_TEST("foundation-adv: determinism: same seed same hash; no state leaks between worlds of any size") {
	const uint32_t h1 = hash(*makeWorld({ .L = 4, .seed = 7 }));
	ok(h1 == hash(*makeWorld({ .L = 4, .seed = 7 })), "same seed -> same hash");
	ok(h1 != hash(*makeWorld({ .L = 4, .seed = 8 })), "different seed -> different hash");
	{
		// abuse a world: draws, tuning, scratch, bus traffic, entities
		auto w = makeWorld({ .L = 6, .seed = 7 });
		for (int i = 0; i < 1000; i++) w->rng.next();
		w->tune<AdvTuneA>().a = 77;
		w->scratch<AdvScratch>().buf.assign(size_t(w->n), 3);
		w->bus.on<EvBattle>([](const EvBattle&) {});
		w->bus.emit(EvBattle{ 3, 0, 1, true, 0 });
		state::newCulture(*w);
		state::newSettlement(*w, 10, 0);
		auto w3 = makeWorld({ .L = 3, .seed = 7 });
		w3->scratch<AdvScratch>().buf.assign(size_t(w3->n), 1);
	}
	ok(h1 == hash(*makeWorld({ .L = 4, .seed = 7 })), "a fresh L=4 world after abusing L=6 / L=3 worlds hashes the same");
	// fresh state::create worlds are identical whatever happened before
	auto a = state::create({ .seed = 5, .L = 4 });
	auto b = state::create({ .seed = 5, .L = 4, .geo = sharedGeo(4) });
	ok(hash(*a) == hash(*b), "own geo vs shared geo give the same world");
	eq(AdvTuneA{}.a, a->tune<AdvTuneA>().a, "tuning of a new world is the default");
}

DM_TEST("foundation-adv: rng forks: parent untouched, independent of draws elsewhere, distinct tags") {
	Rng p(123456u);
	const uint32_t s0 = p.s;
	Rng fa = p.fork("chron:1");
	eq(p.s, s0, "fork(string) does not advance the parent");
	Rng fb = p.fork(17);
	eq(p.s, s0, "fork(int) does not advance the parent");
	// draws on one fork never change another fork's stream
	std::vector<uint32_t> ref;
	{ Rng t = p.fork("b"); for (int i = 0; i < 64; i++) ref.push_back(t.next()); }
	for (int i = 0; i < 5000; i++) fa.next();
	{ Rng t = p.fork("b"); bool same = true; for (int i = 0; i < 64; i++) same = same && t.next() == ref[size_t(i)]; ok(same, "fork('b') unaffected by draws on fork('chron:1')"); }
	// distinct tags and int vs string differ
	ok(p.fork("chron:1").next() != p.fork("chron:2").next(), "different string tags differ");
	ok(p.fork(1).next() != p.fork("1").next() || p.fork(1).next() != p.fork(2).next(), "int tags are not trivially equal");
	ok(p.fork(1).next() != p.fork(2).next(), "different int tags differ");
	ok(p.fork(-5).next() == p.fork(int64_t(-5)).next(), "int tag width does not matter");
	// a fork is not the parent stream shifted
	{
		Rng pp(123456u), ff = pp.fork("x");
		std::set<uint32_t> parent;
		for (int i = 0; i < 20000; i++) parent.insert(pp.next());
		int hits = 0;
		for (int i = 0; i < 200; i++) if (parent.count(ff.next())) hits++;
		ok(hits <= 2, "fork stream overlaps the parent stream (" + std::to_string(hits) + " of 200)");
	}
	// weak independence: correlation between two sibling forks
	{
		Rng x = p.fork("x"), y = p.fork("y");
		double sx = 0, sy = 0, sxy = 0, sxx = 0, syy = 0;
		const int N = 20000;
		for (int i = 0; i < N; i++) { const double u = x.f(), v = y.f(); sx += u; sy += v; sxy += u * v; sxx += u * u; syy += v * v; }
		const double cov = sxy / N - (sx / N) * (sy / N);
		const double corr = cov / std::sqrt((sxx / N - sx * sx / N / N) * (syy / N - sy * sy / N / N));
		ok(std::fabs(corr) < 0.03, "sibling forks correlate: " + std::to_string(corr));
	}
	(void)fb;
	// helper statistics: integer ends, pickIndex, weighted with NaN / inf / negative
	Rng r(9);
	int lo = 0, hi = 0, outside = 0;
	for (int i = 0; i < 20000; i++) { const int v = r.integer(3, 7); if (v == 3) lo++; if (v == 7) hi++; if (v < 3 || v > 7) outside++; }
	ok(outside == 0 && lo > 3000 && hi > 3000, "integer(3,7) inclusive and roughly uniform");
	eq(r.pickIndex(0), -1, "pickIndex(0)");
	const uint32_t before = r.s;
	const std::vector<double> none = { 0, -1, kNaN };
	ok(r.weighted(none, [](double v) { return v; }) == nullptr && r.s == before, "nothing positive -> nullptr, no draw");
	const std::vector<double> mixed = { kNaN, -3, 0, 2, kInf, 5 };
	int infWins = 0;
	for (int i = 0; i < 200; i++) { const double* e = r.weighted(mixed, [](double v) { return v; }); if (e && std::isinf(*e)) infWins++; }
	eq(infWins, 200, "an infinite weight always wins");
	bool gaussFinite = true;
	for (int i = 0; i < 100000; i++) if (!std::isfinite(r.gauss())) gaussFinite = false;
	ok(gaussFinite, "gauss always finite");
}

DM_TEST("foundation-adv: tune / scratch are per world and per type; clearScratch keeps tuning") {
	auto w1 = state::create({ .seed = 1, .L = 2 });
	auto w2 = state::create({ .seed = 1, .L = 2 });
	w1->tune<AdvTuneA>().a = 5;
	w1->tune<AdvTuneB>().a = 6;
	eq(w2->tune<AdvTuneA>().a, 1, "other world keeps the default");
	eq(w1->tune<AdvTuneA>().a, 5, "same world sees its change");
	eq(w1->tune<AdvTuneB>().a, 6, "two types do not share a slot");
	const World& cw = *w1;
	cw.scratch<AdvScratch>().calls++;
	eq(w1->scratch<AdvScratch>().calls, 1, "scratch reachable from const World");
	w1->clearScratch();
	eq(w1->scratch<AdvScratch>().calls, 0, "clearScratch drops scratch");
	eq(w1->tune<AdvTuneA>().a, 5, "clearScratch keeps tuning");
	auto w3 = state::deserialize([&] { std::vector<uint8_t> b; state::serialize(*w1, b); return b; }());
	ok(w3 != nullptr, "round trip");
	if (w3) eq(w3->tune<AdvTuneA>().a, 1, "tuning is not saved (Balance re-applies it)");
}

// ------------------------------------------------------------------ robustness

DM_TEST("foundation-adv: worlds with no sea, no land, and no entities survive the foundation and every stub") {
	for (int variant = 0; variant < 3; variant++) {
		auto w = makeWorld({ .L = 3, .seed = 11, .basics = false });
		if (variant == 0) for (float& e : w->elev) e = 2;          // no sea
		if (variant == 1) for (float& e : w->elev) e = -5;         // no land
		fillBasics(*w);
		const std::string tag = variant == 0 ? "no sea" : variant == 1 ? "no land" : "plain";
		int seaCells = 0;
		for (int c = 0; c < w->n; c++) seaCells += state::isSea(*w, c) ? 1 : 0;
		if (variant == 0) eq(seaCells, 0, tag);
		if (variant == 1) { eq(seaCells, w->n, tag); eq(w->cache.landCount, 0, tag + " landCount"); eq(findLand(*w), -1, tag + " findLand"); }
		if (variant == 0) { eq(w->cache.landCount, 1, tag + " one landmass"); }
		noNaN(*w, tag + " after fillBasics");
		for (int c = 0; c < w->n; c++) if (!std::isfinite(state::surfaceR(*w, c)) || !std::isfinite(state::groundR(*w, c))) { fail(tag + " surfaceR"); break; }
		// every stubbed reader with bad, edge and NaN arguments
		dmt::stub::Guard guard;
		for (int c : { -7, -1, 0, w->n - 1, w->n, 1 << 30 }) {
			civ::at(*w, c);
			civ::near(*w, c, kNaN);
			civ::near(*w, c, 10);
			civ::canSettle(*w, c);
			units::near(*w, c, kNaN);
			life::capacity(*w, c);
			hydro::isLake(*w, c);
			hydro::isRiver(*w, c);
			path::find(*w, c, 0);
			society::peek(*w, c, 0);
			society::atWar(*w, c, c);
			ok(society::rel(*w, c, c) == nullptr, "rel(a, a) is null");
		}
		climate::datum(*w);
		climate::anomaly(*w);
		climate::landIce(*w);
		civ::live(*w);
		society::regard(*w, -1);
		society::regard(*w, 99);
		chronicle::yearStr(*w);
		sim::sample(*w);
		noNaN(*w, tag + " after the stubs");
		std::vector<uint8_t> bytes;
		ok(state::serialize(*w, bytes), tag + " serialize");
		auto back = state::deserialize(bytes);
		ok(back != nullptr, tag + " deserialize");
		ok(mapimg::elev(*w, shot("ue-foundation-adv-tmp.png")) && mapimg::biome(*w, shot("ue-foundation-adv-tmp.png")), tag + " maps");
	}
	std::remove(shot("ue-foundation-adv-tmp.png").c_str());
}

DM_TEST("foundation-adv: honest stub reads match the spec (civ live/at/near, rel/peek, datum, anomaly, mood, yearStr)") {
	if (!dmt::stub::stubbed("civ") || !dmt::stub::stubbed("diplomacy") || !dmt::stub::stubbed("climate")) return;
	dmt::stub::Guard guard;
	auto w = makeWorld({ .L = 4, .seed = 2 });
	state::newCulture(*w);
	state::newCulture(*w);
	const int c0 = findLand(*w);
	const int c1 = w->geo->nbr[size_t(6 * c0)];
	Settlement& s0 = state::newSettlement(*w, c0, 0);
	s0.pop = 50;
	state::newSettlement(*w, c1, 1).pop = 20;
	Settlement& s2 = state::newSettlement(*w, c0, 0);
	s2.dead = true;
	w->owner[size_t(c1)] = 1;
	int other = -1;
	for (int k = 0; k < w->geo->nbrN[size_t(c1)]; k++) { const int m = w->geo->nbr[size_t(6 * c1 + k)]; if (m != c0 && other < 0) other = m; }
	w->owner[size_t(other)] = 1;
	eq(civ::live(*w), std::vector<int>{ 0, 1 }, "live: ascending, dead excluded");
	eq(civ::at(*w, c0), 0, "at: centre");
	eq(civ::at(*w, other), 1, "at: owned cell");
	eq(civ::near(*w, c0, 0.5), std::vector<int>{ 0, 1 }, "near: nearest first");
	ok(society::peek(*w, 0, 1) == nullptr, "peek never creates");
	Relation* r = society::rel(*w, 1, 0);
	ok(r != nullptr && society::peek(*w, 0, 1) == r, "rel creates on demand, symmetric key");
	if (r) r->war = true;
	ok(society::atWar(*w, 0, 1) && society::atWar(*w, 1, 0), "atWar both orders");
	ok(society::rel(*w, 0, 5) == nullptr, "rel with an invalid culture is null");
	w->vars.seaBase = kNaN;
	w->seaLevel = 0.3;
	near(climate::datum(*w), 0.3, 1e-12, "datum ignores a NaN seaBase");
	w->vars.seaBase = -1.0;
	near(climate::datum(*w), 0.0, 1e-12, "datum = max(0, seaBase)");
	w->globalTemp = 2;
	w->dust = 0.1;
	near(climate::anomaly(*w), 2 - 0.9, 1e-12, "anomaly = globalTemp - 9 dust");
	eq(int(sim::mood(0.4, 0.3)), int(Mood::Beloved), "mood beloved");
	eq(int(sim::mood(0.9, -0.2)), int(Mood::Feared), "mood feared");
	eq(int(sim::mood(0.1, 0.0)), int(Mood::Forgotten), "mood forgotten");
	eq(int(sim::mood(0.3, 0.0)), int(Mood::Parting), "mood parting");
	w->year = 312.7;
	eq(chronicle::yearStr(*w), std::string("Year 312"), "yearStr");
	eq(names::cap("umar"), std::string("Umar"), "cap");
}

// ------------------------------------------------------------------ events

DM_TEST("foundation-adv: event table = SPEC-SIM 2 (names and payload keys in order)") {
	struct Row { const char* name; std::vector<const char*> keys; };
	const std::vector<Row> spec = {
		{ "phase", { "phase", "prev" } }, { "power:used", { "id", "cell", "ang", "brush" } },
		{ "miracle:witnessed", { "cell", "kind", "good", "ids" } }, { "lightning", { "cell", "natural" } },
		{ "meteor:launched", { "cell", "eta", "kind", "id" } }, { "meteor:impact", { "cell", "ang", "kind", "sea" } },
		{ "tsunami:start", { "cell" } }, { "tsunami:hit", { "cell", "height", "natural" } },
		{ "volcano:start", { "cell", "id", "natural" } }, { "volcano:end", { "cell", "id" } },
		{ "quake", { "cell", "ang", "power", "natural" } }, { "fire:start", { "cell" } },
		{ "storm", { "cell", "ang", "start" } }, { "drought", { "cell", "ang", "start" } },
		{ "settlement:founded", { "id", "cell", "culture", "by" } }, { "settlement:tier", { "id", "tier", "prev" } },
		{ "settlement:building", { "id", "building" } }, { "settlement:famine", { "id" } },
		{ "settlement:plague", { "id", "start" } }, { "settlement:destroyed", { "id", "cell", "culture", "cause" } },
		{ "settlement:captured", { "id", "from", "to", "how" } }, { "prayer", { "id", "kind", "start" } },
		{ "prayer:answered", { "id", "kind" } }, { "culture:born", { "id", "parent", "cell", "reason" } },
		{ "culture:died", { "id" } }, { "culture:age", { "id", "age", "prev" } }, { "world:age", { "age" } },
		{ "war:declared", { "a", "b", "ally", "blamed", "envied" } }, { "war:ended", { "a", "b", "victor", "thanked" } },   // (v3.1: + ally, blamed, thanked; chronicle-diplomacy: + envied)
		{ "battle", { "cell", "attacker", "defender", "won", "settlement" } }, { "trade:route", { "a", "b" } },
		{ "prophet:risen", { "culture", "settlement", "name" } }, { "prophet:died", { "culture", "name" } },
		{ "edict", { "culture", "edict" } }, { "wonder:built", { "settlement", "name" } },
		{ "nuke", { "cell", "from", "to" } }, { "ark:started", { "culture", "settlement" } },
		{ "ark:launched", { "culture", "settlement" } }, { "climate:milestone", { "kind", "value", "culture" } },   // (v3: + culture)
		{ "unit:spawned", { "id", "type", "culture", "cell" } }, { "unit:arrived", { "id", "type", "culture", "cell" } },
		{ "unit:died", { "id", "type", "culture", "cell", "cause" } }, { "chronicle:entry", { "entry" } },
		{ "awe:short", { "id" } }, { "people:lost", {} }, { "ending", { "type", "mood", "culture", "settlement", "year", "legacy" } },
		{ "drown:warning", { "id", "cell" } },   // (v3, design review)
		{ "alliance", { "a", "b", "formed" } },   // (v3.1, design review 11/13)
	};
	eq(spec.size(), size_t(48), "spec rows (v3: + drown:warning; v3.1: + alliance)");
	// (v4 mythos, M0) the ending payload gains legacy (SPEC-MYTHOS 1.8): its spec row below lists 6 keys
	for (const Row& row : spec) {
		const Ev t = eventFromName(row.name);
		if (!ok(t != Ev::Count, std::string("missing event ") + row.name)) continue;
		const EventInfo& info = kEventTable[int(t)];
		eq(info.nKeys, int(row.keys.size()), std::string(row.name) + " key count");
		for (size_t k = 0; k < row.keys.size() && k < size_t(info.nKeys); k++) eq(std::string(info.keys[k]), std::string(row.keys[k]), std::string(row.name) + " key");
	}
	// every payload struct: variant index = Ev, fields() = table keys, default payload passes checkEvent
	int checked = 0;
	auto each = [&]<size_t... I>(std::index_sequence<I...>) {
		([&] {
			using P = std::variant_alternative_t<I, EventPayload>;
			if constexpr (!std::is_same_v<P, std::monostate>) {
				eq(int(P::kType), int(I), "variant order");
				Event e;
				e.type = P::kType;
				e.data = P{};
				const std::string err = checkEvent(e, 10);
				ok(err.empty(), std::string(e.name()) + ": " + err);
				checked++;
			}
		}(), ...);
	};
	each(std::make_index_sequence<std::variant_size_v<EventPayload>>{});
	eq(checked, kEventCount, "payload structs");
	eq(eventFromName("no:such"), Ev::Count, "unknown name");
}

DM_TEST("foundation-adv: bus: removal / addition / clear during emit, nested emits, interleaved order") {
	EventBus bus;
	std::vector<std::string> log;
	EventBus::Token t2 = 0;
	const EventBus::Token t1 = bus.on<EvFireStart>([&](const EvFireStart&) { log.push_back("a"); bus.off(t2); bus.on<EvFireStart>([&](const EvFireStart&) { log.push_back("late"); }); });
	bus.onAll([&](const Event&) { log.push_back("all"); });
	t2 = bus.on(Ev::FireStart, [&](const Event&) { log.push_back("b"); });
	bus.emit(EvFireStart{ 1 });
	eq(log, std::vector<std::string>{ "a", "all" }, "b removed mid-emit is skipped, late not called yet");
	log.clear();
	bus.off(t1);
	bus.off(t1);
	bus.off(0);
	bus.emit(EvFireStart{ 1 });
	eq(log, std::vector<std::string>{ "all", "late" }, "subscription order after changes");
	// nested emit runs to completion first
	EventBus nb;
	std::vector<int> order;
	nb.on<EvTsunamiStart>([&](const EvTsunamiStart& e) { order.push_back(e.cell); if (e.cell == 1) nb.emit(EvTsunamiStart{ 2 }); order.push_back(-e.cell); });
	nb.emit(EvTsunamiStart{ 1 });
	eq(order, std::vector<int>{ 1, 2, -2, -1 }, "nested emit");
	// clear during emit stops the rest
	EventBus cb;
	int after = 0;
	cb.onAll([&](const Event&) { cb.clear(); });
	cb.onAll([&](const Event&) { after++; });
	cb.emit(EvFireStart{ 0 });
	eq(after, 0, "clear during emit");
	eq(cb.count(), 0, "nothing left after clear");
	// churn
	EventBus ch;
	std::vector<EventBus::Token> toks;
	for (int i = 0; i < 5000; i++) toks.push_back(ch.on<EvFireStart>([](const EvFireStart&) {}));
	for (size_t i = 0; i < toks.size(); i += 2) ch.off(toks[i]);
	eq(ch.count(), 2500, "churn count");
	ch.clear();
	eq(ch.count(), 0, "cleared");
	// a payload with the wrong type for its Ev is reported by checkEvent
	Event wrong;
	wrong.type = Ev::Battle;
	wrong.data = EvFireStart{ 3 };
	ok(!checkEvent(wrong, 10).empty(), "mismatched payload detected");
	Event outOfRange;
	outOfRange.type = Ev::FireStart;
	outOfRange.data = EvFireStart{ 10 };
	ok(!checkEvent(outOfRange, 10).empty(), "cell out of range detected");
	Event nanEv;
	nanEv.type = Ev::TsunamiHit;
	nanEv.data = EvTsunamiHit{ 1, kNaN };
	ok(!checkEvent(nanEv, 10).empty(), "NaN payload detected");
}

// ------------------------------------------------------------------ saves

DM_TEST("foundation-adv: save: NaN cells and entity numbers, extreme values, refusal of non-finite scalars") {
	auto w = makeWorld({ .L = 3, .seed = 4 });
	w->veg[3] = float(kNaN);
	w->elev[4] = 1e9f;
	w->elev[5] = -1e9f;
	w->fert[6] = float(kInf);
	w->owner[7] = -1;
	state::newCulture(*w).name = "Ümar \"quoted\"\n";
	Settlement& s = state::newSettlement(*w, 8, 0);
	s.pop = kNaN;
	s.name = std::string(5000, 'x');
	w->owner[9] = 0;
	w->vars.seaBase = kInf;
	w->vars.num["civ_x"] = kNaN;
	std::vector<uint8_t> bytes;
	std::string why;
	if (!ok(state::serialize(*w, bytes, &why), "serialize with NaN cells / entities: " + why)) return;
	auto b = state::deserialize(bytes, &why);
	if (!ok(b != nullptr, "deserialize: " + why)) return;
	eq(b->veg[3], 0.0f, "NaN veg -> default");
	near(b->elev[4], 20, 1e-3, "elev clamped high");
	near(b->elev[5], -12, 1e-3, "elev clamped low");
	near(b->fert[6], 1.5, 1e-3, "inf fert -> top of range");
	eq(b->settlements[0].pop, 0.0, "NaN pop -> 0");
	eq(b->settlements[0].name.size(), size_t(5000), "long name");
	eq(b->cultures[0].name, std::string("Ümar \"quoted\"\n"), "utf-8 name");
	ok(!b->vars.seaBase.has_value(), "infinite seaBase saved as unset");
	eq(b->vars.num["civ_x"], 0.0, "NaN bag value -> 0");
	eq(int(b->owner[9]), 0, "owner kept");
	noNaN(*b, "loaded world");
	// non-finite world scalars are refused (they could not be loaded back)
	for (int k = 0; k < 4; k++) {
		auto x = makeWorld({ .L = 2, .seed = 4 });
		if (k == 0) x->seaLevel = kNaN;
		if (k == 1) x->year = kInf;
		if (k == 2) x->god.awe = kNaN;
		if (k == 3) x->vars.lifeBoost = -kInf;
		std::vector<uint8_t> o{ 1, 2, 3 };
		ok(!state::serialize(*x, o) && o.empty(), "non-finite scalar " + std::to_string(k) + " refused, output empty");
		eq(state::sizeOf(*x), size_t(0), "sizeOf refuses too");
	}
}

DM_TEST("foundation-adv: save: corrupt cross-references are rejected (owner, tier, culture, unit index)") {
	auto base = makeWorld({ .L = 3, .seed = 5 });
	state::newCulture(*base);
	state::newSettlement(*base, 3, 0);
	Unit& u = state::newUnit(*base, UnitType::Trader);
	u.path = { 3, 4, 5 };
	u.culture = 0;
	u.from = 0;
	std::vector<uint8_t> good;
	if (!ok(state::serialize(*base, good), "serialize")) return;
	ok(state::deserialize(good) != nullptr, "valid save loads");
	auto mutate = [&](const char* what, const std::function<void(World&)>& fn) {
		auto w = state::deserialize(good);
		if (!w) { fail("reload"); return; }
		fn(*w);
		std::vector<uint8_t> bytes;
		if (!state::serialize(*w, bytes)) { fail(std::string(what) + ": serialize refused"); return; }
		std::string why;
		ok(state::deserialize(bytes, &why) == nullptr, std::string(what) + " must be rejected");
		ok(why.rfind("bad save", 0) == 0, std::string(what) + " reason: " + why);
	};
	mutate("owner beyond the settlements", [](World& w) { w.owner[10] = 1; });
	mutate("owner -2", [](World& w) { w.owner[10] = -2; });
	mutate("settlement tier 9", [](World& w) { w.settlements[0].tier = 9; });
	mutate("settlement culture 4", [](World& w) { w.settlements[0].culture = 4; });
	mutate("culture capital 3", [](World& w) { w.cultures[0].capital = 3; });
	mutate("unit index past its path", [](World& w) { w.units[0].i = 3; });
	mutate("unit culture 2", [](World& w) { w.units[0].culture = 2; });
	mutate("world age 7", [](World& w) { w.age = 7; });
	mutate("relation with a missing culture", [](World& w) { w.vars.rel[state::relKey(0, 3)] = Relation{}; });
	mutate("ending settlement 5", [](World& w) { w.ending = Ending{}; w.ending->settlement = 5; });
	// random byte flips never crash and never produce a world with bad references
	Rng r(77);
	int loaded = 0;
	for (int i = 0; i < 3000; i++) {
		std::vector<uint8_t> b = good;
		const int flips = r.integer(1, 4);
		for (int f = 0; f < flips; f++) b[size_t(r.integer(0, int(b.size()) - 1))] ^= uint8_t(1u << r.integer(0, 7));
		auto w = state::deserialize(b);
		if (!w) continue;
		loaded++;
		bool refs = true;
		for (int c = 0; c < w->n; c++) if (w->owner[size_t(c)] < -1 || w->owner[size_t(c)] >= int(w->settlements.size())) refs = false;
		for (const Settlement& s : w->settlements) if (s.tier < 0 || s.tier > 4 || s.cell < -1 || s.cell >= w->n) refs = false;
		if (!refs) { fail("a flipped save loaded with bad references"); break; }
		if (!checkFinite(*w).empty()) { fail("a flipped save loaded with a non-finite value: " + checkFinite(*w)); break; }
	}
	if (verbose()) std::printf("       byte flips: %d of 3000 still loaded (all valid)\n", loaded);
}

// ------------------------------------------------------------------ schema, powers

DM_TEST("foundation-adv: factories: ids, defaults, colour slots, relKey") {
	auto w = state::create({ .seed = 1, .L = 2 });
	w->year = 42;
	for (int i = 0; i < 8; i++) {
		Culture& c = state::newCulture(*w);
		eq(c.id, i, "culture id = index");
		eq(c.born, 42.0, "born = year");
	}
	std::set<std::array<double, 3>> firstSix;
	for (int i = 0; i < 6; i++) firstSix.insert(w->cultures[size_t(i)].color);
	eq(firstSix.size(), size_t(6), "six living cultures get six different colours");
	w->cultures[2].dead = true;
	eq(state::freeColorSlot(*w), 2, "a dead culture frees its colour slot");
	const Settlement& s = state::newSettlement(*w, 5, 1);
	ok(s.id == 0 && s.cell == 5 && s.culture == 1 && !s.dead && !s.deadYear && s.cause == Cause::None && s.founded == 42 && s.lastExpand == 42, "settlement defaults");
	ok(s.radius == 2 && s.tier == 0 && !s.capital && !s.prayer, "settlement defaults 2");
	const int u1 = state::newUnit(*w, UnitType::Army).id;
	const int u2 = state::newUnit(*w, UnitType::Settler).id;
	ok(u2 == u1 + 1 && w->nextId.unit == u2 + 1, "unit ids from nextId");
	ok(state::eraseUnit(*w, u1) && !state::eraseUnit(*w, u1) && state::findUnit(*w, u2) != nullptr, "erase unit");
	const int d1 = state::newDisaster(*w, DisasterType::Storm, 3).id;
	ok(state::findDisaster(*w, d1) && state::eraseDisaster(*w, d1) && !state::findDisaster(*w, d1), "disasters");
	ok(state::relKey(7, 3) == std::pair<int, int>(3, 7) && state::relKey(3, 7) == std::pair<int, int>(3, 7), "relKey");
	ok(state::settlement(*w, -1) == nullptr && state::settlement(*w, 1) == nullptr && state::culture(*w, 99) == nullptr, "lookups of absent ids");
	// enum strings round trip
	for (int i = 0; i < enumCount<MilestoneKind>(); i++) { MilestoneKind k{}; ok(fromStr(toStr(MilestoneKind(i)), k) && int(k) == i, "milestone strings"); }
	for (int i = 0; i < enumCount<Cause>(); i++) { Cause k{}; ok(fromStr(toStr(Cause(i)), k) && int(k) == i, "cause strings"); }
	Phase ph{};
	ok(!fromStr("History", ph) && fromStr("history", ph) && ph == Phase::History, "phase strings are the JS lower-case ones");
}

DM_TEST("foundation-adv: power table = SPEC-SIM 3.10") {
	struct P { const char* id; char key; bool brush; double cost, ang; powers::Target target; double wAng, faith; int age; double awe; };
	using T = powers::Target;
	const P spec[] = {
		{ "comet", 'C', false, 0, 0.03, T::Any, 0, 0, 0, 0 }, { "seed", 'G', false, 0, 0.12, T::Land, 0, 0, 0, 0 },
		{ "awaken", 'K', false, 0, 0, T::Land, 0, 0, Age::SAIL, 450 }, { "raise", 'U', true, 6, 0.06, T::Any, 2, 0.02, 0, 0 },
		{ "lower", 'J', true, 6, 0.06, T::Any, 2, 0.02, 0, 0 }, { "rain", 'N', true, 5, 0.10, T::Any, 2, 0.02, 0, 0 },
		{ "bloom", 'B', true, 6, 0.08, T::Land, 2, 0.02, Age::BRONZE, 150 }, { "lightning", 'L', false, 15, 0, T::Any, 0.10, 0.05, 0, 0 },
		{ "quake", 'Z', false, 60, 0.08, T::Any, 0.20, 0.10, Age::BRONZE, 200 }, { "plague", 'X', false, 80, 0, T::Settlement, 0.12, 0.05, Age::IRON, 0 },
		{ "meteor", 'M', false, 150, 0.05, T::Any, 0.45, 0.25, Age::SAIL, 0 }, { "volcano", 'V', false, 220, 0.04, T::Land, 0.40, 0.25, Age::INDUSTRY, 0 },
		{ "heal", 'T', false, 40, 0, T::Settlement, 0.10, 0.12, 0, 0 }, { "inspire", 'I', false, 60, 0, T::Settlement, 0.10, 0.08, Age::BRONZE, 200 },
		{ "prophet", 'P', false, 120, 0, T::Settlement, 0.15, 0.15, Age::IRON, 0 }, { "peace", 'O', false, 100, 0, T::Settlement, 0, 0.10, Age::IRON, 0 },
		{ "consecrate", 'Y', false, 150, 0, T::Settlement, 0.10, 0.10, Age::BRONZE, 0 },   // (v3, design review)
	};
	eq(powers::kPowerCount, 22, "power count (v3: + consecrate; v4 mythos: + speak, bless, reveal, test, renew)");
	std::set<char> keys;
	for (int i = 0; i < 17 && i < powers::kPowerCount; i++) {
		const powers::PowerDef& d = powers::kPowers[i];
		const P& p = spec[i];
		const std::string tag = p.id;
		eq(std::string(d.id), tag, "order");
		ok(d.key == p.key && d.brush == p.brush && d.cost == p.cost && d.ang == p.ang && d.target == p.target, tag + " key/brush/cost/ang/target");
		ok(d.wAng == p.wAng && d.faith == p.faith, tag + " wAng / faith");
		ok(d.unlock.age == p.age && d.unlock.awe == p.awe, tag + " unlock");
		ok(powers::byId(p.id) == &d, tag + " byId");
		keys.insert(d.key);
	}
	eq(keys.size(), size_t(17), "hotkeys unique");
	ok(powers::byId("nuke") == nullptr && powers::byId("") == nullptr, "byId unknown");
	ok(powers::kPowers[0].unlock.phaseOnly && powers::kPowers[1].unlock.phaseOnly && powers::kPowers[0].unlock.phase == Phase::Water && powers::kPowers[1].unlock.phase == Phase::Life, "genesis powers only in their phase");
	ok(powers::kPowers[15].wScope == powers::WScope::Cultures, "peace witnesses both cultures");
}

// ------------------------------------------------------------------ maps

DM_TEST("foundation-adv: maps of the L=6 helper world (elevation, biome, landmasses)") {
	auto w = makeWorld({ .L = 6, .seed = 3 });
	ok(mapimg::elev(*w, shot("ue-foundation-elev.png"), { .grid = true }), "elev map");
	ok(mapimg::biome(*w, shot("ue-foundation-biome.png")), "biome map");
	// the landmass stand-in: ids by descending size, and the largest coloured
	std::vector<int> sizes(size_t(std::max(1, w->cache.landCount)), 0);
	for (int c = 0; c < w->n; c++) if (w->cache.landId[size_t(c)] >= 0) sizes[size_t(w->cache.landId[size_t(c)])]++;
	bool desc = true;
	for (size_t i = 1; i < sizes.size(); i++) if (sizes[i] > sizes[i - 1]) desc = false;
	ok(desc && w->cache.landCount > 0, "landmass ids by descending size");
	int landCells = 0, labelled = 0;
	for (int c = 0; c < w->n; c++) { landCells += state::isSea(*w, c) ? 0 : 1; labelled += w->cache.landId[size_t(c)] >= 0 ? 1 : 0; }
	eq(labelled, landCells, "every land cell labelled");
	near(double(landCells) / w->n, 0.4, 0.01, "helper land fraction");
	ok(mapimg::custom(*w, shot("ue-foundation-land.png"), [&](int c) {
		const int id = w->cache.landId[size_t(c)];
		if (id < 0) return mapimg::Rgb{ 20, 40, 90 };
		if (id < 6) { const auto& k = kCultureColors[size_t(id)]; return mapimg::Rgb{ k[0] * 255, k[1] * 255, k[2] * 255 }; }
		return mapimg::Rgb{ 140, 140, 140 };
	}), "landmass map");
	if (verbose()) std::printf("       helper L=6 seed 3: %d landmasses, largest %d cells, landHab[0] = %d\n", w->cache.landCount, sizes[0], w->cache.landHab.empty() ? -1 : w->cache.landHab[0]);
}
