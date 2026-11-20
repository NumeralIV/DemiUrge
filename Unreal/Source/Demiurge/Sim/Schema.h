// DEMIURGE — Sim/Schema.h : the shared data model (SPEC-SIM section 1). Port of js/sim/schema.js
// plus the enums of js/sim/state.js.
//
//   Enums with name tables:  Biome::Id (u8 cell values), Building::Id, Age::Id, Phase, Cause,
//                            UnitType, NavalMode, DisasterType, Edict, PrayerKind, FoundedBy,
//                            CaptureHow, BirthReason, ImpactKind, MilestoneKind, EndingType, Mood
//   toStr(x) -> "name" (the JS string), fromStr(text, x&) -> bool
//   kBiomeNames, kAgeNames, kBuildingNames, kCultureColors, state::kVegT
//   Entities: Settlement, Culture, Relation, Unit, Disaster, ChronicleEntry, Prophet, Prayer,
//             Snapshot, River, Ending, ArkRecord
//   Factories (dm::state): newSettlement, newCulture, newUnit, newDisaster, freeColorSlot
//   Lookups   (dm::state): settlement, culture, findUnit, findDisaster, eraseUnit, eraseDisaster,
//                          relKey
//
// Entities live in std::vectors inside the World and are addressed by integer ids:
//   settlements / cultures: id == index; dead ones stay in the list (dead = true).
//   units / disasters: live lists; id from world.nextId; removed when they end (findUnit(w, id)).
// Never keep a Settlement& / Culture& / Unit& / Disaster& (or pointer) across a call that may add
// or remove entities of that kind: the vector may reallocate. Keep ids.
// Fields named in comments as "transient" are caches: never saved, rebuilt after loading.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dm {

struct World;

// ------------------------------------------------------------------ enums used as numbers

namespace Biome {
enum Id : uint8_t {
	SEA = 0, SHALLOWS = 1, BEACH = 2, DESERT = 3, SAVANNA = 4, GRASSLAND = 5, FOREST = 6, RAINFOREST = 7,
	TAIGA = 8, TUNDRA = 9, ICE = 10, ROCK = 11, SNOWPEAK = 12, WETLAND = 13, BARREN = 14, SEAICE = 15,
	COUNT = 16
};
}
extern const char* const kBiomeNames[Biome::COUNT];   // 'Sea', 'Shallows', ..., 'Snow peak', ..., 'Sea ice'

namespace Age {
enum Id : int { STONE = 0, BRONZE = 1, IRON = 2, SAIL = 3, INDUSTRY = 4, ATOMIC = 5, SPACE = 6, COUNT = 7 };
}
extern const char* const kAgeNames[Age::COUNT];       // 'Stone', 'Bronze', 'Iron', 'Sail', 'Industry', 'Atomic', 'Space'

// DM.BUILDINGS, in order. Settlement::b is indexed by these.
namespace Building {
enum Id : int {
	SHRINE = 0, TEMPLE, GRANARY, WALLS, PORT, MARKET, MINE, FACTORY, ACADEMY, PLANT, LAUNCH, WONDER,
	COUNT
};
}
extern const char* const kBuildingNames[Building::COUNT];   // 'shrine', 'temple', ..., 'wonder'

// Six colour-blind-safe culture colours (Okabe-Ito, approximately linear RGB 0..1).
extern const std::array<std::array<double, 3>, 6> kCultureColors;

namespace state {
// Vegetation levels at which a cell shows 1, 2, 3 trees (Life sets dirty.objects on crossings).
inline constexpr double kVegT[3] = { 0.22, 0.5, 0.78 };
}

// ------------------------------------------------------------------ enums used as tags
// Each has toStr() returning the JS string and fromStr() parsing it.

