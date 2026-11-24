// DEMIURGE — SimTests/test_doctrine_judgement.cpp : judgement by deeds and hearts (SPEC-MYTHOS 3.11) and the
// sentences (3.12). Track M3.
#include "test_doctrine_fixture.h"

using namespace dm;
using namespace dmt;
using namespace dmt::doc;

DM_TEST("doctrine judgement: a severe breach proposes a Judgement card with a stable hidden heart") {
	stub::Guard g;
	ProposalLog log;
	if (!log.install()) return;
	DocWorld d = docWorld();
	World& w = *d.w;
	setDecree(w, d.vai, DecreeArea::Violence, int(ViolenceStance::Peace));
	setWar(w, d.vai, d.oru, d.oru);
	w.mythos.faith[0].obedience = 0;
	for (int i = 0; i < 3000 && log.count(CardKind::Judgement) == 0; i++) { w.year += 1; doctrine::yearly(w); }
	const cards::Proposal* p = log.last(CardKind::Judgement);
	if (!ok(p != nullptr, "a Judgement proposal")) return;
	const cards::Proposal first = *p;
	eq(first.culture, d.vai); eq(first.settlement, d.towns[0]); eq(first.cell, w.settlements[size_t(d.towns[0])].cell);
	ok(first.severity >= 0.5, "severe");
	eq(first.ref[1], int(DecreeArea::Violence), "ref[1] = the area");
	eq(first.ref[2], int(Circumstance::War), "they were attacked: war");
	ok(first.ref[3] >= int(HeartKind::Selfless) && first.ref[3] <= int(HeartKind::Zealous), "a real heart");
	eq(int(first.data.at("heartKind")), first.ref[3], "the heart is also in the data");
	eq(first.data.at("deedWrong"), 1.0, "a breach is a wrong deed");
	const bool wrongHeart = first.ref[3] == int(HeartKind::Greedy) || first.ref[3] == int(HeartKind::Zealous);
	eq(first.data.at("heartWrong"), wrongHeart ? 1.0 : 0.0);
	// re-proposed the next years with the same heart (the matter stands), until a card for it exists
	const size_t n0 = log.got.size();
	w.mythos.faith[0].obedience = 1;                         // no new breaches
	for (int i = 0; i < 3; i++) { w.year += 1; doctrine::yearly(w); }
	int again = 0;
	for (size_t i = n0; i < log.got.size(); i++)
		if (log.got[i].kind == CardKind::Judgement) { again++; ok(log.got[i].ref == first.ref, "same matter, same heart"); }
	eq(again, 3, "one proposal a year");
	pushCard(w, CardKind::Judgement, d.vai, d.towns[0], first.ref);
	const size_t n1 = log.got.size();
	w.year += 1; doctrine::yearly(w);
	eq(log.count(CardKind::Judgement), int(again + 1), "a card for it exists: no more proposals");
	(void)n1;
	const Decree* dc = doctrine::decree(w, d.vai, DecreeArea::Violence);
	ok(dc && dc->data.count("pendId") == 0, "the pending breach is cleared");
}

DM_TEST("doctrine judgement: pending breaches expire after breachYears") {
	stub::Guard g;
	ProposalLog log;
	if (!log.install()) return;
	DocWorld d = docWorld();
	World& w = *d.w;
	Decree& dc = setDecree(w, d.vai, DecreeArea::Violence, int(ViolenceStance::Peace));
	dc.data["pendId"] = 7; dc.data["pendSid"] = d.towns[1]; dc.data["pendSev"] = 0.7; dc.data["pendCirc"] = 2; dc.data["pendYear"] = w.year;
	w.mythos.faith[0].obedience = 1;
	for (int i = 0; i < 30; i++) { w.year += 1; doctrine::yearly(w); }
	eq(log.count(CardKind::Judgement), 20, "proposed for 20 years, then the matter is gone");
	const Decree* x = doctrine::decree(w, d.vai, DecreeArea::Violence);
	ok(x && x->data.count("pendId") == 0);
}

