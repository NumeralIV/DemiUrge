# Age2 — Iron Age kit

Script-generated, original (procedural geometry and textures, no external inputs) Iron Age building
kit for DEMIURGE, built to `docs/ASSETS.md`. Visual targets: `docs/concept/settlements/age-2-iron.png`
(the walled town), `docs/concept/landmarks/building-walls.png`, `building-market.png` and
`building-academy.png`.

## What is here

| Asset | Mesh names (`SM_Age2_<Asset>_<State>`) | What it is | Footprint / height |
|---|---|---|---|
| House A | `House_A_*` | Small rubble-stone cottage, steep reed thatch, chimney, shutters, woodpile | 7.3 x 5.5 m walls (9.3 x 6.6 with dressing), 7.1 m |
| House B | `House_B_*` | Stone longhouse, dark split-shingle roof with rafter tails, door porch, central chimney, back outshot, walled yard | 10.8 x 6.3 m walls (11.8 x 11.9 with yard), 7 m |
| House C | `House_C_*` | Two storeys: rubble ground floor, jettied timber-frame and lime-plaster upper floor, shingle gable to the street | 7.9 x 6.9 m walls, 11.5 m |
| House D | `House_D_*` | Farmstead: thatched hip-roofed hall, thatched back wing, walled yard, woodshed | 12.9 x 12.4 m, 7.3 m |
| Wall segment | `WallSeg_*` | 16 m curtain-wall unit, 3.2 m thick, walkway 7.6 m, crenellated outer parapet, battered base. Tiles along X; outer face -Y | 16 x 4.6 m, 9.6 m |
| Wall tower A | `WallTower_A_*` | 9 x 9 m hollow square tower, crenellated top, arrow slits, doors onto the wall walk | 15 m |
| Wall tower B | `WallTower_B_*` | Same tower with timber hoarding on corbels and a pyramidal clay-tile roof | 24.5 m |
| Gatehouse | `Gatehouse_*` | Twin hoarded towers, round-arched passage (road along Y), half-raised portcullis, open gate leaves, tiled timber gallery | 26 x 11 m (28.5 x 14.6 with hoarding and road), 25.5 m |
| Market | `Market_*` | Colonnaded market square: clay-tiled galleries on braced timber posts, paved court, 12 striped-awning stalls, round fountain, carts at the gates | 34.4 x 26.4 m (carts reach 35 m), 4.9 m |
| Temple | `Temple_*` | Peripteral stone temple, 6 x 11 columns on a three-step stylobate, triglyph frieze and dentils, pale stone-slab roof with pediments, altar in front | 23.2 x 38 m (43 with the altar), 14.6 m |
| Academy | `Academy_*` | Cloistered limestone quadrangle, slate roofs, garden court with an armillary sphere, columned porch, verdigris-domed observatory tower | 44.6 x 36.6 m (42.6 with the porch), 22.3 m |
| Market stalls | `MarketStall_A/B/C_Intact` | Props: gable-awning stall (madder), lean-to stall (ochre), cart stall (indigo) | ~3 m |
| Ornaments | `Ornament_A/B/C_Intact` | Culture ridge ornaments (see Cultures) | <1.5 m |

Exact footprints, heights, bounds and triangle counts per LOD are in `manifest.json`.

**States.** Every building has `_Intact`, `_Construction` (walls rising in courses, scaffolding, stone
and timber stacks, no roof), `_Damaged` (burnt holes through the roof with charred rafters, scorched
vertex colour, broken wall tops, rubble) and `_Ruin` (jagged low walls, no roof, fallen charred timbers,
rubble heaps; chimney stacks survive) on the same footprint. Props and ornaments are intact only.

**LODs.** One FBX per mesh holds an `LodGroup` node (`fbx_type` custom property) with children
`_LOD0`.. `_LOD3`. LOD1 and LOD2 are rebuilt from the same script with fewer parts and coarser
roofs (collapse-decimated only if they came out above target, and the decimated mesh is kept only if
it keeps at least 92 % of the surface area and its bounding box within 3 %; otherwise a milder ratio is
tried, then the hand-made LOD is kept; then cleaned of degenerate and duplicate triangles); LOD3 is a massing hull on every building (props and ornaments stop at LOD2). Screen sizes
1.0 / 0.35 / 0.12 / 0.04.

**Triangles (Intact LOD0 / LOD1 / LOD2 / LOD3):** House A 2018 / 804 / 98 / 42, House B 2290 / 1158 /
176 / 42, House C 2410 / 964 / 154 / 76, House D 4954 / 1514 / 220 / 80, WallSeg 274 / 170 / 65 / 6,
WallTower A 594 / 266 / 144 / 24, WallTower B 1728 / 690 / 228 / 52, Gatehouse 4230 / 1692 / 538 / 136,
Market 12192 / 4260 / 936 / 78, Temple 9118 / 2394 / 500 / 74, Academy 12716 / 5086 / 1528 / 138,
stalls 124–184, ornaments 82–136. All 50 meshes (all states) together: LOD0 151 726, LOD1 60 817,
LOD2 16 665, LOD3 2 401 (`manifest.json` is the reference).

