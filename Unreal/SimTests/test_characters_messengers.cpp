// DEMIURGE — SimTests/test_characters_messengers.cpp : messengers and candidates (SPEC-MYTHOS 3.8), track M4.
// Key: run.ps1 characters (every other module a stub; Society's raiseProphet is hooked there) and run.ps1 all
// (the real Society raises the voice). Synthetic worlds: one culture with a capital (city), a town and a village.
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

#include <set>
#include <string>
#include <vector>

using namespace dm;
using namespace dmt;

namespace {

struct Setup {
	std::unique_ptr<World> w;
	int cap = -1, town = -1, village = -1;
};

int freeLand(const World& w, const std::vector<int>& taken) {
	return findLand(w, [&](int c) {
		for (int t : taken) if (t == c) return false;
		return true;
	});
}

Setup setup(uint32_t seed = 1) {
	Setup s;
	s.w = makeWorld({ .L = 3, .seed = seed });
	World& w = *s.w;
	w.tune<MythosTune>().phase = 1;
	w.year = 1000;
	w.god.awe = 900;
	Culture& c = state::newCulture(w);
	c.name = "Umar"; c.age = Age::IRON; c.born = 0;
	std::vector<int> cells;
	for (int k = 0; k < 3; k++) cells.push_back(freeLand(w, cells));
	Settlement& a = state::newSettlement(w, cells[0], 0); a.capital = true; a.tier = 3; a.pop = 5000; a.devotion = 0.6;
	Settlement& b = state::newSettlement(w, cells[1], 0); b.tier = 2; b.pop = 1500; b.devotion = 0.6;
	Settlement& v = state::newSettlement(w, cells[2], 0); v.tier = 1; v.pop = 300; v.devotion = 0.6;
	s.cap = 0; s.town = 1; s.village = 2;
	w.cultures[0].capital = 0;
	for (int i = 0; i < 3; i++) w.owner[size_t(cells[size_t(i)])] = int16_t(i);
	return s;
}

// In `run.ps1 characters` Society is a stub: give it an honest raiseProphet.
void hookProphet() {
	if (!stub::stubbed("society")) return;
	stub::hooks().society_raiseProphet = [](World& w, int sid, const society::RaiseOpts& o) -> Prophet* {
		if (sid < 0 || sid >= int(w.settlements.size()) || w.settlements[size_t(sid)].dead) return nullptr;
		Culture& c = w.cultures[size_t(w.settlements[size_t(sid)].culture)];
		if (c.prophet) return nullptr;
		c.prophet = Prophet{ "Voice", sid, w.year + 60, o.figure };
		return &*c.prophet;
	};
}

// Move to the first decade (from the current year) whose candidates in sid include archetype a; the index.
int findArch(World& w, int sid, MessengerArchetype a) {
	for (int k = 0; k < 400; k++) {
		const std::vector<Figure> cs = characters::candidates(w, sid);
		for (int i = 0; i < int(cs.size()); i++) if (cs[size_t(i)].archetype == uint8_t(a)) return i;
		w.year = std::floor(w.year / 10) * 10 + 10;
	}
	return -1;
}

int chooseArch(World& w, int sid, MessengerArchetype a) {
	const int i = findArch(w, sid, a);
	return i < 0 ? -1 : characters::choose(w, sid, i);
}

Figure* fig(World& w, int id) {
	for (Figure& f : w.mythos.figures) if (f.id == id) return &f;
	return nullptr;
}

void act(World& w, double year, const char* kind, int sid, double good) {
	MythAct a;
	a.year = year; a.kind = kind; a.settlement = sid; a.culture = 0; a.good = good; a.natural = false;
	a.cell = w.settlements[size_t(sid)].cell;
	w.mythos.acts.push_back(a);
}

int countEvents(const std::vector<Event>& ev, Ev type, const std::string& what = "") {
	int n = 0;
	for (const Event& e : ev) {
		if (e.type != type) continue;
		if (what.empty()) { n++; continue; }
		if (const EvFigure* f = e.as<EvFigure>()) if (f->what == what) n++;
	}
	return n;
}

} // namespace

