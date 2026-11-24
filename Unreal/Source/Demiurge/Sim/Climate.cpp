// DEMIURGE — Sim/Climate.cpp : temperature, rainfall, ice, greenhouse gas, dust and sea level (SPEC-SIM 3.3).
// Owner: climate engineer (A3). Port of js/sim/climate.js (which passed its own tests), adapted to C++:
// the per-grid wind table and the per-world cache live in w.scratch<ClimateScratch>() (no globals),
// the module's own persistent numbers in w.vars.num ("climate_ice0", "climate_seaMark", "climate_ms").
//
// Temperature. cos(latitude)^1.3 profile from +29 at the equator to -24 at the poles, plus a seed-dependent
// low-frequency wobble, minus 5.5 per unit of LAND height above climate::datum (never the live sea level: a
// world whose sea is still at -9 is not frozen, and a rising sea does not warm the land), plus the anomaly,
// minus 4 x ice. A sea cell's stored temp stops at -2; sea ice uses the unclamped value.
//
// Rainfall. Winds by latitude band (easterly trades below 30, westerlies 30-60, polar easterlies beyond; the
// trades also drift toward the equator, the westerlies toward the pole; directions from geo.basis, whose east
// is the direction of DEcreasing geo.lon). Each cell takes its air from up to three neighbours on its upwind
// side (up = the strongest link). A link only counts as far as the wind AT the neighbour also blows toward the
// cell, so where the wind turns round (the calm lines at 30 and 60) part of a cell's air is local instead: dry
// sinking air at 30, moist rising air at 60. Without that rule neighbours across a calm line name each other
// as upwind and trap humidity in a loop that no number of sweeps settles.
//   Sea cells: the air picks up moisture toward a source value (warm sea gives more; sea ice seals it).
//   Land cells: every step rains out a share of what the air carries (more in the rainy latitudes, more in air
//   fresh off the sea, more where the air is forced uphill, less where it sinks) and what the thinner air over
//   high ground cannot hold falls on the windward slope, so the lee lies in a rain shadow. Part of the rain
//   evaporates again and moves on; a trickle mixes down from aloft as long as the planet has a sea.
// The equations are monotone and piecewise linear in the humidity, so with the cells visited upwind-first (two
// orders computed once per grid) a few sweeps settle it however wide the continent.
// moist = sat(squeeze(rain / RAIN_REF) + wet - 0.25 x drought): RAIN_REF is fixed, so changing one place never
// rescales another and a world with no sea is bone dry; squeeze leaves values below rainKnee alone and bends
// the ones above smoothly toward 1.
//
// Ice. Land ice 0 at -4 ... 1 at -9; sea ice 0 at -2.5 ... 1 at -6 (unclamped). With the albedo term the land
// edge is a unique fixed point (closed form); the sea edge is bistable over half a degree. A world without a
// sea has no snow to make ice from (iceNeedsSea).
//
// Sea level (yearly). r = 1 + (sum land ice - ice at calibration) / vars.iceRef: the spec's sum / iceRef
// whenever the world had more than the floor of 50 at calibration, and r = 1 at calibration on worlds with
// less; the rest is the spec's rule. An undisturbed world keeps its sea level exactly.
//
// Milestones (each kind/value once; a thaw re-arms the two winter ones): warming 1..4; cooling -2;
// ice-age -4; thaw -1 (back above -1 after a winter); sea-rise 0.2, 0.4; sea-fall -0.2, -0.4.
#include "Climate.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "Life.h"
#include "Mythos.h"

// (v4 mythos, track M6 economy-world-v4) Climate's mythos reads (SPEC-MYTHOS 1.4, 3.1, 3.13); each is skipped
// at its neutral value, so a Balanced world with neutral Factors is v3.1 to the bit:
//   - archetype noiseK (Beautiful 1.3): the temperature wobble (tNoise) and the rain-lift wobble (rainNoise) are
//     noiseK stronger: more varied biomes. (Folded into the per-world cache signature.)
//   - archetype moistBias (Generous +0.05): added to every land cell's moist before the 0..1 clamp.
//   - Factors::co2Uptake (the World rite): the vegetation term of the yearly CO2 uptake is scaled by the
//     land-weighted mean of the owning cultures' co2Uptake (unowned land counts 1):
//     co2 -= co2 x uptake x (0.5 + veg x mean). Cells are read only when some culture's co2Uptake is not 1.

