// DEMIURGE — SimTests/test_civ.cpp : Sim/Civ.cpp, the lifecycle half of dm::civ (SPEC-SIM 3.7, BUILD.md A7).
// Owner: A7 civ.
//
// Runs with Economy (civ::capacity = 0) and every other module stubbed (`run.ps1 civ`); the tests that need
// civ::capacity to answer install a hook and return early when Economy is real (`run.ps1 all`). Worlds are the
// helper planet (makeWorld) or hand-built ones; nothing here depends on another module's behaviour.
// Maps: shots/ue-civ-*.png (owner maps of hand-founded settlements and of a scripted history).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <array>
#include <set>
#include <string>
#include <vector>

#include "Civ.h"
#include "Hydro.h"
#include "MapImg.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;
namespace mi = dm::mapimg;

namespace {

// ------------------------------------------------------------------ helpers

// All land at 0.5 above a sea level of 0, hab everywhere `hab` (no landmass cache).
std::unique_ptr<World> flat(int L = 4, double hab = 0.6) {
	auto w = state::create({ .seed = 1, .L = L, .geo = sharedGeo(L) });
	watch(*w);
	std::fill(w->elev.begin(), w->elev.end(), 0.5f);
	std::fill(w->hab.begin(), w->hab.end(), float(hab));
	w->seaLevel = 0;
	w->heat = 0;
	w->phase = Phase::History;
	return w;
}

int cellAt(const World& w, double x, double y, double z) { return w.geo->cellAt(x, y, z); }
int cellLL(const World& w, double latDeg, double lonDeg) {
	const double la = latDeg * kPi / 180, lo = lonDeg * kPi / 180;
	// Geo convention: +Y north; longitude measured in the X/Z plane
	return w.geo->cellAt(std::cos(la) * std::cos(lo), std::sin(la), std::cos(la) * std::sin(lo));
}

int newCulture(World& w) { return state::newCulture(w).id; }

int hops(const World& w, int a, int b) {
	if (a == b) return 0;
	std::vector<int> d(size_t(w.n), -1), q;
	d[size_t(a)] = 0;
	q.push_back(a);
	for (size_t h = 0; h < q.size(); h++) {
		const int c = q[h];
		for (int k = 0; k < w.geo->nbrN[size_t(c)]; k++) {
			const int m = w.geo->nbr[size_t(6 * c + k)];
			if (m < 0 || d[size_t(m)] >= 0) continue;
			d[size_t(m)] = d[size_t(c)] + 1;
			if (m == b) return d[size_t(m)];
			q.push_back(m);
		}
	}
	return -1;
}

// hop distance from c over land only (the territory metric), -1 if not reachable within cap
std::vector<int> landHops(const World& w, int c, int cap) {
	std::vector<int> d(size_t(w.n), -1), q;
	d[size_t(c)] = 0;
	q.push_back(c);
	for (size_t h = 0; h < q.size(); h++) {
		const int x = q[h];
		if (d[size_t(x)] >= cap) continue;
		for (int k = 0; k < w.geo->nbrN[size_t(x)]; k++) {
			const int m = w.geo->nbr[size_t(6 * x + k)];
			if (m < 0 || d[size_t(m)] >= 0 || state::isSea(w, m)) continue;
			d[size_t(m)] = d[size_t(x)] + 1;
			q.push_back(m);
		}
	}
	return d;
}

// The documented territory shape, written out independently: cells within (r + roundPad) x step of c,
// reached from c over land inside that disc. Result: per cell 1 inside, -1 outside.
std::vector<int> landDisc(const World& w, int c, int r) {
	const double R = (r + w.tune<CivTune>().roundPad) * w.geo->step;
	std::vector<int> d(size_t(w.n), -1), q;
	d[size_t(c)] = 1;
	q.push_back(c);
	for (size_t h = 0; h < q.size(); h++) {
		const int x = q[h];
		for (int k = 0; k < w.geo->nbrN[size_t(x)]; k++) {
			const int m = w.geo->nbr[size_t(6 * x + k)];
			if (m < 0 || d[size_t(m)] >= 0 || state::isSea(w, m) || w.geo->angle(m, c) > R + 1e-12) continue;
			d[size_t(m)] = 1;
			q.push_back(m);
		}
	}
	return d;
}

double dotc(const World& w, int a, int b) {
	const float* pa = &w.geo->pos[size_t(3 * a)];
	const float* pb = &w.geo->pos[size_t(3 * b)];
	return double(pa[0]) * double(pb[0]) + double(pa[1]) * double(pb[1]) + double(pa[2]) * double(pb[2]);
}

// Every live settlement's cells are ascending, owned by it, land; no cell in two lists; every owned cell
// belongs to the list of its (live) owner. Returns "" or what is wrong.
std::string territoryProblem(const World& w) {
	std::vector<int> seen(size_t(w.n), -1);
	for (const Settlement& s : w.settlements) {
		if (s.dead) {
			if (!s.cells.empty()) return "dead settlement " + std::to_string(s.id) + " holds cells";
			continue;
		}
		for (size_t i = 0; i < s.cells.size(); i++) {
			const int c = s.cells[i];
			if (!w.valid(c)) return "bad cell";
			if (i && s.cells[i - 1] >= c) return "cells of " + std::to_string(s.id) + " not ascending";
			if (w.owner[size_t(c)] != s.id) return "cell " + std::to_string(c) + " listed by " + std::to_string(s.id) + " but owned by " + std::to_string(w.owner[size_t(c)]);
			if (seen[size_t(c)] >= 0) return "cell " + std::to_string(c) + " in two territories";
			seen[size_t(c)] = s.id;
		}
	}
	for (int c = 0; c < w.n; c++) {
		const int o = w.owner[size_t(c)];
		if (o < 0) continue;
		if (size_t(o) >= w.settlements.size()) return "owner out of range";
		if (w.settlements[size_t(o)].dead) return "cell " + std::to_string(c) + " owned by dead " + std::to_string(o);
		if (seen[size_t(c)] != o) return "owned cell " + std::to_string(c) + " missing from its owner's list";
	}
	return "";
}

std::vector<Event> eventsOf(const std::vector<Event>& all, Ev t) {
	std::vector<Event> out;
	for (const Event& e : all) if (e.type == t) out.push_back(e);
	return out;
}

// a land cell with hab > 0.3 that is not crowded, scanning from `from`
int freeSite(const World& w, int from = 0) {
	const bool labels = w.cache.landId.size() == size_t(w.n);
	for (int k = 0; k < w.n; k++) {
		const int c = (from + k) % w.n;
		if (labels && w.cache.landId[size_t(c)] != 0) continue;     // on the largest landmass
		if (w.hab[size_t(c)] > 0.3f && civ::canSettle(w, c).ok) return c;
	}
	return -1;
}

uint32_t mix(uint32_t h) { h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16; return h; }

// Owner map that shows each settlement: culture colour with a per-settlement shade, territory borders
// darker, ruins (unowned urban) brown, centres white (live) / red (dead).
std::function<mi::Rgb(int)> historyColors(const World& w) {
	auto deadCentres = std::make_shared<std::set<int>>();
	auto liveCentres = std::make_shared<std::set<int>>();
	for (const Settlement& s : w.settlements) {
		if (!w.valid(s.cell)) continue;
		(s.dead ? deadCentres : liveCentres)->insert(s.cell);
	}
	return [&w, deadCentres, liveCentres](int c) {
		const bool sea = state::isSea(w, c);
		if (liveCentres->count(c)) return mi::Rgb{ 255, 255, 255 };
		if (deadCentres->count(c)) return mi::Rgb{ 220, 30, 30 };
		if (sea) return mi::Rgb{ 18, 40, 90 };
		const int ow = w.owner[size_t(c)];
		if (ow < 0) {
			if (w.urban[size_t(c)] > 0.01f) return mi::Rgb{ 120, 80, 40 };
			const double h = w.hab[size_t(c)];
			return mi::Rgb{ 60 + 40 * h, 70 + 50 * h, 60 + 30 * h };
		}
		const Settlement& s = w.settlements[size_t(ow)];
		const Culture* cu = state::culture(w, s.culture);
		const std::array<double, 3> col = cu ? cu->color : std::array<double, 3>{ 1, 1, 1 };
		double f = 0.62 + 0.38 * double(mix(uint32_t(ow) * 2654435761u) % 1000) / 1000.0;
		bool border = false;
		for (int k = 0; k < w.geo->nbrN[size_t(c)]; k++) {
			const int m = w.geo->nbr[size_t(6 * c + k)];
			if (m >= 0 && !state::isSea(w, m) && w.owner[size_t(m)] != ow) border = true;
		}
		if (border) f *= 0.55;
		return mi::Rgb{ col[0] * 255 * f, col[1] * 255 * f, col[2] * 255 * f };
	};
}
bool historyMap(const World& w, const std::string& file) {
	mi::Opts o;
	o.rivers = false;
	return mi::custom(w, file, historyColors(w), o);
}
// The same colours on a lat / lon window (degrees), same projection as MapImg, W x H pixels.
bool zoomMap(const World& w, const std::string& file, double lat0, double lat1, double lon0, double lon1, int W = 1024, int H = 768) {
	const auto colorOf = historyColors(w);
	std::vector<uint8_t> rgb(size_t(W) * size_t(H) * 3);
	int hint = -1;
	for (int y = 0; y < H; y++) {
		const double lat = (lat1 + (lat0 - lat1) * (y + 0.5) / H) * kPi / 180;
		for (int x = 0; x < W; x++) {
			const double lon = (lon0 + (lon1 - lon0) * (x + 0.5) / W) * kPi / 180;
			const int c = w.geo->cellAt(std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon), hint);
			hint = c;
			const mi::Rgb k = colorOf(c);
			const size_t px = (size_t(y) * size_t(W) + size_t(x)) * 3;
			rgb[px] = uint8_t(std::clamp(k.r, 0.0, 255.0));
			rgb[px + 1] = uint8_t(std::clamp(k.g, 0.0, 255.0));
			rgb[px + 2] = uint8_t(std::clamp(k.b, 0.0, 255.0));
		}
	}
	return mi::writePng(file, W, H, rgb);
}
// lat / lon (degrees) of a cell in the map convention
void latLon(const World& w, int c, double& lat, double& lon) {
	const float* p = &w.geo->pos[size_t(3 * c)];
	lat = std::asin(std::clamp(double(p[1]), -1.0, 1.0)) * 180 / kPi;
	lon = std::atan2(double(p[2]), double(p[0])) * 180 / kPi;
}
// window around the live settlements (with a margin), for zoomMap
void windowOf(const World& w, double& lat0, double& lat1, double& lon0, double& lon1) {
	lat0 = 90; lat1 = -90; lon0 = 180; lon1 = -180;
	for (const Settlement& s : w.settlements) {
		double la, lo;
		latLon(w, s.cell, la, lo);
		lat0 = std::min(lat0, la); lat1 = std::max(lat1, la); lon0 = std::min(lon0, lo); lon1 = std::max(lon1, lo);
	}
	lat0 = std::max(-90.0, lat0 - 8); lat1 = std::min(90.0, lat1 + 8); lon0 = std::max(-180.0, lon0 - 10); lon1 = std::min(180.0, lon1 + 10);
}

} // namespace

