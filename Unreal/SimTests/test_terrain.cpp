// DEMIURGE — SimTests/test_terrain.cpp : dm::terrain (SPEC-SIM 3.2, BUILD.md A2). Port of
// tests/terrain.test.js plus the A2 acceptance checks and the maps.
//   run.ps1 terrain                 every test (every other module stubbed; Terrain calls none)
//   TERRAIN_REPORT=1 run.ps1 terrain  also prints the per-seed acceptance table and timings
// Maps: shots/ue-terrain-elev-s<seed>.png (L 6), ue-terrain-elev-L5-s<seed>.png, ue-terrain-slope.png,
//       ue-terrain-land.png, ue-terrain-fert.png, ue-terrain-ops.png.
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "MapImg.h"
#include "Terrain.h"
#include "TestFramework.h"
#include "TestWorld.h"

using namespace dm;
using namespace dmt;

namespace {

bool report() { const char* r = std::getenv("TERRAIN_REPORT"); return r && *r && *r != '0'; }

std::unique_ptr<World> fresh(int L, uint32_t seed = 1) {
	auto w = state::create({ .seed = seed, .L = L, .geo = sharedGeo(L) });
	watch(*w);
	return w;
}
void clearFlags(World& w) {
	w.dirty = Dirty{ false, false, false, false, false, false };
	w.need = Need{ false, false, false, false };
}
std::unique_ptr<World> generated(int L, uint32_t seed) {
	auto w = fresh(L, seed);
	terrain::generate(*w);
	w->seaLevel = 0;
	clearFlags(*w);
	return w;
}
std::unique_ptr<World> flat(int L, float level) {
	auto w = fresh(L, 1);
	std::fill(w->elev.begin(), w->elev.end(), level);
	w->seaLevel = 0;
	clearFlags(*w);
	return w;
}
bool allFinite(const World& w) {
	for (int c = 0; c < w.n; c++) {
		const float e = w.elev[size_t(c)], f = w.fert[size_t(c)];
		if (!(e >= state::kElevMin && e <= state::kElevMax) || !(f >= 0 && f <= 1.5)) return false;
	}
	return true;
}
bool dirtyIs(const World& w, bool elev, bool color, bool water, bool objects, bool rivers, bool roads) {
	return w.dirty.elev == elev && w.dirty.color == color && w.dirty.water == water && w.dirty.objects == objects && w.dirty.rivers == rivers &&
	       w.dirty.roads == roads;
}
bool needIs(const World& w, bool derive, bool climate, bool hydro, bool life) {
	return w.need.derive == derive && w.need.climate == climate && w.need.hydro == hydro && w.need.life == life;
}
bool touchedFlags(const World& w) {
	return w.dirty.elev && w.dirty.water && w.dirty.color && w.need.derive && w.need.climate && w.need.hydro;
}
std::string fmt(double v, int dec = 2) {
	char b[64];
	std::snprintf(b, sizeof b, "%.*f", dec, v);
	return b;
}
std::string S(int v) { return std::to_string(v); }

// Connected land components by an explicit-stack flood fill (independent of terrain::derive).
struct Comps { std::vector<int> lab, sizes; };
Comps components(const World& w, double sea) {
	const Geo& g = *w.geo;
	Comps r;
	r.lab.assign(size_t(w.n), -1);
	std::vector<int> stack;
	for (int c0 = 0; c0 < w.n; c0++) {
		if (r.lab[size_t(c0)] >= 0 || w.elev[size_t(c0)] < sea) continue;
		const int id = int(r.sizes.size());
		int size = 0;
		stack.push_back(c0);
		r.lab[size_t(c0)] = id;
		while (!stack.empty()) {
			const int c = stack.back();
			stack.pop_back();
			size++;
			for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
				const int m = g.nbr[size_t(6 * c + k)];
				if (r.lab[size_t(m)] < 0 && w.elev[size_t(m)] >= sea) { r.lab[size_t(m)] = id; stack.push_back(m); }
			}
		}
		r.sizes.push_back(size);
	}
	return r;
}

// Everything the acceptance checks and the generation targets look at, at sea level 0.
struct TStats {
	double land = 0, lo = 1e9, hi = -1e9, low = 0, high = 0, shelf = 0, deep = 0, polar = 0, belt = 0, top = 0, second = 0;
	std::vector<int> sizes;
	int majors3 = 0, majors2 = 0, islands = 0;
	int coastMax = 0;
};
TStats stats(const World& w) {
	const Geo& g = *w.geo;
	const int n = w.n;
	TStats s;
	int land = 0, low = 0, high = 0, shelf = 0, deep = 0, polar = 0, belt = 0;
	for (int c = 0; c < n; c++) {
		const double v = w.elev[size_t(c)];
		if (v < s.lo) s.lo = v;
		if (v > s.hi) s.hi = v;
		if (v >= 0) {
			land++;
			if (v < 0.6) low++;
			if (std::fabs(g.lat[size_t(c)]) > kPi / 3) polar++;
			if (v > 3.5) {
				high++;
				int k = 0;
				for (int j = 0; j < g.nbrN[size_t(c)]; j++) if (w.elev[size_t(g.nbr[size_t(6 * c + j)])] > 2.5) k++;
				if (k >= 2) belt++;
			}
		} else {
			if (v > -0.6) shelf++;
			if (v < -3) deep++;
		}
	}
	s.sizes = components(w, 0).sizes;
	std::sort(s.sizes.begin(), s.sizes.end(), [](int a, int b) { return a > b; });
	const int sea = n - land;
	s.land = double(land) / n;
	s.low = land ? double(low) / land : 0;
	s.high = land ? double(high) / land : 0;
	s.shelf = sea ? double(shelf) / sea : 0;
	s.deep = sea ? double(deep) / sea : 0;
	s.polar = land ? double(polar) / land : 0;
	s.belt = high ? double(belt) / high : 0;
	s.top = land && !s.sizes.empty() ? double(s.sizes[0]) / land : 0;
	for (const int sz : s.sizes) {
		if (sz >= 0.03 * n) s.majors3++;
		if (sz >= 0.02 * n) s.majors2++;
	}
	s.islands = int(s.sizes.size()) - s.majors3;
	s.second = s.sizes.size() > 1 ? double(s.sizes[1]) / n : 0;
	return s;
}

// BUILD.md A2, first sentence.
void checkAcceptance(const TStats& s, const std::string& tag) {
	ok(s.land >= 0.36 && s.land <= 0.42, tag + ": land fraction " + fmt(s.land, 4));
	ok(s.lo >= -7 && s.hi <= 8.6, tag + ": elevation range " + fmt(s.lo) + " .. " + fmt(s.hi));
	ok(s.low >= 0.25, tag + ": share of land below +0.6 = " + fmt(s.low, 3));
	ok(s.high >= 0.02, tag + ": share of land above +3.5 = " + fmt(s.high, 3));
	ok(s.majors3 >= 2, tag + ": landmasses >= 3 % of the planet = " + S(s.majors3));
	ok(s.islands >= 3, tag + ": further islands = " + S(s.islands));
}
std::string row(int L, uint32_t seed, const TStats& s, double ms = -1) {
	std::string masses;
	const double n = double(Geo::prefix(L));
	for (size_t i = 0; i < s.sizes.size() && i < 6; i++) masses += (i ? "/" : "") + fmt(100 * s.sizes[i] / n, 1);
	std::string out = "L" + S(L) + " seed " + (seed < 10 ? " " : "") + std::to_string(seed) + "  land " + fmt(100 * s.land, 1) + " %  elev " + fmt(s.lo) +
	                  " .. " + fmt(s.hi) + "  <0.6: " + fmt(100 * s.low, 0) + " %  >3.5: " + fmt(100 * s.high, 1) + " %  masses(% planet) " + masses +
	                  "  islands " + S(s.islands) + "  shelf " + fmt(100 * s.shelf, 0) + " %  deep " + fmt(100 * s.deep, 0) + " %  polar " +
	                  fmt(100 * s.polar, 1) + " %  belt " + fmt(100 * s.belt, 0) + " %";
	if (ms >= 0) out += "  " + fmt(ms, 0) + " ms";
	return out;
}

