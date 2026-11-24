// DEMIURGE — SimTests/test_doctrine_decrees.cpp : decrees, customs, breaches (SPEC-MYTHOS 3.6). Track M3.
#include "test_doctrine_fixture.h"

using namespace dm;
using namespace dmt;
using namespace dmt::doc;

DM_TEST("doctrine: phase 0 is neutral (no proposal, no word, no card answer)") {
	stub::Guard g(true);
	DocWorld d = docWorld(0, true);
	World& w = *d.w;
	setDecree(w, d.vai, DecreeArea::Violence, int(ViolenceStance::Peace));
	setWar(w, d.vai, d.oru, d.oru);
	w.mythos.faith[0].obedience = 0;
	const uint32_t h0 = hash(w);
	const auto ev = capture(w, [&] { for (int i = 0; i < 200; i++) { w.year += 1; doctrine::yearly(w); } });
	eq(int(ev.size()), 0, "no events at phase 0");
	if (stub::stubbed("cards")) eq(stub::called("cards_propose"), 0, "nothing proposed");
	w.year = 300;
	ok(hash(w) == h0, "the world is untouched");
	WordOpts o; o.content = WordContent::Decree; o.area = DecreeArea::Care; o.stance = 0; o.clarity = Clarity::Sign;
	eq(doctrine::speak(w, d.vai, o), -1, "speak refused");
	eq(std::string(doctrine::toStr(doctrine::canSpeak(w, d.vai, o).why)), std::string("phase"));
	const int k = pushCard(w, CardKind::FirstDecree, d.vai, d.towns[0]);
	ok(!doctrine::answer(w, k, 1), "no answer at phase 0");
	ok(doctrine::options(w, k).empty(), "no options at phase 0");
}

DM_TEST("doctrine: strictness table and reads (3.6)") {
	DocWorld d = docWorld();
	const World& w = *d.w;
	near(doctrine::strictness(w, DecreeArea::Violence, 0), 0.9); near(doctrine::strictness(w, DecreeArea::Violence, 1), 0.5);
	near(doctrine::strictness(w, DecreeArea::Violence, 2), 0.2); near(doctrine::strictness(w, DecreeArea::Wealth, 0), 0.7);
	near(doctrine::strictness(w, DecreeArea::Wealth, 1), 0.5); near(doctrine::strictness(w, DecreeArea::Wealth, 2), 0.2);
	near(doctrine::strictness(w, DecreeArea::Honesty, 0), 0.8); near(doctrine::strictness(w, DecreeArea::Honesty, 1), 0.4);
	near(doctrine::strictness(w, DecreeArea::Honesty, 2), 0.1); near(doctrine::strictness(w, DecreeArea::Hospitality, 0), 0.8);
	near(doctrine::strictness(w, DecreeArea::Hospitality, 2), 0.2); near(doctrine::strictness(w, DecreeArea::Care, 0), 0.8);
	near(doctrine::strictness(w, DecreeArea::Care, 1), 0.4); near(doctrine::strictness(w, DecreeArea::Care, 2), 0.2);
	near(doctrine::strictness(w, DecreeArea::Land, 2), 0.9);
	eq(doctrine::strictness(w, DecreeArea::Violence, 3), 0.0, "bad stance");
	eq(doctrine::strictness(w, DecreeArea::Count, 0), 0.0, "bad area");
	ok(doctrine::decree(w, d.vai, DecreeArea::Care) == nullptr, "no decree yet");
	eq(doctrine::kept(w, d.vai, DecreeArea::Care), 0.0);
	eq(doctrine::burden(w, d.vai), 0.0);
	eq(doctrine::burden(w, 99), 0.0, "bad culture");
	eq(doctrine::customStance(w, 99, DecreeArea::Care), -1, "bad culture");
	eq(doctrine::customStance(w, d.vai, DecreeArea::Count), -1, "bad area");
}