// ==================================================================== found

DM_TEST("civ: found validity (bad cell, sea, lake, barren, crowded, culture, pop)") {
	auto w = makeWorld({ .L = 5 });
	const int cu = newCulture(*w);
	const int sea = findLand(*w) >= 0 ? [&] { for (int c = 0; c < w->n; c++) if (state::isSea(*w, c)) return c; return -1; }() : -1;
	ok(sea >= 0, "helper world has sea");
	ok(civ::found(*w, -1, cu, 30) == nullptr, "cell -1");
	ok(civ::found(*w, w->n, cu, 30) == nullptr, "cell n");
	ok(civ::found(*w, sea, cu, 30) == nullptr, "sea");
	const int land = findLand(*w, [&](int c) { return w->hab[size_t(c)] > 0.3f; });
	ok(land >= 0, "a habitable cell");
	w->water[size_t(land)] = 0.5f;
	ok(civ::found(*w, land, cu, 30) == nullptr, "lake cell");
	w->water[size_t(land)] = 0;
	const float h0 = w->hab[size_t(land)];
	w->hab[size_t(land)] = 0.05f;
	ok(civ::found(*w, land, cu, 30) == nullptr, "barren (hab 0.05)");
	w->hab[size_t(land)] = h0;
	ok(civ::found(*w, land, -1, 30) == nullptr, "no culture");
	ok(civ::found(*w, land, 99, 30) == nullptr, "unknown culture");
	ok(civ::found(*w, land, cu, 0) == nullptr, "pop 0");
	ok(civ::found(*w, land, cu, -5) == nullptr, "negative pop");
	ok(civ::found(*w, land, cu, std::numeric_limits<double>::quiet_NaN()) == nullptr, "NaN pop");
	ok(civ::found(*w, land, cu, std::numeric_limits<double>::infinity()) == nullptr, "infinite pop");
	const int dc = newCulture(*w);
	w->cultures[size_t(dc)].dead = true;
	ok(civ::found(*w, land, dc, 30) == nullptr, "dead culture");
	eq(w->settlements.size(), size_t(0), "nothing created by refusals");
	Settlement* s = civ::found(*w, land, cu, 30);
	if (!ok(s != nullptr, "valid site")) return;
	const int sid = s->id;
	// crowded: a cell within 7 hops of owned land
	int crowdedCell = -1, farCell = -1;
	for (int c = 0; c < w->n; c++) {
		if (state::isSea(*w, c) || w->hab[size_t(c)] <= 0.3f || w->owner[size_t(c)] >= 0) continue;
		int best = 99;
		for (int t : w->settlements[size_t(sid)].cells) best = std::min(best, hops(*w, c, t));
		if (best <= 7 && crowdedCell < 0) crowdedCell = c;
		if (best >= 8 && farCell < 0) farCell = c;
	}
	ok(crowdedCell >= 0 && farCell >= 0, "found a crowded and a free cell");
	ok(civ::found(*w, crowdedCell, cu, 30) == nullptr, "crowded");
	ok(civ::found(*w, farCell, cu, 30) != nullptr, "8 hops from owned land is free");
	noNaN(*w, "after found");
}

DM_TEST("civ: found sets fields, claims at once, names, capital, event, no rng draw") {
	auto w = makeWorld({ .L = 5 });
	w->year = 123;
	const int cu = newCulture(*w);
	const int c0 = freeSite(*w);
	const uint32_t rng0 = w->rng.s;
	std::vector<Event> ev = capture(*w, [&] { civ::found(*w, c0, cu, 30, FoundedBy::Awaken); });
	eq(w->rng.s, rng0, "found never draws from w.rng");
	if (!eq(w->settlements.size(), size_t(1), "one settlement")) return;
	const Settlement& s = w->settlements[0];
	eq(s.id, 0, "id == index");
	eq(s.cell, c0, "cell");
	eq(s.culture, cu, "culture");
	near(s.founded, 123, 0, "founded = year");
	near(s.lastExpand, 123, 0, "lastExpand = year");
	near(s.pop, 30, 0, "pop");
	eq(s.tier, 0, "camp");
	eq(s.radius, w->tune<EconomyTune>().radius[0], "radius of a camp");
	near(s.devotion, 0.6, 1e-12, "awakened devotion");
	near(s.love, 0.3, 1e-12, "awakened love");
	ok(!s.name.empty(), "named");
	ok(s.capital, "first settlement of the culture is its capital");
	eq(w->cultures[size_t(cu)].capital, 0, "culture.capital");
	ok(!s.cells.empty(), "territory claimed at once");
	ok(std::find(s.cells.begin(), s.cells.end(), c0) != s.cells.end(), "centre owned");
	eq(territoryProblem(*w), std::string(), "territory consistent");
	ok(w->urban[size_t(c0)] >= 0.1f - 1e-6f, "camp centre built up");
	ok(w->dirty.objects && w->dirty.color, "dirty flags");
	const auto f = eventsOf(ev, Ev::SettlementFounded);
	if (eq(f.size(), size_t(1), "one settlement:founded")) {
		const EvSettlementFounded* e = f[0].as<EvSettlementFounded>();
		eq(e->id, 0); eq(e->cell, c0); eq(e->culture, cu); ok(e->by == FoundedBy::Awaken, "by awaken");
	}
	// settlers: defaults 0.5 / 0, opts override (clamped)
	const int c1 = freeSite(*w, c0 + 1);
	Settlement* s1 = civ::found(*w, c1, cu, 40);
	if (ok(s1 != nullptr, "second")) {
		near(s1->devotion, 0.5, 1e-12, "settler default devotion");
		near(s1->love, 0, 1e-12, "settler default love");
		ok(!s1->capital, "second settlement is not the capital");
	}
	const int c2 = freeSite(*w, c1 + 1);
	civ::FoundOpts fo;
	fo.devotion = 0.83;
	fo.love = -0.4;
	Settlement* s2 = civ::found(*w, c2, cu, 40, FoundedBy::Settlers, fo);
	if (ok(s2 != nullptr, "third")) {
		near(s2->devotion, 0.83, 1e-12, "origin devotion");
		near(s2->love, -0.4, 1e-12, "origin love");
	}
	const int c3 = freeSite(*w, c2 + 1);
	fo.devotion = 7;
	fo.love = -9;
	Settlement* s3 = civ::found(*w, c3, cu, 40, FoundedBy::Settlers, fo);
	if (ok(s3 != nullptr, "fourth")) { near(s3->devotion, 1, 0, "devotion clamped"); near(s3->love, -1, 0, "love clamped"); }
	fo.devotion = std::numeric_limits<double>::quiet_NaN();
	fo.love.reset();
	const int c4 = freeSite(*w, c3 + 1);
	Settlement* s4 = civ::found(*w, c4, cu, 40, FoundedBy::Settlers, fo);
	if (ok(s4 != nullptr, "fifth")) near(s4->devotion, 0.5, 0, "NaN devotion falls back to the default");
	std::set<std::string> names;
	for (const Settlement& t : w->settlements) names.insert(t.name);
	eq(names.size(), w->settlements.size(), "names are unique");
	eq(w->rng.s, rng0, "still no draw");
	noNaN(*w, "found");
}

