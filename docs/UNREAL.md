# DEMIURGE — move to Unreal Engine 5.7 (orchestrator plan)

On 2026-11-19 the owner moved DEMIURGE from the zero-dependency WebGL/JavaScript build to
**Unreal Engine 5.7 (C++)**, for a cinematic, high-quality look. What changes and what stays:

- **The design stays.** `CONCEPT.md` (the game) and `SPEC-SIM.md` (every simulation rule, entity,
  event, writer, tick schedule and balance target) remain the behavioural contract. Ignore the
  parts of CONCEPT that only describe the old tech (low-poly look, `index.html`, "no assets",
  WebGL2 renderer, `DM` namespace).
- **The technology changes.** The simulation is ported to **plain, engine-independent C++20**
  inside the Unreal project, so it compiles both in Unreal and in a fast standalone test build.
  `FOUNDATION.md` is replaced by `FOUNDATION-CPP.md` (written by the foundation engineer).
  `SPEC-RENDER.md` and `SPEC-UI.md` describe the old WebGL client: they are reference for
  *what the player sees and does*, not for how; the Unreal client is specified in
  `SPEC-UNREAL.md` (written by the orchestrator later).
- **Owner decision (2026-11-19), overrides any brief that says otherwise:** "take what's done and
  use that, and start over on the rest that was partially finished." Only **terrain, climate and
  life** in `js/sim/` were finished (they passed their own tests): port those, judging them
  against the spec. For **every other module** (names, hydro, path, units, civ, sim and all the
  placeholders) do **not** port or patch the old JavaScript: build fresh from `SPEC-SIM.md`. You
  may glance at the old file only to understand an idea, never as a base. The JS foundation
  (`js/core/*`, `state.js`, `schema.js`, test helpers) was written by the lead and is complete:
  the foundation engineers port it.

## Layout

```
demiurge/Unreal/
  Demiurge.uproject
  Source/Demiurge.Target.cs, Source/DemiurgeEditor.Target.cs
  Source/Demiurge/Demiurge.Build.cs          (bUseUnity = false)
  Source/Demiurge/Sim/...                     pure C++20 simulation: std only, no Unreal headers
  Source/Demiurge/Game/...                    Unreal-side code (bridge, rendering, camera, UI)
  SimTests/                                   standalone test harness built with MSVC cl.exe
    run.ps1 <module>                          builds and runs one module's tests
    stubs/<Module>_stub.cpp                   neutral stand-ins for every sim module
    test_<module>.cpp
    build/<module>/                           per-module build output (ignored)
  Tools/                                      simrun, mapimg (PNG maps), etc.
  build.ps1                                   runs Unreal's Build.bat for the editor target
```

**PowerShell scripts:** this PC blocks running .ps1 files by default (execution policy). Always
invoke them as `powershell -NoProfile -ExecutionPolicy Bypass -File "<script>" <args>`. Never
change the machine's execution policy.

## Build process

Workflow script `docs/handoff/build-unreal.js`. Stages: foundation (core + state + schema +
module headers and stubs + harness, plus the Unreal skeleton) → Stage A (14 sim modules, each
built and then independently verified) → simulation integration. At most 6 agents at a time.
Opus (effort high) does the complex work, Haiku (effort xhigh) the mechanical work. Markers in
`docs/handoff/done-ue/`: `<key>.built.txt` after a build, `<key>.txt` after verification.

Resume: `Workflow({ scriptPath: "<root>/docs/handoff/build-unreal.js", args: { skip: [verified keys], built: [built-only keys], limit: 6 } })`.

After the simulation: the Unreal client (bridge, planet rendering, sky, camera and input,
objects and towns, effects, HUD and panels, audio, save), specified in `SPEC-UNREAL.md`.

## Client track

`docs/SPEC-UNREAL.md` specifies the Unreal client (bridge, planet, sky, camera/input, nature,
towns, fx, HUD, screens, audio, then client integration). Workflow script
`docs/handoff/build-client.js` (bridge first, then nine modules in private lanes, each built
then verified); markers `client-<key>.built.txt` / `client-<key>.txt`. It starts once the sim
foundation is verified (`done-ue/foundation.txt`), in parallel with sim Stage A. Resume:
`Workflow({ scriptPath: "<root>/docs/handoff/build-client.js", args: { skip: [...], built: [...], limit: N } })`.
The graphics reference is the orchestrator's lab in `UnrealLab/PlanetLab` (shots `shots/ue-lab-*.png`).
