// DEMIURGE — Sim/Society.cpp : the belief half of dm::society (SPEC-SIM 3.8 society.js, BUILD A9).
// Owner: society engineer (A9, key "society"). The relations half (rel, peek, atWar, declareWar,
// makePeace, onArrive, diplomacy) lives in Diplomacy.cpp.
//
//   newCulture   the culture factory: names (daughter language), traits, colour slot, culture:born
//   witness      how the people see the god's acts: devotion, jaded, love, culture.seen, mercy / wrath,
//                answered prayers, miracle:witnessed
//   regard       population-weighted devotion / love and the ending mood
//   raiseProphet, setEdict, aweIncome
//   yearly       housekeeping -> belief -> Awe -> prayers -> prophets / edicts -> schisms -> the Ark ->
//                culture pop / towns -> society::diplomacy -> chronicle::updateEpithets
//
// Persisted bookkeeping lives in w.vars.num under "society_*" keys (prophet travel, the Wonder of a
// build edict; v3: each culture's memory of the god "society_grace_<id>", "society_wrath_<id>",
// "society_heard_<id>", prayers "society_pray_<id>" / "society_answer_<id>"); the only transient state
// is the brush-event pacing counter (SocietyScratch).
//
// v3 (design review, docs/reviews/claude-design-review.md): the ending mood comes from the people's
// long memory (memory(): grace, wrath, lore, verdict, silence), not only from this year's love;
// nothing a culture witnesses while building its Ark enters that memory; hoarding Awe costs devotion;
// the Awe cap grows by age; an answered prayer teaches (+1 progress-year) and an answered rain or
// bloom prayer blesses the town; a famine's rain prayer is answered by Bloom too; estranged towns
// secede after 80 years before the Iron age.
// Everything iterates by ascending id; randomness from w.rng (traits, prophet term) or string forks
// of it (names), so prose never disturbs the sim stream more than one documented draw.
//
// Verifier changes (2026-11-19, test_society_adv.cpp): a schism re-elects the parent's capital only
// when it was lost (civ::electCapital moves a living capital and restarts its Ark); the largest
// estranged bloc secedes first; a prophet journey tries up to 3 destinations (unreachable ones
// skipped); NaN traits / prophet terms / prayer starts no longer poison devotion or live forever.
#include "Society.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Chronicle.h"
#include "Civ.h"
#include "Names.h"
#include "Powers.h"
#include "Units.h"

