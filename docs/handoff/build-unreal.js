export const meta = {
  name: 'demiurge-build-unreal',
  description: 'Port and build the DEMIURGE simulation in C++20 inside the Unreal 5.7 project: foundation, 15 sim modules (each built then independently verified), integration; at most 6 agents at a time, Opus for complex work and Haiku for mechanical work, finished work marked on disk',
  phases: [
    { title: 'Foundation', detail: 'Unreal skeleton, C++ core + state + schema + harness, module headers and stubs, independent verification' },
    { title: 'Stage A build', detail: '15 simulation modules ported/built in C++ with tests' },
    { title: 'Stage A verify', detail: 'independent adversarial reviewer per module, fixes in place' },
    { title: 'Integration', detail: 'whole simulation across seeds, balance, determinism, performance, Unreal build' },
  ],
}

const ROOT = 'C:/Users/Relly/Documents/Fable5.2 Test/demiurge'
const UE = 'C:/Program Files/Epic Games/UE_5.7'
const OPUS = { model: 'opus', effort: 'high' }
const HAIKU = { model: 'haiku', effort: 'xhigh' }

const REPORT = {
  type: 'object',
  properties: {
    status: { type: 'string', enum: ['done', 'partial', 'blocked'], description: 'done = every acceptance check passed and the marker file is written; partial = real progress but not finished; blocked = could not proceed' },
    report: { type: 'string', description: 'The full report: what was built, deviations from the spec, commands run with pass counts, acceptance results with real numbers, map/screenshot files, known weaknesses, decisions for the lead' },
  },
  required: ['status', 'report'],
}

const HEAD = `You are a senior engineer on DEMIURGE, a 3D god game on one small living planet. The project has just moved from a zero-dependency WebGL/JavaScript build to Unreal Engine 5.7 C++. The simulation is being ported to plain, engine-independent C++20 that compiles both inside Unreal and in a fast standalone test build.
Project root (Windows): "${ROOT}". The path contains a space: always quote it. Unreal 5.7 is installed at "${UE}". Visual Studio 2022 Community (MSVC 14.44, Windows SDK 10.0.22621) is installed; its environment comes from "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat". You have PowerShell and Git Bash.
Several engineers work in this folder at the same time and you cannot talk to them; the documents in docs/ are the contract between you.

Rules (binding):
1. Edit only the files you are told you own. Never touch anything outside the project root (the parent folder holds an unrelated finished game). Never edit docs/ files other than the ones assigned to you. No network, no git, no package managers, no downloads.
2. Simulation code under Unreal/Source/Demiurge/Sim/ is standard C++20 only: no Unreal headers, no exceptions (no throw/try/catch), no RTTI (no dynamic_cast/typeid), no global mutable state (state lives in the World), no std::rand / <random> distributions / unordered-container iteration order driving decisions (all randomness from the project RNG, deterministic order everywhere). Never use identifiers that collide with Unreal macros: check, verify, ensure, checkf, TEXT, PI, HALF_PI, INDEX_NONE, SMALL_NUMBER, KINDA_SMALL_NUMBER, BIG_NUMBER, DELTA, FLT_MAX-like redefinitions, TCHAR, UE_LOG; the standalone build force-includes a guard header that turns them into errors. Avoid variable shadowing (Unreal treats it as an error).
3. Do not open the Unreal editor GUI. Only the foundation and integration engineers run Unreal/build.ps1 (Unreal's build tool, one at a time); everyone else builds with SimTests/run.ps1.
4. Prove it. Run your checks and quote the real numbers. Look at your own output: sim engineers write PNG maps with the map tool and inspect them with the image-reading (Read) tool. "It compiles" is not evidence.
5. Keep temporary files out of the tree. Maps go in "${ROOT}/shots/ue-<yourkey>-*.png".
6. This build may have been interrupted by a usage limit. Before writing, look at your files: if one already holds real work, an earlier engineer was cut off. Judge it against the spec; finish and fix it if it is sound, rewrite it if it is poor.
7. Your final answer is a report object: status and a full report (what you built, deviations from the spec, commands run with pass counts, acceptance results with numbers, map files, known weaknesses, anything the lead must decide). No padding.`

