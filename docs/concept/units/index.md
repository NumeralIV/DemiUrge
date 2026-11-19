# T-011 · Units and wildlife on the move

Concept art for everything that moves on the map: the four unit types (settler, trader, army, prophet; SPEC-SIM §1), their ship forms on sea, the decorative life listed in SPEC-RENDER §9, and a silhouette sheet at real game scale.

- **View:** high oblique, about 300 m to 2 km up, golden hour with the sun low from the left, as in `docs/concept/settlements/`.
- **How the images were made:** each age image was generated **from the matching T-001 settlement image** (`age-1-bronze`, `age-3-sail`, `age-5-atomic`, `age-6-space`), so the valley, culture styles and light carry over. The wildlife images were generated from the matching T-008 biome plates.
- **Format:** every image is 1280×720 PNG with no text or watermarks, except the silhouette sheet, which is a labelled reference board.

| File | Shows |
|---|---|
| `units-ancient.png` | Stone and Bronze: settler band, donkey traders, warband, river boats |
| `units-classical.png` | Iron and Sail: caravan, marching army, galley and sailing ships, prophet |
| `units-modern.png` | Industry and Atomic: trucks, freight train, freighter and steamship, aircraft |
| `units-space.png` | Space: launch, ark in its gantry, maglev, aerial transports |
| `wildlife-herds.png` | Savanna migration herd |
| `wildlife-birds.png` | Murmuration over the sea, wader V over the delta |
| `wildlife-sea.png` | Whale mother and calf, fish ball |
| `units-silhouettes.png` | Every unit drawn 1:1 at game scale on our terrain, plus a culture-colour check |
| `contact-sheet.jpg` | All of the above at a glance |
| `src/` | Code and terrain crops for the silhouette sheet (see the last section) |

## Shared rules for the 3D stand-ins

1. **A unit is a group, not a person.** At game scale a "warband" is a dark block 30–50 px wide; a single figure is 3–4 px and reads as noise. Model each unit as a tight cluster of 5–20 low-poly figures or vehicles on one shared base.
2. **Dark body, warm rim.** The body is near-black brown `#17130e`, and the sun-side rim is warm cream `rgba(255,240,205,.55)`. The contact shadow is `rgba(0,0,0,.38)`, offset down-right (sun from the left). This reads on every ground we have except deep water, where hulls go light instead (see ships).
3. **Culture colour lives in three places:** a pennant or banner (the largest coloured area), the loads or sails (traders and ships), and a **2 px culture-coloured ground ring** under the group. The silhouette sheet shows that at far zoom (about 16 px) the pennant alone is not enough, so the ring is essential.
4. **Colours** are `DM.CULTURE_COLORS`: `#e69f00`, `#56b4e9`, `#009e73`, `#f0e442`, `#d55e00`, `#cc79a7`. Known weak pairs, from the sheet:
   - yellow `#f0e442` on savanna and dunes
   - orange `#e69f00` and vermilion `#d55e00` on dunes
   - green `#009e73` on deep sea and shallows
   - sky blue `#56b4e9` on shallows

   For those pairs, rely on the ring plus the dark silhouette, or add a thin dark outline to the coloured element.
5. **Each type has its own shape at every age,** so the type reads before the colour does:

   | Type | Shape |
   |---|---|
   | Settlers | a column with one cart, wagon or bus, plus a small pennant |
   | Traders | a line of identical bright loads (bundles, camel packs, containers) |
   | Army | a dense rectangle with tall banners above it; it is the only type with vertical spikes |
   | Prophet | one white figure with a soft gold halo `#ffd98a`, about 8 px, leading a short line of followers. It is the same at every age, deliberately: the prophet is timeless. |

6. **Scale on screen at mid zoom** (zoom01 ≈ 0.5): 20–60 px long, 10–20 px tall. Below zoom01 0.3 SPEC-RENDER draws no objects, so units should switch to a 6–8 px culture-coloured chevron there. The space ark is the one exception: it is a landmark (about 66 px tall).
7. **Age progression in one line:** muscle → animal → wind → steam → diesel → flight → rocket. The silhouette goes from a ragged line of figures to clean geometric blocks, and from dark to light (space-age craft are white and gold).

## units-ancient.png

**Shows:** the Bronze-age palisade village from T-001, small in the background, with the camera moved upriver.
- A settler band of about twenty people with goats and an ox cart.
- Four pack donkeys with saffron-orange bundles.
- A spear warband in a dark block under one tall orange pennant, near the ford.
- Long wooden boats on the river.

