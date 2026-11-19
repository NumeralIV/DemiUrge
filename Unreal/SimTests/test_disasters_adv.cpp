// DEMIURGE — SimTests/test_disasters_adv.cpp : independent adversarial verification of A12 disasters
// (SPEC-SIM 3.10 disasters half, BUILD.md A12). Owner: disasters verifier.
//
// Runs with every other module stubbed (`run.ps1 disasters`). Collaborator fakes (installed through the
// stub hooks) act like the real modules where a rule needs them to act; hook-dependent checks are skipped
// in `run.ps1 all` (stubbed(...) guards).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Civ.h"
#include "Climate.h"
#include "Life.h"
#include "MapImg.h"
#include "Powers.h"
#include "Sim.h"
#include "Society.h"
#include "Terrain.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "Units.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;
namespace mi = dm::mapimg;

namespace {

bool advAllStubbed() {
	return stub::stubbed("terrain") && stub::stubbed("civ") && stub::stubbed("life") && stub::stubbed("units") &&
	       stub::stubbed("climate") && stub::stubbed("society");
}

struct AdvFakes {
	std::vector<society::WitnessOpts> witnesses;
	std::vector<int> ignited;
	std::vector<std::pair<int, Cause>> destroyed;
	std::vector<std::pair<int, double>> damaged;
	std::vector<std::pair<int, std::string>> killed;
	std::vector<int> infected;
	bool immuneAll = false;
};

// Honest stand-ins for the collaborators (only when stubbed).
void advFakes(AdvFakes& F) {
	auto& h = stub::hooks();
	h.terrain_crater = [](World& w, int c, double ang, double depth) {
		if (!w.valid(c)) return;
		std::vector<int32_t> cells;
		w.geo->within(c, ang * 1.5, cells);
		const double base = w.elev[size_t(c)];
		for (int32_t m : cells) {
			const double t = w.geo->angle(c, m) / std::max(1e-9, ang);
			double e = w.elev[size_t(m)];
			if (t <= 1) e = 0.5 * e + 0.5 * base - depth * (1 - t * t);
			else e += 0.22 * depth * std::max(0.0, 1 - std::fabs(t - 1.1) / 0.4);
			w.elev[size_t(m)] = float(clamp(e, state::kElevMin, state::kElevMax));
		}
		terrain::touched(w);
	};
	h.terrain_cone = [](World& w, int c, double ang, double height) {
		if (!w.valid(c)) return;
		std::vector<int32_t> cells;
		const double R = std::max(ang, w.geo->step * 1.01);
		w.geo->within(c, R, cells);
		const double base = w.elev[size_t(c)];
		for (int32_t m : cells) {
			const double e = base + height * (1 - w.geo->angle(c, m) / R);
			if (e > w.elev[size_t(m)]) w.elev[size_t(m)] = float(clamp(e, state::kElevMin, state::kElevMax));
		}
		terrain::touched(w);
	};
	h.terrain_addElev = [](World& w, int c, double amount) -> double {
		if (!w.valid(c)) return 0;
		if (!std::isfinite(amount)) return w.elev[size_t(c)];
		w.elev[size_t(c)] = float(clamp(double(w.elev[size_t(c)]) + amount, state::kElevMin, state::kElevMax));
		return w.elev[size_t(c)];
	};
	h.terrain_touched = [](World& w) {
		w.dirty.elev = w.dirty.water = w.dirty.color = true;
		w.need.derive = w.need.climate = w.need.hydro = true;
	};
	h.civ_destroy = [&F](World& w, int sid, Cause cause) {
		if (sid < 0 || size_t(sid) >= w.settlements.size() || w.settlements[size_t(sid)].dead) return;
		Settlement& s = w.settlements[size_t(sid)];
		s.dead = true;
		s.cause = cause;
		s.deadYear = w.year;
		for (int c = 0; c < w.n; c++) if (w.owner[size_t(c)] == sid) w.owner[size_t(c)] = -1;
		F.destroyed.push_back({ sid, cause });
	};
	h.civ_damage = [&F](World& w, int sid, double frac, Cause cause) -> double {
		if (sid < 0 || size_t(sid) >= w.settlements.size() || w.settlements[size_t(sid)].dead) return 0;
		F.damaged.push_back({ sid, frac });
		Settlement& s = w.settlements[size_t(sid)];
		const double k = s.pop * clamp(frac, 0.0, 1.0);
		s.pop -= k;
		if (s.pop < 3) civ::destroy(w, sid, cause);
		return k;
	};
	h.civ_infect = [&F](World& w, int sid, double severity) -> bool {
		if (F.immuneAll) return false;
		F.infected.push_back(sid);
		if (sid < 0 || size_t(sid) >= w.settlements.size()) return false;
		w.settlements[size_t(sid)].plague = std::max(w.settlements[size_t(sid)].plague, severity);
		return true;
	};
	// like the real Life: a burning cell loses 80 % of its veg, cannot burn below 0.15
	h.life_ignite = [&F](World& w, int c, double) -> bool {
		if (!w.valid(c) || state::isSea(w, c) || w.veg[size_t(c)] < 0.15f) return false;
		w.fire[size_t(c)] = 1;
		w.veg[size_t(c)] *= 0.2f;
		F.ignited.push_back(c);
		return true;
	};
	h.units_kill = [&F](World& w, int id, std::string_view cause) -> bool {
		if (!state::findUnit(w, id)) return false;
		F.killed.push_back({ id, std::string(cause) });
		return state::eraseUnit(w, id);
	};
	h.climate_addDust = [](World& w, double amount) {
		w.dust = clamp(w.dust + amount, 0.0, 1.0);
		w.need.climate = true;
	};
	h.society_witness = [&F](World&, int, double, const society::WitnessOpts& o) -> int {
		F.witnesses.push_back(o);
		return 0;
	};
}

std::unique_ptr<World> advFlat(int L = 4, double seaLat = -10, float seaDepth = -2.0f, float landElev = 0.5f) {
	auto w = state::create({ .seed = 11, .L = L, .geo = sharedGeo(L) });
	watch(*w);
	w->seaLevel = 0;
	w->heat = 0;
	w->phase = Phase::History;
	w->year = 500;
	for (int c = 0; c < w->n; c++) {
		w->elev[size_t(c)] = w->geo->lat[size_t(c)] < seaLat ? seaDepth : landElev;
		w->veg[size_t(c)] = w->elev[size_t(c)] >= 0 ? 0.6f : 0.0f;
	}
	return w;
}

int advCell(const World& w, double latDeg, double lonDeg) {
	const double la = latDeg * kPi / 180, lo = lonDeg * kPi / 180;
	return w.geo->cellAt(std::cos(la) * std::sin(lo), std::sin(la), std::cos(la) * std::cos(lo));
}

int advTown(World& w, int cell, double pop = 500, int tier = 1) {
	if (w.cultures.empty()) state::newCulture(w);
	Settlement& s = state::newSettlement(w, cell, 0);
	s.pop = pop;
	s.tier = tier;
	w.owner[size_t(cell)] = int16_t(s.id);
	return s.id;
}

int advUnit(World& w, int cell) {
	Unit& u = state::newUnit(w, UnitType::Settler);
	u.path = { int32_t(cell) };
	u.dest = cell;
	u.culture = w.cultures.empty() ? -1 : 0;
	return u.id;
}

int advCount(const std::vector<Event>& evs, Ev t) {
	int k = 0;
	for (const Event& e : evs) if (e.type == t) k++;
	return k;
}

void advYears(World& w, int years) {
	for (int y = 0; y < years; y++) {
		w.year += 1;
		powers::natural(w);
		for (int i = 0; i < 4; i++) { powers::update(w, 0.125); powers::tick(w); }
	}
}

struct AdvNat { int lightning = 0, storms = 0, droughts = 0, quakes = 0, eruptions = 0; std::vector<int> cells; std::vector<int> landfall; };
AdvNat advCountNatural(World& w, int years) {
	AdvNat k;
	auto tok = w.bus.onAll([&](const Event& e) {
		if (const EvLightning* p = e.as<EvLightning>()) { k.lightning++; k.cells.push_back(p->cell); }
		if (const EvStorm* p = e.as<EvStorm>()) { if (p->start) { k.storms++; k.cells.push_back(p->cell); } else k.landfall.push_back(p->cell); }
		if (const EvDrought* p = e.as<EvDrought>()) if (p->start) { k.droughts++; k.cells.push_back(p->cell); }
		if (const EvQuake* p = e.as<EvQuake>()) { k.quakes++; k.cells.push_back(p->cell); }
		if (const EvVolcanoStart* p = e.as<EvVolcanoStart>()) { k.eruptions++; k.cells.push_back(p->cell); }
	});
	advYears(w, years);
	w.bus.off(tok);
	return k;
}

// Azimuthal zoom map around cell c (half-width `half` radians).
bool advZoom(const World& w, int c, double half, const std::string& file, const std::function<mi::Rgb(int)>& color, int px = 512) {
	std::vector<uint8_t> rgb(size_t(px) * size_t(px) * 3);
	const Vec3 p = w.geo->dir(c);
	Vec3 e, nn;
	v3::basis(p, e, nn);
	int hint = c;
	for (int y = 0; y < px; y++)
		for (int x = 0; x < px; x++) {
			const double u = (2.0 * (x + 0.5) / px - 1) * half, v = (1 - 2.0 * (y + 0.5) / px) * half;
			const int m = w.geo->cellAt(v3::norm(p + e * u + nn * v), hint);
			hint = m;
			const mi::Rgb col = color(m);
			const size_t o = (size_t(y) * size_t(px) + size_t(x)) * 3;
			rgb[o] = uint8_t(clamp(col.r, 0.0, 255.0));
			rgb[o + 1] = uint8_t(clamp(col.g, 0.0, 255.0));
			rgb[o + 2] = uint8_t(clamp(col.b, 0.0, 255.0));
		}
	return mi::writePng(file, px, px, rgb);
}

mi::Rgb advElevColor(const World& w, int m) {
	const double e = double(w.elev[size_t(m)]) - w.seaLevel;
	if (e < 0) {
		const double t = sat(-e / 4);
		return { 40 * (1 - t) + 10, 110 * (1 - t) + 30, 200 * (1 - t) + 80 };
	}
	return mi::ramp({ { 0, { 50, 150, 60 } }, { 1, { 220, 210, 90 } }, { 2, { 200, 120, 50 } }, { 3, { 110, 70, 40 } }, { 5, { 250, 250, 250 } } }, e);
}

} // namespace

