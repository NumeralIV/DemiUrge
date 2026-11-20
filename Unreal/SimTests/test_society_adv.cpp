// DEMIURGE — SimTests/test_society_adv.cpp : adversarial verification of the belief half of
// dm::society (A9; SPEC-SIM 3.8 society.js, BUILD.md A9). Written by the A9 verifier.
//
// Tries to break what test_society.cpp does not: capitals and the Ark across schisms (with a
// Civ-like electCapital), NaN / bad ids / empty and odd worlds / every phase, determinism at L = 4
// and 6, ownership (yearly + witness write only what the writers table gives Society), prophets
// whose nearest goal is unreachable, prayer bookkeeping over time, the Ark with two Space cultures,
// brush event pacing, the cost at L = 6 with real territories, and long-run maps.
// Maps: shots/ue-society-adv-*.png.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "Civ.h"
#include "MapImg.h"
#include "Powers.h"
#include "Society.h"
#include "State.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

namespace mi = dm::mapimg;

const double kNaN = std::numeric_limits<double>::quiet_NaN();

std::string f3(double x, int d = 3) { char b[64]; std::snprintf(b, sizeof b, "%.*f", d, x); return b; }
void say(const std::string& s) { std::printf("  %s\n", s.c_str()); std::fflush(stdout); }

bool landCell(const World& w, int c) { return w.elev[size_t(c)] >= w.seaLevel; }

int newCult(World& w, int parent = -1, BirthReason why = BirthReason::Awaken) {
	const Culture* c = society::newCulture(w, { parent, -1, why });
	return c ? c->id : -1;
}

int town(World& w, int cell, int cid, double pop, double devotion = 0.5, double love = 0) {
	Settlement& s = state::newSettlement(w, cell, cid);
	s.pop = pop;
	s.devotion = devotion;
	s.love = love;
	s.name = "T" + std::to_string(s.id);
	const int id = s.id;
	if (Culture* c = state::culture(w, cid)) if (c->capital < 0) { c->capital = id; w.settlements[size_t(id)].capital = true; }
	return id;
}

// land cell whose angle from `from` is closest to `ang`, at least minSep from every cell in avoid
int landNear(const World& w, int from, double ang, const std::vector<int>& avoid = {}, double minSep = 0) {
	int best = -1;
	double bd = 1e9;
	for (int c = 0; c < w.n; c++) {
		if (!landCell(w, c)) continue;
		bool bad = false;
		for (const int a : avoid) if (w.geo->angle(a, c) < minSep) { bad = true; break; }
		if (bad) continue;
		const double d = std::fabs(w.geo->angle(from, c) - ang);
		if (d < bd) { bd = d; best = c; }
	}
	return best;
}

template <class T> std::vector<T> evOf(const std::vector<Event>& evs) {
	std::vector<T> out;
	for (const Event& e : evs) if (const T* p = e.as<T>()) out.push_back(*p);
	return out;
}

// Civ-like collaborators for stubbed runs (the real Civ semantics: capitals are sticky; electCapital
// moves the capital to the most populous settlement and restarts the launch site and Ark).
void civLike() {
	if (!stub::stubbed("civ")) return;
	stub::hooks().civ_electCapital = [](World& ww, int cid) -> int {
		Culture* cu = state::culture(ww, cid);
		if (!cu) return -1;
		int best = -1;
		double bp = 0;
		for (const Settlement& s : ww.settlements) {
			if (s.dead || s.culture != cid) continue;
			if (best < 0 || s.pop > bp) { best = s.id; bp = s.pop; }
		}
		if (cu->capital != best) {
			if (cu->capital >= 0 && size_t(cu->capital) < ww.settlements.size()) {
				ww.settlements[size_t(cu->capital)].b[Building::LAUNCH] = 0;
				ww.settlements[size_t(cu->capital)].ark = 0;
			}
			if (best >= 0) { ww.settlements[size_t(best)].b[Building::LAUNCH] = 0; ww.settlements[size_t(best)].ark = 0; }
		}
		cu->capital = best;
		for (Settlement& s : ww.settlements) if (s.culture == cid) s.capital = (s.id == best);
		return best;
	};
	stub::hooks().civ_setCulture = [](World& ww, int sid, int cid) {
		Settlement& s = ww.settlements[size_t(sid)];
		const int from = s.culture;
		s.culture = cid;
		s.capital = false;
		s.b[Building::LAUNCH] = 0;
		s.ark = 0;
		Culture* fc = state::culture(ww, from);
		if (fc && fc->capital == sid) { fc->capital = -1; stub::hooks().civ_electCapital(ww, from); }
		const Culture* tc = state::culture(ww, cid);
		const Settlement* tcap = tc ? state::settlement(ww, tc->capital) : nullptr;
		if (!tcap || tcap->dead || tcap->culture != cid) stub::hooks().civ_electCapital(ww, cid);
		ww.bus.emit(EvSettlementCaptured{ sid, from, cid, CaptureHow::Schism });
	};
}

// Units::spawn that creates the unit (no movement)
void spawnLike(std::vector<units::SpawnOpts>* log = nullptr, std::function<bool(const units::SpawnOpts&)> refuse = nullptr) {
	if (!stub::stubbed("units")) return;
	stub::hooks().units_spawn = [log, refuse](World& ww, const units::SpawnOpts& o) -> Unit* {
		if (log) log->push_back(o);
		if (refuse && refuse(o)) return nullptr;
		Unit& u = state::newUnit(ww, o.type);
		u.culture = o.culture; u.from = o.from; u.to = o.to; u.dest = o.dest; u.size = o.size;
		return &u;
	};
}

} // namespace

// ================================================================ capitals and the Ark across schisms

