# Ground lab: farmland, hedgerows, forest edges, villages and roads at 2–20 km

Lab: `UnrealLab/GroundLab` (a private copy of `UnrealLab/SurfaceLab`; module name kept as `PlanetLab`).
Shots: `shots/ue-groundlab-*.png`. Written for the C1 `planet` engineer (SPEC-UNREAL §2), who owns
`Game/Planet/` and `Scripts/planet/land.hlsl`. Builds on `docs/lab/surface.md`: read that first.

## 1. Result in one paragraph

The production ground at 2–20 km was a flat board of blocky fields (`shots/ue-play-09-early-village.png`):
rectangular field grids in 1.6 km "estates", one colour per field, no hedges, villages as markers. The lab
replaces it with a land-use layer in the land material that turns three smooth inputs (a farm-intensity field,
a settlement-core field and a road network) into **holdings and fields that follow the terrain and the rivers**.
Holdings are ~1 km warped Voronoi cells. Each holding takes its field orientation from the contours on slopes,
lays long lots perpendicular to a river on flood plains, and otherwise shares a smooth regional orientation with
its neighbours. Inside a holding, fields are either **furlongs of parallel strips** whose ends line up, or
**irregular bocage polygons**. Their size depends on age and region. Every field decides on its own whether
it is cultivated, so farmland **frays field by field** into meadow, woodlot and wild land, and forest clearings
take field-shaped edges. Fields carry crops by season and climate, grass balks, hedgerows with trees and
sun-side shadows, ridge-and-furrow or tramlines, and harvest lanes. Villages are a ragged built-up core of
house plots, gardens and orchard trees. Roads are routed by A* over a 62 m height grid with a grade-squared
cost, so they take valleys and contours, cross rivers rarely, and merge into one network. They are drawn
crisply from a signed distance field. A grade at aerial-perspective scale 0.25 removes the teal cast (forest
b* −1.8 → +5.1) without going tan.

Honest verdict: from 20 km to about 2 km this reads as lived-in country seen from the air, with no visible
tiling. It now has the *structure* of the concept (`descent-3-10km`, `descent-4-2km`). It is still not
photographic close up: at 2 km the fields are clean-edged paint (no crop texture beyond rows, tramlines and
noise), hedges have no geometry, and the village is a ground pattern standing in for the towns module's meshes.

## 2. Shots

