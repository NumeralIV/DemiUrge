// DEMIURGE — SimTests/test_integration_adv.cpp : adversarial verification of the whole simulation
// (key "integration", verifier). Every test needs the REAL modules (run.ps1 all, or
// Tools/simrun/simrun.ps1 --tests integration); with stubs each returns at once.
//
//   - save / load at awkward moments of a living L6 world played by an aggressive god: a meteor in
//     flight, a volcano erupting, a tsunami front, a storm, a drought, armies marching in a war,
//     settlers / traders / prophets on the road, the tick of an age change, a brush stroke half done,
//     a plague, a fire, a pending prayer. For each: the game's Save container and the raw state
//     re-save to the same bytes, two loads continue identically (fingerprints), nothing one-shot
//     happens twice after the load (ages, arks, deaths, foundings, chronicle entries), in-flight
//     things still land, no NaN, every event valid.
//   - genesis: a comet in flight saved and loaded still raises the sea.
//   - god powers used every tick with unlimited Awe ("god mode") for centuries: caps hold (160
//     settlements, 300 units, lava fields <= 400 cells), elevation stays in range, no NaN, ticks
//     stay inside the budget, the run is deterministic.
// DM_LONG=1 makes the runs longer (more years, more seeds).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Chronicle.h"
#include "Civ.h"
#include "Climate.h"
#include "Life.h"
#include "Powers.h"
#include "Save.h"
#include "Sim.h"
#include "Society.h"
#include "State.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

void say(const std::string& s) { std::printf("  %s\n", s.c_str()); std::fflush(stdout); }
std::string f2(double x) { char b[64]; std::snprintf(b, sizeof b, "%.2f", x); return b; }
bool longRuns() { const char* e = std::getenv("DM_LONG"); return e && *e && *e != '0'; }

bool allReal() {
	for (const char* m : { "terrain", "climate", "hydro", "life", "path", "units", "civ", "economy", "society",
	                       "diplomacy", "powers", "disasters", "chronicle", "sim", "balance", "names", "save" })
		if (stub::stubbed(m)) return false;
	return true;
}

int liveCount(const World& w) {
	int k = 0;
	for (const Settlement& s : w.settlements) if (!s.dead) k++;
	return k;
}

// ------------------------------------------------------------------ an aggressive scripted god
// Acts before every tick. With god = true the Awe is topped up to 1,200 first (every power
// unlocked and affordable: a stress test, not a balance test). Keeps at most one brush stroke going
// (dt = kRealPerTick per tick); clicks several powers a tick on the largest / lowest towns, random
// land and random sea. Its randomness is its own (copyable, so a loaded world can get an identical
// player).
struct Abuser {
	Rng r;
	bool god = true;
	double perTick = 0.6;         // expected click attempts per tick
	std::string stroke;
	int strokeCell = -1, strokeLeft = 0;
	double strokeAng = 0.06;
	bool strokeFirst = true;
	int uses = 0, refused = 0, strokes = 0;
	std::map<std::string, int> byId;
	Abuser(uint32_t seed, bool godMode) : r(Rng(seed).fork("integration-adv:abuser")), god(godMode) {}

	bool spare = false;           // wrath and Lower Land only on random cells (the people survive to later ages)

	int pickTarget(const World& w, const powers::PowerDef& d) {
		const std::vector<int> ids = civ::live(w);
		const bool harmful = d.group == powers::Group::Wrath || std::string(d.id) == "lower";
		if (spare && harmful && d.target != powers::Target::Settlement) return r.integer(0, w.n - 1);
		if (spare && harmful && r.chance(0.8)) return -1;     // a plague now and then, not every year
		if ((d.target == powers::Target::Settlement || r.chance(0.7)) && !ids.empty()) {
			// the largest town half the time, else any town
			if (r.chance(0.5)) {
				int best = ids[0];
				for (int sid : ids) if (w.settlements[size_t(sid)].pop > w.settlements[size_t(best)].pop) best = sid;
				return w.settlements[size_t(best)].cell;
			}
			return w.settlements[size_t(ids[size_t(r.pickIndex(int(ids.size())))])].cell;
		}
		return r.integer(0, w.n - 1);
	}

