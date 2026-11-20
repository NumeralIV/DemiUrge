# Tree families as real assets — TreeLab reference (Blender-built trees, LODs, impostors, wind)

Lab: `UnrealLab/TreeLab` (a private copy of `UnrealLab/LifeLab`; module still `PlanetLab`). Asset sources: `art/trees/`.
Screenshots: `shots/ue-treelab-*.png`, contact sheets `shots/ue-treelab-sheet-*.jpg`.
Everything was measured on the RTX 3060 12 GB at 1920x1080, UE 5.7, Lumen + VSM + TSR, LifeLab's renderer settings.

**Short version for the C4 nature engineer:**

1. Five species x four variants, real-world scale, built procedurally and reproducibly in Blender 5.2 from
   `art/trees/build_trees.py` (one command, ~2 minutes, no inputs, no downloads): temperate broadleaf (oak / beech),
   conifer (spruce / fir), coconut palm, umbrella-thorn acacia, shrub. Every mesh has **LOD0-2 geometry plus a LOD3
   impostor** (three cards, 12 triangles), two-sided leaf cards with alpha and a shared volumetric normal, AO in vertex
   alpha, and three wind layers (trunk sway, per-limb branch bend with its own phase, leaf flutter).
2. Import with `UnrealLab/TreeLab/import.ps1` (`Scripts/import_trees.py`): FBX + PNG into `/Game/Trees`, with the
   **legacy FBX/PNG importers** (Interchange crashes headless, §5). 20 meshes, 12 textures, 3 materials in ~5 minutes.
   **The FBX importer flips V on every UV channel**: the exporter pre-flips the wind and impostor channels (§5).
3. Swap them into the game in `UDmNatureFactory::Build()` (load instead of generate), one generic line in
   `ADmNature::MakeComps` (per-slot materials) and two constants in `DmNatureScatter.cpp` (density, scale jitter):
   §9 has the exact code. Keep the generated **Dead** tree, grass and wildlife.
4. **Halve the closed-canopy density** when the real-scale assets go in (`P = Can * 0.62 * 0.5 ...`): crowns are
   12-18 m wide, not 7-10 m like the stand-ins; at the stand-ins' 5.7 m spacing the forest turns into columns (§7).
   Instance scale jitter 0.85-1.12 (not 0.72-1.22): the four variants already cover the size range.
5. The impostor is a **yaw-only camera-facing card** (`M_TreeImpostor`, WPO) with **baked** volumetric normals
   (`T_TreeImpostorsN`, added by the verification, §12: without them LOD3 drew a hard band against LOD2 at golden hour);
   keep WPO enabled at all distances for trees (C4 already does: `WpoDistM = 0`). Crossed static cards made far
   forests look like columns of stripes at 300 m (§6).
6. Cost: the asset forests are **cheaper than the generated stand-ins** at the same cameras: GPU 6.6-18 ms against
   8.4-29.5 ms (§8), with 82k-735k instances per 8 x 8 km patch. LOD0 is only used above 0.45 screen size, everything
   below 0.08 is a 12-triangle impostor, and the halved stand density does the rest.

---

## 1. What is where

| path | contents |
|---|---|
| `art/trees/build_trees.py` | the only entry point (stages `bark,tiles,trees,atlas`) |
| `art/trees/treelib/species.py` | growth of the five species (skeleton, leaf cards, fronds), shading fields, LOD 0-2 meshing |
| `art/trees/treelib/tiles.py` | the leaf-card models (twig sprays, needle sprays, compound leaves, palm fronds) and the leaf atlas bake |
| `art/trees/treelib/bark.py` | tileable bark albedo + DirectX normal maps painted with numpy (FFT noise, Worley plates) |
| `art/trees/treelib/export.py` | per-species build, impostor bake and LOD3, FBX export, `.blend` save, impostor atlas, manifest |
| `art/trees/treelib/mesh.py`, `render.py`, `imaging.py` | mesh builder + vertex contract, EEVEE bake helpers, numpy image tools (dilation, premultiplied downsample) |
| `art/trees/source/<species>.blend` | editable sources: every variant and LOD laid out, bake materials bound to the textures |
| `art/trees/export/<Species>/SM_Tree_<Species>_<v>.fbx`, `..._LOD1..3.fbx` | the exports (one FBX per LOD) |
| `art/trees/export/manifest.json` | heights, crown radii, bounds, triangles per LOD and section, impostor cells, atlas rects, screen sizes |
| `art/trees/textures/` | `T_TreeLeafAtlas.png` (2048², sRGB + alpha), `T_TreeImpostors.png` (2048², sRGB + alpha), `T_TreeImpostorsN.png` (2048², linear: baked impostor normals, §4), `T_Bark_<Species>.png` / `_N.png` (1024²) |
| `art/trees/build/` | regenerated intermediates (raw renders, per-view impostor bakes, review sheets); ignored |
| `UnrealLab/TreeLab/import.ps1`, `Scripts/import_trees.py` | Unreal import + materials (`/Game/Trees/...`) |
| `UnrealLab/TreeLab/matrix.sh`, `Scripts/contact_sheet.py`, `uvcheck.ps1` (`Scripts/inspect_uvs.py`) | biome shot matrix, review sheets, UV-contract check of imported meshes |
| `UnrealLab/TreeLab/Source/PlanetLab/LifeWorld.cpp` | `BuildTrees()` loads the assets (default) or the stand-ins (`-gentrees`) |

Provenance and licence: **everything is original procedural work** made for DEMIURGE by the scripts above (geometry,
leaf and needle models, bark painting, impostor bakes). No third-party models, textures, scans or reference images
were used, so there is nothing to attribute; the owner can treat the outputs like any original artwork (CC0-style,
no restrictions). Blender's GPL does not apply to its output (see `docs/research/assets.md`). Spend: $0.

## 2. Regenerating

```
"C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python art/trees/build_trees.py
        [-- --stages bark,tiles,trees,atlas --species broadleaf,conifer,palm,acacia,shrub --variants 0,1,2,3]
powershell -NoProfile -ExecutionPolicy Bypass -File UnrealLab/TreeLab/import.ps1        # env TREES_ONLY=palm,acacia to re-import a subset
powershell -NoProfile -ExecutionPolicy Bypass -File UnrealLab/TreeLab/build.ps1         # only if LifeWorld/LabGameMode changed
bash UnrealLab/TreeLab/matrix.sh '<regex on biome-alt-light>' [extra args]               # shots + timings.txt
```

