// DEMIURGE — Sim/Economy.cpp : the economy half of the Civ namespace (SPEC-SIM 3.7, economy.js).
// Owner: economy engineer (A8, key "economy"). Built fresh from the spec. Verified 2026-11-19 (A8
// verifier: plague spread radius in hops, NaN fauna per cell, no searches at the settlement cap,
// exact early exits of the settler flood; tests in SimTests/test_economy_adv.cpp).
//
//   civ::capacity(w, s)  K (people) of a settlement or a candidate site: farms, foraging, fishing.
//   civ::yearly(w)       per live settlement (id order): hazards, territory radius / re-claim,
//                        capacity and farm[] / urban[], growth and famine, plague, countdowns,
//                        abandonment, tier gates, buildings, factory CO2; then expansion (at most
//                        EconomyTune::searchesPerYear searches); then research per culture.
//
// Choices the spec leaves open (all numbers in EconomyTune):
//   suit(temp, moist): temperature ramp 0 at suitT[0], 1 across suitT[1]..suitT[2], 0 at suitT[3];
//     water sat((m - suitM[0]) / (suitM[1] - suitM[0])), lowered a little on waterlogged ground
//     (above suitWet); fresh water within irrigateHops irrigates (m >= irrigateMoist).
//   "sea cell in its territory" (port, coastal expansion) = a sea cell bordering the territory
//     (claim keeps only land cells), the same cells that are fished.
//   Fish counts each bordering sea cell once. Only arable cells whose farmPot >= forage are farmed,
//     so lowering their veg never changes K.
//   Famine counts only while pop is more than famineSlack above Keff (the decline converges to Keff
//     from above; without the slack the counter would run forever on a settled population).
//   Plague spread: each live settlement whose centre is within plagueHops hops (grid BFS) of an
//     ill one's centre catches it with chance plagueSpreadP x plague per year (severity
//     plagueSpreadSev x plague); candidates in ascending id per source.
//   Expansion does not search while live settlements + settlers heading for a new site fill
//     CivTune::maxSettlements (civ::found would refuse every party).
//   Buildings are permanent once built (launch is reset by electCapital).
//   "Was at war" (walls) = the culture has been in a relation with war == true at a yearly tick
//     (remembered in vars.num["civ_war<id>"]); the year a culture reached Space in vars.num["civ_space<id>"].
//   Expansion evaluates at most maxCandidates valid sites per search, nearest by path cost first.
//   Sea-rise evacuation: the best site (siteScore) within drownedHops that is land, not a lake,
//     habitable and at least crowdHops from land owned by any OTHER settlement; else the nearest
//     live settlement of the culture; the evacuees travel with NavalMode::Open.
#include "Civ.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "Climate.h"
#include "Hydro.h"
#include "Path.h"
#include "Society.h"
#include "Units.h"

