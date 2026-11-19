// DEMIURGE — Sim/Names.h : procedural languages and names (SPEC-SIM 3.1, DM.Names).
// Owner: names engineer (A1, key "names"). Files: Names.h, Names.cpp, SimTests/test_names*.cpp,
// SimTests/stubs/Names_stub.cpp. Header written by foundation-api; the owner may add, never rename.
// Design: docs/content/cultures.md (eight culture families, their sounds, naming patterns and drift).
//
// No runtime AI: pure combinatorics on an rng. A language is an OPAQUE std::string (stored as
// Culture::lang and saved as is): Names alone defines its encoding and parses it back. Names keeps
// no state between calls (no globals, no caches outside the arguments), so parsing must be cheap.
//
// Every function takes the rng explicitly. Sim code passes w.rng or a fork of it (SPEC-SIM 0.9).
// Every returned name: 3..14 characters, never empty, pronounceable, never an obvious English
// profanity (re-roll on hit). Different languages look and sound clearly different (inventory size,
// vowel harmony or not, clusters or not, apostrophes / hyphens, typical length).
//
// Every language belongs to one of eight culture families (docs/content/cultures.md); a daughter
// language stays in its mother's family and drifts by regular sound laws. Places, people and peoples
// are also spelled so that no two of them, in any language, differ by a single letter (an invisible
// check sum over the letters), so one world never holds near-twins like "Girtdimton" / "Girtdemton".
#pragma once

#include <string>

#include "State.h"

namespace dm {

// Names is not tuned through w.tune<> (its functions take no world): these are constants.
struct NamesTune {
	int minLen = 3, maxLen = 14;          // every returned name (also the whole plural "the X")
	int tries = 16;                       // re-rolls before a plain fallback name
	int newShifts = 4;                    // regular sound shifts a daughter language adds (at most)
	int maxRoots = 56;                    // recurring stems a language keeps (the oldest go first)
	int sample = 600;                     // names drawn to measure how varied a language's names are (probe only)
	double distinct = 0.94;               // share that must be different (places, people, peoples): a probe margin
	                                      // over the spec's 90 % for a fresh sample of 300 names
	int rounds = 3;                       // how many times a language is drawn again to get there (the most varied is kept)
};
inline constexpr NamesTune kNamesTune{};

namespace names {

struct CultureNames {
	std::string name;                     // 'Umar'
	std::string adj;                      // 'Umaran'
	std::string plural;                   // 'the Umari'
};

// Names.language(rng, parent = null): a new language: consonant / vowel inventories, syllable
// shapes, stress, favourite endings for places and people; with a parent (non-empty), the parent's
// roots after a few regular sound shifts so daughter languages look related. Never empty.
std::string language(Rng& rng, const std::string& parent = std::string());
// Names.word(lang, rng, minSyl = 1, maxSyl = 3): a lowercase word. An empty / unparsable lang
// still yields a word (fall back to a default language), never throws, never empty.
std::string word(const std::string& lang, Rng& rng, int minSyl = 1, int maxSyl = 3);
// Names.place: 'Tel Asha', 'Vornhal', 'Qe-Luun'.
std::string place(const std::string& lang, Rng& rng);
// Names.person: 'Umari', 'Dovak'.
std::string person(const std::string& lang, Rng& rng);
// Names.culture: { name, adj, plural }.
CultureNames culture(const std::string& lang, Rng& rng);
// Names.god: the culture's proper name for the player-god.
std::string god(const std::string& lang, Rng& rng);
// Names.planet: a default world name (language-agnostic, evocative).
std::string planet(Rng& rng);
// Names.cap: first letter upper case (ASCII), the rest unchanged.
std::string cap(const std::string& s);

// Added by the names-cultures track (2026-11-19). Titled phrases rather than bare names, so they may be
// longer than 14 characters (at most 24); otherwise the same rules (pronounceable, blocklisted, never empty).
// Names.ruler: a ruler's style in the culture's own tongue: title and name, 'Ossaro Teodo', 'Qadun Zahirun'.
std::string ruler(const std::string& lang, Rng& rng);
// Names.epithet: a ruler's byname as the chronicle translates it, flavoured by the culture's family:
// 'the Tide-Born', 'the Star-Reader'.
std::string epithet(const std::string& lang, Rng& rng);
// Names.ship: a ship or caravan name in the culture's tongue, 'Ondessa Tavarra', 'Brimra Hald'.
std::string ship(const std::string& lang, Rng& rng);

} // namespace names
} // namespace dm
