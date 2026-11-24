// DEMIURGE — SimTests/test_state_mythos.cpp : the save contract of the v4 mythos build (contracts track M0,
// SPEC-MYTHOS 1.3 and the M0 acceptance list): version 2 with tagged extension blocks, version-1 saves still
// loading, the populated MythosState round trip, MYTH id validation, and every new enum's names.
// Key: run.ps1 state (also in all).
#include "TestFramework.h"
#include "TestWorld.h"
#include "fixture_state_v1.h"

#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace dm;
using namespace dmt;

namespace {

std::vector<uint8_t> fixtureV1() { return std::vector<uint8_t>(fixture::kStateV1, fixture::kStateV1 + sizeof(fixture::kStateV1)); }

// A small world with cultures and settlements whose MythosState names them, every record type present.
std::unique_ptr<World> populated() {
	auto w = state::create({ 5, "Myth", 2 });
	Culture& a = state::newCulture(*w); a.name = "Umar"; a.lang = "x";
	state::newCulture(*w, 0);
	state::newSettlement(*w, 3, 0).capital = true;
	state::newSettlement(*w, 9, 1);
	w->cultures[0].capital = 0;
	w->cultures[0].traits.compassion = 0.8; w->cultures[1].traits.courage = 0.25; w->cultures[0].traits.ambition = 0.3;
	w->cultures[0].prophet = Prophet{ "Ossa", 0, 900, 1 };
	w->settings.archetype = Archetype::Strange;
	Ending en; en.culture = 0; en.settlement = 0; en.year = 3000; en.legacy = Legacy::Unneeded; w->ending = en;
	w->wild[5] = 31; w->wild[6] = 255;
	MythosState& m = w->mythos;
	m.presence = Presence::Messengers; m.presenceSince = 400; m.archetype = Archetype::Strange;
	Faith f; f.aspect = Aspect::Parent; f.image = { 0.4, 0.1, 0.05, 0 }; f.agency = 0.61; f.death = int8_t(DeathBelief::Turning);
	f.deathSure = int8_t(Certainty::Glimpsed); f.bearer = 1200; f.charge = int8_t(Charge::Garden); f.data["answered"] = 3;
	m.faith = { f, Faith{} };
	Factors fa; fa.research = 1.04; fa.farmAncient = false; fa.expect[4] = 1.2; m.factors = { fa, Factors{} };
	TownMyth t; t.settlement = 0; t.blessing = BlessingKind::Healer; t.blessedUntil = 960; t.healer = 2; m.towns = { t };
	MythAct act; act.year = 811; act.kind = "rain"; act.cell = 3; act.settlement = 0; act.culture = 0; act.good = 0.6; m.acts = { act };
	Decree d; d.culture = 0; d.area = DecreeArea::Care; d.stance = int8_t(CareStance::Shelter); d.clarity = Clarity::Parable; d.kept = 0.7; d.by = 1;
	Decree cu; cu.culture = 1; cu.area = DecreeArea::Worship; cu.stance = int8_t(Rite::Art); cu.custom = true;
	m.decrees = { d, cu };
	Utterance u; u.id = 3; u.culture = 0; u.figure = 1; u.content = uint8_t(WordContent::Decree); u.area = int8_t(DecreeArea::Care);
	u.meant = 0; u.heard = 1; u.outcome = uint8_t(WordOutcome::Wisdom); u.fidelity = 0.62; m.words = { u };
	Pact p; p.id = 4; p.culture = 0; p.godTerm = uint8_t(GodTerm::Healing); p.peopleTerm = uint8_t(PeopleTerm::NoWar); p.sworn = 700; p.until = 1000;
	p.state = uint8_t(PactState::Kept); p.figure = 1; m.pacts = { p };
	Revelation rv; rv.culture = 1; rv.kind = uint8_t(RevelationKind::Writing); rv.outcome = uint8_t(RevelationOutcome::Hoarded); rv.early = true;
	m.revelations = { rv };
	Figure g1; g1.id = 1; g1.kind = uint8_t(FigureKind::Messenger); g1.archetype = uint8_t(MessengerArchetype::Child); g1.culture = 0; g1.home = 0;
	g1.name = "Ossa"; g1.byname = "of the Goats"; g1.credibility = 0.45; g1.heart = { 0.9f, 0.4f, 0.7f }; g1.flags = FigureFlag::HeartSeen;
	Figure g2; g2.id = 2; g2.kind = uint8_t(FigureKind::Healer); g2.culture = 0; g2.home = 0; g2.name = "Ithel"; g2.trial = int(TrialKind::Hardship);
	m.figures = { g1, g2 };
	Card c; c.id = 5; c.kind = CardKind::Judgement; c.state = uint8_t(CardState::Open); c.culture = 0; c.settlement = 0; c.figure = 2; c.cell = 3;
	c.opened = 800; c.deadline = 860; c.severity = 0.6; c.ref = { 1, -1, -1, 7 }; c.data["heart"] = 1; m.cards = { c };
	Legend l; l.id = 6; l.culture = 0; l.settlement = 0; l.cell = 3; l.deed = "rain"; l.truth = 1; l.told = 2.5; l.credit = uint8_t(LegendCredit::Messenger);
	m.legends = { l };
	House h; h.culture = 0; h.strength = 0.6; h.corruption = 0.3; m.houses = { h };
	RivalVoice rvv; rvv.id = 7; rvv.culture = 1; rvv.figure = 2; rvv.following = 0.2; rvv.way = -1; m.rivals = { rvv };
	Omen o; o.id = 8; o.kind = uint8_t(OmenKind::Eruption); o.cell = 9; o.culture = 1; o.due = 840; o.warned = uint8_t(WarningWay::Dreams); m.omens = { o };
	Keeper k; k.id = 9; k.domain = uint8_t(KeeperDomain::Deep); k.disposition = uint8_t(Temper::Stern); k.culture = 0; k.name = "Asheru"; m.keepers = { k };
	m.withdrawnUntil = 1100; m.withdrawAnnounced = true; m.renewal = 1; m.renewedYear = 990; m.num["cards_lastOpen"] = 800; m.nextId = 10;
	return w;
}

bool sameBytes(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) { return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size()) == 0; }

} // namespace

