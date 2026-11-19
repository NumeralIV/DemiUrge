# Concept art: the genesis sequence from orbit (T-010, Grok)

These are six 1280×720 PNG frames (0.6–1.2 MB each, no text or watermarks) showing one planet
through the four genesis phases in `docs/SPEC-SIM.md` §3.12: `molten` → `water` → `life` →
`people`. The camera, globe size and position, moon and sun direction stay the same in every
frame, so the six can be read as one locked-off shot. `contact-sheet.jpg` shows all six
together.

| File | Sim phase | `genesis.progress` hint |
|---|---|---|
| genesis-1-molten | `molten`, heat ≈ 1 | `cooling` |
| genesis-2-cooling | `molten`, heat → 0 | `cooling` |
| genesis-3-comets | `water`, seaLevel about −5 | `water-more` |
| genesis-4-seas | `water`, seaLevel ≥ −3.5, ready for `life` | `water-ok` |
| genesis-5-life-wave | `life`, cover.veg about 0.1 | `life-more` |
| genesis-6-awakening | `people`, just woken | `people` |

## How these were made, and keeping the planet consistent

All six images are original. They come from Grok Bot's image generator with our own prompts,
which are quoted in full below. No game or named artist was imitated.

**The anchor frame.** `genesis-4-seas` was made first, using our merged UI orbit background
(`docs/concept/ui/src/bg/orbit.jpg`) as the reference. That background was itself generated from
the approved `shots/ue-cloudlab-terminator-final.png`. The anchor keeps the orbit image's moon at
upper left, sun glint at left, terminator on the right, thin blue rim, and the small oval island
at lower left. Every other frame used the anchor as its reference and was told to keep the
"same continents and coastlines".

**One honest difference from the approved orbit image.** The main continent here is broader than
the dagger shape visible in the orbit shot. In that shot, clouds hide most of the land, and when
the generator removed the clouds it filled the land in. My two attempts to keep the narrow shape
both failed: the generator turned the cloud fields into land. Within this series, the continent,
the northern landmass and the island are consistent from frame to frame.

**Post-processing, done in code on the box (no painting by hand):**
- `genesis-4-seas`: the generator kept adding city-light sparkles to the night side of a
  lifeless world. A median filter removed bright point sparkles to the right of the terminator,
  and the deep night side was darkened by 35 %.
- `genesis-6-awakening`: the generator would not render dusk or tiny campfires; it painted a
  glowing crater instead. So the living-world frame (prompt below) was relit in code:
  - **Sphere relighting.** The globe was treated as a sphere (centre 636,357, radius 343 px) and
    lit by a sun direction of (−0.989, 0.05, −0.15). Daylight fades in over a smoothstep from
    n·L = −0.10 to 0.25. The night side is the original colour × (0.16, 0.19, 0.30). A warm dusk
    band (colour × (1.05, 0.72, 0.55), Gaussian, σ 0.11 around n·L = 0.04) runs along the
    terminator, with a cool blue band on the night side of it. The atmosphere rim is dimmed on the
    night side.
  - **Campfires.** Three campfire points (radius 0.8–1.3 px, `#ffc478`, `#ffaa5a`, `#ff9650`) were
    added with soft 5 px and 14 px halos, at about (612, 320) in the 1280×720 frame, on the river
    in the twilight just past the terminator.

## Shared look (all frames)
- **Composition.** The globe is centred at about (636, 357) in a 1280×720 frame, with a radius of
  about 340 px (95 % of the frame height). The moon is at about (100, 88). The background is black
  space with faint stars.
- **Sun direction.** Light comes from the left in every frame, matching the approved orbit shot.
  `genesis-1`, `-3` and `-5` are lit almost full-face, so the step reads clearly. If the build
  keeps a fixed terminator on the right, as in the orbit shot, that is fine; the colour changes
  below are what carry each step.

---

## genesis-1-molten.png — the molten world
- **What it shows.** The whole globe incandescent:
  - Dark red-brown crust rafts already have the shapes of the future continents (main continent,
    northern landmass, island ring).
  - The rafts are separated by bright lava seams and wide molten basins where the seas will be.
  - A hot orange haze sits at the limb.
- **Prompt** (reference: genesis-4 anchor):
  > The same planet as the reference image, same camera, same globe size and position, same moon at upper left, same sun from the left, but at the very beginning of its life: a glowing MOLTEN world with no water at all. The surface is a cracked crust of dark, still-glowing plates whose shapes follow the same continents as the reference (the broad main continent in the middle, the northern landmass at upper left, the small oval island at lower left), separated by bright orange-yellow lava seams and wide molten seas of incandescent orange where the oceans will later be. Bright yellow-white heat where lava is thickest, deep red at cooling edges, a hot orange glow haze in a thin atmosphere around the limb, faint smoke plumes. The night side on the right glows dull red from its own heat. Black space and faint stars. Cinematic near-photoreal space render, physically based emission and bloom. No text, no labels, no watermark.