const SIMREAD = (extra) => `Read, in this order, completely:
  1. docs/UNREAL.md (why and how the project moved to Unreal, and the layout)
  2. docs/CONCEPT.md (the game; ignore the old-tech parts)
  3. docs/FOUNDATION-CPP.md (binding C++ foundation: types, World, RNG, events, geometry, module headers, harness, ownership)
  4. docs/SPEC-SIM.md in full (sections 0-2 are the shared rules, entities, writers table and events; then your module section; read the others too, because you call them and they call you). It is written in JavaScript terms: FOUNDATION-CPP.md says how each JS construct maps to C++.
  5. docs/BUILD.md: the acceptance checks for your assignment (translate any JS tooling they mention to the C++ harness)
${extra || ''}Then read the C++ you build on: Unreal/Source/Demiurge/Sim/Core/*, State, Schema, your module's header, SimTests/ (framework, helpers, run.ps1, stubs) and Unreal/Tools/.`

// ---- Stage A modules ----------------------------------------------------------
// key, owner files, spec, JS reference, build model, verify model, notes
const A = [
  { key: 'names', id: 'A1', files: 'Sim/Names.h, Sim/Names.cpp, SimTests/test_names.cpp, SimTests/stubs/Names_stub.cpp (keep it matching your header)', js: 'none: build fresh from the spec (owner decision in docs/UNREAL.md)', build: HAIKU, verify: HAIKU,
    notes: 'Names carry the whole fiction: a player should tell two cultures apart by their place names alone, and want to read them aloud. Port faithfully; keep the phonology tables as data. Check variety and pronounceability by printing a few hundred names per culture and reading them.' },
  { key: 'terrain', id: 'A2', files: 'Sim/Terrain.h, Sim/Terrain.cpp, SimTests/test_terrain.cpp, SimTests/stubs/Terrain_stub.cpp', js: 'js/sim/terrain.js (passed its own tests in JS)', build: OPUS, verify: OPUS,
    notes: 'The planet shape is the first thing the player sees and everything else follows from it. Render elevation maps constantly and judge them like a cartographer: continents with character (domain-warped shapes, mountain belts, peninsulas, archipelagos, shelves, a few trenches), not fBm blobs.' },
  { key: 'climate', id: 'A3', files: 'Sim/Climate.h, Sim/Climate.cpp, SimTests/test_climate.cpp, SimTests/stubs/Climate_stub.cpp', js: 'js/sim/climate.js (passed its own tests in JS)', build: OPUS, verify: OPUS,
    notes: 'Do not depend on the real terrain in tests (use the world helper or synthetic elevation). Rain shadows and wet/dry belts must be visible on a map. Get the genesis rules right (datum, calibrate, the yearly pass doing nothing before history): a reviewer showed that mistakes there freeze the planet or turn sea level into NaN.' },
  { key: 'hydro', id: 'A4', files: 'Sim/Hydro.h, Sim/Hydro.cpp, SimTests/test_hydro.cpp, SimTests/stubs/Hydro_stub.cpp', js: 'none: build fresh from the spec (owner decision in docs/UNREAL.md)', build: OPUS, verify: OPUS,
    notes: 'Rivers are a signature visual and decide where cities grow. Render maps with rivers and lakes to judge them.' },
  { key: 'life', id: 'A5', files: 'Sim/Life.h, Sim/Life.cpp, SimTests/test_life.cpp, SimTests/stubs/Life_stub.cpp', js: 'js/sim/life.js (passed its own tests in JS)', build: OPUS, verify: OPUS,
    notes: 'The seeding wave of green spreading over the continents is a key genesis moment; biomes must look believable on a map.' },
  { key: 'path-units', id: 'A6', files: 'Sim/Path.h, Sim/Path.cpp, Sim/Units.h, Sim/Units.cpp, SimTests/test_path.cpp, SimTests/test_units.cpp, SimTests/stubs/Path_stub.cpp, SimTests/stubs/Units_stub.cpp', js: 'none: build fresh from the spec (owner decision in docs/UNREAL.md)', build: OPUS, verify: OPUS,
    notes: 'Pathfinding must be cheap at L = 6 (40,962 cells) and deterministic (tie-break on cell id).' },
  { key: 'civ', id: 'A7', files: 'Sim/Civ.cpp (the lifecycle half of the Civ namespace) and SimTests/test_civ.cpp, SimTests/stubs/Civ_stub.cpp; you share Sim/Civ.h with the economy engineer: you own the lifecycle declarations, they own the economy declarations; add, never rename', js: 'none: build fresh from the spec (owner decision in docs/UNREAL.md)', build: OPUS, verify: OPUS,
    notes: 'Settlements founding, growing, splitting into cultures and dying is the heart of the history. Render owner maps over time.' },
  { key: 'economy', id: 'A8', files: 'Sim/Economy.cpp (the economy half of the Civ namespace), SimTests/test_economy.cpp, SimTests/stubs/Economy_stub.cpp; you share Sim/Civ.h with the civ engineer: you own the economy declarations only', js: 'none (placeholder)', build: OPUS, verify: OPUS,
    notes: 'Build from the spec. Keep it cheap and stable: no runaway growth, no starvation spirals unless the rules call for them.' },
  { key: 'society', id: 'A9', files: 'Sim/Society.cpp (belief half), SimTests/test_society.cpp, SimTests/stubs/Society_stub.cpp; you share Sim/Society.h with the diplomacy engineer: you own the belief declarations', js: 'none (placeholder)', build: OPUS, verify: OPUS,
    notes: 'Belief in the god, Awe and epithets are how the player feels seen. Make every power the player uses register in belief in a way a test can show.' },
  { key: 'diplomacy', id: 'A10', files: 'Sim/Diplomacy.cpp (relations half), SimTests/test_diplomacy.cpp, SimTests/stubs/Diplomacy_stub.cpp; you share Sim/Society.h: you own the relations declarations', js: 'none (placeholder)', build: OPUS, verify: OPUS,
    notes: 'Trade, alliances and wars must emerge from the rules and be rare enough to matter.' },
  { key: 'powers', id: 'A11', files: 'Sim/Powers.cpp (power table, use and instant effects), SimTests/test_powers.cpp, SimTests/stubs/Powers_stub.cpp; you share Sim/Powers.h with the disasters engineer: you own the power declarations', js: 'none (placeholder)', build: OPUS, verify: OPUS,
    notes: 'These are the player\'s hands: every power must have an immediate, visible consequence in world state. The power table in the spec is binding data. When the disaster functions are only stubs, comet / meteor / volcano must fail gracefully (return false). Real-time rate limits count from the dt arguments, never a clock.' },
  { key: 'disasters', id: 'A12', files: 'Sim/Disasters.cpp (start, update, tick, natural), SimTests/test_disasters.cpp, SimTests/stubs/Disasters_stub.cpp; you share Sim/Powers.h: you own the disaster declarations', js: 'none (placeholder)', build: OPUS, verify: OPUS,
    notes: 'Meteors leave craters and tsunamis, volcanoes build islands, lightning starts fires: spectacle with lasting consequences, all deterministic.' },
  { key: 'chronicle', id: 'A13', files: 'Sim/Chronicle.h, Sim/Chronicle.cpp, SimTests/test_chronicle.cpp, SimTests/stubs/Chronicle_stub.cpp', js: 'none (placeholder)', build: OPUS, verify: HAIKU,
    notes: 'The chronicle reads like a translated ancient text: mythic, warm, a little wry, never repetitive. Print a few hundred entries from varied events and read them critically. Wording draws from a forked RNG so prose never disturbs the sim stream.' },
  { key: 'sim', id: 'A14', files: 'Sim/Sim.h, Sim/Sim.cpp, Sim/Balance.h, Sim/Balance.cpp, Unreal/Tools/simrun (C++ headless runner, with policies passive/kind/cruel/chaotic), SimTests/test_sim.cpp, SimTests/stubs/Sim_stub.cpp', js: 'none: build fresh from the spec (owner decision in docs/UNREAL.md)', build: OPUS, verify: OPUS,
    notes: 'The tick scheduler (at most one heavy pass per tick), phases from genesis to ended, and the headless runner every later engineer uses for thousand-year runs. Make simrun print the per-seed table the integrator needs and support --hash for determinism checks.' },
  { key: 'save', id: 'A15', files: 'Sim/Save.h, Sim/Save.cpp, SimTests/test_save.cpp, SimTests/stubs/Save_stub.cpp', js: 'js/save.js (placeholder; SPEC-UI section 6 has the old rules)', build: HAIKU, verify: OPUS,
    notes: 'Versioned binary serialization of the entire World (every per-cell field, every entity list, RNG state, clocks, flags) with a round-trip test that compares a full hash before and after, plus rejection of truncated or wrong-version data without crashing.' },
]

