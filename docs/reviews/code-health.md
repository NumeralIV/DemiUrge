# DEMIURGE: code-health audit and cleanup plan (night, 2026-11-20)

Author: the lead's code-health agent. This is a read-only audit. No file under `Unreal/Source`, `Unreal/Config`
or `Sim/` was changed: wave 3 (towns and planet reviews, then the whole-game final check) owns that code tonight,
and the simulation track owns `Sim/`. Every item below is meant to be picked up by whoever owns the module
**after** wave 3 hands its files back. Line numbers are from the tree as it stood at about 03:20 on 2026-11-20.

Companion reviews: `docs/reviews/security-review.md` (covers M2 "scenario harness in every build", the dev server,
the template token and save-file hardening in depth; this document only links to it where code health and
security overlap) and `docs/reviews/claude-design-review.md`.

## 1. What was measured

| Area | Size | Notes |
|---|---|---|
| `Game/` (Unreal client) | 158 files, 53,231 lines | 12,701 lines (24 %) are scenario / verifier / perf / fixture code compiled into **every** build |
| `Sim/` (C++20 simulation) | 22,994 lines | clean: no TODO/FIXME, no debug prints, no `#if 0`, standalone build at `/W4` with shadowing as errors |
| `SimTests/` | 37,228 lines | 52 test files plus 17 module stubs |
| `art/` Blender scripts | 57,914 lines of Python | one shared kit (`art/tools/dmkit.py`) that only `art/tools/` imports |
| Disk (not all versioned) | `Unreal/Lanes` 31 GB, `UnrealLab` 17 GB, `shots` 6.7 GB, `Unreal/Intermediate` 6 GB, `art/style-pilot` 1.3 GB, `SimTests/build` 1.2 GB | see section 4.13 |

How. I used a comment- and string-stripped identifier census over `Source/`, `SimTests/` and `Tools/`: a public
function with no production caller outside its own `.h`/`.cpp` pair is reported as dead or test-only. Then I
grepped for duplicated helpers, asset paths, `/Game/` content references, command-line switches, `__has_include`
fallbacks, raw `UObject*` members, TODO markers, and the last 78 Unreal build logs. Every finding below was checked
by reading the code at that line. The census gave false positives (Slate `SLATE_ARGUMENT`s, engine overrides),
and those are left out.

Overall verdict. The code is in good shape for its age (two days). There is almost no TODO debt. Garbage
collection is handled correctly (83 `TObjectPtr`, 81 `UPROPERTY`, one raw subsystem pointer). Recent build logs
show no compiler warnings at Unreal's default level. The real debt is **structural**, and it comes from building in
parallel lanes. While a module was still missing, each lane had to stand alone, so:
- there are five ground-height implementations and three planet-terrain copies;
- two aurora renderers both run;
- there is a stand-in placeholder planet and sky, and a stand-in HUD control state;
- the HUD and the screens each have their own style system.

None of the stand-ins is needed now that every module is in the build. They cost frame time, memory, startup
time and reader time, and two of them hide real bugs (C-1, U-1).

## 2. The twelve items that matter most

| # | Item | Module | Why it matters | Risk of the fix |
|---|---|---|---|---|
| 1 | **One ground service** instead of five height functions and three terrain snapshots (X-1) | Core, Planet, Nature, Towns, Camera, FX, Input | The camera, FX impacts and the brush ring follow the coarse sim surface. Trees and towns follow the drawn terrain, so effects and the camera can float above or sink into the relief. There are 3 x ~130 ms terrain prepares and 3 copies in memory | Medium |
| 2 | **The setup screen's Calm / Normal / Wild choice does nothing** (U-1) | UI / Flow | The player is offered a real decision that is silently dropped | Low |
| 3 | **Scenario harness out of the shipping game** (C-2) | Core + every area | 24 % of client code, about 100 scenarios, and switches that write files (`-shot`, `-naturedumptex`) all ship | Medium |
| 4 | **Remove the duplicate aurora** (K-1) | Sky | The sky builds and animates a ribbon aurora that the aurora actor hides every frame. Its boost is read back from that hidden material's parameter | Medium |
| 5 | **Remove the placeholder planet and sky** (C-3) | Core, Nature, Towns, Planet | 557 lines plus two cooked materials, kept alive only by fallback branches | Low |
| 6 | **Remove the HUD's stand-in control state** (U-2) | UI | A second copy of the armed-power / brush / overlay state, chosen by `__has_include` | Low |
| 7 | **Fix the stale font log** that misled playtest 1 (U-3) | UI | The log always names "Palatino Linotype / Segoe UI", whatever font is actually loaded | Low |
| 8 | **Unused build dependencies and plugins** (C-5) | Core / build | UMG and Geometry Scripting are linked and enabled but never used | Low |
| 9 | **One UI style system** (U-4) | UI | The palette, fonts and brushes are defined twice and already differ in shape | Low-Medium |
| 10 | **JS-prototype leftovers in the Sim core** (S-1) | Sim | A WebGL-era 4x4 matrix library that the game never calls | Low |
| 11 | **Repository weight** (R-1..R-6) | tools / repo | 280 MB of WAV previews and 252 MB of prototype screenshots are versioned in a public repo; 31 GB of finished lanes sit on disk; lane ownership lists are not versioned | Low |
| 12 | **Art kit consolidation** (A-1) | art scripts | 13 `export_fbx`, 10 `make_material`, 8 `lathe`, 9 `tri_count`, 6 `srgb_to_lin` copies, and the exported meshes are not in the game yet | Medium (output must stay identical) |

## 3. Rules for doing the cleanup

1. **Not before wave 3 releases the files.** Each module owner works in their own lane (`lane.ps1 new <key>`) as
   usual. Make one pull-request-sized change per item ID, so any regression can be traced to one change.
2. **Delete, don't comment out.** The history keeps the old code.
3. **Every item is checked by a fixed regression set** (section 5): the module's own scenarios plus `play`,
   `first-minutes` and `perf-play`. In addition, `SimTests/run.ps1 all` plus `simrun --hash` (8 seeds x 4
   policies, each run 3 times) for anything that touches `Sim/`.
4. **Risk scale.**
   - **Low**: no behaviour change is possible, or the code has no production caller.
   - **Medium**: behaviour is meant to stay the same, but timing, threading or the frame budget changes.
   - **High**: visible behaviour changes. Nothing below is High once done in the stated order.

## 4. The plan, by module owner

### 4.1 Sim (simulation track)

