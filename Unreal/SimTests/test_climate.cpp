// DEMIURGE — SimTests/test_climate.cpp : dm::climate (SPEC-SIM 3.3, BUILD.md A3). Port of tests/climate.test.js.
//   run.ps1 climate                         (DM_VERBOSE=1 lists every test; CLIMATE_REPORT=1 prints the measured numbers)
//   CLIMATE_MAPS=1 run.ps1 climate maps     also renders shots/ue-climate-*.png
// Worlds: the helper's synthetic planets (terrain only: basics false) and hand-built planets (one square continent,
// with and without a north-south mountain wall; an all-land and an all-sea planet). No other module is needed:
// life::cover is hooked wherever climate::yearly runs (in `run.ps1 all` the real Life answers instead and the
// tests that need an exact vegetation number skip those checks).
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <utility>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Climate.h"
#include "Life.h"
#include "MapImg.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

constexpr double kD = 180.0 / dm::kPi;
constexpr double kInf = std::numeric_limits<double>::infinity();
using CoverT = decltype(dm::life::cover(std::declval<const dm::World&>()));

bool reportOn() { static const bool on = std::getenv("CLIMATE_REPORT") != nullptr; return on; }
void say(const std::string& s) { if (reportOn()) std::printf("    . %s\n", s.c_str()); }
std::string f2(double v, int prec = 3) { char b[64]; std::snprintf(b, sizeof b, "%.*f", prec, v); return b; }

std::unique_ptr<World> helperWorld(int L = 5, uint32_t seed = 1, double sea = 0, Phase phase = Phase::History) {
	return makeWorld({ .L = L, .seed = seed, .land = 0.4, .basics = false, .sea = sea, .phase = phase });
}
// One continent between +-55 latitude and +-60 longitude, 0.3 high, in a -4 deep ocean; `wall` adds a north-south ridge
// of that height along longitude 0 (9 degree half-width).
std::unique_ptr<World> squareWorld(int L, double wall) {
	auto w = state::create({ .seed = 5, .L = L, .geo = sharedGeo(L) });
	watch(*w);
	const Geo& g = *w->geo;
	for (int c = 0; c < w->n; c++) {
		const double la = g.lat[size_t(c)] * kD, lo = g.lon[size_t(c)] * kD;
		double e = -4;
		if (std::fabs(la) < 55 && std::fabs(lo) < 60) {
			e = 0.3;
			if (wall != 0 && std::fabs(lo) < 9) e += wall * (1 - (lo / 9) * (lo / 9));
		}
		w->elev[size_t(c)] = float(e);
	}
	w->seaLevel = 0; w->heat = 0; w->phase = Phase::History;
	return w;
}
std::unique_ptr<World> uniformWorld(int L, double elev, double sea, Phase phase = Phase::History, uint32_t seed = 9) {
	auto w = state::create({ .seed = seed, .L = L, .geo = sharedGeo(L) });
	watch(*w);
	std::fill(w->elev.begin(), w->elev.end(), float(elev));
	w->seaLevel = sea; w->heat = 0; w->phase = phase;
	return w;
}
double lat(const World& w, int c) { return w.geo->lat[size_t(c)] * kD; }
double alat(const World& w, int c) { return std::fabs(lat(w, c)); }
double lon(const World& w, int c) { return w.geo->lon[size_t(c)] * kD; }
bool isLand(const World& w, int c) { return double(w.elev[size_t(c)]) >= w.seaLevel; }
template <class P>
double mean(const World& w, const std::vector<float>& arr, P pred) {
	double s = 0; int k = 0;
	for (int c = 0; c < w.n; c++) if (pred(c)) { s += arr[size_t(c)]; k++; }
	return k ? s / k : std::nan("");
}
template <class P>
int count(const World& w, P pred) { int k = 0; for (int c = 0; c < w.n; c++) if (pred(c)) k++; return k; }
std::vector<int> coastDist(const World& w) {            // neighbour hops to the nearest sea cell (brute-force BFS)
	const Geo& g = *w.geo;
	std::vector<int> d(size_t(w.n), 1000000000), q;
	for (int c = 0; c < w.n; c++) if (!isLand(w, c)) { d[size_t(c)] = 0; q.push_back(c); }
	for (size_t h = 0; h < q.size(); h++) {
		const int c = q[h];
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
			const int b = g.nbr[size_t(6 * c + k)];
			if (d[size_t(b)] > d[size_t(c)] + 1) { d[size_t(b)] = d[size_t(c)] + 1; q.push_back(b); }
		}
	}
	return d;
}
std::string finite(const World& w) {
	std::string bad = checkFinite(w);
	if (!bad.empty()) return bad;
	if (w.vars.seaBase && !std::isfinite(*w.vars.seaBase)) return "vars.seaBase";
	if (w.vars.iceRef && !std::isfinite(*w.vars.iceRef)) return "vars.iceRef";
	for (const auto& [k, v] : w.vars.num) if (!std::isfinite(v)) return "vars.num." + k;
	return "";
}
const std::vector<int32_t>& upOf(const World& w) { return w.scratch<ClimateScratch>().up; }
// life::cover answers this vegetation (only while Life is a stub; returns false in `run.ps1 all`)
bool hookVeg(double veg) {
	if (!stub::stubbed("life")) return false;
	stub::hooks().life_cover = [veg](const World&) { CoverT cv{}; cv.land = 0.4; cv.veg = veg; cv.forest = 0.3; return cv; };
	return true;
}
std::string fmtV(double v) { std::ostringstream os; os << v; return os.str(); }
std::vector<std::string> milestones(const std::vector<Event>& ev) {
	std::vector<std::string> out;
	for (const Event& e : ev)
		if (const auto* m = e.as<EvClimateMilestone>()) out.push_back(std::string(toStr(m->kind)) + " " + fmtV(m->value));
	return out;
}
std::string join(const std::vector<std::string>& v) { std::string s; for (size_t i = 0; i < v.size(); i++) s += (i ? "," : "") + v[i]; return s; }
int countOf(const std::vector<std::string>& v, const std::string& x) { return int(std::count(v.begin(), v.end(), x)); }
void clearFlags(World& w) { w.need = { false, false, false, false }; w.dirty = { false, false, false, false, false, false }; }
// what the scheduler does around climate::yearly: an update when asked (at most every 2 years), ice every 5 years
template <class F>
void runYears(World& w, int years, F each) {
	for (int y = 1; y <= years; y++) {
		each(y);
		climate::yearly(w);
		if (w.need.climate && y % 2 == 0) climate::update(w);
		if (y % 5 == 0) climate::stepIce(w);
	}
}
void runYears(World& w, int years) { runYears(w, years, [](int) {}); }
double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
	double m = 0; for (size_t i = 0; i < a.size(); i++) m = std::max(m, double(std::fabs(a[i] - b[i]))); return m;
}

} // namespace

// ------------------------------------------------------------------ API / tuning
DM_TEST("climate: tuning holds the spec's starting values and a fixed RAIN_REF") {
	const ClimateTune T;
	ok(T.rainRef > 0, "RAIN_REF");
	eq(T.tEquator, 29.0); eq(T.tPole, -24.0); eq(T.lapse, 5.5); eq(T.albedo, 4.0); eq(T.dustCool, 9.0);
	eq(T.iceRate, 0.06); eq(T.iceRateGenesis, 0.30); eq(T.iceRefMin, 50.0); eq(T.seaRate, 0.005);
	eq(T.seaLo, -0.3); eq(T.seaHi, 0.6); eq(T.co2Max, 300.0); eq(T.droughtDry, 0.25);
}

// ------------------------------------------------------------------ temperature
DM_TEST("climate: equator 40-60 C warmer than the poles, smooth in latitude, isotherms not circles") {
	// an all-land planet below the datum: no lapse, no sea, no ice: the bare latitude profile plus its wobble
	for (uint32_t seed : { 9u, 2u }) {
		auto w = uniformWorld(5, -1, -9, Phase::Water, seed);
		climate::update(*w);
		const double eqT = mean(*w, w->temp, [&](int c) { return alat(*w, c) < 6; });
		const double pole = mean(*w, w->temp, [&](int c) { return alat(*w, c) > 82; });
		say("seed " + std::to_string(seed) + " equator " + f2(eqT, 1) + " poles " + f2(pole, 1) + " difference " + f2(eqT - pole, 1));
		ok(eqT - pole >= 40 && eqT - pole <= 60, "equator - pole = " + f2(eqT - pole));
		near(eqT, 29, 5, "about +29 at the equator"); near(pole, -24, 7, "about -24 at the poles");
		double prev = 1e9;
		for (int a = 0; a < 90; a += 15) {
			const double m = mean(*w, w->temp, [&](int c) { return alat(*w, c) >= a && alat(*w, c) < a + 15; });
			ok(m < prev, "band " + std::to_string(a) + " is colder than the one before"); prev = m;
		}
		double lo = 1e9, hi = -1e9;
		for (int c = 0; c < w->n; c++) if (std::fabs(lat(*w, c) - 40) < 1.5) { lo = std::min(lo, double(w->temp[size_t(c)])); hi = std::max(hi, double(w->temp[size_t(c)])); }
		ok(hi - lo > 0.5 && hi - lo < 14, "isotherms are not circles, spread at 40N = " + f2(hi - lo));
	}
	// helper worlds: the cells below the datum of a dry world (the ocean floors reach every latitude)
	for (uint32_t seed : { 1u, 2u, 3u }) {
		auto w = helperWorld(5, seed, -9, Phase::Water);
		w->vars.seaBase.reset();
		climate::update(*w);
		const double eqT = mean(*w, w->temp, [&](int c) { return w->elev[size_t(c)] <= 0 && alat(*w, c) < 8; });
		const double pole = mean(*w, w->temp, [&](int c) { return w->elev[size_t(c)] <= 0 && alat(*w, c) > 78; });
		ok(eqT - pole >= 40 && eqT - pole <= 60, "helper seed " + std::to_string(seed) + ": equator - pole = " + f2(eqT - pole));
	}
	// the hand-built planet, calibrated (sea-level land and open sea)
	auto sq = squareWorld(5, 5);
	climate::calibrate(*sq);
	const double eqT = mean(*sq, sq->temp, [&](int c) { return alat(*sq, c) < 6; });
	const double pole = mean(*sq, sq->temp, [&](int c) { return alat(*sq, c) > 82; });
	say("square continent: equator " + f2(eqT, 1) + " poles " + f2(pole, 1) + " (sea floor -2)");
	// the polar sea is stored at -2 (that is the spec's clamp), so the measured difference is smaller there
	ok(eqT - pole >= 25 && eqT > 25, "square continent: equator " + f2(eqT) + " vs poles " + f2(pole));
}

