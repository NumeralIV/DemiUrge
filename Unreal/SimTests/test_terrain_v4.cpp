// DEMIURGE — SimTests/test_terrain_v4.cpp : world archetypes in Terrain::generate (SPEC-MYTHOS 3.1; track M6
// economy-world-v4). Runs with every other module stubbed (`run.ps1 terrain`; Mythos's archetypeParams is an
// honest read of MythosTune::archetypes[settings.archetype] in the stub and the real module alike).
//   TERRAIN_REPORT=1 run.ps1 terrain v4   also prints the per-archetype table.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "MapImg.h"
#include "Mythos.h"
#include "Terrain.h"
#include "TestFramework.h"
#include "TestWorld.h"

using namespace dm;
using namespace dmt;

namespace {

bool report() { const char* r = std::getenv("TERRAIN_REPORT"); return r && *r && *r != '0'; }

std::unique_ptr<World> gen(int L, uint32_t seed, Archetype a) {
	auto w = state::create({ .seed = seed, .L = L, .geo = sharedGeo(L) });
	watch(*w);
	w->settings.archetype = a;
	terrain::generate(*w);
	w->seaLevel = 0;
	return w;
}

struct ArchStats {
	double land = 0, high = 0, coast = 0, fert = 0;
	int masses = 0;
};

ArchStats statsOf(const World& w) {
	const Geo& g = *w.geo;
	ArchStats s;
	int land = 0, high = 0, coast = 0;
	double fert = 0;
	for (int c = 0; c < w.n; c++) {
		if (w.elev[size_t(c)] < 0) continue;
		land++;
		fert += w.fert[size_t(c)];
		if (w.elev[size_t(c)] >= 2.0f) high++;
		for (int k = 0; k < g.nbrN[size_t(c)]; k++)
			if (w.elev[size_t(g.nbr[size_t(6 * c + k)])] < 0) { coast++; break; }
	}
	// landmasses (any size)
	std::vector<uint8_t> seen(size_t(w.n), 0);
	std::vector<int32_t> q;
	for (int c0 = 0; c0 < w.n; c0++) {
		if (seen[size_t(c0)] || w.elev[size_t(c0)] < 0) continue;
		s.masses++;
		q.assign(1, c0);
		seen[size_t(c0)] = 1;
		for (size_t h = 0; h < q.size(); h++) {
			const int c = q[h];
			for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
				const int m = g.nbr[size_t(6 * c + k)];
				if (!seen[size_t(m)] && w.elev[size_t(m)] >= 0) { seen[size_t(m)] = 1; q.push_back(m); }
			}
		}
	}
	s.land = double(land) / w.n;
	s.high = land > 0 ? double(high) / land : 0;
	s.coast = land > 0 ? double(coast) / land : 0;
	s.fert = land > 0 ? fert / land : 0;
	return s;
}

std::vector<int> marvelCells(const World& w) {
	std::vector<int> out;
	const auto it = w.vars.num.find("terrain_marvels");
	if (it == w.vars.num.end()) return out;
	for (int i = 0; i < int(it->second); i++) {
		const auto jt = w.vars.num.find("terrain_marvel_" + std::to_string(i));
		out.push_back(jt == w.vars.num.end() ? -1 : int(jt->second));
	}
	return out;
}

int hopsBetween(const World& w, int a, int b, int cap) {
	std::vector<int32_t> ring;
	for (int h = 0; h <= cap; h++) {
		w.geo->ring(a, h, ring);
		if (std::find(ring.begin(), ring.end(), b) != ring.end()) return h;
	}
	return cap + 1;
}

const char* kArchName[5] = { "balanced", "generous", "dangerous", "beautiful", "strange" };

} // namespace

DM_TEST("terrain v4: a Balanced world is bit-identical to v3.1 and the neutral archetype code path changes nothing") {
	// (the v3.1 world itself is pinned by "generate is bit-identical to the JS reference"; here: the archetype
	// code path with neutral parameters, on a non-Balanced setting, gives the same bytes as Balanced)
	for (const uint32_t seed : { 1u, 5u }) {
		auto a = gen(5, seed, Archetype::Balanced);
		auto b = state::create({ .seed = seed, .L = 5, .geo = sharedGeo(5) });
		watch(*b);
		b->settings.archetype = Archetype::Strange;
		b->tune<MythosTune>().archetypes[size_t(Archetype::Strange)] = ArchetypeParams{};
		terrain::generate(*b);
		b->seaLevel = 0;
		b->settings.archetype = Archetype::Balanced;                       // the setting itself is saved: compare the planet
		if (!eq(hash(*a), hash(*b), "seed " + std::to_string(seed))) return;
		ok(a->vars.num.count("terrain_marvels") == 0, "a Balanced world has no marvels");
	}
}

DM_TEST("terrain v4: every archetype keeps the land fraction (36-42 %, Beautiful 34-42 %) and is deterministic") {
	for (int a = 0; a < 5; a++) {
		for (const uint32_t seed : { 1u, 2u, 3u, 4u }) {
			auto w = gen(5, seed, Archetype(a));
			const ArchStats s = statsOf(*w);
			const double lo = Archetype(a) == Archetype::Beautiful ? 0.34 : 0.36;
			if (!ok(s.land >= lo && s.land <= 0.42, std::string(kArchName[a]) + " seed " + std::to_string(seed) + " land " + std::to_string(s.land))) return;
			if (!ok(checkFinite(*w).empty(), "finite")) return;
			if (seed == 1) {
				auto again = gen(5, seed, Archetype(a));
				if (!eq(hash(*w), hash(*again), std::string(kArchName[a]) + " deterministic")) return;
				ok(w->rng.s == again->rng.s && w->rng.s == state::create({ .seed = seed, .L = 5, .geo = sharedGeo(5) })->rng.s,
					"generate never advances w.rng");
			}
		}
	}
}

