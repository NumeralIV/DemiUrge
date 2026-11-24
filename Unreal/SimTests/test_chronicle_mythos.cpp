// DEMIURGE — SimTests/test_chronicle_mythos.cpp : the mythos chronicle (SPEC-MYTHOS 3.31, v1 entry types; track M8
// "chronicle-mythos"). Synthetic events on a helper world, so it runs with every other module stubbed
// (run.ps1 chronicle) and with the real ones (run.ps1 all). The real-history lint is test_chronicle_mythos_histories.cpp.
//
//   - phase 0: no mythos line, no vars.num key, the old words ("prophet") kept: the chronicle is the pre-mythos one
//   - presence, aspect (only for a touched people or under emergent), decrees of every area x stance x clarity, rites,
//     repeals, customs, breaches (and their digest), every outcome of the Word (one entry with the decree it gave)
//   - messengers: chosen by archetype, redeemed, discredited, vindicated, martyred; no line for rulers; the prophet's
//     own line is not doubled; the name migration (messenger / seer / voice by culture family)
//   - judgements by deed and heart x every sentence, their doctrine tail once, the survivors' line once
//   - card resolutions (shaping, presence, no messenger, every decree conflict case, competing prayers, lapses only
//     where they act); never two entries for one card
//   - the mythos budget (<= 1.2 notable entries per real minute); the aspect epithets; the tale's belief and legacy
//     paragraphs (5 legacies x 6 moods); the lint on everything written
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Cards.h"
#include "Chronicle.h"
#include "Mythos.h"
#include "State.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "test_chronicle_mythos_lint.h"

using namespace dm;
using namespace dmt;

namespace {

void say(const std::string& s) { std::printf("  %s\n", s.c_str()); std::fflush(stdout); }

struct M {
	std::unique_ptr<World> w;
	std::vector<int> land;
	size_t next = 0;
};

const char* const kNames[] = { "Vornhal", "Tel Asha", "Qe-Luun", "Imbrek", "Sarnath", "Ulu Kesh", "Dorrow", "Mirathe", "Kesh Amun", "Oduya",
	"Brennick", "Tamsin Var", "Ilo", "Haddath", "Zuneri", "Penkarrow", "Asel", "Umbrin", "Garrowmere", "Nish" };

M fresh(uint32_t seed, int phase = 1, bool budget = true) {
	M v;
	v.w = makeWorld({ .L = 5, .seed = seed, .land = 0.4 });
	World& w = *v.w;
	for (int c = 0; c < w.n; c++) if (!state::isSea(w, c) && w.hab[size_t(c)] > 0.05f) {
		bool far = true;
		for (int o : v.land) if (w.geo->angle(o, c) < 0.08) { far = false; break; }
		if (far) v.land.push_back(c);
		if (v.land.size() >= 60) break;
	}
	w.tune<ChronicleTune>().maxEntries = 100000;
	w.tune<ChronicleTune>().budget = budget;
	w.tune<MythosTune>().phase = phase;
	w.phase = Phase::History;
	w.year = 100;
	chronicle::attach(w);
	return v;
}

int culture(M& v, const char* name, const char* adj, const char* plural, const char* god, const char* lang, int parent = -1) {
	World& w = *v.w;
	Culture& c = state::newCulture(w, parent);
	c.name = name; c.adj = adj; c.plural = plural; c.godName = god; c.lang = lang;
	const int id = c.id;
	while (w.mythos.faith.size() < w.cultures.size()) w.mythos.faith.push_back(Faith{});
	return id;
}
int town(M& v, int cid, double pop, int tier) {
	World& w = *v.w;
	Settlement& s = state::newSettlement(w, v.land[v.next++ % v.land.size()], cid);
	s.name = kNames[s.id % 20];
	s.pop = pop;
	s.tier = tier;
	const int id = s.id;
	if (Culture* c = state::culture(w, cid)) if (c->capital < 0) { c->capital = id; w.settlements[size_t(id)].capital = true; }
	return id;
}
void years(World& w, double y) { w.year += y; w.tick += int64_t(std::lround(y * 4)) + 1; }

int since(const World& w, int first, const std::string& type = std::string(), int weight = 0) {
	int n = 0;
	for (const ChronicleEntry& e : w.chronicle)
		n += e.id >= first && (type.empty() || e.type == type) && (weight == 0 || e.weight == weight);
	return n;
}
const ChronicleEntry* lastOf(const World& w, const std::string& type) {
	for (size_t i = w.chronicle.size(); i-- > 0;) if (w.chronicle[i].type == type) return &w.chronicle[i];
	return nullptr;
}
bool has(const std::string& t, const std::string& s) { return t.find(s) != std::string::npos; }

// Every entry of w since `first` passes the lint; prints the first few faults.
int lintAll(const World& w, int first, const std::set<std::string>& extra = {}) {
	const std::vector<std::string> nm = m8lint::names(w, extra);
	int faults = 0;
	for (const ChronicleEntry& e : w.chronicle) {
		if (e.id < first) continue;
		const std::string f = m8lint::fault(e, nm);
		if (!f.empty()) { if (faults < 6) say(f + ": [" + e.type + "] " + e.text); faults++; }
	}
	return faults;
}

int figure(World& w, int cid, int home, MessengerArchetype a, const char* name, FigureKind kind = FigureKind::Messenger) {
	Figure f;
	f.id = w.mythos.nextId++;
	f.kind = uint8_t(kind);
	f.archetype = uint8_t(a);
	f.culture = cid;
	f.home = home;
	f.name = name;
	f.born = w.year - 30;
	f.credibility = 0.5;
	w.mythos.figures.push_back(f);
	return f.id;
}
int card(World& w, CardKind k, int cid, int sid, std::array<int, 4> ref = { -1, -1, -1, -1 }, int fig = -1, int other = -1) {
	Card c;
	c.id = w.mythos.nextId++;
	c.kind = k;
	c.state = uint8_t(CardState::Open);
	c.culture = cid; c.settlement = sid; c.figure = fig; c.other = other;
	c.cell = sid >= 0 ? w.settlements[size_t(sid)].cell : -1;
	c.opened = w.year;
	c.ref = ref;
	w.mythos.cards.push_back(c);
	return c.id;
}
Card* cardOf(World& w, int id) {
	for (Card& c : w.mythos.cards) if (c.id == id) return &c;
	return nullptr;
}
void answerCard(World& w, int id, int choice) {
	Card* c = cardOf(w, id);
	if (!c) return;
	c->state = uint8_t(CardState::Answered);
	c->choice = int8_t(std::min(choice, 127));
	w.bus.emit(EvCard{ id, toStr(c->kind), "answered", c->culture, choice });
}
void lapseCard(World& w, int id) {
	Card* c = cardOf(w, id);
	if (!c) return;
	c->state = uint8_t(CardState::Lapsed);
	w.bus.emit(EvCard{ id, toStr(c->kind), "lapsed", c->culture, c->lapse });
}
void touch(World& w, int cid) { w.mythos.faith[size_t(cid)].data["m_touched"] = 1; }

struct Cast { int um = -1, va = -1, te = -1, cap = -1, vcap = -1, tcap = -1; };
Cast cast(M& v) {
	Cast k;
	k.um = culture(v, "Umar", "Umaran", "the Umari", "Ashvel", "lang-umar");
	k.va = culture(v, "Vael", "Vaelish", "the Vaeli", "Orro-Ka", "lang-vael");
	k.te = culture(v, "Teshk", "Teshkan", "the Teshki", "Imu", "lang-teshk");
	k.cap = town(v, k.um, 3000, 3);
	k.vcap = town(v, k.va, 2000, 3);
	k.tcap = town(v, k.te, 1500, 2);
	for (int i = 0; i < 4; i++) { town(v, k.um, 600, 2); town(v, k.va, 500, 2); }
	return k;
}

} // namespace