DM_TEST("climate: lapse 5.5 C per unit, measured from datum, never from the live sea level") {
	auto w = helperWorld(5, 2);
	climate::calibrate(*w);
	const int c = findLand(*w, [&](int k) { return alat(*w, k) < 30 && w->elev[size_t(k)] > 0.3 && w->elev[size_t(k)] < 1.5; });
	if (!ok(c >= 0)) return;
	const double t0 = w->temp[size_t(c)];
	w->elev[size_t(c)] += 2; climate::update(*w);
	near(w->temp[size_t(c)] - t0, -11, 1e-3, "two units higher = 11 C colder");
	w->elev[size_t(c)] -= 2; climate::update(*w);
	near(w->temp[size_t(c)], t0, 1e-3);
	// land below the datum gets no bonus
	auto low = uniformWorld(3, -3, -9, Phase::Water), lower = uniformWorld(3, -6, -9, Phase::Water);
	climate::update(*low); climate::update(*lower);
	for (int k = 0; k < low->n; k += 37) near(low->temp[size_t(k)], lower->temp[size_t(k)], 1e-4, "no lapse below the datum");
	// a rising sea does not warm the land: the datum stays at vars.seaBase
	const std::vector<float> before = w->temp;
	w->seaLevel = 0.4; climate::update(*w);
	eq(climate::datum(*w), 0.0, "datum stays at seaBase");
	int checked = 0;
	for (int k = 0; k < w->n; k++)
		if (w->elev[size_t(k)] >= 0.4) { near(w->temp[size_t(k)], before[size_t(k)], 1e-4, "land temp unchanged by the sea"); checked++; }
	ok(checked > 500, "checked " + std::to_string(checked));
}

DM_TEST("climate: datum = max(0, seaBase) once calibrated, else max(0, seaLevel)") {
	auto w = state::create({ .seed = 1, .L = 2 });
	eq(w->seaLevel, -9.0); eq(climate::datum(*w), 0.0, "genesis world at -9");
	w->seaLevel = 1.2; near(climate::datum(*w), 1.2, 1e-9, "overfilled genesis world");
	w->vars.seaBase = 0.5; w->seaLevel = 0.9; near(climate::datum(*w), 0.5, 1e-9, "calibrated: the base, not the live sea");
	w->vars.seaBase = -3; eq(climate::datum(*w), 0.0, "never below 0");
	for (double bad : { std::nan(""), kInf, -kInf }) {
		w->vars.seaBase = bad; w->seaLevel = 0.25;
		near(climate::datum(*w), 0.25, 1e-9, "a non-finite seaBase falls back to the sea");
	}
	w->vars.seaBase.reset(); near(climate::datum(*w), 0.25, 1e-9, "no seaBase");
	w->seaLevel = std::nan(""); eq(climate::datum(*w), 0.0, "NaN sea level");
}

DM_TEST("climate: genesis: a world whose sea is still at -9 is not frozen (within 3 C of the finished world)") {
	for (uint32_t seed : { 1u, 2u, 3u }) {
		auto full = helperWorld(5, seed);                                // the finished world: sea 0, calibrated
		climate::calibrate(*full);
		auto landSet = [&](int c) { return full->elev[size_t(c)] >= 0; };
		const double tFull = mean(*full, full->temp, landSet);
		for (double sea : { -9.0, -5.0 }) {
			auto w = helperWorld(5, seed, sea, Phase::Water);
			w->vars.seaBase.reset();
			climate::update(*w);
			for (int i = 0; i < 40; i++) climate::stepIce(*w);           // 100 real seconds of genesis ice
			climate::update(*w);
			const double t = mean(*w, w->temp, landSet);                  // the same cells in both worlds
			const double iced = double(count(*w, [&](int c) { return landSet(c) && w->ice[size_t(c)] > 0.5; })) / count(*w, landSet);
			say("seed " + std::to_string(seed) + " sea " + f2(sea, 0) + ": mean land temp " + f2(t, 2) + " vs " + f2(tFull, 2) +
				" at sea 0; iced land " + f2(100 * iced, 1) + "%");
			near(t, tFull, 3, "seed " + std::to_string(seed) + " sea " + f2(sea, 0) + ": mean land temperature");
			ok(iced < 0.3, "not an ice ball: " + f2(iced));
			ok(mean(*w, w->temp, [&](int c) { return landSet(c) && alat(*w, c) < 30; }) > 10, "warm low latitudes");
			eq(finite(*w), std::string(""));
		}
	}
}

DM_TEST("climate: sea cells: stored temp stops at -2, sea ice forms from the unclamped value") {
	auto w = helperWorld(5, 3);
	climate::calibrate(*w);
	int coldSea = 0, polarIce = 0, polarSea = 0, tropicIce = 0;
	bool allAbove = true;
	for (int c = 0; c < w->n; c++) {
		if (isLand(*w, c)) continue;
		if (w->temp[size_t(c)] < -2 - 1e-6) allAbove = false;
		if (w->temp[size_t(c)] <= -2 + 1e-6) coldSea++;
		if (alat(*w, c) > 72) { polarSea++; if (w->ice[size_t(c)] > 0.5) polarIce++; }
		if (alat(*w, c) < 40 && w->ice[size_t(c)] > 0) tropicIce++;
	}
	say("sea cells at the -2 floor " + std::to_string(coldSea) + "; polar sea cells " + std::to_string(polarSea) + " of which iced " + std::to_string(polarIce));
	ok(allAbove, "no sea temp below -2");
	ok(coldSea > 50, "some sea is at the floor: " + std::to_string(coldSea));
	ok(polarSea > 20 && polarIce > 0.8 * polarSea, "the polar sea is frozen: " + std::to_string(polarIce) + " of " + std::to_string(polarSea));
	eq(tropicIce, 0, "no sea ice in the warm seas");
}

DM_TEST("climate: dust cools; emit and addDust clamp") {
	auto w = helperWorld(4, 1);
	climate::calibrate(*w);
	const std::vector<float> before = w->temp, ice = w->ice;
	eq(climate::anomaly(*w), 0.0);
	w->need.climate = false;
	climate::addDust(*w, 0.2);
	near(w->dust, 0.2, 1e-9); eq(w->need.climate, true, "addDust sets need.climate");
	near(climate::anomaly(*w), -1.8, 1e-9, "anomaly = globalTemp - 9 x dust");
	climate::update(*w);
	bool all = true;
	for (int c = 0; c < w->n; c++) if (isLand(*w, c) && std::fabs(w->temp[size_t(c)] - before[size_t(c)] + 1.8) > 1e-3) all = false;
	ok(all, "every land cell 1.8 C colder");
	ok(mean(*w, w->temp, [](int) { return true; }) < mean(*w, before, [](int) { return true; }) - 1, "the world is colder");
	ok(w->ice == ice, "update does not move ice");
	w->globalTemp = 3; near(climate::anomaly(*w), 1.2, 1e-9);
	climate::addDust(*w, 5); eq(w->dust, 1.0); climate::addDust(*w, -9); eq(w->dust, 0.0);
	climate::addDust(*w, std::nan("")); eq(w->dust, 0.0);
	climate::emit(*w, 20); eq(w->co2, 20.0); climate::emit(*w, 1000); eq(w->co2, 300.0); climate::emit(*w, -1e9); eq(w->co2, 0.0);
	climate::emit(*w, std::nan("")); eq(w->co2, 0.0); climate::emit(*w, kInf); eq(w->co2, 300.0);
	w->co2 = std::nan(""); climate::emit(*w, 5); eq(w->co2, 5.0, "a poisoned co2 is repaired");
	w->dust = std::nan(""); climate::addDust(*w, 0.1); near(w->dust, 0.1, 1e-12, "a poisoned dust is repaired");
}

// ------------------------------------------------------------------ rainfall
DM_TEST("climate: winds: easterly trades, westerlies, polar easterlies; up holds upwind neighbours") {
	auto w = squareWorld(5, 0);
	climate::update(*w);
	const Geo& g = *w->geo;
	const auto& up = upOf(*w);
	if (!eq(up.size(), size_t(w->n), "up has n entries")) return;
	int fromEast[3] = { 0, 0, 0 }, total[3] = { 0, 0, 0 };
	bool allNbr = true;
	for (int c = 0; c < w->n; c++) {
		bool isNbr = false;
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) if (g.nbr[size_t(6 * c + k)] == up[size_t(c)]) isNbr = true;
		if (!isNbr) allNbr = false;
		const double a = alat(*w, c);
		const int band = a > 5 && a < 24 ? 0 : a > 36 && a < 54 ? 1 : a > 66 && a < 84 ? 2 : -1;
		if (band < 0) continue;
		Vec3 E, N;
		g.basis(c, E, N);
		const size_t u = size_t(up[size_t(c)]), uc = size_t(c);
		const double d = (g.pos[3 * u] - g.pos[3 * uc]) * E.x + (g.pos[3 * u + 1] - g.pos[3 * uc + 1]) * E.y + (g.pos[3 * u + 2] - g.pos[3 * uc + 2]) * E.z;
		total[band]++; if (d > 0) fromEast[band]++;
	}
	ok(allNbr, "every up[c] is a neighbour of c");
	const double sT = double(fromEast[0]) / total[0], sW = double(fromEast[1]) / total[1], sP = double(fromEast[2]) / total[2];
	say("share of cells whose air comes from the east: trades " + f2(sT, 2) + " westerlies " + f2(sW, 2) + " polar " + f2(sP, 2));
	ok(sT > 0.95, "trades blow from the east: " + f2(sT));
	ok(sW < 0.05, "westerlies blow from the west: " + f2(sW));
	ok(sP > 0.95, "polar easterlies: " + f2(sP));
	// the trades drift toward the equator: the upwind neighbour lies on the poleward side more often than not
	int pole = 0, k = 0;
	for (int c = 0; c < w->n; c++) { const double a = alat(*w, c); if (a > 8 && a < 22) { k++; if (alat(*w, up[size_t(c)]) > a) pole++; } }
	ok(double(pole) / k > 0.6, "trades come from higher latitudes: " + f2(double(pole) / k));
	// the wind table is built once per geo and wind tuning
	eq(w->scratch<ClimateScratch>().gridBuilds, 1);
	for (int c = 0; c < w->n; c += 3) w->elev[size_t(c)] += 0.5f;
	climate::update(*w); climate::stepIce(*w); climate::calibrate(*w);
	eq(w->scratch<ClimateScratch>().gridBuilds, 1, "elevation changes do not rebuild the wind table");
}

