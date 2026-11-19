# DEMIURGE — Rendering spec (v2)

Read `CONCEPT.md`, `FOUNDATION.md` and §0–2 of `SPEC-SIM.md` (rules, entities, events) first.
**Module APIs, the Frame uniform block and who writes it, attribute locations, shader chunk
signatures, the shadow contract, GL state rules, instance layout, model names, budgets and the
draw order are binding.** Visual technique inside each module is yours to make beautiful.

## 0. Look, calibration and budgets

- **Look**: a hand-made miniature planet. Low-poly facets with real lighting, saturated but
  natural biome colours, crisp sun shadows that stretch at the terminator, a soft blue
  atmosphere rim, drifting clouds with shadows, deep-to-turquoise oceans with foam at the
  shore, ice caps, thin rivers, tiny trees and houses, cities that glitter on the night side,
  HDR bloom on anything hot, and a tilt-shift blur when zoomed in so it reads as a diorama.
  It must look good in a still screenshot at every zoom level.
- WebGL2 only, no libraries, no image assets. Works from `file://`.
- World scale: datum radius `DM.R = 100`; one cell ≈ 1.9 units; typical terrain −6.5 … +8.5 but
  sculpting and volcanoes may reach +20 (peaks above the cloud shell rise through the clouds by
  design). All bounds use `DM.R + 22`. A tree is ≈ 0.5–0.9 tall, a house ≈ 0.25–0.4, a tower up
  to 1.4, a person ≈ 0.09.
- **Calibration (binding).** All shading is linear HDR. Albedos are linear and at most 0.85
  (snow and cloud 0.85, sand 0.45, grass 0.10–0.18, deep water 0.03). Sun-lit white = 2.5 scene
  units. `Post.set.exposure` defaults to 0.6. Bloom uses a soft-knee threshold of 3.0 scene units
  (knee 1.0): nothing diffuse blooms; only emissive surfaces, specular glint and the sun do.
  Emissive scale: lava and fire 4–8; windows and the terrain's urban night lights 8–20 (they are
  sub-pixel from orbit); lightning and impact flashes 20–40; the sun disc 40. Moonlit terrain
  keeps at least 30 % of its daylight luminance after tone-mapping (the night side must stay
  readable).
- **Quality** levels 0 / 1 / 2 (`GL.quality` on the CPU, `uScreen.w` in shaders):

  | | 0 | 1 | 2 |
  |---|---|---|---|
  | pixel-ratio cap / max megapixels (scale the drawing buffer, CSS size unchanged) | 1.0 / 1.6 | 1.5 / 3 | 2.0 / 5 |
  | anti-aliasing | off + FXAA in the final Post pass | MSAA 4× | MSAA 4× |
  | shadow map | 1024, terrain only, 1 hardware-PCF tap | 2048, 4 taps | 4096 (2048 if `caps.maxTex` < 8192), 9 taps |
  | `cloudCover` octaves (`cloudShade` never more than 3) | 3 | 5 | 5 |
  | bloom levels / tilt-shift | 3 / off | 5 / on | 5 / on |
  | particle cap | 2,000 | 4,000 | 6,000 |
  | tree instances / building instances (planet-wide) | 6,000 / 2,500 | 14,000 / 5,000 | 24,000 / 8,000 |

- **Budget** at quality 2: scene pass ≤ 1.2 M triangles, shadow pass ≤ 1.0 M, ≤ 150 draw calls.
  Target 60 fps at 1080p on a mid-range discrete GPU; usable on integrated graphics at quality 0.
- Never scan the planet's cell arrays per frame; rebuild GPU buffers only when the matching
  dirty flag says so, and throttle.
- **Reproducibility.** Static content (stars, tree jitter, settlement layouts, model geometry)
  uses `DM.U.hashf` or `DM.U.rng(constant)`, never `Math.random`. Render, FX and audio code never
  draw from `world.rng`. FX uses its own Rng, reseeded when the viewer freezes time.
- Every module looks other modules up as `DM.X.fn` at call time, performs no GL calls and no Bus
  subscriptions at load time (only in `init` / `attach`), and its `init()` must be callable
  twice (context restore).
- Files (`js/render/`, loaded in this order after the sim): `gl.js, models.js, towns.js,
  post.js, planet.js, sky.js, objects.js, fx.js, renderer.js`.

## 1. `gl.js` → `DM.GL`

