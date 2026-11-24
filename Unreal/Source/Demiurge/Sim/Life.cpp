// DEMIURGE — Sim/Life.cpp : the living layer of the planet (SPEC-SIM 3.5): biomes, vegetation,
// wildlife, soil, wildfire and habitability. Port of js/sim/life.js. Owner: life engineer (A5).
//
// How the pieces work (the numbers are in LifeTune, Life.h):
//   Biomes     sea by depth and ice; land: lava / dead scorch -> BARREN, high and cold -> SNOWPEAK, ice -> ICE,
//              steep or very high -> ROCK, a low strip by the sea -> BEACH, flat wet lowland by water ->
//              WETLAND, otherwise a Whittaker table on temperature and effective moisture (heat costs
//              moisture). Heights are measured from climate::datum, slopes are scaled to the reference cell
//              size so every L classifies alike, and the table reads its inputs through a little static noise
//              so borders wander instead of following the climate's isolines.
//   Vegetation exact logistic growth toward capacity (the result does not depend on the slice count), slow
//              die-back above it. A bare cell never greens by itself: it sprouts next to an established
//              neighbour. Sprouting is a hazard, faster on rich ground, along fresh water and where several
//              neighbours are green, slower on slopes, and modulated by two static noise fields: that is what
//              makes the spreading front ragged (lobes, bays, river tendrils). Sprouts are planted at the end
//              of a pass, so the front never depends on the order of the cells. Dead ground (ice, snow, lakes,
//              lava) hands seed pressure on from cell to cell (x airDecay per hop), so a valley behind a
//              glacier is still reached. Drifting seeds (seedsPerYear random land cells per year of the boosted
//              clock) start colonies up to seedHops from a seed source, across the sea; while lifeBoost > 1 a
//              pick that could have been reached over land is thinned, so in genesis the front leads on land
//              and the jumps are the ones over water.
//   Fire       w.cache.fires holds the burning cells. A cell burns for two calls of burn: on the first it tries
//              each neighbour once with P = fireP x veg x sat(dryA - dryB (moist + wet)) and loses burnVeg of its
//              vegetation; on the second it goes out. Bond percolation on the 6-neighbour lattice (threshold
//              ~0.347). fire[] encodes the stage (> kFresh: first tick still to come), so a loaded world carries on.
//
// Private transient state lives in LifeScratch (w.scratch): static noise fields, seed pressure over dead
// ground, per-slice green counts, the cover cache, an edit counter, the colour accumulator, the fire:start
// rate limiter and the bookkeeping that tells whether w.cache.fires is current.
//
// (v4 mythos, track M6 economy-world-v4, SPEC-MYTHOS 3.1) World archetypes, read through
// mythos::archetypeParams(w); Balanced changes nothing (the world's LifeTune is used as it is):
//   - capacityK (Generous 1.1): every biome's vegetation capacity (LifeTune::cap) x capacityK (still at most 1
//     per cell);
//   - fert (Generous +0.15, Dangerous -0.05): added to the natural soil level (fertBase), so the fertility
//     Terrain generated keeps its offset instead of relaxing away;
//   - marvels (Strange; Terrain's vars.num "terrain_marvels" / "terrain_marvel_<i>"): a marvel cell never
//     catches fire (ignite refuses it, fires do not spread onto it) and its fert is held at >= 0.8 by step.
#include "Life.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "Climate.h"
#include "Mythos.h"
#include "Sim.h"

