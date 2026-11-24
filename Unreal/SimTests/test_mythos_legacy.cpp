// DEMIURGE — SimTests/test_mythos_legacy.cpp : the legacy verdict (SPEC-MYTHOS 4): each score on synthetic worlds,
// ties, the silence boost, the remembered gate. Track M1 "mythos-core". Key: run.ps1 mythos; also in all (the
// parts that need a stubbed collaborator return early there).
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

#include <string>
#include <vector>

using namespace dm;
using namespace dmt;

namespace {

std::unique_ptr<World> legacyWorld(int cultures = 1) {
	auto w = makeWorld({ .L = 3, .seed = 5 });
	w->tune<MythosTune>().phase = 1;
	std::vector<int> land;
	for (int c = 0; c < w->n; c++) if (w->elev[size_t(c)] >= w->seaLevel) land.push_back(c);
	for (int c = 0; c < cultures; c++) {
		state::newCulture(*w);
		Settlement& s = state::newSettlement(*w, land[size_t(c * 3 + 1)], c);
		s.pop = 100; s.love = 0; s.capital = true;
		w->cultures[size_t(c)].capital = s.id;
	}
	mythos::rebuild(*w);
	// a plain record: nothing leads
	for (Faith& f : w->mythos.faith) { f.agency = 0; f.obedience = 0; f.dependence = 1; f.selfReliance = 0; }
	for (Culture& cu : w->cultures) cu.traits.compassion = 0;
	for (Settlement& s : w->settlements) s.love = -0.5;
	Ending e; e.type = EndingType::Ark; e.culture = 0; e.settlement = 0; e.year = 3000;
	w->ending = e;
	return w;
}

Decree decreeFor(int c, DecreeArea a, int stance) {
	Decree d; d.culture = c; d.area = a; d.stance = int8_t(stance); d.kept = 1;
	return d;
}

double score(const World& w, Legacy l) { return mythos::legacyScores(w).score[size_t(l)]; }

} // namespace

DM_TEST("mythos legacy: an obedient empire (obedience, the House, a Ruler image, decrees; x 0.5 with fewer than 2)") {
	auto w = legacyWorld();
	Faith& f = w->mythos.faith[0];
	f.obedience = 0.9; f.image = { 0, 1, 0, 0 };
	House h; h.culture = 0; h.authority = 0.8; w->mythos.houses.push_back(h);
	const bool houseRead = faiths::house(*w, 0) != nullptr;
	const double auth = houseRead ? 0.8 : 0;
	for (int a : { 0, 1, 3, 4 }) w->mythos.decrees.push_back(decreeFor(0, DecreeArea(a), 0));
	near(score(*w, Legacy::ObedientEmpire), 0.45 * 0.9 + 0.25 * auth + 0.2 * 1 + 0.1 * 1, 1e-12, "the formula");
	eq(int(mythos::legacy(*w)), int(Legacy::ObedientEmpire), "it wins");
	w->mythos.decrees.resize(1);
	near(score(*w, Legacy::ObedientEmpire), 0.5 * (0.45 * 0.9 + 0.25 * auth + 0.2 + 0.1 * 0.25), 1e-12, "one decree: x 0.5");
}

DM_TEST("mythos legacy: a compassionate civilisation (compassion, care, few famine and plague deaths, love)") {
	auto w = legacyWorld();
	w->cultures[0].traits.compassion = 0.8;
	w->settlements[0].love = 0.5;
	w->mythos.decrees.push_back(decreeFor(0, DecreeArea::Care, int(CareStance::Kin)));
	w->mythos.faith[0].data["m_peak"] = 1000;
	w->mythos.faith[0].data["m_fpDead"] = 50;
	near(score(*w, Legacy::CompassionateCivilisation), 0.35 * 0.8 + 0.25 * 0.5 + 0.25 * (1 - 4 * 0.05) + 0.15 * 1, 1e-12, "the formula (Kin 0.5)");
	w->mythos.decrees.push_back(decreeFor(0, DecreeArea::Worship, int(Rite::Kindness)));
	near(score(*w, Legacy::CompassionateCivilisation), 0.35 * 0.8 + 0.25 * 1 + 0.25 * 0.8 + 0.15, 1e-12, "the Kindness rite adds 0.5 to care");
	eq(int(mythos::legacy(*w)), int(Legacy::CompassionateCivilisation), "it wins");
}

