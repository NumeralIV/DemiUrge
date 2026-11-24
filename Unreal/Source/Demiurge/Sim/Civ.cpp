// DEMIURGE — Sim/Civ.cpp : the lifecycle half of dm::civ (SPEC-SIM 3.7 "civ.js"). Owner: civ engineer
// (A7, key "civ"). Economy.cpp (A8) defines civ::yearly and civ::capacity in the same namespace; every
// private helper here lives in an anonymous namespace so the two files link together.
//
//   found  claim  rebuild  destroy  damage  capture  setCulture  electCapital  infect  cure
//   at  near  live  canSettle  siteScore  bestSite  ageProgress
//
// Rules chosen where the spec leaves room (binding for this file; other modules may rely on them):
//   - Territory (claim). "Within s.radius hops" is read as a ROUND disc: the cells within angle
//     (radius + roundPad) x geo.step of the centre (roundPad 0.25; flat land at L 6: radius 2..6 give
//     19 / 37 / 63 / 97 / 137 cells where the hop disc has 19 / 37 / 61 / 91 / 127), reached from the centre over
//     land without leaving the disc (lakes are land: a territory may hold lake cells, which Economy never
//     farms). Territories are round on the planet, not hop-lattice hexagons, and never jump a strait. A cell a
//     live rival already owns is left alone. An unowned cell that also lies inside a live rival's
//     radius goes to the NEARER CENTRE by great-circle distance (ties: lower id), so borders between
//     neighbours are straight bisectors, not hop-lattice staircases. If the rival wins such a cell and
//     it is unowned, claim hands it to the rival at once (owner[] and the rival's cells), so a border
//     cell never flickers to "nobody" between the two settlements' re-claims.
//   - s.cells is always in ASCENDING cell id (after claim, hand-offs and rebuild), and owner[c] == s.id
//     exactly for those cells.
//   - Capitals are sticky: found gives a culture its first capital; destroy / capture / setCulture
//     re-elect only for a culture whose capital was lost (or that had none). Only electCapital itself
//     moves a living capital to a more populous settlement (it restarts the launch site and the Ark).
//     A culture's capital is a live settlement of that culture or -1, and exactly that settlement has
//     capital == true. A settlement that stops being a capital or changes culture loses b[LAUNCH] and ark.
//   - A destroyed settlement keeps the fields it had when it fell (pop = the people lost with it, which
//     the chronicle may quote); only dead, deadYear, cause, capital, b[LAUNCH], ark and its territory
//     change. Its urban[] cells stay as ruins (Life fades them). Cause::None is stored as Abandoned.
//   - infect emits settlement:plague { start: true } only at the onset (plague was 0); re-infecting a
//     sick settlement raises the severity silently. Severity is clamped to 0..1; <= 0 / NaN does nothing.
//   - canSettle: 'crowded' = a cell owned by a live settlement within crowdHops hops by any path (sea
//     included: a strait does not make room). An invalid cell answers { false, Sea }.
//   - siteScore = hab + 0.6 room + 0.12 fresh + 0.08 coast - 0.2 low (CivTune), room = mean hab of the
//     free (unowned) land within 4 hops; sea / lake cells score 0, barren cells their bare hab.
//   - bestSite = exact argmax of siteScore (ties: lower cell id) over the settleable cells of the
//     landmass with the most habitable cells (w.cache.landId / landHab when present and sized for this
//     world, else our own flood fill; ties: the larger landmass, then the lower label). If that landmass
//     has no free site, the best free site on any landmass; -1 when nothing is settleable.
//   - Names: found names the place with names::place(culture.lang) on w.rng.fork("settle:<id>"), so
//     naming never disturbs the sim stream; re-rolls a few times on a duplicate, then falls back to a
//     plain syllable name. Nothing in this file draws from w.rng itself.
//   - K at found = civ::capacity (Economy); a non-finite or negative answer is stored as 0.
#include "Civ.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "Hydro.h"
#include "Mythos.h"
#include "Names.h"
#include "Units.h"

// (v4 mythos, track M6 economy-world-v4):
//   - damage: the share killed is multiplied by the culture's Factors::harmTaken and the town's
//     TownFactors::harmTaken (non-finite or negative = 1; the share stays <= 1; neutral = v3.1 exactly). This is THE place where
//     war and disaster harm meets harmTaken: callers (Disasters, Diplomacy, Powers, Renewal) pass the raw
//     share and must not scale it again. (Famine's share is scaled in Economy.)
//   - exodus(sid, share): a settler party of share x pop (clamped so that at least minPeople stay; at least
//     one person) leaves for the best settleable site (canSettle ok, siteScore, ties lower cell) within
//     exodusHops = 12 hops; if none, or Units finds no route, it joins the nearest other live settlement of
//     its culture; Land, Coastal from Bronze on a coast, Open from Sail with a port (as the settlers of
//     Economy's expansion). People leave only once units::spawn succeeded; returns the unit id or -1.

