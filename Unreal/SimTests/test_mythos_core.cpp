// DEMIURGE — SimTests/test_mythos_core.cpp : the mythos core's presence (SPEC-MYTHOS 3.2), aspect (3.3) and will
// (3.4) on synthetic worlds (track M1 "mythos-core"). Key: run.ps1 mythos (every other module stubbed); also in all.
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace dm;
using namespace dmt;

namespace {

// `cultures` root cultures with `towns` settlements each (100 people, devotion 0.5), far apart on land; phase 1.
std::unique_ptr<World> mythWorld(int cultures = 2, int towns = 3, int phase = 1) {
	auto w = makeWorld({ .L = 4, .seed = 7 });
	w->tune<MythosTune>().phase = phase;
	std::vector<int> land;
	for (int c = 0; c < w->n; c++) if (w->elev[size_t(c)] >= w->seaLevel) land.push_back(c);
	const int need = cultures * towns;
	const size_t stride = land.size() / size_t(need + 1);
	int k = 1;
	for (int c = 0; c < cultures; c++) {
		Culture& cu = state::newCulture(*w);
		cu.name = "C" + std::to_string(c);
		for (int t = 0; t < towns; t++, k++) {
			Settlement& s = state::newSettlement(*w, land[size_t(k) * stride], c);
			s.pop = 100; s.devotion = 0.5; s.tier = 2;
			if (t == 0) { s.capital = true; w->cultures[size_t(c)].capital = s.id; }
		}
		w->cultures[size_t(c)].pop = 100.0 * towns;
		w->cultures[size_t(c)].towns = towns;
	}
	mythos::attach(*w);
	mythos::rebuild(*w);
	return w;
}

void pass(World& w, int years = 1) {
	for (int i = 0; i < years; i++) { w.year += 1; mythos::yearly(w); }
}

double img(const World& w, int c, Aspect a) { return w.mythos.faith[size_t(c)].image[size_t(a) - 1]; }
double fade1(double hl) { return std::pow(0.5, 1.0 / hl); }

int countEvents(const std::vector<Event>& ev, Ev t) {
	int n = 0;
	for (const Event& e : ev) if (e.type == t) n++;
	return n;
}

} // namespace

// ---------------------------------------------------------------- presence (3.2)

DM_TEST("mythos presence: witness multipliers per stance; neutral at phase 0 and for the people's own fire") {
	auto w = mythWorld();
	const int cell = w->settlements[0].cell;
	const int farCell = w->settlements[5].cell;
	ok(mythos::witnessScale(*w, cell, "heal") == WitnessScale{}, "Wonders: all 1");
	w->mythos.presence = Presence::Unseen;
	WitnessScale u = mythos::witnessScale(*w, cell, "heal");
	near(u.faith, 0.25, 1e-12, "Unseen faith x 0.25");
	near(u.memory, 0.4, 1e-12, "Unseen memory x 0.4");
	near(u.favour, 0, 1e-12, "Unseen: no blame or envy");
	near(u.jaded, 0, 1e-12, "Unseen: no jaded");
	ok(mythos::witnessScale(*w, cell, "nuke") == WitnessScale{}, "nuke is not the god's act");
	w->mythos.presence = Presence::Messengers;
	near(mythos::witnessScale(*w, cell, "heal").faith, 0.6, 1e-12, "Messengers, no messenger: x 0.6");
	w->cultures[0].prophet = Prophet{ "Ossa", 0, 999, -1 };
	near(mythos::witnessScale(*w, cell, "heal").faith, 1.5, 1e-12, "near the messenger: x 1.5");
	near(mythos::witnessScale(*w, farCell, "heal").faith, 0.6, 1e-12, "far from the messenger: x 0.6");
	w->tune<MythosTune>().phase = 0;
	ok(mythos::witnessScale(*w, cell, "heal") == WitnessScale{}, "phase 0: neutral whatever the stance");
}

