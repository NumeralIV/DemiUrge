# Texture pilot: can Nano Banana (Google Flow) paint our textures?

Question from the owner: could Google's Nano Banana image model paint the building textures instead of our
numpy-generated (procedural) sets? This folder answers it for the village shrine's two hero materials, rubble stone
and reed thatch, with the full path from a Flow image to an Unreal-ready texture set, and BEFORE / AFTER renders.

## Verdict

| Material | Verdict | Use | Why |
|---|---|---|---|
| **Rubble stone walls** (`stone-b`) | **Adopt, with conditions** | `T_Shrine_StoneB_*`, 2.0 m tile | Clearly better than the procedural `rubble` at 8 m and 40 m in both lights: real fieldstone shapes, lichen, clay bedding with gravel, believable colour variation; the procedural set reads as a regular pattern of rounded boxes beside it. Condition: at equal mean albedo the Flow wall *reads* darker (dark mortar gaps, no large light areas), most visibly at 300 m; lift the wall albedo target about 10 % or the MI tint when it goes into the game, and check it beside the stand-in in AssetLab. |
| Rubble stone, alternative (`stone-a`) | Keep as a variant | `T_Shrine_StoneA_*`, 1.4 m tile | Bigger, more ochre stones; heals as cleanly as B and renders as well (`texpilot-alt-*`). A second rubble look for variety (another culture or building type). |
| **Plinth** (stone on the plinth, as the brief asked) | **Reject for the plinth** (not the texture) | | The T-014 sheet specifies *fitted limestone blocks* for the plinth and steps, and the 300 m shots show why: the pale limestone plinth is one of the shapes that must survive at map distance, and with rubble it disappears into the walls (`texpilot-300m-*`). Generate a "fitted limestone block" texture with the same recipe instead (template below). |
| **Reed thatch** (`thatch-b`) | **Adopt, with conditions** | `T_Shrine_ThatchB_*`, 1.1 m tile | Photographic reeds, real butt-end courses and weathering beat the procedural combed strands at 8 m and still read at 40 m. Conditions: the roof's straw **fringe** card and the ridge rolls still use the procedural `fringe` set and must be made from the same Flow image (the colour step at the eave shows in the close shots); the darker bundles repeat as a grid on large roofs (4 x 4 proof), so big roofs need macro variation (see "What to watch"). |
| Reed thatch, alternative (`thatch-a`) | Second choice only | `T_Shrine_ThatchA_*`, 1.0 m tile | Warmer and more even, and on the roof it renders as well as B (the slope, rope bands and fringe hide its join). But in the flat 4 x 4 proof the vertical heal shows: the reeds lean one way left of the join and the other way right of it for a whole course, and no cut can avoid that. Fine for roofs, not for anything seen flat-on (reed panels, screens). |

**Overall: adopt AI-painted colour for tileable building materials**, processed by `make_maps.py`, with the conditions
above and the licensing decision below. It is a clear quality step at the distances where a player looks at a
single building, it costs nothing in Flow, and the processing is automatic (about one minute per texture).

The one thing it does not give us is *real* geometry information: height, normal, roughness and AO are derived
from the colour, so they are plausible rather than measured. They are good enough for these materials at game
distances, but they will never be as clean as a procedural height field. Where relief matters more than colour
(paving seen from above, carved stone) the procedural sets stay better or should be combined (Flow colour +
procedural height).

## Evidence

Renders (Cycles, the intact shrine LOD0, its own vertex masks kept, only the textures differ), BEFORE = the asset's
procedural sets from `art/textures`, AFTER = Flow `stone-b` on walls and plinth and `thatch-b` on the roof:

| | golden hour | overcast |
|---|---|---|
| 40 m three-quarter | `shots/texpilot-40m-golden.png` | `shots/texpilot-40m-overcast.png` |
| 300 m oblique (with a 3x centre inset) | `shots/texpilot-300m-golden.png` | `shots/texpilot-300m-overcast.png` |
| close, 8 m | `shots/texpilot-close8m-golden.png` | `shots/texpilot-close8m-overcast.png` |
| Flow B vs Flow A sets | `shots/texpilot-alt-40m-*.png`, `shots/texpilot-alt-close8m-*.png` | |

Reproduce (from the project root; Cycles on the GPU, 96 samples, 960 x 720 per panel, sun 8 deg from the
front-left for golden hour, a sunless grey-white sky for overcast, a plain earth/grass ground):

```
python tools/render_slot.py -- blender -b art/textures-pilot/scene/shrine_pilot.blend --python art/textures-pilot/render_pilot.py -- --out=art/textures-pilot/renders
python tools/render_slot.py -- blender -b art/textures-pilot/scene/shrine_pilot.blend --python art/textures-pilot/render_pilot.py -- --out=art/textures-pilot/renders/alt-A --views=v40,close8 --variants=after --stone=StoneA --thatch=ThatchA
python art/textures-pilot/compose_shots.py
```

Tiling proofs, 4 x 4 (raw Flow image vs processed; and every derived map):
`shots/texpilot-tile-<StoneA|StoneB|ThatchA|ThatchB>-D.png` and `...-maps.png`.

Measured by `make_maps.py` (`out/<Name>.json`):

| Set | tile | seam at wrap, raw (x / y) | worst line after (x / y) | light repeat raw -> after | mean albedo (target) |
|---|---|---|---|---|---|
| StoneA | 1.4 m | 1.57 / 1.58 | 1.33 / 1.30 | 0.034 -> 0.014 | #886d50 (#8a6c4c) |
| StoneB | 2.0 m | 1.53 / 1.78 | 1.33 / 1.46 | 0.028 -> 0.012 | #876d51 (#8a6c4c) |
| ThatchA | 1.0 m | 1.60 / **5.10** | 1.57 / 1.53 | 0.059 -> 0.028 | #967b57 (#9a7a4a) |
| ThatchB | 1.1 m | 1.03 / **4.17** | 1.52 / 1.35 | 0.035 -> 0.026 | #977a56 (#9a7a4a) |

- *Seam*: the mean luminance step across the wrap line divided by the median step across any pixel line (1.0 = the
  wrap is indistinguishable from any other column). *Worst line after* is the largest value over **every** line of
  the processed tile, so it would also catch a straight healed seam; values of 1.3-1.6 are the natural maximum of
  these textures (a mortar joint or a butt line that happens to run straight), and the wrap itself is now 0.8-1.1.
  Both thatches had a strong horizontal wrap seam (4-5x: a course cut in half); stone was milder than it looked
  (about 1.5x) but cut stones in half.
- *Light repeat*: standard deviation of the very-low-pass luminance (sigma = tile/10) over its mean, the
  light/dark blotch that repeats once per tile. Halved for every set.
- Albedo is calibrated to the T-014 sheet in linear space (luma exact, hue moved half-way), so the Flow sets slot
  into the existing materials without re-tuning exposure.

## Files

| Path | What |
|---|---|
| `flow/stone-a.jpg`, `stone-b.jpg`, `thatch-a.jpg`, `thatch-b.jpg` | the four Flow downloads (2048 x 2048 JPEG), untouched |
| `tile-check.jpg` | the lead's 2 x 2 check of the raw images |
| `make_maps.py` | **the recipe**: Flow image -> seamless, de-lit, calibrated texture set. Use it for every future Flow texture |
| `out/T_Shrine_<Name>_<Map>.png` | 2048 sets; `out/1k/` the same names at 1024; `out/<Name>.json` metrics |
| `tile_proof.py` | the 4 x 4 tiling proof sheets |
| `render_pilot.py`, `compose_shots.py` | BEFORE / AFTER renders and the side-by-side sheets |
| `scene/shrine_pilot.blend` | copy of `art/shrine/shrine.blend`; after `render_pilot.py --save` it holds the AFTER materials (`rubble_flow_StoneB`, `limestone_flow_StoneB`, `thatch_flow_ThatchB`) on the intact LOD0 |
| `renders/` | the raw panels (`<view>-<light>-<before|after>.png`, `alt-A/`: Flow A sets) |