**Materials** (exactly 3 slots on every mesh, state and LOD, in the order Structure, Roof, Detail, so
slot indices are stable across states; a slot may carry no faces, e.g. Roof on ruins, see each mesh's
`materials` and `slots_with_faces` in the manifest):

| Material | Used for | UV tile |
|---|---|---|
| `M_Age2_Rubble` | house walls | 3 m |
| `M_Age2_Ashlar` | town walls, towers, gatehouse | 4 m |
| `M_Age2_Limestone` | market, temple, academy | 4 m |
| `M_Age2_Thatch` | houses A, D | 3 m |
| `M_Age2_Shingle` | houses B, C | 2 m |
| `M_Age2_TileClay` | market, tower B, gatehouse roofs | 2 m |
| `M_Age2_TileStone` | temple roof | 2 m |
| `M_Age2_Slate` | academy roofs; culture C roof override | 2 m |
| `M_Age2_Trim` | Detail slot on everything: trim sheet of horizontal bands (timber, planks, 16 window cells, three striped cloths, iron, verdigris bronze, plaster, dressed stone, hedge, water, goods, earth, charred wood). Tiles in U only (2 m per U). | — |

Textures are 1024² PNG in `textures/`: `T_Age2_<Set>_D` (sRGB), `_N` (OpenGL +Y, flip green on UE
import or tick "Flip Green Channel"), `_ORM` (linear AO, roughness, metallic) and `T_Age2_Trim_M`
(R = night-window emissive mask; the window cells of the trim sheet). All tileable sets wrap; UVs are
world-scaled and run outside 0–1 by design (nothing is baked).

**Vertex colour** `Col` (RGB) is a baked multiplier for the base colour: darker foundations, eave
shadow on house walls, darker roof undersides, and scorch around damage. The UE master material
should multiply base colour by it.

**Cultures.** Material instances in the manifest (`material_instances`): `MI_Age2_<Set>_<Culture>` with
a linear tint, a saturation factor, a roughness scale and an optional texture override:
`Valley` (default, the age-2 concept), `A` Cliff-Terrace (warm rose-ochre stone), `B` Lagoon-Weaver
(silver-grey weathered wood and stone), `C` Ring-Builder (pale stone, blue-grey slate roofs via a
texture override on thatch, shingle and clay tile). Mesh variants: `Ornament_A` (plaster dome finial
with a cobalt knob), `Ornament_B` (upswept timber horn with a saffron ribbon), `Ornament_C` (stone
ring finial); attach one at each ridge end listed in a house's `ridge_ends` (Intact meshes; pivot =
the ridge point, local -Y pointing outward along the ridge).

**Mapping to the towns module** (`maps_to` in the manifest): houses replace `EDmTownMesh::Cottage` in
Age 2; `WallSeg` / `WallTower` / `Gatehouse` are the Iron variants (1) of the same ids and keep their
conventions (16 m segment along X with the outer face -Y, scaled in X by the planner; road through
the gate along Y); `Market` -> `Market` (Ages 2–3); `Temple` -> `TempleClassic` variant 0; `Academy`
-> `Academy` (Age 2 form). Night windows: `M_Age2_Trim` emissive = mask R x the per-instance lit
fraction.

## Previews

`shots/asset-age2-<asset>-40m.png` (three-quarter, alone, 1.8 m figure), `-300m.png` and
`-1km.png` (high oblique over a 230 m walled town assembled from the kit), `-states.png` (Intact,
Construction, Damaged, Ruin), `-lods.png` (LOD0..LOD3), plus `asset-age2-cultures.png` (the four houses
in the four cultures with their ornaments, rows front to back Valley, A, B, C) and
`asset-age2-night.png` (window mask lit). Golden-hour sun 20° from the left, cool sky fill, EEVEE
1280 x 720, single-sided materials (back-face culling on, so flipped faces would show as holes).

## How to regenerate

From the project root (renders must go through the slot tool):

```
python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python art/age2/build_age2.py -- --textures --previews
"C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python art/age2/check_age2.py
```

Options after `--`: `--textures` (rebuild the texture sets), `--assets House_A,Temple` (subset),
`--states Intact,Ruin`, `--no-export`, `--no-blend`, `--previews [tags]` (tags: an asset slug such as
`house-a`, a view such as `40m`, `300m`, `1km`, `states`, `lods`, `cultures`, `night`, or
`house-a-40m`). A full build without previews takes about 40 s; previews about 2 minutes once a
render slot is free.

Files: `build_age2.py` (driver: materials, LODs, FBX, manifest, blend), `age2_kit.py` (geometry
builder: walls with openings and state profiles, roofs as thick slabs with holes, LOD and state
logic, mesh output), `age2_assets.py` (one function per asset), `age2_tex.py` (texture synthesis on
`art/tools/dmtex.py`), `age2_preview.py` (render rig and the town layout), `check_age2.py` (export
checks). Outputs: `export/*.fbx`, `textures/*.png`, `source/age2.blend`, `manifest.json`.