namespace dm::civ {
namespace {

constexpr int kMaxHops = 32;              // hard cap on any hop radius used here
constexpr size_t kMaxIds = 32767;         // owner[] is int16: settlement ids must stay below this

// One per world (w.scratch); sized by w.n on first use.
struct CivScratch {
	int n = -1;
	int32_t stamp = 0;
	std::vector<int32_t> mark;            // visited stamps of the current disc()
	std::vector<int32_t> q;               // disc() result (BFS order)
	std::vector<uint8_t> qd;              // hop distance of q[i]
	std::vector<int32_t> oMark;           // claim: stamp "some rival's radius covers this cell"
	std::vector<double> oDot;             // claim: nearness (cosine) of the nearest such rival's centre
	std::vector<int32_t> oId;             // claim: that rival's id
	std::vector<int32_t> tmp;             // claim: new territory
	std::vector<std::pair<int32_t, int32_t>> handoff;   // claim: (rival id, cell) given to a rival
	std::vector<int32_t> bq;              // flood fills (bestSite)
	std::vector<uint8_t> d8;              // bestSite: hops to live-owned land, capped
	std::vector<int32_t> lid;             // bestSite: own landmass labels (fallback)
	std::vector<std::pair<double, int32_t>> cand;       // bestSite: (-part score, cell)
};

CivScratch& scr(const World& w) {
	CivScratch& S = w.scratch<CivScratch>();
	if (S.n != w.n) {
		const size_t n = size_t(std::max(0, w.n));
		S.n = w.n;
		S.stamp = 0;
		S.mark.assign(n, 0);
		S.q.assign(n, 0);
		S.qd.assign(n, 0);
		S.oMark.assign(n, 0);
		S.oDot.assign(n, 0.0);
		S.oId.assign(n, -1);
		S.tmp.assign(n, 0);
		S.bq.assign(n, 0);
		S.d8.assign(n, 0);
		S.lid.assign(n, -1);
	}
	return S;
}

int32_t nextStamp(CivScratch& S) {
	if (S.stamp >= 0x7ffffff0) {
		std::fill(S.mark.begin(), S.mark.end(), 0);
		std::fill(S.oMark.begin(), S.oMark.end(), 0);
		S.stamp = 0;
	}
	return ++S.stamp;
}

bool usable(const World& w) {
	return w.geo && w.n > 0 && int(w.owner.size()) == w.n && int(w.elev.size()) == w.n;
}

int hopsOf(double x, int def) {
	if (!(x >= 0)) return def;
	const double f = std::floor(x);
	return f > kMaxHops ? kMaxHops : int(f);
}

// Live settlement by id, else nullptr.
const Settlement* liveSett(const World& w, int id) {
	if (id < 0 || size_t(id) >= w.settlements.size()) return nullptr;
	const Settlement& s = w.settlements[size_t(id)];
	return s.dead ? nullptr : &s;
}
Settlement* liveSett(World& w, int id) {
	if (id < 0 || size_t(id) >= w.settlements.size()) return nullptr;
	Settlement& s = w.settlements[size_t(id)];
	return s.dead ? nullptr : &s;
}

// (v4 mythos) a harmTaken factor: a finite non-negative number, else neutral 1
double harmOf(double x) { return isFinite(x) && x >= 0 ? x : 1.0; }

bool ownedByLive(const World& w, int c) {
	const int o = w.owner[size_t(c)];
	return o >= 0 && liveSett(w, o) != nullptr;
}

// Cosine of the angle between two cells (larger = nearer). The same arithmetic for every caller, so
// equal distances compare exactly equal.
double dotc(const Geo& g, int a, int b) {
	const float* pa = &g.pos[size_t(3 * a)];
	const float* pb = &g.pos[size_t(3 * b)];
	return double(pa[0]) * double(pb[0]) + double(pa[1]) * double(pb[1]) + double(pa[2]) * double(pb[2]);
}

// The territory shape of a settlement of radius r centred on c: the cells within angle
// (r + roundPad) x geo.step of c that can be reached from c over land without leaving that disc.
// Into S.q[0 .. count-1] (c first, then breadth first). A round disc, not a hop "hexagon".
int territory(const World& w, CivScratch& S, int c, int r, double roundPad) {
	const Geo& g = *w.geo;
	const double R = (double(r) + roundPad) * g.step;
	const double cosR = std::cos(std::clamp(R, 0.0, kPi));
	const int32_t st = nextStamp(S);
	S.mark[size_t(c)] = st;
	S.q[0] = c;
	S.qd[0] = 0;
	int count = 1;
	for (int head = 0; head < count; head++) {
		const int cc = S.q[size_t(head)];
		const int deg = g.nbrN[size_t(cc)];
		for (int k = 0; k < deg; k++) {
			const int m = g.nbr[size_t(6 * cc + k)];
			if (m < 0 || S.mark[size_t(m)] == st) continue;
			S.mark[size_t(m)] = st;
			if (state::isSea(w, m) || dotc(g, m, c) < cosR) continue;
			S.q[size_t(count)] = m;
			S.qd[size_t(count)] = uint8_t(std::min(255, S.qd[size_t(head)] + 1));
			count++;
		}
	}
	return count;
}

// Cells within `hops` neighbour hops of c, breadth first, into S.q[0 .. count-1] (distances S.qd).
// landOnly: never steps onto a sea cell (c itself is always included).
int disc(const World& w, CivScratch& S, int c, int hops, bool landOnly) {
	const Geo& g = *w.geo;
	const int32_t st = nextStamp(S);
	S.mark[size_t(c)] = st;
	S.q[0] = c;
	S.qd[0] = 0;
	int count = 1;
	for (int head = 0; head < count; head++) {
		const int d = S.qd[size_t(head)];
		if (d >= hops) break;            // breadth first: everything after is as far or farther
		const int cc = S.q[size_t(head)];
		const int deg = g.nbrN[size_t(cc)];
		for (int k = 0; k < deg; k++) {
			const int m = g.nbr[size_t(6 * cc + k)];
			if (m < 0 || S.mark[size_t(m)] == st) continue;
			S.mark[size_t(m)] = st;
			if (landOnly && state::isSea(w, m)) continue;
			S.q[size_t(count)] = m;
			S.qd[size_t(count)] = uint8_t(d + 1);
			count++;
		}
	}
	return count;
}

// 0 settleable ground, 1 sea or lake, 2 barren (crowding is checked separately).
int groundCode(const World& w, int c, const CivTune& T) {
	if (state::isSea(w, c) || hydro::isLake(w, c)) return 1;
	return w.hab[size_t(c)] > float(T.barrenHab) ? 0 : 2;   // hab is float: compare in float (hab 0.05f is barren)
}

bool anyLive(const World& w) {
	for (const Settlement& s : w.settlements)
		if (!s.dead) return true;
	return false;
}

bool crowdedAt(const World& w, CivScratch& S, int c, int hops) {
	if (!anyLive(w)) return false;
	const int cnt = disc(w, S, c, hops, false);
	for (int i = 0; i < cnt; i++)
		if (ownedByLive(w, S.q[size_t(i)])) return true;
	return false;
}

// The part of siteScore that needs no walk around the cell.
double sitePart(const World& w, int c, const CivTune& T) {
	const int f = w.fresh[size_t(c)], k = w.coast[size_t(c)];
	const double fresh = f <= 1 ? 1.0 : f == 2 ? 0.6 : f == 3 ? 0.3 : 0.0;
	const double coast = (k >= 1 && k <= 3) ? 1.0 : k == 4 ? 0.5 : 0.0;
	const double low = T.siteLowH > 0 ? sat(1.0 - (double(w.elev[size_t(c)]) - w.seaLevel) / T.siteLowH) : 0.0;
	const double h = w.hab[size_t(c)];
	return (h > 0 ? h : 0.0) + T.siteFresh * fresh + T.siteCoast * coast - T.siteLow * low;
}

// Mean habitability of the free land around c (sea, lakes and live-owned cells count 0).
double siteRoomOf(const World& w, CivScratch& S, int c, const CivTune& T) {
	const int cnt = disc(w, S, c, hopsOf(T.siteRoomHops, 4), false);
	double sum = 0;
	for (int i = 0; i < cnt; i++) {
		const int m = S.q[size_t(i)];
		if (state::isSea(w, m) || hydro::isLake(w, m) || ownedByLive(w, m)) continue;
		const double h = w.hab[size_t(m)];
		if (h > 0) sum += h;
	}
	return cnt > 0 ? sum / cnt : 0.0;
}

double siteValue(const World& w, CivScratch& S, int c, const CivTune& T) {
	const double v = sitePart(w, c, T) + T.siteRoom * siteRoomOf(w, S, c, T);
	return isFinite(v) ? v : 0.0;
}

bool validCapital(const World& w, const Culture& cu) {
	const Settlement* s = liveSett(w, cu.capital);
	return s && s->culture == cu.id;
}

void dropArk(Settlement& s) {
	s.b[size_t(Building::LAUNCH)] = 0;
	s.ark = 0;
}

// Give up every cell this settlement owns (uses s.cells; scans owner[] when the cache is empty).
void release(World& w, Settlement& s) {
	const int id = s.id;
	if (!s.cells.empty()) {
		for (const int32_t c : s.cells)
			if (c >= 0 && c < w.n && w.owner[size_t(c)] == id) w.owner[size_t(c)] = -1;
	} else {
		for (int c = 0; c < w.n; c++)
			if (w.owner[size_t(c)] == id) w.owner[size_t(c)] = -1;
	}
}

// ------------------------------------------------------------------ names

bool nameTaken(const World& w, const std::string& name) {
	for (const Settlement& s : w.settlements)
		if (s.name == name) return true;
	return false;
}

std::string makeName(const World& w, const Culture& cu, int id) {
	static const char* const kC[] = { "b", "d", "g", "k", "l", "m", "n", "r", "s", "t", "v", "z", "th", "sh" };
	static const char* const kV[] = { "a", "e", "i", "o", "u", "ai", "ou" };
	static const char* const kE[] = { "n", "r", "s", "l", "th" };
	Rng rng = w.rng.fork("settle:" + std::to_string(id));
	std::string name;
	for (int tries = 0; tries < 6; tries++) {
		name = names::place(cu.lang, rng);
		if (!name.empty() && !nameTaken(w, name)) return name;
	}
	for (int tries = 0; tries < 8; tries++) {
		std::string alt;
		const int syl = rng.integer(2, 3);
		for (int i = 0; i < syl; i++) {
			alt += kC[rng.pickIndex(14)];
			alt += kV[rng.pickIndex(7)];
		}
		if (rng.chance(0.4)) alt += kE[rng.pickIndex(5)];
		alt[0] = char(alt[0] - 'a' + 'A');
		if (!nameTaken(w, alt)) return alt;
		name = alt;
	}
	return name.empty() ? std::string("Nameless") : name;
}

void changeCulture(World& w, int sid, int cultureId, CaptureHow how) {
	Settlement* s = liveSett(w, sid);
	if (!s) return;
	const Culture* to = state::culture(w, cultureId);
	if (!to || to->dead || to->id == s->culture) return;
	const int from = s->culture;
	s->culture = cultureId;
	s->capital = false;
	dropArk(*s);
	Culture* fromCu = state::culture(w, from);
	if (fromCu && fromCu->capital == sid) {
		fromCu->capital = -1;
		electCapital(w, from);
	}
	const Culture* toCu = state::culture(w, cultureId);
	if (toCu && !validCapital(w, *toCu)) electCapital(w, cultureId);
	w.dirty.objects = true;
	w.dirty.color = true;
	w.bus.emit(EvSettlementCaptured{ sid, from, cultureId, how });
}

// The landmass with the most habitable cells: label of `lab` (filled for every cell, -1 on sea) and
// the winner, -1 when nothing is habitable. Uses w.cache when it is built for this world.
int bestLandmass(const World& w, CivScratch& S, const CivTune& T, const std::vector<int32_t>*& lab) {
	const size_t n = size_t(w.n);
	const WorldCache& wc = w.cache;
	if (wc.landId.size() == n && wc.landCount > 0 && wc.landHab.size() == size_t(wc.landCount)) {
		int best = -1, bestHab = 0;
		for (int k = 0; k < wc.landCount; k++)
			if (wc.landHab[size_t(k)] > bestHab) { bestHab = wc.landHab[size_t(k)]; best = k; }   // ids by size desc: ties stay larger
		if (best >= 0) {
			lab = &wc.landId;
			return best;
		}
	}
	// own flood fill over land
	const Geo& g = *w.geo;
	for (int c = 0; c < w.n; c++) S.lid[size_t(c)] = state::isSea(w, c) ? -1 : -2;
	int k = 0, best = -1, bestHab = 0, bestSize = 0;
	for (int c0 = 0; c0 < w.n; c0++) {
		if (S.lid[size_t(c0)] != -2) continue;
		int head = 0, end = 0, cnt = 0;
		S.bq[size_t(end++)] = c0;
		S.lid[size_t(c0)] = k;
		while (head < end) {
			const int c = S.bq[size_t(head++)];
			if (w.hab[size_t(c)] > float(T.barrenHab) && !hydro::isLake(w, c)) cnt++;
			const int deg = g.nbrN[size_t(c)];
			for (int j = 0; j < deg; j++) {
				const int m = g.nbr[size_t(6 * c + j)];
				if (m >= 0 && S.lid[size_t(m)] == -2) { S.lid[size_t(m)] = k; S.bq[size_t(end++)] = m; }
			}
		}
		if (cnt > bestHab || (cnt == bestHab && cnt > 0 && end > bestSize)) { bestHab = cnt; bestSize = end; best = k; }
		k++;
	}
	lab = &S.lid;
	return best;
}

// Exact argmax of siteScore over the settleable cells with lab[c] == target (target < 0: any land).
int bestOn(const World& w, CivScratch& S, const CivTune& T, const std::vector<int32_t>& lab, int target, bool useCrowd) {
	const int crowd = hopsOf(T.crowdHops, 7);
	S.cand.clear();
	double hmax = 0;
	for (int c = 0; c < w.n; c++) {
		const double h = w.hab[size_t(c)];
		if (h > hmax) hmax = h;
		if (target >= 0 && lab[size_t(c)] != target) continue;
		if (groundCode(w, c, T) != 0) continue;
		if (useCrowd && S.d8[size_t(c)] <= crowd) continue;
		const double p = sitePart(w, c, T);
		if (!isFinite(p)) continue;
		S.cand.emplace_back(-p, c);
	}
	if (S.cand.empty()) return -1;
	std::sort(S.cand.begin(), S.cand.end());          // best part first, ties by cell id
	const double slack = (T.siteRoom > 0 ? T.siteRoom * hmax : 0.0) + 1e-9;   // the most the room term can add
	int best = -1;
	double bestV = -std::numeric_limits<double>::infinity();
	for (const auto& pc : S.cand) {
		const double p = -pc.first;
		if (p + slack < bestV) break;                  // nothing later can win
		const int c = pc.second;
		const double v = siteValue(w, S, c, T);
		if (v > bestV || (v == bestV && c < best)) { bestV = v; best = c; }
	}
	return best;
}

} // namespace

// ==================================================================== queries

std::vector<int> live(const World& w) {
	std::vector<int> ids;
	for (const Settlement& s : w.settlements)
		if (!s.dead) ids.push_back(s.id);
	return ids;
}

int at(const World& w, int cell) {
	if (!usable(w) || !w.valid(cell)) return -1;
	const int o = w.owner[size_t(cell)];
	const Settlement* own = liveSett(w, o);
	if (own && own->cell == cell) return o;
	for (const Settlement& s : w.settlements)
		if (!s.dead && s.cell == cell) return s.id;
	return own ? o : -1;
}

std::vector<int> near(const World& w, int cell, double ang) {
	std::vector<std::pair<double, int>> hits;
	if (usable(w) && w.valid(cell) && ang >= 0 && isFinite(ang)) {
		for (const Settlement& s : w.settlements) {
			if (s.dead || !w.valid(s.cell)) continue;
			const double a = w.geo->angle(cell, s.cell);
			if (a <= ang) hits.emplace_back(a, s.id);
		}
	}
	std::sort(hits.begin(), hits.end());   // nearest first, ties by id
	std::vector<int> ids;
	ids.reserve(hits.size());
	for (const auto& h : hits) ids.push_back(h.second);
	return ids;
}

SettleCheck canSettle(const World& w, int c) {
	if (!usable(w) || !w.valid(c)) return SettleCheck{ false, SettleReason::Sea };
	const CivTune& T = w.tune<CivTune>();
	const int g = groundCode(w, c, T);
	if (g == 1) return SettleCheck{ false, SettleReason::Sea };
	if (g == 2) return SettleCheck{ false, SettleReason::Barren };
	if (crowdedAt(w, scr(w), c, hopsOf(T.crowdHops, 7))) return SettleCheck{ false, SettleReason::Crowded };
	return SettleCheck{ true, SettleReason::Ok };
}

double siteScore(const World& w, int c) {
	if (!usable(w) || !w.valid(c)) return 0.0;
	const CivTune& T = w.tune<CivTune>();
	const int g = groundCode(w, c, T);
	if (g == 1) return 0.0;
	if (g == 2) {
		const double h = w.hab[size_t(c)];
		return h > 0 ? h : 0.0;                    // unsettleable ground earns no bonuses
	}
	return siteValue(w, scr(w), c, T);
}

int bestSite(const World& w) {
	if (!usable(w)) return -1;
	const CivTune& T = w.tune<CivTune>();
	CivScratch& S = scr(w);
	// hops from live-owned land, capped one beyond the crowding distance
	bool useCrowd = false;
	if (anyLive(w)) {
		const Geo& g = *w.geo;
		const int cap = std::min(250, hopsOf(T.crowdHops, 7) + 1);
		int tail = 0;
		for (int c = 0; c < w.n; c++) {
			if (ownedByLive(w, c)) { S.d8[size_t(c)] = 0; S.bq[size_t(tail++)] = c; }
			else S.d8[size_t(c)] = uint8_t(cap);
		}
		useCrowd = tail > 0;
		for (int head = 0; head < tail; head++) {
			const int c = S.bq[size_t(head)];
			const int d = S.d8[size_t(c)] + 1;
			if (d >= cap) continue;
			const int deg = g.nbrN[size_t(c)];
			for (int j = 0; j < deg; j++) {
				const int m = g.nbr[size_t(6 * c + j)];
				if (m >= 0 && S.d8[size_t(m)] > d) { S.d8[size_t(m)] = uint8_t(d); S.bq[size_t(tail++)] = m; }
			}
		}
	}
	const std::vector<int32_t>* lab = nullptr;
	const int target = bestLandmass(w, S, T, lab);
	if (target < 0 || !lab) return -1;
	const int best = bestOn(w, S, T, *lab, target, useCrowd);
	return best >= 0 ? best : bestOn(w, S, T, *lab, -1, useCrowd);   // that landmass is full: anywhere
}

int bandSite(const World& w, int cell) {
	if (!usable(w) || !w.valid(cell) || state::isSea(w, cell)) return -1;
	const CivTune& T = w.tune<CivTune>();
	const Geo& g = *w.geo;
	const int lo = std::max(1, T.bandMinHops), hi = std::max(lo, T.bandMaxHops);
	// BFS over land (no sea crossing: the two bands walk apart)
	std::vector<int32_t> q{ int32_t(cell) };
	std::vector<uint8_t> d(size_t(w.n), uint8_t(255));
	d[size_t(cell)] = 0;
	int best = -1;
	double bestScore = -1e300;
	for (size_t head = 0; head < q.size(); head++) {
		const int c = q[head];
		const int dc = d[size_t(c)];
		if (dc >= lo && groundCode(w, c, T) == 0 && !ownedByLive(w, c)) {
			const double sc = siteScore(w, c);
			if (isFinite(sc) && (sc > bestScore || (sc == bestScore && c < best))) { bestScore = sc; best = c; }
		}
		if (dc >= hi) continue;
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
			const int m = g.nbr[size_t(6 * c + k)];
			if (m < 0 || d[size_t(m)] != 255 || state::isSea(w, m)) continue;
			d[size_t(m)] = uint8_t(dc + 1);
			q.push_back(m);
		}
	}
	return best;
}

