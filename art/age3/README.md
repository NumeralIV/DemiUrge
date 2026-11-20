# Age3: Sail Age kit

A script-generated kit for the Sail Age harbour city: four townhouses, two warehouses, quay segments, a
timber pier, a treadwheel port crane, a lighthouse and a domed civic temple. Each comes with damage states,
four LODs, three shared material sets and four culture material instances. Everything is original and
procedural (licence: `original`). Visual targets are `docs/concept/settlements/age-3-sail.png` and
`docs/concept/landmarks/building-port.png`.

## What is here

| Path | Contents |
|---|---|
| `build_age3.py` | Entry point. Builds the textures, materials, every mesh, LOD and state, the FBX exports, `manifest.json` and `source/age3.blend` |
| `age3_tex.py` | Texture synthesis (numpy, seeded). Uses `art/tools/dmtex.py` for noise |
| `age3_geo.py` | Mesh builder. Gives every face trim-sheet or atlas UVs |
| `age3_assets.py` | One function per asset: `(lod, state) -> (mesh, info)` |
| `preview_age3.py` | Preview renders (hero views, a harbour town for the 300 m and 1 km views, sheets) |
| `textures/` | `T_Age3_{Structure,Roof,Detail}_{D,N,ORM}` plus `_M` for Structure and Detail, all 1024² PNG |
| `export/` | 48 FBX files, one per mesh and state, each holding LOD0 to LOD3 |
| `manifest.json` | Per mesh: footprint, height, triangles per LOD, materials, textures, states, cultures, checks, and a mapping hint to `EDmTownMesh` |
| `source/age3.blend` | Generated source scene (never hand-edit it; rebuild it) |

### Assets (LOD0 triangles, Intact state)
| Mesh | Size (m) | Tris | States |
|---|---|---|---|
| `SM_Age3_Townhouse_A` (narrow gable front, balcony) | 6.6 x 11, 14.6 high | 2168 | Intact, Construction, Damaged, Ruin |
| `SM_Age3_Townhouse_B` (merchant house, arcade, hip roof) | 10 x 10, 17.6 | 3282 | same |
| `SM_Age3_Townhouse_C` (ashlar, eaves to street, dormers, quoins) | 9 x 8.5, 15.4 | 2422 | same |
| `SM_Age3_Townhouse_D` (L-shaped corner block, belvedere) | 13 x 13, 15.2 | 3564 | same |
| `SM_Age3_Warehouse_A` (long quayside, hoist dormer) | 24 x 12, 12.8 | 1967 | same |
| `SM_Age3_Warehouse_B` (tall gable front, apex hoist) | 11 x 16, 17.4 | 2486 | same |
| `SM_Age3_Quay_Straight` (20 m frontage) | 20 x 10 | 312 | Intact, Damaged, Ruin |
| `SM_Age3_Quay_Corner` (rounded outer corner) | 10 x 10 | 258 | same |
| `SM_Age3_Quay_Stairs` (water stairs along the face) | 20 x 12.2 | 500 | same |
| `SM_Age3_Pier` (timber, on piles) | 5 x 24 | 782 | same |
| `SM_Age3_PortCrane` (treadwheel, jib over the water) | 7 x 7 (+jib to y = -11), 15.1 | 1200 | Intact, Construction, Damaged, Ruin |
| `SM_Age3_Lighthouse` (octagonal, glazed lantern) | 10 x 10, 30.7 | 814 | same |
| `SM_Age3_CivicDome` (portico, aisles, drum, ribbed dome, front flight) | 30 x 38.5 (+3 m stair), 38.6 | 9310 | same |

LOD screen sizes are 1.0 / 0.35 / 0.12 / 0.04. LOD1 and LOD2 come from the same generators with less
detail: flat windows and no recesses, sills, shutters or small props. LOD3 is a hull with flat window quads,
so night lights still read.

## Conventions
- Metres, Z up, front (street or water side) toward -Y. The pivot is the footprint centre at ground level,
  and foundations reach 0.3 m below.
