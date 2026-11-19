# DEMIURGE — C++ foundation contract (FOUNDATION-CPP, v1)

This file replaces `FOUNDATION.md` for the Unreal Engine 5.7 build. It is **binding**: every sim
module is written against the APIs below. `SPEC-SIM.md` stays the behavioural contract (rules,
entities, writers, events, tick schedule, balance); it is written in JavaScript terms, and section 2
of this file says how each JS construct maps to C++. If something here is impossible or wrong,
implement the closest working thing and report the deviation.

- **Part 1** (this part; key `foundation-core`): rules, conventions, Core (utilities, RNG, vectors,
  events, geometry), State and Schema (the World, entities, save format), the test harness
  (`SimTests/`), the map tool (`Tools/MapImg`).
- **Part 2** (key `foundation-api`, appended below by its engineer): the module map, module headers
  and stubs, the writers table in C++ terms, the tick order, the per-key `run.ps1` table.

Everything in part 1 is implemented, tested (`run.ps1 core`, `run.ps1 state`) and compiles inside
Unreal (`Unreal/build.ps1`).

---

## 1. Rules (binding for every file under `Unreal/Source/Demiurge/Sim/`)

1. **Standard C++20 only.** No Unreal headers (no `CoreMinimal.h`, no `UObject`, no `.generated.h`),
   **no exceptions** (no `throw` / `try` / `catch`), **no RTTI** (no `dynamic_cast` / `typeid`),
   **no global mutable state** (all state lives in the `World`; `static const` tables are fine,
   `static` scratch buffers are not), and nothing non-deterministic drives a decision: no
   `std::rand`, no `<random>` distributions, no iteration order of `unordered_map` / `unordered_set`.
   All randomness comes from the project RNG (`world.rng` or a fork of it). Iterate entities by
   ascending id; every sort comparator tie-breaks on id or cell id.
2. **Never use identifiers that collide with Unreal macros.** Inside Unreal every Sim file is
   compiled with the module's private PCH (`DemiurgePCH.h` = `CoreMinimal.h`) force-included, so these names are macros there:
   `check`, `checkf`, `verify`, `verifyf`, `ensure`, `ensureMsgf`, `unimplemented`, `TEXT`, `UE_LOG`,
   `LOCTEXT`, `PI`, `HALF_PI`, `TWO_PI`, `INV_PI`, `SMALL_NUMBER`, `KINDA_SMALL_NUMBER`, `BIG_NUMBER`,
   `EULERS_NUMBER`, `DELTA`, `INDEX_NONE`, `TCHAR`, `FORCEINLINE`, `ARRAY_COUNT`, `LIKELY`,
   `UNLIKELY`, `MAX_int32` & co, and the empty object-like macros **`IN`**, **`OUT`**, `RESTRICT`,
   `CDECL`, `STDCALL`, `VARARGS`, `MOBILE`, `CONSOLE` (probed in the real module build: `int IN = 3;`
   silently loses its name there). Never `#define` `FLT_MAX` / `DBL_MAX` / `M_PI`-like names yourself.
   The standalone build force-includes `SimTests/UeMacroGuard.h`, which turns all of these into
   compile errors (function-like ones only when followed by `(`, exactly as in Unreal), and treats
   macro redefinition (C4005) as an error. Use `dm::kPi`, `dm::kTau`, `dm::kPlanetR`.
3. **No variable shadowing** (Unreal treats C4456 / C4457 / C4458 / C4459 as errors; so does
   `run.ps1`). A local must not reuse the name of another local, a parameter, a member, or a
   namespace-scope variable.
4. **`#if` only on defined macros**: Unreal turns C4668 ("undefined macro in #if") into an error
   (so does `run.ps1`).
   Write `#if defined(X)` / `#ifdef X`.
5. **No NaN or infinity** may be written into world arrays or entities (SPEC-SIM 0.6): guard every
   division, `log`, `sqrt`, `acos`. Functions tolerate dead entities, empty lists, the poles, worlds
   with no sea or no land, any phase, and missing caches. Never fail hard on bad input: return
   `false` / `nullptr` / `-1`.
6. **Determinism**: two runs driven by the same calls produce identical worlds (`dmt::hash`).
   Real-time quantities come only from `dt` arguments, never a clock.
7. Edit only the files you own. Sim engineers build and test with `SimTests/run.ps1 <key>`; only the
   foundation and integration engineers run `Unreal/build.ps1` (Unreal's build tool, one at a time).
   Never open the Unreal editor GUI.
8. Prove it: run your checks and quote the numbers; write PNG maps (section 7) to
   `shots/ue-<yourkey>-*.png` and LOOK at them with the image-reading tool.
9. **Floating point is `/fp:precise` in both builds** (`Demiurge.Build.cs` sets `FPSemantics =
   Precise`; `run.ps1` passes `/fp:precise`), so the game follows the same IEEE semantics as the
   standalone tests and `simrun` and NaN tests work there (determinism is guaranteed within each
   build; bit-identical results ACROSS the two builds are likely but not promised: optimizer flags
   differ). (Unreal's default `/fp:fast` was measured to fold
   `x != x` to false and to change NaN comparisons; the foundation verifier switched it off.) Still
   write NaN tests as `std::isnan(x)` / `std::isfinite(x)` / `dm::isFinite(x)`, never `x != x`, and
   guard inputs before comparisons (`!(x > 0)` is a NaN test only under precise semantics). Do not
   add `#pragma float_control` or other FP-mode changes.

`run.ps1` scans `Sim/` before every build and fails on `throw` / `try` / `catch`, `dynamic_cast`,
`typeid`, `#include <random>`, `rand()`, and Unreal includes. The standalone build also uses `/GR-`
(no RTTI), like Unreal.

---

## 2. Conventions and how JavaScript maps to C++

### 2.1 Files, namespaces, includes

```
Unreal/Source/Demiurge/Sim/
  Core/Util.h/.cpp     dm::            constants, scalar helpers, hashes, noise, base64, fmt, ByteWriter/Reader
  Core/Rng.h/.cpp      dm::Rng         the deterministic RNG
  Core/Vec.h/.cpp      dm::Vec3, dm::v3::*, dm::Mat4, dm::m4::*
  Core/Events.h/.cpp   dm::Ev, dm::Ev<Name> payload structs, dm::Event, dm::EventBus, dm::kEventTable
  Core/Geo.h/.cpp      dm::Geo         the icosphere grid
  Schema.h/.cpp        enums, entity structs, tables, dm::state:: factories and lookups
  State.h/.cpp         dm::World, dm::state:: create / isSea / serialize / ... , the cell field table
  <Module>.h/.cpp      dm::<module>::  (part 2)
Unreal/SimTests/       standalone harness (section 6)
Unreal/Tools/MapImg.*  PNG maps (section 7)
```

- Everything is in namespace `dm`. Module APIs live in `dm::<module>` in lower case:
  `dm::terrain`, `dm::climate`, `dm::civ`, ... State and Schema functions are in `dm::state`.
- Include paths are relative to `Sim/`: `#include "State.h"`, `#include "Core/Geo.h"`,
  `#include "Terrain.h"`. (`Demiurge.Build.cs` adds `Sim/` as a public include path, so Unreal-side
  `Game/` code uses the same spelling.) `State.h` includes all of Core and `Schema.h`.
- Naming: types `PascalCase` (`World`, `Settlement`, `EvBattle`); functions and variables
  `camelCase`, keeping the JS names (`cellAt`, `newSettlement`, `seaLevel`); constants `kCamelCase`
  (`kPi`, `kElevMin`, `kCultureColors`); scoped-enum values `PascalCase` (`Phase::History`,
  `Cause::Drowned`); the numeric enums that mirror JS constant tables keep their JS spelling
  (`Biome::SEA`, `Building::SHRINE`, `Age::BRONZE`). Never use ALL-CAPS for your own names (they
  collide with macros).
- Tabs or spaces: follow the file you are in (the foundation uses tabs).

### 2.2 The mapping table

| JavaScript (SPEC-SIM / FOUNDATION) | C++ |
|---|---|
| `DM.X.fn(world, ...)` | `dm::x::fn(World& w, ...)` (read-only functions take `const World&`) |
| `DM.U.clamp`, `DM.U.rng(seed)`, `DM.R` | `dm::clamp`, `dm::Rng(seed)`, `dm::kPlanetR` |
| `DM.State.isSea(w, c)`, `DM.State.newSettlement(w, f)` | `dm::state::isSea(w, c)`, `dm::state::newSettlement(w, cell, culture)` then set fields |
| `world.elev[c]` (Float32Array) | `w.elev[c]` (`std::vector<float>`); element types are those of FOUNDATION 5.1: f32 → `float`, u8 → `uint8_t`, i16 → `int16_t`, i32 → `int32_t` |
| `world.geo.nbr[6*c+k]`, `geo.cellAt(x,y,z)` | `w.geo->nbr[6*c+k]`, `w.geo->cellAt(x, y, z)` |
| `DM.world` (the active world) | does not exist: every function takes the world explicitly |
| `DM.Bus.emit('battle', {...})` | `w.bus.emit(EvBattle{ cell, attacker, defender, won, settlement })` |
| `DM.Bus.on('battle', fn)` / `on('*', fn)` | `w.bus.on<EvBattle>(fn)` / `w.bus.onAll(fn)`; returns a token for `off(token)` |
| `DM.EVENTS` | `dm::kEventTable[int(Ev::X)]` = `{ name, nKeys, keys }` |
| string tags (`phase: 'history'`, `cause: 'war'`, `type: 'army'`) | enums: `Phase::History`, `Cause::War`, `UnitType::Army` (`toStr` / `fromStr` give the JS strings) |
| power ids (`'rain'`, `'meteor'`), chronicle entry types, unit death causes | `std::string` (open sets owned by Powers / Chronicle / Units) |
| `null` / `undefined` fields (`prayer`, `prophet`, `deadYear`, `vars.seaBase`, `ending`) | `std::optional<...>` |
| entity `data: {}` bags, `culture.seen`, `vars.<module>_x` | `NumBag` (= `std::map<std::string,double>`), `vars.num`, `vars.str` |
| `settlement.b.shrine` | `s.b[Building::SHRINE]` (0 or 1), `s.has(Building::SHRINE)` |
| `world.vars.rel['3|7']` | `w.vars.rel[state::relKey(3, 7)]` (`std::map<std::pair<int,int>, Relation>`) |
| transient caches `_cells`, `_n` on settlements | `s.cells` (`std::vector<int32_t>`, size = `_n`); never saved |
| world caches read by several modules: `_landId`, `_landHab`, `_fires` | `w.cache.landId`, `w.cache.landCount`, `w.cache.landHab`, `w.cache.fires` |
| a module's private caches (`_up`, `_ownDist`, `_routes`, Civ.live cache) and module-level scratch buffers "keyed by world.n" | a struct of yours held by the world: `auto& s = w.scratch<ClimateScratch>();` (created empty on first use, one per world, so it is automatically "keyed" by that world's size; never `static`) |
| `const TUNE = DM.TUNE.civ = {...}` | a struct with the spec's starting values as default member initializers, read through `w.tune<CivTune>()`; Balance changes it with `w.tune<CivTune>().x = ...` |
| `Infinity` cost / "no path" | `std::numeric_limits<double>::infinity()` for costs (never stored in the world); `std::optional` / empty vector / `nullptr` for "none" |
| `throw new Error('bad save')` | `nullptr` result + `std::string* why` out-parameter |
| tests `T.test`, `T.ok`, `T.eq`, `T.near`, `T.watchEvents()` | `DM_TEST("name") { ok(); eq(); near(); noNaN(); }`; event checking is automatic on `dmt::makeWorld` worlds (section 6) |
| `T.stub(DM.Civ, 'found', fake)` | link-time stubs: `SimTests/stubs/<Module>_stub.cpp` (part 2). Tests set world fields directly through the helper instead of faking calls |
| `tools/mapimg.js` | `dm::mapimg::*` in `Tools/MapImg.h` |