namespace dm {

namespace {

// Transient (never saved): pacing of miracle:witnessed for brush kinds.
struct SocietyScratch {
	std::map<std::string, int> brushCalls;
};

const SocietyTune& tuneOf(const World& w) { return w.tune<SocietyTune>(); }

double finiteOr(double x, double fallback) { return isFinite(x) ? x : fallback; }

std::string varKey(const char* what, int id) { return std::string("society_") + what + "_" + std::to_string(id); }

std::optional<double> getVar(const World& w, const std::string& k) {
	const auto it = w.vars.num.find(k);
	if (it == w.vars.num.end()) return std::nullopt;
	return it->second;
}

double varOr(const World& w, const std::string& k, double def) {
	const auto it = w.vars.num.find(k);
	return it != w.vars.num.end() && isFinite(it->second) ? it->second : def;
}

// Is the culture building its Ark (its capital has begun one)?
bool buildingArk(const World& w, int cid) {
	const Culture* c = state::culture(w, cid);
	if (!c) return false;
	const Settlement* s = state::settlement(w, c->capital);
	return s && !s->dead && s->culture == cid && s->ark > 0;
}

bool liveSettlement(const World& w, int sid) {
	const Settlement* s = state::settlement(w, sid);
	return s && !s->dead;
}

bool liveCulture(const World& w, int cid) {
	const Culture* c = state::culture(w, cid);
	return c && !c->dead;
}

int clampAge(int age) { return age < 0 ? 0 : age > Age::SPACE ? int(Age::SPACE) : age; }

// The culture's capital is a live settlement of that culture.
bool hasValidCapital(const World& w, int cid) {
	const Culture* c = state::culture(w, cid);
	if (!c) return false;
	const Settlement* s = state::settlement(w, c->capital);
	return s && !s->dead && s->culture == cid;
}

// A trait in 0..1 (NaN -> the neutral 0.5).
double traitOf(double x) { return isFinite(x) ? sat(x) : 0.5; }

// Is the culture at war with any living culture? (reads relations through the diplomacy half)
bool cultureAtWar(const World& w, int cid) {
	for (const Culture& o : w.cultures) {
		if (o.dead || o.id == cid) continue;
		if (society::atWar(w, cid, o.id)) return true;
	}
	return false;
}

bool inDrought(const World& w, int cell) {
	if (!w.valid(cell) || !w.geo) return false;
	for (const Disaster& d : w.disasters) {
		if (d.type != DisasterType::Drought || !w.valid(d.cell)) continue;
		if (w.geo->angle(cell, d.cell) <= d.ang) return true;
	}
	return false;
}

// Mean scorch over the territory (the centre cell when Civ has not claimed one).
double territoryScorch(const World& w, const Settlement& s) {
	double sum = 0;
	int cnt = 0;
	for (const int32_t c : s.cells) {
		if (!w.valid(c)) continue;
		sum += w.scorch[size_t(c)];
		cnt++;
	}
	if (cnt == 0) return w.valid(s.cell) ? double(w.scorch[size_t(s.cell)]) : 0.0;
	return sum / cnt;
}

// Does the cause of a prayer of kind k hold for s? start = the stricter condition to begin praying;
// otherwise the (looser) condition for the prayer to go on.
bool causeHolds(const World& w, const Settlement& s, PrayerKind k, bool start) {
	const SocietyTune& tn = tuneOf(w);
	switch (k) {
	case PrayerKind::Rain: {
		// (v3.1, design review finding 13) late ages pray for bread only when the hunger lasts
		int need = 1;
		if (start) {
			const Culture* c = state::culture(w, s.culture);
			need = std::max(1, tn.rainFamineAge[size_t(clampAge(c ? c->age : 0))]);
		}
		return s.famine >= need || inDrought(w, s.cell);
	}
	case PrayerKind::Heal: return s.plague > (start ? tn.healPlague : tn.healHold);
	case PrayerKind::Peace: {
		if (!cultureAtWar(w, s.culture)) return false;
		if (!start) return true;
		const Culture* c = state::culture(w, s.culture);
		return c && c->exhaustion > tn.peaceExhaustion;
	}
	case PrayerKind::Bloom: return territoryScorch(w, s) > (start ? tn.bloomScorch : tn.bloomHold);
	}
	return false;
}

void refreshCultures(World& w) {
	std::vector<double> pop(w.cultures.size(), 0.0);
	std::vector<int> towns(w.cultures.size(), 0);
	for (const Settlement& s : w.settlements) {
		if (s.dead || s.culture < 0 || size_t(s.culture) >= w.cultures.size()) continue;
		pop[size_t(s.culture)] += std::max(0.0, finiteOr(s.pop, 0));
		towns[size_t(s.culture)]++;
	}
	for (Culture& c : w.cultures) {
		c.pop = c.dead ? 0.0 : pop[size_t(c.id)];
		c.towns = c.dead ? 0 : towns[size_t(c.id)];
	}
}

void clearProphetVars(World& w, int cid) {
	w.vars.num.erase(varKey("prophetUnit", cid));
	w.vars.num.erase(varKey("prophetTo", cid));
}

// The prophet of culture cid dies (term over, road death, home lost, culture gone): his unit is
// recalled, the edict ends, prophet:died.
void prophetDies(World& w, int cid) {
	Culture* c = state::culture(w, cid);
	if (!c || !c->prophet) return;
	const std::string name = c->prophet->name;
	const std::optional<double> uid = getVar(w, varKey("prophetUnit", cid));
	c->prophet.reset();
	c->edict = Edict::None;
	clearProphetVars(w, cid);
	w.vars.num.erase(varKey("prophetNext", cid));
	if (uid && state::findUnit(w, int(*uid))) units::kill(w, int(*uid), "disbanded");
	w.bus.emit(EvProphetDied{ cid, name });
}

// ---------------------------------------------------------------- yearly parts

void housekeeping(World& w) {
	const SocietyTune& tn = tuneOf(w);
	refreshCultures(w);
	std::vector<uint8_t> hasSettler(w.cultures.size(), 0);
	for (const Unit& u : w.units)
		if (u.type == UnitType::Settler && u.culture >= 0 && size_t(u.culture) < w.cultures.size()) hasSettler[size_t(u.culture)] = 1;
	const int nCult = int(w.cultures.size());
	for (int cid = 0; cid < nCult; cid++) {
		if (w.cultures[size_t(cid)].dead) continue;
		if (w.cultures[size_t(cid)].towns > 0 || hasSettler[size_t(cid)]) continue;
		// no live settlement and no settler of its own: the culture is gone
		prophetDies(w, cid);
		{
			Culture& c = w.cultures[size_t(cid)];
			c.dead = true;
			c.prophet.reset();
			c.edict = Edict::None;
			c.pop = 0;
			c.towns = 0;
		}
		w.bus.emit(EvCultureDied{ cid });
		std::vector<int> foes;
		for (const Culture& o : w.cultures)
			if (o.id != cid && society::atWar(w, cid, o.id)) foes.push_back(o.id);
		const double truce = w.tune<DiplomacyTune>().truceYears;
		for (const int f : foes) society::makePeace(w, cid, f, truce, f);
		std::vector<int> mine;
		for (const Unit& u : w.units)
			if (u.culture == cid) mine.push_back(u.id);
		for (const int uid : mine) units::kill(w, uid, "disbanded");
	}
	// (v3) memories of the god fade with a half-life of memoryHalfLife years
	if (tn.memoryHalfLife > 0) {
		const double keep = std::pow(0.5, 1.0 / tn.memoryHalfLife);
		for (const Culture& c : w.cultures) {
			if (c.dead) continue;
			for (const char* what : { "grace", "wrath", "neglect" }) {
				auto it = w.vars.num.find(varKey(what, c.id));
				if (it != w.vars.num.end()) it->second = isFinite(it->second) ? std::max(0.0, it->second * keep) : 0.0;
			}
		}
	}
	// (v3.1, design review finding 11) the god's recent favour fades fast (the neighbours' envy is short)
	if (tn.favourHalfLife > 0) {
		const double keep = std::pow(0.5, 1.0 / tn.favourHalfLife);
		for (const Culture& c : w.cultures) {
			for (const char* what : { "fgrace", "fwrath" }) {
				auto it = w.vars.num.find(varKey(what, c.id));
				if (it == w.vars.num.end()) continue;
				const double v = isFinite(it->second) ? std::max(0.0, it->second * keep) : 0.0;
				if (c.dead || v < 1e-4) w.vars.num.erase(it);
				else it->second = v;
			}
		}
	}
	// (v3, verifier) a year without a deed of its god, after neglectAfter such years, is remembered as neglect
	if (tn.neglectAfter >= 0) {
		for (const Culture& c : w.cultures) {
			if (c.dead) continue;
			const double heard = varOr(w, varKey("heard", c.id), finiteOr(c.born, 0));
			if (finiteOr(w.year, 0) - heard >= tn.neglectAfter) {
				double& n = w.vars.num[varKey("neglect", c.id)];
				n = (isFinite(n) ? std::max(0.0, n) : 0.0) + 1;
			}
		}
	}
	for (Settlement& s : w.settlements) {
		if (s.dead) {
			s.prayer.reset();           // the dead pray no more (silently)
			continue;
		}
		s.jaded = sat(finiteOr(s.jaded, 0) * (1 - tn.jadedDecay));
		s.love = clamp(finiteOr(s.love, 0) * (1 - tn.loveDrift), -1.0, 1.0);
	}
}

double baselineOf(const World& w, const Settlement& s, const Culture& c) {
	const SocietyTune& tn = tuneOf(w);
	const int age = clampAge(c.age);
	double base = tn.baseline[size_t(age)] * (tn.pietyA + tn.pietyB * traitOf(c.traits.piety));
	if (s.has(Building::SHRINE)) base += tn.shrineBonus;
	if (s.has(Building::TEMPLE)) base += tn.templeBonus;
	if (c.prophet) base += tn.prophetBonus;
	if (s.love < tn.fearLove) base += age < Age::ATOMIC ? tn.fearBonus : -tn.fearBonus;
	return sat(base);
}

void belief(World& w) {
	const SocietyTune& tn = tuneOf(w);
	const bool hoarding = w.god.aweMax > 0 && finiteOr(w.god.awe, 0) >= tn.hoardShare * w.god.aweMax;
	for (Settlement& s : w.settlements) {
		if (s.dead) continue;
		const Culture* c = state::culture(w, s.culture);
		if (!c) continue;
		const double base = baselineOf(w, s, *c);
		const double dev = sat(finiteOr(s.devotion, 0));
		s.devotion = sat(dev + tn.devotionDrift * (base - dev));
		// (v3, design review finding 6) "the god who keeps everything does nothing"
		if (hoarding && s.devotion > tn.hoardFloor) s.devotion = std::max(tn.hoardFloor, sat(s.devotion * (1 - tn.hoardDrift)));
	}
}

void awe(World& w) {
	const SocietyTune& tn = tuneOf(w);
	int shrines = 0, temples = 0, wonders = 0;
	for (const Settlement& s : w.settlements) {
		if (s.dead) continue;
		shrines += s.has(Building::SHRINE) ? 1 : 0;
		temples += s.has(Building::TEMPLE) ? 1 : 0;
		wonders += s.has(Building::WONDER) ? 1 : 0;
	}
	// (v3, design review finding 5) the cap grows with the world's age
	const double ageCap = tn.aweCapAge[size_t(clampAge(w.age))];
	const double cap = ageCap > 0 ? std::min(tn.aweCap, ageCap) : tn.aweCap;
	w.god.aweMax = std::min(cap, tn.aweBase + tn.aweShrine * shrines + tn.aweTemple * temples + tn.aweWonder * wonders);
	const double inc = society::aweIncome(w);
	w.god.awe = clamp(finiteOr(w.god.awe, 0) + inc, 0.0, w.god.aweMax);
}

void prayers(World& w) {
	const SocietyTune& tn = tuneOf(w);
	int active = 0;
	std::vector<uint8_t> endedNow(w.settlements.size(), 0);
	const int nSet = int(w.settlements.size());
	for (int sid = 0; sid < nSet; sid++) {
		Settlement& s = w.settlements[size_t(sid)];
		if (s.dead || !s.prayer) continue;
		const PrayerKind k = s.prayer->kind;
		if (!causeHolds(w, s, k, false)) {
			s.prayer.reset();
			endedNow[size_t(sid)] = 1;
			w.bus.emit(EvPrayer{ sid, k, false });
		} else if (!(w.year - s.prayer->since < tn.prayerYears)) {      // (a non-finite since expires too)
			// unanswered: they feel abandoned
			s.devotion = sat(s.devotion - tn.prayerPenalty);
			s.love = clamp(s.love - tn.prayerPenalty, -1.0, 1.0);
			s.prayer.reset();
			endedNow[size_t(sid)] = 1;
			w.bus.emit(EvPrayer{ sid, k, false });
		} else {
			active++;
		}
	}
	struct Cand { double pop; int id; PrayerKind kind; };
	std::vector<Cand> cands;
	static constexpr PrayerKind kOrder[4] = { PrayerKind::Rain, PrayerKind::Heal, PrayerKind::Peace, PrayerKind::Bloom };
	for (const Settlement& s : w.settlements) {
		if (s.dead || s.prayer || endedNow[size_t(s.id)] || s.devotion < tn.prayDevotion) continue;
		if (!liveCulture(w, s.culture)) continue;
		for (const PrayerKind k : kOrder) {
			if (causeHolds(w, s, k, true)) {
				cands.push_back({ finiteOr(s.pop, 0), s.id, k });
				break;
			}
		}
	}
	std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.pop != b.pop ? a.pop > b.pop : a.id < b.id; });
	for (const Cand& cd : cands) {
		if (active >= tn.maxPrayers) break;
		w.settlements[size_t(cd.id)].prayer = Prayer{ cd.kind, w.year };
		w.vars.num[varKey("pray", w.settlements[size_t(cd.id)].culture)] += 1;   // (v3) every prayer of its history
		active++;
		w.bus.emit(EvPrayer{ cd.id, cd.kind, true });
	}
}

