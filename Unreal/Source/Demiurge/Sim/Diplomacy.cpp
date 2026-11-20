// DEMIURGE — Sim/Diplomacy.cpp : relations, war, armies and battles, trade and roads, nuclear exchange
// (SPEC-SIM 3.8 second half, DM.Society diplomacy.js). Owner: diplomacy engineer (A10, key "diplomacy");
// the [diplomacy] half of Society.h and DiplomacyTune.
//
// Writers: w.vars.rel (every Relation), culture.exhaustion, w.stats.wars (declareWar), w.road (trade),
// w.rad (nukes), w.vars.lastNuke, w.dirty.roads / color; a source settlement's pop when an army leaves
// (people removed only after units::spawn succeeded); on a prophet's arrival the visited settlement's
// devotion and culture.prophet->settlement (Units.h arrival contract). Module numbers live in
// w.vars.num under "diplomacy_..." keys (persisted):
//   diplomacy_w<lo>_<hi>_n<c>      live settlements of side c when the war began (40 % rule)
//   diplomacy_w<lo>_<hi>_lost<c>   settlements side c lost in this war (captured or razed)
//   diplomacy_w<lo>_<hi>_army<c>   year side c last sent an army in this war
//   diplomacy_trade<sid>           year settlement sid last tried to send a caravan
//   diplomacy_route<lo>_<hi>       year of the first caravan between two settlements (trade:route once)
// Route cache (JS world._routes) lives in DiplomacyScratch (never saved; rebuilt on demand).
//
// Choices where the spec leaves room (tunable in DiplomacyTune; see the A10 report):
// - contact / border come from one multi-source BFS over all cells (sea included) seeded with every
//   owned cell (and every live settlement centre), labelled by culture, depth ceil(contactHops / 2):
//   where two labels meet the gap is d1 + d2 + 1 hops. Contact = gap <= contactHops (or trade above
//   tradeContact). border = meeting edges with gap <= borderGap / (2 x max(borderMinPerim, the smaller
//   culture's perimeter)), saturated: the share of its outer edge (coast included) facing the other;
//   only meeting edges between two land cells count (a strait gives contact, not a border).
// - Relations are created on first contact; pairs out of contact only relax toward relTarget and keep
//   their decaying trade. Noise is one rng.gauss() per contacted pair per year (pairs in id order).
// - Peace edict: relations +0.02/yr for each side under it, and no new war. Crusade edict: aggression
//   +0.4 (SocietyTune) toward the least devout culture in contact.
// - Alliances emerge from two small rules: when X attacks Y, every culture with v >= allyV toward Y sours
//   by allyOutrage on X; two cultures at war with a common enemy gain commonEnemyV per year.
// - The aggressor is the side with the higher (effective) aggression, ties to the lower id; it must have
//   >= 0.6 of the other's pop x tech or nobody declares. It must also be able to carry the war: a shared
//   landmass, or (Bronze+) a strait of <= borderGap hops, or (Sail+) a port. Without that rule two
//   cultures facing each other over deep water fought endless 60-year wars without a single army.
// - Exhaustion +0.015 per year per war; when an army wins, the defender +0.08 (lost battle) and +0.15 (lost
//   settlement); when it loses, the attacker +0.08. Peace: a side at exhaustion 1 (victor = the other,
//   draw if both), a side that lost >= 40 % of its settlements at war start (victor = the other), 60
//   years (victor = the side that took more settlements, else draw), a dead or empty side (victor =
//   the other). Exhaustion decays 0.03/yr while a culture is at no war.
// - Armies: per war side, due every armyEvery years from its largest settlement with an enemy settlement
//   (nearest by angle) within armyMaxCost hops and a route of cost <= armyMaxCost; at most armyAttempts
//   searches per side per year; nothing found: retry next year. A won battle: pop < 100 razes
//   (civ::destroy war), else the defender loses 15 % (civ::damage war) and is captured (civ::capture);
//   the winning army is consumed (Units erases it silently). A lost battle: units::kill(..., "war").
// - Trade: a town+ with a market, every tradeEvery years (staggered by id), weighted pick among the four
//   best partners within tradeHops (village+, not at war; score sqrt(pop) x 1.5 foreign x 1.5 market /
//   (1 + hops / 10)); route from the cache or one search; traders carry traderSize merchants not taken
//   from the town. On arrival: foreign pair trade += tradeStep (0.15) x (1 - trade), v += 0.02; plague
//   hops with P = 0.5 x origin plague; each land cell of the route (not lakes) raises its road one level
//   with chance roadP[level] (forked rng per unit); trade:route on the first caravan of a settlement
//   pair (any cultures). trade decays tradeDecay (8 %)/yr. A caravan that arrives after its culture
//   went to war with the town's does nothing (verifier fix).
// - Route cache: a cached route that units::spawn refuses (the ground changed: lava, sea rise, sea
//   ice) is dropped, so the next attempt searches again; cached routes need no search budget, so
//   armies keep using them after the year's 4 searches are spent (verifier fixes).
// - Naval mode of armies and traders: Open from the Sail age with a port, Coastal from Bronze, else Land.
// - Nukes: per warring pair with both sides Atomic+, P = 1.5 %/yr; each side strikes the other's two
//   largest live settlements (targets chosen before any strike): nuke event, civ::damage 70 % (nuke),
//   rad = max(rad, 0.8) within 2 hops, life::ignite within 1 hop, climate::addDust 0.10,
//   society::witness(kind "nuke", faith 0, good 0, ang 0.3); then vars.lastNuke and peace (truce 100).
// At most searchesPerYear (4) path::find calls per diplomacy() call (armies first, then traders).
#include "Society.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "Civ.h"
#include "Climate.h"
#include "Hydro.h"
#include "Life.h"
#include "Path.h"
#include "Units.h"