namespace dm {
namespace {

constexpr double kRefStep = 0.0189;          // geo.step of the reference world (L = 6); rates are per reference cell
constexpr double kDeg = 180.0 / kPi;
// climate_ms bits: warming 0-3, cooling 4, ice-age 5, sea-rise 6-7, sea-fall 8-9
constexpr int kMsCool = 16, kMsIceAge = 32, kMsRise = 6, kMsFall = 8;

double fin(double v, double d) { return std::isfinite(v) ? v : d; }
double gaussW(double x, double c, double width) { const double d = (x - c) / width; return std::exp(-d * d); }
double sstepD(double e0, double e1, double x) { const double t = sat((x - e0) / (e1 - e0)); return t * t * (3 - 2 * t); }
// 0 at t >= e0, 1 at t <= e1 (e1 < e0)
double coldRamp(double e0, double e1, double t) { return e0 > e1 ? sat((e0 - t) / (e0 - e1)) : (t < e0 ? 1.0 : 0.0); }
// a tuning count (passes) as a bounded int; NaN -> lo
int countOf(double v, int lo, int hi) { return !(v >= lo) ? lo : v > hi ? hi : int(v); }
bool isHistoric(Phase p) { return p == Phase::History || p == Phase::Ended; }
bool landAt(float e, double sea) { return double(e) >= sea; }      // a cell is land iff elev >= seaLevel (NaN elev: sea)
// the stored floor of sea temperatures; a non-finite tuning value (+inf would be written as is) falls back to -2
double seaFloorOf(const ClimateTune& T) { return std::isfinite(T.seaTempMin) ? T.seaTempMin : -2.0; }
// a milestone threshold that is not a finite number never fires (its value would be a non-finite event payload)
bool reached(double v, double at, bool up) { return std::isfinite(at) && (up ? v >= at : v <= at); }

std::array<double, 21> gridSigOf(const ClimateTune& T) {
	return { T.band1, T.band2, T.bandBlend, T.merid, T.windMix, T.windAgree, T.subsHum, T.frontHum, T.beltEq, T.beltEqW,
		T.dryLat, T.dryW, T.beltDry, T.liftBase, T.liftEq, T.liftEqW, T.liftMid, T.midLat, T.midW, T.liftDry, T.liftMin };
}
// (v4) the archetype's noise multiplier (1 unless a finite positive number)
double noiseKOf(const World& w) {
	const double k = mythos::archetypeParams(w).noiseK;
	return std::isfinite(k) && k > 0 ? k : 1.0;
}
std::array<double, 6> cacheSigOf(const ClimateTune& T, double nk) {
	return { T.tEquator, T.tPole, T.tShape, T.tNoise * nk, T.tNoiseFreq, T.rainNoise * nk };
}
template <size_t N>
bool sameSig(const std::array<double, N>& p, const std::array<double, N>& q) {
	for (size_t i = 0; i < N; i++) if (!(p[i] == q[i])) return false;     // NaN never matches: rebuild (harmless)
	return true;
}

// ------------------------------------------------------------------ wind table (per geo and wind tuning)
void buildGrid(ClimateScratch& S, const Geo& g, const ClimateTune& T) {
	const int n = g.n;
	const size_t un = size_t(n);
	S.up.assign(un, 0); S.up2.assign(un, 0); S.up3.assign(un, 0);
	S.w1.assign(un, 0.f); S.w2.assign(un, 0.f); S.w3.assign(un, 0.f); S.wl.assign(un, 0.f); S.locH.assign(un, 0.f);
	S.belt.assign(un, 0.f); S.lift.assign(un, 0.f);
	std::vector<float> wx(un), wy(un), wz(un);
	std::vector<double> keyLon(un), keyLat(un);
	const double side = sat(T.windMix) * 0.5;
	const double b1 = T.band1, b2 = T.band2, bw = std::max(0.01, T.bandBlend), bMid = 0.5 * (b1 + b2);
	double dE[6], dN[6], sc[6];
	for (int c = 0; c < n; c++) {
		const size_t uc = size_t(c);
		const double px = g.pos[3 * uc], py = g.pos[3 * uc + 1], pz = g.pos[3 * uc + 2];
		const double a = std::fabs(double(g.lat[uc])) * kDeg;
		const double sg = py < 0 ? -1.0 : 1.0;

		// latitude profiles
		S.belt[uc] = float(std::max(0.05, 1 + T.beltEq * gaussW(a, 0, T.beltEqW) - T.beltDry * gaussW(a, T.dryLat, T.dryW)));
		S.lift[uc] = float(std::max(T.liftMin, T.liftBase + T.liftEq * gaussW(a, 0, T.liftEqW) + T.liftMid * gaussW(a, T.midLat, T.midW)
			- T.liftDry * gaussW(a, T.dryLat, T.dryW)));
		S.locH[uc] = float(a < bMid ? T.subsHum : T.frontHum);

		// wind: u toward the east (-1 trades, +1 westerlies, -1 polar easterlies), vp toward the pole
		const double u = -1 + 2 * sstepD(b1 - bw, b1 + bw, a) - 2 * sstepD(b2 - bw, b2 + bw, a);
		double vp;
		if (a < b1) vp = -std::sin(kPi * a / b1);
		else if (a < b2) vp = std::sin(kPi * (a - b1) / (b2 - b1));
		else vp = -std::sin(kPi * (a - b2) / (90 - b2));
		const double vn = vp * T.merid * sg;
		keyLon[uc] = u > 0 ? -double(g.lon[uc]) : double(g.lon[uc]);     // basis east is the direction of decreasing longitude
		keyLat[uc] = vn > 0 ? double(g.lat[uc]) : vn < 0 ? -double(g.lat[uc]) : 0.0;
		// where the air comes from, in the cell's (east, north) frame
		double ue = -u, un2 = -vn;
		const double ul = std::sqrt(ue * ue + un2 * un2);
		if (!(ul >= 1e-6)) { ue = 1; un2 = 0; } else { ue /= ul; un2 /= ul; }

		Vec3 E, N;
		g.basis(c, E, N);
		wx[uc] = float(-(ue * E.x + un2 * N.x)); wy[uc] = float(-(ue * E.y + un2 * N.y)); wz[uc] = float(-(ue * E.z + un2 * N.z));
		const int k = g.nbrN[uc];
		int best = 0;
		for (int j = 0; j < k; j++) {
			const size_t m = size_t(g.nbr[6 * uc + size_t(j)]);
			double dx = g.pos[3 * m] - px, dy = g.pos[3 * m + 1] - py, dz = g.pos[3 * m + 2] - pz;
			const double rad = dx * px + dy * py + dz * pz;                 // drop the radial part: direction in the tangent plane
			dx -= rad * px; dy -= rad * py; dz -= rad * pz;
			double e = dx * E.x + dy * E.y + dz * E.z, nn = dx * N.x + dy * N.y + dz * N.z;
			double l = std::sqrt(e * e + nn * nn);
			if (!(l > 0)) l = 1;
			e /= l; nn /= l;
			dE[j] = e; dN[j] = nn; sc[j] = e * ue + nn * un2;
			if (sc[j] > sc[best]) best = j;
		}
		// the upwind direction lies between the best neighbour and one of the two beside it
		const int jl = (best + k - 1) % k, jr = (best + 1) % k;
		const int second = sc[jl] > sc[jr] ? jl : jr;
		const double det = dE[best] * dN[second] - dN[best] * dE[second];
		double wt = 0;
		if (std::fabs(det) > 1e-9) {
			const double al = (ue * dN[second] - un2 * dE[second]) / det, be = (dE[best] * un2 - dN[best] * ue) / det;
			if (be > 0 && al > 0) wt = be / (al + be);
			else if (be > 0) wt = 1;
		}
		// sideways mixing: a little air also comes from the neighbours on either side of the best one
		S.up[uc] = g.nbr[6 * uc + size_t(best)]; S.w1[uc] = float((1 - wt) * (1 - 2 * side));
		S.up2[uc] = g.nbr[6 * uc + size_t(second)]; S.w2[uc] = float(wt * (1 - 2 * side) + side);
		S.up3[uc] = g.nbr[6 * uc + size_t(second == jl ? jr : jl)]; S.w3[uc] = float(side);
	}

	// A link c <- m carries air only as far as the wind at m blows toward c. Where the wind turns round within a
	// cell or two (the calm lines) neighbours would otherwise name each other as upwind and trap their humidity
	// in a loop; the share that fails becomes local air (wl). Sideways links are weak and stay as they are.
	const double agree = std::max(0.05, T.windAgree);
	for (int c = 0; c < n; c++) {
		const size_t uc = size_t(c);
		const double px = g.pos[3 * uc], py = g.pos[3 * uc + 1], pz = g.pos[3 * uc + 2];
		double lost = 0;
		for (int i = 0; i < 2; i++) {
			const size_t m = size_t(i ? S.up2[uc] : S.up[uc]);
			const double wgt = i ? S.w2[uc] : S.w1[uc];
			const double dx = px - g.pos[3 * m], dy = py - g.pos[3 * m + 1], dz = pz - g.pos[3 * m + 2];
			double l = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (!(l > 0)) l = 1;
			const double gg = sat((wx[m] * dx + wy[m] * dy + wz[m] * dz) / (l * agree));
			if (gg < 1) {
				const double keep = wgt * gg;
				lost += wgt - keep;
				if (i) S.w2[uc] = float(keep); else S.w1[uc] = float(keep);
			}
		}
		S.wl[uc] = float(lost);
		if (S.w2[uc] > S.w1[uc]) {                                   // up is the strongest link
			std::swap(S.up[uc], S.up2[uc]);
			std::swap(S.w1[uc], S.w2[uc]);
		}
		if (S.w2[uc] < 1e-4f) { S.wl[uc] += S.w2[uc]; S.w2[uc] = 0; S.up2[uc] = S.up[uc]; }
		if (S.w3[uc] < 1e-4f) { S.wl[uc] += S.w3[uc]; S.w3[uc] = 0; S.up3[uc] = S.up[uc]; }
	}

	// Two visiting orders in which (nearly) every cell comes after the cells its air arrives from: one runs
	// downwind in longitude (the east-west winds that circle the planet inside each band), one downwind in
	// latitude (near the calm lines). Sweeping them alternately carries the humidity across any continent in a
	// few sweeps; a link that points the wrong way in one order is simply read one sweep late.
	auto byKey = [n](const std::vector<double>& key, std::vector<int32_t>& out) {
		out.resize(size_t(n));
		std::iota(out.begin(), out.end(), 0);
		std::sort(out.begin(), out.end(), [&key](int32_t p, int32_t q) {
			const double kp = key[size_t(p)], kq = key[size_t(q)];
			return kp < kq || (kp == kq && p < q);
		});
	};
	byKey(keyLon, S.orderLon);
	byKey(keyLat, S.orderLat);
	S.scale = std::max(0.05, g.step / kRefStep);
	S.gridGeo = &g;
	S.gridSig = gridSigOf(T);
	S.gridBuilds++;
}

ClimateScratch& gridFor(const World& w) {
	ClimateScratch& S = w.scratch<ClimateScratch>();
	const ClimateTune& T = w.tune<ClimateTune>();
	if (S.gridGeo != w.geo.get() || S.up.size() != size_t(w.n) || !sameSig(S.gridSig, gridSigOf(T))) buildGrid(S, *w.geo, T);
	return S;
}

// ------------------------------------------------------------------ per-world cache (base temperature, wobble)
ClimateScratch& cacheFor(const World& w) {
	ClimateScratch& S = gridFor(w);
	const ClimateTune& T = w.tune<ClimateTune>();
	const double nk = noiseKOf(w);                                     // (v4) Beautiful: more varied
	const auto sig = cacheSigOf(T, nk);
	if (S.cacheOk && S.cacheN == w.n && S.cacheSeed == w.seed && sameSig(S.cacheSig, sig)) return S;
	const Geo& g = *w.geo;
	const size_t un = size_t(w.n);
	S.base.assign(un, 0.f); S.wob.assign(un, 1.f); S.t0.assign(un, 0.f); S.mh.assign(un, 0.f);
	const double ox = hashf(w.seed, 101) * 64, oy = hashf(w.seed, 102) * 64, oz = hashf(w.seed, 103) * 64;
	const double f = T.tNoiseFreq, span = T.tEquator - T.tPole;
	const double tNoise = T.tNoise * nk, rainNoise = T.rainNoise * nk;
	for (size_t c = 0; c < un; c++) {
		const double x = g.pos[3 * c], y = g.pos[3 * c + 1], z = g.pos[3 * c + 2];
		const double cl = std::sqrt(std::max(0.0, 1 - y * y));
		const double bt = T.tPole + span * std::pow(cl, T.tShape) + tNoise * fbm3(x * f + ox, y * f + oy, z * f + oz, 2);
		S.base[c] = float(fin(bt, 0));
		S.wob[c] = float(fin(std::max(0.4, 1 + rainNoise * noise3(x * 2.1 - oy, y * 2.1 + oz, z * 2.1 - ox)), 1));
	}
	S.cacheOk = true;
	S.cacheN = w.n;
	S.cacheSeed = w.seed;
	S.cacheSig = sig;
	S.hasAnom = false;
	S.anom = 0;
	S.wetWorld = 0;
	S.aloft = 0;
	return S;
}

void ensureBuffers(ClimateScratch& S, int n) {
	const size_t un = size_t(n);
	if (S.z.size() == un) return;
	for (std::vector<float>* v : { &S.z, &S.z2, &S.mar, &S.hum, &S.a, &S.b, &S.li, &S.cap, &S.rc, &S.rain, &S.dr }) v->assign(un, 0.f);
	S.list.clear();
	S.list.reserve(un);
}

// ------------------------------------------------------------------ temperature
// Fills S.t0 (no ice term, no sea clamp) and w.temp from the current elevation, sea, anomaly and ice.
void temps(World& w, ClimateScratch& S) {
	const ClimateTune& T = w.tune<ClimateTune>();
	const int n = w.n;
	const double sea = w.seaLevel, dat = climate::datum(w), an = climate::anomaly(w);
	const double lapse = T.lapse, alb = T.albedo, seaMin = seaFloorOf(T);
	int seaCells = 0;
	for (int c = 0; c < n; c++) {
		const size_t uc = size_t(c);
		const float e = w.elev[uc];
		double t = double(S.base[uc]) + an;
		double ic = w.ice[uc];
		if (!(ic >= 0 && ic <= 1)) { ic = ic > 1 ? 1.0 : 0.0; w.ice[uc] = float(ic); }     // repair (NaN included) before anything reads it
		if (landAt(e, sea)) {
			if (e > dat) t -= lapse * (e - dat);
			t = fin(t, 0);
			S.t0[uc] = float(t);
			w.temp[uc] = float(fin(t - alb * ic, t));
		} else {
			seaCells++;
			t = fin(t, 0);
			S.t0[uc] = float(t);
			t = fin(t - alb * ic, t);
			w.temp[uc] = float(t < seaMin ? seaMin : t);
		}
	}
	S.anom = an;
	S.hasAnom = true;
	S.wetWorld = T.iceNeedsSea > 0 ? sat(double(seaCells) / (double(n) * T.iceNeedsSea)) : 1.0;
	S.aloft = T.seepSea > 0 ? sat(double(seaCells) / (double(n) * T.seepSea)) : (seaCells > 0 ? 1.0 : 0.0);
}

// ------------------------------------------------------------------ rainfall
bool droughts(const World& w, ClimateScratch& S) {
	const ClimateTune& T = w.tune<ClimateTune>();
	const Geo& g = *w.geo;
	bool any = false;
	for (const Disaster& d : w.disasters) {
		if (d.type != DisasterType::Drought) continue;
		const int cell = d.cell;
		const double ang = d.ang, power = sat(fin(d.power, 0));
		if (!w.valid(cell) || !(ang > 0) || !std::isfinite(ang) || !(power > 0)) continue;
		if (!any) { std::fill(S.dr.begin(), S.dr.end(), 0.f); any = true; }
		const int cnt = g.within(cell, ang, S.list);
		const size_t ucell = size_t(cell);
		const double cx = g.pos[3 * ucell], cy = g.pos[3 * ucell + 1], cz = g.pos[3 * ucell + 2];
		const double rim = sat(T.droughtRim);
		for (int j = 0; j < cnt; j++) {
			const size_t c = size_t(S.list[size_t(j)]);
			double s = power;
			if (rim > 0) {
				const double dot = cx * g.pos[3 * c] + cy * g.pos[3 * c + 1] + cz * g.pos[3 * c + 2];
				const double t = std::acos(clamp(dot, -1, 1)) / ang;       // 0 centre ... 1 edge
				const double kk = sat((1 - t) / rim);
				s *= kk * kk * (3 - 2 * kk);
			}
			if (s > S.dr[c]) S.dr[c] = float(s);
		}
	}
	return any;
}

// Fills w.moist. Returns the largest change of moist on a land cell since need.hydro was last raised.
double rainfall(World& w, ClimateScratch& S) {
	const ClimateTune& T = w.tune<ClimateTune>();
	const Geo& g = *w.geo;
	const int n = w.n;
	ensureBuffers(S, n);
	const double s = S.scale, sea = w.seaLevel, alb = T.albedo;
	std::vector<float>& z = S.z;
	std::vector<float>& mar = S.mar;
	std::vector<float>& hum = S.hum;
	std::vector<float>& A = S.a;
	std::vector<float>& B = S.b;
	std::vector<float>& rain = S.rain;
	const auto& up = S.up;
	const auto& up2 = S.up2;
	const auto& up3 = S.up3;
	const auto& w1 = S.w1;
	const auto& w2 = S.w2;
	const auto& w3 = S.w3;
	auto inflow = [&](const std::vector<float>& v, size_t c) {
		return double(w1[c]) * v[size_t(up[c])] + double(w2[c]) * v[size_t(up2[c])] + double(w3[c]) * v[size_t(up3[c])];
	};

	// relief as the air feels it: height above the sea surface, smoothed so single-cell bumps do not wring out rain
	bool anySea = false;
	for (int c = 0; c < n; c++) {
		const size_t uc = size_t(c);
		if (landAt(w.elev[uc], sea)) z[uc] = float(double(w.elev[uc]) - sea);
		else { z[uc] = 0; anySea = true; }
	}

	if (anySea) {
		for (int p = countOf(T.reliefSmooth, 0, 16); p > 0; p--) {
			for (int c = 0; c < n; c++) {
				const size_t uc = size_t(c);
				const int k = g.nbrN[uc];
				double sum = 2.0 * z[uc];
				for (int j = 0; j < k; j++) sum += z[size_t(g.nbr[6 * uc + size_t(j)])];
				S.z2[uc] = float(sum / (2 + k));
			}
			z.swap(S.z2);
		}

		// how fresh off the sea the air over each cell is: 1 over open water, fading downwind over land
		const double marKeep = std::exp(-s / std::max(0.25, fin(T.coastLen, 10)));
		for (size_t c = 0; c < size_t(n); c++) mar[c] = landAt(w.elev[c], sea) ? 0.f : float(1 - sat(T.srcIce * w.ice[c]));
		for (int p = 0, np = countOf(T.coastPasses, 1, 16); p < np; p++) {
			const std::vector<int32_t>& order = (p & 1) ? S.orderLat : S.orderLon;
			for (int i = 0; i < n; i++) {
				const size_t c = size_t(order[size_t(i)]);
				if (landAt(w.elev[c], sea)) mar[c] = float(marKeep * inflow(mar, c));
			}
		}

		const double evap = 1 - std::pow(1 - sat(T.evap), s);
		const double srcSpan = T.srcT1 - T.srcT0, recSpan = T.recycleT1 - T.recycleT0;
		const double srcK = (1 - T.srcCold) / (srcSpan != 0 ? srcSpan : 1);
		const double recK = (T.recycleHot - T.recycleCold) / (recSpan != 0 ? recSpan : 1);
		const double recLo = std::min(T.recycleCold, T.recycleHot), recHi = std::min(0.95, std::max(T.recycleCold, T.recycleHot));
		const double capH = -1 / std::max(0.05, T.capH);
		const double dat = climate::datum(w);
		const double seep = T.seep * s * S.aloft, invS = 1 / s;
		for (int c = 0; c < n; c++) {
			const size_t uc = size_t(c);
			const float e = w.elev[uc];
			const double ice = w.ice[uc], wl = S.wl[uc], wob = S.wob[uc], t0 = S.t0[uc];
			if (!landAt(e, sea)) {                                        // sea: evaporation toward the source value
				const double t = t0 - alb * ice;
				double src = clamp(T.srcCold + srcK * (t - T.srcT0), T.srcMin, T.srcMax);
				src *= S.belt[uc] * (1 - T.srcIce * ice);
				const double l = wl * S.locH[uc] * src;
				S.li[uc] = float(l);
				A[uc] = float(1 - evap); B[uc] = float(evap * src + (1 - evap) * l); S.cap[uc] = 1e9f; hum[uc] = float(src);
				S.rc[uc] = float(T.rainout * S.lift[uc] * wob);
			} else {                                                      // land: rain-out
				// climb per reference cell along the way the air came (local air neither climbs nor sinks)
				const double d = (z[uc] * (1 - wl) - inflow(z, uc)) * invS;
				double r = T.rainout * (S.lift[uc] + T.coastLift * mar[uc]) * wob;
				if (d > 0) r *= 1 + T.upK * (d < T.upMax ? d : T.upMax);     // forced uphill: more rain
				else if (d < 0) r *= 1 - T.desc * sat(-d * T.descK);         // sinking air rains less
				const double rec = clamp(T.recycleCold + recK * (t0 - alb * ice - T.recycleT0), recLo, recHi);
				const double lose = 1 - rec;                                 // share of the rain that leaves the air for good
				double keep = 1 - r * lose;
				keep = std::pow(keep < 0 ? 0 : keep, s);
				S.rc[uc] = float((1 - keep) / (lose * s));
				// sea air of this latitude (sea-level temperature of the cell): the yardstick for the terms below
				const double src = clamp(T.srcCold + srcK * (t0 + (e > dat ? T.lapse * (e - dat) : 0) - T.srcT0), T.srcMin, T.srcMax) * S.belt[uc];
				const double l = wl * S.locH[uc] * src;
				S.li[uc] = float(l);
				// a trickle of moisture mixes down from aloft, so the deepest interior is dry, not sterile
				A[uc] = float(keep); B[uc] = float(keep * l + seep * src); hum[uc] = 0;
				// what the air can carry over this ground thins with height: the excess falls on the way up
				// (orographic rain) and the air beyond the crest stays that dry (rain shadow)
				S.cap[uc] = float(src * T.capK * std::exp(capH * z[uc]));
			}
		}

		const int passes = countOf(T.passes, 1, 64);
		const double capPass = std::pow(1 - sat(T.capDump), s), dump = (1 - capPass) * invS;
		for (int p = 0; p < passes; p++) {
			const std::vector<int32_t>& order = (p & 1) ? S.orderLat : S.orderLon;
			for (int i = 0; i < n; i++) {
				const size_t c = size_t(order[size_t(i)]);
				const double h = A[c] * inflow(hum, c) + B[c];
				hum[c] = float(h > S.cap[c] ? S.cap[c] + (h - S.cap[c]) * capPass : h);
			}
		}
		for (size_t c = 0; c < size_t(n); c++) {
			if (!landAt(w.elev[c], sea)) rain[c] = float(double(S.rc[c]) * hum[c]);
			else {
				const double hin = inflow(hum, c);
				const double h = A[c] * hin + B[c];
				rain[c] = float((hin + S.li[c]) * S.rc[c] + (h > S.cap[c] ? (h - S.cap[c]) * dump : 0));
			}
		}
	} else std::fill(rain.begin(), rain.end(), 0.f);

	// one smoothing pass, then the local terms
	const bool anyDr = droughts(w, S);
	const double inv = 1 / (T.rainRef > 1e-9 ? T.rainRef : 1e-9), dry = T.droughtDry;
	const double knee = T.rainKnee < 1 ? (T.rainKnee > 0 ? T.rainKnee : 0) : 1, over = 1 - knee;
	const double biasRaw = mythos::archetypeParams(w).moistBias;          // (v4) Generous: wetter land
	const double bias = std::isfinite(biasRaw) ? biasRaw : 0.0;
	double maxD = 0;
	for (int c = 0; c < n; c++) {
		const size_t uc = size_t(c);
		const int k = g.nbrN[uc];
		double sum = 2.0 * rain[uc];
		for (int j = 0; j < k; j++) sum += rain[size_t(g.nbr[6 * uc + size_t(j)])];
		double m = sum / (2 + k) * inv;
		if (m > knee && over > 0) m = knee + over * (1 - std::exp((knee - m) / over));   // the wettest places approach 1
		m += w.wet[uc];
		if (anyDr) m -= dry * S.dr[uc];
		if (bias != 0 && landAt(w.elev[uc], sea)) m += bias;          // (v4) the archetype's moisture
		m = m > 0 ? (m < 1 ? m : 1) : 0;                              // also turns NaN into 0
		if (landAt(w.elev[uc], sea)) { const double d = std::fabs(m - S.mh[uc]); if (d > maxD) maxD = d; }
		w.moist[uc] = float(m);
	}
	return maxD;
}

void finish(World& w, ClimateScratch& S, double maxD) {
	w.need.climate = false;
	w.need.life = true;
	w.dirty.color = true;
	if (maxD > w.tune<ClimateTune>().hydroDelta) {
		w.need.hydro = true;
		S.mh = w.moist;
	}
}

// ------------------------------------------------------------------ ice
// One cell's equilibrium ice at unclamped temperature t (ice term included).
double iceEq(const ClimateTune& T, bool land, double t, double wetWorld) {
	return land ? wetWorld * coldRamp(T.landIce0, T.landIce1, t) : coldRamp(T.seaIce0, T.seaIce1, t);
}

// Where a cell's ice settles when it starts from i: the fixed point of ice = iceEq(t0 - albedo x ice). On land
// the albedo feedback is weaker than the ramp (slope 4/5), so there is one answer; sea ice has two over half a
// degree and keeps the one it is nearer to.
double iceFixed(const ClimateTune& T, bool land, double t, double i, double alb, double wetWorld) {
	if (land) {
		const double width = T.landIce0 - T.landIce1, room = width - alb * wetWorld;
		if (width > 0 && room > 1e-6) {                              // closed form: exact, so settled ice never creeps
			const double v = wetWorld * (T.landIce0 - t) / room;
			return v > 0 ? (v < wetWorld ? v : wetWorld) : 0;         // (NaN -> 0)
		}
	}
	for (int it = 0; it < 80; it++) {
		const double eq = iceEq(T, land, t - alb * i, wetWorld);
		if (std::fabs(eq - i) < 1e-5) return sat(eq);
		i = eq;
	}
	return sat(fin(i, 0));
}

// Move every cell `rate` of the way to that equilibrium (rate >= 1: put it there). Keeps w.temp in step.
// Sets dirty.color / dirty.water when something changed.
void moveIce(World& w, ClimateScratch& S, double rate) {
	const ClimateTune& T = w.tune<ClimateTune>();
	const double sea = w.seaLevel, alb = T.albedo, snap = T.iceSnap, seaMin = seaFloorOf(T), wetWorld = S.wetWorld;
	const double r = fin(rate, 0);
	bool land = false, water = false;
	for (size_t c = 0; c < size_t(w.n); c++) {
		const bool isLand = landAt(w.elev[c], sea);
		double i0 = w.ice[c];
		if (!(i0 >= 0)) i0 = 0; else if (i0 > 1) i0 = 1;
		const double eq = iceFixed(T, isLand, S.t0[c], i0, alb, wetWorld), d = eq - i0;
		const double ni = sat(r >= 1 || std::fabs(d) <= snap ? eq : i0 + d * r);
		const float nf = float(ni);
		if (nf != w.ice[c]) {
			if (std::fabs(ni - w.ice[c]) > 1e-3 || !std::isfinite(w.ice[c])) { if (isLand) land = true; else water = true; }
			w.ice[c] = nf;
			const double t = fin(S.t0[c] - alb * ni, S.t0[c]);
			w.temp[c] = float(isLand || t >= seaMin ? t : seaMin);
		}
	}
	if (land || water) w.dirty.color = true;
	if (water) w.dirty.water = true;
}

// vars.iceRef for a world whose settled land ice sums to `ice`: max(iceRefMin, ice), always finite and positive
double iceRefFor(const ClimateTune& T, double ice) {
	const double minRef = T.iceRefMin > 1e-6 && std::isfinite(T.iceRefMin) ? T.iceRefMin : 1e-6;
	return ice > minRef ? ice : minRef;
}

double co2Cap(const ClimateTune& T) { return std::isfinite(T.co2Max) ? std::max(0.0, T.co2Max) : 300.0; }

double coverVeg(const World& w) {
	const auto cv = life::cover(w);
	if (std::isfinite(cv.veg) && cv.veg >= 0) return cv.veg > 1 ? 1 : cv.veg;
	const double d = w.tune<ClimateTune>().vegDefault;
	return std::isfinite(d) ? sat(d) : 0.5;
}

// (v4 mythos, the World rite 3.13) the land-weighted mean of the owners' Factors::co2Uptake (unowned land: 1).
// Exactly 1 (and no cell is read) while every culture's factor is 1.
double uptakeOf(const World& w) {
	const size_t nc = w.cultures.size();
	std::vector<double> k(nc, 1.0);
	bool any = false;
	for (size_t id = 0; id < nc; id++) {
		const double f = mythos::factors(w, int(id)).co2Uptake;
		if (std::isfinite(f) && f >= 0 && f != 1) { k[id] = f; any = true; }
	}
	if (!any || w.owner.size() != size_t(w.n) || w.elev.size() != size_t(w.n)) return 1.0;
	const double sea = w.seaLevel;
	double sum = 0;
	int land = 0;
	for (size_t c = 0; c < size_t(w.n); c++) {
		if (!landAt(w.elev[c], sea)) continue;
		land++;
		double f = 1;
		const Settlement* s = state::settlement(w, w.owner[c]);
		if (s && !s->dead && s->culture >= 0 && size_t(s->culture) < nc) f = k[size_t(s->culture)];
		sum += f;
	}
	const double m = land > 0 ? sum / land : 1.0;
	return std::isfinite(m) ? m : 1.0;
}

double numOr(const World& w, const char* key, double d) {
	const auto it = w.vars.num.find(key);
	return it == w.vars.num.end() ? d : fin(it->second, d);
}

// (v3, design review finding 7) the culture whose engines put the most CO2 into the air so far
// (Economy's vars.num "civ_co2_<id>"), -1 if nobody's did
int smokiest(const World& w) {
	int best = -1;
	double most = 0;
	for (const Culture& c : w.cultures) {
		const auto it = w.vars.num.find("civ_co2_" + std::to_string(c.id));
		if (it != w.vars.num.end() && std::isfinite(it->second) && it->second > most) { most = it->second; best = c.id; }
	}
	return best;
}

void milestone(World& w, MilestoneKind kind, double value) { w.bus.emit(EvClimateMilestone{ kind, value, smokiest(w) }); }

} // namespace

namespace climate {

double datum(const World& w) {
	if (w.vars.seaBase && std::isfinite(*w.vars.seaBase)) return std::max(0.0, *w.vars.seaBase);
	return std::max(0.0, fin(w.seaLevel, 0));
}

double anomaly(const World& w) {
	const double a = fin(w.globalTemp, 0) - w.tune<ClimateTune>().dustCool * fin(w.dust, 0);
	return std::isfinite(a) ? a : fin(w.globalTemp, 0);
}

void emit(World& w, double amount) {
	if (std::isnan(amount)) return;                                     // NaN adds nothing
	w.co2 = clamp(fin(w.co2, 0) + amount, 0, co2Cap(w.tune<ClimateTune>()));
}

void addDust(World& w, double amount) {
	if (std::isnan(amount)) return;
	w.dust = clamp(fin(w.dust, 0) + amount, 0, 1);
	w.need.climate = true;
}

double landIce(const World& w) {
	const double sea = w.seaLevel;
	double sum = 0;
	for (size_t c = 0; c < size_t(w.n); c++)
		if (landAt(w.elev[c], sea) && std::isfinite(w.ice[c])) sum += w.ice[c];
	return fin(sum, 0);
}

void update(World& w) {
	ClimateScratch& S = cacheFor(w);
	temps(w, S);
	finish(w, S, rainfall(w, S));
}

void stepIce(World& w) {
	const ClimateTune& T = w.tune<ClimateTune>();
	ClimateScratch& S = cacheFor(w);
	if (!S.hasAnom) temps(w, S);                                       // no update has run yet on this world
	moveIce(w, S, isHistoric(w.phase) ? T.iceRate : T.iceRateGenesis);
}

void calibrate(World& w) {
	const ClimateTune& T = w.tune<ClimateTune>();
	const double base = fin(w.seaLevel, 0);
	w.vars.seaBase = base;                                              // first, so the lapse datum of this very pass is final
	w.seaLevel = base;
	ClimateScratch& S = cacheFor(w);
	temps(w, S);
	moveIce(w, S, 1);
	const double ice0 = landIce(w);
	w.vars.iceRef = iceRefFor(T, ice0);
	w.vars.num["climate_ice0"] = ice0;
	w.vars.num["climate_seaMark"] = base;
	finish(w, S, rainfall(w, S));
}

void yearly(World& w) {
	const ClimateTune& T = w.tune<ClimateTune>();
	Vars& v = w.vars;
	const double coldest = anomaly(w);                                  // before this year's dust settles

	double dust = fin(fin(w.dust, 0) * T.dustDecay, 0);
	if (dust < 1e-4) dust = 0;
	w.dust = dust > 1 ? 1 : dust;

	if (isHistoric(w.phase) && v.seaBase && std::isfinite(*v.seaBase)) {
		const double base = *v.seaBase;
		// greenhouse
		double co2 = fin(w.co2, 0);
		co2 = fin(co2 - co2 * T.uptake * (0.5 + coverVeg(w) * uptakeOf(w)), co2);   // (v4) x the World rite's uptake
		w.co2 = clamp(co2, 0, co2Cap(T));
		double gt = fin(w.globalTemp, 0);
		gt = fin(gt + (T.sens * std::log2(1 + w.co2 / 100) - gt) / (T.lag > 1 ? T.lag : 1), gt);
		w.globalTemp = gt;

		// sea level
		double ref = v.iceRef ? *v.iceRef : 0;
		if (!(std::isfinite(ref) && ref > 0)) {
			// Calibrated by hand (a seaBase but no iceRef: test helpers, dev worlds). Finish the job the way
			// calibrate would: the reference has to be the EQUILIBRIUM ice, or the ice merely settling afterwards
			// would read as melting or growth and move the sea.
			ClimateScratch& S = cacheFor(w);
			temps(w, S);
			moveIce(w, S, 1);
			const double settled = landIce(w);
			ref = iceRefFor(T, settled);
			v.iceRef = ref;
			v.num["climate_ice0"] = settled;
			w.need.climate = true;                                      // the rainfall has not seen this ice yet
		}
		const double ice = landIce(w);
		// iceRef is floored at iceRefMin; climate_ice0 is the ice that was really there, so r = 1 at calibration
		// (exactly 1 while the ice has not moved: an undisturbed world keeps its sea level to the last bit)
		const double ice0 = std::min(ref, numOr(w, "climate_ice0", ref));
		const double r = 1 + (ice - ice0) / ref;
		const double iceTerm = r <= 1 ? T.seaIceGain * (1 - r) : -T.seaIceLoss * std::min(1.0, r - 1);
		double target = base + iceTerm + T.seaTempK * gt;
		const double lo = base + T.seaLo, hi = base + T.seaHi;
		target = target < lo ? lo : target > hi ? hi : target;
		const double old = fin(w.seaLevel, base);
		const double step = fin(target - old, 0), cap = T.seaRate > 0 ? T.seaRate : 0;
		const double sea = fin(old + (step > cap ? cap : step < -cap ? -cap : step), old);
		w.seaLevel = sea;
		const double mark = numOr(w, "climate_seaMark", old);           // the level coasts, rivers and climate were derived for
		if (std::fabs(sea - mark) > T.seaMoved) {
			v.num["climate_seaMark"] = sea;
			w.need.derive = true; w.need.hydro = true; w.need.climate = true;
			w.dirty.water = true; w.dirty.color = true; w.dirty.objects = true;
		} else v.num["climate_seaMark"] = mark;

		// milestones
		const double an = anomaly(w), cold = an < coldest ? an : coldest;
		int ms = int(clamp(numOr(w, "climate_ms", 0), 0, 1023));
		for (int i = 0; i < 4; i++)
			if (reached(an, T.warm[i], true) && !(ms & (1 << i))) { ms |= 1 << i; milestone(w, MilestoneKind::Warming, T.warm[i]); }
		if (reached(cold, T.cool[0], false) && !(ms & kMsCool)) { ms |= kMsCool; milestone(w, MilestoneKind::Cooling, T.cool[0]); }
		if (reached(cold, T.cool[1], false) && !(ms & kMsIceAge)) { ms |= kMsIceAge; milestone(w, MilestoneKind::IceAge, T.cool[1]); }
		if ((ms & (kMsCool | kMsIceAge)) && reached(an, T.thawAt, true) && reached(cold, T.thawAt, true)) {
			ms &= ~(kMsCool | kMsIceAge);
			milestone(w, MilestoneKind::Thaw, T.thawAt);
		}
		const double ds = sea - base;
		for (int i = 0; i < 2; i++) {
			if (reached(ds, T.seaMile[i], true) && !(ms & (1 << (kMsRise + i)))) { ms |= 1 << (kMsRise + i); milestone(w, MilestoneKind::SeaRise, T.seaMile[i]); }
			if (reached(ds, -T.seaMile[i], false) && !(ms & (1 << (kMsFall + i)))) { ms |= 1 << (kMsFall + i); milestone(w, MilestoneKind::SeaFall, -T.seaMile[i]); }
		}
		v.num["climate_ms"] = double(ms);
	}

	// temperatures go stale as dust settles and the greenhouse builds
	const ClimateScratch& S = w.scratch<ClimateScratch>();
	if (!S.hasAnom || !(std::fabs(anomaly(w) - S.anom) < T.anomalyNeed)) w.need.climate = true;
}

} // namespace climate
} // namespace dm
