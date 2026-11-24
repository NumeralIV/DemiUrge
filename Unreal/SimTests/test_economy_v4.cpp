// DEMIURGE — SimTests/test_economy_v4.cpp : Economy's mythos reads (SPEC-MYTHOS 1.4; track M6 economy-world-v4).
// Each Factors / TownFactors field multiplies one tuning constant or a finished amount at one place in
// civ::yearly / civ::capacity; neutral records must reproduce v3.1 to the bit. Runs with every other module
// stubbed (`run.ps1 economy`): Civ's claim / destroy / infect / cure are small reference hooks while Civ is a
// stub (as in test_economy.cpp); TownFactors come from a hook while Mythos is a stub (it answers neutral), so
// those checks run only then. Factors are an honest read of w.mythos.factors in the stub and the real module.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "Civ.h"
#include "Mythos.h"
#include "Path.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

void refClaim(World& w, int sid) {
	if (sid < 0 || size_t(sid) >= w.settlements.size()) return;
	Settlement& s = w.settlements[size_t(sid)];
	if (s.dead || !w.valid(s.cell)) return;
	for (const int32_t c : s.cells) if (w.owner[size_t(c)] == sid) w.owner[size_t(c)] = -1;
	s.cells.clear();
	std::vector<int32_t> ring;
	w.geo->ring(s.cell, s.radius, ring);
	for (const int32_t c : ring) {
		if (state::isSea(w, c)) continue;
		const int o = w.owner[size_t(c)];
		if (o >= 0 && o != sid && !w.settlements[size_t(o)].dead) continue;
		w.owner[size_t(c)] = int16_t(sid);
		s.cells.push_back(c);
	}
	std::sort(s.cells.begin(), s.cells.end());
}
void refDestroy(World& w, int sid, Cause cause) {
	if (sid < 0 || size_t(sid) >= w.settlements.size() || w.settlements[size_t(sid)].dead) return;
	Settlement& s = w.settlements[size_t(sid)];
	s.dead = true; s.deadYear = w.year; s.cause = cause;
	for (const int32_t c : s.cells) if (w.owner[size_t(c)] == sid) w.owner[size_t(c)] = -1;
	s.cells.clear();
}
bool refInfect(World& w, int sid, double sev) {
	if (sid < 0 || size_t(sid) >= w.settlements.size()) return false;
	Settlement& s = w.settlements[size_t(sid)];
	if (s.dead || s.immune > 0) return false;
	s.plague = std::max(s.plague, sev);
	return true;
}
bool refCure(World& w, int sid, double imm) {
	if (sid < 0 || size_t(sid) >= w.settlements.size()) return false;
	Settlement& s = w.settlements[size_t(sid)];
	if (s.dead) return false;
	s.plague = 0; s.immune = imm;
	return true;
}
void installRefs() {
	if (!stub::stubbed("civ")) return;
	auto& h = stub::hooks();
	h.civ_claim = refClaim;
	h.civ_destroy = refDestroy;
	h.civ_infect = refInfect;
	h.civ_cure = refCure;
}

// Flat land everywhere (elev 1, sea level 0): temp 17, moist 0.336 (suit 0.8), fert 0.6, hab 0.5 (as test_economy).
std::unique_ptr<World> uniform(int L = 4) {
	auto w = state::create({ .seed = 7, .L = L, .geo = sharedGeo(L) });
	watch(*w);
	w->phase = Phase::History;
	w->heat = 0;
	w->seaLevel = 0;
	w->vars.seaBase = 0;
	for (int c = 0; c < w->n; c++) {
		const size_t i = size_t(c);
		w->elev[i] = 1; w->temp[i] = 17; w->moist[i] = 0.336f; w->fert[i] = 0.6f; w->hab[i] = 0.5f;
		w->veg[i] = 0; w->fauna[i] = 0; w->fresh[i] = 255; w->coast[i] = 255; w->biome[i] = Biome::GRASSLAND;
		w->slope[i] = 0;
	}
	return w;
}
int culture(World& w, int age = Age::BRONZE) {
	Culture& cu = state::newCulture(w);
	cu.age = age;
	cu.name = "Folk";
	return cu.id;
}
int settle(World& w, int cell, int cu, double pop, int tier = 0) {
	Settlement& s = state::newSettlement(w, cell, cu);
	s.pop = pop;
	s.tier = tier;
	s.radius = w.tune<EconomyTune>().radius[size_t(tier)];
	const int id = s.id;
	refClaim(w, id);
	w.settlements[size_t(id)].K = civ::capacity(w, w.settlements[size_t(id)]);
	return id;
}
// Two towns of one Bronze culture far apart: a growing village and a famished, plague-struck one.
struct Scene {
	std::unique_ptr<World> w;
	int cu = -1, a = -1, b = -1;
};
Scene scene() {
	Scene s;
	s.w = uniform();
	World& w = *s.w;
	s.cu = culture(w);
	s.a = settle(w, 100, s.cu, 50, 1);
	s.b = settle(w, 2000, s.cu, 50, 1);
	const double Kb = w.settlements[size_t(s.b)].K;
	w.settlements[size_t(s.b)].pop = Kb * 1.6;
	w.settlements[size_t(s.b)].plague = 0.5;
	w.settlements[size_t(s.a)].lastExpand = w.year;   // nobody expands in these tests
	w.settlements[size_t(s.b)].lastExpand = w.year;
	return s;
}
bool bitsEq(double x, double y) { return std::memcmp(&x, &y, sizeof x) == 0; }
// (the real Mythos answers Factors{} below phase 1: switch the phase on so a stored record is read)
void setF(World& w, const Factors& f) { w.tune<MythosTune>().phase = 1; w.mythos.factors.assign(w.cultures.size(), f); }

} // namespace

