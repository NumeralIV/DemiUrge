# DEMIURGE — Simulation spec (v2)

Read `CONCEPT.md` and `FOUNDATION.md` first. This file defines the shared data model, the event
list and every sim module's public API and rules. **APIs, entity shapes, event names/payloads,
the writers table and the tick schedule are binding.** Numbers are *starting values*: keep them
in a `TUNE` block (§0.3). After integration only the balance owner changes them (§4). Where a
rule is not specified, choose what makes the world behave believably and keeps it cheap, and say
what you chose in your report.

## 0. Ground rules for sim modules

0.1 **Files** (all under `js/sim/`, loaded in this order after core):
`state.js, schema.js, names.js, terrain.js, climate.js, hydro.js, life.js, path.js, civ.js,
economy.js, society.js, diplomacy.js, units.js, powers.js, disasters.js, chronicle.js, sim.js,
balance.js`. Three namespaces are split over two files: `DM.Civ` = `civ.js` + `economy.js`;
`DM.Society` = `society.js` + `diplomacy.js`; `DM.Powers` = `powers.js` + `disasters.js`. The
second file of each pair extends the namespace created by the first
(`const Civ = (DM.Civ = DM.Civ || {});`). Every module looks other modules up as `DM.X.fn(...)`
**at call time**, never at load time, and does nothing at load time except define itself.

0.2 **Active world.** `DM.world` is the world being simulated. `Sim.use`, `Sim.tick`,
`Sim.advance`, `Powers.use` and `Powers.update` assign `DM.world = world` on entry, so Bus
handlers always see the emitting world. All API functions still take `world` explicitly.

0.3 **Tuning.** Each module keeps its numbers in one object at the top of its file:
`const TUNE = (DM.TUNE = DM.TUNE || {}); TUNE.civ = { ... };` (the second file of a pair uses
`Object.assign(TUNE.civ = TUNE.civ || {}, { ... })`) and reads them at call time.

0.4 **Distances are angles** (radians) in APIs and tuning unless a rule says "hops". Convert with
`geo.step`. The reference world is L = 6 (`geo.step ≈ 0.0189`, one cell ≈ 1.9 world units
across, ≈ 3 square units). 1 hop ≈ 0.019 rad.

0.5 **Time.** One tick = `Sim.TICK = 0.25` years = `Sim.REAL_PER_TICK = 0.125` real seconds at
1×. `world.tick` counts every tick. `world.year` advances only in phases `'history'` and
`'ended'`. "Yearly" = every 4th tick of history (§3.12).

0.6 **Robustness.** No NaN or Infinity may ever be written into world arrays or entities (guard
every division, `log`, `sqrt`). Functions tolerate dead settlements/cultures, empty lists, the
poles, worlds with no sea or no land, any phase, and missing transient caches (treat a missing
`_fires` or `_cells` as empty). Never throw for bad targets: return `false` / `null`.
Module-level scratch buffers are keyed by `world.n` and re-allocated when it changes (tests use
L = 4, the game L = 6).

0.7 **Dirty flags** tell the renderer what to rebuild: `elev` (heights), `color` (biome, veg,
ice, farm, urban, scorch, lava, wet, owner), `water` (sea level, lakes, sea ice), `rivers`,
`roads`, `objects` (trees or buildings must be re-instanced: vegetation crossing a
`DM.State.VEG_T` threshold, a settlement founded / destroyed / changing tier, buildings or
culture). The sim only ever sets them to `true`. **Need flags** are sim-internal:
`derive` (slope / coast / landmasses stale), `climate` (temp / moist stale), `hydro`
(lakes / rivers stale), `life` (biomes / habitability stale).

0.8 **Performance budget** at L = 6 in Node on a desktop CPU: mean tick ≤ 2.5 ms, any single
tick ≤ 30 ms. The scheduler in §3.12 runs at most one heavy pass per tick.

0.9 **Determinism.** (1) All randomness comes from `world.rng` or a fork of it; only `js/sim/*`
may draw from it. (2) Never let object key order drive a decision: iterate settlements and
cultures by ascending id, buildings via `DM.BUILDINGS`, powers via `Powers.LIST`. (3) Every sort
comparator tie-breaks on id (or cell id). (4) Real-time quantities (flight times, per-second
costs, rate limits) are computed only from the `dt` arguments passed in, never from a clock.
(5) UI and render code call only read-only sim functions (`Society.peek`, not `Society.rel`).
(6) Chronicle wording uses `world.rng.fork('chron:' + entryId)` so prose never disturbs the sim
stream. Guarantee: two headless runs driven by the same sequence of `Sim.advance` and
`Powers.use` calls produce identical worlds. (In the live game, impact timing depends on frame
rate by design; a save/load boundary may diverge slightly because fields are quantized.)

0.10 **Tests.** Each test file runs in its own Node process. Your own tests must pass with only
the foundation, `schema.js` and your module present: stub collaborators with `T.stub`. Use
`tests/helpers/world.js` for synthetic planets and `T.watchEvents()` so any event your module
emits is checked against `DM.EVENTS`. Runs longer than ~300 years belong in `tools/simrun.js`.

## 1. Entities

Entities are plain JSON data (FOUNDATION §5.2). They are created **only** through the factories
in `schema.js`, which fill every field below with its default, assign the id and push the entity
into its world list: `DM.State.newSettlement(world, fields)`, `newCulture(world, fields)`,
`newUnit(world, fields)`, `newDisaster(world, fields)`. Underscore fields are transient.

```
Settlement {
  id, cell, name, culture,            // culture id
  founded, dead: false, deadYear: null, cause: null,
  pop,                                // float, people
  tier,                               // 0 camp, 1 village, 2 town, 3 city, 4 metropolis
  K,                                  // carrying capacity (people) computed this year
  growth,                             // last year's population change
  radius,                             // territory radius in hops
  b: { shrine, temple, granary, walls, port, market, mine, factory, academy, plant, launch, wonder },  // 0 or 1 (DM.BUILDINGS)
  devotion,                           // 0..1 strength of belief in the player-god
  love,                               // -1 (terror) .. +1 (adoration)
  jaded,                              // 0..1 numbness to miracles; decays 5 %/yr
  plague, immune,                     // severity 0..1; years of immunity left
  inspire, blessed,                   // years remaining
  famine,                             // consecutive years over capacity
  prayer,                             // null | { kind: 'rain'|'heal'|'peace'|'bloom', since }
  apart,                              // consecutive years estranged from its culture (schisms)
  capital: false,
  ark,                                // 0..1 construction progress (needs b.launch)
  lastExpand,                         // year of the last settler party or failed search
  _cells, _n                          // transient: Int32Array of territory cells, used length
}

Culture {
  id, name, adj, plural,              // 'Umar', 'Umaran', 'the Umari'
  color: [r, g, b],                   // 0..1, from DM.CULTURE_COLORS by free slot
  lang,                               // language object from DM.Names.language
  parent,                             // culture id or -1
  born, dead: false,
  age,                                // DM.AGES index
  science,                            // progress-years accumulated toward the next age
  capital,                            // settlement id or -1
  godName, epithet,                   // their name and current epithet for the player-god
  traits: { piety, aggression, curiosity, seafaring },   // each 0..1
  prophet: null | { name, settlement, until },
  edict: null | 'peace' | 'expand' | 'build' | 'crusade', edictSince,
  pop, towns,                         // refreshed yearly
  exhaustion,                         // 0..1 war weariness
  seen: { },                          // power id -> accumulated faith this culture has witnessed
  lastSplit                           // year a daughter culture last split from it
}

Relation (between two cultures; stored in world.vars.rel['<lowId>|<highId>'])
  { v: -1..1, war: false, since: year, truce: year, trade: 0..1, contact: bool, aggressor: id | -1 }

Unit {
  id, type,                           // 'settler' | 'trader' | 'army' | 'prophet'
  culture, from, to,                  // settlement ids (to = -1 when heading for an empty site)
  dest,                               // destination cell
  path: [cell, …], i, t,              // between path[i] and path[i+1], t in 0..1
  speed,                              // cells per year on plain land
  size,                               // people or strength
  naval,                              // false | 'coastal' | true: what it may cross (Path §3.6)
  sea: false,                         // true while its current cell is a sea cell (drawn as a ship)
  born, data: {}
}

Disaster { id, type, cell, t, dur, power, ang, natural, data: {} }      // t, dur in years
  comet / meteor : cell = target, ang = crater radius, data.eta = real seconds to impact, data.flight = total flight seconds
  volcano        : cell = vent, ang = cone radius, power = current intensity 0..1
  tsunami        : cell = source, ang = final reach, data.front = current angular radius of the wave front, power = wave height (world units)
  storm          : cell = current centre (moves at most one hop per tick), ang = radius, power 0..1
  drought        : cell and ang fixed, power 0..1
  A disaster is removed when t >= dur (comet/meteor: on impact).

ChronicleEntry { id, year, type, text, cell, culture, weight }   // weight 1 minor, 2 notable, 3 epochal
```