DM_TEST("doctrine: a decree by the Word, one per area; kept = obedience x fidelity; the decree event") {
	DocWorld d = docWorld();
	World& w = *d.w;
	const int m = addMessenger(w, d.vai, d.towns[0]);
	w.mythos.faith[0].obedience = 0.6;
	WordOpts o; o.content = WordContent::Decree; o.area = DecreeArea::Violence; o.stance = int(ViolenceStance::Peace);
	o.clarity = Clarity::Exact; o.channel = Channel::Messenger;
	int id = -1;
	const auto ev = capture(w, [&] { id = doctrine::speak(w, d.vai, o); });
	if (!ok(id > 0, "spoken")) return;
	const Utterance& u = w.mythos.words.back();
	eq(u.id, id); eq(u.figure, m, "through the messenger"); eq(int(u.channel), int(Channel::Messenger));
	const Decree* dc = doctrine::decree(w, d.vai, DecreeArea::Violence);
	if (!ok(dc != nullptr, "decree held")) return;
	eq(int(dc->stance), int(ViolenceStance::Peace), "an exact word keeps its stance, however it is read");
	eq(dc->by, m); ok(!dc->custom);
	near(dc->kept, 0.6 * u.fidelity, 1e-9, "kept = obedience x fidelity (reach 1: no archetype)");
	eq(countEvents(ev, Ev::Word), 1, "one word event");
	bool sawDecree = false;
	for (const Event& e : ev) if (const EvDecree* x = e.as<EvDecree>()) {
		sawDecree = true;
		eq(x->culture, d.vai); eq(x->area, std::string("violence")); eq(x->stance, std::string("peace"));
		eq(x->prev, std::string("none")); eq(x->clarity, std::string("exact"));
	}
	ok(sawDecree, "decree event");
	// a second decree in the area replaces it
	w.year += 10;
	o.stance = int(ViolenceStance::Valour);
	std::string prev;
	const auto ev2 = capture(w, [&] { doctrine::speak(w, d.vai, o); });
	for (const Event& e : ev2) if (const EvDecree* x = e.as<EvDecree>()) prev = x->prev;
	int n = 0;
	for (const Decree& x : w.mythos.decrees) if (x.culture == d.vai && x.area == DecreeArea::Violence) n++;
	eq(n, 1, "one decree per area");
	eq(int(doctrine::decree(w, d.vai, DecreeArea::Violence)->stance), int(ViolenceStance::Valour));
	eq(prev, std::string("peace"), "prev names the old stance");
	noNaN(w);
}

DM_TEST("doctrine: kept drifts toward obedience x fidelity, a breach costs 0.1 for 20 years") {
	DocWorld d = docWorld();
	World& w = *d.w;
	const DoctrineTune& T = w.tune<DoctrineTune>();
	setDecree(w, d.vai, DecreeArea::Care, int(CareStance::Strength), 0.2);   // never pressed: no breach can interfere
	w.mythos.faith[0].obedience = 0.9;
	w.year += 1; doctrine::yearly(w);
	near(doctrine::kept(w, d.vai, DecreeArea::Care), 0.2 + T.keptDrift, 1e-9, "moves keptDrift a year");
	for (int i = 0; i < 40; i++) { w.year += 1; doctrine::yearly(w); }
	near(doctrine::kept(w, d.vai, DecreeArea::Care), 0.9, 1e-9, "reaches obedience x fidelity");
	// a breach window: the target is 0.1 lower until it ends
	for (Decree& x : w.mythos.decrees) if (x.area == DecreeArea::Care) x.data["breachUntil"] = w.year + 20;
	for (int i = 0; i < 5; i++) { w.year += 1; doctrine::yearly(w); }
	near(doctrine::kept(w, d.vai, DecreeArea::Care), 0.8, 1e-9, "0.1 lower while the breach is remembered");
	for (int i = 0; i < 30; i++) { w.year += 1; doctrine::yearly(w); }
	near(doctrine::kept(w, d.vai, DecreeArea::Care), 0.9, 1e-9, "and back after 20 years");
	// a custom is kept at customStrength whatever the obedience
	setDecree(w, d.vai, DecreeArea::Wealth, int(WealthStance::Share), 0.1, true);
	w.year += 1; doctrine::yearly(w);
	near(doctrine::kept(w, d.vai, DecreeArea::Wealth), 1.0, 1e-12, "a custom is theirs: kept in full (it works at customStrength in Factors)");
}

namespace {
// Breaches of a Peace decree under attack in `years` yearly passes (the same years every call: same forks).
int peaceBreaches(double judgedBy, double obedience, int years, double* meanSev = nullptr) {
	DocWorld d = docWorld();
	World& w = *d.w;
	setDecree(w, d.vai, DecreeArea::Violence, int(ViolenceStance::Peace));
	setWar(w, d.vai, d.oru, d.oru);
	w.mythos.faith[0].obedience = obedience;
	w.mythos.faith[0].judgedBy = judgedBy;
	int n = 0; double sev = 0;
	(void)w.bus.on<EvBreach>([&](const EvBreach& e) { n++; sev += e.severity; });
	for (int i = 0; i < years; i++) { w.year += 1; doctrine::yearly(w); }
	if (meanSev) *meanSev = n > 0 ? sev / n : 0;
	return n;
}
}

