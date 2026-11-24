// DEMIURGE — Sim/Characters.cpp : the figure registry (SPEC-MYTHOS 3.8 messengers, 3.23 rulers; v1).
// Owner: characters track M4 (key "characters"). Header Characters.h is frozen (M0).
//
// What this file does (phase >= 1; at MythosTune::phase 0 every mutator is neutral and figures never appear):
//   - candidates(w, sid): three would-be messengers of a town, a pure read forked from the WORLD SEED per town and
//     decade (w.rng's state moves every tick, so a w.rng fork could not show "the same three" when re-armed);
//   - choose: the chosen candidate becomes a Messenger figure and society::raiseProphet names it; the archetype's
//     risks are rolled once at that moment (the Criminal's relapse, the Queen's pride) with a w.rng fork;
//   - yearly: messengers follow Society's voice (Culture::prophet; the figure dies when it no longer names the figure),
//     gain credibility from the god's miracles in their towns (MythosState::acts), lose it to unforetold wrath,
//     drift toward their archetype's start; the Criminal's redemption and relapse, the Child's yearly credibility
//     and martyrdom, the Queen's pride; each living Bronze+ culture with a capital keeps exactly one ruler (reigns
//     15-45 years, heir 75 %, a new house when the capital fell or was taken); devout cultures "seek a voice";
//   - the MessengerChoice card's options / answer / lapse.
// Not here (other tracks read the figure): word fidelity terms (Doctrine), the voice's travel and the Child's longer
// term (Society), the House's corruption and authority (Faiths, through faiths::adjustHouse which we call),
// legends (through legends::add, which we call), trials / healers / Lives (M4b, M4c: still neutral below).
//
// Private data keys (Figure::data, private to Characters):
//   "chosen"    year a messenger was chosen        "until"  a ruler's reign end
//   "house"     a ruler's house number             "from"   a ruler's first year
//   "relapseAt" year a Criminal relapses (if not redeemed first)
//   "prideAt"   year a proud Queen shows that pride"deed" / "deedAt"  the deed standing for judgement (1 relapse, 2 pride)
//   "quoted"    year until which a vindicated voice is quoted   "by"  who took a martyred Child's town (culture; -1 the god, -3 razed in a war)
// MythosState::num keys: "characters_acts" (acts processed up to this year), "characters_house" (house counter),
// "characters_voice<culture id>" (last year the culture had a living voice).
// Proposals: MessengerChoice ("seek a voice": settlement, data cost = askCost, asked = 1, decade); Judgement for a
// figure's deed (figure = the figure, settlement = home, ref[0] = figure id, ref[1] = deed code, data deed = code).
#include "Characters.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>

#include "Cards.h"
#include "Faiths.h"
#include "Legends.h"
#include "Mythos.h"
#include "Names.h"
#include "Powers.h"
#include "Society.h"

