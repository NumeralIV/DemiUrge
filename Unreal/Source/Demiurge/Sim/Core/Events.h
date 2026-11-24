// DEMIURGE — Sim/Core/Events.h : typed events and the event bus (port of js/core/events.js and of
// DM.EVENTS in js/sim/schema.js; SPEC-SIM section 2).
//
// Each world owns one bus: `w.bus`. There is no global bus and no "active world".
//
//   Emit:       w.bus.emit(EvSettlementFounded{ s.id, s.cell, s.culture, FoundedBy::Settlers });
//   Subscribe:  auto tok = w.bus.on<EvBattle>([](const EvBattle& e) { ... });        one type
//               auto tok = w.bus.on(Ev::Battle, [](const Event& e) { ... });         one type, untyped
//               auto tok = w.bus.onAll([](const Event& e) { ... });                 every event (JS '*')
//   Inspect:    e.type (Ev), e.name() ("settlement:founded"), e.as<EvBattle>() -> const EvBattle* or nullptr
//   Remove:     w.bus.off(tok);  w.bus.clear();
//
// Semantics (same as DM.Bus): handlers run synchronously, in true subscription order (handlers of
// the type and onAll handlers interleaved by the moment they subscribed). Subscribing or
// unsubscribing during an emit is safe: a handler removed during an emit is not called again in
// that emit; one added during an emit is first called on the next emit. Handlers may emit (nested
// emits run to completion first). Handlers must not throw (the simulation is built without
// exceptions).
//
// Payloads: one struct per event, named Ev<Name>, with exactly the fields of SPEC-SIM section 2
// (and the UI events of DM.EVENTS) in the listed order. Every struct has `static constexpr Ev kType`
// and `fields(f)`, which calls f("key", value) for each field: tests use it to check payloads
// against kEventTable (names, keys, finite numbers, cells in range). Cell fields are cell ids or -1;
// culture and settlement fields are ids (-1 = none).
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "Schema.h"

