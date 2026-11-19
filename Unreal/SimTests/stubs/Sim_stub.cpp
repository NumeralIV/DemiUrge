// DEMIURGE — SimTests/stubs/Sim_stub.cpp : neutral stand-in for Sim/Sim.cpp (see StubHooks.h).
// Owner: sim engineer (keep it matching Sim.h). Written by foundation-api.
// Neutral: create returns a bare state::create world (no terrain, never nullptr); use / rederive /
// tick / advance / refreshStats do nothing (time does not pass); setPhase refuses (false; w.phase is
// never written); checkEnding nullopt; score 0; genesis progress reports the phase only, next / skip
// false; fingerprint 0; firstNonFinite ""; isYearlyTick false; runTicks runs 0 ticks.
// mood (a pure threshold function) and sample (plain reads) answer honestly.
#include "StubHooks.h"

DM_STUB_MODULE(sim);

namespace dm::sim {

std::unique_ptr<World> create(const WorldOpts& opts) {
	DM_STUB(sim_create, opts);
	state::CreateOpts co;
	co.seed = opts.seed;
	co.name = opts.name.empty() ? std::string("Unnamed") : opts.name;
	co.L = opts.L;
	std::unique_ptr<World> w = state::create(co);
	w->settings = opts.settings;
	return w;
}
void use(World& w) {
	DM_STUB(sim_use, w);
}
void rederive(World& w) {
	DM_STUB(sim_rederive, w);
}
void tick(World& w) {
	DM_STUB(sim_tick, w);
}
void advance(World& w, int ticks) {
	DM_STUB(sim_advance, w, ticks);
}
bool setPhase(World& w, Phase phase) {
	DM_STUB(sim_setPhase, w, phase);
	return false;
}
void refreshStats(World& w) {
	DM_STUB(sim_refreshStats, w);
}
std::optional<Ending> checkEnding(World& w) {
	DM_STUB(sim_checkEnding, w);
	return std::nullopt;
}
Score score(const World& w) {
	DM_STUB(sim_score, w);
	return Score{};
}
Mood mood(double devotion, double love) {
	DM_STUB(sim_mood, devotion, love);
	if (devotion >= 0.35 && love >= 0.2) return Mood::Beloved;
	if (love <= -0.2) return Mood::Feared;
	if (devotion < 0.2) return Mood::Forgotten;
	return Mood::Parting;
}

namespace genesis {
Progress progress(const World& w) {
	DM_STUB(sim_genesis_progress, w);
	Progress p;
	p.phase = w.phase;
	return p;
}
bool next(World& w) {
	DM_STUB(sim_genesis_next, w);
	return false;
}
bool skip(World& w, const SkipOpts& opts) {
	DM_STUB(sim_genesis_skip, w, opts);
	return false;
}
} // namespace genesis

Sample sample(const World& w) {
	DM_STUB(sim_sample, w);
	Sample s;
	s.year = w.year;
	s.tick = w.tick;
	s.phase = w.phase;
	s.pop = w.stats.pop;
	for (const Settlement& st : w.settlements) if (!st.dead) s.settlements++;
	for (const Culture& cu : w.cultures) if (!cu.dead) s.cultures++;
	s.age = w.age;
	s.seaLevel = w.seaLevel;
	s.co2 = w.co2;
	s.dust = w.dust;
	s.awe = w.god.awe;
	s.aweMax = w.god.aweMax;
	s.units = int(w.units.size());
	s.disasters = int(w.disasters.size());
	s.wars = w.stats.wars;
	s.miracles = w.stats.miracles;
	return s;
}
uint32_t fingerprint(const World& w) {
	DM_STUB(sim_fingerprint, w);
	return 0;
}
std::string firstNonFinite(const World& w) {
	DM_STUB(sim_firstNonFinite, w);
	return std::string();
}
bool isYearlyTick(const World& w) {
	DM_STUB(sim_isYearlyTick, w);
	return false;
}
int64_t runTicks(World& w, int64_t ticks, const RunHooks& hooks, bool stopAtEnding) {
	DM_STUB(sim_runTicks, w, ticks, hooks, stopAtEnding);
	return 0;
}

} // namespace dm::sim