namespace dm::characters {

namespace {

constexpr int kDeedRelapse = 1, kDeedPride = 2;
constexpr double kDeedWindow = 30;            // years a figure's deed stands for judgement (re-proposed yearly)

const CharactersTune& tuneOf(const World& w) { return w.tune<CharactersTune>(); }
bool on(const World& w) { return w.tune<MythosTune>().phase >= 1; }
int64_t yearInt(const World& w) { return int64_t(std::floor(isFinite(w.year) ? w.year : 0)); }

double finite01(double v) { return isFinite(v) ? clamp(v, 0.0, 1.0) : 0.0; }
double getOr(const NumBag& b, const std::string& k, double def) {
	auto it = b.find(k);
	return it == b.end() ? def : it->second;
}

const Settlement* liveTown(const World& w, int sid) {
	if (sid < 0 || sid >= int(w.settlements.size())) return nullptr;
	const Settlement& s = w.settlements[size_t(sid)];
	return s.dead ? nullptr : &s;
}
const Culture* liveCulture(const World& w, int cid) {
	if (cid < 0 || cid >= int(w.cultures.size())) return nullptr;
	const Culture& c = w.cultures[size_t(cid)];
	return c.dead ? nullptr : &c;
}
Figure* findMut(World& w, int id) {
	for (Figure& f : w.mythos.figures) if (f.id == id) return &f;
	return nullptr;
}
bool living(const Figure& f) { return f.fate == uint8_t(FigureFate::Living); }
bool isKind(const Figure& f, FigureKind k) { return f.kind == uint8_t(k); }
int archIndex(const Figure& f) { return f.archetype < 6 ? int(f.archetype) : 0; }

// A name that is never empty (Names never returns empty, but a stub or an odd language must not leak "").
std::string safeName(std::string s, const char* fallback) { return s.empty() ? std::string(fallback) : s; }

void emitFigure(World& w, const Figure& f, const char* what) {
	w.bus.emit(EvFigure{ f.id, toStr(FigureKind(f.kind)), f.culture, f.home, what });
}

// The settlement's place among the culture's town tiers (3.8 reach): 0 rural, 1 town, 2 court, 3 outcast.
int tierClass(const Settlement& s) {
	if (s.famine > 0 || s.plague > 0) return 3;
	if (s.capital) return 2;
	return s.tier >= 2 ? 1 : 0;
}

// The archetype's reach weighted by where the culture's people live; the row mean without people.
double reachFor(const World& w, int cultureId, int arch) {
	const CharactersTune& t = tuneOf(w);
	const std::array<double, 4>& row = t.archReach[size_t(arch < 6 && arch >= 0 ? arch : 0)];
	double wsum = 0, acc = 0;
	for (const Settlement& s : w.settlements) {
		if (s.dead || s.culture != cultureId || !(s.pop > 0) || !isFinite(s.pop)) continue;
		wsum += s.pop;
		acc += s.pop * row[size_t(tierClass(s))];
	}
	if (wsum > 0) return finite01(acc / wsum);
	return finite01((row[0] + row[1] + row[2] + row[3]) / 4);
}

// Figures something else still names (save validation: every figure reference must resolve).
std::set<int> referenced(const World& w) {
	std::set<int> r;
	const MythosState& m = w.mythos;
	for (const Culture& c : w.cultures) if (c.prophet && c.prophet->figure >= 0) r.insert(c.prophet->figure);
	for (const Decree& d : m.decrees) if (d.by >= 0) r.insert(d.by);
	for (const Utterance& u : m.words) if (u.figure >= 0) r.insert(u.figure);
	for (const Pact& p : m.pacts) if (p.figure >= 0) r.insert(p.figure);
	for (const Card& c : m.cards) if (c.figure >= 0) r.insert(c.figure);
	for (const RivalVoice& v : m.rivals) if (v.figure >= 0) r.insert(v.figure);
	for (const Omen& o : m.omens) if (o.figure >= 0) r.insert(o.figure);
	for (const TownMyth& t : m.towns) if (t.healer >= 0) r.insert(t.healer);
	return r;
}

// Keep the registry under its cap (1.2: 160): drop the longest-dead figure nothing names (ties by id). Living
// figures, referenced ones and a vindicated voice still quoted are kept. False when nothing can go.
bool makeRoom(World& w) {
	const int cap = std::max(1, tuneOf(w).maxFigures);
	std::vector<Figure>& v = w.mythos.figures;
	if (int(v.size()) < cap) return true;
	const std::set<int> refs = referenced(w);
	while (int(v.size()) >= cap) {
		int best = -1;
		for (int i = 0; i < int(v.size()); i++) {
			const Figure& f = v[size_t(i)];
			if (living(f) || refs.count(f.id)) continue;
			if ((f.flags & FigureFlag::Vindicated) && getOr(f.data, "quoted", -1) > w.year) continue;
			if (best < 0) { best = i; continue; }
			const Figure& b = v[size_t(best)];
			if (f.died < b.died || (f.died == b.died && f.id < b.id)) best = i;
		}
		if (best < 0) return false;
		v.erase(v.begin() + best);
	}
	return true;
}

// Append a figure with a fresh id (mythos.nextId). Returns the id, or -1 when the registry is full.
int addFigure(World& w, Figure f) {
	if (!makeRoom(w)) return -1;
	if (w.mythos.nextId < 1) w.mythos.nextId = 1;
	f.id = w.mythos.nextId++;
	f.credibility = finite01(f.credibility);
	f.reach = finite01(f.reach);
	if (!isFinite(f.fame)) f.fame = 0;
	if (!isFinite(f.born)) f.born = w.year;
	w.mythos.figures.push_back(std::move(f));
	return w.mythos.figures.back().id;
}

float r01(Rng& r, double lo, double hi) { return float(clamp(r.range(lo, hi), 0.0, 1.0)); }

// The would-be messengers of one town in one decade (pure: forked from the world seed).
std::vector<Figure> candidatesAt(const World& w, int sid, int64_t decade) {
	std::vector<Figure> out;
	const Settlement* s = liveTown(w, sid);
	if (!s) return out;
	const Culture* c = liveCulture(w, s->culture);
	if (!c) return out;
	const CharactersTune& t = tuneOf(w);
	const bool ruled = s->capital || (s->tier >= 2 && ruler(w, c->id) >= 0);
	std::vector<MessengerArchetype> pool{ MessengerArchetype::Shepherd };
	if (s->tier >= 2) pool.push_back(MessengerArchetype::Criminal);
	pool.push_back(MessengerArchetype::Child);
	if (ruled) pool.push_back(MessengerArchetype::Queen);
	pool.push_back(MessengerArchetype::Unbelieved);
	Rng r = Rng(w.seed).fork("myth:characters:candidates:" + std::to_string(sid) + ":" + std::to_string(decade));
	const int want = std::clamp(t.candidates, 0, int(pool.size()));
	for (int k = 0; k < want; k++) {
		const int i = r.pickIndex(int(pool.size()));
		if (i < 0) break;
		const MessengerArchetype a = pool[size_t(i)];
		pool.erase(pool.begin() + i);
		Figure f;
		f.id = 0;
		f.kind = uint8_t(FigureKind::Candidate);
		f.archetype = uint8_t(a);
		f.culture = c->id;
		f.home = sid;
		std::string nm = names::person(c->lang, r);
		for (int tries = 0; tries < 3; tries++) {   // three different people (a real Names rarely repeats)
			bool dup = false;
			for (const Figure& o : out) if (o.name == nm) dup = true;
			if (!dup) break;
			nm = names::person(c->lang, r);
		}
		f.name = safeName(nm, "Asha");
		f.byname = safeName(names::epithet(c->lang, r), "the Quiet");
		const double age = a == MessengerArchetype::Child ? r.range(7, 13) : a == MessengerArchetype::Queen ? r.range(24, 52) : r.range(18, 62);
		f.born = w.year - age;
		f.credibility = finite01(t.archCred[size_t(int(a))]);
		f.reach = reachFor(w, c->id, int(a));
		// person: zeal, gentleness, cunning, pride; heart: integrity, humility, faith (hidden)
		const bool q = a == MessengerArchetype::Queen, cr = a == MessengerArchetype::Criminal;
		const bool ch = a == MessengerArchetype::Child, ub = a == MessengerArchetype::Unbelieved;
		f.person = { r01(r, ub ? 0.5 : 0.1, 1.0), r01(r, ch || a == MessengerArchetype::Shepherd ? 0.4 : 0.0, 1.0),
			r01(r, cr ? 0.5 : 0.0, cr ? 1.0 : 0.8), r01(r, q ? 0.4 : 0.0, q ? 1.0 : 0.7) };
		f.heart = { r01(r, cr ? 0.0 : 0.2, cr ? 0.8 : 1.0), float(clamp(0.5 * (1 - f.person[3]) + 0.5 * r.f(), 0.0, 1.0)), r01(r, 0.2, 1.0) };
		out.push_back(std::move(f));
	}
	return out;
}

// What the MessengerChoice card costs to answer with a candidate: the card's own data["cost"] when set (a
// "seek a voice" card carries askCost; Powers may set 0 if it charged on arming), else the Messenger power's cost.
double choiceCost(const Card& c) {
	auto it = c.data.find("cost");
	if (it != c.data.end() && isFinite(it->second)) return std::max(0.0, it->second);
	const powers::PowerDef* d = powers::byId("prophet");
	return d ? d->cost : 120;
}

const Card* cardOf(const World& w, int cardId) {
	for (const Card& c : w.mythos.cards) if (c.id == cardId) return &c;
	return nullptr;
}

// ---- rulers (3.23)

// A new ruler of culture cid at its capital. heirOf = the previous ruler when an heir succeeds (same house), -1 for
// a new house. Returns the figure id or -1.
int crown(World& w, int cid, int heirOf) {
	const Culture* c = liveCulture(w, cid);
	if (!c || !liveTown(w, c->capital)) return -1;
	const CharactersTune& t = tuneOf(w);
	const int idHint = std::max(1, w.mythos.nextId);
	Rng r = w.rng.fork("myth:characters:ruler:" + std::to_string(idHint) + ":" + std::to_string(yearInt(w)));
	Figure f;
	f.kind = uint8_t(FigureKind::Ruler);
	f.archetype = uint8_t(MessengerArchetype::None);
	f.culture = cid;
	f.home = c->capital;
	f.name = safeName(names::ruler(c->lang, r), "Asha");
	f.byname = safeName(names::epithet(c->lang, r), "the Quiet");
	f.born = w.year - r.range(heirOf >= 0 ? 16 : 22, heirOf >= 0 ? 40 : 50);
	f.credibility = 0.5;
	const Figure* parent = heirOf >= 0 ? figure(w, heirOf) : nullptr;
	for (int k = 0; k < 4; k++) {
		const double fresh = r.f();
		f.person[size_t(k)] = float(clamp(parent ? 0.5 * double(parent->person[size_t(k)]) + 0.5 * fresh : fresh, 0.0, 1.0));
	}
	f.heart = { r01(r, 0, 1), float(clamp(0.5 * (1 - f.person[3]) + 0.5 * r.f(), 0.0, 1.0)), r01(r, 0, 1) };
	const double lo = std::min(t.reignMin, t.reignMax), hi = std::max(t.reignMin, t.reignMax);
	const double reign = isFinite(lo) && isFinite(hi) ? r.range(std::max(1.0, lo), std::max(1.0, hi)) : 30;
	double house = parent ? getOr(parent->data, "house", -1) : -1;
	if (house < 0) {
		house = getOr(w.mythos.num, "characters_house", 0) + 1;
		w.mythos.num["characters_house"] = house;
	}
	f.data["house"] = house;
	f.data["from"] = w.year;
	f.data["until"] = w.year + reign;
	const int id = addFigure(w, std::move(f));
	if (id >= 0) if (const Figure* nf = figure(w, id)) emitFigure(w, *nf, "rise");
	return id;
}

// End a figure's life (no successor logic).
void endFigure(World& w, Figure& f, FigureFate fate) {
	f.fate = uint8_t(fate);
	f.died = w.year;
	f.trial = -1;
	emitFigure(w, f, fate == FigureFate::Died ? "died" : "fallen");
}

bool rulerQualifies(const World& w, int cid) {
	const Culture* c = liveCulture(w, cid);
	if (!c || c->age < Age::BRONZE) return false;
	const Settlement* cap = liveTown(w, c->capital);
	return cap && cap->culture == cid;
}

void rulersYearly(World& w) {
	const CharactersTune& t = tuneOf(w);
	for (int cid = 0; cid < int(w.cultures.size()); cid++) {
		const bool qualifies = rulerQualifies(w, cid);
		const int cur = ruler(w, cid);
		// every living ruler of this culture but the first is an error state (a bad save): retire them
		for (Figure& f : w.mythos.figures)
			if (isKind(f, FigureKind::Ruler) && living(f) && f.culture == cid && f.id != cur) endFigure(w, f, FigureFate::Died);
		int successorOf = -1;
		bool needNew = qualifies;
		if (cur >= 0) {
			Figure* f = findMut(w, cur);
			if (!qualifies) {
				endFigure(w, *f, FigureFate::Died);
				needNew = false;
			} else if (f->home != w.cultures[size_t(cid)].capital) {
				endFigure(w, *f, FigureFate::Died);              // the capital fell or was taken: a new house
			} else if (w.year >= getOr(f->data, "until", w.year)) {
				Rng r = w.rng.fork("myth:characters:heir:" + std::to_string(cur) + ":" + std::to_string(yearInt(w)));
				const bool heir = r.chance(clamp(t.heirShare, 0.0, 1.0));
				endFigure(w, *f, FigureFate::Died);
				if (heir) successorOf = cur;
			} else {
				needNew = false;                                   // the reign goes on
			}
		}
		if (needNew) crown(w, cid, successorOf);
	}
}

// ---- messengers (3.8)

// A Warning word through this figure in the last 30 years: the wrath was foretold.
bool foretold(const World& w, const Figure& f) {
	for (const Utterance& u : w.mythos.words)
		if (u.figure == f.id && u.content == uint8_t(WordContent::Warning) && u.year <= w.year && w.year - u.year <= 30) return true;
	return false;
}

bool actIn(const World& w, const MythAct& a, int sidA, int sidB) {
	if (a.settlement >= 0 && (a.settlement == sidA || a.settlement == sidB)) return true;
	if (w.valid(a.cell) && size_t(a.cell) < w.owner.size()) {
		const int o = int(w.owner[size_t(a.cell)]);
		if (o >= 0 && (o == sidA || o == sidB)) return true;
	}
	return false;
}

// A Judgement card for a figure's deed (3.11), re-proposed while it stands.
void proposeDeed(World& w, const Figure& f, int deed) {
	for (const Card& c : w.mythos.cards)
		if (c.kind == CardKind::Judgement && c.figure == f.id && c.opened >= getOr(f.data, "deedAt", 0)) return;   // already judged / open
	const Settlement* s = liveTown(w, f.home);
	cards::Proposal p;
	p.kind = CardKind::Judgement;
	p.culture = f.culture;
	p.figure = f.id;
	p.settlement = s ? f.home : -1;
	p.cell = s ? s->cell : -1;
	double world = 0;
	for (const Settlement& x : w.settlements) if (!x.dead && isFinite(x.pop) && x.pop > 0) world += x.pop;
	const double share = s && world > 0 ? s->pop / world * (s->capital ? 1.5 : 1.0) : 0;
	p.severity = clamp(0.5 + finite01(share) * 0.5, 0.5, 1.0);
	p.ref = { f.id, deed, -1, -1 };
	p.data["deed"] = deed;
	cards::propose(w, p);
}

void standingDeed(World& w, Figure& f) {
	const int deed = int(getOr(f.data, "deed", 0));
	if (deed <= 0) return;
	if (w.year - getOr(f.data, "deedAt", w.year) > kDeedWindow) { f.data.erase("deed"); return; }
	proposeDeed(w, f, deed);
}

void martyr(World& w, int figId, int byCulture) {
	Figure* f = findMut(w, figId);
	if (!f || !living(*f)) return;
	f->data["by"] = byCulture;
	if (byCulture == -1 && liveTown(w, f->home)) society::nudge(w, f->home, 0, -0.1);   // the god struck: love -0.1 there
	Legend seed;
	seed.culture = f->culture;
	seed.settlement = f->home;
	if (const Settlement* s = liveTown(w, f->home)) seed.cell = s->cell;
	seed.deed = "martyr";
	seed.truth = 1;
	seed.moral = uint8_t(LegendMoral::Sacrifice);
	seed.credit = uint8_t(LegendCredit::Messenger);
	legends::add(w, seed);
	f = findMut(w, figId);
	if (f && living(*f)) endFigure(w, *f, FigureFate::Martyred);
}

void messengersYearly(World& w, double actFrom) {
	const CharactersTune& t = tuneOf(w);
	const bool msgStance = w.mythos.presence == Presence::Messengers;
	// ids first: martyrdom and legends may touch the registry
	std::vector<int> ids;
	for (const Figure& f : w.mythos.figures) if (isKind(f, FigureKind::Messenger) && living(f)) ids.push_back(f.id);
	for (int id : ids) {
		Figure* f = findMut(w, id);
		if (!f || !living(*f)) continue;
		const Culture* c = (f->culture >= 0 && f->culture < int(w.cultures.size())) ? &w.cultures[size_t(f->culture)] : nullptr;
		if (!c || c->dead || !c->prophet || c->prophet->figure != f->id) { endFigure(w, *f, FigureFate::Died); continue; }
		const int arch = archIndex(*f);
		const MessengerArchetype a = MessengerArchetype(arch);
		const int here = c->prophet->settlement;
		double cred = f->credibility;
		const double chosen = getOr(f->data, "chosen", f->born);

		// the god's acts since the last pass, in their towns (one count per kind of act a year)
		std::set<std::string> good, bad;
		bool struckHome = false;
		for (const MythAct& act : w.mythos.acts) {
			if (act.natural || !(act.year >= actFrom) || !(act.year < w.year)) continue;
			if (!actIn(w, act, f->home, here)) continue;
			if (act.good > 0) good.insert(act.kind);
			else if (act.good < 0) {
				bad.insert(act.kind);
				if (actIn(w, act, f->home, -2)) struckHome = true;
			}
		}
		cred += double(good.size()) * t.credMiracle * (msgStance ? t.msgCredMul : 1.0);
		if (!bad.empty() && !foretold(w, *f)) cred += double(bad.size()) * t.credStruck;

		if (a == MessengerArchetype::Criminal) {
			const bool done = (f->flags & (FigureFlag::Redeemed | FigureFlag::Relapsed)) != 0;
			if (!done && !good.empty() && w.year - chosen <= t.redeemWindow) {
				f->flags |= FigureFlag::Redeemed;
				cred += t.criminalRedeem;
				f->fame += 1;
				f->credibility = finite01(cred);
				emitFigure(w, *f, "redeemed");
			} else if (!done && getOr(f->data, "relapseAt", -1) >= 0 && w.year >= getOr(f->data, "relapseAt", 0)) {
				f->flags |= FigureFlag::Relapsed | FigureFlag::Discredited;
				f->data.erase("relapseAt");
				f->data["deed"] = kDeedRelapse;
				f->data["deedAt"] = w.year;
				f->credibility = finite01(cred);
				emitFigure(w, *f, "discredited");
			}
			f = findMut(w, id);
			if (f && (f->flags & FigureFlag::Redeemed)) faiths::adjustHouse(w, f->culture, 0, 0, 0, -0.01);
			f = findMut(w, id);
			if (!f) continue;
		}
		if (a == MessengerArchetype::Queen && !(f->flags & FigureFlag::Proud) && getOr(f->data, "prideAt", -1) >= 0
			&& w.year >= getOr(f->data, "prideAt", 0)) {
			f->flags |= FigureFlag::Proud;
			f->data.erase("prideAt");
			f->data["deed"] = kDeedPride;
			f->data["deedAt"] = w.year;
		}
		if (a == MessengerArchetype::Child) {
			cred += t.childCredYear;
			const Settlement* home = (f->home >= 0 && f->home < int(w.settlements.size())) ? &w.settlements[size_t(f->home)] : nullptr;
			int by = -2;                                    // -2 nobody, -1 the god, -3 a war (razed), else the captors
			if (home && !home->dead && home->culture != f->culture) by = home->culture;
			else if (home && home->dead && home->cause == Cause::War) by = -3;
			else if (struckHome) by = -1;
			if (by != -2) {
				f->credibility = finite01(cred);
				martyr(w, id, by);
				continue;
			}
		}
		cred += t.credDrift * (t.archCred[size_t(arch)] - cred);
		f->credibility = finite01(cred);
		f->reach = reachFor(w, f->culture, arch);
		standingDeed(w, *f);
	}
	// the dead whose deed still stands (a relapsed messenger who died) are no longer judged
}

// ---- "they seek a voice" (3.8, Bronze+): the MessengerChoice card at askCost

void seekVoice(World& w) {
	const CharactersTune& t = tuneOf(w);
	double world = 0;
	for (const Settlement& s : w.settlements) if (!s.dead && isFinite(s.pop) && s.pop > 0) world += s.pop;
	for (int cid = 0; cid < int(w.cultures.size()); cid++) {
		const Culture& c = w.cultures[size_t(cid)];
		if (c.dead) continue;
		const std::string key = "characters_voice" + std::to_string(cid);
		if (c.prophet || messenger(w, cid) >= 0) { w.mythos.num[key] = w.year; continue; }
		if (c.age < Age::BRONZE) continue;
		const double last = std::max(c.born, getOr(w.mythos.num, key, c.born));
		if (!(w.year - last >= t.askYears)) continue;
		double pop = 0, dev = 0;
		int best = -1;
		for (const Settlement& s : w.settlements) {
			if (s.dead || s.culture != cid || !isFinite(s.pop) || !(s.pop > 0)) continue;
			pop += s.pop;
			dev += s.pop * (isFinite(s.devotion) ? s.devotion : 0);
			if (best < 0 || s.devotion > w.settlements[size_t(best)].devotion) best = s.id;
		}
		if (!(pop > 0) || dev / pop < t.askDevotion) continue;
		bool openAlready = false;
		for (const Card& k : w.mythos.cards)
			if (k.kind == CardKind::MessengerChoice && k.culture == cid && k.state == uint8_t(CardState::Open)) openAlready = true;
		if (openAlready) continue;
		const int sid = liveTown(w, c.capital) && w.settlements[size_t(c.capital)].culture == cid ? c.capital : best;
		if (sid < 0) continue;
		cards::Proposal p;
		p.kind = CardKind::MessengerChoice;
		p.culture = cid;
		p.settlement = sid;
		p.cell = w.settlements[size_t(sid)].cell;
		p.severity = world > 0 ? clamp(pop / world * (w.settlements[size_t(sid)].capital ? 1.5 : 1.0), 0.0, 1.0) : 0;
		p.ref = { sid, -1, -1, -1 };
		p.data["cost"] = std::max(0.0, t.askCost);
		p.data["asked"] = 1;
		p.data["decade"] = double(yearInt(w) / 10);
		cards::propose(w, p);
	}
}

} // namespace

// ------------------------------------------------------------------ public API

void yearly(World& w) {
	if (!on(w)) return;
	const double from = getOr(w.mythos.num, "characters_acts", w.year - 1);
	messengersYearly(w, from);
	rulersYearly(w);
	seekVoice(w);
	w.mythos.num["characters_acts"] = w.year;
}

std::vector<Figure> candidates(const World& w, int sid) {
	if (!on(w)) return {};
	return candidatesAt(w, sid, yearInt(w) / 10);
}

namespace {
int chooseAt(World& w, int sid, int index, int64_t decade) {
	if (!on(w)) return -1;
	std::vector<Figure> cs = candidatesAt(w, sid, decade);
	if (index < 0 || index >= int(cs.size())) return -1;
	const Settlement* s = liveTown(w, sid);
	if (!s) return -1;
	const int cid = s->culture;
	const Culture* c = liveCulture(w, cid);
	if (!c || c->prophet || messenger(w, cid) >= 0) return -1;
	Figure f = cs[size_t(index)];
	f.kind = uint8_t(FigureKind::Messenger);
	f.data["chosen"] = w.year;
	const int id = addFigure(w, std::move(f));
	if (id < 0) return -1;
	if (!society::raiseProphet(w, sid, society::RaiseOpts{ id })) {
		// refused: undo (nothing else saw the id); a pruned dead figure stays pruned (harmless)
		auto it = std::find_if(w.mythos.figures.begin(), w.mythos.figures.end(), [id](const Figure& x) { return x.id == id; });
		if (it != w.mythos.figures.end()) w.mythos.figures.erase(it);
		if (w.mythos.nextId == id + 1) w.mythos.nextId = id;
		return -1;
	}
	const CharactersTune& t = tuneOf(w);
	Figure* nf = findMut(w, id);
	if (!nf) return -1;
	Rng r = w.rng.fork("myth:characters:risk:" + std::to_string(id) + ":" + std::to_string(yearInt(w)));
	const MessengerArchetype a = MessengerArchetype(archIndex(*nf));
	if (a == MessengerArchetype::Criminal && r.chance(clamp(t.criminalRelapse, 0.0, 1.0)))
		nf->data["relapseAt"] = w.year + r.range(1, std::max(1.0, t.criminalWindow));
	if (a == MessengerArchetype::Queen && r.chance(clamp(t.queenPride, 0.0, 1.0)))
		nf->data["prideAt"] = w.year + r.range(3, std::max(3.0, std::min(30.0, t.prophetYears)));
	w.mythos.num["characters_voice" + std::to_string(cid)] = w.year;
	emitFigure(w, *nf, "chosen");
	if (a == MessengerArchetype::Queen) faiths::adjustHouse(w, cid, 0, 0, t.queenAuthority, 0);
	return id;
}
} // namespace

int choose(World& w, int sid, int index) { return chooseAt(w, sid, index, yearInt(w) / 10); }

int spawn(World& w, const Figure& proto) {
	if (!on(w)) return -1;
	const Culture* c = (proto.culture >= 0 && proto.culture < int(w.cultures.size())) ? &w.cultures[size_t(proto.culture)] : nullptr;
	if (!c) return -1;
	if (proto.home < -1 || proto.home >= int(w.settlements.size())) return -1;
	if (proto.kind >= uint8_t(enumCount<FigureKind>()) || proto.archetype >= uint8_t(enumCount<MessengerArchetype>())) return -1;
	Figure f = proto;
	f.fate = uint8_t(FigureFate::Living);
	f.died = -1;
	f.trial = -1;
	const std::string lang = c->lang;
	if (f.name.empty()) {
		Rng r = w.rng.fork("myth:characters:name:" + std::to_string(w.mythos.nextId) + ":" + std::to_string(yearInt(w)));
		f.name = safeName(isKind(f, FigureKind::Ruler) ? names::ruler(lang, r) : names::person(lang, r), "Asha");
	}
	for (float& x : f.person) if (!std::isfinite(x)) x = 0.5f; else x = std::clamp(x, 0.0f, 1.0f);
	for (float& x : f.heart) if (!std::isfinite(x)) x = 0.5f; else x = std::clamp(x, 0.0f, 1.0f);
	const int id = addFigure(w, std::move(f));
	if (id >= 0) if (const Figure* nf = figure(w, id)) emitFigure(w, *nf, "rise");
	return id;
}

bool fell(World& w, int figureId, FigureFate fate) {
	if (!on(w) || fate == FigureFate::Living || uint8_t(fate) >= uint8_t(enumCount<FigureFate>())) return false;
	Figure* f = findMut(w, figureId);
	if (!f || !living(*f)) return false;
	const bool wasRuler = isKind(*f, FigureKind::Ruler);
	const int cid = f->culture;
	endFigure(w, *f, fate);
	if (wasRuler && rulerQualifies(w, cid) && ruler(w, cid) < 0) crown(w, cid, -1);   // a removed ruler: a new house, this year
	return true;
}

int ruler(const World& w, int cultureId) {
	for (const Figure& f : w.mythos.figures)
		if (f.culture == cultureId && f.kind == uint8_t(FigureKind::Ruler) && f.fate == uint8_t(FigureFate::Living)) return f.id;
	return -1;
}
int messenger(const World& w, int cultureId) {
	for (const Figure& f : w.mythos.figures)
		if (f.culture == cultureId && f.kind == uint8_t(FigureKind::Messenger) && f.fate == uint8_t(FigureFate::Living)) return f.id;
	return -1;
}
double credibility(const World& w, int figureId) {
	for (const Figure& f : w.mythos.figures) if (f.id == figureId) return f.credibility;
	return 0.5;
}

bool adjustCredibility(World& w, int figureId, double delta) {
	if (!on(w) || !isFinite(delta)) return false;
	Figure* f = findMut(w, figureId);
	if (!f) return false;
	f->credibility = finite01(f->credibility + delta);
	const CharactersTune& t = tuneOf(w);
	if (isKind(*f, FigureKind::Messenger) && f->archetype == uint8_t(MessengerArchetype::Unbelieved)
		&& !(f->flags & FigureFlag::Vindicated) && f->credibility >= t.vindicated) {
		// vindicated (even after death: "too late to live in"): quoted for vindicatedYears, a legend
		f->flags |= FigureFlag::Vindicated;
		f->data["quoted"] = w.year + t.vindicatedYears;
		f->fame += 2;
		emitFigure(w, *f, "vindicated");
		Legend seed;
		seed.culture = f->culture;
		seed.settlement = liveTown(w, f->home) ? f->home : -1;
		if (const Settlement* s = liveTown(w, f->home)) seed.cell = s->cell;
		seed.deed = "vindicated";
		seed.truth = 1;
		seed.moral = uint8_t(LegendMoral::Warning);
		seed.credit = uint8_t(LegendCredit::Messenger);
		legends::add(w, seed);
	}
	return true;
}

const Figure* figure(const World& w, int id) {
	for (const Figure& f : w.mythos.figures) if (f.id == id) return &f;
	return nullptr;
}

// Trials are v2 (track M4b): neutral until then.
bool test(World& w, int figureId, TrialKind kind) {
	(void)w; (void)figureId; (void)kind;
	return false;
}
double trialOdds(const World& w, int figureId, TrialKind kind) {
	(void)w; (void)figureId; (void)kind;
	return 0.0;
}

// ---- card handlers: MessengerChoice (choices 0..2 = the candidates, 3 = none). Trial (v2) stays neutral.

bool answer(World& w, int cardId, int choice) {
	if (!on(w)) return false;
	const Card* c = cardOf(w, cardId);
	if (!c || c->kind != CardKind::MessengerChoice || c->state != uint8_t(CardState::Open)) return false;
	if (choice == 3) return true;                                   // none: "they will wait for a voice"
	if (choice < 0 || choice > 2) return false;
	const int sid = c->settlement;
	auto it = c->data.find("decade");
	const int64_t decade = it != c->data.end() && isFinite(it->second) ? int64_t(it->second) : int64_t(std::floor(c->opened)) / 10;
	return chooseAt(w, sid, choice, decade) >= 0;
}

void lapse(World& w, int cardId) {
	(void)w; (void)cardId;                                          // MessengerChoice lapses to "none": nothing happens
}

std::vector<CardOption> options(const World& w, int cardId) {
	std::vector<CardOption> out;
	if (!on(w)) return out;
	const Card* c = cardOf(w, cardId);
	if (!c || c->kind != CardKind::MessengerChoice) return out;
	auto it = c->data.find("decade");
	const int64_t decade = it != c->data.end() && isFinite(it->second) ? int64_t(it->second) : int64_t(std::floor(c->opened)) / 10;
	const std::vector<Figure> cs = candidatesAt(w, c->settlement, decade);
	const Settlement* s = liveTown(w, c->settlement);
	const Culture* cu = s ? liveCulture(w, s->culture) : nullptr;
	const bool vacant = cu && !cu->prophet && messenger(w, cu->id) < 0;
	const double cost = choiceCost(*c);
	for (int i = 0; i < 3; i++) {
		CardOption o;
		o.choice = i;
		o.cost = cost;
		if (i >= int(cs.size()) || !vacant) o.why = OptionWhy::Unavailable;
		else if (w.god.awe < cost) o.why = OptionWhy::Awe;
		else o.why = OptionWhy::Ok;
		o.ok = o.why == OptionWhy::Ok;
		out.push_back(o);
	}
	CardOption none;
	none.choice = 3;
	none.cost = 0;
	none.ok = true;
	none.why = OptionWhy::Ok;
	out.push_back(none);
	return out;
}

} // namespace dm::characters