DM_TEST("mythos presence: Walk refused, one change per age, paid through powers::pay, disputes proposed") {
	stub::Guard g(true);
	auto w = mythWorld();
	ok(!mythos::setPresence(*w, Presence::Walk), "Walk is always refused");
	ok(!mythos::setPresence(*w, Presence::Wonders), "the same stance is refused");
	eq(int(w->mythos.presence), int(Presence::Wonders), "nothing changed");
	if (!stub::stubbed("powers") || !stub::stubbed("cards")) return;
	ok(!mythos::setPresence(*w, Presence::Unseen), "the neutral powers::pay refuses: nothing changes");
	eq(int(w->mythos.presence), int(Presence::Wonders), "still Wonders");
	double paid = 0;
	stub::hooks().powers_pay = [&](World&, std::string_view, double amount) { paid += amount; return true; };
	std::vector<cards::Proposal> props;
	stub::hooks().cards_propose = [&](World&, const cards::Proposal& p) { props.push_back(p); return true; };
	w->mythos.faith[0].agency = 0.5;         // only culture 0 argues (agency >= 0.4)
	w->mythos.faith[1].agency = 0.2;
	w->year = 300;
	const auto ev = capture(*w, [&] { ok(mythos::setPresence(*w, Presence::Unseen), "paid change"); });
	near(paid, 100, 1e-12, "presenceCost 100");
	eq(int(w->mythos.presence), int(Presence::Unseen), "now Unseen");
	near(w->mythos.presenceSince, 300, 1e-12, "presenceSince");
	eq(countEvents(ev, Ev::Presence), 1, "one presence event");
	if (const Event* e = ev.empty() ? nullptr : &ev.back(); e && e->as<EvPresence>()) {
		eq(e->as<EvPresence>()->presence, std::string("unseen"), "event names the stance");
		eq(e->as<EvPresence>()->prev, std::string("wonders"), "and the previous one");
	}
	eq(int(props.size()), 1, "one Dispute proposal, for the culture with agency >= 0.4");
	if (!props.empty()) { eq(int(props[0].kind), int(CardKind::Dispute), "a Dispute"); eq(props[0].culture, 0, "culture 0"); }
	ok(!mythos::setPresence(*w, Presence::Messengers), "a second change in the same age is refused");
	w->age = 1;
	ok(mythos::setPresence(*w, Presence::Messengers), "the next age allows one");
	near(paid, 200, 1e-12, "paid twice");
	ok(w->mythos.faith[1].data.count("m_touched") == 1, "a presence change is a mythos act on every people");
	w->tune<MythosTune>().phase = 0;
	w->age = 2;
	ok(!mythos::setPresence(*w, Presence::Wonders), "phase 0 refuses");
}

DM_TEST("mythos presence: under Unseen an act adds Mystery and x 0.4 of its Parent impression") {
	for (int unseen = 0; unseen < 2; unseen++) {
		auto w = mythWorld();
		if (unseen) w->mythos.presence = Presence::Unseen;
		const Settlement& s = w->settlements[0];
		w->bus.emit(EvMiracleWitnessed{ s.cell, "heal", 0.8, { s.id } });
		pass(*w);
		const double share = 1.0 / 3.0, f = fade1(300);
		near(img(*w, 0, Aspect::Parent), 0.1 * share * (unseen ? 0.4 : 1.0) * f, 1e-9, unseen ? "Unseen Parent x 0.4" : "Wonders Parent 0.1 x share");
		near(img(*w, 0, Aspect::Mystery), unseen ? 0.1 * share * f : 0.0, 1e-9, unseen ? "Unseen adds Mystery 0.1 x share" : "no Mystery for a seen deed");
		near(w->mythos.faith[0].visible, (unseen ? 0.25 : 1.0) * std::pow(0.5, 1.0 / 100), 1e-9, "visible counts the deed (x 0.25 unseen)");
	}
}

DM_TEST("mythos presence: under Messengers a miracle near the messenger raises its credibility") {
	stub::Guard g(true);
	if (!stub::stubbed("characters")) return;
	auto w = mythWorld();
	w->mythos.presence = Presence::Messengers;
	w->cultures[0].prophet = Prophet{ "Ossa", 0, 999, 4 };
	double moved = 0; int who = -1;
	stub::hooks().characters_adjustCredibility = [&](World&, int id, double d) { who = id; moved += d; return true; };
	const Settlement& s = w->settlements[0];
	w->bus.emit(EvMiracleWitnessed{ s.cell, "heal", 0.8, { s.id } });
	pass(*w);
	eq(who, 4, "the messenger's figure");
	near(moved, 0.05, 1e-12, "msgCred +0.05");
	near(img(*w, 0, Aspect::Parent), 0.1 / 3.0 * 1.5 * fade1(300), 1e-9, "Parent x 1.5 through the messenger");
}

