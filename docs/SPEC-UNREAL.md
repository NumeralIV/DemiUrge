# DEMIURGE — Unreal client spec (v1, orchestrator)

Read `UNREAL.md` and `FOUNDATION-CPP.md` first. The simulation (`Source/Demiurge/Sim`, plain
C++20) is the game's brain; this document specifies the Unreal client around it: how the world is
shown, how the player acts on it, and how engineers build and test that in parallel.

**What vs how.** `SPEC-RENDER.md` and `SPEC-UI.md` were written for the old WebGL client. They
remain the contract for **what the player sees, hears and does**: every view, overlay, effect,
HUD element, panel, screen, input binding, tooltip, toast, sound cue and the camera's behaviour.
Ignore their technology (WebGL, shaders, DOM, CSS, `DM.*` names, file:// rules). This file says
**how** in Unreal. Where the two conflict, this file wins; where this file is silent, follow the
old spec's intent and say what you chose.

**Look.** The owner rejected low-poly and procedural-primitive looks twice. The target is
cinematic and near-photoreal: a real-time planet that reads like a satellite photograph from
orbit and like a living miniature world up close. Physically based lighting (sun, sky atmosphere,
Lumen), volumetric clouds, soft shadows, restrained bloom, no flat colours, no visible polygon
facets, no tiling patterns. Quality bar: `shots/ue-lab-orbit4.png` is the floor, not the goal.

**Graphics research.** Three lab engineers prototype the hardest visuals in private copies of the
lab and write references: `docs/lab/clouds.md` (weather-driven volumetric clouds), `docs/lab/surface.md`
(descent LOD, ground material, ocean, rivers), `docs/lab/vegetation-and-towns.md` (generated trees and
buildings). Read the one for your module if it exists and build on what it proved.

**Approved concept targets (merged 2026-11-19).** `docs/concept/settlements/` (each age from Stone to
Space, three culture styles, a night city) and `docs/concept/landmarks/` (buildings, wonders,
god-power effects), each with an `index.md` of artist notes. They define the close-up look that the
later real-asset pass aims at; towns, nature and fx engineers should match their layouts,
silhouettes, palettes and effect readability now (with simple stand-in meshes, per 0.4).
Genesis target: `docs/concept/genesis/` (molten, cooling, comets, seas, life wave, awakening). Descent and biome target: `docs/concept/nature/` (one place at 400 km, 50 km, 10 km, 2 km and 300 m, plus nine biomes). UI target: `docs/concept/ui/` (HUD, panels, chronicle book, screens, icons, with px, hex and font notes); fonts Cinzel, Cormorant Garamond, EB Garamond and Inter (SIL OFL) are in `assets/fonts/` (T-009, with licences) and replace the Windows stand-in fonts. Chronicle prose and god epithets: `docs/content/chronicle-voice.md`. Asset sourcing (owner's $0
additional-spend rule): `docs/research/assets.md`.

## 0. Working rules for client engineers

0.1 **Lanes.** Unreal builds one module; a half-written file from one engineer breaks everyone's
build. So each client engineer works in a private **lane**: a copy of the project made by
`Unreal/lane.ps1 new <key>` (copies `Unreal/` without `Intermediate`, `Binaries`, `Saved`,
`DerivedDataCache`, `Lanes`, `SimTests/build`) into `Unreal/Lanes/<key>/`. Build and test only in
your lane (`Unreal/Lanes/<key>/build.ps1`, `shot.ps1`). When you finish, `lane.ps1 sync <key>`
copies **only your owned files** (listed in `Unreal/lanes/<key>.owned`, one path or glob per
line, written by you first) back to the main project. A verifier makes a fresh lane from main.
Never edit another engineer's files, even in your lane; work around a gap and report it.
The bridge engineer (C0) writes `lane.ps1`; until it exists, nobody else starts.

0.2 **Code layout.** Unreal-side code lives in `Source/Demiurge/Game/<Area>/` (one folder per
client module, files prefixed by area: `Planet/DmPlanetActor.h`). Class prefix `Dm`
(`ADmPlanet`, `UDmSimSubsystem`). Unreal coding standard, `bUseUnity = false`, no Blueprints,
no hand-edited `.uasset` files.

0.3 **Content from scripts.** Every asset (material, material instance, texture, mesh, input
action, sound, widget style) is created by an editor Python script you own,
`Unreal/Scripts/<key>.py`, run headless with
`UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<file> -unattended -nosplash`, writing
under `/Game/<Key>/`. Scripts must be re-runnable (delete and recreate). Prefer creating
lightweight objects in C++ at runtime (dynamic material instances, procedural meshes, input
actions via `NewObject`) when that avoids an asset. Reference implementation:
`UnrealLab/PlanetLab/Scripts/make_materials.py` (material graphs via `MaterialEditingLibrary`).

0.4 **No downloaded assets yet.** No network. Geometry is generated in code (procedural or
dynamic meshes, generated once and instanced) and must look crafted: smooth shading, bevels,
silhouette variety, per-instance colour and scale jitter, wind sway. A later asset pass may swap
in Fab or Blender meshes: keep every visual behind a small factory function so that is a
one-place change.

**Owner direction (2026-11-19): no double handling.** The owner loves the orbit look and finds
close-up houses and props "not mature yet". Close-up meshes (buildings, trees, units, props) will
probably be replaced by real assets later, so do not spend effort polishing generated geometry
for them. Put the effort where it survives an asset swap: placement and layout rules, culture /
age / tier variation tables, instancing, LOD and culling, lighting, materials and shading
parameters that real meshes will also use, the mesh factory interface (mesh id + variant +
scale, so an imported asset drops in), and scenario coverage. Generated close-up meshes should be
clean, simple stand-ins. Orbit-scale and planet-surface work (terrain, ocean, clouds, sky,
canopy-from-orbit, city lights) is lasting and deserves full polish.

0.5 **Seeing your work.** Run the game offscreen and capture: `shot.ps1 <name> [args]` (written
by C0, modelled on `UnrealLab/PlanetLab/shot.ps1`) launches
`UnrealEditor.exe <uproject> -game -RenderOffscreen -windowed -ResX=1920 -ResY=1080 -nosound
-shot=<png> -warm=<frames> -scenario=<name> ...`, waits for the screenshot and exits. Look at
every image with the Read tool and judge it like an art director. Clouds, Lumen and TSR need
about 240 frames to settle. Scenarios (C0 provides the mechanism; each engineer registers their
own under their area) set up a world state, camera and time, then shoot. "It compiles" is not
evidence; numbers and pictures are.

0.6 **Performance.** 60 fps target at 1080p on an RTX 3060 (12 GB) with the whole planet in
view; check `stat unit` / `stat gpu` output in scenario logs. No per-frame allocation in hot
paths; rebuild meshes only on the sim's dirty flags.

0.7 **PowerShell**: invoke scripts as `powershell -NoProfile -ExecutionPolicy Bypass -File ...`.

## 1. World scale and coordinates (binding)

- Planet datum radius `R_uu = 100 km = 1.0e7 uu` (sim `DM.R = 100` units maps to it).
- Elevation: `0.2 km per sim elevation unit` (relief exaggerated about 10x for readability).
- Sim +Y (north pole) → Unreal +Z. A sim direction `(x, y, z)` maps to Unreal `(x, z, y)`.
- Sky atmosphere and clouds are Earth's scaled by `S = 1/6.15` in height (coefficients by
  `1/S`): see `UnrealLab/PlanetLab/Source/PlanetLab/LabGameMode.cpp`. Bottom radius = sea
  radius in km. Cloud tracing start/max distance must exceed the orbit camera distance.
