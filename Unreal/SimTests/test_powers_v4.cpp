// DEMIURGE — SimTests/test_powers_v4.cpp : Sim/Powers.cpp, the v4 mythos half (SPEC-MYTHOS 2.1, 3.8, 3.9, 3.16, 3.18,
// 3.25, 3.29, 3.30; build plan 9 track M7 "powers-v4").
// Owner: M7 powers-v4.
//
// What is tested: the Word powers' phase and age gates (MythosTune::phase 0 = all locked, v3.1 bit for bit); each Word
// power's can() reasons and its price (cost() = the cheapest choice; use() = the chosen one); nothing spent, no
// power:used and no deed on a refused word; powers::pay (free, refused, rate-limited awe:short per act); the
// Messenger choice (prophet with UseOpts::choose at phase >= 1 opens the MessengerChoice card and spends nothing; at
// phase 0, or without choose, the v3.1 prophet); the withdrawal refusal (phase 3 only); and guardrail 2 for Powers (a
// god who ignores the Word at phase 1 changes nothing the other powers do).
//
// `run.ps1 powers`: the mythos modules are stubs, so hooks stand in for Doctrine, Mythos, Characters, Cards and Renewal
// (those tests return early in `run.ps1 all`). Tests that read only Powers' own writes run in both builds; the
// "real Doctrine" test runs only in `all`.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Cards.h"
#include "Characters.h"
#include "Civ.h"
#include "Doctrine.h"
#include "Mythos.h"
#include "Powers.h"
#include "Renewal.h"
#include "Sim.h"
#include "Society.h"
#include "TestFramework.h"
#include "TestWorld.h"
#include "stubs/StubHooks.h"

using namespace dm;
using namespace dmt;
namespace pw = dm::powers;

namespace {

const char* const kWord[] = { "speak", "bless", "reveal", "test", "renew" };

int town(World& w, int cell, int culture, double pop = 200, int hops = 2) {
	Settlement& s0 = state::newSettlement(w, cell, culture);
	s0.pop = pop;
	const int sid = s0.id;
	std::vector<int32_t> r;
	w.geo->ring(cell, hops, r);
	std::vector<int32_t> cells;
	for (int c : r)
		if (!state::isSea(w, c) && w.owner[size_t(c)] < 0) { w.owner[size_t(c)] = int16_t(sid); cells.push_back(c); }
	w.settlements[size_t(sid)].cells = cells;
	return sid;
}

// A history world at the Industry age with Awe to spare, one people and their town (and a second people's town).
struct Fixture {
	std::unique_ptr<World> w;
	int cid = -1, sid = -1, cell = -1;
	int cid2 = -1, sid2 = -1, cell2 = -1;
};
Fixture fixture(int mythPhase, uint32_t seed = 3) {
	Fixture f;
	f.w = makeWorld({ .L = 4, .seed = seed });
	World& w = *f.w;
	w.god.awe = 1000;
	w.god.aweMax = 1200;
	w.age = Age::INDUSTRY;
	w.year = 1500;
	w.tune<MythosTune>().phase = mythPhase;
	f.cell = findLand(w, [&](int c) { return w.coast[size_t(c)] >= 3; });
	f.cid = state::newCulture(w).id;
	w.cultures[size_t(f.cid)].age = Age::INDUSTRY;
	f.sid = town(w, f.cell, f.cid, 400);
	w.cultures[size_t(f.cid)].capital = f.sid;
	w.settlements[size_t(f.sid)].capital = true;
	f.cell2 = findLand(w, [&](int c) { return w.coast[size_t(c)] >= 3 && w.geo->angle(c, f.cell) > 0.5; });
	f.cid2 = state::newCulture(w).id;
	w.cultures[size_t(f.cid2)].age = Age::INDUSTRY;
	f.sid2 = town(w, f.cell2, f.cid2, 300);
	w.cultures[size_t(f.cid2)].capital = f.sid2;
	w.settlements[size_t(f.sid2)].capital = true;
	return f;
}

int countEv(const std::vector<Event>& ev, Ev t) {
	return int(std::count_if(ev.begin(), ev.end(), [&](const Event& e) { return e.type == t; }));
}
const char* reasonOf(const pw::CanResult& r) { return pw::toStr(r.reason); }
const char* whyOf(const pw::CanResult& r) { return pw::toStr(r.why); }

bool anyMythosCall() {
	for (const std::string& c : stub::calls())
		for (const char* p : { "doctrine_", "mythos_", "characters_", "cards_", "renewal_", "legends_", "faiths_", "omens_", "keepers_" })
			if (c.rfind(p, 0) == 0) return true;
	return false;
}

// The Doctrine of 3.9 as a hook: costs 60 / 50 / 30 / 90 by clarity, one word per culture per 10 years, Content for
// Warning / Promise, and speak() records the word in mythos.num (the real Doctrine records an Utterance).
struct FakeDoctrine {
	int spoken = 0;
	WordOpts last;
	int lastCulture = -1;
	bool refuseSpeak = false;           // speak() returns -1 although canSpeak said yes
};
FakeDoctrine& fd() { static FakeDoctrine f; return f; }
double fakeCost(const WordOpts& o) {
	static const double k[4] = { 60, 50, 30, 90 };
	return uint8_t(o.clarity) < 4 ? k[uint8_t(o.clarity)] : 0.0;
}
doctrine::SpeakCheck fakeCan(const World& w, int cid, const WordOpts& o) {
	doctrine::SpeakCheck r;
	if (!state::culture(w, cid)) { r.why = doctrine::WordWhy::Target; return r; }
	if (o.content == WordContent::Warning || o.content == WordContent::Promise) { r.why = doctrine::WordWhy::Content; return r; }
	const auto it = w.mythos.num.find("test_last:" + std::to_string(cid));
	if (it != w.mythos.num.end() && w.year - it->second < 10) { r.why = doctrine::WordWhy::Gap; return r; }
	r.cost = fakeCost(o);
	if (w.god.awe < r.cost) { r.why = doctrine::WordWhy::Awe; return r; }
	r.ok = true;
	r.why = doctrine::WordWhy::Ok;
	return r;
}
void hookDoctrine() {
	fd() = FakeDoctrine{};
	stub::hooks().doctrine_wordCost = [](const World& w, int cid, const WordOpts& o) { return state::culture(w, cid) ? fakeCost(o) : 0.0; };
	stub::hooks().doctrine_canSpeak = [](const World& w, int cid, const WordOpts& o) { return fakeCan(w, cid, o); };
	stub::hooks().doctrine_speak = [](World& w, int cid, const WordOpts& o) {
		if (!fakeCan(w, cid, o).ok || fd().refuseSpeak) return -1;
		w.mythos.num["test_last:" + std::to_string(cid)] = w.year;
		fd().spoken++;
		fd().last = o;
		fd().lastCulture = cid;
		return 100 + fd().spoken;
	};
}

struct WCall { int cell; double ang; society::WitnessOpts o; };
std::vector<WCall>& wcalls() { static std::vector<WCall> v; return v; }
void hookWitness() {
	wcalls().clear();
	stub::hooks().society_witness = [](World&, int cell, double ang, const society::WitnessOpts& o) {
		wcalls().push_back({ cell, ang, o });
		return 1;
	};
}

int addFigure(World& w, FigureKind kind, int home, int culture) {
	Figure f;
	f.id = w.mythos.nextId++;
	f.kind = uint8_t(kind);
	f.home = home;
	f.culture = culture;
	f.name = "Ossa";
	f.born = w.year - 30;
	f.credibility = 0.5;
	w.mythos.figures.push_back(f);
	return f.id;
}
Figure* figById(World& w, int id) {
	for (Figure& f : w.mythos.figures) if (f.id == id) return &f;
	return nullptr;
}

} // namespace

