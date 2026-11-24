// DEMIURGE — SimTests/test_mythos_factors.cpp : the mythos core's Factors assembly (SPEC-MYTHOS 1.4, 3.5, 3.6, 3.13,
// 3.3 effects, 3.4 effects, 3.12 survivors), the first people's shaping and the Presence card (3.5, 3.2, 2.3) and the
// archetype seeds (3.1). Track M1 "mythos-core". Key: run.ps1 mythos; also in all.
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

#include <array>
#include <cmath>
#include <string>
#include <vector>

using namespace dm;
using namespace dmt;

namespace {

std::unique_ptr<World> mythWorld(int cultures = 2, int towns = 3, int phase = 1, Archetype arch = Archetype::Balanced) {
	auto w = makeWorld({ .L = 4, .seed = 11 });
	w->tune<MythosTune>().phase = phase;
	w->settings.archetype = arch;
	std::vector<int> land;
	for (int c = 0; c < w->n; c++) if (w->elev[size_t(c)] >= w->seaLevel) land.push_back(c);
	const size_t stride = land.size() / size_t(cultures * towns + 1);
	int k = 1;
	for (int c = 0; c < cultures; c++) {
		state::newCulture(*w);
		for (int t = 0; t < towns; t++, k++) {
			Settlement& s = state::newSettlement(*w, land[size_t(k) * stride], c);
			s.pop = 100; s.devotion = 0.5; s.tier = 2;
			if (t == 0) { s.capital = true; w->cultures[size_t(c)].capital = s.id; }
		}
	}
	mythos::attach(*w);
	mythos::rebuild(*w);
	return w;
}

void pass(World& w, int years = 1) {
	for (int i = 0; i < years; i++) { w.year += 1; mythos::yearly(w); }
}

Decree decreeFor(int c, DecreeArea a, int stance, double kept, bool custom = false) {
	Decree d; d.culture = c; d.area = a; d.stance = int8_t(stance); d.kept = kept; d.custom = custom;
	return d;
}

int addCard(World& w, CardKind kind, int culture) {
	Card c; c.id = w.mythos.nextId++; c.kind = kind; c.state = uint8_t(CardState::Open); c.culture = culture; c.opened = w.year;
	w.mythos.cards.push_back(c);
	return c.id;
}

// ShapePeople choice from five levels (curiosity, compassion, ambition, courage, aggression)
int shapeChoice(int cu, int co, int am, int ku, int ag) { return cu + 3 * co + 9 * am + 27 * ku + 81 * ag; }

} // namespace

// ---------------------------------------------------------------- neutrality

DM_TEST("mythos factors: neutral at phase 0, and at phase 1 for a god who never used a mythos act") {
	auto w0 = mythWorld(2, 3, 0);
	pass(*w0, 20);
	ok(w0->mythos.faith.empty() && w0->mythos.factors.empty(), "phase 0: no records at all");
	ok(mythos::factors(*w0, 0) == Factors{}, "phase 0: neutral");
	ok(state::extensionsDefault(*w0), "phase 0: every extension default");
	auto w = mythWorld(3);
	for (int y = 0; y < 300; y++) {
		// an ordinary god: rain, heal, lightning, answered prayers (no word, no decree, no card)
		const Settlement& a = w->settlements[size_t(y % 9)];
		w->bus.emit(EvMiracleWitnessed{ a.cell, y % 3 == 0 ? "lightning" : "heal", y % 3 == 0 ? -1.0 : 0.8, { a.id } });
		if (y % 7 == 0) w->bus.emit(EvPrayerAnswered{ a.id, PrayerKind::Heal });
		pass(*w);
	}
	for (int c = 0; c < 3; c++) {
		ok(mythos::factors(*w, c) == Factors{}, "neutral Factors for culture " + std::to_string(c));
		ok(w->mythos.faith[size_t(c)].aspect != Aspect::Unknown, "they still formed a view of the god");
	}
	ok(mythos::factors(*w, -1) == Factors{} && mythos::factors(*w, 99) == Factors{}, "bad ids: neutral");
}

// ---------------------------------------------------------------- decrees, customs, rite