// ---- pacing ---------------------------------------------------------------
const SKIP = new Set((args && args.skip) || [])
const BUILT = new Set((args && args.built) || [])
const LIMIT = (args && args.limit) || 6
let active = 0
const waiters = []
async function slot(fn) {
  while (active >= LIMIT) await new Promise((r) => waiters.push(r))
  active++
  try { return await fn() } finally { active--; const w = waiters.shift(); if (w) w() }
}
const run = (prompt, opts) => slot(() => agent(prompt, { schema: REPORT, ...opts }))
const ok = (r) => r && r.status === 'done'
const results = {}

// ---- Foundation -------------------------------------------------------------
phase('Foundation')

const SKELETON = `${HEAD}

YOUR ASSIGNMENT: the Unreal project skeleton (key "skeleton"). You own everything under Unreal/ EXCEPT Unreal/Source/Demiurge/Sim/, Unreal/SimTests/ and Unreal/Tools/ (colleagues own those).
Read docs/UNREAL.md first.
Create:
- Unreal/Demiurge.uproject: EngineAssociation "5.7"; one Runtime module "Demiurge" (LoadingPhase Default); plugins enabled: PythonScriptPlugin, EditorScriptingUtilities, ProceduralMeshComponent, GeometryScripting.
- Unreal/Source/Demiurge.Target.cs (Game) and Unreal/Source/DemiurgeEditor.Target.cs (Editor): DefaultBuildSettings = BuildSettingsVersion.Latest, IncludeOrderVersion = EngineIncludeOrderVersion.Latest, ExtraModuleNames "Demiurge".
- Unreal/Source/Demiurge/Demiurge.Build.cs: PCHUsage UseExplicitOrSharedPCHs, bUseUnity = false, CppStandard = CppStandardVersion.Cpp20, public deps Core, CoreUObject, Engine, InputCore, EnhancedInput, ProceduralMeshComponent, GeometryScriptingCore, GeometryFramework, UMG, Slate, SlateCore; add the module's Sim folder to PublicIncludePaths (ModuleDirectory + "/Sim").
- Unreal/Source/Demiurge/Demiurge.h/.cpp: IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, Demiurge, "Demiurge").
- Unreal/Source/Demiurge/Game/DemiurgeGameMode.h/.cpp: an empty ADemiurgeGameMode (AGameModeBase) so the module has one UCLASS.
- Unreal/Source/Demiurge/Sim/.keep (empty) so the folder exists.
- Unreal/Config/DefaultEngine.ini with GlobalDefaultGameMode=/Script/Demiurge.DemiurgeGameMode and renderer settings for a cinematic look: Lumen global illumination and reflections, virtual shadow maps, Nanite on, ray tracing off, a sensible anti-aliasing (TSR), r.DefaultFeature.AutoExposure on; plus DefaultGame.ini and DefaultInput.ini minimal (enhanced input as default classes).
- Unreal/build.ps1: runs "${UE}/Engine/Build/BatchFiles/Build.bat" DemiurgeEditor Win64 Development -Project="<abs path to Demiurge.uproject>" -WaitMutex -NoHotReload, prints a short summary (Succeeded/Failed, elapsed seconds, and every line containing "error" or "warning C"), and exits with the build's exit code. It must work from any working directory and with the space in the path.
Then run Unreal/build.ps1 until it succeeds, and run it once more to confirm a clean incremental build. Write docs/handoff/done-ue/skeleton.built.txt (one line: date, "skeleton", build result and seconds) only after it succeeds.`

