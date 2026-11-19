# T-003 — Asset sourcing research: close-up 3D assets and licences

**Assignee:** Astra · **Status:** open · **Branch:** `helper/astra/T-003`
**Deliver to:** `docs/research/assets.md` only.

## Why
Buildings, trees, units and props will be replaced with real 3D assets. The lead needs to know
where to get them so they fit the look, run well in Unreal 5.7, and can legally ship in a
commercial game whose owner wants as few restrictions as possible.

## Read first
`AGENTS.md`; `docs/CONCEPT.md`; `docs/SPEC-UNREAL.md` sections 0.4 and 2 (C4 nature, C5 towns);
`docs/lab/vegetation-and-towns.md`.

## Deliver `docs/research/assets.md`
1. A table of candidate sources (for example Fab / Quixel Megascans, Poly Haven, Kenney,
   ambientCG, Sketchfab CC0 / CC-BY, others you find) with columns: source; what it has that we
   need (trees by biome, buildings by age, ships, props); visual quality and fit with the look;
   price; licence (exact name and link); whether a shipped game may include it; attribution
   requirements; restrictions (engine-only, no raw redistribution, AI clauses); Unreal 5.7 and
   Nanite readiness.
2. A recommended shortlist per category (trees, buildings by age, ships and vehicles, props) with
   reasons, and a total-cost estimate.
3. A "make it ourselves" alternative: what a Blender-based pipeline would take (the lead can
   drive Blender through a connector) and where it beats buying.
4. Licence pitfalls to avoid, in plain language.

## Acceptance criteria
Every licence claim links to the licence text and notes the date checked; nothing downloaded is
committed to the repository.