Maps per set (`<Name>` = StoneA, StoneB, ThatchA, ThatchB):

| Map | Encoding | Notes |
|---|---|---|
| `_D` | sRGB 8-bit RGB | de-lit, calibrated diffuse (base colour) |
| `_H` | linear **16-bit** grey | 0..1 height, relief 4.5 cm (stone) / 2.5 cm (thatch) |
| `_N` | linear 8-bit, **OpenGL (+Y)** | project convention: `ue_import.py` sets `flip_green_channel` on import, Blender uses it as is |
| `_N_DX` | linear 8-bit, **DirectX (-Y)** | green already flipped, for importing into Unreal **without** the importer's flip. Use one or the other, never both |
| `_R` | linear 8-bit grey | roughness |
| `_AO` | linear 8-bit grey | cavity AO from the height (mean 0.89) |
| `_ORM` | linear 8-bit RGB | R = AO, G = roughness, B = metallic 0: the project's ORM layout, drop-in for `M_DmAsset` |

## The prompt recipe that worked

Model **Nano Banana 2.1** in Google Flow, aspect **1:1**, download **2K (upscaled)**. Two images per prompt; the
better one is kept.

Stone:

> Seamless tileable texture of an ancient rubble stone wall, irregular rounded fieldstones in warm grey and ochre
> bedded in rough clay mortar, seen straight on, orthographic, flat even overcast lighting, no shadows, no
> perspective, fills the whole square edge to edge, tiles without visible seams, photorealistic material scan,
> high detail, no text

Thatch:

> Seamless tileable texture of weathered reed thatch roofing, bundles of dry golden-brown reeds in overlapping
> courses, seen straight on from above, orthographic, flat even overcast lighting, no shadows, no perspective,
> fills the whole square edge to edge, tiles without visible seams, photorealistic material scan, high detail,
> no text

What the parts do: *seen straight on / orthographic / no perspective* stops the camera-angle photo look (which
cannot tile); *flat even overcast lighting, no shadows* keeps baked light low (it is never zero, hence the
de-light step); *fills the whole square edge to edge* stops vignettes and borders; *photorealistic material scan*
gives the de-lit "scan" look; *no text* stops watermarks-as-text and labels. *Seamless / tiles without visible
seams* does **not** make the image tile: none of the four did. Expect to heal every image.

## Processing (make_maps.py)

```
blender -b --factory-startup --python art/textures-pilot/make_maps.py -- <input.jpg> <Name> <stone|thatch> <tile_m> --target=#rrggbb
blender -b --factory-startup --python art/textures-pilot/make_maps.py -- --pilot        # rebuilds the four pilot sets
python art/textures-pilot/tile_proof.py <Name>                                         # 4 x 4 proofs -> shots/
```

Route it through `tools/render_slot.py` like any heavy job. 40-80 s per 2048 set on the owner's PC (numpy in
Blender's Python; no SciPy or OpenCV needed). Steps, in order (each is one function in the script):

