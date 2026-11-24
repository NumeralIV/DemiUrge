// DEMIURGE — Tools/simrun/Policies.cpp : scripted players (see Policies.h). Owner: sim engineer (A14).
#include "Policies.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Cards.h"
#include "Characters.h"
#include "Civ.h"
#include "Doctrine.h"
#include "Mythos.h"
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
	case PolicyKind::ResponderMyth: return "responder-myth";
	case PolicyKind::RandomMyth: return "random-myth";
	case PolicyKind::Lawgiver: return "lawgiver";
	case PolicyKind::Valour: return "valour";
	case PolicyKind::ExactOnly: return "exact-only";
	case PolicyKind::ParableOnly: return "parable-only";
	case PolicyKind::SignOnly: return "sign-only";
	case PolicyKind::UnseenKind: return "unseen-kind";
	case PolicyKind::MsgKind: return "msg-kind";
	case PolicyKind::JudgeDeeds: return "judge-deeds";
	case PolicyKind::JudgeHearts: return "judge-hearts";
	case PolicyKind::Merciful: return "merciful";
	case PolicyKind::Stern: return "stern";
	case PolicyKind::Kingmaker: return "kingmaker";
	case PolicyKind::Gentle: return "gentle";
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

Policy::Policy(PolicyKind kind, uint32_t seed, const MythosPlay& play)
	: play_(play), kind_(kind), rng_(Rng(seed ^ 0x5eed5eedu).fork("simrun:policy")), cardRng_(Rng(seed ^ 0x5eed5eedu).fork("simrun:policy:myth")) {}

bool Policy::click(World& w, const char* id, int cell) {
	// (v4 mythos, M9) a mythos policy raises a messenger through the MessengerChoice card (SPEC-MYTHOS 3.8)
	if (chooseMessenger() && std::string_view(id) == "prophet" && w.tune<MythosTune>().phase >= 1) return armMessenger(w, cell);
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
	const bool cards = cardsOn();
	if (kind_ == PolicyKind::Passive && !cards) return;
	if (w.phase != Phase::History) {          // genesis is played by the runner; ended worlds are left alone
		strokeId_.clear();
		return;
	}
	continueStroke(w);
	if (cards) playCards(w);                  // (v4 mythos, M9) open cards are answered as soon as they show
	if (kind_ == PolicyKind::Passive) return;
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
	// (v4 mythos, M9) the v1 scripted gods (SPEC-MYTHOS 5.3)
	case PolicyKind::ResponderMyth: case PolicyKind::UnseenKind: decideKind(w); break;
	case PolicyKind::RandomMyth: decideChaotic(w); break;
	// (V1) stern plays the same kind god as merciful (SPEC-MYTHOS 3.12 compares one judging habit with the other):
	// as a passive god it measured passive against kind, not punishment against mercy (it ended forgotten 8/8)
	case PolicyKind::MsgKind: case PolicyKind::Merciful: case PolicyKind::Gentle: case PolicyKind::Stern:
		decideMythos(w);                      // the messenger and the words first: the kind player spends the rest
		decideKind(w);
		break;
	case PolicyKind::Lawgiver: case PolicyKind::Valour: case PolicyKind::ExactOnly: case PolicyKind::ParableOnly: case PolicyKind::SignOnly:
	case PolicyKind::JudgeDeeds: case PolicyKind::JudgeHearts:
		decideMythos(w);                      // otherwise passive
		break;
	default: break;                           // Kingmaker: only its cards
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
		// (v4 mythos, M9) the Word powers only for random-myth: chaotic stays the v3.1 player at every phase
		if (d.group == powers::Group::Word && kind_ != PolicyKind::RandomMyth) continue;
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
	if (d->group == powers::Group::Word) { randomWord(w, cell); return; }
	if (d->brush) startStroke(w, d->id, cell, rng_.integer(4, 16), rng_.range(0.03, 0.15));
	else click(w, d->id, cell);
}