### 2.3 Entity references

Settlements and cultures live in `w.settlements` / `w.cultures` with **id == index**; dead ones stay
(`dead = true`). Units and disasters live in `w.units` / `w.disasters` (live lists; ids from
`w.nextId`; find them with `state::findUnit(w, id)`). **Never hold a `Settlement&` / `Culture&` /
`Unit&` / `Disaster&` (or pointer) across a call that may create or remove entities of that kind**:
the vector may reallocate. Hold ids. Loop with indices when the body may create entities.

---

## 3. Core API

### 3.1 `Core/Util.h` (DM.U)

```
kPlanetR = 100, kPi, kTau
clamp(x, a, b)  sat(x)  lerp(a, b, t)  smooth(t)  sstep(e0, e1, x)  remap(x, a, b, c, d)
approach(x, target, maxDelta)  damp(x, target, lambda, dt)          (damp uses std::exp: visuals only)
isFinite(x)  toInt32(x)                                              (JS ToInt32: NaN/inf -> 0, wraps)
hash32(uint32 a) -> uint32   hash2(a, b) -> uint32   hashf(a, b = 0) -> [0,1)   hashStr(string) -> uint32
noise3(x, y, z) -> [-1,1]    fbm3(x, y, z, oct, lac = 2, gain = 0.5)    ridged3(x, y, z, oct, lac = 2, gain = 0.5) -> [0,1]
b64enc(bytes, n) / b64enc(vector) -> string      b64dec(string_view, vector& out) -> bool
fmtInt(n) -> "12,345"        fmtBig(n) -> "12.3k"
ByteWriter { buf; u8 u16 u32 u64 i16 i32 i64 f32 f64 boolean str bytes }       little-endian
ByteReader(data, size) { u8 ... f64 boolean str take; ok() atEnd() remaining() }  sticky failure
```

The hashes, `noise3` / `fbm3` / `ridged3`, every `Rng` method and the helper world are **bit-identical
to the JS reference** (the tests pin the same values; the L = 6 helper biome map is pixel-identical to
the JS one). Pass ints to the hashes; they are taken modulo 2^32 (`hash32(uint32_t(-5))` is JS
`hash32(-5)`). `hashf(c, salt)` is the cheap per-cell random number that needs no RNG state.

### 3.2 `Core/Rng.h` (DM.U.rng)

```
Rng r(seed);  r.s                           whole state: one uint32 (assign to restore the stream)
r.next() -> uint32    r.f() -> [0,1)    r.range(a, b) -> [a,b)    r.integer(a, b) -> [a,b] (JS rng.int)
r.chance(p)           r.pickIndex(n) -> -1 for n <= 0             r.pick(vec) -> T* (nullptr if empty, no draw)
r.weightedIndex(n, [](int i) { return w_i; })    r.weighted(vec, [](const T& e) { return w; }) -> const T*
r.gauss()             r.shuffle(vec)            r.fork(int) / r.fork("tag") -> Rng (parent not advanced)
Rng::gaussFrom(a, b), Rng::selectWeightedWith(w, n, total, u)    the pure steps (tests)
```

`weighted`: weights <= 0 or NaN are never chosen; if nothing is positive it returns `nullptr` / -1 and
consumes no draw; otherwise exactly one draw; infinite weights win (uniformly among themselves);
`wfn` is called once per element in order and may itself call `weighted` (re-entrant, no static
buffers). `fork("chron:" + std::to_string(id))` is how prose and other side streams avoid disturbing
`w.rng` (SPEC-SIM 0.9). Only Sim code draws from `w.rng`; render / audio / UI use their own `Rng`.

### 3.3 `Core/Vec.h` (DM.V3, DM.M4)

`Vec3 { double x, y, z }` with `+ - * /`, `==`, `[i]`. Free functions (values in, value out):

```
v3::dot cross len dist addScaled lerp norm(zero -> (0,1,0)) slerp angle rotAxis basis(n, east&, north&)
Mat4 { std::array<double,16> m }  column-major, identity by default, m.at(row, col)
m4::identity mul(a, b) perspective(fovy, aspect, zNear, zFar) ortho lookAt invert(out&, a) -> bool
m4::transformPoint transformDir translation scaling rotationAxis fromBasis
```

Semantics are those of FOUNDATION 2 (slerp never NaN, fixed great circle for exact opposites; basis:
`east = normalize(cross(Y, n))`, `north = cross(n, east)`, east = +X at the poles). The Unreal client
renders with FVector / FMatrix; these exist for the sim, the tools and the tests.

### 3.4 `Core/Events.h` (DM.Bus + DM.EVENTS)

Every world owns one bus, `w.bus`; there is no global bus.

```
w.bus.emit(EvSettlementFounded{ s.id, s.cell, s.culture, FoundedBy::Settlers });   // payload fields in spec order
auto tok = w.bus.on<EvBattle>([&](const EvBattle& e) { ... });
auto tok = w.bus.on(Ev::Battle, [&](const Event& e) { const EvBattle* b = e.as<EvBattle>(); });
auto tok = w.bus.onAll([&](const Event& e) { e.type; e.name(); e.fields(f); });
w.bus.off(tok);   w.bus.clear();   w.bus.count();
kEventTable[int(Ev::Battle)] = { "battle", 5, { "cell", "attacker", "defender", "won", "settlement" } }
eventName(Ev) -> "battle"        eventFromName("battle") -> Ev::Battle (Ev::Count if unknown)
```

