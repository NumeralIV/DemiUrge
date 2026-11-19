// DEMIURGE — SimTests/test_sim_adv.cpp : adversarial verification of dm::sim (A14 verifier).
// Runs in `run.ps1 sim` (every other module a stub) and against every real module (`run.ps1 all`,
// `Tools/simrun/simrun.ps1 --tests`). Hook-dependent tests return early when a collaborator is real;
// tests marked "(real)" return early while the collaborators they need are stubs.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <string>
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
bool noneStubbed(std::initializer_list<const char*> mods) {
	for (const char* m : mods) if (stub::stubbed(m)) return false;
	return true;
}
int liveCount(const World& w) {
	int k = 0;
	for (const Settlement& s : w.settlements) if (!s.dead) k++;
	return k;
}

// Every cell array of w as raw bytes (for "nobody else's state was touched").
std::vector<std::vector<uint8_t>> cellBytes(const World& w) {
	std::vector<std::vector<uint8_t>> out;
	for (int i = 0; i < state::kCellFieldCount; i++) {
		const CellArray a = state::cellArray(w, i);
		size_t esz = 4;
		if (a.type == FieldType::U8) esz = 1;
		else if (a.type == FieldType::I16) esz = 2;
		const uint8_t* p = static_cast<const uint8_t*>(a.data);
		out.emplace_back(p, p + a.size * esz);
	}
	return out;
}

std::unique_ptr<World> roundTrip(const World& w) {
	std::vector<uint8_t> bytes;
	std::string why;
	if (!state::serialize(w, bytes, &why)) return nullptr;
	auto x = state::deserialize(bytes, &why);
	if (x) dmt::watch(*x);
	return x;
}

} // namespace

// ------------------------------------------------------------------ the tick tables, adversarially

DM_TEST("sim adv: molten ticks call nothing but heat; the cooling tick only adds the forced snapshot") {
	stub::Guard g(true);
	if (!allStubbed({ "disasters", "life", "climate", "terrain", "hydro", "units", "economy", "chronicle" })) return;
	auto w = makeWorld({ .phase = Phase::Molten });
	w->heat = 1;
	stub::calls().clear();
	sim::advance(*w, 59);
	std::vector<std::string> other;
	for (const std::string& s : stub::calls()) if (s != "powers_update") other.push_back(s);
	eq(other.size(), size_t(0), "59 molten ticks: no module call besides powers::update");
	eq(stub::called("powers_update"), 59, "advance still calls powers::update every tick (comets need it)");
	stub::calls().clear();
	sim::advance(*w, 1);
	std::vector<std::string> want = { "powers_update", "chronicle_snapshot" };
	eq(stub::calls().size(), want.size(), "cooling tick: update + forced snapshot only");
	if (stub::calls().size() == want.size()) for (size_t i = 0; i < want.size(); i++) eq(stub::calls()[i], want[i]);
	eq(int(w->phase), int(Phase::Water));
}

DM_TEST("sim adv: a classify left pending across People -> History lands its habitability on the next (non-yearly) tick") {
	stub::Guard g(true);
	if (!allStubbed({ "terrain", "climate", "hydro", "life", "economy", "society", "chronicle" })) return;
	auto w = makeWorld({ .phase = Phase::People });
	sim::rederive(*w);
	w->need.derive = w->need.climate = w->need.hydro = false;
	w->need.life = true;
	std::map<int64_t, std::vector<std::string>> at;
	std::vector<int64_t> yearly;
	stub::hooks().life_classify = [&](World& ww) { at[ww.tick].push_back("classify"); };
	stub::hooks().life_habitability = [&](World& ww) { at[ww.tick].push_back("hab"); ww.need.life = false; };
	stub::hooks().civ_yearly = [&](World& ww) { yearly.push_back(ww.tick); };
	stub::hooks().climate_calibrate = [](World& ww) { ww.vars.seaBase = ww.seaLevel; };
	sim::advance(*w, 1);                        // classify on this tick
	const int64_t tc = w->tick;
	ok(at.count(tc) && at[tc].size() == 1 && at[tc][0] == "classify", "classify ran on tick " + std::to_string(tc));
	ok(sim::setPhase(*w, Phase::History), "awaken -> history between the two halves");
	eq(w->vars.t0, tc, "t0 = the classify tick");
	sim::advance(*w, 8);
	ok(at.count(tc + 1) && at[tc + 1].size() == 1 && at[tc + 1][0] == "hab", "habitability on the very next tick");
	for (int64_t y : yearly) ok(!at.count(y), "no heavy pass on yearly tick " + std::to_string(y));
	eq(yearly.size(), size_t(2), "yearly ticks t0+4 and t0+8");
	if (yearly.size() == 2) { eq(yearly[0], tc + 4); eq(yearly[1], tc + 8); }
}