DM_TEST("terrain v4: Dangerous has more mountains, Beautiful more coast and islands, Generous richer soil (6 seeds, L = 5)") {
	ArchStats sum[5];
	const int seeds = 6;
	for (int a = 0; a < 5; a++)
		for (uint32_t seed = 1; seed <= uint32_t(seeds); seed++) {
			auto w = gen(5, seed, Archetype(a));
			const ArchStats s = statsOf(*w);
			sum[a].land += s.land / seeds; sum[a].high += s.high / seeds; sum[a].coast += s.coast / seeds;
			sum[a].fert += s.fert / seeds; sum[a].masses += s.masses;
		}
	if (report())
		for (int a = 0; a < 5; a++)
			std::printf("  %-10s land %.3f  high(>=2) %.4f  coast %.4f  fert %.3f  landmasses %.1f\n", kArchName[a], sum[a].land,
				sum[a].high, sum[a].coast, sum[a].fert, sum[a].masses / double(seeds));
	const ArchStats& B = sum[int(Archetype::Balanced)];
	ok(sum[int(Archetype::Dangerous)].high >= 1.15 * B.high, "Dangerous: mountains >= 1.15 x Balanced");
	ok(sum[int(Archetype::Beautiful)].coast >= 1.08 * B.coast, "Beautiful: coast cells >= 1.08 x Balanced");
	ok(sum[int(Archetype::Beautiful)].masses > B.masses, "Beautiful: more landmasses (islands) than Balanced");
	ok(sum[int(Archetype::Generous)].fert >= B.fert + 0.08, "Generous: mean land fert >= Balanced + 0.08");
	ok(sum[int(Archetype::Dangerous)].fert <= B.fert - 0.02, "Dangerous: mean land fert <= Balanced - 0.02");
	near(sum[int(Archetype::Strange)].high, B.high, 0.01, "Strange: the relief of a Balanced world");
}

DM_TEST("terrain v4: Strange places 3-6 marvels on land, never within 7 hops of each other, with fert >= 0.8") {
	int total = 0, kinds[4] = { 0, 0, 0, 0 };
	for (const int L : { 5, 6 }) {
		for (const uint32_t seed : { 1u, 2u, 3u, 4u, 5u, 6u }) {
			if (L == 6 && seed > 2) break;
			auto w = gen(L, seed, Archetype::Strange);
			const std::vector<int> mv = marvelCells(*w);
			const std::string tag = "L " + std::to_string(L) + " seed " + std::to_string(seed);
			if (!ok(mv.size() >= 3 && mv.size() <= 6, tag + ": 3-6 marvels, got " + std::to_string(mv.size()))) return;
			for (size_t i = 0; i < mv.size(); i++) {
				const int c = mv[i];
				if (!ok(w->valid(c), tag + ": a valid cell")) return;
				ok(w->elev[size_t(c)] >= 0.5f, tag + ": on land well above the shore");
				ok(w->fert[size_t(c)] >= 0.8f, tag + ": fert >= 0.8");
				const auto kt = w->vars.num.find("terrain_marvel_kind_" + std::to_string(i));
				if (ok(kt != w->vars.num.end() && kt->second >= 0 && kt->second <= 3, tag + ": a kind 0..3")) kinds[int(kt->second)]++;
				for (size_t j = i + 1; j < mv.size(); j++)
					ok(hopsBetween(*w, c, mv[j], 7) > 7, tag + ": never within 7 hops of another marvel");
			}
			total += int(mv.size());
			// regenerating as another archetype removes them
			if (L == 5 && seed == 1) {
				w->settings.archetype = Archetype::Generous;
				terrain::generate(*w);
				ok(marvelCells(*w).empty() && w->vars.num.count("terrain_marvel_0") == 0, "a regenerated world drops its marvels");
			}
			if (L == 6 && seed == 1) mapimg::elev(*w, shot("ue-mythos-m6-strange-marvels.png"), { .marks = std::vector<int>(mv.begin(), mv.end()) });
		}
	}
	if (report()) std::printf("  marvels %d: stones %d, springs %d, glowing lakes %d, whispering woods %d\n", total, kinds[0], kinds[1], kinds[2], kinds[3]);
	ok(kinds[0] + kinds[1] + kinds[2] + kinds[3] == total, "every marvel has a kind");
	ok((kinds[0] > 0) + (kinds[1] > 0) + (kinds[2] > 0) + (kinds[3] > 0) >= 3, "at least three kinds of marvel occur");
}

DM_TEST("terrain v4: archetype maps (L = 6, seed 3)") {
	for (int a = 0; a < 5; a++) {
		auto w = gen(6, 3, Archetype(a));
		ok(mapimg::elev(*w, shot(std::string("ue-mythos-m6-terrain-") + kArchName[a] + ".png")), "map written");
	}
}
