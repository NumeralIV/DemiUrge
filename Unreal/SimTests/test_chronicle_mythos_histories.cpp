// DEMIURGE — SimTests/test_chronicle_mythos_histories.cpp : the voice lint of SPEC-MYTHOS 3.31 on real histories
// (8 seeds x 6 scripted gods), the mythos share of notable entries, and the chronicle side of guardrail 5.1 rule 2.
// Track M8 "chronicle-mythos". Needs the REAL modules: it runs in run.ps1 all; with any module stubbed
// (run.ps1 chronicle) each test returns at once.
//
// The lint's six gods all use the mythos (the lint is about what the mythos writes; a god who never touches it keeps
// today's chronicle word for word, the second test). Scripted here, with their own Rng, never w.rng (Tools/simrun's
// v1 policies are compiled into test_mythos_integration.cpp, so they cannot be linked a second time). Each answers
// the waking cards (the shaping: chance; the stance: Wonders, quiet: Messengers):
//   responder   kind (answers prayers: rain, heal, peace, bloom; heals plagues), and answers every card with its first
//               answer that can be paid
//   random      chaotic powers, every card answered at random, random words to random peoples
//   lawgiver    six decrees for every people, Exact (through a chosen messenger once there is one), judges by the deed
//   stern       cruel (lightning on the largest towns, now and then a quake), judges by the deed with a disaster
//   quiet       only the waking cards (presence: Messengers), then nothing
//   speaker     kind, and a Sign or a Dream to some people every few years (answers or decrees), no other card
// passive and kind (no card, no word) play the guardrail test. Every god but passive keeps at least 400 Awe (this
// checks the chronicle, not the Awe economy).
// Default: L4, 1,500 years. DM_LONG=1: L5, 3,600 years (the spec's history length). Both: 8 seeds x 6 gods.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Cards.h"
#include "Characters.h"
#include "Chronicle.h"
#include "Doctrine.h"
#include "Mythos.h"
#include "Powers.h"
#include "Sim.h"
#include "State.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"
#include "test_chronicle_mythos_lint.h"

using namespace dm;
using namespace dmt;

