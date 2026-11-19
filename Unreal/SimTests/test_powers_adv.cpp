// DEMIURGE — SimTests/test_powers_adv.cpp : adversarial verification of A11 powers (Sim/Powers.cpp).
// Owner: the A11 verifier. Runs in `run.ps1 powers` (everything else stubbed) and `run.ps1 all`
// (everything real; the integration tests at the end run only there).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Chronicle.h"
#include "Civ.h"
#include "Life.h"
#include "MapImg.h"
#include "Powers.h"
#include "Sim.h"
#include "Society.h"
#include "Terrain.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "Units.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;
namespace pw = dm::powers;
namespace mi = dm::mapimg;

namespace {

int mkTown(World& w, int cell, int cu, double pop = 300, int hops = 2) {
	Settlement& s0 = state::newSettlement(w, cell, cu);
	s0.pop = pop;
	const int sid = s0.id;
	std::vector<int32_t> r;
	w.geo->ring(cell, hops, r);
	std::vector<int32_t> cells;
	for (int c : r)
		if (!state::isSea(w, c) && w.owner[size_t(c)] < 0) { w.owner[size_t(c)] = int16_t(sid); cells.push_back(c); }
	w.settlements[size_t(sid)].cells = cells;
	return sid;
}

std::unique_ptr<World> rich(int L = 5, uint32_t seed = 3) {
	auto w = makeWorld({ .L = L, .seed = seed });
	w->god.awe = 5000;
	w->god.aweMax = 1200;
	w->age = Age::SPACE;
	return w;
}

int inland(const World& w, int minCoast = 3) {
	return findLand(w, [&](int c) { return w.coast[size_t(c)] >= minCoast; });
}
int deepSea(const World& w) {
	for (int c = 0; c < w.n; c++) if (w.elev[size_t(c)] < w.seaLevel - 2) return c;
	return -1;
}
int countEv(const std::vector<Event>& ev, Ev t) {
	return int(std::count_if(ev.begin(), ev.end(), [&](const Event& e) { return e.type == t; }));
}
// hold a brush for `frames` frames of 1/fps (first frame dt 0)
int hold(World& w, const char* id, int cell, double ang, int fps, int frames) {
	int ok_ = 0;
	for (int f = 0; f < frames; f++)
		if (pw::use(w, id, cell, { .ang = ang, .dt = f ? 1.0 / fps : 0.0 })) ok_++;
	return ok_;
}

// a fingerprint of everything Powers must NOT write (per the writers table), field by field
struct Snap {
	std::vector<std::vector<double>> cells;   // every cell field
	std::vector<std::string> sett;            // settlements minus blessed / inspire
	std::vector<std::string> cult;
	std::string scal;
};
std::string settKey(const Settlement& s) {
	char b[512];
	std::snprintf(b, sizeof b, "%d %d %d %.9g %d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %d %d %d %d %.9g %d", s.id, s.cell, s.culture, s.pop, s.tier,
		s.K, s.growth, s.devotion, s.love, s.jaded, s.plague, s.immune, s.ark, s.famine, int(s.dead), int(s.capital), s.radius, s.lastExpand, int(s.prayer.has_value()));
	std::string k = b;
	for (int i = 0; i < Building::COUNT; i++) k += char('0' + s.b[i]);
	return k;
}
std::string cultKey(const Culture& c) {
	char b[256];
	std::snprintf(b, sizeof b, "%d %d %d %.9g %d %.9g %.9g %d %d %.9g", c.id, int(c.dead), c.age, c.science, c.capital, c.pop, c.exhaustion,
		int(c.prophet.has_value()), int(c.edict), c.edictSince);
	std::string k = b;
	for (const auto& kv : c.seen) k += " " + kv.first + "=" + std::to_string(kv.second);
	return k;
}
Snap snap(const World& w) {
	Snap s;
	for (int i = 0; i < state::kCellFieldCount; i++) {
		const CellArray a = state::cellArray(w, i);
		std::vector<double> v(a.size);
		for (size_t c = 0; c < a.size; c++) v[c] = a.get(c);
		s.cells.push_back(std::move(v));
	}
	for (const Settlement& x : w.settlements) s.sett.push_back(settKey(x));
	for (const Culture& x : w.cultures) s.cult.push_back(cultKey(x));
	char b[512];
	std::snprintf(b, sizeof b, "%d %.9g %lld %.9g %.9g %.9g %.9g %.9g %d %.9g %.9g %.9g %zu %zu %zu %zu", int(w.phase), w.year, (long long)w.tick, w.heat,
		w.seaLevel, w.globalTemp, w.co2, w.dust, w.age, w.god.aweMax, w.god.mercy, w.god.wrath, w.units.size(), w.disasters.size(),
		w.chronicle.size(), w.rivers.size());
	s.scal = b;
	return s;
}
// names of the cell fields that differ
std::vector<std::string> changedFields(const Snap& a, const Snap& b) {
	std::vector<std::string> out;
	for (int i = 0; i < state::kCellFieldCount; i++) {
		bool diff = false;
		for (size_t c = 0; c < a.cells[size_t(i)].size() && !diff; c++) {
			const double x = a.cells[size_t(i)][c], y = b.cells[size_t(i)][c];
			diff = !(x == y) && !(std::isnan(x) && std::isnan(y));
		}
		if (diff) out.push_back(state::kCellFields[i].name);
	}
	return out;
}

} // namespace