DM_TEST("sim adv: scheduler holds with odd t0, loaded worlds (no scratch) and History -> Ended mid-run") {
	stub::Guard g(true);
	if (!allStubbed({ "terrain", "climate", "hydro", "life", "economy", "society", "chronicle" })) return;
	for (int variant = 0; variant < 3; variant++) {
		stub::hooks() = stub::Hooks{};
		std::unique_ptr<World> w = makeWorld({ .phase = Phase::History });
		w->tick = 1001;
		w->vars.t0 = variant == 0 ? -7 : (variant == 1 ? 5000 : 1001);   // negative, future and plain t0
		if (variant == 2) {                       // a loaded world: no scheduler scratch at all
			auto x = roundTrip(*w);
			if (!ok(x != nullptr, "round trip")) return;
			w = std::move(x);
		}
		std::map<int64_t, std::vector<std::string>> at;
		std::set<int64_t> yearly;
		auto mark = [&](World& ww, const char* nm) { at[ww.tick].push_back(nm); };
		stub::hooks().terrain_derive = [&](World& ww) { mark(ww, "derive"); ww.need.derive = false; ww.need.life = true; };
		stub::hooks().climate_update = [&](World& ww) { mark(ww, "climate"); ww.need.climate = false; ww.need.life = true; };
		stub::hooks().hydro_update = [&](World& ww) { mark(ww, "hydro"); ww.need.hydro = false; ww.need.life = true; };
		stub::hooks().life_classify = [&](World& ww) { mark(ww, "classify"); };
		stub::hooks().life_habitability = [&](World& ww) { mark(ww, "hab"); ww.need.life = false; };
		stub::hooks().civ_yearly = [&](World& ww) { yearly.insert(ww.tick); };
		Rng r(uint32_t(7 + variant));
		for (int i = 0; i < 1500; i++) {
			if (r.chance(0.05)) w->need.derive = true;
			if (r.chance(0.2)) w->need.climate = true;
			if (r.chance(0.1)) w->need.hydro = true;
			if (r.chance(0.06)) w->need.life = true;
			if (i == 700) { w->ending = Ending{ EndingType::Silence, Mood::Quiet, -1, -1, w->year }; sim::setPhase(*w, Phase::Ended); }
			sim::advance(*w, 1);
		}
		const std::string tag = "variant " + std::to_string(variant) + ": ";
		int multi = 0, onYearly = 0, pairBad = 0, close = 0;
		int64_t prevC = -1, prevH = -1, maxGap = 0;
		for (const auto& kv : at) {
			if (kv.second.size() > 1) multi++;
			if (yearly.count(kv.first)) onYearly++;
			const std::string& s = kv.second[0];
			if (s == "classify" && kv.first < w->tick && (!at.count(kv.first + 1) || at.at(kv.first + 1)[0] != "hab")) pairBad++;
			if (s == "hab" && (!at.count(kv.first - 1) || at.at(kv.first - 1)[0] != "classify")) pairBad++;
			if (s == "climate") { if (prevC >= 0) { if (kv.first - prevC < 8) close++; maxGap = std::max(maxGap, kv.first - prevC); } prevC = kv.first; }
			if (s == "hydro") { if (prevH >= 0 && kv.first - prevH < 8) close++; prevH = kv.first; }
		}
		eq(multi, 0, tag + "two heavy passes in a tick");
		eq(onYearly, 0, tag + "heavy pass on a yearly tick");
		eq(pairBad, 0, tag + "classify / habitability apart");
		eq(close, 0, tag + "climate or hydro closer than 8 ticks");
		ok(maxGap <= 48, tag + "climate gap " + std::to_string(maxGap));
		eq(yearly.size(), size_t(375), tag + "one yearly group per 4 ticks, through Ended too");
		for (int64_t y : yearly) if (((y - w->vars.t0) % 4 + 4) % 4 != 0) { fail(tag + "yearly off the t0 grid"); break; }
		eq(int(w->phase), int(Phase::Ended), tag + "ended");
	}
}