```
GL.init(canvas) → bool               WebGL2 context: { antialias: false, alpha: false, depth: false, stencil: false,
                                     powerPreference: 'high-performance', preserveDrawingBuffer: false }
GL.gl                                the context
GL.caps = { hdr, floatLinear, maxSamples, maxTex }
        hdr: enable EXT_color_buffer_float (else EXT_color_buffer_half_float) and verify with a 4×4 RGBA16F framebuffer.
        maxSamples = min(4, largest SAMPLES value from getInternalformatParameter(RENDERBUFFER, <colour format>, SAMPLES)).
GL.ATTR = { aPos: 0, aNormal: 1, aColor: 2, aData: 3, aI0: 4, aI1: 5, aI2: 6, aI3: 7 }
GL.CLOUD_R = DM.R + 11               GL.ATMO_R = DM.R + 17
GL.quality                           0 | 1 | 2 (set by Renderer.setQuality)
GL.view                              set by Renderer.frame before step 1: the Camera.view() object plus `viewProj` (Float32Array(16)).
                                     Any render module may read it; treat null as far orbit.

GL.chunk(name, glsl)                 register a shader chunk; `#include <name>` on its own line in any shader (or chunk) is replaced by it (once per shader)
GL.program(name, vsBody, fsBody) → prog
        Bodies are GLSL ES 3.00 WITHOUT `#version` / `precision` lines. The helper prepends the version,
        `precision highp float; precision highp int; precision highp sampler2D; precision highp samplerCube;
        precision highp sampler2DShadow;` and the Frame block (§2), expands includes, binds the attribute names in GL.ATTR to
        their fixed locations, binds the Frame block to binding point 0 (skipped when the program does not use it), and if the
        program has a `uShadowMap` sampler assigns it texture unit 7. Compilation is kicked off immediately; link status is
        checked on the first prog.use(), which throws an Error containing the program name, the driver log and numbered source.
        prog.id, prog.u.<uniformName> (location or undefined), prog.use()
GL.mesh({ aPos, aNormal, aColor, aData, aI0, aI1, aI2, aI3, index, dynamic = [] }) → mesh
        aPos/aNormal: 3 floats per vertex; the others 4 floats per vertex (aI0–aI3 may be given as extra per-vertex vec4s on
        meshes that are never instanced). Missing attributes are left disabled and read as constants set at every draw:
        normal (0,1,0), colour (1,1,1,1), everything else (0,0,0,0). index: Uint16Array | Uint32Array | null.
        mesh.vao, mesh.count, mesh.update(name, Float32Array) and mesh.setIndex(array) (both reallocate when given a larger array
        and update mesh.count), mesh.draw(mode = TRIANGLES, count = mesh.count, first = 0), mesh.free()
GL.instances(mesh, capacity) → inst
        A VAO sharing the mesh's vertex buffers plus four per-instance vec4 attributes aI0…aI3 (divisor 1) from one interleaved
        buffer of 16 floats per instance.
        inst.data (Float32Array(16 × capacity)), inst.count, inst.upload(first = 0, count = inst.count), inst.draw(),
        inst.ensure(capacity), inst.free()
GL.target(w, h, { hdr = false, depth = false, depthTex = false, samples = 0, linear = true } = {}) → target
        Colour: RGBA16F when hdr and caps.hdr, else RGBA8; a renderbuffer when samples > 0 (with a depth renderbuffer of the SAME
        sample count), else a texture. depthTex with samples > 0 throws.
        target.fbo, target.tex, target.depthTex, target.w, target.h, target.samples, target.bind() (binds and sets the viewport),
        target.resize(w, h), target.blitTo(dst) (resolve colour; dst is single-sample, same size and format, never the canvas),
        target.free()
GL.depthTarget(size) → target with only a DEPTH_COMPONENT24 texture: TEXTURE_COMPARE_MODE = COMPARE_REF_TO_TEXTURE,
        COMPARE_FUNC = LEQUAL, LINEAR, CLAMP_TO_EDGE (shadow map)
GL.bindTex(unit, texture)            GL.toScreen() (bind the canvas, full viewport)
GL.fsTri()                           draw one fullscreen triangle
GL.FS_VS                             vertex body for fullscreen passes; provides `out vec2 vUv;`
GL.frame                             CPU mirror of the Frame block: frame.set(name, x, y, z, w), frame.setComp(name, index, value),
                                     frame.setMat(name, m4), frame.setStorm(i, x, y, z, radius), frame.upload()
