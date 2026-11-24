// DEMIURGE — SimTests/test_climate_v4.cpp : Climate's mythos reads (SPEC-MYTHOS 1.4, 3.1, 3.13; track M6
// economy-world-v4): the archetype's moistBias and noiseK, and Factors::co2Uptake (the World rite).
// Runs with every other module stubbed (`run.ps1 climate`); Mythos's factors / archetypeParams are honest reads
// of w.mythos.factors and MythosTune in the stub and the real module alike. The CO2 tests need life::cover to
// answer a vegetation share: they install a hook while Life is a stub and use the real Life otherwise.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "Climate.h"
#include "Life.h"
#include "Mythos.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;

namespace {

std::unique_ptr<World> climWorld(Archetype a, uint32_t seed = 1) {
	auto w = makeWorld({ .L = 5, .seed = seed, .land = 0.4, .basics = false, .sea = 0, .phase = Phase::History });
	w->settings.archetype = a;
	return w;
}

bool sameBits(const std::vector<float>& a, const std::vector<float>& b) {
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); i++) if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0) return false;
	return true;
}

// a world whose land is owned by one settlement of culture 0 (land cells with even id) or nobody
std::unique_ptr<World> ownedWorld(double ownedShare) {
	auto w = makeWorld({ .L = 4, .seed = 2, .land = 0.4, .basics = true, .sea = 0, .phase = Phase::History });
	Culture& cu = state::newCulture(*w);
	const int cid = cu.id;
	const int c0 = findLand(*w);
	Settlement& s = state::newSettlement(*w, c0, cid);
	s.pop = 100;
	const int sid = s.id;
	int land = 0, owned = 0;
	for (int c = 0; c < w->n; c++) {
		if (state::isSea(*w, c)) continue;
		land++;
		if (owned < ownedShare * land) { w->owner[size_t(c)] = int16_t(sid); owned++; }
	}
	w->co2 = 200;
	w->vars.seaBase = 0;
	return w;
}

bool hookVeg(double veg) {
	if (!stub::stubbed("life")) return false;
	stub::hooks().life_cover = [veg](const World&) { life::Cover cv{}; cv.land = 0.4; cv.veg = veg; cv.forest = 0.3; return cv; };
	return true;
}

} // namespace

DM_TEST("climate v4: Balanced (and a neutral archetype row) leaves temp, moist and ice bit-identical") {
	auto a = climWorld(Archetype::Balanced);
	climate::calibrate(*a);
	climate::update(*a);
	auto b = climWorld(Archetype::Generous);
	b->tune<MythosTune>().archetypes[size_t(Archetype::Generous)] = ArchetypeParams{};
	climate::calibrate(*b);
	climate::update(*b);
	ok(sameBits(a->temp, b->temp), "temp");
	ok(sameBits(a->moist, b->moist), "moist");
	ok(sameBits(a->ice, b->ice), "ice");
	b->settings.archetype = Archetype::Balanced;                        // the setting itself is saved: compare the rest
	eq(hash(*a), hash(*b), "fingerprint");
}

DM_TEST("climate v4: Generous adds moistBias (+0.05) to every land cell's moist before the clamp; the sea is untouched") {
	auto a = climWorld(Archetype::Balanced);
	auto b = climWorld(Archetype::Generous);
	climate::calibrate(*a); climate::update(*a);
	climate::calibrate(*b); climate::update(*b);
	const double bias = b->tune<MythosTune>().archetypes[size_t(Archetype::Generous)].moistBias;
	near(bias, 0.05, 1e-12, "the spec's starting value");
	int land = 0, bad = 0;
	double mA = 0, mB = 0;
	for (int c = 0; c < a->n; c++) {
		const double ma = a->moist[size_t(c)], mb = b->moist[size_t(c)];
		if (state::isSea(*a, c)) { if (ma != mb) bad++; continue; }
		land++; mA += ma; mB += mb;
		const double want = std::min(1.0, std::max(0.0, ma + bias));
		if (ma > 0 && ma < 1 && std::fabs(mb - want) > 1e-5) bad++;
	}
	eq(bad, 0, "every unclamped land cell + 0.05, every sea cell unchanged");
	ok(mB / land >= mA / land + 0.04, "mean land moisture higher by about the bias");
	ok(checkFinite(*b).empty(), "finite");
}