bool consecrate(World& w, int sid) {
	Settlement* s = liveSett(w, sid);
	if (!s || !s->has(Building::SHRINE) || s->has(Building::TEMPLE)) return false;
	s->b[Building::TEMPLE] = 1;
	w.dirty.objects = true;
	w.bus.emit(EvSettlementBuilding{ sid, Building::TEMPLE });
	return true;
}

double ageProgress(const World& w, int cultureId) {
	const Culture* cu = state::culture(w, cultureId);
	if (!cu) return 0.0;
	const int a = std::clamp(cu->age, 0, int(Age::COUNT) - 1);
	if (a >= int(Age::SPACE)) return 1.0;
	const auto& need = w.tune<EconomyTune>().ageNeed;
	if (size_t(a) >= need.size()) return 1.0;
	const double nd = need[size_t(a)];
	if (!(nd > 0)) return 1.0;
	const double p = cu->science / nd;
	return p > 0 ? (p < 1 ? p : 1.0) : 0.0;
}

// ==================================================================== territory

void claim(World& w, int sid) {
	if (!usable(w)) return;
	Settlement* sp = liveSett(w, sid);
	if (!sp || !w.valid(sp->cell)) return;
	const Geo& g = *w.geo;
	CivScratch& S = scr(w);
	const int cell = sp->cell;
	const int r = hopsOf(double(sp->radius), 2);
	release(w, *sp);

	// Every other live settlement whose radius could reach ours records, for each cell it covers, how
	// near that cell is to its centre (no neighbour step is longer than ~1.3 x geo.step; 1.6 is safe).
	const int32_t ost = nextStamp(S);
	const double pad = w.tune<CivTune>().roundPad;
	const double reach = g.step * 1.02;
	for (const Settlement& t : w.settlements) {
		if (t.dead || t.id == sid || !w.valid(t.cell)) continue;
		const int rt = hopsOf(double(t.radius), 2);
		if (g.angle(cell, t.cell) > (r + rt + 2 * pad + 0.1) * reach) continue;   // the two discs cannot meet
		const int cnt = territory(w, S, t.cell, rt, pad);
		for (int j = 0; j < cnt; j++) {
			const int c = S.q[size_t(j)];
			const double d = dotc(g, c, t.cell);
			if (S.oMark[size_t(c)] != ost || d > S.oDot[size_t(c)] || (d == S.oDot[size_t(c)] && t.id < S.oId[size_t(c)])) {
				S.oMark[size_t(c)] = ost;
				S.oDot[size_t(c)] = d;
				S.oId[size_t(c)] = t.id;
			}
		}
	}

	S.handoff.clear();
	const int cnt = territory(w, S, cell, r, pad);
	int k = 0;
	for (int i = 0; i < cnt; i++) {
		const int c = S.q[size_t(i)];
		if (state::isSea(w, c)) continue;                                   // never the sea
		const int o = w.owner[size_t(c)];
		if (o >= 0 && o != sid && liveSett(w, o)) continue;                  // theirs already
		if (S.oMark[size_t(c)] == ost) {                                    // contested: nearer centre, then lower id
			const double d = dotc(g, c, cell), od = S.oDot[size_t(c)];
			if (od > d || (od == d && S.oId[size_t(c)] < sid)) {
				S.handoff.emplace_back(S.oId[size_t(c)], c);
				continue;
			}
		}
		w.owner[size_t(c)] = int16_t(sid);
		S.tmp[size_t(k++)] = c;
	}
	std::sort(S.tmp.begin(), S.tmp.begin() + k);

	Settlement& s = w.settlements[size_t(sid)];
	const bool same = int(s.cells.size()) == k && std::equal(s.cells.begin(), s.cells.end(), S.tmp.begin());
	if (!same) {
		s.cells.assign(S.tmp.begin(), S.tmp.begin() + k);
		w.dirty.color = true;
	}

	// cells a nearer rival wins go to that rival now (they are unowned)
	if (!S.handoff.empty()) {
		std::sort(S.handoff.begin(), S.handoff.end());
		size_t i = 0;
		while (i < S.handoff.size()) {
			const int rid = S.handoff[i].first;
			Settlement& t = w.settlements[size_t(rid)];
			const size_t before = t.cells.size();
			for (; i < S.handoff.size() && S.handoff[i].first == rid; i++) {
				const int c = S.handoff[i].second;
				if (w.owner[size_t(c)] != -1) continue;
				w.owner[size_t(c)] = int16_t(rid);
				t.cells.push_back(c);
			}
			if (t.cells.size() != before) {
				std::inplace_merge(t.cells.begin(), t.cells.begin() + std::ptrdiff_t(before), t.cells.end());
				w.dirty.color = true;
			}
		}
	}
}

