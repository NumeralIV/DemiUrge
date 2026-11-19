# DEMIURGE — Foundation contract (v1)

Every other module is written against the APIs in this file. They are **binding**: implement them
exactly (names, argument order, return values, array layouts). If something here is impossible or
wrong, implement the closest working thing and report the deviation in your final answer.

Read `docs/CONCEPT.md` first for what the game is.

## 0. Conventions

- Project root: `demiurge/`. Plain classic `<script>` files. No bundler, no npm packages, no ES
  modules, no `import`/`export`, no network. The game must run from `file://`.
- Every source file is wrapped like this and attaches to the single global namespace `DM`:

  ```js
  (function (DM) {
    'use strict';
    // ...
  })(globalThis.DM = globalThis.DM || {});
  ```

- **Core and sim files (`js/core/*`, `js/sim/*`) must run unchanged in Node.** They must not touch
  `window`, `document`, the DOM, WebGL, `performance`, `Date`, `Math.random`, timers, or
  `console.log` in normal operation. `tools/load.js` loads them into Node.
- **Determinism**: all simulation randomness goes through `world.rng` (or an `Rng` forked from it),
  and only `js/sim/*` may draw from it (render, audio, UI and effects use their own generators).
  Two headless runs driven by the same sequence of calls produce identical worlds
  (SPEC-SIM §0.9).
- **No work at load time.** Every module looks other modules up as `DM.X.fn(...)` at call time
  and does nothing when its file loads except define itself: no DOM or GL calls, no Bus
  subscriptions (those happen in `init` / `attach`). `DM.X === undefined` means "not built yet".
- Coordinates: right-handed, planet centre at the origin, **+Y is the north pole**.
  `DM.R = 100` is the datum radius in world units. A "direction" is a unit vector from the centre.
- A **cell** is one vertex of the icosphere. Cells are addressed by integer id `0 … n-1`.
- Hot loops: typed arrays, no per-tick allocation, no closures created per cell.
- Style: 2-space indent, semicolons, `const`/`let`, short doc comment at the top of each file
  listing what it exposes.
- Tests live in `tests/<module>.test.js` and run with `node tools/test.js` (all) or
  `node tools/test.js <substring>` (matching files). See §7.

## 1. `js/core/util.js` → `DM.U`, `DM.R`

```
DM.R = 100
DM.U.TAU                          2π
DM.U.clamp(x, a, b)
DM.U.sat(x)                       clamp to 0..1
DM.U.lerp(a, b, t)
DM.U.smooth(t)                    smoothstep on a 0..1 input (clamped)
DM.U.sstep(e0, e1, x)             GLSL smoothstep
DM.U.remap(x, a, b, c, d)         map a..b → c..d, clamped to c..d
DM.U.approach(x, target, maxDelta)
DM.U.damp(x, target, lambda, dt)  frame-rate independent exponential approach
DM.U.hash32(a)        → uint32    integer hash of an int
DM.U.hash2(a, b)      → uint32    hash of two ints
DM.U.hashf(a, b = 0)  → [0,1)     float hash of two ints
DM.U.rng(seed)        → Rng       seed: any int (coerced with >>> 0; 0 is valid)
DM.U.noise3(x, y, z)  → [-1,1]    smooth gradient/simplex noise, seedless (offset the inputs to vary)
DM.U.fbm3(x, y, z, oct, lac = 2, gain = 0.5)      → about [-1,1]
DM.U.ridged3(x, y, z, oct, lac = 2, gain = 0.5)   → [0,1], sharp ridges near 1
DM.U.b64enc(Uint8Array) → string   standard base64; must not rely on Buffer/atob/btoa
DM.U.b64dec(string)     → Uint8Array
DM.U.fmtInt(n)          → "12,345"  (rounds; handles negatives)
DM.U.fmtBig(n)          → "950", "12.3k", "4.56M", "1.2B"
```

`Rng` (single uint32 of state so it serializes trivially; e.g. mulberry32):

```
rng.s                      current uint32 state (read/write; assigning restores the stream)
rng.next()   → uint32
rng.f()      → [0,1)
rng.range(a, b)            float in [a,b)
rng.int(a, b)              integer in [a,b] inclusive
rng.chance(p)              true with probability p
rng.pick(arr)              uniform element (undefined for empty)
rng.weighted(arr, wfn)     element chosen with weight wfn(el) ≥ 0 (undefined if all weights are 0)
rng.gauss()                standard normal
rng.shuffle(arr)           in place, returns arr
rng.fork(tag)  → Rng       independent stream derived from (state, tag); tag: int or string;
                           does NOT advance the parent
```