- **Engineer notes.**
  - **Colours:**

    | Element | Hex |
    |---|---|
    | Crust | `#3f0a03` (dark) → `#741d0d` (median) |
    | Lava seams | `#e36a2a` |
    | Hottest cores | `#ffde49` (lit seams), `#f5882a` (hot-pixel median) |
    | Limb haze | about `#9e5641` |

  - **Glow intensity.** This frame is self-lit, so emission dominates.
    - Seams should sit at about **8–15× scene white** in HDR, enough to bloom strongly.
    - Crust should sit at about 0.5–1×.
    - The night side should glow at about 0.3× from heat alone, so the terminator nearly vanishes.
    - Use a wide, warm bloom of 1–2 % of the screen height and an orange limb haze.
  - **Animation into frame 2.** Drive everything from `heat` (1 → 0; it falls by 1/60 per tick,
    so the phase lasts 60 ticks ≈ 7.5 s at `REAL_PER_TICK` 0.125):
    - Emission scales as `heat²`.
    - The seam colour slides down the blackbody curve: white-yellow → orange → deep red.
    - The crust darkens to basalt.
    - Thin seams go dark first and wide basins last; key this on a distance-from-seam-centre mask.
    - The limb haze turns from orange to grey-tan.

## genesis-2-cooling.png — the crust cools
- **What it shows.**
  - The crust is charcoal-black basalt in the same continent shapes.
  - The lower basins are smooth dark plains, with no water yet.
  - Thin dim red cracks remain along basin edges, with a few vent points.
  - The first white-grey steam plumes rise.
  - There is a pale sun glint on the glassy basin floor.
- **Prompt** (reference: genesis-4 anchor):
  > The same planet as the reference image, same camera, same globe size and position, same moon at upper left, same sun from the left, at the cooling stage before any water: the crust has gone dark — black and charcoal basalt with a faint brown tint — following the same continents as the reference (the broad main continent in the middle, the northern landmass at upper left, the small oval island at lower left), while the future ocean basins are lower, smoother, darker plains of fresh black rock. Only thin dim red-orange glowing cracks remain, mostly along the basin edges and a few volcanic vents with small orange points. The first white-grey steam clouds and volcanic haze rise in patchy streaks and puffs over the basins, catching the sunlight. Thin hazy grey-tan atmosphere rim. No liquid water at all. The night side is dark with a few faint red glowing seams. Black space, faint stars. Cinematic near-photoreal space render, physically based. No text, no labels, no watermark.
- **Engineer notes.**
  - **Colours:**

    | Element | Hex |
    |---|---|
    | Basalt | `#1a1819` (median), lit `#2f2d2c` |
    | Basin floor | about `#111112` |
    | Cracks | about `#843b26` |
    | Steam | `#625753` in shadow → `#b6a194` lit |

  - **Glow intensity.** Cracks should sit at about **1–2× scene white**: visible, but no longer
    blooming.
  - **Sky and effects.**
    - Steam is a slow-rising particle or volume layer that is **denser over basin edges**. It
      should show strong forward scattering toward the sun.
    - The atmosphere is a thin, dusty grey-tan rim: Rayleigh scattering is low and Mie scattering
      is high.
  - **Animation into frame 3.** The `water` phase begins (`heat` = 0). The steam coverage grows to
    become the first cloud layer, and the glints on the basin floor stay.

## genesis-3-comets.png — the coming of the waters
- **What it shows.**
  - Three icy comets with long blue-white tails come in from the upper left.
  - One strikes the main continent in a white flash with a red ejecta ring.
  - The first shallow seas, dark teal, pool in the lowest basins between wide stretches of
    exposed dark basin floor.
  - Steam rises from the sea edges.
- **Prompt** (reference: genesis-4 anchor):
  > The same planet as the reference image, same camera, same globe size and position, same moon at upper left, same sun from the left, during the coming of the waters: three icy comets with long glowing blue-white tails streak in from the upper left toward the planet, and one has just struck the dark basin east of the main continent, throwing up a bright white-orange flash and an expanding ring of steam. The continents are the same as the reference but still dark charcoal basalt with a few dim red cracks. The ocean basins are only partly filled: shallow new seas of dark blue and murky teal pool in the lowest parts of the basins, with wide dark dry basin floor still exposed between them and steam rising from their edges. Patchy white steam clouds. Thin hazy pale atmosphere rim. Night side dark. Cinematic near-photoreal space render, physically based. No text, no labels, no watermark.
