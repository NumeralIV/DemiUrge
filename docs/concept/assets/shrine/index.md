# T-014 · Model reference sheet: the village shrine (Grok)

**What this is:** the first real asset. It is a stone-and-timber village shrine (`b.shrine` in SPEC-SIM §1), as proposed in `docs/research/assets.md` §1, "Proposed first experiment".

**The look it matches:** the approved Bronze-age village in `docs/concept/settlements/age-1-bronze.png`, which has thatch, rubble stone, oak and a beaten-earth yard. It also carries over two things from the Stone-age shrine in `docs/concept/landmarks/building-shrine.png`: the **standing stones** and the **antlers**. The shrine therefore reads as the same cult, now housed.

| File | What it is | Size |
|---|---|---|
| `shrine-hero.png` | Art target: the shrine in its village, golden hour, three-quarter view from about 40 m | 1280×720 |
| `shrine-front.png` | Front elevation, orthographic-style, neutral background (`#c8c8c8`), flat light | 1152×864 |
| `shrine-side.png` | Right-side elevation, front facing left | 1152×864 |
| `shrine-back.png` | Back elevation | 1152×864 |
| `shrine-top.png` | Plan view, front at the bottom | 1152×864 |
| `shrine-materials.png` | Six material swatches with names and albedo chips. The names are composited as real text in HTML (`src/shrine-materials.html`); the generated swatch image `src/materials-raw.jpg` has no text. | 1920×1080 |
| `shrine-culture-a.png`, `-b.png`, `-c.png` | The three culture variants on the same footprint | 768×576 |
| `contact-sheet.jpg` | Everything at a glance | |

**Order of generation.**
1. The hero shot came first, with the Bronze village and the Stone shrine as references.
2. The front view was generated from the hero.
3. The side, back and top views were generated from the front view plus the hero, so the design is defined once and copied, not reinvented.
4. The top view was regenerated once, because the first take was oblique and showed the front wall.

To re-render the materials sheet: `cd src && node render.js shrine-materials`.

## Dimensions (metres)

These are measured off the front view, with the plinth width fixed at 6.0 m. The other views were checked against them, and small differences were rounded to one clean design.

| Part | Value |
|---|---|
| **Plinth** (footprint) | 6.0 wide × 4.4 deep × 0.8 high, with one 0.10 m offset course at the top |
| **Front steps** | 2.4 wide; 4 risers of 0.20 m and 4 treads of 0.22 m; project 0.9 m in front of the plinth, centred |
| **Total footprint, steps included** | 6.0 × 5.3 |
| **Cella** (walled room) | 4.8 wide × 3.2 deep on the plinth, centred left-right, set 0.5 m back from the plinth's front edge |
| **Wall height** | 2.0 from the plinth top to the eave plate; walls 0.45 thick |
| **Corner posts** | 4, each 0.32 × 0.32, set into the wall corners and standing 0.08 proud of the wall face. Height 2.0, with a 0.12 cap block on top. |
| **Doorway** | Centred in the front wall. Clear opening 1.1 wide × 1.6 high (worshippers stoop). |
| **Door posts** | 2, each 0.22 × 0.22 |
| **Lintel** | 2.2 long × 0.30 high × 0.25 deep; a zigzag band is carved on its outer face |
| **Roof** | Hipped. Pitch **47°** on all four slopes. The eave overhangs 0.3 at the sides and back and 0.4 at the front. Ridge 2.6 long, running left-right; ridge height 1.4 above the eave. Thatch thickness 0.35 at the eave. |
| **Finials** | 2, one at each end of the ridge: a turned oak knob, ball 0.22 in diameter on a 0.12 neck, 0.35 tall in total |
| **Antlers** | A deer skull with a 0.9 antler span, fixed at the centre above the lintel, overlapping the eave |
| **Standing stones** | 2 rough, rounded-top monoliths, 0.65 wide × 0.45 deep × 1.25 high. They stand on the ground just in front of the plinth, either side of the steps, with their centres 1.7 from the centre line. |
| **Altar** | Inside, centred 0.6 m behind the doorway: a flat stone block 0.9 × 0.6 × 0.45, with a small fire bowl recess |
| **Window** | One, in the **right** side wall only: 0.35 × 0.35, centred, sill at 1.1 above the plinth top. No window at the back. |
| **Overall height** | ground to finial top: 0.8 + 2.0 + 1.4 + 0.35 ≈ **4.55 m** |
| **Pivot** | Ground level, centred on the plinth (not on the steps). Front = +X in Blender, or whatever the engine's forward axis is. |

