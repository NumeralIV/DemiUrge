// DEMIURGE — SimTests/test_link.cpp : every declared module function, called once on a small world
// (foundation-api). Proves that the module headers, the stubs (or real modules) and run.ps1 agree.
//
//   run.ps1 link   every module is its stub: also checks that each stub records its call, that the
//                  neutral stubs leave the world unchanged, and that hooks replace a stub.
//   run.ps1 all    every module is real: the same calls must not crash, emit only valid events
//                  (the world is watched) or write NaN. Assertions that hold for the real modules
//                  too (spec facts such as cap("abc") == "Abc") are checked in both builds.
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

#include <filesystem>
#include <string>
#include <vector>

using namespace dm;
using namespace dmt;

namespace {

int countStubbed() {
	int k = 0;
	for (const char* m : stub::kStubModules) if (stub::stubbed(m)) k++;
	return k;
}
bool allStubbed() { return countStubbed() == int(sizeof(stub::kStubModules) / sizeof(stub::kStubModules[0])); }

std::string tempDir() {
	std::error_code ec;
	std::filesystem::path p = std::filesystem::temp_directory_path(ec) / "demiurge-test-link";
	std::filesystem::create_directories(p, ec);
	return p.string();
}

int firstNbr(const World& w, int c) { return w.geo->nbrN[size_t(c)] > 0 ? w.geo->nbr[size_t(6 * c)] : c; }

} // namespace

DM_TEST("link: the stub registry matches the stub files") {
	const int k = countStubbed();
	ok(k == 0 || allStubbed(), "either every module is a stub (link) or none is (all): " + std::to_string(k));
	for (const stub::StubFn& f : stub::kStubFns) {
		bool known = false;
		for (const char* m : stub::kStubModules) if (std::string(m) == f.module) known = true;
		ok(known, std::string("row of an unknown module: ") + f.module + " " + f.hook);
	}
	eq(stub::kStubFnCount, 143, "declared module functions (v3, design review: +8; v3.1: + society::swearPeace, society::tense)");
	eq(powers::kPowerCount, 17, "power table rows (SPEC-SIM 3.10; v3: + consecrate)");
}

