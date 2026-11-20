# art/units-land: land units (family `Unit`)

Every land unit on the map, plus the air traffic, the space launch and the ark, built by one Blender
script to `docs/ASSETS.md`. Visual targets: `docs/concept/units/` (the silhouette sheet
`units-silhouettes.png` and its `index.md` rules), with `units-ancient/classical/modern/space.png` for
the shapes.

## What is here

| File | What |
|---|---|
| `build_units-land.py` | The generator. It makes every mesh, LOD, the trim sheet, materials, FBX exports, manifest and previews. |
| `export/SM_Unit_*.fbx` | One FBX per mesh, holding an `LodGroup` empty with `_LOD0` to `_LOD3` children. |
| `textures/T_Unit_Trim_{D,N,ORM,M}.png` | The shared 1024² trim sheet: 8 × 8 swatches of 128 px, all procedural (`art/tools/dmtex.py`). |
| `manifest.json` | One entry per mesh: footprint, height, bounding box, triangles per LOD, materials, textures, states, culture variants, sim mapping, licence, script, sheet scale. |
| `units-land.blend` | The source scene (rebuilt by the script). |
| `shots/asset-units-land-<asset>-{40m,300m,1km}.png` | Previews. The 1 km view shows all six culture colours side by side. |

### Meshes (21)

| Mesh | Unit, ages | Content | LOD0 tris |
|---|---|---|---|
| `SM_Unit_Settlers_Ancient` | Settler, Stone–Bronze | pennant bearer, 12 walkers with loads, 3 goats, two oxen, a disc-wheeled cart | 3341 |
| `SM_Unit_Settlers_Classical` | Settler, Iron–Sail | covered wagon (culture band on the canvas), two oxen, driver, 6 walkers | 1804 |
| `SM_Unit_Settlers_Modern` | Settler, Industry–Atomic | bus convoy: 3 buses with culture band and roof; the lead has a roof rack and pennant | 1076 |
| `SM_Unit_Settlers_Space` | Settler, Space | white aerial transport with blue pods and culture flank stripe and fin, hovering 4 m up | 540 |
| `SM_Unit_Traders_Ancient` | Trader, Stone–Bronze | 4 pack donkeys with culture-coloured panniers, 2 drovers | 1790 |
| `SM_Unit_Traders_Classical` | Trader, Iron–Sail | camel caravan: 4 camels with culture packs, 2 robed handlers | 2014 |
| `SM_Unit_Traders_Modern` | Trader, Industry–Atomic | freight train: steam loco and tender, red, ochre and container wagons (one culture band, one culture container), on its own track strip | 2376 |
| `SM_Unit_Traders_Space` | Trader, Space | white 3-car maglev with culture stripe and glass band on a guideway beam with a blue glow | 472 |
| `SM_Unit_Army_Ancient` | Army, Stone–Bronze | warband: 30 spearmen in a ragged dark block, hide shields, one tall culture pennant | 3910 |
| `SM_Unit_Army_Classical` | Army, Iron–Sail | formation: 50 helmeted spearmen in a parallelogram, 3 tall culture banners, 2 riders | 3734 |
| `SM_Unit_Army_Modern` | Army, Industry–Atomic | truck column: 4 olive trucks, the lead with culture canvas and a pennant | 1720 |
| `SM_Unit_Army_Space` | Army, Space | dark armoured hover transport with a culture fin and blue drive glow, 2 escorts | 406 |
| `SM_Unit_Prophet_A` | Prophet, all ages | white-robed prophet with a gold halo and staff, 7 followers | 1250 |
| `SM_Unit_Aircraft_Prop` | air traffic, Atomic | twin-prop high-wing cargo plane, culture fin | 416 |
| `SM_Unit_Aircraft_Jet` | air traffic, Atomic–Space | swept-wing jet, culture cheat line and fin | 396 |
| `SM_Unit_Rocket_A_Pad` / `_Launch` | Space launch | rocket on its mount with an umbilical tower / rising on a white-gold plume over a smoke ring | 1634 / 2450 |
| `SM_Unit_Ark_A_Construction` / `_Intact` / `_Damaged` | the ark (landmark) | 100 m white-and-gold hull with culture stripes in a lattice gantry with cranes; half-built in scaffold / complete / nose torn off with a smoke plume, scorched breach (dark inner wall behind it), broken gantry and crane, debris | 4924 / 3826 / 3902 |
| `SM_Unit_GroundRing_A` | flying units | the culture ring on its own (radius 8 m), placed on the ground under an aircraft | 64 |

The total is 42.0k triangles at LOD0, 17.7k at LOD1, 7.9k at LOD2 and 2.3k at LOD3. LOD1 is about 40 %
of LOD0 (34–70 %; the small craft under 600 triangles keep more) and LOD2 7–42 %. LOD3 is a block hull plus the ring. Each mesh is within the vehicle
budget (1–4k; a unit is a group). The ark is a landmark, far under the wonder budget.

