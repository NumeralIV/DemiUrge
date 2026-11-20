# art/wonders-b: wonders 4 to 6 and the two T-002 wonders

Five wonders for DEMIURGE, built by script to `docs/ASSETS.md`. Everything is original and procedural (licence `original`). There are no downloads and nothing was edited by hand.

| Asset id | Mesh token | Concept | Footprint | Height |
|---|---|---|---|---|
| `wonder-4-industry` | `ChoirOfChimneys` | `docs/concept/wonders/wonder-4-industry.png` | 180 m | 97 m |
| `wonder-5-atomic` | `EarOfTheSky` | `docs/concept/wonders/wonder-5-atomic.png` | 363 x 409 m (berm and control building) | 111 m |
| `wonder-6-space` | `Ladder` | `docs/concept/wonders/wonder-6-space.png` | 404 m (the tether is a separate mesh) | 26 m (tether 3 km) |
| `wonder-sunwell` | `Sunwell` | `docs/concept/landmarks/wonder-sunwell.png` | 352 m (avenues included) | +50 m / -27 m pit |
| `wonder-stair-of-rains` | `StairOfRains` | `docs/concept/landmarks/wonder-stair-of-rains.png` | 312 x 305 m (its own mountain) | 128 m |

## What is here

- **`export/` (41 FBX files).** Each file holds one mesh with its LOD group: an Empty with `fbx_type = LodGroup` and children `<name>_LOD0..3`. Screen sizes are 1.0, 0.35, 0.12 and 0.04. Files are binary FBX with tangents and face smoothing. The axes use the standard settings: Z up and front −Y in Blender, which becomes +X forward in Unreal. One Blender unit is 1 m.
  - **States:** `SM_Wonder_<Token>_{Intact,Construction,Damaged,Ruin}` for each wonder, on the same footprint. That makes 20 meshes with LOD0 to LOD3, where LOD3 is a hull of 360 to 1,500 triangles.
  - **Culture ornaments:** `SM_Wonder_<Token>_{CapA..C | CollarA..C | CrownA..C | DiscA..C | CanopyA..C}`. These are small meshes (LOD0 to LOD2) that are instanced at the ornament sockets listed in the manifest:
    - Choir: 12 chimney caps (a brass bell crown, a finned crown, or a slate cone)
    - Ear: 3 pylon collars
    - Ladder: the crown at the anchor
    - Sunwell: the sun disc and mirror
    - Stair: the pavilion canopy (a lattice dome, an upswept saddle, or a cone)

    The base meshes do not include the ornaments, so the integration must place them. Each state lists sockets only for its unbroken parts: Intact has all of them, Damaged keeps 8 of 12 chimney caps on the Choir and 2 of 3 pylon collars on the Ear, and Construction and Ruin have none. (Verifier correction: this line used to say that only Intact lists sockets.)
  - **Extras:**
    - `SM_Wonder_Ladder_Tether_{Intact,Construction,Damaged,Ruin}`: the tether is 3 km tall, and its vertex colour `Fade` runs from 1 at 1.8 km to 0 at 3 km.
    - `SM_Wonder_Ladder_Climber`: a 10 m capsule with a 1.3 m bore, which you animate along Z.
    - `SM_Wonder_Sunwell_Beam`: an additive shaft up to 1.5 km, with vertex colour `Fade`. It uses its own material, `M_Wonder_LightShaft`.
- **`textures/` (60 PNG files, 147 MB).** Each wonder has three material slots (`Structure`, `Roof`, `Detail`), each with `T_Wonder_<Token><Slot>_{D,N,ORM,M}.png` at 2048².
- **`source/<asset>.blend`.** All states, LODs, ornaments, extras, materials and culture material instances (`MI_..._A/B/C`).
- **`manifest.json`.** For each mesh it gives:
  - footprint, height, base Z, triangles per LOD, materials, textures
  - the sibling state meshes, culture variants (material instances and ornament mesh) and ornament socket transforms
  - FX sockets, lights and tree sockets
  - mesh checks, the concept image, the licence, the script and the export path

  It also has a material table (bands, emissive parameters, culture instances) and the conventions.
- **Scripts:**
  - `build_wonders-b.py`: the entry point
  - `wb_defs.py`: the asset table, atlas bands, material parameters and culture tints
  - `wb_tex.py`: band texture synthesis, built on `art/tools/dmtex.py`
  - `wb_geo.py`: the mesh kernel (revolve with morph, warp and jag; boxes, tubes, blobs, scaffolds, cranes)
  - `wb_assets.py`: the five builders, ornaments and extras
  - `wb_preview.py`: the preview rig
- **Previews:** `shots/asset-wonders-b-<asset id>-{40m,300m,1km,night,states,cultures}.png`.

## How the materials work (for the Unreal master material)