namespace dm {

// X(StructSuffix, "event:name")  — the order is the order of DM.EVENTS; the 22 mythos rows of SPEC-MYTHOS 1.8
// (v4, M0) are appended after the UI events so no existing Ev value moves.
#define DM_EVENT_LIST(X)                                  \
	X(Phase, "phase")                                     \
	X(PowerUsed, "power:used")                            \
	X(MiracleWitnessed, "miracle:witnessed")              \
	X(Lightning, "lightning")                             \
	X(MeteorLaunched, "meteor:launched")                  \
	X(MeteorImpact, "meteor:impact")                      \
	X(TsunamiStart, "tsunami:start")                      \
	X(TsunamiHit, "tsunami:hit")                          \
	X(VolcanoStart, "volcano:start")                      \
	X(VolcanoEnd, "volcano:end")                          \
	X(Quake, "quake")                                     \
	X(FireStart, "fire:start")                            \
	X(Storm, "storm")                                     \
	X(Drought, "drought")                                 \
	X(SettlementFounded, "settlement:founded")            \
	X(SettlementTier, "settlement:tier")                  \
	X(SettlementBuilding, "settlement:building")          \
	X(SettlementFamine, "settlement:famine")              \
	X(SettlementPlague, "settlement:plague")              \
	X(SettlementDestroyed, "settlement:destroyed")        \
	X(SettlementCaptured, "settlement:captured")          \
	X(Prayer, "prayer")                                   \
	X(PrayerAnswered, "prayer:answered")                  \
	X(CultureBorn, "culture:born")                        \
	X(CultureDied, "culture:died")                        \
	X(CultureAge, "culture:age")                          \
	X(WorldAge, "world:age")                              \
	X(WarDeclared, "war:declared")                        \
	X(WarEnded, "war:ended")                              \
	X(Battle, "battle")                                   \
	X(TradeRoute, "trade:route")                          \
	X(ProphetRisen, "prophet:risen")                      \
	X(ProphetDied, "prophet:died")                        \
	X(Edict, "edict")                                     \
	X(WonderBuilt, "wonder:built")                        \
	X(Nuke, "nuke")                                       \
	X(ArkStarted, "ark:started")                          \
	X(ArkLaunched, "ark:launched")                        \
	X(ClimateMilestone, "climate:milestone")              \
	X(UnitSpawned, "unit:spawned")                        \
	X(UnitArrived, "unit:arrived")                        \
	X(UnitDied, "unit:died")                              \
	X(ChronicleEntry, "chronicle:entry")                  \
	X(AweShort, "awe:short")                              \
	X(PeopleLost, "people:lost")                          \
	X(Ending, "ending")                                   \
	X(DrownWarning, "drown:warning")                      \
	X(Alliance, "alliance")                               \
	X(UiState, "ui:state")                                \
	X(UiSpeed, "ui:speed")                                \
	X(UiPower, "ui:power")                                \
	X(UiSelect, "ui:select")                              \
	X(UiOverlay, "ui:overlay")                            \
	X(UiHud, "ui:hud")                                    \
	X(UiSettings, "ui:settings")                          \
	X(UiToast, "ui:toast")                                \
	X(CameraShake, "camera:shake")                        \
	X(Card, "card")                       \
	X(Presence, "presence")               \
	X(Aspect, "aspect")                   \
	X(Decree, "decree")                   \
	X(Breach, "breach")                   \
	X(Word, "word")                       \
	X(Pact, "pact")                       \
	X(Figure, "figure")                   \
	X(Trial, "trial")                     \
	X(Judgement, "judgement")             \
	X(Legend, "legend")                   \
	X(Blessing, "blessing")               \
	X(Omen, "omen")                       \
	X(Dispute, "dispute")                 \
	X(House, "house")                     \
	X(Rival, "rival")                     \
	X(Bearers, "bearers")                 \
	X(Revelation, "revelation")           \
	X(Keeper, "keeper")                   \
	X(Grown, "grown")                     \
	X(Withdraw, "withdraw")               \
	X(Renewal, "renewal")

enum class Ev : uint16_t {
#define DM_EV_ENUM(T, s) T,
	DM_EVENT_LIST(DM_EV_ENUM)
#undef DM_EV_ENUM
	Count
};
inline constexpr int kEventCount = int(Ev::Count);

// The contract table (DM.EVENTS): name and payload keys of every event, indexed by Ev.
struct EventInfo {
	const char* name;
	int nKeys;
	const char* keys[6];
};
extern const EventInfo kEventTable[kEventCount];
const char* eventName(Ev type);
// Ev for a name, or Ev::Count if unknown.
Ev eventFromName(std::string_view name);

// ------------------------------------------------------------------ payloads

#define DM_F(k) f(#k, k)
struct EvPhase { static constexpr Ev kType = Ev::Phase; Phase phase = Phase::Molten; Phase prev = Phase::Molten;
	template <class F> void fields(F&& f) const { DM_F(phase); DM_F(prev); } };
// id = power id ('raise', 'meteor', ...); brush = it is a brush power
struct EvPowerUsed { static constexpr Ev kType = Ev::PowerUsed; std::string id; int cell = -1; double ang = 0; bool brush = false;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(cell); DM_F(ang); DM_F(brush); } };
// kind = power id or 'nuke'; ids = up to 16 settlement ids that gained anything
struct EvMiracleWitnessed { static constexpr Ev kType = Ev::MiracleWitnessed; int cell = -1; std::string kind; double good = 0; std::vector<int> ids;
	template <class F> void fields(F&& f) const { DM_F(cell); DM_F(kind); DM_F(good); DM_F(ids); } };
struct EvLightning { static constexpr Ev kType = Ev::Lightning; int cell = -1; bool natural = false;
	template <class F> void fields(F&& f) const { DM_F(cell); DM_F(natural); } };
struct EvMeteorLaunched { static constexpr Ev kType = Ev::MeteorLaunched; int cell = -1; double eta = 0; ImpactKind kind = ImpactKind::Meteor; int id = 0;
	template <class F> void fields(F&& f) const { DM_F(cell); DM_F(eta); DM_F(kind); DM_F(id); } };
struct EvMeteorImpact { static constexpr Ev kType = Ev::MeteorImpact; int cell = -1; double ang = 0; ImpactKind kind = ImpactKind::Meteor; bool sea = false;
	template <class F> void fields(F&& f) const { DM_F(cell); DM_F(ang); DM_F(kind); DM_F(sea); } };
struct EvTsunamiStart { static constexpr Ev kType = Ev::TsunamiStart; int cell = -1;
	template <class F> void fields(F&& f) const { DM_F(cell); } };
