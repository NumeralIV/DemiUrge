# DEMIURGE — Build plan and acceptance checks (v2)

Each engineer owns the files listed for their assignment and nothing else. The specs are the
contract: `FOUNDATION.md`, `SPEC-SIM.md`, `SPEC-RENDER.md`, `SPEC-UI.md`. This file says who
builds what, in which stage, and what must be demonstrated before reporting "done".

## Rules for everyone

1. **Ownership.** Edit only your assigned files (a placeholder already exists for each: replace
   its contents). Many engineers work in this folder at the same time. If you need something
   changed in a file you do not own, work around it and report it. Never edit the docs, the
   tools, `js/sim/schema.js`, `js/game.js`, `index.html`, `dev/play.html` or `css/base.css`.
2. **No dependencies, no network, no git, no npm.** Node v24 and Microsoft Edge are installed.
3. **Contracts are binding.** Implement the documented API exactly. If the spec is wrong or
   impossible, implement the nearest sensible thing and list the deviation in your report.
4. **Other modules may be missing, unfinished or mid-edit** while you work (a placeholder
   defines nothing, so `DM.X` is `undefined`). Call collaborators through `DM.X.fn` at call
   time. Your module must not crash at load time if another file is absent, and your own tests
   must not depend on anyone else's module (see Tools).
5. **Prove it.** Run your acceptance checks and include the actual numbers and outputs in your
   report. Look at your own output: sim engineers render PNG maps with `tools/mapimg.js`;
   rendering and UI engineers take screenshots with `tools/shot.js` and inspect them with the
   image-reading tool. "It compiles" is not evidence.
6. **Report format** (your final message): what you built; deviations from the spec; commands
   run with pass counts; acceptance results with numbers; screenshot / map file names; known
   weaknesses; anything the lead must decide. No padding.
7. Keep temporary files out of the tree. Screenshots and maps go in `shots/` with your module
   name as a prefix (`shots/hydro-rivers.png`). Dev-only pages go in `dev/`, browser scenarios in
   `tests/browser/<module>.scn.js`.

## Tools

- `node tools/test.js <name>` runs `tests/*<name>*.test.js`, each file in its own process.
  `tools/t.js`: `T.test`, `T.ok`, `T.eq`, `T.near`, `T.throws`, `T.stub(obj, name, value)`
  (auto-restored), `T.watchEvents()` (checks every emitted event against `DM.EVENTS`).
- `require('../tools/load.js')({ only: ['yourmodule'] })` loads the core, `state.js`,
  `schema.js` and just the files you name. **Use this form in your own tests**; stub every other
  collaborator with `T.stub`.
- `tests/helpers/world.js` builds synthetic planets using only the foundation:
  `makeWorld({ L, seed, land, basics })`, `fillBasics(w)` (simple stand-ins for temp, moist,
  biome, veg, fauna, hab, slope, coast, fresh), `bestHab`, `findLand`, `checkFinite`,
  `capture(fn)` (collect Bus events), `hash(world)` (fingerprint for determinism checks).
- `tools/mapimg.js` writes equirectangular PNG maps from Node: `elev`, `biome`,
  `field(world, name, file, min, max)`, `owner`, `custom`. Use it and look at the result.
- `tools/shot.js` (read `docs/TOOLS.md`) drives headless Edge on the real GPU: load a page,
  evaluate JS, send mouse / touch / key events, take screenshots, read pixels with `decodePng`.
  Page errors, failed resource loads and WebGL errors are captured. Many instances can run at
  once. Sessions end after 2 minutes unless you raise `--timeout`.
- `dev/view.html` (Stage B onward; SPEC-RENDER §12) renders a world with whatever render
  modules exist. `dev/play.html?bare=1` is the real game shell plus `DM.debug.fixture()` (a
  populated world without the sim) and `DM.debug.new()` (the real sim).
- `tools/server.js` is optional (the game and the rig run from `file://`). If you need it, use
  `start({ port: 0 })` from Node so parallel runs do not collide.

## Stage A — independent modules (all in parallel)

