# Real-asset pipeline (Blender -> Unreal) and the first real asset: the village shrine

Track `asset-pipeline` (wave 2). Owner rule: **$0 extra spend** (`docs/research/assets.md`): everything here is
original, built by scripts in Blender 5.2 and imported by scripts into Unreal 5.7. No downloads, no paid tools, no
third-party inputs, so there is nothing to license or attribute.

Reference: `docs/concept/assets/shrine/` (T-014 sheet: dimensions, parts, materials, culture variants) and
`docs/concept/assets/shrine/states/` (T-015: construction, damaged, burnt, ruin, drowned).

**Short version for the next asset author**

1. Write `art/tools/<asset>.py`: `CONFIGS`, `build(config, lod) -> {part: MB}`, `mask(config)`, `collision()`,
   `has_fire(config)`, `FIRE`. Model in real metres, pivot at ground level in the centre of the footprint, front to
   -Y in Blender. Use the dmkit primitives; never hand-edit the .blend.
2. `blender -b --factory-startup --python art/tools/build_asset.py -- <asset>` builds every config x 3 LODs, bakes the
   vertex masks, exports `art/export/<asset>/SM_<Asset>_<Config>[_LODn].fbx` and the manifest, saves
   `art/<asset>/<asset>.blend`.
3. `blender -b art/<asset>/<asset>.blend --python art/tools/render_preview.py -- art/renders/blender` for a
   30-second look at all configs (Cycles, reference-sheet framing). Iterate here first.
4. `UnrealEditor-Cmd <uproject> -run=pythonscript -script=art/tools/ue_import.py` (env `DM_ASSETS=<asset>`) imports
   textures, builds the masters and instances, imports the meshes with LODs and collision.
5. Judge in `UnrealLab/AssetLab` (`renders.sh`): golden hour, storm, night at 40 m, 300 m and 1 km, beside the
   generated stand-in. Iterate until it beats the stand-in and reads like the concept at every distance.

---

## 1. What exists

| Path | What |
|---|---|
| `art/tools/dmtex.py` | numpy library for **tileable** PBR textures (periodic value noise, fbm, ridged, jittered Worley, height -> normal, cavity AO, PNG writer through Blender) |
| `art/tools/make_textures.py` | every texture set (24): limestone, limestone_fresh, rubble, oak, oak_fresh, oak_charred, thatch, thatch_sodden, fringe, rope, wool, bone, ember, monolith, plaster, paint_cobalt, reed_panel, dressed_stone, shingle, slate, turf, birch_bark, foliage, flame. Deterministic seeds; prints the mean albedo against the sheet's hex target |
| `art/tools/dmkit.py` | the modelling kit: `MB` mesh builder (keyed vertices, auto winding, metric UVs, material per face), `cbox` chamfered box, `gbox` grid box, `wall` (slab with top profile, openings, rough corners), `tube`, `lathe`, `blob` (rocks, skulls), `card`, preview materials, `join_copies` |
| `art/tools/shrine.py` | the shrine: dimensions from T-014 as constants, one function per part, 9 configs x 3 LODs, masks, collision hulls |
| `art/tools/build_asset.py` | Blender driver: build, vertex masks (AO ray-traced, fire light ray-tested), join, turn to the engine frame, UCX hulls, FBX export, manifest, .blend |
| `art/tools/render_preview.py`, `contact_sheet.py` | Cycles previews and contact sheets (iteration and review) |
| `art/tools/ue_import.py` | Unreal editor script: textures, 3 master materials, material instances per slot, meshes + LODs + collision, report |
| `art/textures/<set>/T_<Set>_BC/N/ORM.png`, `manifest.json` | generated textures (tile size in metres per set, albedo check) |
| `art/export/shrine/*.fbx`, `shrine.manifest.json` | 27 FBX (9 configs x 3 LODs) + manifest (slots, triangles, bounds, LOD files) |
| `art/shrine/shrine.blend` | every part of every config and LOD as separate objects (collections `<config>_LOD<n>`), plus the joined export copies (`export_<config>`) |
| `art/renders/blender/`, `art/renders/ue/` | preview renders and the judged AssetLab renders |
| `UnrealLab/AssetLab/` | private copy of LifeLab (module still `PlanetLab`): `AssetShow.cpp` places imported assets beside the stand-in; `StandIn/` is a read-only copy of the towns mesh factory (`standin.ps1` refreshes it); `renders.sh` is the judged render set; LifeWorld flattens and clears the showcase site; LabGameMode adds `-storm` and `-focus=asset` |

Run times on the owner's PC: textures 2 min (all 24), shrine build 25 s (27 meshes), preview of 9 configs 60 s,
Unreal import 1-6 min (6 when the master materials change and every shader recompiles), one AssetLab shot 45-90 s.