// ================================================================== gates

DM_TEST("powers v4: phase 0 locks every Word power, shows its table price and reaches no mythos module") {
	stub::Guard guard(true);
	Fixture f = fixture(0);
	World& w = *f.w;
	for (const char* id : kWord) {
		const pw::PowerDef* d = pw::byId(id);
		if (!ok(d != nullptr, id)) continue;
		eq(int(d->group), int(pw::Group::Word), std::string(id) + " is in the Word group");
		ok(!pw::unlocked(w, id), std::string(id) + " locked at phase 0");
		const pw::CanResult r = pw::can(w, id, f.cell);
		eq(std::string(reasonOf(r)), std::string("locked"), std::string(id) + " can: locked");
		near(pw::cost(w, id, -1, f.cell), d->cost, 0, std::string(id) + " shows the table price");
		pw::UseOpts o;
		o.word.content = WordContent::Answer;
		o.word.clarity = Clarity::Dream;
		o.word.blessing = BlessingKind::Prosperity;
		o.word.renewal = RenewalKind::Water;
		o.choose = true;
		const double awe = w.god.awe;
		std::vector<Event> ev = capture(w, [&] { ok(!pw::use(w, id, f.cell, o), std::string(id) + " use refused"); });
		eq(int(ev.size()), 0, std::string(id) + ": no event");
		near(w.god.awe, awe, 0, std::string(id) + ": nothing spent");
		eq(int(w.god.deeds.count(id)), 0, std::string(id) + ": no deed");
	}
	ok(!anyMythosCall(), "no mythos module is called at phase 0");
	ok(state::extensionsDefault(w), "the world's mythos state is untouched");
}

DM_TEST("powers v4: each Word power needs its mythos phase (1 speak, 2 bless and test, 3 reveal and renew) and its age") {
	struct Need { const char* id; int phase; int age; };
	const Need needs[] = { { "speak", 1, Age::STONE }, { "bless", 2, Age::BRONZE }, { "test", 2, Age::IRON },
	                       { "reveal", 3, Age::BRONZE }, { "renew", 3, Age::IRON } };
	Fixture f = fixture(0);
	World& w = *f.w;
	w.god.aweMax = 1e6;                                  // aweMax never unlocks a Word power (age only)
	for (int ph = 0; ph <= 3; ph++) {
		w.tune<MythosTune>().phase = ph;
		for (int age = Age::STONE; age <= Age::SPACE; age++) {
			w.age = age;
			for (const Need& n : needs) {
				const bool want = ph >= n.phase && age >= n.age;
				eq(pw::unlocked(w, n.id), want, std::string(n.id) + " phase " + std::to_string(ph) + " age " + std::to_string(age));
			}
		}
	}
	// genesis phases: never (the Word is for history)
	w.tune<MythosTune>().phase = 3;
	w.age = Age::SPACE;
	w.phase = Phase::People;
	for (const char* id : kWord) eq(std::string(reasonOf(pw::can(w, id, f.cell))), std::string("phase"), std::string(id) + " before history");
}