// natural (v3, design review finding 7): the wave of a natural quake, not of the god's meteor or quake
struct EvTsunamiHit { static constexpr Ev kType = Ev::TsunamiHit; int cell = -1; double height = 0; bool natural = false;
	template <class F> void fields(F&& f) const { DM_F(cell); DM_F(height); DM_F(natural); } };
struct EvVolcanoStart { static constexpr Ev kType = Ev::VolcanoStart; int cell = -1; int id = 0; bool natural = false;
	template <class F> void fields(F&& f) const { DM_F(cell); DM_F(id); DM_F(natural); } };
struct EvVolcanoEnd { static constexpr Ev kType = Ev::VolcanoEnd; int cell = -1; int id = 0;
	template <class F> void fields(F&& f) const { DM_F(cell); DM_F(id); } };
struct EvQuake { static constexpr Ev kType = Ev::Quake; int cell = -1; double ang = 0; double power = 0; bool natural = false;
	template <class F> void fields(F&& f) const { DM_F(cell); DM_F(ang); DM_F(power); DM_F(natural); } };
struct EvFireStart { static constexpr Ev kType = Ev::FireStart; int cell = -1;
	template <class F> void fields(F&& f) const { DM_F(cell); } };
struct EvStorm { static constexpr Ev kType = Ev::Storm; int cell = -1; double ang = 0; bool start = true;
	template <class F> void fields(F&& f) const { DM_F(cell); DM_F(ang); DM_F(start); } };
struct EvDrought { static constexpr Ev kType = Ev::Drought; int cell = -1; double ang = 0; bool start = true;
	template <class F> void fields(F&& f) const { DM_F(cell); DM_F(ang); DM_F(start); } };
struct EvSettlementFounded { static constexpr Ev kType = Ev::SettlementFounded; int id = -1; int cell = -1; int culture = -1; FoundedBy by = FoundedBy::Settlers;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(cell); DM_F(culture); DM_F(by); } };
struct EvSettlementTier { static constexpr Ev kType = Ev::SettlementTier; int id = -1; int tier = 0; int prev = 0;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(tier); DM_F(prev); } };
struct EvSettlementBuilding { static constexpr Ev kType = Ev::SettlementBuilding; int id = -1; Building::Id building = Building::SHRINE;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(building); } };
struct EvSettlementFamine { static constexpr Ev kType = Ev::SettlementFamine; int id = -1;
	template <class F> void fields(F&& f) const { DM_F(id); } };
struct EvSettlementPlague { static constexpr Ev kType = Ev::SettlementPlague; int id = -1; bool start = true;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(start); } };
struct EvSettlementDestroyed { static constexpr Ev kType = Ev::SettlementDestroyed; int id = -1; int cell = -1; int culture = -1; Cause cause = Cause::None;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(cell); DM_F(culture); DM_F(cause); } };
// from / to are culture ids
struct EvSettlementCaptured { static constexpr Ev kType = Ev::SettlementCaptured; int id = -1; int from = -1; int to = -1; CaptureHow how = CaptureHow::War;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(from); DM_F(to); DM_F(how); } };
struct EvPrayer { static constexpr Ev kType = Ev::Prayer; int id = -1; PrayerKind kind = PrayerKind::Rain; bool start = true;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(kind); DM_F(start); } };
struct EvPrayerAnswered { static constexpr Ev kType = Ev::PrayerAnswered; int id = -1; PrayerKind kind = PrayerKind::Rain;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(kind); } };
struct EvCultureBorn { static constexpr Ev kType = Ev::CultureBorn; int id = -1; int parent = -1; int cell = -1; BirthReason reason = BirthReason::Awaken;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(parent); DM_F(cell); DM_F(reason); } };
struct EvCultureDied { static constexpr Ev kType = Ev::CultureDied; int id = -1;
	template <class F> void fields(F&& f) const { DM_F(id); } };
struct EvCultureAge { static constexpr Ev kType = Ev::CultureAge; int id = -1; int age = 0; int prev = 0;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(age); DM_F(prev); } };
struct EvWorldAge { static constexpr Ev kType = Ev::WorldAge; int age = 0;
	template <class F> void fields(F&& f) const { DM_F(age); } };
