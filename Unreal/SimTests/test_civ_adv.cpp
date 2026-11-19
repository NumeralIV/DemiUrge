// DEMIURGE — SimTests/test_civ_adv.cpp : adversarial verification of Sim/Civ.cpp (A7 civ lifecycle).
// Owner: the A7 verifier. Written independently of test_civ.cpp: brute-force references for every query,
// a random-operation fuzzer with invariant, ownership (writers table) and event checks after every call,
// determinism across runs and across a save / load, order independence of territory claims, hostile inputs
// (NaN fields, stale caches, dead owners, poles, water worlds, every phase, L 4 / 5 / 6) and L 6 timings.
// Runs with Economy stubbed (`run.ps1 civ`, capacity 0) and with every module real (`run.ps1 all`).
// Maps: shots/ue-civ-adv-*.png.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Civ.h"
#include "Hydro.h"
#include "MapImg.h"
#include "TestFramework.h"
#include "TestWorld.h"

using namespace dm;
using namespace dmt;
namespace mi = dm::mapimg;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

// ------------------------------------------------------------------ independent references

// hop distance from c over every cell (sea included), capped
std::vector<int> hopField(const World& w, const std::vector<int>& sources, int cap) {
	std::vector<int> d(size_t(w.n), -1), q;
	for (int s : sources) if (d[size_t(s)] < 0) { d[size_t(s)] = 0; q.push_back(s); }
	for (size_t h = 0; h < q.size(); h++) {
		const int x = q[h];
		if (d[size_t(x)] >= cap) continue;
		for (int k = 0; k < w.geo->nbrN[size_t(x)]; k++) {
			const int m = w.geo->nbr[size_t(6 * x + k)];
			if (m < 0 || d[size_t(m)] >= 0) continue;
			d[size_t(m)] = d[size_t(x)] + 1;
			q.push_back(m);
		}
	}
	return d;
}

bool liveId(const World& w, int id) { return id >= 0 && size_t(id) < w.settlements.size() && !w.settlements[size_t(id)].dead; }

// The documented canSettle, written out from SPEC-SIM 3.7 + the header's reading of "crowded".
civ::SettleReason refSettle(const World& w, int c) {
	if (!w.valid(c)) return civ::SettleReason::Sea;
	if (state::isSea(w, c) || hydro::isLake(w, c)) return civ::SettleReason::Sea;
	if (!(w.hab[size_t(c)] > 0.05f)) return civ::SettleReason::Barren;
	std::vector<int> d = hopField(w, { c }, std::max(0, w.tune<CivTune>().crowdHops));
	for (int m = 0; m < w.n; m++)
		if (d[size_t(m)] >= 0 && liveId(w, w.owner[size_t(m)])) return civ::SettleReason::Crowded;
	return civ::SettleReason::Ok;
}

double dotc(const World& w, int a, int b) {
	const float* pa = &w.geo->pos[size_t(3 * a)];
	const float* pb = &w.geo->pos[size_t(3 * b)];
	return double(pa[0]) * double(pb[0]) + double(pa[1]) * double(pb[1]) + double(pa[2]) * double(pb[2]);
}

// The documented territory disc of a centre: land cells within (r + roundPad) x step reached over land inside it.
std::vector<char> refDisc(const World& w, int c, int r) {
	std::vector<char> in(size_t(w.n), 0);
	if (!w.valid(c)) return in;
	const double cosR = std::cos((r + w.tune<CivTune>().roundPad) * w.geo->step);
	std::vector<char> seen(size_t(w.n), 0);
	std::vector<int> q{ c };
	seen[size_t(c)] = 1;
	if (!state::isSea(w, c)) in[size_t(c)] = 1;
	for (size_t h = 0; h < q.size(); h++) {
		const int x = q[h];
		for (int k = 0; k < w.geo->nbrN[size_t(x)]; k++) {
			const int m = w.geo->nbr[size_t(6 * x + k)];
			if (m < 0 || seen[size_t(m)]) continue;
			seen[size_t(m)] = 1;
			if (state::isSea(w, m) || dotc(w, m, c) < cosR) continue;
			in[size_t(m)] = 1;
			q.push_back(m);
		}
	}
	return in;
}

// "" or what is wrong with territories (cells ascending, owned by the lister, land, unique, every owned cell listed)
std::string territoryIssue(const World& w) {
	std::vector<int> seen(size_t(w.n), -1);
	for (const Settlement& s : w.settlements) {
		if (s.dead) { if (!s.cells.empty()) return "dead " + std::to_string(s.id) + " holds cells"; continue; }
		for (size_t i = 0; i < s.cells.size(); i++) {
			const int c = s.cells[i];
			if (!w.valid(c)) return "bad cell id";
			if (i && s.cells[i - 1] >= c) return "cells of " + std::to_string(s.id) + " not ascending";
			if (w.owner[size_t(c)] != s.id) return "cell " + std::to_string(c) + " listed by " + std::to_string(s.id) + " owned by " + std::to_string(w.owner[size_t(c)]);
			if (seen[size_t(c)] >= 0) return "cell " + std::to_string(c) + " in two lists";
			if (state::isSea(w, c)) return "sea cell " + std::to_string(c) + " owned by " + std::to_string(s.id);
			seen[size_t(c)] = s.id;
		}
	}
	for (int c = 0; c < w.n; c++) {
		const int o = w.owner[size_t(c)];
		if (o < 0) continue;
		if (size_t(o) >= w.settlements.size()) return "owner out of range";
		if (w.settlements[size_t(o)].dead) return "cell " + std::to_string(c) + " owned by dead " + std::to_string(o);
		if (seen[size_t(c)] != o) return "owned cell " + std::to_string(c) + " not in its owner's list";
	}
	return "";
}

// "" or what is wrong with capitals: a culture's capital is -1 or a live settlement of it flagged capital; every
// flagged settlement is live and its culture's capital; a culture with a live settlement has a capital.
std::string capitalIssue(const World& w) {
	for (const Culture& cu : w.cultures) {
		bool hasLive = false;
		for (const Settlement& s : w.settlements) if (!s.dead && s.culture == cu.id) hasLive = true;
		if (cu.capital >= 0) {
			if (!liveId(w, cu.capital)) return "culture " + std::to_string(cu.id) + " capital " + std::to_string(cu.capital) + " not live";
			const Settlement& s = w.settlements[size_t(cu.capital)];
			if (s.culture != cu.id) return "culture " + std::to_string(cu.id) + " capital belongs to " + std::to_string(s.culture);
			if (!s.capital) return "capital " + std::to_string(s.id) + " not flagged";
		} else if (hasLive) return "culture " + std::to_string(cu.id) + " has live settlements but no capital";
	}
	for (const Settlement& s : w.settlements) {
		if (!s.capital) continue;
		if (s.dead) return "dead " + std::to_string(s.id) + " flagged capital";
		const Culture* cu = state::culture(w, s.culture);
		if (!cu || cu->capital != s.id) return "settlement " + std::to_string(s.id) + " flagged capital but culture says otherwise";
	}
	return "";
}