// ================================================================== read-only functions

DM_TEST("powers adv: unlocked / cost / can are read-only: no rng draw, no state change, every phase") {
	stub::Guard guard(false);
	auto w = rich(4);
	const int cu = state::newCulture(*w).id;
	mkTown(*w, inland(*w), cu, 200);
	const uint32_t h0 = hash(*w);
	const uint32_t r0 = w->rng.s;
	int calls = 0;
	for (Phase ph : { Phase::Molten, Phase::Water, Phase::Life, Phase::People, Phase::History, Phase::Ended }) {
		w->phase = ph;
		for (const pw::PowerDef& d : pw::kPowers)
			for (int c = -2; c < w->n + 2; c += 97) {
				(void)pw::unlocked(*w, d.id);
				(void)pw::cost(*w, d.id, 0.1, c);
				(void)pw::can(*w, d.id, c);
				calls++;
			}
	}
	w->phase = Phase::History;
	eq(hash(*w), h0, "world hash unchanged after " + std::to_string(calls) + " read-only calls");
	eq(w->rng.s, r0, "rng not advanced");
}

// ================================================================== ownership

DM_TEST("powers adv: ownership: each power writes only what the writers table gives Powers (collaborators neutral)") {
	stub::Guard guard(false);
	if (!stub::stubbed("civ") || !stub::stubbed("society") || !stub::stubbed("terrain") || !stub::stubbed("life") || !stub::stubbed("disasters")) return;
	for (const pw::PowerDef& d : pw::kPowers) {
		auto w = rich(4);
		const int c0 = state::newCulture(*w).id, c1 = state::newCulture(*w).id;
		const int a = inland(*w);
		mkTown(*w, a, c0, 400);
		society::rel(*w, c0, c1)->war = true;         // so the omen has a target
		w->fire[size_t(a)] = 0.5f;
		w->phase = d.unlock.phaseOnly ? d.unlock.phase : Phase::History;
		const Snap s0 = snap(*w);
		const uint32_t rng0 = w->rng.s;
		const double awe0 = w->god.awe;
		const int target = std::string(d.id) == "awaken" ? -1 : a;
		bool any = false;
		if (d.brush) any = hold(*w, d.id, a, -1, 60, 40) > 0;
		else if (target >= 0) any = pw::use(*w, d.id, target);
		const Snap s1 = snap(*w);
		const std::vector<std::string> ch = changedFields(s0, s1);
		std::vector<std::string> allowed;
		if (std::string(d.id) == "rain") allowed = { "wet", "fert" };
		for (const std::string& f : ch)
			ok(std::find(allowed.begin(), allowed.end(), f) != allowed.end(), std::string(d.id) + " wrote cell field " + f);
		eq(s1.scal, s0.scal, std::string(d.id) + " world scalars / entity counts untouched");
		const bool draws = std::string(d.id) == "lightning" || std::string(d.id) == "quake";   // damage fractions
		if (!draws) eq(w->rng.s, rng0, std::string(d.id) + " draws nothing from w.rng");
		ok(s1.sett == s0.sett, std::string(d.id) + " settlements: only blessed / inspire may change");
		ok(s1.cult == s0.cult, std::string(d.id) + " cultures untouched");
		if (any) ok(w->god.awe <= awe0, std::string(d.id) + " Awe only spent");
		std::printf("    %-9s used=%d cells changed: %s\n", d.id, int(any), show(ch).c_str());
	}
}

