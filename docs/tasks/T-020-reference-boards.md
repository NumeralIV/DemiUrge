# T-020 — Reference boards: the strategy-game look at 40 m, 300 m and 2 km

**Assignee:** Grok · **Status:** open · **Branch:** `helper/grok/T-020`
**Deliver to:** `docs/concept/reference-boards/` only.

## Why
The owner wants the game to look like a high-quality strategy and city-building game: cinematic, polished and
readable, with a whole world that holds up close, at the town view and from orbit. "Stylised realism" is too
vague to judge a texture or a building against. These boards turn it into concrete visual targets for three
subjects (**buildings**, **terrain**, **towns**) at the three distances the player actually uses (**40 m**,
**300 m**, **2 km**). Every Flow texture in `docs/flow-queue.json` and every kit and material change will be
judged against them.

Describe and generate the look by its **qualities**. Do not name, copy or imitate any existing game, studio,
film or living artist, in prompts, file names or notes (AGENTS.md section 4).

## Read first
`AGENTS.md`; `docs/CONCEPT.md`; `art/style-pilot/README.md` (the brief, the strategy grade, the S1/S2 prompt
families) and its sheets `shots/style-s300.png`, `shots/style-s80.png`, `shots/style-c15.png`,
`shots/style-swatches.png`; the approved orbit look `shots/ue-cloudlab-terminator-final.png` (does not change);
`docs/concept/settlements/index.md` (ages and the three cultures, with palettes);
`docs/concept/nature/index.md` (descent series and biomes, with measured hex targets);
`docs/flow-queue.json` (header and tier 0 jobs: the three look anchors your boards refine).

## The look in qualities (the brief your boards must make concrete)
1. **Shape before surface.** Every building reads as a clear silhouette and roof shape at the distance it is
   seen. Detail is grouped into a few large, deliberate shapes (courses, panels, tiles) rather than noise.
2. **Value structure.** Roofs, walls, ground and vegetation sit in distinct value bands, so a greyscale version
   of the image still reads. Shadows are lifted and clean, not crushed; highlights roll off gently.
3. **Controlled, slightly rich palette.** About 10-15 % more saturated than a photograph, never neon. Each age
   and each culture owns a signature palette (for example warm straw and ochre in the Bronze age, terracotta and
   whitewash in the Sail age, red-brown brick and slate in the Industry age, white, pale gold and green in the
   Space age; rose-ochre with cobalt for the Cliff-Terrace people, silver-grey with saffron for the
   Lagoon-Weavers, pale limestone with blue-grey slate for the Ring-Builders).
4. **Believable materials, simplified.** Stone, thatch, brick and timber are physically lit and recognisable,
   with clean joints and gentle edge highlights, but no photo-scan grit, no bold outlines, nothing toy-like.
5. **Light tells the time.** Golden-hour sun from the left with long soft shadows is the hero light; boards may
   add one overcast and one night variant per subject so we can see the materials hold up without the sun.
6. **Life and use.** Paths worn where people walk, fields at different stages, smoke, laundry, boats, carts:
   towns look inhabited and reactive, not like empty models.
7. **Continuity of scale.** The same place must read as the same place from 40 m to 2 km: roof colours,
   field pattern and road lines carry through as the detail falls away.

## The nine boards
One board per subject and distance. Each board is a single 1920 x 1080 PNG laid out as:
a **hero image** (left two thirds), **three detail crops** (right column, each a 2x enlargement of a part of the
hero that shows a quality above), a **palette strip** of 8-12 swatches with hex values along the bottom, and
**no text inside the hero**. Put labels and notes in `index.md`, not on the images, except the swatch hex codes.