- Quays and the pier have their top surface at Z = 0, with water expected at `water_z` = -1.8.
- The pier pivot is its centre, not the shore end the stand-in `Pier` uses.
- The FBX files use the Blender exporter's defaults (`-Z` forward, `Y` up, unit scale applied),
  triangulated, with tangents and per-face smoothing. Each file holds an Empty with
  `fbx_type = LodGroup` whose children are `<name>_LOD0..3`, so import with *Import Mesh LODs*.
- Material slots are at most `Structure`, `Roof`, `Detail`, in that order. A mesh lists only the slots it
  uses (for example, ruins have no Roof).

## Materials and cultures
- **Structure** is a trim sheet of horizontal bands, each tileable in U and V: plaster, ashlar, rusticated
  quay stone, planks, beam, dressed stone and rubble. The mesh builder cuts long faces into strips one band
  high, so the texture keeps its real scale (about 100 px/m).
- **Roof** has four bands of equal size: terracotta (0), slate (1), moss-grey timber shingle (2) and lead (3).
  A material instance swaps the covering by shifting V by `0.25 * RoofBand`. This is valid only on meshes
  marked `roof_band_swappable`; the lighthouse and dome mix lead with tile, so they take tints only.
- **Detail** is a 4x4 atlas: windows, a window with shutters closed, an arched window, a shutter, a door,
  a wide door, an arcade, lantern glass, iron, rope, canvas, gilt, crate, charred wood, a boarded window and
  a broken window.
- In the `M` textures, R is the night emissive mask (window glass, lantern) and G is the tint mask (plaster
  on Structure, painted wood on Detail).
- Vertex colour `Col` multiplies the base colour. It carries ground grime, the wet quay foot below the water
  line and soot on the damaged states.
- Culture instances, with parameters in `manifest.json -> cultures`:
  - `Base`: cream limewash and terracotta.
  - `CultureA`, Cliff-Terrace: ochre-red plaster and deep red tiles.
  - `CultureB`, Lagoon-Weaver: silver limewash, shingle roofs and saffron paint.
  - `CultureC`, Ring-Builder: cool stone, slate roofs and blue paint.

  The preview materials in the .blend (`MI_Age3_*`) implement the same parameters.

## States
- **Construction**: walls cut flat at about 55 % of the eave height (stepped by sector), no roof, walls
  given real thickness (the solidify pass also fills the openings), timber scaffolding with board walks,
  and stacks of stone and timber.
- **Damaged**: a burnt hole in the roof with charred rafters, a charred roof lining and a burnt attic floor, soot, some windows
  broken or boarded, and rubble with fallen timbers. The lighthouse loses lantern glass. The crane loses its jib,
  which lies on the ground. The civic temple also loses part of its dome.
- **Ruin**: jagged, sloping wall tops on six sectors, thick walls, no roof, a charred rubble heap inside,
  and debris and soot outside. The civic temple keeps broken column stumps.
- In the construction and ruin states, LOD1 and LOD2 show the window and door openings as dark charred cards
  (thickened with the walls) instead of real holes.
- **Docks**: missing coping, rubble and slumped blocks in the water. Pier decks fall away down to stumps.

## Regenerate
```
"C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python art/age3/build_age3.py -- [--textures] [--only Townhouse_A,Pier] [--no-export]
python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b art/age3/source/age3.blend --python art/age3/preview_age3.py -- [--assets A,B] [--views 40m,300m,1km] [--sheets states,cultures,town,night] [--samples 32]
```
A full build takes under a minute. A full preview run takes a few minutes inside one render slot.
Previews are written to `shots/asset-age3-<asset>-{40m,300m,1km,states}.png`, plus
`asset-age3-cultures.png`, `asset-age3-town-500m.png` and `asset-age3-town-night.png`. The 300 m and 1 km
views put each asset in a small harbour town built from the kit, and every instance shows the LOD that
Unreal would choose at that distance. The "40 m" view backs off only for assets too large to fit in frame:
the civic temple and the lighthouse. A red 1.8 m figure gives scale.