- **Atlas.** Each slot texture is four 2048×512 bands. Band *k* is UV v in [k/4, (k+1)/4].
  - u tiles freely: UVs run past 0..1 in u on purpose, so use Wrap addressing.
  - v stays inside one band. Geometry is cut into strips of one band period.
  - Band *k* covers U×V metres, with U = 4V. `manifest.json → materials → bands` gives the size of each band.
  - Repetition is visible on the largest surfaces. A world-space macro variation in the master material would hide it.
- **Base colour:** `D * lerp(BaseTint, AccentTint, M.B)`.
- **Roughness:** `ORM.G * RoughnessScale`. Metallic is `ORM.B` and AO is `ORM.R`.
- **Normals:** `N` is OpenGL (+Y), so flip green on import.
- **Night lighting:** `Emissive = M.R * lerp(EmissiveA, EmissiveB, M.G) * (EmissiveDay | EmissiveNight)`. The values for each material are in the manifest:
  - the Choir dome and windows
  - the Ear's control building (warm) and three red aviation lights (EmissiveB, blinking 1 s on and 1 s off, with the phase per pylon in `lights`)
  - the Ladder's ring windows (warm white) and the tether core (it glows by day too)
  - the Sunwell rim lamps, pool and mirror
  - the Stair's pools, falls and pavilion lantern

  Small lights use a tiny emissive spot per band. Every light vertex samples that one spot, so thin parts that share the atlas (cables, trims) do not pick up glow at far mips.
- **Culture instances.** A = Cliff-Terrace (warm rose), B = Lagoon-Weaver (silver and verdigris), C = Ring-Builder (cool slate and pewter). The tints and roughness are in `culture_instances`. Map the sim's culture index onto A, B and C, for example by hash.

## Regenerate

```
"C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python art/wonders-b/build_wonders-b.py -- [--assets choir,ear,ladder,sunwell,stair] [--retex]
python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b art/wonders-b/source/<asset>.blend --python art/wonders-b/wb_preview.py -- --asset <asset> [--views 40m,300m,1km,night,states,cultures]
```

A full build takes about 3 minutes; textures are cached, and `--retex` rebuilds them. Each preview set takes under a minute once a render slot is free. The preview dressing is not exported. That covers the trees at the tree sockets, the volumetric steam on the Choir, the 1.8 m red figure, the ground and the haze.

## Integration notes

- **Sunwell.** It is a pit: the floor is at −27 m. `terrain_cut` asks for the terrain to be hidden or lowered inside radius 101 m.
- **Ear of the Sky.** It brings its own berm (r 123 to 178 m). Flatten the ground inside 178 m.
- **Stair of Rains.** It brings its own terraced mountain. The front (−Y) is terraced over about ±84°, and the flanks and back are rock and forest. Blend its skirt (r 150 to 152 m, z 0 to −2) into the terrain. It is best placed on a hillside facing the city.
- **FX sockets in the manifest:**
  - steam: 12 plumes that should lean together into one cloud 300 to 600 m tall. This is the Choir's long-distance marker.
  - mist and a rainbow on the Stair
  - the anchor glow, and a pulse line up the tether
  - the Sunwell sky beam, which should pulse with devotion

## Known gaps

- **Ear and Ladder triangles.** At LOD0 they come to 15k to 22k triangles, below the 30k to 80k wonder range. Their concept read is a clean white circle, so extra triangles would only add noise. The other three are 36k (Choir), 45k (Sunwell) and 72k (Stair).
- **Triangle totals by mesh type (LOD0):**
  - Intact meshes: 189,500 for the five
  - all 20 state meshes: 638,279 (LOD1 258,147, LOD2 76,123, LOD3 14,038)
  - ornaments: 12,690
  - extras: 1,108
  - everything: 652,077, or 1,010,851 across all LODs (after the verification fixes)
- **Open shells.** Many parts are open where they sit on or in other parts. For example, chimney shafts stand on pedestals and the berm runs under the wall. The manifest's `non_manifold_edges` counts these. No closed part has holes that would be visible.
- **No baked maps.** UVs are tiling trim UVs, not a unique 0..1 layout, so a lightmap UV must be generated on import if one is needed. Lumen does not need one.
- **Shading in the previews.** The previews are EEVEE stand-ins: dome glass is opaque with a warm emissive glow, not translucent, and water is opaque.
- **Not built.** Burnt and Drowned states. Animated FX (steam, mist, rainbow, the blink, the tether pulse) are left to the FX and integration agents through the sockets.
- **Stair close-ups.** The blossom crowns are still procedural blobs (now clumped and smooth, see the verification fixes); real foliage cards or instanced trees would read better at 40 m. The waterfalls are opaque sheets, so a panner on u in Unreal would help.

## Independent verification (2026-11-19)