DM_TEST("state v2: a version-1 save of the pre-mythos build loads (empty mythos, wild 0) and re-saves as v1 byte for byte") {
	const std::vector<uint8_t> v1 = fixtureV1();
	std::string why;
	auto w = state::deserialize(v1, &why);
	if (!ok(w != nullptr, "the v1 fixture loads: " + why)) return;
	eq(w->name, std::string("Fixture"), "name");
	eq(int(w->cultures.size()), 2, "cultures");
	eq(int(w->settlements.size()), 3, "settlements");
	ok(w->cultures[0].prophet && w->cultures[0].prophet->figure == -1, "prophet.figure defaults to -1");
	ok(w->ending && w->ending->legacy == Legacy::None, "ending.legacy defaults to None");
	eq(int(w->settings.archetype), int(Archetype::Balanced), "archetype Balanced");
	eq(w->cultures[1].traits.compassion, 0.5, "new traits at 0.5");
	int wild = 0;
	for (uint8_t v : w->wild) wild += v;
	eq(wild, 0, "wild 0");
	ok(w->mythos.faith.empty() && w->mythos.cards.empty() && w->mythos.nextId == 1, "empty MythosState");
	ok(state::extensionsDefault(*w), "extensionsDefault");
	std::vector<uint8_t> again;
	ok(state::serialize(*w, again, &why, 1), "serialize version 1");
	ok(sameBytes(again, v1), "the legacy writer reproduces the pre-mythos bytes exactly (" + std::to_string(again.size()) + " vs " + std::to_string(v1.size()) + ")");
	std::vector<uint8_t> v2;
	ok(state::serialize(*w, v2), "serialize version 2");
	eq(uint32_t(v2[4]), 2u, "current version 2");
	ok(v2.size() > v1.size(), "v2 adds the wild field and the blocks");
	auto w2 = state::deserialize(v2, &why);
	if (!ok(w2 != nullptr, "the v2 re-save loads: " + why)) return;
	std::vector<uint8_t> v1b;
	state::serialize(*w2, v1b, nullptr, 1);
	ok(sameBytes(v1b, v1), "v1 -> v2 -> v1 is lossless for a pre-mythos world");
	std::vector<uint8_t> bad;
	ok(!state::serialize(*w, bad, &why, 3) && bad.empty(), "an unknown save version is refused: " + why);
}