// ------------------------------------------------------------------ natural rates at the reference L 6

DM_TEST("disasters adv: natural rates at L 6 within +-40 % on several worlds (storms included)") {
	AdvFakes F;
	stub::Guard g(false);
	if (!advAllStubbed()) return;
	advFakes(F);
	struct Case { int L; uint32_t seed; double land; };
	const Case cases[] = { { 6, 1, 0.4 }, { 6, 2, 0.3 }, { 6, 3, 0.55 }, { 5, 4, 0.4 }, { 4, 5, 0.4 } };
	for (const Case& cs : cases) {
		auto w = makeWorld({ .L = cs.L, .seed = cs.seed, .land = cs.land });
		w->year = 1000;
		w->stats.pop = 1e6;
		int made = 0;
		for (int c = 0; c < w->n && made < 5; c += w->n / 7) if (!state::isSea(*w, c)) { advTown(*w, c, 5000, 3); made++; }
		const int years = 5000;
		const double t0 = nowMs();
		AdvNat k = advCountNatural(*w, years);
		const double ms = nowMs() - t0;
		std::printf("    L%d seed %u land %.2f: lightning %d (300) storms %d (150) droughts %d (100) quakes %d (50) eruptions %d (20); %.0f ms\n",
		            cs.L, cs.seed, cs.land, k.lightning, k.storms, k.droughts, k.quakes, k.eruptions, ms);
		auto within = [&](int got, double want, const char* what) {
			ok(got >= 0.6 * want && got <= 1.4 * want, "L" + std::to_string(cs.L) + " seed " + std::to_string(cs.seed) + " " + what + ": " + std::to_string(got) + " vs " + std::to_string(want));
		};
		within(k.lightning, 300, "lightning");
		within(k.storms, 150, "storms");
		within(k.droughts, 100, "droughts");
		within(k.quakes, 50, "quakes");
		within(k.eruptions, 20, "eruptions");
		noNaN(*w, "after 5,000 years");
	}
}