// ===================================================================== guardrail

DM_TEST("chronicle mythos: phase 0 writes no mythos line, sets no key and keeps the old words") {
	M v = fresh(201, 0);
	World& w = *v.w;
	Cast k = cast(v);
	const int f = figure(w, k.um, k.cap, MessengerArchetype::Shepherd, "Ossa");
	const int cd = card(w, CardKind::DecreeConflict, k.um, k.cap, { 0, k.va, k.cap, -1 }, -1, k.va);
	const NumBag before = w.vars.num;
	const int first = w.nextId.entry;
	w.bus.emit(EvPresence{ "unseen", "wonders" });
	w.bus.emit(EvAspect{ k.um, "ruler", "unknown" });
	w.bus.emit(EvDecree{ k.um, "violence", "peace", "none", "exact" });
	w.bus.emit(EvDecree{ k.va, "hospitality", "guarded", "none", "custom" });
	w.bus.emit(EvBreach{ k.um, "violence", k.cap, -1, 0.7 });
	w.bus.emit(EvWord{ 5, k.um, "answer", "dream", "dream", "heard" });
	w.bus.emit(EvFigure{ f, "messenger", k.um, k.cap, "chosen" });
	w.bus.emit(EvJudgement{ cd, k.um, "heart", "forgive" });
	answerCard(w, cd, 0);
	eq(since(w, first), 0, "no entry for any mythos event at phase 0");
	ok(w.vars.num == before, "no vars.num key written");
	// the prophet keeps its old name at phase 0 (and only there)
	bool prophet0 = false;
	for (int i = 0; i < 12; i++) {
		w.cultures[size_t(k.um)].prophet.reset();
		w.bus.emit(EvProphetRisen{ k.um, k.cap, "Dovak" });
		w.bus.emit(EvProphetDied{ k.um, "Dovak" });
		years(w, 30);
	}
	for (const ChronicleEntry& e : w.chronicle) if (e.id >= first && m8lint::hasWord(m8lint::lower(e.text), "prophet")) prophet0 = true;
	ok(prophet0, "phase 0: the prophet lines are today's");
	// phase 1 and a god who never touched the mythos: today's words still (guardrail 5.1 rule 2)
	{
		M vq = fresh(201, 1);
		World& wq = *vq.w;
		Cast kq = cast(vq);
		const int fq = wq.nextId.entry;
		bool prophetQ = false;
		for (int i = 0; i < 12; i++) {
			wq.bus.emit(EvProphetRisen{ kq.um, kq.cap, "Dovak" });
			wq.bus.emit(EvProphetDied{ kq.um, "Dovak" });
			years(wq, 30);
			chronicle::updateEpithets(wq);
		}
		for (const ChronicleEntry& e : wq.chronicle) if (e.id >= fq && m8lint::hasWord(m8lint::lower(e.text), "prophet")) prophetQ = true;
		ok(prophetQ, "phase 1, untouched: the prophet lines are today's");
		eq(wq.vars.num.count("chronicle_mv"), size_t(0), "and the book was not re-voiced");
		// the god's first mythos act re-voices the whole book once
		touch(wq, kq.va);
		chronicle::updateEpithets(wq);
		bool left = false;
		for (const ChronicleEntry& e : wq.chronicle) left |= m8lint::hasWord(m8lint::lower(e.text), "prophet") || m8lint::hasWord(m8lint::lower(e.text), "disciples");
		ok(!left, "after the first mythos act no entry of the book says prophet or disciples");
		eq(wq.vars.num.count("chronicle_mv"), size_t(1), "re-voiced once");
	}
	// a god who engaged the mythos: the people's own word, never "prophet"
	M v1 = fresh(201, 1);
	World& w1 = *v1.w;
	Cast k1 = cast(v1);
	touch(w1, k1.te);
	const int f1 = w1.nextId.entry;
	for (int i = 0; i < 12; i++) {
		w1.bus.emit(EvProphetRisen{ k1.um, k1.cap, "Dovak" });
		w1.bus.emit(EvProphetDied{ k1.um, "Dovak" });
		years(w1, 30);
	}
	int named = 0;
	const std::string word = "messenger seer voice";
	for (const ChronicleEntry& e : w1.chronicle) {
		if (e.id < f1) continue;
		ok(!m8lint::hasWord(m8lint::lower(e.text), "prophet") && !m8lint::hasWord(m8lint::lower(e.text), "disciples"), "no prophet at phase 1: " + e.text);
		named += m8lint::hasWord(m8lint::lower(e.text), "messenger") || m8lint::hasWord(m8lint::lower(e.text), "seer") || m8lint::hasWord(m8lint::lower(e.text), "voice");
	}
	ok(named >= 4, "the migrated lines name a messenger, seer or voice (" + std::to_string(named) + ")");
	eq(lintAll(w1, f1, { "Dovak" }), 0, "the lint");
}