DM_TEST("powers v4: the other powers are the same at phase 1 when the god never speaks (guardrail 2)") {
	stub::Guard guard(false);
	Fixture a = fixture(0), b = fixture(1);
	auto run = [](Fixture& f) {
		World& w = *f.w;
		int used = 0;
		std::vector<Event> ev = capture(w, [&] {
			used += pw::use(w, "lightning", f.cell2) ? 1 : 0;
			used += pw::use(w, "inspire", f.cell) ? 1 : 0;
			used += pw::use(w, "heal", f.cell) ? 1 : 0;
			used += pw::use(w, "prophet", f.cell) ? 1 : 0;              // choose false: the v3.1 prophet
			pw::UseOpts r;
			r.dt = 0.1;
			used += pw::use(w, "rain", f.cell, r) ? 1 : 0;
			used += pw::use(w, "quake", f.cell2) ? 1 : 0;
		});
		return std::make_pair(used, int(ev.size()));
	};
	const auto ra = run(a), rb = run(b);
	eq(rb.first, ra.first, "the same powers succeed");
	eq(rb.second, ra.second, "the same number of events");
	near(b.w->god.awe, a.w->god.awe, 0, "the same Awe spent");
	eq(dmt::hash(*b.w), dmt::hash(*a.w), "the same world");
	for (const char* id : { "lightning", "quake", "plague", "heal", "inspire", "prophet", "peace", "rain", "bloom" })
		near(pw::cost(*b.w, id, -1, b.cell), pw::cost(*a.w, id, -1, a.cell), 0, std::string(id) + " costs the same");
}

// ================================================================== speak (3.9)

DM_TEST("powers v4: speak's can() reasons and its shown price (the cheapest word)") {
	stub::Guard guard(false);
	if (!stub::stubbed("doctrine")) return;
	hookDoctrine();
	Fixture f = fixture(1);
	World& w = *f.w;
	const int seaC = [&] { for (int c = 0; c < w.n; c++) if (state::isSea(w, c)) return c; return -1; }();
	pw::CanResult r = pw::can(w, "speak", seaC);
	eq(std::string(reasonOf(r)), std::string("target"), "no town: target");
	eq(std::string(whyOf(r)), std::string("settlement"), "why settlement");
	r = pw::can(w, "speak", -5);
	eq(std::string(reasonOf(r)), std::string("target"), "bad cell: target");
	r = pw::can(w, "speak", f.cell);
	ok(r.ok, "a town of a living people: ok");
	near(pw::cost(w, "speak", -1, f.cell), 30, 1e-12, "shows the cheapest word (a Dream, 30)");
	near(pw::cost(w, "speak", -1, -1), pw::byId("speak")->cost, 0, "no town: the table price");
	// short of the cheapest word: Awe
	w.god.awe = 29;
	eq(std::string(reasonOf(pw::can(w, "speak", f.cell))), std::string("awe"), "29 Awe: awe");
	w.god.awe = 30;
	ok(pw::can(w, "speak", f.cell).ok, "30 Awe: ok");
	// the 10-year gap: target, why none (not now)
	w.god.awe = 1000;
	w.mythos.num["test_last:" + std::to_string(f.cid)] = w.year - 4;
	r = pw::can(w, "speak", f.cell);
	eq(std::string(reasonOf(r)), std::string("target"), "within 10 years of the last word: target");
	eq(std::string(whyOf(r)), std::string("none"), "why none");
	ok(pw::can(w, "speak", f.cell2).ok, "the other people may still be spoken to");
	w.year += 6;
	ok(pw::can(w, "speak", f.cell).ok, "10 years on: ok");
	// Doctrine says the people cannot be spoken to: target, why settlement
	stub::hooks().doctrine_canSpeak = [](const World&, int, const WordOpts&) {
		doctrine::SpeakCheck k; k.why = doctrine::WordWhy::Target; return k;
	};
	r = pw::can(w, "speak", f.cell);
	eq(std::string(whyOf(r)), std::string("settlement"), "doctrine Target: why settlement");
}

