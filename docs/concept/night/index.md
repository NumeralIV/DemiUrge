# T-016 · The night side through the ages (Grok)

Five 1280×720 PNG frames, about 0.8 MB each, with no text or watermarks. Each shows **the same planet and framing as T-013** (`docs/concept/world-states/`), with the terminator in view and most of the disc in night. City light grows from scattered firelight to continents of light.

This is **revision 2, after the lead's review.** The cities are now organic sprawl rather than dots joined by lines. Roads are broken, curving threads, and the Stone age has none. The frames have bloom, limb dimming and soft cloud occlusion, and the aurora is drawn as curtains in perspective.

| File | Age | What it shows |
|---|---|---|
| `night-stone.png` | Stone / Bronze | About 60 lone campfires (some in pairs), orange pinpricks, mostly near coasts, with **no connecting lines** |
| `night-iron.png` | Iron / Sail | About 40 small towns with soft, irregular glows and a satellite village each, and faint broken tracks between near neighbours |
| `night-industry.png` | Industry / Atomic | About 75 cities in sodium yellow, sized by a power law, each with 1–10 satellite towns. They merge into coastal sprawl belts, with faint rail threads and a warm light-haze. |
| `night-space.png` | Space | About 90 warm-white cities with many satellites, a dense web of curving lit corridors, and a cool-white launch-site point with a short rising plume |
| `night-aurora.png` | (Iron lights) | Auroral curtains: green at the base and violet at the top, with vertical rays and folds. They cross the upper night side and stand in profile above the limb at top right. Moonlit clouds; city lights dimmed under the clouds. |
| `contact-sheet.jpg` | | All five, plus a lock check: the coastline of `state-healthy` drawn in cyan over `night-industry` |
| `src/night.py` | | The script that draws all five (numpy, scipy, Pillow): `python docs/concept/night/src/night.py` |

## How these were made, and why in code

**Drawn in code, not generated.** Every frame is drawn by `src/night.py` from the merged `docs/concept/world-states/state-healthy.png`. The continents, camera, moon and stars are therefore **identical** to T-013 by construction, which follows the lead's T-010 lesson on drift.

**I tried the generator once and rejected it.** I asked it to add the aurora. It shrank and moved the globe, and drew a halo ring floating in space.

**Steps in the script:**

1. **Relight.**
   - The globe is treated as a sphere: centre (644, 354), radius 362 px, measured from the limb.
   - The sun is at (−0.86, 0.12, −0.50), so only a crescent on the left stays in daylight.
   - **Day factor** is a smoothstep of n·L from −0.06 to 0.22.
   - **Night colour** is the day colour × (0.030, 0.038, 0.070), plus a cold moon and earthshine fill of × (0.10, 0.12, 0.18) × 0.9 (× 1.4 in the aurora frame). The fill keeps the coastlines faintly readable.
   - A narrow warm dusk band runs along the terminator, and a thin blue rim sits on the night limb.
   - The healthy frame's own city lights are removed first with a median filter.

2. **Land and coast.**
   - The land mask comes from the healthy day colours.
   - Sites are sampled on night-side land and weighted toward the sea by `exp(−distance_to_sea / k)` (k = 6–9 px), with a minimum spacing, so lights belt the coasts.

3. **Organic sprawl** (`sprawl()`).
   - Each settlement has a core radius `rc = 1.6 + 2.6·√size` px, where size is a Pareto draw (α 1.3–2.2) capped at 3 (Iron), 9 (Industry) or 12 (Space).
   - **Density:** `exp(−(d / (rc·(0.55 + 0.9·noise)))^1.4)`. The noise is a fractal value-noise field, so every edge is irregular and no two cities share a shape. The y axis is squashed 1.15× to hint at the curvature of the globe.
   - **Texture:** each pixel is lit with probability `1.6·density`, at brightness `random^3`. That gives a sparkle of many dim and few bright lights, never a smooth disc.
   - **Satellite towns:** 1 (Iron), 1 + size (Industry) or 2 + size (Space), placed 2.5–5.5 rc away on land.

4. **Roads and rail** (`thread()`).
   - A noisy walk steers toward a near neighbour: 18 % pull toward the target per step, ±0.22 rad of random turn.
   - Each thread is broken into dashes and keeps 35–80 % of its points. It is drawn only on land and is dim (0.22 / 0.40 / 0.50 of a city point).
   - It never draws a straight segment between city centres. **Stone has no threads.**

5. **Optics** (`finish()`).
   - Lights × night factor × **limb dimming** `0.3 + 0.7·√(n·view)` × **cloud occlusion** `1 − 0.8·blur(cloud, 3 px)`.
   - **Bloom:** 0.8·E + 1.3·blur 1.2 px + 1.3·blur 4 px + (1.2 + 6·haze)·blur 14 px.
   - Haze is 0.35 in Industry and 0.25 in Space.
   - Compositing: `out = 1 − (1 − base)·exp(−light)`, so dense cores saturate softly instead of clipping.

