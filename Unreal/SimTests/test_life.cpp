// DEMIURGE — SimTests/test_life.cpp : dm::life (SPEC-SIM 3.5, BUILD.md A5). Port of tests/life.test.js plus
// the A5 acceptance checks. Every other module is a neutral stub in `run.ps1 life`: worlds are set up
// directly (helper planets, hand-built plains). Prints the biome histogram, spread, fire and timing numbers.
// Maps (shots/ue-life-*.png) are written by the tests whose names start with "maps:".
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numeric>
#include <set>
#include <string>
#include <vector>

#include "Life.h"
#include "MapImg.h"
#include "State.h"
#include "TestFramework.h"
#include "TestWorld.h"

using namespace dm;
using namespace dmt;

namespace {

namespace mi = dm::mapimg;

LifeTune& TL(World& w) { return w.tune<LifeTune>(); }
bool isLand(const World& w, int c) { return w.elev[size_t(c)] >= w.seaLevel; }
std::string f2(double x, int d = 2) { char b[64]; std::snprintf(b, sizeof b, "%.*f", d, x); return b; }
void say(const std::string& s) { std::printf("  %s\n", s.c_str()); std::fflush(stdout); }

// ------------------------------------------------------------------ world builders
struct PlainOpts {
	uint32_t seed = 1;
	double elev = 0.8, temp = 15, moist = 0.55, fert = 0.6, veg = 0;
	uint8_t biome = Biome::FOREST;
};
// an all-land plain carrying one biome everywhere: the lattice the fire and growth rules are measured on
std::unique_ptr<World> plain(int L, const PlainOpts& o = {}) {
	auto w = makeWorld({ .L = L, .seed = o.seed, .land = 1, .basics = false });
	std::fill(w->elev.begin(), w->elev.end(), float(o.elev));
	w->seaLevel = 0; w->vars.seaBase = 0;
	fillBasics(*w);
	std::fill(w->ice.begin(), w->ice.end(), 0.0f);
	std::fill(w->water.begin(), w->water.end(), 0.0f);
	std::fill(w->wet.begin(), w->wet.end(), 0.0f);
	std::fill(w->scorch.begin(), w->scorch.end(), 0.0f);
	std::fill(w->fire.begin(), w->fire.end(), 0.0f);
	std::fill(w->fauna.begin(), w->fauna.end(), 0.0f);
	std::fill(w->temp.begin(), w->temp.end(), float(o.temp));
	std::fill(w->moist.begin(), w->moist.end(), float(o.moist));
	std::fill(w->fert.begin(), w->fert.end(), float(o.fert));
	std::fill(w->fresh.begin(), w->fresh.end(), uint8_t(255));
	std::fill(w->biome.begin(), w->biome.end(), o.biome);
	std::fill(w->veg.begin(), w->veg.end(), float(o.veg));
	life::rebuild(*w);
	return w;
}
// a helper planet with Life's own biomes; `bare` strips the stand-in vegetation
std::unique_ptr<World> planet(int L, uint32_t seed, bool bare = false, double land = 0.4) {
	auto w = makeWorld({ .L = L, .seed = seed, .land = land });
	life::classify(*w);
	if (bare) { std::fill(w->veg.begin(), w->veg.end(), 0.0f); std::fill(w->fauna.begin(), w->fauna.end(), 0.0f); }
	return w;
}
void advance(World& w, int ticks, int parts) {
	for (int i = 0; i < ticks; i++) { w.tick++; life::burn(w); life::step(w, int(w.tick % parts), parts); }
}
void stepOnly(World& w, int ticks, int parts) {
	for (int i = 0; i < ticks; i++) { w.tick++; life::step(w, int(w.tick % parts), parts); }
}
// hop distance from c0 over every cell (BFS)
std::vector<int> hopsFrom(const World& w, int c0) {
	const Geo& g = *w.geo;
	std::vector<int> hops(size_t(w.n), -1), q{ c0 };
	hops[size_t(c0)] = 0;
	for (size_t h = 0; h < q.size(); h++) {
		const int c = q[h];
		for (int j = 0; j < g.nbrN[size_t(c)]; j++) {
			const int m = g.nbr[size_t(6 * c + j)];
			if (hops[size_t(m)] < 0) { hops[size_t(m)] = hops[size_t(c)] + 1; q.push_back(m); }
		}
	}
	return hops;
}
// connected land components by flood fill
struct LandIds { std::vector<int> id; std::vector<int> size; };
LandIds landIds(const World& w) {
	const Geo& g = *w.geo;
	LandIds r;
	r.id.assign(size_t(w.n), -1);
	std::vector<int> q;
	for (int s = 0; s < w.n; s++) {
		if (r.id[size_t(s)] >= 0 || !isLand(w, s)) continue;
		const int id = int(r.size.size());
		q.assign(1, s);
		r.id[size_t(s)] = id;
		for (size_t h = 0; h < q.size(); h++) {
			const int c = q[h];
			for (int j = 0; j < g.nbrN[size_t(c)]; j++) {
				const int m = g.nbr[size_t(6 * c + j)];
				if (r.id[size_t(m)] < 0 && isLand(w, m)) { r.id[size_t(m)] = id; q.push_back(m); }
			}
		}
		r.size.push_back(int(q.size()));
	}
	return r;
}
int biggest(const LandIds& ids) {
	int b = 0;
	for (int i = 1; i < int(ids.size.size()); i++) if (ids.size[size_t(i)] > ids.size[size_t(b)]) b = i;
	return b;
}
// a well-vegetated cell near the middle of a landmass
int heart(const World& w, const LandIds& ids, int k) {
	const std::vector<float>& p = w.geo->pos;
	double x = 0, y = 0, z = 0;
	for (int c = 0; c < w.n; c++) if (ids.id[size_t(c)] == k) { x += p[size_t(3 * c)]; y += p[size_t(3 * c + 1)]; z += p[size_t(3 * c + 2)]; }
	int best = -1;
	double bd = -1e300;
	for (int c = 0; c < w.n; c++) {
		if (ids.id[size_t(c)] != k || !(life::capacity(w, c) > 0.3)) continue;
		const double d = p[size_t(3 * c)] * x + p[size_t(3 * c + 1)] * y + p[size_t(3 * c + 2)] * z;
		if (d > bd) { bd = d; best = c; }
	}
	return best;
}
struct LandCov { int plant = 0, bare = 0; double cover = 0; };
LandCov landCover(World& w, const LandIds& ids, int k) {
	LandCov r;
	double sc = 0, sv = 0;
	const double pm = TL(w).plantMin;
	for (int c = 0; c < w.n; c++) {
		if (ids.id[size_t(c)] != k) continue;
		const double cap = life::capacity(w, c);
		if (!(cap > pm)) continue;
		r.plant++; sc += cap; sv += std::min(cap, double(w.veg[size_t(c)]));
		if (w.veg[size_t(c)] == 0) r.bare++;
	}
	r.cover = sc > 0 ? sv / sc : 0;
	return r;
}
int burning(const World& w) { int k = 0; for (int c = 0; c < w.n; c++) if (w.fire[size_t(c)] > 0) k++; return k; }
std::string firesMatch(const World& w) {
	std::set<int> seen;
	for (int c : w.cache.fires) {
		if (c < 0 || c >= w.n) return "listed cell " + std::to_string(c) + " out of range";
		if (!(w.fire[size_t(c)] > 0)) return "listed cell " + std::to_string(c) + " is not burning";
		if (!seen.insert(c).second) return "cell " + std::to_string(c) + " listed twice";
	}
	if (int(seen.size()) != burning(w)) return "fires list " + std::to_string(w.cache.fires.size()) + " but " + std::to_string(burning(w)) + " cells burn";
	return "";
}
// burn one fire to the end: cells scorched, ticks, most cells burning at once (-1 size: did not ignite)
struct BurnOut { int size = -1, ticks = 0, peak = 0; };
BurnOut burnOut(World& w, int c, int limit = 20000) {
	BurnOut r;
	if (!life::ignite(w, c)) return r;
	r.peak = 1;
	while (!w.cache.fires.empty() && r.ticks < limit) {
		w.tick++; life::burn(w); r.ticks++;
		r.peak = std::max(r.peak, int(w.cache.fires.size()));
	}
	r.size = 0;
	for (int i = 0; i < w.n; i++) if (w.scorch[size_t(i)] > 0) r.size++;
	return r;
}
struct Sizes {
	int n = 0; std::vector<int> s; double mean = 0; int median = 0, p90 = 0, max = 0, peak = 0;
	double over(int x) const { int k = 0; for (int v : s) if (v > x) k++; return s.empty() ? 0 : double(k) / s.size(); }
};
Sizes fireSizes(int L, double veg, double moist, int trials, uint32_t seed = 4242) {
	auto w = plain(L, { .moist = moist, .veg = veg });
	Rng pick(seed);
	Sizes r;
	r.n = w->n;
	for (int i = 0; i < trials; i++) {
		std::fill(w->veg.begin(), w->veg.end(), float(veg));
		std::fill(w->scorch.begin(), w->scorch.end(), 0.0f);
		std::fill(w->fire.begin(), w->fire.end(), 0.0f);
		life::rebuild(*w);
		const BurnOut b = burnOut(*w, pick.integer(0, w->n - 1));
		r.s.push_back(b.size);
		r.peak = std::max(r.peak, b.peak);
	}
	std::sort(r.s.begin(), r.s.end());
	auto q = [&](double p) { return r.s[std::min(r.s.size() - 1, size_t(std::floor(p * r.s.size())))]; };
	r.mean = std::accumulate(r.s.begin(), r.s.end(), 0.0) / r.s.size();
	r.median = q(0.5); r.p90 = q(0.9); r.max = r.s.back();
	return r;
}

// ------------------------------------------------------------------ map colours
// what the ground looks like: bare soil by biome, green by vegetation, fire and ash on top
mi::Rgb look(const World& w, int c) {
	const size_t i = size_t(c);
	if (!isLand(w, c)) return w.ice[i] > 0.5 ? mi::Rgb{ 200, 220, 235 } : (w.seaLevel - w.elev[i] > 0.6 ? mi::Rgb{ 18, 34, 78 } : mi::Rgb{ 34, 66, 118 });
	if (w.fire[i] > 0) return { 255, 140, 20 };
	if (w.ice[i] > 0.5) return { 232, 238, 244 };
	if (w.water[i] > 0.03) return { 70, 120, 210 };
	mi::Rgb base{ 150, 122, 92 };
	switch (w.biome[i]) {
		case Biome::DESERT: base = { 214, 190, 132 }; break;
		case Biome::ROCK: base = { 122, 114, 110 }; break;
		case Biome::SNOWPEAK: base = { 246, 246, 250 }; break;
		case Biome::BEACH: base = { 224, 208, 164 }; break;
		case Biome::TUNDRA: base = { 150, 142, 124 }; break;
		case Biome::BARREN: base = { 62, 52, 50 }; break;
		case Biome::SAVANNA: base = { 176, 150, 104 }; break;
		default: break;
	}
	const double s = w.scorch[i];
	if (s > 0) base = { base.r * (1 - 0.75 * s), base.g * (1 - 0.8 * s), base.b * (1 - 0.8 * s) };
	const double v = std::min(1.0, double(w.veg[i]));
	if (!(v > 0)) return base;
	const double t = std::min(1.0, 0.2 + 0.8 * std::sqrt(v));
	const mi::Rgb gr{ 78 - 56 * v, 152 - 62 * v, 54 - 24 * v };
	return { base.r + (gr.r - base.r) * t, base.g + (gr.g - base.g) * t, base.b + (gr.b - base.b) * t };
}
// orthographic close-up centred on a cell
bool zoom(const World& w, const std::string& file, int cell, double half, const std::function<mi::Rgb(int)>& colorFn, int size = 720) {
	const Geo& g = *w.geo;
	const Vec3 d = g.dir(cell);
	Vec3 e, nn;
	g.basis(cell, e, nn);
	std::vector<uint8_t> rgb(size_t(size) * size * 3);
	const double sh = std::sin(half);
	int hint = cell;
	std::vector<mi::Rgb> cache(size_t(w.n));
	std::vector<char> have(size_t(w.n), 0);
	for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
		const double u = ((x + 0.5) / size * 2 - 1) * sh, v = (1 - (y + 0.5) / size * 2) * sh;
		const double k = std::sqrt(std::max(0.0, 1 - u * u - v * v));
		const int c = g.cellAt(d.x * k + e.x * u + nn.x * v, d.y * k + e.y * u + nn.y * v, d.z * k + e.z * u + nn.z * v, hint);
		hint = c;
		if (!have[size_t(c)]) { cache[size_t(c)] = colorFn(c); have[size_t(c)] = 1; }
		const mi::Rgb col = cache[size_t(c)];
		const size_t o = (size_t(y) * size + x) * 3;
		rgb[o] = uint8_t(std::clamp(col.r, 0.0, 255.0));
		rgb[o + 1] = uint8_t(std::clamp(col.g, 0.0, 255.0));
		rgb[o + 2] = uint8_t(std::clamp(col.b, 0.0, 255.0));
	}
	return mi::writePng(file, size, size, rgb);
}

