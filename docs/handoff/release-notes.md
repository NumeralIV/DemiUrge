
## Owner playtest 1 (2026-11-19 18:25, packaged Development build, machine loaded by ~25 build agents) — "I couldn't even play it"

Evidence: `dist/DEMIURGE-Win64-Development/Windows/Demiurge/Saved/Logs/Demiurge.log` (3 minutes). **Fix these before anything cosmetic; the final whole-game check must replay this path.**
1. **New world skipped genesis.** Title → play created a world with `sim::create(..., skip)`: phase history, year 0, one camp, nothing happening. The player never saw molten / comets / life / awakening, which are the tutorial. New World must start genesis unless the player explicitly picks a quick start.
2. **Nothing told the player what to do.** 25 clicks on empty ground in 90 seconds all logged `select -1`; nine double-clicks in a row zoomed in. No power was armed and no hint appeared. The first minutes need an explicit, unmissable prompt ("Choose a power below, then click the land") and clicking land with nothing armed should say what clicking does.
3. **Frame rate collapsed near the ground:** 6–16 fps at 2–5 km altitude (35–87 fps at orbit) on a machine also running agents. Even on an idle machine close views were 54–59 fps. Near-ground cost must come down with real headroom (target 60 fps at 1080p High, and a Medium preset that holds 60 with the CPU half busy).
4. **The HUD ignored the shipped fonts:** the log loads the OFL families (`DmFonts`) and then reports `DmHud: up (fonts: serif Palatino Linotype, sans Segoe UI)`.
5. Minor: transient-texture overwrite hitches on title → play (`Gamethread hitch waiting for resource cleanup` on DmPlanet* textures).
