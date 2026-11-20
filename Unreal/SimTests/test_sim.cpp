// DEMIURGE — SimTests/test_sim.cpp : dm::sim (SPEC-SIM 3.12) and dm::balance; BUILD.md A14.
// No JS tests existed (js/tests had no sim.test.js): these are the A14 acceptance checks written
// fresh. In `run.ps1 sim` every other module is a stub; call-recording stubs (StubHooks) prove the
// tick tables and the scheduler from the call log. Hook-dependent tests return early when the
// collaborator is real (`run.ps1 all`); the rest hold for the real modules too.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Balance.h"
#include "Chronicle.h"
#include "Civ.h"
#include "Climate.h"
#include "Life.h"
#include "Powers.h"
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
std::string f3(double x) { char b[64]; std::snprintf(b, sizeof b, "%.3f", x); return b; }

bool allStubbed(std::initializer_list<const char*> mods) {
	for (const char* m : mods) if (!stub::stubbed(m)) return false;
	return true;
}

// The heavy passes, as hook names.
const std::vector<std::string> kHeavy = { "terrain_derive", "climate_update", "hydro_update", "life_classify", "life_habitability" };
bool isHeavy(const std::string& s) { return std::find(kHeavy.begin(), kHeavy.end(), s) != kHeavy.end(); }

// Hooks that behave like the real modules for the need flags (they clear what they recompute).
void installNeedHooks() {
	auto& h = stub::hooks();
	h.terrain_derive = [](World& w) { w.need.derive = false; w.need.life = true; };
	h.climate_update = [](World& w) { w.need.climate = false; w.need.life = true; };
	h.hydro_update = [](World& w) { w.need.hydro = false; w.need.life = true; };
	h.life_habitability = [](World& w) { w.need.life = false; };
}

// The call log split into ticks: a new segment starts at every "powers_update" (advance calls it
// once before each tick). Returns the segments in order.
std::vector<std::vector<std::string>> segments(const std::vector<std::string>& log) {
	std::vector<std::vector<std::string>> seg;
	for (const std::string& s : log) {
		if (s == "powers_update") { seg.emplace_back(); continue; }
		if (!seg.empty()) seg.back().push_back(s);
	}
	return seg;
}

std::string join(const std::vector<std::string>& v) {
	std::string s;
	for (size_t i = 0; i < v.size(); i++) s += (i ? "," : "") + v[i];
	return s;
}

} // namespace

// ------------------------------------------------------------------ constants and pure functions

DM_TEST("sim: constants and mood thresholds") {
	eq(sim::kTick, 0.25, "kTick");
	eq(sim::kRealPerTick, 0.125, "kRealPerTick");
	eq(sim::kTicksPerYear, 4, "kTicksPerYear");
	eq(int(sim::mood(0.35, 0.2)), int(Mood::Beloved), "0.35 / 0.2 beloved");
	eq(int(sim::mood(0.3499, 0.2)), int(Mood::Parting), "just below devotion: parting");
	eq(int(sim::mood(0.35, 0.1999)), int(Mood::Parting), "just below love: parting");
	eq(int(sim::mood(0.9, -0.2)), int(Mood::Feared), "love -0.2 feared");
	eq(int(sim::mood(0.1, -0.5)), int(Mood::Feared), "feared before forgotten");
	eq(int(sim::mood(0.1999, 0)), int(Mood::Forgotten), "devotion < 0.2 forgotten");
	eq(int(sim::mood(0.2, -0.1999)), int(Mood::Parting), "0.2 / -0.1999 parting");
	eq(int(sim::mood(std::nan(""), std::nan(""))), int(Mood::Forgotten), "NaN treated as 0");
	eq(std::string(sim::toStr(sim::Hint::WaterMore)), std::string("water-more"));
	eq(std::string(sim::toStr(sim::Hint::LifeOk)), std::string("life-ok"));
}

// ------------------------------------------------------------------ phases

DM_TEST("sim: molten cools in exactly 60 ticks, then setPhase(Water) emits phase") {
	auto w = makeWorld({ .phase = Phase::Molten });
	w->heat = 1;
	int phaseEvents = 0;
	Phase got = Phase::Molten, prev = Phase::Ended;
	w->bus.on<EvPhase>([&](const EvPhase& e) { phaseEvents++; got = e.phase; prev = e.prev; });
	for (int i = 0; i < 59; i++) sim::advance(*w, 1);
	eq(int(w->phase), int(Phase::Molten), "still molten after 59 ticks");
	ok(w->heat > 0 && w->heat < 0.02, "heat after 59 ticks " + f3(w->heat));
	sim::advance(*w, 1);
	eq(int(w->phase), int(Phase::Water), "water after 60 ticks");
	eq(w->heat, 0.0, "heat 0");
	eq(phaseEvents, 1, "one phase event");
	eq(int(got), int(Phase::Water));
	eq(int(prev), int(Phase::Molten));
	eq(w->tick, int64_t(60), "tick counts every tick");
	eq(w->year, 0.0, "year does not advance before history");
	noNaN(*w);
}

