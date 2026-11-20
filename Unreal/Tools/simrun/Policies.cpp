// DEMIURGE — Tools/simrun/Policies.cpp : scripted players (see Policies.h). Owner: sim engineer (A14).
#include "Policies.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "Civ.h"
#include "Powers.h"
#include "Sim.h"
#include "Society.h"

namespace dm::simrun {
namespace {

bool alive(const World& w) {
	for (const Settlement& s : w.settlements) if (!s.dead) return true;
	for (const Unit& u : w.units) if (u.type == UnitType::Settler) return true;
	return false;
}

// live settlement ids, most populous first (ties by id)
std::vector<int> byPop(const World& w) {
	std::vector<int> ids = civ::live(w);
	std::stable_sort(ids.begin(), ids.end(), [&](int a, int b) {
		const double pa = w.settlements[size_t(a)].pop, pb = w.settlements[size_t(b)].pop;
		if (pa != pb) return pa > pb;
		return a < b;
	});
	return ids;
}

bool anyWar(const World& w, int culture) {
	for (const Culture& c : w.cultures) {
		if (c.dead || c.id == culture) continue;
		if (society::atWar(w, culture, c.id)) return true;
	}
	return false;
}

// The culture most likely to launch the Ark (highest age, then most people; ties by id), or -1.
int leadingCulture(const World& w) {
	int best = -1;
	for (const Culture& c : w.cultures) {
		if (c.dead) continue;
		if (best < 0) { best = c.id; continue; }
		const Culture& b = w.cultures[size_t(best)];
		if (c.age > b.age || (c.age == b.age && c.pop > b.pop)) best = c.id;
	}
	return best;
}

// The `k` most populous live settlements of a culture (all cultures if culture < 0).
std::vector<int> topOf(const World& w, int culture, size_t k) {
	std::vector<int> out;
	for (int sid : byPop(w)) {
		if (culture >= 0 && w.settlements[size_t(sid)].culture != culture) continue;
		out.push_back(sid);
		if (out.size() >= k) break;
	}
	return out;
}

} // namespace

const char* toStr(PolicyKind k) {
	switch (k) {
	case PolicyKind::Passive: return "passive";
	case PolicyKind::Kind: return "kind";
	case PolicyKind::Cruel: return "cruel";
	case PolicyKind::Chaotic: return "chaotic";
	case PolicyKind::Doom: return "doom";
	case PolicyKind::Curious: return "curious";
	case PolicyKind::BlessOnly: return "blessonly";
	case PolicyKind::Prayers: return "prayers";
	case PolicyKind::Repent: return "repent";
	case PolicyKind::Sculptor: return "sculptor";
	case PolicyKind::LateKind: return "late-kind";
	case PolicyKind::LateCruel: return "late-cruel";
	case PolicyKind::InspireSpam: return "inspire-spam";
	case PolicyKind::Hoarder: return "hoarder";
	case PolicyKind::Builder: return "builder";
	case PolicyKind::AtomicKind: return "atomic-kind";
	case PolicyKind::AtomicCruel: return "atomic-cruel";
	case PolicyKind::Peacemaker: return "peacemaker";
	case PolicyKind::Favourite: return "favourite";
	case PolicyKind::Scourge: return "scourge";
	default: return "?";
	}
}

bool policyFromStr(std::string_view s, PolicyKind& out) {
	for (int i = 0; i < int(PolicyKind::Count); i++) {
		const PolicyKind k = PolicyKind(i);
		if (s == toStr(k)) { out = k; return true; }
	}
	return false;
}

Policy::Policy(PolicyKind kind, uint32_t seed) : kind_(kind), rng_(Rng(seed ^ 0x5eed5eedu).fork("simrun:policy")) {}

bool Policy::click(World& w, const char* id, int cell) {
	if (!powers::can(w, id, cell).ok) return false;
	powers::UseOpts o;
	if (powers::use(w, id, cell, o)) {
		stats_.clicks++;
		stats_.byPower[id]++;
		return true;
	}
	stats_.refused++;
	return false;
}

bool Policy::startStroke(World& w, const char* id, int cell, int ticks, double ang) {
	if (!strokeId_.empty() || ticks <= 0) return false;
	if (!powers::can(w, id, cell).ok) return false;
	strokeId_ = id;
	strokeCell_ = cell;
	strokeLeft_ = ticks;
	strokeFirst_ = true;
	strokeAng_ = ang;
	stats_.strokes++;
	stats_.byPower[id]++;
	return true;
}

void Policy::continueStroke(World& w) {
	if (strokeId_.empty()) return;
	powers::UseOpts o;
	o.ang = strokeAng_;
	o.dt = strokeFirst_ ? 0.0 : sim::kRealPerTick;
	strokeFirst_ = false;
	const bool used = powers::use(w, strokeId_, strokeCell_, o);
	if (--strokeLeft_ <= 0 || !used) {
		strokeId_.clear();
		strokeLeft_ = 0;
	}
}

bool Policy::reawaken(World& w) {
	if (w.phase != Phase::History || alive(w)) return false;
	const int site = civ::bestSite(w);
	return w.valid(site) && click(w, "awaken", site);
}

void Policy::before(World& w) {
	if (kind_ == PolicyKind::Passive) return;
	if (w.phase != Phase::History) {          // genesis is played by the runner; ended worlds are left alone
		strokeId_.clear();
		return;
	}
	continueStroke(w);
	if (kind_ == PolicyKind::LateKind || kind_ == PolicyKind::LateCruel) {
		if (lateFrom_ < 0) {
			for (const Settlement& s : w.settlements)
				if (!s.dead && s.ark > 0) { lateFrom_ = w.year; break; }
			if (lateFrom_ < 0) return;
			nextYear_ = 0;
		}
	}
	if (kind_ == PolicyKind::AtomicKind || kind_ == PolicyKind::AtomicCruel) {
		if (lateFrom_ < 0) {
			if (w.age < Age::ATOMIC) return;
			lateFrom_ = w.year;
			nextYear_ = 0;
		}
	}
	if (w.year + 1e-9 < nextYear_) return;
	nextYear_ = std::floor(w.year) + 1;
	const double y = w.year;
	switch (kind_) {
	case PolicyKind::Kind: case PolicyKind::LateKind: case PolicyKind::AtomicKind: decideKind(w); break;
	case PolicyKind::Cruel: case PolicyKind::LateCruel: case PolicyKind::AtomicCruel: decideCruel(w, true); break;
	case PolicyKind::Doom: decideCruel(w, false); break;
	case PolicyKind::Chaotic: decideChaotic(w); break;
	case PolicyKind::Curious:
		if (y < 900) smallBlessings(w);
		else if (y < 1100) wrathBurst(w);
		break;
	case PolicyKind::BlessOnly:
		if (y < 1000) smallBlessings(w);
		break;
	case PolicyKind::Prayers: case PolicyKind::Hoarder:
		if (!reawaken(w)) answerPrayers(w, 3);
		break;
	case PolicyKind::Builder:
		if (reawaken(w)) break;
		answerPrayers(w, 3);
		consecrate(w);
		break;
	case PolicyKind::Repent:
		if (y < 800) wrathBurst(w);
		else { answerPrayers(w, 2); smallBlessings(w); }
		break;
	case PolicyKind::Sculptor: {
		// a curious player who plays with Raise / Lower Land near the people every ~15 years
		const std::vector<int> ids = civ::live(w);
		if (!ids.empty() && rng_.chance(1.0 / 15)) {
			const int sid = ids[size_t(rng_.pickIndex(int(ids.size())))];
			const int cell = w.settlements[size_t(sid)].cell;
			if (w.valid(cell)) startStroke(w, rng_.chance(0.5) ? "raise" : "lower", cell, 8);   // one second of brush
		}
		reawaken(w);
		break;
	}
	case PolicyKind::InspireSpam: {
		if (reawaken(w)) break;
		const int lead = leadingCulture(w);
		if (lead < 0) break;
		const Settlement* cap = state::settlement(w, w.cultures[size_t(lead)].capital);
		if (cap && !cap->dead) click(w, "inspire", cap->cell);
		break;
	}
	case PolicyKind::Peacemaker: decidePeacemaker(w); break;
	case PolicyKind::Favourite: decideFavourite(w); break;
	case PolicyKind::Scourge: decideScourge(w); break;
	default: break;
	}
}

// ---------------------------------------------------------------- the design review's players

void Policy::answerPrayers(World& w, int maxN) {
	int n = 0;
	for (int sid : byPop(w)) {
		if (n >= maxN) break;
		const Settlement& s = w.settlements[size_t(sid)];
		if (!s.prayer) continue;
		const int cell = s.cell;
		bool did = false;
		switch (s.prayer->kind) {
		case PrayerKind::Heal: did = click(w, "heal", cell); break;
		case PrayerKind::Peace: did = click(w, "peace", cell); break;
		case PrayerKind::Rain: did = startStroke(w, "rain", cell, 8); break;
		case PrayerKind::Bloom: did = startStroke(w, "bloom", cell, 8) || startStroke(w, "rain", cell, 8); break;
		}
		if (did) n++;
	}
}

void Policy::smallBlessings(World& w) {
	answerPrayers(w, 2);
	for (int sid : byPop(w)) {
		const Settlement& s = w.settlements[size_t(sid)];
		if (s.plague > 0.2 && click(w, "heal", s.cell)) break;
	}
	const std::vector<int> ids = civ::live(w);
	if (ids.empty()) return;
	// a new player dabs rain / bloom on a random town about every other year
	if (rng_.chance(0.5)) {
		const int cell = w.settlements[size_t(ids[size_t(rng_.pickIndex(int(ids.size())))])].cell;
		if (!startStroke(w, "bloom", cell, 4)) startStroke(w, "rain", cell, 4);
	}
	if (w.god.awe > 100 && rng_.chance(0.2)) click(w, "inspire", w.settlements[size_t(ids[size_t(rng_.pickIndex(int(ids.size())))])].cell);
}

void Policy::wrathBurst(World& w) {
	// a curious player wants to see the big ones: save for the greatest unlocked wrath the Awe can
	// hold, throwing the odd lightning while waiting
	const std::vector<int> ids = byPop(w);
	if (ids.empty()) return;
	static const char* const kGreat[] = { "meteor", "volcano", "quake", "plague" };
	const char* great = nullptr;
	for (const char* id : kGreat)
		if (powers::unlocked(w, id) && powers::cost(w, id) <= w.god.aweMax) { great = id; break; }
	const int cell = w.settlements[size_t(ids[size_t(rng_.pickIndex(int(std::min<size_t>(3, ids.size()))))])].cell;
	if (great && click(w, great, cell)) return;
	for (const char* id : kGreat) if (click(w, id, cell)) return;
	if (!great || rng_.chance(0.25)) click(w, "lightning", cell);
}

// builder: consecrate the largest shrine without a temple, or raise a Wonder in a capital
bool Policy::consecrate(World& w) {
	if (!powers::byId("consecrate")) return false;
	for (int sid : byPop(w)) {
		const Settlement& s = w.settlements[size_t(sid)];
		if (powers::can(w, "consecrate", s.cell).ok && click(w, "consecrate", s.cell)) return true;
	}
	return false;
}

void Policy::decideKind(World& w) {
	if (reawaken(w)) return;
	const std::vector<int> ids = byPop(w);
	int actions = 0;
	// 0. (sim-findings-2 verifier) a kind god stops a war before it answers the next prayer: one Omen of Peace on
	//    the largest settlement of a warring culture (it used to wait behind two prayers, so a busy year left the war)
	for (int sid : ids) {
		const Settlement& s = w.settlements[size_t(sid)];
		if (anyWar(w, s.culture) && click(w, "peace", s.cell)) { actions++; break; }
	}
	// 1. answer prayers, largest settlements first
	for (int sid : ids) {
		if (actions >= 2) break;
		const Settlement& s = w.settlements[size_t(sid)];
		if (!s.prayer) continue;
		const int cell = s.cell;
		switch (s.prayer->kind) {
		case PrayerKind::Heal: if (click(w, "heal", cell)) actions++; break;
		case PrayerKind::Peace: if (click(w, "peace", cell)) actions++; break;
		case PrayerKind::Rain: if (startStroke(w, "rain", cell, 8)) actions++; break;
		case PrayerKind::Bloom:
			if (startStroke(w, "bloom", cell, 8) || startStroke(w, "rain", cell, 8)) actions++;
			break;
		}
	}
	// 2. plagues nobody prays about yet
	for (int sid : ids) {
		if (actions >= 2) break;
		const Settlement& s = w.settlements[size_t(sid)];
		if (s.plague > 0.2 && click(w, "heal", s.cell)) actions++;
	}
	// 3. wars: an Omen of Peace on the largest settlement of a warring culture; (v3.1, sim-findings-2) and,
	//    with Awe to spare, on a people on the brink of war (society::tense: the Omen calms it beforehand)
	for (int sid : ids) {
		if (actions >= 2) break;
		const Settlement& s = w.settlements[size_t(sid)];
		if (anyWar(w, s.culture) && click(w, "peace", s.cell)) actions++;
	}
	for (int sid : ids) {
		if (actions >= 2 || w.god.awe < powers::cost(w, "peace") + 40) break;
		const Settlement& s = w.settlements[size_t(sid)];
		if (!anyWar(w, s.culture) && !society::tense(w, s.culture).empty() && click(w, "peace", s.cell)) actions++;
	}
	// 4. a prophet (and his edict) for the leading culture; for the others only with Awe to spare
	const int lead = leadingCulture(w);
	for (const Culture& cu : w.cultures) {
		if (actions >= 2) break;
		if (cu.dead || cu.prophet || cu.capital < 0) continue;
		if (cu.id != lead && w.god.awe < 0.8 * w.god.aweMax) continue;
		const Settlement* cap = state::settlement(w, cu.capital);
		if (!cap || cap->dead) continue;
		if (w.god.awe < powers::cost(w, "prophet") + 20) continue;   // keep something for prayers
		const int cid = cu.id;
		if (click(w, "prophet", cap->cell)) {
			actions++;
			const Edict e = anyWar(w, cid) ? Edict::Peace : Edict::Build;
			if (society::setEdict(w, cid, e)) stats_.edicts++;
		}
	}
	// 5. every year, win the hearts of the people most likely to reach the stars: a bloom (rain
	//    before Bloom unlocks) over the least loved of the leading culture's 24 largest towns;
	//    with Awe to spare also inspire one
	if (lead < 0) return;
	// (integration) while the leading culture has no prophet, save up for one instead of blooming:
	// a prophet is what keeps a late-age culture devout (baseline +0.15 and his visits)
	{
		const Culture& lc = w.cultures[size_t(lead)];
		const Settlement* lcap = state::settlement(w, lc.capital);
		const bool wantProphet = !lc.prophet && lcap && !lcap->dead && powers::unlocked(w, "prophet");
		if (wantProphet && w.god.awe < powers::cost(w, "prophet") + 20) return;
	}
	std::vector<int> top = topOf(w, lead, 24);
	std::stable_sort(top.begin(), top.end(), [&](int a, int b) { return w.settlements[size_t(a)].love < w.settlements[size_t(b)].love; });
	if (!top.empty()) {
		const int cell = w.settlements[size_t(top[0])].cell;     // half a second: two witness pulses
		if (!startStroke(w, "bloom", cell, 4)) startStroke(w, "rain", cell, 4);
	}
	if (actions < 2 && w.god.awe > 0.6 * w.god.aweMax) {
		for (int sid : top) {
			const Settlement& s = w.settlements[size_t(sid)];
			if (s.inspire <= 0 && click(w, "inspire", s.cell)) break;
		}
	}
}

void Policy::decideCruel(World& w, bool restrained) {
	const std::vector<int> ids = byPop(w);
	if (ids.empty()) return;
	if (restrained) {
		// a tyrant who stops short of extinction (never while fewer than 4 settlements live) and
		// rules by fear: earthquakes (which frighten a whole region) on the least frightened of the
		// leading culture's 12 largest towns whenever affordable, lightning while saving up, and a
		// great wrath (volcano, meteor) only when the Awe would otherwise be wasted at its cap
		if (ids.size() < 4) return;
		std::vector<int> top = topOf(w, leadingCulture(w), 12);
		if (top.empty()) return;
		std::stable_sort(top.begin(), top.end(), [&](int a, int b) { return w.settlements[size_t(a)].love > w.settlements[size_t(b)].love; });
		const int target = w.settlements[size_t(top[0])].cell;
		const bool atCap = w.god.awe >= 0.9 * w.god.aweMax;
		if (atCap && (click(w, "volcano", target) || click(w, "meteor", target))) return;
		if (click(w, "quake", target)) return;
		const bool quakeLater = powers::unlocked(w, "quake") && powers::cost(w, "quake") <= w.god.aweMax;
		if (!quakeLater || rng_.chance(0.3)) click(w, "lightning", target);
		return;
	}
	// doom: no restraint. Let the people grow past the 1,000 a silence needs while hoarding Awe,
	// then drown their towns one after another with Lower Land (the lowest above the sea first)
	// and strike the largest with every great wrath it can pay for
	if (w.stats.peakPop < 1000) return;
	static const char* const kGreat[] = { "meteor", "volcano", "quake", "plague" };
	if (strokeId_.empty()) {
		int low = -1;
		double lowH = 1e9;
		for (int sid : ids) {
			const Settlement& s = w.settlements[size_t(sid)];
			const double h = double(w.elev[size_t(s.cell)]) - w.seaLevel;
			if (h < lowH) { lowH = h; low = sid; }
		}
		if (low >= 0) {
			const int ticks = std::clamp(int(std::ceil((lowH + 0.3) / 0.2 / sim::kRealPerTick)), 8, 160);
			startStroke(w, "lower", w.settlements[size_t(low)].cell, ticks, 0.04);
		}
	}
	const double reserve = strokeId_.empty() ? 0 : 60;
	int done = 0;
	for (int sid : ids) {
		if (done >= 3) break;
		bool hit = false;
		for (const char* id : kGreat) {
			if (hit || w.god.awe < powers::cost(w, id) + reserve) continue;
			hit = click(w, id, w.settlements[size_t(sid)].cell);
		}
		if (!hit && w.god.awe >= 0.9 * w.god.aweMax) hit = click(w, "lightning", w.settlements[size_t(sid)].cell);
		if (!hit) break;
		done++;
	}
}

// ---------------------------------------------------------------- the verifier's adversarial players

void Policy::decidePeacemaker(World& w) {
	if (reawaken(w)) return;
	int n = 0;
	std::vector<int> done;                  // one Omen per people a year
	for (int sid : byPop(w)) {
		if (n >= 3) break;
		const Settlement& s = w.settlements[size_t(sid)];
		if (std::find(done.begin(), done.end(), s.culture) != done.end()) continue;
		if (!anyWar(w, s.culture) && society::tense(w, s.culture).empty()) continue;
		done.push_back(s.culture);
		if (click(w, "peace", s.cell)) n++;
	}
}

void Policy::decideFavourite(World& w) {
	if (reawaken(w)) return;
	if (chosen_ < 0 || size_t(chosen_) >= w.cultures.size() || w.cultures[size_t(chosen_)].dead) {
		chosen_ = -1;
		double best = -1;
		for (const Culture& c : w.cultures)
			if (!c.dead && c.pop > best) { best = c.pop; chosen_ = c.id; }
		if (chosen_ < 0) return;
	}
	const std::vector<int> mine = topOf(w, chosen_, 1000);
	if (mine.empty()) return;
	int actions = 0;
	for (int sid : mine) {
		if (actions >= 2) break;
		const Settlement& s = w.settlements[size_t(sid)];
		if (s.prayer) {
			bool did = false;
			switch (s.prayer->kind) {
			case PrayerKind::Heal: did = click(w, "heal", s.cell); break;
			case PrayerKind::Peace: did = click(w, "peace", s.cell); break;
			case PrayerKind::Rain: did = startStroke(w, "rain", s.cell, 8); break;
			case PrayerKind::Bloom: did = startStroke(w, "bloom", s.cell, 8) || startStroke(w, "rain", s.cell, 8); break;
			}
			if (did) actions++;
		} else if (s.plague > 0.2 && click(w, "heal", s.cell)) actions++;
	}
	std::vector<int> top(mine.begin(), mine.begin() + std::min<size_t>(24, mine.size()));
	std::stable_sort(top.begin(), top.end(), [&](int a, int b) { return w.settlements[size_t(a)].love < w.settlements[size_t(b)].love; });
	if (!startStroke(w, "bloom", w.settlements[size_t(top[0])].cell, 4)) startStroke(w, "rain", w.settlements[size_t(top[0])].cell, 4);
	if (w.god.awe > 0.6 * w.god.aweMax)
		for (int sid : top)
			if (w.settlements[size_t(sid)].inspire <= 0 && click(w, "inspire", w.settlements[size_t(sid)].cell)) break;
}

void Policy::decideScourge(World& w) {
	reawaken(w);
	int liveCultures = 0;
	for (const Culture& c : w.cultures)
		if (!c.dead && !topOf(w, c.id, 1).empty()) liveCultures++;
	if (liveCultures < 2) return;          // never the last people
	if (chosen_ < 0 || size_t(chosen_) >= w.cultures.size() || w.cultures[size_t(chosen_)].dead || topOf(w, chosen_, 1).empty()) {
		chosen_ = -1;
		double best = 1e300;
		for (const Culture& c : w.cultures)
			if (!c.dead && !topOf(w, c.id, 1).empty() && c.pop < best) { best = c.pop; chosen_ = c.id; }
		if (chosen_ < 0) return;
	}
	const std::vector<int> theirs = topOf(w, chosen_, 3);
	const int cell = w.settlements[size_t(theirs[size_t(rng_.pickIndex(int(theirs.size())))])].cell;
	if (click(w, "quake", cell)) return;
	const bool quakeLater = powers::unlocked(w, "quake") && powers::cost(w, "quake") <= w.god.aweMax;
	if (!quakeLater || rng_.chance(0.3)) click(w, "lightning", cell);
}

void Policy::decideChaotic(World& w) {
	if (reawaken(w)) return;
	if (!rng_.chance(0.5)) return;
	std::vector<const powers::PowerDef*> pool;
	for (const powers::PowerDef& d : powers::kPowers) {
		if (d.group == powers::Group::Genesis) continue;
		if (powers::unlocked(w, d.id)) pool.push_back(&d);
	}
	if (pool.empty()) return;
	const powers::PowerDef* d = pool[size_t(rng_.pickIndex(int(pool.size())))];
	const std::vector<int> ids = civ::live(w);
	int cell = -1;
	for (int tries = 0; tries < 24 && cell < 0; tries++) {
		int c = -1;
		if (d->target == powers::Target::Settlement || (!ids.empty() && rng_.chance(0.5))) {
			if (ids.empty()) break;
			c = w.settlements[size_t(ids[size_t(rng_.pickIndex(int(ids.size())))])].cell;
		} else {
			c = rng_.integer(0, w.n - 1);
		}
		if (powers::can(w, d->id, c).ok) cell = c;
	}
	if (cell < 0) return;
	if (d->brush) startStroke(w, d->id, cell, rng_.integer(4, 16), rng_.range(0.03, 0.15));
	else click(w, d->id, cell);
}

} // namespace dm::simrun