- Required renderer settings (in `Config/DefaultEngine.ini`): Lumen GI and reflections, virtual
  shadow maps with `r.Shadow.Virtual.Clipmap.LastLevel=26`, TSR, manual exposure (no eye
  adaptation from orbit), `SetbUsePerSampleAtmosphericLightTransmittance(true)` on clouds.
- Sim time drives the sun: the planet turns (day length is a tuning value, default 40 real
  seconds at 1x) and the sun is fixed; seasons tilt the sun ±23°.

## 2. Modules

Each module: key, owner, folder, what it does. "Old spec" = the section of SPEC-RENDER / SPEC-UI
that defines its behaviour.

**C0 `bridge`** — `Game/Core/`, `Unreal/lane.ps1`, `Unreal/shot.ps1`, `Unreal/lanes/`,
`Config/*` (shared settings; others request changes in their report).
- `UDmSimSubsystem` (`UGameInstanceSubsystem`): owns the `dm::World`, creates worlds (seed, name),
  runs the simulation in real time (speeds 0, 0.25, 1, 2, 4, 8; pause), fixed-step ticking with
  catch-up limits, routes every sim event to a native multicast delegate (`OnSimEvent`) with the
  typed payload, exposes read-only accessors, `UsePower(id, cell, dt)`, save / load to slots
  under `Saved/SaveGames/` through `dm::save`, and the selected settlement / culture.
- A **fixture loader**: builds a believable populated world without the full sim, from
  `UnrealLab/data/planet_*.lab` (terrain, climate, life fields exported from the finished JS
  modules) plus generated settlements, cultures, roads and units, so every client engineer can
  render a real-looking world before the sim is integrated.