	void act(World& w) {
		if (w.phase != Phase::History && w.phase != Phase::Ended) { stroke.clear(); return; }
		if (god) { w.god.aweMax = 1200; w.god.awe = 1200; }
		// the brush stroke in progress
		if (!stroke.empty()) {
			powers::UseOpts o;
			o.ang = strokeAng;
			o.dt = strokeFirst ? 0.0 : sim::kRealPerTick;
			strokeFirst = false;
			const bool okUse = powers::use(w, stroke, strokeCell, o);
			if (!okUse || --strokeLeft <= 0) stroke.clear();
		}
		// clicks (and maybe a new stroke)
		int attempts = 0;
		while (r.f() < perTick && attempts < 4) {
			attempts++;
			// (v4 mythos, M0) only the pre-mythos rows: the Word rows are appended at the end and locked at phase 0,
			// so the abusive god keeps its v3 random stream and its number of acts
			int preMythos = 0;
			for (const powers::PowerDef& pd : powers::kPowers) if (pd.group != powers::Group::Word) preMythos++;
			const powers::PowerDef& d = powers::kPowers[size_t(r.pickIndex(preMythos))];
			if (d.group == powers::Group::Genesis && std::string(d.id) != "awaken") continue;
			if (!powers::unlocked(w, d.id)) continue;
			const int cell = pickTarget(w, d);
			if (!w.valid(cell) || !powers::can(w, d.id, cell).ok) continue;
			if (d.brush) {
				if (!stroke.empty()) continue;
				stroke = d.id;
				strokeCell = cell;
				strokeLeft = r.integer(4, 40);
				strokeAng = r.range(0.03, 0.15);
				strokeFirst = true;
				strokes++;
				byId[d.id]++;
				continue;
			}
			if (powers::use(w, d.id, cell, {})) { uses++; byId[d.id]++; }
			else refused++;
			if (std::string(d.id) == "prophet") {
				const int sid = civ::at(w, cell);
				if (const Settlement* s = state::settlement(w, sid)) society::setEdict(w, s->culture, Edict(1 + r.integer(0, 3)));
			}
		}
	}
	std::string summary() const {
		std::string s = std::to_string(uses) + " clicks, " + std::to_string(strokes) + " strokes, " + std::to_string(refused) + " refused:";
		for (const auto& kv : byId) s += " " + kv.first + " " + std::to_string(kv.second);
		return s;
	}
};

// ------------------------------------------------------------------ one-shot bookkeeping
// What the world has already done once and must never do again after a load.
struct OneShots {
	int worldAge = 0;
	std::vector<int> cultureAge;           // per culture id
	std::vector<char> cultureDead;
	std::vector<char> settlementDead;
	size_t settlements = 0, cultures = 0;
	std::set<int> arkStarted;              // culture ids
	bool arkLaunched = false;
	int64_t nextEntry = 0;
	std::vector<std::string> lastTexts;    // the last 30 chronicle texts
	static OneShots of(const World& w, const std::set<int>& arks) {
		OneShots o;
		o.worldAge = w.age;
		for (const Culture& c : w.cultures) { o.cultureAge.push_back(c.age); o.cultureDead.push_back(c.dead ? 1 : 0); }
		for (const Settlement& s : w.settlements) o.settlementDead.push_back(s.dead ? 1 : 0);
		o.settlements = w.settlements.size();
		o.cultures = w.cultures.size();
		o.arkStarted = arks;
		o.arkLaunched = w.vars.ark.has_value();
		o.nextEntry = w.nextId.entry;
		for (size_t i = w.chronicle.size() > 30 ? w.chronicle.size() - 30 : 0; i < w.chronicle.size(); i++) o.lastTexts.push_back(w.chronicle[i].text);
		return o;
	}
};

