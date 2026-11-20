// DEMIURGE — Sim/Society.h : belief, Awe, prayers, prophets, cultures, the Ark; relations, war,
// trade, nukes (SPEC-SIM 3.8, DM.Society).
//
// ONE namespace, TWO owners, TWO source files (SPEC-SIM 0.1 pair society.js + diplomacy.js):
//   [society]   belief half: society engineer (A9, key "society"): Sim/Society.cpp,
//               SimTests/test_society.cpp, SimTests/stubs/Society_stub.cpp. Owns the declarations
//               marked [society] and SocietyTune.
//   [diplomacy] relations half: diplomacy engineer (A10, key "diplomacy"): Sim/Diplomacy.cpp,
//               SimTests/test_diplomacy.cpp, SimTests/stubs/Diplomacy_stub.cpp. Owns the
//               declarations marked [diplomacy] and DiplomacyTune.
// Both edit this header: add to your own half, never rename or change the other half. Each
// function is DEFINED in its owner's .cpp only; private helpers go in an anonymous namespace.
// Header written by foundation-api.
//
// Writers (SPEC-SIM 1.1): settlement devotion, love, jaded, prayer, apart, ark, b[WONDER];
// culture pop, towns, dead, prophet, edict, edictSince, exhaustion, seen, traits, lastSplit
// (epithet / godName changes: Chronicle::updateEpithets); god.awe income (Powers spends; awaken sets
// the starting 60), god.aweMax, god.mercy, god.wrath; stats.wars (declareWar); rad (nukes; Life
// decays); road (trade); vars.rel, vars.ark, vars.lastNuke. Cultures are created only through
// state::newCulture inside society::newCulture.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "State.h"