## 2. Conventions (binding for every asset)

**Names.** Asset key lowercase (`shrine`). Mesh `SM_<Asset>_<Config>` with `Config` in CamelCase
(`SM_Shrine_Intact`, `SM_Shrine_Construction`, `SM_Shrine_CultureA`); LOD files add `_LOD1`, `_LOD2`. Collision
`UCX_<Mesh>_NN`. Texture sets lowercase with underscores (`oak_charred`), files `T_<CamelSet>_<BC|N|ORM>.png`.
Material instances `MI_<CamelSet>[_<CamelStyle>]`. Unreal paths: `/Game/Assets/<Asset>/SM_*`,
`/Game/Assets/Textures/<Set>/T_*`, `/Game/Assets/Materials/M_DmAsset*`, `/Game/Assets/Materials/Instances/MI_*`.

**Scale and units.** Model in metres (Blender unit scale 1). Export uses `apply_scale_options='FBX_SCALE_ALL'`; the
importer uses `convert_scene_unit = True`; the result is centimetres in Unreal at scale 1 (verified: the shrine
imports at 602 x 529 x 457 cm above ground; its 0.8 m plinth is 80 cm). Never scale on placement to fix size.

**Axes, pivot, front.** Z up. Pivot = ground level (z = 0) at the centre of the **main footprint** (the plinth, not
the steps). In the .blend the front faces **-Y** (Blender's Front view shows the front; the shrine's window is on the
right-hand wall, +X). The exporter turns every mesh 180 deg about Z, so in Unreal the front faces **-Y**, which is
the towns factory convention (`DmTownsKit.h`: "the front (door / street side) toward -Y, the ridge or long axis
along X"). Verified in AssetLab: with `-assetyaw` the steps face the requested direction.

**Footprint and slopes.** Everything that touches the ground continues below z = 0: the shrine's plinth has a 1 m
skirt, the step block 0.3 m, standing stones 0.2 m. The towns planner sets the floor 30 % of the way from the lowest
to the highest footprint corner, so a 1 m skirt covers a 6 m footprint on a slope up to ~25 %. Bottom faces are never
modelled.

**Parts.** One part = one MB = one Blender object (named `<config>.<part>.LOD<n>`), so variants and states swap
parts (T-014: "every part is a separate mesh"). The export joins them into one static mesh per config and LOD.

**States and variants are code paths, not copies.** `build(config, lod)` is one function; each part looks at the
config (walls get a different top profile, the roof a slump function, posts a fall transform...). The footprint,
pivot, steps and standing stones are shared, so every state sits exactly on the same footprint (T-015: "every state
is made by editing the base model").

## 3. Geometry rules (dmkit)

- **Winding is automatic.** Every face is given the direction it must face (`want`); the builder flips it. This
  removes the most common bug class (see LifeLab notes).
- **Keyed vertices weld.** Grid borders, wall cells and roof slopes key their vertices by undisplaced position, and
  displacement is a pure function of that position, so neighbouring faces share vertices and stay watertight even
  when jittered. Smoothing: shade smooth + sharp edges above 35 deg (`set_sharp_from_angle`).
- **Silhouettes are irregular on purpose.** Walls: low-frequency surface noise (1.4 cm) and stronger corner noise
  (2-3.5 cm, more in ruined states); reveals are rough; broken tops are a `top(s)` profile plus noise. Thatch: 3 cm
  lumps, a rounded eave lip, a ragged bottom edge and an alpha **straw fringe** (one card strip round the eave,
  15 cm, `fringe` texture); the fringe was the single biggest step toward the concept look.
- **Hidden faces are not built** (cbox `skip`, wall caps where walls butt, no bottoms). Interiors that can be seen
  through a door are built (inner wall faces, floor, a ceiling under the roof so Lumen does not see sky through
  back faces).
- **Metric UV0.** 1 UV unit = one tile of the slot's texture set; tile sizes live in `art/textures/manifest.json`
  (limestone, rubble, thatch, plaster, dressed stone 2 m; oak, shingle, slate, reed 1 m; wool, bone 0.5 m...). Box
  projection keeps courses horizontal (v = z - z0) and wood grain along the member (`grain='x'` for a lintel).
  Roofs map v up the slope so straw runs downhill. Horizontal masonry faces use 0.55 x scale (paving slabs,
  one row per step tread). Cards (foliage, flame, fringe) use atlas UVs.

## 4. Materials and textures

**Texture sets** are tileable 512-2048 px PNGs generated in numpy from height fields: BC (sRGB), N (tangent space,
OpenGL +Y; the import flips green for Unreal), ORM (linear: R cavity AO from the height, G roughness, B metallic 0).
Albedo is designed in linear space against the sheet's hex targets; the generator prints the mean albedo and the
luma ratio to the target (shrine sets: 0.92-1.10, see the manifest).

**Slots** are named `<set>` or `<set>@<style>` (the FBX keeps the `@`). The style says what the mask channels mean:

| style | stain (G) means | growth (B) means | used by |
|---|---|---|---|
| (none) | soot / dirt: x StainMul (0.12, 0.10, 0.09) | moss, in texture cavities | intact, burnt, variants |
| `wet` | wetness: x0.42, roughness 0.22 | algae / weed band | drowned |
| `dust` | cracks: x0.22 | pale dust #b8ab96 on top faces | damaged |
| `ruin` | weathering x0.6 | moss and lichen everywhere it is wet or facing up | ruin |
| `ash` | - (no ember glow) | - | burnt altar ash, skull eye sockets |
| `antler` | Tint (0.70, 0.58, 0.45) on the bone set | - | antlers |

**Per-vertex mask** `Mask` (RGBA, linear, written per corner in Blender, exported as FBX colours; the legacy FBX
path stores the value as the byte, so the shader reads it unchanged):

- R ambient occlusion: 48 cosine-weighted rays per vertex against the joined mesh plus a ground plane (1.6 m),
  self-hits under 5 cm ignored. (Cycles' bake-to-colour-attribute returns zeros headless in 5.2.)
- G stain, B growth: the asset's `mask(config)` function of position, normal, part and slot (soot fans above the
  door and window, splash dirt at the plinth foot, a wet band 0.6 m above the drowned waterline, moss on north
  faces, cracks across the damaged walls...). Vertex density (0.25 x 0.3 m on walls) is enough because the
  material concentrates growth and stain into the texture cavities.
- A fire light: Lambert falloff from the asset's `FIRE` point (altar bowl), ray-tested against the mesh, so only
  surfaces that see the fire glow. The material adds `albedo x FireColor x A x FireStrength x lit`.

Vertex masks instead of a baked UV1 texture: they survive every LOD (each LOD is masked by the same functions), cost
no texture memory and no extra UV channel.

**Masters** (`ue_import.py`, Custom HLSL like `towns.py`):

- `M_DmAsset` (opaque, used with instanced static meshes): textures x Tint; **culture slot** (`CultureAmt` 1 on
  `wool`): keeps the cloth's woven value, takes the hue from the owner's culture colour (culture colour divided by
  its own luminance, times the wool's luminance x `CultureValue`, then scaled so no channel exceeds `CultureMax`
  0.7, so a dark saturated culture colour does not turn neon); town tint (custom data
  0..2 / 3..5) at `TintAmt` 0.2-0.3 on wall / roof sets so instances vary without losing the texture; stain and
  growth by style; state (custom data 7) darkens and desaturates; emissive = fire light (A) + texture-masked
  embers (BC alpha: `ember`, `oak_charred` cracks) scaled by custom data 6 (lit).
- `M_DmAssetFoliage` (masked 0.4, two-sided foliage, dithered LOD fade): grass, fern, birch, weed, straw fringe.
- `M_DmAssetGlow` (additive unlit, flicker): flame cards, x custom data 6.

**Per-instance custom data contract** (same layout as `UDmTownsFactory`, plus three floats):
0..2 wall tint, 3..5 roof tint, 6 lit (fire, embers), 7 state (0 intact .. 1 burnt/ruined), **8..10 culture colour
(linear RGB)**. Missing floats fall back to defaults (tints 1, lit 1, state 0, culture Umari `#e69f00`).

**Pitfalls found (cost real time):**
- A Custom node reading `Parameters.VertexColor` gets **zeros** unless a real VertexColor node is connected somewhere
  in the graph (the interpolator is not compiled otherwise). Symptom: black shadow sides (AO 0 kills Lumen GI), no
  masks, no fire light. Fixed by passing the VertexColor node's RGB into the Custom node.
- The Interchange FBX/texture path asserts in a commandlet (`SlateApplication::CurrentApplication`): set
  `task.factory` to `FbxFactory` / `TextureFactory` and `Interchange.FeatureFlags.Import.FBX 0`.
- Re-importing over an existing mesh keeps that mesh's **old** FBX import settings (a wrong unit conversion survived
  three runs): the script deletes the mesh asset first.
- `StaticMeshEditorSubsystem` is `None` in a commandlet: `unreal.new_object(unreal.StaticMeshEditorSubsystem)` works
  for `import_lod`, `set_lod_screen_sizes`, `get_convex_collision_count`. `get_lod_screen_sizes` then reads 0; the
  runtime value is correct (AssetShow logs `ScreenSize[i]`: 1.0 / 0.06 / 0.015).

## 5. LOD rules

| | screen size | triangles (shrine) | what it keeps |
|---|---|---|---|
| LOD0 | >= 0.06 (closer than ~200 m) | 4.4k-11.4k (intact 10.3k) | everything: carving, ropes, fringe, cloth, skull and antlers, embers and flame, props |
| LOD1 | 0.015-0.06 (~200-830 m) | 1.0k-2.3k (intact 2.0k) | massing with 0.8 m wall cells, roof 0.5 m, rolls, finials, posts, lintel, stones; no ropes, fringe, carving, flame |
| LOD2 | < 0.015 (beyond ~830 m) | 156-458 (intact 222) | plinth, sloped step block, walls with a door notch (reads dark), hip roof with thick eave, finials, standing stones (T-014: "only the plinth, the roof silhouette, the finials and the standing stones need to survive") |

LODs are **built, not decimated**: the same functions with coarser spacing and fewer parts, so silhouettes stay
deliberate and masks are recomputed per LOD. Screen sizes are set by the import (`SCREEN` in `ue_import.py`), sized
for a ~9 m bounds sphere at 40 deg FOV; scale them with the asset's size class. Nanite is off (towns and LifeLab
measured conventional LODs faster on the RTX 3060).

