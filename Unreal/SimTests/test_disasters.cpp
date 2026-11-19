// DEMIURGE — SimTests/test_disasters.cpp : Sim/Disasters (SPEC-SIM 3.10 disasters half, BUILD.md A12).
// Owner: A12 disasters.
//
// Runs with every other module stubbed (`run.ps1 disasters`). Where a collaborator must act (Terrain
// digging the crater, Civ losing people, Life lighting fires, Units dying) the test installs small fakes
// through the stub hooks (installFakes); the call log proves the hand-offs. In `run.ps1 all` the real
// modules are linked: hook-dependent checks are skipped there (stubbed(...) guards).
#include <algorithm>
#include <cmath>
#include <cstdio>
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

bool allStubbed() {
	return stub::stubbed("terrain") && stub::stubbed("civ") && stub::stubbed("life") && stub::stubbed("units") &&
	       stub::stubbed("climate") && stub::stubbed("society");
}

struct Fakes {
	int witnessCalls = 0;
	std::vector<society::WitnessOpts> witnesses;
	std::vector<double> witnessAng;
	std::vector<double> dustAdds;
	std::vector<int> ignited;
	std::vector<std::pair<int, Cause>> destroyed;
	std::vector<std::pair<int, double>> damaged;
	std::vector<std::pair<int, std::string>> killed;
	std::vector<int> infected;
	int craters = 0, cones = 0, roughens = 0;
};

