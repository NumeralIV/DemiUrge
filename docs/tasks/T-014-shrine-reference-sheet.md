# T-014 — Model reference sheet: the village shrine (first real asset)

**Assignee:** Grok · **Status:** open · **Branch:** `helper/grok/T-014`
**Deliver to:** `docs/concept/assets/shrine/` only.

## Why
The real-asset pass starts with one test asset, a stone-and-timber village shrine (see
`docs/research/assets.md`, "Proposed first experiment"). A modeller (Blender) or an image-to-3D
service needs a clean, consistent reference: views from several sides, proportions and materials.

## Read first
`AGENTS.md`; `docs/research/assets.md` (section 1, the shrine experiment); your
`docs/concept/landmarks/building-shrine.png` and `docs/concept/settlements/age-1-bronze.png`
(the look to match); `docs/SPEC-SIM.md` section 1 (`BUILDINGS`).

## Deliver
1. `shrine-hero.png`: the shrine in its village setting, golden hour, three-quarter view from
   about 40 m (the art target).
2. `shrine-front.png`, `shrine-side.png`, `shrine-back.png`, `shrine-top.png`: orthographic-style
   views on a plain neutral background, flat even light, no perspective distortion, the same
   design in every view (suitable for image-to-3D and for modelling).
3. `shrine-materials.png`: close swatches of each material (stone, timber, thatch or tile, cloth,
   carved details) with names.
4. Three variants as small images: `shrine-culture-a.png`, `-b.png`, `-c.png` matching the three
   culture styles in `docs/concept/settlements/`, keeping the same footprint.
5. `index.md`: prompts; real-world dimensions in metres (footprint, wall height, roof pitch,
   doorway), a part list (base, walls, posts, roof, ornaments) for a modeller, material notes with
   hex colours, and what must stay identical between the culture variants.

## Acceptance criteria
The four plain views show one consistent design; dimensions given; readable silhouette; matches
the approved Bronze-age look; no text or watermarks in the images.