// land hypsometry: share of land in elevation bands (report only)
std::string hypso(const World& w) {
	static const double edges[] = { 0, 0.3, 0.6, 1, 1.5, 2, 3, 4.5, 99 };
	int cnt[8] = {}, land = 0;
	for (int c = 0; c < w.n; c++) {
		const double e = w.elev[size_t(c)];
		if (e < 0) continue;
		land++;
		for (int b = 0; b < 8; b++) if (e < edges[b + 1]) { cnt[b]++; break; }
	}
	std::string out = "    land by height:";
	for (int b = 0; b < 8; b++) out += " " + fmt(edges[b], 1) + "+:" + fmt(100.0 * cnt[b] / std::max(1, land), 0) + "%";
	return out;
}

// A shaded-relief map (light from the north-west, strong vertical exaggeration) in cartographic colours:
// what a reader sees of belts, plateaus, trenches and ridges that a flat tint hides.
bool reliefMap(const World& w, const std::string& file) {
	namespace mi = dm::mapimg;
	const Geo& g = *w.geo;
	std::vector<double> shade(size_t(w.n), 0.0);
	for (int c = 0; c < w.n; c++) {
		Vec3 east, north;
		g.basis(c, east, north);
		const Vec3 pc = g.dir(c);
		double gx = 0, gy = 0;
		const int deg = g.nbrN[size_t(c)];
		for (int k = 0; k < deg; k++) {
			const int m = g.nbr[size_t(6 * c + k)];
			const Vec3 d = (g.dir(m) - pc) * kPlanetR;
			const double dx = v3::dot(d, east), dy = v3::dot(d, north), l2 = dx * dx + dy * dy;
			const bool landC = w.elev[size_t(c)] >= w.seaLevel;     // land is shaded against the shore, not the sea floor
			const double em = landC ? std::max(double(w.elev[size_t(m)]), w.seaLevel) : double(w.elev[size_t(m)]);
			const double de = em - double(w.elev[size_t(c)]);
			if (l2 > 0) { gx += de * dx / l2; gy += de * dy / l2; }
		}
		gx *= 2.0 / deg; gy *= 2.0 / deg;
		shade[size_t(c)] = std::clamp(0.8 + 2.2 * 0.7071 * (gx - gy), 0.25, 1.3);   // light from the north-west
	}
	auto mix = [](mi::Rgb a, mi::Rgb b, double t) { return mi::Rgb{ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; };
	return mi::custom(w, file, [&](int c) {
		const double e = w.elev[size_t(c)] - w.seaLevel;
		mi::Rgb k;
		if (e < 0) {
			const double t = std::min(1.0, -e / 6.5);
			k = t < 0.1 ? mix({ 120, 190, 215 }, { 60, 130, 190 }, t / 0.1) : mix({ 60, 130, 190 }, { 10, 30, 80 }, (t - 0.1) / 0.9);
			const double s = 0.75 + 0.25 * std::min(1.2, shade[size_t(c)]);
			return mi::Rgb{ k.r * s, k.g * s, k.b * s };
		}
		if (e < 0.6) k = mix({ 96, 140, 80 }, { 140, 160, 95 }, e / 0.6);
		else if (e < 1.5) k = mix({ 140, 160, 95 }, { 185, 170, 115 }, (e - 0.6) / 0.9);
		else if (e < 3.5) k = mix({ 185, 170, 115 }, { 150, 120, 90 }, (e - 1.5) / 2.0);
		else if (e < 5.5) k = mix({ 150, 120, 90 }, { 215, 210, 205 }, (e - 3.5) / 2.0);
		else k = { 245, 245, 250 };
		const double s = shade[size_t(c)];
		return mi::Rgb{ std::min(255.0, k.r * s), std::min(255.0, k.g * s), std::min(255.0, k.b * s) };
	}, { .rivers = false });
}

struct Gen5 { std::unique_ptr<World> w; TStats s; };
std::map<uint32_t, Gen5>& cache5() { static std::map<uint32_t, Gen5> m; return m; }
Gen5& world5(uint32_t seed) {
	auto& m = cache5();
	auto it = m.find(seed);
	if (it == m.end()) {
		Gen5 g;
		g.w = generated(5, seed);
		g.s = stats(*g.w);
		it = m.emplace(seed, std::move(g)).first;
	}
	return it->second;
}

// one-cell pits: cells strictly below every neighbour
std::set<int> pits(const World& w) {
	const Geo& g = *w.geo;
	std::set<int> out;
	for (int c = 0; c < w.n; c++) {
		bool pit = true;
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) if (!(w.elev[size_t(g.nbr[size_t(6 * c + k)])] > w.elev[size_t(c)])) { pit = false; break; }
		if (pit) out.insert(c);
	}
	return out;
}