DM_TEST("doctrine: breach chance = 0.02 x strictness x pressure x (1 - obedience) x judgement doctrine") {
	stub::Guard g;
	const int years = 20000;
	double sev0 = 0, sevD = 0;
	const int base = peaceBreaches(0, 0, years, &sev0);
	const double expect = 0.02 * 0.9 * 1.0 * 1.0 * years;   // attacked: pressure 1
	ok(std::fabs(base - expect) <= 0.15 * expect, "Peace under attack: " + std::to_string(base) + " breaches, expected about " + std::to_string(expect));
	const int half = peaceBreaches(0, 0.5, years);
	ok(std::fabs(half - expect / 2) <= 0.15 * expect / 2, "obedience 0.5 halves it: " + std::to_string(half));
	const int deeds = peaceBreaches(-0.5, 0, years, &sevD);
	const int hearts = peaceBreaches(0.5, 0, years);
	ok(deeds <= base && base <= hearts, "deeds x0.9 <= none <= hearts x1.1 (same forks): " + std::to_string(deeds) + " " +
		std::to_string(base) + " " + std::to_string(hearts));
	const double ratio = deeds > 0 ? double(hearts) / deeds : 0;
	ok(ratio > 1.08 && ratio < 1.38, "hearts / deeds about 1.1 / 0.9: " + std::to_string(ratio));
	near(sev0, 0.5, 1e-9, "severity = the capital's share of the people (0.5) x pressure 1");
	near(sevD, 0.6, 1e-9, "judged by deeds: each breach is told as graver (x 1.2)");
}

DM_TEST("doctrine: breach severity is the town's share of the people x pressure; severe breaches wait for judgement") {
	stub::Guard g;
	DocWorld d = docWorld();
	World& w = *d.w;
	setDecree(w, d.vai, DecreeArea::Violence, int(ViolenceStance::Peace));
	setWar(w, d.vai, d.oru, d.oru);
	w.mythos.faith[0].obedience = 0;
	std::vector<EvBreach> got;
	(void)w.bus.on<EvBreach>([&](const EvBreach& e) { got.push_back(e); });
	for (int i = 0; i < 2000 && got.empty(); i++) { w.year += 1; doctrine::yearly(w); }
	if (!ok(!got.empty(), "a breach")) return;
	const double share = 400.0 / 800.0;
	near(got[0].severity, share * 1.0, 1e-9, "capital share 0.5 x pressure 1");
	eq(got[0].settlement, d.towns[0], "the town with the most pressure (the most people)");
	eq(got[0].area, std::string("violence"));
	const Decree* dc = doctrine::decree(w, d.vai, DecreeArea::Violence);
	ok(dc && dc->data.count("pendId") == 1, "severity 0.5 >= breachCard: waits for a Judgement card");
	ok(dc && dc->data.at("breachUntil") > w.year, "the breach is remembered");
}

DM_TEST("doctrine: stances that are never pressed are never breached; customs are never breached") {
	DocWorld d = docWorld();
	World& w = *d.w;
	setDecree(w, d.vai, DecreeArea::Violence, int(ViolenceStance::Valour));
	setDecree(w, d.vai, DecreeArea::Wealth, int(WealthStance::Gain));
	setDecree(w, d.vai, DecreeArea::Honesty, int(HonestyStance::Cunning));
	setDecree(w, d.vai, DecreeArea::Hospitality, int(HospitalityStance::Closed));
	setDecree(w, d.vai, DecreeArea::Care, int(CareStance::Strength));
	setDecree(w, d.vai, DecreeArea::Worship, int(Rite::Prayer));
	setDecree(w, d.oru, DecreeArea::Violence, int(ViolenceStance::Peace), 0.5, true);   // Oru's custom
	setWar(w, d.vai, d.oru, d.oru);
	w.mythos.faith[0].obedience = 0; w.mythos.faith[1].obedience = 0;
	for (Settlement& s : w.settlements) { s.famine = 5; s.plague = 0.5; }
	int n = 0;
	(void)w.bus.on<EvBreach>([&](const EvBreach&) { n++; });
	for (int i = 0; i < 3000; i++) { w.year += 1; doctrine::yearly(w); }
	eq(n, 0, "no breach in 3,000 years");
}

