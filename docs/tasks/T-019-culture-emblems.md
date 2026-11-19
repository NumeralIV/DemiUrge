# T-019 — Culture emblems and banners

**Assignee:** Grok · **Status:** open · **Branch:** `helper/grok/T-019`
**Deliver to:** `docs/concept/emblems/` only.

## Why
Each people needs a visual identity beyond a colour: the HUD's culture markers, map labels, unit
pennants and the chronicle book all show it. The HUD currently uses a letter in a circle.

## Read first
`AGENTS.md`; `docs/SPEC-SIM.md` section 1 (cultures, `CULTURE_COLORS`); `docs/concept/ui/index.md`
(icon line style, colours, fonts); your three culture styles in `docs/concept/settlements/`;
`docs/content/cultures.md` if Astra's T-004 has been merged by then (otherwise invent freely).

## Deliver
1. `emblems-sheet.png`: 16 invented emblems drawn in the same line style as the UI icons, each in
   a circular badge, each usable in any of the culture colours. Simple enough to read at 16 px.
2. The emblems as individual SVG files (`emblem-01.svg` … `emblem-16.svg`): 24×24 viewBox, single
   stroke colour `currentColor`, 1.6 px stroke, round joins, no text, no fills except where noted.
3. `banners.png`: how an emblem appears on a banner, a pennant, a shield and a building flag,
   at game scale and close up.
4. `index.md`: per emblem, a name and a one-line meaning (in the chronicle's voice), and which
   culture styles it suits.

## Acceptance criteria
Original (no real-world flags, religious symbols or existing game insignia); readable at 16 px;
consistent with the UI icon set; SVGs valid and minimal.
