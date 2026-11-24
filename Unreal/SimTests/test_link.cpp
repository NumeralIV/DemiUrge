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
	eq(stub::kStubFnCount, 254, "declared module functions (v3, design review: +8; v3.1: + society::swearPeace, society::tense; chronicle-diplomacy: + society::swornUntil; v4 mythos M0: + 102 in the nine mythos modules, powers::pay, 5 society, 2 civ)");
	eq(powers::kPowerCount, 22, "power table rows (SPEC-SIM 3.10; v3: + consecrate; v4 mythos: + speak, bless, reveal, test, renew)");
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
	(void)society::swornUntil(w, cu, cuB);
	(void)society::tense(w, cu);                      // (chronicle-diplomacy) the v3.1 pair was never called here
	(void)society::swearPeace(w, cu, cuB);
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
	w.tune<MythosTune>().phase = 0;           // (B1) Balance.cpp turns the mythos on; the block below checks phase 0
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

	// ---- (v4 mythos, M0) the mythos modules and the new owner APIs (neutral at phase 0, stubs and real alike)
	{
		WordOpts wo;
		wo.content = WordContent::Decree; wo.area = DecreeArea::Violence; wo.stance = 0; wo.clarity = Clarity::Parable;
		cards::Proposal pr;
		pr.kind = CardKind::Dispute; pr.culture = cu; pr.settlement = sid; pr.severity = 0.5;
		Figure proto;
		proto.kind = uint8_t(FigureKind::RivalVoice); proto.culture = cu; proto.home = sid;
		Legend seed;
		seed.culture = cu; seed.deed = "rain"; seed.truth = 1;
		mythos::attach(w);
		mythos::rebuild(w);
		mythos::yearly(w);
		eq(int(mythos::legacy(w)), int(Legacy::None), "mythos::legacy neutral");
		(void)mythos::legacyScores(w);
		ok(mythos::factors(w, cu) == Factors{}, "mythos::factors neutral");
		ok(mythos::factors(w, 999) == Factors{}, "mythos::factors bad id neutral");
		ok(mythos::townFactors(w, sid) == TownFactors{}, "mythos::townFactors neutral");
		ok(mythos::witnessScale(w, c, "rain") == WitnessScale{}, "mythos::witnessScale neutral");
		eq(int(mythos::aspect(w, cu)), int(Aspect::Unknown), "mythos::aspect");
		eq(mythos::warGoal(w, cu), -1, "mythos::warGoal");
		eq(mythos::neglectScale(w, cu), 1.0, "mythos::neglectScale");
		eq(mythos::archetypeParams(w).natural, 1.0, "mythos::archetypeParams Balanced");
		eq(int(mythos::rite(w, cu)), int(Rite::None), "mythos::rite");
		ok(mythos::faith(w, cu) == nullptr, "mythos::faith before rebuild at phase 0");
		ok(!mythos::setPresence(w, Presence::Walk), "Walk is refused");
		(void)mythos::setPresence(w, Presence::Unseen);
		(void)mythos::withdraw(w, true);
		(void)mythos::bless(w, sid, BlessingKind::Prosperity);
		(void)mythos::answer(w, 1, 0);
		mythos::lapse(w, 1);
		(void)mythos::options(w, 1);
		(void)cards::propose(w, pr);
		cards::yearly(w);
		(void)cards::open(w, pr);
		(void)cards::answer(w, 1, 0);
		(void)cards::withdraw(w, 1);
		(void)cards::lookIntoHeart(w, 1);
		(void)cards::options(w, 1);
		ok(cards::find(w, 1) == nullptr, "cards::find");
		ok(cards::openCards(w).empty(), "cards::openCards");
		(void)cards::priority(w, pr);
		(void)cards::answerOwn(w, 1, 0);
		cards::lapseOwn(w, 1);
		(void)cards::optionsOwn(w, 1);
		doctrine::yearly(w);
		(void)doctrine::speak(w, cu, wo);
		(void)doctrine::canSpeak(w, cu, wo);
		(void)doctrine::wordCost(w, cu, wo);
		(void)doctrine::fidelity(w, cu, wo);
		ok(doctrine::decree(w, cu, DecreeArea::Violence) == nullptr, "doctrine::decree");
		eq(doctrine::kept(w, cu, DecreeArea::Violence), 0.0, "doctrine::kept");
		(void)doctrine::burden(w, cu);
		eq(doctrine::strictness(w, DecreeArea::Violence, 0), 0.9, "doctrine::strictness Peace");
		eq(doctrine::strictness(w, DecreeArea::Count, 0), 0.0, "doctrine::strictness bad area");
		(void)doctrine::customStance(w, cu, DecreeArea::Care);
		(void)doctrine::swearPact(w, cu, GodTerm::Rain, PeopleTerm::NoWar, 100, -1);
		(void)doctrine::reveal(w, cu, RevelationKind::Writing, Clarity::Dream);
		eq(int(doctrine::deathBelief(w, cu)), int(DeathBelief::None), "doctrine::deathBelief");
		(void)doctrine::answer(w, 1, 0);
		doctrine::lapse(w, 1);
		(void)doctrine::options(w, 1);
		characters::yearly(w);
		(void)characters::candidates(w, sid);
		(void)characters::choose(w, sid, 0);
		(void)characters::spawn(w, proto);
		(void)characters::fell(w, 1, FigureFate::Fallen);
		eq(characters::ruler(w, cu), -1, "characters::ruler");
		eq(characters::messenger(w, cu), -1, "characters::messenger");
		eq(characters::credibility(w, -1), 0.5, "characters::credibility neutral");
		(void)characters::adjustCredibility(w, 1, 0.1);
		ok(characters::figure(w, 1) == nullptr, "characters::figure");
		(void)characters::test(w, 1, TrialKind::Power);
		(void)characters::trialOdds(w, 1, TrialKind::Power);
		(void)characters::answer(w, 1, 0);
		characters::lapse(w, 1);
		(void)characters::options(w, 1);
		legends::yearly(w);
		(void)legends::add(w, seed);
		(void)legends::strongest(w, cu, 3);
		faiths::yearly(w);
		ok(faiths::house(w, cu) == nullptr, "faiths::house");
		(void)faiths::adjustHouse(w, cu, 0.1, 0, 0, 0);
		(void)faiths::seedRival(w, cu, sid, 0.1);
		(void)faiths::chooseBearers(w, cu, Charge::CarryWord);
		eq(faiths::bearers(w), -1, "faiths::bearers");
		(void)faiths::answer(w, 1, 0);
		faiths::lapse(w, 1);
		(void)faiths::options(w, 1);
		omens::yearly(w);
		(void)omens::foresee(w, OmenKind::Quake, c, w.year + 30, 1, 0.1);
		(void)omens::foretell(w, sid, w.year + 30, -1);
		(void)omens::struck(w, 1);
		(void)omens::warn(w, 1, WarningWay::Dreams);
		ok(!omens::foresightOn(w), "omens::foresightOn off at phase 0");
		(void)omens::answer(w, 1, 0);
		omens::lapse(w, 1);
		(void)omens::options(w, 1);
		keepers::yearly(w);
		(void)keepers::appoint(w, KeeperDomain::Deep, Temper::Gentle, cu);
		(void)keepers::dismiss(w, 1);
		(void)keepers::setTemper(w, 1, Temper::Stern);
		eq(keepers::slots(w), 0, "keepers::slots at phase 0");
		(void)keepers::answer(w, 1, 0);
		keepers::lapse(w, 1);
		(void)keepers::options(w, 1);
		renewal::yearly(w);
		(void)renewal::begin(w, RenewalKind::Water, { sid });
		ok(!renewal::active(w), "renewal::active");
		ok(!renewal::remnant(w, sid), "renewal::remnant");
		(void)renewal::answer(w, 1, 0);
		renewal::lapse(w, 1);
		(void)renewal::options(w, 1);
		ok(!powers::pay(w, "link", 10), "powers::pay is neutral at M0");
		ok(!powers::unlocked(w, "speak"), "the Word powers are locked at phase 0");
		{
			Traits t;
			(void)society::shapeTraits(w, cu, t);
		}
		society::nudge(w, sid, 0.1, 0.1);
		society::remember(w, cu, 0.1, 0.1);
		(void)society::answerPrayer(w, sid, 0.5);
		(void)civ::demoteTemple(w, sid);
	}
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
	(void)civ::exodus(w, sid, 0.3);                    // (v4 mythos, M0)
	(void)society::secede(w, { sid }, BirthReason::Schism, -1);
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
			(void)renewal::reseed(*w2, {});      // (v4 mythos, M0)
			(void)legends::importOld(*w2, w, 2);
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
