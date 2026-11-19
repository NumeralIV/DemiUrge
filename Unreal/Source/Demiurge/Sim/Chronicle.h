// DEMIURGE — Sim/Chronicle.h : the history as prose, epithets, time-lapse snapshots, the tale
// (SPEC-SIM 3.11, DM.Chronicle).
// Owner: chronicle engineer (A13, key "chronicle"). Files: Chronicle.h, Chronicle.cpp,
// SimTests/test_chronicle.cpp, SimTests/stubs/Chronicle_stub.cpp. Header written by foundation-api;
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
