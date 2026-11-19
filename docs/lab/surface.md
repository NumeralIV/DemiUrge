# Surface lab: descent LOD, ground material, ocean, rivers

Lab: `UnrealLab/SurfaceLab` (a copy of `UnrealLab/PlanetLab`; module name kept as `PlanetLab`).
Shots: `shots/ue-surfacelab-*.png`. Written for the C1 `planet` engineer (SPEC-UNREAL §2).

## 1. Result in one paragraph

The planet is a quadtree of triangular patches on the 20 icosahedron faces. The recursive edge-midpoint
subdivision is the same as the sim grid, so level 6 of the tree is exactly the sim's triangles. Each patch
samples one pure height function, `FSurfaceTerrain::Height(dir, spacing)`, and is stitched exactly to any
coarser neighbour, so there are no cracks. The camera can go from 400 km to 300 m above a valley.
The mesh carries only the noise octaves it can resolve (wavelength ≥ 8 vertex spacings). The land material
evaluates the same noise, bit for bit, and adds the missing octaves as normals. So the shading and the
relief look the same at every LOD level, and from orbit you still see the ridges and valleys.
Rivers are built from the sim's flow field as smoothed, meandering polylines. They carve channels into the
height function and show as thin lines from orbit (a baked coverage map), as continuous lines at mid range
(a distance field) and as crisp water channels close up (a signed distance per vertex). The ocean is Single
Layer Water: waves come from swell plus noise normals, roughness grows with the wave slope lost to
distance (so the sun glint widens from orbit), and there are turquoise shallows and surf foam bands.

Contact sheets (orbit 400 km → 79 km → 12 km → 1.3 km → 290 m above the target):

| series | sheet | single shots |
|---|---|---|
| snowy mountain valley (-54.32, -117.87) | `ue-surfacelab-sheet-mtn.png` | `ue-surfacelab-mtn-1..5.png` |
| forested mountain valley (-24.16, -113.67) | `ue-surfacelab-sheet-forest.png` | `ue-surfacelab-forest-1..5.png` |
| rainforest coast + river mouth (12.01, -126.65) | `ue-surfacelab-sheet-coast.png` | `ue-surfacelab-coast-1..5.png` |
| streamed 20 s descent (async LOD, no settle) | `ue-surfacelab-sheet-fly.png` | `ue-surfacelab-fly-0..4.png` |
| LOD debug: colour by level / by patch | | `ue-surfacelab-debug-lod-levels.png`, `-lod-patches.png` |
| aerial perspective scale 1.0 vs 0.35 | | `ue-surfacelab-apscale-1.0.png`, `-0.35.png` |

Best frames: `mtn-2`, `mtn-4`, `forest-4`, `forest-5`, `coast-3`, `coast-4`. Honest verdict: from orbit to
about 1 km it reads as a real planet photographed from the air. Below about 1 km the ground has convincing
shapes, light and water, but the ground cover is still a texture standing in for real vegetation (see §8).

## 2. Running it

```
powershell -NoProfile -ExecutionPolicy Bypass -File UnrealLab/SurfaceLab/build.ps1          # C++ (≈5 s incremental)
powershell -NoProfile -ExecutionPolicy Bypass -File UnrealLab/SurfaceLab/mats.ps1           # materials from Scripts/make_materials.py
powershell -NoProfile -ExecutionPolicy Bypass -File UnrealLab/SurfaceLab/shot.ps1 <name> -look=lat,lon,distKm,heading,pitch,fov -sunl=elev,az -ev=-1 -apscale=0.5
bash UnrealLab/SurfaceLab/series.sh <prefix> <lat> <lon> <heading> <sunElev,sunAz> -ev=-1 -apscale=0.5   # the 5-altitude series
```
- `-look`: the camera looks at the surface point (lat, lon) from `distKm` away, with heading measured from
  north and pitch measured down from the horizon. `-sunl`: sun elevation and azimuth in that point's local frame.
- `-fly=20 -flyshots=5 -flyrange=400,0.85,90`: an exponential descent with async LOD streaming. It logs
  `FLY hitch` and `FLY DONE` (average and worst frame time).
- Debugging: `-surfdebug=1` paints skirts magenta, `=2` turns skirts off, `=3` colours by level, `=4` colours
  by patch. `-profile` logs a height profile. `-lodpx=`, `-k=` and `-maxlevel=` set the LOD parameters.
  `-landmat=` and `-watermat=` swap materials (used for the A/B cost test).
