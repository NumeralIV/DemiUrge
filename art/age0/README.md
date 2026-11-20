# Age0: Stone Age kit (family `Age0`)

Script-generated, original assets for the stone-age camp in `docs/concept/settlements/age-0-stone.png`
(units scale cross-checked against `docs/concept/units/units-ancient.png`). Built to `docs/ASSETS.md`.
Licence: original (procedural geometry and textures, no external inputs).

## What is here

| Asset | Mesh(es) | States | LOD0 tris (Intact) | Notes |
|---|---|---|---|---|
| Hut A | `SM_Age0_Hut_A_<State>` | 5 | 2447 | thatch beehive dome, 5 m across, 3.9 m to the topknot: the concept's main hut |
| Hut B | `SM_Age0_Hut_B_<State>` | 5 | 2791 | conical thatch on a wattle-and-daub drum, pole ends at the apex |
| Hut C | `SM_Age0_Hut_C_<State>` | 5 | 2792 | hide-covered bender dome, thatch cap, smoke hole, hem stones |
| Lean-to A | `SM_Age0_LeanTo_A_<State>` | 5 | 987 | forked uprights, ridge pole, thatch and hides, hearth in front |
| Longhouse A | `SM_Age0_Longhouse_A_<State>` | 5 | 5757 | 16 m bow-walled longhouse, hipped thatch, ridge roll, porch |
| Longhouse B | `SM_Age0_Longhouse_B_<State>` | 5 | 6579 | 22 m clan hall (civic budget) |
| Fire pit | `SM_Age0_FirePit_A` | - | 900 | stone ring, log tipi, embers (always-on emissive), seat logs, tripod |
| Drying racks | `SM_Age0_DryingRack_A`, `_B` | - | 298 / 361 | A-frame with stretched hides (one painted); long tripod rack |
| Palisade segment | `SM_Age0_Palisade_Seg_<State>` | 5 | 1706 | 8 m module along X, outer face -Y, earth bank; tiles end to end |
| Palisade gate | `SM_Age0_Palisade_Gate_<State>` | 5 | 1278 | same 8 m module: post-and-lintel gate, two leaves, culture banner |
| Storage pits | `SM_Age0_StoragePit_A`, `_B` | - | 753 / 364 | thatch-capped clay pit; open pit with split-log lid and quern |
| Standing stones | `SM_Age0_StandingStones_A_<Intact/Ruin>` | 2 | 2075 | ring of 12 menhirs, 16 m, altar stone (the age-0 shrine) |
| Standing stone | `SM_Age0_StandingStone_A_<Intact/Ruin>` | 2 | 235 | single menhir |
| Ornaments | `SM_Age0_Ornament_Pennant`, `_Totem` | - | 114 / 360 | culture pennant pole and painted totem post (culture colour via M.G) |

States are `Intact, Construction, Damaged, Ruin, Burnt` on the same footprint. `Burnt` meshes are drawn with the
`MI_Age0_*_Burnt` instances (the manifest says so per mesh). 51 FBX files in all; per-mesh triangles for every LOD,
footprints, heights, slots used and the mapping onto the towns kit (`EDmTownMesh` + variant) are in `manifest.json`.

Kit mapping suggested in the manifest: Hut 0/1/2 -> Hut A/B/C, Tent 1 -> Hut C, Tent 0/2 -> LeanTo A, Longhouse 0/1 ->
Longhouse A/B, Campfire -> FirePit A, Stones -> StandingStones A, WallSeg 0 -> Palisade Seg, Gatehouse 0 -> Palisade Gate,
Granary 0 -> StoragePit A. Racks, the second pit, the single menhir and the ornaments are props with no kit id yet.

## Materials and textures

Three slots on every mesh, always in this order: `Structure`, `Roof`, `Detail` (`M_Age0_Structure`, `M_Age0_Roof`,
`M_Age0_Detail`). Each has one 1024² trim sheet (`textures/T_Age0_<Slot>_{D,N,ORM,M}.png`): bands tile in U and the
geometry maps V inside a band, so UVs run outside 0-1 in U on purpose (nothing is baked).

