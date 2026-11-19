# T-013 — Concept art: the planet's changing states from orbit

**Assignee:** Grok · **Status:** open · **Branch:** `helper/grok/T-013`
**Deliver to:** `docs/concept/world-states/` only.

## Why
Over a history the planet itself changes: industry warms it, seas rise, wars and volcanoes darken
the sky, ice advances or retreats, forests are cleared. The planet and sky modules render these
states from simulation fields; they need a visual target for each.

## Read first
`AGENTS.md`; `docs/CONCEPT.md` (pillar 2); `docs/SPEC-SIM.md` sections 3.3 (climate: CO2,
anomaly, dust, sea level, ice) and 3.10 (disasters); `shots/ue-cloudlab-terminator-final.png`
and your `docs/concept/genesis/` (use the same planet and framing).

## Deliver (whole planet from orbit, same planet and framing as T-010)
1. `state-healthy.png`: a lush, balanced world at its peak of life (reference for the others).
2. `state-industrial.png`: industry age, brown haze over cities, cleared forests, lights spreading.
3. `state-warming.png`: hot world, shrunken ice caps, higher seas eating the coasts, more storms.
4. `state-ice-age.png`: ice caps reaching far south, grey seas, sparse life.
5. `state-nuclear-winter.png`: a dust-darkened sky, dim grey world, few lights.
6. `state-volcanic.png`: a large eruption with an ash plume spreading over a hemisphere.
7. `state-wasteland.png`: after collapse (the "silence" ending), greenery returning over ruins.
8. `index.md`: per image, what it shows, the prompt, and per state how each visible field should
   change (water colour, ice extent, haze colour and density, vegetation colour, night lights) as
   hex values and rough percentages, for the engineers.

## Acceptance criteria
Same planet throughout; each state instantly distinguishable from orbit; cinematic and
near-photoreal; no text or watermarks.
