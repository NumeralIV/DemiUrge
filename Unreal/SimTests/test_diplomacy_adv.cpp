// DEMIURGE — SimTests/test_diplomacy_adv.cpp : adversarial verification of the relations half of Society
// (Sim/Diplomacy.cpp; SPEC-SIM 3.8, BUILD.md A10). Owner: the A10 verifier.
//
// Runs in `run.ps1 diplomacy` (every collaborator a stub; where one must act, hooks install a fake Civ,
// fake Units with real-like path validation and a breadth-first path::find) and in `run.ps1 all` (real
// modules: the hook tests return early; the last test then drives the real Civ / Units / Path / Economy /
// Society and writes shots/ue-diplomacy-adv-real-*.png).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Civ.h"
#include "Hydro.h"
#include "MapImg.h"
#include "Path.h"
#include "Society.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "Units.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

namespace mi = dm::mapimg;

bool seaAt(const World& w, int c) { return double(w.elev[size_t(c)]) < w.seaLevel; }

std::vector<int> hopsFrom(const World& w, int from) {
	std::vector<int> d(size_t(w.n), -1);
	std::vector<int> q{ from };
	d[size_t(from)] = 0;
	for (size_t h = 0; h < q.size(); h++) {
		const int c = q[h];
		for (int k = 0; k < w.geo->nbrN[size_t(c)]; k++) {
			const int m = w.geo->nbr[size_t(6 * c + k)];
			if (m >= 0 && d[size_t(m)] < 0) { d[size_t(m)] = d[size_t(c)] + 1; q.push_back(m); }
		}
	}
	return d;
}

// a habitable land cell exactly k hops from `from` whose ring of `rad` hops is all land and unowned
int landAt(const World& w, int from, int k, int rad = 2) {
	if (from < 0) return -1;
	const std::vector<int> d = hopsFrom(w, from);
	std::vector<int32_t> tmp;
	for (int c = 0; c < w.n; c++) {
		if (d[size_t(c)] != k || seaAt(w, c) || w.hab[size_t(c)] < 0.1f) continue;
		w.geo->ring(c, rad, tmp);
		bool good = true;
		for (const int m : tmp) if (seaAt(w, m) || w.owner[size_t(m)] >= 0) { good = false; break; }
		if (good) return c;
	}
	return -1;
}

int newFolk(World& w, double aggression, int age = 0) {
	Culture& c = state::newCulture(w);
	c.traits.aggression = aggression;
	c.age = age;
	c.name = "K" + std::to_string(c.id);
	return c.id;
}

int newTown(World& w, int cell, int cu, double pop, int radius = 2, int tier = 0) {
	if (cell < 0) return -1;
	Settlement& s = state::newSettlement(w, cell, cu);
	s.pop = pop;
	s.tier = tier;
	s.radius = radius;
	s.name = "S" + std::to_string(s.id);
	const int id = s.id;
	std::vector<int32_t> tmp;
	w.geo->ring(cell, radius, tmp);
	for (const int c : tmp) {
		if (seaAt(w, c) || w.owner[size_t(c)] >= 0) continue;
		w.owner[size_t(c)] = int16_t(id);
		w.settlements[size_t(id)].cells.push_back(c);
	}
	return id;
}

int count(const std::vector<Event>& evs, Ev t) {
	int k = 0;
	for (const Event& e : evs) if (e.type == t) k++;
	return k;
}

bool blocked(const World& w, int c) { return double(w.lava[size_t(c)]) > 0.2; }

bool passable(const World& w, int c, NavalMode naval) {
	if (blocked(w, c)) return false;
	if (!seaAt(w, c)) return true;
	if (naval == NavalMode::Open) return true;
	if (naval == NavalMode::Coastal) return double(w.elev[size_t(c)]) > w.seaLevel - 0.6;
	return false;
}

bool neighbours(const World& w, int a, int b) {
	for (int k = 0; k < w.geo->nbrN[size_t(a)]; k++) if (w.geo->nbr[size_t(6 * a + k)] == b) return true;
	return false;
}

std::vector<int32_t> bfs(const World& w, int from, int to, NavalMode naval, double maxCost) {
	if (!w.valid(from) || !w.valid(to) || !passable(w, to, naval)) return {};
	if (from == to) return { from };
	std::vector<int> prev(size_t(w.n), -2), depth(size_t(w.n), 0);
	std::vector<int> q{ from };
	prev[size_t(from)] = -1;
	for (size_t h = 0; h < q.size(); h++) {
		const int c = q[h];
		if (c == to) break;
		if (depth[size_t(c)] >= int(maxCost)) continue;
		for (int k = 0; k < w.geo->nbrN[size_t(c)]; k++) {
			const int m = w.geo->nbr[size_t(6 * c + k)];
			if (m < 0 || prev[size_t(m)] != -2 || !passable(w, m, naval)) continue;
			prev[size_t(m)] = c;
			depth[size_t(m)] = depth[size_t(c)] + 1;
			q.push_back(m);
		}
	}
	if (prev[size_t(to)] == -2) return {};
	std::vector<int32_t> p;
	for (int c = to; c != -1; c = prev[size_t(c)]) p.push_back(c);
	std::reverse(p.begin(), p.end());
	return p;
}

void kill(World& w, int sid, Cause cause) {
	Settlement* s = state::settlement(w, sid);
	if (!s || s->dead) return;
	s->dead = true;
	s->cause = cause;
	s->deadYear = w.year;
	for (int c = 0; c < w.n; c++) if (w.owner[size_t(c)] == sid) w.owner[size_t(c)] = -1;
	s->cells.clear();
}

