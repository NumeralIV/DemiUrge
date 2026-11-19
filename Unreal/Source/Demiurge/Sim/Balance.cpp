// DEMIURGE — Sim/Balance.cpp : balance overrides of the per-world tuning structs (SPEC-SIM 3.13).
// Owner: sim engineer (A14) until integration, then the balance owner (simulation integrator), who
// is the only one to edit it afterwards. Module authors do not retune.
//
// apply(w): the overrides (empty at first). set(w, "module.field", v): one value by name, for simrun
// experiments (--set economy.growthR=0.032); keys(): every name set() accepts, sorted. Array fields
// are addressed by index: "economy.ageNeed.2". Integer fields are rounded.
#include "Balance.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "Chronicle.h"
#include "Civ.h"
#include "Climate.h"
#include "Hydro.h"
#include "Life.h"
#include "Powers.h"
#include "Sim.h"
#include "Society.h"
#include "Terrain.h"
#include "Units.h"

namespace dm::balance {
namespace {

using Setter = void (*)(World&, double);
struct KeyRow {
	const char* key;
	Setter set;
};

template <class T, double T::*M>
void setD(World& w, double v) { w.tune<T>().*M = v; }
template <class T, int T::*M>
void setI(World& w, double v) { w.tune<T>().*M = int(std::lround(std::clamp(v, -1e9, 1e9))); }
template <class T, size_t N, std::array<double, N> T::*M, size_t I>
void setA(World& w, double v) { (w.tune<T>().*M)[I] = v; }
template <class T, size_t N, std::array<int, N> T::*M, size_t I>
void setAI(World& w, double v) { (w.tune<T>().*M)[I] = int(std::lround(std::clamp(v, -1e9, 1e9))); }

// Registered keys (alphabetical within a module is not required: keys() sorts).
const KeyRow kKeys[] = {
	// sim
	{ "sim.lifeBoost", &setD<SimTune, &SimTune::lifeBoost> },
	{ "sim.heatPerTick", &setD<SimTune, &SimTune::heatPerTick> },
	{ "sim.climateMin", &setI<SimTune, &SimTune::climateMin> },
	{ "sim.climateMax", &setI<SimTune, &SimTune::climateMax> },
	{ "sim.hydroMin", &setI<SimTune, &SimTune::hydroMin> },
	{ "sim.waterGate", &setD<SimTune, &SimTune::waterGate> },
	{ "sim.lifeGate", &setD<SimTune, &SimTune::lifeGate> },
	{ "sim.silenceYears", &setD<SimTune, &SimTune::silenceYears> },
	{ "sim.silencePeak", &setD<SimTune, &SimTune::silencePeak> },
	{ "sim.ashYears", &setD<SimTune, &SimTune::ashYears> },
	{ "sim.startAwe", &setD<SimTune, &SimTune::startAwe> },
	// economy (civ.yearly)
	{ "economy.growthR", &setD<EconomyTune, &EconomyTune::growthR> },
	{ "economy.granaryR", &setD<EconomyTune, &EconomyTune::granaryR> },
	{ "economy.blessedR", &setD<EconomyTune, &EconomyTune::blessedR> },
	{ "economy.loveR", &setD<EconomyTune, &EconomyTune::loveR> },
	{ "economy.granaryK", &setD<EconomyTune, &EconomyTune::granaryK> },
	{ "economy.plantK", &setD<EconomyTune, &EconomyTune::plantK> },
	{ "economy.plagueDeath", &setD<EconomyTune, &EconomyTune::plagueDeath> },
	{ "economy.expandK", &setD<EconomyTune, &EconomyTune::expandK> },
	{ "economy.expandPop", &setD<EconomyTune, &EconomyTune::expandPop> },
	{ "economy.expandCooldown", &setD<EconomyTune, &EconomyTune::expandCooldown> },
	{ "economy.searchesPerYear", &setI<EconomyTune, &EconomyTune::searchesPerYear> },
	{ "economy.landMaxCost", &setD<EconomyTune, &EconomyTune::landMaxCost> },
	{ "economy.navalMaxCost", &setD<EconomyTune, &EconomyTune::navalMaxCost> },
	{ "economy.campKMin", &setD<EconomyTune, &EconomyTune::campKMin> },
	{ "economy.partyFrac", &setD<EconomyTune, &EconomyTune::partyFrac> },
	{ "economy.rateMin", &setD<EconomyTune, &EconomyTune::rateMin> },
	{ "economy.rateMax", &setD<EconomyTune, &EconomyTune::rateMax> },
	{ "economy.academyF", &setD<EconomyTune, &EconomyTune::academyF> },
	{ "economy.marketF", &setD<EconomyTune, &EconomyTune::marketF> },
	{ "economy.factoryF", &setD<EconomyTune, &EconomyTune::factoryF> },
	{ "economy.inspireF", &setD<EconomyTune, &EconomyTune::inspireF> },
	{ "economy.catchUp", &setD<EconomyTune, &EconomyTune::catchUp> },
	{ "economy.factoryCo2", &setD<EconomyTune, &EconomyTune::factoryCo2> },
	{ "economy.ageNeed.0", &setA<EconomyTune, 6, &EconomyTune::ageNeed, 0> },
	{ "economy.ageNeed.1", &setA<EconomyTune, 6, &EconomyTune::ageNeed, 1> },
	{ "economy.ageNeed.2", &setA<EconomyTune, 6, &EconomyTune::ageNeed, 2> },
	{ "economy.ageNeed.3", &setA<EconomyTune, 6, &EconomyTune::ageNeed, 3> },
	{ "economy.ageNeed.4", &setA<EconomyTune, 6, &EconomyTune::ageNeed, 4> },
	{ "economy.ageNeed.5", &setA<EconomyTune, 6, &EconomyTune::ageNeed, 5> },
	{ "economy.techYield.0", &setA<EconomyTune, 7, &EconomyTune::techYield, 0> },
	{ "economy.techYield.1", &setA<EconomyTune, 7, &EconomyTune::techYield, 1> },
	{ "economy.techYield.2", &setA<EconomyTune, 7, &EconomyTune::techYield, 2> },
	{ "economy.techYield.3", &setA<EconomyTune, 7, &EconomyTune::techYield, 3> },
	{ "economy.techYield.4", &setA<EconomyTune, 7, &EconomyTune::techYield, 4> },
	{ "economy.techYield.5", &setA<EconomyTune, 7, &EconomyTune::techYield, 5> },
	{ "economy.techYield.6", &setA<EconomyTune, 7, &EconomyTune::techYield, 6> },
	{ "economy.gate.1", &setA<EconomyTune, 5, &EconomyTune::gate, 1> },
	{ "economy.gate.2", &setA<EconomyTune, 5, &EconomyTune::gate, 2> },
	{ "economy.gate.3", &setA<EconomyTune, 5, &EconomyTune::gate, 3> },
	{ "economy.gate.4", &setA<EconomyTune, 5, &EconomyTune::gate, 4> },
	{ "economy.radius.0", &setAI<EconomyTune, 5, &EconomyTune::radius, 0> },
	{ "economy.radius.1", &setAI<EconomyTune, 5, &EconomyTune::radius, 1> },
	{ "economy.radius.2", &setAI<EconomyTune, 5, &EconomyTune::radius, 2> },
	{ "economy.radius.3", &setAI<EconomyTune, 5, &EconomyTune::radius, 3> },
	{ "economy.radius.4", &setAI<EconomyTune, 5, &EconomyTune::radius, 4> },
	// (integration) more economy / civ keys
	{ "economy.ownDistMin", &setI<EconomyTune, &EconomyTune::ownDistMin> },
	{ "economy.ownDistCap", &setI<EconomyTune, &EconomyTune::ownDistCap> },
	{ "economy.destHops", &setI<EconomyTune, &EconomyTune::destHops> },
	{ "economy.sizeBase", &setD<EconomyTune, &EconomyTune::sizeBase> },
	{ "economy.sizeRef", &setD<EconomyTune, &EconomyTune::sizeRef> },
	{ "economy.sizeMin", &setD<EconomyTune, &EconomyTune::sizeMin> },
	{ "economy.sizeMax", &setD<EconomyTune, &EconomyTune::sizeMax> },
	{ "economy.traitBase", &setD<EconomyTune, &EconomyTune::traitBase> },
	{ "economy.traitK", &setD<EconomyTune, &EconomyTune::traitK> },
	{ "economy.farmK", &setD<EconomyTune, &EconomyTune::farmK> },
	{ "economy.forageK", &setD<EconomyTune, &EconomyTune::forageK> },
	{ "economy.fishK", &setD<EconomyTune, &EconomyTune::fishK> },
	{ "economy.launchDelay", &setD<EconomyTune, &EconomyTune::launchDelay> },
	{ "economy.drownedFrac", &setD<EconomyTune, &EconomyTune::drownedFrac> },
	{ "economy.failBack", &setD<EconomyTune, &EconomyTune::failBack> },
	{ "economy.partyMin", &setD<EconomyTune, &EconomyTune::partyMin> },
	{ "economy.partyMax", &setD<EconomyTune, &EconomyTune::partyMax> },
	{ "economy.expandEdict", &setD<EconomyTune, &EconomyTune::expandEdict> },
	{ "civ.maxSettlements", &setI<CivTune, &CivTune::maxSettlements> },
	{ "civ.crowdHops", &setI<CivTune, &CivTune::crowdHops> },
	{ "civ.siteLow", &setD<CivTune, &CivTune::siteLow> },
	{ "civ.siteLowH", &setD<CivTune, &CivTune::siteLowH> },
	{ "civ.siteCoast", &setD<CivTune, &CivTune::siteCoast> },
	{ "civ.siteRoom", &setD<CivTune, &CivTune::siteRoom> },
	// society
	{ "society.baseline.0", &setA<SocietyTune, 7, &SocietyTune::baseline, 0> },
	{ "society.baseline.1", &setA<SocietyTune, 7, &SocietyTune::baseline, 1> },
	{ "society.baseline.2", &setA<SocietyTune, 7, &SocietyTune::baseline, 2> },
	{ "society.baseline.3", &setA<SocietyTune, 7, &SocietyTune::baseline, 3> },
	{ "society.baseline.4", &setA<SocietyTune, 7, &SocietyTune::baseline, 4> },
	{ "society.baseline.5", &setA<SocietyTune, 7, &SocietyTune::baseline, 5> },
	{ "society.baseline.6", &setA<SocietyTune, 7, &SocietyTune::baseline, 6> },
	{ "society.shrineBonus", &setD<SocietyTune, &SocietyTune::shrineBonus> },
	{ "society.templeBonus", &setD<SocietyTune, &SocietyTune::templeBonus> },
	{ "society.prophetBonus", &setD<SocietyTune, &SocietyTune::prophetBonus> },
	{ "society.aweBase", &setD<SocietyTune, &SocietyTune::aweBase> },
	{ "society.aweShrine", &setD<SocietyTune, &SocietyTune::aweShrine> },
	{ "society.aweTemple", &setD<SocietyTune, &SocietyTune::aweTemple> },
	{ "society.aweWonder", &setD<SocietyTune, &SocietyTune::aweWonder> },
	{ "society.aweCap", &setD<SocietyTune, &SocietyTune::aweCap> },
	{ "society.prophetYears", &setD<SocietyTune, &SocietyTune::prophetYears> },
	{ "society.apartYears", &setD<SocietyTune, &SocietyTune::apartYears> },
	{ "society.apartAng", &setD<SocietyTune, &SocietyTune::apartAng> },
	{ "society.birthEvery", &setD<SocietyTune, &SocietyTune::birthEvery> },
	{ "society.maxCultures", &setI<SocietyTune, &SocietyTune::maxCultures> },
	{ "society.arkYears", &setD<SocietyTune, &SocietyTune::arkYears> },
	{ "society.arkPopRef", &setD<SocietyTune, &SocietyTune::arkPopRef> },
	{ "society.arkMin", &setD<SocietyTune, &SocietyTune::arkMin> },
	{ "society.arkMax", &setD<SocietyTune, &SocietyTune::arkMax> },
	{ "society.loveDrift", &setD<SocietyTune, &SocietyTune::loveDrift> },
	{ "society.jadedDecay", &setD<SocietyTune, &SocietyTune::jadedDecay> },
	{ "society.devotionDrift", &setD<SocietyTune, &SocietyTune::devotionDrift> },
	{ "society.pietyA", &setD<SocietyTune, &SocietyTune::pietyA> },
	{ "society.pietyB", &setD<SocietyTune, &SocietyTune::pietyB> },
	{ "society.incomeBase", &setD<SocietyTune, &SocietyTune::incomeBase> },
	{ "society.incomeK", &setD<SocietyTune, &SocietyTune::incomeK> },
	{ "society.sCamp", &setD<SocietyTune, &SocietyTune::sCamp> },
	{ "society.sShrine", &setD<SocietyTune, &SocietyTune::sShrine> },
	{ "society.sTemple", &setD<SocietyTune, &SocietyTune::sTemple> },
	{ "society.witnessLove", &setD<SocietyTune, &SocietyTune::witnessLove> },
	{ "society.witnessJaded", &setD<SocietyTune, &SocietyTune::witnessJaded> },
	{ "society.apartLove", &setD<SocietyTune, &SocietyTune::apartLove> },
	{ "society.secedeAng", &setD<SocietyTune, &SocietyTune::secedeAng> },
	{ "society.fearBonus", &setD<SocietyTune, &SocietyTune::fearBonus> },
	// diplomacy
	{ "diplomacy.relax", &setD<DiplomacyTune, &DiplomacyTune::relax> },
	{ "diplomacy.tradeK", &setD<DiplomacyTune, &DiplomacyTune::tradeK> },
	{ "diplomacy.borderK", &setD<DiplomacyTune, &DiplomacyTune::borderK> },
	{ "diplomacy.aggrK", &setD<DiplomacyTune, &DiplomacyTune::aggrK> },
	{ "diplomacy.loveK", &setD<DiplomacyTune, &DiplomacyTune::loveK> },
	{ "diplomacy.noise", &setD<DiplomacyTune, &DiplomacyTune::noise> },
	{ "diplomacy.warV", &setD<DiplomacyTune, &DiplomacyTune::warV> },
	{ "diplomacy.warStrength", &setD<DiplomacyTune, &DiplomacyTune::warStrength> },
	{ "diplomacy.armyEvery", &setD<DiplomacyTune, &DiplomacyTune::armyEvery> },
	{ "diplomacy.armyFrac", &setD<DiplomacyTune, &DiplomacyTune::armyFrac> },
	{ "diplomacy.warMaxYears", &setD<DiplomacyTune, &DiplomacyTune::warMaxYears> },
	{ "diplomacy.truceYears", &setD<DiplomacyTune, &DiplomacyTune::truceYears> },
	{ "diplomacy.tradeEvery", &setD<DiplomacyTune, &DiplomacyTune::tradeEvery> },
	{ "diplomacy.nukeP", &setD<DiplomacyTune, &DiplomacyTune::nukeP> },
	{ "diplomacy.relTarget", &setD<DiplomacyTune, &DiplomacyTune::relTarget> },
	{ "diplomacy.aggrBase", &setD<DiplomacyTune, &DiplomacyTune::aggrBase> },
	{ "diplomacy.tradeDecay", &setD<DiplomacyTune, &DiplomacyTune::tradeDecay> },
	{ "diplomacy.tradeStep", &setD<DiplomacyTune, &DiplomacyTune::tradeStep> },
	{ "diplomacy.tradeArriveV", &setD<DiplomacyTune, &DiplomacyTune::tradeArriveV> },
	{ "diplomacy.contactHops", &setI<DiplomacyTune, &DiplomacyTune::contactHops> },
	{ "diplomacy.lostShare", &setD<DiplomacyTune, &DiplomacyTune::lostShare> },
	// powers / disasters
	{ "powers.awakenAwe", &setD<PowersTune, &PowersTune::awakenAwe> },
	{ "powers.awakenMaxCost", &setD<PowersTune, &PowersTune::awakenMaxCost> },
	{ "disasters.lightningPerCentury", &setD<DisastersTune, &DisastersTune::lightningPerCentury> },
	{ "disasters.stormsPerCentury", &setD<DisastersTune, &DisastersTune::stormsPerCentury> },
	{ "disasters.droughtsPerCentury", &setD<DisastersTune, &DisastersTune::droughtsPerCentury> },
	{ "disasters.quakesPerCentury", &setD<DisastersTune, &DisastersTune::quakesPerCentury> },
	{ "disasters.plaguesPerCentury", &setD<DisastersTune, &DisastersTune::plaguesPerCentury> },
	{ "disasters.eruptionsPerCentury", &setD<DisastersTune, &DisastersTune::eruptionsPerCentury> },
	{ "disasters.cometK", &setD<DisastersTune, &DisastersTune::cometK> },
	// climate
	{ "climate.sens", &setD<ClimateTune, &ClimateTune::sens> },
	{ "climate.lag", &setD<ClimateTune, &ClimateTune::lag> },
	{ "climate.uptake", &setD<ClimateTune, &ClimateTune::uptake> },
	{ "climate.seaRate", &setD<ClimateTune, &ClimateTune::seaRate> },
	{ "climate.seaTempK", &setD<ClimateTune, &ClimateTune::seaTempK> },
	{ "climate.seaIceGain", &setD<ClimateTune, &ClimateTune::seaIceGain> },
	{ "climate.dustDecay", &setD<ClimateTune, &ClimateTune::dustDecay> },
	// life
	{ "life.grow", &setD<LifeTune, &LifeTune::grow> },
	{ "life.fireP", &setD<LifeTune, &LifeTune::fireP> },
	{ "life.spread", &setD<LifeTune, &LifeTune::spread> },
	// units
	{ "units.speedSettler", &setD<UnitsTune, &UnitsTune::speedSettler> },
	{ "units.speedTrader", &setD<UnitsTune, &UnitsTune::speedTrader> },
	{ "units.speedArmy", &setD<UnitsTune, &UnitsTune::speedArmy> },
	{ "units.seaMul", &setD<UnitsTune, &UnitsTune::seaMul> },
	// chronicle
	{ "chronicle.snapEvery", &setD<ChronicleTune, &ChronicleTune::snapEvery> },
};

} // namespace

void apply(World& w) {
	// Balance overrides (simulation integrator, 2026-11-19), verified with simrun on 6+ seeds x 4,000
	// years against SPEC-SIM 4. Idempotent and deterministic (constants only).
	//
	// Expansion. With the spec's 25-year settler cooldown the home continent filled up by year ~800
	// (48-84 live settlements at year 1,000 against 8-40, population 5-13x the reference curve), and
	// the big leading culture then researched at 1.3-1.7 progress-years per year (sizeF at its 1.25
	// cap), so Industry .. Space and the Ark came 400-700 years early. A settlement now sends a party
	// at most every 180 years (90 under the expand edict, the spec's ratio of about one half): 14-26
	// settlements at 1,000, 94-110 at 3,000 (with the spacing below); population 0.7-1.3x the reference
	// at 500 / 1,000, 2-3.4x at 2,000, 0.8-1.9x at 3,000 / 3,500.
	EconomyTune& E = w.tune<EconomyTune>();
	E.expandCooldown = 180;
	E.expandEdict = 90;
	// Research. Progress needed per age scaled to the rates a real leading culture reaches (0.8 in the
	// Stone age rising to about 1.6 in the Atomic age). 10 passive seeds: Bronze 505-549, Iron
	// 1,041-1,124, Sail 1,591-1,805, Industry 2,239-2,490, Atomic 2,784-3,059, Space 3,230-3,519, Ark
	// 3,449-3,738 (spec nominal 520 / 1,000 / 1,550 / 2,200 / 2,750 / 3,200, Ark 3,300-4,500).
	E.ageNeed = { 375, 520, 750, 1015, 935, 767 };
	// New sites at least 8 hops from owned land (spec 7; crowdHops of canSettle and the owned-distance
	// field follow): with 7 the late world held up to 124 settlements at year 3,000 (target 25-120);
	// now 94-110.
	E.ownDistMin = 8;
	E.ownDistCap = 9;
	// Factory CO2 0.002 x sqrt(pop / 1000) per year (spec 0.0025): the passive anomaly peaked at
	// +4.3..+5.0 (co2 155-193), at the very top of the +2..+5 target and above the spec's "+2..+4.5 by
	// the Ark"; now +3.6..+4.1 (co2 ~150-175), sea rise 0.25-0.38.
	E.factoryCo2 = 0.002;

	// Founders shun ground barely above the sea. About a third of all settlements sat within 0.1 of
	// the sea and over half within 0.3, so the +0.3 sea rise of the industrial ages drowned 90-145
	// settlements per passive world (half of everything ever founded). The low-ground penalty of
	// siteScore (A7: 0.2 fading to 0 at +0.3) is raised to 1.0 fading at +0.6: 21-44 drowned.
	CivTune& C = w.tune<CivTune>();
	C.siteLow = 1.0;
	C.siteLowH = 0.6;
	C.crowdHops = 8;

	// Awe. With the spec formula (0.6 + 0.9 ln(1 + S)) late income was ~5 / yr: a great miracle (150)
	// every ~14 s at 1x against the target 25-60 s, while the Stone-age small one (40) took 14-16 s
	// (target 5-15 s). A flatter curve, 1.25 + 0.31 ln(1 + S): Stone age ~1.5 / yr (a small miracle
	// every ~13 s), later ~2.8 / yr (small every ~7 s, great every ~27 s).
	SocietyTune& S = w.tune<SocietyTune>();
	S.incomeBase = 1.25;
	S.incomeK = 0.31;
	// Endings. With 1 %/yr love fading toward 0, a kind god who blooms, heals and answers prayers every
	// year held the Ark culture's love at +0.12..0.16 and a tyrant at -0.16..-0.23: 'beloved' (love >=
	// 0.2) was never reached (0 of 6 seeds). Love now fades 0.25 %/yr (half-life ~280 years: people
	// remember what their god did for generations). The late ages were also too secular for any god to
	// be beloved (devotion >= 0.35) on the smaller income: the Atomic / Space baselines rise from 0.18 /
	// 0.10 to 0.26 / 0.20 (x piety), and a living prophet adds 0.20 (spec 0.15). Result: kind ends beloved
	// on 9 of 10 seeds, cruel feared on 9 of 10, passive parting on 10 of 10 (its love stays 0).
	S.loveDrift = 0.0025;
	S.baseline[5] = 0.26;
	S.baseline[6] = 0.20;
	S.prophetBonus = 0.2;

	// Relations. With the spec's numbers trade alone held every pair of cultures at v ~ +0.4..+0.7 and
	// 5 of 6 passive worlds never saw a war in 3,500 years. Weaker trade bonus (0.015, spec 0.04), a
	// cooler resting point (-0.05, spec +0.1) and more yearly noise (0.05, spec 0.03): every passive
	// world but one of 10 now sees wars (0-99 declarations, median ~5; the warlike seeds are pairs of
	// aggressive neighbours that fight again soon after each 40-year truce).
	DiplomacyTune& D = w.tune<DiplomacyTune>();
	D.tradeK = 0.015;
	D.relTarget = -0.05;
	D.noise = 0.05;
}

bool set(World& w, std::string_view key, double value) {
	if (!std::isfinite(value)) return false;
	for (const KeyRow& r : kKeys) {
		if (key == r.key) {
			r.set(w, value);
			return true;
		}
	}
	return false;
}

std::vector<std::string> keys() {
	std::vector<std::string> out;
	for (const KeyRow& r : kKeys) out.emplace_back(r.key);
	std::sort(out.begin(), out.end());
	out.erase(std::unique(out.begin(), out.end()), out.end());
	return out;
}

} // namespace dm::balance