// a = aggressor (culture ids). (v3.1, design review 11/13) ally = the culture whose call to arms a
// joined (a is its ally and b attacked it; -1: a war of a's own); blamed = a's people blamed the god for
// this war (the god struck them, or favoured b over them, in the last decades: they turned on b).
// (chronicle-diplomacy) envied = the blame was envy: the god's favour of b over a weighed more than its
// wrath on a ("the god favoured them"); blamed && !envied: "the god struck us". Never true without blamed.
struct EvWarDeclared { static constexpr Ev kType = Ev::WarDeclared; int a = -1; int b = -1; int ally = -1; bool blamed = false; bool envied = false;
	template <class F> void fields(F&& f) const { DM_F(a); DM_F(b); DM_F(ally); DM_F(blamed); DM_F(envied); } };
// victor = culture id, -1 = draw. (v3.1) thanked = the victor's people thanked the god for the victory
// (the god's grace was with them while they fought: their devotion rose, the loser's love fell)
struct EvWarEnded { static constexpr Ev kType = Ev::WarEnded; int a = -1; int b = -1; int victor = -1; bool thanked = false;
	template <class F> void fields(F&& f) const { DM_F(a); DM_F(b); DM_F(victor); DM_F(thanked); } };
// attacker / defender culture ids; settlement = the defending settlement; won = attacker won
struct EvBattle { static constexpr Ev kType = Ev::Battle; int cell = -1; int attacker = -1; int defender = -1; bool won = false; int settlement = -1;
	template <class F> void fields(F&& f) const { DM_F(cell); DM_F(attacker); DM_F(defender); DM_F(won); DM_F(settlement); } };
// a, b = settlement ids (first caravan between the pair)
struct EvTradeRoute { static constexpr Ev kType = Ev::TradeRoute; int a = -1; int b = -1;
	template <class F> void fields(F&& f) const { DM_F(a); DM_F(b); } };
struct EvProphetRisen { static constexpr Ev kType = Ev::ProphetRisen; int culture = -1; int settlement = -1; std::string name;
	template <class F> void fields(F&& f) const { DM_F(culture); DM_F(settlement); DM_F(name); } };
struct EvProphetDied { static constexpr Ev kType = Ev::ProphetDied; int culture = -1; std::string name;
	template <class F> void fields(F&& f) const { DM_F(culture); DM_F(name); } };
struct EvEdict { static constexpr Ev kType = Ev::Edict; int culture = -1; Edict edict = Edict::None;
	template <class F> void fields(F&& f) const { DM_F(culture); DM_F(edict); } };
struct EvWonderBuilt { static constexpr Ev kType = Ev::WonderBuilt; int settlement = -1; std::string name;
	template <class F> void fields(F&& f) const { DM_F(settlement); DM_F(name); } };
// from / to = culture ids
struct EvNuke { static constexpr Ev kType = Ev::Nuke; int cell = -1; int from = -1; int to = -1;
	template <class F> void fields(F&& f) const { DM_F(cell); DM_F(from); DM_F(to); } };
struct EvArkStarted { static constexpr Ev kType = Ev::ArkStarted; int culture = -1; int settlement = -1;
	template <class F> void fields(F&& f) const { DM_F(culture); DM_F(settlement); } };
struct EvArkLaunched { static constexpr Ev kType = Ev::ArkLaunched; int culture = -1; int settlement = -1;
	template <class F> void fields(F&& f) const { DM_F(culture); DM_F(settlement); } };
// culture (v3, design review finding 7): the culture whose engines put the most CO2 into the air so far
// (-1 when nobody's did, e.g. a cooling after a meteor's dust); the HUD and the chronicle name it for a
// warming or a rising sea instead of blaming the god
struct EvClimateMilestone { static constexpr Ev kType = Ev::ClimateMilestone; MilestoneKind kind = MilestoneKind::Warming; double value = 0; int culture = -1;
	template <class F> void fields(F&& f) const { DM_F(kind); DM_F(value); DM_F(culture); } };
struct EvUnitSpawned { static constexpr Ev kType = Ev::UnitSpawned; int id = 0; UnitType type = UnitType::Settler; int culture = -1; int cell = -1;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(type); DM_F(culture); DM_F(cell); } };
struct EvUnitArrived { static constexpr Ev kType = Ev::UnitArrived; int id = 0; UnitType type = UnitType::Settler; int culture = -1; int cell = -1;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(type); DM_F(culture); DM_F(cell); } };
// cause: 'disbanded', 'lava', 'battle', 'lightning', 'meteor', 'quake', 'wave', 'path', ... (open set)
struct EvUnitDied { static constexpr Ev kType = Ev::UnitDied; int id = 0; UnitType type = UnitType::Settler; int culture = -1; int cell = -1; std::string cause;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(type); DM_F(culture); DM_F(cell); DM_F(cause); } };
struct EvChronicleEntry { static constexpr Ev kType = Ev::ChronicleEntry; ChronicleEntry entry;
	template <class F> void fields(F&& f) const { DM_F(entry); } };