// ---------------------------------------------------------------- aspect (3.3)

DM_TEST("mythos aspect: impressions per act (grace, prayer, wrath, just punishment, words, sentences, pacts)") {
	auto w = mythWorld();
	const double share = 1.0 / 3.0, f = fade1(300);
	const Settlement& a = w->settlements[0];
	const Settlement& b = w->settlements[1];
	w->bus.emit(EvPrayerAnswered{ a.id, PrayerKind::Rain });
	w->bus.emit(EvMiracleWitnessed{ b.cell, "lightning", -1, { b.id } });
	pass(*w);
	near(img(*w, 0, Aspect::Parent), 0.3 * share * f, 1e-9, "an answered prayer: Parent 0.3 x share");
	near(img(*w, 0, Aspect::Threat), 0.4 * share * f, 1e-9, "wrath on a town that broke nothing: Threat 0.4 x share x harm");
	// a breach, then wrath on that town: just punishment (Ruler 0.3)
	auto w2 = mythWorld();
	const Settlement& c = w2->settlements[2];
	w2->bus.emit(EvBreach{ 0, "violence", c.id, -1, 0.3 });
	w2->bus.emit(EvMiracleWitnessed{ c.cell, "lightning", -1, { c.id } });
	pass(*w2);
	near(img(*w2, 0, Aspect::Ruler), 0.3 * share * f, 1e-9, "just punishment of a breach: Ruler 0.3 x share");
	near(img(*w2, 0, Aspect::Threat), 0, 1e-12, "and no Threat");
	// words by clarity (3.9 table), a catastrophe (Threat +0.2)
	auto w3 = mythWorld(4);
	w3->bus.emit(EvWord{ 1, 0, "answer", "exact", "messenger", "heard" });
	w3->bus.emit(EvWord{ 2, 1, "answer", "parable", "messenger", "wisdom" });
	w3->bus.emit(EvWord{ 3, 2, "answer", "sign", "sky", "catastrophe" });
	w3->bus.emit(EvWord{ 4, 3, "answer", "dream", "dream", "heard" });
	pass(*w3);
	near(img(*w3, 0, Aspect::Ruler), 0.5 * f, 1e-9, "Exact: Ruler +0.5");
	near(img(*w3, 1, Aspect::Parent), 0.2 * f, 1e-9, "Parable: Parent +0.2");
	near(img(*w3, 2, Aspect::Mystery), (0.3 + 0.002) * f, 1e-9, "Sign: Mystery +0.3 (and +0.002: a word is not a witnessed deed)");
	near(img(*w3, 2, Aspect::Threat), 0.2 * f, 1e-9, "a catastrophe: Threat +0.2");
	near(img(*w3, 3, Aspect::Parent), 0.2 * f, 1e-9, "Dream: Parent +0.2");
	// sentences and pacts
	auto w4 = mythWorld(3);
	w4->year = 500;
	w4->bus.emit(EvJudgement{ 9, 0, "heart", "forgive" });
	w4->bus.emit(EvJudgement{ 9, 1, "deed", "remove" });
	w4->bus.emit(EvPact{ 3, 2, "sworn" });
	pass(*w4);
	const Faith& f0 = w4->mythos.faith[0];
	const Faith& f1 = w4->mythos.faith[1];
	near(img(*w4, 0, Aspect::Parent), 0.3 * f, 1e-9, "Forgive: Parent +0.3");
	near(f0.mercy, std::pow(0.5, 1.0 / 500), 1e-9, "mercy +1 (fading, half-life 500)");
	near(f0.survivorUntil, 571, 1e-9, "survivors remember for 70 years");
	near(f0.survivorLove, 0.05, 1e-12, "Forgive's survivors: love target +0.05");
	near(f0.judgedBy, 0.25 * std::pow(0.5, 1.0 / 300), 1e-9, "judged by the heart: +0.25");
	near(img(*w4, 1, Aspect::Ruler), (0.3 + 0.2) * f, 1e-9, "Remove the ruler, by the deed: Ruler 0.3 + 0.2");
	near(f1.sternness, std::pow(0.5, 1.0 / 500), 1e-9, "sternness +1");
	near(f1.survivorFear, 0.06, 1e-12, "Remove's survivors: obedience +0.06");
	ok(f1.judgedBy < -0.2, "judged by the deed");
	near(img(*w4, 2, Aspect::Ruler), 0.2 * f, 1e-9, "a pact sworn: Ruler +0.2");
	for (int ci = 0; ci < 3; ci++) ok(w4->mythos.faith[size_t(ci)].data.count("m_touched") == 1, "a sentence or pact touches the people");
	// survivors end after 70 years
	pass(*w4, 70);
	near(w4->mythos.faith[0].survivorUntil, -1, 1e-12, "the last of the survivors died");
	near(w4->mythos.faith[0].survivorLove, 0, 1e-12, "their memory is gone");
}