DM_TEST("powers v4: speak pays the chosen word's price, only when Doctrine speaks it") {
	stub::Guard guard(false);
	if (!stub::stubbed("doctrine")) return;
	hookDoctrine();
	hookWitness();
	Fixture f = fixture(1);
	World& w = *f.w;
	const Clarity cl[4] = { Clarity::Exact, Clarity::Parable, Clarity::Dream, Clarity::Sign };
	const double price[4] = { 60, 50, 30, 90 };
	for (int k = 0; k < 4; k++) {
		w.year += 11;
		pw::UseOpts o;
		o.word.content = WordContent::Answer;
		o.word.clarity = cl[k];
		const double awe = w.god.awe;
		const int deeds = w.god.deeds["speak"];
		std::vector<Event> ev = capture(w, [&] { ok(pw::use(w, "speak", f.cell, o), "spoken"); });
		near(awe - w.god.awe, price[k], 1e-9, std::string("price of ") + toStr(cl[k]));
		eq(countEv(ev, Ev::PowerUsed), 1, "one power:used");
		eq(w.god.deeds["speak"], deeds + 1, "one deed");
		eq(fd().lastCulture, f.cid, "spoken to the clicked town's people");
	}
	eq(int(wcalls().size()), 0, "speaking is not witnessed by Powers (Doctrine and Mythos tell it)");
	// a Dream with no town named is dreamed in the clicked town; a named one is kept
	w.year += 11;
	pw::UseOpts d;
	d.word.content = WordContent::Answer;
	d.word.clarity = Clarity::Dream;
	ok(pw::use(w, "speak", f.cell, d), "a dream");
	eq(fd().last.ref, f.sid, "the dreamer's town is the clicked town");
	w.year += 11;
	const int other = town(w, findLand(w, [&](int c) { return w.owner[size_t(c)] < 0 && w.geo->angle(c, f.cell) > 0.2 && w.geo->angle(c, f.cell2) > 0.2; }), f.cid, 50);
	d.word.ref = other;
	ok(pw::use(w, "speak", f.cell, d), "a dream for a named town");
	eq(fd().last.ref, other, "the named town is kept");
	// the gap: refused, nothing spent, no event
	{
		const double awe = w.god.awe;
		std::vector<Event> ev = capture(w, [&] { ok(!pw::use(w, "speak", f.cell, d), "within 10 years: refused"); });
		eq(int(ev.size()), 0, "no event");
		near(w.god.awe, awe, 0, "nothing spent");
	}
	// Doctrine refuses to speak it although canSpeak said yes: nothing spent, no power:used, no deed
	w.year += 11;
	fd().refuseSpeak = true;
	{
		const double awe = w.god.awe;
		const int deeds = w.god.deeds["speak"];
		std::vector<Event> ev = capture(w, [&] { ok(!pw::use(w, "speak", f.cell, d), "Doctrine refused"); });
		eq(countEv(ev, Ev::PowerUsed), 0, "no power:used");
		near(w.god.awe, awe, 0, "nothing spent on a refused word");
		eq(w.god.deeds["speak"], deeds, "no deed");
	}
	fd().refuseSpeak = false;
	// content Doctrine will not take (a Warning before v2): refused, no awe:short
	{
		pw::UseOpts wn;
		wn.word.content = WordContent::Warning;
		const double awe = w.god.awe;
		std::vector<Event> ev = capture(w, [&] { ok(!pw::use(w, "speak", f.cell, wn), "a warning is refused at phase 1"); });
		eq(int(ev.size()), 0, "no event");
		near(w.god.awe, awe, 0, "nothing spent");
	}
	// enough for the cheapest word but not for this one: awe:short { speak }, nothing spent, Doctrine not asked to speak
	{
		w.god.awe = 70;
		pw::UseOpts s;
		s.word.content = WordContent::Answer;
		s.word.clarity = Clarity::Sign;
		const int spoken = fd().spoken;
		std::vector<Event> ev = capture(w, [&] { ok(!pw::use(w, "speak", f.cell, s), "a Sign needs 90"); });
		eq(countEv(ev, Ev::AweShort), 1, "awe:short");
		if (!ev.empty()) if (const EvAweShort* a = ev.back().as<EvAweShort>()) eq(a->id, std::string("speak"), "awe:short id");
		near(w.god.awe, 70, 0, "nothing spent");
		eq(fd().spoken, spoken, "not spoken");
	}
}

DM_TEST("powers v4: speak through the real Doctrine (all): a Dream costs 30, one word per 10 years") {
	stub::Guard guard(false);
	if (stub::stubbed("doctrine")) return;
	Fixture f = fixture(1);
	World& w = *f.w;
	mythos::rebuild(w);
	if (!ok(pw::can(w, "speak", f.cell).ok, "can speak")) return;
	pw::UseOpts o;
	o.word.content = WordContent::Answer;
	o.word.clarity = Clarity::Dream;
	const double awe = w.god.awe;
	const size_t words = w.mythos.words.size();
	ok(pw::use(w, "speak", f.cell, o), "spoken");
	near(awe - w.god.awe, w.tune<DoctrineTune>().wordCost[size_t(Clarity::Dream)], 1e-9, "a Dream's price");
	eq(w.mythos.words.size(), words + 1, "one utterance");
	if (!w.mythos.words.empty()) {
		const Utterance& u = w.mythos.words.back();
		eq(u.culture, f.cid, "to the clicked town's people");
		eq(int(u.clarity), int(Clarity::Dream), "a Dream");
		const auto it = u.data.find("dreamer");
		ok(it != u.data.end() && int(it->second) == f.sid, "dreamed in the clicked town");
	}
	const pw::CanResult r = pw::can(w, "speak", f.cell);
	eq(std::string(reasonOf(r)), std::string("target"), "the same year again: target");
	const double after = w.god.awe;
	ok(!pw::use(w, "speak", f.cell, o), "refused");
	near(w.god.awe, after, 0, "nothing spent");
	eq(checkFinite(w), std::string(), "finite");
}

// ================================================================== bless (3.16), test (3.18), reveal (3.25), renew (3.30)