// fakes like the real modules where it matters: spawn validates a ready route the way Units does
// (starts at the origin, ends at dest, a chain of passable neighbours) and refuses otherwise
void fakes() {
	auto& h = stub::hooks();
	h.civ_capture = [](World& w, int sid, int cu) { if (Settlement* s = state::settlement(w, sid)) if (!s->dead) s->culture = cu; };
	h.civ_destroy = [](World& w, int sid, Cause cause) { kill(w, sid, cause); };
	h.civ_damage = [](World& w, int sid, double frac, Cause cause) -> double {
		Settlement* s = state::settlement(w, sid);
		if (!s || s->dead) return 0.0;
		const double gone = s->pop * std::clamp(frac, 0.0, 1.0);
		s->pop -= gone;
		if (s->pop < 3) kill(w, sid, cause);
		return gone;
	};
	h.civ_infect = [](World& w, int sid, double sev) -> bool {
		Settlement* s = state::settlement(w, sid);
		if (!s || s->dead || s->immune > 0) return false;
		s->plague = std::max(s->plague, sev);
		return true;
	};
	h.units_spawn = [](World& w, const units::SpawnOpts& o) -> Unit* {
		const Settlement* from = state::settlement(w, o.from);
		if (!from || from->dead || w.units.size() >= 300) return nullptr;
		std::vector<int32_t> p = o.path;
		if (p.empty()) p = bfs(w, from->cell, o.dest, o.naval, std::isnan(o.maxCost) ? 400 : o.maxCost);
		if (p.empty() || p.front() != from->cell || p.back() != o.dest) return nullptr;
		for (size_t k = 0; k < p.size(); k++) {
			if (!passable(w, p[k], o.naval)) return nullptr;
			if (k && !neighbours(w, p[k - 1], p[k])) return nullptr;
		}
		Unit& u = state::newUnit(w, o.type);
		u.culture = o.culture; u.from = o.from; u.to = o.to; u.dest = o.dest;
		u.path = p; u.size = o.size; u.naval = o.naval;
		u.speed = o.type == UnitType::Trader ? 7 : 5;
		u.data["arrive"] = w.year + std::max(1.0, std::ceil(double(p.size() - 1) / u.speed));
		return &u;
	};
	h.units_kill = [](World& w, int id, std::string_view) -> bool { return state::eraseUnit(w, id); };
	h.path_find = [](const World& w, int from, int to, const path::FindOpts& o, double* cost) -> std::vector<int32_t> {
		std::vector<int32_t> p = bfs(w, from, to, o.naval, std::isnan(o.maxCost) ? 400 : o.maxCost);
		if (cost) *cost = p.empty() ? path::kNoPath : double(p.size() - 1);
		return p;
	};
}

bool faked() { return stub::stubbed("units") && stub::stubbed("civ") && stub::stubbed("path"); }

// units whose time has come arrive (the Units arrival contract, simplified)
void deliver(World& w, bool all = false) {
	std::vector<int> ids;
	for (const Unit& u : w.units) {
		const auto it = u.data.find("arrive");
		if (all || it == u.data.end() || it->second <= w.year) ids.push_back(u.id);
	}
	for (const int id : ids) {
		const Unit* u = state::findUnit(w, id);
		if (!u) continue;
		const Settlement* t = state::settlement(w, u->to);
		if ((u->type == UnitType::Army || u->type == UnitType::Trader) && (!t || t->dead)) { state::eraseUnit(w, id); continue; }
		if (u->type == UnitType::Army && !society::atWar(w, u->culture, t->culture)) { state::eraseUnit(w, id); continue; }
		society::onArrive(w, id);
		state::eraseUnit(w, id);
	}
}

int armiesOf(const World& w, int cu) {
	int k = 0;
	for (const Unit& u : w.units) if (u.type == UnitType::Army && u.culture == cu) k++;
	return k;
}

// two cultures with one town each, centres `gap` hops apart
struct Duo {
	std::unique_ptr<World> w;
	int a = -1, b = -1, sa = -1, sb = -1;
};
Duo duo(int gap, int L = 5, uint32_t seed = 3, double aggrA = 0.8, double aggrB = 0.3) {
	Duo d;
	d.w = makeWorld({ .L = L, .seed = seed, .land = 0.45 });
	World& w = *d.w;
	const int c0 = bestHab(w);
	d.a = newFolk(w, aggrA);
	d.b = newFolk(w, aggrB);
	d.sa = newTown(w, c0, d.a, 1000);
	d.sb = newTown(w, landAt(w, c0, gap), d.b, 1000);
	return d;
}

} // namespace

// ================================================================ ownership

