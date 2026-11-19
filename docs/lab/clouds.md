# Weather-driven volumetric clouds: lab reference (CloudLab)

Lab: `UnrealLab/CloudLab` (a copy of `UnrealLab/PlanetLab`, same module name `PlanetLab`). Engine: UE 5.7,
RTX 3060 12 GB, 1080p, TSR, Lumen. Everything below was rendered with `shot.ps1` / `views.ps1` and checked by eye;
every number comes from a `ProfileGPU` dump in the scenario log.

Reference images (all `shots/ue-cloudlab-*-final.png`):

| view | file | camera |
|---|---|---|
| full-orbit day | `ue-cloudlab-day-final.png` | `-cam=5,15,15,30 -sun=40,25` |
| terminator + moon + stars | `ue-cloudlab-terminator-final.png` | `-cam=5,20,15,30 -sun=110,10 -moon=-13,6,25,27 -stars=3` |
| hurricane at mid altitude (30 km) | `ue-cloudlab-cyclone-final.png` | `-pov=3,15,30,0,-50,50 -sun=70,18` |
| below the clouds, 30 m above the sea | `ue-cloudlab-ground-final.png` | `-pov=12,8,0.03,30,22,75 -sun=60,30` |
| cloud shadows on land (8 km) | `ue-cloudlab-shadows-final.png` | `-pov=10,-52,8,0,-65,60 -sun=-100,25` |
| night side, moonlit | `ue-cloudlab-night-final.png` | `-cam=5,215,10,30 -sun=110,10 -moonlux=0.5 -moon=12,-5,25,27 -stars=3` |
| weather map preview | `ue-cloudlab-weathermap.png` | `-mapout=<png>` |

Iteration history is kept as `ue-cloudlab-*-v1 ... v26.png` (v2/v6 popcorn, v13 distance LOD, v15 ground fix,
v18 canopy, shadow tests).

## 1. Architecture (what works)

```
planet fields (moist, temp, elev, land)                    C++  CloudWeather.cpp
   -> 0.5 deg climate grids (Gaussian splat of the 40,962 cells)
   -> weather systems: 2 tropical cyclones, 3 extratropical lows per hemisphere
   -> per texel of a 4096x2048 equirectangular map (ParallelFor, 1.0-1.7 s):
        R presence field (0.5 = cloud edge), G cloud-top height, B cumuliform, A optical thickness / storm core
   -> UTexture2D::CreateTransient + hand-built mip chain
tileable noise volumes 128^3 shape + 64^3 detail            C++  CloudNoise:: (0.3-0.5 s)
                                   |
M_WeatherClouds (Volume domain, Additive, UsedWithVolumetricCloud)   Python make_clouds.py
   Custom HLSL node cloud_density.hlsl: world pos -> lat/lon -> weather map, 3D noise, height profile
   UMaterialInstanceDynamic per run: textures + scalar parameters
```

Files (all under `UnrealLab/CloudLab/`):

- `Source/PlanetLab/CloudWeather.h/.cpp`: `FCloudWeather` (climate grids, cyclones, map, texture, PNG preview) and
  `CloudNoise::MakeShapeVolume/MakeDetailVolume`.
- `Source/PlanetLab/LabGameMode.cpp`: `SetupClouds()` (component, light, MID), `SetupStarsAndMoon()`, `PlaceMoon()`,
  `-pov=` camera, sample / shadow profiles by altitude, perf logging, waiting for shader compilation.
- `Scripts/make_clouds.py` (creates `/Game/CloudLab/M_WeatherClouds`, `M_StarSky`, `M_Moon`; run with `mats.ps1`),
  `Scripts/cloud_density.hlsl`, `Scripts/star_sky.hlsl`, `Scripts/moon_albedo.hlsl` (read by the script at build time).
- `shot.ps1` (one view), `views.ps1 <tag> [-profilegpu]` (all six views), `profsum.sh <log>` (pass table from a
  ProfileGPU dump), `build.ps1`, `mats.ps1`.

### 1.1 Weather map (C++)

`FCloudWeather::Build` (read the code; the comments carry the reasoning). Key points:

- **Climate grids.** Each sim cell is splatted with a Gaussian (sigma 0.9 deg, reach 2.4 deg) into 720x360 grids of
  moisture, temperature, land fraction and elevation, then sampled bilinearly. A "land to the east" grid (land in the
  next 1..15 deg of longitude) finds the cold west coasts of continents for stratocumulus decks. Direct nearest-cell
  lookup gives visible Voronoi cells; do not do that.
