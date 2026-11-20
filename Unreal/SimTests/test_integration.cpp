// DEMIURGE — SimTests/test_integration.cpp : the whole simulation as one system (BUILD.md Stage
// A-int). Owner: simulation integrator (key "integration").
//
// Every test here needs the REAL modules: they run in `run.ps1 all` and in
// `Tools/simrun/simrun.ps1 --tests integration` (every real module, no stubs). In `run.ps1
// integration` (every module stubbed) each test returns at once.
//
// The long checks (6+ seeds x 4,000 years, the SPEC-SIM 4 balance table, tick timings at --jobs 1)
// live in Tools/simrun (simrun.ps1 --seeds 1-10 --policy passive --summary); these tests are the
// fast cross-module contract: worlds made by sim::create run for centuries with every event valid
// and no NaN, the balance overrides are live (also after a load), determinism holds with a player
// acting, a save / load boundary continues deterministically, the chronicle never touches the sim
// stream, and every power can be used on a living world.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "Balance.h"
#include "Chronicle.h"
#include "Civ.h"
#include "Climate.h"
#include "Life.h"
#include "Powers.h"
#include "Save.h"
#include "Sim.h"
#include "Society.h"
#include "State.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

void say(const std::string& s) { std::printf("  %s\n", s.c_str()); std::fflush(stdout); }
std::string f2(double x) { char b[64]; std::snprintf(b, sizeof b, "%.2f", x); return b; }

// True when every module the whole simulation needs is the real one.
bool allReal() {
	for (const char* m : { "terrain", "climate", "hydro", "life", "path", "units", "civ", "economy", "society",
	                       "diplomacy", "powers", "disasters", "chronicle", "sim", "balance", "names", "save" })
		if (stub::stubbed(m)) return false;
	return true;
}

int liveCount(const World& w) {
	int k = 0;
	for (const Settlement& s : w.settlements) if (!s.dead) k++;
	return k;
}

// A world in history: sim::create + genesis::skip, event contract checked from the start.
std::unique_ptr<World> historyWorld(uint32_t seed, int L) {
	auto w = sim::create({ .seed = seed, .name = "", .L = L });
	dmt::watch(*w);
	sim::genesis::skip(*w, {});
	return w;
}

// A deterministic scripted player (no rng of the world): every few years one power from the table
// on a settlement or a random land cell picked with its own Rng.
struct Player {
	Rng r;
	int uses = 0, refused = 0;
	std::map<std::string, int> byId;
	explicit Player(uint32_t seed) : r(Rng(seed).fork("integration:player")) {}
	void act(World& w) {
		if (w.phase != Phase::History) return;
		const std::vector<int> ids = civ::live(w);
		for (const powers::PowerDef& d : powers::kPowers) {
			if (d.group == powers::Group::Genesis) continue;
			if (!r.chance(0.15)) continue;
			int cell = -1;
			if ((d.target == powers::Target::Settlement || r.chance(0.6)) && !ids.empty())
				cell = w.settlements[size_t(ids[size_t(r.pickIndex(int(ids.size())))])].cell;
			else cell = r.integer(0, w.n - 1);
			if (!powers::can(w, d.id, cell).ok) continue;
			bool used = false;
			if (d.brush) {
				powers::UseOpts o;
				o.dt = 0;
				used = powers::use(w, d.id, cell, o);
				o.dt = sim::kRealPerTick;
				for (int k = 0; k < 4 && used; k++) used = powers::use(w, d.id, cell, o);
			} else {
				used = powers::use(w, d.id, cell, {});
			}
			if (used) { uses++; byId[d.id]++; }
			else refused++;
		}
	}
};

} // namespace

// ------------------------------------------------------------------ the whole system runs