DM_TEST("civ: found computes K through civ::capacity (economy hook)") {
	stub::Guard guard(true);
	auto w = makeWorld({ .L = 5 });
	const int cu = newCulture(*w);
	const int c0 = freeSite(*w);
	if (stub::stubbed("economy")) {
		Settlement* s = civ::found(*w, c0, cu, 30);
		if (ok(s != nullptr)) near(s->K, 0, 0, "economy stub: K 0");
		int seenCells = -1;
		stub::hooks().civ_capacity = [&](const World&, const Settlement& t) -> double { seenCells = int(t.cells.size()); return 131.5; };
		Settlement* s1 = civ::found(*w, freeSite(*w, c0 + 1), cu, 30);
		if (ok(s1 != nullptr)) {
			near(s1->K, 131.5, 0, "K from civ::capacity");
			eq(seenCells, int(s1->cells.size()), "capacity sees the claimed territory");
			ok(seenCells > 0, "territory claimed before capacity");
		}
		stub::hooks().civ_capacity = [](const World&, const Settlement&) -> double { return std::numeric_limits<double>::quiet_NaN(); };
		Settlement* s2 = civ::found(*w, freeSite(*w, c0 + 200), cu, 30);
		if (ok(s2 != nullptr)) near(s2->K, 0, 0, "NaN capacity stored as 0");
		ok(stub::called("civ_capacity") >= 3, "capacity called by found");
	} else {
		Settlement* s = civ::found(*w, c0, cu, 30);
		if (ok(s != nullptr)) ok(std::isfinite(s->K) && s->K >= 0, "real economy: finite K");
	}
	noNaN(*w);
}

DM_TEST("civ: found refuses beyond maxSettlements (160) live settlements") {
	auto w = flat(5);
	w->tune<CivTune>().crowdHops = 0;     // pack them (the cap, not crowding, must stop the 161st)
	const int cu = newCulture(*w);
	int made = 0, c = 0;
	for (; c < w->n && made < 160; c++)
		if (w->owner[size_t(c)] < 0 && civ::found(*w, c, cu, 20)) made++;
	eq(made, 160, "160 founded");
	int spare = -1;
	for (int k = c; k < w->n; k++) if (civ::canSettle(*w, k).ok) { spare = k; break; }
	if (!ok(spare >= 0, "a free site remains")) return;
	ok(civ::found(*w, spare, cu, 20) == nullptr, "161st refused");
	civ::destroy(*w, 5, Cause::Famine);
	ok(civ::found(*w, spare, cu, 20) != nullptr, "a death makes room again");
	eq(int(civ::live(*w).size()), 160, "160 live");
	eq(territoryProblem(*w), std::string(), "territories consistent");
	// a tuned cap
	auto w2 = makeWorld({ .L = 5 });
	w2->tune<CivTune>().maxSettlements = 2;
	const int cu2 = newCulture(*w2);
	ok(civ::found(*w2, freeSite(*w2), cu2, 30) != nullptr, "1");
	ok(civ::found(*w2, freeSite(*w2, 900), cu2, 30) != nullptr, "2");
	ok(civ::found(*w2, freeSite(*w2, 1800), cu2, 30) == nullptr, "tuned cap");
}

// ==================================================================== claim / rebuild

DM_TEST("civ: claim = land within radius hops, never sea, grows with radius") {
	auto w = makeWorld({ .L = 5, .seed = 2 });
	const int cu = newCulture(*w);
	// a coastal site so the disc covers sea
	int site = -1;
	for (int c = 0; c < w->n && site < 0; c++)
		if (w->coast[size_t(c)] == 1 && w->hab[size_t(c)] > 0.2f && civ::canSettle(*w, c).ok) site = c;
	if (!ok(site >= 0, "coastal site")) return;
	Settlement* s = civ::found(*w, site, cu, 50);
	if (!ok(s != nullptr)) return;
	const int sid = s->id;
	for (int r = 2; r <= 6; r++) {
		w->settlements[size_t(sid)].radius = r;
		civ::claim(*w, sid);
		const auto d = landDisc(*w, site, r);
		std::vector<int32_t> expect;
		for (int c = 0; c < w->n; c++) if (d[size_t(c)] >= 0 && !state::isSea(*w, c)) expect.push_back(c);
		const auto& cells = w->settlements[size_t(sid)].cells;
		eq(cells.size(), expect.size(), "radius " + std::to_string(r) + ": every land cell of the round disc");
		ok(cells == expect, "radius " + std::to_string(r) + ": exactly those cells, ascending");
		bool anySea = false;
		for (int c : cells) anySea = anySea || state::isSea(*w, c);
		ok(!anySea, "no sea cell");
	}
	// the sea rises over part of the territory: re-claim drops the drowned cells
	const size_t before = w->settlements[size_t(sid)].cells.size();
	w->seaLevel = 0.15;
	civ::claim(*w, sid);
	const auto& cells = w->settlements[size_t(sid)].cells;
	bool anySea = false;
	for (int c : cells) anySea = anySea || state::isSea(*w, c);
	ok(!anySea, "sea cells dropped after the sea rose");
	ok(cells.size() < before, "territory shrank (" + std::to_string(before) + " -> " + std::to_string(cells.size()) + ")");
	for (int c = 0; c < w->n; c++) if (state::isSea(*w, c) && w->owner[size_t(c)] == sid) { fail("drowned cell still owned"); break; }
	eq(territoryProblem(*w), std::string());
	// claim of bad / dead ids does nothing
	civ::claim(*w, -1);
	civ::claim(*w, 77);
	noNaN(*w);
}

DM_TEST("civ: territory sizes by radius are round discs close to the hop counts (L 6)") {
	auto w = flat(6);
	const int cu = newCulture(*w);
	const int c = cellLL(*w, 25, 40);
	Settlement* s = civ::found(*w, c, cu, 50);
	if (!ok(s != nullptr)) return;
	const int sid = s->id;
	for (int r = 2; r <= 6; r++) {
		w->settlements[size_t(sid)].radius = r;
		civ::claim(*w, sid);
		const auto hp = landHops(*w, c, r);
		int hopN = 0;
		for (int x = 0; x < w->n; x++) hopN += hp[size_t(x)] >= 0;
		const int n = int(w->settlements[size_t(sid)].cells.size());
		double maxA = 0, minEdge = 9;
		for (int x : w->settlements[size_t(sid)].cells) maxA = std::max(maxA, w->geo->angle(c, x));
		for (int x = 0; x < w->n; x++) if (w->owner[size_t(x)] != sid) minEdge = std::min(minEdge, w->geo->angle(c, x));
		std::printf("    radius %d: %d cells (hop disc %d, hex 3r^2+3r+1 = %d); outermost %.2f steps, first outside %.2f steps\n",
			r, n, hopN, 3 * r * r + 3 * r + 1, maxA / w->geo->step, minEdge / w->geo->step);
		ok(n > 0.8 * hopN && n < 1.25 * hopN, "radius " + std::to_string(r) + ": size within 20 % of the hop disc");
		ok(maxA - minEdge < 0.35 * w->geo->step, "radius " + std::to_string(r) + ": round (inside and outside radii differ by < 0.35 step)");
	}
}

DM_TEST("civ: claim lets lake cells in, never steps over the sea") {
	auto w = flat(4);
	// a sea channel along the equator; a lake next to the settlement
	for (int c = 0; c < w->n; c++) if (std::fabs(w->geo->lat[size_t(c)]) < 0.06) w->elev[size_t(c)] = -1;
	const int site = cellLL(*w, 14, 0);
	if (!ok(!state::isSea(*w, site), "site on land")) return;
	int lake = -1;
	for (int k = 0; k < w->geo->nbrN[size_t(site)]; k++) {
		const int m = w->geo->nbr[size_t(6 * site + k)];
		if (!state::isSea(*w, m)) { lake = m; break; }
	}
	w->water[size_t(lake)] = 0.6f;
	const int cu = newCulture(*w);
	Settlement* s = civ::found(*w, site, cu, 50);
	if (!ok(s != nullptr)) return;
	const int sid = s->id;
	w->settlements[size_t(sid)].radius = 6;
	civ::claim(*w, sid);
	const auto& cells = w->settlements[size_t(sid)].cells;
	ok(std::find(cells.begin(), cells.end(), lake) != cells.end(), "lake cell is territory");
	int south = 0;
	for (int c : cells) if (w->geo->lat[size_t(c)] < 0) south++;
	eq(south, 0, "nothing across the sea channel");
}

