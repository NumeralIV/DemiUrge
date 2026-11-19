# Vegetation and towns — LifeLab reference (generated trees, forests, settlements)

Lab: `UnrealLab/LifeLab` (a copy of `UnrealLab/PlanetLab`, module still called `PlanetLab`).
Screenshots: `shots/ue-lifelab-*.png` (final set: `ue-lifelab-f-*.png`, see §12).
Everything below was measured on the RTX 3060 12 GB at 1920x1080, UE 5.7, Lumen + VSM + TSR, the
orchestrator's `DefaultEngine.ini` unchanged except `bSmoothFrameRate=False`.

**Short version for the production engineer (C4 nature / C5 towns):**

1. Generate tree variants in C++ as `FMeshDescription` → `UStaticMesh::BuildFromMeshDescriptions(bFastBuild=true)`
   with **4 hand-made LODs**, two material sections (bark opaque, leaves masked TwoSidedFoliage), leaf cards
   **duplicated back-to-back with one shared volumetric normal** and AO in vertex alpha. 20 meshes (5 kinds x 4
   variants) build in 0.7 s.
2. Instance them with one `UHierarchicalInstancedStaticMeshComponent` per (kind, variant), density from the
   planet's `veg/temp/moist` fields plus local noise. 850k–1.3M instances over an 8 x 8 km patch is fine.
3. **Set `ShadowCacheInvalidationBehavior = Rigid` on every foliage HISM.** With wind WPO the virtual shadow map
   pages are otherwise re-rendered every frame: the same city view went from 33.3 ms to 9.1 ms frame time.
   Nanite foliage (PreserveArea or the new Voxelize) was 3–4x slower on the GPU here (35 ms) — do not use it on a 3060.
4. Forests must also exist in the **ground material** (dark canopy colour + crown-sized mottling, vertex alpha =
   canopy fraction): from 3 km up the instanced trees and the painted canopy merge, and beyond the tree radius the
   ground alone carries the forest (see `f-high`, `f-forest-mid`).
5. Settlements: plan on a 2 m land-use raster (roads, paved, fields, yards, built, no-tree), sample it in the
   terrain material (crisp roads and fields with crop rows, no decals), and build buildings as **merged per-300 m
   chunk static meshes** (5 sections: wall, roof, glass, lamp, additive glow). City + walled town + village =
   1,927 buildings, 2.4 M triangles, 6 s to plan and build.
6. Night = one scalar (`Night`) on the window / lamp / glow materials; lit windows carry their colour in the vertex
   colour (black = unlit), cheap additive light-pool discs under lamps and lanterns, a dim blue moon as
   atmosphere light 1, +3.5 EV.

---

## 1. Running the lab

```
powershell -NoProfile -ExecutionPolicy Bypass -File UnrealLab/LifeLab/build.ps1        # C++ (editor target)
powershell -NoProfile -ExecutionPolicy Bypass -File UnrealLab/LifeLab/mats.ps1         # material graphs (Scripts/make_life_materials.py)
powershell -NoProfile -ExecutionPolicy Bypass -File UnrealLab/LifeLab/shot.ps1 <name> [args]   # -> shots/ue-lifelab-<name>.png
bash UnrealLab/LifeLab/batch.sh "<name> <args>" "<name> <args>" ...                    # several shots, prints timing lines
```

Important arguments (all in `LabGameMode.cpp` / `LifeWorld.cpp` / `LifeTown.cpp`):

| arg | meaning |
|---|---|
| `-site=<cell>` | planet cell at the centre of the patch. 38500 = temperate coast (default), 21445 = savanna coast, 5299 = rainforest coast |
| `-focus=village\|town\|city\|x,y` `-view=dist,yaw,pitch` `-aim=dz` `-fov=` | orbit-style framing around a point (metres) |
| `-cam=x,y,alt,yaw,pitch` | direct camera (alt above ground) |
| `-sun=yaw,pitch` `-night` `-ev=` `-glow=` `-moonlux=` `-sky=` `-haze=` | lighting (default sun 135/21, sky light 0.85, aerial perspective scale 0.4) |
| `-notrees` `-notowns` `-lineup` `-treehalf=` `-treedensity=` `-treecull=` | content switches; `-lineup` puts every kind and variant on a cleared lawn at 0,0 |
| `-nanitetrees=0/1/2` | experiment: Nanite tree meshes (None / PreserveArea / Voxelize); editor-only build |
| `-swayshadows` | turn the rigid shadow-cache fix off (to reproduce the slow case) |
| `-dumptex=<dir>` | writes the leaf atlas and a downscaled land-use map as BMP |
| env `LIFEWARM=<frames>` | warm-up frames before the shot (default 300) |

