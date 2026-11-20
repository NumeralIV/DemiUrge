// DEMIURGE — Sim/Disasters.cpp : the disasters half of DM.Powers (SPEC-SIM 3.10 disasters.js).
// Owner: disasters engineer (A12, key "disasters"). Declarations: the [disasters] half of Powers.h.
//
//   start    the only creator of w.disasters (state::newDisaster): comet, meteor, volcano, tsunami,
//            storm, drought, with the documented field meanings and their start events.
//   update   real time: comet / meteor flights count down by realDt only; impacts (crater, sea level,
//            destruction, fires or a tsunami, dust, meteor:impact); the rate-limit clock.
//   tick     sim time (kTick years per call): eruptions and lava, tsunami fronts, storms, droughts;
//            a disaster is removed when t >= dur.
//   natural  the yearly roll for natural events with the protection rules.
//
// Writers: lava and w.disasters (owner), wet (raised by storms and waves), fert (ash, flood silt),
// seaLevel (comet impacts, before history only), farm / urban cleared under lava (SPEC-SIM 3.10
// "destroying farms and urban cells"; reported deviation from the writers table). Everything else
// goes through its owner: elev via terrain::crater / cone / roughen / addElev / touched, fires via
// life::ignite, people via civ::damage / destroy / infect, units via units::near / kill, dust via
// climate::addDust, belief via society::witness.
//
// Determinism: every random draw is from w.rng; disasters are processed in list (= ascending id)
// order, settlements in the order the Civ queries return (nearest first / ascending id), cells in
// fixed orders; no clock. Scratch (never saved): the real-time clock, per-wave rate-limit times and
// each tsunami's over-sea distance order (rebuilt on demand after a load).
#include "Powers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "Civ.h"
#include "Climate.h"
#include "Life.h"
#include "Sim.h"
#include "Society.h"
#include "Terrain.h"
#include "Units.h"