std::vector<float> bruteSlope(const World& w) {
	const Geo& g = *w.geo;
	std::vector<float> out(size_t(w.n), 0.0f);
	for (int c = 0; c < w.n; c++) {
		float s = 0;
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) s = std::max(s, std::fabs(w.elev[size_t(g.nbr[size_t(6 * c + k)])] - w.elev[size_t(c)]));
		out[size_t(c)] = s;
	}
	return out;
}
// hops to the nearest sea cell by repeated relaxation (no queue), capped at 255
std::vector<int> bruteCoast(const World& w) {
	const Geo& g = *w.geo;
	std::vector<int> d(size_t(w.n));
	for (int c = 0; c < w.n; c++) d[size_t(c)] = w.elev[size_t(c)] < w.seaLevel ? 0 : 255;
	for (bool changed = true; changed;) {
		changed = false;
		for (int c = 0; c < w.n; c++) {
			for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
				const int v = d[size_t(g.nbr[size_t(6 * c + k)])] + 1;
				if (v < d[size_t(c)]) { d[size_t(c)] = v; changed = true; }
			}
		}
	}
	return d;
}
// derive against brute force; returns the number of landmasses
int checkDerive(World& w, const std::string& tag) {
	w.need.derive = true;
	w.need.life = false;
	terrain::derive(w);
	eq(w.need.derive, false, tag + ": clears need.derive");
	eq(w.need.life, true, tag + ": sets need.life");
	const int n = w.n;
	const std::vector<float> slope = bruteSlope(w);
	const std::vector<int> coast = bruteCoast(w);
	const Comps comp = components(w, w.seaLevel);
	const std::vector<int32_t>& lid = w.cache.landId;
	if (!eq(lid.size(), size_t(n), tag + ": landId has n entries")) return 0;
	int badSlope = 0, badCoast = 0;
	for (int c = 0; c < n; c++) {
		if (w.slope[size_t(c)] != slope[size_t(c)] && badSlope++ < 3) fail(tag + ": slope[" + S(c) + "] differs from brute force");
		if (int(w.coast[size_t(c)]) != coast[size_t(c)] && badCoast++ < 3) fail(tag + ": coast[" + S(c) + "] = " + S(w.coast[size_t(c)]) + ", brute force " + S(coast[size_t(c)]));
	}
	// labels: -1 exactly on sea cells; otherwise a bijection with the brute-force components
	const int k = int(comp.sizes.size());
	eq(w.cache.landCount, k, tag + ": landCount");
	std::vector<int> toDerived(size_t(k), -1), seen(size_t(k), -1), count(size_t(k), 0), first(size_t(k), -1);
	int bad = 0;
	for (int c = 0; c < n; c++) {
		const int b = comp.lab[size_t(c)], id = lid[size_t(c)];
		if (b < 0) { if (id != -1 && bad++ < 3) fail(tag + ": sea cell " + S(c) + " has label " + S(id)); continue; }
		if (!(id >= 0 && id < k)) { if (bad++ < 3) fail(tag + ": land cell " + S(c) + " has label " + S(id)); continue; }
		if (toDerived[size_t(b)] < 0) {
			if (seen[size_t(id)] >= 0 && bad++ < 3) fail(tag + ": label " + S(id) + " spans two landmasses");
			toDerived[size_t(b)] = id;
			seen[size_t(id)] = b;
		} else if (toDerived[size_t(b)] != id && bad++ < 3) fail(tag + ": one landmass carries two labels");
		count[size_t(id)]++;
		if (first[size_t(id)] < 0) first[size_t(id)] = c;
	}
	for (int i = 0; i < k; i++) {
		if (seen[size_t(i)] >= 0) eq(count[size_t(i)], comp.sizes[size_t(seen[size_t(i)])], tag + ": size of landmass " + S(i));
		if (i > 0) {
			ok(count[size_t(i)] <= count[size_t(i - 1)], tag + ": ids by descending size");
			if (count[size_t(i)] == count[size_t(i - 1)]) ok(first[size_t(i)] > first[size_t(i - 1)], tag + ": equal sizes ordered by lowest cell id");
		}
	}
	return k;
}

} // namespace

// ------------------------------------------------------------------ API surface

DM_TEST("terrain: TerrainTune carries the JS TUNE block with the spec's starting values") {
	auto w = fresh(2, 1);
	const TerrainTune& T = w->tune<TerrainTune>();
	near(T.land, 0.39, 1e-12, "land");
	ok(T.peakMin >= 6 && T.peakMax <= 8.5 && T.peakMin < T.peakMax, "peaks inside +6 .. +8.5");
	ok(T.lowQ >= 0.25 && T.lowE <= 0.6, "lowland guarantee at least the spec's");
	ok(T.deepMin <= 6.5 && T.deepMax <= 7, "trench depth about 6.5");
	eq(T.shelfLim.size(), size_t(3), "three shelf steps");
}

// ------------------------------------------------------------------ generate

DM_TEST("terrain: generate acceptance statistics for 20 seeds at L = 5") {
	for (uint32_t seed = 1; seed <= 20; seed++) {
		const Gen5& g = world5(seed);
		if (report()) std::printf("%s\n", row(5, seed, g.s).c_str());
		checkAcceptance(g.s, "L5 seed " + std::to_string(seed));
	}
}

DM_TEST("terrain: generate acceptance statistics and time for 3 seeds at L = 6") {
	(void)sharedGeo(6);                                            // build the sphere outside the timed part
	double worst = 0, total = 0;
	for (uint32_t seed = 1; seed <= 3; seed++) {
		auto w = fresh(6, seed);
		const double t0 = nowMs();
		terrain::generate(*w);
		const double t = nowMs() - t0;
		worst = std::max(worst, t);
		total += t;
		w->seaLevel = 0;
		const TStats s = stats(*w);
		if (report()) std::printf("%s\n%s\n", row(6, seed, s, t).c_str(), hypso(*w).c_str());
		checkAcceptance(s, "L6 seed " + std::to_string(seed));
		ok(allFinite(*w), "finite elev and fert");
		noNaN(*w, "after generate");
		ok(t < 2500, "generate took " + fmt(t, 0) + " ms (target 500 ms, asserted at 5x)");
	}
	std::printf("  terrain: generate at L = 6: mean %.0f ms, worst %.0f ms (target < 500 ms)\n", total / 3, worst);
}

DM_TEST("terrain: generate gives identical output for identical seeds (20 seeds at L = 5, one at L = 6)") {
	for (uint32_t seed = 1; seed <= 20; seed++) {
		const World& a = *world5(seed).w;
		auto b = fresh(5, seed);
		terrain::generate(*b);
		ok(a.elev == b->elev, "elev differs for seed " + std::to_string(seed));
		ok(a.fert == b->fert, "fert differs for seed " + std::to_string(seed));
	}
	auto a = fresh(6, 3), b = fresh(6, 3);
	terrain::generate(*a);
	terrain::generate(*b);
	ok(a->elev == b->elev && a->fert == b->fert, "L6 seed 3");
	eq(hash(*a), hash(*b), "fingerprints match");
}

// FNV-1a over the raw float bits (the JS reference computed the same over its Float32Arrays).
uint32_t bitsHash(const std::vector<float>& a) {
	uint32_t h = 2166136261u;
	for (const float f : a) {
		uint32_t u;
		std::memcpy(&u, &f, 4);
		h ^= u;
		h *= 16777619u;
	}
	return h;
}

DM_TEST("terrain: generate is bit-identical to the JS reference (js/sim/terrain.js, pinned fingerprints)") {
	struct Pin { int L; uint32_t seed, elev, fert; };
	const Pin pins[] = { { 4, 1, 1882514635u, 1375299932u }, { 5, 1, 2861462420u, 3243500064u }, { 5, 13, 403544639u, 3711514438u },
	                     { 6, 2, 3205904804u, 4009193975u } };
	for (const Pin& p : pins) {
		auto w = fresh(p.L, p.seed);
		terrain::generate(*w);
		const std::string tag = "L" + S(p.L) + " seed " + std::to_string(p.seed);
		eq(bitsHash(w->elev), p.elev, tag + ": elev bits");
		eq(bitsHash(w->fert), p.fert, tag + ": fert bits");
	}
}

DM_TEST("terrain: generate depends on the seed only (not on earlier calls, the name or the rng position)") {
	auto a = fresh(4, 77);
	terrain::generate(*a);
	auto other = fresh(5, 78);
	terrain::generate(*other);                                     // different size and seed in between
	auto b = state::create({ .seed = 77, .name = "Another name", .L = 4 });
	watch(*b);
	for (int i = 0; i < 13; i++) b->rng.next();                    // the world's own stream is not what generate reads
	terrain::generate(*b);
	ok(a->elev == b->elev && a->fert == b->fert, "same seed, different history");
	terrain::generate(*b);                                         // regenerating in place gives the same planet again
	ok(a->elev == b->elev && a->fert == b->fert, "regenerated in place");
	auto c = fresh(4, 78);
	terrain::generate(*c);
	int diff = 0;
	for (int i = 0; i < a->n; i++) if (a->elev[size_t(i)] != c->elev[size_t(i)]) diff++;
	ok(diff > 0.9 * a->n, "another seed gives another planet (" + S(diff) + " of " + S(a->n) + " cells differ)");
}

