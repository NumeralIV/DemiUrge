// DEMIURGE — SimTests/test_life_adv.cpp : independent adversarial verification of dm::life (A5).
// Written by the A5 verifier (not the builder); runs with `run.ps1 life` next to test_life.cpp.
// What it attacks that test_life.cpp does not: ownership (each API writes only the cells and fields the writers
// table allows, diffed over every cell field and the world scalars), NaN / infinity in every input field and in
// every LifeTune number, non-finite fauna / fire left behind, read-only functions never touching state or the rng,
// the fires cache under random operation sequences (incl. sea-level changes), fire:start payloads and rate limit,
// the poles at L = 4 and 6, exact decay rates against the spec, determinism at L = 6 (with and without cover()
// calls in between), and performance at L = 6 on the history and life-phase schedules, during fires and with an
// absurd lifeBoost.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
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

void tellA(const std::string& s) { std::printf("  %s\n", s.c_str()); std::fflush(stdout); }
std::string fmtA(double x, int d = 3) { char b[64]; std::snprintf(b, sizeof b, "%.*f", d, x); return b; }
bool landA(const World& w, int c) { return w.elev[size_t(c)] >= w.seaLevel; }

// ------------------------------------------------------------------ a busy world: every kind of ground and state
// Helper planet with Life's own biomes and vegetation, fires burning, owned territory with farms and a town,
// ruins and fields on unowned land, fallout, wet and scorched ground, lakes, and land garbage on sea cells.
struct Busy { std::unique_ptr<World> w; std::vector<int> owned, unowned, seaJunk, lakes; };
Busy busyWorld(int L, uint32_t seed) {
	Busy b;
	b.w = makeWorld({ .L = L, .seed = seed, .land = 0.4 });
	World& w = *b.w;
	life::classify(w);
	life::fill(w, 0.8);
	life::habitability(w);
	Culture& cu = state::newCulture(w);
	const int cid = cu.id;
	const int c0 = bestHab(w);
	Settlement& s = state::newSettlement(w, c0, cid);
	const int sid = s.id;
	std::vector<int32_t> ring;
	w.geo->ring(c0, 3, ring);
	for (int c : ring) if (landA(w, c)) { w.owner[size_t(c)] = int16_t(sid); w.farm[size_t(c)] = 0.6f; w.urban[size_t(c)] = 0.3f; b.owned.push_back(c); }
	int k = 0;
	for (int c = 0; c < w.n; c++) {
		const size_t i = size_t(c);
		if (landA(w, c)) {
			if (w.owner[i] >= 0) continue;
			if (k % 11 == 0) { w.farm[i] = 0.5f; w.urban[i] = 0.4f; b.unowned.push_back(c); }
			if (k % 13 == 0) w.rad[i] = 0.7f;
			if (k % 17 == 0) w.wet[i] = 0.6f;
			if (k % 19 == 0) w.scorch[i] = 0.5f;
			if (k % 41 == 0 && b.lakes.size() < 20) { w.water[i] = 0.4f; b.lakes.push_back(c); }
			k++;
		} else if (c % 23 == 0) {
			w.veg[i] = 0.5f; w.farm[i] = 0.3f; w.urban[i] = 0.2f; w.road[i] = 2; w.water[i] = 0.1f; w.scorch[i] = 0.4f; w.fire[i] = 0.4f;
			b.seaJunk.push_back(c);
		}
	}
	life::rebuild(w);
	// a few fires on dry ground
	int lit = 0;
	for (int c = 0; c < w.n && lit < 12; c += 37) {
		if (!landA(w, c) || w.veg[size_t(c)] < 0.3f) continue;
		w.moist[size_t(c)] = 0.1f;
		if (life::ignite(w, c)) lit++;
	}
	w.dirty = Dirty{};
	w.dirty.elev = w.dirty.color = w.dirty.water = w.dirty.objects = w.dirty.rivers = w.dirty.roads = false;
	return b;
}

