# Concept art: landscapes and biomes on the way down (T-008, Grok)

There are 14 images here, all 1280×720 PNG, 1.1–1.7 MB each, with no text or watermarks:

- **Descent series** — five altitudes over one place, a forested river valley meeting the sea.
- **Biomes** — nine golden-hour biomes at about 1–2 km.
- **Two helper sheets:**
  - `descent-continuity.jpg` — the series side by side, with the gold box showing the area used as the reference for the next image.
  - `contact-sheet.jpg` — all 14 images.

## How these were made

Every image is original, made with Grok Bot's image generator from our own prompts (quoted exactly
below). No game or named artist was imitated.

**The descent was generated as a chain:**

1. `descent-1-400km` used the approved `shots/ue-cloudlab-terminator-final.png` as its reference. This gives it the same ocean navy, land palette, cumulus and warm low sun as the orbit image.
2. Each later image used a crop of the previous image as its reference. The crop is the gold box in `descent-continuity.jpg`, cut out and upscaled to 1280×720. The prompt then asks for "exactly its composition".
3. Because of this, the coastline, river course, river mouth, islet, beach and village carry through from one image to the next.

**Light:** every image has warm late-afternoon sun from the left, with long soft shadows.

**Continuity check** (see `descent-continuity.jpg`):

- 400 km → 50 km: the bay, the river's S-course and the rocky island at the mouth are kept. The 50 km image adds a second small islet that is too small to see at 400 km.
- 50 km → 10 km: the river mouth, the sand spit on its left, the curved shore on the right and the small round islet are kept.
- 10 km → 2 km: the beach strip, the riverside village and the river are kept.
- 2 km → 300 m: the lower village and the river bank are kept.

**The altitudes are approximate.** Estimated frame widths, judged from visible features:

| Image | Estimated frame width |
|---|---|
| 400 km | about 400–600 km |
| 50 km | about 50 km |
| 10 km | about 5–8 km |
| 2 km | about 1.2 km (houses about 12 px) |
| 300 m | about 300 m (houses about 40 px) |

The 10 km and 2 km frames are a little tighter than their labels.

**Colour notes.** Hex values are sRGB medians measured from the images:

- shade and lit vegetation = the 20th and 80th luminance percentiles
- "water" = blue-dominant dark pixels
- "shallows" = cyan pixels

They are targets for the *final, lit, tonemapped* look, not albedos. The lab's albedo values are in
`docs/lab/surface.md`. The lab shows the orbit end of this look working; these images mostly show
what should happen from about 10 km down.

---

## Descent series

### descent-1-400km.png — the whole region
- **What it shows.** About 500 km of coast, seen as if from a space station:
  - a large bay of the sea at lower left, closed by a headland
  - forested hills and ridges with a few snow-dusted summits at upper left
  - a single river as a thin thread with one S-bend into a tiny delta at the head of the bay, with a small island at the mouth
  - scattered cumulus and a broad sun glint on the bay
- **Prompt** (reference: `shots/ue-cloudlab-terminator-final.png`):
  > Satellite photograph from 400 km altitude, like an astronaut photo from a space station, looking down at a coastal region about 700 km wide, in the look of the reference planet image (deep navy ocean, tan-and-green land, bright white cumulus, warm low sun from the left). At this scale the land is a soft mosaic of dark green forest and tan hills with fine ridge texture; individual trees are invisible. A large bay of the sea occupies the lower left third, its coast a smooth curve with a pale turquoise rim of shallows. From the top right, a forested valley descends toward the bay and a single river shows only as a thin dark hair-line thread with one distinct S-bend before reaching a tiny fan-shaped delta at the head of the bay; one small island lies just offshore of the delta and a curved headland closes the bay on the left. Scattered small cumulus clouds and their shadows over the land, a broad soft sun glint on the sea at lower left, faint blue atmospheric haze toward the edges. Near-photoreal, physically based, cinematic. No text, no labels, no watermark.
