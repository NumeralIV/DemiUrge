# DEMIURGE

*A world in your hands.*

A god game on one small, living planet. You are a newborn god handed a ball of molten rock: cool
it, call down comets to fill its seas, seed life, awaken a people, and shape the world they live
in while they climb from the Stone Age to the stars, writing down everything you do. Full pitch:
[`docs/CONCEPT.md`](docs/CONCEPT.md).

**AI assistants: read [`AGENTS.md`](AGENTS.md) first.** It defines the hierarchy (owner →
Claude as project lead → helpers) and exactly how helpers take tasks and deliver work.

## Status (2026-11-19)

The project moved from a WebGL / JavaScript prototype to **Unreal Engine 5.7 (C++)** for a
cinematic, high-quality look. See [`docs/UNREAL.md`](docs/UNREAL.md).

- **Simulation** (engine-independent C++20, `Unreal/Source/Demiurge/Sim/`): foundation built and
  independently verified; 12 of 15 modules built, now being verified; chronicle, sim core and
  save in progress. Then whole-simulation integration and balance.
- **Unreal client** (`Unreal/Source/Demiurge/Game/`): spec written
  ([`docs/SPEC-UNREAL.md`](docs/SPEC-UNREAL.md)); bridge in progress, then planet, sky,
  camera / input, nature, towns, effects, HUD, screens and audio.
- **Graphics research** (`UnrealLab/`): orbit view with weather-driven clouds, descent to the
  surface, generated vegetation and towns. Write-ups in [`docs/lab/`](docs/lab/).
  Current best frame: `shots/ue-cloudlab-terminator-final.png`.

Close-up buildings, trees and props are stand-ins; real assets come later (see `docs/tasks/`).

## Layout

| Path | What |
|---|---|
| `AGENTS.md` | Rules for every AI assistant working in this repository |
| `docs/` | Concept, specs (`SPEC-SIM`, `SPEC-UNREAL`, `FOUNDATION-CPP`, …), plans, lab write-ups |
| `docs/tasks/` | The task board for helpers |
| `Unreal/` | The Unreal 5.7 project: `Source/Demiurge/Sim` (simulation), `Source/Demiurge/Game` (client), `SimTests` (standalone tests), `Tools` (simrun, map images) |
| `UnrealLab/` | Graphics prototypes (separate small Unreal projects) and the exported test planet |
| `js/`, `css/`, `dev/`, `tests/`, `tools/`, `index.html` | The earlier JavaScript prototype, kept as reference |
| `shots/` | Screenshots (the Unreal ones are curated finals only) |

## Building (owner's machine)

Unreal Engine 5.7 and Visual Studio 2022 (C++ game development workload). Simulation tests:
`powershell -NoProfile -ExecutionPolicy Bypass -File Unreal/SimTests/run.ps1 all`.
Unreal build: `powershell -NoProfile -ExecutionPolicy Bypass -File Unreal/build.ps1`.