DM_TEST("link: every declared function is callable") {
	stub::Guard guard(true);
	auto wp = makeWorld({ .L = 3, .seed = 7 });
	World& w = *wp;
	const int c = findLand(w);
	const int c2 = findLand(w, [&](int x) { return x != c && w.geo->angle(c, x) > 0.3; });
	if (!ok(c >= 0 && c2 >= 0, "helper world has land")) return;
	const int nb = firstNbr(w, c);

	// ---- set-up through the factories (no module calls)
	Culture& cu0 = state::newCulture(w);
	const int cu = cu0.id;
	const int cuB = state::newCulture(w).id;
	Settlement& s0 = state::newSettlement(w, c, cu);
	s0.pop = 100;
	const int sid = s0.id;
	w.owner[size_t(c)] = int16_t(sid);
	Unit& u0 = state::newUnit(w, UnitType::Trader);
	u0.culture = cu;
	u0.from = sid;
	u0.dest = nb;
	u0.path = { c, nb };
	const int uid = u0.id;
	const uint32_t h0 = hash(w);
	const bool neutral = allStubbed();

	// ---- queries that hold for stubs and real modules alike
	eq(names::cap("abc"), std::string("Abc"), "names::cap");
	eq(civ::at(w, c), sid, "civ::at on the centre");
	{
		const std::vector<int> nr = civ::near(w, c, 0.05);
		ok(!nr.empty() && nr[0] == sid, "civ::near finds the settlement first");
		const std::vector<int> lv = civ::live(w);
		ok(lv.size() == 1 && lv[0] == sid, "civ::live");
	}
	eq(chronicle::yearStr(w, 312.0), std::string("Year 312"), "chronicle::yearStr");
	eq(sim::mood(0.5, 0.5), Mood::Beloved, "sim::mood beloved");
	eq(sim::mood(0.5, -0.5), Mood::Feared, "sim::mood feared");
	eq(save::metaOf(w, 5).savedAt, int64_t(5), "save::metaOf savedAt");
	eq(save::defaultSettings(true).flashes, 0.3, "reduced-motion flashes");
	ok(!balance::set(w, "no.such-key", 1), "balance::set rejects unknown keys");
	{
		const powers::PowerDef* m = powers::byId("meteor");
		ok(m && m->key == 'M' && m->cost == 150 && m->wAng == 0.45, "byId(meteor)");
		ok(powers::byId("nope") == nullptr, "byId(unknown)");
	}
	ok(hydro::isLake(w, c) == (w.water[size_t(c)] > 0.03f), "hydro::isLake");

	// ---- names
	Rng r(11);
	const std::string lang = names::language(r, std::string());
	ok(!names::word(lang, r, 1, 3).empty(), "word");
	ok(!names::place(lang, r).empty(), "place");
	ok(!names::person(lang, r).empty(), "person");
	ok(!names::culture(lang, r).name.empty(), "culture name");
	ok(!names::god(lang, r).empty(), "god");
	ok(!names::planet(r).empty(), "planet");

	// ---- queries on every module (stub answers are neutral)
	(void)climate::datum(w);
	(void)climate::anomaly(w);
	(void)climate::landIce(w);
	(void)hydro::riverT(w);
	(void)hydro::isRiver(w, c);
	(void)life::capacity(w, c);
	(void)life::cover(w);
	{
		double pc = 0;
		(void)path::find(w, c, c2, {}, &pc);
		(void)path::stepCost(w, c, nb, NavalMode::Land);
		int visited = 0;
		(void)path::reach(w, c, 5, NavalMode::Coastal, [&](int, double) { return ++visited < 50; });
		(void)path::cost(w, { c, nb }, NavalMode::Land);
	}
	{
		(void)units::near(w, c, 0.2);
		const Unit* u = state::findUnit(w, uid);
		if (u) {
			Vec3 d;
			(void)units::cellOf(w, *u);
			(void)units::dirOf(w, *u, d);
			ok(isFinite(d.x) && isFinite(d.y) && isFinite(d.z), "dirOf finite");
		}
	}
	(void)civ::canSettle(w, c2);
	(void)civ::siteScore(w, c2);
	(void)civ::bestSite(w);
	(void)civ::ageProgress(w, cu);
	{
		const Settlement* sp = state::settlement(w, sid);
		if (sp) (void)civ::capacity(w, *sp);
	}
	(void)society::regard(w, -1);
	(void)society::aweIncome(w);
	(void)society::memory(w, cu);
	(void)civ::bandSite(w, c2);
	(void)powers::upgrade(w, "rain");
	(void)powers::drownRisk(w, c, 0.06);
	(void)society::atWar(w, cu, cuB);
	(void)powers::unlocked(w, "rain");
	(void)powers::cost(w, "rain", -1, -1);
	(void)powers::can(w, "rain", c);
	(void)chronicle::epithet(w, cu);
	(void)chronicle::tale(w);
	(void)sim::sample(w);
	(void)sim::fingerprint(w);
	(void)sim::firstNonFinite(w);
	(void)sim::isYearlyTick(w);
	(void)sim::score(w);
	(void)sim::genesis::progress(w);
	(void)balance::keys();
	(void)save::slotPath("dir", "slot");
	(void)save::peek(nullptr, 0);

	// ---- mutators (neutral stubs change nothing)
	Rng rr = w.rng.fork("link");
	terrain::sculpt(w, c, 0.1, 0.2);
	terrain::crater(w, c2, 0.1, 0.5);
	terrain::cone(w, c2, 0.1, 0.5);
	terrain::roughen(w, c2, 0.1, 0.1, rr);
	terrain::addElev(w, c2, 0.05);
	terrain::touched(w);
	terrain::derive(w);
	climate::update(w);
	climate::stepIce(w);
	climate::yearly(w);
	climate::emit(w, 1);
	climate::addDust(w, 0.01);
	hydro::update(w);
	life::classify(w);
	life::step(w, 0, 8);
	life::burn(w);
	life::ignite(w, c2, 1);
	life::seed(w, c2, 0.1);
	life::bloom(w, c2, 0.1, 0.1);
	life::habitability(w);
	life::rebuild(w);
	units::step(w, 0.25);
	(void)civ::infect(w, sid, 0.6);
	(void)civ::cure(w, sid, 30);
	civ::claim(w, sid);
	civ::rebuild(w);
	(void)civ::electCapital(w, cu);
	(void)society::witness(w, c, 0.2, { "rain", 0.1, 0.5, {}, 0 });
	(void)society::raiseProphet(w, sid);
	(void)society::setEdict(w, cu, Edict::Peace);
	(void)society::raiseWonder(w, sid);
	(void)civ::consecrate(w, sid);
	(void)civ::addScience(w, cu, 1);
	(void)civ::strikeKnowledge(w, { sid }, 1);
	(void)powers::use(w, "rain", c, { 0.1, 0.125 });
	(void)powers::start(w, DisasterType::Storm, c2, {});
	powers::update(w, 0.125);
	powers::tick(w);
	chronicle::attach(w);
	(void)chronicle::add(w, { "test", "A test entry.", c, cu, 1 });
	chronicle::updateEpithets(w);
	(void)chronicle::snapshot(w, true);
	balance::apply(w);
	{
		units::SpawnOpts so;
		so.type = UnitType::Settler;
		so.culture = cu;
		so.from = sid;
		so.dest = c2;
		so.size = 10;
		(void)units::spawn(w, so);
	}
	(void)civ::found(w, c2, cu, 30, FoundedBy::Settlers, {});
	if (neutral) eq(hash(w), h0, "neutral stubs leave the world unchanged");

	// ---- the yearly passes and the destructive ones
	civ::yearly(w);
	society::yearly(w);
	society::diplomacy(w);
	powers::natural(w);
	climate::calibrate(w);
	life::fill(w, 1);
	society::declareWar(w, cu, cuB);
	society::makePeace(w, cu, cuB, 40, -1);
	society::onArrive(w, uid);
	(void)units::kill(w, uid, "test");
	(void)society::newCulture(w, { -1, c2, BirthReason::Awaken });
	(void)civ::damage(w, sid, 0.1, Cause::War);
	civ::setCulture(w, sid, cuB);
	civ::capture(w, sid, cu);
	civ::destroy(w, sid, Cause::Famine);
	{
		Relation* rl = society::rel(w, cu, cuB);
		ok(rl != nullptr, "society::rel creates on demand");
		ok(society::peek(w, cu, cuB) != nullptr, "society::peek after rel");
		ok(society::rel(w, cu, cu) == nullptr, "society::rel(a, a) is null");
	}

	// ---- sim on its own small world
	{
		auto w2 = sim::create({ 3, std::string(), 2, Settings{} });
		if (ok(w2 != nullptr, "sim::create returns a world")) {
			watch(*w2);
			terrain::generate(*w2);
			sim::use(*w2);
			sim::rederive(*w2);
			sim::tick(*w2);
			sim::advance(*w2, 2);
			(void)sim::setPhase(*w2, Phase::Water);
			sim::refreshStats(*w2);
			(void)sim::checkEnding(*w2);
			(void)sim::genesis::next(*w2);
			(void)sim::genesis::skip(*w2, {});
			(void)sim::runTicks(*w2, 2, {}, false);
			noNaN(*w2, "sim world");
		}
	}

	// ---- save (stubs touch no file; real modules write to the temp folder)
	{
		const std::string dir = tempDir();
		std::vector<uint8_t> bytes;
		std::string why;
		(void)save::encode(w, bytes, {}, &why);
		(void)save::decode(bytes.data(), bytes.size(), &why);
		const std::string file = dir + "/link.dmsave";
		(void)save::writeFile(w, file, {}, &why);
		(void)save::readFile(file, &why);
		(void)save::readMeta(file);
		(void)save::writeSlot(w, dir, "link", {}, &why);
		(void)save::readSlot(dir, "link", &why);
		(void)save::listSlots(dir);
		(void)save::removeSlot(dir, "link");
		const std::string sfile = dir + "/settings.txt";
		(void)save::saveSettings(save::defaultSettings(false), sfile);
		(void)save::loadSettings(sfile, false);
		std::error_code ec;
		std::filesystem::remove_all(dir, ec);
	}

	noNaN(w, "after every module call");

	// ---- every stubbed function recorded its call
	int missing = 0;
	for (const stub::StubFn& f : stub::kStubFns) {
		if (!stub::stubbed(f.module)) continue;
		if (stub::called(f.hook) < 1) {
			missing++;
			fail(std::string("never called through its stub: ") + f.hook);
		}
	}
	eq(missing, 0, "functions without a recorded call");
	if (neutral) ok(int(stub::calls().size()) >= stub::kStubFnCount, "call log size " + std::to_string(stub::calls().size()));
}