DM_TEST("state v2: a populated MythosState and the BASE fields round-trip and re-save byte-identically") {
	auto w = populated();
	ok(!state::extensionsDefault(*w), "not default");
	std::vector<uint8_t> b;
	std::string why;
	if (!ok(state::serialize(*w, b, &why), "serializes: " + why)) return;
	auto back = state::deserialize(b, &why);
	if (!ok(back != nullptr, "loads: " + why)) return;
	std::vector<uint8_t> b2;
	state::serialize(*back, b2);
	ok(sameBytes(b, b2), "re-save is byte-identical");
	const MythosState& m = back->mythos;
	eq(int(m.presence), int(Presence::Messengers), "presence");
	eq(m.faith.size(), size_t(2), "faith"); eq(m.faith[0].agency, 0.61, "agency"); eq(m.faith[0].data.at("answered"), 3.0, "faith data");
	eq(int(m.faith[0].death), int(DeathBelief::Turning), "death"); eq(int(m.faith[0].charge), int(Charge::Garden), "charge");
	ok(m.factors[0].research == 1.04 && !m.factors[0].farmAncient && m.factors[0].expect[4] == 1.2, "factors");
	ok(m.towns.size() == 1 && m.towns[0].healer == 2 && m.towns[0].blessing == BlessingKind::Healer, "towns");
	ok(m.acts.size() == 1 && m.acts[0].kind == "rain", "acts");
	ok(m.decrees.size() == 2 && m.decrees[1].custom && m.decrees[0].by == 1, "decrees");
	ok(m.words.size() == 1 && m.words[0].outcome == uint8_t(WordOutcome::Wisdom) && m.words[0].area == int8_t(DecreeArea::Care), "words");
	ok(m.pacts.size() == 1 && m.pacts[0].until == 1000, "pacts");
	ok(m.revelations.size() == 1 && m.revelations[0].early, "revelations");
	ok(m.figures.size() == 2 && m.figures[0].byname == "of the Goats" && m.figures[0].heart[0] == 0.9f && m.figures[1].trial == 1, "figures");
	ok(m.cards.size() == 1 && m.cards[0].ref[3] == 7 && m.cards[0].deadline == 860, "cards");
	ok(m.legends.size() == 1 && m.legends[0].told == 2.5, "legends");
	ok(m.houses.size() == 1 && m.rivals.size() == 1 && m.omens.size() == 1 && m.keepers.size() == 1 && m.keepers[0].name == "Asheru", "faiths, omens, keepers");
	ok(m.withdrawAnnounced && m.renewal == 1 && m.nextId == 10 && m.num.at("cards_lastOpen") == 800, "scalars");
	eq(back->cultures[0].traits.compassion, 0.8, "compassion"); eq(back->cultures[1].traits.courage, 0.25, "courage");
	eq(back->cultures[0].prophet->figure, 1, "prophet.figure");
	eq(int(back->settings.archetype), int(Archetype::Strange), "archetype");
	eq(int(back->ending->legacy), int(Legacy::Unneeded), "legacy");
	eq(int(back->wild[5]), 31, "wild"); eq(int(back->wild[6]), 255, "wild grove");
	// the legacy view drops the extensions
	std::vector<uint8_t> v1;
	ok(state::serialize(*w, v1, &why, 1), "version 1 view");
	auto lossy = state::deserialize(v1, &why);
	ok(lossy && lossy->mythos.figures.empty() && lossy->wild[5] == 0 && lossy->cultures[0].traits.compassion == 0.5, "version 1 is lossy by design");
	// every truncated prefix is refused
	int accepted = 0;
	for (size_t len = 0; len < b.size(); len++) if (state::deserialize(b.data(), len)) accepted++;
	eq(accepted, 0, "no truncated prefix of a v2 save loads");
	std::printf("       populated L=2 v2 save: %zu bytes (v1 view %zu)\n", b.size(), v1.size());
}