enum class Phase : uint8_t { Molten, Water, Life, People, History, Ended };
// Why a settlement was destroyed ('drowned', 'meteor', ...). None = alive / unknown.
// Sea (v3, design review finding 7): the centre went under a sea the people's own engines raised (Climate's
// sea rise), never the god's doing; Drowned is the god's (Lower Land, quakes) or a flooded lake.
enum class Cause : uint8_t { None, Drowned, Meteor, Lava, War, Famine, Plague, Quake, Nuke, Abandoned, Wave, Lightning, Sea };
enum class UnitType : uint8_t { Settler, Trader, Army, Prophet };
// What a unit may cross: Land = JS false, Coastal = 'coastal', Open = JS true (Path section 3.6).
enum class NavalMode : uint8_t { Land, Coastal, Open };
enum class DisasterType : uint8_t { None, Comet, Meteor, Volcano, Tsunami, Storm, Drought };
enum class Edict : uint8_t { None, Peace, Expand, Build, Crusade };
enum class PrayerKind : uint8_t { Rain, Heal, Peace, Bloom };
enum class FoundedBy : uint8_t { Awaken, Settlers };
enum class CaptureHow : uint8_t { War, Schism };
enum class BirthReason : uint8_t { Awaken, Schism, Distance };
enum class ImpactKind : uint8_t { Comet, Meteor };
enum class MilestoneKind : uint8_t { Warming, Cooling, SeaRise, SeaFall, IceAge, Thaw };
enum class EndingType : uint8_t { Ark, Silence };
enum class Mood : uint8_t { Beloved, Feared, Forgotten, Parting, Ash, Quiet };

const char* toStr(Phase v);         bool fromStr(std::string_view s, Phase& out);
const char* toStr(Cause v);         bool fromStr(std::string_view s, Cause& out);          // None <-> "none"
const char* toStr(UnitType v);      bool fromStr(std::string_view s, UnitType& out);
const char* toStr(NavalMode v);     bool fromStr(std::string_view s, NavalMode& out);      // "land" "coastal" "open"
const char* toStr(DisasterType v);  bool fromStr(std::string_view s, DisasterType& out);   // None <-> "none"
const char* toStr(Edict v);         bool fromStr(std::string_view s, Edict& out);          // None <-> "none"
const char* toStr(PrayerKind v);    bool fromStr(std::string_view s, PrayerKind& out);
const char* toStr(FoundedBy v);     bool fromStr(std::string_view s, FoundedBy& out);
const char* toStr(CaptureHow v);    bool fromStr(std::string_view s, CaptureHow& out);
const char* toStr(BirthReason v);   bool fromStr(std::string_view s, BirthReason& out);
const char* toStr(ImpactKind v);    bool fromStr(std::string_view s, ImpactKind& out);
const char* toStr(MilestoneKind v); bool fromStr(std::string_view s, MilestoneKind& out); // "sea-rise", "ice-age", ...
const char* toStr(EndingType v);    bool fromStr(std::string_view s, EndingType& out);
const char* toStr(Mood v);          bool fromStr(std::string_view s, Mood& out);

// Number of enumerators (for loops and validation).
template <class E> constexpr int enumCount();
template <> constexpr int enumCount<Phase>() { return 6; }
template <> constexpr int enumCount<Cause>() { return 13; }
template <> constexpr int enumCount<UnitType>() { return 4; }
template <> constexpr int enumCount<NavalMode>() { return 3; }
template <> constexpr int enumCount<DisasterType>() { return 7; }
template <> constexpr int enumCount<Edict>() { return 5; }
template <> constexpr int enumCount<PrayerKind>() { return 4; }
template <> constexpr int enumCount<FoundedBy>() { return 2; }
template <> constexpr int enumCount<CaptureHow>() { return 2; }
template <> constexpr int enumCount<BirthReason>() { return 3; }
template <> constexpr int enumCount<ImpactKind>() { return 2; }
template <> constexpr int enumCount<MilestoneKind>() { return 6; }
template <> constexpr int enumCount<EndingType>() { return 2; }
template <> constexpr int enumCount<Mood>() { return 6; }