// Civ-written values are finite (the fuzzer injects nothing non-finite into these)
std::string civFinite(const World& w) {
	for (const Settlement& s : w.settlements) {
		const double v[] = { s.pop, s.K, s.devotion, s.love, s.plague, s.immune, s.ark, s.deadYear.value_or(0) };
		for (double x : v) if (!std::isfinite(x)) return "settlement " + std::to_string(s.id);
	}
	for (int c = 0; c < w.n; c++) if (!std::isfinite(w.urban[size_t(c)])) return "urban";
	return "";
}

// Serialize, load, blank everything the Civ lifecycle half may write (writers table), drop settlements created
// since `keep`, serialize again. Two snapshots compare equal iff Civ touched nothing else.
std::vector<uint8_t> neutral(const World& w, size_t keep) {
	std::vector<uint8_t> b, out;
	std::string why;
	if (!state::serialize(w, b, &why)) return { 1, 2, 3 };
	auto x = state::deserialize(b, &why);
	if (!x) return { 4, 5, 6 };
	std::fill(x->owner.begin(), x->owner.end(), int16_t(-1));
	std::fill(x->urban.begin(), x->urban.end(), 0.0f);
	if (x->settlements.size() > keep) x->settlements.resize(keep);
	for (Settlement& s : x->settlements) {
		s.pop = 0; s.K = 0; s.tier = 0; s.radius = 2; s.plague = 0; s.immune = 0;
		s.culture = -1; s.capital = false; s.dead = false; s.deadYear.reset(); s.cause = Cause::None;
		s.b[size_t(Building::LAUNCH)] = 0; s.ark = 0;
	}
	for (Culture& cu : x->cultures) cu.capital = -1;
	state::serialize(*x, out);
	return out;
}

struct Snap {
	std::vector<uint8_t> neutralBytes;
	size_t n = 0;
	std::vector<double> ark, launch, devotion, love, inspire, blessed, jaded;
	Need need;
	uint32_t rngS = 0;
};
Snap snap(const World& w) {
	Snap s;
	s.neutralBytes = neutral(w, w.settlements.size());
	s.n = w.settlements.size();
	for (const Settlement& t : w.settlements) {
		s.ark.push_back(t.ark); s.launch.push_back(t.b[size_t(Building::LAUNCH)]);
		s.devotion.push_back(t.devotion); s.love.push_back(t.love); s.inspire.push_back(t.inspire);
		s.blessed.push_back(t.blessed); s.jaded.push_back(t.jaded);
	}
	s.need = w.need;
	s.rngS = w.rng.s;
	return s;
}
// "" or the first write outside the Civ lifecycle's rights since `a`
std::string writesOutside(const World& w, const Snap& a, bool mayCreate) {
	if (w.settlements.size() < a.n) return "settlements removed";
	if (w.settlements.size() > a.n + (mayCreate ? 1 : 0)) return "settlements created by an op that must not";
	if (w.rng.s != a.rngS) return "w.rng drawn";
	if (w.need.derive != a.need.derive || w.need.climate != a.need.climate || w.need.hydro != a.need.hydro || w.need.life != a.need.life) return "need flags changed";
	for (size_t i = 0; i < a.n; i++) {
		const Settlement& t = w.settlements[i];
		if (t.ark != a.ark[i] && t.ark != 0) return "ark changed to non-zero on " + std::to_string(i);
		if (t.b[size_t(Building::LAUNCH)] != a.launch[i] && t.b[size_t(Building::LAUNCH)] != 0) return "launch built on " + std::to_string(i);
		if (t.devotion != a.devotion[i] || t.love != a.love[i] || t.jaded != a.jaded[i]) return "belief of " + std::to_string(i) + " changed (Society's)";
		if (t.inspire != a.inspire[i] || t.blessed != a.blessed[i]) return "inspire / blessed of " + std::to_string(i) + " changed";
	}
	if (neutral(w, a.n) != a.neutralBytes) return "state outside the Civ writers changed";
	return "";
}

std::vector<int> refNear(const World& w, int cell, double ang) {
	std::vector<std::pair<double, int>> v;
	if (!w.valid(cell) || !(ang >= 0)) return {};
	for (const Settlement& s : w.settlements)
		if (!s.dead && w.valid(s.cell) && w.geo->angle(cell, s.cell) <= ang) v.emplace_back(w.geo->angle(cell, s.cell), s.id);
	std::sort(v.begin(), v.end());
	std::vector<int> ids;
	for (auto& p : v) ids.push_back(p.second);
	return ids;
}

bool atOk(const World& w, int cell, int got) {
	if (!w.valid(cell)) return got == -1;
	bool centred = false;
	for (const Settlement& s : w.settlements) if (!s.dead && s.cell == cell) centred = true;
	if (centred) return liveId(w, got) && w.settlements[size_t(got)].cell == cell;
	const int o = w.owner[size_t(cell)];
	return liveId(w, o) ? got == o : got == -1;
}

int countEv(const std::vector<Event>& ev, Ev t) {
	int k = 0;
	for (const Event& e : ev) k += e.type == t;
	return k;
}

int liveCount(const World& w) {
	int k = 0;
	for (const Settlement& s : w.settlements) k += !s.dead;
	return k;
}

// ------------------------------------------------------------------ the fuzzer

struct FuzzResult {
	uint32_t hash = 0;
	std::vector<std::string> evLog;
	int ops = 0, founded = 0, destroyed = 0, captured = 0, failures = 0;
};

