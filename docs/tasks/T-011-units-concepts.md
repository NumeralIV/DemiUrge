# T-011 — Concept art: units and wildlife on the move

**Assignee:** Grok · **Status:** open · **Branch:** `helper/grok/T-011`
**Deliver to:** `docs/concept/units/` only.

## Why
Settlers, traders, armies, prophets, ships and herds move across the planet and are what make it
feel alive between your settlements. They are small on screen, so their silhouettes and colours
must read at the game camera's distance.

## Read first
`AGENTS.md`; `docs/CONCEPT.md`; `docs/SPEC-SIM.md` section 1 (units) and 3.9 (Units);
`docs/SPEC-RENDER.md` section 9; your `docs/concept/settlements/` work (match the ages and the
three culture styles).

## Deliver (high oblique view from about 300 m to 2 km, golden hour, like T-001)
1. Units across the ages, one image per age band: Stone and Bronze (`units-ancient.png`: settler
   band, traders with pack animals, warband, reed boats); Iron and Sail (`units-classical.png`:
   caravans on a road, a marching army with banners, galleys and sailing ships, a prophet with
   followers); Industry and Atomic (`units-modern.png`: trains, steamships and freighters, columns
   of trucks, aircraft); Space (`units-space.png`: launch traffic, transports, the ark under
   construction).
2. Wildlife: `wildlife-herds.png` (a herd crossing savanna), `wildlife-birds.png` (a flock over a
   wetland), `wildlife-sea.png` (whales or a fish shoal seen from above in shallows).
3. A readability sheet `units-silhouettes.png`: each unit type as a small silhouette at actual game
   scale (about 20–60 px tall), with its culture colour, so the engineers can check what reads.
4. `index.md`: per image, what it shows, the prompt, and notes on silhouette, colour (hex) and
   scale for the 3D stand-ins and the later real assets.

## Acceptance criteria
Silhouettes distinguishable at game scale; the age progression is obvious; matches the earlier
culture styles; cinematic and near-photoreal; no text or watermarks.