## 6. Collision

UCX convex hulls exported with LOD0 (`collision()` in the asset module): plinth + steps (one hull, convex because
the steps only add a sloped front), walls (cella box), roof (hip hull). `one_convex_hull_per_ucx`,
`CTF_USE_SIMPLE_AS_COMPLEX`. Import log: `hulls 3` for every shrine config. The towns module currently disables
collision on its instances; the hulls are there for picking and units.

## 7. Unreal import

`UnrealEditor-Cmd.exe <project> -run=pythonscript -script="<root>/art/tools/ue_import.py" -unattended -nosplash`
(`DM_ASSETS=shrine` limits the assets, `DM_ONLY_MATS=1` stops after materials). It reads every
`art/export/<asset>/<asset>.manifest.json`, imports the texture sets the slots need, rebuilds the three masters,
creates one MI per slot (`SET_PARAMS` per texture set, `STYLE_PARAMS` per style), imports LOD0 with UCX, adds LOD1/2
with `import_lod`, sets screen sizes, assigns MIs by slot name, disables Nanite and logs:

```
DMASSET mesh SM_Shrine_Intact lods 3 tris [10346, 1970, 222] hulls 3 slots ['limestone', 'monolith', 'rubble', 'oak',
'thatch', 'fringe', 'rope', 'bone', 'ember@ash', 'bone@antler', 'wool', 'ember', 'flame'] extent (301 264 279) cm
```

