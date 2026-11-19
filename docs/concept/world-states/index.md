# T-013 · The planet's changing states from orbit (Grok)

Seven 1280×720 PNG frames (0.7–1.1 MB each, no text or watermarks) showing **one planet** in seven world states. Planet and sky modules can use them as visual targets for the climate fields in SPEC-SIM §3.3 (`co2`, `Climate.anomaly`, `dust`, `seaLevel`, `ice`) and the disasters in §3.10.

## Locking the planet (lesson from T-010)

**One chain, one reference.**
- `state-healthy.png` was generated from the merged `docs/concept/genesis/genesis-4-seas.png`, the T-010 anchor frame. It therefore keeps the same globe, the moon at upper left, the sun glint at left and the terminator on the right.
- **Every other state was generated from `state-healthy.png` only, never from another state,** so drift cannot accumulate along a chain.
- Each prompt begins "Exactly the same image as the reference: identical camera, globe size and position, moon at upper left, sun from the left, terminator, and EXACTLY the same continents, coastlines and islands", and then names only what changes.

**Checked in code, not by eye.**
- I built a land mask from `state-healthy` (land where the mean of red and green is more than blue + 5, median-filtered at 7 px) and traced its outline.
- That cyan outline was drawn over every other state. The check image is `/workspace/T-013-coastline-lock-check.jpg` on the build box; the last cell of `contact-sheet.jpg` shows it on nuclear winter.
- **Result:** the main continent, the northern landmass and the small oval island line up within a few pixels in all six states. Ice and ash cover parts of them where they are meant to.

**Retakes and one composite.**
- **`state-industrial`:** the first take filled the eastern ocean with lit "land" (continent drift). It was regenerated with "keep all the oceans as open ocean, no new land".
- **`state-volcanic`:** the first take had a plume billowing off the planet into space, which was cartoonish. It was regenerated with the ash lying flat in the atmosphere, inside the disc.
- **`state-nuclear-winter` is a code composite**, because the generated frame hid the continents completely and could not be checked. It is built as follows:
  1. Take `state-healthy` with its city lights removed by a median filter (east of x = 700).
  2. Desaturate it 85 %, darken it to 62 % and tint it warm grey (×1.00, 0.93, 0.82).
  3. Blend 50/50 with the generated dust-veil frame.
  4. Add four dying lights (`#d2783a` core, 1.5 px; 7 px halo) on the night-side landmass, where the healthy frame had cities.

## Field targets for the engineers

All values are design targets, read off these frames. "Coverage" means the share of the visible disc.

| Field | healthy | industrial | warming | ice-age | nuclear-winter | volcanic | wasteland |
|---|---|---|---|---|---|---|---|
| Deep ocean | `#0b2a4a` | `#1c2e44` (−15 % sat) | `#123a55` | `#3c4a58` (steel) | `#24272a` | `#0b2a4a`, `#2a2622` under ash | `#0b2a4a` |
| Shallows / coast | `#2f8fa3` | `#4f7f80` (silty) | `#5e8f84` + brown silt `#8a7a5a` at mouths | `#56707c` | `#3a3c3b` | `#2f8fa3` | `#2f8fa3` |
| Forest | `#2f4a22` | `#3a4626`, 35 % less forest | `#5a5228` (yellowed) | `#2b3a26`, 60 % less | `#2a2a24` | `#2f4a22` | `#2f4a22` returning on 45 % of former forest |
| Grass / farm | `#6f8a3c` | `#9a7a50` farmland | `#a07a3a` | `#6b6448` tundra | `#3a3630` | `#6f8a3c` | `#7c8c48` patchy |
| Desert / bare | `#b9875a` | `#b08560` | `#b0532e`, 40 % larger | `#9c8a6c` | `#4a4038` | `#b9875a` | `#7a6e62` ruin fields |
| Ice coverage (land + sea) | 6 %, `#e8ecf0` | 6 % | 2 %, broken | 38 %, `#dfe6ee`, down to about 35° | 9 %, greyed `#a6a29c` | 6 % | 6 % |
| Sea level vs `seaBase` | 0 | +0.05 | +0.45 (coastal cells flood) | −0.25 | 0 | 0 | +0.1 |
| Haze colour / density | none (rim `#6fa8ff`) | `#a08a5a` at 20 % over land near cities | `#d8c0a0` at 10 % globally, warm rim | none, rim `#9cc0ff` | `#5a5048` at 70 % globally | ash `#6b5d50` at 80 % over about 30 % of the disc, feathered 15 % | none |
| Clouds | 15 % | 15 % + smoke `#5c5a58` | 30 %, with 4 cyclones | 10 % | hidden under dust | 15 % | 15 % |
| Day-side brightness | 1.0 | 0.95 | 1.05 | 1.10 (albedo) | 0.45 | 1.0, 0.5 under ash | 1.0 |
| Night lights | few, `#ffc070` | ×4 density, `#ffb055`, along coasts and rivers | ×4 | ×0.2 | ×0.03, `#d2783a` | ×1, dimmed 70 % under ash | 0 |