DM_TEST("powers v4: bless costs its kind's price, only when Mythos blesses the town") {
	stub::Guard guard(true);
	if (!stub::stubbed("mythos")) return;
	hookWitness();
	Fixture f = fixture(2);
	World& w = *f.w;
	bool accept = true;
	BlessingKind asked = BlessingKind::None;
	stub::hooks().mythos_bless = [&](World&, int sid, BlessingKind k) { asked = k; return accept && sid >= 0; };
	const MythosTune& M = w.tune<MythosTune>();
	near(pw::cost(w, "bless", -1, f.cell), std::min({ M.prosperityCost, M.healerCost, M.enduranceCost }), 0, "shows the cheapest blessing");
	ok(pw::can(w, "bless", f.cell).ok, "can bless a town");
	struct K { BlessingKind k; double cost; };
	for (const K& k : { K{ BlessingKind::Prosperity, M.prosperityCost }, K{ BlessingKind::Healer, M.healerCost }, K{ BlessingKind::Endurance, M.enduranceCost } }) {
		pw::UseOpts o;
		o.word.blessing = k.k;
		const double awe = w.god.awe;
		const size_t wc = wcalls().size();
		std::vector<Event> ev = capture(w, [&] { ok(pw::use(w, "bless", f.cell, o), "blessed"); });
		near(awe - w.god.awe, k.cost, 1e-9, std::string("price of ") + toStr(k.k));
		eq(int(asked), int(k.k), "the chosen kind reached Mythos");
		eq(countEv(ev, Ev::PowerUsed), 1, "power:used");
		eq(wcalls().size(), wc + 1, "witnessed once");
		if (wcalls().size() == wc + 1) {
			eq(wcalls().back().o.kind, std::string("bless"), "as a blessing");
			ok(wcalls().back().o.good > 0, "a good deed");
		}
	}
	// no kind chosen: refused before Mythos is asked
	{
		stub::calls().clear();
		pw::UseOpts o;
		const double awe = w.god.awe;
		ok(!pw::use(w, "bless", f.cell, o), "no blessing chosen: refused");
		eq(stub::called("mythos_bless"), 0, "Mythos not asked");
		near(w.god.awe, awe, 0, "nothing spent");
	}
	// Mythos refuses: nothing spent, no power:used, not witnessed
	{
		accept = false;
		pw::UseOpts o;
		o.word.blessing = BlessingKind::Prosperity;
		const double awe = w.god.awe;
		const size_t wc = wcalls().size();
		std::vector<Event> ev = capture(w, [&] { ok(!pw::use(w, "bless", f.cell, o), "refused"); });
		near(w.god.awe, awe, 0, "nothing spent");
		eq(countEv(ev, Ev::PowerUsed), 0, "no power:used");
		eq(wcalls().size(), wc, "not witnessed");
		accept = true;
	}
	// short of this kind: awe:short { bless }, Mythos not asked
	{
		w.god.awe = M.prosperityCost + 1;
		stub::calls().clear();
		pw::UseOpts o;
		o.word.blessing = BlessingKind::Healer;
		std::vector<Event> ev = capture(w, [&] { ok(!pw::use(w, "bless", f.cell, o), "a healer costs more"); });
		eq(countEv(ev, Ev::AweShort), 1, "awe:short");
		eq(stub::called("mythos_bless"), 0, "Mythos not asked");
		w.god.awe = M.prosperityCost - 1;
		eq(std::string(reasonOf(pw::can(w, "bless", f.cell))), std::string("awe"), "below the cheapest: awe");
	}
	// the price follows the tune
	w.god.awe = 1000;
	w.tune<MythosTune>().prosperityCost = 10;
	near(pw::cost(w, "bless", -1, f.cell), 10, 0, "a retuned price is shown");
}