- Stages: `bark` (5 s) → `tiles` (leaf atlas, 40 s) → `trees` (meshes, impostor bakes, FBX, `.blend`, manifest, 60 s)
  → `atlas` (packs the impostor views of every variant on disk, 5 s). `trees` needs the leaf atlas and the bark
  PNGs; `atlas` must run after any `trees` run. Total ~110 s.
- Deterministic: every random draw comes from `numpy.random.default_rng(seed)` with seeds derived from species and
  variant (`species.build`: `1000 + 97*species + 13*variant`). Re-running gives byte-identical PNGs and manifest; the
  FBX files differ only in their header timestamp (63 bytes of 143 KB, checked on `SM_Tree_Palm_0`).
- To make a new variant: add a row to the species table (`BROADLEAF`, `CONIFER`, `PALM`, `ACACIA`, `SHRUB` in
  `species.py`), raise `NUM_VARIANTS` (the impostor atlas has room for 64 views = 21 variants) and the Unreal
  side picks it up from the manifest.
- TreeLab arguments added on top of LifeLab's (§1 of `vegetation-and-towns.md`):

  | arg | meaning |
  |---|---|
  | (default) / `-gentrees` | Blender assets from `/Game/Trees` / LifeLab's generated stand-ins (A/B comparisons) |
  | `-canopydensity=0.5` | closed-canopy density factor for the asset trees (§7) |
  | `-lodsizes=1,0.45,0.2,0.08` | override the LOD screen sizes at runtime (tuning) |
  | `-treewpo=<m>` | WPO disable distance for tree HISMs (default 0 = never for assets, 1500 for stand-ins) |
  | `-wind= -treebright= -leaftrans=` | wind strength (x100 cm), leaf/impostor/bark brightness, transmission (dynamic instances) |
  | `-impbright= -imptrans=` | impostor-only brightness / transmission override (verification, for matching LOD3 to LOD2) |
  | `-shotlist=<file>` `-shotwarm=150` | several shots from one world build: lines `name camx,camy,alt,yaw,pitch sunyaw,sunpitch ev clouds` |
  | `-findsites` | logs candidate planet cells per biome (boreal, temperate, rainforest, savanna) and exits |

  `matrix.sh '<biome regex>' '<alt-light regex>' [args]` writes one shot list per biome
  (`Saved/shotlists/`), runs one editor per biome and appends `name | trees N | frame/game/render/rhi/gpu` lines
  to `UnrealLab/TreeLab/timings.txt`. Sites: temperate 38500, boreal 3753, rainforest 5299, savanna 21445, palm
  coast 5299 (palm-densest cell). Cameras look at the densest cell of the biome's tree kind; day = sun 48° behind-left,
  golden hour = sun 6° from the left with +1.4 EV; the 5 km shots hide the cloud layer (it sits at 1.6-3.8 km).
- Look at `art/trees/build/impostor/review_sheet.png` (every variant from both sides and the top) and
  `art/trees/build/tiles/preview_atlas_clip.png` (the leaf atlas as the 0.4 alpha clip sees it) after a build.

## 3. The five species

All numbers from `manifest.json` (height = top of the crown above the trunk base; crown radius = half the larger
horizontal extent; LOD triangles include both faces of every card). Broadleaf rows updated by the verification
(§12: LOD1 without level-2 branches). Unreal reports ~180 fewer triangles on palm LOD0-2 (2,224 vs 2,408 on
`SM_Tree_Palm_0`): the zero-width frond tips are degenerate and the build drops them even with remove-degenerates off.

| mesh | what | height m | crown r m | LOD0 / LOD1 / LOD2 / LOD3 tris | cards |
|---|---|---|---|---|---|
| SM_Tree_Broadleaf_0 | mature oak, broad domed crown | 18.1 | 7.8 | 11,485 / 3,511 / 1,214 / 12 | 861 |
| SM_Tree_Broadleaf_1 | tall beech, leader to the top | 24.6 | 8.0 | 20,601 / 6,497 / 2,148 / 12 | 1,521 |
| SM_Tree_Broadleaf_2 | young tree | 12.1 | 4.6 | 5,100 / 1,648 / 539 / 12 | 315 |
| SM_Tree_Broadleaf_3 | old spreading oak | 16.6 | 8.6 | 14,492 / 4,330 / 1,465 / 12 | 1,086 |
| SM_Tree_Conifer_0 | mature spruce | 22.1 | 4.3 | 21,088 / 9,420 / 2,116 / 12 | 1,554 |
| SM_Tree_Conifer_1 | old tall spruce, dead lower whorls | 28.1 | 4.8 | 19,632 / 8,868 / 1,976 / 12 | 1,353 |
| SM_Tree_Conifer_2 | young spruce, full to the ground | 11.1 | 2.9 | 13,236 / 6,008 / 1,514 / 12 | 1,061 |
| SM_Tree_Conifer_3 | narrow fir spire | 17.1 | 3.0 | 13,472 / 6,262 / 1,488 / 12 | 986 |
| SM_Tree_Palm_0 | coconut palm, gentle lean | 16.5 | 4.0 | 2,405 / 1,572 / 734 / 12 | 23 fronds |
| SM_Tree_Palm_1 | young palm, heavy crown | 10.8 | 3.8 | 2,589 / 1,684 / 862 / 12 | 26 fronds |
| SM_Tree_Palm_2 | tall leaning palm | 21.4 | 4.3 | 2,133 / 1,348 / 702 / 12 | 21 fronds |
| SM_Tree_Palm_3 | coastal palm, strong curve | 13.9 | 3.7 | 2,301 / 1,492 / 766 / 12 | 22 fronds |
| SM_Tree_Acacia_0 | umbrella thorn, wide flat crown | 8.5 | 6.4 | 9,380 / 4,001 / 1,155 / 12 | 945 |
| SM_Tree_Acacia_1 | tall two-tiered acacia | 10.7 | 6.6 | 8,636 / 3,641 / 1,057 / 12 | 847 |
| SM_Tree_Acacia_2 | young acacia | 6.3 | 4.7 | 5,065 / 2,282 / 632 / 12 | 515 |
| SM_Tree_Acacia_3 | multi-stemmed, lopsided | 6.9 | 6.4 | 6,917 / 2,973 / 841 / 12 | 681 |
| SM_Tree_Shrub_0 | round bush | 2.0 | 1.6 | 3,015 / 989 / 373 / 12 | 247 |
| SM_Tree_Shrub_1 | low spreading bush | 1.6 | 1.9 | 3,309 / 1,071 / 429 / 12 | 275 |
| SM_Tree_Shrub_2 | tall upright shrub | 2.8 | 1.4 | 2,749 / 927 / 363 / 12 | 223 |
| SM_Tree_Shrub_3 | small bush | 1.2 | 1.0 | 1,151 / 349 / 105 / 12 | 91 |

