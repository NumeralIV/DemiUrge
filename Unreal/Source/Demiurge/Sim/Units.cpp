// DEMIURGE — Sim/Units.cpp : settlers, traders, armies, prophets on the move (SPEC-SIM 3.9, DM.Units).
// Owner: A6 path-units.
//
// Movement. A unit is between path[i] and path[i + 1] at fraction t (0..1); its current cell is the nearer
// end of that step, and `sea` says whether it is a sea cell. `speed` is in open-land steps per year. A step
// a -> b takes: min(slowMax, path::stepCost(a, b)) open-land steps between two land cells (slopes, forest,
// ice and lakes slow, roads help), 1 / seaMul between two sea cells, shoreTime to embark or land.
// Per unit and step, in this order: an army or trader whose target settlement is dead, or an army whose
// culture is no longer at war with its target's culture, dies 'disbanded'; a unit on lava > lavaKill dies
// 'lava'; then it moves, dying 'lava' if it enters such a cell. A unit that has not yet entered its next cell
// (t < 0.5) and finds it impassable re-paths once in its life from where it stands (data "units_repathed");
// a second block, or no route, kills it ('lost'). A unit already more than half way into a cell that became
// impassable finishes that step.
// Arrival (Units.h contract): unit:arrived, then settler -> join `to` when it is a live settlement of its
// culture, else civ::found(dest, culture, size, Settlers, origin's devotion / love), else join the nearest
// live settlement of its culture (ties: lower id), else die 'lost'; trader / army / prophet ->
// society::onArrive while still in w.units. Finally the unit is erased if still present (no unit:died).
// No rng is used. Units never hold a Unit& across a call into another module or an emit (vectors move).
#include "Units.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Civ.h"
#include "Path.h"
#include "Society.h"