DM_TEST("chronicle mythos: the messenger's word is the culture family's, and a daughter people keeps it") {
	M v = fresh(202);
	World& w = *v.w;
	std::map<std::string, int> seen;
	for (int i = 0; i < 9; i++) {
		const std::string lang = "lang-" + std::to_string(i * 37 + 5);
		const int c = culture(v, "Umar", "Umaran", "the Umari", "Ashvel", lang.c_str());
		const int d = culture(v, "Vael", "Vaelish", "the Vaeli", "Orro-Ka", (lang + "-drift").c_str(), c);
		const int g = culture(v, "Teshk", "Teshkan", "the Teshki", "Imu", (lang + "-far").c_str(), d);
		town(v, c, 1000, 2); town(v, d, 800, 2); town(v, g, 700, 2);
		touch(w, c);                                         // the god has engaged the mythos: the neutral words
		// the god's epithet for prophets, read in each people's word
		for (int x : { c, d, g }) w.cultures[size_t(x)].seen["prophet"] = 6;
		chronicle::updateEpithets(w);
		const std::string ec = w.cultures[size_t(c)].epithet;
		ok(!has(ec, "Prophet") && !has(ec, "Heaven"), "neutral epithet: " + ec);
		std::string wc, wd;
		// the word itself, through a prophet's line (cultures c and its granddaughter g)
		auto wordOf = [&](int cid) {
			for (int tries = 0; tries < 8; tries++) {
				const int f0 = w.nextId.entry;
				w.bus.emit(EvProphetDied{ cid, "Ithel" });
				years(w, 40);
				for (const ChronicleEntry& e : w.chronicle) {
					if (e.id < f0) continue;
					const std::string t = m8lint::lower(e.text);
					for (const char* x : { "messenger", "seer", "voice" }) if (m8lint::hasWord(t, x)) return std::string(x);
				}
			}
			return std::string();
		};
		wc = wordOf(c);
		wd = wordOf(g);
		if (!wc.empty() && !wd.empty()) eq(wd, wc, "a granddaughter people keeps its family's word");
		if (!wc.empty()) seen[wc]++;
	}
	std::string s;
	for (const auto& kv : seen) s += kv.first + " x" + std::to_string(kv.second) + " ";
	say("families' words: " + s);
	ok(seen.size() >= 2, "different families use different words");
}

// ===================================================================== presence and aspect

DM_TEST("chronicle mythos: presence lines per stance; aspect lines only for a touched people or under emergent") {
	M v = fresh(203);
	World& w = *v.w;
	Cast k = cast(v);
	int first = w.nextId.entry;
	for (const char* p : { "messengers", "unseen", "wonders" }) {
		w.bus.emit(EvPresence{ p, "wonders" });
		years(w, 50);
	}
	w.bus.emit(EvPresence{ "walk", "wonders" });
	eq(since(w, first, "presence"), 3, "one line per stance change (Walk is never one)");
	const ChronicleEntry* e = nullptr;
	for (const ChronicleEntry& x : w.chronicle) if (x.id >= first && x.type == "presence") { if (!e) e = &x; say(x.text); }
	if (ok(e != nullptr, "presence told")) eq(e->weight, 2, "notable (while the mythos bucket has a token)");
	// aspect: untouched -> nothing
	first = w.nextId.entry;
	w.mythos.faith[size_t(k.um)].aspect = Aspect::Ruler;
	w.bus.emit(EvAspect{ k.um, "ruler", "unknown" });
	eq(since(w, first, "aspect"), 0, "an untouched people's view is not news (guardrail 5.1 rule 2)");
	touch(w, k.um);
	for (const char* a : { "parent", "ruler", "mystery", "threat" }) { w.bus.emit(EvAspect{ k.um, a, "unknown" }); years(w, 45); }
	eq(since(w, first, "aspect"), 4, "a touched people: one line per change");
	w.tune<MythosTune>().emergent = true;
	w.bus.emit(EvAspect{ k.va, "mystery", "unknown" });
	eq(since(w, first, "aspect"), 5, "under emergent every people's view is told");
	eq(lintAll(w, 0), 0, "the lint");
}

DM_TEST("chronicle mythos: aspect epithets when no deed stands out; the absent names under the Unseen stance") {
	M v = fresh(204);
	World& w = *v.w;
	Cast k = cast(v);
	const char* const kFam[4][3] = { { "the Keeper of the Hearth", "the One Who Feeds Us", "the Hearth-Fire" },
		{ "the Lawgiver", "the Judge Above", "the High Seat" }, { "the Unseen", "the Hidden One", "the Veiled Sky" },
		{ "the Storm Over Us", "the Dread Above", "the Watching Fire" } };
	chronicle::updateEpithets(w);
	const std::string plain = w.cultures[size_t(k.um)].epithet;
	w.mythos.faith[size_t(k.um)].aspect = Aspect::Ruler;
	years(w, 50); chronicle::updateEpithets(w);
	eq(w.cultures[size_t(k.um)].epithet, plain, "untouched: the old name");
	for (int a = 1; a <= 4; a++) {
		touch(w, k.um);
		w.mythos.faith[size_t(k.um)].aspect = Aspect(a);
		years(w, 50);
		chronicle::updateEpithets(w);
		const std::string ep = w.cultures[size_t(k.um)].epithet;
		bool in = false;
		for (const char* n : kFam[a - 1]) in |= ep == n;
		ok(in, std::string("aspect ") + toStr(Aspect(a)) + " names the god " + ep);
	}
	// Unseen: deeds read as fortune
	w.cultures[size_t(k.va)].seen["rain"] = 40;
	years(w, 50); chronicle::updateEpithets(w);
	const std::string rainName = w.cultures[size_t(k.va)].epithet;
	w.mythos.presence = Presence::Unseen;
	w.cultures[size_t(k.va)].seen["rain"] = 80;
	years(w, 50); chronicle::updateEpithets(w);
	const std::string unseen = w.cultures[size_t(k.va)].epithet;
	say("rain-giver under Wonders: " + rainName + "; under Unseen: " + unseen);
	ok(unseen != rainName, "under Unseen the rain no longer names the god");
	ok(unseen == "the Silent One" || unseen == "the Sleeper" || unseen == "the Empty Chair" || unseen == "the Rumour", "the absent family: " + unseen);
	eq(lintAll(w, 0), 0, "the lint");
}