**Pitfall:** `FParse::Value(Cmd, Key, FString&)` stops at commas by default. For list arguments call it with
`bShouldStopOnSeparator=false` (the orchestrator's lab `ParseList` has this bug: only the first number of
`-cam=` is read).

Frame timing: `LabGameMode::Tick` averages the 60 frames before the shot and logs
`LIFESTATS timing ... gpu X ms (median Y)` using `RHIGetGPUFrameCycles` and `GGameThreadTime/GRenderThreadTime/GRHIThreadTime`.
**Trust the GPU number.** Render/RHI thread times in `-RenderOffscreen` runs swing by 2–4x between identical runs
(PSO creation and offscreen presentation); I saw 8.8 ms and 36 ms frames for the same street view.

## 2. The test terrain: a real-scale patch of the planet

`ALifeWorld` (`LifeWorld.h/.cpp`) builds a curved patch around one planet cell, at the binding scale
(R = 100 km, 0.2 km per sim elevation unit), so the sky atmosphere and clouds of the orchestrator's lab work unchanged.

- **Frame.** The site is rotated to the planet's +Z (`SiteQ = FindBetweenNormals(site, +Z)`). Patch coordinates
  (X east, Y north, metres) are **gnomonic**: a world point is `normalize(X, Y, R) * (R + H)`, so straight lines
  stay great circles and planet triangles can be hit-tested in 2D. The actor sits at world (0, 0, R); every
  instance and vertex is stored relative to it (float precision is fine within ±70 km).
- **Planet fields.** `SampleFields` buckets the planet triangles that face the site, samples elev/temp/moist/veg/hab
  with barycentrics onto a 561² grid (250 m) over ±70 km, then Gaussian-blurs (radius 3–4 cells) to remove
  triangle facets. Elevation is read with Catmull-Rom (`FLifeGrid::Cubic`). One planet cell is ~1.75 km, so the
  patch inherits real biome boundaries: the temperate site has grassland, forest, taiga and coast within 6 km.
- **Height** (`BaseHeightAt`/`HeightAt`): planet relief + fbm hills (2.6 km, ±30 m) + ridged noise (1.1 km,
  ±34 m on high ground) + fine undulation (170 m ±2.4 m, 37 m ±0.35 m), a compression of the band around sea
  level for gentle beaches, and flattening pads for towns that **stop at the sea** (the pad edge becomes a quay).
- **Meshes.** Three rings of chunked static meshes with skirts: inner ±4.1 km at 5.12 m (64 chunks of 200²
  quads), middle ±16 km at 32 m, outer ±65 km at 256 m: 7.8 M terrain triangles, built in ~9 s
  (ParallelFor for vertices, mesh builds on the game thread). Water is a separate ring mesh at sea level with
  depth-tinted vertex colour and foam in vertex alpha, skipped where all four corners are dry (0.3–0.6 M tris).
  This is a lab stand-in; the descent LOD belongs to the surface engineer (`docs/lab/surface.md`).
- **Land use.** 4096² raster at 2 m over the inner patch (`LuRoad, LuPaved, LuField, LuYard, LuNoTree, LuBuilt` +
  `LuFieldCol` crop colour with row angle in alpha). Planning stamps it (`StampRoad`, `StampField`, `StampPoly`,
  `StampDisc`); trees and buildings read it; two 4096² textures (`T_LandUse` RGBA = road, paved, field, yard and
  `T_FieldCol`) feed the terrain material through UV1. Clamp addressing with a zeroed border makes the outer rings
  read "nothing". This gives crisp 2 m roads and field edges with no decals and no z-fighting.

## 3. Trees