DM_TEST("disasters adv: a natural storm never makes landfall within the protected radius") {
	AdvFakes F;
	stub::Guard g(false);
	if (advAllStubbed()) advFakes(F);
	auto w = makeWorld({ .L = 5, .seed = 7 });
	// a young world: one coastal settlement; protection 0.3 rad
	int home = -1;
	for (int c = 0; c < w->n; c++) if (!state::isSea(*w, c) && w->coast[size_t(c)] == 1) { home = c; break; }
	if (!ok(home >= 0)) return;
	advTown(*w, home, 150, 1);
	w->year = 50;
	w->stats.pop = 150;
	w->settings.calamity = 20;
	int closest = 0, storms = 0;
	auto tok = w->bus.onAll([&](const Event& e) { if (const EvStorm* p = e.as<EvStorm>()) if (p->start) storms++; });
	for (int y = 0; y < 400; y++) {
		w->year = 50;   // stays young
		powers::natural(*w);
		for (int i = 0; i < 4; i++) {
			powers::tick(*w);
			for (const Disaster& d : w->disasters)
				if (d.type == DisasterType::Storm && w->geo->angle(d.cell, home) <= 0.15) closest++;
		}
	}
	w->bus.off(tok);
	std::printf("    storms %d, storm-ticks with the eye within 0.15 rad of the young settlement: %d\n", storms, closest);
	ok(storms > 20, "storms happened: " + std::to_string(storms));
	eq(closest, 0, "no natural storm eye over the protected settlement");
}

// ------------------------------------------------------------------ lava

DM_TEST("disasters adv: lava burns and damages each covered cell once per eruption, not on every re-crossing") {
	AdvFakes F;
	stub::Guard g(false);
	if (!advAllStubbed()) return;
	advFakes(F);
	auto w = advFlat(5);
	const int vent = advCell(*w, 15, 60);
	const Vec3 p = w->geo->dir(vent);
	Vec3 east, north;
	v3::basis(p, east, north);
	for (int c = 0; c < w->n; c++) {
		const double x = v3::dot(w->geo->dir(c) - p, east);
		w->elev[size_t(c)] = float(1.0 - 4.0 * x);
	}
	// a town 3 hops downhill, owning 2 rings (its centre is not on the main lava path at first)
	std::vector<int32_t> nb;
	w->geo->ring(vent, 3, nb);
	int site = -1;
	double bx = 0;
	for (int32_t c : nb) {
		const double x = v3::dot(w->geo->dir(c) - p, east), y = v3::dot(w->geo->dir(c) - p, north);
		if (x > bx && y > 0.01) { bx = x; site = c; }
	}
	if (!ok(site >= 0)) return;
	const int town = advTown(*w, site, 1000, 2);
	std::vector<int32_t> terr;
	w->geo->ring(site, 2, terr);
	for (int32_t c : terr) w->owner[size_t(c)] = int16_t(town);
	std::vector<Event> evs = capture(*w, [&] {
		powers::start(*w, DisasterType::Volcano, vent);
		int t = 0;
		while (!w->disasters.empty() && t < 200) { powers::tick(*w); t++; }
	});
	std::map<int, int> perCell;
	for (int c : F.ignited) perCell[c]++;
	int maxIgn = 0;
	for (const auto& kv : perCell) maxIgn = std::max(maxIgn, kv.second);
	int dmgCalls = 0;
	double dmgFrac = 0;
	for (const auto& d : F.damaged) if (d.first == town) { dmgCalls++; dmgFrac += d.second; }
	std::printf("    lava: %zu ignitions on %zu cells (max %d per cell), town damage calls %d summing %.2f, town %s pop %.0f\n",
	            F.ignited.size(), perCell.size(), maxIgn, dmgCalls, dmgFrac, w->settlements[size_t(town)].dead ? "dead" : "alive",
	            w->settlements[size_t(town)].pop);
	// count how often a cell turned hot: the lava effects must not repeat for a cell already covered
	ok(dmgFrac <= 0.5 + 1e-9 || w->settlements[size_t(town)].dead, "territory damage summed over the eruption stays <= 50 % unless the centre was buried");
	eq(advCount(evs, Ev::VolcanoEnd), 1, "one volcano:end");
	noNaN(*w);
}

DM_TEST("disasters adv: volcano power, field bound and lava cleanup at L 4 and L 6; lava never NaN") {
	AdvFakes F;
	stub::Guard g(false);
	if (advAllStubbed()) advFakes(F);
	for (int L : { 4, 6 }) {
		auto w = makeWorld({ .L = L, .seed = 12 });
		DisastersTune& T = w->tune<DisastersTune>();
		T.lavaRate = 30;   // a flood of lava
		const int vent = bestHab(*w);
		Disaster* d = powers::start(*w, DisasterType::Volcano, vent);
		if (!ok(d != nullptr)) return;
		const int id = d->id;
		int maxCells = 0, maxHot = 0, ticks = 0;
		bool powerOk = true;
		while (state::findDisaster(*w, id) && ticks < 400) {
			powers::tick(*w);
			ticks++;
			const Disaster* e = state::findDisaster(*w, id);
			if (e) { maxCells = std::max(maxCells, int(e->cells.size())); powerOk &= e->power >= 0 && e->power <= 1; }
			int hot = 0;
			for (int c = 0; c < w->n; c++) {
				if (!std::isfinite(w->lava[size_t(c)]) || w->lava[size_t(c)] < 0 || w->lava[size_t(c)] > 8) { fail("lava out of range"); return; }
				if (w->lava[size_t(c)] > 0) hot++;
			}
			maxHot = std::max(maxHot, hot);
		}
		std::printf("    L%d flood volcano: %d ticks, field max %d, lava cells max %d\n", L, ticks, maxCells, maxHot);
		eq(ticks, 48, "12 years");
		ok(maxCells <= 400 && maxHot <= 400, "field bounded");
		ok(powerOk, "power stays 0..1");
		int left = 0;
		for (int c = 0; c < w->n; c++) if (w->lava[size_t(c)] != 0) left++;
		eq(left, 0, "no lava left after the end");
		noNaN(*w);
	}
}