DM_TEST("mythos legacy: a terrified world and the remembered gate of the unneeded (society::memory)") {
	stub::Guard g(true);
	if (!stub::stubbed("society")) return;
	society::Memory mem;
	stub::hooks().society_memory = [&](const World&, int) { return mem; };
	auto w = legacyWorld();
	w->mythos.faith[0].image = { 0, 0, 0, 1 };
	mem.verdict = -0.5;
	near(score(*w, Legacy::TerrifiedWorld), 0.6 + 0.4, 1e-12, "a feared verdict and a Threat image");
	eq(int(mythos::legacy(*w)), int(Legacy::TerrifiedWorld), "it wins");
	mem.verdict = 0;
	Faith& f = w->mythos.faith[0];
	f.selfReliance = 0.8; f.agency = 0.9; f.dependence = 0.1;
	const double un = 0.5 * 0.8 + 0.3 * 0.9 + 0.2 * 0.9;
	near(score(*w, Legacy::Unneeded), 0.5 * un, 1e-12, "a forgotten god was never outgrown: x 0.5");
	mem.remembered = true;
	near(score(*w, Legacy::Unneeded), un, 1e-12, "remembered: in full");
	eq(int(mythos::legacy(*w)), int(Legacy::Unneeded), "it wins");
}

DM_TEST("mythos legacy: a thriving wilderness (forest kept, the Land decree, cover) and the silence boost") {
	stub::Guard g(true);
	if (!stub::stubbed("life")) return;
	life::Cover cov; cov.forest = 0.3; cov.veg = 0.6;
	stub::hooks().life_cover = [&](const World&) { return cov; };
	auto w = legacyWorld(2);
	w->mythos.num["mythos_forest0"] = 0.4;
	w->mythos.decrees.push_back(decreeFor(0, DecreeArea::Land, int(LandStance::Equal)));
	const double wi = 0.45 * 0.75 + 0.3 * 1 + 0.25 * 0.6;
	near(score(*w, Legacy::ThrivingWilderness), wi, 1e-12, "Equal standing");
	eq(int(mythos::legacy(*w)), int(Legacy::ThrivingWilderness), "it wins");
	// a silence: everyone, x 1.3 (both cultures weighted by people; culture 1 has no Land decree: 0.3)
	w->ending->type = EndingType::Silence;
	const double wi1 = 0.45 * 0.75 + 0.3 * 0.3 + 0.25 * 0.6;
	near(score(*w, Legacy::ThrivingWilderness), 1.3 * (wi + wi1) / 2, 1e-12, "silence: x 1.3, the mean over the peoples");
	// all dead: weighted by peak people
	for (Settlement& s : w->settlements) { s.dead = true; s.pop = 0; }
	for (Culture& c : w->cultures) c.dead = true;
	w->mythos.faith[0].data["m_peak"] = 300; w->mythos.faith[1].data["m_peak"] = 100;
	near(score(*w, Legacy::ThrivingWilderness), 1.3 * (0.75 * wi + 0.25 * wi1), 1e-12, "the dead weighted by their peak");
	eq(int(mythos::legacy(*w)), int(Legacy::ThrivingWilderness), "the world without them thrives");
}

DM_TEST("mythos legacy: ties go Unneeded, Compassionate, Wilderness, Obedient, Terrified; None without an ending or at phase 0") {
	auto w = legacyWorld();
	// a world where every score is 0: the first of the tie order wins
	w->mythos.faith[0].dependence = 1;
	w->mythos.decrees.push_back(decreeFor(0, DecreeArea::Land, int(LandStance::Dominion)));   // the wilderness scores 0 too
	w->mythos.decrees.back().custom = true;  // (their own custom: not a decree of the god's for the obedient score)
	w->cultures[0].traits.compassion = 0; w->settlements[0].love = -0.5;
	w->mythos.faith[0].data["m_peak"] = 100; w->mythos.faith[0].data["m_fpDead"] = 25;      // care 0, deaths x 4 = 1
	const LegacyScores s = mythos::legacyScores(*w);
	bool allZero = true;
	for (int l = 1; l < 6; l++) allZero = allZero && s.score[size_t(l)] == 0;
	if (stub::stubbed("life") && stub::stubbed("society")) ok(allZero, "a world with every score 0");
	if (allZero) eq(int(s.best), int(Legacy::Unneeded), "all equal: Unneeded");
	const auto saved = w->ending;
	w->ending.reset();
	eq(int(mythos::legacy(*w)), int(Legacy::None), "no ending: None");
	ok(mythos::legacyScores(*w).best != Legacy::None, "but the scores can be read during play (everyone)");
	w->ending = saved;
	w->tune<MythosTune>().phase = 0;
	eq(int(mythos::legacy(*w)), int(Legacy::None), "phase 0: None");
	eq(int(mythos::legacyScores(*w).best), int(Legacy::None), "phase 0: no scores");
}