// Five helper worlds. The helper's stand-in climate has hardly any arid land, so two of the five are dried
// out by scaling that stand-in; one is warmed, one cooled with sea ice.
struct Five { const char* name; uint32_t seed; double land; double temp; double dry; bool seaIce; };
const Five kFive[] = {
	{ "temperate", 1, 0.4, 0, 0, false },
	{ "warm seas", 2, 0.3, 5, 0, false },
	{ "arid", 3, 0.6, 3, 0.3, false },
	{ "cold", 4, 0.4, -6, 0, true },
	{ "steppe", 5, 0.5, 0, 0.55, false },
};
std::unique_ptr<World> fiveWorld(const Five& sp, int L = 5) {
	auto w = makeWorld({ .L = L, .seed = sp.seed, .land = sp.land, .basics = false });
	w->globalTemp = sp.temp; fillBasics(*w); w->globalTemp = 0;
	if (sp.dry > 0) for (int c = 0; c < w->n; c++) if (isLand(*w, c)) w->moist[size_t(c)] *= float(sp.dry);
	if (sp.seaIce) for (int c = 0; c < w->n; c++) if (!isLand(*w, c) && std::fabs(w->geo->lat[size_t(c)]) > 1.15) w->ice[size_t(c)] = 0.8f;
	return w;
}

} // namespace

// ================================================================== API
DM_TEST("life: API and the spec's tuning table") {
	auto w = plain(3);
	const LifeTune& T = TL(*w);
	const double want[16] = { 0, 0, 0.05, 0.05, 0.35, 0.3, 0.9, 1, 0.75, 0.15, 0, 0.03, 0, 0.7, 0, 0 };
	for (int b = 0; b < 16; b++) eq(T.cap[b], want[b], std::string("biomeCapacity ") + kBiomeNames[b]);
	eq(T.maxBurning, 1500); eq(T.fireP, 0.55); eq(T.dryA, 1.2); eq(T.dryB, 1.6); eq(T.seedsPerYear, 8); eq(T.seedHops, 12);
	eq(T.seedVeg, 0.08); eq(T.seedMin, 0.35); eq(T.radDecay, 0.015); eq(T.farmDecay, 0.05); eq(T.urbanDecay, 0.01);
}

// ================================================================== biomes
DM_TEST("classify: sea biomes only on sea cells, land biomes only on land; depth and ice rules") {
	int seaIce = 0;
	for (const Five& sp : kFive) {
		auto w = fiveWorld(sp);
		w->dirty.color = false; w->dirty.objects = false;
		life::classify(*w);
		ok(w->dirty.color && w->dirty.objects, "classify sets dirty.color and dirty.objects");
		int bad = 0;
		for (int c = 0; c < w->n; c++) {
			const int b = w->biome[size_t(c)];
			const bool seaB = b == Biome::SEA || b == Biome::SHALLOWS || b == Biome::SEAICE;
			if (isLand(*w, c)) { if (seaB && bad++ < 3) fail(std::string(sp.name) + ": land cell " + std::to_string(c) + " classified " + kBiomeNames[b]); }
			else {
				const int want = w->ice[size_t(c)] > 0.5 ? Biome::SEAICE : (w->seaLevel - w->elev[size_t(c)] > 0.6 ? Biome::SEA : Biome::SHALLOWS);
				if (b != want && bad++ < 3) fail(std::string(sp.name) + ": sea cell " + std::to_string(c) + " is " + kBiomeNames[b]);
				if (b == Biome::SEAICE) seaIce++;
			}
		}
	}
	ok(seaIce > 20, "sea ice appears where the sea is iced over (" + std::to_string(seaIce) + " cells)");
}

DM_TEST("classify: every land biome appears across 5 helper worlds, in believable proportions") {
	double tot[16] = {};
	double land = 0;
	std::string head = "biome shares, % of land (5 helper worlds, L = 5):\n    world      ";
	for (int b = 2; b <= 14; b++) { char s[16]; std::snprintf(s, sizeof s, "%7.6s", kBiomeNames[b]); head += s; }
	say(head);
	for (const Five& sp : kFive) {
		auto w = fiveWorld(sp);
		// a lava field and a burnt-out patch (the only way BARREN exists), on the first world
		if (sp.seed == 1) {
			const int c0 = findLand(*w, [&](int c) { return w->temp[size_t(c)] > 10 && w->coast[size_t(c)] > 2; });
			std::vector<int32_t> buf;
			const int k = w->geo->ring(c0, 2, buf);
			for (int i = 0; i < k; i++) {
				const int m = buf[size_t(i)];
				if (!isLand(*w, m)) continue;
				if (i % 2) w->lava[size_t(m)] = 0.6f; else { w->scorch[size_t(m)] = 0.95f; w->veg[size_t(m)] = 0; }
			}
		}
		life::classify(*w);
		double h[16] = {}, l = 0;
		for (int c = 0; c < w->n; c++) if (isLand(*w, c)) { h[w->biome[size_t(c)]]++; l++; }
		char line[256];
		std::snprintf(line, sizeof line, "  %-11s", sp.name);
		std::string s = line;
		for (int b = 2; b <= 14; b++) { std::snprintf(line, sizeof line, "%7.1f", 100 * h[b] / l); s += line; }
		say(s);
		for (int b = 0; b < 16; b++) tot[b] += h[b];
		land += l;
	}
	{
		std::string s = "  all five   ";
		char line[32];
		for (int b = 2; b <= 14; b++) { std::snprintf(line, sizeof line, "%7.1f", 100 * tot[b] / land); s += line; }
		say(s);
	}
	for (int b = 2; b <= 14; b++) ok(tot[b] > 0, std::string(kBiomeNames[b]) + " never appears");
	auto share = [&](int b) { return tot[b] / land; };
	const double woods = share(Biome::FOREST) + share(Biome::RAINFOREST) + share(Biome::TAIGA), open = share(Biome::GRASSLAND) + share(Biome::SAVANNA);
	ok(woods > 0.15 && woods < 0.7, "forest + rainforest + taiga share " + f2(woods));
	ok(open > 0.04 && open < 0.5, "grassland + savanna share " + f2(open));
	ok(share(Biome::DESERT) > 0.01 && share(Biome::DESERT) < 0.35, "desert share " + f2(share(Biome::DESERT)));
	ok(share(Biome::ICE) > 0.05 && share(Biome::ICE) < 0.45, "ice share " + f2(share(Biome::ICE)));
	ok(share(Biome::TUNDRA) > 0.01 && share(Biome::TUNDRA) < 0.2, "tundra share " + f2(share(Biome::TUNDRA)));
	ok(share(Biome::ROCK) + share(Biome::SNOWPEAK) < 0.25, "rock + snow share");
	ok(share(Biome::BEACH) < 0.08 && share(Biome::WETLAND) < 0.1 && share(Biome::BARREN) < 0.01, "beach / wetland / barren stay rare");
}

DM_TEST("classify: the land rules one by one") {
	auto pw = plain(4);
	World& w = *pw;
	const double sk = w.geo->step / TL(w).refStep;       // slopes are judged at the reference cell size
	std::fill(w.coast.begin(), w.coast.end(), uint8_t(255));
	struct Case { int c; int want; };
	std::vector<Case> cases;
	int next = 20;
	auto put = [&](int want, const std::function<void(size_t)>& f) { const int c = next; next += 7; f(size_t(c)); cases.push_back({ c, want }); };
	auto tm = [&](double t, double m) { return [&w, t, m](size_t c) { w.temp[c] = float(t); w.moist[c] = float(m); }; };
	// the Whittaker table, well away from every threshold (the jitter is at most +-0.075 moist, +-1.5 C)
	put(Biome::RAINFOREST, tm(25, 0.95));
	put(Biome::FOREST, tm(25, 0.57));
	put(Biome::SAVANNA, tm(25, 0.30));
	put(Biome::DESERT, tm(25, 0.03));
	put(Biome::DESERT, tm(13, 0.01));
	put(Biome::GRASSLAND, tm(13, 0.22));
	put(Biome::FOREST, tm(13, 0.60));
	put(Biome::TAIGA, tm(5, 0.60));
	put(Biome::TAIGA, tm(0, 0.60));
	put(Biome::TUNDRA, tm(0, 0.08));
	put(Biome::TUNDRA, tm(-8, 0.50));
	// special ground
	put(Biome::BARREN, [&](size_t c) { w.lava[c] = 0.5f; });
	put(Biome::BARREN, [&](size_t c) { w.scorch[c] = 0.9f; w.veg[c] = 0; });
	put(Biome::FOREST, [&](size_t c) { w.scorch[c] = 0.9f; w.veg[c] = 0.3f; w.moist[c] = 0.6f; });      // scorched but still green
	put(Biome::ICE, [&](size_t c) { w.ice[c] = 0.9f; w.temp[c] = -12; });
	put(Biome::SNOWPEAK, [&](size_t c) { w.elev[c] = 4; w.temp[c] = -6; });
	put(Biome::SNOWPEAK, [&](size_t c) { w.elev[c] = 4; w.temp[c] = -12; w.ice[c] = 1; });           // a glaciated summit
	put(Biome::ROCK, [&](size_t c) { w.elev[c] = 5.2f; w.temp[c] = 4; });                              // very high
	put(Biome::ROCK, [&](size_t c) { w.slope[c] = float(2.2 * sk); });                                  // very steep
	put(Biome::FOREST, [&](size_t c) { w.slope[c] = float(0.8 * sk); w.moist[c] = 0.6f; });           // hilly, not rock
	put(Biome::BEACH, [&](size_t c) { w.elev[c] = 0.04f; w.coast[c] = 1; });
	put(Biome::FOREST, [&](size_t c) { w.elev[c] = 0.04f; w.coast[c] = 3; w.moist[c] = 0.4f; });       // low but not at the sea
	put(Biome::WETLAND, [&](size_t c) { w.elev[c] = 0.3f; w.moist[c] = 0.8f; w.fresh[c] = 0; });
	put(Biome::FOREST, [&](size_t c) { w.elev[c] = 0.3f; w.moist[c] = 0.8f; w.fresh[c] = 9; });         // wet lowland far from water
	put(Biome::FOREST, [&](size_t c) { w.elev[c] = 0.3f; w.moist[c] = 0.8f; w.fresh[c] = 0; w.slope[c] = float(0.8 * sk); });   // not flat
	life::classify(w);
	for (const Case& k : cases) eq(std::string(kBiomeNames[w.biome[size_t(k.c)]]), std::string(kBiomeNames[k.want]), "case at cell " + std::to_string(k.c));
}

DM_TEST("classify: static and deterministic (same world twice, two worlds of one seed)") {
	auto a = planet(4, 3), b = planet(4, 3);
	const std::vector<uint8_t> first = a->biome;
	life::classify(*a);
	ok(a->biome == first, "reclassifying changes nothing");
	ok(a->biome == b->biome, "two worlds of one seed agree");
}

DM_TEST("classify: heights are measured from climate::datum (a world at sea level -9 is not all rock)") {
	auto w = planet(4, 2);
	const std::vector<uint8_t> ref = w->biome;
	// the same land under a sea 9 units lower whose datum (seaBase) is still 0: same land biomes
	for (int c = 0; c < w->n; c++) if (!isLand(*w, c)) w->elev[size_t(c)] = -10;
	w->seaLevel = -9;
	life::classify(*w);
	int land = 0, same = 0;
	for (int c = 0; c < w->n; c++) if (w->elev[size_t(c)] >= 0) { land++; if (w->biome[size_t(c)] == ref[size_t(c)] || ref[size_t(c)] == Biome::BEACH || ref[size_t(c)] == Biome::WETLAND) same++; }
	ok(same > 0.95 * land, "land biomes unchanged by the sea level when the datum is fixed (" + std::to_string(same) + " of " + std::to_string(land) + ")");
}

// ================================================================== capacity
DM_TEST("capacity: biomeCapacity x (0.6 + 0.4 fert) x (1 - farm) x (1 - urban) x (1 - 0.8 rad); 0 on sea, lakes, ice") {
	auto pw = plain(4, { .fert = 0.5 });
	World& w = *pw;
	near(life::capacity(w, 100), 0.9 * 0.8, 1e-6, "plain forest");
	w.farm[101] = 0.25f; w.urban[101] = 0.2f; w.rad[101] = 0.5f;
	near(life::capacity(w, 101), 0.9 * 0.8 * 0.75 * 0.8 * 0.6, 1e-6, "farm, town and fallout");
	w.fert[102] = 1.5f; w.biome[102] = Biome::RAINFOREST;
	near(life::capacity(w, 102), 1, 1e-6, "never above 1");
	for (int b = 0; b < 16; b++) { w.biome[103] = uint8_t(b); near(life::capacity(w, 103), TL(w).cap[b] * 0.8, 1e-6, kBiomeNames[b]); }
	w.water[104] = 0.5f; eq(life::capacity(w, 104), 0.0, "lake cell");
	w.ice[105] = 0.8f; eq(life::capacity(w, 105), 0.0, "ice");
	w.lava[106] = 0.4f; eq(life::capacity(w, 106), 0.0, "lava");
	w.elev[107] = -1; eq(life::capacity(w, 107), 0.0, "sea");
	w.farm[108] = 1; eq(life::capacity(w, 108), 0.0, "fully farmed");
	for (int bad : { -1, w.n, w.n + 5, -100000 }) eq(life::capacity(w, bad), 0.0, "bad cell " + std::to_string(bad));
}

// ================================================================== vegetation
DM_TEST("vegetation: a barren planet stays barren for 2,000 ticks") {
	auto w = planet(4, 2, true);
	w->vars.lifeBoost = 25; w->phase = Phase::Life;
	advance(*w, 2000, 2);
	double veg = 0, fauna = 0;
	for (int c = 0; c < w->n; c++) { veg += w->veg[size_t(c)]; if (isLand(*w, c)) fauna += w->fauna[size_t(c)]; }
	eq(veg, 0.0, "no vegetation without a seed");
	eq(fauna, 0.0, "no land animals without vegetation");
	eq(checkFinite(*w), std::string());
}