GL.checkErrors(label)                dev aid: logs gl.getError() with the label; no-op unless GL.debug is true
```

The gl.js helpers bind their own VAO for every draw and every buffer operation and call
`bindVertexArray(null)` afterwards (index-buffer bindings are VAO state: a stray upload under
someone else's VAO corrupts that mesh).

## 2. The Frame uniform block (available in every program, both stages)

```glsl
layout(std140) uniform Frame {
  mat4 uViewProj;      // world → clip (the light's matrix during the shadow pass)
  mat4 uView;          // world → eye
  mat4 uInvViewProj;   // clip → world (camera)
  mat4 uShadowMat;     // world → shadow-map texture space (xyz in 0..1)
  vec4 uCamPos;        // xyz eye position, w height of the eye above the datum sphere
  vec4 uSunDir;        // xyz unit vector toward the sun, w light scale (1; 0.4 without HDR targets)
  vec4 uSunCol;        // rgb sun colour, w ambient strength
  vec4 uTime;          // x real seconds, y sim year, z cloud drift time, w frame dt
  vec4 uScreen;        // x,y framebuffer size in px, z zoom01 (0 far … 1 near), w quality 0|1|2
  vec4 uPlanet;        // x DM.R, y displayed sea level, z GL.CLOUD_R, w GL.ATMO_R
  vec4 uBrush;         // xyz unit direction of the brush centre, w angular radius (0 = hidden)
  vec4 uBrushCol;      // rgb, a (a < 0.5 = draw the ring dashed: invalid target)
  vec4 uFlash;         // xyz world position of a transient light, w radius (0 = none)
  vec4 uFlashCol;      // rgb intensity of that light
  vec4 uWorld;         // x heat (1 molten … 0 cooled), y dust 0..1, z night-light level 0..1, w overlay mode
  vec4 uFx;            // x cloud opacity (fades as the camera descends), y cloudiness 0..1, z ocean fraction 0..1, w free
  vec4 uStorms[8];     // xyz unit direction, w angular radius (0 = unused)
};
```

**Ownership.** Only `renderer.js` writes and uploads the Frame block, with one exception:
`Sky.update` overwrites `uFx.y`, `uFx.z` (via `setComp`) and `uStorms` (via `setStorm`).
Each frame, before step 1 of §4, `Renderer.frame` writes: the matrices, `uCamPos`, `uSunDir`,
`uSunCol`, `uTime` (z = real seconds × 0.01), `uScreen`, `uPlanet` = (`DM.R`, displayed sea
level, `GL.CLOUD_R`, `GL.ATMO_R`), `uBrush`, `uBrushCol`, `uFlash`, `uFlashCol`,
`uWorld` = (`world.heat`, `world.dust`, `[0.12, 0.2, 0.35, 0.5, 0.8, 1, 1][world.age]`,
`DM.Planet ? DM.Planet.overlay : 0`), `uFx` = (`smoothstep(14, 45, altitude)`, 0.5, sea-cell
fraction, 0) and zeroed `uStorms`. The displayed sea level eases toward `world.seaLevel` at
about 1 unit per second (so comets make the sea *rise*). The renderer uploads the block twice
per frame: after step 1 with `uViewProj` = light matrix (shadow pass), and again with the camera
matrix (scene pass).

## 3. Shader chunks (registered by `gl.js`, used by everyone, edited by nobody else)

```glsl
// #include <noise>
float hash13(vec3 p);                 // 0..1
vec3  hash33(vec3 p);                 // 0..1 each
float vnoise(vec3 p);                 // smooth value noise 0..1
float fbm3(vec3 p);                   // 3 octaves, 0..1
float fbm5(vec3 p);                   // 5 octaves, 0..1

// #include <clouds>      (includes <noise>)
float cloudCover(vec3 dir);           // 0 clear … 1 solid cloud above unit direction dir (drifts with uTime.z, banded by
                                      // latitude, denser inside uStorms, scaled by uFx.y; octaves by quality)

// #include <lighting>    (includes <clouds>; declares `uniform sampler2DShadow uShadowMap;`)
float dayAt(vec3 worldPos);           // 0 night … 1 day, soft terminator
float sunShadow(vec3 worldPos, vec3 normal);   // 1 lit … 0 in shadow. Offsets the lookup along the normal by 1.5 texels,
                                               // PCF taps by quality, returns 1 outside the map and fades to 1 over its outer 8 %
float cloudShade(vec3 worldPos);      // 1 clear … ≈ 0.45 under thick cloud (≤ 3 octaves)
vec3  lightAt(vec3 worldPos);         // unshadowed light for volumes: sun colour × dayAt × cloudShade + night ambient + the uFlash light
vec3  shade(vec3 albedo, vec3 normal, vec3 worldPos, float specular, float emissive);
                                      // full surface lighting, linear HDR: sun N·L × sunShadow × cloudShade, sky fill, cool
                                      // moonlight on the night side, optional specular, the uFlash light, plus albedo × emissive;
                                      // everything scaled by uSunDir.w
vec3  inscatter(vec3 rayOrigin, vec3 rayDir, float tMax);
                                      // sunlight scattered into a view ray over [0, tMax] inside the shell of radius uPlanet.w:
                                      // blue by day, orange near the terminator, faint at night, scaled by uFx.z, dimmed by dust