Series at 20, 10, 5 and 2 km altitude (pitch 60°, FOV 50°, sun 32° from the camera's left, `-ev=-1 -apscale=0.25`):

| site | where | sheet | single shots |
|---|---|---|---|
| farmland (grassland, river, village) | 34.36, 158.10 | `ue-groundlab-sheet-farm.png` | `ue-groundlab-farm-20/10/5/2.png` |
| forest edge (clearing round a village in broadleaf forest) | 39.455, 158.828 | `ue-groundlab-sheet-edge.png` | `ue-groundlab-edge-20/10/5/2.png` |
| coast (forest coast, river mouth, dunes) | 34.93, 168.60 (heading 90) | `ue-groundlab-sheet-coast.png` | `ue-groundlab-coast-20/10/5/2.png` |
| side by side with the concept | | `ue-groundlab-cmp-10km.png`, `ue-groundlab-cmp-2km.png` | |
| seasons, age, climate | farmland 5 km | `ue-groundlab-sheet-variants.png` | `-spring-5`, `-farm-5` (summer), `-autumn-5`, `-modern-5`, `-dry-10` (irrigated valley), `-hills-5` (contour strips) |
| continuity from 40 km | | | `ue-groundlab-farm-40.png` |
| debug: land model (farm green, cores red, roads white, rivers blue) | | | `ue-groundlab-map-farm.png` |
| debug: holding mode / orientation | | | `ue-groundlab-dbg1.png`, `-dbg2.png` |
| before (lab baseline, same view) | | | `ue-groundlab-base-farm-10.png` |

## 3. Running it

```
powershell -NoProfile -ExecutionPolicy Bypass -File UnrealLab/GroundLab/build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File UnrealLab/GroundLab/mats.ps1        # M_SurfLand + M_SurfLandNoGround (A/B)
bash UnrealLab/GroundLab/gseries.sh <prefix> <lat> <lon> <heading> [args]               # 20/10/5/2 km (ONLY="10 2" limits)
powershell ... shot.ps1 <name> -look=lat,lon,distKm,heading,pitch,fov -sunl=elev,az -ev=-1 -apscale=0.25
```
Ground options: `-season=0..1` (0 spring, 0.42 high summer (default), 0.5 harvest, 0.68 autumn, 0.9 winter),
`-age=0..1` (0.35 default = pre-industrial; above 0.55 tramlines and harvest lanes, above 0.65 tarmac main roads,
fields grow with age), `-tilekm=64`, `-noground`, `-gdebug=1` (holding mode: red contour, blue river lots, green
regional), `=2` (long-axis orientation), `=3` (core / village mask / road), `-groundmap` (writes
`Saved/groundmap.rgb`, 1024² RGB of the tile), `-groundlist` (settlements in the tile with river/sea distance),
`-profilegpu` (ProfileGPU 10 frames before the shot; `profsum.sh <log>`), `-landmat=/Game/Surface/M_SurfLandNoGround.M_SurfLandNoGround`.

## 4. Inputs (what production must provide)

| input | lab source (stand-in) | production source | resolution needed |
|---|---|---|---|
| farm intensity 0..1 | `FGroundLand::FarmAt`: settlements' warped discs × suitability | the sim's per-cell `farm` (already in `DmPlanetMaps` MapC.r) | smooth; 173 m texels are plenty |
| settlement core 0..1 | distance to each settlement < `CoreR` | towns / `urban` (MapC.g), per settlement tier | smooth, ≤ 173 m |
| temperature °C | sim `Temp` | MapB.r | smooth |
| roads | A* in the lab (§7) | the sim's trade routes, routed the same way (§7) | polylines |
| smooth height | `Height(dir, 62.5 m)` on a 1024² grid | same function (DmPlanetTerrain) | ≤ 62 m |
| river distance | `RiverQuery` | river polylines (already there) | ~16 m |

Lab textures (`SurfacePlanet::BuildGround`, 1.3–1.6 s at load, all on the CPU):
- `LandTex`: global octahedral 2048² BGRA8 with box mips (173 m texels). R farm, G core, B road coverage
  (area × 4, for distant views), A temperature `(T+20)/60`.
- `TileH`: 1024² R32F smooth height over a 64 km camera-centred tile (62.5 m). Half floats are not enough:
  the contour coordinate divides height by the grade, so 0.25 m steps become 10 m kinks in field edges.
- `TileF`: 4096² RGBA16F over the same tile (15.6 m texels). R = road distance signed by side, G = unsigned road
  distance, B = road half-width, A = river distance (m, ≤ 2000).
- Tile mapping (gnomonic on the tangent plane at the tile centre C, axes E, N, R = 100 km):
  `x = R (d·E)/(d·C)`, `y = R (d·N)/(d·C)`, `uv = xy / (2 half) + 0.5`. Material parameters: `TileO = (C, half)`,
  `TileE = (E, coarse N)`, `TileN = (N, fine N)`, `Season`, `Age`, `GDebug`.

**Production note.** The tile is the lab's answer to the PMC limit of 4 UV pairs (surface.md §4). In
production use either (a) per-vertex channels (smooth height and road signed distance, which the production mesh
already partly carries: "road from the mesh (G)"), or (b) a camera-centred clipmap of 2–3 levels (1024² at
16/64/256 m), rebuilt asynchronously when the camera moves half a level. The road raster for a 64 km tile takes
~50 ms, and the river channel ~450 ms (it is a `RiverQuery` per texel: rasterize river segments instead).

## 5. The land-use material (`Scripts/hlsl/ground.hlsl`, called from `land.hlsl`)

One function, `Ground(...)`, returns the land-use colour and mask, a forest multiplier, the hedge cover and hedge
shadow, a normal offset, the road cover and colour, and the roof colour and cover. `land.hlsl` applies them in
this order: base colour → **fields** (`lerp(col, gCol, gMask)`) → forest canopy (density × `gForestK`) →
**hedges** and their shadows → **roofs** (over the canopy) → rock / sand / snow / rivers → **roads** (not over
river water). It is pasted into the Custom node after `common.hlsl`, like everything else in the lab. A
`#define GROUND_ON 0` variant (`M_SurfLandNoGround`) exists for cost A/B tests.

### 5.1 Holdings (estates)
- **3D Voronoi, `Le` = 1000 m**, sites jittered in 0.15..0.85 of the cell, on `Pm + 70 m · tangent(∇Noised(Pm/650 m))`.
  The warp makes holding borders and the tracks along them wander. Two passes: the nearest site, then the exact
  distance to its nearest bisector (27 + 26 hashes).
- Per-holding constants come **only from the site** (`cu = normalize(site)`). Anything that varies per pixel and
  scales the field coordinates stretches the fields. **Pitfall:** the first version scaled field size by the
  per-pixel settlement core and drew radial streaks ("sunbursts") round every village.
- **Orientation** (axial, one per holding):
  1. *Contour* where the smooth grade at the site, `gm`, exceeds 2–5 % (`contourW = smooth((gm−0.02)/0.03)`).
     The across-field axis is the gradient (4 `TileH` taps at ±1 coarse texel).
  2. *River long lots* on flats within 900 m of a river (`TileF.a` gradient over ±3 fine texels). The long side
     runs away from the river.
  3. *Regional*: `angle = 2.2π · Noised(cu · 100 km / 7 km) + (rnd − 0.5) · 0.6`. Neighbours share a direction
     (as along an old road or a valley), and each holding turns by up to ±17°. A purely random angle per holding
     read as a quilt of unrelated Voronoi cells.
- **Contour coordinate:** on slopes the across-field coordinate is `v = (Hs(p) − Hs(site)) / gm` (smooth height
  from `TileH`), blended by `contourW` and clamped to ±350 m of the planar `v`. Strip boundaries are then real
  contour lines that bend round spurs and hollows. The band-limited `TileH` keeps them identical at every LOD;
  the mesh height would change with the LOD level.
- **Layout:** strips (furlongs) with probability `1 − lerp(0.55, 0.25, age)`, else polygons. Contour holdings use
  strips 75 % of the time, and river lots always do.

### 5.2 Fields
- **Size:** `s0 = lerp(115, 300, smooth((age−0.2)/0.7)) m × lerp(0.8, 1.3, rnd) × lerp(0.8, 1.25, region) × lerp(0.65, 1, 1 − 1.5·core(site))`.
  Strips: `W = s0·lerp(1.5, 2.8)`, `H = s0·lerp(0.5, 0.75)`. Polygons: `W = s0·lerp(1, 1.6)`, `H = s0·lerp(0.75, 1)`.
  River lots: W × 1.4, H × 0.8.
- **Boundary warp** (one noise gradient): wavelength `3·max(W,H) + 100 m`, amplitude `(0.10 strips | 0.30 polygons)·min(W,H) + 4 m`.
  **Pitfall:** at 0.22·min(W,H) and a shorter wavelength, strip edges read as sine waves at 2 km.
- **Furlongs:** rows of height H across. The block ends along u lie at `W·(k + 0.2 + 0.6·rnd(k))` and are
  **shared by every row**, so strip ends line up as in real open fields. 30 % of strips split once more.
  **Pitfall:** per-row ends with tilted boundaries made staircase / comb edges and zig-zag hedge shadows.
- **Polygons:** anisotropic 2D Voronoi in `(u/W, v/H)`, jitter 0.02..0.98 (0.1..0.9 looked like a honeycomb),
  3×3 then 5×5 for the edge. Metric edge distance = bisector distance / |(n.x/W, n.y/H)|.
- **Per-field decision at the field's centre** (one `LandTex` tap at the centre; per-pixel farm would cut
  fields in half): with `fray = 0.22(nF + 0.6 nA) + 0.12 nE`,
  `cult = r1 < farm·1.3 − 0.08 + fray`, and `wild = r1 > farm·2.2 + 0.25 + fray` (untouched land).
  No crops where `gm > 0.12..0.22` (steep: grazing or wood) or within `25..60 m` of a river bank (riparian strip).