DM_TEST("sim: setPhase entry actions, forward only, emits phase then forces a snapshot") {
	stub::Guard g(true);
	const bool hooked = allStubbed({ "chronicle", "climate" });
	auto w = makeWorld({ .phase = Phase::Molten });
	w->heat = 0.7;
	std::vector<std::string> order;
	w->bus.on<EvPhase>([&](const EvPhase& e) { order.push_back(std::string("phase:") + toStr(e.phase)); });
	if (hooked) {
		stub::hooks().chronicle_snapshot = [&](World& ww, bool force) -> bool {
			order.push_back(std::string("snapshot:") + (force ? "forced" : "due") + ":" + toStr(ww.phase));
			return true;
		};
		stub::hooks().climate_calibrate = [&](World& ww) { order.push_back("calibrate"); ww.vars.seaBase = ww.seaLevel; };
	}
	ok(!sim::setPhase(*w, Phase::Molten), "same phase refused");
	ok(sim::setPhase(*w, Phase::Water), "molten -> water");
	eq(w->heat, 0.0, "water: heat 0");
	ok(!sim::setPhase(*w, Phase::Molten), "backwards refused");
	eq(int(w->phase), int(Phase::Water));
	w->vars.lifeBoost = 3;
	ok(sim::setPhase(*w, Phase::Life), "water -> life");
	eq(w->vars.lifeBoost, w->tune<SimTune>().lifeBoost, "life: lifeBoost = SimTune.lifeBoost (25)");
	eq(w->vars.lifeBoost, 25.0);
	ok(sim::setPhase(*w, Phase::People), "life -> people");
	eq(w->vars.lifeBoost, 1.0, "people: lifeBoost 1");
	w->year = 123;
	w->tick = 777;
	w->vars.lifeBoost = 9;
	ok(sim::setPhase(*w, Phase::History), "people -> history");
	eq(w->year, 0.0, "history: year 0");
	eq(w->vars.t0, int64_t(777), "history: t0 = tick");
	eq(w->vars.lifeBoost, 1.0, "history: lifeBoost 1");
	ok(sim::setPhase(*w, Phase::Ended), "history -> ended");
	ok(!sim::setPhase(*w, Phase::History), "ended -> history refused");
	if (hooked) {
		eq(stub::called("climate_calibrate"), 1, "calibrate once (history entry)");
		const std::vector<std::string> want = {
			"phase:water", "snapshot:forced:water", "phase:life", "snapshot:forced:life", "phase:people", "snapshot:forced:people",
			"calibrate", "phase:history", "snapshot:forced:history", "phase:ended", "snapshot:forced:ended" };
		eq(join(order), join(want), "entry action -> phase event -> forced snapshot");
	}
}

DM_TEST("sim: setPhase is the only writer of w.phase (source scan)") {
	namespace fs = std::filesystem;
	const fs::path dir = fs::path(projectRoot()) / "Unreal" / "Source" / "Demiurge" / "Sim";
	const std::regex writeRe(R"(\b(w|world|wp|ww)\s*(\.|->)\s*phase\s*=[^=])");
	int inSim = 0;
	std::vector<std::string> others;
	for (const auto& ent : fs::directory_iterator(dir)) {
		if (ent.path().extension() != ".cpp") continue;
		const std::string base = ent.path().filename().string();
		std::ifstream in(ent.path());
		std::string line;
		int ln = 0;
		while (std::getline(in, line)) {
			ln++;
			const std::string code = line.substr(0, line.find("//"));
			if (!std::regex_search(code, writeRe)) continue;
			if (base == "Sim.cpp") inSim++;
			else if (base != "State.cpp") others.push_back(base + ":" + std::to_string(ln));   // State: deserialize
		}
	}
	eq(inSim, 1, "Sim.cpp writes w.phase exactly once (in setPhase)");
	for (const std::string& o : others) say("warning: another module writes w.phase: " + o);
}

// ------------------------------------------------------------------ the tick tables

DM_TEST("sim: tick tables for water, life and people (call log)") {
	stub::Guard g(true);
	if (!allStubbed({ "disasters", "life", "climate", "terrain", "hydro", "units", "economy" })) return;
	installNeedHooks();
	std::vector<std::pair<int, int>> steps;
	stub::hooks().life_step = [&](World& w, int part, int parts) { (void)w; steps.push_back({ part, parts }); };
	for (Phase ph : { Phase::Water, Phase::Life, Phase::People }) {
		auto w = makeWorld({ .phase = ph });
		w->need = Need{};
		w->need.derive = w->need.climate = w->need.hydro = w->need.life = false;
		w->tick = 8;                        // the next tick is 9, then 10 (stepIce)
		stub::calls().clear();
		steps.clear();
		sim::advance(*w, 2);
		const auto seg = segments(stub::calls());
		if (!eq(seg.size(), size_t(2), "two ticks")) return;
		const int P = ph == Phase::Life ? 2 : 8;
		// tick 9: no stepIce, and climate is overdue (no rederive: last = never) -> one heavy pass
		// (climate_update, which sets need.life); tick 10: stepIce, then the classify half of the life pass
		eq(join(seg[0]), std::string("powers_tick,life_burn,life_step,climate_update"), std::string(toStr(ph)) + " tick 9");
		eq(join(seg[1]), std::string("powers_tick,life_burn,life_step,climate_stepIce,life_classify"), std::string(toStr(ph)) + " tick 10 (ice)");
		if (eq(steps.size(), size_t(2))) {
			eq(steps[0].first, 9 % P, "part = tick % P");
			eq(steps[0].second, P, "parts");
			eq(steps[1].first, 10 % P);
		}
		eq(w->year, 0.0, "no years in genesis");
		ok(stub::called("units_step") == 0 && stub::called("civ_yearly") == 0 && stub::called("powers_natural") == 0 &&
		   stub::called("climate_yearly") == 0, "no units, civ, climate.yearly, natural events in genesis");
	}
}

DM_TEST("sim: tick table for history and the yearly group (call log)") {
	stub::Guard g(true);
	if (!allStubbed({ "disasters", "life", "climate", "terrain", "hydro", "units", "economy", "society", "chronicle" })) return;
	installNeedHooks();
	double unitDt = -1;
	bool snapForced = true;
	stub::hooks().units_step = [&](World& w, double dt) { (void)w; unitDt = dt; };
	stub::hooks().chronicle_snapshot = [&](World& w, bool force) -> bool { (void)w; snapForced = force; return false; };
	auto w = makeWorld({ .phase = Phase::History });
	w->vars.t0 = 6;
	w->tick = 6;
	w->year = 0;
	sim::rederive(*w);                      // fresh scheduler: nothing due for a while
	w->need.derive = w->need.climate = w->need.hydro = w->need.life = false;
	stub::calls().clear();
	sim::advance(*w, 4);                    // ticks 7, 8, 9, 10 (10 = yearly AND stepIce)
	const auto seg = segments(stub::calls());
	if (!eq(seg.size(), size_t(4))) return;
	const std::string base = "powers_tick,life_burn,units_step,life_step";
	eq(join(seg[0]), base, "tick 7");
	eq(join(seg[1]), base, "tick 8");
	eq(join(seg[2]), base, "tick 9");
	eq(join(seg[3]), base + ",civ_yearly,society_yearly,climate_yearly,powers_natural,chronicle_snapshot,climate_stepIce",
		"tick 10: yearly group then stepIce, no heavy pass");
	eq(unitDt, 0.25, "units::step(kTick)");
	ok(!snapForced, "yearly snapshot is not forced");
	eq(w->year, 1.0, "year 1 after 4 history ticks");
	ok(!sim::isYearlyTick(*w), "tick 11 is not yearly");
	sim::advance(*w, 3);
	ok(sim::isYearlyTick(*w), "tick 14 is yearly");
	// Ended keeps ticking like history
	sim::setPhase(*w, Phase::Ended);
	stub::calls().clear();
	sim::advance(*w, 1);
	const auto seg2 = segments(stub::calls());
	ok(seg2.size() == 1 && std::count(seg2[0].begin(), seg2[0].end(), "civ_yearly") == 1, "ended: yearly group still runs");
}