// Watches a loaded world for events that repeat something already done before the save.
struct RepeatWatch {
	OneShots before;
	std::vector<std::string> bad;
	std::vector<std::string> repeats;      // chronicle texts of the last 30 entries written again (checked against the never-saved world)
	std::map<std::string, int> counts;
	void attach(World& w) {
		w.bus.onAll([this](const Event& e) {
			counts[e.name()]++;
			if (const EvWorldAge* a = e.as<EvWorldAge>()) {
				if (a->age <= before.worldAge) bad.push_back("world:age " + std::to_string(a->age) + " again");
			} else if (const EvCultureAge* c = e.as<EvCultureAge>()) {
				if (c->id >= 0 && size_t(c->id) < before.cultureAge.size() && c->age <= before.cultureAge[size_t(c->id)])
					bad.push_back("culture:age " + std::to_string(c->id) + " -> " + std::to_string(c->age) + " again");
			} else if (const EvCultureDied* d = e.as<EvCultureDied>()) {
				if (d->id >= 0 && size_t(d->id) < before.cultureDead.size() && before.cultureDead[size_t(d->id)])
					bad.push_back("culture:died " + std::to_string(d->id) + " again");
			} else if (const EvSettlementDestroyed* s = e.as<EvSettlementDestroyed>()) {
				if (s->id >= 0 && size_t(s->id) < before.settlementDead.size() && before.settlementDead[size_t(s->id)])
					bad.push_back("settlement:destroyed " + std::to_string(s->id) + " again");
			} else if (const EvSettlementFounded* f = e.as<EvSettlementFounded>()) {
				if (f->id >= 0 && size_t(f->id) < before.settlements) bad.push_back("settlement:founded reused id " + std::to_string(f->id));
			} else if (const EvArkStarted* a2 = e.as<EvArkStarted>()) {
				if (before.arkStarted.count(a2->culture)) bad.push_back("ark:started again for culture " + std::to_string(a2->culture));
			} else if (e.type == Ev::ArkLaunched) {
				if (before.arkLaunched) bad.push_back("ark:launched again");
			} else if (const EvChronicleEntry* ce = e.as<EvChronicleEntry>()) {
				if (ce->entry.id < before.nextEntry) bad.push_back("chronicle entry id " + std::to_string(ce->entry.id) + " reused");
				for (const std::string& t : before.lastTexts)
					if (t == ce->entry.text) { repeats.push_back(ce->entry.type + "|" + t); break; }
			}
		});
	}
};

// The moments worth saving at, judged after a tick.
struct Moment { const char* label; bool done = false; int hits = 0; };

bool hasDisaster(const World& w, DisasterType t) {
	for (const Disaster& d : w.disasters) if (d.type == t) return true;
	return false;
}
bool hasUnit(const World& w, UnitType t) {
	for (const Unit& u : w.units) if (u.type == t) return true;
	return false;
}
bool anyWarArmy(const World& w) {
	bool war = false;
	for (const auto& kv : w.vars.rel) if (kv.second.war) war = true;
	return war && hasUnit(w, UnitType::Army);
}
bool anyPrayer(const World& w) {
	for (const Settlement& s : w.settlements) if (!s.dead && s.prayer) return true;
	return false;
}
bool anyPlague(const World& w) {
	for (const Settlement& s : w.settlements) if (!s.dead && s.plague > 0.05) return true;
	return false;
}

struct BranchResult {
	bool okAll = true;
	std::string line;
};