// Free-form persisted numbers keyed by name (sorted map: iteration order is deterministic).
using NumBag = std::map<std::string, double>;

// ------------------------------------------------------------------ entities

struct Prayer {
	PrayerKind kind = PrayerKind::Rain;
	double since = 0;                       // year the prayer started
	bool operator==(const Prayer&) const = default;
};

struct Settlement {
	int id = -1;
	int cell = -1;
	std::string name;
	int culture = -1;                       // culture id
	double founded = 0;                     // year
	bool dead = false;
	std::optional<double> deadYear;         // null while alive
	Cause cause = Cause::None;              // why it died
	double pop = 0;                         // people (float)
	int tier = 0;                           // 0 camp, 1 village, 2 town, 3 city, 4 metropolis
	double K = 0;                           // carrying capacity computed this year
	double growth = 0;                      // last year's population change
	int radius = 2;                         // territory radius in hops
	std::array<uint8_t, Building::COUNT> b{};   // 0 or 1 per building: s.b[Building::SHRINE]
	double devotion = 0.5;                  // 0..1
	double love = 0;                        // -1 terror .. +1 adoration
	double jaded = 0;                       // 0..1
	double plague = 0;                      // severity 0..1
	double immune = 0;                      // years of immunity left
	double inspire = 0, blessed = 0;        // years remaining
	int famine = 0;                         // consecutive years over capacity
	std::optional<Prayer> prayer;           // null = not praying
	int apart = 0;                          // consecutive years estranged from its culture
	bool capital = false;
	double ark = 0;                         // 0..1 construction progress
	double lastExpand = 0;                  // year of the last settler party or failed search
	// transient (Civ.claim / Civ.rebuild): territory cells; size() is JS `_n`
	std::vector<int32_t> cells;

	bool has(Building::Id k) const { return b[size_t(k)] != 0; }
	bool alive() const { return !dead; }
};

struct Traits {
	double piety = 0.5, aggression = 0.5, curiosity = 0.5, seafaring = 0.5;
	bool operator==(const Traits&) const = default;
};

struct Prophet {
	std::string name;
	int settlement = -1;                    // where he lives now
	double until = 0;                       // year his term ends
	bool operator==(const Prophet&) const = default;
};

struct Culture {
	int id = -1;
	std::string name, adj, plural;          // 'Umar', 'Umaran', 'the Umari'
	std::array<double, 3> color{ 1, 1, 1 }; // 0..1, from kCultureColors by free slot
	std::string lang;                       // language, OPAQUE text owned by Names (empty = none yet)
	int parent = -1;                        // culture id or -1
	double born = 0;
	bool dead = false;
	int age = 0;                            // Age::Id
	double science = 0;                     // progress-years toward the next age
	int capital = -1;                       // settlement id or -1
	std::string godName, epithet;
	Traits traits;
	std::optional<Prophet> prophet;
	Edict edict = Edict::None;
	double edictSince = -9999;
	double pop = 0;                         // refreshed yearly
	int towns = 0;
	double exhaustion = 0;                  // 0..1 war weariness
	NumBag seen;                            // power id -> accumulated faith witnessed
	double lastSplit = -9999;               // year a daughter culture last split from it
};

// Between two cultures, stored in World::vars.rel under relKey(a, b) (low id first).
struct Relation {
	double v = 0;                           // -1..1
	bool war = false;
	double since = 0;                       // year the current war / peace began
	double truce = 0;                       // no war before this year
	double trade = 0;                       // 0..1
	bool contact = false;
	int aggressor = -1;                     // culture id or -1
	bool operator==(const Relation&) const = default;
};

struct Unit {
	int id = 0;
	UnitType type = UnitType::Settler;
	int culture = -1;
	int from = -1, to = -1;                 // settlement ids (to = -1 when heading for an empty site)
	int dest = -1;                          // destination cell
	std::vector<int32_t> path;              // cells, path[0] = start
	int i = 0;                              // between path[i] and path[i + 1]
	double t = 0;                           // 0..1 along that step
	double speed = 5;                       // cells per year on plain land
	double size = 0;                        // people or strength
	NavalMode naval = NavalMode::Land;
	bool sea = false;                       // its current cell is a sea cell (drawn as a ship)
	double born = 0;
	NumBag data;                            // module-specific numbers (persisted)
};

