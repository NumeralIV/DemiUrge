// DEMIURGE — Sim/State.h : the World (FOUNDATION section 5). Port of js/sim/state.js.
// Allocation, defaults and (de)serialization only; no simulation logic.
//
//   std::unique_ptr<World> w = state::create({ .seed = 7, .L = 6 });
//   w->elev[c], w->seaLevel, w->settlements, w->bus, w->rng, w->geo->nbr ...
//   state::isSea(w, c)  groundR(w, c)  surfaceR(w, c)  kElevMin = -12  kElevMax = 20
//   state::kCellFields (the per-cell field table as data), fieldIndex(name), cellArray(w, i)
//   state::serialize(w, bytes) / deserialize(bytes) -> versioned little-endian binary
//   state::sizeOf(w) -> bytes of serialize(w)
//   w->tune<T>() / w->scratch<T>()  per-module tuning and scratch structs held by the world
//
// The World is neither copyable nor movable (the bus and module scratch refer to it); worlds live
// behind std::unique_ptr. Everything a world needs is inside it: there are no globals.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Events.h"
#include "Core/Geo.h"
#include "Core/Rng.h"
#include "Core/Util.h"
#include "Schema.h"

namespace dm {

// ------------------------------------------------------------------ per-cell fields
// X(name, C++ element type, FieldType, default, SaveCodec, quantization min, max)
// in the order of FOUNDATION 5.1. Adding a row here adds the World member, its default, its
// table entry, its serialization and its generic accessor.
#define DM_CELL_FIELDS(X)                                   \
	X(elev,   float,   F32, 0.0,   Q16,  -12.0, 20.0)       \
	X(water,  float,   F32, 0.0,   Q16,  0.0, 8.0)          \
	X(flow,   float,   F32, 0.0,   None, 0.0, 0.0)          \
	X(down,   int32_t, I32, -1.0,  None, 0.0, 0.0)          \
	X(temp,   float,   F32, 0.0,   None, 0.0, 0.0)          \
	X(moist,  float,   F32, 0.0,   None, 0.0, 0.0)          \
	X(wet,    float,   F32, 0.0,   Q8,   0.0, 1.0)          \
	X(veg,    float,   F32, 0.0,   Q8,   0.0, 1.0)          \
	X(biome,  uint8_t, U8,  0.0,   None, 0.0, 0.0)          \
	X(fert,   float,   F32, 0.5,   Q8,   0.0, 1.5)          \
	X(ice,    float,   F32, 0.0,   Q8,   0.0, 1.0)          \
	X(fire,   float,   F32, 0.0,   Q8,   0.0, 1.0)          \
	X(scorch, float,   F32, 0.0,   Q8,   0.0, 1.0)          \
	X(lava,   float,   F32, 0.0,   Q16,  0.0, 8.0)          \
	X(fauna,  float,   F32, 0.0,   Q8,   0.0, 1.0)          \
	X(rad,    float,   F32, 0.0,   Q8,   0.0, 1.0)          \
	X(owner,  int16_t, I16, -1.0,  Raw,  0.0, 0.0)          \
	X(farm,   float,   F32, 0.0,   Q8,   0.0, 1.0)          \
	X(urban,  float,   F32, 0.0,   Q8,   0.0, 1.0)          \
	X(road,   uint8_t, U8,  0.0,   Raw,  0.0, 0.0)          \
	X(hab,    float,   F32, 0.0,   None, 0.0, 0.0)          \
	X(slope,  float,   F32, 0.0,   None, 0.0, 0.0)          \
	X(coast,  uint8_t, U8,  255.0, None, 0.0, 0.0)          \
	X(fresh,  uint8_t, U8,  255.0, None, 0.0, 0.0)          \
	X(wild,   uint8_t, U8,  0.0,   Raw,  0.0, 0.0)
// (v4 mythos, M0) wild: decades of unbroken forest cover, 0..255 (Life writes it, SPEC-MYTHOS 3.22; old growth
// at >= LifeTune::wildAncient; 255 also marks a sacred grove). Added after save version 1: fieldSince() = 2.

enum class FieldType : uint8_t { F32, U8, I16, I32 };
// None = derived (recomputed after loading, not saved); Q8 / Q16 = linear quantization of
// [min, max] to 0..255 / 0..65535; Raw = stored as is.
enum class SaveCodec : uint8_t { None, Q8, Q16, Raw };

struct CellField {
	const char* name;
	FieldType type;
	double def;
	SaveCodec save;
	double min, max;
};

// Untyped view of one cell array (generic code: serialization, hashes, map tool).
struct CellArray {
	FieldType type = FieldType::F32;
	void* data = nullptr;
	size_t size = 0;
	double get(size_t c) const;
	void set(size_t c, double v) const;
};

// ------------------------------------------------------------------ world parts

struct God {
	double awe = 0, aweMax = 100, mercy = 0, wrath = 0;
	std::string epithet;
	std::map<std::string, int> deeds;       // power id -> times used
};

struct NextId { int unit = 1, disaster = 1, entry = 1; };

struct Stats {
	double pop = 0, peakPop = 0, believers = 0;
	int wars = 0, miracles = 0;
};

struct Settings {
	double calamity = 1;                    // natural-disaster frequency multiplier (0 = off)
	Archetype archetype = Archetype::Balanced;   // (v4 mythos, M0) the kind of world (SPEC-MYTHOS 3.1); set at setup
};

// world.vars: persisted module-owned scalars (SPEC-SIM 1.1 lists the owner of each).
struct Vars {
	std::optional<double> seaBase;          // Climate.calibrate
	std::optional<double> iceRef;           // Climate.calibrate
	double lifeBoost = 1;                   // Sim.setPhase
	int64_t t0 = 0;                         // Sim.setPhase('history'): tick history began
	std::map<std::pair<int, int>, Relation> rel;   // Society (diplomacy): key = state::relKey(a, b)
	std::optional<ArkRecord> ark;           // Society
	std::optional<double> lastNuke;         // Society
	std::optional<double> snapEvery, snapNext;     // Chronicle
	NumBag num;                             // any other number, key prefixed with its module ('civ_...')
	std::map<std::string, std::string> str; // any other string, key prefixed with its module
};

// Set to true by the sim when the renderer must rebuild that data; the renderer copies and clears.
struct Dirty { bool elev = true, color = true, water = true, objects = true, rivers = true, roads = true; };
// Sim-internal "recompute soon" flags.
struct Need { bool derive = true, climate = true, hydro = true, life = true; };

// Transient caches read by more than one module (never saved; rebuilt by their owners).
struct WorldCache {
	std::vector<int32_t> landId;            // Terrain.derive: per cell, -1 sea, 0..k-1 by size (desc); empty = not built
	int landCount = 0;                      // k
	std::vector<int32_t> landHab;           // Life.habitability: habitable cells per landmass id
	std::vector<int32_t> fires;             // Life: the cells with fire > 0 (JS _fires.cells[0..n-1])
};

// ------------------------------------------------------------------ the world

struct World {
	World();
	~World();
	World(const World&) = delete;
	World& operator=(const World&) = delete;
	World(World&&) = delete;
	World& operator=(World&&) = delete;

