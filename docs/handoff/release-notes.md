# DEMIURGE: release notes (wave 3 final check, 2026-11-20)

Build: `main` after the wave-3 merges (gameplay rules v3, chronicle budget, towns performance, planet fixes,
performance tiers, flow / HUD clarity, aurora) and the final check's fixes. The packaged Windows build is
`dist/DEMIURGE-Win64-Development/Windows/Demiurge.exe` (Development, about 1 GB with symbols). Evidence for every
number below is in `docs/handoff/wave3-final/` and `docs/handoff/done-ue/wave3-final.txt`.

**Not in this build:** the wave-3 sky track (`Unreal/Lanes/sky3-verify`: daytime sky fill, lower haze near the
ground, golden-hour and moonlit-night lifts, stars hidden by day). It was verified (7/10) but `lane.ps1 sync
sky3-verify -Force` is refused for build agents by the permission system, so the owner has to run it. One merge is
needed when it is synced: the final check changed `ADmSky::EndPlay` in `Game/Sky/DmSkyActor.cpp` (do not wait for a
weather build at exit, see fix 6 below); `lane.ps1 sync` will report that file as a CONFLICT. Keep both changes.

## What a player can do

- **Start a world.** Title (over a turning planet with its own history) -> New World (name, seed, Calm / Normal /
  Wild calamities, now really applied) -> genesis. Quick start skips genesis; it is a separate button.
- **Genesis, the tutorial** (player-paced): a molten world cools; comets (armed for you) fill the seas, Raise /
  Lower Land shape the soft clay; Seed Life starts a green wave; Awaken wakes the first people where you click. A
  banner says what to do at each step and a Continue button moves on.
- **History** (Stone -> Bronze -> Iron -> Sail -> Industry -> Atomic -> Space, about 30 real minutes at 1x with the
  auto-pace; pause / 1x / 2x / 4x / 8x): settlements grow from camps to metropolises with fields, roads, walls,
  harbours and ships; peoples split into cultures with their own languages and names for you; they trade, pray,
  ally, go to war, get sick, starve, build wonders and finally an Ark.
- **14 powers** in four groups, paid in Awe, unlocked by age: Raise / Lower Land, Rain, Bloom, Lightning,
  Earthquake, Plague, Meteor, Volcano, Heal, Inspire, Prophet, Omen of Peace, Consecrate (shrine -> temple, or a
  Wonder in a capital once per age), plus the genesis powers. Rain becomes Monsoon in the Atomic age and Meteor
  becomes Star-fall in the Space age. Every one is checked end to end (input -> sim -> effect -> sound -> HUD).
- **Be judged.** Each people remembers your deeds for about a thousand years (grace, wrath, neglect, answered
  prayers). The HUD's regard word and the small "how they will remember you" map show where the ending stands;
  four Ark endings (Beloved, Feared, Forgotten, In parting) plus silence, each reachable by a different way of
  playing (8 seeds: kind 8/8 beloved, cruel 8/8 feared, passive 8/8 forgotten, chaotic / curious 8/8 parting).