DM_TEST("doctrine: customs form only with emergent, at the culture's year 200 and its first shrine") {
	for (int em = 0; em < 2; em++) {
		DocWorld d = docWorld(1, em == 1);
		World& w = *d.w;
		w.year = 150;
		w.settlements[size_t(d.towns[1])].b[Building::SHRINE] = 1;
		std::vector<EvDecree> got;
		(void)w.bus.on<EvDecree>([&](const EvDecree& e) { got.push_back(e); });
		w.year += 1; doctrine::yearly(w);
		if (em == 0) { eq(int(w.mythos.decrees.size()), 0, "emergent off: no custom"); eq(int(got.size()), 0); continue; }
		eq(int(got.size()), 1, "Vai's first shrine (Oru: no shrine, 151 years old)");
		int vaiCustoms = 0;
		for (const Decree& x : w.mythos.decrees) if (x.culture == d.vai) { ok(x.custom, "a custom"); vaiCustoms++; }
		eq(vaiCustoms, 1, "the first shrine: one custom");
		const Decree* rite = doctrine::decree(w, d.vai, DecreeArea::Worship);
		ok(rite && rite->custom, "the first custom is the rite");
		for (const EvDecree& e : got) if (e.culture == d.vai) eq(e.clarity, std::string("custom"), "a custom says so");
		w.year = 201; doctrine::yearly(w);
		vaiCustoms = 0;
		for (const Decree& x : w.mythos.decrees) if (x.culture == d.vai) vaiCustoms++;
		eq(vaiCustoms, 2, "year 200: a second custom");
		w.year = 400; doctrine::yearly(w);
		vaiCustoms = 0;
		for (const Decree& x : w.mythos.decrees) if (x.culture == d.vai) vaiCustoms++;
		eq(vaiCustoms, 2, "only those two moments");
		eq(doctrine::burden(w, d.vai), 0.0, "customs are no burden");
	}
}

DM_TEST("doctrine: customStance follows the traits (pure)") {
	DocWorld d = docWorld();
	World& w = *d.w;
	Traits& t = w.cultures[0].traits;
	t.compassion = 0.95; t.aggression = 0.1; t.piety = 0.2; t.ambition = 0.2; t.curiosity = 0.3; t.courage = 0.3;
	eq(doctrine::customStance(w, d.vai, DecreeArea::Care), int(CareStance::Shelter));
	eq(doctrine::customStance(w, d.vai, DecreeArea::Wealth), int(WealthStance::Share));
	eq(doctrine::customStance(w, d.vai, DecreeArea::Worship), int(Rite::Kindness));
	eq(doctrine::customStance(w, d.vai, DecreeArea::Violence), int(ViolenceStance::Peace));
	eq(doctrine::customStance(w, d.vai, DecreeArea::Worship), doctrine::customStance(w, d.vai, DecreeArea::Worship), "pure");
	t.compassion = 0.1; t.aggression = 0.95; t.courage = 0.9; t.ambition = 0.9;
	eq(doctrine::customStance(w, d.vai, DecreeArea::Violence), int(ViolenceStance::Valour));
	eq(doctrine::customStance(w, d.vai, DecreeArea::Hospitality), int(HospitalityStance::Closed));
	eq(doctrine::customStance(w, d.vai, DecreeArea::Wealth), int(WealthStance::Gain));
	eq(doctrine::customStance(w, d.vai, DecreeArea::Care), int(CareStance::Strength));
	eq(doctrine::customStance(w, d.vai, DecreeArea::Honesty), int(HonestyStance::Cunning));
}

DM_TEST("doctrine: burden is the mean strictness of the god's decrees (literal +0.2, a bent truth -0.3)") {
	DocWorld d = docWorld();
	World& w = *d.w;
	setDecree(w, d.vai, DecreeArea::Violence, int(ViolenceStance::Peace));
	setDecree(w, d.vai, DecreeArea::Care, int(CareStance::Strength));
	setDecree(w, d.vai, DecreeArea::Wealth, int(WealthStance::Share), 0.5, true);
	near(doctrine::burden(w, d.vai), (0.9 + 0.2) / 2, 1e-12);
	for (Decree& x : w.mythos.decrees) if (x.area == DecreeArea::Care) x.data["literal"] = 1;
	near(doctrine::burden(w, d.vai), (0.9 + 0.4) / 2, 1e-12, "literal");
	for (Decree& x : w.mythos.decrees) if (x.area == DecreeArea::Violence) x.data["strictAdj"] = -0.3;
	near(doctrine::burden(w, d.vai), (0.6 + 0.4) / 2, 1e-12, "bent");
}