// ================================================================== awaken at the settlement cap

DM_TEST("powers adv: awaken while 160 settlements live is refused before a culture is created (no orphan cultures)") {
	stub::Guard guard(true);
	auto w = rich(5, 4);
	w->god.awe = 5000;
	const int cu = state::newCulture(*w).id;
	// 160 live settlements centred on sea-free cells without territory (owner stays -1)
	int made = 0;
	for (int c = 0; c < w->n && made < 160; c += 7)
		if (!state::isSea(*w, c)) { state::newSettlement(*w, c, cu).pop = 50; made++; }
	ok(made == 160, "160 settlements");
	const bool stubCiv = stub::stubbed("civ"), stubSoc = stub::stubbed("society");
	if (stubCiv) {
		stub::hooks().civ_canSettle = [](const World& ww, int c) { return civ::SettleCheck{ !state::isSea(ww, c), state::isSea(ww, c) ? civ::SettleReason::Sea : civ::SettleReason::Ok }; };
		stub::hooks().civ_found = [](World& ww, int c, int cuId, double pop, FoundedBy, const civ::FoundOpts&) -> Settlement* {
			if (int(civ::live(ww).size()) >= 160) return nullptr;   // as the real Civ
			Settlement& s = state::newSettlement(ww, c, cuId); s.pop = pop; return &s;
		};
	}
	if (stubSoc) stub::hooks().society_newCulture = [](World& ww, const society::NewCultureOpts& o) -> Culture* { return &state::newCulture(ww, o.parent); };
	// a target the real canSettle accepts (if any), else any land cell
	int target = -1;
	for (int c = 3; c < w->n && target < 0; c += 5)
		if (!state::isSea(*w, c) && civ::canSettle(*w, c).ok) target = c;
	if (target < 0) target = findLand(*w);
	const size_t nc = w->cultures.size();
	const pw::CanResult cr = pw::can(*w, "awaken", target);
	std::vector<Event> ev = capture(*w, [&] {
		for (int i = 0; i < 5; i++) ok(!pw::use(*w, "awaken", target), "awaken refused at the cap");
	});
	eq(w->cultures.size(), nc, "no culture created by 5 refused awakens");
	eq(countEv(ev, Ev::CultureBorn), 0, "no culture:born");
	ok(!cr.ok && cr.reason == pw::Reason::Target, std::string("can: target, got ") + pw::toStr(cr.reason) + " / " + pw::toStr(cr.why));
	if (stubCiv || civ::canSettle(*w, target).ok) eq(int(cr.why), int(pw::Why::Crowded), "why crowded at the cap");
	// one dies: awaken works again
	w->settlements[5].dead = true;
	if (stubCiv && stubSoc) {
		ok(pw::use(*w, "awaken", target), "159 live: awaken works");
		eq(w->cultures.size(), nc + 1, "one new culture");
	}
}

// ================================================================== frame-rate independence