- **Regimes** (latitude belts times the sim's moisture; the sim's moisture already has ITCZ / subtropical-dry /
  mid-latitude / polar structure, which helps a lot):
  - ITCZ deep convection: Gaussian band (8 deg) around `ItczLatDeg` (default 5 N, wavy, pushed poleward over land),
    clustered by low-frequency noise; tall (G 0.95), cumuliform.
  - Trade-wind cumulus: ocean, 8-40 deg, small cells, low tops (0.3).
  - Marine stratocumulus decks: subtropical ocean with land to the east, low flat tops (0.22).
  - Mid-latitude frontal sheets: band at 52 deg (13 deg wide), noise sampled with z scaled 20 vs xy 7 so it streaks
    zonally like jet-stream cloud.
  - Polar stratus poleward of 55-72 deg; orographic cloud over high moist land; dry subtropical land cleared
    (`Clear = 1 - 0.92 * dryLand`).
  - Regimes combine as a probabilistic union `1 - prod(1 - c_i)`; top height / type / thickness are weighted means.
- **Cyclones.** Tropical: searched along `TropicalLatDeg` (17 N) for the open-ocean longitude nearest the preferred
  one, radius 9 deg (about 16 km on this planet), plus a smaller southern one with the opposite spin. Extratropical:
  3 per hemisphere at 44-58 deg, radius 10-15 deg.
  - All cyclones rotate the *noise lookup* about their centre by `-Sign * swirl(r)` (Rodrigues), which shears every
    noise field into spiral arms. Swirl strength 7 (tropical) / 3.2 (extratropical), `exp(-1.6 r)` / `exp(-1.1 r)`,
    and it **must fade to exactly 0** by r = 3 (`* (1 - smoothstep(1.8, 3, r))`) or there is a circular seam.
  - Tropical structure: clear eye `smoothstep(0.05, 0.085, r)`, eyewall ring at r = 0.11, central dense overcast
    `exp(-(r/0.36)^2)`, two log-spiral rain bands `cos(2 (theta + Sign 1.9 ln r) + noise)` squared, made wavy by
    low-frequency phase noise and broken into segments by a second noise. Without the segmenting the bands read as
    concentric onion rings.
  - Extratropical comma: one long log-spiral arm (`k = 1.05`, width shrinking with r) plus a head near the low.
  - Spiral sense: NH cyclones turn the same way as planet longitude increases (counter-clockwise seen from above the
    north pole when the planet spins with positive yaw); rain bands traced outward turn the other way.
- **Presence field (the most important decision).** R is not a coverage *fraction* but a field whose 0.5 contour is
  the cloud edge. The map itself carries cloud-cell texture down to its texel size (0.15 km at 4096x2048):
  `R = 0.1 + 0.9 cover + amp(type) * Nf * (0.5 + cover)` with `Nf` = fBm at 70 / 190 / 420 cycles per radius,
  `amp` 0.35 for sheets and 1.1 for heaps. Heaps become separate puffs, sheets stay solid with holes.
- **A channel** = optical thickness; values above ~0.62 are reserved for hurricane cores (cirrus canopy, see 1.3);
  keep the other regimes below that (ITCZ uses 0.55).
- **Texture.** `UTexture2D::CreateTransient(W, H, PF_B8G8R8A8, name, bytes)`, `SRGB=false`, `TF_Trilinear`,
  `AddressX=Wrap`, `AddressY=Clamp`, `TC_VectorDisplacementmap` (sampler type must match the material's default
  texture, here a linear `TC_DEFAULT` engine texture), `NeverStream=true`, and the mip chain built by hand:
  `new FTexture2DMipMap(w, h, 1)`, `BulkData.Lock / Realloc / Unlock` per level, then `UpdateResource()`.
  Transient textures otherwise have only mip 0.

### 1.2 Noise volumes (C++)