// ===================================================================== decrees, customs, breaches

DM_TEST("chronicle mythos: every area x stance x clarity tells its decree; rites, repeals and customs") {
	M v = fresh(205, 1, false);
	World& w = *v.w;
	Cast k = cast(v);
	const int msg = figure(w, k.um, k.cap, MessengerArchetype::Queen, "Teodo");
	const int first = w.nextId.entry;
	int n = 0;
	for (int ai = 0; ai < int(DecreeArea::Count); ai++) {
		const DecreeArea a = DecreeArea(ai);
		for (int s = 0; s < stanceCount(a); s++) {
			for (int cl = 0; cl < 4; cl++) {
				// half of the exact words through a messenger
				w.mythos.decrees.clear();
				w.mythos.decrees.push_back(Decree{ k.um, a, int8_t(s), Clarity(cl), w.year, 0.8, (cl == 0 && s % 2 == 0) ? msg : -1, false, {} });
				const int f0 = w.nextId.entry;
				w.bus.emit(EvDecree{ k.um, toStr(a), stanceName(a, s), "none", toStr(Clarity(cl)) });
				const ChronicleEntry* e = lastOf(w, "decree");
				if (!ok(since(w, f0, "decree") == 1 && e, std::string("told: ") + toStr(a) + " " + stanceName(a, s) + " " + toStr(Clarity(cl)))) continue;
				if (a == DecreeArea::Worship) ok(has(e->text, "worship"), "a rite says how they worship: " + e->text);
				if (n++ < 6 || (a == DecreeArea::Worship && cl == 1)) say(e->text);
				years(w, 11);
			}
			// the custom
			const int f1 = w.nextId.entry;
			w.bus.emit(EvDecree{ k.va, toStr(a), stanceName(a, s), "none", "custom" });
			ok(since(w, f1, "custom") == 1, std::string("custom told: ") + toStr(a) + " " + stanceName(a, s));
			years(w, 11);
		}
		const int f2 = w.nextId.entry;
		w.bus.emit(EvDecree{ k.um, toStr(a), "repealed", stanceName(a, 0), "parable" });
		eq(since(w, f2, "decree"), 1, std::string("repeal told: ") + toStr(a));
		years(w, 11);
	}
	const ChronicleEntry* c = lastOf(w, "custom");
	if (c) { say(c->text); eq(c->weight, 1, "a custom is a minor entry"); }
	const ChronicleEntry* d = nullptr;
	for (const ChronicleEntry& e : w.chronicle) if (e.id >= first && e.type == "decree" && has(e.text, "shed no blood")) { d = &e; break; }
	ok(d != nullptr, "the stance in the people's words ('shed no blood')");
	eq(lintAll(w, first, { "Teodo" }), 0, "the lint");
	ok(m8lint::maxBodyUse(w) > 0, "mythos template bodies are counted");
}

DM_TEST("chronicle mythos: breaches: severe ones notable, small ones minor, a flood of them folds into the digest") {
	M v = fresh(206);
	World& w = *v.w;
	Cast k = cast(v);
	int first = w.nextId.entry;
	w.bus.emit(EvBreach{ k.um, "violence", k.cap, -1, 0.7 });
	const ChronicleEntry* e = lastOf(w, "breach");
	if (ok(e != nullptr, "severe breach told")) { say(e->text); eq(e->weight, 2, "a severe breach is notable"); }
	years(w, 12);
	w.bus.emit(EvBreach{ k.um, "care", k.cap + 1, -1, 0.2 });
	e = lastOf(w, "breach");
	if (ok(e != nullptr && e->id >= first, "small breach told")) eq(e->weight, 1, "a small breach is minor");
	// thirty small breaches in one decade
	years(w, 12);
	first = w.nextId.entry;
	for (int i = 0; i < 30; i++) { w.bus.emit(EvBreach{ k.va, i % 2 ? "wealth" : "honesty", k.vcap, -1, 0.1 }); years(w, 0.25); }
	const int told = since(w, first, "breach");
	say("30 breaches in a decade: " + std::to_string(told) + " written");
	ok(told <= 3, "they fold");
	years(w, 40);
	chronicle::updateEpithets(w);
	const ChronicleEntry* d = lastOf(w, "digest");
	if (ok(d != nullptr && d->id >= first, "the digest tells them")) { say(d->text); ok(has(d->text, "broke") || has(d->text, "broken"), "as broken decrees"); }
	eq(lintAll(w, 0), 0, "the lint");
}

// ===================================================================== the Word

