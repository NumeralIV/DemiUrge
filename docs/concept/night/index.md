# T-016 · The night side through the ages (Grok)

Five 1280×720 PNG frames, about 0.8 MB each, with no text or watermarks. Each shows **the same planet and framing as T-013** (`docs/concept/world-states/`), with the terminator in view and most of the disc in night. City lights grow from scattered firelight to networks of lit corridors.

| File | Age | What it shows |
|---|---|---|
| `night-stone.png` | Stone / Bronze | About 60 scattered firelights, orange pinpricks, mostly near coasts |
| `night-iron.png` | Iron / Sail | About 40 town clusters, each 8–22 points, joined by the first faint dotted roads |
| `night-industry.png` | Industry / Atomic | About 70 cities in sodium yellow (size follows a power law), rail lines between neighbours, brighter coastal ports, a warm light-haze |
| `night-space.png` | Space | About 85 cities in warm-white LED, joined by continuous lit corridors, plus a cool-white launch-site bloom with a short plume streak |
| `night-aurora.png` | (Iron lights) | A green auroral oval with violet tops round the north pole, moonlit clouds, and lights dimmed under the clouds |
| `contact-sheet.jpg` | | All five, plus a lock check: the coastline of `state-healthy` drawn in cyan over `night-industry` |
| `src/night.py` | | The script that draws all five (numpy, scipy, Pillow): `python docs/concept/night/src/night.py` |

## How these were made, and why in code

**Drawn in code, not generated.** Every frame is drawn by `src/night.py` from the merged `docs/concept/world-states/state-healthy.png`. That keeps the continents, camera, moon and stars **identical** to T-013 by construction, which follows the lead's T-010 lesson on drift.

**I tried the generator once and rejected it.** I asked it to add the aurora to `night-iron.png`. It shrank and moved the globe, and drew the aurora as a cartoon halo floating in space, so both the lock and the scale were lost. The aurora is drawn in code as well.

**Steps in the script:**
1. **Relight.**
   - The globe is treated as a sphere: centre (644, 354), radius 362 px, measured from the limb of `state-healthy`.
   - It is lit by a sun at (−0.86, 0.12, −0.50), which is behind and to the left, so only a crescent on the left stays in daylight.
   - **Day factor** is a smoothstep of n·L from −0.06 to 0.22.
   - **Night colour** is the day colour × (0.030, 0.038, 0.070), plus a cold "earthshine and moon" fill of × (0.10, 0.12, 0.18) × moon, where moon = 0.9 (1.4 in the aurora frame). The fill keeps the coastlines faintly readable.
   - A narrow warm dusk band runs along the terminator, and a thin blue rim sits on the night limb.
   - The healthy frame's own city lights were removed first with a median filter, so each age draws its own.
2. **Land mask.** Taken from the healthy day colours: land where the mean of red and green is more than blue + 0.02 and luminance is between 0.12 and 0.80.
3. **Cities.**
   - **Placement:** city sites are sampled on night-side land, weighted toward coasts by `exp(−distance_to_sea / k)` (k = 6–9 px), with a minimum spacing between sites.
   - **Shape:** each site is a gaussian cluster of points. The number of points and the spread grow with city size; size is a Pareto draw (α 1.4–1.6), so a few big cities and many small ones.
   - **Routes:** roads, rail and corridors are gently curved Bézier chains of light points between nearest neighbours, drawn only over land.
4. **Compositing.**
   - The lights are added as emission: `out = 1 − (1 − base)·exp(−light)`, so cores saturate softly instead of clipping.
   - On top come a 6 px glow at 0.25 and, from Industry on, a 22 px haze at 0.35–0.5.
   - The lights are masked by the night factor, so they fade in across the terminator.
5. **Aurora.**
   - An oval at 19° from a magnetic pole tilted toward the camera, (0.30, 0.68, 0.67), with a folded, wandering edge.
   - Three parallel folds give the band its width. Curtains stand 0.3–8.5 % of the radius above the surface: green at the bottom, fading upward to a violet top.
   - A random "ray" texture runs along the oval. The aurora is hidden where it goes behind the planet, and fades over the sunlit crescent.
   - Blurred at 1.1, 5 and 16 px and added as emission.
   - Clouds (white, unsaturated pixels of the healthy frame) are lit silver-blue on the night side, and they dim the city lights beneath them by 75 %.