// ---------------------------------------------------------------- (v4 mythos, M9) cards and words (SPEC-MYTHOS 5.3)
//
// Every mythos act goes through the player's own doors: cards::answer / cards::options (the card buttons),
// powers::use("speak", cell, { word }) after doctrine::canSpeak (the Word chooser), powers::use("prophet", cell,
// { choose }) (the Messenger chooser). Card and word choices draw from cardRng_ only, so a base player (kind,
// chaotic) inside a mythos policy keeps its own rng_ draws.

namespace {

int mythPhase(const World& w) { return w.tune<MythosTune>().phase; }

// The town a word to a culture is spoken at: its capital, else its most populous live town; -1 none.
int townOf(const World& w, int culture) {
	const Culture* cu = state::culture(w, culture);
	if (!cu || cu->dead) return -1;
	if (const Settlement* cap = state::settlement(w, cu->capital); cap && !cap->dead && cap->culture == culture) return cap->id;
	const std::vector<int> top = topOf(w, culture, 1);
	return top.empty() ? -1 : top[0];
}

using DecreeSet = std::vector<std::pair<DecreeArea, int>>;
// 3.6 lawgiver: Peace, Share, Temples, Truth, Guarded, Shelter (+ Stewardship, phase 2)
const DecreeSet& lawSet() {
	static const DecreeSet s{ { DecreeArea::Violence, int(ViolenceStance::Peace) }, { DecreeArea::Wealth, int(WealthStance::Share) },
		{ DecreeArea::Worship, int(Rite::Temples) }, { DecreeArea::Honesty, int(HonestyStance::Truth) },
		{ DecreeArea::Hospitality, int(HospitalityStance::Guarded) }, { DecreeArea::Care, int(CareStance::Shelter) },
		{ DecreeArea::Land, int(LandStance::Stewardship) } };
	return s;
}
// 3.6 valour: Valour, Gain, Closed, Strength
const DecreeSet& valourSet() {
	static const DecreeSet s{ { DecreeArea::Violence, int(ViolenceStance::Valour) }, { DecreeArea::Wealth, int(WealthStance::Gain) },
		{ DecreeArea::Hospitality, int(HospitalityStance::Closed) }, { DecreeArea::Care, int(CareStance::Strength) } };
	return s;
}
// 4 gentle: the Kindness rite and Shelter
const DecreeSet& gentleSet() {
	static const DecreeSet s{ { DecreeArea::Worship, int(Rite::Kindness) }, { DecreeArea::Care, int(CareStance::Shelter) } };
	return s;
}

// The channel a word of this clarity takes (3.9): Exact through the messenger when there is one, else from the sky.
WordOpts wordFor(const World& w, int culture, Clarity clarity) {
	WordOpts o;
	o.clarity = clarity;
	const int msg = characters::messenger(w, culture);
	switch (clarity) {
	case Clarity::Exact: o.channel = msg >= 0 ? Channel::Messenger : Channel::Sky; o.figure = msg; break;
	case Clarity::Parable: o.channel = Channel::Messenger; o.figure = msg; break;
	case Clarity::Dream: o.channel = Channel::Dream; break;
	default: o.channel = Channel::Sky; break;
	}
	return o;
}

double bagNum(const NumBag& b, const char* k, double def) {
	const auto it = b.find(k);
	return it == b.end() || !std::isfinite(it->second) ? def : it->second;
}

struct ShapePreset { const char* name; const char* code; };   // L / M / H for curiosity, compassion, ambition, courage, aggression
constexpr ShapePreset kShapes[] = {
	{ "middling", "MMMMM" }, { "curious-brave", "HLLHM" }, { "kind-meek", "MHMLL" }, { "high-compassion", "MHLMM" },
	{ "low-compassion", "MLMMM" }, { "high-ambition", "MLHMM" }, { "low-ambition", "MMLMM" }, { "high-courage", "MLMHM" },
	{ "low-courage", "MMMLM" }, { "curious", "HMMML" }, { "fierce", "LMMMH" },
};

} // namespace

bool Policy::cardsOn() const {
	return isMythos(kind_) || play_.cards != CardMode::Lapse || play_.shape >= 0 || play_.presence >= 0;
}

bool Policy::chooseMessenger() const { return isMythos(kind_); }

