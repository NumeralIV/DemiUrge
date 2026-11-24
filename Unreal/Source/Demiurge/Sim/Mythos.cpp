// DEMIURGE — Sim/Mythos.cpp : the mythos core (SPEC-MYTHOS 1.4-1.5, 3.1-3.5, 4; track M1 "mythos-core").
// Header: Sim/Mythos.h (FROZEN by M0). Owner: mythos-core (M1 / M1b / M1c, key "mythos").
//
// What M1 builds (v1): the presence stance and its witness multipliers (3.2), the four fading impressions and the
// aspect (3.3), the will (agency, obedience, dependence: 3.4), the first people's shaping (ShapePeople card, 3.5)
// and the Presence card, the archetype's mythic seeds (3.1; its generator parameters are read by the owners
// through archetypeParams), the yearly Factors assembly (traits, decrees and customs, the rite, aspect, will,
// survivor memory, beliefs about death) and the legacy verdict (4). v2 / v3 parts (blessings' consequences,
// disputes' answers, warGoal, coming of age, withdrawal) are left to M1b / M1c: their entry points refuse or
// return neutral values here (townFactors already reads blessings that exist).
//
// How mythos learns what happened: attach() subscribes recorders to w.bus that ONLY append MythAct records to
// mythos.acts (they never act, 1.5). The yearly own pass consumes them and drops them. Act kinds:
//   <power id>                    a god's act witnessed by settlement `settlement` (miracle:witnessed; one act
//                                 per witnessing settlement; good = the event's good, never 0)
//   "prayer:answered"             settlement's prayer was answered
//   "breach"                      a decree was broken at settlement (Doctrine's breach)
//   "decree:<area>:<clarity>:<stance>"   Doctrine's decree event (stance "repealed" for a repeal)
//   "word:<clarity>:<outcome>:<content>" Doctrine's word event
//   "judgement:<by>:<sentence>:<card>"   Doctrine's judgement event (a Disaster is just when the card's data
//                                 ["deedWrong"] (by the deed) / ["heartWrong"] (by the heart) is 1; Forgive of a repeat
//                                 (data["repeat"]) costs forgiveRepeat obedience)
//   "pact:<what>"                 Doctrine's pact event
//   "card:<kind>:<stage>:<id>:<choice>"  a card about the culture was answered (the god acted on them) or lapsed;
//                                 a DecreeConflict's case lines (3.7) that are Mythos's are applied from it
// Natural events are not acts: Society witnesses them with good 0 (and "nuke" is the people's own fire).
//
// Gates (1.6, 5.1): MythosTune::phase 0 = nothing at all (no subscription, no record, neutral reads). At phase >= 1
// with emergent off, a culture's Factors move only through what the god did to it: a decree or custom it holds
// (customs exist only under emergent), its rite, a survivor memory, a death belief, and, once the god has used a
// mythos act on it ("touched": a word, decree, judgement, pact, answered card, presence change, or the shaping),
// its aspect and dependence effects and the shaped traits. Agency's research and schism terms and dependence's
// research term are emergent-only. Ordinary powers never touch the gates, so a god who ignores every card and
// Word power leaves every Factors record neutral and the sim unchanged.
//
// The first people: the ShapePeople answer sets every woken people's five traits through society::shapeTraits;
// peoples woken later inherit them in Society's newCulture (it records the shaping), Mythos only marks them shaped.
//
// Faith::data keys (private to Mythos; prefix "m_"): m_touched, m_shaped, m_clS / m_clW (clarity sums),
// m_ans (answered prayers, ~century window), m_lastDeed, m_fpDead (famine and plague losses), m_peak (peak people).
// mythos.num keys: mythos_waking, mythos_presenceCard, mythos_shaped, mythos_shape0..4 (TraitLevel), mythos_presenceAge,
// mythos_forest0, mythos_lastYear, mythos_breach<sid> (year of the last breach at that settlement).
//
// Not in a TUNE struct (the headers are frozen): the per-stance decree / rite effect sizes of 3.6 / 3.13 and the
// trait effect sizes of 3.5 are the spec's starting values in kDecreeFx / traitFold below. The lead may move them
// into MythosTune between waves.
#include "Mythos.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "Cards.h"
#include "Characters.h"
#include "Civ.h"
#include "Doctrine.h"
#include "Faiths.h"
#include "Keepers.h"
#include "Legends.h"
#include "Life.h"
#include "Omens.h"
#include "Powers.h"
#include "Renewal.h"
#include "Society.h"