DM_TEST("sim: advance calls powers::update(kRealPerTick) before each tick") {
	stub::Guard g(true);
	if (!stub::stubbed("disasters")) return;
	auto w = makeWorld({ .phase = Phase::History });
	std::vector<std::string> order;
	stub::hooks().powers_update = [&](World& ww, double dt) { order.push_back("update:" + std::to_string(ww.tick) + ":" + f3(dt)); };
	stub::hooks().powers_tick = [&](World& ww) { order.push_back("tick:" + std::to_string(ww.tick)); };
	sim::advance(*w, 3);
	eq(join(order), std::string("update:0:0.125,tick:1,update:1:0.125,tick:2,update:2:0.125,tick:3"));
	order.clear();
	sim::tick(*w);
	eq(join(order), std::string("tick:4"), "tick never calls powers::update");
	order.clear();
	sim::advance(*w, 0);
	ok(order.empty(), "advance(0) does nothing");
}

DM_TEST("sim: heavy-pass scheduler: one per tick, never on yearly ticks, classify+habitability consecutive, spacing") {
	stub::Guard g(true);
	if (!allStubbed({ "terrain", "climate", "hydro", "life", "economy" })) return;
	for (Phase ph : { Phase::History, Phase::Water }) {
		// the previous iteration's hooks capture its (dead) locals: drop them before anything runs
		stub::hooks() = stub::Hooks{};
		installNeedHooks();
		auto w = makeWorld({ .phase = ph });
		w->vars.t0 = 3;
		w->tick = 3;
		sim::rederive(*w);
		std::map<int64_t, std::vector<std::string>> heavyAt;
		std::vector<int64_t> yearlyTicks;
		int badDerive = 0, badClimate = 0;
		int64_t lastClimateTick = w->tick;
		auto mark = [&](const char* nm) { heavyAt[w->tick].push_back(nm); };
		stub::hooks().terrain_derive = [&](World& ww) { if (!ww.need.derive) badDerive++; mark("derive"); ww.need.derive = false; ww.need.life = true; };
		stub::hooks().climate_update = [&](World& ww) {
			if (!ww.need.climate && ww.tick - lastClimateTick < 40) badClimate++;
			lastClimateTick = ww.tick;
			mark("climate"); ww.need.climate = false; ww.need.life = true;
		};
		stub::hooks().hydro_update = [&](World& ww) { mark("hydro"); ww.need.hydro = false; ww.need.life = true; };
		stub::hooks().life_classify = [&](World& ww) { (void)ww; mark("classify"); };
		stub::hooks().life_habitability = [&](World& ww) { mark("hab"); ww.need.life = false; };
		stub::hooks().civ_yearly = [&](World& ww) { yearlyTicks.push_back(ww.tick); };
		Rng r(99);
		const int N = 4000;
		for (int i = 0; i < N; i++) {
			if (r.chance(0.03)) w->need.derive = true;
			if (r.chance(0.15)) w->need.climate = true;
			if (r.chance(0.08)) w->need.hydro = true;
			if (r.chance(0.04)) w->need.life = true;
			sim::advance(*w, 1);
		}
		const std::string tag = std::string(toStr(ph)) + ": ";
		int multi = 0, onYearly = 0, pairBad = 0, climateClose = 0, hydroClose = 0;
		int64_t maxClimateGap = 0, prevC = -1, prevH = -1;
		std::map<std::string, int> counts;
		for (const auto& kv : heavyAt) {
			if (kv.second.size() > 1) multi++;
			if (std::find(yearlyTicks.begin(), yearlyTicks.end(), kv.first) != yearlyTicks.end()) onYearly++;
			for (const std::string& s : kv.second) {
				counts[s]++;
				if (s == "classify" && kv.first < w->tick) {   // (a classify on the very last tick has its pair after the run)
					auto nx = heavyAt.find(kv.first + 1);
					if (nx == heavyAt.end() || nx->second.empty() || nx->second[0] != "hab") pairBad++;
				}
				if (s == "hab") {
					auto pv = heavyAt.find(kv.first - 1);
					if (pv == heavyAt.end() || pv->second.empty() || pv->second[0] != "classify") pairBad++;
				}
				if (s == "climate") {
					if (prevC >= 0 && kv.first - prevC < 8) climateClose++;
					if (prevC >= 0) maxClimateGap = std::max(maxClimateGap, kv.first - prevC);
					prevC = kv.first;
				}
				if (s == "hydro") {
					if (prevH >= 0 && kv.first - prevH < 8) hydroClose++;
					prevH = kv.first;
				}
			}
		}
		say(tag + "heavy passes over " + std::to_string(N) + " ticks: derive " + std::to_string(counts["derive"]) + ", climate " +
			std::to_string(counts["climate"]) + ", hydro " + std::to_string(counts["hydro"]) + ", classify " + std::to_string(counts["classify"]) +
			", habitability " + std::to_string(counts["hab"]) + "; yearly ticks " + std::to_string(yearlyTicks.size()) +
			"; longest climate gap " + std::to_string(maxClimateGap));
		eq(multi, 0, tag + "ticks with two heavy passes");
		eq(onYearly, 0, tag + "heavy passes on yearly ticks");
		eq(pairBad, 0, tag + "classify / habitability not on consecutive ticks");
		eq(climateClose, 0, tag + "climate passes closer than 8 ticks");
		eq(hydroClose, 0, tag + "hydro passes closer than 8 ticks");
		eq(badDerive, 0, tag + "derive without need.derive");
		eq(badClimate, 0, tag + "climate without need.climate before 40 ticks");
		ok(maxClimateGap <= 48, tag + "climate at least every ~40 ticks (longest gap " + std::to_string(maxClimateGap) + ")");
		ok(counts["derive"] > 50 && counts["climate"] > 50 && counts["hydro"] > 50 && counts["classify"] > 50, tag + "every pass gets served");
		if (ph == Phase::History) eq(yearlyTicks.size(), size_t(N / 4), tag + "one yearly group per 4 ticks");
		else eq(yearlyTicks.size(), size_t(0), tag + "no yearly group in genesis");
	}
}