DM_TEST("diplomacy adv: writes only what the writers table allows (collaborators neutral)") {
	stub::Guard guard(true);
	if (!faked() || !stub::stubbed("society") || !stub::stubbed("life") || !stub::stubbed("climate")) return;
	auto w = makeWorld({ .L = 5, .seed = 2, .land = 0.45 });
	const int c0 = bestHab(*w);
	const int a = newFolk(*w, 0.9, Age::ATOMIC), b = newFolk(*w, 0.8, Age::ATOMIC), c = newFolk(*w, 0.5, Age::ATOMIC);
	const int a1 = newTown(*w, c0, a, 3000, 2, 2), a2 = newTown(*w, landAt(*w, c0, 7), a, 2000, 2, 2);
	const int b1 = newTown(*w, landAt(*w, c0, 6), b, 2500, 2, 2), b2 = newTown(*w, landAt(*w, c0, 12), b, 900, 2, 1);
	const int c1 = newTown(*w, landAt(*w, c0, 16), c, 800, 2, 2);
	for (const int s : { a1, a2, b1, b2, c1 }) if (!ok(s >= 0, "sites")) return;
	for (Settlement& s : w->settlements) s.b[Building::MARKET] = 1;
	w->cultures[size_t(a)].prophet = Prophet{ "Ilo", a1, 500 };
	w->tune<DiplomacyTune>().nukeP = 1;
	society::declareWar(*w, a, b);
	society::rel(*w, a, c)->v = 0.2;
	auto mk = [&](UnitType t, int cu, int from, int to) {
		Unit& u = state::newUnit(*w, t);
		u.culture = cu; u.from = from; u.to = to; u.dest = w->settlements[size_t(to)].cell; u.size = 400;
		u.path = bfs(*w, w->settlements[size_t(from)].cell, u.dest, NavalMode::Land, 99);
		return u.id;
	};
	const int army = mk(UnitType::Army, a, a1, b1), trader = mk(UnitType::Trader, a, a2, c1), prophet = mk(UnitType::Prophet, a, a1, a2);
	std::vector<uint8_t> b0;
	if (!ok(state::serialize(*w, b0), "serialize")) return;
	std::vector<std::vector<double>> cellsBefore(size_t(state::kCellFieldCount));
	for (int f = 0; f < state::kCellFieldCount; f++) {
		const CellArray arr = state::cellArray(*w, f);
		for (int k = 0; k < w->n; k++) cellsBefore[size_t(f)].push_back(arr.get(size_t(k)));
	}
	// everything Diplomacy does: three arrivals and three yearly calls (war, nukes, peace, trade attempts)
	stub::calls().clear();
	const double year0 = w->year;
	society::onArrive(*w, army);
	society::onArrive(*w, trader);
	society::onArrive(*w, prophet);
	for (int y = 1; y <= 3; y++) { w->year = year0 + y; society::diplomacy(*w); }
	ok(w->vars.lastNuke.has_value(), "the scenario reached the bomb");
	// allowed changes, patched back from the saved copy
	auto ref = state::deserialize(b0);
	if (!ok(ref != nullptr, "reload")) return;
	const int roadF = state::fieldIndex("road"), radF = state::fieldIndex("rad");
	for (int f = 0; f < state::kCellFieldCount; f++) {
		if (f == roadF || f == radF) continue;
		const CellArray arr = state::cellArray(*w, f);
		int diff = 0;
		for (int k = 0; k < w->n; k++) if (!(arr.get(size_t(k)) == cellsBefore[size_t(f)][size_t(k)])) diff++;
		eq(diff, 0, std::string("cell field untouched: ") + state::kCellFields[f].name);
	}
	int devChanged = 0;
	for (size_t k = 0; k < w->settlements.size(); k++) {
		if (w->settlements[k].devotion != ref->settlements[k].devotion) { devChanged++; eq(int(k), a2, "only the prophet's host gains devotion"); }
		w->settlements[k].devotion = ref->settlements[k].devotion;
	}
	eq(devChanged, 1, "one devotion change");
	w->road = ref->road;
	w->rad = ref->rad;
	w->vars.rel = ref->vars.rel;
	w->vars.lastNuke = ref->vars.lastNuke;
	for (auto it = w->vars.num.begin(); it != w->vars.num.end();) it = it->first.rfind("diplomacy_", 0) == 0 ? w->vars.num.erase(it) : std::next(it);
	for (const auto& kv : ref->vars.num) if (kv.first.rfind("diplomacy_", 0) == 0) w->vars.num[kv.first] = kv.second;
	for (size_t k = 0; k < w->cultures.size(); k++) {
		w->cultures[k].exhaustion = ref->cultures[k].exhaustion;
		w->cultures[k].prophet = ref->cultures[k].prophet;
	}
	w->stats.wars = ref->stats.wars;
	w->rng = ref->rng;
	w->year = ref->year;
	std::vector<uint8_t> b1;
	state::serialize(*w, b1);
	ok(b1 == b0, "nothing else in the persisted world changed (" + std::to_string(b0.size()) + " bytes compared)");
	// collaborators called: only the documented edges of the call graph (and read-only queries)
	const std::set<std::string> allowed{ "civ_capture", "civ_destroy", "civ_damage", "civ_infect", "units_spawn", "units_kill",
		"life_ignite", "climate_addDust", "society_witness", "path_find", "civ_at", "civ_near", "civ_live", "units_near", "units_cellOf" };
	std::set<std::string> seen;
	for (const auto& name : stub::calls()) seen.insert(name);
	std::string list;
	for (const auto& name : seen) { list += name + " "; ok(allowed.count(name) == 1, "unexpected collaborator call: " + name); }
	std::printf("    collaborators called: %s\n", list.c_str());
	ok(stub::called("climate_addDust") == 4 && stub::called("civ_damage") >= 4, "the bomb went through Climate and Civ");
}

// ================================================================ route cache

DM_TEST("diplomacy adv: a cached route that the ground closed is searched again (armies and caravans)") {
	stub::Guard guard(true);
	if (!faked()) return;
	fakes();
	Duo d = duo(9);
	World& w = *d.w;
	if (!ok(d.sb >= 0, "site")) return;
	society::declareWar(w, d.a, d.b);
	std::vector<double> sentAt;
	std::vector<int32_t> first;
	bool crossedLava = false;
	for (int y = 0; y <= 14; y++) {
		w.year = y;
		for (Culture& c : w.cultures) c.exhaustion = 0;
		const size_t before = w.units.size();
		society::diplomacy(w);
		for (size_t k = before; k < w.units.size(); k++) {
			const Unit& u = w.units[k];
			if (u.type != UnitType::Army || u.culture != d.a) continue;
			sentAt.push_back(y);
			if (first.empty()) first = u.path;
			for (const int c : u.path) if (blocked(w, c)) crossedLava = true;
		}
		if (y == 0 && first.size() >= 3) {
			// lava closes the middle of the cached route (and its neighbours, so the detour is real)
			const int mid = first[first.size() / 2];
			std::vector<int32_t> ring;
			w.geo->ring(mid, 1, ring);
			for (const int c : ring) if (c != first.front() && c != first.back()) w.lava[size_t(c)] = 1.0f;
		}
	}
	std::printf("    A's armies left in years %s\n", show(sentAt).c_str());
	if (!ok(sentAt.size() >= 2 && sentAt[0] == 0, "first army at year 0")) return;
	ok(sentAt[1] >= 8 && sentAt[1] <= 10, "the next army leaves on a new route by year 10 (not after the cache expires at 40)");
	ok(!crossedLava, "no army walks over lava");
	// caravans: same story
	Duo t = duo(12, 5, 3, 0.5, 0.5);
	World& x = *t.w;
	if (!ok(t.sb >= 0, "site")) return;
	x.settlements[size_t(t.sa)].tier = 2;
	x.settlements[size_t(t.sa)].b[Building::MARKET] = 1;
	x.settlements[size_t(t.sb)].tier = 1;
	std::vector<double> caravans;
	std::vector<int32_t> route;
	for (int y = 1; y <= 25; y++) {
		x.year = y;
		const size_t before = x.units.size();
		society::diplomacy(x);
		for (size_t k = before; k < x.units.size(); k++)
			if (x.units[k].type == UnitType::Trader) {
				caravans.push_back(y);
				if (route.empty()) {
					route = x.units[k].path;
					if (route.size() >= 3) {
						std::vector<int32_t> ring;
						x.geo->ring(route[route.size() / 2], 1, ring);
						for (const int c : ring) if (c != route.front() && c != route.back()) x.lava[size_t(c)] = 1.0f;
					}
				}
			}
		x.units.clear();
	}
	std::printf("    caravans left in years %s\n", show(caravans).c_str());
	ok(caravans.size() >= 3, "caravans keep going after lava cut the first route");
	if (caravans.size() >= 2) ok(caravans[1] - caravans[0] <= 8, "the second caravan found a new road within a few years");
	noNaN(w);
	noNaN(x);
}