vec3  atmosphere(vec3 color, vec3 worldPos);   // aerial perspective = color × transmittance + inscatter(uCamPos.xyz, dir, distance)
```

Every lit surface (terrain, water, objects) goes through `shade` then `atmosphere`; clouds and
non-emissive particles use `lightAt` (clouds then `atmosphere`). The halo outside the planet's
silhouette is the same `inscatter`, so the limb matches by construction. Depth-pass programs
must not include `<lighting>` or `<clouds>`.

## 4. Frame (done by `renderer.js`)

1. **CPU work**: copy `world.dirty` into `Renderer.dirty` (same keys) and set every
   `world.dirty` flag to false; then `Planet.sync(world, dt, dirty)`,
   `Objects.sync(world, dt, dirty)`, `Sky.update(world, dt)`, `FX.update(world, dt)`.
   Render modules read the copy and never write `world.dirty`; a module that throttles keeps its
   own pending flags.
2. **Shadow pass** (skipped at quality 0 for objects; see §9): the renderer binds the shadow
   target, clears depth, sets `colorMask(false ×4)`, `POLYGON_OFFSET_FILL` with
   `polygonOffset(1.5, 4.0)`, cull off, `GL.bindTex(7, null)`; then `Planet.drawDepth()`,
   `Objects.drawDepth()`.
   *Fit*: centre c = `GL.view.target`; radius r = `clamp(1.6 × |eye − target|, 12, DM.R + 22)`
   (the whole planet above an eye-to-target distance of 76). Light space: z along `uSunDir`,
   x and y from a basis built from world +Y (from +X when the sun is within 5° of a pole). The
   ortho box is `[c.x − r, c.x + r] × [c.y − r, c.y + r]` with c.x, c.y snapped to multiples of
   `2r / size`; the z range always spans the whole planet (±(`DM.R + 22`) from the centre) so
   every caster between the sun and the box is included.
3. **Scene pass** into `Post.scene`, cleared to black, camera matrices, `GL.bindTex(7, shadow
   depth texture)` (a 1×1 depth texture cleared to 1.0 when shadows are off):
   1. `Sky.drawBack()` — stars, sun, moon. No depth test or write.
   2. `Planet.drawTerrain()` — opaque.
   3. `Objects.draw()` — opaque instanced meshes.
   4. `Planet.drawWater()` — ocean (writes depth), then lakes, rivers, roads (no depth write).
   5. `FX.draw(0)` — particles below `GL.CLOUD_R`.
   6. `Sky.drawClouds()` — blended shell; depth test on, write off.
   7. `Sky.drawAtmosphere()` — halo; depth test on, write off.
   8. `FX.draw(1)` — particles at or above `GL.CLOUD_R`, plus bolts, trails and rings.
4. `Post.run()` — resolve, bloom, tilt-shift, tone-map and grade to the canvas.

**Canonical GL state** on entry to and exit from every draw function: DEPTH_TEST on,
`depthFunc(LEQUAL)`, `depthMask(true)`, BLEND off, CULL_FACE off with `cullFace(BACK)` and
`frontFace(CCW)`, SCISSOR_TEST off, no VAO bound, and colour mask and POLYGON_OFFSET_FILL as the
renderer set them for the pass (a scene-pass draw that enables polygon offset disables it
again; `drawDepth` never changes colour mask, polygon offset or cull). Whoever enables BLEND
sets `blendFunc` first. Only `target.bind()` and `GL.toScreen()` change the framebuffer and
viewport. Texture units 6 and 7 are reserved for the renderer; modules use 0–5. The renderer
re-asserts the canonical state before each numbered step.

**Isolation.** The renderer calls each module inside try/catch; a module that throws is
disabled for the rest of the session (one `console.error`), so one broken module cannot blank
the frame. A module that is not loaded is skipped. `Renderer.init` creates every program before
the first frame. The renderer handles `webglcontextlost` (preventDefault, stop drawing) and
`webglcontextrestored` (`GL.init`, every module's `init()`, then `setWorld(world)`).

## 5. `post.js` → `DM.Post`

```
Post.init()                          Post.resize(w, h)                 (reads GL.quality)
Post.scene                           the scene target (MSAA 4× HDR with depth at quality ≥ 1; single-sample at quality 0)
Post.beginScene()                    bind + clear the scene target
Post.run()                           resolve → bloom → tilt-shift → tonemap / grade / vignette / grain / fade (+ FXAA at quality 0) → canvas
Post.set = { exposure: 0.6, bloom: 0.9, tilt: 1, focusY: 0.5, vignette: 0.35, grain: 0.02, fade: 0, saturation: 1.05, flash: [r, g, b, a] }
```

Bloom: soft-knee threshold 3.0, input clamped to 64 (no fireflies), 3–5 level down/up chain.
Tilt-shift: blur grows away from a horizontal focus band at screen height `focusY`
(0 = top of the screen, 1 = bottom), strength =
`Post.set.tilt × smoothstep(0.45, 1, zoom01)`, zero when zoomed out. ACES-style tonemap, gamma
2.2. `fade` 0..1 fades to black. `flash` is a full-screen tint (rgb, a). When `!GL.caps.hdr` the
renderer sets `uSunDir.w = 0.4`, and Post divides exposure by 0.4 and skips bloom.

## 6. `planet.js` → `DM.Planet`

```
Planet.init()                        Planet.build(world)
Planet.sync(world, dt, dirty)        rebuild what dirty.elev / color / water / rivers / roads require
Planet.drawDepth()   Planet.drawTerrain()   Planet.drawWater()
Planet.groundAt(dir, hint = -1) → radius     rendered terrain radius under any direction (barycentric within the mesh triangle);
                                             hint = a cell id near dir