DM_TEST("integration: 400 years at L6 from genesis skip: valid events, no NaN, the world grows") {
	if (!allReal()) return;
	const double t0 = nowMs();
	auto w = historyWorld(1, 6);
	if (!ok(w->phase == Phase::History, "genesis skip reached history")) return;
	std::map<std::string, int> counts;
	w->bus.onAll([&](const Event& e) { counts[e.name()]++; });
	double tickMs = 0, worst = 0;
	int ticks = 0;
	for (int y = 0; y < 400; y++) {
		for (int k = 0; k < sim::kTicksPerYear; k++) {
			const double a = nowMs();
			sim::advance(*w, 1);
			const double dt = nowMs() - a;
			tickMs += dt;
			worst = std::max(worst, dt);
			ticks++;
		}
		if (y % 100 == 99) {
			if (!noNaN(*w, "year " + std::to_string(y + 1))) return;
			if (!eq(sim::firstNonFinite(*w), std::string(), "firstNonFinite at year " + std::to_string(y + 1))) return;
		}
	}
	const double mean = tickMs / std::max(1, ticks);
	say("L6 400 years: " + std::to_string(liveCount(*w)) + " settlements, pop " + f2(w->stats.pop) + ", age " +
	    std::to_string(w->age) + ", chronicle " + std::to_string(w->chronicle.size()) + " entries, ticks mean " + f2(mean) +
	    " ms, worst " + f2(worst) + " ms (" + f2((nowMs() - t0) / 1000) + " s in all)");
	std::string ev;
	for (const auto& kv : counts) ev += " " + kv.first + " " + std::to_string(kv.second);
	say("events:" + ev);
	ok(liveCount(*w) >= 2, "settlers founded new settlements");
	ok(w->stats.pop > 200, "population grew past 200");
	ok(counts["settlement:founded"] >= 2, "settlement:founded");
	ok(counts["chronicle:entry"] >= 5, "the chronicle wrote");
	ok(w->snapshots.size() >= 10, "snapshots every 25 years");
	ok(mean <= 12.5, "mean tick within 5x the 2.5 ms budget");
	eq(int(w->phase), int(Phase::History), "still in history");
}

DM_TEST("integration: genesis played through powers (comets, seeds, awaken) reaches history at L5") {
	if (!allReal()) return;
	auto w = sim::create({ .seed = 4, .name = "", .L = 5 });
	dmt::watch(*w);
	Rng r = Rng(4).fork("integration:genesis");
	int guard = 0, comets = 0, seeds = 0;
	while (w->phase == Phase::Molten && guard++ < 200) sim::advance(*w, 1);
	eq(int(w->phase), int(Phase::Water), "molten cools to water");
	while (w->phase == Phase::Water && guard++ < 4000) {
		if (sim::genesis::progress(*w).canAdvance && w->seaLevel >= -0.05 && w->disasters.empty()) { sim::genesis::next(*w); break; }
		if (w->tick % 6 == 0 && w->seaLevel < 0) {
			const int c = r.integer(0, w->n - 1);
			if (powers::can(*w, "comet", c).ok && powers::use(*w, "comet", c)) comets++;
		}
		sim::advance(*w, 1);
	}
	eq(int(w->phase), int(Phase::Life), "comets filled the sea, water -> life");
	while (w->phase == Phase::Life && guard++ < 8000) {
		if (sim::genesis::progress(*w).canAdvance) { sim::genesis::next(*w); break; }
		if (w->tick % 4 == 0)
			for (int t = 0; t < 20; t++) {
				const int c = r.integer(0, w->n - 1);
				if (powers::can(*w, "seed", c).ok) { if (powers::use(*w, "seed", c)) seeds++; break; }
			}
		sim::advance(*w, 1);
	}
	eq(int(w->phase), int(Phase::People), "life spread, life -> people");
	const int site = civ::bestSite(*w);
	ok(w->valid(site) && powers::use(*w, "awaken", site), "awaken at civ::bestSite");
	eq(int(w->phase), int(Phase::History), "the first awakening starts history");
	ok(w->god.awe >= 60, "Awe 60");
	ok(w->vars.seaBase.has_value(), "climate calibrated");
	say("genesis: " + std::to_string(comets) + " comets (sea " + f2(w->seaLevel) + "), " + std::to_string(seeds) + " seeds, " +
	    std::to_string(w->tick) + " ticks; veg " + f2(life::cover(*w).veg));
	sim::advance(*w, 400);
	noNaN(*w, "100 years after a played genesis");
	ok(liveCount(*w) >= 1, "the people live");
}

// ------------------------------------------------------------------ balance is live

DM_TEST("integration: balance overrides are applied by sim::create and again after a load") {
	if (!allReal()) return;
	auto w = sim::create({ .seed = 2, .name = "", .L = 3 });
	const EconomyTune defaults{};
	const EconomyTune& E = w->tune<EconomyTune>();
	ok(E.expandCooldown != defaults.expandCooldown, "economy.expandCooldown overridden (" + f2(E.expandCooldown) + ")");
	ok(E.ageNeed != defaults.ageNeed, "economy.ageNeed overridden");
	const SocietyTune sdef{};
	ok(w->tune<SocietyTune>().loveDrift != sdef.loveDrift, "society.loveDrift overridden");
	sim::genesis::skip(*w, {});
	std::vector<uint8_t> bytes;
	if (!ok(state::serialize(*w, bytes), "serialize")) return;
	auto x = state::deserialize(bytes);
	if (!ok(x != nullptr, "deserialize")) return;
	eq(x->tune<EconomyTune>().expandCooldown, defaults.expandCooldown, "a raw loaded world has the defaults (tuning is not saved)");
	sim::use(*x);
	eq(x->tune<EconomyTune>().expandCooldown, E.expandCooldown, "sim::use re-applies the overrides");
	eq(x->tune<SocietyTune>().loveDrift, w->tune<SocietyTune>().loveDrift);
	eq(x->tune<DiplomacyTune>().tradeK, w->tune<DiplomacyTune>().tradeK);
	auto y = sim::create({ .seed = 2, .name = "", .L = 3 });
	balance::apply(*y);
	eq(y->tune<EconomyTune>().expandCooldown, E.expandCooldown, "apply is idempotent");
}