// ------------------------------------------------------------------ snapshots and diffs
struct Snap {
	std::vector<std::vector<uint8_t>> cells;          // raw bytes of every cell field
	std::vector<double> scalars;
	std::vector<int32_t> landId, landHab, fires;
	int landCount = 0;
	uint32_t rng = 0;
	size_t settlements = 0, cultures = 0, units = 0, disasters = 0, chronicle = 0, rivers = 0;
	std::vector<double> ents;
};
size_t elemSize(FieldType t) { return t == FieldType::F32 || t == FieldType::I32 ? 4 : t == FieldType::I16 ? 2 : 1; }
Snap snap(const World& w) {
	Snap s;
	for (int f = 0; f < state::kCellFieldCount; f++) {
		const CellArray a = state::cellArray(w, f);
		const uint8_t* p = static_cast<const uint8_t*>(a.data);
		s.cells.emplace_back(p, p + a.size * elemSize(a.type));
	}
	s.scalars = { w.year, double(w.tick), double(int(w.phase)), w.heat, w.seaLevel, w.globalTemp, w.co2, w.dust, double(w.age),
		w.god.awe, w.god.aweMax, w.god.mercy, w.god.wrath, w.vars.lifeBoost, w.vars.seaBase.value_or(-777), w.vars.iceRef.value_or(-777),
		double(w.vars.t0), double(w.vars.num.size()), double(w.vars.rel.size()), w.stats.pop, double(w.stats.miracles),
		double(w.nextId.unit), double(w.nextId.disaster), double(w.nextId.entry) };
	s.landId = w.cache.landId; s.landHab = w.cache.landHab; s.fires = w.cache.fires; s.landCount = w.cache.landCount;
	s.rng = w.rng.s;
	s.settlements = w.settlements.size(); s.cultures = w.cultures.size(); s.units = w.units.size(); s.disasters = w.disasters.size();
	s.chronicle = w.chronicle.size(); s.rivers = w.rivers.size();
	for (const Settlement& st : w.settlements) { s.ents.push_back(st.pop); s.ents.push_back(st.devotion); s.ents.push_back(st.K); }
	for (const Culture& cu : w.cultures) { s.ents.push_back(cu.pop); s.ents.push_back(cu.science); }
	return s;
}
// cells whose field `f` changed
std::vector<int> changed(const World& w, const Snap& a, const Snap& b, int f) {
	std::vector<int> out;
	const CellArray arr = state::cellArray(w, f);
	const size_t es = elemSize(arr.type);
	for (int c = 0; c < w.n; c++)
		if (std::memcmp(&a.cells[size_t(f)][size_t(c) * es], &b.cells[size_t(f)][size_t(c) * es], es) != 0) out.push_back(c);
	return out;
}
// Allowed writes of one call: field name -> predicate on the cell (nullptr: any cell).
using Allow = std::map<std::string, std::function<bool(int)>>;
struct Extra { bool rng = false, fires = false, landHab = false; };
void checkWrites(const World& w, const Snap& a, const Snap& b, const Allow& allow, Extra ex, const std::string& what) {
	for (int f = 0; f < state::kCellFieldCount; f++) {
		const std::string name = state::kCellFields[f].name;
		const std::vector<int> ch = changed(w, a, b, f);
		if (ch.empty()) continue;
		auto it = allow.find(name);
		if (it == allow.end()) { fail(what + " wrote " + name + " (" + std::to_string(ch.size()) + " cells, e.g. " + std::to_string(ch[0]) + ")"); continue; }
		if (!it->second) continue;
		int bad = 0;
		for (int c : ch) if (!it->second(c) && bad++ < 2) fail(what + " wrote " + name + " on cell " + std::to_string(c) + " where it may not");
	}
	ok(a.scalars == b.scalars, what + ": world scalars / vars / stats untouched");
	ok(a.ents == b.ents && a.settlements == b.settlements && a.cultures == b.cultures && a.units == b.units && a.disasters == b.disasters &&
		a.chronicle == b.chronicle && a.rivers == b.rivers, what + ": entities untouched");
	ok(a.landId == b.landId && a.landCount == b.landCount, what + ": landmass cache untouched");
	if (!ex.landHab) ok(a.landHab == b.landHab, what + ": landHab untouched");
	if (!ex.fires) ok(a.fires == b.fires, what + ": fires cache untouched");
	if (!ex.rng) ok(a.rng == b.rng, what + ": rng not drawn");
}

// the Life-owned fields must be finite; biome in range
std::string lifeFinite(const World& w) {
	for (int c = 0; c < w.n; c++) {
		const size_t i = size_t(c);
		const float v[] = { w.veg[i], w.fauna[i], w.fire[i], w.scorch[i], w.hab[i], w.wet[i], w.fert[i], w.rad[i] };
		const char* nm[] = { "veg", "fauna", "fire", "scorch", "hab", "wet", "fert", "rad" };
		for (int k = 0; k < 8; k++) if (!std::isfinite(v[k])) return std::string(nm[k]) + "[" + std::to_string(c) + "]";
		if (w.owner[i] < 0 && (!std::isfinite(w.farm[i]) || !std::isfinite(w.urban[i]))) return "farm/urban[" + std::to_string(c) + "] (unowned)";
		if (w.biome[i] >= Biome::COUNT) return "biome[" + std::to_string(c) + "] out of range";
	}
	return "";
}
std::string firesConsistent(const World& w) {
	std::set<int> seen;
	for (int c : w.cache.fires) {
		if (c < 0 || c >= w.n) return "listed cell out of range";
		if (!(w.fire[size_t(c)] > 0)) return "listed cell " + std::to_string(c) + " is not burning";
		if (!seen.insert(c).second) return "cell " + std::to_string(c) + " listed twice";
	}
	int k = 0;
	for (int c = 0; c < w.n; c++) if (w.fire[size_t(c)] > 0) k++;
	if (k != int(seen.size())) return std::to_string(k) + " cells burn but " + std::to_string(seen.size()) + " are listed";
	return "";
}
void cycle(World& w, int ticks) {
	life::classify(w); life::habitability(w);
	for (int t = 0; t < ticks; t++) {
		w.tick++; life::burn(w); life::step(w, int(w.tick % 8), 8);
		if (t == ticks / 2) { life::classify(w); life::habitability(w); }
	}
	for (int t = 0; t < 4; t++) { w.tick++; life::burn(w); life::step(w, int(w.tick % 2), 2); }
	(void)life::cover(w);
}

} // namespace