DM_TEST("characters: phase 0 is neutral (no candidates, no choice, no figures)") {
	stub::Guard g;
	hookProphet();
	Setup s = setup();
	s.w->tune<MythosTune>().phase = 0;
	ok(characters::candidates(*s.w, s.cap).empty(), "no candidates at phase 0");
	eq(characters::choose(*s.w, s.cap, 0), -1, "no choice at phase 0");
	characters::yearly(*s.w);
	ok(s.w->mythos.figures.empty(), "yearly creates nothing at phase 0");
	ok(s.w->mythos.num.empty(), "no counters at phase 0");
	eq(characters::credibility(*s.w, -1), 0.5, "credibility of nobody is neutral");
	Figure p; p.culture = 0;
	eq(characters::spawn(*s.w, p), -1, "spawn refused at phase 0");
}

DM_TEST("characters: candidates are three distinct archetypes, deterministic per town and decade, never nameless") {
	Setup s = setup(3);
	World& w = *s.w;
	const std::vector<Figure> a = characters::candidates(w, s.village);
	eq(int(a.size()), 3, "three candidates");
	std::set<int> arch;
	for (const Figure& f : a) {
		arch.insert(f.archetype);
		eq(f.id, 0, "candidate id 0");
		eq(int(f.kind), int(FigureKind::Candidate), "kind Candidate");
		ok(!f.name.empty() && !f.byname.empty(), "names never empty");
		eq(f.home, s.village, "home");
		eq(f.culture, 0, "culture");
		ok(f.archetype != uint8_t(MessengerArchetype::Queen), "no Queen in a village of a ruler-less culture");
		ok(f.archetype != uint8_t(MessengerArchetype::Criminal), "no Criminal below a town");
		ok(f.credibility >= 0 && f.credibility <= 1 && f.reach >= 0 && f.reach <= 1, "credibility and reach in 0..1");
	}
	eq(int(arch.size()), 3, "distinct archetypes");
	// the same three for the whole decade, whatever happened to w.rng in between
	w.rng.next(); w.rng.next();
	w.year = 1007.5;
	const std::vector<Figure> b = characters::candidates(w, s.village);
	bool same = a.size() == b.size();
	for (size_t i = 0; same && i < a.size(); i++) same = a[i].name == b[i].name && a[i].archetype == b[i].archetype && a[i].heart == b[i].heart;
	ok(same, "re-arming in the same decade shows the same three");
	// across decades they change (somewhere in 20 decades)
	bool changed = false;
	for (int d = 1; d <= 20 && !changed; d++) {
		w.year = 1000 + 10 * d;
		const std::vector<Figure> c = characters::candidates(w, s.village);
		for (size_t i = 0; i < c.size(); i++) if (c[i].archetype != a[i].archetype || c[i].heart != a[i].heart) changed = true;
	}
	ok(changed, "a later decade offers other people");
	ok(characters::candidates(w, -1).empty() && characters::candidates(w, 99).empty(), "empty for a bad settlement");
	w.settlements[size_t(s.town)].dead = true;
	ok(characters::candidates(w, s.town).empty(), "empty for a dead settlement");
}

DM_TEST("characters: the Queen is offered only in the capital or a ruled town; the Criminal only in a town or larger") {
	Setup s = setup(5);
	World& w = *s.w;
	int queenCap = 0, queenTown = 0, queenTownRuled = 0, crimVillage = 0, crimTown = 0;
	for (int d = 0; d < 300; d++) {
		w.year = 1000 + 10 * d;
		for (const Figure& f : characters::candidates(w, s.cap)) queenCap += f.archetype == uint8_t(MessengerArchetype::Queen);
		for (const Figure& f : characters::candidates(w, s.town)) {
			queenTown += f.archetype == uint8_t(MessengerArchetype::Queen);
			crimTown += f.archetype == uint8_t(MessengerArchetype::Criminal);
		}
		for (const Figure& f : characters::candidates(w, s.village)) crimVillage += f.archetype == uint8_t(MessengerArchetype::Criminal);
	}
	ok(queenCap > 100, "the capital offers a Queen often: " + std::to_string(queenCap));
	eq(queenTown, 0, "no Queen in a town while the culture has no ruler");
	eq(crimVillage, 0, "no Criminal in a village");
	ok(crimTown > 100, "a town offers the Criminal: " + std::to_string(crimTown));
	characters::yearly(w);   // Iron age with a capital: a ruler is crowned
	ok(characters::ruler(w, 0) >= 0, "the culture has a ruler");
	for (int d = 0; d < 300; d++) {
		w.year = 4000 + 10 * d;
		for (const Figure& f : characters::candidates(w, s.town)) queenTownRuled += f.archetype == uint8_t(MessengerArchetype::Queen);
	}
	ok(queenTownRuled > 100, "a ruled town offers the Queen: " + std::to_string(queenTownRuled));
}