DM_TEST("mythos factors: each decree's effects reach Factors exactly, scaled by kept; customs at half strength") {
	auto w = mythWorld(4);
	w->mythos.decrees.push_back(decreeFor(0, DecreeArea::Wealth, int(WealthStance::Share), 1));
	w->mythos.decrees.push_back(decreeFor(0, DecreeArea::Violence, int(ViolenceStance::Peace), 1));
	w->mythos.decrees.push_back(decreeFor(1, DecreeArea::Wealth, int(WealthStance::Share), 0.5));
	w->mythos.decrees.push_back(decreeFor(2, DecreeArea::Wealth, int(WealthStance::Share), 1, true));
	w->mythos.decrees.push_back(decreeFor(3, DecreeArea::Care, int(CareStance::Shelter), 1));
	w->mythos.decrees.push_back(decreeFor(3, DecreeArea::Hospitality, int(HospitalityStance::Closed), 1));
	w->mythos.decrees.push_back(decreeFor(3, DecreeArea::Land, int(LandStance::Equal), 1));
	pass(*w);
	// (in run.ps1 all the real Doctrine moves each decree's kept toward obedience x fidelity first: read it back)
	auto k = [&](int c, DecreeArea area) {
		for (const Decree& d : w->mythos.decrees) if (d.culture == c && d.area == area) return d.kept * (d.custom ? 0.5 : 1.0);
		return 0.0;
	};
	auto mul = [](double m, double kk) { return 1 + (m - 1) * kk; };
	const double kS = k(0, DecreeArea::Wealth), kP = k(0, DecreeArea::Violence);
	if (stub::stubbed("doctrine")) near(kS, 1, 1e-12, "kept as set (Doctrine stubbed)");
	const Factors a = mythos::factors(*w, 0);
	near(a.famineLoss, mul(0.8, kS), 1e-12, "Share: famine losses x 0.8");
	near(a.growthR, mul(0.98, kS), 1e-12, "Share: growth x 0.98");
	near(a.research, mul(0.95, kS), 1e-12, "Share: research x 0.95");
	near(a.warV, -0.15 * kP, 1e-12, "Peace: warV -0.15");
	near(a.aggression, -0.1 * kP, 1e-12, "Peace: aggression -0.1");
	near(a.battle, mul(0.9, kP), 1e-12, "Peace: battle x 0.9");
	near(mythos::factors(*w, 1).famineLoss, mul(0.8, k(1, DecreeArea::Wealth)), 1e-12, "kept 0.5: half the effect");
	if (stub::stubbed("doctrine")) near(mythos::factors(*w, 1).famineLoss, 0.9, 1e-12, "kept 0.5: x 0.9");
	near(mythos::factors(*w, 2).famineLoss, mul(0.8, k(2, DecreeArea::Wealth)), 1e-12, "a custom: half strength");
	if (stub::stubbed("doctrine")) near(mythos::factors(*w, 2).famineLoss, 0.9, 1e-12, "a custom kept 1: x 0.9");
	const Factors d = mythos::factors(*w, 3);
	const double kC = k(3, DecreeArea::Care), kH = k(3, DecreeArea::Hospitality), kL = k(3, DecreeArea::Land);
	near(d.famineLoss, mul(0.75, kC), 1e-12, "Shelter: famine x 0.75");
	near(d.plagueDeath, mul(0.85, kC), 1e-12, "Shelter: plague deaths x 0.85");
	near(d.growthR, mul(0.97, kC), 1e-12, "Shelter: growth x 0.97");
	near(d.relDrift, -0.008 * kH, 1e-12, "Closed: relations -0.008 a year");
	near(d.plagueHop, mul(0.7, kH), 1e-12, "Closed: plague hop x 0.7");
	near(d.K, mul(0.9, kL), 1e-12, "Equal standing: K x 0.9");
	ok(kL < 0.5 || (!d.farmAncient && !d.claimAncient), "Equal standing: old growth neither farmed nor claimed");
}