The Sim is the cleanest part of the project. What remains is leftovers from the JS port and functions that were
specified but never wired in.

| ID | Where | Finding | Action | Risk |
|---|---|---|---|---|
| S-1 | `Sim/Core/Vec.h:63-87`, `Vec.cpp:75` onward | `Mat4` with `identity / mul / perspective / ortho / lookAt / invert / transformPoint / transformDir / translation / scaling / rotationAxis / fromBasis`: the WebGL renderer's camera maths, ported with the foundation. No production caller (the client uses `FTransform` and `FMatrix`). Only `SimTests/test_core_vec.cpp:157` onward uses it | Delete `Mat4` and its functions plus their tests. `slerp` and `addScaled` (`Vec.h:45,53`) are test-only too: keep `slerp` only if Sim will need it | Low |
| S-2 | `Sim/Core/Util.h:48-52, 79, 89` | `remap`, `approach`, `damp`, `ridged3`, `b64dec` (and `b64enc`, `Util.cpp:212`) have no production caller. `b64*` is the JS save format, which binary `Save` replaced | Delete `b64*` and `ridged3`. Keep the three easing helpers only if a module adopts them this week | Low |
| S-3 | `Sim/Powers.h:264` `drownRisk` | Specified and tested, never called. Design-review finding 10 (Raise / Lower Land drowns a people in one second) is exactly what it is for | **Wire, do not delete.** Have the HUD brush preview show it before the click. Ask the HUD owner | Low |
| S-4 | `Sim/Names.h:71` `ruler`, `:76` `ship` | Name generators no caller uses (ships have no names on screen; the culture panel has no ruler) | Wire them in with `docs/reviews/story-v4.md` (recurring figures), or delete them if story v4 is not taken | Low |
| S-5 | `Sim/Core/Events.h:116` `eventFromName` | Test-only | Keep only if the replay or time-lapse needs it, else delete | Low |
| S-6 | `Sim/Chronicle.cpp` (4,046 lines) | The largest file in the project, and the subject of the determinism alarm in `docs/UNREAL.md` | After the alarm is cleared, split it into `Chronicle.cpp` (subscription, budget, entries), `ChronicleTemplates.cpp` (prose tables) and `ChronicleVoice.cpp` (epithets, voice). Do not split before: a split hides a bisect | Medium |
| S-7 | `Sim/Names.cpp` (2,368 lines) | Mostly syllable and culture data in code | Move the tables to `Sim/NamesData.inc` (still compiled; no file I/O in Sim) so the logic reads in about 600 lines | Low |
| S-8 | `Sim/.keep` | Empty placeholder file from the skeleton; the folder has had 40 files for a day | Delete | Low |
| S-9 | `State.h:188` `clearScratch`, `:230` `groundR`, `Sim.h:178` `runTicks`, `Hydro.h:87` `isRiver` | Test and tool API (SimTests, simrun), not dead | Mark each one `// test / tool API` in its header so the next audit does not flag it | Low |

### 4.2 Core (bridge, C0)