DM_TEST("characters: choose creates the messenger, names the voice, and refuses a second one") {
	stub::Guard g(true);
	hookProphet();
	Setup s = setup();
	World& w = *s.w;
	const std::vector<Figure> cs = characters::candidates(w, s.town);
	const int next = w.mythos.nextId;
	std::vector<Event> ev;
	int id = -1;
	ev = capture(w, [&] { id = characters::choose(w, s.town, 1); });
	if (!ok(id >= 0, "chosen")) return;
	eq(id, next, "id from mythos.nextId");
	const Figure* f = characters::figure(w, id);
	if (!ok(f != nullptr, "figure exists")) return;
	eq(int(f->kind), int(FigureKind::Messenger), "kind Messenger");
	eq(f->name, cs[1].name, "the chosen candidate");
	eq(int(f->archetype), int(cs[1].archetype), "its archetype");
	near(f->credibility, w.tune<CharactersTune>().archCred[f->archetype], 1e-12, "credibility starts at the archetype's");
	ok(w.cultures[0].prophet && w.cultures[0].prophet->figure == id, "the culture's voice names the figure");
	eq(characters::messenger(w, 0), id, "messenger()");
	eq(countEvents(ev, Ev::Figure, "chosen"), 1, "figure chosen emitted");
	eq(characters::choose(w, s.cap, 0), -1, "one messenger per culture");
	eq(characters::choose(w, s.town, 5), -1, "bad index");
	eq(characters::choose(w, -3, 0), -1, "bad settlement");
	noNaN(w, "after choose");
}

DM_TEST("characters: a refused raise leaves no figure and no used id") {
	stub::Guard g;
	Setup s = setup();
	World& w = *s.w;
	w.cultures[0].prophet = Prophet{ "Old", s.cap, 2000, -1 };   // an unnamed voice already speaks
	const int next = w.mythos.nextId;
	eq(characters::choose(w, s.cap, 0), -1, "refused while the culture has a voice");
	ok(w.mythos.figures.empty(), "no figure left behind");
	eq(w.mythos.nextId, next, "nextId unchanged");
	if (stub::stubbed("society")) {
		w.cultures[0].prophet.reset();                            // the stub raiseProphet refuses everything
		eq(characters::choose(w, s.cap, 0), -1, "refused by Society");
		ok(w.mythos.figures.empty() && w.mythos.nextId == next, "undone");
	}
}

DM_TEST("characters: credibility moves with miracles, unforetold wrath and the drift (x 1.5 under Messengers)") {
	stub::Guard g;
	hookProphet();
	Setup s = setup();
	World& w = *s.w;
	const CharactersTune& t = w.tune<CharactersTune>();
	const int id = chooseArch(w, s.village, MessengerArchetype::Shepherd);
	if (!ok(id >= 0, "a shepherd")) return;
	const double y = w.year;
	act(w, y + 0.5, "rain", s.village, 0.6);
	act(w, y + 0.5, "rain", s.village, 0.6);     // the same kind twice in a year counts once
	act(w, y + 0.5, "heal", s.cap, 0.6);         // elsewhere: nothing
	w.year = y + 1;
	characters::yearly(w);
	double c = 0.5 + t.credMiracle;
	c += t.credDrift * (0.5 - c);
	near(characters::credibility(w, id), c, 1e-12, "miracle in their town +0.05, then the drift");
	act(w, y + 1.5, "lightning", s.village, -0.8);
	w.year = y + 2;
	characters::yearly(w);
	double c2 = c + t.credStruck;
	c2 += t.credDrift * (0.5 - c2);
	near(characters::credibility(w, id), c2, 1e-12, "wrath they did not foretell -0.1");
	Utterance u; u.id = w.mythos.nextId++; u.culture = 0; u.figure = id; u.content = uint8_t(WordContent::Warning); u.year = y + 2;
	w.mythos.words.push_back(u);
	act(w, y + 2.5, "quake", s.village, -0.8);
	w.year = y + 3;
	characters::yearly(w);
	double c3 = c2 + t.credDrift * (0.5 - c2);
	near(characters::credibility(w, id), c3, 1e-12, "foretold wrath costs nothing");
	w.mythos.presence = Presence::Messengers;
	act(w, y + 3.5, "bloom", s.village, 0.5);
	w.year = y + 4;
	characters::yearly(w);
	double c4 = c3 + t.credMiracle * t.msgCredMul;
	c4 += t.credDrift * (0.5 - c4);
	near(characters::credibility(w, id), c4, 1e-12, "x 1.5 under Messengers");
	// natural events are not the god's
	MythAct n; n.year = y + 4.5; n.kind = "quake"; n.settlement = s.village; n.good = -1; n.natural = true;
	w.mythos.acts.push_back(n);
	w.year = y + 5;
	characters::yearly(w);
	near(characters::credibility(w, id), c4 + t.credDrift * (0.5 - c4), 1e-12, "natural events ignored");
	// adjustCredibility clamps
	ok(characters::adjustCredibility(w, id, 5), "adjust");
	eq(characters::credibility(w, id), 1.0, "clamped at 1");
	ok(characters::adjustCredibility(w, id, -9), "adjust");
	eq(characters::credibility(w, id), 0.0, "clamped at 0");
	ok(!characters::adjustCredibility(w, 9999, 0.1), "bad id");
	ok(!characters::adjustCredibility(w, id, std::nan("")), "NaN refused");
}