DM_TEST("sim: heavy pass waits for its gaps and runs when only it is due") {
	stub::Guard g(true);
	if (!allStubbed({ "terrain", "climate", "hydro", "life" })) return;
	installNeedHooks();
	auto w = makeWorld({ .phase = Phase::Water });
	sim::rederive(*w);                      // climate + hydro just ran (tick 0)
	w->need.derive = w->need.climate = w->need.life = false;
	w->need.hydro = true;
	stub::calls().clear();
	sim::advance(*w, 7);
	eq(stub::called("hydro_update"), 0, "hydro waits 8 ticks after the last");
	sim::advance(*w, 1);
	eq(stub::called("hydro_update"), 1, "hydro on tick 8");
	// with nothing needed, climate still runs 40 ticks after the last (rederive restarts both clocks)
	sim::rederive(*w);
	w->need = Need{};
	w->need.derive = w->need.climate = w->need.hydro = w->need.life = false;
	stub::calls().clear();
	sim::advance(*w, 39);
	eq(stub::called("climate_update"), 0, "no climate before 40 ticks");
	sim::advance(*w, 1);
	eq(stub::called("climate_update"), 1, "climate at 40 ticks regardless");
	// derive beats everything
	w->need.derive = true;
	w->need.climate = true;
	w->need.hydro = true;
	stub::calls().clear();
	sim::advance(*w, 1);
	eq(stub::called("terrain_derive"), 1, "derive first");
	eq(stub::called("climate_update") + stub::called("hydro_update"), 0, "and nothing else on that tick");
}

// ------------------------------------------------------------------ rederive, use, create

DM_TEST("sim: rederive and use call their steps in order") {
	stub::Guard g(true);
	if (!allStubbed({ "terrain", "climate", "hydro", "life", "civ", "chronicle" })) return;
	auto w = makeWorld({ .phase = Phase::Water });
	stub::calls().clear();
	sim::rederive(*w);
	eq(join(stub::calls()), std::string("terrain_derive,climate_update,hydro_update,life_classify,life_habitability,life_rebuild,civ_rebuild"));
	auto h = makeWorld({ .phase = Phase::History });
	h->vars.seaBase.reset();
	stub::calls().clear();
	sim::rederive(*h);
	eq(join(stub::calls()), std::string("climate_calibrate,terrain_derive,climate_update,hydro_update,life_classify,life_habitability,life_rebuild,civ_rebuild"),
		"history without seaBase calibrates first");
	h->vars.seaBase = 0.1;
	stub::calls().clear();
	sim::use(*h);
	eq(join(stub::calls()), std::string("chronicle_attach,terrain_derive,climate_update,hydro_update,life_classify,life_habitability,life_rebuild,civ_rebuild"),
		"use: (balance::apply, real here) chronicle::attach, then rederive");
}

DM_TEST("sim: create generates terrain in phase molten and ends with use") {
	stub::Guard g(true);
	const bool hooked = allStubbed({ "terrain", "names", "chronicle", "climate", "hydro", "life", "civ" });
	auto w = sim::create({ .seed = 42, .name = "", .L = 4, .settings = { 0.5 } });
	if (!ok(w != nullptr)) return;
	eq(int(w->phase), int(Phase::Molten), "molten");
	eq(w->heat, 1.0, "heat 1");
	eq(w->seed, 42u);
	eq(w->settings.calamity, 0.5, "settings copied");
	ok(!w->name.empty() && w->name != "Unnamed", "named by names::planet: " + w->name);
	if (hooked) {
		eq(join(stub::calls()), std::string("names_planet,terrain_generate,chronicle_attach,terrain_derive,climate_update,hydro_update,"
			"life_classify,life_habitability,life_rebuild,civ_rebuild"));
	}
	auto named = sim::create({ .seed = 42, .name = "Ys", .L = 4 });
	eq(named->name, std::string("Ys"), "explicit name kept");
	eq(named->rng.s, w->rng.s, "naming never draws from w.rng");
	noNaN(*w);
}

// ------------------------------------------------------------------ genesis

DM_TEST("sim: genesis progress values and hint keys") {
	stub::Guard g(true);
	const bool coverHook = stub::stubbed("life");
	double veg = 0;
	if (coverHook) stub::hooks().life_cover = [&](const World&) { life::Cover c; c.veg = veg; c.land = 0.4; return c; };
	auto w = makeWorld({ .phase = Phase::Molten });
	w->heat = 0.4;
	auto p = sim::genesis::progress(*w);
	near(p.value, 0.6, 1e-12, "molten value 1 - heat");
	eq(int(p.hint), int(sim::Hint::Cooling));
	ok(!p.canAdvance, "molten cannot be advanced");
	near(p.ocean, 0.6, 0.02, "ocean share (helper world: 40 % land)");
	sim::setPhase(*w, Phase::Water);
	w->seaLevel = -5;
	p = sim::genesis::progress(*w);
	near(p.value, 0.5, 1e-12, "water value sat((sea + 6.5) / 3)");
	eq(int(p.hint), int(sim::Hint::WaterMore));
	ok(!p.canAdvance);
	w->seaLevel = -3.5;
	p = sim::genesis::progress(*w);
	near(p.value, 1.0, 1e-12);
	eq(int(p.hint), int(sim::Hint::WaterOk), "-3.5 is enough");
	ok(p.canAdvance);
	w->seaLevel = -8;
	eq(sim::genesis::progress(*w).value, 0.0, "clamped at 0");
	if (coverHook) {
		w->seaLevel = 0;
		sim::setPhase(*w, Phase::Life);
		veg = 0.1;
		p = sim::genesis::progress(*w);
		near(p.value, 0.4, 1e-12, "life value veg / 0.25");
		eq(int(p.hint), int(sim::Hint::LifeMore));
		near(p.veg, 0.1, 1e-12, "stats.veg");
		veg = 0.25;
		p = sim::genesis::progress(*w);
		eq(int(p.hint), int(sim::Hint::LifeOk));
		ok(p.canAdvance);
	}
	sim::setPhase(*w, Phase::People);
	p = sim::genesis::progress(*w);
	eq(p.value, 0.0, "people value 0");
	eq(int(p.hint), int(sim::Hint::People));
	ok(!p.canAdvance, "only an awakening leaves people");
}