6. **Aurora** (`aurora()`).
   - The auroral oval sits 34° around a magnetic pole tilted toward the upper-right limb, (0.36, 0.86, 0.36), so one arc crosses the night side seen from above and the far arc stands **in profile above the limb**.
   - The edge folds and wanders: sines plus a random walk.
   - Each of 5,200 columns is a **vertical curtain** of 30 samples along the local vertical, rising 7–13 % of the radius (exaggerated about 2× for readability).
   - **Colour:** a bright green lower border fading upward, with a violet top band at about 35 %.
   - **Ray texture:** a gamma-distributed brightness per column times a fine sinusoidal striation, giving the vertical rays.
   - It is hidden behind the planet, fades over the sunlit crescent, and is blurred at 0.7, 3 and 12 px.
   - A very soft 20 px glow tints the ground and the clouds below.

**Check:** the cyan coastline from `state-healthy` sits exactly on the night frames (last cell of the contact sheet).

## Notes for the engineer (planet module)

### Light colours by age
These are emission colours before bloom.

| Age | Main colour | Accents | Approx. colour temperature |
|---|---|---|---|
| Stone, Bronze | firelight `#ff8c38` | | about 1,900 K |
| Iron, Sail | oil lamp `#ffad52` | | about 2,200 K |
| Industry, Atomic | sodium `#ffc766` | | about 2,700 K |
| Space | LED warm-white `#fff0d1` | launch site cool-white `#cce0ff` | about 4,000 K, launch about 7,000 K |
| Aurora | green `#4dff8c` at the lower border | violet top `#b852ff` at about 35 % | |
| Moonlit cloud | add `#29303f` | | on the night side only |

### Brightness

Measured on these frames, as 8-bit luminance:

| Age | Night-side mean | Pixels > 60 | Pixels > 120 | Brightest |
|---|---|---|---|---|
| Daylit land (reference) | about 100 | | | |
| Stone | 4.7 | 314 | 74 | fires about 65 |
| Iron | 5.4 | 1,100 | 18 | towns about 90 |
| Industry | 13.4 | 14,275 | 3,153 | |
| Space | 19.6 | 26,859 | 8,490 | core about 225 |

In HDR terms, a dense core is about 2.2× daylit land after bloom, and a single fire about 0.65×. The **total** light on the night side grows about 4× from Stone to Space, and the lit area grows about 85×. Age reads mainly from **coverage and colour**, not from peak brightness.

### Clustering
- **Footprint, not intensity, carries the size.** Core radius grows with √size: about 2 px for a village and about 9 px for a metropolis at this zoom. Brightness per light stays about the same, and only the bloom of dense cores grows.
- **Every city is irregular:** a noise-bent falloff, sparkle texture and satellite towns. Big cities in the Industry and Space ages merge along coasts into continuous belts.
- **Placement:** about 70 % of lights sit within 2 cells of the sea; the rest follow inland. In the game, add rivers and valleys from `Hydro` here; this frame has no river data.
- **Roads:**
  - Stone: none.
  - Iron: faint, broken tracks between neighbours within about 70 px.
  - Industry: rail to the 2 nearest neighbours.
  - Space: corridors to the 3 nearest neighbours.
  - Always curving, broken and dim (20–50 % of a city light), over land only.
- **Haze:** from Industry on, a wide, warm light-dome round the big belts.

### Optics
- **Limb dimming:** lights × (0.3 + 0.7·√cos θ), where θ is the angle between the surface normal and the view.
- **Clouds:** clouds dim the lights by up to 80 %, with soft edges (3 px blur), and are themselves lit cold silver by the moon.
- **Terminator:** lights fade in from n·L = +0.04 to −0.12, so they never show in daylight.

### Aurora
- **Oval:** about 30–35° from a geomagnetic pole, which can be tilted per world from the seed.
- **Curtains:** stand along the local vertical. The real height is about 2–5 % of the radius; these frames use 7–13 % so they read at orbit zoom.
- **Look:** a sharp, bright green lower border, fading upward, with a violet top. Vertical ray striations drift along the oval at about 0.02 rad/s, and the brightness flickers slowly (period 2–8 s).
- **When to show it:** when the night side faces the camera and `dust` < 0.2. Dust and ash hide it.

## Known misses

- **Far-eastern night side:** the land there is inferred from the healthy frame's old city lights, because that region is never shown in daylight. Its coast is therefore less certain.
- **No rivers:** the lights follow coasts but not rivers, because the concept image carries no river data. The build should use `Hydro`.
- **Aurora height:** the curtains are about 2× taller than physical, for readability. The near arc partly crosses the dusk zone, where it fades.
