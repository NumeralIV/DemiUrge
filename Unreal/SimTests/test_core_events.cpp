// DEMIURGE — SimTests/test_core_events.cpp : Core/Events.h (port of tests/events.test.js + the
// DM.EVENTS contract checks).
#include <set>
#include <utility>

#include "Core/Events.h"
#include "TestFramework.h"
#include "TestWorld.h"

using namespace dm;
using namespace dmt;

namespace {

template <size_t I = 0>
void forEachPayload(const std::function<void(size_t, const Event&)>& f) {
	if constexpr (I < size_t(kEventCount)) {
		Event e;
		e.type = Ev(I);
		e.data = EventPayload(std::in_place_index<I>);
		f(I, e);
		forEachPayload<I + 1>(f);
	}
}

} // namespace

DM_TEST("events: the table is DM.EVENTS and every payload struct matches it") {
	eq(kEventCount, 79, "event count (DM.EVENTS has 55 entries; v3, design review: + drown:warning; v3.1: + alliance; v4 mythos M0: + 22)");
	std::set<std::string> names;
	for (int i = 0; i < kEventCount; i++) names.insert(kEventTable[i].name);
	eq(names.size(), size_t(kEventCount), "names are unique");
	eq(std::string(kEventTable[int(Ev::SettlementDestroyed)].name), std::string("settlement:destroyed"));
	eq(std::string(eventName(Ev::CameraShake)), std::string("camera:shake"));
	ok(eventFromName("battle") == Ev::Battle && eventFromName("nope") == Ev::Count, "eventFromName");
	forEachPayload([&](size_t i, const Event& e) {
		std::vector<std::string> keys;
		e.fields([&](const char* k, const auto&) { keys.push_back(k); });
		std::vector<std::string> want;
		for (int k = 0; k < kEventTable[i].nKeys; k++) want.push_back(kEventTable[i].keys[k]);
		eq(keys, want, std::string("payload keys of ") + kEventTable[i].name);
		eq(checkEvent(e, 10), std::string(), std::string("default payload of ") + kEventTable[i].name + " passes the checker");
	});
	// each struct's kType is its own index in the variant
	ok(int(EvPhase::kType) == 0 && int(EvRenewal::kType) == kEventCount - 1, "kType order");
	// (v4 mythos, M0) the 22 rows of SPEC-MYTHOS 1.8 follow the UI events, in the spec's order; ending gains legacy
	const char* mythosRows[] = { "card", "presence", "aspect", "decree", "breach", "word", "pact", "figure", "trial", "judgement",
		"legend", "blessing", "omen", "dispute", "house", "rival", "bearers", "revelation", "keeper", "grown", "withdraw", "renewal" };
	for (int i = 0; i < 22; i++) eq(std::string(kEventTable[int(Ev::CameraShake) + 1 + i].name), std::string(mythosRows[i]), "mythos event row " + std::to_string(i));
	eq(std::string(kEventTable[int(Ev::Ending)].keys[5]), std::string("legacy"), "ending.legacy");
	eq(kEventTable[int(Ev::Word)].nKeys, 6, "word has 6 keys");
	ok(EventPayload(EvBattle{}).index() == size_t(Ev::Battle), "variant index == Ev");
}

DM_TEST("events: the checker catches bad payloads") {
	Event e;
	e.type = Ev::Quake;
	e.data = EvQuake{ 3, std::nan(""), 1, false };
	ok(!checkEvent(e, 10).empty(), "NaN number");
	e.data = EvQuake{ 99, 0.1, 1, false };
	ok(!checkEvent(e, 10).empty(), "cell out of range");
	e.data = EvFireStart{ 3 };
	ok(!checkEvent(e, 10).empty(), "payload of another event");
	e.data = EvQuake{ -1, 0.1, 1, true };
	eq(checkEvent(e, 10), std::string(), "cell -1 is allowed");
}

DM_TEST("bus: handlers receive the event synchronously, in subscription order; types independent") {
	EventBus bus;
	std::vector<std::string> log;
	bus.on(Ev::Quake, [&](const Event& e) { log.push_back(std::string("a:") + e.name()); });
	bus.on<EvQuake>([&](const EvQuake& q) { log.push_back("b:" + std::to_string(q.cell)); });
	bus.on(Ev::Quake, [&](const Event& e) { log.push_back("c:" + std::to_string(e.as<EvQuake>()->cell)); });
	bus.on(Ev::Lightning, [&](const Event&) { log.push_back("L"); });
	bus.emit(EvQuake{ 42, 0.1, 0.5, false });
	eq(log, std::vector<std::string>{ "a:quake", "b:42", "c:42" });
	log.clear();
	bus.emit(EvFireStart{ 1 });
	ok(log.empty(), "no handler for fire:start");
	bus.emit(EvLightning{ 3, true });
	eq(log, std::vector<std::string>{ "L" });
	eq(bus.count(), 4);
}

