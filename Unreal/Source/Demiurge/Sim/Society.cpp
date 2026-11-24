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
//
// v4 mythos (track M5 "society-v4", SPEC-MYTHOS 3.2, 3.3, 3.5, 3.8, 3.10, 1.4). Society READS mythos and never
// writes MythosState:
//   - Factors (mythos::factors, neutral = Factors{}): devotionBase (belief baseline), loveTarget (love drift target),
//     prayerChance x expect[kind] (prayers: below 1 a forked roll, above 1 a lower devotion gate),
//     unansweredPenalty, answeredGain (an answered prayer's faith and lesson), aweS (the culture's weight in S),
//     schism (estrangement years needed / schism); the archetype's aweIncome multiplies the income (3.1).
//   - mythos::witnessScale (presence 3.2) in witness: faith, jaded, long memory and favour sums.
//   - mythos::aspect (3.3): the v3 fear rule applies to a Threat people whatever their love, and a people's courage
//     softens fear's devotion bonus (x (1 - 0.5 courage), 3.5): both only where mythos acts on the culture
//     (phase >= 1 and emergent, or its Factors are not neutral), so a passive world stays v3.1.
//   - characters::credibility / figure (3.8): a messenger's visits (Diplomacy.cpp), a Shepherd travels every
//     shepherdTravel years, a Child lives childYears.
//   - New traits (3.5): compassion, ambition, courage are rolled at phase >= 1 from the fork
//     "myth:society:traits:<id>" (never w.rng; 0.5 at phase 0), daughters drift; shapeTraits records the god's
//     shaping ("society_shape_<trait>") and later root peoples inherit it (+- traitDrift).
//   - The Victory and Sun prayers (3.10) start only at phase >= 1 with MythosTune::emergent on (they would move a
//     passive world: SPEC-MYTHOS 5.1 rule 2); sunYears <= 0 or victoryExhaustion <= 0 switch either off.
//     Victory: a capital at war, exhaustion < victoryExhaustion; answered when the god harms a people it is at war
//     with (witness). Sun: territory wet > sunWet for sunYears years (society_wet_<sid>), a storm over it, or a
//     flooded territory (mean wet >= kFloodWet); answered by Raise Land on it, or through answerPrayer (Cards).
//   - nudge, remember, answerPrayer are real (secede stays neutral until M5b).
// With every read neutral the results are bit-identical (multiplications by 1, offsets skipped when 0, no new
// draw on w.rng): test_society_v4_trace.cpp checks a recorded 300-year trace.
#include "Society.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Characters.h"
#include "Chronicle.h"
#include "Civ.h"
#include "Mythos.h"
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

// ---------------------------------------------------------------- v4 mythos reads (M5)

// SPEC-MYTHOS 3.5: "fear's devotion bonus x (1 - 0.5 k)" (courage); no TUNE field exists for the 0.5 (headers frozen).
constexpr double kCourageFear = 0.5;
// Sun prayers (3.10): a territory whose mean wetness is at least this is flooded.
constexpr double kFloodWet = 0.95;
// answerPrayer (no act of the god at a cell): the faith of a typical click power (Heal 0.12, Inspire 0.08).
constexpr double kAnswerFaith = 0.1;

bool mythOn(const World& w) { return w.tune<MythosTune>().phase >= 1; }
// Victory and Sun start only where they cannot move a passive world (5.1 rule 2).
bool newPrayersOn(const World& w) { return mythOn(w) && w.tune<MythosTune>().emergentOn(MythosTune::FamPrayers); }
// Mythos is read only once it is on: at phase 0 its reads are neutral by contract, so Society does not even call
// them (the v3.1 call graph and cost stay exactly as they were).
Factors mythFactors(const World& w, int cid) { return mythOn(w) ? mythos::factors(w, cid) : Factors{}; }

// A finite multiplier (non-finite -> 1: a bad record never poisons the sim).
double mulOf(double x) { return isFinite(x) ? x : 1.0; }
double addOf(double x) { return isFinite(x) ? x : 0.0; }