## 2. `js/core/vec.js` → `DM.V3`, `DM.M4`

Vectors are any array-like of length ≥ 3 (plain arrays or Float32Array). Output parameter first.
Functions return the output (or a number). Outputs may alias inputs.

```
V3.create(x = 0, y = 0, z = 0) → Float32Array(3)
V3.set(o, x, y, z)   V3.copy(o, a)   V3.add(o, a, b)   V3.sub(o, a, b)   V3.scale(o, a, s)
V3.addScaled(o, a, b, s)      o = a + b*s
V3.dot(a, b)   V3.cross(o, a, b)   V3.len(a)   V3.dist(a, b)
V3.norm(o, a)                 normalize (zero vector → (0,1,0))
V3.lerp(o, a, b, t)
V3.slerp(o, a, b, t)          great-circle interpolation of unit vectors (falls back to nlerp when nearly parallel)
V3.angle(a, b)                angle in radians between unit vectors (numerically safe)
V3.rotAxis(o, a, axis, ang)   Rodrigues rotation of a about unit axis
V3.basis(n, outEast, outNorth)  local tangent frame at unit direction n: east = normalize(cross(Y, n)),
                                north = cross(n, east); well-defined at the poles (pick east = +X there)
```

Matrices are column-major `Float32Array(16)`, directly usable with `gl.uniformMatrix4fv(loc, false, m)`.
Clip space is WebGL's (z in −1..1).

```
M4.create() → identity        M4.identity(o)      M4.copy(o, a)
M4.mul(o, a, b)               o = a · b (apply b first); o may alias a or b
M4.perspective(o, fovyRad, aspect, near, far)
M4.ortho(o, left, right, bottom, top, near, far)
M4.lookAt(o, eye, target, up)
M4.invert(o, a) → bool        false if singular (o untouched)
M4.transformPoint(o, m, v)    v as point (w = 1), divides by w
M4.transformDir(o, m, v)      v as direction (w = 0), no divide
M4.translation(o, x, y, z)    M4.scaling(o, x, y, z)    M4.rotationAxis(o, unitAxis, ang)
M4.fromBasis(o, xAxis, yAxis, zAxis, origin)   columns = the three axes and the origin
```

## 3. `js/core/events.js` → `DM.Bus`

Synchronous publish/subscribe used to decouple the sim from rendering, audio, chronicle and UI.

```
DM.Bus.on(type, fn) → unsubscribe function      fn(payload, type)
DM.Bus.on('*', fn)                              receives every event: fn(payload, type)
DM.Bus.off(type, fn)
DM.Bus.emit(type, payload)                      handlers run immediately, in subscription order;
                                                an exception in one handler is caught, reported with
                                                console.error, and does not stop the others or the emitter
DM.Bus.clear()                                  remove all handlers (tests)
```

## 4. `js/core/geo.js` → `DM.Geo`

`DM.Geo.build(L)` returns the icosphere for subdivision level `L` (0 … 7). It is expensive; callers
build once and share. `DM.Geo.get(L)` returns a cached instance (builds on first use).

Construction rules (other code relies on these):

- Level 0 is an icosahedron oriented with **vertex 0 at the north pole (0,1,0)** and
  **vertex 11 at the south pole (0,−1,0)**; vertices 1–5 form the upper ring (latitude
  atan(1/2), longitudes 0°, 72°, …), vertices 6–10 the lower ring (latitude −atan(1/2),
  longitudes 36°, 108°, …).
- Each subdivision splits every triangle into 4, creating one new vertex per edge (normalized
  midpoint). New vertices are **appended after all existing ones**, so the cells of level `k` are
  exactly the first `10·4^k + 2` cells of every finer level (the *prefix property*).
- Triangles are counter-clockwise when viewed from outside the sphere.

```
geo.L, geo.n (= 10·4^L + 2), geo.nTri (= 20·4^L)
geo.pos    Float32Array(3n)   unit direction of each cell (x,y,z)
geo.lat    Float32Array(n)    asin(y), radians, north positive
geo.lon    Float32Array(n)    atan2(z, x), radians
geo.nbr    Int32Array(6n)     neighbours of cell c are nbr[6c … 6c+nbrN[c]-1], ordered
                              counter-clockwise seen from outside; unused 6th slot of the 12
                              pentagon cells (ids 0–11) is −1
geo.nbrN   Uint8Array(n)      5 or 6
geo.tris   Uint32Array(3·nTri)
geo.step   number             mean angular distance between neighbouring cells (radians)
geo.prefix(k) → 10·4^k + 2

geo.cellAt(x, y, z, hint = -1) → id of the cell whose direction is nearest to (x,y,z)
                              (input need not be normalized; must be exact, i.e. the true
                              nearest cell; use a coarse lookup or the hint, then greedy
                              neighbour descent on the dot product)
geo.angle(a, b) → great-circle angle in radians between cells a and b
geo.within(c, ang, out = []) → count
                              fills out[0 … count-1] with all cells whose direction is within
                              angular radius `ang` of cell c's direction (c itself first).
                              `out` may be an Array or Int32Array; the caller sizes typed arrays.
                              Re-entrant: safe to call from inside a loop over a previous result.
geo.ring(c, steps, out = []) → count
                              all cells within `steps` neighbour hops of c (BFS order, c first)
geo.dir(c, out) → out         copies the unit direction of cell c
geo.basis(c, outEast, outNorth)   local tangent frame at cell c (see V3.basis)
```