// ------------------------------------------------------------------ endings, adversarially

DM_TEST("sim adv: ending is emitted after phase(ended), its payload equals w.ending, and it survives save/load") {
	stub::Guard g(true);
	auto w = makeWorld();
	Culture& cu = state::newCulture(*w);
	const int cid = cu.id;
	Settlement& s = state::newSettlement(*w, findLand(*w), cid);
	s.pop = 2000;
	s.devotion = 0.5;
	const int sid = s.id;
	w->year = 777;
	w->vars.ark = ArkRecord{ cid, sid, 777 };
	std::vector<std::string> order;
	EvEnding got{};
	w->bus.onAll([&](const Event& e) {
		if (e.type == Ev::Phase) order.push_back("phase");
		if (e.type == Ev::Ending) { order.push_back("ending"); got = *e.as<EvEnding>(); }
	});
	const auto e = sim::checkEnding(*w);
	if (!ok(e.has_value() && w->ending.has_value(), "ark ending")) return;
	eq(order.size(), size_t(2));
	if (order.size() == 2) { eq(order[0], std::string("phase")); eq(order[1], std::string("ending")); }
	eq(int(got.type), int(w->ending->type));
	eq(int(got.mood), int(w->ending->mood));
	eq(got.culture, cid);
	eq(got.settlement, sid);
	eq(got.year, 777.0);
	auto x = roundTrip(*w);
	if (!ok(x != nullptr, "an ended world saves and loads")) return;
	sim::use(*x);
	int endings = 0;
	x->bus.on<EvEnding>([&](const EvEnding&) { endings++; });
	sim::advance(*x, 40);
	eq(endings, 0, "no second ending after load");
	eq(int(x->phase), int(Phase::Ended), "still ended");
	eq(x->year, 787.0, "ended worlds keep counting years");
	ok(!sim::genesis::skip(*x, {}), "skip refused when ended");
	ok(!sim::setPhase(*x, Phase::History) && !sim::setPhase(*x, Phase::Ended), "no phase change out of Ended");
	noNaN(*x);
}

DM_TEST("sim adv: ark of a culture that died before launch still ends the game without NaN") {
	auto w = makeWorld();
	Culture& cu = state::newCulture(*w);
	const int cid = cu.id;
	w->cultures[size_t(cid)].dead = true;
	w->vars.ark = ArkRecord{ cid, -1, 3000 };
	w->year = 3000;
	const auto e = sim::checkEnding(*w);
	if (!ok(e.has_value(), "ending")) return;
	eq(int(e->type), int(EndingType::Ark));
	eq(e->settlement, -1);
	ok(std::isfinite(sim::score(*w).total), "score finite");
	noNaN(*w);
}

DM_TEST("sim adv: silence through real ticks lands exactly 50 years after the last people, and survives save/load") {
	stub::Guard g(true);
	if (!allStubbed({ "economy", "society", "units" })) return;   // nobody may be born / die behind our back
	auto w = makeWorld({ .phase = Phase::History });
	w->tick = 0;
	w->vars.t0 = 0;
	w->stats.peakPop = 5000;
	std::vector<double> endYears;
	int lost = 0;
	w->bus.on<EvEnding>([&](const EvEnding& e) { endYears.push_back(e.year); });
	w->bus.on<EvPeopleLost>([&](const EvPeopleLost&) { lost++; });
	sim::advance(*w, 4 * 20);                   // the clock starts at year 1
	ok(w->vars.num.count("sim_empty") == 1 && w->vars.num["sim_empty"] == 1.0, "empty since year 1");
	auto x = roundTrip(*w);                      // save in the middle of the countdown
	if (!ok(x != nullptr, "round trip")) return;
	sim::use(*x);
	x->bus.on<EvEnding>([&](const EvEnding& e) { endYears.push_back(e.year); });
	x->bus.on<EvPeopleLost>([&](const EvPeopleLost&) { lost++; });
	sim::advance(*x, 4 * 40);
	eq(endYears.size(), size_t(1), "one ending");
	if (!endYears.empty()) eq(endYears[0], 51.0, "silence at year 51 (empty since 1)");
	eq(lost, 0, "no people:lost above 1,000 peak");
	ok(x->ending.has_value() && x->ending->mood == Mood::Quiet, "quiet");
}