// Save `w` now (Save container and raw state), load twice, continue all three `years` years with
// copies of the player, and check the contract.
BranchResult branch(World& w, Abuser& player, const std::set<int>& arks, const std::string& label, double years) {
	BranchResult R;
	std::vector<uint8_t> raw, blob, again;
	std::string why;
	if (!ok(state::serialize(w, raw, &why), label + ": serialize " + why)) { R.okAll = false; return R; }
	if (!ok(save::encode(w, blob, {}, &why), label + ": save::encode " + why)) { R.okAll = false; return R; }
	auto x = save::decode(blob.data(), blob.size(), &why);
	auto y = save::decode(blob.data(), blob.size(), &why);
	if (!ok(x && y, label + ": save::decode " + why)) { R.okAll = false; return R; }
	state::serialize(*x, again);
	R.okAll &= ok(again == raw, label + ": the decoded world re-saves to the same state bytes");
	R.okAll &= eq(x->disasters.size(), w.disasters.size(), label + ": disasters kept");
	R.okAll &= eq(x->units.size(), w.units.size(), label + ": units kept");
	R.okAll &= eq(x->snapshots.size(), w.snapshots.size(), label + ": snapshots kept");
	sim::use(*x);
	sim::use(*y);
	dmt::watch(*x);
	dmt::watch(*y);
	RepeatWatch rx;
	rx.before = OneShots::of(w, arks);
	rx.attach(*x);
	std::vector<std::string> epithetsBefore;
	for (const Culture& c : w.cultures) epithetsBefore.push_back(c.epithet);

	// in-flight impacts must still land
	int flyingBefore = 0;
	for (const Disaster& d : w.disasters) if (d.type == DisasterType::Meteor || d.type == DisasterType::Comet) flyingBefore++;
	int impactsX = 0, impactsW = 0;
	const auto tx = x->bus.on<EvMeteorImpact>([&](const EvMeteorImpact&) { impactsX++; });
	const auto tw = w.bus.on<EvMeteorImpact>([&](const EvMeteorImpact&) { impactsW++; });
	// the first tick after the load, event by event, against the never-saved world
	std::vector<std::string> firstW, firstX;
	std::set<std::string> textsW;          // what the never-saved world wrote meanwhile
	const auto cw = w.bus.on<EvChronicleEntry>([&](const EvChronicleEntry& e) { textsW.insert(e.entry.type); });
	const auto fw = w.bus.onAll([&](const Event& e) { firstW.push_back(e.name()); });
	const auto fx = x->bus.onAll([&](const Event& e) { firstX.push_back(e.name()); });

	Abuser px = player, py = player;
	const int64_t ticks = int64_t(years * sim::kTicksPerYear);
	int firstImpactTickX = -1;
	for (int64_t i = 0; i < ticks; i++) {
		player.act(w);
		sim::advance(w, 1);
		px.act(*x);
		sim::advance(*x, 1);
		py.act(*y);
		sim::advance(*y, 1);
		if (i == 0) { w.bus.off(fw); x->bus.off(fx); }
		if (firstImpactTickX < 0 && impactsX > 0) firstImpactTickX = int(i);
	}
	w.bus.off(tw);
	w.bus.off(cw);
	x->bus.off(tx);
	// (a repeated TEXT alone is allowed: template shuffle bags are scratch, so a loaded world may pick the
	// template it used last time; a repeated entry of a kind the never-saved world did not write is not)
	for (const std::string& t : rx.repeats)
		if (!textsW.count(t.substr(0, t.find('|')))) rx.bad.push_back("chronicle repeats a kind the never-saved world did not write: " + t);
	R.okAll &= eq(sim::fingerprint(*x), sim::fingerprint(*y), label + ": two loads continue identically");
	R.okAll &= noNaN(*x, label + ": after the loaded run");
	R.okAll &= eq(sim::firstNonFinite(*x), std::string(), label + ": firstNonFinite");
	if (flyingBefore > 0) R.okAll &= ok(impactsX >= 1, label + ": the meteor / comet in flight still lands after the load");
	for (const std::string& b : rx.bad) { R.okAll = false; fail(label + ": after the load: " + b); }
	// epithets right after the load are the saved ones (Chronicle memory is persisted)
	int epiChanged = 0;
	for (size_t i = 0; i < epithetsBefore.size() && i < x->cultures.size(); i++) if (x->cultures[i].epithet != epithetsBefore[i]) epiChanged++;
	size_t same = 0;
	for (size_t i = 0; i < std::min(firstW.size(), firstX.size()); i++) if (firstW[i] == firstX[i]) same++;
	if (same < std::max(firstW.size(), firstX.size())) {
		std::string a, b;
		for (const std::string& s : firstW) a += " " + s;
		for (const std::string& s : firstX) b += " " + s;
		say(label + ": first tick, never-saved:" + a + " | loaded:" + b);
	}
	const double pw = w.stats.pop, px2 = x->stats.pop;
	R.line = label + ": " + std::to_string(raw.size() / 1024) + " KB raw / " + std::to_string(blob.size() / 1024) + " KB container; first tick events " +
	         std::to_string(same) + "/" + std::to_string(std::max(firstW.size(), firstX.size())) + " identical; impacts W " + std::to_string(impactsW) +
	         " X " + std::to_string(impactsX) + (firstImpactTickX >= 0 ? " (first after " + std::to_string(firstImpactTickX) + " ticks)" : std::string()) +
	         "; after " + f2(years) + " y: pop W " + f2(pw) + " X " + f2(px2) + ", settlements W " + std::to_string(liveCount(w)) + " X " +
	         std::to_string(liveCount(*x)) + ", epithets changed " + std::to_string(epiChanged);
	return R;
}

} // namespace

