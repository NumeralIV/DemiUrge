# T-015 · Shrine reference: construction, damage and ruin states (Grok)

These are five states of the T-014 village shrine (`docs/concept/assets/shrine/`), plus an intact base frame for comparison. They follow `docs/research/assets.md` §4, step 5 (construction and ruin states on the same footprint). They cover the damage causes in SPEC-SIM §3.10: quake, fire (lightning, war), flood (rising sea), and abandonment after the silence ending.

**All six images:**
- 1024×768 PNG, about 1 MB each, with no text or watermarks.
- The same three-quarter front view from slightly above, the same plain `#c8c8c8` background and the same warm light from the left as the T-014 culture variants.

| File | State | Game trigger (suggested) |
|---|---|---|
| `shrine-intact.png` | Base: the T-014 design in this view (reference only) | normal |
| `shrine-construction.png` | Being built | `b.shrine` just set, for the first ~3 years |
| `shrine-damaged.png` | After an earthquake | `quake` within range, or a battle |
| `shrine-burnt.png` | After a fire | lightning fire, war capture, or a volcano |
| `shrine-ruin.png` | Centuries abandoned | settlement destroyed or abandoned for more than 100 years; silence ending |
| `shrine-drowned.png` | Half under a risen sea | the cell's elevation is below `seaLevel` by 0–1.5 units |

**How they were made.**
1. `shrine-intact` was generated from the merged `shrine-front.png` plus `shrine-hero.png`.
2. **Each state was generated from `shrine-intact` only**, with "same camera angle, same framing, same size in frame", so the plinth, steps and standing stones stay in place.
3. **Retakes:**
   - **Construction:** the first take already had the cloth on the door posts.
   - **Burnt:** two retakes. The generator kept building the stone walls up to where the roof had been, making the shrine about 1.4× too tall. The final prompt pins the wall top to "the base of the antler skull".
   - **Drowned:** the first take had the water only at plinth level and a fire still burning inside.
4. **Checks:** `contact-sheet.jpg` shows all six. On the build box, `/workspace/T-015-game-distance-check.png` shows each state at about 64 px tall, which is town-zoom scale; every state reads by its silhouette.

## Part-by-part changes against the T-014 part list

The parts are the ones listed in `../index.md`: `plinth, steps, walls, posts_corner ×4, posts_door ×2, lintel, roof, finials ×2, antlers, cloth ×2, standing_stones ×2, altar`.

**These never change in any state:**
- the plinth footprint (6.0 × 4.4 × 0.8 m)
- the step position
- the two standing-stone positions
- the pivot

Every state is made by **editing the base model**, never by rebuilding it.

### shrine-construction.png
| Part | Change |
|---|---|
| plinth, steps, standing_stones | Finished and unchanged. The stone is cleaner and paler than when weathered: limestone `#b2a284`. |
| walls | Built only to **1.1–1.4 m** (55–70 % of 2.0 m), with stepped, uneven top courses. Make a cut of the wall mesh along a noisy horizontal line. The door opening and the lower part of the window are already formed. |
| posts_corner, posts_door, lintel | Full height. Fresh oak is lighter, `#7a5c40`. |
| roof | **Frame only:** 4 eave plates, 4 hip rafters, about 9 common rafters per long slope and 5 per short slope (round poles 0.10 m in diameter), 3 rows of purlins, and every joint lashed with rope (`#6b5236`, small torus wraps). No thatch. |
| finials | Already on the ridge ends. |
| antlers, cloth, altar fire | **Absent.** The shrine is not yet consecrated. The altar block is in place without a fire. |
| **Extra props** | Pole scaffold on the right side (verticals 0.08 m in diameter, 2 lifts, lashed); a ladder 3.2 m long; 2–3 reed bundles (cylinders 1.2 × 0.3 m) on the plinth; one basket of stones. |