std::string wonderName(World& w, int sid, const Culture& c) {
	static constexpr const char* kKinds[8] = { "Spire", "Great Temple", "Gate", "Colossus", "Library", "Hanging Garden", "Pyramid", "Observatory" };
	Rng r = w.rng.fork("wonder:" + std::to_string(sid));
	const int k = r.pickIndex(8);
	std::string place = names::place(c.lang, r);
	if (place.empty()) place = c.name;
	return std::string("the ") + kKinds[k < 0 ? 0 : k] + " of " + place;
}

void prophets(World& w) {
	const SocietyTune& tn = tuneOf(w);
	const int nCult = int(w.cultures.size());
	for (int cid = 0; cid < nCult; cid++) {
		if (w.cultures[size_t(cid)].dead || !w.cultures[size_t(cid)].prophet) continue;
		// 1. a prophet unit on the road: arrived (Diplomacy moved him) or lost on the way
		bool travelling = false;
		if (const std::optional<double> uid = getVar(w, varKey("prophetUnit", cid))) {
			if (state::findUnit(w, int(*uid))) {
				travelling = true;
			} else {
				const std::optional<double> to = getVar(w, varKey("prophetTo", cid));
				if (to && w.cultures[size_t(cid)].prophet->settlement == int(*to)) {
					clearProphetVars(w, cid);         // arrived
				} else {
					prophetDies(w, cid);              // died on the road
					continue;
				}
			}
		}
		// 2. the term ends, or his home is lost while he is at home
		{
			const Prophet& p = *w.cultures[size_t(cid)].prophet;
			const Settlement* home = state::settlement(w, p.settlement);
			const bool homeLost = !home || home->dead || home->culture != cid;
			if (!(w.year < p.until) || (!travelling && homeLost)) {   // (a non-finite term ends too)
				prophetDies(w, cid);
				continue;
			}
		}
		// 3. about every 10 years he travels to the least devout settlement of his culture
		const std::string nextK = varKey("prophetNext", cid);
		if (!getVar(w, nextK)) w.vars.num[nextK] = w.year + tn.prophetTravel;
		if (!travelling && w.year >= w.vars.num[nextK]) {
			w.vars.num[nextK] = w.year + tn.prophetTravel;
			const int home = w.cultures[size_t(cid)].prophet->settlement;
			// candidates: the least devout first (ties by id); an unreachable one (another island before
			// the culture sails) must not pin him at home forever, so try up to prophetTries of them
			std::vector<std::pair<double, int>> cands;
			for (const Settlement& s : w.settlements) {
				if (s.dead || s.culture != cid || s.id == home || !w.valid(s.cell)) continue;
				cands.emplace_back(sat(finiteOr(s.devotion, 0)), s.id);
			}
			std::sort(cands.begin(), cands.end());
			if (liveSettlement(w, home)) {
				const int age = clampAge(w.cultures[size_t(cid)].age);
				const int tries = std::min(int(cands.size()), std::max(1, tn.prophetTries));
				for (int k = 0; k < tries; k++) {
					const int target = cands[size_t(k)].second;
					units::SpawnOpts so;
					so.type = UnitType::Prophet;
					so.culture = cid;
					so.from = home;
					so.to = target;
					so.dest = w.settlements[size_t(target)].cell;
					so.size = 1;
					so.naval = age >= Age::SAIL ? NavalMode::Open : age >= Age::BRONZE ? NavalMode::Coastal : NavalMode::Land;
					if (const Unit* u = units::spawn(w, so)) {
						w.vars.num[varKey("prophetUnit", cid)] = u->id;
						w.vars.num[varKey("prophetTo", cid)] = target;
						break;
					}
				}
			}
		}
		// 4. the build edict: the capital raises a Wonder 30 years after the edict, once per edict
		const Culture& c = w.cultures[size_t(cid)];
		if (c.edict == Edict::Build && w.year - c.edictSince >= tn.wonderYears) {
			const std::string wk = varKey("wonderEdict", cid);
			const std::optional<double> done = getVar(w, wk);
			const int capId = c.capital;
			if ((!done || *done != c.edictSince) && liveSettlement(w, capId) && w.settlements[size_t(capId)].culture == cid
				&& !w.settlements[size_t(capId)].has(Building::WONDER)) {
				w.vars.num[wk] = c.edictSince;
				society::raiseWonder(w, capId);
			}
		}
	}
}

