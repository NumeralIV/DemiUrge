// DEMIURGE — Sim/Powers.h : the god's powers and the disasters (SPEC-SIM 3.10, DM.Powers).
//
// ONE namespace, TWO owners, TWO source files (SPEC-SIM 0.1 pair powers.js + disasters.js):
//   [powers]    power table, use and instant effects: powers engineer (A11, key "powers"):
//               Sim/Powers.cpp, SimTests/test_powers.cpp, SimTests/stubs/Powers_stub.cpp. Owns the
//               declarations marked [powers], the power table and PowersTune.
//   [disasters] start, update, tick, natural: disasters engineer (A12, key "disasters"):
//               Sim/Disasters.cpp, SimTests/test_disasters.cpp, SimTests/stubs/Disasters_stub.cpp.
//               Owns the declarations marked [disasters] and DisastersTune.
// Both edit this header: add to your own half, never rename or change the other half. Each
// function is DEFINED in its owner's .cpp only; private helpers go in an anonymous namespace.
// Header written by foundation-api.
//
// Writers (SPEC-SIM 1.1): god.awe spending, god.deeds, stats.miracles (use, once per power:used);
// wet (raise), fert (ash, floods); seaLevel before history (comets); lava and w.disasters
// (disasters half; created only through state::newDisaster inside powers::start). Everything else
// goes through its owner: Terrain (elev), Life (ignite, seed, bloom), Civ (damage, destroy, infect,
// cure; Powers sets inspire / blessed), Society (witness, raiseProphet, makePeace), Units (near,
// kill), Climate (addDust), Sim (setPhase; Powers never writes w.phase).
//
// Real-time pieces (flights, brush pacing, rate limits) count only from the dt arguments, never a
// clock; their clocks live in w.scratch<...>() (not saved).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "State.h"