// ------------------------------------------------------------------ determinism

DM_TEST("integration: determinism with a player acting: two worlds advanced alternately stay identical") {
	if (!allReal()) return;
	auto a = historyWorld(7, 5);
	auto b = historyWorld(7, 5);
	Player pa(7), pb(7);
	for (int y = 0; y < 250; y++) {
		pa.act(*a);
		pb.act(*b);
		for (int k = 0; k < sim::kTicksPerYear; k++) { sim::advance(*a, 1); sim::advance(*b, 1); }
		if (y % 50 == 49 && !eq(sim::fingerprint(*a), sim::fingerprint(*b), "fingerprint at year " + std::to_string(y + 1))) return;
	}
	eq(hash(*a), hash(*b), "dmt::hash agrees");
	eq(a->rng.s, b->rng.s, "same rng stream");
	std::string used;
	for (const auto& kv : pa.byId) used += " " + kv.first + " " + std::to_string(kv.second);
	say("player: " + std::to_string(pa.uses) + " uses (" + std::to_string(pa.refused) + " refused):" + used);
	ok(pa.uses >= 50, "the player acted");
	noNaN(*a, "after 250 years with a player");
	eq(sim::firstNonFinite(*a), std::string());
}

DM_TEST("integration: the chronicle never draws from the sim stream") {
	if (!allReal()) return;
	auto a = sim::create({ .seed = 9, .name = "", .L = 5 });
	auto b = sim::create({ .seed = 9, .name = "", .L = 5 });
	b->bus.clear();                 // b has no chronicle subscription (and no event checking)
	dmt::watch(*a);
	sim::genesis::skip(*a, {});
	sim::genesis::skip(*b, {});
	sim::advance(*a, 4 * 300);
	sim::advance(*b, 4 * 300);
	ok(a->chronicle.size() > b->chronicle.size() + 5, "a has a chronicle (" + std::to_string(a->chronicle.size()) + " vs " +
	   std::to_string(b->chronicle.size()) + ")");
	eq(a->rng.s, b->rng.s, "same rng stream with and without the chronicle");
	eq(a->owner == b->owner, true, "same territories");
	eq(liveCount(*a), liveCount(*b), "same settlements");
	eq(a->stats.pop, b->stats.pop, "same population");
}

// ------------------------------------------------------------------ save / load

DM_TEST("integration: mid-run save / load at L6: exact re-save, deterministic continuation, close to the original") {
	if (!allReal()) return;
	auto w = historyWorld(3, 6);
	sim::advance(*w, 4 * 300);
	std::vector<uint8_t> bytes, again;
	std::string why;
	if (!ok(state::serialize(*w, bytes, &why), "serialize: " + why)) return;
	auto x = state::deserialize(bytes, &why);
	auto y = state::deserialize(bytes, &why);
	if (!ok(x && y, "deserialize: " + why)) return;
	state::serialize(*x, again);
	ok(again == bytes, "re-saving the loaded world reproduces the bytes exactly");
	sim::use(*x);
	sim::use(*y);
	dmt::watch(*x);
	eq(x->year, w->year, "year");
	eq(x->rng.s, w->rng.s, "rng stream continues");
	eq(liveCount(*x), liveCount(*w), "settlements");
	eq(x->chronicle.size(), w->chronicle.size(), "chronicle");
	eq(sim::isYearlyTick(*x), sim::isYearlyTick(*w), "yearly alignment");
	const int s0 = liveCount(*w);
	const double p0 = w->stats.pop;
	for (int i = 0; i < 4 * 150; i++) { sim::advance(*w, 1); sim::advance(*x, 1); sim::advance(*y, 1); }
	eq(sim::fingerprint(*x), sim::fingerprint(*y), "two loads of the same bytes continue identically");
	noNaN(*x, "150 years after the load");
	const double pw = w->stats.pop, px = x->stats.pop;
	const int sw = liveCount(*w), sx = liveCount(*x);
	say("save " + std::to_string(bytes.size()) + " bytes at year 300 (" + std::to_string(s0) + " settlements, pop " + f2(p0) +
	    "); 150 years later: original " + std::to_string(sw) + " / " + f2(pw) + ", loaded " + std::to_string(sx) + " / " + f2(px));
	ok(std::fabs(px - pw) <= 0.25 * std::max(pw, 1.0), "population within 25 % of the never-saved run (quantization only)");
	ok(std::abs(sx - sw) <= std::max(3, sw / 4), "settlement count close to the never-saved run");
}

