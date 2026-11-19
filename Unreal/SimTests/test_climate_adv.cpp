// DEMIURGE — SimTests/test_climate_adv.cpp : independent adversarial verification of dm::climate (A3).
// Written by the A3 verifier (not the builder). run.ps1 climate (CLIMATE_REPORT=1 prints the measured numbers;
// CLIMATE_MAPS=1 also renders shots/ue-climate-adv-*.png).
// What it attacks that test_climate.cpp does not: ownership (every API writes only what the writers table allows),
// hemisphere / zonal symmetry of the wind and rain model, rain shadows at L = 6 in the southern hemisphere at
// another longitude (sides defined by geo basis vectors, not the lon sign convention), far-side invariance at L = 6,
// every double of ClimateTune poisoned, L = 0..7, save / load determinism, every phase for stepIce, milestone
// once-ness over long runs, datum edge cases, droughts of other disaster types, performance at L = 6.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <utility>
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

constexpr double kDg = 180.0 / dm::kPi;
using CoverAdv = decltype(dm::life::cover(std::declval<const dm::World&>()));

bool advReport() { static const bool on = std::getenv("CLIMATE_REPORT") != nullptr; return on; }
void tell(const std::string& s) { if (advReport()) std::printf("    : %s\n", s.c_str()); }
std::string fx(double v, int prec = 3) { char b[64]; std::snprintf(b, sizeof b, "%.*f", prec, v); return b; }

bool hookCover(double veg) {
	if (!stub::stubbed("life")) return false;
	stub::hooks().life_cover = [veg](const World&) { CoverAdv cv{}; cv.land = 0.4; cv.veg = veg; cv.forest = 0.3; return cv; };
	return true;
}
bool landC(const World& w, int c) { return double(w.elev[size_t(c)]) >= w.seaLevel; }
double latD(const World& w, int c) { return w.geo->lat[size_t(c)] * kDg; }
Vec3 posOf(const World& w, int c) { const size_t u = size_t(c); return { w.geo->pos[3 * u], w.geo->pos[3 * u + 1], w.geo->pos[3 * u + 2] }; }
int nearestCell(const World& w, double latDeg, double lonDeg) {
	const double la = latDeg / kDg, lo = lonDeg / kDg;
	return w.geo->cellAt(std::cos(la) * std::cos(lo), std::sin(la), std::cos(la) * std::sin(lo));
}
std::unique_ptr<World> blank(int L, uint32_t seed = 7) {
	auto w = state::create({ .seed = seed, .L = L, .geo = sharedGeo(L) });
	watch(*w);
	w->heat = 0; w->phase = Phase::History; w->seaLevel = 0;
	return w;
}
std::string allFinite(const World& w) {
	std::string bad = checkFinite(w);
	if (!bad.empty()) return bad;
	for (int c = 0; c < w.n; c++) {
		const size_t u = size_t(c);
		if (!std::isfinite(w.temp[u])) return "temp[" + std::to_string(c) + "]";
		if (!(w.moist[u] >= 0 && w.moist[u] <= 1)) return "moist[" + std::to_string(c) + "] = " + fx(w.moist[u]);
		if (!(w.ice[u] >= 0 && w.ice[u] <= 1)) return "ice[" + std::to_string(c) + "] = " + fx(w.ice[u]);
	}
	if (!std::isfinite(w.seaLevel) || !std::isfinite(w.co2) || !std::isfinite(w.dust) || !std::isfinite(w.globalTemp)) return "scalar";
	if (w.vars.seaBase && !std::isfinite(*w.vars.seaBase)) return "seaBase";
	if (w.vars.iceRef && !std::isfinite(*w.vars.iceRef)) return "iceRef";
	for (const auto& [k, v] : w.vars.num) if (!std::isfinite(v)) return "vars.num." + k;
	return "";
}
template <class P>
double meanOf(const World& w, const std::vector<float>& a, P pred, int* cnt = nullptr) {
	double s = 0; int k = 0;
	for (int c = 0; c < w.n; c++) if (pred(c)) { s += a[size_t(c)]; k++; }
	if (cnt) *cnt = k;
	return k ? s / k : 0;
}