const CORE = `${HEAD}

YOUR ASSIGNMENT: the C++ foundation, part 1 (key "foundation-core"). You own Unreal/Source/Demiurge/Sim/Core/*, Sim/State.h/.cpp, Sim/Schema.h/.cpp, everything under Unreal/SimTests/ except stubs/ and module tests, Unreal/Tools/MapImg.*, and docs/FOUNDATION-CPP.md (part 1).
Read completely: docs/UNREAL.md, docs/CONCEPT.md, docs/FOUNDATION.md (the JS foundation you are porting), docs/SPEC-SIM.md sections 0-2 (rules, entities, writers, events), docs/BUILD.md "Rules for everyone" and "Tools". Then the JS you port: js/core/util.js, js/core/vec.js, js/core/events.js, js/core/geo.js, js/sim/state.js, js/sim/schema.js, tests/helpers/world.js, tools/t.js, tools/mapimg.js, and the JS tests tests/util|vec|events|geo|geo.adv|state.test.js.
Build:
1. Core (namespace dm): utilities and the deterministic RNG with string-keyed fork (same algorithm family as DM.R; bit-exact equality with JS is NOT required, determinism within C++ IS), vectors/matrices, the event bus (owned by the World, not global; typed events covering every entry of DM.EVENTS with their payload fields; subscribers via std::function; an event-name table so tests can check every emitted event is a known one), and geometry (icosphere level L with neighbour lists, cell positions, step, lookup of the nearest cell to a direction, every DM.Geo function).
2. State and Schema: the World (per-cell fields with the same element types as the JS typed arrays, clocks, phase, flags, dirty and need flags, entity lists), BIOME / AGES enums and tables, VEG_T, entity structs and factories, BUILDINGS, CULTURE_COLORS, and every DM.State function. Entities live in vectors and are addressed by stable integer ids.
3. Unreal/SimTests: a tiny test framework header (test registration, ok / eq / near / expect-no-NaN, a per-test event watcher that checks every emitted event is in the event table), a world helper (makeWorld({L, seed, land, basics}), fillBasics, bestHab, findLand, checkFinite, capture, hash), SimTests/UeMacroGuard.h (force-included in standalone builds: makes the Unreal macro names from rule 2 compile errors if used), and SimTests/run.ps1 <name>: imports the vcvars64 environment, compiles with cl /std:c++20 /permissive- /W4 /we4456 /we4457 /we4458 /we4459 /O2 /FI UeMacroGuard.h the core + State + Schema + Sim/<Module>.cpp for the module(s) named + SimTests/stubs/*_stub.cpp for every OTHER module + test_<name>*.cpp + helpers, into SimTests/build/<name>/ (so parallel engineers never collide), runs the tests, and prints one summary line "<name> N passed, M failed". Also "run.ps1 all" (all real modules, no stubs, all tests) for the integrator. Until the module headers exist (part 2, a colleague after you) the stub and module lists may be empty: make run.ps1 tolerate that. Use /MP and keep a single module's rebuild fast.
4. Unreal/Tools/MapImg: a dependency-free PNG writer (stored/deflate-uncompressed is fine) and equirectangular map renderers elev, biome, field(world, name, file, min, max), owner, custom, with rivers/lakes overlays when present, callable from tests and tools.
5. Port the core and state JS tests to C++ and make them pass (run.ps1 core, run.ps1 state). Render an elevation and a biome map of a helper world and look at them.
6. docs/FOUNDATION-CPP.md part 1: conventions (namespace, naming, ownership of files, how each JS construct maps to C++: DM.X.fn -> dm::x::fn(World&,...); typed arrays -> std::vector fields; events; TUNE blocks -> per-module tuning structs held in the World so balance can change them; scratch buffers keyed by world size held in the World, never static), the full core / State / Schema API, the harness and map tool, and the rules engineers must follow (copy rule 2 of this brief). Write it so a module engineer needs nothing else.
When everything passes, write docs/handoff/done-ue/foundation-core.built.txt (one line: date, key, test summary).`