// One random operation on w drawn from r, with every check. Returns false after the first failed check.
bool fuzzStep(World& w, Rng& r, FuzzResult& R, int step) {
	const std::string tag = "step " + std::to_string(step);
	const int nS = int(w.settlements.size()), nC = int(w.cultures.size());
	auto anySid = [&] { return r.chance(0.08) ? r.integer(-2, nS + 2) : (nS ? r.integer(0, nS - 1) : -1); };
	auto anyCu = [&] { return r.chance(0.08) ? r.integer(-1, nC + 3) : (nC ? r.integer(0, nC - 1) : -1); };
	const int op = r.integer(0, 13);
	const Snap before = snap(w);
	const int liveBefore = liveCount(w);
	std::vector<Event> ev;
	std::string bad;
	bool mayCreate = false;
	switch (op) {
	case 0: case 1: {                                                    // found at a random cell / at bestSite
		const int c = op == 0 ? r.integer(-1, w.n) : civ::bestSite(w);
		if (op == 1 && c >= 0 && !civ::canSettle(w, c).ok) { bad = "bestSite returned an unsettleable cell"; break; }
		const int cu = anyCu();
		const double pop = r.chance(0.05) ? kNaN : r.range(-5, 400);
		const bool cuOk = cu >= 0 && cu < nC && !w.cultures[size_t(cu)].dead;
		const bool siteOk = civ::canSettle(w, c).ok;
		const bool capOk = liveBefore < w.tune<CivTune>().maxSettlements;
		civ::FoundOpts fo;
		if (r.chance(0.5)) fo.devotion = r.range(-1, 2);
		if (r.chance(0.5)) fo.love = r.range(-2, 2);
		const FoundedBy by = r.chance(0.3) ? FoundedBy::Awaken : FoundedBy::Settlers;
		Settlement* s = nullptr;
		mayCreate = true;
		ev = capture(w, [&] { s = civ::found(w, c, cu, pop, by, fo); });
		const bool expect = cuOk && siteOk && capOk && std::isfinite(pop) && pop > 0;
		if ((s != nullptr) != expect) { bad = "found returned " + std::string(s ? "a settlement" : "null") + " against the rules"; break; }
		if (s) {
			R.founded++;
			const int sid = s->id;
			const Settlement& t = w.settlements[size_t(sid)];
			if (t.cell != c || t.culture != cu || t.dead || t.tier != 0) { bad = "found fields"; break; }
			if (std::find(t.cells.begin(), t.cells.end(), c) == t.cells.end()) { bad = "found does not own its centre"; break; }
			const double dv = fo.devotion ? std::clamp(*fo.devotion, 0.0, 1.0) : (by == FoundedBy::Awaken ? 0.6 : 0.5);
			const double lv = fo.love ? std::clamp(*fo.love, -1.0, 1.0) : (by == FoundedBy::Awaken ? 0.3 : 0.0);
			if (std::fabs(t.devotion - dv) > 1e-12 || std::fabs(t.love - lv) > 1e-12) { bad = "found devotion / love"; break; }
			if (countEv(ev, Ev::SettlementFounded) != 1 || ev.size() != 1) { bad = "found events"; break; }
			const EvSettlementFounded* e = ev[0].as<EvSettlementFounded>();
			if (!e || e->id != sid || e->cell != c || e->culture != cu || e->by != by) { bad = "settlement:founded payload"; break; }
		} else if (!ev.empty()) { bad = "refused found emitted"; }
		break;
	}
	case 2: {                                                            // claim with a new radius
		const int sid = anySid();
		if (liveId(w, sid)) w.settlements[size_t(sid)].radius = r.chance(0.05) ? r.integer(-3, 40) : r.integer(0, 7);
		Snap b2 = snap(w);
		ev = capture(w, [&] { civ::claim(w, sid); });
		if (!ev.empty()) bad = "claim emitted";
		if (bad.empty()) bad = writesOutside(w, b2, false);
		if (bad.empty() && liveId(w, sid)) {
			// every land cell of its disc is now its own or held by a live rival
			const Settlement& s = w.settlements[size_t(sid)];
			const auto disc = refDisc(w, s.cell, std::clamp(s.radius < 0 ? 2 : s.radius, 0, 32));
			for (int c = 0; c < w.n && bad.empty(); c++) {
				if (!disc[size_t(c)]) { if (w.owner[size_t(c)] == sid) bad = "claim took cell " + std::to_string(c) + " outside its disc"; continue; }
				if (w.owner[size_t(c)] < 0) bad = "claim left cell " + std::to_string(c) + " of its disc unowned";
			}
		}
		break;
	}
	case 3: {                                                            // destroy
		const int sid = anySid();
		const bool wasLive = liveId(w, sid);
		const Cause cause = Cause(r.integer(0, 11));
		ev = capture(w, [&] { civ::destroy(w, sid, cause); });
		if (countEv(ev, Ev::SettlementDestroyed) != (wasLive ? 1 : 0) || ev.size() != size_t(wasLive ? 1 : 0)) { bad = "destroy events"; break; }
		if (wasLive) {
			R.destroyed++;
			const Settlement& s = w.settlements[size_t(sid)];
			const EvSettlementDestroyed* e = ev[0].as<EvSettlementDestroyed>();
			const Cause want = cause == Cause::None ? Cause::Abandoned : cause;
			if (!s.dead || !s.deadYear || *s.deadYear != w.year || s.cause != want) bad = "destroy fields";
			else if (e->id != sid || e->cell != s.cell || e->culture != s.culture || e->cause != want) bad = "settlement:destroyed payload";
			else if (!s.cells.empty() || s.capital) bad = "destroyed keeps territory or capital";
		}
		break;
	}
	case 4: {                                                            // damage
		const int sid = anySid();
		const double frac = r.chance(0.1) ? (r.chance(0.5) ? kNaN : kInf) : r.range(-0.2, 1.2);
		const bool wasLive = liveId(w, sid);
		const double p0 = wasLive ? w.settlements[size_t(sid)].pop : 0;
		double k = -1;
		ev = capture(w, [&] { k = civ::damage(w, sid, frac, Cause::Quake); });
		if (!std::isfinite(k) || k < 0) { bad = "damage returned " + std::to_string(k); break; }
		if (!wasLive || !(frac > 0)) { if (k != 0 || !ev.empty()) bad = "damage of nothing"; break; }
		const Settlement& s = w.settlements[size_t(sid)];
		const double f = std::min(1.0, frac);
		if (p0 - p0 * f < 3) {
			if (!s.dead || countEv(ev, Ev::SettlementDestroyed) != 1 || s.cause != Cause::Quake) bad = "damage below 3 people must destroy";
			else if (std::fabs(k - p0) > 1e-9) bad = "damage that destroys returns the people there";
		} else {
			if (s.dead || !ev.empty()) bad = "damage destroyed / emitted above 3 people";
			else if (std::fabs(k - p0 * f) > 1e-9 * std::max(1.0, p0) || std::fabs(s.pop - (p0 - k)) > 1e-9 * std::max(1.0, p0)) bad = "damage arithmetic";
		}
		break;
	}
	case 5: case 6: {                                                    // capture / setCulture
		const int sid = anySid(), cu = anyCu();
		const bool wasLive = liveId(w, sid);
		const int from = wasLive ? w.settlements[size_t(sid)].culture : -1;
		const bool should = wasLive && cu >= 0 && cu < nC && !w.cultures[size_t(cu)].dead && cu != from;
		ev = capture(w, [&] { if (op == 5) civ::capture(w, sid, cu); else civ::setCulture(w, sid, cu); });
		if (countEv(ev, Ev::SettlementCaptured) != (should ? 1 : 0) || ev.size() != size_t(should ? 1 : 0)) { bad = "capture events"; break; }
		if (should) {
			R.captured++;
			const EvSettlementCaptured* e = ev[0].as<EvSettlementCaptured>();
			if (e->id != sid || e->from != from || e->to != cu || e->how != (op == 5 ? CaptureHow::War : CaptureHow::Schism)) bad = "settlement:captured payload";
			else if (w.settlements[size_t(sid)].culture != cu) bad = "culture not changed";
		}
		break;
	}
	case 7: {                                                            // electCapital
		const int cu = anyCu();
		int got = -2;
		ev = capture(w, [&] { got = civ::electCapital(w, cu); });
		int want = -1;
		double bp = -1;
		for (const Settlement& s : w.settlements)
			if (!s.dead && s.culture == cu && s.pop > bp) { bp = s.pop; want = s.id; }
		if (got != want) bad = "electCapital " + std::to_string(got) + " want " + std::to_string(want);
		else if (!ev.empty()) bad = "electCapital emitted";
		break;
	}
	case 8: {                                                            // infect
		const int sid = anySid();
		const double sev = r.chance(0.1) ? kNaN : r.range(-0.3, 1.4);
		const bool wasLive = liveId(w, sid);
		const double p0 = wasLive ? w.settlements[size_t(sid)].plague : 0, im = wasLive ? w.settlements[size_t(sid)].immune : 1;
		bool got = false;
		ev = capture(w, [&] { got = civ::infect(w, sid, sev); });
		const bool should = wasLive && !(im > 0) && sev > 0;
		if (got != should) { bad = "infect result"; break; }
		if (countEv(ev, Ev::SettlementPlague) != ((should && p0 == 0) ? 1 : 0)) { bad = "infect event count"; break; }
		if (should) {
			const double want = std::max(p0, std::min(1.0, sev));
			if (std::fabs(w.settlements[size_t(sid)].plague - want) > 1e-12) bad = "infect severity";
			if (!ev.empty() && !ev[0].as<EvSettlementPlague>()->start) bad = "plague start flag";
		}
		break;
	}
	case 9: {                                                            // cure
		const int sid = anySid();
		const double yrs = r.chance(0.1) ? kNaN : r.range(-5, 60);
		const bool wasLive = liveId(w, sid);
		const bool ill = wasLive && w.settlements[size_t(sid)].plague > 0;
		bool got = false;
		ev = capture(w, [&] { got = civ::cure(w, sid, yrs); });
		if (got != wasLive) { bad = "cure result"; break; }
		if (countEv(ev, Ev::SettlementPlague) != (ill ? 1 : 0)) { bad = "cure event"; break; }
		if (ill && ev[0].as<EvSettlementPlague>()->start) bad = "cure emitted start: true";
		if (wasLive && w.settlements[size_t(sid)].plague != 0) bad = "cure left plague";
		break;
	}
	case 10: {                                                           // rebuild from owner[] alone
		std::vector<std::vector<int32_t>> cells;
		for (Settlement& s : w.settlements) { cells.push_back(s.cells); s.cells.clear(); }
		const auto own = w.owner;
		civ::rebuild(w);
		if (w.owner != own) { bad = "rebuild changed owner"; break; }
		for (size_t i = 0; i < cells.size(); i++) if (cells[i] != w.settlements[i].cells) { bad = "rebuild cells differ for " + std::to_string(i); break; }
		break;
	}
	case 11: {                                                           // a new culture (Society's job, done by hand)
		if (nC < 6) state::newCulture(w);
		break;
	}
	default: {                                                           // queries against the references
		for (int k = 0; k < 6 && bad.empty(); k++) {
			const int c = r.integer(-1, w.n);
			const civ::SettleCheck sc = civ::canSettle(w, c);
			const civ::SettleReason want = refSettle(w, c);
			if (sc.reason != want || sc.ok != (want == civ::SettleReason::Ok)) bad = "canSettle(" + std::to_string(c) + ") " + civ::toStr(sc.reason) + " want " + civ::toStr(want);
			const int a = civ::at(w, c);
			if (!atOk(w, c, a)) bad = "at(" + std::to_string(c) + ") = " + std::to_string(a);
			const double ang = r.range(-0.05, 0.8);
			if (civ::near(w, c, ang) != refNear(w, c, ang)) bad = "near order";
			const double sv = civ::siteScore(w, c);
			if (!std::isfinite(sv) || sv < -1) bad = "siteScore not finite";
		}
		std::vector<int> lv;
		for (const Settlement& s : w.settlements) if (!s.dead) lv.push_back(s.id);
		if (bad.empty() && civ::live(w) != lv) bad = "live";
		break;
	}
	}
	if (bad.empty() && op != 2 && op != 11) bad = writesOutside(w, before, mayCreate);   // 11: the test made a culture
	if (bad.empty()) bad = territoryIssue(w);
	if (bad.empty()) bad = capitalIssue(w);
	if (bad.empty()) bad = civFinite(w);
	if (bad.empty() && liveCount(w) > w.tune<CivTune>().maxSettlements) bad = "more than maxSettlements live";
	for (const Event& e : ev) {
		std::string line = e.name();
		e.fields([&](const char*, const auto& v) {
			using V = std::decay_t<decltype(v)>;
			if constexpr (std::is_arithmetic_v<V>) line += " " + std::to_string(double(v));
			else if constexpr (std::is_enum_v<V>) line += " e" + std::to_string(int(v));
		});
		R.evLog.push_back(line);
	}
	R.ops++;
	if (!bad.empty()) {
		R.failures++;
		return fail(tag + " (op " + std::to_string(op) + "): " + bad);
	}
	return true;
}