DM_TEST("sim: genesis next gates") {
	stub::Guard g(true);
	if (!stub::stubbed("life")) return;
	double veg = 0;
	stub::hooks().life_cover = [&](const World&) { life::Cover c; c.veg = veg; return c; };
	auto w = makeWorld({ .phase = Phase::Molten });
	w->heat = 0.5;
	ok(!sim::genesis::next(*w), "molten: no");
	sim::setPhase(*w, Phase::Water);
	w->seaLevel = -3.6;
	ok(!sim::genesis::next(*w), "water at -3.6: no");
	eq(int(w->phase), int(Phase::Water));
	w->seaLevel = -3.5;
	ok(sim::genesis::next(*w), "water at -3.5: yes");
	eq(int(w->phase), int(Phase::Life));
	eq(w->vars.lifeBoost, 25.0, "life boost on");
	veg = 0.249;
	ok(!sim::genesis::next(*w), "life at veg 0.249: no");
	veg = 0.25;
	ok(sim::genesis::next(*w), "life at 0.25: yes");
	eq(int(w->phase), int(Phase::People));
	eq(w->vars.lifeBoost, 1.0);
	ok(!sim::genesis::next(*w), "people: no (awaken)");
	eq(int(w->phase), int(Phase::People));
}

DM_TEST("sim: genesis skip (stubbed collaborators) -> history, one settlement, calibrated, Awe 60") {
	stub::Guard g(true);
	if (!allStubbed({ "civ", "society", "climate", "powers", "life" })) return;
	auto w = makeWorld({ .phase = Phase::Molten });
	w->heat = 1;
	w->vars.seaBase.reset();
	const int site = bestHab(*w);
	stub::hooks().climate_calibrate = [](World& ww) { ww.vars.seaBase = ww.seaLevel; ww.vars.iceRef = 50; };
	stub::hooks().civ_bestSite = [site](const World&) { return site; };
	stub::hooks().civ_canSettle = [](const World& ww, int c) { civ::SettleCheck k; k.ok = !state::isSea(ww, c); k.reason = k.ok ? civ::SettleReason::Ok : civ::SettleReason::Sea; return k; };
	stub::hooks().society_newCulture = [](World& ww, const society::NewCultureOpts& o) -> Culture* {
		Culture& c = state::newCulture(ww, o.parent);
		c.name = "Folk";
		return &c;
	};
	stub::hooks().civ_found = [](World& ww, int cell, int cu, double pop, FoundedBy, const civ::FoundOpts&) -> Settlement* {
		Settlement& s = state::newSettlement(ww, cell, cu);
		s.pop = pop;
		s.devotion = 0.6;
		ww.owner[size_t(cell)] = int16_t(s.id);
		return &s;
	};
	std::vector<std::string> phases;
	w->bus.on<EvPhase>([&](const EvPhase& e) { phases.push_back(toStr(e.phase)); });
	ok(sim::genesis::skip(*w, { .sea = 0.25, .cell = -1 }), "skip returns true");
	eq(int(w->phase), int(Phase::History), "history");
	eq(w->heat, 0.0);
	eq(w->seaLevel, 0.25, "sea set");
	ok(w->vars.seaBase.has_value() && *w->vars.seaBase == 0.25, "calibrated: seaBase = sea");
	int live = 0;
	for (const Settlement& s : w->settlements) if (!s.dead) live++;
	eq(live, 1, "one settlement");
	if (!w->settlements.empty()) eq(w->settlements[0].cell, site, "at civ::bestSite");
	eq(w->god.awe, 60.0, "Awe 60");
	eq(w->stats.pop, 30.0, "refreshStats ran (30 people)");
	eq(join(phases), std::string("people,history"));
	ok(stub::called("life_fill") == 1, "life::fill once");
	ok(stub::called("powers_use") >= 1, "tried powers::use(awaken) first, like a player");
	ok(!sim::genesis::skip(*w, {}), "skip again refused in history");
	// sea level clamps; a NaN sea means 0
	auto w2 = makeWorld({ .phase = Phase::Water });
	sim::genesis::skip(*w2, { .sea = 99 });
	eq(w2->seaLevel, 1.5, "sea clamped to +1.5");
	auto w3 = makeWorld({ .phase = Phase::Water });
	sim::genesis::skip(*w3, { .sea = std::nan("") });
	eq(w3->seaLevel, 0.0, "NaN sea -> 0");
	eq(int(w3->phase), int(Phase::History), "NaN sea: still reaches history");
	noNaN(*w);
}

DM_TEST("sim: genesis skip reports false (but reaches history) when nobody can wake") {
	stub::Guard g(true);
	if (!allStubbed({ "civ", "society", "powers" })) return;
	auto w = makeWorld({ .phase = Phase::Water });           // the neutral stubs found nothing and wake nobody
	ok(!sim::genesis::skip(*w, {}), "false: nobody woke");
	eq(int(w->phase), int(Phase::History), "history anyway (awaken is free there)");
	eq(w->god.awe, 0.0, "no starting Awe without a people");
}