namespace dm {
namespace {

// One tsunami's sea cells in order of their over-sea angular distance from the source.
struct WaveOrder {
	std::vector<std::pair<float, int32_t>> order;   // (distance, cell), ascending
	size_t next = 0;                                 // first entry the front has not reached yet
	std::vector<int> hitSettlements;                 // settlements already struck by this wave
};

struct DisastersScratch {
	double clock = 0;                                // real seconds advanced by update()
	std::map<int, double> lastHit;                   // tsunami id -> clock of its last tsunami:hit
	std::map<int, WaveOrder> waves;                  // tsunami id -> distance order
	std::vector<int32_t> stamp;                      // per-cell marks (generation counter)
	int32_t gen = 0;
	std::vector<int32_t> cellsA, cellsB;             // within() results
	std::vector<int32_t> shore;                      // natural storms: coastal land cells
	std::vector<std::pair<int32_t, int32_t>> bfs;    // (cell, hops)
	std::vector<float> delta;                        // lava flow deltas
	std::vector<float> dist;                         // Dijkstra scratch
};

DisastersScratch& scr(const World& w) {
	DisastersScratch& S = w.scratch<DisastersScratch>();
	if (S.stamp.size() != size_t(w.n)) {
		S.stamp.assign(size_t(w.n), 0);
		S.gen = 0;
		S.delta.assign(size_t(w.n), 0.0f);
		S.dist.assign(size_t(w.n), 0.0f);
	}
	return S;
}

int32_t newGen(DisastersScratch& S) {
	if (S.gen >= std::numeric_limits<int32_t>::max() - 1) {
		std::fill(S.stamp.begin(), S.stamp.end(), 0);
		S.gen = 0;
	}
	return ++S.gen;
}

bool usable(const World& w) {
	return w.geo && w.n > 0 && w.geo->n == w.n && w.elev.size() == size_t(w.n) && w.lava.size() == size_t(w.n);
}

double pickOpt(const std::optional<double>& v, double def) {
	return (v && isFinite(*v)) ? *v : def;
}

// The caller's data bag without non-finite entries (entities never hold NaN / infinity).
NumBag finiteData(const NumBag& in) {
	NumBag out;
	for (const auto& kv : in) if (isFinite(kv.second)) out.insert(kv);
	return out;
}

// Smooth falloff 1 at the centre to 0 at ang.
double falloff(double a, double ang) {
	if (!(ang > 0)) return a <= 0 ? 1.0 : 0.0;
	const double t = sat(a / ang);
	const double u = 1 - t * t;
	return u * u;
}

bool isLand(const World& w, int c) { return !state::isSea(w, c); }

// Unit tangent vector at cell c pointing toward cell m (zero-safe: an arbitrary tangent).
Vec3 tangentToward(const World& w, int c, int m) {
	const Vec3 p = w.geo->dir(c), q = w.geo->dir(m);
	Vec3 t = q - p * v3::dot(p, q);
	if (v3::len(t) < 1e-9) {
		Vec3 e, nn;
		v3::basis(p, e, nn);
		return e;
	}
	return v3::norm(t);
}

const powers::PowerDef* defOf(const char* id) { return powers::byId(id); }

// Natural events witness with a quarter of the faith and good 0 (SPEC-SIM 3.10).
void witnessNatural(World& w, int cell, const char* kind) {
	const powers::PowerDef* d = defOf(kind);
	if (!d || !w.valid(cell)) return;
	society::WitnessOpts o;
	o.kind = kind;
	o.faith = d->faith * w.tune<PowersTune>().naturalFaith;
	o.good = 0;
	o.natural = true;                    // (v3) not a deed of the god: no memory, not 'heard'
	society::witness(w, cell, d->wAng, o);
}

// Kill every unit within ang of cell.
void killUnits(World& w, int cell, double ang, const char* cause) {
	const std::vector<int> ids = units::near(w, cell, ang);
	for (int uid : ids) units::kill(w, uid, cause);
}

// Live settlement ids (ascending) and the protection rule for a natural event centred on c.
bool protectedCell(const World& w, int c, const std::vector<int>& liveIds) {
	const DisastersTune& T = w.tune<DisastersTune>();
	const bool young = w.stats.pop < T.protectPop || w.year < T.protectYear;
	const bool few = int(liveIds.size()) < T.protectTowns;
	const double r = young ? T.protectAng : (few ? T.protectAngFew : 0.0);
	if (!(r > 0)) return false;
	for (int sid : liveIds) {
		if (sid < 0 || size_t(sid) >= w.settlements.size()) continue;
		const Settlement& s = w.settlements[size_t(sid)];
		if (s.dead || !w.valid(s.cell)) continue;
		if (w.geo->angle(c, s.cell) <= r) return true;
	}
	return false;
}

// ------------------------------------------------------------------ tsunami distance order

// Dijkstra over sea cells from the source (a land source seeds from the sea cells within 2 hops),
// bounded by reach. Ties on (distance, cell).
void buildWave(const World& w, int src, double reach, WaveOrder& out) {
	out.order.clear();
	out.next = 0;
	DisastersScratch& S = scr(w);
	const Geo& g = *w.geo;
	const int32_t mark = newGen(S);
	using Item = std::pair<float, int32_t>;
	std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
	auto seed = [&](int c, float dd) {
		if (S.stamp[size_t(c)] != mark || dd < S.dist[size_t(c)]) {
			S.stamp[size_t(c)] = mark;
			S.dist[size_t(c)] = dd;
			pq.push({ dd, int32_t(c) });
		}
	};
	if (state::isSea(w, src)) {
		seed(src, 0.0f);
	} else {
		g.ring(src, 2, S.cellsA);
		for (int32_t m : S.cellsA)
			if (state::isSea(w, m)) seed(m, float(g.angle(src, m)));
	}
	const float lim = float(reach);
	while (!pq.empty()) {
		const Item it = pq.top();
		pq.pop();
		const int c = it.second;
		if (S.stamp[size_t(c)] != mark || it.first > S.dist[size_t(c)]) continue;
		if (it.first > lim) break;
		out.order.push_back(it);
		S.dist[size_t(c)] = -1.0f;   // finalized
		const int deg = g.nbrN[size_t(c)];
		for (int k = 0; k < deg; k++) {
			const int m = g.nbr[size_t(6 * c + k)];
			if (m < 0 || !state::isSea(w, m)) continue;
			if (S.stamp[size_t(m)] == mark && S.dist[size_t(m)] < 0) continue;   // finalized
			const float nd = it.first + float(g.angle(c, m));
			if (nd > lim) continue;
			if (S.stamp[size_t(m)] != mark || nd < S.dist[size_t(m)]) {
				S.stamp[size_t(m)] = mark;
				S.dist[size_t(m)] = nd;
				pq.push({ nd, int32_t(m) });
			}
		}
	}
	std::sort(out.order.begin(), out.order.end());
}

WaveOrder& waveOf(World& w, const Disaster& d) {
	DisastersScratch& S = scr(w);
	auto it = S.waves.find(d.id);
	if (it != S.waves.end()) return it->second;
	WaveOrder& wo = S.waves[d.id];
	buildWave(w, d.cell, d.ang, wo);
	// after a load: the part of the wave already behind the front is done
	while (wo.next < wo.order.size() && double(wo.order[wo.next].first) <= d.front && d.front > 0) wo.next++;
	return wo;
}

// ------------------------------------------------------------------ impacts

void impactComet(World& w, int id) {
	const Disaster* d = state::findDisaster(w, id);
	if (!d) return;
	const int c = d->cell;
	const double ang = d->ang;
	state::eraseDisaster(w, id);
	if (!w.valid(c)) return;
	const DisastersTune& T = w.tune<DisastersTune>();
	const bool sea = state::isSea(w, c);
	terrain::crater(w, c, ang, T.cometDepth);
	if (w.phase < Phase::History) {
		double s = w.seaLevel;
		if (!(s >= T.cometFirstSea)) s = T.cometFirstSea;   // the first comet (or a NaN guard)
		else s += clamp(T.cometK * (T.cometRef - s), T.cometMin, T.cometMax);
		// hard cap +1.5, but a comet only ever adds water: a sea already above the cap stays where it is
		const double cur = isFinite(w.seaLevel) ? w.seaLevel : s;
		w.seaLevel = std::max(cur, std::min(s, T.seaCap));
		w.dirty.water = true;
		w.dirty.color = true;
		w.dirty.objects = true;
	}
	terrain::touched(w);
	w.bus.emit(EvMeteorImpact{ c, ang, ImpactKind::Comet, sea });
}

void impactMeteor(World& w, int id) {
	const Disaster* d = state::findDisaster(w, id);
	if (!d) return;
	const int c = d->cell;
	const double ang = d->ang;
	const bool nat = d->natural;
	const bool wantWitness = d->data.count("witness") && d->data.at("witness") > 0;
	state::eraseDisaster(w, id);
	if (!w.valid(c)) return;
	const DisastersTune& T = w.tune<DisastersTune>();
	const Geo& g = *w.geo;
	const bool sea = state::isSea(w, c);
	const double outer = ang * T.meteorOuter;
	// the impact first, then its consequences (settlement:destroyed, unit:died, fire:start, tsunami:start)
	w.bus.emit(EvMeteorImpact{ c, ang, ImpactKind::Meteor, sea });
	// people
	std::vector<int> harmed;
	const std::vector<int> hit = civ::near(w, c, outer);
	// (v3, design review finding 3) the god's star on a capital or an academy sets research back
	if (!nat && wantWitness) civ::strikeKnowledge(w, hit, w.tune<PowersTune>().setbackMeteor);
	for (int sid : hit) {
		if (sid < 0 || size_t(sid) >= w.settlements.size() || w.settlements[size_t(sid)].dead) continue;
		const double a = g.angle(c, w.settlements[size_t(sid)].cell);
		if (a <= ang) civ::destroy(w, sid, Cause::Meteor);
		else civ::damage(w, sid, T.meteorOuterLoss, Cause::Meteor);
		harmed.push_back(sid);
	}
	killUnits(w, c, ang * T.meteorUnitKill, "meteor");
	// the ground
	// (v3, design review finding 5) Star-fall: from the Space age the god's star digs deeper (a lake)
	const double depth = !nat && powers::upgrade(w, "meteor") ? T.meteorDepth * w.tune<PowersTune>().starfallDepth : T.meteorDepth;
	terrain::crater(w, c, ang, depth);
	climate::addDust(w, T.meteorDust);
	if (!sea) {
		DisastersScratch& S = scr(w);
		g.within(c, ang * T.meteorFireOut, S.cellsA);
		const std::vector<int32_t> cells = S.cellsA;   // ignite may not touch our scratch, but be safe
		const double span = std::max(1e-9, ang * (T.meteorFireOut - 1));
		for (int32_t m : cells) {
			if (!isLand(w, m)) continue;
			const double a = g.angle(c, m);
			const double p = a <= ang ? T.meteorFireIn : T.meteorFireRim * (1 - sat((a - ang) / span));
			if (w.rng.chance(p)) life::ignite(w, m, 1);
		}
	} else {
		powers::StartOpts o;
		o.power = T.meteorWave;
		o.ang = T.meteorWaveAng;
		o.natural = nat;
		powers::start(w, DisasterType::Tsunami, c, o);
	}
	if (nat || wantWitness) {
		const powers::PowerDef* def = defOf("meteor");
		if (def) {
			society::WitnessOpts o;
			o.kind = "meteor";
			if (nat) { o.faith = def->faith * w.tune<PowersTune>().naturalFaith; o.good = 0; o.natural = true; }
			else { o.faith = def->faith; o.good = 0; o.harmed = harmed; o.goodHarmed = -1; }
			society::witness(w, c, def->wAng, o);
		}
	}
}

// ------------------------------------------------------------------ tick pieces (true = finished)

bool tickVolcano(World& w, int id, double dt) {
	Disaster* d = state::findDisaster(w, id);
	if (!d) return true;
	const DisastersTune& T = w.tune<DisastersTune>();
	DisastersScratch& S = scr(w);
	const Geo& g = *w.geo;
	const int vent = d->cell;
	d->t += dt;
	const double dur = std::max(dt, d->dur);
	const double t = d->t;
	const double erupt = sat(T.volcanoErupt) * dur;
	const double t0 = 0.4 * dur;
	double pw = 0;
	if (t <= erupt) pw = t <= t0 ? 1.0 : 1.0 - 0.7 * sat((t - t0) / std::max(1e-9, erupt - t0));
	d->power = pw;
	const bool ending = t >= dur;
	std::vector<int32_t> field;              // work on a copy (valid, distinct cells); written back below
	field.reserve(d->cells.size() + 8);
	const int32_t inField = newGen(S);
	for (int32_t c : d->cells)
		if (w.valid(c) && S.stamp[size_t(c)] != inField) { S.stamp[size_t(c)] = inField; field.push_back(c); }
	if (!w.valid(vent)) { d->cells.clear(); return true; }
	auto addCell = [&](int32_t c) {
		if (S.stamp[size_t(c)] == inField) return true;
		if (int(field.size()) >= T.lavaMaxCells) return false;
		S.stamp[size_t(c)] = inField;
		field.push_back(c);
		return true;
	};
	std::vector<float>& lava = w.lava;
	// previous depth (before this tick's emission and flow), to find cells that just turned hot
	if (pw > 0) addCell(int32_t(vent));
	std::vector<float> before;
	before.reserve(field.size() + 16);
	for (int32_t c : field) before.push_back(lava[size_t(c)]);
	if (pw > 0) {
		lava[size_t(vent)] = float(std::min(T.lavaMax, double(lava[size_t(vent)]) + T.lavaRate * pw * dt));
		climate::addDust(w, T.volcanoDust * dt);
	}
	// flow: each cell sends lava to its lowest neighbour (by surface elev + lava)
	const size_t n0 = field.size();
	for (size_t i = 0; i < n0; i++) {
		const int32_t c = field[i];
		const double L = lava[size_t(c)];
		if (L < T.lavaMin) continue;
		const double h = double(w.elev[size_t(c)]) + L;
		int best = -1;
		double bh = h;
		const int deg = g.nbrN[size_t(c)];
		for (int k = 0; k < deg; k++) {
			const int m = g.nbr[size_t(6 * c + k)];
			if (m < 0) continue;
			const double hm = double(w.elev[size_t(m)]) + double(lava[size_t(m)]);
			if (hm < bh - 1e-9 || (std::fabs(hm - bh) <= 1e-9 && best >= 0 && m < best)) { bh = hm; best = m; }
		}
		if (best < 0 || bh >= h - 0.01) continue;
		if (!addCell(int32_t(best))) continue;   // the field is full: lava piles up
		const double mv = std::min(L - 0.5 * T.lavaMin, 0.5 * (h - bh) * T.lavaFlow);
		if (!(mv > 0)) continue;
		S.delta[size_t(c)] -= float(mv);
		S.delta[size_t(best)] += float(mv);
	}
	for (int32_t c : field) {
		const float v = lava[size_t(c)] + S.delta[size_t(c)];
		lava[size_t(c)] = float(clamp(double(v), 0.0, T.lavaMax));
		S.delta[size_t(c)] = 0.0f;
	}
	while (before.size() < field.size()) before.push_back(0.0f);
	// effects of lava that just turned hot
	std::map<int, int> newCells;   // settlement id -> newly covered territory cells
	std::vector<int> centres;      // settlements whose centre cell was covered
	for (size_t i = 0; i < field.size(); i++) {
		const int32_t c = field[i];
		if (!(lava[size_t(c)] >= T.lavaHot) || before[i] >= T.lavaHot) continue;
		if (!isLand(w, c)) continue;
		life::ignite(w, c, 1);
		w.farm[size_t(c)] = 0.0f;
		w.urban[size_t(c)] = 0.0f;
		const int sid = civ::at(w, c);
		if (sid >= 0 && size_t(sid) < w.settlements.size()) {
			if (w.settlements[size_t(sid)].cell == c) centres.push_back(sid);
			else newCells[sid]++;
		}
	}
	for (int sid : centres) {
		if (!w.settlements[size_t(sid)].dead) civ::destroy(w, sid, Cause::Lava);
		newCells.erase(sid);
	}
	for (const auto& kv : newCells) {
		if (w.settlements[size_t(kv.first)].dead) continue;
		civ::damage(w, kv.first, std::min(0.5, T.lavaDamage * kv.second), Cause::Lava);
	}
	// cooling into rock (via Terrain)
	bool newLand = false;
	std::vector<int32_t> keep;
	keep.reserve(field.size());
	for (int32_t c : field) {
		const double L = lava[size_t(c)];
		if (!(L > 0)) { lava[size_t(c)] = 0.0f; continue; }
		const bool wasSea = state::isSea(w, c);
		double rate = wasSea ? T.coolSea : (t <= erupt ? T.coolLand : T.coolTail);
		double amount = L * sat(rate);
		if (ending || L - amount < 1e-3) amount = L;
		if (amount > 0) {
			terrain::addElev(w, c, amount * T.rockPerLava);
			lava[size_t(c)] = float(std::max(0.0, L - amount));
			if (wasSea && !state::isSea(w, c)) newLand = true;
		}
		if (lava[size_t(c)] > 0) keep.push_back(c);
	}
	d = state::findDisaster(w, id);   // collaborators never create disasters, but stay safe
	if (!d) return true;
	d->cells = keep;
	w.dirty.color = true;
	w.dirty.elev = true;
	const int64_t ticksIn = int64_t(std::llround(d->t / dt));
	if (newLand || ending || T.touchEvery <= 1 || ticksIn % T.touchEvery == 0) terrain::touched(w);
	if (!ending) return false;
	// the end: ash fertility, volcano:end
	const double R = std::max(g.step, d->ang * T.ashAngMul);
	g.within(vent, R, S.cellsB);
	for (int32_t m : S.cellsB) {
		if (!isLand(w, m)) continue;
		const double a = g.angle(vent, m);
		const double add = T.ashFert * (1 - 0.5 * sat(a / R) * sat(a / R));
		w.fert[size_t(m)] = float(clamp(double(w.fert[size_t(m)]) + add, 0.0, 1.5));
	}
	d->cells.clear();
	terrain::touched(w);
	w.bus.emit(EvVolcanoEnd{ vent, id });
	return true;
}

bool tickTsunami(World& w, int id, double dt) {
	Disaster* d = state::findDisaster(w, id);
	if (!d) return true;
	const DisastersTune& T = w.tune<DisastersTune>();
	DisastersScratch& S = scr(w);
	const Geo& g = *w.geo;
	d->t += dt;
	WaveOrder& wo = waveOf(w, *d);
	const double reach = std::max(0.0, d->ang);
	const double front = std::min(reach, d->front + T.tsunamiSpeed);
	d->front = front;
	const double power = std::max(0.0, d->power);
	const bool natural = d->natural;
	const bool done = front >= reach || d->t >= d->dur + 1e-9 || wo.next >= wo.order.size();
	const double sea = w.seaLevel;
	const int32_t mark = newGen(S);
	int firstHit = -1;
	double firstH = 0;
	std::map<int, double> centreDepth;   // settlement id -> depth over its centre
	std::map<int, int> ownedFlooded;     // settlement id -> flooded territory cells
	std::vector<int32_t> flooded;        // land cells the water reached this tick
	while (wo.next < wo.order.size() && double(wo.order[wo.next].first) <= front) {
		const double dist = wo.order[wo.next].first;
		const int s = wo.order[wo.next].second;
		wo.next++;
		const double h = power * (1 - T.tsunamiDecay * sat(reach > 0 ? dist / reach : 1));
		if (!(h > 0.05)) continue;
		const int maxHops = int(clamp(std::floor(h * T.tsunamiHops), 1.0, 5.0));
		const int deg = g.nbrN[size_t(s)];
		for (int k = 0; k < deg; k++) {
			const int m0 = g.nbr[size_t(6 * s + k)];
			if (m0 < 0 || state::isSea(w, m0)) continue;
			// flood inland from this coastal cell through low ground. A cell keeps the highest water level
			// that reached it this tick (S.dist under `mark`); a higher level reaching it later is passed on.
			if (S.stamp[size_t(m0)] == mark && double(S.dist[size_t(m0)]) >= h - 1e-4) continue;
			S.bfs.clear();
			S.bfs.push_back({ int32_t(m0), 1 });
			S.stamp[size_t(m0)] = mark;
			S.dist[size_t(m0)] = float(h);
			for (size_t qi = 0; qi < S.bfs.size(); qi++) {
				const int c = S.bfs[qi].first;
				const int hops = S.bfs[qi].second;
				const double local = h * (1 - double(hops - 1) / double(maxHops + 1));
				if (double(S.dist[size_t(c)]) > local + 1e-4) continue;   // a higher level got here meanwhile
				const double above = double(w.elev[size_t(c)]) - sea;
				if (!(above < local)) continue;   // too high: stays dry, does not pass the water on
				const double depth = local - std::max(0.0, above);
				flooded.push_back(c);
				if (w.wet[size_t(c)] < 1.0f)   // silt once per soaking, not once per front passing by
					w.fert[size_t(c)] = float(clamp(double(w.fert[size_t(c)]) + T.floodFert, 0.0, 1.5));
				w.wet[size_t(c)] = 1.0f;
				if (firstHit < 0) { firstHit = c; firstH = local; }
				const int o = w.owner[size_t(c)];
				if (o >= 0 && size_t(o) < w.settlements.size() && !w.settlements[size_t(o)].dead) {
					if (w.settlements[size_t(o)].cell == c) centreDepth[o] = std::max(centreDepth[o], depth);
					else ownedFlooded[o]++;
				}
				const int sid = civ::at(w, c);   // a centre on unowned ground
				if (sid >= 0 && sid != o && w.settlements[size_t(sid)].cell == c) centreDepth[sid] = std::max(centreDepth[sid], depth);
				if (hops >= maxHops) continue;
				const double nextLocal = h * (1 - double(hops) / double(maxHops + 1));
				const int dg = g.nbrN[size_t(c)];
				for (int j = 0; j < dg; j++) {
					const int m = g.nbr[size_t(6 * c + j)];
					if (m < 0 || state::isSea(w, m)) continue;
					if (S.stamp[size_t(m)] == mark && double(S.dist[size_t(m)]) >= nextLocal - 1e-4) continue;
					S.stamp[size_t(m)] = mark;
					S.dist[size_t(m)] = float(nextLocal);
					S.bfs.push_back({ int32_t(m), int32_t(hops + 1) });
				}
			}
		}
	}
	if (firstHit >= 0) {
		w.dirty.color = true;
		// units on flooded land
		const int32_t fmark = newGen(S);
		for (int32_t c : flooded) S.stamp[size_t(c)] = fmark;
		std::vector<int> dead;
		for (const Unit& u : w.units) {
			const int uc = units::cellOf(w, u);
			if (w.valid(uc) && S.stamp[size_t(uc)] == fmark) dead.push_back(u.id);
		}
		for (int uid : dead) units::kill(w, uid, "wave");
		// settlements, once per wave
		auto struck = [&](int sid) {
			std::vector<int>& hs = wo.hitSettlements;
			if (std::find(hs.begin(), hs.end(), sid) != hs.end()) return true;
			hs.push_back(sid);
			return false;
		};
		for (const auto& kv : centreDepth) {
			if (w.settlements[size_t(kv.first)].dead || struck(kv.first)) continue;
			if (kv.second >= T.waveOverwhelm) civ::destroy(w, kv.first, Cause::Wave);
			else civ::damage(w, kv.first, clamp(T.waveLoss + T.waveLossK * kv.second, 0.0, 0.9), Cause::Wave);
		}
		for (const auto& kv : ownedFlooded) {
			if (centreDepth.count(kv.first) || w.settlements[size_t(kv.first)].dead || struck(kv.first)) continue;
			civ::damage(w, kv.first, std::min(0.2, 0.02 * kv.second), Cause::Wave);
		}
		// tsunami:hit, rate-limited per wave in real time
		auto it = S.lastHit.find(id);
		if (it == S.lastHit.end() || S.clock - it->second >= T.hitEvery) {
			S.lastHit[id] = S.clock;
			w.bus.emit(EvTsunamiHit{ firstHit, firstH, natural });
		}
	}
	return done;
}

bool tickStorm(World& w, int id, double dt) {
	Disaster* d = state::findDisaster(w, id);
	if (!d) return true;
	const DisastersTune& T = w.tune<DisastersTune>();
	DisastersScratch& S = scr(w);
	const Geo& g = *w.geo;
	d->t += dt;
	int c = d->cell;
	Vec3 H{ d->data.count("hx") ? d->data["hx"] : 0.0, d->data.count("hy") ? d->data["hy"] : 0.0, d->data.count("hz") ? d->data["hz"] : 0.0 };
	// drift: at most one hop per tick, along the heading
	{
		const Vec3 p = g.dir(c);
		if (!isFinite(H.x) || !isFinite(H.y) || !isFinite(H.z)) H = Vec3{ 0, 0, 0 };   // a broken stored heading
		H = H - p * v3::dot(H, p);
		if (v3::len(H) < 1e-9) { Vec3 e, nn; v3::basis(p, e, nn); H = e; }
		H = v3::norm(v3::rotAxis(v3::norm(H), p, w.rng.gauss() * T.stormWobble));
		if (w.rng.chance(T.stormMove)) {
			int best = -1;
			double bd = 0;
			const int deg = g.nbrN[size_t(c)];
			for (int k = 0; k < deg; k++) {
				const int m = g.nbr[size_t(6 * c + k)];
				if (m < 0) continue;
				const double s = v3::dot(tangentToward(w, c, m), H);
				if (best < 0 || s > bd) { bd = s; best = m; }
			}
			// a natural storm honours the protection rules all its life, not only where it formed: it turns
			// back at the edge of a protected zone instead of drifting onto a young settlement
			if (best >= 0 && bd > 0 && d->natural) {
				const std::vector<int> liveIds = civ::live(w);
				if (protectedCell(w, best, liveIds) && !protectedCell(w, c, liveIds)) { H = H * -1.0; best = -1; }
				d = state::findDisaster(w, id);
				if (!d) return true;
			}
			if (best >= 0 && bd > 0) {
				c = best;
				const Vec3 q = g.dir(c);
				H = H - q * v3::dot(H, q);
				if (v3::len(H) > 1e-9) H = v3::norm(H);
			}
		}
	}
	d->cell = c;
	d->data["hx"] = H.x;
	d->data["hy"] = H.y;
	d->data["hz"] = H.z;
	const bool overLand = isLand(w, c);
	d->power = std::max(0.0, d->power * (1 - (overLand ? T.stormLandDecay : T.stormSeaDecay)));
	const double pw = d->power;
	const double ang = d->ang;
	// rain
	g.within(c, ang, S.cellsA);
	bool wetted = false;
	for (int32_t m : S.cellsA) {
		if (!isLand(w, m)) continue;
		const double target = T.stormWet * pw * falloff(g.angle(c, m), ang);
		if (target > double(w.wet[size_t(m)])) { w.wet[size_t(m)] = float(sat(target)); wetted = true; }
	}
	if (wetted) { w.dirty.color = true; w.need.climate = true; }
	// minor damage under the eye, once per storm per settlement
	const std::vector<int> under = civ::near(w, c, 0.5 * ang);
	for (int sid : under) {
		const std::string key = "hit" + std::to_string(sid);
		Disaster* dd = state::findDisaster(w, id);
		if (!dd || dd->data.count(key)) continue;
		dd->data[key] = 1;
		civ::damage(w, sid, T.stormLoss * pw, Cause::Wave);
	}
	d = state::findDisaster(w, id);
	if (!d) return true;
	if (d->t >= d->dur || d->power < T.stormMinPower) {
		w.need.climate = true;
		w.bus.emit(EvStorm{ d->cell, d->ang, false });
		return true;
	}
	return false;
}

bool tickDrought(World& w, int id, double dt) {
	Disaster* d = state::findDisaster(w, id);
	if (!d) return true;
	d->t += dt;
	if (d->t < d->dur) return false;
	w.need.climate = true;
	w.bus.emit(EvDrought{ d->cell, d->ang, false });
	return true;
}

// ------------------------------------------------------------------ natural events

// A random cell satisfying pred and not protected; prefer(c) picks among the candidates seen (first
// preferred one wins, else the first acceptable one). -1 if none in naturalTries draws.
int sampleCell(World& w, const std::vector<int>& liveIds, const std::function<bool(int)>& pred,
               const std::function<bool(int)>& prefer = nullptr) {
	const DisastersTune& T = w.tune<DisastersTune>();
	int fallback = -1;
	for (int i = 0; i < T.naturalTries; i++) {
		const int c = w.rng.pickIndex(w.n);
		if (c < 0 || !pred(c) || protectedCell(w, c, liveIds)) continue;
		if (!prefer || prefer(c)) return c;
		if (fallback < 0) fallback = c;
	}
	return fallback;
}

void naturalLightning(World& w, const std::vector<int>& liveIds) {
	const PowersTune& P = w.tune<PowersTune>();
	const int c = sampleCell(w, liveIds, [&](int m) { return isLand(w, m); },
	                         [&](int m) { return w.veg[size_t(m)] >= 0.25f; });
	if (c < 0) return;
	const Geo& g = *w.geo;
	w.bus.emit(EvLightning{ c, true });
	life::ignite(w, c, 1);
	const std::vector<int> hit = civ::near(w, c, 1.5 * g.step);
	for (int sid : hit) civ::damage(w, sid, w.rng.range(P.lightningLo, P.lightningHi), Cause::Lightning);
	killUnits(w, c, 0.6 * g.step, "lightning");
	witnessNatural(w, c, "lightning");
}

void naturalStorm(World& w, const std::vector<int>& liveIds) {
	const DisastersTune& T = w.tune<DisastersTune>();
	const Geo& g = *w.geo;
	DisastersScratch& S = scr(w);
	// landfall candidates: land within 2 hops of the sea. Sampled from this list, not from all cells: at
	// L = 6 coastal land is only ~7 % of the planet, and random cells found one in time for half the
	// storms. Both the landfall cell and the formation cell must be unprotected.
	std::vector<int32_t>& shore = S.shore;
	shore.clear();
	for (int c = 0; c < w.n; c++)
		if (isLand(w, c) && w.coast[size_t(c)] <= 2) shore.push_back(int32_t(c));
	if (shore.empty()) return;
	for (int tries = 0; tries < T.naturalTries; tries++) {
		const int L = shore[size_t(w.rng.pickIndex(int(shore.size())))];
		if (protectedCell(w, L, liveIds)) continue;
		// form out at sea: the farthest sea cell within stormOffset of that coast
		g.within(L, T.stormOffset, S.cellsA);
		int o = -1;
		double best = -1;
		for (int32_t m : S.cellsA) {
			if (!state::isSea(w, m)) continue;
			const double a = g.angle(L, m);
			if (a > best + 1e-12) { best = a; o = m; }
		}
		if (o < 0 || protectedCell(w, o, liveIds)) continue;
		const Vec3 h = tangentToward(w, o, L);
		powers::StartOpts so;
		so.natural = true;
		so.data["hx"] = h.x;
		so.data["hy"] = h.y;
		so.data["hz"] = h.z;
		powers::start(w, DisasterType::Storm, o, so);
		return;
	}
}

void naturalDrought(World& w, const std::vector<int>& liveIds) {
	const int c = sampleCell(w, liveIds, [&](int m) { return isLand(w, m); });
	if (c < 0) return;
	powers::StartOpts so;
	so.natural = true;
	powers::start(w, DisasterType::Drought, c, so);
}

void naturalQuake(World& w, const std::vector<int>& liveIds) {
	const DisastersTune& T = w.tune<DisastersTune>();
	const PowersTune& P = w.tune<PowersTune>();
	const Geo& g = *w.geo;
	DisastersScratch& S = scr(w);
	const bool wantSea = w.rng.chance(T.quakeSeaShare);
	auto nearLand = [&](int m) {
		g.within(m, 0.12, S.cellsB);
		for (int32_t k : S.cellsB) if (isLand(w, k)) return true;
		return false;
	};
	int c = -1;
	if (wantSea) c = sampleCell(w, liveIds, [&](int m) { return state::isSea(w, m) && nearLand(m); });
	if (c < 0) c = sampleCell(w, liveIds, [&](int m) { return isLand(w, m); });
	if (c < 0) return;
	const double pw = w.rng.range(0.5, 1.0);
	const double ang = T.quakeAng;
	const bool sea = state::isSea(w, c);
	w.bus.emit(EvQuake{ c, ang, pw, true });
	const std::vector<int> hit = civ::near(w, c, ang);
	for (int sid : hit) civ::damage(w, sid, w.rng.range(P.quakeLo, P.quakeHi), Cause::Quake);
	killUnits(w, c, ang, "quake");
	terrain::roughen(w, c, ang, T.quakeRough * pw, w.rng);
	if (sea) {
		powers::StartOpts so;
		so.natural = true;
		so.power = T.quakeWave * pw;
		powers::start(w, DisasterType::Tsunami, c, so);
	}
	witnessNatural(w, c, "quake");
}

void naturalPlague(World& w, const std::vector<int>& liveIds) {
	std::vector<int> cities;
	for (int sid : liveIds) {
		if (sid < 0 || size_t(sid) >= w.settlements.size()) continue;
		const Settlement& s = w.settlements[size_t(sid)];
		if (s.dead || s.tier < 3 || !w.valid(s.cell) || protectedCell(w, s.cell, liveIds)) continue;
		cities.push_back(sid);
	}
	if (cities.empty()) return;
	const int sid = cities[size_t(w.rng.pickIndex(int(cities.size())))];
	const int cell = w.settlements[size_t(sid)].cell;
	// an immune city shrugs it off: no plague, nothing to witness
	if (civ::infect(w, sid, w.tune<PowersTune>().plagueSeverity)) witnessNatural(w, cell, "plague");
}

void naturalEruption(World& w, const std::vector<int>& liveIds) {
	const DisastersTune& T = w.tune<DisastersTune>();
	const int c = sampleCell(w, liveIds, [&](int m) { return double(w.elev[size_t(m)]) > w.seaLevel - T.eruptSeaDepth; },
	                         [&](int m) { return isLand(w, m); });
	if (c < 0) return;
	powers::StartOpts so;
	so.natural = true;
	if (powers::start(w, DisasterType::Volcano, c, so)) witnessNatural(w, c, "volcano");
}

// Number of events this year for a per-century rate. A jittered schedule, not an independent yearly
// chance: each type accumulates its expected events per year (p = perCentury / 100 x calamity) in
// vars.num["disasters_acc_<k>"] and fires whenever the sum passes a threshold drawn from 0.5..1.5
// (vars.num["disasters_thr_<k>"]), which is then subtracted. The long-run rate is exactly p, follows
// calamity changes at once, and the gaps vary by +-50 % instead of being exponential, so a 5,000-year
// count of a rare event (eruptions: 20) stays within a few events of the table instead of swinging
// +-45 % with the seed. No draws while p = 0; one draw per event (and one for the first threshold).
int rollCount(World& w, const char* k, double perCentury, double calamity) {
	const double p = perCentury / 100.0 * calamity;
	if (!(p > 0) || !isFinite(p)) return 0;
	const std::string accKey = std::string("disasters_acc_") + k, thrKey = std::string("disasters_thr_") + k;
	NumBag& num = w.vars.num;
	auto acc = num.find(accKey);
	double a = (acc != num.end() && isFinite(acc->second) && acc->second >= 0) ? acc->second : 0.0;
	auto thr = num.find(thrKey);
	double t = (thr != num.end() && isFinite(thr->second) && thr->second >= 0.5 && thr->second <= 1.5) ? thr->second : w.rng.range(0.5, 1.5);
	a += std::min(p, 50.0);
	int n = 0;
	while (a >= t && n < 50) {
		a -= t;
		n++;
		t = w.rng.range(0.5, 1.5);
	}
	if (n >= 50) a = 0;
	num[accKey] = a;
	num[thrKey] = t;
	return n;
}

} // namespace

namespace powers {

Disaster* start(World& w, DisasterType type, int cell, const StartOpts& opts) {
	if (!usable(w) || !w.valid(cell)) return nullptr;
	const DisastersTune& T = w.tune<DisastersTune>();
	const double maxAng = kPi;
	auto angOr = [&](double def) { return clamp(pickOpt(opts.ang, def), 0.0, maxAng); };
	switch (type) {
	case DisasterType::Comet:
	case DisasterType::Meteor: {
		const bool comet = type == DisasterType::Comet;
		if (comet) {
			int inFlight = 0;
			for (const Disaster& e : w.disasters) if (e.type == DisasterType::Comet) inFlight++;
			if (inFlight >= T.maxComets) return nullptr;
		}
		const double eta = std::max(0.0, pickOpt(opts.eta, comet ? T.cometFlight : T.meteorFlight));
		Disaster& d = state::newDisaster(w, type, cell);
		d.ang = angOr(comet ? T.cometAng : T.meteorAng);
		d.power = std::max(0.0, pickOpt(opts.power, 1.0));
		d.dur = 0;
		d.eta = eta;
		d.flight = eta;
		d.natural = opts.natural;
		d.data = finiteData(opts.data);
		const int id = d.id;
		w.bus.emit(EvMeteorLaunched{ cell, eta, comet ? ImpactKind::Comet : ImpactKind::Meteor, id });
		return state::findDisaster(w, id);
	}
	case DisasterType::Volcano: {
		Disaster& d = state::newDisaster(w, type, cell);
		d.ang = angOr(T.volcanoAng);
		d.dur = std::max(sim::kTick, pickOpt(opts.dur, T.volcanoYears));
		d.power = 1;
		d.natural = opts.natural;
		d.data = finiteData(opts.data);
		d.cells = { int32_t(cell) };
		const int id = d.id;
		const double ang = d.ang;
		const bool noCone = d.data.count("noCone") && d.data.at("noCone") > 0;
		const double height = T.volcanoHeight * std::max(0.0, pickOpt(opts.power, 1.0));
		if (!noCone && height > 0) terrain::cone(w, cell, ang, height);
		w.dirty.color = true;
		w.bus.emit(EvVolcanoStart{ cell, id, opts.natural });
		return state::findDisaster(w, id);
	}
	case DisasterType::Tsunami: {
		Disaster& d = state::newDisaster(w, type, cell);
		d.ang = angOr(T.tsunamiAng);
		d.power = std::max(0.0, pickOpt(opts.power, T.tsunamiHeight));
		d.front = 0;
		const double spd = std::max(1e-6, T.tsunamiSpeed);
		d.dur = pickOpt(opts.dur, (std::ceil(d.ang / spd) + 1) * sim::kTick);
		d.natural = opts.natural;
		d.data = finiteData(opts.data);
		const int id = d.id;
		{
			DisastersScratch& S = scr(w);
			WaveOrder& wo = S.waves[id];
			buildWave(w, cell, d.ang, wo);
		}
		w.bus.emit(EvTsunamiStart{ cell });
		return state::findDisaster(w, id);
	}
	case DisasterType::Storm: {
		const double dur = pickOpt(opts.dur, w.rng.range(T.stormMinYears, T.stormMaxYears));
		Disaster& d = state::newDisaster(w, type, cell);
		d.ang = angOr(T.stormAng);
		d.power = sat(pickOpt(opts.power, T.stormPower));
		d.dur = std::max(sim::kTick, dur);
		d.natural = opts.natural;
		d.data = finiteData(opts.data);
		const int id = d.id;
		const double ang = d.ang;
		const bool hasHeading = d.data.count("hx") && d.data.count("hy") && d.data.count("hz");
		if (!hasHeading) {
			// toward the nearest land cell within 0.5 rad, else a random direction
			const Geo& g = *w.geo;
			DisastersScratch& S = scr(w);
			g.within(cell, 0.5, S.cellsA);
			int best = -1;
			double ba = 0;
			for (int32_t m : S.cellsA) {
				if (m == cell || !isLand(w, m)) continue;
				const double a = g.angle(cell, m);
				if (best < 0 || a < ba - 1e-12) { ba = a; best = m; }
			}
			Vec3 h;
			if (best >= 0) h = tangentToward(w, cell, best);
			else {
				Vec3 e, nn;
				v3::basis(g.dir(cell), e, nn);
				h = v3::rotAxis(e, g.dir(cell), w.rng.range(0, kTau));
			}
			Disaster* dp = state::findDisaster(w, id);
			dp->data["hx"] = h.x;
			dp->data["hy"] = h.y;
			dp->data["hz"] = h.z;
		}
		w.need.climate = true;
		w.bus.emit(EvStorm{ cell, ang, true });
		return state::findDisaster(w, id);
	}
	case DisasterType::Drought: {
		const double ang = clamp(pickOpt(opts.ang, w.rng.range(T.droughtAng, T.droughtAngMax)), 0.0, maxAng);
		const double pw = sat(pickOpt(opts.power, w.rng.range(T.droughtPowerMin, T.droughtPowerMax)));
		const double dur = pickOpt(opts.dur, w.rng.range(T.droughtMinYears, T.droughtMaxYears));
		Disaster& d = state::newDisaster(w, type, cell);
		d.ang = ang;
		d.power = pw;
		d.dur = std::max(sim::kTick, dur);
		d.natural = opts.natural;
		d.data = finiteData(opts.data);
		const int id = d.id;
		w.need.climate = true;
		w.bus.emit(EvDrought{ cell, ang, true });
		return state::findDisaster(w, id);
	}
	default:
		return nullptr;
	}
}

void update(World& w, double realDt) {
	if (!usable(w)) return;
	if (!(realDt > 0) || !isFinite(realDt)) return;
	DisastersScratch& S = scr(w);
	S.clock += realDt;
	std::vector<std::pair<int, bool>> landed;   // (id, comet)
	for (Disaster& d : w.disasters) {
		if (d.type != DisasterType::Comet && d.type != DisasterType::Meteor) continue;
		if (!isFinite(d.eta)) d.eta = 0;
		d.eta = std::max(0.0, d.eta - realDt);
		if (d.eta <= 0) landed.push_back({ d.id, d.type == DisasterType::Comet });
	}
	for (const auto& l : landed) {
		if (l.second) impactComet(w, l.first);
		else impactMeteor(w, l.first);
	}
}

void tick(World& w) {
	if (!usable(w) || w.disasters.empty()) return;
	DisastersScratch& S = scr(w);
	const double dt = sim::kTick;
	std::vector<int> ids;
	ids.reserve(w.disasters.size());
	for (const Disaster& d : w.disasters) ids.push_back(d.id);
	for (int id : ids) {
		const Disaster* d = state::findDisaster(w, id);
		if (!d) continue;
		bool finished = false;
		if (!w.valid(d->cell) && d->type != DisasterType::Comet && d->type != DisasterType::Meteor) {
			finished = true;   // a broken record (bad cell) is dropped without effects
		} else switch (d->type) {
		case DisasterType::Comet:
		case DisasterType::Meteor: continue;   // real time: update()
		case DisasterType::Volcano: finished = tickVolcano(w, id, dt); break;
		case DisasterType::Tsunami: finished = tickTsunami(w, id, dt); break;
		case DisasterType::Storm: finished = tickStorm(w, id, dt); break;
		case DisasterType::Drought: finished = tickDrought(w, id, dt); break;
		default: finished = true; break;
		}
		if (finished) {
			state::eraseDisaster(w, id);
			S.waves.erase(id);
			S.lastHit.erase(id);
		}
	}
}

void natural(World& w) {
	if (!usable(w)) return;
	if (w.phase != Phase::History && w.phase != Phase::Ended) return;
	const DisastersTune& T = w.tune<DisastersTune>();
	// (v3.1, design review finding 13) the young world is busier, the old one quieter
	const double ageMul = T.calamityAge[size_t(std::clamp(w.age, 0, 6))];
	const double cal = w.settings.calamity * (isFinite(ageMul) ? std::max(0.0, ageMul) : 1.0);
	if (!(cal > 0) || !isFinite(cal)) return;
	const std::vector<int> liveIds = civ::live(w);
	// (v3, design review finding 4) the first trial: a dry spell over the first people's camp in
	// year trialYear (inside the usual protection), so the first prayer (for rain) comes early
	if (T.trialYear > 0 && w.year + 1e-9 >= T.trialYear && !w.vars.num.count("disasters_trial")) {
		w.vars.num["disasters_trial"] = w.year;
		int cap = -1;
		for (const Culture& cu : w.cultures)
			if (!cu.dead && cu.capital >= 0 && state::settlement(w, cu.capital) && !w.settlements[size_t(cu.capital)].dead) { cap = cu.capital; break; }
		if (cap >= 0) {
			powers::StartOpts so;
			so.natural = true;
			so.power = T.trialPower;
			so.ang = T.trialAng;
			so.dur = T.trialYears;
			powers::start(w, DisasterType::Drought, w.settlements[size_t(cap)].cell, so);
		}
	}
	const int nL = rollCount(w, "lightning", T.lightningPerCentury, cal);
	const int nS = rollCount(w, "storm", T.stormsPerCentury, cal);
	const int nD = rollCount(w, "drought", T.droughtsPerCentury, cal);
	const int nQ = rollCount(w, "quake", T.quakesPerCentury, cal);
	const int nP = rollCount(w, "plague", T.plaguesPerCentury, cal);
	const int nE = rollCount(w, "eruption", T.eruptionsPerCentury, cal);
	for (int i = 0; i < nL; i++) naturalLightning(w, liveIds);
	for (int i = 0; i < nS; i++) naturalStorm(w, liveIds);
	for (int i = 0; i < nD; i++) naturalDrought(w, liveIds);
	for (int i = 0; i < nQ; i++) naturalQuake(w, civ::live(w));
	for (int i = 0; i < nP; i++) naturalPlague(w, civ::live(w));
	for (int i = 0; i < nE; i++) naturalEruption(w, civ::live(w));
}

} // namespace powers
} // namespace dm