const char* toStr(CardMode m) {
	switch (m) {
	case CardMode::Lapse: return "lapse";
	case CardMode::First: return "first";
	case CardMode::Random: return "random";
	default: return "policy";
	}
}

bool cardModeFromStr(std::string_view s, CardMode& out) {
	for (int i = 0; i < 4; i++) {
		if (s == toStr(CardMode(i))) { out = CardMode(i); return true; }
	}
	return false;
}

int shapeFromStr(std::string_view s) {
	if (s == "chance") return 243;
	std::string_view code = s;
	for (const ShapePreset& p : kShapes) if (s == p.name) code = p.code;
	if (code.size() != 5) return -1;
	int v = 0, mul = 1, high = 0, low = 0;
	for (char ch : code) {
		int lv = -1;
		if (ch == 'L' || ch == 'l') { lv = int(TraitLevel::Low); low++; }
		else if (ch == 'M' || ch == 'm') lv = int(TraitLevel::Middling);
		else if (ch == 'H' || ch == 'h') { lv = int(TraitLevel::High); high++; }
		if (lv < 0) return -1;
		v += lv * mul;
		mul *= 3;
	}
	if (high > 2 || low < high) return -1;     // the budget rule of 3.5: at most two High, each with a Low
	return v;
}

const char* shapePresets() {
	return "chance, middling, curious-brave, kind-meek, high-compassion, low-compassion, high-ambition, low-ambition, high-courage, "
		"low-courage, curious, fierce, or a five-letter L/M/H code (curiosity, compassion, ambition, courage, aggression)";
}

void Policy::playCards(World& w) {
	if (mythPhase(w) < 1) return;
	for (int id : cards::openCards(w)) {
		const Card* c = cards::find(w, id);
		if (!c || c->state != uint8_t(CardState::Open)) continue;
		if (const auto it = cardTried_.find(id); it != cardTried_.end() && w.year - it->second < 5) continue;   // refused: again in 5 years
		const Card card = *c;                 // a copy: answering changes the list
		int choice = ruleFor(w, card);
		if (choice == -2) continue;
		if (choice < 0) choice = modeChoice(w, card, play_.cards);
		if (choice < 0) continue;
		if (cards::answer(w, id, choice)) {
			stats_.cardsAnswered++;
			stats_.answers[std::string(toStr(card.kind)) + ":" + std::to_string(choice)]++;
			cardTried_.erase(id);
			if (card.kind == CardKind::CompetingPrayers) castArmed(w, id);
		} else {
			stats_.cardsRefused++;
			cardTried_[id] = w.year;
		}
	}
}