Planet.surfaceAt(dir, hint = -1) → radius    same, but the water surface where that point is under sea or lake
Planet.raycast(origin, dirVec, out) → bool   nearest hit of a ray with the terrain / water surface;
                                             out = { cell, pos: [x,y,z], dir: [x,y,z] (unit, from the centre), t }
Planet.setOverlay(mode)              0 none, 1 cultures, 2 temperature, 3 rainfall, 4 devotion, 5 habitability
Planet.overlay                       current mode (read-only property)
Planet.showSnapshot(snap | null)     time-lapse colouring (called only by Renderer.showSnapshot)
Planet.cellColor(world, c, out) → out   linear RGB albedo used for a cell
```

- **Terrain**: one mesh over `geo.tris`; positions `dir × (DM.R + elev)`; smooth vertex normals
  recomputed when elevation changes. Vertex colour computed on the CPU per cell from biome,
  vegetation (the ground darkens with `veg` so forests read from orbit without the trees),
  ice/snow, scorch, a continuous dryness tint from `moist` (droughts must yellow the land within
  a second), wetness, farmland, urban, sea-bed depth, with a little per-cell variation; overlays
  replace it. Farmland shows as a patchwork; lava and the molten genesis crust glow (emissive,
  animated cracks while `uWorld.x > 0`); urban cells glitter with small lights at night (scaled
  by `uWorld.z`).
  **Fragment detail is binding, not optional** (a cell is ≈ 300 px wide at close zoom): using
  `<noise>` on world position, albedo variation of ±12 % at three scales (about 6, 1 and 0.15
  units); a detail normal from the noise gradient whose strength rises with `uScreen.z`; biome
  borders broken up by perturbing the interpolated vertex colour with noise so they are ragged,
  not linear; steep facets shift toward rock; grass, sand and snow each get their own fine
  pattern. All detail fades out below zoom01 = 0.3 so it cannot alias from orbit.
  **Facets**: the face normal comes from screen-space derivatives of a plain `smooth` world
  position varying (never `centroid`; no shader in the project uses `flat` varyings).
  Facet weight = `0.85 × smoothstep(2.0, 8.0, 1.9 / length(fwidth(worldPos)))`: fully faceted
  when a cell edge spans 8 px or more, pure smooth vertex normal below 2 px and toward the limb.
- **Ocean**: sea-level independent buffers: `aPos` = unit direction, `aData` = (elev, sea ice,
  0, 0); the vertex shader places the vertex at radius `uPlanet.x + uPlanet.y` (plus gentle
  waves) and computes depth = `uPlanet.y − aData.x`. The index buffer is all of `geo.tris`: it
  is drawn after the terrain, so land triangles are depth-rejected, and with no sea the sphere
  is entirely underground. Colour by depth: transparent turquoise shallows → deep blue; animated
  foam where depth is near zero; sun glint; sky reflection toward the limb; sea ice where
  `ice > 0.5`. Analytic sphere normal plus procedural waves (never derivative normals).
- **Lakes**: emit every triangle that has at least one lake vertex, with all three corners at
  that lake's surface radius, so the shore is a depth-buffer intersection like the ocean's.
- **Rivers** (`world.rivers`, shape in SPEC-SIM §3.4; a missing or empty list means no rivers)
  and **roads** (neighbouring cells with `road > 0`): smooth the cell polyline (two Chaikin
  passes), sample it every 0.5 units or less, put each ribbon vertex at `groundAt + 0.03`, width
  from `flow / maxFlow` (rivers) or road level, draw with `polygonOffset(−2, −2)` and depth write
  off. Rivers flow (animated).
- The **brush ring** (`uBrush`, dashed when `uBrushCol.a < 0.5`) is drawn by both the terrain
  and the water shaders.
- **Snapshots**: while one is shown, `sync` leaves its pending flags alone and touches nothing.
  Colour = biome base colour, blended toward the owning culture's colour where `owner ≥ 0`, grey
  where `urban > 0`; `snap.heat > 0` renders the molten crust. `showSnapshot(null)` restores live
  colours with a full rebuild.
- Colour rebuilds may be throttled to ~6 Hz; elevation edits (the sculpt brush) must show on
  the next frame. Elevation re-upload ≤ 4 ms, colour rebuild ≤ 6 ms at L = 6.

## 7. `sky.js` → `DM.Sky`

```
Sky.init()     Sky.update(world, dt)
Sky.drawBack()         stars (thousands of points with slight twinkle and colour, a faint galactic band), the sun disc and glare, a small moon
Sky.drawClouds()       cloud shell at GL.CLOUD_R using cloudCover(); lit with lightAt(), dark and pink-edged at the terminator, passed
                       through atmosphere(); BACK faces culled when the camera is outside the shell, FRONT faces when inside; opacity × uFx.x