DM_TEST("adv society: a schism never moves a still-valid parent capital nor resets its Ark") {
	stub::Guard guard(true);
	civLike();
	auto w = makeWorld({ .L = 5, .seed = 1 });
	const int cid = newCult(*w);
	const int capCell = bestHab(*w);
	const int cap = town(*w, capCell, cid, 600, 0.5, 0);
	// a much bigger town near the capital: the most populous, but the capital is sticky
	const int bigCell = landNear(*w, capCell, 0.08);
	const int big = town(*w, bigCell, cid, 9000, 0.5, 0);
	// two far towns, close to each other: they will secede by distance
	const int fa = landNear(*w, capCell, 0.8);
	const int fb = landNear(*w, fa, 0.08, { fa }, 0.04);
	town(*w, fa, cid, 300, 0.5, 0);
	town(*w, fb, cid, 250, 0.5, 0);
	Culture& c = w->cultures[size_t(cid)];
	c.age = Age::SPACE;
	w->settlements[size_t(cap)].b[Building::LAUNCH] = 1;
	w->settlements[size_t(cap)].ark = 0.3;
	w->year = 3000;
	int born = 0;
	for (int y = 0; y < 130; y++) {
		w->year += 1;
		auto evs = capture(*w, [&] { society::yearly(*w); });
		born += int(evOf<EvCultureBorn>(evs).size());
	}
	say("cultures " + std::to_string(w->cultures.size()) + ", parent capital " + std::to_string(w->cultures[size_t(cid)].capital)
		+ " (was " + std::to_string(cap) + "; most populous " + std::to_string(big) + "), ark " + f3(w->settlements[size_t(cap)].ark));
	ok(born == 1, "the far pair seceded once (born " + std::to_string(born) + ")");
	eq(w->cultures[size_t(cid)].capital, cap, "parent capital unchanged");
	ok(w->settlements[size_t(cap)].has(Building::LAUNCH), "launch site kept");
	ok(w->settlements[size_t(cap)].ark > 0.3 + 0.1, "Ark kept progressing through the schism");
	if (born == 1) {
		const int nc = int(w->cultures.size()) - 1;
		const Settlement* nccap = state::settlement(*w, w->cultures[size_t(nc)].capital);
		ok(nccap && nccap->culture == nc, "the daughter has a capital of her own");
	}
	noNaN(*w);
}

DM_TEST("adv society: the largest estranged bloc secedes first, not the lowest-id pair") {
	stub::Guard guard(false);
	civLike();
	auto w = makeWorld({ .L = 5, .seed = 1 });
	const int cid = newCult(*w);
	const int capCell = bestHab(*w);
	town(*w, capCell, cid, 2000, 0.5, 0);
	// a far pair with low ids
	const int pa = landNear(*w, capCell, 0.9);
	const int pb = landNear(*w, pa, 0.06, { pa }, 0.03);
	const int s1 = town(*w, pa, cid, 100);
	const int s2 = town(*w, pb, cid, 100);
	// a far province of five with higher ids, elsewhere
	int hub = -1;
	for (int c = 0; c < w->n && hub < 0; c++)
		if (landCell(*w, c) && w->geo->angle(c, capCell) > 0.75 && w->geo->angle(c, pa) > 0.8) hub = c;
	if (!ok(hub >= 0, "a second far region")) return;
	std::vector<int> prov{ hub };
	std::vector<int> provIds{ town(*w, hub, cid, 300) };
	for (int k = 0; k < 4; k++) {
		const int c = landNear(*w, hub, 0.05 + 0.04 * k, prov, 0.03);
		prov.push_back(c);
		provIds.push_back(town(*w, c, cid, 200));
	}
	w->year = 500;
	for (int y = 0; y < 125; y++) { w->year += 1; society::yearly(*w); }
	if (!eq(int(w->cultures.size()), 2, "one birth")) return;
	int moved = 0;
	for (const int sid : provIds) moved += w->settlements[size_t(sid)].culture == 1 ? 1 : 0;
	say("province towns in the daughter: " + std::to_string(moved) + "/5; low-id pair: " + std::to_string(w->settlements[size_t(s1)].culture) + ", " + std::to_string(w->settlements[size_t(s2)].culture));
	ok(moved >= 4, "the province left together");
	ok(w->settlements[size_t(s1)].culture == cid && w->settlements[size_t(s2)].culture == cid, "the pair waits its turn");
}

// ================================================================ robustness

DM_TEST("adv society: NaN in every belief input never reaches the world") {
	stub::Guard guard(false);
	civLike();
	spawnLike();
	auto w = makeWorld({ .L = 4, .seed = 2 });
	const int a = newCult(*w);
	const int b = newCult(*w);
	std::vector<int> land;
	for (int c = 0; c < w->n; c++) if (landCell(*w, c)) land.push_back(c);
	std::vector<int> sids;
	for (int k = 0; k < 12; k++) sids.push_back(town(*w, land[size_t(k * 37 % int(land.size()))], k % 2 ? b : a, 100 + k));
	for (Culture& cu : w->cultures) { cu.traits.piety = kNaN; cu.traits.aggression = kNaN; cu.traits.curiosity = kNaN; cu.traits.seafaring = kNaN; cu.pop = kNaN; }
	for (const int sid : sids) {
		Settlement& s = w->settlements[size_t(sid)];
		s.devotion = kNaN; s.love = kNaN; s.jaded = kNaN; s.ark = kNaN; s.pop = (sid % 3) ? s.pop : kNaN;
	}
	w->settlements[size_t(sids[0])].prayer = Prayer{ PrayerKind::Rain, kNaN };
	w->settlements[size_t(sids[0])].famine = 2;
	w->cultures[size_t(a)].age = Age::SPACE;
	w->settlements[size_t(w->cultures[size_t(a)].capital)].b[Building::LAUNCH] = 1;
	w->god.awe = kNaN;
	w->god.mercy = kNaN;
	w->year = 500;
	const Prophet* p = society::raiseProphet(*w, sids[1]);
	ok(p != nullptr, "prophet raised despite NaN traits");
	if (Culture* cb = state::culture(*w, w->settlements[size_t(sids[1])].culture); cb && cb->prophet) cb->prophet->until = kNaN;
	// a daughter of a culture with NaN traits
	const int d = newCult(*w, a, BirthReason::Schism);
	ok(d >= 0, "daughter created");
	if (d >= 0) {
		const Traits& t = w->cultures[size_t(d)].traits;
		ok(std::isfinite(t.piety) && std::isfinite(t.aggression) && std::isfinite(t.curiosity) && std::isfinite(t.seafaring), "daughter traits finite");
	}
	// witnesses with poisoned arguments
	society::witness(*w, land[0], kNaN, { "rain", 0.1, 0.5, {}, 0 });
	society::witness(*w, land[0], 0.3, { "rain", kNaN, kNaN, { sids[2] }, kNaN });
	society::witness(*w, -1, 0.3, { "meteor", 0.2, -1, {}, 0 });
	society::witness(*w, w->n, 0.3, { "meteor", 0.2, -1, {}, 0 });
	society::witness(*w, land[0], std::numeric_limits<double>::infinity(), { "quake", 0.1, -0.2, {}, 0 });
	society::witness(*w, land[0], 50.0, { "quake", 0.1, -0.2, { sids[3] }, -0.8 });
	society::witness(*w, land[0], -1.0, { "quake", 0.1, -0.2, {}, 0 });
	society::witness(*w, land[0], 0.4, { "", 5.0, 7.0, { -4, 99999 }, -9 });
	for (int y = 0; y < 30; y++) { w->year += 1; society::yearly(*w); }
	const society::Regard r = society::regard(*w);
	ok(std::isfinite(r.devotion) && std::isfinite(r.love), "regard finite");
	ok(std::isfinite(society::aweIncome(*w)), "aweIncome finite");
	ok(!w->cultures[size_t(w->settlements[size_t(sids[1])].culture)].prophet.has_value(), "a prophet with a NaN term ends (does not live forever)");
	ok(!w->settlements[size_t(sids[0])].prayer || std::isfinite(w->settlements[size_t(sids[0])].prayer->since), "a NaN prayer expires");
	// pop / ark of non-capitals are not Society's to repair (Civ owns pop; ark is only written in a
	// Space capital): everything Society writes must be finite, the rest is restored before the check
	for (const Settlement& s : w->settlements) {
		if (s.dead) continue;
		ok(std::isfinite(s.devotion) && std::isfinite(s.love) && std::isfinite(s.jaded), "belief of " + std::to_string(s.id) + " finite");
	}
	for (const Culture& cu : w->cultures) ok(std::isfinite(cu.pop) && std::isfinite(cu.traits.piety + cu.traits.aggression + cu.traits.curiosity + cu.traits.seafaring) || cu.parent < 0, "culture " + std::to_string(cu.id));
	ok(std::isfinite(w->god.awe) && std::isfinite(w->god.aweMax) && std::isfinite(w->god.mercy) && std::isfinite(w->god.wrath), "god finite");
	for (Settlement& s : w->settlements) { if (!std::isfinite(s.pop)) s.pop = 100; if (!std::isfinite(s.ark)) s.ark = 0; }
	for (Culture& cu : w->cultures) { Traits& t = cu.traits; for (double* x : { &t.piety, &t.aggression, &t.curiosity, &t.seafaring }) if (!std::isfinite(*x)) *x = 0.5; }
	noNaN(*w, "after NaN inputs");
	const std::string bad = checkFinite(*w);
	ok(bad.empty(), "checkFinite: " + bad);
}