int Policy::ruleFor(World& w, const Card& c) {
	if (c.kind == CardKind::ShapePeople && play_.shape >= 0) return play_.shape;
	if (c.kind == CardKind::Presence && play_.presence >= 0) return play_.presence;
	if (c.kind == CardKind::MessengerChoice && isMythos(kind_) && kind_ != PolicyKind::RandomMyth) {
		const int k = messengerPick(w, c);
		return k >= 0 ? k : -2;
	}
	// a Judgement by `by`: the first sentence of `pref` that can be paid; -2 (wait) when only Awe is short
	auto judge = [&](JudgeBy by, std::initializer_list<Sentence> pref) {
		const std::vector<CardOption> opts = cards::options(w, c.id);
		bool shortOfAwe = false;
		for (Sentence s : pref) {
			const int ch = int(by) * 4 + int(s);
			for (const CardOption& o : opts) {
				if (o.choice != ch) continue;
				if (o.ok) return ch;
				if (o.why == OptionWhy::Awe) shortOfAwe = true;
			}
		}
		return shortOfAwe ? -2 : -1;
	};
	const bool judgement = c.kind == CardKind::Judgement;
	switch (kind_) {
	case PolicyKind::ResponderMyth: return modeChoice(w, c, CardMode::First);
	case PolicyKind::RandomMyth: return modeChoice(w, c, CardMode::Random);
	case PolicyKind::UnseenKind: if (c.kind == CardKind::Presence) return int(Presence::Unseen); break;
	case PolicyKind::MsgKind: if (c.kind == CardKind::Presence) return int(Presence::Messengers); break;
	case PolicyKind::Lawgiver: if (judgement) return judge(JudgeBy::Deed, { Sentence::Disaster, Sentence::Warn }); break;
	case PolicyKind::JudgeDeeds: if (judgement) return judge(JudgeBy::Deed, { Sentence::Warn }); break;
	case PolicyKind::JudgeHearts: if (judgement) return judge(JudgeBy::Heart, { Sentence::Warn }); break;
	case PolicyKind::Merciful: case PolicyKind::Gentle: if (judgement) return judge(JudgeBy::Heart, { Sentence::Forgive }); break;
	case PolicyKind::Stern: if (judgement) return judge(JudgeBy::Deed, { Sentence::Disaster, Sentence::Remove, Sentence::Warn }); break;
	case PolicyKind::Kingmaker:
		if (c.kind == CardKind::CompetingPrayers) {
			// stand with the side whose people remember the god best (society::memory's verdict)
			const Settlement* a = state::settlement(w, c.ref[0]);
			const Settlement* b = state::settlement(w, c.ref[1]);
			const int ca = a && !a->dead ? a->culture : -1, cb = b && !b->dead ? b->culture : -1;
			const double va = ca >= 0 ? society::memory(w, ca).verdict : -1e9, vb = cb >= 0 ? society::memory(w, cb).verdict : -1e9;
			const int side = (cb >= 0 && vb > va) ? 1 : 0;
			for (const CardOption& o : cards::options(w, c.id)) {
				if (o.choice != side) continue;
				if (!o.ok) return -1;
				const int who = side == 0 ? ca : cb;
				if (who >= 0) stats_.favoured[who]++;
				return side;
			}
			return -1;
		}
		break;
	default: break;
	}
	return -1;
}

int Policy::modeChoice(World& w, const Card& c, CardMode m) {
	if (m == CardMode::Lapse || m == CardMode::Policy) return -1;
	if (c.kind == CardKind::ShapePeople && m == CardMode::First) return 243;    // "leave it to chance" is the card's first button
	std::vector<int> ok;
	for (const CardOption& o : cards::options(w, c.id)) if (o.ok) ok.push_back(o.choice);
	if (ok.empty()) return -1;                // nothing payable now: let it be (tried again later)
	if (m == CardMode::First) return ok.front();
	return ok[size_t(cardRng_.pickIndex(int(ok.size())))];
}

void Policy::castArmed(World& w, int cardId) {
	const Card* c = cards::find(w, cardId);
	if (!c) return;
	const int arm = int(bagNum(c->data, "arm", -1));
	if (arm < 0 || arm >= powers::kPowerCount) return;
	const powers::PowerDef& d = powers::kPowers[arm];
	const int cell = int(bagNum(c->data, "armCell", -1));
	const double angMul = bagNum(c->data, "armAng", 1);
	if (!w.valid(cell)) return;
	bool did = false;
	if (d.brush) {
		strokeId_.clear();                    // the answer comes first
		did = startStroke(w, d.id, cell, 8, angMul == 1 ? -1 : d.ang * angMul);
	} else {
		did = click(w, d.id, cell);
	}
	if (did) stats_.armed++;
}

bool Policy::armMessenger(World& w, int cell) {
	if (!powers::can(w, "prophet", cell).ok) return false;
	const int sid = civ::at(w, cell);
	powers::UseOpts o;
	o.choose = true;
	if (!powers::use(w, "prophet", cell, o)) { stats_.refused++; return false; }
	// answer the MessengerChoice card on that town now (the chooser's second step)
	for (int id : cards::openCards(w)) {
		const Card* c = cards::find(w, id);
		if (!c || c->kind != CardKind::MessengerChoice || c->settlement != sid) continue;
		const Card card = *c;
		const int k = messengerPick(w, card);
		if (k >= 0 && cards::answer(w, id, k)) {
			stats_.cardsAnswered++;
			stats_.answers[std::string(toStr(card.kind)) + ":" + std::to_string(k)]++;
			stats_.messengers++;
			stats_.clicks++;
			stats_.byPower["prophet"]++;
			return true;
		}
		break;
	}
	return false;
}