// ------------------------------------------------------------------ ownership harness
// Two identical worlds; the op runs on A; the state the writers table gives Climate is copied from A to B; then every
// cell array (derived ones included), the persisted hash and the flags must agree.
struct Allowed {
	bool temp = false, moist = false, ice = false, co2 = false, dust = false, globalTemp = false, seaLevel = false;
	bool seaBase = false, iceRef = false, num = false;
	bool needClimateClear = false;
};
std::unique_ptr<World> ownWorld(Phase ph) {
	auto w = makeWorld({ .L = 4, .seed = 11, .land = 0.4, .basics = true, .sea = 0, .phase = ph });
	Culture& cu = state::newCulture(*w);
	const int cid = cu.id;
	const int cell = bestHab(*w);
	Settlement& s = state::newSettlement(*w, cell, cid);
	s.pop = 500; s.tier = 1;
	w->owner[size_t(cell)] = int16_t(s.id);
	Settlement& dead = state::newSettlement(*w, findLand(*w), cid);
	dead.dead = true;
	Disaster& d = state::newDisaster(*w, DisasterType::Drought, cell);
	d.ang = 0.3; d.power = 0.8; d.dur = 10;
	Disaster& st = state::newDisaster(*w, DisasterType::Storm, findLand(*w));
	st.ang = 0.3; st.power = 1; st.dur = 2;
	w->co2 = 40; w->dust = 0.2; w->globalTemp = 0.7;
	w->vars.num["civ_other"] = 3;                                      // another module's key: never touched
	w->vars.str["climate_none"] = "x";
	return w;
}
void copyAllowed(const World& a, World& b, const Allowed& al) {
	if (al.temp) b.temp = a.temp;
	if (al.moist) b.moist = a.moist;
	if (al.ice) b.ice = a.ice;
	if (al.co2) b.co2 = a.co2;
	if (al.dust) b.dust = a.dust;
	if (al.globalTemp) b.globalTemp = a.globalTemp;
	if (al.seaLevel) b.seaLevel = a.seaLevel;
	if (al.seaBase) b.vars.seaBase = a.vars.seaBase;
	if (al.iceRef) b.vars.iceRef = a.vars.iceRef;
	if (al.num) for (const auto& [k, v] : a.vars.num) if (k.rfind("climate_", 0) == 0) b.vars.num[k] = v;
}
template <class Op>
void checkOwnership(const std::string& tag, Phase ph, const Allowed& al, Op op, bool calibrateFirst) {
	auto a = ownWorld(ph), b = ownWorld(ph);
	if (calibrateFirst) { climate::calibrate(*a); climate::calibrate(*b); }
	const Need na0 = a->need; const Dirty da0 = a->dirty;
	std::vector<Event> ev = capture(*a, [&] { op(*a); });
	for (const Event& e : ev) ok(e.type == Ev::ClimateMilestone, tag + ": emitted " + e.name());
	copyAllowed(*a, *b, al);
	for (const auto& [k, v] : a->vars.num) if (k.rfind("climate_", 0) != 0) ok(b->vars.num.count(k) && b->vars.num.at(k) == v, tag + ": vars.num." + k);
	for (int i = 0; i < state::kCellFieldCount; i++) {
		const CellArray x = state::cellArray(*a, i), y = state::cellArray(*b, i);
		int diff = 0;
		for (size_t c = 0; c < x.size; c++) {
			const double p = x.get(c), q = y.get(c);
			if (!(p == q) && !(std::isnan(p) && std::isnan(q))) diff++;
		}
		ok(diff == 0, tag + ": wrote " + state::kCellFields[i].name + " (" + std::to_string(diff) + " cells)");
	}
	eq(hash(*a), hash(*b), tag + ": persisted state outside Climate's share unchanged (entities, rng, vars, scalars)");
	// flags: dirty only ever raised; need.climate may be cleared by update / calibrate only
	const bool* d0 = &da0.elev; const bool* d1 = &a->dirty.elev;
	for (int i = 0; i < 6; i++) ok(!d0[i] || d1[i], tag + ": a dirty flag was cleared");
	if (!al.needClimateClear) ok(!na0.climate || a->need.climate, tag + ": need.climate cleared");
	ok(!na0.derive || a->need.derive, tag + ": need.derive cleared");
	ok(!na0.hydro || a->need.hydro, tag + ": need.hydro cleared");
	ok(!na0.life || a->need.life, tag + ": need.life cleared");
	ok(a->dirty.elev == da0.elev && a->dirty.rivers == da0.rivers && a->dirty.roads == da0.roads, tag + ": dirty.elev / rivers / roads are not Climate's");
}

} // namespace