| # | assignment | files | spec |
|---|---|---|---|
| A1 | names | `js/sim/names.js`, `tests/names.test.js` | SIM §3.1 |
| A2 | terrain | `js/sim/terrain.js`, `tests/terrain.test.js` | SIM §3.2 |
| A3 | climate | `js/sim/climate.js`, `tests/climate.test.js` | SIM §3.3 |
| A4 | hydro | `js/sim/hydro.js`, `tests/hydro.test.js` | SIM §3.4 |
| A5 | life | `js/sim/life.js`, `tests/life.test.js` | SIM §3.5 |
| A6 | path + units | `js/sim/path.js`, `js/sim/units.js`, `tests/path.test.js`, `tests/units.test.js` | SIM §3.6, §3.9 |
| A7 | civ (lifecycle) | `js/sim/civ.js`, `tests/civ.test.js` | SIM §3.7 first half |
| A8 | economy | `js/sim/economy.js`, `tests/economy.test.js` | SIM §3.7 second half |
| A9 | society (belief) | `js/sim/society.js`, `tests/society.test.js` | SIM §3.8 first half |
| A10 | diplomacy | `js/sim/diplomacy.js`, `tests/diplomacy.test.js` | SIM §3.8 second half |
| A11 | powers | `js/sim/powers.js`, `tests/powers.test.js` | SIM §3.10 first half |
| A12 | disasters | `js/sim/disasters.js`, `tests/disasters.test.js` | SIM §3.10 second half |
| A13 | chronicle | `js/sim/chronicle.js`, `tests/chronicle.test.js` | SIM §3.11 |
| A14 | sim core + headless runner | `js/sim/sim.js`, `tools/simrun.js`, `tools/policies.js`, `tests/sim.test.js` | SIM §3.12, §4 |
| A15 | render core | `js/render/gl.js`, `js/render/post.js`, `js/render/renderer.js`, `dev/view.html`, `dev/view.js`, `tests/browser/rendercore.scn.js` | RENDER §0–5, §11, §12 |
| A16 | fixtures | `dev/fixtures.js`, `tests/fixtures.test.js` | RENDER §12, SIM §1 |
| A17 | models | `js/render/models.js`, `tests/models.test.js`, `dev/models.html` | RENDER §8 |
| A18 | towns | `js/render/towns.js`, `tests/towns.test.js` | RENDER §9 (Towns) |
| A19 | camera + input | `js/ui/camera.js`, `js/ui/input.js`, `tests/camera.test.js`, `dev/input.html`, `tests/browser/input.scn.js` | UI §1, §2 |
| A20 | icons | `js/ui/icons.js`, `dev/icons.html` | UI §3 |
| A21 | audio engine | `js/audio/audio.js`, `dev/audio.html`, `tests/browser/audio.scn.js` | UI §7 |
| A22 | music | `js/audio/music.js`, `dev/music.html`, `tests/browser/music.scn.js` | UI §7 |
| A23 | save | `js/save.js`, `dev/save.html`, `tests/browser/save.scn.js` | UI §6 |

### Acceptance checks, Stage A

**A1 names.** Deterministic for a given rng. For 8 languages × 300 names each: every name 3–14
characters, pronounceable (no run of 4+ consonants or 3+ identical letters), ≥ 90 % distinct
within a language, and languages are distinguishable (print 10 place names from each of 6
languages in your report). A daughter language shares recognizable roots with its parent
(print parent/daughter pairs). `lang` survives `JSON.parse(JSON.stringify(lang))` and produces
the same names afterwards. 10,000 names in < 150 ms.

**A2 terrain.** For 20 seeds at L = 5 and 3 seeds at L = 6: land fraction at sea level 0 within
36–42 %; elevation within [−7, 8.6]; ≥ 25 % of land below +0.6; ≥ 2 % of land above +3.5; at
least 2 landmasses each ≥ 3 % of the planet and at least 3 further islands; identical output for
identical seeds. `generate` < 500 ms at L = 6. `sculpt` raises the centre by `amount`, falls
smoothly to 0 at `ang`, touches nothing outside, clamps, and sets the flags. `crater` leaves a
bowl with a rim above the old level; `cone` a peak with a summit pit; `roughen` creates no
one-cell pits. `derive` matches a brute-force slope, BFS coast distance and landmass labelling
(`_landId`: every land cell labelled, ids by descending size). Render `M.elev` maps for 4 seeds,
look at them, and describe what you see (continents with mountain belts, shelves and islands, or
noise?). Iterate until they look like a world.