DM_TEST("diplomacy adv: armies keep using cached routes when the year's search budget is spent") {
	stub::Guard guard(true);
	if (!faked()) return;
	fakes();
	auto w = makeWorld({ .L = 5, .seed = 4, .land = 0.45 });
	const int c0 = bestHab(*w);
	// one hub culture at war with three neighbours: six army sides, more than 4 searches
	std::vector<int> folks{ newFolk(*w, 0.9) };
	std::vector<int> towns{ newTown(*w, c0, folks[0], 3000) };
	for (int k = 0; k < 3; k++) {
		folks.push_back(newFolk(*w, 0.4));
		towns.push_back(newTown(*w, landAt(*w, c0, 6 + 4 * k), folks.back(), 2000));
	}
	for (const int s : towns) if (!ok(s >= 0, "sites")) return;
	for (int k = 1; k <= 3; k++) society::declareWar(*w, folks[0], folks[size_t(k)]);
	w->tune<DiplomacyTune>().searchesPerYear = 100;   // year 0: everything searched and cached
	w->year = 0;
	society::diplomacy(*w);
	const size_t first = w->units.size();
	w->units.clear();
	w->tune<DiplomacyTune>().searchesPerYear = 0;     // later: no search at all
	stub::calls().clear();
	int sent = 0;
	for (int y = 1; y <= 8; y++) {
		w->year = y;
		for (Culture& c : w->cultures) c.exhaustion = 0;
		const size_t before = w->units.size();
		society::diplomacy(*w);
		sent += int(w->units.size() - before);
	}
	std::printf("    year 0: %zu armies (searched); year 8 with no budget: %d armies from the cache\n", first, sent);
	eq(stub::called("path_find"), 0, "no search without budget");
	eq(size_t(sent), first, "every side whose route is cached marched again in year 8");
}

// ================================================================ trade

DM_TEST("diplomacy adv: a caravan reaching a town now at war with its culture does nothing") {
	Duo d = duo(10, 5, 3, 0.5, 0.5);
	World& w = *d.w;
	if (!ok(d.sb >= 0, "site")) return;
	Relation* r = society::rel(w, d.a, d.b);
	r->trade = 0.3;
	r->v = -0.2;
	std::fill(w.road.begin(), w.road.end(), uint8_t(0));
	Unit& u = state::newUnit(w, UnitType::Trader);
	u.culture = d.a; u.from = d.sa; u.to = d.sb; u.dest = w.settlements[size_t(d.sb)].cell;
	u.path = bfs(w, w.settlements[size_t(d.sa)].cell, u.dest, NavalMode::Land, 99);
	const int id = u.id;
	society::declareWar(w, d.a, d.b);
	const auto evs = capture(w, [&] { society::onArrive(w, id); });
	r = society::rel(w, d.a, d.b);
	near(r->trade, 0.3, 0, "no trade with the enemy");
	near(r->v, -0.2, 0, "no goodwill either");
	eq(count(evs, Ev::TradeRoute), 0, "no route");
	int roads = 0;
	for (const uint8_t v : w.road) roads += v;
	eq(roads, 0, "no road");
	// after the peace the same caravan trades
	society::makePeace(w, d.a, d.b);
	Unit& u2 = state::newUnit(w, UnitType::Trader);
	u2.culture = d.a; u2.from = d.sa; u2.to = d.sb; u2.dest = w.settlements[size_t(d.sb)].cell;
	u2.path = bfs(w, w.settlements[size_t(d.sa)].cell, u2.dest, NavalMode::Land, 99);
	const int id2 = u2.id;
	const auto evs2 = capture(w, [&] { society::onArrive(w, id2); });
	ok(society::rel(w, d.a, d.b)->trade > 0.3, "peace: trade again");
	eq(count(evs2, Ev::TradeRoute), 1, "first caravan: trade:route");
	for (const Event& e : evs2)
		if (const EvTradeRoute* tr = e.as<EvTradeRoute>()) { eq(tr->a, d.sa, "a = settlement id"); eq(tr->b, d.sb, "b = settlement id"); }
}

