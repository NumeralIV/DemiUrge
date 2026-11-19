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
	default: return "doom";
	}
}

bool policyFromStr(std::string_view s, PolicyKind& out) {
	for (PolicyKind k : { PolicyKind::Passive, PolicyKind::Kind, PolicyKind::Cruel, PolicyKind::Chaotic, PolicyKind::Doom }) {
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
	if (w.year + 1e-9 < nextYear_) return;
	nextYear_ = std::floor(w.year) + 1;
	switch (kind_) {
	case PolicyKind::Kind: decideKind(w); break;
	case PolicyKind::Cruel: decideCruel(w, true); break;
	case PolicyKind::Doom: decideCruel(w, false); break;
	case PolicyKind::Chaotic: decideChaotic(w); break;
	default: break;
	}
}

void Policy::decideKind(World& w) {
	if (reawaken(w)) return;
	const std::vector<int> ids = byPop(w);
	int actions = 0;
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
	// 3. wars: an Omen of Peace on the largest settlement of a warring culture
	for (int sid : ids) {
		if (actions >= 2) break;
		const Settlement& s = w.settlements[size_t(sid)];
		if (anyWar(w, s.culture) && click(w, "peace", s.cell)) actions++;
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