FuzzResult fuzz(World& w, uint32_t seed, int steps, int startStep = 0) {
	FuzzResult R;
	Rng r(seed);
	for (int i = 0; i < startStep; i++) r.next();
	for (int i = 0; i < steps; i++) {
		w.year += 0.25 * r.integer(0, 8);
		if (!fuzzStep(w, r, R, i)) break;
	}
	R.hash = hash(w);
	return R;
}

void seedCultures(World& w, int k) { for (int i = 0; i < k; i++) state::newCulture(w); }

} // namespace

// ==================================================================== fuzz: invariants, ownership, events

DM_TEST("civ adv: random operations keep every invariant (L 4 and L 5, 4 seeds, 1,400 ops)") {
	int ops = 0, founded = 0, destroyed = 0, captured = 0;
	for (uint32_t seed = 1; seed <= 4; seed++) {
		auto w = makeWorld({ .L = seed <= 2 ? 4 : 5, .seed = seed, .land = 0.35 + 0.05 * seed });
		if (seed == 3) w->tune<CivTune>().crowdHops = 2;      // dense packing: many contested borders
		if (seed == 4) w->tune<CivTune>().maxSettlements = 12; // the cap gets hit
		seedCultures(*w, 3);
		const FuzzResult R = fuzz(*w, 100 + seed, 350);
		ops += R.ops; founded += R.founded; destroyed += R.destroyed; captured += R.captured;
		if (R.failures) break;
	}
	std::printf("    fuzz: %d ops, %d founded, %d destroyed, %d captured / converted, all invariants held\n", ops, founded, destroyed, captured);
	ok(founded > 40 && destroyed > 10 && captured > 10, "the fuzzer exercised founding, destruction and capture");
}