DM_TEST("terrain: generate writes only elev and fert, does not advance w.rng, emits nothing, sets every flag") {
	auto w = fresh(4, 5);
	clearFlags(*w);
	const uint32_t rng0 = w->rng.s;
	const std::vector<Event> evs = capture(*w, [&] { terrain::generate(*w); });
	eq(evs.size(), size_t(0), "no events");
	eq(w->rng.s, rng0, "w.rng untouched");
	ok(dirtyIs(*w, true, true, true, true, true, true), "every dirty flag");
	ok(needIs(*w, true, true, true, true), "every need flag");
	const int ie = state::fieldIndex("elev"), ifert = state::fieldIndex("fert");
	for (int i = 0; i < state::kCellFieldCount; i++) {
		if (i == ie || i == ifert) continue;
		const CellArray arr = state::cellArray(*w, i);
		bool same = true;
		for (size_t c = 0; c < arr.size; c++) if (arr.get(c) != state::kCellFields[i].def) { same = false; break; }
		ok(same, std::string(state::kCellFields[i].name) + " left at its default");
	}
	eq(w->seaLevel, -9.0, "seaLevel");
	ok(w->phase == Phase::Molten, "phase");
	eq(w->heat, 1.0, "heat");
	eq(w->year, 0.0, "year");
	eq(w->settlements.size() + w->cultures.size() + w->units.size() + w->disasters.size(), size_t(0), "no entities");
	ok(allFinite(*w), "finite, inside the clamps");
}

DM_TEST("terrain: fertility is varied, in range, and richer in the lowlands than in the mountains") {
	for (uint32_t seed = 1; seed <= 3; seed++) {
		const World& w = *world5(seed).w;
		int n = 0, rich = 0, poor = 0, lowN = 0, highN = 0;
		double sum = 0, sum2 = 0, lowSum = 0, highSum = 0;
		bool inRange = true;
		for (int c = 0; c < w.n; c++) {
			const double f = w.fert[size_t(c)];
			if (!(f >= 0 && f <= 1.5)) inRange = false;
			if (w.elev[size_t(c)] < 0) continue;
			n++; sum += f; sum2 += f * f;
			if (f > 0.7) rich++;
			if (f < 0.4) poor++;
			if (w.elev[size_t(c)] < 0.6) { lowSum += f; lowN++; } else if (w.elev[size_t(c)] > 3) { highSum += f; highN++; }
		}
		ok(inRange, "fert in range");
		const double mean = sum / n, sd = std::sqrt(sum2 / n - mean * mean);
		ok(mean > 0.4 && mean < 0.7, "mean land fertility " + fmt(mean, 3));
		ok(sd > 0.08, "standard deviation " + fmt(sd, 3));
		ok(rich > 0.03 * n && poor > 0.03 * n, "rich " + S(rich) + ", poor " + S(poor) + " of " + S(n));
		ok(lowN > 0 && highN > 0 && lowSum / lowN > highSum / highN + 0.1, "lowlands " + fmt(lowSum / std::max(1, lowN)) + " vs mountains " + fmt(highSum / std::max(1, highN)));
	}
}

DM_TEST("terrain: the planet has the shape the spec asks for (20 seeds at L = 5)") {
	int threeOrMore = 0;
	for (uint32_t seed = 1; seed <= 20; seed++) {
		const TStats& s = world5(seed).s;
		const std::string tag = "seed " + std::to_string(seed) + ": ";
		ok(s.hi >= 6 && s.hi <= 8.5, tag + "highest peak " + fmt(s.hi));
		ok(s.lo <= -5.8 && s.lo >= -6.8, tag + "deepest ocean floor " + fmt(s.lo));
		ok(s.shelf >= 0.12, tag + "continental shelf: " + fmt(100 * s.shelf, 0) + " % of the sea is shallower than 0.6");
		ok(s.deep >= 0.3, tag + "deep ocean: " + fmt(100 * s.deep, 0) + " % of the sea is deeper than 3");
		ok(s.polar >= 0.01, tag + "land beyond 60 deg latitude: " + fmt(100 * s.polar, 1) + " % of the land");
		ok(s.majors2 >= 2 && s.majors2 <= 7, tag + "landmasses >= 2 % of the planet: " + S(s.majors2));
		ok(s.top <= 0.75, tag + "largest landmass holds " + fmt(100 * s.top, 0) + " % of the land");
		ok(s.belt >= 0.7, tag + "mountains form belts, not speckle: " + fmt(100 * s.belt, 0) + " % of high cells have high neighbours");
		if (s.majors2 >= 3) threeOrMore++;
	}
	ok(threeOrMore >= 17, "3 or more major landmasses on " + S(threeOrMore) + " of 20 seeds");
	std::printf("  terrain: 3+ major landmasses (>= 2 %% of the planet) on %d of 20 seeds at L = 5\n", threeOrMore);
}

DM_TEST("terrain: generate works at every subdivision level and for edge-case seeds") {
	for (int L = 0; L <= 4; L++) {
		for (const uint32_t seed : { 0u, 1u, 0xffffffffu }) {
			auto w = fresh(L, seed);
			terrain::generate(*w);
			const std::string tag = "L" + S(L) + " seed " + std::to_string(seed);
			ok(allFinite(*w), tag + ": finite, inside the clamps");
			int land = 0;
			for (int c = 0; c < w->n; c++) if (w->elev[size_t(c)] >= 0) land++;
			ok(land > 0 && land < w->n, tag + ": both land and sea");
			if (L >= 3) ok(double(land) / w->n >= 0.36 && double(land) / w->n <= 0.42, tag + ": land fraction " + fmt(double(land) / w->n, 3));
		}
	}
	// a world without a usable grid is left alone (no crash)
	auto bare = fresh(2, 1);
	bare->elev.clear();
	terrain::generate(*bare);
	terrain::derive(*bare);
	ok(bare->elev.empty(), "a world with a broken elev array is not touched");
}

// ------------------------------------------------------------------ touched

DM_TEST("terrain: touched sets dirty.elev / water / color and need.derive / climate / hydro, nothing else") {
	auto w = fresh(3);
	clearFlags(*w);
	terrain::touched(*w);
	ok(dirtyIs(*w, true, true, true, false, false, false), "dirty flags");
	ok(needIs(*w, true, true, true, false), "need flags");
}

// ------------------------------------------------------------------ sculpt