struct Disaster {
	int id = 0;
	DisasterType type = DisasterType::None;
	int cell = -1;
	double t = 0, dur = 0;                  // years; removed when t >= dur (comet / meteor: on impact)
	double power = 1;
	double ang = 0;
	bool natural = false;
	// typed parts of JS `data`:
	double eta = 0;                         // comet / meteor: real seconds to impact
	double flight = 0;                      // comet / meteor: total flight seconds
	double front = 0;                       // tsunami: current angular radius of the wave front
	std::vector<int32_t> cells;             // e.g. the volcano's lava field (persisted)
	NumBag data;                            // any other module-specific numbers (persisted)
};

struct ChronicleEntry {
	int id = 0;
	double year = 0;
	std::string type;                       // event kind ('founded', 'war', ...; Chronicle's vocabulary)
	std::string text;
	int cell = -1;
	int culture = -1;
	int weight = 1;                         // 1 minor, 2 notable, 3 epochal
	bool operator==(const ChronicleEntry&) const = default;
};

// Time-lapse frame (Chronicle.snapshot). Kept in memory only (not serialized).
struct Snapshot {
	double year = 0, heat = 0, seaLevel = 0, pop = 0;
	std::vector<uint8_t> biome;             // n
	std::vector<int8_t> owner;              // n: culture id of the owning settlement, -1 none
	std::vector<uint8_t> urban;             // n: round(urban * 255)
};

// Derived (Hydro.update): one river from source to mouth; cells and flow have equal length.
struct River {
	std::vector<int32_t> cells;
	std::vector<float> flow;
};

// world.ending (SPEC-SIM 3.12)
struct Ending {
	EndingType type = EndingType::Ark;
	Mood mood = Mood::Parting;
	int culture = -1, settlement = -1;
	double year = 0;
	bool operator==(const Ending&) const = default;
};

// world.vars.ark (Society)
struct ArkRecord {
	int culture = -1, settlement = -1;
	double year = 0;
	bool operator==(const ArkRecord&) const = default;
};

namespace state {

// ------------------------------------------------------------------ factories
// The ONLY way entities are created: fill every field with its default (founded / born /
// lastExpand = world.year), assign the id, push into the world list, return a reference that
// stays valid until the next entity of that kind is created.

Settlement& newSettlement(World& w, int cell = -1, int cultureId = -1);
// Picks the colour by freeColorSlot.
Culture& newCulture(World& w, int parent = -1);
Unit& newUnit(World& w, UnitType type);                           // id = w.nextId.unit++
Disaster& newDisaster(World& w, DisasterType type, int cell = -1);  // id = w.nextId.disaster++
// Index into kCultureColors not used by a living culture (falls back to id modulo palette size).
int freeColorSlot(const World& w);

// ------------------------------------------------------------------ lookups (nullptr if absent)

Settlement* settlement(World& w, int id);
const Settlement* settlement(const World& w, int id);
Culture* culture(World& w, int id);
const Culture* culture(const World& w, int id);
Unit* findUnit(World& w, int id);
const Unit* findUnit(const World& w, int id);
Disaster* findDisaster(World& w, int id);
const Disaster* findDisaster(const World& w, int id);
// Remove from the live list (order of the others is kept). False if absent. Emits nothing:
// Units.kill / Powers emit their own events.
bool eraseUnit(World& w, int id);
bool eraseDisaster(World& w, int id);
// Key of the relation between cultures a and b in World::vars.rel: (min, max).
inline std::pair<int, int> relKey(int a, int b) { return a < b ? std::pair<int, int>(a, b) : std::pair<int, int>(b, a); }

} // namespace state
} // namespace dm
