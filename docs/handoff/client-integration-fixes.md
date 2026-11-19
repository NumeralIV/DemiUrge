# C10 integration: cross-module fixes (2026-11-19)

Every change C10 made to a file owned by another client module (C0-C9), with the reason and the evidence.
All were made in lane `integration` and synced to main. The module owner's next lane should `lane.ps1 refresh`.

| Module | File | Change | Why / evidence |
|---|---|---|---|
| C0 bridge | `Game/Core/DmGameMode.*`, `DmScenario.*` | `HarnessShotPath(path, bWithUI)` / `Ctx.ShotPath`, `ShotsDir()`, `GetLastUnitMs()`; `FDmShotArgs::bFixtureArg` | the playthrough shoots with the HUD and screens in the picture; perf-play reads per-frame unit timers |
| C0 bridge | `Game/Core/DmGameMode.cpp` | "play" (and `playthrough*`, `flow*`) no longer load the fixture unless `-fixture=` is given | the game itself runs the real simulation (no fixture in normal play) |
| C0 bridge | `Game/Core/DmCoreScenarios.cpp` | "play" with no world: `ADmFlow::PrepareTitleWorld` + all parts + `ADmFlow::StartTitle` | the default (packaged) start is the title over a real sim world |
| C0 bridge | `Game/Core/DmSimSubsystem.*` | `BeforeTick` hook called before every sim tick (RunFrame and StepTicks) | scripted players (the playthrough's god) act before each tick, as simrun's policies do |
| C1 planet | `Scripts/planet/land.hlsl` | molten crust: plates barely glow (0.004-0.02), seams 0.5; gain 9 -> 3.2, deeper red | the molten world rendered as a pale peach disc (shots/ue-int-gen-1.png before / after); matches docs/concept/genesis 1-2 |
| C1 planet | `Scripts/planet/water.hlsl` | deep-water scattering a little bluer (0.00002, 0.00009, 0.00019) | with the thinner orbit aerial perspective the open sea read grey-navy |
| C1 planet | `Game/Planet/DmPlanetMaps.*` | `FDmMapInputs::Barren` (1 molten, 0.85 water, 0.55 life, 0 history): bare land is dark basalt before life | a lifeless world drew desert sand; the concept genesis frames show dark rock |
| C1 planet | `Game/Planet/DmPlanetMaps.cpp` | every `ParallelFor` in the map jobs at `EParallelForFlags::BackgroundPriority` | worker jobs starved the frame's own task-graph work: 100-140 ms `TG_EndPhysics` stalls in `stat dumphitches` |
| C1 planet | `Game/Planet/DmPlanet.*`, `DmPlanetMaps.*` | terrain re-capture at most every 0.4 s; river map at most every 3 s and updated in place (`UpdateRiverTexture`, buffer hand-over) instead of a new 4096^2 texture | hitch dumps at 8x: DmPlanet tick 41-54 ms (30 ms building the texture, 5-17 ms destroying the old one) |
| C2 sky | `Game/Sky/DmSkyActor.*` | sun `bPerPixelAtmosphereTransmittance = true`, `ForwardShadingPriority = 1` | the whole planet was tinted pink / tan from orbit (C4's report); on-screen "multiple directional lights" warning |
| C2 sky | `Game/Sky/DmSkyActor.*` | aerial perspective by altitude: 0.6 near the ground, `ApScale` 0.38 from orbit (`-apscale=`) | forests read teal-grey from orbit and towns sat in a blue-grey wash (target: docs/concept/nature/descent-1-400km.png) |
| C2 sky | `Game/Sky/DmSkyActor.cpp` | molten haze 1 + 5 x heat -> 1 + 1.2 x heat; the sun dimmed by up to 80 % on a molten world | the molten world's glowing seams were drowned in a pale veil |
| C2 sky | `Game/Sky/DmSkyActor.cpp` | cloud cover thins with altitude (x0.08 at 3 km .. x1 from 28 km); clouds hidden below 2.5 km, cloud shadows off below 4 km; cloud shadow map 1.5x / 1x rays below 80 km (was 4x / 2x) (`-keepclouds` keeps them) | a village at 2.6 km was all cloud; `ProfileGPU` at a 15 km view: CloudShadow 9.4 ms of 26.7 ms |
| C2 sky | `Game/Sky/DmSkyWeather.cpp` | `ParallelFor` at background priority | as for the planet maps |
| C4 nature | `Game/Nature/DmNatureActor.cpp`, `DmNatureFields.cpp` | `UE::Tasks::Launch` / `ParallelFor` at background priority | as above |
| C5 towns | `Game/Towns/DmTownsActor.cpp` | ground changes: a town without a plan is not re-planned; a sea-level change re-plans only towns within 30 m of the new sea; plan tasks at background priority | under a rising sea at 8x every terrain re-capture re-planned all ~100 towns (log: "ground moved under 99 of 99"), 30-230 ms game-thread frames |
| C7 hud / C8 screens | `Game/UI/Hud/DmHudStyle.*`, `Game/UI/Screens/DmScreenStyle.*` | the shipped OFL fonts (new `Game/UI/DmFonts.*`) replace Palatino / Segoe UI: EB Garamond, Cormorant Garamond (serif 20 px and up), Inter, Cinzel (logo) | T-009 / docs/concept/ui |
| C8 screens | `Game/UI/Screens/DmScreenPagesWorld.cpp` | people counts of a million and more read "10.5M" | "10,547,547" overran the next stat on the ending page |
| C8 screens | `Game/UI/Screens/DmScreenSubsystem.cpp` | the card clears the last message line | "Saved to ..." from the save page showed under the planet card |