`DM.CULTURE_COLORS` (schema.js) = six colour-blind-safe colours
`#e69f00, #56b4e9, #009e73, #f0e442, #d55e00, #cc79a7` as linear-ish `[r,g,b]`.
`DM.State.VEG_T = [0.22, 0.5, 0.78]` are the vegetation levels at which a cell shows 1, 2, 3 trees.

### 1.1 Writers (binding: one owner per piece of state)

| state | written by |
|---|---|
| `elev` | Terrain.* only (Powers calls Terrain for craters, cones, sculpting, lava rock) |
| `slope`, `coast`, `_landId` | Terrain.derive |
| `temp`, `moist`, `ice`, `co2`, `dust`, `globalTemp`, `_up` | Climate |
| `seaLevel` | Powers (comets) and Sim.genesis.skip before history; Climate.yearly in history/ended |
| `water`, `flow`, `down`, `fresh`, `rivers` | Hydro.update |
| `veg`, `fauna`, `fire`, `scorch`, `biome`, `hab`, `_fires`, `_landHab` | Life (Civ lowers `veg` on new farmland) |
| `wet` | Powers raises; Life.step decays |
| `fert` | Terrain.generate; Life.step / bloom; Powers (ash, floods) |
| `lava`, `disasters` | Powers (disasters.js) |
| `rad` | Society raises (nukes); Life.step decays 1.5 %/yr |
| `owner`, `farm`, `urban` | Civ (Life decays farm/urban on unowned cells) |
| `road` | Society (diplomacy.js: trade) |
| settlement: pop, K, growth, tier, radius, famine, plague, immune, inspire, blessed, lastExpand, `b.*` except wonder, culture, capital, dead | Civ (`inspire`/`blessed` are set by Powers, counted down by Civ.yearly; plague only via Civ.infect / Civ.cure) |
| settlement: devotion, love, jaded, prayer, apart, ark, `b.wonder` | Society |
| culture: age, science; `world.age`; `culture.capital` | Civ |
| culture: everything else (pop, towns, dead, prophet, edict, exhaustion, seen, epithet via Chronicle) | Society (`epithet`, `godName` changes: Chronicle.updateEpithets) |
| `god.awe` | Society adds income; Powers.use spends; `awaken` sets the starting 60 |
| `god.aweMax` | Society.yearly |
| `god.deeds`, `stats.miracles` | Powers.use (once per `power:used`) |
| `god.mercy`, `god.wrath` | Society.witness |
| `god.epithet` | Chronicle.updateEpithets (the epithet of the most populous living culture) |
| `stats.wars` | Society.declareWar |
| `stats.pop`, `peakPop`, `believers` | Sim.refreshStats (`believers` = Σ pop × devotion) |
| `world.phase`, `year`, `tick`, `heat`, `ending` | Sim |
| `chronicle`, `snapshots` | Chronicle |
| `vars.seaBase`, `vars.iceRef` | Climate.calibrate · `vars.lifeBoost`, `vars.t0` Sim.setPhase · `vars.rel`, `vars.ark`, `vars.lastNuke` Society · `vars.snapEvery`, `vars.snapNext` Chronicle. Any other key is prefixed with its module name (`civ_…`). |

## 2. Events (DM.Bus)

Emitters send exactly these payloads (`DM.EVENTS` in schema.js is this table as data; tests
check against it). `cell` is a cell id or −1. Culture and settlement references are ids.

| event | payload | emitted by |
|---|---|---|
| `phase` | `{ phase, prev }` | Sim.setPhase |
| `power:used` | `{ id, cell, ang, brush }` (brush powers: at most 4 per real second) | Powers.use |
| `miracle:witnessed` | `{ cell, kind, good, ids }` (`ids`: up to 16 settlement ids that noticed; brush powers at most 2 per real second) | Society.witness |
| `lightning` | `{ cell, natural }` | Powers |
| `meteor:launched` | `{ cell, eta, kind: 'comet'|'meteor', id }` | Powers |
| `meteor:impact` | `{ cell, ang, kind, sea }` | Powers |
| `tsunami:start` `{ cell }` · `tsunami:hit` `{ cell, height }` (rate-limited) | | Powers |
| `volcano:start` `{ cell, id, natural }` · `volcano:end` `{ cell, id }` | | Powers |
| `quake` | `{ cell, ang, power, natural }` | Powers |
| `fire:start` | `{ cell }` (at most 2 per tick) | Life |
| `storm` `{ cell, ang, start }` · `drought` `{ cell, ang, start }` | start: true when it begins, false when it ends | Powers |
| `settlement:founded` | `{ id, cell, culture, by: 'awaken'|'settlers' }` | Civ |
| `settlement:tier` | `{ id, tier, prev }` | Civ |
| `settlement:building` | `{ id, building }` (every building, wonder included) | Civ; Society for `wonder` |
| `settlement:famine` `{ id }` · `settlement:plague` `{ id, start }` | | Civ |
| `settlement:destroyed` | `{ id, cell, culture, cause }` cause: `'drowned'|'meteor'|'lava'|'war'|'famine'|'plague'|'quake'|'nuke'|'abandoned'|'wave'|'lightning'` | Civ |
| `settlement:captured` | `{ id, from, to, how: 'war'|'schism' }` (culture ids) | Civ |
| `prayer` `{ id, kind, start }` · `prayer:answered` `{ id, kind }` | | Society |
| `culture:born` | `{ id, parent, cell, reason: 'awaken'|'schism'|'distance' }` | Society |
| `culture:died` `{ id }` | | Society |
| `culture:age` `{ id, age, prev }` · `world:age` `{ age }` | | Civ |
| `war:declared` `{ a, b }` (a = aggressor) · `war:ended` `{ a, b, victor }` (−1 = draw) | culture ids | Society |
| `battle` | `{ cell, attacker, defender, won, settlement }` (culture ids; `settlement` = the defending settlement; `won` = attacker won) | Society |
| `trade:route` | `{ a, b }` (settlement ids; first caravan between a pair) | Society |
| `prophet:risen` `{ culture, settlement, name }` · `prophet:died` `{ culture, name }` · `edict` `{ culture, edict }` | | Society |
| `wonder:built` | `{ settlement, name }` | Society |
| `nuke` | `{ cell, from, to }` (culture ids) | Society |
| `ark:started` `{ culture, settlement }` · `ark:launched` `{ culture, settlement }` | | Society |
| `climate:milestone` | `{ kind: 'warming'|'cooling'|'sea-rise'|'sea-fall'|'ice-age'|'thaw', value }` | Climate |
| `unit:spawned` · `unit:arrived` `{ id, type, culture, cell }` · `unit:died` `{ id, type, culture, cell, cause }` | | Units |
| `chronicle:entry` | `{ entry }` | Chronicle |
| `awe:short` | `{ id }` (at most once per press and once per 1.5 real seconds per power) | Powers |
| `people:lost` | `{ }` (no settlements left before the world was populous enough for an ending) | Sim |
| `ending` | the `world.ending` object (§3.12) | Sim |

## 3. Modules

### 3.1 `names.js` → `DM.Names`

Procedural languages. No runtime AI; pure combinatorics on an rng.