// ------------------------------------------------------------------ save / load at awkward moments

DM_TEST("integration adv: save / load at awkward moments of an abused L6 world (meteor in flight, volcano, war, age change, ...)") {
	if (!allReal()) return;
	const std::vector<uint32_t> seeds = longRuns() ? std::vector<uint32_t>{ 41, 42, 43 } : std::vector<uint32_t>{ 41 };
	for (uint32_t seed : seeds) {
		auto w = sim::create({ .seed = seed, .name = "", .L = 6 });
		dmt::watch(*w);
		sim::genesis::skip(*w, {});
		Abuser player(seed, true);
		player.perTick = 0.1;                  // a power every ~2.5 years, wrath on the wilds: the people live to see wars and ages
		player.spare = true;
		std::set<int> arks;
		w->bus.on<EvArkStarted>([&](const EvArkStarted& e) { arks.insert(e.culture); });
		int ageEvents = 0;
		w->bus.on<EvWorldAge>([&](const EvWorldAge&) { ageEvents++; });
		Moment moments[] = { { "meteor in flight" }, { "volcano erupting" }, { "tsunami front" }, { "storm" }, { "drought" },
		                     { "war with armies marching" }, { "settlers on the road" }, { "trader on the road" }, { "prophet travelling" },
		                     { "age change tick" }, { "brush stroke half done" }, { "plague" }, { "fire burning" }, { "pending prayer" } };
		const int nMoments = int(sizeof(moments) / sizeof(moments[0]));
		const double years = longRuns() ? 2500 : 1500;
		int lastAge = ageEvents;
		int branches = 0;
		for (int64_t i = 0; i < int64_t(years * sim::kTicksPerYear) && branches < nMoments; i++) {
			player.act(*w);
			sim::advance(*w, 1);
			const bool ageTick = ageEvents != lastAge;
			lastAge = ageEvents;
			const bool now[] = {
				hasDisaster(*w, DisasterType::Meteor), hasDisaster(*w, DisasterType::Volcano), hasDisaster(*w, DisasterType::Tsunami),
				hasDisaster(*w, DisasterType::Storm), hasDisaster(*w, DisasterType::Drought), anyWarArmy(*w), hasUnit(*w, UnitType::Settler),
				hasUnit(*w, UnitType::Trader), hasUnit(*w, UnitType::Prophet), ageTick, !player.stroke.empty() && !player.strokeFirst,
				anyPlague(*w), !w->cache.fires.empty(), anyPrayer(*w) };
			for (int k = 0; k < nMoments; k++) {
				if (moments[k].done || !now[k]) continue;
				// most moments: the second time they are seen (a later, busier world); age changes and
				// meteors at once (they are rare / short)
				if (++moments[k].hits < 2 && k != 0 && k != 9) continue;
				moments[k].done = true;
				branches++;
				const BranchResult br = branch(*w, player, arks, "seed " + std::to_string(seed) + " y" + std::to_string(int(w->year)) + " " + moments[k].label,
				                               k == 1 ? 16.0 : 8.0);
				say(br.line);
				if (!br.okAll) return;
				break;                           // one branch per tick
			}
		}
		std::string missed;
		for (const Moment& m : moments) if (!m.done) missed += std::string(" '") + m.label + "'";
		say("seed " + std::to_string(seed) + ": " + std::to_string(branches) + " of " + std::to_string(nMoments) + " moments saved and loaded by year " +
		    std::to_string(int(w->year)) + " (age " + std::to_string(w->age) + ", " + std::to_string(liveCount(*w)) + " settlements); not met:" +
		    (missed.empty() ? std::string(" none") : missed) + "; player " + player.summary());
		ok(branches >= nMoments - 3, "most awkward moments were reached and checked");
		noNaN(*w, "the abused world itself");
	}
}