**A3 climate.** On helper worlds and on a hand-built planet (one continent with a north–south
mountain wall): equator warmer than poles by 40–60 °C; lapse measured from `Climate.datum`
(a world at `seaLevel = −9` is NOT frozen: mean land temperature within 3 °C of the same world
at sea level 0); the windward side of the wall is clearly wetter than the lee; coasts wetter
than deep interiors; a planet with no sea has `moist` < 0.05 everywhere; one new mountain does
not change `moist` on the far side of the planet (fixed `RAIN_REF`); `dust` cools; sea ice can
form (unclamped sea temperature). `update` ≤ 12 ms at L = 6 (report). `calibrate` sets a finite
`seaBase` and `iceRef ≥ 50`. `yearly`: does nothing but decay dust in genesis phases and while
`vars.seaBase` is undefined; a world created with phase `'history'` and empty `vars` survives
400 `yearly` calls with a finite `seaLevel`; sustained `emit` at the documented per-city rate
reaches the documented equilibrium (40 cities of 20,000 → co2 ≈ 80, anomaly ≈ +2.6), ice
shrinks, the sea rises within `seaBase ± 0.6` at ≤ 0.005/yr; each milestone fires once. Render
temperature and moisture maps for 2 seeds: wet equatorial belt, dry subtropics, rain shadows.

**A4 hydro.** On helper worlds, 10 seeds: following `down[]` from every land cell reaches a sea
cell without cycles; `flow` never decreases downstream and equals the sum of upstream `moist`;
`water ≥ 0`, only in depressions of ≥ 3 cells or depth ≥ 0.25, never on sea cells; one-cell pits
make no lake; river entries are `{ cells, flow }` of equal length ≥ 2, chains of neighbours,
ending on the first sea cell, lake cell or confluence; `fresh` equals a brute-force BFS. Works
with no sea and with no land. Sets `need.life`. `update` ≤ 15 ms at L = 6. Render an elevation
map with rivers for 3 seeds: several long branching rivers per continent reaching the coast,
lakes in basins, no rivers along ridges.

**A5 life.** `classify`: only SEA/SHALLOWS/SEAICE on sea cells and never on land; each land
biome appears across 5 helper worlds with real-looking proportions (print the histogram).
Vegetation: a barren planet stays barren for 2,000 ticks (apart from nothing: drifting seeds
need a source); after `seed` at one cell, with `vars.lifeBoost = 25` on the `Life.step(t % 2,
2)` schedule the green reaches ≥ 25 % mean veg/capacity on its continent within 240 ticks and
keeps spreading; the spread front is organic, not a circle (map it); an unseeded island greens
over centuries via drifting seeds; forests regrow after clearing in roughly 100–160 years at
boost 1; `Life.step` honours dt = parts × TICK (same result per year for parts 2 and 8, ±10 %).
`fill` sets veg to capacity. Fire with the documented rule: in wet forest it dies within a few
cells; in dry forest most fires stay under ~200 cells; under drought it runs; never enters sea /
ice / lake / bare cells; `wet` stops it; never more than 1,500 burning cells; `_fires` matches
`fire[] > 0` and `rebuild` restores it. Lake cells: capacity 0, hab 0. Unowned farm/urban decay;
`rad` decays; sea cells are zeroed. `habitability` 0 on sea, ice, rock, snow; river valleys in
dry land beat the surrounding desert; `_landHab` correct. `step` for one slice of 8 ≤ 0.6 ms,
`classify` ≤ 6 ms at L = 6. Render biome and habitability maps.

**A6 path + units.** `Path.find` returns optimal-cost paths (compare with a plain Dijkstra in
the test, 200 random pairs at L = 4) for `naval` false, `'coastal'` and true; respects
impassable cells, lake and embark costs and `maxCost`; `[from]` when from = to; `null` when
unreachable. `Path.reach` visits in non-decreasing cost and stops when the callback returns
false. Worst-case `find` at L = 6 ≤ 25 ms (report). Units: created through
`State.newUnit`; a settler follows its path at the documented speed, flips `sea` on sea cells,
arrives and calls `Civ.found` (stubbed) with its origin's devotion and love, joins the nearest
settlement when the site is invalid; re-paths once when blocked (honouring `naval`), then dies;
armies and traders disband per the rules; `spawn` returns null without a path or at the cap;
events carry `{ id, type, culture, cell }`; `dirOf` returns unit-length directions continuous
between cells.

**A7 civ (lifecycle).** Every function in the `civ.js` list exists with the documented
behaviour: `found` (validity, 160 cap, initial devotion/love by origin, first settlement becomes
capital, immediate claim and K via `Civ.capacity` — stub it when `economy.js` is absent),
`claim` (no overlap, contested cells to the nearer centre, drops sea cells), `rebuild` (restores
`_cells` without changing `owner`), `destroy` / `capture` / `setCulture` (events, capital
re-election, launch/ark reset), `electCapital`, `infect` / `cure` (immunity, events), `damage`,
`at`, `near` (order, ties), `canSettle` reasons, `siteScore`, `bestSite` (largest habitable
landmass), `ageProgress`. All entities come from `State.newSettlement`. No NaN. Render an owner
map of 12 hand-founded settlements.