const API = `${HEAD}

YOUR ASSIGNMENT: the C++ foundation, part 2 (key "foundation-api"). Part 1 (core, State, Schema, SimTests harness, MapImg, docs/FOUNDATION-CPP.md part 1) was just finished by a colleague; build on it and change it only to fix real defects (say which). You own every module header and stub and the initial module source files: Unreal/Source/Demiurge/Sim/{Names,Terrain,Climate,Hydro,Life,Path,Units,Civ,Society,Powers,Chronicle,Sim,Balance,Save}.h, Sim/{Names,Terrain,Climate,Hydro,Life,Path,Units,Civ,Economy,Society,Diplomacy,Powers,Disasters,Chronicle,Sim,Balance,Save}.cpp (initial contents only), Unreal/SimTests/stubs/*_stub.cpp, SimTests/test_link.cpp, and docs/FOUNDATION-CPP.md part 2.
Read completely: docs/UNREAL.md, docs/FOUNDATION-CPP.md, docs/SPEC-SIM.md (all of it), docs/SPEC-UI.md section 6 (old save rules), docs/BUILD.md (all of it: you are fixing the contract every Stage A engineer will build to), and skim the JS reference modules in js/sim/ to see the APIs they implemented.
Build:
1. One header per module (pairs share one header: Civ.h for civ + economy, Society.h for society + diplomacy, Powers.h for powers + disasters) declaring the complete public API from SPEC-SIM section 3 as C++ (namespace dm::<module>, functions taking World& / const World&), its tuning struct with the spec's starting values, and a clear comment per function stating the rule it implements and which engineer owns it (for split pairs, mark each declaration's owner). Save.h: versioned binary save / load of the whole World. Balance.h: applying balance overrides to the tuning structs. Sim.h: TICK, REAL_PER_TICK, use / tick / advance / phases, plus the headless-runner hooks the simrun tool needs.
2. One stub per .cpp file in SimTests/stubs/ (Civ_stub.cpp and Economy_stub.cpp separately, and so on) with neutral implementations: return false / null-equivalents / no-ops, never crash, never write NaN.
3. Initial Sim/<Module>.cpp files with the same neutral contents as the stubs, so the full Unreal build always links. Module engineers will replace them.
4. SimTests/test_link.cpp: a test that calls every declared function once through the stubs on a small world (proves the headers, stubs and run.ps1 agree). Make run.ps1 <module> work for every Stage A key: names, terrain, climate, hydro, life, path-units (path + units), civ, economy, society, diplomacy, powers, disasters, chronicle, sim, save; and run.ps1 all.
5. docs/FOUNDATION-CPP.md part 2: the module map (key, owned files, header, which functions), the writers table in C++ terms, how modules call each other (direct calls through the headers; in a standalone test the others are stubs, so tests set up world fields directly through the helper), the tick order, and a per-key "run.ps1 <key>" table.
6. Run Unreal/build.ps1 (Unreal build) and make it succeed with the real core and the neutral module files. Run run.ps1 link and run.ps1 all.
When everything passes, write docs/handoff/done-ue/foundation-api.built.txt (one line: date, key, test summary, Unreal build result).`