DM_TEST("seed: veg >= 0.35 on plantable land within ang, nothing else touched") {
	auto pw = planet(4, 1, true);
	World& w = *pw;
	const LandIds ids = landIds(w);
	const int c0 = heart(w, ids, biggest(ids));
	if (!ok(c0 >= 0, "a heart cell")) return;
	w.dirty.color = false; w.dirty.objects = false;
	life::seed(w, c0, 0.2);
	int inside = 0, bad = 0;
	for (int c = 0; c < w.n; c++) {
		const bool nearIt = w.geo->angle(c0, c) <= 0.2, plantable = life::capacity(w, c) > TL(w).plantMin;
		if (nearIt && plantable) { inside++; if (!(w.veg[size_t(c)] >= 0.35) && bad++ < 3) fail("plantable cell " + std::to_string(c) + " inside the brush has veg " + std::to_string(w.veg[size_t(c)])); }
		else if (w.veg[size_t(c)] != 0 && bad++ < 3) fail("cell " + std::to_string(c) + (nearIt ? " (not plantable)" : " (outside the brush)") + " was planted");
	}
	ok(inside >= 10, "the brush covers land (" + std::to_string(inside) + " cells)");
	ok(w.dirty.color && w.dirty.objects, "dirty flags");
	const std::vector<float> before = w.veg;
	life::seed(w, c0, 0.2);
	ok(w.veg == before, "seeding twice plants nothing new");
	w.veg[size_t(c0)] = 0.9f;
	life::seed(w, c0, 0.05);
	near(w.veg[size_t(c0)], 0.9, 1e-6, "never lowers vegetation");
	life::seed(w, -1, 0.2);
	life::seed(w, w.n, 0.2);
	life::seed(w, c0, std::nan(""));
	eq(checkFinite(w), std::string());
}

DM_TEST("vegetation: from one seed the green crosses its continent (boost 25, 2 slices) and keeps spreading") {
	auto pw = planet(5, 2, true);
	World& w = *pw;
	const LandIds ids = landIds(w);
	const int k = biggest(ids), c0 = heart(w, ids, k);
	w.vars.lifeBoost = 25; w.phase = Phase::Life;
	life::seed(w, c0, 0.12);
	LandCov at[481];
	std::string line = "spread on a continent of " + std::to_string(ids.size[size_t(k)]) + " cells (L = 5):";
	double prev = 0;
	for (int t = 1; t <= 480; t++) {
		w.tick++; life::burn(w); life::step(w, int(w.tick % 2), 2);
		if (t == 20 || t % 60 == 0) {
			at[t] = landCover(w, ids, k);
			line += " t" + std::to_string(t) + " " + f2(100 * at[t].cover, 1) + "%";
			ok(at[t].cover >= prev - 1e-6, "cover never falls back (tick " + std::to_string(t) + ")");
			prev = at[t].cover;
		}
	}
	say(line + "; bare plantable cells left " + std::to_string(at[480].bare) + " of " + std::to_string(at[480].plant));
	ok(at[20].cover < 0.25, "it is a wave, not a switch: " + f2(at[20].cover) + " after 20 ticks");
	ok(at[240].cover >= 0.25, "at least 25 % of capacity within 240 ticks (got " + f2(at[240].cover) + ")");
	ok(at[120].cover > at[60].cover && at[240].cover > at[120].cover, "still spreading");
	ok(at[480].cover > 0.97, "the continent ends up green (" + f2(at[480].cover) + ")");
	ok(at[480].bare <= 0.005 * at[480].plant, "every plantable region is reached (" + std::to_string(at[480].bare) + " bare cells left)");
	eq(checkFinite(w), std::string());
}

DM_TEST("vegetation at game size (L = 6): one seed greens its continent within the life phase, nothing left out") {
	auto pw = planet(6, 2, true);
	World& w = *pw;
	const LandIds ids = landIds(w);
	const int k = biggest(ids), c0 = heart(w, ids, k);
	w.vars.lifeBoost = 25; w.phase = Phase::Life;
	life::seed(w, c0, 0.12);
	int planted = 0;
	for (int c = 0; c < w.n; c++) if (w.veg[size_t(c)] > 0) planted++;
	LandCov at[481];
	std::string line = "spread on a continent of " + std::to_string(ids.size[size_t(k)]) + " cells (L = 6, " + std::to_string(planted) + " cells seeded):";
	for (int t = 1; t <= 480; t++) {
		w.tick++; life::burn(w); life::step(w, int(w.tick % 2), 2);
		if (t == 30 || t == 60 || t == 120 || t == 240 || t == 480) { at[t] = landCover(w, ids, k); line += " t" + std::to_string(t) + " " + f2(100 * at[t].cover, 1) + "%"; }
	}
	w.tick++;
	say(line + "; bare plantable cells left " + std::to_string(at[480].bare) + " of " + std::to_string(at[480].plant) + "; world cover " + f2(100 * life::cover(w).veg, 1) + "%");
	ok(at[30].cover < 0.25, "a wave the player can watch: " + f2(at[30].cover) + " after 30 ticks");
	ok(at[240].cover >= 0.25, "at least 25 % of capacity within 240 ticks (got " + f2(at[240].cover) + ")");
	ok(at[60].cover < at[120].cover && at[120].cover < at[240].cover && at[240].cover <= at[480].cover + 1e-6, "keeps spreading");
	ok(at[480].cover > 0.97, "the continent ends up green (" + f2(at[480].cover) + ")");
	ok(at[480].bare <= 0.01 * at[480].plant, "every plantable region is reached (" + std::to_string(at[480].bare) + " of " + std::to_string(at[480].plant) + " still bare)");
}

namespace {
// The front on a featureless plain: a deterministic rule would draw a hexagon. How far the edge wanders.
struct Front { int green = 0; double mean = 0, cv = 0, mn = 0, mx = 0; };
Front frontShape(const World& w, int c0) {
	const Geo& g = *w.geo;
	std::vector<double> rs;
	Front f;
	for (int c = 0; c < w.n; c++) {
		if (!(w.veg[size_t(c)] > 0)) continue;
		f.green++;
		for (int j = 0; j < g.nbrN[size_t(c)]; j++) if (w.veg[size_t(g.nbr[size_t(6 * c + j)])] == 0) { rs.push_back(g.angle(c0, c) / g.step); break; }
	}
	if (rs.empty()) return f;
	f.mean = std::accumulate(rs.begin(), rs.end(), 0.0) / rs.size();
	double v = 0;
	for (double r : rs) v += (r - f.mean) * (r - f.mean);
	f.cv = std::sqrt(v / rs.size()) / f.mean;
	f.mn = *std::min_element(rs.begin(), rs.end());
	f.mx = *std::max_element(rs.begin(), rs.end());
	return f;
}
} // namespace

DM_TEST("vegetation: the spreading front is ragged, not a circle") {
	auto pw = plain(5, { .moist = 0.55 });
	World& w = *pw;
	const int c0 = 100;
	w.vars.lifeBoost = 25;
	life::seed(w, c0, 0.12);
	const Front s0 = frontShape(w, c0);
	advance(w, 40, 2);
	const Front s = frontShape(w, c0);
	say("front on a uniform plain after 40 ticks: " + std::to_string(s.green) + " green cells, edge " + f2(s.mn, 1) + " .. " + f2(s.mx, 1) +
		" hops from the seed (mean " + f2(s.mean, 1) + "), radial spread " + f2(100 * s.cv, 0) + " % (the seed disc itself: " + f2(100 * s0.cv, 0) + " %)");
	ok(s.green > 3 * s0.green && s.green < 0.6 * w.n, "the green grew but has not filled the planet (" + std::to_string(s.green) + " cells)");
	ok(s.cv > 0.12, "radial spread of the front " + f2(s.cv) + " (a circle is ~0.03)");
	ok(s.mx > 1.8 * s.mn, "lobes and bays: the edge runs from " + f2(s.mn, 1) + " to " + f2(s.mx, 1) + " hops out");
}

DM_TEST("vegetation: the green advances one ring per pass at most, whatever the order of the cells") {
	auto pw = plain(4);
	World& w = *pw;
	const int c0 = 11;
	w.vars.lifeBoost = 1;
	TL(w).spread = 1e9;                  // every sprouting roll succeeds
	TL(w).seedsPerYear = 0;              // no drift
	w.veg[size_t(c0)] = 0.5f;
	const std::vector<int> hops = hopsFrom(w, c0);
	for (int pass = 1; pass <= 6; pass++) {
		w.tick++; life::step(w, 0, 1);
		int bad = 0;
		for (int c = 0; c < w.n; c++) if ((w.veg[size_t(c)] > 0) != (hops[size_t(c)] <= pass) && bad++ < 3)
			fail("pass " + std::to_string(pass) + ": cell " + std::to_string(c) + " at " + std::to_string(hops[size_t(c)]) + " hops is " + (w.veg[size_t(c)] > 0 ? "green" : "bare"));
	}
	const int q1 = w.geo->nbr[size_t(6 * c0)];
	ok(w.veg[size_t(q1)] > 0.06 && w.veg[size_t(q1)] < 0.2, "a sprout starts small and grows (" + f2(w.veg[size_t(q1)], 3) + ")");
}

DM_TEST("vegetation: life crosses ice and lakes on land (seed pressure over dead ground), so no pocket is left out") {
	// a pocket of good ground walled in by a closed ring of dead cells three deep; drift switched off
	struct R { int first = -1, green = 0, pocket = 0, wall = 0, onWall = 0; };
	auto run = [](bool ice, double airDecay) {
		auto pw = plain(5);
		World& w = *pw;
		const int c0 = 2000;
		const std::vector<int> hops = hopsFrom(w, c0);
		std::vector<int> pocket;
		R r;
		int start = -1;
		for (int c = 0; c < w.n; c++) {
			const int h = hops[size_t(c)];
			if (h <= 5) pocket.push_back(c);
			else if (h <= 8) { r.wall++; if (ice) w.ice[size_t(c)] = 1; else w.water[size_t(c)] = 0.5f; }
			else if (h == 12 && start < 0) start = c;
		}
		TL(w).seedsPerYear = 0;
		if (airDecay >= 0) TL(w).airDecay = airDecay;
		w.vars.lifeBoost = 25;
		life::seed(w, start, 0.06);
		for (int t = 1; t <= 400; t++) {
			w.tick++; life::step(w, int(w.tick % 2), 2);
			if (r.first < 0) for (int c : pocket) if (w.veg[size_t(c)] > 0) { r.first = t; break; }
		}
		for (int c : pocket) if (w.veg[size_t(c)] > 0) r.green++;
		for (int c = 0; c < w.n; c++) if (hops[size_t(c)] > 5 && hops[size_t(c)] <= 8 && w.veg[size_t(c)] > 0) r.onWall++;
		r.pocket = int(pocket.size());
		return r;
	};
	for (bool ice : { true, false }) {
		const R r = run(ice, -1);
		const std::string kind = ice ? "ice" : "lake";
		say("pocket of " + std::to_string(r.pocket) + " cells behind a 3-cell ring of " + kind + " (" + std::to_string(r.wall) + " cells): first sprout inside at tick " + std::to_string(r.first) + ", " + std::to_string(r.green) + " green after 400 ticks");
		ok(r.first > 0, "life gets across the " + kind);
		eq(r.green, r.pocket, "and fills the pocket");
		eq(r.onWall, 0, "nothing grows on the " + kind + " itself");
	}
	const R blocked = run(true, 0);
	eq(blocked.green, 0, "with the seed pressure switched off the ring is a real barrier");
}

DM_TEST("vegetation: sprouting needs an established neighbour; lifeBoost scales the pace") {
	auto pw = plain(4);
	World& w = *pw;
	const int nb = w.geo->nbr[6 * 200];
	w.veg[size_t(nb)] = 0.04f;           // below 0.05 and far below half of its capacity: not established
	w.vars.lifeBoost = 0;                // (freeze growth so it stays that way)
	stepOnly(w, 400, 1);
	eq(double(w.veg[200]), 0.0, "a thin neighbour does not seed the cell");
	// pace: years until a seeded plain is half green, boost 1 against boost 4
	auto half = [](double boost) {
		auto p = plain(4);
		p->vars.lifeBoost = boost;
		life::seed(*p, 300, 0.25);
		for (int t = 1; t <= 4000; t++) { p->tick++; life::step(*p, int(p->tick % 8), 8); if (t % 4 == 0 && life::cover(*p).veg >= 0.5) return t / 4.0; }
		return 1e9;
	};
	const double y1 = half(1), y4 = half(4);
	say("half cover of a plain from one seed: " + f2(y1, 0) + " years at boost 1, " + f2(y4, 0) + " years at boost 4");
	ok(y4 < 0.6 * y1, "boost 4 is clearly faster (" + f2(y4, 0) + " vs " + f2(y1, 0) + " years)");
	ok(y1 < 1e9, "boost 1 does get there");
}