namespace {

void say(const std::string& s) { std::printf("  %s\n", s.c_str()); std::fflush(stdout); }

bool allReal() {
	for (const char* m : { "terrain", "climate", "hydro", "life", "path", "units", "civ", "economy", "society", "diplomacy",
	                       "powers", "disasters", "chronicle", "sim", "balance", "names", "mythos", "cards", "doctrine",
	                       "characters", "legends", "faiths", "omens", "keepers", "renewal" })
		if (stub::stubbed(m)) return false;
	return true;
}
bool longRuns() { const char* e = std::getenv("DM_LONG"); return e && e[0] == '1'; }

enum class Player { Passive, Kind, Stern, Responder, Random, Lawgiver, Quiet, Speaker };
const char* name(Player g) {
	switch (g) {
	case Player::Passive: return "passive"; case Player::Kind: return "kind"; case Player::Stern: return "stern";
	case Player::Responder: return "responder"; case Player::Random: return "random"; case Player::Quiet: return "quiet";
	case Player::Speaker: return "speaker"; default: return "lawgiver";
	}
}

std::unique_ptr<World> historyWorld(uint32_t seed, int L, int phase) {
	auto w = sim::create({ .seed = seed, .name = "", .L = L });
	if (!w) return w;
	w->tune<MythosTune>().phase = phase;
	w->tune<MythosTune>().emergent = false;
	if (phase >= 1) { mythos::attach(*w); mythos::rebuild(*w); }
	sim::genesis::skip(*w, {});
	return w;
}

std::vector<int> liveTowns(const World& w) {
	std::vector<int> v;
	for (const Settlement& s : w.settlements) if (!s.dead) v.push_back(s.id);
	return v;
}
bool tryUse(World& w, const char* id, int cell) {
	if (!powers::can(w, id, cell).ok) return false;
	return powers::use(w, id, cell);
}

void kindAct(World& w, Rng& r) {
	int done = 0;
	for (const Settlement& s : w.settlements) {
		if (s.dead || !s.prayer || done >= 2) continue;
		const char* id = nullptr;
		switch (s.prayer->kind) {
		case PrayerKind::Rain: id = "rain"; break;
		case PrayerKind::Heal: id = "heal"; break;
		case PrayerKind::Peace: id = "peace"; break;
		case PrayerKind::Bloom: id = "bloom"; break;
		default: break;
		}
		if (id && r.chance(0.7) && tryUse(w, id, s.cell)) done++;
	}
	for (const Settlement& s : w.settlements) if (!s.dead && s.plague > 0.2 && r.chance(0.3)) { tryUse(w, "heal", s.cell); break; }
}
void cruelAct(World& w, Rng& r, int year) {
	const std::vector<int> live = liveTowns(w);
	if (live.size() < 4) return;
	int big = live[0];
	for (int id : live) if (w.settlements[size_t(id)].pop > w.settlements[size_t(big)].pop) big = id;
	if (year % 3 == 0) tryUse(w, "lightning", w.settlements[size_t(live[size_t(r.pickIndex(int(live.size())))])].cell);
	if (year % 25 == 0) tryUse(w, "quake", w.settlements[size_t(big)].cell);
}
void randomAct(World& w, Rng& r) {
	static const char* const kIds[] = { "rain", "bloom", "heal", "lightning", "quake", "inspire", "prophet", "peace", "plague" };
	const std::vector<int> live = liveTowns(w);
	if (live.empty()) return;
	if (r.chance(0.5)) tryUse(w, kIds[r.pickIndex(9)], w.settlements[size_t(live[size_t(r.pickIndex(int(live.size())))])].cell);
}
// The ShapePeople answer "leave it to chance"; any other card: the first answer that can be chosen (or a random one).
void answerCards(World& w, Rng& r, bool random, bool byDeed) {
	for (int id : cards::openCards(w)) {
		const Card* c = cards::find(w, id);
		if (!c) continue;
		if (c->kind == CardKind::ShapePeople) { cards::answer(w, id, 243); continue; }
		std::vector<int> okc;
		for (const CardOption& o : cards::options(w, id)) if (o.ok) okc.push_back(o.choice);
		if (okc.empty()) continue;
		int choice = okc[0];
		if (random) choice = okc[size_t(r.pickIndex(int(okc.size())))];
		if (byDeed && c->kind == CardKind::Judgement) {
			for (int pref : { 3, 2, 1 }) if (std::find(okc.begin(), okc.end(), pref) != okc.end()) { choice = pref; break; }   // by the deed
		}
		cards::answer(w, id, choice);
	}
}
void speakAt(World& w, int cid, WordOpts o) {
	const int m = characters::messenger(w, cid);
	o.figure = m;
	if (o.clarity == Clarity::Exact) o.channel = m >= 0 ? Channel::Messenger : Channel::Sky;
	else if (o.clarity == Clarity::Parable) o.channel = Channel::Messenger;
	else if (o.clarity == Clarity::Dream) o.channel = Channel::Dream;
	else o.channel = Channel::Sky;
	if (doctrine::canSpeak(w, cid, o).ok) doctrine::speak(w, cid, o);
}
void randomWord(World& w, Rng& r) {
	std::vector<int> live;
	for (const Culture& c : w.cultures) if (!c.dead) live.push_back(c.id);
	if (live.empty()) return;
	const int cid = live[size_t(r.pickIndex(int(live.size())))];
	WordOpts o;
	const int pick = r.pickIndex(5);
	o.content = pick < 3 ? WordContent::Decree : pick == 3 ? WordContent::Repeal : WordContent::Answer;
	o.area = DecreeArea(r.pickIndex(6));
	o.stance = r.pickIndex(stanceCount(o.area));
	o.clarity = Clarity(r.pickIndex(4));
	speakAt(w, cid, o);
}
void lawgiverAct(World& w, int year) {
	static const std::pair<DecreeArea, int> kSet[] = {
		{ DecreeArea::Violence, int(ViolenceStance::Peace) }, { DecreeArea::Wealth, int(WealthStance::Share) },
		{ DecreeArea::Worship, int(Rite::Temples) }, { DecreeArea::Honesty, int(HonestyStance::Truth) },
		{ DecreeArea::Hospitality, int(HospitalityStance::Guarded) }, { DecreeArea::Care, int(CareStance::Shelter) } };
	if (year % 5 != 0) return;
	for (const Culture& c : w.cultures) {
		if (c.dead) continue;
		if (characters::messenger(w, c.id) < 0 && !c.prophet) {
			const Settlement* cap = state::settlement(w, c.capital);
			if (cap && !cap->dead && cap->culture == c.id && !characters::candidates(w, cap->id).empty()) characters::choose(w, cap->id, 0);
		}
		for (const auto& [area, stance] : kSet) {
			const Decree* d = doctrine::decree(w, c.id, area);
			if (d && d->stance == stance && !d->custom) continue;
			WordOpts o;
			o.content = WordContent::Decree;
			o.area = area;
			o.stance = stance;
			o.clarity = Clarity::Exact;
			speakAt(w, c.id, o);
			break;                                        // one word per people per decade (Doctrine's gate)
		}
	}
}

// The waking cards only (ShapePeople: chance; Presence: the given stance).
void answerWaking(World& w, int presence) {
	for (int id : cards::openCards(w)) {
		const Card* c = cards::find(w, id);
		if (!c) continue;
		if (c->kind == CardKind::ShapePeople) cards::answer(w, id, 243);
		else if (c->kind == CardKind::Presence) cards::answer(w, id, presence);
	}
}
void speakerAct(World& w, Rng& r) {
	std::vector<int> live;
	for (const Culture& c : w.cultures) if (!c.dead) live.push_back(c.id);
	if (live.empty()) return;
	WordOpts o;
	o.content = r.chance(0.5) ? WordContent::Answer : WordContent::Decree;
	o.area = DecreeArea(r.pickIndex(6));
	o.stance = r.pickIndex(stanceCount(o.area));
	o.clarity = r.chance(0.5) ? Clarity::Sign : Clarity::Dream;
	speakAt(w, live[size_t(r.pickIndex(int(live.size())))], o);
}

struct Run {
	std::vector<ChronicleEntry> chronicle;
	std::vector<std::string> names;
	int maxBody = 0;
	double years = 0;
	int cardsOpened = 0, cardsAnswered = 0;
	chronicle::Tale tale;
};

Run play(uint32_t seed, Player g, int L, int years, int phase = 1) {
	Run out;
	auto w = historyWorld(seed, L, phase);
	if (!w) return out;
	w->tune<ChronicleTune>().maxEntries = 100000;
	std::set<std::string> names;
	w->bus.on<EvProphetRisen>([&names](const EvProphetRisen& e) { if (!e.name.empty()) names.insert(e.name); });
	World* wp = w.get();
	w->bus.on<EvFigure>([&names, wp](const EvFigure& e) {
		if (const Figure* f = characters::figure(*wp, e.id)) if (!f->name.empty()) names.insert(f->name);
	});
	w->bus.on<EvCard>([&out](const EvCard& e) { out.cardsOpened += e.stage == "open"; out.cardsAnswered += e.stage == "answered"; });
	Rng r(seed * 7919u + uint32_t(g) * 104729u + 13u);
	const double y0 = w->year;
	for (int y = 0; y < years && !w->ending; y++) {
		if (g != Player::Passive) w->god.awe = std::max(w->god.awe, 400.0);
		switch (g) {
		case Player::Kind: kindAct(*w, r); break;
		case Player::Stern: answerWaking(*w, 0); cruelAct(*w, r, y); answerCards(*w, r, false, true); break;
		case Player::Quiet: answerWaking(*w, 1); break;
		case Player::Speaker: answerWaking(*w, 0); kindAct(*w, r); if (y % 6 == 0) speakerAct(*w, r); break;
		case Player::Responder: kindAct(*w, r); answerCards(*w, r, false, false); break;
		case Player::Random: randomAct(*w, r); answerCards(*w, r, true, false); if (y % 4 == 0) randomWord(*w, r); break;
		case Player::Lawgiver: answerWaking(*w, 0); lawgiverAct(*w, y); answerCards(*w, r, false, true); break;
		default: break;
		}
		sim::advance(*w, sim::kTicksPerYear);
	}
	out.years = w->year - y0;
	out.chronicle = w->chronicle;
	out.names = m8lint::names(*w, names);
	out.maxBody = m8lint::maxBodyUse(*w);
	out.tale = chronicle::tale(*w);
	return out;
}

} // namespace