namespace dm {
namespace {

struct RouteEntry {
	std::vector<int32_t> cells;          // from the lower settlement id to the higher; empty = no route
	double cost = 0;
	double year = 0;
};

struct DiplomacyScratch {
	std::vector<int32_t> label;          // culture id of the nearest territory (-1 none)
	std::vector<uint8_t> dist;           // hops to it
	std::vector<int32_t> queue;
	std::map<std::tuple<int, int, int>, RouteEntry> routes;   // (lo sid, hi sid, naval) -> route
	int searches = 0;                    // path::find calls in the current diplomacy() call
	int budget = 0;
	std::vector<int32_t> tmp;
};

// Per culture, refreshed at the start of diplomacy()
struct Agg {
	bool alive = false;
	double pop = 0;
	int towns = 0;
	double love = 0, devotion = 0;
	int age = 0;
	double tech = 1;
	int perim = 0, cells = 0;
	int crusadeTarget = -1;
	// (v3.1, design review 11/13) the god's recent favour (impulse a year), the sick share of the people, a prophet
	double fGrace = 0, fWrath = 0, sick = 0;
	bool prophet = false;
};

// (v3.1) persisted keys of alliances and sworn peaces: diplomacy_ally<lo>_<hi> (year formed),
// diplomacy_sworn<lo>_<hi> (year the Omen's sworn peace ends)
std::string pairKey(const char* what, int a, int b) {
	const auto k = state::relKey(a, b);
	return std::string("diplomacy_") + what + std::to_string(k.first) + "_" + std::to_string(k.second);
}
bool isAlly(const World& w, int a, int b) {
	return a != b && w.vars.num.count(pairKey("ally", a, b)) > 0;
}
// The allies of culture c (ascending ids), from the persisted alliance keys.
std::vector<int> alliesOf(const World& w, int c) {
	std::vector<int> out;
	for (int z = 0; z < int(w.cultures.size()); z++)
		if (z != c && isAlly(w, c, z)) out.push_back(z);
	return out;
}
// The god's recent favour of culture c as rates (impulse a year), from Society's fading sums.
double favourRate(const World& w, const char* what, int c) {
	const double hl = w.tune<SocietyTune>().favourHalfLife;
	if (!(hl > 0)) return 0;
	const auto it = w.vars.num.find(std::string("society_") + what + "_" + std::to_string(c));
	if (it == w.vars.num.end() || !isFinite(it->second)) return 0;
	return std::max(0.0, it->second) * (1 - std::pow(0.5, 1.0 / hl));
}

std::string ns(int v) { return std::to_string(v); }
std::string warPrefix(int a, int b) {
	const auto k = state::relKey(a, b);
	return "diplomacy_w" + ns(k.first) + "_" + ns(k.second) + "_";
}
double getNum(const World& w, const std::string& key, double def) {
	const auto it = w.vars.num.find(key);
	return (it == w.vars.num.end() || !isFinite(it->second)) ? def : it->second;
}
void setNum(World& w, const std::string& key, double v) {
	if (isFinite(v)) w.vars.num[key] = v;
}
void erasePrefix(World& w, const std::string& prefix) {
	auto it = w.vars.num.lower_bound(prefix);
	while (it != w.vars.num.end() && it->first.compare(0, prefix.size(), prefix) == 0) it = w.vars.num.erase(it);
}

bool cultureOk(const World& w, int c) {
	return c >= 0 && size_t(c) < w.cultures.size();
}
bool cultureAlive(const World& w, int c) {
	return cultureOk(w, c) && !w.cultures[size_t(c)].dead;
}
const Settlement* liveSettlement(const World& w, int sid) {
	const Settlement* s = state::settlement(w, sid);
	return (s && !s->dead && w.valid(s->cell)) ? s : nullptr;
}
double techOf(const World& w, int cultureId) {
	const DiplomacyTune& T = w.tune<DiplomacyTune>();
	if (!cultureOk(w, cultureId)) return T.tech[0];
	const int a = std::clamp(w.cultures[size_t(cultureId)].age, 0, 6);
	return T.tech[size_t(a)];
}
double finiteOr(double v, double def) { return isFinite(v) ? v : def; }

NavalMode navalOf(const World& w, const Settlement& s) {
	if (!cultureOk(w, s.culture)) return NavalMode::Land;
	const int a = w.cultures[size_t(s.culture)].age;
	if (a >= Age::SAIL && s.has(Building::PORT)) return NavalMode::Open;
	if (a >= Age::BRONZE) return NavalMode::Coastal;
	return NavalMode::Land;
}

std::vector<Agg> aggregates(const World& w) {
	const size_t nc = w.cultures.size();
	std::vector<Agg> ag(nc);
	std::vector<double> wsum(nc, 0.0), cnt(nc, 0.0), loveU(nc, 0.0), devU(nc, 0.0);
	for (size_t k = 0; k < nc; k++) {
		const Culture& cu = w.cultures[k];
		ag[k].alive = !cu.dead;
		ag[k].age = std::clamp(cu.age, 0, 6);
		ag[k].tech = techOf(w, int(k));
		if (!cu.dead) {
			ag[k].fGrace = favourRate(w, "fgrace", int(k));
			ag[k].fWrath = favourRate(w, "fwrath", int(k));
			ag[k].prophet = cu.prophet.has_value();
		}
	}
	const double sickAt = w.tune<DiplomacyTune>().plagueSick;
	for (const Settlement& s : w.settlements) {
		if (s.dead || !cultureOk(w, s.culture)) continue;
		const size_t k = size_t(s.culture);
		const double p = std::max(0.0, finiteOr(s.pop, 0));
		if (finiteOr(s.plague, 0) > sickAt) ag[k].sick += p;
		ag[k].pop += p;
		ag[k].towns++;
		ag[k].love += p * finiteOr(s.love, 0);
		ag[k].devotion += p * finiteOr(s.devotion, 0);
		wsum[k] += p;
		cnt[k] += 1;
		loveU[k] += finiteOr(s.love, 0);
		devU[k] += finiteOr(s.devotion, 0);
	}
	for (size_t k = 0; k < nc; k++) {
		ag[k].sick = ag[k].pop > 0 ? ag[k].sick / ag[k].pop : 0;
		if (wsum[k] > 0) { ag[k].love /= wsum[k]; ag[k].devotion /= wsum[k]; }
		else if (cnt[k] > 0) { ag[k].love = loveU[k] / cnt[k]; ag[k].devotion = devU[k] / cnt[k]; }
		else { ag[k].love = 0; ag[k].devotion = 0; }
	}
	return ag;
}

// Contact / border geometry. minGap[a * nc + b] (255 = none), edges[a * nc + b] (gap <= borderGap).
void geometry(const World& w, std::vector<Agg>& ag, std::vector<int>& minGap, std::vector<int>& edges) {
	const DiplomacyTune& T = w.tune<DiplomacyTune>();
	DiplomacyScratch& S = w.scratch<DiplomacyScratch>();
	const size_t nc = w.cultures.size();
	minGap.assign(nc * nc, 255);
	edges.assign(nc * nc, 0);
	if (!w.geo || w.n <= 0 || w.owner.size() != size_t(w.n)) return;
	const Geo& g = *w.geo;
	const size_t n = size_t(w.n);
	S.label.assign(n, -1);
	S.dist.assign(n, 255);
	S.queue.clear();
	auto seed = [&](int c, int cu) {
		if (S.label[size_t(c)] >= 0) return;
		S.label[size_t(c)] = cu;
		S.dist[size_t(c)] = 0;
		S.queue.push_back(c);
	};
	for (size_t c = 0; c < n; c++) {
		const int o = w.owner[c];
		if (o < 0 || size_t(o) >= w.settlements.size()) continue;
		const Settlement& s = w.settlements[size_t(o)];
		if (s.dead || !cultureAlive(w, s.culture)) continue;
		seed(int(c), s.culture);
	}
	for (const Settlement& s : w.settlements)
		if (!s.dead && w.valid(s.cell) && cultureAlive(w, s.culture)) seed(s.cell, s.culture);
	const int depth = std::clamp((T.contactHops + 1) / 2, 1, 120);
	for (size_t h = 0; h < S.queue.size(); h++) {
		const int c = S.queue[h];
		const int d = S.dist[size_t(c)];
		if (d >= depth) continue;
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
			const int m = g.nbr[size_t(6 * c + k)];
			if (m < 0 || S.label[size_t(m)] >= 0) continue;
			S.label[size_t(m)] = S.label[size_t(c)];
			S.dist[size_t(m)] = uint8_t(d + 1);
			S.queue.push_back(m);
		}
	}
	for (size_t h = 0; h < S.queue.size(); h++) {
		const int c = S.queue[h];
		const int lc = S.label[size_t(c)];
		const int dc = S.dist[size_t(c)];
		bool edge = false;
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
			const int m = g.nbr[size_t(6 * c + k)];
			if (m < 0) continue;
			const int lm = S.label[size_t(m)];
			if (dc == 0 && (lm != lc || S.dist[size_t(m)] != 0)) edge = true;
			if (lm < 0 || lm == lc || m < c) continue;
			const int gap = dc + int(S.dist[size_t(m)]) + 1;
			const size_t ab = size_t(lc) * nc + size_t(lm), ba = size_t(lm) * nc + size_t(lc);
			if (gap < minGap[ab]) { minGap[ab] = gap; minGap[ba] = gap; }
			// a frontier is shared on land: a strait gives contact, not a border
			if (gap <= T.borderGap && !state::isSea(w, c) && !state::isSea(w, m)) { edges[ab]++; edges[ba]++; }
		}
		if (dc == 0) {
			ag[size_t(lc)].cells++;
			if (edge) ag[size_t(lc)].perim++;
		}
	}
}

