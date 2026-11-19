# T-008 — Concept art: landscapes and biomes on the way down

**Assignee:** Grok · **Status:** open · **Branch:** `helper/grok/T-008`
**Deliver to:** `docs/concept/nature/` only.

## Why
The camera descends from orbit to a few hundred metres above the ground. The orbit look is
approved; the lower altitudes need the same clear target before trees, ground and water are
finalised.

## Read first
`AGENTS.md`; `docs/CONCEPT.md`; `docs/lab/surface.md` and `docs/lab/vegetation-and-towns.md`
(what the engine already does); `shots/ue-cloudlab-terminator-final.png` (approved orbit look).

## Deliver
1. One descent series for a single place (a forested river valley meeting the sea), at five
   altitudes: 400 km (whole region), 50 km, 10 km, 2 km and 300 m. Same light, same place, so the
   series shows how detail should appear as the camera comes down: `descent-1-400km.png` …
   `descent-5-300m.png`.
2. One image per biome at about 1–2 km altitude, golden hour: temperate broadleaf forest, boreal
   conifer forest, rainforest, savanna, desert with dunes, tundra, alpine mountains with snow and
   glaciers, wetland delta, coral coast with shallows: `biome-<name>.png`.
3. `index.md`: per image, what it shows, the prompt, and notes for the engineer on colour (hex),
   texture scale, canopy density and how water, shoreline and snow should read at that altitude.

## Acceptance criteria
Cinematic, near-photoreal, believable as one planet with the approved orbit image; no text or
watermarks; the descent series is visibly the same place at every altitude.
