# art/culture: culture overlays for the Age2 and Age3 kits

Roof, ornament and structural variant meshes, plus material instances, that turn the Age2 (Iron) and Age3 (Sail) house kits into the three invented cultures from `docs/concept/settlements/culture-a|b|c.png`:

| | Culture | What carries it |
|---|---|---|
| **A** | Cliff-Terrace (warm canyon) | Every house wears a plaster roof terrace: a jettied attic storey with cobalt-framed windows, a parapet open toward the stair, and a lime-washed dome. Houses stack up red sandstone terraces with stairs and cisterns. The temple is an arched niche carved into the cliff. |
| **B** | Lagoon-Weaver (aqua lagoon) | Every house stands on a tarred pile platform over the water and wears a tall silver-shingle saddle roof whose ridge sweeps up into horns, with saffron ribbons. Curved boardwalks run round the water courts, joined by bridges, with floating gardens, a woven breakwater and a spiral-roofed pier temple. |
| **C** | Ring-Builder (cool highland) | Pale limestone drum houses with tall bell-cast slate cones and ring finials. Rectangular hosts get a steep slate gable with stone gable walls. Dry-stone ring walls, a 29 m spire of stacked discs, and standing stones. |

Everything is original and procedural: Blender 5.2 Python, numpy textures and no external inputs (licence `original`).

## How overlays meet the host kits

We coordinate only through `art/age2/manifest.json` and `art/age3/manifest.json`. They are read, never edited.

The host kits bake their pitched roofs into the house meshes, so a culture roof cannot swap the host roof. It has to **envelope** it. For each house listed in those manifests, `cul_hosts.py` imports the Intact FBX the manifest points to (read-only, in a throw-away scene) and measures the main roof in host space. That gives its bounds, eave height, ridge height and ridge axis. L-shaped blocks are split into two rectangles; the second wing is extended along its ridge to where the host's ridge really ends. Small roof pieces hugging the main roof (Age2 straw fringe, verges) grow the rectangle and lower its eave. Each rectangle also records `profile`, the host roof's measured upper envelope across the ridge (21 samples from the centre line out), which the B and C roofs clear by 0.12 m, so asymmetric eaves and dormers stay inside. Small high islands such as towers and belvederes are left out and rise through the culture roof. The results are cached in `hosts.json` (delete it to force a re-measure after changing `cul_hosts.py`).

The build then generates, per host house:

- `SM_Culture_A<age>Roof<Host>_<State>`: Cliff-Terrace attic storey, terrace and dome
- `SM_Culture_B<age>Roof<Host>_<State>`: Lagoon-Weaver saddle roof. Its height is raised until the shell clears the host gable.
- `SM_Culture_B<age>Stilts<Host>_<State>`: pile platform. The deck top is at the host's ground (z 0) and the water is at -1.8. Raise host and platform together 1.8 m above the water.
- `SM_Culture_C<age>Roof<Host>_<State>`: Ring-Builder steep slate gable with stone gable walls

These overlays are modelled **in host space**: place them with exactly the host's transform. Pair each one with the same state of the host mesh, for example `SM_Age2_House_A_Damaged` with `SM_Culture_A2RoofHouseA_Damaged`. The hosts re-skin themselves for each culture through their own material instances (`MI_Age2_*_A|B|C`, Age3 `CultureA|B|C` parameters). The manifest lists them under `host_culture_materials`.

If a host is listed in a manifest but its FBX is missing, its overlay is sized from the manifest's own numbers (bounds, eave, ridge). If a kit lists no houses at all, nominal stand-in hosts are used (`NominalS/M/L/T`).

## The other assets