DM_TEST("mythos aspect: impressions fade with half-life 300; Unknown until one leads; hysteresis of 40 years") {
	auto w = mythWorld(1);
	Faith& f = w->mythos.faith[0];
	ok(f.aspect == Aspect::Unknown, "Unknown at first");
	f.image = { 0.1, 0.09, 0, 0 };
	f.data["m_lastDeed"] = 1e9;              // no Mystery drift (a deed "every year")
	pass(*w);
	ok(f.aspect == Aspect::Unknown, "below aspectMin: Unknown");
	f.image = { 0.4, 0.0, 0, 0 };
	auto ev = capture(*w, [&] { pass(*w); });
	ok(f.aspect == Aspect::Parent, "Parent leads");
	eq(countEvents(ev, Ev::Aspect), 1, "one aspect event");
	f.image = { 0.4, 0.45, 0, 0 };
	pass(*w);
	ok(f.aspect == Aspect::Parent, "Ruler is ahead but not by 1.2 x");
	f.image = { 0.2, 0.8, 0, 0 };
	ev = capture(*w, [&] { pass(*w); });
	ok(f.aspect == Aspect::Parent, "held: fewer than 40 years since the last change");
	eq(countEvents(ev, Ev::Aspect), 0, "no event while held");
	int events = 0;
	ev = capture(*w, [&] { pass(*w, 40); });
	events = countEvents(ev, Ev::Aspect);
	ok(f.aspect == Aspect::Ruler, "after 40 years Ruler takes it");
	eq(events, 1, "exactly one more aspect event");
	// fading over 300 years
	auto w2 = mythWorld(1);
	Faith& g = w2->mythos.faith[0];
	g.image = { 1.0, 0, 0, 0 };
	g.data["m_lastDeed"] = 1e9;
	pass(*w2, 300);
	near(g.image[0], 0.5, 1e-9, "half after 300 years");
}

DM_TEST("mythos aspect: a passive god drifts into Mystery (0.2 per 100 years without a deed)") {
	auto w = mythWorld(1);
	pass(*w, 100);
	const double f = fade1(300);
	double want = 0;
	for (int i = 0; i < 100; i++) want = (want + 0.002) * f;
	near(img(*w, 0, Aspect::Mystery), want, 1e-9, "Mystery from silence");
	ok(w->mythos.faith[0].aspect == Aspect::Mystery, "after a silent century they see a mystery");
	ok(mythos::factors(*w, 0) == Factors{}, "and nothing in the sim moves (no mythos act, emergent off)");
}

// ---------------------------------------------------------------- will (3.4)