DM_TEST("powers v4: test needs a living notable figure at the town and costs the trial's price") {
	stub::Guard guard(false);
	Fixture f = fixture(2);
	World& w = *f.w;
	pw::CanResult r = pw::can(w, "test", f.cell);
	eq(std::string(reasonOf(r)), std::string("target"), "nobody to test: target");
	eq(std::string(whyOf(r)), std::string("none"), "why none");
	// not testable: a candidate, a dead ruler, a ruler living elsewhere
	addFigure(w, FigureKind::Candidate, f.sid, f.cid);
	const int deadId = addFigure(w, FigureKind::Ruler, f.sid, f.cid);
	figById(w, deadId)->died = w.year - 1;
	figById(w, deadId)->fate = uint8_t(FigureFate::Died);
	addFigure(w, FigureKind::Ruler, f.sid2, f.cid2);
	eq(std::string(reasonOf(pw::can(w, "test", f.cell))), std::string("target"), "only the untestable here: target");
	for (FigureKind k : { FigureKind::Ruler, FigureKind::Messenger, FigureKind::Healer, FigureKind::Hero }) {
		Fixture g = fixture(2);
		addFigure(*g.w, k, g.sid, g.cid);
		ok(pw::can(*g.w, "test", g.cell).ok, std::string("a ") + toStr(k) + " may be tested");
	}
	const int ruler = addFigure(w, FigureKind::Ruler, f.sid, f.cid);
	ok(pw::can(w, "test", f.cell).ok, "a living ruler at home: ok");
	const auto& tc = w.tune<CharactersTune>().trialCost;
	near(pw::cost(w, "test", -1, f.cell), *std::min_element(tc.begin(), tc.end()), 0, "shows the cheapest trial");
	if (!stub::stubbed("characters")) return;
	bool accept = true;
	int askedFig = -1;
	TrialKind askedKind = TrialKind::Power;
	stub::hooks().characters_test = [&](World&, int fig, TrialKind k) { askedFig = fig; askedKind = k; return accept; };
	for (int k = 0; k < 4; k++) {
		pw::UseOpts o;
		o.word.trial = TrialKind(k);
		const double awe = w.god.awe;
		std::vector<Event> ev = capture(w, [&] { ok(pw::use(w, "test", f.cell, o), "tested"); });
		near(awe - w.god.awe, tc[size_t(k)], 1e-9, std::string("price of ") + toStr(TrialKind(k)));
		eq(askedFig, ruler, "the testable figure of the town");
		eq(int(askedKind), k, "the chosen trial");
		eq(countEv(ev, Ev::PowerUsed), 1, "power:used");
	}
	// a named figure that is not at this town: refused, Characters not asked
	{
		pw::UseOpts o;
		o.word.figure = deadId + 1000;
		askedFig = -1;
		const double awe = w.god.awe;
		ok(!pw::use(w, "test", f.cell, o), "a figure that is not here");
		eq(askedFig, -1, "Characters not asked");
		near(w.god.awe, awe, 0, "nothing spent");
	}
	// Characters refuses: nothing spent
	accept = false;
	{
		pw::UseOpts o;
		const double awe = w.god.awe;
		std::vector<Event> ev = capture(w, [&] { ok(!pw::use(w, "test", f.cell, o), "refused"); });
		near(w.god.awe, awe, 0, "nothing spent");
		eq(countEv(ev, Ev::PowerUsed), 0, "no power:used");
	}
}

DM_TEST("powers v4: reveal costs the knowledge's price, needs a clarity, and spends nothing when Doctrine refuses") {
	stub::Guard guard(false);
	if (!stub::stubbed("doctrine")) return;
	Fixture f = fixture(3);
	World& w = *f.w;
	w.age = Age::SAIL;
	bool accept = true;
	stub::hooks().doctrine_reveal = [&](World&, int cid, RevelationKind, Clarity) { return accept && cid >= 0; };
	const auto& rc = w.tune<DoctrineTune>().revealCost;
	near(pw::cost(w, "reveal", -1, f.cell), *std::min_element(rc.begin(), rc.end()), 0, "shows the cheapest knowledge");
	ok(pw::can(w, "reveal", f.cell).ok, "can reveal");
	for (int k = 0; k < 5; k++) {
		pw::UseOpts o;
		o.word.revelation = RevelationKind(k);
		o.word.clarity = Clarity::Parable;
		const double awe = w.god.awe;
		ok(pw::use(w, "reveal", f.cell, o), "revealed");
		near(awe - w.god.awe, rc[size_t(k)], 1e-9, std::string("price of ") + toStr(RevelationKind(k)));
		w.god.awe = 1000;
	}
	pw::UseOpts bad;
	bad.word.clarity = Clarity(9);
	ok(!pw::use(w, "reveal", f.cell, bad), "no such clarity: refused");
	near(w.god.awe, 1000, 0, "nothing spent");
	accept = false;
	pw::UseOpts o;
	std::vector<Event> ev = capture(w, [&] { ok(!pw::use(w, "reveal", f.cell, o), "Doctrine refused"); });
	near(w.god.awe, 1000, 0, "nothing spent");
	eq(countEv(ev, Ev::PowerUsed), 0, "no power:used");
}

DM_TEST("powers v4: renew needs 600 Awe, costs all of it, and happens once per world") {
	stub::Guard guard(false);
	Fixture f = fixture(3);
	World& w = *f.w;
	const double minAwe = w.tune<RenewalTune>().minAwe;
	w.god.awe = minAwe - 1;
	eq(std::string(reasonOf(pw::can(w, "renew", f.cell))), std::string("awe"), "below 600: awe");
	near(pw::cost(w, "renew", -1, f.cell), minAwe, 0, "shows at least 600");
	w.god.awe = 900;
	near(pw::cost(w, "renew", -1, f.cell), 900, 0, "shows all the Awe");
	ok(pw::can(w, "renew", f.cell).ok, "can renew");
	// no kind chosen: refused, nothing spent, no event
	{
		pw::UseOpts o;
		std::vector<Event> ev = capture(w, [&] { ok(!pw::use(w, "renew", f.cell, o), "no kind: refused"); });
		eq(int(ev.size()), 0, "no event");
		near(w.god.awe, 900, 0, "nothing spent");
	}
	if (stub::stubbed("renewal")) {
		std::vector<int> remnant;
		bool accept = false;
		stub::hooks().renewal_begin = [&](World&, RenewalKind, const std::vector<int>& r) { remnant = r; return accept; };
		pw::UseOpts o;
		o.word.renewal = RenewalKind::Water;
		o.word.remnant = { f.sid, -1, f.sid };
		ok(!pw::use(w, "renew", f.cell, o), "Renewal refused");
		near(w.god.awe, 900, 0, "nothing spent");
		eq(int(remnant.size()), 1, "the remnant without repeats or -1");
		accept = true;
		std::vector<Event> ev = capture(w, [&] { ok(pw::use(w, "renew", f.cell, o), "renewed"); });
		near(w.god.awe, 0, 0, "all the Awe is spent");
		eq(countEv(ev, Ev::PowerUsed), 1, "power:used");
		eq(w.god.deeds["renew"], 1, "a deed");
	}
	// once per world
	w.god.awe = 900;
	w.mythos.renewal = int(RenewalKind::Water);
	pw::CanResult r = pw::can(w, "renew", f.cell);
	eq(std::string(reasonOf(r)), std::string("target"), "renewed before: target");
	w.mythos.renewal = 0;
	w.mythos.renewedYear = 900;
	eq(std::string(reasonOf(pw::can(w, "renew", f.cell))), std::string("target"), "renewed in a past year: target");
}