DM_TEST("characters: the Criminal is redeemed by a miracle within 20 years, never after") {
	stub::Guard g(true);
	hookProphet();
	Setup s = setup(2);
	World& w = *s.w;
	const CharactersTune& t = w.tune<CharactersTune>();
	const int id = chooseArch(w, s.town, MessengerArchetype::Criminal);
	if (!ok(id >= 0, "a criminal")) return;
	near(characters::credibility(w, id), 0.2, 1e-12, "starts at 0.2");
	const double y = w.year;
	act(w, y + 5.5, "heal", s.town, 0.7);
	w.year = y + 6;
	fig(w, id)->data.erase("relapseAt");
	std::vector<Event> ev = capture(w, [&] { characters::yearly(w); });
	const Figure* f = characters::figure(w, id);
	ok(f->flags & FigureFlag::Redeemed, "redeemed");
	double c = 0.2 + t.credMiracle + t.criminalRedeem;
	c += t.credDrift * (0.2 - c);
	near(f->credibility, c, 1e-12, "+0.4 at once");
	eq(countEvents(ev, Ev::Figure, "redeemed"), 1, "figure redeemed");
	if (stub::stubbed("faiths")) ok(stub::called("faiths_adjustHouse") >= 1, "the House's corruption falls while the redeemed one lives");

	// a second criminal (another culture) whose miracle comes too late
	Culture& c2 = state::newCulture(w); c2.age = Age::IRON;
	const int cell = freeLand(w, { w.settlements[0].cell, w.settlements[1].cell, w.settlements[2].cell });
	Settlement& t2 = state::newSettlement(w, cell, 1); t2.tier = 2; t2.pop = 900; t2.capital = true;
	w.cultures[1].capital = t2.id;
	w.owner[size_t(cell)] = int16_t(t2.id);
	const int id2 = chooseArch(w, t2.id, MessengerArchetype::Criminal);
	if (!ok(id2 >= 0, "a second criminal")) return;
	fig(w, id2)->data.erase("relapseAt");
	const double y2 = w.year;
	act(w, y2 + 24.5, "heal", t2.id, 0.7);
	w.mythos.num["characters_acts"] = y2 + 24;
	w.year = y2 + 25;
	characters::yearly(w);
	ok(!(characters::figure(w, id2)->flags & FigureFlag::Redeemed), "no redemption after the window");
}