DM_TEST("powers adv: brush totals are frame-rate independent (20..240 fps and jittered dt), 5 s strokes") {
	stub::Guard guard(true);
	struct Tot { int used = 0; double spent = 0, wet = 0, fert = 0, faith = 0; };
	static std::vector<double> faiths;
	auto run = [&](const char* id, int fps, bool jitter) {
		Tot t;
		auto w = rich(5);
		const int cu = state::newCulture(*w).id;
		const int a = inland(*w, 4);
		std::vector<int32_t> r;
		w->geo->ring(a, 3, r);
		mkTown(*w, r.back(), cu, 300, 1);
		faiths.clear();
		if (stub::stubbed("society"))
			stub::hooks().society_witness = [](World&, int, double, const society::WitnessOpts& o) { faiths.push_back(o.faith); return 1; };
		double wet0 = 0, fert0 = 0;
		for (int c = 0; c < w->n; c++) { wet0 += w->wet[size_t(c)]; fert0 += w->fert[size_t(c)]; }
		const double awe0 = w->god.awe;
		Rng jr(99);
		double held = 0;
		auto ev = capture(*w, [&] {
			pw::use(*w, id, a, { .ang = 0.1, .dt = 0 });
			while (held < 5 - 1e-9) {
				double dt = jitter ? jr.range(1.0 / 200, 1.0 / 25) : 1.0 / fps;
				if (held + dt > 5) dt = 5 - held;
				held += dt;
				pw::use(*w, id, a, { .ang = 0.1, .dt = dt });
			}
		});
		t.used = countEv(ev, Ev::PowerUsed);
		t.spent = awe0 - w->god.awe;
		for (int c = 0; c < w->n; c++) { t.wet += w->wet[size_t(c)]; t.fert += w->fert[size_t(c)]; }
		t.wet -= wet0;
		t.fert -= fert0;
		for (double f : faiths) t.faith += f;
		noNaN(*w, id);
		return t;
	};
	for (const char* id : { "rain", "lower", "bloom" }) {
		const Tot ref = run(id, 60, false);
		std::printf("    %-5s 60 fps: used %d spent %.3f wet %.3f fert %.4f faith %.5f\n", id, ref.used, ref.spent, ref.wet, ref.fert, ref.faith);
		for (int fps : { 20, 45, 144, 240, 0 }) {
			const Tot t = run(id, fps ? fps : 60, fps == 0);
			const std::string tag = std::string(id) + (fps ? " " + std::to_string(fps) + " fps" : " jitter");
			std::printf("    %-5s %-8s: used %d spent %.3f wet %.3f fert %.4f faith %.5f\n", id, fps ? std::to_string(fps).c_str() : "jitter", t.used, t.spent, t.wet, t.fert, t.faith);
			ok(std::abs(t.used - ref.used) <= 1 && t.used <= 21, tag + " power:used count " + std::to_string(t.used));
			near(t.spent, ref.spent, ref.spent * 0.05, tag + " Awe spent");
			if (ref.wet > 0) near(t.wet, ref.wet, ref.wet * 0.05, tag + " wet");
			if (ref.fert > 0) near(t.fert, ref.fert, ref.fert * 0.05, tag + " fert");
			if (stub::stubbed("society")) near(t.faith, ref.faith, ref.faith * 0.05, tag + " faith");
		}
	}
}

// ================================================================== events

DM_TEST("powers adv: power:used payloads (id, cell, ang = effective radius, brush flag) and refusals emit nothing") {
	stub::Guard guard(true);
	auto w = rich(5);
	const int cu = state::newCulture(*w).id;
	const int a = inland(*w);
	mkTown(*w, a, cu, 500);
	for (const pw::PowerDef& d : pw::kPowers) {
		w->phase = d.unlock.phaseOnly ? d.unlock.phase : Phase::History;
		auto ev = capture(*w, [&] {
			if (d.brush) hold(*w, d.id, a, 0.11, 60, 20);
			else pw::use(*w, d.id, a);
		});
		for (const Event& e : ev) {
			const EvPowerUsed* p = e.as<EvPowerUsed>();
			if (!p) continue;
			eq(p->id, std::string(d.id), "id");
			eq(p->cell, a, std::string(d.id) + " cell");
			eq(p->brush, d.brush, std::string(d.id) + " brush flag");
			near(p->ang, d.brush ? 0.11 : d.ang, 1e-12, std::string(d.id) + " ang");
		}
	}
	// a refused use emits nothing but (rate-limited) awe:short
	w->phase = Phase::History;
	w->god.awe = 0;
	auto ev = capture(*w, [&] { pw::use(*w, "meteor", a); pw::use(*w, "heal", deepSea(*w)); pw::use(*w, "nope", a); });
	ok(ev.size() == 1 && ev[0].type == Ev::AweShort, "only awe:short: " + std::to_string(ev.size()) + " events");
}

// ================================================================== hostile worlds