void rebuild(World& w) {
	if (!usable(w)) return;
	const size_t m = w.settlements.size();
	std::vector<int32_t> count(m, 0);
	for (int c = 0; c < w.n; c++) {
		const int o = w.owner[size_t(c)];
		if (o >= 0 && size_t(o) < m) count[size_t(o)]++;
	}
	for (size_t i = 0; i < m; i++) {
		Settlement& s = w.settlements[i];
		s.cells.clear();
		if (!s.dead) s.cells.reserve(size_t(count[i]));
	}
	for (int c = 0; c < w.n; c++) {
		const int o = w.owner[size_t(c)];
		if (o < 0 || size_t(o) >= m) continue;
		Settlement& s = w.settlements[size_t(o)];
		if (!s.dead) s.cells.push_back(c);
	}
}

// ==================================================================== capitals and membership

int electCapital(World& w, int cultureId) {
	Culture* cu = state::culture(w, cultureId);
	if (!cu) return -1;
	int best = -1;
	double bp = 0;
	for (const Settlement& s : w.settlements) {
		if (s.dead || s.culture != cultureId) continue;
		const double p = isFinite(s.pop) ? s.pop : 0.0;
		if (best < 0 || p > bp) { best = s.id; bp = p; }       // ascending id: ties stay with the lower id
	}
	const int old = cu->capital;
	if (old != best) {
		if (old >= 0 && size_t(old) < w.settlements.size()) dropArk(w.settlements[size_t(old)]);
		if (best >= 0) dropArk(w.settlements[size_t(best)]);    // a new capital starts its own launch site and Ark
		w.dirty.objects = true;
	}
	cu->capital = best;
	for (Settlement& s : w.settlements)
		if (s.culture == cultureId) s.capital = (s.id == best);
	return best;
}

