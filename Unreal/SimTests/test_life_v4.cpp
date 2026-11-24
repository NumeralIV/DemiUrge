// DEMIURGE — SimTests/test_life_v4.cpp : Life's world-archetype reads (SPEC-MYTHOS 3.1; track M6
// economy-world-v4): capacityK (Generous 1.1), the archetype's soil offset in the natural fertility level, and
// marvels (Strange) that never burn and keep fert >= 0.8. Runs with every other module stubbed
// (`run.ps1 life`); marvels are Terrain's vars.num keys "terrain_marvels" / "terrain_marvel_<i>", set by hand here.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "Life.h"
#include "Mythos.h"
#include "TestFramework.h"
#include "TestWorld.h"

using namespace dm;
using namespace dmt;

namespace {

std::unique_ptr<World> lifeWorld(Archetype a, uint32_t seed = 3) {
	auto w = makeWorld({ .L = 4, .seed = seed, .land = 0.4, .basics = true, .sea = 0, .phase = Phase::History });
	w->settings.archetype = a;
	return w;
}

double meanLandFert(const World& w) {
	double s = 0;
	int k = 0;
	for (int c = 0; c < w.n; c++) if (!state::isSea(w, c)) { s += w.fert[size_t(c)]; k++; }
	return k ? s / k : 0;
}

void years(World& w, int calls) {
	for (int i = 0; i < calls; i++) for (int p = 0; p < 8; p++) life::step(w, p, 8);   // 2 years per round
}

// a dry forest cell with every neighbour on dry forested land too
int dryForest(World& w) {
	const Geo& g = *w.geo;
	for (int c = 0; c < w.n; c++) {
		if (state::isSea(w, c)) continue;
		bool inland = true;
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
			const int m = g.nbr[size_t(6 * c + k)];
			if (state::isSea(w, m)) { inland = false; break; }
			for (int j = 0; j < g.nbrN[size_t(m)]; j++) if (state::isSea(w, g.nbr[size_t(6 * m + j)])) { inland = false; break; }
		}
		if (!inland) continue;
		std::vector<int32_t> ring;
		g.ring(c, 2, ring);
		for (const int32_t m : ring) {
			w.veg[size_t(m)] = 0.9f; w.moist[size_t(m)] = 0.05f; w.wet[size_t(m)] = 0; w.ice[size_t(m)] = 0;
			w.water[size_t(m)] = 0; w.fire[size_t(m)] = 0;
		}
		return c;
	}
	return -1;
}

void setMarvels(World& w, const std::vector<int>& cells) {
	w.vars.num["terrain_marvels"] = double(cells.size());
	for (size_t i = 0; i < cells.size(); i++) {
		w.vars.num["terrain_marvel_" + std::to_string(i)] = cells[i];
		w.vars.num["terrain_marvel_kind_" + std::to_string(i)] = 3;
	}
}

} // namespace

DM_TEST("life v4: Balanced and a neutral archetype row leave capacity and a 100-year step bit-identical") {
	auto a = lifeWorld(Archetype::Balanced);
	auto b = lifeWorld(Archetype::Generous);
	b->tune<MythosTune>().archetypes[size_t(Archetype::Generous)] = ArchetypeParams{};
	for (int c = 0; c < a->n; c++) {
		const double ka = life::capacity(*a, c), kb = life::capacity(*b, c);
		if (!ok(std::memcmp(&ka, &kb, sizeof ka) == 0, "capacity of cell " + std::to_string(c))) return;
	}
	years(*a, 50);
	years(*b, 50);
	b->settings.archetype = Archetype::Balanced;                        // the setting itself is saved: compare the rest
	eq(hash(*a), hash(*b), "100 years of step");
}

DM_TEST("life v4: Generous multiplies every cell's vegetation capacity by capacityK (1.1), still at most 1") {
	auto a = lifeWorld(Archetype::Balanced);
	auto b = lifeWorld(Archetype::Generous);
	const double k = b->tune<MythosTune>().archetypes[size_t(Archetype::Generous)].capacityK;
	near(k, 1.1, 1e-12, "the spec's starting value");
	int more = 0, bad = 0;
	for (int c = 0; c < a->n; c++) {
		const double ka = life::capacity(*a, c), kb = life::capacity(*b, c);
		if (std::fabs(kb - std::min(1.0, ka * k)) > 1e-9) bad++;
		if (kb > ka) more++;
		if (kb > 1) bad++;
	}
	eq(bad, 0, "capacity x 1.1, capped at 1");
	ok(more > 0, "some cells can hold more");
	// fill follows the capacity: more vegetation on a Generous world
	life::fill(*a, 1);
	life::fill(*b, 1);
	ok(life::cover(*b).forest >= life::cover(*a).forest, "at least as much forest");
	noNaN(*b, "finite");
}