Semantics as DM.Bus: synchronous; true subscription order (typed and `onAll` handlers interleaved);
a handler removed during an emit is not called again in it; one added during an emit is first called
on the next; nested emits run to completion first; handlers must not throw. Payload structs
(`Ev<Name>`, 55 of them = every DM.EVENTS entry, UI events included) have exactly the keys of
SPEC-SIM 2 in order. Types of the payload fields:

| field | C++ type |
|---|---|
| `cell`, `id`, `culture`, `settlement`, `a`, `b`, `from`, `to`, `attacker`, `defender`, `victor`, `parent`, `tier`, `prev` (tier), `age` | `int` (-1 = none) |
| `ang`, `eta`, `height`, `power`, `value`, `good`, `year`, `speed`, `amount` | `double` |
| `natural`, `sea`, `start`, `won`, `brush`, `paused`, `hidden` | `bool` |
| `phase` / `prev` of `phase` | `Phase` |
| `kind` of `meteor:*` | `ImpactKind`; of `prayer*` `PrayerKind`; of `climate:milestone` `MilestoneKind`; of `miracle:witnessed` `std::string` (power id or "nuke") |
| `by`, `how`, `reason`, `cause` (settlement), `edict`, `building`, `type` (unit), `type` / `mood` (ending) | `FoundedBy`, `CaptureHow`, `BirthReason`, `Cause`, `Edict`, `Building::Id`, `UnitType`, `EndingType` / `Mood` |
| `id` of `power:used` / `awe:short`, `name`, `cause` of `unit:died`, UI strings | `std::string` |
| `ids` of `miracle:witnessed` | `std::vector<int>` (at most 16) |
| `entry` of `chronicle:entry` | `ChronicleEntry` (a copy) |

### 3.5 `Core/Geo.h` (DM.Geo)

```
std::shared_ptr<Geo> Geo::build(L)        L clamped to 0..7; about 5 ms at L = 6 (release)
Geo::prefix(k) = 10 * 4^k + 2             Geo::kMaxLevel = 7
g.L  g.n  g.nTri  g.step                  (L = 6: n = 40,962, step ~ 0.0189 rad)
g.pos (3n floats)  g.lat  g.lon (n floats)  g.nbr (6n int32, -1 pad)  g.nbrN (n uint8)  g.tris (3 nTri uint32)
g.cellAt(x, y, z, hint = -1) / cellAt(Vec3, hint)   exact nearest cell (0.05 us)
g.angle(a, b)                             great-circle angle between cells
g.within(c, ang, out) -> count            every cell with angle(c, m) <= ang, c first; out is cleared and filled
g.ring(c, steps, out) -> count            every cell within `steps` hops, BFS order, c first
g.dir(c) -> Vec3     g.basis(c, east&, north&)     g.nbrs(c) -> const int32_t*   g.degree(c)   g.valid(c)
```

Neighbour loop: `for (int k = 0; k < g.nbrN[c]; k++) { const int m = g.nbr[6 * c + k]; ... }`.
Cell numbering, neighbour order (counter-clockwise seen from outside) and the prefix property are
those of FOUNDATION 4 and identical to the JS build (pinned topology hashes). `within` / `ring` use
scratch inside the `Geo` (the `out` vector is yours, so nested calls are safe); therefore a `Geo` may
be shared by worlds on one thread only. There is no global cache (`DM.Geo.get`): each world builds
its own unless `CreateOpts::geo` passes one in (tests share via `dmt::sharedGeo(L)`).

---

## 4. State API (`State.h`)

### 4.1 Per-cell fields

`DM_CELL_FIELDS` in `State.h` is the single source of the table of FOUNDATION 5.1 (same order,
types, defaults, save codecs and ranges); adding a row there adds the `World` member, its default,
its table row, its serialization and its generic accessor.

| field | type | default | saved | field | type | default | saved |
|---|---|---|---|---|---|---|---|
| `elev` | float | 0 | q16 −12…20 | `fire` | float | 0 | q8 0…1 |
| `water` | float | 0 | q16 0…8 | `scorch` | float | 0 | q8 0…1 |
| `flow` | float | 0 | derived | `lava` | float | 0 | q16 0…8 |
| `down` | int32 | −1 | derived | `fauna` | float | 0 | q8 0…1 |
| `temp` | float | 0 | derived | `rad` | float | 0 | q8 0…1 |
| `moist` | float | 0 | derived | `owner` | int16 | −1 | raw |
| `wet` | float | 0 | q8 0…1 | `farm` | float | 0 | q8 0…1 |
| `veg` | float | 0 | q8 0…1 | `urban` | float | 0 | q8 0…1 |
| `biome` | uint8 | 0 | derived | `road` | uint8 | 0 | raw |
| `fert` | float | 0.5 | q8 0…1.5 | `hab` | float | 0 | derived |
| `ice` | float | 0 | q8 0…1 | `slope` | float | 0 | derived |
| | | | | `coast`, `fresh` | uint8 | 255 | derived |

Meanings are those of FOUNDATION 5.1. Generic access: `state::kCellFields[i]` (`CellField { name,
type, def, save, min, max }`), `state::kCellFieldCount`, `state::fieldIndex("moist")`,
`state::cellArray(w, i)` → `CellArray { type, data, size; get(c); set(c, v) }`.
A cell is **sea** iff `elev[c] < seaLevel` (`state::isSea`). `state::kElevMin = -12`, `kElevMax = 20`
(hard clamp for elev; Terrain enforces it).

### 4.2 The World

```
struct World {                                  // not copyable or movable: always std::unique_ptr<World>
  int version = 1; uint32_t seed; std::string name = "Unnamed"; int L, n;
  std::shared_ptr<const Geo> geo;   Rng rng;
  std::vector<...> elev, water, ... fresh;      // section 4.1
  double year = 0; int64_t tick = 0; Phase phase = Phase::Molten; double heat = 1, seaLevel = -9,
  globalTemp = 0, co2 = 0, dust = 0; int age = 0;
  God god;                 { awe 0, aweMax 100, mercy 0, wrath 0, epithet "", deeds map<string,int> }
  std::vector<Settlement> settlements;  std::vector<Culture> cultures;     // id == index
  std::vector<Unit> units;  std::vector<Disaster> disasters;              // live lists
  std::vector<River> rivers;                    // derived (Hydro): { cells, flow } source -> mouth
  std::vector<ChronicleEntry> chronicle;  std::vector<Snapshot> snapshots;   // snapshots: memory only
  NextId nextId { unit 1, disaster 1, entry 1 };
  Stats stats { pop, peakPop, believers (double); wars, miracles (int) };
  Settings settings { calamity 1 };
  Vars vars;                                    // below
  std::optional<Ending> ending;                 // { type, mood, culture, settlement, year }
  Dirty dirty { elev, color, water, objects, rivers, roads } = all true at creation
  Need need { derive, climate, hydro, life } = all true at creation
  WorldCache cache { landId, landCount, landHab, fires }       // transient, shared by modules
  EventBus bus;
  template <class T> T& tune() const;     template <class T> T& scratch() const;    void clearScratch() const;
  bool valid(int c) const;                      // 0 <= c < n
};
struct Vars {                                   // world.vars: owners per SPEC-SIM 1.1
  std::optional<double> seaBase, iceRef;        // Climate.calibrate ("not a finite number" = nullopt)
  double lifeBoost = 1;                         // Sim.setPhase
  int64_t t0 = 0;                               // Sim.setPhase('history')
  std::map<std::pair<int,int>, Relation> rel;   // Society (diplomacy); key state::relKey(a, b)
  std::optional<ArkRecord> ark;                 // Society { culture, settlement, year }
  std::optional<double> lastNuke;               // Society
  std::optional<double> snapEvery, snapNext;    // Chronicle
  NumBag num;  std::map<std::string, std::string> str;   // anything else, key prefixed with its module ("civ_...")
};
```

Dirty flags: the sim only ever sets them to `true`; the client copies and clears them once per frame
(SPEC-SIM 0.7). Need flags are sim-internal.

**Module structs held by the world** (no globals):

```cpp
struct LifeTune    { double regrowYears = 120; double seedCells = 8; /* spec starting values */ };
struct LifeScratch { std::vector<int32_t> queue; std::vector<float> tmp; };
void life::step(World& w, int part, int parts) {
    const LifeTune& T = w.tune<LifeTune>();          // created with the defaults on first use
    LifeScratch& S = w.scratch<LifeScratch>();       // one per world, sized by you on first use
    if (S.tmp.size() != size_t(w.n)) S.tmp.assign(size_t(w.n), 0.0f);
    ...
}
```