void capture(World& w, int sid, int cultureId) {
	changeCulture(w, sid, cultureId, CaptureHow::War);
}

void setCulture(World& w, int sid, int cultureId) {
	changeCulture(w, sid, cultureId, CaptureHow::Schism);
}

// ==================================================================== founding and destruction

Settlement* found(World& w, int cell, int cultureId, double pop, FoundedBy by, const FoundOpts& opts) {
	if (!usable(w) || !w.valid(cell)) return nullptr;
	const Culture* cu = state::culture(w, cultureId);
	if (!cu || cu->dead) return nullptr;
	if (!isFinite(pop) || !(pop > 0)) return nullptr;
	const CivTune& T = w.tune<CivTune>();
	int liveN = 0;
	for (const Settlement& s : w.settlements)
		if (!s.dead) liveN++;
	if (liveN >= T.maxSettlements || w.settlements.size() >= kMaxIds) return nullptr;
	if (opts.band) {
		if (groundCode(w, cell, T) != 0 || ownedByLive(w, cell)) return nullptr;   // a second band: crowding allowed
	} else if (!canSettle(w, cell).ok) {
		return nullptr;
	}

	double devotion = by == FoundedBy::Awaken ? T.awakenDevotion : T.settlerDevotion;
	double love = by == FoundedBy::Awaken ? T.awakenLove : T.settlerLove;
	if (opts.devotion && isFinite(*opts.devotion)) devotion = *opts.devotion;
	if (opts.love && isFinite(*opts.love)) love = *opts.love;
	const int sid = int(w.settlements.size());
	std::string name = makeName(w, *cu, sid);
	const int radius0 = hopsOf(double(w.tune<EconomyTune>().radius[0]), 2);

	{
		Settlement& s = state::newSettlement(w, cell, cultureId);
		s.name = std::move(name);
		s.pop = pop;
		s.tier = 0;
		s.radius = radius0;
		s.devotion = sat(devotion);
		s.love = clamp(love, -1.0, 1.0);
		s.cells.clear();
	}
	claim(w, sid);
	{
		const double K = capacity(w, w.settlements[size_t(sid)]);
		w.settlements[size_t(sid)].K = (isFinite(K) && K > 0) ? K : 0.0;
	}
	Culture* cup = state::culture(w, cultureId);
	if (cup && !validCapital(w, *cup)) {
		cup->capital = sid;
		w.settlements[size_t(sid)].capital = true;
	}
	float& u = w.urban[size_t(cell)];
	if (u < T.campUrban) u = float(T.campUrban);
	w.dirty.objects = true;
	w.dirty.color = true;
	w.bus.emit(EvSettlementFounded{ sid, cell, cultureId, by });
	return &w.settlements[size_t(sid)];
}