Sky.drawAtmosphere()   the shell sphere at GL.ATMO_R with FRONT faces culled, output = inscatter(camera, viewDir, distance to the shell's
                       far side), additive. With depth test on it is visible only outside the planet's silhouette. Haze over the disc
                       comes only from atmosphere() in the surface shaders.
```

`Sky.update` fills `uStorms` from `world.disasters` (type `'storm'`, smoothing the moving
centre) and from recent `rain` use (`power:used`), and sets `uFx.y` (cloudiness: more as the
ocean fraction grows, thin while the world has no sea) and `uFx.z` (sea-cell fraction). Optional
if cheap: aurora curtains near the poles on the night side.

## 8. `models.js` → `DM.Models` (pure data, no GL; loads in Node)

```
Models.get(name) → { aPos, aNormal, aColor, aData, index, height, radius }   (cached; flat-shaded: vertices duplicated per face)
Models.has(name)     Models.names() → [string]
```

Local space: +Y up, origin at the base centre, model units = world units. **Every model extends
0.15 below y = 0** (foundation or trunk) so it never floats on a slope. `aColor` = linear RGB
albedo (a = 1). `aData` = (emissive mask 0..1 — windows, fires; sway weight 0..1 — tree tops,
flags; tint mask 0..1 — how much of the per-instance tint this vertex takes: roofs, sails,
banners; spare 0).

Triangle limits: plants and rock ≤ 20; groves ≤ 40; homes ≤ 48; civic and movers ≤ 150.

- Plants: `tree_broadleaf, tree_conifer, tree_palm, tree_jungle, tree_dead, shrub, cactus, rock`
- Groves (3–4 trunks in one mesh, footprint ≈ 1 unit, so three instances close a cell's canopy):
  `grove_broadleaf, grove_conifer, grove_jungle`
- Homes by era: `tent, hut, house_clay, house_stone, townhouse, rowhouse, factory, tower_block, tower_glass`
- Civic: `campfire, shrine, temple_early, temple_classic, temple_late, granary, wall, pier,
  lighthouse, market, mine, academy, cooling_tower, gantry, rocket, wonder, ruin`
- Movers: `person, settlers, caravan, army, prophet, ship_sail, ship_steam, boat, deer, bird, whale`

## 9. `towns.js` → `DM.Towns` and `objects.js` → `DM.Objects`

`towns.js` (pure, no GL; loads in Node):

```
Towns.layout(world, settlement, out, groundAt = null) → count
        Deterministic (seeded by the settlement id) list of what stands where for one settlement, written into out[0..count-1] as
        { model, dir: [x,y,z] (unit), scale, yaw, tint: [r,g,b,a], emissive, anchor: null | 'chimney' | 'campfire' | 'gantry' | 'pier' }.
        Built from tier, culture colour and age, and buildings `b`: camps are a few tents round a campfire; villages are huts;
        towns gain walls, a temple, a granary; cities and metropolises spread over neighbouring urban cells with era-appropriate
        houses, factories with chimneys, towers; ports get a pier, a lighthouse and boats; a capital with a launch site shows the
        gantry and a rocket that grows with `ark`; a wonder towers over everything; a dead settlement is ruins.
        Per-settlement instance limits by tier: 6 / 14 / 40 / 110 / 260. Never places anything on sea or lake cells (except piers and boats).
```

`objects.js`:

```
Objects.init()     Objects.build(world)
Objects.sync(world, dt, dirty)
Objects.drawDepth()   Objects.draw()
Objects.anchors(kind) → Float32Array of world xyz triples for kind 'chimney' | 'campfire' | 'gantry' | 'pier' (for FX)
Objects.showSnapshot(snap | null)    hide all trees, buildings and movers; draw one culture-tinted marker (`hut` where urban < 128,
                                     `townhouse` otherwise) on each cell with snap.urban > 0, at most 2,000; null restores