DM_TEST("terrain: sculpt raises the centre by amount, falls smoothly to 0 at ang, touches nothing outside") {
	auto w = flat(5, 1);
	const Geo& g = *w->geo;
	const int c = 1234;
	const double ang = 0.25, amount = 2;
	const int n = terrain::sculpt(*w, c, ang, amount);
	near(w->elev[size_t(c)], 1 + amount, 1e-5, "centre");
	int inside = 0, outsideMoved = 0, badFall = 0;
	double edgeMax = 0, stepMax = 0;
	for (int m = 0; m < w->n; m++) {
		const double t = g.angle(c, m) / ang, d = w->elev[size_t(m)] - 1.0;
		if (t >= 1) { if (w->elev[size_t(m)] != 1.0f) outsideMoved++; continue; }
		inside++;
		const double want = amount * (1 - t * t) * (1 - t * t);
		if (std::fabs(d - want) > 2e-3 * amount && badFall++ < 3) fail("falloff at t = " + fmt(t, 3) + ": " + fmt(d, 4) + " vs " + fmt(want, 4));
		if (!(d >= 0 && d <= amount + 1e-6)) fail("between 0 and amount");
		if (t > 0.9 && d > edgeMax) edgeMax = d;
		for (int k = 0; k < g.nbrN[size_t(m)]; k++) stepMax = std::max(stepMax, double(std::fabs(w->elev[size_t(g.nbr[size_t(6 * m + k)])] - w->elev[size_t(m)])));
	}
	eq(outsideMoved, 0, "cells outside the brush changed");
	ok(inside > 100, "brush covers " + S(inside) + " cells");
	eq(n, inside, "returns the number of cells changed");
	ok(edgeMax < 0.04 * amount, "reaches 0 at the edge without a step (largest change in the outer 10 %: " + fmt(edgeMax, 4) + ")");
	// (1 - t^2)^2 is never steeper than 1.54 / ang: no cliffs between neighbouring cells
	ok(stepMax < 1.54 * amount * (1.25 * g.step) / ang * 1.15, "smooth: largest step between neighbours " + fmt(stepMax, 3));
	ok(touchedFlags(*w), "flags set");
}

DM_TEST("terrain: sculpt with negative amounts lowers, repeated strokes add up, flags are set") {
	auto w = flat(4, 0.5f);
	const int c = 300;
	ok(terrain::sculpt(*w, c, 0.2, -1.5) > 0);
	near(w->elev[size_t(c)], -1, 1e-5, "lowered");
	ok(dirtyIs(*w, true, true, true, false, false, false), "dirty flags");
	ok(needIs(*w, true, true, true, false), "need flags");
	for (int i = 0; i < 10; i++) terrain::sculpt(*w, c, 0.2, 0.15);
	near(w->elev[size_t(c)], 0.5, 1e-4, "ten strokes of +0.15 undo -1.5");
	double worst = 0;
	for (int m = 0; m < w->n; m++) worst = std::max(worst, std::fabs(w->elev[size_t(m)] - 0.5));
	ok(worst < 1e-4, "the whole brush returned to where it was");
}

DM_TEST("terrain: sculpt clamps to kElevMin .. kElevMax") {
	auto w = flat(4, 0);
	const int c = 77;
	terrain::sculpt(*w, c, 0.3, 1000);
	eq(double(w->elev[size_t(c)]), state::kElevMax, "top clamp");
	terrain::sculpt(*w, c, 0.3, -5000);
	eq(double(w->elev[size_t(c)]), state::kElevMin, "bottom clamp");
	bool inside = true;
	for (int m = 0; m < w->n; m++) if (!(w->elev[size_t(m)] >= state::kElevMin && w->elev[size_t(m)] <= state::kElevMax)) inside = false;
	ok(inside, "inside the clamps");
}

DM_TEST("terrain: sculpt with tiny, huge and odd brushes; the poles") {
	auto w = flat(4, 0);
	eq(terrain::sculpt(*w, 10, 0, 1), 1, "ang = 0 moves the centre cell only");
	near(w->elev[10], 1, 1e-6);
	eq(terrain::sculpt(*w, 20, -3, 1), 1, "negative ang behaves as 0");
	eq(terrain::sculpt(*w, 30, std::numeric_limits<double>::quiet_NaN(), 1), 1, "NaN ang behaves as 0");
	eq(terrain::sculpt(*w, 40, w->geo->step * 0.4, 1), 1, "a brush smaller than a cell");
	for (const int pole : { 0, 11 }) {
		const int n = terrain::sculpt(*w, pole, 0.3, 1);
		ok(n > 20, "pole brush covers " + S(n) + " cells");
		near(w->elev[size_t(pole)], 1, 1e-6, "pole centre");
	}
	auto w2 = flat(3, 0);
	const int n = terrain::sculpt(*w2, 5, 10, 1);                  // larger than the planet: clamped to pi
	ok(n >= w2->n - 1, "whole planet");
	bool good = true;
	for (int m = 0; m < w2->n; m++) if (!(std::isfinite(w2->elev[size_t(m)]) && w2->elev[size_t(m)] >= 0 && w2->elev[size_t(m)] <= 1 + 1e-6)) good = false;
	ok(good, "finite");
	noNaN(*w2);
}

DM_TEST("terrain: operations ignore bad targets and amounts: change nothing, set no flags") {
	auto w = generated(4, 3);
	const std::vector<float> before = w->elev;
	Rng rng(1);
	const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
	for (const int c : { -1, w->n, w->n + 5, INT_MIN, INT_MAX }) {
		eq(terrain::sculpt(*w, c, 0.1, 1), 0, "sculpt " + S(c));
		terrain::crater(*w, c, 0.1, 1);
		terrain::cone(*w, c, 0.1, 1);
		terrain::roughen(*w, c, 0.1, 1, rng);
		eq(terrain::addElev(*w, c, 1), 0.0, "addElev " + S(c));
	}
	for (const double a : { 0.0, nan, inf, -inf }) {
		eq(terrain::sculpt(*w, 5, 0.1, a), 0, "sculpt amount " + fmt(a));
		terrain::roughen(*w, 5, 0.1, a, rng);
		eq(terrain::addElev(*w, 5, a == 0.0 ? nan : a), double(before[5]), "addElev amount " + fmt(a));
	}
	for (const double a : { 0.0, -1.0, nan, inf }) {
		terrain::crater(*w, 5, 0.1, a);
		terrain::cone(*w, 5, 0.1, a);
	}
	ok(w->elev == before, "elevation untouched");
	eq(w->dirty.elev, false, "no dirty flag");
	eq(w->need.derive, false, "no need flag");
	eq(rng.s, 1u, "no draws for rejected calls");
}

// ------------------------------------------------------------------ crater

DM_TEST("terrain: crater on level ground is a bowl of the given depth with a raised rim at 1.0-1.3 x ang (meteor at L = 6)") {
	auto w = flat(6, 1);
	const Geo& g = *w->geo;
	const int c = 20000;
	const double ang = 0.05, depth = 1.6;
	terrain::crater(*w, c, ang, depth);
	near(w->elev[size_t(c)], 1 - depth, 1e-4, "centre is `depth` below the old ground");
	int rimCells = 0, bowl = 0, outside = 0, badBowl = 0, lowRim = 0;
	double rimMax = 0, rimMaxT = 0;
	for (int m = 0; m < w->n; m++) {
		const double t = g.angle(c, m) / ang, d = w->elev[size_t(m)] - 1.0;
		if (t > 1.5) { if (d != 0) outside++; continue; }
		if (t <= 0.8) { bowl++; if (std::fabs(d + depth * (1 - t * t)) > 3e-3 * depth) badBowl++; }
		if (t >= 1 && t <= 1.3) { rimCells++; if (!(d > 0.05 * depth)) lowRim++; }
		if (d > rimMax) { rimMax = d; rimMaxT = t; }
	}
	eq(outside, 0, "cells beyond 1.5 x ang changed");
	eq(badBowl, 0, "bowl profile -depth (1 - t^2)");
	eq(lowRim, 0, "every rim cell stands above the old level");
	ok(bowl >= 7 && rimCells >= 6, S(bowl) + " bowl cells, " + S(rimCells) + " rim cells");
	ok(rimMax > 0.1 * depth && rimMax < 0.3 * depth, "rim height " + fmt(rimMax, 3));
	ok(rimMaxT >= 0.95 && rimMaxT <= 1.35, "rim crest at " + fmt(rimMaxT) + " x ang");
	ok(touchedFlags(*w), "flags");
}