DM_TEST("mythos will: each target term (drift 1 shows the targets)") {
	auto mk = [](auto setup) {
		auto w = mythWorld(1);
		MythosTune& T = w->tune<MythosTune>();
		T.agencyDrift = 1; T.obedienceDrift = 1; T.dependenceDrift = 1;
		w->mythos.faith[0].data["m_lastDeed"] = 1e9;
		setup(*w);
		pass(*w);
		return w;
	};
	// the base: clarity 0.5, nothing seen, curiosity 0.5, no academies, dependence 0.3, devotion 0.5, agency 0.3
	auto w = mk([](World&) {});
	const Faith& f = w->mythos.faith[0];
	near(f.agency, 0.25 + 0.35 * 0.5 + 0.25 + 0.1 * 0.5 - 0.15 * 0.3, 1e-9, "agency target");
	near(f.obedience, 0.30 + 0.35 * 0.5, 1e-9, "obedience target");
	near(f.dependence, 0.15 - 0.30 * 0.3 + 0.1 * 0.5, 1e-9, "dependence target (compassion +0.1 c)");
	// presence
	near(mk([](World& x) { x.mythos.presence = Presence::Unseen; })->mythos.faith[0].agency, f.agency + 0.15, 1e-9, "Unseen +0.15");
	near(mk([](World& x) { x.mythos.presence = Presence::Messengers; })->mythos.faith[0].agency, f.agency + 0.05, 1e-9, "Messengers +0.05");
	// literacy: academies everywhere
	near(mk([](World& x) { for (Settlement& s : x.settlements) s.b[Building::ACADEMY] = 1; })->mythos.faith[0].agency,
		f.agency + 0.15 * 0.5, 1e-9, "literacy 0.5 x 0.15");
	// clarity: Exact words lower agency
	auto ex = mk([](World& x) { for (int i = 0; i < 3; i++) x.bus.emit(EvWord{ i, 0, "answer", "exact", "messenger", "heard" }); });
	const double cl = (3 * 1.0 * std::pow(0.5, 1.0 / 200) + 0.5) / (3 * std::pow(0.5, 1.0 / 200) + 1);
	near(ex->mythos.faith[0].clarity, cl, 1e-9, "fading mean clarity with a 0.5 prior");
	ok(ex->mythos.faith[0].aspect == Aspect::Ruler, "Exact words make a Ruler");
	near(ex->mythos.faith[0].agency, f.agency - 0.35 * (cl - 0.5) - 0.05, 1e-9, "clearer words: less agency (and the Ruler term)");
	// visible deeds
	auto seen = mk([](World& x) { x.mythos.faith[0].visible = 20.0 / std::pow(0.5, 1.0 / 100); });
	near(seen->mythos.faith[0].agency, f.agency - 0.25, 1e-9, "a god seen 20 times: -0.25");
	// aspect terms (Ruler: agency -0.05, obedience +0.1)
	auto ru = mk([](World& x) { x.mythos.faith[0].aspect = Aspect::Ruler; x.mythos.faith[0].image = { 0, 0.5, 0, 0 }; });
	near(ru->mythos.faith[0].agency, f.agency - 0.05, 1e-9, "Ruler agency -0.05");
	near(ru->mythos.faith[0].obedience, f.obedience + 0.1, 1e-9, "Ruler obedience +0.1");
	auto pa = mk([](World& x) { x.mythos.faith[0].aspect = Aspect::Parent; x.mythos.faith[0].image = { 0.5, 0, 0, 0 }; });
	near(pa->mythos.faith[0].dependence, f.dependence + 0.1, 1e-9, "Parent dependence +0.1");
	// decrees' burden and count (obedience uses last year's agency 0.3)
	auto dc = mk([](World& x) {
		for (int a = 0; a < 5; a++) { Decree d; d.culture = 0; d.area = DecreeArea(a == 2 ? 6 : a); d.stance = 0; d.kept = 1; x.mythos.decrees.push_back(d); }
	});
	double sumStrict = 0;
	for (int a : { 0, 1, 6, 3, 4 }) sumStrict += dc->tune<DoctrineTune>().strictness[size_t(a)][0];
	near(dc->mythos.faith[0].obedience, f.obedience - 0.45 * 0.3 * (sumStrict / 5) - 0.04 * 2, 1e-9, "burden and more than 3 decrees");
	// answered prayers (dependence)
	auto pr = mk([](World& x) { for (int i = 0; i < 8; i++) x.bus.emit(EvPrayerAnswered{ i % 3, PrayerKind::Rain }); });
	ok(pr->mythos.faith[0].aspect == Aspect::Parent, "answered prayers make a Parent");
	near(pr->mythos.faith[0].dependence, f.dependence + 0.35 * sat(8 * std::exp(-1.0 / 100) / 8) + 0.1, 1e-9, "answered prayers raise dependence (and the Parent term)");
	// judged by deeds
	auto jd = mk([](World& x) { x.mythos.faith[0].judgedBy = -0.9; });
	near(jd->mythos.faith[0].obedience, f.obedience + 0.05, 1e-9, "judged by deeds: obedience +0.05");
	near(jd->mythos.faith[0].agency, f.agency - 0.03, 1e-9, "and agency -0.03");
	// survivors' fear
	auto sv = mk([](World& x) { x.mythos.faith[0].survivorFear = 0.06; x.mythos.faith[0].survivorUntil = 9999; });
	near(sv->mythos.faith[0].obedience, f.obedience + 0.06, 1e-9, "survivors: obedience +0.06");
}

