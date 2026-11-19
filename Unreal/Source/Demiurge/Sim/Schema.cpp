// DEMIURGE — Sim/Schema.cpp : see Schema.h (port of js/sim/schema.js).
#include "Schema.h"

#include <cmath>

#include "State.h"

namespace dm {

const char* const kBiomeNames[Biome::COUNT] = {
	"Sea", "Shallows", "Beach", "Desert", "Savanna", "Grassland", "Forest", "Rainforest",
	"Taiga", "Tundra", "Ice", "Rock", "Snow peak", "Wetland", "Barren", "Sea ice",
};

const char* const kAgeNames[Age::COUNT] = { "Stone", "Bronze", "Iron", "Sail", "Industry", "Atomic", "Space" };

const char* const kBuildingNames[Building::COUNT] = {
	"shrine", "temple", "granary", "walls", "port", "market", "mine", "factory", "academy", "plant", "launch", "wonder",
};

const std::array<std::array<double, 3>, 6> kCultureColors = { {
	{ 0.787, 0.342, 0.0 },     // #e69f00 orange
	{ 0.093, 0.458, 0.807 },   // #56b4e9 sky blue
	{ 0.0, 0.342, 0.171 },     // #009e73 green
	{ 0.871, 0.776, 0.058 },   // #f0e442 yellow
	{ 0.665, 0.112, 0.0 },     // #d55e00 vermilion
	{ 0.604, 0.191, 0.392 },   // #cc79a7 purple
} };

// ------------------------------------------------------------------ enum names

namespace {
template <class E, size_t N>
const char* nameIn(const char* const (&names)[N], E v) {
	const size_t i = size_t(v);
	return i < N ? names[i] : "?";
}
template <class E, size_t N>
bool parseIn(const char* const (&names)[N], std::string_view s, E& out) {
	for (size_t i = 0; i < N; i++) if (s == names[i]) { out = E(i); return true; }
	return false;
}

const char* const PHASE_N[] = { "molten", "water", "life", "people", "history", "ended" };
const char* const CAUSE_N[] = { "none", "drowned", "meteor", "lava", "war", "famine", "plague", "quake", "nuke", "abandoned", "wave", "lightning" };
const char* const UNIT_N[] = { "settler", "trader", "army", "prophet" };
const char* const NAVAL_N[] = { "land", "coastal", "open" };
const char* const DIS_N[] = { "none", "comet", "meteor", "volcano", "tsunami", "storm", "drought" };
const char* const EDICT_N[] = { "none", "peace", "expand", "build", "crusade" };
const char* const PRAYER_N[] = { "rain", "heal", "peace", "bloom" };
const char* const BY_N[] = { "awaken", "settlers" };
const char* const HOW_N[] = { "war", "schism" };
const char* const REASON_N[] = { "awaken", "schism", "distance" };
const char* const IMPACT_N[] = { "comet", "meteor" };
const char* const MILE_N[] = { "warming", "cooling", "sea-rise", "sea-fall", "ice-age", "thaw" };
const char* const ENDING_N[] = { "ark", "silence" };
const char* const MOOD_N[] = { "beloved", "feared", "forgotten", "parting", "ash", "quiet" };
} // namespace

const char* toStr(Phase v) { return nameIn(PHASE_N, v); }
const char* toStr(Cause v) { return nameIn(CAUSE_N, v); }
const char* toStr(UnitType v) { return nameIn(UNIT_N, v); }
const char* toStr(NavalMode v) { return nameIn(NAVAL_N, v); }
const char* toStr(DisasterType v) { return nameIn(DIS_N, v); }
const char* toStr(Edict v) { return nameIn(EDICT_N, v); }
const char* toStr(PrayerKind v) { return nameIn(PRAYER_N, v); }
const char* toStr(FoundedBy v) { return nameIn(BY_N, v); }
const char* toStr(CaptureHow v) { return nameIn(HOW_N, v); }
const char* toStr(BirthReason v) { return nameIn(REASON_N, v); }
const char* toStr(ImpactKind v) { return nameIn(IMPACT_N, v); }
const char* toStr(MilestoneKind v) { return nameIn(MILE_N, v); }
const char* toStr(EndingType v) { return nameIn(ENDING_N, v); }
const char* toStr(Mood v) { return nameIn(MOOD_N, v); }
bool fromStr(std::string_view s, Phase& out) { return parseIn(PHASE_N, s, out); }
bool fromStr(std::string_view s, Cause& out) { return parseIn(CAUSE_N, s, out); }
bool fromStr(std::string_view s, UnitType& out) { return parseIn(UNIT_N, s, out); }
bool fromStr(std::string_view s, NavalMode& out) { return parseIn(NAVAL_N, s, out); }
bool fromStr(std::string_view s, DisasterType& out) { return parseIn(DIS_N, s, out); }
bool fromStr(std::string_view s, Edict& out) { return parseIn(EDICT_N, s, out); }
bool fromStr(std::string_view s, PrayerKind& out) { return parseIn(PRAYER_N, s, out); }
bool fromStr(std::string_view s, FoundedBy& out) { return parseIn(BY_N, s, out); }
bool fromStr(std::string_view s, CaptureHow& out) { return parseIn(HOW_N, s, out); }
bool fromStr(std::string_view s, BirthReason& out) { return parseIn(REASON_N, s, out); }
bool fromStr(std::string_view s, ImpactKind& out) { return parseIn(IMPACT_N, s, out); }
bool fromStr(std::string_view s, MilestoneKind& out) { return parseIn(MILE_N, s, out); }
bool fromStr(std::string_view s, EndingType& out) { return parseIn(ENDING_N, s, out); }
bool fromStr(std::string_view s, Mood& out) { return parseIn(MOOD_N, s, out); }

static_assert(sizeof(PHASE_N) / sizeof(PHASE_N[0]) == size_t(enumCount<Phase>()));
static_assert(sizeof(CAUSE_N) / sizeof(CAUSE_N[0]) == size_t(enumCount<Cause>()));
static_assert(sizeof(UNIT_N) / sizeof(UNIT_N[0]) == size_t(enumCount<UnitType>()));
static_assert(sizeof(NAVAL_N) / sizeof(NAVAL_N[0]) == size_t(enumCount<NavalMode>()));
static_assert(sizeof(DIS_N) / sizeof(DIS_N[0]) == size_t(enumCount<DisasterType>()));
static_assert(sizeof(EDICT_N) / sizeof(EDICT_N[0]) == size_t(enumCount<Edict>()));
static_assert(sizeof(PRAYER_N) / sizeof(PRAYER_N[0]) == size_t(enumCount<PrayerKind>()));
static_assert(sizeof(BY_N) / sizeof(BY_N[0]) == size_t(enumCount<FoundedBy>()));
static_assert(sizeof(HOW_N) / sizeof(HOW_N[0]) == size_t(enumCount<CaptureHow>()));
static_assert(sizeof(REASON_N) / sizeof(REASON_N[0]) == size_t(enumCount<BirthReason>()));
static_assert(sizeof(IMPACT_N) / sizeof(IMPACT_N[0]) == size_t(enumCount<ImpactKind>()));
static_assert(sizeof(MILE_N) / sizeof(MILE_N[0]) == size_t(enumCount<MilestoneKind>()));
static_assert(sizeof(ENDING_N) / sizeof(ENDING_N[0]) == size_t(enumCount<EndingType>()));
static_assert(sizeof(MOOD_N) / sizeof(MOOD_N[0]) == size_t(enumCount<Mood>()));

namespace state {

// ------------------------------------------------------------------ factories

Settlement& newSettlement(World& w, int cell, int cultureId) {
	Settlement s;
	s.id = int(w.settlements.size());
	s.cell = cell;
	s.culture = cultureId;
	s.founded = w.year;
	s.lastExpand = w.year;
	w.settlements.push_back(std::move(s));
	return w.settlements.back();
}

int freeColorSlot(const World& w) {
	const size_t P = kCultureColors.size();
	bool used[6] = { false, false, false, false, false, false };
	for (const Culture& c : w.cultures) {
		if (c.dead) continue;
		for (size_t i = 0; i < P; i++) {
			const auto& k = kCultureColors[i];
			if (std::fabs(k[0] - c.color[0]) + std::fabs(k[1] - c.color[1]) + std::fabs(k[2] - c.color[2]) < 1e-3) used[i] = true;
		}
	}
	for (size_t i = 0; i < P; i++) if (!used[i]) return int(i);
	return int(w.cultures.size() % P);
}

Culture& newCulture(World& w, int parent) {
	Culture c;
	c.id = int(w.cultures.size());
	c.color = kCultureColors[size_t(freeColorSlot(w))];
	c.parent = parent;
	c.born = w.year;
	w.cultures.push_back(std::move(c));
	return w.cultures.back();
}

Unit& newUnit(World& w, UnitType type) {
	Unit u;
	u.id = w.nextId.unit++;
	u.type = type;
	u.born = w.year;
	w.units.push_back(std::move(u));
	return w.units.back();
}

Disaster& newDisaster(World& w, DisasterType type, int cell) {
	Disaster d;
	d.id = w.nextId.disaster++;
	d.type = type;
	d.cell = cell;
	w.disasters.push_back(std::move(d));
	return w.disasters.back();
}

// ------------------------------------------------------------------ lookups

Settlement* settlement(World& w, int id) { return (id >= 0 && size_t(id) < w.settlements.size()) ? &w.settlements[size_t(id)] : nullptr; }
const Settlement* settlement(const World& w, int id) { return (id >= 0 && size_t(id) < w.settlements.size()) ? &w.settlements[size_t(id)] : nullptr; }
Culture* culture(World& w, int id) { return (id >= 0 && size_t(id) < w.cultures.size()) ? &w.cultures[size_t(id)] : nullptr; }
const Culture* culture(const World& w, int id) { return (id >= 0 && size_t(id) < w.cultures.size()) ? &w.cultures[size_t(id)] : nullptr; }

Unit* findUnit(World& w, int id) {
	for (Unit& u : w.units) if (u.id == id) return &u;
	return nullptr;
}
const Unit* findUnit(const World& w, int id) {
	for (const Unit& u : w.units) if (u.id == id) return &u;
	return nullptr;
}
Disaster* findDisaster(World& w, int id) {
	for (Disaster& d : w.disasters) if (d.id == id) return &d;
	return nullptr;
}
const Disaster* findDisaster(const World& w, int id) {
	for (const Disaster& d : w.disasters) if (d.id == id) return &d;
	return nullptr;
}

bool eraseUnit(World& w, int id) {
	for (size_t i = 0; i < w.units.size(); i++) {
		if (w.units[i].id == id) { w.units.erase(w.units.begin() + std::ptrdiff_t(i)); return true; }
	}
	return false;
}
bool eraseDisaster(World& w, int id) {
	for (size_t i = 0; i < w.disasters.size(); i++) {
		if (w.disasters[i].id == id) { w.disasters.erase(w.disasters.begin() + std::ptrdiff_t(i)); return true; }
	}
	return false;
}

} // namespace state
} // namespace dm