DM_TEST("integration adv: genesis save / load with comets in flight: the loaded world's comets still fill the sea") {
	if (!allReal()) return;
	auto w = sim::create({ .seed = 44, .name = "", .L = 5 });
	dmt::watch(*w);
	int guard = 0;
	while (w->phase == Phase::Molten && guard++ < 400) sim::advance(*w, 1);
	if (!eq(int(w->phase), int(Phase::Water), "molten -> water")) return;
	Rng r = Rng(44).fork("adv:comets");
	int launched = 0;
	for (int t = 0; t < 40 && launched < 3; t++) {
		const int c = r.integer(0, w->n - 1);
		if (powers::can(*w, "comet", c).ok && powers::use(*w, "comet", c)) launched++;
	}
	if (!ok(launched >= 1, "a comet launched")) return;
	sim::advance(*w, 2);                      // mid-flight
	int flying = 0;
	for (const Disaster& d : w->disasters) if (d.type == DisasterType::Comet) flying++;
	if (!ok(flying >= 1, "comets still in flight")) return;
	std::vector<uint8_t> blob;
	std::string why;
	if (!ok(save::encode(*w, blob, {}, &why), "encode " + why)) return;
	auto x = save::decode(blob.data(), blob.size(), &why);
	auto y = save::decode(blob.data(), blob.size(), &why);
	if (!ok(x && y, "decode " + why)) return;
	sim::use(*x);
	sim::use(*y);
	dmt::watch(*x);
	const double sea0 = x->seaLevel;
	int impacts = 0;
	x->bus.on<EvMeteorImpact>([&](const EvMeteorImpact& e) { if (e.kind == ImpactKind::Comet) impacts++; });
	sim::advance(*w, 40);
	sim::advance(*x, 40);
	sim::advance(*y, 40);
	eq(impacts, flying, "every comet in flight at the save lands in the loaded world");
	ok(x->seaLevel > sea0 + 0.5, "the sea rose in the loaded world (" + f2(sea0) + " -> " + f2(x->seaLevel) + ")");
	near(x->seaLevel, w->seaLevel, 1e-6, "same sea as the never-saved world");
	eq(x->disasters.size(), size_t(0), "no comet left hanging");
	eq(sim::fingerprint(*x), sim::fingerprint(*y), "two loads continue identically");
	noNaN(*x, "after the comets");
}