DM_TEST("adv society: bad ids, dead entities, empty / sea-only / land-only worlds, every phase") {
	stub::Guard guard(false);
	// empty world (no cultures, no settlements)
	{
		auto w = makeWorld({ .L = 4, .seed = 3 });
		for (int y = 0; y < 5; y++) { w->year += 1; society::yearly(*w); }
		eq(society::aweIncome(*w), 0.0, "nobody believes: no income");
		near(w->god.aweMax, 100, 1e-12, "aweMax base");
		const society::Regard r = society::regard(*w);
		ok(r.mood == Mood::Forgotten && r.devotion == 0 && r.love == 0, "regard of nobody");
		ok(society::regard(*w, 7).mood == Mood::Forgotten, "regard of a missing culture");
		ok(society::raiseProphet(*w, 0) == nullptr && society::raiseProphet(*w, -1) == nullptr, "raiseProphet on nothing");
		ok(!society::setEdict(*w, 0, Edict::Peace) && !society::setEdict(*w, -5, Edict::Build), "setEdict on nothing");
		eq(society::witness(*w, 3, 0.5, { "rain", 0.1, 0.6, {}, 0 }), 0, "witness with nobody");
		const Culture* c = society::newCulture(*w, { 42, 99999, BirthReason::Schism });
		ok(c && c->parent == -1, "dangling parent -> root culture");
		noNaN(*w);
	}
	// sea-only and land-only worlds
	for (const double land : { 0.0, 1.0 }) {
		auto w = makeWorld({ .L = 4, .seed = 4, .land = land });
		const int cid = newCult(*w);
		for (int k = 0; k < 6; k++) town(*w, k * 31, cid, 200, 0.4, 0.1);
		for (int y = 0; y < 20; y++) {
			w->year += 1;
			society::witness(*w, (y * 17) % w->n, 0.2, { "bloom", 0.01, 0.4, {}, 0 });
			society::yearly(*w);
		}
		noNaN(*w, land == 0 ? "sea-only" : "land-only");
		ok(checkFinite(*w).empty(), "checkFinite " + f3(land, 0));
	}
	// poles: towns at both poles and a witness centred on each
	{
		auto w = makeWorld({ .L = 5, .seed = 5 });
		int north = 0, south = 0;
		for (int c = 0; c < w->n; c++) {
			if (w->geo->lat[size_t(c)] > w->geo->lat[size_t(north)]) north = c;
			if (w->geo->lat[size_t(c)] < w->geo->lat[size_t(south)]) south = c;
		}
		const int cid = newCult(*w);
		const int sn = town(*w, north, cid, 100, 0.2, 0);
		const int ss = town(*w, south, cid, 100, 0.2, 0);
		society::witness(*w, north, 0.1, { "heal", 0.12, 1, {}, 0 });
		society::witness(*w, south, 0.1, { "lightning", 0.05, 0, { ss }, -0.6 });
		ok(w->settlements[size_t(sn)].devotion > 0.2 && w->settlements[size_t(ss)].love < 0, "the poles see the god too");
		for (int y = 0; y < 150; y++) { w->year += 1; society::yearly(*w); }
		noNaN(*w, "poles");
	}
	// dead settlements / dead cultures are never touched by witness and never pray
	{
		auto w = makeWorld({ .L = 4, .seed = 6 });
		const int cid = newCult(*w);
		const int cell = bestHab(*w);
		const int s0 = town(*w, cell, cid, 300, 0.3, 0);
		const int s1 = town(*w, landNear(*w, cell, 0.05), cid, 300, 0.3, 0);
		w->settlements[size_t(s1)].dead = true;
		w->settlements[size_t(s1)].famine = 5;
		w->settlements[size_t(s1)].prayer = Prayer{ PrayerKind::Rain, 0 };
		const auto before = w->settlements[size_t(s1)];
		auto evs = capture(*w, [&] { society::witness(*w, cell, 0.3, { "rain", 0.02, 0.6, {}, 0 }); });
		for (const auto& e : evOf<EvMiracleWitnessed>(evs)) ok(std::find(e.ids.begin(), e.ids.end(), s1) == e.ids.end(), "dead not among witnesses");
		eq(w->settlements[size_t(s1)].devotion, before.devotion, "dead devotion untouched");
		w->year += 1;
		auto evy = capture(*w, [&] { society::yearly(*w); });
		for (const auto& e : evOf<EvPrayer>(evy)) ok(e.id != s1, "the dead neither start nor end prayers loudly");
		ok(!w->settlements[size_t(s1)].prayer.has_value(), "the dead pray no more");
		ok(w->settlements[size_t(s0)].devotion > 0.3, "the living one witnessed");
	}
	// every phase: witness and yearly tolerate them (Sim only calls yearly in History / Ended)
	for (const Phase ph : { Phase::Molten, Phase::Water, Phase::Life, Phase::People, Phase::History, Phase::Ended }) {
		auto w = makeWorld({ .L = 4, .seed = 7, .phase = ph });
		const int cid = newCult(*w);
		town(*w, bestHab(*w), cid, 30, 0.6, 0.3);
		society::witness(*w, bestHab(*w), 0.2, { "raise", 0.005, 0, {}, 0 });
		w->year += 1;
		society::yearly(*w);
		ok(w->phase == ph, std::string("phase untouched: ") + toStr(ph));
		noNaN(*w);
	}
}

