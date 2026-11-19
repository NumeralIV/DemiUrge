# T-017 · One wonder per age (Grok)

There are seven invented wonders, one for each age. Each is shown from a high oblique view about 1 km away, at golden hour, set in its city of that age. The cities match `docs/concept/settlements/age-*.png`, which were passed to the generator as references. A view from orbit shows that the late wonders still read from about 400 km.

The images are 1280×720 PNGs, 1.4–1.7 MB each, with no text or watermarks. All designs are original: none copies a real-world monument. They are also distinct from my T-002 wonders (the Sunwell and the Stair of Rains).

| File | Age | Wonder | Reads from above as |
|---|---|---|---|
| `wonder-0-stone.png` | Stone | The Hearth of Many Fires | concentric rings of fire round a tall timber cone |
| `wonder-1-bronze.png` | Bronze | The Great Hive | a white beehive dome with a spiral ramp and a gleaming bronze tip |
| `wonder-2-iron.png` | Iron | The Seven Who Keep Watch | seven hooded heads with seven fires on a round hilltop platform |
| `wonder-3-sail.png` | Sail | The Sea Gate | two towers and one long arch across the harbour mouth |
| `wonder-4-industry.png` | Industry | The Choir of Chimneys | a glowing glass dome ringed by twelve chimneys under one steam cloud |
| `wonder-5-atomic.png` | Atomic | The Ear of the Sky | a perfect white circle with a cable triangle over it |
| `wonder-6-space.png` | Space | The Ladder | a white ring with a bright line rising out of frame |
| `wonders-from-orbit.png` | | | three wonders seen from about 400 km (see below) |
| `contact-sheet.jpg` | | | all eight, at the size of a game thumbnail |

Every prompt ends with the same shared suffix:

> *Cinematic near-photoreal concept art for a god game, golden-hour low sun from the left casting long warm shadows, high oblique aerial view from about 1 kilometre away looking down at 40 degrees, the wonder centred as the clear hero of the shot and readable from above by [its signature], with the city and landscape around it for scale, rich natural colours, soft atmospheric haze, crisp detail, physically based lighting. No text, no labels, no signs, no watermark, no UI.*

The prompts below are shown without this suffix. Each one also says "…like the reference image" for its city, and was given that age's settlement image as a reference.

**Where the names fit.** The names are written for the chronicle (`{wonder}` in `docs/content/chronicle-voice.md`, entry 38). They read in sentences such as: "In the 312th year the Umari finished **the Hearth of Many Fires** at Tel Asha. It took three lifetimes…"

**About the hex colours.** The colours listed under each wonder are albedo targets for the modeller. They are not sampled from the images, because golden-hour grading shifts everything warm.

---

## wonder-0-stone.png: the Hearth of Many Fires

**What it is for.** A camp people's gift of warmth. Every family keeps one fire burning on the god's bank, so the god is never cold or alone in the dark. It fits a culture that sees the god as a giver of fire and light. It also suits a god that throws fire: the people answer fire with fire, tended and tame.

**Prompt:**
> Original invented wonder of a Stone Age people, the Hearth of Many Fires: a vast spiral earthwork, about 180 metres across, a low grassy bank of piled earth and river stones winding inward in three turns like a coiled serpent, with hundreds of small fire pits burning along the crest of the spiral, thin smoke rising, and at the centre a tall cone of stacked timber poles about 25 metres high hung with hides and antlers. Around it a Stone Age camp of round domed hide-and-thatch huts, drying racks, worn footpaths, a river bend and wild oak and pine forest. Tiny figures carrying firewood along the spiral.

**Dimensions**
- Footprint: about 160 m across.
- The generator drew two concentric banks rather than a true spiral, which reads better at distance.
- Banks: 1.5–2 m high and about 8 m wide at the base.
- Fire pits: about 1 m, every 4–5 m along each crest (about 250 in all).
- Central cone: 25 m tall, 10 m across at the base, topped with an antler crown.

**Notes for the modeller**
- **Silhouette:** flat rings with one spike. At game distance it is "rings of orange dots plus a dark cone". The cone is the only vertical element in the camp.
- **Materials:**
  - earth bank `#5a4630`
  - river stones `#8a8070`
  - grass `#5f7a3a`
  - timber poles `#4a3624`
  - hides `#9a7550`
  - bone and antler `#d8cbb0`