// id = power id
struct EvAweShort { static constexpr Ev kType = Ev::AweShort; std::string id;
	template <class F> void fields(F&& f) const { DM_F(id); } };
struct EvPeopleLost { static constexpr Ev kType = Ev::PeopleLost;
	template <class F> void fields(F&&) const {} };
// the world.ending object
// (v4 mythos, M0) legacy = Ending::legacy (SPEC-MYTHOS 4; None until mythos::legacy is built)
struct EvEnding { static constexpr Ev kType = Ev::Ending; EndingType type = EndingType::Ark; Mood mood = Mood::Parting; int culture = -1; int settlement = -1; double year = 0; Legacy legacy = Legacy::None;
	template <class F> void fields(F&& f) const { DM_F(type); DM_F(mood); DM_F(culture); DM_F(settlement); DM_F(year); DM_F(legacy); } };
// (v3, design review finding 10) a Lower Land press stopped at a settlement's centre just above the sea
// (sea level + 0.05) instead of drowning it: a second press after this one ends would drown it.
// id = the settlement, cell = its centre. At most once per settlement per press. (verifier) A town the
// sea already brought within 0.05 is warned (not drowned) by its first press too.
struct EvDrownWarning { static constexpr Ev kType = Ev::DrownWarning; int id = -1; int cell = -1;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(cell); } };
// (v3.1, design review 11/13) culture ids a < b became allies (formed = true: in contact, at peace,
// relations at least DiplomacyTune::allyForm) or their alliance ended (formed = false: relations below
// allyBreak, a war between them, or one of them died). Allies come to each other's defence.
struct EvAlliance { static constexpr Ev kType = Ev::Alliance; int a = -1; int b = -1; bool formed = true;
	template <class F> void fields(F&& f) const { DM_F(a); DM_F(b); DM_F(formed); } };
// Interface events (SPEC-UI section 0), kept so client code can share the bus.
struct EvUiState { static constexpr Ev kType = Ev::UiState; std::string state, prev;
	template <class F> void fields(F&& f) const { DM_F(state); DM_F(prev); } };
struct EvUiSpeed { static constexpr Ev kType = Ev::UiSpeed; double speed = 1; bool paused = false;
	template <class F> void fields(F&& f) const { DM_F(speed); DM_F(paused); } };
struct EvUiPower { static constexpr Ev kType = Ev::UiPower; std::string id;
	template <class F> void fields(F&& f) const { DM_F(id); } };
struct EvUiSelect { static constexpr Ev kType = Ev::UiSelect; int id = -1;
	template <class F> void fields(F&& f) const { DM_F(id); } };
struct EvUiOverlay { static constexpr Ev kType = Ev::UiOverlay; std::string mode;
	template <class F> void fields(F&& f) const { DM_F(mode); } };
struct EvUiHud { static constexpr Ev kType = Ev::UiHud; bool hidden = false;
	template <class F> void fields(F&& f) const { DM_F(hidden); } };
struct EvUiSettings { static constexpr Ev kType = Ev::UiSettings;
	template <class F> void fields(F&&) const {} };
struct EvUiToast { static constexpr Ev kType = Ev::UiToast; std::string text, kind;
	template <class F> void fields(F&& f) const { DM_F(text); DM_F(kind); } };
struct EvCameraShake { static constexpr Ev kType = Ev::CameraShake; double amount = 0;
	template <class F> void fields(F&& f) const { DM_F(amount); } };
// ---- (v4 mythos, M0) SPEC-MYTHOS 1.8: payload strings are open sets (std::string), ids int (-1 none), numbers double.
// kind = toStr(CardKind) ("judgement"); stage "open" | "answered" | "lapsed" | "withdrawn"; choice -1 unless answered / lapsed
struct EvCard { static constexpr Ev kType = Ev::Card; int id = -1; std::string kind; std::string stage; int culture = -1; int choice = -1;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(kind); DM_F(stage); DM_F(culture); DM_F(choice); } };
// toStr(Presence) of the new and the previous stance
struct EvPresence { static constexpr Ev kType = Ev::Presence; std::string presence; std::string prev;
	template <class F> void fields(F&& f) const { DM_F(presence); DM_F(prev); } };