DM_TEST("climate: rain shadow: the windward side of a mountain wall is clearly wetter than the lee") {
	for (int L : { 4, 5 }) {
		auto wall = squareWorld(L, 5), flat = squareWorld(L, 0);
		climate::calibrate(*wall); climate::calibrate(*flat);
		auto band = [](const World& w, double a0, double a1, double l0, double l1) {
			return mean(w, w.moist, [&](int c) { return isLand(w, c) && alat(w, c) >= a0 && alat(w, c) < a1 && lon(w, c) > l0 && lon(w, c) < l1; });
		};
		// which side the air comes from, read off the wind table rather than assumed
		auto side = [&](double la) {
			const Geo& g = *wall->geo;
			const int c = g.cellAt(std::cos(la / kD) * std::cos(-0.5), std::sin(la / kD), std::cos(la / kD) * std::sin(-0.5));
			return g.lon[size_t(upOf(*wall)[size_t(c)])] < g.lon[size_t(c)] ? -1 : 1;
		};
		const int sT = side(12), sW = side(45);
		ok(sT == -sW, "trades and westerlies blow opposite ways");
		auto lo0 = [](int s) { return s < 0 ? -50.0 : 15.0; };
		auto lo1 = [](int s) { return s < 0 ? -15.0 : 50.0; };
		const double tw = band(*wall, 3, 22, lo0(sT), lo1(sT)), tl = band(*wall, 3, 22, lo0(-sT), lo1(-sT)), tlFlat = band(*flat, 3, 22, lo0(-sT), lo1(-sT));
		const double ww = band(*wall, 38, 52, lo0(sW), lo1(sW)), wlee = band(*wall, 38, 52, lo0(-sW), lo1(-sW)), wlFlat = band(*flat, 38, 52, lo0(-sW), lo1(-sW));
		say("L" + std::to_string(L) + " trades: windward " + f2(tw) + " lee " + f2(tl) + " (lee without the wall " + f2(tlFlat) +
			") | westerlies: windward " + f2(ww) + " lee " + f2(wlee) + " (without the wall " + f2(wlFlat) + ")");
		ok(tw > 1.6 * tl, "trades: windward " + f2(tw) + " vs lee " + f2(tl));
		ok(ww > 1.6 * wlee, "westerlies: windward " + f2(ww) + " vs lee " + f2(wlee));
		ok(tl < 0.8 * tlFlat, "the wall dries its lee (trades): " + f2(tl) + " vs " + f2(tlFlat));
		ok(wlee < 0.9 * wlFlat, "the wall dries its lee (westerlies): " + f2(wlee) + " vs " + f2(wlFlat));
		// orographic rain: the windward slope is wetter than the same strip of the flat continent
		const double s0 = sT < 0 ? -9 : 2, s1 = sT < 0 ? -2 : 9;
		const double upw = band(*wall, 3, 22, s0, s1), upFlat = band(*flat, 3, 22, s0, s1);
		say("L" + std::to_string(L) + " windward slope " + f2(upw) + " vs the same strip without the wall " + f2(upFlat));
		ok(upw > upFlat + 0.1, "windward slope " + f2(upw) + " vs plain " + f2(upFlat));
		eq(finite(*wall), std::string(""));
	}
}

DM_TEST("climate: belts: wet equator, dry subtropics, a wet equatorial coast about 0.9") {
	auto flat = squareWorld(5, 0);
	climate::calibrate(*flat);
	const Geo& g = *flat->geo;
	const int probe = g.cellAt(std::cos(-0.5), 0.05, std::sin(-0.5));
	const int side = g.lon[size_t(upOf(*flat)[size_t(probe)])] < -0.5 ? -1 : 1;     // windward coast of the trades
	const World& F = *flat;
	const double coast = mean(F, F.moist, [&](int c) { return isLand(F, c) && alat(F, c) < 8 && side * lon(F, c) > 52; });
	const double eqM = mean(F, F.moist, [&](int c) { return isLand(F, c) && alat(F, c) < 8; });
	const double dry = mean(F, F.moist, [&](int c) { return isLand(F, c) && alat(F, c) > 24 && alat(F, c) < 34; });
	const double mid = mean(F, F.moist, [&](int c) { return isLand(F, c) && alat(F, c) > 40 && alat(F, c) < 52; });
	say("square continent: equatorial windward coast " + f2(coast) + " equatorial land " + f2(eqM) + " land at 24-34 " + f2(dry) + " land at 40-52 " + f2(mid));
	ok(coast > 0.8 && coast <= 1, "a wet equatorial coast is about 0.9: " + f2(coast));
	ok(eqM > 0.5, "wet equatorial belt " + f2(eqM));
	ok(dry < 0.2 && dry < 0.35 * eqM, "dry subtropics " + f2(dry));
	ok(mid > 1.5 * dry, "the storm belt is wetter than the subtropics: " + f2(mid));
	for (uint32_t seed : { 1u, 2u, 3u }) {
		auto w = helperWorld(5, seed);
		climate::calibrate(*w);
		const World& W = *w;
		auto sea = [&](double a0, double a1) { return mean(W, W.moist, [&](int c) { return !isLand(W, c) && alat(W, c) >= a0 && alat(W, c) < a1; }); };
		auto land = [&](double a0, double a1) { return mean(W, W.moist, [&](int c) { return isLand(W, c) && alat(W, c) >= a0 && alat(W, c) < a1; }); };
		say("helper seed " + std::to_string(seed) + " land moist by band 0-10 / 22-34 / 38-52: " + f2(land(0, 10), 2) + " " + f2(land(22, 34), 2) + " " +
			f2(land(38, 52), 2) + " | sea: " + f2(sea(0, 10), 2) + " " + f2(sea(22, 34), 2) + " " + f2(sea(38, 52), 2));
		ok(sea(0, 8) > 2 * sea(24, 32) && sea(40, 52) > 1.5 * sea(24, 32), "sea: wet equator, dry belt, wet storm belt");
		ok(land(0, 10) > 0.45 && land(0, 10) > 1.6 * land(22, 34), "land: wet equator vs dry subtropics");
		ok(count(W, [&](int c) { return !(W.moist[size_t(c)] >= 0 && W.moist[size_t(c)] <= 1); }) == 0, "moist in 0..1");
	}
}

DM_TEST("climate: coasts are wetter than deep interiors") {
	double sc = 0, sd = 0; int kc = 0, kd = 0;
	for (uint32_t seed = 1; seed <= 6; seed++) {
		auto w = helperWorld(5, seed);
		climate::calibrate(*w);
		const World& W = *w;
		const auto d = coastDist(W);
		auto okc = [&](int c) { return isLand(W, c) && W.ice[size_t(c)] < 0.5 && alat(W, c) > 12 && alat(W, c) < 60; };
		const double coast = mean(W, W.moist, [&](int c) { return okc(c) && d[size_t(c)] <= 2; });
		const double deep = mean(W, W.moist, [&](int c) { return okc(c) && d[size_t(c)] >= 4; });
		say("helper seed " + std::to_string(seed) + " moist within 2 hops of the sea " + f2(coast) + ", 4+ hops inland " + f2(deep) + ", ratio " + f2(coast / deep, 2));
		ok(coast > 1.1 * deep, "seed " + std::to_string(seed) + ": coast " + f2(coast) + " vs interior " + f2(deep));
		for (int c = 0; c < W.n; c++) if (okc(c)) {
			if (d[size_t(c)] <= 2) { sc += W.moist[size_t(c)]; kc++; } else if (d[size_t(c)] >= 4) { sd += W.moist[size_t(c)]; kd++; }
		}
	}
	ok(sc / kc > 1.3 * sd / kd, "six worlds together: coast " + f2(sc / kc) + " vs interior " + f2(sd / kd));
	auto flat = squareWorld(5, 0);
	climate::calibrate(*flat);
	const World& F = *flat;
	const auto d = coastDist(F);
	const double c2 = mean(F, F.moist, [&](int c) { return isLand(F, c) && F.ice[size_t(c)] < 0.5 && d[size_t(c)] <= 2; });
	const double c8 = mean(F, F.moist, [&](int c) { return isLand(F, c) && F.ice[size_t(c)] < 0.5 && d[size_t(c)] >= 8; });
	say("square continent: coast " + f2(c2) + " vs 8+ hops inland " + f2(c8));
	ok(c2 > 1.15 * c8, "square continent: coast " + f2(c2) + " vs interior " + f2(c8));
}

DM_TEST("climate: a planet with no sea is bone dry; a planet with no land does not break") {
	stub::Guard guard;
	std::vector<std::unique_ptr<World>> dry;
	dry.push_back(helperWorld(5, 1, -9));
	dry.push_back(uniformWorld(4, 2, 0));
	dry.push_back(uniformWorld(4, 0, -9, Phase::Water));
	for (auto& w : dry) {
		climate::calibrate(*w); climate::update(*w); climate::stepIce(*w);
		double mx = 0; for (int c = 0; c < w->n; c++) mx = std::max(mx, double(w->moist[size_t(c)]));
		ok(mx < 0.05, "moist < 0.05 everywhere, max " + f2(mx, 5));
		eq(count(*w, [&](int c) { return w->ice[size_t(c)] > 0; }), 0, "no snow without a sea");
		eq(finite(*w), std::string(""));
		w->wet[5] = 0.6f; climate::update(*w);
		near(w->moist[5], 0.6, 1e-6, "the rain miracle still wets it");
	}
	auto ocean = uniformWorld(4, -3, 0);
	climate::calibrate(*ocean); climate::update(*ocean); climate::stepIce(*ocean);
	hookVeg(0.6);
	for (int y = 0; y < 30; y++) climate::yearly(*ocean);
	eq(finite(*ocean), std::string(""));
	eq(*ocean->vars.iceRef, 50.0, "iceRef floor");
	near(ocean->seaLevel, 0, 1e-9, "no land ice to melt: the sea stays");
	ok(mean(*ocean, ocean->moist, [&](int c) { return alat(*ocean, c) < 8; }) > 0.5, "rain over the equatorial sea");
}