// ------------------------------------------------------------------ every power on a living world

DM_TEST("integration: every power used on a living L5 world, then 60 years: valid events, no NaN") {
	if (!allReal()) return;
	auto w = historyWorld(5, 5);
	sim::advance(*w, 4 * 250);
	w->god.awe = w->god.aweMax = 1200;
	// (v3, design review: powers past Bronze unlock by age only) the world's age opens every power
	w->age = Age::INDUSTRY;
	for (Culture& c : w->cultures) if (!c.dead) c.age = std::max(c.age, int(Age::BRONZE));
	std::map<std::string, std::string> result;
	for (const powers::PowerDef& d : powers::kPowers) {
		if (d.group == powers::Group::Genesis && std::string(d.id) != "awaken") continue;
		w->god.awe = w->god.aweMax = 1200;          // (society::yearly recomputes aweMax every year)
		const std::vector<int> ids = civ::live(*w);
		int cell = -1;
		for (int sid : ids) {
			const int c = w->settlements[size_t(sid)].cell;
			if (powers::can(*w, d.id, c).ok) { cell = c; break; }
		}
		if (cell < 0)
			for (int c = 0; c < w->n; c += 7) if (powers::can(*w, d.id, c).ok) { cell = c; break; }
		if (cell < 0) { result[d.id] = "no target"; continue; }
		bool used;
		if (d.brush) {
			powers::UseOpts o;
			used = powers::use(*w, d.id, cell, o);
			o.dt = sim::kRealPerTick;
			for (int k = 0; k < 8 && used; k++) used = powers::use(*w, d.id, cell, o);
		} else {
			used = powers::use(*w, d.id, cell, {});
		}
		result[d.id] = used ? "ok" : "refused";
		sim::advance(*w, 4);
		if (!noNaN(*w, std::string("after ") + d.id)) return;
	}
	std::string line;
	for (const auto& kv : result) line += " " + kv.first + ":" + kv.second;
	say("powers:" + line);
	for (const char* id : { "rain", "bloom", "lightning", "quake", "plague", "meteor", "volcano", "heal", "inspire", "prophet" })
		eq(result[id], std::string("ok"), id);
	sim::advance(*w, 4 * 60);
	noNaN(*w, "60 years after every power");
	eq(sim::firstNonFinite(*w), std::string());
	ok(liveCount(*w) >= 1, "people survive the barrage");
}

// ------------------------------------------------------------------ the Save container on a long-lived world

DM_TEST("integration: save::encode / decode of a 400-year world with snapshots, then the game continues") {
	if (!allReal()) return;
	auto w = historyWorld(6, 5);
	sim::advance(*w, 4 * 400);
	std::vector<uint8_t> blob;
	std::string why;
	if (!ok(save::encode(*w, blob, {}, &why), "encode: " + why)) return;
	const std::optional<save::Meta> meta = save::peek(blob.data(), blob.size());
	if (!ok(meta.has_value(), "peek")) return;
	eq(meta->year, w->year, "meta year");
	eq(meta->age, w->age, "meta age");
	auto x = save::decode(blob.data(), blob.size(), &why);
	if (!ok(x != nullptr, "decode: " + why)) return;
	eq(x->snapshots.size(), w->snapshots.size(), "time-lapse frames kept");
	if (!x->snapshots.empty()) {
		ok(x->snapshots.back().owner == w->snapshots.back().owner, "last frame's owner map");
		ok(x->snapshots.back().biome == w->snapshots.back().biome, "last frame's biomes");
	}
	std::vector<uint8_t> raw;
	state::serialize(*w, raw);
	auto y = state::deserialize(raw);
	if (!ok(y != nullptr, "state::deserialize")) return;
	sim::use(*x);
	sim::use(*y);
	for (int i = 0; i < 4 * 100; i++) { sim::advance(*x, 1); sim::advance(*y, 1); }
	eq(sim::fingerprint(*x), sim::fingerprint(*y), "a Save container load continues exactly like a raw state load");
	noNaN(*x, "100 years after save::decode");
	say("save container " + std::to_string(blob.size()) + " bytes (" + std::to_string(w->snapshots.size()) + " frames) vs raw state " +
	    std::to_string(raw.size()) + " bytes");
}