const FVERIFY = (reports) => `${HEAD}

YOUR ASSIGNMENT: independent adversarial verification of the whole foundation (key "foundation"): the Unreal skeleton, part 1 (core, State, Schema, harness, MapImg) and part 2 (module headers, stubs, initial module files, FOUNDATION-CPP.md). Fifteen engineers will build on this the moment you sign off, in parallel, unable to talk to each other: any ambiguity or defect here multiplies. You may fix anything in these files; record every fix.
Read docs/UNREAL.md, docs/FOUNDATION-CPP.md, docs/FOUNDATION.md, docs/SPEC-SIM.md in full, docs/BUILD.md, then all the code. The builders reported:
---
${reports}
---
Do not trust those reports. Check, with your own runs and new adversarial tests (put them in SimTests/test_foundation_adv.cpp):
- run.ps1 core, state, link, every Stage A key, and all; Unreal/build.ps1 (clean and incremental). Two run.ps1 builds of different keys at the same time must not collide.
- Determinism: same seed -> identical hash; forks independent of draw order elsewhere; no hidden statics or globals.
- Geometry at L = 4 and 6: cell count 10 * 4^L + 2, neighbour symmetry, 12 pentagons, nearest-cell lookup correct near poles and seams, step value.
- Robustness: no NaN paths, empty lists, worlds with no sea or no land, size changes between worlds.
- The macro guard really catches check / verify / PI etc.; the code really compiles with Unreal's settings (no shadowing, no exceptions, no RTTI).
- Contract completeness: every function, entity field, event and payload in SPEC-SIM sections 1-3 has a C++ declaration; ownership of split headers is unambiguous; FOUNDATION-CPP.md is enough on its own for a module engineer. Fix gaps.
- Maps: render elevation and biome maps of a helper world and look at them.
When everything holds, write docs/handoff/done-ue/foundation.txt (one line: date, "foundation", test summary, Unreal build result, your 1-10 quality score). Report the defects you found and fixed.`