// ================================================================== ownership
DM_TEST("adv ownership: every Life call writes only the fields and cells the writers table gives it") {
	for (int L : { 4, 6 }) {
		const std::string at = " (L " + std::to_string(L) + ")";
		auto owned = [](const World& w, int c) { return w.owner[size_t(c)] >= 0; };
		auto isSeaC = [](const World& w, int c) { return w.elev[size_t(c)] < w.seaLevel; };
		{
			Busy b = busyWorld(L, 3); World& w = *b.w;
			const Snap a = snap(w); life::classify(w); const Snap z = snap(w);
			checkWrites(w, a, z, { { "biome", nullptr } }, {}, "classify" + at);
			ok(w.dirty.color && w.dirty.objects, "classify sets dirty.color and dirty.objects");
		}
		{
			Busy b = busyWorld(L, 3); World& w = *b.w;
			const Snap a = snap(w);
			double s = 0; for (int c = -2; c < w.n + 2; c++) s += life::capacity(w, c);
			const life::Cover cv = life::cover(w);
			const Snap z = snap(w);
			checkWrites(w, a, z, {}, {}, "capacity / cover" + at);
			ok(std::isfinite(s) && std::isfinite(cv.land + cv.veg + cv.forest), "read-only results finite");
			ok(w.dirty.elev == false && w.dirty.color == false && w.dirty.objects == false, "read-only calls set no dirty flag");
		}
		{
			Busy b = busyWorld(L, 3); World& w = *b.w;
			const Snap a = snap(w);
			for (int p = 0; p < 8; p++) { w.tick++; life::step(w, p, 8); }
			w.tick -= 8;
			const Snap z = snap(w);
			const World& cw = w;
			// owned land keeps Civ's farm / urban; road, water and fire only on sea cells (zeroed)
			auto unownedOrSea = [&cw, owned, isSeaC](int c) { return !owned(cw, c) || isSeaC(cw, c); };
			auto seaOnly = [&cw, isSeaC](int c) { return isSeaC(cw, c); };
			checkWrites(w, a, z, { { "veg", nullptr }, { "fauna", nullptr }, { "fert", nullptr }, { "wet", nullptr }, { "scorch", nullptr },
				{ "rad", nullptr }, { "farm", unownedOrSea }, { "urban", unownedOrSea }, { "road", seaOnly }, { "water", seaOnly }, { "fire", seaOnly } },
				{ .rng = true, .fires = true }, "step x 8 slices" + at);
			int junkLeft = 0;
			for (int c : b.seaJunk) { const size_t i = size_t(c); if (w.veg[i] || w.farm[i] || w.urban[i] || w.road[i] || w.water[i] || w.fire[i] || w.scorch[i]) junkLeft++; }
			eq(junkLeft, 0, "every sea cell zeroed after one full cycle of slices" + at);
			int civKept = 0;
			for (int c : b.owned) if (w.farm[size_t(c)] == 0.6f && w.urban[size_t(c)] == 0.3f) civKept++;
			eq(civKept, int(b.owned.size()), "Civ's farm / urban on owned land untouched" + at);
			eq(firesConsistent(w), std::string(), "fires cache after step" + at);
		}
		{
			Busy b = busyWorld(L, 3); World& w = *b.w;
			w.tick++; const Snap a = snap(w); life::burn(w); const Snap z = snap(w);
			checkWrites(w, a, z, { { "fire", nullptr }, { "veg", nullptr }, { "scorch", nullptr }, { "fauna", nullptr }, { "fert", nullptr } },
				{ .rng = true, .fires = true }, "burn" + at);
		}
		{
			Busy b = busyWorld(L, 3); World& w = *b.w;
			const int c = findLand(w, [&](int m) { return w.veg[size_t(m)] > 0.3f && w.fire[size_t(m)] == 0 && w.wet[size_t(m)] < 0.3f && w.water[size_t(m)] < 0.03f; });
			const Snap a = snap(w); const bool lit = life::ignite(w, c); const Snap z = snap(w);
			ok(lit, "ignite on a forest cell" + at);
			checkWrites(w, a, z, { { "fire", [c](int m) { return m == c; } } }, { .fires = true }, "ignite" + at);
		}
		{
			Busy b = busyWorld(L, 3); World& w = *b.w;
			std::fill(w.veg.begin(), w.veg.end(), 0.0f);
			const int c = bestHab(w);
			const Snap a = snap(w); life::seed(w, c, 0.15); const Snap z = snap(w);
			checkWrites(w, a, z, { { "veg", nullptr } }, {}, "seed" + at);
			const Snap a2 = snap(w); life::bloom(w, c, 0.1, 0.3); const Snap z2 = snap(w);
			checkWrites(w, a2, z2, { { "veg", nullptr }, { "fert", nullptr }, { "scorch", nullptr } }, {}, "bloom" + at);
			const Snap a3 = snap(w); life::fill(w, 0.5); const Snap z3 = snap(w);
			checkWrites(w, a3, z3, { { "veg", nullptr }, { "fauna", nullptr } }, {}, "fill" + at);
			const Snap a4 = snap(w); life::habitability(w); const Snap z4 = snap(w);
			checkWrites(w, a4, z4, { { "hab", nullptr } }, { .landHab = true }, "habitability" + at);
			const Snap a5 = snap(w); life::rebuild(w); const Snap z5 = snap(w);
			checkWrites(w, a5, z5, {}, { .fires = true }, "rebuild" + at);
		}
	}
}