## Part list (for the modeller)

Every part listed here is a separate mesh, so the culture variants can swap the walls and the roof.

1. `plinth`: a box with a bevelled top course and block seams. Material: limestone.
2. `steps`: 4 stacked boxes, with cheek blocks at both ends. Material: limestone.
3. `walls`: 3 solid walls plus a front wall with the door opening and the side window. Material: rubble stone. Give the corners rough, irregular silhouettes; no mathematically straight corners.
4. `posts_corner` ×4 and `posts_door` ×2, each with a cap block. Material: oak.
5. `lintel`: carved oak; the carving is normal-mapped or modelled at LOD0.
6. `roof`: a hipped shell with a thick eave lip, plus 3–4 horizontal rope bands per slope (rope `#6b5236`). Material: thatch. Give the eave edge a ragged alpha or a geometry fringe.
7. `finials` ×2. Material: oak.
8. `antlers`: one skull plus antlers (bone `#d9cbb0`).
9. `cloth` ×2: strips about 0.25 × 0.7, tied round each door post just below the lintel. Material: saffron wool, with optional wind cloth sim.
10. `standing_stones` ×2: separate meshes on the ground, not attached to the plinth.
11. `altar`: a stone block with a fire bowl. Emissive embers `#ff9a3a` at night.

**Targets:**
- LOD0 about 6–10 k triangles, LOD1 about 2 k, LOD2 (map scale) about 300.
- At map distance only the plinth, the roof silhouette, the finials and the standing stones need to survive.
- Collision: plinth + walls + roof as 3 boxes.

## Materials (albedo targets, de-lit)

| Material | Base colour | Range | Roughness | Notes |
|---|---|---|---|---|
| Limestone (plinth, steps) | `#a39276` | `#8a7a60`–`#b8a888` | 0.85 | Fitted blocks 0.4–0.6 m long, with thin darker joints `#5e5240` and fine chisel marks |
| Rubble stone and clay (walls) | `#8a6c4c` | `#64472d`–`#a08060` | 0.9 | Irregular stones 0.15–0.35 m, clay bedding `#9a7f5c` |
| Oak (posts, lintel, finials) | `#5b4532` | `#38281a`–`#795b43` | 0.75 | Deep vertical grain, end checks (cracks), weathered silver-brown where the rain hits |
| Reed thatch | `#9a7a4a` | `#6c4f37`–`#b89868` | 0.95 | Grain runs down the slope; rope bands `#6b5236`. Use a darker, mossier tone `#6e6440` on the north slope if a variant is wanted. |
| Saffron wool | `#c88f2e` | `#a06a1c`–`#e0a848` | 0.9 | The **culture colour slot**: tint this to `DM.CULTURE_COLORS` per owner culture. Umari `#e69f00` is the default. |
| Bone (antlers) | `#d9cbb0` | | 0.7 | |

**Note on the generated images:** the generator rendered every material in a warm golden light, so the swatches look browner than these targets. **Use the hex values in this table, not colours picked from the images.** The hero shot shows how they read under the game's golden hour.

## Culture variants

These match the culture styles in `docs/concept/settlements/` (T-001).

**What must stay identical between variants:**
- the plinth: footprint 6.0 × 4.4 × 0.8
- the front steps
- both standing stones and their positions
- the doorway position and clear opening (1.1 × 1.6)
- the door posts, the lintel and the antler skull
- the saffron cloth strips (culture colour slot)
- the altar
- the overall height within ±0.5 m

Together these are the **shrine signature**: a player recognises "shrine" in any culture from the stones, the steps and the antlers.

**What changes:**