// ================================================================ determinism

namespace {
uint32_t detRun(int L, uint32_t seed) {
	stub::Guard guard(false);
	civLike();
	spawnLike();
	auto w = makeWorld({ .L = L, .seed = seed });
	Rng r(seed * 31u + 7);
	const int c0 = newCult(*w);
	std::vector<int> land;
	for (int c = 0; c < w->n; c++) if (landCell(*w, c) && w->hab[size_t(c)] > 0.05) land.push_back(c);
	for (int k = 0; k < 50; k++) {
		const int sid = town(*w, land[size_t(r.integer(0, int(land.size()) - 1))], c0, r.range(20, 4000), r.f(), r.range(-0.5, 0.5));
		w->settlements[size_t(sid)].tier = r.integer(0, 4);
		w->settlements[size_t(sid)].b[Building::SHRINE] = uint8_t(r.chance(0.5));
		w->settlements[size_t(sid)].b[Building::TEMPLE] = uint8_t(r.chance(0.2));
	}
	const char* kinds[] = { "rain", "bloom", "meteor", "quake", "heal", "plague", "lightning" };
	for (int y = 0; y < 700; y++) {
		w->year += 1;
		if (y % 7 == 0) {
			const int cell = land[size_t(r.integer(0, int(land.size()) - 1))];
			society::witness(*w, cell, r.range(0.05, 0.45), { kinds[r.integer(0, 6)], r.range(0, 0.25), r.range(-1, 1), {}, 0 });
		}
		if (y % 60 == 5) {
			const int sid = r.integer(0, int(w->settlements.size()) - 1);
			society::raiseProphet(*w, sid);
			society::setEdict(*w, w->settlements[size_t(sid)].culture, Edict(r.integer(1, 4)));
		}
		if (!w->units.empty() && r.chance(0.2)) {
			Unit& u = w->units[0];
			if (Culture* cu = state::culture(*w, u.culture); cu && cu->prophet && u.type == UnitType::Prophet) cu->prophet->settlement = u.to;   // arrival
			state::eraseUnit(*w, u.id);
		}
		society::yearly(*w);
	}
	noNaN(*w);
	return hash(*w);
}
} // namespace

DM_TEST("adv society: determinism at L = 4 and L = 6 (prophets, schisms, witnesses)") {
	for (const int L : { 4, 6 }) {
		const uint32_t h1 = detRun(L, 9);
		const uint32_t h2 = detRun(L, 9);
		const uint32_t h3 = detRun(L, 10);
		say("L " + std::to_string(L) + ": hash " + std::to_string(h1) + " / " + std::to_string(h2) + " (seed 10: " + std::to_string(h3) + ")");
		eq(h1, h2, "same inputs, same hash at L " + std::to_string(L));
		ok(h1 != h3, "a different seed differs");
	}
}

// ================================================================ ownership

namespace {
// Everything the belief half must NOT write (writers table, FOUNDATION-CPP 13), as one string.
std::string foreign(const World& w, size_t nCultures) {   // cultures Society creates are its own
	std::string s;
	auto num = [&](double x) { s += f3(x, 9); s += ','; };
	for (int i = 0; i < state::kCellFieldCount; i++) {
		const CellArray a = state::cellArray(w, i);
		double sum = 0;
		for (int c = 0; c < int(a.size); c++) sum += a.get(c) * double(1 + (c % 7));
		num(sum);
	}
	num(w.seaLevel); num(double(w.tick)); num(double(int(w.phase))); num(w.heat); num(w.co2); num(w.dust); num(w.globalTemp); num(double(w.age));
	num(w.stats.pop); num(w.stats.peakPop); num(w.stats.believers); num(double(w.stats.wars)); num(double(w.stats.miracles));
	for (const auto& kv : w.god.deeds) { s += kv.first; num(kv.second); }
	s += w.god.epithet;
	num(double(w.chronicle.size())); num(double(w.disasters.size()));
	for (const Settlement& t : w.settlements) {
		num(t.id); num(t.cell); s += t.name; num(t.culture); num(t.founded); num(t.dead); num(t.deadYear.value_or(-1)); num(double(int(t.cause)));
		// (v3, design review: Society blesses a town whose rain / bloom prayer it answers, so blessed is not foreign)
		num(t.pop); num(t.tier); num(t.K); num(t.growth); num(t.radius); num(t.plague); num(t.immune); num(t.inspire);
		num(t.famine); num(t.capital); num(t.lastExpand); num(double(t.cells.size()));
		for (int k = 0; k < Building::COUNT; k++) if (k != Building::WONDER) num(t.b[size_t(k)]);
	}
	for (size_t i = 0; i < nCultures && i < w.cultures.size(); i++) {
		const Culture& c = w.cultures[i];
		num(c.id); s += c.name + c.adj + c.plural + c.lang + c.godName + c.epithet; num(c.color[0]); num(c.parent); num(c.born);
		num(c.age); num(c.science); num(c.capital);
	}
	for (const auto& kv : w.vars.rel) { num(kv.first.first); num(kv.first.second); num(kv.second.v); num(kv.second.war); }
	for (const auto& kv : w.vars.num) if (kv.first.rfind("society_", 0) != 0) { s += kv.first; num(kv.second); }
	for (const auto& kv : w.vars.str) s += kv.first + kv.second;
	num(w.vars.seaBase.value_or(-99)); num(w.vars.lifeBoost); num(double(w.vars.t0));
	return s;
}
} // namespace