DM_TEST("mythos will: drift, bounds, no NaN, zero cultures") {
	auto w = mythWorld(1);
	Faith& f = w->mythos.faith[0];
	const double a0 = f.agency;
	pass(*w);
	const double aT = 0.25 + 0.35 * 0.5 + 0.25 + 0.1 * 0.5 - 0.15 * 0.3;
	near(f.agency, a0 + (aT - a0) * 0.01, 1e-9, "agency drifts 1 % of the gap a year");
	pass(*w, 2000);
	ok(f.agency >= 0.6, "a passive god's people think for themselves: agency " + std::to_string(f.agency));
	for (double v : { f.agency, f.obedience, f.dependence }) ok(v >= 0 && v <= 1, "bounded");
	// NaN inputs
	w->settlements[0].devotion = std::nan("");
	w->settlements[1].pop = std::numeric_limits<double>::infinity();
	w->cultures[0].traits.curiosity = std::nan("");
	pass(*w, 3);
	w->settlements[0].devotion = 0.5; w->settlements[1].pop = 100; w->cultures[0].traits.curiosity = 0.5;   // the test's own bad values
	eq(checkFinite(*w), std::string(""), "no NaN written");
	noNaN(*w, "after bad inputs");
	auto z = makeWorld({ .L = 3, .seed = 2 });
	z->tune<MythosTune>().phase = 1;
	mythos::attach(*z);
	mythos::rebuild(*z);
	z->year = 5; mythos::yearly(*z);
	ok(z->mythos.faith.empty(), "zero cultures: nothing to keep");
	eq(int(mythos::legacy(*z)), int(Legacy::None), "no ending: no legacy");
}

DM_TEST("mythos acts: handlers only record; the cap keeps the newest; attach subscribes once; phase 0 records nothing") {
	auto w = mythWorld();
	const int subs = w->bus.count();
	mythos::attach(*w);
	eq(w->bus.count(), subs, "re-attach replaces the subscriptions");
	const Settlement& s = w->settlements[0];
	const double dev = s.devotion;
	w->bus.emit(EvMiracleWitnessed{ s.cell, "heal", 0.8, { s.id, 1 } });
	w->bus.emit(EvMiracleWitnessed{ s.cell, "quake", 0, { s.id } });       // natural (good 0): not an act
	w->bus.emit(EvMiracleWitnessed{ s.cell, "nuke", -1, { s.id } });       // the people's own fire
	eq(int(w->mythos.acts.size()), 2, "one act per witnessing settlement of the god's act");
	near(s.devotion, dev, 0, "a handler never acts");
	for (int i = 0; i < 120; i++) w->bus.emit(EvMiracleWitnessed{ s.cell, "heal", 0.5, { s.id } });
	w->bus.emit(EvWord{ 1, 0, "answer", "sign", "sky", "heard" });
	for (int i = 0; i < 120; i++) w->bus.emit(EvMiracleWitnessed{ s.cell, "heal", 0.5, { s.id } });
	eq(int(w->mythos.acts.size()), 96, "at most 96 acts");
	bool wordKept = false;
	for (const MythAct& a : w->mythos.acts) wordKept = wordKept || a.kind.rfind("word:", 0) == 0;
	ok(wordKept, "witness acts are dropped before the god's words");
	pass(*w);
	ok(w->mythos.acts.empty(), "the yearly pass consumes them");
	auto z = mythWorld(2, 3, 0);
	const int zs = z->bus.count();
	mythos::attach(*z);
	z->bus.emit(EvMiracleWitnessed{ z->settlements[0].cell, "heal", 0.8, { 0 } });
	ok(z->mythos.acts.empty() && z->bus.count() == zs, "phase 0: no subscription, no record");
}