`tune<T>()` / `scratch<T>()` return the one `T` of that world (default-constructed on first use,
identified by type; two modules must use different struct types). Tuning is never saved (Balance
re-applies its overrides when a world is created or loaded); scratch is never saved and
`clearScratch()` drops it. A freshly loaded world has no scratch anyway, so Sim code never needs
`clearScratch()` on a live world; if a tool calls it, bus subscriptions are NOT scratch and stay: call
`w.bus.clear()` before and `sim::use(w)` after (Chronicle keeps its tokens in scratch and would
otherwise subscribe twice). Both are reachable from `const World&`. Not thread-safe:
a world is simulated on one thread.

### 4.3 Functions (`dm::state`)

```
std::unique_ptr<World> create(const CreateOpts& o = {})     CreateOpts { seed = 0, name = "Unnamed", L = 6, geo = nullptr }
bool isSea(const World&, int c)        double groundR(w, c) = kPlanetR + elev
double surfaceR(w, c)                  sea: kPlanetR + seaLevel; land: kPlanetR + elev + water
bool serialize(const World&, std::vector<uint8_t>& out, std::string* why = nullptr)
std::unique_ptr<World> deserialize(const uint8_t*, size_t, std::string* why = nullptr)   (also a vector overload)
size_t sizeOf(const World&)            bytes serialize would write (0 if it would refuse)
uint32_t quantize(const CellField&, double v)    double dequantize(const CellField&, uint32_t code)
kFormat = "DMWD", kVersion = 1
```

**Save format** (binary, little-endian on every platform; layout documented at the top of
`State.cpp`): marker, version, seed, name, L, n, `rng.s`, scalars, the persisted cell fields
(quantized per the table; each field is tagged with its name and codec, so a save that lacks a
field loads it at its default and unknown fields are skipped), god, settlements, cultures (incl. the
opaque `lang` string), units, disasters, chronicle, nextId, stats, settings, vars, ending, end
marker. Not saved: geo, derived fields, dirty, need, snapshots, rivers, `cache`,
`Settlement::cells`, tune / scratch, bus subscribers. `serialize` refuses (returns false) a world
that could not be loaded back: a non-finite world scalar or a cell array of the wrong length;
non-finite entity numbers are written as 0 (so catch them in tests: `dmt::checkFinite` / `noNaN` cover
every entity number). `deserialize` returns `nullptr` ("bad save: <reason>")
on a wrong marker / version, truncation at any byte, trailing bytes, out-of-range enums or cell ids,
non-finite numbers, ids that are not their index, or dangling cross-references (`owner[c]` outside
`-1 .. settlements.size()-1`; a settlement's `culture` / a culture's `parent`, `capital`, prophet
settlement / a unit's `culture`, `from`, `to` / relation ids / `vars.ark` / `ending` / chronicle
`culture` naming no entity; `tier` outside 0..4, `radius` outside 0..64, `w.age` outside 0..6,
`unit.i` past its path). -1 is always allowed. So in tests, create the culture before a settlement
that names it if you round-trip the world; never a half-loaded world. Round trip:
persisted cells within half a quantization step, everything else exact, the rng continues the same
stream, and re-saving a loaded world reproduces the bytes. An empty L = 6 world is 778,675 bytes
(19.0 per cell); serialize 1.3 ms, deserialize 6.5 ms. The Save module (part 2 / A15) wraps this
(slots, files, versions, checksums); it does not reimplement it.

---

## 5. Schema API (`Schema.h`)

### 5.1 Enums and tables

```
namespace Biome { enum Id : uint8_t { SEA, SHALLOWS, BEACH, DESERT, SAVANNA, GRASSLAND, FOREST, RAINFOREST,
                                      TAIGA, TUNDRA, ICE, ROCK, SNOWPEAK, WETLAND, BARREN, SEAICE, COUNT }; }
kBiomeNames[16]  ("Sea" ... "Snow peak" ... "Sea ice")
namespace Age { enum Id : int { STONE, BRONZE, IRON, SAIL, INDUSTRY, ATOMIC, SPACE, COUNT }; }    kAgeNames[7]
namespace Building { enum Id : int { SHRINE, TEMPLE, GRANARY, WALLS, PORT, MARKET, MINE, FACTORY, ACADEMY,
                                     PLANT, LAUNCH, WONDER, COUNT }; }                         kBuildingNames[12]
kCultureColors[6]   (Okabe-Ito, linear-ish RGB 0..1)          state::kVegT[3] = { 0.22, 0.5, 0.78 }
enum class Phase { Molten, Water, Life, People, History, Ended }
enum class Cause { None, Drowned, Meteor, Lava, War, Famine, Plague, Quake, Nuke, Abandoned, Wave, Lightning }
enum class UnitType { Settler, Trader, Army, Prophet }        enum class NavalMode { Land, Coastal, Open }
enum class DisasterType { None, Comet, Meteor, Volcano, Tsunami, Storm, Drought }
enum class Edict { None, Peace, Expand, Build, Crusade }      enum class PrayerKind { Rain, Heal, Peace, Bloom }
enum class FoundedBy { Awaken, Settlers }   enum class CaptureHow { War, Schism }   enum class BirthReason { Awaken, Schism, Distance }
enum class ImpactKind { Comet, Meteor }     enum class MilestoneKind { Warming, Cooling, SeaRise, SeaFall, IceAge, Thaw }
enum class EndingType { Ark, Silence }      enum class Mood { Beloved, Feared, Forgotten, Parting, Ash, Quiet }
toStr(x) -> the JS string ("history", "sea-rise", "coastal")     fromStr(text, x&) -> bool     enumCount<E>()
```

`NavalMode::Land` is JS `naval: false`, `Open` is JS `naval: true`.

### 5.2 Entities (fields and defaults exactly as SPEC-SIM 1)

```
Settlement { id, cell, name, culture, founded, dead=false, deadYear (optional), cause=Cause::None, pop=0, tier=0,
             K=0, growth=0, radius=2, b[Building::COUNT] (uint8 0/1), devotion=0.5, love=0, jaded=0, plague=0,
             immune=0, inspire=0, blessed=0, famine=0 (int), prayer (optional Prayer{kind, since}), apart=0 (int),
             capital=false, ark=0, lastExpand, cells (transient territory) ; has(Building::Id) }
Culture    { id, name, adj, plural, color {r,g,b}, lang (opaque std::string owned by Names; empty until Names fills it),
             parent=-1, born, dead=false, age=0, science=0, capital=-1, godName, epithet,
             traits {piety, aggression, curiosity, seafaring = 0.5}, prophet (optional Prophet{name, settlement, until}),
             edict=Edict::None, edictSince=-9999, pop=0, towns=0, exhaustion=0, seen (NumBag), lastSplit=-9999 }
Relation   { v=0, war=false, since=0, truce=0, trade=0, contact=false, aggressor=-1 }
Unit       { id, type, culture=-1, from=-1, to=-1, dest=-1, path (vector<int32_t>), i=0, t=0, speed=5, size=0,
             naval=NavalMode::Land, sea=false, born, data (NumBag) }
Disaster   { id, type, cell=-1, t=0, dur=0, power=1, ang=0, natural=false,
             eta, flight (comet/meteor), front (tsunami), cells (e.g. lava field), data (NumBag) }
ChronicleEntry { id, year, type (string), text, cell=-1, culture=-1, weight=1 }
Snapshot   { year, heat, seaLevel, pop, biome (n x uint8), owner (n x int8 culture id), urban (n x uint8) }
River      { cells, flow }      Ending { type, mood, culture, settlement, year }      ArkRecord { culture, settlement, year }
```

### 5.3 Factories and lookups (`dm::state`)

```
Settlement& newSettlement(World&, int cell = -1, int cultureId = -1)   id = index; founded = lastExpand = w.year
Culture&    newCulture(World&, int parent = -1)                        id = index; colour = freeColorSlot; born = w.year
Unit&       newUnit(World&, UnitType)                                  id = w.nextId.unit++; born = w.year
Disaster&   newDisaster(World&, DisasterType, int cell = -1)           id = w.nextId.disaster++
int freeColorSlot(const World&)
Settlement* settlement(w, id)   Culture* culture(w, id)   Unit* findUnit(w, id)   Disaster* findDisaster(w, id)   (nullptr if absent; const overloads)
bool eraseUnit(w, id)   bool eraseDisaster(w, id)          (order of the others kept; emit nothing)
std::pair<int,int> relKey(a, b)
```

Entities are created only through the factories; set the remaining fields on the returned reference
immediately (it stays valid until the next entity of that kind is created).

---

## 6. Test harness (`Unreal/SimTests/`)

### 6.1 Running

```
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\Relly\Documents\Fable5.2 Test\demiurge\Unreal\SimTests\run.ps1" <key> [name filter ...]
```

- `<key>` = `core`, `state`, `link`, `all`, or a module key (part 2 lists them; `path-units` builds
  Path + Units, `sim` builds Sim + Balance). Optional filters run only tests whose name contains one.