DM_TEST("sim: genesis skip with the real modules (all)") {
	if (stub::stubbed("civ") || stub::stubbed("powers") || stub::stubbed("society") || stub::stubbed("terrain")) return;
	const double t0 = nowMs();
	auto w = sim::create({ .seed = 3, .name = "", .L = 6 });
	const double t1 = nowMs();
	ok(sim::genesis::skip(*w, {}), "skip");
	const double t2 = nowMs();
	say("L6 create " + f3(t1 - t0) + " ms, skip " + f3(t2 - t1) + " ms (budget 800 together)");
	ok(t2 - t0 < 4000, "create + skip well inside 5x budget");
	eq(int(w->phase), int(Phase::History));
	int live = 0;
	for (const Settlement& s : w->settlements) if (!s.dead) live++;
	eq(live, 2, "two camps (v3, design review: an awakening wakes two bands)");
	ok(w->god.awe >= 60, "Awe >= 60");
	ok(w->vars.seaBase.has_value(), "calibrated");
	noNaN(*w);
	sim::advance(*w, 400);
	noNaN(*w, "after 100 years");
	eq(sim::firstNonFinite(*w), std::string());
}

// ------------------------------------------------------------------ stats, endings, score

DM_TEST("sim: refreshStats sums live settlements") {
	auto w = makeWorld();
	Culture& cu = state::newCulture(*w);
	const int cid = cu.id;
	const int land = findLand(*w);
	Settlement& a = state::newSettlement(*w, land, cid);
	a.pop = 100; a.devotion = 0.5;
	Settlement& b = state::newSettlement(*w, land, cid);
	b.pop = 50; b.devotion = 1;
	Settlement& c = state::newSettlement(*w, land, cid);
	c.pop = 999; c.dead = true;
	w->stats.peakPop = 120;
	sim::refreshStats(*w);
	eq(w->stats.pop, 150.0);
	eq(w->stats.believers, 100.0, "sum pop x devotion");
	eq(w->stats.peakPop, 150.0, "peak rises");
	w->settlements[0].pop = 10;
	sim::refreshStats(*w);
	eq(w->stats.peakPop, 150.0, "peak never falls");
	eq(w->stats.pop, 60.0);
}

// (v3, design review) the Ark mood is the verdict of the Ark culture's memory: society::memory's mood
// (its rules are tested in test_society / test_integration_review)
DM_TEST("sim: ark ending moods at each threshold, then no second ending") {
	stub::Guard g(true);
	if (!stub::stubbed("society")) return;
	struct Case { double dev, love; Mood want; };
	const Case cases[] = { { 0.35, 0.2, Mood::Beloved }, { 0.34, 0.5, Mood::Parting }, { 0.6, -0.2, Mood::Feared },
		{ 0.19, 0.0, Mood::Forgotten }, { 0.25, 0.19, Mood::Parting }, { 0.1, -0.3, Mood::Feared } };
	for (const Case& k : cases) {
		auto w = makeWorld();
		Culture& cu = state::newCulture(*w);
		const int cid = cu.id;
		Settlement& s = state::newSettlement(*w, findLand(*w), cid);
		s.pop = 5000;
		const int sid = s.id;
		w->year = 3456;
		w->vars.ark = ArkRecord{ cid, sid, 3456 };
		int regardFor = -9;
		stub::hooks().society_memory = [&](const World&, int c) { regardFor = c; society::Memory m; m.devotion = k.dev; m.love = k.love; m.mood = k.want; return m; };
		std::vector<EvEnding> ends;
		w->bus.on<EvEnding>([&](const EvEnding& e) { ends.push_back(e); });
		const auto e = sim::checkEnding(*w);
		const std::string tag = "dev " + f3(k.dev) + " love " + f3(k.love);
		if (!ok(e.has_value(), tag)) continue;
		eq(int(e->type), int(EndingType::Ark), tag);
		eq(int(e->mood), int(k.want), tag + " mood");
		eq(e->culture, cid);
		eq(e->settlement, sid);
		eq(e->year, 3456.0);
		eq(regardFor, cid, "memory of the ark's culture");
		ok(w->ending.has_value() && *w->ending == *e, "w.ending written");
		eq(int(w->phase), int(Phase::Ended), "phase ended");
		eq(ends.size(), size_t(1), "ending emitted once");
		if (!ends.empty()) eq(int(ends[0].mood), int(k.want), "event carries the ending");
		ok(!sim::checkEnding(*w).has_value(), "no second ending");
		sim::advance(*w, 40);
		eq(ends.size(), size_t(1), "still one ending after 10 more years");
	}
}