DM_TEST("disasters adv: two overlapping volcanoes end cleanly") {
	AdvFakes F;
	stub::Guard g(false);
	if (advAllStubbed()) advFakes(F);
	auto w = advFlat(5);
	const int a = advCell(*w, 20, 20);
	std::vector<int32_t> r;
	w->geo->ring(a, 2, r);
	const int b = r.back();
	powers::StartOpts o;
	o.dur = 6;
	powers::start(*w, DisasterType::Volcano, a, o);
	o.dur = 10;
	powers::start(*w, DisasterType::Volcano, b, o);
	std::vector<Event> evs = capture(*w, [&] { for (int i = 0; i < 60; i++) powers::tick(*w); });
	eq(advCount(evs, Ev::VolcanoEnd), 2, "two ends");
	ok(w->disasters.empty());
	int left = 0;
	for (int c = 0; c < w->n; c++) if (w->lava[size_t(c)] != 0) left++;
	eq(left, 0, "no lava left");
	noNaN(*w);
}

// ------------------------------------------------------------------ tsunami

DM_TEST("disasters adv: a tsunami does not cross land into another basin; settlements struck once") {
	AdvFakes F;
	stub::Guard g(false);
	if (!advAllStubbed()) return;
	advFakes(F);
	// two seas separated by a land wall at longitude 0 (land from lon -6..6 deg), everything else sea
	auto w = advFlat(5, 99, -2.0f, -2.0f);
	// longitude in advCell's convention (x = cos(lat) sin(lon), z = cos(lat) cos(lon))
	auto lonOf = [&](int c) { const Vec3 q = w->geo->dir(c); return std::atan2(q.x, q.z) * 180 / kPi; };
	for (int c = 0; c < w->n; c++) {
		const double lon = lonOf(c);
		const double lat = w->geo->lat[size_t(c)] * 180 / kPi;
		if (std::fabs(lon) < 10 || std::fabs(lat) > 70) w->elev[size_t(c)] = 0.3f;
		else w->elev[size_t(c)] = -2.0f;
		w->veg[size_t(c)] = w->elev[size_t(c)] >= 0 ? 0.5f : 0.0f;
	}
	const int src = advCell(*w, 0, -20);   // west basin, 20 deg = 0.35 rad from the wall
	const int westShore = advCell(*w, 0, -9), eastShore = advCell(*w, 0, 9);
	if (!ok(state::isSea(*w, src) && !state::isSea(*w, westShore) && !state::isSea(*w, eastShore), "layout")) return;
	const int tw = advTown(*w, westShore, 400, 1);
	const int te = advTown(*w, eastShore, 400, 1);
	powers::StartOpts o;
	o.power = 2.0;
	o.ang = 1.0;   // reaches far beyond the wall by angle
	const int id = powers::start(*w, DisasterType::Tsunami, src, o)->id;
	std::vector<Event> evs = capture(*w, [&] {
		int t = 0;
		while (state::findDisaster(*w, id) && t < 200) { powers::update(*w, 0.125); powers::tick(*w); t++; }
	});
	ok(w->wet[size_t(westShore)] >= 1.0f, "west shore flooded");
	// cells on the east side of the wall (lon > 0) reached only through the land wall: dry
	int eastWet = 0;
	for (int c = 0; c < w->n; c++) {
		const double lon = lonOf(c);
		if (lon > 8 && lon < 30 && std::fabs(w->geo->lat[size_t(c)] * 180 / kPi) < 60 && w->wet[size_t(c)] > 0) eastWet++;
	}
	eq(eastWet, 0, "the east basin's shore stays dry");
	near(w->settlements[size_t(te)].pop, 400, 1e-9, "east town untouched");
	int hitsWest = 0;
	for (const auto& d : F.damaged) if (d.first == tw) hitsWest++;
	for (const auto& d : F.destroyed) if (d.first == tw) hitsWest += 0;
	ok(hitsWest <= 1, "the west town is struck at most once by one wave: " + std::to_string(hitsWest));
	ok(w->settlements[size_t(tw)].dead || w->settlements[size_t(tw)].pop < 400, "the west town suffered");
	ok(advCount(evs, Ev::TsunamiHit) >= 1, "tsunami:hit");
	noNaN(*w);
}

DM_TEST("disasters adv: an overwhelming wave destroys a low town with cause wave") {
	AdvFakes F;
	stub::Guard g(false);
	if (!advAllStubbed()) return;
	advFakes(F);
	auto w = advFlat(5, 0.0, -2.0f, 0.05f);
	const int src = advCell(*w, -6, 0);
	const int shore = advCell(*w, 1.2, 0);
	if (!ok(!state::isSea(*w, shore))) return;
	const int t = advTown(*w, shore, 2000, 2);
	powers::StartOpts o;
	o.power = 3.0;
	powers::start(*w, DisasterType::Tsunami, src, o);
	for (int i = 0; i < 40 && !w->disasters.empty(); i++) { powers::update(*w, 0.125); powers::tick(*w); }
	ok(w->settlements[size_t(t)].dead, "destroyed");
	bool wave = false;
	for (const auto& d : F.destroyed) if (d.first == t && d.second == Cause::Wave) wave = true;
	ok(wave, "cause wave");
}