DM_TEST("diplomacy adv: roads rise on land, never on sea or lake cells, cap 3") {
	Duo d = duo(10, 5, 3, 0.5, 0.5);
	World& w = *d.w;
	if (!ok(d.sb >= 0, "site")) return;
	std::fill(w.road.begin(), w.road.end(), uint8_t(0));
	std::vector<int32_t> p = bfs(w, w.settlements[size_t(d.sa)].cell, w.settlements[size_t(d.sb)].cell, NavalMode::Land, 99);
	if (!ok(p.size() >= 5, "route")) return;
	const int lake = p[p.size() / 2];
	w.water[size_t(lake)] = 0.6f;
	ok(hydro::isLake(w, lake), "a lake cell on the route");
	p.push_back(-5);                  // garbage cells in a path are skipped
	p.push_back(w.n + 3);
	w.dirty.roads = false;
	for (int k = 0; k < 60; k++) {
		Unit& u = state::newUnit(w, UnitType::Trader);
		u.culture = d.a; u.from = d.sa; u.to = d.sb; u.path = p;
		const int id = u.id;
		society::onArrive(w, id);
		state::eraseUnit(w, id);
	}
	int top = 0, land = 0;
	for (const int c : p) {
		if (!w.valid(c)) continue;
		top = std::max(top, int(w.road[size_t(c)]));
		if (c != lake && !seaAt(w, c)) land += w.road[size_t(c)] > 0;
	}
	eq(int(w.road[size_t(lake)]), 0, "no road across the lake");
	eq(top, 3, "roads reach the cap of 3 and stop there");
	ok(land >= int(p.size()) - 4, "every land cell of the route has a road");
	ok(w.dirty.roads, "dirty.roads");
	int elsewhere = 0;
	for (int c = 0; c < w.n; c++) if (w.road[size_t(c)] && std::find(p.begin(), p.end(), c) == p.end()) elsewhere++;
	eq(elsewhere, 0, "roads only on the route");
}

// ================================================================ robustness

DM_TEST("diplomacy adv: NaN and garbage inputs never put NaN into the world") {
	stub::Guard guard(false);
	const bool fk = faked();
	if (fk) fakes();
	Duo d = duo(5, 5, 3, 0.9, 0.9);
	World& w = *d.w;
	if (!ok(d.sb >= 0, "site")) return;
	const double nan = std::numeric_limits<double>::quiet_NaN();
	const double inf = std::numeric_limits<double>::infinity();
	newTown(w, landAt(w, w.settlements[size_t(d.sa)].cell, 9), d.a, 500, 2, 2);
	for (Settlement& s : w.settlements) { s.b[Building::MARKET] = 1; s.tier = 2; }
	w.settlements[size_t(d.sa)].love = nan;
	w.settlements[size_t(d.sb)].devotion = inf;
	w.settlements[size_t(d.sb)].pop = nan;
	w.settlements[2].plague = nan;
	w.cultures[size_t(d.a)].traits.aggression = nan;
	w.cultures[size_t(d.b)].exhaustion = nan;
	w.cultures[size_t(d.a)].age = 99;                 // out of range ages clamp
	w.cultures[size_t(d.b)].age = -4;
	Relation* r = society::rel(w, d.a, d.b);
	r->v = nan;
	r->trade = inf;
	society::declareWar(w, d.a, d.b);
	for (int y = 1; y <= 40; y++) {
		w.year = y;
		society::diplomacy(w);
		if (fk) deliver(w, true);
		// the inputs other modules own stay garbage (Diplomacy must not choke on them)
		w.settlements[size_t(d.sa)].love = nan;
		w.cultures[size_t(d.a)].traits.aggression = nan;
	}
	// Diplomacy's own numbers are clean again
	for (const auto& kv : w.vars.rel) {
		ok(std::isfinite(kv.second.v) && std::isfinite(kv.second.trade) && std::isfinite(kv.second.truce) && std::isfinite(kv.second.since), "relation finite");
		ok(kv.second.v >= -1 && kv.second.v <= 1 && kv.second.trade >= 0 && kv.second.trade <= 1, "relation in range");
	}
	for (const Culture& c : w.cultures) ok(std::isfinite(c.exhaustion) && c.exhaustion >= 0 && c.exhaustion <= 1, "exhaustion finite, 0..1");
	for (const auto& kv : w.vars.num) ok(std::isfinite(kv.second), "vars.num finite: " + kv.first);
	for (int c = 0; c < w.n; c++) if (!std::isfinite(w.rad[size_t(c)]) || w.road[size_t(c)] > 3) { fail("rad / road"); break; }
	// units with garbage
	Unit& u = state::newUnit(w, UnitType::Army);
	u.culture = d.a; u.to = d.sb; u.from = 9999; u.size = nan;
	society::declareWar(w, d.a, d.b);
	society::onArrive(w, u.id);
	Unit& v = state::newUnit(w, UnitType::Trader);
	v.culture = 77; v.to = d.sb; v.from = -3; v.path = { -1, w.n, 5 };
	society::onArrive(w, v.id);
	Unit& p = state::newUnit(w, UnitType::Prophet);
	p.culture = d.b; p.to = 12345;
	society::onArrive(w, p.id);
	w.settlements[size_t(d.sa)].love = 0;
	w.cultures[size_t(d.a)].traits.aggression = 0.5;
	w.settlements[size_t(d.sb)].pop = 100;
	w.settlements[size_t(d.sb)].devotion = 0.5;
	w.settlements[2].plague = 0;
	for (Unit& x : w.units) if (!std::isfinite(x.size)) x.size = 0;
	eq(checkFinite(w), std::string(""), "the world is finite");
}