DM_TEST("life v4: the archetype's soil offset lasts: Generous soils settle about +0.15 richer, Dangerous poorer") {
	auto a = lifeWorld(Archetype::Balanced);
	auto g = lifeWorld(Archetype::Generous);
	auto d = lifeWorld(Archetype::Dangerous);
	years(*a, 150);                                                       // 300 years: 70 % of the gap closed
	years(*g, 150);
	years(*d, 150);
	const double fa = meanLandFert(*a), fg = meanLandFert(*g), fd = meanLandFert(*d);
	ok(fg >= fa + 0.07, "Generous " + std::to_string(fg) + " vs Balanced " + std::to_string(fa));
	ok(fd <= fa - 0.02, "Dangerous " + std::to_string(fd) + " vs Balanced " + std::to_string(fa));
	noNaN(*g, "finite");
	noNaN(*d, "finite");
}

DM_TEST("life v4: a marvel never catches fire (ignite refuses it, fires do not spread onto it)") {
	auto w = lifeWorld(Archetype::Strange);
	const int c = dryForest(*w);
	if (!ok(c >= 0, "a dry forest cell")) return;
	const Geo& g = *w->geo;
	// control: without the marvel key the cell burns
	ok(life::ignite(*w, c, 1), "an ordinary dry forest cell catches fire");
	w->fire[size_t(c)] = 0;
	life::rebuild(*w);
	setMarvels(*w, { c });
	ok(!life::ignite(*w, c, 1), "the marvel refuses fire");
	eq(double(w->fire[size_t(c)]), 0.0, "and is not burning");
	// a ring of fire around it: certain spread to every ordinary neighbour, never onto the marvel
	w->tune<LifeTune>().fireP = 1e9;
	for (int round = 0; round < 6; round++) {
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
			const int m = g.nbr[size_t(6 * c + k)];
			if (w->fire[size_t(m)] == 0) { w->veg[size_t(m)] = 0.9f; life::ignite(*w, m, 1); }
		}
		life::burn(*w);
		if (!ok(!(w->fire[size_t(c)] > 0), "round " + std::to_string(round) + ": the marvel stays unburnt")) return;
	}
	eq(double(w->veg[size_t(c)]), double(0.9f), "its forest stands");
	// an ordinary cell next to the fire did catch (the rule works around it)
	int burnt = 0;
	std::vector<int32_t> ring;
	g.ring(c, 2, ring);
	for (const int32_t m : ring) if (m != c && w->scorch[size_t(m)] > 0) burnt++;
	ok(burnt > 0, "the fire burnt the ground around it");
	noNaN(*w, "finite");
}

DM_TEST("life v4: a marvel keeps fert >= 0.8 through step; bad marvel keys are ignored") {
	auto w = lifeWorld(Archetype::Strange);
	const int c = findLand(*w);
	if (!ok(c >= 0, "land")) return;
	setMarvels(*w, { c });
	w->fert[size_t(c)] = 0.2f;
	life::step(*w, (c + 1) % 8, 8);
	eq(double(w->fert[size_t(c)]), double(0.2f), "only its own slice touches it");
	life::step(*w, c % 8, 8);
	ok(w->fert[size_t(c)] >= 0.8f, "lifted to 0.8 by its own slice");
	years(*w, 100);
	ok(w->fert[size_t(c)] >= 0.8f, "and held there for 200 years");
	// garbage keys: out of range, NaN, counts beyond the list
	w->vars.num["terrain_marvels"] = 99;
	w->vars.num["terrain_marvel_1"] = -5;
	w->vars.num["terrain_marvel_2"] = std::nan("");
	w->vars.num["terrain_marvel_3"] = 1e9;
	years(*w, 2);
	const int d = dryForest(*w);
	if (d >= 0 && d != c) ok(life::ignite(*w, d, 1), "ordinary cells still burn");
	w->vars.num.erase("terrain_marvel_2");                                // (the NaN key was ours)
	noNaN(*w, "finite");
}