- Compiles `Sim/Core/*.cpp`, `State.cpp`, `Schema.cpp`, `Tools/MapImg.cpp`, the harness, the real
  `Sim/<Module>.cpp` of the key's module(s), `SimTests/stubs/<Module>_stub.cpp` for every **other**
  module (a module without a stub links its real file), and `SimTests/test_<key>*.cpp` plus
  `test_<module>*.cpp` for each module of the key. `all`: every real module, no stubs, every test.
- Output goes to `SimTests/build/<key>/` (parallel runs of different keys never collide); only
  changed sources (or all, if any header changed) are recompiled. A one-file rebuild takes ~1.5 s.
- Flags: `cl /std:c++20 /permissive- /W4 /we4456 /we4457 /we4458 /we4459 /we4005 /we4668 /fp:precise
  /Zc:preprocessor /O2 /GR- /EHsc /MP /utf-8 /FI UeMacroGuard.h` (the Unreal module build uses /W4,
  /we4456 /we4458 /we4459 /we4668, /fp:precise, /Zc:preprocessor, /GR- as well). Keep your files free of warnings too (the foundation has none).
- The last line is the summary, `<key> N passed, M failed` (or `<key> build failed`); exit code 0
  only when everything passed. `DM_VERBOSE=1` prints every test with its time.

### 6.2 Writing tests (`TestFramework.h`)

```cpp
#include "TestFramework.h"
#include "TestWorld.h"
#include "Terrain.h"
using namespace dm;
using namespace dmt;

DM_TEST("terrain: land fraction 36-42 % for every seed") {
    for (uint32_t seed = 1; seed <= 6; seed++) {
        auto w = state::create({ .seed = seed, .L = 6 });
        dmt::watch(*w);                                   // event checking (makeWorld does it for you)
        terrain::generate(*w);
        const double frac = /* ... */ 0.39;
        if (!near(frac, 0.39, 0.03, "seed " + std::to_string(seed))) break;
        noNaN(*w, "after generate");
    }
}
```

- `ok(cond, msg)`, `eq(a, b, msg)` (prints both values), `near(a, b, eps, msg)`, `fail(msg)`,
  `noNaN(world, msg)`: each records file:line on failure and returns bool (`if (!ok(..)) return;`).
  A test fails if any check failed; only the first 12 failures per test are printed.
- `nowMs()` for timings (report them; never assert budgets tighter than 5x the target).
- `shot("ue-<yourkey>-elev.png")` → absolute path in `<project>/shots/` (directory created).
- **Event contract**: every event emitted on a watched world is checked against `kEventTable`
  (payload of the right event, keys in order, finite numbers, `cell` in `-1..n-1`); a violation
  fails the running test. `makeWorld` worlds are watched automatically; call `dmt::watch(*w)` on
  worlds you create with `state::create`. `dmt::checkEvent(e, n)` checks one event.

### 6.3 The helper world (`TestWorld.h`, port of tests/helpers/world.js)

```
std::unique_ptr<World> makeWorld(MakeOpts o = {})    MakeOpts { L = 4, seed = 1, land = 0.4, basics = true, sea = 0,
                                                                phase = Phase::History, watch = true, geo = nullptr }
    noise terrain with exactly `land` of the cells above the sea, sea level `sea`, heat 0, vars.seaBase = sea,
    and (basics) simple stand-ins for slope, coast, temp, moist, fresh, down/flow, biome, ice, veg, fauna,
    fert, hab, and the landmass caches w.cache.landId / landCount (connected land, ids by descending
    size) and landHab (cells with hab > 0.05 per landmass); clears need.derive / climate / hydro.
    NOT the real models.
World& fillBasics(World&)      recompute the stand-ins (after you change elev or seaLevel)
int findLand(w, pred = nullptr) -> first land cell with pred(c), or -1        int bestHab(w)
std::string checkFinite(w)      "" or the first non-finite "field[c]" / scalar / entity
std::vector<Event> capture(w, fn)   events emitted on w while fn runs
uint32_t hash(w)                fingerprint of the persisted state (raw float bits + the save bytes)
void watch(w)                   std::shared_ptr<const Geo> sharedGeo(L)   (one cached Geo per level, tests only)
```

Example: `auto w = makeWorld({ .L = 5, .seed = 3, .land = 0.3 });` (designated initializers must
follow the member order above). At L = 6 the helper planet is bit-identical to the JS helper's.

### 6.4 Rules for tests

Tests may use anything (statics, iostreams, exceptions); rule 1 applies to `Sim/` only. Tests must be
deterministic, finish in a few seconds, and pass with every other module stubbed: set up world
fields directly (through the helper) instead of calling other modules. Name files
`SimTests/test_<key>*.cpp` so `run.ps1 <key>` picks them up.

---

## 7. Map tool (`Unreal/Tools/MapImg.h`, port of tools/mapimg.js)

```cpp
#include "MapImg.h"
namespace mi = dm::mapimg;
mi::elev(*w, dmt::shot("ue-terrain-elev.png"));              // hypsometric tint, sea in blues, lakes
mi::biome(*w, dmt::shot("ue-life-biome.png"));               // Biome colours, lakes
mi::field(*w, "moist", file, 0, 1);                           // any cell field by name; blue-yellow-red
mi::field(*w, "temp", file);                                  // min / max from the data
mi::field(*w, myVector, file, mn, mx, { .dimSea = true });   // any std::vector<float> / <double> of n values
mi::owner(*w, file);                                          // territories by culture colour, centres white
mi::custom(*w, file, [&](int c) { return mi::Rgb{ r, g, b }; });
```

Options (`mi::Opts`, last argument): `w = 1024, h = 512, rivers = true` (draws `w.rivers`),
`lakes = true` (land cells with `water > 0.03`), `marks = {cells}` (white), `grid = false`,
`dimSea = false`. NaN values show magenta in `field`. Equirectangular, north up, longitude −180° at
the left. Returns false if the file could not be written. Files are uncompressed PNGs (~1.5 MB at
1024x512; ~25 ms at L = 6). Write them as `shots/ue-<yourkey>-*.png` and open them with the
image-reading tool.

---

## 8. Notes for part 2 and for module engineers

- Module headers declare `namespace dm::<module> { ... }` functions taking `World&` / `const World&`,
  a `<Module>Tune` struct with the spec's starting values (read via `w.tune<...>()`), and use
  `w.scratch<...>()` for buffers. Headers may forward-declare (`struct World;`) or include
  `State.h`.
- `std::string Culture::lang` is opaque to the foundation: Names defines its encoding (any text) and
  parses it back, caching the parsed form in a scratch struct if it wants speed. Save persists it as
  is.
- `Geo` is per world; within/ring results go into your own vectors (keep them in your scratch struct
  to avoid per-call allocation in hot loops).
- Unreal's build of this module compiles every `.cpp` under `Source/Demiurge/` (no unity build; the
  private PCH `DemiurgePCH.h` (CoreMinimal) is force-included, which is why rule 2 exists; /fp:precise, rule 9). Exceptions are enabled there but banned
  here by rule; RTTI is off in both builds.

## 9. Deviations from the JavaScript foundation (decided in part 1)

- No `DM.world` active world, no global bus, no `DM.Geo.get` cache: worlds own their bus and geo.
- `State.serialize` produces a versioned binary blob instead of a JSON object with base64 cells;
  `b64enc` / `b64dec` remain available (e.g. for share codes). `sizeOf` measures that blob.
- Entity `data` bags are typed (`eta`, `flight`, `front`, `cells`) plus a numeric `NumBag`; string
  tags with closed sets are enums.
- `rng.int` is `rng.integer`; `rng.pick` / `weighted` return pointers (nullptr = JS undefined).
- `fmtBig` rounds exact binary ties half-to-even where JS `toFixed` rounds them up (e.g. 1.125k);
  non-tie values match JS exactly.
- `Geo::build` clamps L to 0..7 instead of throwing.

---

# Part 2 (key `foundation-api`): module headers, stubs, ownership, schedule

Everything below exists and is proven: `run.ps1 link` (every module a stub) and `run.ps1 all`
(every module real; at first the neutral initial files) pass, every Stage A key builds with
`run.ps1 <key>`, and `Unreal/build.ps1` compiles and links all 17 module files.

## 10. Module map

Headers are in `Unreal/Source/Demiurge/Sim/`, stubs in `Unreal/SimTests/stubs/`, tests in
`Unreal/SimTests/`. Each `.cpp` defines ONLY the functions of its half. The header's top comment
names its owner(s); for split headers every declaration is marked `[civ]` / `[economy]`,
`[society]` / `[diplomacy]`, `[powers]` / `[disasters]`.