DM_TEST("sim adv: people:lost is not repeated after save/load within one extinction") {
	auto w = makeWorld();
	w->stats.peakPop = 300;
	int lost = 0;
	w->bus.on<EvPeopleLost>([&](const EvPeopleLost&) { lost++; });
	w->year = 10;
	sim::checkEnding(*w);
	eq(lost, 1);
	auto x = roundTrip(*w);
	if (!ok(x != nullptr)) return;
	x->bus.on<EvPeopleLost>([&](const EvPeopleLost&) { lost++; });
	for (int y = 11; y < 200; y++) { x->year = y; sim::checkEnding(*x); }
	eq(lost, 1, "still once");
	ok(!x->ending.has_value(), "never an ending below 1,000 peak population");
}

// ------------------------------------------------------------------ ownership

DM_TEST("sim adv: with every collaborator stubbed, 100 years of ticks write no cell and no entity (writers table)") {
	stub::Guard g(false);
	if (!allStubbed({ "terrain", "climate", "hydro", "life", "civ", "economy", "society", "diplomacy", "units", "powers", "disasters", "chronicle" })) return;
	auto w = makeWorld({ .phase = Phase::History });
	Culture& cu = state::newCulture(*w);
	const int cid = cu.id;
	Settlement& s = state::newSettlement(*w, findLand(*w), cid);
	s.pop = 500; s.devotion = 0.4; s.love = 0.1;
	w->owner[size_t(s.cell)] = int16_t(s.id);
	const auto cells0 = cellBytes(*w);
	const double sea0 = w->seaLevel, awe0 = w->god.awe, co20 = w->co2;
	const std::string set0 = std::to_string(w->settlements[0].pop) + "/" + std::to_string(w->settlements[0].devotion);
	sim::advance(*w, 400);
	const auto cells1 = cellBytes(*w);
	for (int i = 0; i < state::kCellFieldCount; i++)
		ok(cells0[size_t(i)] == cells1[size_t(i)], std::string("cell field untouched by Sim: ") + state::kCellFields[i].name);
	eq(w->seaLevel, sea0, "seaLevel (Climate's in history)");
	eq(w->god.awe, awe0, "god.awe");
	eq(w->co2, co20, "co2");
	eq(std::to_string(w->settlements[0].pop) + "/" + std::to_string(w->settlements[0].devotion), set0, "settlement");
	eq(w->stats.pop, 500.0, "stats.pop refreshed (Sim's own)");
	eq(w->year, 100.0);
}

DM_TEST("sim adv: source scan: Sim.cpp assigns only the state the writers table gives it") {
	namespace fs = std::filesystem;
	const fs::path file = fs::path(projectRoot()) / "Unreal" / "Source" / "Demiurge" / "Sim" / "Sim.cpp";
	std::ifstream in(file);
	if (!ok(bool(in), "open Sim.cpp")) return;
	// w.<a>(.<b>)* = ...  /  w-><a>... = ...  (also +=, -=, ++)
	const std::regex re(R"(\bw\s*(?:\.|->)\s*([A-Za-z_][A-Za-z0-9_]*(?:\s*\.\s*[A-Za-z_][A-Za-z0-9_]*)*)\s*(?:\[[^\]]*\])?\s*(\+\+|--|[-+*/]?=)(?!=))");
	const std::set<std::string> allowed = { "phase", "heat", "year", "tick", "ending", "vars.lifeBoost", "vars.t0", "seaLevel",
		"stats.pop", "stats.believers", "stats.peakPop", "god.awe", "need", "dirty", "name", "settings", "settings.calamity" };
	std::string line, bad;
	int ln = 0, hits = 0;
	while (std::getline(in, line)) {
		ln++;
		const std::string code = line.substr(0, line.find("//"));
		for (auto it = std::sregex_iterator(code.begin(), code.end(), re); it != std::sregex_iterator(); ++it) {
			std::string target = (*it)[1].str();
			target.erase(std::remove(target.begin(), target.end(), ' '), target.end());
			hits++;
			if (!allowed.count(target)) bad += " " + target + "@" + std::to_string(ln);
		}
	}
	say("Sim.cpp world assignments found: " + std::to_string(hits));
	eq(bad, std::string(), "assignments outside the writers table");
	// vars.num keys must carry the module prefix
	std::ifstream in2(file);
	const std::regex numRe(R"(num\s*\[\s*\"([^\"]+)\")");
	std::string bad2;
	while (std::getline(in2, line))
		for (auto it = std::sregex_iterator(line.begin(), line.end(), numRe); it != std::sregex_iterator(); ++it)
			if ((*it)[1].str().rfind("sim_", 0) != 0) bad2 += " " + (*it)[1].str();
	eq(bad2, std::string(), "vars.num keys prefixed sim_");
}