DM_TEST("civ: claim splits contested land by the nearer centre, no overlap, hand-off") {
	auto w = flat(5);
	w->tune<CivTune>().crowdHops = 0;
	const int cu = newCulture(*w);
	const int a = cellLL(*w, 0, 0), b = cellLL(*w, 0, 9);   // about 8 hops apart at L = 5
	Settlement* sa = civ::found(*w, a, cu, 50);
	Settlement* sb = civ::found(*w, b, cu, 50);
	if (!ok(sa && sb, "two founded")) return;
	w->settlements[0].radius = 6;
	w->settlements[1].radius = 6;
	civ::claim(*w, 0);
	civ::claim(*w, 1);
	eq(territoryProblem(*w), std::string(), "after both claims");
	// every owned cell inside both radii is held by the nearer centre (ties lower id)
	const auto da = landDisc(*w, a, 6), db = landDisc(*w, b, 6);
	int contested = 0, wrong = 0, unownedInside = 0;
	for (int c = 0; c < w->n; c++) {
		const bool inA = da[size_t(c)] >= 0, inB = db[size_t(c)] >= 0;
		if (!inA && !inB) continue;
		const int o = w->owner[size_t(c)];
		if (o < 0) { unownedInside++; continue; }
		if (inA && inB) {
			contested++;
			const double xa = dotc(*w, c, a), xb = dotc(*w, c, b);
			const int want = xa > xb ? 0 : xb > xa ? 1 : 0;
			if (o != want) wrong++;
		}
	}
	ok(contested > 10, "a real contested strip (" + std::to_string(contested) + " cells)");
	eq(wrong, 0, "contested cells held by the nearer centre");
	eq(unownedInside, 0, "no hole between the two (hand-off)");
	// re-claiming in the other order changes nothing
	std::vector<int16_t> own0 = w->owner;
	civ::claim(*w, 1);
	civ::claim(*w, 0);
	ok(w->owner == own0, "claims are stable");
	// a rival that owns a cell first keeps it, even when it is nearer to the claimant
	auto w2 = flat(5);
	w2->tune<CivTune>().crowdHops = 0;
	const int cu2 = newCulture(*w2);
	Settlement* t0 = civ::found(*w2, a, cu2, 50);
	if (!ok(t0 != nullptr)) return;
	w2->settlements[0].radius = 8;
	civ::claim(*w2, 0);
	const std::vector<int16_t> own2 = w2->owner;
	{
		Settlement& t1 = state::newSettlement(*w2, b, cu2);     // a newcomer inside the older one's land
		t1.pop = 50;
		t1.radius = 6;
	}
	civ::claim(*w2, 1);
	int stolen = 0, nearer = 0;
	for (int c = 0; c < w2->n; c++) {
		if (own2[size_t(c)] != 0) continue;
		if (w2->owner[size_t(c)] != 0) stolen++;
		if (dotc(*w2, c, b) > dotc(*w2, c, a)) nearer++;
	}
	ok(nearer > 0, "some of the older territory is nearer the newcomer");
	eq(stolen, 0, "claim never takes a cell a live rival owns");
	ok(!w2->settlements[1].cells.empty(), "the newcomer gets the free land");
	eq(territoryProblem(*w2), std::string(), "older owner kept, still consistent");
	// ties between two centres at the same distance go to the lower id: two settlements on ONE centre cell
	// (every cell is exactly equidistant), claimed in either order
	auto w3 = flat(4);
	const int cu3 = newCulture(*w3);
	const int pc = cellLL(*w3, 30, 30);
	for (int k = 0; k < 2; k++) { Settlement& q = state::newSettlement(*w3, pc, cu3); q.pop = 50; q.radius = 3; }
	civ::claim(*w3, 1);
	civ::claim(*w3, 0);
	civ::claim(*w3, 1);
	eq(w3->settlements[1].cells.size(), size_t(0), "the higher id gets nothing");
	ok(w3->settlements[0].cells.size() >= 37, "the lower id holds the disc");
	eq(territoryProblem(*w3), std::string());
}


DM_TEST("civ: rebuild restores cells from owner[] without changing owner") {
	auto w = makeWorld({ .L = 5, .seed = 4 });
	const int cu = newCulture(*w);
	for (int k = 0; k < 8; k++) civ::found(*w, freeSite(*w, k * 1200), cu, 40);
	for (Settlement& s : w->settlements) s.radius = 4;
	for (size_t i = 0; i < w->settlements.size(); i++) civ::claim(*w, int(i));
	civ::destroy(*w, 2, Cause::War);
	std::vector<std::vector<int32_t>> before;
	for (const Settlement& s : w->settlements) before.push_back(s.cells);
	const std::vector<int16_t> own = w->owner;
	for (Settlement& s : w->settlements) s.cells.clear();
	civ::rebuild(*w);
	ok(w->owner == own, "owner untouched");
	bool same = true;
	for (size_t i = 0; i < before.size(); i++) same = same && before[i] == w->settlements[i].cells;
	ok(same, "cells restored exactly (ascending)");
	eq(territoryProblem(*w), std::string());
	// after a save / load round trip (cells are transient)
	std::vector<uint8_t> bytes;
	if (!ok(state::serialize(*w, bytes), "serialize")) return;
	std::string why;
	auto w2 = state::deserialize(bytes, &why);
	if (!ok(w2 != nullptr, "deserialize: " + why)) return;
	bool empty = true;
	for (const Settlement& s : w2->settlements) empty = empty && s.cells.empty();
	ok(empty, "loaded settlements have no cells yet");
	civ::rebuild(*w2);
	same = true;
	for (size_t i = 0; i < before.size(); i++) same = same && before[i] == w2->settlements[i].cells;
	ok(same, "rebuild after load restores the territories");
	ok(w2->owner == own, "loaded owner unchanged");
	// rebuild on an empty world is harmless
	auto w3 = makeWorld({ .L = 5 });
	civ::rebuild(*w3);
	ok(w3->settlements.empty());
}

// ==================================================================== destroy / damage

DM_TEST("civ: destroy releases land, keeps ruins, re-elects the capital, emits once") {
	auto w = makeWorld({ .L = 5, .seed = 3 });
	w->year = 400;
	const int cu = newCulture(*w);
	const int a = civ::found(*w, freeSite(*w), cu, 100)->id;
	const int b = civ::found(*w, freeSite(*w, 3000), cu, 300)->id;
	const int c = civ::found(*w, freeSite(*w, 7000), cu, 200)->id;
	eq(w->cultures[size_t(cu)].capital, a, "first is capital");
	w->settlements[size_t(a)].b[Building::LAUNCH] = 1;
	w->settlements[size_t(a)].ark = 0.4;
	const int centre = w->settlements[size_t(a)].cell;
	w->urban[size_t(centre)] = 0.6f;
	const std::vector<int32_t> cells = w->settlements[size_t(a)].cells;
	std::vector<Event> ev = capture(*w, [&] { civ::destroy(*w, a, Cause::Meteor); });
	const Settlement& s = w->settlements[size_t(a)];
	ok(s.dead, "dead");
	ok(s.deadYear && *s.deadYear == 400, "deadYear");
	ok(s.cause == Cause::Meteor, "cause");
	ok(s.cells.empty(), "no territory");
	ok(!s.capital && s.b[Building::LAUNCH] == 0 && s.ark == 0, "launch site and Ark gone");
	bool freed = true;
	for (int x : cells) freed = freed && w->owner[size_t(x)] == -1;
	ok(freed, "cells unowned");
	near(w->urban[size_t(centre)], 0.6, 1e-6, "ruins stay");
	eq(w->cultures[size_t(cu)].capital, b, "most populous survivor is the new capital");
	ok(w->settlements[size_t(b)].capital && !w->settlements[size_t(c)].capital, "capital flags");
	const auto d = eventsOf(ev, Ev::SettlementDestroyed);
	if (eq(d.size(), size_t(1), "one settlement:destroyed")) {
		const auto* e = d[0].as<EvSettlementDestroyed>();
		eq(e->id, a); eq(e->cell, centre); eq(e->culture, cu); ok(e->cause == Cause::Meteor);
	}
	ev = capture(*w, [&] { civ::destroy(*w, a, Cause::War); });
	ok(ev.empty(), "destroying a dead settlement does nothing");
	ok(w->settlements[size_t(a)].cause == Cause::Meteor, "cause kept");
	// a non-capital death keeps the capital; Cause::None is stored as abandoned
	civ::destroy(*w, c, Cause::None);
	ok(w->settlements[size_t(c)].cause == Cause::Abandoned, "None -> abandoned");
	eq(w->cultures[size_t(cu)].capital, b, "capital unchanged");
	// the last one: culture has no capital
	civ::destroy(*w, b, Cause::Plague);
	eq(w->cultures[size_t(cu)].capital, -1, "no settlement, no capital");
	eq(civ::live(*w).size(), size_t(0));
	civ::destroy(*w, -3, Cause::War);
	civ::destroy(*w, 50, Cause::War);
	eq(territoryProblem(*w), std::string());
	noNaN(*w);
}

DM_TEST("civ: damage kills a fraction and destroys below 3 people") {
	auto w = makeWorld({ .L = 5 });
	const int cu = newCulture(*w);
	const int a = civ::found(*w, freeSite(*w), cu, 200)->id;
	near(civ::damage(*w, a, 0.25, Cause::Quake), 50, 1e-9, "25 % of 200");
	near(w->settlements[size_t(a)].pop, 150, 1e-9);
	near(civ::damage(*w, a, 0, Cause::Quake), 0, 0, "frac 0");
	near(civ::damage(*w, a, -1, Cause::Quake), 0, 0, "negative frac");
	near(civ::damage(*w, a, std::numeric_limits<double>::quiet_NaN(), Cause::Quake), 0, 0, "NaN frac");
	near(w->settlements[size_t(a)].pop, 150, 1e-9, "untouched");
	std::vector<Event> ev = capture(*w, [&] { near(civ::damage(*w, a, 0.99, Cause::Lava), 150, 1e-9, "everyone dies (1.5 left < 3)"); });
	ok(w->settlements[size_t(a)].dead && w->settlements[size_t(a)].cause == Cause::Lava, "destroyed with the cause");
	eq(eventsOf(ev, Ev::SettlementDestroyed).size(), size_t(1));
	near(civ::damage(*w, a, 0.5, Cause::Lava), 0, 0, "dead: 0");
	const int b = civ::found(*w, freeSite(*w, 1300), cu, 10)->id;
	near(civ::damage(*w, b, 5, Cause::War), 10, 1e-12, "frac clamped to 1");
	ok(w->settlements[size_t(b)].dead);
	near(civ::damage(*w, 999, 0.5, Cause::War), 0, 0, "bad id");
	noNaN(*w);
}

// ==================================================================== capture / setCulture / electCapital