DM_TEST("vegetation: drifting seeds cross water within 12 hops (veg 0.08), and no further") {
	struct B { std::unique_ptr<World> w; std::vector<int> island; };
	auto build = [](int band) {
		B b;
		b.w = plain(4);
		World& w = *b.w;
		const int c0 = 700;
		const std::vector<int> hops = hopsFrom(w, c0);
		for (int c = 0; c < w.n; c++) {
			const int h = hops[size_t(c)];
			if (h <= 3) b.island.push_back(c);
			else if (h <= band) w.elev[size_t(c)] = -1;          // a strait all round
		}
		life::fill(w, 1);
		for (int c : b.island) w.veg[size_t(c)] = 0;
		w.vars.lifeBoost = 1;
		return b;
	};
	B nearB = build(8);         // a strait 5 cells wide: every island cell is within 12 hops of the mainland forest
	int first = -1;
	double year = -1;
	for (int t = 1; t <= 4 * 600 && first < 0; t++) {
		nearB.w->tick++; life::step(*nearB.w, int(nearB.w->tick % 8), 8);
		for (int c : nearB.island) if (nearB.w->veg[size_t(c)] > 0) { first = c; year = t / 4.0; break; }
	}
	if (!ok(first >= 0, "a seed reaches the island")) return;
	near(nearB.w->veg[size_t(first)], 0.08, 1e-6, "a drifted seedling starts at 0.08");
	stepOnly(*nearB.w, 4 * 300, 8);
	int green = 0;
	for (int c : nearB.island) if (nearB.w->veg[size_t(c)] > 0.3) green++;
	say("island behind a 5-cell strait: first seedling in year " + f2(year, 1) + ", " + std::to_string(green) + " of " + std::to_string(nearB.island.size()) + " cells wooded 300 years later");
	ok(green > 0.8 * nearB.island.size(), "the island greens over");
	B farB = build(20);         // a strait 17 cells wide: out of reach
	stepOnly(*farB.w, 4 * 600, 8);
	int seeded = 0;
	for (int c : farB.island) if (farB.w->veg[size_t(c)] != 0) seeded++;
	eq(seeded, 0, "an island 17 hops out is never seeded");
}

DM_TEST("vegetation: drift runs on the boosted clock; overland forerunners are thinned, jumps over water are not") {
	// A disc of bare ground 6 hops wide inside a forest, cut off by a ring 3 cells wide that is either sea (every
	// seedling in the disc crossed water) or bare land. Growth and sprouting off: only the drift acts. 6 runs.
	const int RUNS = 6;
	struct C { double k = 0; int cells = 0, land = 0; };
	auto count = [&](bool seaRing, double boost, int ticks, double seaBoost) {
		C r;
		for (int run = 0; run < RUNS; run++) {
			auto pw = plain(4);
			World& w = *pw;
			const std::vector<int> hops = hopsFrom(w, 1500);
			life::fill(w, 1);
			std::vector<int> disc;
			r.land = w.n;
			for (int c = 0; c < w.n; c++) {
				const int h = hops[size_t(c)];
				if (h <= 6) { disc.push_back(c); w.veg[size_t(c)] = 0; }
				else if (h <= 9) { w.veg[size_t(c)] = 0; if (seaRing) { w.elev[size_t(c)] = -1; r.land--; } }
			}
			TL(w).grow = 0; TL(w).spread = 0;
			if (seaBoost >= 0) TL(w).driftSeaBoost = seaBoost;
			w.vars.lifeBoost = boost; w.rng = Rng(uint32_t(900 + run));
			stepOnly(w, ticks, 2);
			for (int c : disc) if (w.veg[size_t(c)] > 0) {
				r.k += 1.0 / RUNS;
				if (std::fabs(w.veg[size_t(c)] - 0.08) > 1e-6) fail("a seedling has veg " + std::to_string(w.veg[size_t(c)]));
			}
			r.cells = int(disc.size());
		}
		return r;
	};
	// 8 picks per year of the boosted clock, each on a random land cell: share of the disc hit after `ticks`
	auto predict = [](const C& r, double rate, int ticks) { return r.cells * (1 - std::exp(-8 * 0.25 * rate * ticks / r.land)); };
	const C sea = count(true, 25, 20, -1), land = count(false, 25, 20, -1);
	const double expSea = predict(sea, 25, 20), expLand = predict(land, std::pow(25, 0.3), 20);
	say("drift into a bare disc of " + std::to_string(sea.cells) + " cells in 20 ticks at boost 25 (L = 4): " + f2(sea.k, 1) + " seedlings across a strait (predicted " + f2(expSea, 1) + "), " + f2(land.k, 1) + " across bare land (thinned to boost ^ 0.3: " + f2(expLand, 1) + ")");
	ok(std::fabs(sea.k - expSea) < 0.2 * expSea, "over water: " + f2(sea.k, 1) + " seedlings, expected about " + f2(expSea, 1));
	ok(std::fabs(land.k - expLand) < 0.4 * expLand + 1, "overland: " + f2(land.k, 1) + " seedlings, expected about " + f2(expLand, 1));
	ok(land.k < 0.3 * sea.k, "overland forerunners are much rarer");
	const C slow = count(true, 25, 20, 0.3);
	const double expSlow = predict(slow, std::pow(25, 0.3), 20);
	ok(std::fabs(slow.k - expSlow) < 0.4 * expSlow + 1, "driftSeaBoost 0.3: " + f2(slow.k, 1) + " seedlings across the strait, expected about " + f2(expSlow, 1));
	const C sea1 = count(true, 1, 500, -1), land1 = count(false, 1, 500, -1);
	const double exp1 = predict(sea1, 1, 500), exp1l = predict(land1, 1, 500);
	say("the same at boost 1 over 125 years: " + f2(sea1.k, 1) + " across the strait (predicted " + f2(exp1, 1) + "), " + f2(land1.k, 1) + " across bare land (" + f2(exp1l, 1) + ")");
	ok(std::fabs(sea1.k - exp1) < 0.2 * exp1 && std::fabs(land1.k - exp1l) < 0.2 * exp1l, "boost 1: one rule wherever the source is");
}

DM_TEST("vegetation: an unseeded island of a helper world greens over centuries (drifting seeds)") {
	auto pw = planet(4, 1);
	World& w = *pw;
	const LandIds ids = landIds(w);
	const int big = biggest(ids);
	life::fill(w, 1);
	std::vector<int> isl;
	for (int c = 0; c < w.n; c++) if (ids.id[size_t(c)] >= 0 && ids.id[size_t(c)] != big) { w.veg[size_t(c)] = 0; if (life::capacity(w, c) > 0.1) isl.push_back(c); }
	if (!ok(isl.size() > 50, "the helper world has islands (" + std::to_string(isl.size()) + " island cells)")) return;
	w.vars.lifeBoost = 1;
	auto countG = [&](int& g, int& nIsl) { std::set<int> s; g = 0; for (int c : isl) if (w.veg[size_t(c)] > 0) { g++; s.insert(ids.id[size_t(c)]); } nIsl = int(s.size()); };
	advance(w, 4 * 25, 8);
	int ga, ia; countG(ga, ia);
	advance(w, 4 * 375, 8);
	int gb, ib; countG(gb, ib);
	say("islands (all but the largest landmass stripped bare): " + std::to_string(ga) + " green cells on " + std::to_string(ia) + " islands after 25 years, " + std::to_string(gb) + " of " + std::to_string(isl.size()) + " on " + std::to_string(ib) + " islands after 400");
	ok(ga < isl.size() * 0.9, "not at once");
	ok(ib >= 5 && gb > 0.9 * isl.size(), "most island ground is green after four centuries");
}

DM_TEST("vegetation: a cleared forest regrows in roughly 100-160 years at boost 1") {
	auto pw = plain(4);
	World& w = *pw;
	life::fill(w, 1);
	w.vars.lifeBoost = 1;
	std::vector<int32_t> cells;
	w.geo->ring(500, 2, cells);
	eq(int(cells.size()), 19);
	for (int c : cells) w.veg[size_t(c)] = 0;
	double y50 = -1, y90 = -1;
	for (int t = 1; t <= 4 * 300 && y90 < 0; t++) {
		w.tick++; life::step(w, int(w.tick % 8), 8);
		if (t % 4) continue;
		double s = 0, sc = 0;
		for (int c : cells) { s += w.veg[size_t(c)]; sc += life::capacity(w, c); }
		if (y50 < 0 && s >= 0.5 * sc) y50 = t / 4.0;
		if (s >= 0.9 * sc) y90 = t / 4.0;
	}
	say("a 19-cell clearing in forest: half grown after " + f2(y50, 0) + " years, 90 % after " + f2(y90, 0) + " (design target " + f2(TL(w).regrowYears, 0) + ")");
	ok(y90 >= 100 && y90 <= 160, "90 % regrown after " + f2(y90, 0) + " years");
	auto pv = plain(4);
	World& v = *pv;
	life::fill(v, 1); v.vars.lifeBoost = 1; v.veg[900] = 0;
	double y = -1;
	for (int t = 1; t <= 4 * 300 && y < 0; t++) { v.tick++; life::step(v, int(v.tick % 8), 8); if (v.veg[900] >= 0.9 * life::capacity(v, 900)) y = t / 4.0; }
	ok(y >= 85 && y <= 160, "a single felled cell is back after " + f2(y, 0) + " years");
}

DM_TEST("step: honours dt = parts x TICK (2 slices and 8 slices agree)") {
	// (a) everything deterministic: growth of standing vegetation and every decay
	struct Sums { double veg = 0, fauna = 0, fert = 0, wet = 0, scorch = 0, rad = 0, farm = 0, urban = 0; };
	auto run = [](int parts) {
		auto pw = planet(4, 2, true);
		World& w = *pw;
		w.vars.lifeBoost = 1;
		for (int c = 0; c < w.n; c++) {
			if (!isLand(w, c)) continue;
			const size_t i = size_t(c);
			if (life::capacity(w, c) > TL(w).plantMin) w.veg[i] = 0.06f;
			w.wet[i] = 0.25f; w.scorch[i] = 0.6f; w.rad[i] = 0.3f;
			if (c % 5 == 0) { w.farm[i] = 0.5f; w.urban[i] = 0.4f; }
		}
		advance(w, 4 * 40, parts);
		Sums s;
		for (int c = 0; c < w.n; c++) if (isLand(w, c)) {
			const size_t i = size_t(c);
			s.veg += w.veg[i]; s.fauna += w.fauna[i]; s.fert += w.fert[i]; s.wet += w.wet[i];
			s.scorch += w.scorch[i]; s.rad += w.rad[i]; s.farm += w.farm[i]; s.urban += w.urban[i];
		}
		return s;
	};
	const Sums a = run(2), b = run(8);
	auto cmp = [](const char* k, double x, double y) { ok(std::fabs(x - y) <= 0.03 * std::max(x, y) + 1e-9, std::string(k) + " after 40 years: " + f2(x, 3) + " with 2 slices, " + f2(y, 3) + " with 8"); };
	cmp("veg", a.veg, b.veg); cmp("fauna", a.fauna, b.fauna); cmp("fert", a.fert, b.fert); cmp("wet", a.wet, b.wet);
	cmp("scorch", a.scorch, b.scorch); cmp("rad", a.rad, b.rad); cmp("farm", a.farm, b.farm); cmp("urban", a.urban, b.urban);
	say("after 40 years, 2 vs 8 slices: veg " + f2(a.veg, 1) + " / " + f2(b.veg, 1) + ", fauna " + f2(a.fauna, 1) + " / " + f2(b.fauna, 1) + ", fert " + f2(a.fert, 1) + " / " + f2(b.fert, 1));
	// (b) the stochastic spread from one seed: years to half cover, mean of 4 runs each
	auto half = [](int parts, int r) {
		auto pw = planet(4, 1, true);
		World& w = *pw;
		const LandIds ids = landIds(w);
		w.vars.lifeBoost = 1; w.rng = Rng(uint32_t(500 + r));
		life::seed(w, heart(w, ids, biggest(ids)), 0.2);
		for (int t = 1; t <= 4 * 800; t++) { w.tick++; life::step(w, int(w.tick % parts), parts); if (t % 4 == 0 && life::cover(w).veg >= 0.5) return t / 4.0; }
		return 1e9;
	};
	double m2 = 0, m8 = 0;
	for (int r = 0; r < 4; r++) { m2 += half(2, r) / 4; m8 += half(8, r) / 4; }
	say("years to half cover from one seed (helper L = 4, boost 1): " + f2(m2, 1) + " with 2 slices, " + f2(m8, 1) + " with 8");
	ok(std::fabs(m2 - m8) <= 0.1 * std::max(m2, m8), "spread per year: " + f2(m2, 1) + " vs " + f2(m8, 1) + " years");
	// (c) one call of one slice is a quarter of a year (sim::kTick)
	auto pw = plain(4);
	pw->rad[5] = 0.5f;
	life::step(*pw, 0, 1);
	near(pw->rad[5], 0.5 * std::pow(0.985, 0.25), 1e-6, "one tick is a quarter of a year");
}

DM_TEST("step: only the cells of its slice change") {
	auto pw = plain(4);
	World& w = *pw;
	std::fill(w.rad.begin(), w.rad.end(), 0.5f);
	life::step(w, 3, 8);
	int bad = 0;
	for (int c = 0; c < w.n; c++) if ((w.rad[size_t(c)] < 0.5f) != (c % 8 == 3)) bad++;
	eq(bad, 0, "cells outside slice 3 untouched");
}