DM_TEST("powers adv: NaN fields, dead entities, poles, no sea, no land, L 4 and 6: never NaN, never a crash") {
	stub::Guard guard(false);
	const double nan = std::nan("");
	for (int L : { 4, 6 }) {
		for (int mode = 0; mode < 3; mode++) {        // 0 normal, 1 no sea, 2 no land
			auto w = rich(L, 11);
			const int cu = state::newCulture(*w).id;
			const int a = inland(*w);
			if (a >= 0) mkTown(*w, a, cu, 300);
			// a dead settlement that still claims cells, a settlement of a dead culture, a unit without a path
			if (a >= 0) {
				const int b = findLand(*w, [&](int c) { return w->geo->angle(c, a) > 0.3; });
				if (b >= 0) { const int sb = mkTown(*w, b, cu, 50); w->settlements[size_t(sb)].dead = true; }
			}
			Unit& u = state::newUnit(*w, UnitType::Army);
			u.culture = 77;
			if (mode == 1) { w->seaLevel = -100; fillBasics(*w); }
			if (mode == 2) { w->seaLevel = 100; fillBasics(*w); }
			const int np = w->geo->cellAt(0, 1, 0), sp = w->geo->cellAt(0, -1, 0);
			// NaN in the fields Powers reads
			for (int c : { np, sp, a }) if (w->valid(c)) { w->wet[size_t(c)] = float(nan); w->fert[size_t(c)] = float(nan); w->moist[size_t(c)] = float(nan); }
			int nanBefore = 0;
			for (int c = 0; c < w->n; c++) nanBefore += std::isnan(w->wet[size_t(c)]) + std::isnan(w->fert[size_t(c)]);
			for (Phase ph : { Phase::Molten, Phase::Water, Phase::Life, Phase::People, Phase::History, Phase::Ended }) {
				w->phase = ph;
				for (const pw::PowerDef& d : pw::kPowers)
					for (int c : { np, sp, a, 0, w->n - 1 }) {
						if (d.brush) hold(*w, d.id, c, 0.15, 30, 6);
						else pw::use(*w, d.id, c, { .ang = 0.2, .dt = 0.1 });
						w->god.awe = 5000;
					}
			}
			int nanAfter = 0;
			for (int c = 0; c < w->n; c++) nanAfter += std::isnan(w->wet[size_t(c)]) + std::isnan(w->fert[size_t(c)]);
			const std::string tag = "L " + std::to_string(L) + " mode " + std::to_string(mode);
			ok(nanAfter <= nanBefore, tag + ": Powers wrote no new NaN (" + std::to_string(nanBefore) + " -> " + std::to_string(nanAfter) + ")");
			// clear the injected ones and check the whole world
			for (int c = 0; c < w->n; c++) {
				if (std::isnan(w->wet[size_t(c)])) w->wet[size_t(c)] = 0;
				if (std::isnan(w->fert[size_t(c)])) w->fert[size_t(c)] = 0.5f;
				if (std::isnan(w->moist[size_t(c)])) w->moist[size_t(c)] = 0.5f;
			}
			eq(checkFinite(*w), std::string(""), tag + " finite");
			ok(std::isfinite(w->god.awe) && w->god.awe >= 0, tag + " awe");
		}
	}
}

// ================================================================== performance

DM_TEST("powers adv: L 6 cost of use() and of a HUD poll with 160 settlements and 6 cultures") {
	stub::Guard guard(false);
	auto w = rich(6, 5);
	std::vector<int> cus;
	for (int i = 0; i < 6; i++) cus.push_back(state::newCulture(*w).id);
	int made = 0;
	for (int c = 0; c < w->n && made < 160; c += 211)
		if (!state::isSea(*w, c) && w->owner[size_t(c)] < 0) { mkTown(*w, c, cus[size_t(made % 6)], 500, 3); made++; }
	for (int i = 1; i < 6; i++) society::rel(*w, cus[0], cus[size_t(i)])->war = true;
	const int a = w->settlements[0].cell;
	double hudSum = 0, hudMax = 0;
	for (int f = 0; f < 200; f++) {
		const double t0 = nowMs();
		for (const pw::PowerDef& d : pw::kPowers) { (void)pw::can(*w, d.id, a); (void)pw::cost(*w, d.id, -1, a); (void)pw::unlocked(*w, d.id); }
		const double dt = nowMs() - t0;
		hudSum += dt; hudMax = std::max(hudMax, dt);
	}
	auto brush = [&](const char* id) {
		double sum = 0, mx = 0;
		for (int f = 0; f < 300; f++) {
			const double t0 = nowMs();
			pw::use(*w, id, w->settlements[size_t(f % 40)].cell, { .ang = 0.15, .dt = f ? 1.0 / 60 : 0.0 });
			const double dt = nowMs() - t0;
			sum += dt; mx = std::max(mx, dt);
			w->god.awe = 5000;
		}
		std::printf("    %-6s brush frame (ang 0.15): mean %.4f ms, max %.4f ms\n", id, sum / 300, mx);
		return sum / 300;
	};
	std::printf("    HUD poll (can + cost + unlocked for 16 powers): mean %.4f ms, max %.4f ms\n", hudSum / 200, hudMax);
	ok(hudSum / 200 < 2.5, "HUD poll well inside a tick");
	for (const char* id : { "rain", "raise", "bloom" }) ok(brush(id) < 2.5, std::string(id) + " brush frame < 2.5 ms");
	for (const char* id : { "lightning", "quake", "plague", "heal", "inspire", "prophet", "peace", "meteor" }) {
		const double t0 = nowMs();
		int k = 0;
		for (int i = 0; i < 20; i++) { if (pw::use(*w, id, w->settlements[size_t(i)].cell)) k++; w->god.awe = 5000; }
		const double ms = (nowMs() - t0) / 20;
		std::printf("    %-9s click: %.4f ms (%d of 20 accepted)\n", id, ms, k);
		ok(ms < 30, std::string(id) + " click < 30 ms");
	}
}