// ------------------------------------------------------------------ ownership
DM_TEST("climate adv: ownership: every entry point writes only its share of the writers table") {
	stub::Guard guard;
	hookCover(0.5);
	Allowed up; up.temp = up.moist = up.ice = true; up.needClimateClear = true;
	checkOwnership("update", Phase::History, up, [&](World& w) { climate::update(w); }, false);
	checkOwnership("update(calibrated)", Phase::Water, up, [&](World& w) { climate::update(w); }, true);
	Allowed si; si.temp = si.ice = true;
	checkOwnership("stepIce", Phase::History, si, [&](World& w) { climate::stepIce(w); }, true);
	checkOwnership("stepIce(cold)", Phase::Life, si, [&](World& w) { climate::stepIce(w); }, false);
	Allowed yg; yg.dust = true;
	Allowed ygr = yg; ygr.seaBase = true;                            // (copies the test's own reset of seaBase)
	checkOwnership("yearly(genesis)", Phase::People, ygr, [&](World& w) { w.vars.seaBase.reset(); for (int i = 0; i < 30; i++) climate::yearly(w); }, false);
	checkOwnership("yearly(genesis, seaBase set)", Phase::Water, yg, [&](World& w) { for (int i = 0; i < 30; i++) climate::yearly(w); }, false);
	checkOwnership("yearly(history, no seaBase)", Phase::History, ygr, [&](World& w) { w.vars.seaBase.reset(); for (int i = 0; i < 30; i++) climate::yearly(w); }, false);
	Allowed yh; yh.dust = yh.co2 = yh.globalTemp = yh.seaLevel = yh.num = true;
	checkOwnership("yearly(history)", Phase::History, yh, [&](World& w) {
		for (int i = 0; i < 400; i++) { w.co2 = 250; climate::yearly(w); }
	}, true);
	// stepIce inside the loop is allowed to move ice / temp: let the copy include them
	Allowed yh2 = yh; yh2.ice = yh2.temp = true;
	checkOwnership("yearly+stepIce(ended)", Phase::Ended, yh2, [&](World& w) {
		for (int i = 0; i < 400; i++) { climate::emit(w, 1.5); climate::yearly(w); if (i % 5 == 0) climate::stepIce(w); }
	}, true);
	Allowed yhand = yh; yhand.ice = yhand.temp = yhand.iceRef = yhand.seaBase = true;
	checkOwnership("yearly(hand-calibrated)", Phase::History, yhand, [&](World& w) { w.vars.seaBase = 0.0; climate::yearly(w); }, false);
	Allowed ca; ca.temp = ca.moist = ca.ice = ca.seaBase = ca.iceRef = ca.num = true; ca.needClimateClear = true;
	checkOwnership("calibrate", Phase::History, ca, [&](World& w) { climate::calibrate(w); }, false);
	Allowed em; em.co2 = true;
	checkOwnership("emit", Phase::History, em, [&](World& w) { climate::emit(w, 12); climate::emit(w, -3); }, false);
	Allowed ad; ad.dust = true;
	checkOwnership("addDust", Phase::History, ad, [&](World& w) { climate::addDust(w, 0.3); }, false);
	Allowed none;
	checkOwnership("reads", Phase::History, none, [&](World& w) {
		const World& cw = w; volatile double s = climate::datum(cw) + climate::anomaly(cw) + climate::landIce(cw); (void)s;
	}, true);
	// addDust raises need.climate; emit does not
	auto w = ownWorld(Phase::History);
	w->need.climate = false; climate::emit(*w, 5); eq(w->need.climate, false, "emit leaves need.climate");
	climate::addDust(*w, 0.01); eq(w->need.climate, true, "addDust sets need.climate");
}

// ------------------------------------------------------------------ symmetry of the model
DM_TEST("climate adv: hemispheres mirror each other and a zonal world is zonal (noise off)") {
	for (int L : { 5, 6 }) {
		for (int kind = 0; kind < 2; kind++) {
			auto w = blank(L);
			ClimateTune& T = w->tune<ClimateTune>();
			T.tNoise = 0; T.rainNoise = 0;
			for (int c = 0; c < w->n; c++) {
				const double a = std::fabs(latD(*w, c));
				w->elev[size_t(c)] = float(kind == 1 && a > 12 && a < 42 ? 0.3 : -4.0);   // all sea, or a land ring each side
			}
			climate::calibrate(*w);
			const std::string tag = "L" + std::to_string(L) + (kind ? " land rings" : " all sea");
			double worst = 0, worstT = 0, worstSpread = 0; int worstBand = -1;
			for (int b = 0; b < 18; b++) {
				const double a0 = b * 5.0, a1 = a0 + 5;
				auto inN = [&](int c) { const double la = latD(*w, c); return la >= a0 && la < a1; };
				auto inS = [&](int c) { const double la = -latD(*w, c); return la >= a0 && la < a1; };
				int kn = 0, ks = 0;
				const double mn = meanOf(*w, w->moist, inN, &kn), ms = meanOf(*w, w->moist, inS, &ks);
				const double tn = meanOf(*w, w->temp, inN), ts = meanOf(*w, w->temp, inS);
				if (kn < 3 || ks < 3) continue;
				if (std::fabs(mn - ms) > worst) { worst = std::fabs(mn - ms); worstBand = b; }
				worstT = std::max(worstT, std::fabs(tn - ts));
				// zonal: the spread among cells of (nearly) the same latitude, 1-degree bins (ring edges excluded)
				for (int sub = 0; sub < 5; sub++) {
					const double b0 = a0 + sub, b1 = b0 + 1;
					if (kind == 1 && ((b1 > 10 && b0 < 14) || (b1 > 40 && b0 < 44))) continue;
					auto inB = [&](int c) { const double la = latD(*w, c); return la >= b0 && la < b1; };
					int k2 = 0;
					const double mb = meanOf(*w, w->moist, inB, &k2);
					if (k2 < 5) continue;
					double s2 = 0;
					for (int c = 0; c < w->n; c++) if (inB(c)) { const double d = w->moist[size_t(c)] - mb; s2 += d * d; }
					worstSpread = std::max(worstSpread, std::sqrt(s2 / k2));
				}
			}
			tell(tag + ": worst N-S band moist difference " + fx(worst) + " (band " + std::to_string(worstBand * 5) + "), temp " + fx(worstT, 2) +
				", worst within-band sd " + fx(worstSpread));
			ok(worst < 0.04, tag + ": north and south moist agree band by band: " + fx(worst));
			ok(worstT < 0.3, tag + ": north and south temp agree: " + fx(worstT));
			ok(worstSpread < 0.035, tag + ": moist is the same all round a latitude band: sd " + fx(worstSpread));
			eq(allFinite(*w), std::string(""), tag);
			if (std::getenv("CLIMATE_MAPS") && L == 6) ok(mapimg::field(*w, "moist", shot(std::string("ue-climate-adv-zonal-") + (kind ? "rings" : "sea") + ".png"), 0, 1));
		}
	}
}