Generated at startup (`CloudNoise::`), tileable, `UVolumeTexture::CreateTransient(S, S, S, PF_B8G8R8A8)`,
`TC_Masks` (matches the engine's default noise volumes so the sampler type matches), `TA_Wrap`.

- Shape 128^3: R = Perlin fBm (4/8/16 cells) minus Worley creases `Pn - 0.3 (1 - W4fbm) - 0.12 (1 - W8fbm)`,
  **histogram-equalised** so R is uniform in [0,1] (mean 0.5). G/B/A Worley fBm at 8/16/32 cells (spare).
- Detail 64^3: R/G/B Worley fBm at 2/4/8 cells, A Perlin.
- Pitfall: Worley-dominated shape noise reads as polygons (v2-v5). Keep Perlin dominant and Worley as billows.

### 1.3 Material (Python + Custom HLSL)

`Scripts/make_clouds.py` builds `M_WeatherClouds`: `material_domain = MD_VOLUME`, `blend_mode = BLEND_ADDITIVE`,
`used_with_volumetric_cloud = True`. One `MaterialExpressionCustom` (CMOT_FLOAT4) holds the density function
(`cloud_density.hlsl`) with inputs: `WorldPosition`, `CameraPositionWS`, `CloudSampleAttribute.NormAltitudeInLayer`,
`CloudSampleAttribute.ShadowSampleDistance`, `Time`, `SkyAtmosphereLightDirection`, three `TextureObjectParameter`s
and 24 scalar parameters. Outputs: x density, y albedo scale, z sky AO, w conservative density.

- Extinction = `density * ExtinctionPerM` (0.06 /m) into **Subsurface Color** (that pin is "Extinction" for volume
  materials; it needs a float3, so `AppendVector` twice). Albedo (0.96) into Base Color, AO into Ambient Occlusion.
- `VolumetricAdvancedMaterialOutput`: 2 multiple-scattering octaves, ray-marched volume shadows on, ground
  contribution off, and **phase / MS as scalar parameters wired into its pins** (`PhaseG 0.6`, `PhaseG2 -0.25`,
  `PhaseBlend 0.35`, `MSContribution 0.5`, `MSOcclusion 0.5`, `MSEccentricity 0.6`) so a MID can retune them.
  ConservativeDensity = the map's presence value (lets the tracer skip empty sky).
- Inside the HLSL:
  - lat/lon from the world position (planet at the origin): `lon = atan2(D.y, D.x) - LonOffset`,
    `uv = (frac(lon/2pi + 0.5), 0.5 - lat/pi)`, `Texture2DSampleLevel(Weather, WeatherSampler, uv, 0)`. Always
    `SampleLevel`: there are no useful derivatives in the cloud ray march. No seam is visible at the date line or
    the poles at 4096x2048.
  - `F = R + (shape - 0.5) * NoiseAmp * lerp(0.35, 1, cum)`, plus macro variation (`MacroKm` 24, amp 0.35) and, near
    the camera, a mid octave at `ShapeKm/5` (amp 0.6). The 3D noise only perturbs the map; it never decides
    coverage on its own (v6: equalised noise thresholded by coverage = uniform popcorn and the hurricane vanished).
  - Local cloud tops: `localTop = top * lerp(1, saturate(0.55 + 1.2 (R_mip3 - 0.5) + 0.6 (shape - 0.5)), cum)`
    using a blurred mip (`TopMip` 3, about 1.2 km). Using the full-resolution R made spiky, cardboard-cut towers.
  - Height profile: base ramp `hl / lerp(0.22, 0.06, cum)` (flat bases for heaps), top `sqrt((1-hl)/lerp(0.45,0.7,cum))`.
  - `d = saturate((F * prof - 0.5) * EdgeSharp)`, `EdgeSharp` 4.
  - Veil: thin cloud where `0.3 < F < 0.5` (`VeilAmt` 0.015, scaled down to 25% near the camera because it reads
    as fog close up).
  - Detail erosion with the 64^3 Worley at `DetailKm` 1.6, amount 0.35, inverted at the base (wispy) and normal at
    the top (billowy), only near the camera.
  - Cirrus canopy: where A > 0.62 (hurricane CDO) a thin smooth sheet at 0.78-0.97 of the layer, `CanopyAmt` 0.35,
    textured by the macro noise. This is what turned the CDO from a field of popcorn into a hurricane.
  - **Distance LOD**: `lod = saturate((dist_km - 30) / (250 - 30))` fades the 3D noise to 35%, halves EdgeSharp and
    drops detail and the mid octave. Without it the orbit view is grey speckle (sub-pixel noise; v12 vs v13).
  - **Shadow evaluations are cheap**: `ShadowSampleDistance > 0` for both secondary shadow rays and the cloud
    shadow-map pass (the shadow-map pass passes the distance along the light ray). We skip the mid octave and the
    detail erosion there. Before this the shadow-map pass cost 5.5 ms near the ground.
  - **Night-side ambient**: UE feeds clouds a single "distant sky light" value computed for the whole planet, so
    night-side clouds glowed with daylight ambient (v3). The material multiplies AO by
    `smoothstep(-0.10, 0.30, dot(D, SunDir)) * SkyAmb (0.8)`. No SkyLight actor: a real-time-capture SkyLight in
    space captures the sunlit planet and lights the night side (v2).

### 1.4 Component, light and renderer settings

Cloud layer (km): bottom 0.25, height 1.6 (Earth's 1.5 km base and 10 km tops times S = 1/6.15). Planet radius =
sea radius. `TracingStartMaxDistance` 2000 km, `TracingMaxDistance` 300 km,
`SetbUsePerSampleAtmosphericLightTransmittance(true)`, `SkyLightCloudBottomOcclusion` 0.5.

Sample budget by camera altitude (do this every frame in production):

| altitude | ViewSampleCountScale | r.VolumetricCloud.DistanceToSampleMaxCount | shadow samples / distance |
|---|---|---|---|
| < 15 km | 2 (192 samples) | clamp(alt * 0.3, 3, 15) km | 2 (20) / 4 km |
| >= 15 km | 1 (96) | same formula (15 at orbit) | 2 / 4 km |

Defaults (15 km over 96 samples) give 13 samples through a 1.6 km layer seen from below: heavy grain (v12 ground).

Directional light (sun): `bCastCloudShadows`, `CloudShadowStrength 1`, `CloudShadowOnSurfaceStrength 1`,
`CloudShadowOnAtmosphereStrength 0` (any value > 0 draws streaky light shafts across the whole disc from orbit),
`CloudShadowRaySampleCountScale 2`, `CloudShadowMapResolutionScale 4` (2048^2), and the extent by altitude:

```
CloudShadowExtent_km = clamp(cameraAltitudeKm * 1.2 + 8, 12, 130)   // 130 = 1.3 R covers the disc from orbit
```

The shadow map is an ortho map centred on the ground under the camera, 2 * extent wide; its depth range is
4 * extent. With 130 km near the ground the shadows were blurred **and displaced**; with 31 km at 8 km altitude they
were 200 m rectangles (texel 30 m, traced at half resolution by the temporal filter, stretched by 1/sin(sun
elevation)). 15.6 km at 8 km altitude gives cloud-shaped shadows for 2.2 ms. 4096^2 without temporal filtering also
works but costs 16.5 ms.

`Config/DefaultEngine.ini` `[SystemSettings]` additions:

```
r.VolumetricCloud.ViewRaySampleMaxCount=512
r.VolumetricCloud.ShadowMap.MaxResolution=4096
r.VolumetricCloud.ShadowMap.TemporalFiltering.NewFrameWeight=0.25   ; half-res temporal shadow map, 6.4 -> 2.6 ms
```

Leave `r.VolumetricCloud.ShadowMap.RaySampleMaxCount` at its default 128: 32 made blocky shadows and did not save
time. `r.VolumetricRenderTarget.Mode` stays 0 (quarter-res trace, half-res reconstruct). Mode 3 (full res) removes
grain but costs 2-3x; mode 1 does not help.

### 1.5 Stars, moon, moonlight

- `M_StarSky`: unlit, two-sided, **not** `is_sky` (an `is_sky` mesh at 20,000 km printed "skydome does not cover
  the screen" warnings over the image). An Engine `BasicShapes/Sphere` scaled 1e7 (radius 5,000 km) around the
  origin. Stars from three hashed cell grids on the view direction (Gaussian dots of about 1 px, colour tints), plus
  a faint milky-way band. Do not use cell hashes in the band: they show up as blocks (v16).
  `StarBrightness` 3 reads in the terminator shot at EV 0.
- Moon: `-moon=az,el,distR,radiusKm` places a sphere (radius 27 km, 25 R away) relative to the camera view, with
  `M_Moon` (maria and crater rims from object-space value noise). Lit by the sun, so its phase is geometric.
- Moonlight: optional second directional light, `AtmosphereSunLightIndex 1`, 0.5 lux (sun 10 lux), colour
  (0.75, 0.85, 1.0), no cloud shadows. It lights night clouds faintly and puts a moon glint on the sea
  (`night-final`). This is an artistic exaggeration (real moonlight is 1/400,000 of the sun).

## 2. Performance (1080p, RTX 3060, TSR, Lumen, `ProfileGPU`)

| view | Scene total | VolumetricCloudShadow | VolumetricCloud (view) | PostProcessing |
|---|---|---|---|---|
| day orbit | 10.3 ms | 2.1 | 2.2 | 3.2 |
| terminator | 12.2 ms | 3.0 | 3.0 | 3.3 |
| hurricane 30 km | 10.1 ms | 3.3 | 1.2 | 3.2 |
| ground 30 m | 8.7 ms | 2.1 | 1.6 | 3.4 |
| shadows 8 km | 11.3 ms | 2.4 | 3.6 | 3.2 |
| night (moonlight) | 14.6 ms | 2.6 | 5.2 | 3.4 |
| day orbit, `-noclouds` | 5.8 ms | - | - | 3.2 |

So clouds cost 3.4-6 ms (a second atmospheric light roughly doubles the view pass), and the whole frame stays at
9-15 ms of GPU, inside 60 fps. Weather map build: 1.0-1.7 s on the game thread (4096x2048, ParallelFor); noise
volumes 0.3-0.5 s. The `CloudLabPerf` frame-time line in the logs is noisy (the "frame" figure includes offscreen
present stalls); trust ProfileGPU.

## 3. Pitfalls found

1. **`FParse::Value` stops at commas** by default. `UnrealLab/PlanetLab`'s `ParseList` uses it, so `-cam=5,20,15,30`
   and `-sun=110,10` only ever parsed the first number (the other values silently kept their defaults). Pass
   `bShouldStopOnSeparator=false`. Fixed in CloudLab; the production shot harness must do the same.
2. **Shaders compile asynchronously in `-game`**: with 240 warm-up frames at 150 fps the first shot had no clouds.
   CloudLab does not count warm-up frames while `GShaderCompilingManager->IsCompiling()` (`WITH_EDITOR` only).
3. Thresholding 3D noise by coverage destroys the weather structure: make the map a presence field and let the
   noise only perturb it.
4. Night-side cloud ambient (see 1.3) and SkyLight real-time capture from orbit (lights the night side).
5. Cloud shadow map extent: blurred, displaced or blocky shadows unless it shrinks with altitude (see 1.4).
6. Thin veil and canopy layers show the volumetric render target's checkerboard as hatching when seen at long
   range through thin density; keep thin layers to where they matter.
7. Spiral warp must fade to zero smoothly, or a circle appears at the cutoff radius.
8. Custom-node texture inputs need sampler types that match the parameter's default texture (we use
   `TC_VectorDisplacementmap`, linear, for the map and `TC_Masks` for the volumes).
9. `UMaterialEditingLibrary` link failures are silent; `make_clouds.py` logs `link failed` for each failed connection.

## 4. What is still missing (for the production `sky` module)

- **Weather over time.** The map is static. Production plan: rebuild the map from the sim's moisture / wind / storm
  state every few game days, time-sliced over rows on a worker thread (1.7 s total today, so about 30 ms per frame
  over 60 frames), upload with `UpdateTextureRegions`, and crossfade two maps in the material (`lerp` by a
  parameter). Between rebuilds, drift with `LonOffset` per latitude band (trade winds westward, westerlies
  eastward) and move the cyclone centres (they are just parameters in `FCloudCyclone`).
- **Planet rotation.** The game spins the planet under a fixed sun; the cloud component is world-fixed. Set the
  material's `LonOffset` = planet yaw in radians every frame (the lab parameter exists; not exercised in the shots).
  Season: shift `ItczLatDeg` (default 5) by +-5 deg.
- Sim hooks: storm entities (from SPEC-SIM) should spawn / move `FCloudCyclone`s; Rain power = a local cover bump;
  lightning flashes in storm cells (emissive term in the material, keyed by A); aurora.
- Low sun on land: at 15 deg sun elevation shadow edges still step a little; consider `CloudShadowMapResolutionScale`
  6-8 for cinematics.
- Ground view: the overhead cumulus is good, but its base lacks fine wisps at 200-500 m (detail noise is soft).
  The ocean in this lab is dark and smooth, which makes cloud shadows on the sea hard to see: that is the ocean
  material, not the clouds.
- Rain shafts / virga under storm cells, cumulonimbus anvils outside hurricanes, contrails, fog banks.
- The weather map is not seamless in *time* yet (rebuilding moves the noise).
