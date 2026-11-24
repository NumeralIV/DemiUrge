// DEMIURGE — SimTests/test_doctrine_conflicts.cpp : when decrees collide (SPEC-MYTHOS 3.7) and the FirstDecree card.
// Track M3. Case numbers are Doctrine.cpp's Case order (the 3.7 table): 0 raided weak, 1 hidden stranger,
// 2 temple granary, 3 enemy at the gate, 4 wood or children (v2), 5 altar or bread, 6 clever and true, 7 strong and weak.
#include "test_doctrine_fixture.h"

using namespace dm;
using namespace dmt;
using namespace dmt::doc;

namespace {
// Run one yearly pass and return the DecreeConflict proposal's case, or -1.
int caseNow(World& w, ProposalLog& log) {
	log.got.clear();
	w.year += 1;
	doctrine::yearly(w);
	const cards::Proposal* p = log.last(CardKind::DecreeConflict);
	return p ? p->ref[0] : -1;
}
DocWorld obedient() {
	DocWorld d = docWorld();
	for (Faith& f : d.w->mythos.faith) f.obedience = 1;     // no breaches interfere
	return d;
}
}

DM_TEST("doctrine conflicts: each case's trigger proposes its card") {
	stub::Guard g;
	ProposalLog log;
	if (!log.install()) return;
	{   // raided weak: Peace x Shelter, attacked
		DocWorld d = obedient(); World& w = *d.w;
		setDecree(w, d.vai, DecreeArea::Violence, int(ViolenceStance::Peace));
		setDecree(w, d.vai, DecreeArea::Care, int(CareStance::Shelter));
		eq(caseNow(w, log), -1, "no trigger");
		setWar(w, d.vai, d.oru, d.vai);
		eq(caseNow(w, log), -1, "they started it: not raided");
		setWar(w, d.vai, d.oru, d.oru);
		eq(caseNow(w, log), 0, "raided weak");
		const cards::Proposal* p = log.last(CardKind::DecreeConflict);
		ok(p && p->other == d.oru && p->culture == d.vai && p->severity > 0, "against the Oru");
		for (Decree& x : w.mythos.decrees) if (x.area == DecreeArea::Care) { x.kept = 0.3; x.data["fidelity"] = 0.3; }
		w.mythos.faith[0].obedience = 1;
		eq(caseNow(w, log), -1, "a decree kept below 0.4 cannot collide");
	}
	{   // hidden stranger: Truth x Open, a war in contact
		DocWorld d = obedient(); World& w = *d.w;
		setDecree(w, d.vai, DecreeArea::Honesty, int(HonestyStance::Truth));
		setDecree(w, d.vai, DecreeArea::Hospitality, int(HospitalityStance::Open));
		setWar(w, d.vai, d.oru, d.vai);
		eq(caseNow(w, log), 1, "hidden stranger");
	}
	{   // temple granary: Tithe x Shelter, famine while the House is rich
		DocWorld d = obedient(); World& w = *d.w;
		setDecree(w, d.vai, DecreeArea::Wealth, int(WealthStance::Tithe));
		setDecree(w, d.vai, DecreeArea::Care, int(CareStance::Shelter));
		w.settlements[size_t(d.towns[2])].famine = 1;
		eq(caseNow(w, log), -1, "a poor House: no case");
		House h; h.culture = d.vai; h.wealth = 0.6; w.mythos.houses.push_back(h);
		eq(caseNow(w, log), 2, "temple granary");
		eq(log.last(CardKind::DecreeConflict)->settlement, d.towns[2], "the hungry town");
	}
	{   // the enemy at the gate: Open x Defence, an army of a people in contact near a town
		DocWorld d = obedient(); World& w = *d.w;
		setDecree(w, d.vai, DecreeArea::Hospitality, int(HospitalityStance::Open));
		setDecree(w, d.vai, DecreeArea::Violence, int(ViolenceStance::Defence));
		w.vars.rel[state::relKey(d.vai, d.oru)].contact = true;
		Unit& u = state::newUnit(w, UnitType::Army);
		u.culture = d.oru; u.path = { w.settlements[size_t(d.towns[1])].cell }; u.i = 0;
		eq(caseNow(w, log), 3, "enemy at the gate");
		eq(log.last(CardKind::DecreeConflict)->settlement, d.towns[1]);
	}
	{   // the altar or the bread: Temples x Share, a Wonder due while a town hungers
		DocWorld d = obedient(); World& w = *d.w;
		setDecree(w, d.vai, DecreeArea::Worship, int(Rite::Temples));
		setDecree(w, d.vai, DecreeArea::Wealth, int(WealthStance::Share));
		w.settlements[size_t(d.towns[1])].famine = 2;
		w.cultures[0].edict = Edict::Build; w.cultures[0].edictSince = w.year - 5;
		eq(caseNow(w, log), 5, "altar or bread");
	}
	{   // the clever and the true: a daughter's Cunning custom against its parent's Truth
		DocWorld d = obedient(); World& w = *d.w;
		w.cultures[1].parent = d.vai; w.cultures[1].born = w.year;
		setDecree(w, d.vai, DecreeArea::Honesty, int(HonestyStance::Truth));
		setDecree(w, d.oru, DecreeArea::Honesty, int(HonestyStance::Cunning), 0.5, true);
		eq(caseNow(w, log), 6, "clever and true (the daughter's card)");
		ok(log.last(CardKind::DecreeConflict)->culture == d.oru && log.last(CardKind::DecreeConflict)->other == d.vai, "the daughter, against the parent");
		w.year += 20;
		eq(caseNow(w, log), -1, "only at the schism");
	}
	{   // the strong and the weak: Strength x Kindness, plague
		DocWorld d = obedient(); World& w = *d.w;
		setDecree(w, d.vai, DecreeArea::Care, int(CareStance::Strength));
		setDecree(w, d.vai, DecreeArea::Worship, int(Rite::Kindness));
		w.settlements[size_t(d.towns[2])].plague = 0.4;
		eq(caseNow(w, log), 7, "strong and weak");
	}
}