DM_TEST("terrain: crater on real terrain drops the floor, raises the rim, moves nothing beyond 1.5 x ang") {
	auto w = generated(5, 2);
	const Geo& g = *w->geo;
	Rng rng(9);
	terrain::derive(*w);
	int done = 0;
	for (int i = 0; i < 4000 && done < 25; i++) {
		const int c = rng.integer(0, w->n - 1);
		if (w->elev[size_t(c)] < 0.1 || w->elev[size_t(c)] > 2 || w->slope[size_t(c)] > 0.6) continue;   // gentle land
		done++;
		const double ang = rng.range(0.06, 0.2), depth = rng.range(0.4, 2);
		const std::vector<float> before = w->elev;
		terrain::crater(*w, c, ang, depth);
		int inN = 0, rimN = 0, outside = 0, rimLow = 0;
		double inSum = 0, rimSum = 0;
		for (int m = 0; m < w->n; m++) {
			const double t = g.angle(c, m) / ang, d = double(w->elev[size_t(m)]) - before[size_t(m)];
			if (t > 1.5) { if (d != 0) outside++; continue; }
			if (t < 0.6) { inN++; inSum += d; }
			if (t >= 1 && t <= 1.3) { rimN++; rimSum += w->elev[size_t(m)]; if (!(d > 0)) rimLow++; }
		}
		eq(outside, 0, "outside");
		eq(rimLow, 0, "rim cells above their old level");
		ok(inSum / inN < -0.5 * depth, "floor dropped by " + fmt(-inSum / inN) + " for depth " + fmt(depth));
		ok(rimN > 0 && w->elev[size_t(c)] < rimSum / rimN, "the centre lies below the rim");
	}
	eq(done, 25, "found 25 sites");
}

DM_TEST("terrain: crater smaller than a cell still digs the centre and raises a rim; clamps") {
	auto w = flat(4, 2);
	const Geo& g = *w->geo;
	const int c = 500;
	terrain::crater(*w, c, 0, 1);
	near(w->elev[size_t(c)], 1, 1e-5, "centre down by depth");
	int up = 0;
	for (int k = 0; k < g.nbrN[size_t(c)]; k++) if (w->elev[size_t(g.nbr[size_t(6 * c + k)])] > 2) up++;
	ok(up >= 3, S(up) + " neighbours form a rim");
	terrain::crater(*w, c, 0.2, 500);
	eq(double(w->elev[size_t(c)]), state::kElevMin, "clamped");
	bool inside = true;
	for (int m = 0; m < w->n; m++) if (!(w->elev[size_t(m)] >= state::kElevMin && w->elev[size_t(m)] <= state::kElevMax)) inside = false;
	ok(inside, "inside the clamps");
}

// ------------------------------------------------------------------ cone

DM_TEST("terrain: cone on level ground is a peak of the given height with a summit pit (volcano at L = 6)") {
	auto w = flat(6, 0.5f);
	const Geo& g = *w->geo;
	const int c = 31000;
	const double ang = 0.04, height = 1.8;
	terrain::cone(*w, c, ang, height);
	double top = 0;
	int changed = 0, outside = 0, bad = 0;
	for (int m = 0; m < w->n; m++) {
		const double d = w->elev[size_t(m)] - 0.5;
		if (d != 0) changed++;
		if (g.angle(c, m) >= ang && d != 0) outside++;
		if (!(d >= 0 && d <= height * 1.0001)) bad++;
		if (d > top) top = d;
	}
	eq(outside, 0, "cells outside the cone changed");
	eq(bad, 0, "never lower than before, never above the stated height");
	ok(top > 0.9 * height, "crest reaches " + fmt(top, 3) + " of " + fmt(height));
	near(w->elev[size_t(c)] - 0.5, 0.75 * height, 1e-3, "pit floor");
	double ringMin = 1e9;
	for (int k = 0; k < g.nbrN[size_t(c)]; k++) ringMin = std::min(ringMin, double(w->elev[size_t(g.nbr[size_t(6 * c + k)])]));
	ok(ringMin > w->elev[size_t(c)] + 0.1 * height, "every neighbour of the vent stands above it (crest " + fmt(ringMin) + ", pit " + fmt(w->elev[size_t(c)]) + ")");
	ok(changed >= 13, "the flanks reach beyond the crest ring (" + S(changed) + " cells)");
	ok(touchedFlags(*w), "flags");
}

DM_TEST("terrain: a wide cone has a crater bowl, a crest ring and flanks that fall steadily to the foot") {
	auto w = flat(5, 0);
	const Geo& g = *w->geo;
	const TerrainTune& P = w->tune<TerrainTune>();
	const int c = 4000;
	const double ang = 0.4, height = 3;
	terrain::cone(*w, c, ang, height);
	const double rc = P.conePit * ang;
	near(w->elev[size_t(c)], height * (1 - P.conePitDepth), 1e-3, "pit floor");
	double top = 0, topD = 0;
	int outside = 0, badFlank = 0, badPit = 0;
	for (int m = 0; m < w->n; m++) {
		const double d = g.angle(c, m), e = w->elev[size_t(m)];
		if (e > top) { top = e; topD = d; }
		if (d >= ang) { if (e != 0) outside++; continue; }
		if (d > rc) {
			const double q = 1 - (d - rc) / (ang - rc);
			if (std::fabs(e - height * q * std::sqrt(q)) > 5e-3 * height) badFlank++;
		} else if (!(e >= height * (1 - P.conePitDepth) - 1e-3 && e <= height + 1e-6)) badPit++;
	}
	eq(outside, 0, "outside");
	eq(badFlank, 0, "flank profile q^1.5");
	eq(badPit, 0, "inside the crater");
	ok(top > 0.95 * height && std::fabs(topD - rc) < 1.2 * g.step, "crest " + fmt(top) + " at " + fmt(topD, 3) + " rad (ring at " + fmt(rc, 3) + ")");
}