**Prompt (from `age-1-bronze.png`):**
> Same river valley, same golden-hour light from the left and same art style as the reference image, but the camera has moved upriver: the Bronze age village sits small in the far background. In the foreground and middle ground, show (1) a settler band of about twenty people on foot carrying bundles, with a few goats and a cart pulled by two oxen, walking along a dirt track; (2) a small trader caravan of four pack donkeys laden with bright saffron-orange bundles; (3) a warband of about thirty spearmen in a tight dark block with round hide shields, one tall orange pennant, crossing a ford; (4) on the river, three long reed boats with upturned ends. High oblique view about 300 m up. Cinematic near-photoreal concept art for a god game. No text, no watermark.

**Notes:**
- Settler: about 40 px. The cart is the anchor shape; keep it.
- Trader: the orange loads (`#e69f00`, Umari culture) carry the culture colour.
- Army: the pennant should be taller than in the image, about 1.5 times the block height.
- Boats: the generator drew **plank boats with masts, not reed boats**. For the stand-in, use the sheet's version instead: a crescent hull in reed tan `#b08a4a` with upturned ends and no mast.

## units-classical.png

**Shows:** the Sail-age walled, domed city from T-001.
- A caravan of camels and covered wagons on a pale road.
- A marching army block with sky-blue banners (`#56b4e9`, Vesh culture) and horsemen.
- A war galley and three-masted ships in the bay.
- A robed prophet leading a line of followers on the far road (right of frame).

**Prompt (from `age-3-sail.png`):**
> Same coast and walled city, same golden-hour light and art style as the reference image, camera about 700 m up and pulled back so the roads and bay are visible. Show (1) a caravan of covered wagons and pack camels on a pale paved road; (2) a marching army of about 200 soldiers in a disciplined block with tall blue banners and a few horsemen at the flank; (3) in the bay, a long war galley with oars and two three-masted sailing ships; (4) a lone robed prophet in white leading about 40 followers along a side road. Cinematic near-photoreal concept art for a god game. No text, no watermark.

**Notes:**
- The army formation is the clearest silhouette in the set: a parallelogram block with three banners rising above it.
- The prophet in the image is in earth tones, not white. **Use the sheet version** (white robe plus halo); it is the only way it reads at 8 px.
- Ships: cream sails `#efe6cf` are the brightest thing on water, so keep them oversized, about 1.3 times real scale. Put culture colour on a hull stripe and the masthead pennant.

## units-modern.png

**Shows:** the Atomic-age city from T-001.
- A long column of olive trucks on the highway.
- A freight train crossing a steel truss bridge.
- A container freighter and a steamship with a smoke trail on the river.
- Propeller aircraft overhead, with cooling towers behind.

**Prompt (from `age-5-atomic.png`):**
> Same city and river, same golden-hour light and art style as the reference image, camera about 1.5 km up. Show (1) a long freight train crossing a steel truss bridge; (2) a column of about 20 olive-drab military trucks on the highway; (3) on the river, a container freighter and a single-funnel steamship trailing smoke; (4) in the sky, two propeller cargo aircraft and a jet airliner with a contrail. Cinematic near-photoreal concept art for a god game. No text, no watermark.

**Notes:**
- The aircraft are **drawn too large and look like WWII aircraft**. At game scale an aircraft is about 20 px with a ground shadow offset 20–30 px. **The shadow is what sells altitude**; see the sheet.
- The truck column (army) is the weakest silhouette on grass. Give it a culture-coloured lead vehicle or a pennant, plus the ring.
- Train and container colours: muted reds, blues and ochres, with the culture colour on one wagon band.

## units-space.png

**Shows:** the Space-age city from T-001.
- A rocket rising on a white-gold plume.
- The ark: a tall white-and-gold hull inside a gantry.
- A white maglev on an elevated track.
- Small flying transports.

**Prompt (from `age-6-space.png`):**
> Same city, same golden-hour light and art style as the reference image, camera about 2 km up. Show (1) a rocket rising on a white-gold flame; (2) the ark under construction: a huge white-and-gold hull standing in a gantry with scaffolding; (3) a white maglev train on an elevated track; (4) two slim aerial transports with glowing blue engine pods flying low; (5) a convoy of white crawler transporters with cylindrical segments. Cinematic near-photoreal concept art for a god game. No text, no watermark.