DM_TEST("doctrine judgement: hearts follow the traits") {
	// many breaches of Tithe in famine: a greedy-hearted people shows Greedy far more often than a kind one
	stub::Guard g;
	ProposalLog log;
	if (!log.install()) return;
	int greedy[2] = { 0, 0 }, selfless[2] = { 0, 0 };
	for (int k = 0; k < 2; k++) {
		log.got.clear();
		DocWorld d = docWorld();
		World& w = *d.w;
		Traits& t = w.cultures[0].traits;
		if (k == 0) { t.compassion = 0.95; t.ambition = 0.05; } else { t.compassion = 0.05; t.ambition = 0.95; }
		setDecree(w, d.vai, DecreeArea::Wealth, int(WealthStance::Tithe));
		w.settlements[size_t(d.towns[0])].famine = 4;
		w.settlements[size_t(d.towns[1])].pop = 50; w.settlements[size_t(d.towns[2])].pop = 50;   // capital share 0.8: severe
		w.mythos.faith[0].obedience = 0;
		int lastRef = -1;
		for (int i = 0; i < 40000; i++) {
			w.year += 1; doctrine::yearly(w);
			const cards::Proposal* p = log.last(CardKind::Judgement);
			if (p && p->ref[0] != lastRef) {
				lastRef = p->ref[0];
				if (p->ref[3] == int(HeartKind::Greedy)) greedy[k]++;
				if (p->ref[3] == int(HeartKind::Selfless)) selfless[k]++;
				pushCard(w, CardKind::Judgement, d.vai, d.towns[0], p->ref, -1, -1, CardState::Lapsed);
				if (w.mythos.cards.size() > 40) w.mythos.cards.erase(w.mythos.cards.begin());
			}
		}
	}
	ok(greedy[0] + selfless[0] >= 30 && greedy[1] + selfless[1] >= 30, "enough judgements: " + std::to_string(greedy[0] + selfless[0]));
	ok(selfless[0] > 2 * greedy[0], "the kind people: mostly selfless (" + std::to_string(selfless[0]) + " / " + std::to_string(greedy[0]) + ")");
	ok(greedy[1] > selfless[1], "the grasping people: mostly greedy (" + std::to_string(greedy[1]) + " / " + std::to_string(selfless[1]) + ")");
}

DM_TEST("doctrine judgement: options, costs and who may be removed") {
	stub::Guard g;
	DocWorld d = docWorld();
	World& w = *d.w;
	const DoctrineTune& T = w.tune<DoctrineTune>();
	const int k = pushCard(w, CardKind::Judgement, d.vai, d.towns[1], { 3, int(DecreeArea::Violence), 2, 1 });
	auto o = doctrine::options(w, k);
	if (!eq(int(o.size()), 8, "deed / heart x four sentences")) return;
	for (int i = 0; i < 8; i++) eq(o[size_t(i)].choice, i);
	ok(o[0].ok && o[0].cost == 0, "forgive is free");
	ok(o[1].ok && o[1].cost == T.warnCost, "warn costs 20");
	ok(!o[2].ok && o[2].why == OptionWhy::NeedsRuler, "a town's deed: no ruler to remove");
	ok(o[2].cost == T.removeCost, "remove costs 60");
	ok(o[4].ok && o[5].ok, "the same by the heart");
	if (stub::stubbed("powers")) {
		ok(!o[3].ok, "the wrath power is locked in the stub");
		stub::hooks().powers_can = [](const World&, std::string_view id, int) {
			powers::CanResult r; r.ok = id == "lightning"; r.reason = r.ok ? powers::Reason::Ok : powers::Reason::Locked; return r;
		};
		o = doctrine::options(w, k);
		ok(o[3].ok && o[3].cost == 0 && o[7].ok, "disaster: the power pays its own cost");
	}
	const int r = addRuler(w, d.vai, d.towns[0]);
	const int k2 = pushCard(w, CardKind::Judgement, d.vai, -1, { -1, -1, -1, -1 }, r);
	o = doctrine::options(w, k2);
	ok(o[2].ok && o[6].ok, "a ruler's deed: remove the ruler");
	w.god.awe = 10;
	o = doctrine::options(w, k);
	ok(!o[1].ok && o[1].why == OptionWhy::Awe, "warn: too little Awe");
	ok(o[0].ok, "forgive still free");
	eq(int(doctrine::options(w, 12345).size()), 0, "bad card");
}

