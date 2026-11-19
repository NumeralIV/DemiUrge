# T-007 — Concept art: HUD, panels and screens

**Assignee:** Grok · **Status:** open · **Branch:** `helper/grok/T-007`
**Deliver to:** `docs/concept/ui/` only.

## Why
The in-game interface is being built now in Unreal (Slate, C++). A visual target lets the lead
judge and refine it once, instead of iterating blind.

## Read first
`AGENTS.md`; `docs/CONCEPT.md` (tone: quiet gold on ink, serif type, the planet is the star);
`docs/SPEC-UI.md` sections 3 (icons), 4 (HUD), 4b (panels) and 5 (screens); your own
`docs/concept/` work and `shots/ue-cloudlab-terminator-final.png` (the planet the UI sits over).

## Deliver (1920x1080 mockups, the planet visible behind the UI)
1. `hud-orbit.png`: the full HUD over the orbit view (power palette, Awe, time controls, age and
   year, chronicle ticker, overlay switch).
2. `hud-power-armed.png`: a power armed with the brush ring on the planet and a tooltip.
3. `panel-settlement.png` and `panel-culture.png`: the selection panels.
4. `chronicle-book.png`: the open chronicle book with a few entries (use lines from
   `docs/content/chronicle-voice.md`).
5. `screen-title.png`, `screen-genesis.png` (one genesis step prompt), `screen-ending.png`, and
   `planet-card.png` (the shareable end card).
6. `icons-sheet.png`: the icon set named in SPEC-UI section 3 (powers, ages, buildings, statuses),
   consistent line style.
7. `index.md`: per image, what it shows, the prompt, and notes on layout (positions, sizes in px at
   1080p), colours (hex) and type (suggest open-licence serif fonts by name) for the engineer.

## Acceptance criteria
Restrained and elegant; the planet stays dominant; every element named in SPEC-UI 4 appears in
`hud-orbit.png`; text in mockups is legible and in English; no imitation of existing games' UIs.