- The log has `LOD settle <ms>` and a `STATS frame/game/render/gpu` line averaged over the last 100 warm frames.

## 3. The height function (one source of truth)

`Source/PlanetLab/SurfaceTerrain.cpp` `Height()`. It is a pure function of (unit direction, vertex
spacing S). Units are metres above the datum, and sea level is 0.

1. **Base: a Loop-subdivided sim grid.** The sim elevation gets one neighbour-smoothing pass and then two
   rounds of Loop subdivision, level 6 → level 8 (655 362 vertices, 400 m spacing, 56 ms at load). The
   point is located with the same recursive descent as `js/core/geo.js` (child order (a,ab,ca)(b,bc,ab)
   (c,ca,bc)(ab,bc,ca), checked against the sim's `tris` at startup) and the base is linearly interpolated.
   Elevation maps to metres at 200 m per unit. Below sea level it maps through a shelf,
   `-200·|E|·(0.5 + 0.5·smoothstep(0.1, 0.8, |E|))`, so shallows exist near the coast.
   - *Rejected:* blending the corners' tangent planes with squared barycentric weights. It is smooth at
     cells but terraces (plateaus at cells, steps between them). At the coast this made a raised lip and a
     cliff (`coast` shots before the fix). Plain linear interpolation of level 6 shows 1.6 km facets.
2. **Relief: one loop, two fractals** (`SurfNoise::Relief`). λ0 = 6000 m, 10 octaves, gain 0.5, and a fixed
   orthonormal rotation between octaves (iq's m3). Gradients are carried back to the planet frame and
   projected onto the tangent plane.
   - *Eroded:* `a += b·n/(1+|d|²)`, iq's derivative damping, which gives smooth valleys. Amplitude
     `RAe = 70 + 140·M` m.
   - *Ridged multifractal:* only the first 4 octaves (down to 750 m), `(1−1.5·sqrt(n²+0.003))² − 0.35`,
     each weighted by the previous ridge, with no erosion damping. Amplitude `RAr = 650·M²` m. With 6
     octaves the finer ridges read as worms. With damping the crests go soft.
   - Mountainness is `M = sat((E−0.3)/2)·(0.5+0.5·sat(slope/1.4))`. Relief fades in over the first 0.25
     elevation units above sea level (`RAe ×= 0.35…1`), so coastlines keep the sim's outline.
3. **Fine fractal:** plain fbm, λ0 = 16 m, 5 octaves, amplitude `0.3 + 1.5·RAr/(RAe+RAr)` m. It is
   derived from the two relief amplitudes so that the material can reproduce it without another vertex channel.
4. **Rivers:** valley floor and channel (§6). Relief is multiplied by
   `smoothstep(hw+30, 4hw+650, d)`, giving a flat valley floor along every river. The channel carve is
   `(2 + 0.1·hw)·(1 − smoothstep(0.7hw, 1.6hw, d))` m, and is turned off on the sea floor.
5. **Band limit:** an octave of wavelength L has the weight `MeshWeight = smoothstep(4S, 8S, L)` in a mesh
   of spacing S. The material uses `1 − MeshWeight` (identical HLSL in `Scripts/hlsl/common.hlsl`) times an
   anti-alias weight `smoothstep(2fp, 5fp, L)`, where fp is the pixel footprint from `ddx/ddy` of the world
   position. **Pitfall:** with the first thresholds (2.5S…5S) the octaves near the cut-off were sampled at
   about 3 vertices per wavelength. The interpolated vertex normals then gave them less contrast than the
   material did, which showed as rings at every LOD border. Octaves need 8 or more vertices per wavelength.

Vertex normals are forward differences of `Height` at ½S in a fixed tangent frame (`NormalAt`). That is a pure
function, so patches of the same level agree on shared edges. Climate colours are read at a domain-warped
direction (`dir + 0.022·warp`, two fbm scales) after two smoothing passes of Veg, Moist, Temp and the
wetland mask. Without that, biome borders follow the hexagonal sim cells (`hex` artefacts in early shots).

Noise (`Source/PlanetLab/SurfaceNoise.h` ⇄ `Scripts/hlsl/common.hlsl`): an integer hash
`(x·73856093 ^ y·19349663 ^ z·83492791)`, then xorshift-multiply, then 12 cube-edge gradients, and iq's
quintic `noised` with analytic derivatives. The noise input is `dir · 100 000 m / λ`, so it depends on the
direction only. In the shader that input is built from the float world position. That precision (1 cm at
1e7 cm) is enough for octaves down to 0.25 m. Below about 0.1 m (eye height) you would need camera-relative
or double-float positions.

## 4. LOD scheme (`Source/PlanetLab/SurfacePlanet.cpp`)

- **Tree.** The roots are the 20 icosahedron faces. Children come from the normalised edge midpoints, so
  level-6 nodes are exactly sim triangles. Each patch is a K×K triangle grid. **K = 32** (K = 64 gave
  similar quality with 1.5× the triangles, because culling is coarser). The root vertex spacing is
  110.7 km/32 = 3.46 km. Level 12 is 0.84 m, which is MaxLevel for 300 m views; 13 is allowed.
  `GridDir()` uses one formula for every patch. With K a power of two, the weights k/K are exact and a
  zero-weighted corner adds an exact zero, so shared edges give bit-identical directions and heights.
- **Split rule.** A patch splits when `spacing / (distance − bound radius) · pxPerRad > TargetPx`, with
  **TargetPx = 8**. The material carries the finer relief, so 8 px between vertices does not look coarse.
  At 4 px the triangle counts were 3–4× higher with no visible gain. There is **hysteresis**: a split patch
  merges back only below 0.7·TargetPx. Without it, every flicker re-stitches the neighbours.
- **Culling.** Horizon: the occluder is the sea sphere 50 m below sea level, and a patch is drawn when
  `θ − ρ < acos(Ro/Dc) + acos(Ro/(R+maxH))`. A cone test against the frustum stops patches outside the view
  from splitting, but they are still drawn coarse so that shadows still land. A node's height range comes
  from its last build; before that it inherits its parent's.
- **Crack-free: stitching.** After each selection, every drawn patch probes the point 5 % beyond each edge
  midpoint and finds the drawn node there by point location (`DrawnAt`). If that node is coarser, the patch
  is (re)built with that edge stitched: every edge vertex is moved onto the coarse patch's own edge. That
  means a linear interpolation between the two nearest coarse grid vertices, with *their* positions and
  heights evaluated by the same `GridDir`/`Height`/`NormalAt` at the coarse spacing. This is exact to float
  rounding. Safety skirts of `0.2·S + 0.5 m` stay for three cases: float rounding, a corner shared with a
  same-level patch, and a frame where a re-stitch has not landed yet. `-surfdebug=1` shows no visible skirt
  in the final shots.
  - *Rejected:* skirts alone, 1.5 S deep. The geometry closes, but the skirt sits in a slit shadowed on
    both sides and reads as a dark dotted line along LOD borders.
- **Water** is a second mesh section of the same patch: the same grid at sea radius, with only the triangles
  whose lowest land vertex is below +1 m. It is stitched the same way. Without stitching, the sag of the
  sphere chord, s²/8R (3.7 m at level 0), opens gaps between water patches of different levels.
- **Building.** `Settle()` is used for shots: select, build every needed patch with `ParallelFor`, apply,
  and repeat until stable. `Step()` is the runtime path: completed async builds are applied (at most 32 per
  frame), then the tree is selected, then up to 64 builds start on the thread pool. First builds go before
  re-stitches, and the nearest go first. A split patch keeps drawing until all 4 children are ready
  (`Ready()` is recursive). A merge draws the built descendants until the parent is ready.
- **Vertex data** (PMC gives 4 half-float UV pairs plus one 8-bit colour):
  `Color = macro albedo (sRGB) + forest density`, `UV0 = (beach zone, aridity)`,
  `UV1 = (RAe, RAr)` (the relief amplitudes the material needs), `UV2 = (log2 S, snow − wet)`,
  `UV3 = (signed river distance m, river half-width m)`. Water: `UV0 = (water depth m, river distance)`,
  `UV1.x = log2 S`. **Pitfall:** do not pack two fields into one interpolated channel. The city lights,
  quantised into UV2.y, decoded into white contour lines wherever interpolation crossed a packing step.

## 5. Materials (`Scripts/make_materials.py`, HLSL in `Scripts/hlsl/*.hlsl`)

Each material is one Custom node whose code is `struct FSurf { <common.hlsl> <land.hlsl> }; FSurf S; return S.Land(...)`.
Member functions are the usual way to define functions inside a Custom expression. The extra outputs
(Rough, Nw, Emis, AO) are Custom output pins. `tangent_space_normal = False`, so the normal is world space.

**Land (`land.hlsl`)**, with up = normalize(P), height h = |P| − R, and footprint fp:
- *Normals:* the vertex normal minus the missing relief gradient (both fractals, the same loop as C++),
  minus the missing fine gradient (7 octaves; the last 2, at 0.5 m and 0.25 m, exist only in the
  material). The mid-scale normal (no fine octaves) drives the slope rules.
- *Colour:* macro albedo × noise at 2.6 km / 700 m / 180 m / 45 m / 11 m / 2.7 m, each faded by fp, so the
  orbit view gets texture too. Dry patches and soil patches are scaled by aridity and absence of forest.
- *Forest canopy:* two 3D-Worley crown sizes (5.5 m and 10 m) mixed in clumps, crown radius 0.42–0.68 of a
  cell, a dark understorey at 0.22× in the gaps, per-crown tint, and a dome normal (0.7). When the crowns
  are smaller than a pixel it averages to 0.62× the macro colour. It is off on beaches and ponds.
- *Rock with strata:* the rock mask is `smoothstep(0.16, 0.36, 1 − cos slope)` with noise, plus a
  mountainness term. Strata are bands of 6.5 m in height, warped by noise at 140 m and 23 m, each band with
  a hashed colour among (0.27, 0.24, 0.21), (0.43, 0.39, 0.33) and (0.35, 0.27, 0.20). They fall back to
  the average when the bands are below a pixel. Crevices are darker.
- *Beach:* sand (0.60, 0.52, 0.36) where `h < 0.4…1.3 m`, only inside the *beach zone* (base height within
  4–18 m of the sea). Without the zone, every low spot of a coastal plain turned into a sand blob.
- *Wet shore* below 1.4 m: 0.55× darker, roughness 0.3. *Marsh:* darker overall, and ponds as patches.
- *Snow:* the climate snow factor (temperature with an exaggerated lapse of 0.012 °C/m) plus noise. It is
  removed on faces steeper than about 40° (`1 − cos > 0.20`) and smooths the fine normals. Albedo is
  (0.72, 0.74, 0.78) and roughness 0.55. Steep rock shows through on ridges (`mtn-4`).
- *Seabed* below 0.5 m: (0.20, 0.19, 0.15), seen through the water.
- *AO output* = `smoothstep(-0.12, 0.18, dot(up, sun))`. The sky light is a single cubemap captured above
  the ground under the camera, so without this gate it lights the night side as if it were day.

**Ocean (`water.hlsl`, Single Layer Water)**:
- *Swells:* 3 directional waves at λ 117 / 51 / 23 m with slope 0.06 and deep-water phase speed. The
  amplitude is modulated by noise at 7λ so the crests do not read as stripes. On top are 6 drifting noise
  octaves from 18 m down, with gain 0.75. Each component is faded by fp. Roughness is
  `0.035 + 1.6·sqrt(slope variance that was faded out)`, about 0.2 from orbit, which gives a broad, real
  sun glint (`mtn-2`, `coast-1`). Waves flatten in the last 6 m of depth.
- *SLW coefficients* in cm⁻¹: scattering (2e-5, 7e-5, 1e-4), absorption (4.0e-3, 6.5e-4, 2.5e-4), phase g 0.3.
  This gives a deep navy ocean from orbit and turquoise water a few metres deep, with the seabed visible.
- *Foam* goes through the SLW **Opacity** pin (Opacity = coverage of the base colour over the water). It
  is made of a swash line (depth < ~1 m), surf bands `sin(depth·2.4 − 1.3t + noise)` within 6 m of depth,
  and whitecaps at 25 m. **Pitfall:** never fade the foam *masks* by distance, only their texture. The
  first version multiplied everything by an anti-alias weight of 3 m and the foam vanished beyond about 500 m.
  The bands get their own anti-aliasing from `ddx/ddy` of the band phase.
- An opaque fallback (`M_SurfWaterOpaque`) exists, with colour tinted by depth.

## 6. Rivers (`BuildRivers`, `BuildRiverTexture`, the river block in `land.hlsl`)

- *Network.* River cells are land cells with flow > riverT (5). Downstream is the neighbour with flow ≥
  own flow and the lowest elevation, which follows the sim's monotone discharge. Chains run from each mouth
  up along the largest inflow; tributaries become their own chains and end *on the parent chain's final
  curve*, so junctions meet. On planet_7 that is 1297 cells, 295 chains and 92 577 dense points (25 m).
- *Shape.* Cell centres are smoothed (2 Laplacian passes, which removes the 60° hexagonal turns), then
  Catmull-Rom, then two lateral meanders: noise at λ = clamp(14·hw, 220, 1400) m with amplitude
  min(6hw+60, 0.3λ), and a swing at 2.6–4 km with amplitude 260 m. Both are tapered over 0.9–1.5 km at the
  chain ends. Half-width is `(5 + 4.5·sqrt(flow/riverT))·(1 ± 0.25 noise)` m, 7 to 45 m on this planet.
- *Queries.* A hash grid of 1.2 km cells holds the segments, and each query checks 27 cells. A patch with
  no river nearby skips the query (`RiversNear`).
- *Rendering, three regimes, blended:*
  1. Close up (S < 12 m): the mesh's **signed** distance across the nearest reach. It is linear across a
     straight reach, so interpolation keeps the channel edge crisp even when vertices are wider apart than
     the river. Unsigned distance gave a dotted chain of blobs (`fly-2` before the fix). The channel colour
     depends on river size, from dark (0.03, 0.045, 0.04) to silty (0.075, 0.065, 0.042), with roughness 0.07.
  2. Mid range: the river map's distance field, drawn as a line at least 45 m wide and dimmed to the
     river's real share of the pixel. This is continuous but follows no meanders under about 90 m.
  3. Orbit: the river map's coverage channel, sampled at the mip of the footprint. Coverage is the river
     area over the texel area, exaggerated 5× so rivers read as thin lines from 400 km.
  The river map is a 4096² octahedral BGRA8 texture (86.6 m texels): R = distance (0–400 m), G = half-width/64,
  B = coverage with box-filtered mips. It is built at load in 330–390 ms and is a transient `UTexture2D`
  with hand-made mips, set on a dynamic instance of `M_SurfLand`.

## 7. Engine pitfalls found (all fixed in the lab, keep them in production)

1. **`UDirectionalLightComponent::bPerPixelAtmosphereTransmittance = true`, then `MarkRenderStateDirty()`.**
   Without it the sun colour on opaque surfaces is the atmosphere transmittance at the *planet's north pole*.
   Any sun below that pole's horizon lit nothing: the whole planet rendered as blue haze over a black
   surface. The reference lab only worked because its sun was 10° above the pole's horizon.
2. **The real-time sky light captures at its own position** (`SkyLightProxy->CapturePosition`). At the
   planet centre it captures the inside of the planet, which gives black reflections on all water and
   weak ambient. The lab moves it every frame to 30 m above the ground under the camera, and gates the
   ambient with the material AO (§5).
3. **Aerial perspective is about 6× Earth's at ground level**, a side effect of the 1/S coefficient
   scaling. Forests 10–20 km away turn teal. `SkyAtmosphere.AerialPespectiveViewDistanceScale = 0.5`
   (the engine's spelling) scales only the haze on geometry, so the sky and limb keep full density. All
   final shots use 0.5. See `apscale-1.0` vs `-0.35`. This setting belongs to the sky module (C2).
4. **Materials compile asynchronously in `-game`.** Shots taken right after a material change showed the
   default grey material. The lab waits while `GShaderCompilingManager->GetNumRemainingJobs() > 0`.
5. **`FParse::Value(..., FString&)` stops at commas by default.** The reference lab's `-cam=a,b,c,d` only
   ever parsed the first number. Pass `bShouldStopOnSeparator = false`.
6. **Single Layer Water assumes world Z is up** for the light transmittance from the seabed to the surface
   (`SingleLayerWaterShading.ush`: `DistanceFromScenePixelToWaterTop` uses `.z`). On a sphere the seabed
   lighting in shallows is slightly wrong away from the poles. It is not visible in these shots. A
   production fix is an engine change or opaque-plus-custom water for shallows.
7. `TArray::Add(Array.Last())` asserts (aliasing). Copy the value first.

## 8. Performance (1080p, RTX 3060 12 GB, editor `-game` Development, 24 threads)

Two other labs were rendering on the same GPU during most runs, so the frame times are upper bounds and
noisy (±5 ms). `render` is the render-thread time, which includes waiting for the GPU in offscreen mode.

| shot | altitude | patches | land + water tris | settle | frame | GPU |
|---|---|---|---|---|---|---|
| mtn-1 | 400 km | 164 (L2) | 0.23 M + 0.13 M | 0.14 s | 13.7 ms | 7.4 ms |
| mtn-2 | 79 km | 663 (≤L4) | 0.93 M + 0.34 M | 0.26 s | 10.5 ms | 9.8 ms |
| mtn-3 | 12 km | 1063 (≤L7) | 1.50 M + 0.21 M | 0.42 s | 12.5 ms | 10.7 ms |
| mtn-4 | 1.3 km | 1604 (≤L10) | 2.26 M + 0.05 M | 0.64 s | 22.6 ms* | 11.7 ms |
| mtn-5 | 290 m | 1937 (≤L12) | 2.73 M + 0.01 M | 0.88 s | 13.0 ms | 12.3 ms |
| coast-5 | 290 m | 1686 (≤L12) | 2.37 M + 1.07 M | 0.87 s | 16.0 ms | 15.2 ms |
| forest-5 | 290 m | 2387 (≤L13) | 3.36 M + 0.20 M | 1.12 s | 16.3 ms | 15.6 ms |

\* That frame was a render-thread spike during contention; the GPU time is the reliable number.

- **Material cost:** the same mtn-5 view with the reference lab's vertex-colour materials takes 6.0 ms GPU
  and 7.1 ms per frame; with the full surface materials it takes 12.4 ms GPU and 13.5 ms per frame. So the
  HLSL ground, water and river shading costs about **6.4 ms** at 1080p. Most of it is the relief loop
  (10 `noised` per pixel, needed for the erosion state), the fine loop (7), two Worley evaluations in
  forests and the colour noises.
- **Patch build:** 3.1–3.7 ms per K=32 patch on one thread, including 3 height evaluations per vertex for
  normals and river queries. A settle from scratch takes 0.1 s at orbit and 0.9–1.1 s at 300 m with all
  threads. Loading the terrain, Loop subdivision and rivers takes 20 ms + 56 ms; the river map takes 0.35 s.
- **Streamed descent** (`-fly=20`, 400 km → 0.85 km in 20 s): average frame 10.9 ms. The worst frames,
  230–400 ms, are the 4 screenshots themselves. Excluding those, 4 frames went over 33 ms (86, 90, 59 and
  39 ms). It reached level 12 at the end. It also ran **44 914 builds** for about 1900 drawn patches.
  Most were re-stitches and intermediate levels during the fast motion (see the next steps).
- The render thread pays for every procedural mesh component through the dynamic draw path (no cached
  mesh draw commands). About 1900 components cost about 7 ms of render thread with the simple material.

## 9. What is missing, and next steps for production

1. **Mesh path.** Replace `UProceduralMeshComponent` with a custom primitive that uses cached static draw
   commands, or one instanced patch-grid vertex buffer with per-patch heights in a texture (GPU-driven).
   Do this before adding anything else. It removes render-thread cost and half-float UV limits, and lets
   re-stitches update only the edge vertices instead of rebuilding the whole patch. A re-stitch changes
   only 3·(K+1) vertices.
2. **No popping yet.** Levels switch instantly, which is acceptable in screenshots and visible in motion.
   Add geomorphing: store the parent-level height per vertex and blend in world-position offset by the
   distance ratio, as in CDLOD.
3. **Material cost.** Pass the erosion state (accumulated derivative and ridge weight) at the mesh cut-off
   per vertex, so the shader loops only over the missing octaves. Bake the fine and canopy detail into
   small tiling textures. Target about 3 ms.
4. **Ground cover close up.** Below about 1 km the forest is a Worley canopy texture and reads as a
   carpet of bumps (`coast-5`, `forest-5`). It needs the vegetation module's instanced trees within about
   1–2 km, with the canopy texture as the far LOD. Farmland, roads, towns and scorch belong to other
   modules but need vertex or texture channels, which the PMC format cannot give.
5. **Rivers.** The mid range (10–20 km) shows straight, thick lines with no meanders. A river ribbon mesh,
   or a camera-centred high-resolution distance-field tile, would fix it. River water is plain land
   material: no flow, no ripples, no SLW. Lakes (the sim's `water` field) are drawn only as marsh ponds:
   lake surfaces at their filled level need their own water section.
6. **Coast detail.** The beach is a uniform band on every gentle coast. It needs rocky versus sandy coast
   by exposure, cliffs where the sim slope is high, and river deltas. Swash foam close up is blotchy.
7. **Small leftovers.** A few dotted dark lines remain along small tributaries on hillsides at 300 m
   (probably the 2–3 m carve on slopes where the nearest reach changes between vertices). Faint LOD borders
   show on large snowfields from 12 km. Night lights were dropped from the ground (towns module).
8. **The data has only elevation and climate.** Every look decision is derived from them. With a sim that
   writes vegetation density, rivers and lakes per cell (the real `hydro` module), the same pipeline applies
   unchanged.