// ================================================================== NaN and garbage
DM_TEST("adv robust: non-finite fauna on a sea or lake cell with no wildlife around is cleaned, not kept") {
	auto pw = makeWorld({ .L = 4, .seed = 2 });
	World& w = *pw;
	life::classify(w);
	std::fill(w.fauna.begin(), w.fauna.end(), 0.0f);
	const int sea = findLand(w, nullptr) >= 0 ? [&] { for (int c = 0; c < w.n; c++) if (!landA(w, c) && w.seaLevel - w.elev[size_t(c)] < 0.5) return c; return -1; }() : -1;
	const int lake = findLand(w, [&](int c) { return w.temp[size_t(c)] > 5; });
	if (!ok(sea >= 0 && lake >= 0, "a shallow sea cell and a land cell")) return;
	w.water[size_t(lake)] = 0.5f;
	const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
	w.fauna[size_t(sea)] = nan; w.fauna[size_t(lake)] = inf;
	for (int p = 0; p < 8; p++) life::step(w, p, 8);
	ok(std::isfinite(w.fauna[size_t(sea)]), "sea fauna finite (" + std::to_string(w.fauna[size_t(sea)]) + ")");
	ok(std::isfinite(w.fauna[size_t(lake)]), "lake fauna finite (" + std::to_string(w.fauna[size_t(lake)]) + ")");
	// a NaN / infinite fire value: nobody lists it, so step / rebuild must clean it
	const int f = findLand(w, [&](int c) { return c != lake; });
	w.fire[size_t(f)] = nan;
	life::rebuild(w);
	for (int p = 0; p < 8; p++) { w.tick++; life::burn(w); life::step(w, p, 8); }
	ok(std::isfinite(w.fire[size_t(f)]), "non-finite fire cleaned");
	eq(lifeFinite(w), std::string(), "Life fields finite");
	eq(firesConsistent(w), std::string(), "fires cache");
}

DM_TEST("adv robust: NaN / infinity in any one input field never reaches a Life-owned field (L = 4)") {
	const char* fields[] = { "elev", "temp", "moist", "slope", "ice", "water", "lava", "fert", "wet", "scorch", "rad", "farm", "urban", "veg", "fauna", "fire", "flow" };
	const float bads[] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };
	int runs = 0, failed = 0;
	for (const char* fname : fields) {
		for (float bad : bads) {
			Busy b = busyWorld(4, 5);
			World& w = *b.w;
			const int fi = state::fieldIndex(fname);
			const CellArray arr = state::cellArray(w, fi);
			for (int c = 0; c < w.n; c += 3) arr.set(size_t(c), double(bad));     // a third of all cells, sea and land
			life::rebuild(w);
			cycle(w, 24);
			life::seed(w, 7, 0.3); life::bloom(w, 9, 0.3, 0.4); life::ignite(w, 30); life::fill(w, 0.6); cycle(w, 16);
			runs++;
			const std::string r = lifeFinite(w);
			if (!r.empty() && failed++ < 6) fail(std::string(fname) + " = " + std::to_string(bad) + ": non-finite " + r);
			const std::string fc = firesConsistent(w);
			if (!fc.empty() && failed++ < 6) fail(std::string(fname) + " = " + std::to_string(bad) + ": fires " + fc);
			const life::Cover cv = life::cover(w);
			if (!(std::isfinite(cv.land) && std::isfinite(cv.veg) && std::isfinite(cv.forest)) && failed++ < 6) fail(std::string(fname) + ": cover not finite");
		}
	}
	tellA(std::to_string(runs) + " poisoned-field runs (17 fields x NaN, +inf, -inf on every third cell), " + std::to_string(failed) + " failures");
	// non-finite world scalars
	for (int k = 0; k < 4; k++) {
		Busy b = busyWorld(4, 6);
		World& w = *b.w;
		const double nan = std::nan("");
		if (k == 0) w.seaLevel = nan;
		if (k == 1) w.vars.seaBase = nan;
		if (k == 2) w.vars.lifeBoost = std::numeric_limits<double>::infinity();
		if (k == 3) { w.seaLevel = 1e30; w.vars.seaBase = -1e30; }
		cycle(w, 16);
		eq(lifeFinite(w), std::string(), "world scalar case " + std::to_string(k));
	}
}