DM_TEST("diplomacy adv: no sea, no land, the poles, every phase, bare worlds, L 4 and L 6") {
	stub::Guard guard(false);
	const bool fk = faked();
	if (fk) fakes();
	for (const int L : { 4, 6 }) {
		// no sea: every cell is land
		{
			auto w = makeWorld({ .L = L, .seed = 5, .land = 1.0 });
			const int c0 = bestHab(*w);
			const int a = newFolk(*w, 0.9, Age::SAIL), b = newFolk(*w, 0.9, Age::SAIL);
			newTown(*w, c0, a, 2000, 2, 2);
			newTown(*w, landAt(*w, c0, 6), b, 2000, 2, 2);
			for (Settlement& s : w->settlements) s.b[Building::MARKET] = s.b[Building::PORT] = 1;
			society::rel(*w, a, b)->v = -0.9;
			for (int y = 1; y <= 30; y++) { w->year = y; society::diplomacy(*w); if (fk) deliver(*w); }
			ok(w->stats.wars >= 1, "L " + std::to_string(L) + " no sea: war still possible");
			noNaN(*w, "no sea");
		}
		// no land: cultures without a single settlement
		{
			auto w = makeWorld({ .L = L, .seed = 5, .land = 0.0 });
			newFolk(*w, 0.9);
			newFolk(*w, 0.9);
			society::rel(*w, 0, 1)->war = true;
			for (int y = 1; y <= 5; y++) { w->year = y; society::diplomacy(*w); }
			ok(!society::atWar(*w, 0, 1), "a war between empty cultures ends");
			noNaN(*w, "no land");
		}
		// the poles: towns centred on the north and south pole cells
		{
			auto w = makeWorld({ .L = L, .seed = 6, .land = 0.9 });
			int north = 0, south = 0;
			for (int c = 0; c < w->n; c++) {
				if (w->geo->pos[size_t(3 * c + 1)] > w->geo->pos[size_t(3 * north + 1)]) north = c;
				if (w->geo->pos[size_t(3 * c + 1)] < w->geo->pos[size_t(3 * south + 1)]) south = c;
			}
			for (const int c : { north, south }) { w->elev[size_t(c)] = 1; w->hab[size_t(c)] = 0.5f; }
			const int a = newFolk(*w, 0.9, Age::ATOMIC), b = newFolk(*w, 0.9, Age::ATOMIC);
			newTown(*w, north, a, 5000, 3, 3);
			newTown(*w, south, b, 5000, 3, 3);
			std::vector<int32_t> ring;
			w->geo->ring(north, 2, ring);
			if (ring.size() > 3) newTown(*w, ring.back(), b, 900, 1, 2);
			society::declareWar(*w, a, b);
			w->tune<DiplomacyTune>().nukeP = 1;
			const auto evs = capture(*w, [&] { w->year = 1; society::diplomacy(*w); });
			ok(count(evs, Ev::Nuke) >= 2, "nukes at the poles");
			noNaN(*w, "poles");
		}
		// every phase, a bare world (no geo-derived data, no cultures, then cultures without anything)
		for (const Phase ph : { Phase::Molten, Phase::Water, Phase::Life, Phase::People, Phase::History, Phase::Ended }) {
			auto w = makeWorld({ .L = L, .seed = 1, .basics = false, .phase = ph });
			society::diplomacy(*w);
			newFolk(*w, 0.5);
			society::diplomacy(*w);
			society::makePeace(*w, 0, 0);
			society::declareWar(*w, 0, 0);
			noNaN(*w, std::string("phase ") + toStr(ph));
		}
		auto bare = state::create({ .seed = 2, .L = L });
		watch(*bare);
		bare->owner.clear();                     // a broken cache must not crash it either
		newFolk(*bare, 0.5);
		newFolk(*bare, 0.5);
		state::newSettlement(*bare, 3, 0).pop = 50;
		state::newSettlement(*bare, -1, 1).pop = 50;    // a settlement without a cell
		society::rel(*bare, 0, 1)->war = true;
		society::diplomacy(*bare);
		ok(true, "bare world survived");
	}
}

// ================================================================ determinism, save / load

DM_TEST("diplomacy adv: same calls, same world (L 4 and L 6); a mid-war save/load carries on") {
	stub::Guard guard(false);
	if (!faked()) return;
	fakes();
	auto run = [](int L, uint32_t seed, int years, std::unique_ptr<World>* keep) -> uint32_t {
		auto w = makeWorld({ .L = L, .seed = seed, .land = 0.45 });
		const int c0 = bestHab(*w);
		const int a = newFolk(*w, 0.85, Age::ATOMIC), b = newFolk(*w, 0.8, Age::ATOMIC), c = newFolk(*w, 0.6, Age::IRON);
		const int step = L >= 6 ? 9 : 5;
		newTown(*w, c0, a, 3000, 2, 2);
		newTown(*w, landAt(*w, c0, step), b, 2500, 2, 2);
		newTown(*w, landAt(*w, c0, 2 * step), a, 1500, 2, 2);
		newTown(*w, landAt(*w, c0, 3 * step), c, 1200, 2, 2);
		for (Settlement& s : w->settlements) s.b[Building::MARKET] = 1;
		w->tune<DiplomacyTune>().nukeP = 0.05;
		for (int y = 0; y < years; y++) {
			w->year = y;
			society::diplomacy(*w);
			deliver(*w);
			for (Settlement& s : w->settlements) if (!s.dead) s.pop = std::max(3.0, s.pop + 0.03 * s.pop * (1 - s.pop / 3000));
		}
		noNaN(*w);
		const uint32_t h = hash(*w);
		if (keep) *keep = std::move(w);
		return h;
	};
	for (const int L : { 4, 6 }) {
		std::unique_ptr<World> w1;
		const uint32_t h1 = run(L, 4, 200, &w1), h2 = run(L, 4, 200, nullptr), h3 = run(L, 5, 200, nullptr);
		std::printf("    L %d: hashes %08x %08x (seed 5: %08x); wars %d\n", L, h1, h2, h3, w1->stats.wars);
		eq(h1, h2, "L " + std::to_string(L) + ": same calls, same hash");
		ok(h1 != h3, "another seed, another world");
		ok(w1->stats.wars >= 1, "something happened");
	}
	// save / load in the middle of a war: the loaded world keeps fighting (route cache rebuilt on demand)
	Duo d = duo(9, 5, 3, 0.9, 0.3);
	World& w = *d.w;
	society::declareWar(w, d.a, d.b);
	for (int y = 0; y < 5; y++) { w.year = y; society::diplomacy(w); }
	std::vector<uint8_t> bytes;
	if (!ok(state::serialize(w, bytes), "save")) return;
	auto l = state::deserialize(bytes);
	if (!ok(l != nullptr, "load")) return;
	watch(*l);
	ok(society::atWar(*l, d.a, d.b), "still at war after loading");
	int sentAfter = 0;
	for (int y = 5; y <= 12; y++) {
		l->year = y;
		for (Culture& c : l->cultures) c.exhaustion = 0;
		const size_t before = l->units.size();
		society::diplomacy(*l);
		sentAfter += int(l->units.size() - before);
	}
	ok(sentAfter >= 1, "armies march from the loaded world (year 8 is due)");
	noNaN(*l);
}

// ================================================================ events