DM_TEST("characters: the Criminal's relapse discredits and offers a Judgement of the deed") {
	stub::Guard g(true);
	hookProphet();
	std::vector<cards::Proposal> props;
	if (stub::stubbed("cards")) stub::hooks().cards_propose = [&](World&, const cards::Proposal& p) { props.push_back(p); return true; };
	Setup s = setup(4);
	World& w = *s.w;
	const int id = chooseArch(w, s.town, MessengerArchetype::Criminal);
	if (!ok(id >= 0, "a criminal")) return;
	const double y = w.year;
	fig(w, id)->data["relapseAt"] = y + 3;
	w.year = y + 4;
	std::vector<Event> ev = capture(w, [&] { characters::yearly(w); });
	const Figure* f = characters::figure(w, id);
	ok((f->flags & FigureFlag::Relapsed) && (f->flags & FigureFlag::Discredited), "relapsed and discredited");
	eq(countEvents(ev, Ev::Figure, "discredited"), 1, "figure discredited");
	if (stub::stubbed("cards")) {
		bool judged = false;
		for (const cards::Proposal& p : props)
			if (p.kind == CardKind::Judgement && p.figure == id && p.ref[1] == 1 && p.severity >= 0.5 && p.settlement == s.town) judged = true;
		ok(judged, "a Judgement proposal for the relapse");
		props.clear();
		w.year = y + 5;
		characters::yearly(w);
		ok(!props.empty() && props[0].kind == CardKind::Judgement, "re-proposed while the deed stands");
		props.clear();
		w.year = y + 40;
		characters::yearly(w);
		w.year = y + 41;
		characters::yearly(w);
		bool still = false;
		for (const cards::Proposal& p : props) if (p.kind == CardKind::Judgement) still = true;
		ok(!still, "the deed no longer stands after 30 years");
	}
}

DM_TEST("characters: the Criminal relapses in ~20 % and the Queen's pride shows in ~25 % (forked at the choice)") {
	stub::Guard g;
	hookProphet();
	Setup s = setup(6);
	World& w = *s.w;
	int crim = 0, relapse = 0, queens = 0, proud = 0;
	for (int k = 0; k < 900; k++) {
		w.year = 1000 + 20.0 * k;
		const bool queen = (k % 2) == 1;
		const int id = chooseArch(w, s.cap, queen ? MessengerArchetype::Queen : MessengerArchetype::Criminal);
		if (id < 0) continue;
		const Figure* f = characters::figure(w, id);
		if (queen) { queens++; proud += f->data.count("prideAt") ? 1 : 0; }
		else {
			crim++; relapse += f->data.count("relapseAt") ? 1 : 0;
			ok(!f->data.count("relapseAt") || f->data.at("relapseAt") - w.year <= 30, "a relapse falls in the first 30 years");
		}
		w.mythos.figures.clear();
		w.cultures[0].prophet.reset();
	}
	ok(crim > 400 && queens > 400, "enough trials");
	const double rs = double(relapse) / std::max(1, crim), ps = double(proud) / std::max(1, queens);
	ok(rs > 0.15 && rs < 0.25, "relapse share " + std::to_string(rs));
	ok(ps > 0.20 && ps < 0.30, "pride share " + std::to_string(ps) + " (spec target 15-35 %)");
}

DM_TEST("characters: the Queen raises the House's authority, and the Queen's pride offers a Judgement") {
	stub::Guard g(true);
	hookProphet();
	std::vector<cards::Proposal> props;
	double authority = 0;
	if (stub::stubbed("cards")) stub::hooks().cards_propose = [&](World&, const cards::Proposal& p) { props.push_back(p); return true; };
	if (stub::stubbed("faiths"))
		stub::hooks().faiths_adjustHouse = [&](World&, int, double, double, double a, double) { authority += a; return true; };
	Setup s = setup(7);
	World& w = *s.w;
	const int id = chooseArch(w, s.cap, MessengerArchetype::Queen);
	if (!ok(id >= 0, "a queen")) return;
	if (stub::stubbed("faiths")) near(authority, w.tune<CharactersTune>().queenAuthority, 1e-12, "House authority +0.1");
	near(characters::credibility(w, id), 0.7, 1e-12, "starts at 0.7");
	const double y = w.year;
	fig(w, id)->data["prideAt"] = y + 2;
	w.year = y + 3;
	characters::yearly(w);
	ok(characters::figure(w, id)->flags & FigureFlag::Proud, "the pride showed");
	if (stub::stubbed("cards")) {
		bool judged = false;
		for (const cards::Proposal& p : props) if (p.kind == CardKind::Judgement && p.figure == id && p.ref[1] == 2) judged = true;
		ok(judged, "a Judgement proposal for the pride");
	}
}