DM_TEST("adv society: yearly and witness write only what the writers table gives Society") {
	stub::Guard guard(true);
	// with the real Diplomacy / Chronicle / Civ / Units linked (run.ps1 all) they legitimately write
	// their own state from inside yearly: this check is for the belief half alone
	for (const char* m : { "diplomacy", "chronicle", "civ", "units" }) if (!stub::stubbed(m)) return;
	auto w = makeWorld({ .L = 5, .seed = 8 });
	const int a = newCult(*w);
	const int b = newCult(*w);
	std::vector<int> land;
	for (int c = 0; c < w->n; c++) if (landCell(*w, c)) land.push_back(c);
	Rng r(5);
	for (int k = 0; k < 40; k++) {
		const int sid = town(*w, land[size_t(r.integer(0, int(land.size()) - 1))], k % 2 ? a : b, r.range(30, 5000), r.f(), r.range(-1, 1));
		Settlement& s = w->settlements[size_t(sid)];
		s.tier = r.integer(0, 4);
		s.famine = r.chance(0.3) ? 2 : 0;
		s.plague = r.chance(0.2) ? 0.6 : 0;
		for (int bb = 0; bb < Building::COUNT; bb++) s.b[size_t(bb)] = uint8_t(r.chance(0.3));
	}
	for (int c = 0; c < w->n; c++) w->scorch[size_t(c)] = float(r.f() * 0.6);
	w->cultures[size_t(a)].age = Age::SPACE;
	society::raiseProphet(*w, w->cultures[size_t(a)].capital);
	society::setEdict(*w, a, Edict::Build);
	w->year = 1000;
	const size_t nc0 = w->cultures.size();
	const std::string before = foreign(*w, nc0);
	for (int y = 0; y < 200; y++) {
		w->year += 1;
		society::witness(*w, land[size_t(y * 13 % int(land.size()))], 0.3, { y % 2 ? "rain" : "meteor", 0.1, y % 2 ? 0.6 : -1, {}, 0 });
		const double savedYear = w->year;
		society::yearly(*w);
		ok(w->year == savedYear, "year untouched");
	}
	// the only collaborator writes allowed are through calls (setCulture / electCapital / spawn / kill /
	// makePeace are stubs without hooks here, so nothing foreign may change at all)
	const std::string after = foreign(*w, nc0);
	say("cultures before / after: " + std::to_string(nc0) + " / " + std::to_string(w->cultures.size()));
	if (!ok(before == after, "no foreign state changed")) {
		size_t k = 0;
		while (k < before.size() && k < after.size() && before[k] == after[k]) k++;
		say("first difference at char " + std::to_string(k) + ": ..." + before.substr(k > 40 ? k - 40 : 0, 80) + " | " + after.substr(k > 40 ? k - 40 : 0, 80));
	}
	int society = 0;
	for (const auto& kv : w->vars.num) society += kv.first.rfind("society_", 0) == 0 ? 1 : 0;
	say("society_* bookkeeping keys: " + std::to_string(society) + ", calls: setCulture " + std::to_string(stub::called("civ_setCulture"))
		+ ", electCapital " + std::to_string(stub::called("civ_electCapital")) + ", spawn " + std::to_string(stub::called("units_spawn"))
		+ ", diplomacy " + std::to_string(stub::called("society_diplomacy")) + ", updateEpithets " + std::to_string(stub::called("chronicle_updateEpithets")));
	eq(stub::called("society_diplomacy"), 200, "yearly ends with diplomacy every year");
	eq(stub::called("chronicle_updateEpithets"), 200, "and updateEpithets");
	// order: diplomacy before updateEpithets in every year
	int lastD = -1;
	bool order = true;
	for (size_t i = 0; i < stub::calls().size(); i++) {
		if (stub::calls()[i] == "society_diplomacy") lastD = int(i);
		if (stub::calls()[i] == "chronicle_updateEpithets" && lastD < 0) order = false;
		if (stub::calls()[i] == "chronicle_updateEpithets") lastD = -1;
	}
	ok(order, "diplomacy, then updateEpithets");
}

// ================================================================ prophets

DM_TEST("adv society: a prophet whose least devout town is unreachable visits the next one") {
	stub::Guard guard(true);
	if (!stub::stubbed("units")) return;
	auto w = makeWorld({ .L = 5, .seed = 1 });
	const int cid = newCult(*w);
	const int capCell = bestHab(*w);
	const int cap = town(*w, capCell, cid, 800, 0.5);
	const int lonely = town(*w, landNear(*w, capCell, 0.3), cid, 50, 0.05);   // unreachable (refused below)
	const int next = town(*w, landNear(*w, capCell, 0.15), cid, 200, 0.2);
	std::vector<units::SpawnOpts> log;
	spawnLike(&log, [lonely](const units::SpawnOpts& o) { return o.to == lonely; });
	w->year = 10;
	ok(society::raiseProphet(*w, cap) != nullptr, "raised");
	w->cultures[size_t(cid)].prophet->until = 1e6;
	for (int y = 0; y < 12; y++) { w->year += 1; society::yearly(*w); }
	say("spawn attempts: " + std::to_string(log.size()) + (log.empty() ? "" : ", targets " + std::to_string(log[0].to) + (log.size() > 1 ? " then " + std::to_string(log[1].to) : "")));
	ok(log.size() >= 2 && log[0].to == lonely && log[1].to == next, "tries the least devout first, then the next");
	ok(w->units.size() == 1 && w->units[0].to == next, "a journey to the reachable town");
	ok(w->cultures[size_t(cid)].prophet.has_value(), "he lives");
}

// ================================================================ prayers over time

DM_TEST("adv society: prayers over 200 years: never more than 6, no restart the year one ends, events paired") {
	stub::Guard guard(false);
	auto w = makeWorld({ .L = 5, .seed = 3 });
	const int a = newCult(*w);
	std::vector<int> land;
	for (int c = 0; c < w->n; c++) if (landCell(*w, c)) land.push_back(c);
	Rng r(77);
	std::vector<int> sids;
	for (int k = 0; k < 30; k++) sids.push_back(town(*w, land[size_t(k * 53 % int(land.size()))], a, 100 + 10 * k, 0.5));
	std::map<int, int> open;    // settlement -> open prayer count from events
	int maxActive = 0, starts = 0, ends = 0, answered = 0;
	bool paired = true;
	for (int y = 0; y < 200; y++) {
		w->year += 1;
		for (const int sid : sids) {
			Settlement& s = w->settlements[size_t(sid)];
			s.famine = r.chance(0.3) ? 1 : 0;      // causes come and go
			s.plague = r.chance(0.2) ? 0.5 : 0;
		}
		auto evs = capture(*w, [&] {
			society::yearly(*w);
			if (y % 9 == 0) society::witness(*w, w->settlements[size_t(sids[size_t(y % 30)])].cell, 0.2, { "rain", 0.01, 0.6, {}, 0 });
		});
		std::map<int, int> endedThisYear;
		for (const Event& e : evs) {
			if (const EvPrayer* p = e.as<EvPrayer>()) {
				if (p->start) { if (open[p->id] != 0 || endedThisYear[p->id]) paired = false; open[p->id] = 1; starts++; }
				else { if (open[p->id] != 1) paired = false; open[p->id] = 0; endedThisYear[p->id] = 1; ends++; }
			}
			if (const EvPrayerAnswered* p = e.as<EvPrayerAnswered>()) { if (open[p->id] != 1) paired = false; open[p->id] = 0; answered++; }
		}
		int active = 0;
		for (const int sid : sids) active += w->settlements[size_t(sid)].prayer ? 1 : 0;
		maxActive = std::max(maxActive, active);
		int fromEvents = 0;
		for (const auto& kv : open) fromEvents += kv.second;
		if (fromEvents != active) paired = false;
	}
	say("200 years: " + std::to_string(starts) + " starts, " + std::to_string(ends) + " ends, " + std::to_string(answered) + " answered, max active " + std::to_string(maxActive));
	ok(maxActive <= 6 && maxActive >= 4, "at most 6 prayers at once (and the cap is reached)");
	ok(paired, "every prayer start has exactly one end / answer; no restart the year one ends");
	ok(starts > 20 && answered > 0, "prayers are frequent and some are answered");
	noNaN(*w);
}