1. **Decode** (Blender's image loader), sRGB -> linear.
2. **Pre-flatten**: divide luminance by a very wide blur (sigma = N/8) so the two image edges that will meet have
   the same brightness.
3. **Offset by half** (N/2, N/2): the wrap seam becomes a cross in the middle; the tile's new edges are the image's
   untouched centre lines, so they tile perfectly.
4. **Heal the cross**, one arm at a time: the strips either side of the seam (16 % of N for stone, 20 % / 12 % for
   thatch) are laid on top of each other and joined along a **minimum-cost cut** found by dynamic programming. The
   cut closes on itself across the tile. Its cost is colour difference plus a material term:
   - stone: crossing a stone is expensive (a stone mask from brightness, greyness and edge business, cleaned
     morphologically), so the cut runs **along the mortar joints** and does not halve stones;
   - thatch, horizontal arm: the cut is cheap only along a **course butt line** (one course's reed ends lying over
     the next is exactly what a real course junction looks like);
   - thatch, vertical arm: the cut must keep the **reed direction** the same on both sides (structure-tensor angle
     term) and may jump sideways at a butt line, so the join is staggered course by course like bundles.
   The two sides are merged with **multi-band blending** (5 Gaussian bands, each crossfaded over a width matched
   to its scale): colour matches over a wide zone, reed and gravel detail stays sharp at the cut.
5. **Resample** the healed (N - overlap) tile back to N x N by FFT (exact for a periodic image).
6. **De-light**: luminance high-pass at sigma = N/10, ratio clamped 0.6-1.6, so 4 x 4 tiling has no light/dark
   repeat; then a mild **cavity lift** (35 % stone, 15 % thatch) because the photo's crevice shadows are partly
   baked AO and the engine adds AO again.
7. **Calibrate** albedo: mean luma = the art-direction target (T-014 sheet), hue moved half-way to the target.
8. **Maps**: height (stone: domed stones from the stone mask over low gravelly mortar, plus detail; thatch: reed
   band-pass plus a ramp that rises to each butt line), normal (Sobel on the periodic height, OpenGL and
   DirectX), roughness (stone: mortar 0.96, stone 0.80, tops 0.66; thatch 0.80-0.97 by cavity and height), AO
   (cavity at three radii, normalised to mean 0.89), ORM.
9. **Write** 2048 and 1024 (2 x 2 box average, normals renormalised).

To add a new family (wood, plaster, brick, tiles), add an entry to `KINDS` and, if the cut needs a material
term (brick: follow the joints; planks: cut along the grain), a branch in `heal_rows`; for most materials the
colour term alone is enough.

## What to watch for

- **Seams.** No Flow image tiled out of the box, whatever the prompt says. Always heal, then look at the 4 x 4
  proof. Reject a source when a structure runs across the whole tile in two different directions (thatch-a's reed
  lean) - no cut can fix that; generate another image instead.
- **Baked light.** Even "flat overcast, no shadows" images carry a soft gradient across the frame (thatch-a: 6 % of
  the mean) and dark crevices. The de-light step removes the gradient; crevice darkness is only partly removed on
  purpose (it is also real albedo). Check the result in golden-hour renders: a texture with baked light from one
  side looks wrong on the wall that faces the other way.
- **Repetition.** A photographic texture has memorable features (one ochre stone, one dark bundle) that repeat
  every tile. At 2 m (stone) it is fine on a 5 m wall; on the thatch at 1.1 m the dark bundles form a visible
  grid on large roofs. Mitigate in the material: the `M_DmAsset` per-instance tint already varies buildings;
  add a world-space macro-variation (low-frequency noise on tint and roughness, 7-15 m) and, for big roofs,
  blend two Flow sources by a noise mask. Prefer the larger plausible tile size.
- **Scale.** Count the features: stones 25-40 cm, reed courses about 0.3 m. The tile size goes into the UVs
  (our UV0 is metric, 2 m per unit; the pilot scales by `2.0 / tile_m`).
- **Colour drift.** Flow images come out warmer or greyer than the sheet; the calibration step fixes the mean,
  not the spread. Keep the art-direction hex targets in the call (`--target`).
- **Derived maps are approximations.** Height from colour inverts where a dark stone sits in light mortar; the
  stone mask handles this pilot's images but check the `_H` sheet for every new source.
- **JPEG.** The 2K download is a JPEG; do all processing from it once, keep the PNGs as the masters.

## Cost

| Item | Cost |
|---|---|
| Generation in Flow with Nano Banana 2.1 | **0 credits** on the owner's existing subscription (as of 2026-11-19) |
| 2K download (upscaled, what the pilot used) | free |
| 4K download | needs a plan upgrade: not needed, our building sets ship at 2048 and 1024 |
| Processing | local, about 1 min per set; no paid tools |

## Licensing notes (owner decision needed)

- **Project rule.** Until now every asset was original and procedural, with "no downloads, no third-party inputs,
  nothing to license" (`docs/lab/asset-pipeline.md`, the age kit READMEs). Flow textures are third-party
  generated inputs, so adopting them changes that rule: the owner should approve it explicitly, and every Flow
  texture needs a provenance line (tool, model, date, prompt, account) in the kit's manifest. This README is the
  record for the four pilot images.
- **Google's terms.** As generally published, Google's generative AI terms (Google Terms of Service, the
  Generative AI Additional Terms and the Generative AI Prohibited Use Policy, which Flow is under) say Google does
  not claim ownership of the content you generate and that you are responsible for how you use it, within the
  prohibited-use policy. This pilot had no network access, so the current text was **not** re-checked: before
  shipping, the owner should read the current terms for the Flow plan of the account that generated the images
  (commercial use, any attribution or disclosure duty) and keep a dated copy with this README.
- **SynthID.** Every Nano Banana image carries Google's invisible **SynthID** watermark. It is built to survive
  crops, resizing, colour changes and compression, so assume our processed textures still carry it. It is
  invisible and harmless in-game, but anyone can verify that the textures are AI-generated. No visible watermark
  was found on these four downloads.
- **Copyright.** In several jurisdictions (for example the US Copyright Office's position) purely AI-generated
  images are not protected by copyright, so we could not stop others from reusing these textures. That is an
  acceptable risk for tileable surface textures; it would not be for characters or key art.
- **Disclosure.** Some storefronts ask whether a game contains AI-generated content (Steam requires a disclosure
  for pre-generated AI content). Adopting this means ticking that box.

## Proposed workflow for the building kits

Per kit: list the tileable surface materials, write one prompt per material from the template, generate 2-4
images, keep the one that heals best, run `make_maps.py` with the kit's tile size and albedo target, check the
4 x 4 proofs, then hand the set to the kit's material slot. Trim sheets and atlases (Age0 / Age3 / Age5
`Structure / Roof / Detail`) are not tileable squares: keep them procedural, or generate the individual
materials with Flow and pack them into the sheet with the existing scripts.

**Prompt template** (fill the braces; keep the tail exactly):

> Seamless tileable texture of {material, its age and condition}, {the shapes, sizes and how they are laid},
> {two or three colours}, seen straight on{, from above for roofs and floors}, orthographic, flat even overcast
> lighting, no shadows, no perspective, fills the whole square edge to edge, tiles without visible seams,
> photorealistic material scan, high detail, no text

| Age | Materials to generate (`kind` for make_maps) | Example fill-ins |
|---|---|---|
| 0 Stone | reed/grass thatch (thatch), wattle and daub (new kind `plaster`), hide (new `cloth`), packed earth (stone with mortar term off) | "beehive hut thatch of grass and heather, loose uneven tufts, straw and grey-brown"; "wattle-and-daub wall, woven hazel rods half covered in cracked clay daub, ochre and grey" |
| 1 Bronze | **rubble stone** (done), **reed thatch** (done), mudbrick (brick), lime render (plaster), fitted limestone blocks (stone) | "sun-dried mudbrick wall, rectangular bricks 40 cm long in mud mortar, eroded edges, tan and grey-brown"; "fitted limestone block plinth, rectangular blocks 0.4-0.6 m long with thin dark joints, pale buff" |
| 2 Iron | ashlar, rubble, limestone, split shingle, slate, clay tile, stone tile, thatch | "split oak shingle roof, overlapping rows of hand-split shingles 15 cm wide, silver-grey weathered wood" |
| 3 Sail | brick, timber cladding, pantile, cobbles, harbour stone | "terracotta pantile roof, S-shaped clay tiles in overlapping rows, orange-red with lichen" |
| 4 Industry | red and buff brick, slate, granite setts, gravel, corrugated iron | "Victorian red brick wall in Flemish bond, soot-darkened, lime mortar joints" |
| 5 Atomic | precast concrete, terrazzo, asphalt, tar roof | "precast concrete panel surface, board-marked, rain streaks, light grey" |
| 6 Space | ceramic cladding, green roof sedum, composite panels | "sedum green roof, dense low succulents in mixed greens and reds, seen from above" |

New `kind`s needed for that list: `brick` (cut along the joints, mortar mask like the stone one but from
straight lines), `plaster` (colour cut only, larger high-pass radius), `wood` (cut along the grain, roughness from
grain), `cloth`. Each is an entry in `KINDS` plus at most a few lines in `heal_rows`, `height_map` and `rough_map`.

Integration (not done here; the shrine's own files are untouched): copy the chosen set to
`art/textures/<set>/T_<Set>_BC/N/ORM.png` (`_D` -> `_BC`, `_N` OpenGL, `_ORM`), add its `tile_m` to
`art/textures/manifest.json`, re-run `build_asset.py` / `ue_import.py`. Because UV0 is metric at 2 m per unit, a
set with a different tile size needs either a UV scale in the master material (`TileScale` parameter per MI) or
a re-export with the manifest's new `tile_m`.

## Independent check

Second reviewer, 2026-11-19. I did not run the pilot. I re-ran its recipe, read every proof sheet and render
panel, measured the maps and renders, and fixed what I could in this folder. Tools and evidence are in `check/`.

### Verdict

**The pilot's main answer stands, with one correction.** Flow (Nano Banana) colour, processed by `make_maps.py`,
beats the procedural rubble and thatch at the distances where a player looks at one building. The pilot's "Flow
wall reads darker because of its mortar" condition was wrong, though. The cause was a defect in the derived
normal maps, now fixed. No albedo lift is needed.

| Question | Answer |
|---|---|
| Do the Flow textures clearly beat the procedural ones at game distances? | **At 8 m, yes, clearly**: real fieldstones, lichen, gravelly clay and real reed courses against a regular grid of rounded boxes and combed strands. **At 40 m, yes for the walls** (they read as rubble, not a pattern); the roof is a smaller gain (the courses read better). **At 300 m, no difference** apart from the plinth: the shrine is about 70 px wide and only mean colour survives. After the fix the Flow walls and roof render at 0.97-1.03x the procedural brightness in every view and light (table below), so they neither win nor lose there. The plinth verdict is confirmed: with rubble on it, the plinth side renders at 0.60x and the pale plinth silhouette is lost. |
| Do the derived normal and roughness maps behave? | **Direction yes, strength no (fixed).** Relief is not inverted: the normal agrees with the slope of `_H` (correlation +0.74 to +0.89 on x and y), `_N_DX` is exactly the green-flipped `_N`, and there is no baked-light tilt (low-frequency normal content 0.005-0.016, lower than the procedural sets' 0.012-0.049). There is no stripe at the healed lines in N, R, D or H: the largest row/column means sit away from the heal cross and match source structure (stone courses, reed bundles). **But** the pilot's normals had a mean tilt of **61-67 deg** from flat, against 23 deg (rubble) and 31 deg (thatch) for the procedural sets. The pixel-scale and JPEG detail in the height was taken at full metric relief. That alone made the Flow walls render **0.68-0.72x** and the roof **0.67-0.78x** as bright as BEFORE (A/B proof below). The thatch course ramp was also inverted (high just *under* each butt line); the luminance term hid it in the total height, so I fixed it but its visual effect is small. Roughness has no stripes. The stone set was glossier than the project's rubble (mean 0.836 against 0.917, stone tops 0.64), now 0.89. Thatch R is nearly flat (0.90 +/- 0.07), which is fine for reed. |
| Is the workflow repeatable? | **The texture step, yes. The render step was not (now fixed).** `make_maps.py --pilot` reproduced every pilot PNG **byte for byte**, in 22-45 s per set, and the single-image command works. Re-running the README's render command on the saved `scene/shrine_pilot.blend` was broken three ways (details below), so the before/after evidence could not be reproduced as documented. Still manual, and the README says so: generate 2-4 images and reject sources that cannot heal (thatch-a), check the 4 x 4 proof for every new image, and add a `kind` per new material family. |

**Revised conditions for adoption** (the rest of the pilot's verdict table stands):

- Rubble stone-b: adopt. **Drop the "+10 % albedo" condition**: it was compensating for the normal-map defect. Re-check brightness in AssetLab once only, because UE's normal-map shading and mip filtering differ from Cycles.
- Plinth: reject rubble, as the pilot said; generate fitted limestone.
- Thatch-b: adopt with the pilot's two conditions: a matching eave fringe and ridge, and macro variation on big roofs. The 4 x 4 proof shows the darker-bundle grid clearly.
- Stone-a as a variant and thatch-a as second choice: agreed.
- Licensing and owner approval: agreed as written. I could not re-check Google's terms either (no network). The owner still has to approve the change to the "no third-party inputs" rule in `docs/lab/asset-pipeline.md` before anything goes into `art/textures`.

### Evidence

Brightness of the same screen regions, AFTER / BEFORE, same render run. Measured with `check/measure_ab.py`
(mean of the display image converted back with the sRGB curve, so read it as relative only):

| View, light | Wall front, pilot -> **fixed** | Roof, pilot -> **fixed** | Right wall or plinth side, pilot -> **fixed** |
|---|---|---|---|
| 40 m golden | 0.70 -> **0.99** | 0.78 -> **1.03** | plinth 0.47 -> 0.60 |
| 40 m overcast | 0.68 -> **1.01** | 0.67 -> **1.01** | plinth 0.45 -> 0.60 |
| 8 m golden | 0.71 -> **1.00** | 0.77 -> **1.00** | wall 0.69 -> **1.00** |
| 8 m overcast | 0.68 -> **1.02** | 0.71 -> **1.00** | wall 0.68 -> **0.97** |

- `shots/texpilot-check-normals.png`: A/B at 480 x 360. Columns: BEFORE; AFTER with the pilot's normals; AFTER
  with no normal map; AFTER with the fixed normals. Only the normal map changes between the last three columns.
  The pilot's normals are 0.67-0.78x; with no normal map the Flow surfaces are 1.07-1.15x; with the procedural
  normal map 0.96-1.03x; with the fixed normals 0.97-1.03x. Swapping the roughness changed nothing (0.67-0.78x
  either way). So the darkening was the normals alone, not the albedo or the mortar.
- The `shots/texpilot-*` sheets are re-rendered with the fixed maps and the fixed `render_pilot.py` (Cycles,
  960 x 720, 128 samples: the script's default, not the 96 the "Reproduce" section says). The pilot's originals
  are kept in `check/orig-shots/` and `check/orig-renders/`; its normal maps and JSON are in `check/orig-out/`.
- Map measurements: `check/check_maps.py` (normal against height, normal tilt, a regression of the photo's
  baked light on the normal, row and column banding, thatch course profile; `--procedural` gives the same
  numbers for `art/textures`).
- The thatch course profile (mean `_H` around detected butt shadows, 2048 px) is now the expected sawtooth.
  ThatchB `_H` rises from 0.49 at 60 px above to 0.57 just above the butt ends, drops to 0.35 in the shadow, then
  climbs slowly through the next course.
- Healed seams at full resolution (`check/heal_before_after_<StoneB|ThatchB>.png`, pilot on top, now below):
  there is no straight seam and no loss of detail along a whole row or column. Small local ghosts remain where a
  cut crosses unlike structure (a soft shadow band under a butt line, two stones touching). They are 5-30 cm on
  the wall, appear once per tile, and only show from closer than about 8 m. Treat this as a known limit of the
  recipe, not a blocker.

New metrics (`out/<Name>.json`). Seams, light repeat and albedo are as the pilot reported, within 0.01, except
StoneB's worst row line, which is now 1.35 (was 1.46):

| Set | wrap x / y | worst x / y | light repeat | albedo | roughness mean | normal tilt (strength) |
|---|---|---|---|---|---|---|
| StoneA | 0.82 / 1.09 | 1.33 / 1.30 | 0.014 | #886d50 | 0.886 | 24 deg (0.23 x 4.5 cm) |
| StoneB | 0.93 / 0.83 | 1.35 / 1.35 | 0.014 | #876d51 | 0.891 | 24 deg (0.33 x 4.5 cm) |
| ThatchA | 0.87 / 1.04 | 1.57 / 1.53 | 0.028 | #967b57 | 0.897 | 30 deg (0.16 x 2.5 cm) |
| ThatchB | 0.76 / 0.85 | 1.52 / 1.35 | 0.026 | #977a56 | 0.900 | 30 deg (0.17 x 2.5 cm) |

### Fixes made (all in `art/textures-pilot/`)

`make_maps.py`:

- **Normal strength.** The height is pre-blurred by 1 px (at 2048) before Sobel. The strength is then lowered,
  never raised, until the mean tilt matches the kind's `tilt_deg`: stone 24 and thatch 30, the procedural
  sets' levels. The JSON records `normal_strength` and `normal_tilt_deg`. `_H` and its stated relief are
  unchanged.
- **Thatch course ramp** now rises toward each butt line going down the roof, then drops into the shadow
  (it was inverted).
- **Stone roughness** is `(0.96, 0.88, 0.78)` (was `0.80` body, `0.66` tops), giving a mean of 0.89. This
  replaces the numbers in step 8 of "Processing" above.
- **Variance-preserving multi-band blend.** Each detail band's crossfade is divided by sqrt(m^2 + (1-m)^2),
  after Heitz and Neyret 2018. A plain 50/50 mix halves the contrast of two unrelated bands along the cut. The
  gain is small (the worst detail dip along StoneA's heal rows went from 0.76 to 0.82 of the median), but it
  never hurts.
- New flags: `--out=<dir>`, and `--prefix=` (file-name prefix, default `T_Shrine_`), so a kit set can be named
  `T_<Set>_...` straight away. `out/` holds the fixed sets, and `scene/shrine_pilot.blend` picks them up by path.

`render_pilot.py`: re-running on the saved scene was broken three ways:

1. `fix_paths()` always re-based `//..` paths on `art/shrine`. The saved scene's paths are relative to
   `scene/`, so all 31 procedural images went missing and BEFORE rendered pink. It now keeps any path that
   resolves.
2. `show_only()` hid the saved `PilotGround`, so the shrine floated over an empty horizon with its plinth
   bottom showing. It is now shown again.
3. `apply_after()` matched slots by the asset material's name, but the saved slots are already linked to
   `*_flow_StoneB`. The alt-A command would silently have rendered the B sets. It now resets to the asset
   materials first. (The pilot's alt-A panels were correct: they were rendered before the scene was saved.)

### Still open

- Not imported into Unreal, and not compared in AssetLab. UE shades normal maps differently from Cycles, so
  check the brightness against the procedural stand-in there before adopting.
- Height, normal, roughness and AO are still derived from colour. The stone mask still misses some
  dark-centred stones (they show as pits in `_H`).
- The pilot's run left four empty folders outside the project, under `C:/art`. I could not touch them and did
  not check them. The owner should delete `C:/art`, as the pilot asked.