DM_TEST("doctrine judgement: sentences act through the owners and emit judgement") {
	stub::Guard g(true);
	if (!stub::stubbed("society") || !stub::stubbed("powers") || !stub::stubbed("characters")) return;
	DocWorld d = docWorld();
	World& w = *d.w;
	const DoctrineTune& T = w.tune<DoctrineTune>();
	struct Nudge { int sid; double dDev, dLove; };
	std::vector<Nudge> nudges;
	stub::hooks().society_nudge = [&](World&, int sid, double a, double b) { nudges.push_back({ sid, a, b }); };
	std::vector<EvJudgement> got;
	(void)w.bus.on<EvJudgement>([&](const EvJudgement& e) { got.push_back(e); });
	// forgive, by the deed
	int k = pushCard(w, CardKind::Judgement, d.vai, d.towns[1], { 1, 0, 2, 1 });
	ok(doctrine::answer(w, k, 0), "forgive");
	if (!eq(int(nudges.size()), 1)) return;
	eq(nudges[0].sid, d.towns[1]); near(nudges[0].dLove, T.forgiveLove, 1e-12, "love + 0.08 there");
	if (!eq(int(got.size()), 1)) return;
	eq(got[0].card, k); eq(got[0].culture, d.vai); eq(got[0].by, std::string("deed")); eq(got[0].sentence, std::string("forgive"));
	// forgive by the heart, a people that judges by hearts: x 1.3
	w.mythos.faith[0].judgedBy = 0.5;
	k = pushCard(w, CardKind::Judgement, d.vai, d.towns[1], { 2, 0, 2, 1 });
	ok(doctrine::answer(w, k, 4));
	near(nudges.back().dLove, T.forgiveLove * 1.3, 1e-12, "hearts: forgiveness weighs more");
	eq(got.back().by, std::string("heart"));
	// warn: nobody harmed
	const size_t nn = nudges.size();
	k = pushCard(w, CardKind::Judgement, d.vai, d.towns[1], { 3, 0, 2, 1 });
	ok(doctrine::answer(w, k, 1)); eq(got.back().sentence, std::string("warn")); eq(nudges.size(), nn, "no nudge");
	// remove: only a ruler, through characters::fell
	ok(!doctrine::answer(w, k, 2), "no ruler: refused");
	const int r = addRuler(w, d.vai, d.towns[0]);
	int fellId = -1;
	stub::hooks().characters_fell = [&](World&, int id, FigureFate f) { fellId = id; return f == FigureFate::Fallen; };
	k = pushCard(w, CardKind::Judgement, d.vai, -1, { -1, -1, -1, -1 }, r);
	ok(doctrine::answer(w, k, 2), "remove the ruler");
	eq(fellId, r);
	near(nudges.back().dLove, T.removeFear, 1e-12, "fear in the capital");
	eq(nudges.back().sid, d.towns[0]);
	eq(got.back().sentence, std::string("remove"));
	// disaster: lightning on the town; refused when the power is
	std::string usedId; int usedCell = -1; bool allow = false;
	stub::hooks().powers_use = [&](World&, std::string_view id, int cell, const powers::UseOpts&) { usedId = std::string(id); usedCell = cell; return allow; };
	k = pushCard(w, CardKind::Judgement, d.vai, d.towns[2], { 4, 0, 2, 3 });
	const size_t ng = got.size();
	ok(!doctrine::answer(w, k, 3), "the power refused: the card stays open");
	eq(got.size(), ng, "no judgement without the strike");
	allow = true;
	ok(doctrine::answer(w, k, 7), "disaster, by the heart");
	eq(usedId, std::string("lightning")); eq(usedCell, w.settlements[size_t(d.towns[2])].cell);
	eq(got.back().sentence, std::string("disaster")); eq(got.back().by, std::string("heart"));
	ok(!doctrine::answer(w, k, 8) && !doctrine::answer(w, k, -1), "bad choices");
	// a lapse is the people's own judgement: nothing from the god
	const size_t ng2 = got.size();
	doctrine::lapse(w, k);
	eq(got.size(), ng2);
}