DM_TEST("climate: fixed RAIN_REF: a new mountain does not change moist on the far side of the planet") {
	for (uint32_t seed : { 1u, 3u }) {
		auto w = helperWorld(5, seed);
		climate::calibrate(*w);
		const std::vector<float> before = w->moist;
		const int c0 = findLand(*w, [&](int c) { return alat(*w, c) > 8 && alat(*w, c) < 22 && w->elev[size_t(c)] > 0.2 && w->elev[size_t(c)] < 1 && before[size_t(c)] > 0.3; });
		if (!ok(c0 >= 0)) continue;
		std::vector<int32_t> list;
		const int k = w->geo->within(c0, 0.12, list);
		for (int i = 0; i < k; i++) if (isLand(*w, list[size_t(i)])) w->elev[size_t(list[size_t(i)])] = std::min(8.0f, w->elev[size_t(list[size_t(i)])] + 5);
		climate::update(*w);
		double nearD = 0, farD = 0; int farCells = 0;
		for (int c = 0; c < w->n; c++) {
			const double a = w->geo->angle(c0, c), d = std::fabs(w->moist[size_t(c)] - before[size_t(c)]);
			if (a < 0.5) nearD = std::max(nearD, d);
			if (a > 1.7) { farD = std::max(farD, d); farCells++; }
		}
		say("seed " + std::to_string(seed) + " largest change within 0.5 rad of the new mountain " + f2(nearD) + "; beyond 1.7 rad " + f2(farD, 6) + " over " + std::to_string(farCells) + " cells");
		ok(nearD > 0.1, "the mountain changes its own surroundings: " + f2(nearD));
		ok(farD < 1e-3, "far side untouched: " + f2(farD, 6));
	}
}

DM_TEST("climate: moist adds wet and subtracts 0.25 x drought") {
	auto w = helperWorld(5, 2);
	climate::calibrate(*w);
	const std::vector<float> before = w->moist;
	const int c0 = findLand(*w, [&](int c) { return before[size_t(c)] > 0.4 && before[size_t(c)] < 0.6; });
	if (!ok(c0 >= 0)) return;
	{ Disaster& d = state::newDisaster(*w, DisasterType::Drought, c0); d.ang = 0.25; d.power = 1; d.dur = 8; }
	{ Disaster& d = state::newDisaster(*w, DisasterType::Drought, -1); d.ang = 0.3; }            // junk entries are ignored
	{ Disaster& d = state::newDisaster(*w, DisasterType::Drought, 3); d.ang = std::nan(""); }
	{ Disaster& d = state::newDisaster(*w, DisasterType::Drought, w->n); d.ang = 0.3; }
	{ Disaster& d = state::newDisaster(*w, DisasterType::Storm, c0); d.ang = 0.3; }
	climate::update(*w);
	near(w->moist[size_t(c0)], before[size_t(c0)] - 0.25, 1e-5, "centre of a full drought");
	double outside = 0; int inside = 0; bool onlyDries = true;
	for (int c = 0; c < w->n; c++) {
		const double a = w->geo->angle(c0, c), dd = double(before[size_t(c)]) - w->moist[size_t(c)];
		if (a > 0.25 + 1e-6) outside = std::max(outside, std::fabs(dd));
		else { if (!(dd >= -1e-6 && dd <= 0.25 + 1e-5)) onlyDries = false; inside++; }
	}
	ok(onlyDries, "drought only dries, by at most 0.25");
	ok(inside > 30, "cells inside " + std::to_string(inside)); ok(outside < 1e-6, "nothing changes outside the drought: " + f2(outside, 8));
	w->disasters[0].power = 0.4; climate::update(*w);
	near(w->moist[size_t(c0)], before[size_t(c0)] - 0.1, 1e-5, "power scales it");
	w->disasters.clear();
	w->wet[size_t(c0)] = 0.3f; climate::update(*w);
	near(w->moist[size_t(c0)], before[size_t(c0)] + 0.3, 1e-5, "wet adds");
	w->wet[size_t(c0)] = 1; climate::update(*w);
	eq(w->moist[size_t(c0)], 1.0f, "saturates at 1");
}

DM_TEST("climate: update: flags, determinism, no rng, idempotent") {
	stub::Guard guard;
	auto a = helperWorld(5, 4), b = helperWorld(5, 4);
	clearFlags(*a); a->need.climate = true;
	const uint32_t rng = a->rng.s;
	climate::update(*a);
	eq(a->need.climate, false, "clears need.climate"); eq(a->need.life, true, "sets need.life"); eq(a->dirty.color, true, "sets dirty.color");
	eq(a->need.hydro, true, "the first rainfall asks the rivers to follow");
	eq(a->need.derive, false); eq(a->dirty.elev, false); eq(a->dirty.water, false);
	clearFlags(*a);
	const std::vector<float> m1 = a->moist, t1 = a->temp;
	climate::update(*a);
	eq(a->need.hydro, false, "unchanged rainfall leaves the rivers alone");
	ok(a->moist == m1, "idempotent moist"); ok(a->temp == t1, "idempotent temp");
	std::fill(a->wet.begin(), a->wet.end(), 0.5f); climate::update(*a);
	eq(a->need.hydro, true, "a rain miracle moves moist enough to refresh the rivers");
	std::fill(a->wet.begin(), a->wet.end(), 0.f); climate::update(*a);
	climate::update(*b);
	ok(b->moist == m1, "same seed, same rainfall"); ok(b->temp == t1, "same seed, same temperature");
	climate::calibrate(*a); climate::stepIce(*a); hookVeg(0.6); climate::yearly(*a);
	eq(a->rng.s, rng, "the climate never draws from w.rng");
	auto other = helperWorld(5, 5);
	climate::update(*other);
	ok(other->temp != t1, "another seed, another world");
}

DM_TEST("climate: determinism: worlds simulated side by side do not disturb each other") {
	stub::Guard guard;
	hookVeg(0.6);
	auto mk = [](uint32_t seed, int L) { auto w = helperWorld(L, seed); climate::calibrate(*w); return w; };
	auto year = [](World& w, int y) {
		climate::emit(w, 0.7); if (y == 30) climate::addDust(w, 0.3);
		climate::yearly(w);
		if (w.need.climate && y % 2 == 0) climate::update(w);
		if (y % 5 == 0) climate::stepIce(w);
	};
	auto solo = mk(4, 4), a = mk(4, 4), b = mk(4, 4), other = mk(9, 3);
	for (int y = 1; y <= 120; y++) year(*solo, y);
	for (int y = 1; y <= 120; y++) { year(*a, y); year(*other, y); year(*b, y); }
	for (World* w : { a.get(), b.get() }) {
		ok(w->temp == solo->temp, "temp"); ok(w->moist == solo->moist, "moist"); ok(w->ice == solo->ice, "ice");
		eq(w->seaLevel, solo->seaLevel); eq(w->co2, solo->co2); eq(w->globalTemp, solo->globalTemp); eq(w->dust, solo->dust);
		ok(w->vars.num == solo->vars.num && w->vars.seaBase == solo->vars.seaBase && w->vars.iceRef == solo->vars.iceRef, "vars");
		eq(hash(*w), hash(*solo), "world hash");
	}
	ok(solo->co2 > 50 && solo->globalTemp > 0.5, "and something did happen: co2 " + f2(solo->co2));
}

// ------------------------------------------------------------------ ice
DM_TEST("climate: ice equilibrium: land 0 at -4 ... 1 at -9, sea 0 at -2.5 ... 1 at -6; stepIce 6 % / 30 %") {
	auto w = helperWorld(5, 1);
	climate::update(*w);                                               // ice still 0: temp is the ice-free temperature
	const std::vector<float> tFree = w->temp;
	const int land = findLand(*w, [&](int c) { return tFree[size_t(c)] < -12; });
	const int warm = findLand(*w, [&](int c) { return tFree[size_t(c)] > 5; });
	int sea = -1;
	for (int c = 0; c < w->n && sea < 0; c++) if (!isLand(*w, c) && alat(*w, c) > 80) sea = c;
	if (!ok(land >= 0 && warm >= 0 && sea >= 0, "probe cells")) return;
	eq(w->temp[size_t(sea)], -2.0f, "polar sea is stored at the floor");
	clearFlags(*w);
	climate::stepIce(*w);
	near(w->ice[size_t(land)], 0.06, 1e-6, "history: 6 % of the way"); near(w->ice[size_t(sea)], 0.06, 1e-6, "sea ice from the unclamped temperature");
	eq(w->ice[size_t(warm)], 0.0f);
	near(w->temp[size_t(land)], tFree[size_t(land)] - 4 * 0.06, 1e-4, "albedo follows the ice");
	eq(w->dirty.color, true); eq(w->dirty.water, true, "sea ice changed");
	climate::stepIce(*w);
	near(w->ice[size_t(land)], 0.06 + 0.94 * 0.06, 1e-6);
	for (Phase ph : { Phase::Molten, Phase::Water, Phase::Life, Phase::People }) {
		auto x = helperWorld(5, 1, 0, ph); climate::update(*x); climate::stepIce(*x);
		near(x->ice[size_t(land)], 0.30, 1e-6, std::string("genesis 30 %: ") + toStr(ph));
	}
	{ auto e = helperWorld(5, 1, 0, Phase::Ended); climate::update(*e); climate::stepIce(*e); near(e->ice[size_t(land)], 0.06, 1e-6, "ended"); }
	// many steps reach the same equilibrium that calibrate sets directly
	for (int i = 0; i < 250; i++) climate::stepIce(*w);
	auto cal = helperWorld(5, 1); climate::calibrate(*cal);
	const double worst = maxAbsDiff(w->ice, cal->ice);
	ok(worst < 0.02, "stepIce converges on the calibrated ice: " + f2(worst, 5));
	clearFlags(*w); climate::stepIce(*w);
	eq(w->dirty.color, false, "nothing changed, nothing flagged"); eq(w->dirty.water, false);
	// the equilibrium itself, on the calibrated world (temp includes the -4 x ice albedo term)
	int full = 0, none = 0, bad = 0;
	for (int c = 0; c < cal->n; c++) {
		const double ic = cal->ice[size_t(c)], t = cal->temp[size_t(c)];
		if (!(ic >= 0 && ic <= 1)) bad++;
		if (isLand(*cal, c)) {
			if (t < -9) { if (std::fabs(ic - 1) > 1e-4) bad++; full++; }
			else if (t > -4) { if (std::fabs(ic) > 1e-4) bad++; none++; }
			else if (std::fabs(ic - (-4 - t) / 5) > 1e-3) bad++;
		} else if (ic == 0 && t < -2) bad++;
	}
	eq(bad, 0, "every cell on its equilibrium");
	ok(full > 100 && none > 1000, "both kinds of land exist: " + std::to_string(full) + ", " + std::to_string(none));
}

