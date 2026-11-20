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
template <class T, bool T::*M>
void setB(World& w, double v) { w.tune<T>().*M = v != 0; }

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
	// v3, design review
	{ "economy.blessedK", &setD<EconomyTune, &EconomyTune::blessedK> },
	{ "economy.famineSlack", &setD<EconomyTune, &EconomyTune::famineSlack> },
	{ "economy.expandCooldownAge.0", &setA<EconomyTune, 7, &EconomyTune::expandCooldownAge, 0> },
	{ "economy.expandCooldownAge.1", &setA<EconomyTune, 7, &EconomyTune::expandCooldownAge, 1> },
	{ "economy.expandCooldownAge.2", &setA<EconomyTune, 7, &EconomyTune::expandCooldownAge, 2> },
	{ "economy.expandCooldownAge.3", &setA<EconomyTune, 7, &EconomyTune::expandCooldownAge, 3> },
	{ "society.memoryHalfLife", &setD<SocietyTune, &SocietyTune::memoryHalfLife> },
	{ "society.loreFloor", &setD<SocietyTune, &SocietyTune::loreFloor> },
	{ "society.neglectK", &setD<SocietyTune, &SocietyTune::neglectK> },
	{ "society.neglectAfter", &setD<SocietyTune, &SocietyTune::neglectAfter> },
	{ "society.verdictLove", &setD<SocietyTune, &SocietyTune::verdictLove> },
	{ "society.rememberMin", &setD<SocietyTune, &SocietyTune::rememberMin> },
	{ "society.mixedRatio", &setD<SocietyTune, &SocietyTune::mixedRatio> },
	{ "society.silentYears", &setD<SocietyTune, &SocietyTune::silentYears> },
	{ "society.answeredShare", &setD<SocietyTune, &SocietyTune::answeredShare> },
	{ "society.hoardShare", &setD<SocietyTune, &SocietyTune::hoardShare> },
	{ "society.hoardDrift", &setD<SocietyTune, &SocietyTune::hoardDrift> },
	{ "society.belovedDevotion", &setD<SocietyTune, &SocietyTune::belovedDevotion> },
	{ "society.forgottenDevotion", &setD<SocietyTune, &SocietyTune::forgottenDevotion> },
	{ "society.apartYearsEarly", &setD<SocietyTune, &SocietyTune::apartYearsEarly> },
	{ "society.apartAngEarly", &setD<SocietyTune, &SocietyTune::apartAngEarly> },
	{ "society.prayerScience", &setD<SocietyTune, &SocietyTune::prayerScience> },
	{ "society.prayerBlessed", &setD<SocietyTune, &SocietyTune::prayerBlessed> },
	{ "society.aweCapAge.0", &setA<SocietyTune, 7, &SocietyTune::aweCapAge, 0> },
	{ "society.aweCapAge.1", &setA<SocietyTune, 7, &SocietyTune::aweCapAge, 1> },
	{ "society.aweCapAge.2", &setA<SocietyTune, 7, &SocietyTune::aweCapAge, 2> },
	{ "society.aweCapAge.3", &setA<SocietyTune, 7, &SocietyTune::aweCapAge, 3> },
	{ "society.aweCapAge.4", &setA<SocietyTune, 7, &SocietyTune::aweCapAge, 4> },
	{ "society.aweCapAge.5", &setA<SocietyTune, 7, &SocietyTune::aweCapAge, 5> },
	{ "powers.inspireScience", &setD<PowersTune, &PowersTune::inspireScience> },
	{ "powers.setbackQuake", &setD<PowersTune, &PowersTune::setbackQuake> },
	{ "powers.setbackPlague", &setD<PowersTune, &PowersTune::setbackPlague> },
	{ "powers.setbackMeteor", &setD<PowersTune, &PowersTune::setbackMeteor> },
	{ "powers.setbackVolcano", &setD<PowersTune, &PowersTune::setbackVolcano> },
	{ "powers.setbackLightning", &setD<PowersTune, &PowersTune::setbackLightning> },
	{ "powers.offTerritoryMul", &setD<PowersTune, &PowersTune::offTerritoryMul> },
	{ "powers.drownFloor", &setD<PowersTune, &PowersTune::drownFloor> },
	{ "disasters.trialYear", &setD<DisastersTune, &DisastersTune::trialYear> },
	{ "civ.bandPop", &setD<CivTune, &CivTune::bandPop> },
	{ "sim.silenceMinYear", &setD<SimTune, &SimTune::silenceMinYear> },
	// v3.1, design review findings 11 and 13
	{ "diplomacy.borderAge.0", &setA<DiplomacyTune, 7, &DiplomacyTune::borderAge, 0> },
	{ "diplomacy.borderAge.1", &setA<DiplomacyTune, 7, &DiplomacyTune::borderAge, 1> },
	{ "diplomacy.borderAge.2", &setA<DiplomacyTune, 7, &DiplomacyTune::borderAge, 2> },
	{ "diplomacy.borderAge.3", &setA<DiplomacyTune, 7, &DiplomacyTune::borderAge, 3> },
	{ "diplomacy.borderAge.4", &setA<DiplomacyTune, 7, &DiplomacyTune::borderAge, 4> },
	{ "diplomacy.borderAge.5", &setA<DiplomacyTune, 7, &DiplomacyTune::borderAge, 5> },
	{ "diplomacy.borderAge.6", &setA<DiplomacyTune, 7, &DiplomacyTune::borderAge, 6> },
	{ "diplomacy.envyK", &setD<DiplomacyTune, &DiplomacyTune::envyK> },
	{ "diplomacy.favourRef", &setD<DiplomacyTune, &DiplomacyTune::favourRef> },
	{ "diplomacy.sharedWrathK", &setD<DiplomacyTune, &DiplomacyTune::sharedWrathK> },
	{ "diplomacy.plagueK", &setD<DiplomacyTune, &DiplomacyTune::plagueK> },
	{ "diplomacy.plagueSick", &setD<DiplomacyTune, &DiplomacyTune::plagueSick> },
	{ "diplomacy.zealK", &setD<DiplomacyTune, &DiplomacyTune::zealK> },
	{ "diplomacy.zealRef", &setD<DiplomacyTune, &DiplomacyTune::zealRef> },
	{ "diplomacy.faithK", &setD<DiplomacyTune, &DiplomacyTune::faithK> },
	{ "diplomacy.faithDev", &setD<DiplomacyTune, &DiplomacyTune::faithDev> },
	{ "diplomacy.allyForm", &setD<DiplomacyTune, &DiplomacyTune::allyForm> },
	{ "diplomacy.allyBreak", &setD<DiplomacyTune, &DiplomacyTune::allyBreak> },
	{ "diplomacy.allyMax", &setI<DiplomacyTune, &DiplomacyTune::allyMax> },
	{ "diplomacy.allyCall", &setB<DiplomacyTune, &DiplomacyTune::allyCall> },
	{ "diplomacy.allyDeter", &setB<DiplomacyTune, &DiplomacyTune::allyDeter> },
	{ "diplomacy.allyV", &setD<DiplomacyTune, &DiplomacyTune::allyV> },
	{ "diplomacy.omenTarget", &setD<DiplomacyTune, &DiplomacyTune::omenTarget> },
	{ "diplomacy.omenYears", &setD<DiplomacyTune, &DiplomacyTune::omenYears> },
	{ "diplomacy.omenTension", &setD<DiplomacyTune, &DiplomacyTune::omenTension> },
	{ "diplomacy.omenCalmTruce", &setD<DiplomacyTune, &DiplomacyTune::omenCalmTruce> },
	{ "diplomacy.omenSworn", &setB<DiplomacyTune, &DiplomacyTune::omenSworn> },
	{ "diplomacy.omenCalm", &setB<DiplomacyTune, &DiplomacyTune::omenCalm> },
	{ "diplomacy.nukeRepeatMul", &setD<DiplomacyTune, &DiplomacyTune::nukeRepeatMul> },
	{ "diplomacy.blameMin", &setD<DiplomacyTune, &DiplomacyTune::blameMin> },
	{ "diplomacy.thankMin", &setD<DiplomacyTune, &DiplomacyTune::thankMin> },
	{ "diplomacy.thankDevotion", &setD<DiplomacyTune, &DiplomacyTune::thankDevotion> },
	{ "diplomacy.blameLove", &setD<DiplomacyTune, &DiplomacyTune::blameLove> },
	{ "diplomacy.peaceV", &setD<DiplomacyTune, &DiplomacyTune::peaceV> },
	{ "diplomacy.exhaustYear", &setD<DiplomacyTune, &DiplomacyTune::exhaustYear> },
	{ "society.favourHalfLife", &setD<SocietyTune, &SocietyTune::favourHalfLife> },
	{ "society.rainFamineAge.0", &setAI<SocietyTune, 7, &SocietyTune::rainFamineAge, 0> },
	{ "society.rainFamineAge.1", &setAI<SocietyTune, 7, &SocietyTune::rainFamineAge, 1> },
	{ "society.rainFamineAge.2", &setAI<SocietyTune, 7, &SocietyTune::rainFamineAge, 2> },
	{ "society.rainFamineAge.3", &setAI<SocietyTune, 7, &SocietyTune::rainFamineAge, 3> },
	{ "society.rainFamineAge.4", &setAI<SocietyTune, 7, &SocietyTune::rainFamineAge, 4> },
	{ "society.rainFamineAge.5", &setAI<SocietyTune, 7, &SocietyTune::rainFamineAge, 5> },
	{ "society.rainFamineAge.6", &setAI<SocietyTune, 7, &SocietyTune::rainFamineAge, 6> },
	{ "society.maxPrayers", &setI<SocietyTune, &SocietyTune::maxPrayers> },
	{ "society.hoardFloor", &setD<SocietyTune, &SocietyTune::hoardFloor> },
	{ "diplomacy.feudK", &setD<DiplomacyTune, &DiplomacyTune::feudK> },
	{ "diplomacy.warWeary", &setD<DiplomacyTune, &DiplomacyTune::warWeary> },
	{ "diplomacy.warStall", &setD<DiplomacyTune, &DiplomacyTune::warStall> },
	{ "diplomacy.warredMax", &setD<DiplomacyTune, &DiplomacyTune::warredMax> },
	{ "diplomacy.warredHalfLife", &setD<DiplomacyTune, &DiplomacyTune::warredHalfLife> },
	{ "diplomacy.omenCalmGood", &setD<DiplomacyTune, &DiplomacyTune::omenCalmGood> },
	{ "diplomacy.feudMax", &setD<DiplomacyTune, &DiplomacyTune::feudMax> },
	{ "diplomacy.omenWide", &setB<DiplomacyTune, &DiplomacyTune::omenWide> },
	{ "diplomacy.omenNoEnvy", &setB<DiplomacyTune, &DiplomacyTune::omenNoEnvy> },
	{ "diplomacy.omenAge", &setI<DiplomacyTune, &DiplomacyTune::omenAge> },
	{ "disasters.calamityAge.0", &setA<DisastersTune, 7, &DisastersTune::calamityAge, 0> },
	{ "disasters.calamityAge.1", &setA<DisastersTune, 7, &DisastersTune::calamityAge, 1> },
	{ "disasters.calamityAge.2", &setA<DisastersTune, 7, &DisastersTune::calamityAge, 2> },
	{ "disasters.calamityAge.3", &setA<DisastersTune, 7, &DisastersTune::calamityAge, 3> },
	{ "disasters.calamityAge.4", &setA<DisastersTune, 7, &DisastersTune::calamityAge, 4> },
	{ "disasters.calamityAge.5", &setA<DisastersTune, 7, &DisastersTune::calamityAge, 5> },
	{ "disasters.calamityAge.6", &setA<DisastersTune, 7, &DisastersTune::calamityAge, 6> },
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
	// (v3, design review finding 4) a shorter Stone age: 250 progress-years (Bronze ~350-400)
	E.ageNeed[0] = 250;
	// ... and the later ages need about 11 % more (the faster early world researches faster). 8 passive
	// seeds x 4,500 years: Bronze 309-334, Iron 872-996, Sail 1,534-1,809, Industry 2,301-2,588, Atomic
	// 2,916-3,229, Space 3,412-3,748, Ark 3,631-3,967; a kind god launches 170-470 years earlier, a cruel
	// one 170-450 years later, inspire-spam reaches Space 300-580 years earlier
	E.ageNeed[1] = 575;
	E.ageNeed[2] = 830;
	E.ageNeed[3] = 1125;
	E.ageNeed[4] = 1035;
	E.ageNeed[5] = 850;
	// (v3, design review finding 4) settlers leave every 60 years in the Stone age (the review proposed 50 /
	// 90 / 180), 260 in Bronze, 240 in Iron and 180 after (the edict halves it): the first minutes have
	// more than one camp to watch while year 1,000 stays inside 8-40 settlements
	E.expandCooldownAge = { 60, 260, 240, 180, 180, 180, 180 };
	// (v3, design review finding 3) Inspire is a real push: an inspired settlement's people learn twice as fast
	E.inspireF = 1.0;
	// (v3, design review finding 9) a famine is a real shortfall: it counts only while the people are 5 %
	// above what the land feeds (2 % before), so the slow ebb of a bloomed or rained field is not one
	E.famineSlack = 0.05;
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
	// (v3, design review) a flatter curve again, 2.0 + 0.17 ln(1 + S): hoarding now costs devotion (and so
	// income), and the larger v3 world earned a great miracle in under 25 s with 0.31; 8 seeds x 4
	// policies: Stone small every 9-10 s, later small every 7.0-7.9 s, great every 25.4-28.3 s
	S.incomeBase = 2.0;
	S.incomeK = 0.17;
	// (v3, design review finding 6) hoarding costs 1.5 % of devotion a year
	S.hoardDrift = 0.015;
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

	// ---- v3.1, design review findings 11 and 13 (sim-findings-2), 8 seeds x 17 policies x 4,500 years ----
	// Tuning first. Every caravan that arrived added +0.02 to its pair's relations: with dozens of market
	// towns that was +0.1 a year, so from the Sail age every pair of peoples sat at v +0.4..+0.7 and only a
	// few pairs of aggressive neighbours ever fought (passive at war in 0.6-13 % of years, median 3.4 %).
	// A caravan now adds 0.004; trade itself (tradeK) still warms a pair. Aggression counts a little less
	// (it decided which seeds had wars at all), frontiers chafe more as the peoples grow.
	D.tradeArriveV = 0.004;
	D.aggrK = 0.02;
	D.relTarget = -0.2;
	D.borderAge = { 1, 1, 1.3, 1.6, 1.6, 1.6, 1.6 };
	// Rules second: the god in the relations (rates: the fading sums x ln 2 / 30 years)
	D.envyK = 0.002;
	D.favourRef = 0.015;
	D.sharedWrathK = 0.006;
	D.plagueK = 0.02;
	D.zealK = 0.002;
	D.faithK = 0.006;
	D.blameMin = 0.004;
	D.thankMin = 0.004;
	D.thankDevotion = 0.05;
	D.blameLove = 0.05;
	// alliances that matter: allies defend each other, and an aggressor must match the whole alliance
	D.allyForm = 0.55;
	D.allyBreak = 0.15;
	D.allyMax = 2;
	D.allyCall = true;
	D.allyDeter = true;
	// the Omen of Peace: a sworn peace (80 years at +0.3), and it may calm a neighbour on the brink
	D.omenSworn = true;
	D.omenCalm = true;
	D.omenTension = -0.4;
	// a war of nothing (no army from either side for 25 years) peters out in a draw
	D.warStall = 25;
	// the bomb is feared after the first exchange
	D.nukeRepeatMul = 0.1;
	// old enemies tire of each other: each further war of a pair lengthens its truce by its length again (at most x5)
	D.feudK = 1;
	D.feudMax = 4;
	// (sim-findings-2) hoarding never drags devotion below 0.3 (seed 7's shrine-less trap)
	S.hoardFloor = 0.3;
	// a people still weary from its last war (exhaustion above 0.3, about 20 years of peace) starts no new one
	D.warWeary = 0.3;
	// ... nor one that spent about a sixth of the last century at war (remembered war years, half-life 80)
	D.warredMax = 18;
	// finding 13: two estranged towns secede after 40 years beyond 0.28 rad before the Iron age (v3: 60 / 0.40):
	// a second people at 250-370 (was 340-640), the first war at 335-925 (was 640-2,350)
	S.apartAngEarly = 0.28;
	S.apartYearsEarly = 40;
	// wars now slow every people; the god's quake on a capital or an academy costs 50 progress-years (v3: 20) so
	// a cruel god still sets the world back 150+ years (review 3)
	PowersTune& P = w.tune<PowersTune>();
	P.setbackQuake = 50;
	// the wars cost the passive world about a century (seed 7, at war in a quarter of its years, reached Atomic in
	// 3,415 and Space in 3,957 against <= 3,400 / 3,900): the Sail, Industry and Atomic ages need 5 % less
	E.ageNeed[3] = 1070;
	E.ageNeed[4] = 985;
	E.ageNeed[5] = 810;
	// finding 13: a busier young world and a quieter old one; late peoples pray for bread after two hungry years
	DisastersTune& DT = w.tune<DisastersTune>();
	DT.calamityAge = { 1.4, 1.2, 1, 1, 0.85, 0.75, 0.7 };
	S.rainFamineAge = { 1, 1, 1, 1, 2, 2, 2 };
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