int Policy::messengerPick(World& w, const Card& c) {
	const std::vector<Figure> cand = characters::candidates(w, c.settlement);
	const std::vector<CardOption> opts = cards::options(w, c.id);
	auto okChoice = [&](int i) {
		for (const CardOption& o : opts) if (o.choice == i) return o.ok;
		return false;
	};
	// the archetype this god wants first: Shepherds for Parables, Queens for Exact words, else the most credible
	MessengerArchetype want = MessengerArchetype::None;
	if (kind_ == PolicyKind::ParableOnly || kind_ == PolicyKind::Gentle) want = MessengerArchetype::Shepherd;
	else if (kind_ == PolicyKind::Lawgiver || kind_ == PolicyKind::Valour || kind_ == PolicyKind::ExactOnly) want = MessengerArchetype::Queen;
	int best = -1;
	double bestScore = -1e9;
	for (int i = 0; i < int(cand.size()) && i < 3; i++) {
		if (!okChoice(i)) continue;
		const double score = (MessengerArchetype(cand[size_t(i)].archetype) == want ? 10.0 : 0.0) + cand[size_t(i)].credibility;
		if (score > bestScore + 1e-12) { bestScore = score; best = i; }
	}
	return best;
}

bool Policy::speakTo(World& w, int culture, const WordOpts& o) {
	const int sid = townOf(w, culture);
	if (sid < 0) return false;
	const int cell = w.settlements[size_t(sid)].cell;
	if (!doctrine::canSpeak(w, culture, o).ok) return false;
	if (!powers::can(w, "speak", cell).ok) return false;
	powers::UseOpts u;
	u.word = o;
	if (o.clarity == Clarity::Dream && u.word.ref < 0) u.word.ref = sid;
	if (powers::use(w, "speak", cell, u)) {
		stats_.words++;
		stats_.wordsBy[std::string(toStr(o.clarity)) + ":" + toStr(o.content)]++;
		stats_.byPower["speak"]++;
		return true;
	}
	stats_.wordsRefused++;
	return false;
}

// Give each living people the decrees of `set` it does not hold yet (one word a people, at most maxWords this year;
// a decree heard differently is given again after 150 years).
void Policy::giveDecrees(World& w, const std::vector<std::pair<DecreeArea, int>>& set, Clarity clarity, int maxWords) {
	if (mythPhase(w) < 1) return;
	const bool reserve = kind_ == PolicyKind::Merciful || kind_ == PolicyKind::Gentle || kind_ == PolicyKind::MsgKind || kind_ == PolicyKind::Stern;
	int words = 0;
	for (const Culture& cu : w.cultures) {
		if (words >= maxWords) break;
		if (cu.dead || townOf(w, cu.id) < 0) continue;
		for (const auto& [area, stance] : set) {
			if (area == DecreeArea::Land && mythPhase(w) < 2) continue;
			const Decree* held = doctrine::decree(w, cu.id, area);
			if (held && !held->custom && held->stance == stance) continue;
			const auto key = std::make_pair(cu.id, int(area));
			if (const auto it = decreeTried_.find(key); it != decreeTried_.end() && w.year - it->second < 150) continue;
			WordOpts o = wordFor(w, cu.id, clarity);
			o.content = WordContent::Decree;
			o.area = area;
			o.stance = stance;
			if (reserve && w.god.awe < doctrine::wordCost(w, cu.id, o) + 60) return;   // the kind player's prayers come first
			if (speakTo(w, cu.id, o)) { words++; decreeTried_[key] = w.year; }
			break;                            // one word a people (the 10-year gate)
		}
	}
}