DM_TEST("chronicle mythos histories: the voice lint on 8 seeds x 6 gods; mythos <= 1.2 notable entries a real minute") {
	if (!allReal()) return;
	const bool lng = longRuns();
	const int L = lng ? 5 : 4, years = lng ? 3600 : 1500;
	const Player gods[] = { Player::Responder, Player::Random, Player::Lawgiver, Player::Stern, Player::Quiet, Player::Speaker };
	std::map<std::string, int> types;
	int runs = 0, faults = 0, overBody = 0, overRate = 0, allEntries = 0, longWorld = 0;
	double worstRate = 0, sumNotable = 0, sumMinutes = 0;
	int worstBody = 0;
	const double t0 = nowMs();
	for (uint32_t seed = 1; seed <= 8; seed++)
		for (Player g : gods) {
			const Run run = play(seed, g, L, years);
			if (run.chronicle.empty()) { fail("no history for seed " + std::to_string(seed)); continue; }
			runs++;
			int notable = 0, mythN = 0;
			for (const ChronicleEntry& e : run.chronicle) {
				allEntries++;
				const std::string f = m8lint::fault(e, run.names);
				if (!f.empty()) { if (faults < 10) say(std::string(name(g)) + " seed " + std::to_string(seed) + ": " + f + ": [" + e.type + "] " + e.text); faults++; }
				if (!m8lint::isMythType(e.type) && e.weight <= 2 && m8lint::words(e.text) > 50) longWorld++;
				if (m8lint::isMythType(e.type)) { types[e.type]++; mythN++; if (e.weight >= 2) notable++; }
			}
			for (const std::string& p : run.tale.paragraphs) {
				ChronicleEntry te; te.type = "tale"; te.weight = 3; te.text = p;
				const std::string f = m8lint::fault(te, run.names);
				if (!f.empty()) { if (faults < 10) say(std::string(name(g)) + " seed " + std::to_string(seed) + " tale: " + f + ": " + p); faults++; }
			}
			if (seed == 1 && (g == Player::Lawgiver || g == Player::Random || g == Player::Responder)) {
				int shown = 0;
				for (const ChronicleEntry& e : run.chronicle)
					if (m8lint::isMythType(e.type) && shown < 8 && (e.weight >= 2 || shown < 3)) { say("    [" + e.type + " w" + std::to_string(e.weight) + "] " + e.text); shown++; }
			}
			const double minutes = std::max(1e-9, run.years / 120.0);
			const double rate = notable / minutes;
			sumNotable += notable; sumMinutes += minutes;
			worstRate = std::max(worstRate, rate);
			worstBody = std::max(worstBody, run.maxBody);
			overBody += run.maxBody > 10;
			overRate += rate > 1.2 + 1e-9;
			if (seed == 1 || g == Player::Random) say(std::string(name(g)) + " seed " + std::to_string(seed) + ": " + std::to_string(run.chronicle.size()) + " entries, " +
				std::to_string(mythN) + " mythos (" + std::to_string(notable) + " notable, " + std::to_string(rate).substr(0, 4) + " a real minute), cards " +
				std::to_string(run.cardsOpened) + " opened / " + std::to_string(run.cardsAnswered) + " answered, most used mythos line x" + std::to_string(run.maxBody));
		}
	std::string ts;
	for (const auto& kv : types) ts += kv.first + " " + std::to_string(kv.second) + ", ";
	say(std::to_string(runs) + " histories (L" + std::to_string(L) + ", " + std::to_string(years) + " years) in " + std::to_string(int((nowMs() - t0) / 1000)) +
		" s; " + std::to_string(allEntries) + " entries; mythos: " + ts);
	say("mythos notable a real minute: mean " + std::to_string(sumNotable / std::max(1e-9, sumMinutes)).substr(0, 4) + ", worst " + std::to_string(worstRate).substr(0, 4) +
		"; most used mythos line x" + std::to_string(worstBody) + "; world entries over 50 words (not linted): " + std::to_string(longWorld));
	eq(runs, 48, "8 seeds x 6 gods");
	eq(faults, 0, "every entry and every tale paragraph passes the lint");
	eq(overBody, 0, "no mythos template body more than 10 times in a history");
	eq(overRate, 0, "mythos adds at most 1.2 notable entries a real minute in every history");
	ok(types["decree"] + types["word"] > 0 && types["judgement"] + types["card"] > 0, "the mythos was told");
}

DM_TEST("chronicle mythos histories: phase 1, emergent off: a god who ignores every card keeps the phase-0 chronicle") {
	if (!allReal()) return;
	for (uint32_t seed : { 3u, 6u }) {
		for (Player g : { Player::Passive, Player::Kind }) {
			const Run a = play(seed, g, 4, 600, 0);
			const Run b = play(seed, g, 4, 600, 1);
			int myth = 0;
			for (const ChronicleEntry& e : b.chronicle) myth += m8lint::isMythType(e.type);
			eq(myth, 0, std::string(name(g)) + " seed " + std::to_string(seed) + ": no mythos entry for a god who never touched the mythos");
			bool same = a.chronicle.size() == b.chronicle.size();
			for (size_t i = 0; same && i < a.chronicle.size(); i++) same = a.chronicle[i].text == b.chronicle[i].text && a.chronicle[i].weight == b.chronicle[i].weight;
			ok(same, std::string(name(g)) + " seed " + std::to_string(seed) + ": the same chronicle, word for word (" + std::to_string(a.chronicle.size()) + " / " +
				std::to_string(b.chronicle.size()) + " entries)");
		}
	}
}