// ================================================================== determinism

DM_TEST("powers adv: determinism: a scripted session through every phase gives the same hash (L 5)") {
	stub::Guard guard(false);
	auto play = []() {
		auto w = rich(5, 9);
		std::vector<std::string> evs;
		w->bus.onAll([&](const Event& e) { evs.push_back(e.name()); });
		const int a = inland(*w), s = deepSea(*w);
		w->phase = Phase::Water;
		pw::use(*w, "comet", s);
		hold(*w, "raise", a, 0.08, 60, 30);
		w->phase = Phase::Life;
		pw::use(*w, "seed", a, { .ang = 0.2 });
		w->phase = Phase::People;
		pw::use(*w, "awaken", a);
		w->phase = Phase::History;
		const int cu = state::newCulture(*w).id;
		mkTown(*w, a, cu, 800);
		for (int i = 0; i < 30; i++) {
			for (const pw::PowerDef& d : pw::kPowers) {
				const int c = (a + i * 41 + int(d.key)) % w->n;
				if (d.brush) hold(*w, d.id, c, 0.09, 50, 12);
				else pw::use(*w, d.id, d.target == pw::Target::Settlement ? a : c);
			}
			w->tick += 3;
			w->god.awe = 5000;
		}
		uint32_t eh = 2166136261u;
		for (const std::string& n : evs) for (char ch : n) eh = (eh ^ uint8_t(ch)) * 16777619u;
		return std::vector<uint32_t>{ hash(*w), w->rng.s, eh, uint32_t(evs.size()) };
	};
	const auto h1 = play(), h2 = play();
	ok(h1 == h2, "hash / rng / event sequence identical: " + show(h1) + " vs " + show(h2));
}

// ================================================================== the real modules (run.ps1 all)

namespace {
bool allReal() {
	for (const char* m : { "civ", "society", "diplomacy", "terrain", "life", "disasters", "sim", "units", "chronicle" })
		if (stub::stubbed(m)) return false;
	return true;
}
}