// ================================================================== pay

DM_TEST("powers v4: pay spends for a mythos act, refuses without Awe, and rate-limits awe:short per act") {
	Fixture f = fixture(1);
	World& w = *f.w;
	w.god.awe = 100;
	std::vector<Event> ev = capture(w, [&] {
		ok(pw::pay(w, "card:judgement", 0), "free");
		ok(pw::pay(w, "card:judgement", -5), "negative is free");
		ok(!pw::pay(w, "look", std::nan("")), "NaN refused");
		ok(!pw::pay(w, "look", INFINITY), "infinity refused");
	});
	eq(int(ev.size()), 0, "no event for free or refused-as-malformed amounts");
	near(w.god.awe, 100, 0, "nothing spent");
	ev = capture(w, [&] { ok(pw::pay(w, "look", 20), "paid"); });
	near(w.god.awe, 80, 1e-12, "20 spent");
	eq(int(ev.size()), 0, "paying emits nothing (no power:used)");
	eq(int(w.god.deeds.count("look")), 0, "never a deed");
	ok(pw::pay(w, "presence", 80), "exactly what is left");
	near(w.god.awe, 0, 0, "all spent");
	// short: false, awe:short { what }, at most once per aweShortEvery per act
	ev = capture(w, [&] {
		ok(!pw::pay(w, "presence", 100), "short");
		ok(!pw::pay(w, "presence", 100), "short again at once");
		ok(!pw::pay(w, "keeper:upkeep", 2), "another act");
	});
	eq(countEv(ev, Ev::AweShort), 2, "one awe:short per act");
	if (ev.size() >= 2) {
		const EvAweShort* a = ev[0].as<EvAweShort>();
		const EvAweShort* b = ev[1].as<EvAweShort>();
		ok(a && a->id == "presence", "awe:short presence");
		ok(b && b->id == "keeper:upkeep", "awe:short keeper:upkeep");
	}
	near(w.god.awe, 0, 0, "nothing spent when short");
	// time passes (ticks count at the fastest game speed): the limit opens again
	const PowersTune& T = w.tune<PowersTune>();
	const int64_t ticks = int64_t(std::ceil(T.aweShortEvery * T.maxGameSpeed / sim::kRealPerTick)) + 1;
	w.tick += ticks;
	ev = capture(w, [&] { ok(!pw::pay(w, "presence", 100), "still short"); });
	eq(countEv(ev, Ev::AweShort), 1, "awe:short again after aweShortEvery seconds");
	w.god.awe = std::nan("");
	ok(!pw::pay(w, "look", 1), "a broken Awe value pays nothing");
	ok(std::isnan(w.god.awe) || w.god.awe >= 0, "never negative");
}

// ================================================================== withdrawal (3.29, a v3 system)

DM_TEST("powers v4: while the god keeps a century of silence every power refuses (phase 3 only)") {
	Fixture f = fixture(3);
	World& w = *f.w;
	w.mythos.withdrawnUntil = w.year + 50;
	for (const char* id : { "lightning", "heal", "speak", "rain", "prophet" }) {
		eq(std::string(reasonOf(pw::can(w, id, f.cell))), std::string("withdrawn"), std::string(id) + ": withdrawn");
		const double awe = w.god.awe;
		pw::UseOpts o;
		o.dt = 0.1;
		std::vector<Event> ev = capture(w, [&] { ok(!pw::use(w, id, f.cell, o), std::string(id) + " refused"); });
		eq(int(ev.size()), 0, "no event");
		near(w.god.awe, awe, 0, "nothing spent");
	}
	w.year += 51;
	ok(pw::can(w, "lightning", f.cell).ok, "the century is over");
	// below phase 3 the field means nothing to Powers
	Fixture g = fixture(1);
	g.w->mythos.withdrawnUntil = g.w->year + 50;
	ok(pw::can(*g.w, "lightning", g.cell).ok, "phase 1: not withdrawn");
	g.w->tune<MythosTune>().phase = 0;
	ok(pw::can(*g.w, "lightning", g.cell).ok, "phase 0: not withdrawn");
}

// ================================================================== the Messenger choice (3.8)