- **Uncultivated outfields** are meadow, or woodlots (`r3 < 0.18 + 0.5·sat(1.5·forest)`, planted density 1).
  Their forest multiplier is 0.35. Cultivated fields clear the forest (×0). Wild fields keep it (×1).
  Clearings in forest therefore get field-shaped edges, and the farmland edge frays into meadows and copses.

### 5.3 Crops (`CropColour`, linear albedo)
Class by climate, region (`frac(r2 + 0.35 nE + 0.15 nF)`) and chance:
cereal `lerp(0.42, 0.55, dry)·(1 − 0.5 cold)`, pasture `0.22 + 0.25 cold − 0.1 dry`, hay `0.10 + 0.08 cold`,
row crops `0.12 (1 − cold)`, fallow 0.08, the rest oilseed (pasture when cold).
Each field gets a season phase offset of ±0.03.

| class | spring (0–0.15) | early summer (0.2–0.33) | high summer / harvest (0.4–0.5) | autumn (0.6–0.8) | winter |
|---|---|---|---|---|---|
| cereal | soil → green (0.060–0.085, 0.105–0.120, 0.032–0.040) | green → gold | gold (0.235–0.270, 0.200–0.230, 0.098–0.125) → stubble (0.235, 0.210, 0.145) | ploughed (soil × 0.7–0.9) or stubble | sown: plough/green |
| pasture | green | green (dry tan (0.19, 0.17, 0.085) in arid summers) | | | dull (0.085, 0.095, 0.05) |
| hay | green | cut, pale (0.15, 0.16, 0.07) at 0.28–0.42 | regrowth | | dull |
| row crops | ploughed | dark green (0.040, 0.080, 0.026) | dark green → yellowing | ploughed | |
| fallow | soil with weeds (0.11, 0.105, 0.06) | | | | |
| oilseed | yellow (0.48, 0.40, 0.045) at 0.10–0.19 | green | | ploughed | |