DM_TEST("chronicle mythos: every outcome of the Word is one entry, with the decree it gave") {
	M v = fresh(207, 1, false);
	World& w = *v.w;
	Cast k = cast(v);
	const int msg = figure(w, k.um, k.cap, MessengerArchetype::Shepherd, "Ossa");
	struct Case { WordContent content; Clarity cl; WordOutcome out; DecreeArea area; int meant, heard; };
	const Case cases[] = {
		{ WordContent::Decree, Clarity::Exact, WordOutcome::Heard, DecreeArea::Violence, 0, 0 },
		{ WordContent::Decree, Clarity::Exact, WordOutcome::Literal, DecreeArea::Violence, 0, 0 },
		{ WordContent::Decree, Clarity::Exact, WordOutcome::Catastrophe, DecreeArea::Hospitality, 2, 2 },
		{ WordContent::Decree, Clarity::Parable, WordOutcome::Wisdom, DecreeArea::Care, 0, 1 },
		{ WordContent::Decree, Clarity::Parable, WordOutcome::Sect, DecreeArea::Wealth, 0, 0 },
		{ WordContent::Decree, Clarity::Dream, WordOutcome::Personal, DecreeArea::Honesty, 0, 0 },
		{ WordContent::Decree, Clarity::Dream, WordOutcome::Rival, DecreeArea::Honesty, 1, 1 },
		{ WordContent::Decree, Clarity::Dream, WordOutcome::Catastrophe, DecreeArea::Care, 0, -1 },
		{ WordContent::Decree, Clarity::Sign, WordOutcome::HouseRead, DecreeArea::Hospitality, 1, 2 },
		{ WordContent::Decree, Clarity::Sign, WordOutcome::Rival, DecreeArea::Worship, 2, 2 },
		{ WordContent::Decree, Clarity::Sign, WordOutcome::Catastrophe, DecreeArea::Wealth, 1, -1 },
		{ WordContent::Repeal, Clarity::Dream, WordOutcome::Heard, DecreeArea::Violence, -1, -1 },
		{ WordContent::Repeal, Clarity::Sign, WordOutcome::Catastrophe, DecreeArea::Care, -1, -1 },
		{ WordContent::Answer, Clarity::Exact, WordOutcome::Heard, DecreeArea::Count, -1, -1 },
		{ WordContent::Answer, Clarity::Parable, WordOutcome::Wisdom, DecreeArea::Count, -1, -1 },
		{ WordContent::Answer, Clarity::Dream, WordOutcome::Personal, DecreeArea::Count, -1, -1 },
		{ WordContent::Answer, Clarity::Sign, WordOutcome::HouseRead, DecreeArea::Count, -1, -1 },
	};
	const int first = w.nextId.entry;
	for (const Case& c : cases) {
		Utterance u;
		u.id = w.mythos.nextId++;
		u.culture = k.um;
		u.figure = (c.cl == Clarity::Exact || c.cl == Clarity::Parable) ? msg : -1;
		u.content = uint8_t(c.content); u.clarity = c.cl; u.year = w.year; u.outcome = uint8_t(c.out);
		u.meant = int8_t(c.meant); u.heard = int8_t(c.heard);
		u.area = c.area == DecreeArea::Count ? int8_t(-1) : int8_t(c.area);
		if (c.cl == Clarity::Dream) u.data["dreamer"] = k.cap + 1;
		w.mythos.words.push_back(u);
		const int f0 = w.nextId.entry;
		w.bus.emit(EvWord{ u.id, k.um, toStr(c.content), toStr(c.cl), c.cl == Clarity::Sign ? "sky" : "messenger", toStr(c.out) });
		// Doctrine's decree event follows a decree word that took (and a repeal that did)
		if (c.content == WordContent::Decree && c.heard >= 0) w.bus.emit(EvDecree{ k.um, toStr(c.area), stanceName(c.area, c.heard), "none", toStr(c.cl) });
		if (c.content == WordContent::Repeal && c.out != WordOutcome::Catastrophe) w.bus.emit(EvDecree{ k.um, toStr(c.area), "repealed", "peace", toStr(c.cl) });
		const int n = since(w, f0);
		const std::string label = std::string(toStr(c.content)) + " " + toStr(c.cl) + " " + toStr(c.out);
		eq(n, 1, "one entry: " + label);
		if (n >= 1) say(label + ": " + w.chronicle.back().text);
		years(w, 11);
	}
	// the wisdom line names the milder stance they heard (Care: Kin, "begin their care at the hearth")
	bool kin = false;
	for (const ChronicleEntry& e : w.chronicle) if (e.id >= first && has(e.text, "care at the hearth")) kin = true;
	ok(kin, "wisdom: the stance they heard");
	eq(lintAll(w, first, { "Ossa" }), 0, "the lint");
}

// ===================================================================== messengers

DM_TEST("chronicle mythos: messengers chosen by archetype, redeemed, discredited, vindicated, martyred; rulers untold") {
	M v = fresh(208, 1, false);
	World& w = *v.w;
	Cast k = cast(v);
	touch(w, k.te);                                          // a chosen messenger: the god answered a card
	const int first = w.nextId.entry;
	const char* const nm[] = { "Ossa", "Darro", "Ilun", "Teodo", "Ithel", "Sela" };
	const MessengerArchetype arch[] = { MessengerArchetype::Shepherd, MessengerArchetype::Criminal, MessengerArchetype::Child,
		MessengerArchetype::Queen, MessengerArchetype::Unbelieved, MessengerArchetype::None };
	for (int i = 0; i < 6; i++) {
		const int f = figure(w, k.um, k.cap, arch[i], nm[i]);
		w.cultures[size_t(k.um)].prophet = Prophet{ "Society-Name", k.cap, w.year + 60, f };
		const int f0 = w.nextId.entry;
		w.bus.emit(EvProphetRisen{ k.um, k.cap, "Society-Name" });   // Society first, then Characters' figure event
		w.bus.emit(EvFigure{ f, "messenger", k.um, k.cap, "chosen" });
		eq(since(w, f0), 1, std::string("one entry for a chosen ") + toStr(arch[i]));
		if (since(w, f0)) { say(w.chronicle.back().text); ok(has(w.chronicle.back().text, nm[i]), "the figure's own name"); }
		// the prophet dies: the figure's name, the people's word
		const int f1 = w.nextId.entry;
		w.cultures[size_t(k.um)].prophet.reset();
		w.bus.emit(EvProphetDied{ k.um, "Society-Name" });
		if (since(w, f1) == 1) ok(has(w.chronicle.back().text, nm[i]) && !has(w.chronicle.back().text, "Society-Name"), "died under the figure's name: " + w.chronicle.back().text);
		years(w, 30);
	}
	const int crim = figure(w, k.va, k.vcap, MessengerArchetype::Criminal, "Vesk");
	const int unb = figure(w, k.va, k.vcap, MessengerArchetype::Unbelieved, "Ithra");
	const int child = figure(w, k.te, k.tcap, MessengerArchetype::Child, "Pim");
	const int f2 = w.nextId.entry;
	w.bus.emit(EvFigure{ crim, "messenger", k.va, k.vcap, "redeemed" }); years(w, 5);
	w.bus.emit(EvFigure{ crim, "messenger", k.va, k.vcap, "discredited" }); years(w, 5);
	w.bus.emit(EvFigure{ unb, "messenger", k.va, k.vcap, "vindicated" }); years(w, 5);
	for (Figure& f : w.mythos.figures) if (f.id == child) f.fate = uint8_t(FigureFate::Martyred);
	w.bus.emit(EvFigure{ child, "messenger", k.te, k.tcap, "fallen" }); years(w, 5);
	w.bus.emit(EvFigure{ child, "messenger", k.te, k.tcap, "died" });
	eq(since(w, f2, "figure"), 4, "redeemed, discredited, vindicated and martyred told; died is Society's");
	// a martyred messenger's later prophet death is not told again
	w.cultures[size_t(k.te)].prophet = Prophet{ "Pim", k.tcap, w.year + 60, child };
	w.vars.num["chronicle_pf." + std::to_string(k.te)] = child;
	const int f3 = w.nextId.entry;
	w.cultures[size_t(k.te)].prophet.reset();
	w.bus.emit(EvProphetDied{ k.te, "Pim" });
	eq(since(w, f3), 0, "the martyr was told at the martyrdom");
	// rulers come and go without a line
	const int r = figure(w, k.um, k.cap, MessengerArchetype::None, "Arvo", FigureKind::Ruler);
	const int f4 = w.nextId.entry;
	w.bus.emit(EvFigure{ r, "ruler", k.um, k.cap, "rise" });
	w.bus.emit(EvFigure{ r, "ruler", k.um, k.cap, "died" });
	eq(since(w, f4), 0, "no line for a ruler's reign");
	eq(lintAll(w, first, { "Society-Name" }), 0, "the lint");
}

