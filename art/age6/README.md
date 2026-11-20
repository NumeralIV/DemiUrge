# Age6: the Space Age kit

Script-generated meshes for the Space Age capital (concepts `docs/concept/settlements/age-6-space.png` and
`docs/concept/landmarks/building-launch.png`). Everything here is original. It is produced by the scripts in this
folder: no downloads, no hand-edited exports. Licence: `original`.

## What is here

| Asset | Mesh (one per state) | Size (m) | LOD0 tris (Intact) | Notes |
|---|---|---|---|---|
| Arcology tower A | `SM_Age6_ArcologyTower_A_*` | 32 x 28 x 136 | 8.7k | Garden spire: slender rounded shaft with alternating garden balconies, gold ring, planted crown, spire |
| Arcology tower B | `SM_Age6_ArcologyTower_B_*` | 38 x 32 x 124 | 6.8k | Four rounded tiers with planted setbacks, roof solar |
| Arcology tower C | `SM_Age6_ArcologyTower_C_*` | 29 x 29 x 176 | 5.2k | Round tower with a helix of garden balconies |
| Arcology tower D | `SM_Age6_ArcologyTower_D_*` | 48 x 40 x 103 | 6.6k | Twin rounded slabs on a planted podium, garden sky-bridges |
| Arcology tower E | `SM_Age6_ArcologyTower_E_*` | 48 x 44 x 191 | 19.0k | Hero: six-step garden "mountain" with a spire tower, sky-garden rings, glass dome |
| Green-roof block A | `SM_Age6_GreenBlock_A_*` | 44 x 28 x 22 | 8.2k | Stadium ziggurat, five planted steps |
| Green-roof block B | `SM_Age6_GreenBlock_B_*` | 40 x 30 x 25 | 6.9k | Street block stepping down to its front (-Y) |
| Green-roof block C | `SM_Age6_GreenBlock_C_*` | 32 x 32 x 36 | 7.1k | Round drum tiers, gold ring, glass dome |
| Green-roof block D | `SM_Age6_GreenBlock_D_*` | 48 x 40 x 23 | 7.5k | Courtyard block, garden court, penthouse |
| Maglev station | `SM_Age6_MaglevStation_A_*` | 90 x 54 x 18 | 6.4k | Two guideways between platforms under a white-ribbed glass vault, planted halls both sides |
| Maglev track, straight | `SM_Age6_MaglevTrack_Straight_*` | 40 x 9 x 10 | 0.2k | Twin guideway, one pylon at the centre; chains every 40 m |
| Maglev track, curve | `SM_Age6_MaglevTrack_Curve_*` | 40 x 11 x 10 | 0.6k | 40 m arc, R = 160 m (14.3 deg), curving to +Y |
| Solar array A | `SM_Age6_SolarArray_A_*` | 46 x 30 x 3 | 4.8k | Six rows of tilted tables facing -Y |
| Solar array B | `SM_Age6_SolarArray_B_*` | 46 x 30 x 5 | 0.9k | Agrivoltaic canopy over planted strips |
| Launch pad | `SM_Age6_LaunchPad_*` | 140 x 124 x 115 | 6.3k | Apron, hardstand with flame trench, mount, 92 m lattice gantry with arms and crane, fuel spheres, tanks, pipes, water tower, light masts, service buildings, rail sidings, scorch |
| Rocket | `SM_Age6_Rocket_Intact` | 7 x 15 x 82 | 1.5k | Core + two boosters; stands on the pad socket `rocket_mount` (0, 0, 9) |
| Assembly hall | `SM_Age6_AssemblyHall_*` | 222 x 106 x 65 | 1.1k | The biggest box in the game, long and low (the rocket stands taller, as in the concepts): ribbed cladding and pilasters, hipped metal roof with skylight ridge, three 30 x 50 m door bays on the long front (-Y; launchers are integrated lying down and rolled out), end annexes, workshop lean-to, rooftop plant |
| Control bunker | `SM_Age6_ControlBunker_*` | 48 x 40 x 30 | 0.3k | Earth-bermed mound (grass), concrete block with slit windows, masts, dish |
| Crawlerway | `SM_Age6_Crawlerway_Intact` | 60 x 33 x 0.6 | 0.2k | Two concrete lanes and a gravel median; chains every 60 m |

States: every building and the track, solar and pad pieces have `_Intact`, `_Construction`, `_Damaged`, `_Ruin` on the
same footprint (70 meshes in all). The rocket and the crawlerway are `_Intact` only (the planner already scales the
rocket's height by the ark's progress).

Totals over all 70 meshes: LOD0 253,530, LOD1 90,915, LOD2 25,205, LOD3 7,634 triangles. Per-mesh counts, footprints,
bounds, sockets, slots, textures, states and the sim mapping are in `manifest.json`.

## Files