**Check:** the cyan coastline from `state-healthy` sits exactly on the night frames (last cell of the contact sheet), since they are the same pixels.

## Notes for the engineer (planet module)

### Light colours by age
These are emission colours before bloom.

| Age | Main colour | Accents | Approx. colour temperature |
|---|---|---|---|
| Stone, Bronze | firelight `#ff8c38` | | about 1,900 K |
| Iron, Sail | oil lamp `#ffad52` | a few fires `#ff8c38` | about 2,200 K |
| Industry, Atomic | sodium `#ffc766` | port cores `#ffad52` | about 2,700 K |
| Space | LED warm-white `#fff0d1` | launch site cool-white `#cce0ff` | about 4,000 K, launch about 7,000 K |
| Aurora | green `#4dff8c` | violet tops `#bf59ff`, at about 30 % | |
| Moonlit cloud | add `#29303f` | | on the night side only |

### Brightness

Measured on these frames, as 8-bit luminance:

| | Value |
|---|---|
| Daylit land (reference) | about 100 |
| Night-side mean, Stone | 4.5 |
| Night-side mean, Iron | 5.9 |
| Night-side mean, Industry | 9.5 |
| Night-side mean, Space | 12.4 |
| Brightest city cores | 180–215, i.e. **1.8–2.1× daylit land** (bloomed) |
| Stone fires | about 75, 0.7× daylit land |

In HDR terms, a big city core is about 2× daylit land and a single fire about 0.7×. The *total* light on the night side grows about 3× from Stone to Space. The count of bright pixels grows 52 → 738 → 1,697 → 3,910, which is about ×75 from Stone to Space.

### Clustering
- Brightness and footprint scale with settlement population.
- Footprint radius grows with sqrt(size): about 1.5 px for a village and about 6 px for a metropolis at this zoom.
- Brightness per point stays roughly constant, so **a big city is brighter because it is bigger and denser, not because each light is hotter**. It also gets more bloom.
- Clusters are irregular, not discs: about 0.8 aspect, with a scatter of satellite points.

**Placement rules:**
- **Coasts:** about 70 % of lights sit within 2 cells of the sea. Ports (Industry and later) get a brighter core of `#ffad52`.
- **Roads:** they appear from Iron, and only between settlements less than about 80 px apart at this zoom.
  - **Iron:** dotted (about 70 % of points kept), dim at 0.45.
  - **Industry:** rail between the 2 nearest neighbours, 0.22, mostly continuous.
  - **Space:** corridors to the 3 nearest neighbours, continuous, 0.30, which turns the land into a lit web.
  - In every age, routes curve gently and follow land, never crossing open sea.
- **Haze:** from Industry on, a 20–25 px blur of the lights is added at 0.25–0.5. It reads as light pollution and smog glow, and ties into `state-industrial`.

### Clouds and weather
- Clouds dim the lights under them by about 75 %, and are themselves lit cold silver by the moon.
- Storms (T-013 warming) would black out whole regions, which is a good, readable effect.

### Twilight and the limb
- Lights fade in from n·L = +0.04 to −0.12, so they never show in daylight.
- Keep lights off the outer 7 % of the radius at the limb, where foreshortening crushes them into a line.

### Aurora
- An oval about 19° from a geomagnetic pole, which can be tilted per world from the seed.
- Curtains 0.3–8.5 % of the radius high, with a ray texture that should drift slowly, about 0.02 rad per second.
- Show it when the night side faces the camera and `dust` < 0.2; dust or ash hides it.

## Known misses

- **Iron frame:** a few of the "towns" in the far east of the night side sit where the healthy frame had lights. With no daylit view of that region, the land there is inferred from those old lights, so the coast is less certain on that side.
- **Aurora:** it is seen from above as an oval rather than as curtains in profile on the limb, because the north pole of this planet sits near the top of the frame and is partly cut off by it.
- **Launch site:** the Space-age launch bloom is in the north-east of the main continent. It is chosen in code as the biggest coastal city, not hand-placed.