DM_TEST("doctrine conflicts: one open case per culture; the same case again only after 150 years") {
	stub::Guard g;
	ProposalLog log;
	if (!log.install()) return;
	DocWorld d = obedient(); World& w = *d.w;
	setDecree(w, d.vai, DecreeArea::Violence, int(ViolenceStance::Peace));
	setDecree(w, d.vai, DecreeArea::Care, int(CareStance::Shelter));
	setWar(w, d.vai, d.oru, d.oru);
	const int k = pushCard(w, CardKind::DecreeConflict, d.vai, d.towns[0], { 5, -1, -1, -1 });
	eq(caseNow(w, log), -1, "another case is open");
	for (Card& c : w.mythos.cards) if (c.id == k) { c.state = uint8_t(CardState::Answered); c.ref[0] = 0; c.opened = w.year - 100; }
	eq(caseNow(w, log), -1, "the same case opened 100 years ago");
	for (Card& c : w.mythos.cards) if (c.id == k) c.opened = w.year - 151;
	w.mythos.num.clear();                                    // forget the remembered year (it came from the card)
	eq(caseNow(w, log), 0, "151 years later: again");
	// the remembered opening outlives the card itself
	for (Card& c : w.mythos.cards) if (c.id == k) c.opened = w.year - 20;
	caseNow(w, log);
	w.mythos.cards.clear();
	eq(caseNow(w, log), -1, "closed cards are dropped, the record is not");
}

DM_TEST("doctrine conflicts: answers bend the decrees; a literal decree cannot bend") {
	stub::Guard g(true);
	DocWorld d = obedient(); World& w = *d.w;
	setDecree(w, d.vai, DecreeArea::Violence, int(ViolenceStance::Peace));
	setDecree(w, d.vai, DecreeArea::Care, int(CareStance::Shelter));
	setWar(w, d.vai, d.oru, d.oru);
	int k = pushCard(w, CardKind::DecreeConflict, d.vai, d.towns[0], { 0, d.oru, d.towns[0], -1 }, -1, d.oru);
	auto o = doctrine::options(w, k);
	if (!eq(int(o.size()), 3)) return;
	ok(o[0].ok && o[1].ok && o[2].ok, "three answers");
	eq(o[2].cost, 80.0, "a sign between them: 80");
	ok(doctrine::answer(w, k, 0), "the gate may be held");
	eq(int(doctrine::decree(w, d.vai, DecreeArea::Violence)->stance), int(ViolenceStance::Defence), "Peace acts as Defence");
	for (int i = 0; i < 49; i++) { w.year += 1; doctrine::yearly(w); }
	eq(int(doctrine::decree(w, d.vai, DecreeArea::Violence)->stance), int(ViolenceStance::Defence), "for 50 years");
	w.year += 1; doctrine::yearly(w);
	eq(int(doctrine::decree(w, d.vai, DecreeArea::Violence)->stance), int(ViolenceStance::Peace), "then Peace again");
	if (stub::stubbed("diplomacy")) {
		int peace = 0;
		stub::hooks().society_makePeace = [&](World&, int, int, double, int) { peace++; };
		stub::hooks().society_atWar = [](const World&, int, int) { return true; };
		ok(doctrine::answer(w, k, 2), "a sign between them");
		eq(peace, 1, "the Omen's calm");
	}
	// a literal Peace loses its bending answer
	for (Decree& x : w.mythos.decrees) if (x.area == DecreeArea::Violence) x.data["literal"] = 1;
	o = doctrine::options(w, k);
	ok(!o[0].ok, "kept by the letter: no bending");
	ok(o[1].ok, "the others stay");
	ok(!doctrine::answer(w, k, 0), "refused");
	// the hidden stranger: the lie that saves softens Truth for good
	setDecree(w, d.vai, DecreeArea::Honesty, int(HonestyStance::Truth));
	k = pushCard(w, CardKind::DecreeConflict, d.vai, d.towns[0], { 1, d.oru, d.towns[0], -1 }, -1, d.oru);
	const double b0 = doctrine::burden(w, d.vai);
	ok(doctrine::answer(w, k, 0));
	near(doctrine::burden(w, d.vai), b0 - 0.3 / 3, 1e-9, "Truth's strictness -0.3");
	// the gate shuts: Open acts as Guarded for 40 years
	setDecree(w, d.vai, DecreeArea::Hospitality, int(HospitalityStance::Open));
	k = pushCard(w, CardKind::DecreeConflict, d.vai, d.towns[1], { 3, d.oru, d.towns[1], -1 }, -1, d.oru);
	o = doctrine::options(w, k);
	ok(!o[2].ok, "two answers only");
	ok(doctrine::answer(w, k, 1));
	eq(int(doctrine::decree(w, d.vai, DecreeArea::Hospitality)->stance), int(HospitalityStance::Guarded));
	// carry them: Strength acts as Kin
	setDecree(w, d.vai, DecreeArea::Care, int(CareStance::Strength));
	k = pushCard(w, CardKind::DecreeConflict, d.vai, d.towns[2], { 7, -1, d.towns[2], -1 });
	ok(doctrine::answer(w, k, 1));
	eq(int(doctrine::decree(w, d.vai, DecreeArea::Care)->stance), int(CareStance::Kin));
	ok(!doctrine::answer(w, k, 5), "bad choice");
	noNaN(w);
}