- Structure: bark poles, weathered wood, wattle and daub, packed earth.
- Roof: thatch course (cut straw ends at the band bottom, so every course lip shows them) and sewn hide panels.
- Detail: lichened stone (V = height above ground), leather, painted hide, embers / char / dark interior patches, cord.
- `N` is OpenGL (+Y): flip green on import. `ORM` = AO, roughness, metallic (0).
- `M`: **R night emissive** (doorways, smoke hole, vents), **G culture-colour paint** (pennants, banner, painted hide,
  totem bands), **B moss / grime**, **A always-on fire** (embers).
- Climate instances `MI_Age0_<Slot>_{Temperate,Arid,Tropical,Cold}` (tint + moss), matching the planner's `EStyle`, and
  `MI_Age0_<Slot>_Burnt`. The culture colour is a per-instance parameter (`CultureColor`, the six `DM.CULTURE_COLORS`).

## LODs

LOD0-LOD3 under one FBX `LodGroup` empty (`fbx_type = LodGroup`) per file, named `<Mesh>_LOD0..3`; screen sizes
1.0 / 0.35 / 0.12 / 0.04. LODs are regenerated from the same generator with fewer segments and courses, not decimated.
LOD3 is a simple hull for everything (huts ~40-120 tris, longhouses ~60-120, palisade 30). In the camp renders the LOD
picked by screen size was: 40 m view mostly LOD1, 300 m mostly LOD2, 1 km almost all LOD3.

## Regenerate

From the project root:

```
"C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python art/age0/build_age0.py
    (-- --no-tex skips the 20 s texture synthesis; -- --only Hut_A,FirePit_A builds a subset into age0_partial.blend)
python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b art/age0/source/age0.blend
    --python art/age0/render_age0.py -- <solo all | states all | camp all | variants | lineup | close Hut_A>
```

Files: `build_age0.py` (registry, materials, LOD groups, FBX, manifest), `a0_assets.py` (generators), `a0_geo.py`
(mesh builder, trim bands, primitives), `a0_tex.py` (trim sheets, uses `art/tools/dmtex.py`), `render_age0.py` (preview
rig: 20° warm sun from the left, cool sky fill, EEVEE 1280x720, LOD chosen by screen size; trees and ground in the
previews are context only). Output: `export/`, `textures/`, `source/age0.blend`, `manifest.json`.

Previews: `shots/asset-age0-<asset>-40m.png` (each asset, 40 m three-quarter, 1.8 m figure),
`-states-40m.png` (all states side by side), `-close.png` (14 m inspection), `asset-age0-camp-{40m,300m,1km}.png`
(the concept camp of 16 huts round a hearth, plus a palisaded longhouse hamlet so every asset is in the far views),
`asset-age0-variants-40m.png` (four climates, six culture colours), `asset-age0-lineup-40m.png`.

## Independent verification (2026-11-19)

A second engineer rebuilt the family from the scripts, re-imported all 51 FBX files, re-rendered every preview through
`tools/render_slot.py` and compared them with the age-0 concept. The rebuild is reproducible: the textures come out
pixel-identical, the manifest is identical apart from its date, and the FBX files differ only in their embedded
timestamps. The checks were run on every LOD of every file:
- the FBX `LodGroup` attribute is present;
- one empty and four meshes, named `_LOD0..3`;
- three slots in the order Structure, Roof, Detail;
- pivot at the footprint centre, 0.3-0.6 m below ground;
- no n-gons, no degenerate faces, no loose vertices and no inverted closed parts;
- triangle counts match the manifest.

Fixes made, all in `art/age0/`:
1. **UV magnitude** (`a0_geo.py`). Per-piece random U offsets (`seed * 0.37` m on poles, `seed * 1.7` m on rocks,
   `seed * 0.3` m on menhirs) pushed U up to 343. Unreal's default 16-bit UVs cannot hold that: at U = 256 the
   half-float step is 0.25, i.e. 1 m of trim.
   - The offsets are now taken modulo one tile.
   - `MB.wrapped_uvs()` shifts every connected piece by whole tiles so it sits round U = 0.
   - The largest |U| is now 7.4 (the longhouse eave rings). Neither change is visible, because the bands tile in U.