Warm climates (>18 °C) shift ×(1.06, 1, 0.9). Regional soil: brown loam (0.135, 0.100, 0.070), red earth
(0.150, 0.088, 0.058) and grey clay (0.115, 0.105, 0.088), by `nE`/`nF`, ×1.4 paler where arid.
Inside a field: tone ×0.88..1.12 per field; noise at 70 m (0.16) and 23 m (0.08) seeded per field, plus nB/nC/nD;
greener damp patches; a compacted headland band within 4–7 m of the edge (+10 %); a grass balk 1.2–2.6 m wide;
crop rows at 0.8 m; ridge and furrow at 11 m (age < 0.5, also as a normal offset 0.10); tramlines every 21 m
(age > 0.55); combine lanes parallel to the edges during harvest (age > 0.55, season 0.43–0.49).
**AA:** below a pixel the field mosaic fades to an average of cereal and pasture per holding
(`AAW(min(W,H), fp)`), so 20 km keeps a mottled farmland tone instead of shimmering.

### 5.4 Hedgerows, tracks and shadows
- Hedge on an edge if `rnd(edgeId) < bocage = sat(0.35 + 0.5 nE + 0.35 (1 − dry) − 0.6 age)`. The edge id is
  hashed from both sides (strip row or block index; polygon site pair), so both fields agree.
  Width 1.8–3.2 m, drawn as line coverage `sat((max(w, fp/2) − d)/(0.6 fp) + 0.5) · min(1, w / max(w, fp/2))`
  (it dims instead of vanishing when sub-pixel). Colour (0.030, 0.050, 0.020).
- **Hedge trees:** 7 m crowns kept where `crown random > 0.45` within 2–4.5 m of the edge, with dome normals.
- **Hedge shadow without geometry:** the edge normal `eNw` comes from the layout. When `dot(eNw, sunT) > 0` the
  hedge stands between the field and the sun, and the field is shadowed (×0.5) for
  `5 m · cot(sun elevation) · dot(eNw, sunT)` beyond the hedge. That is the long thin shadow the concept shows at low sun.