| | `shrine-culture-a.png`: Cliff-Terrace | `shrine-culture-b.png`: Lagoon-Weaver | `shrine-culture-c.png`: Ring-Builder |
|---|---|---|---|
| Walls | Smooth rose-ochre lime plaster (renders near-white in this image), target `#e8d6c0`; the plinth is also plastered | Woven reed panels `#7a5c3c` in oak frames | Pale dressed stone in neat courses `#cdb894`; the cella becomes a **round drum** (5.0 diameter) |
| Roof | A low plaster dome `#efe7da` on a square drum, plus a cobalt finial | An upswept **saddle roof** with horns at both ends rising to about 5.5 m, silver shingle `#8f949a` | A steep **slate cone** `#3c4048`, about 55°, with a pale stone ring finial |
| Accent | Cobalt door surround and band `#2f4fa8` | Saffron ribbons from the roof horns | Concentric spiral rings carved on the lintel |

## Prompts

**shrine-hero** (references: `settlements/age-1-bronze.png`, `landmarks/building-shrine.png`)
> Bronze age village shrine, the hero of the shot, inside a palisaded village of thatched timber houses like the reference image, golden-hour low sun from the left with long warm shadows, three-quarter view from about 40 metres away and 25 degrees above. THE SHRINE DESIGN (keep exactly): a square two-step plinth of fitted grey-ochre limestone blocks about 5 m wide; on it a small square cella about 3.6 m wide with walls of rough rubble stone bonded with clay, about 2.2 m high, closed on the back and sides; the front wall has one open doorway framed by two thick dark oak posts and a heavy oak lintel carved with a zigzag band, a pair of deer antlers fixed above the lintel; four stout oak corner posts rise from the plinth corners outside the walls and carry a steep hipped roof of golden-brown reed thatch (about 45 degrees) with a deep overhang, the ridge crowned by a short carved wooden finial; two waist-high standing stones flank the steps in front; a strip of saffron-ochre cloth is tied to each door post; inside the doorway a low flat altar stone with a small fire glow. Villagers small around it for scale, beaten earth yard. Cinematic near-photoreal concept art, crisp detail, physically based lighting. No text, no labels, no watermark.

