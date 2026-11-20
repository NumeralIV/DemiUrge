// DEMIURGE — Sim/Chronicle.h : the history as prose, epithets, time-lapse snapshots, the tale
// (SPEC-SIM 3.11, DM.Chronicle).
// Owner: chronicle engineer (A13, key "chronicle"; wave 3 "chronicle-budget"). Files: Chronicle.h, Chronicle.cpp,
// SimTests/test_chronicle*.cpp, SimTests/stubs/Chronicle_stub.cpp. Header written by foundation-api;
// the owner may add, never rename.
//
// Writers (SPEC-SIM 1.1): w.chronicle, w.snapshots, w.nextId.entry, culture epithet / godName
// changes, god.epithet, vars.snapEvery, vars.snapNext.
// Wording draws ONLY from w.rng.fork("chron:" + std::to_string(entryId)) (or other forks): prose
// never disturbs the sim stream, so dmt::hash(w) is unchanged by chronicle activity.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "State.h"

namespace dm {

// Starting values from SPEC-SIM 3.11.
struct ChronicleTune {
	double snapEvery = 25;           // years between snapshots (vars.snapEvery starts here)
	int maxFrames = 200;             // past this, drop every second history frame and double the interval
	double minorGap = 5;             // rate limit: at most one minor entry of a type per culture per this many years
	double silentYears = 300;        // no deed seen by a culture for this long -> 'the Silent One'
	int taleMinParagraphs = 4, taleMaxParagraphs = 8;
	// --- choices of the chronicle engineer where SPEC-SIM leaves a rule open (A13 report) ---
	double brushGap = 12;            // rain / bloom / sculpting seen by a culture: one minor entry per this many years
	double noisyGap = 8;             // fires, natural lightning, storms: at most one entry of a kind in the whole world per this many years
	double fireGap = 20;             // wildfires, lightning and storms near a culture: one entry per this many years
	double buildingGap = 12;         // ordinary buildings of a culture (not its first of a kind): one entry per this many years
	double tierGap = 10;             // villages and towns of a culture growing: one entry per this many years
	double nearAng = 0.30;           // an event further than this from every live settlement is described by region
	double witnessAng = 0.15;        // lightning / fires / waves farther than this from every settlement go unrecorded
	int maxEntries = 2500;           // past this, the oldest minor entry (then notable) is dropped
	int cometEvery = 4;              // genesis: the 1st comet is chronicled, then every 4th
	double memoryHalfLife = 200;     // years: epithets follow what a culture saw recently (seen deltas decay)
	double minDeeds = 0.05;          // remembered faith below this: the culture names its god for its waking
	double mixedShare = 0.3;         // benevolent and wrathful memories both >= this share -> a two-sided epithet
	double switchMargin = 1.25;      // a new dominant deed must outweigh the current one by this factor
	double epithetHold = 40;         // years a culture keeps a name before a new one may replace it (not the waker / silent names)
	double drownWeight = 0.3;        // remembered weight of each settlement of a culture lost to drowning / waves
	double waveWeight = 0.35;        // a tsunami striking near a culture's settlement counts as this many drownings
	// with no distinct memory, how the people feel (society::regard) names the god
	double belovedLove = 0.4, belovedDevotion = 0.45;   // -> 'the Beloved', 'the Kind One', ...
	double dreadLove = -0.35;        // -> 'the Dreaded', 'the Jealous One', ... (also overrides a two-sided epithet)
	// --- integration verifier (2026-11-19): the late game drowned the chronicle in notable entries ---
	double famineGap = 10;           // a famine in a city (notable): at most one entry per culture per this many years
	double bigTierGap = 20;          // a culture's cities / metropolises after its first: one entry per this many years
	double dustFamine = 0.05;        // w.dust at or above this: famines are told as the darkened sun's doing
	double forgetDeeds = 0.5;        // a deed-named god falls back to a plain name only below forgetDeeds x minDeeds
	// --- chronicle voice (docs/content/chronicle-voice.md, 2026-11-19) ---
	double plagueTold = 30;          // a plague's end is told only if its coming was written down within this many years
	double kindShare = 0.8;          // 'Kind' / 'Cruel' epithets: one side of the memory is at least this share of it ...
	double kindSpread = 0.45;        // ... no single deed is this share of that side or more (many kinds of deed) ...
	double kindHold = 0.6;           // ... (a people already using the name keeps it until one deed reaches this share) ...
	double kindLove = 0.2, cruelLove = -0.2;   // ... and regard love is at least kindLove (Kind) / at most cruelLove (Cruel)
	double dustFamineGap = 3;        // famines under a darkened sky: one entry in the whole world per this many years
	double prayerWorldGap = 3;       // prayers begun (minor): at most one entry in the whole world per this many years
	double tradeGap = 20, tradeWorldGap = 5;   // later trade routes (minor): per culture / in the whole world (was minorGap x 2 per culture)
	double answerWorldGap = 4;       // answered prayers (told): at most one in the whole world per this many years (verifier 2026-11-19)
	// --- voice review 2 (2026-11-19) ---
	double answerQuiet = 5;          // years after a town's answered prayer is told in which its new prayers and the god's rain / bloom over it go untold
	double prayerTownGap = 12;       // a town's prayers begun (minor): at most one entry per this many years
	double prayerHeard = 25;         // a people whose prayers were answered (told) within this many years expects the answer: its new prayers are not news (the answer line tells them)
	double brushTownGap = 30;        // rain / bloom / sculpting over the same town (minor): one entry per this many years (the early book was Nara's rain every 13 years)
	double firstCluster = 4;         // a people's "first of its kind" buildings: one notable entry per this many years, the rest are ordinary buildings
	// --- chronicle budget (wave 3, design review finding 8: the late-game firehose) ---
	// Budgets are token buckets in vars.num (saved, deterministic) that refill with the years. Story entries
	// (wars, peace, prophets, edicts, epithets, ages, the god's own great acts, ...) are never dropped: over
	// budget the frequent ones (wars, peace, later ages, destroyed towns, prophets, the god's heal / omen) only
	// lose weight. Routine notables (city famines, growth, first buildings, answered prayers, captures, ...)
	// over budget become minor; repeated minor events of one kind and people are folded into one digest entry
	// per decade instead of being written one by one. See "the budget" in Chronicle.cpp.
	bool budget = true;              // false: the pre-budget chronicle (every gate above, no buckets, no digests)
	double digestYears = 10;         // the budget's decade: one entry per kind and people, one digest per decade
	double storyPerDecade = 0.25;    // (verify: was 0.3) weight-2 wars, peace, later ages, destroyed towns, prophets, edicts, the god's heal /
	                                 // inspire / omen / plague, natural eruptions, a smaller people's new name for the god:
	                                 // refill per decade ...
	double storyBurst = 4;           // ... and the bucket's size (over it they are told as minor entries, never dropped)
	double notablePerDecade = 0.2;   // (verify: was 0.25) routine weight-2 entries: refill per decade ...
	double notableBurst = 3;         // ... and the bucket's size
	double epochalPerDecade = 0.06;  // (verify: was 0.08) weight-3 entries that are not once-a-world moments (a city destroyed, a people
	                                 // born or dead): refill per decade (about 0.7 per real minute) ...
	double epochalBurst = 2;         // ... bucket size; over it they are told as notable
	double minorPerDecade = 2.5;     // the world's own minor entries of the foldable kinds (not the god's small acts): refill per decade ...
	double minorBurst = 6;           // ... and bucket size; over it (or a second of its kind for a people in one decade) the event is folded
	double digestEvery = 30;         // (verify) at most one digest in this many years: folds wait and add up
	double digestStale = 80;         // (verify) folds too small for a clause are let go once this many years old (a digest's span
	                                 // stays under a century)
	int digestMin = 5;               // a digest clause needs folded events of one kind and people x the kind's weight (Chronicle.cpp kClauses) >= this
	int templateCap = 10;            // a foldable minor line used this many times is retired while its family has fresher lines;
	                                 // a family whose every line reached it folds its minor entries into the digest
	bool godDrownsOnly = true;       // (finding 7) a town the sea covers is the god's drowning only near a recent stroke (below);
	                                 // the sea's own rise and the engines' are told as such and never name the god the Drowner
	double strokeYears = 5;          // a settlement the sea covers within this many years of the god's Lower Land, quake or meteor
	double strokeAng = 0.12;         // ... within the stroke's radius plus this many radians is the god's drowning (else the sea's)
	double enginesCo2 = 40;          // w.co2 at or above this while some people burn fuel (factories, plants): the sea rise is the engines'
};

namespace chronicle {

// add() options (JS { type, text, cell = -1, culture = -1, weight = 1 })
struct EntryOpts {
	std::string type;                // 'founded', 'war', ... (Chronicle's vocabulary)
	std::string text;
	int cell = -1;
	int culture = -1;
	int weight = 1;                  // 1 minor, 2 notable, 3 epochal
};
// tale() result
struct Tale {
	std::string title;
	std::vector<std::string> paragraphs;
	NumBag stats;
};

// Chronicle.attach: (re)subscribe this world's bus: unsubscribe the tokens kept from the previous
// attach (if any), subscribe again, so there is exactly one subscription per handler even after
// w.bus.clear() or repeated calls. No boolean guard. Keep the tokens in w.scratch<...>() (note:
// clearScratch() drops them; sim::use calls attach after loading).
void attach(World& w);
// Chronicle.add: push an entry (id = w.nextId.entry++, year = w.year) and emit chronicle:entry.
// Returns a pointer into w.chronicle (valid until the next add) or nullptr if refused.
ChronicleEntry* add(World& w, const EntryOpts& opts);
// Chronicle.yearStr(world, year = world.year) -> 'Year 312'. Read-only.
std::string yearStr(const World& w, std::optional<double> year = std::nullopt);
// Chronicle.epithet: e.g. 'the Rain-Giver', from culture.seen, god.deeds and society::regard.
// Read-only.
std::string epithet(const World& w, int cultureId);
// Chronicle.updateEpithets: recompute each living culture's epithet (chronicle it when it changes)
// and god.epithet (the epithet of the most populous living culture). Society::yearly calls it last.
void updateEpithets(World& w);
// Chronicle.snapshot(world, force = false): push a time-lapse frame if due (year >= vars.snapNext,
// every vars.snapEvery = 25 years; past 200 frames drop every second history frame and double the
// interval) or forced (every phase change). Frame: year, heat, seaLevel, pop, biome, owner (culture
// id of the owning settlement, -1), urban (round(urban x 255)). Returns whether one was pushed.
bool snapshot(World& w, bool force = false);
// Chronicle.tale: end-of-game retelling: title, 4-8 paragraphs about this world's actual history,
// stats. Read-only (no draws from w.rng; forks only).
Tale tale(const World& w);

} // namespace chronicle
} // namespace dm