// A route between two live settlements, from `from` to `to` (oriented). Uses the cache; searches only
// while the per-call budget lasts. Returns false when there is none (or no budget).
bool route(World& w, int from, int to, NavalMode naval, double maxCost, std::vector<int32_t>& out) {
	const DiplomacyTune& T = w.tune<DiplomacyTune>();
	DiplomacyScratch& S = w.scratch<DiplomacyScratch>();
	out.clear();
	const Settlement* a = liveSettlement(w, from);
	const Settlement* b = liveSettlement(w, to);
	if (!a || !b || from == to) return false;
	const int lo = std::min(from, to), hi = std::max(from, to);
	const auto key = std::make_tuple(lo, hi, int(naval));
	const int loCell = from == lo ? a->cell : b->cell;
	const int hiCell = from == lo ? b->cell : a->cell;
	auto it = S.routes.find(key);
	bool fresh = it != S.routes.end() && w.year - it->second.year < T.routeYears && w.year >= it->second.year;
	if (fresh && !it->second.cells.empty() && (it->second.cells.front() != loCell || it->second.cells.back() != hiCell)) fresh = false;
	// a cached "no route" found under a smaller budget does not answer a larger one
	if (fresh && it->second.cells.empty() && it->second.cost < maxCost) fresh = false;
	if (!fresh) {
		if (S.budget <= 0) return false;
		S.budget--;
		S.searches++;
		path::FindOpts fo;
		fo.naval = naval;
		fo.maxCost = maxCost;
		double cost = 0;
		const int fromCell = a->cell, toCell = b->cell;
		std::vector<int32_t> cells = path::find(w, fromCell, toCell, fo, &cost);
		if (!cells.empty() && (cells.front() != fromCell || cells.back() != toCell)) cells.clear();
		if (!cells.empty() && from != lo) std::reverse(cells.begin(), cells.end());
		RouteEntry e;
		e.cost = cells.empty() ? maxCost : finiteOr(cost, maxCost);
		e.cells = std::move(cells);
		e.year = w.year;
		it = S.routes.insert_or_assign(key, std::move(e)).first;
	}
	const RouteEntry& r = it->second;
	if (r.cells.empty() || !(r.cost <= maxCost)) return false;
	out = r.cells;
	if (from != lo) std::reverse(out.begin(), out.end());
	return true;
}

// units::spawn refused a cached route (the ground changed under it: lava, a risen sea, sea ice): drop
// it so the next attempt searches again instead of retrying the dead route until it expires.
void forgetRoute(World& w, int from, int to, NavalMode naval) {
	DiplomacyScratch& S = w.scratch<DiplomacyScratch>();
	S.routes.erase(std::make_tuple(std::min(from, to), std::max(from, to), int(naval)));
}

// Live settlement ids of a culture, largest first (ties by id).
std::vector<int> byPop(const World& w, int cultureId) {
	std::vector<int> ids;
	for (const Settlement& s : w.settlements)
		if (!s.dead && s.culture == cultureId && w.valid(s.cell)) ids.push_back(s.id);
	std::sort(ids.begin(), ids.end(), [&](int x, int y) {
		const double px = w.settlements[size_t(x)].pop, py = w.settlements[size_t(y)].pop;
		if (px != py) return px > py;
		return x < y;
	});
	return ids;
}

// Could culture x carry a war to y at all? A shared landmass (w.cache.landId), or from the Bronze age a
// strait of at most borderGap hops (canoes along the shelf), or from the Sail age a port.
bool canReach(const World& w, int x, int y, int gap) {
	const DiplomacyTune& T = w.tune<DiplomacyTune>();
	const int age = cultureOk(w, x) ? w.cultures[size_t(x)].age : 0;
	if (age >= Age::BRONZE && gap <= T.borderGap) return true;
	bool port = false;
	std::vector<int> massX, massY;
	const bool haveIds = w.cache.landId.size() == size_t(w.n);
	for (const Settlement& s : w.settlements) {
		if (s.dead || !w.valid(s.cell)) continue;
		if (s.culture == x && s.has(Building::PORT)) port = true;
		const int m = haveIds ? w.cache.landId[size_t(s.cell)] : 0;
		if (s.culture == x) massX.push_back(m);
		else if (s.culture == y) massY.push_back(m);
	}
	if (age >= Age::SAIL && port) return true;
	for (const int m : massX)
		if (std::find(massY.begin(), massY.end(), m) != massY.end()) return true;
	return false;
}

int liveCount(const World& w, int cultureId) {
	int k = 0;
	for (const Settlement& s : w.settlements)
		if (!s.dead && s.culture == cultureId) k++;
	return k;
}

// One army from side x toward an enemy settlement of y. True when one was sent.
bool sendArmy(World& w, int x, int y) {
	const DiplomacyTune& T = w.tune<DiplomacyTune>();
	DiplomacyScratch& S = w.scratch<DiplomacyScratch>();
	if (!w.geo) return false;
	const std::vector<int> sources = byPop(w, x);
	const std::vector<int> targets = byPop(w, y);
	if (sources.empty() || targets.empty()) return false;
	const double maxAng = T.armyMaxCost * w.geo->step;
	int attempts = 0;
	std::vector<int32_t> cells;
	for (const int src : sources) {
		// (cached routes need no search: they stay usable after the year's search budget is spent)
		if (attempts >= T.armyAttempts) break;
		const Settlement& s = w.settlements[size_t(src)];
		const double size = T.armyFrac * std::max(0.0, finiteOr(s.pop, 0));
		if (size < T.armyMinSize) continue;
		int best = -1;
		double bestAng = 0;
		for (const int t : targets) {
			const double a = w.geo->angle(s.cell, w.settlements[size_t(t)].cell);
			if (a > maxAng) continue;
			if (best < 0 || a < bestAng) { best = t; bestAng = a; }
		}
		if (best < 0) continue;
		const NavalMode naval = navalOf(w, s);
		const int before = S.searches;
		const bool found = route(w, src, best, naval, T.armyMaxCost, cells);
		if (S.searches > before) attempts++;
		if (!found) continue;
		units::SpawnOpts o;
		o.type = UnitType::Army;
		o.culture = x;
		o.from = src;
		o.to = best;
		o.dest = w.settlements[size_t(best)].cell;
		o.size = size;
		o.naval = naval;
		o.maxCost = T.armyMaxCost;
		o.path = cells;
		const Unit* u = units::spawn(w, o);
		if (!u) { forgetRoute(w, src, best, naval); continue; }
		Settlement& origin = w.settlements[size_t(src)];
		origin.pop = std::max(0.0, origin.pop - size);
		return true;
	}
	return false;
}