// Stand-ins for the collaborators (only when stubbed): simple but honest effects on world state.
void installFakes(Fakes& F) {
	auto& h = stub::hooks();
	h.terrain_crater = [&F](World& w, int c, double ang, double depth) {
		F.craters++;
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
	h.terrain_cone = [&F](World& w, int c, double ang, double height) {
		F.cones++;
		if (!w.valid(c)) return;
		std::vector<int32_t> cells;
		w.geo->within(c, std::max(ang, w.geo->step * 1.01), cells);
		const double base = w.elev[size_t(c)];
		const double R = std::max(ang, w.geo->step * 1.01);
		for (int32_t m : cells) {
			const double t = w.geo->angle(c, m) / R;
			const double e = base + height * (1 - t);
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
	h.terrain_roughen = [&F](World&, int, double, double, Rng&) { F.roughens++; };
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
		F.infected.push_back(sid);
		if (sid < 0 || size_t(sid) >= w.settlements.size()) return false;
		w.settlements[size_t(sid)].plague = std::max(w.settlements[size_t(sid)].plague, severity);
		return true;
	};
	h.life_ignite = [&F](World& w, int c, double) -> bool {
		if (!w.valid(c) || state::isSea(w, c) || w.veg[size_t(c)] < 0.15f) return false;
		w.fire[size_t(c)] = 1;
		F.ignited.push_back(c);
		return true;
	};
	h.units_kill = [&F](World& w, int id, std::string_view cause) -> bool {
		if (!state::findUnit(w, id)) return false;
		F.killed.push_back({ id, std::string(cause) });
		return state::eraseUnit(w, id);
	};
	h.climate_addDust = [&F](World& w, double amount) {
		F.dustAdds.push_back(amount);
		w.dust = clamp(w.dust + amount, 0.0, 1.0);
		w.need.climate = true;
	};
	h.society_witness = [&F](World&, int, double ang, const society::WitnessOpts& o) -> int {
		F.witnessCalls++;
		F.witnesses.push_back(o);
		F.witnessAng.push_back(ang);
		return 0;
	};
}

// A uniform land world (elev 0.5) with optional sea: cells with lat below seaLat (radians) are sea.
std::unique_ptr<World> flatWorld(int L = 4, double seaLat = -10, float seaDepth = -2.0f, float landElev = 0.5f) {
	auto w = state::create({ .seed = 7, .L = L, .geo = sharedGeo(L) });
	watch(*w);
	w->seaLevel = 0;
	w->heat = 0;
	w->phase = Phase::History;
	for (int c = 0; c < w->n; c++) {
		w->elev[size_t(c)] = w->geo->lat[size_t(c)] < seaLat ? seaDepth : landElev;
		w->veg[size_t(c)] = w->elev[size_t(c)] >= 0 ? 0.6f : 0.0f;
	}
	return w;
}

int cellAt(const World& w, double latDeg, double lonDeg) {
	const double la = latDeg * kPi / 180, lo = lonDeg * kPi / 180;
	return w.geo->cellAt(std::cos(la) * std::sin(lo), std::sin(la), std::cos(la) * std::cos(lo));
}

int newTown(World& w, int cell, double pop = 500, int tier = 1) {
	if (w.cultures.empty()) state::newCulture(w);
	Settlement& s = state::newSettlement(w, cell, 0);
	s.pop = pop;
	s.tier = tier;
	w.owner[size_t(cell)] = int16_t(s.id);
	return s.id;
}

int newUnitAt(World& w, int cell) {
	Unit& u = state::newUnit(w, UnitType::Settler);
	u.path = { int32_t(cell) };
	u.dest = cell;
	u.culture = w.cultures.empty() ? -1 : 0;
	return u.id;
}

int countType(const World& w, DisasterType t) {
	int k = 0;
	for (const Disaster& d : w.disasters) if (d.type == t) k++;
	return k;
}

int countEv(const std::vector<Event>& evs, Ev t) {
	int k = 0;
	for (const Event& e : evs) if (e.type == t) k++;
	return k;
}

double landShare(const World& w) {
	int k = 0;
	for (int c = 0; c < w.n; c++) if (!state::isSea(w, c)) k++;
	return double(k) / std::max(1, w.n);
}

// Zoomed map: an azimuthal window of half-width `half` radians around cell c, sampled with cellAt.
bool zoomMap(const World& w, int c, double half, const std::string& file, const std::function<mi::Rgb(int)>& color, int px = 512) {
	std::vector<uint8_t> rgb(size_t(px) * size_t(px) * 3);
	const Vec3 p = w.geo->dir(c);
	Vec3 e, nn;
	v3::basis(p, e, nn);
	int hint = c;
	for (int y = 0; y < px; y++) {
		for (int x = 0; x < px; x++) {
			const double u = (2.0 * (x + 0.5) / px - 1) * half, v = (1 - 2.0 * (y + 0.5) / px) * half;
			const Vec3 q = v3::norm(p + e * u + nn * v);
			const int m = w.geo->cellAt(q, hint);
			hint = m;
			const mi::Rgb col = color(m);
			const size_t o = (size_t(y) * size_t(px) + size_t(x)) * 3;
			rgb[o] = uint8_t(clamp(col.r, 0.0, 255.0));
			rgb[o + 1] = uint8_t(clamp(col.g, 0.0, 255.0));
			rgb[o + 2] = uint8_t(clamp(col.b, 0.0, 255.0));
		}
	}
	return mi::writePng(file, px, px, rgb);
}

// Height above sea level in strong bands (0 green, 1 yellow, 2 orange-brown, 3 dark brown, 5 white).
mi::Rgb elevColor(const World& w, int m) {
	const double e = double(w.elev[size_t(m)]) - w.seaLevel;
	if (e < 0) {
		const double t = sat(-e / 4);
		return { 40 * (1 - t) + 10, 110 * (1 - t) + 30, 200 * (1 - t) + 80 };
	}
	return mi::ramp({ { 0, { 50, 150, 60 } }, { 1, { 220, 210, 90 } }, { 2, { 200, 120, 50 } }, { 3, { 110, 70, 40 } }, { 5, { 250, 250, 250 } } }, e);
}

} // namespace

// ------------------------------------------------------------------ start

DM_TEST("disasters: start creates each type through newDisaster with the documented fields") {
	auto w = makeWorld({ .L = 4, .seed = 2 });
	stub::Guard g(true);
	const int land = findLand(*w);
	int sea = -1;
	for (int c = 0; c < w->n; c++) if (state::isSea(*w, c)) { sea = c; break; }
	if (!ok(land >= 0 && sea >= 0, "helper world has land and sea")) return;
	const int id0 = w->nextId.disaster;
	std::vector<Event> evs = capture(*w, [&] {
		Disaster* m = powers::start(*w, DisasterType::Meteor, land);
		if (ok(m != nullptr, "meteor")) {
			eq(m->id, id0, "first id from nextId");
			near(m->ang, 0.05, 1e-12, "meteor ang = crater radius");
			near(m->eta, 1.6, 1e-12, "meteor eta");
			near(m->flight, 1.6, 1e-12, "meteor flight");
			eq(m->cell, land, "meteor target");
		}
		Disaster* cm = powers::start(*w, DisasterType::Comet, sea);
		if (ok(cm != nullptr, "comet")) { near(cm->ang, 0.03, 1e-12); near(cm->eta, 1.4, 1e-12); }
		powers::StartOpts vo;
		vo.natural = true;
		Disaster* v = powers::start(*w, DisasterType::Volcano, land, vo);
		if (ok(v != nullptr, "volcano")) { near(v->ang, 0.04, 1e-12); near(v->dur, 12, 1e-12); near(v->power, 1, 1e-12); ok(v->natural); }
		Disaster* t = powers::start(*w, DisasterType::Tsunami, sea);
		if (ok(t != nullptr, "tsunami")) { near(t->front, 0, 1e-12, "front starts at 0"); ok(t->ang > 0 && t->power > 0, "reach and height"); }
		w->need.climate = false;
		Disaster* s = powers::start(*w, DisasterType::Storm, sea);
		if (ok(s != nullptr, "storm")) { ok(s->ang > 0 && s->power > 0 && s->power <= 1 && s->dur > 0, "storm fields"); }
		w->need.climate = false;
		Disaster* d = powers::start(*w, DisasterType::Drought, land);
		if (ok(d != nullptr, "drought")) { ok(d->dur >= 6 && d->dur <= 12, "drought 6-12 years"); ok(d->power >= 0.6 && d->power <= 1, "drought power"); }
		ok(w->need.climate, "drought start sets need.climate");
	});
	eq(int(w->disasters.size()), 6, "six disasters");
	for (size_t i = 1; i < w->disasters.size(); i++) ok(w->disasters[i].id > w->disasters[i - 1].id, "ascending ids");
	eq(countEv(evs, Ev::MeteorLaunched), 2, "meteor:launched for comet and meteor");
	eq(countEv(evs, Ev::VolcanoStart), 1);
	eq(countEv(evs, Ev::TsunamiStart), 1);
	eq(countEv(evs, Ev::Storm), 1);
	eq(countEv(evs, Ev::Drought), 1);
	for (const Event& e : evs) {
		if (const EvVolcanoStart* vs = e.as<EvVolcanoStart>()) ok(vs->natural, "volcano:start natural flag");
		if (const EvMeteorLaunched* ml = e.as<EvMeteorLaunched>()) ok(ml->eta > 1 && ml->id >= id0, "launch payload");
	}
	if (stub::stubbed("terrain")) eq(stub::called("terrain_cone"), 1, "the volcano builds its cone through terrain::cone");
	// refusals
	ok(powers::start(*w, DisasterType::Meteor, -1) == nullptr, "bad cell");
	ok(powers::start(*w, DisasterType::Meteor, w->n) == nullptr, "cell past the end");
	ok(powers::start(*w, DisasterType::None, land) == nullptr, "type None");
	powers::StartOpts bad;
	bad.ang = std::nan("");
	bad.power = std::numeric_limits<double>::infinity();
	ok(powers::start(*w, DisasterType::Storm, sea, bad) != nullptr, "NaN options fall back to defaults");
	noNaN(*w, "after starts");
}

DM_TEST("disasters: at most 3 comets in flight") {
	auto w = flatWorld(4, 0.0);
	w->phase = Phase::Water;
	int okCount = 0;
	for (int i = 0; i < 5; i++) if (powers::start(*w, DisasterType::Comet, i * 7)) okCount++;
	eq(okCount, 3, "only three accepted");
	for (int i = 0; i < 12; i++) powers::update(*w, 0.125);
	eq(countType(*w, DisasterType::Comet), 0, "all landed");
	ok(powers::start(*w, DisasterType::Comet, 3) != nullptr, "room again after impact");
}

// ------------------------------------------------------------------ update: flights

DM_TEST("disasters: flights count down only by the dt passed in") {
	auto w = flatWorld(4);
	Disaster* m = powers::start(*w, DisasterType::Meteor, cellAt(*w, 20, 30));
	if (!ok(m != nullptr)) return;
	const int id = m->id;
	for (int i = 0; i < 50; i++) powers::tick(*w);
	powers::update(*w, 0);
	powers::update(*w, -1);
	powers::update(*w, std::nan(""));
	if (!ok(state::findDisaster(*w, id) != nullptr, "ticks, 0, negative and NaN dt do not move it")) return;
	near(state::findDisaster(*w, id)->eta, 1.6, 1e-12);
	for (int i = 0; i < 12; i++) powers::update(*w, 0.125);
	const Disaster* d = state::findDisaster(*w, id);
	if (ok(d != nullptr, "still flying after 1.5 s")) near(d->eta, 0.1, 1e-9, "eta = 1.6 - 1.5");
	powers::update(*w, 0.125);
	ok(state::findDisaster(*w, id) == nullptr, "landed after 1.625 s");
	// frame-rate independence: 60 fps vs one big step land on the same update
	auto w2 = flatWorld(4);
	Disaster* m2 = powers::start(*w2, DisasterType::Meteor, cellAt(*w2, 20, 30));
	const int id2 = m2->id;
	int frames = 0;
	while (state::findDisaster(*w2, id2) && frames < 1000) { powers::update(*w2, 1.0 / 60); frames++; }
	ok(frames >= 96 && frames <= 97, "1.6 s at 60 fps = 96-97 frames, got " + std::to_string(frames));
}

DM_TEST("disasters: a meteor lands within 16 updates: crater, destruction, ring damage, units, fires, dust, event") {
	Fakes F;
	stub::Guard g(true);
	if (allStubbed()) installFakes(F);
	auto w = flatWorld(5);
	const int gz = cellAt(*w, 10, 40);
	// a settlement at ground zero, one in the outer ring, one far away
	const int sZero = newTown(*w, gz, 800, 2);
	std::vector<int32_t> ring;
	w->geo->ring(gz, 2, ring);
	int outerCell = -1;
	for (int32_t c : ring) {
		const double a = w->geo->angle(gz, c);
		if (a > 0.05 && a <= 0.125) { outerCell = c; break; }
	}
	if (!ok(outerCell >= 0, "an outer-ring cell")) return;
	const int sRing = newTown(*w, outerCell, 400, 1);
	const int sFar = newTown(*w, cellAt(*w, -30, -100), 400, 1);
	const int u1 = newUnitAt(*w, gz), u2 = newUnitAt(*w, cellAt(*w, -30, -100));
	const float before = w->elev[size_t(gz)];
	const double dust0 = w->dust;
	Disaster* m = powers::start(*w, DisasterType::Meteor, gz);
	if (!ok(m != nullptr)) return;
	std::vector<Event> evs = capture(*w, [&] { for (int i = 0; i < 16; i++) powers::update(*w, 0.125); });
	eq(countType(*w, DisasterType::Meteor), 0, "landed");
	eq(countEv(evs, Ev::MeteorImpact), 1, "meteor:impact");
	for (const Event& e : evs)
		if (const EvMeteorImpact* mi2 = e.as<EvMeteorImpact>()) {
			eq(mi2->cell, gz);
			eq(mi2->kind, ImpactKind::Meteor);
			ok(!mi2->sea, "on land");
			near(mi2->ang, 0.05, 1e-12);
		}
	if (stub::stubbed("terrain")) eq(stub::called("terrain_crater"), 1, "terrain::crater called once");
	eq(countType(*w, DisasterType::Tsunami), 0, "no tsunami on land");
	if (!allStubbed()) return;
	ok(w->elev[size_t(gz)] < before - 1.0f, "a bowl was dug");
	ok(w->settlements[size_t(sZero)].dead && w->settlements[size_t(sZero)].cause == Cause::Meteor, "ground zero destroyed by the meteor");
	ok(!w->settlements[size_t(sRing)].dead, "outer ring survives");
	near(w->settlements[size_t(sRing)].pop, 200, 1e-6, "outer ring lost 50 %");
	near(w->settlements[size_t(sFar)].pop, 400, 1e-9, "far town untouched");
	ok(state::findUnit(*w, u1) == nullptr && state::findUnit(*w, u2) != nullptr, "unit at ground zero killed, far unit alive");
	ok(!F.killed.empty() && F.killed[0].second == "meteor", "unit cause meteor");
	ok(F.ignited.size() >= 3, "fires lit around the crater: " + std::to_string(F.ignited.size()));
	near(w->dust - dust0, 0.25, 1e-12, "dust +0.25");
	noNaN(*w);
}

DM_TEST("disasters: a meteor in the sea starts a tsunami instead of fires") {
	Fakes F;
	stub::Guard g(true);
	if (allStubbed()) installFakes(F);
	auto w = flatWorld(4, 0.0);
	const int c = cellAt(*w, -40, 10);
	if (!ok(state::isSea(*w, c))) return;
	powers::start(*w, DisasterType::Meteor, c);
	std::vector<Event> evs = capture(*w, [&] { for (int i = 0; i < 16; i++) powers::update(*w, 0.125); });
	eq(countType(*w, DisasterType::Tsunami), 1, "a tsunami");
	eq(countEv(evs, Ev::TsunamiStart), 1, "tsunami:start");
	bool seaFlag = false;
	for (const Event& e : evs) if (const EvMeteorImpact* p = e.as<EvMeteorImpact>()) seaFlag = p->sea;
	ok(seaFlag, "meteor:impact sea = true");
	if (allStubbed()) eq(int(F.ignited.size()), 0, "no fires at sea");
	const Disaster& t = w->disasters.back();
	near(t.power, 3.0, 1e-12, "meteor wave height");
}

// ------------------------------------------------------------------ comets

DM_TEST("disasters: comets fill the sea: -6.0 first, 4 pass -3.5, about 12 reach 0, cap +1.5") {
	auto w = flatWorld(4, -10, -2, -7);   // all low ground
	w->phase = Phase::Water;
	w->seaLevel = -9;
	std::vector<double> levels;
	for (int k = 0; k < 40; k++) {
		if (!powers::start(*w, DisasterType::Comet, (k * 37) % w->n)) { fail("comet refused"); return; }
		for (int i = 0; i < 12; i++) powers::update(*w, 0.125);
		levels.push_back(w->seaLevel);
	}
	near(levels[0], -6.0, 1e-12, "first comet");
	int toGate = 0, toZero = 0;
	for (size_t i = 0; i < levels.size(); i++) {
		if (!toGate && levels[i] >= -3.5) toGate = int(i) + 1;
		if (!toZero && levels[i] >= 0) toZero = int(i) + 1;
	}
	eq(toGate, 4, "comets to pass -3.5");
	ok(toZero >= 11 && toZero <= 14, "comets to reach 0: " + std::to_string(toZero));
	near(levels.back(), 1.5, 1e-9, "hard cap");
	for (size_t i = 1; i < levels.size(); i++) ok(levels[i] >= levels[i - 1], "never falls");
	std::printf("    comets: gate %d, zero %d, levels %.2f %.2f %.2f %.2f ... %.2f\n", toGate, toZero, levels[0], levels[1], levels[2], levels[3], levels.back());
	// in history a comet leaves the sea alone
	w->phase = Phase::History;
	const double s0 = w->seaLevel;
	powers::start(*w, DisasterType::Comet, 5);
	for (int i = 0; i < 12; i++) powers::update(*w, 0.125);
	eq(w->seaLevel, s0, "no sea change in history");
}

// ------------------------------------------------------------------ volcano

namespace {
struct VolcanoRun {
	int vent = -1;
	int maxField = 0;
	double dust = 0;
	std::vector<Event> evs;
	bool lavaLeft = false;
	int ticks = 0;
};
VolcanoRun runVolcano(World& w, int vent, int maxTicks = 200) {
	VolcanoRun r;
	r.vent = vent;
	const double dust0 = w.dust;
	r.evs = capture(w, [&] {
		powers::start(w, DisasterType::Volcano, vent);
		while (!w.disasters.empty() && r.ticks < maxTicks) {
			powers::tick(w);
			r.ticks++;
			for (const Disaster& d : w.disasters) r.maxField = std::max(r.maxField, int(d.cells.size()));
			int hot = 0;
			for (int c = 0; c < w.n; c++) if (w.lava[size_t(c)] > 0) hot++;
			r.maxField = std::max(r.maxField, hot);
		}
	});
	r.dust = w.dust - dust0;
	for (int c = 0; c < w.n; c++) if (w.lava[size_t(c)] != 0) r.lavaLeft = true;
	return r;
}
} // namespace

DM_TEST("disasters: a volcano builds a cone, lava runs downhill, burns, ruins farms, hurts towns, cools into rock, ends with ash") {
	Fakes F;
	stub::Guard g(true);
	if (!allStubbed()) return;
	installFakes(F);
	auto w = flatWorld(5);
	// a tilted plain: elevation falls toward the east of the vent
	const int vent = cellAt(*w, 15, 60);
	const Vec3 p = w->geo->dir(vent);
	Vec3 east, north;
	v3::basis(p, east, north);
	for (int c = 0; c < w->n; c++) {
		const double x = v3::dot(w->geo->dir(c) - p, east);
		w->elev[size_t(c)] = float(1.0 - 4.0 * x);
		w->veg[size_t(c)] = 0.6f;
		w->farm[size_t(c)] = 0.5f;
		w->urban[size_t(c)] = 0.2f;
		w->fert[size_t(c)] = 0.5f;
	}
	// a town downhill (east) of the vent, owning a few cells around it
	std::vector<int32_t> nb;
	w->geo->ring(vent, 4, nb);
	int downhill = -1;
	double bx = 0;
	for (int32_t c : nb) {
		const double x = v3::dot(w->geo->dir(c) - p, east);
		if (x > bx && std::fabs(v3::dot(w->geo->dir(c) - p, north)) < 0.01) { bx = x; downhill = c; }
	}
	if (!ok(downhill >= 0, "a downhill cell")) return;
	const int town = newTown(*w, downhill, 600, 1);
	std::vector<int32_t> terr;
	w->geo->ring(downhill, 1, terr);
	for (int32_t c : terr) w->owner[size_t(c)] = int16_t(town);
	const std::vector<float> elev0 = w->elev;
	const float vent0 = w->elev[size_t(vent)];
	VolcanoRun r = runVolcano(*w, vent);
	eq(F.cones, 1, "cone built once");
	ok(w->elev[size_t(vent)] > vent0 + 1.5f, "the vent stands higher (cone + rock)");
	eq(countEv(r.evs, Ev::VolcanoStart), 1);
	eq(countEv(r.evs, Ev::VolcanoEnd), 1, "volcano:end");
	ok(w->disasters.empty(), "removed after dur");
	near(r.ticks, 48, 0.5, "12 years = 48 ticks");
	ok(!r.lavaLeft, "all lava cooled at the end");
	ok(r.maxField > 3 && r.maxField <= 400, "lava field " + std::to_string(r.maxField) + " cells");
	// downhill flow: rock (elevation gain beyond the cone) mostly east of the vent
	double gainE = 0, gainW = 0;
	int rockCells = 0;
	for (int c = 0; c < w->n; c++) {
		const double gain = double(w->elev[size_t(c)]) - double(elev0[size_t(c)]);
		if (gain <= 1e-4 || w->geo->angle(vent, c) <= 0.045) continue;
		rockCells++;
		(v3::dot(w->geo->dir(c) - p, east) > 0 ? gainE : gainW) += gain;
	}
	ok(rockCells >= 3, "lava turned to rock beyond the cone: " + std::to_string(rockCells) + " cells");
	ok(gainE > 3 * gainW, "rock lies downhill: east " + std::to_string(gainE) + " west " + std::to_string(gainW));
	ok(F.ignited.size() >= 3, "lava ignites vegetation: " + std::to_string(F.ignited.size()));
	int cleared = 0;
	for (int c : F.ignited) if (w->farm[size_t(c)] == 0 && w->urban[size_t(c)] == 0) cleared++;
	eq(cleared, int(F.ignited.size()), "farms and towns under lava are cleared");
	ok(w->settlements[size_t(town)].dead || w->settlements[size_t(town)].pop < 600, "the town downhill suffered");
	bool lavaCause = false;
	for (const auto& d : F.destroyed) if (d.second == Cause::Lava) lavaCause = true;
	for (const auto& d : F.damaged) if (d.first == town) lavaCause = true;
	ok(lavaCause, "damage with cause lava");
	near(r.dust, 0.03 * 12 * 0.75, 0.011, "dust 0.03 per erupting year");
	// ash: fertility up within 3 x the cone radius, unchanged far away
	const int nearCell = nb.size() > 3 ? int(nb[3]) : vent;
	ok(w->fert[size_t(nearCell)] > 0.75f, "ash fertility near the vent");
	eq(w->fert[size_t(cellAt(*w, -40, -120))], 0.5f, "fertility far away unchanged");
	std::printf("    volcano: field max %d cells, rock cells %d, gain E %.2f W %.2f, fires %zu, dust %.3f\n", r.maxField, rockCells, gainE, gainW, F.ignited.size(), r.dust);
	noNaN(*w);
}

DM_TEST("disasters: a volcano in shallow sea makes a new island; the field stays <= 400 cells") {
	Fakes F;
	stub::Guard g(true);
	if (!allStubbed()) return;
	installFakes(F);
	auto w = flatWorld(6, 0.3, -0.6f);   // all sea at 0.6 depth below 17 deg N
	const int vent = cellAt(*w, -10, 0);
	if (!ok(state::isSea(*w, vent))) return;
	int island0 = 0;
	std::vector<int32_t> area;
	w->geo->within(vent, 0.3, area);
	for (int32_t c : area) if (!state::isSea(*w, c)) island0++;
	eq(island0, 0, "open sea before");
	VolcanoRun r = runVolcano(*w, vent);
	int island = 0;
	for (int32_t c : area) if (!state::isSea(*w, c)) island++;
	ok(!state::isSea(*w, vent), "the vent is land now");
	ok(island >= 5, "new island cells: " + std::to_string(island));
	ok(r.maxField <= 400, "field <= 400");
	std::printf("    island: %d new land cells, field max %d\n", island, r.maxField);
	// a huge eruption is still bounded
	auto w2 = flatWorld(6);
	DisastersTune& T = w2->tune<DisastersTune>();
	T.lavaRate = 60;
	T.coolLand = 0.0;
	VolcanoRun r2 = runVolcano(*w2, cellAt(*w2, 5, 5));
	ok(r2.maxField <= 400, "bounded field under a flood of lava: " + std::to_string(r2.maxField));
	ok(r2.maxField >= 300, "and it did spread: " + std::to_string(r2.maxField));
	noNaN(*w2);
}

// ------------------------------------------------------------------ tsunami

DM_TEST("disasters: a tsunami front expands over sea only and floods a low coast") {
	Fakes F;
	stub::Guard g(true);
	if (allStubbed()) installFakes(F);
	// sea south of 0 deg, land north: a low strip (0.3) then high ground (3.0)
	auto w = flatWorld(5, 0.0, -2.0f);
	for (int c = 0; c < w->n; c++) {
		const double la = w->geo->lat[size_t(c)] * 180 / kPi;
		if (la >= 0) w->elev[size_t(c)] = la < 6 ? 0.3f : 3.0f;
	}
	const int src = cellAt(*w, -12, 20);
	const int lowTownCell = cellAt(*w, 2, 20), highTownCell = cellAt(*w, 12, 20);
	if (!ok(!state::isSea(*w, lowTownCell) && w->elev[size_t(lowTownCell)] < 1, "low coast")) return;
	const int low = newTown(*w, lowTownCell, 300, 1), high = newTown(*w, highTownCell, 300, 1);
	const int unitLow = newUnitAt(*w, lowTownCell);
	Disaster* t = powers::start(*w, DisasterType::Tsunami, src);
	if (!ok(t != nullptr)) return;
	const int id = t->id;
	double prevFront = -1;
	int ticks = 0;
	std::vector<Event> evs = capture(*w, [&] {
		while (state::findDisaster(*w, id) && ticks < 200) {
			const Disaster* d = state::findDisaster(*w, id);
			ok(d->front >= prevFront, "front never shrinks");
			prevFront = d->front;
			powers::update(*w, 0.125);
			powers::tick(*w);
			ticks++;
		}
	});
	ok(state::findDisaster(*w, id) == nullptr, "the wave ends");
	ok(ticks >= 15 && ticks <= 30, "front reaches 0.6 rad in ~20 ticks: " + std::to_string(ticks));
	ok(w->wet[size_t(lowTownCell)] >= 1.0f, "the low coast is soaked");
	eq(w->wet[size_t(highTownCell)], 0.0f, "high ground stays dry");
	ok(countEv(evs, Ev::TsunamiHit) >= 1, "tsunami:hit");
	ok(countEv(evs, Ev::TsunamiHit) <= ticks / 4 + 1, "tsunami:hit rate-limited: " + std::to_string(countEv(evs, Ev::TsunamiHit)));
	for (const Event& e : evs)
		if (const EvTsunamiHit* h = e.as<EvTsunamiHit>()) ok(!state::isSea(*w, h->cell) && h->height > 0, "hit on land with a height");
	// the far side of the land is untouched (the wave does not cross land)
	eq(w->wet[size_t(cellAt(*w, 40, 20))], 0.0f, "inland far away dry");
	if (allStubbed()) {
		ok(w->settlements[size_t(low)].dead || w->settlements[size_t(low)].pop < 300, "the low town lost people");
		near(w->settlements[size_t(high)].pop, 300, 1e-9, "the high town did not");
		ok(state::findUnit(*w, unitLow) == nullptr, "unit on flooded ground drowned");
		bool waveCause = false;
		for (const auto& k : F.killed) if (k.second == "wave") waveCause = true;
		ok(waveCause, "unit cause wave");
	}
	noNaN(*w);
}

DM_TEST("disasters: the tsunami wave order covers sea cells only, inside its reach") {
	auto w = makeWorld({ .L = 5, .seed = 4 });
	int src = -1;
	for (int c = 0; c < w->n; c++) if (state::isSea(*w, c) && w->coast[size_t(c)] == 0) {
		std::vector<int32_t> r;
		w->geo->ring(c, 3, r);
		bool hasLand = false;
		for (int32_t m : r) if (!state::isSea(*w, m)) hasLand = true;
		if (hasLand) { src = c; break; }
	}
	if (!ok(src >= 0)) return;
	Disaster* t = powers::start(*w, DisasterType::Tsunami, src);
	const int id = t->id;
	// watch the front: every cell the front has passed (by over-sea distance) is sea and within reach
	std::vector<float> wet0 = w->wet;
	int ticks = 0;
	while (state::findDisaster(*w, id) && ticks < 100) {
		const Disaster* d = state::findDisaster(*w, id);
		ok(d->front <= d->ang + 1e-12, "front within reach");
		powers::tick(*w);
		ticks++;
	}
	// only land cells get wet, and only near a coast (a few hops)
	int wetLand = 0, farWet = 0;
	for (int c = 0; c < w->n; c++) {
		if (w->wet[size_t(c)] > wet0[size_t(c)]) {
			ok(!state::isSea(*w, c), "wet only on land");
			wetLand++;
			if (w->coast[size_t(c)] > 6) farWet++;
		}
	}
	eq(farWet, 0, "no flooding deep inland");
	std::printf("    tsunami on helper world: %d land cells flooded in %d ticks\n", wetLand, ticks);
	noNaN(*w);
}

// ------------------------------------------------------------------ storms and droughts

DM_TEST("disasters: a storm drifts from sea to land one hop at most per tick, wets the land, ends") {
	Fakes F;
	stub::Guard g(true);
	if (allStubbed()) installFakes(F);
	auto w = flatWorld(5, 0.0, -2.0f);   // sea in the south
	const int c0 = cellAt(*w, -8, 100);
	powers::StartOpts so;
	so.dur = 6;
	Disaster* s = powers::start(*w, DisasterType::Storm, c0, so);
	if (!ok(s != nullptr)) return;
	const int id = s->id;
	int prev = c0, ticks = 0, landTicks = 0;
	std::vector<int> track;
	std::vector<Event> evs = capture(*w, [&] {
		while (state::findDisaster(*w, id) && ticks < 100) {
			powers::tick(*w);
			ticks++;
			const Disaster* d = state::findDisaster(*w, id);
			if (!d) break;
			ok(d->cell == prev || w->geo->angle(prev, d->cell) <= 1.6 * w->geo->step, "one hop at most");
			prev = d->cell;
			track.push_back(d->cell);
			if (!state::isSea(*w, d->cell)) landTicks++;
		}
	});
	ok(state::findDisaster(*w, id) == nullptr, "storm ended");
	ok(landTicks > 0, "it came ashore");
	int wetCells = 0;
	for (int c = 0; c < w->n; c++) if (w->wet[size_t(c)] > 0.2f) { wetCells++; ok(!state::isSea(*w, c), "wet on land only"); }
	ok(wetCells > 5, "rain on land: " + std::to_string(wetCells));
	int starts = 0, ends = 0;
	for (const Event& e : evs) if (const EvStorm* st = e.as<EvStorm>()) (st->start ? starts : ends)++;
	eq(ends, 1, "storm { start: false }");
	ok(w->need.climate, "storm rain sets need.climate");
	std::printf("    storm: %d ticks, %d over land, %d wet cells\n", ticks, landTicks, wetCells);
	noNaN(*w);
}

DM_TEST("disasters: droughts start and end with events and need.climate") {
	auto w = flatWorld(4);
	w->need.climate = false;
	powers::StartOpts o;
	o.dur = 6;
	std::vector<Event> evs = capture(*w, [&] { powers::start(*w, DisasterType::Drought, 10, o); });
	ok(w->need.climate, "need.climate at start");
	eq(countEv(evs, Ev::Drought), 1);
	w->need.climate = false;
	int ticks = 0;
	evs = capture(*w, [&] { while (!w->disasters.empty() && ticks < 100) { powers::tick(*w); ticks++; } });
	eq(ticks, 24, "6 years");
	ok(w->need.climate, "need.climate at end");
	bool ended = false;
	for (const Event& e : evs) if (const EvDrought* d = e.as<EvDrought>()) ended = !d->start && d->cell == 10;
	ok(ended, "drought { start: false }");
}

// ------------------------------------------------------------------ natural events

namespace {
struct NatCount { int lightning = 0, storms = 0, droughts = 0, quakes = 0, plagues = 0, eruptions = 0; std::vector<int> cells; bool allNatural = true; };
NatCount runNatural(World& w, int years, Fakes* F) {
	NatCount k;
	const size_t inf0 = F ? F->infected.size() : 0;
	auto tok = w.bus.onAll([&](const Event& e) {
		if (const EvLightning* p = e.as<EvLightning>()) { k.lightning++; k.cells.push_back(p->cell); k.allNatural &= p->natural; }
		if (const EvStorm* p = e.as<EvStorm>()) if (p->start) { k.storms++; k.cells.push_back(p->cell); }
		if (const EvDrought* p = e.as<EvDrought>()) if (p->start) { k.droughts++; k.cells.push_back(p->cell); }
		if (const EvQuake* p = e.as<EvQuake>()) { k.quakes++; k.cells.push_back(p->cell); k.allNatural &= p->natural; }
		if (const EvVolcanoStart* p = e.as<EvVolcanoStart>()) { k.eruptions++; k.cells.push_back(p->cell); k.allNatural &= p->natural; }
	});
	for (int y = 0; y < years; y++) {
		w.year += 1;
		powers::natural(w);
		for (int i = 0; i < 4; i++) { powers::update(w, 0.125); powers::tick(w); }
	}
	w.bus.off(tok);
	if (F) {
		k.plagues = int(F->infected.size() - inf0);
		for (size_t i = inf0; i < F->infected.size(); i++) k.cells.push_back(w.settlements[size_t(F->infected[i])].cell);
	}
	return k;
}
} // namespace

DM_TEST("disasters: natural: nothing in 2,000 years at calamity 0") {
	Fakes F;
	stub::Guard g(true);
	if (allStubbed()) installFakes(F);
	auto w = makeWorld({ .L = 4, .seed = 3 });
	w->settings.calamity = 0;
	w->year = 500;
	w->stats.pop = 1e6;
	const uint32_t rng0 = w->rng.s;
	NatCount k = runNatural(*w, 2000, &F);
	eq(k.lightning + k.storms + k.droughts + k.quakes + k.eruptions + k.plagues, 0, "no natural events");
	eq(w->rng.s, rng0, "no draws either");
	ok(w->disasters.empty());
}

DM_TEST("disasters: natural rates at calamity 1 within +-40 % of the table over 5,000 years") {
	Fakes F;
	stub::Guard g(true);
	if (!allStubbed()) return;   // counting plague needs the infect hook
	installFakes(F);
	auto w = makeWorld({ .L = 4, .seed = 5 });
	w->year = 1000;
	w->stats.pop = 1e6;
	// five cities (tier 3) on land so plague has a target and protection is off (>= 4 live, pop >= 200)
	int made = 0;
	for (int c = 0; c < w->n && made < 5; c += 97) if (!state::isSea(*w, c)) { newTown(*w, c, 5000, 3); made++; }
	if (!ok(made == 5)) return;
	const double t0 = nowMs();
	NatCount k = runNatural(*w, 5000, &F);
	const double ms = nowMs() - t0;
	std::printf("    5,000 yr: lightning %d (300), storms %d (150), droughts %d (100), quakes %d (50), plague %d (50), eruptions %d (20); %.0f ms\n",
	            k.lightning, k.storms, k.droughts, k.quakes, k.plagues, k.eruptions, ms);
	auto within = [&](int got, double want, const char* what) { ok(got >= 0.6 * want && got <= 1.4 * want, std::string(what) + ": " + std::to_string(got) + " vs " + std::to_string(want)); };
	within(k.lightning, 300, "lightning");
	within(k.storms, 150, "storms");
	within(k.droughts, 100, "droughts");
	within(k.quakes, 50, "quakes");
	within(k.plagues, 50, "plague");
	within(k.eruptions, 20, "eruptions");
	ok(k.allNatural, "natural flags");
	ok(F.witnessCalls > 0, "natural events witnessed");
	bool quarter = true;
	for (const auto& o : F.witnesses) {
		const powers::PowerDef* d = powers::byId(o.kind);
		if (!d) { quarter = false; continue; }
		if (std::fabs(o.faith - d->faith / 4) > 1e-12 || o.good != 0) quarter = false;
	}
	ok(quarter, "witness with a quarter of the faith and good 0");
	noNaN(*w);
}

DM_TEST("disasters: natural protection: 0.3 rad while young, 0.15 rad while fewer than 4 settlements") {
	Fakes F;
	stub::Guard g(true);
	if (allStubbed()) installFakes(F);
	for (int phase = 0; phase < 2; phase++) {
		auto w = makeWorld({ .L = 4, .seed = 6 });
		w->settings.calamity = 5;
		const int home = bestHab(*w);
		const int sid = newTown(*w, home, 150, 3);
		w->year = phase == 0 ? 50 : 300;
		w->stats.pop = phase == 0 ? 150 : 5000;
		const double r = phase == 0 ? 0.3 : 0.15;
		NatCount k = runNatural(*w, 1500, allStubbed() ? &F : nullptr);
		int inside = 0;
		for (int c : k.cells) if (w->geo->angle(c, home) <= r) inside++;
		eq(inside, 0, std::string(phase == 0 ? "young world" : "few towns") + ": events within the protected radius");
		ok(k.cells.size() > 50, "events elsewhere: " + std::to_string(k.cells.size()));
		if (phase == 0) eq(k.plagues, 0, "no plague on the protected city");
		(void)sid;
		// still young: a natural event near the town once protection is lifted
		if (phase == 1) {
			w->stats.pop = 5000;
			for (int i = 0; i < 3; i++) newTown(*w, findLand(*w, [&](int c) { return w->geo->angle(c, home) > 0.5 && w->owner[size_t(c)] < 0; }), 300, 1);
			NatCount k2 = runNatural(*w, 1500, allStubbed() ? &F : nullptr);
			int close = 0;
			for (int c : k2.cells) if (w->geo->angle(c, home) <= 0.15) close++;
			ok(close > 0, "with 4 towns and 200 people nothing is protected: " + std::to_string(close));
		}
	}
}

// ------------------------------------------------------------------ every event, budget, determinism

DM_TEST("disasters: every documented event fires") {
	Fakes F;
	stub::Guard g(true);
	if (allStubbed()) installFakes(F);
	auto w = makeWorld({ .L = 5, .seed = 8 });
	w->year = 500;
	w->stats.pop = 1e6;
	w->settings.calamity = 3;
	std::set<Ev> seen;
	auto tok = w->bus.onAll([&](const Event& e) { seen.insert(e.type); });
	const int land = bestHab(*w);
	int sea = -1;
	for (int c = 0; c < w->n; c++) if (state::isSea(*w, c) && w->elev[size_t(c)] > -1.5f) { sea = c; break; }
	powers::start(*w, DisasterType::Meteor, land);
	powers::start(*w, DisasterType::Meteor, sea);
	powers::start(*w, DisasterType::Volcano, land);
	powers::start(*w, DisasterType::Storm, sea);
	powers::start(*w, DisasterType::Drought, land);
	w->phase = Phase::Water;
	powers::start(*w, DisasterType::Comet, sea);
	for (int i = 0; i < 16; i++) powers::update(*w, 0.125);
	w->phase = Phase::History;
	for (int y = 0; y < 300; y++) {
		powers::natural(*w);
		for (int i = 0; i < 4; i++) { powers::update(*w, 0.125); powers::tick(*w); }
		w->year += 1;
	}
	w->bus.off(tok);
	const Ev want[] = { Ev::MeteorLaunched, Ev::MeteorImpact, Ev::TsunamiStart, Ev::TsunamiHit, Ev::VolcanoStart, Ev::VolcanoEnd,
	                    Ev::Quake, Ev::Lightning, Ev::Storm, Ev::Drought };
	for (Ev e : want) ok(seen.count(e) == 1, std::string("event fired: ") + eventName(e));
	noNaN(*w);
}

DM_TEST("disasters: tick <= 0.5 ms with 3 active disasters at L 6") {
	Fakes F;
	stub::Guard g(false);
	if (allStubbed()) installFakes(F);
	auto w = makeWorld({ .L = 6, .seed = 2 });
	const int land = bestHab(*w);
	int sea = -1;
	for (int c = 0; c < w->n; c++) if (state::isSea(*w, c) && w->coast[size_t(c)] == 0 && w->elev[size_t(c)] > -1.0f) { sea = c; break; }
	powers::StartOpts o;
	o.dur = 40;
	powers::start(*w, DisasterType::Volcano, land, o);
	powers::start(*w, DisasterType::Tsunami, sea);
	powers::start(*w, DisasterType::Storm, sea, o);
	double total = 0, worst = 0;
	int n = 0;
	for (int i = 0; i < 24; i++) {
		if (w->disasters.size() < 3) break;
		const double t0 = nowMs();
		powers::tick(*w);
		const double dtm = nowMs() - t0;
		total += dtm;
		worst = std::max(worst, dtm);
		n++;
	}
	const double mean = n ? total / n : 0;
	std::printf("    tick with 3 disasters at L6: mean %.3f ms, max %.3f ms over %d ticks\n", mean, worst, n);
	ok(n >= 12, "three were active for a while");
	ok(mean <= 0.5 * 5, "mean tick within 5x of 0.5 ms (harness rule); measured " + std::to_string(mean));
}

DM_TEST("disasters: deterministic: same calls, same world") {
	auto run = [] {
		Fakes F;
		stub::Guard g(false);
		if (allStubbed()) installFakes(F);
		auto w = makeWorld({ .L = 4, .seed = 9 });
		w->year = 400;
		w->stats.pop = 1e5;
		w->settings.calamity = 4;
		powers::start(*w, DisasterType::Meteor, bestHab(*w));
		for (int y = 0; y < 400; y++) {
			powers::natural(*w);
			for (int i = 0; i < 4; i++) { powers::update(*w, 0.125); powers::tick(*w); }
			w->year += 1;
		}
		return hash(*w);
	};
	const uint32_t a = run(), b = run();
	eq(a, b, "hash");
}

DM_TEST("disasters: robust on empty worlds, other phases and bad state") {
	stub::Guard g(false);
	auto w = state::create({ .seed = 1, .L = 3, .geo = sharedGeo(3) });   // all ground at 0, sea -9: no sea
	watch(*w);
	w->phase = Phase::History;
	w->year = 1000;
	w->stats.pop = 1e6;
	w->settings.calamity = 10;
	for (int y = 0; y < 200; y++) { powers::natural(*w); powers::update(*w, 0.125); powers::tick(*w); }
	powers::start(*w, DisasterType::Tsunami, 5);   // a tsunami on a dry world simply ends
	powers::start(*w, DisasterType::Storm, 5);     // a storm with no sea anywhere
	for (int i = 0; i < 100; i++) powers::tick(*w);
	ok(w->disasters.empty(), "everything ended");
	noNaN(*w, "dry world");
	// a disaster with a broken cell and a NaN eta
	Disaster& d = state::newDisaster(*w, DisasterType::Meteor, -5);
	d.eta = std::nan("");
	powers::update(*w, 0.125);
	ok(w->disasters.empty(), "broken meteor removed without harm");
	Disaster& v = state::newDisaster(*w, DisasterType::Volcano, 3);
	v.dur = 1;
	v.cells = { 3, 99999, -2 };
	for (int i = 0; i < 10; i++) powers::tick(*w);
	ok(w->disasters.empty(), "broken volcano ended");
	noNaN(*w, "after broken disasters");
	// molten phase: natural is silent
	w->phase = Phase::Molten;
	const uint32_t s0 = w->rng.s;
	powers::natural(*w);
	eq(w->rng.s, s0, "no natural events outside history");
	// a world without geo does nothing
	World* raw = nullptr;
	(void)raw;
}

// ------------------------------------------------------------------ maps (look at them)

DM_TEST("disasters: maps of a meteor and a volcano before and after (L 6)") {
	Fakes F;
	stub::Guard g(false);
	if (!allStubbed()) return;
	installFakes(F);
	auto w = makeWorld({ .L = 6, .seed = 3 });
	// meteor on land
	int gz = -1;
	for (int c = w->n / 3; c < w->n; c++) if (!state::isSea(*w, c) && w->coast[size_t(c)] >= 6 && w->veg[size_t(c)] > 0.3f) { gz = c; break; }
	if (!ok(gz >= 0)) return;
	auto ecol = [&](int m) { return elevColor(*w, m); };
	zoomMap(*w, gz, 0.2, shot("ue-disasters-meteor-before.png"), ecol);
	powers::start(*w, DisasterType::Meteor, gz);
	for (int i = 0; i < 16; i++) powers::update(*w, 0.125);
	zoomMap(*w, gz, 0.2, shot("ue-disasters-meteor-after.png"), ecol);
	zoomMap(*w, gz, 0.2, shot("ue-disasters-meteor-fires.png"), [&](int m) {
		if (w->fire[size_t(m)] > 0) return mi::Rgb{ 255, 90, 20 };
		const mi::Rgb e = elevColor(*w, m);
		return mi::Rgb{ e.r * 0.5, e.g * 0.5, e.b * 0.5 };
	});
	// volcano in shallow sea next to a coast
	int vent = -1;
	for (int c = 0; c < w->n; c++)
		if (state::isSea(*w, c) && w->elev[size_t(c)] > -0.7f && w->elev[size_t(c)] < -0.3f && w->geo->angle(c, gz) > 0.5) {
			std::vector<int32_t> r;
			w->geo->ring(c, 4, r);
			int landN = 0;
			for (int32_t m : r) if (!state::isSea(*w, m)) landN++;
			if (landN > 3) { vent = c; break; }
		}
	if (!ok(vent >= 0, "a shallow-sea vent")) return;
	const std::vector<float> elev0 = w->elev;
	zoomMap(*w, vent, 0.15, shot("ue-disasters-volcano-before.png"), ecol);
	powers::start(*w, DisasterType::Volcano, vent);
	for (int i = 0; i < 30; i++) powers::tick(*w);
	zoomMap(*w, vent, 0.15, shot("ue-disasters-volcano-erupting.png"), [&](int m) {
		const double L = w->lava[size_t(m)];
		if (L > 0.01) return mi::Rgb{ 255, 60 + 160 * sat(L / 1.5), 20 };
		return elevColor(*w, m);
	});
	while (!w->disasters.empty()) powers::tick(*w);
	zoomMap(*w, vent, 0.15, shot("ue-disasters-volcano-after.png"), ecol);
	int newLand = 0;
	for (int c = 0; c < w->n; c++) if (elev0[size_t(c)] < w->seaLevel && w->elev[size_t(c)] >= w->seaLevel) newLand++;
	std::printf("    maps: meteor at %d (fires %zu), volcano at %d made %d new land cells\n", gz, F.ignited.size(), vent, newLand);
	ok(newLand > 0, "the volcano made land");
	// a tsunami flood map
	int src = -1;
	for (int c = 0; c < w->n; c++) if (state::isSea(*w, c) && w->elev[size_t(c)] < -2 && w->geo->angle(c, gz) < 0.6) { src = c; break; }
	if (src >= 0) {
		const std::vector<float> wet0 = w->wet;
		powers::StartOpts o;
		o.power = 3;
		o.ang = 0.8;
		powers::start(*w, DisasterType::Tsunami, src, o);
		while (!w->disasters.empty()) powers::tick(*w);
		std::vector<float> flood(size_t(w->n), 0.0f);
		int nf = 0;
		for (int c = 0; c < w->n; c++) if (w->wet[size_t(c)] > wet0[size_t(c)]) { flood[size_t(c)] = 1; nf++; }
		zoomMap(*w, src, 0.6, shot("ue-disasters-tsunami.png"), [&](int m) {
			if (flood[size_t(m)] > 0) return mi::Rgb{ 255, 40, 200 };
			if (m == src) return mi::Rgb{ 255, 255, 255 };
			return elevColor(*w, m);
		});
		std::printf("    tsunami map: %d land cells flooded\n", nf);
	}
	noNaN(*w);
}

DM_TEST("disasters: maps with the real Terrain (run.ps1 all only)") {
	if (stub::stubbed("terrain")) return;
	auto w = makeWorld({ .L = 6, .seed = 3 });
	auto ecol = [&](int m) { return elevColor(*w, m); };
	int gz = -1;
	for (int c = w->n / 3; c < w->n; c++) if (!state::isSea(*w, c) && w->coast[size_t(c)] >= 6 && w->veg[size_t(c)] > 0.3f) { gz = c; break; }
	if (!ok(gz >= 0)) return;
	const float e0 = w->elev[size_t(gz)];
	powers::start(*w, DisasterType::Meteor, gz);
	for (int i = 0; i < 16; i++) powers::update(*w, 0.125);
	zoomMap(*w, gz, 0.2, shot("ue-disasters-real-meteor.png"), ecol);
	ok(w->elev[size_t(gz)] < e0 - 1.0f, "real crater dug");
	int vent = -1;
	for (int c = 0; c < w->n; c++)
		if (state::isSea(*w, c) && w->elev[size_t(c)] > -0.7f && w->elev[size_t(c)] < -0.3f && w->geo->angle(c, gz) > 0.5) { vent = c; break; }
	if (!ok(vent >= 0)) return;
	powers::start(*w, DisasterType::Volcano, vent);
	for (int i = 0; i < 30; i++) powers::tick(*w);
	zoomMap(*w, vent, 0.15, shot("ue-disasters-real-volcano-erupting.png"), [&](int m) {
		const double L = w->lava[size_t(m)];
		if (L > 0.01) return mi::Rgb{ 255, 60 + 160 * sat(L / 1.5), 20 };
		return elevColor(*w, m);
	});
	while (!w->disasters.empty()) powers::tick(*w);
	zoomMap(*w, vent, 0.15, shot("ue-disasters-real-volcano-after.png"), ecol);
	ok(!state::isSea(*w, vent), "real cone + lava made an island");
	std::printf("    real terrain: crater %.2f -> %.2f, vent now %.2f\n", e0, w->elev[size_t(gz)], w->elev[size_t(vent)]);
	noNaN(*w);
}
