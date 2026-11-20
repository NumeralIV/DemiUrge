# Style pilot: a more game-like look for close-up assets

The owner wrote: "flow renders are extremely realistic.. which is cool, but I do want a bit more game like".

**Brief.** Stylised realism for close-up assets: silhouettes that read from the strategy camera, clean and
simplified surfaces, a controlled palette slightly richer than real life, a gentle hand-finished feel, still
physically lit. Not low-poly, not cartoon, not photo-scan grit. The approved orbit look
(`shots/ue-cloudlab-terminator-final.png`) does not change.

**Status (2026-11-20, third run): complete.** All five looks are rendered in all three views, with and without
the strategy grade, and the swatch sheet shows every material in every look. S1 and S2 come from 16 Flow
images, one per material and prompt (variant "a"; no "b" was generated), each with an exact-prompt provenance
record. The recommendation is at the end of "Verdict".

## Look at these

| Sheet | What it shows |
|---|---|
| `shots/style-s300.png` | strategy camera, 300 m oblique (inset: the village at 2x) |
| `shots/style-s80.png` | strategy camera, 80 m oblique |
| `shots/style-c15.png` | close, 15 m: the shrine front with a house behind |
| `shots/style-c15-crop.png` | shrine wall crop from a 1920 x 1200 render, all five looks, as rendered and graded |
| `shots/style-swatches.png` | the processed diffuse of every material in every look, tiled 2 x 2 |

Each view sheet has five columns: CURRENT, PHOTOREAL, S1, S2, P. The top row is as rendered. The bottom row
has the **strategy grade**: wide-radius clarity, saturation +14 %, a lifted and cleaned shadow toe, and a
slightly eased highlight shoulder (`grade()` in `compose_style.py`).

**Scene.** The intact shrine (LOD0) sits at the centre of 8 Age-1 houses on a ring. There is an earth plaza,
a ring lane, door spurs, a road out, grass and broadleaf trees. The houses use variants: plaster walls on 3
houses and terracotta roofs on 2 (the kit has no tile roof, so CURRENT keeps thatch there). Light is golden
hour (sun at 16° elevation), rendered in Cycles with AgX Medium High Contrast. Geometry, light and camera are
identical across looks; only the textures change. Every Flow-based set is calibrated to the same mean
brightness as the CURRENT set for that material, so the comparison is about texture style, not exposure.

## The options

| Look | What it is |
|---|---|
| **CURRENT** | the shipped procedural sets |
| **PHOTOREAL** | the Flow library as generated (`art/textures-flow/`, "a" variants) |
| **S1** | Flow, hand-painted prompt |
| **S2** | Flow, stylised-realism prompt |
| **P** | PHOTOREAL processed down locally (smoothed, saturated, contrast reduced) |

## Verdict

**CURRENT.** Flat and beige. At 80 and 300 m the walls, roofs and ground are one brown, and up close the stone
is a printed pattern with no depth. It loses on every view. It would mean keeping a game that looks unfinished.

**PHOTOREAL.** Good at the strategy camera, because the palette (red tiles, white plaster, green grass) does the
work. Up close it is what the owner already said: impressive but photographic. The rubble is gritty, the
thatch is grey straw and the ground is gravel. Choosing it means a realistic game, with units and props that
must also be photoreal to match, and constant work hiding tiling on large grounds.

**S1 (hand-painted).** The most readable look. At 80 m every material names itself: golden thatch, red brick,
terracotta, a clear ring lane. Up close the stones are clean shapes with crisp dark joints, like a premium
board game or a mobile strategy title. The cost is that the texture paints its own light: every stone has a
highlight on its top edge even on the wall facing away from the sun, its grass stays busier than S2's at
80 m even after calming, and with the grade it tips toward cartoon. It would pull the whole game away from the cinematic orbit view, and units, props and terrain would
all need the same painted finish, which is the most art time.

**S2 (stylised realism).** The best match to the brief. It reads as well as S1 from the strategy camera, because
it has the same distinct palette. Up close the rubble has larger, simpler stones with crisp joints and some
grey, moss and ochre variation, the thatch reads as bundled reed, and the light on it still looks physical. It
looks designed, not scanned, and still sits next to the realistic planet. The weak spots were in the source
images, not the look: the path image has a large X across the square and had to be reduced to fine detail
(regenerate it), the planks came out horizontal and were turned for the posts, and the grass needed its
contrast calmed so it does not become a carpet pattern at 80 m. For the rest of the game it means clean
models with soft normals, palettes a step richer than reality, and one Flow prompt family for every age.

