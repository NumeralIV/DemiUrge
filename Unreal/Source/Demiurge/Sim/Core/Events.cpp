// DEMIURGE — Sim/Core/Events.cpp : see Events.h.
#include "Core/Events.h"

namespace dm {

// DM.EVENTS (js/sim/schema.js), written out as the contract. The tests check that every payload
// struct's fields() list matches these keys exactly, in order.
const EventInfo kEventTable[kEventCount] = {
	{ "phase", 2, { "phase", "prev" } },
	{ "power:used", 4, { "id", "cell", "ang", "brush" } },
	{ "miracle:witnessed", 4, { "cell", "kind", "good", "ids" } },
	{ "lightning", 2, { "cell", "natural" } },
	{ "meteor:launched", 4, { "cell", "eta", "kind", "id" } },
	{ "meteor:impact", 4, { "cell", "ang", "kind", "sea" } },
	{ "tsunami:start", 1, { "cell" } },
	{ "tsunami:hit", 2, { "cell", "height" } },
	{ "volcano:start", 3, { "cell", "id", "natural" } },
	{ "volcano:end", 2, { "cell", "id" } },
	{ "quake", 4, { "cell", "ang", "power", "natural" } },
	{ "fire:start", 1, { "cell" } },
	{ "storm", 3, { "cell", "ang", "start" } },
	{ "drought", 3, { "cell", "ang", "start" } },
	{ "settlement:founded", 4, { "id", "cell", "culture", "by" } },
	{ "settlement:tier", 3, { "id", "tier", "prev" } },
	{ "settlement:building", 2, { "id", "building" } },
	{ "settlement:famine", 1, { "id" } },
	{ "settlement:plague", 2, { "id", "start" } },
	{ "settlement:destroyed", 4, { "id", "cell", "culture", "cause" } },
	{ "settlement:captured", 4, { "id", "from", "to", "how" } },
	{ "prayer", 3, { "id", "kind", "start" } },
	{ "prayer:answered", 2, { "id", "kind" } },
	{ "culture:born", 4, { "id", "parent", "cell", "reason" } },
	{ "culture:died", 1, { "id" } },
	{ "culture:age", 3, { "id", "age", "prev" } },
	{ "world:age", 1, { "age" } },
	{ "war:declared", 2, { "a", "b" } },
	{ "war:ended", 3, { "a", "b", "victor" } },
	{ "battle", 5, { "cell", "attacker", "defender", "won", "settlement" } },
	{ "trade:route", 2, { "a", "b" } },
	{ "prophet:risen", 3, { "culture", "settlement", "name" } },
	{ "prophet:died", 2, { "culture", "name" } },
	{ "edict", 2, { "culture", "edict" } },
	{ "wonder:built", 2, { "settlement", "name" } },
	{ "nuke", 3, { "cell", "from", "to" } },
	{ "ark:started", 2, { "culture", "settlement" } },
	{ "ark:launched", 2, { "culture", "settlement" } },
	{ "climate:milestone", 2, { "kind", "value" } },
	{ "unit:spawned", 4, { "id", "type", "culture", "cell" } },
	{ "unit:arrived", 4, { "id", "type", "culture", "cell" } },
	{ "unit:died", 5, { "id", "type", "culture", "cell", "cause" } },
	{ "chronicle:entry", 1, { "entry" } },
	{ "awe:short", 1, { "id" } },
	{ "people:lost", 0, {} },
	{ "ending", 5, { "type", "mood", "culture", "settlement", "year" } },
	{ "ui:state", 2, { "state", "prev" } },
	{ "ui:speed", 2, { "speed", "paused" } },
	{ "ui:power", 1, { "id" } },
	{ "ui:select", 1, { "id" } },
	{ "ui:overlay", 1, { "mode" } },
	{ "ui:hud", 1, { "hidden" } },
	{ "ui:settings", 0, {} },
	{ "ui:toast", 2, { "text", "kind" } },
	{ "camera:shake", 1, { "amount" } },
};

const char* eventName(Ev type) {
	const int i = int(type);
	return (i >= 0 && i < kEventCount) ? kEventTable[i].name : "?";
}

Ev eventFromName(std::string_view name) {
	for (int i = 0; i < kEventCount; i++) if (name == kEventTable[i].name) return Ev(i);
	return Ev::Count;
}

EventBus::Token EventBus::on(Ev type, Handler fn) {
	const int slot = int(type);
	if (slot < 0 || slot >= kEventCount || !fn) return 0;
	auto e = std::make_shared<Entry>();
	e->seq = ++seq_;
	e->fn = std::move(fn);
	listFor(slot).push_back(std::move(e));
	return seq_;
}

EventBus::Token EventBus::onAll(Handler fn) {
	if (!fn) return 0;
	auto e = std::make_shared<Entry>();
	e->seq = ++seq_;
	e->fn = std::move(fn);
	listFor(kEventCount).push_back(std::move(e));
	return seq_;
}

void EventBus::off(Token tok) {
	if (tok == 0) return;
	for (auto& list : lists_) {
		for (auto& e : list) {
			if (e->seq == tok && e->live) { e->live = false; dirty_ = true; }
		}
	}
	if (depth_ == 0) compact();
}

void EventBus::clear() {
	for (auto& list : lists_) for (auto& e : list) e->live = false;
	dirty_ = true;
	if (depth_ == 0) compact();
}

void EventBus::compact() {
	if (!dirty_) return;
	for (auto& list : lists_) {
		size_t w = 0;
		for (size_t r = 0; r < list.size(); r++) if (list[r]->live) list[w++] = list[r];
		list.resize(w);
	}
	dirty_ = false;
}

int EventBus::count() const {
	int c = 0;
	for (const auto& list : lists_) for (const auto& e : list) if (e->live) c++;
	return c;
}

void EventBus::emit(Event ev) {
	const int slot = int(ev.type);
	if (slot < 0 || slot >= kEventCount) return;
	// Lists are only appended to (removal is deferred while depth_ > 0), so the first na / nb
	// entries are exactly the subscriptions that existed when this emit began.
	const List& a = lists_[size_t(slot)];
	const List& b = lists_[size_t(kEventCount)];
	const size_t na = a.size(), nb = b.size();
	depth_++;
	size_t i = 0, j = 0;
	while (i < na || j < nb) {
		std::shared_ptr<Entry> e = (j >= nb || (i < na && a[i]->seq < b[j]->seq)) ? a[i++] : b[j++];
		if (!e->live) continue;
		e->fn(ev);
	}
	depth_--;
	if (depth_ == 0) compact();
}

} // namespace dm