Code: `LifeTrees.h/.cpp` (shapes), `LifeMesh.h/.cpp` (builder → `UStaticMesh`), `LifeTex.cpp` (leaf/bark textures).

### 3.1 Building blocks

- `FLifeMesh`: vertices (pos, normal, UV0, UV1, sRGB colour), one index list per material section, helpers
  (`Tri/Quad` with **automatic winding from a wanted normal** — Unreal's front face is `cross(C-A, B-A)`; getting
  this wrong was the most common bug, so every helper takes the outward direction), boxes, beams, cylinders,
  cones, pyramids, `Append(mesh, transform)`.
- `Tube()`: tapered tube along a polyline with parallel-transported frames, smooth normals, UV u = metres around,
  v = metres along (bark texture stays the same size on every branch).
- `Card()`: a leaf card (1 or 2 segments, tip droops by `Bend`). **Emitted twice, back to back, with a single
  shared normal** = `normalize(radial * 0.75 + faceNormal * 0.25)` where *radial* points out of the crown volume
  (ellipsoid gradient, cone for conifers, up-biased disc for acacias). The leaf material is therefore **not**
  two-sided (no flipped back-face normals); the crown shades like a volume and the TwoSidedFoliage transmission
  lights the far side of the crown when the sun is behind it.
- Vertex alpha = **ambient occlusion**: 0.25 deep inside the crown → 1 at the surface, darker underneath. The
  material multiplies base colour by `lerp(0.28, 1, AO)` and also feeds AO. This is what makes crowns read as
  volumes from altitude (VSM self-shadowing of cards alone is not enough).
- UV1 = wind weights: `x` = sway (metres per unit wind; `0.018*H*(z/H)^2 + 0.025*r` for broadleaf), `y` =
  flutter amplitude growing towards the card tip.

### 3.2 The five kinds (numbers as generated)

| kind | shape recipe | LOD0 tris |
|---|---|---|
| Conifer | H 14–24 m, straight trunk (ends at 0.9 H), whorls every 0.36–0.44·H/18 m with 5–7 drooping branches, crown radius `Rb·((H−z)/(H−z0))^0.95`, 2–4 needle-spray cards per branch, two leader cards; LOD3 = 3 crossed vertical cards (the spray tile is feather-shaped, so it reads as a spruce silhouette) | 6,756 |
| Broadleaf | H 11–17 m, trunk to 0.36–0.48 H with root flare, 3–5 limbs at 25–50° that curl upward, 3–4 sub-branches each, a leader; crown ellipsoid (0.32–0.4 H radius); 230 cards of ~2.3·H/14 m in the shell 0.25–1.0 of the radius | 2,704 |
| Palm | H 8–13 m, trunk bends by `lean·f^1.8` (0.6–2.6 m), leaf-base bulb, 3–7 coconuts, 13–17 fronds as 7-segment **V-folded strips** (28° fold) arching down, 2–4 dead brown fronds hanging | 1,380 |
| Acacia | H 6–9.5 m, short trunk (0.22–0.38 H), 2–4 limbs at 48–66°, 3–4 sub-branches each ending in a flat clump (r 2.3–3.4 m, 0.9–1.4 m thick), 42 near-horizontal leaflet cards per clump: a flat-topped umbrella | 1,472 |
| Shrub | H 1.2–2.4 m, 4–7 stems, 85 cards in a squat ellipsoid, broadleaf or leaflet tile | 824 |

Four variants per kind (different seeds). Per-instance: yaw, ±1.7° tilt, scale 0.72–1.22 (x0.9–1.15 for lone
trees, rainforest broadleaf x1.35 with 4% emergents at x1.6), height stretch 0.9–1.1, and hue from
`PerInstanceRandom` in the material.

### 3.3 LODs (hand-made, not simplified)

| LOD | screen size | trunk/branch sides | cards |
|---|---|---|---|
| 0 | 1.0 | 9 / 6 / 4 | all (broadleaf 230, 2 segments) |
| 1 | 0.30 | 7 / 4 / 3 | ~half, x1.4 size |
| 2 | 0.10 | 5 / 3 / – | ~15%, x2.0 size |
| 3 | 0.03 | 3 / – / – | 5–12 cards x2.6–2.9 (conifer: 3 crossed silhouette cards) |