// toStr(Aspect)
struct EvAspect { static constexpr Ev kType = Ev::Aspect; int culture = -1; std::string aspect; std::string prev;
	template <class F> void fields(F&& f) const { DM_F(culture); DM_F(aspect); DM_F(prev); } };
// area toStr(DecreeArea); stance / prev stanceName(area, s) ("repealed" when a decree is repealed, prev "none" for a first decree); clarity toStr(Clarity)
struct EvDecree { static constexpr Ev kType = Ev::Decree; int culture = -1; std::string area; std::string stance; std::string prev; std::string clarity;
	template <class F> void fields(F&& f) const { DM_F(culture); DM_F(area); DM_F(stance); DM_F(prev); DM_F(clarity); } };
// severity 0..1 (3.6)
struct EvBreach { static constexpr Ev kType = Ev::Breach; int culture = -1; std::string area; int settlement = -1; int figure = -1; double severity = 0;
	template <class F> void fields(F&& f) const { DM_F(culture); DM_F(area); DM_F(settlement); DM_F(figure); DM_F(severity); } };
// outcome "heard" | "wisdom" | "sect" | "misread" | "catastrophe" (toStr(WordOutcome) for the finer ones is allowed: open set)
struct EvWord { static constexpr Ev kType = Ev::Word; int id = -1; int culture = -1; std::string content; std::string clarity; std::string channel; std::string outcome;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(culture); DM_F(content); DM_F(clarity); DM_F(channel); DM_F(outcome); } };
// what "sworn" | "kept" | "god-broke" | "people-broke" | "forgiven" | "lapsed"
struct EvPact { static constexpr Ev kType = Ev::Pact; int id = -1; int culture = -1; std::string what;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(culture); DM_F(what); } };
// kind toStr(FigureKind); what "chosen" | "rise" | "vindicated" | "discredited" | "redeemed" | "fallen" | "exalted" | "died"
struct EvFigure { static constexpr Ev kType = Ev::Figure; int id = -1; std::string kind; int culture = -1; int settlement = -1; std::string what;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(kind); DM_F(culture); DM_F(settlement); DM_F(what); } };
// kind toStr(TrialKind); what "begun" | "passed" | "failed"
struct EvTrial { static constexpr Ev kType = Ev::Trial; int figure = -1; std::string kind; std::string what;
	template <class F> void fields(F&& f) const { DM_F(figure); DM_F(kind); DM_F(what); } };
// by "deed" | "heart"; sentence "forgive" | "warn" | "remove" | "disaster"
struct EvJudgement { static constexpr Ev kType = Ev::Judgement; int card = -1; int culture = -1; std::string by; std::string sentence;
	template <class F> void fields(F&& f) const { DM_F(card); DM_F(culture); DM_F(by); DM_F(sentence); } };
// what "born" | "grew" | "credited" | "forgotten" | "inherited"
struct EvLegend { static constexpr Ev kType = Ev::Legend; int id = -1; int culture = -1; std::string what;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(culture); DM_F(what); } };
// kind toStr(BlessingKind); start false when it ends
struct EvBlessing { static constexpr Ev kType = Ev::Blessing; int settlement = -1; std::string kind; bool start = true;
	template <class F> void fields(F&& f) const { DM_F(settlement); DM_F(kind); DM_F(start); } };
// kind toStr(OmenKind); what "foreseen" | "warned" | "repented" | "prepared" | "exploited" | "dismissed" | "struck" | "averted"
struct EvOmen { static constexpr Ev kType = Ev::Omen; int id = -1; std::string kind; int cell = -1; int culture = -1; std::string what;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(kind); DM_F(cell); DM_F(culture); DM_F(what); } };
// kind toStr(DisputeKind); answer toStr(DisputeAnswer) or "" while open
struct EvDispute { static constexpr Ev kType = Ev::Dispute; int culture = -1; std::string kind; std::string answer;
	template <class F> void fields(F&& f) const { DM_F(culture); DM_F(kind); DM_F(answer); } };