DM_TEST("civ: capture and setCulture move membership, emit, re-elect capitals") {
	auto w = makeWorld({ .L = 5, .seed = 6 });
	const int A = newCulture(*w), B = newCulture(*w);
	const int a0 = civ::found(*w, freeSite(*w), A, 500)->id;
	const int a1 = civ::found(*w, freeSite(*w, 2500), A, 300)->id;
	const int a2 = civ::found(*w, freeSite(*w, 5000), A, 100)->id;
	const int b0 = civ::found(*w, freeSite(*w, 7500), B, 50)->id;
	eq(w->cultures[size_t(A)].capital, a0);
	eq(w->cultures[size_t(B)].capital, b0);
	w->settlements[size_t(a0)].b[Building::LAUNCH] = 1;
	w->settlements[size_t(a0)].ark = 0.7;
	std::vector<Event> ev = capture(*w, [&] { civ::capture(*w, a0, B); });
	eq(w->settlements[size_t(a0)].culture, B, "conquered");
	ok(w->settlements[size_t(a0)].b[Building::LAUNCH] == 0 && w->settlements[size_t(a0)].ark == 0, "launch / ark reset");
	eq(w->cultures[size_t(A)].capital, a1, "A elects its most populous remaining town");
	ok(w->settlements[size_t(a1)].capital, "flag");
	eq(w->cultures[size_t(B)].capital, b0, "B keeps its (sticky) capital");
	ok(!w->settlements[size_t(a0)].capital, "captured city is not B's capital");
	const auto c = eventsOf(ev, Ev::SettlementCaptured);
	if (eq(c.size(), size_t(1), "one settlement:captured")) {
		const auto* e = c[0].as<EvSettlementCaptured>();
		eq(e->id, a0); eq(e->from, A); eq(e->to, B); ok(e->how == CaptureHow::War, "how war");
	}
	// schism
	const int S = newCulture(*w);
	ev = capture(*w, [&] { civ::setCulture(*w, a2, S); });
	eq(w->settlements[size_t(a2)].culture, S);
	eq(w->cultures[size_t(S)].capital, a2, "the new culture's only town becomes its capital");
	ok(w->settlements[size_t(a2)].capital);
	const auto c2 = eventsOf(ev, Ev::SettlementCaptured);
	if (eq(c2.size(), size_t(1))) ok(c2[0].as<EvSettlementCaptured>()->how == CaptureHow::Schism, "how schism");
	// no-ops: same culture, dead culture, bad ids, dead settlement
	const int D = newCulture(*w);
	w->cultures[size_t(D)].dead = true;
	ev = capture(*w, [&] {
		civ::capture(*w, a1, A);
		civ::capture(*w, a1, D);
		civ::setCulture(*w, a1, 99);
		civ::capture(*w, -1, B);
	});
	ok(ev.empty(), "no events for refused changes");
	eq(w->settlements[size_t(a1)].culture, A);
	civ::destroy(*w, b0, Cause::War);
	eq(w->cultures[size_t(B)].capital, a0, "B's capital fell: the captured city takes over");
	ev = capture(*w, [&] { civ::capture(*w, b0, A); });
	ok(ev.empty(), "a dead settlement cannot be captured");
	// invariant: each culture's capital is a live member with the flag; exactly one flag per culture
	for (const Culture& cu : w->cultures) {
		int flags = 0;
		for (const Settlement& s : w->settlements) if (!s.dead && s.culture == cu.id && s.capital) flags++;
		if (cu.capital >= 0) {
			const Settlement& cap = w->settlements[size_t(cu.capital)];
			ok(!cap.dead && cap.culture == cu.id && cap.capital, "capital valid for " + std::to_string(cu.id));
			eq(flags, 1, "one capital flag");
		} else eq(flags, 0, "no flag without a capital");
	}
	eq(territoryProblem(*w), std::string(), "territory unaffected by membership");
}

DM_TEST("civ: electCapital picks the most populous, ties lower id, resets launch / ark") {
	auto w = makeWorld({ .L = 5, .seed = 8 });
	const int A = newCulture(*w);
	const int s0 = civ::found(*w, freeSite(*w), A, 100)->id;
	const int s1 = civ::found(*w, freeSite(*w, 3000), A, 100)->id;
	const int s2 = civ::found(*w, freeSite(*w, 6000), A, 100)->id;
	eq(civ::electCapital(*w, A), s0, "tie: lower id");
	w->settlements[size_t(s0)].b[Building::LAUNCH] = 1;
	w->settlements[size_t(s0)].ark = 0.5;
	w->settlements[size_t(s2)].pop = 900;
	eq(civ::electCapital(*w, A), s2, "most populous");
	ok(w->settlements[size_t(s0)].b[Building::LAUNCH] == 0 && w->settlements[size_t(s0)].ark == 0, "old capital loses launch / ark");
	ok(!w->settlements[size_t(s0)].capital && !w->settlements[size_t(s1)].capital && w->settlements[size_t(s2)].capital, "flags");
	eq(w->cultures[size_t(A)].capital, s2);
	// re-electing the same capital keeps its progress
	w->settlements[size_t(s2)].ark = 0.3;
	w->settlements[size_t(s2)].b[Building::LAUNCH] = 1;
	eq(civ::electCapital(*w, A), s2);
	near(w->settlements[size_t(s2)].ark, 0.3, 0, "unchanged capital keeps its Ark");
	eq(int(w->settlements[size_t(s2)].b[Building::LAUNCH]), 1);
	eq(civ::electCapital(*w, -1), -1, "bad id");
	eq(civ::electCapital(*w, 42), -1, "unknown culture");
	const int E = newCulture(*w);
	eq(civ::electCapital(*w, E), -1, "culture without settlements");
	eq(w->cultures[size_t(E)].capital, -1);
}

// ==================================================================== plague

DM_TEST("civ: infect / cure (immunity, severity, events)") {
	auto w = makeWorld({ .L = 5 });
	const int cu = newCulture(*w);
	const int a = civ::found(*w, freeSite(*w), cu, 100)->id;
	std::vector<Event> ev = capture(*w, [&] { ok(civ::infect(*w, a), "infect"); });
	near(w->settlements[size_t(a)].plague, 0.6, 1e-12, "default severity 0.6");
	auto p = eventsOf(ev, Ev::SettlementPlague);
	if (eq(p.size(), size_t(1), "onset event")) { eq(p[0].as<EvSettlementPlague>()->id, a); ok(p[0].as<EvSettlementPlague>()->start, "start true"); }
	ev = capture(*w, [&] { ok(civ::infect(*w, a, 0.3), "re-infect lower"); });
	near(w->settlements[size_t(a)].plague, 0.6, 1e-12, "max(plague, severity)");
	ok(ev.empty(), "no event when already ill");
	ok(civ::infect(*w, a, 0.9));
	near(w->settlements[size_t(a)].plague, 0.9, 1e-12, "raised");
	ok(civ::infect(*w, a, 4));
	near(w->settlements[size_t(a)].plague, 1, 0, "clamped to 1");
	ok(!civ::infect(*w, a, 0), "severity 0 refused");
	ok(!civ::infect(*w, a, std::numeric_limits<double>::quiet_NaN()), "NaN refused");
	ev = capture(*w, [&] { ok(civ::cure(*w, a), "cure"); });
	near(w->settlements[size_t(a)].plague, 0, 0);
	near(w->settlements[size_t(a)].immune, 30, 0, "default 30 years immunity");
	p = eventsOf(ev, Ev::SettlementPlague);
	if (eq(p.size(), size_t(1))) ok(!p[0].as<EvSettlementPlague>()->start, "start false");
	ev = capture(*w, [&] { ok(!civ::infect(*w, a), "immune: refused"); });
	ok(ev.empty());
	near(w->settlements[size_t(a)].plague, 0, 0);
	ev = capture(*w, [&] { ok(civ::cure(*w, a, 12), "cure a healthy one"); });
	ok(ev.empty(), "no event when not ill");
	near(w->settlements[size_t(a)].immune, 12, 0);
	ok(civ::cure(*w, a, std::numeric_limits<double>::quiet_NaN()));
	near(w->settlements[size_t(a)].immune, 0, 0, "NaN years -> 0");
	ok(civ::infect(*w, a, 0.2), "no immunity left");
	civ::destroy(*w, a, Cause::Plague);
	ok(!civ::infect(*w, a), "dead: false");
	ok(!civ::cure(*w, a), "dead: false");
	ok(!civ::infect(*w, -1) && !civ::cure(*w, 99), "bad ids");
	noNaN(*w);
}

// ==================================================================== queries