Set `RenderData->ScreenSize[i].Default` after the fast build (the fast path hard-codes 0.75^i). Material has
`dithered_lod_transition`.

**Pitfall (cost me an hour):** the fast build **skips empty sections**, and the material is then looked up by slot
name per section — but a LOD whose bark section is empty (shrub LOD3 has no branches) still ended up drawing its
leaves with the bark material in the editor-run game: opaque white squares all over hedgerows. Fix in
`LifeMakeStaticMesh`: give every empty section one degenerate triangle.

### 3.4 Leaf and bark textures (painted in C++, no assets)

`FLifeTextures::Build()` paints everything in 0.46 s at startup:

- **Leaf atlas** 2048², four 1024 tiles: broadleaf twig (6–8 side twigs, ~40 pointed leaves with midrib, two-tone
  halves and per-leaf colour), fir needle spray (feather-shaped), palm frond (drawn in metres for a 1.8 x 5 m card,
  7% torn leaflets, yellow tips), bipinnate acacia leaflets. Dump with `-dumptex=`.
- **Mips:** (1) *push-pull dilation* fills transparent texels with the surrounding leaf colour (no dark fringes);
  (2) **alpha-coverage preservation**: each mip's alpha is scaled (binary search) so the fraction of texels above
  the 0.4 clip equals mip 0's. Without it canopies thin out and turn see-through at 1–3 km.
- Bark (fissured, 512²) and palm trunk rings with derived normal maps; wall (plaster, coursed stone, running-bond
  brick, wood grain packed in RGBA, 4 m per tile), roof (thatch, barrel clay tiles, slate, standing-seam metal),
  ground masks (fine mottle, **crown clumps** from cellular noise, pebbles, broad variation), water ripples.
- `LifeMakeTexture` creates a transient `UTexture2D` (BGRA8) and fills every mip by hand; sRGB on colour, linear on
  masks/normals. Material samplers for runtime textures: use `SAMPLERTYPE_LINEAR_COLOR` and decode normals by hand
  (`xy*2-1`, `DeriveNormalZ`) — the default-texture/sampler-type check only runs on the asset's default texture.

## 4. Materials (`Scripts/make_life_materials.py`)

All graphs are built with `MaterialEditingLibrary`, `used_with_instanced_static_meshes = True`. Parameters are set
per world through dynamic instances.

- **M_LifeLeaf** — Masked (clip 0.4), shading model TwoSidedFoliage, *not* two-sided, dithered LOD transition.
  `Base = atlas.rgb * VC^2.2 * lerp(TintA, TintB, PerInstanceRandom) * lerp(0.28, 1, VC.a) * Brightness`;
  `Subsurface = Base * Transmission(0.75, 0.9, 0.3)`; `AO = VC.a`; roughness 0.85, **specular 0.04** (set from C++, see `MakeMaterials`).
  Values in use: Brightness 0.85, TintA (0.70, 0.82, 0.80), TintB (1.12, 1.02, 0.70).
- **M_LifeBark** — opaque, bark albedo x vertex colour, normal map, same wind as leaves (no flutter).
- **Wind (WPO, both):** `t = Time*WindSpeed; ph = PerInstanceRandom*2π;`
  `s = (0.6 sin(1.1t+ph) + 0.25 sin(2.7t+1.7ph) + 0.35) * (1 + 0.4 sin(0.31t+0.5ph))`;
  `WPO = WindDir(1,0.4,0) * s * UV1.x * WindStrength(100 cm)`; leaves add
  `VertexNormalWS * sin(8t + 40(u+v) + 3ph) * UV1.y * FlutterStrength(60)`. WPO is disabled beyond 1.5 km
  (`SetWorldPositionOffsetDisableDistance`).
- **M_LifeTerrain** — vertex colour (climate/biome albedo) x three detail octaves of the ground mask texture at
  9, 61 and 433 m; **canopy**: where vertex alpha (canopy fraction) is high, multiply by crown clumps sampled at
  130 m and 47 m (`lerp(0.62, 1.18, crownA*crownB)`); then land use: fields (crop colour from `T_FieldCol`,
  crop rows `sin(2π·dot(uv, angle)/1.6 m)`), yards/packed earth, dirt roads, paved (cobble pebbles).
