// DEMIURGE — SimTests/test_doctrine_rite.cpp : the Rite, what worship means (SPEC-MYTHOS 3.13). Track M3.
#include "test_doctrine_fixture.h"

using namespace dm;
using namespace dmt;
using namespace dmt::doc;

DM_TEST("doctrine rite: the people's own rite follows their traits and the world") {
	DocWorld d = docWorld();
	World& w = *d.w;
	Traits& t = w.cultures[0].traits;
	const auto rite = [&] { return Rite(doctrine::customStance(w, d.vai, DecreeArea::Worship)); };
	t = Traits{}; t.piety = 0.95; t.ambition = 0.1; t.compassion = 0.2; t.curiosity = 0.2;
	eq(int(rite()), int(Rite::Prayer), "pious: Prayer");
	t.ambition = 0.95;
	eq(int(rite()), int(Rite::Temples), "pious and ambitious: Temples");
	t = Traits{}; t.compassion = 0.95; t.piety = 0.3; t.curiosity = 0.3;
	eq(int(rite()), int(Rite::Kindness), "compassionate: Kindness");
	t = Traits{}; t.curiosity = 0.95; t.piety = 0.3; t.compassion = 0.3;
	eq(int(rite()), int(Rite::Art), "curious: Art");
	t = Traits{};
	w.settings.archetype = Archetype::Beautiful;
	eq(int(rite()), int(Rite::Art), "a Beautiful world: Art");
	w.settings.archetype = Archetype::Generous;
	eq(int(rite()), int(Rite::World), "a Generous world: World");
}

DM_TEST("doctrine rite: a Worship decree by the Word is the rite; mythos::rite reads it") {
	DocWorld d = docWorld();
	World& w = *d.w;
	w.mythos.faith[0].agency = 1;                            // no catastrophe
	WordOpts o; o.content = WordContent::Decree; o.area = DecreeArea::Worship; o.stance = int(Rite::Kindness);
	o.clarity = Clarity::Exact; o.channel = Channel::Sky;
	std::string stance;
	const auto ev = capture(w, [&] { ok(doctrine::speak(w, d.vai, o) > 0, "spoken"); });
	for (const Event& e : ev) if (const EvDecree* x = e.as<EvDecree>()) stance = x->stance;
	eq(stance, std::string("kindness"));
	eq(int(mythos::rite(w, d.vai)), int(Rite::Kindness), "the rite");
	near(doctrine::strictness(w, DecreeArea::Worship, int(Rite::Kindness)), 0.5, 1e-12, "rites: 0.5 until the balance owner sets them");
}

DM_TEST("doctrine rite: what presses each rite (3.13 pressure column)") {
	for (int ri = 0; ri < 5; ri++) {
		DocWorld d = docWorld();
		World& w = *d.w;
		setDecree(w, d.vai, DecreeArea::Worship, ri);
		w.mythos.faith[0].obedience = 0;
		// pressure on: famine (Temples, World), war (Kindness), ambition (Art); Prayer: never
		w.settlements[size_t(d.towns[1])].famine = 3;
		setWar(w, d.vai, d.oru, d.vai);
		w.cultures[0].traits.ambition = 0.8;
		int n = 0, famineTown = 0;
		(void)w.bus.on<EvBreach>([&](const EvBreach& e) { n++; if (e.settlement == d.towns[1]) famineTown++; });
		for (int i = 0; i < 20000; i++) { w.year += 1; doctrine::yearly(w); }
		const Rite r = Rite(ri);
		if (r == Rite::Prayer) eq(n, 0, "Prayer is never breached");
		else {
			const double p = r == Rite::Art ? 0.6 : 0.8;
			const double expect = 0.02 * 0.5 * p * 20000;
			ok(std::fabs(n - expect) <= 0.3 * expect, std::string(stanceName(DecreeArea::Worship, ri)) + ": " + std::to_string(n) +
				" breaches, expected about " + std::to_string(expect));
		}
		if (r == Rite::Temples || r == Rite::World) eq(famineTown, n, "famine presses the hungry town");
	}
}

DM_TEST("doctrine: a long synthetic history stays finite and deterministic") {
	const auto run = [](uint32_t seed) {
		DocWorld d = docWorld(1, true, seed);
		World& w = *d.w;
		addMessenger(w, d.vai, d.towns[0]);
		w.mythos.faith[0].obedience = 0.3;
		w.mythos.faith[1].obedience = 0.2;
		setWar(w, d.vai, d.oru, d.oru);
		for (int i = 0; i < 1500; i++) {
			w.year += 1;
			if (i % 4 == 0) for (Settlement& s : w.settlements) s.famine = (i / 4) % 3;
			if (i % 25 == 0) {
				WordOpts o; o.content = WordContent::Decree; o.area = DecreeArea(i / 25 % 6); o.stance = (i / 150) % 3;
				o.clarity = Clarity(i / 25 % 4);
				o.channel = o.clarity == Clarity::Dream ? Channel::Dream : o.clarity == Clarity::Sign ? Channel::Sky : Channel::Messenger;
				doctrine::speak(w, i % 50 == 0 ? d.vai : d.oru, o);
			}
			doctrine::yearly(w);
		}
		noNaN(w, "1,500 years");
		ok(checkFinite(w).empty(), checkFinite(w));
		return hash(w);
	};
	const uint32_t a = run(3), b = run(3);
	eq(a, b, "two runs, one world");
}