- **Tracks:** 45 % of holding edges, 1.6 m, (0.20, 0.175, 0.13).

### 5.5 Forest edges and open land (`land.hlsl`)
- Density: `fden = sat((1.45·forest + 0.5·(0.45 nF + 0.35 nA + 0.30 nB + 0.12 nC) − 0.28)/0.32)`, a margin
  broken by clumps and glades at 700/180/45/11 m. Then × `gForestK`, or, when `gForestK > 1` (planted:
  woodlot 2.0, village orchard 1.75, garden trees 1.35), `max(0.5 fden, sat(gForestK − 1 + 0.3 nB))`.
- **Trees thin out one by one** at margins: a crown is kept if `frac(rnd·5.31) < 1.25·dens + 0.05`, so the
  edge is ragged individual trees, not a blurred outline.
- **Crown clusters (34 m Worley domes)** carry the 5–20 km read ("crown clusters 20–60 m with a shadowed side",
  concept 10 km). Colour `lerp(0.22, 0.78·tint) × macro`, tint 0.80–1.22, dome normal strength 0.9.
  Individual crowns (5.5/10 m, from surface.md) take over below 6 m footprint.
- **Shrubs and lone trees on open land:** 26 m Worley, present where `frac(rnd·13.7) < shrubD`,
  `shrubD = sat(0.05 + 0.3 forest + 0.12 (1 − arid) + 0.12 nA) · smooth(1.2 nF + 0.8 nA + 0.15)` (clumped
  along broad patches), × the field's forest multiplier, so there are none in crops. Without this, grassland
  is felt. Uniform density read as pepper.

### 5.6 Villages (ground only; the towns module places buildings)
- Edge: `ck = smooth((core − 0.30 + 0.16 nB + 0.10 nA + 0.05 nC)/0.04)`, an organic, ragged edge.
  A per-plot step looked pixelated, and a plain smoothstep looked like a disc.
- 18 m plots in the holding frame. A house with probability `0.12 + 0.55·core²`, roof 0.22–0.34 × 0.16–0.26 of
  the plot: terracotta (0.30, 0.13, 0.07) 45 %, grey (0.36, 0.355, 0.34) 35 %, ochre (0.38, 0.31, 0.21) 20 %, ×0.85–1.15.
  Garden plots: beds (2.2 m stripes) 30 %, grass 55 %, bare 15 %, a yard round each house, fence lines. Garden trees
  1.35, orchards 1.75. Roofs are applied **after** the canopy. Below 18 m/1.0 px they fade to their area share.
- This doubles as the far LOD for the towns module (beyond ~5 km a house is ≤ 2 px). Close up, the towns
  module's meshes replace the roofs, but keep the gardens and trees.

### 5.7 Roads (drawing)
- `TileF` signed distance: `line = sat((max(hw, 0.6 fp) − |sd|)/max(0.75 fp, 0.3) + 0.5) · min(1, hw / max(hw, 0.6 fp))`,
  only where the **unsigned** distance is `< hw + 0.75 texel`. This rejects the false zero crossings where two
  reaches meet with opposite signs.
- From `fp > 0.8 texel` it crossfades to `LandTex.b` coverage × 0.35 (a faint trace from far away).
- Dirt (0.17, 0.145, 0.105) → main roads (0.25, 0.215, 0.16), with wheel ruts at ±0.9 m; tarmac
  (0.065, 0.065, 0.068) for main roads once age > 0.65.

## 6. Colour grade (matching `ue-cloudlab-terminator-final.png`, no pink or tan)

Measured with CIE L*a*b* on 160×90 downsamples (`stats.py` in the lab notes below). The concept is the target
for the ground, and the orbit shot for neutrality:

| image / region | mean sRGB | L* p10/50/90 | a* | b* | chroma |
|---|---|---|---|---|---|
| concept 10 km, land | #414935 | 19/28/42 | −7.0 | +11.2 | 15.8 |
| concept 2 km, land | #414631 | 18/26/42 | −5.9 | +11.8 | 15.8 |
| lab, apscale 0.5 (surface.md default), whole frame | #4f5849 | 22/38/46 | −6.6 | +7.3 | 11.8 |
| lab, apscale 0.5, forest | #2c3f3e | 21/22/34 | −7.3 | **−1.8 (teal)** | 9.1 |
| lab, apscale 0.25, whole frame | #4c5238 | 18/35/44 | −7.3 | +14.1 | 16.8 |
| lab, apscale 0.25, forest | #28362b | 16/18/32 | −8.6 | +5.1 | 10.8 |
| lab, apscale 0.15, whole frame | #494d2e | 16/33/42 | −6.9 | **+17.8 (tan)** | 19.7 |
| final lab, farm 10 km (lower 70 %) | #545b3e | 29/37/46 | −8.1 | +15.4 | 17.9 |
| final lab, farm 2 km | #50563a | 23/33/49 | −7.8 | +15.4 | 18.3 |
| final lab, forest edge 2 km | #434a34 | 15/29/47 | −7.1 | +12.3 | 15.4 |
| final lab, coast 10 km, land | #354231 | 17/23/42 | −9.0 | +8.8 | 13.8 |
| final lab, pure forest 10 km | #1f302c | 16/18/21 | −8.3 | +0.5 | 8.6 |
| orbit shot (approved), land and cloud patch | #878f91 | 41/60/74 | −1.2 | −2.3 | 8.8 |