if (!SKIP.has('foundation')) {
  const fr = []
  if (!BUILT.has('skeleton')) {
    const r = await run(SKELETON, { ...HAIKU, label: 'build:skeleton', phase: 'Foundation' })
    results.skeleton = r && r.status
    if (!ok(r)) { log('skeleton not done: ' + (r ? r.status : 'agent failed')); return { stopped: 'skeleton', results, report: r && r.report } }
    fr.push('SKELETON: ' + r.report)
  }
  if (!BUILT.has('foundation-core')) {
    const r = await run(CORE, { ...OPUS, label: 'build:foundation-core', phase: 'Foundation' })
    results['foundation-core'] = r && r.status
    if (!ok(r)) { log('foundation-core not done'); return { stopped: 'foundation-core', results, report: r && r.report } }
    fr.push('PART 1: ' + r.report)
  }
  if (!BUILT.has('foundation-api')) {
    const r = await run(API, { ...OPUS, label: 'build:foundation-api', phase: 'Foundation' })
    results['foundation-api'] = r && r.status
    if (!ok(r)) { log('foundation-api not done'); return { stopped: 'foundation-api', results, report: r && r.report } }
    fr.push('PART 2: ' + r.report)
  }
  const v = await run(FVERIFY(fr.join('\n\n') || '(the builders finished in an earlier run; read docs/handoff/done-ue/*.built.txt)'), { ...OPUS, label: 'verify:foundation', phase: 'Foundation' })
  results.foundation = v && v.status
  if (!ok(v)) { log('foundation verification not done'); return { stopped: 'foundation', results, report: v && v.report } }
  log('foundation verified')
}

// ---- Stage A ------------------------------------------------------------------
const BUILD_A = (m) => `${HEAD}

YOUR ASSIGNMENT: ${m.id} "${m.key}". You own: ${m.files} (paths under Unreal/Source/Demiurge/ for Sim/, under Unreal/ otherwise). The JavaScript reference: ${m.js}; read it critically.
${SIMREAD()}
Notes from the lead: ${m.notes}
Build your module to the spec in C++ (replace the neutral initial contents), port any JS tests for it and write the acceptance checks for ${m.id} from docs/BUILD.md as C++ tests, run them with: powershell -File "${ROOT}/Unreal/SimTests/run.ps1" ${m.key}. Render maps of your output and LOOK at them; iterate until it is right, not merely passing. Your tests must pass with every other module stubbed. Keep the stub file(s) you own matching your header.
When everything passes, write docs/handoff/done-ue/${m.key}.built.txt (one line: date, key, test summary line). Do not write it earlier.`

const VERIFY_A = (m, rep) => `${HEAD}

YOUR ASSIGNMENT: independent adversarial verification of ${m.id} "${m.key}" (files: ${m.files}). You did not build it. You may fix it in place (record every fix) and add tests in SimTests/test_${m.key}_adv.cpp.
${SIMREAD()}
The builder reported:
---
${rep || '(built in an earlier run; see docs/handoff/done-ue/' + m.key + '.built.txt)'}
---
Do not trust the report. Re-run their tests; then try to break the module: every rule in its SPEC-SIM section and every acceptance check in BUILD.md for ${m.id}, robustness (NaN, empty lists, dead entities, poles, no sea / no land, every phase, L = 4 and 6), determinism (same inputs -> same hash), event payloads against the event table, ownership (it writes only what the writers table allows), performance at L = 6 against the budget, and does the output LOOK right on maps. Compare against the JS reference where it helps.
When the module holds, write docs/handoff/done-ue/${m.key}.txt (one line: date, key, test summary, your 1-10 quality score). Report what you found, what you fixed and what remains.`