namespace {
struct TuneField { const char* name; double* (*get)(LifeTune&); };
#define DM_ADV_TF(x) { #x, [](LifeTune& t) { return &t.x; } }
#define DM_ADV_TA(x, k) { #x "[" #k "]", [](LifeTune& t) { return &t.x[k]; } }
const TuneField kTuneFields[] = {
	DM_ADV_TF(deepSea), DM_ADV_TF(iceBiome), DM_ADV_TF(lakeWater), DM_ADV_TF(regrowYears), DM_ADV_TF(seedSource), DM_ADV_TF(seedVeg),
	DM_ADV_TF(seedMin), DM_ADV_TF(sproutNbr), DM_ADV_TF(fireP), DM_ADV_TF(dryA), DM_ADV_TF(dryB), DM_ADV_TF(fireMinVeg), DM_ADV_TF(burnVeg),
	DM_ADV_TF(wetOut), DM_ADV_TF(radDecay), DM_ADV_TF(farmDecay), DM_ADV_TF(urbanDecay), DM_ADV_TF(habTLo), DM_ADV_TF(habTHi),
	DM_ADV_TF(lavaBarren), DM_ADV_TF(scorchBarren), DM_ADV_TF(vegBarren), DM_ADV_TF(snowH), DM_ADV_TF(snowT), DM_ADV_TF(rockSlope),
	DM_ADV_TF(rockH), DM_ADV_TF(beachH), DM_ADV_TF(beachSlope), DM_ADV_TF(beachT), DM_ADV_TF(wetlandM), DM_ADV_TF(wetlandH),
	DM_ADV_TF(wetlandSlope), DM_ADV_TF(wetlandT), DM_ADV_TF(evap), DM_ADV_TF(tundraT), DM_ADV_TF(borealT), DM_ADV_TF(taigaM),
	DM_ADV_TF(coolT), DM_ADV_TF(hotT), DM_ADV_TF(desertM), DM_ADV_TF(grassM), DM_ADV_TF(savannaM), DM_ADV_TF(rainM), DM_ADV_TF(jitterM),
	DM_ADV_TF(jitterG), DM_ADV_TF(jitterT), DM_ADV_TF(refStep), DM_ADV_TF(plantMin), DM_ADV_TF(grow), DM_ADV_TF(die), DM_ADV_TF(kill),
	DM_ADV_TF(establishedRel), DM_ADV_TF(sprout), DM_ADV_TF(spread), DM_ADV_TF(spreadBoost), DM_ADV_TF(spreadCap0), DM_ADV_TF(spreadCapRef),
	DM_ADV_TF(spreadSlope), DM_ADV_TF(spreadNoise), DM_ADV_TF(spreadPatch), DM_ADV_TF(airVeg), DM_ADV_TF(airDecay), DM_ADV_TF(seedSourceRel),
	DM_ADV_TF(driftBoost), DM_ADV_TF(driftSeaBoost), DM_ADV_TF(forestVeg), DM_ADV_TF(maxBoost), DM_ADV_TF(burnFauna), DM_ADV_TF(burnFert),
	DM_ADV_TF(faunaGrow), DM_ADV_TF(faunaDie), DM_ADV_TF(faunaSeed), DM_ADV_TF(faunaSrc), DM_ADV_TF(fishShallow), DM_ADV_TF(fishDeep),
	DM_ADV_TF(fishDeepAt), DM_ADV_TF(fishLake), DM_ADV_TF(fertRate), DM_ADV_TF(fertBase), DM_ADV_TF(fertVar), DM_ADV_TF(fertLow),
	DM_ADV_TF(fertHigh), DM_ADV_TF(fertAsh), DM_ADV_TF(fertMin), DM_ADV_TF(fertMax), DM_ADV_TF(fertCap), DM_ADV_TF(wetDecay),
	DM_ADV_TF(scorchDecay), DM_ADV_TF(tiny), DM_ADV_TF(habColdZero), DM_ADV_TF(habHotZero), DM_ADV_TF(habFert), DM_ADV_TF(habMin),
	DM_ADV_TA(cap, 0), DM_ADV_TA(cap, 6), DM_ADV_TA(cap, 7), DM_ADV_TA(cap, 13), DM_ADV_TA(spreadK, 1), DM_ADV_TA(spreadK, 2), DM_ADV_TA(spreadK, 6),
	DM_ADV_TA(spreadFresh, 0), DM_ADV_TA(spreadFresh, 1), DM_ADV_TA(faunaCap, 6), DM_ADV_TA(faunaCap, 2), DM_ADV_TA(fertFresh, 0), DM_ADV_TA(fertFresh, 3),
	DM_ADV_TA(habRain, 0), DM_ADV_TA(habRain, 1), DM_ADV_TA(habFresh, 0), DM_ADV_TA(habFresh, 2), DM_ADV_TA(habSlope, 0), DM_ADV_TA(habSlope, 1),
	DM_ADV_TA(habCoast, 0), DM_ADV_TA(habCoast, 1), DM_ADV_TA(habCoast, 2),
};
#undef DM_ADV_TF
#undef DM_ADV_TA
} // namespace

DM_TEST("adv robust: every LifeTune number poisoned (NaN, +-inf, 0, -1, 1e9) never writes a non-finite value") {
	const double vals[] = { std::nan(""), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), 0, -1, 1e9 };
	int runs = 0, failed = 0;
	std::set<std::string> culprits;
	for (const TuneField& tf : kTuneFields) {
		for (double v : vals) {
			auto pw = makeWorld({ .L = 3, .seed = 4 });
			World& w = *pw;
			life::classify(w); life::fill(w, 0.7);
			*tf.get(w.tune<LifeTune>()) = v;
			w.vars.lifeBoost = 25;
			for (int c = 0; c < w.n; c += 29) { w.moist[size_t(c)] = 0.05f; life::ignite(w, c); }
			cycle(w, 16);
			life::seed(w, 5, 0.3); life::bloom(w, 8, 0.3, 0.3); life::fill(w, 0.5);
			for (int c = 0; c < w.n; c += 31) life::ignite(w, c);
			cycle(w, 8);
			runs++;
			const std::string r = lifeFinite(w);
			const life::Cover cv = life::cover(w);
			const bool coverOk = std::isfinite(cv.land) && std::isfinite(cv.veg) && std::isfinite(cv.forest);
			if (!r.empty() || !coverOk) { culprits.insert(tf.name); if (failed++ < 8) fail(std::string(tf.name) + " = " + std::to_string(v) + ": " + (r.empty() ? "cover" : r)); }
		}
	}
	tellA(std::to_string(runs) + " poisoned-tuning runs over " + std::to_string(std::size(kTuneFields)) + " LifeTune numbers: " + std::to_string(failed) + " wrote a non-finite value" +
		(culprits.empty() ? "" : " (" + std::to_string(culprits.size()) + " fields)"));
	// absurd integer tuning: must not hang, overflow or write garbage
	for (int k = 0; k < 4; k++) {
		auto pw = makeWorld({ .L = 3, .seed = 4 });
		World& w = *pw;
		life::classify(w); life::fill(w, 0.7);
		LifeTune& T = w.tune<LifeTune>();
		if (k == 0) { T.seedsPerYear = 2000000000; T.driftMax = 50; }
		if (k == 1) { T.seedHops = 100000; T.seedsPerYear = 80; }
		if (k == 2) { T.maxBurning = -5; T.fireEventsPerTick = -1; T.driftMax = -1; T.habFreshHops = -3; }
		if (k == 3) { T.seedsPerYear = -8; T.seedHops = -2; T.maxBurning = 0; }
		w.vars.lifeBoost = 400;
		std::fill(w.veg.begin(), w.veg.begin() + w.n / 2, 0.0f);
		for (int c = 0; c < w.n; c += 29) life::ignite(w, c);
		cycle(w, 16);
		eq(lifeFinite(w), std::string(), "integer tuning case " + std::to_string(k));
		eq(firesConsistent(w), std::string(), "fires cache, integer case " + std::to_string(k));
	}
}