- **Engineer notes.**
  - **Colours:**

    | Element | Hex |
    |---|---|
    | Comet heads and tails | `#d5e7f4` → transparent |
    | Impact flash core | near-white `#fcfdfb`, with a red ejecta rim about `#c0402a` |
    | New seas | `#3f4c59` (median), deep about `#091d23`, teal edges |
    | Crust | `#242325` |

  - **Glow intensity.**
    - The comet head should sit at about **4× white**, with a tail that fades to zero over
      0.3–0.5 of the globe radius.
    - The impact flash should peak at **20× white** for about 0.15 s, then decay over 1 s.
    - The flash is followed by a white steam ring that expands to about 6 % of the globe radius
      and fades over about 3 s.
  - **Animation.**
    - Each comet click raises the sea a step. Seas should **fill from the deepest cells upward**:
      threshold height against `seaLevel`, with a 0.5–1 s ease, so the shorelines creep outward.
    - Fresh water stays darker and more turquoise-murky until frame 4.
    - The impact in the image is on land. For the effect itself, aim the strikes at basins; the
      flash, steam ring and sea rise are what matter.

## genesis-4-seas.png — oceans filled, bare world (the anchor frame)
- **What it shows.**
  - Full oceans: deep navy with teal shallows and a sun glint.
  - Bare continents of grey basalt, umber and rust rock with ridges and craters, and no green.
  - Very few clouds.
  - A thin, faint blue atmosphere rim.
  - A totally dark night side.
- **Prompt** (reference: `docs/concept/ui/src/bg/orbit.jpg`, then a fix-up pass with the first
  result as reference):
  > The same planet as the reference image, seen from far orbit so the WHOLE globe is in frame: the planet is centred, about 85% of the frame height, with black space and a few faint stars all around and the small grey moon at upper left. Keep exactly the same continents and their positions as the reference: the long dagger-shaped main continent in the upper middle, the large northern landmass along the upper left limb, the small oval island in the lower left. But this is the young world just after its oceans filled, before any life: the continents are bare rock and mineral soil in grey, umber, rust and ochre with dark basalt plains, crisp ridges and old impact craters, no green anywhere; the oceans are deep navy and slightly murky teal near coasts; only a few thin wisps of cloud; the atmosphere rim is a thin, faint, pale blue line, much thinner than on a living world. Sun from the left with a soft sun glint on the ocean, the day-night terminator crossing the right third of the globe, the night side completely dark with no lights. Cinematic near-photoreal space render, physically based lighting. No text, no labels, no UI, no watermark.

  > (fix-up) Edit of the reference image: keep exactly the same planet, camera, globe size and position, the same continent shapes and coastlines, the same small oval island at lower left, the same moon at upper left and the same sun direction from the left with the terminator on the right. Fix only the night side: it must be completely black with no lights or sparkles at all (no life exists yet). Everything else stays: bare lifeless rock continents in grey, umber, rust and ochre with craters and ridges, deep navy oceans with murky teal shallows, soft sun glint, a very thin faint pale-blue atmosphere rim, only a few faint wisps of cloud. Cinematic near-photoreal space render. No text, no labels, no watermark.

  Night-side sparkles were then removed in code, as described above.
- **Engineer notes.**
  - **Colours:**

    | Element | Hex |
    |---|---|
    | Ocean | `#1f334a` (median); deep about `#0d2d4c`, as in the approved orbit shot |
    | Rust rock | `#573c2c` |
    | Grey rock | `#413f3d` |
    | Lit rock | about `#6a6662` |
    | Atmosphere rim | thin, about `#8ca1b9` |

  - **Sky and effects.**
    - Rayleigh scattering is at about **40 % of the living world's**, so the rim is a hairline.
    - Cloud cover is about 5 %.
    - There are no city lights, and the night side is black.
  - **This frame is the target for "water-ok"** (ocean 35–75 %, "a temperate world"). It matches
    `docs/concept/ui/screen-genesis.png`, which shows a few minutes earlier.
  - **Animation into frame 5.** Shallows clear from murky teal to the orbit shot's turquoise over
    about 2 s as `life` begins.

## genesis-5-life-wave.png — life spreads from one touch
- **What it shows.**
  - A disc of fresh green forest and grassland on the main continent, centred on a touch point on
    its western coast.
  - At its edge, a bright **glowing lime-gold wave front** with a pale mist, curved like a ripple
    and still moving outward over the bare rock.
  - New white clouds appear over the green.
  - Everything outside the disc is still bare rock.
- **Prompt** (reference: genesis-4 anchor):
  > Edit of the reference image: keep exactly the same planet, camera, globe size and position, the same continent shapes and coastlines, the same small oval island at lower left, the same moon, the same sun from the left and terminator on the right. Add only this: life has just been seeded at ONE point on the main continent, at its western coast just right of the sun glint. From that point a circular patch of fresh green forest and grassland has spread outward, now about 300 km... a disc covering roughly the western third of the main continent only. The edge of the green disc is the wave front: a narrow, softly glowing ring of bright lime-yellow and gold light with a faint pale mist, clearly curved like a ripple centred on the touch point, still moving out across the bare rock. Everything outside the disc stays bare grey, umber and rust rock exactly as in the reference; the ocean stays the same; no ring over the ocean. A few new white clouds. Night side dark, no lights. Cinematic near-photoreal space render. No text, no labels, no watermark.