Performance targets at L = 6: `build` < 400 ms, `cellAt` < 2 µs with a good hint and < 10 µs
without, `within` linear in the result size.

## 5. `js/sim/state.js` → `DM.State`, `DM.BIOME`, `DM.AGES`

The whole simulation is one plain object, the **world**. Per-cell data are typed arrays of length
`n` hanging directly off the world (`world.elev[c]`). `state.js` owns allocation, defaults and
(de)serialization only; it contains no simulation logic.

### 5.1 Per-cell fields

| field   | type | default | persisted | meaning |
|---------|------|---------|-----------|---------|
| `elev`  | f32  | 0    | yes (q16, −12…20) | terrain height in world units relative to the datum radius. Typical −6 (ocean floor) … +9 (peaks). Rendered radius = `DM.R + elev`. |
| `water` | f32  | 0    | yes (q16, 0…8)  | depth of standing fresh water (lakes) on a land cell; lake surface = `elev + water` |
| `flow`  | f32  | 0    | derived | river discharge through the cell (accumulated rainfall) |
| `down`  | i32  | −1   | derived | downstream neighbour cell id, −1 for sea cells and sinks |
| `temp`  | f32  | 0    | derived | annual mean temperature, °C |
| `moist` | f32  | 0    | derived | annual precipitation, normalized 0 (arid) … 1 (very wet) |
| `wet`   | f32  | 0    | yes (q8, 0…1) | transient extra moisture from rain miracles / floods; decays |
| `veg`   | f32  | 0    | yes (q8, 0…1) | vegetation density |
| `biome` | u8   | 0    | derived | `DM.BIOME` id |
| `fert`  | f32  | 0.5  | yes (q8, 0…1.5) | soil fertility |
| `ice`   | f32  | 0    | yes (q8, 0…1) | permanent ice / snow cover on land, or sea-ice cover on sea |
| `fire`  | f32  | 0    | yes (q8, 0…1) | burning intensity |
| `scorch`| f32  | 0    | yes (q8, 0…1) | burnt / ash-covered look; decays |
| `lava`  | f32  | 0    | yes (q16, 0…8) | molten lava thickness |
| `fauna` | f32  | 0    | yes (q8, 0…1) | wildlife density (fish on sea cells) |
| `rad`   | f32  | 0    | yes (q8, 0…1) | radioactive fallout |
| `owner` | i16  | −1   | yes (raw) | id of the settlement whose territory includes the cell, or −1 |
| `farm`  | f32  | 0    | yes (q8, 0…1) | cultivated fraction |
| `urban` | f32  | 0    | yes (q8, 0…1) | built-up fraction |
| `road`  | u8   | 0    | yes (raw) | road level 0…3 |
| `hab`   | f32  | 0    | derived | habitability score for settling, 0…1 |
| `slope` | f32  | 0    | derived | largest absolute height difference to a neighbour (world units) |
| `coast` | u8   | 255  | derived | neighbour hops to the nearest sea cell (0 for sea cells), capped at 255 |
| `fresh` | u8   | 255  | derived | neighbour hops to the nearest fresh water (river or lake), capped at 255 |

A cell is **sea** iff `elev[c] < world.seaLevel`; otherwise it is **land**. Helper:
`DM.State.isSea(world, c)`.

"Derived" fields are not saved; after loading they are recomputed by the sim
(`DM.Sim.rederive(world)`, specified elsewhere). Quantization `q8`/`q16` = linear map of the listed
range to an unsigned 8/16-bit integer; `raw` = stored as is.

`DM.State.CELL_FIELDS` is the table above as data:
`[{ name, type: 'f32'|'u8'|'i16'|'i32', def, save: 'q8'|'q16'|'raw'|null, min, max }, …]`
in the order listed.

### 5.2 World object

`DM.State.create({ seed, name = 'Unnamed', L = 6, geo = null })` returns:

```
world.version = 1
world.seed            uint32
world.name            string
world.L, world.n      subdivision level and cell count
world.geo             DM.Geo instance (built via DM.Geo.get(L) unless passed in); never serialized
world.rng             DM.U.rng(seed)
world.<cell fields>   one typed array per row of §5.1, filled with the default

world.year = 0        simulation time in years (float). Starts advancing when the first people wake.
world.tick = 0        integer sim tick counter
world.phase = 'molten'   'molten' | 'water' | 'life' | 'people' | 'history' | 'ended'
world.heat = 1        1 = molten surface … 0 = fully cooled (genesis visuals)
world.seaLevel = -9   sea surface height in world units relative to the datum (0 once the oceans are "full")
world.globalTemp = 0  °C offset added to every cell's temperature (greenhouse / dust)
world.co2 = 0         greenhouse loading, 0 = pre-industrial
world.dust = 0        0…1 stratospheric dust / ash / soot (cools the world)
world.age = 0         highest DM.AGES index reached by any culture

world.god = { awe: 0, aweMax: 100, mercy: 0, wrath: 0, epithet: '', deeds: {} }
                      deeds: power id → times used
world.settlements = []   index = settlement id; destroyed ones stay with `.dead = true`
world.cultures = []      index = culture id
world.units = []         live moving entities
world.disasters = []     active multi-tick events (eruptions, tsunamis, storms …)
world.rivers = []        derived: array of { cells: Int32Array, flow: Float32Array } (equal length,
                         source → mouth, flow = discharge at each cell); see SPEC-SIM §3.4
world.chronicle = []     history entries
world.snapshots = []     time-lapse frames (kept in memory; persisted only if small)
world.nextId = { unit: 1, disaster: 1, entry: 1 }
world.stats = { pop: 0, peakPop: 0, believers: 0, wars: 0, miracles: 0 }
world.settings = { calamity: 1 }          natural-disaster frequency multiplier (0 = off)
world.vars = {}       free-form bag of persisted JSON-compatible scalars owned by individual sim
                      modules (for example `vars.seaBase`); serialized as is
world.ending = null   or { type: 'ark' | 'silence', mood, culture, settlement, year } (SPEC-SIM §3.12)
world.dirty = { elev: true, color: true, water: true, objects: true, rivers: true, roads: true }
                      set to true by the sim when the renderer must rebuild that data. Once per
                      frame the renderer copies these flags and sets them all to false; render
                      modules read the copy and never write world.dirty (SPEC-RENDER §4)
world.need = { derive: true, climate: true, hydro: true, life: true }
                      sim-internal "recompute soon" flags (life = biomes and habitability)
```

Entities (settlements, cultures, units, disasters, chronicle entries) are plain JSON-compatible
objects: numbers, strings, booleans, nulls, plain arrays and plain objects only. No typed arrays,
no functions, no cycles (refer to other entities by id).

**Transient caches.** Any property whose name starts with an underscore, on the world itself or
on any entity at any depth (`world._fires`, `settlement._cells`), is a transient cache: it may
hold anything (typed arrays included), is never serialized, and must be rebuildable from
persisted state. `serialize` skips such properties; `deserialize` never produces them.

### 5.3 Functions

```
DM.State.create(opts) → world
DM.State.isSea(world, c) → bool
DM.State.groundR(world, c) → DM.R + elev[c]                     radius of the solid ground at a cell
DM.State.surfaceR(world, c) → radius of what you would stand or float on at a cell:
        sea cell → DM.R + seaLevel; land cell → DM.R + elev[c] + water[c]
DM.State.ELEV_MIN = -12, DM.State.ELEV_MAX = 20                 hard clamp for elev (matches q16 range)
DM.State.serialize(world) → plain JSON-compatible object
        Persisted cell fields are quantized (per CELL_FIELDS) and base64-encoded. Scalars, god,
        settlements, cultures, units, disasters, chronicle, nextId, stats, settings, vars, ending
        and rng state (`rng.s`) are included. `geo`, derived fields, `dirty`, `need`, `snapshots`,
        `rivers` and every underscore-prefixed property are not.
DM.State.deserialize(obj) → world
        Rebuilds geo for obj.L, restores everything serialize wrote, sets derived fields to their
        defaults, all `dirty` and `need` flags to true. Throws Error('bad save') on malformed or
        wrong-version input.
DM.State.sizeOf(obj) → approximate size in bytes of JSON.stringify(obj) (for save-size checks)
```