DM_TEST("doctrine conflicts: a lapse lets the stricter decree win") {
	DocWorld d = obedient(); World& w = *d.w;
	setDecree(w, d.vai, DecreeArea::Violence, int(ViolenceStance::Peace), 0.8);
	setDecree(w, d.vai, DecreeArea::Care, int(CareStance::Shelter), 0.8);
	const int k = pushCard(w, CardKind::DecreeConflict, d.vai, d.towns[0], { 0, d.oru, d.towns[0], -1 });
	doctrine::lapse(w, k);
	near(doctrine::kept(w, d.vai, DecreeArea::Violence), 0.8, 1e-12, "Peace (0.9) wins");
	near(doctrine::kept(w, d.vai, DecreeArea::Care), 0.7, 1e-12, "Shelter (0.8) gives way: kept - 0.1");
}

DM_TEST("doctrine first decree: answers and the lapse rule") {
	stub::Guard g;
	for (int em = 0; em < 2; em++) {
		DocWorld d = docWorld(1, em == 1); World& w = *d.w;
		const int k = pushCard(w, CardKind::FirstDecree, d.vai, d.towns[0], { d.vai, -1, -1, -1 });
		auto o = doctrine::options(w, k);
		if (!eq(int(o.size()), 2)) return;
		near(o[0].cost, 30, 1e-9, "give one decree: a dream without a messenger");
		ok(o[1].ok && o[1].cost == 0, "they will find their own way");
		doctrine::lapse(w, k);
		eq(int(w.mythos.decrees.size()), em, em ? "emergent: they take a custom" : "emergent off: nothing");
	}
	DocWorld d = docWorld(); World& w = *d.w;
	addMessenger(w, d.vai, d.towns[0]);
	int k = pushCard(w, CardKind::FirstDecree, d.vai, d.towns[0], { d.vai, -1, -1, -1 });
	near(doctrine::options(w, k)[0].cost, 50, 1e-9, "a parable through the messenger");
	ok(doctrine::answer(w, k, 0), "give one decree");
	int held = 0;
	for (const Decree& x : w.mythos.decrees) if (x.culture == d.vai && !x.custom) held++;
	ok(held == 1 || (held == 0 && WordOutcome(w.mythos.words.back().outcome) == WordOutcome::Catastrophe), "a decree was given");
	eq(int(w.mythos.words.size()), 1, "as a word");
	k = pushCard(w, CardKind::FirstDecree, d.oru, d.oruTown, { d.oru, -1, -1, -1 });
	ok(doctrine::answer(w, k, 1), "their own way (the god's choice: not emergent-gated)");
	ok(doctrine::decree(w, d.oru, DecreeArea::Worship) && doctrine::decree(w, d.oru, DecreeArea::Worship)->custom, "a custom: the rite");
	// a decree spoken with the Word since the card opened answers choice 0 for free
	k = pushCard(w, CardKind::FirstDecree, d.vai, d.towns[0], { d.vai, -1, -1, -1 }, -1, -1, CardState::Open, w.year - 1);
	eq(doctrine::options(w, k)[0].cost, 0.0, "already given");
	const size_t nw = w.mythos.words.size();
	ok(doctrine::answer(w, k, 0));
	eq(w.mythos.words.size(), nw, "no second word");
}