DM_TEST("terrain: cone on real terrain builds on the vent, keeps its pit and never digs") {
	auto w = generated(6, 2);
	const Geo& g = *w->geo;
	Rng rng(4);
	const double ang = 0.04, height = 1.8;
	int done = 0, sea = 0;
	std::vector<int32_t> zone;
	for (int i = 0; i < 4000 && done < 40; i++) {
		const int c = rng.integer(0, w->n - 1);
		if (w->elev[size_t(c)] < -1) continue;                     // land and shallow sea, any slope
		done++;
		const std::vector<float> before = w->elev;
		const double ref = before[size_t(c)];
		terrain::cone(*w, c, ang, height);
		near(w->elev[size_t(c)], ref + 0.75 * height, 1e-3, "pit floor is 0.75 x height above the old vent");
		double peak = -1e9;
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
			const int m = g.nbr[size_t(6 * c + k)];
			ok(w->elev[size_t(m)] > w->elev[size_t(c)], "neighbour above the pit");
			peak = std::max(peak, double(w->elev[size_t(m)]));
		}
		ok(peak >= ref + 0.9 * height, "crest at least 0.9 x height above the old vent");
		int lowered = 0, outside = 0;
		for (int m = 0; m < w->n; m++) {
			if (w->elev[size_t(m)] < before[size_t(m)]) lowered++;
			if (w->elev[size_t(m)] != before[size_t(m)] && g.angle(c, m) >= ang) outside++;
		}
		eq(lowered, 0, "no cell is lowered");
		eq(outside, 0, "outside");
		if (ref < 0) { sea++; ok(w->elev[size_t(c)] > 0, "a cone in shallow water makes an island"); }
		w->elev = before;
	}
	eq(done, 40, "found 40 sites");
	ok(sea >= 3, "some of them under shallow water (" + S(sea) + ")");
}

DM_TEST("terrain: cone smaller than the mesh can show is a plain peak; clamps") {
	auto w = flat(4, 0);
	const int c = 900;                                            // ang 0.04 at L = 4 is half a cell
	terrain::cone(*w, c, 0.04, 1.8);
	int changed = 0;
	for (int m = 0; m < w->n; m++) if (w->elev[size_t(m)] != 0) changed++;
	eq(changed, 1, "one cell");
	near(w->elev[size_t(c)], 1.8, 1e-5, "raised by the full height");
	terrain::cone(*w, c, 0.5, 1000);
	bool inside = true;
	for (int m = 0; m < w->n; m++) if (!(w->elev[size_t(m)] >= 0 && w->elev[size_t(m)] <= state::kElevMax)) inside = false;
	ok(inside, "inside the clamps");
}

// ------------------------------------------------------------------ roughen

DM_TEST("terrain: roughen is a random +- displacement up to `amount`, smooth, inside the patch, flags set") {
	auto w = flat(5, 1);
	const Geo& g = *w->geo;
	const int c = 2000;
	const double ang = 0.3, amount = 0.8;
	Rng r(5);
	terrain::roughen(*w, c, ang, amount, r);
	int up = 0, down = 0, outside = 0;
	double mx = 0, sumAbs = 0, sumStep = 0;
	int steps = 0;
	for (int m = 0; m < w->n; m++) {
		const double d = w->elev[size_t(m)] - 1.0;
		if (g.angle(c, m) > ang + 1.3 * g.step) { if (d != 0) outside++; continue; }
		if (d > 0) up++; else if (d < 0) down++;
		mx = std::max(mx, std::fabs(d));
		sumAbs += std::fabs(d);
		for (int k = 0; k < g.nbrN[size_t(m)]; k++) { sumStep += std::fabs(w->elev[size_t(g.nbr[size_t(6 * m + k)])] - w->elev[size_t(m)]); steps++; }
	}
	eq(outside, 0, "cells outside the patch changed");
	ok(mx <= amount * 1.0001 && mx > 0.6 * amount, "largest displacement " + fmt(mx, 3) + " for amount " + fmt(amount));
	ok(up > 30 && down > 30, "both signs: " + S(up) + " up, " + S(down) + " down");
	// neighbours move together: the mean step between neighbours is below the mean displacement
	ok(sumStep / steps < 0.8 * sumAbs / (up + down), "smooth field (mean step " + fmt(sumStep / steps, 4) + ", mean |d| " + fmt(sumAbs / (up + down), 4) + ")");
	eq(pits(*w).size(), size_t(0), "no one-cell pits on what was a plain");
	ok(touchedFlags(*w), "flags");
}

DM_TEST("terrain: roughen creates no one-cell pits on real terrain (80 quakes)") {
	auto w = generated(5, 4);
	Rng pick(21);
	int total = 0, newPits = 0;
	for (int i = 0; i < 80; i++) {
		const int c = pick.integer(0, w->n - 1);
		const double ang = pick.range(0.03, 0.3), amount = pick.range(0.1, 1.5) * (pick.chance(0.5) ? 1 : -1);
		const std::set<int> before = pits(*w);
		const std::vector<float> old = w->elev;
		Rng r(uint32_t(1000 + i));
		terrain::roughen(*w, c, ang, amount, r);
		for (int m = 0; m < w->n; m++) if (w->elev[size_t(m)] != old[size_t(m)]) total++;
		for (const int p : pits(*w)) if (!before.count(p) && newPits++ < 5) fail("new one-cell pit at cell " + S(p) + " (quake " + S(i) + ")");
	}
	eq(newPits, 0, "new one-cell pits");
	ok(total > 2000, "the quakes moved " + S(total) + " cells");
	ok(allFinite(*w), "finite, inside the clamps");
}

DM_TEST("terrain: roughen draws only from the rng it is given (deterministic), never from w.rng") {
	auto a = generated(4, 6), b = generated(4, 6);
	const uint32_t s0 = a->rng.s;
	Rng ra(42), rb(42);
	terrain::roughen(*a, 100, 0.2, 0.5, ra);
	terrain::roughen(*b, 100, 0.2, 0.5, rb);
	ok(a->elev == b->elev, "same rng, same result");
	eq(ra.s, rb.s, "same number of draws");
	ok(ra.s != 42u, "the given rng advanced");
	eq(a->rng.s, s0, "w.rng untouched");
	const std::vector<float> before = a->elev;
	Rng r43(43), r44(44);
	terrain::roughen(*a, 100, 0.2, 0.5, r43);
	terrain::roughen(*b, 100, 0.2, 0.5, r44);
	ok(a->elev != b->elev && a->elev != before, "another stream gives another shake");
}

// ------------------------------------------------------------------ addElev

DM_TEST("terrain: addElev changes one cell, clamps, returns the new height and sets no flag") {
	auto w = flat(4, 0.25f);
	const double v = terrain::addElev(*w, 42, 0.5);
	near(v, 0.75, 1e-6, "returns the new elevation");
	near(w->elev[42], 0.75, 1e-6, "cell raised");
	int changed = 0;
	for (int m = 0; m < w->n; m++) if (w->elev[size_t(m)] != 0.25f) changed++;
	eq(changed, 1, "only that cell");
	eq(w->dirty.elev, false, "no flag (the caller calls touched)");
	eq(terrain::addElev(*w, 42, 1e9), state::kElevMax, "top clamp");
	eq(terrain::addElev(*w, 42, -1e9), state::kElevMin, "bottom clamp");
}

// ------------------------------------------------------------------ derive

DM_TEST("terrain: derive slope, coast and landmass labels match brute force (generated worlds, three sea levels)") {
	const std::pair<int, uint32_t> cases[] = { { 4, 1 }, { 4, 2 }, { 5, 3 }, { 5, 7 } };
	for (const auto& [L, seed] : cases) {
		auto w = generated(L, seed);
		for (const double sea : { 0.0, -2.5, 1.2 }) {
			w->seaLevel = sea;
			const int k = checkDerive(*w, "L" + S(L) + " seed " + std::to_string(seed) + " sea " + fmt(sea, 1));
			ok(k >= 2, "several landmasses (" + S(k) + ")");
		}
	}
}