DM_TEST("mythos factors: the five rites (3.13)") {
	auto w = mythWorld(5);
	for (int r = 0; r < 5; r++) w->mythos.decrees.push_back(decreeFor(r, DecreeArea::Worship, r, 1));
	pass(*w);
	std::array<double, 5> k{};
	for (const Decree& d : w->mythos.decrees) if (d.area == DecreeArea::Worship && d.culture >= 0 && d.culture < 5) k[size_t(d.culture)] = d.kept;
	auto mul = [](double m, double kk) { return 1 + (m - 1) * kk; };
	near(mythos::factors(*w, 0).prayerChance, mul(1.3, k[0]), 1e-12, "Prayer: prayer chance x 1.3");
	near(mythos::factors(*w, 0).answeredGain, mul(1.25, k[0]), 1e-12, "Prayer: answered effects x 1.25");
	near(mythos::factors(*w, 1).aweS, mul(1.2, k[1]), 1e-12, "Temples: aweS x 1.2");
	near(mythos::factors(*w, 2).plagueDeath, mul(0.8, k[2]), 1e-12, "Kindness: plague deaths x 0.8");
	near(mythos::factors(*w, 2).loveTarget, 0.03 * k[2], 1e-12, "Kindness: love drift +0.03");
	near(mythos::factors(*w, 3).research, mul(1.03, k[3]), 1e-12, "Art: research x 1.03");
	near(mythos::factors(*w, 4).farmConvert, mul(0.8, k[4]), 1e-12, "World: farm conversion x 0.8");
	near(mythos::factors(*w, 4).co2Uptake, mul(1.1, k[4]), 1e-12, "World: CO2 uptake x 1.1");
	if (stub::stubbed("doctrine")) near(mythos::factors(*w, 4).co2Uptake, 1.1, 1e-12, "kept 1 (Doctrine stubbed): x 1.1 exactly");
	eq(int(mythos::rite(*w, 2)), int(Rite::Kindness), "rite() reads the Worship decree");
	eq(int(mythos::rite(*w, 9)), int(Rite::None), "no culture: no rite");
}

// ---------------------------------------------------------------- aspect, will, survivors in Factors

DM_TEST("mythos factors: aspect and dependence effects only once the god acted on them (or emergent)") {
	auto w = mythWorld(2);
	for (Faith& f : w->mythos.faith) { f.image = { 0.8, 0, 0, 0 }; f.data["m_lastDeed"] = 1e9; }
	pass(*w);
	ok(w->mythos.faith[0].aspect == Aspect::Parent, "both see a Parent");
	ok(mythos::factors(*w, 0) == Factors{}, "untouched: neutral");
	w->bus.emit(EvWord{ 1, 0, "answer", "parable", "messenger", "heard" });   // the god speaks to culture 0
	pass(*w);
	const Faith& f0 = w->mythos.faith[0];
	const Factors a = mythos::factors(*w, 0);
	near(a.prayerChance, 1.25 * (1 + 0.5 * f0.dependence), 1e-12, "Parent x 1.25 and dependence x (1 + 0.5 d)");
	near(a.loveTarget, 0.08, 1e-12, "Parent: love target +0.08");
	near(a.unansweredPenalty, 0.6 + 0.8 * f0.dependence, 1e-12, "silence hurts a dependent people more");
	near(a.research, 1, 1e-12, "agency and dependence on research are emergent-only");
	ok(mythos::factors(*w, 1) == Factors{}, "culture 1 was not spoken to: neutral");
	w->tune<MythosTune>().emergent = true;
	pass(*w);
	const Faith& g = w->mythos.faith[1];
	const Factors b = mythos::factors(*w, 1);
	near(b.research, (1 + 0.12 * (g.agency - 0.4)) * (1 - 0.1 * g.dependence), 1e-12, "emergent: research by agency and dependence");
	near(b.schism, 0.7 + 0.6 * g.agency, 1e-12, "emergent: schism pace by agency");
	near(b.loveTarget, 0.08 + 0.03 * 0.5, 1e-12, "emergent: the aspect applies, and the traits fold in");
}