namespace dm {
namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr const char* kRepathKey = "units_repathed";

struct UnitsScratch {
	std::vector<int> ids;          // the units being stepped
	bool busy = false;             // a nested step (from a handler) uses its own list
};

inline bool usable(const World& w) { return w.geo && w.n > 0 && w.geo->n == w.n && w.elev.size() == size_t(w.n); }

double typeSpeed(const UnitsTune& T, UnitType t) {
	switch (t) {
	case UnitType::Settler: return T.speedSettler;
	case UnitType::Trader: return T.speedTrader;
	case UnitType::Army: return T.speedArmy;
	case UnitType::Prophet: return T.speedProphet;
	default: return 0;
	}
}

// index of the unit in w.units (ascending ids: binary search, linear fallback), -1 if absent
int findIdx(const World& w, int id) {
	const auto it = std::lower_bound(w.units.begin(), w.units.end(), id, [](const Unit& u, int x) { return u.id < x; });
	if (it != w.units.end() && it->id == id) return int(it - w.units.begin());
	for (size_t k = 0; k < w.units.size(); k++)
		if (w.units[k].id == id) return int(k);
	return -1;
}

int currentCell(const World& w, const Unit& u) {
	const size_t len = u.path.size();
	if (len == 0) return w.valid(u.dest) ? u.dest : -1;
	const int i = u.i < 0 ? 0 : u.i;
	if (size_t(i) + 1 >= len) {
		const int c = u.path[len - 1];
		return w.valid(c) ? c : -1;
	}
	const int a = u.path[size_t(i)], b = u.path[size_t(i) + 1];
	const int c = u.t >= 0.5 ? b : a;
	if (w.valid(c)) return c;
	return w.valid(a) ? a : w.valid(b) ? b : -1;
}

inline bool seaCell(const World& w, int c) { return w.valid(c) && state::isSea(w, c); }

// time a -> b takes, in open-land steps; kInf when the step is impassable for this unit
double segTime(const World& w, int a, int b, NavalMode naval, const UnitsTune& T) {
	const double sc = path::stepCost(w, a, b, naval);
	if (!(sc < kInf)) return kInf;
	const bool sa = state::isSea(w, a), sb = state::isSea(w, b);
	double t;
	if (sa && sb) t = T.seaMul > 0 ? 1 / T.seaMul : 1;
	else if (sa || sb) t = T.shoreTime;
	else t = sc < T.slowMax ? sc : T.slowMax;
	return t >= 0 && t < kInf ? t : 1;
}

// nearest live settlement of a culture to a cell (ties: lower id), -1 if none
int nearestOwn(const World& w, int cell, int culture) {
	int best = -1;
	double bd = kInf;
	for (const Settlement& s : w.settlements) {
		if (s.dead || s.culture != culture || !w.valid(s.cell)) continue;
		const double d = w.geo->angle(cell, s.cell);
		if (d < bd) { bd = d; best = s.id; }
	}
	return best;
}

void join(World& w, int sid, double size) {
	Settlement* s = state::settlement(w, sid);
	if (!s) return;
	const double add = isFinite(size) && size > 0 ? size : 0;
	s->pop = (isFinite(s->pop) && s->pop > 0 ? s->pop : 0) + add;
}

// The one re-path in a unit's life, from the cell it stands on (path[i]; it has not entered path[i + 1]).
// A unit part-way along the blocked step turns round on the spot, so its position never jumps.
bool repath(World& w, Unit& u, const UnitsTune& T) {
	const auto it = u.data.find(kRepathKey);
	if (it != u.data.end() && it->second != 0) return false;
	if (!w.valid(u.dest) || u.i < 0 || size_t(u.i) + 1 >= u.path.size()) return false;
	const int here = u.path[size_t(u.i)], ahead = u.path[size_t(u.i) + 1];
	if (!w.valid(here)) return false;
	// budget: UnitsTune::maxCost, or twice what the rest of the old route costs (its passable steps) when that is
	// more, so a unit sent on a long journey (spawn maxCost above the default) can still take a detour
	double rest = 0;
	for (size_t k = size_t(u.i) + 1; k < u.path.size(); k++) {
		const double sc = path::stepCost(w, u.path[k - 1], u.path[k], u.naval);
		if (sc < kInf) rest += sc;
	}
	path::FindOpts fo;
	fo.naval = u.naval;
	fo.maxCost = T.maxCost > 2 * rest ? T.maxCost : 2 * rest;
	std::vector<int32_t> p = path::find(w, here, u.dest, fo);
	if (p.empty()) return false;
	u.data[kRepathKey] = 1;
	if (u.t > 0 && w.valid(ahead)) {
		p.insert(p.begin(), ahead);
		u.t = 1 - u.t;
	} else {
		u.t = 0;
	}
	u.path = std::move(p);
	u.i = 0;
	return true;
}

// A unit reached the last cell of its path.
void arrive(World& w, int id) {
	int k = findIdx(w, id);
	if (k < 0) return;
	UnitType type;
	int culture, from, to, cell;
	double size;
	{
		Unit& u = w.units[size_t(k)];
		u.i = int(u.path.size()) - 1;
		u.t = 0;
		cell = u.path.back();
		u.sea = seaCell(w, cell);
		type = u.type;
		culture = u.culture;
		from = u.from;
		to = u.to;
		size = u.size;
	}
	w.bus.emit(EvUnitArrived{ id, type, culture, cell });
	if (type == UnitType::Settler) {
		bool home = false;
		const Settlement* target = to >= 0 ? state::settlement(w, to) : nullptr;
		if (target && !target->dead && target->culture == culture) {
			join(w, to, size);
			home = true;
		} else {
			civ::FoundOpts fo;
			const Settlement* origin = state::settlement(w, from);
			if (origin && isFinite(origin->devotion) && isFinite(origin->love)) {
				fo.devotion = origin->devotion;
				fo.love = origin->love;
			}
			const Settlement* s = civ::found(w, cell, culture, size, FoundedBy::Settlers, fo);
			if (s) {
				home = true;
			} else {
				const int sid = nearestOwn(w, cell, culture);
				if (sid >= 0) { join(w, sid, size); home = true; }
			}
		}
		if (!home) { units::kill(w, id, "lost"); return; }
	} else {
		society::onArrive(w, id);
	}
	state::eraseUnit(w, id);   // whoever received it may already have removed it (a lost battle)
}

void advance(World& w, int id, double dt) {
	const UnitsTune& T = w.tune<UnitsTune>();
	const int k = findIdx(w, id);
	if (k < 0) return;
	Unit& u = w.units[size_t(k)];
	if (u.path.empty() || !w.valid(u.path.back())) { units::kill(w, id, "lost"); return; }
	// orders that no longer stand (atWar is a read: no entity is created, u stays valid)
	if ((u.type == UnitType::Army || u.type == UnitType::Trader) && u.to >= 0) {
		const Settlement* target = state::settlement(w, u.to);
		if (!target || target->dead) { units::kill(w, id, "disbanded"); return; }
		if (u.type == UnitType::Army && !society::atWar(w, u.culture, target->culture)) {
			units::kill(w, id, "disbanded");
			return;
		}
	}
	const int last = int(u.path.size()) - 1;
	if (!(u.i >= 0)) u.i = 0;
	if (u.i > last) u.i = last;
	if (!(u.t >= 0 && u.t < 1)) u.t = 0;
	int cur = currentCell(w, u);
	if (cur >= 0 && double(w.lava[size_t(cur)]) > T.lavaKill) { units::kill(w, id, "lava"); return; }

	double speed = u.speed;
	if (!(speed > 0) || !isFinite(speed)) speed = typeSpeed(T, u.type) > 0 ? typeSpeed(T, u.type) : 5;
	double left = dt * speed;                                  // budget, in open-land steps
	for (;;) {
		if (size_t(u.i) + 1 >= u.path.size()) { arrive(w, id); return; }   // u is not used after this
		const int a = u.path[size_t(u.i)], b = u.path[size_t(u.i) + 1];
		double wt = (w.valid(a) && w.valid(b)) ? segTime(w, a, b, u.naval, T) : kInf;
		if (!(wt < kInf)) {
			if (u.t < 0.5) {                                   // the cell ahead has become impassable
				if (!repath(w, u, T)) { units::kill(w, id, "lost"); return; }
				continue;
			}
			wt = 1;                                            // already inside it: finish the step
		}
		const double need = (1 - u.t) * wt;
		if (left >= need) {
			left -= need;
			u.i++;
			u.t = 0;
			if (w.valid(b) && double(w.lava[size_t(b)]) > T.lavaKill) {
				u.sea = seaCell(w, b);
				units::kill(w, id, "lava");
				return;
			}
		} else {
			u.t += left / wt;
			if (!(u.t < 1)) u.t = 0.999999;
			break;
		}
	}
	cur = currentCell(w, u);
	u.sea = seaCell(w, cur);
}

} // namespace