### shrine-damaged.png (quake)
| Part | Change |
|---|---|
| plinth | One crack running from the steps through the front course (decal plus a split in the mesh); one block displaced 5 cm. |
| steps | The crack continues up the middle of the steps. |
| walls | The **front-right corner has collapsed**: remove about 35 % of the right side wall and the right end of the front wall, from the eave down to about 0.6 m, in a jagged break. Diagonal cracks run across the rest of the walls (decals). |
| posts_corner | The front-right post has **fallen** and leans at about 30° against the rubble. The others stand. |
| roof | The right hip has slumped: drop the right third of the roof mesh by 0.4–0.6 m, tear the thatch to expose 3–4 rafters, and make the eave ragged. |
| finials | Both still on (in this image). Optionally the right finial lies on the plinth. |
| antlers, cloth, lintel | Unchanged; the lintel tilts at most 2°. |
| **Extra props** | A rubble pile of 25–40 stones (scaled rocks reused from the wall kit) in a cone 1.5 m across, against the right side, spilling off the plinth onto the ground. Grey dust tint `#b8ab96` at 30 %. |

### shrine-burnt.png (fire)
| Part | Change |
|---|---|
| plinth, steps, standing_stones | Intact, with soot: multiply by `#6b5a48` on the top surfaces near the door. |
| walls | Same height as the base (2.0 m) and **open at the top**. Black soot fans `#1e1915` rise above the door and the window, plus soot on the top 0.4 m all round. |
| posts_corner, posts_door, lintel | Same positions and heights, but **charred**: albedo `#1f1a16` with cracked "alligator" char in the normal map, about 15 % thinner, and embers `#ff6a2a` in the cracks for the first year only. |
| roof | **Gone.** Replace it with 3–5 charred beams (0.12 m, `#1f1a16`) lying diagonally across the wall tops. |
| finials, cloth | Gone. |
| antlers | Blackened, `#3a2f28`, and now sitting directly on the wall top. |
| altar | Buried in ash and charcoal (`#7d7a74` with `#2a2522` chunks), with a thin smoke wisp particle for the first ~2 years. |

### shrine-ruin.png (centuries later)
| Part | Change |
|---|---|
| plinth | Present, with **rounded, worn edges**. Moss `#5f6b2e` and grass fill every joint; partly buried by about 0.1 m of soil and grass on its top. |
| steps | Worn and dished in the middle; grass grows in the risers. |
| walls | **Broken stumps** 0.6–1.5 m high (30–75 %), highest at the back corners and lowest at the front, so the doorway is no longer a clean gap. Tumbled stones lie on the plinth. Lichen `#8f9468` and moss on the north faces. |
| posts, lintel, roof, finials, cloth, antlers, altar fire | **All gone** (rotted). The altar block survives under the grass. |
| standing_stones | **Unchanged in position.** Grey with lichen, `#8a8a7a`; they outlast everything. |
| **Extra props** | Grass and fern cards inside; one young **birch**, about 4.5 m tall, growing from the centre of the cella (the silhouette signal "ruin" at map scale); 6–10 loose stones on the ground around the plinth. |

### shrine-drowned.png (risen sea)
| Part | Change |
|---|---|
| Water | The water plane sits **1.0 m above the plinth top** (1.8 m above ground). The water is the game's shallow-water material, clear green-grey `#6f8a80`, with the submerged parts visible through it. |
| plinth, steps | Fully submerged and darkened, with weed `#3d4a2a`. |
| standing_stones | Submerged; only the top 0.25 m shows above the water, with weed. |
| walls | The lower half is under water. Just above the waterline there is a **weed and algae band** 0.3 m high (`#4a5a2a`, with streaks hanging down) and a pale tide line `#b8b29a`. |
| posts, lintel | Wet and dark (`#2e2620`), with weed strands hanging from the lintel and the antlers. |
| roof | **Sodden:** thatch darkened to `#5a4a3a`, mottled with rot holes (4–8 dark patches), sagging slightly. |
| cloth, altar fire | Gone; the interior is dark water. |
| finials, antlers | Still in place. |
| **Extra props** | Floating reed debris and weed patches on the water plane. |

## Prompts