```

One shared instanced program (plus its depth twin: same vertex position code, fragment
`void main(){}`). Per-instance attributes:

```
aI0 = (x, y, z, scale)                 world position of the model's base point; uniform scale
aI1 = (yaw, swayPhase, emissive, fade) yaw about the local up axis; emissive multiplier for the mesh's emissive mask; fade 0 hides
aI2 = (tintR, tintG, tintB, tintAmount)  applied where the mesh's tint mask is set
aI3 = (burn 0..1, snow 0..1, spare, spare)
```

The vertex shader builds the tangent frame from the instance position (up = normalize(pos)),
applies yaw and wind sway, and hides far-side instances in both passes: hide only when
`angle(instance direction, camera direction) > acos(p / |cam|) + acos(p / |pos|) + 0.05` with
`p = uPlanet.x + min(uPlanet.y, 0.0) − 1.0`. Emissive windows light up at night
(`1 − dayAt`) scaled by `uWorld.z`.

- **Trees and rocks** from cells: land, not a lake cell, not farm/urban-dominated; count per
  cell from `DM.State.VEG_T` (1, 2 or 3) capped by quality; species by biome; cells with
  `veg > 0.6` in FOREST, RAINFOREST or TAIGA use grove models; jittered inside the cell; placed
  on `Planet.groundAt(dir, cell)` (fall back to `DM.State.groundR`); charred on scorched cells;
  thinned deterministically by cell hash when over the planet-wide cap.
  **Never rebuild the planet's trees in one frame**: a rolling sweep over cells (≥ 500 cells per
  frame, ≤ 1.5 ms, the whole planet in about 1.5 s) fills a back buffer that is swapped in when
  the sweep completes. `dirty.objects` (re)starts a sweep; `dirty.elev` re-seats existing
  instances on the ground (radius only, no re-layout, at most 10 Hz); `dirty.color` refreshes
  burn/snow.
- **Settlements**: each keeps its own instance range, rebuilt from `Towns.layout` individually
  on `settlement:founded | tier | building | destroyed | captured`, on `culture:age` for that
  culture, and whenever a sweep passes its cell.
- **Movers**: `world.units` at a position damped toward `Units.dirOf` (`DM.U.damp`, λ = 10 per
  second, never snapped), facing their heading, ships when `unit.sea`; plus decorative life that
  is purely visual: villagers milling near settlements (close zoom only), fishing boats off
  ports, deer on rich grassland, birds, a few whales. Movers pass their current cell as the
  `groundAt` hint.
- `Objects.drawDepth()` draws nothing when `GL.view.zoom01 < 0.3` (a tree is under 5 px there)
  and nothing at quality 0.

## 10. `fx.js` → `DM.FX`

```
FX.init()     FX.attach()            (re)subscribe to DM.Bus: keep the unsubscribe functions, call them, subscribe again (no boolean guard)
FX.update(world, dt)     FX.draw(layer)          layer 0 = below GL.CLOUD_R, 1 = at or above it, plus bolts, trails, rings
FX.spawn(type, pos, opts)            generic emitter for other modules. Binding types: 'smoke' | 'dust' | 'sparks' | 'splash' | 'steam' | 'motes';
                                     opts { count = 8, size = 1, color = null, rise = 1 }; unknown types are ignored