DM_TEST("sim: silence ending (quiet / ash) after 50 empty years, people:lost below 1,000 peak") {
	for (int ashCase = 0; ashCase < 2; ashCase++) {
		auto w = makeWorld();
		w->stats.peakPop = 5000;
		w->year = 1000;
		if (ashCase) w->vars.lastNuke = 900;
		auto evs = capture(*w, [&] { ok(!sim::checkEnding(*w).has_value(), "empty: clock starts"); });
		eq(evs.size(), size_t(0), "no people:lost above 1,000 peak");
		w->year = 1049.75;
		ok(!sim::checkEnding(*w).has_value(), "49.75 years: not yet");
		w->year = 1050;
		const auto e = sim::checkEnding(*w);
		if (!ok(e.has_value(), "silence at 50 years")) continue;
		eq(int(e->type), int(EndingType::Silence));
		eq(int(e->mood), int(ashCase ? Mood::Ash : Mood::Quiet), ashCase ? "nuke 150 years ago: ash" : "quiet");
		eq(e->culture, -1);
		eq(int(w->phase), int(Phase::Ended));
	}
	{   // a nuke more than 200 years ago: quiet
		auto w = makeWorld();
		w->stats.peakPop = 5000;
		w->vars.lastNuke = 1100;
		w->year = 1400;
		sim::checkEnding(*w);
		w->year = 1450;
		const auto e = sim::checkEnding(*w);
		ok(e.has_value() && e->mood == Mood::Quiet, "old nuke: quiet");
	}
	{   // a settler unit alive counts as people; revival resets the clock
		auto w = makeWorld();
		w->stats.peakPop = 5000;
		w->year = 1010;
		sim::checkEnding(*w);
		Unit& u = state::newUnit(*w, UnitType::Settler);
		u.size = 20;
		w->year = 1100;
		ok(!sim::checkEnding(*w).has_value(), "settler alive: no silence");
		state::eraseUnit(*w, u.id);
		w->year = 1120;
		ok(!sim::checkEnding(*w).has_value(), "clock restarted at 1120");
		w->year = 1169;
		ok(!sim::checkEnding(*w).has_value(), "49 years");
		w->year = 1170;
		ok(sim::checkEnding(*w).has_value(), "50 years after the settler vanished");
	}
	{   // (v3, design review finding 10) no silence before year 1,000: people:lost instead, then silence
		auto w = makeWorld();
		w->stats.peakPop = 5000;
		int lost = 0;
		w->bus.on<EvPeopleLost>([&](const EvPeopleLost&) { lost++; });
		w->year = 400;
		sim::checkEnding(*w);
		w->year = 600;
		ok(!sim::checkEnding(*w).has_value(), "200 empty years before 1,000: no silence");
		eq(lost, 1, "people:lost once instead");
		w->year = 1000;
		const auto e = sim::checkEnding(*w);
		ok(e.has_value() && e->type == EndingType::Silence, "silence at year 1,000");
	}
	{   // below 1,000 peak: people:lost once per extinction, never an ending
		auto w = makeWorld();
		w->stats.peakPop = 400;
		int lost = 0;
		w->bus.on<EvPeopleLost>([&](const EvPeopleLost&) { lost++; });
		for (int y = 0; y < 300; y += 10) { w->year = y; ok(!sim::checkEnding(*w).has_value(), "no ending below 1,000"); }
		eq(lost, 1, "people:lost once");
		Culture& cu = state::newCulture(*w);
		Settlement& s = state::newSettlement(*w, findLand(*w), cu.id);
		s.pop = 30;
		sim::checkEnding(*w);
		w->settlements[0].dead = true;
		for (int y = 300; y < 400; y += 10) { w->year = y; sim::checkEnding(*w); }
		eq(lost, 2, "again after the next extinction");
		eq(int(w->phase), int(Phase::History), "still history");
	}
	{   // only in history
		auto w = makeWorld({ .phase = Phase::People });
		w->stats.peakPop = 5000;
		w->year = 500;
		ok(!sim::checkEnding(*w).has_value(), "no ending in genesis");
	}
}

DM_TEST("sim: score parts and ending multiplier") {
	auto w = makeWorld();
	w->stats.peakPop = 999999;               // log10(1e6) = 6 -> 600
	w->age = 3;                              // 450
	Culture& a = state::newCulture(*w);
	const int aid = a.id;
	state::newCulture(*w).dead = true;
	Settlement& s = state::newSettlement(*w, findLand(*w), aid);
	s.b[Building::WONDER] = 1;
	w->year = 1234;                          // 123
	const sim::Score s1 = sim::score(*w);
	eq(s1.parts.at("pop"), 600.0);
	eq(s1.parts.at("ages"), 450.0);
	eq(s1.parts.at("cultures"), 60.0);
	eq(s1.parts.at("wonders"), 120.0);
	eq(s1.parts.at("years"), 123.0);
	const double base = s1.parts.at("base");
	eq(s1.total, base, "no ending: x1");
	w->ending = Ending{ EndingType::Ark, Mood::Beloved, aid, 0, 1234 };
	const sim::Score s2 = sim::score(*w);
	eq(s2.total, std::round(base * 2), "beloved x2");
	eq(s2.parts.at("ending"), s2.total - base);
	w->ending->mood = Mood::Ash;
	eq(sim::score(*w).total, std::round(base * 0.4), "ash x0.4");
	ok(std::isfinite(sim::score(*makeWorld()).total), "empty world scores");
}

// ------------------------------------------------------------------ determinism and runner hooks

DM_TEST("sim: two worlds with the same seed advanced alternately end with equal hashes") {
	auto a = sim::create({ .seed = 11, .name = "", .L = 4 });
	auto b = sim::create({ .seed = 11, .name = "", .L = 4 });
	eq(sim::fingerprint(*a), sim::fingerprint(*b), "after create");
	sim::genesis::skip(*a, {});
	sim::genesis::skip(*b, {});
	for (int i = 0; i < 300; i++) { sim::advance(*a, 1); sim::advance(*b, 1); }
	eq(sim::fingerprint(*a), sim::fingerprint(*b), "fingerprint after 75 years, interleaved");
	eq(hash(*a), hash(*b), "dmt::hash agrees");
	eq(a->tick, b->tick);
	eq(a->year, b->year);
	auto c = sim::create({ .seed = 12, .name = "", .L = 4 });
	sim::genesis::skip(*c, {});
	sim::advance(*c, 300);
	if (!stub::stubbed("terrain")) ok(sim::fingerprint(*c) != sim::fingerprint(*a), "another seed differs");
	say("fingerprints: seed 11 " + std::to_string(sim::fingerprint(*a)) + ", seed 12 " + std::to_string(sim::fingerprint(*c)));
	noNaN(*a);
}

DM_TEST("sim: no active world: each world's bus only hears its own events") {
	auto a = makeWorld({ .phase = Phase::Molten });
	auto b = makeWorld({ .phase = Phase::Molten });
	a->heat = 1;
	b->heat = 1;
	int na = 0, nb = 0;
	a->bus.onAll([&](const Event&) { na++; });
	b->bus.onAll([&](const Event&) { nb++; });
	for (int i = 0; i < 60; i++) sim::advance(*a, 1);
	ok(na >= 1, "a heard its phase change (" + std::to_string(na) + " events)");
	eq(nb, 0, "b heard nothing while a advanced");
	eq(int(b->phase), int(Phase::Molten), "b untouched");
	eq(b->tick, int64_t(0));
	for (int i = 0; i < 60; i++) sim::advance(*b, 1);
	ok(nb >= 1, "b hears its own");
}