| key | id | owned files | header | functions (namespace `dm::<ns>`) | tuning struct |
|---|---|---|---|---|---|
| `names` | A1 | `Names.h/.cpp`, `test_names.cpp`, `Names_stub.cpp` | `Names.h` | `names::` language, word, place, person, culture → `CultureNames`, god, planet, cap (no `World`: rng passed in) | `NamesTune` (constants `kNamesTune`; not balanced) |
| `terrain` | A2 | `Terrain.h/.cpp`, `test_terrain.cpp`, `Terrain_stub.cpp` | `Terrain.h` | `terrain::` generate, sculpt, crater, cone, roughen, **addElev**, touched, derive | `TerrainTune` |
| `climate` | A3 | `Climate.h/.cpp`, `test_climate.cpp`, `Climate_stub.cpp` | `Climate.h` | `climate::` update, stepIce, yearly, calibrate, datum, anomaly, emit, addDust, landIce | `ClimateTune` |
| `hydro` | A4 | `Hydro.h/.cpp`, `test_hydro.cpp`, `Hydro_stub.cpp` | `Hydro.h` | `hydro::` update, riverT, isRiver; `isLake` (inline, always real) | `HydroTune` |
| `life` | A5 | `Life.h/.cpp`, `test_life.cpp`, `Life_stub.cpp` | `Life.h` | `life::` classify, capacity, step, burn, ignite, seed, bloom, fill, habitability, rebuild, cover → `Cover` | `LifeTune` |
| `path-units` | A6 | `Path.h/.cpp`, `Units.h/.cpp`, `test_path.cpp`, `test_units.cpp`, `Path_stub.cpp`, `Units_stub.cpp` | `Path.h`, `Units.h` | `path::` find, stepCost, reach, cost; `units::` spawn, step, kill, near, cellOf, dirOf | `PathTune`, `UnitsTune` |
| `civ` | A7 | `Civ.cpp`, `test_civ.cpp`, `Civ_stub.cpp`; `[civ]` half of `Civ.h` | `Civ.h` | `civ::` found, claim, rebuild, destroy, damage, capture, setCulture, electCapital, infect, cure, at, near, live, canSettle → `SettleCheck`, siteScore, bestSite, ageProgress; `kTiers` | `CivTune` |
| `economy` | A8 | `Economy.cpp`, `test_economy.cpp`, `Economy_stub.cpp`; `[economy]` half of `Civ.h` | `Civ.h` | `civ::` yearly, capacity | `EconomyTune` |
| `society` | A9 | `Society.cpp`, `test_society.cpp`, `Society_stub.cpp`; `[society]` half of `Society.h` | `Society.h` | `society::` newCulture, yearly, witness, regard → `Regard`, raiseProphet, setEdict, aweIncome | `SocietyTune` |
| `diplomacy` | A10 | `Diplomacy.cpp`, `test_diplomacy.cpp`, `Diplomacy_stub.cpp`; `[diplomacy]` half of `Society.h` | `Society.h` | `society::` rel, peek, atWar, declareWar, makePeace, onArrive, diplomacy | `DiplomacyTune` |
| `powers` | A11 | `Powers.cpp`, `test_powers.cpp`, `Powers_stub.cpp`; `[powers]` half of `Powers.h` | `Powers.h` | `powers::` unlocked, cost, can → `CanResult`, use; table `kPowers` / `kPowerCount` / `byId` (inline data) | `PowersTune` |
| `disasters` | A12 | `Disasters.cpp`, `test_disasters.cpp`, `Disasters_stub.cpp`; `[disasters]` half of `Powers.h` | `Powers.h` | `powers::` start, update, tick, natural | `DisastersTune` |
| `chronicle` | A13 | `Chronicle.h/.cpp`, `test_chronicle.cpp`, `Chronicle_stub.cpp` | `Chronicle.h` | `chronicle::` attach, add, yearStr, epithet, updateEpithets, snapshot, tale → `Tale` | `ChronicleTune` |
| `sim` | A14 | `Sim.h/.cpp`, `Balance.h/.cpp`, `Tools/simrun`, `test_sim.cpp`, `Sim_stub.cpp`, `Balance_stub.cpp` | `Sim.h`, `Balance.h` | `sim::` create, use, rederive, tick, advance, setPhase, refreshStats, checkEnding, score, mood, `genesis::` progress / next / skip; runner hooks sample, fingerprint, firstNonFinite, isYearlyTick, runTicks; `kTick`, `kRealPerTick`, `kTicksPerYear`; `balance::` apply, set, keys | `SimTune` |
| `save` | A15 | `Save.h/.cpp`, `test_save.cpp`, `Save_stub.cpp` | `Save.h` | `save::` metaOf, encode, decode, peek, writeFile, readFile, readMeta, slotPath, writeSlot, readSlot, listSlots, removeSlot, defaultSettings, loadSettings, saveSettings | – |

133 functions in all; `SimTests/stubs/StubHooks.h` lists every one (`DM_STUBS_ALL`), and
`test_link.cpp` calls every one.

## 11. Header conventions (binding)

- **Include** the headers you call: `#include "Civ.h"`. Every module header includes `State.h`.
  Unreal-side `Game/` code should spell them `#include "Sim/Civ.h"` (unambiguous against engine
  headers of the same name; `Source/Demiurge/` is on the module's private include path, verified
  by a probe build).
- **Entity arguments are ids** (`int sid`, `int cultureId`, `int unitId`), never references:
  vectors may reallocate when another module founds a settlement, creates a culture or spawns a
  unit. Read-only queries that take an entity by `const&` (`civ::capacity(w, s)`,
  `units::cellOf(w, u)`, `units::dirOf`) do not create entities.
- **Creators return pointers** into the world list, `nullptr` = JS `null`: `civ::found` →
  `Settlement*`, `society::newCulture` → `Culture*`, `units::spawn` → `Unit*`, `powers::start` →
  `Disaster*`, `chronicle::add` → `ChronicleEntry*`, `society::raiseProphet` → `Prophet*`. Valid
  until the next entity of that kind is created: read `->id` and keep the id.
- **"Many values" returns** are id vectors in a documented order (`civ::live` ascending, `civ::near`
  nearest first, ties by id, `units::near` ascending). `path::find` returns an empty vector for
  "null" and `{from}` for from == to.
- **JS option objects** are structs with the spec defaults: `civ::FoundOpts`, `units::SpawnOpts`,
  `path::FindOpts`, `society::NewCultureOpts`, `society::WitnessOpts`, `powers::UseOpts`,
  `powers::StartOpts`, `chronicle::EntryOpts`, `sim::WorldOpts`, `sim::genesis::SkipOpts`,
  `save::EncodeOpts`. Use designated initializers or set fields: `civ::found(w, c, cu, 30, FoundedBy::Awaken)`.
- **String results** of closed sets are enums with an inline `toStr`: `civ::SettleReason`,
  `powers::Reason` / `Why`, `sim::Hint`; moods are `dm::Mood`. Open sets stay `std::string`
  (power ids, unit death causes, chronicle entry types).
- **Read-only functions take `const World&`** and may cache in `w.scratch<...>()` (reachable from a
  const world). They never write world state and never draw from `w.rng`.
- **Tuning**: one `XTune` struct per module (per half for split pairs) in `namespace dm`, holding
  the spec's starting values as default member initializers; read with `w.tune<CivTune>()`.
  Owners extend their struct (Terrain, Climate and Life port the JS TUNE blocks); nobody else
  writes it except `balance::apply` / `balance::set`.
- **Split headers**: add declarations only to your half, never rename or change the other half's.
  Put private helpers of `Civ.cpp` / `Economy.cpp` (etc.) in an anonymous namespace: the two files
  are linked together, so two non-static helpers with the same name are a link error.
- **Adding a public function** (allowed, never renaming): declare it in your header, add its row
  to the right `DM_STUBS_<MODULE>` list in `StubHooks.h` and its `DM_STUB(...)` body to your stub,
  and make sure `test_link.cpp` still passes (it counts the rows: update the expected count of 133
  in its first test). No overloads for module functions (the hook table takes `&dm::ns::fn`): use
  default arguments.
- Every function tolerates bad ids and cells (returns `false` / `nullptr` / `-1` / `0` / empty),
  dead entities, empty worlds, any phase; never writes NaN.

## 12. Stubs and hooks (`SimTests/stubs/`, the C++ `T.stub`)

`run.ps1 <key>` links the real `.cpp` of the key's modules and `stubs/<Module>_stub.cpp` for every
other module. A stub function (1) records its call when recording is on, (2) returns the result of
a hook if the test installed one, (3) otherwise is **neutral**:

| module | neutral behaviour | honest pure reads (real answers from world data) |
|---|---|---|
| names | placeholder names ("stub" language, "Place", "Person", "Folk", "Maker", "World"); no rng draws | `cap` |
| terrain | every op a no-op, `sculpt` → 0, elev untouched | `addElev` returns the unchanged elev |
| climate | update / stepIce / yearly / calibrate / emit / addDust no-ops | `datum`, `anomaly`, `landIce` |
| hydro | update no-op, `riverT` = 1e30, `isRiver` false | (`isLake` is inline) |
| life | nothing grows / burns / is classified, `capacity` 0, `ignite` false, `cover` zeros | – |
| path | nothing reachable: `find` empty (cost = `kNoPath`), `stepCost` / `cost` = `kNoPath`, `reach` visits 0 | – |
| units | `spawn` nullptr, `step` / `kill` nothing | `near`, `cellOf`, `dirOf` (cell direction, no interpolation) |
| civ | `found` nullptr, mutations no-ops, `damage` 0, `electCapital` -1, `infect` / `cure` false, `canSettle` {false, Sea}, `siteScore` 0, `bestSite` -1, `ageProgress` 0 | `live`, `at`, `near` |
| economy | `yearly` nothing, `capacity` 0 | – |
| society | `newCulture` nullptr, `yearly` nothing, `witness` 0, `regard` zeros / Forgotten, `raiseProphet` nullptr, `setEdict` false, `aweIncome` 0 | – |
| diplomacy | `declareWar` / `makePeace` / `onArrive` / `diplomacy` nothing | `rel` (creates on demand), `peek`, `atWar` |
| powers | `unlocked` false, `cost` 0, `can` {false, Locked}, `use` false | (table `kPowers` / `byId` inline) |
| disasters | `start` nullptr; `update` / `tick` / `natural` nothing | – |
| chronicle | `attach` subscribes nothing, `add` nullptr, `epithet` "", `snapshot` false, `tale` empty | `yearStr` |
| sim | `create` = bare `state::create` world (no terrain, never nullptr); time never passes; `setPhase` false (phase never written); `checkEnding` nullopt; `genesis` progress = phase only, next / skip false; `fingerprint` 0; `firstNonFinite` ""; `runTicks` 0 | `mood`, `sample` |
| balance | no overrides, `set` false, `keys` empty | – |
| save | no file touched; false / nullptr / nullopt (why "save: stub") | `metaOf`, `slotPath`, `defaultSettings`, `loadSettings` (= defaults) |

So in your own tests, set up the world directly (the helper world, `state::newSettlement`,
`state::newCulture`, `w.owner[c] = sid`, `society::rel(w, a, b)->war = true`, ...) and read the
other modules' state through the honest reads. Where a rule needs a collaborator to *act* (A8's
"stub Units.spawn to teleport", A14's "call-recording stubs"), install a **hook**:

```cpp
#include "stubs/StubHooks.h"
DM_TEST("economy: expansion sends settlers through units::spawn") {
    dmt::stub::Guard guard(true);                  // resets hooks + log now and at exit; true = record calls
    if (!dmt::stub::stubbed("units")) return;      // `run.ps1 all` links the real Units: hooks are ignored
    dmt::stub::hooks().units_spawn = [](World& w, const units::SpawnOpts& o) -> Unit* { ... };
    ...
    ok(dmt::stub::called("units_spawn") >= 1);     // the log holds hook names: "units_spawn", "civ_found", "sim_genesis_skip"
}
```