```

A pooled CPU particle system drawn as instanced camera-facing quads. All FX fragments output
premultiplied alpha under one blend state, `blendFunc(ONE, ONE_MINUS_SRC_ALPHA)` (additive =
alpha 0), so one instanced draw per layer holds both kinds in spawn order; a particle's layer is
decided each frame from its radius. Non-emissive particles multiply their colour by
`lightAt(worldPos)` (smoke must not glow at night). Effects, driven by events and by world
state near the camera (`GL.view`): comet and meteor (bright head, long tail, then flash,
shockwave ring, ejecta, dust; steam column when it hits water), lightning (branching bolt +
`Renderer.flash`), volcano (ember fountain, lava glow, tall smoke column; `world.disasters`),
fires (flames and smoke on `world._fires` cells near the camera), rain and storm streaks,
tsunami front (`data.front`), quake dust and `camera:shake`, `miracle:witnessed` (gold motes
rising from each listed settlement when good > 0, red when good < 0), a thin pale-gold column
above each praying settlement (visible from orbit), a soft beacon at `Civ.bestSite(world)`
during phase `'people'`, sparkles on founding, light rays on age advance, battle puffs, plague
miasma, nuclear flash and mushroom cloud, the Ark's rocket plume, chimney and campfire smoke
(`Objects.anchors`). Request camera shake with `DM.Bus.emit('camera:shake', { amount })`
(0..1). Particle cap by quality; degrade gracefully. FX stays quiet while a snapshot is shown.

## 11. `renderer.js` → `DM.Renderer`

```
Renderer.init(canvas) → bool         GL.init + init of every loaded render module (and FX.attach()); creates every program
Renderer.setWorld(world)             build GPU data for a world (call again for a new or loaded world)
Renderer.resize()                    match the canvas drawing buffer to its CSS size × the pixel-ratio cap for the quality level
Renderer.frame(view, dt)             view from DM.Camera.view(); does §4
Renderer.pick(clientX, clientY, out) → bool     ray through a canvas pixel → Planet.raycast (ray-sphere at DM.R + geo.cellAt when Planet is missing)
Renderer.project(worldPos, out) → bool          world → canvas CSS pixels { x, y, depth }; false if behind the camera or hidden by the planet
Renderer.setBrush(dir | null, ang, rgba)
Renderer.flash(worldPos, radius, rgb, seconds)  transient light (uFlash) with decay, scaled by the `flashes` setting
Renderer.setQuality(q)               0 | 1 | 2; resizes targets and sets dirty.objects
Renderer.showSnapshot(snap | null)   the only entry point for the time-lapse: calls Planet.showSnapshot and Objects.showSnapshot, shows
                                     snap.seaLevel as the sea level, sets the night-light level from the share of urban cells, and skips
                                     FX.update / FX.draw while a snapshot is shown
Renderer.sun = { mode: 'orbit' | 'follow', dayLength: 150, angle: 0, dir: [x,y,z] }
        'orbit': the sun circles the planet once per dayLength real seconds at a small declination;
        'follow': the sun stays up and to the side of the camera so the view is always lit.
Renderer.capture(mime = 'image/jpeg', quality = 0.9) → data URL of the current frame (renders one frame first)
Renderer.freeze(t | null)            pin uTime.x / uTime.z and the sun angle to time t and step FX at a fixed 1/60 (null = live)
Renderer.dirty                       this frame's copy of world.dirty
Renderer.stats = { ms, draws, tris, instances, particles }
Renderer.slow                        true when the 2-second mean of stats.ms has exceeded 26 for 4 s (Game lowers the quality)
```

## 12. Dev viewer and fixtures

- `dev/view.html` + `dev/view.js` (owned by the render core author): loads every source file in
  order (placeholders exist for all of them, so nothing 404s; `DM.X === undefined` means "not
  built yet"), creates a world, and renders it with whatever render modules exist, with a
  built-in orbit camera when `DM.Camera` is undefined.
  URL parameters: `seed`, `years` (advance the sim with `Sim.advance` if it is loaded),
  `fixture=1` (use `DM.Fixtures.world`), `zoom` (0..1), `lat`, `lon` (degrees), `sun`
  (`orbit|follow`), `sunAngle` (degrees), `phase` (`molten|water|life|history`), `overlay`,
  `q` (quality), `only=planet,sky` (use only these render modules), `t=<seconds>` (freeze time:
  with `t` set, two runs must give the same picture), `size=WxH`.
  `window.DV = { world, ready, cam(latDeg, lonDeg, zoom), tick(n), settle(frames = 3) → Promise,
  find(what), pick(x, y), stats(), freeze(t), frameMs() }`. `DV.ready` is a boolean, false until
  the world is built and two frames have rendered with every dirty flag consumed.
- `dev/fixtures.js` → `DM.Fixtures` (its own author; pure JS that also loads in Node):
  `Fixtures.world({ seed = 7, L = 6, populate = true }) → world` builds a planet with seas,
  climate and biomes using whatever sim modules are loaded (closed-form fallbacks otherwise, like
  `tests/helpers/world.js`), and `Fixtures.populate(world)` adds a plausible mid-history
  civilization **without running the sim**, created through the `DM.State.new*` factories:
  settlements of every tier and age across 3 cultures (with territory in `owner`, farms, urban
  cells, `_cells`), every building flag somewhere, a capital with a launch site and a half-built
  Ark, a ruined settlement, roads, rivers in the SPEC-SIM §3.4 shape, units of every type on
  valid paths (one at sea), a burning forest (`fire` + `_fires`), a lava field, an active
  volcano, storm, drought and tsunami in `world.disasters`, settlements with plague / famine /
  blessed / inspire / a prayer, a living prophet with an active edict, one war and one trade
  relation, ≥ 40 chronicle entries across weights 1–3, ≥ 12 snapshots, `god.awe` 180 of
  `aweMax` 400. `Fixtures.ending(world, kind)` sets `world.ending` for kind `'ark-beloved' |
  'ark-feared' | 'ark-forgotten' | 'ark-parting' | 'silence-quiet' | 'silence-ash'`.