- **At night:** the rings become two circles of firelight `#ff8c38` (about 1,900 K) with flicker. It is the brightest thing on a Stone-age night side, so use it as a light marker in the T-016 style.

## wonder-1-bronze.png: the Great Hive

**What it is for.** A granary for the god: the first fruits of every harvest are carried up the ramp and sealed inside. It is the village's promise that no one will starve while the god is fed. It suits a culture whose god is the Green and Fruitful one, and a famine-prone river valley.

**Prompt:**
> Original invented wonder of a Bronze Age people, the Great Hive: a colossal beehive-shaped granary dome of white-plastered mud brick, about 70 metres wide and 55 metres tall, with a spiral ramp winding up its outside to the top, the very top capped with a gleaming polished bronze cone that catches the sun, small dark doorways along the ramp, rows of ochre storage jars at its foot. It stands in the middle of a Bronze Age village: rectangular timber and mud-brick houses with thatch inside an oval timber palisade, beehive granaries, rectangular grain fields outside, a river with dugout boats. Tiny figures carrying baskets up the ramp.

**Dimensions**
- Base: 45 m across. The render reads taller and slimmer than the prompt, and that is the version to model.
- Height: 55 m, including a bronze cap 12 m tall.
- Ramp: 3 m wide, about 3 turns.
- Doorways: 1.2 × 2 m.

**Notes for the modeller**
- **Silhouette:** a tall white cone with stepped bands (the ramp) and a metal tip. It is the tallest thing for many kilometres in the Bronze age.
- **Materials:**
  - lime plaster `#e8e0cf`
  - mud brick showing through `#b8956a`
  - polished bronze `#b08440` (metallic, roughness 0.25)
  - storage jars `#b5683a`
  - palisade timber `#6b5236`
- **At night:** oil lamps glow in the doorways along the ramp `#ffad52`, forming a dotted spiral. The bronze tip picks up moonlight.

## wonder-2-iron.png: the Seven Who Keep Watch

**What it is for.** Guardians set around the town's holy hill, facing every way, so that whatever the god sends finds the people awake. It is half a prayer and half a vigil: their fire bowls are never allowed to go out. It suits a culture that fears the god (love < 0) or has lived through raids. The walls below say what they fear.

**Prompt (the second take was used):**
> Original invented wonder of an Iron Age people, the Seven Who Keep Watch, seen from high above: on the flat top of a grassy hill outside and beside a walled town, a round grey stone platform about 90 metres across, and standing on its edge in a complete closed circle, evenly spaced all the way around like the numbers on a clock, seven colossal hooded stone figures each about 35 metres tall, every figure facing outward away from the centre with its back to the empty round court in the middle, each figure holding an iron fire bowl with a burning flame. Below the hill, a smaller Iron Age walled town: grey stone curtain wall with square towers and a gatehouse, stone and timber houses with thatch, forge smoke, a timber bridge over a river, hedged fields. [The shared suffix used 50 degrees here.]

**Dimensions**
- Platform: 90 m across and 4 m high, with a dry-stone revetment.
- Figures: 35 m tall and about 9 m across at the base, evenly spaced at 51.4° on the platform rim.
- Fire bowls: iron, 4 m across, held at chest height (about 22 m up).

**Notes for the modeller**
- **Silhouette:** a crown of seven hooded points on a hill above the town.
- **The figures must have no faces**, only deep hood shadow, because the god in DEMIURGE is never given a face.
- **Materials:**
  - weathered grey granite `#7d7a72`, with lichen `#8f8c5a` on the shoulders
  - platform paving `#9a8d78`
  - iron bowls `#3a3530` with rust `#7a4a2a`
- **At night:** seven fires `#ff9a40` on a dark hill. They are the town's landmark at night and read well from orbit as a tiny ring of seven points.

## wonder-3-sail.png: the Sea Gate

**What it is for.** A gate the god's sea must pass through: a promise to the sea that ships will always come back, and a lantern so they can. It belongs to a harbour culture that has lost ships to storms or waves. It suits a culture whose epithet for the god is about drowning.