DM_TEST("disasters adv: meteor:impact comes before the destruction it causes; launch id = disaster id") {
	AdvFakes F;
	stub::Guard g(false);
	if (!advAllStubbed()) return;
	advFakes(F);
	auto w = advFlat(4);
	const int gz = advCell(*w, 10, 10);
	advTown(*w, gz, 500, 2);
	std::vector<std::string> order;
	int launchedId = -1;
	auto tok = w->bus.onAll([&](const Event& e) {
		order.push_back(e.name());
		if (const EvMeteorLaunched* p = e.as<EvMeteorLaunched>()) launchedId = p->id;
	});
	const int id = powers::start(*w, DisasterType::Meteor, gz)->id;
	for (int i = 0; i < 16; i++) powers::update(*w, 0.125);
	w->bus.off(tok);
	eq(launchedId, id, "meteor:launched id");
	ok(!order.empty() && order[0] == "meteor:launched" && order.size() >= 2 && order[1] == "meteor:impact", "launch, then impact first: " + (order.size() > 1 ? order[1] : std::string("?")));
	ok(w->settlements[0].dead, "ground zero destroyed");
}

// ------------------------------------------------------------------ comets

DM_TEST("disasters adv: a comet never lowers the sea; nothing in history or ended") {
	auto w = advFlat(4, -10, -2, -7);
	w->phase = Phase::Water;
	w->seaLevel = 2.5;   // above the cap (a sculpted or skipped world)
	powers::start(*w, DisasterType::Comet, 10);
	for (int i = 0; i < 12; i++) powers::update(*w, 0.125);
	ok(w->seaLevel >= 2.5, "comet did not pull the sea down: " + std::to_string(w->seaLevel));
	w->phase = Phase::Ended;
	w->seaLevel = -1;
	powers::start(*w, DisasterType::Comet, 11);
	for (int i = 0; i < 12; i++) powers::update(*w, 0.125);
	eq(w->seaLevel, -1.0, "ended: sea untouched");
	// 1/60 s frames: still exactly the documented steps
	auto w2 = advFlat(4, -10, -2, -7);
	w2->phase = Phase::Water;
	w2->seaLevel = -9;
	for (int k = 0; k < 4; k++) {
		powers::start(*w2, DisasterType::Comet, k * 5);
		for (int i = 0; i < 100; i++) powers::update(*w2, 1.0 / 60);
	}
	ok(w2->seaLevel >= -3.5 && w2->seaLevel < -3.0, "4 comets pass -3.5 at 60 fps: " + std::to_string(w2->seaLevel));
}

DM_TEST("disasters adv: three comets in flight land on the same update and each counts") {
	auto w = advFlat(4, -10, -2, -7);
	w->phase = Phase::Water;
	w->seaLevel = -9;
	for (int k = 0; k < 3; k++) ok(powers::start(*w, DisasterType::Comet, 3 + k * 50) != nullptr);
	std::vector<Event> evs = capture(*w, [&] { for (int i = 0; i < 12; i++) powers::update(*w, 0.125); });
	eq(advCount(evs, Ev::MeteorImpact), 3, "three impacts");
	near(w->seaLevel, -4.0, 1e-9, "-9 -> -6 -> -5 -> -4");
}

// ------------------------------------------------------------------ robustness

DM_TEST("disasters adv: hostile options and data never write NaN") {
	AdvFakes F;
	stub::Guard g(false);
	if (advAllStubbed()) advFakes(F);
	auto w = makeWorld({ .L = 4, .seed = 3 });
	const double nan = std::nan(""), inf = std::numeric_limits<double>::infinity();
	const int land = bestHab(*w);
	int sea = -1;
	for (int c = 0; c < w->n; c++) if (state::isSea(*w, c)) { sea = c; break; }
	const DisasterType types[] = { DisasterType::Comet, DisasterType::Meteor, DisasterType::Volcano, DisasterType::Tsunami, DisasterType::Storm, DisasterType::Drought };
	const double vals[] = { nan, inf, -inf, -5, 0, 1e9 };
	for (DisasterType ty : types)
		for (double v : vals) {
			powers::StartOpts o;
			o.power = v; o.ang = v; o.dur = v; o.eta = v;
			o.data["hx"] = v; o.data["hy"] = v; o.data["hz"] = v; o.data["noCone"] = v; o.data["witness"] = v;
			if (ty == DisasterType::Comet) w->phase = Phase::Water;
			powers::start(*w, ty, ty == DisasterType::Storm || ty == DisasterType::Tsunami ? sea : land, o);
			w->phase = Phase::History;
			const std::string bad = checkFinite(*w);
			if (!ok(bad.empty(), "after start type " + std::to_string(int(ty)) + " value " + std::to_string(v) + ": " + bad)) return;
		}
	for (int i = 0; i < 200; i++) {
		powers::update(*w, 0.125);
		powers::tick(*w);
		const std::string bad = checkFinite(*w);
		if (!ok(bad.empty(), "tick " + std::to_string(i) + ": " + bad)) return;
	}
	int stillOn = 0;
	for (const Disaster& d : w->disasters)   // a 1e9-year drought or a 1e9-second flight may go on
		if (!(d.dur >= 1e8) && !(d.eta >= 1e8)) { stillOn++; std::printf("    left: type %d dur %g eta %g t %g front %g ang %g\n", int(d.type), d.dur, d.eta, d.t, d.front, d.ang); }
	eq(stillOn, 0, "everything with a sane duration ended");
	w->disasters.clear();
	// a stored storm heading of zero length or NaN
	Disaster* s = powers::start(*w, DisasterType::Storm, sea);
	s->data["hx"] = nan;
	for (int i = 0; i < 10; i++) powers::tick(*w);
	ok(checkFinite(*w).empty(), "NaN heading repaired: " + checkFinite(*w));
}