DM_TEST("climate: stepIce before any update, on a loaded world and at another grid size") {
	auto w = helperWorld(4, 2);
	climate::stepIce(*w);                                              // no climate::update has run on this world
	ok(count(*w, [&](int c) { return w->ice[size_t(c)] > 0; }) > 10, "ice starts to form");
	eq(finite(*w), std::string(""));
	auto big = helperWorld(5, 2); climate::update(*big); climate::stepIce(*big);
	auto small = helperWorld(3, 3); climate::update(*small); climate::stepIce(*small); climate::calibrate(*small);
	eq(finite(*big), std::string("")); eq(finite(*small), std::string(""));
	// after save / load the transient caches are gone and everything still works
	climate::calibrate(*w);
	std::vector<uint8_t> bytes;
	std::string why;
	if (!ok(state::serialize(*w, bytes, &why), "serialize: " + why)) return;
	auto l = state::deserialize(bytes, &why);
	if (!ok(l != nullptr, "deserialize: " + why)) return;
	watch(*l);
	eq(l->scratch<ClimateScratch>().gridBuilds, 0, "no wind table yet");
	climate::stepIce(*l); climate::update(*l);
	eq(upOf(*l).size(), size_t(l->n), "wind table rebuilt");
	const double worst = maxAbsDiff(l->temp, w->temp);
	ok(worst < 0.5, "a loaded world gets the same climate (elev and ice are quantized): " + f2(worst, 4));
	near(*l->vars.seaBase, *w->vars.seaBase, 1e-12); near(*l->vars.iceRef, *w->vars.iceRef, 1e-9);
	ok(l->vars.num == w->vars.num, "climate_* vars survive a save");
}

// ------------------------------------------------------------------ calibrate
DM_TEST("climate: calibrate: finite seaBase, iceRef >= 50, ice at its equilibrium, idempotent") {
	const std::pair<uint32_t, int> cases[] = { { 1, 5 }, { 2, 5 }, { 3, 4 }, { 1, 3 } };
	for (const auto& [seed, L] : cases) {
		auto w = helperWorld(L, seed, 0.3, Phase::People);
		w->vars = Vars{};
		clearFlags(*w); w->need.climate = true;
		climate::calibrate(*w);
		ok(w->vars.seaBase && *w->vars.seaBase == 0.3, "seaBase = seaLevel"); near(w->seaLevel, 0.3, 1e-12);
		double li = 0; for (int c = 0; c < w->n; c++) if (isLand(*w, c)) li += w->ice[size_t(c)];
		const double ref = w->vars.iceRef ? *w->vars.iceRef : -1;
		say("seed " + std::to_string(seed) + " L" + std::to_string(L) + " land ice " + f2(li, 1) + " iceRef " + f2(ref, 1));
		ok(std::isfinite(ref) && ref >= 50, "iceRef " + f2(ref));
		near(ref, std::max(50.0, li), 1e-3, "iceRef = max(50, sum of land ice)");
		eq(w->need.climate, false); eq(w->need.life, true); eq(w->dirty.color, true);
		ok(count(*w, [&](int c) { return w->moist[size_t(c)] > 0.3; }) > 20, "rainfall is filled in too");
		const std::vector<float> ice = w->ice, temp = w->temp, moist = w->moist;
		climate::stepIce(*w);
		ok(maxAbsDiff(w->ice, ice) < 1e-3, "already at equilibrium: stepIce moves nothing (" + f2(maxAbsDiff(w->ice, ice), 6) + ")");
		climate::calibrate(*w);
		ok(*w->vars.seaBase == 0.3); near(*w->vars.iceRef, ref, 1e-3, "second calibrate: same iceRef");
		const double worst = std::max({ maxAbsDiff(w->ice, ice), maxAbsDiff(w->temp, temp), maxAbsDiff(w->moist, moist) });
		ok(worst < 1e-3, "second calibrate changes nothing: " + f2(worst, 6));
		eq(finite(*w), std::string(""));
	}
	auto bad = helperWorld(3, 1);
	bad->vars = Vars{}; bad->seaLevel = std::nan("");
	climate::calibrate(*bad);
	ok(bad->vars.seaBase && *bad->vars.seaBase == 0, "a NaN sea level is repaired, not copied"); eq(bad->seaLevel, 0.0);
	ok(bad->vars.iceRef && *bad->vars.iceRef >= 50);
	eq(finite(*bad), std::string(""));
}

// ------------------------------------------------------------------ yearly
DM_TEST("climate: yearly does nothing but decay dust in genesis phases") {
	stub::Guard guard;
	hookVeg(0.6);
	for (Phase ph : { Phase::Molten, Phase::Water, Phase::Life, Phase::People }) {
		for (bool calibrated : { false, true }) {
			auto w = helperWorld(4, 1, -2, ph);
			if (calibrated) climate::calibrate(*w); else { w->vars = Vars{}; climate::update(*w); }
			w->co2 = 80; w->globalTemp = 1.5; w->dust = 0.5;
			const double sea = w->seaLevel;
			const std::vector<float> ice = w->ice;
			const auto num = w->vars.num;
			const auto sb = w->vars.seaBase, ir = w->vars.iceRef;
			const auto ev = capture(*w, [&] { for (int y = 0; y < 60; y++) climate::yearly(*w); });
			const std::string tag = std::string(toStr(ph)) + (calibrated ? " (calibrated)" : "");
			eq(ev.size(), size_t(0), tag + ": no events");
			eq(w->seaLevel, sea, tag + ": sea untouched"); eq(w->co2, 80.0); eq(w->globalTemp, 1.5);
			near(w->dust, 0.5 * std::pow(0.93, 60), 1e-9, "dust x 0.93 per year");
			ok(w->ice == ice, tag + ": ice untouched");
			ok(w->vars.num == num && w->vars.seaBase == sb && w->vars.iceRef == ir, tag + ": vars untouched");
		}
	}
	auto w = helperWorld(3, 1, 0, Phase::Water); w->dust = 1e-4; climate::yearly(*w);
	eq(w->dust, 0.0, "dust settles to exactly 0");
}

DM_TEST("climate: yearly never touches the sea while vars.seaBase is missing: 400 years, finite sea level") {
	stub::Guard guard;
	hookVeg(0.6);
	for (double sea : { 0.0, -9.0, 0.7 }) {
		auto w = helperWorld(4, 2, sea, Phase::History);
		w->vars = Vars{};                                              // a world "created with phase history and empty vars"
		w->co2 = 120; w->dust = 0.3; w->globalTemp = 2;
		const auto ev = capture(*w, [&] { runYears(*w, 400); });
		eq(w->seaLevel, sea, "sea level untouched"); ok(std::isfinite(w->seaLevel));
		ok(!w->vars.seaBase, "yearly does not calibrate behind Sim's back");
		eq(w->co2, 120.0, "and leaves the greenhouse alone"); eq(w->globalTemp, 2.0);
		eq(w->dust, 0.0, "dust still settles");
		eq(milestones(ev).size(), size_t(0));
		eq(finite(*w), std::string(""));
	}
	for (double bad : { std::nan(""), kInf, -kInf }) {
		auto w = helperWorld(3, 2);
		w->vars = Vars{}; w->vars.seaBase = bad; w->co2 = 50;
		for (int y = 0; y < 50; y++) climate::yearly(*w);
		eq(w->seaLevel, 0.0, "non-finite seaBase: sea untouched"); eq(w->co2, 50.0);
	}
}

DM_TEST("climate: yearly: a calibrated, undisturbed world keeps its sea level exactly") {
	stub::Guard guard;
	hookVeg(0.6);
	auto w = helperWorld(4, 3, 0.2);
	climate::calibrate(*w);
	clearFlags(*w);
	const auto ev = capture(*w, [&] { runYears(*w, 400); });
	say("undisturbed: sea after 400 years " + f2(w->seaLevel, 6) + " (base 0.2), co2 " + f2(w->co2) + " globalTemp " + f2(w->globalTemp));
	eq(w->seaLevel, 0.2, "no drift without a cause, not even in the last bit");
	eq(w->co2, 0.0); eq(w->globalTemp, 0.0); eq(milestones(ev).size(), size_t(0));
	eq(w->need.derive, false, "and never asks for a re-derive");
	eq(finite(*w), std::string(""));
}