| Path | What |
|---|---|
| `build_age6.py` | Entry point: textures, materials and culture instances, all meshes x states x LODs, FBX export, manifest, `.blend` |
| `age6_tex.py` | Procedural tileable texture sets (numpy, uses `art/tools/dmtex.py`) |
| `age6_kit.py` | Geometry kit: parts, profiles, prism / lathe / sweep / hedge primitives, UV skins, state cuts, rubble, scaffold, crane, merge |
| `age6_assets.py` | The asset builders and the registry (`ASSETS`) |
| `preview_age6.py` | Preview renders (EEVEE 1280x720), run through `tools/render_slot.py` |
| `export/SM_Age6_*.fbx` | One file per mesh: an `LodGroup` empty with children `<mesh>_LOD0..LOD3` |
| `textures/T_Age6_*.png` | 6 sets at 1024^2: Facade, GreenRoof, Tech (D/N/ORM/M), Concrete, Hull, Cladding (D/N/ORM) |
| `source/age6.blend` | Everything, with preview materials (LOD1-3 hidden) |
| `manifest.json` | Integration data |
| `verify_age6.py` | Independent check: re-imports every FBX (LodGroup, LODs, triangles vs manifest, slots, UVs, `Col`, bounds, open / non-manifold edges, inside-out shells, degenerate faces) and the manifest / textures; writes `verify_report.json` |
| `verify_render_age6.py` | Verification renders on the preview rig: forced LOD0-3 sheet and a 1.8 m figure scale check |

## Regenerate

```
"C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python art/age6/build_age6.py
python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b art/age6/source/age6.blend --python art/age6/preview_age6.py
```

Checks (no rendering for the first; the second through the slot tool):

```
"C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python art/age6/verify_age6.py
python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b art/age6/source/age6.blend --python art/age6/verify_render_age6.py
```

`build_age6.py` takes `--only Name1,Name2` (writes `age6_partial.blend` and merges into the manifest), `--skip-tex`
and `--no-export`. The build is about 15-50 s. `preview_age6.py` takes `--shots towers,greenblocks,maglev,solar,launch,states,cultures,district`
and `--fast` (16 samples).

## Conventions used

- 1 unit = 1 m, Z up, front toward -Y, pivot at the footprint centre on the ground, plinth to Z = -0.3. The assembly
  hall is re-centred on its footprint (annexes included). Its `doors` socket is in the manifest.
- Slots: `Structure`, `Roof`, `Detail` (at most 3). The material in each slot depends on the mesh:
  - city buildings: `M_Age6_Facade` / `M_Age6_GreenRoof` / `M_Age6_Tech`
  - launch, track, solar: `M_Age6_Concrete` / (`M_Age6_GreenRoof`) / `M_Age6_Tech`
  - rocket: `M_Age6_Hull` / - / `M_Age6_Tech`
  - hall: `M_Age6_Cladding` / `M_Age6_RoofMetal` / `M_Age6_Tech` (RoofMetal = the Cladding textures, albedo x0.62)
- The FBX slot list is compact: a mesh that does not use `Roof` has two slots (`Structure` then `Detail`). Match
  slots by material name through `material_slots` in the manifest, not by index.
- UVs are world-scale and tile (nothing is baked), so they run outside 0..1 by design. The Tech texture is an atlas
  of flat-colour regions (white metal, dark steel, pale gold, black, signal orange, red lamp, glazing) plus a solar
  module; Tech faces are mapped into the middle of their region.
- Facade storeys are 3.6 m; tiers start on multiples of it so the window bands line up across tiers.
- `M` texture: R = night emissive (lit windows, lamps, station glazing), G = culture tint mask, B = accent (pale gold)
  mask. `ORM` is linear. Normals are OpenGL (+Y): flip green on import.
- Vertex colour `Col` (all three channels the same) multiplies the albedo: 1 clean, down to 0.12 for scorch and
  rubble. It carries the damage/ruin charring and the launch-pad exhaust scorch.
- Culture variants are material instances: `MI_Age6_{Facade,Tech,GreenRoof}_{CliffTerrace,LagoonWeaver,RingBuilder}`
  (parameters in `manifest.json` under `culture_instances`). Default is the concept's white, cream and pale gold.
- Damage states are cut from the intact geometry by planes, so they share the footprint and pivot: Construction is
  the lower 45-60 % with bare unglazed top storeys, scaffolding and a tower crane; Damaged has a top corner sheared off,
  charred cut faces, broken chunks on the rim and rubble on one side; Ruin is a charred stump at about a third of the
  height with rubble all round. Track and solar pieces lose a span or panels instead.
- Sockets (manifest): tower `crown`, pad `rocket_mount` and `crawlerway`, track and crawlerway `start` / `end`
  (x, y, z, heading deg), station `start` / `end`, hall `doors`. A suggested launch-site layout is under
  `launch_site_layout`.

## Previews (shots/asset-age6-*.png)