// ================================================================== read-only, determinism
DM_TEST("adv determinism: L = 6 genesis wave with fires, twice -> same hash; cover() / capacity() calls in between change nothing") {
	auto run = [](bool readers) {
		auto pw = makeWorld({ .L = 6, .seed = 8 });
		World& w = *pw;
		life::classify(w);
		std::fill(w.veg.begin(), w.veg.end(), 0.0f); std::fill(w.fauna.begin(), w.fauna.end(), 0.0f);
		w.vars.lifeBoost = 25;
		life::seed(w, bestHab(w), 0.12);
		for (int t = 1; t <= 160; t++) {
			w.tick++; life::burn(w); life::step(w, int(w.tick % 2), 2);
			if (readers) { (void)life::cover(w); (void)life::capacity(w, t * 97); }
			if (t % 40 == 0) for (int c = 0; c < w.n; c += 4001) life::ignite(w, c);
			if (t == 100) { life::classify(w); life::habitability(w); }
		}
		w.vars.lifeBoost = 1;
		for (int t = 1; t <= 160; t++) { w.tick++; life::burn(w); life::step(w, int(w.tick % 8), 8); if (readers && t % 3 == 0) (void)life::cover(w); }
		return hash(w);
	};
	const uint32_t a = run(false), b = run(false), c = run(true);
	eq(b, a, "two identical runs");
	eq(c, a, "the same run with read-only calls in between");
}

// ================================================================== fires
DM_TEST("adv fire: random sequences of ignite / burn / step / sea-level changes / rain keep the cache exact") {
	auto pw = makeWorld({ .L = 4, .seed = 9 });
	World& w = *pw;
	life::classify(w); life::fill(w, 1);
	for (int c = 0; c < w.n; c++) w.moist[size_t(c)] = 0.12f;      // drought: fires run
	Rng r(77);
	int checks = 0, bad = 0, maxBurn = 0;
	for (int it = 0; it < 3000; it++) {
		const int op = r.integer(0, 9);
		if (op <= 2) life::ignite(w, r.integer(-3, w.n + 3), r.range(-0.5, 2));
		else if (op <= 5) { w.tick++; life::burn(w); }
		else if (op == 6) life::step(w, r.integer(0, 7), 8);
		else if (op == 7) { w.seaLevel = r.range(-0.5, 1.5); }                       // flood land that may be burning
		else if (op == 8) { const int c = r.integer(0, w.n - 1); std::vector<int32_t> buf; w.geo->ring(c, 2, buf); for (int m : buf) w.wet[size_t(m)] = 0.9f; }   // rain
		else { life::rebuild(w); }
		int k = 0; for (int c = 0; c < w.n; c++) if (w.fire[size_t(c)] > 0) k++;
		maxBurn = std::max(maxBurn, k);
		const std::string m = firesConsistent(w);
		checks++;
		if (!m.empty() && bad++ < 4) fail("op " + std::to_string(it) + " (" + std::to_string(op) + "): " + m);
	}
	tellA("random fire ops: " + std::to_string(checks) + " checks, " + std::to_string(bad) + " cache mismatches, most cells burning " + std::to_string(maxBurn));
	// burning sea cells never stay alight past a burn, burning cells never sit on sea / ice / lake after burn
	w.seaLevel = 0;
	w.tick++; life::burn(w);
	int wrong = 0;
	for (int c = 0; c < w.n; c++) if (w.fire[size_t(c)] > 0 && (w.elev[size_t(c)] < w.seaLevel || w.wet[size_t(c)] > 0.3f)) wrong++;
	eq(wrong, 0, "after burn nothing burns on sea or wet ground");
	eq(lifeFinite(w), std::string());
}

DM_TEST("adv fire: fire:start payloads, at most 2 a tick, only from ignite; spread itself emits nothing") {
	auto pw = makeWorld({ .L = 5, .seed = 3 });
	World& w = *pw;
	life::classify(w); life::fill(w, 1);
	for (int c = 0; c < w.n; c++) w.moist[size_t(c)] = 0.1f;
	std::vector<int> lit;
	const auto ev = capture(w, [&] {
		for (int c = 0; c < w.n && lit.size() < 10; c += 101) if (life::ignite(w, c)) lit.push_back(c);
	});
	eq(int(ev.size()), 2, "10 ignitions in one tick -> 2 fire:start");
	for (size_t k = 0; k < ev.size(); k++) {
		const EvFireStart* f = ev[k].as<EvFireStart>();
		if (!ok(f != nullptr, "payload is EvFireStart")) continue;
		eq(f->cell, lit[k], "payload cell = the ignited cell");
	}
	int spreadEv = 0, burnt = 0;
	for (int t = 0; t < 30; t++) {
		w.tick++;
		spreadEv += int(capture(w, [&] { life::burn(w); }).size());
		spreadEv += int(capture(w, [&] { life::step(w, int(w.tick % 8), 8); }).size());
	}
	for (int c = 0; c < w.n; c++) if (w.scorch[size_t(c)] > 0) burnt++;
	tellA("10 drought fires burnt " + std::to_string(burnt) + " cells in 30 ticks; events emitted by burn / step: " + std::to_string(spreadEv));
	ok(burnt > 100, "the fires spread");
	eq(spreadEv, 0, "burn and step emit nothing");
	w.tick++;
	const int c2 = findLand(w, [&](int c) { return w.fire[size_t(c)] == 0 && w.veg[size_t(c)] > 0.3f; });
	eq(int(capture(w, [&] { life::ignite(w, c2); }).size()), 1, "the limit resets on the next tick");
}

