# T-022 — Concept art: the decision cards

**Assignee:** Grok · **Status:** open (after T-020) · **Branch:** `helper/grok/T-022`
**Deliver to:** `docs/concept/cards/` only.

## Why
The god now meets the people through decision cards (`docs/SPEC-MYTHOS.md` sections 2.1 to 2.4): a culture
asks for a pact, two prayers collide, a ruler is to be judged, a disaster is foreseen. Each card needs an
illustration that tells the player at a glance what is at stake, in the game's look.

## Read first
`AGENTS.md`; `docs/SPEC-MYTHOS.md` section 0 (principles and the neutral-naming rules in 0.2, binding)
and sections 2.1 to 2.4 (the card catalogue table lists every kind); `docs/concept/ui/index.md` (the HUD and
panel style the cards sit in); `docs/concept/emblems/index.md` (your culture emblems, usable on banners in the
art); `docs/content/cultures.md` (the eight families, their palettes and architecture);
`docs/concept/settlements/index.md`.

## Deliver
1. One illustration per card kind in the version-1 and version-2 rows of the catalogue (about 16):
   1600×1000 JPG, `card-<kind-in-lowercase-hyphens>.jpg`, e.g. `card-competing-prayers.jpg`. Show people, places
   and signs, never the god as a figure (the god is unseen; light, weather and signs are fine).
2. `cards-sheet.png`: all of them as they would appear in a card frame at panel size (about 360 px wide),
   using the UI concept's frame, type and colours, so readability at game size can be judged.
3. `index.md`: per card, what it shows and why, and which culture families it was drawn for. Note the
   generator and prompt for every image (provenance), and the licence (your own output).

## Acceptance criteria
- Readable at 360 px: the stakes are clear without the text.
- Mythology-neutral per SPEC-MYTHOS 0.2: no real-religion symbols, clergy dress or scripture imagery.
- Cinematic stylised realism matching the approved concepts; no imitation of any named game or artist.
- No playable mortal avatar or "the god as a person" imagery.
