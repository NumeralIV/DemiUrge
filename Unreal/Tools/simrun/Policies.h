// DEMIURGE — Tools/simrun/Policies.h : scripted players for the headless runner (JS tools/policies.js).
// Owner: sim engineer (A14). Tool code (not Sim/): it may use anything, but it only ever acts on the
// world through powers::can / powers::use and society::setEdict, exactly like a player, and draws
// its own randomness from a private Rng (never w.rng), so runs stay deterministic.
//
// Policies (called before EVERY tick; decisions about once a year, brush strokes over several ticks
// with dt = sim::kRealPerTick, the first call of a press with dt = 0):
//   passive   does nothing (an undisturbed world)
//   kind      answers prayers (rain / heal / peace / bloom), heals plagues, ends wars with the Omen
//             of Peace (sim-findings-2 verifier: one Omen on a war comes first, before the two prayers of the year)
//             (v3.1: and, with Awe to spare, calms a people on the brink of war), keeps a prophet with the leading culture (edict peace while at war, else
//             build; other cultures only with Awe to spare), every year blooms (rains, before
//             Bloom; half a second) over the least loved of the leading culture's 24 largest towns and inspires
//             one with spare Awe, and wakes a new people for free when everyone is gone
//   cruel     a tyrant who stops short of extinction (never acts while fewer than 4 settlements
//             live) and rules by fear: earthquakes on the least frightened of the leading
//             culture's 12 largest towns whenever affordable, lightning while saving up, a volcano
//             or meteor only when the Awe would otherwise be wasted at its cap
//   chaotic   about every other year a random unlocked power on a random target (settlement
//             targets on settlements, brush strokes of random size and length); re-awakens
//   doom      no restraint: lets the people pass the 1,000 a silence needs while hoarding Awe,
//             then drowns their towns with Lower Land (lowest first) and strikes up to 3 of the
//             largest a year with every great wrath it can pay for
// The design review's new players (docs/reviews/claude-design-review.md, ported from its driver):
//   curious      small blessings until year 900 (answers 2 prayers, heals a plague, a dab of rain or
//                bloom on a random town about every other year, now and then an Inspire), then a burst
//                of wrath until 1100 (saves for the greatest wrath its Awe can hold, lightning while
//                waiting), then neglect to the end
//   blessonly    the same small blessings until year 1000, then nothing
//   prayers      answers every prayer it can (up to 3 a year) and nothing else; re-awakens
//   repent       the wrath burst until year 800, then answers prayers and blesses
//   sculptor     about every 15 years holds Raise or Lower Land for 1 s on a random town; re-awakens
//   late-kind    nothing until an Ark is being built, then the kind player
//   late-cruel   nothing until an Ark is being built, then the cruel player
//   inspire-spam Inspire on the leading culture's capital whenever affordable; re-awakens
//   hoarder      answers prayers (like `prayers`) and never buys anything durable
//   builder      answers prayers and consecrates (shrine -> temple, capital -> Wonder) whenever it can
// The verifier's (wave3-gameplay-verify) late players: the review's late-kind / late-cruel, but waking
// with the Atomic age (about 500-700 years, 4-6 real minutes, before the launch) instead of the Ark:
//   atomic-kind  nothing until the world reaches the Atomic age, then the kind player
//   atomic-cruel nothing until the world reaches the Atomic age, then the cruel player
// The findings-11/13 verifier's (sim-findings-2) adversarial players, which push the new diplomacy rules:
//   peacemaker   only the Omen of Peace, on every people at war or on the brink whenever it can pay (up
//                to 3 a year): can a player hold the world in permanent peace, and what does it buy?
//   favourite    blesses one people only (the largest when it starts; a new one when it dies): its
//                prayers, plagues, a bloom on its least loved town and an Inspire with Awe to spare.
//                The envy rule's war spiral against a favourite
//   scourge      strikes one small people only (the smallest of at least 2, never the last): lightning,
//                a quake when affordable. The scourged blame the spared: extinction cascades, spirals
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include "Core/Rng.h"
#include "State.h"

namespace dm::simrun {

enum class PolicyKind : uint8_t { Passive, Kind, Cruel, Chaotic, Doom, Curious, BlessOnly, Prayers, Repent, Sculptor, LateKind, LateCruel,
	InspireSpam, Hoarder, Builder, AtomicKind, AtomicCruel, Peacemaker, Favourite, Scourge, Count };
const char* toStr(PolicyKind k);
bool policyFromStr(std::string_view s, PolicyKind& out);

struct PolicyStats {
	int clicks = 0;                       // successful click powers
	int strokes = 0;                      // brush strokes started
	int refused = 0;                      // powers::use returned false after can() said ok
	std::map<std::string, int> byPower;   // successful uses (clicks) / strokes per power id
	int edicts = 0;
};

class Policy {
public:
	Policy(PolicyKind kind, uint32_t seed);
	// Call before every sim::advance(w, 1).
	void before(World& w);
	PolicyKind kind() const { return kind_; }
	const PolicyStats& stats() const { return stats_; }

private:
	bool click(World& w, const char* id, int cell);
	bool startStroke(World& w, const char* id, int cell, int ticks, double ang = -1);
	void continueStroke(World& w);
	bool reawaken(World& w);
	void decideKind(World& w);
	void decideCruel(World& w, bool restrained);
	void decideChaotic(World& w);
	// the design review's players
	void answerPrayers(World& w, int maxN);
	void smallBlessings(World& w);
	void wrathBurst(World& w);
	bool consecrate(World& w);
	// the verifier's adversarial players (sim-findings-2)
	void decidePeacemaker(World& w);
	void decideFavourite(World& w);
	void decideScourge(World& w);
	int chosen_ = -1;                     // favourite / scourge: the people chosen (-1 none yet)

	double lateFrom_ = -1;                // late-kind / late-cruel: year the first Ark began; atomic-*: the Atomic age (-1: not yet)

	PolicyKind kind_;
	Rng rng_;
	PolicyStats stats_;
	double nextYear_ = 0;                 // next decision year (history)
	// the brush stroke in progress
	std::string strokeId_;
	int strokeCell_ = -1;
	int strokeLeft_ = 0;
	bool strokeFirst_ = true;
	double strokeAng_ = -1;
};

} // namespace dm::simrun