DM_TEST("fill: veg = frac x capacity, wildlife at its capacity") {
	auto pw = planet(4, 3, true);
	World& w = *pw;
	life::fill(w);
	double richShallow = 0, richDeep = 0;
	int shallow = 0, deep = 0, bad = 0;
	for (int c = 0; c < w.n; c++) {
		const size_t i = size_t(c);
		if (std::fabs(w.veg[i] - life::capacity(w, c)) > 1e-6 && bad++ < 3) fail("veg at capacity, cell " + std::to_string(c));
		if (isLand(w, c)) {
			if (w.veg[i] > 0.2 && !(w.fauna[i] > 0.1) && bad++ < 3) fail("no wildlife in the woods at " + std::to_string(c));
			if (w.veg[i] == 0 && !(w.water[i] > 0.03) && w.fauna[i] != 0 && bad++ < 3) fail("wildlife on bare ground at " + std::to_string(c));
		} else if (w.seaLevel - w.elev[i] <= 0.6) { shallow++; richShallow += w.fauna[i]; }
		else if (w.seaLevel - w.elev[i] > 3) { deep++; richDeep += w.fauna[i]; }
	}
	ok(shallow > 0 && deep > 0 && richShallow / shallow > 1.5 * richDeep / deep, "shallows are richer in fish than the deep sea");
	near(life::cover(w).veg, 1, 1e-6, "cover 1");
	const std::vector<float> before = w.fauna;
	advance(w, 16, 8);
	double drift = 0;
	for (int c = 0; c < w.n; c++) drift = std::max(drift, double(std::fabs(w.fauna[size_t(c)] - before[size_t(c)])));
	ok(drift < 0.02, "fauna set by fill is the capacity step relaxes to (largest change " + f2(drift, 4) + ")");
	w.tick++;
	life::fill(w, 0.5);
	bad = 0;
	for (int c = 0; c < w.n; c++) if (std::fabs(w.veg[size_t(c)] - 0.5 * life::capacity(w, c)) > 1e-6) bad++;
	eq(bad, 0, "fill(0.5)");
	near(life::cover(w).veg, 0.5, 1e-6, "cover 0.5");
	life::fill(w, std::nan(""));
	eq(checkFinite(w), std::string(), "fill(NaN) writes no NaN");
}

DM_TEST("step: dirty.objects when vegetation crosses a kVegT level, dirty.color when colours move") {
	auto pw = plain(4);
	World& w = *pw;
	std::fill(w.veg.begin(), w.veg.end(), 0.2f);       // just under the first tree level (0.22)
	w.vars.lifeBoost = 1; w.dirty.objects = false; w.dirty.color = false;
	int t = 0;
	while (!w.dirty.objects && t < 200) { w.tick++; life::step(w, int(w.tick % 8), 8); t++; }
	ok(w.dirty.objects, "trees appear");
	ok(w.dirty.color, "and the ground changes colour");
	auto pq = plain(4);
	World& q = *pq;
	life::fill(q, 1);
	std::fill(q.fert.begin(), q.fert.end(), 0.6f);
	advance(q, 80, 8);
	q.dirty.objects = false;
	advance(q, 40, 8);
	ok(!q.dirty.objects, "no re-instancing while nothing crosses a level");
}

// ================================================================== fire
DM_TEST("ignite: starts a fire only where the ground can burn; fire:start at most twice a tick") {
	auto pw = plain(4, { .veg = 0.8 });
	World& w = *pw;
	const std::vector<Event> ev = capture(w, [&] {
		eq(life::ignite(w, 50), true, "forest burns");
		ok(w.fire[50] > 0 && w.fire[50] <= 1, "fire value in 0..1");
		eq(life::ignite(w, 50), false, "already burning");
		w.elev[51] = -1; eq(life::ignite(w, 51), false, "sea");
		w.ice[52] = 0.8f; eq(life::ignite(w, 52), false, "ice");
		w.water[53] = 0.4f; eq(life::ignite(w, 53), false, "lake");
		w.veg[54] = 0.14f; eq(life::ignite(w, 54), false, "veg < 0.15");
		w.wet[55] = 0.31f; eq(life::ignite(w, 55), false, "wet > 0.3");
		eq(life::ignite(w, 56, 0), false, "no intensity");
		eq(life::ignite(w, 57, std::nan("")), false, "NaN intensity");
		for (int bad : { -1, w.n, w.n + 7 }) eq(life::ignite(w, bad), false, "bad cell " + std::to_string(bad));
		eq(life::ignite(w, 60, 0.3), true);
		eq(life::ignite(w, 70), true);
		eq(life::ignite(w, 80), true);
	});
	std::vector<int> starts;
	for (const Event& e : ev) if (const EvFireStart* f = e.as<EvFireStart>()) starts.push_back(f->cell);
	eq(int(starts.size()), 2, "two fire:start events in one tick");
	if (!starts.empty()) eq(starts[0], 50, "the first names its cell");
	eq(firesMatch(w), std::string());
	eq(int(w.cache.fires.size()), 4);
	w.tick++;
	eq(int(capture(w, [&] { life::ignite(w, 90); }).size()), 1, "the next tick may announce fires again");
}

DM_TEST("fire: a cell burns for exactly 2 ticks and tries each neighbour once, on its first") {
	auto pw = plain(4, { .moist = 0.2, .veg = 0.8 });
	World& w = *pw;
	const int c0 = 300;
	TL(w).fireP = 1e9;                   // every attempt succeeds: the fire is a clean expanding ring
	const std::vector<int> hops = hopsFrom(w, c0);
	life::ignite(w, c0);
	std::vector<int> lit(size_t(w.n), 0);
	for (int t = 1; t <= 8; t++) {
		w.tick++; life::burn(w);
		int bad = 0;
		for (int c = 0; c < w.n; c++) {
			const bool want = hops[size_t(c)] == t || hops[size_t(c)] == t - 1;   // ring t lit this tick, ring t-1 on its second
			if ((w.fire[size_t(c)] > 0) != want && bad++ < 3) fail("tick " + std::to_string(t) + ": cell " + std::to_string(c) + " at " + std::to_string(hops[size_t(c)]) + " hops is " + (w.fire[size_t(c)] > 0 ? "burning" : "not burning"));
			if (w.fire[size_t(c)] > 0) lit[size_t(c)]++;
		}
		eq(firesMatch(w), std::string());
	}
	int bad = 0;
	for (int c = 0; c < w.n; c++) if (hops[size_t(c)] >= 1 && hops[size_t(c)] <= 6 && lit[size_t(c)] != 2) bad++;
	eq(bad, 0, "every cell 1-6 hops out burnt for exactly two ticks");
	near(w.veg[size_t(c0)], 0.16, 1e-6, "burning removes 80 % of the vegetation");
	ok(w.scorch[size_t(c0)] > 0.5, "and leaves scorch");
}

DM_TEST("fire: P = 0.55 x veg x sat(1.2 - 1.6 x (moist + wet)) (measured frequency and the hard limits)") {
	// the documented landmarks
	ok(0.55 * 0.9 * sat(1.2 - 1.6 * 0.7) < 0.05, "wet forest P < 0.05");
	near(0.55 * 0.9 * sat(1.2 - 1.6 * 0.4), 0.28, 0.005, "dry forest P ~0.28");
	// measured frequencies against the formula, for several veg / moist / wet combinations
	struct K { double veg, moist, wet; };
	const K cases[] = { { 0.8, 0.36, 0 }, { 0.9, 0.3, 0.1 }, { 0.6, 0.2, 0.25 }, { 1.0, 0.0, 0 }, { 0.5, 0.1, 0 } };
	for (const K& k : cases) {
		auto pw = plain(4, { .moist = k.moist, .veg = k.veg });
		World& w = *pw;
		std::fill(w.wet.begin(), w.wet.end(), float(k.wet));
		const double P = 0.55 * double(float(k.veg)) * sat(1.2 - 1.6 * (double(float(k.moist)) + double(float(k.wet))));
		int litN = 0, tries = 0;
		for (int i = 0; i < 2000; i++) {
			const int c = 12 + (i * 7) % (w.n - 12);       // (cells 0-11 are the pentagons)
			std::fill(w.veg.begin(), w.veg.end(), float(k.veg));
			std::fill(w.fire.begin(), w.fire.end(), 0.0f);
			life::rebuild(w);
			life::ignite(w, c); w.tick++; life::burn(w);
			litN += burning(w) - 1; tries += 6;
		}
		const double f = double(litN) / tries, sd = std::sqrt(P * (1 - P) / tries);
		ok(std::fabs(f - P) < 4 * sd + 1e-3, "veg " + f2(k.veg) + " moist " + f2(k.moist) + " wet " + f2(k.wet) + ": frequency " + f2(f, 4) + " vs P " + f2(P, 4));
		if (k.veg == 0.8) say("ignition frequency at P = " + f2(P, 4) + ": " + f2(f, 4) + " over " + std::to_string(tries) + " attempts");
	}
	// moist + wet >= 0.75: dry = 0, nothing ever catches (even with a huge fireP)
	{
		auto pw = plain(4, { .moist = 0.75, .veg = 0.9 });
		TL(*pw).fireP = 1e9;
		life::ignite(*pw, 300); pw->tick++; life::burn(*pw);
		eq(burning(*pw), 1, "moist >= 0.75: only the struck cell burns (its second tick), no neighbour caught");
		pw->tick++; life::burn(*pw);
		eq(burning(*pw), 0, "and then it is out");
	}
	{
		auto pw = plain(4, { .moist = 0.2, .veg = 0.9 });
		life::ignite(*pw, 300);
		std::fill(pw->wet.begin(), pw->wet.end(), 0.35f);
		pw->tick++; life::burn(*pw);
		eq(burning(*pw), 0, "wet > 0.3: the fire itself goes out");
	}
}

DM_TEST("fire: dies in wet forest, stays small in dry forest, runs under drought (fire sizes)") {
	const Sizes wet = fireSizes(5, 0.85, 0.70, 80), dry = fireSizes(5, 0.9, 0.40, 200), drought = fireSizes(5, 0.85, 0.15, 12);
	auto line = [](const char* name, double P, const Sizes& r) {
		char b[256];
		std::snprintf(b, sizeof b, "%-34sP %.3f  mean %7.1f  median %5d  p90 %5d  max %5d  > 200 cells: %5.1f %%  peak burning %d", name, P, r.mean, r.median, r.p90, r.max, 100 * r.over(200), r.peak);
		say(b);
	};
	say("fire sizes in cells on a uniform forest of " + std::to_string(wet.n) + " cells:");
	line("wet forest (veg .85, moist .70)", 0.55 * 0.85 * 0.08, wet);
	line("dry forest (veg .90, moist .40)", 0.55 * 0.9 * 0.56, dry);
	line("drought (veg .85, moist .15)", 0.55 * 0.85 * 0.96, drought);
	ok(wet.max <= 10 && wet.mean < 2.5, "wet forest: a few cells at most (max " + std::to_string(wet.max) + ")");
	ok(dry.median < 80 && dry.over(200) < 0.25, "dry forest: most fires under ~200 cells (" + f2(100 * dry.over(200), 1) + " % larger)");
	ok(dry.mean > 4 * wet.mean, "but far larger than in wet forest");
	ok(drought.median > 0.5 * drought.n, "drought: the fire runs (median " + std::to_string(drought.median) + " of " + std::to_string(drought.n) + " cells)");
}

DM_TEST("fire: a sharp percolation threshold near P = 0.35 (sweep of P on a uniform forest)") {
	const double VEG = 0.9;
	struct Row { double P, mean, big; };
	std::vector<Row> rows;
	say("fire size against P (uniform forest, veg 0.9, L = 5, 10,242 cells):");
	const std::pair<double, int> sweep[] = { { 0.10, 120 }, { 0.20, 120 }, { 0.25, 120 }, { 0.28, 120 }, { 0.30, 100 }, { 0.33, 60 }, { 0.36, 40 }, { 0.40, 40 }, { 0.45, 40 } };
	for (const auto& [P, trials] : sweep) {
		const double dry = P / (0.55 * VEG);
		const Sizes r = fireSizes(5, VEG, (1.2 - dry) / 1.6, trials, uint32_t(9000 + std::lround(P * 100)));
		const double big = r.over(int(0.1 * r.n));
		rows.push_back({ P, r.mean, big });
		char b[200];
		std::snprintf(b, sizeof b, "  P %.2f  mean %7.1f  median %5d  p90 %5d  max %5d  > 200 cells %3.0f %%  > 10 %% of the planet %3.0f %%", P, r.mean, r.median, r.p90, r.max, 100 * r.over(200), 100 * big);
		say(b);
	}
	for (const Row& r : rows) {
		if (r.P <= 0.28) eq(r.big, 0.0, "P = " + f2(r.P) + ": no fire crosses the planet");
		if (r.P >= 0.40) ok(r.big >= 0.7, "P = " + f2(r.P) + ": most fires cross the planet (" + f2(100 * r.big, 0) + " %)");
	}
	for (size_t i = 1; i < rows.size(); i++) ok(rows[i].mean > rows[i - 1].mean, "fires grow with P (" + f2(rows[i - 1].P) + " -> " + f2(rows[i].P) + ")");
	ok(rows[6].mean > 20 * rows[4].mean, "the step: mean size " + f2(rows[4].mean, 0) + " cells at P = 0.30, " + f2(rows[6].mean, 0) + " at 0.36");
}