// Why a settlement is estranged from its culture (or not).
enum class Apart : uint8_t { No, Distance, Love };

Apart estrangement(const World& w, const Settlement& s, const std::vector<double>& meanLove) {
	const SocietyTune& tn = tuneOf(w);
	const Culture* c = state::culture(w, s.culture);
	if (!c || c->dead) return Apart::No;
	const Settlement* cap = state::settlement(w, c->capital);
	if (cap && !cap->dead && cap->id != s.id && cap->culture == s.culture && w.geo && w.valid(s.cell) && w.valid(cap->cell)) {
		const double far = c->age < Age::IRON ? tn.apartAngEarly : tn.apartAng;   // (v3)
		if (w.geo->angle(s.cell, cap->cell) > far) return Apart::Distance;
		const auto& lid = w.cache.landId;
		if (lid.size() == size_t(w.n)) {
			const int a = lid[size_t(s.cell)], b = lid[size_t(cap->cell)];
			if (a >= 0 && b >= 0 && a != b) return Apart::Distance;
		}
	}
	if (std::fabs(s.love - meanLove[size_t(s.culture)]) > tn.apartLove) return Apart::Love;
	return Apart::No;
}

void schisms(World& w) {
	const SocietyTune& tn = tuneOf(w);
	std::vector<double> meanLove(w.cultures.size(), 0.0);
	for (const Culture& c : w.cultures)
		if (!c.dead) meanLove[size_t(c.id)] = society::regard(w, c.id).love;
	std::vector<Apart> why(w.settlements.size(), Apart::No);
	for (Settlement& s : w.settlements) {
		if (s.dead) { s.apart = 0; continue; }
		why[size_t(s.id)] = estrangement(w, s, meanLove);
		s.apart = why[size_t(s.id)] != Apart::No ? s.apart + 1 : 0;
	}
	int living = 0;
	for (const Culture& c : w.cultures) living += c.dead ? 0 : 1;
	const int nCult = int(w.cultures.size());   // cultures born below are not parents this year
	for (int cid = 0; cid < nCult && living < tn.maxCultures; cid++) {
		if (w.cultures[size_t(cid)].dead) continue;
		if (w.year - w.cultures[size_t(cid)].lastSplit < tn.birthEvery) continue;
		std::vector<int> ready;
		// (v3, design review finding 4) a second culture is possible in the Bronze age
		const double need = w.cultures[size_t(cid)].age < Age::IRON ? tn.apartYearsEarly : tn.apartYears;
		for (const Settlement& s : w.settlements)
			if (!s.dead && s.culture == cid && s.apart >= need) ready.push_back(s.id);
		if (ready.size() < 2 || !w.geo) continue;
		// the largest estranged bloc secedes (the seed with the most ready partners within secedeAng;
		// ties: lower id), so a whole far province leaves rather than whichever pair has the lowest id
		int bestSeed = -1;
		size_t bestN = 1;
		for (const int seed : ready) {
			const int seedCell = w.settlements[size_t(seed)].cell;
			size_t cnt = 1;
			for (const int o : ready)
				if (o != seed && w.geo->angle(seedCell, w.settlements[size_t(o)].cell) <= tn.secedeAng) cnt++;
			if (cnt > bestN) { bestN = cnt; bestSeed = seed; }
		}
		if (bestSeed >= 0) {
			const int seed = bestSeed;
			std::vector<int> group{ seed };
			const int seedCell = w.settlements[size_t(seed)].cell;
			for (const int o : ready)
				if (o != seed && w.geo->angle(seedCell, w.settlements[size_t(o)].cell) <= tn.secedeAng) group.push_back(o);
			const BirthReason reason = why[size_t(seed)] == Apart::Distance ? BirthReason::Distance : BirthReason::Schism;
			const Culture* nc = society::newCulture(w, { cid, seedCell, reason });
			if (!nc) continue;
			const int ncid = nc->id;
			for (const int sid : group) {
				civ::setCulture(w, sid, ncid);
				w.settlements[size_t(sid)].apart = 0;
			}
			// Capitals are sticky (Civ): civ::electCapital MOVES a living capital to the most populous
			// settlement and restarts its launch site and Ark, so only call it where the capital is
			// missing or no longer of that culture (setCulture normally re-elects already). The daughter
			// takes her most populous member (none of them can hold an Ark yet).
			civ::electCapital(w, ncid);
			if (!hasValidCapital(w, cid)) civ::electCapital(w, cid);
			living++;                             // one birth per parent per 150 years (newCulture sets lastSplit)
		}
	}
}