DM_TEST("link: a hook replaces a stub and the guard removes it") {
	auto wp = makeWorld({ .L = 2, .seed = 1 });
	World& w = *wp;
	if (!stub::stubbed("civ") || !stub::stubbed("units")) return;   // real modules ignore hooks
	{
		stub::Guard guard(true);
		stub::hooks().civ_live = [](const World&) { return std::vector<int>{ 42 }; };
		stub::hooks().units_spawn = [](World& ww, const units::SpawnOpts& o) -> Unit* {
			Unit& u = state::newUnit(ww, o.type);
			u.culture = o.culture;
			return &u;
		};
		const std::vector<int> lv = civ::live(w);
		ok(lv.size() == 1 && lv[0] == 42, "hooked civ::live");
		units::SpawnOpts so;
		so.culture = 3;
		const Unit* u = units::spawn(w, so);
		ok(u && u->culture == 3 && w.units.size() == 1, "hooked units::spawn");
		eq(stub::called("civ_live"), 1, "log counts the hooked call");
		eq(stub::called("units_spawn"), 1, "log counts the hooked call");
	}
	ok(!stub::hooks().civ_live && stub::calls().empty() && !stub::recording(), "guard reset");
	ok(civ::live(w).empty(), "neutral again after the guard");
	ok(units::spawn(w, {}) == nullptr, "neutral spawn after the guard");
}