namespace dm {
namespace {

// Private scratch (one per world). Capacity has its own buffers because it is called from inside
// the expansion flood and from other modules (civ::found) at any time.
struct EconomyScratch {
	// capacity()
	std::vector<int32_t> capRing;
	std::vector<uint32_t> capStamp;
	uint32_t capStampId = 0;
	// yearly()
	std::vector<uint8_t> ownDist, destDist, otherDist;
	std::vector<int32_t> queue;
	std::vector<int32_t> ring;
	std::vector<std::pair<double, int>> farmCand;
	std::vector<std::pair<double, int>> waiting;
	std::vector<int32_t> settlerDests;
	std::vector<uint8_t> plagueMark;   // plague spread: cells within plagueHops of the source
};

struct Survey {
	double K = 0;
	bool sea = false;   // a sea cell borders the territory
	bool rock = false;  // rock / snow peak / steep cells in the territory
};

inline int cultureAge(const World& w, int cultureId) {
	const Culture* cu = state::culture(w, cultureId);
	if (!cu) return 0;
	return cu->age < 0 ? 0 : cu->age > Age::SPACE ? int(Age::SPACE) : cu->age;
}

inline int clampTier(int t) { return t < 0 ? 0 : t > 4 ? 4 : t; }

double suit(const EconomyTune& T, const World& w, int c) {
	const double t = w.temp[size_t(c)];
	double m = w.moist[size_t(c)];
	if (!isFinite(t) || !isFinite(m)) return 0;
	if (int(w.fresh[size_t(c)]) <= T.irrigateHops) m = std::max(m, T.irrigateMoist);
	double ts;
	if (t <= T.suitT[0] || t >= T.suitT[3]) ts = 0;
	else if (t < T.suitT[1]) ts = (t - T.suitT[0]) / (T.suitT[1] - T.suitT[0]);
	else if (t <= T.suitT[2]) ts = 1;
	else ts = (T.suitT[3] - t) / (T.suitT[3] - T.suitT[2]);
	const double span = T.suitM[1] - T.suitM[0];
	double ms = span > 0 ? sat((m - T.suitM[0]) / span) : (m >= T.suitM[1] ? 1.0 : 0.0);
	if (m > T.suitWet) ms = std::min(ms, 1 - T.suitWetLoss * (m - T.suitWet));
	const double v = sat(ts) * sat(ms);
	return isFinite(v) ? v : 0;
}

// farmPot and forage of one land cell.
inline void cellYield(const EconomyTune& T, const World& w, int c, double tech, double& farmPot, double& forage) {
	const size_t i = size_t(c);
	const bool arable = w.hab[i] > T.arableHab && !hydro::isLake(w, c);
	farmPot = 0;
	if (arable) {
		const double f = clamp(double(w.fert[i]), 0.0, T.fertCap);
		farmPot = T.farmK * f * suit(T, w, c) * tech;
	}
	forage = T.forageK * (0.6 * clamp(double(w.veg[i]), 0.0, 1.0) + 0.4 * clamp(double(w.fauna[i]), 0.0, 1.0));
	if (!isFinite(farmPot)) farmPot = 0;
	if (!isFinite(forage)) forage = 0;
}

// The territory (or its estimate) and its yield. `cells` empty -> estimate: land cells within
// radius[tier] hops of s.cell not owned by another settlement.
Survey survey(const World& w, const Settlement& s) {
	Survey out;
	if (!w.geo || !w.valid(s.cell)) return out;
	const EconomyTune& T = w.tune<EconomyTune>();
	EconomyScratch& S = w.scratch<EconomyScratch>();
	const Geo& g = *w.geo;
	const int age = cultureAge(w, s.culture);
	const double tech = T.techYield[size_t(age)];
	const double fishTech = std::sqrt(std::max(0.0, std::min(tech, T.fishTechCap)));
	const double port = (s.b[Building::PORT] != 0) ? 1.0 : 0.0;

	if (S.capStamp.size() != size_t(w.n)) { S.capStamp.assign(size_t(w.n), 0u); S.capStampId = 0; }
	if (++S.capStampId == 0) { std::fill(S.capStamp.begin(), S.capStamp.end(), 0u); S.capStampId = 1; }
	const uint32_t stamp = S.capStampId;

	const std::vector<int32_t>* cells = &s.cells;
	if (s.cells.empty()) {
		g.ring(s.cell, T.radius[size_t(clampTier(s.tier))], S.capRing);
		size_t k = 0;
		for (size_t j = 0; j < S.capRing.size(); j++) {
			const int c = S.capRing[j];
			if (state::isSea(w, c)) continue;
			const int o = w.owner[size_t(c)];
			if (o >= 0 && o != s.id) continue;
			S.capRing[k++] = c;
		}
		S.capRing.resize(k);
		cells = &S.capRing;
	}
	double land = 0, fish = 0;
	for (const int32_t c : *cells) {
		if (!w.valid(c)) continue;
		S.capStamp[size_t(c)] = stamp;   // territory cells are never fished
	}
	for (const int32_t c : *cells) {
		if (!w.valid(c) || state::isSea(w, c)) continue;
		double farmPot, forage;
		cellYield(T, w, c, tech, farmPot, forage);
		land += std::max(farmPot, forage);
		const uint8_t b = w.biome[size_t(c)];
		if (b == Biome::ROCK || b == Biome::SNOWPEAK || w.slope[size_t(c)] > T.mineSlope) out.rock = true;
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
			const int m = g.nbr[size_t(6 * c + k)];
			if (m < 0 || S.capStamp[size_t(m)] == stamp) continue;
			S.capStamp[size_t(m)] = stamp;
			if (!state::isSea(w, m)) continue;
			out.sea = true;
			const double fa = double(w.fauna[size_t(m)]);
			if (isFinite(fa) && fa > 0) fish += T.fishK * std::min(fa, 1.0) * (1 + port) * fishTech;   // a NaN cell adds nothing
		}
	}
	double K = land + fish;
	if (s.b[Building::GRANARY]) K *= T.granaryK;
	if (s.b[Building::PLANT]) K *= T.plantK;
	// (v3) a blessed town's fields give more; the gift fades over its last blessedFade years, so the
	// end of a blessing is an ebb the people follow, not a famine
	if (s.blessed > 0) K *= 1 + (T.blessedK - 1) * (T.blessedFade > 0 ? std::min(1.0, s.blessed / T.blessedFade) : 1.0);
	out.K = (isFinite(K) && K > 0) ? K : 0;
	return out;
}

// Multi-source BFS distance in hops from `sources`, capped at cap (cells farther stay at cap).
void bfs(const World& w, std::vector<uint8_t>& dist, std::vector<int32_t>& queue, int cap) {
	const Geo& g = *w.geo;
	size_t head = 0;
	while (head < queue.size()) {
		const int c = queue[head++];
		const int d = dist[size_t(c)];
		if (d + 1 >= cap) continue;
		for (int k = 0; k < g.nbrN[size_t(c)]; k++) {
			const int m = g.nbr[size_t(6 * c + k)];
			if (m >= 0 && dist[size_t(m)] > d + 1) { dist[size_t(m)] = uint8_t(d + 1); queue.push_back(m); }
		}
	}
}

// Distance from land owned by any settlement other than `skip` (-1: every owner), capped.
void ownedDistance(const World& w, std::vector<uint8_t>& dist, std::vector<int32_t>& queue, int cap, int skip) {
	dist.assign(size_t(w.n), uint8_t(cap));
	queue.clear();
	for (int c = 0; c < w.n; c++) {
		const int o = w.owner[size_t(c)];
		if (o >= 0 && o != skip) { dist[size_t(c)] = 0; queue.push_back(c); }
	}
	bfs(w, dist, queue, cap);
}

std::string numKey(const char* prefix, int id) { return std::string(prefix) + std::to_string(id); }

void setTier(World& w, int sid, int tier) {
	Settlement& s = w.settlements[size_t(sid)];
	const int prev = s.tier;
	if (prev == tier) return;
	s.tier = tier;
	s.radius = w.tune<EconomyTune>().radius[size_t(clampTier(tier))];
	w.dirty.objects = true;
	w.dirty.color = true;
	w.bus.emit(EvSettlementTier{ sid, tier, prev });
	civ::claim(w, sid);
}

void build(World& w, int sid, Building::Id b) {
	Settlement& s = w.settlements[size_t(sid)];
	if (s.b[size_t(b)]) return;
	s.b[size_t(b)] = 1;
	w.dirty.objects = true;
	w.bus.emit(EvSettlementBuilding{ sid, b });
}

// farm[] and urban[] for one settlement's territory after K is known.
void tendLand(World& w, int sid) {
	const EconomyTune& T = w.tune<EconomyTune>();
	EconomyScratch& S = w.scratch<EconomyScratch>();
	Settlement& s = w.settlements[size_t(sid)];
	const Geo& g = *w.geo;
	const double tech = T.techYield[size_t(cultureAge(w, s.culture))];
	// farms: the best arable cells by farmPot until their sum reaches farmTarget x pop
	S.farmCand.clear();
	for (const int32_t c : s.cells) {
		if (!w.valid(c) || state::isSea(w, c) || w.owner[size_t(c)] != sid) continue;
		double farmPot, forage;
		cellYield(T, w, c, tech, farmPot, forage);
		if (farmPot > 0 && farmPot >= forage) S.farmCand.emplace_back(-farmPot, c);
		else if (w.farm[size_t(c)] > 0) {
			w.farm[size_t(c)] = float(std::max(0.0, double(w.farm[size_t(c)]) - T.farmRelax));
			w.dirty.color = true;
		}
	}
	std::sort(S.farmCand.begin(), S.farmCand.end());   // best first, ties by cell id
	const double need = T.farmTarget * std::max(0.0, s.pop);
	double acc = 0;
	for (const auto& fc : S.farmCand) {
		const size_t i = size_t(fc.second);
		const double f0 = w.farm[i];
		double f1;
		if (acc < need) {
			acc += -fc.first;
			f1 = std::min(1.0, f0 + T.farmRate);
			const double v0 = w.veg[i];
			const double v1 = std::min(v0, std::max(0.0, 1 - f1));
			if (v1 < v0) {
				for (const double th : state::kVegT) if (v0 >= th && v1 < th) w.dirty.objects = true;
				w.veg[i] = float(v1);
			}
		} else {
			f1 = std::max(0.0, f0 - T.farmRelax);
		}
		if (f1 != f0) { w.farm[i] = float(f1); w.dirty.color = true; }
	}
	// urban: centre by tier, rings for cities (0.5) and metropolises (0.8, 0.4); elsewhere in the
	// territory urban fades like ruins
	const int tier = clampTier(s.tier);
	const double ring1 = tier == 4 ? T.urbanRing[1] : tier == 3 ? T.urbanRing[0] : 0.0;
	const double ring2 = tier == 4 ? T.urbanRing[2] : 0.0;
	auto setUrban = [&](int c, double target) {
		const size_t i = size_t(c);
		const double u0 = w.urban[i];
		double u1 = u0;
		if (u0 < target) u1 = target;
		else if (u0 > target) u1 = std::max(target, u0 - T.urbanFade * u0);
		if (u1 < 0.004) u1 = 0;
		if (u1 != u0) { w.urban[i] = float(u1); w.dirty.color = true; }
	};
	if (!state::isSea(w, s.cell)) setUrban(s.cell, T.urbanCentre[size_t(tier)]);
	g.ring(s.cell, 2, S.ring);
	for (size_t j = 1; j < S.ring.size(); j++) {
		const int c = S.ring[j];
		if (state::isSea(w, c)) continue;
		const int o = w.owner[size_t(c)];
		if (o != sid && !(o < 0 && s.cells.empty())) continue;
		bool first = false;
		for (int k = 0; k < g.nbrN[size_t(s.cell)]; k++) if (g.nbr[size_t(6 * s.cell + k)] == c) first = true;
		setUrban(c, first ? ring1 : ring2);
	}
	for (const int32_t c : s.cells) {
		if (!w.valid(c) || c == s.cell || state::isSea(w, c) || w.owner[size_t(c)] != sid) continue;
		if (w.urban[size_t(c)] <= 0) continue;
		if (std::find(S.ring.begin(), S.ring.end(), c) != S.ring.end()) continue;
		setUrban(c, 0);
	}
}

// The centre went under the sea: send 60 % of the people away, then destroy (drowned).
void evacuate(World& w, int sid) {
	const EconomyTune& T = w.tune<EconomyTune>();
	const CivTune& CT = w.tune<CivTune>();
	EconomyScratch& S = w.scratch<EconomyScratch>();
	const Geo& g = *w.geo;
	const Settlement& s = w.settlements[size_t(sid)];
	const int culture = s.culture, from = s.cell;
	const double size = T.drownedFrac * std::max(0.0, s.pop);
	ownedDistance(w, S.otherDist, S.queue, std::max(1, CT.crowdHops), sid);
	g.ring(from, T.drownedHops, S.ring);
	int best = -1;
	double bestScore = -1e300;
	for (const int32_t c : S.ring) {
		if (state::isSea(w, c) || hydro::isLake(w, c)) continue;
		if (!(w.hab[size_t(c)] > CT.barrenHab) || w.lava[size_t(c)] > 0.05f) continue;
		if (int(S.otherDist[size_t(c)]) < CT.crowdHops) continue;
		const double sc = civ::siteScore(w, c);
		if (isFinite(sc) && sc > bestScore) { bestScore = sc; best = c; }
	}
	bool sent = false;
	if (size >= 1) {
		if (best >= 0) {
			units::SpawnOpts o;
			o.type = UnitType::Settler; o.culture = culture; o.from = sid; o.to = -1; o.dest = best;
			o.size = size; o.naval = NavalMode::Open;
			sent = units::spawn(w, o) != nullptr;
		}
		if (!sent) {
			int joinId = -1;
			double joinAng = 1e300;
			for (const Settlement& o : w.settlements) {
				if (o.dead || o.id == sid || o.culture != culture || !w.valid(o.cell)) continue;
				const double a = g.angle(from, o.cell);
				if (a < joinAng) { joinAng = a; joinId = o.id; }
			}
			if (joinId >= 0) {
				units::SpawnOpts o;
				o.type = UnitType::Settler; o.culture = culture; o.from = sid; o.to = joinId;
				o.dest = w.settlements[size_t(joinId)].cell; o.size = size; o.naval = NavalMode::Open;
				sent = units::spawn(w, o) != nullptr;
			}
		}
	}
	if (sent) w.settlements[size_t(sid)].pop = std::max(0.0, w.settlements[size_t(sid)].pop - size);
	// (v3, design review finding 7) the sea the people's engines raised took it: not the god's deed
	civ::destroy(w, sid, Cause::Sea);
}

// One settlement's year (it may be destroyed on the way).
void settlementYear(World& w, int sid, const std::vector<uint8_t>& warFlag) {
	const EconomyTune& T = w.tune<EconomyTune>();
	const CivTune& CT = w.tune<CivTune>();
	{
		const Settlement& s = w.settlements[size_t(sid)];
		if (!w.valid(s.cell)) return;
		// hazards
		if (state::isSea(w, s.cell)) { evacuate(w, sid); return; }
		if (w.water[size_t(s.cell)] > T.drownWater) { civ::destroy(w, sid, Cause::Drowned); return; }
		if (w.lava[size_t(s.cell)] > T.lavaKill) { civ::destroy(w, sid, Cause::Lava); return; }
	}
	// territory
	{
		Settlement& s = w.settlements[size_t(sid)];
		const int r = T.radius[size_t(clampTier(s.tier))];
		const long yr = long(std::floor(w.year + 0.5));
		const int every = std::max(1, T.reclaimYears);
		const bool due = ((yr + sid) % every + every) % every == 0;
		if (s.radius != r || s.cells.empty() || due) {
			s.radius = r;
			civ::claim(w, sid);
		}
	}
	// capacity, farms, towns
	{
		const Survey sv = survey(w, w.settlements[size_t(sid)]);
		w.settlements[size_t(sid)].K = sv.K;
		tendLand(w, sid);
		Settlement& s = w.settlements[size_t(sid)];
		// growth and famine
		const double p0 = isFinite(s.pop) ? std::max(0.0, s.pop) : 0.0;
		double pop = p0;
		const double Keff = std::max(s.K, 1.0);
		if (pop <= Keff) {
			const double r = T.growthR * (1 + T.granaryR * s.b[Building::GRANARY] + T.blessedR * (s.blessed > 0 ? 1 : 0)
				+ T.loveR * std::max(0.0, isFinite(s.love) ? s.love : 0.0));
			pop += r * pop * (1 - pop / Keff);
			s.famine = 0;
		} else {
			// (v3, design review finding 9) a blessed town is not starving: the god's gifts carry it while
			// its people fall back to what the land feeds
			const bool hungry = pop > Keff * (1 + T.famineSlack) && !(s.blessed > 0);
			pop -= std::min(T.famineLoss * pop, T.famineGap * (pop - Keff));
			if (hungry) {
				s.famine += 1;
				if (s.famine == T.famineEvent) w.bus.emit(EvSettlementFamine{ sid });
			} else {
				s.famine = 0;
			}
		}
		// plague deaths and fading
		bool cureNow = false;
		if (s.plague > 0) {
			pop -= std::min(pop, s.plague * T.plagueDeath * pop);
			s.plague *= (1 - T.plagueFade);
			if (s.plague < T.plagueCure) cureNow = true;
		}
		if (!isFinite(pop) || pop < 0) pop = 0;
		s.pop = pop;
		s.growth = pop - p0;
		// countdowns
		s.blessed = std::max(0.0, s.blessed - 1);
		s.inspire = std::max(0.0, s.inspire - 1);
		s.immune = std::max(0.0, s.immune - 1);
		if (cureNow) civ::cure(w, sid);
	}
	// abandonment
	{
		const Settlement& s = w.settlements[size_t(sid)];
		if (s.pop < CT.minPeople) {
			const Cause c = s.famine > 0 ? Cause::Famine : (s.plague > 0 ? Cause::Plague : Cause::Abandoned);
			civ::destroy(w, sid, c);
			return;
		}
	}
	// tier gates (one step per year; demotion with hysteresis)
	{
		const Settlement& s = w.settlements[size_t(sid)];
		const int age = cultureAge(w, s.culture);
		const int tier = clampTier(s.tier);
		if (tier < 4 && s.pop >= T.gate[size_t(tier + 1)] && age >= T.gateAge[size_t(tier + 1)]) setTier(w, sid, tier + 1);
		else if (tier > 0 && s.pop < T.demote * T.gate[size_t(tier)]) setTier(w, sid, tier - 1);
	}
	// buildings (DM.BUILDINGS order) and CO2
	{
		const Settlement& s = w.settlements[size_t(sid)];
		const int age = cultureAge(w, s.culture);
		const int tier = s.tier;
		const Survey sv = survey(w, s);   // territory may have changed with the tier
		const double pop = s.pop, dev = s.devotion;
		const bool warred = s.culture >= 0 && size_t(s.culture) < warFlag.size() && warFlag[size_t(s.culture)];
		const Culture* cu = state::culture(w, s.culture);
		const bool isCapital = s.capital && cu && cu->capital == sid;
		if (pop >= T.shrinePop && dev >= T.shrineDevotion) build(w, sid, Building::SHRINE);
		if (age >= Age::BRONZE && tier >= 2 && dev >= T.templeDevotion) build(w, sid, Building::TEMPLE);
		if (age >= Age::BRONZE && tier >= 1) build(w, sid, Building::GRANARY);
		if (age >= Age::BRONZE && tier >= 2 && warred) build(w, sid, Building::WALLS);
		if (age >= Age::IRON && tier >= 1 && sv.sea) build(w, sid, Building::PORT);
		if (age >= Age::IRON && tier >= 2) build(w, sid, Building::MARKET);
		if (age >= Age::IRON && sv.rock) build(w, sid, Building::MINE);
		if (age >= Age::INDUSTRY && tier >= 3) build(w, sid, Building::FACTORY);
		if (age >= Age::SAIL && tier >= 3) build(w, sid, Building::ACADEMY);
		if (age >= Age::ATOMIC && tier >= 3) build(w, sid, Building::PLANT);
		if (age >= Age::SPACE && isCapital) {
			const std::string key = numKey("civ_space", s.culture);
			auto it = w.vars.num.find(key);
			if (it == w.vars.num.end()) it = w.vars.num.emplace(key, w.year).first;
			if (w.year - it->second >= T.launchDelay) build(w, sid, Building::LAUNCH);
		}
		const Settlement& s2 = w.settlements[size_t(sid)];
		if (s2.b[Building::FACTORY]) {
			double amount = T.factoryCo2 * std::sqrt(std::max(0.0, s2.pop) / 1000.0);
			if (age >= Age::SPACE) amount *= T.spaceCo2Mul;
			if (isFinite(amount) && amount > 0) {
				climate::emit(w, amount);
				w.vars.num[numKey("civ_co2_", s2.culture)] += amount;   // (v3) whose engines warmed the air
			}
		}
	}
}

// Settler parties: at most searchesPerYear searches, longest-waiting first.
void expansion(World& w) {
	const EconomyTune& T = w.tune<EconomyTune>();
	EconomyScratch& S = w.scratch<EconomyScratch>();
	S.waiting.clear();
	for (const Settlement& s : w.settlements) {
		if (s.dead || !w.valid(s.cell)) continue;
		const Culture* cu = state::culture(w, s.culture);
		if (!cu || cu->dead) continue;
		// (v3, design review finding 4) the cooldown by age: short in the Stone age, long from Iron
		const int age = cu->age < 0 ? 0 : cu->age > Age::SPACE ? int(Age::SPACE) : cu->age;
		const double base = T.expandCooldownAge[size_t(age)] > 0 ? T.expandCooldownAge[size_t(age)] : T.expandCooldown;
		const double ratio = T.expandCooldown > 0 ? T.expandEdict / T.expandCooldown : 0.5;
		const double cool = cu->edict == Edict::Expand ? base * ratio : base;
		if (s.pop >= T.expandK * s.K && s.pop >= T.expandPop && w.year - s.lastExpand >= cool)
			S.waiting.emplace_back(s.lastExpand, s.id);
	}
	if (S.waiting.empty()) return;
	// (verifier fix) no searches while civ::found would refuse the party: live settlements plus
	// settlers already heading for a new site fill CivTune::maxSettlements. The waiting settlements
	// keep their lastExpand, so the longest-waiting one goes first once a slot frees up.
	int slots = w.tune<CivTune>().maxSettlements;
	for (const Settlement& s : w.settlements) slots -= s.dead ? 0 : 1;
	for (const Unit& u : w.units) slots -= (u.type == UnitType::Settler && u.to < 0) ? 1 : 0;
	if (slots <= 0) return;
	std::sort(S.waiting.begin(), S.waiting.end());
	const size_t searches = std::min({ S.waiting.size(), size_t(std::max(0, T.searchesPerYear)), size_t(slots) });
	if (searches == 0) return;
	// owned-land distance (once per year) and reserved settler destinations
	ownedDistance(w, S.ownDist, S.queue, T.ownDistCap, -1);
	S.destDist.assign(size_t(w.n), uint8_t(T.destHops));
	S.queue.clear();
	for (const Unit& u : w.units)
		if (u.type == UnitType::Settler && w.valid(u.dest) && S.destDist[size_t(u.dest)] != 0) { S.destDist[size_t(u.dest)] = 0; S.queue.push_back(u.dest); }
	bfs(w, S.destDist, S.queue, T.destHops);
	std::vector<std::pair<double, int>> order(S.waiting.begin(), S.waiting.begin() + std::ptrdiff_t(searches));
	for (const auto& wt : order) {
		const int sid = wt.second;
		const Settlement& s = w.settlements[size_t(sid)];
		if (s.dead) continue;
		const int age = cultureAge(w, s.culture);
		const Survey sv = survey(w, s);
		NavalMode naval = NavalMode::Land;
		double maxCost = T.landMaxCost;
		if (age >= Age::SAIL && s.b[Building::PORT]) { naval = NavalMode::Open; maxCost = T.navalMaxCost; }
		else if (age >= Age::BRONZE && sv.sea) naval = NavalMode::Coastal;
		const int culture = s.culture, origin = s.cell;
		int best = -1, evaluated = 0;
		double bestScore = -1e300;
		Settlement cand;
		cand.culture = culture;
		cand.tier = 0;
		const double barren = w.tune<CivTune>().barrenHab;
		// the cheap site filters; (verifier) count the cells that pass them first: none -> no flood at
		// all, and the flood stops once it has seen every one of them (both exact: the result is the
		// same, a crowded late-game planet just stops paying ~4.6 ms per whole-planet naval flood)
		auto passes = [&](int c) {
			return int(S.ownDist[size_t(c)]) >= T.ownDistMin && int(S.destDist[size_t(c)]) >= T.destHops
				&& !state::isSea(w, c) && !hydro::isLake(w, c) && w.hab[size_t(c)] > barren;
		};
		int remaining = 0;
		for (int c = 0; c < w.n; c++) remaining += passes(c) ? 1 : 0;
		if (remaining > 0) path::reach(w, origin, maxCost, naval, [&](int c, double cost) -> bool {
			if (!w.valid(c) || !passes(c)) return true;
			const bool more = --remaining > 0;
			if (!civ::canSettle(w, c).ok) return more;
			cand.cell = c;
			if (!(civ::capacity(w, cand) >= T.campKMin)) return more;
			const double sc = civ::siteScore(w, c) - T.pathW * (isFinite(cost) ? cost : maxCost);
			if (isFinite(sc) && sc > bestScore) { bestScore = sc; best = c; }
			return more && ++evaluated < T.maxCandidates;
		});
		bool sent = false;
		if (best >= 0) {
			const double pop = w.settlements[size_t(sid)].pop;
			const double party = clamp(T.partyFrac * pop, T.partyMin, T.partyMax);
			if (pop - party >= w.tune<CivTune>().minPeople) {
				units::SpawnOpts o;
				o.type = UnitType::Settler; o.culture = culture; o.from = sid; o.to = -1; o.dest = best;
				o.size = party; o.naval = naval;
				if (units::spawn(w, o) != nullptr) {
					Settlement& s2 = w.settlements[size_t(sid)];   // re-fetch: spawn may found settlements
					s2.pop = std::max(0.0, s2.pop - party);
					sent = true;
					// reserve the destination for the second search of this year
					S.queue.clear();
					if (S.destDist[size_t(best)] != 0) { S.destDist[size_t(best)] = 0; S.queue.push_back(best); }
					bfs(w, S.destDist, S.queue, T.destHops);
				}
			}
		}
		Settlement& s3 = w.settlements[size_t(sid)];
		s3.lastExpand = sent ? w.year : w.year - T.failBack;
	}
}

// Research per living culture (id order); culture:age and world:age.
void research(World& w) {
	const EconomyTune& T = w.tune<EconomyTune>();
	const size_t nc = w.cultures.size();
	struct Agg { int n = 0, academy = 0, market = 0, factory = 0; double pop = 0, inspired = 0; };
	std::vector<Agg> agg(nc);
	for (const Settlement& s : w.settlements) {
		if (s.dead || s.culture < 0 || size_t(s.culture) >= nc) continue;
		Agg& a = agg[size_t(s.culture)];
		a.n++;
		a.academy += s.b[Building::ACADEMY] ? 1 : 0;
		a.market += s.b[Building::MARKET] ? 1 : 0;
		a.factory += s.b[Building::FACTORY] ? 1 : 0;
		const double p = std::max(0.0, isFinite(s.pop) ? s.pop : 0.0);
		a.pop += p;
		if (s.inspire > 0) a.inspired += p;
	}
	int top = w.age;
	for (size_t id = 0; id < nc; id++) {
		Culture& cu = w.cultures[id];
		if (cu.dead) continue;
		const Agg& a = agg[id];
		if (a.n > 0 && cu.age < Age::SPACE) {
			const double n = double(a.n);
			const double sizeF = clamp(T.sizeBase + T.sizeBase * std::sqrt(n / T.sizeRef), T.sizeMin, T.sizeMax);
			const double buildF = 1 + T.academyF * (a.academy / n) + T.marketF * (a.market / n) + T.factoryF * (a.factory / n);
			const double cur = isFinite(cu.traits.curiosity) ? clamp(cu.traits.curiosity, 0.0, 1.0) : 0.5;
			const double traitF = T.traitBase + T.traitK * cur;
			const double inspireF = 1 + T.inspireF * (a.pop > 0 ? a.inspired / a.pop : 0.0);
			double catchUp = 1;
			for (size_t o = 0; o < nc; o++) {
				if (o == id || w.cultures[o].dead || w.cultures[o].age < cu.age + 1) continue;
				const Relation* r = society::peek(w, int(id), int(o));
				if (r && r->contact) { catchUp = T.catchUp; break; }
			}
			double rate = clamp(sizeF * buildF * traitF * inspireF * catchUp, T.rateMin, T.rateMax);
			if (!isFinite(rate)) rate = T.rateMin;
			cu.science = (isFinite(cu.science) ? std::max(0.0, cu.science) : 0.0) + rate;
			while (cu.age < Age::SPACE && cu.science >= T.ageNeed[size_t(cu.age)]) {
				cu.science -= T.ageNeed[size_t(cu.age)];
				const int prev = cu.age;
				cu.age++;
				if (cu.age == Age::SPACE) {
					w.vars.num[numKey("civ_space", int(id))] = w.year;
					cu.science = 0;
				}
				w.dirty.objects = true;
				w.bus.emit(EvCultureAge{ int(id), cu.age, prev });
			}
		}
		top = std::max(top, w.cultures[id].age);
	}
	if (top > w.age) {
		w.age = std::min(top, int(Age::SPACE));
		w.bus.emit(EvWorldAge{ w.age });
	}
}

} // namespace