// Factors per culture id (Factors{} for cultures without a record).
std::vector<Factors> cultureFactors(const World& w) {
	std::vector<Factors> f(w.cultures.size());
	for (size_t k = 0; k < f.size(); k++) f[k] = mythFactors(w, int(k));
	return f;
}
const Factors& factorOf(const std::vector<Factors>& f, int cid) {
	static const Factors kNeutral{};
	return cid >= 0 && size_t(cid) < f.size() ? f[size_t(cid)] : kNeutral;
}
// Does mythos act on this culture (the gate of the aspect and trait effects Society reads itself)?
bool mythActive(const World& w, const Factors& f) {
	if (!mythOn(w)) return false;
	return w.tune<MythosTune>().emergentOn(MythosTune::FamAspect | MythosTune::FamTraits) || !(f == Factors{});
}

// The territory's mean wetness (the centre cell when Civ has not claimed one).
double territoryWet(const World& w, const Settlement& s) {
	if (w.wet.size() != size_t(w.n)) return 0;
	double sum = 0;
	int cnt = 0;
	for (const int32_t c : s.cells) {
		if (!w.valid(c)) continue;
		sum += w.wet[size_t(c)];
		cnt++;
	}
	if (cnt == 0) return w.valid(s.cell) ? double(w.wet[size_t(s.cell)]) : 0.0;
	return sum / cnt;
}

bool stormOver(const World& w, int cell) {
	if (!w.valid(cell) || !w.geo) return false;
	for (const Disaster& d : w.disasters) {
		if (d.type != DisasterType::Storm || !w.valid(d.cell)) continue;
		if (w.geo->angle(cell, d.cell) <= std::max(d.ang, 0.5 * w.geo->step)) return true;
	}
	return false;
}

bool flooded(const World& w, const Settlement& s) { return territoryWet(w, s) >= kFloodWet; }

// The messenger figure of a culture's prophet, or nullptr (an unnamed prophet).
const Figure* prophetFigure(const World& w, const Culture& c) {
	if (!c.prophet || c.prophet->figure < 0) return nullptr;
	return characters::figure(w, c.prophet->figure);
}
// Years between a prophet's journeys (a Shepherd travels more often, 3.8).
double travelYears(const World& w, const Culture& c) {
	const double base = w.tune<SocietyTune>().prophetTravel;
	const Figure* f = prophetFigure(w, c);
	if (f && f->archetype == uint8_t(MessengerArchetype::Shepherd)) {
		const double t = w.tune<CharactersTune>().shepherdTravel;
		if (isFinite(t) && t > 0) return t;
	}
	return base;
}

const char* const kShapeKeys[5] = { "society_shape_curiosity", "society_shape_compassion", "society_shape_ambition",
	"society_shape_courage", "society_shape_aggression" };

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
	case PrayerKind::Victory: {
		// (v4, 3.10) a capital at war, not yet weary, prays to win; the prayer lasts while the war does
		if (!cultureAtWar(w, s.culture)) return false;
		if (!start) return true;
		const Culture* c = state::culture(w, s.culture);
		return c && c->capital == s.id && finiteOr(c->exhaustion, 0) < tn.victoryExhaustion;
	}
	case PrayerKind::Sun: {
		// (v4, 3.10) too much rain: a wet territory for sunYears years, a storm over it, or a flood
		if (tn.sunYears <= 0) return false;
		if (stormOver(w, s.cell) || flooded(w, s)) return true;
		if (!start) return territoryWet(w, s) > tn.sunWet;
		return varOr(w, varKey("wet", s.id), 0) >= tn.sunYears;
	}
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
	const std::vector<Factors> F = cultureFactors(w);
	for (Settlement& s : w.settlements) {
		if (s.dead) {
			s.prayer.reset();           // the dead pray no more (silently)
			continue;
		}
		s.jaded = sat(finiteOr(s.jaded, 0) * (1 - tn.jadedDecay));
		// (v4, 1.4) the love drift target: 0 as in v3.1 unless mythos moves it (Parent, Threat, survivors ...)
		const double target = addOf(factorOf(F, s.culture).loveTarget);
		if (target == 0) s.love = clamp(finiteOr(s.love, 0) * (1 - tn.loveDrift), -1.0, 1.0);
		else s.love = clamp(finiteOr(s.love, 0) + tn.loveDrift * (clamp(target, -1.0, 1.0) - finiteOr(s.love, 0)), -1.0, 1.0);
	}
}