// ===================================================================== judgement and survivors

DM_TEST("chronicle mythos: judgements by deed and heart x every sentence, one entry per card, the survivors once") {
	M v = fresh(209, 1, false);
	World& w = *v.w;
	Cast k = cast(v);
	const int first = w.nextId.entry;
	int tails = 0;
	for (int by = 0; by < 2; by++)
		for (int s = 0; s < 4; s++) {
			const int cid = by == 0 ? k.um : k.va;
			const int sid = by == 0 ? k.cap : k.vcap;
			int fig = -1;
			if (s == 2) fig = figure(w, cid, sid, MessengerArchetype::None, by ? "Vela" : "Arvo", FigureKind::Ruler);
			const int cd = card(w, CardKind::Judgement, cid, sid, { 1, int(DecreeArea::Care), int(Circumstance::Famine), 1 }, fig);
			Card* c = cardOf(w, cd);
			c->data["area"] = int(DecreeArea::Care); c->data["circumstance"] = int(Circumstance::Famine);
			if (fig >= 0) c->data["deed"] = 2;
			const int f0 = w.nextId.entry;
			w.bus.emit(EvJudgement{ cd, cid, toStr(JudgeBy(by)), toStr(Sentence(s)) });
			answerCard(w, cd, by * 4 + s);
			eq(since(w, f0), 1, std::string("one entry: by ") + toStr(JudgeBy(by)) + ", " + toStr(Sentence(s)));
			if (since(w, f0)) {
				const ChronicleEntry& e = w.chronicle.back();
				say(e.text);
				eq(e.type, std::string("judgement"), "a judgement entry");
				tails += has(e.text, "After that") || has(e.text, "From then on");
			}
			years(w, 20);
		}
	eq(tails, 2, "the doctrine it begins is told once per people (deed, heart)");
	// survivors: the memory ends 70 years after the sentence (Mythos's survivorUntil)
	Faith& f = w.mythos.faith[size_t(k.va)];
	f.survivorUntil = w.year + 70;
	const int f1 = w.nextId.entry;
	for (int y = 0; y < 100; y++) { years(w, 1); chronicle::updateEpithets(w); }
	eq(since(w, f1, "survivors"), 1, "the survivors' memory ends with one line");
	if (const ChronicleEntry* e = lastOf(w, "survivors")) say(e->text);
	eq(lintAll(w, first), 0, "the lint");
}

// ===================================================================== card resolutions

DM_TEST("chronicle mythos: card resolutions: the shaping, the stance, no messenger, decree conflicts, competing prayers") {
	M v = fresh(210, 1, false);
	World& w = *v.w;
	Cast k = cast(v);
	touch(w, k.te);                                          // the god uses the mythos (a card answered)
	const int first = w.nextId.entry;
	// the shaping of the first people: the two strongest traits
	Traits& t = w.cultures[size_t(k.um)].traits;
	t.curiosity = 0.8; t.compassion = 0.8; t.ambition = 0.25; t.courage = 0.5; t.aggression = 0.25;
	int cd = card(w, CardKind::ShapePeople, k.um, k.cap);
	int f0 = w.nextId.entry;
	answerCard(w, cd, 1 * 1 + 2 * 3 + 0 * 9 + 1 * 27 + 0 * 81);
	if (ok(since(w, f0, "card") == 1, "the shaping is told")) {
		const std::string& s = w.chronicle.back().text;
		say(s);
		ok(has(s, "question") || has(s, "apart") || has(s, "stars"), "curiosity's clause");
		ok(has(s, "lame") || has(s, "stranger") || has(s, "left behind"), "compassion's clause");
	}
	// the Presence card: Wonders kept is the card's line; another stance is the presence event's
	cd = card(w, CardKind::Presence, k.um, k.cap);
	f0 = w.nextId.entry;
	answerCard(w, cd, 0);
	eq(since(w, f0, "card"), 1, "Wonders at the waking: a card line");
	cd = card(w, CardKind::Presence, k.va, k.vcap);
	f0 = w.nextId.entry;
	w.bus.emit(EvPresence{ "unseen", "wonders" });
	answerCard(w, cd, 2);
	eq(since(w, f0), 1, "Unseen at the waking: only the presence line");
	// no messenger chosen; one chosen is the figure's line
	cd = card(w, CardKind::MessengerChoice, k.va, k.vcap);
	f0 = w.nextId.entry;
	answerCard(w, cd, 3);
	eq(since(w, f0, "card"), 1, "no messenger: a card line");
	cd = card(w, CardKind::MessengerChoice, k.te, k.tcap);
	const int fig = figure(w, k.te, k.tcap, MessengerArchetype::Child, "Pim");
	f0 = w.nextId.entry;
	w.bus.emit(EvFigure{ fig, "messenger", k.te, k.tcap, "chosen" });
	answerCard(w, cd, 1);
	eq(since(w, f0), 1, "a chosen messenger: only the figure's line");
	years(w, 20);
	// every decree conflict case x answer, and its lapse
	for (int kase = 0; kase < 8; kase++) {
		for (int a = 0; a < 3; a++) {
			cd = card(w, CardKind::DecreeConflict, k.um, k.cap, { kase, k.va, k.cap, -1 }, -1, k.va);
			f0 = w.nextId.entry;
			answerCard(w, cd, a);
			eq(since(w, f0, "card"), 1, "conflict case " + std::to_string(kase) + " answer " + std::to_string(a));
			if (a == 0 && since(w, f0)) say(w.chronicle.back().text);
			years(w, 11);
		}
		cd = card(w, CardKind::DecreeConflict, k.um, k.cap, { kase, k.va, k.cap, -1 }, -1, k.va);
		f0 = w.nextId.entry;
		lapseCard(w, cd);
		eq(since(w, f0, "card-lapsed"), 1, "conflict lapse " + std::to_string(kase));
		years(w, 11);
	}
	// competing prayers: every pair x answer; their plain answered line is not doubled
	for (int pair = 0; pair < 4; pair++)
		for (int a = 0; a < 4; a++) {
			cd = card(w, CardKind::CompetingPrayers, k.um, k.cap, { k.cap, k.vcap, pair, -1 }, -1, k.va);
			f0 = w.nextId.entry;
			w.bus.emit(EvPrayerAnswered{ k.cap, PrayerKind::Rain });   // Society's answer inside the card's answer
			answerCard(w, cd, a);
			eq(since(w, f0), 1, "competing pair " + std::to_string(pair) + " answer " + std::to_string(a) + ": one entry");
			if (a == 2 && since(w, f0)) say(w.chronicle.back().text);
			years(w, 11);
		}
	eq(lintAll(w, first, { "Pim" }), 0, "the lint");
}