**P (photoreal processed down).** A small gain over PHOTOREAL, no new art needed. Filtering cannot invent clean
shapes: the rubble turns blotchy and the joints smear (`style-c15-crop.png`). It is only useful as a stopgap
on soft materials (path, grass, plaster) until those have an S2 version.

**Two things hold for every look.** At 300 m, palette, value contrast and light matter more than texture
style; PHOTOREAL, P, S1 and S2 differ mainly in colour there. And the strategy grade (bottom rows) improves
every column at no cost.

### Recommended

**S2, with the strategy grade.** Use the S2 prompt for buildings, props and ground in every age. Put the grade
in the game's post-process now. Before production:

- Regenerate the S2 path with "no large shapes, no paths or patterns across the square" added, and generate a
  "b" for every material so each has a choice.
- Ask for "vertical boards" in the planks prompt, and "even, low-contrast tufts" in the grass prompt, instead
  of fixing those afterwards.
- Keep the ground earth-brown, not brick-red: the stylised paths came out brick-red and fought the roofs and
  walls, so here their colour is taken from the shipped soil (see "Processing" below).
- Keep S1 in reserve only if the owner wants a bolder, board-game look and accepts repainting units and
  terrain to match.

## What each look would mean

| | Terrain | Units | Towns |
|---|---|---|---|
| **PHOTOREAL** | gritty ground; needs heavy macro variation to hide tiling | photoreal units clash with a simplified UI | busy at 80 m; roofs read, walls are noise |
| **S2** | larger, calmer grass and earth patterns; tile at 2-3 m and macro-tint | units modelled with clean forms and soft normals to match; skin and cloth slightly saturated | each age keeps a signature palette; walls and roofs separate at a glance |
| **S1** | painted ground, strong readability, risks clashing with the realistic orbit view | units need a matching painted finish; more art time | very readable; the most "board game" feel |
| **P** | cheap now, from existing library images; soft materials only | no change | a modest gain; hard materials still need regeneration |

All looks keep make_maps.py processing, PBR, Lumen light and the orbit planet as they are. Stylised sets get
softer normals: 0.6 x tilt and a 2.5 px pre-blur for S1/S2, 0.65 x and 2.5 px for P.

## Exact Flow prompts (S1, S2)

Settings: Image, 1:1, Nano Banana 2.1, x2, 0 credits. Download the "2K upscaled" item. Two images per prompt.
Materials: `rubble stone wall`, `reed thatch`, `mudbrick`, `whitewashed plaster`, `terracotta roof tiles`,
`timber planks`, `packed earth path`, `lush grass`.

**S1:** `Seamless tileable stylized game texture of <material>, hand-painted look, clean readable shapes,
simplified detail, soft painted highlights on the top edges, gentle dark accents in the gaps, warm natural
colours slightly richer than real life, flat even lighting, no shadows, no perspective, fills the whole square
edge to edge, no text`

**S2:** `Seamless tileable stylized game texture of <material>, stylized realism, crisp clean surfaces, larger
simpler shapes, subtle colour variation, slightly saturated, painterly finish, clean readable shapes, simplified
detail, soft painted highlights on the top edges, gentle dark accents in the gaps, warm natural colours
slightly richer than real life, flat even lighting, no shadows, no perspective, fills the whole square edge to
edge, no text`

## Processing (how S1 and S2 were made, and how to redo them)

1. **Flow.** 16 images (2 prompts x 8 materials, variant "a" only), Image, 1:1, Nano Banana 2.1, 2K upscaled,
   generated 2026-11-20 between 02:44 and 03:01 local time, ingested with
   `python tools/flow_provenance.py ingest "<download>" art/style-pilot/flow/<S1|S2>/<mat>-a.jpg --prompt="<exact prompt>"`.
2. **Tile sizes** (`STYL` in `style_maps.py`), from counting stones, courses, boards and tiles per square and
   erring slightly large, because stylised means bigger readable shapes:

   | m per tile | rubble | thatch | mudbrick | plaster | terracotta | planks | path | grass |
   |---|---|---|---|---|---|---|---|---|
   | S1 | 2.6 | 1.6 | 1.8 | 3.0 | 2.2 | 1.4 | 3.0 | 2.0 |
   | S2 | 2.6 | 1.4 | 1.4 | 3.0 | 2.4 | 1.6 | 4.0 | 1.6 |

3. **Fixes before make_maps** (`PREFLAT` in `style_maps.py`; the fixed source goes to `prep/<look>-<mat>.png`):
   S1 terracotta had a light diagonal streak that repeated per tile (flatten at 1/14 of the image); S2 path had
   an X across the whole square that tiled into a diamond lattice, which a flatten turned into a star lattice,
   so it keeps only detail finer than 1/40 of the image; S1 and S2 grass had their tuft contrast reduced to
   0.55 (S1 toward the mean colour, because its dark gaps are blue-green and turned teal otherwise); S1 and S2
   planks were turned 90 degrees to vertical boards. The paths take the shipped soil's hue
   (`HUE_FROM_CURRENT`).