- **Engineer notes.**
  - **Colour:**

    | Element | Hex |
    |---|---|
    | Deep sea | `#223d53` (darkest `#0d2d4c`) |
    | Shallows rim | `#3a5e76` → `#134a5b` |
    | Land (lit / shade) | `#616558` / `#3b4035` |
    | Dry hills | `#83735e` |
    | Cloud | `#d8d3d0` |

  - **Land at this altitude:**
    - Land is desaturated grey-olive, not green. Forest should be about 0.6× the macro colour, which matches the lab's 0.62 sub-pixel canopy average.
    - Texture scale is set by **terrain relief**: ridges every 2–10 km. There is no tree texture at all.
  - **Water:**
    - The river reads as a 1–2 px dark line, darker than the land and never blue. Keep it visible at this scale; it is the thing the eye follows.
    - The coast reads as a thin turquoise rim, 2–4 px.
    - Sun glint is broad and soft, spanning about a quarter of the frame.
    - No surf is visible.
  - **Snow:** only on the highest summits, as white flecks.

### descent-2-50km.png — the bay head
- **What it shows.** The head of the bay:
  - the river now a clear dark-blue line winding down to the mouth
  - the rocky island off the mouth, plus a small islet
  - a pale beach sweeping in both directions
  - the valley floor as a patchwork of forest, meadows and small fields, with forested ridges either side
- **Prompt** (reference: crop of descent-1):
  > Re-render the reference image as a sharp, detailed aerial photograph from 50 km altitude, keeping exactly its composition, coastline shape, river course and island positions: the curved bay shoreline on the left and bottom, the river winding down from the top right with the same bends into a small fan-shaped delta at the bottom centre, the small rocky island just offshore of the mouth and the tiny islet to its left. Add the detail visible at 50 km: the river is a clear dark-blue line a few pixels wide with a slightly lighter sandy floodplain, the valley floor is a mosaic of dark green broadleaf forest patches, lighter green meadows and small tan fields, forested ridges show crisp terrain relief with shadowed gullies, a few tiny pale town patches along the river. Sea deep navy grading to a turquoise band of shallows, thin white surf along a pale sand beach. Same warm late-afternoon light from the left, soft haze. Near-photoreal, cinematic, physically based. No text, no labels, no watermark.
- **Engineer notes.**
  - **Colour:**

    | Element | Hex |
    |---|---|
    | Sea | `#0f3955` |
    | Shallows band | `#295f69` (lightening to about `#2f7c80` against the beach) |
    | Land (lit / shade) | `#575b4b` / `#3a4034` |
    | Fields and dry ground | `#7f6d55` |

  - **Land:**
    - Canopy is a **velvety mottling at about 0.5–2 km scale** (forest patches and clearings). Individual crowns are still invisible. This matches the lab's crown clumps at 130 m / 47 m, which here should average out.
    - Field patchwork is 0.3–1 km.
  - **Water and shoreline:**
    - The river is 4–8 px and should be the most saturated blue on land.
    - The shallows band is about 1–2 km wide.
    - The beach reads as a continuous 1–2 px bright line.
    - Surf is not yet separable from the beach.

### descent-3-10km.png — the river mouth
- **What it shows.** The river reaches the sea:
  - a long sand spit and beach on its left
  - a small village between beach and river
  - forest with meadows and tracks on the right bank
  - a curved sandy shore at lower right
  - the small round islet with a green top and a surf ring
- **Prompt** (reference: crop of descent-2):
  > Re-render the reference image as a sharp, detailed aerial photograph taken from 10 km altitude, looking down at a slight angle, keeping exactly its composition: the river running from the top centre down to its mouth at the bottom centre, the long pale sand beach and spit on the left side of the mouth, the curved bay shore on the lower right, and the small round rocky islet in the sea at lower left. Add the detail visible at 10 km: the forest is now a lush green broadleaf canopy whose tree crowns read as a fine bumpy texture, broken by meadows, a few small fields and dirt tracks; the river is a broad dark blue-green channel with pale sandbars and a muddy plume where it enters the sea; a small village of light roofs near the river mouth; turquoise shallows with visible sandy bottom, two or three thin white surf lines along the beach; the islet has a green top and white surf ring. Same warm golden late-afternoon light from the left with long soft shadows.  Near-photoreal, cinematic, physically based. No text, no labels, no watermark.