**shrine-intact** (references: `../shrine-front.png`, `../shrine-hero.png`)
> The EXACT same Bronze age shrine as the reference images (first is its front elevation, second its three-quarter village view): stepped fitted limestone plinth, central front steps, two rounded standing stones flanking the steps, rubble-stone walls with four oak corner posts with cap blocks, open doorway with two oak door posts and a carved zigzag oak lintel, deer skull with antlers above the lintel, saffron cloth strips on the door posts, altar stone with a small fire inside, hipped golden-brown reed thatch roof with rope bands and a round knob finial at each end of the short ridge, small square window in the right side wall. Shown isolated in a three-quarter front view from slightly above (front facing the viewer and turned about 30 degrees to the left so the right side wall with its window is visible), on a plain flat light-grey (#c8c8c8) studio background with a soft contact shadow, warm even light from the left. Near-photoreal. No people, no text, no labels, no watermark.

The five states all use `shrine-intact` as the only reference. Each begins: "The same shrine as the reference image, same camera angle, same framing, same size in frame, same plain flat light-grey (#c8c8c8) background and same warm even light from the left, but …". Each ends: "Near-photoreal. No people, no text, no labels, no watermark."

**construction** (second take):
> … shown UNDER CONSTRUCTION: the full fitted limestone plinth and the front steps are finished, the two standing stones already stand in front, all four oak corner posts and the two bare oak door posts with the carved lintel are up, the rubble-stone walls are only half built (about chest height, uneven top courses, loose stones and a basket of stones on the plinth), lashed wooden pole scaffolding with a ladder along the right side wall, and above it the bare roof frame of the hipped roof with a short ridge: pale timber rafters lashed with rope, no thatch at all, bundles of reeds stacked on the plinth. The door posts are BARE wood with NO cloth, NO antlers, NO skull, NO fire.

**damaged:**
> … DAMAGED BY AN EARTHQUAKE: the plinth intact but with a long crack and one displaced block, deep diagonal cracks through the rubble-stone walls, the upper right corner of the front wall collapsed into a pile of rubble on the plinth, the front-right oak corner post fallen and leaning diagonally against the wall, the reed thatch roof sagging and slipped at the right corner exposing a few rafters, one ridge finial fallen, the lintel tilted slightly, one standing stone leaning, the antlers still in place. Dust on everything. No fire.

The generator kept both finials and both stones upright. The table above treats a fallen finial as optional and keeps the stones upright: the standing stones never move.

**burnt** (third take):
> … AFTER A FIRE, seen as a LOW, OPEN, ROOFLESS box. IMPORTANT: the stone walls END exactly at the old eave line, which is level with the base of the antler skull just above the carved lintel; there is NO stone above that line, nothing taller than the antler tips except two or three short charred post stumps. The thatch roof has burnt away completely, so we look over the top of the walls into the open blackened interior full of ash and a few fallen charred rafters lying diagonally across the wall tops. The four oak corner posts, the two door posts and the lintel stand in place but are charred black with cracked alligator-skin char. Black soot streaks fan upward on the stone above the doorway and the window. Grey-white ash on the plinth; the cloth is gone; the antler skull blackened; plinth, steps and standing stones intact but sooty; a thin wisp of smoke.

**ruin:**
> … as an ANCIENT RUIN centuries later: no roof and no timber left at all, posts and lintel rotted away, the rubble-stone walls broken down to uneven stumps from knee to shoulder height with the doorway gap still readable, tumbled stones on and around the plinth, the plinth and steps worn and partly buried with grass and moss in every joint, the two standing stones still standing but lichen-covered, wild grass, ferns and flowers growing inside the walls and a small young birch tree growing out of the middle of the ruin.

**drowned** (second take):
> … DROWNED and abandoned: a risen sea of calm, clear, shallow green-blue water now reaches HALF-WAY UP THE WALLS, about one metre above the top of the plinth, so the whole plinth, the steps and most of the two standing stones are submerged and seen faintly through the clear water (only the rounded tops of the standing stones break the surface), the lower half of the doorway is flooded and the inside is dark water with NO fire. Dark green weed and slimy algae coat the walls just above the waterline with a pale tide-line stain; the thatch roof is sodden, dark grey-brown, sagging and rotted through with several holes; the oak posts are black with wet; the saffron cloth is gone; a few strands of weed hang from the lintel and antlers; small floating reed debris. The water fades into a plain flat light-grey (#c8c8c8) haze background.

## Known misses

- **`shrine-burnt`:** the camera is slightly lower and the shrine slightly smaller in frame than in the other states. The proportions match: the walls end at the eave line.
- **`shrine-ruin`:** the plinth reads a little broader and flatter, because of the grass mound, and the doorway gap is less clear than requested. The tables above are the specification.
- **`shrine-drowned`:** the background is open water fading to grey rather than a plain backdrop, since the water has to go somewhere.
- **`shrine-damaged`:** the requested fallen finial and leaning standing stone did not appear. See the note under its prompt.