// ------------------------------------------------------------------ rain shadows, southern hemisphere, L = 6, sides by basis vectors
DM_TEST("climate adv: L6 southern continent at lon 115: trades wet its east side, westerlies its west side; a wall shadows both") {
	auto mk = [](double wallH) {
		auto w = blank(6, 21);
		const int c0 = nearestCell(*w, -32, 115);
		const Vec3 n0 = posOf(*w, c0);
		Vec3 E, N; v3::basis(n0, E, N);
		const double cosR = std::cos(30 / kDg), wallS = std::sin(6 / kDg);
		for (int c = 0; c < w->n; c++) {
			const Vec3 p = posOf(*w, c);
			// signed distance from the meridian plane through the centre: + is geo-east of the wall
			const Vec3 axis = v3::norm(v3::cross(Vec3{ 0, 1, 0 }, n0));    // = east at the centre (FOUNDATION 3.3 basis)
			const double s = v3::dot(p, axis);
			double e = -4;
			if (v3::dot(p, n0) > cosR) { e = 0.3; if (std::fabs(s) < wallS) e += wallH * (1 - (s / wallS) * (s / wallS)); }
			w->elev[size_t(c)] = float(e);
		}
		(void)E; (void)N;
		climate::calibrate(*w);
		return w;
	};
	auto side = [](const World& w, double la0, double la1, int sgn) {
		const int c0 = nearestCell(w, -32, 115);
		const Vec3 n0 = posOf(w, c0);
		const Vec3 axis = v3::norm(v3::cross(Vec3{ 0, 1, 0 }, n0));
		const double s0 = std::sin(8 / kDg), s1 = std::sin(20 / kDg);
		return meanOf(w, w.moist, [&](int c) {
			const double la = latD(w, c);
			const double s = v3::dot(posOf(w, c), axis) * sgn;
			return landC(w, c) && la >= la0 && la <= la1 && s > s0 && s < s1;
		});
	};
	auto wall = mk(5), flat = mk(0);
	// trades (from the east): windward = east (+), lee = west (-). Westerlies: the reverse.
	const double tWind = side(*wall, -24, -8, +1), tLee = side(*wall, -24, -8, -1), tLeeFlat = side(*flat, -24, -8, -1);
	const double wWind = side(*wall, -56, -40, -1), wLee = side(*wall, -56, -40, +1), wLeeFlat = side(*flat, -56, -40, +1);
	tell("south trades: windward(east) " + fx(tWind) + " lee " + fx(tLee) + " lee without wall " + fx(tLeeFlat));
	tell("south westerlies: windward(west) " + fx(wWind) + " lee " + fx(wLee) + " lee without wall " + fx(wLeeFlat));
	ok(tWind > 1.5 * tLee, "trades: windward " + fx(tWind) + " vs lee " + fx(tLee));
	ok(wWind > 1.5 * wLee, "westerlies: windward " + fx(wWind) + " vs lee " + fx(wLee));
	ok(tLee < 0.85 * tLeeFlat && wLee < 0.85 * wLeeFlat, "the wall casts the shadow (lee drier than without it)");
	// on the flat continent the coast the wind comes from is the wetter one
	const double fE = side(*flat, -24, -8, +1), fW = side(*flat, -24, -8, -1);
	ok(fE > fW, "flat continent, trades: east side wetter " + fx(fE) + " vs " + fx(fW));
	eq(allFinite(*wall), std::string(""));
	if (std::getenv("CLIMATE_MAPS")) {
		ok(mapimg::field(*wall, "moist", shot("ue-climate-adv-south-wall-moist.png"), 0, 1, { .dimSea = true }));
		ok(mapimg::elev(*wall, shot("ue-climate-adv-south-wall-elev.png")));
	}
}

// ------------------------------------------------------------------ far side, L = 6
DM_TEST("climate adv: L6: a new 4-high mountain changes nothing beyond 1.6 rad and something near it") {
	for (uint32_t seed : { 2u, 5u }) {
		auto w = makeWorld({ .L = 6, .seed = seed, .land = 0.4, .basics = false });
		climate::calibrate(*w);
		const std::vector<float> m0 = w->moist, t0 = w->temp;
		const int c = findLand(*w, [&](int k) { return std::fabs(latD(*w, k)) > 38 && std::fabs(latD(*w, k)) < 52 && w->elev[size_t(k)] < 1; });
		if (!ok(c >= 0, "mountain site")) return;
		std::vector<int32_t> cells;
		w->geo->within(c, 0.08, cells);
		for (int k : cells) if (landC(*w, k)) w->elev[size_t(k)] += 4.0f * float(1 - w->geo->angle(c, k) / 0.08);
		climate::update(*w);
		double nearMax = 0, farMax = 0, farT = 0; int farN = 0;
		for (int k = 0; k < w->n; k++) {
			const double a = w->geo->angle(c, k);
			const double dm = std::fabs(w->moist[size_t(k)] - m0[size_t(k)]);
			if (a < 0.5) nearMax = std::max(nearMax, dm);
			if (a > 1.6) { farMax = std::max(farMax, dm); farT = std::max(farT, double(std::fabs(w->temp[size_t(k)] - t0[size_t(k)]))); farN++; }
		}
		tell("seed " + std::to_string(seed) + ": near change " + fx(nearMax) + ", far change " + fx(farMax, 7) + " moist, " + fx(farT, 7) + " temp over " + std::to_string(farN) + " cells");
		ok(nearMax > 0.05, "the mountain does something nearby: " + fx(nearMax));
		ok(farMax < 1e-4, "nothing on the far side: " + fx(farMax, 7));
		ok(farT < 1e-4, "far temperatures the same (float rounding only)");
	}
}