// ------------------------------------------------------------------ god mode: every power, every tick

DM_TEST("integration adv: god mode, powers every tick for centuries: caps, ranges, budget, determinism") {
	if (!allReal()) return;
	struct Case { int L; uint32_t seed; double years; };
	std::vector<Case> cases = { { 5, 45, 400 }, { 6, 46, 150 } };
	if (longRuns()) cases = { { 5, 45, 1500 }, { 5, 47, 1500 }, { 6, 46, 600 }, { 6, 48, 600 } };
	for (const Case& K : cases) {
		auto a = sim::create({ .seed = K.seed, .name = "", .L = K.L });
		auto b = sim::create({ .seed = K.seed, .name = "", .L = K.L });
		dmt::watch(*a);
		sim::genesis::skip(*a, {});
		sim::genesis::skip(*b, {});
		Abuser pa(K.seed, true), pb(K.seed, true);
		std::map<std::string, int> ev;
		a->bus.onAll([&](const Event& e) { ev[e.name()]++; });
		double worst = 0, sum = 0, worstYear = 0;
		int64_t n = 0;
		int maxLive = 0, maxUnits = 0, maxDis = 0;
		size_t maxLava = 0;
		double elevMin = 1e9, elevMax = -1e9;
		bool fine = true;
		for (int64_t i = 0; i < int64_t(K.years * sim::kTicksPerYear) && fine; i++) {
			pa.act(*a);
			const double t0 = nowMs();
			sim::advance(*a, 1);
			const double dt = nowMs() - t0;
			pb.act(*b);
			sim::advance(*b, 1);
			sum += dt;
			n++;
			if (dt > worst) { worst = dt; worstYear = a->year; }
			maxLive = std::max(maxLive, liveCount(*a));
			maxUnits = std::max(maxUnits, int(a->units.size()));
			maxDis = std::max(maxDis, int(a->disasters.size()));
			for (const Disaster& d : a->disasters) maxLava = std::max(maxLava, d.cells.size());
			if (i % 40 == 0) {
				for (int c = 0; c < a->n; c++) { elevMin = std::min(elevMin, double(a->elev[size_t(c)])); elevMax = std::max(elevMax, double(a->elev[size_t(c)])); }
				fine &= noNaN(*a, "god mode, year " + std::to_string(int(a->year)));
			}
		}
		const std::string tag = "L" + std::to_string(K.L) + " seed " + std::to_string(K.seed);
		say(tag + ": " + f2(a->year) + " years, ticks mean " + f2(sum / std::max<int64_t>(1, n)) + " ms, worst " + f2(worst) + " ms (year " +
		    f2(worstYear) + "); peak live " + std::to_string(maxLive) + ", units " + std::to_string(maxUnits) + ", disasters " + std::to_string(maxDis) +
		    ", lava field " + std::to_string(maxLava) + " cells; elev " + f2(elevMin) + " .. " + f2(elevMax) + "; now " + std::to_string(liveCount(*a)) +
		    " settlements, pop " + f2(a->stats.pop) + ", age " + std::to_string(a->age) + ", " + std::to_string(a->cultures.size()) + " cultures ever, chronicle " +
		    std::to_string(a->chronicle.size()));
		say(tag + " player: " + pa.summary());
		std::string evs;
		for (const char* k : { "settlement:destroyed", "settlement:founded", "meteor:impact", "volcano:start", "tsunami:hit", "quake", "battle",
		                       "people:lost", "culture:born", "miracle:witnessed", "awe:short" })
			evs += std::string(" ") + k + " " + std::to_string(ev[k]);
		say(tag + " events:" + evs);
		ok(maxLive <= 160, tag + ": at most 160 live settlements");
		ok(maxUnits <= 300, tag + ": at most 300 units");
		ok(maxLava <= 400, tag + ": lava fields bounded at 400 cells");
		ok(elevMin >= state::kElevMin - 1e-6 && elevMax <= state::kElevMax + 1e-6, tag + ": elevation within the hard clamp");
		ok(sum / std::max<int64_t>(1, n) <= 12.5, tag + ": mean tick within 5x the 2.5 ms budget");
		ok(worst <= 150, tag + ": no tick above 5x the 30 ms budget");
		eq(sim::fingerprint(*a), sim::fingerprint(*b), tag + ": two abused worlds stay identical");
		eq(sim::firstNonFinite(*a), std::string(), tag + ": firstNonFinite");
		ok(pa.uses > int(K.years), tag + ": the god acted (" + std::to_string(pa.uses) + " clicks)");
	}
}