DM_TEST("powers v4: prophet without the choice, or at phase 0, is the v3.1 prophet") {
	stub::Guard guard(false);
	for (int ph : { 0, 1 }) {
		Fixture a = fixture(ph), b = fixture(ph);
		pw::UseOpts plain, choose;
		choose.choose = true;
		const bool ra = pw::use(*a.w, "prophet", a.cell, plain);
		const bool rb = pw::use(*b.w, "prophet", b.cell, ph == 0 ? choose : plain);
		eq(rb, ra, "the same answer");
		eq(dmt::hash(*b.w), dmt::hash(*a.w), "the same world (phase " + std::to_string(ph) + ")");
		near(b.w->god.awe, a.w->god.awe, 0, "the same Awe");
	}
	Fixture p0 = fixture(0), p1 = fixture(1);
	pw::use(*p0.w, "prophet", p0.cell);
	pw::use(*p1.w, "prophet", p1.cell);
	eq(dmt::hash(*p1.w), dmt::hash(*p0.w), "phase 1 without the choice = phase 0");
}

DM_TEST("powers v4: prophet with the choice opens the MessengerChoice card and spends nothing (stubs)") {
	stub::Guard guard(true);
	if (!stub::stubbed("cards") || !stub::stubbed("characters")) return;
	Fixture f = fixture(1);
	World& w = *f.w;
	std::vector<cards::Proposal> opened;
	int raised = 0;
	int messengerOf = -1;
	size_t nCandidates = 3;
	stub::hooks().cards_open = [&](World&, const cards::Proposal& p) { opened.push_back(p); return 40 + int(opened.size()); };
	stub::hooks().characters_candidates = [&](const World&, int sid) {
		std::vector<Figure> out(nCandidates);
		for (size_t i = 0; i < out.size(); i++) { out[i].kind = uint8_t(FigureKind::Candidate); out[i].home = sid; out[i].name = "Cand"; }
		return out;
	};
	stub::hooks().characters_messenger = [&](const World&, int) { return messengerOf; };
	stub::hooks().society_raiseProphet = [&](World&, int, const society::RaiseOpts&) -> Prophet* { raised++; return nullptr; };
	pw::UseOpts o;
	o.choose = true;
	const double awe = w.god.awe;
	std::vector<Event> ev = capture(w, [&] { ok(pw::use(w, "prophet", f.cell, o), "the card opens"); });
	eq(int(opened.size()), 1, "one card");
	if (opened.size() == 1) {
		const cards::Proposal& p = opened[0];
		eq(int(p.kind), int(CardKind::MessengerChoice), "a MessengerChoice card");
		eq(p.culture, f.cid, "for the town's people");
		eq(p.settlement, f.sid, "on the town");
		eq(p.ref[0], f.sid, "ref = the town");
		eq(p.cell, f.cell, "at its cell");
		ok(p.severity > 0 && p.severity <= 1, "a severity in (0, 1]");
		ok(p.data.count("armed") && p.data.at("armed") == 1, "armed by the god");
		ok(p.data.count("decade") && p.data.at("decade") == std::floor(w.year / 10), "the decade (same three candidates)");
	}
	near(w.god.awe, awe, 0, "nothing spent: the answer pays");
	eq(countEv(ev, Ev::PowerUsed), 0, "no power:used yet");
	eq(raised, 0, "no prophet raised yet");
	eq(w.god.deeds["prophet"], 0, "no deed yet");
	// refused: no candidate, a messenger already, a prophet already, short of the power's price
	nCandidates = 0;
	ok(!pw::use(w, "prophet", f.cell, o), "no candidate: refused");
	nCandidates = 3;
	messengerOf = 77;
	ok(!pw::use(w, "prophet", f.cell, o), "a living messenger: refused");
	messengerOf = -1;
	w.cultures[size_t(f.cid)].prophet = Prophet{ "Ves", f.sid, w.year + 30, -1 };
	ok(!pw::use(w, "prophet", f.cell, o), "a prophet already: refused");
	w.cultures[size_t(f.cid)].prophet.reset();
	w.god.awe = pw::byId("prophet")->cost - 1;
	ev = capture(w, [&] { ok(!pw::use(w, "prophet", f.cell, o), "short of the power's price"); });
	eq(countEv(ev, Ev::AweShort), 1, "awe:short");
	eq(int(opened.size()), 1, "no other card");
	near(w.god.awe, pw::byId("prophet")->cost - 1, 0, "nothing spent");
	eq(raised, 0, "never the v3.1 prophet with the choice");
}

DM_TEST("powers v4: prophet with the choice through the real Characters and Cards (all)") {
	stub::Guard guard(false);
	if (stub::stubbed("cards") || stub::stubbed("characters")) return;
	Fixture f = fixture(1);
	World& w = *f.w;
	mythos::rebuild(w);
	pw::UseOpts o;
	o.choose = true;
	const double awe = w.god.awe;
	bool r = false;
	std::vector<Event> ev = capture(w, [&] { r = pw::use(w, "prophet", f.cell, o); });
	near(w.god.awe, awe, 0, "nothing spent");
	eq(countEv(ev, Ev::PowerUsed), 0, "no power:used");
	ok(!w.cultures[size_t(f.cid)].prophet.has_value(), "no prophet yet");
	if (r) {
		bool found = false;
		for (const Card& c : w.mythos.cards)
			if (c.kind == CardKind::MessengerChoice && c.state == uint8_t(CardState::Open) && c.settlement == f.sid) found = true;
		ok(found, "an open MessengerChoice card on the town");
		eq(countEv(ev, Ev::Card), 1, "card { open }");
	}
	eq(checkFinite(w), std::string(), "finite");
}