void destroy(World& w, int sid, Cause cause) {
	Settlement* sp = liveSett(w, sid);
	if (!sp) return;
	if (cause == Cause::None) cause = Cause::Abandoned;
	Settlement& s = *sp;
	s.dead = true;
	s.deadYear = w.year;
	s.cause = cause;
	if (usable(w)) release(w, s);
	s.cells.clear();
	s.capital = false;
	dropArk(s);
	const int cultureId = s.culture, cell = s.cell;
	Culture* cu = state::culture(w, cultureId);
	if (cu && cu->capital == sid) {
		cu->capital = -1;
		electCapital(w, cultureId);
	}
	w.dirty.objects = true;
	w.dirty.color = true;
	w.bus.emit(EvSettlementDestroyed{ sid, w.valid(cell) ? cell : -1, cultureId, cause });
}

double damage(World& w, int sid, double frac, Cause cause) {
	Settlement* s = liveSett(w, sid);
	if (!s || !(frac > 0)) return 0.0;
	if (frac > 1) frac = 1;
	{
		// (v4 mythos, SPEC-MYTHOS 1.4 / 3.16) what the town takes from war and disaster: the culture's harmTaken
		// x the town's (Endurance, the Remnant, preparation); neutral 1 = v3.1 exactly
		const double h = harmOf(mythos::factors(w, s->culture).harmTaken) * harmOf(mythos::townFactors(w, sid).harmTaken);
		if (h != 1) frac *= h;
		if (!(frac > 0)) return 0.0;
		if (frac > 1) frac = 1;
	}
	const double before = (isFinite(s->pop) && s->pop > 0) ? s->pop : 0.0;
	const double killed = before * frac;
	if (before - killed < w.tune<CivTune>().minPeople) {
		s->pop = before;                          // a ruin keeps the number of people lost with it
		destroy(w, sid, cause);
		return before;
	}
	s->pop = before - killed;
	return killed;
}