DM_TEST("terrain: derive at L = 6: the largest landmass is id 0 and distances inland are plausible") {
	auto w = generated(6, 1);
	const int k = checkDerive(*w, "L6 seed 1");
	ok(k >= 5, S(k) + " landmasses");
	int farHops = 0;
	for (int c = 0; c < w->n; c++) farHops = std::max(farHops, int(w->coast[size_t(c)]));
	ok(farHops >= 8 && farHops < 120, "deepest interior is " + S(farHops) + " hops from the sea");
	double t0 = nowMs();
	for (int i = 0; i < 10; i++) terrain::derive(*w);
	std::printf("  terrain: derive at L = 6: %.2f ms\n", (nowMs() - t0) / 10);
}

DM_TEST("terrain: derive on worlds with no sea and with no land") {
	auto w = generated(4, 9);
	w->seaLevel = -20;                                             // dry planet
	terrain::derive(*w);
	bool good = true;
	for (int c = 0; c < w->n; c++) if (w->coast[size_t(c)] != 255 || w->cache.landId[size_t(c)] != 0) good = false;
	ok(good, "coast 255 and one landmass on a planet with no sea");
	eq(w->cache.landCount, 1, "one landmass holding every cell");
	ok(w->slope == bruteSlope(*w), "slope still computed");
	w->seaLevel = 25;                                              // drowned planet
	terrain::derive(*w);
	good = true;
	for (int c = 0; c < w->n; c++) if (w->coast[size_t(c)] != 0 || w->cache.landId[size_t(c)] != -1) good = false;
	ok(good, "coast 0 and no labels on a drowned planet");
	eq(w->cache.landCount, 0, "no landmasses");
	eq(w->need.derive, false);
}

DM_TEST("terrain: derive follows sculpting (a new island appears, ordering flips) and world size changes") {
	auto w = flat(4, -1);                                          // all sea
	terrain::derive(*w);
	eq(w->cache.landCount, 0);
	terrain::sculpt(*w, 100, 0.12, 2);
	eq(w->need.derive, true, "sculpt asks for a derive");
	checkDerive(*w, "one island");
	eq(w->cache.landCount, 1, "one island");
	eq(w->cache.landId[100], 0);
	ok(w->coast[100] > 0, "island centre is inland");
	int other = -1;
	for (int c = 0; c < w->n; c++) if (w->geo->angle(c, 100) > 1.5) { other = c; break; }
	terrain::sculpt(*w, other, 0.3, 2);
	eq(checkDerive(*w, "two islands"), 2);
	eq(w->cache.landId[size_t(other)], 0, "the larger island is landmass 0");
	eq(w->cache.landId[100], 1, "the smaller one is landmass 1");
	auto big = fresh(5, 2);                                        // a different world size in between
	terrain::generate(*big);
	big->seaLevel = 0;
	checkDerive(*big, "L5 after L4");
	checkDerive(*w, "L4 after L5");
}

// ------------------------------------------------------------------ everything together

DM_TEST("terrain: a storm of random operations leaves a finite, clamped, derivable world and emits nothing") {
	auto w = generated(4, 11);
	Rng rng(77);
	const std::vector<Event> evs = capture(*w, [&] {
		for (int i = 0; i < 400; i++) {
			const int c = rng.integer(0, w->n - 1);
			const double ang = rng.chance(0.1) ? rng.range(0, 3.5) : rng.range(0, 0.3);
			const int k = rng.integer(0, 3);
			const double big = rng.chance(0.05) ? 40 : 1;
			if (k == 0) terrain::sculpt(*w, c, ang, rng.range(-3, 3) * big);
			else if (k == 1) terrain::crater(*w, c, ang, rng.range(0.1, 3) * big);
			else if (k == 2) terrain::cone(*w, c, ang, rng.range(0.1, 3) * big);
			else terrain::roughen(*w, c, ang, rng.range(-2, 2) * big, rng);
		}
	});
	eq(evs.size(), size_t(0), "terrain emits no events");
	bool inside = true;
	for (int c = 0; c < w->n; c++) if (!(w->elev[size_t(c)] >= state::kElevMin && w->elev[size_t(c)] <= state::kElevMax)) inside = false;
	ok(inside, "inside the clamps");
	checkDerive(*w, "after the storm");
	noNaN(*w, "after the storm");
	eq(checkFinite(*w), std::string(), "checkFinite");
}

DM_TEST("terrain: a generated world survives a save round trip") {
	auto w = generated(5, 5);
	std::vector<uint8_t> bytes;
	ok(state::serialize(*w, bytes), "serialize");
	auto back = state::deserialize(bytes);
	if (!ok(back != nullptr, "deserialize")) return;
	double worst = 0;
	for (int c = 0; c < w->n; c++) worst = std::max(worst, std::fabs(double(back->elev[size_t(c)]) - w->elev[size_t(c)]));
	ok(worst < 32.0 / 65535, "elev within half a q16 step (" + fmt(worst, 6) + ")");
	terrain::derive(*back);
	noNaN(*back, "derived after load");
}

// ------------------------------------------------------------------ maps (look at them)

DM_TEST("terrain: maps of four L = 6 worlds and the derived fields") {
	namespace mi = dm::mapimg;
	for (const uint32_t seed : { 1u, 2u, 3u, 7u }) {
		auto w = generated(6, seed);
		ok(mi::elev(*w, shot("ue-terrain-elev-s" + std::to_string(seed) + ".png")), "elev map");
		ok(reliefMap(*w, shot("ue-terrain-relief-s" + std::to_string(seed) + ".png")), "relief map");
		if (seed == 1) {
			terrain::derive(*w);
			ok(mi::field(*w, "slope", shot("ue-terrain-slope.png"), 0, 1.5), "slope map");
			ok(mi::field(*w, "fert", shot("ue-terrain-fert.png"), 0, 1, { .dimSea = true }), "fert map");
			ok(mi::custom(*w, shot("ue-terrain-land.png"), [&](int c) {
				const int id = w->cache.landId[size_t(c)];
				if (id < 0) return mi::Rgb{ 20, 30, 60 };
				static const mi::Rgb pal[] = { { 230, 159, 0 }, { 86, 180, 233 }, { 0, 158, 115 }, { 240, 228, 66 }, { 213, 94, 0 }, { 204, 121, 167 } };
				if (id < 6) return pal[id];
				return mi::Rgb{ 200, 200, 200 };
			}), "landmass map");
		}
	}
	// the operations on a flat plain: a sculpt hill, a crater, a cone, a roughened patch
	auto f = flat(6, 0.4f);
	terrain::sculpt(*f, f->geo->cellAt(1, 0.2, 0.2), 0.15, 2);
	terrain::crater(*f, f->geo->cellAt(0.2, 0.2, 1), 0.08, 1.6);
	terrain::cone(*f, f->geo->cellAt(-1, 0.1, 0.1), 0.08, 2.5);
	Rng r(3);
	terrain::roughen(*f, f->geo->cellAt(0.2, 0.2, -1), 0.2, 1.0, r);
	ok(mi::elev(*f, shot("ue-terrain-ops.png")), "ops map");
	for (const uint32_t seed : { 4u, 5u, 6u, 8u }) {
		const World& w5 = *world5(seed).w;
		ok(mi::elev(w5, shot("ue-terrain-elev-L5-s" + std::to_string(seed) + ".png")), "L5 elev map");
	}
}