DM_TEST("state v2: deserialize refuses a MYTH block naming a missing culture, figure or card") {
	std::string why;
	auto refuses = [&](const char* what, auto mutate) {
		auto w = populated();
		mutate(*w);
		std::vector<uint8_t> b;
		if (!state::serialize(*w, b, &why)) { ok(true, std::string(what) + ": refused at serialize (" + why + ")"); return; }
		auto back = state::deserialize(b, &why);
		ok(back == nullptr, std::string(what) + " is refused: " + why);
	};
	refuses("a decree of a missing culture", [](World& w) { w.mythos.decrees[0].culture = 7; });
	refuses("a decree by a missing figure", [](World& w) { w.mythos.decrees[0].by = 44; });
	refuses("a card naming a missing figure", [](World& w) { w.mythos.cards[0].figure = 44; });
	refuses("a card naming a missing culture", [](World& w) { w.mythos.cards[0].culture = 9; });
	refuses("a card naming a missing settlement", [](World& w) { w.mythos.cards[0].settlement = 9; });
	refuses("two cards with one id", [](World& w) { w.mythos.cards.push_back(w.mythos.cards[0]); });
	refuses("a card id at or past nextId", [](World& w) { w.mythos.cards[0].id = 10; });
	refuses("a legend of a missing culture", [](World& w) { w.mythos.legends[0].culture = 4; });
	refuses("a rival voice's missing figure", [](World& w) { w.mythos.rivals[0].figure = 99; });
	refuses("a figure of a missing culture", [](World& w) { w.mythos.figures[0].culture = 5; });
	refuses("a healer that is no figure", [](World& w) { w.mythos.towns[0].healer = 50; });
	refuses("more Faith records than cultures", [](World& w) { w.mythos.faith.resize(3); });
	refuses("a prophet naming a missing figure", [](World& w) { w.cultures[0].prophet->figure = 77; });
	refuses("the reserved presence Walk", [](World& w) { w.mythos.presence = Presence::Walk; });
	refuses("a stance outside its area", [](World& w) { w.mythos.decrees[0].stance = 3; });
	refuses("a word naming a missing figure", [](World& w) { w.mythos.words[0].figure = 12; });
	refuses("a pact of a missing culture", [](World& w) { w.mythos.pacts[0].culture = 2; });
	// and the valid populated world still loads
	auto w = populated();
	std::vector<uint8_t> b;
	state::serialize(*w, b);
	ok(state::deserialize(b, &why) != nullptr, "the unmodified world loads: " + why);
}

DM_TEST("state v2: unknown extension blocks are skipped, known ones refused when malformed") {
	auto w = populated();
	std::vector<uint8_t> b;
	state::serialize(*w, b);
	// the top-level block list ends with "END_" "END!": insert an unknown block before the terminator
	const size_t at = b.size() - 8;
	ok(std::memcmp(b.data() + at, "END_END!", 8) == 0, "the blob ends with END_ END!");
	std::vector<uint8_t> x(b.begin(), b.begin() + std::ptrdiff_t(at));
	const uint8_t blk[] = { 'S', 'T', 'O', 'R', 1, 0, 3, 0, 0, 0, 7, 8, 9 };   // story-v4's future block, version 1, 3 bytes
	x.insert(x.end(), blk, blk + sizeof(blk));
	x.insert(x.end(), b.begin() + std::ptrdiff_t(at), b.end());
	std::string why;
	auto back = state::deserialize(x, &why);
	ok(back != nullptr, "an unknown tag is skipped: " + why);
	if (back) { std::vector<uint8_t> again; state::serialize(*back, again); ok(sameBytes(again, b), "and dropped on re-save"); }
	std::vector<uint8_t> twice(b.begin(), b.begin() + std::ptrdiff_t(at));
	const uint8_t dup[] = { 'B', 'A', 'S', 'E', 9, 0, 0, 0, 0, 0 };   // a second BASE (a newer version is skipped, a repeat is not)
	twice.insert(twice.end(), dup, dup + sizeof(dup));
	twice.insert(twice.end(), b.begin() + std::ptrdiff_t(at), b.end());
	ok(state::deserialize(twice, &why) == nullptr, "a block that appears twice is refused: " + why);
	std::vector<uint8_t> trail = b;
	trail.insert(trail.end() - 4, uint8_t(0));
	ok(state::deserialize(trail, &why) == nullptr, "garbage before the end marker is refused: " + why);
}