phase('Stage A build')
const todo = A.filter((m) => !SKIP.has(m.key))
await pipeline(
  todo,
  async (m) => {
    if (BUILT.has(m.key)) return { status: 'done', report: '' }
    const r = await run(BUILD_A(m), { ...m.build, label: 'build:' + m.key, phase: 'Stage A build' })
    results[m.key + '.build'] = r ? r.status : 'failed'
    return r
  },
  async (r, m) => {
    if (!ok(r)) { log(m.key + ': build not done, skipping verify'); return null }
    const v = await run(VERIFY_A(m, r.report), { ...m.verify, label: 'verify:' + m.key, phase: 'Stage A verify' })
    results[m.key] = v ? v.status : 'failed'
    return v
  },
)

const verified = A.filter((m) => SKIP.has(m.key) || results[m.key] === 'done').map((m) => m.key)
const missing = A.map((m) => m.key).filter((k) => !verified.includes(k))
if (missing.length) {
  log('Stage A incomplete: ' + missing.join(', '))
  return { stopped: 'stage-a', verified, missing, results }
}

// ---- Integration --------------------------------------------------------------
phase('Integration')
const INTEG = `${HEAD}

YOUR ASSIGNMENT: simulation integration (key "integration"). You own all of Unreal/Source/Demiurge/Sim/* and Unreal/Tools/* and SimTests/test_integration.cpp, and you may fix any module. Make the whole simulation run as one system.
${SIMREAD('  6. docs/BUILD.md "Stage A-int" and SPEC-SIM section 4 (balance targets) closely\n')}
Required: powershell -File "${ROOT}/Unreal/SimTests/run.ps1" all green; simrun for 6 seeds x 4,000 years with the passive policy and 3 seeds each with kind, cruel and chaotic completes with no crash and no NaN; determinism (--hash twice, identical); a mid-run save / load round trip continues identically; mean tick <= 2.5 ms and worst <= 30 ms at L = 6 (release build); the SPEC-SIM section 4 balance targets met, or each gap quantified, by tuning through Balance (and fixing rules that are wrong); owner and biome maps at years 500, 1,500 and 3,000 for one seed, inspected; Unreal/build.ps1 succeeds with the real modules.
When all of that is done, write docs/handoff/done-ue/integration.built.txt (one line: date, "integration", test and simrun summary). Report a per-seed table (year each age was reached, settlements and population at years 1,000 / 2,000 / 3,000, cultures, wars, ending and its year), performance numbers, every cross-module bug fixed, and what remains off-target.`

let ir = null
if (!SKIP.has('integration')) {
  if (!BUILT.has('integration')) {
    ir = await run(INTEG, { ...OPUS, label: 'build:integration', phase: 'Integration' })
    results['integration.build'] = ir ? ir.status : 'failed'
    if (!ok(ir)) return { stopped: 'integration-build', results, report: ir && ir.report }
  }
  const IV = `${HEAD}

YOUR ASSIGNMENT: independent adversarial verification of the simulation integration (key "integration"). You may fix anything under Unreal/Source/Demiurge/Sim/ and Unreal/Tools/; record every fix.
${SIMREAD('  6. docs/BUILD.md "Stage A-int" and SPEC-SIM section 4\n')}
The integrator reported:
---
${(ir && ir.report) || '(integrated in an earlier run; see docs/handoff/done-ue/integration.built.txt)'}
---
Re-run everything they claim with your own commands and different seeds (at least 4 new seeds), and try to break the whole: long runs, save / load at awkward moments (mid-disaster, during war, at an age change), the god powers used aggressively by a scripted policy, determinism, performance spikes, and the story it tells (read the chronicle of one full run end to end: is it a history worth reading?). Run Unreal/build.ps1.
When it holds, write docs/handoff/done-ue/integration.txt (one line: date, "integration", summary, your 1-10 quality score). Report findings, fixes and what remains.`
  const iv = await run(IV, { ...OPUS, label: 'verify:integration', phase: 'Integration' })
  results.integration = iv ? iv.status : 'failed'
  if (!ok(iv)) return { stopped: 'integration-verify', results, report: iv && iv.report }
}

return { finished: true, results }