DM_TEST("fire: never enters sea, ice, lakes or thin vegetation; wet ground stops it") {
	auto pw = plain(5, { .moist = 0.1, .veg = 0.85 });
	World& w = *pw;
	const Geo& g = *w.geo;
	std::vector<uint8_t> kind(size_t(w.n), 0);
	for (int c = 0; c < w.n; c++) {
		const double lon = g.lon[size_t(c)], lat = g.lat[size_t(c)];
		if (std::fabs(lat) > 1.2) continue;
		const int s = int(std::floor((lon + kPi) / (kPi / 6))) % 6;
		const size_t i = size_t(c);
		if (std::fabs(lat - 0.5) < 0.06 && s == 1) { kind[i] = 1; w.elev[i] = -1; }
		else if (std::fabs(lat + 0.3) < 0.06 && s == 2) { kind[i] = 2; w.ice[i] = 0.9f; }
		else if (std::fabs(lat - 0.1) < 0.06 && s == 3) { kind[i] = 3; w.water[i] = 0.5f; }
		else if (std::fabs(lat + 0.6) < 0.06 && s == 4) { kind[i] = 4; w.veg[i] = 0.14f; }
		else if (std::fabs(lat - 0.8) < 0.06 && s == 5) { kind[i] = 5; w.wet[i] = 0.5f; }
	}
	const char* names[] = { "", "sea", "ice", "lake", "thin vegetation", "wet ground" };
	int had[6] = {};
	for (int c = 0; c < w.n; c++) had[kind[size_t(c)]]++;
	for (int k = 1; k <= 5; k++) ok(had[k] > 10, std::string(names[k]) + " stripe exists");
	life::ignite(w, findLand(w, [&](int c) { return kind[size_t(c)] == 0 && std::fabs(g.lat[size_t(c)]) < 0.05; }));
	int ticks = 0, bad = 0;
	while (!w.cache.fires.empty() && ticks < 3000) {
		w.tick++; life::burn(w); ticks++;
		for (int c = 0; c < w.n; c++) if (kind[size_t(c)] && w.fire[size_t(c)] > 0 && bad++ < 3) fail(std::string("fire on ") + names[kind[size_t(c)]] + " at tick " + std::to_string(ticks));
		if (ticks % 16 == 0) eq(firesMatch(w), std::string());
	}
	int burnt = 0;
	for (int c = 0; c < w.n; c++) if (w.scorch[size_t(c)] > 0) { burnt++; if (kind[size_t(c)] && bad++ < 3) fail(std::string(names[kind[size_t(c)]]) + " was scorched"); }
	ok(burnt > 0.5 * w.n, "the drought fire did run across the plain (" + std::to_string(burnt) + " cells)");
	eq(burning(w), 0);
	auto pr = plain(4, { .moist = 0.1, .veg = 0.85 });
	World& r = *pr;
	life::ignite(r, 400);
	for (int i = 0; i < 6; i++) { r.tick++; life::burn(r); }
	ok(burning(r) > 10, "a fire front is running (" + std::to_string(burning(r)) + " cells)");
	std::fill(r.wet.begin(), r.wet.end(), 0.31f);
	r.tick++; life::burn(r);
	eq(burning(r), 0, "wet > 0.3 puts every fire out");
	eq(int(r.cache.fires.size()), 0);
}

DM_TEST("fire: never more than 1,500 cells burn at once") {
	auto pw = plain(6, { .moist = 0.05, .veg = 0.9 });
	World& w = *pw;
	int started = 0;
	for (int c = 5; c < w.n; c += 273) if (life::ignite(w, c)) started++;
	eq(started, 151);
	int peak = 0, ticks = 0, atCap = 0, busy = 0, bad = 0;
	double busyMs = 0, worstMs = 0;
	while (!w.cache.fires.empty() && ticks < 4000) {
		const size_t before = w.cache.fires.size();
		const double t0 = nowMs();
		w.tick++; life::burn(w); ticks++;
		const double d = nowMs() - t0;
		if (before >= 1000) { busy++; busyMs += d; worstMs = std::max(worstMs, d); }
		const int b = burning(w);
		peak = std::max(peak, b);
		if (b >= 1450) atCap++;
		if (b != int(w.cache.fires.size()) && bad++ < 3) fail("fires list " + std::to_string(w.cache.fires.size()) + " != " + std::to_string(b) + " burning cells");
	}
	int burnt = 0;
	for (int c = 0; c < w.n; c++) if (w.scorch[size_t(c)] > 0) burnt++;
	say("151 simultaneous strikes in drought forest (L = 6): peak " + std::to_string(peak) + " cells burning, " + std::to_string(atCap) + " ticks at the cap, " + std::to_string(burnt) +
		" cells burnt, out after " + std::to_string(ticks) + " ticks; burn with >= 1,000 cells alight: mean " + f2(busyMs / std::max(1, busy), 3) + " ms, worst " + f2(worstMs, 3) + " ms");
	ok(peak <= 1500, "peak " + std::to_string(peak));
	ok(atCap > 5, "the cap was really reached (" + std::to_string(atCap) + " ticks)");
	eq(burning(w), 0, "and the fire ends");
	auto px = plain(6, { .moist = 0.05, .veg = 0.9 });
	World& x = *px;
	int litN = 0;
	for (int c = 0; c < x.n; c += 13) if (life::ignite(x, c)) litN++;
	eq(litN, 1500, "ignite stops at 1,500");
	eq(life::ignite(x, 7), false);
	x.tick++; life::burn(x);
	eq(burning(x), 1500, "nothing new catches while 1,500 burn");
}

DM_TEST("fire: the fires cache mirrors fire[]; rebuild restores it; a missing cache is tolerated; save/load mid-fire") {
	auto pw = plain(4, { .moist = 0.2, .veg = 0.85 });
	World& w = *pw;
	life::ignite(w, 100); life::ignite(w, 900);
	for (int i = 0; i < 5; i++) { w.tick++; life::burn(w); eq(firesMatch(w), std::string()); }
	const int n = burning(w);
	ok(n > 2, "fires are running");
	w.cache.fires.clear();
	life::rebuild(w);
	eq(int(w.cache.fires.size()), n, "rebuild restores the list");
	eq(firesMatch(w), std::string());
	// a cache lost without rebuild (another tool cleared it): burn notices and rebuilds
	w.cache.fires.clear();
	w.tick++; life::burn(w);
	eq(firesMatch(w), std::string(), "burn rebuilt a cleared cache");
	// quantized reload (fire[] to 8 bits, cache and scratch gone): every fire keeps its stage
	auto a = plain(4, { .moist = 0.2, .veg = 0.85 }), b = plain(4, { .moist = 0.2, .veg = 0.85 });
	for (World* x : { a.get(), b.get() }) {
		life::ignite(*x, 100, 0.5); life::ignite(*x, 1500, 0.01);
		for (int i = 0; i < 4; i++) { x->tick++; life::burn(*x); }
		life::ignite(*x, 2000, 0.01); life::ignite(*x, 2100, 1);
	}
	for (int c = 0; c < b->n; c++) b->fire[size_t(c)] = float(std::round(b->fire[size_t(c)] * 255) / 255);
	b->cache.fires.clear();
	b->clearScratch();
	ok(burning(*a) > 8, "fires burning before the reload");
	TL(*a).fireP = 0; TL(*b).fireP = 0;          // no new ignitions: watch the old ones end
	for (int i = 0; i < 2; i++) {
		a->tick++; life::burn(*a); b->tick++; life::burn(*b);
		int diff = 0;
		for (int c = 0; c < a->n; c++) if ((a->fire[size_t(c)] > 0) != (b->fire[size_t(c)] > 0)) diff++;
		eq(diff, 0, "after a reload every cell burns exactly as long");
		eq(burning(*a) > 0, i == 0, "all out two ticks later");
	}
	// a real save / load in the middle of a fire
	{
		auto s = plain(4, { .moist = 0.2, .veg = 0.85 });
		life::ignite(*s, 300);
		for (int i = 0; i < 3; i++) { s->tick++; life::burn(*s); }
		life::ignite(*s, 1200);
		std::vector<uint8_t> bytes;
		ok(state::serialize(*s, bytes), "serialize");
		auto t = state::deserialize(bytes);
		if (ok(t != nullptr, "deserialize")) {
			int same = 0, total = 0;
			for (int c = 0; c < s->n; c++) {
				const bool fs = s->fire[size_t(c)] > 0.55f, ft = t->fire[size_t(c)] > 0.55f;
				if (s->fire[size_t(c)] > 0) { total++; if (fs == ft && t->fire[size_t(c)] > 0) same++; }
			}
			eq(same, total, "every burning cell keeps its stage through a save");
			life::rebuild(*t);
			eq(firesMatch(*t), std::string(), "rebuild after load");
		}
	}
	// no cache at all, odd calls
	auto pe = plain(4, { .veg = 0.85 });
	World& e = *pe;
	e.cache.fires.clear(); e.clearScratch();
	life::burn(e);
	eq(int(e.cache.fires.size()), 0);
	eq(life::ignite(e, 5), true);
	eq(int(e.cache.fires.size()), 1);
	e.cache.fires.push_back(-7); e.cache.fires.push_back(e.n + 3);      // garbage in the cache is survived
	e.tick++; life::burn(e);
	eq(firesMatch(e), std::string(), "garbage entries are dropped");
	e.fire[5] = 0;
	life::step(e, 0, 1);
	eq(checkFinite(e), std::string());
}

// ================================================================== decay, lakes, the sea
DM_TEST("lake cells: capacity 0, habitability 0, vegetation dies, fish arrive") {
	auto pw = plain(4, { .moist = 0.5, .veg = 0.7 });
	World& w = *pw;
	std::fill(w.fauna.begin(), w.fauna.end(), 0.4f);
	w.water[640] = 0.6f; w.water[641] = 0.02f;           // a lake cell, and a puddle below the 0.03 threshold
	eq(life::capacity(w, 640), 0.0);
	ok(life::capacity(w, 641) > 0.5, "a puddle is not a lake");
	life::habitability(w);
	eq(double(w.hab[640]), 0.0, "lake hab 0");
	ok(w.hab[641] > 0.1, "puddle hab > 0");
	advance(w, 4 * 30, 8);
	ok(w.veg[640] < 0.01, "the drowned forest is gone (veg " + f2(w.veg[640], 4) + ")");
	ok(w.fauna[640] > 0.2, "fish in the lake (" + f2(w.fauna[640]) + ")");
}

DM_TEST("step: farm and urban fade on unowned cells only; rad, wet and scorch decay") {
	auto pw = plain(4, { .veg = 0 });
	World& w = *pw;
	std::fill(w.biome.begin(), w.biome.end(), uint8_t(Biome::ROCK));
	for (int c : { 10, 11 }) { w.farm[size_t(c)] = 0.8f; w.urban[size_t(c)] = 0.8f; }
	w.owner[11] = 0;
	w.rad[12] = 0.5f; w.wet[13] = 0.9f; w.scorch[14] = 0.9f;
	advance(w, 4 * 10, 8);
	near(w.farm[10], 0.8 * std::pow(0.95, 10), 1e-4, "farm -5 %/yr");
	near(w.urban[10], 0.8 * std::pow(0.99, 10), 1e-4, "urban -1 %/yr");
	near(w.farm[11], 0.8, 1e-6, "owned farmland stays");
	near(w.urban[11], 0.8, 1e-6, "owned town stays");
	near(w.rad[12], 0.5 * std::pow(0.985, 10), 1e-4, "rad -1.5 %/yr");
	ok(w.wet[13] < 0.5 && w.wet[13] > 0.1, "wet decays (" + f2(w.wet[13]) + " after 10 years)");
	ok(w.scorch[14] < 0.7 && w.scorch[14] > 0.2, "scorch decays (" + f2(w.scorch[14]) + " after 10 years)");
	advance(w, 4 * 500, 8);
	eq(double(w.farm[10]), 0.0, "ruined fields vanish");
	eq(double(w.wet[13]), 0.0); eq(double(w.scorch[14]), 0.0);
	ok(w.urban[10] < 0.01 && w.rad[12] < 0.001, "ruins and fallout fade");
}

DM_TEST("step: sea cells are zeroed (veg, farm, urban, road, water, fire, scorch)") {
	auto pw = plain(4, { .veg = 0.8 });
	World& w = *pw;
	const int c = 333;
	life::ignite(w, c);
	w.farm[c] = 0.5f; w.urban[c] = 0.5f; w.road[c] = 2; w.water[c] = 0.4f; w.scorch[c] = 0.7f;
	w.elev[c] = -2;                                      // the sea takes it
	w.dirty.roads = false; w.dirty.water = false;
	for (int p = 0; p < 8; p++) life::step(w, p, 8);
	eq(double(w.veg[c]), 0.0, "veg"); eq(double(w.farm[c]), 0.0, "farm"); eq(double(w.urban[c]), 0.0, "urban");
	eq(int(w.road[c]), 0, "road"); eq(double(w.water[c]), 0.0, "water"); eq(double(w.fire[c]), 0.0, "fire"); eq(double(w.scorch[c]), 0.0, "scorch");
	eq(firesMatch(w), std::string(), "and it left the fire list");
	ok(w.dirty.roads && w.dirty.water, "dirty.roads / dirty.water");
	eq(checkFinite(w), std::string());
}