- **Engineer notes.**
  - **Colour:**

    | Element | Hex |
    |---|---|
    | Sea | `#095367` |
    | Shallows | `#19646d` |
    | Canopy (lit / shade) | `#5b6442` / `#253425` |
    | Canopy median | `#3b452f` |
    | Beach | `#d9c4b1` (sunlit) / `#917b5d` (median) |
    | River | `#243e43` |

  - **Canopy:**
    - Canopy **turns green here**, with clearly more saturation than at 50 km.
    - It reads as **crown clusters 20–60 m across**, a bumpy texture with one shadowed side, on a dark base.
    - Canopy density is about 85 % cover, with meadows as distinct lighter shapes.
  - **Water and shoreline:**
    - The shoreline has three bands, from sea to land: turquoise shallows (100–300 m), 2–3 thin white surf lines, then the beach.
    - The beach is the brightest thing in the frame.
    - The river shows a mud plume into the sea.
  - **Village:** a grey-white speckle.

### descent-4-2km.png — village between beach and river
- **What it shows.** The beach, dunes and surf on the left; the village in its trees in the middle; the river with a sandy bank path on the right; forest beyond.
- **Prompt** (reference: crop of descent-3):
  > Re-render the reference image as a sharp, detailed aerial photograph from 2 km altitude, looking down at a slight angle, keeping exactly its composition: the sea and surf on the left, the long pale sand beach and dune strip running top to bottom, a strip of forest with a small village of light-coloured houses with terracotta and grey roofs in the middle, and the broad dark river curving down on the right with a sandy bank path, more forest beyond it. Add the detail visible at 2 km: individual broadleaf tree crowns are now visible as distinct rounded clumps of varied greens with dark gaps and shadows, small gardens and fields, dirt lanes between houses, small boats pulled up on the river bank, a wooden jetty, the river water dark teal with gentle ripples and reflections of the sky, waves breaking in several white lines on the beach, wet sand darker at the water's edge, turquoise shallows fading to deeper blue. Warm golden late-afternoon light from the left with long soft shadows of trees and houses. Near-photoreal, cinematic, physically based. No text, no labels, no watermark.