namespace civ {

double capacity(const World& w, const Settlement& s) {
	return survey(w, s).K;
}

double addScience(World& w, int cultureId, double years) {
	Culture* cu = state::culture(w, cultureId);
	if (!cu || cu->dead || !isFinite(years) || years == 0) return 0;
	if (cu->age >= Age::SPACE) return 0;                 // nothing left to learn
	const double before = isFinite(cu->science) ? std::max(0.0, cu->science) : 0.0;
	const double after = std::max(0.0, before + years);
	cu->science = after;
	return after - before;
}

int strikeKnowledge(World& w, const std::vector<int>& harmed, double years) {
	if (!(years > 0) || harmed.empty()) return 0;
	std::vector<int> struck;
	for (const int sid : harmed) {
		const Settlement* s = state::settlement(w, sid);
		if (!s || s->culture < 0) continue;
		const Culture* cu = state::culture(w, s->culture);
		if (!cu || cu->dead) continue;
		const bool capital = cu->capital == sid;
		if (!capital && !s->has(Building::ACADEMY)) continue;
		if (std::find(struck.begin(), struck.end(), s->culture) == struck.end()) struck.push_back(s->culture);
	}
	std::sort(struck.begin(), struck.end());
	for (const int cid : struck) addScience(w, cid, -years);
	return int(struck.size());
}

void yearly(World& w) {
	if (!w.geo || w.n <= 0 || w.geo->n != w.n) return;
	if (w.owner.size() != size_t(w.n)) return;
	// cultures that are or were at war (walls)
	std::vector<uint8_t> warFlag(w.cultures.size(), 0);
	for (const auto& kv : w.vars.rel) {
		if (!kv.second.war) continue;
		for (const int id : { kv.first.first, kv.first.second })
			if (id >= 0 && size_t(id) < w.cultures.size()) w.vars.num[numKey("civ_war", id)] = 1;
	}
	for (size_t id = 0; id < w.cultures.size(); id++)
		if (w.vars.num.count(numKey("civ_war", int(id)))) warFlag[id] = 1;
	const size_t count = w.settlements.size();
	for (size_t sid = 0; sid < count; sid++) {
		if (w.settlements[sid].dead) continue;
		settlementYear(w, int(sid), warFlag);
	}
	// plague spread (after every settlement's own year, so the order of ids does not matter):
	// to every live settlement whose centre is within plagueHops hops of an ill one's centre
	// (verifier fix: was an angle of 8.5 x geo.step, which missed some 8-hop centres and reached
	// half of the 9-hop ones)
	{
		const EconomyTune& T = w.tune<EconomyTune>();
		EconomyScratch& S = w.scratch<EconomyScratch>();
		const Geo& g = *w.geo;
		const size_t now = w.settlements.size();
		std::vector<std::pair<int, double>> ill;
		for (size_t sid = 0; sid < now; sid++) {
			const Settlement& s = w.settlements[sid];
			if (!s.dead && s.plague > 0 && w.valid(s.cell)) ill.emplace_back(int(sid), s.plague);
		}
		if (!ill.empty()) {
			if (S.plagueMark.size() != size_t(w.n)) S.plagueMark.assign(size_t(w.n), 0);
			for (const auto& src : ill) {
				g.ring(w.settlements[size_t(src.first)].cell, std::max(0, T.plagueHops), S.ring);
				for (const int32_t c : S.ring) S.plagueMark[size_t(c)] = 1;
				for (size_t t = 0; t < w.settlements.size(); t++) {
					const Settlement& o = w.settlements[t];
					if (int(t) == src.first || o.dead || !w.valid(o.cell) || !S.plagueMark[size_t(o.cell)]) continue;
					if (o.plague > 0 || o.immune > 0) continue;
					if (w.rng.chance(T.plagueSpreadP * src.second)) civ::infect(w, int(t), T.plagueSpreadSev * src.second);
				}
				for (const int32_t c : S.ring) S.plagueMark[size_t(c)] = 0;
			}
		}
	}
	expansion(w);
	research(w);
}

} // namespace civ
} // namespace dm