// ------------------------------------------------------------------ robustness

DM_TEST("sim adv: poisoned Sim inputs never leak NaN (heat, peakPop, tuning)") {
	{
		auto w = makeWorld({ .phase = Phase::Molten });
		w->heat = std::nan("");
		sim::advance(*w, 1);
		eq(int(w->phase), int(Phase::Water), "NaN heat: cools at once");
		ok(std::isfinite(w->heat), "heat finite");
	}
	{
		auto w = makeWorld();
		w->stats.peakPop = std::nan("");
		sim::refreshStats(*w);
		ok(std::isfinite(w->stats.peakPop), "peakPop repaired");
	}
	{
		auto w = makeWorld({ .phase = Phase::Water });
		SimTune& T = w->tune<SimTune>();
		T.lifeBoost = std::nan("");
		T.iceEvery = 0;
		T.lifeParts = 0;
		T.otherParts = -3;
		T.heatPerTick = std::nan("");
		ok(sim::setPhase(*w, Phase::Life), "life");
		eq(w->vars.lifeBoost, 1.0, "NaN lifeBoost -> 1");
		sim::advance(*w, 20);
		noNaN(*w, "degenerate slices / ice cadence");
		T.scorePop = std::nan("");
		T.multQuiet = std::numeric_limits<double>::infinity();
		w->ending = Ending{ EndingType::Silence, Mood::Quiet, -1, -1, 0 };
		ok(std::isfinite(sim::score(*w).total), "score finite with poisoned score tuning");
		ok(std::isfinite(sim::genesis::progress(*w).value), "progress finite");
	}
	{
		auto w = makeWorld({ .phase = Phase::Water });
		w->tune<SimTune>().waterValueSpan = 0;
		w->tune<SimTune>().lifeGate = std::nan("");
		ok(std::isfinite(sim::genesis::progress(*w).value), "zero span");
		w->tune<SimTune>().waterGate = -100;
		ok(sim::genesis::next(*w), "water -> life");
		const auto p = sim::genesis::progress(*w);
		ok(std::isfinite(p.value), "NaN life gate: value finite");
	}
}

DM_TEST("sim adv: tiny and odd worlds (L 0..3, no land, all land) skip and run 100 years in every build") {
	for (int L = 0; L <= 3; L++) {
		auto w = sim::create({ .seed = uint32_t(40 + L), .name = "", .L = L });
		if (!ok(w != nullptr, "create L " + std::to_string(L))) continue;
		dmt::watch(*w);
		sim::genesis::skip(*w, {});
		eq(int(w->phase), int(Phase::History), "L " + std::to_string(L) + " history");
		sim::advance(*w, 400);
		noNaN(*w, "L " + std::to_string(L));
		eq(sim::firstNonFinite(*w), std::string(), "L " + std::to_string(L));
	}
	for (double land : { 0.0, 1.0 }) {
		auto w = makeWorld({ .L = 4, .land = land, .phase = Phase::Water });
		if (land == 0.0) for (float& e : w->elev) e = -5.0f;     // a true ocean world (the helper keeps its top cell at 0)
		int lost = 0;
		w->bus.on<EvPeopleLost>([&](const EvPeopleLost&) { lost++; });
		const bool woke = sim::genesis::skip(*w, { .sea = 0, .cell = -1 });
		eq(int(w->phase), int(Phase::History), "history, land " + f3(land));
		if (land == 0.0) ok(!woke, "an ocean world wakes nobody");
		sim::advance(*w, 400);
		noNaN(*w, "100 years, land " + f3(land));
		if (!woke) { eq(lost, 1, "people:lost once (never populous)"); ok(!w->ending.has_value(), "no ending"); }
		say("land " + f3(land) + ": woke " + std::to_string(woke) + ", live " + std::to_string(liveCount(*w)) + ", pop " + f3(w->stats.pop));
	}
}

