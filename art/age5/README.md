# Age5 — Atomic Age kit

Script-generated meshes for the Atomic Age (concept targets: `docs/concept/settlements/age-5-atomic.png`,
`docs/concept/landmarks/building-plant.png`, night read from `city-night.png`). Everything here is original and
procedural: no external inputs, licence `original`. Built to `docs/ASSETS.md`.

## What is there

100 meshes (one FBX each in `export/`, LOD0 to LOD3 plus sockets), 3 texture sets, one source `.blend`, and
`manifest.json` (footprint, height, triangles per LOD, materials, textures, states, culture variants, sockets,
suggested mapping to the town kit's `EDmTownMesh` ids).

| Asset | Variants (storeys) | States | LOD0 tris (intact) | Notes |
|---|---|---|---|---|
| `ApartmentSlab` | Low 9 / Mid 12 / High 16 | 4 | 2.7k–4.5k | 36 x 12 m precast slab, balcony columns front and back, blank gable ends, shops at street level |
| `ApartmentPoint` | Low 14 / Mid 20 / High 26 | 4 | 2.9k–5.3k | 21 m cruciform point tower, L-shaped corner loggias, roof plant room and mast |
| `ApartmentBrick` | Low 5 / Mid 7 / High 9 | 4 | 0.8k–1.3k | 30 x 15 m brick gallery-access block, oriel bays, stair tower, shops |
| `OfficeGlass` | Low 16 / Mid 24 / High 32 | 4 | 1.7k–1.8k | curtain-wall slab on a two-storey podium, aluminium fins, penthouse, roof plant |
| `OfficeRibbon` | Low 12 / Mid 18 / High 25 | 4 | 1.4k–2.1k | setback concrete tower (the concept's beige towers), ribbon windows, sun-shade blades, terraces |
| `OfficeRound` | Low 12 / Mid 18 / High 24 | 4 | 2.2k–4.5k | round curtain-wall tower with balcony rings on a square podium, crown and mast |
| `PowerPlant` | Std | 4 | 3.3k | containment dome, turbine hall, auxiliary/fuel/admin/pump buildings, stack, tanks, fence |
| `CoolingTower` | Std | 4 | 3.5k | hyperbolic shell, 112 m, 77 m base, V-legs, basin; `SOCKET_Steam` at the top |
| `Switchyard` | Std | 4 | 6.2k | three gantry rows, breakers, transformers, control hut |
| `Pylon` | Std | intact | 0.9k | 44 m lattice pylon, `SOCKET_Wire` at each insulator |
| `University` | Std | 4 | 5.4k | teaching slab on pilotis, library tower, raked lecture hall, copper planetarium dome, lawn quad, atom sculpture |
| `HighwayInterchange` | Std | Intact, Damaged, Ruin | 11.2k | 240 m cloverleaf: 6-lane viaduct (deck top 9 m) along X over a 4-lane arterial, 4 loops, 4 ramps |
| `HighwayViaduct` | Std | 4 | 0.35k | 60 m tiling segment along X matching the interchange (pier at its centre) |
| `HighwayGrade` | Std | intact | 0.3k | 60 m at-grade segment on a 1 m embankment |
| `Crown` | CultureA / B / C | intact | 0.1k–0.4k | culture roof ornaments placed at any tower's `SOCKET_Crown` |

States are `_Intact`, `_Construction` (top 45 % missing, bare concrete frame, tower crane), `_Damaged` (blast
cut out of the upper front-right corner with torn slabs, streaky soot, rubble at the foot), `_Ruin` (jagged
stumps 6–28 % of the height with one taller remnant, heavy soot, rubble mounds), all on the same footprint.
Totals: 100 meshes, 424k LOD0 triangles, 780k over all LODs (exact numbers in `manifest.json`).

Previews: `shots/asset-age5-<set>-<near|mid|far>.png` for `apartment-slab`, `apartment-point`,
`apartment-brick`, `office-glass`, `office-ribbon`, `office-round`, `plant`, `university`, `highway`,
`states-housing`, `states-plant`, `states-civic`, `cultures`, `district` (and `district-night`).

## How it is built

- `build_age5.py` driver: asset table, materials, mesh finalising (weld, triangulate, colours), FBX export,
  manifest, `.blend`. `age5_geo.py` geometry core. `age5_assets.py` one builder per asset.
  `age5_textures.py` the texture sets (uses the shared `art/tools/dmtex.py`). `preview_age5.py` the preview rig.
- **Voxel grids.** Buildings are grids of cells (one bay by one storey). The intact building, its construction
  stage, damage and ruin are the same grid with cells removed by a `State`, so every state is watertight and
  shares the footprint. Exposed interior faces get the blank-concrete strip plus soot; torn slab remnants are
  added at the cut.
- **Facade trim sheet.** `T_Age5_Facade_*` has 8 strips, each exactly one storey tall, tiling every 24 m in U:
  `apt_panel`, `apt_brick`, `office_curtain`, `office_ribbon`, `ground_shop`, `concrete_blank`,
  `industrial`, `civic_fin`. Each storey of each wall maps to one strip, with a per-storey U offset by whole
  bays so lit windows do not form columns. `M` texture: R = night-window mask holding a per-window random rank
  (light a fraction f with `M.R > 1 - f`), G = culture-tint mask.
- **Materials** (3 slots, every mesh): `M_Age5_Structure` (facade sheet), `M_Age5_Roof` (tiling membrane and
  gravel, world-projected at 16 m), `M_Age5_Detail` (sheet: 3-lane road, 2-lane ramp, concrete, grey/yellow
  steel, verdigris copper, rubble, lawn). Vertex colour `Col` (linear): R soot, G ground occlusion on walls.
  The Blender materials show the intended master-material graph:
  `D * lerp(1, tint, M.G) * lerp(1, soot, Col.R) * Col.G * lerp(1, ORM.R, 0.7)`.
- **Cultures** by material instance (`MI_Age5_<Slot>_Culture{A,B,C}`; tints in `manifest.json` → `cultures`):
  A warm ochre/rose plaster, B silver-grey/aqua, C pale cool limestone with blue-grey slate roofs; plus one roof
  ornament mesh per culture (`SM_Age5_Crown_*`, attach at `SOCKET_Crown`).
- **LODs:** LOD1 drops small details and merges balconies into solid bands, LOD2 is massing plus large parts,
  LOD3 is a hull (one box per setback tier, frustums for round parts). Screen sizes 1.0 / 0.35 / 0.12 / 0.04.
- **Sockets** (empties parented to LOD0, exported as `SOCKET_<Kind>_<n>`, e.g. `SOCKET_Crown_1`): `SOCKET_Crown`, `SOCKET_WarnLight` (red aviation lights),
  `SOCKET_Steam` (cooling tower plume), `SOCKET_Exhaust` (plant stack), `SOCKET_Floodlight`, `SOCKET_Lamp`
  (highway lights), `SOCKET_Wire` (pylon insulators).

## Regenerate

From the project root (run renders through the slot tool):

```
"C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python art/age5/build_age5.py -- --textures
python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python art/age5/preview_age5.py -- --set plant
```

Options: `--only <substring>` rebuilds matching meshes and merges them into the manifest; `--no-blend` skips the
`.blend`; `--textures-only`. Preview: `--views near,mid,far,top,close` (close: exactly 40 m from the scale figure), `--tag <suffix>`, `--night`, `--small`.
A full build takes about one minute (textures 4 s).

## Known gaps

- **LOD import:** each FBX holds an FBX **LodGroup** (the root Empty carries `fbx_type = 'LodGroup'`, which
  Blender's exporter writes as the node attribute) with children `<name>_LOD0..3`; Unreal's FBX import with
  "Import Mesh LODs" takes all four in one go. Screen sizes are still set by the import (1.0 / 0.35 / 0.12 / 0.04).
- **LOD ratios** follow the 40 % / 12 % guide loosely: towers are cheap at LOD0 already (1–5k), so their LOD1
  sits at 50–90 % and the brick block's LOD1 barely drops (`lod_ratio` per mesh in the manifest).
- **Pivot:** at the centre of the main block; back cores, balconies and canopies make the bounding box slightly
  off-centre (bounds per mesh in the manifest).
- **Steam, smoke and night lights** are not meshes: use the sockets for FX and the `M.R` mask for windows.
- **Construction/damage of the switchyard and highway** are simple (parts removed, fallen spans, rubble).
  `HighwayInterchange` has no `_Construction`; `Pylon`, `HighwayGrade` and the crowns are intact only.
- **Close-up (40 m):** windows are texture-only (normal-mapped reveals, no geometry); balconies, fins, blades,
  parapets and roof plant are geometry. Holds at 40 m in the previews; a parallax or window-reveal pass in the
  UE master material would add depth.
- The UE master material (tint mask, soot, AO multiply, window emission) must be authored in Unreal to match
  the graph above; the FBX only carries slot names.
- Previews use a neutral flat ground and a box of haze, not the game's terrain or sky.

## Independent verification (2026-11-19)

Rebuilt from the scripts twice (textures byte-identical, manifest identical apart from its date), re-imported all
100 FBX into an empty Blender scene and checked every LOD against `docs/ASSETS.md`: one root and four `_LOD0..3`
meshes, the three slots `M_Age5_Structure/Roof/Detail` in order on every LOD, triangle counts and bounds equal to the
manifest, no n-gons, zero-area or duplicated faces, positive signed volume (outward normals), one UV map and the
`Col` attribute, facade faces each inside one trim strip, Z up, front -Y, pivot at the footprint centre, buildings
reaching Z = -0.3 or lower, every LOD0 within its budget (largest: `OfficeGlass_High_Damaged` 16.2k of 20k).
Scale checked against a 1.8 m figure at exactly 40 m (new `close` preview view): shop storeys 4.2 m, flats 3.0 m,
offices 3.6 m. Fixes:

1. **Socket names** carried Blender duplicate suffixes (`SOCKET_Crown_1.033`, `SOCKET_WarnLight_1.018`, ...) in the
   FBX and the manifest, so every mesh had a differently named crown socket. The .blend copies are now named
   `<mesh>|SOCKET_<Kind>_<n>` and `export_fbx` renames them to the clean name only for the export.
2. **No FBX LodGroup**: the root was a plain Null. It is now a LodGroup (`fbx_type`), as in the age0-age4 kits; the
   "Blender cannot write a LOD group" gap is closed.
3. **University lecture hall**: the east gable triangle reused the west one's UVs (a sheared 244 m-per-UV smear), and
   a triangle duplicated the 0.9 m front band (coplanar faces, z-fighting). UVs fixed, duplicate removed.
4. **Damage read as stacked dark voxels**: every broken interior wall now carries a pale torn floor-slab lip
   (0.45-1.35 m, slightly tilted, LOD0-1) at its storey line, so blast holes and ruins read as exposed storeys.
   +26k LOD0 triangles over the family.
5. **Pylon** legs stopped at Z = -0.1; four concrete footings now reach Z = -0.3.
6. **Tank roofs** (power plant) used the half-grey, half-yellow steel row across their whole circumference, so each
   roof showed yellow pie wedges; they now use the pale concrete row.
7. **Highway asphalt** lifted from `#4a4946` to `#5f5d58` (mean albedo) so highways read as the concept's pale
   ribbons rather than dark lines.

Verifier previews: `shots/asset-age5-<set>-verify-<close|near|mid|far>.png`.

Still open (not fixed here): the ruins' tallest remnant is still a stepped stack of cells; the reactor dome uses the
same pale concrete as the rest (the concept's dome is whiter: brighten it in its material instance if wanted); the
turbine hall's 6 m storeys stretch the corrugated `industrial` strip 2x vertically (invisible on vertical
corrugation); horizontal ledge tops and the plinth sides use one-texel UV smears (small faces, solid colour);
LOD1 ratios stay at 0.5-0.8 on the towers.