// what "rose" | "corrupt" | "cleansed" | "reformed" | "endorsed" | "fell"
struct EvHouse { static constexpr Ev kType = Ev::House; int culture = -1; std::string what;
	template <class F> void fields(F&& f) const { DM_F(culture); DM_F(what); } };
// what "rise" | "exposed" | "martyr" | "faded" | "split"
struct EvRival { static constexpr Ev kType = Ev::Rival; int id = -1; int culture = -1; int figure = -1; std::string what;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(culture); DM_F(figure); DM_F(what); } };
// charge toStr(Charge); what "chosen" | "proud" | "rebuked" | "released" | "replaced"
struct EvBearers { static constexpr Ev kType = Ev::Bearers; int culture = -1; std::string charge; std::string what;
	template <class F> void fields(F&& f) const { DM_F(culture); DM_F(charge); DM_F(what); } };
// kind toStr(RevelationKind); what "given" | "shared" | "hoarded" | "weaponised"
struct EvRevelation { static constexpr Ev kType = Ev::Revelation; int culture = -1; std::string kind; std::string what; bool early = false;
	template <class F> void fields(F&& f) const { DM_F(culture); DM_F(kind); DM_F(what); DM_F(early); } };
// domain toStr(KeeperDomain); what "appointed" | "acted" | "disagreed" | "rogue" | "dismissed"
struct EvKeeper { static constexpr Ev kType = Ev::Keeper; int id = -1; std::string domain; int culture = -1; std::string what;
	template <class F> void fields(F&& f) const { DM_F(id); DM_F(domain); DM_F(culture); DM_F(what); } };
// answer "" when they come of age, else the ComingOfAge answer ("bless" | "remind" | "hold")
struct EvGrown { static constexpr Ev kType = Ev::Grown; int culture = -1; std::string answer;
	template <class F> void fields(F&& f) const { DM_F(culture); DM_F(answer); } };
// start false when the silence ends
struct EvWithdraw { static constexpr Ev kType = Ev::Withdraw; bool start = true; bool announced = false;
	template <class F> void fields(F&& f) const { DM_F(start); DM_F(announced); } };
// kind toStr(RenewalKind); what "begun" | "remnant" | "ended"
struct EvRenewal { static constexpr Ev kType = Ev::Renewal; std::string kind; std::string what;
	template <class F> void fields(F&& f) const { DM_F(kind); DM_F(what); } };
#undef DM_F

using EventPayload = std::variant<
#define DM_EV_VARIANT(T, s) Ev##T,
	DM_EVENT_LIST(DM_EV_VARIANT)
#undef DM_EV_VARIANT
	std::monostate>;

struct Event {
	Ev type = Ev::Count;
	EventPayload data = std::monostate{};
	const char* name() const { return eventName(type); }
	template <class T> const T* as() const { return std::get_if<T>(&data); }
	// calls f(key, value) for each payload field
	template <class F> void fields(F&& f) const {
		std::visit([&](const auto& p) {
			if constexpr (!std::is_same_v<std::decay_t<decltype(p)>, std::monostate>) p.fields(f);
		}, data);
	}
};

// ------------------------------------------------------------------ the bus

class EventBus {
public:
	using Handler = std::function<void(const Event&)>;
	using Token = uint64_t;                   // 0 is never a valid token

	EventBus() = default;
	EventBus(const EventBus&) = delete;
	EventBus& operator=(const EventBus&) = delete;

	Token on(Ev type, Handler fn);
	Token onAll(Handler fn);
	template <class T, class Fn> Token on(Fn&& fn) {
		return on(T::kType, [f = std::forward<Fn>(fn)](const Event& e) {
			if (const T* p = e.as<T>()) f(*p);
		});
	}
	// Removes one subscription; safe to call repeatedly or with 0.
	void off(Token tok);
	void clear();
	void emit(Event e);
	template <class T> void emit(T payload) {
		Event e;
		e.type = T::kType;
		e.data = std::move(payload);
		emit(std::move(e));
	}
	// Number of live subscriptions (tests).
	int count() const;

private:
	struct Entry { Token seq = 0; Handler fn; bool live = true; };
	using List = std::vector<std::shared_ptr<Entry>>;
	List& listFor(int slot) { return lists_[size_t(slot)]; }
	void compact();

	// slot kEventCount holds the onAll handlers
	std::array<List, kEventCount + 1> lists_;
	Token seq_ = 0;
	int depth_ = 0;
	bool dirty_ = false;
};

} // namespace dm