**Prompt:**
> Original invented wonder of a Sail Age people, the Sea Gate: two tall tapering towers of pale limestone standing on rocky islets on either side of a harbour mouth, about 90 metres tall, joined near their tops by a single high slender stone arch spanning 200 metres of water, a large bronze-domed lantern pavilion sitting at the crown of the arch with a fire glowing inside, three-masted sailing ships with white sails passing beneath the arch into the harbour. Behind it a Sail Age harbour city: dense plastered houses with terracotta tile roofs on a regular street pattern, a domed temple with a portico, stone quays, a windmill, vineyards.

**Dimensions**
- Towers: 70 m tall and 14 m square at the base, tapering, on rubble islets.
- Arch: spans 160 m, with its soffit 45 m above the water (masts clear it) and its crown at about 60 m.
- Lantern pavilion: 12 m across, with a ribbed bronze dome.

**Notes for the modeller**
- **Silhouette:** an "Π" with a round arch across the water. From above it is one thin bar across a gap in the coast, the only wonder that sits over water.
- **Materials:**
  - limestone `#d9cdb4`
  - arch stone in the same colour, with darker voussoir joints `#a8997e`
  - bronze dome `#8a6a3a` with verdigris `#5f8f80`
  - harbour water `#2f5f78`
- **At night:** the lantern burns `#ffb050` (a fire behind glass) and lays a reflection track on the water. It is the harbour's lighthouse.

## wonder-4-industry.png: the Choir of Chimneys

**What it is for.** The factory city's hymn. On feast days the twelve chimneys are blown together and the whole city hears them sing. It is the proof that a people who no longer need to pray still want to be heard. Under its glass is a hall where everyone fits at once. It suits a pious culture in an age when devotion is falling (SPEC-SIM §3.8 baselines).

**Prompt:**
> Original invented wonder of an Industry Age people, the Choir of Chimneys: a vast round hall with a glass-and-cast-iron dome about 120 metres across, glowing warmly from inside, ringed by twelve very tall slender red-brick chimneys of different heights arranged in a circle like the pipes of an organ, each chimney topped with a polished brass cap and each releasing a white plume of steam, the steam plumes leaning together in the wind into one cloud. Around it an Industry Age city: brick terraces with slate roofs, long brick mills, rail lines, an iron truss bridge, docks with steamships.

**Dimensions**
- Dome: 100 m across, crown 40 m high, on a brick drum 15 m high.
- Chimneys: 12, from 60 to 95 m tall and 5 m across, set on a 150 m ring.
- Steam: a merged plume that rises 300–600 m on still days.

**Notes for the modeller**
- **Silhouette:** a glass dome inside a crown of organ-pipe chimneys. The steam cloud is the long-distance marker; it shows in the orbit view.
- **Materials:**
  - red brick `#8a4632`
  - cast iron `#2e2e30`
  - glass `#9fb3b8` (transmissive, warm interior `#ffcf88`)
  - brass caps `#c49a4a`
  - steam `#f0ece6`
- **At night:** the dome glows like a lantern `#ffc766` (sodium-era warmth). The steam is lit from below and orange, so it stands out over the T-016 Industry lights.

## wonder-5-atomic.png: the Ear of the Sky

**What it is for.** When the people stopped hearing the god, they built an ear big enough to listen for it. It is half observatory and half chapel: the receiver is aimed straight up, where the god was always said to be. It suits a defiant or "forgotten"-mood culture in the Atomic age that still wants an answer.

**Prompt:**
> Original invented wonder of an Atomic Age people, the Ear of the Sky: an enormous smooth white concrete bowl about 300 metres across set into a hill just outside the city, facing straight up at the sky, with three tall slender white concrete pylons around its rim holding a triangular receiver platform suspended by cables high above the centre of the bowl, a faint red warning light on each pylon top, a straight tree-lined road leading to a low building at its edge. Beyond it an Atomic Age metropolis: concrete and glass towers, slab blocks, curving multi-lane highways, a container port, suburbs on the hills, two cooling towers on the far bank.

**Dimensions**
- Bowl: 250 m across, rim wall about 25 m high (the render shows it standing proud of the hill), dish depth 40 m.
- Pylons: three, 110 m tall.
- Receiver: a triangular truss 40 m on a side, 80 m above the bowl floor.

**Notes for the modeller**
- **Silhouette:** a perfect white circle, the largest clean geometric shape in the game. It reads from orbit as a chalk dot.
- **Materials:**
  - white concrete `#e6e4de`
  - bowl panels in a slightly cooler tone `#d6dadc`
  - steel cables and truss `#5a5e62`
  - red aviation lights `#ff3a2a`