// ==================================================================== plague

bool infect(World& w, int sid, double severity) {
	Settlement* s = liveSett(w, sid);
	if (!s || s->immune > 0 || !(severity > 0)) return false;
	if (severity > 1) severity = 1;
	const double was = s->plague > 0 ? s->plague : 0.0;
	s->plague = was > severity ? was : severity;
	if (was == 0) w.bus.emit(EvSettlementPlague{ sid, true });
	return true;
}

bool cure(World& w, int sid, double immuneYears) {
	Settlement* s = liveSett(w, sid);
	if (!s) return false;
	const bool was = s->plague > 0;
	s->plague = 0;
	s->immune = (isFinite(immuneYears) && immuneYears > 0) ? immuneYears : 0.0;
	if (was) w.bus.emit(EvSettlementPlague{ sid, false });
	return true;
}

// ---- (v4 mythos, M0) neutral bodies of the new [civ] APIs (Civ.h); the economy-world-v4 tracks replace them.
int exodus(World& w, int sid, double share) {
	constexpr int kExodusHops = 12;
	if (!usable(w)) return -1;
	const Settlement* src = liveSett(w, sid);
	if (!src || !w.valid(src->cell) || !(share > 0)) return -1;
	if (share > 1) share = 1;
	const Culture* cu = state::culture(w, src->culture);
	if (!cu || cu->dead) return -1;
	const double pop = (isFinite(src->pop) && src->pop > 0) ? src->pop : 0.0;
	const double size = std::min(share * pop, pop - w.tune<CivTune>().minPeople);
	if (!(size >= 1)) return -1;
	const int culture = src->culture, from = src->cell;
	const int age = cu->age < 0 ? 0 : cu->age;
	const bool coastal = int(w.coast[size_t(from)]) <= src->radius + 1;
	NavalMode naval = NavalMode::Land;
	if (age >= Age::SAIL && src->has(Building::PORT)) naval = NavalMode::Open;
	else if (age >= Age::BRONZE && coastal) naval = NavalMode::Coastal;
	// the best free site nearby (canSettle: not sea or lake, not barren, not crowded)
	const Geo& g = *w.geo;
	std::vector<int32_t> ringCells;
	g.ring(from, kExodusHops, ringCells);
	int best = -1;
	double bestScore = -std::numeric_limits<double>::infinity();
	for (const int32_t c : ringCells) {
		if (!canSettle(w, c).ok) continue;
		const double sc = siteScore(w, c);
		if (!isFinite(sc)) continue;
		if (sc > bestScore || (sc == bestScore && c < best)) { bestScore = sc; best = c; }
	}
	units::SpawnOpts o;
	o.type = UnitType::Settler;
	o.culture = culture;
	o.from = sid;
	o.size = size;
	o.naval = naval;
	o.data["civ_exodus"] = 1;                      // (an exodus column, for the chronicle and the client)
	Unit* u = nullptr;
	if (best >= 0) {
		o.to = -1;
		o.dest = best;
		u = units::spawn(w, o);
	}
	if (!u) {
		// no free site or no route: they go to the nearest other town of their people
		int join = -1;
		double joinAng = std::numeric_limits<double>::infinity();
		for (const Settlement& t : w.settlements) {
			if (t.dead || t.id == sid || t.culture != culture || !w.valid(t.cell)) continue;
			const double a = g.angle(from, t.cell);
			if (a < joinAng) { joinAng = a; join = t.id; }
		}
		if (join < 0) return -1;
		o.to = join;
		o.dest = w.settlements[size_t(join)].cell;
		u = units::spawn(w, o);
	}
	if (!u) return -1;
	const int uid = u->id;
	Settlement& s2 = w.settlements[size_t(sid)];   // re-fetch: never hold a reference across spawn
	s2.pop = std::max(0.0, s2.pop - size);
	return uid;
}
bool demoteTemple(World& w, int sid) {
	(void)w; (void)sid;
	return false;
}

} // namespace dm::civ