	// identity
	int version = 1;
	uint32_t seed = 0;
	std::string name = "Unnamed";
	int L = 0, n = 0;
	std::shared_ptr<const Geo> geo;         // never serialized
	Rng rng;

	// per-cell fields, each std::vector of n elements
#define DM_CELL_MEMBER(nm, T, ft, def, sv, mn, mx) std::vector<T> nm;
	DM_CELL_FIELDS(DM_CELL_MEMBER)
#undef DM_CELL_MEMBER

	// scalars
	double year = 0;                        // years; advances only in History / Ended
	int64_t tick = 0;
	Phase phase = Phase::Molten;
	double heat = 1;                        // 1 molten .. 0 cooled
	double seaLevel = -9;
	double globalTemp = 0;
	double co2 = 0;
	double dust = 0;
	int age = 0;                            // highest Age::Id reached by any culture

	God god;
	std::vector<Settlement> settlements;    // index = id; dead ones stay
	std::vector<Culture> cultures;          // index = id
	std::vector<Unit> units;                // live units
	std::vector<Disaster> disasters;        // active disasters
	std::vector<River> rivers;              // derived
	std::vector<ChronicleEntry> chronicle;
	std::vector<Snapshot> snapshots;        // in memory only
	NextId nextId;
	Stats stats;
	Settings settings;
	Vars vars;
	std::optional<Ending> ending;
	MythosState mythos;                     // (v4 mythos, M0) SPEC-MYTHOS 1.2 (MythosTypes.h); writers per its table
	Dirty dirty;
	Need need;
	WorldCache cache;                       // transient
	EventBus bus;                           // this world's events (subscribers are not saved)

	// Per-module structs held by the world, created on first use with their default values.
	// T must be default-constructible. One instance of each type per world.
	//   tune<T>():    tuning numbers (SPEC-SIM 0.3 TUNE blocks); Balance overrides them.
	//   scratch<T>(): buffers and private transient caches (never saved; sized by this world's n).
	template <class T> T& tune() const { return slotOf<T>(tune_); }
	template <class T> T& scratch() const { return slotOf<T>(scratch_); }
	// Drop every scratch struct (after a load, or to force a rebuild). Tuning is kept.
	void clearScratch() const { scratch_.clear(); }