DM_TEST("step: fertility relaxes toward a natural level, higher by fresh water and on old ash") {
	auto make = [](bool special) {
		auto w = plain(4, { .fert = 0.5, .veg = 0 });
		std::fill(w->biome.begin(), w->biome.end(), uint8_t(Biome::GRASSLAND));
		std::fill(w->fresh.begin(), w->fresh.end(), uint8_t(9));
		if (special) w->fresh[200] = 0;
		for (int i = 0; i < 4 * 1500; i++) { w->tick++; if (special && i % 4 == 0) w->scorch[201] = 0.9f; life::step(*w, int(w->tick % 8), 8); }
		return w;
	};
	auto w = make(true), ctl = make(false);
	ok(std::fabs(ctl->fert[300] - 0.5) > 0.01 || std::fabs(ctl->fert[301] - 0.5) > 0.01, "fertility moves away from its start");
	auto again = make(false);
	stepOnly(*again, 4 * 1500, 8);
	near(again->fert[300], ctl->fert[300], 0.01, "and settles at a level");
	ok(w->fert[200] > ctl->fert[200] + 0.15, "river bank " + f2(w->fert[200]) + " against " + f2(ctl->fert[200]) + " for the same ground without water");
	ok(w->fert[201] > ctl->fert[201] + 0.1, "old ash " + f2(w->fert[201]) + " against " + f2(ctl->fert[201]) + " without");
	double lo = 9, hi = 0;
	for (int c = 0; c < w->n; c++) { lo = std::min(lo, double(w->fert[size_t(c)])); hi = std::max(hi, double(w->fert[size_t(c)])); }
	ok(lo >= 0.1 && hi <= 1.5, "fertility stays in range (" + f2(lo) + " .. " + f2(hi) + ")");
}

DM_TEST("step: wildlife follows the vegetation; a BARREN cell that recovers asks for new biomes") {
	auto pw = plain(4, { .veg = 0 });
	World& w = *pw;
	w.vars.lifeBoost = 1;
	advance(w, 80, 8);
	eq(double(w.fauna[77]), 0.0, "no animals on bare ground");
	life::fill(w, 1);
	std::fill(w.fauna.begin(), w.fauna.end(), 0.0f);
	advance(w, 4 * 40, 8);
	ok(w.fauna[77] > 0.6, "animals fill a forest (" + f2(w.fauna[77]) + ")");
	w.veg[77] = 0; w.farm[77] = 1; w.owner[77] = 0;      // cleared and kept under the plough
	advance(w, 4 * 40, 8);
	ok(w.fauna[77] < 0.05, "and leave cleared land (" + f2(w.fauna[77], 3) + ")");
	auto pb = plain(4, { .veg = 0 });
	World& b = *pb;
	b.lava[50] = 0.5f;
	life::classify(b);
	eq(int(b.biome[50]), int(Biome::BARREN));
	b.need.life = false;
	advance(b, 16, 8);
	eq(b.need.life, false, "still molten: nothing to do");
	b.lava[50] = 0;
	advance(b, 16, 8);
	eq(b.need.life, true, "cooled: need.life is set");
}

// ================================================================== bloom, cover
DM_TEST("bloom: raises vegetation toward capacity and fertility by amount x falloff") {
	auto pw = plain(4, { .fert = 0.5, .veg = 0 });
	World& w = *pw;
	const int c0 = 420;
	const double ang = 0.3;
	const int sea = w.geo->nbr[6 * c0], ash = w.geo->nbr[6 * c0 + 1];
	w.elev[size_t(sea)] = -1;
	w.scorch[size_t(ash)] = 0.5f;
	life::bloom(w, c0, ang, 0.3);
	near(w.veg[size_t(c0)], 0.3, 1e-6, "centre gets the full amount");
	near(w.fert[size_t(c0)], 0.8, 1e-6, "and its fertility");
	eq(double(w.veg[size_t(sea)]), 0.0, "nothing grows on the sea");
	near(w.fert[size_t(sea)], 0.5, 1e-6, "and the sea bed is left alone");
	ok(w.scorch[size_t(ash)] < 0.5, "ash is cleared");
	int bad = 0, touched = 0;
	for (int c = 0; c < w.n; c++) {
		const double d = w.geo->angle(c0, c);
		if (d >= ang) { if ((w.veg[size_t(c)] != 0 || std::fabs(w.fert[size_t(c)] - 0.5) > 1e-6) && bad++ < 3) fail("cell " + std::to_string(c) + " outside the brush changed"); }
		else if (isLand(w, c)) { touched++; if (!(w.veg[size_t(c)] <= 0.3 + 1e-6 && w.fert[size_t(c)] >= 0.5 - 1e-6 && w.fert[size_t(c)] <= 0.8 + 1e-6) && bad++ < 3) fail("bounded by the amount"); }
	}
	ok(touched > 10, "cells touched: " + std::to_string(touched));
	const int mid = findLand(w, [&](int c) { const double d = w.geo->angle(c0, c); return d > 0.45 * ang && d < 0.55 * ang; });
	ok(w.veg[size_t(mid)] > 0.08 && w.veg[size_t(mid)] < 0.22, "smooth falloff (half way out: " + f2(w.veg[size_t(mid)]) + ")");
	for (int i = 0; i < 20; i++) life::bloom(w, c0, ang, 0.3);
	near(w.veg[size_t(c0)], life::capacity(w, c0), 1e-6, "never beyond capacity");
	ok(w.fert[size_t(c0)] <= 1.5 + 1e-6, "fertility capped at 1.5");
	const std::vector<float> before = w.veg;
	life::bloom(w, -3, ang, 0.3);
	life::bloom(w, c0, ang, 0);
	life::bloom(w, c0, ang, std::nan(""));
	ok(w.veg == before, "bad cell / zero / NaN amount: nothing happens");
}

DM_TEST("cover: land share, vegetation / capacity over plantable land, forest share; cached per tick") {
	auto pw = plain(4, { .veg = 0 });
	World& w = *pw;
	for (int c = 0; c < w.n; c++) if (c % 2) w.veg[size_t(c)] = float(life::capacity(w, c));
	for (int c = 0; c < w.n; c += 4) w.elev[size_t(c)] = -1;   // a quarter of the planet under water
	for (int p = 0; p < 2; p++) life::step(w, p, 2);
	w.tick++;
	const life::Cover a = life::cover(w);
	near(a.land, 0.75, 1e-3, "land");
	near(a.veg, 2.0 / 3, 0.01, "two thirds of the remaining land is at capacity");
	near(a.forest, 2.0 / 3, 0.01, "and wooded");
	life::fill(w, 1);
	near(life::cover(w).veg, 1, 1e-6, "an edit invalidates the cache");
	auto pe = plain(4, { .veg = 0 });
	std::fill(pe->elev.begin(), pe->elev.end(), -1.0f);
	const life::Cover e = life::cover(*pe);
	ok(e.land == 0 && e.veg == 0 && e.forest == 0, "no land at all: zeros");
}

// ================================================================== habitability
DM_TEST("habitability: 0..1, zero on sea, lakes, ice, rock, snow peaks, lava and barren ground; landHab") {
	for (uint32_t seed : { 1u, 4u }) {
		auto pw = planet(5, seed);
		World& w = *pw;
		const int c1 = findLand(w, [&](int c) { return w.biome[size_t(c)] == Biome::FOREST; });
		const int c2 = findLand(w, [&](int c) { return w.biome[size_t(c)] == Biome::FOREST && c != c1; });
		const int c3 = findLand(w, [&](int c) { return w.biome[size_t(c)] == Biome::TAIGA || w.biome[size_t(c)] == Biome::GRASSLAND; });
		if (!ok(c1 >= 0 && c2 >= 0 && c3 >= 0, "test cells exist")) continue;
		w.water[size_t(c1)] = 0.5f; w.lava[size_t(c2)] = 0.5f; w.scorch[size_t(c3)] = 0.95f; w.veg[size_t(c3)] = 0;
		life::classify(w);
		const LandIds ids = landIds(w);
		w.cache.landId.assign(ids.id.begin(), ids.id.end());
		w.cache.landCount = int(ids.size.size());
		w.need.life = true;
		life::habitability(w);
		eq(w.need.life, false, "clears need.life");
		std::vector<int32_t> want(ids.size.size(), 0);
		int good = 0, bad = 0;
		for (int c = 0; c < w.n; c++) {
			const size_t i = size_t(c);
			const double h = w.hab[i];
			const int b = w.biome[i];
			if (!(h >= 0 && h <= 1) && bad++ < 3) fail("hab out of range at " + std::to_string(c));
			const bool dead = !isLand(w, c) || w.water[i] > 0.03 || w.ice[i] > 0.5 || w.lava[i] > 0.05 || b == Biome::ICE || b == Biome::ROCK || b == Biome::SNOWPEAK || b == Biome::BARREN;
			if (dead && h != 0 && bad++ < 3) fail(std::string(kBiomeNames[b]) + " cell " + std::to_string(c) + " has hab " + std::to_string(h));
			if (h > 0.05) want[size_t(ids.id[i])]++;
			if (h > 0.5) good++;
		}
		eq(double(w.hab[size_t(c1)]), 0.0, "lake"); eq(double(w.hab[size_t(c2)]), 0.0, "lava"); eq(double(w.hab[size_t(c3)]), 0.0, "barren");
		ok(good > 100, "the world has good land (" + std::to_string(good) + " cells above 0.5)");
		ok(w.cache.landHab == want, "landHab = habitable cells per landmass");
	}
	auto pw = planet(4, 2);
	pw->cache.landId.clear(); pw->cache.landCount = 0;
	life::habitability(*pw);
	eq(int(pw->cache.landHab.size()), 0, "no landmass labels: an empty table");
}

DM_TEST("habitability: temperature, water, soil, slope, coast and fallout each count; a river makes a desert liveable") {
	auto pw = plain(4, { .temp = 16, .moist = 0.5, .fert = 0.6 });
	World& w = *pw;
	const double sk = w.geo->step / TL(w).refStep;
	std::fill(w.coast.begin(), w.coast.end(), uint8_t(20));
	const size_t base = 30, cold = 31, hot = 32, freezing = 33, dry = 34, oasis = 35, bank2 = 36, bank3 = 37, poor = 38, steep = 39, cliff = 40, shore = 41, hotspot = 42, mild = 43, warm = 44;
	w.temp[cold] = -4; w.temp[hot] = 37; w.temp[freezing] = -12;
	w.moist[dry] = 0.02f; w.moist[oasis] = 0.02f; w.fresh[oasis] = 1; w.moist[bank2] = 0.02f; w.fresh[bank2] = 2; w.moist[bank3] = 0.02f; w.fresh[bank3] = 3;
	w.fert[poor] = 0.1f; w.slope[steep] = float(1.2 * sk); w.slope[cliff] = float(2 * sk);
	w.coast[shore] = 1; w.rad[hotspot] = 0.6f; w.temp[mild] = 9; w.temp[warm] = 25;
	life::classify(w);
	for (size_t c : { dry, oasis, bank2, bank3 }) eq(int(w.biome[c]), int(Biome::DESERT), "desert cell " + std::to_string(c));
	life::habitability(w);
	const std::vector<float>& h = w.hab;
	ok(h[base] > 0.6, "good land scores high (" + f2(h[base]) + ")");
	ok(h[cold] < 0.3 * h[base] && h[hot] < 0.3 * h[base] && h[freezing] == 0, "too cold / too hot");
	near(h[mild], h[base], 1e-6, "anything from 8 to 26 C is equally good");
	near(h[warm], h[base], 1e-6);
	eq(double(h[dry]), 0.0, "desert without water");
	ok(h[oasis] > 0.4 && h[bank2] > 0.25 && h[bank2] <= h[oasis], "fresh water within 2 hops makes it liveable (" + f2(h[oasis]) + ", " + f2(h[bank2]) + ")");
	eq(double(h[bank3]), 0.0, "three hops is too far");
	ok(h[poor] < h[base] && h[poor] > 0.5 * h[base], "poor soil costs something");
	ok(h[steep] < 0.6 * h[base] && h[steep] > 0 && h[cliff] == 0, "slopes");
	ok(h[shore] > h[base] && h[shore] < 1.2 * h[base], "a small coastal bonus");
	near(h[hotspot], 0.4 * h[base], 1e-4, "x (1 - rad)");
}

DM_TEST("habitability: river valleys in dry land beat the surrounding desert (helper world)") {
	auto pw = makeWorld({ .L = 5, .seed = 3, .land = 0.6 });
	World& w = *pw;
	const Geo& g = *w.geo;
	for (int c = 0; c < w.n; c++) if (isLand(w, c)) { w.moist[size_t(c)] *= 0.12f; w.fresh[size_t(c)] = 255; }
	std::vector<uint8_t> river(size_t(w.n), 0);
	int made = 0;
	for (int s = 0; s < w.n && made < 60; s += 37) {
		if (!isLand(w, s) || w.coast[size_t(s)] < 3 || w.temp[size_t(s)] < 10) continue;
		int c = s, len = 0;
		while (isLand(w, c) && len < 80) {
			river[size_t(c)] = 1; len++;
			int lo = c;
			for (int j = 0; j < g.nbrN[size_t(c)]; j++) { const int m = g.nbr[size_t(6 * c + j)]; if (w.elev[size_t(m)] < w.elev[size_t(lo)]) lo = m; }
			if (lo == c) break;
			c = lo;
		}
		made++;
	}
	std::vector<int> q;
	for (int c = 0; c < w.n; c++) if (river[size_t(c)]) { w.fresh[size_t(c)] = 0; q.push_back(c); }
	for (size_t hh = 0; hh < q.size(); hh++) {
		const int c = q[hh];
		for (int j = 0; j < g.nbrN[size_t(c)]; j++) {
			const int m = g.nbr[size_t(6 * c + j)];
			if (isLand(w, m) && w.fresh[size_t(m)] > w.fresh[size_t(c)] + 1) { w.fresh[size_t(m)] = uint8_t(w.fresh[size_t(c)] + 1); q.push_back(m); }
		}
	}
	life::classify(w);
	life::habitability(w);
	double sv = 0, sd = 0;
	int nv = 0, nd = 0;
	const double sk = TL(w).refStep / g.step;
	for (int c = 0; c < w.n; c++) {
		if (w.biome[size_t(c)] != Biome::DESERT || w.slope[size_t(c)] * sk > 1) continue;
		if (w.fresh[size_t(c)] <= 2) { sv += w.hab[size_t(c)]; nv++; } else if (w.fresh[size_t(c)] >= 4) { sd += w.hab[size_t(c)]; nd++; }
	}
	say("arid helper world: mean hab " + f2(sv / std::max(1, nv)) + " in " + std::to_string(nv) + " desert valley cells (fresh water <= 2 hops), " + f2(sd / std::max(1, nd), 3) + " in " + std::to_string(nd) + " open desert cells");
	ok(nv > 100 && nd > 500, "both kinds of ground exist (" + std::to_string(nv) + ", " + std::to_string(nd) + ")");
	ok(sv / nv > 0.2 && sv / nv > 5 * (sd / nd) + 0.1, "valleys " + f2(sv / nv) + " vs desert " + f2(sd / nd, 3));
}

