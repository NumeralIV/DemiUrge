# T-015 — Shrine reference: construction, damage and ruin states

**Assignee:** Grok · **Status:** open · **Branch:** `helper/grok/T-015`
**Deliver to:** `docs/concept/assets/shrine/states/` only.

## Why
Every building in the game appears while being built, after damage (war, quake, fire) and as a
ruin (drowned, abandoned, after the silence ending). The asset research (`docs/research/assets.md`,
step 5 of the Blender workflow) calls for these states on the same footprint. Your shrine
(T-014) is the test case.

## Read first
`AGENTS.md`; your `docs/concept/assets/shrine/index.md` and images; `docs/research/assets.md`
section 4; `docs/SPEC-SIM.md` section 3.10 (disasters).

## Deliver (same plain-background front three-quarter view and lighting as T-014)
1. `shrine-construction.png`: plinth and posts up, walls half laid, scaffold, roof frame without thatch.
2. `shrine-damaged.png`: after a quake: cracked walls, slipped thatch, a fallen post, rubble.
3. `shrine-burnt.png`: after a fire: charred posts, roof gone, soot on stone.
4. `shrine-ruin.png`: centuries later: roofless, walls broken down, grass and a small tree growing in it.
5. `shrine-drowned.png`: half under shallow water with weed.
6. `index.md`: per state, what changes part by part against the T-014 part list, so a modeller can
   build each state by editing the base model.

## Acceptance criteria
Same footprint, plinth and proportions as T-014 in every state; readable at game distance; no text
or watermarks.