DM_TEST("civ: live, at, near (order, ties)") {
	auto w = makeWorld({ .L = 5, .seed = 5 });
	const int cu = newCulture(*w);
	std::vector<int> ids;
	for (int k = 0; k < 6; k++) { Settlement* s = civ::found(*w, freeSite(*w, k * 1700), cu, 50); if (s) ids.push_back(s->id); }
	eq(int(ids.size()), 6, "six founded");
	civ::destroy(*w, ids[2], Cause::War);
	const std::vector<int> lv = civ::live(*w);
	ok(std::is_sorted(lv.begin(), lv.end()), "ascending");
	eq(lv.size(), size_t(5), "dead excluded");
	ok(std::find(lv.begin(), lv.end(), ids[2]) == lv.end());
	// at
	const Settlement& s0 = w->settlements[size_t(ids[0])];
	eq(civ::at(*w, s0.cell), ids[0], "centre");
	int other = -1;
	for (int c : s0.cells) if (c != s0.cell) { other = c; break; }
	eq(civ::at(*w, other), ids[0], "owned cell");
	eq(civ::at(*w, w->settlements[size_t(ids[2])].cell), -1, "dead settlement's centre");
	int sea = 0;
	while (sea < w->n && !state::isSea(*w, sea)) sea++;
	eq(civ::at(*w, sea), -1, "sea");
	eq(civ::at(*w, -1), -1); eq(civ::at(*w, w->n), -1);
	// a settlement centred on a cell another one owns: the centre wins
	Settlement& z = state::newSettlement(*w, other, cu);
	z.pop = 5;
	eq(civ::at(*w, other), z.id, "centred beats owning");
	// near: sorted by angle, ties by id, dead excluded
	const int probe = w->settlements[size_t(ids[1])].cell;
	const std::vector<int> nr = civ::near(*w, probe, 4.0);
	eq(nr.size(), civ::live(*w).size(), "ang 4 covers the planet");
	bool sorted = true;
	for (size_t i = 1; i < nr.size(); i++) {
		const double d0 = w->geo->angle(probe, w->settlements[size_t(nr[i - 1])].cell), d1 = w->geo->angle(probe, w->settlements[size_t(nr[i])].cell);
		if (d1 < d0 || (d1 == d0 && nr[i] < nr[i - 1])) sorted = false;
	}
	ok(sorted, "nearest first, ties by id");
	eq(nr[0], ids[1], "itself first");
	// exact ties: two settlements centred on the same cell
	Settlement& t1 = state::newSettlement(*w, probe, cu);
	t1.pop = 5;
	const int tid = t1.id;
	const std::vector<int> nr2 = civ::near(*w, probe, 0.0);
	if (eq(nr2.size(), size_t(2), "ang 0: the two at the probe")) { eq(nr2[0], ids[1], "lower id first"); eq(nr2[1], tid); }
	ok(civ::near(*w, probe, -1).empty(), "negative ang");
	ok(civ::near(*w, probe, std::numeric_limits<double>::quiet_NaN()).empty(), "NaN ang");
	ok(civ::near(*w, -1, 1).empty(), "bad cell");
}

DM_TEST("civ: canSettle reasons") {
	auto w = makeWorld({ .L = 5 });
	const int cu = newCulture(*w);
	int sea = 0;
	while (sea < w->n && !state::isSea(*w, sea)) sea++;
	ok(civ::canSettle(*w, sea).reason == civ::SettleReason::Sea, "sea");
	ok(!civ::canSettle(*w, sea).ok);
	const int land = freeSite(*w);
	auto r = civ::canSettle(*w, land);
	ok(r.ok && r.reason == civ::SettleReason::Ok, "ok");
	eq(std::string(civ::toStr(r.reason)), std::string("ok"));
	w->water[size_t(land)] = 0.04f;
	ok(civ::canSettle(*w, land).reason == civ::SettleReason::Sea, "lake cell counts as sea");
	w->water[size_t(land)] = 0.03f;
	ok(civ::canSettle(*w, land).ok, "water 0.03 is not a lake");
	w->water[size_t(land)] = 0;
	const float h = w->hab[size_t(land)];
	w->hab[size_t(land)] = 0.05f;
	ok(civ::canSettle(*w, land).reason == civ::SettleReason::Barren, "hab 0.05 barren");
	w->hab[size_t(land)] = 0.051f;
	ok(civ::canSettle(*w, land).ok, "hab 0.051 fine");
	w->hab[size_t(land)] = std::numeric_limits<float>::quiet_NaN();
	ok(civ::canSettle(*w, land).reason == civ::SettleReason::Barren, "NaN hab barren");
	w->hab[size_t(land)] = h;
	Settlement* s = civ::found(*w, land, cu, 30);
	if (!ok(s != nullptr)) return;
	ok(civ::canSettle(*w, land).reason == civ::SettleReason::Crowded, "an owned cell is crowded");
	// exactly the 7-hop rule against owned land
	int wrong = 0, crowdedN = 0;
	for (int c = 0; c < w->n; c++) {
		if (state::isSea(*w, c) || hydro::isLake(*w, c) || !(w->hab[size_t(c)] > 0.05f)) continue;
		int best = 99;
		for (int t : w->settlements[0].cells) best = std::min(best, hops(*w, c, t));
		const auto rr = civ::canSettle(*w, c);
		const bool want = best <= 7;
		if (want) crowdedN++;
		if ((rr.reason == civ::SettleReason::Crowded) != want) wrong++;
	}
	eq(wrong, 0, "crowded iff within 7 hops of owned land");
	ok(crowdedN > 20, "crowded zone of " + std::to_string(crowdedN) + " cells");
	// a dead settlement's former land is no longer crowding
	civ::destroy(*w, s->id, Cause::War);
	ok(civ::canSettle(*w, land).ok, "after destruction the site is free");
	ok(civ::canSettle(*w, -1).reason == civ::SettleReason::Sea && !civ::canSettle(*w, w->n).ok, "invalid cell");
	eq(std::string(civ::toStr(civ::SettleReason::Barren)), std::string("barren"));
	eq(std::string(civ::toStr(civ::SettleReason::Crowded)), std::string("crowded"));
	eq(std::string(civ::toStr(civ::SettleReason::Sea)), std::string("sea"));
}

DM_TEST("civ: siteScore = hab plus fresh water, coast and room") {
	auto w = flat(5, 0.5);
	const int c = cellLL(*w, 20, 40);
	const double base = civ::siteScore(*w, c);
	// on flat land with no sea / rivers: hab + 0.6 x room (room = mean hab of the 4-hop disc = 0.5)
	near(base, 0.5 + 0.6 * 0.5, 1e-6, "bare site");
	w->fresh[size_t(c)] = 1;
	near(civ::siteScore(*w, c), base + 0.12, 1e-6, "fresh water at 1 hop");
	w->fresh[size_t(c)] = 2;
	near(civ::siteScore(*w, c), base + 0.12 * 0.6, 1e-6, "fresh water at 2 hops");
	w->fresh[size_t(c)] = 255;
	w->coast[size_t(c)] = 2;
	near(civ::siteScore(*w, c), base + 0.08, 1e-6, "coast within 3 hops");
	w->coast[size_t(c)] = 255;
	w->elev[size_t(c)] = 0.05f;
	ok(civ::siteScore(*w, c) < base - 0.1, "ground barely above the sea is penalised");
	w->elev[size_t(c)] = 0.5f;
	// room: owned land around it lowers the score
	const int cu = newCulture(*w);
	w->tune<CivTune>().crowdHops = 0;
	civ::found(*w, w->geo->nbr[size_t(6 * c)], cu, 30);
	ok(civ::siteScore(*w, c) < base - 0.05, "owned land nearby leaves less room");
	// sea / lake 0; barren its hab
	w->elev[size_t(c)] = -1;
	near(civ::siteScore(*w, c), 0, 0, "sea 0");
	w->elev[size_t(c)] = 0.5f;
	w->water[size_t(c)] = 1;
	near(civ::siteScore(*w, c), 0, 0, "lake 0");
	w->water[size_t(c)] = 0;
	w->hab[size_t(c)] = 0.02f;
	near(civ::siteScore(*w, c), 0.02, 1e-7, "barren: its bare hab");
	near(civ::siteScore(*w, -1), 0, 0, "invalid");
	// on the helper world: river / coast sites outrank the bare interior on average, never NaN
	auto h = makeWorld({ .L = 5, .seed = 3 });
	double sumFresh = 0, sumDry = 0;
	int nf = 0, nd = 0;
	for (int k = 0; k < h->n; k++) {
		const double v = civ::siteScore(*h, k);
		if (!std::isfinite(v)) { fail("non-finite siteScore"); break; }
		if (!civ::canSettle(*h, k).ok || h->hab[size_t(k)] < 0.3f) continue;
		if (h->fresh[size_t(k)] <= 1) { sumFresh += v - h->hab[size_t(k)]; nf++; } else { sumDry += v - h->hab[size_t(k)]; nd++; }
	}
	if (ok(nf > 0 && nd > 0)) ok(sumFresh / nf > sumDry / nd, "fresh-water bonus visible on the helper world");
}