DM_TEST("powers adv: real modules: first awaken in People starts history; second awaken; peace omen; chronicle counts") {
	stub::Guard guard(false);
	if (!allReal()) return;
	auto w = sim::create({ .seed = 21, .L = 5 });
	watch(*w);
	sim::setPhase(*w, Phase::Water);
	sim::setPhase(*w, Phase::Life);
	sim::setPhase(*w, Phase::People);
	w->god.awe = 5;
	// a genesis the comets would have made: sea at 0, then life everywhere, so there is habitable land
	w->seaLevel = 0;
	sim::rederive(*w);
	life::fill(*w, 1);
	sim::rederive(*w);
	const int site = civ::bestSite(*w);
	if (!ok(site >= 0, "a site")) return;
	const pw::CanResult c0 = pw::can(*w, "awaken", site);
	ok(c0.ok, std::string("can awaken: ") + pw::toStr(c0.reason) + " / " + pw::toStr(c0.why));
	ok(pw::use(*w, "awaken", site), "awaken");
	eq(int(w->phase), int(Phase::History), "history began (via sim::setPhase)");
	near(w->god.awe, 60, 1e-9, "Awe 60");
	eq(int(civ::live(*w).size()), 1, "one camp");
	if (civ::live(*w).size() == 1) eq(w->settlements[size_t(civ::live(*w)[0])].pop, 30.0, "of 30");
	eq(int(w->cultures.size()), 1, "one culture");
	// a second people far away (aweMax 450 unlocks)
	w->god.aweMax = 1200;
	w->god.awe = 2000;
	int far = -1;
	for (int c = 0; c < w->n; c++)
		if (w->geo->angle(c, site) > 1.0 && civ::canSettle(*w, c).ok && (far < 0 || w->hab[size_t(c)] > w->hab[size_t(far)])) far = c;
	if (!ok(far >= 0 && pw::use(*w, "awaken", far), "second awaken")) return;
	near(w->god.awe, 2000 - 250, 1e-9, "250 spent");
	const int cA = 0, cB = 1;
	society::declareWar(*w, cA, cB);
	// grow both a little so they have several towns
	for (int i = 0; i < 4; i++) {
		const int extra = findLand(*w, [&](int c) { return civ::canSettle(*w, c).ok && w->geo->angle(c, i % 2 ? far : site) < 0.6; });
		if (extra >= 0) civ::found(*w, extra, i % 2 ? cB : cA, 200, FoundedBy::Settlers);
	}
	const size_t chron0 = w->chronicle.size();
	const double mercy0 = w->god.mercy;
	auto ev = capture(*w, [&] { ok(pw::use(*w, "peace", site), "omen of peace"); });
	ok(!society::atWar(*w, cA, cB), "the war ended");
	const Relation* r = society::peek(*w, cA, cB);
	if (r) near(r->v, 0.3, 1e-9, "relations -0.1 + 0.4");
	if (r) near(r->truce, w->year + 80, 1e-9, "80-year truce");
	int peaceEntries = 0;
	for (size_t i = chron0; i < w->chronicle.size(); i++) if (w->chronicle[i].type == "miracle-peace") peaceEntries++;
	std::printf("    omen of peace over %d live towns: %d miracle:witnessed events, %d 'miracle-peace' chronicle entries, mercy +%.3f (one act)\n",
		int(civ::live(*w).size()), countEv(ev, Ev::MiracleWitnessed), peaceEntries, w->god.mercy - mercy0);
	noNaN(*w, "after the omen");
}