FBX settings that matter: legacy `FbxFactory`, `convert_scene=True`, `convert_scene_unit=True`,
`import_uniform_scale=1`, `normal_import_method=IMPORT_NORMALS` (smoothing from Blender), `vertex_color_import_option
=REPLACE`, `generate_lightmap_uvs=False`, `combine_meshes=True`, `reorder_material_to_fbx_order=True`.

The script has only been run against `UnrealLab/AssetLab`. To bring the shrine into the game, the integration owner
runs the same command against `Unreal/Demiurge.uproject` (it only writes under `/Game/Assets`).

## 8. The towns factory hook: exactly how an asset drops in

The towns module (`Unreal/Source/Demiurge/Game/Towns`, not edited by this track) already funnels every visual
through `UDmTownsFactory` (`DmTownsKit.h/.cpp`): the planner asks `Kit.Get(EDmTownMesh, variant)` for footprints
(`FDmKitInfo::W, D, H`) and `ADmTowns::BuildComponents` creates one `UInstancedStaticMeshComponent` per (mesh,
variant) with `SetStaticMesh(Info.Mesh)` and 8 custom floats per instance. The component does **not** override
materials, so an imported mesh renders with its own MIs. The shrine is `EDmTownMesh::Shrine`, variant
`Age <= 2 ? 0 : 1` (`DmTownsPlan.cpp`, `PlanCivic`), placed facing the plaza with its front -Y. Proposed change
(owner of Game/Towns applies it):

1. **Load instead of generate** at the end of `UDmTownsFactory::Build()`:

```cpp
// real assets (art/ pipeline): replace the generated stand-in where an imported mesh exists
auto Real = [&](EDmTownMesh Id, int32 V, const TCHAR* Name, float W, float D, float H, bool bLit)
{
    const FString Path = FString::Printf(TEXT("/Game/Assets/Shrine/%s.%s"), Name, Name);
    if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *Path))
    {
        FDmKitInfo& I = Kit[int32(Id)][V];
        I.Mesh = M; I.W = W; I.D = D; I.H = H; I.bLit = bLit;
        I.WallPat = EDmTownPat::Stone; I.RoofPat = EDmTownPat::Thatch;
        I.Tris = M->GetRenderData() ? M->GetRenderData()->LODResources[0].GetNumTriangles() : 0;
        Meshes.Add(M);
    }
};
Real(EDmTownMesh::Shrine, 0, TEXT("SM_Shrine_Intact"), 6.0f, 5.3f, 4.6f, true);
```

   Footprint for the planner: 6.0 x 5.3 m (plinth + steps; the pivot is the plinth centre, the steps add 0.9 m at
   -Y), height 4.6 m. `bLit = true` so the planner gives it a lit value (custom data 6) and the altar fire burns;
   with `false` the planner writes 0 and the fire, embers and flame go dark (correct for burnt, ruin, drowned).

2. **Culture colour**: raise `UDmTownsFactory::NumCustomData` to 11 and write `In.Color` (the owner culture colour,
   `FDmTownInput::Color`) into floats 8..10 in `FPlanner::EmitInstances` (and in the units path, which already uses
   the colour for floats 3..5). Without it the cloth uses the default Umari saffron.

3. **Culture variants**: add the three meshes as variants of `EDmTownMesh::Shrine` (bump `X(Shrine, 2)` to
   `X(Shrine, 4)`: 0 base/Bronze, 1 later ages, 2..4 or a culture-style table) and choose by culture style, e.g.
   `CultureA` (Cliff-Terrace, arid/terraced), `CultureB` (Lagoon-Weaver, tropical coast), `CultureC` (Ring-Builder,
   cold/temperate inland). All share the plinth footprint, so the planner footprint does not change (variant B's
   horns widen the roof to 8 m but stay above the plaza).

4. **States**: each is a whole mesh on the same footprint and pivot, so swapping is a mesh change on the same
   instance transform:

| state mesh | when (T-015 / SPEC-SIM 3.10) | towns data that exists today | custom data |
|---|---|---|---|
| `SM_Shrine_Construction` | `b.shrine` just set (first ~3 years) | `ADmTowns::Building` + `ConstructionSeconds` (today it shows `EDmTownMesh::Scaffold` scaled to the footprint): use this mesh instead for shrines | lit 0 |
| `SM_Shrine_Damaged` | quake in range, battle | needs a per-settlement "recent quake / battle" flag from the sim events | lit as intact |
| `SM_Shrine_Burnt` | lightning fire, war capture, volcano | needs "burnt" from fire / war events | state 0.3-1; lit > 0 the first year shows embers in the char cracks |
| `SM_Shrine_Ruin` | dead > 100 years, silence ending | `In.bDead` and `In.Ruin` | state 0..0.55 as the planner sets for dead towns |
| `SM_Shrine_Drowned` | cell below sea level by 0-1.5 units | cell elevation vs `seaLevel` | lit 0; the game's water must sit 1.8 m above the instance floor |

5. **Lights and materials**: nothing to wire. The asset materials do not need `SetFrame` / `NightOverride` (the
   fire is physically emissive, so it is invisible by day and glows at night through the exposure change). Cull
   distance comes from `CullM(Info)` (footprint under 14 m -> 26 km), which the 222-triangle LOD2 affords.

**Observation for the towns owner (not acted on):** in AssetLab the copied factory's generated shrine reports
`ScreenSize` 2.0 / 0.75 / 0.5625 at runtime (AssetShow reads `GetRenderData()->ScreenSize[i].Default` after
`Build()`), not the 1 / 0.006 / 0.0018 that `UDmTownsFactory::Build` passes to `MakeStaticMesh`; the imported mesh
reads its own values correctly. If the game shows the same, generated buildings switch LODs far earlier than
intended. Worth a check with the same log line in `ADmTowns`.

AssetLab proves this path: `AssetShow.cpp` places each mesh exactly like `BuildComponents` (one ISM component, no
material overrides, 11 custom floats, the stand-in from the copied factory beside it).

## 9. AssetLab

`UnrealLab/AssetLab` = LifeLab (real-scale curved planet patch at site 38500, LifeLab trees and villages,
orchestrator sky) + the showcase:

```
powershell -NoProfile -ExecutionPolicy Bypass -File UnrealLab/AssetLab/build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File UnrealLab/AssetLab/shot.ps1 <name> -assets=SM_Shrine_Intact,standin -focus=asset -view=40,215,24 -sun=290,6
bash UnrealLab/AssetLab/renders.sh [filter]        # the judged set -> art/renders/ue/<name>.png
```