	bool valid(int c) const { return c >= 0 && c < n; }

private:
	struct Slot { const void* tag; std::shared_ptr<void> p; };
	template <class T> struct TypeTag { static constexpr char id = 0; };
	template <class T> static T& slotOf(std::vector<Slot>& slots) {
		const void* tag = &TypeTag<T>::id;
		for (Slot& s : slots) if (s.tag == tag) return *static_cast<T*>(s.p.get());
		auto p = std::make_shared<T>();
		T& ref = *p;
		slots.push_back(Slot{ tag, std::shared_ptr<void>(std::move(p)) });
		return ref;
	}
	mutable std::vector<Slot> tune_, scratch_;
};

namespace state {

inline constexpr double kElevMin = -12, kElevMax = 20;   // hard clamp for elev (= the q16 range)
inline constexpr char kFormat[4] = { 'D', 'M', 'W', 'D' };
// (v4 mythos, M0) version 2 = the version-1 payload followed by tagged extension blocks (State.cpp); a
// version-1 blob still loads (wild 0, the BASE fields at their defaults, an empty MythosState).
inline constexpr uint32_t kVersion = 2;

// The per-cell field table (FOUNDATION 5.1) and generic access to it.
extern const CellField kCellFields[];
extern const int kCellFieldCount;
int fieldIndex(std::string_view name);              // -1 if unknown
CellArray cellArray(World& w, int fieldIdx);        // .data == nullptr for a bad index
CellArray cellArray(const World& w, int fieldIdx);  // (data must not be written through)

struct CreateOpts {
	uint32_t seed = 0;
	std::string name = "Unnamed";
	int L = 6;                                       // clamped to 0..7
	std::shared_ptr<const Geo> geo;                  // reused as is if given (decides L and n)
};
// A new world: cell fields at their defaults, scalars / bags at the contract defaults, every
// dirty and need flag set, rng = Rng(seed), phase Molten.
std::unique_ptr<World> create(const CreateOpts& opts = {});

inline bool isSea(const World& w, int c) { return w.elev[size_t(c)] < w.seaLevel; }
inline double groundR(const World& w, int c) { return kPlanetR + w.elev[size_t(c)]; }
// Radius of what you would stand or float on: sea -> R + seaLevel; land -> R + elev + water.
inline double surfaceR(const World& w, int c) {
	const double e = w.elev[size_t(c)];
	return e < w.seaLevel ? kPlanetR + w.seaLevel : kPlanetR + e + w.water[size_t(c)];
}

// Versioned little-endian binary save of everything persisted: identity, rng state, scalars,
// persisted cell fields (quantized per kCellFields), god, entities, nextId, stats, settings,
// vars, ending. Not saved: geo, derived cell fields, dirty, need, snapshots, rivers, cache,
// Settlement::cells, tune / scratch structs, bus subscribers.
// Returns false (and leaves `out` empty, `why` set) for a world that could not be loaded back:
// a non-finite scalar or a cell array of the wrong length.
// (v4 mythos, M0) version: kVersion (the default) or 1, the LEGACY layout, byte for byte what the version-1
// build wrote: the extensions (wild, the BASE and MYTH blocks) are LEFT OUT, so version 1 is lossy unless
// extensionsDefault(w). sim::fingerprint uses it to keep the pre-mythos hashes (the phase-0 guardrail).
// Any other version is refused.
bool serialize(const World& w, std::vector<uint8_t>& out, std::string* why = nullptr, uint32_t version = kVersion);
// The inverse. nullptr ("bad save", with `why` set) on malformed, truncated, trailing or
// wrong-version input; never a half-loaded world. Derived fields come back at their defaults,
// every dirty / need flag true. Round trip: persisted cells within their quantization step,
// scalars and entities exactly, and the rng continues the same stream.
std::unique_ptr<World> deserialize(const uint8_t* data, size_t size, std::string* why = nullptr);
inline std::unique_ptr<World> deserialize(const std::vector<uint8_t>& bytes, std::string* why = nullptr) {
	return deserialize(bytes.data(), bytes.size(), why);
}
// Bytes serialize(w) produces (0 if it would refuse).
size_t sizeOf(const World& w);

// (v4 mythos, M0) The save version that introduced cell field fieldIdx: 1 for the original table, 2 for wild
// (0 for a bad index).
uint32_t fieldSince(int fieldIdx);
// (v4 mythos, M0) True when everything a version-2 save adds is at its default: every wild cell 0, every
// culture's compassion / ambition / courage 0.5 and prophet figure -1, settings.archetype Balanced, ending
// legacy None, and w.mythos equal to a default MythosState. Then serialize(w, out, why, 1) loses nothing.
bool extensionsDefault(const World& w);

// Quantization used for Q8 / Q16 fields: code = round((v - min) / (max - min) * top), clamped to
// 0..top; NaN is stored as the default's code. decode(code) = min + (max - min) * code / top.
uint32_t quantize(const CellField& f, double v);
double dequantize(const CellField& f, uint32_t code);

} // namespace state
} // namespace dm