- **At night:** three blinking red lights (1 s on, 1 s off, staggered). The bowl stays dark, a black disc in a lit city. That inversion is the point.

## wonder-6-space.png: the Ladder

**What it is for.** A stair to where the god lives. It is the people's last gift: they no longer ask the god to come down, they climb up. It is a precursor to the Ark (SPEC-SIM: the Ark and the endings) and fits the "parting" mood.

**Prompt:**
> Original invented wonder of a Space Age people, the Ladder: a single impossibly thin glowing white tether rising perfectly straight from the ground up into the sky until it vanishes above the clouds, anchored at the centre of a huge round white and pale-gold ring-shaped platform about 400 metres across standing in a lake, the platform with terraced gardens and solar glass, small capsule climbers visible partway up the tether catching the sun, a faint line of light running up the tether. Around it a Space Age capital: slender white and pale-gold towers, terraced garden buildings with solar glass, light rail, wind turbines on the ridge, a river.

**Dimensions**
- Ring building: 400 m across, 60 m wide and 25 m high, with garden roofs and solar glass. The render set it in a park ringed by highways, not in a lake; that is fine.
- Anchor plaza: 120 m across.
- Tether: 2 m thick, rising out of sight (a game asset can fade it out at about 3 km).
- Climbers: 10 m capsules.

**Notes for the modeller**
- **Silhouette:** a ring plus a vertical line. It is the only wonder that breaks the top of the frame, and from orbit it is a line standing off the planet.
- **Materials:**
  - white composite `#f2f0ea`
  - pale gold trim `#d9c08a`
  - solar glass `#2c3f5a`
  - garden roofs `#5f7f45`
  - tether core `#fffaf0` (emissive)
- **At night:** the tether glows faint white `#e8f0ff` with climbers as moving points. The ring is a circle of warm-white `#fff0d1` lights. From orbit on the night side it is a hair-thin line of light, the one artificial line T-016 would allow.

---

## wonders-from-orbit.png: the late wonders from about 400 km

**Prompt:**
> Photoreal view of an Earth-like planet from low orbit about 400 kilometres up, looking down at an angle across a green and brown continent with a coastline, rivers and scattered white clouds, the curved horizon and thin blue atmosphere at the top of the frame against black space, late afternoon golden light from the left. Three invented wonders are readable as distinctive marks: (1) near the centre, a single hair-thin perfectly straight bright white line rising from a tiny white ring beside a river city and climbing up out of the atmosphere into space, glinting in the sun, casting a long thin shadow line across the land; (2) on the coast to the lower left, a tiny brilliant white circle like a dot of chalk beside a grey city patch; (3) to the right, inside an industrial city, a long white steam plume trailing downwind. Cities appear as grey-brown patches with faint road lines. Cinematic near-photoreal space photography style, crisp detail, soft atmospheric haze toward the horizon, physically based lighting. No text, no labels, no arrows, no circles drawn around anything, no watermark, no UI.

(`state-healthy.png` from T-013 was passed as a reference for the palette.)

**What reads, and how:**
- **The Ladder** reads by its **light and shape**: a straight bright line standing off the surface.
- **The Choir of Chimneys** reads by its **plume**: a steam cloud far larger than the building.
- **The Ear of the Sky** reads only weakly, as a pale mark with a long shadow in the lower-left city.

**Rule of thumb for the game camera.** At orbit zoom a wonder needs one of:
- an emissive or specular line or point
- a plume or column at least 10× its footprint
- a geometric white shape at least 3 px

Earlier wonders (Hearth, Seven) only read on the night side, as tiny rings of fire.

## Known misses

- **Hearth of Many Fires:** concentric rings, not the three-turn spiral in the prompt. I kept it because it is cleaner at distance. The dimensions above describe what is drawn.
- **Seven Who Keep Watch:** the figures still turn toward the camera, not strictly outward, and the platform is large compared with the town. The first take was worse (a row, not a ring), so the second take is used.
- **Ear of the Sky:** the rim is a tall wall, so it reads partly as a stadium or tank from this angle.
- **The Ladder:** it stands in a park, not a lake.
- **Orbit view:** this is a separate, low-orbit shot of a continent, not the T-013 globe framing, because a 400 km view shows a region, not the whole disc. The Ear of the Sky's mark is faint, so two wonders are clearly readable and the third only weakly.