| arg | meaning |
|---|---|
| `-assets=a,b,...` | meshes in a row (names under `/Game/Assets/<Asset>/`, or `standin`/`standin1` = towns factory shrine variant 0/1) |
| `-assetat=x,y` `-assetgap=m` `-assetyaw=deg` | site (default: the open green beside the first village's crossroads), spacing (13 m), front direction (250) |
| `-culture=r,g,b` `-assetlit=f` `-assetstate=f` | custom data 8..10, 6, 7 |
| `-water=h` | translucent water plane h m above the site floor (drowned) |
| `-focus=asset` / `asset<i>` | camera target: the row centre / one mesh |
| `-storm` | low dense storm clouds (engine cloud material: coverage 0.85, density 1.6, StormClouds 1), cool dim sun, sky 1.5 |
| golden / night | `-sun=290,6 -ev=1 -haze=0.8` / `-night -sun=290,-25` (moonlight, +3.5 EV) |

The site is flattened (a LifeWorld pad), cleared of trees, hedges and shrubs and stamped as beaten-earth yard
before the terrain is built. Materials needed: `ue_import.py` (assets), `Scripts/make_assetlab_materials.py`
(`M_LabWater`), and the towns materials from `Unreal/Scripts/towns.py` run against the AssetLab project (for the
stand-in).

## 10. Results

All numbers measured on the owner's RTX 3060 12 GB, UE 5.7, 1920x1080, Lumen + VSM + TSR (AssetLab = LifeLab's
renderer settings unchanged).

**Meshes** (`art/export/shrine/shrine.manifest.json`; bounds above ground, the 1 m skirt not included):

| mesh | tris LOD0 / LOD1 / LOD2 | slots | size (m) |
|---|---|---|---|
| `SM_Shrine_Intact` | 10346 / 1970 / 222 | 13 | 6.02 x 5.29 x 4.57 |
| `SM_Shrine_Construction` | 6180 / 1680 / 458 | 7 | 7.00 x 5.29 x 4.55 (scaffold and ladder outside the plinth) |
| `SM_Shrine_Damaged` | 11384 / 2274 / 202 | 13 | 6.97 x 5.29 x 4.57 (rubble spills off the plinth) |
| `SM_Shrine_Burnt` | 4960 / 1026 / 156 | 6 | 6.02 x 5.29 x 3.76 |
| `SM_Shrine_Ruin` | 4432 / 1047 / 168 | 6 | 7.14 x 5.93 x 5.85 (the birch) |
| `SM_Shrine_Drowned` | 10068 / 1830 / 222 | 10 | 8.14 x 7.13 x 4.57 (floating debris at +1.8 m) |
| `SM_Shrine_CultureA` | 5534 / 1249 / 255 | 10 | 6.01 x 5.28 x 4.86 |
| `SM_Shrine_CultureB` | 7416 / 2062 / 232 | 11 | 8.01 x 5.29 x 5.66 |
| `SM_Shrine_CultureC` | 5334 / 1108 / 185 | 11 | 6.02 x 5.53 x 5.06 |

Unreal confirms the same triangles per LOD, 3 convex hulls each, runtime screen sizes 1.0 / 0.06 / 0.015 and
602 x 529 cm footprint (AssetShow log). Every config shares the plinth footprint and pivot (the T-014/T-015 rule).
Intact LOD0 is 3 % over the 10k target (the straw fringe, 480 tris, is worth it). 13 slots = 13 draw sections at
LOD0: fine for one shrine per town; houses (hundreds of instances) should stay at 4-6 slots.

**Textures:** 24 sets, 81 MB of PNG on disk; albedo within 0.92-1.10 of the sheet's targets for the main sets
(limestone 0.92, rubble 1.01, oak 1.10, thatch 0.94, wool 1.01, plaster 0.98, dressed stone 0.98, slate 0.98).

**Frame cost:** whole AssetLab scene (terrain rings, ~850k instanced trees, three settlements, sky and clouds) with
the showcase: GPU 6.2-7.3 ms at 40 m-1 km and 9.3 ms with five states in view; the shrine's own cost is inside
the run-to-run noise (+-1 ms).

**Renders** (`art/renders/ue/`):

| file | what |
|---|---|
| `contact-distances.png` | rows 40 m / 300 m / 1 km (the 300 m and 1 km frames cropped around the pair), columns golden hour / storm / night; real shrine right, generated stand-in left |
| `contact-closeups.png` | 40 m, hero framing (FOV 15-18): intact, construction, damaged, burnt, ruin, drowned, culture A, B (blue culture colour), C (red culture colour) |
| `hero40-golden.png` | the art target's framing (40 m, 25 deg up, three-quarter front) |
| `cmp40/300/1k-golden/storm/night.png` | the nine distance x weather shots, full frame |
| `states-golden.png`, `states-night.png`, `variants-golden.png`, `drowned-golden.png` | line-ups and the drowned state in water |
| `../blender/contact-states-variants.png`, `contact-lods.png` | Cycles previews: all nine configs; LOD0 / LOD1 / LOD2 |