DM_TEST("mythos factors: Mystery and Threat effects; survivors' love target while they live") {
	auto w = mythWorld(3);
	w->mythos.faith[0].image = { 0, 0, 0.8, 0 };
	w->mythos.faith[1].image = { 0, 0, 0, 0.8 };
	for (int c = 0; c < 3; c++) { w->mythos.faith[size_t(c)].data["m_touched"] = 1; w->mythos.faith[size_t(c)].data["m_lastDeed"] = 1e9; }
	w->mythos.faith[2].survivorLove = 0.05; w->mythos.faith[2].survivorUntil = 50;
	pass(*w);
	near(mythos::factors(*w, 0).devotionBase, -0.03, 1e-12, "Mystery: devotion baseline -0.03");
	near(mythos::factors(*w, 1).loveTarget, -0.05, 1e-12, "Threat: love drift target -0.05");
	near(mythos::factors(*w, 2).loveTarget, 0.05, 1e-12, "survivors of a forgiveness: love target +0.05");
	w->year = 49; pass(*w);
	near(mythos::factors(*w, 2).loveTarget, 0, 1e-12, "at year 50 they are gone");
}

// ---------------------------------------------------------------- the first people (3.5)

DM_TEST("mythos shape: the budget rule (at most two High, each High needs a Low) and the options") {
	auto w = mythWorld(1);
	const int id = addCard(*w, CardKind::ShapePeople, 0);
	const std::vector<CardOption> opts = mythos::options(*w, id);
	eq(int(opts.size()), 244, "243 shapes and 'leave it to chance'");
	int valid = 0;
	for (int x = 0; x < 243; x++) {
		int hi = 0, lo = 0, v = x;
		for (int i = 0; i < 5; i++) { if (v % 3 == 2) hi++; else if (v % 3 == 0) lo++; v /= 3; }
		const bool want = hi <= 2 && lo >= hi;
		if (want) valid++;
		if (size_t(x) < opts.size() && opts[size_t(x)].ok != want) { fail("option " + std::to_string(x)); break; }
	}
	ok(opts.size() == 244 && opts[243].ok, "chance is always allowed");
	ok(valid > 0 && valid < 243, "some shapes are refused");
	ok(!mythos::answer(*w, id, shapeChoice(2, 2, 2, 0, 0)), "three High refused");
	ok(!mythos::answer(*w, id, shapeChoice(2, 2, 0, 1, 1)), "two High and one Low refused");
	ok(!mythos::answer(*w, id, 300), "out of range refused");
	ok(w->mythos.num.count("mythos_shaped") == 0, "nothing recorded by a refused shape");
	for (const CardOption& o : opts) ok(o.cost == 0, "shaping is free");
}

DM_TEST("mythos shape: the answer sets the five traits through society::shapeTraits; others inherit with drift") {
	stub::Guard g(true);
	if (!stub::stubbed("society")) return;
	std::vector<std::pair<int, Traits>> calls;
	stub::hooks().society_shapeTraits = [&](World& w, int c, const Traits& t) {
		calls.push_back({ c, t });
		w.cultures[size_t(c)].traits = t;
		return true;
	};
	auto w = mythWorld(2);
	state::newCulture(*w, 0);                // a daughter: Society drifts its traits, the shaping does not set them
	mythos::rebuild(*w);
	const int id = addCard(*w, CardKind::ShapePeople, 0);
	ok(mythos::answer(*w, id, shapeChoice(2, 0, 1, 2, 0)), "curious, brave, not gentle, not fierce");
	eq(int(calls.size()), 2, "both woken peoples, not the daughter");
	if (calls.size() == 2) {
		const Traits& t = calls[1].second;
		eq(calls[1].first, 0, "the card's people last (Society keeps the last shaping for later awakenings)");
		near(t.curiosity, 0.8, 1e-12, "High = 0.8"); near(t.compassion, 0.25, 1e-12, "Low = 0.25");
		near(t.ambition, 0.5, 1e-12, "Middling = 0.5"); near(t.courage, 0.8, 1e-12, "High"); near(t.aggression, 0.25, 1e-12, "Low");
		const Traits& u = calls[0].second;
		ok(std::fabs(u.curiosity - 0.8) <= 0.05 + 1e-12 && std::fabs(u.compassion - 0.25) <= 0.05 + 1e-12, "the second band inherits +- 0.05");
	}
	ok(w->mythos.faith[0].data.count("m_shaped") == 1 && w->mythos.faith[2].data.count("m_shaped") == 0, "shaped flags");
	// the shaped traits fold into Factors (3.5 table)
	pass(*w);
	const Factors f = mythos::factors(*w, 0);
	near(f.famineLoss, 1 - 0.25 * 0.25, 1e-12, "compassion: famine x (1 - 0.25 c)");
	near(f.defence, 1 + 0.15 * 0.8, 1e-12, "courage: defence x (1 + 0.15 k)");
	near(f.warStrength, 1 - 0.2 * 0.8, 1e-12, "courage: the warStrength gate x (1 - 0.2 k)");
	near(f.battle, (1 - 0.1 * 0.25) * (1 + 0.1 * 0.25), 1e-12, "compassion and aggression on battle");
	near(f.expandCooldown, (1 + 0.2 * 0.25) * (1 - 0.2 * 0.5), 1e-12, "compassion and ambition on the settler cooldown");
	ok(mythos::factors(*w, 2) == Factors{}, "the unshaped daughter (born before the shaping) stays neutral");
	// a later awakening inherits the shaping in Society's newCulture; Mythos marks it shaped (its traits fold in)
	calls.clear();
	state::newCulture(*w);
	pass(*w);
	eq(int(calls.size()), 0, "Mythos does not shape it again");
	ok(w->mythos.faith[3].data.count("m_shaped") == 1, "the new woken people counts as shaped");
	ok(!(mythos::factors(*w, 3) == Factors{}), "so its traits fold into its Factors");
	// leave it to chance: a valid shape, the same for the same card
	auto w2 = mythWorld(1);
	const int id2 = addCard(*w2, CardKind::ShapePeople, 0);
	calls.clear();
	ok(mythos::answer(*w2, id2, 243), "leave it to chance");
	ok(calls.size() == 1, "rolled and applied");
	if (!calls.empty()) {
		const Traits& t = calls[0].second;
		int hi = 0, lo = 0;
		for (double v : { t.curiosity, t.compassion, t.ambition, t.courage, t.aggression }) { if (v > 0.7) hi++; else if (v < 0.3) lo++; }
		ok(hi <= 2 && lo >= hi, "the roll keeps the budget rule");
	}
}