DM_TEST("civ adv: deterministic: same ops, same hash and event log; save / load continues identically") {
	auto a = makeWorld({ .L = 4, .seed = 9 });
	auto b = makeWorld({ .L = 4, .seed = 9 });
	seedCultures(*a, 3);
	seedCultures(*b, 3);
	const FuzzResult ra = fuzz(*a, 77, 300), rb = fuzz(*b, 77, 300);
	eq(ra.hash, rb.hash, "same hash");
	ok(ra.evLog == rb.evLog, "same event log (" + std::to_string(ra.evLog.size()) + " events)");
	// save / load: continue the original and a loaded copy with the same operations (derived fields copied over,
	// since the helper's stand-ins are not saved)
	std::vector<uint8_t> bytes;
	if (!ok(state::serialize(*a, bytes), "serialize")) return;
	std::string why;
	auto c = state::deserialize(bytes, &why);
	if (!ok(c != nullptr, "deserialize " + why)) return;
	watch(*c);
	// every cell array except owner (the Civ state under test) comes from the original: derived fields are not saved
	// and quantized ones would feed a real Economy's capacity slightly different numbers (run.ps1 all)
#define DM_ADV_COPY(name, ...) if (std::string(#name) != "owner") c->name = a->name;
	DM_CELL_FIELDS(DM_ADV_COPY)
#undef DM_ADV_COPY
	c->cache = a->cache;
	civ::rebuild(*c);
	bool same = true;
	for (size_t i = 0; i < a->settlements.size(); i++) same = same && a->settlements[i].cells == c->settlements[i].cells;
	ok(same, "rebuild after load restores every territory");
	const FuzzResult ra2 = fuzz(*a, 501, 200), rc2 = fuzz(*c, 501, 200);
	std::vector<uint8_t> ba, bc;
	state::serialize(*a, ba);
	state::serialize(*c, bc);
	ok(ba == bc, "original and loaded world are identical after 200 more ops");
	ok(ra2.evLog == rc2.evLog, "same events after the load");
	eq(a->rng.s, c->rng.s, "rng untouched and equal");
}

// ==================================================================== claims

DM_TEST("civ adv: after one claim round the land is shared by the nearest reaching centre, in any order") {
	for (uint32_t seed = 1; seed <= 3; seed++) {
		const std::string tag = "seed " + std::to_string(seed);
		std::vector<std::vector<int16_t>> results;
		for (int order = 0; order < 3; order++) {
			auto w = makeWorld({ .L = 5, .seed = seed });
			w->tune<CivTune>().crowdHops = 3;
			const int cu = state::newCulture(*w).id;
			Rng r(seed * 31);
			for (int k = 0; k < 400 && liveCount(*w) < 40; k++) civ::found(*w, r.integer(0, w->n - 1), cu, 50);
			for (Settlement& s : w->settlements) s.radius = 2 + int(hash32(uint32_t(s.id) * 7 + seed) % 5);
			std::vector<int> ids;
			for (const Settlement& s : w->settlements) ids.push_back(s.id);
			if (order == 1) std::reverse(ids.begin(), ids.end());
			if (order == 2) { Rng rr(seed + 5); rr.shuffle(ids); }
			for (int sid : ids) civ::claim(*w, sid);
			if (!eq(territoryIssue(*w), std::string(), tag)) return;
			// reference: each land cell inside any disc belongs to the nearest centre whose disc contains it (ties lower id)
			std::vector<std::vector<char>> discs;
			for (const Settlement& s : w->settlements) discs.push_back(refDisc(*w, s.cell, s.radius));
			int wrong = 0, holes = 0, inside = 0;
			for (int c = 0; c < w->n; c++) {
				int best = -1;
				double bd = -2;
				for (size_t i = 0; i < discs.size(); i++) {
					if (!discs[i][size_t(c)]) continue;
					const double d = dotc(*w, c, w->settlements[i].cell);
					if (d > bd) { bd = d; best = int(i); }
				}
				if (best < 0) { if (w->owner[size_t(c)] >= 0) wrong++; continue; }
				inside++;
				if (w->owner[size_t(c)] < 0) holes++;
				else if (w->owner[size_t(c)] != best) wrong++;
			}
			eq(wrong, 0, tag + " order " + std::to_string(order) + ": cells held by a farther centre");
			eq(holes, 0, tag + " order " + std::to_string(order) + ": holes");
			if (order == 0) std::printf("    seed %u: %d settlements, %d cells inside a disc\n", seed, liveCount(*w), inside);
			results.push_back(w->owner);
		}
		ok(results[0] == results[1] && results[0] == results[2], tag + ": owner map independent of claim order");
	}
}

DM_TEST("civ adv: shrinking radii, rising sea and dead owners: claims drop what they no longer hold") {
	auto w = makeWorld({ .L = 5, .seed = 5 });
	const int cu = state::newCulture(*w).id;
	int sid = -1;
	for (int c = 0; c < w->n && sid < 0; c++)
		if (w->coast[size_t(c)] == 2 && w->hab[size_t(c)] > 0.3f && civ::canSettle(*w, c).ok) {
			Settlement* s = civ::found(*w, c, cu, 80);
			if (s) sid = s->id;
		}
	if (!ok(sid >= 0, "coastal settlement")) return;
	w->settlements[size_t(sid)].radius = 6;
	civ::claim(*w, sid);
	const size_t big = w->settlements[size_t(sid)].cells.size();
	w->settlements[size_t(sid)].radius = 1;
	civ::claim(*w, sid);
	const size_t small = w->settlements[size_t(sid)].cells.size();
	ok(small < big && small >= 1, "shrinking releases land (" + std::to_string(big) + " -> " + std::to_string(small) + ")");
	int stray = 0;
	for (int c = 0; c < w->n; c++) stray += w->owner[size_t(c)] == sid;
	eq(size_t(stray), small, "owner[] matches the new list");
	// the sea rises over part of the territory: the next claim gives up the drowned cells
	w->settlements[size_t(sid)].radius = 6;
	civ::claim(*w, sid);
	w->seaLevel = 0.25;
	int drowned = 0;
	for (int32_t c : w->settlements[size_t(sid)].cells) drowned += state::isSea(*w, c);
	civ::claim(*w, sid);
	int seaOwned = 0;
	for (int c = 0; c < w->n; c++) seaOwned += (w->owner[size_t(c)] >= 0 && state::isSea(*w, c));
	ok(drowned > 0, "the rise drowned " + std::to_string(drowned) + " territory cells");
	eq(seaOwned, 0, "no sea cell owned after the re-claim");
	eq(territoryIssue(*w), std::string());
	// stale owner[] pointing at a dead settlement (a hand-edited world): a live claim takes it over, canSettle and at
	// ignore it
	w->seaLevel = 0;
	auto w2 = makeWorld({ .L = 4, .seed = 2 });
	const int cu2 = state::newCulture(*w2).id;
	const int c0 = bestHab(*w2);
	{
		Settlement& d = state::newSettlement(*w2, c0, cu2);
		d.dead = true;
	}
	for (int c = 0; c < w2->n; c++) if (!state::isSea(*w2, c)) w2->owner[size_t(c)] = 0;
	eq(civ::at(*w2, c0), -1, "at ignores a dead owner");
	ok(civ::canSettle(*w2, c0).ok, "dead owners do not crowd");
	Settlement* s2 = civ::found(*w2, c0, cu2, 40);
	if (ok(s2 != nullptr, "found over a dead owner's land")) {
		eq(w2->owner[size_t(c0)], int16_t(1), "centre taken over");
		const auto disc = refDisc(*w2, c0, w2->settlements[1].radius);
		int missing = 0, inDisc = 0;
		for (int c = 0; c < w2->n; c++) if (disc[size_t(c)]) { inDisc++; missing += w2->owner[size_t(c)] != 1; }
		eq(missing, 0, "the whole camp disc (" + std::to_string(inDisc) + " cells) taken from the dead owner");
	}
}