**Art direction, against `shrine-hero.png`.** It reads as the same building: the stepped limestone plinth, central
steps between two menhirs, rubble cella with oak corner posts and caps, the carved zigzag lintel on two door posts
with saffron cloth, the deer skull and antlers standing in front of the thatch, the hipped reed roof with rope bands,
ridge roll and two turned finials, and the altar fire visible through the door. The thatch (combed straw texture,
ragged lip, alpha straw fringe) is the strongest part. At night the fire lights the doorway, threshold and steps
(baked fire-light mask), which the stand-in cannot do; at 300 m it still reads as "the shrine" (pale plinth,
thatch, the two stones, a glowing door at night); at 1 km LOD2 keeps the plinth, roof, finials and stones as the
sheet asks. Next to the generated stand-in (a white hip-roofed box, 110 triangles) it is a clear step up.
What still misses the target (next iteration, in order of visibility):
1. The masonry is too clean and regular: the hero's rubble is rougher and greyer, the plinth blocks weathered; add
   a second, larger-scale stone variation layer and stronger splash dirt.
2. The skull reads as a white blob at 40 m and the cloth as flat bright cards; darken and model the skull more,
   give the cloth folds and a weave-darkened tone.
3. The menhirs are too smooth and symmetric for monoliths.
4. Ground contact: the hero has grass tufts, scattered stones and a worn path at the plinth foot; the asset has none
   (that belongs either to the asset as optional "skirt" props or to the towns ground overlay).
5. Storm is only an overcast in the lab (no rain, no wet surfaces); a global wetness parameter for the asset master
   (roughness down, albedo darker) would let the storm read.
The lab's grading is cooler and flatter than the concept's warm haze; that is the sky module's look, not the asset.

## 11. Deviations from the reference sheet (the numbers that cannot all be true)

- **Roof pitch.** The sheet asks for 47 deg on all four slopes **and** a 2.6 m ridge 1.4 m above the eave with
  0.3/0.4 m overhangs on a 4.8 x 3.2 cella. A hip roof cannot have both (equal 47 deg slopes over a 3.9 m deep
  eave put the ridge 2.1 m up). Measured off `shrine-front.png` the eave top is 3.06 m, the ridge 4.28 m: modelled
  as eave top 3.08 m, eave bottom 2.78 m (the carved lintel shows below it, as in the views), ridge top 4.25 m, so
  the hips are 42 deg and the long slopes 33 deg; total height 4.6 m.
- **Variant C drum.** A 5.0 m drum cannot stand on the 4.4 m deep plinth; the drum is 4.3 m (r 2.15, centred) with
  the straight door frame, lintel and antlers set into its front.
- **Variant C cone.** 55 deg over the drum would put the apex at 6.3 m and break the hard "overall height within
  +-0.5 m" rule; the cone is 40 deg, apex 4.8 m, ring finial to 5.06 m.
- **Variant B horns** rise to 5.66 m: the sheet asks for "about 5.5 m", which is itself outside the +-0.5 m rule;
  kept as the sheet describes the culture.
- **Damaged:** the optional fallen finial and leaning standing stone were left out (the T-015 text keeps the stones
  upright).

## 12. Checklist for the next asset

1. Reference sheet with real dimensions, a part list and states; resolve contradictions first and write them down.
2. `art/tools/<asset>.py`: constants from the sheet; one function per part; `lod` and `config` handled inside each
   part; keyed vertices for anything displaced; skirts below z = 0; no bottoms.
3. Reuse texture sets; add a set only for a new material (generator in `make_textures.py`, check the printed albedo
   against the target). Slots `<set>[@style]`; add a style only for a new kind of weathering.
4. `mask(config)` for stain and growth; `FIRE`/`has_fire` if it has a fire or lamp.
5. Budgets: LOD0 ~6-10k for a hero building, 1-3k for houses; LOD1 ~20 %; LOD2 ~300; collision 1-4 hulls.
6. Preview all configs in Blender; then import and judge in AssetLab at 40 m, 300 m, 1 km in golden hour, storm and
   night beside the stand-in. Look at shadow sides (AO / GI), night (emissive), silhouette at 1 km.
7. Write the factory hook for its `EDmTownMesh` id (footprint W/D/H, bLit, variants) and hand it to the towns owner.

## 13. Independent verification (2026-11-19)

Re-run from scratch by a second agent (did not build it); everything below was measured, not copied.