DM_TEST("diplomacy adv: event payloads carry the documented ids (war, battle, nuke)") {
	stub::Guard guard(true);
	if (!faked()) return;
	fakes();
	Duo d = duo(7, 5, 3, 0.3, 0.9);       // B is the aggressor
	World& w = *d.w;
	if (!ok(d.sb >= 0, "site")) return;
	w.tune<DiplomacyTune>().noise = 0;
	society::rel(w, d.a, d.b)->v = -0.9;
	w.year = 3;
	auto evs = capture(w, [&] { society::diplomacy(w); });
	bool declared = false;
	for (const Event& e : evs)
		if (const EvWarDeclared* x = e.as<EvWarDeclared>()) { declared = true; eq(x->a, d.b, "a = the aggressor"); eq(x->b, d.a, "b = the victim"); }
	if (!ok(declared, "war:declared")) return;
	// a battle at A's town
	Unit& u = state::newUnit(w, UnitType::Army);
	u.culture = d.b; u.from = d.sb; u.to = d.sa; u.size = 5000; u.dest = w.settlements[size_t(d.sa)].cell;
	const int uid = u.id;
	evs = capture(w, [&] { society::onArrive(w, uid); });
	for (const Event& e : evs)
		if (const EvBattle* x = e.as<EvBattle>()) {
			eq(x->attacker, d.b, "battle attacker"); eq(x->defender, d.a, "battle defender"); eq(x->settlement, d.sa, "settlement");
			eq(x->cell, w.settlements[size_t(d.sa)].cell, "battle cell"); ok(x->won, "5000 soldiers win");
		}
	eq(w.settlements[size_t(d.sa)].culture, d.b, "captured");
	// A has nothing left: peace, B the victor, a and b in aggressor order
	w.year = 4;
	evs = capture(w, [&] { society::diplomacy(w); });
	bool ended = false;
	for (const Event& e : evs)
		if (const EvWarEnded* x = e.as<EvWarEnded>()) { ended = true; eq(x->a, d.b, "war:ended a = aggressor"); eq(x->b, d.a, "b"); eq(x->victor, d.b, "victor"); }
	ok(ended, "war:ended");
	// nukes: from / to are cultures, cell the struck town's
	Duo n = duo(7, 5, 3, 0.9, 0.9);
	World& x = *n.w;
	x.cultures[size_t(n.a)].age = Age::ATOMIC;
	x.cultures[size_t(n.b)].age = Age::SPACE;
	x.tune<DiplomacyTune>().nukeP = 1;
	society::declareWar(x, n.a, n.b);
	evs = capture(x, [&] { x.year = 1; society::diplomacy(x); });
	int nukes = 0;
	for (const Event& e : evs)
		if (const EvNuke* k = e.as<EvNuke>()) {
			nukes++;
			const int target = k->to == n.b ? n.sb : n.sa;
			eq(k->cell, x.settlements[size_t(target)].cell, "nuke cell = the target town");
			ok((k->from == n.a && k->to == n.b) || (k->from == n.b && k->to == n.a), "from / to are the two cultures");
		}
	eq(nukes, 2, "one town each: one strike each way");
	ok(!society::atWar(x, n.a, n.b), "peace right after");
	near(society::peek(x, n.a, n.b)->truce, 101, 1e-9, "100-year truce");
	// once per war: a new war only after the truce, and the same war never twice
	x.year = 50;
	society::rel(x, n.a, n.b)->v = -0.9;
	evs = capture(x, [&] { society::diplomacy(x); });
	eq(count(evs, Ev::WarDeclared), 0, "no war inside the nuclear truce");
}

// ================================================================ budget and cost

DM_TEST("diplomacy adv: never more than 4 searches a year even with an empty cache; L 6 worst-case cost") {
	stub::Guard guard(true);
	if (!faked()) return;
	fakes();
	auto w = makeWorld({ .L = 6, .seed = 5, .land = 0.45 });
	for (int k = 0; k < 6; k++) newFolk(*w, 0.7, Age::SAIL);
	std::vector<int32_t> tmp;
	int placed = 0;
	for (int c = 0; c < w->n && placed < 160; c += 5) {
		if (seaAt(*w, c) || w->hab[size_t(c)] < 0.15f) continue;
		w->geo->ring(c, 4, tmp);
		bool free = true;
		for (const int m : tmp) if (w->owner[size_t(m)] >= 0) { free = false; break; }
		if (!free) continue;
		const int s = newTown(*w, c, placed % 6, 1500, 3, 2);
		w->settlements[size_t(s)].b[Building::MARKET] = 1;
		w->settlements[size_t(s)].b[Building::PORT] = 1;
		placed++;
	}
	for (int a = 0; a < 6; a++) for (int b = a + 1; b < 6; b++) if ((a + b) % 2) society::declareWar(*w, a, b);
	int maxFind = 0;
	double total = 0, worst = 0;
	for (int y = 1; y <= 30; y++) {
		w->year = y;
		for (Culture& c : w->cultures) c.exhaustion = 0;
		for (auto& kv : w->vars.rel) kv.second.since = y;
		w->clearScratch();                      // no cache: every route must be searched
		stub::calls().clear();
		const double t0 = nowMs();
		society::diplomacy(*w);
		const double dt = nowMs() - t0;
		total += dt;
		worst = std::max(worst, dt);
		maxFind = std::max(maxFind, stub::called("path_find"));
		w->units.clear();
	}
	std::printf("    %d settlements, 9 wars, empty cache every year: at most %d searches a year; diplomacy() mean %.2f ms, worst %.2f ms (BFS stand-in)\n",
		placed, maxFind, total / 30, worst);
	ok(maxFind <= 4, "at most 4 path searches per year");
	ok(maxFind >= 1, "it searched");
	ok(total / 30 < 50, "within a sane budget (the real path::find is measured by the verifier at <= 3.5 ms per open-sea search)");
}

// ================================================================ edge rules