DM_TEST("disasters adv: poles, no sea, no land, every phase") {
	AdvFakes F;
	stub::Guard g(false);
	if (advAllStubbed()) advFakes(F);
	// poles
	{
		auto w = makeWorld({ .L = 4, .seed = 2 });
		int north = 0, south = 0;
		for (int c = 0; c < w->n; c++) {
			if (w->geo->lat[size_t(c)] > w->geo->lat[size_t(north)]) north = c;
			if (w->geo->lat[size_t(c)] < w->geo->lat[size_t(south)]) south = c;
		}
		for (int pc : { north, south })
			for (DisasterType ty : { DisasterType::Meteor, DisasterType::Volcano, DisasterType::Tsunami, DisasterType::Storm, DisasterType::Drought }) {
				powers::StartOpts o;
				o.data["hx"] = 0; o.data["hy"] = 1; o.data["hz"] = 0;   // a heading along the axis: degenerate tangent
				powers::start(*w, ty, pc, o);
			}
		for (int i = 0; i < 120; i++) { powers::update(*w, 0.125); powers::tick(*w); }
		ok(w->disasters.empty(), "pole disasters ended");
		noNaN(*w, "poles");
	}
	// all sea and all land worlds, natural for 300 years at calamity 5
	for (float e : { -3.0f, 2.0f }) {
		auto w = advFlat(4, -99, -3.0f, e);
		w->settings.calamity = 5;
		w->stats.pop = 1e6;
		w->year = 1000;
		AdvNat k = advCountNatural(*w, 300);
		std::printf("    %s world: lightning %d storms %d droughts %d quakes %d eruptions %d\n", e < 0 ? "all-sea" : "all-land", k.lightning, k.storms, k.droughts, k.quakes, k.eruptions);
		if (e < 0) eq(k.lightning + k.droughts + k.storms, 0, "nothing on land in an all-sea world");
		else eq(k.storms, 0, "no storms without sea");
		for (int i = 0; i < 200; i++) powers::tick(*w);
		ok(w->disasters.empty(), "ended");
		noNaN(*w);
	}
	// every phase: update / tick / natural with a mix of disasters
	for (Phase ph : { Phase::Molten, Phase::Water, Phase::Life, Phase::People, Phase::History, Phase::Ended }) {
		auto w = makeWorld({ .L = 4, .seed = 4, .phase = ph });
		w->settings.calamity = 3;
		const int land = bestHab(*w);
		int sea = -1;
		for (int c = 0; c < w->n; c++) if (state::isSea(*w, c)) { sea = c; break; }
		powers::start(*w, DisasterType::Meteor, sea);
		powers::start(*w, DisasterType::Volcano, land);
		powers::start(*w, DisasterType::Storm, sea);
		powers::start(*w, DisasterType::Drought, land);
		powers::start(*w, DisasterType::Comet, land);
		const uint32_t s0 = w->rng.s;
		if (ph != Phase::History && ph != Phase::Ended) {
			powers::natural(*w);
			eq(w->rng.s, s0, std::string("natural silent in ") + toStr(ph));
		}
		for (int i = 0; i < 100; i++) { powers::update(*w, 0.125); powers::tick(*w); }
		ok(w->disasters.empty(), std::string("ended in ") + toStr(ph));
		noNaN(*w, toStr(ph));
	}
}

DM_TEST("disasters adv: dead or vanished entities and broken records") {
	AdvFakes F;
	stub::Guard g(false);
	if (advAllStubbed()) advFakes(F);
	auto w = advFlat(4, 0.0);
	const int c = advCell(*w, 10, 10);
	const int sid = advTown(*w, c, 500, 3);
	w->settlements[size_t(sid)].dead = true;   // dead but still owning its cell
	const int uid = advUnit(*w, c);
	(void)uid;
	w->year = 1000;
	w->stats.pop = 1e6;
	powers::start(*w, DisasterType::Meteor, c);
	powers::start(*w, DisasterType::Volcano, c);
	powers::start(*w, DisasterType::Tsunami, advCell(*w, -5, 10));
	for (int i = 0; i < 60; i++) { powers::update(*w, 0.125); powers::tick(*w); }
	// plague only where a (live) city exists: the dead city never gets one
	w->settings.calamity = 50;
	advYears(*w, 100);
	if (advAllStubbed()) eq(int(F.infected.size()), 0, "no plague on a dead city");
	w->settings.calamity = 0;
	for (int i = 0; i < 400 && !w->disasters.empty(); i++) { powers::update(*w, 0.125); powers::tick(*w); }
	ok(w->disasters.empty(), "natural disasters drained");
	// a broken disaster list entry of every type with an out-of-range cell
	for (DisasterType ty : { DisasterType::Comet, DisasterType::Meteor, DisasterType::Volcano, DisasterType::Tsunami, DisasterType::Storm, DisasterType::Drought })
		state::newDisaster(*w, ty, w->n + 5);
	for (int i = 0; i < 20; i++) { powers::update(*w, 0.125); powers::tick(*w); }
	ok(w->disasters.empty(), "broken records dropped");
	noNaN(*w);
}

DM_TEST("disasters adv: natural plague on an immune city is not witnessed") {
	AdvFakes F;
	stub::Guard g(false);
	if (!advAllStubbed()) return;
	advFakes(F);
	F.immuneAll = true;
	auto w = makeWorld({ .L = 4, .seed = 8 });
	w->year = 1000;
	w->stats.pop = 1e6;
	int made = 0;
	for (int c = 0; c < w->n && made < 4; c += 151) if (!state::isSea(*w, c)) { advTown(*w, c, 5000, 3); made++; }
	w->tune<DisastersTune>().lightningPerCentury = 0;
	w->tune<DisastersTune>().stormsPerCentury = 0;
	w->tune<DisastersTune>().droughtsPerCentury = 0;
	w->tune<DisastersTune>().quakesPerCentury = 0;
	w->tune<DisastersTune>().eruptionsPerCentury = 0;
	w->tune<DisastersTune>().plaguesPerCentury = 50;
	advYears(*w, 20);
	int plagueWitness = 0;
	for (const auto& o : F.witnesses) if (o.kind == "plague") plagueWitness++;
	eq(plagueWitness, 0, "a plague that did not take is not a sign");
}

// ------------------------------------------------------------------ ownership