An independent verifier rebuilt the family from a clean folder (exports, textures, sources and manifest removed). The first rebuild was reproducible: all 60 textures were byte-identical and the manifest matched apart from the date. Every FBX was then re-imported into an empty Blender scene and checked:
- each file has a LodGroup empty with `_LOD0..n` children at identity transforms
- triangle counts match the manifest
- no n-gons, no zero-area faces and no inverted closed shells (signed volume per closed component)
- one `UVMap`, with every face's v inside one atlas band
- three slots in the order Structure, Roof, Detail (the beam has `M_Wonder_LightShaft`), and the `Fade` colour on the Intact tether and the beam
- pivots at the footprint centre, Z up, with the front at −Y

Fixes, all in these scripts:
1. **Stair blossoms (wb_assets.py, wb_tex.py).** A crown was one 80-triangle icosphere with a hard-edged Voronoi "mosaic" texture, and the wall greenery was 20-triangle "crystals". At LOD0 each crown now gets a second smaller lump (from its own random seed, so the layout is unchanged), the side clumps and the wall greenery use 80-triangle spheres, and LOD1 crowns stay 80 triangles. The blossom and foliage bands use a new `g_blossom` generator: soft floret clumps, leafy shadowed gaps and colour drift, with no cell outlines.
2. **Choir chimney flues.** The flue was open at its foot, so from above you could see the ground through every chimney (visible as red holes in the back-face check). It now has a sooted floor 5 m down.
3. **Choir damaged dome.** Seen through its holes, the far side of the dome was back-face culled. An inward-facing inner skin now covers it (about 4k triangles, Damaged state only).
4. **Choir LOD3 hull.** The hull drum and arcade ring had no inner walls, so the inside of the far arcade showed as a band behind the dome. Both are now closed.
5. **Choir dome glass.** Random dark and dim panes read as a checkerboard by day. There are no dark panes now, and the brightness spread is 12 % (it was 35 %).
6. **Sunwell ruin.** The earth/stone split followed a single angular threshold, so the ruin read as clean pie wedges from 1 km. A per-ring jitter now leaves ragged stone remnants.
7. **Smoothing bug (wb_geo.py `to_object`).** The soft-canopy flags were collected into a set of BMFace wrappers before their layer was removed. Removing the layer invalidates those wrappers, so no edge was ever treated as soft and every canopy fell back to the 32° angle rule. This is why the crowns looked faceted. The layer is now read directly and removed afterwards. Soft canopies also get one continuous wrap-around UV (u around the blob, v by height) instead of a per-face box projection, whose per-face tangents faceted the normal map. Only the Stair uses soft blobs.
8. **Ear rim in the broken states.** The rim wall had no inner face because the dish closed it. Where panels are missing (Construction, the Damaged holes, Ruin), the far wall was seen from behind and culled, so the rim vanished. An inner wall now runs down to a concrete floor at +1 m (Construction, Damaged and Ruin only).
9. **Ladder ruin walls.** With the roof gardens fallen in, the single-sided shell walls were culled when seen from inside. The three walls now have back skins and lids on their broken tops (Ruin only).
10. **Previews (wb_preview.py).** The 1.8 m figure was missing from most 40 m views: it was buried 7 m inside the Ear's berm and hidden behind a chimney pedestal on the Choir and blossoms on the Stair. It is now placed by a ray cast on the visible surface, at spots inside the frame. Source objects are also hidden from the depsgraph so the ray cast only sees the placed meshes.
11. **Manifest text.** The Ear blink phase in the material params now matches the `lights` list (0, 0.667 and 1.334 s), and the tether note names Detail band 3 (`tether_core`), not band 1.
12. **Build.** No `.blend1` backup copies are written next to the sources.

Accepted as is, and documented:
- Revolve cuts in the damaged states leave coplanar down-facing slivers on the cut tops. They are culled from above and harmless.
- Ornament tube ends (sun-disc yokes, chimney-cap spikes) are open, and too small to matter.
- The Ear (16k) and the Ladder (20.6k) stay under the 30–80k range.
- The Sunwell and Stair `Structure` materials set `EmissiveNight` (3.0 and 2.0), but their M.R mask is empty, so the parameter has no effect. The rim lamps and pools are lit through other slots.
- The Ear's cables show a dotted sparkle in the night preview. It is specular, not emission: the steel band's M.R is 0, which the verifier checked per band.
- The Stair crowns are still 80-triangle spheres (two lumps each at LOD0). A 320-triangle sphere would push the Stair past 80k. They read well from 300 m but are still visibly low-poly at 40 m.
- The Stair LOD3 hull is a three-tier cake all round, while LOD0–2 show smooth rock at the back. This pops only at a screen size below 0.04.
- The Ladder Ruin, seen from behind, still shows some open faces around the courtyard walls on the far side (from about 1 km those slots are 2 to 4 px). The back skins and lids removed the vanishing outer wall.
- In the Choir states preview, a few cap-sized rings show on the ground between the four models. This is preview dressing only: the re-imported meshes stay inside their 180 m footprint and the manifest sockets sit on the chimney tops. It is not chased further.