// ------------------------------------------------------------------ every tuning double poisoned
DM_TEST("climate adv: every ClimateTune double set to NaN / +-inf / 0 / -1 / 1e9 keeps every cell finite and in range") {
	stub::Guard guard;
	hookCover(0.5);
	static_assert(sizeof(ClimateTune) % sizeof(double) == 0, "ClimateTune is all doubles");
	const int nd = int(sizeof(ClimateTune) / sizeof(double));
	const double inf = std::numeric_limits<double>::infinity();
	const double bads[] = { std::nan(""), inf, -inf, 0.0, -1.0, 1e9 };
	int fails = 0, runs = 0;
	std::set<int> badIdx;
	for (int i = 0; i < nd; i++) {
		for (double bv : bads) {
			auto w = makeWorld({ .L = 3, .seed = 4, .land = 0.4, .basics = false });
			std::fill(w->wet.begin(), w->wet.begin() + 40, 0.3f);
			Disaster& d = state::newDisaster(*w, DisasterType::Drought, 5); d.ang = 0.6; d.power = 1;
			ClimateTune& T = w->tune<ClimateTune>();
			reinterpret_cast<double*>(&T)[i] = bv;
			climate::update(*w); climate::stepIce(*w); climate::calibrate(*w);
			for (int y = 0; y < 30; y++) { climate::emit(*w, 2); if (y == 3) climate::addDust(*w, 0.4); climate::yearly(*w); if (y % 5 == 0) climate::stepIce(*w); }
			climate::update(*w);
			runs++;
			const std::string bad = allFinite(*w);
			const bool dOk = std::isfinite(climate::datum(*w)) && std::isfinite(climate::anomaly(*w)) && std::isfinite(climate::landIce(*w));
			if (!bad.empty() || !dOk) { if (fails++ < 8) fail("tune double #" + std::to_string(i) + " = " + fx(bv) + ": " + bad); badIdx.insert(i); }
		}
	}
	tell(std::to_string(runs) + " poisoned runs over " + std::to_string(nd) + " tuning doubles, " + std::to_string(fails) + " broke");
	eq(fails, 0, "poisoned tuning runs that wrote NaN or out-of-range cells");
}

// ------------------------------------------------------------------ grid sizes
DM_TEST("climate adv: L = 0..7: every API at every level, no NaN, poles colder than the equator from L = 2") {
	stub::Guard guard;
	hookCover(0.5);
	for (int L = 0; L <= 7; L++) {
		auto w = blank(L, 3);
		for (int c = 0; c < w->n; c++) { const Vec3 p = posOf(*w, c); w->elev[size_t(c)] = float(3 * fbm3(p.x * 2, p.y * 2, p.z * 2, 3) - 0.3); }
		const double t = nowMs();
		climate::calibrate(*w);
		const double tc = nowMs() - t;
		for (int y = 0; y < 20; y++) { climate::emit(*w, 3); climate::yearly(*w); climate::stepIce(*w); }
		climate::update(*w);
		const std::string bad = allFinite(*w);
		ok(bad.empty(), "L" + std::to_string(L) + ": " + bad);
		if (L >= 2) {
			double eqT = 0, poT = 0; int ke = 0, kp = 0;
			for (int c = 0; c < w->n; c++) { const double a = std::fabs(latD(*w, c)); if (a < 20) { eqT += w->temp[size_t(c)]; ke++; } if (a > 60) { poT += w->temp[size_t(c)]; kp++; } }
			if (ke && kp) ok(eqT / ke > poT / kp + 15, "L" + std::to_string(L) + ": equator warmer");
		}
		tell("L" + std::to_string(L) + " n " + std::to_string(w->n) + ": calibrate " + fx(tc, 2) + " ms");
	}
}