- `ADmGameMode` (replaces the skeleton's), `ADmPlayerController`, the scenario / shot harness
  (`-scenario=`, `-shot=`, `-warm=`, `-cam=`, `-sun=`, `-year=`, `-fixture=`, registration of
  scenarios by name from any area), `Game/Core/DmDirtyRouter` that fans the sim's dirty flags out
  to listeners once per frame.
- The cell picking service: screen point → cell id / direction, used by input and HUD.

**C1 `planet`** — `Game/Planet/`. Old spec: RENDER §6 (planet), and every per-cell visual in
§0. The land and ocean surfaces built from the world: start from the lab
(`UnrealLab/PlanetLab/Source/PlanetLab/LabPlanet.cpp`: subdivided icosphere, shared edges,
smoothing, fractal relief, albedo from continuous climate fields, depth-tinted ocean, city
lights) and take it much further: chunked LOD so the camera can descend to a single valley
(distance-based subdivision of patches near the camera, crack-free), real normal detail,
texture-like ground variation from noise in the material (rock strata, dunes, field patterns on
farms), shorelines with foam and shallows, ocean waves and sun glint, sea ice, lakes, rivers
(width from flow, meandering, visible from orbit as thin lines), roads, farmland, urban grey,
scorch, lava glow, snow, owner borders as an overlay, and the overlays the old spec lists.
Incremental updates from dirty flags without hitches.

**C2 `sky`** — `Game/Sky/`. Old spec: RENDER §7. Sun and planet rotation, sky atmosphere,
weather-driven volumetric clouds (a cloud-cover texture generated from the sim's moisture, wind
belts and storms, updated over time, sampled by a custom volumetric cloud material, with
cyclones visible from orbit), cloud shadows, stars and a milky way, aurora at the poles, a moon,
lightning flashes in storm cells, colour grading and exposure per time of day and mood, the
genesis looks (molten world glowing, comet rain).

**C3 `camera-input`** — `Game/Camera/`, `Game/Input/`. Old spec: UI §1 and §2 (all of it,
including touch rules where Unreal supports them). `ADmPawn` with the orbit → surface descent
(whole planet and atmosphere in frame at zoom 0; tilting toward the horizon near the ground; never
below the terrain; parallel-transported heading; inertia; fly-to; shake), Enhanced Input actions
and mapping contexts created in C++, every keyboard binding of the old spec, hover picking,
power arming / brush / click semantics, the brush ring (a decal or projected material on the
planet), selection.

**C4 `nature`** — `Game/Nature/`. Old spec: RENDER §9 (trees and wildlife parts). Forests that
read as forests from orbit (canopy texture) and as individual trees close up: several tree types
by biome (conifer, broadleaf, palm, savanna acacia, dead trees), grass and shrubs near the
camera, wildlife herds, birds, fish shoals as tiny instanced meshes, all instanced (HISM), with
wind. Generated meshes per 0.4.

**C5 `towns`** — `Game/Towns/`. Old spec: RENDER §9 (towns and objects) and §8 (models).
Settlements by tier and age (camp, village, town, city, metropolis; stone to space age), per
culture style and colour, buildings from `DM.BUILDINGS` (shrine, temple, granary, walls, port,
market, mine, factory, academy, plant, launch site, wonders), lit windows at night, smoke,
construction and destruction states, units (ships, caravans, armies, missionaries, rockets) moving
along paths, roads. Generated meshes per 0.4, instanced.

**C6 `fx`** — `Game/FX/`. Old spec: RENDER §10 and the powers / disasters sections of SIM §3.10.
Every power and disaster has a spectacular, readable effect: comet and meteor entry with
atmospheric glow and impact flash and shockwave, crater, tsunami ring, volcano eruption with ash
plume and lava, lightning, forest fire and smoke, rain and storms, bloom (vegetation wave),
earthquake (camera shake plus ground ripple), plague miasma, blessing light, prophet glow, peace
omen, aurora, nuclear flash. No Niagara assets unless created by your script; procedural meshes,
dynamic materials and light pulses are fine.

**C7 `hud`** — `Game/UI/Hud/`. Old spec: UI §3 (icons), §4 (HUD) and §4b (panels). Slate in C++
(no UMG assets): power palette, Awe, time controls, age and year, chronicle ticker and book,
settlement / culture panels, overlays switch, tooltips, toasts. Gold on ink, serif type (load a
font face from `C:/Windows/Fonts` at runtime for now; note it in the report: shipping needs an
open font), icons drawn as vector paths in Slate.

**C8 `screens`** — `Game/UI/Screens/`. Old spec: UI §5 and §6. Title, new world (name, seed),
genesis step prompts, pause, settings (graphics quality tiers, audio volumes, controls), save /
load slots, endings, the time-lapse replay and the planet card (a captured image plus text).

**C9 `audio`** — `Game/Audio/`. Old spec: UI §7. Synthesised in C++ (`USynthComponent`
subclasses, procedural): ambience by zoom (space hum, wind, sea, birds, towns), every power and
disaster cue, UI clicks, and generative music that follows the age and the mood of the world.
Nobody can listen: verify numerically (RMS, peak, spectrum, no clipping, no DC, no silence where
sound is expected) and keep gains conservative with a limiter.

**C10 `integration`** — owns everything under `Game/` after C0–C9 are verified, plus the full
game flow: title → genesis (molten, water, life, people) → history → ending, wiring powers to the
sim and FX, the real sim replacing the fixture, saving and loading, performance, and a complete
scripted playthrough captured as a screenshot series.

## 3. Order

C0 first (built, then verified). Then C1–C9 in parallel lanes, each built then independently
verified. Then C10 (built, verified). Markers in `docs/handoff/done-ue/`: `client-<key>.built.txt`
and `client-<key>.txt`.