DM_TEST("sim adv: genesis skip from every genesis phase (real): one settlement, Awe 60, calibrated") {
	if (!noneStubbed({ "civ", "powers", "society", "terrain", "climate", "life" })) return;
	for (Phase ph : { Phase::Molten, Phase::Water, Phase::Life, Phase::People }) {
		auto w = sim::create({ .seed = 9, .name = "", .L = 4 });
		dmt::watch(*w);
		if (ph >= Phase::Water) sim::setPhase(*w, Phase::Water);
		if (ph >= Phase::Life) sim::setPhase(*w, Phase::Life);
		if (ph >= Phase::People) sim::setPhase(*w, Phase::People);
		std::vector<Phase> phases;
		w->bus.on<EvPhase>([&](const EvPhase& e) { phases.push_back(e.phase); });
		const std::string tag = std::string("from ") + toStr(ph) + ": ";
		ok(sim::genesis::skip(*w, {}), tag + "skip");
		eq(int(w->phase), int(Phase::History), tag + "history");
		eq(liveCount(*w), 1, tag + "one settlement");
		ok(w->god.awe >= 60, tag + "Awe " + f3(w->god.awe));
		ok(w->vars.seaBase.has_value() && *w->vars.seaBase == w->seaLevel, tag + "calibrated at the skip sea");
		ok(!phases.empty() && phases.back() == Phase::History, tag + "last phase event is history");
		eq(w->heat, 0.0, tag + "heat 0");
		noNaN(*w, tag);
	}
}

DM_TEST("sim adv: genesis skip at a sea cell, the pole and a chosen land cell (real)") {
	if (!noneStubbed({ "civ", "powers", "society", "terrain", "climate", "life" })) return;
	auto base = sim::create({ .seed = 5, .name = "", .L = 5 });
	int seaCell = -1;
	for (int c = 0; c < base->n && seaCell < 0; c++) if (base->elev[size_t(c)] < -2) seaCell = c;
	{
		auto w = sim::create({ .seed = 5, .name = "", .L = 5 });
		ok(sim::genesis::skip(*w, { .sea = 0, .cell = seaCell }), "sea cell: wakes elsewhere");
		eq(liveCount(*w), 1);
		if (liveCount(*w) == 1) ok(w->settlements[0].cell != seaCell && !state::isSea(*w, w->settlements[0].cell), "on land");
	}
	{
		auto w = sim::create({ .seed = 5, .name = "", .L = 5 });
		const int pole = w->geo->cellAt(0, kPlanetR, 0);
		const bool woke = sim::genesis::skip(*w, { .sea = 0, .cell = pole });
		ok(woke && liveCount(*w) == 1, "pole: still one settlement (at the best site if the pole is ice)");
		noNaN(*w, "pole");
	}
	{
		auto w = sim::create({ .seed = 5, .name = "", .L = 5 });
		// a land cell that will be settleable after the skip: take the best site of a skipped twin
		auto twin = sim::create({ .seed = 5, .name = "", .L = 5 });
		sim::genesis::skip(*twin, {});
		int pick = -1;
		for (int c = 0; c < twin->n && pick < 0; c++)
			if (civ::canSettle(*twin, c).ok && twin->hab[size_t(c)] > 0.3 && c != civ::bestSite(*twin)) pick = c;
		if (!ok(pick >= 0, "found a second good site")) return;
		ok(sim::genesis::skip(*w, { .sea = 0, .cell = pick }), "chosen cell");
		if (liveCount(*w) == 1) eq(w->settlements[0].cell, pick, "woke exactly there");
	}
}