## How the rules of the concept are met

- **A unit is a group on one base.** Each unit is a tight cluster of figures, animals or vehicles. Every
  ground unit carries the **culture ground ring**, a stadium-shaped band 0.55 m wide that follows the
  LOD0 footprint with a 1.2 m margin. The script adds it and recentres the pivot on the footprint, so a
  later change to a unit cannot misplace it. Flying units use `SM_Unit_GroundRing_A` on the ground under
  them, so the ring and the offset sun shadow together show the altitude.
- **Dark body, warm rim.** Figures and animals use near-black browns (`#17130e` family, with 4 dark cloth
  swatches for variety). Space craft are light on dark (white `#eceae4`, gold, blue glow `#8fd0ff`).
  The space army stays dark.
- **Culture colour** goes on the pennant or banners, the loads (panniers, camel packs, the lead truck's
  canvas, containers), stripes, and the ring. All of it is in the `Detail` slot and tinted by mask (below).
- **Shape per type:** settlers are a column with a cart, wagon, bus or transport plus a pennant. Traders
  are a line of identical bright loads. The army is a dense block, and only the army has vertical spikes
  (spears, banners). The prophet is one white figure with a gold halo leading a short line. Each one
  was checked at 300 m and 1 km against the silhouette sheet.

## Materials (for the integration agent)

There is one master material, `M_Unit_Trim`, with three slots on every mesh. All of them sample the same
trim sheet.

| Slot | Holds |
|---|---|
| 0 `Structure` | figures, animals, wood, metal, glass |
| 1 `Roof` | canvas, white hulls, robes, smoke |
| 2 `Detail` | culture cloth and paint, the ring, gold, glows |

- Base colour = `T_D * lerp(1, CultureColor, T_M.G)`.
- Emissive = `BaseColor * (T_M.B * EmissiveStrength + T_M.R * NightFactor)`.
- The channels of `T_M`:
  - R: night windows (vehicle glass).
  - G: culture mask.
  - B: always-on glow (the halo, engines, plume, lamps, and the ring at 0.12).
- `T_ORM` is linear: R occlusion, G roughness, B metallic.
- `T_N` uses the OpenGL convention: **flip green on import**.

Instances: `MI_Unit_Structure`, `MI_Unit_Roof`, `MI_Unit_Detail_{Amber,Sky,Teal,Yellow,Vermilion,Rose}`
(`DM.CULTURE_COLORS` in order). If the units HISM keeps passing the culture colour as per-instance
custom data (as `DmTownsActor` does today), drive `CultureColor` from it instead and use one Detail
instance.

**Scale in game:** the units layer scales every unit by the same distance factor, so a 46 m freight
train would draw 2.4 times longer than a 19 m settler band. The silhouette sheet draws them at about the
same length. The manifest gives `sheet_px` and `suggested_unit_scale` for each mesh, relative to
`SM_Unit_Settlers_Ancient` = 1. Multiply by it to match the sheet. The ark and the launch are landmarks;
apply their factor only if they are drawn through the units layer.

## Regenerate

From the project root (always through the slot tool):

```
python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python art/units-land/build_units-land.py -- --build
python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python art/units-land/build_units-land.py -- --render --assets settlers-ancient,ark-intact
```

| Option | Effect |
|---|---|
| `--build` | Exports the FBX files, merges `manifest.json` and saves the `.blend`. The `.blend` is saved only for a full build. |
| `--textures` | Regenerates the trim sheet. It is also regenerated when a texture is missing. |
| `--render` | Writes the three previews. |
| `--assets` | Takes short names: `settlers-ancient`, `traders-modern`, `army-space`, `prophet`, `aircraft-prop`, `rocket-pad`, `ark-damaged`, `groundring`, and the rest. |
| `--views 40m,300m,1km` | Picks the preview views. A `15m` close-up is also available. |
| `--outdir` | Sends renders somewhere other than `shots/`. |

A full build takes about 4 s. Rendering takes about 1 s per view with EEVEE.

Each build prints, for each asset:
- the triangles per LOD and the LOD ratios
- the z range, to check the pivot
- a normals check: the share of faces pointing toward their part's axis or centroid

The expected warnings are on the ark (80 faces each): the inner wall of the half-built hull (Construction)
and the torn rim of the damaged nose (Damaged), which face inward on purpose. `Settlers_Ancient` and
`Traders_Ancient` also report 1 face: the end cap of a goat or donkey tail, where the heuristic compares the
cap with its own axis end point. The cap faces outward (false positive; the welded-volume check below agrees).

## Preview rig