// Atomic exchange between the warring pair (a, b): two strikes per side.
void strike(World& w, int from, int to, int sid) {
	const DiplomacyTune& T = w.tune<DiplomacyTune>();
	DiplomacyScratch& S = w.scratch<DiplomacyScratch>();
	const Settlement* s = liveSettlement(w, sid);
	if (!s) return;
	const int cell = s->cell;
	w.bus.emit(EvNuke{ cell, from, to });
	civ::damage(w, sid, T.nukeDamage, Cause::Nuke);
	if (w.geo && w.rad.size() == size_t(w.n)) {
		w.geo->ring(cell, T.nukeHops, S.tmp);
		const std::vector<int32_t> cells = S.tmp;
		for (const int c : cells) w.rad[size_t(c)] = float(std::max(double(w.rad[size_t(c)]), T.nukeRad));
		w.geo->ring(cell, T.nukeFireHops, S.tmp);
		const std::vector<int32_t> fire = S.tmp;
		for (const int c : fire) life::ignite(w, c, 1);
		w.dirty.color = true;
	}
	climate::addDust(w, T.nukeDust);
	society::WitnessOpts wo;
	wo.kind = "nuke";
	wo.faith = 0;
	wo.good = 0;
	wo.natural = true;                   // (v3) the people's own fire: not a deed of the god (no memory, not 'heard')
	society::witness(w, cell, T.nukeWitnessAng, wo);
}

void nukeExchange(World& w, int a, int b) {
	const DiplomacyTune& T = w.tune<DiplomacyTune>();
	std::vector<std::pair<int, int>> plan;   // (attacker culture, target settlement)
	for (int side = 0; side < 2; side++) {
		const int x = side == 0 ? a : b, y = side == 0 ? b : a;
		const std::vector<int> t = byPop(w, y);
		for (size_t k = 0; k < t.size() && int(k) < T.nukeStrikes; k++) plan.emplace_back(x, t[k]);
	}
	for (const auto& p : plan) strike(w, p.first, p.first == a ? b : a, p.second);
	w.vars.lastNuke = w.year;
	society::makePeace(w, a, b, T.nukeTruce, -1);
}

void addExhaustion(World& w, int c, double amount) {
	if (!cultureOk(w, c)) return;
	Culture& cu = w.cultures[size_t(c)];
	cu.exhaustion = std::clamp(finiteOr(cu.exhaustion, 0) + amount, 0.0, 1.0);
}

void battle(World& w, int unitId) {
	const DiplomacyTune& T = w.tune<DiplomacyTune>();
	const Unit* u = state::findUnit(w, unitId);
	if (!u) return;
	const int attacker = u->culture;
	const double size = std::max(0.0, finiteOr(u->size, 0));
	const int sid = u->to;
	const Settlement* s = liveSettlement(w, sid);
	if (!s || !cultureAlive(w, attacker)) { units::kill(w, unitId, "disbanded"); return; }
	const int defender = s->culture;
	if (defender == attacker) return;                                    // already ours: the army stands down
	if (!society::atWar(w, attacker, defender)) { units::kill(w, unitId, "disbanded"); return; }
	const double attack = size * techOf(w, attacker) * w.rng.range(T.battleLo, T.battleHi);
	const double pop = std::max(0.0, finiteOr(s->pop, 0));
	const double defence = T.defenceK * pop * (1 + (s->has(Building::WALLS) ? 1 : 0)) * techOf(w, defender);
	const bool won = attack > defence;
	const int cell = s->cell;
	w.bus.emit(EvBattle{ cell, attacker, defender, won, sid });
	const std::string pre = warPrefix(attacker, defender);
	if (won) {
		addExhaustion(w, defender, T.exhaustBattle + T.exhaustTown);
		const std::string lostKey = pre + "lost" + ns(defender);
		setNum(w, lostKey, getNum(w, lostKey, 0) + 1);
		if (pop < T.razeBelow) {
			civ::destroy(w, sid, Cause::War);
		} else {
			civ::damage(w, sid, T.defenderLoss, Cause::War);
			const Settlement* still = liveSettlement(w, sid);
			if (still && still->culture == defender) civ::capture(w, sid, attacker);
		}
	} else {
		addExhaustion(w, attacker, T.exhaustBattle);
		units::kill(w, unitId, "war");
	}
}

void caravan(World& w, int unitId) {
	const DiplomacyTune& T = w.tune<DiplomacyTune>();
	const Unit* u = state::findUnit(w, unitId);
	if (!u) return;
	const int from = u->from, to = u->to, cu = u->culture;
	const Settlement* target = liveSettlement(w, to);
	if (!target) return;
	const int tc = target->culture;
	// the cultures went to war while the caravan was on the road: the gates stay shut (no trade, no road,
	// no route); caravans never leave for an enemy town in the first place (sendTrader)
	if (cu != tc && society::atWar(w, cu, tc)) return;
	const std::vector<int32_t> cells = u->path;
	// relations (foreign caravans only)
	if (cultureAlive(w, cu) && cultureAlive(w, tc) && cu != tc) {
		if (Relation* r = society::rel(w, cu, tc)) {
			r->trade = std::clamp(finiteOr(r->trade, 0) + T.tradeStep * (1 - finiteOr(r->trade, 0)), 0.0, 1.0);
			r->v = std::clamp(finiteOr(r->v, 0) + T.tradeArriveV, -1.0, 1.0);
			r->contact = true;
		}
	}
	// plague rides with the merchants
	const Settlement* origin = state::settlement(w, from);
	if (origin && origin->plague > 0 && isFinite(origin->plague)) {
		const double sev = std::clamp(origin->plague, 0.0, 1.0);
		if (w.rng.chance(T.plagueHop * sev)) civ::infect(w, to, std::max(0.3, sev));
	}
	// roads along the land part of the route
	if (w.road.size() == size_t(w.n)) {
		Rng rr = w.rng.fork("diplomacy:road:" + ns(unitId));
		bool any = false;
		for (const int c : cells) {
			if (!w.valid(c) || state::isSea(w, c) || hydro::isLake(w, c)) continue;   // no roads on water
			const int lv = w.road[size_t(c)];
			if (lv >= T.roadCap || lv >= int(T.roadP.size())) continue;
			if (rr.chance(T.roadP[size_t(lv)])) { w.road[size_t(c)] = uint8_t(lv + 1); any = true; }
		}
		if (any) w.dirty.roads = true;
	}
	// the first caravan between the pair
	if (from >= 0 && to >= 0 && from != to) {
		const std::string key = "diplomacy_route" + ns(std::min(from, to)) + "_" + ns(std::max(from, to));
		if (w.vars.num.find(key) == w.vars.num.end()) {
			setNum(w, key, w.year);
			w.bus.emit(EvTradeRoute{ from, to });
		}
	}
}

void prophetVisit(World& w, int unitId) {
	const SocietyTune& ST = w.tune<SocietyTune>();
	const Unit* u = state::findUnit(w, unitId);
	if (!u || !cultureAlive(w, u->culture)) return;
	const int c = u->culture, to = u->to;
	Culture& cu = w.cultures[size_t(c)];
	if (!cu.prophet) return;
	if (!state::settlement(w, to)) return;
	Settlement& s = w.settlements[size_t(to)];
	if (s.dead || s.culture != c) return;
	s.devotion = std::clamp(finiteOr(s.devotion, 0) + ST.prophetDevotion, 0.0, 1.0);
	cu.prophet->settlement = to;
}