DM_TEST("bus: onAll interleaves with typed handlers by subscription time") {
	EventBus bus;
	std::vector<int> log;
	bus.on(Ev::Storm, [&](const Event&) { log.push_back(1); });
	bus.onAll([&](const Event&) { log.push_back(2); });
	bus.on(Ev::Storm, [&](const Event&) { log.push_back(3); });
	bus.onAll([&](const Event&) { log.push_back(4); });
	bus.emit(EvStorm{ 0, 0.1, true });
	eq(log, std::vector<int>{ 1, 2, 3, 4 });
	log.clear();
	bus.emit(EvDrought{ 0, 0.1, true });
	eq(log, std::vector<int>{ 2, 4 }, "onAll sees every event");
}

DM_TEST("bus: off by token, repeatedly, unknown tokens; clear") {
	EventBus bus;
	int a = 0, b = 0;
	const auto ta = bus.on(Ev::Phase, [&](const Event&) { a++; });
	bus.on(Ev::Phase, [&](const Event&) { b++; });
	bus.emit(EvPhase{});
	bus.off(ta);
	bus.off(ta);
	bus.off(0);
	bus.off(123456);
	bus.emit(EvPhase{});
	eq(a, 1); eq(b, 2);
	eq(bus.count(), 1);
	bus.clear();
	bus.emit(EvPhase{});
	eq(b, 2, "cleared");
	eq(bus.count(), 0);
	ok(bus.on(Ev::Phase, nullptr) == 0, "a null handler is refused");
	ok(bus.on(Ev::Count, [](const Event&) {}) == 0, "an invalid type is refused");
}

DM_TEST("bus: removal and addition during an emit; nested emits; clear inside a handler") {
	EventBus bus;
	std::vector<std::string> log;
	EventBus::Token t2 = 0;
	bus.on(Ev::Quake, [&](const Event&) { log.push_back("1"); bus.off(t2); });
	t2 = bus.on(Ev::Quake, [&](const Event&) { log.push_back("2"); });
	bus.on(Ev::Quake, [&](const Event&) {
		log.push_back("3");
		bus.on(Ev::Quake, [&](const Event&) { log.push_back("new"); });
	});
	bus.emit(EvQuake{});
	eq(log, std::vector<std::string>{ "1", "3" }, "removed during emit: not called; added: not called yet");
	log.clear();
	bus.emit(EvQuake{});
	eq(log, std::vector<std::string>{ "1", "3", "new" });

	EventBus nb;
	std::vector<std::string> nlog;
	nb.on(Ev::Quake, [&](const Event&) { nlog.push_back("q1"); nb.emit(EvFireStart{ 1 }); nlog.push_back("q1-end"); });
	nb.on(Ev::FireStart, [&](const Event&) { nlog.push_back("fire"); });
	nb.on(Ev::Quake, [&](const Event&) { nlog.push_back("q2"); });
	nb.emit(EvQuake{});
	eq(nlog, std::vector<std::string>{ "q1", "fire", "q1-end", "q2" }, "nested emit runs to completion first");

	EventBus cb;
	int after = 0;
	cb.on(Ev::Quake, [&](const Event&) { cb.clear(); });
	cb.on(Ev::Quake, [&](const Event&) { after++; });
	cb.emit(EvQuake{});
	eq(after, 0, "clear() inside a handler stops the rest");
	eq(cb.count(), 0);

	EventBus sb;
	int self = 0;
	EventBus::Token ts = 0;
	ts = sb.on(Ev::Quake, [&](const Event&) { self++; sb.off(ts); });
	sb.emit(EvQuake{}); sb.emit(EvQuake{});
	eq(self, 1, "a handler may remove itself");

	EventBus many;
	int hits = 0;
	many.on(Ev::Quake, [&](const Event&) {
		for (int i = 0; i < 100; i++) many.on(Ev::Quake, [&](const Event&) { hits++; });   // forces reallocation mid-emit
	});
	many.emit(EvQuake{});
	eq(hits, 0, "handlers added mid-emit (with reallocation) are not called in that emit");
	many.emit(EvQuake{});
	eq(hits, 100);
}

DM_TEST("bus: each world owns its bus; makeWorld watches it") {
	auto a = makeWorld({ .L = 2 });
	auto b = makeWorld({ .L = 2, .seed = 2 });
	int na = 0, nb = 0;
	a->bus.onAll([&](const Event&) { na++; });
	b->bus.onAll([&](const Event&) { nb++; });
	a->bus.emit(EvSettlementFounded{ 0, 5, 0, FoundedBy::Awaken });
	eq(na, 1); eq(nb, 0, "no global bus");
	const auto got = capture(*a, [&] { a->bus.emit(EvWarDeclared{ 0, 1 }); a->bus.emit(EvPeopleLost{}); });
	eq(got.size(), size_t(2));
	ok(got[0].as<EvWarDeclared>() && got[0].as<EvWarDeclared>()->b == 1 && got[1].type == Ev::PeopleLost, "capture");
}