// ================================================================ the Ark with two Space cultures

DM_TEST("adv society: two Space cultures: vars.ark set once, ark:launched once; a new capital restarts") {
	stub::Guard guard(false);
	civLike();
	auto w = makeWorld({ .L = 5, .seed = 2 });
	const int a = newCult(*w);
	const int b = newCult(*w);
	const int ca = bestHab(*w);
	const int sa = town(*w, ca, a, 2.0e6, 0.2);
	const int sb = town(*w, landNear(*w, ca, 0.6), b, 1.0e6, 0.2);
	for (const int cid : { a, b }) w->cultures[size_t(cid)].age = Age::SPACE;
	w->settlements[size_t(sa)].b[Building::LAUNCH] = 1;
	w->settlements[size_t(sb)].b[Building::LAUNCH] = 1;
	w->year = 3500;
	std::vector<Event> all;
	for (int y = 0; y < 600; y++) {
		w->year += 1;
		auto evs = capture(*w, [&] { society::yearly(*w); });
		all.insert(all.end(), evs.begin(), evs.end());
	}
	auto started = evOf<EvArkStarted>(all);
	auto launched = evOf<EvArkLaunched>(all);
	say("ark:started " + std::to_string(started.size()) + ", ark:launched " + std::to_string(launched.size())
		+ (w->vars.ark ? ", vars.ark culture " + std::to_string(w->vars.ark->culture) + " year " + f3(w->vars.ark->year, 0) : ""));
	eq(int(started.size()), 2, "each capital starts once");
	eq(int(launched.size()), 1, "launched exactly once");
	if (ok(w->vars.ark.has_value(), "vars.ark set")) {
		eq(w->vars.ark->culture, a, "the larger culture (rate x 1.25) wins");
		near(w->vars.ark->year, 3500 + 200, 1.0, "250 / 1.25 = 200 years");
	}
	const double arkB = w->settlements[size_t(sb)].ark;
	near(arkB, 199 * 0.8 / 250, 1e-9, "the other (1 M people: rate x 0.8) stopped where it was when the Ark left");
	// a new capital starts its own Ark from 0 (Civ resets; Society emits ark:started again)
	auto w2 = makeWorld({ .L = 5, .seed = 2 });
	const int c2 = newCult(*w2);
	const int s1 = town(*w2, bestHab(*w2), c2, 1.5e6, 0.2);
	const int s2 = town(*w2, landNear(*w2, bestHab(*w2), 0.1), c2, 0.5e6, 0.2);
	w2->cultures[size_t(c2)].age = Age::SPACE;
	w2->settlements[size_t(s1)].b[Building::LAUNCH] = 1;
	for (int y = 0; y < 50; y++) { w2->year += 1; society::yearly(*w2); }
	w2->settlements[size_t(s1)].dead = true;               // the capital falls
	w2->cultures[size_t(c2)].capital = s2;                  // (Civ re-elects)
	w2->settlements[size_t(s2)].capital = true;
	w2->settlements[size_t(s2)].b[Building::LAUNCH] = 1;    // (Economy builds a new launch site)
	auto evs2 = capture(*w2, [&] { w2->year += 1; society::yearly(*w2); });
	eq(int(evOf<EvArkStarted>(evs2).size()), 1, "the new capital starts its own Ark");
	near(w2->settlements[size_t(s2)].ark, (1.0 / 250) * 0.8, 1e-9, "from 0 at the minimum rate (0.5 M people)");
}

// ================================================================ brush pacing

DM_TEST("adv society: brush witnesses at Powers' 4 calls / s give at most 2 miracle:witnessed / s") {
	auto w = makeWorld({ .L = 5, .seed = 1 });
	const int cid = newCult(*w);
	const int cell = bestHab(*w);
	for (int k = 0; k < 5; k++) town(*w, landNear(*w, cell, 0.02 * k), cid, 200, 0.3);
	int events = 0;
	auto evs = capture(*w, [&] {
		for (int k = 0; k < 40; k++) society::witness(*w, cell, 0.2, { "rain", 0.02 * 0.25, 0.1, {}, 0 });   // 10 real seconds
	});
	events = int(evOf<EvMiracleWitnessed>(evs).size());
	say("10 s of rain brush (40 witness calls): " + std::to_string(events) + " miracle:witnessed");
	ok(events <= 20 && events >= 19, "2 per real second");
	// click powers are never paced
	auto evc = capture(*w, [&] { for (int k = 0; k < 5; k++) society::witness(*w, cell, 0.1, { "heal", 0.12, 1, {}, 0 }); });
	eq(int(evOf<EvMiracleWitnessed>(evc).size()), 5, "click acts each witnessed");
	// love per second of brushing is frame-rate independent: 4 calls of 0.25 s == 2 calls of 0.5 s
	auto lw = [&](int calls) {
		auto ww = makeWorld({ .L = 4, .seed = 1 });
		const int cc = newCult(*ww);
		const int sid = town(*ww, bestHab(*ww), cc, 100, 0.3, 0);
		for (int k = 0; k < calls; k++) society::witness(*ww, bestHab(*ww), 0.2, { "bloom", 0.02 * (2.0 / calls), 0.4, {}, 0 });
		return ww->settlements[size_t(sid)].love;
	};
	const double l4 = lw(8), l2 = lw(4);
	say("love after 2 s of bloom: 8 calls " + f3(l4, 4) + ", 4 calls " + f3(l2, 4));
	near(l4, l2, 0.02, "same love for the same seconds");
}

// ================================================================ cost at L = 6