## Known gaps

- Wall pieces have no budget line in ASSETS.md and are deliberately light (they are instanced by the
  dozen); everything else is inside its ASSETS.md budget (`check_age2.py` reports any drift).
- LOD ratios are not exactly 40 % / 12 %: small parts (log ends, dentils, triglyphs, cover strips)
  are LOD0 only, so the temple and House D drop to 26–31 % at LOD1 and the houses and temple to 4–8 %
  at LOD2; where decimation would have broken the shape the hand-made LOD is kept, so the wall pieces
  run higher (WallSeg 62 % / 24 %, WallTower A 45 % / 24 %; both are a few hundred triangles).
- The decimated LOD2 of `Ornament_C` (16 triangles, a ring the size of a hand at that distance) has
  some flipped triangles; invisible at its screen size.
- Footprints in the manifest are mesh bounds, so they include dressing outside the walls (woodpiles,
  yards, carts, the temple altar, the gatehouse road strip); the building lines are given above.
- `WallSeg` ends are closed in every state (the towns planner puts a tower only on every sixth wall
  corner, so most joints are bare segment-to-segment corners).
- The town-wall planner scales `WallSeg` in X, which stretches its texture by the same factor
  (about 0.8–1.3x in practice).
- Collision primitives are not authored (use simple box collision or complex-as-simple).
- Culture tints are approximations of the concept palettes; culture C's slate override is a texture
  swap and needs a material instance that exposes the texture parameters.
- The ground, fields and town layout in the 300 m / 1 km previews are preview context only, not
  assets.

## Independent verification (2026-11-19)

Verified by an asset verifier who did not build the family. Everything was rebuilt from the scripts
(textures byte-identical to the builder's, manifest identical apart from the date), re-imported and
audited with my own scripts (hierarchy, transforms, slots, UVs, ray-cast visible-back-face test from
outside at every LOD, decimation area test), and all 81 previews were re-rendered through the slot tool.
Fixes, all in the scripts:

1. **Decimation wrecked the wall LODs.** `WallSeg` LOD2 had lost the whole wall body (a 1 m sliver of
   parapet floating at 4.7–8.7 m; 12 % of the surface left), LOD1 kept 43 % of its surface, and
   `WallTower_A` LOD1/LOD2 were torn (16 % of rays saw back faces). `build_age2.py` now keeps a
   decimated LOD only if it preserves the surface area (92 %) and the bounding box (3 %), else tries a
   milder ratio, else keeps the hand-made LOD; arrow slits are dropped at LOD1 (`age2_kit.py`).
2. **Material slot indices shifted between states.** Unused slots were dropped, so Detail was index 2
   on intact buildings, 1 on ruins and walls and 0 on props, and culture instances applied by index
   would have painted trim with roof material. Every mesh and LOD now has the three slots Structure,
   Roof, Detail at 0, 1, 2; `slots_with_faces` in the manifest says which carry geometry.
3. **Crenel floors were holes.** The lower parapet boxes of `WallSeg` had no top at LOD0/1, so each
   crenel looked into the hollow parapet. Tops restored.
4. **Open wall-segment ends.** The towns planner puts towers on every sixth corner only, so most
   segment joints are bare; the README's "always capped by towers" did not hold. Ends are now closed in
   every state, and the battered base has end triangles.
5. **Floating arches on ruins.** Arch spandrels were drawn to full height even where the wall had
   collapsed (gatehouse ruin: an arch hanging over the rubble; academy cloister arcades in the
   construction and ruin states). Spandrels are clamped to the standing wall and capped.
6. **Trim-sheet UVs.** Cylinder caps on the trim sheet (barrels, baskets, the altar, the fountain)
   used world-planar UVs that wandered over other bands, including the night-window cells: a barrel by
   House B's door glowed at night. Caps now stay in their band. Tops and reveals of band-mapped walls
   (timber hoarding, jetty plaster) had zero-area UVs (536 faces on the gatehouse); now mapped across
   the wall thickness.
7. **Dropped faces.** The undersides of the lean-to and cart canopies and the back of the ornament
   ribbon coincided with their front faces and were discarded by the mesh builder, so they vanished
   from below/behind. They are offset now. The cart's +Y wheel showed its open side; both caps face out.
8. **Twisted tubes.** `tube()` rebuilt its frame per ring, which flipped on the curved ornament horn
   and stone ring; frames are now parallel-transported, and the horn has end caps. The cobalt knob of
   `Ornament_A` is kept down to LOD2.
9. `check_age2.py` now also checks the fixed slot order on every LOD, faces on Detail (and Roof for
   houses and civic buildings) on intact meshes, and that intact LOD1/LOD2 keep 80 % of the LOD0 top
   and height. The height test catches defect 1 on the builder's exports; the slot test caught a
   regression in my own first attempt at fix 2 (clearing the slots had reset every face to slot 0).
10. The manifest's material list was written in set order, which changed from run to run; it is
    sorted now, so two builds give byte-identical manifests.