- **M_LifeWall / M_LifeRoof** — style index in UV1.x selects a channel of the packed mask textures and the matching
  normal; colour comes from vertex colour (so one material covers every building).
- **M_LifeWindow** — dark glass (roughness 0.12), `Emissive = VC^2.2 * Night * Glow` (VC black = unlit window).
- **M_LifeLamp** — emissive lamp heads; **M_LifeGlow** — additive, unlit light pools (`VC^2.2 * Night * Glow`):
  invisible by day because additive zero is nothing.

**Pitfalls:**
- `MaterialExpressionVertexColor` outputs are all **unnamed** in 5.7, so Python cannot connect its alpha pin by name.
  Use a `Custom` node returning `Parameters.VertexColor` (float4) for alpha, keeping a real VertexColor node in the
  graph so the interpolator exists.
- Foliage specular: with the default 0.5 (or even 0.35) the sunlit cards of the generated crowns flash **white**
  (screenshots `w1` vs `w2`); keep leaf specular ≤ 0.1.
- `LifeFbm2(x, y, octaves, seed)`: passing a seed in the octave slot gave 55–500 octaves, overflowed to NaN and
  silently deleted every street and the town wall. Range-check octave counts in production.

## 5. Scattering and density from the planet

`ALifeWorld::BuildTrees()`. One candidate per 4.5 m jittered cell over ±4 km (3.2 M candidates, ParallelFor by
rows, 0.7 s including mesh builds):

```
Canopy = smooth(0.42, 0.62, veg + 0.6*(0.5*fbm(x/520 m, 5 oct) + 0.12*fbm(x/90 m)))      // fractal forest edges
       * smooth(1.5, 4, H) * smooth(0.7, 0.85, slopeNormal.z) * smooth(-7, -3, T) * ClearingAt(x, y)
Open   = (0.012 + 0.03*veg + 0.1*Hot*Dry*veg) * slopeTerm * (0.4 + 0.6*Clearing)          // lone trees, savanna
P      = Canopy*0.62*(1 - 0.45*RainforestWet) + (1-Canopy)*Open + Coast*0.06               // per 4.5 m cell
```

`T = planet temp − 6.5 °C/km x 5 x H` (the relief is exaggerated, so the lapse rate is too), `Hot = smooth(17, 22, T)`,
`Dry = smooth(0.5, 0.3, moist)`, `Coast = smooth(14, 2, H)*Hot`. Kind weights:

```
Conifer   = smooth(13, 5, T) + 0.6*smooth(0.05, 0.35, fbm(x/700))*smooth(19, 11, T)   // stands of spruce in mixed forest
Broadleaf = smooth(3, 10, T) * smooth(0.2, 0.4, moist) * (1 - 0.85*Coast) * lerp(1, smooth(0.3, 0.55, moist), Hot)
Palm      = Hot * (4*smooth(25, 3, H) + 0.35*smooth(0.6, 0.8, moist))
Acacia    = smooth(16, 21, T) * Dry * (Canopy < 0.5 ? 3 : 0.6)
```

Shrubs are a second layer (6 m cells, ±3 km): `0.05 + 0.12 veg + 0.35 forestEdge`, plus **hedgerows** along 35–60%
of field edges (shrub every 3.2 m, a broadleaf tree 10% of the time). `LuNoTree` removes trees from roads,
yards, fields, streets, walled towns and city blocks; `Clearings` (900 m around villages and towns, 1150 m around
the city, edge wobbled ±30% by noise) fade the forest out so settlements sit in farmland.

The ground uses the **same canopy function** for its vertex alpha and dark canopy colour, so beyond the 4 km tree
radius (and between crowns) the forest continues as painted canopy with crown mottling.

Instance counts (whole inner patch):

| site | conifer | broadleaf | palm | acacia | shrub | total |
|---|---|---|---|---|---|---|
| 38500 temperate, with towns | 395,825 | 306,990 | 0 | 0 | 143,254 | **846,069** |
| 21445 savanna coast | 0 | 122,651 | 4,290 | 109,410 | 87,804 | **324,155** |
| 5299 rainforest coast | 2,278 | 1,014,358 | 62,943 | 29,657 | 163,796 | **1,273,032** |