DM_TEST("chronicle mythos: lapsed cards are told only where the lapse acts (emergent), and never twice") {
	M v = fresh(211, 1, false);
	World& w = *v.w;
	Cast k = cast(v);
	const int first = w.nextId.entry;
	auto lapsed = [&](CardKind kind, std::array<int, 4> ref) {
		const int cd = card(w, kind, k.um, k.cap, ref, -1, k.va);
		Card* c = cardOf(w, cd);
		c->data["area"] = int(DecreeArea::Violence);
		const int f0 = w.nextId.entry;
		lapseCard(w, cd);
		years(w, 11);
		return since(w, f0);
	};
	eq(lapsed(CardKind::CompetingPrayers, { k.cap, k.vcap, 0, -1 }), 0, "emergent off: a lapsed competing prayer did nothing, so no line");
	eq(lapsed(CardKind::Judgement, { 1, 0, 0, 1 }), 0, "emergent off: nobody judged, no line");
	eq(lapsed(CardKind::MessengerChoice, { k.cap, -1, -1, -1 }), 0, "a lapsed messenger choice is nothing");
	eq(lapsed(CardKind::FirstDecree, { -9, -1, -1, -1 }), 0, "a lapsed first decree is told by its custom, if one forms");
	w.tune<MythosTune>().emergent = true;
	eq(lapsed(CardKind::CompetingPrayers, { k.cap, k.vcap, 1, -1 }), 1, "emergent: the competing prayer's lapse is told");
	eq(lapsed(CardKind::Judgement, { 1, 0, 0, 1 }), 1, "emergent: the people judged for themselves");
	// a card withdrawn or opened writes nothing
	const int cd = card(w, CardKind::Dispute, k.um, k.cap);
	const int f0 = w.nextId.entry;
	w.bus.emit(EvCard{ cd, "dispute", "open", k.um, -1 });
	w.bus.emit(EvCard{ cd, "dispute", "withdrawn", k.um, -1 });
	eq(since(w, f0), 0, "open and withdrawn are not resolutions");
	eq(lintAll(w, first), 0, "the lint");
}

// ===================================================================== the budget

DM_TEST("chronicle mythos: mythos adds at most 1.2 notable entries per real minute, and the rest is still told") {
	M v = fresh(212);
	World& w = *v.w;
	Cast k = cast(v);
	for (int c : { k.um, k.va, k.te }) touch(w, c);
	const int first = w.nextId.entry;
	const double y0 = w.year;
	Rng r(77);
	int events = 0;
	for (int y = 0; y < 1200; y++) {
		// a god who talks every year, and a world that keeps changing its mind about the god
		const int cid = r.pickIndex(3);
		const int sid = cid == 0 ? k.cap : cid == 1 ? k.vcap : k.tcap;
		const DecreeArea a = DecreeArea(r.pickIndex(6));
		const int st = r.pickIndex(stanceCount(a));
		w.mythos.decrees.clear();
		w.mythos.decrees.push_back(Decree{ cid, a, int8_t(st), Clarity::Exact, w.year, 0.8, -1, false, {} });
		w.bus.emit(EvDecree{ cid, toStr(a), stanceName(a, st), "none", "dream" });
		if (y % 3 == 0) w.bus.emit(EvAspect{ cid, toStr(Aspect(1 + r.pickIndex(4))), "unknown" });
		if (y % 7 == 0) w.bus.emit(EvBreach{ cid, toStr(a), sid, -1, r.f() });
		if (y % 50 == 0) w.bus.emit(EvPresence{ y % 100 ? "messengers" : "wonders", "wonders" });
		events += 2;
		years(w, 1);
		chronicle::updateEpithets(w);
	}
	int notable = 0, minor = 0;
	for (const ChronicleEntry& e : w.chronicle) {
		if (e.id < first || !m8lint::isMythType(e.type)) continue;
		if (e.weight >= 2) notable++; else minor++;
	}
	const double minutes = (w.year - y0) / 120.0;
	say("1,200 talkative years (" + std::to_string(events) + "+ mythos events): " + std::to_string(notable) + " notable, " + std::to_string(minor) +
		" minor mythos entries; " + std::to_string(notable / minutes).substr(0, 4) + " notable a real minute");
	ok(notable / minutes <= 1.2, "at most 1.2 notable mythos entries a real minute");
	ok(notable >= 3, "but the mythos is told");
	int digests = 0;
	for (const ChronicleEntry& e : w.chronicle) digests += e.id >= first && e.type == "digest";
	ok(digests > 0, "what the spent lines cannot tell goes into the digest");
	ok(minor > notable, "the rest is minor (or folded)");
	ok(m8lint::maxBodyUse(w) <= 10 || minor > 0, "rotation");
	say("most used mythos template body: " + std::to_string(m8lint::maxBodyUse(w)) + " times");
	if (m8lint::maxBodyUse(w) > 10) {
		std::map<std::string, int> heads;
		for (const ChronicleEntry& e : w.chronicle) if (e.id >= first && m8lint::isMythType(e.type)) {
			heads[e.type + ": " + e.text.substr(0, 50)]++;
			heads[e.type + ": ..." + e.text.substr(e.text.size() > 40 ? e.text.size() - 40 : 0)]++;
		}
		for (const auto& kv : w.vars.num) if (kv.first.rfind("chronicle_mu.", 0) == 0 && kv.second > 10) say(kv.first + " = " + std::to_string(kv.second));
		for (const auto& kv : heads) if (kv.second > 8) say(std::to_string(kv.second) + " x " + kv.first);
	}
	ok(m8lint::maxBodyUse(w) <= 10, "no mythos template body more than 10 times in 1,200 years");
	eq(lintAll(w, first), 0, "the lint");
}