// exact-only / parable-only / sign-only: the people's own leanings as decrees, then answers, as often as allowed.
void Policy::wordsOnly(World& w, Clarity clarity) {
	if (mythPhase(w) < 1) return;
	int words = 0;
	for (const Culture& cu : w.cultures) {
		if (words >= 2) break;
		if (cu.dead || townOf(w, cu.id) < 0) continue;
		if (clarity == Clarity::Parable && characters::messenger(w, cu.id) < 0) continue;   // no Parable without a messenger
		WordOpts o = wordFor(w, cu.id, clarity);
		o.content = WordContent::Answer;
		for (int a = 0; a < int(DecreeArea::Count); a++) {
			const DecreeArea area = DecreeArea(a);
			if (area == DecreeArea::Land && mythPhase(w) < 2) continue;
			if (doctrine::decree(w, cu.id, area)) continue;
			const auto key = std::make_pair(cu.id, a);
			if (const auto it = decreeTried_.find(key); it != decreeTried_.end() && w.year - it->second < 150) continue;
			int stance = doctrine::customStance(w, cu.id, area);
			if (stance < 0) stance = area == DecreeArea::Worship ? int(Rite::Prayer) : 1;   // no leaning: the middle stance
			o.content = WordContent::Decree;
			o.area = area;
			o.stance = stance;
			break;
		}
		if (speakTo(w, cu.id, o)) {
			words++;
			if (o.content == WordContent::Decree) decreeTried_[std::make_pair(cu.id, int(o.area))] = w.year;
		}
	}
}

// A chosen messenger in the culture's capital (from the Iron age, when the Messenger power unlocks).
void Policy::keepMessenger(World& w, int culture) {
	if (mythPhase(w) < 1) return;
	const Culture* cu = state::culture(w, culture);
	if (!cu || cu->dead || cu->prophet || characters::messenger(w, culture) >= 0) return;
	const Settlement* cap = state::settlement(w, cu->capital);
	if (!cap || cap->dead) return;
	if (w.god.awe < powers::cost(w, "prophet") + 10) return;
	armMessenger(w, cap->cell);
}

void Policy::randomWord(World& w, int cell) {
	const int sid = civ::at(w, cell);
	const Settlement* s = state::settlement(w, sid);
	if (!s || s->dead) return;
	const Clarity clarity = Clarity(cardRng_.integer(0, 3));
	WordOpts o = wordFor(w, s->culture, clarity);
	if (cardRng_.chance(0.6)) {
		o.content = WordContent::Decree;
		o.area = DecreeArea(cardRng_.integer(0, int(DecreeArea::Land) - 1));
		o.stance = cardRng_.integer(0, stanceCount(o.area) - 1);
	} else {
		o.content = WordContent::Answer;
	}
	speakTo(w, s->culture, o);
}

void Policy::decideMythos(World& w) {
	if (mythPhase(w) < 1) return;
	const int lead = leadingCulture(w);
	switch (kind_) {
	case PolicyKind::Lawgiver:
		if (lead >= 0) keepMessenger(w, lead);
		giveDecrees(w, lawSet(), Clarity::Exact, 2);
		break;
	case PolicyKind::Valour:
		if (lead >= 0) keepMessenger(w, lead);
		giveDecrees(w, valourSet(), Clarity::Exact, 2);
		break;
	case PolicyKind::ExactOnly: wordsOnly(w, Clarity::Exact); break;
	case PolicyKind::ParableOnly: {
		// one new messenger a year, the leading people first
		std::vector<int> order;
		if (lead >= 0) order.push_back(lead);
		for (const Culture& cu : w.cultures) if (!cu.dead && cu.id != lead) order.push_back(cu.id);
		for (int c : order) {
			if (characters::messenger(w, c) >= 0) continue;
			keepMessenger(w, c);
			if (characters::messenger(w, c) >= 0) break;
		}
		wordsOnly(w, Clarity::Parable);
		break;
	}
	case PolicyKind::SignOnly: wordsOnly(w, Clarity::Sign); break;
	case PolicyKind::JudgeDeeds: case PolicyKind::JudgeHearts:
		giveDecrees(w, lawSet(), Clarity::Dream, 2);
		break;
	case PolicyKind::Merciful: case PolicyKind::Stern: giveDecrees(w, lawSet(), Clarity::Dream, 1); break;   // (V1) the same words
	case PolicyKind::MsgKind: if (lead >= 0) keepMessenger(w, lead); break;
	case PolicyKind::Gentle: {
		const bool msg = lead >= 0 && characters::messenger(w, lead) >= 0;
		giveDecrees(w, gentleSet(), msg ? Clarity::Parable : Clarity::Dream, 1);
		break;
	}
	default: break;
	}
}

} // namespace dm::simrun