namespace dm {

namespace {

// Event-bus subscriptions of this world (never saved; re-made by attach).
struct MythosScratch {
	std::vector<EventBus::Token> tokens;
};

constexpr int kMaxActs = 96;                 // MythAct cap (MythosTypes.h: [last 96])
constexpr double kRemoveSurvivorLove = -0.02;   // 3.12: Remove the ruler, survivors' love (not in DoctrineTune)
constexpr double kHealerCare = 0.25;         // 4: a healer of the culture adds this to its care
constexpr double kShapeDrift = 0.05;         // 3.5: later awakenings inherit the shaping +- this
constexpr double kAnsweredLife = 100;        // 3.4: "answered prayers in the last century" as a fading count

double fin(double x, double d = 0) { return std::isfinite(x) ? x : d; }
double get(const NumBag& b, const char* k, double d = 0) {
	const auto it = b.find(k);
	return it == b.end() ? d : fin(it->second, d);
}
double fadeK(double dt, double halfLife) { return halfLife > 0 ? std::pow(0.5, dt / halfLife) : 0.0; }
// share of the gap closed in dt years at `rate` per year
double driftK(double dt, double rate) { return 1 - std::pow(1 - clamp(rate, 0.0, 1.0), dt); }
void mulK(double& x, double m, double k) { x *= 1 + (m - 1) * k; }
void addK(double& x, double a, double k) { x += a * k; }

const MythosTune& tuneOf(const World& w) { return w.tune<MythosTune>(); }
bool on(const World& w) { return tuneOf(w).phase >= 1; }

bool liveCulture(const World& w, int c) {
	return c >= 0 && size_t(c) < w.cultures.size() && !w.cultures[size_t(c)].dead;
}
bool liveSettlement(const World& w, int s) {
	return s >= 0 && size_t(s) < w.settlements.size() && !w.settlements[size_t(s)].dead;
}
Faith* faithOf(World& w, int c) {
	return c >= 0 && size_t(c) < w.mythos.faith.size() ? &w.mythos.faith[size_t(c)] : nullptr;
}

std::vector<std::string> split(const std::string& s) {
	std::vector<std::string> out;
	size_t a = 0;
	for (;;) {
		const size_t b = s.find(':', a);
		out.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
		if (b == std::string::npos) break;
		a = b + 1;
	}
	return out;
}

bool isWitnessKind(const std::string& k) { return k.find(':') == std::string::npos && k != "breach"; }

// Record an act (handlers only). Keeps at most kMaxActs, dropping the oldest witness act first.
void record(World& w, MythAct a) {
	std::vector<MythAct>& acts = w.mythos.acts;
	if (int(acts.size()) >= kMaxActs) {
		auto it = std::find_if(acts.begin(), acts.end(), [](const MythAct& x) { return isWitnessKind(x.kind); });
		acts.erase(it != acts.end() ? it : acts.begin());
	}
	a.year = w.year;
	acts.push_back(std::move(a));
}

// ---------------------------------------------------------------- the will's inputs per culture

struct CultureAgg {
	double pop = 0, devW = 0, loveW = 0;     // live settlement people, sum of devotion x pop, love x pop
	int towns = 0, academies = 0;            // live settlements, those with an academy
	bool blessed = false;                    // a blessing of theirs is active (3.16)
	double fpLoss = 0;                       // this year's losses of famine / plague towns
	double devotion() const { return pop > 0 ? devW / pop : 0.5; }
	double love() const { return pop > 0 ? loveW / pop : 0; }
};

std::vector<CultureAgg> aggregate(const World& w) {
	std::vector<CultureAgg> a(w.cultures.size());
	for (const Settlement& s : w.settlements) {
		if (s.dead || s.culture < 0 || size_t(s.culture) >= a.size()) continue;
		CultureAgg& g = a[size_t(s.culture)];
		const double p = std::max(0.0, fin(s.pop));
		g.pop += p;
		g.devW += sat(fin(s.devotion, 0.5)) * p;
		g.loveW += clamp(fin(s.love), -1.0, 1.0) * p;
		g.towns++;
		if (s.has(Building::ACADEMY)) g.academies++;
		if ((s.famine > 0 || fin(s.plague) > 0) && fin(s.growth) < 0) g.fpLoss += -fin(s.growth);
	}
	for (const TownMyth& t : w.mythos.towns) {
		if (t.blessing == BlessingKind::None || !(t.blessedUntil > w.year) || !liveSettlement(w, t.settlement)) continue;
		const int c = w.settlements[size_t(t.settlement)].culture;
		if (c >= 0 && size_t(c) < a.size()) a[size_t(c)].blessed = true;
	}
	return a;
}

// Decrees the god gave (not customs) and their mean strictness (3.4 burden).
void godDecrees(const World& w, int c, int& count, double& burden) {
	count = 0;
	double sum = 0;
	for (const Decree& d : w.mythos.decrees) {
		if (d.culture != c || d.custom || d.stance < 0 || d.area == DecreeArea::Count) continue;
		count++;
		sum += doctrine::strictness(w, d.area, d.stance);
	}
	burden = count > 0 ? sum / count : 0;
}

bool writingShared(const World& w, int c) {
	for (const Revelation& r : w.mythos.revelations)
		if (r.culture == c && r.kind == uint8_t(RevelationKind::Writing) && r.outcome == uint8_t(RevelationOutcome::Shared)) return true;
	return false;
}

double certaintyK(const World& w, const Faith& f) {
	const int s = f.deathSure;
	const std::array<double, 3>& k = w.tune<DoctrineTune>().certainty;
	return s >= 1 && s <= 3 ? k[size_t(s - 1)] : 0.0;
}

const Decree* decreeOf(const World& w, int c, DecreeArea area) {
	for (const Decree& d : w.mythos.decrees)
		if (d.culture == c && d.area == area && d.stance >= 0) return &d;
	return nullptr;
}

// (B1) `family` = the emergent family that reaches every people (MythosTune::Family); a touched people always
bool touchedOf(const World& w, int c, unsigned family) {
	if (tuneOf(w).emergentOn(family)) return true;
	const Faith* f = mythos::faith(w, c);
	return f && get(f->data, "m_touched") > 0;
}

// ---------------------------------------------------------------- Factors

// One stance's effects at kept = 1 (3.6, 3.13, 3.22), scaled by k.
void applyStance(const World& w, Factors& F, DecreeArea area, int st, double k) {
	const DoctrineTune& D = w.tune<DoctrineTune>();
	switch (area) {
	case DecreeArea::Violence:
		if (st == int(ViolenceStance::Peace)) { addK(F.warV, -0.15, k); addK(F.aggression, -0.1, k); mulK(F.battle, 0.9, k); }
		else if (st == int(ViolenceStance::Defence)) mulK(F.defence, 1.15, k);
		else if (st == int(ViolenceStance::Valour)) { mulK(F.battle, 1.1, k); addK(F.aggression, 0.1, k); addK(F.relDrift, -0.005, k); }
		break;
	case DecreeArea::Wealth:
		if (st == int(WealthStance::Share)) { mulK(F.famineLoss, 0.8, k); mulK(F.growthR, 0.98, k); mulK(F.research, 0.95, k); }
		else if (st == int(WealthStance::Tithe)) mulK(F.aweS, 1.15, k);
		else if (st == int(WealthStance::Gain)) { mulK(F.tradeEvery, 0.8, k); mulK(F.research, 1.04, k); }
		break;
	case DecreeArea::Worship:
		if (st == int(Rite::Prayer)) { mulK(F.prayerChance, 1.3, k); mulK(F.answeredGain, 1.25, k); }
		else if (st == int(Rite::Temples)) mulK(F.aweS, 1.2, k);
		else if (st == int(Rite::Kindness)) { mulK(F.plagueDeath, 0.8, k); addK(F.loveTarget, 0.03, k); addK(F.relDrift, 0.003, k); }
		else if (st == int(Rite::Art)) mulK(F.research, 1.03, k);
		else if (st == int(Rite::World)) { mulK(F.farmConvert, 0.8, k); mulK(F.co2Uptake, 1.1, k); }
		break;
	case DecreeArea::Honesty:
		if (st == int(HonestyStance::Truth)) addK(F.relDrift, 0.002, k);
		else if (st == int(HonestyStance::Cunning)) { mulK(F.research, 1.03, k); mulK(F.warStrength, 0.95, k); }
		break;
	case DecreeArea::Hospitality:
		if (st == int(HospitalityStance::Open)) { addK(F.relDrift, 0.01, k); mulK(F.plagueHop, 1.3, k); }
		else if (st == int(HospitalityStance::Guarded)) addK(F.relDrift, 0.004, k);
		else if (st == int(HospitalityStance::Closed)) { addK(F.relDrift, -0.008, k); mulK(F.plagueHop, 0.7, k); }
		break;
	case DecreeArea::Care:
		if (st == int(CareStance::Shelter)) { mulK(F.famineLoss, 0.75, k); mulK(F.plagueDeath, 0.85, k); mulK(F.growthR, 0.97, k); }
		else if (st == int(CareStance::Kin)) mulK(F.famineLoss, 0.9, k);
		else if (st == int(CareStance::Strength)) { mulK(F.growthR, 1.03, k); mulK(F.famineLoss, 1.2, k); mulK(F.battle, 1.05, k); }
		break;
	case DecreeArea::Land:
		if (st == int(LandStance::Dominion)) { mulK(F.farmConvert, D.dominionFarm, k); mulK(F.K, D.dominionK, k); }
		else if (st == int(LandStance::Stewardship)) { mulK(F.K, D.stewardK, k); if (k >= 0.5) F.farmAncient = false; }
		else if (st == int(LandStance::Equal)) { mulK(F.K, D.equalK, k); if (k >= 0.5) { F.farmAncient = false; F.claimAncient = false; } }
		break;
	default: break;
	}
}

// The shaped traits' strengths and problems (3.5); curiosity's research and aggression's relations terms are
// already in Economy / Diplomacy, aggression's battle term is folded here.
void traitFold(Factors& F, const Traits& t) {
	const double c = sat(fin(t.compassion, 0.5)), a = sat(fin(t.ambition, 0.5)), k = sat(fin(t.courage, 0.5));
	const double g = sat(fin(t.aggression, 0.5));
	F.famineLoss *= 1 - 0.25 * c;
	F.plagueDeath *= 1 - 0.15 * c;
	F.loveTarget += 0.03 * c;
	F.battle *= (1 - 0.1 * c) * (1 + 0.1 * g);
	F.expandCooldown *= (1 + 0.2 * c) * (1 - 0.2 * a);
	F.growthR *= 1 + 0.05 * a;
	F.defence *= 1 + 0.15 * k;
	F.warStrength *= 1 - 0.2 * k;
}

void finite(Factors& F) {
	for (double* p : { &F.growthR, &F.K, &F.famineLoss, &F.plagueDeath, &F.research, &F.expandCooldown, &F.farmConvert,
	                   &F.prayerChance, &F.answeredGain, &F.unansweredPenalty, &F.aweS, &F.schism, &F.battle, &F.defence,
	                   &F.plagueHop, &F.warStrength, &F.tradeEvery, &F.naturalRate, &F.harmTaken, &F.co2Uptake })
		*p = std::max(0.0, fin(*p, 1));
	for (double* p : { &F.devotionBase, &F.loveTarget, &F.warV, &F.aggression, &F.relDrift }) *p = fin(*p, 0);
	for (double& e : F.expect) e = std::max(0.0, fin(e, 1));
}

Factors assemble(const World& w, int c) {
	Factors F;
	if (!liveCulture(w, c) || size_t(c) >= w.mythos.faith.size()) return F;
	const MythosTune& T = tuneOf(w);
	const DoctrineTune& D = w.tune<DoctrineTune>();
	const Faith& f = w.mythos.faith[size_t(c)];
	const Culture& cu = w.cultures[size_t(c)];
	const bool touchedA = touchedOf(w, c, MythosTune::FamAspect);       // (B1) the aspect family
	const bool touchedD = touchedOf(w, c, MythosTune::FamDependence);   // (B1) the dependence family
	// archetype: the devotion baseline offset (Awe income and natural rates are read by Society / Disasters
	// from archetypeParams directly; Balanced is neutral)
	F.devotionBase += mythos::archetypeParams(w).devotion;
	// the shaped traits
	if (T.emergentOn(MythosTune::FamTraits) || get(f.data, "m_shaped") > 0) traitFold(F, cu.traits);
	// decrees and customs (customs at half strength), the rite among them
	for (const Decree& d : w.mythos.decrees) {
		if (d.culture != c || d.stance < 0) continue;
		const double k = sat(fin(d.kept)) * (d.custom ? D.customStrength : 1.0);
		if (k > 0) applyStance(w, F, d.area, d.stance, k);
	}
	// aspect (3.3)
	if (touchedA) {
		if (f.aspect == Aspect::Parent) { F.prayerChance *= T.parentPray; F.loveTarget += T.parentLove; }
		else if (f.aspect == Aspect::Mystery) F.devotionBase += T.mysteryDevotion;
		else if (f.aspect == Aspect::Threat) F.loveTarget += T.threatLove;
	}
	// the will (3.4)
	const double ag = sat(fin(f.agency, 0.3)), dep = sat(fin(f.dependence, 0.3));
	{
		const bool emA = T.emergentOn(MythosTune::FamAgency), emD = T.emergentOn(MythosTune::FamDependence);
		if (emA || emD) {                    // one product, as before the families were split (same bits)
			const double ra = emA ? 1 + T.researchAgency * (ag - 0.4) : 1.0;
			const double rd = emD ? 1 - T.dependResearch * dep : 1.0;
			F.research *= ra * rd;
		}
		if (emA) F.schism *= 0.7 + T.schismAgency * ag;
	}
	if (touchedD) {
		F.prayerChance *= 1 + T.dependPray * dep;
		F.unansweredPenalty *= 0.6 + T.dependPenalty * dep;
	}
	// the survivors of a sentence (3.12)
	if (f.survivorUntil > w.year) F.loveTarget += fin(f.survivorLove);
	// beliefs about death (3.27)
	const double dk = certaintyK(w, f);
	if (dk > 0) {
		switch (DeathBelief(f.death)) {
		case DeathBelief::FurtherShore: F.defence *= 1 + D.shoreDefence * dk; F.aggression += D.shoreAggr * dk; break;
		case DeathBelief::Turning: F.farmConvert *= 1 - D.turnFarm * dk; F.loveTarget += D.turnLove * dk; F.research *= 1 - D.turnResearch * dk; break;
		case DeathBelief::Return: F.devotionBase += D.returnDevotion * dk; break;
		case DeathBelief::LongSleep: F.research *= 1 + D.sleepResearch * dk; F.devotionBase += D.sleepDevotion * dk; break;
		default: break;
		}
	}
	finite(F);
	return F;
}

// ---------------------------------------------------------------- faith records

void touch(World& w, int c) {
	if (Faith* f = faithOf(w, c)) f->data["m_touched"] = 1;
}

// One Faith (and neutral Factors) per culture ever born. Root cultures get the archetype's seeds; daughters
// inherit their parent's view of the god (Doctrine's and Faiths' fields start at their defaults).
void ensureRecords(World& w) {
	MythosState& m = w.mythos;
	const ArchetypeParams ap = mythos::archetypeParams(w);
	while (m.faith.size() < w.cultures.size()) {
		const int c = int(m.faith.size());
		const Culture& cu = w.cultures[size_t(c)];
		Faith f;
		if (cu.parent >= 0 && size_t(cu.parent) < m.faith.size()) {
			const Faith& p = m.faith[size_t(cu.parent)];
			f.aspect = p.aspect; f.image = p.image; f.agency = p.agency; f.obedience = p.obedience;
			f.dependence = p.dependence; f.selfReliance = p.selfReliance; f.clarity = p.clarity; f.visible = p.visible;
			f.judgedBy = p.judgedBy; f.mercy = p.mercy; f.sternness = p.sternness;
			f.survivorLove = p.survivorLove; f.survivorFear = p.survivorFear; f.survivorUntil = p.survivorUntil;
			f.data = p.data;
			f.data.erase("m_fpDead"); f.data.erase("m_peak");
		} else {
			f.image[size_t(Aspect::Threat) - 1] += ap.threatSeed;
			f.image[size_t(Aspect::Mystery) - 1] += ap.mysterySeed;
			// a people woken after the shaping: Society's newCulture gave them the shaped traits (+- drift)
			if (get(m.num, "mythos_shaped") > 0) { f.data["m_shaped"] = 1; f.data["m_touched"] = 1; }
		}
		f.aspectSince = w.year;
		m.faith.push_back(std::move(f));
	}
	while (m.factors.size() < w.cultures.size()) m.factors.push_back(Factors{});
}

// ---------------------------------------------------------------- the first people (3.5)

bool decodeShape(const MythosTune& T, int choice, std::array<int, 5>& lv) {
	if (choice < 0 || choice >= 243) return false;
	int x = choice, hi = 0, lo = 0;
	for (int i = 0; i < 5; i++) { lv[size_t(i)] = x % 3; x /= 3; }
	for (int v : lv) { if (v == int(TraitLevel::High)) hi++; else if (v == int(TraitLevel::Low)) lo++; }
	return hi <= T.maxHigh && lo >= hi;
}

// Set the five shaped traits of culture c (levels, +- drift from a fork when drift > 0).
void shapeCulture(World& w, int c, const std::array<int, 5>& lv, double drift) {
	if (!liveCulture(w, c)) return;
	const MythosTune& T = tuneOf(w);
	std::array<double, 5> v{};
	Rng r = w.rng.fork("myth:mythos:shape:" + std::to_string(c) + ":" + std::to_string(int64_t(std::floor(w.year))));
	for (size_t i = 0; i < 5; i++) {
		const int l = std::clamp(lv[i], 0, 2);
		v[i] = T.traitLevel[size_t(l)];
		if (drift > 0) v[i] = sat(v[i] + r.range(-drift, drift));
	}
	Traits t = w.cultures[size_t(c)].traits;
	t.curiosity = v[0]; t.compassion = v[1]; t.ambition = v[2]; t.courage = v[3]; t.aggression = v[4];
	society::shapeTraits(w, c, t);
	if (Faith* f = faithOf(w, c)) { f->data["m_shaped"] = 1; f->data["m_touched"] = 1; }
}

bool answerShape(World& w, const Card& card, int choice) {
	const MythosTune& T = tuneOf(w);
	std::array<int, 5> lv{};
	if (choice == 243) {                     // leave it to chance: a forked roll that keeps the budget rule
		Rng r = w.rng.fork("myth:mythos:shape:chance:" + std::to_string(card.id));
		for (int tries = 0; tries < 64; tries++) {
			const int x = r.integer(0, 242);
			if (decodeShape(T, x, lv)) break;
			lv = { 1, 1, 1, 1, 1 };
		}
	} else if (!decodeShape(T, choice, lv)) {
		return false;
	}
	MythosState& m = w.mythos;
	m.num["mythos_shaped"] = 1;
	for (int i = 0; i < 5; i++) m.num["mythos_shape" + std::to_string(i)] = lv[size_t(i)];
	ensureRecords(w);
	// the woken peoples (root cultures, ascending id): the others with drift, then the card's own exactly (last,
	// so Society's record of the shaping, which later awakenings inherit, is the chosen one)
	for (const Culture& cu : w.cultures)
		if (!cu.dead && cu.parent < 0 && cu.id != card.culture) shapeCulture(w, cu.id, lv, kShapeDrift);
	shapeCulture(w, card.culture, lv, 0.0);
	return true;
}

// ---------------------------------------------------------------- the presence stance

void setStance(World& w, Presence p) {
	MythosState& m = w.mythos;
	const Presence prev = m.presence;
	if (prev == p) return;
	m.presence = p;
	m.presenceSince = w.year;
	w.bus.emit(EvPresence{ toStr(p), toStr(prev) });
}

// The living messenger (prophet) of any culture whose town is within ang of cell: its figure id (or -2 for an
// unnamed prophet), or -1 when none.
int messengerNear(const World& w, int cell, double ang) {
	if (!w.geo || !w.valid(cell)) return -1;
	for (const Culture& cu : w.cultures) {
		if (cu.dead || !cu.prophet || !liveSettlement(w, cu.prophet->settlement)) continue;
		const int sc = w.settlements[size_t(cu.prophet->settlement)].cell;
		if (!w.valid(sc) || w.geo->angle(cell, sc) > ang) continue;
		return cu.prophet->figure >= 0 ? cu.prophet->figure : -2;
	}
	return -1;
}

// ---------------------------------------------------------------- consuming the acts

struct Impress { std::array<double, 4> add{}; double visible = 0; bool deed = false; };

void addImage(Faith& f, Aspect a, double v) {
	if (a == Aspect::Unknown || !std::isfinite(v)) return;
	double& x = f.image[size_t(a) - 1];
	x = std::max(0.0, x + v);
}

// The Mythos-owned lines of a decree conflict's resolution (3.7; Doctrine applies the rest): choice -1 = lapsed
// (they could not keep both: obedience lapseObey). Cases in Doctrine's order (ref[0]).
void caseLine(const World& w, Faith& f, int kase, int choice) {
	const DoctrineTune& D = w.tune<DoctrineTune>();
	if (choice < 0) { f.obedience = sat(fin(f.obedience, 0.5) + D.lapseObey); return; }
	switch (kase) {
	case 0:                                  // the raided weak: hold the gate / no blood, even now
		if (choice == 0) addImage(f, Aspect::Ruler, 0.2);
		else if (choice == 1) addImage(f, Aspect::Parent, 0.2);
		break;
	case 1:                                  // the hidden stranger: the lie that saves / truth, whatever it costs
		if (choice == 0) { f.agency = sat(fin(f.agency, 0.3) + 0.03); f.judgedBy = clamp(fin(f.judgedBy) + D.judgedStep, -1.0, 1.0); }
		else if (choice == 1) { f.obedience = sat(fin(f.obedience, 0.5) + 0.05); f.judgedBy = clamp(fin(f.judgedBy) - D.judgedStep, -1.0, 1.0); }
		break;
	case 6:                                  // the clever and the true: both are mine
		if (choice == 0) addImage(f, Aspect::Mystery, 0.2);
		break;
	default: break;
	}
}

void consumeActs(World& w, const std::vector<CultureAgg>& agg) {
	MythosState& m = w.mythos;
	const MythosTune& T = tuneOf(w);
	const DoctrineTune& D = w.tune<DoctrineTune>();
	const size_t nc = m.faith.size();
	std::vector<MythAct> acts;
	acts.swap(m.acts);
	auto shareOf = [&](int sid, int c) {
		if (!liveSettlement(w, sid) || c < 0 || size_t(c) >= agg.size()) return 0.0;
		const double p = std::max(0.0, fin(w.settlements[size_t(sid)].pop));
		return agg[size_t(c)].pop > 0 ? sat(p / agg[size_t(c)].pop) : 1.0;
	};
	// 1. breaches first (a wrath on a town that broke a decree this year is just punishment)
	for (const MythAct& a : acts)
		if (a.kind == "breach" && a.settlement >= 0) m.num["mythos_breach" + std::to_string(a.settlement)] = a.year;
	auto broke = [&](int sid) {
		const auto it = m.num.find("mythos_breach" + std::to_string(sid));
		return it != m.num.end() && w.year - it->second <= D.breachYears;
	};
	// 2. witnessed acts, grouped into events (consecutive acts with the same year, kind and cell)
	std::vector<Impress> imp(nc);
	size_t i = 0;
	while (i < acts.size()) {
		const MythAct& a0 = acts[i];
		if (!isWitnessKind(a0.kind)) { i++; continue; }
		size_t j = i;
		while (j < acts.size() && isWitnessKind(acts[j].kind) && acts[j].kind == a0.kind && acts[j].cell == a0.cell && acts[j].year == a0.year) j++;
		const powers::PowerDef* def = powers::byId(a0.kind);
		const double brush = def && def->brush ? 0.25 : 1.0;
		double near = 1, far = 1, vis = 1, memK = 1;
		if (m.presence == Presence::Messengers) {
			const int fid = messengerNear(w, a0.cell, T.msgNearAng);
			near = far = fid != -1 ? T.msgFaithNear : T.msgFaithFar;
			vis = near;
			if (fid >= 0) characters::adjustCredibility(w, fid, T.msgCred);
		} else if (m.presence == Presence::Unseen) {
			near = far = memK = T.unseenMemory;
			vis = T.unseenFaith;
		}
		std::vector<int> seen;               // cultures that witnessed this event
		for (size_t k = i; k < j; k++) {
			const MythAct& a = acts[k];
			const int c = a.culture;
			if (c < 0 || size_t(c) >= nc || !(std::fabs(a.good) > 0)) continue;
			const double s = shareOf(a.settlement, c);
			Impress& I = imp[size_t(c)];
			if (a.good > 0) I.add[0] += 0.1 * s * brush * near;
			else if (broke(a.settlement)) I.add[1] += 0.3 * s * brush * far;
			else I.add[3] += 0.4 * s * std::fabs(a.good) * brush * memK;
			if (m.presence == Presence::Unseen) I.add[2] += 0.1 * s * brush;
			if (std::find(seen.begin(), seen.end(), c) == seen.end()) {
				seen.push_back(c);
				I.visible += brush * vis;
				I.deed = true;
			}
		}
		i = j;
	}
	// 3. everything else, in order
	for (const MythAct& a : acts) {
		if (isWitnessKind(a.kind) || a.kind == "breach") continue;
		const int c = a.culture;
		Faith* f = faithOf(w, c);
		if (!f) continue;
		const std::vector<std::string> p = split(a.kind);
		const std::string& head = p[0];
		if (head == "prayer") {
			imp[size_t(c)].add[0] += 0.3 * shareOf(a.settlement, c);
			f->data["m_ans"] = get(f->data, "m_ans") + 1;
		} else if (head == "decree" && p.size() >= 4) {
			DecreeArea area = DecreeArea::Count;
			Clarity cl = Clarity::Exact;
			if (!fromStr(p[1], area) || !fromStr(p[2], cl)) continue;
			const Decree* d = decreeOf(w, c, area);
			if (p[3] != "repealed" && d && d->custom) continue;   // the people's own custom: not the god's
			f->data["m_touched"] = 1;
			if (T.actDeed) imp[size_t(c)].deed = true;
			if (p[3] != "repealed" && cl != Clarity::Exact) addImage(*f, Aspect::Ruler, 0.3);
		} else if (head == "word" && p.size() >= 3) {
			Clarity cl = Clarity::Dream;
			if (!fromStr(p[1], cl)) continue;
			f->data["m_touched"] = 1;
			const double v = T.clarityValue[size_t(cl)];
			if (T.actDeed) imp[size_t(c)].deed = true;
			if (T.wordVisible > 0) imp[size_t(c)].visible += v * T.wordVisible;   // (B1) the god was heard
			f->data["m_clS"] = get(f->data, "m_clS") + v;
			f->data["m_clW"] = get(f->data, "m_clW") + 1;
			if (cl == Clarity::Exact) addImage(*f, Aspect::Ruler, 0.5);
			else if (cl == Clarity::Sign) addImage(*f, Aspect::Mystery, 0.3);
			else addImage(*f, Aspect::Parent, 0.2);
			if (p[2] == "catastrophe") addImage(*f, Aspect::Threat, D.catastropheThreat);
		} else if (head == "judgement" && p.size() >= 3) {
			f->data["m_touched"] = 1;
			if (T.actDeed) imp[size_t(c)].deed = true;
			const bool byDeed = p[1] == "deed";
			f->judgedBy = clamp(fin(f->judgedBy) + (byDeed ? -D.judgedStep : D.judgedStep), -1.0, 1.0);
			if (byDeed) addImage(*f, Aspect::Ruler, 0.2);
			const std::string& s = p[2];
			double love = 0, obey = 0;
			const Card* cd = p.size() >= 4 ? cards::find(w, std::atoi(p[3].c_str())) : nullptr;
			if (s == "forgive") {
				addImage(*f, Aspect::Parent, 0.3); f->mercy = fin(f->mercy) + 1; love = D.survForgive;
				if (cd && get(cd->data, "repeat") > 0) f->obedience = sat(fin(f->obedience, 0.5) + D.forgiveRepeat);
			} else if (s == "warn") {
				addImage(*f, byDeed ? Aspect::Ruler : Aspect::Mystery, 0.2); obey = D.survWarn; f->sternness = fin(f->sternness) + 1;
				f->obedience = sat(fin(f->obedience, 0.5) + D.warnObey);
			} else if (s == "remove") {
				addImage(*f, Aspect::Ruler, 0.3); f->sternness = fin(f->sternness) + 1; obey = D.survRemove; love = kRemoveSurvivorLove;
				f->obedience = sat(fin(f->obedience, 0.5) + D.removeObey);
			} else if (s == "disaster") {
				// just when the thing judged was wrong (Doctrine's card data: deedWrong / heartWrong; else "just")
				bool just = true;
				if (cd) {
					const char* key = byDeed ? "deedWrong" : "heartWrong";
					if (cd->data.count(key)) just = get(cd->data, key) > 0;
					else if (cd->data.count("just")) just = get(cd->data, "just") > 0;
				}
				f->sternness = fin(f->sternness) + 1;
				if (just) { addImage(*f, Aspect::Ruler, 0.3); obey = D.survJust; love = D.survJustLove; }
				else { addImage(*f, Aspect::Threat, 0.4); love = D.survUnjust; }
			} else {
				continue;
			}
			f->survivorLove = love; f->survivorFear = obey; f->survivorUntil = w.year + D.survivorYears;
		} else if (head == "pact" && p.size() >= 2) {
			f->data["m_touched"] = 1;
			if (T.actDeed) imp[size_t(c)].deed = true;
			if (p[1] == "sworn") addImage(*f, Aspect::Ruler, 0.2);
			else if (p[1] == "kept") addImage(*f, Aspect::Parent, 0.3);
			else if (p[1] == "god-broke") addImage(*f, Aspect::Threat, D.godBrokeThreat);
		} else if (head == "card" && p.size() >= 5) {
			const bool answered = p[2] == "answered";
			if (answered) { f->data["m_touched"] = 1; if (T.actDeed) imp[size_t(c)].deed = true; }
			if (p[1] == toStr(CardKind::DecreeConflict)) {
				const Card* cd = cards::find(w, std::atoi(p[3].c_str()));
				caseLine(w, *f, cd ? cd->ref[0] : -1, answered ? std::atoi(p[4].c_str()) : -1);
			}
		}
	}
	for (size_t c = 0; c < nc; c++) {
		Faith& f = m.faith[c];
		for (size_t k = 0; k < 4; k++) if (imp[c].add[k] > 0) f.image[k] += imp[c].add[k];
		f.visible = fin(f.visible) + imp[c].visible;
		if (imp[c].deed) f.data["m_lastDeed"] = w.year;
	}
}

// ---------------------------------------------------------------- the yearly own pass

void choosesAspect(World& w, int c, Faith& f) {
	const MythosTune& T = tuneOf(w);
	int best = 0;
	for (int k = 1; k < 4; k++) if (f.image[size_t(k)] > f.image[size_t(best)]) best = k;
	double second = 0;
	for (int k = 0; k < 4; k++) if (k != best) second = std::max(second, f.image[size_t(k)]);
	const double b = f.image[size_t(best)];
	if (!(b >= T.aspectMin) || !(b >= T.aspectLead * second)) return;
	const Aspect cand = Aspect(best + 1);
	if (cand == f.aspect) return;
	if (f.aspect != Aspect::Unknown && w.year - f.aspectSince < T.aspectHold) return;
	const Aspect prev = f.aspect;
	f.aspect = cand;
	f.aspectSince = w.year;
	w.bus.emit(EvAspect{ c, toStr(cand), toStr(prev) });
}

void will(World& w, int c, Faith& f, const CultureAgg& g, double dt) {
	const MythosTune& T = tuneOf(w);
	const DoctrineTune& D = w.tune<DoctrineTune>();
	const Culture& cu = w.cultures[size_t(c)];
	const ArchetypeParams ap = mythos::archetypeParams(w);
	const Presence pr = w.mythos.presence;
	const double ag = sat(fin(f.agency, 0.3)), dep = sat(fin(f.dependence, 0.3)), self = sat(fin(f.selfReliance));
	const double literacy = 0.5 * (g.towns > 0 ? double(g.academies) / g.towns : 0) + 0.5 * (writingShared(w, c) ? 1 : 0);
	const double cur = sat(fin(cu.traits.curiosity, 0.5)), comp = sat(fin(cu.traits.compassion, 0.5));
	int decrees = 0;
	double burden = 0;
	godDecrees(w, c, decrees, burden);
	const House* h = faiths::house(w, c);
	const double auth = h ? sat(fin(h->authority)) : 0;
	const double dk = certaintyK(w, f);
	const DeathBelief belief = DeathBelief(f.death);
	const bool survivors = f.survivorUntil > w.year;
	const double jb = fin(f.judgedBy);
	// agency
	double aT = T.agencyBase + T.agencyClarity * (1 - sat(fin(f.clarity, 0.5))) + T.agencyHidden * (1 - sat(fin(f.visible) / T.visibleRef))
		+ T.agencyCurious * cur + T.agencyLiteracy * literacy - T.agencyDepend * dep + ap.agency;
	if (pr == Presence::Messengers) aT += T.msgAgency;
	else if (pr == Presence::Unseen) aT += T.unseenAgency;
	if (f.aspect == Aspect::Ruler) aT += T.rulerAgency;
	else if (f.aspect == Aspect::Mystery) aT += T.mysteryAgency;
	if (jb <= -D.judgedLine) aT += D.deedAgency;
	else if (jb >= D.judgedLine) aT += D.heartAgency;
	aT = clamp(fin(aT, 0.5), T.agencyMin, T.agencyMax);
	// obedience
	double oT = T.obedienceBase + T.obedienceDevotion * g.devotion() + T.obedienceHouse * auth
		- T.obedienceBurden * ag * burden - T.obedienceDecrees * std::max(0.0, decrees - T.obedienceFreeDecrees);
	if (f.aspect == Aspect::Ruler) oT += T.rulerObey;
	else if (f.aspect == Aspect::Threat) oT += T.threatObey;
	if (survivors) oT += fin(f.survivorFear);
	if (jb <= -D.judgedLine) oT += D.deedObey;
	if (belief == DeathBelief::FurtherShore) oT += D.shoreObey * dk;
	oT = clamp(fin(oT, 0.5), T.obedienceMin, T.obedienceMax);
	// dependence
	const Decree* rt = decreeOf(w, c, DecreeArea::Worship);
	double dT = T.dependBase + T.dependAnswered * sat(get(f.data, "m_ans") / T.dependAnsweredRef) + (g.blessed ? T.dependBlessing : 0)
		- T.dependAgency * ag - T.dependSelf * self + ap.dependence + 0.1 * comp;
	if (f.aspect == Aspect::Parent) dT += T.dependParent;
	if (rt && rt->stance == int8_t(Rite::Prayer)) dT += 0.1 * sat(fin(rt->kept)) * (rt->custom ? D.customStrength : 1.0);
	if (belief == DeathBelief::Return) dT += D.returnDepend * dk;
	dT = clamp(fin(dT, 0.3), 0.0, 1.0);
	f.agency = sat(ag + (aT - ag) * driftK(dt, T.agencyDrift));
	f.obedience = sat(fin(f.obedience, 0.5) + (oT - fin(f.obedience, 0.5)) * driftK(dt, T.obedienceDrift));
	f.dependence = sat(dep + (dT - dep) * driftK(dt, T.dependenceDrift));
}

} // namespace

namespace mythos {

// ---------------------------------------------------------------- lifecycle

void attach(World& w) {
	MythosScratch& S = w.scratch<MythosScratch>();
	for (EventBus::Token t : S.tokens) w.bus.off(t);
	S.tokens.clear();
	if (!on(w)) return;
	World* pw = &w;
	S.tokens.push_back(w.bus.on<EvMiracleWitnessed>([pw](const EvMiracleWitnessed& e) {
		World& W = *pw;
		if (e.kind.empty() || e.kind == "nuke" || !(std::fabs(e.good) > 0) || !std::isfinite(e.good)) return;
		for (int sid : e.ids) {
			if (!liveSettlement(W, sid)) continue;
			MythAct a; a.kind = e.kind; a.cell = e.cell; a.settlement = sid; a.culture = W.settlements[size_t(sid)].culture; a.good = e.good;
			record(W, std::move(a));
		}
	}));
	S.tokens.push_back(w.bus.on<EvPrayerAnswered>([pw](const EvPrayerAnswered& e) {
		World& W = *pw;
		if (!liveSettlement(W, e.id)) return;
		MythAct a; a.kind = "prayer:answered"; a.settlement = e.id; a.cell = W.settlements[size_t(e.id)].cell;
		a.culture = W.settlements[size_t(e.id)].culture; a.good = 1;
		record(W, std::move(a));
	}));
	S.tokens.push_back(w.bus.on<EvBreach>([pw](const EvBreach& e) {
		MythAct a; a.kind = "breach"; a.settlement = e.settlement; a.culture = e.culture; a.good = -fin(e.severity);
		record(*pw, std::move(a));
	}));
	S.tokens.push_back(w.bus.on<EvDecree>([pw](const EvDecree& e) {
		MythAct a; a.kind = "decree:" + e.area + ":" + e.clarity + ":" + e.stance; a.culture = e.culture;
		record(*pw, std::move(a));
	}));
	S.tokens.push_back(w.bus.on<EvWord>([pw](const EvWord& e) {
		MythAct a; a.kind = "word:" + e.clarity + ":" + e.outcome + ":" + e.content; a.culture = e.culture;
		record(*pw, std::move(a));
	}));
	S.tokens.push_back(w.bus.on<EvJudgement>([pw](const EvJudgement& e) {
		MythAct a; a.kind = "judgement:" + e.by + ":" + e.sentence + ":" + std::to_string(e.card); a.culture = e.culture;
		record(*pw, std::move(a));
	}));
	S.tokens.push_back(w.bus.on<EvPact>([pw](const EvPact& e) {
		MythAct a; a.kind = "pact:" + e.what; a.culture = e.culture;
		record(*pw, std::move(a));
	}));
	S.tokens.push_back(w.bus.on<EvCard>([pw](const EvCard& e) {
		if ((e.stage != "answered" && e.stage != "lapsed") || e.culture < 0) return;
		MythAct a; a.kind = "card:" + e.kind + ":" + e.stage + ":" + std::to_string(e.id) + ":" + std::to_string(e.choice);
		a.culture = e.culture;
		record(*pw, std::move(a));
	}));
}

void rebuild(World& w) {
	if (!on(w)) return;
	ensureRecords(w);
}

void yearly(World& w) {
	const MythosTune& T = tuneOf(w);
	if (T.phase < 1) return;
	MythosState& m = w.mythos;
	ensureRecords(w);
	// the submodules (1.5), only those whose phase is on
	doctrine::yearly(w);
	characters::yearly(w);
	if (T.phase >= 2) { faiths::yearly(w); omens::yearly(w); legends::yearly(w); }
	if (T.phase >= 3) { keepers::yearly(w); renewal::yearly(w); }
	ensureRecords(w);                        // a submodule may have made a culture (a Way)

	const auto ly = m.num.find("mythos_lastYear");
	const double dt = ly == m.num.end() ? 1.0 : clamp(w.year - fin(ly->second, w.year), 0.0, 50.0);
	m.num["mythos_lastYear"] = w.year;
	if (m.num.find("mythos_forest0") == m.num.end()) m.num["mythos_forest0"] = fin(life::cover(w).forest);

	// the waking (2.3: ShapePeople and Presence, not budgeted), once the first people live
	int firstRoot = -1;
	for (const Culture& cu : w.cultures) if (!cu.dead && cu.parent < 0) { firstRoot = cu.id; break; }
	if (firstRoot >= 0) {
		if (get(m.num, "mythos_waking") <= 0) {
			cards::Proposal p; p.kind = CardKind::ShapePeople; p.culture = firstRoot;
			if (cards::open(w, p) >= 0) m.num["mythos_waking"] = 1;
		}
		if (get(m.num, "mythos_presenceCard") <= 0) {
			cards::Proposal p; p.kind = CardKind::Presence;
			if (cards::open(w, p) >= 0) m.num["mythos_presenceCard"] = 1;
		}
	}
	const std::vector<CultureAgg> agg = aggregate(w);
	consumeActs(w, agg);

	const double fImage = fadeK(dt, T.imageHalfLife), fVis = fadeK(dt, T.visibleHalfLife), fCl = fadeK(dt, T.clarityHalfLife);
	const DoctrineTune& D = w.tune<DoctrineTune>();
	const double fJudge = fadeK(dt, D.judgedHalfLife), fMercy = fadeK(dt, D.mercyHalfLife);
	const double fAns = std::exp(-dt / kAnsweredLife);
	for (size_t c = 0; c < m.faith.size(); c++) {
		Faith& f = m.faith[c];
		const bool alive = liveCulture(w, int(c));
		if (alive) {
			// each 100 years without a deed: Mystery +0.2 (3.3)
			const auto ld = f.data.find("m_lastDeed");
			const double since = ld == f.data.end() ? 1e9 : w.year - ld->second;
			if (since >= std::max(1.0, fin(T.silenceYears, 1))) addImage(f, Aspect::Mystery, 0.002 * dt);
		}
		for (double& x : f.image) x = std::max(0.0, fin(x) * fImage);
		f.visible = std::max(0.0, fin(f.visible) * fVis);
		f.data["m_clS"] = get(f.data, "m_clS") * fCl;
		f.data["m_clW"] = get(f.data, "m_clW") * fCl;
		f.clarity = sat((get(f.data, "m_clS") + 0.5) / (get(f.data, "m_clW") + 1));
		f.judgedBy = clamp(fin(f.judgedBy) * fJudge, -1.0, 1.0);
		f.mercy = std::max(0.0, fin(f.mercy) * fMercy);
		f.sternness = std::max(0.0, fin(f.sternness) * fMercy);
		f.data["m_ans"] = get(f.data, "m_ans") * fAns;
		if (f.survivorUntil >= 0 && !(f.survivorUntil > w.year)) { f.survivorLove = 0; f.survivorFear = 0; f.survivorUntil = -1; }
		if (!alive) continue;
		const CultureAgg& g = agg[c];
		f.data["m_peak"] = std::max(get(f.data, "m_peak"), g.pop);
		if (g.fpLoss > 0) f.data["m_fpDead"] = get(f.data, "m_fpDead") + g.fpLoss;
		choosesAspect(w, int(c), f);
		will(w, int(c), f, g, dt);
	}
	// stale breach marks
	for (auto it = m.num.lower_bound("mythos_breach"); it != m.num.end() && it->first.rfind("mythos_breach", 0) == 0;) {
		if (w.year - it->second > D.breachYears) it = m.num.erase(it);
		else ++it;
	}
	for (size_t c = 0; c < m.factors.size(); c++) m.factors[c] = assemble(w, int(c));
}

// ---------------------------------------------------------------- the legacy (4)

namespace {

// The five scores of one culture (dead cultures count for "everyone"); silence = the ending is a silence.
std::array<double, 6> scoresOf(const World& w, int c, bool silence) {
	std::array<double, 6> s{};
	const Faith* f = faith(w, c);
	if (!f || c < 0 || size_t(c) >= w.cultures.size()) return s;
	const Culture& cu = w.cultures[size_t(c)];
	const House* h = faiths::house(w, c);
	double imgSum = 0;
	for (double x : f->image) imgSum += std::max(0.0, fin(x));
	auto imgShare = [&](Aspect a) { return imgSum > 0 ? std::max(0.0, fin(f->image[size_t(a) - 1])) / imgSum : 0.0; };
	int decrees = 0;
	double burden = 0;
	godDecrees(w, c, decrees, burden);
	// obedient empire
	double ob = 0.45 * sat(fin(f->obedience)) + 0.25 * (h ? sat(fin(h->authority)) : 0) + 0.2 * imgShare(Aspect::Ruler) + 0.1 * sat(decrees / 4.0);
	if (decrees < 2) ob *= 0.5;
	// compassionate civilisation
	double care = 0;
	if (const Decree* d = decreeOf(w, c, DecreeArea::Care)) care += d->stance == int8_t(CareStance::Shelter) ? 1 : d->stance == int8_t(CareStance::Kin) ? 0.5 : 0;
	if (const Decree* d = decreeOf(w, c, DecreeArea::Worship); d && d->stance == int8_t(Rite::Kindness)) care += 0.5;
	for (const Figure& g : w.mythos.figures)
		if (g.culture == c && g.kind == uint8_t(FigureKind::Healer)) { care += kHealerCare; break; }
	double love = 0, pop = 0;
	for (const Settlement& st : w.settlements)
		if (!st.dead && st.culture == c) { const double p = std::max(0.0, fin(st.pop)); love += clamp(fin(st.love), -1.0, 1.0) * p; pop += p; }
	love = pop > 0 ? love / pop : 0;
	const double peak = get(f->data, "m_peak");
	const double dead = peak > 0 ? get(f->data, "m_fpDead") / peak : 0;
	const double co = 0.35 * sat(fin(cu.traits.compassion, 0.5)) + 0.25 * sat(care) + 0.25 * sat(1 - 4 * dead) + 0.15 * sat(love + 0.5);
	// terrified world
	const society::Memory mem = society::memory(w, c);
	const double te = 0.6 * sat(-fin(mem.verdict) / 0.5) + 0.4 * imgShare(Aspect::Threat);
	// thriving wilderness
	const life::Cover cv = life::cover(w);
	const double f0 = get(w.mythos.num, "mythos_forest0");
	const double forestRatio = f0 > 0 ? sat(fin(cv.forest) / f0) : (fin(cv.forest) > 0 ? 1.0 : 0.0);
	double land = 0.3;
	if (const Decree* d = decreeOf(w, c, DecreeArea::Land))
		land = d->stance == int8_t(LandStance::Equal) ? 1 : d->stance == int8_t(LandStance::Stewardship) ? 0.6 : 0;
	double wi = 0.45 * forestRatio + 0.3 * land + 0.25 * sat(fin(cv.veg));
	if (silence) wi *= 1.3;
	// a humanity that no longer needs you
	double un = 0.5 * sat(fin(f->selfReliance)) + 0.3 * sat(fin(f->agency)) + 0.2 * (1 - sat(fin(f->dependence)));
	if (!mem.remembered) un *= 0.5;
	s[size_t(Legacy::ObedientEmpire)] = fin(ob);
	s[size_t(Legacy::CompassionateCivilisation)] = fin(co);
	s[size_t(Legacy::TerrifiedWorld)] = fin(te);
	s[size_t(Legacy::ThrivingWilderness)] = fin(wi);
	s[size_t(Legacy::Unneeded)] = fin(un);
	return s;
}

} // namespace

LegacyScores legacyScores(const World& w) {
	LegacyScores out;
	if (!on(w) || w.mythos.faith.empty()) return out;
	const bool silence = w.ending && w.ending->type == EndingType::Silence;
	if (w.ending && w.ending->type == EndingType::Ark && w.ending->culture >= 0 && faith(w, w.ending->culture)) {
		out.score = scoresOf(w, w.ending->culture, false);
	} else {
		// everyone: weighted by people (the living), else by peak people, else equally
		double wsum = 0;
		std::array<double, 6> acc{};
		for (int pass = 0; pass < 3 && !(wsum > 0); pass++) {
			for (size_t c = 0; c < w.mythos.faith.size() && c < w.cultures.size(); c++) {
				double wt = 0;
				if (pass == 0) {
					if (w.cultures[c].dead) continue;
					for (const Settlement& st : w.settlements) if (!st.dead && st.culture == int(c)) wt += std::max(0.0, fin(st.pop));
				} else if (pass == 1) {
					wt = get(w.mythos.faith[c].data, "m_peak");
				} else {
					wt = 1;
				}
				if (!(wt > 0)) continue;
				const std::array<double, 6> s = scoresOf(w, int(c), silence);
				for (size_t k = 0; k < 6; k++) acc[k] += wt * s[k];
				wsum += wt;
			}
		}
		if (wsum > 0) for (size_t k = 0; k < 6; k++) out.score[k] = acc[k] / wsum;
	}
	// the highest wins; ties: Unneeded, Compassionate, Wilderness, Obedient, Terrified
	static const Legacy order[] = { Legacy::Unneeded, Legacy::CompassionateCivilisation, Legacy::ThrivingWilderness,
		Legacy::ObedientEmpire, Legacy::TerrifiedWorld };
	double best = -1;
	for (Legacy l : order) {
		const double v = out.score[size_t(l)];
		if (v > best + 1e-12) { best = v; out.best = l; }
	}
	return out;
}

Legacy legacy(const World& w) {
	if (!on(w) || !w.ending) return Legacy::None;
	return legacyScores(w).best;
}

// ---------------------------------------------------------------- reads

// An honest read of the stored record (FOUNDATION-CPP 19), whatever the phase: at phase 0 Mythos never fills it.
Factors factors(const World& w, int cultureId) {
	if (cultureId < 0 || size_t(cultureId) >= w.mythos.factors.size()) return Factors{};
	return w.mythos.factors[size_t(cultureId)];
}

TownFactors townFactors(const World& w, int sid) {
	TownFactors t;
	if (tuneOf(w).phase < 2 || sid < 0) return t;
	const MythosTune& T = tuneOf(w);
	for (const TownMyth& m : w.mythos.towns) {
		if (m.settlement != sid) continue;
		if (m.blessing == BlessingKind::None || !(m.blessedUntil > w.year)) break;
		if (m.blessing == BlessingKind::Prosperity) { t.K = T.prosperityK; t.growthR = T.prosperityR; }
		else if (m.blessing == BlessingKind::Endurance) t.harmTaken = T.enduranceHarm;
		else if (m.blessing == BlessingKind::Healer) t.plagueDeath = T.healerDeath;
		break;
	}
	return t;
}

WitnessScale witnessScale(const World& w, int cell, std::string_view kind) {
	WitnessScale s;
	if (!on(w) || kind.empty() || kind == "nuke") return s;
	const MythosTune& T = tuneOf(w);
	if (w.mythos.presence == Presence::Messengers) {
		s.faith = messengerNear(w, cell, T.msgNearAng) != -1 ? T.msgFaithNear : T.msgFaithFar;
	} else if (w.mythos.presence == Presence::Unseen) {
		s.faith = T.unseenFaith;
		s.memory = T.unseenMemory;
		s.favour = 0;
		s.jaded = 0;
	}
	return s;
}

Aspect aspect(const World& w, int cultureId) {
	if (cultureId < 0 || size_t(cultureId) >= w.mythos.faith.size()) return Aspect::Unknown;
	return w.mythos.faith[size_t(cultureId)].aspect;
}

int warGoal(const World& w, int cultureId) {
	(void)w; (void)cultureId;
	return -1;                               // v2 (3.16 Conquest): M1b
}

double neglectScale(const World& w, int cultureId) {
	(void)w; (void)cultureId;
	return 1.0;                              // v3 (3.28, 3.29): M1c
}

ArchetypeParams archetypeParams(const World& w) {
	const size_t a = size_t(w.settings.archetype);
	const MythosTune& T = w.tune<MythosTune>();
	return a < T.archetypes.size() ? T.archetypes[a] : ArchetypeParams{};
}

Rite rite(const World& w, int cultureId) {
	for (const Decree& d : w.mythos.decrees)
		if (d.culture == cultureId && d.area == DecreeArea::Worship && d.stance >= 0 && d.stance < stanceCount(DecreeArea::Worship))
			return Rite(d.stance);
	return Rite::None;
}

const Faith* faith(const World& w, int cultureId) {
	if (cultureId < 0 || size_t(cultureId) >= w.mythos.faith.size()) return nullptr;
	return &w.mythos.faith[size_t(cultureId)];
}

// ---------------------------------------------------------------- the god's choices

bool setPresence(World& w, Presence p) {
	if (!on(w) || p == Presence::Walk || uint8_t(p) >= uint8_t(Presence::Walk)) return false;
	MythosState& m = w.mythos;
	if (p == m.presence) return false;
	const auto it = m.num.find("mythos_presenceAge");
	if (it != m.num.end() && int(it->second) == w.age) return false;   // once per world age
	if (!powers::pay(w, "presence", tuneOf(w).presenceCost)) return false;
	m.num["mythos_presenceAge"] = w.age;
	setStance(w, p);
	ensureRecords(w);
	for (const Culture& cu : w.cultures) {
		if (cu.dead) continue;
		touch(w, cu.id);
		// the people notice (3.2, 3.19): "Why have you hidden your face?"
		const Faith* f = faith(w, cu.id);
		if (p == Presence::Unseen && f && f->agency >= tuneOf(w).disputeAgency) {
			cards::Proposal q; q.kind = CardKind::Dispute; q.culture = cu.id; q.severity = 0.5;
			q.ref[0] = int(DisputeKind::HiddenFace); q.data["dispute"] = int(DisputeKind::HiddenFace);
			cards::propose(w, q);
		}
	}
	return true;
}

bool withdraw(World& w, bool announced) {
	(void)w; (void)announced;
	return false;                            // v3 (3.29): M1c
}

bool bless(World& w, int sid, BlessingKind kind) {
	(void)w; (void)sid; (void)kind;
	return false;                            // v2 (3.16): M1b
}

// ---------------------------------------------------------------- card handlers

bool answer(World& w, int cardId, int choice) {
	if (!on(w)) return false;
	const Card* c = cards::find(w, cardId);
	if (!c || c->state != uint8_t(CardState::Open)) return false;
	const Card card = *c;                    // the handler may make records that move the card list
	switch (card.kind) {
	case CardKind::ShapePeople:
		return answerShape(w, card, choice);
	case CardKind::Presence: {
		if (choice < 0 || choice > 2) return false;
		const Presence p = Presence(choice);
		setStance(w, p);
		ensureRecords(w);
		if (p != Presence::Wonders) for (const Culture& cu : w.cultures) if (!cu.dead) touch(w, cu.id);
		return true;
	}
	default:
		return false;                        // Blessing, Dispute (v2: M1b), ComingOfAge, SilenceReport (v3: M1c)
	}
}

void lapse(World& w, int cardId) {
	if (!on(w)) return;
	const Card* c = cards::find(w, cardId);
	if (!c) return;
	// ShapePeople lapses into the people's own rolled nature (nothing changes); Presence into Wonders (today's
	// stance, already the default); the v2 / v3 kinds are M1b's / M1c's.
	(void)c;
}

std::vector<CardOption> options(const World& w, int cardId) {
	std::vector<CardOption> out;
	const Card* c = cards::find(w, cardId);
	if (!c) return out;
	int n = 0;
	for (const CardKindInfo& k : kCardKinds) if (k.kind == c->kind) n = k.answers;
	const MythosTune& T = tuneOf(w);
	for (int i = 0; i < n; i++) {
		CardOption o;
		o.choice = i;
		o.cost = 0;
		if (!on(w)) { o.ok = false; o.why = OptionWhy::Locked; }
		else if (c->kind == CardKind::ShapePeople) {
			std::array<int, 5> lv{};
			o.ok = i == 243 || decodeShape(T, i, lv);
			o.why = o.ok ? OptionWhy::Ok : OptionWhy::Unavailable;
		} else if (c->kind == CardKind::Presence) {
			o.ok = true; o.why = OptionWhy::Ok;
		} else {
			o.ok = false; o.why = OptionWhy::Locked;   // v2 / v3 kinds: not built in M1
		}
		out.push_back(o);
	}
	return out;
}

} // namespace mythos
} // namespace dm