HISM settings: one component per kind x variant (20), `Mobility = Static`, no collision,
`bAffectDistanceFieldLighting = false`, cull end 9 km for trees, 2.5 km for shrubs, WPO off beyond 1.5 km,
**`ShadowCacheInvalidationBehavior = Rigid`** (shadows do not sway; nobody notices).

## 6. Settlements (`LifeTown.cpp`)

### 6.1 Site choice

`FindSite` scans the patch on an 80 m grid with `BaseHeightAt` (no pads yet):
- **city**: 2–30 m above sea, 12–50% sea on a 480 m ring, dry land on a 220 m ring, low roughness at 350 m;
- **walled town**: prominence over the 480 m ring minus 2.5 x roughness at 110 m (a hilltop), land all round;
- **village**: flat (roughness at 260 m), high `hab`, ≥1.35 km from the others.
Overrides: `-city=x,y -town=x,y -village=x,y`. The city then gets a flattening pad (650/1000 m) that keeps the sea.

### 6.2 Layouts

- **Village** (99 buildings): a 1.4 km main road along the contour (sinusoid 28 m + noise), two lanes at 65–110°;
  houses every 12–20 m on both sides, set back 3.5 m + half depth + 1–4 m, door facing the road, 22% with an
  L-wing, 35% with a barn behind; church on the green; 10 outlying farmsteads; packed-earth yards. Fields: three
  sectors, each a rotated, brick-bonded grid of long strips (120–190 x 45–85 m, 22% doubled), jittered corners,
  ragged outer edge, 9 crop colours (wheat, barley, green crops, pasture, ploughed, fallow, stubble, rare rapeseed),
  hedgerows. Earlier attempt with polar rings looked like a target from altitude — avoid radial layouts.
- **Walled town** (663 buildings incl. wall pieces): noisy 44-point ring (R 215 m ±13%), gates towards the other
  settlements plus one more, paved main streets square→gates, a ring street at 0.83 R, 7 lanes, a cathedral on the
  30 m square, a keep at the highest point, **houses with gable ends to the street** (2–4 storeys, 48–56° roofs,
  65% plaster in six pastels, 45% of those half-timbered, stone, brick), 500 back-lot fill attempts, wall pieces
  ≤9 m following the ground with merlons and an inner parapet, round towers every 4th point (cone roofs),
  gatehouses with two towers, suburbs along the roads out of the gates, fields beyond.
- **Industrial port city** (1,165 buildings, 161 blocks): grid (92 x 72 m blocks, 14 m streets, 22 m avenues)
  aligned with the coast (seaward direction = lowest mean height over a 250–650 m fan), clipped to an ellipse and to
  land; zones by distance from the sea: **industrial** (sheds 1 storey x 8–11 m, sawtooth or low gable metal
  roofs, brick, 42–68 m smokestacks), **downtown** (5–11 storeys, flat roofs with parapets, cornices and rooftop
  clutter, a civic hall with a 48 m clock tower), **residential perimeter blocks** (3–6 storeys, brick/stone/plaster,
  flat/hip/gable). Docks: a pier every 110–150 m along the shore (110–180 m long, 15 m wide, on pillars), quay
  cranes, warehouses, cargo ships with containers. Street lamps every 32 m on both sides.

### 6.3 Building generator

One spec struct (`FBuildingSpec`) → `GenHouse / GenChurch / GenKeep / GenRoundTower / GenWallSeg / GenPier /
GenCrane / GenShip / GenStack / GenLamp`, in a local frame with z = 0 at the floor. Walls run down to 0.6 m below the
lowest ground corner (visible stepped plinths on slopes). Details that made the difference against "boxes":
plinth band, window sills and lintels, doors, half-timbering (storey beams, posts between windows, corner braces),
roof overhangs with real thickness (eave fascia, verge strips, darker soffit), ridge caps, thatch 0.35 m thick with
0.6 m overhang, chimneys with caps, parapets and cornices, gable triangles in wall material. Footprint variety:
size ranges per kind plus L-wings; hip roofs swap axes when the house is deeper than wide.