DM_TEST("mythos waking: the first people open the ShapePeople and Presence cards once") {
	stub::Guard g(true);
	if (!stub::stubbed("cards")) return;
	std::vector<cards::Proposal> opened;
	stub::hooks().cards_open = [&](World&, const cards::Proposal& p) { opened.push_back(p); return int(opened.size()); };
	auto w = mythWorld(1);
	pass(*w);
	eq(int(opened.size()), 2, "two cards");
	if (opened.size() == 2) {
		eq(int(opened[0].kind), int(CardKind::ShapePeople), "ShapePeople");
		eq(opened[0].culture, 0, "for the first people");
		eq(int(opened[1].kind), int(CardKind::Presence), "Presence");
	}
	pass(*w, 3);
	eq(int(opened.size()), 2, "never again");
	auto z = mythWorld(1, 3, 0);
	pass(*z);
	eq(int(opened.size()), 2, "phase 0 opens nothing");
}

DM_TEST("mythos presence card: the first stance is free; Walk is not an answer") {
	auto w = mythWorld(2);
	const int id = addCard(*w, CardKind::Presence, -1);
	const std::vector<CardOption> o = mythos::options(*w, id);
	eq(int(o.size()), 3, "Wonders, Messengers, Unseen");
	for (const CardOption& x : o) ok(x.ok && x.cost == 0, "free");
	ok(!mythos::answer(*w, id, 3), "Walk (3) is not an answer");
	const auto ev = capture(*w, [&] { ok(mythos::answer(*w, id, int(Presence::Messengers)), "Messengers"); });
	eq(int(w->mythos.presence), int(Presence::Messengers), "set");
	ok(!ev.empty() && ev.back().type == Ev::Presence, "presence event");
	ok(w->mythos.faith[1].data.count("m_touched") == 1, "a stance other than Wonders touches every people");
	mythos::lapse(*w, id);
	eq(int(w->mythos.presence), int(Presence::Messengers), "lapse changes nothing");
	const int other = addCard(*w, CardKind::Blessing, 0);
	ok(!mythos::answer(*w, other, 0), "v2 kinds are refused in M1");
	ok(!mythos::answer(*w, 999, 0), "bad card id");
	ok(mythos::options(*w, 999).empty(), "no options for a bad id");
}

