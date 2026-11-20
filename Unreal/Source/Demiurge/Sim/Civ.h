// DEMIURGE — Sim/Civ.h : settlements, territory, capacity, growth, research (SPEC-SIM 3.7, DM.Civ).
//
// ONE namespace, TWO owners, TWO source files (SPEC-SIM 0.1 pair civ.js + economy.js):
//   [civ]     lifecycle half: civ engineer (A7, key "civ"): Sim/Civ.cpp, SimTests/test_civ.cpp,
//             SimTests/stubs/Civ_stub.cpp. Owns the declarations marked [civ] and CivTune.
//   [economy] economy half: economy engineer (A8, key "economy"): Sim/Economy.cpp,
//             SimTests/test_economy.cpp, SimTests/stubs/Economy_stub.cpp. Owns the declarations
//             marked [economy] and EconomyTune.
// Both edit this header: add to your own half, never rename or change the other half. Each
// function is DEFINED in its owner's .cpp only; private helpers go in an anonymous namespace (the
// two files are linked together). Header written by foundation-api.
//
// Writers (SPEC-SIM 1.1): owner, farm, urban; Civ lowers veg on new farmland; settlement pop, K,
// growth, tier, radius, famine, plague (only via infect / cure), immune, inspire / blessed countdown
// (Powers sets them), lastExpand, b[] except WONDER, culture, capital, dead, deadYear, cause;
// culture age, science, capital; w.age. Settlements are created only through state::newSettlement
// inside civ::found.
//
// Entity arguments are ids (sid = settlement id, cultureId): the settlement vector may reallocate
// when settlements are founded, so never hold a Settlement& across a call that may found one.
#pragma once

#include <array>
#include <optional>
#include <vector>

#include "State.h"