DM_TEST("sim: a saved and loaded world continues deterministically after sim::use") {
	auto w = sim::create({ .seed = 21, .name = "", .L = 4 });
	sim::genesis::skip(*w, {});
	sim::advance(*w, 200);
	std::vector<uint8_t> bytes;
	std::string why;
	if (!ok(state::serialize(*w, bytes, &why), "serialize: " + why)) return;
	auto x = state::deserialize(bytes, &why);
	auto y = state::deserialize(bytes, &why);
	if (!ok(x && y, "deserialize: " + why)) return;
	std::vector<uint8_t> again;
	state::serialize(*x, again);
	ok(again == bytes, "re-saving a loaded world reproduces the bytes");
	sim::use(*x);
	sim::use(*y);
	dmt::watch(*x);
	eq(int(x->phase), int(w->phase), "phase kept");
	eq(x->year, w->year, "year kept");
	eq(x->vars.t0, w->vars.t0, "t0 kept (yearly ticks stay aligned)");
	eq(sim::isYearlyTick(*x), sim::isYearlyTick(*w), "yearly alignment kept");
	for (int i = 0; i < 200; i++) { sim::advance(*x, 1); sim::advance(*y, 1); }
	eq(sim::fingerprint(*x), sim::fingerprint(*y), "two loaded copies stay identical");
	eq(x->year, w->year + 50, "50 more years");
	noNaN(*x, "after load + 50 years");
	eq(sim::firstNonFinite(*x), std::string());
}

DM_TEST("sim: fingerprint sees every change; firstNonFinite finds NaNs") {
	auto w = makeWorld();
	const uint32_t f0 = sim::fingerprint(*w);
	eq(sim::fingerprint(*w), f0, "stable");
	w->temp[5] += 0.5f;                     // a derived field
	ok(sim::fingerprint(*w) != f0, "derived field changes it");
	w->temp[5] -= 0.5f;
	w->year += 0.25;
	ok(sim::fingerprint(*w) != f0, "year changes it");
	eq(sim::firstNonFinite(*w), std::string(), "clean world");
	w->moist[7] = std::nanf("");
	eq(sim::firstNonFinite(*w), std::string("moist[7]"));
	w->moist[7] = 0;
	w->god.awe = std::numeric_limits<double>::infinity();
	eq(sim::firstNonFinite(*w), std::string("god.awe"));
	w->god.awe = 0;
	Culture& cu = state::newCulture(*w);
	Settlement& s = state::newSettlement(*w, findLand(*w), cu.id);
	s.love = std::nan("");
	eq(sim::firstNonFinite(*w), std::string("settlement 0"));
	w->settlements[0].love = 0;
	w->vars.num["x_y"] = std::nan("");
	eq(sim::firstNonFinite(*w), std::string("vars"));
}

DM_TEST("sim: sample, isYearlyTick and runTicks hooks") {
	auto w = makeWorld({ .phase = Phase::History });
	w->vars.t0 = 0;
	w->tick = 0;
	int before = 0, after = 0, yearly = 0;
	sim::RunHooks hk;
	hk.before = [&](World&) { before++; };
	hk.after = [&](World&) { after++; };
	hk.yearly = [&](World& ww) { yearly++; ok(ww.tick % 4 == 0, "yearly hook after a yearly tick"); };
	const int64_t n = sim::runTicks(*w, 41, hk);
	eq(n, int64_t(41));
	eq(before, 41);
	eq(after, 41);
	eq(yearly, 10, "10 yearly ticks in 41");
	eq(w->year, 10.25);
	const sim::Sample s = sim::sample(*w);
	eq(s.year, 10.25);
	eq(s.tick, int64_t(41));
	eq(int(s.phase), int(Phase::History));
	w->ending = Ending{};
	eq(sim::runTicks(*w, 10, {}, true), int64_t(0), "stopAtEnding");
	eq(sim::runTicks(*w, 3, {}, false), int64_t(3), "keeps going otherwise");
	auto m = makeWorld({ .phase = Phase::Molten });
	ok(!sim::isYearlyTick(*m), "never in genesis");
}

// ------------------------------------------------------------------ balance

DM_TEST("balance: set by key, keys sorted, apply idempotent") {
	auto w = makeWorld();
	const std::vector<std::string> k = balance::keys();
	ok(k.size() >= 50, "keys registered: " + std::to_string(k.size()));
	ok(std::is_sorted(k.begin(), k.end()), "sorted");
	ok(std::adjacent_find(k.begin(), k.end()) == k.end(), "unique");
	ok(balance::set(*w, "economy.growthR", 0.032), "economy.growthR");
	eq(w->tune<EconomyTune>().growthR, 0.032);
	ok(balance::set(*w, "economy.ageNeed.2", 777), "array element");
	eq(w->tune<EconomyTune>().ageNeed[2], 777.0);
	ok(balance::set(*w, "society.maxCultures", 4.6), "int rounds");
	eq(w->tune<SocietyTune>().maxCultures, 5);
	ok(balance::set(*w, "sim.lifeBoost", 30), "sim key");
	eq(w->tune<SimTune>().lifeBoost, 30.0);
	ok(!balance::set(*w, "economy.nope", 1), "unknown key");
	ok(!balance::set(*w, "economy.growthR", std::nan("")), "NaN refused");
	eq(w->tune<EconomyTune>().growthR, 0.032, "unchanged by a refused set");
	for (const std::string& key : k) ok(balance::set(*w, key, 1), "every listed key is settable: " + key);
	auto a = makeWorld(), b = makeWorld();
	balance::apply(*a);
	balance::apply(*b);
	balance::apply(*b);
	eq(a->tune<EconomyTune>().growthR, b->tune<EconomyTune>().growthR, "idempotent");
}

// ------------------------------------------------------------------ robustness

DM_TEST("sim: every phase ticks on degenerate worlds without NaN") {
	for (double land : { 0.0, 1.0 }) {
		for (Phase ph : { Phase::Molten, Phase::Water, Phase::Life, Phase::People, Phase::History, Phase::Ended }) {
			auto w = makeWorld({ .L = 3, .land = land, .phase = ph });
			sim::advance(*w, 90);
			sim::genesis::progress(*w);
			sim::score(*w);
			sim::checkEnding(*w);
			noNaN(*w, std::string(toStr(ph)) + " land " + f3(land));
			eq(sim::firstNonFinite(*w), std::string());
		}
	}
}
