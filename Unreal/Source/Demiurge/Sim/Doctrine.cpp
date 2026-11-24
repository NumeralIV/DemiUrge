// DEMIURGE — Sim/Doctrine.cpp : decrees, customs, breaches (SPEC-MYTHOS 3.6), conflict cases (3.7), the Word and
// its interpretation (3.9), judgement (3.11), sentences (3.12) and the rite (3.13). Track M3 "doctrine", 2026-11-20.
// Pacts (3.14), the Land decree (3.22), revelations (3.25) and beliefs about death (3.27) are the later halves
// (M3b / M3c): their entry points here stay neutral (refuse, -1, false) and their card kinds are not answered.
//
// State written (SPEC-MYTHOS 1.7): mythos.decrees, mythos.words, Decree::data / Utterance::data (private), and
// mythos.num keys prefixed "doctrine_" (per-culture flags and the breach counter). Never a Faith field: every
// change of a Mythos-owned value (obedience, agency, impressions, judgedBy, mercy / sternness, survivor memory,
// clarity) is the Mythos track's to apply from the events this module emits and the card records it proposes:
//
//   event / record                         what Mythos reads (numbers in DoctrineTune / MythosTune)
//   decree { clarity != "custom" }         a word given as a decree: Ruler +0.3 (Exact +0.5); clarity "custom" =
//                                          the people's own custom (3.6): no impression, never breached; its
//                                          kept is 1 and Mythos folds it in at DoctrineTune::customStrength
//   word { clarity, outcome }              clarity value (3.4) and the 3.9 impression; outcome "catastrophe":
//                                          Threat + catastropheThreat
//   breach                                 (records only; Chronicle tells it)
//   judgement { card, by, sentence }       judgedBy +-judgedStep; mercy (forgive) / sternness (warn, remove,
//                                          disaster); survivor memory survivorYears with the 3.12 targets; the
//                                          sentence's obedience step (warnObey, removeObey; forgiveRepeat when
//                                          the card's data["repeat"] is 1) and impressions. A disaster is
//                                          JUST when the thing judged was wrong: by deed -> data["deedWrong"],
//                                          by heart -> data["heartWrong"] (both on the card from the proposal;
//                                          ref[3] also holds the HeartKind of doctrine's own cards).
//   card { kind decree-conflict, ... }     the case is ref[0] (doctrine::Case order below); answered: the
//                                          impression / will line of the case table (3.7); lapsed: lapseObey.
//
// Randomness: forks only, w.rng.fork("myth:doctrine:<what>:<id>:<year>") (1.10). Customs, the people's choice of
// a hidden heart and every other "forked" choice that must be a pure read use dm::hashf (no rng state at all).
// Gates: MythosTune::phase < 1 = every mutator refuses or does nothing. MythosTune::emergent gates the people's
// own customs (formed at the culture's year 200 and its first shrine, and when a FirstDecree card lapses), so a
// world whose god never speaks keeps no decree and no breach (guardrail 5.1 rule 2).
#include "Doctrine.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "Cards.h"
#include "Characters.h"
#include "Civ.h"
#include "Faiths.h"
#include "Mythos.h"
#include "Powers.h"
#include "Society.h"
#include "Units.h"