// ------------------------------------------------------------------ determinism, save / load
DM_TEST("climate adv: determinism: update is a pure function of the world; loaded twins stay identical; save/load drift is tiny") {
	stub::Guard guard;
	hookCover(0.55);
	auto a = makeWorld({ .L = 5, .seed = 8, .land = 0.4, .basics = false }), b = makeWorld({ .L = 5, .seed = 8, .land = 0.4, .basics = false });
	climate::calibrate(*a); climate::calibrate(*b);
	// a: extra updates with a rain miracle in between and a tuning detour that rebuilds the wind table
	for (int i = 0; i < 3; i++) { std::fill(a->wet.begin(), a->wet.end(), 0.4f); climate::update(*a); }
	std::fill(a->wet.begin(), a->wet.end(), 0.0f);
	a->tune<ClimateTune>().merid = 0.1; climate::update(*a); a->tune<ClimateTune>().merid = ClimateTune{}.merid;
	climate::update(*a); climate::update(*b);
	ok(a->moist == b->moist && a->temp == b->temp, "same world -> same climate, whatever ran before");
	// save / load
	std::vector<uint8_t> bytes;
	if (!ok(state::serialize(*a, bytes), "serialize")) return;
	auto l1 = state::deserialize(bytes), l2 = state::deserialize(bytes);
	if (!ok(l1 && l2, "deserialize")) return;
	watch(*l1); watch(*l2);
	auto run = [](World& w) {
		for (int y = 1; y <= 300; y++) {
			climate::emit(w, 1.2); if (y == 50) climate::addDust(w, 0.3);
			climate::yearly(w);
			if (w.need.climate && y % 2 == 0) climate::update(w);
			if (y % 5 == 0) climate::stepIce(w);
		}
		climate::update(w);
	};
	climate::update(*l1); climate::update(*l2);                       // what Sim::use / rederive does after a load
	run(*a); run(*l1); run(*l2);
	eq(hash(*l1), hash(*l2), "two loads of one save, driven alike, are identical");
	ok(l1->moist == l2->moist && l1->temp == l2->temp, "and so are their derived fields");
	const double dSea = std::fabs(a->seaLevel - l1->seaLevel), dT = std::fabs(a->globalTemp - l1->globalTemp);
	tell("after 300 years: original vs loaded sea " + fx(dSea, 6) + " globalTemp " + fx(dT, 6) + " landIce " + fx(climate::landIce(*a), 2) + " vs " + fx(climate::landIce(*l1), 2));
	ok(dSea < 0.005 && dT < 1e-6, "a save/load boundary diverges only slightly (quantized ice)");
	eq(allFinite(*l1), std::string(""));
}

// ------------------------------------------------------------------ phases
DM_TEST("climate adv: stepIce rate per phase: 30 % in Molten / Water / Life / People, 6 % in History / Ended; settled ice raises no flags") {
	const Phase ph[] = { Phase::Molten, Phase::Water, Phase::Life, Phase::People, Phase::History, Phase::Ended };
	for (Phase p : ph) {
		auto w = makeWorld({ .L = 4, .seed = 2, .land = 0.4, .basics = false, .sea = 0, .phase = p });
		climate::calibrate(*w);                                     // settled
		w->dirty = { false, false, false, false, false, false };
		climate::stepIce(*w);
		ok(!w->dirty.color && !w->dirty.water, std::string(toStr(p)) + ": settled ice raises no flags");
		// melt every cell's ice at once: each cell must move exactly rate x the gap (or snap)
		const std::vector<float> eqIce = w->ice;
		std::fill(w->ice.begin(), w->ice.end(), 0.f);
		climate::update(*w);                                        // temps without ice
		climate::stepIce(*w);
		double moved = 0, gap = 0;
		for (int c = 0; c < w->n; c++) if (landC(*w, c) && eqIce[size_t(c)] > 0.99f) { moved += w->ice[size_t(c)]; gap += 1; }
		const double rate = gap > 0 ? moved / gap : -1;
		const double want = (p == Phase::History || p == Phase::Ended) ? 0.06 : 0.30;
		tell(std::string(toStr(p)) + ": share of the gap closed in one stepIce " + fx(rate));
		near(rate, want, 0.01, std::string(toStr(p)) + ": rate");
		ok(w->dirty.color, "moving ice sets dirty.color");
	}
}

// ------------------------------------------------------------------ milestones
DM_TEST("climate adv: milestones over 3,000 years of rising CO2: warming 1..4 each once, in order; sea-rise at most once each") {
	stub::Guard guard;
	hookCover(0.5);
	auto w = makeWorld({ .L = 4, .seed = 3, .land = 0.4, .basics = false });
	climate::calibrate(*w);
	std::vector<std::pair<int, std::string>> got;
	const double base = *w->vars.seaBase;
	double peakAn = -1e9, peakSea = -1e9;
	auto ev = capture(*w, [&] {
		for (int y = 1; y <= 3000; y++) {
			w->co2 = std::min(300.0, 0.1 * y);                          // a slow industrial ramp to the cap
			climate::yearly(*w);
			if (w->need.climate && y % 2 == 0) climate::update(*w);
			if (y % 5 == 0) climate::stepIce(*w);
			peakAn = std::max(peakAn, climate::anomaly(*w)); peakSea = std::max(peakSea, w->seaLevel - base);
		}
	});
	std::vector<std::string> seq;
	for (const Event& e : ev) if (const auto* m = e.as<EvClimateMilestone>()) seq.push_back(std::string(toStr(m->kind)) + " " + fx(m->value, 1));
	std::string all; for (auto& s : seq) all += s + "; ";
	tell("peak anomaly " + fx(peakAn, 2) + " peak sea rise " + fx(peakSea, 3) + " co2 " + fx(w->co2, 1) + " milestones: " + all);
	std::set<std::string> uniq(seq.begin(), seq.end());
	eq(uniq.size(), seq.size(), "no milestone twice");
	std::vector<std::string> warm;
	for (auto& s : seq) if (s.rfind("warming", 0) == 0) warm.push_back(s);
	ok(warm.size() == 4 && warm[0] == "warming 1.0" && warm[3] == "warming 4.0", "warming 1..4 in order: " + all);
	ok(std::find(seq.begin(), seq.end(), "sea-rise 0.2") != seq.end(), "sea rose 0.2");
	ok(w->seaLevel - base <= 0.6 + 1e-9, "sea within seaBase + 0.6");
}

