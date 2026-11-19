// DEMIURGE — Tools/simrun/Policies.h : scripted players for the headless runner (JS tools/policies.js).
// Owner: sim engineer (A14). Tool code (not Sim/): it may use anything, but it only ever acts on the
// world through powers::can / powers::use and society::setEdict, exactly like a player, and draws
// its own randomness from a private Rng (never w.rng), so runs stay deterministic.
//
// Policies (called before EVERY tick; decisions about once a year, brush strokes over several ticks
// with dt = sim::kRealPerTick, the first call of a press with dt = 0):
//   passive   does nothing (an undisturbed world)
//   kind      answers prayers (rain / heal / peace / bloom), heals plagues, ends wars with the Omen
//             of Peace, keeps a prophet with the leading culture (edict peace while at war, else
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
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include "Core/Rng.h"
#include "State.h"

namespace dm::simrun {

enum class PolicyKind : uint8_t { Passive, Kind, Cruel, Chaotic, Doom };
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