**Reproducible.** `art/tools` copied to a scratch root and run there: `make_textures.py` (4.3 min, GPU shared with
other agents) regenerates 70 PNGs, 68 byte-identical; `build_asset.py -- shrine` (21 s) gives 27 FBX whose
geometry, UVs and masks are identical after re-import (hash over every vertex and UV), and an identical manifest
(the FBX bytes differ only in embedded paths and timestamps). `render_preview.py` runs (2.5 min). `ue_import.py`
against AssetLab: 3 min, "import finished without errors", same triangles, 3 hulls each, steps at -Y (bounds
origin y = -44 cm); AssetShow reports LOD screen sizes 1.0 / 0.06 / 0.015 at runtime.

**Checked on all 27 FBX** (re-imported into Blender): unit scale 1; pivot at the plinth centre, z = 0, 1 m skirt;
3 convex UCX hulls on every LOD0; a `Mask` colour on every mesh; no degenerate faces. Six-view renders with
back-face culling (what a one-sided opaque material shows) find no holes or inverted parts in any config,
including from below the eaves and behind (the variant B saddle roof has a closed underside). Small leftovers, all
without visible effect: 12 loose vertices in Damaged LOD0 (torn roof; Unreal drops unreferenced vertices),
5-7-sided n-gons on the broken wall tops of Damaged and Ruin (Unreal triangulates them), hidden faces under the
walls and under the variant C lintel rings (~200 triangles). UVs peak at 58.6 (rope V) and 38.7 (fringe U): below
the age0 problem (343) but inside half-float steps of 1/32 tile, invisible on rope and straw.

**AssetLab, new views** (`art/renders/ue/verify-*.png`): back, side (window on the right as seen from the front,
as specified), eye level at 13 m, `-assetyaw=0` (steps face the requested direction), storm close-up, night with
`-assetlit=0` (fire, embers and flame go dark), daylight with `-assetlit=0` (identical pixels: the fire emissive
does not leak into daylight colours; under the storm overcast the fire light on the threshold is visible, which
reads as intended), culture yellow and vermilion.

**Fixes (verifier):**
1. `art/textures/paint_cobalt/T_PaintCobalt_BC.png` and `thatch_sodden/T_ThatchSodden_BC.png` were stale (made
   before a later generator edit; up to 28/255 off). Replaced by the current generator's output; manifest albedo
   line for `thatch_sodden` updated. Every texture now matches its generator byte for byte.
2. Culture colour in `M_DmAsset` (`ue_import.py`): the cloth was `Culture x lum / 0.40`, so its brightness followed
   the culture colour's own luminance (yellow culture 1.9x the wool's value, a 0.6-albedo glare; vermilion 0.55x),
   contrary to "keeps the woven value". Now the culture colour is divided by its own luminance and capped at
   `CultureMax` 0.7 per channel. Default Umari orange is 3 % darker (rendered cloth 249,214,50 -> 248,212,48 sRGB); materials re-imported, culture
   close-ups re-rendered.
3. Comments: `build_asset.py` said AO is "baked by Cycles" (it is ray-traced); `ue_import.py` said meshes are
   re-imported in place (they are deleted and re-created at the same path, so reference them by path).
4. `contact-closeups.png` regenerated:
   `blender -b --factory-startup --python art/tools/contact_sheet.py -- art/renders/ue/contact-closeups.png 3 620
   <hero40-golden, close-construction, close-damaged, close-burnt, close-ruin, drowned-golden, close-culture-a/b/c>`.

**Open (not fixed here):**
- At the game's high camera (40 m, 25 deg) the carved zigzag lintel, a signature part, is hidden by the front
  eave lip and the straw fringe (visible only from eye level); the concept shows it clearly. Options: a raised
  "eyebrow" in the front eave over the door, or a shorter fringe on the front slope.
- The real shrine (6.0 x 5.3 x 4.6 m, as the sheet specifies) is smaller than every LifeLab house beside it and
  smaller than the generated stand-in (10 x 12.6 m footprint with skirt); in the concept it is the largest building
  of its village. At 300 m it reads by its pale plinth and lit door, not by size. The towns owner should check it
  against the game's Bronze-age house sizes before swapping.
- The hook snippet gives every variant `WallPat = Stone`, `RoofPat = Thatch`; for variants A/B/C set them per
  variant (Plaster / Wood / Stone walls; roof tints Plaster / Slate) or the planner's thatch tint lands on slate.
- Frame cost could not be re-measured: other agents' Unreal runs shared the GPU (7-55 ms per frame in these runs).
- The `states-*` row is wider than the cleared 24 m showcase site: the Ruin overlaps a LifeLab house (lab only).