DM_TEST("adv society: cost at L = 6 with real territories, droughts, wars and 160 settlements") {
	stub::Guard guard(false);
	auto w = makeWorld({ .L = 6, .seed = 2 });
	std::vector<int> cids;
	for (int k = 0; k < 6; k++) cids.push_back(newCult(*w));
	std::vector<int> land;
	for (int c = 0; c < w->n; c++) if (landCell(*w, c)) land.push_back(c);
	int placed = 0;
	for (size_t k = 0; k < land.size() && placed < 160; k += land.size() / 160) {
		const int sid = town(*w, land[k], cids[size_t(placed % 6)], 500 + 37 * placed, 0.4, 0.1 * ((placed % 5) - 2));
		w->settlements[size_t(sid)].tier = placed % 5;
		w->settlements[size_t(sid)].b[Building::SHRINE] = 1;
		placed++;
	}
	// territories: 6-hop discs (metropolis radius) without overlap: nearest centre wins
	std::fill(w->owner.begin(), w->owner.end(), int16_t(-1));
	const double rad = 6 * w->geo->step;
	std::vector<int> disc;
	for (const Settlement& s : w->settlements) {
		w->geo->within(s.cell, rad, disc);
		for (const int c : disc) {
			if (!landCell(*w, c)) continue;
			const int o = w->owner[size_t(c)];
			if (o < 0 || w->geo->angle(c, s.cell) < w->geo->angle(c, w->settlements[size_t(o)].cell)) w->owner[size_t(c)] = int16_t(s.id);
		}
	}
	for (Settlement& s : w->settlements) s.cells.clear();
	for (int c = 0; c < w->n; c++) if (w->owner[size_t(c)] >= 0) w->settlements[size_t(w->owner[size_t(c)])].cells.push_back(c);
	size_t cells = 0;
	for (const Settlement& s : w->settlements) cells += s.cells.size();
	for (int c = 0; c < w->n; c++) w->scorch[size_t(c)] = 0.2f;     // bloom causes evaluated everywhere, just below the gate
	for (int k = 0; k < 8; k++) {
		Disaster& d = state::newDisaster(*w, DisasterType::Drought, land[size_t(k * 997 % int(land.size()))]);
		d.ang = 0.2; d.power = 0.6; d.dur = 1e9;
	}
	for (size_t i = 0; i < cids.size(); i++)
		for (size_t j = i + 1; j < cids.size(); j++) if (Relation* rl = society::rel(*w, cids[i], cids[j])) { rl->war = (i + j) % 2 == 1; }
	double worst = 0;
	const double t0 = nowMs();
	for (int y = 0; y < 200; y++) {
		w->year += 1;
		const double a = nowMs();
		society::yearly(*w);
		worst = std::max(worst, nowMs() - a);
	}
	const double mean = (nowMs() - t0) / 200;
	// witness: a meteor-size act and a quarter-second of brush, over 160 settlements
	const double t1 = nowMs();
	for (int k = 0; k < 400; k++) society::witness(*w, land[size_t(k * 211 % int(land.size()))], 0.45, { "meteor", 0.25, 0, {}, -1 });
	const double wMeteor = (nowMs() - t1) / 400;
	const double t2 = nowMs();
	for (int k = 0; k < 400; k++) society::witness(*w, land[size_t(k * 211 % int(land.size()))], 0.12, { "rain", 0.005, 0.1, {}, 0 });
	const double wBrush = (nowMs() - t2) / 400;
	say(std::to_string(placed) + " settlements, " + std::to_string(cells) + " territory cells, 8 droughts, 7 wars: yearly mean " + f3(mean, 3)
		+ " ms, worst " + f3(worst, 3) + " ms (diplomacy / chronicle " + (stub::stubbed("diplomacy") ? "stubbed" : "real") + ")");
	say("witness: meteor-size " + f3(wMeteor * 1000, 1) + " us, brush " + f3(wBrush * 1000, 1) + " us per call");
	// yearly runs once per 4 ticks; the tick budget is 2.5 ms mean / 30 ms max (5x margin in tests)
	ok(mean < 2.5, "yearly mean is a small share of one tick");
	ok(worst < 30, "no yearly spike");
	ok(wMeteor < 1 && wBrush < 1, "witness well under a millisecond");
	noNaN(*w);
}

// ================================================================ long-run maps

