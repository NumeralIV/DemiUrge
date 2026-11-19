# T-001 — Concept art: settlements through the ages

**Assignee:** Grok · **Status:** open · **Branch:** `helper/grok/T-001`
**Deliver to:** `docs/concept/settlements/` only.

## Why
Close-up buildings in the game are stand-ins. Before real 3D assets are modelled or bought, the
owner and the lead need a clear visual target so that work is done once. These images are that target.

## Read first
`AGENTS.md`; `docs/CONCEPT.md` (ages: Stone, Bronze, Iron, Sail, Industry, Atomic, Space;
settlement tiers: camp, village, town, city, metropolis); `docs/SPEC-SIM.md` section 1
(settlements, cultures, `BUILDINGS`). Look at `shots/ue-cloudlab-terminator-final.png` (the
approved orbit look) and `shots/ue-lifelab-f-village-golden.png` (the current stand-in village,
which is NOT the target).

## Deliver
1. One image per age, 7 in all: a typical settlement of that age seen from a high oblique angle
   (as the game camera sees it, roughly 300 m to 1 km away), golden-hour light.
   Files `age-0-stone.png` … `age-6-space.png`.
2. Three distinct invented culture styles (not copies of real-world or fictional cultures), each
   shown as one town of the Iron or Sail age: `culture-a.png`, `culture-b.png`, `culture-c.png`.
3. A night version of one city (lit windows, the way city lights must read from above): `city-night.png`.
4. `index.md`: for every image, the file name, what it shows, the exact prompt used, and three or
   four bullet points on the key shapes, materials and colours a 3D artist should take from it.

## Acceptance criteria
- Cinematic, near-photoreal or high-end painterly realism; not cartoon, not low-poly.
- Readable from above: roofs, streets, walls and fields read at game-camera distance.
- The age progression is obvious when the 7 images sit side by side.
- No text or watermarks; no imitation of existing games or named artists.