| Asset | Culture | Pivot / sockets |
|---|---|---|
| `A2Ornament`, `A3Ornament` | A | bottom centre of the host doorway on the wall face; cobalt surround, band, threshold (A3: arch, screened windows) |
| `ATerraceStair` | A | footprint centre; `TerraceTop` z 3 m, stair foot / head sockets |
| `ACistern` | A | footprint centre; Roof slot = water MI |
| `ATempleNiche` | A | footprint centre; 18 x 22 m rock face, 15 m arched niche |
| `BWalkwayStraight`, `BWalkwayCurve`, `BBridge` | B | footprint centre; water at z 0, deck +1.8 m (level with the `B*Stilts*` house decks). The curve is 45° at a 10 m centre-line radius, so 8 pieces make a water court. |
| `BGardenRaft`, `BBreakwater`, `BTempleSpiral` | B | footprint centre; water at z 0 |
| `B2Ornament`, `B3Ornament` | B | woven reed facade panels for the host front wall (scale X to the wall) |
| `C2DrumHouse`, `C3DrumHouse` | C | footprint centre; `RoofBase` socket at 2.75 m / 5.6 m. These replace the host body: the Ring-Builders have one house type. |
| `C2RoofCone`, `C3RoofCone` | C | the drum's eave plane (`RoofBase`) |
| `CRingWallCurve` (R 25 m), `CRingWallStraight`, `CSpire`, `CStandingStone1-3` | C | footprint centre |
| `C2Ornament`, `C3Ornament` | C | door relief: lintel with concentric rings (C3: stone wheel, pillar stones) |

## States

Every building part has `_Intact`, `_Construction`, `_Damaged` and `_Ruin` on the same footprint (`cul_states.py`):

- **Roof overlays**
  - Construction: cut timbers and bundles of roofing wait on the ground in front of the host.
  - Damaged: holes are punched through the covering, so the rafters show, and bites are taken out of the walls. A third of the ornaments are gone or knocked askew, and there is rubble.
  - Ruin: collapsed covering, beams and rubble lie on the host's ground.
- **Structures**
  - Construction: built to about 55 % with a ragged course line and a timber scaffold. Decks and boardwalks are laid from one end.
  - Damaged: bites and rubble.
  - Ruin: broken to 30-50 % with a jagged top, plus rubble mounds.

Ornaments and props are `_Intact` only. Keep them with the host's Intact and Damaged states, and hide them in Construction and Ruin.

## LODs, materials, textures

- **LODs.** LOD0, LOD1 (~40 %), LOD2 (~12 %) and, for anything over 10 m, LOD3 (~4 %, a decimated hull). The LOD3 decision is per asset, so every state of an asset has the same LOD count. Every LOD is cleaned after decimation (folded duplicate faces, degenerate faces, loose vertices removed; inside-out closed islands flipped). Screen sizes are 1.0 / 0.35 / 0.12 / 0.04. The generators build LOD1/2 with fewer segments and fewer details; collapse decimation only tops up.
- **Slots and exports.** At most three slots, named exactly `Structure`, `Roof`, `Detail`; unused slots are left out. One FBX per mesh state is written to `export/`: an FBX LodGroup node `<name>` (identity transform) with the children `<name>_LOD0..LOD3`, as in the other families, so Unreal's *Import Mesh LODs* reads them as one mesh's LODs. That is binary FBX with face smoothing, tangents and triangles.
- **Material instances.** All are instances of the family master `M_Culture_Standard`, except `MI_Culture_Water_A`, which parents the shared water material. The parameters, textures, tints and accent colours are in `manifest.json` → `material_instances`. Every mesh lists its slot → MI mapping.
- **Textures.** 1024² PNG, `T_Culture_<Set>_D|N|ORM` (+ `_M` on the trim sheet), in `textures/`.
  - Only `D` is sRGB.
  - `N` is OpenGL +Y: flip green on import to Unreal.
  - `ORM` is AO / roughness / metallic.
  - The trim sheet has 8 bands that tile in U (2 m per tile): oak, tar, cobalt, saffron, rope, pale stone, woven reed and lit window. Its `M` channels are R night-window emissive, G culture accent tint and B cloth.
  - The texture sets are PlasterA, DomeA, RubbleA, TimberB, ShingleB, GardenB, StoneC, SlateC, DryStoneC, Rock (tinted per culture) and Trim.

## Regenerate

From the project root:

```
"C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python art/culture/build_culture.py -- [--tex] [--only A2RoofHouseA,CSpire] [--no-fbx]
python tools/render_slot.py -- "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b art/culture/culture.blend --python art/culture/preview_culture.py -- --set a|b|c|states|kit [--views 40m,300m,1km]
```