**A8 economy.** With `civ.js` stubbed or real: the five gate checks on a synthetic uniform
territory (fert 0.6, suit 0.8, 65 % arable): camp K ≈ 131; village at Bronze ≈ 638; town at Iron
≈ 2,104; city at Industry ≈ 15,700; metropolis at Space ≈ 105,000 (± 15 %). K is independent of
`farm[]` and of population (halve K: the settlement declines ≤ 12 %/yr and stabilises, no death
spiral; K = 0 never produces NaN). Growth, famine counter and event, plague deaths / fading /
spread via `Civ.infect`, countdown of blessed / inspire / immune, tier hysteresis (a village
oscillating around 60 does not flap), buildings by their conditions with events, farms follow
the documented rule, `urban` rings. Expansion: at most 2 searches per year, candidates respect
`_ownDist` ≥ 7 and reserved destinations, party size, people removed only after a successful
`Units.spawn` (stub it to teleport: call `Civ.found` at the destination), cooldown set even on
failure, `'coastal'` from Bronze, naval with a port from Sail. Sea-rise hazard evacuates 60 %.
Research: a nominal culture (12 settlements, curiosity 0.5) crosses the ages near 520 / 1,000 /
1,550 / 2,200 / 2,750 / 3,200; print the years; emits `culture:age` and `world:age`. A
300-year run from one camp on the helper world (teleporting settlers): population 60 within
60–200 years, several settlements by year 300, no NaN. `yearly` ≤ 3 ms with 100 settlements at
L = 6.

**A9 society (belief).** `newCulture` (factory, names from `DM.Names` or a stub, colour slot,
daughter traits/language, event). `witness`: the documented formulas exactly (jaded, saturation,
love, `culture.seen`, mercy/wrath, prayer doubling and `prayer:answered`,
`miracle:witnessed` ids). Devotion drifts to the documented baselines by age and piety; fear
bonus before Atomic and penalty after. `aweMax` and income match the formula (one camp with
devotion 0.6 ≈ 0.70/yr; print three more cases). Prayers start, end, expire with the documented
penalties, at most 6. Housekeeping marks dead cultures once and cleans up. Prophets: raised,
travel as units (stub `Units.spawn`), raise devotion, die, clear the edict; each edict has its
documented effect; `setEdict` rules (needs prophet, 20-year change limit). Schism rules produce
a daughter culture from estranged settlements and never more than 6 living cultures. The Ark
progresses only in a Space-age capital with a launch site, sets `vars.ark` and emits
`ark:launched` exactly once. `regard` returns population-weighted means and the right mood at
each threshold. No NaN.

**A10 diplomacy.** `rel` / `peek` (peek never creates), symmetric storage key. On hand-built
scenarios (two cultures with adjacent territories on a helper world): relations follow the
documented formula; war is declared under the documented conditions (and not under a `peace`
edict or truce); armies are spawned through `Units.spawn` (stub) every 8 years with people
removed only on success; `onArrive` resolves battles both ways with the documented strengths,
captures or razes, emits `battle`; exhaustion rises as documented and peace follows (also the
60-year cap and the 40 % rule); `makePeace` works at any time and sets the truce; trade raises
`trade`, relations and `road` (dirty.roads), emits `trade:route` once per pair, may carry plague
via `Civ.infect`; nuclear exchange at the documented risk, once per war, with `rad`, dust,
`vars.lastNuke`, `nuke` events and a 100-year truce; at most 4 path searches per year. Two
aggressive neighbours over 600 years: at least one war starts and ends (print the timeline).
No NaN.

**A11 powers.** `LIST` equals the spec table field for field (ids, names, keys, groups, kinds,
costs, angs, targets, unlocks, wAng, faith). `unlocked` by phase / age / aweMax; genesis powers
only in their phase; `awaken` free when nobody is alive. `cost` scales with brush area; × 4 for
sculpting on owned land in history. `can` reasons and `why` for every target type, including
`'small'`. `use`: spends Awe; brush powers emit `power:used`, count deeds and call `witness` at
most 4 times per real second with accumulated faith (independent of frame rate: 60 fps and
144 fps give the same totals ± 5 %); `awe:short` rate-limited; never throws on bad cells. Instant
effects verified on a helper world with collaborators stubbed (`T.stub`): seed, sculpt rates by
phase, rain (wet, fire out, need.climate), bloom, lightning (ignite, damage, unit kill), quake
(damage, roughen, tsunami via `Powers.start` when under the sea), plague / heal / inspire
(through `Civ.infect` / `Civ.cure`), prophet, peace, awaken (culture + camp of 30, first use in
`'people'` calls `Sim.setPhase` and sets Awe to 60; never writes `world.phase`), comet and
meteor and volcano hand off to `Powers.start` (stub when `disasters.js` is absent). Witness
calls carry the table's `wAng`, faith and good. No NaN.