// ------------------------------------------------------------------ a god who wakes people everywhere

DM_TEST("integration adv: awaken flood (god mode): cultures, colours, the 160 cap and the unit cap hold") {
	if (!allReal()) return;
	auto w = sim::create({ .seed = 49, .name = "", .L = 6 });
	dmt::watch(*w);
	sim::genesis::skip(*w, {});
	Rng r = Rng(49).fork("adv:flood");
	int woke = 0, refused = 0, maxLive = 0, maxCult = 0, maxUnits = 0;
	std::map<std::string, int> reasons;
	for (int y = 0; y < 300; y++) {
		w->god.aweMax = w->god.awe = 1200;
		for (int t = 0; t < 6; t++) {
			const int c = r.integer(0, w->n - 1);
			const powers::CanResult cr = powers::can(*w, "awaken", c);
			if (!cr.ok) { reasons[powers::toStr(cr.reason)]++; continue; }
			w->god.awe = 1200;
			if (powers::use(*w, "awaken", c, {})) woke++;
			else refused++;
		}
		sim::advance(*w, sim::kTicksPerYear);
		int cult = 0;
		for (const Culture& cu : w->cultures) if (!cu.dead) cult++;
		maxLive = std::max(maxLive, liveCount(*w));
		maxCult = std::max(maxCult, cult);
		maxUnits = std::max(maxUnits, int(w->units.size()));
	}
	// colours of the living cultures: the palette has 6 slots
	std::map<std::string, int> colours;
	std::set<std::string> names;
	int living = 0;
	for (const Culture& cu : w->cultures) {
		if (cu.dead) continue;
		living++;
		char b[64];
		std::snprintf(b, sizeof b, "%.3f,%.3f,%.3f", cu.color[0], cu.color[1], cu.color[2]);
		colours[b]++;
		names.insert(cu.name);
	}
	std::string rs;
	for (const auto& kv : reasons) rs += " " + kv.first + " " + std::to_string(kv.second);
	say("awaken flood: " + std::to_string(woke) + " awakenings (" + std::to_string(refused) + " refused after can; can refused:" + rs + "); peak live " +
	    std::to_string(maxLive) + " settlements, " + std::to_string(maxCult) + " living cultures, " + std::to_string(maxUnits) + " units; now " +
	    std::to_string(living) + " living cultures with " + std::to_string(colours.size()) + " distinct colours and " + std::to_string(names.size()) +
	    " distinct names; " + std::to_string(w->cultures.size()) + " cultures ever; chronicle " + std::to_string(w->chronicle.size()));
	ok(woke >= 5, "the god woke several peoples");
	ok(maxLive <= 160, "at most 160 live settlements");
	ok(maxUnits <= 300, "at most 300 units");
	eq(names.size(), size_t(living), "living cultures have distinct names");
	ok(living <= 6 || colours.size() == 6, "living cultures beyond the palette still use its colours");
	noNaN(*w, "after the flood");
	eq(sim::firstNonFinite(*w), std::string());
	std::vector<uint8_t> raw;
	ok(state::serialize(*w, raw), "the flooded world saves");
	auto x = state::deserialize(raw);
	ok(x != nullptr, "and loads");
}