## Known gaps
- Warehouse A (1967) sits just under the 2k house floor; the port crane (1.2k), lighthouse (0.8k) and the dock
  pieces have no exact budget class (they are closest to vehicle / prop). Everything else is inside its budget.
- LOD1 ratios (LOD1 / LOD0) are 0.26-0.44 on the intact buildings (the verifier's LOD0 additions are LOD0 only, so
  LOD1 now pops from framed, recessed windows to flat ones), 0.36-0.92 on the construction, damaged and ruin
  states, and 0.6-0.86 on the small dock pieces, the crane and the lighthouse, which have little to remove. LOD2 is
  about 8-11 % on intact buildings. LODs are hand-built, not decimated.
- Ruin LODs are cut by the same seeded planes but on different geometry, so LOD2/LOD3 ruin silhouettes can be up to
  ~1 m taller than LOD0.
- The meshes are open shells. `checks` in the manifest counts boundary edges. Walls are single-sided except in the
  construction and ruin states. The damaged roof hole shows a charred inner roof lining and a burnt attic floor.
- There are no Lagoon-Weaver mesh variants (upswept saddle roofs, buildings on piles): culture B is
  material-only (shingle and tints). Star-shaped bastion walls are not in this family.
- There is no baked lightmap UV channel; let Unreal generate one, or use Lumen.
- The normal maps are OpenGL convention (+Y). Flip green on import.
- Preview night lighting turns every window on. The game sets the lit fraction per instance.

## Verification (independent review, 2026-11-19)
Rebuilt from the scripts (textures byte-identical, manifest identical before the fixes), re-imported all 48 FBX
files, rendered back-face debug views and the full preview set through `tools/render_slot.py`. Fixes made:
1. `age3_geo.MB.beam` built every beam inside out (left-handed frame): scaffolds, crane bracing and jib, rafters,
   raking cornices, hoists, timbers. Winding and caps reversed.
2. Civic temple: both lean-to aisle roofs faced down, so with back-face culling the aisles had no roof. All faces
   of the aisle roofs are now oriented explicitly, with a soffit added.
3. Civic temple drum: the window cards sat on polygon corners and were half buried in the drum. `drum_window`
   bends each card across the facets it spans.
4. Loose vertices and edges left by the bisect passes (71 of 192 meshes) inflated bounds and the manifest heights
   (Lighthouse_Construction read 23 m instead of 13.6 m, Townhouse_A_Ruin 13 m instead of 7 m). They are removed.
5. Pivots: the civic temple was 1.25 m off its footprint centre in Y; buildings are now re-centred on their design
   footprint (docks keep their tiling frame). The manifest adds `footprint_design_m` (walls / quay block / deck,
   the same in every state); `footprint_m` remains the full bounds including scaffolds and debris.
6. Construction and ruin LOD1/LOD2 were 89-96 % / 45-74 % of LOD0. Above LOD0 their openings are now dark
   charred cards (thickened into boxes, so they read from both sides) instead of real holes.
7. Under-budget LOD0s: dressed-stone window architraves on every house and warehouse (LOD0 only), quoins and a
   first-floor band on Townhouse C, and on the civic temple fluted columns, dentil courses, a pediment sculpture
   group, a smoother drum and dome (36 sides, 14 rings) and a walkable front flight of 0.167 m risers between
   cheeks (the 0.5 m podium steps alone were not human scale).
8. Damaged roofs: a charred inward-facing lining around the hole (single-sided roofs were culled from inside).
   The damaged temple's gable-roof hole hid under the drum, so from 300 m it read as intact; the dome is now
   broken on its front-left shoulder, lined with charred lead and closed at the spring line.
9. Small culling leaks: closed the column base tori, flush lighthouse platform moulding, string courses tucked
   into the battered tower, window jambs capped.
10. Previews: the 1.8 m figure now stands in front of the facade (it was cut off by the frame edge).

Triangle totals over all 48 meshes after the fixes: LOD0 97,705, LOD1 49,015, LOD2 20,642, LOD3 5,918
(`manifest.json` is the reference; the numbers in `docs/handoff/done-ue/asset-age3.built.txt` predate the fixes).