// One caravan from settlement sid if it can. Returns true if one left.
bool sendTrader(World& w, int sid) {
	const DiplomacyTune& T = w.tune<DiplomacyTune>();
	const Settlement* s = liveSettlement(w, sid);
	if (!s || !w.geo) return false;
	const int cu = s->culture;
	const int cell = s->cell;
	const double maxAng = T.tradeHops * w.geo->step;
	struct Cand { int id; double score; };
	std::vector<Cand> cands;
	for (const Settlement& p : w.settlements) {
		if (p.dead || p.id == sid || p.tier < 1 || !w.valid(p.cell)) continue;
		if (cu != p.culture && society::atWar(w, cu, p.culture)) continue;
		const double a = w.geo->angle(cell, p.cell);
		if (a > maxAng) continue;
		const double hops = a / std::max(1e-9, w.geo->step);
		double score = std::sqrt(std::max(0.0, finiteOr(p.pop, 0)));
		if (p.culture != cu) score *= 1.5;
		if (p.has(Building::MARKET)) score *= 1.5;
		score /= 1 + hops / 10;
		if (score > 0 && isFinite(score)) cands.push_back({ p.id, score });
	}
	if (cands.empty()) return false;
	std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) {
		if (x.score != y.score) return x.score > y.score;
		return x.id < y.id;
	});
	if (cands.size() > 4) cands.resize(4);
	const Cand* pick = w.rng.weighted(cands, [](const Cand& c) { return c.score; });
	if (!pick) return false;
	const int to = pick->id;
	const NavalMode naval = navalOf(w, *s);
	const double maxCost = naval == NavalMode::Open ? T.tradeNavalMaxCost : T.tradeMaxCost;
	std::vector<int32_t> cells;
	if (!route(w, sid, to, naval, maxCost, cells)) return false;
	units::SpawnOpts o;
	o.type = UnitType::Trader;
	o.culture = cu;
	o.from = sid;
	o.to = to;
	o.dest = w.settlements[size_t(to)].cell;
	o.size = T.traderSize;
	o.naval = naval;
	o.maxCost = maxCost;
	o.path = cells;
	if (units::spawn(w, o)) return true;
	forgetRoute(w, sid, to, naval);
	return false;
}

int parseInt(std::string_view sv, size_t& pos) {
	int v = -1;
	const char* b = sv.data() + pos;
	const char* e = sv.data() + sv.size();
	const auto r = std::from_chars(b, e, v);
	if (r.ec != std::errc()) return -1;
	pos = size_t(r.ptr - sv.data());
	return v;
}

// Drop the numbers of dead settlements and stale cached routes.
void prune(World& w) {
	DiplomacyScratch& S = w.scratch<DiplomacyScratch>();
	const DiplomacyTune& T = w.tune<DiplomacyTune>();
	auto deadSid = [&](int id) { const Settlement* s = state::settlement(w, id); return !s || s->dead; };
	for (auto it = w.vars.num.begin(); it != w.vars.num.end();) {
		const std::string& k = it->first;
		bool drop = false;
		if (k.rfind("diplomacy_trade", 0) == 0) {
			size_t pos = 15;
			const int a = parseInt(k, pos);
			drop = a < 0 || deadSid(a);
		} else if (k.rfind("diplomacy_route", 0) == 0) {
			size_t pos = 15;
			const int a = parseInt(k, pos);
			int b = -1;
			if (pos < k.size() && k[pos] == '_') { pos++; b = parseInt(k, pos); }
			drop = a < 0 || b < 0 || deadSid(a) || deadSid(b);
		} else if (k.rfind("diplomacy_sworn", 0) == 0 || k.rfind("diplomacy_feud", 0) == 0) {
			// (v3.1) a sworn peace that ended; the pair's feud or peace once one of them is gone
			const bool sworn = k[10] == 's';
			size_t pos = sworn ? 15 : 14;
			const int a = parseInt(k, pos);
			int b = -1;
			if (pos < k.size() && k[pos] == '_') { pos++; b = parseInt(k, pos); }
			drop = !cultureAlive(w, a) || !cultureAlive(w, b) || (sworn && !(it->second > w.year));
		}
		it = drop ? w.vars.num.erase(it) : std::next(it);
	}
	for (auto it = S.routes.begin(); it != S.routes.end();) {
		const bool drop = deadSid(std::get<0>(it->first)) || deadSid(std::get<1>(it->first)) || w.year - it->second.year >= T.routeYears || w.year < it->second.year;
		it = drop ? S.routes.erase(it) : std::next(it);
	}
}

// (v3.1) end an alliance (event once)
void breakAlliance(World& w, int a, int b) {
	const std::string key = pairKey("ally", a, b);
	if (!w.vars.num.count(key)) return;
	w.vars.num.erase(key);
	const auto k = state::relKey(a, b);
	w.bus.emit(EvAlliance{ k.first, k.second, false });
}

// (v3.1, design review 11) did the god move a's people to this war against b? Its wrath on a in the last
// decades, or its favour of b over a (envy), at least blameMin (impulse a year).
bool blamedOnGod(const World& w, int a, int b) {
	const DiplomacyTune& T = w.tune<DiplomacyTune>();
	const double wrathA = favourRate(w, "fwrath", a);
	const double envy = favourRate(w, "fgrace", b) - favourRate(w, "fgrace", a);
	return wrathA >= T.blameMin || envy >= T.blameMin;
}

// declareWar's body; ally >= 0: a joins the war of its ally `ally`, whom b attacked (no further calls)
void startWar(World& w, int a, int b, int ally) {
	const DiplomacyTune& T = w.tune<DiplomacyTune>();
	if (!cultureAlive(w, a) || !cultureAlive(w, b) || a == b) return;
	Relation* r = society::rel(w, a, b);
	if (!r || r->war) return;
	breakAlliance(w, a, b);                      // allies who come to blows are allies no more
	r->war = true;
	r->since = w.year;
	r->aggressor = a;
	const std::string pre = warPrefix(a, b);
	erasePrefix(w, pre);
	setNum(w, pre + "n" + ns(a), liveCount(w, a));
	setNum(w, pre + "n" + ns(b), liveCount(w, b));
	if (ally >= 0) setNum(w, pre + "ally", ally);
	{
		const std::string fk = pairKey("feud", a, b);
		setNum(w, fk, getNum(w, fk, 0) + 1);
	}
	w.stats.wars++;
	const bool blamed = blamedOnGod(w, a, b);
	w.bus.emit(EvWarDeclared{ a, b, ally, blamed });
	// friends of the victim sour on the aggressor
	for (size_t z = 0; z < w.cultures.size(); z++) {
		const int zi = int(z);
		if (zi == a || zi == b || w.cultures[z].dead) continue;
		const Relation* fz = society::peek(w, zi, b);
		if (!fz || fz->war || !(fz->v >= T.allyV)) continue;
		if (Relation* rz = society::rel(w, zi, a)) rz->v = std::clamp(finiteOr(rz->v, 0) - T.allyOutrage, -1.0, 1.0);
	}
	// (v3.1) the call to arms: b's allies who can reach the aggressor come to its defence
	if (ally < 0 && T.allyCall) {
		for (const int z : alliesOf(w, b)) {
			if (z == a || !cultureAlive(w, z) || society::atWar(w, z, a) || isAlly(w, z, a)) continue;
			if (w.cultures[size_t(z)].edict == Edict::Peace) continue;
			const Relation* rz = society::peek(w, z, a);
			if (rz && w.year < rz->truce) continue;
			if (!canReach(w, z, a, 255)) continue;
			startWar(w, z, a, b);
		}
	}
}

} // namespace