DM_TEST("climate: yearly: sustained emissions reach the documented equilibrium; ice shrinks; the sea rises slowly in bounds") {
	stub::Guard guard;
	const bool hooked = hookVeg(0.6);
	auto w = helperWorld(5, 1);
	climate::calibrate(*w);
	const double base = *w->vars.seaBase, ice0 = climate::landIce(*w);
	const double perYear = 40 * 0.0025 * std::sqrt(20000.0 / 1000);    // 40 cities of 20,000
	double maxStep = 0, prev = w->seaLevel, lo = 1e9, hi = -1e9;
	const auto ev = capture(*w, [&] {
		runYears(*w, 1500, [&](int) {
			climate::emit(*w, perYear);
			maxStep = std::max(maxStep, std::fabs(w->seaLevel - prev)); prev = w->seaLevel;
			lo = std::min(lo, w->seaLevel); hi = std::max(hi, w->seaLevel);
		});
	});
	const auto ms = milestones(ev);
	const double veg = life::cover(*w).veg;
	say("40 cities x 20,000 for 1,500 years: co2 " + f2(w->co2, 1) + " anomaly " + f2(climate::anomaly(*w), 2) + " land ice " + f2(ice0, 0) + " -> " +
		f2(climate::landIce(*w), 0) + " sea " + f2(base, 2) + " -> " + f2(w->seaLevel, 3) + " largest yearly step " + f2(maxStep, 4) + " milestones: " + join(ms));
	// emit then uptake each year: c' = (c + e)(1 - k), k = 0.005 (0.5 + veg); from 0 after 1,500 years c = e (1 - k) / k x (1 - (1 - k)^1500)
	// (exact for a constant veg; with the real Life in `run.ps1 all`, veg = 0 and the 400-year time constant is not over yet)
	const double kUp = 0.005 * (0.5 + veg);
	near(w->co2, perYear * (1 - kUp) / kUp * (1 - std::pow(1 - kUp, 1500)), 0.5, "equilibrium = emissions / (0.005 x (0.5 + veg))");
	if (hooked) { near(w->co2, 81.3, 3, "co2 about 80"); near(climate::anomaly(*w), 2.6, 0.15, "anomaly about +2.6"); }
	ok(climate::landIce(*w) < 0.9 * ice0, "ice shrinks: " + f2(ice0, 0) + " -> " + f2(climate::landIce(*w), 0));
	ok(w->seaLevel > base + 0.1, "the sea rises: " + f2(w->seaLevel));
	ok(lo >= base - 0.3 - 1e-9 && hi <= base + 0.6 + 1e-9, "within seaBase -0.3 ... +0.6");
	ok(maxStep <= 0.005 + 1e-9, "at most 0.005 per year: " + f2(maxStep, 6));
	if (hooked) { eq(countOf(ms, "warming 1"), 1); eq(countOf(ms, "warming 2"), 1); eq(countOf(ms, "warming 3"), 0); }
	eq(std::set<std::string>(ms.begin(), ms.end()).size(), ms.size(), "each milestone fires once: " + join(ms));
	if (w->seaLevel >= base + 0.2) eq(countOf(ms, "sea-rise 0.2"), 1);
	eq(finite(*w), std::string(""));
}

DM_TEST("climate: yearly: CO2 uptake, the 30-year lag and the vegetation term") {
	stub::Guard guard;
	if (!stub::stubbed("life")) return;                                 // exact vegetation numbers need the hook
	auto w = helperWorld(3, 1);
	climate::calibrate(*w);
	w->co2 = 100;
	hookVeg(0.5);
	climate::yearly(*w);
	near(w->co2, 100 - 100 * 0.005 * 1.0, 1e-9, "uptake 0.5 % x (0.5 + veg)");
	near(w->globalTemp, 3 * std::log2(1 + w->co2 / 100) / 30, 1e-9, "one thirtieth of the gap");
	hookVeg(1); w->co2 = 100; climate::yearly(*w);
	near(w->co2, 100 - 100 * 0.005 * 1.5, 1e-9, "a green world takes up more");
	hookVeg(std::nan("")); w->co2 = 100; climate::yearly(*w);
	near(w->co2, 99.5, 1e-9, "a NaN cover is not believed: veg 0.5 assumed");
	hookVeg(0.5); w->co2 = 300; w->globalTemp = 0;
	for (int y = 0; y < 30; y++) { w->co2 = 300; climate::yearly(*w); }
	near(w->globalTemp, 6 * (1 - std::pow(29.0 / 30, 30)), 0.05, "about 63 % of the way after 30 years");
	w->co2 = std::nan(""); w->globalTemp = std::nan(""); w->dust = std::nan(""); climate::yearly(*w);
	eq(finite(*w), std::string(""), "poisoned scalars are repaired");
}

DM_TEST("climate: yearly: sea level follows land ice and warmth, 0.005 per year, clamped; re-derive flags every 0.03") {
	stub::Guard guard;
	hookVeg(0.5);
	// hot: all land ice gone and co2 held at the cap
	auto hot = helperWorld(5, 1);
	climate::calibrate(*hot);
	const double base = *hot->vars.seaBase, ref = *hot->vars.iceRef;
	ok(ref > 50, "this world has real ice caps: " + f2(ref, 1));
	int flagged = 0; double maxStep = 0, prev = hot->seaLevel; bool allSix = true;
	const auto ev = capture(*hot, [&] {
		for (int y = 1; y <= 400; y++) {
			hot->co2 = 300; std::fill(hot->ice.begin(), hot->ice.end(), 0.f);
			clearFlags(*hot);
			climate::yearly(*hot);
			maxStep = std::max(maxStep, std::fabs(hot->seaLevel - prev)); prev = hot->seaLevel;
			if (y == 1) near(hot->seaLevel, base + 0.005, 1e-9, "first year: +0.005");
			if (hot->need.derive) {
				flagged++;
				if (!(hot->need.hydro && hot->need.climate && hot->dirty.water && hot->dirty.color && hot->dirty.objects)) allSix = false;
			}
		}
	});
	ok(allSix, "all six flags together");
	const double top = base + 0.35 + 0.04 * hot->globalTemp;
	say("no ice, co2 300: sea " + f2(base, 2) + " -> " + f2(hot->seaLevel) + " (target " + f2(top) + ", cap " + f2(base + 0.6, 2) + "); " + std::to_string(flagged) + " re-derive requests");
	near(hot->seaLevel, top, 0.01, "base + 0.35 (ice) + 0.04 x globalTemp");
	ok(hot->seaLevel <= base + 0.6 + 1e-9); ok(maxStep <= 0.005 + 1e-9, "step " + f2(maxStep, 6));
	const double moved = hot->seaLevel - base;
	ok(flagged >= int(std::floor(moved / 0.04)) && flagged <= int(std::ceil(moved / 0.03)), "one request per 0.03-0.035 of rise: " + std::to_string(flagged) + " for " + f2(moved));
	const auto ms = milestones(ev);
	eq(countOf(ms, "sea-rise 0.2"), 1); eq(countOf(ms, "sea-rise 0.4"), 1);
	std::vector<std::string> warms;
	for (const auto& m : ms) if (m.rfind("warming", 0) == 0) warms.push_back(m);
	eq(join(warms), std::string("warming 1,warming 2,warming 3,warming 4"), "warming milestones in order, once each");
	eq(std::set<std::string>(ms.begin(), ms.end()).size(), ms.size());
	// cold: the land ice has more than doubled
	auto cold = helperWorld(5, 1);
	climate::calibrate(*cold);
	const auto ev2 = capture(*cold, [&] { for (int y = 1; y <= 200; y++) { std::fill(cold->ice.begin(), cold->ice.end(), 1.f); climate::yearly(*cold); } });
	say("land ice everywhere: sea " + f2(base, 2) + " -> " + f2(cold->seaLevel));
	near(cold->seaLevel, base - 0.25, 1e-6, "ice doubled: -0.25"); ok(cold->seaLevel >= base - 0.3);
	eq(join(milestones(ev2)), std::string("sea-fall -0.2"), "one sea-fall milestone");
	// half of the land ice gone: + 0.35 x 0.5
	auto half = helperWorld(5, 1);
	climate::calibrate(*half);
	const std::vector<float> keep = half->ice;
	for (int y = 1; y <= 100; y++) {
		for (int c = 0; c < half->n; c++) half->ice[size_t(c)] = isLand(*half, c) ? keep[size_t(c)] * 0.5f : keep[size_t(c)];
		climate::yearly(*half);
	}
	// (a little more than 0.175: the rising sea floods some iced lowland, which then no longer counts as land ice)
	ok(half->seaLevel >= base + 0.175 - 1e-6 && half->seaLevel < base + 0.195, "r = 0.5 -> +0.175: " + f2(half->seaLevel, 4));
	// a world with almost no ice at calibration (iceRef at its floor) must not start to drown at once
	auto bare = squareWorld(4, 0);
	climate::calibrate(*bare);
	eq(*bare->vars.iceRef, 50.0, "floor"); ok(climate::landIce(*bare) < 50);
	for (int y = 0; y < 100; y++) climate::yearly(*bare);
	near(bare->seaLevel, 0, 1e-9, "r = 1 at calibration even below the iceRef floor");
	// hand-calibrated world without iceRef (the helper sets only seaBase): the first yearly finishes the calibration,
	// so the ice settling afterwards is not mistaken for an ice age
	for (bool started : { false, true }) {
		auto hand = helperWorld(4, 2);
		ok(!hand->vars.iceRef, "no iceRef yet"); eq(count(*hand, [&](int c) { return hand->ice[size_t(c)] > 0; }), 0, "no ice yet");
		if (started) { climate::update(*hand); for (int i = 0; i < 3; i++) climate::stepIce(*hand); }
		auto cal = helperWorld(4, 2); climate::calibrate(*cal);
		hand->need.climate = false;
		climate::yearly(*hand);
		near(*hand->vars.iceRef, *cal->vars.iceRef, 1e-3, "same iceRef as calibrate gives");
		ok(*hand->vars.iceRef > 60, "a real reference, not the floor: " + f2(*hand->vars.iceRef, 1));
		const double worst = std::max(maxAbsDiff(hand->ice, cal->ice), maxAbsDiff(hand->temp, cal->temp));
		ok(worst < 1e-3, "ice and temp are the calibrated ones: " + f2(worst, 6));
		eq(hand->need.climate, true, "and the rainfall is asked to follow");
		ok(hand->vars.seaBase && *hand->vars.seaBase == 0, "the hand-set seaBase is kept");
		runYears(*hand, 300);
		say("hand-calibrated world (seaBase only), 300 years: sea " + f2(hand->seaLevel, 5) + " iceRef " + f2(*hand->vars.iceRef, 1));
		ok(std::fabs(hand->seaLevel) < 1e-3, "the sea stays where it was: " + f2(hand->seaLevel, 6));
		eq(finite(*hand), std::string(""));
	}
}