DM_TEST("characters: the Child gains 0.01 a year, and is martyred when the town is taken or struck") {
	stub::Guard g(true);
	hookProphet();
	double love = 0;
	if (stub::stubbed("society")) stub::hooks().society_nudge = [&](World&, int, double, double dl) { love += dl; };
	Setup s = setup(8);
	World& w = *s.w;
	const CharactersTune& t = w.tune<CharactersTune>();
	const int id = chooseArch(w, s.village, MessengerArchetype::Child);
	if (!ok(id >= 0, "a child")) return;
	near(characters::credibility(w, id), 0.35, 1e-12, "starts at 0.35");
	w.year += 1;
	characters::yearly(w);
	double c = 0.35 + t.childCredYear;
	c += t.credDrift * (0.35 - c);
	near(characters::credibility(w, id), c, 1e-12, "+0.01 a year");
	// struck by the god
	act(w, w.year + 0.5, "lightning", s.village, -0.9);
	w.year += 1;
	std::vector<Event> ev = capture(w, [&] { characters::yearly(w); });
	const Figure* f = characters::figure(w, id);
	eq(int(f->fate), int(FigureFate::Martyred), "martyred");
	eq(f->died, w.year, "died this year");
	near(f->data.at("by"), -1, 0, "by the god");
	eq(countEvents(ev, Ev::Figure, "fallen"), 1, "figure fallen");
	if (stub::stubbed("society")) near(love, -0.1, 1e-12, "love -0.1 in the town that was struck");
	if (stub::stubbed("legends")) ok(stub::called("legends_add") >= 1, "a martyr legend asked for");
	eq(characters::messenger(w, 0), -1, "no living messenger");

	// taken by another people
	w.cultures[0].prophet.reset();
	Culture& other = state::newCulture(w); other.age = Age::IRON;
	(void)other;
	const int id2 = chooseArch(w, s.town, MessengerArchetype::Child);
	if (!ok(id2 >= 0, "a second child")) return;
	w.settlements[size_t(s.town)].culture = 1;
	w.year += 1;
	characters::yearly(w);
	const Figure* f2 = characters::figure(w, id2);
	eq(int(f2->fate), int(FigureFate::Martyred), "martyred when the town is taken");
	near(f2->data.at("by"), 1, 0, "by the captors");
}

DM_TEST("characters: the Unbelieved starts at 0.1 and is vindicated at 0.8, even after death") {
	stub::Guard g(true);
	hookProphet();
	Setup s = setup(9);
	World& w = *s.w;
	const int id = chooseArch(w, s.village, MessengerArchetype::Unbelieved);
	if (!ok(id >= 0, "an unbelieved")) return;
	near(characters::credibility(w, id), 0.1, 1e-12, "starts at 0.1");
	const CharactersTune& t = w.tune<CharactersTune>();
	characters::adjustCredibility(w, id, t.credFulfilled);
	characters::adjustCredibility(w, id, t.credFulfilled);
	ok(!(characters::figure(w, id)->flags & FigureFlag::Vindicated), "0.7: not yet");
	ok(characters::fell(w, id, FigureFate::Died), "dies unbelieved");
	std::vector<Event> ev = capture(w, [&] { characters::adjustCredibility(w, id, t.credFulfilled); });
	const Figure* f = characters::figure(w, id);
	ok(f->flags & FigureFlag::Vindicated, "vindicated after death");
	near(f->data.at("quoted"), w.year + 1000, 1e-9, "quoted for 1,000 years");
	eq(countEvents(ev, Ev::Figure, "vindicated"), 1, "figure vindicated once");
	ev = capture(w, [&] { characters::adjustCredibility(w, id, 0.1); });
	eq(countEvents(ev, Ev::Figure, "vindicated"), 0, "only once");
	if (stub::stubbed("legends")) ok(stub::called("legends_add") >= 1, "a legend asked for");
}

DM_TEST("characters: the messenger dies with the voice Society keeps") {
	stub::Guard g;
	hookProphet();
	Setup s = setup();
	World& w = *s.w;
	const int id = characters::choose(w, s.cap, 0);
	if (!ok(id >= 0, "chosen")) return;
	w.year += 1;
	characters::yearly(w);
	eq(characters::messenger(w, 0), id, "alive while the voice lives");
	w.cultures[0].prophet.reset();
	w.year += 1;
	std::vector<Event> ev = capture(w, [&] { characters::yearly(w); });
	eq(int(characters::figure(w, id)->fate), int(FigureFate::Died), "died");
	eq(countEvents(ev, Ev::Figure, "died"), 1, "figure died");
	eq(characters::messenger(w, 0), -1, "none");
}