How they grow (`species.py`):

- **Broadleaf**: trunk with root flare and 5-lobed buttresses; *decurrent* variants (oaks) split at the bole into 4-5
  limbs aimed at points inside a lumpy ellipsoid crown, *excurrent* ones (beech, young) keep a leader with
  phyllotactic (137.5°) limbs. Level-2 branches and level-3 twigs are clipped to the crown envelope (ray to the
  lumpy ellipsoid), so twig tips fill the crown and cluster at the branch ends: every level-2 branch is a **clump**
  (the "cauliflower" unit) whose centre drives part of the card AO and normal. Cards: 1.2 m spray tiles (five
  leafy twigs, true leaf size 8-12 cm), oak (lobed) or beech (ovate) leaves, lighter young foliage on the outer shell.
- **Conifer**: straight trunk to the top; whorls every ~0.5 m of 4-6 branches plus interwhorl shoots; branch length
  follows the cone `Rb * ((H - z)/(H - cb))^0.95`; spruce branches sag and turn up at the tip, fir branches stay
  level. Cards are flat needle sprays every 0.34 m along each branch (rolled ±20°) plus hanging side sprays every
  second step; two crossed leader cards on the (leaning) trunk top; dead bare stubs below the live crown on the old tree.
- **Palm**: ringed trunk that leans `lean * f^1.8` and turns back up near the top, swollen base, frond-sheath bulb,
  18-24 fronds in a 137.5° spiral (young ones steep, old ones drooping 95°), each a **V-folded strip** (28-42°) along
  a drooping rachis with the frond tile, 2-3 dead brown fronds hanging, 6-11 coconuts.
- **Acacia**: short trunk forking into 2-3 zig-zag limbs (or two stems from the ground), level-2 branches running
  out almost horizontally to the rim of a flat ellipsoid crown, short upward twigs carrying **flat clumps** of 6-9
  near-horizontal bipinnate-leaf cards: a flat-topped umbrella, two tiers on the tall variant.
- **Shrub**: 6-10 stems fanning from the ground, side shoots clipped to a squat ellipsoid, dense 0.42 m small-leaf cards.

Bark: 1024² tileable albedo + normal per species (fissured ridges with lichen for broadleaf/shrub, scaly plates for
spruce, leaf-scar rings for the palm, dark deeply fissured for acacia). Bark UVs are in tile units (u around with an
integer number of tiles per stem, v along), so the materials need no tiling parameters.

## 4. Vertex data, sections, textures (the contract)

| attribute | meaning |
|---|---|
| position | metres in Blender / FBX, centimetres in Unreal; trunk base at the origin, ground at z = 0, roots reach z = -0.4 m (sink instances 0.2 m on slopes, as LifeLab does) |
| normal | explicit; leaf cards carry a **shared volumetric normal** (crown gradient + clump direction + a little face normal), emitted twice back to back so the leaf material is *not* two-sided |
| UV0 | bark: tile units; leaves and impostor: atlas space |
| UV1 | x = trunk sway, metres per unit wind (`0.010-0.02 * H * (z/H)^2`); y = leaf flutter amplitude, metres (0 at the card base) |
| UV2 | leaves/bark: x = branch bend, metres per unit wind (`bend_k * Lref * (path/Lref)^1.5`, path = distance along the limb), y = limb phase 0..1 (one value per main limb, so a limb and everything on it move together) — impostor: x = offset along the card from the trunk axis (m), y = card code (1 side XZ, 2 side YZ, 0 top) |
| UV3 | (0, 0) guard: keeps C4's `M_NatureLeaf` billboard path (`UV3.y >= 0.5`) switched off |
| vertex colour | sRGB tint in RGB (0.85-1.0, slightly warmer on the outer shell), **AO in alpha** (0.22 deep in the crown → 1 at the surface, darker underneath) |

Material slots (by name, identical on every mesh): **0 `Bark`, 1 `Leaves`, 2 `Impostor`**. LOD0-2 use slots 0-1,
LOD3 uses slot 2 only (imported meshes handle per-LOD sections; no degenerate "guard" triangles are needed, and they
cost an extra draw per cluster when present).

Leaf atlas `T_TreeLeafAtlas` (2048², 4x4 cells of 512, Blender UVs, v up; rects inset 6 texels; see
`manifest.json["leaf_atlas"]`): row 0 `broadleaf_a` (beech, 1.2 m spray), `broadleaf_b` (oak), `conifer_a`
(needle spray, 0.8 m), `conifer_b` (dense, for tops and leaders); row 1 `acacia_a` (bipinnate twig with paired white
thorns), `shrub_a` (0.45 m, small leaves), `acacia_b` (denser), `broadleaf_c` (young light foliage); rows 2-3 (cells
512x1024) `palm_a`, `palm_b`, `palm_dead` (a 2.5 x 5 m frond); one 512x1024 cell spare. Every tile is a small 3D
model (leaf blades with outline, curl, pale midrib, two-tone halves, per-leaf colour, depth darkening) rendered
top-down unlit by EEVEE at 2x, downsampled with premultiplied alpha and push-pull dilated (no dark fringes).

Impostor atlas `T_TreeImpostors` (2048², 8x8 cells of 256): cell = `species*12 + variant*3 + view`, view 0 = side
looking +Y, 1 = side looking -X, 2 = top. Albedo includes the crown AO; each card is cropped to the alpha bounding
box of its view to save overdraw.