- **Read the story.** A chronicle in a mythic voice names the god by its deeds ("the Breaker of Spears", "the
  Gentle Hand"), rate-limited so the late game is readable; the ending screen tells the long story of the world,
  then the ages time-lapse and a shareable planet card (Save as PNG).
- **Learn as you go.** First-steps thread (find your people, make it rain, answer a prayer ...), hints, toasts that
  merge ("8 prayers answered"), an inspector that explains famine, prayers, plague and the rising sea.
- **Save and load anywhere.** Pause menu slots, autosave, Continue on the title. Loads are exact at every moment the
  final check could reach (14 awkward save/load moments, 33 checks, below).
- **Graphics tiers.** Settings -> Low / Medium / High / Epic / Auto; `-tier=` on the command line.

## Known issues, most important first

1. **Night, sea and clouds still break the "cinematic" promise in common views.** (a) The night side near the
   ground renders as bright cream daylight with dark cloud-shadow blotches (`shots/ue-ab-night-base.png`): the
   moonlight / night-exposure lift is far too strong (sky; the sky3-verify lane reworks the night lift but does
   not claim this view). (b) The ocean is the atmosphere's flat navy ground, not water: Single Layer Water gets no
   sun and no depth extinction, shelves and drowned land cannot show, and large pale straight-edged polygons appear
   on the sea from orbit, most visibly on the shareable planet card (`shots/ue-play-19-planet-card.png`)
   (planet water + sky, needs one owner for both; see wave3-planet-fixes). (c) Volumetric clouds are blocky and
   dithered at their edges from orbit and cast hard square shadows close up (`ue-power-volcano.png`).
   (d) A hard circular seam with a cyan rim cuts across the globe near the night limb in some orbit views
   (`shots/ue-saveload-awkward-ended-world-loaded.png`).
2. **Live play near the ground is still under 60 fps, even on an idle machine** (playtest 1's issue 3 is only half
   fixed). The GPU fits 60 fps at High in every view (perf-ab medians 10.5 orbit / 10.9 terminator / 11.7 mid /
   14.4 low / 13.1 close / 13.8 night ms; Medium 8.8-11.8 ms). But the packaged game in live play at 8x with the
   scripted god, measured with the machine idle (perf-play): orbit 81 fps, mid 53, low (2-5 km) 40, close 71 at
   High; at Medium mid 60, low 43, close 64. The low view is CPU-bound: game thread 15 ms average (p95 33-42),
   render thread 18 ms (p95 25-33); and every view has a 75-650 ms hitch every few seconds that shows in neither
   thread's time (13-53 per 30-60 s stop; likely garbage collection or synchronous work between frames). Next: an
   Unreal Insights capture of the low view at 8x; suspects are nature's "fields" ticks (28-70 ms in the towns logs),
   the yearly sim tick at 8x and the per-frame instance updates of towns and nature on the render thread.
3. **Close-up ground is a patchwork of flat coloured slabs.** Fields are regular brick rectangles at 10 km and
   radial floating slabs round towns at 2-5 km (`ue-play-11-war.png`, `ue-play-13-golden-age-city.png`); the
   concept is wooded country with few, organic fields. The war frame still shows no readable army.
4. **Genesis looks are below the concept board.** The molten world is a uniform Voronoi crack pattern
   (`ue-play-03`), and the life wave barely colours the land (`ue-play-07`, grey land after seeding).
5. **Effects read as sprites.** The volcano is a flat orange starburst, Bloom fills the screen with large
   four-point sparkles (`ue-play-09-early-village.png`), city lights at night are white blobs with glowing roads.
6. **The chronicle's early quiet stretch** (no notable entry for 224-330 years before year 1,000; target 150) and
   the Awe economy (passive sits at the Awe cap 88 % of years, prayers 22-62 %; targets 50 % / 15 %) still miss
   the design review; kind has more famine entries than passive on 4/8 seeds (target 5/6).
7. **Minor:** "Preparing Shaders (n)" can show over the town panel during the first minutes of a fresh install
   (`ue-play-08`); the narrow (phone) layout has no remember card or guidance thread; the guidance counter shows the
   first undone step; the score on the ending screen and the planet card can differ by a few points (the world
   keeps ticking after the ending); a load in history drops the armed power (by design, the player re-picks it). The title's footer says "Every image and every
   sound in DEMIURGE is made in code": change it when the real-asset pass lands.
8. **Tooling:** `lane.ps1 sync` keeps the lane file's old timestamps, so main can link stale objects: touch synced
   files before `build.ps1` (every wave-3 agent hit this). Several lanes' `.owned` globs overlap (`integration`
   claims `Game/**`), so every client sync needs `-Force`.

## What the real-asset pass should replace first

1. **Ocean and coast** (water material and shoreline: depth colour, shelves, foam, glint, a real sea surface the
   sun lights). It covers most of every orbital frame and the planet card.
2. **Ground textures at 300 m - 10 km** (grass, earth, field, rock, sand and the field / hedgerow layout): the
   brick patchwork and the grey-olive land are what the player looks at most of the game.