**Sim mapping** (suggested):

| State | Field values |
|---|---|
| healthy | `co2` < 40, anomaly 0 … +0.5, `dust` < 0.05 |
| industrial | `co2` 60–120, some factories, `Life.cover.veg` −30 % |
| warming | anomaly ≥ +3, sea ≥ +0.4 |
| ice-age | anomaly ≤ −4 |
| nuclear-winter | `dust` ≥ 0.6 after strikes |
| volcanic | an active `'volcano'` disaster with `dust` rising (the plume is local to the vent and drifts with the westerlies) |
| wasteland | the silence ending: population 0 with vegetation regrowing |

The states can mix. For example, warming plus volcanic is a hot world with an ash fan. So the shader should treat each column as an independent blend of fields, not as a preset.

## The images

### state-healthy.png
- **What it shows:** the reference.
  - A lush, balanced world: deep green forests, a tan dry interior on the main continent, and snowy ranges in the north.
  - Moderate white polar ice, sapphire ocean with turquoise shallows, scattered clouds and a crisp blue rim.
  - A few warm city lights on the night-side landmass at right.
- **Prompt** (reference `docs/concept/genesis/genesis-4-seas.png`):
  > The same planet as the reference image, with exactly the same camera, globe size and position, the same moon at upper left, the same sun from the left and the same terminator on the right, and EXACTLY the same continents, coastlines and islands (the broad main continent in the middle, the northern landmass at upper left, the small oval island at lower left). Change only the surface: the world is now at the peak of life, lush and balanced. Deep green forests and lighter green grasslands over most land, ochre-tan dry interiors and pale deserts only near the tropics, brown-grey mountain spines with snow on the peaks, bright white polar ice caps of moderate size at top and bottom, deep sapphire-blue oceans with turquoise shallows along the coasts, scattered white weather clouds and a few cyclone swirls, a crisp thin blue atmosphere rim. On the night side beyond the terminator, a few small warm golden city lights scattered along coasts and rivers. Black space with faint stars. Cinematic near-photoreal space render, physically based lighting. No text, no labels, no watermark.

### state-industrial.png
- **What it shows:**
  - Forests cleared to a brown patchwork of fields across the main continent.
  - Brownish smog lying over the coasts, and grey smoke drifting east over the ocean.
  - Duller coastal water, and more city lights on the night side.
- **Prompt** (reference `state-healthy`, second take):
  > Exactly the same image as the reference: identical camera, globe size and position, moon at upper left, sun from the left, terminator, and EXACTLY the same continents, coastlines and islands. Keep ALL the oceans as open blue ocean exactly where they are in the reference: the large ocean east of the main continent, the western sea and the southern ocean stay water, with no new land. Change only the surface condition to an INDUSTRIAL AGE planet: on the land, large patches of forest cleared to tan-brown farmland, a thin brownish-yellow smog haze lying over coasts and river valleys, a few dark grey smoke plumes drifting east off the continent, the oceans slightly duller blue-grey near the coasts. Daylit land shows NO lights. On the night side only, more warm orange-gold city lights than the reference, clustered along coasts and rivers of the land that is already there. Polar ice the same as the reference. Black space with faint stars. Cinematic near-photoreal space render. No text, no labels, no watermark.

### state-warming.png
- **What it shows:**
  - A hot world: the interior desert has turned rust-red and the forests have yellowed.
  - The north polar ice is thinned to broken patches.
  - Four large cyclones churn over the warm oceans, and a warm glare sits on the limb.
- **Prompt** (reference `state-healthy`):
  > Exactly the same image as the reference: identical camera, globe size and position, moon at upper left, sun from the left, terminator, and the same continents and mountains, pixel-aligned. Change only the surface condition to a WARMING, HOT planet: the polar ice caps shrunk to small broken patches, the seas risen so the low coastal margins and river mouths of every continent are flooded into wide shallow turquoise-brown bays (coastlines pulled slightly inland but recognisable), the forests yellowed and browner, deserts in the interior larger and redder, more large white storm systems and two big cyclone spirals over warm oceans, a warmer slightly hazy atmosphere rim. Many city lights on the night side, some along drowned coasts. Black space with faint stars. Cinematic near-photoreal space render. No text, no labels, no watermark.

### state-ice-age.png
- **What it shows:**
  - An ice sheet covering the whole northern landmass and the upper half of the main continent, and spreading as sea ice over the eastern ocean.
  - The south is olive and tan tundra, the oceans are steel grey-blue, and there are only a handful of lights.