Farmland and forest-edge views now sit at the concept's hue (a* −7…−8, b* +12…+15, chroma 15–18). Neutral
elements stay neutral (roads, grey roofs, clouds), and there is no warm white-balance shift (no pink). **Still
off:** dense forest from 10 km is too dark and neutral (L* 18, b* +0.5 against the concept canopy median
#3b452f, about L* 28, b* +10). Warming the forest macro albedo (0.13, 0.21, 0.10) → (0.155, 0.225, 0.085) moved it
only from b* −0.2 to +0.5, because closed canopy is dominated by the gap term (`macro × 0.22`) and the dark side of
the cluster domes. The fix belongs in the canopy: lighter gaps (~0.35), a cluster lit side ~0.95 × macro, and a
warm lit-top tint. That is for the nature module's canopy LOD.

Rules that came out of it:
1. **Aerial perspective is the cast.** At 2–20 km the camera is above most of the scaled atmosphere
   (Rayleigh scale height 1.3 km), and the haze is additive blue-grey: it lifts dark forest into teal. 0.25 is the
   sweet spot. At 0.15 the land's own yellow shows through as a tan cast. **Production (sky module, C2):** scale
   `AerialPespectiveViewDistanceScale` with camera altitude, 0.5 above ~60 km → 0.25 below ~25 km.
2. Keep the tone mapper neutral (`WhiteTemp` 6500, saturation and contrast 1), as the orbit shot does. Warmth
   comes from the sun angle, not from white balance. Sky light intensity 0.6 vs 1.0 changed b* by only 0.1.
3. Albedos matter more than the grade: the lab's grassland was yellow (b* +18 where wild), so `Grass` became
   (0.33, 0.42, 0.21) sRGB and the dry-patch tint was halved (`×(1.16, 1.08, 0.86)`, weight 0.25–0.5 by aridity).
4. Crops were muted until fields stopped reading as a quilt: gold 0.235–0.27 (not 0.30–0.34) and stubble 0.235.
5. Lab albedo changes made outside the ground block (`SurfaceTerrain.cpp`): `Grass` (0.36, 0.42, 0.22) → (0.33, 0.42, 0.21),
   `Steppe` (0.50, 0.45, 0.32) → (0.48, 0.45, 0.33), `Forest` (0.13, 0.21, 0.10) → (0.155, 0.225, 0.085).
   AP sweep shots: `ue-groundlab-sw-ap05s10/ap025s10/ap025s06/ap015s06.png`.

## 7. Roads that follow contours (`GroundLand.cpp`, `RouteRoads`)

- **Links:** each settlement in the tile to its 3 nearest within 15 km, de-duplicated and routed shortest first.
- **A\*** on the 1024² smooth-height grid (62.5 m), 16-neighbour stencil (knight moves give 26.6° headings; the
  cell a knight move crosses is checked too). Move cost
  `L · (rough + (grade/0.05)² + (grade > 0.16 ? 25·(grade − 0.16)/0.04 : 0))`, with:
  - `rough = 1 + 1.6 · sat(0.6 n(900 m) + 0.4 n(2.6 km) + 0.35)`, a slowly varying "ground difficulty". Without it,
    roads on flat land ran dead straight along stencil rows (`ue-groundlab-map-*` before / after);
  - a bridge (+900) when the move enters a river (within its half-width + 14 m), ×3 per metre to stop roads
    running along the bed, and sea forbidden;
  - ×0.42 on cells an earlier road uses, so links merge into a network and junctions appear;
  - heuristic 0.42 × the Euclidean distance (admissible with the discount), search box = the link's bounding
    box + 4 km.
- **Network:** all paths are merged into one graph of grid nodes. Chains between junctions (degree ≠ 2 or a
  settlement) are each smoothed once (4 Laplacian passes, ends fixed), so shared stretches are not drawn twice
  and roads meet exactly. Then Catmull-Rom at 8 m. Half-width 2.3 m (one link), 2.9 m (two), 3.6 m (three or more).
- **Raster:** for every 8 m segment, the texels within 90 m get the signed and unsigned distance and the
  half-width (min |d| wins).

Measured on the 62.5 m smooth height, every chain sampled every 8 m, against a straight line between the
same chain ends (log line `Ground: road grade`):

| tile (64 km) | routed km | mean grade | max grade | total climb | straight km | mean | max | climb |
|---|---|---|---|---|---|---|---|---|
| farmland | 344.9 | 1.14 % | 6.3 % | 1914 m | 332.4 | 1.29 % | 10.5 % | 2078 m |
| forest edge | 357.8 | 1.10 % | 9.2 % | 2052 m | 343.8 | 1.24 % | 7.7 % | 2221 m |
| coast | 272.5 | 1.13 % | 8.1 % | 1594 m | 263.5 | 1.24 % | 9.2 % | 1696 m |
| hills (38.05, 149.06) | 377.9 | 1.13 % | 9.4 % | 2504 m | 363.4 | 1.27 % | 8.6 % | 2688 m |
| dry valley | 319.5 | 1.11 % | 9.2 % | 1641 m | 309.7 | 1.24 % | 11.2 % | 1798 m |

Honest reading: this planet's lowland is gentle (a straight line averages 1.25 %), so routing gains about 10 %
in mean grade and 7 % in climb for 4 % more length. The visible effect is the routes themselves: they go
round hills and along valleys and join at shared junctions (`ue-groundlab-map-farm.png`, roads in every series).
The **max** grade is not reliably lower, because the Laplacian smoothing after routing cuts corners onto
slopes and the 62.5 m grid cannot see steep pitches shorter than ~200 m. Production fixes: smooth with a grade
constraint (move a point back toward its routed position when the local grade exceeds the routed one),
route on a 30 m grid near the camera, and compare per link (settlement to settlement) rather than per chain.

## 8. Performance (1080p output, TSR from 1400×788, RTX 3060, editor `-game`)

Other labs were rendering on the same GPU during most runs, so whole-frame times (`STATS`) swing by ±10 ms.
Pass timings from `ProfileGPU` are reliable:

| view | material | BasePass | PrePass | GPU frame |
|---|---|---|---|---|
| farm 10 km | with ground | 0.596 ms | 0.185 ms | 6.42 ms |
| farm 10 km | without (`M_SurfLandNoGround`) | 0.855 ms | 0.188 ms | 7.31 ms |
| farm 2 km | with ground | 0.737 ms | 0.177 ms | 6.31 ms |
| farm 2 km | without | 0.533 ms | 0.169 ms | 6.61 ms |

So the ground block is **within run-to-run noise (≤ ~0.2 ms)** of the base pass at this resolution. Its cost
per pixel is small next to the relief loop it sits beside: about 110 integer hashes, 3–4 `Noised`, 9–13
texture taps and one 7 m Worley near hedges. The interleaved `STATS` A/B in `Saved/ab.txt` agrees: no
systematic difference.

CPU at load (24 threads): settlements 26–31 ms (539 sites from 6099 candidates); tile heights 1024² 102–112 ms;
routing 300–404 ms (61 links, 1.0–1.1 M A* expansions, 345 km of road); fine tile 4096² 460–610 ms (almost all
of it is the per-texel river query); global land map ~0.3 s. Total 1.3–1.6 s. Memory: TileF 128 MB, TileH 4 MB,
LandTex 21 MB with mips.

## 9. Pitfalls (all hit in the lab)

1. **Per-pixel values inside per-region coordinates.** Anything that scales or rotates the field coordinates must
   be constant per holding (taken at the site). Per-pixel field size gave radial streaks round villages.
2. **`Rand01(h >> 21)` with a 16-bit `Rand01` is always < 0.03.** Shifted hashes must be re-mixed. Every village
   plot was a kitchen garden, and no orchard ever appeared, until `Rand01` re-hashed its input.
3. **The snow term made flecks in warm lowland** (inherited from SurfaceLab):
   `sat(snowC·1.4 − 0.2 + 0.35 nB + 0.2 nC)` is positive on noise peaks at zero snow climate. It is now
   × `smooth(snowC/0.15)`. The same bug is likely in production `land.hlsl`: check it.
4. **Dashed white line along small rivers near the coast** (inherited, *not fixed*): the carved channel bed dips
   below +1 m, so the water section (triangles whose lowest vertex is below +1 m) puts tiny patches of sea water
   in it, and they render as swash foam (`coast-2`). Excluding beach sand from river channels (done, harmless) did
   not remove it. Same cause as item 10.
5. **Half-float heights** are too coarse for the contour coordinate (§4). Use R32F.
6. **Unsigned road distance alone** cannot draw a 5 m road on 16 m texels: the interpolated minimum between two
   texels is ~8 m. Signed distance interpolates exactly across a straight reach. Keep the unsigned distance only
   to veto sign flips.
7. **Hard per-row field ends** make comb and staircase edges. Share the ends across a block (furlongs).
8. **Uniform shrub density reads as pepper,** and shrubs inside crops look like a rendering bug. Clump them by
   low-frequency noise and gate them by the field.
9. **Aerial perspective below 0.2** exposes the albedos' yellow (tan cast). Fix the albedos rather than going lower.
10. Not fixed (SurfaceLab, water): small depressions behind beaches fill with sea water and render as white foam
    ovals (`coast-2`, `coast-5`, `edge-10`). The water section needs a "connected to the sea" test.
11. Shots must wait for shader compilation (the lab does). A material change takes 20–60 s to compile in `-game`.

## 10. What production should take, and what is still missing

Take, in this order:
1. The **holding → field** structure (§5.1–5.2), with per-holding constants, the per-field cultivation decision,
   and furlongs / polygons. It replaces the rectangular estate grid in `land.hlsl` (lines ~165–227) one for one:
   same inputs (MapC.r farm, MapC.g urban), plus a smooth height and a river distance.
2. **Contour orientation**, which needs the smooth height (a vertex channel or the tile, §4). Without it, use the
   regional orientation alone: it is already better than per-estate random angles.
3. The **forest multiplier** handshake (cleared / wild / planted > 1), and individual trees at margins.
4. **Hedges, hedge shadows, balks, roads** (signed distance), and the village ground (as the towns module's far LOD).
5. The **AP scale by altitude** (sky module) and the albedo changes in §6.

Still missing:
- Close-up crop texture (below ~1 km fields are clean paint with rows). It needs small tiling detail textures
  per crop class, or instanced crop and hedge geometry from the nature module.
- Hedges have no height. Their shadows are faked by the edge normal.
- Wood-edge shadows: the forest margin casts none. The canopy's dome normals give shading only.
- Field shapes ignore roads (real fields line up along roads). Pass the road direction into the holding
  orientation when a road crosses the holding.
- Terraces on steep cultivated slopes, paddy fields in wet tropics, irrigation circles in the modern age, and
  vineyards are not done. Hooks: the class table in `CropColour` and `age`.
- The lab land model (settlements, farm discs) is a stand-in: the sim's `farm` field drives all of it in production.