DM_TEST("climate: yearly: need.climate once the anomaly has moved 0.25 C since the last update") {
	stub::Guard guard;
	hookVeg(0.5);
	auto w = helperWorld(4, 1);
	climate::calibrate(*w);
	int first = -1;
	for (int y = 1; y <= 12 && first < 0; y++) {
		w->co2 = 100; climate::yearly(*w);
		if (w->need.climate) first = y; else ok(w->globalTemp < 0.25);
	}
	ok(first > 0 && w->globalTemp >= 0.25 && w->globalTemp < 0.36, "flagged in year " + std::to_string(first) + " at " + f2(w->globalTemp));
	climate::update(*w);
	eq(w->need.climate, false);
	w->co2 = 0; climate::yearly(*w);
	eq(w->need.climate, false, "a small change is not flagged");
	w->dust = 0.05; w->need.climate = false; climate::yearly(*w);
	eq(w->need.climate, true, "dust moves the anomaly too");
}

DM_TEST("climate: milestones: cooling, ice-age and thaw fire once per winter") {
	stub::Guard guard;
	hookVeg(0.5);
	auto w = helperWorld(4, 1);
	climate::calibrate(*w);
	auto ev = capture(*w, [&] { climate::addDust(*w, 0.25); for (int y = 0; y < 40; y++) climate::yearly(*w); });   // a meteor: -2.25 C
	eq(join(milestones(ev)), std::string("cooling -2,thaw -1"), "meteor winter");
	ev = capture(*w, [&] { for (int y = 0; y < 40; y++) climate::yearly(*w); });
	eq(milestones(ev).size(), size_t(0), "nothing more once it is over");
	ev = capture(*w, [&] { climate::addDust(*w, 0.6); for (int y = 0; y < 60; y++) climate::yearly(*w); });         // a nuclear winter: -5.4 C
	eq(join(milestones(ev)), std::string("cooling -2,ice-age -4,thaw -1"), "a later, deeper winter is announced again");
	ev = capture(*w, [&] {
		climate::addDust(*w, 0.3); for (int y = 0; y < 3; y++) climate::yearly(*w);
		climate::addDust(*w, 0.3); for (int y = 0; y < 60; y++) climate::yearly(*w);
	});
	eq(join(milestones(ev)), std::string("cooling -2,ice-age -4,thaw -1"), "two blows in one winter: each line once");
	for (const Event& e : ev) if (const auto* m = e.as<EvClimateMilestone>()) ok(std::isfinite(m->value));
	eq(finite(*w), std::string(""));
}

// ------------------------------------------------------------------ robustness
DM_TEST("climate: robustness: odd and poisoned worlds never get NaN or out-of-range cells") {
	stub::Guard guard;
	hookVeg(0.4);
	Rng rng(20261118);
	const double nan = std::nan("");
	auto pickD = [&](std::initializer_list<double> l) { const std::vector<double> v(l); return v[size_t(rng.integer(0, int(v.size()) - 1))]; };
	const Phase phases[] = { Phase::Molten, Phase::Water, Phase::Life, Phase::People, Phase::History, Phase::Ended };
	int bad = 0;
	for (int i = 0; i < 40; i++) {
		auto w = state::create({ .seed = rng.next(), .L = rng.integer(0, 4) });
		watch(*w);
		const bool flat = rng.chance(0.3);
		const double base = rng.range(-12, 20);
		for (int c = 0; c < w->n; c++) {
			w->elev[size_t(c)] = float(flat ? base : rng.range(-12, 20));
			if (rng.chance(0.02)) w->elev[size_t(c)] = float(nan);
			w->ice[size_t(c)] = rng.chance(0.15) ? float(pickD({ nan, -1, 2, 0.5, 1 })) : 0.f;
			w->wet[size_t(c)] = rng.chance(0.15) ? float(pickD({ nan, 5, -1, 0.4 })) : 0.f;
		}
		w->seaLevel = pickD({ -12, -9, -3, 0, 0, 2, 19, 25, nan });
		w->phase = phases[rng.integer(0, 5)];
		if (rng.chance(0.5)) w->vars.seaBase = pickD({ 0, -4, 3, nan });
		if (rng.chance(0.3)) w->vars.iceRef = pickD({ 0, -1, nan, 1e-9, 500 });
		w->co2 = pickD({ 0, 50, nan, -5, 1e9 }); w->dust = pickD({ 0, 0.4, nan, 7, -2 }); w->globalTemp = pickD({ 0, 3, nan, -40, 1e6 });
		for (int k = rng.integer(0, 3); k > 0; k--) {
			const int cells[] = { 0, w->n - 1, -1, w->n, -7 };
			Disaster& d = state::newDisaster(*w, DisasterType::Drought, cells[rng.integer(0, 4)]);
			d.ang = pickD({ 0.2, 4, 0, -1, nan, kInf }); d.power = pickD({ 1, 0.5, nan, 9, -1 });
		}
		capture(*w, [&] {                                               // milestone payloads are checked by the event contract
			climate::update(*w); climate::stepIce(*w);
			for (int y = 0; y < 3; y++) climate::yearly(*w);
			climate::calibrate(*w); climate::stepIce(*w); climate::addDust(*w, rng.range(-1, 1)); climate::emit(*w, rng.range(-50, 50));
			for (int y = 0; y < 5; y++) climate::yearly(*w);
			climate::update(*w);
		});
		for (int c = 0; c < w->n; c++) {
			if (!std::isfinite(w->temp[size_t(c)]) || !(w->moist[size_t(c)] >= 0 && w->moist[size_t(c)] <= 1) || !(w->ice[size_t(c)] >= 0 && w->ice[size_t(c)] <= 1)) {
				if (bad++ < 5) fail("world " + std::to_string(i) + ": cell " + std::to_string(c) + " temp " + f2(w->temp[size_t(c)]) + " moist " + f2(w->moist[size_t(c)]) + " ice " + f2(w->ice[size_t(c)]));
			}
		}
		const bool hist = w->phase == Phase::History || w->phase == Phase::Ended;
		ok(std::isfinite(w->seaLevel) && std::isfinite(w->co2) && std::isfinite(w->dust), "world " + std::to_string(i) + ": scalars");
		if (hist) ok(std::isfinite(w->globalTemp), "world " + std::to_string(i) + ": globalTemp");
		ok(std::isfinite(climate::anomaly(*w)) && std::isfinite(climate::datum(*w)), "anomaly and datum are always finite");
		ok(w->co2 >= 0 && w->co2 <= 300 && w->dust >= 0 && w->dust <= 1, "co2 / dust in range");
		ok(w->vars.seaBase && std::isfinite(*w->vars.seaBase) && w->vars.iceRef && *w->vars.iceRef >= 50, "calibrated");
		ok(std::fabs(w->seaLevel - *w->vars.seaBase) <= 0.6 + 1e-9, "sea within its bounds");
		for (const auto& [k, v] : w->vars.num) ok(std::isfinite(v), "vars.num." + k);
	}
	eq(bad, 0, "bad cells");
}

DM_TEST("climate: robustness: a broken yearly tuning value cannot turn the sea level (or co2, dust, globalTemp, iceRef) into NaN") {
	stub::Guard guard;
	hookVeg(0.5);
	const ClimateTune keep;
	double ClimateTune::* const keys[] = { &ClimateTune::iceRefMin, &ClimateTune::lag, &ClimateTune::dustDecay, &ClimateTune::uptake,
		&ClimateTune::sens, &ClimateTune::seaRate, &ClimateTune::seaTempK, &ClimateTune::seaIceGain, &ClimateTune::seaIceLoss,
		&ClimateTune::seaLo, &ClimateTune::seaHi, &ClimateTune::co2Max, &ClimateTune::seaMoved, &ClimateTune::anomalyNeed,
		&ClimateTune::dustCool, &ClimateTune::vegDefault };
	const char* names[] = { "iceRefMin", "lag", "dustDecay", "uptake", "sens", "seaRate", "seaTempK", "seaIceGain", "seaIceLoss",
		"seaLo", "seaHi", "co2Max", "seaMoved", "anomalyNeed", "dustCool", "vegDefault" };
	for (size_t ki = 0; ki < std::size(keys); ki++) {
		for (double badV : { std::nan(""), 0.0, -1.0, kInf }) {
			std::vector<std::unique_ptr<World>> ws;
			ws.push_back(squareWorld(3, 0));                                // almost no land ice (iceRef on its floor)
			ws.push_back(helperWorld(3, 3));                                 // ice caps
			for (auto& w : ws) {
				w->tune<ClimateTune>().*keys[ki] = badV;
				w->vars = Vars{}; w->co2 = 90; w->dust = 0.3; w->globalTemp = 1;
				const std::string tag = std::string(names[ki]) + " = " + f2(badV);
				capture(*w, [&] {
					climate::calibrate(*w);
					for (int y = 1; y <= 12; y++) { climate::emit(*w, 1); if (y == 6) std::fill(w->ice.begin(), w->ice.end(), 1.f); climate::yearly(*w); }
					climate::update(*w); climate::stepIce(*w);
				});
				ok(std::isfinite(w->seaLevel) && std::isfinite(w->co2) && std::isfinite(w->dust) && std::isfinite(w->globalTemp), tag + ": scalars finite");
				ok(w->vars.iceRef && std::isfinite(*w->vars.iceRef) && *w->vars.iceRef > 0, tag + ": iceRef");
				ok(std::fabs(w->seaLevel - *w->vars.seaBase) <= 12 * keep.seaRate + 1e-9 || names[ki] == std::string("seaRate"), tag + ": the sea still creeps: " + f2(w->seaLevel));
				eq(finite(*w), std::string(""), tag);
			}
		}
	}
	auto w = squareWorld(3, 0); climate::calibrate(*w);
	eq(*w->vars.iceRef, 50.0, "a fresh world has the default tuning");
}