`TryPlace()` tests the footprint (+margin) against water, slope and the `LuBuilt/LuPaved/LuRoad` raster, then marks
built + yard + no-tree. Each building is appended into the mesh of its 300 m chunk with
`UpQuat(x, y) * yaw` and `SurfaceM(x, y, base)` (buildings stand on the curved planet), and each chunk becomes one
`UStaticMeshComponent` with 5 sections: 31 chunks / 1.85 M tris for the city, 7 / 0.55 M for the town, 8 / 31 k
for the village. Planning + building all three: ~6 s.

## 7. Night

- Sun at −25°, moon = second directional light, `AtmosphereSunLightIndex = 1`, 0.06 lux (sun 10), colour
  (0.62, 0.72, 1.0), shadows on; exposure +3.5 EV; `World->SetNight(1, glow 0.45)`.
- Windows: each window is a glass quad 3.5 cm proud of the wall; lit ones get a random warm colour
  (`LitCol*0.55–1`, 25% of city windows a cool white), unlit ones black. Lit fractions: village 0.45, town 0.33,
  city residential 0.28, downtown 0.38, factories 0.22 (dim blue-white skylight glazing).
- Additive light pools (`GlowDisc`, 3-ring fan: centre, 40% at 0.4 R, black rim): street lamps 11 m radius,
  house lanterns 3.6 m beside 30% of lit doors. They give the warm puddles of light that make the night shots
  read, at zero lighting cost. Make them soft — the first version (hard 7.5 m discs) looked like polka dots.

## 8. Performance (1080p, RTX 3060, GPU time from `RHIGetGPUFrameCycles`)

| view | GPU ms | notes |
|---|---|---|
| village mid-altitude (700 m) | ~8–9 | 846k trees in the patch |
| village street level | 8.4 | |
| walled town mid / street | 6.2 / 8.3 | |
| city 1.6 km oblique (whole city + forest) | 9.2 | 1.85 M city tris |
| city night | 7.9–8.1 | additive pools included |
| 9–12 km altitude over everything | 10.6 | |
| savanna (324k trees) | 9.0 | |
| rainforest canopy (1.27 M trees) | 29–30 | **over budget** (see §10) |

The two decisive experiments (city view, same build):

| setting | frame ms | GPU ms |
|---|---|---|
| WPO wind, default shadow invalidation | 33.3 | 10.2 (CPU/RHI bound: VSM re-renders foliage pages) |
| same, VSM disabled | 10.8 | 10.2 |
| **`ShadowCacheInvalidationBehavior = Rigid`** | **9.1** | **8.5** |
| Nanite trees, PreserveArea | 51.1 | 35.1 |
| Nanite trees, Voxelize (5.7) | 63.4 | 34.3 |

Startup (lab): fields 0.01 s, textures 0.46 s, terrain + water 9.6 s, 20 tree meshes + 850k instances 0.7 s,
three settlements + land-use textures 6.3 s.

## 9. Pitfalls (collected)

1. WPO foliage + VSM: set `ShadowCacheInvalidationBehavior::Rigid` (or VSM cost explodes).
2. Empty mesh sections in a LOD shift materials → keep a degenerate triangle.
3. Leaf specular must be ~0.06 or sunlit cards go white.
4. Leaf cards as two back-to-back copies with one volumetric normal, not a two-sided material.
5. Alpha-coverage-preserving mips + colour dilation for any alpha-tested atlas.
6. Python cannot address VertexColor pins by name — use a Custom node for alpha.
7. `FParse::Value` stops at commas.
8. Unreal triangle winding: front = `cross(C-A, B-A)`; build helpers that take the outward normal.
9. Runtime static meshes have no distance fields: Lumen's global SDF does not see trees or buildings (the lab runs
   `r.Lumen.TraceMeshSDFs=0` anyway). Interiors of streets get sky light as if open; screen traces cover most of it.
   Production can bake the 20 tree variants and a building-part kit as assets offline if SDFs are wanted.
10. A pad that flattens a city must leave the sea alone (`lerp(H, target, w*smooth(-1.5, 0.8, H))`), otherwise the
    coastline moves.