// ==================================================================== hostile worlds

DM_TEST("civ adv: water world, land-only world, barren world, bare world") {
	{   // all sea
		auto w = makeWorld({ .L = 4, .land = 0.4 });
		w->seaLevel = 50;
		const int cu = state::newCulture(*w).id;
		int refused = 0;
		for (int c = 0; c < w->n; c += 7) refused += civ::found(*w, c, cu, 30) == nullptr;
		eq(refused, (w->n + 6) / 7, "nothing founded on a water world");
		eq(civ::bestSite(*w), -1, "no best site");
		ok(civ::canSettle(*w, 5).reason == civ::SettleReason::Sea, "sea");
		eq(civ::siteScore(*w, 5), 0.0, "score 0");
		{ Settlement& s = state::newSettlement(*w, 10, cu); s.pop = 30; s.radius = 4; }
		civ::claim(*w, 0);
		ok(w->settlements[0].cells.empty(), "a settlement under the sea claims nothing");
		civ::destroy(*w, 0, Cause::Drowned);
		ok(w->settlements[0].dead, "and can be destroyed");
		noNaN(*w);
	}
	{   // all land, no sea at all
		auto w = makeWorld({ .L = 4 });
		w->seaLevel = -50;
		std::fill(w->hab.begin(), w->hab.end(), 0.5f);
		w->cache.landId.clear();                               // no cache: bestSite labels the one landmass itself
		const int cu = state::newCulture(*w).id;
		int made = 0;
		for (int k = 0; k < 60; k++) {
			const int c = civ::bestSite(*w);
			if (c < 0) break;
			if (!ok(civ::canSettle(*w, c).ok, "bestSite settleable")) break;
			if (civ::found(*w, c, cu, 30)) made++;
		}
		ok(made >= 5, "settlements fill a land planet (" + std::to_string(made) + ")");
		ok(civ::bestSite(*w) == -1 || civ::canSettle(*w, civ::bestSite(*w)).ok, "last bestSite valid");
		eq(territoryIssue(*w), std::string());
		noNaN(*w);
	}
	{   // barren (hab 0 everywhere)
		auto w = makeWorld({ .L = 4 });
		std::fill(w->hab.begin(), w->hab.end(), 0.0f);
		const int cu = state::newCulture(*w).id;
		eq(civ::bestSite(*w), -1, "barren: no best site");
		ok(civ::found(*w, findLand(*w), cu, 30) == nullptr, "barren: no founding");
		ok(civ::canSettle(*w, findLand(*w)).reason == civ::SettleReason::Barren, "reason barren");
	}
	{   // bare state::create world at L 0 and L 6 (elev 0 vs seaLevel -9: all land, hab 0)
		for (int L : { 0, 6 }) {
			auto w = state::create({ .seed = 2, .L = L });
			watch(*w);
			const int cu = state::newCulture(*w).id;
			eq(civ::bestSite(*w), -1, "bare: -1");
			ok(civ::found(*w, 0, cu, 30) == nullptr, "bare: no founding (hab 0)");
			ok(civ::near(*w, 0, 10).empty() && civ::at(*w, 0) == -1, "bare queries");
			civ::rebuild(*w);
		}
	}
}

DM_TEST("civ adv: NaN and garbage in the fields Civ reads never leak into what it writes") {
	auto w = makeWorld({ .L = 5, .seed = 6 });
	const int cu = state::newCulture(*w).id;
	Rng r(4);
	// poison 3 % of hab, elev and fresh / coast
	std::vector<int> poisoned;
	for (int c = 0; c < w->n; c++) {
		if (r.f() < 0.03) { w->hab[size_t(c)] = float(kNaN); poisoned.push_back(c); }
		else if (r.f() < 0.02) w->elev[size_t(c)] = float(kNaN);
		else if (r.f() < 0.02) w->hab[size_t(c)] = float(kInf);
		if (r.f() < 0.05) { w->fresh[size_t(c)] = uint8_t(r.integer(0, 255)); w->coast[size_t(c)] = uint8_t(r.integer(0, 255)); }
	}
	for (int c : poisoned) {
		const civ::SettleCheck sc = civ::canSettle(*w, c);
		if (!ok(!sc.ok, "NaN hab is not settleable")) break;
	}
	int made = 0;
	for (int k = 0; k < 30; k++) {
		const int c = civ::bestSite(*w);
		if (c < 0) break;
		if (!ok(std::isfinite(civ::siteScore(*w, c)), "bestSite score finite")) break;
		if (civ::found(*w, c, cu, 40)) made++;
		else { fail("bestSite returned an unfoundable cell " + std::to_string(c)); break; }
	}
	ok(made >= 10, "founded " + std::to_string(made) + " on a poisoned world");
	for (int c = 0; c < w->n; c++) {
		const double v = civ::siteScore(*w, c);
		if (!ok(std::isfinite(v), "siteScore finite at " + std::to_string(c))) break;
	}
	for (Settlement& s : w->settlements) s.radius = 5;
	for (size_t i = 0; i < w->settlements.size(); i++) civ::claim(*w, int(i));
	// rival with NaN pop, a settlement with an invalid cell, a culture id out of range
	w->settlements[0].pop = kNaN;
	{ Settlement& g = state::newSettlement(*w, -1, cu); g.pop = 30; }
	{ Settlement& g = state::newSettlement(*w, 7, 42); g.pop = 30; }
	const int ghost = int(w->settlements.size()) - 2, alien = ghost + 1;
	civ::claim(*w, ghost);
	ok(w->settlements[size_t(ghost)].cells.empty(), "invalid centre claims nothing");
	civ::destroy(*w, ghost, Cause::War);
	civ::capture(*w, alien, cu);
	civ::electCapital(*w, cu);
	civ::damage(*w, 0, 0.5, Cause::War);
	ok(w->settlements[0].dead, "a NaN population is destroyed by any damage");
	eq(civFinite(*w), std::string(), "everything Civ wrote is finite");
	for (Settlement& s : w->settlements) if (!s.dead) { s.radius = 3; civ::claim(*w, s.id); }
	eq(territoryIssue(*w), std::string(), "territories consistent on a poisoned world");
}