namespace society {

Relation* rel(World& w, int a, int b) {
	if (a == b || !cultureOk(w, a) || !cultureOk(w, b)) return nullptr;
	return &w.vars.rel[state::relKey(a, b)];
}

const Relation* peek(const World& w, int a, int b) {
	if (a == b) return nullptr;
	const auto it = w.vars.rel.find(state::relKey(a, b));
	return it == w.vars.rel.end() ? nullptr : &it->second;
}

bool atWar(const World& w, int a, int b) {
	const Relation* r = peek(w, a, b);
	return r && r->war;
}

void declareWar(World& w, int a, int b) {
	startWar(w, a, b, -1);
}

void makePeace(World& w, int a, int b, double truceYears, int victor) {
	const DiplomacyTune& T = w.tune<DiplomacyTune>();
	Relation* r = rel(w, a, b);
	if (!r) return;
	if (!isFinite(truceYears) || truceYears < 0) truceYears = T.truceYears;
	if (!r->war) {
		r->truce = std::max(finiteOr(r->truce, 0), w.year + truceYears);
		return;
	}
	// (v3.1) old enemies tire of each other: a longer truce after each war of the pair
	const double feud = std::max(0.0, getNum(w, pairKey("feud", a, b), 1) - 1);
	const double truce = w.year + truceYears * (1 + std::max(0.0, T.feudK) * std::min(T.feudMax, feud));
	if (victor != a && victor != b) victor = -1;
	const int first = (r->aggressor == a || r->aggressor == b) ? r->aggressor : a;
	const int second = first == a ? b : a;
	r->war = false;
	r->v = T.peaceV;
	r->since = w.year;
	r->truce = truce;
	r->aggressor = -1;
	erasePrefix(w, warPrefix(a, b));
	// (v3.1, design review 11) a victor the god favoured while it fought thanks the god; the loser blames it
	bool thanked = false;
	if (victor >= 0 && cultureAlive(w, victor) && favourRate(w, "fgrace", victor) >= T.thankMin) {
		thanked = true;
		const int loser = victor == a ? b : a;
		for (Settlement& s : w.settlements) {
			if (s.dead) continue;
			if (s.culture == victor && T.thankDevotion > 0) s.devotion = sat(finiteOr(s.devotion, 0) + T.thankDevotion);
			if (s.culture == loser && T.blameLove > 0) s.love = std::clamp(finiteOr(s.love, 0) - T.blameLove, -1.0, 1.0);
		}
	}
	w.bus.emit(EvWarEnded{ first, second, victor, thanked });
	// (v3.1) the wars allies joined for a or b against the other end with it (the league makes peace together)
	struct Joined { int joiner, enemy, v; };
	std::vector<Joined> joined;
	for (const auto& kv : w.vars.rel) {
		if (!kv.second.war) continue;
		const int p = kv.first.first, q = kv.first.second;
		const double caller = getNum(w, warPrefix(p, q) + "ally", -1);
		for (const int principal : { a, b }) {
			const int enemy = principal == a ? b : a;
			if (int(caller) != principal || (p != enemy && q != enemy)) continue;
			const int joiner = p == enemy ? q : p;
			joined.push_back({ joiner, enemy, victor == principal ? joiner : victor == enemy ? enemy : -1 });
		}
	}
	for (const Joined& j : joined) makePeace(w, j.joiner, j.enemy, truceYears, j.v);
}

bool swearPeace(World& w, int a, int b) {
	const DiplomacyTune& T = w.tune<DiplomacyTune>();
	if (!T.omenSworn || a == b || !cultureAlive(w, a) || !cultureAlive(w, b) || !(T.omenYears > 0)) return false;
	setNum(w, pairKey("sworn", a, b), w.year + T.omenYears);
	return true;
}

std::vector<int> tense(const World& w, int cultureId) {
	const DiplomacyTune& T = w.tune<DiplomacyTune>();
	std::vector<int> out;
	if (!T.omenCalm || !cultureAlive(w, cultureId)) return out;
	for (const Culture& o : w.cultures) {
		if (o.dead || o.id == cultureId) continue;
		const Relation* r = peek(w, cultureId, o.id);
		if (r && !r->war && r->contact && r->v <= T.omenTension) out.push_back(o.id);
	}
	return out;
}

void onArrive(World& w, int unitId) {
	const Unit* u = state::findUnit(w, unitId);
	if (!u) return;
	switch (u->type) {
	case UnitType::Army: battle(w, unitId); break;
	case UnitType::Trader: caravan(w, unitId); break;
	case UnitType::Prophet: prophetVisit(w, unitId); break;
	default: break;
	}
}

void diplomacy(World& w) {
	if (!w.geo || w.cultures.empty()) return;
	const DiplomacyTune& T = w.tune<DiplomacyTune>();
	const SocietyTune& ST = w.tune<SocietyTune>();
	DiplomacyScratch& S = w.scratch<DiplomacyScratch>();
	S.searches = 0;
	S.budget = T.searchesPerYear;
	const size_t nc = w.cultures.size();

	std::vector<Agg> ag = aggregates(w);
	std::vector<int> minGap, edges;
	geometry(w, ag, minGap, edges);
	auto inContact = [&](int a, int b) {
		if (minGap[size_t(a) * nc + size_t(b)] <= T.contactHops) return true;
		const Relation* r = peek(w, a, b);
		return r && r->trade > T.tradeContact;
	};
	// crusade targets: the least devout living culture in contact
	for (size_t a = 0; a < nc; a++) {
		if (!ag[a].alive || w.cultures[a].edict != Edict::Crusade) continue;
		int best = -1;
		for (size_t b = 0; b < nc; b++) {
			if (b == a || !ag[b].alive || ag[b].towns == 0 || !inContact(int(a), int(b))) continue;
			if (best < 0 || ag[b].devotion < ag[size_t(best)].devotion) best = int(b);
		}
		ag[a].crusadeTarget = best;
	}
	auto aggr = [&](int a, int b) {
		double x = finiteOr(w.cultures[size_t(a)].traits.aggression, 0.5);
		if (ag[size_t(a)].crusadeTarget == b) x += ST.crusadeAggression;
		return x;
	};

	// 1. wars in progress: weariness, peace, the bomb
	std::vector<std::pair<int, int>> wars;
	for (const auto& kv : w.vars.rel)
		if (kv.second.war) wars.push_back(kv.first);
	for (const auto& pr : wars) {
		const int a = pr.first, b = pr.second;
		// (sim-findings-2 verifier) a war an ally joined may already have ended this year with its principal
		// (makePeace ends the joined wars too): no weariness, no second peace and no bomb for a pair at peace
		if (!atWar(w, a, b)) continue;
		const bool okA = cultureAlive(w, a) && ag[size_t(a)].towns > 0;
		const bool okB = cultureAlive(w, b) && ag[size_t(b)].towns > 0;
		if (!okA || !okB) { makePeace(w, a, b, T.truceYears, okA ? a : okB ? b : -1); continue; }
		addExhaustion(w, a, T.exhaustYear);
		addExhaustion(w, b, T.exhaustYear);
		const Relation* r = peek(w, a, b);
		const std::string pre = warPrefix(a, b);
		const double lostA = getNum(w, pre + "lost" + ns(a), 0), lostB = getNum(w, pre + "lost" + ns(b), 0);
		const double n0A = std::max(1.0, getNum(w, pre + "n" + ns(a), 1)), n0B = std::max(1.0, getNum(w, pre + "n" + ns(b), 1));
		const bool fallA = lostA >= T.lostShare * n0A, fallB = lostB >= T.lostShare * n0B;
		const bool tiredA = w.cultures[size_t(a)].exhaustion >= 1, tiredB = w.cultures[size_t(b)].exhaustion >= 1;
		if (fallA || fallB) { makePeace(w, a, b, T.truceYears, fallA && fallB ? -1 : fallA ? b : a); continue; }
		if (tiredA || tiredB) { makePeace(w, a, b, T.truceYears, tiredA && tiredB ? -1 : tiredA ? b : a); continue; }
		if (r && w.year - r->since >= T.warMaxYears) {
			makePeace(w, a, b, T.truceYears, lostA < lostB ? a : lostB < lostA ? b : -1);
			continue;
		}
		// (v3.1, design review 11/13) a war of nothing peters out: no army from either side for warStall years
		if (r && T.warStall > 0 && w.year - r->since >= T.warStall) {
			const double lastA = getNum(w, pre + "army" + ns(a), -1e9), lastB = getNum(w, pre + "army" + ns(b), -1e9);
			if (w.year - std::max(lastA, lastB) >= T.warStall) {
				makePeace(w, a, b, T.truceYears, lostA < lostB ? a : lostB < lostA ? b : -1);
				continue;
			}
		}
		// (v3.1, design review 11/13) after the world's first exchange the bomb is feared (nukeRepeatMul)
		const double pNuke = T.nukeP * (w.vars.lastNuke ? T.nukeRepeatMul : 1.0);
		if (ag[size_t(a)].age >= Age::ATOMIC && ag[size_t(b)].age >= Age::ATOMIC && w.rng.chance(pNuke)) nukeExchange(w, a, b);
	}

	// a peace made this year starts from v = -0.1: no drift on top of it
	std::vector<std::pair<int, int>> ended;
	for (const auto& pr : wars)
		if (!atWar(w, pr.first, pr.second)) ended.push_back(pr);

	// 2. relations between living cultures (ascending pairs)
	for (size_t a = 0; a < nc; a++) {
		if (!ag[a].alive) continue;
		for (size_t b = a + 1; b < nc; b++) {
			if (!ag[b].alive) continue;
			const int ia = int(a), ib = int(b);
			if (std::find(ended.begin(), ended.end(), std::make_pair(ia, ib)) != ended.end()) continue;
			const bool contact = inContact(ia, ib);
			if (!contact && !peek(w, ia, ib)) continue;
			Relation* r = rel(w, ia, ib);
			if (!r) continue;
			r->contact = contact;
			const double trade = std::clamp(finiteOr(r->trade, 0), 0.0, 1.0);
			double v = finiteOr(r->v, 0);
			// (v3.1) an Omen's sworn peace: the pair rests at omenTarget until it ends
			double target = T.relTarget;
			{
				const std::string sk = pairKey("sworn", ia, ib);
				const auto it = w.vars.num.find(sk);
				if (it != w.vars.num.end()) {
					if (isFinite(it->second) && w.year < it->second) target = T.omenTarget;
					else w.vars.num.erase(it);
				}
			}
			double dv = T.relax * (target - v) + T.tradeK * trade;
			if (contact) {
				const int perim = std::max(T.borderMinPerim, std::min(ag[a].perim, ag[b].perim));
				const double border = sat(double(edges[a * nc + b]) / (2.0 * std::max(1, perim)));
				// (v3.1, design review 11) frontiers chafe more as the peoples grow
				const double ageMul = T.borderAge[size_t(std::max(ag[a].age, ag[b].age))];
				dv -= T.borderK * (isFinite(ageMul) ? ageMul : 1.0) * border;
				dv -= T.aggrK * std::max(0.0, aggr(ia, ib) + aggr(ib, ia) - T.aggrBase);
				dv -= T.loveK * std::fabs(ag[a].love - ag[b].love);
				// (v3.1, design review 11) the god in the relations: a favourite is envied and the scourged
				// blame the spared; peoples struck together stand together; a fever is blamed on the
				// neighbours it came from; a prophet's zeal against the less devout, and devout peoples' kinship
				if (T.envyK > 0 && T.favourRef > 0) {
					const double fa = ag[a].fGrace - ag[a].fWrath, fb = ag[b].fGrace - ag[b].fWrath;
					dv -= T.envyK * sat(std::fabs(fa - fb) / T.favourRef);
				}
				if (T.sharedWrathK > 0 && T.favourRef > 0) dv += T.sharedWrathK * sat(std::min(ag[a].fWrath, ag[b].fWrath) / T.favourRef);
				if (T.plagueK > 0) dv -= T.plagueK * std::max(ag[a].sick, ag[b].sick) * sat(trade + border);
				if (T.zealK > 0 && T.zealRef > 0) {
					if (ag[a].prophet) dv -= T.zealK * sat((ag[a].devotion - ag[b].devotion) / T.zealRef);
					if (ag[b].prophet) dv -= T.zealK * sat((ag[b].devotion - ag[a].devotion) / T.zealRef);
				}
				if (T.faithK > 0 && (ag[a].prophet || ag[b].prophet) && ag[a].devotion >= T.faithDev && ag[b].devotion >= T.faithDev) dv += T.faithK;
				dv += T.noise * w.rng.gauss();
				if (w.cultures[a].edict == Edict::Peace) dv += T.peaceEdictV;
				if (w.cultures[b].edict == Edict::Peace) dv += T.peaceEdictV;
				if (!r->war) {
					bool common = false;
					for (size_t z = 0; z < nc && !common; z++)
						if (z != a && z != b && atWar(w, ia, int(z)) && atWar(w, ib, int(z))) common = true;
					if (common) dv += T.commonEnemyV;
				}
			}
			v = std::clamp(v + finiteOr(dv, 0), -1.0, 1.0);
			r->v = v;
			const double decayed = trade * (1 - T.tradeDecay);
			r->trade = decayed < 1e-4 ? 0 : decayed;
		}
	}

	// 2b. (v3.1, design review 11) alliances: friends at peace become allies, and fall apart
	{
		std::vector<std::pair<int, int>> have;
		std::vector<std::string> junk;
		const std::string pre = "diplomacy_ally";
		for (auto it = w.vars.num.lower_bound(pre); it != w.vars.num.end() && it->first.compare(0, pre.size(), pre) == 0; ++it) {
			size_t pos = pre.size();
			const int x = parseInt(it->first, pos);
			int y = -1;
			if (pos < it->first.size() && it->first[pos] == '_') { pos++; y = parseInt(it->first, pos); }
			if (!cultureOk(w, x) || !cultureOk(w, y) || x == y) junk.push_back(it->first);
			else have.emplace_back(x, y);
		}
		for (const std::string& k : junk) w.vars.num.erase(k);
		std::vector<int> count(nc, 0);
		for (const auto& p : have) {
			const Relation* r = peek(w, p.first, p.second);
			const bool keep = cultureAlive(w, p.first) && cultureAlive(w, p.second) && r && !r->war && r->v >= T.allyBreak;
			if (!keep) { breakAlliance(w, p.first, p.second); continue; }
			count[size_t(p.first)]++;
			count[size_t(p.second)]++;
		}
		if (T.allyForm <= 1) {
			std::vector<std::pair<int, int>> cand;
			for (const auto& kv : w.vars.rel) {
				const int x = kv.first.first, y = kv.first.second;
				if (!cultureAlive(w, x) || !cultureAlive(w, y) || kv.second.war || !kv.second.contact || !(kv.second.v >= T.allyForm)) continue;
				if (isAlly(w, x, y)) continue;
				cand.emplace_back(x, y);
			}
			// the warmest friendships first (ties: lower ids)
			std::sort(cand.begin(), cand.end(), [&](const std::pair<int, int>& p, const std::pair<int, int>& q) {
				const double vp = peek(w, p.first, p.second)->v, vq = peek(w, q.first, q.second)->v;
				if (vp != vq) return vp > vq;
				return p < q;
			});
			for (const auto& p : cand) {
				if (count[size_t(p.first)] >= T.allyMax || count[size_t(p.second)] >= T.allyMax) continue;
				setNum(w, pairKey("ally", p.first, p.second), w.year);
				count[size_t(p.first)]++;
				count[size_t(p.second)]++;
				w.bus.emit(EvAlliance{ p.first, p.second, true });
			}
		}
	}

	// 3. new wars
	for (size_t a = 0; a < nc; a++) {
		if (!ag[a].alive || ag[a].towns == 0) continue;
		for (size_t b = a + 1; b < nc; b++) {
			if (!ag[b].alive || ag[b].towns == 0) continue;
			const int ia = int(a), ib = int(b);
			const Relation* r = peek(w, ia, ib);
			if (!r || r->war || !r->contact || !(r->v < T.warV) || w.year < r->truce) continue;
			if (w.cultures[a].edict == Edict::Peace || w.cultures[b].edict == Edict::Peace) continue;
			if (isAlly(w, ia, ib)) continue;
			const double xa = aggr(ia, ib), xb = aggr(ib, ia);
			const int agg = xb > xa ? ib : ia;
			const int def = agg == ia ? ib : ia;
			// (v3.1) a people still weary from its last war, or from a century of them, starts no new one
			if (finiteOr(w.cultures[size_t(agg)].exhaustion, 0) > T.warWeary) continue;
			if (T.warredMax > 0 && getNum(w, "diplomacy_warred" + ns(agg), 0) > T.warredMax) continue;
			const double sAgg = ag[size_t(agg)].pop * ag[size_t(agg)].tech;
			double sDef = ag[size_t(def)].pop * ag[size_t(def)].tech;
			if (!(sAgg >= T.warStrength * sDef) || !(sAgg > 0)) continue;
			if (!canReach(w, agg, def, minGap[a * nc + b])) continue;
			// (v3.1) the defender's allies who would come: an aggressor must match them too
			if (T.allyDeter && T.allyCall) {
				for (const int z : alliesOf(w, def)) {
					if (z == agg || !ag[size_t(z)].alive || atWar(w, z, agg) || isAlly(w, z, agg)) continue;
					if (w.cultures[size_t(z)].edict == Edict::Peace) continue;
					const Relation* rz = peek(w, z, agg);
					if (rz && w.year < rz->truce) continue;
					if (!canReach(w, z, agg, 255)) continue;
					sDef += ag[size_t(z)].pop * ag[size_t(z)].tech;
				}
				if (!(sAgg >= T.warStrength * sDef)) continue;
			}
			declareWar(w, agg, def);
		}
	}

	// 4. weariness fades in peace; (v3.1) the years at war are remembered (warredMax)
	const double warredKeep = T.warredHalfLife > 0 ? std::pow(0.5, 1.0 / T.warredHalfLife) : 0.0;
	for (size_t a = 0; a < nc; a++) {
		const std::string wk = "diplomacy_warred" + ns(int(a));
		if (w.cultures[a].dead) {
			if (T.warredMax > 0) w.vars.num.erase(wk);
			continue;
		}
		bool fighting = false;
		for (const auto& kv : w.vars.rel)
			if (kv.second.war && (kv.first.first == int(a) || kv.first.second == int(a))) { fighting = true; break; }
		if (!fighting) w.cultures[a].exhaustion = std::max(0.0, finiteOr(w.cultures[a].exhaustion, 0) - T.exhaustDecay);
		if (T.warredMax > 0) {
			const double v = getNum(w, wk, 0) * warredKeep + (fighting ? 1.0 : 0.0);
			if (v > 0.01) setNum(w, wk, v);
			else w.vars.num.erase(wk);
		}
	}

	// 5. armies (each side of each war, every armyEvery years)
	wars.clear();
	for (const auto& kv : w.vars.rel)
		if (kv.second.war) wars.push_back(kv.first);
	for (const auto& pr : wars) {
		const Relation* r = peek(w, pr.first, pr.second);
		if (!r || !r->war) continue;
		const int first = (r->aggressor == pr.first || r->aggressor == pr.second) ? r->aggressor : pr.first;
		const int second = first == pr.first ? pr.second : pr.first;
		const std::string pre = warPrefix(first, second);
		for (int side = 0; side < 2; side++) {
			const int x = side == 0 ? first : second, y = side == 0 ? second : first;
			const std::string key = pre + "army" + ns(x);
			const double last = getNum(w, key, -1e9);
			if (w.year - last < T.armyEvery) continue;
			if (sendArmy(w, x, y)) setNum(w, key, w.year);
		}
	}

	// 6. trade: towns with a market, every tradeEvery years (staggered by id)
	struct Due { int id; double last; };
	std::vector<Due> due;
	for (const Settlement& s : w.settlements) {
		if (s.dead || s.tier < 2 || !s.has(Building::MARKET) || !cultureAlive(w, s.culture)) continue;
		const std::string key = "diplomacy_trade" + ns(s.id);
		auto it = w.vars.num.find(key);
		if (it == w.vars.num.end() || !isFinite(it->second)) {
			const double start = w.year - double(s.id % std::max(1, int(T.tradeEvery)));
			it = w.vars.num.insert_or_assign(key, start).first;
		}
		if (w.year - it->second >= T.tradeEvery) due.push_back({ s.id, it->second });
	}
	std::sort(due.begin(), due.end(), [](const Due& x, const Due& y) {
		if (x.last != y.last) return x.last < y.last;
		return x.id < y.id;
	});
	for (const Due& d : due) {
		const bool sent = sendTrader(w, d.id);
		const std::string key = "diplomacy_trade" + ns(d.id);
		if (sent) setNum(w, key, w.year);
		else if (S.budget > 0) setNum(w, key, w.year - T.tradeEvery + T.tradeRetry);   // nobody to trade with: retry soon
		// out of search budget: stays due, first in line next year
	}

	// 7. housekeeping
	const int yr = int(std::floor(w.year));
	if (yr % 50 == 0) prune(w);
}

} // namespace society
} // namespace dm