DM_TEST("maps adv: 900 years of belief on the L = 6 helper planet (cultures, devotion, love)") {
	stub::Guard guard(false);
	civLike();
	spawnLike();
	auto w = makeWorld({ .L = 6, .seed = 1 });
	const int c0 = newCult(*w);
	Rng r(3);
	std::vector<int> cells;
	for (int c = 0; c < w->n; c++) {
		if (!landCell(*w, c) || w->hab[size_t(c)] < 0.1) continue;
		bool far = true;
		for (const int o : cells) if (w->geo->angle(o, c) < 0.1) { far = false; break; }
		if (far && r.chance(0.6)) cells.push_back(c);
	}
	for (const int c : cells) {
		const int sid = town(*w, c, c0, r.range(200, 3000), 0.5, 0);
		w->settlements[size_t(sid)].tier = r.integer(1, 3);
		w->settlements[size_t(sid)].b[Building::SHRINE] = uint8_t(r.chance(0.6));
	}
	// the capital: the most populous
	int cap = 0;
	for (const Settlement& s : w->settlements) if (s.pop > w->settlements[size_t(cap)].pop) cap = s.id;
	w->cultures[size_t(c0)].capital = cap;
	for (Settlement& s : w->settlements) s.capital = s.id == cap;
	// the god: steady rain near the capital, meteors on the far side every 40 years
	int farCell = cells[0];
	for (const int c : cells) if (w->geo->angle(c, w->settlements[size_t(cap)].cell) > w->geo->angle(farCell, w->settlements[size_t(cap)].cell)) farCell = c;
	std::vector<Event> all;
	for (int y = 0; y < 900; y++) {
		w->year += 1;
		auto evs = capture(*w, [&] {
			for (int k = 0; k < 4; k++) society::witness(*w, w->settlements[size_t(cap)].cell, 0.25, { "rain", 0.005, 0.1, {}, 0 });
			if (y % 40 == 0) {
				std::vector<int> hit = civ::near(*w, farCell, 0.12);
				society::witness(*w, farCell, 0.45, { "meteor", 0.25, 0, hit, -1 });
			}
			if (y % 120 == 3) society::raiseProphet(*w, w->cultures[size_t(c0)].capital);
			society::yearly(*w);
		});
		all.insert(all.end(), evs.begin(), evs.end());
	}
	// territories for the maps
	std::fill(w->owner.begin(), w->owner.end(), int16_t(-1));
	for (int c = 0; c < w->n; c++) {
		if (!landCell(*w, c)) continue;
		double bd = 0.06;
		int bs = -1;
		for (const Settlement& s : w->settlements) {
			if (s.dead) continue;
			const double d = w->geo->angle(c, s.cell);
			if (d <= bd) { bd = d; bs = s.id; }
		}
		w->owner[size_t(c)] = int16_t(bs);
	}
	int living = 0;
	for (const Culture& c : w->cultures) living += c.dead ? 0 : 1;
	std::map<int, int> perCulture;
	for (const Settlement& s : w->settlements) if (!s.dead) perCulture[s.culture]++;
	std::string dist;
	for (const auto& kv : perCulture) dist += " " + std::to_string(kv.first) + ":" + std::to_string(kv.second);
	say(std::to_string(cells.size()) + " settlements; cultures " + std::to_string(w->cultures.size()) + " (" + std::to_string(living) + " living); towns per culture" + dist);
	say("events: culture:born " + std::to_string(evOf<EvCultureBorn>(all).size()) + ", prophet:risen " + std::to_string(evOf<EvProphetRisen>(all).size())
		+ ", prophet:died " + std::to_string(evOf<EvProphetDied>(all).size()) + ", prayer " + std::to_string(evOf<EvPrayer>(all).size())
		+ ", miracle:witnessed " + std::to_string(evOf<EvMiracleWitnessed>(all).size()));
	const society::Regard rg = society::regard(*w);
	say("world regard: devotion " + f3(rg.devotion) + " love " + f3(rg.love) + " mood " + toStr(rg.mood) + "; awe " + f3(w->god.awe, 1) + "/" + f3(w->god.aweMax, 0)
		+ " mercy " + f3(w->god.mercy, 2) + " wrath " + f3(w->god.wrath, 2));
	ok(living >= 2 && living <= 6, "schisms happened, never more than 6 living cultures");
	// every daughter is a compact group: members within 2 x 0.28 rad of each other at birth time is not
	// checkable now; check the daughters are geographically coherent (mean spread)
	for (const Culture& c : w->cultures) {
		if (c.dead || c.parent < 0) continue;
		std::vector<int> mine;
		for (const Settlement& s : w->settlements) if (!s.dead && s.culture == c.id) mine.push_back(s.cell);
		double maxD = 0;
		for (const int x : mine) for (const int y2 : mine) maxD = std::max(maxD, w->geo->angle(x, y2));
		say("  daughter " + std::to_string(c.id) + " (parent " + std::to_string(c.parent) + ", born " + f3(c.born, 0) + "): " + std::to_string(mine.size()) + " towns, spread " + f3(maxD) + " rad");
		ok(maxD <= 2 * w->tune<SocietyTune>().secedeAng + 1e-9, "daughter towns lie together");
	}
	auto byVal = [&](const std::function<mi::Rgb(const Settlement&)>& col) {
		return [&w, col](int c) -> mi::Rgb {
			if (w->elev[size_t(c)] < w->seaLevel) return mi::Rgb{ 22, 34, 66 };
			const int o = w->owner[size_t(c)];
			if (o < 0) return mi::Rgb{ 70, 70, 62 };
			const Settlement& s = w->settlements[size_t(o)];
			if (s.cell == c) return s.capital ? mi::Rgb{ 255, 255, 255 } : mi::Rgb{ 20, 20, 20 };
			return col(s);
		};
	};
	auto diverge = [](double t) {           // -1 blue, 0 white-ish, +1 red
		t = clamp(t, -1.0, 1.0);
		return t < 0 ? mi::Rgb{ 245 + 200 * t, 245 + 150 * t, 245 } : mi::Rgb{ 245, 245 - 190 * t, 245 - 210 * t };
	};
	ok(mi::owner(*w, shot("ue-society-adv-cultures.png")), "culture map");
	ok(mi::custom(*w, shot("ue-society-adv-love.png"), byVal([&](const Settlement& s) { return diverge(s.love); })), "love map");
	ok(mi::custom(*w, shot("ue-society-adv-devotion.png"), byVal([&](const Settlement& s) { return mi::heat(s.devotion); })), "devotion map");
	noNaN(*w);
}

// ================================================================ integration (run.ps1 all only)

#include "Sim.h"

DM_TEST("adv society: integration, real sim 1500 years at L = 6 (all modules real)") {
	for (const char* m : { "sim", "civ", "economy", "diplomacy", "units", "chronicle", "life", "climate" }) if (stub::stubbed(m)) return;
	auto w = sim::create({ .seed = 7, .name = "", .L = 6 });
	if (!ok(w != nullptr, "world")) return;
	watch(*w);
	if (!ok(sim::genesis::skip(*w), "genesis skip")) return;
	std::map<std::string, int> counts;
	const auto tok = w->bus.onAll([&](const Event& e) { counts[e.name()]++; });
	int atCap = 0, yearsSeen = 0, maxPrayers = 0, maxLiving = 0;
	bool orphan = false, range = true;
	const double t0 = nowMs();
	for (int y = 0; y < 1500; y++) {
		sim::advance(*w, 4);
		yearsSeen++;
		if (w->god.awe >= 0.9 * w->god.aweMax) atCap++;
		int pr = 0, living = 0;
		for (const Settlement& s : w->settlements) {
			if (s.dead) continue;
			pr += s.prayer ? 1 : 0;
			const Culture* c = state::culture(*w, s.culture);
			if (!c || c->dead) orphan = true;
			if (!(s.devotion >= 0 && s.devotion <= 1 && s.love >= -1 && s.love <= 1 && s.jaded >= 0 && s.jaded <= 1)) range = false;
		}
		for (const Culture& c : w->cultures) living += c.dead ? 0 : 1;
		maxPrayers = std::max(maxPrayers, pr);
		maxLiving = std::max(maxLiving, living);
		if (w->ending) break;
	}
	const double ms = nowMs() - t0;
	w->bus.off(tok);
	int live = 0;
	for (const Settlement& s : w->settlements) live += s.dead ? 0 : 1;
	const society::Regard rg = society::regard(*w);
	say("year " + f3(w->year, 0) + " (" + f3(ms / 1000, 1) + " s): pop " + f3(w->stats.pop, 0) + ", " + std::to_string(live) + " towns, cultures " + std::to_string(w->cultures.size())
		+ " (max living " + std::to_string(maxLiving) + "), awe " + f3(w->god.awe, 1) + "/" + f3(w->god.aweMax, 0) + " income " + f3(society::aweIncome(*w), 2)
		+ ", years >= 90 % of cap " + std::to_string(atCap) + "/" + std::to_string(yearsSeen));
	say("regard: devotion " + f3(rg.devotion) + " love " + f3(rg.love) + " mood " + toStr(rg.mood) + "; max prayers at once " + std::to_string(maxPrayers));
	std::string ev;
	for (const char* k : { "culture:born", "culture:died", "prayer", "prayer:answered", "settlement:captured", "war:declared", "wonder:built", "ark:started", "ark:launched" })
		ev += std::string(" ") + k + "=" + std::to_string(counts[k]);
	say("events:" + ev);
	ok(!orphan, "every live settlement belongs to a living culture");
	ok(range, "devotion / love / jaded stay in range");
	ok(maxPrayers <= 6, "at most 6 prayers");
	ok(maxLiving <= 7, "cultures bounded (6 by schism; awaken may add one)");
	noNaN(*w, "integration");
	ok(mi::owner(*w, shot("ue-society-adv-sim-owner.png")), "owner map");
}