**Notes:**
- Space units are **light on dark** (white `#eceae4`, pale gold, blue engine glow `#8fd0ff`), which inverts the earlier ages. That inversion is itself the age signal.
- The crawler convoy did not appear in the image; the generator added an odd hover vehicle at left instead.
- The sheet's space army is a dark armoured hover craft with a culture fin, so the army keeps its dark, menacing read.

## wildlife-herds.png

**Shows:** a savanna migration of a few hundred wildebeest-like animals raising dust along a gallery forest, with kopjes on the horizon (from `nature/biome-savanna.png`).

**Prompt:**
> Same savanna, same golden-hour light and art style as the reference image, camera about 500 m up: a great migration herd of about 300 wildebeest-like animals streaming across the plain in a long curving column, raising warm dust, alongside a gallery forest. Cinematic near-photoreal. No text, no watermark.

**Notes:**
- Decorative life only, with no culture colour.
- Render it as a dust-lit **stream**: a dark ribbon of 2–3 px dots with a warm dust haze `#d9b27a` at 30 % on the sun side.
- Deer (SPEC-RENDER §9) are the temperate version: 3–6 animals, about 6 px each.

## wildlife-birds.png

**Shows:** a huge starling murmuration over the sea at sunset, and a loose V of white wading birds low over the delta (from `nature/biome-wetland-delta.png`).

**Prompt:**
> Same delta, same golden-hour light and art style as the reference image, camera about 400 m up: a vast murmuration of starlings swirling over the water toward the sun, and a loose V of large white wading birds flying low over the channels. Cinematic near-photoreal. No text, no watermark.

**Notes:**
- Two bird modes:
  - **Flock:** 40–80 dark points with a boids-style swirl, about 2 px each.
  - **V:** 5–9 white birds, about 4 px each, with a slow wingbeat.
- White waders are the ones that read on land; dark flocks read against bright sky and water.

## wildlife-sea.png

**Shows:** a whale mother and calf with a spout, and a huge silver fish ball, over a turquoise reef coast (from `nature/biome-coral-coast.png`).

**Prompt:**
> Same coral coast, same light and art style as the reference image, camera about 300 m up looking nearly straight down: a humpback-like whale mother and calf surfacing with a spout, and a large silver shoal of fish swirling in a ball nearby. Cinematic near-photoreal. No text, no watermark.

**Notes:**
- **Scale is exaggerated** in the image, and the fish ball is enormous.
- In game, a whale is about 40 px at mid zoom. It is dark `#22303a` with a white wake and a spout puff (0.5 s, every 6–10 s).
- The shoal is a 30–50 px shimmering silver disc. SPEC-RENDER asks for only "a few whales", so keep these rare.

## units-silhouettes.png

**How it was made.** The sheet is **drawn in code, not generated**, because the generator cannot hold an exact pixel scale.
- `src/sil.js` defines every unit as hand-built SVG at 1:1 pixels, using the rules above.
- `src/units-silhouettes.html` lays out the grid over real terrain crops cut from our merged concept art, stored in `src/bg/`:

  | Crop | Source |
  |---|---|
  | `grass.jpg` | temperate-broadleaf meadow |
  | `savanna.jpg` | savanna |
  | `sand.jpg` | desert dunes |
  | `sea.jpg` and `shallows.jpg` | coral coast |

- To rebuild it: `cd src && node render.js units-silhouettes` (Chrome 1920×1080, same script as `docs/concept/ui/src`).

**What it shows:**
- **Rows are ages,** each in its culture colour:

  | Row | Culture colour |
  |---|---|
  | Stone·Bronze | `#e69f00` |
  | Iron·Sail | `#56b4e9` |
  | Industry·Atomic | `#009e73` |
  | Space | `#cc79a7` |

  A final row holds wildlife.
- **Columns are:** settlers, traders, army, prophet, at sea, and air or other.
- **The right panel** puts all six culture colours on five grounds at far zoom.

Each label gives the unit's on-screen height or length in px.

**Findings:**
- Every type is distinguishable at its age, and the age progression reads across each column.
- The weakest cells are the truck column and the freight train on grass: dark on mid-dark. The ring and a coloured lead vehicle fix this.

## Concerns

- `units-ancient.png` shows plank boats rather than reed boats. `units-classical.png` shows an earth-toned prophet. Use the silhouette sheet for both.
- Aircraft in `units-modern.png` and creatures in `wildlife-sea.png` are larger than game scale, for drama.
- The crawler convoy requested for `units-space.png` is missing.
- The image backgrounds re-imagine the T-001 towns from a new angle, so building details drift somewhat; the culture styles and palette hold.