DM_TEST("state v2: fieldSince and extensionsDefault") {
	eq(state::fieldSince(state::fieldIndex("elev")), 1u, "elev is version 1");
	eq(state::fieldSince(state::fieldIndex("wild")), 2u, "wild is version 2");
	eq(state::fieldSince(-1), 0u, "bad index");
	auto w = state::create({ 1, "x", 2 });
	ok(state::extensionsDefault(*w), "a fresh world");
	w->wild[0] = 1; ok(!state::extensionsDefault(*w), "wild"); w->wild[0] = 0;
	w->mythos.num["mythos_x"] = 1; ok(!state::extensionsDefault(*w), "mythos.num"); w->mythos.num.clear();
	w->settings.archetype = Archetype::Generous; ok(!state::extensionsDefault(*w), "archetype"); w->settings.archetype = Archetype::Balanced;
	state::newCulture(*w).traits.ambition = 0.8; ok(!state::extensionsDefault(*w), "ambition");
}

DM_TEST("schema v4: every new enum and stance round-trips through toStr / fromStr") {
	int checked = 0;
	auto all = [&](auto tag, int n, const char* what) {
		using E = decltype(tag);
		for (int i = 0; i < n; i++) {
			E v{};
			const char* s = toStr(E(i));
			ok(s && std::string(s) != "?" && fromStr(s, v) && int(v) == i, std::string(what) + " " + std::to_string(i) + " '" + (s ? s : "") + "'");
			checked++;
		}
		E v{};
		ok(!fromStr("no-such-name", v), std::string(what) + " rejects unknown names");
		ok(std::string(toStr(E(n))) == "?", std::string(what) + " out of range prints ?");
	};
	all(Presence{}, enumCount<Presence>(), "Presence"); all(Aspect{}, enumCount<Aspect>(), "Aspect");
	all(Archetype{}, enumCount<Archetype>(), "Archetype"); all(Legacy{}, enumCount<Legacy>(), "Legacy");
	all(DecreeArea{}, enumCount<DecreeArea>(), "DecreeArea"); all(Clarity{}, enumCount<Clarity>(), "Clarity");
	all(Channel{}, enumCount<Channel>(), "Channel"); all(CardKind{}, enumCount<CardKind>(), "CardKind");
	all(WordContent{}, enumCount<WordContent>(), "WordContent"); all(WordOutcome{}, enumCount<WordOutcome>(), "WordOutcome");
	all(GodTerm{}, enumCount<GodTerm>(), "GodTerm"); all(PeopleTerm{}, enumCount<PeopleTerm>(), "PeopleTerm");
	all(PactState{}, enumCount<PactState>(), "PactState"); all(FigureKind{}, enumCount<FigureKind>(), "FigureKind");
	all(MessengerArchetype{}, enumCount<MessengerArchetype>(), "MessengerArchetype"); all(FigureFate{}, enumCount<FigureFate>(), "FigureFate");
	all(TrialKind{}, enumCount<TrialKind>(), "TrialKind"); all(CardState{}, enumCount<CardState>(), "CardState");
	all(LegendMoral{}, enumCount<LegendMoral>(), "LegendMoral"); all(LegendCredit{}, enumCount<LegendCredit>(), "LegendCredit");
	all(RivalState{}, enumCount<RivalState>(), "RivalState"); all(OmenKind{}, enumCount<OmenKind>(), "OmenKind");
	all(WarningWay{}, enumCount<WarningWay>(), "WarningWay"); all(OmenResponse{}, enumCount<OmenResponse>(), "OmenResponse");
	all(KeeperDomain{}, enumCount<KeeperDomain>(), "KeeperDomain"); all(Temper{}, enumCount<Temper>(), "Temper");
	all(RevelationKind{}, enumCount<RevelationKind>(), "RevelationKind"); all(RevelationOutcome{}, enumCount<RevelationOutcome>(), "RevelationOutcome");
	all(DeathBelief{}, enumCount<DeathBelief>(), "DeathBelief"); all(Certainty{}, enumCount<Certainty>(), "Certainty");
	all(Charge{}, enumCount<Charge>(), "Charge"); all(BlessingKind{}, enumCount<BlessingKind>(), "BlessingKind");
	all(Consequence{}, enumCount<Consequence>(), "Consequence"); all(JudgeBy{}, enumCount<JudgeBy>(), "JudgeBy");
	all(Sentence{}, enumCount<Sentence>(), "Sentence"); all(Circumstance{}, enumCount<Circumstance>(), "Circumstance");
	all(HeartKind{}, enumCount<HeartKind>(), "HeartKind"); all(DisputeKind{}, enumCount<DisputeKind>(), "DisputeKind");
	all(DisputeAnswer{}, enumCount<DisputeAnswer>(), "DisputeAnswer"); all(RenewalKind{}, enumCount<RenewalKind>(), "RenewalKind");
	all(TraitLevel{}, enumCount<TraitLevel>(), "TraitLevel"); all(CardSource{}, enumCount<CardSource>(), "CardSource");
	all(OptionWhy{}, enumCount<OptionWhy>(), "OptionWhy");
	// the extended v3 enums
	all(Cause{}, enumCount<Cause>(), "Cause"); all(PrayerKind{}, enumCount<PrayerKind>(), "PrayerKind");
	all(DisasterType{}, enumCount<DisasterType>(), "DisasterType");
	eq(std::string(toStr(Cause::Deluge)), std::string("deluge"), "deluge"); eq(std::string(toStr(PrayerKind::Sun)), std::string("sun"), "sun");
	// stances
	for (int a = 0; a < enumCount<DecreeArea>(); a++) {
		const DecreeArea area = DecreeArea(a);
		for (int s = -1; s < stanceCount(area); s++) {
			int back = -9;
			const char* nm = stanceName(area, s);
			ok(stanceFromName(area, nm, back) && back == s, std::string("stance ") + toStr(area) + " " + nm);
			checked++;
		}
		int dummy = 0;
		ok(!stanceFromName(area, "nope", dummy), "unknown stance");
	}
	eq(stanceCount(DecreeArea::Worship), 5, "five rites"); eq(stanceCount(DecreeArea::Count), 0, "Count has none");
	eq(std::string(stanceName(DecreeArea::Hospitality, int(HospitalityStance::Guarded))), std::string("guarded"), "guarded");
	eq(std::string(stanceName(DecreeArea::Worship, int(Rite::World))), std::string("world"), "the World rite");
	std::printf("       %d enum values and stances round-tripped\n", checked);
}