namespace units {

Unit* spawn(World& w, const SpawnOpts& o) {
	if (!usable(w)) return nullptr;
	const UnitsTune& T = w.tune<UnitsTune>();
	if (int(w.units.size()) >= T.maxUnits) return nullptr;
	const double speed = typeSpeed(T, o.type);
	if (!(speed > 0) || !isFinite(speed)) return nullptr;
	const Settlement* origin = state::settlement(w, o.from);
	if (!origin || origin->dead || !w.valid(origin->cell)) return nullptr;
	const Settlement* target = nullptr;
	if (o.to != -1) {
		target = state::settlement(w, o.to);
		if (!target) return nullptr;
	}
	if (o.culture != -1 && !state::culture(w, o.culture)) return nullptr;
	const int start = origin->cell;
	const int dest = w.valid(o.dest) ? o.dest : (o.dest < 0 && target && w.valid(target->cell)) ? target->cell : -1;
	if (dest < 0) return nullptr;

	std::vector<int32_t> route;
	if (!o.path.empty()) {
		if (o.path.front() != start || o.path.back() != dest) return nullptr;
		if (!(path::cost(w, o.path, o.naval) < kInf)) return nullptr;
		route = o.path;
	} else {
		path::FindOpts fo;
		fo.naval = o.naval;
		fo.maxCost = std::isnan(o.maxCost) ? T.maxCost : o.maxCost;
		route = path::find(w, start, dest, fo);
	}
	if (route.empty()) return nullptr;

	Unit& u = state::newUnit(w, o.type);
	const int id = u.id;
	u.culture = o.culture;
	u.from = o.from;
	u.to = o.to;
	u.dest = dest;
	u.path = std::move(route);
	u.i = 0;
	u.t = 0;
	u.speed = speed;
	u.size = isFinite(o.size) && o.size > 0 ? o.size : 0;
	u.naval = o.naval;
	u.sea = seaCell(w, start);
	u.data = o.data;
	w.bus.emit(EvUnitSpawned{ id, o.type, o.culture, start });
	return state::findUnit(w, id);
}

void step(World& w, double dtYears) {
	if (!usable(w) || !(dtYears > 0) || !isFinite(dtYears) || w.units.empty()) return;
	UnitsScratch& S = w.scratch<UnitsScratch>();
	std::vector<int> local;
	const bool outer = S.busy;
	std::vector<int>& ids = outer ? local : S.ids;
	S.busy = true;
	ids.clear();
	for (const Unit& u : w.units) ids.push_back(u.id);
	for (size_t k = 0; k < ids.size(); k++) advance(w, ids[k], dtYears);
	S.busy = outer;
}

bool kill(World& w, int unitId, std::string_view cause) {
	const int k = findIdx(w, unitId);
	if (k < 0) return false;
	const Unit& u = w.units[size_t(k)];
	const int cell = currentCell(w, u);
	const UnitType type = u.type;
	const int culture = u.culture;
	w.units.erase(w.units.begin() + k);
	w.bus.emit(EvUnitDied{ unitId, type, culture, cell, cause.empty() ? std::string("lost") : std::string(cause) });
	return true;
}

std::vector<int> near(const World& w, int cell, double ang) {
	std::vector<int> ids;
	if (!usable(w) || !w.valid(cell) || !(ang >= 0)) return ids;
	for (const Unit& u : w.units) {
		const int c = currentCell(w, u);
		if (c >= 0 && w.geo->angle(cell, c) <= ang) ids.push_back(u.id);
	}
	std::sort(ids.begin(), ids.end());
	return ids;
}

int cellOf(const World& w, const Unit& u) {
	if (!usable(w)) return -1;
	return currentCell(w, u);
}

int dirOf(const World& w, const Unit& u, Vec3& out) {
	out = Vec3{ 0, 1, 0 };
	if (!usable(w)) return -1;
	const int cur = currentCell(w, u);
	if (cur < 0) return -1;
	const size_t len = u.path.size();
	const int i = u.i < 0 ? 0 : u.i;
	int a = cur, b = -1;
	double t = 0;
	if (len > 0 && size_t(i) + 1 < len) {
		a = u.path[size_t(i)];
		b = u.path[size_t(i) + 1];
		t = u.t;
	}
	if (!w.valid(a) || !w.valid(b) || !(t > 0)) {
		out = v3::norm(w.geo->dir(cur));   // exactly unit length in double (pos is float)
		return cur;
	}
	if (t > 1) t = 1;
	const Vec3 pa = w.geo->dir(a), pb = w.geo->dir(b);
	const Vec3 p = pa + (pb - pa) * t;
	const double l = v3::len(p);
	out = l > 1e-12 ? p / l : pa;
	return cur;
}

} // namespace units
} // namespace dm