DM_TEST("civ: bestSite = argmax of siteScore on the most habitable landmass") {
	for (uint32_t seed = 1; seed <= 4; seed++) {
		auto w = makeWorld({ .L = 5, .seed = seed });
		const std::string tag = "seed " + std::to_string(seed);
		int bigL = -1, bigH = -1;
		for (int k = 0; k < w->cache.landCount; k++) if (w->cache.landHab[size_t(k)] > bigH) { bigH = w->cache.landHab[size_t(k)]; bigL = k; }
		const int b = civ::bestSite(*w);
		if (!ok(b >= 0, tag + ": a site")) continue;
		eq(w->cache.landId[size_t(b)], bigL, tag + ": on the landmass with the most habitable cells");
		ok(civ::canSettle(*w, b).ok, tag + ": settleable");
		int brute = -1;
		double bv = -1e9;
		for (int c = 0; c < w->n; c++) {
			if (w->cache.landId[size_t(c)] != bigL || !civ::canSettle(*w, c).ok) continue;
			const double v = civ::siteScore(*w, c);
			if (v > bv) { bv = v; brute = c; }
		}
		eq(b, brute, tag + ": exact argmax (brute force)");
		// after a settlement there, the next best respects crowding
		const int cu = newCulture(*w);
		civ::found(*w, b, cu, 30);
		const int b2 = civ::bestSite(*w);
		if (ok(b2 >= 0, tag + ": second site")) ok(civ::canSettle(*w, b2).ok, tag + ": second site is not crowded");
	}
	// no land / nothing habitable: -1
	auto sea = flat(4);
	std::fill(sea->elev.begin(), sea->elev.end(), -2.0f);
	eq(civ::bestSite(*sea), -1, "water world");
	auto bare = flat(4, 0.0);
	eq(civ::bestSite(*bare), -1, "barren world");
	// own landmass labels when the cache is absent: two islands, the larger habitable one wins; when it is
	// full the other one is used
	auto w = flat(5, 0.6);
	for (int c = 0; c < w->n; c++) {
		const double y = w->geo->pos[size_t(3 * c + 1)], x = w->geo->pos[size_t(3 * c)];
		const bool north = y > 0.75, east = x > 0.9;
		w->elev[size_t(c)] = (north || east) ? 0.5f : -1.0f;
	}
	const int b = civ::bestSite(*w);
	if (ok(b >= 0)) ok(w->geo->pos[size_t(3 * b + 1)] > 0.75, "north cap (larger) chosen without a cache");
	w->tune<CivTune>().crowdHops = 30;       // one settlement crowds the whole cap
	const int cu = newCulture(*w);
	civ::found(*w, b, cu, 30);
	const int b2 = civ::bestSite(*w);
	if (ok(b2 >= 0, "falls back to another landmass")) ok(w->geo->pos[size_t(3 * b2)] > 0.9, "east island");
}

DM_TEST("civ: ageProgress") {
	auto w = makeWorld({ .L = 5 });
	const int cu = newCulture(*w);
	Culture& c = w->cultures[size_t(cu)];
	near(civ::ageProgress(*w, cu), 0, 0, "start");
	c.science = 210;
	near(civ::ageProgress(*w, cu), 0.5, 1e-12, "half of 420 in Stone");
	c.age = Age::IRON;
	c.science = 150;
	near(civ::ageProgress(*w, cu), 0.25, 1e-12, "150 of 600 in Iron");
	c.science = 5000;
	near(civ::ageProgress(*w, cu), 1, 0, "clamped");
	c.science = -3;
	near(civ::ageProgress(*w, cu), 0, 0, "negative");
	c.science = std::numeric_limits<double>::quiet_NaN();
	near(civ::ageProgress(*w, cu), 0, 0, "NaN -> 0");
	c.age = Age::SPACE;
	c.science = 0;
	near(civ::ageProgress(*w, cu), 1, 0, "last age: 1");
	near(civ::ageProgress(*w, -1), 0, 0, "bad id");
	near(civ::ageProgress(*w, 9), 0, 0, "unknown id");
	eq(std::string(civ::kTiers[0]), std::string("camp"));
	eq(std::string(civ::kTiers[4]), std::string("metropolis"));
}

// ==================================================================== robustness / determinism

DM_TEST("civ: robust on empty worlds, bad ids and NaN, never writes NaN") {
	auto w = makeWorld({ .L = 5 });
	ok(civ::live(*w).empty());
	eq(civ::at(*w, 5), -1);
	ok(civ::near(*w, 5, 1).empty());
	civ::rebuild(*w);
	civ::claim(*w, 0);
	civ::destroy(*w, 0, Cause::War);
	near(civ::damage(*w, 0, 0.5, Cause::War), 0, 0);
	civ::capture(*w, 0, 0);
	civ::setCulture(*w, 0, 0);
	eq(civ::electCapital(*w, 0), -1);
	ok(!civ::infect(*w, 0) && !civ::cure(*w, 0));
	// a settlement with a non-finite pop and radius is handled
	const int cu = newCulture(*w);
	Settlement* s = civ::found(*w, freeSite(*w), cu, 30);
	if (!ok(s != nullptr)) return;
	const int sid = s->id;
	w->settlements[size_t(sid)].radius = -4;
	civ::claim(*w, sid);
	ok(!w->settlements[size_t(sid)].cells.empty(), "negative radius treated as 2");
	w->settlements[size_t(sid)].radius = 1000;
	civ::claim(*w, sid);
	ok(w->settlements[size_t(sid)].cells.size() < size_t(w->n), "radius capped");
	w->settlements[size_t(sid)].radius = 2;
	civ::claim(*w, sid);
	w->settlements[size_t(sid)].pop = std::numeric_limits<double>::quiet_NaN();
	eq(civ::electCapital(*w, cu), sid);
	const double k = civ::damage(*w, sid, 0.5, Cause::War);
	ok(std::isfinite(k), "damage of a NaN pop is finite");
	ok(w->settlements[size_t(sid)].dead, "a NaN population counts as nobody");
	eq(checkFinite(*w), std::string(), "no NaN anywhere");
	// a world with no geo-sized arrays (bare state::create) does not crash
	auto bare = state::create({ .seed = 3, .L = 3 });
	ok(civ::bestSite(*bare) == -1, "bare world: no habitable land");
	ok(!civ::canSettle(*bare, 0).ok);
}

DM_TEST("civ: deterministic (same calls, same world hash)") {
	auto run = [](uint32_t seed) {
		auto w = makeWorld({ .L = 5, .seed = seed });
		const int A = newCulture(*w), B = newCulture(*w);
		for (int k = 0; k < 14; k++) {
			const int c = civ::bestSite(*w);
			if (c < 0) break;
			civ::found(*w, c, (k % 2) ? A : B, 40 + k);
		}
		for (Settlement& s : w->settlements) s.radius = 3 + s.id % 3;
		for (size_t i = 0; i < w->settlements.size(); i++) civ::claim(*w, int(i));
		civ::destroy(*w, 3, Cause::Famine);
		civ::capture(*w, 4, A);
		civ::infect(*w, 5, 0.4);
		return hash(*w);
	};
	const uint32_t h1 = run(7), h2 = run(7), h3 = run(8);
	eq(h1, h2, "same seed, same hash");
	ok(h1 != h3, "different seed, different world");
}

// ==================================================================== maps (look at them)

DM_TEST("civ: owner map of 12 hand-founded settlements (L 6)") {
	auto w = makeWorld({ .L = 6, .seed = 1 });
	const int A = newCulture(*w), B = newCulture(*w), C = newCulture(*w);
	const int cus[3] = { A, B, C };
	// hand-placed on a grid of points ~9 hops apart around the middle of the largest landmass, so
	// neighbours' territories meet and the contested strips can be seen
	Vec3 mid{ 0, 0, 0 };
	for (int c = 0; c < w->n; c++)
		if (w->cache.landId[size_t(c)] == 0) mid = mid + w->geo->dir(c);
	const int c0 = w->geo->cellAt(v3::norm(mid));
	Vec3 east, north;
	w->geo->basis(c0, east, north);
	const double step = 9.5 * w->geo->step;
	int made = 0;
	for (int ring = 0; ring < 6 && made < 12; ring++) {
		for (int i = -ring; i <= ring && made < 12; i++) {
			for (int j = -ring; j <= ring && made < 12; j++) {
				if (std::max(std::abs(i), std::abs(j)) != ring) continue;
				const Vec3 d = v3::norm(w->geo->dir(c0) + east * (i * step) + north * (j * step * 0.87) + east * ((j & 1) * 0.5 * step));
				const int c = w->geo->cellAt(d);
				if (!(w->hab[size_t(c)] > 0.15f) || !civ::canSettle(*w, c).ok) continue;
				if (civ::found(*w, c, cus[made % 3], 50 + 40 * made, made == 0 ? FoundedBy::Awaken : FoundedBy::Settlers)) made++;
			}
		}
	}
	eq(made, 12, "12 founded");
	double la0, la1, lo0, lo1;
	windowOf(*w, la0, la1, lo0, lo1);
	zoomMap(*w, shot("ue-civ-owner12-camps.png"), la0, la1, lo0, lo1);
	// grow them: radius by an invented tier (2..6 hops), re-claim in id order
	for (Settlement& s : w->settlements) s.radius = 2 + (s.id * 7) % 5;
	for (size_t i = 0; i < w->settlements.size(); i++) civ::claim(*w, int(i));
	eq(territoryProblem(*w), std::string(), "consistent after growth");
	ok(mi::owner(*w, shot("ue-civ-owner12.png")), "owner map written");
	zoomMap(*w, shot("ue-civ-owner12-zoom.png"), la0, la1, lo0, lo1);
	// all at radius 6 (metropolises): the land is shared out by bisectors
	for (Settlement& s : w->settlements) s.radius = 6;
	for (size_t i = 0; i < w->settlements.size(); i++) civ::claim(*w, int(i));
	eq(territoryProblem(*w), std::string(), "consistent at radius 6");
	zoomMap(*w, shot("ue-civ-owner12-r6.png"), la0, la1, lo0, lo1);
	// contested cells: each is held by the nearest centre among the settlements whose radius reaches it
	int wrong = 0, holes = 0;
	std::vector<std::vector<int>> d;
	for (const Settlement& s : w->settlements) d.push_back(landDisc(*w, s.cell, 6));
	for (int c = 0; c < w->n; c++) {
		if (state::isSea(*w, c)) continue;
		int best = -1;
		double bd = -2;
		for (size_t i = 0; i < d.size(); i++) {
			if (d[i][size_t(c)] < 0) continue;
			const double x = dotc(*w, c, w->settlements[i].cell);
			if (x > bd) { bd = x; best = int(i); }
		}
		if (best < 0) continue;
		if (w->owner[size_t(c)] < 0) holes++;
		else if (w->owner[size_t(c)] != best) wrong++;
	}
	eq(wrong, 0, "every reachable cell held by its nearest reaching centre");
	eq(holes, 0, "no unowned holes inside the reach of a settlement");
	int owned = 0;
	for (int c = 0; c < w->n; c++) owned += w->owner[size_t(c)] >= 0;
	std::printf("    12 settlements at radius 6 own %d cells; window lat %.0f..%.0f lon %.0f..%.0f\n", owned, la0, la1, lo0, lo1);
}