- **Engineer notes.**
  - **Colours:**

    | Element | Hex |
    |---|---|
    | New vegetation | `#363b20` (median); lit about `#4e5a2c` |
    | Wave front | `#b7aa74` → core about `#e8e09a` (lime-gold) |
    | Bare rock outside | as in frame 4 |
    | Clouds | `#d7ccb0` |

  - **Glow intensity.**
    - The front is a band about **1.5–2 % of the globe radius** wide.
    - Its emission is about **2–3× white** at the leading edge, falling to zero over the band
      behind it (an asymmetric profile).
    - It carries a thin, low-opacity mist layer.
    - Do not bloom it as hard as lava. It should read as "life", not fire.
  - **Animation.**
    - Front radius ≈ distance from the touch cell, driven by `Life.step` with `lifeBoost` ≈ 25 in
      `'life'`. It should look like a **ripple at roughly constant speed**: about 15–25 % of a
      continent per second at 1×.
    - Behind the front, biome colours fade in over about 1 s: forest dark green, grass tan-green,
      desert stays tan.
    - **The front must stop at the coastline.** In the image it spills over the coast and
      slightly into the sea on the west side. Treat that as an error; mask the front by land.
    - When `cover.veg` reaches 0.25 (the `life-ok` hint), the fronts can fade out.

## genesis-6-awakening.png — the first people wake, at dusk
- **What it shows.**
  - The same world, now fully alive: green forests, tan grassland, brown uplands, rivers, white
    clouds and a full blue rim.
  - It is dusk. The terminator runs through the main continent, with a warm sunset band along it
    and blue twilight beyond.
  - On the river in the twilight just past the terminator are **three tiny warm campfire points**,
    the first people waking. Nothing else is lit anywhere.
- **Prompt** for the living-world base (reference: genesis-4 anchor). The dusk lighting and the
  fires were added in code, as described above.
  > The same planet as the reference image, same camera, globe size and position, same continents and coastlines, same small oval island, same moon, now fully alive: green forests, tan grassland and brown uplands with fine river lines on every continent, deep navy oceans with turquoise shallows, scattered white cumulus clouds and a glowing blue atmosphere rim. It is DUSK on the main continent: the sun is setting off to the left behind the planet, so only the left third of the globe is still in daylight, a warm orange-rose sunset band runs down the terminator across the western part of the main continent, and the right two thirds of the globe, including most of the main continent, are in deep blue twilight and night. In that night, on the main continent near its western coast at a river mouth, there is one tiny cluster of three or four faint warm orange pinpoints of campfire light — the first people waking. Absolutely no other lights anywhere. Black space with faint stars. Cinematic near-photoreal space render, physically based. No text, no labels, no watermark.
- **Engineer notes.**
  - **Colours:**

    | Element | Hex |
    |---|---|
    | Day ocean | `#1f354e` |
    | Day forest | about `#383e2a` |
    | Sunset band | about `#d38e75` (rose-orange) |
    | Twilight land | about `#0c101a` |
    | Campfires | `#ffc478` core |

  - **Glow intensity.**
    - Campfires should be **3–5 px on screen at this zoom**, at **6–10× white**, with a 5–15 px
      soft halo. That is far larger than physical, on purpose: the player must see where their
      people are.
    - Use a gentle 1–2 Hz flicker of ±15 %.
    - These are the seed of the Stone-age lights in `docs/concept/settlements/`.
  - **Sky.**
    - The atmosphere rim is at full living-world strength, matching the approved orbit shot.
    - The dusk band is a **warm Mie and ozone term along the terminator**, about 8–12 % of the
      radius wide.
  - **Animation.**
    - On entering `people`, the camera holds while the terminator sweeps to the chosen cell (or
      the camera turns the planet so the cell sits at dusk).
    - Then the fires blink on one by one, 0.3 s apart.
    - On `awaken`, the camera eases toward them. This frame is a good title-card moment for
      "The first people wake."

---

## Open questions
1. Should the series match the cloud-hidden continent shapes of the approved orbit image
   exactly? The generator could not do that from orbit without clouds (see above). If an exact
   match is needed, the build's own terrain should drive these looks, and these frames serve only
   as colour and effects targets.
2. Frames 1, 3 and 5 are lit almost full-face, while frames 4 and 6 have the right-hand
   terminator. Should genesis lock the sun so the action always faces the light?