- **Prompt** (reference `state-healthy`):
  > Exactly the same image as the reference: identical camera, globe size and position, moon at upper left, sun from the left, terminator, and the same continents, coastlines and islands, pixel-aligned. Change only the surface condition to an ICE AGE planet: white ice sheets spreading from the north pole far south over the whole northern landmass and the upper half of the main continent, white sea ice covering the northern and southern oceans, the remaining land in the south dull olive and tan tundra with sparse dark green forest, the oceans a cold steel grey-blue, few clouds, a pale cold blue atmosphere rim, only a handful of tiny lights on the night side. Black space with faint stars. Cinematic near-photoreal space render. No text, no labels, no watermark.

### state-nuclear-winter.png
- **What it shows:**
  - A dust-darkened, desaturated world: the continents show as muted grey shapes under a brown-grey veil.
  - The ice is greyed, the limb is dull brown, and four faint dying lights remain.
  - This frame is a composite; see "Retakes and one composite" above.
- **Prompt for the generated dust layer** (reference `state-healthy`):
  > Exactly the same image as the reference: identical camera, globe size and position, moon at upper left, sun from the left, terminator, and the same continents, coastlines and islands. Change only the condition to NUCLEAR WINTER: a dense dark grey-brown dust veil covers most of the globe so the surface is dim and desaturated, continents barely visible as muted grey-olive shapes through the murk, oceans dark slate grey, the sunlit side only dimly lit as if through smoke, dark smudges of soot spreading in bands, a dull brownish atmosphere rim, polar ice slightly larger and greyed, and on the night side almost no lights, only three or four faint dying orange points. Black space with faint stars, the moon unchanged. Cinematic near-photoreal space render. No text, no labels, no watermark.

### state-volcanic.png
- **What it shows:**
  - A bright orange vent with lava flows on the east side of the main continent.
  - A vast, flat ash sheet fanning north and east over about a third of the disc, shadowing the land and sea.
  - The rest of the world is still healthy.
- **Prompt** (reference `state-healthy`, second take):
  > Exactly the same image as the reference: identical camera, globe size and position, moon at upper left, sun from the left, terminator, and the same continents, coastlines and islands. Seen from orbit, a great VOLCANIC ERUPTION on the eastern coast of the main continent: a small bright orange glowing vent with a short ash column, and a vast flat sheet of grey-brown volcanic ash lying in the upper atmosphere, spreading from the vent in a great fan eastward and northward across the ocean, hugging the curve of the globe like a thin smoke layer and staying INSIDE the planet's disc (nothing sticks out into space), covering about a third of the visible hemisphere and darkening the land and sea beneath it, thinner and paler at its feathered edges. The rest of the planet stays lush green with blue oceans exactly as in the reference. Night-side city lights under the ash are dimmed. Black space with faint stars. Cinematic near-photoreal space render. No text, no labels, no watermark.

### state-wasteland.png
- **What it shows:**
  - After the collapse: grey-brown ruin fields where the farmland and cities were, with green returning along the coasts and rivers.
  - Clear blue oceans, clean air and **no lights at all**: the silence ending.
- **Prompt** (reference `state-healthy`):
  > Exactly the same image as the reference: identical camera, globe size and position, moon at upper left, sun from the left, terminator, and the same continents, coastlines and islands. Change only the condition to a WASTELAND after the collapse of civilisation, centuries later: the land is a patchwork of grey-brown scarred ground and pale ash-coloured ruin fields where cities were, with fresh bright green growth returning in wide patches along rivers and coasts and spreading outward over the ruins, the oceans clear blue again, normal white clouds, polar ice like the reference, a clean blue atmosphere rim, and on the night side complete darkness with NO city lights at all. Quiet and hopeful. Black space with faint stars. Cinematic near-photoreal space render. No text, no labels, no watermark.

## contact-sheet.jpg

The seven states, in the order above, in a 4 × 2 grid. The last cell is the lock check: the cyan healthy coastline drawn over nuclear winter.

## Known misses

- **Warming:** the higher seas are subtle. The coastlines sit almost where they do in the healthy frame, so the sea-level rise reads mainly through the shrunken ice and the storms. In the build, flooded coastal cells should show the silty shallows colour (`#5e8f84` with `#8a7a5a`).
- **Lighting:** the warming frame has a stronger warm glare on the left limb than the others. The sun direction is the same.
- **Wasteland:** the ruin fields use a coarse blocky texture that is busier than the healthy frame.
- **Surface texture:** the generator's fine "tiled" surface texture is visible on the land in all frames. It is a generator artifact, not a target.
- **No moon change:** the moon is unchanged in every frame, by design.
