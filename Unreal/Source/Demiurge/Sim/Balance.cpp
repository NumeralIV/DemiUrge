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
	{ "economy.radius.4", &setAI<EconomyTune, 5, &EconomyTune::radius, 4> },
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
	// balance overrides (simulation integrator): e.g.
	//   w.tune<EconomyTune>().growthR = 0.032;
	// Must be idempotent and deterministic. Empty until integration.
	(void)w;
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