DM_TEST("doctrine: decrees and words survive a save round trip") {
	DocWorld d = docWorld();
	World& w = *d.w;
	addMessenger(w, d.vai, d.towns[0]);
	WordOpts o; o.content = WordContent::Decree; o.area = DecreeArea::Hospitality; o.stance = 1; o.clarity = Clarity::Parable;
	if (!ok(doctrine::speak(w, d.vai, o) > 0)) return;
	w.year += 10;
	o.area = DecreeArea::Care; o.stance = 0; o.clarity = Clarity::Dream; o.ref = d.towns[2];
	if (!ok(doctrine::speak(w, d.vai, o) > 0)) return;
	setDecree(w, d.oru, DecreeArea::Worship, int(Rite::Art), 0.5, true);
	for (int i = 0; i < 5; i++) { w.year += 1; doctrine::yearly(w); }
	std::vector<uint8_t> bytes;
	std::string why;
	if (!ok(state::serialize(w, bytes, &why), why)) return;
	auto back = state::deserialize(bytes, &why);
	if (!ok(back != nullptr, why)) return;
	back->tune<MythosTune>().phase = 1;                     // tuning is never saved (Balance re-applies it)
	eq(back->mythos.decrees.size(), w.mythos.decrees.size());
	for (size_t i = 0; i < w.mythos.decrees.size() && i < back->mythos.decrees.size(); i++) {
		const Decree& a = w.mythos.decrees[i]; const Decree& b = back->mythos.decrees[i];
		ok(a.culture == b.culture && a.area == b.area && a.stance == b.stance && a.kept == b.kept && a.custom == b.custom &&
			a.by == b.by && a.data == b.data, "decree " + std::to_string(i));
	}
	eq(back->mythos.words.size(), w.mythos.words.size());
	ok(back->mythos.num == w.mythos.num, "doctrine counters");
	// and the next years go the same way
	for (int i = 0; i < 50; i++) { w.year += 1; back->year += 1; doctrine::yearly(w); doctrine::yearly(*back); }
	std::vector<uint8_t> b1, b2;
	state::serialize(w, b1); state::serialize(*back, b2);
	ok(b1 == b2, "replays identically after the load");
}

DM_TEST("doctrine: the first decree card is proposed once a culture has a shrine or is 200 years old") {
	stub::Guard g;
	ProposalLog log;
	if (!log.install()) return;
	DocWorld d = docWorld();
	World& w = *d.w;
	w.year = 100;
	w.cultures[1].born = 50;
	doctrine::yearly(w);
	eq(log.count(CardKind::FirstDecree), 0, "too young, no shrine");
	w.settlements[size_t(d.towns[1])].b[Building::SHRINE] = 1;
	w.year = 101; doctrine::yearly(w);
	eq(log.count(CardKind::FirstDecree), 1, "Vai's first shrine");
	const cards::Proposal* p = log.last(CardKind::FirstDecree);
	ok(p && p->culture == d.vai && p->settlement == d.towns[0] && p->severity > 0.3 && p->severity <= 1, "for Vai, at the capital");
	w.year = 260; doctrine::yearly(w);
	eq(log.count(CardKind::FirstDecree), 3, "re-proposed while the matter stands; Oru at its year 200");
	pushCard(w, CardKind::FirstDecree, d.vai, d.towns[0], { d.vai, -1, -1, -1 }, -1, -1, CardState::Lapsed);
	setDecree(w, d.oru, DecreeArea::Care, 0);
	const size_t before = log.got.size();
	w.year = 261; doctrine::yearly(w);
	eq(log.got.size(), before, "once a card was seen, or a decree given, never again");
}

DM_TEST("doctrine: guardrail 2 - phase 1, emergent off, a god who never speaks: the sim outside mythos is untouched") {
	stub::Guard g;
	DocWorld d = docWorld(1, false);
	World& w = *d.w;
	setWar(w, d.vai, d.oru, d.oru);
	for (Settlement& s : w.settlements) { s.famine = 3; s.plague = 0.2; s.b[Building::SHRINE] = 1; }
	w.cultures[1].parent = d.vai;
	const uint32_t rng0 = w.rng.s;
	std::vector<uint8_t> before, after;
	state::serialize(w, before, nullptr, 1);                 // the pre-mythos view
	for (int i = 0; i < 600; i++) { w.year += 1; doctrine::yearly(w); }
	// answers nobody gave: let every card the director would have opened lapse
	const int k = pushCard(w, CardKind::FirstDecree, d.vai, d.towns[0], { d.vai, -1, -1, -1 });
	doctrine::lapse(w, k);
	w.year = 300;
	state::serialize(w, after, nullptr, 1);
	eq(w.rng.s, rng0, "w.rng never drawn");
	ok(before == after, "settlements, cultures, relations, cells: identical");
	eq(int(w.mythos.decrees.size()), 0, "no custom, no decree");
	eq(int(w.mythos.words.size()), 0);
}