DM_TEST("diplomacy adv: double declarations, repeated peace, edicts on the aggressor, an army arriving at its own town") {
	stub::Guard guard(true);
	if (!faked()) return;
	fakes();
	Duo d = duo(6, 5, 3, 0.9, 0.2);
	World& w = *d.w;
	if (!ok(d.sb >= 0, "site")) return;
	w.year = 10;
	auto evs = capture(w, [&] { society::declareWar(w, d.a, d.b); society::declareWar(w, d.b, d.a); });
	eq(count(evs, Ev::WarDeclared), 1, "one war:declared");
	eq(w.stats.wars, 1, "counted once");
	eq(society::peek(w, d.a, d.b)->aggressor, d.a, "the first declaration names the aggressor");
	evs = capture(w, [&] { society::makePeace(w, d.a, d.b, 40, d.a); society::makePeace(w, d.a, d.b, 40, d.a); });
	eq(count(evs, Ev::WarEnded), 1, "one war:ended");
	near(society::peek(w, d.a, d.b)->truce, 50, 1e-9, "truce = year + 40");
	// the peace edict on the aggressor's side blocks it too
	w.year = 60;
	w.tune<DiplomacyTune>().noise = 0;
	society::rel(w, d.a, d.b)->v = -0.9;
	w.cultures[size_t(d.a)].edict = Edict::Peace;
	society::diplomacy(w);
	ok(!society::atWar(w, d.a, d.b), "no war while the aggressor is under a peace edict");
	w.cultures[size_t(d.a)].edict = Edict::None;
	society::rel(w, d.a, d.b)->v = -0.9;
	w.year = 61;
	society::diplomacy(w);
	ok(society::atWar(w, d.a, d.b), "edict lifted: war");
	// an army reaching a town its culture already holds stands down: no battle, nobody hurt
	w.settlements[size_t(d.sb)].culture = d.a;
	Unit& u = state::newUnit(w, UnitType::Army);
	u.culture = d.a; u.from = d.sa; u.to = d.sb; u.size = 300;
	const int uid = u.id;
	const double pop = w.settlements[size_t(d.sb)].pop;
	evs = capture(w, [&] { society::onArrive(w, uid); });
	eq(count(evs, Ev::Battle), 0, "no battle at home");
	near(w.settlements[size_t(d.sb)].pop, pop, 0, "nobody hurt");
	// a culture that is already Space-age but faces a Stone-age one: no bomb (both must be Atomic+)
	w.settlements[size_t(d.sb)].culture = d.b;
	w.cultures[size_t(d.a)].age = Age::SPACE;
	w.tune<DiplomacyTune>().nukeP = 1;
	evs = capture(w, [&] { w.year = 62; society::diplomacy(w); });
	eq(count(evs, Ev::Nuke), 0, "no bomb against a Stone-age people");
}

// ================================================================ real modules (run.ps1 all)

DM_TEST("diplomacy adv (real modules): wars, armies, battles and captures through the real Civ / Units / Path") {
	if (stub::stubbed("units") || stub::stubbed("civ") || stub::stubbed("path") || stub::stubbed("economy")) return;   // run.ps1 all only
	auto play = [](bool maps, int* warsOut, int* battlesOut, int* capturesOut, int* armiesOut) -> uint32_t {
		auto w = makeWorld({ .L = 5, .seed = 2, .land = 0.5 });
		const int c0 = civ::bestSite(*w);
		const int a = newFolk(*w, 0.95, Age::BRONZE), b = newFolk(*w, 0.9, Age::BRONZE);
		std::vector<int> sites;
		const Settlement* s0 = civ::found(*w, c0, a, 600, FoundedBy::Awaken);
		if (!s0) return 0;
		// a row of towns: A, A, B, B, B
		int prev = c0;
		for (int k = 1; k < 5; k++) {
			int site = -1;
			for (int dist = 8; dist <= 14 && site < 0; dist++) {
				const std::vector<int> dh = hopsFrom(*w, prev);
				for (int c = 0; c < w->n && site < 0; c++)
					if (dh[size_t(c)] == dist && civ::canSettle(*w, c).ok) site = c;
			}
			if (site < 0) break;
			civ::found(*w, site, k < 2 ? a : b, 600);
			prev = site;
		}
		int armies = 0, wars = 0, battles = 0, captures = 0;
		const auto tok1 = w->bus.on<EvUnitSpawned>([&](const EvUnitSpawned& e) { if (e.type == UnitType::Army) armies++; });
		const auto tok2 = w->bus.on<EvWarDeclared>([&](const EvWarDeclared&) { wars++; });
		const auto tok3 = w->bus.on<EvBattle>([&](const EvBattle&) { battles++; });
		const auto tok4 = w->bus.on<EvSettlementCaptured>([&](const EvSettlementCaptured&) { captures++; });
		for (int y = 1; y <= 300; y++) {
			for (int q = 0; q < 4; q++) { w->year += 0.25; units::step(*w, 0.25); }
			civ::yearly(*w);
			society::yearly(*w);       // ends with society::diplomacy
			if (maps && y == 150) mi::owner(*w, shot("ue-diplomacy-adv-real-150.png"));
		}
		if (maps) mi::owner(*w, shot("ue-diplomacy-adv-real-300.png"));
		w->bus.off(tok1); w->bus.off(tok2); w->bus.off(tok3); w->bus.off(tok4);
		noNaN(*w, "300 real years");
		*warsOut = wars; *battlesOut = battles; *capturesOut = captures; *armiesOut = armies;
		return hash(*w);
	};
	int wars = 0, battles = 0, captures = 0, armies = 0, w2 = 0, b2 = 0, c2 = 0, a2 = 0;
	const uint32_t h1 = play(true, &wars, &battles, &captures, &armies);
	const uint32_t h2 = play(false, &w2, &b2, &c2, &a2);
	std::printf("    real modules, 300 years: %d wars, %d armies, %d battles, %d captures; hash %08x / %08x\n", wars, armies, battles, captures, h1, h2);
	eq(h1, h2, "deterministic with the real modules");
	ok(wars >= 1, "aggressive neighbours go to war");
	ok(armies >= 1 && battles >= 1, "armies march and fight");
}