| ID | Where | Finding | Action | Risk |
|---|---|---|---|---|
| C-1 | see X-1 | Ground service | Owned here: see section 4.12 | Medium |
| C-2 | All `*Scenarios.cpp`, `*Verify*.cpp`, `Core/DmBridgeCheck.cpp`, `Core/DmPerfScenarios.cpp`, `Core/DmFixture.*`, `Audio/DmAudioTest.*`, `Audio/DmAudioAnalysis.*` (12,701 lines in all) | About 100 scenarios are registered by static objects in every build (Shipping too). So are their command-line switches: `-scenario`, `-shot`, `-fixture=<path>`, `-naturedumptex=<dir>`, `-gpulog`, `-dmaudiomaster`. All of it is linked, run at static-initialisation time and reachable from a shortcut's command line. Security angle: `security-review.md` M2 | Wrap every test-only `.cpp` in `#if !UE_BUILD_SHIPPING` (quick). Later, move them into a second module `DemiurgeDev` (Type `DeveloperTool` / `UncookedOnly`-style, loaded only in non-Shipping targets). The registrar API (`DmScenario.h`) stays where it is. `ADmGameMode` (`DmGameMode.cpp:80`) must still default to `play` when no scenario is registered | Medium (packaging, static registration order) |
| C-3 | `Core/Placeholder/DmPlaceholderPlanet.*` (404 lines), `DmPlaceholderSky.*` (153 lines), registered at priority -100 (`DmPlaceholderPlanet.cpp:349`, `DmPlaceholderSky.cpp:111`); materials `/Game/Bridge/M_Placeholder*` made by `Scripts/bridge.py:68,93` and force-cooked by `Config/DefaultGame.ini:16` | The real planet and sky (priority 0) win in every scene, so the placeholders never spawn. They are kept alive by the fallback branches N-2, T-2 and `Planet/DmPlanetScenarios.cpp:100-110` | Delete both classes, their two materials, the `bridge.py` section and the cook line, together with N-2 and T-2. `-noparts=planet` then means "no planet", which is what it says | Low |
| C-4 | `Core/DmFixture.cpp:995`, `DmFixture.h:45`, `DmSimSubsystem.cpp:124-142` | The fixture reads `UnrealLab/data/planet_7.lab` from **outside** the Unreal project (it walks the project's ancestors). A packaged or cloned-elsewhere build cannot find it. It also makes `UnrealLab/` impossible to archive. It is test-only now: `play` uses the real sim | Move `planet_7.lab` to `Unreal/Fixtures/` (or generate it with `simrun`), resolve it under `FPaths::ProjectDir()` only, and move it with C-2 into the dev module | Low |
| C-5 | `Demiurge.Build.cs:34-36`; `Demiurge.uproject:27` | `GeometryScriptingCore`, `GeometryFramework` and `UMG` are linked, and the `GeometryScripting` plugin is enabled, but no source file uses any of them (Slate is used directly; the meshes are `ProceduralMeshComponent`) | Remove the three module names and the plugin entry. Also mark `PythonScriptPlugin` and `EditorScriptingUtilities` with `"TargetAllowList": ["Editor"]` | Low (one build proves it) |
| C-6 | `Core/DmCoords.h:54` `CellGroundPos`; `DmDirtyRouter.h:46` `Broadcasts`; `DmGameMode.h:42` `GetSceneActors`; `DmPicking.h:33,37` `Min/MaxRadiusUU`; `DmScene.h:63` `BuildPart`; `DmSimSubsystem.h:107` `GetFixtureReport`, `:143-146` `SelectCulture / GetSelectedCulture`, `:150` `SetScale3D` | No caller anywhere. `SelectCulture` is in the spec (selected culture), but no panel uses it: the culture panel follows the selected settlement | Delete, except `SelectCulture`: hand it to the HUD owner (clicking a culture's name in the chronicle could select it) or delete it | Low |
| C-7 | `Config/DefaultEngine.ini:44` `r.ShaderPipelineCache.LogPSO=1` | PSO logging stays on in the player's build | Keep it for the Development build, turn it off in a `[Shipping]`-only section or in `DmRenderTiers` | Low |
| C-8 | `Config/DefaultEngine.ini:49-61` Android File Server block, `SecurityToken` at `:52` | Template leftover for a platform the game does not target (see `security-review.md` L5) | Delete the section | Low |
| C-9 | `Config/DefaultGame.ini:25` cooks `/Engine/EngineDebugMaterials` | Needed only because four fallbacks load `VertexColorMaterial` (`DmPlanet.cpp:91`, `DmTownsKit.cpp:1238`, and two in the placeholder planet). A missing material then shows up as vertex colour without a clear error | After C-3, log an `Error` when a project material is missing, and stop force-cooking the debug folder (the engine still cooks what is referenced) | Low |
| C-10 | `Core/DmPerfScenarios.cpp`, `Towns/DmTownsPerf.*`, `DmRenderTiers.cpp:152` (`-gpulog`) | Three separate frame-time recorders | Keep `FDmTownsPerf` as the pattern, generalise it to `FDmPerf` in Core with a per-module scope (`DM_PERF_SCOPE(Towns)`), and fold the other two into it | Low |

### 4.3 Planet (C1)

| ID | Where | Finding | Action | Risk |
|---|---|---|---|---|
| P-1 | `Planet/DmPlanet.h:170-172` `GroundRadiusUU / GroundPointPlanet / GroundPointWorld` | The planet already offers exactly the query every other module needs, and nobody calls it (nature, towns and the camera each rebuilt their own) | This becomes the ground service of X-1: expose the immutable `TSharedPtr<const FDmPlanetTerrain>` plus a version counter | Medium |
| P-2 | `Planet/DmPlanetNoise.h:15,65-66,134` | `Hash`, `Sat`, `Smooth` and `Fbm`: the fourth copy of the same noise helpers (see X-2) | Becomes the shared `Game/Core/DmMath.h` (X-2) | Low |
| P-3 | `Planet/DmPlanet.cpp` (1,289 lines), `DmPlanetScenarios.cpp` (912 lines) | Large but coherent | Split only the mesh-LOD code into `DmPlanetLod.cpp` if a reviewer asks; not a priority | Low |

### 4.4 Sky (C2)

| ID | Where | Finding | Action | Risk |
|---|---|---|---|---|
| K-1 | Old ribbon aurora: `Sky/DmSkyActor.cpp:85-130` (`MakeAuroraRings`), `:212-217` (component), `:517-524` (material load), `:1012-1018` (per-frame update), material `/Game/Sky/M_SkyAurora` from `Scripts/sky/aurora.hlsl`. New aurora: `Sky/DmSkyAurora.cpp` with `/Game/Sky/Aurora/M_Aurora` from `Scripts/aurora/aurora.hlsl` | Two aurora renderers. The sky still builds the ribbon mesh and its MID and updates them every frame. `ADmAurora::HideSkyRibbons` (`DmSkyAurora.cpp:168`) hides it, and it is called **every tick** (`:197`). Worse, `ReadSkyBoost` (`:183-191`) gets the FX surge strength back by reading the `Strength` parameter of the hidden ribbon's material | Move the boost state into a plain API (`ADmAurora::Boost(Amount, Seconds)`; `DmFx.cpp:180` calls it instead of `ADmSky::BoostAurora`). Then delete the ribbon mesh, its MID, `M_SkyAurora`, `Scripts/sky/aurora.hlsl`, `HideSkyRibbons` and `ReadSkyBoost`. Update `DmSkyAuroraScenarios.cpp:12-13,155-156` | Medium (the FX surge path must be re-verified with `aurora-check`) |
| K-2 | `Sky/DmSkyActor.cpp:41`, `DmSkyStorms.cpp:11`, `DmSkyWeather.cpp:25-40` | Three private copies of `Smooth / Sat / Hash3 / HashF` in one module | Use `DmMath.h` (X-2) | Low |
| K-3 | `Sky/DmSkyWeather.h:69` `MoistAt` | No caller | Delete | Low |
| K-4 | `Sky/DmSkyActor.cpp:51` | A private comma-list parser that exists because `FParse::Value` stops at commas. `Core/DmScenario.cpp:17` has the same need | Move it into `FDmShotArgs` (dev module, C-2) | Low |

### 4.5 Nature (C4)

| ID | Where | Finding | Action | Risk |
|---|---|---|---|---|
| N-1 | `Nature/DmNatureGround.h:1-100`, `.cpp` (340 lines) | Ground source #2: provider, placeholder mesh, planet terrain and sim surface. It holds its **own** `FDmPlanetTerrain` (`DmNatureGround.cpp:168-178`: capture plus a ~0.1-0.5 s `Prepare` on a worker) | Replace it with the ground service (X-1). The cube-sphere helpers `DmCube` and `FDmCellInterp` (`DmNatureGround.h:28-48`) are used by the scatter: move them to `Nature/DmNatureCube.h` | Medium |
| N-2 | `Nature/DmNatureActor.cpp:266-300`, `DmNatureGround.cpp:211-219, 291, 333-336`, `DmNatureScenarios.cpp:455` | Placeholder-mesh ground: reads the placeholder planet's mesh back and bucket-builds it | Delete with C-3 | Low |
| N-3 | `Nature/DmNatureGround.cpp:14-15, 124-126, 152-159, 327-330` | Provider hooks `SetProvider / ClearProvider / FromProvider`: nobody registers a provider | Delete (the ground service replaces them) | Low |
| N-4 | `Nature/DmNatureTextures.cpp:649` `-naturedumptex=<dir>` | A debug switch that writes BMPs to any folder, in every build | Move it behind `!UE_BUILD_SHIPPING` (C-2) | Low |
| N-5 | `Nature/DmNatureActor.cpp:181` | Shadow casting is chosen by matching the substring "Grass" in a mesh's name (this allocates a string per component, per change) | Use the factory's mesh id, `EDmNatureMesh::Grass*` | Low |
| N-6 | `Nature/DmNatureActor.h:79,81` `SetCanopyVisible`, `RequestRebuild`; `DmNatureMesh.h:61` `SetUV1From`; `DmNatureScatter.h:31` `CellsPerTile`; `DmNatureTextures.h:24` `DmLeafTileOrigin`; `DmNatureGround.h:70,79` `FromMesh`, `MeshMisses` | No caller | Delete (`FromMesh` goes with N-2) | Low |
| N-7 | `Nature/DmNatureMesh.h:87-114` | `Hash2/3`, `Hash01`, `Smooth`, `VNoise3`, `Fbm3`: a fifth copy of the noise helpers | `DmMath.h` (X-2) | Low |

### 4.6 Towns (C5)

| ID | Where | Finding | Action | Risk |
|---|---|---|---|---|
| T-1 | `Towns/DmTownsGround.h:1-93`, `.cpp` (317 lines); `DmTownsActor.cpp:374-395` (`LaunchGroundJob`) | Ground source #3, with the same four sources as nature but a different API (`RadiusBand`, `HeightM`, river queries). It captures **another** `FDmPlanetTerrain` and runs a ~130 ms `Prepare` (`DmTownsActor.cpp:388`). The comment explains why: copying the planet's terrain took 9-34 ms on the game thread. Sharing the planet's immutable snapshot by pointer avoids both the copy and the second prepare | Replace it with the ground service (X-1). Keep `RadiusBand` and the river queries as methods of the service | Medium |
| T-2 | `Towns/DmTownsGround.cpp:23-27, 85-190, 220-250` | Placeholder-mesh source | Delete with C-3 | Low |
| T-3 | `Towns/DmTownsGround.cpp:19-67, 129-130, 245`; `DmTownsActor.cpp:380` | Provider hooks nobody registers | Delete | Low |
| T-4 | `Towns/DmTownsGround.h:40` `AdoptPlanetCopy` | The superseded synchronous copy path (towns-perf replaced it with `AdoptPlanetTerrain`) | Delete | Low |
| T-5 | `Towns/DmTownsActor.h:110,113,123` `SetAgeOverride / ClearOverrides / TotalInstances` | No caller (scenarios use other paths) | Delete, or keep them as scenario API in the dev module | Low |
| T-6 | `Towns/DmTownsActor.h:136` `SmokeAnchors` (defined at `DmTownsActor.cpp:1395`) | Documented as the FX hook for chimney and stack smoke (`DmTownsActor.h:27`), but **FX never calls it**: town smoke is not drawn from the towns' own anchors | **Wire, do not delete**: hand it to the FX owner (cheap, visible life at medium zoom) | Low |
| T-7 | `Towns/DmTownsKit.h:76-78` `BodyMat / GlassMat / GlowMat` | No caller | Delete, unless the asset pass needs them | Low |
| T-8 | `Towns/DmTownsScenarios.cpp:461-463` | `static UDmTownsFactory* F` plus `AddToRoot()`: a rooted factory that is never released, and loads `/Game/Towns/M_TownDebug` (made by `Scripts/towns.py:419`, then cooked) | Moves with C-2. Release it in the scenario's end, and do not cook `M_TownDebug` in Shipping | Low |
| T-9 | `Towns/DmTownsPlan.cpp` (1,736 lines), `DmTownsActor.cpp` (1,463), `DmTownsKit.cpp` (1,311) | The biggest client files. `DmTownsPlan.cpp` mixes street layout, plots, fields, walls and ports | After wave 3, split the plan into `DmTownsPlanStreets.cpp`, `DmTownsPlanPlots.cpp` and `DmTownsPlanWater.cpp` (ports, quays). The kit's per-age tables move into `DmTownsKitTables.cpp`, which the asset pass will replace anyway | Low-Medium |
| T-10 | `Towns/DmTownsMesh.h:112-122`, `DmTownsPlan.cpp:101,178`, `DmTownsActor.cpp:34` | Sixth copy of the hash, noise and smooth helpers | `DmMath.h` (X-2) | Low |

### 4.7 FX (C6)

| ID | Where | Finding | Action | Risk |
|---|---|---|---|---|
| F-1 | `FX/DmFx.cpp:248` | Impacts, craters and rings are placed on `SurfaceRadiusUU`, the sim surface. The planet draws relief tens to hundreds of metres off it, so close up an effect can float above or sink into the ground | Use the ground service (X-1) | Medium (re-shoot `fxv-*`) |
| F-2 | `FX/DmFx.h:91,94,101,116,152` `IsQuiet / GetAurora / GetSim / AddUpright / GetExposureComp`; `DmFxRender.h:38` `NumPending` | No caller | Delete | Low |
| F-3 | `FX/DmFx.h:180` `UDmSimSubsystem* SimSub` | The only raw `UObject` pointer member in a `UCLASS`. Safe in practice (the game-instance subsystem outlives the actor), but it is the one exception to the rule | `TWeakObjectPtr` or `TObjectPtr` | Low |
| F-4 | `FX/DmFxEffects.cpp` (1,614 lines) | One file for every power and disaster effect | Split by family (sky-borne, ground, life, people) when the effect pass starts | Low |
| F-5 | `FX/DmFxScenarios.cpp` vs `FX/DmFxVerifyScenarios.cpp` | Builder and verifier scenarios live in two files with different conventions | Merge into one file under the dev module | Low |
| F-6 | `FX/DmFxTypes.h:121` | Another `Smooth01` | `DmMath.h` (X-2) | Low |
| F-7 | `FX/DmFx.cpp:176-180` | Calls `ADmSky::BoostAurora`, which only feeds the hidden ribbon (K-1) | Call `ADmAurora::Boost` after K-1 | Low |

### 4.8 Camera / Input (C3)

| ID | Where | Finding | Action | Risk |
|---|---|---|---|---|
| I-1 | `Camera/DmPawn.cpp:25, 67-68, 114-123`; `DmPawn.h:16-18, 43-44` | Ground source #4. The camera keeps above `SurfaceRadiusUU` (the coarse sim surface) or above a provider that **no one registers**. The spec says "never below the terrain", but the drawn terrain can stand above the sim surface, so a low pass can clip into a ridge | Use the ground service (X-1), with the band-limited query (`RadiusBand`) for speed. Delete the provider | Medium (re-run `cam-descent`, `cam-verify`) |
| I-2 | `Input/DmBrushRing.cpp:99` | The brush ring is projected on the sim surface, so it is offset from the drawn ground | Use the ground service (X-1) | Low |
| I-3 | `Camera/DmCameraScenarios.cpp:399`, `Camera/DmCameraVerify.cpp:88` | The same ground lambda written twice | Gone with X-1 | Low |
| I-4 | `Camera/DmPawn.h:63` `WorldToPlanetDir`; `Input/DmBrushRing.h:40-43` `GetColor / HasMaterials / GetReticleAlpha`; `DmInputRouter.h:84` `GetMappingContext` | No caller | Delete | Low |
| I-5 | `Camera/DmCameraRig.cpp:8-9` | `Smooth01 / Smoother01` | `DmMath.h` (X-2) | Low |
| I-6 | `Camera/DmCameraVerify.cpp` vs `*Scenarios.cpp` | A naming outlier (see X-4) | Rename it to `DmCameraVerifyScenarios.cpp` or fold it in | Low |

### 4.9 UI (C7 HUD, C8 screens)

| ID | Where | Finding | Action | Risk |
|---|---|---|---|---|
| U-1 | `UI/Screens/DmScreenTypes.h:39-50` (`Calamity`, `CalamityMultiplier`); `DmScreenPages.cpp:154, 206-209`; `DmScreenSubsystem.cpp:356-372` (`BeginWorld`) | **A live bug that looks like dead code.** The setup screen offers Calm / Normal / Wild, and stores the choice in `FDmNewWorldRequest::Calamity`. `BeginWorld` calls `CreateWorld(Seed, Name, bQuickStart, 0.0)` and never applies `CalamityMultiplier()` to `dm::World::settings.calamity`. `HandleBeginWorld` (`:359`) is never set by Flow either. The player's choice changes nothing | After `CreateWorld`, set `S->SimMutable()->settings.calamity = Req.CalamityMultiplier()` (or pass it through `CreateWorld`). Add a check to `screen-setup` that reads it back. Delete `HandleBeginWorld` if Flow will not use it | Low |
| U-2 | `UI/Hud/DmHudControl.cpp:17-18` (`__has_include("Game/Input/DmControl.h")`), `:63-66` (runtime class scan), `DmHudControl.h:7-12, 68, 92-96` (`Local*` state) | A stand-in control state from the lane era: the HUD's own armed power, brush, overlay, hidden flag and menu counter, used when camera-input "is not in the build". It always is now. The fallback still runs a class scan at start-up and keeps a second state that could drift. `MenuRequests()` (`:68`) even returns the local counter unconditionally | Make `FDmHudControl` a thin forwarder to `UDmControlSubsystem` (or delete it and call the subsystem directly). Remove the `Local*` fields and the class scan | Low |
| U-3 | `UI/Hud/DmHudActor.cpp:67-69`; comments `DmHudStyle.h:4`, `DmScreenStyle.h:15` | The start-up log prints "serif Palatino Linotype, sans Segoe UI" whenever a face loaded, whatever file was loaded. This is the line that playtest 1 (`docs/UNREAL.md`, issue 4) read as "the HUD ignored the shipped fonts". The comments still describe the Windows fonts | Print the real family names from `DmFonts` (for example "EB Garamond, Inter"), and fix both comments. Let the final check grep for this line | Low |
| U-4 | `UI/Hud/DmHudStyle.h:28-78` (`FDmHudStyle`: `Ink`... `Water` members, `Font(EDmFont, Px)`, `DotBrush`) vs `UI/Screens/DmScreenStyle.h:13-60` (`DmStyle::Ink()`... `Water()` functions, `Font(EFace, Px)`, `Box`, `Solid`) | Two copies of the same design tokens (the same hex values), two font enums and two brush caches. The HUD and the screens can drift apart in colour and type, which the owner would read as "not polished" | Make one `Game/UI/DmUiStyle.h` (tokens, fonts, brushes). The HUD and the screens keep only their own layout constants | Low-Medium (pixel diff `hud-*` and `screen-*` shots before and after) |
| U-5 | `UI/Hud/DmHudModel.h:257` `ClickOverlay`, `DmHudActor.h:37` `GetRoot`, `DmHudStyle.h:66` `DotBrush`, `UI/DmFonts.h:48` `CountAvailable`, `DmScreenPagesCommon.h:38` `DynBool`, `DmScreenStyle.h:59` `ScrimTop` | No caller. `ClickOverlay` is the overlay toggle the old spec gave to the HUD's overlay buttons. Check that the overlay bar really calls `SetOverlay` before deleting it | Delete them, after confirming the overlay buttons work | Low |
| U-6 | `UI/Hud/SDmHud.cpp` (1,570 lines) | One widget that holds the palette, time bar, ticker, toasts and tooltips | Split per region (`SDmHudPalette.cpp`, `SDmHudTicker.cpp`) when the UI pass touches it | Low |
| U-7 | `UI/Screens/DmScreenStyle.cpp:81` `Smooth` | Another copy | `DmMath.h` (X-2) | Low |

### 4.10 Audio (C9)

| ID | Where | Finding | Action | Risk |
|---|---|---|---|---|
| AU-0 | `Audio/DmAudioTest.*` (564 lines), `Audio/DmAudioAnalysis.*` (605 lines), `DmAudioScenarios.cpp` | Offline render and analysis (RMS, spectrum, PNG plots) used only by `audio-render` and `audio-live`, but linked into the game | Move them with C-2 | Low |
| AU-1 | `Audio/DmAudioDirector.cpp:138` `-dmaudiomaster=` | A test switch in the player's build | Behind `!UE_BUILD_SHIPPING` | Low |
| AU-2 | `Audio/DmAudioDsp.h:19-20, 34, 278` `GainToDb`, `exp2`, `SoftSat`, `ResetStats`; `DmAudioDirector.h:70,83` `GetCuesRequested`, `IsCapturing`; `DmAudioMusic.h:48` `GetGain` | No caller | Delete, or keep `SoftSat` if the limiter rework wants it | Low |
| AU-3 | `Audio/DmAudioDsp.h:22-23` `Sat01 / Lerp` | These shadow the meaning of `FMath::Lerp` inside the namespace | Use `FMath` directly | Low |

### 4.11 Flow (C10 integration)

| ID | Where | Finding | Action | Risk |
|---|---|---|---|---|
| L-1 | `Flow/DmGodPolicy.cpp` (222 lines) vs `Tools/simrun/Policies.cpp` (429 lines) | The "kind" god exists twice: the client copy acts through the control subsystem, the simrun copy through `powers::use`. Balance changes to one silently skip the other, so soak tests and simrun stop measuring the same player | Move the decision logic into a header-only policy that emits "intents" (power, cell, dt) to a sink: `Tools/simrun/PolicyCore.h`, shared by both. Each side keeps only its sink | Medium |
| L-2 | `Flow/DmFlow.h:68,72` `GetState / IsBackdrop` | No caller | Delete | Low |
| L-3 | `Flow/DmFlowScenarios.cpp` (1,359 lines) with `FPowersCheck` (`:1064`) | The largest scenario file. Like `FBridgeCheck` (`Core/DmBridgeCheck.cpp:43`), `FCheck` (`Nature/DmNatureScenarios.cpp:317`, `Sky/DmSkyAuroraScenarios.cpp:45`) and `FDmAudioCheck` (`Audio/DmAudioAnalysis.h:31`), it writes its own pass / fail tally | One `FDmCheck` (Expect, Info, Result line) in `DmScenario.h`, used by every area, with a single "DmCheck RESULT" format the final check can grep | Low |

### 4.12 Cross-module (the lead assigns these)

**X-1: one ground service (highest value).** Today there are five answers to the question "how high is the
ground here":

| # | Where | What it answers from |
|---|---|---|
| 1 | `Core/DmPicking` via `UDmSimSubsystem::SurfaceRadiusUU` (`DmSimSubsystem.h:171`) | the sim triangles; used by the camera, FX, brush ring and picking |
| 2 | `FDmNatureGround` | provider, own planet terrain, placeholder mesh, or sim |
| 3 | `FDmTownsGround` | provider, own planet terrain, placeholder mesh, or sim |
| 4 | `ADmPawn` provider | never set |
| 5 | `ADmPlanet::GroundHeightM` | the real drawn terrain; no external caller |

There are also three `FDmPlanetTerrain` snapshots in memory: the planet's, the towns' and nature's. Each runs its
own `Prepare` on a worker (about 130 ms, up to 0.5 s) whenever the planet's terrain changes.

**Plan.**
1. `ADmPlanet` publishes `TSharedPtr<const FDmPlanetTerrain, ESPMode::ThreadSafe> GetTerrain()` plus `Version`.
   The snapshot is already immutable after `Prepare`.
2. Add `Game/Core/DmGround.h`, a static service holding the current snapshot (or the sim surface until the planet
   has one). It offers `Radius(Dir)`, `RadiusBand(Dir, SpacingM)`, `Normal`, `WaterHeightM` and the river queries.
   It is thread-safe because it hands out the shared pointer, not a copy.
3. Nature, towns, camera, FX and the brush ring read from it. Delete sources 2-4, the provider hooks, N-2, T-2,
   T-4 and I-3.

Expected gain: one `Prepare` instead of three, two fewer terrain copies in memory, and the camera, FX and brush
ring sitting on the drawn ground. That directly serves "up close views look solid" and the 60 fps target.

Risk: **medium**. It changes threading (shared reads instead of private copies) and the timing of when towns and
nature first see terrain. Verify with `towns-hitch`, `towns-perf`, `nature-check`, `cam-descent`, `fxv-planet`,
`perf-play` and a full `playthrough`. Do it after wave 3 and before any new close-up art lands, because the asset
pass will make the ground mismatch far more visible.

**X-2: one math header.** `Smooth` or `Smoothstep` is defined 12 times: `Camera/DmCameraRig.cpp:8`,
`FX/DmFxTypes.h:121`, `Nature/DmNatureMesh.h:100`, `Planet/DmPlanetNoise.h:66`, `Sky/DmSkyActor.cpp:41`,
`Sky/DmSkyStorms.cpp:11`, `Sky/DmSkyWeather.cpp:26`, `Towns/DmTownsMesh.h:122`, `UI/Screens/DmScreenStyle.cpp:81`
and others. The integer hash is defined 5 times (`DmNatureMesh.h:87-99`, `DmPlanetNoise.h:15`,
`DmSkyWeather.cpp:34-40`, `DmTownsMesh.h:112-118`, `DmTownsActor.cpp:34`). Value noise / fbm is defined 5 times
(`DmPlaceholderPlanet.cpp:21`, `DmNatureMesh.h:113-114`, `DmPlanetNoise.h:134`, `DmSkyWeather.h:98`,
`DmTownsMesh.h:120-121`). `Sim/Core/Util.h:40-79` already has `sat / lerp / smooth / sstep / hash32 / hash2 /
hashf / noise3 / fbm3`.

Action: add `Game/Core/DmMath.h` with float and double `Sat`, `Smooth(e0, e1, x)`, `Smoother`, `Hash(a, b, c)`,
`Hash01`, value noise and fbm. Each module switches to it in its own change.

Risk: **low**, with one trap. The planet's and sky's noise drives what the player sees. Where a seed-exact
pattern matters (the planet relief and cloud weather), keep their exact hash: move it, don't swap it, and
pixel-diff the `orbit` and `sky-orbit` shots.

**X-3: lane-era scaffolding.** C-3 (placeholders), U-2 (HUD stand-in), the provider hooks (N-3, T-3, I-1) and
`HandleBeginWorld` (U-1) all exist so that a lane could build alone. Remove them together in one "post-lanes"
change, and add a rule to `SPEC-UNREAL.md` section 0.1: **stand-ins are deleted when the module they stand in for
is verified.**

**X-4: naming.**
- Actor files are named three ways: `DmPlanet.cpp` (class `ADmPlanet`), `DmSkyActor.cpp` (`ADmSky`),
  `DmNatureActor.cpp` (`ADmNature`), `DmTownsActor.cpp` (`ADmTowns`), `DmFx.cpp` (`ADmFx`) and `DmSkyAurora.cpp`
  (`ADmAurora`). Spec 0.2 and the `DmScene.h:6` example say `DmPlanetActor.h`.
- Test files are named five ways: `*Scenarios.cpp`, `*Verify.cpp`, `*VerifyScenarios.cpp`, `DmBridgeCheck.cpp`,
  `DmAudioTest.cpp`.
- One test file has a hyphen: `SimTests/test_path-units_adv.cpp`.

Proposal: `<Area>/Dm<Area>.h/.cpp` holds the actor, `Dm<Area>Scenarios.cpp` holds every scenario of an area
(builder and verifier alike), and `test_<module>[_<topic>].cpp` uses underscores only. Rename once, in one change
after C-2, so `lane.ps1` ownership lists are updated in one go.

Risk: low. Renames break open lanes, so do them when no lane is open.

### 4.13 tools, build scripts and repository hygiene

| ID | Where | Finding | Action | Risk |
|---|---|---|---|---|
| R-1 | `SERSRELLYAPPDATAocaltemp/claude/.../scratchpad/dbg1.png` at the repository root | A stray folder created by a Bash call that lost the backslashes of a Windows temp path. It is not ignored (`*.png` is only ignored under `art/`), so the next `git add -A` commits it | Delete the folder. Agents must quote temp paths or use forward slashes (add one line to `docs/BUILD.md`) | Low |
| R-2 | `.gitignore:33` `Unreal/Lanes/`, plus Git's case-insensitive matching on Windows | Each lane's `.owned` list (the file-ownership contract of SPEC-UNREAL 0.1) and `Unreal/Lanes/README.md` (the client engineers' quick start) live inside the ignored folder, so neither is versioned. `.gitignore:53` (`Unreal/lanes/*.owned.bak`) shows they were meant to be | Move the `.owned` lists and the README to a versioned `Unreal/LaneOwners/` (update `lane.ps1`), or add `!Unreal/Lanes/*.owned` and `!Unreal/Lanes/README.md` | Low |
| R-3 | `Unreal/Lanes/` (31 GB on disk, ignored) | About 30 finished build and verify lanes are kept after sync | `lane.ps1 remove <key>` for every lane whose marker in `docs/handoff/done-ue/` is final. Add `lane.ps1 prune --done` | Low |
| R-4 | `docs/audio-preview/*.wav` (26 files, ~11 MB each, ~280 MB) | Versioned uncompressed previews in a public repository. Every regeneration adds another ~280 MB to the history | Commit FLAC or Ogg (about 10x smaller), or ignore them and regenerate with `tools/audio-preview/run.ps1` | Low |
| R-5 | `shots/` (1,013 non-`ue-*` images, 252 MB, versioned on purpose by `.gitignore:19`) | Prototype-era history. It is the bulk of every clone | Move them to a release asset or an archive branch and keep only the finals | Low (owner's call: the ignore comment says it was intentional) |
| R-6 | Root `js/`, `css/`, `dev/`, `tests/`, `index.html`, and the JS half of `tools/` (`api.js`, `server.js`, `shot.js`, `t.js`, `test.js`, `load.js`, `mapimg.js`, `smoke/`) | The WebGL prototype sits at the repository root beside live tools (`tools/render_slot.py`, `cpu_governor.ps1`, `flow_provenance.py`, `audio-preview/`, `lead/`). New agents cannot tell which is which. `server.js:39-42` sends `Access-Control-Allow-Origin: *` with a write endpoint (`security-review.md` L4) | Move the prototype into `legacy/web-prototype/` (or tag it and delete it). Keep the live tools in `tools/`. Update `README.md`'s layout table. Retire `server.js`, or drop its CORS header | Low |
| R-7 | `docs/FOUNDATION.md`, `docs/SPEC-RENDER.md`, `docs/SPEC-UI.md`, `docs/handoff/done/` (3 JS markers), `docs/handoff/critique-*.txt`, `spec-critique-raw.json` | Superseded or one-off documents next to the binding specs | Move them to `docs/legacy/` and `docs/handoff/archive/`. `SPEC-RENDER` and `SPEC-UI` remain the "what" reference by SPEC-UNREAL's rule, so link them from there | Low |
| R-8 | `UnrealLab/` (17 GB on disk; seven copies named `PlanetLab.uproject`, each with its own `Source/`) | Graphics labs whose results are ported into `Game/` (`DmPlanetNoise.h:2`, `DmPlanetTerrain.h:2`, `DmSkyWeather.cpp:2` name their lab sources). Only `data/planet_7.lab` is still needed at run time (C-4) | After C-4, version only each lab's `Source/`, `Scripts/` and write-up. Delete the labs' `Intermediate`, `Binaries`, `Saved` and DDC on disk. Mark the labs read-only references in `README.md` | Low |
| R-9 | `Unreal/Scripts/nature-mats.ps1:12`, `planet/mats.ps1:7`, `aurora/mats.ps1:8` | Three hand-copied runners for headless material builds. Each hardcodes `C:\Program Files\Epic Games\UE_5.7\...`, and they sit in inconsistent places (one at the folder root, two in subfolders). `nature-mats.ps1:4-5` notes that a project path with a space breaks `-script=`, and copies the script to `%TEMP%`; `planet/mats.ps1` and `aurora/mats.ps1` do not | One `Scripts/run-content.ps1 <key>` that finds the engine (`EngineAssociation`, then the registry, then an env var), does the `%TEMP%` copy, and runs one or all of `bridge / camera-input / fx / nature / planet / sky / towns / aurora`. Delete the three runners | Low |
| R-10 | `SimTests/stubs/*_stub.cpp` (17 files) | Still useful for single-module isolation runs, but every Sim header change now needs a stub edit | Keep them. Add a `run.ps1 stubs` check that compiles every stub against the current headers, so drift fails loudly | Low |
| R-11 | `.gitignore` | `__pycache__/` is listed twice (`:52` and `:66`, plus the redundant `UnrealLab` rule at `:51`). Rules are in no clear order. `*.jpg` and `*.wav` are unbounded | Sort it into sections (engine, lanes, art outputs, media, legacy). Add a size guard: a pre-commit check refusing files over 8 MB outside `assets/` | Low |
| R-12 | `Unreal/Tools/simrun/build`, `build-tests` (106 MB), `SimTests/build` (1.2 GB) | Ignored, but they grow without bound across agent runs | Add a `-Clean` switch to `run.ps1` and `simrun.ps1` | Low |

### 4.14 art scripts (asset pipeline)

| ID | Where | Finding | Action | Risk |
|---|---|---|---|---|
| A-1 | `art/*/` builders: `export_fbx` x 13 (for example `age0/build_age0.py:186`, `age3/build_age3.py:710`, `ships/build_ships.py:199`, `units-land/build_units-land.py:1805`, `tools/build_asset.py:159`); `make_material` x 10; `lathe` x 8 (`age0/a0_geo.py:402`, `age1/age1_geo.py:265`, `age4/lib/a4geo.py:333`, `age6/age6_kit.py:239`, `props/props_lib.py:172`, `wonders-a/wa_lib.py:202`, `tools/dmkit.py:361`...); `tri_count` x 9; `decimate_to` x 4; `vnoise` x 4; `srgb_to_lin` x 6; `setup_render` x 5 | `art/tools/dmkit.py:1` says it is "shared by every asset script", but only `art/tools/` imports it. Every family (age0-6, culture, props, ships, animals, units, wonders) grew its own kit in parallel. The FBX export rules (axis turn, LOD group, UCX) are re-implemented 13 times. That is where the verifier found real defects: age3's inside-out beams and the loose vertices that inflated bounds | Make `art/tools/dmkit.py` (geometry), `dmtex.py` (textures) and a new `dmexport.py` (FBX, LOD, manifest, `tri_count`, decimation) the only implementations. Port one family at a time, and require byte-identical `manifest.json` plus identical triangle counts before and after (the verifiers already check both) | Medium (asset output must not change) |
| A-2 | `art/export/**` (FBX) and `Unreal/Content/` | No mesh from the asset pipeline is in the game: `Content/` holds only materials, one texture and fonts, and `Game/` loads no imported mesh (`LoadObject<UStaticMesh>` is used only for engine basic shapes, `DmSkyActor.cpp:492,526`). Fourteen asset families (age0-6, animals, culture, props, ships, units-land, wonders-a, wonders-b) are verified but unused | Not dead code to delete, but **disconnected output**. The asset-integration pass should import through `art/tools/ue_import.py` into `/Game/Art/<Family>`, and the towns factory (`DmTownsKit.h:3`) should swap mesh ids for imported ones. Until then, do not polish the procedural stand-ins further (owner direction in SPEC-UNREAL 0.4) | Medium (owner-visible) |
| A-3 | `art/style-pilot/` (1.3 GB), `art/textures-pilot/`, `art/textures-flow/` | Three generations of texture experiments side by side; only one will feed the game | When the material pass picks a set, move the losers to `art/archive/`. Keep each Flow batch's provenance file (`tools/flow_provenance.py`) | Low |
| A-4 | `art/*/` folders with `*.blend1` | Blender backup files on disk (ignored, but they double disk use) | Set `bpy.context.preferences.filepaths.save_version = 0` in the kit's save helper | Low |

## 5. Compiler warnings

**Now.**
- The Unreal module compiles with Unreal's defaults. The last 78 build logs in `%TEMP%\demiurge-ue-build-*.log`
  contain no `warning C`. The only warning ever recorded is an old `LNK4006` (a duplicate
  `FDmTownsGround::Radius` symbol, since fixed).
- The Sim, standalone (`SimTests/run.ps1:175`, `Tools/simrun/simrun.ps1:98`), compiles at `/W4 /permissive-`, with
  shadowing (`C4456-4459`), macro redefinition (`C4005`) and undefined-macro (`C4668`) as errors.

**Proposal**, in this order. Each step is one build in a lane.
1. `Demiurge.Build.cs`: raise the module's own warnings. In UE 5.5 and later these live under
   `CppCompileWarningSettings`; the old top-level `ShadowVariableWarningLevel` fields are deprecated, so check
   the 5.7 `ModuleRules` reference.
   - `ShadowVariableWarningLevel = WarningLevel.Error` (matches the Sim's standalone rule)
   - `UnsafeTypeCastWarningLevel = WarningLevel.Warning`
   - `bEnableUndefinedIdentifierWarnings = true`

   Expected cost: Game code may show some `float`/`double` narrowing (many `float(...)` casts already exist).
   Risk: low; it is build-only.
2. One survey build of `Game/` only at `/W4`, through a `#pragma warning(push, 4)` wrapper in a lane, never
   committed. File what it finds per module. Do not adopt `/W4` for Game: engine headers are noisy at that level.
3. `Demiurge.Target.cs`: `bWarningsAsErrors = true` for the Game target **only after** step 1 is clean, so an
   agent cannot sync a warning to main. Risk: low, but it can block a lane's sync, so announce it in `AGENTS.md`.

## 6. Verification set for every item

| Changed | Run |
|---|---|
| `Sim/` | `SimTests/run.ps1 all`; `simrun --hash` 8 seeds x 4 policies, each run 3 times, all identical; chronicle entry count identical |
| Core / Flow | `bridgecheck`, `play` (title, then genesis, then history), `first-minutes`, `playthrough`, `perf-play` |
| Planet / ground service | `orbit`, `planet-fly`, `towns-hitch`, `towns-perf`, `nature-check`, `cam-descent`, `cam-verify`, `fxv-planet`, plus a pixel diff of `orbit` |
| Sky | `sky-check`, `aurora-check`, `sky-orbit` (pixel diff), `fxv-at` (aurora surge) |
| Nature / Towns | `nature-check`, `towns-ages`, `towns-live`, `towns-stress`, `towns-perf` |
| UI | every `hud-*` and `screen-*` scenario, plus a pixel diff when U-4 lands; `screen-setup` must read back the calamity value (U-1) |
| Audio | `audio-render`, `audio-live` |
| Build / packaging (C-2, C-5, C-8, C-9) | a clean editor build, then `package.ps1`. In the packaged game, `-scenario=towns-city` must do nothing in Shipping and the title must come up with the shipped fonts named in the log (U-3) |
| art (A-1) | byte-identical `manifest.json` and identical triangle counts for the ported family |

## 7. Order of work

1. **Right after wave 3 hands the files back (half a day in all, all low risk):** U-1 (calamity bug), U-3 (font
   log), C-5 (unused modules), C-8, R-1, R-2, S-8.
2. **Post-lanes cleanup (one change per module, low risk):**
   - C-3 with N-2 and T-2 (placeholders)
   - U-2 (HUD stand-in)
   - the provider hooks N-3, T-3 and I-1's provider
   - the "no caller" lists C-6, N-6, T-5, T-7, F-2, I-4, U-5, AU-2, L-2, K-3, S-1, S-2
3. **K-1 (one aurora)**, then F-7.
4. **X-1 (ground service)**, then F-1, I-1, I-2. Measure before and after with `perf-play` and `towns-hitch`.
5. **C-2 (dev-only harness)** with C-4, N-4, AU-0, AU-1 and L-3. Then X-4 renames, while no lane is open.
6. **X-2 (math header)** and **U-4 (one UI style)**, each with a pixel diff.
7. **L-1 (shared policy core)**; S-6 and S-7 once the determinism alarm is closed; the T-9, F-4 and U-6 file splits
   when those modules are next touched.
8. **Repository (any time, the owner's call for R-5 and R-6):** R-3 to R-12. **Art:** A-1 one family per
   change; A-2 belongs to the asset-integration pass.

Items to wire rather than delete, to hand to their owners: S-3 `drownRisk` (HUD brush warning), S-4 `ruler` /
`ship` names (story v4), T-6 `SmokeAnchors` (FX town smoke), C-6 `SelectCulture` (HUD).