| Board | Camera (1080p) | What must read | Measurable targets for the notes |
|---|---|---|---|
| `buildings-40m` | 40 m, three-quarter, 25-35° down | one house and one civic building of the same age: wall material, roof covering, doors and windows, plinth on sloped ground | house about 300-500 px wide; stone courses, roof tiles and thatch courses individually visible; no texture repeat visible on a 10 m wall |
| `buildings-300m` | 300 m, 45° down | a street block: roof shape and colour per building, wall colour, which one is the temple | house about 40 px; roof vs wall contrast readable in greyscale; the temple or civic building is the lightest or most distinct roof in frame |
| `buildings-2km` | 2 km, 45-55° down | a district: roof colour fields, landmark silhouettes (dome, chimney, tower) | house about 8 px; landmarks still identifiable by silhouette alone |
| `terrain-40m` | 40 m, 30° down | ground under a town edge: grass, path, field edge, rock outcrop, river bank | grass clumps and path pebbles visible but calm; no tiling pattern across 40 m; blends between materials soft and irregular |
| `terrain-300m` | 300 m, 45° down | a valley floor: meadow, forest edge, fields, river, beach | field stripes and forest crowns read as texture, not noise; river darker and more saturated than land; shoreline a clean bright rim |
| `terrain-2km` | 2 km, 50° down | biome mosaic: forest, grassland, farmland quilt, rock ridges, snow line | macro variation at 100-500 m scale; no visible repeat of any ground texture; colours trend toward the measured descent targets in `docs/concept/nature/index.md` |
| `towns-40m` | 40 m, street level to 30° | a lane with people, carts, market stalls, smoke | figures about 30-60 px tall, readable by colour and silhouette; props grouped, not scattered evenly |
| `towns-300m` | 300 m, 45° down | a whole town of one age and culture: walls, lanes, plaza, fields outside | the town footprint and its wall or edge read in under a second; lanes form a clear network; culture identity readable from palette alone |
| `towns-2km` | 2 km, 50° down | three towns of different sizes and ages in one valley | each town reads as a coloured cluster with a landmark; roads link them; farmland rings each town |

Make all nine in the **Iron age, Valley people** first (it sits in the middle of the age range and uses the
most materials), on one shared invented valley, so the boards show continuity of scale.

## Extensions (same layout, smaller sets)
- `ages-300m.png`: seven 300 m panels in one row strip (Stone to Space), the same street corner in every age, so
  the signature palette of each age is side by side.
- `cultures-300m.png`: three 300 m panels, the same Iron-age town built by each of the three cultures.
- `night-300m.png`: the `towns-300m` town at night (warm windows, lamp pools, dark roofs still separated from
  the ground), for the night-side lights target.
- `do-and-dont.png`: four pairs. Left of each pair: what we want. Right: the failure to avoid (photo-scan grit;
  cartoon outlines and toy colours; beige-on-beige where roofs and walls merge; a ground texture tiling in a
  visible grid).

## Deliver
1. The nine boards, `buildings-40m.png` … `towns-2km.png`, and the four extension sheets, as PNG (at most
   about 4 MB each) in `docs/concept/reference-boards/`.
2. The raw hero images separately as `hero-<board>.png`, so the lead can use them as Flow reference images.
3. `index.md`: for each board, the exact prompt of every generated image, what each detail crop proves, the
   palette hex values (sRGB, measured from the hero, like `docs/concept/nature/index.md` does), and three to six
   **checkable statements** an engineer can test a game screenshot against (for example "roof and wall differ by
   at least 20 % in luminance", "no ground repeat visible inside the frame").
4. `contact-sheet.jpg`: all boards on one sheet.

## Acceptance criteria
- Nine boards plus four extensions, all following the layout above, with no text in hero images.
- The look matches the qualities above: stylised realism, clean readable shapes, slightly rich palette,
  physically lit; not photo-scan, not cartoon. Placed beside `shots/ue-cloudlab-terminator-final.png`, the
  2 km boards look like the same world on the way down.
- Continuity: the nine Iron-age boards show one valley; roof colours, fields and roads carry through the three
  distances.
- Every hero passes a greyscale check (roof, wall, ground and vegetation separate) and the notes say so.
- Every prompt recorded exactly; everything original; no named game, studio, film or artist anywhere.