DM_TEST("powers adv: real modules at L 6: meteor, volcano, sea quake, lightning, rain on fire, drowning by lowering; maps") {
	stub::Guard guard(false);
	if (!allReal()) return;
	auto w = sim::create({ .seed = 8, .L = 6 });
	watch(*w);
	if (!ok(sim::genesis::skip(*w), "genesis skip")) return;
	w->god.awe = 5000;
	w->god.aweMax = 1200;
	w->settings.calamity = 0;
	const std::vector<float> elev0 = w->elev;
	const std::vector<int> live0 = civ::live(*w);
	if (!ok(!live0.empty(), "people")) return;
	const int home = w->settlements[size_t(live0[0])].cell;
	std::vector<int> marks;
	// meteor 0.2 rad from the town: witnessed at impact
	const int mcell = findLand(*w, [&](int c) { const double g = w->geo->angle(c, home); return g > 0.18 && g < 0.25; });
	std::vector<Event> ev = capture(*w, [&] {
		ok(pw::use(*w, "meteor", mcell), "meteor launched");
		sim::advance(*w, 16);
	});
	marks.push_back(mcell);
	eq(countEv(ev, Ev::MeteorImpact), 1, "meteor impact within 16 ticks");
	int metWit = 0;
	for (const Event& e : ev) if (const EvMiracleWitnessed* m = e.as<EvMiracleWitnessed>()) if (m->kind == "meteor") metWit++;
	eq(metWit, 1, "meteor witnessed once, at impact");
	ok(w->elev[size_t(mcell)] < elev0[size_t(mcell)] - 0.5, "crater at the impact: " + std::to_string(w->elev[size_t(mcell)] - elev0[size_t(mcell)]));
	// volcano far from everyone
	const int vcell = findLand(*w, [&](int c) { return w->coast[size_t(c)] >= 4 && w->geo->angle(c, home) > 0.8; });
	w->god.aweMax = 1200;      // society::yearly recomputed it during the advance
	w->god.awe = 5000;
	{
		const pw::CanResult vc = pw::can(*w, "volcano", vcell), qc = pw::can(*w, "quake", 5);
		std::printf("    after the advance: aweMax %.0f age %d; can volcano %s/%s, quake %s\n", w->god.aweMax, w->age, pw::toStr(vc.reason), pw::toStr(vc.why), pw::toStr(qc.reason));
	}
	ev = capture(*w, [&] { ok(pw::use(*w, "volcano", vcell), "volcano"); });
	marks.push_back(vcell);
	eq(countEv(ev, Ev::VolcanoStart), 1, "volcano:start");
	ok(w->elev[size_t(vcell)] > elev0[size_t(vcell)] + 0.5, "cone built once: +" + std::to_string(w->elev[size_t(vcell)] - elev0[size_t(vcell)]));
	ok(w->elev[size_t(vcell)] < elev0[size_t(vcell)] + 2.5, "not built twice");
	// sea quake -> tsunami
	int qcell = -1;
	for (int c = 0; c < w->n && qcell < 0; c++) if (w->elev[size_t(c)] < w->seaLevel - 2 && w->geo->angle(c, home) > 0.5) qcell = c;
	ev = capture(*w, [&] { ok(pw::use(*w, "quake", qcell), "sea quake"); });
	marks.push_back(qcell);
	eq(countEv(ev, Ev::TsunamiStart), 1, "tsunami:start");
	ok(std::any_of(w->disasters.begin(), w->disasters.end(), [](const Disaster& z) { return z.type == DisasterType::Tsunami; }), "tsunami disaster in flight");
	// lightning next to the town, then rain over a fire
	const double pop0 = w->settlements[size_t(live0[0])].pop;
	const int nb = w->geo->nbr[size_t(6 * home)];
	ok(pw::use(*w, "lightning", nb), "lightning");
	const double loss = 1 - w->settlements[size_t(live0[0])].pop / pop0;
	ok(loss >= 0.049 && loss <= 0.121, "town lost 5-12 %: " + std::to_string(loss));
	const int fcell = findLand(*w, [&](int c) { return w->veg[size_t(c)] > 0.4 && w->geo->angle(c, home) > 0.4 && w->geo->angle(c, home) < 0.7; });
	if (fcell >= 0 && life::ignite(*w, fcell, 1)) {
		hold(*w, "rain", fcell, 0.06, 60, 31);
		sim::advance(*w, 1);
		eq(double(w->fire[size_t(fcell)]), 0.0, "rain put the fire out");
		marks.push_back(fcell);
	}
	// lower the ground under a fresh coastal camp until the sea takes it
	const int ccell = findLand(*w, [&](int c) { return w->coast[size_t(c)] == 1 && civ::canSettle(*w, c).ok && w->elev[size_t(c)] - w->seaLevel < 0.8; });
	if (ccell >= 0) {
		Settlement* cs = civ::found(*w, ccell, w->settlements[size_t(live0[0])].culture, 50, FoundedBy::Settlers);
		const int csid = cs ? cs->id : -1;
		if (csid >= 0) {
			hold(*w, "lower", ccell, 0.05, 60, 60 * 20);
			ok(w->settlements[size_t(csid)].dead && w->settlements[size_t(csid)].cause == Cause::Drowned,
				std::string("lowered camp drowned: dead ") + std::to_string(w->settlements[size_t(csid)].dead) + " cause " + toStr(w->settlements[size_t(csid)].cause) +
				" elev-sea " + std::to_string(w->elev[size_t(ccell)] - w->seaLevel));
			marks.push_back(ccell);
		}
	}
	sim::advance(*w, 40);
	noNaN(*w, "after the session");
	std::vector<float> dE(size_t(w->n));
	for (int c = 0; c < w->n; c++) dE[size_t(c)] = w->elev[size_t(c)] - elev0[size_t(c)];
	const std::string f1 = shot("ue-powersadv-elev-delta.png"), f2 = shot("ue-powersadv-wet.png");
	ok(mi::field(*w, dE, f1, -2, 2, { .rivers = false, .marks = marks }), "elev delta map");
	ok(mi::field(*w, "wet", f2, 0, 1, { .rivers = false, .marks = marks }), "wet map");
	std::printf("    maps %s %s; marks: meteor %d volcano %d quake %d fire %d camp %d\n", f1.c_str(), f2.c_str(), mcell, vcell, qcell, fcell, ccell);
}
