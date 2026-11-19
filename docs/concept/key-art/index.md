# T-018 · Key art: title screen and capsule images (Grok)

One key-art master and five crops cut from it. None of these files contains text, a logo or a watermark.

The planet is **the T-013 planet itself**: the pixels of `docs/concept/world-states/state-healthy.png`, relit in code. The continents, coastlines, craters and clouds are therefore identical to the world states and the T-016 night frames by construction.

| File | Size | Use |
|---|---|---|
| `key-art-master.png` | 3840×2160, 3.2 MB | The master: the planet held in the light, half day and half night with city lights, a thin moon, two comets arriving, and a warm light gathered along the terminator. The left side is calm space for the title. |
| `title-1920x1080.png` | 1920×1080 | Title-screen background. The whole master scaled by ½, with the title on the left as in `docs/concept/ui/screen-title.png`. |
| `capsule-header-920x430.png` | 920×430 | Store header capsule. The planet is on the right and slightly cropped top and bottom; the logo goes on the left. |
| `capsule-main-1232x706.png` | 1232×706 | Main store capsule. The whole planet, with the logo on the left. |
| `capsule-vertical-748x896.png` | 748×896 | Vertical capsule. The planet fills the frame, and the logo sits in a band at the top (see Known misses). |
| `library-hero-3840x1240.png` | 3840×1240 | Library hero: a band across the master at full resolution. |
| `contact-sheet.jpg` | | All crops, plus the header capsule at real small-capsule size (231 px wide) to check how it reads when small. |
| `safe-zones-preview.png` | 1920×1080 | **A guide, not a deliverable.** It shows each crop with its logo safe zone dashed and a sample wordmark. The wordmark is real text set in Cinzel and EB Garamond (OFL, `assets/fonts/`) in HTML, not generated. |
| `src/keyart.py` | | Draws the master and all crops: `python docs/concept/key-art/src/keyart.py` (numpy, scipy, Pillow; about 30 s). |
| `src/safe-zones-preview.html`, `src/render.js` | | The guide page and its headless-Chrome renderer: `node docs/concept/key-art/src/render.js safe-zones-preview`. |

## How the master was made (no generator prompt)

There is **no image-generator prompt for this task**. The master is drawn in code, so that the planet is guaranteed to be the T-013 planet (the lead's T-010 and T-013 lesson on drift). It reuses the T-016 night-side code, `docs/concept/night/src/night.py`, which was accepted as the target for city lights.

1. **Relight.**
   - The sun is at (−0.95, 0.14, 0.22) in globe space, so the terminator runs just right of the disc centre and the day side faces the title.
   - Night colour, a dusk band, moon fill (0.9) and a thin night rim come from `night.relit()`.
2. **City lights on the night half**, with the T-016 v2 method:
   - 85 cities with Pareto sizes, weighted toward the coast
   - organic sprawl with satellite towns, in a sodium and LED mix (late Industry to Atomic)
   - broken, curving rail threads
   - limb dimming, soft cloud occlusion and three-scale bloom
3. **The god's presence, with no figure or face.**
   - A broad warm light pools along the terminator, strongest at mid-disc (`#ffb35c` at about 22 % before bloom). It is a hint that someone is holding the world toward the light.
   - Two comets come in from the upper right toward the night side, an echo of the genesis comets.
   - The comets are drawn as a bright head, a soft coma and a tail that widens and fades.
4. **Layout at 3840×2160.**
   - **Planet:** scaled 2.35× from the 1280 frame (Lanczos plus a light unsharp mask). Centre (2600, 1110), radius 851 px.
   - **Atmosphere:** a blue rim strong on the day limb and faint on the night limb, and a faint warm glow in space beside the lit limb.
   - **Background:** a code star field of 2,600 stars, mostly faint.
   - **Moon:** the half-lit moon from `state-healthy.png`, moved to the upper right at (3470, 300).
   - **Grade:** a gentle filmic shoulder. There is no grain, because grain pushed the PNGs past the 4 MB limit.
5. **Crops.** Each one is a box cut from the master and resized with Lanczos:

   | Crop | Box in the master (x0, y0, x1, y1) | Scale |
   |---|---|---|
   | title | 0, 0, 3840, 2160 | ½ |
   | header | 528, 422, 3472, 1798 | 1/3.2 |
   | main | 398, 192, 3601, 2027 | 1/2.6 |
   | vertical | 1771, 7, 3528, 2112 | 1/2.35 |
   | library hero | 0, 490, 3840, 1730 | 1:1 |

## Safe zones for the logo
Coordinates are in each file's own pixels, as (x0, y0) to (x1, y1). Keep the logo inside the zone. The planet's lit limb lies just outside each zone.

| File | Logo safe zone | Notes |
|---|---|---|
| `title-1920x1080.png` | (120, 200) to (820, 700) | Matches the screen-title mockup: wordmark at about 160, 250, with the menu below it, all left of x = 874, where the limb is. |
| `capsule-header-920x430.png` | (24, 60) to (350, 370) | The limb is at x ≈ 381. |
| `capsule-main-1232x706.png` | (40, 120) to (480, 590) | The limb is at x ≈ 520. |
| `capsule-vertical-748x896.png` | (30, 16) to (520, 100) | A thin band above the planet. A second option is the dark upper-right night quadrant, which has few lights. |
| `library-hero-3840x1240.png` | (160, 200) to (1500, 1040) | The store adds the logo separately; keep it in this zone. |
| `key-art-master.png` | (300, 400) to (1600, 1500) | The calm space left of the planet. |

## Acceptance criteria
- **Same planet as T-013:** these are the same pixels, relit.
- **Cinematic and near-photoreal:** the look follows `shots/ue-cloudlab-terminator-final.png`, with a dark night side, sparkle cities and a blue day limb.
- **Reads at small capsule size:** see the 231 px header in `contact-sheet.jpg`. The day and night halves, the city lights and the moon still read.
- **No text, logos or watermarks** in any crop. Only the guide, `safe-zones-preview.png`, has sample text, and it is not a crop.

## Known misses
- **Resolution:** the planet is upscaled 2.35× from the 1280×720 T-013 frame, so at 100 % in the 4K master and library hero the surface is softer than true 4K art. For release, re-render the same composition from the Unreal planet at 4K. The layout, boxes and safe zones above still apply.
- **The god's light is subtle:** it reads as warm dusk at the terminator rather than as an obvious presence. This is deliberate ("a hint"), but it could be raised in `keyart.py` (`god = … * 0.22`).
- **Vertical capsule:** the planet fills it, leaving only a thin band at the top for the logo.
- **Header capsule:** the planet is cropped slightly at the top and bottom.
- **Far-east night side:** as in T-016, the land there is inferred, so its city lights are placed with less certainty.