3. **Clouds and night:** cloud edge / shadow quality and the night-side grade with warm, structured city lights
   (`docs/concept/night`).
4. **Town kits** (roofs and walls per age and culture) and the field slabs round towns.
5. **Units and ships at the 2-5 km zoom** (with the culture ground ring), so wars and trade read on screen.
6. **Power effects** (volcano, meteor crater, bloom, rain) against `docs/concept/landmarks/power-*`.
7. **Genesis** (molten crust with lava rivers, a visible green life wave).
`docs/flow-queue.json` already ranks 196 generation jobs in this order (look anchors, then the most-seen
materials).

## What the final check fixed (all on main, listed in `Unreal/lanes/wave3-final.owned`)

1. The setup screen's calamity choice (Calm / Normal / Wild) was stored and never applied to the world
   (`UI/Screens/DmScreenSubsystem.cpp`, code-health U-1).
2. The HUD's start-up log always said "Palatino Linotype / Segoe UI" (the line that made playtest 1 report that the
   shipped fonts were ignored); it now names the shipped families (`UI/Hud/DmHudActor.cpp`, `DmHudStyle.h`, U-3).
3. Consecrate, the new v3 power, had no icon (a bare dot in the power bar), no effect and no sound; it now has a
   temple icon (`UI/Hud/DmHudIcons.cpp`), the blessing effect (`FX/DmFx.cpp`) and a choir cue
   (`Audio/DmAudioDirector.cpp`). powers-check now gives it a town that can take it (`Flow/DmFlowScenarios.cpp`).
4. Loading a genesis save in the same phase disarmed its power (ArmPower toggles), so the player's comet / seed /
   awaken clicks did nothing after the load (`Flow/DmFlow.cpp`).
5. Loading a save made in the ending's tick or its 7-second camera hold left an ended world in play with no ending
   screen, ever; an ended world now opens on its ending screen (`Flow/DmFlow.cpp`).
6. The game took over 60 s to quit (every scenario was killed at exit): the sky's EndPlay waited for a weather build
   queued behind the planet's patch jobs; it no longer waits (`Sky/DmSkyActor.cpp`). Exit now takes about 5 s.
7. New scenario `saveload-awkward` (`Flow/DmSaveLoadAwkward.cpp`): save + load through the screens at 14 awkward
   moments (molten, comets in flight, life wave, before / right after the awakening, mid Rain press, pause menu
   open, a 16x frame, an army on the march, after a battle, after a world age, the Ark being built, the launch tick,
   the ending screen) plus genesis-in-history and back, two loads in a frame, a missing slot, the ending replayed
   and load from the title: 33 checks, `DmAwk: RESULT PASS`.

## Owner playtest 1 and the determinism alarm: status

- *New world skipped genesis:* fixed (flow-hud). New World runs the whole genesis; Quick start is its own button.
  The playthrough replays the path title -> setup -> genesis -> history -> Ark -> ending -> timelapse -> card.
- *Nothing told the player what to do:* fixed (flow-hud): genesis banners with the power armed for you, the
  first-steps thread and hints (first-minutes PASS in the flow-hud verify). Not re-checked here: what a click on
  empty land with nothing armed says.
- *Frame rate near the ground:* GPU fixed (perf tiers, towns, planet); live play near the ground is still 40-53 fps
  on an idle machine (CPU-bound, plus periodic hitches), see known issue 2.
- *HUD ignored the shipped fonts:* the fonts were in use; the log line was wrong and now names them
  (`DmHud: up (fonts: serif EB Garamond / Cormorant Garamond (shipped), sans Inter (shipped))`).
- *Transient-texture hitches on title -> play:* not seen in the final logs (towns and planet moved work off the
  game thread); not separately re-measured.
- *Determinism alarm (chronicle length varying run to run):* cleared. simrun `--hash` 3 times x 8 seeds x 4
  policies (passive, kind, cruel, chaotic) at 4,500 years: 32 of 32 worlds give the same hash and the same chronicle
  entry count on every run (`docs/handoff/wave3-final/determinism-rep1..3.txt`, `det_compare.py`).