Re-run the build whenever the Age2 or Age3 kit changes its houses. `hosts.json` is refreshed automatically when a manifest changes, and stale exports are removed. A full build takes about 2-5 minutes, depending on machine load.

Files:
- `build_culture.py`: driver
- `cul_assets.py`: generators and registry
- `cul_geo.py`: bmesh builders and UVs
- `cul_states.py`: state builder and island-wise booleans
- `cul_hosts.py`: host measurement
- `cul_tex.py`: textures, using `art/tools/dmtex.py`
- `cul_mats.py`: MIs
- `preview_culture.py`: preview renders

Outputs:
- `manifest.json`
- `export/*.fbx`
- `textures/*.png`
- `culture.blend`: every LOD of every state, with preview materials
- `hosts.json`

Previews are `shots/asset-culture-{a,b,c}-{40m,300m,1km}.png`, `shots/asset-culture-states-40m.png` and `shots/asset-culture-kit-40m.png`.

## What is there (build of 2026-11-19)

- **57 assets** (A 13, B 24, C 20) → **195 meshes** (asset × state) → **195 FBX**.
  - Per-host overlays cover the 8 houses the kits publish: Age2 House_A-D and Age3 Townhouse_A-D.
  - Age2 House_D and Age3 Townhouse_D are L-shaped, so each gets a two-wing overlay.
- **Triangles, all meshes and states** (after verification): LOD0 269,790 / LOD1 109,885 / LOD2 28,690 / LOD3 9,285. The Intact LOD0 sum is 81,712. 164 meshes have 4 LODs, 31 (small props, ornaments, sub-10 m pieces) have 3.
- **Intact LOD0 ranges by role:**
  - roofs 864-3,954
  - structures 548-2,316
  - landmarks 2,836-7,104
  - ornaments 60-908
  - props 192-816
- **Textures:** 34 PNGs (11 sets).
- **Previews:**
  - `shots/asset-culture-a|b|c-40m|300m|1km.png`: town vignettes on the real Age2 / Age3 host meshes
  - `shots/asset-culture-states-40m.png`: Intact / Construction / Damaged / Ruin for one house of each culture, paired with the host's own state meshes
  - `shots/asset-culture-kit-40m.png`
  - Orange 1.8 m figures stand in the 40 m views for scale.

## Known gaps

- **Host roofs are enveloped, not removed.** The Age2 and Age3 houses carry their own roof, so every culture roof is a shell around it. A host chimney, belvedere or porch roof outside the main roof can still poke through or show beside it; that is intended for towers and chimneys.
  - The hidden host roof still costs triangles. Host + overlay (+ pile platform for B) LOD0 runs from 2.9k to 10.8k; 18 of the 48 Intact/Damaged pairings exceed the 6k house budget, worst B3 Townhouse_D Damaged (10.8k) and B2 House_D Damaged (10.4k).
  - A roofless body variant from the host kits would remove both problems. That is a proposal for the Age2/Age3 owners, not something this folder can change.
- **Host states differ in shape from the host's Intact roof.** A damaged host roof that sags or slides can stick out past the culture roof's Damaged shell; holes in a Damaged culture roof show the host's own roof underneath (1-8 % of the roof seen from the game camera). Ruin and Construction pairs are clean, because the overlay comes down to the ground there.
- **No door sockets.** The host manifests publish no door positions, so `A*/B*/C*Ornament` have a door-relative pivot but nothing to snap to yet. The vignettes do not place them on hosts.
- **Age3 hosts keep their own culture look.** Age3 textures are banded atlases, so the culture wall overrides (`host_culture_materials.wall_overrides`, Age2 only) cannot apply there. Age3 hosts keep their CultureA/B/C parameters, which are tints.
- **Open surfaces.** 23 Intact meshes report non-manifold edges. These are intentionally open thin surfaces: water discs, dome lip rings, window panes. No closed part is open.
- **Tiling UVs.** UVs are world-scale tiling (metres / 4 m, trim 2 m), not 0-1 unwrapped. Nothing here is baked, so a lightmap UV must be generated on import if static lighting is ever used.
- **Shared slots, culture-specific instances.** Material slots are named `Structure`/`Roof`/`Detail` in every FBX. The culture-specific MIs are assigned from the manifest (`meshes[].materials`); the `.uasset` instances are not created here.
- **Preview dressing is placeholder.** The ground, water, palms, mangroves and fields in the vignettes are crude stand-ins. Judge the buildings, not the terrain.
- **Saffron ribbons** are thin strips and barely register beyond 40 m. They add life up close only.
- **Host secondary roofs.** Porch canopies and lean-tos away from the main roof are not covered: Age2 House_B (back lean-to and front canopy, 7 % of the visible roof), House_D (side lean-to, 1-2 %) and the Age3 Townhouse_D belvedere (3-4 %, intended) keep the host's roof material.