namespace dm {

namespace {

constexpr double kFresh = 0.55;      // fire[] above this: the first tick is still to come
constexpr double kFireNew = 0.6;     // fire[] of a new fire is kFireNew .. 1

struct LifeScratch {
	// static noise fields (built once per world and seed): grain, regional, patches; all 0..1
	bool nzOk = false;
	uint32_t nzSeed = 0;
	std::vector<float> grain, region, patch;
	// seed pressure carried by dead ground
	std::vector<float> air;
	// sprouts of one step pass, planted at the end
	std::vector<int32_t> sproutC;
	std::vector<float> sproutV;
	// buffers for within / ring and the overland BFS
	std::vector<int32_t> list, ringBuf, walk, mark;
	int32_t stamp = 0;
	// per-slice count of vegetated cells at the last visit (-1: not visited / unknown)
	int greenParts = 0;
	std::vector<int32_t> green;
	// edit counter (cover cache key), colour accumulator, fire:start limiter
	uint32_t ver = 0;
	double acc = 0;
	int64_t evTick = -1;
	int evN = 0;
	// w.cache.fires bookkeeping: built by us, and its length at our last write
	bool firesOk = false;
	size_t firesN = 0;
	// cover cache
	bool coverOk = false;
	int64_t coverTick = 0;
	uint32_t coverVer = 0;
	double coverSea = 0;
	life::Cover coverVal;
	// (v4 mythos) the archetype-adjusted copy of LifeTune (only used when the archetype is not neutral)
	LifeTune archTune;
};

constexpr int kMaxMarvels = 16;

inline LifeScratch& scr(const World& w) {
	LifeScratch& S = w.scratch<LifeScratch>();
	const size_t n = size_t(w.n);
	if (S.mark.size() != n) {
		S.mark.assign(n, 0);
		S.stamp = 0;
		S.air.assign(n, 0.0f);
		S.sproutC.assign(n, 0);
		S.sproutV.assign(n, 0.0f);
		S.nzOk = false;
	}
	return S;
}

// (v4 mythos) The LifeTune of this world's archetype: the world's own when the archetype is neutral for Life
// (Balanced: bit-identical), else a copy in scratch with cap[] x capacityK and fertBase + fert.
const LifeTune& tuneOf(const World& w) {
	const LifeTune& T = w.tune<LifeTune>();
	const ArchetypeParams ap = mythos::archetypeParams(w);
	const double capK = isFinite(ap.capacityK) && ap.capacityK > 0 ? ap.capacityK : 1.0;
	const double fertAdd = isFinite(ap.fert) ? ap.fert : 0.0;
	if (capK == 1 && fertAdd == 0) return T;
	LifeTune& A = scr(w).archTune;
	A = T;
	if (capK != 1) for (double& k : A.cap) k *= capK;
	A.fertBase += fertAdd;
	return A;
}

// (v4 mythos) The marvel cells Terrain placed (vars.num "terrain_marvels" / "terrain_marvel_<i>"); returns the
// count written to out (at most kMaxMarvels). No marvels: one map lookup, 0.
int marvelsOf(const World& w, int32_t* out) {
	const auto it = w.vars.num.find("terrain_marvels");
	if (it == w.vars.num.end() || !(it->second >= 1)) return 0;
	const int k = it->second > kMaxMarvels ? kMaxMarvels : int(it->second);
	int m = 0;
	for (int i = 0; i < k; i++) {
		const auto jt = w.vars.num.find("terrain_marvel_" + std::to_string(i));
		if (jt == w.vars.num.end() || !(jt->second >= 0) || !(jt->second < w.n)) continue;
		out[m++] = int32_t(jt->second);
	}
	return m;
}
inline bool isMarvel(const int32_t* mv, int nm, int c) {
	for (int k = 0; k < nm; k++) if (mv[k] == c) return true;
	return false;
}

inline double sm(double e0, double e1, double x) {            // smoothstep, safe for e0 == e1
	if (!(e1 > e0)) return x < e0 ? 0.0 : 1.0;
	const double t = x <= e0 ? 0.0 : x >= e1 ? 1.0 : (x - e0) / (e1 - e0);
	return t * t * (3 - 2 * t);
}
// the smallest float that is not below x (veg[] is float: "veg >= 0.35" must hold for the stored value)
inline float f32up(double x) {
	float f = float(x);
	if (double(f) < x) f = std::nextafter(f, 2.0f);
	return f;
}
inline void bump(const World& w) { scr(w).ver++; }
// vegetation planted outside step: the per-slice record of where anything grows is no longer known
inline void greenUnknown(const World& w) {
	LifeScratch& S = scr(w);
	std::fill(S.green.begin(), S.green.end(), -1);
}
inline bool crosses(double a, double b) {
	const double* VT = state::kVegT;
	return (a >= VT[0]) != (b >= VT[0]) || (a >= VT[1]) != (b >= VT[1]) || (a >= VT[2]) != (b >= VT[2]);
}

double datumOf(const World& w) {
	const double d = climate::datum(w);
	return isFinite(d) ? d : 0.0;
}
double slopeScale(const World& w, const LifeTune& T) {
	const double st = w.geo ? w.geo->step : 0.0;
	return st > 0 ? T.refStep / st : 1.0;
}
double boostOf(const World& w, const LifeTune& T) {
	const double b = w.vars.lifeBoost;
	if (!(b >= 0)) return 1.0;                                  // NaN, negative
	const double mx = T.maxBoost;
	const bool mxOk = isFinite(mx) && mx >= 0;                  // (verifier: a non-finite / negative maxBoost made boost -inf)
	if (mxOk && b > mx) return mx;
	return isFinite(b) ? b : (mxOk ? mx : 1.0);
}
// a per-call multiplicative factor that must lie in [0, 1] (bad tuning: NaN -> fb, out of range -> clamped)
inline double unit(double x, double fb) { return isFinite(x) ? (x < 0 ? 0.0 : x > 1 ? 1.0 : x) : (x > 0 ? 1.0 : x < 0 ? 0.0 : fb); }
// a Life-owned 0..1 field read back from the world: NaN -> 0, +inf -> 1, -inf -> 0 (finite values untouched)
inline double finField(double x) { return isFinite(x) ? x : (x > 0 ? 1.0 : 0.0); }
// usable world: a geo and cell arrays of the right size
bool usable(const World& w) {
	return w.geo && w.n > 0 && w.geo->n == w.n && w.elev.size() == size_t(w.n) && w.veg.size() == size_t(w.n);
}

// Three static noise fields per world, built once from the seed: grain (a few cells across: how readily the
// ground takes seed), region (natural soil richness) and patch (in between: where biome borders bulge).
const LifeScratch& noiseOf(const World& w) {
	LifeScratch& S = scr(w);
	if (S.nzOk && S.nzSeed == w.seed && S.grain.size() == size_t(w.n)) return S;
	const size_t n = size_t(w.n);
	const std::vector<float>& pos = w.geo->pos;
	const uint32_t sd = w.seed;
	S.grain.assign(n, 0.5f);
	S.region.assign(n, 0.5f);
	S.patch.assign(n, 0.5f);
	const double ax = hashf(sd, 7101) * 97, ay = hashf(sd, 7102) * 97, az = hashf(sd, 7103) * 97;
	const double bx = hashf(sd, 7104) * 97, by = hashf(sd, 7105) * 97, bz = hashf(sd, 7106) * 97;
	const double cx = hashf(sd, 7107) * 97, cy = hashf(sd, 7108) * 97, cz = hashf(sd, 7109) * 97;
	for (size_t c = 0; c < n; c++) {
		const double x = pos[3 * c], y = pos[3 * c + 1], z = pos[3 * c + 2];
		const double a = 0.62 * noise3(x * 6.5 + ax, y * 6.5 + ay, z * 6.5 + az) + 0.38 * noise3(x * 15.1 + az, y * 15.1 + ax, z * 15.1 + ay);
		const double b = noise3(x * 2.6 + bx, y * 2.6 + by, z * 2.6 + bz);
		const double d = 0.7 * noise3(x * 4.3 + cx, y * 4.3 + cy, z * 4.3 + cz) + 0.3 * noise3(x * 9.7 + cz, y * 9.7 + cx, z * 9.7 + cy);
		const double ga = 0.5 + 0.95 * a, rb = 0.5 + 0.75 * b, pd = 0.5 + 0.9 * d;
		S.grain[c] = float(isFinite(ga) ? sat(ga) : 0.5);
		S.region[c] = float(isFinite(rb) ? sat(rb) : 0.5);
		S.patch[c] = float(isFinite(pd) ? sat(pd) : 0.5);
	}
	S.nzOk = true;
	S.nzSeed = sd;
	return S;
}

// ------------------------------------------------------------------ capacity
double capOf(const World& w, int c, const LifeTune& T) {
	const size_t i = size_t(c);
	if (w.elev[i] < w.seaLevel) return 0;
	if (w.water[i] > T.lakeWater || w.ice[i] > T.iceBiome || w.lava[i] > T.lavaBarren) return 0;
	const int b = w.biome[i] < Biome::COUNT ? int(w.biome[i]) : 0;
	const double k = T.cap[b] * (0.6 + 0.4 * double(w.fert[i])) * (1 - double(w.farm[i])) * (1 - double(w.urban[i])) * (1 - 0.8 * double(w.rad[i]));
	if (!(k > 0)) return 0;
	return k > 1 ? 1 : k;
}

// does a cell shed seed for the drift: veg above seedSource, or a good share of what its own ground can carry
bool sheds(const World& w, int m, const LifeTune& T) {
	const double vm = w.veg[size_t(m)];
	return vm > T.seedSource || (vm > T.sproutNbr && vm >= T.seedSourceRel * capOf(w, m, T));
}

// Is there a seed source within `hops` of cell c that can be reached without crossing the sea? (BFS over land
// cells only; ice and lakes count as land.) Such a pick lies ahead of a front that will arrive by itself; one that
// fails this test is a true jump over water.
bool overland(const World& w, int c, int hops, const LifeTune& T) {
	LifeScratch& S = scr(w);
	const Geo& g = *w.geo;
	if (S.stamp >= 0x7ffffff0) { std::fill(S.mark.begin(), S.mark.end(), 0); S.stamp = 0; }
	const int32_t stamp = ++S.stamp;
	std::vector<int32_t>& q = S.walk;
	q.clear();
	q.push_back(c);
	S.mark[size_t(c)] = stamp;
	size_t head = 0;
	const double sea = w.seaLevel;
	for (int d = 0; d < hops && head < q.size(); d++) {
		for (const size_t end = q.size(); head < end; head++) {
			const int a = q[head];
			const int deg = g.nbrN[size_t(a)];
			for (int j = 0; j < deg; j++) {
				const int m = g.nbr[size_t(6 * a + j)];
				if (S.mark[size_t(m)] == stamp) continue;
				S.mark[size_t(m)] = stamp;
				if (w.elev[size_t(m)] < sea) continue;
				if (sheds(w, m, T)) return true;
				q.push_back(m);
			}
		}
	}
	return false;
}

// wildlife a dry land cell could hold
double faunaLand(const World& w, int c, const LifeTune& T, double v) {
	const size_t i = size_t(c);
	const int b = w.biome[i] < Biome::COUNT ? int(w.biome[i]) : 0;
	const double fc = T.faunaCap[b];
	if (!(fc > 0) || !(v > 0) || w.ice[i] > T.iceBiome) return 0;
	const double rel = v / (0.7 * T.cap[b] + 0.03);
	const double k = fc * std::sqrt(rel > 1 ? 1 : rel) * (1 - double(w.urban[i])) * (1 - 0.5 * double(w.farm[i])) * (1 - double(w.rad[i]));
	return k > 0 ? (k > 1 ? 1 : k) : 0;
}
// fish: by depth (rich shallows), water temperature, ice cover and fallout; depth < 0 means a lake
double faunaWater(const World& w, int c, const LifeTune& T, double depth) {
	const size_t i = size_t(c);
	const double t = w.temp[i];
	double k;
	if (depth < 0) k = T.fishLake;
	else if (depth <= T.deepSea) k = T.fishShallow;
	else {
		const double span = T.fishDeepAt - T.deepSea;
		k = T.fishShallow + (T.fishDeep - T.fishShallow) * (span > 0 ? sat((depth - T.deepSea) / span) : 1.0);
	}
	k *= (0.55 + 0.45 * sat((t + 2) / 8)) * (1 - 0.3 * sat((t - 24) / 6));
	k *= (1 - 0.8 * sat(w.ice[i])) * (1 - sat(w.rad[i]));
	return k > 0 ? (k > 1 ? 1 : k) : 0;
}

// ------------------------------------------------------------------ the fire list
std::vector<int32_t>& firesOf(World& w) {
	LifeScratch& S = scr(w);
	if (!S.firesOk || w.cache.fires.size() != S.firesN) life::rebuild(w);
	return w.cache.fires;
}
void firesDone(World& w) {
	LifeScratch& S = scr(w);
	S.firesOk = true;
	S.firesN = w.cache.fires.size();
}
void dropFire(World& w, int c) {
	std::vector<int32_t>& F = w.cache.fires;
	for (size_t i = 0; i < F.size(); i++) {
		if (F[i] == c) { F[i] = F.back(); F.pop_back(); break; }
	}
	firesDone(w);
}
// can the cell catch fire at all (the vegetation and wetness rules; not the chance)
bool flammable(const World& w, int c, const LifeTune& T) {
	const size_t i = size_t(c);
	return w.elev[i] >= w.seaLevel && !(w.ice[i] > T.iceBiome) && !(w.water[i] > T.lakeWater) &&
		w.veg[i] >= T.fireMinVeg && !(w.wet[i] > T.wetOut) && !(w.fire[i] > 0);
}

} // namespace

namespace life {

double capacity(const World& w, int c) {
	if (!w.valid(c) || !usable(w)) return 0;
	return capOf(w, c, tuneOf(w));
}

// ------------------------------------------------------------------ biomes
void classify(World& w) {
	if (!usable(w)) return;
	const LifeTune& T = w.tune<LifeTune>();
	const int n = w.n;
	const double sea = w.seaLevel, datum = datumOf(w), sk = slopeScale(w, T);
	const LifeScratch& nz = noiseOf(w);
	const double jM = T.jitterM, jG = T.jitterG, jT = T.jitterT;
	for (int c = 0; c < n; c++) {
		const size_t i = size_t(c);
		const double e = w.elev[i];
		uint8_t b;
		if (e < sea) {
			b = w.ice[i] > T.iceBiome ? Biome::SEAICE : (sea - e > T.deepSea ? Biome::SEA : Biome::SHALLOWS);
		} else {
			const double t = w.temp[i], h = e - datum, s = double(w.slope[i]) * sk;
			const bool iced = w.ice[i] > T.iceBiome;
			if (w.lava[i] > T.lavaBarren || (w.scorch[i] > T.scorchBarren && w.veg[i] < T.vegBarren)) b = Biome::BARREN;
			else if (h >= T.snowH && (iced || t < T.snowT)) b = Biome::SNOWPEAK;
			else if (iced) b = Biome::ICE;
			else if (s > T.rockSlope || h > T.rockH) b = Biome::ROCK;
			else if (w.coast[i] <= 1 && e - sea < T.beachH && s < T.beachSlope && t > T.beachT && !(w.water[i] > T.lakeWater)) b = Biome::BEACH;
			else {
				const double m = w.moist[i];
				if (m >= T.wetlandM && t >= T.wetlandT && h < T.wetlandH && s <= T.wetlandSlope && (w.fresh[i] <= 1 || w.coast[i] <= 1)) b = Biome::WETLAND;
				else {
					const double tj = t + jT * (double(nz.region[i]) - 0.5);
					if (tj < T.tundraT) b = Biome::TUNDRA;
					else {
						const double me = m - T.evap * (t > 10 ? t - 10 : 0) + jM * (double(nz.patch[i]) - 0.5) + jG * (double(nz.grain[i]) - 0.5);
						if (tj < T.borealT) b = me >= T.taigaM ? Biome::TAIGA : Biome::TUNDRA;
						else if (me < T.desertM) b = Biome::DESERT;
						else if (tj < T.coolT) b = me < T.grassM ? Biome::GRASSLAND : Biome::TAIGA;
						else if (tj < T.hotT) b = me < T.grassM ? Biome::GRASSLAND : Biome::FOREST;
						else b = me < T.savannaM ? Biome::SAVANNA : (me < T.rainM ? Biome::FOREST : Biome::RAINFOREST);
					}
				}
			}
		}
		w.biome[i] = b;
	}
	w.dirty.color = true;
	w.dirty.objects = true;
	bump(w);
}

// ------------------------------------------------------------------ fires
void rebuild(World& w) {
	std::vector<int32_t>& F = w.cache.fires;
	F.clear();
	if (w.fire.size() == size_t(w.n))
		for (int c = 0; c < w.n; c++) {
			float& f = w.fire[size_t(c)];
			if (!isFinite(f)) f = f > 0 ? 1.0f : 0.0f;            // (verifier) garbage fire values: NaN out, +inf a fresh fire
			if (f > 0) F.push_back(c);
		}
	firesDone(w);
}

bool ignite(World& w, int c, double intensity) {
	if (!w.valid(c) || !usable(w)) return false;
	const LifeTune& T = w.tune<LifeTune>();
	if (!(intensity > 0)) return false;
	if (!flammable(w, c, T)) return false;
	{
		int32_t mv[kMaxMarvels];
		const int nm = marvelsOf(w, mv);
		if (nm > 0 && isMarvel(mv, nm, c)) return false;               // (v4) a marvel never burns
	}
	std::vector<int32_t>& F = firesOf(w);
	if (int(F.size()) >= T.maxBurning) return false;
	w.fire[size_t(c)] = float(kFireNew + (1 - kFireNew) * sat(intensity));
	F.push_back(c);
	firesDone(w);
	LifeScratch& S = scr(w);
	if (S.evTick != w.tick) { S.evTick = w.tick; S.evN = 0; }
	if (S.evN < T.fireEventsPerTick) { S.evN++; w.bus.emit(EvFireStart{ c }); }
	w.dirty.color = true;
	return true;
}

void burn(World& w) {
	if (!usable(w)) return;
	std::vector<int32_t>& cells = firesOf(w);
	const size_t n0 = cells.size();
	if (n0 == 0) return;
	const LifeTune& T = w.tune<LifeTune>();
	const Geo& g = *w.geo;
	Rng& rng = w.rng;
	const double sea = w.seaLevel, iceT = T.iceBiome, lake = T.lakeWater;
	const int fmax = T.maxBurning;
	const double fP = T.fireP, dA = T.dryA, dB = T.dryB, fVeg = T.fireMinVeg, fWet = T.wetOut, keep = unit(1 - T.burnVeg, 0.2);
	const double keepFauna = unit(1 - T.burnFauna, 0.5), ash = isFinite(T.burnFert) ? T.burnFert : 0.0;
	std::vector<float>& fire = w.fire;
	int32_t mv[kMaxMarvels];
	const int nm = marvelsOf(w, mv);                                    // (v4) marvels never catch fire
	cells.reserve(n0 + size_t(std::max(0, fmax)) + 8);
	// Pass 1, the fires as the tick finds them. Ground that can no longer burn (drowned, frozen, rained on) goes
	// out at once. A fire on its second tick is spent, but stays marked (negative, cleared below) until the tick is
	// over, so the cells it lit cannot light it again while it is still burning.
	int live = 0;
	for (size_t k = 0; k < n0; k++) {
		const int c = cells[k];
		if (c < 0 || c >= w.n) continue;
		const size_t i = size_t(c);
		double f = fire[i];
		if (!(f > 0)) continue;                                         // went out some other way
		if (f > 1) { f = 1; fire[i] = 1; }                              // (verifier) +inf / garbage above a fresh fire
		if (w.elev[i] < sea || w.ice[i] > iceT || w.water[i] > lake || w.wet[i] > fWet) fire[i] = 0;
		else if (f > kFresh) live++;
		else fire[i] = -1;
	}
	// Pass 2, the first tick of a fire: one attempt per neighbour, then the cell itself burns down. `live` counts
	// the cells that will be burning when this call returns; nothing new catches once that reaches the cap.
	bool burnt = false, objects = false;
	for (size_t k = 0; k < n0; k++) {
		const int c = cells[k];
		if (c < 0 || c >= w.n) continue;
		const size_t i = size_t(c);
		const double f = fire[i];
		if (!(f > kFresh)) continue;
		const int deg = g.nbrN[i];
		for (int j = 0; j < deg; j++) {
			if (live >= fmax) break;
			const int m = g.nbr[6 * i + size_t(j)];
			const size_t mi = size_t(m);
			const double vm = w.veg[mi];
			if (!(vm >= fVeg) || fire[mi] != 0 || w.elev[mi] < sea || w.ice[mi] > iceT || w.water[mi] > lake) continue;
			if (nm > 0 && isMarvel(mv, nm, m)) continue;
			const double wm = w.wet[mi];
			if (wm > fWet) continue;
			double dry = dA - dB * (double(w.moist[mi]) + wm);
			if (!(dry > 0)) continue;
			if (dry > 1) dry = 1;
			if (rng.f() < fP * vm * dry) { fire[mi] = 1; cells.push_back(m); live++; }
		}
		const double v = w.veg[i];
		if (v > 0) {
			const double nv = v * keep;
			w.veg[i] = float(nv);
			const double sc = 0.3 + 0.9 * v;
			if (sc > w.scorch[i]) w.scorch[i] = float(sc > 1 ? 1 : sc);
			w.fauna[i] = float(finField(w.fauna[i]) * keepFauna);
			const double fe = w.fert[i], nf = fe + ash * v;
			if (isFinite(nf)) w.fert[i] = float(nf > T.fertCap ? (fe > T.fertCap ? fe : T.fertCap) : nf);
			if (crosses(v, nv)) objects = true;
			burnt = true;
		}
		fire[i] = float(f * 0.5);
	}
	// Pass 3: the spent fires go out, and the list is rebuilt without them and without duplicates (a kept cell
	// is flagged by flipping its sign for the length of the loop).
	for (size_t k = 0; k < n0; k++) {
		const int c = cells[k];
		if (c >= 0 && c < w.n && fire[size_t(c)] < 0) fire[size_t(c)] = 0;
	}
	size_t kept = 0;
	const size_t end = cells.size();
	for (size_t k = 0; k < end; k++) {
		const int c = cells[k];
		if (c < 0 || c >= w.n) continue;
		const float f = fire[size_t(c)];
		if (f > 0) { cells[kept++] = c; fire[size_t(c)] = -f; }
	}
	cells.resize(kept);
	for (size_t k = 0; k < kept; k++) fire[size_t(cells[k])] = -fire[size_t(cells[k])];
	firesDone(w);
	w.dirty.color = true;
	if (objects) w.dirty.objects = true;
	if (burnt) bump(w);
}

// ------------------------------------------------------------------ the step
void step(World& w, int part, int parts) {
	if (!usable(w)) return;
	const LifeTune& T = tuneOf(w);
	const int n = w.n;
	if (parts <= 0) parts = 1;
	part = part > 0 ? part % parts : 0;
	const double tickYears = sim::kTick, dt = parts * tickYears, boost = boostOf(w, T), gt = dt * boost;
	const Geo& g = *w.geo;
	Rng& rng = w.rng;
	const double vt0 = state::kVegT[0];
	const double sea = w.seaLevel, datum = datumOf(w), sk = slopeScale(w, T);
	LifeScratch& S = scr(w);
	noiseOf(w);
	const std::vector<float>& grain = S.grain;
	const std::vector<float>& region = S.region;
	const std::vector<float>& patch = S.patch;
	std::vector<float>& air = S.air;
	const double airVeg = T.airVeg, airDecay = sat(T.airDecay);
	const double iceT = T.iceBiome, lake = T.lakeWater, lavaT = T.lavaBarren, tiny = T.tiny;
	const double plantMin = T.plantMin, est = T.sproutNbr, estRel = T.establishedRel, sprout = T.sprout;
	// per-call factors: every rate below is exact for the time step, so 2 slices and 8 slices agree
	// (verifier: each factor is held to [0, 1], so a NaN / infinite / negative rate in LifeTune cannot write NaN)
	const double eGrow = unit(std::exp(-T.grow * gt), 1), kDie = unit(1 - std::exp(-T.die * gt), 0), kKill = unit(1 - std::exp(-T.kill * gt), 0);
	const double eFauna = unit(std::exp(-T.faunaGrow * gt), 1), kFaunaDie = unit(1 - std::exp(-T.faunaDie * gt), 0);
	const double fWet = unit(std::exp(-T.wetDecay * dt), 1), fScorch = unit(std::exp(-T.scorchDecay * dt), 1);
	const double fRad = unit(std::pow(1 - sat(T.radDecay), dt), 1), fFarm = unit(std::pow(1 - sat(T.farmDecay), dt), 1), fUrban = unit(std::pow(1 - sat(T.urbanDecay), dt), 1);
	const double kFert = unit(1 - std::exp(-T.fertRate * dt), 0);
	const double spreadG = T.spread * dt * std::pow(boost, T.spreadBoost), sc0 = T.spreadCap0, scRef = T.spreadCapRef > 0 ? T.spreadCapRef : 1.0;
	const double fseed = T.faunaSeed, fsrc = T.faunaSrc;
	double dCol = 0;
	bool objects = false, relife = false;
	size_t ns = 0;
	int greenN = 0;

	for (int c = part; c < n; c += parts) {
		const size_t i = size_t(c);
		const int deg = g.nbrN[i];
		const int32_t* nb = &g.nbr[6 * i];
		const double e = w.elev[i];
		// ---------------- sea: nothing of the land survives; fish
		if (e < sea) {
			if (w.veg[i] != 0) { if (w.veg[i] >= vt0) objects = true; w.veg[i] = 0; dCol += 1; }
			if (w.farm[i] != 0) { w.farm[i] = 0; dCol += 1; }
			if (w.urban[i] != 0) { w.urban[i] = 0; dCol += 1; objects = true; }
			if (w.scorch[i] != 0) { w.scorch[i] = 0; dCol += 1; }
			if (w.road[i] != 0) { w.road[i] = 0; w.dirty.roads = true; }
			if (w.water[i] != 0) { w.water[i] = 0; w.dirty.water = true; }
			if (w.fire[i] != 0) { w.fire[i] = 0; dropFire(w, c); }
			if (air[i] != 0) air[i] = 0;
			if (!isFinite(w.fert[i])) w.fert[i] = 0.5f;                       // (verifier) garbage soil under the sea
			double wt = w.wet[i];
			if (wt != 0) { wt = finField(wt) * fWet; w.wet[i] = float(wt > tiny ? wt : 0); }
			double r = w.rad[i];
			if (r != 0) { r = finField(r) * fRad; w.rad[i] = float(r > tiny ? r : 0); }
			const double K = faunaWater(w, c, T, sea - e);
			double F = w.fauna[i];
			if (!(F >= 0) || !isFinite(F)) { F = 0; w.fauna[i] = 0; }          // (verifier) NaN stayed when no neighbour had fish
			if (F > K) w.fauna[i] = float(F + (K - F) * kFaunaDie);
			else if (F > 0) { if (F < K) w.fauna[i] = float(K * F / (F + (K - F) * eFauna)); }
			else if (K > fsrc) {
				for (int j = 0; j < deg; j++) if (w.fauna[size_t(nb[j])] > fsrc) { w.fauna[i] = float(fseed < K ? fseed : K); break; }
			} else if (F != 0) w.fauna[i] = 0;
			continue;
		}

		// ---------------- land: decays
		// (verifier: finField: a NaN / infinite value of a Life-owned field is repaired instead of kept)
		double wt = w.wet[i];
		if (wt != 0) { const double o = finField(wt); wt = o * fWet; if (!(wt > tiny)) wt = 0; w.wet[i] = float(wt); dCol += std::fabs(o - wt); }
		double s = w.scorch[i];
		if (s != 0) { const double o = finField(s); s = o * fScorch; if (!(s > tiny)) s = 0; w.scorch[i] = float(s); dCol += std::fabs(o - s); }
		double r = w.rad[i];
		if (r != 0) { r = finField(r) * fRad; if (!(r > tiny)) r = 0; w.rad[i] = float(r); }
		if (!isFinite(w.fire[i])) w.fire[i] = w.fire[i] > 0 ? 1.0f : 0.0f;
		double fa = w.farm[i], ur = w.urban[i];
		if (w.owner[i] < 0) {
			if (fa != 0) { const double o = finField(fa); fa = o * fFarm; if (!(fa > tiny)) fa = 0; w.farm[i] = float(fa); dCol += std::fabs(o - fa); }
			if (ur != 0) { const double o = finField(ur); ur = o * fUrban; if (!(ur > tiny)) ur = 0; w.urban[i] = float(ur); dCol += std::fabs(o - ur); }
		}
		if (!isFinite(fa)) fa = 0;
		if (!isFinite(ur)) ur = 0;
		if (!isFinite(r)) r = 0;

		// ---------------- soil: toward the natural level (rich by fresh water, on lowlands and on old ash)
		const double h = e - datum;
		const int fr = w.fresh[i];
		double f = w.fert[i];
		{
			double nat = T.fertBase + T.fertVar * double(region[i]) + (fr < 4 ? T.fertFresh[fr] : 0.0) + T.fertAsh * s;
			if (h < 1.2) nat += T.fertLow * (h > 0 ? 1 - h / 1.2 : 1.0);
			else if (h > 2.2) nat -= T.fertHigh * (h < 5.2 ? (h - 2.2) / 3 : 1.0);
			if (nat < T.fertMin) nat = T.fertMin; else if (nat > T.fertMax) nat = T.fertMax;
			f += (nat - f) * kFert;
			if (!isFinite(f)) f = 0.5;
			w.fert[i] = float(f);
		}

		// ---------------- vegetation
		const bool isLake = w.water[i] > lake;
		const bool hard = isLake || w.ice[i] > iceT || w.lava[i] > lavaT;
		const int bi = w.biome[i] < Biome::COUNT ? int(w.biome[i]) : 0;
		double cap = 0;
		if (!hard) {
			cap = T.cap[bi] * (0.6 + 0.4 * f) * (1 - fa) * (1 - ur) * (1 - 0.8 * r);
			if (!(cap > 0)) cap = 0; else if (cap > 1) cap = 1;
		}
		const double v0 = w.veg[i], v = v0 > 0 ? (v0 > 1 ? 1.0 : v0) : 0;     // (verifier: +inf veg made NaN)
		double nv = v;
		bool sprouted = false;
		if (v > 0) {
			if (v < cap) nv = cap * v / (v + (cap - v) * eGrow);
			else if (v > cap) {
				nv = v + (cap - v) * (hard ? kKill : kDie);
				if (nv < tiny && cap < tiny) nv = 0;
			}
		} else if (cap > plantMin) {
			int k = 0;
			double a = 0;
			for (int j = 0; j < deg; j++) {
				const int m = nb[j];
				const double vm = w.veg[size_t(m)];
				if (vm > est) k++;
				else if (vm > 0 && vm >= estRel * capOf(w, m, T)) k++;
				else { const double am = air[size_t(m)]; if (am > a) a = am; }
			}
			if (k > 0 || a > 0) {
				double q = (sc0 + (1 - sc0) * sat(cap / scRef)) * (k > 0 ? T.spreadK[k] : T.spreadK[1] * a) *
					std::exp(T.spreadNoise * (double(grain[i]) - 0.5) + T.spreadPatch * (double(patch[i]) - 0.5));
				if (fr < 2) q *= T.spreadFresh[fr];
				q /= 1 + T.spreadSlope * double(w.slope[i]) * sk;
				if (rng.f() < 1 - std::exp(-spreadG * q)) { nv = sprout < cap ? sprout : cap; sprouted = true; }
			}
		}
		if (!isFinite(nv)) nv = v;
		if (nv > 0) greenN++;
		if (sprouted) {
			// planted after the pass, so a sprout never seeds its own neighbours within the call that made it
			S.sproutC[ns] = c;
			S.sproutV[ns++] = float(nv);
			dCol += nv;
			if (nv >= vt0) objects = true;
		} else if (!(nv == v0)) {
			w.veg[i] = float(nv);
			dCol += std::fabs(nv - v);
			if (crosses(v, nv)) objects = true;
		}

		// ---------------- seed pressure over dead ground
		if (cap > plantMin) { if (air[i] != 0) air[i] = 0; }
		else {
			double a = 0;
			for (int j = 0; j < deg; j++) {
				const int m = nb[j];
				const double am = w.veg[size_t(m)] > airVeg ? 1.0 : double(air[size_t(m)]);
				if (am > a) a = am;
			}
			a *= airDecay;
			air[i] = float(a > 1e-4 ? a : 0);
		}

		// ---------------- wildlife
		double F = w.fauna[i];
		if (!(F >= 0) || !isFinite(F)) { F = 0; w.fauna[i] = 0; }          // (verifier) NaN / inf fauna was kept on lakes
		double K;
		if (isLake) K = faunaWater(w, c, T, -1);
		else {
			const double fc = T.faunaCap[bi];
			if (fc > 0 && nv > 0 && !(w.ice[i] > iceT)) {
				const double rel = nv / (0.7 * T.cap[bi] + 0.03);
				K = fc * std::sqrt(rel > 1 ? 1 : rel) * (1 - ur) * (1 - 0.5 * fa) * (1 - r);
				if (!(K > 0)) K = 0; else if (K > 1) K = 1;
			} else K = 0;
		}
		if (F > K) { const double nf = F + (K - F) * kFaunaDie; w.fauna[i] = float(nf > tiny || K > 0 ? nf : 0); }
		else if (F > 0) { if (F < K) w.fauna[i] = float(K * F / (F + (K - F) * eFauna)); }
		else if (K > 0.02) {
			if (!isLake) w.fauna[i] = float(fseed < K ? fseed : K);
			else {
				for (int j = 0; j < deg; j++) if (w.fauna[size_t(nb[j])] > fsrc) { w.fauna[i] = float(fseed < K ? fseed : K); break; }
			}
		} else if (F != 0) w.fauna[i] = 0;

		// a BARREN cell whose lava has cooled and whose ash has thinned is due for a new biome
		if (bi == Biome::BARREN && !(w.lava[i] > lavaT) && !(s > T.scorchBarren)) relife = true;
	}
	for (size_t k = 0; k < ns; k++) w.veg[size_t(S.sproutC[k])] = S.sproutV[k];
	// (v4 mythos) a marvel keeps its rich soil (fert >= 0.8) whatever the natural level says
	{
		int32_t mv[kMaxMarvels];
		const int nm = marvelsOf(w, mv);
		for (int k = 0; k < nm; k++) {
			const int c = mv[k];
			if (c % parts != part || w.elev[size_t(c)] < sea) continue;
			if (!(w.fert[size_t(c)] >= 0.8f)) w.fert[size_t(c)] = 0.8f;
		}
	}

	// Did any slice hold vegetation when it was last visited? (On a dead planet the drift has nothing to look for.)
	bool track = false, alive = true;
	if (parts <= 64) {
		if (S.greenParts != parts || S.green.size() != size_t(parts)) { S.greenParts = parts; S.green.assign(size_t(parts), -1); }
		S.green[size_t(part)] = greenN;
		track = true;
		alive = false;
		for (int k = 0; k < parts; k++) if (S.green[size_t(k)] != 0) { alive = true; break; }
	}

	// ---------------- drifting seeds (a planet-wide process: picks per call, not per slice)
	if (alive) {
		double want = double(T.seedsPerYear) * tickYears * boost;
		const double wantMax = T.driftMax > 0 ? double(T.driftMax) : 0.0;
		if (!(want <= wantMax)) want = wantMax;                     // (verifier) no int overflow for huge seedsPerYear
		if (!(want > 0)) want = 0;
		int picks = int(std::floor(want));
		if (rng.f() < want - picks) picks++;
		if (picks > T.driftMax) picks = T.driftMax;
		const int hops = T.seedHops > 0 ? T.seedHops : 0;
		const double thin = boost > 1 ? sat(std::pow(boost, T.driftBoost - 1)) : 1.0;       // chance that an overland pick takes
		const double thinSea = boost > 1 ? sat(std::pow(boost, T.driftSeaBoost - 1)) : 1.0; // ... and one across water
		for (int p = 0; p < picks; p++) {
			int c = -1;
			for (int tries = 0; tries < 8; tries++) {
				int q = int(rng.f() * n);
				if (q >= n) q = n - 1;
				if (w.elev[size_t(q)] >= sea) { c = q; break; }
			}
			if (c < 0 || w.veg[size_t(c)] != 0) continue;
			if (!(capOf(w, c, T) > plantMin)) continue;
			const int cnt = g.ring(c, hops, S.ringBuf);
			bool found = false;
			for (int k = 1; k < cnt; k++) if (sheds(w, S.ringBuf[size_t(k)], T)) { found = true; break; }
			if (!found) continue;
			if (thin < 1 || thinSea < 1) {
				const double chance = (thin == thinSea || overland(w, c, hops, T)) ? thin : thinSea;
				if (chance < 1 && !(rng.f() < chance)) continue;
			}
			w.veg[size_t(c)] = float(T.seedVeg);
			dCol += 1;
			if (track && S.green[size_t(c % parts)] >= 0) S.green[size_t(c % parts)]++;
			if (T.seedVeg >= vt0) objects = true;
		}
	}

	// colour: rebuild when enough has visibly changed (slow drifts add up over several slices)
	double acc = (S.acc > 0 ? S.acc : 0) + dCol;
	if (acc >= 0.25) { w.dirty.color = true; acc = 0; }
	S.acc = acc;
	if (objects) w.dirty.objects = true;
	if (relife) w.need.life = true;
	bump(w);
}

// ------------------------------------------------------------------ direct edits
void seed(World& w, int c, double ang) {
	if (!w.valid(c) || !usable(w)) return;
	const LifeTune& T = tuneOf(w);
	if (!(ang >= 0)) ang = 0.12;
	LifeScratch& S = scr(w);
	const int cnt = w.geo->within(c, ang, S.list);
	const float sv = f32up(T.seedMin);
	int k = 0;
	for (int j = 0; j < cnt; j++) {
		const int m = S.list[size_t(j)];
		if (!(capOf(w, m, T) > T.plantMin)) continue;
		if (w.veg[size_t(m)] < sv || !isFinite(w.veg[size_t(m)])) { w.veg[size_t(m)] = sv; k++; }
	}
	if (k) { w.dirty.color = true; w.dirty.objects = true; greenUnknown(w); bump(w); }
}

void bloom(World& w, int c, double ang, double amount) {
	if (!w.valid(c) || !usable(w) || !(amount > 0)) return;
	const LifeTune& T = tuneOf(w);
	const Geo& g = *w.geo;
	if (!(ang >= 0)) ang = 0.08;
	LifeScratch& S = scr(w);
	const int cnt = g.within(c, ang, S.list);
	const double sea = w.seaLevel;
	int k = 0;
	bool objects = false;
	for (int j = 0; j < cnt; j++) {
		const int m = S.list[size_t(j)];
		const size_t mi = size_t(m);
		if (w.elev[mi] < sea) continue;
		const double d = ang > 0 ? g.angle(c, m) / ang : 0;
		const double fall = d >= 1 ? 0 : 1 - d * d * (3 - 2 * d);     // 1 at the centre, 0 at the rim
		const double a = amount * fall;
		if (!(a > 0)) continue;
		const double f = w.fert[mi];
		if (f < T.fertCap) w.fert[mi] = float(f + a > T.fertCap ? T.fertCap : f + a);
		else if (!isFinite(f)) w.fert[mi] = 0.5f;
		const double s = w.scorch[mi];
		if (s > 0) w.scorch[mi] = float(s > a ? s - a : 0);
		const double cap = capOf(w, m, T), v = w.veg[mi] > 0 ? double(w.veg[mi]) : 0.0;
		if (cap > T.plantMin && v < cap) {
			const double nv = v + a > cap ? cap : v + a;
			w.veg[mi] = float(nv);
			if (crosses(v, nv)) objects = true;
		}
		k++;
	}
	if (k) { w.dirty.color = true; if (objects) w.dirty.objects = true; greenUnknown(w); bump(w); }
}

void fill(World& w, double frac) {
	if (!usable(w)) return;
	const LifeTune& T = tuneOf(w);
	frac = sat(frac);
	if (!(frac >= 0)) frac = 1;
	const double sea = w.seaLevel;
	for (int c = 0; c < w.n; c++) {
		const size_t i = size_t(c);
		if (w.elev[i] < sea) { w.veg[i] = 0; w.fauna[i] = float(faunaWater(w, c, T, sea - double(w.elev[i]))); continue; }
		const double v = frac * capOf(w, c, T);
		w.veg[i] = float(v);
		w.fauna[i] = float(w.water[i] > T.lakeWater ? faunaWater(w, c, T, -1) : faunaLand(w, c, T, v));
	}
	w.dirty.color = true;
	w.dirty.objects = true;
	greenUnknown(w);
	bump(w);
}

// ------------------------------------------------------------------ habitability
void habitability(World& w) {
	if (!usable(w)) { w.cache.landHab.clear(); w.need.life = false; return; }
	const LifeTune& T = w.tune<LifeTune>();
	const int n = w.n;
	const double sea = w.seaLevel, sk = slopeScale(w, T);
	const double c0 = T.habColdZero, c1 = T.habTLo, h0 = T.habTHi, h1 = T.habHotZero;
	const double r0 = T.habRain[0], r1 = T.habRain[1], rs = r1 > r0 ? 1 / (r1 - r0) : 0;
	const double s0 = T.habSlope[0], s1 = T.habSlope[1], ss = s1 > s0 ? 1 / (s1 - s0) : 0;
	const int freshHops = std::min(2, T.habFreshHops);
	const std::vector<int32_t>& lid = w.cache.landId;
	const bool useLid = lid.size() == size_t(n);
	int kLand = 0;
	if (useLid) {
		kLand = std::max(0, w.cache.landCount);
		for (int c = 0; c < n; c++) if (lid[size_t(c)] >= kLand) kLand = lid[size_t(c)] + 1;
	}
	std::vector<int32_t>& landHab = w.cache.landHab;
	landHab.assign(size_t(kLand), 0);
	for (int c = 0; c < n; c++) {
		const size_t i = size_t(c);
		double v = 0;
		const int b = w.biome[i];
		if (!(w.elev[i] < sea) && !(w.water[i] > T.lakeWater) && !(w.ice[i] > T.iceBiome) && !(w.lava[i] > T.lavaBarren) &&
		    b != Biome::ICE && b != Biome::ROCK && b != Biome::SNOWPEAK && b != Biome::BARREN && b != Biome::SEA &&
		    b != Biome::SHALLOWS && b != Biome::SEAICE) {
			const double t = w.temp[i];
			const double ts = sm(c0, c1, t) * (1 - sm(h0, h1, t));
			const double wr = sat((double(w.moist[i]) - r0) * rs);
			const int fr = w.fresh[i];
			const double wf = fr <= freshHops ? T.habFresh[fr] : 0.0;
			const double wat = wr > wf ? wr : wf;
			const double fe = T.habFert + (1 - T.habFert) * sat(w.fert[i]);
			const double sl = sat(1 - (double(w.slope[i]) * sk - s0) * ss);
			const int co = w.coast[i];
			const double cb = co < 3 ? T.habCoast[co] : 1.0;
			v = ts * wat * fe * sl * cb * (1 - sat(w.rad[i]));
			if (!(v > 0)) v = 0; else if (v > 1) v = 1;
		}
		w.hab[i] = float(v);
		if (useLid && v > T.habMin) { const int l = lid[i]; if (l >= 0 && l < kLand) landHab[size_t(l)]++; }
	}
	w.need.life = false;
}

// ------------------------------------------------------------------ cover
Cover cover(const World& w) {
	if (!usable(w)) return Cover{};
	LifeScratch& S = scr(w);
	if (S.coverOk && S.coverTick == w.tick && S.coverVer == S.ver && S.coverSea == w.seaLevel) return S.coverVal;
	const LifeTune& T = tuneOf(w);
	const int n = w.n;
	const double sea = w.seaLevel, fv = T.forestVeg;
	int land = 0, forest = 0;
	double sumV = 0, sumC = 0;
	for (int c = 0; c < n; c++) {
		const size_t i = size_t(c);
		if (w.elev[i] < sea) continue;
		land++;
		const double v = w.veg[i];
		if (v >= fv) forest++;
		const double k = capOf(w, c, T);
		if (k > T.plantMin && v > 0) sumV += v < k ? v : k;
		if (k > T.plantMin) sumC += k;
	}
	Cover out;
	out.land = n > 0 ? double(land) / n : 0;
	out.veg = sumC > 0 ? sumV / sumC : 0;
	out.forest = land > 0 ? double(forest) / land : 0;
	S.coverOk = true;
	S.coverTick = w.tick;
	S.coverVer = S.ver;
	S.coverSea = w.seaLevel;
	S.coverVal = out;
	return out;
}

} // namespace life
} // namespace dm