2. **Mossy hearth stones** (`a0_geo.rock`, `a0_assets.firepit`). Small rocks sampled only the bottom 0.3 m of the stone
   band, where the moss mask (M.B) is 0.6-0.9. Hearth stones and hem stones therefore rendered mint-green.
   - Rocks now start 0.25 m up the band (`vlift`).
   - The fire-pit ring starts 0.7 m up the band, so it is grey stone with lichen.
3. **Stray debris** (`a0_assets.debris_poles`). Longhouse Ruin and Burnt logs were scattered on a circle of radius
   0.4 L, which put one log 9.4 m from the axis of the 8.7 m-wide Longhouse B. The scatter is now an ellipse that stays
   within the building's plot: Ruin and Burnt footprints are 7.2-8.6 m deep, where they were 10-16 m.
4. **Lean-to LOD3** (`a0_assets.leanto`). The cover slab was single-sided, so from the open front (and in most
   top-down views) the LOD3 lean-to showed only two poles. A back face was added (+2 tris).
5. **Open storage pit, inner wall** (`a0_assets.storagepit`). The inner wall's profile already runs top to bottom, so
   the lathe faced the pit axis. An extra flip turned it toward the earth: normal·radial was +1.0 on every LOD, and
   back-face culling hid it. On LOD2 it also left 8 edges with inconsistent winding where it welds onto the rim. The flip
   is removed (normal·radial is now -1.0).
6. **Loose vertices** (`build_age0.weld`). The generators' damage and ruin `keep()` filters left 6,588 vertices without
   faces across the damaged, ruined and construction meshes. They are now deleted before export; no triangle counts
   change.
7. **Manifest**. Each mesh now lists its `textures` and `pivot`. The palisade entries note that the kit `WallSeg` is a
   16 m stretch.
8. **Camp preview** (`render_age0.py`). The hut yaw was overwritten so that every door faced away from the hearth; doors
   now face the fire, as in the concept.

Verifier-only previews: `shots/asset-age0-verify-lods-{huts,large,props}.png` (LOD0-LOD3 side by side) and
`shots/asset-age0-verify-back-{small,large}.png` (LOD0 from behind; the preview materials cull back faces, so a flipped
face would show as a hole).

## Known gaps

- No smoke column or flames: the hearth has emissive embers only; smoke and flame belong to the FX module.
- Domes are open underneath (hidden by the footing); they are not closed manifolds. Closed parts (stones, logs) are.
- LOD1 for the huts is ~50 % of LOD0 (target 40 %) and LOD2 for the longhouses ~8 % (target 12 %); within reason.
- Damage holes in hut roofs read as patches at 40 m rather than clear holes; ruins read as low collapsed mounds.
- Lean-to rafters sit just above the thatch near the ridge (reads as poles holding the cover down).
- The `LodGroup` FBX attribute and the green-flip of the normal maps are untested in Unreal (no Unreal per asset).
- Climate tints are deliberately subtle; culture identity is carried by the painted elements (M.G).
- No `_Drowned` state; no tipi (kit Tent 0 maps to the lean-to) and no rectangular age-1 hut (kit Hut 3).
- Gaps the verifier found:
  - The gate banner and the painted rack hide drop out at LOD2, so the culture colour is gone at about 300 m.
  - The thatch courses are very even, stepped tiers with dark lips. At 14-40 m the domes read a little like layered
    cakes rather than loose, ribbed thatch; the concept's huts are rougher and more conical.
  - The open storage pit (B) is a shaft down to -0.6 m. Any terrain without a hole hides it, so in game it reads as a
    rim with a log lid.
  - The gate LOD3 hull is a dark box whose bark mapping looks like masonry. It only shows at 1 km.