DM_TEST("adv fire: ignite refuses at the 1,500 cap, on thin / wet / sea / ice / lake / burning cells and for intensity <= 0") {
	auto pw = makeWorld({ .L = 6, .seed = 2 });
	World& w = *pw;
	life::classify(w); life::fill(w, 1);
	int lit = 0, tried = 0;
	for (int c = 0; c < w.n && lit < 1500; c++) { if (!landA(w, c)) continue; tried++; if (life::ignite(w, c)) lit++; }
	eq(lit, 1500, "1,500 fires lit");
	const int extra = findLand(w, [&](int c) { return w.fire[size_t(c)] == 0 && w.veg[size_t(c)] > 0.3f; });
	ok(!life::ignite(w, extra), "the 1,501st is refused");
	w.tick++; life::burn(w);
	int burningNow = 0; for (int c = 0; c < w.n; c++) if (w.fire[size_t(c)] > 0) burningNow++;
	ok(burningNow <= 1500, "after a burn at most 1,500 burn (" + std::to_string(burningNow) + ")");
	auto p2 = makeWorld({ .L = 4, .seed = 2 });
	World& v = *p2;
	life::classify(v); life::fill(v, 1);
	const int c = findLand(v, [&](int m) { return v.veg[size_t(m)] > 0.4f; });
	v.veg[size_t(c)] = 0.149f; ok(!life::ignite(v, c), "veg 0.149"); v.veg[size_t(c)] = 0.6f;
	v.wet[size_t(c)] = 0.31f; ok(!life::ignite(v, c), "wet 0.31"); v.wet[size_t(c)] = 0;
	v.ice[size_t(c)] = 0.6f; ok(!life::ignite(v, c), "ice"); v.ice[size_t(c)] = 0;
	v.water[size_t(c)] = 0.1f; ok(!life::ignite(v, c), "lake"); v.water[size_t(c)] = 0;
	ok(!life::ignite(v, c, 0) && !life::ignite(v, c, -1) && !life::ignite(v, c, std::nan("")), "intensity 0 / -1 / NaN");
	ok(life::ignite(v, c, 1e30), "a huge intensity is fine");
	ok(v.fire[size_t(c)] > 0.55f && v.fire[size_t(c)] <= 1.0f, "fire value in its fresh range (" + std::to_string(v.fire[size_t(c)]) + ")");
	ok(!life::ignite(v, c), "already burning");
	const int s = [&] { for (int m = 0; m < v.n; m++) if (!landA(v, m)) return m; return -1; }();
	v.veg[size_t(s)] = 0.9f; ok(!life::ignite(v, s), "sea cell with junk vegetation");
}

// ================================================================== the poles, levels
DM_TEST("adv poles: seed, bloom, fire and growth at both poles, L = 4 and 6") {
	for (int L : { 4, 6 }) {
		auto pw = makeWorld({ .L = L, .seed = 1, .land = 1, .basics = true });
		World& w = *pw;
		std::fill(w.ice.begin(), w.ice.end(), 0.0f);
		std::fill(w.temp.begin(), w.temp.end(), 15.0f);
		std::fill(w.moist.begin(), w.moist.end(), 0.5f);
		life::classify(w);
		std::fill(w.veg.begin(), w.veg.end(), 0.0f);
		for (double y : { 1.0, -1.0 }) {
			const int p = w.geo->cellAt(0, y, 0);
			std::vector<int32_t> disc;
			w.geo->within(p, 0.12, disc);
			life::seed(w, p, 0.12);
			int planted = 0, plantable = 0;
			for (int c : disc) if (life::capacity(w, c) > w.tune<LifeTune>().plantMin) { plantable++; if (w.veg[size_t(c)] >= 0.35f) planted++; }
			eq(planted, plantable, "seed covers the whole disc at the pole (L " + std::to_string(L) + ", y " + fmtA(y, 0) + ")");
			ok(plantable > 10, "the disc around the pole has land");
			life::bloom(w, p, 0.1, 0.5);
			for (int t = 0; t < 40; t++) { w.tick++; life::burn(w); life::step(w, int(w.tick % 8), 8); }
			w.moist[size_t(p)] = 0.05f;
			ok(life::ignite(w, p), "fire at the pole");
			for (int t = 0; t < 10; t++) { w.tick++; life::burn(w); }
		}
		eq(lifeFinite(w), std::string(), "finite at L " + std::to_string(L));
		eq(firesConsistent(w), std::string());
	}
}

// ================================================================== exact rates
DM_TEST("adv rates: rad 1.5 %/yr, unowned farm 5 %/yr and urban 1 %/yr, exact over 40 years for 1, 2 and 8 slices") {
	for (int parts : { 1, 2, 8 }) {
		auto pw = makeWorld({ .L = 3, .seed = 1, .land = 1 });
		World& w = *pw;
		life::classify(w);
		const int c = 40;
		w.rad[size_t(c)] = 0.8f; w.farm[size_t(c)] = 0.9f; w.urban[size_t(c)] = 0.9f; w.owner[size_t(c)] = -1;
		const int ticks = 160;                        // 40 years
		for (int t = 0; t < ticks; t++) { w.tick++; life::step(w, int(w.tick % parts), parts); }
		const std::string at = " (" + std::to_string(parts) + " slices)";
		near(w.rad[size_t(c)], 0.8 * std::pow(0.985, 40), 2e-4, "rad" + at);
		near(w.farm[size_t(c)], 0.9 * std::pow(0.95, 40), 2e-4, "farm" + at);
		near(w.urban[size_t(c)], 0.9 * std::pow(0.99, 40), 2e-4, "urban" + at);
	}
}