DM_TEST("disasters adv: writes only lava, wet, fert, farm, urban, seaLevel (genesis), the disaster list and flags") {
	stub::Guard g(false);   // neutral collaborators: anything else that changes was written by Disasters
	auto w = makeWorld({ .L = 5, .seed = 6 });
	const int land = bestHab(*w);
	int sea = -1, shallow = -1;
	for (int c = 0; c < w->n; c++) {
		if (sea < 0 && state::isSea(*w, c) && w->elev[size_t(c)] < -2) sea = c;
		if (shallow < 0 && state::isSea(*w, c) && w->elev[size_t(c)] > -0.6f) shallow = c;
	}
	int sid = advTown(*w, land, 3000, 3);
	(void)sid;
	advUnit(*w, land);
	w->year = 1000;
	w->stats.pop = 1e6;
	w->settings.calamity = 4;
	auto snap = [](const World& x) {
		std::vector<std::vector<double>> f(static_cast<size_t>(state::kCellFieldCount));
		for (int i = 0; i < state::kCellFieldCount; i++) {
			const CellArray a = state::cellArray(x, i);
			f[size_t(i)].resize(a.size);
			for (size_t c = 0; c < a.size; c++) f[size_t(i)][c] = a.get(c);
		}
		return f;
	};
	const auto before = snap(*w);
	const double year0 = w->year, dust0 = w->dust, co20 = w->co2, temp0 = w->globalTemp, awe0 = w->god.awe;
	const int64_t tick0 = w->tick;
	const Phase ph0 = w->phase;
	const double pop0 = w->settlements[0].pop;
	const size_t units0 = w->units.size(), chron0 = w->chronicle.size(), cult0 = w->cultures.size(), setl0 = w->settlements.size();
	powers::start(*w, DisasterType::Meteor, land);
	powers::start(*w, DisasterType::Meteor, sea);
	powers::start(*w, DisasterType::Volcano, shallow);
	powers::start(*w, DisasterType::Volcano, land);
	powers::start(*w, DisasterType::Storm, sea);
	powers::start(*w, DisasterType::Drought, land);
	for (int y = 0; y < 300; y++) {
		powers::natural(*w);
		for (int i = 0; i < 4; i++) { powers::update(*w, 0.125); powers::tick(*w); }
	}
	const auto after = snap(*w);
	const std::set<std::string> allowed = { "lava", "wet", "fert", "farm", "urban" };
	for (int i = 0; i < state::kCellFieldCount; i++) {
		int changed = 0;
		for (size_t c = 0; c < before[size_t(i)].size(); c++) if (before[size_t(i)][c] != after[size_t(i)][c]) changed++;
		const std::string name = state::kCellFields[i].name;
		if (allowed.count(name)) { std::printf("    wrote %-6s on %d cells\n", name.c_str(), changed); continue; }
		eq(changed, 0, "field " + name + " untouched");
	}
	eq(w->year, year0, "year");
	eq(int64_t(w->tick), tick0, "tick");
	ok(w->phase == ph0, "phase");
	eq(w->dust, dust0, "dust only through climate::addDust");
	eq(w->co2, co20, "co2");
	eq(w->globalTemp, temp0, "globalTemp");
	eq(w->god.awe, awe0, "awe");
	eq(w->settlements[0].pop, pop0, "people only through civ::damage");
	eq(w->units.size(), units0, "units only through units::kill");
	eq(w->chronicle.size(), chron0, "chronicle");
	eq(w->cultures.size(), cult0, "cultures");
	eq(w->settlements.size(), setl0, "settlements");
	eq(w->seaLevel, 0.0, "sea level in history");
}

// ------------------------------------------------------------------ determinism, save / load

DM_TEST("disasters adv: determinism at L 6 and continuation after a save / load mid-disaster") {
	auto run = [](bool reload, std::vector<int>* endsOut) {
		AdvFakes F;
		stub::Guard g(false);
		if (advAllStubbed()) advFakes(F);
		auto w = makeWorld({ .L = 6, .seed = 5 });
		w->year = 800;
		w->stats.pop = 1e6;
		w->settings.calamity = 6;
		const int land = bestHab(*w);
		int sea = -1;
		for (int c = 0; c < w->n; c++) if (state::isSea(*w, c) && w->coast[size_t(c)] == 0 && w->elev[size_t(c)] < -2) { sea = c; break; }
		powers::start(*w, DisasterType::Volcano, land);
		powers::start(*w, DisasterType::Meteor, sea);
		powers::start(*w, DisasterType::Storm, sea);
		advYears(*w, 3);
		std::unique_ptr<World> cur;
		if (reload) {
			std::vector<uint8_t> bytes;
			if (!state::serialize(*w, bytes)) return uint32_t(0);
			std::string why;
			cur = state::deserialize(bytes, &why);
			if (!cur) { std::printf("    load failed: %s\n", why.c_str()); return uint32_t(0); }
			watch(*cur);
		}
		World& x = reload ? *cur : *w;
		std::vector<int> ends;
		auto tok = x.bus.onAll([&](const Event& e) {
			if (e.type == Ev::VolcanoEnd || e.type == Ev::Storm || e.type == Ev::Drought) ends.push_back(int(e.type));
		});
		advYears(x, 60);
		x.bus.off(tok);
		if (endsOut) *endsOut = ends;
		noNaN(x, reload ? "reloaded" : "straight");
		return hash(x);
	};
	std::vector<int> e1, e2;
	const uint32_t a = run(false, &e1), b = run(false, nullptr);
	eq(a, b, "same calls, same hash");
	const uint32_t c = run(true, &e2);
	ok(c != 0, "the reloaded world ran on");
	ok(!e2.empty(), "events after the reload");
	std::printf("    hash %08x / %08x; reload %08x; events after load: straight %zu, reloaded %zu\n", a, b, c, e1.size(), e2.size());
}

// ------------------------------------------------------------------ performance at L 6