- **Engineer notes.**
  - **Colour:**

    | Element | Hex |
    |---|---|
    | Shallows | `#216f70` |
    | Deeper water | `#133a47` |
    | Beach | `#ac8e6d` (dry sand lit about `#c19873`) |
    | Canopy (lit / shade) | `#58613b` / `#1e2e1f` |
    | Roofs | terracotta about `#a0603f`, grey about `#9a9a94` |

  - **Trees:** **individual crowns, 8–15 m (about 6–12 px)**, packed edge to edge, with dark gaps. The lab's 10 m Worley crowns are the right size here.
    - This is where the instanced trees must have taken over from the painted canopy (the lab's merge is at about 3 km).
    - Canopy density is 90 % in the woods and 40–60 % among the houses.
  - **Water and shoreline:**
    - Surf shows as 3–5 parallel white lines with foam streaks.
    - Wet sand is a darker band 10–20 m wide at the water's edge.
    - The river is dark teal, noticeably darker than the sea shallows, with a soft sky sheen.

### descent-5-300m.png — riverside village
- **What it shows.** Houses with terracotta and grey roofs among broadleaf trees, a dense band of riverside trees, a reed edge, a small wooden boat, and the dark river on the right.
- **Prompt** (reference: crop of descent-4):
  > Re-render the reference image as a crisp, detailed aerial photograph from 300 m altitude, looking down at a slight angle, keeping exactly its composition: houses with terracotta and pale grey roofs among trees on the left two thirds, a band of dense riverside trees in the middle, and the broad dark river along the right side with its bank running diagonally from upper right to lower centre. Add the detail visible at 300 m: each broadleaf tree is a distinct rounded crown with individual leaf clumps, varied greens from olive to deep green with golden sunlit tops, grass and small vegetable gardens between houses, dirt paths, low stone walls, a few fruit trees, a muddy river bank with reeds and a small wooden boat, the river surface dark teal-blue with soft ripples, sky reflections and a faint sun glint. Warm golden late-afternoon light from the left with long soft shadows of trees and houses across the grass. Near-photoreal, cinematic, physically based. No people close up, no text, no labels, no watermark.
- **Engineer notes.**
  - **Colour:**

    | Element | Hex |
    |---|---|
    | Crowns (lit / shade) | `#383f2a` / `#122418` |
    | Sunlit crown tops | warm, up to about `#8a8a40` |
    | Grass | `#353729` |
    | Reeds | `#564e26` |
    | River | `#153741` |
    | Paths and bare ground | `#a88463` |
    | Roofs | about `#835844` |

  - **Trees:** each crown shows **leaf clumps of about 1–2 m**, with the sunlit side gold-green and the far side nearly black. This needs the lab's AO-darkened crown interior and two-sided foliage.
    - **Gap the lab already notes:** below about 1 km the ground needs a real **grass layer** and garden/field texture. A flat painted ground texture will not hold at 300 m.
  - **Water:** the river surface is dark, with small ripples and a faint glint; there is no visible bottom.
  - **Shoreline:** the bank is a 2–5 m reed and mud strip.

---

## Biomes (golden hour, about 1–2 km, oblique)

**Shared prompt pattern:** "Aerial photograph from about 1.5 km (or 2 km) altitude, oblique view toward the horizon … Low warm sun from the left … Near-photoreal, cinematic, physically based. No text, no labels, no watermark, no people, no roads."

Each prompt is quoted in full below. None used a reference image. Prompts name a biome's real-world
look (e.g. "African-style savanna"), never a game or artist.

### biome-temperate-broadleaf.png
- **What it shows.** Rolling hills under continuous oak, beech and maple canopy with a few early-autumn trees; a sunlit stream; meadow clearings; mist in the valleys; blue receding ridges.
- **Prompt:**
  > Aerial photograph from about 1.5 km altitude, oblique view toward the horizon, of a temperate broadleaf forest landscape at golden hour: rolling hills covered in a dense continuous canopy of oak, beech and maple crowns in many greens with a few early-autumn yellow and copper trees, a small meandering stream with glints of sun, a few clearings with meadows, low mist in the valleys, a distant hazy ridge line and a curved horizon under a clear pale sky with a few cumulus clouds. Low warm sun from the left, long tree shadows, soft atmospheric perspective turning distant hills blue. Near-photoreal, cinematic, physically based, same planet look as a living earth-like world. No text, no labels, no watermark, no people, no roads.
- **Engineer notes.**
  - **Colour:**

    | Element | Hex |
    |---|---|
    | Canopy (median / shade / lit) | `#283120` / `#151f12` / `#474c37` |
    | Autumn accents | about `#8a5a2a` |
    | Meadows | `#696d4a` |
    | Valley mist | `#d5bdaa` |
    | Far ridges | `#a1a2a0` |

  - **Canopy:** density about 95 %. Crowns are 10–15 m and form a **cauliflower texture**, with a 1-in-30 colour variation (autumn) that breaks the repetition.
  - **Distance:** atmospheric haze does most of the depth. By 5 km out, ridges are grey-blue.
  - **Water:** the stream reads only where it catches the sun.
  - **Note:** the camera here is lower than 1.5 km (crowns are large in the foreground); treat the foreground as about 500 m.

### biome-boreal-conifer.png
- **What it shows.** Dark spruce and fir taiga on glacial terrain, with many small dark lakes and bogs, a winding river with a golden bog meadow, yellow birch stands, and mist.
- **Prompt:**
  > Aerial photograph from about 1.5 km altitude, oblique view toward the horizon, of a boreal conifer forest at golden hour: endless dark spruce and fir forest of narrow pointed crowns on gentle glacial terrain, dotted with many small dark lakes and bogs reflecting the sky, a winding river with sandbars, pale birch stands in yellow-green patches, light mist over the water, distant low hills fading to blue haze and a clear horizon with thin clouds. Low warm sun from the left casting long narrow shadows of every spruce.  Near-photoreal, cinematic, physically based. No text, no labels, no watermark, no people, no roads.
- **Engineer notes.**
  - **Colour:**

    | Element | Hex |
    |---|---|
    | Conifer (median / shade / lit) | `#2a2f1c` / `#131a0b` / `#4a4e3a` |
    | Bog and sedge meadow | `#927a4c` |
    | Birch | about `#917619` |
    | Lakes | dark `#25404c`, or sky reflections `#96b9d3` |

  - **Canopy:** density 60–75 %. It is **darker and cooler** than broadleaf, with clear gaps.
  - **Silhouette:** the conifer **spike silhouette and its long thin shadow** carry the biome. At grazing light, shadows are 3–5× the tree height.
  - **Water:** lakes are near-black unless they reflect the sky, so the Fresnel reflection is what matters.

### biome-rainforest.png
- **What it shows.** Unbroken, very bumpy canopy with emergent giants and flowering crowns, a muddy meandering river with oxbows, rising mist, and towering cumulus.
- **Prompt:**
  > Aerial photograph from about 1.5 km altitude, oblique view toward the horizon, of a tropical rainforest at golden hour: an unbroken, very bumpy canopy of huge cauliflower-shaped tree crowns in deep saturated greens with a few emergent giants and flowering trees in pale yellow, a brown muddy river with wide oxbow meanders cutting through, wisps of low cloud and mist rising from the forest, towering cumulus building in the distance, a hazy horizon. Low warm sun from the left lighting crown tops gold and leaving deep shadows between crowns. Near-photoreal, cinematic, physically based. No text, no labels, no watermark, no people, no roads.
- **Engineer notes.**
  - **Colour:**

    | Element | Hex |
    |---|---|
    | Canopy (median / shade / lit) | `#2b371f` / `#0f1e0e` / `#555c40` |
    | Flowering crowns | about `#b8a060` |
    | River | brown, about `#8a6a45` lit |
    | Mist | `#b8a083` (warm) |

  - **Canopy:**
    - Density is 100 %, but it reads through **strong crown relief**: crowns 15–30 m, emergents 1.5× taller, and deep shadows between them.
    - The lab says rainforest is "too heavy". This image suggests the fix is more **height variation and shadowed gaps**, plus **mist**, rather than a darker colour.
  - **Water:** the river is opaque brown, not blue.

### biome-savanna.png
- **What it shows.** Flat golden grassland with widely spaced flat-topped acacias and their long shadows, a green ribbon of gallery forest along a seasonal river, and a line of rocky kopjes on the horizon.
- **Prompt:**
  > High aerial photograph from about 2 km altitude, oblique view toward the horizon, of an African-style savanna at golden hour: vast flat golden-tan grassland dotted with scattered flat-topped acacia trees and small dark shrub clusters spaced far apart, each tree casting a long shadow, a seasonal river lined with a ribbon of greener gallery forest winding across the plain, a few pale termite-mound dots and game trails, a distant line of rocky kopje hills and a hazy curved horizon with a few small clouds. Low warm sun from the left, dusty golden atmosphere. Trees are small at this altitude. Near-photoreal, cinematic, physically based. No animals close up, no text, no labels, no watermark, no people, no roads.
- **Engineer notes.**
  - **Colour:**

    | Element | Hex |
    |---|---|
    | Grass | `#ae7f46` (sunlit about `#c99d6a`, shade `#8b6536`) |
    | Trees | `#2b2e1e` |
    | Gallery forest | `#3d3c30` |
    | Haze | `#c8a27a` |

  - **Tree cover:** 2–5 %, as **isolated dots spaced 30–100 m apart**, each with a shadow longer than its crown. The shadow is what makes the biome read; do not cluster the trees.
  - **Grass:** reads as a fine streaky texture at about 5–20 m, from game trails and patches.
  - **Water:** the river is invisible; only the gallery-forest ribbon shows it.

### biome-desert-dunes.png
- **What it shows.** A field of large crescent and linear dunes with sharp lit crests and shadowed slip faces, ripple texture, a dark rocky plateau edge and a dry wadi with a few shrubs in the foreground.
- **Prompt:**
  > High aerial photograph from about 2 km altitude, oblique view toward the horizon, of a sand desert at golden hour: a sea of large crescent and linear dunes in warm ochre and apricot sand, sharp sunlit crests and deep blue-violet shadowed slip faces, fine ripple texture on the dune flanks, a dark rocky plateau edge and a dry wadi with a few tiny green shrubs in the foreground, the dune field fading into dusty haze toward a clean horizon under a pale sky. Low warm sun from the left. Near-photoreal, cinematic, physically based. No text, no labels, no watermark, no people, no vehicles.
- **Engineer notes.**
  - **Colour:**

    | Element | Hex |
    |---|---|
    | Sand (median) | `#a87548` |
    | Lit crests | about `#d9a06a` |
    | Slip-face shadows | about `#6e4a35` (warm, with a slight violet tint) |
    | Rock | `#57433a` |
    | Sky / haze | `#e9e3d9` |

  - **Dunes:** wavelength 100–400 m, as a **heightfield, not a texture**. The dunes only read through grazing light and **sharp crest lines**. Ripple detail is about 1 m and only matters below about 300 m.
  - **Vegetation:** none.
  - **Colour vs. the lab:** the lab's sand albedo (0.60, 0.52, 0.36) is too yellow-grey for a dune sea. Push it toward ochre.

### biome-tundra.png
- **What it shows.** Treeless rolling tundra in olive, rust and ochre, with permafrost polygons, many thaw ponds, a braided grey river with gravel bars, lingering snow patches, and low hills with snow streaks.
- **Prompt:**
  > High aerial photograph from about 1.5 km altitude, oblique view toward the horizon, of arctic tundra at golden hour in late summer: flat to gently rolling treeless land in muted olive, rust-red and ochre low vegetation, a polygon pattern of permafrost cracks, countless small shallow ponds and thaw lakes reflecting the sky, a braided grey-green river with gravel bars, scattered lingering snow patches in hollows, a distant low range of bare hills with snow streaks, a cool clear sky. Low warm sun from the left skimming the ground, long shadows. Near-photoreal, cinematic, physically based. No trees, no text, no labels, no watermark, no people, no roads.
- **Engineer notes.**
  - **Colour:**

    | Element | Hex |
    |---|---|
    | Ground (median) | `#81674a` |
    | Rust | about `#7a3e25` |
    | Olive | about `#6b6a3a` |
    | Ponds | sky `#8bb1d1`, or dark about `#2a2d30` |
    | Snow patches | `#dadcdb` |
    | Braided river | grey `#61615d` |

  - **Ground:** **patchy mosaic at 20–200 m**, plus a polygon crack network of 10–30 m cells. There is no canopy; ground colour does all the work.
  - **Water:** ponds are the key read, small and very numerous, alternating between mirror-bright and black depending on the sky reflection.
  - **Snow:** only in hollows and on lee slopes, as irregular blobs.

### biome-alpine-glacier.png
- **What it shows.** Jagged granite peaks with snowfields on the gentler slopes and bare dark rock on the steep faces, two glaciers with crevasse bands and moraine stripes, a milky turquoise lake, and conifers only in the lowest valley.
- **Prompt:**
  > Aerial photograph from about 2 km above a high alpine mountain range at golden hour, oblique view: jagged grey granite peaks and knife-edge ridges with layered rock strata, snowfields on the gentler slopes and summits while the steepest rock faces stay bare and dark, two large glaciers with crevasse bands and dark medial moraine stripes flowing down valleys, a milky turquoise glacial lake below, dark green conifer forest only in the lowest valley, scree fans. Low warm sun from the left turning the snow on sunlit faces peach-gold and leaving shadowed snow cool blue. Clear sky, light haze between ranges. Near-photoreal, cinematic, physically based. No text, no labels, no watermark, no people, no buildings.
- **Engineer notes.**
  - **Colour:**

    | Element | Hex |
    |---|---|
    | Lit snow | `#e6cdae` (peach) |
    | Shadowed snow | `#8497aa` (blue) |
    | Glacier ice | `#8694a5` |
    | Rock | `#3d424a` |
    | Lake | `#4b7684` |
    | Valley forest | near-black `#07141e` |

  - **Snow:**
    - **Snow is removed from faces steeper than about 40°**, exactly as in the lab rule. That contrast between white snow and dark rock is the whole look.
    - Lit snow takes on the sun's colour, while shadowed snow takes the sky's blue. Do not render it as flat white.
  - **Glaciers:** 1–2 km wide flows with **transverse crevasse bands every 20–50 m** and dark moraine stripes along them.

### biome-wetland-delta.png
- **What it shows.** A river splitting into a fan of channels through bright green reed marsh, with mudflats, sandbars and a brown sediment plume into a sea lit by a glittering sun path.
- **Prompt:**
  > Aerial photograph from about 1.5 km altitude, oblique view toward the sea, of a wetland river delta at golden hour: a river splits into a branching fan of many channels winding through bright green reed marshes, mudflats and pale sandbars, dark lagoons and tidal creeks reflecting the gold sky, patches of low shrubby trees on the higher levees, the channels open into a brownish sediment plume that fades into blue sea at the horizon. Flocks of birds as tiny specks. Low warm sun from the left with a glittering sun path on the water. Near-photoreal, cinematic, physically based. No text, no labels, no watermark, no people, no boats, no buildings.
- **Engineer notes.**
  - **Colour:**

    | Element | Hex |
    |---|---|
    | Marsh (median / shade / lit) | `#2b3112` / `#13190a` / `#51571d` (a yellow-green, distinct from forest) |
    | Mudflats | about `#5a4632` |
    | Sandbars | `#c7ab82` |
    | Sediment plume | `#a17f5f` |
    | Sea (sun path) | `#fcf9ee` highlights |

  - **Channels:** a **dendritic network**: 2–5 orders of branching, from about 200 m wide down to about 5 m.
  - **Marsh:** smooth, short-pile texture, with no crown bumps. It is the only flat-textured green.
  - **Water:** channels read dark, or mirror the sky; their edges are mud, not sand.
  - **Note:** the sun here is ahead of the camera rather than at the left, for the sun path.

### biome-coral-coast.png
- **What it shows.** A green hilly island with a palm-fringed beach, a wide turquoise lagoon with dark coral heads, a barrier reef with a continuous line of surf, and deep navy ocean beyond.
- **Prompt:**
  > Aerial photograph from about 1.5 km altitude, oblique view, of a tropical coral coast at golden hour: a green hilly island coast with palm-fringed white sand beaches, a wide lagoon of clear turquoise and jade shallows over pale sand with dark coral heads and patch reefs visible through the water, a fringing barrier reef line where white surf breaks in a thin continuous band, then a sharp drop to deep navy ocean beyond. Low warm sun from the left adding a golden sheen and long shadows from the hills. Near-photoreal, cinematic, physically based. No text, no labels, no watermark, no people, no boats, no buildings.
- **Engineer notes.**
  - **Colour:**

    | Element | Hex |
    |---|---|
    | Deep ocean | `#133e5b` |
    | Lagoon | `#388c86` (brightest about `#3fb2a6`, over sand) |
    | Coral heads | `#104c4d` |
    | Reef surf | about `#e8ecef` |
    | Island hills | `#6a522f` lit grass, `#2e3a1e` forest |

  - **Water:**
    - Water colour must come from **depth**: sand under 1–3 m reads jade/turquoise, coral heads read as dark blots, and the reef drop-off is a **hard edge** to navy. This is the same rule as the lab's "turquoise shallows", made stronger.
    - The surf line is continuous, 5–15 m wide.
    - Coral heads are 5–50 m patches.
