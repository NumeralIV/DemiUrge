# DEMIURGE

*A world in your hands.*

An in-development god game on a small, living 3D planet. Shape continents, seed life, awaken civilizations, and become part of the history they tell about their world.

## Project status

This repository preserves the existing local project as of November 18, 2026. It is an unfinished development snapshot, not a playable release.

The foundation, environmental simulation, naming, settlement lifecycle, units, simulation orchestrator, and renderer core contain implementations. Many other modules are explicitly marked `PLACEHOLDER`, including economy, society, powers, the chronicle, production scene rendering, player controls, interface, audio, and saving.

The concept remains of interest to the project owner; the current graphics are **not an approved visual direction**. Render-viewer screenshots include stand-in test scenes and should not be presented as final game art.

## Layout

- `index.html` — game entry point; incomplete dependencies currently prevent normal play.
- `js/` — core utilities, simulation, rendering, UI, audio, and game shell.
- `css/` — base styles and placeholder interface styles.
- `docs/` — concept, specifications, build plan, tools documentation, and historical handoff material.
- `dev/` — development viewer and game test page.
- `tests/` — Node tests and a browser rendering scenario.
- `tools/` — test runner, browser harness, map rendering, and development tools.
- `shots/` — existing screenshots, diagnostic maps, and visual experiments, preserved in full.

## Validation

The project uses plain JavaScript without an npm install or build step. Existing development tooling targets Node.js 24 and Microsoft Edge on Windows.

From the repository root:

```sh
node tools/test.js
```

The source project was checked on November 18, 2026: **382 passed, 7 failed across 13 test files**. The failures were an API check for absent `Civ.yearly` and `Civ.capacity`, one hydrology test, and five unit tests. This records a baseline, not a passing release gate.

A separate headless check at seed 7, subdivision level 4, entered history and advanced ten years with zero settlements and cultures: the society and awakening dependencies remain unimplemented.

The default API test tolerates entirely absent namespaces; a green subset of tests is not proof of a complete game. Browser validation is separate from the Node suite.

## Working with the handoff

Start with `docs/CONCEPT.md`, then the foundation and relevant specifications. Historical build workflows in `docs/handoff/` reference the original local path and an external agent runtime; they are not portable standalone Node build commands. `.built.txt` files record builder checkpoints, not independent verification.

The original source files were copied unchanged for this migration. Coordinate ownership before concurrent work, use separate branches for implementation, and review integration before merging into `main`.