11. The scaled atmosphere (Rayleigh x6.15) hazes ground views heavily; the lab uses
    `SetAerialPespectiveViewDistanceScale(0.4)` for 1–3 km views. Request this from C2/sky as a tunable (camera height
    dependent), not a constant.
12. Timing in offscreen runs: use GPU time; render/RHI thread times are noisy � and **never run two lab processes
    at once** (a background batch I had forgotten doubled frame times and locked the material `.uasset`s, so a
    material rebuild silently failed to save).
13. Occupancy on a 2 m raster: terraced neighbours 0.3 m apart collide through rasterisation. Test only cells whose
    centres are > 1.05 m inside the new footprint (`TryPlace`); this alone took the walled town from 663 to 830
    buildings and filled the city's street fronts.
14. Street widths must be known per segment (the first version guessed avenues from segment endpoints and widened
    random streets to 22 m, which rejected whole city frontages); inset each block by the half-width of *its* streets.
15. The material script reuses existing assets (`delete_all_material_expressions`) instead of delete + create:
    `create_asset` fails unattended when the old package is still referenced.

## 10. What is still missing

- **Rainforest is too heavy** (1.27 M broadleaf, 30 ms GPU): needs a far LOD that is a single crossed impostor or
  an octahedral impostor, fewer/larger crowns, cull distance tied to camera height, and a coarser scatter beyond
  2 km (HISM per 1 km tile with per-tile density thinning).
- No grass blades or ground clutter: close-ups at 1.7 m look bare (`forest1`); a near-camera grass/fern layer
  (geometry blades within ~60 m, HISM per tile) is the next step for street-level shots.
- Leaf cards up close (< 5 m) look painted; fine for a god game camera, not for a first-person view.
- No river/lake interplay (bridges, mills), no roads between non-adjacent settlements, no settlement growth stages
  (camp → metropolis), no construction/destruction states, no smoke from chimneys and stacks (C6 fx).
- City is still a regular grid; districts with rotated grids and a medieval core would read more organic.
- Terraced houses in the walled town do not share walls (each is a separate box with a 0.15 m gap): fuse runs of
  frontage into terraces with shared party walls.
- Building windows are a regular grid; no shopfronts, balconies or dormers.
- Night sky (stars, moon disc) and point-light shadows are not done (sky module).
- Tree LOD screen sizes and cull distances were tuned by eye on a few views only; the canopy far LOD transition at
  the 4 km tree radius needs a smooth density fade instead of a hard edge.

## 11. File pointers

| file | contents |
|---|---|
| `UnrealLab/LifeLab/Source/PlanetLab/LifeMesh.h/.cpp` | mesh builder, winding rule, `LifeMakeStaticMesh` (fast build, LOD screen sizes, empty-section fix, optional Nanite), noise |
| `.../LifeTex.h/.cpp` | leaf atlas painters, coverage-preserving mips, push-pull dilation, bark/wall/roof/ground/water textures |
| `.../LifeTrees.h/.cpp` | `LifeBuildTree(kind, seed)`: conifer, broadleaf, palm, acacia, shrub with 4 LODs |
| `.../LifeWorld.h/.cpp` | patch frame, planet field sampling, terrain/water rings, land-use raster and textures, scattering, HISM setup |
| `.../LifeTown.cpp` | site finding, village / walled town / city planning, fields and hedgerows, building generators, chunked meshes |
| `.../LabGameMode.cpp` | camera framing, sun/moon/night, sky light, atmosphere, timing |
| `UnrealLab/LifeLab/Scripts/make_life_materials.py` | all material graphs |

## 12. Screenshots

Final set (`shots/ue-lifelab-f-*.png`): `trees-lineup`, `forest-mid`, `high`, `village-mid`, `village-close`,
`village-golden`, `village-night`, `town-mid`, `town-close`, `town-night`, `city-mid`, `city-close`, `city-night`.
Earlier diagnostics kept for reference: `first` (first canopy), `lineup1–3`, `w1/w2` (leaf specular on/off), `town0`
(before the NaN fix), `sav2` (acacia savanna), `palm2` (palms at the savanna coast), `trop2` (rainforest canopy),
`forest1` (forest floor at 1.7 m), `n1/n2` (Nanite trees).