```
Names.language(rng, parent = null) → lang    JSON object: consonant/vowel inventories, syllable shapes, stress, favourite
                                              endings for places/people, and (with a parent) the parent's roots after a few
                                              regular sound shifts, so daughter languages look related
Names.word(lang, rng, minSyl = 1, maxSyl = 3) → lowercase word
Names.place(lang, rng)   → 'Tel Asha', 'Vornhal', 'Qe-Luun'
Names.person(lang, rng)  → 'Umari', 'Dovak'
Names.culture(lang, rng) → { name, adj, plural }
Names.god(lang, rng)     → the culture's proper name for the player-god
Names.planet(rng)        → a default world name (language-agnostic, evocative)
Names.cap(s)             → capitalized
```

Different languages must look and sound clearly different (inventory size, vowel harmony or not,
clusters or not, apostrophes/hyphens, typical length). Names are pronounceable, 3–14 characters,
never empty, never an obvious English profanity (small blocklist, re-roll on hit).

### 3.2 `terrain.js` → `DM.Terrain`

```
Terrain.generate(world)                      fill elev (and a varied initial fert); deterministic from world.seed
Terrain.sculpt(world, c, ang, amount) → n    add amount × smooth falloff within ang of cell c (amount may be negative)
Terrain.crater(world, c, ang, depth)         bowl of the given depth with a raised rim at ~1.0–1.3 × ang
Terrain.cone(world, c, ang, height)          volcanic cone with a small summit crater
Terrain.roughen(world, c, ang, amount, rng)  random ± displacement (earthquakes); smooth enough not to create one-cell pits
Terrain.touched(world)                       call after any elevation change: dirty.elev/water/color, need.derive/climate/hydro
Terrain.derive(world)                        slope, coast, and landmass labels; clears need.derive; sets need.life
```

Generation targets at `seaLevel = 0`: 36–42 % land; 3–6 major landmasses plus islands; mountain
belts (ridged noise) inside continents and along some coasts; broad coastal lowlands (at least a
quarter of land below +0.6); continental shelves; ocean floor down to about −6.5; highest peaks
+6 … +8.5; some land at high latitudes. Use quantile remapping of the raw noise so the land
fraction is hit for every seed, then add ridge detail. All ops clamp to
`State.ELEV_MIN … ELEV_MAX`. `generate` leaves every other field at its default and sets all
dirty and need flags. `derive` also labels connected land components in `world._landId`
(Int32Array, −1 on sea cells; ids 0…k−1 by descending size).

### 3.3 `climate.js` → `DM.Climate`

```
Climate.update(world)       temp and moist for every cell; clears need.climate; sets need.life and dirty.color
Climate.stepIce(world)      ice toward its equilibrium (called every 20 ticks)
Climate.yearly(world)       CO₂, dust, globalTemp, sea level; milestones (history/ended only; see below)
Climate.calibrate(world)    Climate.update, then set ice to its equilibrium directly (a few fixed-point passes with the
                            albedo term), then vars.seaBase = seaLevel and vars.iceRef = max(50, Σ ice over land cells).
                            Called by Sim when history begins (and by Sim.use if vars.seaBase is missing).
Climate.datum(world) → max(0, vars.seaBase) when vars.seaBase is a finite number, else max(0, world.seaLevel)
Climate.anomaly(world) → globalTemp − 9 × dust        the number the HUD shows and milestones use
Climate.emit(world, amount)      add to world.co2 (clamped 0..300)
Climate.addDust(world, amount)   add to world.dust (clamped 0..1); sets need.climate
```

- **Temperature** (°C): about +29 at the equator falling to −24 at the poles (smooth in |lat|),
  minus `5.5 × max(0, elev − Climate.datum(world))`, plus `Climate.anomaly(world)`, minus
  `4 × ice[c]` (albedo), plus a little low-frequency noise so isotherms are not perfect circles.
  The stored `temp` of a sea cell is clamped at −2, but sea-ice equilibrium uses the unclamped value.