DM_TEST("climate v4: Beautiful's noiseK (1.3) makes the temperature and rain wobbles 1.3x stronger (more varied biomes)") {
	auto a = climWorld(Archetype::Balanced);
	auto b = climWorld(Archetype::Beautiful);
	climate::update(*a);
	climate::update(*b);
	const ClimateTune& T = a->tune<ClimateTune>();
	const ClimateScratch& SA = a->scratch<ClimateScratch>();
	const ClimateScratch& SB = b->scratch<ClimateScratch>();
	if (!ok(SA.base.size() == size_t(a->n) && SB.base.size() == size_t(b->n), "caches built")) return;
	double worst = 0, varA = 0, varB = 0;
	for (int c = 0; c < a->n; c++) {
		const double y = a->geo->pos[3 * size_t(c) + 1];
		const double cl = std::sqrt(std::max(0.0, 1 - y * y));
		const double profile = T.tPole + (T.tEquator - T.tPole) * std::pow(cl, T.tShape);
		const double da = SA.base[size_t(c)] - profile, db = SB.base[size_t(c)] - profile;
		worst = std::max(worst, std::fabs(db - 1.3 * da));
		varA += da * da; varB += db * db;
	}
	ok(worst < 1e-3, "base temperature wobble x 1.3 (worst " + std::to_string(worst) + ")");
	ok(varB > 1.6 * varA, "the variance of temperature around the latitude profile grows ~1.69x");
	// the rain-lift wobble too: wob = max(0.4, 1 + rainNoise x n)
	int checked = 0, off = 0;
	for (int c = 0; c < a->n; c++) {
		const double wa = SA.wob[size_t(c)], wb = SB.wob[size_t(c)];
		if (wa <= 0.41 || wb <= 0.41) continue;
		checked++;
		if (std::fabs((wb - 1) - 1.3 * (wa - 1)) > 1e-4) off++;
	}
	ok(checked > a->n / 2 && off == 0, "rain wobble x 1.3");
	// the cache follows a change of archetype (the signature holds the effective noise)
	b->settings.archetype = Archetype::Balanced;
	climate::update(*b);
	ok(sameBits(a->temp, b->temp), "back to Balanced: the same temperatures as a Balanced world");
}

DM_TEST("climate v4: neutral co2Uptake Factors leave the yearly CO2 bit-identical") {
	stub::Guard guard;
	hookVeg(0.5);
	auto a = ownedWorld(0.5);
	auto b = ownedWorld(0.5);
	b->tune<MythosTune>().phase = 1; b->mythos.factors.assign(b->cultures.size(), Factors{});           // stored, all neutral
	for (int y = 0; y < 20; y++) { climate::yearly(*a); climate::yearly(*b); }
	ok(std::memcmp(&a->co2, &b->co2, sizeof(double)) == 0, "co2 to the bit: " + std::to_string(a->co2));
	ok(std::memcmp(&a->globalTemp, &b->globalTemp, sizeof(double)) == 0, "globalTemp to the bit");
}

DM_TEST("climate v4: the World rite's co2Uptake scales the vegetation uptake by the land-weighted mean of the owners") {
	stub::Guard guard;
	const bool hooked = hookVeg(0.5);
	const ClimateTune T{};
	for (const double share : { 1.0, 0.5 }) {
		auto a = ownedWorld(share);
		auto b = ownedWorld(share);
		Factors f{};
		f.co2Uptake = 1.1;
		b->tune<MythosTune>().phase = 1; b->mythos.factors.assign(b->cultures.size(), f);
		const double veg = life::cover(*a).veg;
		const double c0 = a->co2;
		climate::yearly(*a);
		climate::yearly(*b);
		ok(b->co2 < a->co2, "more uptake (share " + std::to_string(share) + ")");
		if (hooked) {
			// mean = share x 1.1 + (1 - share) x 1 over the land cells
			int land = 0, owned = 0;
			for (int c = 0; c < b->n; c++) if (!state::isSea(*b, c)) { land++; owned += b->owner[size_t(c)] >= 0 ? 1 : 0; }
			const double mean = (owned * 1.1 + (land - owned)) / land;
			near(a->co2, c0 - c0 * T.uptake * (0.5 + veg), 1e-9, "neutral: the v3.1 rule");
			near(b->co2, c0 - c0 * T.uptake * (0.5 + veg * mean), 1e-9, "x the mean on the vegetation term");
		}
	}
	// a non-finite factor counts as neutral
	auto w = ownedWorld(1.0);
	Factors bad{};
	bad.co2Uptake = std::nan("");
	w->tune<MythosTune>().phase = 1; w->mythos.factors.assign(w->cultures.size(), bad);
	auto ref = ownedWorld(1.0);
	climate::yearly(*w);
	climate::yearly(*ref);
	ok(std::memcmp(&w->co2, &ref->co2, sizeof(double)) == 0, "a NaN co2Uptake is neutral");
	ok(std::isfinite(w->co2) && std::isfinite(w->globalTemp), "finite");
}