**A12 disasters.** `start` creates disasters through `State.newDisaster` with the documented
field meanings. `update`: comet and meteor flights count down only by the dt passed in; after
`Sim.advance(world, 16)`-equivalent (16 × `update(0.125)`) a meteor has landed: crater via
`Terrain.crater` (stub or real), settlement at ground zero destroyed, 50 % damage in the outer
ring, units killed, fires, dust +0.25, `meteor:impact`; in the sea it starts a tsunami. Comets:
the first sets the sea to −6.0, later ones follow the documented step (4 pass −3.5, ~12 reach
0, cap +1.5), at most 3 in flight. `tick`: a volcano builds a cone, lava flows downhill, ignites,
destroys farms/urban, damages settlements, cools into rock, makes a new island when started in
shallow sea, stays ≤ 400 cells, ends with ash fertility and `volcano:end`; a tsunami front
(`data.front`) expands over sea only and floods a low coast with the documented effects;
storms drift and wet the land; droughts start/end with events and need.climate. `natural`:
0 events in 2,000 years at calamity 0; rates at calamity 1 within ± 40 % of the table over
5,000 years; protection rules hold. Every documented event fires. `tick` ≤ 0.5 ms with 3 active
disasters. Render before/after elevation maps for a meteor and a volcano.

**A13 chronicle.** Emit every sim event from SPEC-SIM §2 with realistic payloads against a
helper world holding a few named settlements and cultures (built with the factories): each
produces sensible prose with real names, never `undefined`, `NaN`, `[object Object]` or a raw
id; repeated events vary in wording; minor entries are rate-limited; wording never draws from
`world.rng` directly (hash unchanged by chronicle activity). `attach()` works after
`DM.Bus.clear()` and never double-subscribes. Epithets change with what a culture has seen
(print the epithet after simulated careers: only rain and bloom; only meteors; floods; silence
for 800 years; mixed) and differ between a blessed culture and a struck one; `god.epithet`
follows the most populous culture. Snapshots: due logic by `vars.snapNext`, forced on phase
change, thin out past 200, carry `heat` and `urban` scaled to 0..255. `tale` returns a title and
4–8 paragraphs that mention the world's actual history and does not disturb the sim stream.
Print 40 sample entries in your report.

**A14 sim core.** With every other sim module stubbed by call-recording stubs, then again with
whatever real modules exist when you finish: `setPhase` is the only writer of `phase`, runs the
entry actions, emits `phase` and forces a snapshot; the per-phase tick tables and the
one-heavy-pass scheduler match §3.12 exactly (verify from the call log: never two heavy passes
in one tick, never on a yearly tick, classify then habitability on consecutive ticks, climate
and hydro at least 8 ticks apart); `advance` calls `Powers.update(0.125)` before each tick;
`tick` assigns `DM.world`; two worlds with the same seed advanced alternately end with equal
hashes; `rederive` order; `genesis.progress` values and hint keys; `genesis.next` gates;
`genesis.skip` yields a `'history'` world with one settlement, calibrated climate and Awe 60;
endings (ark moods at each threshold, silence quiet/ash, `people:lost` below 1,000 peak
population, no second ending). `tools/simrun.js` works with every policy and flag and prints the
timeline; include its output for one seed in your report (say plainly what misbehaves because
other modules are unfinished).