// ---------------------------------------------------------------- archetypes (3.1)

DM_TEST("mythos archetype: parameters are the MythosTune rows; Balanced is neutral; the mythic seeds") {
	auto w = mythWorld(1);
	const ArchetypeParams b = mythos::archetypeParams(*w);
	ok(b.fert == 0 && b.natural == 1 && b.capacityK == 1 && b.marvels == 0 && b.agency == 0 && b.devotion == 0 && !b.stormRing,
		"Balanced: every parameter neutral");
	const MythosTune& T = w->tune<MythosTune>();
	for (int a = 0; a < 5; a++) {
		w->settings.archetype = Archetype(a);
		const ArchetypeParams p = mythos::archetypeParams(*w);
		const ArchetypeParams& q = T.archetypes[size_t(a)];
		ok(p.fert == q.fert && p.natural == q.natural && p.ridgeK == q.ridgeK && p.marvels == q.marvels && p.threatSeed == q.threatSeed,
			std::string("row of ") + toStr(Archetype(a)));
	}
	near(T.archetypes[size_t(Archetype::Generous)].fert, 0.15, 1e-12, "Generous fert +0.15");
	near(T.archetypes[size_t(Archetype::Dangerous)].natural, 1.6, 1e-12, "Dangerous natural x 1.6");
	eq(T.archetypes[size_t(Archetype::Strange)].marvels, 3, "Strange: 3-6 marvels");
	// seeds at the waking
	auto d = mythWorld(1, 3, 1, Archetype::Dangerous);
	near(d->mythos.faith[0].image[size_t(Aspect::Threat) - 1], 0.1, 1e-12, "Dangerous: Threat +0.1 at the waking");
	auto s = mythWorld(1, 3, 1, Archetype::Strange);
	near(s->mythos.faith[0].image[size_t(Aspect::Mystery) - 1], 0.1, 1e-12, "Strange: Mystery +0.1 at the waking");
	state::newCulture(*s, 0);
	mythos::rebuild(*s);
	near(s->mythos.faith[1].image[size_t(Aspect::Mystery) - 1], 0.1, 1e-12, "a daughter inherits the parent's view, not a second seed");
	// the will and Factors terms
	pass(*d);
	near(mythos::factors(*d, 0).devotionBase, 0.03, 1e-12, "Dangerous: devotion baseline +0.03");
	auto gen = mythWorld(1, 3, 1, Archetype::Generous);
	auto bal = mythWorld(1, 3, 1, Archetype::Balanced);
	for (World* x : { gen.get(), bal.get() }) {
		MythosTune& t = x->tune<MythosTune>();
		t.agencyDrift = 1; t.dependenceDrift = 1;
		pass(*x);
	}
	near(gen->mythos.faith[0].agency - bal->mythos.faith[0].agency, -0.03, 1e-9, "Generous: agency target -0.03");
	near(gen->mythos.faith[0].dependence - bal->mythos.faith[0].dependence, 0.05, 1e-9, "Generous: dependence target +0.05");
}

// ---------------------------------------------------------------- town factors (blessings read)

DM_TEST("mythos townFactors: neutral without a blessing and below phase 2; a blessing's numbers while it lasts") {
	auto w = mythWorld(1);
	ok(mythos::townFactors(*w, 0) == TownFactors{}, "no blessing");
	TownMyth t; t.settlement = 0; t.blessing = BlessingKind::Prosperity; t.blessedUntil = 100;
	w->mythos.towns.push_back(t);
	ok(mythos::townFactors(*w, 0) == TownFactors{}, "phase 1: blessings are v2");
	w->tune<MythosTune>().phase = 2;
	near(mythos::townFactors(*w, 0).K, 1.25, 1e-12, "Prosperity K x 1.25");
	near(mythos::townFactors(*w, 0).growthR, 1.2, 1e-12, "Prosperity growth x 1.2");
	w->year = 100;
	ok(mythos::townFactors(*w, 0) == TownFactors{}, "it ended");
	ok(mythos::townFactors(*w, -3) == TownFactors{}, "bad id");
	eq(mythos::warGoal(*w, 0), -1, "warGoal: none (v2)");
	near(mythos::neglectScale(*w, 0), 1, 0, "neglectScale: 1 (v3)");
}