DM_TEST("climate: the wettest places approach 1 smoothly; every grid size gets the same climate") {
	std::vector<std::vector<double>> stats;
	for (int L : { 4, 5 }) {
		auto w = helperWorld(L, 2);
		climate::calibrate(*w);
		const World& W = *w;
		const int land = count(W, [&](int c) { return isLand(W, c) && W.ice[size_t(c)] < 0.5; });
		const int top = count(W, [&](int c) { return isLand(W, c) && W.ice[size_t(c)] < 0.5 && W.moist[size_t(c)] >= 0.995; });
		ok(top < 0.04 * land, "L" + std::to_string(L) + ": few cells sit on the ceiling: " + std::to_string(top) + " of " + std::to_string(land));
		auto b = [&](double a0, double a1) { return mean(W, W.moist, [&](int c) { return isLand(W, c) && alat(W, c) >= a0 && alat(W, c) < a1; }); };
		stats.push_back({ b(0, 12), b(12, 24), b(24, 36), b(36, 54), mean(W, W.temp, [&](int c) { return isLand(W, c); }) });
	}
	say("land moist by band (0-12, 12-24, 24-36, 36-54) and mean land temp: L4 " + f2(stats[0][0], 2) + " " + f2(stats[0][1], 2) + " " + f2(stats[0][2], 2) + " " +
		f2(stats[0][3], 2) + " " + f2(stats[0][4], 1) + " | L5 " + f2(stats[1][0], 2) + " " + f2(stats[1][1], 2) + " " + f2(stats[1][2], 2) + " " + f2(stats[1][3], 2) + " " + f2(stats[1][4], 1));
	for (size_t i = 0; i < 4; i++) near(stats[0][i], stats[1][i], 0.1, "band " + std::to_string(i) + " reads the same at L = 4 and L = 5");
	near(stats[0][4], stats[1][4], 1.5, "so does the temperature");
}

DM_TEST("climate: retuning the winds rebuilds the wind table; other tuning is read at call time") {
	auto w = squareWorld(4, 0);
	climate::calibrate(*w);
	ClimateTune& T = w->tune<ClimateTune>();
	const ClimateTune keep = T;
	const std::vector<float> m0 = w->moist;
	const int builds = w->scratch<ClimateScratch>().gridBuilds;
	T.rainRef = keep.rainRef * 2; climate::update(*w);
	eq(w->scratch<ClimateScratch>().gridBuilds, builds, "same winds");
	const int c = findLand(*w, [&](int k) { return m0[size_t(k)] > 0.2 && m0[size_t(k)] < 0.6; });
	if (ok(c >= 0)) near(w->moist[size_t(c)], m0[size_t(c)] / 2, 0.02, "RAIN_REF is read at call time");
	T.rainRef = keep.rainRef;
	T.merid = 0; climate::update(*w);
	eq(w->scratch<ClimateScratch>().gridBuilds, builds + 1, "new wind table");
	T.merid = keep.merid; climate::update(*w);
	ok(w->moist == m0, "and back");
}

// ------------------------------------------------------------------ the whole genesis story
DM_TEST("climate: genesis to history: comets, ice, calibration, then centuries with a steady sea") {
	stub::Guard guard;
	hookVeg(0.6);
	auto w = helperWorld(5, 2, -9, Phase::Water);
	w->vars = Vars{};
	climate::update(*w);
	for (int i = 0; i < 6; i++) climate::stepIce(*w);
	eq(count(*w, [&](int c) { return w->ice[size_t(c)] > 0; }), 0, "dry rock: no ice yet");
	double sea = -6;
	while (sea < 0) {                                                  // each comet raises the sea; the scheduler updates and steps the ice
		w->seaLevel = sea; w->need.climate = true;
		climate::update(*w); climate::stepIce(*w); climate::stepIce(*w);
		climate::yearly(*w);                                           // Sim does not call it in genesis; if anyone does, the sea is safe
		eq(w->seaLevel, sea, "yearly leaves the genesis sea alone");
		eq(climate::datum(*w), 0.0);
		sea = std::min(0.0, sea + std::max(0.12, std::min(1.0, 0.2 * (0.6 - sea))));
	}
	w->seaLevel = 0; climate::update(*w);
	w->phase = Phase::Life; for (int i = 0; i < 12; i++) climate::stepIce(*w);
	const int landCells = count(*w, [&](int c) { return isLand(*w, c); });
	const int iced = count(*w, [&](int c) { return isLand(*w, c) && w->ice[size_t(c)] > 0.5; });
	say("end of genesis: iced land " + f2(100.0 * iced / landCells, 1) + "%, mean land temp " + f2(mean(*w, w->temp, [&](int c) { return isLand(*w, c); }), 1));
	ok(iced > 0 && iced < 0.3 * landCells, "ice caps, not an ice ball: " + std::to_string(iced) + " of " + std::to_string(landCells));
	ok(!w->vars.seaBase, "no seaBase before history");
	w->phase = Phase::History;
	climate::calibrate(*w);                                            // Sim::setPhase(History)
	ok(w->vars.seaBase && *w->vars.seaBase == 0, "seaBase 0"); ok(w->vars.iceRef && *w->vars.iceRef >= 50, "iceRef");
	const double iceAtStart = climate::landIce(*w);
	runYears(*w, 300);
	say("300 undisturbed years: sea " + f2(w->seaLevel, 6) + ", land ice " + f2(iceAtStart, 0) + " -> " + f2(climate::landIce(*w), 0));
	eq(w->seaLevel, 0.0, "the sea neither drains nor floods");
	eq(climate::landIce(*w), iceAtStart, "the ice caps hold exactly");
	eq(finite(*w), std::string(""));
}

// ------------------------------------------------------------------ cost
DM_TEST("climate: cost at L = 6: update within 12 ms (asserted at 5x for a shared machine)") {
	stub::Guard guard;
	hookVeg(0.6);
	auto w = helperWorld(6, 1);
	double t = nowMs();
	climate::calibrate(*w);
	const double first = nowMs() - t;
	std::vector<double> ts;
	for (int i = 0; i < 11; i++) { t = nowMs(); climate::update(*w); ts.push_back(nowMs() - t); }
	std::sort(ts.begin(), ts.end());
	t = nowMs();
	for (int i = 0; i < 20; i++) climate::stepIce(*w);
	const double ice = (nowMs() - t) / 20;
	t = nowMs();
	for (int i = 0; i < 50; i++) climate::yearly(*w);
	const double yr = (nowMs() - t) / 50;
	std::printf("    climate timing at L = 6: update median %.2f ms (min %.2f, max %.2f), first calibrate %.1f ms (builds the wind table), stepIce %.3f ms, yearly %.3f ms\n",
		ts[5], ts[0], ts[10], first, ice, yr);
	ok(ts[5] < 60, "update median " + f2(ts[5]) + " ms");
	ok(ice < 5 && yr < 5);
	eq(finite(*w), std::string(""));
}

// ------------------------------------------------------------------ maps (CLIMATE_MAPS=1)
// shots/ue-climate-*.png: temperature, rainfall (sea dimmed), ice and elevation for helper seeds 1 and 2 at L = 6, the
// same world 4.6 C warmer, the hand-built continent with and without its mountain wall. North is up; longitude -180 is at
// the left; geo.basis east is the direction of DEcreasing longitude, so the trades (from the east) arrive from the LEFT.
DM_TEST("climate: maps") {
	if (!std::getenv("CLIMATE_MAPS")) return;
	stub::Guard guard;
	hookVeg(0.6);
	namespace mi = dm::mapimg;
	auto bands = [](const World& w) {
		double m[9] = {}, t[9] = {}; int k[9] = {};
		for (int c = 0; c < w.n; c++) if (isLand(w, c)) { const int b = std::min(8, int(alat(w, c) / 10)); m[b] += w.moist[size_t(c)]; t[b] += w.temp[size_t(c)]; k[b]++; }
		std::string s = "land moist by 10-degree band";
		for (int i = 0; i < 9; i++) s += " " + (k[i] ? f2(m[i] / k[i], 2) : std::string("--"));
		s += " | temp";
		for (int i = 0; i < 9; i++) s += " " + (k[i] ? f2(t[i] / k[i], 0) : std::string("--"));
		return s;
	};
	auto iceMap = [](const World& w, const std::string& file) {
		return mi::custom(w, file, [&](int c) {
			const double i = w.ice[size_t(c)];
			return isLand(w, c) ? mi::Rgb{ 90 + 165 * i, 120 + 135 * i, 80 + 175 * i } : mi::Rgb{ 20 + 190 * i, 50 + 180 * i, 110 + 135 * i };
		});
	};
	for (uint32_t seed : { 1u, 2u }) {
		auto w = helperWorld(6, seed);
		climate::calibrate(*w);
		const std::string s = std::to_string(seed);
		std::printf("    helper seed %u: %s\n", seed, bands(*w).c_str());
		ok(mi::elev(*w, shot("ue-climate-elev-seed" + s + ".png")));
		ok(mi::field(*w, "temp", shot("ue-climate-temp-seed" + s + ".png"), -30, 32));
		ok(mi::field(*w, "moist", shot("ue-climate-moist-seed" + s + ".png"), 0, 1, { .dimSea = true }));
		ok(iceMap(*w, shot("ue-climate-ice-seed" + s + ".png")));
		if (seed == 1) {                                               // 800 years with co2 held at 192
			for (int y = 1; y <= 800; y++) { w->co2 = 192; climate::yearly(*w); if (w->need.climate && y % 2 == 0) climate::update(*w); if (y % 5 == 0) climate::stepIce(*w); }
			climate::update(*w);
			std::printf("    the same world at anomaly %.2f: sea %.3f, land ice x %.3f; %s\n", climate::anomaly(*w), w->seaLevel,
				climate::landIce(*w) / *w->vars.iceRef, bands(*w).c_str());
			ok(iceMap(*w, shot("ue-climate-ice-warm-seed1.png")));
		}
	}
	for (double wall : { 5.0, 0.0 }) {
		auto w = squareWorld(6, wall);
		climate::calibrate(*w);
		const std::string name = wall != 0 ? "wall" : "flat";
		std::printf("    %s continent: %s\n", name.c_str(), bands(*w).c_str());
		ok(mi::field(*w, "moist", shot("ue-climate-" + name + "-moist.png"), 0, 1, { .dimSea = true }));
		if (wall != 0) ok(mi::elev(*w, shot("ue-climate-wall-elev.png")));
	}
}