DM_TEST("characters: a devout culture without a voice for 250 years seeks one (MessengerChoice at askCost)") {
	stub::Guard g;
	if (!stub::stubbed("cards")) return;   // needs the proposal hook
	std::vector<cards::Proposal> props;
	stub::hooks().cards_propose = [&](World&, const cards::Proposal& p) { props.push_back(p); return true; };
	Setup s = setup();
	World& w = *s.w;
	w.year = 200;
	characters::yearly(w);
	ok(props.empty(), "not before 250 years");
	w.year = 260;
	characters::yearly(w);
	if (!ok(props.size() == 1, "proposed")) return;
	const cards::Proposal& p = props[0];
	eq(int(p.kind), int(CardKind::MessengerChoice), "kind");
	eq(p.culture, 0, "culture");
	eq(p.settlement, s.cap, "the capital");
	near(p.data.at("cost"), 60, 0, "askCost");
	ok(p.severity > 0 && p.severity <= 1, "severity 0..1");
	props.clear();
	for (Settlement& x : w.settlements) x.devotion = 0.3;
	w.year = 270;
	characters::yearly(w);
	ok(props.empty(), "not when devotion < 0.45");
	for (Settlement& x : w.settlements) x.devotion = 0.6;
	w.cultures[0].prophet = Prophet{ "Old", s.cap, 300, -1 };
	w.year = 280;
	characters::yearly(w);
	ok(props.empty(), "not while a voice speaks");
	w.cultures[0].prophet.reset();
	w.year = 290;
	characters::yearly(w);
	ok(props.empty(), "the 250 years count from the last voice");
	w.year = 531;
	characters::yearly(w);
	ok(props.size() == 1, "again 250 years after the last voice");
	props.clear();
	w.cultures[0].age = Age::STONE;
	w.year = 900;
	characters::yearly(w);
	ok(props.empty(), "not in the Stone age");
}

DM_TEST("characters: the MessengerChoice card: options, costs, answer by the opening decade, none, lapse") {
	stub::Guard g;
	hookProphet();
	Setup s = setup(11);
	World& w = *s.w;
	Card c; c.id = w.mythos.nextId++; c.kind = CardKind::MessengerChoice; c.state = uint8_t(CardState::Open);
	c.culture = 0; c.settlement = s.town; c.opened = w.year; c.deadline = w.year + 60;
	w.mythos.cards.push_back(c);
	const std::vector<Figure> cs = characters::candidates(w, s.town);
	std::vector<CardOption> o = characters::options(w, c.id);
	if (!ok(o.size() == 4, "four answers")) return;
	for (int i = 0; i < 3; i++) { eq(o[size_t(i)].cost, 120.0, "the Messenger power's cost"); ok(o[size_t(i)].ok, "available"); }
	ok(o[3].ok && o[3].cost == 0, "none is free");
	w.mythos.cards.back().data["cost"] = 60;
	o = characters::options(w, c.id);
	eq(o[0].cost, 60.0, "the card's own cost");
	w.god.awe = 10;
	o = characters::options(w, c.id);
	eq(int(o[0].why), int(OptionWhy::Awe), "too little Awe");
	ok(!o[0].ok, "not ok");
	w.god.awe = 900;
	ok(characters::answer(w, c.id, 3), "none");
	ok(w.mythos.figures.empty(), "none chooses nobody");
	characters::lapse(w, c.id);
	ok(w.mythos.figures.empty(), "the lapse chooses nobody");
	w.year += 15;                                                 // a new decade before the answer
	ok(characters::answer(w, c.id, 2), "answered");
	const int m = characters::messenger(w, 0);
	if (!ok(m >= 0, "messenger")) return;
	eq(characters::figure(w, m)->name, cs[2].name, "the candidate shown when the card opened");
	o = characters::options(w, c.id);
	eq(int(o[0].why), int(OptionWhy::Unavailable), "unavailable once the culture has a voice");
	ok(!characters::answer(w, c.id, 7), "bad choice");
	ok(!characters::answer(w, 9999, 0), "bad card");
	ok(characters::options(w, 9999).empty(), "no options for a bad card");
	Card j; j.id = w.mythos.nextId++; j.kind = CardKind::Judgement; j.state = uint8_t(CardState::Open); j.culture = 0;
	w.mythos.cards.push_back(j);
	ok(!characters::answer(w, j.id, 0) && characters::options(w, j.id).empty(), "not a Characters card");
}