A hook has exactly the function's signature (every argument, defaults included). Hooks and the
log are process-wide test state: always use a `Guard`. In `run.ps1 all` nothing is stubbed: guard
hook-dependent tests with `stubbed(...)` (or make them hold for the real modules too).
`dmt::stub::calls()` is the ordered call log (the sim engineer's schedule checks read it).

The initial `Sim/<Module>.cpp` files contain the same neutral code without the hooks, so the full
Unreal build links before any module exists. Module engineers replace them entirely; keep your stub
matching your header.

## 13. Writers (SPEC-SIM 1.1 in C++ terms; binding)

| state | written by (C++) |
|---|---|
| `w.elev` | `terrain::*` only (Powers / Disasters call `terrain::sculpt`, `crater`, `cone`, `roughen`, `addElev`) |
| `w.slope`, `w.coast`, `w.cache.landId`, `w.cache.landCount` | `terrain::derive` |
| `w.temp`, `w.moist`, `w.ice`, `w.co2`, `w.dust`, `w.globalTemp`, climate scratch (upwind cache) | `climate::*` (`emit` / `addDust` are the entry points for other modules) |
| `w.seaLevel` | `powers::update` (comet impacts) and `sim::genesis::skip` before History; `climate::yearly` in History / Ended |
| `w.water`, `w.flow`, `w.down`, `w.fresh`, `w.rivers` | `hydro::update` |
| `w.veg`, `w.fauna`, `w.fire`, `w.scorch`, `w.biome`, `w.hab`, `w.cache.fires`, `w.cache.landHab` | `life::*` (Civ lowers `veg` on new farmland) |
| `w.wet` | Powers raises (rain, storms, tsunami); `life::step` decays |
| `w.fert` | `terrain::generate`; `life::step` / `bloom`; Powers / Disasters (ash, floods) |
| `w.lava`, `w.disasters` | Disasters (`powers::start` / `update` / `tick`; created only via `state::newDisaster` in `start`) |
| `w.rad` | Diplomacy raises (nukes); `life::step` decays 1.5 %/yr |
| `w.owner`, `w.farm`, `w.urban` | Civ / Economy (`life::step` decays farm / urban where `owner == -1`) |
| `w.road` | Diplomacy (trade) |
| `Settlement`: pop, K, growth, tier, radius, famine, plague (only `civ::infect` / `cure`), immune, lastExpand, `b[]` except `WONDER`, culture, capital, dead, deadYear, cause, cells | Civ / Economy. `inspire` / `blessed`: Powers sets them, `civ::yearly` counts them down |
| `Settlement`: devotion, love, jaded, prayer, apart, ark, `b[Building::WONDER]` | Society (belief half; prophet visits in `onArrive` raise devotion) |
| `Culture`: age, science, capital; `w.age` | Civ / Economy |
| `Culture`: pop, towns, dead, prophet, edict, edictSince, exhaustion, seen, traits, lastSplit | Society / Diplomacy (exhaustion: diplomacy) |
| `Culture`: epithet, godName changes; `w.god.epithet` | `chronicle::updateEpithets` |
| `w.settlements` (create) / `w.cultures` (create) / `w.units` | `civ::found` / `society::newCulture` / `units::spawn`, `units::kill`, arrivals |
| `w.god.awe` | Society adds income; `powers::use` spends; awaken sets the starting 60 |
| `w.god.aweMax` | `society::yearly` |
| `w.god.deeds`, `w.stats.miracles` | `powers::use` (once per power:used) |
| `w.god.mercy`, `w.god.wrath` | `society::witness` |
| `w.stats.wars` | `society::declareWar` |
| `w.stats.pop`, `peakPop`, `believers` | `sim::refreshStats` |
| `w.phase` | `sim::setPhase` ONLY; `w.year`, `w.tick`, `w.heat`, `w.ending`: Sim |
| `w.chronicle`, `w.snapshots`, `w.nextId.entry` | Chronicle |
| `w.vars.seaBase`, `vars.iceRef` | `climate::calibrate` |
| `w.vars.lifeBoost`, `vars.t0` | `sim::setPhase` |
| `w.vars.rel` | Diplomacy (`society::rel`); `w.vars.ark`, `vars.lastNuke`: Society / Diplomacy |
| `w.vars.snapEvery`, `vars.snapNext` | Chronicle |
| `w.vars.num` / `vars.str` | any module, keys prefixed with its module name (`"civ_..."`, `"powers_..."`) |
| `w.dirty.*` | any module, only ever to `true` (the client clears them) |
| `w.need.*` | set by whoever invalidates (Terrain `touched`, Climate, Powers rain / droughts); cleared by the module that recomputes |
| tuning `w.tune<XTune>()` | its module (defaults) and `balance::*` |
| `w.scratch<T>()` | the module that owns `T` (one private struct type per module) |

## 14. How modules call each other

- **Direct calls through the headers**, at any time: `civ::found(w, ...)`, `climate::addDust(w, 0.1)`.
  There is no "module missing" case in C++: in a standalone test the others are linked as neutral
  stubs (section 12), so call freely and write your tests against world state you set up yourself.
- **Every call names its world.** There is no active world: `sim::use`, `sim::tick`,
  `powers::use` do not set any global. Bus handlers receive events of the world they subscribed to.
- **Events** are emitted by the owners listed in SPEC-SIM 2 (`w.bus.emit(EvBattle{...})`). Only
  Chronicle subscribes inside the sim (`chronicle::attach`); everything else is called directly.
- **Call graph** (who calls whom; every edge is a header call):
  - Sim → Terrain (generate, derive), Climate (update, stepIce, yearly, calibrate), Hydro (update),
    Life (classify, habitability, step, burn, fill, rebuild, cover), Units (step), Civ (yearly,
    rebuild, bestSite, found), Society (yearly, regard, newCulture), Powers (update, tick, natural),
    Chronicle (attach, snapshot), Balance (apply), Names (planet).
  - Powers → Terrain, Life, Civ, Society (witness, raiseProphet, makePeace, newCulture), Units (near,
    kill), Climate (addDust), Sim (setPhase on the first awaken), Disasters (start).
  - Disasters → Terrain (crater, addElev, touched), Life (ignite), Civ (damage, destroy, infect,
    near), Units (near, kill), Climate (addDust), Society (witness).
  - Economy → Civ lifecycle, Path (reach, find), Units (spawn), Climate (emit), Life (capacity).
  - Civ lifecycle → Economy (capacity).
  - Society → Names, Civ (setCulture, live, near), Units (spawn for prophets, kill), Chronicle
    (updateEpithets), Diplomacy (diplomacy). Diplomacy → Units (spawn), Civ (capture, destroy,
    damage, infect), Life (ignite), Climate (addDust), Society (witness), Path (find).
  - Units → Path (find, stepCost), Civ (found, near), Society (onArrive, atWar).
  - Chronicle → Society (regard), Civ (live).
- **Arrival contract** (Units.h): unit:arrived, then `civ::found` (settler) or `society::onArrive`
  (trader / army / prophet, unit still in `w.units`, may `units::kill` it), then Units erases the
  unit if still present, without unit:died.
- **People removed only after success**: a caller removes people from the source settlement only
  after `units::spawn` returned non-null.
- **Witness**: `society::witness(w, cell, ang, { kind, faith, good, harmed, goodHarmed })`.
  "−0.8 damaged, −0.2 others" = `goodHarmed = -0.8, good = -0.2, harmed = {ids hurt}`. Society
  applies "half faith to unharmed witnesses when good ≤ 0"; natural events pass faith / 4 and good 0.

## 15. Tick order (`sim::tick`, binding; SPEC-SIM 3.12)

```
Molten:              heat -= 1/60; at 0 -> setPhase(Water). Nothing else.
Water, Life, People: powers::tick -> life::burn -> life::step(tick % P, P)   (P = 2 in Life, else 8)
                     -> climate::stepIce if tick % 20 == 10 -> one heavy pass
History, Ended:      year += 0.25 -> powers::tick -> life::burn -> units::step(0.25) -> life::step(tick % 8, 8)
                     -> if (tick - vars.t0) % 4 == 0: YEARLY GROUP = civ::yearly, society::yearly
                        (which ends with society::diplomacy and chronicle::updateEpithets), climate::yearly,
                        powers::natural, sim::refreshStats, chronicle::snapshot, sim::checkEnding
                     -> climate::stepIce if tick % 20 == 10
                     -> on a tick that is NOT yearly: one heavy pass
Heavy pass (at most one per tick, the first due):
  1 need.derive -> terrain::derive
  2 (need.climate and >= 8 ticks since the last) or >= 40 ticks since the last -> climate::update
  3 need.hydro and >= 8 ticks since the last -> hydro::update
  4 need.life -> life::classify this tick, life::habitability the next
sim::advance(w, n): n x { powers::update(w, 0.125); sim::tick(w); }   (headless code only advances this way)
sim::rederive: terrain::derive -> climate::update -> hydro::update -> life::classify -> life::habitability
               -> life::rebuild -> civ::rebuild   (climate::calibrate first if history began and seaBase is unset)
sim::use (after create or load): balance::apply, chronicle::attach, rederive
```

## 16. `run.ps1 <key>`

`powershell -NoProfile -ExecutionPolicy Bypass -File "<root>\Unreal\SimTests\run.ps1" <key> [filters]`

| key | real modules | stubs | tests compiled |
|---|---|---|---|
| `core` | – | all 17 | `test_core*.cpp` |
| `state` | – | all 17 | `test_state*.cpp` |
| `link` | – | all 17 | `test_link*.cpp` |
| `names` | Names | 16 others | `test_names*.cpp` |
| `terrain` | Terrain | 16 others | `test_terrain*.cpp` |
| `climate` | Climate | 16 others | `test_climate*.cpp` |
| `hydro` | Hydro | 16 others | `test_hydro*.cpp` |
| `life` | Life | 16 others | `test_life*.cpp` |
| `path-units` | Path, Units | 15 others | `test_path-units*.cpp`, `test_path*.cpp`, `test_units*.cpp` |
| `civ` | Civ (lifecycle) | Economy and 15 others | `test_civ*.cpp` |
| `economy` | Economy | Civ and 15 others | `test_economy*.cpp` |
| `society` | Society (belief) | Diplomacy and 15 others | `test_society*.cpp` |
| `diplomacy` | Diplomacy | Society and 15 others | `test_diplomacy*.cpp` |
| `powers` | Powers | Disasters and 15 others | `test_powers*.cpp` |
| `disasters` | Disasters | Powers and 15 others | `test_disasters*.cpp` |
| `chronicle` | Chronicle | 16 others | `test_chronicle*.cpp` |
| `sim` | Sim, Balance | 15 others | `test_sim*.cpp`, `test_balance*.cpp` |
| `save` | Save | 16 others | `test_save*.cpp` |
| `foundation` | – | all 17 | `test_foundation*.cpp` (the verifier's adversarial tests) |
| `all` | all 17 | none | every `test_*.cpp` |

Note for split pairs: `run.ps1 civ` tests the lifecycle half against the economy STUB (capacity 0),
and vice versa. Each half's tests must pass that way; the pair is tested together in `all`.
Until a module's test file exists its key prints `warning: no test files` and `0 passed` (exit 0).

## 17. Decisions and deviations of part 2

- **Ids instead of entity references** for every function that takes an existing entity
  (`civ::destroy(w, sid, cause)`, `units::kill(w, unitId, cause)`, `society::onArrive(w, unitId)`).
- **Additions** (C++ needs, all marked in the headers): `terrain::addElev` (lava cools into rock
  "via Terrain"); `powers::cost(..., cell)` (the × 4 owned-land sculpt cost needs the brush cell);
  `society::WitnessOpts::harmed / goodHarmed` (per-settlement good of the power table);
  `path::cost`, `units::cellOf`, `climate::landIce`, `hydro::isRiver`, `hydro::isLake` (JS public
  helpers); `sim::mood` (JS `Sim.mood`); the `sim::` runner hooks; `balance::set` / `keys`; the save
  container, slots and settings API.
- **Power table as inline data** in `Powers.h` (`kPowers`, 16 rows of SPEC-SIM 3.10), so the stub
  and the real module share it; special cases (awaken, free genesis sculpting, `2 × ang` witness
  radius, "both cultures") are flags plus logic in `unlocked` / `cost` / `use`.
- **Names takes no world**; its tuning is constant (`kNamesTune`).
- **`path::find` "null" is an empty vector**; costs use `path::kNoPath` (infinity, never stored).
- **Honest stub reads** (section 12) instead of fully neutral ones where other modules' tests read
  world data through them; **stub hooks** (`StubHooks.h`) provide what JS `T.stub` did. This extends
  part 1's "tests set world fields directly": prefer that; hook only when a collaborator must act.
- **Part 1 was not changed** by part 2 (the verifier's changes are in section 18).

## 18. Foundation verification (key `foundation`, 2026-11-19)

Adversarial tests: `SimTests/test_foundation_adv.cpp` (`run.ps1 foundation`; also in `all`). What
changed, so nobody is surprised:

- **Unreal FP mode**: `Demiurge.Build.cs` now sets `FPSemantics = Precise` with a private PCH
  `Source/Demiurge/DemiurgePCH.h` (CoreMinimal only) and adds the module root to the private include
  path (`#include "Sim/Civ.h"` from `Game/` works). Unreal's default `/fp:fast` silently broke NaN
  guards (rule 9).
- **`run.ps1`** adds `/we4668 /fp:precise /Zc:preprocessor`, matching the Unreal compile flags.
- **`state::deserialize`** rejects dangling cross-references (section 4.3); `quantize` / `fmtBig`
  no longer test NaN with `x != x`.
- **Test helper**: `fillBasics` also fills `w.cache.landId` / `landCount` / `landHab`;
  `checkFinite` / `noNaN` now cover every entity number (units, disasters, relations, vars, bags).
- `test_state.cpp` round-trip fixtures now name existing settlements / cultures.
- Do not add new ALL-CAPS identifiers: the few private ALL-CAPS constants inside the foundation's
  `.cpp` files are known to compile in Unreal; new ones may not.