DM_TEST("schema v4: neutral defaults of the shared records") {
	const Factors f{};
	ok(f.growthR == 1 && f.K == 1 && f.research == 1 && f.warV == 0 && f.devotionBase == 0 && f.farmAncient && f.claimAncient &&
		f.co2Uptake == 1 && f.expect[5] == 1, "Factors{} is neutral");
	const TownFactors t{};
	ok(t.K == 1 && t.growthR == 1 && t.harmTaken == 1 && t.plagueDeath == 1, "TownFactors{} is neutral");
	const Faith fa{};
	ok(fa.agency == 0.3 && fa.obedience == 0.5 && fa.dependence == 0.3 && fa.clarity == 0.5 && fa.bearer == -1 && fa.grown == -1, "Faith starting values (3.4)");
	const MythosState m{};
	ok(m.presence == Presence::Wonders && m.archetype == Archetype::Balanced && m.nextId == 1 && m.withdrawnUntil == -1, "MythosState defaults");
	const Card c{};
	ok(c.choice == -1 && c.lapse == -1 && c.ref[0] == -1 && c.deadline == -1, "Card defaults");
	eq(int(sizeof(Factors{}.expect) / sizeof(double)), enumCount<PrayerKind>(), "expect[] has one slot per prayer kind");
}

DM_TEST("test helper: checkFinite / noNaN cover the MythosState") {
	auto w = populated();
	eq(checkFinite(*w), std::string(), "the populated world is finite");
	w->mythos.faith[1].agency = std::numeric_limits<double>::quiet_NaN();
	eq(checkFinite(*w), std::string("mythos.faith 1"), "a NaN agency is found");
	w->mythos.faith[1].agency = 0.3;
	w->mythos.figures[0].heart[1] = std::numeric_limits<float>::infinity();
	eq(checkFinite(*w), std::string("mythos.figures 1"), "an infinite heart is found");
}