DM_TEST("civ: a scripted history of founding, growth, conquest, schism and death (L 6 maps)") {
	// Economy is stubbed here, so this test plays a crude economy itself: every 10 "years" each settlement
	// grows one radius step up to 6, and at most 3 settlements (longest waiting first) send colonists to the
	// best free site within 22 hops by siteScore - 0.01 x hops (canSettle decides). Wars capture, a meteor
	// and a plague destroy, distant towns secede. Every change goes through civ::* only.
	auto w = makeWorld({ .L = 6, .seed = 3 });
	const int A = newCulture(*w);
	const int first = civ::bestSite(*w);
	if (!ok(first >= 0)) return;
	civ::found(*w, first, A, 30, FoundedBy::Awaken);
	std::vector<double> waited;
	const double t0 = nowMs();
	auto decade = [&](int steps) {
		for (int k = 0; k < steps; k++) {
			w->year += 10;
			for (int id : civ::live(*w)) {
				Settlement& s = w->settlements[size_t(id)];
				const int want = std::min(6, 2 + int((w->year - s.founded) / 60));
				if (want != s.radius) { s.radius = want; civ::claim(*w, id); }
			}
			std::vector<std::pair<double, int>> queue;
			for (int id : civ::live(*w)) {
				const Settlement& s = w->settlements[size_t(id)];
				if (w->year - s.lastExpand >= 40) queue.emplace_back(s.lastExpand, id);
			}
			std::sort(queue.begin(), queue.end());
			for (size_t q = 0; q < queue.size() && q < 3; q++) {
				const int id = queue[q].second;
				const int from = w->settlements[size_t(id)].cell, cu = w->settlements[size_t(id)].culture;
				w->settlements[size_t(id)].lastExpand = w->year;
				std::vector<int32_t> ring;
				w->geo->ring(from, 22, ring);
				int best = -1;
				double bv = -1e9;
				for (int c : ring) {
					if (!civ::canSettle(*w, c).ok) continue;
					const double v = civ::siteScore(*w, c) - 0.01 * w->geo->angle(from, c) / w->geo->step;
					if (v > bv || (v == bv && c < best)) { bv = v; best = c; }
				}
				if (best >= 0) civ::found(*w, best, cu, 40);
			}
		}
	};
	decade(12);
	historyMap(*w, shot("ue-civ-history-1-colonies.png"));
	const size_t era1 = civ::live(*w).size();
	// a second people wakes on another landmass
	const int B = newCulture(*w);
	int bSite = -1;
	double bBest = 0;
	for (int c = 0; c < w->n; c++)
		if (w->cache.landId[size_t(c)] != w->cache.landId[size_t(first)] && civ::canSettle(*w, c).ok && civ::siteScore(*w, c) > bBest) { bBest = civ::siteScore(*w, c); bSite = c; }
	if (bSite >= 0) civ::found(*w, bSite, B, 30, FoundedBy::Awaken);
	decade(24);
	historyMap(*w, shot("ue-civ-history-2-growth.png"));
	eq(territoryProblem(*w), std::string(), "era 2 consistent");
	const size_t era2 = civ::live(*w).size();
	// war: where the two cultures are nearest each other, B takes A's three nearest towns
	std::vector<std::pair<double, int>> front;
	for (int a : civ::live(*w)) {
		if (w->settlements[size_t(a)].culture != A) continue;
		double dmin = 9;
		for (int b : civ::live(*w)) if (w->settlements[size_t(b)].culture == B) dmin = std::min(dmin, w->geo->angle(w->settlements[size_t(a)].cell, w->settlements[size_t(b)].cell));
		front.emplace_back(dmin, a);
	}
	std::sort(front.begin(), front.end());
	int captured = 0;
	for (size_t k = 0; k < front.size() && captured < 3; k++) { civ::capture(*w, front[k].second, B); captured++; }
	// a meteor and a plague
	std::vector<int> aLive;
	for (int id : civ::live(*w)) if (w->settlements[size_t(id)].culture == A) aLive.push_back(id);
	int destroyed = 0;
	if (aLive.size() > 6) {
		civ::destroy(*w, aLive[aLive.size() / 2], Cause::Meteor);
		civ::destroy(*w, aLive[aLive.size() / 3], Cause::Plague);
		destroyed = 2;
	}
	// schism: A's settlements farthest from the capital secede together; then the capital falls in war
	const int S = state::newCulture(*w, A).id;
	const int capA = w->cultures[size_t(A)].capital;
	std::vector<std::pair<double, int>> far;
	for (int id : civ::live(*w)) if (w->settlements[size_t(id)].culture == A && id != capA) far.emplace_back(-w->geo->angle(w->settlements[size_t(capA)].cell, w->settlements[size_t(id)].cell), id);
	std::sort(far.begin(), far.end());
	// the pivot: a far town with the most fellow towns of A within 0.28 rad (they secede together)
	int pivot = -1;
	double pv = -1;
	for (const auto& f : far) {
		int k = 0;
		for (const auto& g : far) k += w->geo->angle(w->settlements[size_t(f.second)].cell, w->settlements[size_t(g.second)].cell) <= 0.28;
		const double v = (k >= 3 ? k : 0) * -f.first;
		if (v > pv) { pv = v; pivot = f.second; }
	}
	int seceded = 0;
	for (const auto& f : far) {
		if (pivot >= 0 && w->geo->angle(w->settlements[size_t(pivot)].cell, w->settlements[size_t(f.second)].cell) <= 0.28) { civ::setCulture(*w, f.second, S); seceded++; }
	}
	eq(w->cultures[size_t(S)].capital >= 0, true, "the new culture has a capital");
	civ::destroy(*w, capA, Cause::War);
	ok(w->cultures[size_t(A)].capital != capA, "A moved its capital");
	historyMap(*w, shot("ue-civ-history-3-schism.png"));
	decade(20);
	historyMap(*w, shot("ue-civ-history-4-late.png"));
	ok(mi::owner(*w, shot("ue-civ-history-owner.png")), "standard owner map");
	{
		double la, lo;
		latLon(*w, first, la, lo);
		zoomMap(*w, shot("ue-civ-history-4-zoom.png"), la - 32, la + 32, lo - 45, lo + 45, 1200, 860);
	}
	const double ms = nowMs() - t0;
	eq(territoryProblem(*w), std::string(), "final territories consistent");
	for (const Culture& cu : w->cultures) {
		if (cu.capital < 0) continue;
		const Settlement& cap = w->settlements[size_t(cu.capital)];
		ok(!cap.dead && cap.culture == cu.id && cap.capital, "culture " + std::to_string(cu.id) + " has a valid capital");
	}
	int perCulture[8] = {};
	int dead = 0;
	for (const Settlement& s : w->settlements) { if (s.dead) dead++; else if (s.culture >= 0 && s.culture < 8) perCulture[s.culture]++; }
	std::printf("    history (year %.0f): %zu live after era 1, %zu after era 2; captured %d, destroyed %d, seceded %d; final %zu live, %d dead; A %d, B %d, S %d; %.0f ms\n",
		w->year, era1, era2, captured, destroyed, seceded, civ::live(*w).size(), dead, perCulture[A], perCulture[B], perCulture[S], ms);
	ok(perCulture[A] > 0 && perCulture[B] > 0 && perCulture[S] > 0, "three living cultures");
	noNaN(*w, "history");
}

DM_TEST("civ: timings at L 6 (report)") {
	auto w = makeWorld({ .L = 6, .seed = 2 });
	const int A = newCulture(*w);
	double tBest = 0, tFound = 0;
	int n = 0;
	for (int k = 0; k < 60; k++) {
		double t = nowMs();
		const int c = civ::bestSite(*w);
		tBest += nowMs() - t;
		if (c < 0) break;
		t = nowMs();
		civ::found(*w, c, A, 40);
		tFound += nowMs() - t;
		n++;
	}
	for (Settlement& s : w->settlements) s.radius = 6;
	double t = nowMs();
	for (size_t i = 0; i < w->settlements.size(); i++) civ::claim(*w, int(i));
	const double tClaim = (nowMs() - t) / std::max<size_t>(1, w->settlements.size());
	t = nowMs();
	int okN = 0;
	for (int c = 0; c < w->n; c += 7) okN += civ::canSettle(*w, c).ok;
	const double tCan = (nowMs() - t) / (w->n / 7.0) * 1000.0;
	t = nowMs();
	civ::rebuild(*w);
	const double tRebuild = nowMs() - t;
	std::printf("    L6, %d settlements: bestSite %.2f ms mean, found %.3f ms mean, claim r6 %.3f ms, canSettle %.2f us, rebuild %.2f ms\n",
		n, tBest / std::max(1, n), tFound / std::max(1, n), tClaim, tCan, tRebuild);
	ok(n >= 30, "the helper planet takes at least 30 colonies (" + std::to_string(n) + ")");
	ok(tBest / std::max(1, n) < 100, "bestSite well under 100 ms");
	eq(territoryProblem(*w), std::string());
	(void)okN;
}