DM_TEST("climate adv: milestones: repeated meteor winters announce cooling once per winter, never twice without a thaw") {
	stub::Guard guard;
	hookCover(0.5);
	auto w = makeWorld({ .L = 4, .seed = 3, .land = 0.4, .basics = false });
	climate::calibrate(*w);
	std::vector<std::string> seq;
	auto ev = capture(*w, [&] {
		for (int y = 1; y <= 600; y++) {
			if (y <= 300 && y % 7 == 0) climate::addDust(*w, 0.25);     // a meteor every 7 years: one long winter
			if (y > 300 && y % 40 == 0) climate::addDust(*w, 0.26);     // later: one every 40 years
			climate::yearly(*w);
		}
	});
	for (const Event& e : ev) if (const auto* m = e.as<EvClimateMilestone>()) seq.push_back(toStr(m->kind));
	int cool = 0, thaw = 0; bool armed = true, okSeq = true;
	for (auto& s : seq) {
		if (s == "cooling") { cool++; if (!armed) okSeq = false; armed = false; }
		if (s == "thaw") { thaw++; armed = true; }
	}
	tell("meteor rain: " + std::to_string(cool) + " cooling, " + std::to_string(thaw) + " thaw");
	ok(okSeq, "never cooling twice without a thaw between");
	ok(cool >= 2 && cool <= 9, "one per winter (first 300 years: one long winter; then one per 40-year meteor): " + std::to_string(cool));
	eq(allFinite(*w), std::string(""));
}

DM_TEST("climate adv: sea-fall milestones: -0.2 is reachable, -0.4 is not with the spec's clamp (seaBase - 0.3)") {
	stub::Guard guard;
	hookCover(0.5);
	auto w = makeWorld({ .L = 4, .seed = 3, .land = 0.4, .basics = false });
	climate::calibrate(*w);
	const double base = *w->vars.seaBase;
	std::vector<std::string> seq;
	auto ev = capture(*w, [&] {
		for (int y = 1; y <= 400; y++) { climate::addDust(*w, 1); w->globalTemp = -12; std::fill(w->ice.begin(), w->ice.end(), 1.f); climate::yearly(*w); }
	});
	for (const Event& e : ev) if (const auto* m = e.as<EvClimateMilestone>()) seq.push_back(std::string(toStr(m->kind)) + " " + fx(m->value, 1));
	std::string all; for (auto& s : seq) all += s + "; ";
	tell("deep freeze: sea " + fx(w->seaLevel - base, 3) + " milestones " + all);
	ok(w->seaLevel - base >= -0.3 - 1e-9, "never below seaBase - 0.3");
	ok(std::find(seq.begin(), seq.end(), "sea-fall -0.2") != seq.end(), "sea-fall -0.2 fires");
	ok(std::find(seq.begin(), seq.end(), "sea-fall -0.4") == seq.end(), "sea-fall -0.4 cannot fire (spec clamp; reported)");
}

// ------------------------------------------------------------------ datum and lapse edges
DM_TEST("climate adv: datum edges: seaBase -3 / +1.2 / inf / NaN sea; lapse only above the datum") {
	auto w = makeWorld({ .L = 4, .seed = 6, .land = 0.4, .basics = false });
	w->vars.seaBase = -3.0; eq(climate::datum(*w), 0.0, "negative seaBase -> 0");
	w->vars.seaBase = 1.2; eq(climate::datum(*w), 1.2);
	w->vars.seaBase = std::numeric_limits<double>::infinity(); w->seaLevel = 0.7; eq(climate::datum(*w), 0.7, "infinite seaBase -> seaLevel");
	w->vars.seaBase.reset(); w->seaLevel = std::nan(""); eq(climate::datum(*w), 0.0, "NaN sea -> 0");
	w->seaLevel = 0;
	w->vars.seaBase = 1.2;
	const int c = findLand(*w, [&](int k) { return std::fabs(latD(*w, k)) < 30; });
	if (!ok(c >= 0)) return;
	auto tAt = [&](double e) { w->elev[size_t(c)] = float(e); climate::update(*w); return double(w->temp[size_t(c)]); };
	near(tAt(0.5) - tAt(1.1), 0.0, 1e-5, "below the datum: no lapse");
	near(tAt(2.2) - tAt(3.2), 5.5, 1e-3, "above the datum: 5.5 per unit");
	near(tAt(1.2) - tAt(2.2), 5.5, 1e-3, "measured from the datum (1.2), not from the sea (0)");
}