// ================================================================== robustness, determinism, speed
DM_TEST("robust: no sea, no land, the poles, bad arguments, any phase; never NaN") {
	auto run = [](World& w, const std::string& label) {
		for (Phase ph : { Phase::Molten, Phase::Water, Phase::Life, Phase::People, Phase::History, Phase::Ended }) {
			w.phase = ph;
			life::classify(w); life::habitability(w); life::rebuild(w);
			life::seed(w, 0, 0.3); life::seed(w, 11, 0.3); life::bloom(w, 0, 0.2, 0.2); life::ignite(w, 0); life::ignite(w, 11);
			advance(w, 12, 8); advance(w, 4, 2); advance(w, 2, 1);
			life::fill(w, 0.7); (void)life::cover(w);
			life::step(w, 0, 0); life::step(w, -3, 8); life::step(w, 99, 8); life::step(w, 0, -5); life::step(w, 1, 200);
		}
		if (!std::isfinite(w.vars.lifeBoost)) w.vars.lifeBoost = 1;     // (the test's own NaN: Life never writes vars)
		eq(checkFinite(w), std::string(), label);
		eq(firesMatch(w), std::string(), label);
	};
	{ auto w = plain(4, { .veg = 0.5 }); run(*w, "no sea"); }
	{ auto w = plain(4); std::fill(w->elev.begin(), w->elev.end(), -3.0f); run(*w, "no land"); }
	{ auto w = plain(4, { .temp = -40 }); std::fill(w->ice.begin(), w->ice.end(), 1.0f); run(*w, "all ice"); }
	{ auto w = planet(4, 5); w->vars.lifeBoost = 1e9; run(*w, "absurd boost"); }
	{ auto w = planet(4, 6); w->vars.lifeBoost = std::nan(""); w->vars.seaBase.reset(); w->seaLevel = -9; run(*w, "sea level -9, NaN boost"); }
	{ auto w = planet(4, 6); w->vars = Vars{}; run(*w, "empty vars"); }
	{
		// garbage in the fields the other modules own must not spread
		auto pg = planet(4, 7);
		World& g = *pg;
		const float nan = std::nanf("");
		g.moist[5] = nan; g.temp[6] = nan; g.slope[7] = nan;
		const int f0 = findLand(g), v0 = findLand(g, [](int c) { return c > 300; }), v1 = findLand(g, [](int c) { return c > 900; });
		g.fert[size_t(f0)] = nan; g.veg[size_t(v0)] = nan; g.veg[size_t(v1)] = -0.5f;
		const int l0 = findLand(g, [](int c) { return c > 1200; });
		g.farm[size_t(l0)] = nan; g.wet[size_t(l0 + 1)] = nan;
		for (int c = 0; c < g.n; c += 97) g.temp[size_t(c)] = nan;
		advance(g, 16, 8); life::classify(g); life::habitability(g); life::fill(g, 0.5); advance(g, 16, 8);
		int bad = 0;
		for (int c = 0; c < g.n; c++) {
			const size_t i = size_t(c);
			const float vals[] = { g.veg[i], g.fauna[i], g.hab[i], g.fire[i], g.scorch[i], g.wet[i], g.rad[i], g.urban[i], g.fert[i] };
			for (float v : vals) if (!std::isfinite(v) && bad++ < 3) fail("non-finite Life field at cell " + std::to_string(c));
		}
		ok(std::isfinite(g.fert[size_t(f0)]), "a NaN fertility is repaired");
	}
}

DM_TEST("deterministic: same seed and calls -> same world; another world in between does not matter") {
	auto run = [](uint32_t seed) {
		auto pw = planet(4, seed, true);
		World& w = *pw;
		const LandIds ids = landIds(w);
		const int c0 = heart(w, ids, biggest(ids));
		w.vars.lifeBoost = 5;
		life::seed(w, c0, 0.2);
		for (int t = 1; t <= 300; t++) {
			w.tick++; life::burn(w); life::step(w, int(w.tick % 8), 8);
			if (t == 150) { life::ignite(w, c0); life::bloom(w, c0, 0.1, 0.2); }
			if (t == 200) { auto big = planet(5, 1); advance(*big, 3, 8); life::seed(*big, 40, 0.1); (void)life::cover(*big); }
		}
		return hash(w);
	};
	const uint32_t a = run(3);
	eq(run(3), a, "two runs of seed 3");
	ok(run(4) != a, "another seed differs");
}

DM_TEST("speed at L = 6: one slice of 8 <= 0.6 ms, classify <= 6 ms (asserted at 5x)") {
	auto pw = makeWorld({ .L = 6, .seed = 1 });
	World& w = *pw;
	life::classify(w); life::fill(w, 1);
	stepOnly(w, 40, 8);
	const int N = 400;
	double worst = 0;
	const double t0 = nowMs();
	for (int i = 0; i < N; i++) { w.tick++; const double s = nowMs(); life::step(w, int(w.tick % 8), 8); worst = std::max(worst, nowMs() - s); }
	const double stepMs = (nowMs() - t0) / N;
	double t1 = nowMs();
	for (int i = 0; i < 20; i++) life::classify(w);
	const double cls = (nowMs() - t1) / 20;
	t1 = nowMs();
	for (int i = 0; i < 20; i++) life::habitability(w);
	const double hab = (nowMs() - t1) / 20;
	t1 = nowMs();
	for (int i = 0; i < 20; i++) { w.tick++; (void)life::cover(w); }
	const double cov = (nowMs() - t1) / 20;
	// the life phase: an empty world greening at boost 25, 2 slices
	auto pg = makeWorld({ .L = 6, .seed = 2 });
	World& g = *pg;
	life::classify(g);
	std::fill(g.veg.begin(), g.veg.end(), 0.0f);
	g.vars.lifeBoost = 25;
	const LandIds ids = landIds(g);
	life::seed(g, heart(g, ids, biggest(ids)), 0.12);
	t1 = nowMs();
	for (int i = 0; i < 200; i++) { g.tick++; life::burn(g); life::step(g, int(g.tick % 2), 2); }
	const double gen = (nowMs() - t1) / 200;
	say("L = 6 timings: step (1 slice of 8) mean " + f2(stepMs, 3) + " ms, worst " + f2(worst, 3) + " ms; classify " + f2(cls, 3) + " ms; habitability " + f2(hab, 3) +
		" ms; cover " + f2(cov, 3) + " ms; life-phase tick (2 slices, boost 25) " + f2(gen, 3) + " ms");
	ok(stepMs <= 3, "step slice mean " + f2(stepMs, 3) + " ms");
	ok(cls <= 30, "classify " + f2(cls, 3) + " ms");
	ok(hab <= 30, "habitability " + f2(hab, 3) + " ms");
}

// ================================================================== maps (look at them)
DM_TEST("maps: biome and habitability of helper worlds; the genesis wave; a drought fire") {
	for (uint32_t seed : { 1u, 2u }) {
		auto w = planet(6, seed);
		mi::biome(*w, shot("ue-life-biome-helper-s" + std::to_string(seed) + ".png"));
		life::habitability(*w);
		mi::field(*w, "hab", shot("ue-life-hab-helper-s" + std::to_string(seed) + ".png"), 0, 1, { .rivers = false, .dimSea = true });
	}
	{
		// the five biome test worlds at L = 6, as they are classified (the arid and cold ones show the dry and icy rules)
		const Five& arid = kFive[2];
		auto w = fiveWorld(arid, 6);
		life::classify(*w);
		mi::biome(*w, shot("ue-life-biome-arid.png"));
	}
	// the life phase from one seed: frames, and an arrival map (tick each cell first turned green, bands of 20 ticks)
	auto wave = [](World& w, const std::string& tag, int c0, double half, const std::vector<int>& frames) {
		std::fill(w.veg.begin(), w.veg.end(), 0.0f);
		std::fill(w.fauna.begin(), w.fauna.end(), 0.0f);
		w.vars.lifeBoost = 25; w.phase = Phase::Life; w.tick = 0;
		const LandIds ids = landIds(w);
		const int k = ids.id[size_t(c0)];
		std::vector<int> first(size_t(w.n), -1);
		const int last = frames.back();
		life::seed(w, c0, 0.12);
		auto lk = [&w](int c) { return look(w, c); };
		zoom(w, shot("ue-life-" + tag + "-t0.png"), c0, half, lk);
		std::string line = tag + " (landmass of " + std::to_string(ids.size[size_t(k)]) + " cells):";
		for (int t = 0; t <= last; t++) {
			if (t > 0) { w.tick++; life::burn(w); life::step(w, int(w.tick % 2), 2); }
			for (int c = 0; c < w.n; c++) if (first[size_t(c)] < 0 && w.veg[size_t(c)] > 0) first[size_t(c)] = t;
			if (std::find(frames.begin(), frames.end(), t) != frames.end()) {
				zoom(w, shot("ue-life-" + tag + "-t" + std::to_string(t) + ".png"), c0, half, lk);
				mi::custom(w, shot("ue-life-" + tag + "-globe-t" + std::to_string(t) + ".png"), lk, { .rivers = false });
				const LandCov s = landCover(w, ids, k);
				line += " t" + std::to_string(t) + " " + f2(100 * s.cover, 1) + "% (bare " + std::to_string(s.bare) + "/" + std::to_string(s.plant) + ")";
			}
		}
		say(line);
		const std::vector<std::pair<double, mi::Rgb>> RAMP = { { 0, { 255, 250, 150 } }, { 0.2, { 150, 215, 90 } }, { 0.45, { 50, 160, 110 } }, { 0.7, { 40, 100, 160 } }, { 1, { 80, 40, 120 } } };
		std::vector<int> times;
		for (int c = 0; c < w.n; c++) if (ids.id[size_t(c)] == k && first[size_t(c)] >= 0) times.push_back(first[size_t(c)]);
		std::sort(times.begin(), times.end());
		const int span = std::max(20, times.empty() ? last : times[size_t(std::floor(0.97 * (times.size() - 1)))]);
		say(tag + ": " + std::to_string(times.size()) + " cells of the landmass turned green; half by tick " + std::to_string(times.empty() ? -1 : times[times.size() / 2]) + ", 97 % by tick " + std::to_string(span));
		auto arrival = [&](double range) {
			return [&, range](int c) -> mi::Rgb {
				const size_t i = size_t(c);
				if (!isLand(w, c)) return { 14, 24, 56 };
				if (w.ice[i] > 0.5) return { 226, 232, 240 };
				if (w.water[i] > 0.03) return { 60, 100, 180 };
				if (first[i] < 0) return life::capacity(w, c) > w.tune<LifeTune>().plantMin ? mi::Rgb{ 150, 122, 92 } : mi::Rgb{ 112, 106, 102 };
				const mi::Rgb k2 = mi::ramp(RAMP, first[i] / range);
				const double band = (first[i] / 20) % 2 ? 0.84 : 1.0;
				return { k2.r * band, k2.g * band, k2.b * band };
			};
		};
		zoom(w, shot("ue-life-" + tag + "-arrival.png"), c0, half, arrival(span));
		mi::custom(w, shot("ue-life-" + tag + "-globe-arrival.png"), arrival(last), { .rivers = false, .marks = { c0 } });
	};
	{ auto w = plain(6, { .moist = 0.55 }); wave(*w, "front-uniform", 100, 0.5, { 15, 30, 60 }); }
	{ auto w = planet(6, 2); const LandIds ids = landIds(*w); wave(*w, "wave-helper", heart(*w, ids, biggest(ids)), 0.7, { 30, 60, 120, 240 }); }
	{
		// a fire under drought on a helper continent: the front (orange) and the scar behind it
		auto pw = planet(6, 2);
		World& w = *pw;
		const LandIds ids = landIds(w);
		const int c0 = heart(w, ids, biggest(ids));
		life::fill(w, 1);
		for (int c = 0; c < w.n; c++) if (isLand(w, c)) w.moist[size_t(c)] *= 0.2f;
		std::vector<int32_t> buf;
		const int cnt = w.geo->ring(c0, 3, buf);
		for (int i = 0; i < cnt && burning(w) < 30; i++) {
			if (burning(w) == 0) life::ignite(w, buf[size_t(i)]);
			for (int t = 0; t < 12 && burning(w) > 0 && burning(w) < 30; t++) { w.tick++; life::burn(w); }
		}
		for (int t = 0; t < 16; t++) { w.tick++; life::burn(w); }
		zoom(w, shot("ue-life-fire-drought.png"), c0, 0.45, [&w](int c) { return look(w, c); });
		say("drought fire map: " + std::to_string(burning(w)) + " cells burning on the front");
	}
	ok(true);
}