Round-trip requirement: `deserialize(JSON.parse(JSON.stringify(serialize(w))))` reproduces every
persisted field to within its quantization step and every scalar/entity exactly, and the rng
continues the same stream. Target size at L = 6 with an empty entity list: under 1.2 MB of JSON
(19 bytes per cell, base64; no compression is required).

Entity factories (`DM.State.newSettlement / newCulture / newUnit / newDisaster`), `DM.BUILDINGS`,
`DM.CULTURE_COLORS`, `DM.EVENTS` and `DM.State.VEG_T` live in `js/sim/schema.js` (written by
the lead; see SPEC-SIM §1–2). `state.js` does not define them.

### 5.4 Enums

```
DM.BIOME = { SEA: 0, SHALLOWS: 1, BEACH: 2, DESERT: 3, SAVANNA: 4, GRASSLAND: 5, FOREST: 6,
             RAINFOREST: 7, TAIGA: 8, TUNDRA: 9, ICE: 10, ROCK: 11, SNOWPEAK: 12, WETLAND: 13,
             BARREN: 14, SEAICE: 15 }
DM.BIOME_NAMES = ['Sea', 'Shallows', 'Beach', 'Desert', 'Savanna', 'Grassland', 'Forest',
                  'Rainforest', 'Taiga', 'Tundra', 'Ice', 'Rock', 'Snow peak', 'Wetland',
                  'Barren', 'Sea ice']
DM.AGES = [ { id: 0, name: 'Stone' }, { id: 1, name: 'Bronze' }, { id: 2, name: 'Iron' },
            { id: 3, name: 'Sail' }, { id: 4, name: 'Industry' }, { id: 5, name: 'Atomic' },
            { id: 6, name: 'Space' } ]
```

## 6. Tools (Node, zero dependencies)

- `tools/load.js` *(exists)* — `const DM = require('../tools/load.js')();` loads every existing
  DOM-free file (core, sim, `js/render/models.js`, `js/render/towns.js`, `js/ui/camera.js`,
  `dev/fixtures.js`) in dependency order into `globalThis.DM` and returns it. Missing files are
  skipped. `require('../tools/load.js')({ only: ['civ', 'economy'] })` loads the core,
  `state.js`, `schema.js` and just the named files: **use this form in a module's own tests** so
  a half-written neighbour cannot break your run.
- `tools/t.js` *(exists)* — tiny assertion helper used by tests (see §7).
- `tools/test.js` *(exists)* — test runner; each test file runs in its own Node process.
- `tools/mapimg.js` *(exists)* — PNG maps of a world from Node (elevation, biomes, any field,
  territories), so sim engineers can look at what their module produces.
- `tools/server.js` — static dev server for the project root on `127.0.0.1:5174`, no-cache
  headers, correct MIME types, and `POST /__save?name=<file>` which writes the raw request body to
  `shots/<file>` (name sanitized to `[A-Za-z0-9._-]`). If the body is a `data:` URL it is decoded.
- `tools/shot.js` — headless-browser rig built on Microsoft Edge's DevTools protocol with Node's
  built-in `WebSocket` and `fetch` (no puppeteer). See `docs/TOOLS.md` once written.

## 7. Tests

```js
// tests/example.test.js
const T = require('../tools/t.js');
const DM = require('../tools/load.js')();
T.test('lerp midpoint', () => { T.near(DM.U.lerp(0, 10, 0.5), 5, 1e-9); });
```

`T.test(name, fn)`, `T.ok(cond, msg)`, `T.eq(a, b, msg)` (deep-equal via JSON for objects),
`T.near(a, b, eps, msg)`, `T.throws(fn, msg)`. Run with `node tools/test.js` from the project
root, or run one file directly (`node tests/example.test.js`). Exit code is non-zero if any
test fails. Tests must be deterministic and finish in a few seconds per file.

- **Stubs.** Never assign onto `DM.*` in a test except through `T.stub(obj, name, value)`
  (for example `T.stub(DM.Civ, 'found', fake)` or `T.stub(DM, 'Civ', { found: fake })` when the
  module does not exist); stubs made inside a `T.test` are restored when it ends.
- **Events.** Sim tests call `T.watchEvents()` once: every Bus event emitted during a test is
  checked against `DM.EVENTS` (unknown name, missing or extra payload key, non-finite number ⇒
  the test fails).
- **Timing.** Tests never assert wall-clock budgets tighter than 5× the stated target (many
  engineers share this machine). Report exact timings in your write-up instead.
- **Browser scenarios** live in `tests/browser/<module>.scn.js`, exporting
  `async (page, browser) => result`, and run with
  `node tools/shot.js --run tests/browser/<module>.scn.js`. They throw on failure, never use
  `--allow-errors`, and save screenshots as `shots/<module>-<view>.png`.