DM_TEST("civ adv: stale or missing landmass caches still give a valid bestSite") {
	auto w = makeWorld({ .L = 5, .seed = 3 });
	const int good = civ::bestSite(*w);
	ok(good >= 0 && civ::canSettle(*w, good).ok, "with the cache");
	auto bad = [&](const char* what) {
		const int b = civ::bestSite(*w);
		ok(b >= 0 && civ::canSettle(*w, b).ok, std::string(what) + ": valid site");
		return b;
	};
	const WorldCache keep = w->cache;
	w->cache.landId.resize(10);
	eq(bad("landId wrong size"), good, "own labels agree with the cache");
	w->cache = keep;
	w->cache.landHab.pop_back();
	eq(bad("landHab short"), good, "own labels agree with the cache");
	w->cache = keep;
	w->cache.landCount = 0;
	eq(bad("landCount 0"), good, "own labels agree");
	w->cache = keep;
	std::fill(w->cache.landHab.begin(), w->cache.landHab.end(), 0);
	bad("landHab all zero");
	w->cache = keep;
	// labels for another world: every label 0 (one giant "landmass") -> any land is a candidate, still valid
	std::fill(w->cache.landId.begin(), w->cache.landId.end(), 0);
	bad("labels all 0");
	// labels that point outside landCount
	std::fill(w->cache.landId.begin(), w->cache.landId.end(), 999);
	const int b = civ::bestSite(*w);
	ok(b == -1 || civ::canSettle(*w, b).ok, "labels out of range: -1 or a valid site");
	w->cache = keep;
}

DM_TEST("civ adv: poles and every phase") {
	auto w = state::create({ .seed = 1, .L = 6, .geo = sharedGeo(6) });
	watch(*w);
	std::fill(w->elev.begin(), w->elev.end(), 0.5f);
	std::fill(w->hab.begin(), w->hab.end(), 0.6f);
	w->seaLevel = 0;
	const int cu = state::newCulture(*w).id;
	const int np = w->geo->cellAt(0, 1, 0), sp = w->geo->cellAt(0, -1, 0), eqc = w->geo->cellAt(1, 0, 0);
	std::vector<size_t> sizes;
	for (int c : { np, sp, eqc }) {
		Settlement* s = civ::found(*w, c, cu, 50);
		if (!ok(s != nullptr, "found at " + std::to_string(c))) return;
		const int sid = s->id;
		w->settlements[size_t(sid)].radius = 6;
		civ::claim(*w, sid);
		sizes.push_back(w->settlements[size_t(sid)].cells.size());
	}
	std::printf("    radius 6 territory: north pole %zu, south pole %zu, equator %zu cells\n", sizes[0], sizes[1], sizes[2]);
	// the disc is round in angle, so its cell count follows the icosphere's cell density (cells are smallest at the
	// 12 icosahedron vertices, one of which is each pole): measure the spread over the whole sphere
	ok(std::abs(int(sizes[0]) - int(sizes[2])) <= 20 && sizes[0] == sizes[1], "pole discs within 15 % of the equator's");
	{
		auto f = state::create({ .seed = 1, .L = 6, .geo = sharedGeo(6) });
		std::fill(f->elev.begin(), f->elev.end(), 0.5f);
		f->seaLevel = 0;
		size_t mn = 1u << 30, mx = 0;
		for (int c = 0; c < f->n; c += 97) {
			const auto d = refDisc(*f, c, 6);
			size_t k = 0;
			for (char x : d) k += x;
			mn = std::min(mn, k); mx = std::max(mx, k);
		}
		std::printf("    radius 6 disc over the sphere (every 97th cell): %zu .. %zu cells\n", mn, mx);
		ok(double(mx) / double(mn) < 1.35, "cell count of a full disc varies < 35 % over the sphere (round disc; see the report)");
	}
	const auto ring = civ::near(*w, np, 0.3);
	eq(ring.size(), size_t(1), "near at the pole");
	ok(mi::owner(*w, shot("ue-civ-adv-poles.png")), "map");
	// every phase: the lifecycle works the same (no phase gate in the spec)
	const Phase phases[] = { Phase::Molten, Phase::Water, Phase::Life, Phase::People, Phase::History, Phase::Ended };
	int k = 0;
	for (Phase ph : phases) {
		auto v = makeWorld({ .L = 4, .seed = 3, .phase = ph });
		const int c = state::newCulture(*v).id;
		const int site = civ::bestSite(*v);
		Settlement* s = civ::found(*v, site, c, 30, FoundedBy::Awaken);
		if (!ok(s != nullptr, std::string("found in phase ") + toStr(ph))) continue;
		const int sid = s->id;
		civ::claim(*v, sid);
		civ::infect(*v, sid);
		civ::cure(*v, sid);
		civ::damage(*v, sid, 0.99, Cause::Meteor);
		ok(v->settlements[size_t(sid)].dead, "destroyed in every phase");
		ok(v->phase == ph, "phase untouched");
		k++;
	}
	eq(k, 6, "six phases");
}

DM_TEST("civ adv: capital and Ark rules across capture, schism and destruction") {
	auto w = makeWorld({ .L = 5, .seed = 2 });
	w->tune<CivTune>().crowdHops = 2;
	const int A = state::newCulture(*w).id, B = state::newCulture(*w).id;
	int sid[3];
	const double pops[3] = { 100, 500, 300 };
	for (int i = 0; i < 3; i++) {
		const int c = civ::bestSite(*w);
		Settlement* s = civ::found(*w, c, A, pops[i]);
		if (!ok(s != nullptr, "found")) return;
		sid[i] = s->id;
	}
	auto S = [&](int i) -> Settlement& { return w->settlements[size_t(sid[i])]; };
	eq(w->cultures[size_t(A)].capital, sid[0], "the first founded is the capital (sticky, not the most populous)");
	S(0).b[size_t(Building::LAUNCH)] = 1; S(0).ark = 0.5;
	// conquest of a non-capital: the loser keeps its capital and Ark; the winner (no capital yet) gets one
	std::vector<Event> ev = capture(*w, [&] { civ::capture(*w, sid[1], B); });
	eq(countEv(ev, Ev::SettlementCaptured), 1, "one captured event");
	eq(w->cultures[size_t(A)].capital, sid[0], "A keeps its capital");
	near(S(0).ark, 0.5, 0, "A's Ark untouched");
	eq(w->cultures[size_t(B)].capital, sid[1], "B's first town is its capital");
	eq(capitalIssue(*w), std::string());
	// conquest of the capital: A re-elects (s2, the only one left), the captured capital loses launch site and Ark
	civ::capture(*w, sid[0], B);
	eq(w->cultures[size_t(A)].capital, sid[2], "A re-elected");
	ok(S(0).b[size_t(Building::LAUNCH)] == 0 && S(0).ark == 0, "captured capital: launch and Ark reset");
	eq(w->cultures[size_t(B)].capital, sid[1], "B keeps its capital");
	ok(!S(0).capital, "captured capital is not B's capital");
	eq(capitalIssue(*w), std::string());
	// explicit election moves the capital to the most populous and restarts the Ark of both
	S(1).b[size_t(Building::LAUNCH)] = 1; S(1).ark = 0.3;
	eq(civ::electCapital(*w, B), sid[1], "s1 (500) stays");
	near(S(1).ark, 0.3, 0, "unchanged capital keeps its Ark");
	S(0).pop = 900;
	eq(civ::electCapital(*w, B), sid[0], "s0 (900) wins");
	ok(S(1).ark == 0 && S(1).b[size_t(Building::LAUNCH)] == 0 && !S(1).capital, "old capital reset");
	eq(capitalIssue(*w), std::string());
	// the last of A dies: A has no capital
	civ::destroy(*w, sid[2], Cause::Plague);
	eq(w->cultures[size_t(A)].capital, -1, "A without settlements has no capital");
	// a schism back into A: it becomes A's capital
	ev = capture(*w, [&] { civ::setCulture(*w, sid[1], A); });
	if (eq(countEv(ev, Ev::SettlementCaptured), 1)) ok(ev[0].as<EvSettlementCaptured>()->how == CaptureHow::Schism, "how schism");
	eq(w->cultures[size_t(A)].capital, sid[1], "A's capital again");
	eq(w->cultures[size_t(B)].capital, sid[0], "B unchanged");
	eq(capitalIssue(*w), std::string());
	// capture into a dead culture or the same culture: nothing
	w->cultures[size_t(A)].dead = true;
	ev = capture(*w, [&] { civ::capture(*w, sid[0], A); civ::capture(*w, sid[0], B); });
	ok(ev.empty() && S(0).culture == B, "capture into a dead / the same culture does nothing");
}