## Verification (independent, 2026-11-19)

Rebuilt from scratch twice with `--tex` (textures byte-identical; manifest identical except the pier temple's Damaged convex-hull LOD3, which varies by 2 triangles), re-imported every FBX into empty scenes and checked LOD structure, triangle counts against the manifest, loose and degenerate geometry, closed-island winding, UV layers, slots, transforms, back faces seen from the game camera's hemisphere, and host poke-through (host LOD0 + overlay LOD0 from the FBX exports, ray-cast from 25-70 degrees elevation; the share of visible roof hits that land on the host's roof). Fixes, all in these scripts:

1. **No LodGroup in any FBX** (195 files): the LODs were four loose meshes, which Unreal would combine or import as separate meshes. `export_fbx` now writes an `fbx_type = LodGroup` empty parenting `_LOD0..3`, like every other family.
2. **Ring-Builder (C) roofs let the host roof through on every Age2 house**: the shallow bell-cast kick ran below the host thatch, so 31-38 % of the visible roof on C2 House_A-D (and 2-8 % on the C3 townhouses) was host thatch. The C roof now widens (and only if needed lifts) until its underside clears the host envelope by 0.12 m; gable walls keep the host width. C2 House_A/C and C3 Townhouse_A/B/C: 0 %.
3. **Wrong ridge axis for the second wing of Age2 House_D**: its overlay ran across the wing's ridge in all three cultures (10 % host showing). The axis is now chosen by fitting both gable models; the wing is extended to where its ridge ends.
4. **Asymmetric host roofs, fringes and dormers** (Age2 straw fringe under every A attic, House_D's high back eave, Age3 Townhouse_C dormers through B and C): hosts now carry a measured cross-ridge `profile` that the B and C roofs clear, and fringe pieces grow the rectangles. Intact poke-through is now 0-2 % everywhere except the secondary roofs listed above (was up to 38 %).
5. **Damaged Age3 Townhouse_B saddle lost its whole front slope** (the EXACT boolean misfired on the thin shell; 35 % of the host roof showed): booleans that remove far more than the cutter could are rejected and the island kept uncut (now 0.7 %).
6. **Decimation junk**: LOD1-3 carried folded duplicate faces (up to 30 % of a LOD, e.g. B2 House_D Damaged LOD2: 136 of 448), loose vertices on 32 LODs and 6 inside-out convex-hull LOD3s (the pier temple's 367 m2 hull rendered inverted). Every LOD is now cleaned and the manifest counts match the FBX exactly.
7. **Cistern water faced down** (culled from every camera above it); it now faces up.
8. **LOD count now per asset**, not per state (Damaged roofs had a LOD3 their Intact twin lacked).
9. **Scale**: boardwalk and bridge decks raised from 1.3 m to 1.8 m over the water, level with the house pile decks they join (was a 0.5 m step); terrace stair now 15 risers of 0.2 m instead of 10 of 0.3 m.

Left as is: Construction-state roof timbers show 0.3-1 % open tube ends from above; 6 sub-millimetre zero-area faces; pivots of the stilts, drum houses and temple niche are offset 0.4-1.4 m from their bounding-box centre by verandahs, door steps and stairs (the main footprint is centred).