The preview rig is built into the script, because `art/tools/preview_rig.py` did not exist when this
was built:
- sun at 20° elevation, warm, from the camera's left and a little behind the subject
- cool sky fill and a neutral earth ground
- EEVEE at 1280×720 with a 50 mm lens
- a 1.8 m grey mannequin beside the unit in the 40 m view, for scale

Tall assets (rocket, ark) and the space craft are pulled back from 40 m until they fit the frame, and
the script logs that. Flying assets are rendered 24 m up, with the ground ring below them.

## Known gaps

- The figures are deliberately low-poly. They have about 16 to 100 triangles: legs, torso, head and
  arms, with stride and lean. They read correctly at the game camera's 20 to 60 px but look like
  mannequins in a 15 m close-up. There are no skeletal or vertex animations. Walk cycles, banner
  flutter, rotating propellers and smoke drift are left to runtime (wobble in the material or an
  instance offset).
- UVs are box-projected per face into the swatch cells of a shared trim sheet, so they overlap by design.
  Nothing is baked, units are movable (no lightmaps), and Unreal can generate a lightmap UV if one is
  ever needed.
- The FBX files carry an `LodGroup` node made from Blender's `fbx_type` empty property, and the children
  are named `_LOD0` to `_LOD3`. Blender's own importer does not read the LodGroup back, so this was
  verified only as far as the hierarchy, scale (metres to centimetres), materials and UVs. Check the
  first Unreal import.
- The ground ring is a lit flat band with a small emissive term. On yellow over savanna or dunes, and on
  green or sky blue over sea, it relies on the dark silhouette, as the concept notes say. No outline
  variant is built.
- Ships are not in this family (sea units). The freight train and maglev carry a short piece of track or
  guideway that moves with them, so they do not need a rail network to read.
- The 300 m and 1 km previews are at true scale, without the game's distance scaling, so the units are
  small in frame. That is the honest far-zoom read.
- Not built: the crawler convoy that the concept requested (it never appeared in the concept art), and
  per-culture mesh variants (culture is material-only).

## Independent verification (2026-11-19)

A second engineer rebuilt the family from the script (`--build --textures`: textures and manifest byte-identical,
FBX identical apart from the FBX timestamp), re-imported every FBX into an empty Blender scene and checked it:
hierarchy (`LodGroup` empty + `_LOD0`..`_LOD3`), triangles per LOD against the manifest, scale 1.0 in metres, three
slots in the order `M_Unit_Structure` / `M_Unit_Roof` / `M_Unit_Detail`, UVs inside 0-1, no n-gons, pivots
(every footprint centred on 0,0; ground at z 0, aprons at -0.3), and the normals after welding coincident
vertices (no closed part has negative volume or inconsistent winding). Fixes made then, all in the script:

- **Preview rig:** the 1.8 m reference figure was shifted down instead of up after the join, so it stood
  1.56 m underground with only its head showing. It now stands on the ground in every 40 m view.
- **Degenerate triangles:** fanning n-gon caps with collinear corners (the bus ends) made 12 zero-area
  triangles in `SM_Unit_Settlers_Modern` (LOD0 1088 to 1076). They are dropped, and the manifest now
  counts the triangles of the exported mesh rather than the builder's polygon list.
- **Track strip:** every 4 m ballast segment had two hidden end faces inside the strip (30 non-manifold
  edges in `SM_Unit_Traders_Modern`). Only the strip ends are capped now (LOD0 2416 to 2376).
- **Truck column LODs:** LOD1 was 62 % of LOD0. Glass, lamps and the middle axle are now LOD0 only
  (LOD1 1068 to 668, 39 %).
- **Damaged ark:** the breach was a see-through hole in the single-sided hull. A scorched inner wall now
  sits behind it, and the smoke plume that the manifest description promised rises from the torn nose
  (LOD0 3508 to 3902).
- **Launch:** the smoke column hid all but about 4 m of the plume, so the rocket seemed to float over a
  cloud. It now has three low puffs, which leaves about 11 m of white-gold plume in view (LOD0 2650 to 2450).
- **Manifest text:** the ring glow is 0.12 in `T_M.B` (it said 0.35). Aircraft entries now say that the
  ground ring is scaled about 1.9 times to clear the wingspan.

Still open (for the integration agent): the FBX `LodGroup` node is written by Blender's exporter as a
`NodeAttribute` of type `LodGroup`. Unreal has to import it with *Import Mesh LODs* on, and that is
unconfirmed until the first import. `art/tools/ue_import.py` (the shrine pipeline) expects separate FBX
files for each LOD under `art/export/`, so it cannot import this family as it is. Remaining minor items:
a few welded edges where parts touch (wheels or yokes in the ancient units, propeller hubs) are
non-manifold, and the aircraft and small space craft keep 55-70 % at LOD1 (all under 600 triangles).