- **Moisture**: prevailing winds by latitude band (easterly trades |lat| < 30°, westerlies
  30–60°, polar easterlies; blended at band edges; build directions with `geo.basis`). Precompute
  each cell's upwind neighbour once per geo (cache `world._up`). Sea cells are humidity sources
  (warmer sea → more; a dry belt near ±28°, a wet belt at the equator). Sweep humidity downwind
  for enough passes to cross a continent: each land step rains out a fraction (more when the air
  is forced uphill: orographic rain and rain shadows) and passes the rest on.
  `moist = sat(rain / RAIN_REF + wet[c] − 0.25 × drought)` with a **fixed** constant `RAIN_REF`
  chosen so a wet equatorial coast is ≈ 0.9 (never normalize by the planet's current maximum),
  `drought` = strength of any active drought disaster covering the cell; one smoothing pass.
  A world with no sea is bone dry. Cost ≤ 12 ms at L = 6.
- **Ice** equilibrium: land, 1 where temperature < −9 ramping from −4; sea ice, 0 at −2.5
  ramping to 1 at −6 (unclamped sea temperature). `stepIce` moves 6 % of the way per call in
  history and 30 % in genesis phases. Sets dirty.color / dirty.water when anything changed.
- **Yearly** (does nothing but decay dust in genesis phases; never touches `seaLevel` while
  `vars.seaBase` is not a finite number):
  `dust *= 0.93`. Uptake: `co2 −= co2 × 0.005 × (0.5 + Life.cover(world).veg)`.
  `globalTemp` approaches `3 × log2(1 + co2 / 100)` with a ~30-year lag.
  Sea level: `r = (Σ ice over land) / vars.iceRef`; `iceTerm = 0.35 × (1 − r)` for r ≤ 1, else
  `−0.25 × min(1, r − 1)`; `target = clamp(vars.seaBase + iceTerm + 0.04 × globalTemp,
  vars.seaBase − 0.3, vars.seaBase + 0.6)`; `seaLevel` moves toward it by at most 0.005 per year.
  When `seaLevel` has moved more than 0.03 since it last did so: need.derive / hydro / climate,
  dirty.water / color / objects. Set need.climate when `Climate.anomaly` has changed by 0.25 °C
  since the last `Climate.update`. Emit each `climate:milestone` once per threshold crossed
  (anomaly ≥ +1, +2, +3, +4; ≤ −2, −4; sea ±0.2, ±0.4 relative to `vars.seaBase`).
- **Cross-module amounts** (binding starting values): a settlement with a factory calls
  `Climate.emit(world, 0.0025 × sqrt(pop / 1000))` per year (× 0.5 in the Space age). Dust:
  meteor +0.25; volcano +0.03 per year while erupting; each nuclear strike +0.10. Target: an
  undisturbed world reaches co2 60–200 (anomaly +2 … +4.5 °C) by the Ark.

### 3.4 `hydro.js` → `DM.Hydro`

```
Hydro.update(world)       lakes, flow directions, discharge, river polylines, distance to fresh water; clears need.hydro;
                          sets need.life, dirty.rivers, dirty.water
Hydro.riverT(world)       discharge above which a cell counts as a river (scales with cell count)
```

Priority-flood from all sea cells over the land (binary heap, reused buffers) gives a filled
surface and a drainage parent for every land cell; `down[c]` = that parent (−1 on sea cells).
**Lakes**: a depression becomes a lake only if it has at least 3 cells or a maximum depth ≥ 0.25,
and its cells' mean `moist` ≥ 0.15; then `water[c] = min(2.5, filled − elev[c])`. Smaller pits
are treated as filled ground (`water = 0`) and drain through. A *lake cell* is a land cell with
`water > 0.03`. Discharge, from the highest filled level to the lowest:
`flow[c] = moist[c] + Σ flow[upstream]`. Rivers: cells with `flow > riverT`. `world.rivers` =
array of `{ cells: Int32Array, flow: Float32Array }` from each river source downstream; the last
cell is the first sea cell, the first lake cell, or the confluence cell (inclusive), so a river
always ends under water or on another river. `fresh[c]` = hops to the nearest river or lake cell
(multi-source BFS, cap 255). If there is no sea, the lowest cell is the outlet. Cost ≤ 15 ms.

### 3.5 `life.js` → `DM.Life`

```
Life.classify(world)                 biome for every cell; sets dirty.color and dirty.objects
Life.capacity(world, c) → 0..1       vegetation the cell could support right now
Life.step(world, part, parts)        advance veg, fauna, fert, wet, scorch, rad (and farm/urban decay on unowned cells) for cells
                                     with c % parts === part, by dt = parts × Sim.TICK years
Life.burn(world)                     advance fires; call every tick
Life.ignite(world, c, intensity = 1) → bool   start a fire if the cell can burn
Life.seed(world, c, ang)             plant starter vegetation (veg ≥ 0.35 on plantable land within ang)
Life.bloom(world, c, ang, amount)    raise veg toward capacity and fert by amount × falloff
Life.fill(world, frac = 1)           set veg = frac × capacity and fauna to capacity everywhere (Sim.genesis.skip)
Life.habitability(world)             hab for every cell and world._landHab (habitable cell count per landmass id); clears need.life
Life.rebuild(world)                  rebuild transient caches from persisted state (world._fires from fire[])
Life.cover(world) → { land, veg, forest }   land fraction; mean veg / capacity over plantable land; forest fraction (cached per tick)
```

- **Biomes** (`DM.BIOME`): sea deeper than 0.6 → SEA, else SHALLOWS; sea with ice > 0.5 → SEAICE.
  Land: lava or heavy scorch with no vegetation → BARREN; ice > 0.5 → ICE; very high and cold →
  SNOWPEAK; very steep or very high → ROCK; a low strip next to the sea → BEACH; otherwise a
  Whittaker-style table on `temp` and `moist`: TUNDRA, TAIGA, DESERT, SAVANNA, GRASSLAND, FOREST,
  RAINFOREST, WETLAND (flat, very wet lowland near water).
- **Vegetation.** Growth needs `veg > 0` in the cell or a neighbour with `veg > 0.05`: a barren
  world stays barren until seeded. Logistic growth toward
  `capacity = biomeCapacity × (0.6 + 0.4·fert) × (1 − farm) × (1 − urban) × (1 − 0.8·rad)`
  (0 on lake cells, sea, ice); die-back toward capacity when above it; a cleared forest regrows
  in ~120 years. Rate × `world.vars.lifeBoost` (default 1). *Drifting seeds*: each yearly-equivalent
  step pick 8 random land cells; a plantable cell with `veg = 0` that has a cell with `veg > 0.3`
  within 12 hops (sea included) gets `veg = 0.08`, so islands and new volcanic land green over
  centuries. Set dirty.objects when a cell's veg crosses a `State.VEG_T` level.
- **Fire** (`world._fires = { cells: Int32Array, n }`; `cells[0..n−1]` are the cells with
  `fire > 0`): a cell burns for exactly 2 ticks and makes one ignition attempt per neighbour on
  its first tick with `P = 0.55 × veg × dry`, `dry = sat(1.2 − 1.6 × (moist + wet))`; never onto
  sea, ice, lake cells or `veg < 0.15`. (Wet forest P < 0.05; dry forest ≈ 0.28, below the
  lattice's percolation threshold of 0.35; only drought on dense vegetation exceeds it.) No new
  ignitions while more than 1,500 cells burn. Burning removes 80 % of veg and sets `scorch`.
  `wet > 0.3` extinguishes.
- **Fauna**: logistic toward a capacity from vegetation and biome on land, from depth and
  temperature in the sea (shallows are rich). **Fertility** relaxes toward a natural level
  (higher beside fresh water and on old ash). `wet` and `scorch` decay. `rad` decays 1.5 %/yr.
  On cells with `owner = −1`, `farm` decays 5 %/yr and `urban` 1 %/yr (ruins fade). On sea
  cells veg, farm, urban, road, water, fire, scorch are forced to 0.
- **Habitability** 0..1: zero on sea, lake cells, ice, rock, snow peaks, lava, barren; otherwise
  the product of temperature suitability (best 8–26 °C), water (rainfall, or fresh water within
  2 hops makes a desert liveable), fertility, gentle slope, a small coastal bonus, `(1 − rad)`.

### 3.6 `path.js` → `DM.Path`

```
Path.find(world, from, to, { naval = false, maxCost = 400 } = {}) → [from, …, to] | null
Path.stepCost(world, a, b, naval) → cost of moving from cell a to neighbour b, or Infinity
Path.reach(world, from, maxCost, naval, visit)    Dijkstra flood; visit(cell, cost) in non-decreasing cost; return false to stop
```

A* on the cell graph with a reused binary heap and stamp arrays (no per-call allocation except
the result). Land step ≈ 1, plus slope (× 6 per unit of height difference), dense forest +0.4,
ice +3, lake cell +2, road −0.25 per level (floor 0.3); lava is impassable. `naval`:
`false` = land only; `'coastal'` = may also cross sea cells shallower than 0.6 (cost 0.6 each:
canoes along shelves and between near islands); `true` = any sea cell (cost 0.35 each). Embarking
or landing costs 3; sea ice (> 0.6) is impassable. Worst case (whole planet) ≤ 25 ms.

### 3.7 `civ.js` + `economy.js` → `DM.Civ`

`civ.js` (the lifecycle API everyone calls):

```
Civ.found(world, cell, cultureId, pop, by = 'settlers') → settlement | null
        null when the site is invalid or there are already 160 live settlements. Claims territory and computes K at once.
        An awakened tribe starts with devotion 0.6 and love 0.3; settlers copy the values of their origin (pass them in
        `opts` as a 6th argument { devotion, love } or default 0.5 / 0). Makes the settlement the culture's capital if the
        culture has none. Emits settlement:founded.
Civ.claim(world, s)                    recompute territory (owner[] and s._cells): land cells within s.radius hops, not sea, not
                                       owned by another settlement; a contested unowned cell goes to the nearer centre (ties: lower id)
Civ.rebuild(world)                     rebuild every live settlement's _cells/_n from owner[] WITHOUT changing owner (after load)
Civ.destroy(world, s, cause)           dead = true, release territory, elect a new capital if needed; emits settlement:destroyed
Civ.damage(world, s, frac, cause) → people killed   (destroys the settlement if fewer than 3 remain)
Civ.capture(world, s, cultureId)       conquest: change culture, emit settlement:captured { how: 'war' }; re-elect capitals
Civ.setCulture(world, s, cultureId)    peaceful change of membership (schisms): emits settlement:captured { how: 'schism' }
Civ.electCapital(world, cultureId) → settlement id | -1   the most populous live settlement becomes capital; writes
                                       culture.capital and s.capital for every settlement of that culture. A new capital starts
                                       its own launch site and Ark from 0 (b.launch = 0, ark = 0 on the old one).
Civ.infect(world, s, severity = 0.6) → bool   false if dead or immune > 0; plague = max(plague, severity); emits settlement:plague start
Civ.cure(world, s, immuneYears = 30) → bool   plague = 0, immune = immuneYears; emits settlement:plague { start: false } if it was ill
Civ.at(world, cell) → live settlement centred on the cell or owning it, else null
Civ.near(world, cell, ang) → live settlements whose centre is within ang (nearest first; ties by id)
Civ.live(world) → array of live settlements (ascending id; cached per tick)
Civ.canSettle(world, c) → { ok, reason }      reason: 'ok' | 'sea' (sea or lake) | 'barren' (hab ≤ 0.05) | 'crowded' (within 7 hops of owned land)
Civ.siteScore(world, c) → number              hab plus bonuses (fresh water, coast, room around it)
Civ.bestSite(world) → cell                    best site on the landmass with the most habitable cells (−1 if none)
Civ.ageProgress(world, cultureId) → 0..1      science toward the next age (1 at the last age)
Civ.TIERS = ['camp', 'village', 'town', 'city', 'metropolis']
```

`economy.js`:

```
Civ.yearly(world)          everything below, for every live settlement in id order, then research per culture
Civ.capacity(world, s) → K (people) from the rules below (also used to estimate K for a candidate site)
```

- **Territory** radius by tier: 2, 3, 4, 5, 6 hops. Re-claim when the tier changes and every 10
  years (staggered by id).
- **Capacity.** `techYield = [1, 2.5, 5, 10, 25, 60, 120]` by culture age. Per territory land cell:
  `farmPot = 22 × min(fert, 1.2) × suit(temp, moist) × techYield[age]` on arable cells
  (`hab > 0.15`, not a lake cell); `forage = 3 × (0.6·veg + 0.4·fauna)` (not multiplied by tech);
  per sea cell bordering the territory `fish = 4 × fauna × (1 + port) × sqrt(min(techYield, 10))`.
  `K = Σ max(farmPot, forage) + Σ fish`, × 1.15 with a granary, × 1.25 with a plant.
  **K never depends on `farm[]` or on the population.** After K is known, `farm[]` is set for
  display and ecology: the best cells by farmPot are farmed (farm → 1 at 0.25/yr, lowering veg)
  until their summed farmPot reaches 1.15 × pop; other cells relax toward 0 by 0.1/yr. Lake cells
  are never farmed. `urban[]`: centre cell by tier (0.1, 0.25, 0.5, 0.8, 1); one ring at 0.5 for
  cities; two rings (0.8, 0.4) for metropolises.
- **Growth.** `Keff = max(K, 1)`. If `pop ≤ Keff`: `pop += r × pop × (1 − pop/Keff)` with
  `r = 0.03 × (1 + 0.3·granary + 0.3·[blessed > 0] + 0.2·max(0, love))`, `famine = 0`.
  Else `pop −= min(0.12 × pop, 0.5 × (pop − Keff))`, `famine += 1` (emit `settlement:famine` when
  it reaches 3). Then plague: deaths `plague × 8 %` of pop per year; plague fades 15 %/yr
  (`Civ.cure` below 0.05) and each year may spread (`Civ.infect`) to settlements within 8 hops.
  Count down `blessed`, `inspire`, `immune` by 1 per year (floor 0). Destroy below 3 people
  (cause `famine`, `plague` or `abandoned`).
- **Tier gates**: village 60; town 400 (Bronze+); city 1,500 (Iron+); metropolis 12,000
  (Industry+). Promote when pop reaches the gate; demote only when pop < 0.5 × the gate of the
  current tier. Emit `settlement:tier`.
- **Buildings** appear when their conditions hold; emit `settlement:building` for each:
  shrine (Stone; pop ≥ 30 and devotion ≥ 0.3), temple (Bronze; town+; devotion ≥ 0.35),
  granary (Bronze; village+), walls (Bronze; town+ whose culture is or was at war), port (Iron;
  village+ with a sea cell in its territory), market (Iron; town+), mine (Iron; rock or steep
  cells in the territory), academy (Sail; city+), factory (Industry; city+), plant (Atomic;
  city+), launch (Space; the culture's capital, 20 years after the culture reached Space).
- **Expansion.** A settlement with `pop ≥ 0.85 K`, `pop ≥ 40` and `year − lastExpand ≥ 25` (12
  under the `expand` edict) may send settlers. At most 2 expansion searches per yearly tick
  (longest-waiting settlement first, ties by id). Search with `Path.reach` (maxCost 45 on land;
  from the Bronze age a settlement with a sea cell in its territory uses `naval = 'coastal'`;
  from the Sail age with a port, `naval = true`, maxCost 120). Candidate sites: `canSettle` ok,
  estimated camp K ≥ 60, at least 7 hops from every owned cell (use `world._ownDist`, a
  multi-source BFS distance field capped at 8, rebuilt once per year) and at least 9 hops from
  the `dest` of any live settler unit. Best = `siteScore − 0.01 × path cost`. Party size =
  `clamp(0.12 × pop, 12, 500)`, removed from the origin only after `Units.spawn` returns a unit.
  Any search, successful or not, sets `lastExpand = year` (or `year − 15` if it found nothing:
  retry in 10 years).
- **Hazards** (checked yearly): centre cell under the sea because the sea rose → spawn a settler
  unit with 60 % of the people toward the best valid site within 12 hops (it joins the nearest
  settlement of its culture if there is none), then `destroy(s, 'drowned')`; centre cell with
  `water > 0.3` → `drowned`; lava on the centre → `lava`. (Waves, sculpting under the sea and
  impacts kill outright via Powers.)
- **Research** (intensive, measured in progress-years so world size does not change the pacing):
  each culture gains `rate = sizeF × buildF × traitF × inspireF × catchUp` per year, clamped to
  0.4..2.0. `sizeF = clamp(0.5 + 0.5 × sqrt(liveSettlements / 12), 0.5, 1.25)`;
  `buildF = 1 + 0.15 × (share of its settlements with an academy) + 0.10 × (share with a market)
  + 0.10 × (share with a factory)`; `traitF = 0.85 + 0.3 × curiosity`;
  `inspireF = 1 + 0.25 × (share of its people in inspired settlements)`;
  `catchUp = 1.5` if a culture in contact is at least one age ahead. Progress needed to leave
  each age: `[420, 500, 600, 750, 620, 520]` (nominal: Bronze ≈ 520, Iron ≈ 1,000, Sail ≈ 1,550,
  Industry ≈ 2,200, Atomic ≈ 2,750, Space ≈ 3,200). Emit `culture:age`, and `world:age` when
  `world.age` rises.

Destroyed settlements leave `urban` behind as ruins (Life fades them); their cells become unowned.

### 3.8 `society.js` + `diplomacy.js` → `DM.Society`

`society.js`:

```
Society.newCulture(world, { parent = -1, cell, reason }) → culture     names from DM.Names (daughter language when parent ≥ 0);
                                                                        traits rolled (daughters drift from the parent); emits culture:born
Society.yearly(world)                  housekeeping, belief, Awe, prayers, prophets and edicts, schisms, the Ark; then Society.diplomacy(world)
Society.witness(world, cell, ang, { kind, faith, good })
        Settlements within ang of the cell saw an act of the god. kind = power id (or 'nuke'); faith = devotion gained by a
        settlement at the centre, 0..0.3 (already multiplied by elapsed seconds for brush powers); good in −1..1.
        For each witness, with smooth falloff 1 → 0 across ang:
            gain = faith × falloff × (1 − jaded) × (1 − devotion);   devotion += gain
            jaded = min(1, jaded + 2 × faith × falloff)
            love += 0.5 × good × falloff × (1 − |love|)
            culture.seen[kind] += faith × falloff
        A praying settlement whose prayer matches `kind` gets double faith and good = +1, its prayer is cleared, and
        prayer:answered is emitted. Adds max(0, good) × faith to god.mercy and max(0, −good) × faith to god.wrath.
        Emits miracle:witnessed with the ids that gained anything.
Society.regard(world, cultureId = -1) → { devotion, love, mood }
        population-weighted means over the culture's live settlements (−1 = everyone); mood by the ending thresholds (§3.12):
        'beloved' | 'feared' | 'forgotten' | 'parting'
Society.raiseProphet(world, s) → prophet | null
Society.setEdict(world, cultureId, edict) → bool      needs a living prophet; edicts are free; one per culture; may be changed
                                                      once every 20 years; lasts until the prophet dies
Society.aweIncome(world) → Awe per year (read-only)
```

`diplomacy.js`:

```
Society.rel(world, a, b) → relation object (created on demand; a ≠ b)          sim-only
Society.peek(world, a, b) → relation | null (never creates)                    for UI
Society.atWar(world, a, b) → bool
Society.declareWar(world, a, b)        Society.makePeace(world, a, b, truceYears = 40, victor = -1)
Society.onArrive(world, unit)          traders, armies and prophets reaching their destination
Society.diplomacy(world)               yearly: contact, relations, wars, armies, trade, nuclear risk
```

- **Housekeeping** (start of `yearly`, cultures in id order): refresh `pop` and `towns`; a
  culture with no live settlement and no settler unit of its own becomes `dead` (emit
  `culture:died` once; `makePeace` on all its wars with the other side as victor; kill its units
  with cause `'disbanded'`; `prophet = null`). `jaded` decays 5 %/yr; `love` drifts 1 %/yr toward 0.
- **Belief.** Each settlement's `devotion` drifts 2 %/yr toward
  `baseline = [0.55, 0.6, 0.55, 0.45, 0.3, 0.18, 0.1][age] × (0.6 + 0.8 × piety)` plus 0.05 with
  a shrine, 0.10 with a temple, 0.15 while its culture has a living prophet. Fear (love < −0.3)
  adds 0.1 to the baseline before the Atomic age (they pray harder) and subtracts 0.1 from it in
  the Atomic and Space ages (defiance).
- **Awe.** `god.aweMax = min(1200, 100 + 25 × shrines + 60 × temples + 150 × wonders)`.
  `S = Σ over live settlements of devotion × (0.2 + 0.5·shrine + 1.2·temple) × (1 + tier/2)`.
  Income per year = `0.6 + 0.9 × ln(1 + S)` (one camp ≈ 0.7; three shrined villages ≈ 1.4;
  twenty settlements ≈ 3.3; late game ≈ 4.5). `yearly` adds it to `god.awe`, clamped to aweMax.
- **Prayers.** Yearly, a settlement with devotion ≥ 0.15 and no prayer starts praying for
  `'rain'` (famine ≥ 1 or inside a drought), `'heal'` (plague > 0.2), `'peace'` (its culture at
  war with exhaustion > 0.4) or `'bloom'` (mean scorch of its territory > 0.3). At most 6 prayers
  at once, largest settlements first. Emit `prayer { start: true }`. A prayer ends when its cause
  is gone (`start: false`), when answered (see `witness`), or after 15 unanswered years
  (devotion −0.05, love −0.05, `start: false`).
- **Prophets and edicts.** `raiseProphet` gives the culture a named prophet for ~60 years living
  in `s`. About every 10 years he travels as a `'prophet'` unit to another settlement of his
  culture; on arrival devotion there +0.15 and `prophet.settlement` moves. If the unit dies or
  the term ends, emit `prophet:died` and clear the edict. Edicts: `peace` (no new wars by this
  culture, relations +0.02/yr), `expand` (settler cooldown 12 years), `build` (after 30 years the
  capital raises a Wonder: `b.wonder = 1`, `wonder:built` and `settlement:building`), `crusade`
  (aggression +0.4 toward the least devout neighbouring culture).
- **New cultures.** A settlement is *estranged* while it is more than 0.57 rad from its capital,
  or on another landmass (`world._landId`), or its love differs from its culture's mean by more
  than 0.6; `apart` counts consecutive estranged years. When two or more settlements within
  0.28 rad of each other have `apart ≥ 120`, they secede together (`newCulture` with
  `reason: 'distance'` or `'schism'`, then `Civ.setCulture` for each). At most one birth per
  parent per 150 years and at most 6 living cultures.
- **The Ark.** Only in the capital of a Space-age culture with `b.launch = 1`: emit
  `ark:started` once, then `ark += (1/250) × clamp(culture pop / 1.5 million, 0.8, 1.25)` per
  year. On reaching 1, and only while `vars.ark` is undefined: set
  `vars.ark = { culture, settlement, year }` and emit `ark:launched`.
- **Relations** (`diplomacy`, yearly per pair of living cultures; `contact` = their territories
  come within ~10 hops or a trade route links them):
  `v += 0.03 × (0.1 − v) + 0.04 × trade − 0.02 × border − 0.03 × max(0, aggrA + aggrB − 0.9)
  − 0.03 × |loveA − loveB| + 0.03 × rng.gauss()`, clamped to −1..1 (`border` 0..1 = how much
  frontier they share; `loveX` = the culture's mean love of the god). **War** when `v < −0.5`,
  `year ≥ truce`, neither side is under a `peace` edict, and the more aggressive side has at
  least 0.6 of the other's `pop × tech`, `tech = [1, 1.5, 2.2, 3, 5, 8, 10][age]`.
  At war: `exhaustion += 0.015` per year, `+0.08` per lost battle, `+0.15` per settlement lost.
  Each side sends one army every 8 years from its largest settlement within path cost 60 of an
  enemy settlement (size = 6 % of that settlement's people, removed only after `Units.spawn`
  succeeds). Battle on arrival: `size × tech[a] × rng.range(0.7, 1.3)` versus
  `0.05 × pop × (1 + walls) × tech[d]`; attacker wins → `Civ.capture` (`Civ.destroy(…, 'war')`
  if pop < 100) and the defender loses 15 % of its people; attacker loses → the army dies. Emit
  `battle`. **Peace** when either side's exhaustion reaches 1, after 60 years regardless, when a
  side has lost 40 % of its settlements, or by miracle/edict: `v = −0.1`, truce 40 years;
  exhaustion then decays 0.03/yr. At most 4 army/trader path searches per yearly tick.
- **Trade.** Every ~5 years a town+ with a market sends a `'trader'` unit to a partner within
  reach (cache routes per pair in `world._routes`). On arrival: `trade` toward 1, relations up,
  plague may hop (`Civ.infect`), and `road[]` along the land part of the path rises with traffic
  (cap 3; set dirty.roads). First caravan between a pair emits `trade:route`.
- **Nuclear exchange.** 1.5 % per year per warring pair with both sides Atomic or later, at most
  once per war: 2 strikes per side on the enemy's largest cities (`Civ.damage` 70 %, `rad` 0.8
  within 2 hops, `Life.ignite`, `Climate.addDust(0.10)` each, `witness` with kind `'nuke'`,
  faith 0, good 0), `vars.lastNuke = year`, emit `nuke` per strike, then immediate peace with a
  100-year truce.
- **Epithets.** `yearly` ends by calling `DM.Chronicle.updateEpithets(world)`.

### 3.9 `units.js` → `DM.Units`

```
Units.spawn(world, { type, culture, from, to = -1, dest, size, naval = false, data = {} }) → unit | null
        null when no path exists or 300 units are alive. Callers remove people from the source only after a non-null return.
Units.step(world, dtYears)             move every unit; handle arrivals and losses
Units.kill(world, unit, cause)         removes it and emits unit:died (with its last cell)
Units.near(world, cell, ang) → units within ang
Units.dirOf(world, unit, out) → current cell; writes the interpolated unit direction into out[0..2]
```

Speeds (cells/year on open land): settler 5, trader 7, army 5, prophet 4; × 2.5 on sea; roads
help. A unit whose next cell became impassable re-paths once (honouring its `naval`), then dies.
An army or trader whose target settlement is dead, or (army) whose cultures are no longer at
war, dies with cause `'disbanded'`. Units die on `lava > 0.2`. (Powers kills units caught by
lightning, meteors, quakes and waves using `Units.near` + `Units.kill`.) Arrival: settler →
`Civ.found(dest, …)` passing its origin's devotion and love (if the site is no longer valid it
joins the nearest live settlement of its culture, adding its people); trader, army, prophet →
`Society.onArrive`.

### 3.10 `powers.js` + `disasters.js` → `DM.Powers`

`powers.js`:

```
Powers.LIST                            array of power definitions (table below, as data), Powers.byId map
Powers.unlocked(world, id) → bool
Powers.cost(world, id, ang = def.ang) → Awe per use (click powers) or per real second (brush powers: def.cost × (ang / def.ang)²)
Powers.can(world, id, cell) → { ok, reason, why }
        reason: 'ok' | 'locked' | 'awe' | 'target' | 'phase';  why (when reason is 'target'): 'sea' | 'land' | 'settlement' |
        'barren' | 'crowded' | null.  `awaken` on a landmass with fewer than 600 habitable cells returns ok with why: 'small'.
Powers.use(world, id, cell, { ang, dt } = {}) → bool
        Click powers: one call per click; `ang` is ignored (the definition's ang is used) except for `seed`.
        Brush powers: called every frame while held, dt = real seconds since the previous call, ang clamped to 0.03–0.15.
        Spends Awe. Brush powers accumulate dt and, at most 4 times per real second, emit power:used, count one deed and
        call Society.witness once with the accumulated faith. Emits awe:short (rate-limited) and returns false when the
        god cannot pay. Never throws.
```

`disasters.js`:

```
Powers.start(world, type, cell, { power, ang, dur, natural, data } = {}) → disaster      the only way disasters are created
Powers.update(world, realDt)           real-time pieces: advances comet/meteor flights by realDt and resolves impacts (using world.rng);
                                       advances the rate-limit clocks. The live game calls it every frame (even when paused);
                                       headless code never calls it directly: Sim.advance does.
Powers.tick(world)                     sim-time pieces: eruptions and lava, tsunami fronts, storms, droughts
Powers.natural(world)                  yearly roll for natural events, scaled by world.settings.calamity
```

Power definition: `{ id, name, key, group: 'genesis'|'shape'|'sky'|'wrath'|'grace', brush: bool,
cost, ang, target: 'any'|'land'|'sea'|'settlement', unlock: { phase, age, awe }, wAng, faith,
desc }`. Unlocked when the world has reached `unlock.phase` **and** (`world.age ≥ unlock.age`
**or** `god.aweMax ≥ unlock.awe`). Genesis powers are available only in their phase. `key` is
the hotkey (Input and HUD read it; nobody else binds letters to powers). `wAng` and `faith` are
the witness radius and the devotion gained at the centre (per second for brush powers).

| id | name | key | group | kind | cost | ang | target | unlock | wAng | faith | good |
|---|---|---|---|---|---|---|---|---|---|---|---|
| `comet` | Comet | C | genesis | click | 0 | 0.03 | any | phase 'water' only | – | – | – |
| `seed` | Seed Life | G | genesis | click | 0 | 0.12 | land | phase 'life' only | – | – | – |
| `awaken` | Awaken | K | genesis / grace | click | 0, later min(250, aweMax) | 0 | land | phase 'people'; in history: free whenever no settlement or settler is alive, else Sail or 450 | – | – | – |
| `raise` | Raise Land | U | shape | brush | 6/s | 0.06 | any | from 'water' (free during genesis) | 2×ang | 0.02/s | −0.5 on owned land, else 0 |
| `lower` | Lower Land | J | shape | brush | 6/s | 0.06 | any | from 'water' (free during genesis) | 2×ang | 0.02/s | −0.5 on owned land, else 0 |
| `rain` | Rain | N | sky | brush | 5/s | 0.10 | any | history | 2×ang | 0.02/s | +0.6 on dry (moist < 0.3), burning, drought-struck or rain-praying land, else +0.1 |
| `bloom` | Bloom | B | sky | brush | 6/s | 0.08 | land | Bronze or 150 | 2×ang | 0.02/s | +0.4 |
| `lightning` | Lightning | L | wrath | click | 15 | 0 | any | history | 0.10 | 0.05 | −0.6 if it hurt them, else 0 |
| `quake` | Earthquake | Z | wrath | click | 60 | 0.08 | any | Bronze or 200 | 0.20 | 0.10 | −0.8 damaged, −0.2 others |
| `plague` | Plague | X | wrath | click | 80 | 0 | settlement | Iron or 300 | 0.12 | 0.05 | −0.7 |
| `meteor` | Meteor | M | wrath | click | 150 | 0.05 | any | Sail or 450 | 0.45 | 0.25 | −1 damaged, 0 others |
| `volcano` | Volcano | V | wrath | click | 220 | 0.04 | land | Industry or 600 | 0.40 | 0.25 | −1 damaged, 0 others |
| `heal` | Heal | T | grace | click | 40 | 0 | settlement | history | 0.10 | 0.12 | +1 |
| `inspire` | Inspire | I | grace | click | 60 | 0 | settlement | Bronze or 200 | 0.10 | 0.08 | +0.6 |
| `prophet` | Prophet | P | grace | click | 120 | 0 | settlement | Iron or 300 | 0.15 | 0.15 | +0.5 |
| `peace` | Omen of Peace | O | grace | click | 100 | 0 | settlement | Iron or 300 | both cultures | 0.10 | +0.8 |

An act with good ≤ 0 gives half faith to witnesses it did not harm. Natural events call
`witness` with a quarter of the faith and good = 0.

Effects:

- **comet**: up to 3 in flight; ≈ 1.4 s flight (`meteor:launched`), then a small crater and
  water: the first comet sets `seaLevel = −6.0` (water pools in the deepest basins); each later
  one adds `clamp(0.2 × (0.6 − seaLevel), 0.12, 1.0)` (4 comets pass the −3.5 gate, about 12
  reach 0; hard cap +1.5). `Terrain.touched`.
- **seed**: `Life.seed(world, cell, ang)`.
- **awaken**: needs `Civ.canSettle`. `Society.newCulture`, then `Civ.found(cell, culture, 30,
  'awaken')`. The first successful use in phase `'people'` calls `Sim.setPhase(world, 'history')`
  and sets `god.awe = max(god.awe, 60)`. Powers never writes `world.phase` itself.
- **raise / lower**: `Terrain.sculpt` at 1.5 units/s at the brush centre during genesis and
  0.2 units/s in history; in history the cost is × 4 while the brush centre is on owned land.
  A settlement whose centre goes under the sea is destroyed (`drowned`).
- **rain**: `wet` toward 1 within the brush, extinguishes fire, fertility slowly up; sets
  need.climate (the scheduler rate-limits it).
- **bloom**: `Life.bloom`.
- **lightning**: `Life.ignite`; a settlement at or next to the cell loses 5–12 % of its people;
  kills a unit there.
- **quake**: settlements within 0.08 rad lose 15–35 %; `Terrain.roughen`; under the sea it
  starts a tsunami.
- **plague / heal / inspire**: `Civ.infect(s, 0.6)` / `Civ.cure(s)` + `blessed = 30` /
  `inspire = 40`.
- **meteor**: ≈ 1.6 s flight, then `Terrain.crater` (0.05 rad, depth 1.6); settlements inside
  are destroyed (`meteor`), those within 2.5 × that radius lose 50 %; units die; fires at the
  rim; `Climate.addDust(0.25)`; in the sea it starts a tsunami instead of fires.
- **volcano**: `Terrain.cone` (0.04 rad, height 1.8), then a `'volcano'` disaster for ~12 years:
  lava wells up at the vent and flows to the lowest neighbour, igniting vegetation, destroying
  farms and urban cells, damaging settlements (`lava`); it cools into rock (`elev` rises via
  Terrain, making new land where it reaches the sea); dust +0.03/yr while it lasts; afterwards
  fertility +0.3 within 3 × the cone radius. Bound the lava field (≤ 400 cells).
- **prophet**: `Society.raiseProphet(s)`. **peace**: `Society.makePeace` for every war of that
  settlement's culture with an 80-year truce and relations +0.4.
- **Tsunami**: `data.front` expands over sea cells each tick up to `ang`; where the front meets
  a coast, land cells within a few hops and lower than the wave height (decaying with distance)
  are flooded: `wet = 1`, settlements there lose people by depth (destroyed with cause `wave`
  if overwhelmed), fertility up afterwards. Emit `tsunami:hit` (rate-limited).
- **Natural events** per century at calamity 1: lightning fires 6, storms 3 (a `'storm'`
  disaster drifting from sea to land, raising `wet`, minor damage), droughts 2 (6–12 years),
  quakes 1, plague 1 (only where a city exists), eruptions 0.4. Each emits the same events as
  its miracle twin with `natural: true`. **Protection**: no natural event is centred within
  0.3 rad of a live settlement while `stats.pop < 200` or before year 100, nor within 0.15 rad
  while fewer than 4 settlements live. Droughts set need.climate when they start and end.

### 3.11 `chronicle.js` → `DM.Chronicle`

```
Chronicle.attach()                     (re)subscribe to the Bus: keep the unsubscribe functions, call them, subscribe again, so
                                       there is exactly one subscription per handler even after DM.Bus.clear(). No boolean guard.
Chronicle.add(world, { type, text, cell = -1, culture = -1, weight = 1 }) → entry   (emits chronicle:entry)
Chronicle.yearStr(world, year = world.year) → 'Year 312'
Chronicle.epithet(world, cultureId) → e.g. 'the Rain-Giver'
Chronicle.updateEpithets(world)        recompute each living culture's epithet (chronicle it when it changes) and god.epithet
Chronicle.snapshot(world, force = false)   push a time-lapse frame if one is due (or forced)
Chronicle.tale(world) → { title, paragraphs: [string], stats: { … } }   end-of-game retelling (read-only: no rng from the sim stream)
```

Turns events into short prose in the voice of an old chronicle, using the cultures' own names:
foundings, tier-ups, ages, famines, plagues, wars, battles, prophets, edicts, wonders, trade
routes, schisms, prayers answered and unanswered, disasters natural and divine, climate
milestones, the Ark. Several templates per event; rate-limit minor entries. Epithets come from
what the god has actually done to that culture (`culture.seen`, `god.deeds`) and how its people
feel (`Society.regard`): mostly rain and bloom → 'the Rain-Giver', 'the Green Hand'; meteors →
'She Who Burns the Sky', 'the Hammer'; floods → 'the Drowner'; nothing for centuries → 'the
Silent One'. **Snapshots**: `{ year, heat, seaLevel, pop, biome: Uint8Array(n), owner:
Int8Array(n) (culture id of the owning settlement, −1 none), urban: Uint8Array(n)
(round(urban × 255)) }`. Sim calls `Chronicle.snapshot(world)` in every yearly group and
`snapshot(world, true)` on every phase change; Chronicle alone decides when one is due
(`year ≥ vars.snapNext`, every `vars.snapEvery` = 25 years; past 200 frames drop every second
history frame and double the interval).

### 3.12 `sim.js` → `DM.Sim`

```
Sim.TICK = 0.25          Sim.REAL_PER_TICK = 0.125
Sim.create({ seed, name, L = 6, settings }) → world     State.create + Terrain.generate; phase 'molten'; ends with Sim.use(world)
Sim.use(world)                         make a (new or loaded) world the active one: DM.world, Chronicle.attach(), Sim.rederive
Sim.rederive(world)                    Terrain.derive → Climate.update → Hydro.update → Life.classify → Life.habitability →
                                       Life.rebuild → Civ.rebuild (and Climate.calibrate first if history has begun and vars.seaBase is missing)
Sim.tick(world)                        advance one tick (any phase). Never calls Powers.update.
Sim.advance(world, ticks = 1)          for each tick: Powers.update(world, Sim.REAL_PER_TICK); Sim.tick(world). The ONLY way headless
                                       code (simrun, policies, DM.debug.ff, dev viewer, tests) advances time.
Sim.setPhase(world, phase) → bool      the ONLY writer of world.phase. Runs the entry actions, then emits `phase`, then forces a snapshot.
Sim.genesis.progress(world) → { phase, value, canAdvance, hint, stats: { ocean, veg } }
Sim.genesis.next(world) → bool         'water' → 'life' needs seaLevel ≥ −3.5; 'life' → 'people' needs cover.veg ≥ 0.25
Sim.genesis.skip(world, { sea = 0, cell = -1 } = {})   straight to 'history': heat 0, set the sea, rederive, Life.fill, rederive
                                       biomes, wake people at `cell` (or Civ.bestSite). Sim.create + skip ≤ 800 ms at L = 6.
Sim.refreshStats(world)
Sim.checkEnding(world) → ending | null
Sim.score(world) → { total, parts: { … } }
```

**Phase entry actions** (`setPhase`): `'water'`: `heat = 0`. `'life'`:
`vars.lifeBoost = TUNE.sim.lifeBoost` (≈ 25). `'people'`: `vars.lifeBoost = 1`. `'history'`:
`vars.lifeBoost = 1`, `Climate.calibrate(world)`, `year = 0`, `vars.t0 = tick`. `'ended'`: none.

**genesis.progress**: `value` 0..1: molten `1 − heat`; water `sat((seaLevel + 6.5) / 3)`; life
`sat(cover.veg / 0.25)`; people 0. `hint` is a key, not prose: `'cooling' | 'water-more' |
'water-ok' | 'life-more' | 'life-ok' | 'people'`. `stats.ocean` = share of cells that are sea.

**Tick schedule.** `Sim.tick` skips any step whose namespace is undefined (the only place such
guards are allowed), so a partial tree still ticks.

- `'molten'`: `heat −= 1/60`; at 0 → `setPhase('water')`. Nothing else.
- `'water'`, `'life'`, `'people'`: `Powers.tick` → `Life.burn` → `Life.step(tick % P, P)` with
  P = 2 in `'life'` and 8 otherwise → `Climate.stepIce` when `tick % 20 === 10` → one heavy
  pass (below). No units, civ, society, `Climate.yearly`, natural events or ending checks.
- `'history'`, `'ended'`: `year += TICK` → `Powers.tick` → `Life.burn` → `Units.step(TICK)` →
  `Life.step(tick % 8, 8)` → when `(tick − vars.t0) % 4 === 0`, the **yearly group**:
  `Civ.yearly`, `Society.yearly`, `Climate.yearly`, `Powers.natural`, `Sim.refreshStats`,
  `Chronicle.snapshot`, `Sim.checkEnding` → `Climate.stepIce` when `tick % 20 === 10` → on
  ticks that are **not** yearly ticks, one heavy pass.
- **Heavy pass** — run at most ONE per tick, the first that is due:
  (1) `need.derive` → `Terrain.derive`;
  (2) (`need.climate` and ≥ 8 ticks since the last) or ≥ 40 ticks since the last →
  `Climate.update`;
  (3) `need.hydro` and ≥ 8 ticks since the last → `Hydro.update`;
  (4) `need.life` → `Life.classify` on this tick and `Life.habitability` on the next.

**Endings.** `world.ending = { type: 'ark' | 'silence', mood, culture, settlement, year }`.
`checkEnding` returns null once `world.ending` is set. Ark: when `vars.ark` exists →
`type: 'ark'`, mood from `Society.regard(world, vars.ark.culture)`: `'beloved'` (devotion ≥ 0.35
and love ≥ 0.2), `'feared'` (love ≤ −0.2), `'forgotten'` (devotion < 0.2), else `'parting'`.
Silence: no live settlement and no settler unit for 50 years **and** `stats.peakPop ≥ 1000` →
`type: 'silence'`, mood `'ash'` if `vars.lastNuke` is within 200 years, else `'quiet'`. Below
that population, emit `people:lost` once per extinction instead (the player may `awaken` again
for free). Setting an ending: write `world.ending`, `setPhase('ended')`, emit `ending` with that
object. Score: peak population, ages reached, cultures alive, wonders, years survived, living
world (vegetation cover), with the ending as a multiplier.

**Headless tools** (owned by the sim.js author): `tools/simrun.js`:
`node tools/simrun.js --seed 7 --years 4000 --policy passive|kind|cruel|chaotic [--L 6] [--calamity 1] [--json] [--hash] [--seeds 1-10 --summary]`
prints a timeline every 100 years (population, settlements, cultures, highest age, anomaly,
seaLevel, awe and Awe income, share of years at the Awe cap, counts of key events), the ending,
peak anomaly and dust, the largest single fire, timing (mean and max tick ms), any NaN found,
and with `--hash` a fingerprint (two runs must match). `--summary` prints a pass/fail table
against §4. `tools/policies.js` holds the scripted players (they call `Powers.use`, with
`dt = Sim.REAL_PER_TICK` for brush powers, between `Sim.advance` calls).

### 3.13 `balance.js`

Loaded last. Empty at first (`// balance overrides`). After integration it is the only file the
balance owner edits: `Object.assign(DM.TUNE.civ, { … })`. Module authors do not retune afterwards.

## 4. Balance targets (verified with simrun on ≥ 6 seeds; owner: the simulation integrator)

- Undisturbed (`passive`) worlds reach Bronze in years 300–700, Iron 700–1,300, Sail
  1,200–2,000, Industry 1,900–2,800, Atomic 2,500–3,400, Space 3,000–3,900, and launch an Ark
  between 3,300 and 4,500. No `passive` world loses its people before year 500 (10 of 10 seeds).
- Reference curve for isolated tuning: world population ≈ 30 at year 0; 2,000 at 500; 20,000 at
  1,000; 200,000 at 2,000; 2 million at 3,000; 5 million at 3,500. Live settlements ≈ 1, 6, 20,
  45, 70, 90 at those years (acceptable: 8–40 at year 1,000; 25–120 at 3,000). Living cultures
  at year 2,000: 2–6.
- Climate: a `passive` world peaks between +2 and +5 °C; the sea rises by 0.2–0.5.
- Endings: `kind` reaches `'beloved'` on at least 6 of 10 seeds; `passive` mostly ends
  `'forgotten'` or `'parting'`; a `cruel` player can drive a world to silence, and one that
  stops short of extinction ends `'feared'`.
- Awe at 1× (2 years per real second): a small miracle (≤ 40) every 5–15 s in the Stone age
  and every 3–8 s later; a great one (≥ 150) every 25–60 s from the age it unlocks. An active
  policy sits below 90 % of aweMax in at least 60 % of years.
- Performance per §0.8.