// ==================================================================== L 6: scale, timings, maps

DM_TEST("civ adv: L 6 with 160 settlements: timings and the owner map (look at it)") {
	auto w = makeWorld({ .L = 6, .seed = 4 });
	const int cus[3] = { state::newCulture(*w).id, state::newCulture(*w).id, state::newCulture(*w).id };
	double tBest = 0, tFound = 0;
	int nBest = 0, made = 0;
	for (int pass = 0; pass < 2; pass++) {
		if (pass == 1) w->tune<CivTune>().crowdHops = 3;          // the default spacing fills up first: pack the rest
		for (int k = 0; k < 400 && made < 160; k++) {
			double t0 = nowMs();
			const int c = civ::bestSite(*w);
			tBest += nowMs() - t0; nBest++;
			if (c < 0) break;
			t0 = nowMs();
			Settlement* s = civ::found(*w, c, cus[made % 3], 40 + made);
			tFound += nowMs() - t0;
			if (!s) { fail("bestSite cell refused by found"); break; }
			made++;
		}
		if (pass == 0) std::printf("    default spacing (crowdHops 7) fits %d settlements on seed 4 (L 6)\n", made);
	}
	eq(made, 160, "160 settlements");
	const int cellAtCap = civ::bestSite(*w);
	ok(cellAtCap < 0 || civ::found(*w, cellAtCap, cus[0], 30) == nullptr, "the 161st is refused");
	Rng r(8);
	for (Settlement& s : w->settlements) s.radius = r.integer(2, 6);
	double tClaim = 0;
	for (int round = 0; round < 2; round++) {
		const double t0 = nowMs();
		for (size_t i = 0; i < w->settlements.size(); i++) civ::claim(*w, int(i));
		tClaim += nowMs() - t0;
	}
	eq(territoryIssue(*w), std::string(), "consistent");
	double t0 = nowMs();
	int land = 0, okN = 0;
	for (int c = 0; c < w->n; c++) if (!state::isSea(*w, c)) { land++; okN += civ::canSettle(*w, c).ok; }
	const double tCan = nowMs() - t0;
	t0 = nowMs();
	double acc = 0;
	for (int c = 0; c < w->n; c++) if (!state::isSea(*w, c)) acc += civ::siteScore(*w, c);
	const double tScore = nowMs() - t0;
	ok(std::isfinite(acc));
	t0 = nowMs();
	size_t nn = 0;
	for (int k = 0; k < 2000; k++) nn += civ::near(*w, r.integer(0, w->n - 1), 0.3).size();
	const double tNear = nowMs() - t0;
	t0 = nowMs();
	for (int k = 0; k < 2000; k++) nn += size_t(civ::at(*w, r.integer(0, w->n - 1)) + 1);
	const double tAt = nowMs() - t0;
	t0 = nowMs();
	for (int k = 0; k < 20; k++) civ::rebuild(*w);
	const double tRebuild = (nowMs() - t0) / 20;
	ok(mi::owner(*w, shot("ue-civ-adv-l6-owner160.png")), "owner map");
	// borders: per settlement shade, borders dark, centres white
	std::set<int> centres;
	for (const Settlement& s : w->settlements) centres.insert(s.cell);
	mi::Opts o;
	o.rivers = false;
	mi::custom(*w, shot("ue-civ-adv-l6-borders.png"), [&](int c) {
		if (centres.count(c)) return mi::Rgb{ 255, 255, 255 };
		if (state::isSea(*w, c)) return mi::Rgb{ 20, 40, 90 };
		const int ow = w->owner[size_t(c)];
		if (ow < 0) return mi::Rgb{ 70, 80, 60 };
		const auto& col = w->cultures[size_t(w->settlements[size_t(ow)].culture)].color;
		double f = 0.55 + 0.45 * double(hash32(uint32_t(ow) * 2654435761u) % 1000) / 1000.0;
		for (int k = 0; k < w->geo->nbrN[size_t(c)]; k++) {
			const int m = w->geo->nbr[size_t(6 * c + k)];
			if (m >= 0 && !state::isSea(*w, m) && w->owner[size_t(m)] != ow) { f *= 0.45; break; }
		}
		return mi::Rgb{ col[0] * 255 * f, col[1] * 255 * f, col[2] * 255 * f };
	}, o);
	// destruction and conquest at scale
	t0 = nowMs();
	for (int k = 0; k < 40; k++) civ::destroy(*w, k * 4, Cause::War);
	const double tDestroy = (nowMs() - t0) / 40;
	t0 = nowMs();
	for (int k = 0; k < 40; k++) civ::capture(*w, k * 4 + 1, cus[(k + 1) % 3]);
	const double tCapture = (nowMs() - t0) / 40;
	eq(territoryIssue(*w), std::string(), "consistent after destruction");
	eq(capitalIssue(*w), std::string(), "capitals after conquest");
	noNaN(*w, "L 6");
	const double claimMs = tClaim / (2.0 * double(w->settlements.size()));
	std::printf("    L6: bestSite %.2f ms mean (%d calls), found %.3f ms, claim %.4f ms (mixed radii, 160 neighbours), canSettle %.2f us over %d land cells (%d ok), siteScore %.2f us, near(0.3) %.2f us, at %.3f us, rebuild %.3f ms, destroy %.3f ms, capture %.3f ms\n",
		tBest / nBest, nBest, tFound / made, claimMs, 1000 * tCan / land, land, okN, 1000 * tScore / land, 1000 * tNear / 2000, 1000 * tAt / 2000, tRebuild, tDestroy, tCapture);
	ok(claimMs < 0.5, "claim well inside Economy's 3 ms / 100 settlements yearly budget");
	ok(1000 * tCan / land < 50, "canSettle cheap enough for expansion searches");
	ok(tBest / nBest < 30, "bestSite under one tick's worst-case budget");
}