**shrine-front** (reference: hero)
> Model reference sheet view: the EXACT same shrine building as in the reference image, isolated, shown in a straight-on FRONT ELEVATION, orthographic style with no perspective distortion, camera level with the middle of the building and perfectly centred, on a plain flat light-grey (#c8c8c8) studio background with a soft contact shadow only, flat even diffuse lighting with no strong shadows. Keep every detail identical to the reference: the stepped limestone plinth, the front steps in the centre, the rubble-stone walls, the oak corner posts, the open doorway with the carved zigzag oak lintel, the deer skull with antlers above it, the saffron cloth strips on the door posts, the altar stone visible inside, the hipped reed thatch roof with a short ridge and a round knob finial at each end of the ridge, and the two standing stones flanking the steps. Symmetrical. No villagers, no ground clutter, no landscape. Near-photoreal materials. No text, no labels, no watermark.

**shrine-side** (references: front, hero)
> Model reference sheet view: the EXACT same shrine building as in the reference images (first image is its front elevation, second is a three-quarter view), now shown as a straight-on RIGHT SIDE ELEVATION, orthographic style with no perspective distortion, camera level with the middle of the building, building centred, front of the shrine facing LEFT. Plain flat light-grey (#c8c8c8) studio background, soft contact shadow only, flat even diffuse lighting. Visible from this side: the stepped limestone plinth with the front steps protruding at the left end, one standing stone at the left in front of the steps, the rubble-stone side wall with a single small square window in its middle, the oak corner posts at both ends of the wall, the edge of the carved oak lintel and deer antlers at the left front corner, the hipped reed thatch roof seen from the side with its sloped hip ends and the short ridge with a round knob finial at each end. Same materials, colours and proportions as the reference. No villagers, no landscape. Near-photoreal materials. No text, no labels, no watermark.

**shrine-back** (references: front, hero)
> Model reference sheet view: the EXACT same shrine building as in the reference images (first image is its front elevation, second is a three-quarter view), now shown from directly BEHIND as a straight-on BACK ELEVATION, orthographic style with no perspective distortion, camera level with the middle of the building, perfectly centred and symmetrical. Plain flat light-grey (#c8c8c8) studio background, soft contact shadow only, flat even diffuse lighting. From the back: the stepped limestone plinth with NO steps (the steps are only at the front), a solid rubble-stone back wall with no door and no window, the four oak corner posts with the two rear posts at the wall ends, the hipped reed thatch roof seen from behind with the same overhang and the short ridge with a round knob finial at each end; the tops of the two standing stones may just peek out at the sides. Same materials, colours and proportions as the reference. No villagers, no landscape. Near-photoreal materials. No text, no labels, no watermark.

**shrine-top** (references: front, first top take; second take)
> Architectural PLAN VIEW photograph of the same small shrine as the reference images, camera exactly 90 degrees straight down from high above, true orthographic top view: NO walls are visible at all, NO facade, NO doorway, only what is seen from directly overhead. The image shows: a rectangular hipped reed-thatch roof (wider left-to-right than front-to-back, about 5.6 m by 4.6 m) whose four sloped hips meet a short straight ridge running left-right across the middle, with a small round wooden knob finial seen from above as a circle at each end of the ridge; beneath the eaves, a narrow border of the square fitted limestone plinth shows on all four sides, wider at the bottom edge where a short flight of stone steps (seen from above as parallel stone treads) projects downward in the centre; two rounded standing-stone tops seen from above as small ovals on either side of the steps. Plain flat light-grey (#c8c8c8) background, flat even diffuse light, a faint shadow ring. Same thatch, stone and colours as the reference. No text, no labels, no watermark.

**materials** (reference: front; source image `src/materials-raw.jpg`, with names added in HTML)
> A material swatch board for a 3D modeller: six square close-up material samples arranged in a neat 3 by 2 grid with even gaps on a plain flat dark charcoal (#2a2a2a) background, each square lit with the same flat soft studio light, photographed straight on, all taken from the shrine in the reference image. Top row left to right: (1) fitted grey-ochre limestone plinth blocks with fine chisel marks and thin mortar joints; (2) rough rubble wall stone in warm brown-ochre bonded with tan clay; (3) weathered dark oak timber post with deep grain and cracks. Bottom row left to right: (4) golden-brown reed thatch with bound horizontal rope bands; (5) saffron-ochre handwoven wool cloth with a coarse weave and frayed edge; (6) close-up of the carved oak lintel with a crisp zigzag chevron relief band. Near-photoreal, crisp texture detail. Absolutely no text, no letters, no numbers, no labels, no watermark.

**culture-a / -b / -c** (references: front + `settlements/culture-a|b|c.png`). Each prompt begins:
> Culture variant X of the same village shrine as the first reference image (keep exactly the same footprint: the same rectangular stone plinth, the same central front steps, the same two standing stones flanking them, the same open doorway position and the same overall size and proportions), restyled in the … culture of the second reference:

The rest of each prompt describes the culture:
- **a:** "walls rendered in smooth rose-ochre lime plaster, the oak door frame painted with a deep cobalt-blue door surround, and instead of thatch the roof is a low whitewashed plaster dome on a short square drum with a small cobalt finial, a simple cobalt painted band below the eaves"
- **b:** "the walls become woven reed-and-timber panels between the same four oak corner posts, and the roof becomes a dramatic upswept saddle roof with sweeping curved ridge ends rising high at front and back, covered in silver-grey wooden shingles, with saffron-orange woven ribbons hanging from the ridge ends and the door posts"
- **c:** "the walls become smooth pale cream dressed-stone masonry in neat courses with rounded corners, and the roof becomes a steep dark slate-grey conical-hipped roof of overlapping slate tiles rising to a single point with a small pale stone ring finial, concentric carved rings on the door lintel"

Each prompt ends: "Three-quarter front view from slightly above, the shrine isolated on a plain flat light-grey (#c8c8c8) studio background with a soft contact shadow, warm even light from the left. Near-photoreal. No people, no text, no labels, no watermark."

## Known differences between views (the dimensions table wins)

- **Side view:** the antlers appear as a full deer head rather than a bare skull. **Model the skull, as in the front view.**
- **Hero vs plain views:** in the hero, the plinth reads as two-stepped, while the plain views show one course plus the steps. Use the plain views.
- **Plain views:** they are "orthographic-style" generator images, not true orthographic projections. Treat their proportions as a guide and the dimensions above as the specification.
- **Variants a and c:**
  - **Variant a** plasters the plinth. It keeps the plinth's shape, just in that culture's finish.
  - **Variant c** rounds the cella into a drum. The plinth, steps, stones and door stay the same, as required.