DM_TEST("economy v4: neutral Factors (stored or absent) leave 40 years of civ::yearly bit-identical") {
	stub::Guard guard;
	installRefs();
	Scene x = scene(), y = scene();
	setF(*y.w, Factors{});
	for (int yr = 0; yr < 40; yr++) {
		x.w->year += 1; y.w->year += 1;
		civ::yearly(*x.w);
		civ::yearly(*y.w);
	}
	bool same = true;
	for (size_t i = 0; i < x.w->settlements.size(); i++) {
		const Settlement& p = x.w->settlements[i];
		const Settlement& q = y.w->settlements[i];
		same = same && bitsEq(p.pop, q.pop) && bitsEq(p.K, q.K) && p.famine == q.famine && bitsEq(p.plague, q.plague) && p.tier == q.tier;
	}
	ok(same, "settlements to the bit");
	ok(bitsEq(x.w->cultures[0].science, y.w->cultures[0].science), "science to the bit");
	ok(std::memcmp(x.w->farm.data(), y.w->farm.data(), x.w->farm.size() * sizeof(float)) == 0, "farm[] to the bit");
	ok(std::memcmp(x.w->urban.data(), y.w->urban.data(), x.w->urban.size() * sizeof(float)) == 0, "urban[] to the bit");
}

DM_TEST("economy v4: K x the culture's Factors::K (candidates too) x the town's TownFactors::K (real towns only)") {
	stub::Guard guard;
	installRefs();
	Scene s = scene();
	World& w = *s.w;
	const double K0 = civ::capacity(w, w.settlements[size_t(s.a)]);
	const double Kb0 = civ::capacity(w, w.settlements[size_t(s.b)]);
	Settlement cand;
	cand.cell = 600; cand.culture = s.cu; cand.tier = 0;
	const double C0 = civ::capacity(w, cand);
	Factors f{};
	f.K = 1.2;
	setF(w, f);
	near(civ::capacity(w, w.settlements[size_t(s.a)]), K0 * 1.2, 1e-9 * K0, "a town: x 1.2");
	near(civ::capacity(w, cand), C0 * 1.2, 1e-9 * C0, "a candidate site of that culture: x 1.2");
	if (stub::stubbed("mythos")) {
		const int sid = s.a;
		stub::hooks().mythos_townFactors = [sid](const World&, int id) { TownFactors t{}; if (id == sid) t.K = 1.25; return t; };
		near(civ::capacity(w, w.settlements[size_t(s.a)]), K0 * 1.2 * 1.25, 1e-9 * K0, "x the town's 1.25 (Prosperity)");
		Settlement copy = w.settlements[size_t(s.a)];
		near(civ::capacity(w, copy), K0 * 1.2, 1e-9 * K0, "a copy of the town is a candidate: no town factor");
		near(civ::capacity(w, w.settlements[size_t(s.b)]), Kb0 * 1.2, 1e-9 * Kb0, "other towns: the culture's factor only");
	}
	f.K = std::nan("");
	setF(w, f);
	near(civ::capacity(w, cand), C0, 1e-12, "a NaN factor is neutral");
}