DM_TEST("mythos acts: a Disaster is just by Doctrine's card keys; a repeat's forgiveness; decree conflicts' lines") {
	auto w = mythWorld(4);
	const double f = fade1(300);
	auto card = [&](int culture, std::initializer_list<std::pair<const char*, double>> data, int ref0 = -1) {
		Card c; c.id = w->mythos.nextId++; c.kind = CardKind::Judgement; c.state = uint8_t(CardState::Answered); c.culture = culture;
		c.ref[0] = ref0;
		for (const auto& kv : data) c.data[kv.first] = kv.second;
		w->mythos.cards.push_back(c);
		return c.id;
	};
	const int unjustDeed = card(0, { { "deedWrong", 0 }, { "heartWrong", 1 } });
	const int justHeart = card(1, { { "deedWrong", 0 }, { "heartWrong", 1 } });
	const int repeat = card(2, { { "repeat", 1 } });
	w->bus.emit(EvJudgement{ unjustDeed, 0, "deed", "disaster" });
	w->bus.emit(EvJudgement{ justHeart, 1, "heart", "disaster" });
	w->bus.emit(EvJudgement{ repeat, 2, "heart", "forgive" });
	const double ob2 = w->mythos.faith[2].obedience;
	pass(*w);
	near(img(*w, 0, Aspect::Threat), 0.4 * f, 1e-9, "judged by the deed, and the deed was not wrong: unjust, Threat +0.4");
	near(w->mythos.faith[0].survivorLove, -0.1, 1e-12, "unjust: survivors' love -0.1");
	near(img(*w, 1, Aspect::Ruler), 0.3 * f, 1e-9, "judged by the heart, and the heart was wrong: just, Ruler +0.3");
	near(w->mythos.faith[1].survivorFear, 0.08, 1e-12, "just: survivors' obedience +0.08");
	ok(w->mythos.faith[2].obedience < ob2 + 0.02 - 0.029, "forgiving a repeat costs obedience (forgiveRepeat -0.03 before drift)");
	// decree conflicts (3.7): the raided weak held the gate (Ruler +0.2); the hidden stranger kept the truth
	// (obedience +0.05, judged by deeds); a lapse: obedience -0.05
	auto conflict = [&](int culture, int kase) {
		Card c; c.id = w->mythos.nextId++; c.kind = CardKind::DecreeConflict; c.culture = culture; c.ref[0] = kase;
		c.state = uint8_t(CardState::Answered);
		w->mythos.cards.push_back(c);
		return c.id;
	};
	const int k0 = conflict(3, 0), k1 = conflict(3, 1), k2 = conflict(2, 4);
	const double img3 = img(*w, 3, Aspect::Ruler);
	const double jb3 = w->mythos.faith[3].judgedBy;
	w->bus.emit(EvCard{ k0, "decree-conflict", "answered", 3, 0 });
	w->bus.emit(EvCard{ k1, "decree-conflict", "answered", 3, 1 });
	w->bus.emit(EvCard{ k2, "decree-conflict", "lapsed", 2, -1 });
	const double o2 = w->mythos.faith[2].obedience;
	pass(*w);
	near(img(*w, 3, Aspect::Ruler), (img3 + 0.2) * f, 1e-9, "hold the gate: Ruler +0.2");
	near(w->mythos.faith[3].judgedBy, (jb3 - 0.25) * std::pow(0.5, 1.0 / 300), 1e-9, "truth whatever it costs: judged by deeds");
	ok(w->mythos.faith[3].data.count("m_touched") == 1, "an answered card touches the people");
	ok(w->mythos.faith[2].obedience < o2 - 0.04, "a lapsed conflict: obedience -0.05");
}