**A15 render core.** `node tools/smoke/smoke.js` still passes. `tests/browser/rendercore.scn.js`
proves, in `dev/view.html` with **no** planet / sky / objects / fx module present (use
`only=`), a built-in test scene (a lit grey sphere of radius 100 with a few boxes and an
emissive one): Frame block std140 offsets equal `gl.getActiveUniforms(..., UNIFORM_OFFSET)`
(report them); a program including every chunk compiles; a failing shader's error contains
numbered source; shadow pass (boxes cast shadows on the sphere; no feedback-loop errors; unit 7
bound as specified); `shade` + `atmosphere`; MSAA HDR scene target; bloom only on the emissive
box (a sun-lit white box does NOT bloom); tilt-shift when zoomed in; tonemap; fallbacks without
float targets and at each quality level (force each with a URL flag and screenshot it);
`Renderer.project` then `Renderer.pick` round-trips within 0.5 units at three zooms;
`showSnapshot`, `freeze` (two frozen runs give PNGs with mean absolute difference < 1/255),
`capture` (> 10 kB); module isolation (a deliberately throwing fake module is disabled and the
frame still renders); zero entries in `page.errors`. Screenshots: orbit, mid, close, night side,
terminator. Report frame cost measured around `gl.finish()`.

**A16 fixtures.** `tests/fixtures.test.js` (Node): `Fixtures.world()` with only the foundation
and schema loaded, and again with every existing sim module loaded, yields a world where every
entity has exactly the fields of SPEC-SIM §1 (compare key sets with the factories' output),
every river is `{ cells, flow }` with equal lengths ≥ 2 and neighbouring cells, every unit path
is a chain of neighbours, `owner` / `_cells` agree, `_fires` matches `fire[]`, all listed
content is present (every tier, every age across 3 cultures, every building flag, each disaster
type, statuses, prophet + edict, war + trade relation, ≥ 40 chronicle entries, ≥ 12 snapshots),
`checkFinite` passes, and it survives `State.serialize` → JSON → `deserialize`. Each
`Fixtures.ending` kind sets a valid ending. Deterministic for a seed. Builds in < 1.5 s at L = 6.
Render owner and biome maps of the fixture and look at them.

**A17 models.** Every required name exists; triangle limits respected (print the table);
normals unit length and facing outward; indices in range; every model reaches y ≤ −0.15 and
`height` = max y; colours in 0..0.85. `dev/models.html`: a gallery that draws every model lit on
a grid with its name (its own minimal WebGL2 code), with tinted variants (culture colours on
roofs / sails / banners) and emissive windows on/off. Screenshot it, look, and iterate until
each model reads clearly as what it is at the size it will appear (silhouette first).

**A18 towns.** Node tests: `layout` is deterministic per settlement id; counts within the tier
limits; every model name exists in the SPEC-RENDER §8 list; nothing on sea or lake cells except
piers and boats; instances do not overlap grossly (minimum spacing); every building flag in `b`
produces its model; launch site shows gantry + rocket scaled by `ark`; dead → ruins; anchors
set for chimneys, campfires, gantry, pier; culture tint and age-appropriate house models.
Plot top-down plans (PNG via `tools/mapimg.js` `encodePNG`, one image with a camp, village,
town, city and metropolis across ages) and look at them: organic clusters, a centre, streets or
rings, not a grid of dots.

**A19 camera + input.** Node tests: distance at zoom 0 = the documented formula for 16:9 and
390×844 (± 1 %); height 6 above the ground at zoom 1; never below ground + 1.5 given a
`groundAt` with a +12 mountain; `near` / `far` formulas; no NaN in 10,000 random orbit / zoom /
turn steps across both poles; `view()` basis orthonormal; the direction under the cursor is
preserved by a drag (± 2 px) at zoom 0.2 / 0.6 / 0.9; `flyTo` ends exactly on target; heading
preserved through a pole crossing. `tests/browser/input.scn.js` on `dev/input.html` (stub
`Game` / `Renderer` / `Powers` / `Screens` that log calls): unarmed drag orbits, wheel zooms
toward the pointer, click selects; click power: tap fires, drag past threshold orbits and does
not fire; brush power: held press applies every frame, a second finger ends the stroke, the
100 ms touch delay holds; dashed ring and toast on an invalid target; hotkeys come from
`Powers.byId[id].key`; `Screens.current` blocks input; no page scroll, no context menu.

**A20 icons.** Every required name returns valid SVG and an unknown name a neutral dot;
`dev/icons.html` shows all of them at 20, 28 and 40 px on the ink background with labels;
screenshot and inspect: each recognizable, consistent in weight and style.

**A21 audio engine.** `tests/browser/audio.scn.js` on `dev/audio.html`: `Audio.renderTest` for
every scenario (10 s each, with `DM.Music` present or absent): `nan` false, `clipped` false,
peak ≤ 0.98, rms > 0.005, sub-bass share < 25 %, scenarios measurably different from each
other (print the table). `play` for every one-shot name and every sim event with the context
missing or suspended does not throw. No sound before a user gesture; `suspend` works; volumes
respond to settings; `Audio.buses` exist; zero page errors.

**A22 music.** `tests/browser/music.scn.js` on `dev/music.html` (own OfflineAudioContext
harness; `Music.init(ctx, outNode)` must not need `DM.Audio`): 30 s renders for each age 0–6,
for genesis, for war/disaster, and each stinger: no NaN, no clipping, rms > 0.003 and < 0.25,
ages measurably different in spectrum, note density sparse (print notes per minute), different
seeds give different scales. No scheduling drift over a simulated 10-minute `update` run; no
node leak (voices are stopped and disconnected).

**A23 save.** `tests/browser/save.scn.js` on `dev/save.html`, over `file://` and over http
(`start({ port: 0 })`): write a fixture-like world with entities and snapshots, reload the page
within the same browser session, read it back: `State.serialize` JSON equal, snapshots
byte-identical; `meta` correct; corrupt and missing saves → `null`; settings persist, merge new
defaults, reject garbage, honour prefers-reduced-motion defaults; works when IndexedDB is
unavailable (hide `indexedDB`). Report the stored size.

## Stage A-int — simulation integration (after A1–A14 are verified)

One engineer owns **all of `js/sim/*` except `schema.js`, plus `tools/simrun.js`,
`tools/policies.js` and `tests/integration.test.js`** and makes the whole simulation run as one
system: full `node tools/test.js` green; `simrun` for 6 seeds × 4,000 years with `passive` and 3
seeds each with `kind`, `cruel`, `chaotic` completes with no exception and no NaN; determinism
(`--hash` twice) and a mid-run save/load round trip; mean tick ≤ 2.5 ms, worst ≤ 30 ms at L = 6;
the balance targets in SPEC-SIM §4 met, or the gap quantified, by editing `js/sim/balance.js`
(and fixing rules that are wrong). Render owner / biome maps at years 500, 1,500, 3,000 for one
seed and inspect them. Report a per-seed table.

## Stage B — modules that need the render core (in parallel, after A15–A20 and A23)

| # | assignment | files | spec |
|---|---|---|---|
| B1 | planet | `js/render/planet.js`, `tests/browser/planet.scn.js` | RENDER §6 |
| B2 | sky | `js/render/sky.js`, `tests/browser/sky.scn.js` | RENDER §7 |
| B3 | objects | `js/render/objects.js`, `tests/browser/objects.scn.js` | RENDER §9 |
| B4 | effects | `js/render/fx.js`, `tests/browser/fx.scn.js` | RENDER §10 |
| B5 | HUD | `js/ui/hud.js`, `css/hud.css`, `tests/browser/hud.scn.js` | UI §4 |
| B6 | panels | `js/ui/panels.js`, `css/panels.css`, `tests/browser/panels.scn.js` | UI §4b |
| B7 | screens | `js/ui/screens.js`, `css/screens.css`, `tests/browser/screens.scn.js` | UI §5 |

### Acceptance checks, Stage B

Common to B1–B4: use `dev/view.html?fixture=1&t=5` (frozen time) with `only=<your module>` (plus
`planet` once it exists) while developing, then with everything loaded. Screenshots at: orbit
(zoom 0), continent (0.4), close (0.85), the night side, the terminator, the `molten` and
`water` phases, and at quality 0 and 2. Look at every screenshot critically as an art director
and iterate until it would not look out of place on a game's store page. Zero `page.errors`
(WebGL errors included); canonical GL state on exit; report `Renderer.stats` (draws, triangles,
instances) and frame cost. Verify that changing the world and setting the dirty flag updates the
picture (mutate, settle, re-screenshot). Your scenario file must be re-runnable by the lead.

**B1 planet.** Biomes read at a glance from orbit; shorelines have foam and a shallow-to-deep
gradient; the sea rises smoothly when `uPlanet.y` changes (no rebuild); mountains show facets
and snow; rivers and lakes visible at mid zoom, no z-fighting at any zoom; ice caps and sea ice;
farmland patchwork and roads at close zoom; the close-zoom ground has the required fragment
detail (no flat 300 px gradients); city glitter at night; lava and molten crust glow; dryness
tint (set a drought, colours yellow); overlays 1–5 with distinct legible ramps; brush ring on
land and on water, dashed when `uBrushCol.a < 0.5`; `groundAt(geo.dir(c))` = `DM.R + elev[c]`
± 1e-3 and matches the mesh between vertices; for 200 visible cells `Renderer.project` then
`Renderer.pick` returns the cell or a neighbour; `raycast` returns the first hit when grazing a
mountain; a sculpt shows up in `groundAt` and on screen on the next frame; `showSnapshot`
recolours (including molten heat) and restores. Day-side orbit shot: `decodePng().stats()` lit ≥
0.25 and colors ≥ 500. Elevation re-upload ≤ 4 ms, colour rebuild ≤ 6 ms at L = 6.

**B2 sky.** Stars and a sun with glare behind the planet; an atmosphere rim that matches the
surface haze at the limb (no dark outline, no bright step: zoom the screenshot on the limb and
check), warm at the terminator, thin on a sea-less world; clouds with believable large-scale
structure, lit and shadowing the ground consistently with `cloudCover`, fading as the camera
descends, no double-blended ring at the limb; storms visible from orbit; only `uFx.y`, `uFx.z`
and `uStorms` written.

**B3 objects.** Forests read as forests from orbit and as groves and individual trees up close,
right species per biome; each settlement tier and age is recognizably different; culture colours
visible; ports, walls, temples, wonder, launch site and rocket present when flagged; ruins;
units move smoothly (damped) along paths and become ships at sea; nothing floats or sinks (after
a +3 sculpt under a forested cell and a settle, every tree within the brush has
`|length(aI0.xyz) − Planet.groundAt(dir)| < 0.05`); windows light up at night; far-side cull
never removes visible mountain-top trees; rolling sweep: no frame spends more than 1.5 ms on
re-instancing (measure) and the whole planet refreshes in about 1.5 s; instance caps by quality;
`drawDepth` skipped at zoom < 0.3 and at quality 0; `anchors` returns positions; `showSnapshot`
markers.

**B4 effects.** Trigger each event through `DM.Bus.emit` with fixture data and screenshot
mid-effect (advance with `DV.settle` under frozen time): comet, meteor (flight, impact,
aftermath), lightning, volcano, fire, rain and storm, tsunami front, quake, witnessed motes
(gold and red), prayer column, people-phase beacon, founding, age rays, battle, plague, nuke,
Ark launch, chimney and campfire smoke. Each must be unmistakable and beautiful, with bloom
where it is hot and no glowing smoke at night. Layers correct (rain under clouds, plumes above).
Emitting every §2 event with a valid payload gives zero errors and stays under the particle cap;
≤ 1 ms CPU per frame with 3,000 particles; `attach()` twice does not double the effects.

**B5 HUD.** In `dev/play.html?bare=1` with `DM.debug.fixture()` and with `DM.debug.new()`:
screenshots at 1280×720, 1920×1080 and 390×844 (touch) in each genesis phase and mid-history,
with a brush power armed (armed chip visible), with a power locked / unaffordable, with an
overlay legend, and with the HUD hidden (eye button present). Drive real interactions with the
rig using the binding selectors: clicking `[data-power]` arms it (`Game.power`) and clicking
again disarms; `[data-speed]` changes `Game.speed` / pause; `#hud-menu` opens the pause screen
(or calls `Screens.pause`); `#hud-genesis-next` calls `Game.nextPhase`; first-steps hints appear
once and are remembered; toasts for `awe:short` are rate-limited. At all three sizes: no element
outside the viewport, touch targets ≥ 40 px, text readable over a bright planet, pointer events
pass through to the canvas outside panels. `HUD.update` ≤ 0.4 ms average.

**B6 panels.** Same pages and sizes: inspector for settlements of several tiers and states
(plague, famine, prayer, war, prophet with edict buttons that call `Game.setEdict`), using
`Society.peek` only (assert `vars.rel` is unchanged by opening it); chronicle feed and full
chronicle with filters; clicking an entry or a now-strip item calls `Game.flyToCell`; labels
follow `Renderer.project`, respect zoom thresholds and the 40 cap, show status glyphs, never
capture pointer events; narrow layout uses bottom sheets. `Panels.update` ≤ 0.6 ms average with
100 settlements.

**B7 screens.** Screenshots of every screen at the three sizes. Drive the full flow with the
rig: title → New World → setup → Begin starts a world (and asks before replacing a save);
pause → settings → change quality and volumes → values persist in `DM.Save.settings`; help; an
ending screen for each of the six ending kinds (`DM.Fixtures.ending`) with the tale and stats;
time-lapse plays and scrubs the fixture snapshots through `Renderer.showSnapshot` and closes by
button; the planet card produces a PNG data URL of the right size. `Screens.current` is a
property with the documented values. Every screen has a visible Back / Close button; Escape
goes back one step.