// ------------------------------------------------------------------ droughts and other disasters
DM_TEST("climate adv: droughts: only Drought disasters dry the land; overlaps take the strongest; outside the radius nothing changes") {
	auto w = makeWorld({ .L = 5, .seed = 1, .land = 0.4, .basics = false });
	climate::calibrate(*w);
	const std::vector<float> m0 = w->moist;
	const int c = findLand(*w, [&](int k) { return m0[size_t(k)] > 0.5 && std::fabs(latD(*w, k)) < 40; });
	if (!ok(c >= 0)) return;
	for (DisasterType t : { DisasterType::Storm, DisasterType::Volcano, DisasterType::Tsunami, DisasterType::Comet, DisasterType::Meteor }) {
		Disaster& d = state::newDisaster(*w, t, c); d.ang = 0.4; d.power = 1;
	}
	climate::update(*w);
	ok(w->moist == m0, "no other disaster type changes moist");
	Disaster& d1 = state::newDisaster(*w, DisasterType::Drought, c); d1.ang = 0.2; d1.power = 1;
	Disaster& d2 = state::newDisaster(*w, DisasterType::Drought, c); d2.ang = 0.2; d2.power = 0.4;
	climate::update(*w);
	near(m0[size_t(c)] - w->moist[size_t(c)], 0.25, 1e-4, "centre: -0.25 x the strongest drought, not the sum");
	std::vector<int32_t> in;
	w->geo->within(c, 0.2 + 1e-9, in);
	std::vector<char> inside(size_t(w->n), 0);
	for (int k : in) inside[size_t(k)] = 1;
	int outside = 0;
	for (int k = 0; k < w->n; k++) if (!inside[size_t(k)] && w->moist[size_t(k)] != m0[size_t(k)]) outside++;
	eq(outside, 0, "cells outside every drought keep their moist exactly");
	// a drought at the pole and a dead (zero-power / zero-radius) one
	Disaster& d3 = state::newDisaster(*w, DisasterType::Drought, nearestCell(*w, 90, 0)); d3.ang = 0.5; d3.power = 1;
	Disaster& d4 = state::newDisaster(*w, DisasterType::Drought, c); d4.ang = 0; d4.power = 1;
	climate::update(*w);
	eq(allFinite(*w), std::string(""));
}

// ------------------------------------------------------------------ cost at L = 6
DM_TEST("climate adv: cost at L = 6 (asserted at 5x): update on 3 seeds, all sea, all land, 6 big droughts; cold first update") {
	stub::Guard guard;
	hookCover(0.5);
	auto timeIt = [](World& w, int reps) {
		std::vector<double> ts;
		for (int i = 0; i < reps; i++) { const double t = nowMs(); climate::update(w); ts.push_back(nowMs() - t); }
		std::sort(ts.begin(), ts.end());
		return std::pair<double, double>(ts[ts.size() / 2], ts.back());
	};
	std::string line;
	double worst = 0;
	for (uint32_t seed : { 1u, 6u, 9u }) {
		auto w = makeWorld({ .L = 6, .seed = seed, .land = 0.4, .basics = false });
		const double t = nowMs(); climate::update(*w); const double cold = nowMs() - t;
		const auto [med, mx] = timeIt(*w, 9);
		line += "seed " + std::to_string(seed) + " cold " + fx(cold, 1) + " median " + fx(med, 2) + " max " + fx(mx, 2) + "; ";
		ok(med < 60 && cold < 150, "seed " + std::to_string(seed));
		worst = std::max(worst, mx);
		if (seed == 1) {
			for (int k = 0; k < 6; k++) { Disaster& d = state::newDisaster(*w, DisasterType::Drought, k * 5000); d.ang = 0.6; d.power = 1; }
			const auto [dm2, dx2] = timeIt(*w, 5);
			line += "6 droughts of 0.6 rad median " + fx(dm2, 2) + "; ";
			ok(dm2 < 60);
		}
	}
	auto sea = blank(6); std::fill(sea->elev.begin(), sea->elev.end(), -4.f); climate::update(*sea);
	auto land = blank(6); std::fill(land->elev.begin(), land->elev.end(), 1.f); climate::update(*land);
	const auto [sm, sx] = timeIt(*sea, 5); const auto [lm, lx] = timeIt(*land, 5);
	line += "all sea " + fx(sm, 2) + ", all land " + fx(lm, 2);
	std::printf("    climate adv timing at L = 6: %s\n", line.c_str());
	ok(sm < 60 && lm < 60);
	(void)sx; (void)lx; (void)worst;
}

// ------------------------------------------------------------------ balance numbers for the lead (reported, loosely asserted)
DM_TEST("climate adv: sea rise against anomaly at equilibrium (balance target: +2..+5 C with a 0.2..0.5 rise)") {
	stub::Guard guard;
	hookCover(0.6);
	std::string line;
	for (double co2 : { 60.0, 80.0, 120.0, 200.0, 300.0 }) {
		auto w = makeWorld({ .L = 5, .seed = 1, .land = 0.4, .basics = false });
		climate::calibrate(*w);
		const double base = *w->vars.seaBase;
		for (int y = 1; y <= 1500; y++) { w->co2 = co2; climate::yearly(*w); if (w->need.climate && y % 2 == 0) climate::update(*w); if (y % 5 == 0) climate::stepIce(*w); }
		line += "co2 " + fx(co2, 0) + ": +" + fx(climate::anomaly(*w), 2) + " C, sea +" + fx(w->seaLevel - base, 3) + "; ";
		ok(w->seaLevel - base >= 0 && w->seaLevel - base <= 0.6 + 1e-9);
	}
	std::printf("    climate adv equilibrium: %s\n", line.c_str());
}