// ===================================================================== the kind of world (3.1)

DM_TEST("chronicle mythos: a world made other than Balanced has its genesis line, in the book and in the tale") {
	for (int a = 0; a < 5; a++) {
		M v = fresh(220 + uint32_t(a));
		World& w = *v.w;
		w.settings.archetype = Archetype(a);
		w.phase = Phase::Life;
		const int f0 = w.nextId.entry;
		w.bus.emit(EvPhase{ Phase::People, Phase::Life });
		const int lines = since(w, f0, "genesis");
		eq(lines, a == 0 ? 1 : 2, std::string("genesis lines for ") + toStr(Archetype(a)));
		if (a > 0 && lines == 2) {
			const std::string line = w.chronicle.back().text;
			say(toStr(Archetype(a)) + std::string(": ") + line);
			eq(w.chronicle.back().weight, 3, "epochal");
			const chronicle::Tale t = chronicle::tale(w);
			ok(!t.paragraphs.empty() && t.paragraphs[0].size() > 40, "the tale's genesis paragraph");
		}
		eq(lintAll(w, f0), 0, "the lint");
	}
	// phase 0: never
	M v0 = fresh(230, 0);
	v0.w->settings.archetype = Archetype::Strange;
	const int f0 = v0.w->nextId.entry;
	v0.w->bus.emit(EvPhase{ Phase::People, Phase::Life });
	eq(since(*v0.w, f0, "genesis"), 1, "phase 0: today's genesis only");
}

// ===================================================================== the tale

DM_TEST("chronicle mythos: the tale gains what they believed and the legacy, for every legacy x mood") {
	M v = fresh(213, 1, false);
	World& w = *v.w;
	Cast k = cast(v);
	w.mythos.decrees.push_back(Decree{ k.um, DecreeArea::Violence, int8_t(ViolenceStance::Peace), Clarity::Exact, 120, 0.8, -1, false, {} });
	w.mythos.decrees.push_back(Decree{ k.um, DecreeArea::Hospitality, int8_t(HospitalityStance::Guarded), Clarity::Parable, 150, 0.7, -1, false, {} });
	w.mythos.decrees.push_back(Decree{ k.um, DecreeArea::Worship, int8_t(Rite::Kindness), Clarity::Dream, 160, 0.9, -1, false, {} });
	w.mythos.decrees.push_back(Decree{ k.um, DecreeArea::Care, int8_t(CareStance::Kin), Clarity::Sign, 170, 1.0, -1, true, {} });
	w.mythos.faith[size_t(k.um)].aspect = Aspect::Parent;
	w.mythos.faith[size_t(k.um)].agency = 0.7;
	touch(w, k.um);
	w.god.deeds["card:judgement"] = 5;
	w.god.deeds["rain"] = 3;
	w.god.deeds["prophet"] = 4;
	int checked = 0;
	for (int lg = 1; lg <= 5; lg++)
		for (int m = 0; m < 6; m++) {
			Ending en;
			en.type = m >= 4 ? EndingType::Silence : EndingType::Ark;
			en.mood = Mood(m);
			en.culture = k.um;
			en.settlement = k.cap;
			en.year = w.year;
			en.legacy = Legacy(lg);
			w.ending = en;
			const chronicle::Tale t = chronicle::tale(w);
			ok(int(t.paragraphs.size()) <= w.tune<ChronicleTune>().taleMaxParagraphs + 2, "paragraph count");
			bool belief = false;
			std::string all;
			for (const std::string& p : t.paragraphs) { belief |= has(p, "believed, in the end"); all += p + "\n"; }
			ok(belief, "a paragraph of what they believed");
			ok(has(all, "the Decree of Blood") && has(all, "the Decree of the Gate"), "the decrees kept at the end");
			ok(!has(all, "card") && !has(all, "prophets"), "no bookkeeping in the god's paragraph");
			ok(has(all, "think without being told"), "the people's will, named once");
			ChronicleEntry fake;
			fake.type = "card";
			fake.weight = 3;
			fake.text = t.paragraphs.back();
			eq(m8lint::fault(fake, {}), std::string(), "the legacy paragraph reads clean: " + fake.text);
			if (m == 0 || lg == 5) { if (checked < 7) say(toStr(Legacy(lg)) + std::string(" / ") + toStr(Mood(m)) + ": " + t.paragraphs.back()); }
			checked++;
		}
	eq(checked, 30, "5 legacies x 6 moods");
	// phase 0: today's tale
	w.tune<MythosTune>().phase = 0;
	const chronicle::Tale t0 = chronicle::tale(w);
	bool belief0 = false;
	for (const std::string& p : t0.paragraphs) belief0 |= has(p, "believed, in the end");
	ok(!belief0, "phase 0: no mythos paragraph");
	ok(int(t0.paragraphs.size()) <= w.tune<ChronicleTune>().taleMaxParagraphs, "phase 0: today's paragraph count");
}