Impostor normal atlas `T_TreeImpostorsN` (verification addition, same cells): the LOD0 mesh's own shading normals
(the cards' volumetric normals, bark normals) rendered per view in that view's frame, stored linear as `0.5 + 0.5 n`
with x = image right, y = image up, z = towards the bake camera (side view 0: +X / +Z / -Y, side view 1: +Y / +Z / +X,
top: +X / +Y / +Z, Blender axes). `M_TreeImpostor` turns it into a world normal with the card's own frame, so LOD3 is
lit like LOD0-2 (§12.2).

## 5. Import rules (Unreal)

`Scripts/import_trees.py`, run headless by `import.ps1`:

```
UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=import_trees.py -unattended -nosplash -nosound
    -dpcvars=Interchange.FeatureFlags.Import.FBX=0,Interchange.FeatureFlags.Import.PNG=0,Interchange.FeatureFlags.Import.SyncToBrowser=0
```

- **Interchange off.** With Interchange (the 5.7 default) the very first PNG import asserts in Slate
  (`CurrentApplication.IsValid()`, it tries to sync the content browser) and kills the commandlet. The cvars above
  switch PNG and FBX to the legacy factories.
- `StaticMeshEditorSubsystem` is **None in a commandlet**; `unreal.get_default_object(unreal.StaticMeshEditorSubsystem)`
  works for `import_lod`, `get/set_lod_build_settings`, `set_lod_screen_sizes`.
- FBX options: static mesh, combine meshes, no materials/textures/collision/lightmap UVs/Nanite, **import normals**
  (`FBXNIM_IMPORT_NORMALS`), vertex colour **Replace**, remove degenerates **off**, uniform scale 1 (Blender metres →
  Unreal cm through the FBX unit, checked: `SM_Tree_Broadleaf_0` extent 7.75 x 7.68 x 9.27 m half-size).
  LOD1-3 come from their own FBX through `import_lod(mesh, i, file)`; slots are matched by material name.
- Build settings on every LOD: **full-precision UVs** (atlas UVs and wind weights), recompute normals off, MikkTSpace
  tangents, distance-field resolution 0 (foliage SDFs are useless with `r.Lumen.TraceMeshSDFs=0`).
- LOD screen sizes (from the manifest): **1.0 / 0.45 / 0.20 / 0.08**.
- **The FBX importer flips V on every UV channel** (`v -> 1 - v`), not just on the texture channel. The exporter
  therefore writes UV1-UV3 pre-flipped (`mesh.to_blender`), so Unreal sees exactly the values of §4. Checked in
  Unreal with `Scripts/inspect_uvs.py` (UV1.y flutter 0-0.04, UV2 branch 0-0.95, UV3 = 0). Before this fix every
  vertex had ~1 m of flutter and branch weight: wind tore crowns into star-shaped smears and the impostor codes were
  wrong (the first forest shots, superseded).
- Textures are **never streamed** (`never_stream = True`): a `-game` run, and the first seconds of play, otherwise
  sample the leaf atlas at low mips, which smears leaves into brush strokes.
- The FBX importer mirrors **Blender +Y to Unreal -Y** (verified with the leaning palm's bounds). Anything that
  encodes a direction in vertex data must account for it (the impostor's side-card axis is `-LocalY`).
- `T_TreeImpostorsN`: linear (`srgb = False`), default compression, sampled as linear colour.
- Textures: atlases sRGB, default (BC3) compression, `do_scale_mips_for_alpha_coverage = True`,
  `alpha_coverage_thresholds = (0, 0, 0, 0.4)` (Unreal's own coverage-preserving mips, the job LifeLab did by hand);
  bark sRGB; bark normals `TC_NORMALMAP` (DirectX green, generated that way).
- Paths and names: `/Game/Trees/<Species>/SM_Tree_<Species>_<v>`, `/Game/Trees/Textures/T_*`,
  `/Game/Trees/Materials/M_TreeLeaf|M_TreeBark|M_TreeImpostor`, `MI_TreeLeaf`, `MI_TreeImpostor`, `MI_TreeBark_<Species>`.
  Species names match `DmNature::TreeName` (`Conifer, Broadleaf, Palm, Acacia, Shrub`).
- New materials compile asynchronously in `-game` runs: until they finish the trees render with the grey default
  material (my first impostor shots). `LabGameMode` now postpones the shot while
  `GShaderCompilingManager->GetNumRemainingJobs() > 0`; the game should prewarm (or ship cooked) the same way.

## 6. Materials, LODs, impostors, wind

- **M_TreeLeaf** (masked 0.4, TwoSidedFoliage, not two-sided, dithered LOD): `base = atlas.rgb * pow(VC.rgb, 2.2) *
  lerp(TintA, TintB, PerInstanceRandom) * lerp(0.28, 1, AO) * Brightness`, SSS = base x (0.75, 0.9, 0.3), AO out,
  roughness 0.82, specular 0.05; defaults Brightness 0.78, TintA (0.74, 0.86, 0.82), TintB (1.0, 0.99, 0.78), `Dry`
  slot as in C4. Identical formula to C4's `M_NatureLeaf`, so the assets also render correctly with C4's materials.
- **M_TreeBark** (opaque): `tex * pow(VC, 2.2) * lerp(0.55, 1, AO)`, normal map, roughness 0.88.
- **M_TreeImpostor** (masked, TwoSidedFoliage, world-space normal): the two side cards turn about the trunk axis to
  face the camera (yaw only, WPO): `centre = WP - axis * UV2.x`, `pos = centre + cross(toCamFlat, up) * UV2.x`;
  one of the two side views is kept per instance (PerInstanceRandom) and mirrored half the time, so neighbours do not
  repeat. The top card stays horizontal at the crown's mass height. Normals are computed per pixel from the position
  inside the atlas cell (left edge faces left, top faces up, centre faces the camera): crowns get a lit and a shadowed
  side like a volume. **Since the verification (§12.2) the normal is baked** (`T_TreeImpostorsN`, parameter
`NormalAtlas`): `n = r * flip * t.x + up * t.y + f * t.z` for the side cards (r = card right, f = towards the camera,
flip = the instance's mirror) and `lx * t.x - ly * t.y + up * t.z` for the top card; the guessed normal it replaced
(`frac(uv0 * 8)` inside the cell, 0.65 towards the camera) left LOD3 flat and dark against LOD2 at golden hour. In
shadow passes the cards face the light, so they cast full-size shadows. The top card only
  exists for steep views (`smoothstep(0.5, 0.7, dot(toCamera, up))`, else it collapses onto the trunk axis via
  `ObjectPositionWS`): at oblique angles it showed as pale plates inside conifer spires.
- **LOD switching**: 1.0 / 0.45 / 0.20 / 0.08 with dithered transitions. LOD1 keeps ~42% of the cards at 1.45x size,
  LOD2 ~13% at 2.5x pulled 12% towards the crown centre; branch tubes drop sides (12/8/5 trunk, 7/5/3 limbs, twigs
  only in LOD0; broadleaf level-2 branches also only in LOD0 since the verification); palm fronds 9/6/4 segments.
- **Wind** (`WindStrength`, `BranchStrength`, `FlutterStrength`, `WindSpeed`, `WindDir`, `PlanetCenter`):
  `sway = windDir * S(t) * UV1.x * WindStrength` (LifeLab gust function), `branch = (windDir * 0.5 + up * 0.85) *
  (0.55 sin(2.3t + phase) + 0.3 sin(3.7t + 1.9 phase)) * gust * UV2.x * BranchStrength`, `flutter = normal *
  sin(8t + 40(u + v) + ...) * UV1.y * FlutterStrength`. Limbs bob independently while everything on a limb stays
  attached (one phase per limb: the cheap half of Pivot Painter). Rigid VSM invalidation as in LifeLab.

## 7. Stand density for real-scale trees

LifeLab's scatter (one candidate per 4.5 m cell, `Canopy * 0.62`) was tuned for 7-10 m crowns. With the real-scale
crowns (12-18 m) it puts a trunk every 5.7 m: crowns interpenetrate and only narrow tops show (the first 50 m shots
looked like a field of cypress columns). TreeLab multiplies the closed-canopy term by **0.5** (`-canopydensity=`,
default 0.5 for asset trees): ~8 m spacing, crowns touch, gaps stay dark — what the biome sheets show (95% cover with
crown relief). Open-land and coast terms are unchanged. Instances: 943k → 554k on the temperate site.

## 8. Results

### 8.1 Shots

Forest matrix, one site per biome, camera looking at the densest stand of the biome's tree kind, 50 m / 300 m /
1.2 km / 5 km altitude (pitch -7 / -20 / -32 / -50°), day (sun 48°) and golden hour (sun 6°, +1.4 EV):
`shots/ue-treelab-<temperate|boreal|rainforest|savanna|palmcoast>-<50|300|1200|5000>-<day|gold>.png` (40 shots), one
sheet per biome `shots/ue-treelab-sheet-<biome>.jpg` (top row 50 m and 300 m, bottom row 1.2 km and 5 km; day and gold
side by side). Stand-ins at the same cameras: `...-300-day-gen.png`, `...-1200-gold-gen.png`, side by side in
`shots/ue-treelab-sheet-vs-standins.jpg` (per biome row: asset 300 m, stand-in 300 m, asset 1.2 km, stand-in 1.2 km).
Per-species close-ups and the lineup: `shots/ue-treelab-species-*.png`, `ue-treelab-lineup-*.png`,
`ue-treelab-palmcoast-close-*.png`, sheet `ue-treelab-sheet-species.jpg`. Experiments (density 1.0, alternative LOD
sizes): `ue-treelab-temperate-*-dens1.png`, `*-lodB.png`, sheet `ue-treelab-sheet-experiments.jpg`.

Against the concept sheets (`docs/concept/nature/`):
- **Temperate** (`biome-temperate-broadleaf`): closed, dark, cauliflower canopy with crown relief, about 1 crown in 30
  turning copper or yellow (the `Autumn` accent), gold rims at golden hour. Darker and less lime than the stand-ins,
  as the concept's canopy median `#283120` asks. Valley mist and meadow clearings are terrain/sky work, not trees.
- **Boreal** (`biome-boreal-conifer`): dark narrow spires with clear gaps (60-75% cover), long thin shadows at golden
  hour; the stand-ins read as rounded green blobs here. Birch patches and lakes are outside this task.
- **Rainforest** (`biome-rainforest`): unbroken canopy, but **not yet the concept's crown relief**: the broadleaf
  variants are temperate shapes scaled x1.35. A dedicated tropical family is the next species (§11).
- **Savanna** (`biome-savanna`): isolated flat-topped acacias 30-100 m apart, each with its own shadow, gallery
  broadleaf near water; reads like the concept at 300 m and 1.2 km.
- **Palm coast**: a palm fringe along the beach behind the broadleaf; fronds read as fronds down to 50 m.
- **Canopy from altitude** (`descent-3-10km`: crown clusters 20-60 m, ~85% cover, dark base): at 1.2 km the forest is a
  bumpy dark texture with lit tops and black gaps; at 5 km it merges with the painted-canopy terrain into one dark
  mass with ragged edges (`temperate-5000-*`, `boreal-5000-*`). The impostors keep silhouette and colour there (no
  thinning or brightening when LOD3 takes over, thanks to the alpha-coverage mips).

Wind (trunk sway, per-limb bobbing, leaf flutter) only shows in motion; the shots are stills.

### 8.2 Frame time (RHI GPU median of the 60 frames before each shot)

Assets (density 0.5, LOD sizes 1 / 0.45 / 0.2 / 0.08), the whole 8 x 8 km tree patch instanced (one HISM per kind x
variant, 20 components, rigid VSM invalidation):

| biome (site) | instances | 50 m | 300 m | 1.2 km | 5 km |
|---|---|---|---|---|---|
| temperate (38500) | 554,491 | 17.2 / 17.7 | 13.1 / 12.8 | 10.5 / 10.8 | 12.7 / 13.2 |
| boreal (3753) | 81,973 | 14.1 / 14.7 | 10.5 / 10.2 | 8.3 / 8.3 | 9.4 / 9.3 |
| rainforest (5299) | 735,065 | 20.3 / 20.5 | 18.0 / 18.2 | 12.1 / 11.8 | 16.4 / 16.3 |
| savanna (21445) | 257,761 | 8.2 / 8.4 | 6.6 / 6.9 | 7.5 / 7.5 | 10.8 / 10.4 |
| palm coast (5299) | 735,065 | 14.1 / 14.1 | 11.2 / 11.4 | 8.6 / 8.6 | 12.4 / 12.5 |

(day / golden hour, ms). The scene also has 7.8 M terrain triangles, water, sky atmosphere and volumetric clouds (the
5 km shots hide the cloud layer).

Same cameras with LifeLab's generated stand-ins (`-gentrees`, LifeLab density):

| biome | stand-in instances | 300 m day: assets / stand-ins | 1.2 km gold: assets / stand-ins |
|---|---|---|---|
| temperate | 943,139 | **13.1** / 19.3 | **10.8** / 17.0 |
| boreal | 113,185 | **10.5** / 16.7 | **8.3** / 9.7 |
| rainforest | 1,273,032 | **18.0** / 29.5 | **11.8** / 21.0 |
| savanna | 324,155 | **6.6** / 11.4 | **7.5** / 8.4 |
| palm coast | 1,273,032 | **11.2** / 12.4 | **8.6** / 12.1 |

Experiments (temperate, cameras 50 m day / 300 m day / 1.2 km gold):

| variant | instances | GPU ms |
|---|---|---|
| default (density 0.5) | 554,491 | 17.2 / 13.1 / 10.8 |
| density 1.0 (LifeLab spacing) | 943,139 | 24.1 / 19.6 / 16.3, and the canopy turns into columns |
| LOD sizes 1 / 0.4 / 0.22 / 0.12 | 554,491 | 15.5 / 11.9 / 11.6: ~1.5 ms cheaper near the camera, flatter crowns at 300 m; not adopted |

Caveats: three or four other Unreal processes (AssetLab, GroundLab, game lanes) rendered on the same GPU during most
runs, so absolute numbers are pessimistic and the *frame* times (dominated by RHI-thread waits, 9-56 ms) are not
meaningful; asset and stand-in runs were taken under the same conditions. LifeLab measured 8.5-10 ms for a temperate
view with 846k stand-ins on a quiet machine, so expect roughly half of the figures above in isolation. The rainforest
remains the heaviest case (18-20 ms near the canopy): §11.

Build cost: Blender ~110 s for everything (bark 5 s, leaf atlas 40 s, 20 trees + 60 impostor views 60 s); Unreal import
~5 min. Disk: `art/trees/textures` 27 MB, `export` 18 MB, `source` 13 MB; `/Game/Trees` 40 MB uncooked. Texture
memory: leaf and impostor atlases 2048² BC3 (~5.3 MB each with mips), ten 1024² bark maps (~13 MB).

## 9. Swapping them into the nature module (Game/Nature, C4)

`Game/Nature` was read, not edited. The factory interface (mesh id + variant -> `FDmNatureMeshInfo`) already makes this
a small change. Steps for the C4 owner, in this order:

**9.1 Put the assets in the game project.** Copy `UnrealLab/TreeLab/Scripts/import_trees.py` to `Unreal/Scripts/trees.py`
(it finds `art/trees` by walking up from its own folder; env `TREES_ART` overrides) and run it like the other content
scripts, with the Interchange cvars of §5:

```
UnrealEditor-Cmd.exe Unreal/Demiurge.uproject -run=pythonscript -script=Unreal/Scripts/trees.py -unattended -nosplash -nosound
    -dpcvars=Interchange.FeatureFlags.Import.FBX=0,Interchange.FeatureFlags.Import.PNG=0,Interchange.FeatureFlags.Import.SyncToBrowser=0
```

It writes `/Game/Trees/...` (meshes with LODs, screen sizes and build settings, the 13 textures, `M_TreeLeaf`,
`M_TreeBark`, `M_TreeImpostor` and their instances). Re-runnable; ~4.5 min.

**9.2 `DmNatureFactory.h`**: give the mesh info optional per-slot materials.

```cpp
struct FDmNatureMeshInfo
{
	UStaticMesh* Mesh = nullptr;
	float Height = 1.f;
	float Radius = 1.f;
	int32 Tris = 0;
	TArray<UMaterialInterface*> Materials;   // per slot; empty = the mesh's own (kept alive through UDmNatureFactory::Keep)
};
```

**9.3 `DmNatureFactory.cpp`, `Build()`**: before generating a tree variant, try the asset (the Dead tree stays generated).

```cpp
#include "StaticMeshResources.h"
...
	// asset pass (docs/lab/trees.md): Blender tree families in /Game/Trees; the generated stand-ins are the fallback
	UMaterialInstanceDynamic* AssetLeaf = LoadMat(TEXT("M_NatureLeaf"));       // same shading formula as M_TreeLeaf
	AssetLeaf->SetTextureParameterValue(TEXT("Atlas"), LoadObject<UTexture2D>(nullptr, TEXT("/Game/Trees/Textures/T_TreeLeafAtlas.T_TreeLeafAtlas")));
	UMaterialInterface* AssetImpostor = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Trees/Materials/MI_TreeImpostor.MI_TreeImpostor"));
	TArray<UMaterialInstanceDynamic*> AssetBarks;
	for (int32 K = 0; K < int32(EDmTreeKind::Num); ++K)
	{
		UMaterialInstanceDynamic* Bark = nullptr;
		for (int32 V = 0; V < NumVariants(EDmNatureMesh(K)); ++V)
		{
			if (EDmTreeKind(K) != EDmTreeKind::Dead)
			{
				const FString Kn = DmNature::TreeName(EDmTreeKind(K));
				const FString P = FString::Printf(TEXT("/Game/Trees/%s/SM_Tree_%s_%d.SM_Tree_%s_%d"), *Kn, *Kn, V, *Kn, V);
				if (UStaticMesh* SM = LoadObject<UStaticMesh>(nullptr, *P))
				{
					if (!Bark)    // one bark instance per species
					{
						Bark = LoadMat(TEXT("M_NatureBark"));
						Bark->SetTextureParameterValue(TEXT("BarkTex"), LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/Trees/Textures/T_Bark_%s.T_Bark_%s"), *Kn, *Kn)));
						Bark->SetTextureParameterValue(TEXT("BarkN"), LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/Trees/Textures/T_Bark_%s_N.T_Bark_%s_N"), *Kn, *Kn)));
						Bark->SetScalarParameterValue(TEXT("BarkTileU"), 1.f);   // asset bark UVs are already in tile units
						Bark->SetScalarParameterValue(TEXT("BarkTileV"), 1.f);
						AssetBarks.Add(Bark);
					}
					FDmNatureMeshInfo Info;
					Info.Mesh = SM;
					const FBox B = SM->GetBoundingBox();
					Info.Height = float(B.Max.Z / 100.0);
					Info.Radius = float(FMath::Max(B.Max.X - B.Min.X, B.Max.Y - B.Min.Y) / 200.0);
					Info.Tris = SM->GetRenderData() ? SM->GetRenderData()->LODResources[0].GetNumTriangles() : 0;
					Info.Materials = { Bark, AssetLeaf, AssetImpostor };          // slots 0 Bark, 1 Leaves, 2 Impostor
					Keep.Add(SM);
					Infos[K].Add(Info);
					continue;
				}
			}
			// ... the existing generated path (DmNature::BuildTree + MakeStaticMesh) unchanged ...
		}
	}
```

Add `AssetLeaf` and `AssetBarks` to `Keep` and to the material loops of `SetWind`, `SetPlanetFrame` and `SetLeafLook`
(store them as members), so wind strength, the planet frame (wind direction, snow "up") and the leaf look reach them.

**9.4 `DmNatureActor.cpp`, `MakeComps()`**, right after `C->SetStaticMesh(Info.Mesh);`:

```cpp
		for (int32 M = 0; M < Info.Materials.Num(); ++M) if (Info.Materials[M]) C->SetMaterial(M, Info.Materials[M]);
```

Everything else stays: `ShadowCacheInvalidationBehavior = Rigid`, the per-tile shadow distance toggle, `WpoDistM = 0`
(the impostor needs WPO at every distance), cull distances, per-instance custom data, `NumVariants = 4`.

**9.5 `DmNatureScatter.cpp`**: real-scale density and scale (§7). In the two places that compute the tree probability,
halve the closed-canopy term, and narrow the scale jitter:

```cpp
	float P0 = (Can * 0.62f * 0.5f * (1.f - 0.15f * Wet) ...           // was Can * 0.62f
	const float P = (CanH * 0.62f * 0.5f * (1.f - 0.15f * Wet) ...     // was CanH * 0.62f
	float Scale = FMath::Lerp(0.85f, 1.12f, H1);                         // was 0.72 .. 1.22
	if (CanH < 0.3f) Scale *= 0.95f + 0.12f * Hash01(...);               // was 0.9 + 0.25 * ...
```

The rainforest x1.35 / emergent x1.6 rule can stay (broadleaf variants are 12-25 m; rainforest crowns 15-30 m).

**9.6 Material notes.** The assets follow C4's vertex contract (sRGB tint + AO alpha, UV1 sway / flutter, UV3 = 0),
so `M_NatureLeaf` / `M_NatureBark` shade them correctly, including burn / snow / dry custom data. Two optional
upgrades for `Unreal/Scripts/nature.py`:
- **branch motion**: in both `Wpo` functions, when `uv3.y < 0.5` (not one of C4's own billboard cards), add
  `float bp = uv2.y * 6.283 + ph; float b = (0.55 * sin(2.3 * tt + bp) + 0.3 * sin(3.7 * tt + 1.9 * bp)) * (0.55 + 0.45 * WindS(tt, ph));`
  `sway += (wd * 0.5 + up * 0.85) * b * uv2.x * BranchStrength;` (BranchStrength 100, like WindStrength).
- **M_NatureImpostor**: port `M_TreeImpostor` (the `IMPOSTOR` HLSL in `import_trees.py`: `Bill` for WPO, `Nrm` for a
  world-space normal from the baked `T_TreeImpostorsN` with `tangent_space_normal = False`; keep `Brightness` equal
  to the leaf material's) and add the burn / snow / dry lines of `Leaf()`, so burnt
  and snowy forests also change colour at distance. Until then `MI_TreeImpostor` works but ignores custom data.

## 10. Pitfalls (collected)

1. **Interchange PNG/FBX import asserts in a commandlet** (Slate `CurrentApplication.IsValid()`): use the legacy
   importers via `-dpcvars=Interchange.FeatureFlags.Import.FBX=0,...PNG=0,...SyncToBrowser=0`.
2. `unreal.get_editor_subsystem(StaticMeshEditorSubsystem)` is None in commandlets: `unreal.get_default_object(...)` works.
3. FBX mirrors Blender +Y to Unreal -Y. Directions stored in vertex data must be mirrored in the material.
4. New materials render as the grey default material until their shaders compile: wait for
   `GShaderCompilingManager->GetNumRemainingJobs() == 0` before shooting (now in `LabGameMode`).
5. Degenerate "guard" triangles that keep every section in every LOD (needed for LifeLab's runtime mesh builds) are
   not needed for imported meshes and cost an extra draw per HISM cluster and LOD: removed.
6. Crossed static impostor cards read as vertical stripes in a closed forest at 300 m; yaw-billboarded cards with
   per-pixel normals read as crowns.
7. Real-scale crowns at the stand-ins' density interpenetrate into columns: halve the closed-canopy density.
8. Card size vs leaf size: a 0.7 m twig tile on 1.2 m cards gave 17 cm leaves ("painted" look); the broadleaf tile is
   now a 1.2 m spray of five twigs (true 8-12 cm leaves).
9. Thin needles and tiny acacia leaflets vanish under the 0.4 alpha clip after downsampling: needles are 2.8 mm wide
   and acacia leaflets about 2x natural size in the tiles (0.4-clip coverage: needle tiles 0.23-0.29, acacia
   0.19-0.22, broadleaf 0.25-0.28, shrub 0.36).
10. Golden hour with a 6° sun and manual exposure 0 is nearly black: the matrix uses `-ev=1.4` for golden hour.
11. Blender: bake with view transform **Standard** (the default AgX shifts albedo); `BYTE_COLOR` attributes come out
    linearised in shaders (matching the Unreal material's `pow(VC, 2.2)`); `.blend1` backups are off.
12. FBX import flips V on **all** UV channels: data channels must be written pre-flipped (§5). Verify any vertex-data
    contract in Unreal (`Scripts/inspect_uvs.py`), not only in Blender.
13. Conifer leader cards must sit on the actual (leaning) trunk top, not on the mesh's z axis, or they float beside
    the tree.
14. Timing: up to four labs were rendering on the same GPU during these runs (AssetLab, GroundLab, a sky lane):
    absolute numbers are inflated and noisy. Compare asset vs stand-in runs of the same view (§8); trust the GPU median.

## 11. Known issues and next steps

- **Rainforest crowns**: tropical forest uses the temperate broadleaf variants x1.35; the concept wants emergent giants
  with umbrella crowns and deep gaps between them. Add a tropical broadleaf family (tall clear bole, buttress roots,
  flat wide crown) as a sixth species in `species.py` and give C4 its own id or variants 4-7.
- **Autumn accent** is one material scalar (`Autumn`, 3.5% of broadleaf crowns, also in the rainforest); set it per
  biome and season (0 in the tropics) through the dynamic instance or a custom-data float in production.
- **Near/far band at golden hour**: fixed by the verification (§12.2, baked impostor normals). It was not "a little
  brighter": at 300 m it was a hard horizontal line across the whole frame where LOD2 meets LOD3.
- **Golden-hour saturation** (§12.4): lit leaves go lime at golden hour (saturation 0.52-0.81 against the concept's
  0.46-0.51); the transmission colour `(0.75, 0.9, 0.3)` and `TintB` blue 0.78 crush blue. A C4 material decision, not an
  asset one: try `Transmission (0.6, 0.72, 0.38)` and `TintB (1, 0.98, 0.88)` in `M_NatureLeaf` and judge against
  `biome-temperate-broadleaf` (lit tops `#484425`).
- **Crown shape from oblique views**: broadleaf crowns still read as narrow, pointed and all the same size from 50-300 m
  (the concept's temperate canopy is rounded "cauliflower" domes of mixed sizes). Real-scale open-grown crowns at ~8 m
  spacing interpenetrate, so only the tops show; forest-grown variants (narrower, flatter-topped crowns with the mass
  high) would read better than density alone.
- **Close-ups below ~5 m** still show cards (painted leaves); fine for the god-game camera.
- **Dead trees** stay generated (C4's `Dead` kind); a Blender family can follow the same pipeline.
- **Distance fields**: imported meshes can build mesh SDFs (`distance_field_resolution_scale` > 0) if Lumen mesh-SDF
  tracing is ever enabled; the lab keeps them off (`r.Lumen.TraceMeshSDFs=0`).

## 12. Independent verification (2026-11-20)

Rebuilt from the scripts (one command, 54 s; a second full rebuild gave byte-identical PNGs and manifest), re-imported
(20 meshes, 13 textures, 0 errors), UV contract re-checked in Unreal (`uvcheck.ps1`), the 40-shot matrix re-rendered
twice (as delivered, then after the fixes below), plus forced-LOD and storm-wind experiments. Helper:
`UnrealLab/TreeLab/vshots.sh <biome> '<shot regex>' <tag> [args]` renders named matrix shots with a tag and extra args
(forced LODs: `-lodsizes=1,10,10,10` = all LOD3, `1,10,10,0.00001` = all LOD2, `1,10,0.00001,0.00001` = all LOD1;
**`r.ForceLOD` does nothing for these HISMs**). Review sheet: `shots/ue-treelab-sheet-verify.jpg` (rows: 300 m golden
hour before/after; forced LOD2 vs LOD3 with the old impostor; the same with the new one; forced LOD1 before/after).

### 12.1 What held

- Real-world scale, pivots, slots, the UV contract (UV1.y flutter 0-0.04, UV2 0-0.4, UV3 = 0), never-stream textures,
  LOD screen sizes 1 / 0.45 / 0.2 / 0.08 and the import rules all check out.
- GPU medians reproduce within noise (temperate 17.4 / 12.9 / 10.9 / 12.8 ms at 50 m / 300 m / 1.2 km / 5 km against
  17.2 / 13.1 / 10.5 / 12.7 claimed), and are unchanged by the fixes.
- Assets beat the stand-ins on *frame* time too, not only GPU, in a quiet moment: temperate 50 m 17.6 ms vs 28.2 ms,
  300 m 12.9 vs 19.6 ms (frame = GPU-bound, RHI 9-11 ms). Under contention from other labs the RHI thread swings
  wildly (the same savanna 300 m shot: 26.8 ms then 8.7 ms frame; GPU 7 ms both times; no trees: 6.7 ms), so treat the
  `frame` column of `timings.txt` as noise and re-measure on a quiet machine before C4 commits to a budget.
- Savanna and boreal read like the concepts; the 1.2 km / 5 km canopy matches `descent-3-10km`. Impostors cast shadows.
- Storm wind (`-wind=3`, `shots/ue-treelab-wind-*-a/b.png`): trunks, limbs and cards move together, no tearing.

### 12.2 Fixed

1. **LOD2 -> LOD3 band (the worst flaw)**: at 300 m golden hour a hard line crossed the frame where the impostors
   start: near field lime and lit, far field flat and dark (forced LOD2 vs LOD3 on the same view: lit-leaf 80th
   percentile `#3b491f` vs `#2a3918`). Cause: the impostor normal was guessed (`frac(uv0 * 8)` over the *cell*, while
   cards are cropped to the alpha box, plus 0.65 towards the camera), so LOD3 barely responded to the sun. Now the
   bake also renders the LOD0 mesh's own shading normals per view (`export._bake_views`, `render.mat_bake_normal`,
   atlas `T_TreeImpostorsN`) and `M_TreeImpostor` rotates them into the card frame. After: `#3d4c20` vs `#3d4a1e`,
   median luminance 50 vs 47 (gold) and 57 vs 55 (day); the line is gone in `temperate-300-gold`. `MI_TreeImpostor`
   `Brightness` 0.7 -> 0.78 (= the leaf material; both apply the same AO, the 0.7 left LOD3 ~8% darker).
2. **LOD1 greyer than LOD0 and LOD2, with bare twigs**: broadleaf LOD1 kept the level-2 branches but only 42% of the
   cards, so grey sticks poked out of every crown ("dead tree" tops) and LOD1 read desaturated (HSV saturation 143-165
   vs 185-192 for LOD0 and LOD2 on the same rows of `temperate-50-day`). LOD1 now drops level-2 branches (broadleaf
   `sides[2] = (4, 0, 0)`): saturation 189-197, matching; LOD1 triangles -25% (e.g. 4,756 -> 3,511).
3. **Trunk-top spike in wind**: the tip cap of a tube took `path0 + length` as branch-bend path for every level, so
   trunk caps got up to 1.1 m of branch bend (palm: UV2.x 0.95 on one vertex, 0.025 on everything else) and pulled a
   spike out of the crown top in wind. The cap now follows the rings' rule (no bend on level 0).
4. **Impostor bark bake ignored the AO** (`render.mat_bake_tree`): LOD3 trunks and limbs were up to 1.8x brighter than
   on LOD0-2 (`lerp(0.55, 1, AO)` in `M_TreeBark`). The bake now applies the same factor.
5. **Pale twigs and pink spruce "fishbones"** through the cards at 50-300 m: branch bark inside the crown was
   unoccluded. `species.bark_ao` multiplies branch AO by the crown's leaf-AO field (and the trunk's above the crown
   base); broadleaf level-2/3 and live conifer branches get a darker grey tint (`TWIG_TINT`, `CONIFER_BRANCH_TINT`).
   Note the bark material's AO floor (0.55) limits what AO alone can do; the tint does the rest.
6. **Conifer stems off plumb**: the trunk's random walk (`gnarl` 0.012) bent spruce stems up to 4.8° (2.2 m at the top of
   the 28 m tree, plainly leaning in the species shot). `gnarl` 0.004 (same random draws, nothing else moves).

### 12.3 Not fixed here (owners)

- Rainforest crown relief (needs the tropical family, §11), Autumn accents in the tropics (per-biome `Autumn`),
  golden-hour saturation (C4 material values, §11), crown shape from oblique views (§11), palm coast is mostly
  broadleaf with a thin palm fringe (scatter weights, C4).

### 12.4 Concept colour check (sRGB, 20th / 50th / 80th luminance percentile of the canopy)

| image | p20 | median | p80 | saturation |
|---|---|---|---|---|
| concept `biome-temperate-broadleaf` | `#141d14` | `#292e1d` | `#484425` | 0.46 |
| temperate 50 m day (after fixes, lower 60% of frame) | `#14240c` | `#253a13` | `#354d17` | 0.69 |
| temperate 50 m golden hour | `#162505` | `#243509` | `#3f4e12` | 0.81 |
| temperate 300 m golden hour | `#1f2e18` | `#2a3b1c` | `#415022` | 0.52 |

Brightness is on target; the near views are too saturated (golden hour 0.81) and the lit tops too green (concept: R = G).