namespace dm {

// [society] Starting values from SPEC-SIM 3.8 (belief half).
struct SocietyTune {
	// housekeeping
	double jadedDecay = 0.05, loveDrift = 0.01;           // per year
	// belief
	double devotionDrift = 0.02;                          // per year toward the baseline
	std::array<double, 7> baseline{ 0.55, 0.6, 0.55, 0.45, 0.3, 0.18, 0.1 };   // by age
	double pietyA = 0.6, pietyB = 0.8;                    // x (pietyA + pietyB x piety)
	double shrineBonus = 0.05, templeBonus = 0.10, prophetBonus = 0.15;
	double fearLove = -0.3, fearBonus = 0.1;              // love < fearLove: +0.1 before Atomic, -0.1 from Atomic
	// Awe
	double aweBase = 100, aweShrine = 25, aweTemple = 60, aweWonder = 150, aweCap = 1200;
	double incomeBase = 0.6, incomeK = 0.9;               // income = incomeBase + incomeK x ln(1 + S)
	double sCamp = 0.2, sShrine = 0.5, sTemple = 1.2;     // S weights (x (1 + tier / 2))
	// prayers
	double prayDevotion = 0.15;  int maxPrayers = 6;
	double healPlague = 0.2, peaceExhaustion = 0.4, bloomScorch = 0.3;
	double prayerYears = 15, prayerPenalty = 0.05;
	// prophets and edicts
	double prophetYears = 60, prophetTravel = 10, prophetDevotion = 0.15;
	double edictChange = 20, wonderYears = 30, crusadeAggression = 0.4;
	// new cultures
	double apartAng = 0.57, apartLove = 0.6, apartYears = 120, secedeAng = 0.28;
	double birthEvery = 150;  int maxCultures = 6;
	// the Ark
	double arkYears = 250, arkPopRef = 1.5e6, arkMin = 0.8, arkMax = 1.25;
	// moods (ending thresholds, SPEC-SIM 3.12; v3: belovedLove / fearedLove are thresholds of the verdict,
	// see society::memory)
	double belovedDevotion = 0.30, belovedLove = 0.2, fearedLove = -0.2, forgottenDevotion = 0.25;
	// witness
	double witnessJaded = 2;                              // jaded += 2 x faith x falloff
	double witnessLove = 0.5;                             // love += 0.5 x good x falloff x (1 - |love|)
	int maxWitnessIds = 16;
	// --- choices of the society engineer where SPEC-SIM leaves a rule open (A9 report) ---
	double harmlessFaith = 0.5;                           // good <= 0: unharmed witnesses get this share of faith
	double prayerFaith = 2;                               // an answered prayer: x faith, good = +1
	int brushEventEvery = 2;                              // brush kinds: miracle:witnessed on every 2nd call (4 calls/s -> 2 events/s)
	double prophetJitter = 0.15;                          // prophet term = prophetYears x (1 +- jitter)
	int prophetTries = 3;                                 // a journey tries the least devout settlements in turn (unreachable ones skipped)
	double healHold = 0.05, bloomHold = 0.15;             // prayer causes end below these (hysteresis); rain / peace end when gone
	double traitSpread = 0.35;                            // root culture traits: 0.5 +- spread (triangular)
	double traitDrift = 0.12, schismDrift = 0.2;          // daughter traits: parent + drift x gauss
	double awakenSeen = 0.3;                              // an awakened culture remembers its waking: seen["awaken"]
	double daughterSeen = 0.5;                            // a daughter inherits this share of the parent's seen
	int maxCultureIds = 120;                              // newCulture refuses past this many cultures ever (snapshot owner is int8)
	// --- v3, design review (docs/reviews/claude-design-review.md) ---
	// Long memory (findings 1, 2): each culture remembers the god's grace and wrath (persisted in
	// vars.num "society_grace_<id>" / "society_wrath_<id>"): every witnessed act adds
	// |good| x falloff x seconds (brush) x the witness's share of its culture's people to one of the two;
	// both fade with memoryHalfLife. lore = (grace - wrath) / (grace + wrath + loreFloor) in -1..1;
	// verdict = verdictLove x love + (1 - verdictLove) x lore decides the mood (see society::memory).
	double memoryHalfLife = 1000;                         // years
	double loreFloor = 2.5;                               // small memories give a small lore (a sculptor is not a tyrant)
	double verdictLove = 0.35;                            // the share of today's love in the verdict
	double rememberMin = 0.25;                            // grace + wrath at least this: the god is remembered
	double mixedRatio = 0.35;                             // the smaller hand at least this share of the larger: two-handed
	double silentYears = 600;                             // no deed witnessed for this long: silent
	double answeredShare = 0.6;  int answeredMin = 10;    // beloved by answered prayers (share of every prayer, at least this many)
	// (finding 1) nothing bought late: while a culture builds its Ark, deeds change love but not its memory
	bool arkNoMemory = true;
	// (finding 1, verifier) neglect is remembered too: every year a culture has heard no deed of its god for
	// neglectAfter years adds 1 to its neglect (vars.num "society_neglect_<id>", fading with memoryHalfLife,
	// inherited by daughters); grace counts x presence = 1 / (1 + neglectK x neglect) in the lore (wrath
	// whole), so a god absent for millennia cannot buy a beloved verdict in the few centuries before the
	// Ark (simrun atomic-kind: idle until the Atomic age, then kind, was beloved on 7 of 8 seeds)
	double neglectAfter = 120, neglectK = 0.02;
	// (finding 6) hoarding: while Awe >= hoardShare x aweMax, every devotion drifts down hoardDrift a year
	double hoardShare = 0.95, hoardDrift = 0.01;
	// (v3.1, sim-findings-2) hoarding never drags a town's devotion below hoardFloor (0 = no floor): a young,
	// low-piety people whose god sits at a small cap (150) otherwise never built a shrine, so its cap never
	// grew (passive seed 7: aweMax 150 for 1,600 years, the later-age small miracle 8.7 s against 3-8)
	double hoardFloor = 0;
	// (finding 5) the Awe cap grows with the world: aweMax <= aweCapAge[world age] (and <= aweCap)
	std::array<double, 7> aweCapAge{ 300, 450, 600, 800, 1000, 1200, 1200 };
	// (finding 4) before the Iron age a town is estranged beyond apartAngEarly of its capital (apartAng
	// from Iron) and secedes after apartYearsEarly years (apartYears from Iron): a second people in Bronze
	double apartYearsEarly = 60, apartAngEarly = 0.40;
	// (findings 3, 9) an answered prayer: progress-years for the culture (priest-scholars); an answered
	// rain or bloom prayer blesses the town (blessed >= prayerBlessed years: growth and K, Economy)
	double prayerScience = 1.0, prayerBlessed = 20;
	// --- v3.1, design review findings 11 and 13 ---
	// (11) the god's recent favour, for the neighbours to envy or blame: every witnessed deed of the god adds
	// the same impulse as the memory (grace or wrath, x the witness's share of its culture) to vars.num
	// "society_fgrace_<id>" / "society_fwrath_<id>", which fade with favourHalfLife (also while the Ark is
	// built: envy is not memory). Diplomacy reads them as rates (x ln 2 / favourHalfLife: impulse a year).
	double favourHalfLife = 30;
	// (13) fewer, bigger prayers late: a town begins a rain prayer for a famine only after this many famine
	// years in a row, by its culture's age (droughts start one at once in every age)
	std::array<int, 7> rainFamineAge{ 1, 1, 1, 1, 1, 1, 1 };
};

// [diplomacy] Starting values from SPEC-SIM 3.8 (relations half).
struct DiplomacyTune {
	int contactHops = 10;                                 // territories within ~10 hops, or a trade route
	double relax = 0.03, relTarget = 0.1, tradeK = 0.04, borderK = 0.02;
	double aggrK = 0.03, aggrBase = 0.9, loveK = 0.03, noise = 0.03;
	double warV = -0.5, warStrength = 0.6;
	std::array<double, 7> tech{ 1, 1.5, 2.2, 3, 5, 8, 10 };   // by age
	double exhaustYear = 0.015, exhaustBattle = 0.08, exhaustTown = 0.15, exhaustDecay = 0.03;
	double armyEvery = 8, armyMaxCost = 60, armyFrac = 0.06;
	double battleLo = 0.7, battleHi = 1.3, defenceK = 0.05;
	double razeBelow = 100, defenderLoss = 0.15;
	double warMaxYears = 60, lostShare = 0.4, peaceV = -0.1, truceYears = 40;
	int searchesPerYear = 4;
	double tradeEvery = 5;  int roadCap = 3;
	double nukeP = 0.015;  int nukeStrikes = 2;
	double nukeDamage = 0.7, nukeRad = 0.8;  int nukeHops = 2;
	double nukeDust = 0.10, nukeTruce = 100;
	// --- choices of the diplomacy engineer where SPEC-SIM leaves a rule open (A10 report) ---
	// border = sat(meeting edges at gap <= borderGap / (2 x max(borderMinPerim, smaller perimeter))):
	// the share of the smaller culture's outer edge that faces the other within borderGap hops
	int borderGap = 3;  int borderMinPerim = 6;
	double tradeContact = 0.05;                           // trade above this counts as a linking route (contact)
	double tradeDecay = 0.08;                             // trade *= (1 - decay) per year (one caravan / 5 yr holds ~0.22)
	double tradeStep = 0.15;                              // a caravan: trade += step x (1 - trade)
	double tradeArriveV = 0.02;                           // a caravan: v += this
	int tradeHops = 30;                                   // partners within this many hops (angle)
	double tradeMaxCost = 90, tradeNavalMaxCost = 160;    // trader route budget (land-coastal / open sea)
	double traderSize = 4;                                // merchants (not taken from the town's people)
	double tradeRetry = 2;                                // no partner / no route: try again after this many years
	double routeYears = 40;                               // cached routes are recomputed after this many years
	std::array<double, 3> roadP{ 0.5, 0.2, 0.08 };        // per caravan, a land cell's road rises with this chance by level
	double plagueHop = 0.5;                               // P(infect target) = plagueHop x origin plague
	double peaceEdictV = 0.02;                            // peace edict: relations +0.02 / yr
	double armyMinSize = 5;  int armyAttempts = 2;        // per side per year: at most this many route searches
	// alliances (emergent): friends of a victim sour on the aggressor; a common enemy draws cultures together
	double allyV = 0.5, allyOutrage = 0.15, commonEnemyV = 0.02;
	double nukeWitnessAng = 0.3;  int nukeFireHops = 1;
	// --- v3.1, design review findings 11 and 13 (SPEC-SIM 3.8 "(v3.1, design review 11/13)") ---
	// frontier friction by the older side's age (borderK x borderAge[age])
	std::array<double, 7> borderAge{ 1, 1, 1, 1, 1, 1, 1 };
	// the god in the relations: favour rates come from SocietyTune::favourHalfLife (impulse a year)
	double envyK = 0;                // v -= envyK x sat(|favour a - favour b| / favourRef): a god's favourite is envied, its scourged blame the spared
	double favourRef = 0.01;         // favour (grace - wrath, impulse a year) that counts in full
	double sharedWrathK = 0;         // v += sharedWrathK x sat(min(wrath a, wrath b) / favourRef): struck together, they stand together
	double plagueK = 0;              // v -= plagueK x max(sick share a, sick share b) x sat(trade + border): the fever came from over there
	double plagueSick = 0.2;         // a town counts as sick above this plague
	double zealK = 0;                // a people with a living prophet: v -= zealK x sat((devotion - theirs) / zealRef) toward the less devout
	double zealRef = 0.3;
	double faithK = 0;               // two devout peoples (both devotion >= faithDev), one with a prophet: v += faithK
	double faithDev = 0.4;
	// alliances: a pair in contact, at peace, with v >= allyForm becomes allied (vars.num diplomacy_ally<lo>_<hi> = the
	// year; alliance event); it ends below allyBreak, at war, or when one dies. allyMax: at most this many allies each
	double allyForm = 2, allyBreak = 0.2;  int allyMax = 2;
	bool allyCall = false;           // when a culture is attacked its allies who can reach the aggressor join the war
	bool allyDeter = false;          // an aggressor must match warStrength x (the defender + its allies who could join)
	// the Omen of Peace: a peace it made is sworn: for omenYears the pair rests at omenTarget instead of relTarget
	// (vars.num diplomacy_sworn<lo>_<hi> = until); it may also calm a neighbour this tense (v <= omenTension) before a war
	double omenTarget = 0.3, omenYears = 80, omenTension = -0.3, omenCalmTruce = 40;
	bool omenSworn = false, omenCalm = false;
	// after the world's first nuclear exchange the bomb is feared: nukeP x nukeRepeatMul
	double nukeRepeatMul = 1;
	// old enemies tire of each other: the truce after the n-th war of a pair lasts x (1 + feudK x min(feudMax, n - 1))
	// (vars.num diplomacy_feud<lo>_<hi> counts the pair's wars)
	double feudK = 0, feudMax = 4;
	// a weary people starts no war: the aggressor's exhaustion must be at most warWeary (1 = any), and its
	// remembered war years (vars.num diplomacy_warred<id>: +1 a year at war, half-life warredHalfLife) at most
	// warredMax (0 = no limit): a people that fought most of the last century does not start another war
	double warWeary = 1, warredHalfLife = 80, warredMax = 0;
	// a war in which neither side has sent an army for warStall years ends in a draw (0 = never): no 60-year
	// wars of nothing between peoples whose armies cannot reach each other
	double warStall = 0;
	// the Omen calming a neighbour (no war yet): the people witness it with this good (an Omen ending a war: 0.8)
	double omenCalmGood = 0.3;
	// blame and thanks: a war is blamed on the god when the aggressor's recent wrath, or its envy of the defender's
	// favour, is at least blameMin; its victor thanks the god when its grace rate is at least thankMin (devotion
	// + thankDevotion in its towns; the loser's love - blameLove)
	double blameMin = 0.004, thankMin = 0.004, thankDevotion = 0, blameLove = 0;
	// --- chronicle-diplomacy (sim-findings-2 "open for the lead": the Omen of Peace was weak) ---
	// omenWide: an Omen that ends a people's wars also calms every other neighbour of that people on the brink
	// (society::tense), not only the warring pairs (with no war it already calms them all)
	bool omenWide = false;
	// omenNoEnvy: the Omen leaves no envy: its witnesses add nothing to the god's fading favour (society_fgrace:
	// a peace seen by both armies made every third people envy them), and a pair under a sworn peace does not
	// envy each other's favour while the oath lasts
	bool omenNoEnvy = false;
	// omenAge >= 0: the Omen of Peace wakes in this age instead of its power table's (Iron); -1 = the table
	int omenAge = -1;
};

namespace society {

// [society] newCulture() options (JS { parent = -1, cell, reason })
struct NewCultureOpts {
	int parent = -1;
	int cell = -1;
	BirthReason reason = BirthReason::Awaken;
};
// [society] witness() options. C++ detail: the per-settlement good values of the power table
// ("-0.8 damaged, -0.2 others") are expressed with `harmed` / `goodHarmed`.
struct WitnessOpts {
	std::string kind;                    // power id, or "nuke"
	double faith = 0;                    // devotion gained at the centre, 0..0.3 (brush: already x elapsed seconds)
	double good = 0;                     // -1..1, for witnesses not in `harmed`
	std::vector<int> harmed;             // settlement ids the act hurt (they use goodHarmed)
	double goodHarmed = 0;
	bool natural = false;                // (v3) a natural event: no deed of the god (no memory, not 'heard')
	bool favour = true;                  // (chronicle-diplomacy) false: the deed adds nothing to the fading favour
	                                     // sums (society_fgrace / fwrath: envy, blame, thanks); the Omen of Peace
	                                     // under DiplomacyTune::omenNoEnvy
};
// [society] regard() result
struct Regard {
	double devotion = 0, love = 0;
	Mood mood = Mood::Forgotten;         // Beloved | Feared | Forgotten | Parting (from memory(): the verdict)
};
// [society] (v3, design review) memory() result: what a culture (or everyone) remembers of its god.
struct Memory {
	double devotion = 0, love = 0;       // as regard(): population-weighted means today
	double grace = 0, wrath = 0;         // remembered deeds (fading with SocietyTune::memoryHalfLife)
	double neglect = 0;                  // remembered years of an absent god (fading like grace and wrath)
	double presence = 1;                 // 1 / (1 + neglectK x neglect): the share of grace the lore holds
	double lore = 0;                     // (grace x presence - wrath) / (grace x presence + wrath + loreFloor), -1..1
	double verdict = 0;                  // verdictLove x love + (1 - verdictLove) x lore
	double silent = 0;                   // years since a deed of the god was last witnessed
	int prayers = 0, answered = 0;       // prayers begun / answered in the culture's history (daughters inherit)
	bool remembered = false;             // grace + wrath >= rememberMin
	bool mixed = false;                  // both hands remembered (the smaller >= mixedRatio x the larger)
	Mood mood = Mood::Forgotten;
};

// ---------------------------------------------------------------- [society] belief (Society.cpp)

// [society] Society.newCulture: names from Names (daughter language when parent >= 0), traits rolled
// (daughters drift from the parent), colour by free slot; emits culture:born. Pointer valid until
// the next culture is created: keep ->id. nullptr only if it cannot create one.
Culture* newCulture(World& w, const NewCultureOpts& opts = {});
// [society] Society.yearly: housekeeping (pop / towns, dead cultures: culture:died once, makePeace
// with the other side as victor, units killed 'disbanded', prophet cleared; jaded decay, love
// drift), belief drift, Awe (aweMax, income), prayers, prophets and edicts, schisms (new cultures),
// the Ark; then society::diplomacy(w); then chronicle::updateEpithets(w).
void yearly(World& w);
// [society] Society.witness: settlements within ang of the cell saw an act of the god. For each,
// with smooth falloff 1 -> 0 across ang: gain = faith x falloff x (1 - jaded) x (1 - devotion);
// devotion += gain; jaded = min(1, jaded + 2 x faith x falloff); love += 0.5 x good x falloff x
// (1 - |love|); culture.seen[kind] += faith x falloff. An act with good <= 0 gives half faith to
// witnesses it did not harm. A praying settlement whose prayer matches kind gets double faith and
// good = +1, its prayer is cleared and prayer:answered is emitted. god.mercy += max(0, good) x
// faith; god.wrath += max(0, -good) x faith. Emits miracle:witnessed with up to 16 ids that gained
// anything (brush powers: at most 2 per real second; Powers paces its calls). Returns how many
// settlements gained anything. A9 detail: for brush kinds the love term is also x elapsed seconds
// (faith / def.faith, at most 1), so love per second of brushing follows the formula; brush kinds
// emit miracle:witnessed on every 2nd call (Powers calls 4 x / s -> 2 events / s).
int witness(World& w, int cell, double ang, const WitnessOpts& opts);
// [society] Society.regard: population-weighted means over the culture's live settlements
// (-1 = everyone); mood by the ending thresholds. Read-only.
Regard regard(const World& w, int cultureId = -1);
// [society] Society.raiseProphet: the culture of settlement sid gets a named prophet for ~60 years
// living there (prophet:risen). nullptr if invalid or the culture already has one. The pointer
// points into the culture: keep the culture id.
Prophet* raiseProphet(World& w, int sid);
// [society] Society.setEdict: needs a living prophet; one per culture; may be changed once every
// 20 years; lasts until the prophet dies. Emits edict. Edicts are free.
bool setEdict(World& w, int cultureId, Edict edict);
// [society] Society.aweIncome: Awe per year = 0.6 + 0.9 x ln(1 + S). Read-only. 0 while no
// settlement lives (nobody believes).
double aweIncome(const World& w);
// [society] (v3, design review findings 1, 2) the culture's memory of the god and the mood it
// leaves (-1 = everyone: population-weighted over living cultures; the latest deed of any). Read-only.
// Mood, in this order:
//   silent >= silentYears              -> Parting when remembered, else Forgotten (an absent god)
//   mixed (both hands remembered)      -> Parting ("the Two-Handed")
//   verdict <= fearedLove (-0.2)       -> Feared
//   verdict >= belovedLove (0.2) and devotion >= belovedDevotion (0.30), or answered >= answeredShare
//   of at least answeredMin prayers with verdict > 0                  -> Beloved
//   devotion < forgottenDevotion (0.25) -> Forgotten
//   otherwise                           -> Parting
Memory memory(const World& w, int cultureId = -1);
// [society] (v3, design review finding 6) raise a Wonder in settlement sid now (b.wonder, wonder:built
// with its name, settlement:building). False if it is dead or already has one. Used by the build edict
// and by Consecrate on a capital.
bool raiseWonder(World& w, int sid);
// [society] notes for the readers of edicts (A9 owner): Society.yearly implements `build` (the
// Wonder 30 years after edictSince, once per edict) and prophet travel; the other edict effects are
// read by their owners from culture.edict: `peace` (no new wars, relations +0.02/yr) and `crusade`
// (aggression +0.4 toward the least devout neighbour) by Diplomacy, `expand` (settler cooldown 12) by
// Economy. Prophet travel: a Prophet unit (to = the target settlement); Diplomacy's onArrive must set
// prophet.settlement = unit.to (and devotion +0.15 there); a prophet unit that vanished without
// moving him means he died on the road (prophet:died, edict cleared).

// ---------------------------------------------------------------- [diplomacy] relations (Diplomacy.cpp)

// [diplomacy] Society.rel: the relation between cultures a != b, created on demand in
// w.vars.rel[state::relKey(a, b)]. nullptr when a == b or either id is invalid. Sim-only (UI uses peek).
Relation* rel(World& w, int a, int b);
// [diplomacy] Society.peek: the relation or nullptr; never creates. Read-only (for UI and render).
const Relation* peek(const World& w, int a, int b);
// [diplomacy] Society.atWar. Read-only.
bool atWar(const World& w, int a, int b);
// [diplomacy] Society.declareWar: a = aggressor; war = true, since = year, aggressor; stats.wars++;
// emits war:declared.
void declareWar(World& w, int a, int b);
// [diplomacy] Society.makePeace: works at any time; v = -0.1, truce = year + truceYears, war ends
// (war:ended { a, b, victor }, -1 = draw).
void makePeace(World& w, int a, int b, double truceYears = 40, int victor = -1);
// (v3.1, design review 11) makePeace also: a victor whose god favoured it lately (grace rate >= thankMin)
// thanks the god (war:ended.thanked; devotion + thankDevotion in its towns, the loser's love - blameLove),
// and the wars allies joined for a or b against the other end with it. declareWar: allies a war breaks
// up; war:declared.blamed when the god moved the aggressor (blameMin); with allyCall b's allies who can
// reach a join (war:declared.ally = b).
// [diplomacy] (v3.1) the Omen of Peace swears the pair to peace (omenSworn): for omenYears their relations
// rest at omenTarget. False when it is off or invalid.
bool swearPeace(World& w, int a, int b);
// [diplomacy] (v3.1) the cultures in contact with cultureId, at peace with it, whose relations are at most
// omenTension (empty unless omenCalm): the neighbours an Omen of Peace may calm before a war. Read-only.
std::vector<int> tense(const World& w, int cultureId);
// [diplomacy] (chronicle-diplomacy) the year the Omen's sworn peace between a and b ends, or -1 when they are
// not sworn (none, ended, or bad ids). Read-only (the chronicle names the peoples an Omen calmed).
double swornUntil(const World& w, int a, int b);
// [diplomacy] Society.onArrive: a trader, army or prophet reached its destination (see the arrival
// contract in Units.h): battles (battle event, capture / raze, losses), trade (trade, relations,
// road[] along the land part of the path, plague hop via civ::infect, trade:route once per pair),
// prophet visits (devotion +0.15, prophet.settlement moves).
void onArrive(World& w, int unitId);
// [diplomacy] Society.diplomacy: yearly: contact, relations, wars (declare, armies every 8 years via
// Units::spawn, exhaustion, peace), trade (traders every ~5 years), nuclear risk; at most 4
// army / trader path searches per year.
void diplomacy(World& w);

} // namespace society
} // namespace dm