DM_TEST("sim adv: a comet still in flight when genesis is skipped never moves the sea in history (real)") {
	if (!noneStubbed({ "powers", "disasters", "terrain", "climate" })) return;
	auto w = sim::create({ .seed = 8, .name = "", .L = 4 });
	dmt::watch(*w);
	sim::setPhase(*w, Phase::Water);
	int launched = 0;
	for (int c = 0; c < w->n && launched < 3; c += 97) if (powers::use(*w, "comet", c)) launched++;
	ok(launched >= 1, "comets launched: " + std::to_string(launched));
	ok(!w->disasters.empty(), "in flight");
	ok(sim::genesis::skip(*w, { .sea = 0 }), "skip");
	const double sea0 = w->seaLevel;
	sim::advance(*w, 40);                        // 5 s of flight time pass: every comet lands
	ok(std::fabs(w->seaLevel - sea0) <= 10 * 0.005 + 1e-9, "sea moved only by climate (" + f3(w->seaLevel - sea0) + ")");
	noNaN(*w);
}

// ------------------------------------------------------------------ determinism with a player

DM_TEST("sim adv: same seed, same powers::use calls, interleaved worlds -> equal fingerprints (real or stub)") {
	auto a = sim::create({ .seed = 31, .name = "", .L = 4 });
	auto b = sim::create({ .seed = 31, .name = "", .L = 4 });
	sim::genesis::skip(*a, {});
	sim::genesis::skip(*b, {});
	auto play = [](World& w, int i) {
		if (w.settlements.empty()) return;
		const int c = w.settlements[0].cell;
		if (i % 37 == 0) powers::use(w, "lightning", c);
		if (i % 53 < 6) powers::use(w, "rain", c, { .ang = 0.1, .dt = i % 53 == 0 ? 0.0 : sim::kRealPerTick });
		if (i % 101 == 0) powers::use(w, "heal", c);
	};
	for (int i = 0; i < 800; i++) {
		play(*a, i); sim::advance(*a, 1);
		play(*b, i); sim::advance(*b, 1);
	}
	eq(sim::fingerprint(*a), sim::fingerprint(*b), "fingerprints");
	eq(dmt::hash(*a), dmt::hash(*b), "dmt::hash");
	eq(a->god.awe, b->god.awe, "awe");
	noNaN(*a);
}

// ------------------------------------------------------------------ performance (real, L = 6)

DM_TEST("sim adv: L6 budgets (real): create + skip, genesis-phase ticks, history ticks") {
	if (!noneStubbed({ "terrain", "climate", "hydro", "life", "civ", "economy", "society", "diplomacy", "units", "powers", "disasters" })) return;
	const double t0 = nowMs();
	auto w = sim::create({ .seed = 12, .name = "", .L = 6 });
	const double t1 = nowMs();
	// genesis-phase cost: Life (P = 2) is the dearest
	auto g = sim::create({ .seed = 12, .name = "", .L = 6 });
	sim::setPhase(*g, Phase::Water);
	sim::setPhase(*g, Phase::Life);
	for (int i = 0; i < 40; i++) life::seed(*g, (i * 997) % g->n, 0.12);
	double lifeMax = 0, lifeSum = 0;
	for (int i = 0; i < 200; i++) { const double a = nowMs(); sim::advance(*g, 1); const double d = nowMs() - a; lifeSum += d; lifeMax = std::max(lifeMax, d); }
	const double t2 = nowMs();
	sim::genesis::skip(*w, {});
	const double t3 = nowMs();
	double hMax = 0, hSum = 0;
	for (int i = 0; i < 800; i++) { const double a = nowMs(); sim::advance(*w, 1); const double d = nowMs() - a; hSum += d; hMax = std::max(hMax, d); }
	say("L6 create " + f3(t1 - t0) + " ms + skip " + f3(t3 - t2) + " ms (budget 800); Life-phase tick mean " + f3(lifeSum / 200) +
		" max " + f3(lifeMax) + " ms; history (first 200 years) mean " + f3(hSum / 800) + " max " + f3(hMax) + " ms");
	ok((t1 - t0) + (t3 - t2) < 5 * 800, "create + skip within 5x");
	ok(lifeSum / 200 < 5 * 2.5, "Life-phase mean within 5x");
	ok(hSum / 800 < 5 * 2.5, "history mean within 5x");
	ok(hMax < 5 * 30 && lifeMax < 5 * 30, "worst tick within 5x");
}