4. **make_maps.py** heals the seams, de-lights, calibrates every set to the CURRENT set's mean brightness and
   writes D, N, R, AO and ORM. Stylised sets get softer normals (0.6 x tilt, 2.5 px pre-blur). Every wrap seam
   ended below the set's own joint lines (`seam_after` in `maps/<LOOK>/<Mat>.json`).
5. Commands, each through `python tools/render_slot.py --`:
   `blender -b --factory-startup --python art/style-pilot/style_maps.py -- --looks=S1,S2`, then
   `blender -b art/style-pilot/scene/style_village.blend --python art/style-pilot/render_style.py -- --looks=S1,S2`,
   the crop's close view with `--views=c15 --res=1920,1200 --samples=128 --out=art/style-pilot/renders/hi`, and
   `python art/style-pilot/compose_style.py`. Logs: `log-maps-s1s2*.txt`, `log-render-*.txt`.

**Known limits of the S1 and S2 sets here.** One image per material, no choice of a or b. The painted
highlights are part of the texture, so they do not follow the sun (most visible on S1). The eave fringe
card on the shrine has no Flow version and is only tinted to match.

## Files and provenance

- `build_scene.py` builds `scene/style_village.blend` and `scene/path_mask.png` from `art/shrine/shrine.blend`,
  `art/age1/source/age1.blend` and `art/trees/source/broadleaf.blend`.
- `style_maps.py` makes `p-src/` (look P) and `maps/<LOOK>/`. It runs every candidate through
  `art/textures-pilot/make_maps.py` and writes a JSON report per set. It adds kinds for brick, plaster, tiles,
  wood, ground and grass, with a generic height map.
- `render_style.py` renders to `renders/<view>-<LOOK>.png`. The first P attempt (lighter processing) is kept in
  `renders/p-v1/`.
- `compose_style.py` builds the sheets, the crop and the grade. Logs are in `log-*.txt`. The crop uses
  `renders/hi/c15-<LOOK>.png` (1920 x 1200).
- **PHOTOREAL and P sources:** `art/textures-flow/` "a" images (Google Flow, Nano Banana, owner's account,
  2026-11-19, 0 credits).
- **Library provenance (backfilled 2026-11-20):** every one of the 84 JPEGs in `art/textures-flow/` now has
  `<image>.provenance.json`, written by `python tools/flow_provenance.py backfill`. That command never
  overwrites a sidecar. What each record is based on:
  - **Dates, upscale and origin are proven by the file.** Each download carries Google-signed C2PA manifests. One
    says "Created by Google Generative AI" (trainedAlgorithmicMedia) with SynthID applied, and its RFC 3161
    time-stamp is the generation time. One says opened and resized, which is the 2K upscale on download. The
    library was generated on 2026-11-19 from 21:07 to 21:48 local time (-05:00). The pilot's four were generated at 19:34. The file times trail by
    seconds to a minute.
  - **Prompts:** 78 of the 84 are recorded as `"prompt_status": "template, exact wording not recorded"`. No
    record of the generating agent's exact wording exists in the project, and its transcript is not
    available. The record keeps the `art/textures-pilot/README.md` template and the folder name. The 4 images
    in `stone-rubble/` and `thatch-reed/` are byte-identical (sha256) to the pilot's `art/textures-pilot/flow/`
    downloads, generated at 19:34. Their records carry the pilot's exact stone and thatch prompts.
  - **The 2 night masks** (`glass-office-a-night-mask`, `window-grid-concrete-b-night-mask`) are Flow
    *edits*. Their C2PA has a second "created" manifest with an input image, and that image's generation
    manifest matches `glass-office-a.jpg` and `window-grid-concrete-b.jpg`. The records name the parent. The
    edit instruction is not recorded.
  - **Model** is "Nano Banana 2.1" from the project recipe. C2PA does not name the model, and each record says
    so. **Credits** are 0 as the owner reported (`docs/ASSETS.md`); this is not shown per image.
- **S1/S2:** `flow/<S1|S2>/<mat>-a.jpg`, each with `<mat>-a.provenance.json` written at download by
  `tools/flow_provenance.py ingest`: exact prompt (`"prompt_status": "exact"`), Google C2PA generation and
  download time-stamps, 2048 x 2048, sha256. Generated 2026-11-20, 02:44 to 03:01 local time (-05:00).