namespace dm::doctrine {
namespace {

// ------------------------------------------------------------------ small helpers

// The conflict cases of 3.7, in table order (DecreeConflict cards carry the case in ref[0]).
enum class Case : int { RaidedWeak, HiddenStranger, TempleGranary, EnemyGate, WoodChildren, AltarBread, CleverTrue, StrongWeak, Count };

int phaseOf(const World& w) { return w.tune<MythosTune>().phase; }
// (B1) the people's own customs are the "customs" emergent family (MythosTune::FamCustoms)
bool emergentOn(const World& w) { return w.tune<MythosTune>().emergentOn(MythosTune::FamCustoms); }

std::string nkey(const char* what, int id) { return std::string("doctrine_") + what + ":" + std::to_string(id); }
double numGet(const World& w, const std::string& k, double def = 0) {
	const auto it = w.mythos.num.find(k);
	return it == w.mythos.num.end() ? def : it->second;
}
void numSet(World& w, const std::string& k, double v) { w.mythos.num[k] = isFinite(v) ? v : 0.0; }
double bagGet(const NumBag& b, const char* k, double def = 0) {
	const auto it = b.find(k);
	return it == b.end() || !isFinite(it->second) ? def : it->second;
}
void bagSet(NumBag& b, const char* k, double v) { b[k] = isFinite(v) ? v : 0.0; }

std::string forkTag(const char* what, int64_t id, double year) {
	return std::string("myth:doctrine:") + what + ":" + std::to_string(id) + ":" + std::to_string(int64_t(std::floor(year)));
}

const Culture* liveCulture(const World& w, int c) {
	if (c < 0 || size_t(c) >= w.cultures.size()) return nullptr;
	const Culture& cu = w.cultures[size_t(c)];
	return cu.dead ? nullptr : &cu;
}
const Settlement* liveSett(const World& w, int sid) {
	if (sid < 0 || size_t(sid) >= w.settlements.size()) return nullptr;
	const Settlement& s = w.settlements[size_t(sid)];
	return s.dead ? nullptr : &s;
}
const Faith* faithOf(const World& w, int c) {
	if (c < 0 || size_t(c) >= w.mythos.faith.size()) return nullptr;
	return &w.mythos.faith[size_t(c)];
}
double agencyOf(const World& w, int c) { const Faith* f = faithOf(w, c); return f ? sat(f->agency) : Faith{}.agency; }
double obedienceOf(const World& w, int c) { const Faith* f = faithOf(w, c); return f ? sat(f->obedience) : Faith{}.obedience; }
double judgedByOf(const World& w, int c) { const Faith* f = faithOf(w, c); return f ? clamp(f->judgedBy, -1, 1) : 0.0; }
double authorityOf(const World& w, int c) { const House* h = faiths::house(w, c); return h ? sat(h->authority) : 0.0; }
double corruptionOf(const World& w, int c) { const House* h = faiths::house(w, c); return h ? sat(h->corruption) : 0.0; }
double wealthOf(const World& w, int c) { const House* h = faiths::house(w, c); return h ? sat(h->wealth) : 0.0; }

double cultPop(const World& w, int c) {
	double p = 0;
	for (const Settlement& s : w.settlements) if (!s.dead && s.culture == c && isFinite(s.pop)) p += std::max(0.0, s.pop);
	return p;
}
double worldPop(const World& w) {
	double p = 0;
	for (const Settlement& s : w.settlements) if (!s.dead && isFinite(s.pop)) p += std::max(0.0, s.pop);
	return p;
}
// Share of the culture's people living in settlement sid (0..1).
double shareOf(const World& w, int c, int sid) {
	const Settlement* s = liveSett(w, sid);
	const double cp = cultPop(w, c);
	if (!s || !(cp > 0)) return 0.0;
	return sat(std::max(0.0, s->pop) / cp);
}
// The culture's most populous live settlement (ties: lowest id), or -1.
int largestTown(const World& w, int c) {
	int best = -1; double bp = -1;
	for (const Settlement& s : w.settlements)
		if (!s.dead && s.culture == c && s.pop > bp) { best = s.id; bp = s.pop; }
	return best;
}
int capitalOf(const World& w, int c) {
	const Culture* cu = liveCulture(w, c);
	if (cu && liveSett(w, cu->capital) && liveSett(w, cu->capital)->culture == c) return cu->capital;
	return largestTown(w, c);
}
bool hasShrine(const World& w, int c) {
	for (const Settlement& s : w.settlements)
		if (!s.dead && s.culture == c && (s.has(Building::SHRINE) || s.has(Building::TEMPLE))) return true;
	return false;
}
// The live settlement of culture c with the highest value of `key` (ties: lowest id); -1 when every value <= min.
template <class F> int worstTown(const World& w, int c, F key, double floor0 = 0) {
	int best = -1; double bv = floor0;
	for (const Settlement& s : w.settlements)
		if (!s.dead && s.culture == c) { const double v = key(s); if (isFinite(v) && v > bv) { bv = v; best = s.id; } }
	return best;
}

// ---- relations (read-only through Diplomacy's peek)
struct Rel { int other = -1; const Relation* r = nullptr; };
std::vector<Rel> relationsOf(const World& w, int c) {
	std::vector<Rel> out;
	for (const auto& [k, r] : w.vars.rel) {
		if (k.first == c && k.second != c) out.push_back({ k.second, &r });
		else if (k.second == c && k.first != c) out.push_back({ k.first, &r });
	}
	std::sort(out.begin(), out.end(), [](const Rel& a, const Rel& b) { return a.other < b.other; });
	return out;
}
bool alive(const World& w, int c) { return liveCulture(w, c) != nullptr; }
// A culture at war with c that started it (c was attacked), or -1.
int attackerOf(const World& w, int c) {
	for (const Rel& x : relationsOf(w, c))
		if (x.r->war && x.r->aggressor == x.other && alive(w, x.other)) return x.other;
	return -1;
}
int enemyOf(const World& w, int c) {
	for (const Rel& x : relationsOf(w, c)) if (x.r->war && alive(w, x.other)) return x.other;
	return -1;
}
int contactEnemyOf(const World& w, int c) {
	for (const Rel& x : relationsOf(w, c)) if (x.r->war && x.r->contact && alive(w, x.other)) return x.other;
	return -1;
}
int brinkOf(const World& w, int c) {
	for (const Rel& x : relationsOf(w, c))
		if (!x.r->war && x.r->contact && x.r->v <= -0.4 && alive(w, x.other)) return x.other;
	return -1;
}

// ---- decrees
Decree* findDecree(World& w, int c, DecreeArea a) {
	for (Decree& d : w.mythos.decrees) if (d.culture == c && d.area == a) return &d;
	return nullptr;
}
int stanceOf(const World& w, int c, DecreeArea a) {
	const Decree* d = decree(w, c, a);
	return d ? d->stance : -1;
}
bool holds(const World& w, int c, DecreeArea a, int stance, double minKept) {
	const Decree* d = decree(w, c, a);
	return d && d->stance == stance && d->kept >= minKept;
}
double effStrict(const World& w, const Decree& d) {
	double s = strictness(w, d.area, d.stance) + bagGet(d.data, "strictAdj");
	if (bagGet(d.data, "literal") > 0) s += 0.2;
	return sat(s);
}
// Insert or replace keeping the vector ascending by (culture, area).
Decree& placeDecree(World& w, int c, DecreeArea a) {
	if (Decree* d = findDecree(w, c, a)) return *d;
	Decree nd; nd.culture = c; nd.area = a;
	auto it = std::find_if(w.mythos.decrees.begin(), w.mythos.decrees.end(), [&](const Decree& x) {
		return x.culture > c || (x.culture == c && uint8_t(x.area) > uint8_t(a));
	});
	return *w.mythos.decrees.insert(it, nd);
}
bool eraseDecree(World& w, int c, DecreeArea a) {
	auto& v = w.mythos.decrees;
	const auto it = std::find_if(v.begin(), v.end(), [&](const Decree& x) { return x.culture == c && x.area == a; });
	if (it == v.end()) return false;
	v.erase(it);
	return true;
}

// The messenger figure a word goes through: o.figure when it is a living messenger of the culture, else the
// culture's own messenger; -1 none.
int messengerFor(const World& w, int c, int figure) {
	if (const Figure* f = characters::figure(w, figure))
		if (f->culture == c && f->kind == uint8_t(FigureKind::Messenger) && f->fate == uint8_t(FigureFate::Living)) return f->id;
	return characters::messenger(w, c);
}

// Reach (3.8): the messenger archetype's reach weighted by the culture's people per town class.
double reachMean(const World& w, int c, int figureId) {
	const Figure* f = characters::figure(w, figureId);
	if (!f || f->archetype == 0 || f->archetype >= 6) return 1.0;
	const auto& R = w.tune<CharactersTune>().archReach[f->archetype];
	double sum = 0, wsum = 0;
	for (const Settlement& s : w.settlements) {
		if (s.dead || s.culture != c || !(s.pop > 0)) continue;
		const int cls = (s.famine > 0 || s.plague > 0) ? 3 : s.capital ? 2 : s.tier >= 2 ? 1 : 0;
		sum += s.pop * R[size_t(cls)];
		wsum += s.pop;
	}
	return wsum > 0 ? sat(sum / wsum) : 1.0;
}

// How well a decree is kept now, before the yearly drift (3.6, 3.8).
double keptTarget(const World& w, const Decree& d) {
	const DoctrineTune& T = w.tune<DoctrineTune>();
	if (d.custom) return 1.0;                             // theirs, kept in full; it WORKS at customStrength (Mythos)
	double t = obedienceOf(w, d.culture) * sat(bagGet(d.data, "fidelity", 1.0));
	if (d.by >= 0) {
		t *= 0.5 + 0.5 * reachMean(w, d.culture, d.by);
		const Figure* f = characters::figure(w, d.by);
		const double ramp = w.tune<CharactersTune>().decreeShepherdYears;
		if (f && f->archetype == uint8_t(MessengerArchetype::Shepherd) && ramp > 0) t *= sat((w.year - d.since) / ramp);
	}
	if (bagGet(d.data, "personal") > 0) {
		const double share = shareOf(w, d.culture, int(bagGet(d.data, "dreamer", -1)));
		t *= share + 0.3 * (1 - share);
	}
	if (w.year < bagGet(d.data, "breachUntil", -1)) t += T.breachKept;
	return sat(t);
}

// ------------------------------------------------------------------ customs (3.6, 3.13)

// Scores of an area's stances from the culture's traits (and the archetype for the rite). Pure.
std::array<double, 5> stanceScores(const World& w, int c, DecreeArea a) {
	std::array<double, 5> s{ -1, -1, -1, -1, -1 };
	const Culture* cu = liveCulture(w, c);
	if (!cu) return s;
	const Traits& t = cu->traits;
	switch (a) {
	case DecreeArea::Violence: s = { 0.6 * t.compassion + 0.4 * (1 - t.aggression), 0.25 + 0.5 * t.courage, 0.6 * t.aggression + 0.4 * t.courage, -1, -1 }; break;
	case DecreeArea::Wealth: s = { t.compassion, t.piety, t.ambition, -1, -1 }; break;
	case DecreeArea::Worship: {
		const ArchetypeParams P = mythos::archetypeParams(w);
		const bool generous = w.settings.archetype == Archetype::Generous;
		s = { 0.9 * t.piety, 0.55 * t.piety + 0.4 * t.ambition, t.compassion, t.curiosity + (P.artCustom ? 1.0 : 0.0), 0.3 + (generous ? 1.0 : 0.0) };
		break;
	}
	case DecreeArea::Honesty: s = { 0.5 * t.piety + 0.5 * (1 - t.ambition), 0.5, 0.5 * t.curiosity + 0.5 * t.ambition, -1, -1 }; break;
	case DecreeArea::Hospitality: s = { 0.5 * t.compassion + 0.5 * t.seafaring, 0.5, 0.5 * t.aggression + 0.5 * (1 - t.compassion), -1, -1 }; break;
	case DecreeArea::Care: s = { t.compassion, 0.5, 0.5 * t.courage + 0.5 * t.aggression, -1, -1 }; break;
	case DecreeArea::Land: s = { t.ambition, 0.5, 0.5 * (1 - t.ambition) + 0.5 * t.piety, -1, -1 }; break;
	default: break;
	}
	// the people's own small leanings: a deterministic per-culture jitter breaks ties (a pure "fork")
	for (int i = 0; i < stanceCount(a); i++) s[size_t(i)] += 0.02 * hashf(uint32_t(c) * 131u + uint32_t(a) * 7u + uint32_t(i), 0x51u);
	return s;
}
// The area a culture would take its next custom in (the rite first), or Count.
DecreeArea customArea(const World& w, int c) {
	if (!decree(w, c, DecreeArea::Worship)) return DecreeArea::Worship;
	DecreeArea best = DecreeArea::Count; double bv = -1;
	for (int ai = 0; ai < int(DecreeArea::Count); ai++) {
		const DecreeArea a = DecreeArea(ai);
		if (a == DecreeArea::Worship || decree(w, c, a)) continue;
		if (a == DecreeArea::Land && phaseOf(w) < 2) continue;          // the Land decree is v2 (3.22)
		const auto s = stanceScores(w, c, a);
		const double v = *std::max_element(s.begin(), s.begin() + stanceCount(a));
		if (v > bv) { bv = v; best = a; }
	}
	return best;
}
void emitDecree(World& w, int c, DecreeArea a, int stance, int prev, const char* clarity) {
	w.bus.emit(EvDecree{ c, toStr(a), stance < 0 ? "repealed" : stanceName(a, stance), stanceName(a, prev), clarity });
}
// The people take a custom (3.6): half strength, never breached. False when there is no free area.
bool formCustom(World& w, int c) {
	if (!alive(w, c)) return false;
	const DecreeArea a = customArea(w, c);
	if (a == DecreeArea::Count) return false;
	const int s = customStance(w, c, a);
	if (s < 0) return false;
	Decree& d = placeDecree(w, c, a);
	d.stance = int8_t(s); d.clarity = Clarity::Sign; d.since = w.year; d.by = -1; d.custom = true; d.data.clear();
	d.kept = 1.0;                                         // Mythos folds a custom in at DoctrineTune::customStrength
	emitDecree(w, c, a, s, -1, "custom");
	return true;
}

// ------------------------------------------------------------------ hearts and circumstances (3.11)

bool heartWrong(HeartKind h) { return h == HeartKind::Greedy || h == HeartKind::Zealous; }
// The real heart behind a deed: from a figure's hidden heart, or the town's culture traits; a pure function of
// (who, key) so the same deed always shows the same heart.
HeartKind heartOf(const World& w, int c, int figureId, int64_t key, Circumstance circ, DecreeArea area) {
	std::array<double, 4> wt{};                       // Selfless, Fearful, Greedy, Zealous
	const bool hard = circ == Circumstance::Famine || circ == Circumstance::War || circ == Circumstance::Plague || circ == Circumstance::Fear;
	if (const Figure* f = characters::figure(w, figureId)) {
		const double integ = sat(f->heart[0]), humil = sat(f->heart[1]), faith = sat(f->heart[2]);
		wt = { 0.1 + integ * humil, 0.1 + 0.5 * (1 - faith) * (hard ? 1.5 : 0.7), 0.1 + 0.8 * (1 - integ), 0.1 + faith * (1 - humil) };
	} else if (const Culture* cu = liveCulture(w, c)) {
		const Traits& t = cu->traits;
		wt = { 0.25 + 0.5 * t.compassion, (0.15 + 0.4 * (1 - t.courage)) * (hard ? 1.5 : 0.7),
			(0.1 + 0.5 * t.ambition) * (area == DecreeArea::Wealth ? 1.5 : 1.0),
			(0.1 + 0.4 * t.piety) * (area == DecreeArea::Worship || area == DecreeArea::Violence ? 1.3 : 1.0) };
	} else {
		return HeartKind::Unknown;
	}
	double tot = 0;
	for (double x : wt) tot += std::max(0.0, x);
	if (!(tot > 0)) return HeartKind::Selfless;
	double u = hashf(uint32_t(key), uint32_t(c) * 2654435761u + uint32_t(figureId + 1)) * tot;
	for (int i = 0; i < 4; i++) { u -= std::max(0.0, wt[size_t(i)]); if (u < 0) return HeartKind(i + 1); }
	return HeartKind::Zealous;
}

// ------------------------------------------------------------------ breaches (3.6)

struct Press { double p = 0; int sid = -1; Circumstance circ = Circumstance::None; };
Press pressureOf(const World& w, int c, const Decree& d, bool strangerCase) {
	const DoctrineTune& T = w.tune<DoctrineTune>();
	const Culture* cu = liveCulture(w, c);
	Press r;
	if (!cu || d.custom || d.stance < 0) return r;
	r.sid = largestTown(w, c);
	if (r.sid < 0) return r;
	const auto idle = [&] { r.p = T.pressureIdle; return r; };
	const auto famine = [&](double p) {
		const int t = worstTown(w, c, [](const Settlement& s) { return double(s.famine); });
		if (t < 0) return idle();
		r.p = p; r.sid = t; r.circ = Circumstance::Famine; return r;
	};
	const auto war = [&](int enemy, double p) {
		if (enemy < 0) return idle();
		r.p = p; r.circ = Circumstance::War; return r;
	};
	switch (d.area) {
	case DecreeArea::Violence:
		if (d.stance == int(ViolenceStance::Peace)) {
			if (attackerOf(w, c) >= 0) { r.p = 1.0; r.circ = Circumstance::War; return r; }
			if (brinkOf(w, c) >= 0) { r.p = 0.6; r.circ = Circumstance::Fear; return r; }
			return idle();
		}
		if (d.stance == int(ViolenceStance::Defence)) {
			if (cu->edict == Edict::Crusade) { r.p = 1.0; r.circ = Circumstance::Order; return r; }
			return idle();
		}
		return Press{};                                   // Valour: never
	case DecreeArea::Wealth:
		if (d.stance == int(WealthStance::Share)) {
			int markets = 0, mt = -1; double mp = -1;
			for (const Settlement& s : w.settlements)
				if (!s.dead && s.culture == c && s.has(Building::MARKET)) { markets++; if (s.pop > mp) { mp = s.pop; mt = s.id; } }
			if (cu->traits.ambition >= 0.6 && markets >= 2) { r.p = 0.8; r.sid = mt; return r; }
			return idle();
		}
		if (d.stance == int(WealthStance::Tithe)) return famine(0.8);
		return Press{};                                   // Gain: never
	case DecreeArea::Worship:
		switch (Rite(d.stance)) {
		case Rite::Temples: return famine(0.8);
		case Rite::Kindness: return war(enemyOf(w, c), 0.8);
		case Rite::Art: if (cu->traits.ambition >= 0.6) { r.p = 0.6; return r; } return idle();
		case Rite::World: return famine(0.8);
		default: return Press{};                          // Prayer: never
		}
	case DecreeArea::Honesty:
		if (d.stance == int(HonestyStance::Truth)) {
			if (strangerCase) { r.p = 1.0; r.circ = Circumstance::War; return r; }
			return war(enemyOf(w, c), 0.7);
		}
		if (d.stance == int(HonestyStance::Discretion)) return idle();
		return Press{};
	case DecreeArea::Hospitality:
		if (d.stance == int(HospitalityStance::Open)) return war(contactEnemyOf(w, c), 0.9);
		if (d.stance == int(HospitalityStance::Guarded)) return idle();
		return Press{};
	case DecreeArea::Care:
		if (d.stance == int(CareStance::Shelter)) {
			const int f3 = worstTown(w, c, [](const Settlement& s) { return double(s.famine); }, 2.5);
			if (f3 >= 0) { r.p = 0.8; r.sid = f3; r.circ = Circumstance::Famine; return r; }
			const int pl = worstTown(w, c, [](const Settlement& s) { return s.plague; });
			if (pl >= 0) { r.p = 0.8; r.sid = pl; r.circ = Circumstance::Plague; return r; }
			return idle();
		}
		if (d.stance == int(CareStance::Kin)) return idle();
		return Press{};
	default:
		return Press{};                                   // Land: v2 (3.22)
	}
}

double judgementFactor(const World& w, int c) {
	const DoctrineTune& T = w.tune<DoctrineTune>();
	const double j = judgedByOf(w, c);
	return j <= -T.judgedLine ? T.deedBreach : j >= T.judgedLine ? T.heartBreach : 1.0;
}

bool caseHolds(const World& w, int c, Case k, int& other, int& sid, double& stake);

void breaches(World& w, int c) {
	const DoctrineTune& T = w.tune<DoctrineTune>();
	int o = -1, s = -1; double st = 0;
	const bool stranger = caseHolds(w, c, Case::HiddenStranger, o, s, st);
	const double obey = obedienceOf(w, c);
	const double jf = judgementFactor(w, c);
	const bool deeds = judgedByOf(w, c) <= -T.judgedLine;
	for (size_t i = 0; i < w.mythos.decrees.size(); i++) {
		if (w.mythos.decrees[i].culture != c) continue;
		const Decree snap = w.mythos.decrees[i];
		if (snap.custom || snap.stance < 0) continue;
		const Press pr = pressureOf(w, c, snap, stranger);
		if (!(pr.p > 0) || pr.sid < 0) continue;
		const double P = T.breachBase * effStrict(w, snap) * pr.p * (1 - obey) * jf;
		Rng r = w.rng.fork(forkTag("breach", int64_t(c) * 16 + int64_t(snap.area), w.year));
		if (!r.chance(P)) continue;
		const int id = int(numGet(w, "doctrine_breaches")) + 1;
		numSet(w, "doctrine_breaches", id);
		const double sev = sat(shareOf(w, c, pr.sid) * pr.p * (deeds ? 1.2 : 1.0));
		Decree& d = w.mythos.decrees[i];
		const bool repeat = bagGet(d.data, "breaches") > 0;
		d.kept = sat(d.kept + T.breachKept);
		bagSet(d.data, "breachUntil", w.year + T.breachYears);
		bagSet(d.data, "breaches", bagGet(d.data, "breaches") + 1);
		if (sev >= T.breachCard) {
			bagSet(d.data, "pendId", id); bagSet(d.data, "pendSid", pr.sid); bagSet(d.data, "pendSev", sev);
			bagSet(d.data, "pendCirc", int(pr.circ)); bagSet(d.data, "pendYear", w.year); bagSet(d.data, "pendRepeat", repeat ? 1 : 0);
		}
		const DecreeArea area = d.area;
		w.bus.emit(EvBreach{ c, toStr(area), pr.sid, -1, sev });
	}
}

// A card of `kind` for culture c exists whose ref[0] is `ref0` (any state). Also returns the latest opening year.
bool cardSeen(const World& w, CardKind kind, int c, int ref0, double* lastOpened = nullptr) {
	bool any = false;
	for (const Card& k : w.mythos.cards) {
		if (k.kind != kind || k.culture != c || (ref0 != -9 && k.ref[0] != ref0)) continue;
		any = true;
		if (lastOpened) *lastOpened = std::max(*lastOpened, k.opened);
	}
	return any;
}

// The severe breaches still waiting for a Judgement card are re-proposed every year while the matter stands.
void proposeJudgements(World& w, int c) {
	const DoctrineTune& T = w.tune<DoctrineTune>();
	for (size_t i = 0; i < w.mythos.decrees.size(); i++) {
		Decree& d = w.mythos.decrees[i];
		if (d.culture != c || bagGet(d.data, "pendId") <= 0) continue;
		const int id = int(bagGet(d.data, "pendId"));
		const int sid = int(bagGet(d.data, "pendSid", -1));
		const Settlement* s = liveSett(w, sid);
		if (cardSeen(w, CardKind::Judgement, c, id) || !s || w.year - bagGet(d.data, "pendYear") > T.breachYears) {
			d.data.erase("pendId"); d.data.erase("pendSid"); d.data.erase("pendSev"); d.data.erase("pendCirc");
			d.data.erase("pendYear"); d.data.erase("pendRepeat");
			continue;
		}
		const Circumstance circ = Circumstance(int(clamp(bagGet(d.data, "pendCirc"), 0, enumCount<Circumstance>() - 1)));
		const HeartKind heart = heartOf(w, c, -1, id, circ, d.area);
		cards::Proposal p;
		p.kind = CardKind::Judgement; p.culture = c; p.settlement = sid; p.cell = s->cell;
		p.severity = sat(bagGet(d.data, "pendSev"));
		p.ref = { id, int(d.area), int(circ), int(heart) };
		p.data["breach"] = id; p.data["area"] = int(d.area); p.data["circumstance"] = int(circ);
		p.data["heartKind"] = int(heart); p.data["deedWrong"] = 1; p.data["heartWrong"] = heartWrong(heart) ? 1 : 0;
		p.data["repeat"] = bagGet(d.data, "pendRepeat");
		cards::propose(w, p);
	}
}

// ------------------------------------------------------------------ conflict cases (3.7)

struct CaseInfo { DecreeArea a, b; int bendA, bendB; };   // bendX: the answer index that bends decree X (-1 none)
CaseInfo caseInfo(Case k) {
	switch (k) {
	case Case::RaidedWeak: return { DecreeArea::Violence, DecreeArea::Care, 0, -1 };
	case Case::HiddenStranger: return { DecreeArea::Honesty, DecreeArea::Care, 0, -1 };
	case Case::TempleGranary: return { DecreeArea::Wealth, DecreeArea::Care, 0, 1 };
	case Case::EnemyGate: return { DecreeArea::Hospitality, DecreeArea::Violence, 1, -1 };
	case Case::WoodChildren: return { DecreeArea::Land, DecreeArea::Care, -1, -1 };
	case Case::AltarBread: return { DecreeArea::Worship, DecreeArea::Wealth, 1, 0 };
	case Case::CleverTrue: return { DecreeArea::Honesty, DecreeArea::Honesty, -1, -1 };
	case Case::StrongWeak: return { DecreeArea::Care, DecreeArea::Worship, 1, -1 };
	default: return { DecreeArea::Count, DecreeArea::Count, -1, -1 };
	}
}

bool caseHolds(const World& w, int c, Case k, int& other, int& sid, double& stake) {
	const double K = w.tune<DoctrineTune>().conflictKept;
	const Culture* cu = liveCulture(w, c);
	other = -1; sid = -1; stake = 0;
	if (!cu) return false;
	switch (k) {
	case Case::RaidedWeak:
		if (!holds(w, c, DecreeArea::Violence, int(ViolenceStance::Peace), K) || !holds(w, c, DecreeArea::Care, int(CareStance::Shelter), K)) return false;
		other = attackerOf(w, c); sid = capitalOf(w, c); stake = 0.8;
		return other >= 0 && sid >= 0;
	case Case::HiddenStranger:
		if (!holds(w, c, DecreeArea::Honesty, int(HonestyStance::Truth), K)) return false;
		if (!holds(w, c, DecreeArea::Care, int(CareStance::Shelter), K) && !holds(w, c, DecreeArea::Hospitality, int(HospitalityStance::Open), K)) return false;
		other = contactEnemyOf(w, c); sid = capitalOf(w, c); stake = 0.5;
		return other >= 0 && sid >= 0;
	case Case::TempleGranary:
		if (!holds(w, c, DecreeArea::Wealth, int(WealthStance::Tithe), K) || !holds(w, c, DecreeArea::Care, int(CareStance::Shelter), K)) return false;
		if (wealthOf(w, c) < 0.5) return false;
		sid = worstTown(w, c, [](const Settlement& s) { return double(s.famine); });
		stake = std::max(0.4, shareOf(w, c, sid));
		return sid >= 0;
	case Case::EnemyGate: {
		if (!holds(w, c, DecreeArea::Hospitality, int(HospitalityStance::Open), K)) return false;
		if (!holds(w, c, DecreeArea::Violence, int(ViolenceStance::Peace), K) && !holds(w, c, DecreeArea::Violence, int(ViolenceStance::Defence), K)) return false;
		if (!w.geo) return false;
		for (const Unit& u : w.units) {
			if (u.type != UnitType::Army || u.culture == c || u.culture < 0) continue;
			const Relation* r = society::peek(w, c, u.culture);
			if (!r || !r->contact || !alive(w, u.culture)) continue;
			const int uc = units::cellOf(w, u);
			if (!w.valid(uc)) continue;
			int best = -1; double ba = 0.15;
			for (const Settlement& s : w.settlements)
				if (!s.dead && s.culture == c && w.valid(s.cell)) { const double a = w.geo->angle(uc, s.cell); if (a <= ba) { ba = a; best = s.id; } }
			if (best >= 0) {
				other = u.culture; sid = best;
				const Settlement* s = liveSett(w, best);
				stake = std::max(0.3, sat(shareOf(w, c, best) * (s && s->capital ? 1.5 : 1.0)));
				return true;
			}
		}
		return false;
	}
	case Case::WoodChildren:
		return false;                                      // the Land decree and old growth are v2 (M3b)
	case Case::AltarBread:
		if (!holds(w, c, DecreeArea::Worship, int(Rite::Temples), K) || !holds(w, c, DecreeArea::Wealth, int(WealthStance::Share), K)) return false;
		if (cu->edict != Edict::Build || w.year - cu->edictSince > 30) return false;
		sid = worstTown(w, c, [](const Settlement& s) { return double(s.famine); });
		stake = std::max(0.3, shareOf(w, c, sid));
		return sid >= 0;
	case Case::CleverTrue: {
		const Culture* pa = liveCulture(w, cu->parent);
		if (!pa || w.year - cu->born > 10) return false;
		const int mine = stanceOf(w, c, DecreeArea::Honesty), theirs = stanceOf(w, pa->id, DecreeArea::Honesty);
		const bool clash = (mine == int(HonestyStance::Cunning) && theirs == int(HonestyStance::Truth)) ||
			(mine == int(HonestyStance::Truth) && theirs == int(HonestyStance::Cunning));
		if (!clash || doctrine::kept(w, c, DecreeArea::Honesty) < K || doctrine::kept(w, pa->id, DecreeArea::Honesty) < K) return false;
		other = pa->id; sid = capitalOf(w, c); stake = 0.5;
		return sid >= 0;
	}
	case Case::StrongWeak:
		if (!holds(w, c, DecreeArea::Care, int(CareStance::Strength), K) || !holds(w, c, DecreeArea::Worship, int(Rite::Kindness), K)) return false;
		sid = worstTown(w, c, [](const Settlement& s) { return s.plague; });
		stake = std::max(0.3, shareOf(w, c, sid));
		return sid >= 0;
	default:
		return false;
	}
}

bool openConflict(const World& w, int c) {
	for (const Card& k : w.mythos.cards)
		if (k.kind == CardKind::DecreeConflict && k.culture == c && k.state == uint8_t(CardState::Open)) return true;
	return false;
}

void conflicts(World& w, int c) {
	const DoctrineTune& T = w.tune<DoctrineTune>();
	// remember when each case last opened (closed cards are dropped after 48: the record outlives them)
	for (int ki = 0; ki < int(Case::Count); ki++) {
		double last = -1e9;
		if (cardSeen(w, CardKind::DecreeConflict, c, ki, &last)) {
			const std::string key = nkey(("case" + std::to_string(ki)).c_str(), c);
			numSet(w, key, std::max(numGet(w, key, -1e9), last));
		}
	}
	if (openConflict(w, c)) return;
	for (int ki = 0; ki < int(Case::Count); ki++) {
		int other = -1, sid = -1; double stake = 0;
		if (!caseHolds(w, c, Case(ki), other, sid, stake)) continue;
		const double last = numGet(w, nkey(("case" + std::to_string(ki)).c_str(), c), -1e9);
		if (w.year - last < T.conflictGap) continue;
		cards::Proposal p;
		p.kind = CardKind::DecreeConflict; p.culture = c; p.other = other; p.settlement = sid;
		const Settlement* s = liveSett(w, sid);
		p.cell = s ? s->cell : -1;
		p.severity = sat(stake);
		p.ref = { ki, other, sid, -1 };
		p.data["case"] = ki;
		cards::propose(w, p);
		return;                                            // one case per culture at a time
	}
}

// ------------------------------------------------------------------ the first decree (2.3)

void firstDecree(World& w, int c) {
	if (numGet(w, nkey("fd", c)) > 0) return;
	if (cardSeen(w, CardKind::FirstDecree, c, -9)) { numSet(w, nkey("fd", c), 1); return; }
	const Culture* cu = liveCulture(w, c);
	if (!cu) return;
	for (const Decree& d : w.mythos.decrees) if (d.culture == c && !d.custom) { numSet(w, nkey("fd", c), 1); return; }
	if (!hasShrine(w, c) && w.year - cu->born < w.tune<DoctrineTune>().customYear) return;
	const double wp = worldPop(w);
	cards::Proposal p;
	p.kind = CardKind::FirstDecree; p.culture = c; p.settlement = capitalOf(w, c);
	const Settlement* s = liveSett(w, p.settlement);
	p.cell = s ? s->cell : -1;
	p.severity = sat(0.3 + 0.7 * (wp > 0 ? cultPop(w, c) / wp : 0));
	p.ref = { c, -1, -1, -1 };
	cards::propose(w, p);
}

// The custom triggers: the culture's year 200 and its first shrine (emergent only).
void customs(World& w, int c) {
	const Culture* cu = liveCulture(w, c);
	if (!cu) return;
	if (numGet(w, nkey("c200", c)) <= 0 && w.year - cu->born >= w.tune<DoctrineTune>().customYear) {
		numSet(w, nkey("c200", c), 1);
		formCustom(w, c);
	}
	if (numGet(w, nkey("cshrine", c)) <= 0 && hasShrine(w, c)) {
		numSet(w, nkey("cshrine", c), 1);
		formCustom(w, c);
	}
}

// Kept drift, acts-as expiry.
void tendDecrees(World& w, int c) {
	const DoctrineTune& T = w.tune<DoctrineTune>();
	for (size_t i = 0; i < w.mythos.decrees.size(); i++) {
		if (w.mythos.decrees[i].culture != c) continue;
		{
			Decree& d = w.mythos.decrees[i];
			const double until = bagGet(d.data, "actsUntil", -1);
			if (until >= 0 && w.year >= until) {
				d.stance = int8_t(clamp(bagGet(d.data, "stance0", d.stance), 0, stanceCount(d.area) - 1));
				d.data.erase("actsUntil"); d.data.erase("stance0");
			}
		}
		const double t = keptTarget(w, w.mythos.decrees[i]);
		Decree& d = w.mythos.decrees[i];
		d.kept = d.custom ? 1.0 : sat(approach(d.kept, t, T.keptDrift));
	}
}

// ------------------------------------------------------------------ the Word (3.9)

Channel channelFor(const World& w, int c, const WordOpts& o) {
	switch (o.clarity) {
	case Clarity::Exact:
		if (o.channel == Channel::Sky) return Channel::Sky;
		if (messengerFor(w, c, o.figure) >= 0) return Channel::Messenger;
		return w.mythos.presence == Presence::Wonders ? Channel::Sky : Channel::Messenger;
	case Clarity::Parable: return Channel::Messenger;
	case Clarity::Dream: return Channel::Dream;
	default: return Channel::Sky;
	}
}
// The town a Dream is dreamed in: o.ref when it is a live settlement of the culture, else its capital.
int dreamerOf(const World& w, int c, const WordOpts& o) {
	const Settlement* s = liveSett(w, o.ref);
	if (s && s->culture == c) return s->id;
	return capitalOf(w, c);
}
bool writingShared(const World& w, int c) {
	for (const Revelation& r : w.mythos.revelations)
		if (r.culture == c && r.kind == uint8_t(RevelationKind::Writing) && r.outcome == uint8_t(RevelationOutcome::Shared)) return true;
	return false;
}
bool nearMarvel(const World& w, int cell) {
	const ArchetypeParams P = mythos::archetypeParams(w);
	if (!(P.marvelFidelity != 1) || !w.geo || !w.valid(cell)) return false;
	const int n = int(std::max(0.0, std::min(16.0, std::floor(w.vars.num.count("terrain_marvels") ? w.vars.num.at("terrain_marvels") : 0))));
	for (int i = 0; i < n; i++) {
		const auto it = w.vars.num.find("terrain_marvel_" + std::to_string(i));
		if (it == w.vars.num.end()) continue;
		const int m = int(it->second);
		if (w.valid(m) && w.geo->angle(cell, m) <= P.marvelAng) return true;
	}
	return false;
}

bool contentLive(const World& w, int c, const WordOpts& o) {
	const int ph = phaseOf(w);
	switch (o.content) {
	case WordContent::Decree:
		if (uint8_t(o.area) >= uint8_t(DecreeArea::Count)) return false;
		if (o.area == DecreeArea::Land && ph < 2) return false;
		return o.stance >= 0 && o.stance < stanceCount(o.area);
	case WordContent::Repeal:
		return uint8_t(o.area) < uint8_t(DecreeArea::Count) && decree(w, c, o.area) != nullptr;
	case WordContent::Answer: return true;
	case WordContent::Warning: case WordContent::Promise: return false;   // v2: Omens' foretelling, pacts (M3b)
	default: return false;                                                // Revelation: the reveal power (v3)
	}
}

double nextMisread(Rng& r) { return r.f(); }

// Speak now (the checks are the caller's): record, roll, apply, emit. The utterance id.
int speakNow(World& w, int c, const WordOpts& o) {
	const DoctrineTune& T = w.tune<DoctrineTune>();
	const Channel ch = channelFor(w, c, o);
	const int fig = ch == Channel::Messenger ? messengerFor(w, c, o.figure) : -1;
	const double F = fidelity(w, c, o);
	Utterance u;
	u.id = w.mythos.nextId++;
	u.culture = c; u.figure = fig; u.content = uint8_t(o.content); u.clarity = o.clarity; u.channel = ch;
	u.year = w.year; u.fidelity = F;
	const bool decreeWord = o.content == WordContent::Decree || o.content == WordContent::Repeal;
	u.area = decreeWord ? int8_t(o.area) : int8_t(-1);
	u.meant = o.content == WordContent::Decree ? int8_t(o.stance) : int8_t(-1);
	const int dreamer = dreamerOf(w, c, o);
	if (o.clarity == Clarity::Dream) bagSet(u.data, "dreamer", dreamer);

	// ---- the people's reading (one fork per word)
	Rng r = w.rng.fork(forkTag("word", u.id, w.year));
	const double agency = agencyOf(w, c);
	WordOutcome out = WordOutcome::Heard;
	if (!(r.f() < F)) {
		const double x = nextMisread(r);
		switch (o.clarity) {
		case Clarity::Exact: {
			const double pc = std::min(1 - T.literalShare, T.exactCat * (1 - agency) * (0.5 + authorityOf(w, c)));
			out = x < pc ? WordOutcome::Catastrophe : WordOutcome::Literal;
			break;
		}
		case Clarity::Parable:
			out = x < T.parableWisdom ? WordOutcome::Wisdom : WordOutcome::Sect;
			break;
		case Clarity::Dream: {
			const double pc = std::min(1 - T.dreamRival, T.dreamCat * (1 - agency));
			out = x < T.dreamRival ? WordOutcome::Rival : x < T.dreamRival + pc ? WordOutcome::Catastrophe : WordOutcome::Personal;
			break;
		}
		default: {
			const double pc = std::min(std::max(0.0, 1 - T.signHouse - T.signRival), T.signCat * (1 - agency) * (1 + corruptionOf(w, c)));
			out = x < T.signRival ? WordOutcome::Rival : x < T.signRival + pc ? WordOutcome::Catastrophe : WordOutcome::HouseRead;
			break;
		}
		}
	}
	u.outcome = uint8_t(out);

	// ---- what they heard (Decree: a stance; Repeal: the repeal)
	int heard = u.meant;
	bool takes = true;                                    // the decree word takes effect
	if (o.content == WordContent::Decree) {
		const int n = stanceCount(o.area);
		if (out == WordOutcome::Wisdom) {                 // the neighbouring milder stance
			const double s0 = strictness(w, o.area, o.stance);
			int best = o.stance; double bs = -1;
			for (int s : { o.stance - 1, o.stance + 1 }) {
				if (s < 0 || s >= n) continue;
				const double ss = strictness(w, o.area, s);
				if (ss < s0 && ss > bs) { bs = ss; best = s; }
			}
			heard = best;
		} else if (out == WordOutcome::HouseRead) {       // the House picks the stricter meaning
			int best = o.stance; double bs = strictness(w, o.area, o.stance);
			for (int s = 0; s < n; s++) if (strictness(w, o.area, s) > bs) { bs = strictness(w, o.area, s); best = s; }
			heard = best;
		} else if (out == WordOutcome::Catastrophe && o.clarity != Clarity::Exact) {
			takes = false;                                // a Dream's exodus, a Sign's burning: no decree came of it
			heard = -1;
		}
	} else if (o.content == WordContent::Repeal) {
		takes = out != WordOutcome::Catastrophe;
	}
	u.heard = int8_t(o.content == WordContent::Decree ? heard : -1);

	// keep the last maxWords
	w.mythos.words.push_back(u);
	const size_t cap = size_t(std::max(1, T.maxWords));
	if (w.mythos.words.size() > cap) w.mythos.words.erase(w.mythos.words.begin(), w.mythos.words.begin() + ptrdiff_t(w.mythos.words.size() - cap));
	numSet(w, nkey("lastWord", c), w.year);
	numSet(w, nkey("lastDecreeWord", c), decreeWord ? w.year : numGet(w, nkey("lastDecreeWord", c), -1e9));
	w.bus.emit(EvWord{ u.id, c, toStr(o.content), toStr(o.clarity), toStr(ch), toStr(out) });

	// ---- the decree itself
	if (o.content == WordContent::Decree && takes && heard >= 0) {
		const Decree* old = decree(w, c, o.area);
		const int prev = old ? old->stance : -1;
		Decree& d = placeDecree(w, c, o.area);
		d.stance = int8_t(heard); d.clarity = o.clarity; d.since = w.year; d.by = fig; d.custom = false; d.data.clear();
		bagSet(d.data, "fidelity", F);
		bagSet(d.data, "word", u.id);
		if (out == WordOutcome::Literal || (out == WordOutcome::Catastrophe && o.clarity == Clarity::Exact)) bagSet(d.data, "literal", 1);
		if (out == WordOutcome::Personal) { bagSet(d.data, "personal", 1); bagSet(d.data, "dreamer", dreamer); }
		d.kept = keptTarget(w, d);
		emitDecree(w, c, o.area, heard, prev, toStr(o.clarity));
		numSet(w, nkey("fd", c), 1);
	} else if (o.content == WordContent::Repeal && takes) {
		const Decree* old = decree(w, c, o.area);
		const int prev = old ? old->stance : -1;
		if (eraseDecree(w, c, o.area)) emitDecree(w, c, o.area, -1, prev, toStr(o.clarity));
	}

	// ---- consequences of the reading
	if (fig >= 0 && (out == WordOutcome::Heard || out == WordOutcome::Wisdom)) characters::adjustCredibility(w, fig, T.heardCred);
	const int town = o.clarity == Clarity::Dream ? dreamer : capitalOf(w, c);
	switch (out) {
	case WordOutcome::Wisdom: {
		if (const Culture* cu = liveCulture(w, c)) {
			Traits t = cu->traits;
			t.compassion = sat(t.compassion + 0.02);
			society::shapeTraits(w, c, t);
		}
		break;
	}
	case WordOutcome::Sect: if (town >= 0) faiths::seedRival(w, c, town, T.sectFollowing); break;
	case WordOutcome::Rival:
		if (town >= 0) faiths::seedRival(w, c, town, o.clarity == Clarity::Dream ? T.dreamFollowing : T.signFollowing);
		break;
	case WordOutcome::HouseRead: faiths::adjustHouse(w, c, 0, 0, 0.1, 0); break;
	case WordOutcome::Catastrophe:
		if (o.clarity == Clarity::Exact) {
			// the letter turned on someone: a war of zeal on the least devout neighbour they already dislike, else a purge
			int foe = -1; double fd = 2;
			for (const Rel& x : relationsOf(w, c)) {
				if (x.r->war || !x.r->contact || !(x.r->v < 0) || !alive(w, x.other)) continue;
				double dv = 0, n = 0;
				for (const Settlement& s : w.settlements) if (!s.dead && s.culture == x.other) { dv += s.devotion; n++; }
				const double mean = n > 0 ? dv / n : 1;
				if (mean < fd) { fd = mean; foe = x.other; }
			}
			if (foe >= 0) society::declareWar(w, c, foe);
			else {
				// the town with the most strangers: the most foreign towns within 0.3 rad (ties: people, id)
				int best = -1; int bn = -1; double bp = -1;
				for (const Settlement& s : w.settlements) {
					if (s.dead || s.culture != c || !w.valid(s.cell) || !w.geo) continue;
					int nf = 0;
					for (const Settlement& o2 : w.settlements)
						if (!o2.dead && o2.culture != c && w.valid(o2.cell) && w.geo->angle(s.cell, o2.cell) <= 0.3) nf++;
					if (nf > bn || (nf == bn && s.pop > bp)) { bn = nf; bp = s.pop; best = s.id; }
				}
				if (best >= 0) civ::damage(w, best, 0.1, Cause::War);
			}
		} else if (o.clarity == Clarity::Dream) {
			if (dreamer >= 0) civ::exodus(w, dreamer, T.exodusShare);
		} else {
			// a burning: an academy town loses 30 progress-years, or the House's temple burns
			int acad = -1;
			for (const Settlement& s : w.settlements) if (!s.dead && s.culture == c && s.has(Building::ACADEMY)) { acad = s.id; break; }
			if (acad >= 0) civ::strikeKnowledge(w, { acad }, 30);
			else {
				faiths::adjustHouse(w, c, -0.3, 0, 0, 0);
				int temple = -1;
				for (const Settlement& s : w.settlements) if (!s.dead && s.culture == c && s.has(Building::TEMPLE)) { temple = s.id; break; }
				if (temple >= 0) civ::demoteTemple(w, temple);
			}
		}
		break;
	default: break;
	}
	return u.id;
}

// ------------------------------------------------------------------ card helpers

CardOption opt(const World& w, int choice, double cost, bool avail, OptionWhy why = OptionWhy::Unavailable) {
	CardOption o; o.choice = choice; o.cost = std::max(0.0, cost);
	if (!avail) { o.ok = false; o.why = why; return o; }
	if (o.cost > 0 && w.god.awe < o.cost) { o.ok = false; o.why = OptionWhy::Awe; return o; }
	o.ok = true; o.why = OptionWhy::Ok;
	return o;
}
// The word the FirstDecree card's "give one decree" speaks: the area and stance the people lean to, through
// their messenger as a parable, else as a dream.
WordOpts firstDecreeWord(const World& w, int c) {
	WordOpts o;
	o.content = WordContent::Decree;
	DecreeArea a = DecreeArea::Count; double bv = -1;
	for (int ai = 0; ai < int(DecreeArea::Count); ai++) {
		const DecreeArea x = DecreeArea(ai);
		if (x == DecreeArea::Land || x == DecreeArea::Worship) continue;
		const auto s = stanceScores(w, c, x);
		const double v = *std::max_element(s.begin(), s.begin() + stanceCount(x));
		if (v > bv) { bv = v; a = x; }
	}
	o.area = a == DecreeArea::Count ? DecreeArea::Violence : a;
	o.stance = std::max(0, customStance(w, c, o.area));
	const bool msg = messengerFor(w, c, -1) >= 0 && w.mythos.presence != Presence::Unseen;
	o.clarity = msg ? Clarity::Parable : Clarity::Dream;
	o.channel = msg ? Channel::Messenger : Channel::Dream;
	o.ref = capitalOf(w, c);
	return o;
}
bool decreeGivenSince(const World& w, int c, double since) { return numGet(w, nkey("lastDecreeWord", c), -1e9) >= since; }

void nudgeCulture(World& w, int c, double dDev, double dLove) {
	for (size_t i = 0; i < w.settlements.size(); i++)
		if (!w.settlements[i].dead && w.settlements[i].culture == c) society::nudge(w, int(i), dDev, dLove);
}
void actsAs(World& w, int c, DecreeArea a, int stance, double years) {
	Decree* d = findDecree(w, c, a);
	if (!d) return;
	if (bagGet(d->data, "actsUntil", -1) < 0) bagSet(d->data, "stance0", d->stance);
	d->stance = int8_t(stance);
	bagSet(d->data, "actsUntil", w.year + years);
}
bool bent(const World& w, int c, Case k, int choice) {
	const CaseInfo ci = caseInfo(k);
	const auto lit = [&](DecreeArea a) { const Decree* d = decree(w, c, a); return d && bagGet(d->data, "literal") > 0; };
	return (choice == ci.bendA && lit(ci.a)) || (choice == ci.bendB && lit(ci.b));
}

std::vector<CardOption> conflictOptions(const World& w, const Card& k) {
	const Case cs = Case(k.ref[0]);
	std::array<double, 3> cost{ 0, 0, 0 };
	std::array<bool, 3> avail{ true, true, true };
	switch (cs) {
	case Case::RaidedWeak: cost[2] = 80; break;
	case Case::HiddenStranger: cost[2] = 30; break;
	case Case::TempleGranary: break;
	case Case::EnemyGate: case Case::AltarBread: case Case::StrongWeak: avail[2] = false; break;
	case Case::CleverTrue: break;
	default: avail = { false, false, false }; break;
	}
	std::vector<CardOption> out;
	for (int i = 0; i < 3; i++) out.push_back(opt(w, i, cost[size_t(i)], avail[size_t(i)] && !bent(w, k.culture, cs, i)));
	return out;
}

bool answerConflict(World& w, const Card& k, int choice) {
	const int c = k.culture;
	const Case cs = Case(k.ref[0]);
	const auto o = conflictOptions(w, k);
	if (choice < 0 || choice >= int(o.size()) || (!o[size_t(choice)].ok && o[size_t(choice)].why != OptionWhy::Awe)) return false;
	const int other = k.other >= 0 ? k.other : k.ref[1];
	const int sid = k.settlement >= 0 ? k.settlement : k.ref[2];
	switch (cs) {
	case Case::RaidedWeak:
		if (choice == 0) actsAs(w, c, DecreeArea::Violence, int(ViolenceStance::Defence), 50);
		else if (choice == 1) nudgeCulture(w, c, 0, 0.05);
		else if (alive(w, other) && society::atWar(w, c, other)) society::makePeace(w, c, other);
		return true;
	case Case::HiddenStranger: {
		int pick = choice;
		if (choice == 2) {                                 // a dream: the people choose, by their own judgement
			Rng r = w.rng.fork(forkTag("stranger", k.id, w.year));
			pick = r.chance(agencyOf(w, c)) ? 0 : 1;
		}
		if (pick == 0) {
			if (Decree* d = findDecree(w, c, DecreeArea::Honesty)) bagSet(d->data, "strictAdj", bagGet(d->data, "strictAdj") - 0.3);
		} else {
			nudgeCulture(w, c, 0, -0.04);
		}
		return true;
	}
	case Case::TempleGranary:
		if (choice == 0) { faiths::adjustHouse(w, c, 0, -0.3, 0, -0.1); if (sid >= 0) society::nudge(w, sid, 0, 0.05); }
		else if (choice == 1) { faiths::adjustHouse(w, c, 0, 0, 0.1, 0); if (sid >= 0) society::nudge(w, sid, 0, -0.05); }
		else if (const Settlement* s = liveSett(w, sid)) {
			powers::UseOpts uo; uo.dt = 1.0;
			powers::use(w, "bloom", s->cell, uo);           // the god feeds them itself (its own cost; may be refused)
		}
		return true;
	case Case::EnemyGate:
		if (choice == 0) { if (alive(w, other) && society::atWar(w, c, other)) society::makePeace(w, c, other); }
		else actsAs(w, c, DecreeArea::Hospitality, int(HospitalityStance::Guarded), 40);
		return true;
	case Case::AltarBread:
		if (choice == 0) { nudgeCulture(w, c, 0.02, 0); if (sid >= 0) society::nudge(w, sid, 0, -0.03); }
		else { if (sid >= 0) society::nudge(w, sid, 0, 0.04); const int cap = capitalOf(w, c); if (cap >= 0) society::nudge(w, cap, -0.02, 0); }
		return true;
	case Case::CleverTrue:
		if (choice == 1) nudgeCulture(w, c, 0, -0.1);      // the parent's decree is right: the daughter is hurt
		else if (choice == 2 && alive(w, other)) nudgeCulture(w, other, 0, -0.1);
		return true;
	case Case::StrongWeak:
		if (choice == 1) actsAs(w, c, DecreeArea::Care, int(CareStance::Kin), 50);
		return true;
	default:
		return false;
	}
}

void lapseConflict(World& w, const Card& k) {
	const DoctrineTune& T = w.tune<DoctrineTune>();
	const CaseInfo ci = caseInfo(Case(k.ref[0]));
	if (ci.a == DecreeArea::Count) return;
	int ca = k.culture, cb = k.culture;
	if (Case(k.ref[0]) == Case::CleverTrue) cb = k.other >= 0 ? k.other : k.ref[1];
	Decree* da = findDecree(w, ca, ci.a);
	Decree* db = findDecree(w, cb, ci.b);
	if (!da || !db || da == db) return;
	Decree* loser = effStrict(w, *da) >= effStrict(w, *db) ? db : da;   // the stricter decree wins
	if (!loser->custom) loser->kept = sat(loser->kept + T.lapseKept);
}

int sentenceTown(const World& w, const Card& k) {
	if (liveSett(w, k.settlement)) return k.settlement;
	if (const Figure* f = characters::figure(w, k.figure)) if (liveSett(w, f->home)) return f->home;
	return capitalOf(w, k.culture);
}
bool rulerDeed(const World& w, const Card& k) {
	const Figure* f = characters::figure(w, k.figure);
	return f && f->kind == uint8_t(FigureKind::Ruler) && f->fate == uint8_t(FigureFate::Living);
}

std::vector<CardOption> judgementOptions(const World& w, const Card& k) {
	const DoctrineTune& T = w.tune<DoctrineTune>();
	std::vector<CardOption> out;
	const int sid = sentenceTown(w, k);
	for (int by = 0; by < 2; by++)
		for (int s = 0; s < 4; s++) {
			const int ch = by * 4 + s;
			switch (Sentence(s)) {
			case Sentence::Forgive: out.push_back(opt(w, ch, 0, true)); break;
			case Sentence::Warn: out.push_back(opt(w, ch, T.warnCost, sid >= 0)); break;
			case Sentence::Remove: out.push_back(opt(w, ch, T.removeCost, rulerDeed(w, k), OptionWhy::NeedsRuler)); break;
			default: {
				// the wrath power pays its own cost when it strikes (powers::use)
				const Settlement* s2 = liveSett(w, sid);
				const powers::CanResult cr = s2 ? powers::can(w, "lightning", s2->cell) : powers::CanResult{};
				const OptionWhy why = cr.reason == powers::Reason::Awe ? OptionWhy::Awe : OptionWhy::Locked;
				out.push_back(opt(w, ch, 0, s2 && cr.ok, s2 ? why : OptionWhy::Unavailable));
				break;
			}
			}
		}
	return out;
}

bool answerJudgement(World& w, const Card& k, int choice) {
	const DoctrineTune& T = w.tune<DoctrineTune>();
	if (choice < 0 || choice > 7) return false;
	const JudgeBy by = JudgeBy(choice / 4);
	const Sentence s = Sentence(choice % 4);
	const int c = k.culture;
	const int sid = sentenceTown(w, k);
	const int cardId = k.id, figId = k.figure;
	switch (s) {
	case Sentence::Forgive: {
		const double hearts = judgedByOf(w, c) >= T.judgedLine ? 1.3 : 1.0;
		if (sid >= 0) society::nudge(w, sid, 0, T.forgiveLove * hearts);
		break;
	}
	case Sentence::Warn:
		if (sid < 0) return false;
		break;                                              // a dream or a sign over the town: nobody is harmed
	case Sentence::Remove: {
		if (!rulerDeed(w, k)) return false;
		if (!characters::fell(w, figId, FigureFate::Fallen)) return false;
		const int cap = capitalOf(w, c);
		if (cap >= 0) society::nudge(w, cap, 0, T.removeFear);
		break;
	}
	default: {
		const Settlement* st = liveSett(w, sid);
		if (!st) return false;
		const int cell = st->cell;
		if (!powers::use(w, "lightning", cell)) return false;   // refused: the card stays open
		break;
	}
	}
	w.bus.emit(EvJudgement{ cardId, c, toStr(by), toStr(s) });
	return true;
}

} // namespace

// ================================================================== the API

void yearly(World& w) {
	if (phaseOf(w) < 1) return;
	const bool em = emergentOn(w);
	for (size_t ci = 0; ci < w.cultures.size(); ci++) {
		const int c = int(ci);
		if (w.cultures[ci].dead) continue;
		tendDecrees(w, c);
		if (em) customs(w, c);
		breaches(w, c);
		proposeJudgements(w, c);
		conflicts(w, c);
		firstDecree(w, c);
	}
}

SpeakCheck canSpeak(const World& w, int cultureId, const WordOpts& o) {
	SpeakCheck r;
	r.why = WordWhy::Phase;
	if (phaseOf(w) < 1 || w.phase != Phase::History) return r;
	if (!liveCulture(w, cultureId) || largestTown(w, cultureId) < 0) { r.why = WordWhy::Target; return r; }
	if (uint8_t(o.clarity) >= uint8_t(enumCount<Clarity>()) || !contentLive(w, cultureId, o)) { r.why = WordWhy::Content; return r; }
	const Presence pr = w.mythos.presence;
	if (pr == Presence::Unseen && (o.clarity == Clarity::Exact || o.clarity == Clarity::Parable)) { r.why = WordWhy::Content; return r; }
	const double last = numGet(w, nkey("lastWord", cultureId), -1e9);
	if (w.year - last < w.tune<DoctrineTune>().wordGap) { r.why = WordWhy::Gap; return r; }
	const Channel ch = channelFor(w, cultureId, o);
	if (ch == Channel::Messenger && messengerFor(w, cultureId, o.figure) < 0) { r.why = WordWhy::NoMessenger; return r; }
	if (o.clarity == Clarity::Exact && ch == Channel::Sky && pr != Presence::Wonders) { r.why = WordWhy::NoMessenger; return r; }
	r.cost = wordCost(w, cultureId, o);
	if (w.god.awe < r.cost) { r.why = WordWhy::Awe; return r; }
	r.ok = true;
	r.why = WordWhy::Ok;
	return r;
}

int speak(World& w, int cultureId, const WordOpts& o) {
	if (!canSpeak(w, cultureId, o).ok) return -1;
	return speakNow(w, cultureId, o);
}

double wordCost(const World& w, int cultureId, const WordOpts& o) {
	if (!liveCulture(w, cultureId) || uint8_t(o.clarity) >= uint8_t(enumCount<Clarity>())) return 0.0;
	const DoctrineTune& T = w.tune<DoctrineTune>();
	const MythosTune& M = w.tune<MythosTune>();
	double c = T.wordCost[size_t(o.clarity)];
	if (w.mythos.presence == Presence::Messengers) c *= M.msgWordCost;
	if (o.clarity == Clarity::Exact && channelFor(w, cultureId, o) == Channel::Sky && w.mythos.presence == Presence::Wonders) c *= M.exactSkyCost;
	return isFinite(c) && c > 0 ? c : 0.0;
}

double fidelity(const World& w, int cultureId, const WordOpts& o) {
	if (!liveCulture(w, cultureId) || uint8_t(o.clarity) >= uint8_t(enumCount<Clarity>())) return 0.0;
	const DoctrineTune& T = w.tune<DoctrineTune>();
	const CharactersTune& C = w.tune<CharactersTune>();
	const Channel ch = channelFor(w, cultureId, o);
	const int fig = ch == Channel::Messenger ? messengerFor(w, cultureId, o.figure) : -1;
	const Figure* f = characters::figure(w, fig);
	const double cred = f ? sat(characters::credibility(w, fig)) : T.noMessengerCred;
	double bonus = 1;
	const int hon = stanceOf(w, cultureId, DecreeArea::Honesty);
	if (hon == int(HonestyStance::Truth)) bonus += T.truthFidelity;
	else if (hon == int(HonestyStance::Discretion)) bonus += T.discretionFidelity;
	if (writingShared(w, cultureId)) bonus += T.writingFidelity;
	double mul = 1;
	if (f) {
		const MessengerArchetype a = MessengerArchetype(f->archetype);
		if (a == MessengerArchetype::Child) bonus += T.childFidelity;
		if (a == MessengerArchetype::Shepherd && o.clarity == Clarity::Parable) bonus += C.parableShepherd;
		if (a == MessengerArchetype::Queen && o.clarity == Clarity::Exact) bonus += C.queenFidelity;
		if (a == MessengerArchetype::Unbelieved && !(f->flags & FigureFlag::Vindicated)) mul *= C.unbelievedStart;
		if (f->flags & FigureFlag::Discredited) mul = 0;   // words through a discredited messenger fail
	}
	for (const Figure& v : w.mythos.figures)               // a vindicated voice of this people: every later word
		if (v.culture == cultureId && (v.flags & FigureFlag::Vindicated)) { bonus += C.unbelievedFidelity; break; }
	if (o.clarity == Clarity::Dream || o.clarity == Clarity::Sign) {
		const Settlement* s = liveSett(w, o.clarity == Clarity::Dream ? dreamerOf(w, cultureId, o) : capitalOf(w, cultureId));
		if (s && nearMarvel(w, s->cell)) mul *= mythos::archetypeParams(w).marvelFidelity;
	}
	const double F = T.fidelityBase[size_t(o.clarity)] * (T.credFloor + T.credK * cred) * bonus * mul;
	return isFinite(F) ? clamp(F, 0, T.fidelityCap) : 0.0;
}

const Decree* decree(const World& w, int cultureId, DecreeArea area) {
	for (const Decree& d : w.mythos.decrees) if (d.culture == cultureId && d.area == area) return &d;
	return nullptr;
}

double kept(const World& w, int cultureId, DecreeArea area) {
	const Decree* d = decree(w, cultureId, area);
	return d && isFinite(d->kept) ? d->kept : 0.0;
}

double burden(const World& w, int cultureId) {
	double s = 0; int n = 0;
	for (const Decree& d : w.mythos.decrees)
		if (d.culture == cultureId && !d.custom && d.stance >= 0) { s += effStrict(w, d); n++; }
	return n > 0 ? s / n : 0.0;
}

double strictness(const World& w, DecreeArea area, int stance) {
	const DoctrineTune& T = w.tune<DoctrineTune>();
	const size_t a = size_t(area);
	if (a >= T.strictness.size() || stance < 0 || stance >= stanceCount(area)) return 0.0;
	return T.strictness[a][size_t(stance)];
}

int customStance(const World& w, int cultureId, DecreeArea area) {
	if (!liveCulture(w, cultureId) || uint8_t(area) >= uint8_t(DecreeArea::Count)) return -1;
	const auto s = stanceScores(w, cultureId, area);
	int best = -1; double bv = -1e9;
	for (int i = 0; i < stanceCount(area); i++) if (s[size_t(i)] > bv) { bv = s[size_t(i)]; best = i; }
	return best;
}

int swearPact(World& w, int cultureId, GodTerm godTerm, PeopleTerm peopleTerm, double years, int figure) {
	(void)w; (void)cultureId; (void)godTerm; (void)peopleTerm; (void)years; (void)figure;
	return -1;                                              // pacts are v2 (M3b)
}

bool reveal(World& w, int cultureId, RevelationKind kind, Clarity clarity) {
	(void)w; (void)cultureId; (void)kind; (void)clarity;
	return false;                                           // revelations are v3 (M3c)
}

DeathBelief deathBelief(const World& w, int cultureId) {
	if (cultureId < 0 || size_t(cultureId) >= w.mythos.faith.size()) return DeathBelief::None;
	const int d = w.mythos.faith[size_t(cultureId)].death;
	return d >= 0 && d < enumCount<DeathBelief>() ? DeathBelief(d) : DeathBelief::None;
}

bool answer(World& w, int cardId, int choice) {
	if (phaseOf(w) < 1) return false;
	const Card* kp = cards::find(w, cardId);            // Cards dispatches before it marks the card answered
	if (!kp) return false;
	const Card k = *kp;                                     // a copy: handlers may add cards or figures
	switch (k.kind) {
	case CardKind::FirstDecree: {
		if (choice == 0) {
			if (decreeGivenSince(w, k.culture, k.opened)) return true;   // the god already gave one with the Word
			if (!liveCulture(w, k.culture) || largestTown(w, k.culture) < 0) return false;
			return speakNow(w, k.culture, firstDecreeWord(w, k.culture)) >= 0;
		}
		if (choice == 1) { formCustom(w, k.culture); numSet(w, nkey("fd", k.culture), 1); return true; }
		return false;
	}
	case CardKind::DecreeConflict: return answerConflict(w, k, choice);
	case CardKind::Breach: return choice == 0;              // never proposed by this module (it proposes Judgement)
	case CardKind::Judgement: return answerJudgement(w, k, choice);
	default: return false;                                  // pacts, land, revelations, death: v2 / v3
	}
}

void lapse(World& w, int cardId) {
	if (phaseOf(w) < 1) return;
	const Card* kp = cards::find(w, cardId);
	if (!kp) return;
	const Card k = *kp;
	switch (k.kind) {
	case CardKind::FirstDecree:
		numSet(w, nkey("fd", k.culture), 1);
		if (emergentOn(w)) formCustom(w, k.culture);       // they take a custom (an emergent rule)
		break;
	case CardKind::DecreeConflict: lapseConflict(w, k); break;
	default: break;                                         // Judgement: the people judge for themselves
	}
}

std::vector<CardOption> options(const World& w, int cardId) {
	const Card* kp = cards::find(w, cardId);
	if (!kp || phaseOf(w) < 1) return {};
	const Card& k = *kp;
	switch (k.kind) {
	case CardKind::FirstDecree: {
		const bool given = decreeGivenSince(w, k.culture, k.opened);
		const bool can = liveCulture(w, k.culture) && largestTown(w, k.culture) >= 0;
		const double cost = given || !can ? 0 : wordCost(w, k.culture, firstDecreeWord(w, k.culture));
		return { opt(w, 0, cost, can), opt(w, 1, 0, true) };
	}
	case CardKind::DecreeConflict: return conflictOptions(w, k);
	case CardKind::Breach: return { opt(w, 0, 0, true) };
	case CardKind::Judgement: return judgementOptions(w, k);
	default: return {};
	}
}

} // namespace dm::doctrine