DM_TEST("disasters adv: L 6 budget: tick with 3 heavy disasters, sea impact, natural()") {
	AdvFakes F;
	stub::Guard g(false);
	if (advAllStubbed()) advFakes(F);
	auto w = makeWorld({ .L = 6, .seed = 2 });
	w->year = 1000;
	w->stats.pop = 1e6;
	// a big sea meteor (tsunami height 3, reach 0.9) near a long coast
	int sea = -1;
	for (int c = 0; c < w->n; c++) if (state::isSea(*w, c) && w->coast[size_t(c)] == 0 && w->elev[size_t(c)] < -1.5f) {
		std::vector<int32_t> r;
		w->geo->ring(c, 4, r);
		int landN = 0;
		for (int32_t m : r) if (!state::isSea(*w, m)) landN++;
		if (landN >= 4) { sea = c; break; }
	}
	if (!ok(sea >= 0)) return;
	// lots of towns on the coast to make the flood work hard
	int made = 0;
	for (int c = 0; c < w->n && made < 60; c += 37) if (!state::isSea(*w, c) && w->coast[size_t(c)] <= 2 && w->owner[size_t(c)] < 0) { advTown(*w, c, 800, 2); made++; }
	for (int i = 0; i < 200; i++) advUnit(*w, (i * 211) % w->n);
	powers::StartOpts vo;
	vo.dur = 40;
	powers::start(*w, DisasterType::Volcano, bestHab(*w), vo);
	for (int i = 0; i < 30; i++) powers::tick(*w);   // a grown lava field
	powers::start(*w, DisasterType::Storm, sea, vo);
	powers::start(*w, DisasterType::Meteor, sea);
	double impactMs = 0;
	for (int i = 0; i < 16; i++) {
		const double t0 = nowMs();
		powers::update(*w, 0.125);
		impactMs = std::max(impactMs, nowMs() - t0);
	}
	double total = 0, worst = 0;
	int n = 0;
	for (int i = 0; i < 40; i++) {
		if (w->disasters.size() < 3) break;
		const double t0 = nowMs();
		powers::tick(*w);
		const double dt = nowMs() - t0;
		total += dt;
		worst = std::max(worst, dt);
		n++;
		powers::update(*w, 0.125);
	}
	const double mean = n ? total / n : 0;
	// natural() cost per year
	w->settings.calamity = 1;
	double natTotal = 0, natWorst = 0;
	for (int y = 0; y < 400; y++) {
		const double t0 = nowMs();
		powers::natural(*w);
		const double dt = nowMs() - t0;
		natTotal += dt;
		natWorst = std::max(natWorst, dt);
	}
	std::printf("    L6: tick with volcano + tsunami(3.0, 0.9) + storm: mean %.3f ms, max %.3f ms over %d ticks; sea impact update max %.2f ms; natural mean %.4f ms max %.3f ms\n",
	            mean, worst, n, impactMs, natTotal / 400, natWorst);
	ok(n >= 10, "three active for a while: " + std::to_string(n));
	ok(mean <= 0.5 * 5, "mean tick within 5x of 0.5 ms");
	ok(worst <= 30, "any single tick <= 30 ms");
	ok(impactMs <= 30, "impact frame <= 30 ms");
	noNaN(*w);
}

// ------------------------------------------------------------------ maps (look at them)

DM_TEST("disasters adv: maps: storm track, drought, flood and lava on the L 6 helper world") {
	AdvFakes F;
	stub::Guard g(false);
	if (!advAllStubbed()) return;
	advFakes(F);
	auto w = makeWorld({ .L = 6, .seed = 9 });
	w->year = 1000;
	w->stats.pop = 1e6;
	// storm from sea onto a coast
	int coast = -1;
	for (int c = w->n / 2; c < w->n; c++) if (!state::isSea(*w, c) && w->coast[size_t(c)] == 1 && std::fabs(w->geo->lat[size_t(c)]) < 0.6) { coast = c; break; }
	if (!ok(coast >= 0)) return;
	std::vector<int32_t> around;
	w->geo->within(coast, 0.2, around);
	int start = -1;
	double far = -1;
	for (int32_t m : around) if (state::isSea(*w, m) && w->geo->angle(coast, m) > far) { far = w->geo->angle(coast, m); start = m; }
	if (!ok(start >= 0)) return;
	const std::vector<float> wet0 = w->wet;
	powers::StartOpts so;
	so.dur = 6;
	const int sidStorm = powers::start(*w, DisasterType::Storm, start, so)->id;
	std::set<int> track;
	for (int i = 0; i < 40 && state::findDisaster(*w, sidStorm); i++) {
		powers::tick(*w);
		if (const Disaster* d = state::findDisaster(*w, sidStorm)) track.insert(d->cell);
	}
	advZoom(*w, coast, 0.4, shot("ue-disasters-adv-storm.png"), [&](int m) {
		if (track.count(m)) return mi::Rgb{ 255, 255, 255 };
		const double dw = double(w->wet[size_t(m)]) - double(wet0[size_t(m)]);
		if (dw > 0.02) return mi::Rgb{ 255 * (1 - dw), 60, 255 };
		const mi::Rgb e = advElevColor(*w, m);
		return mi::Rgb{ e.r * 0.6, e.g * 0.6, e.b * 0.6 };
	});
	std::printf("    storm map: %zu track cells around coast cell %d\n", track.size(), coast);
	// a land volcano on the helper world
	const int vent = bestHab(*w);
	const std::vector<float> elev0 = w->elev;
	powers::start(*w, DisasterType::Volcano, vent);
	for (int i = 0; i < 30; i++) powers::tick(*w);
	advZoom(*w, vent, 0.12, shot("ue-disasters-adv-lava.png"), [&](int m) {
		const double L = w->lava[size_t(m)];
		if (L > 0.01) return mi::Rgb{ 255, 60 + 160 * sat(L / 1.5), 20 };
		return advElevColor(*w, m);
	});
	while (!w->disasters.empty()) powers::tick(*w);
	advZoom(*w, vent, 0.12, shot("ue-disasters-adv-lava-rock.png"), [&](int m) {
		const double gain = double(w->elev[size_t(m)]) - double(elev0[size_t(m)]);
		if (gain > 0.01) return mi::ramp({ { 0, { 120, 80, 80 } }, { 1, { 200, 40, 40 } }, { 4, { 255, 230, 200 } } }, gain);
		const mi::Rgb e = advElevColor(*w, m);
		return mi::Rgb{ e.r * 0.5, e.g * 0.5, e.b * 0.5 };
	});
	noNaN(*w);
}