void ark(World& w) {
	const SocietyTune& tn = tuneOf(w);
	if (w.vars.ark) return;
	const int nCult = int(w.cultures.size());
	for (int cid = 0; cid < nCult; cid++) {
		const Culture& c = w.cultures[size_t(cid)];
		if (c.dead || c.age < Age::SPACE) continue;
		const int sid = c.capital;
		if (!liveSettlement(w, sid)) continue;
		Settlement& s = w.settlements[size_t(sid)];
		if (s.culture != cid || !s.has(Building::LAUNCH)) continue;
		if (!(s.ark > 0)) {
			s.ark = 0;
			w.bus.emit(EvArkStarted{ cid, sid });
		}
		const double rate = (1.0 / tn.arkYears) * clamp(finiteOr(c.pop, 0) / tn.arkPopRef, tn.arkMin, tn.arkMax);
		Settlement& s2 = w.settlements[size_t(sid)];
		s2.ark = std::min(1.0, s2.ark + rate);
		if (s2.ark >= 1 && !w.vars.ark) {
			w.vars.ark = ArkRecord{ cid, sid, w.year };
			w.bus.emit(EvArkLaunched{ cid, sid });
			return;
		}
	}
}

} // namespace

namespace society {

// ---------------------------------------------------------------- newCulture

Culture* newCulture(World& w, const NewCultureOpts& opts) {
	const SocietyTune& tn = tuneOf(w);
	if (int(w.cultures.size()) >= tn.maxCultureIds) return nullptr;
	int parent = opts.parent;
	if (parent >= 0 && !state::culture(w, parent)) parent = -1;   // a dangling parent is no parent
	const int cell = w.valid(opts.cell) ? opts.cell : -1;
	// read the parent before the vector may reallocate
	std::string parentLang;
	Traits pt;
	int pAge = 0;
	double pScience = 0;
	NumBag pSeen;
	if (parent >= 0) {
		const Culture& p = w.cultures[size_t(parent)];
		parentLang = p.lang;
		pt = p.traits;
		pAge = clampAge(p.age);
		pScience = finiteOr(p.science, 0);
		pSeen = p.seen;
	}
	const int id = int(w.cultures.size());
	Rng nr = w.rng.fork("culture:" + std::to_string(id));
	std::string lang = names::language(nr, parentLang);
	names::CultureNames cn = names::culture(lang, nr);
	std::string godName = names::god(lang, nr);
	if (cn.name.empty()) cn.name = "Folk";
	if (cn.adj.empty()) cn.adj = cn.name;
	if (cn.plural.empty()) cn.plural = "the " + cn.name;
	// traits from the sim stream: roots triangular around 0.5, daughters drift from the parent
	Traits tr;
	double* slots[4] = { &tr.piety, &tr.aggression, &tr.curiosity, &tr.seafaring };
	const double* from[4] = { &pt.piety, &pt.aggression, &pt.curiosity, &pt.seafaring };
	const double drift = opts.reason == BirthReason::Schism ? tn.schismDrift : tn.traitDrift;
	for (int k = 0; k < 4; k++) {
		if (parent >= 0) {
			*slots[k] = clamp(traitOf(*from[k]) + drift * w.rng.gauss(), 0.05, 0.95);
		} else {
			*slots[k] = clamp(0.5 + tn.traitSpread * (w.rng.f() + w.rng.f() - 1), 0.05, 0.95);
		}
	}
	Culture& c = state::newCulture(w, parent);
	c.name = cn.name;
	c.adj = cn.adj;
	c.plural = cn.plural;
	c.lang = lang;
	c.godName = godName;
	c.traits = tr;
	if (parent >= 0) {
		// a daughter keeps her mother's knowledge and some of her memories of the god
		c.age = pAge;
		c.science = pScience;
		for (const auto& kv : pSeen)
			if (kv.second > 0) c.seen[kv.first] = kv.second * tn.daughterSeen;
	}
	if (opts.reason == BirthReason::Awaken && tn.awakenSeen > 0) c.seen["awaken"] += tn.awakenSeen;
	if (parent >= 0) w.cultures[size_t(parent)].lastSplit = w.year;
	// (v3, design review finding 1) a daughter inherits her mother's memory of the god; a new people
	// has just heard its god wake it
	for (const char* what : { "grace", "wrath", "neglect", "pray", "answer" }) {
		const double v = parent >= 0 ? varOr(w, varKey(what, parent), 0) : 0;
		if (v > 0) w.vars.num[varKey(what, id)] = v;
	}
	w.vars.num[varKey("heard", id)] = parent >= 0 ? varOr(w, varKey("heard", parent), w.year) : w.year;
	w.dirty.color = true;
	w.bus.emit(EvCultureBorn{ id, parent, cell, opts.reason });
	return &w.cultures[size_t(id)];
}

// ---------------------------------------------------------------- yearly

void yearly(World& w) {
	housekeeping(w);
	belief(w);
	awe(w);
	prayers(w);
	prophets(w);
	schisms(w);
	ark(w);
	refreshCultures(w);
	society::diplomacy(w);
	chronicle::updateEpithets(w);
}

// ---------------------------------------------------------------- witness

int witness(World& w, int cell, double ang, const WitnessOpts& opts) {
	const SocietyTune& tn = tuneOf(w);
	if (!w.valid(cell) || !w.geo || !isFinite(ang) || ang < 0) return 0;
	const double faith = clamp(finiteOr(opts.faith, 0), 0.0, 1.0);
	const double good = clamp(finiteOr(opts.good, 0), -1.0, 1.0);
	const double goodHarmed = clamp(finiteOr(opts.goodHarmed, 0), -1.0, 1.0);
	const double reach = std::max(ang, 0.5 * w.geo->step);
	// the act's own sign (mercy / wrath, the event): the stronger of the two goods when it hurt someone
	double actGood = good;
	if (!opts.harmed.empty() && std::fabs(goodHarmed) > std::fabs(good)) actGood = goodHarmed;

	// Brush powers: faith arrives already x elapsed seconds, but the love rule has no time factor;
	// scale it by the same elapsed seconds (faith / def.faith, at most 1) so love per SECOND of
	// brushing follows the formula instead of love per call (4 calls / s would saturate it in ~2 s).
	const powers::PowerDef* def = powers::byId(opts.kind);
	double loveScale = 1;
	if (def && def->brush && def->faith > 0) loveScale = clamp(faith / def->faith, 0.0, 1.0);

	const std::vector<int> ids = civ::near(w, cell, reach);
	std::vector<int> noticed;
	int count = 0;
	// (v3) what each culture will remember of this act: grace / wrath impulses, and that it was heard
	struct MemAdd { int culture; double grace, wrath; };
	std::vector<MemAdd> mem;
	auto memOf = [&](int cid) -> MemAdd& {
		for (MemAdd& m : mem) if (m.culture == cid) return m;
		mem.push_back({ cid, 0, 0 });
		return mem.back();
	};
	for (const int sid : ids) {
		if (!liveSettlement(w, sid)) continue;
		Settlement& s = w.settlements[size_t(sid)];
		if (!w.valid(s.cell)) continue;
		const double d = w.geo->angle(cell, s.cell);
		const double f = 1 - smooth(d / reach);
		if (!(f > 0)) continue;
		const bool harmed = std::find(opts.harmed.begin(), opts.harmed.end(), sid) != opts.harmed.end();
		double g = harmed ? goodHarmed : good;
		double fs = faith;
		bool answered = false;
		// (v3, design review finding 9) a famine's rain prayer is a prayer for bread: Bloom answers it too
		const bool bread = s.prayer && s.prayer->kind == PrayerKind::Rain && s.famine >= 1 && opts.kind == "bloom";
		if (s.prayer && !opts.kind.empty() && (opts.kind == toStr(s.prayer->kind) || bread)) {
			answered = true;
			fs = faith * tn.prayerFaith;
			g = 1;
		} else if (!harmed && g <= 0) {
			fs = faith * tn.harmlessFaith;
		}
		const double dev0 = sat(finiteOr(s.devotion, 0));
		const double jad0 = sat(finiteOr(s.jaded, 0));
		const double love0 = clamp(finiteOr(s.love, 0), -1.0, 1.0);
		const double gain = fs * f * (1 - jad0) * (1 - dev0);
		s.devotion = sat(dev0 + gain);
		s.jaded = std::min(1.0, jad0 + tn.witnessJaded * fs * f);
		s.love = clamp(love0 + tn.witnessLove * g * f * loveScale * (1 - std::fabs(love0)), -1.0, 1.0);
		const double seenAdd = fs * f;
		if (seenAdd > 0 && !opts.kind.empty()) {
			if (Culture* cu = state::culture(w, s.culture)) cu->seen[opts.kind] += seenAdd;
		}
		const bool gained = gain > 0 || s.love != love0 || seenAdd > 0 || answered;
		if (!opts.natural && s.culture >= 0) {
			const Culture* cu = state::culture(w, s.culture);
			const double cpop = cu ? finiteOr(cu->pop, 0) : 0;
			const double sp = std::max(0.0, finiteOr(s.pop, 0));
			const double share = cpop > sp && cpop > 0 ? sp / cpop : 1.0;
			const double impulse = std::fabs(g) * f * loveScale * share;
			MemAdd& m = memOf(s.culture);
			if (g > 0) m.grace += impulse;
			else if (g < 0) m.wrath += impulse;
		}
		if (answered) {
			const PrayerKind k = s.prayer->kind;
			const int cid = s.culture;
			s.prayer.reset();
			// (v3, design review findings 3, 9) the priests learn from an answered prayer; an answered
			// rain or bloom prayer blesses the town (Economy: growth and K x blessedK while blessed)
			if (k == PrayerKind::Rain || k == PrayerKind::Bloom) s.blessed = std::max(finiteOr(s.blessed, 0), tn.prayerBlessed);
			w.vars.num[varKey("answer", cid)] += 1;
			w.bus.emit(EvPrayerAnswered{ sid, k });
			civ::addScience(w, cid, tn.prayerScience);
		}
		if (gained) {
			count++;
			if (int(noticed.size()) < tn.maxWitnessIds) noticed.push_back(sid);
		}
	}
	// (v3) the long memory: every culture that saw it heard its god; while it builds its Ark the deed
	// moves love but is not written into the memory (nothing is bought in the last years)
	for (const MemAdd& m : mem) {
		if (!liveCulture(w, m.culture)) continue;
		w.vars.num[varKey("heard", m.culture)] = w.year;
		// (v3.1, design review finding 11) the god's recent favour (Diplomacy: envy, blame, thanks), Ark or not
		if (tn.favourHalfLife > 0) {
			if (m.grace > 0) w.vars.num[varKey("fgrace", m.culture)] += m.grace;
			if (m.wrath > 0) w.vars.num[varKey("fwrath", m.culture)] += m.wrath;
		}
		if (tn.arkNoMemory && buildingArk(w, m.culture)) continue;
		if (m.grace > 0) w.vars.num[varKey("grace", m.culture)] += m.grace;
		if (m.wrath > 0) w.vars.num[varKey("wrath", m.culture)] += m.wrath;
	}
	w.god.mercy = finiteOr(w.god.mercy, 0) + std::max(0.0, actGood) * faith;
	w.god.wrath = finiteOr(w.god.wrath, 0) + std::max(0.0, -actGood) * faith;
	if (count > 0) {
		bool emitNow = true;
		if (def && def->brush && tn.brushEventEvery > 1) {
			int& calls = w.scratch<SocietyScratch>().brushCalls[opts.kind];
			emitNow = calls % tn.brushEventEvery == 0;
			calls = (calls + 1) % tn.brushEventEvery;
		}
		if (emitNow) w.bus.emit(EvMiracleWitnessed{ cell, opts.kind, actGood, noticed });
	}
	return count;
}

// ---------------------------------------------------------------- regard

Regard regard(const World& w, int cultureId) {
	const SocietyTune& tn = tuneOf(w);
	Regard r;
	double wsum = 0, dsum = 0, lsum = 0, dplain = 0, lplain = 0;
	int cnt = 0;
	for (const Settlement& s : w.settlements) {
		if (s.dead) continue;
		if (cultureId >= 0 && s.culture != cultureId) continue;
		const double p = std::max(0.0, finiteOr(s.pop, 0));
		const double dv = sat(finiteOr(s.devotion, 0));
		const double lv = clamp(finiteOr(s.love, 0), -1.0, 1.0);
		wsum += p;
		dsum += p * dv;
		lsum += p * lv;
		dplain += dv;
		lplain += lv;
		cnt++;
	}
	if (cnt == 0) return r;                        // nobody: zeros, Forgotten
	if (wsum > 0) {
		r.devotion = dsum / wsum;
		r.love = lsum / wsum;
	} else {
		r.devotion = dplain / cnt;
		r.love = lplain / cnt;
	}
	(void)tn;
	r.mood = memory(w, cultureId).mood;
	return r;
}

// ---------------------------------------------------------------- memory (v3, design review)

Memory memory(const World& w, int cultureId) {
	const SocietyTune& tn = tuneOf(w);
	Memory m;
	// today's feelings: population-weighted means (as regard)
	double wsum = 0, dsum = 0, lsum = 0, dplain = 0, lplain = 0;
	int cnt = 0;
	for (const Settlement& s : w.settlements) {
		if (s.dead) continue;
		if (cultureId >= 0 && s.culture != cultureId) continue;
		const double p = std::max(0.0, finiteOr(s.pop, 0));
		const double dv = sat(finiteOr(s.devotion, 0));
		const double lv = clamp(finiteOr(s.love, 0), -1.0, 1.0);
		wsum += p; dsum += p * dv; lsum += p * lv; dplain += dv; lplain += lv;
		cnt++;
	}
	if (cnt == 0) return m;                        // nobody: Forgotten
	m.devotion = wsum > 0 ? dsum / wsum : dplain / cnt;
	m.love = wsum > 0 ? lsum / wsum : lplain / cnt;
	// the memory: one culture, or everyone (weighted by people; the latest deed of any)
	double heard = -1e18;
	if (cultureId >= 0) {
		m.grace = varOr(w, varKey("grace", cultureId), 0);
		m.wrath = varOr(w, varKey("wrath", cultureId), 0);
		m.neglect = varOr(w, varKey("neglect", cultureId), 0);
		const Culture* c = state::culture(w, cultureId);
		heard = varOr(w, varKey("heard", cultureId), c ? finiteOr(c->born, 0) : 0);
		m.prayers = int(varOr(w, varKey("pray", cultureId), 0));
		m.answered = int(varOr(w, varKey("answer", cultureId), 0));
	} else {
		double pw = 0, g = 0, wr = 0, ng = 0;
		for (const Culture& c : w.cultures) {
			if (c.dead) continue;
			const double p = std::max(0.0, finiteOr(c.pop, 0)) + 1e-9;
			pw += p;
			g += p * varOr(w, varKey("grace", c.id), 0);
			wr += p * varOr(w, varKey("wrath", c.id), 0);
			ng += p * varOr(w, varKey("neglect", c.id), 0);
			heard = std::max(heard, varOr(w, varKey("heard", c.id), finiteOr(c.born, 0)));
			m.prayers += int(varOr(w, varKey("pray", c.id), 0));
			m.answered += int(varOr(w, varKey("answer", c.id), 0));
		}
		if (pw > 0) { m.grace = g / pw; m.wrath = wr / pw; m.neglect = ng / pw; }
		if (heard < -1e17) heard = 0;
	}
	m.grace = std::max(0.0, m.grace);
	m.wrath = std::max(0.0, m.wrath);
	m.neglect = std::max(0.0, finiteOr(m.neglect, 0));
	m.silent = std::max(0.0, finiteOr(w.year, 0) - heard);
	const double total = m.grace + m.wrath;
	// (v3, verifier) kindness from a god who was long absent is held lightly: grace counts x presence,
	// presence = 1 / (1 + neglectK x neglect) (wrath is remembered whole: terror needs no habit)
	m.presence = 1 / (1 + std::max(0.0, finiteOr(tn.neglectK, 0)) * m.neglect);
	const double held = m.grace * m.presence;
	m.lore = (held - m.wrath) / (held + m.wrath + std::max(1e-9, tn.loreFloor));
	m.verdict = tn.verdictLove * m.love + (1 - tn.verdictLove) * m.lore;
	m.remembered = total >= tn.rememberMin;
	m.mixed = m.remembered && std::min(m.grace, m.wrath) >= tn.mixedRatio * std::max(m.grace, m.wrath);
	const bool byPrayers = m.prayers >= tn.answeredMin && m.answered >= tn.answeredShare * m.prayers && m.verdict > 0;
	if (m.silent >= tn.silentYears) m.mood = m.remembered ? Mood::Parting : Mood::Forgotten;
	else if (m.mixed) m.mood = Mood::Parting;
	else if (m.verdict <= tn.fearedLove) m.mood = Mood::Feared;
	else if ((m.verdict >= tn.belovedLove && m.devotion >= tn.belovedDevotion) || byPrayers) m.mood = Mood::Beloved;
	else if (m.devotion < tn.forgottenDevotion) m.mood = Mood::Forgotten;
	else m.mood = Mood::Parting;
	return m;
}

bool raiseWonder(World& w, int sid) {
	if (!liveSettlement(w, sid)) return false;
	Settlement& s = w.settlements[size_t(sid)];
	if (s.has(Building::WONDER)) return false;
	const Culture* c = state::culture(w, s.culture);
	if (!c) return false;
	const std::string nm = wonderName(w, sid, *c);
	w.settlements[size_t(sid)].b[Building::WONDER] = 1;
	w.dirty.objects = true;
	w.bus.emit(EvWonderBuilt{ sid, nm });
	w.bus.emit(EvSettlementBuilding{ sid, Building::WONDER });
	return true;
}

// ---------------------------------------------------------------- prophets and edicts

Prophet* raiseProphet(World& w, int sid) {
	const SocietyTune& tn = tuneOf(w);
	if (!liveSettlement(w, sid)) return nullptr;
	const int cid = w.settlements[size_t(sid)].culture;
	if (!liveCulture(w, cid) || w.cultures[size_t(cid)].prophet) return nullptr;
	Rng nr = w.rng.fork("prophet:" + std::to_string(cid) + ":" + std::to_string(int64_t(std::floor(w.year))));
	std::string name = names::person(w.cultures[size_t(cid)].lang, nr);
	if (name.empty()) name = "the Nameless";
	const double term = tn.prophetYears * (1 + tn.prophetJitter * (2 * w.rng.f() - 1));
	Culture& c = w.cultures[size_t(cid)];
	c.prophet = Prophet{ name, sid, w.year + term };
	clearProphetVars(w, cid);
	w.vars.num[varKey("prophetNext", cid)] = w.year + tn.prophetTravel;
	w.bus.emit(EvProphetRisen{ cid, sid, name });
	Culture& c2 = w.cultures[size_t(cid)];
	return c2.prophet ? &*c2.prophet : nullptr;
}

bool setEdict(World& w, int cultureId, Edict edict) {
	const SocietyTune& tn = tuneOf(w);
	if (!liveCulture(w, cultureId) || edict == Edict::None) return false;
	Culture& c = w.cultures[size_t(cultureId)];
	if (!c.prophet) return false;
	if (c.edict == edict) return false;
	if (c.edict != Edict::None && w.year - c.edictSince < tn.edictChange) return false;
	c.edict = edict;
	c.edictSince = w.year;
	w.bus.emit(EvEdict{ cultureId, edict });
	return true;
}

// ---------------------------------------------------------------- Awe

double aweIncome(const World& w) {
	const SocietyTune& tn = tuneOf(w);
	double S = 0;
	int live = 0;
	for (const Settlement& s : w.settlements) {
		if (s.dead) continue;
		live++;
		const double weight = tn.sCamp + (s.has(Building::SHRINE) ? tn.sShrine : 0) + (s.has(Building::TEMPLE) ? tn.sTemple : 0);
		S += sat(finiteOr(s.devotion, 0)) * weight * (1 + std::clamp(s.tier, 0, 4) / 2.0);
	}
	if (live == 0) return 0;
	return tn.incomeBase + tn.incomeK * std::log(1 + S);
}

} // namespace society
} // namespace dm