DM_TEST("economy v4: growth r x growthR (culture and town); famine loss x famineLoss x harmTaken; plague deaths x plagueDeath") {
	stub::Guard guard;
	installRefs();
	// growth: pop well below K, one year
	auto grow = [](double fGrowth, double tGrowth) {
		Scene s = scene();
		World& w = *s.w;
		Factors f{};
		f.growthR = fGrowth;
		setF(w, f);
		if (tGrowth != 1 && stub::stubbed("mythos")) {
			const int sid = s.a;
			stub::hooks().mythos_townFactors = [sid, tGrowth](const World&, int id) { TownFactors t{}; if (id == sid) t.growthR = tGrowth; return t; };
		}
		w.year += 1;
		civ::yearly(w);
		stub::hooks().mythos_townFactors = nullptr;
		return w.settlements[size_t(s.a)].growth;
	};
	const double g0 = grow(1, 1), g2 = grow(2, 1);
	ok(g0 > 0, "the village grows");
	near(g2, 2 * g0, 1e-9 * g0, "growthR 2: twice the growth");
	if (stub::stubbed("mythos")) near(grow(2, 1.2), 2.4 * g0, 1e-9 * g0, "x the town's 1.2 (Prosperity)");
	// famine and plague: the famished town loses min(0.12 pop, 0.5 (pop - K)) x factors, and plague x 0.08 x factors
	auto starve = [](const Factors& f, double tHarm, double tPlague) {
		Scene s = scene();
		World& w = *s.w;
		setF(w, f);
		if ((tHarm != 1 || tPlague != 1) && stub::stubbed("mythos")) {
			const int sid = s.b;
			stub::hooks().mythos_townFactors = [sid, tHarm, tPlague](const World&, int id) {
				TownFactors t{};
				if (id == sid) { t.harmTaken = tHarm; t.plagueDeath = tPlague; }
				return t;
			};
		}
		const Settlement& b = w.settlements[size_t(s.b)];
		const double p0 = b.pop, K = b.K, plague = b.plague;
		w.year += 1;
		civ::yearly(w);
		stub::hooks().mythos_townFactors = nullptr;
		struct R { double p0, K, plague, p1; };
		return R{ p0, K, plague, w.settlements[size_t(s.b)].pop };
	};
	const EconomyTune T{};
	auto expect = [&](double p0, double K, double plague, double famineK, double plagueK) {
		double pop = p0;
		pop -= std::min(T.famineLoss * pop, T.famineGap * (pop - K)) * famineK;
		pop -= std::min(pop, plague * T.plagueDeath * plagueK * pop);
		return pop;
	};
	{
		const auto r = starve(Factors{}, 1, 1);
		near(r.p1, expect(r.p0, r.K, r.plague, 1, 1), 1e-9 * r.p0, "neutral: the v3.1 rule");
	}
	{
		Factors f{};
		f.famineLoss = 0.8; f.harmTaken = 0.5; f.plagueDeath = 0.85;
		const auto r = starve(f, 1, 1);
		near(r.p1, expect(r.p0, r.K, r.plague, 0.8 * 0.5, 0.85), 1e-9 * r.p0, "Share x Endurance-like harm; Shelter's plague");
	}
	if (stub::stubbed("mythos")) {
		const auto r = starve(Factors{}, 0.5, 0.5);
		near(r.p1, expect(r.p0, r.K, r.plague, 0.5, 0.5), 1e-9 * r.p0, "the town's harmTaken and plagueDeath (a healer)");
	}
}

DM_TEST("economy v4: farm conversion x farmConvert; research x research (inside the rate clamp); cooldown x expandCooldown") {
	stub::Guard guard(true);
	installRefs();
	// farm[]: a new village's farmed cells rise by farmRate x farmConvert in their first year
	auto firstFarm = [](double k) {
		Scene s = scene();
		World& w = *s.w;
		Factors f{};
		f.farmConvert = k;
		setF(w, f);
		w.year += 1;
		civ::yearly(w);
		double mx = 0;
		for (const int32_t c : w.settlements[size_t(s.a)].cells) mx = std::max(mx, double(w.farm[size_t(c)]));
		return mx;
	};
	const EconomyTune T{};
	near(firstFarm(1), T.farmRate, 1e-6, "neutral: farmRate");
	near(firstFarm(1.2), T.farmRate * 1.2, 1e-6, "Dominion: x 1.2");
	near(firstFarm(0.8), T.farmRate * 0.8, 1e-6, "the World rite: x 0.8");
	// research: one year's science
	auto science = [](double k) {
		Scene s = scene();
		World& w = *s.w;
		Factors f{};
		f.research = k;
		setF(w, f);
		w.year += 1;
		civ::yearly(w);
		return w.cultures[size_t(s.cu)].science;
	};
	const double r0 = science(1), r1 = science(1.1), rBig = science(100);
	ok(r0 > T.rateMin && r0 < T.rateMax, "an unclamped rate");
	near(r1, r0 * 1.1, 1e-9, "x 1.1");
	near(rBig, T.rateMax, 1e-9, "still clamped at rateMax");
	// the settler cooldown: a full town waiting 30 years (cooldown 25) searches; x 2 (50 years) it does not
	auto searches = [](double k) {
		Scene s = scene();
		World& w = *s.w;
		Factors f{};
		f.expandCooldown = k;
		setF(w, f);
		Settlement& a = w.settlements[size_t(s.a)];
		a.pop = std::max(a.K, 60.0);
		a.lastExpand = w.year - 30;
		w.settlements[size_t(s.b)].pop = 1;                                // (b is not a candidate)
		w.settlements[size_t(s.b)].plague = 0;
		const double le0 = a.lastExpand;
		const int before = stub::called("path_reach");
		w.year += 1;
		civ::yearly(w);
		const bool searched = stub::stubbed("path") ? stub::called("path_reach") > before : w.settlements[size_t(s.a)].lastExpand != le0;
		return searched;
	};
	ok(searches(1), "neutral: the town searches for a site");
	ok(!searches(2), "expandCooldown 2: not yet");
}