namespace dm {

// [civ] Starting values from SPEC-SIM 3.7 (lifecycle half).
struct CivTune {
	int maxSettlements = 160;            // found() refuses beyond this many live settlements
	double awakenDevotion = 0.6, awakenLove = 0.3;     // an awakened tribe
	double settlerDevotion = 0.5, settlerLove = 0;     // defaults when FoundOpts gives none
	double barrenHab = 0.05;             // canSettle: hab <= this -> 'barren'
	int crowdHops = 7;                   // canSettle: within this many hops of owned land -> 'crowded'
	double minPeople = 3;                // damage() destroys below this
	// [civ] added by A7 (choices where the spec leaves room; see Civ.cpp's top comment)
	double roundPad = 0.25;              // claim(): territory = land within (radius + roundPad) x geo.step of the centre
	double campUrban = 0.1;              // found(): built-up fraction of a new camp's centre cell (at least)
	// siteScore = hab + siteRoom x room + siteFresh x fresh + siteCoast x coast - siteLow x low
	double siteRoom = 0.6;               // room = mean hab of the free land within siteRoomHops (sea, lakes, owned = 0)
	int siteRoomHops = 4;
	double siteFresh = 0.12;             // fresh water at <= 1 hop (0.6 of it at 2 hops, 0.3 at 3)
	double siteCoast = 0.08;             // the sea within 1..3 hops (half at 4): fish now, a port later
	double siteLow = 0.2, siteLowH = 0.3;   // penalty for ground barely above the sea, fading to 0 at siteLowH
	// (v3, design review finding 4) an awakening wakes two bands: the second of bandPop people camps
	// bandMinHops..bandMaxHops from the first (civ::bandSite)
	double bandPop = 20;  int bandMinHops = 4, bandMaxHops = 6;
};

// [economy] Starting values from SPEC-SIM 3.7 (economy half).
struct EconomyTune {
	// territory
	std::array<int, 5> radius{ 2, 3, 4, 5, 6 };          // hops by tier
	int reclaimYears = 10;                                // re-claim every 10 years (staggered by id)
	// capacity
	std::array<double, 7> techYield{ 1, 2.5, 5, 10, 25, 60, 120 };   // by culture age
	double farmK = 22, fertCap = 1.2, arableHab = 0.15;  // farmPot = farmK x min(fert, fertCap) x suit x techYield
	double forageK = 3;                                   // forage = forageK x (0.6 veg + 0.4 fauna)
	double fishK = 4, fishTechCap = 10;                   // fish = fishK x fauna x (1 + port) x sqrt(min(techYield, cap))
	double granaryK = 1.15, plantK = 1.25;
	double farmTarget = 1.15, farmRate = 0.25, farmRelax = 0.1;   // farm[] display / ecology rule
	std::array<double, 5> urbanCentre{ 0.1, 0.25, 0.5, 0.8, 1 };   // by tier; rings 0.5 (city), 0.8 / 0.4 (metropolis)
	// growth, famine, plague
	double growthR = 0.03, granaryR = 0.3, blessedR = 0.3, loveR = 0.2;
	double famineLoss = 0.12, famineGap = 0.5;            // pop -= min(famineLoss x pop, famineGap x (pop - Keff))
	int famineEvent = 3;                                  // settlement:famine when famine reaches this
	double plagueDeath = 0.08, plagueFade = 0.15, plagueCure = 0.05;
	int plagueHops = 8;                                   // spread to settlements within this many hops
	// tiers
	std::array<double, 5> gate{ 0, 60, 400, 1500, 12000 };          // pop to reach each tier
	std::array<int, 5> gateAge{ 0, 0, Age::BRONZE, Age::IRON, Age::INDUSTRY };
	double demote = 0.5;                                  // demote below demote x the current tier's gate
	// buildings
	double shrinePop = 30, shrineDevotion = 0.3, templeDevotion = 0.35;
	double launchDelay = 20;                              // years after the culture reached Space
	// expansion
	double expandK = 0.85, expandPop = 40, expandCooldown = 25, expandEdict = 12;
	// (v3, design review finding 4) the settler cooldown by the culture's age (0 = expandCooldown);
	// under the expand edict it is x expandEdict / expandCooldown
	std::array<double, 7> expandCooldownAge{ 0, 0, 0, 0, 0, 0, 0 };
	int searchesPerYear = 2;
	double landMaxCost = 45, navalMaxCost = 120;
	double campKMin = 60;
	int ownDistMin = 7, ownDistCap = 8, destHops = 9;
	double pathW = 0.01;                                  // best = siteScore - pathW x path cost
	double partyFrac = 0.12, partyMin = 12, partyMax = 500;
	double failBack = 15;                                 // no site: lastExpand = year - failBack
	// hazards
	double drownedFrac = 0.6;  int drownedHops = 12;  double drownWater = 0.3;
	// research
	double rateMin = 0.4, rateMax = 2.0;
	std::array<double, 6> ageNeed{ 420, 500, 600, 750, 620, 520 };  // progress to leave each age
	double academyF = 0.15, marketF = 0.10, factoryF = 0.10, inspireF = 0.25, catchUp = 1.5;
	// emissions (Climate::emit)
	double factoryCo2 = 0.0025, spaceCo2Mul = 0.5;
	// ---- [economy] choices the spec leaves open (added by the economy engineer, A8)
	// suit(temp, moist): temperature 0 at suitT[0], 1 from suitT[1] to suitT[2], 0 at suitT[3];
	// water sat((m - suitM[0]) / (suitM[1] - suitM[0])), at most 1 - suitWetLoss x (m - suitWet);
	// fresh water within irrigateHops hops counts as moisture irrigateMoist at least.
	std::array<double, 4> suitT{ -2, 10, 24, 38 };
	std::array<double, 2> suitM{ 0.08, 0.40 };
	double suitWet = 0.8, suitWetLoss = 0.5;
	int irrigateHops = 1;  double irrigateMoist = 0.4;
	double mineSlope = 1.0;                               // mine: a territory cell this steep (or ROCK / SNOWPEAK)
	std::array<double, 3> urbanRing{ 0.5, 0.8, 0.4 };     // city ring; metropolis rings 1 and 2
	double urbanFade = 0.01;                              // other territory cells: urban fades 1 %/yr (ruins)
	double famineSlack = 0.02;                            // famine counts only while pop > Keff x (1 + slack)
	double plagueSpreadP = 0.3, plagueSpreadSev = 0.8;    // per neighbour and year: chance P x plague, severity Sev x plague
	double lavaKill = 0.05;                               // lava on the centre above this destroys ('lava')
	int maxCandidates = 96;                               // expansion: valid sites evaluated per search (nearest first)
	double sizeBase = 0.5, sizeRef = 12, sizeMin = 0.5, sizeMax = 1.25;   // sizeF = clamp(b + b sqrt(n / ref), min, max)
	double traitBase = 0.85, traitK = 0.3;                // traitF = base + k x curiosity
	// (v3, design review findings 3, 9) a blessed settlement (Heal, an answered rain / bloom prayer)
	// feeds more: K x blessedK while blessed > 0
	double blessedK = 1.2, blessedFade = 10;
};

namespace civ {

// [civ] Civ.TIERS
inline constexpr const char* kTiers[5] = { "camp", "village", "town", "city", "metropolis" };

// [civ] canSettle reasons ('ok' | 'sea' (sea or lake) | 'barren' | 'crowded')
enum class SettleReason : uint8_t { Ok, Sea, Barren, Crowded };
inline const char* toStr(SettleReason r) {
	switch (r) { case SettleReason::Ok: return "ok"; case SettleReason::Sea: return "sea"; case SettleReason::Barren: return "barren"; default: return "crowded"; }
}
struct SettleCheck {
	bool ok = false;
	SettleReason reason = SettleReason::Sea;
};
// [civ] found() 6th argument: initial devotion / love (unset = by origin: awaken 0.6 / 0.3,
// settlers 0.5 / 0)
struct FoundOpts {
	std::optional<double> devotion, love;
	// (v3, design review finding 4) the second band of an awakening: it may camp within crowdHops of
	// its sister camp (canSettle's 'crowded' is not checked; sea / lake / barren still are)
	bool band = false;
};

// ---------------------------------------------------------------- [civ] lifecycle (Civ.cpp)

// [civ] Civ.found: a new settlement of pop people at cell; nullptr when the site is invalid
// (canSettle not ok) or maxSettlements are alive. Claims territory and computes K (civ::capacity)
// at once; first settlement of a culture without a capital becomes its capital. Emits
// settlement:founded. Pointer valid until the next settlement is founded: keep ->id.
Settlement* found(World& w, int cell, int cultureId, double pop, FoundedBy by = FoundedBy::Settlers, const FoundOpts& opts = {});
// [civ] Civ.claim: recompute territory (owner[] and s.cells): land cells within s.radius hops, not
// sea, not owned by another settlement; a contested unowned cell goes to the nearer centre (ties:
// lower id). dirty.color. A7 choices: "within radius hops" = the round disc of angle
// (radius + CivTune::roundPad) x geo.step reached over land inside the disc (on flat land at L 6:
// 19 / 37 / 63 / 97 / 137 cells for radius 2..6, the hop disc has 19 / 37 / 61 / 91 / 127); lake cells are territory; a contested cell the
// rival wins is handed to that rival at once if unowned. s.cells is ascending.
void claim(World& w, int sid);
// [civ] Civ.rebuild: rebuild every live settlement's cells from owner[] WITHOUT changing owner
// (after a load).
void rebuild(World& w);
// [civ] Civ.destroy: dead = true, deadYear, cause, release territory (urban stays as ruins), elect a
// new capital if needed; emits settlement:destroyed; dirty.objects / color. pop keeps the number of
// people lost with it (always iterate civ::live, never sum dead settlements); Cause::None -> Abandoned.
void destroy(World& w, int sid, Cause cause);
// [civ] Civ.damage: kill frac of the people; returns the number killed; destroys the settlement
// (with cause) if fewer than minPeople remain.
double damage(World& w, int sid, double frac, Cause cause);
// [civ] Civ.capture: conquest: change culture, emit settlement:captured { how: War }; re-elect the
// capitals of both cultures.
void capture(World& w, int sid, int cultureId);
// [civ] Civ.setCulture: peaceful change of membership (schisms): settlement:captured { how: Schism }.
void setCulture(World& w, int sid, int cultureId);
// [civ] Civ.electCapital: the most populous live settlement of the culture becomes capital (writes
// culture.capital and s.capital for each of its settlements); a new capital starts its launch site
// and Ark from 0 (b[LAUNCH] = 0, ark = 0 on the old one). Returns the id or -1.
int electCapital(World& w, int cultureId);
// [civ] Civ.infect: false if dead or immune > 0; plague = max(plague, severity); emits
// settlement:plague { start: true } when it was not ill.
bool infect(World& w, int sid, double severity = 0.6);
// [civ] Civ.cure: plague = 0, immune = immuneYears; emits settlement:plague { start: false } if it
// was ill; false if dead.
bool cure(World& w, int sid, double immuneYears = 30);
// [civ] Civ.at: live settlement centred on the cell or owning it, else -1. Read-only.
int at(const World& w, int cell);
// [civ] Civ.near: live settlements whose centre is within ang of the cell (nearest first; ties by
// id). Read-only.
std::vector<int> near(const World& w, int cell, double ang);
// [civ] Civ.live: ids of the live settlements, ascending (may be cached per tick in scratch; must
// reflect founds / destroys made earlier in the same tick). Read-only.
std::vector<int> live(const World& w);
// [civ] Civ.canSettle: { ok, reason }: 'sea' (sea or lake cell), 'barren' (hab <= 0.05),
// 'crowded' (within 7 hops of land owned by a live settlement, by any path). Read-only. An invalid
// cell answers { false, Sea }.
SettleCheck canSettle(const World& w, int c);
// [civ] Civ.siteScore: hab plus bonuses (fresh water, coast, room around it). Read-only.
double siteScore(const World& w, int c);
// [civ] Civ.bestSite: best site on the landmass with the most habitable cells (w.cache.landHab), -1
// if none. Read-only. Exact argmax of siteScore over canSettle-ok cells (ties: lower cell); if that
// landmass is full, the best free site on any landmass. Uses its own landmass labels when w.cache is
// not built for this world.
int bestSite(const World& w);
// [civ] Civ.ageProgress: science toward the next age, 0..1 (1 at the last age). Read-only.
double ageProgress(const World& w, int cultureId);
// [civ] (v3, design review finding 4) the camp of an awakening's second band: the best siteScore
// cell bandMinHops..bandMaxHops hops from `cell` (BFS over land), on land that is not sea, lake,
// barren or owned; -1 if none. Read-only.
int bandSite(const World& w, int cell);
// [civ] (v3, design review finding 6) Consecrate: settlement sid's shrine becomes a temple (b[TEMPLE],
// settlement:building). False if it is dead, has no shrine or already has a temple.
bool consecrate(World& w, int sid);

// ---------------------------------------------------------------- [economy] (Economy.cpp)

// [economy] Civ.yearly: for every live settlement in id order: territory radius by tier and
// re-claim (tier change, every 10 years staggered by id), capacity and farm[] / urban[], growth,
// famine, plague (deaths, fading, spread via infect), countdown of blessed / inspire / immune, tier
// gates with hysteresis, buildings, factory emissions, hazards (sea rise -> evacuate 60 % then
// destroy 'drowned'; water > 0.3 'drowned'; lava 'lava'), expansion (at most 2 searches per year,
// Path::reach, settlers through Units::spawn); then research per culture (culture:age, world:age).
// Budget <= 3 ms with 100 settlements at L = 6.
void yearly(World& w);
// [economy] Civ.capacity: K (people) for settlement s from the rules of SPEC-SIM 3.7; K never
// depends on farm[] or on the population. s may be a candidate that is not in the world (fill
// cell, culture, tier, b[]): when s.cells is empty the territory is estimated as the land cells
// within EconomyTune::radius[tier] hops of s.cell not owned by another settlement. Read-only.
double capacity(const World& w, const Settlement& s);
// [economy] (v3, design review finding 3) the god moves a culture's progress: add `years`
// progress-years to its science (Inspire, answered prayers); a negative amount sets it back, never
// below the start of its current age (ages are not lost). Returns the change made. Age-ups happen at
// the next research step (yearly).
double addScience(World& w, int cultureId, double years);
// [economy] (v3) wrath that hits a capital or an academy sets that culture's research back `years`
// (once per culture per act): every culture whose capital, or a settlement with an academy, is among
// `harmed` (settlement ids). Returns how many cultures were set back.
int strikeKnowledge(World& w, const std::vector<int>& harmed, double years);

} // namespace civ
} // namespace dm