DM_TEST("adv fill: frac edge cases (NaN, negative, > 1) and every land cell at frac x capacity") {
	auto pw = makeWorld({ .L = 4, .seed = 3 });
	World& w = *pw;
	life::classify(w);
	for (double frac : { std::nan(""), -2.0, 5.0, 0.25 }) {
		life::fill(w, frac);
		const double want = std::isnan(frac) ? 1.0 : std::clamp(frac, 0.0, 1.0);
		int bad = 0;
		for (int c = 0; c < w.n; c++) {
			const double k = life::capacity(w, c);
			if (std::fabs(w.veg[size_t(c)] - want * k) > 1e-6 && bad++ < 2) fail("frac " + std::to_string(frac) + ": cell " + std::to_string(c));
		}
	}
	eq(lifeFinite(w), std::string());
}

// ================================================================== performance
DM_TEST("adv speed at L = 6: history schedule with fires, the life-phase wave, an absurd boost") {
	auto timeRun = [](World& w, int ticks, int parts, int igniteEvery, double& mean, double& worst) {
		worst = 0;
		const double t0 = nowMs();
		for (int t = 1; t <= ticks; t++) {
			if (igniteEvery > 0 && t % igniteEvery == 0) for (int c = t % 97; c < w.n; c += 2111) life::ignite(w, c);
			w.tick++;
			const double s = nowMs();
			life::burn(w); life::step(w, int(w.tick % parts), parts);
			const double d = nowMs() - s;
			if (d > worst) { worst = d; }
			if (d > 2) tellA("    slow tick " + std::to_string(t) + ": " + fmtA(d) + " ms");
		}
		mean = (nowMs() - t0) / ticks;
	};
	double mH, wH, mF, wF, mG, wG, mB, wB;
	{
		auto pw = makeWorld({ .L = 6, .seed = 4 });
		World& w = *pw;
		life::classify(w); life::fill(w, 0.9); life::habitability(w);
		w.tick++; life::step(w, 0, 8);
		timeRun(w, 800, 8, 0, mH, wH);
		for (int c = 0; c < w.n; c++) w.moist[size_t(c)] = std::min(w.moist[size_t(c)], 0.2f);    // a dry planet: fires everywhere
		timeRun(w, 400, 8, 20, mF, wF);
	}
	{
		auto pw = makeWorld({ .L = 6, .seed = 5 });
		World& w = *pw;
		life::classify(w);
		std::fill(w.veg.begin(), w.veg.end(), 0.0f);
		w.vars.lifeBoost = 25;
		life::seed(w, bestHab(w), 0.12);
		timeRun(w, 480, 2, 0, mG, wG);
		w.vars.lifeBoost = 1e6;                         // clamped to maxBoost 400
		std::fill(w.veg.begin(), w.veg.end(), 0.0f);
		life::seed(w, bestHab(w), 0.12);
		timeRun(w, 100, 2, 0, mB, wB);
	}
	tellA("L = 6 burn + step per tick: history (1 of 8) mean " + fmtA(mH) + " ms, worst " + fmtA(wH) + "; dry planet with fires mean " + fmtA(mF) + ", worst " + fmtA(wF) +
		"; life-phase wave (1 of 2, boost 25) mean " + fmtA(mG) + ", worst " + fmtA(wG) + "; boost 400 mean " + fmtA(mB) + ", worst " + fmtA(wB));
	ok(mH <= 3.0, "history slice mean <= 5 x 0.6 ms");
	ok(mF <= 5.0, "with fires");
	ok(mG <= 12.5, "life phase mean within 5 x the 2.5 ms tick budget");
	ok(wH < 30 && wF < 30 && wG < 30 && wB < 30, "no single tick near the 30 ms cap");
}

// ================================================================== maps
DM_TEST("adv maps: L = 6 fauna, fertility and a dry-planet fire scar (shots/ue-life-adv-*.png)") {
	auto pw = makeWorld({ .L = 6, .seed = 2 });
	World& w = *pw;
	life::classify(w); life::fill(w, 1);
	for (int t = 0; t < 400; t++) { w.tick++; life::burn(w); life::step(w, int(w.tick % 8), 8); }
	life::habitability(w);
	ok(mapimg::field(w, "fauna", shot("ue-life-adv-fauna.png"), 0, 1), "fauna map");
	ok(mapimg::field(w, "fert", shot("ue-life-adv-fert.png"), 0, 1.2), "fertility map");
	ok(mapimg::field(w, "hab", shot("ue-life-adv-hab.png"), 0, 1, { .dimSea = true }), "habitability map");
	for (int c = 0; c < w.n; c++) if (landA(w, c)) w.moist[size_t(c)] = std::min(w.moist[size_t(c)], 0.18f);
	for (int c = 0; c < w.n; c += 1500) life::ignite(w, c);
	for (int t = 0; t < 120; t++) { w.tick++; life::burn(w); }
	ok(mapimg::field(w, "scorch", shot("ue-life-adv-scorch.png"), 0, 1, { .dimSea = true }), "scorch map");
	int scorched = 0, land = 0;
	for (int c = 0; c < w.n; c++) if (landA(w, c)) { land++; if (w.scorch[size_t(c)] > 0) scorched++; }
	tellA("dry planet, 28 ignitions: " + std::to_string(scorched) + " of " + std::to_string(land) + " land cells scorched");
}