towers / greenblocks / maglev / solar at 40 m, 300 m and 1 km; launch at 40 m, the pad at 300 m (the concept's
view), the hall at 300 m and the whole site at 1 km; states (towers, blocks, track; and the pad) at 300 m; the four
cultures at 300 m; a district made only from this kit at 1 km by day and by night. LODs in the previews are picked by
screen size, as in game.

## Known gaps

- **Not imported into Unreal yet.** The FBX was re-imported into Blender (LodGroup node present in the file, 4 LODs,
  at most 3 slots, metres, `Col`); the LodGroup, unit scale and the green-channel flip still need a check on the first
  UE import.
- **Budgets:** towers B-D, green blocks B-D and the station sit at 5-7.5k, a little under the civic 8-20k band; towers A
  and E and block A are inside it. The assembly hall is only 1.1k (a clad box with pilasters, doors and roof plant):
  in the civic band by role, but there is plenty of room for close-up detail. The facades, cladding and panels are
  carried by texture.
- **LOD1-3 are partly decimated.** LOD0 of all 70 meshes is closed (no open edges, no inside-out shells). Collapse
  decimation of thin beams at LOD1-3 leaves slivers, so 35 far LODs report a few open edges (3-140); LOD3 is at least
  ~100 triangles so separate boxes do not shatter. Solar LOD2/3 are cards above the ground, and two ruin LOD2/3s have
  rubble 0.1-0.3 m below the plinth: harmless at those screen sizes.
- **Facade realism**: one facade trim for every city building (glass bands, cream panels, pale-gold sill). From 1 km
  the windows can speckle; a second, calmer facade for the lower blocks would help. Glass is opaque (reflections only).
- **Planting** is lumpy hedge bands and 80-face tree blobs: fine from 300 m, still a little blobby at 40 m. Real tree
  cards or the Tree family's meshes would lift the close-ups.
- **Culture variation is tint only** and subtle in the previews (no culture-specific crowns or ornaments yet); the
  `crown` socket on each tower is where they would go.
- **Not in this family:** the rocket's venting vapour and launch FX, the maglev train, wind turbines, the domed temple,
  roads, trees on the ground and the coastline from the concept.
- The night preview is only a check that the `M` emissive mask works; it is flatter than `city-night.png` (no street
  lights, which belong to the roads).
- The preview rig has no world volume: a world volume renders black in EEVEE here, so the haze is a compositor mix by
  the mist pass.

## Independent verification (2026-11-19)

Rebuilt from scratch twice: textures byte-identical to the builder's, triangle counts identical, and the manifest
identical between rebuilds (apart from its timestamp). `verify_age6.py` re-imported all 70 FBX files. Fixes, all in
the scripts:

1. **Every beam was inside-out** (`age6_kit.beam` built a left-handed frame, mirroring the box): 254 inverted shells on
   the launch pad (the whole gantry lattice, light masts, pipes, crane), the solar legs and canopy posts, the dome
   ribs on block C, and every scaffold in the Construction states. Invisible in Blender, but with back-face culling in
   UE the lattice would have rendered from the inside. The winding is flipped back when the frame is mirrored.
2. **Manifest triangle counts disagreed with the files** on 34 meshes: decimation left zero-area and back-to-back
   duplicate triangles that importers drop (creating holes). Every LOD is now cleaned before it is counted, so the
   manifest matches the re-imported FBX on all 280 LODs.
3. **Far LODs shattered**: decimating a few separate slabs to 4 % (e.g. 6 triangles for the crawlerway) broke the
   crawlerway into dashes at 1 km, left the hall and bunker with missing faces and floated the bunker. LODs now keep at
   least ~96 triangles and never exceed the LOD before them.
4. **Assembly hall re-proportioned** to the concepts' "huge low hall" that the rocket stands taller than: it was a
   124 x 84 x 104 m near-cube that read as a tower block at 1 km. Now 176 x 92 x 58 m (65 m with the roof), a hipped
   roof with a plan-projected texture (side mapping stretched it), three door bays on the long front, pilasters, end
   annexes and a back lean-to. The manifest's launch-site layout now puts the hall doors exactly at the end of three
   crawlerway segments (it overlapped the crawlerway before), and the previews use that layout.
5. **Faceted trees**: the LOD0 tree and shrub blobs were 20-face icosahedra (a low-poly read at 40 m). They are 80-face
   spheres now (LOD1 and rubble keep 20). This moved towers A and E and block A into the civic band (E 19.0k).
6. **Damaged-state leftovers**: the gantry's inner core and the hall's pilasters and door posts were 'detail' parts, so
   they survived damage cuts whole and stood up as a black monolith / bare ribs above the break. They are cut with the
   structure now.
7. The rocket is budgeted as a vehicle (1-4k; 1.5k) rather than civic. The pad preview is framed like the concept
   (rocket and gantry left, hall right, fuel spheres in front, low sun front left). New verification renders:
   `shots/asset-age6-verify-lods.png`, `shots/asset-age6-verify-scale.png`.