// What belief() needs of a culture's mythos once a year.
struct BeliefMyth {
	double devotionBase = 0;
	bool threat = false;                // the Threat aspect's fear rule applies (3.3)
	double fearMul = 1;                 // courage softens fear's devotion bonus (3.5)
};

double baselineOf(const World& w, const Settlement& s, const Culture& c, const BeliefMyth& m) {
	const SocietyTune& tn = tuneOf(w);
	const int age = clampAge(c.age);
	double base = tn.baseline[size_t(age)] * (tn.pietyA + tn.pietyB * traitOf(c.traits.piety));
	if (s.has(Building::SHRINE)) base += tn.shrineBonus;
	if (s.has(Building::TEMPLE)) base += tn.templeBonus;
	if (c.prophet) base += tn.prophetBonus;
	if (s.love < tn.fearLove || m.threat) {
		const double bonus = m.fearMul == 1 ? tn.fearBonus : tn.fearBonus * m.fearMul;
		base += age < Age::ATOMIC ? bonus : -bonus;
	}
	if (m.devotionBase != 0) base += m.devotionBase;
	return sat(base);
}

void belief(World& w) {
	const SocietyTune& tn = tuneOf(w);
	const bool hoarding = w.god.aweMax > 0 && finiteOr(w.god.awe, 0) >= tn.hoardShare * w.god.aweMax;
	std::vector<BeliefMyth> myth(w.cultures.size());
	for (const Culture& c : w.cultures) {
		if (c.dead) continue;
		const Factors f = mythFactors(w, c.id);
		BeliefMyth& m = myth[size_t(c.id)];
		m.devotionBase = addOf(f.devotionBase);
		if (mythActive(w, f)) {
			m.threat = mythos::aspect(w, c.id) == Aspect::Threat;
			m.fearMul = clamp(1 - kCourageFear * traitOf(c.traits.courage), 0.0, 1.0);
		}
	}
	for (Settlement& s : w.settlements) {
		if (s.dead) continue;
		const Culture* c = state::culture(w, s.culture);
		if (!c) continue;
		const double base = baselineOf(w, s, *c, myth[size_t(c->id)]);
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
	const std::vector<Factors> F = cultureFactors(w);
	const bool v4 = newPrayersOn(w);
	// (v4, 3.10) the Sun prayer's wet years in a row, per settlement (kept only while the new prayers are on)
	if (v4) {
		for (const Settlement& s : w.settlements) {
			const std::string k = varKey("wet", s.id);
			if (!s.dead && territoryWet(w, s) > tn.sunWet) w.vars.num[k] = varOr(w, k, 0) + 1;
			else w.vars.num.erase(k);
		}
	}
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
			// unanswered: they feel abandoned (v4: x unansweredPenalty, silence hurts a dependent people more)
			const double pen = tn.prayerPenalty * mulOf(factorOf(F, s.culture).unansweredPenalty);
			s.devotion = sat(s.devotion - pen);
			s.love = clamp(s.love - pen, -1.0, 1.0);
			s.prayer.reset();
			endedNow[size_t(sid)] = 1;
			w.bus.emit(EvPrayer{ sid, k, false });
		} else {
			active++;
		}
	}
	struct Cand { double pop; int id; PrayerKind kind; };
	std::vector<Cand> cands;
	static constexpr PrayerKind kOrder[6] = { PrayerKind::Rain, PrayerKind::Heal, PrayerKind::Peace, PrayerKind::Bloom,
		PrayerKind::Victory, PrayerKind::Sun };
	const int kinds = v4 ? 6 : 4;
	for (const Settlement& s : w.settlements) {
		if (s.dead || s.prayer || endedNow[size_t(s.id)]) continue;
		if (!liveCulture(w, s.culture)) continue;
		for (int ki = 0; ki < kinds; ki++) {
			const PrayerKind k = kOrder[ki];
			if (!causeHolds(w, s, k, true)) continue;
			// (v4, 1.4) the prayer chance: x prayerChance (aspect, rite, dependence) x expect (living legends).
			// 1 = v3.1 (devotion >= prayDevotion, no roll); above 1 the devotion gate falls; below 1 a forked roll.
			const Factors& f = factorOf(F, s.culture);
			const double p = mulOf(f.prayerChance) * mulOf(f.expect[size_t(k)]);
			const double gate = p > 1 ? tn.prayDevotion / p : tn.prayDevotion;
			if (s.devotion < gate) break;
			if (p < 1) {
				Rng r = w.rng.fork("myth:society:pray:" + std::to_string(s.id) + ":" + std::to_string(int64_t(std::floor(w.year))));
				if (!r.chance(p)) break;
			}
			cands.push_back({ finiteOr(s.pop, 0), s.id, k });
			break;
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
		// (v4, 3.8) a Shepherd messenger travels every shepherdTravel years instead of prophetTravel
		const double travel = travelYears(w, w.cultures[size_t(cid)]);
		const std::string nextK = varKey("prophetNext", cid);
		if (!getVar(w, nextK)) w.vars.num[nextK] = w.year + travel;
		if (!travelling && w.year >= w.vars.num[nextK]) {
			w.vars.num[nextK] = w.year + travel;
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
		double need = w.cultures[size_t(cid)].age < Age::IRON ? tn.apartYearsEarly : tn.apartYears;
		// (v4, 1.4) the schism pace (agency, 3.4): x schism faster; 1 = v3.1, 0 = no secession
		const double pace = mulOf(mythFactors(w, cid).schism);
		if (!(pace > 0)) continue;
		if (pace != 1) need /= pace;
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

// ---------------------------------------------------------------- witnessed deeds (shared by witness, answerPrayer, remember)

// What one culture will remember of an act: grace / wrath impulses (x the witness's share of its culture).
struct MemAdd { int culture; double grace, wrath; };

MemAdd& memOf(std::vector<MemAdd>& mem, int cid) {
	for (MemAdd& m : mem) if (m.culture == cid) return m;
	mem.push_back({ cid, 0, 0 });
	return mem.back();
}

// A witness s with good g at falloff f (x loveScale) adds |g| x f x loveScale x share to its culture's grace or wrath.
void addImpulse(const World& w, std::vector<MemAdd>& mem, const Settlement& s, double g, double f, double loveScale) {
	const Culture* cu = state::culture(w, s.culture);
	const double cpop = cu ? finiteOr(cu->pop, 0) : 0;
	const double sp = std::max(0.0, finiteOr(s.pop, 0));
	const double share = cpop > sp && cpop > 0 ? sp / cpop : 1.0;
	const double impulse = std::fabs(g) * f * loveScale * share;
	MemAdd& m = memOf(mem, s.culture);
	if (g > 0) m.grace += impulse;
	else if (g < 0) m.wrath += impulse;
}

// The culture heard its god; the deed enters the fading favour sums (x wsFavour) and, unless it is building its
// Ark, the long memory (x wsMemory). Scales 1 = v3.1.
void commitMemory(World& w, const MemAdd& m, bool favour, double wsMemory, double wsFavour) {
	const SocietyTune& tn = tuneOf(w);
	if (!liveCulture(w, m.culture)) return;
	w.vars.num[varKey("heard", m.culture)] = w.year;
	// (v3.1, design review finding 11) the god's recent favour (Diplomacy: envy, blame, thanks), Ark or not
	// (chronicle-diplomacy) except a deed that leaves no envy (opts.favour false: the Omen of Peace)
	if (tn.favourHalfLife > 0 && favour) {
		const double fg = wsFavour == 1 ? m.grace : m.grace * wsFavour;
		const double fw = wsFavour == 1 ? m.wrath : m.wrath * wsFavour;
		if (fg > 0) w.vars.num[varKey("fgrace", m.culture)] += fg;
		if (fw > 0) w.vars.num[varKey("fwrath", m.culture)] += fw;
	}
	if (tn.arkNoMemory && buildingArk(w, m.culture)) return;
	const double mg = wsMemory == 1 ? m.grace : m.grace * wsMemory;
	const double mw = wsMemory == 1 ? m.wrath : m.wrath * wsMemory;
	if (mg > 0) w.vars.num[varKey("grace", m.culture)] += mg;
	if (mw > 0) w.vars.num[varKey("wrath", m.culture)] += mw;
}

// The bookkeeping of an answered prayer (its prayer already cleared): (v3, design review findings 3, 9) the priests
// learn from it and an answered rain or bloom prayer blesses the town (Economy: growth and K x blessedK while
// blessed); (v4) both x the culture's answeredGain, x scale (answerPrayer's partial answers).
void prayerAnswered(World& w, int sid, int cid, PrayerKind k, double scale) {
	const SocietyTune& tn = tuneOf(w);
	Settlement& s = w.settlements[size_t(sid)];
	const double gainK = mulOf(mythFactors(w, cid).answeredGain);
	const double blessed = scale == 1 ? tn.prayerBlessed : tn.prayerBlessed * scale;
	if (k == PrayerKind::Rain || k == PrayerKind::Bloom) s.blessed = std::max(finiteOr(s.blessed, 0), blessed);
	w.vars.num[varKey("answer", cid)] += 1;
	w.bus.emit(EvPrayerAnswered{ sid, k });
	const double sci = gainK == 1 && scale == 1 ? tn.prayerScience : tn.prayerScience * gainK * scale;
	civ::addScience(w, cid, sci);
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
	// (v4, SPEC-MYTHOS 3.5) compassion, ambition, courage: rolled like the others but from a fork (the sim stream is
	// untouched), only once mythos is on (phase 0 keeps them at 0.5, bit for bit). A root people woken after the
	// god shaped the first one inherits that shaping (+- traitDrift) in all five named traits.
	if (mythOn(w)) {
		Rng mr = w.rng.fork("myth:society:traits:" + std::to_string(id));
		double* nslots[3] = { &tr.compassion, &tr.ambition, &tr.courage };
		const double* nfrom[3] = { &pt.compassion, &pt.ambition, &pt.courage };
		for (int k = 0; k < 3; k++) {
			if (parent >= 0) *nslots[k] = clamp(traitOf(*nfrom[k]) + drift * mr.gauss(), 0.05, 0.95);
			else *nslots[k] = clamp(0.5 + tn.traitSpread * (mr.f() + mr.f() - 1), 0.05, 0.95);
		}
		if (parent < 0) {
			double* shaped[5] = { &tr.curiosity, &tr.compassion, &tr.ambition, &tr.courage, &tr.aggression };
			for (int k = 0; k < 5; k++) {
				const auto it = w.vars.num.find(kShapeKeys[k]);
				if (it == w.vars.num.end() || !isFinite(it->second)) continue;
				*shaped[k] = clamp(sat(it->second) + tn.traitDrift * mr.gauss(), 0.05, 0.95);
			}
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
	const double faith0 = clamp(finiteOr(opts.faith, 0), 0.0, 1.0);
	const double good = clamp(finiteOr(opts.good, 0), -1.0, 1.0);
	const double goodHarmed = clamp(finiteOr(opts.goodHarmed, 0), -1.0, 1.0);
	const double reach = std::max(ang, 0.5 * w.geo->step);
	// the act's own sign (mercy / wrath, the event): the stronger of the two goods when it hurt someone
	double actGood = good;
	if (!opts.harmed.empty() && std::fabs(goodHarmed) > std::fabs(good)) actGood = goodHarmed;
	// (v4, SPEC-MYTHOS 3.2) how near the god comes: the witnesses' faith, jaded, memory and favour (all 1 under
	// Wonders; a natural event is no act of the god and is never scaled)
	const WitnessScale ws = opts.natural || !mythOn(w) ? WitnessScale{} : mythos::witnessScale(w, cell, opts.kind);
	const double wsFaith = std::max(0.0, mulOf(ws.faith)), wsJaded = std::max(0.0, mulOf(ws.jaded));
	const double wsMemory = std::max(0.0, mulOf(ws.memory)), wsFavour = std::max(0.0, mulOf(ws.favour));
	const double faith = wsFaith == 1 ? faith0 : clamp(faith0 * wsFaith, 0.0, 1.0);

	// Brush powers: faith arrives already x elapsed seconds, but the love rule has no time factor;
	// scale it by the same elapsed seconds (faith / def.faith, at most 1) so love per SECOND of
	// brushing follows the formula instead of love per call (4 calls / s would saturate it in ~2 s).
	const powers::PowerDef* def = powers::byId(opts.kind);
	double loveScale = 1;
	if (def && def->brush && def->faith > 0) loveScale = clamp(faith0 / def->faith, 0.0, 1.0);

	const std::vector<int> ids = civ::near(w, cell, reach);
	std::vector<int> noticed;
	int count = 0;
	// (v3) what each culture will remember of this act: grace / wrath impulses, and that it was heard
	std::vector<MemAdd> mem;
	// (v4, 3.10) the peoples this act harmed: Victory prayers of their enemies are answered
	std::vector<int> struck;
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
		// (v4, 3.10) a Sun prayer over a flooded or sodden town: Raise Land under it answers it
		const bool dry = s.prayer && s.prayer->kind == PrayerKind::Sun && opts.kind == "raise"
			&& (flooded(w, s) || (w.wet.size() == size_t(w.n) && w.wet[size_t(s.cell)] > tn.sunWet));
		if (s.prayer && !opts.kind.empty() && (opts.kind == toStr(s.prayer->kind) || bread || dry)) {
			answered = true;
			const double gainK = mulOf(mythFactors(w, s.culture).answeredGain);
			fs = gainK == 1 ? faith * tn.prayerFaith : faith * tn.prayerFaith * gainK;
			g = 1;
		} else if (!harmed && g <= 0) {
			fs = faith * tn.harmlessFaith;
		}
		if (g < 0 && !opts.natural && s.culture >= 0 && std::find(struck.begin(), struck.end(), s.culture) == struck.end())
			struck.push_back(s.culture);
		const double dev0 = sat(finiteOr(s.devotion, 0));
		const double jad0 = sat(finiteOr(s.jaded, 0));
		const double love0 = clamp(finiteOr(s.love, 0), -1.0, 1.0);
		const double gain = fs * f * (1 - jad0) * (1 - dev0);
		s.devotion = sat(dev0 + gain);
		s.jaded = std::min(1.0, wsJaded == 1 ? jad0 + tn.witnessJaded * fs * f : jad0 + tn.witnessJaded * fs * f * wsJaded);
		s.love = clamp(love0 + tn.witnessLove * g * f * loveScale * (1 - std::fabs(love0)), -1.0, 1.0);
		const double seenAdd = fs * f;
		if (seenAdd > 0 && !opts.kind.empty()) {
			if (Culture* cu = state::culture(w, s.culture)) cu->seen[opts.kind] += seenAdd;
		}
		const bool gained = gain > 0 || s.love != love0 || seenAdd > 0 || answered;
		if (!opts.natural && s.culture >= 0) addImpulse(w, mem, s, g, f, loveScale);
		if (answered) {
			const PrayerKind k = s.prayer->kind;
			const int cid = s.culture;
			s.prayer.reset();
			prayerAnswered(w, sid, cid, k, 1.0);
		}
		if (gained) {
			count++;
			if (int(noticed.size()) < tn.maxWitnessIds) noticed.push_back(sid);
		}
	}
	// (v4, 3.10) "stand with us": a capital praying for victory is answered by any wrath of its god on a people it
	// is at war with, wherever that falls (the act is witnessed there as an answered prayer)
	if (!struck.empty()) {
		const int nSet = int(w.settlements.size());
		for (int sid = 0; sid < nSet; sid++) {
			Settlement& s = w.settlements[size_t(sid)];
			if (s.dead || !s.prayer || s.prayer->kind != PrayerKind::Victory || !w.valid(s.cell)) continue;
			bool enemy = false;
			for (const int e : struck) if (e != s.culture && society::atWar(w, s.culture, e)) { enemy = true; break; }
			if (!enemy) continue;
			const double gainK = mulOf(mythFactors(w, s.culture).answeredGain);
			const double fs = faith * tn.prayerFaith * gainK;
			const double dev0 = sat(finiteOr(s.devotion, 0));
			const double jad0 = sat(finiteOr(s.jaded, 0));
			const double love0 = clamp(finiteOr(s.love, 0), -1.0, 1.0);
			s.devotion = sat(dev0 + fs * (1 - jad0) * (1 - dev0));
			s.jaded = std::min(1.0, jad0 + tn.witnessJaded * fs * wsJaded);
			s.love = clamp(love0 + tn.witnessLove * loveScale * (1 - std::fabs(love0)), -1.0, 1.0);
			if (fs > 0 && !opts.kind.empty()) {
				if (Culture* cu = state::culture(w, s.culture)) cu->seen[opts.kind] += fs;
			}
			addImpulse(w, mem, s, 1.0, 1.0, loveScale);
			const int cid = s.culture;
			s.prayer.reset();
			prayerAnswered(w, sid, cid, PrayerKind::Victory, 1.0);
			count++;
			if (int(noticed.size()) < tn.maxWitnessIds) noticed.push_back(sid);
		}
	}
	// (v3) the long memory: every culture that saw it heard its god; while it builds its Ark the deed
	// moves love but is not written into the memory (nothing is bought in the last years)
	for (const MemAdd& m : mem) commitMemory(w, m, opts.favour, wsMemory, wsFavour);
	w.god.mercy = finiteOr(w.god.mercy, 0) + std::max(0.0, actGood) * faith0;
	w.god.wrath = finiteOr(w.god.wrath, 0) + std::max(0.0, -actGood) * faith0;
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

Prophet* raiseProphet(World& w, int sid, const RaiseOpts& opts) {
	const SocietyTune& tn = tuneOf(w);
	if (!liveSettlement(w, sid)) return nullptr;
	const int cid = w.settlements[size_t(sid)].culture;
	if (!liveCulture(w, cid) || w.cultures[size_t(cid)].prophet) return nullptr;
	Rng nr = w.rng.fork("prophet:" + std::to_string(cid) + ":" + std::to_string(int64_t(std::floor(w.year))));
	std::string name = names::person(w.cultures[size_t(cid)].lang, nr);
	if (name.empty()) name = "the Nameless";
	// (v4, 3.8) a Child messenger lives childYears (80) instead of prophetYears (60)
	double years = tn.prophetYears;
	if (opts.figure >= 0) {
		const Figure* f = characters::figure(w, opts.figure);
		const double child = w.tune<CharactersTune>().childYears;
		if (f && f->archetype == uint8_t(MessengerArchetype::Child) && isFinite(child) && child > 0) years = child;
	}
	const double term = years * (1 + tn.prophetJitter * (2 * w.rng.f() - 1));
	Culture& c = w.cultures[size_t(cid)];
	c.prophet = Prophet{ name, sid, w.year + term, opts.figure };
	clearProphetVars(w, cid);
	w.vars.num[varKey("prophetNext", cid)] = w.year + travelYears(w, w.cultures[size_t(cid)]);
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
	// (v4, 1.4) each culture's weight in S (Temples rite, Tithe, the House, a grown people ...); 1 = v3.1
	std::vector<double> weightS(w.cultures.size(), 1.0);
	for (size_t k = 0; k < weightS.size(); k++) weightS[k] = std::max(0.0, mulOf(mythFactors(w, int(k)).aweS));
	double S = 0;
	int live = 0;
	for (const Settlement& s : w.settlements) {
		if (s.dead) continue;
		live++;
		const double weight = tn.sCamp + (s.has(Building::SHRINE) ? tn.sShrine : 0) + (s.has(Building::TEMPLE) ? tn.sTemple : 0);
		const double cw = s.culture >= 0 && size_t(s.culture) < weightS.size() ? weightS[size_t(s.culture)] : 1.0;
		S += sat(finiteOr(s.devotion, 0)) * weight * (1 + std::clamp(s.tier, 0, 4) / 2.0) * cw;
	}
	if (live == 0) return 0;
	const double income = tn.incomeBase + tn.incomeK * std::log(1 + S);
	// (v4, 3.1) the world's archetype: a Dangerous world prays harder (Awe income x 1.1); Balanced = 1
	const double arch = mythOn(w) ? std::max(0.0, mulOf(mythos::archetypeParams(w).aweIncome)) : 1.0;
	return arch == 1 ? income : income * arch;
}

// ---- (v4 mythos) the owner APIs Mythos acts through (SPEC-MYTHOS 1.7). M5: shapeTraits, nudge, answerPrayer (and
// remember, which is small); secede stays neutral until M5b. Nothing here runs unless a mythos module calls it.

bool shapeTraits(World& w, int cultureId, const Traits& t) {
	if (!liveCulture(w, cultureId)) return false;
	const double v[5] = { t.curiosity, t.compassion, t.ambition, t.courage, t.aggression };
	for (const double x : v) if (!isFinite(x)) return false;
	Culture& c = w.cultures[size_t(cultureId)];
	double* slot[5] = { &c.traits.curiosity, &c.traits.compassion, &c.traits.ambition, &c.traits.courage, &c.traits.aggression };
	for (int k = 0; k < 5; k++) {
		*slot[k] = sat(v[k]);
		// the second band and later awakenings inherit the choice (newCulture: +- traitDrift)
		w.vars.num[kShapeKeys[k]] = *slot[k];
	}
	return true;
}

void nudge(World& w, int sid, double dDevotion, double dLove) {
	if (!liveSettlement(w, sid)) return;
	Settlement& s = w.settlements[size_t(sid)];
	if (isFinite(dDevotion) && dDevotion != 0) s.devotion = sat(finiteOr(s.devotion, 0) + dDevotion);
	if (isFinite(dLove) && dLove != 0) s.love = clamp(finiteOr(s.love, 0) + dLove, -1.0, 1.0);
}

void remember(World& w, int cultureId, double grace, double wrath) {
	if (!liveCulture(w, cultureId)) return;
	MemAdd m{ cultureId, std::max(0.0, finiteOr(grace, 0)), std::max(0.0, finiteOr(wrath, 0)) };
	if (!(m.grace > 0) && !(m.wrath > 0)) return;
	commitMemory(w, m, true, 1.0, 1.0);
}

Culture* secede(World& w, const std::vector<int>& towns, BirthReason reason, int rivalId) {
	(void)w; (void)towns; (void)reason; (void)rivalId;
	return nullptr;
}

bool answerPrayer(World& w, int sid, double scale) {
	const SocietyTune& tn = tuneOf(w);
	if (!liveSettlement(w, sid) || !isFinite(scale) || !(scale > 0)) return false;
	Settlement& s = w.settlements[size_t(sid)];
	if (!s.prayer || !liveCulture(w, s.culture)) return false;
	const double k = std::min(1.0, scale);
	// the effect of an answered prayer at the centre of an act (witness), x scale
	const double gainK = mulOf(mythFactors(w, s.culture).answeredGain);
	const double fs = kAnswerFaith * tn.prayerFaith * gainK * k;
	const double dev0 = sat(finiteOr(s.devotion, 0));
	const double jad0 = sat(finiteOr(s.jaded, 0));
	const double love0 = clamp(finiteOr(s.love, 0), -1.0, 1.0);
	s.devotion = sat(dev0 + fs * (1 - jad0) * (1 - dev0));
	s.jaded = std::min(1.0, jad0 + tn.witnessJaded * fs);
	s.love = clamp(love0 + tn.witnessLove * k * (1 - std::fabs(love0)), -1.0, 1.0);
	const PrayerKind kind = s.prayer->kind;
	const int cid = s.culture;
	std::vector<MemAdd> mem;
	addImpulse(w, mem, s, k, 1.0, 1.0);
	s.prayer.reset();
	prayerAnswered(w, sid, cid, kind, k);
	for (const MemAdd& m : mem) commitMemory(w, m, true, 1.0, 1.0);
	return true;
}

} // namespace society
} // namespace dm