namespace dm {

// [powers] Starting values from SPEC-SIM 3.10 (powers half).
struct PowersTune {
	double brushMinAng = 0.03, brushMaxAng = 0.15;   // brush ang clamp
	double brushRate = 4;                             // power:used / deed / witness at most 4 per real second
	double aweShortEvery = 1.5;                       // awe:short at most once per press and per 1.5 s per power
	double sculptGenesis = 1.5, sculptHistory = 0.2;  // raise / lower units per second at the brush centre
	double ownedCostMul = 4;                          // sculpting on owned land in history
	int awakenSmall = 600;                            // landmass with fewer habitable cells -> why 'small'
	double awakenPop = 30, awakenAwe = 60, awakenMaxCost = 250;
	double lightningLo = 0.05, lightningHi = 0.12;    // people lost at or next to the cell
	double quakeLo = 0.15, quakeHi = 0.35;            // people lost within the quake ang
	double plagueSeverity = 0.6, blessYears = 30, inspireYears = 40;
	double peaceTruce = 80, peaceRel = 0.4;
	double harmlessFaith = 0.5;                       // good <= 0: half faith to the unharmed (Society applies it)
	double naturalFaith = 0.25;                       // natural events: a quarter of the faith, good 0
	double rainWet = 1;                               // rain: wet toward this
	// (A11 additions: values the spec leaves open)
	double brushDtMax = 0.25;                         // a single brush call counts at most this many real seconds
	double brushMinPay = 0.25;                        // can(): a brush needs this many seconds of Awe to start
	double strokeGap = 0.5;                           // a brush call with dt <= 0 or dt > this starts a new press
	                                                  // (Lower Land's drown guard: only dt <= 0 or another power)
	double maxGameSpeed = 8;                          // awe:short clock: a tick counts as kRealPerTick / this real seconds
	double rainRate = 3;                              // wet moves this share of the way to rainWet per second (x falloff)
	double rainFert = 0.02, rainFertCap = 1.2;        // rain: fertility per second at the centre, never above the cap
	double rainDry = 0.3;                             // rain: good +0.6 where moist < this (or burning, drought, praying)
	double rainGoodDry = 0.6, rainGood = 0.1;
	double bloomRate = 0.25;                          // life::bloom amount per second
	double sculptHarm = -0.5;                         // raise / lower: good for settlements whose land the brush touched
	double quakeRough = 0.5;                          // terrain::roughen amount (largest |change|, world units)
	double quakeUnitAng = 0.5;                        // units within this share of the quake ang die
	double seedMinAng = 0.02, seedMaxAng = 0.5;       // seed: clamp of opts.ang
	// ---- v3, design review (docs/reviews/claude-design-review.md) ----
	// (finding 3) Inspire teaches at once; wrath that hits a capital or an academy sets that culture's
	// research back (progress-years, once per culture per act; natural events never do)
	double inspireScience = 2;
	double setbackLightning = 1, setbackQuake = 20, setbackPlague = 20, setbackMeteor = 40, setbackVolcano = 30;
	// (finding 6) Rain and Bloom cost x this when the brush covers no settlement's territory
	double offTerritoryMul = 2;
	// (finding 6) Consecrate: a shrine becomes a temple (def.cost); a capital with a temple raises a
	// Wonder for wonderCost, once per age of the world
	double wonderCost = 400;
	// (finding 10) in history one press of Lower Land stops a settlement's centre at sea level + this
	// (drown:warning); only a press that begins with the centre already there may drown it
	double drownFloor = 0.05;
	// (finding 5) the age upgrades: Monsoon (rain, Atomic): brush up to monsoonMaxAng, cost x monsoonCostMul;
	// Star-fall (meteor, Space): the player's crater x starfallDepth deep (a lake where it is wet enough)
	double monsoonMaxAng = 0.25, monsoonCostMul = 0.5, starfallDepth = 1.6;
};

// [disasters] Starting values from SPEC-SIM 3.10 (disasters half).
struct DisastersTune {
	// comets
	int maxComets = 3;  double cometFlight = 1.4, cometAng = 0.03;
	double cometFirstSea = -6.0, cometK = 0.2, cometRef = 0.6, cometMin = 0.12, cometMax = 1.0, seaCap = 1.5;
	// meteors
	double meteorFlight = 1.6, meteorAng = 0.05, meteorDepth = 1.6;
	double meteorOuter = 2.5, meteorOuterLoss = 0.5, meteorDust = 0.25;
	// volcanoes
	double volcanoAng = 0.04, volcanoHeight = 1.8, volcanoYears = 12, volcanoDust = 0.03;
	double ashFert = 0.3, ashAngMul = 3;  int lavaMaxCells = 400;
	// natural events per century at calamity 1
	double lightningPerCentury = 6, stormsPerCentury = 3, droughtsPerCentury = 2;
	double quakesPerCentury = 1, plaguesPerCentury = 1, eruptionsPerCentury = 0.4;
	// (v3.1, design review finding 13) the natural rates x calamityAge[world age]: a busier young world, a
	// quieter old one (1 = the table above in every age)
	std::array<double, 7> calamityAge{ 1, 1, 1, 1, 1, 1, 1 };
	double droughtMinYears = 6, droughtMaxYears = 12;
	// protection
	double protectAng = 0.3, protectPop = 200, protectYear = 100, protectAngFew = 0.15;  int protectTowns = 4;
	// ---- choices of the disasters engineer where SPEC-SIM leaves a rule open (A12 report) ----
	double cometDepth = 0.5;                          // comet crater depth (the comet is mostly water)
	double meteorUnitKill = 2.5;                      // units within this x ang die
	double meteorFireIn = 0.9, meteorFireRim = 0.6, meteorFireOut = 2.5;    // ignition chance inside ang, at the rim, reach (x ang)
	double meteorWave = 3.0, meteorWaveAng = 0.9;     // sea impact: tsunami height and reach
	// volcano: eruption profile, lava
	double volcanoErupt = 0.75;                       // lava wells up during this share of dur, then the field cools
	double lavaRate = 1.6;                            // lava units per year at the vent at full power
	double lavaMax = 8;                               // cap per cell (the saved range)
	double lavaMin = 0.02;                            // thinner lava does not flow
	double lavaFlow = 0.6;                            // share of half the surface difference moved per tick
	double lavaHot = 0.05;                            // lava deeper than this burns, clears farms / towns
	double coolLand = 0.03, coolSea = 0.5, coolTail = 0.15;   // share of lava turning to rock per tick
	double rockPerLava = 1.0;                         // elevation gained per unit of cooled lava
	double lavaDamage = 0.04;                         // settlement loses this per newly covered territory cell (max 0.5)
	int touchEvery = 8;                               // terrain::touched at most every this many ticks during an eruption
	// tsunami
	double tsunamiHeight = 1.6, tsunamiAng = 0.6;     // defaults (quake-sized)
	double tsunamiSpeed = 0.03;                       // front advance, rad per tick
	double tsunamiDecay = 0.6;                        // height at the final reach = (1 - decay) x power
	double tsunamiHops = 2;                           // inland reach in hops per unit of local height (1..5)
	double floodFert = 0.05;                          // silt on flooded land
	double waveLoss = 0.1, waveLossK = 0.4, waveOverwhelm = 1.2;   // loss = 0.1 + 0.4 x depth; destroyed at depth >= 1.2
	double hitEvery = 0.5;                            // tsunami:hit at most once per this many real seconds per wave
	// storms
	double stormAng = 0.12, stormPower = 0.8, stormMinYears = 3, stormMaxYears = 6;
	double stormMove = 0.7;                           // chance per tick to move one hop
	double stormWobble = 0.12;                        // heading change per tick (rad, gaussian)
	double stormWet = 0.9;                            // wet raised toward this x power x falloff
	double stormLandDecay = 0.06, stormSeaDecay = 0.005, stormMinPower = 0.15;
	double stormLoss = 0.04;                          // people lost once per storm by a settlement under the eye
	double stormOffset = 0.2;                         // natural storms form this far out at sea
	// droughts
	double droughtAng = 0.2, droughtAngMax = 0.3, droughtPowerMin = 0.6, droughtPowerMax = 1.0;
	// natural quakes / eruptions
	double quakeAng = 0.08, quakeRough = 0.35, quakeWave = 1.4, quakeSeaShare = 0.3;
	double eruptSeaDepth = 0.8;                       // natural vents: land, or sea at most this deep
	int naturalTries = 24;                            // random cells tried per natural event
	// (v3, design review finding 4) the first trial: a mild drought over the first capital in this year
	// (0 = none), once per world; vars.num["disasters_trial"] remembers it
	double trialYear = 45, trialPower = 0.5, trialAng = 0.12, trialYears = 8;
};

namespace powers {

// [powers] power definition (SPEC-SIM 3.10 table as data)
// (v4 mythos, M0) Word: the god speaks, blesses, tests, reveals, renews (SPEC-MYTHOS 2.1; the palette shows it
// between Grace and the genesis powers).
enum class Group : uint8_t { Genesis, Shape, Sky, Wrath, Grace, Word };
enum class Target : uint8_t { Any, Land, Sea, Settlement };
// How the witness radius is given: Radius = wAng radians; BrushRel = wAng x the brush ang;
// Cultures = everyone of the cultures involved (Omen of Peace: "both cultures"); None = no witnesses.
enum class WScope : uint8_t { None, Radius, BrushRel, Cultures };
struct Unlock {
	Phase phase = Phase::History;        // the world must have reached this phase ...
	int age = 0;                         // ... and (w.age >= age ...
	double awe = 0;                      // ... or god.aweMax >= awe); awe <= 0 means "age only" (v3: every power past Bronze)
	bool phaseOnly = false;              // genesis powers: available ONLY in `phase`
	int mythos = 0;                      // (v4 mythos, M0) MythosTune::phase the power needs (0 = not a mythos power);
	                                     // below it the power is Locked whatever the age (phase 0: every Word power)
};
struct PowerDef {
	const char* id;
	const char* name;
	char key;                            // hotkey (Input and HUD read it; nobody else binds letters to powers)
	Group group;
	bool brush;
	double cost;                         // Awe per use (click) or per real second at def.ang (brush)
	double ang;                          // default radius (rad)
	Target target;
	Unlock unlock;
	WScope wScope;
	double wAng;                         // see WScope
	double faith;                        // devotion at the centre (per second for brush powers)
	const char* desc;
};

// [powers] Powers.LIST, in the order of the spec table. Special cases implemented by
// unlocked() / cost(): awaken is free in 'people' and in history whenever no settlement or settler
// is alive, else costs min(250, aweMax) and needs Sail or 450; raise / lower are free during genesis.
inline constexpr PowerDef kPowers[] = {
	{ "comet",     "Comet",         'C', Group::Genesis, false,   0, 0.03, Target::Any,        { Phase::Water,   0,             0,   true  }, WScope::None,     0,    0,    "Hurl an icy comet: water for the young world." },
	{ "seed",      "Seed Life",     'G', Group::Genesis, false,   0, 0.12, Target::Land,       { Phase::Life,    0,             0,   true  }, WScope::None,     0,    0,    "Plant the first green." },
	{ "awaken",    "Awaken",        'K', Group::Genesis, false,   0, 0,    Target::Land,       { Phase::People,  Age::SAIL,     450, false }, WScope::None,     0,    0,    "Wake a people." },
	{ "raise",     "Raise Land",    'U', Group::Shape,   true,    6, 0.06, Target::Any,        { Phase::Water,   0,             0,   false }, WScope::BrushRel, 2,    0.02, "Lift the ground." },
	{ "lower",     "Lower Land",    'J', Group::Shape,   true,    6, 0.06, Target::Any,        { Phase::Water,   0,             0,   false }, WScope::BrushRel, 2,    0.02, "Sink the ground." },
	{ "rain",      "Rain",          'N', Group::Sky,     true,    5, 0.10, Target::Any,        { Phase::History, 0,             0,   false }, WScope::BrushRel, 2,    0.02, "Bring rain." },
	{ "bloom",     "Bloom",         'B', Group::Sky,     true,    6, 0.08, Target::Land,       { Phase::History, Age::BRONZE,   150, false }, WScope::BrushRel, 2,    0.02, "Make the land flourish." },
	{ "lightning", "Lightning",     'L', Group::Wrath,   false,  15, 0,    Target::Any,        { Phase::History, 0,             0,   false }, WScope::Radius,   0.10, 0.05, "Strike with lightning." },
	{ "quake",     "Earthquake",    'Z', Group::Wrath,   false,  60, 0.08, Target::Any,        { Phase::History, Age::BRONZE,   200, false }, WScope::Radius,   0.20, 0.10, "Shake the earth." },
	{ "plague",    "Plague",        'X', Group::Wrath,   false,  80, 0,    Target::Settlement, { Phase::History, Age::IRON,     0,   false }, WScope::Radius,   0.12, 0.05, "Send a sickness." },
	{ "meteor",    "Meteor",        'M', Group::Wrath,   false, 150, 0.05, Target::Any,        { Phase::History, Age::SAIL,     0,   false }, WScope::Radius,   0.45, 0.25, "Call down a star." },
	{ "volcano",   "Volcano",       'V', Group::Wrath,   false, 220, 0.04, Target::Land,       { Phase::History, Age::INDUSTRY, 0,   false }, WScope::Radius,   0.40, 0.25, "Open the earth's fire." },
	{ "heal",      "Heal",          'T', Group::Grace,   false,  40, 0,    Target::Settlement, { Phase::History, 0,             0,   false }, WScope::Radius,   0.10, 0.12, "Lift a sickness." },
	{ "inspire",   "Inspire",       'I', Group::Grace,   false,  60, 0,    Target::Settlement, { Phase::History, Age::BRONZE,   200, false }, WScope::Radius,   0.10, 0.08, "Kindle new ideas." },
	{ "prophet",   "Prophet",       'P', Group::Grace,   false, 120, 0,    Target::Settlement, { Phase::History, Age::IRON,     0,   false }, WScope::Radius,   0.15, 0.15, "Raise a prophet." },
	{ "peace",     "Omen of Peace", 'O', Group::Grace,   false, 100, 0,    Target::Settlement, { Phase::History, Age::IRON,     0,   false }, WScope::Cultures, 0,    0.10, "End a war." },
	{ "consecrate","Consecrate",    'Y', Group::Grace,   false, 150, 0,    Target::Settlement, { Phase::History, Age::BRONZE,   0,   false }, WScope::Radius,   0.10, 0.10, "Raise a shrine into a temple; in a capital, a Wonder." },
	// (v4 mythos, M0) the Word group (SPEC-MYTHOS 2.1, 3.9, 3.16, 3.18, 3.25, 3.30). cost = the cheapest choice;
	// the real cost comes from UseOpts::word (doctrine::wordCost, MythosTune, CharactersTune, DoctrineTune,
	// RenewalTune). Locked while MythosTune::phase < unlock.mythos. renew has no key.
	{ "speak",     "Speak",         '5', Group::Word,    false,  30, 0,    Target::Settlement, { Phase::History, Age::STONE,    0,   false, 1 }, WScope::None,     0,    0,    "Speak to a people: a decree, a warning, a promise, an answer." },
	{ "bless",     "Bless",         '6', Group::Word,    false,  80, 0,    Target::Settlement, { Phase::History, Age::BRONZE,   0,   false, 2 }, WScope::Radius,   0.10, 0.10, "Bless a town: prosperity, a healer, endurance." },
	{ "reveal",    "Reveal",        '7', Group::Word,    false, 150, 0,    Target::Settlement, { Phase::History, Age::BRONZE,   0,   false, 3 }, WScope::None,     0,    0,    "Give a people knowledge, in time or too early." },
	{ "test",      "Test",          '8', Group::Word,    false,  40, 0,    Target::Settlement, { Phase::History, Age::IRON,     0,   false, 2 }, WScope::None,     0,    0,    "Test a notable person." },
	{ "renew",     "Renew",         '\0', Group::Word, false, 600, 0,    Target::Any,        { Phase::History, Age::IRON,     0,   false, 3 }, WScope::None,     0,    0,    "The Great Water or the Great Burning: begin again, with a remnant." },
};
inline constexpr int kPowerCount = int(sizeof(kPowers) / sizeof(kPowers[0]));

// [powers] (v3, design review finding 5) the late ages upgrade a power instead of unlocking one.
// Active while the world's age >= age (powers::upgrade).
struct Upgrade {
	const char* id;                      // the power
	int age;                             // Age::Id
	const char* name;                    // what it is called from then on
	const char* desc;
};
inline constexpr Upgrade kUpgrades[] = {
	{ "rain",   Age::ATOMIC, "Monsoon",   "Rain over a whole country, at half the cost." },
	{ "meteor", Age::SPACE,  "Star-fall", "A falling star that leaves a deep crater lake." },
};
inline constexpr int kUpgradeCount = int(sizeof(kUpgrades) / sizeof(kUpgrades[0]));

// [powers] Powers.byId: the definition or nullptr.
inline const PowerDef* byId(std::string_view id) {
	for (const PowerDef& d : kPowers) if (id == d.id) return &d;
	return nullptr;
}

// [powers] can() result: reason 'ok' | 'locked' | 'awe' | 'target' | 'phase'; why (reason Target, or
// ok with Small for awaken on a landmass with fewer than 600 habitable cells).
// (v4 mythos, M0) Withdrawn: the god keeps a century of silence (SPEC-MYTHOS 3.29).
enum class Reason : uint8_t { Ok, Locked, Awe, Target, Phase, Withdrawn };
enum class Why : uint8_t { None, Sea, Land, Settlement, Barren, Crowded, Small };
inline const char* toStr(Reason r) {
	switch (r) { case Reason::Ok: return "ok"; case Reason::Locked: return "locked"; case Reason::Awe: return "awe"; case Reason::Target: return "target"; case Reason::Withdrawn: return "withdrawn"; default: return "phase"; }
}
inline const char* toStr(Why y) {
	switch (y) { case Why::None: return "none"; case Why::Sea: return "sea"; case Why::Land: return "land"; case Why::Settlement: return "settlement"; case Why::Barren: return "barren"; case Why::Crowded: return "crowded"; default: return "small"; }
}
struct CanResult {
	bool ok = false;
	Reason reason = Reason::Locked;
	Why why = Why::None;
};
// [powers] use() options: ang < 0 = the definition's ang (ignored by click powers except seed;
// brush powers clamp it to 0.03..0.15); dt = real seconds since the previous call (brush powers:
// required every frame, at most PowersTune::brushDtMax counted per call; the first call of a press
// passes 0). Click powers may pass dt too: it only feeds the awe:short rate-limit clock (which
// otherwise advances with w.tick at the fastest game speed, so the limit can never be exceeded).
struct UseOpts {
	double ang = -1;
	double dt = 0;
	WordOpts word;                       // (v4 mythos, M0) what the Word powers say or give (MythosTypes.h)
	// (v4 mythos, M7; additive) prophet (shown as Messenger) at MythosTune::phase >= 1: true = open the MessengerChoice
	// card on the town (SPEC-MYTHOS 3.8: three candidates; nothing is spent now, the card's answer pays the power's
	// cost through powers::pay). false (the default) = the v3.1 unnamed prophet at once, so a god or policy that
	// never asks for the choice keeps the v3.1 history (SPEC-MYTHOS 5.1 rule 2). The client sets it when arming.
	bool choose = false;
};

// [disasters] start() options (JS { power, ang, dur, natural, data }); unset = the type's default.
struct StartOpts {
	std::optional<double> power, ang, dur;
	bool natural = false;
	std::optional<double> eta;           // comet / meteor: flight seconds (default from DisastersTune)
	NumBag data;
};

// ---------------------------------------------------------------- [powers] (Powers.cpp)

// [powers] Powers.unlocked: the world has reached unlock.phase AND (w.age >= unlock.age OR
// god.aweMax >= unlock.awe); genesis powers only in their phase; awaken special case. Read-only.
bool unlocked(const World& w, std::string_view id);
// [powers] (v3) the upgrade of power id active in this world (age reached), or nullptr. Read-only.
const Upgrade* upgrade(const World& w, std::string_view id);
// [powers] (v3, design review finding 10) for the hover tip of Lower Land at cell with brush ang: the
// live settlement a press here would drown (its centre is within ang, at the floor, i.e. within
// drownFloor of the sea, and it already had its drown:warning), or -1. Read-only. (verifier fix) A town
// at the floor that was never warned (the rising sea brought it there) is only warned by its first press.
int drownRisk(const World& w, int cell, double ang = -1);
// [powers] Powers.cost: Awe per use (click) or per real second (brush: def.cost x (ang / def.ang)^2;
// x 4 for raise / lower in history while the brush centre is on owned land; free during genesis);
// ang < 0 = def.ang. 0 for an unknown id. Read-only.
double cost(const World& w, std::string_view id, double ang = -1, int cell = -1);
// [powers] Powers.can(world, id, cell) -> { ok, reason, why }. Read-only. Checked in this order:
// Phase (phase not reached, or a genesis power outside its phase), Locked (unknown id, or age /
// aweMax short), Target (bad cell: why None; land target on sea: Sea; sea target on land: Land; no
// live settlement at / owning the cell: Settlement; awaken: civ::canSettle's Sea / Barren /
// Crowded, and Crowded too while CivTune::maxSettlements settlements live; Omen of Peace on a
// culture at war with nobody: None), Awe (awe < cost; brush powers:
// awe < cost per second x PowersTune::brushMinPay). awaken on a landmass with fewer than 600
// habitable cells (w.cache.landHab) is ok with why Small.
CanResult can(const World& w, std::string_view id, int cell);
// [powers] Powers.use(world, id, cell, { ang, dt }) -> bool: spends Awe and applies the instant
// effect (SPEC-SIM 3.10 "Effects"; comet / meteor / volcano hand off to powers::start). Brush powers
// accumulate dt and at most 4 times per real second emit power:used, count one deed and call
// society::witness once with the accumulated faith. When the god cannot pay: awe:short
// (rate-limited) and false. First successful awaken in 'people' calls sim::setPhase(History) and
// sets god.awe = max(awe, 60). Never fails hard on bad cells or ids (false).
// Order on success: the effect (its own events), then Awe is spent, power:used, the deed, then
// society::witness with the table's wAng / faith / good. Nothing is spent and no power:used is
// emitted when the effect is refused (powers::start nullptr, raiseProphet nullptr, infect false,
// newCulture / found nullptr, Omen of Peace with no war).
// The PLAYER's meteor is started with data["witness"] = 1, so Disasters witnesses it at impact
// (table wAng / faith, good -1 for the settlements it hurt); use() does not witness it. The
// player's volcano is witnessed here, at launch (harmed = settlements within
// DisastersTune::ashAngMul x ang, good -1 for them, 0 for others). Quake under the sea starts its tsunami through powers::start(Tsunami) with default options.
bool use(World& w, std::string_view id, int cell, const UseOpts& opts = {});
// [powers] (v4 mythos, M0) The god pays for a mythos act (a card answer, a word, Look into the heart, a Keeper's
// upkeep): spends `amount` Awe and returns true, or emits awe:short { what } (rate-limited like use) and returns
// false when god.awe < amount. what = an id for the act ("card:judgement", "look", "keeper:upkeep" ...).
// amount <= 0 is free (true). Never a deed, never witnessed (the act itself is, where it shows).
bool pay(World& w, std::string_view what, double amount);

// ---------------------------------------------------------------- [disasters] (Disasters.cpp)

// [disasters] Powers.start: the only way disasters are created (state::newDisaster); field meanings
// per SPEC-SIM 1 (comet / meteor: cell = target, ang = crater radius, eta / flight seconds; volcano:
// vent, cone radius, power 0..1; tsunami: source, final reach, front, power = wave height; storm:
// centre, radius, power; drought: fixed cell and ang, power). Emits meteor:launched / volcano:start /
// tsunami:start / storm / drought { start: true } as appropriate. nullptr if refused (e.g. 3 comets
// in flight, bad cell). Pointer valid until the next disaster is created: keep ->id.
// A12 details: start(Volcano) BUILDS THE CONE itself (terrain::cone, ang x volcanoHeight; callers
// must not build one too; data["noCone"] = 1 skips it) and accepts land or sea vents (a shallow-sea
// vent makes an island). Defaults when an option is unset: comet / meteor ang 0.03 / 0.05, eta 1.4 /
// 1.6 s; volcano ang 0.04, dur 12; tsunami ang 0.6, power (wave height) 1.6; storm ang 0.12, power
// 0.8, dur 3..6 (rng), heading toward the nearest land (or data["hx","hy","hz"]); drought ang
// 0.2..0.3, power 0.6..1, dur 6..12 (rng). Witnessing: Disasters calls society::witness only for
// natural events (quarter faith, good 0) and, for a divine meteor whose data["witness"] = 1, at
// impact with the table's wAng / faith and good -1 for the settlements it hurt (so powers::use may
// defer the meteor's witness to the moment it lands); otherwise powers::use witnesses its acts.
Disaster* start(World& w, DisasterType type, int cell, const StartOpts& opts = {});
// [disasters] Powers.update(world, realDt): real-time pieces: comet / meteor flights count down by
// realDt and resolve impacts (crater, sea level for comets, destruction, fires or tsunami, dust,
// meteor:impact; draws from w.rng); advances the rate-limit clocks. The live game calls it every
// frame (even when paused); headless code never calls it directly: sim::advance does.
void update(World& w, double realDt);
// [disasters] Powers.tick: sim-time pieces: eruptions and lava (<= 400 cells, cools into rock via
// terrain::addElev, dust +0.03 / yr, ash fertility at the end, volcano:end), tsunami fronts
// (tsunami:hit rate-limited), storms (drift, wet, end event), droughts (need.climate at start / end).
// Removes a disaster when t >= dur. Budget <= 0.5 ms with 3 active disasters.
void tick(World& w);
// [disasters] Powers.natural: yearly roll for natural events (per century at calamity 1: lightning
// fires 6, storms 3, droughts 2, quakes 1, plague 1 (only where a city exists), eruptions 0.4),
// scaled by w.settings.calamity, with the protection rules; each emits the events of its miracle
// twin with natural = true and witnesses with a quarter of the faith and good 0.
// A12 verification details: the yearly roll is a jittered schedule (per type, an accumulator of
// expected events and a threshold 0.5..1.5 in w.vars.num["disasters_acc_<type>" / "disasters_thr_<type>"]),
// so long-run rates equal the table and 5,000-year counts stay close to it; no draws at calamity 0.
// Natural storms land on coastal land (coast <= 2) sampled from all such cells, and turn back at the
// edge of a protected zone. An immune city's natural plague is not witnessed. start() drops
// non-finite entries of opts.data; a comet never lowers the sea.
void natural(World& w);

} // namespace powers
} // namespace dm
