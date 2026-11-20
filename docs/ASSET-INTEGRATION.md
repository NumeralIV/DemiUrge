# DEMIURGE — asset integration: real Blender assets into the game

*Track "asset-integration" (night of 2026-11-19 → 20), lead's build agent. Status: plan and tooling done; the tool is in
`Unreal/Scripts/assets/` (new folder), tested in lane `assetimport` only (lane not synced, no game code changed). Binding for the module owners who make the
factory changes in section 6; ASSETS.md stays binding for the asset engineers.*

Inputs read: `docs/ASSETS.md`, every `art/*/manifest.json` and `README.md` (14 families), `art/trees/export/manifest.json`,
`art/export/shrine/shrine.manifest.json`, `art/tools/ue_import.py`, and the factories in `Game/Towns` (`DmTownsKit`,
`DmTownsPlan`, `DmTownsActor`: units are drawn there too) and `Game/Nature` (`DmNatureFactory`, `DmNatureScatter`,
`DmNatureCritters`).

---

## 1. Summary

- **What is there.** 825 static meshes in 15 families (shrine excluded, it has its own importer), 896 FBX files, 358
  textures (832 MB of GPU memory if everything were resident; wonders alone are 469 MB of that), 11 skeletal animals.
  Every FBX is a LOD group (LOD0..LOD3) except trees and the shrine (one file per LOD). Material slots are named after
  the family material (`M_Age2_Limestone`) or after the role (`Structure`, `Roof`, `Detail`, `Bark`, `Leaves`).
- **The tool.** `Unreal/Scripts/assets/import_assets.py` (+ `import_assets.ps1`) plans everything in plain Python
  (coverage check: every FBX, every slot, every texture resolves; **0 issues** on the current art), then imports it
  headless into `/Game/Assets/<Family>/` with the right scale, a normalised facing, all LODs, collision and material
  instances, and checks every mesh against its manifest (height, bounds, LOD count, triangles). It is re-runnable.
- **One material system for every family.** Three masters (`M_DmKit` opaque, `M_DmKitFoliage` masked two-sided,
  `M_DmKitGlow` additive) driven by parameters, plus a material parameter collection `MPC_DmAssets` that carries the
  planet frame. Each family's mask and vertex-colour conventions are mapped onto the master by channel selectors, so
  the 14 different family contracts did not need 14 masters. 418 instances: one base per family material, plus every
  culture, climate and biome instance the manifests name, with the manifests' own names.
- **Three findings the factory owners must act on** (details in sections 5 and 6):
  1. **LOD screen sizes.** The manifests' 1.0 / 0.35 / 0.12 / 0.04 were tuned for a 40 m look. At the god camera
     (0.3-3 km) every building would sit on its hull. The importer's default profile switches by projected size instead.
  2. **Towns draw with plain ISMs, whose LOD is picked per component**, from the bounds of the whole settlement (the
     towns code says so: HISM loses custom data in 5.7). With real LOD chains that means a town renders LOD0 for every
     house until the whole town is small. Real assets need either Nanite or one component per district cell (6.1).
  3. **Facing.** The plain FBX import turns every family except the shrine 180° from the towns convention. The tool
     measures this per family and corrects it at import, so the factories see buildings front -Y and movers front +X.
- **Gaps** (no asset yet, keep the generated stand-in or commission it): Villa, Windmill, MarketHall, TempleLate,
  Granary v1 (modern silo), early MineHead, Barn, Sail-age walls, modern Crane, the dead tree. Listed in 4.9.

---

## 2. The import tool

```
# plan only (no Unreal): coverage, texture / instance resolution, issues
powershell -NoProfile -ExecutionPolicy Bypass -File "<project>\Scripts\assets\import_assets.ps1" -Plan
python Unreal/Scripts/assets/import_assets.py --plan [--families age2,ships] [--verbose] [--json plan.json]

# import into THIS project (the script's own Unreal/ or Unreal/Lanes/<key>/), headless, through tools/render_slot.py
powershell -NoProfile -ExecutionPolicy Bypass -File "<project>\Scripts\assets\import_assets.ps1"
    [-Families age2,ships,shrine] [-Match House_A,Tower] [-Limit 3] [-LodProfile game|art] [-Nanite]
    [-NoSkeletal] [-NoShrine] [-OnlyMats] [-Dry] [-Timeout 7200]
```

The project is the folder the script sits in, so a lane's copy imports into that lane only. The editor runs as
`UnrealEditor-Cmd -run=pythonscript` (no window). Options travel as `DMI_*` environment variables, and a two-line
bootstrap in the temp folder runs the real script, because the commandlet's `-script=` value cannot carry a path
with a space (`Fable5.2 Test`). The session disables distance-field generation
(`-ini:Engine:[/Script/Engine.RendererSettings]:r.GenerateMeshDistanceFields=False`): every mesh edit rebuilt the
distance field and the Lumen cards, about 100 s per mesh under load. Both are derived data. The game and the cook
build them on first load and cache them in the DDC, so the first launch after an import is slower once.

Output: `Saved/Logs/asset-import-<stamp>.log` (full log, `DMI ...` lines), `Saved/AssetImport/plan.json` and
`report.json` (per mesh: path, yaw, LOD count, triangles per LOD, screen sizes, slots, bounds in cm, scale check
against the manifest height, facing error against the manifest bounds, collision, Nanite). The script exits 0 on
`DMI RESULT PASS`.

### 2.1 What it creates

| Path | What |
|---|---|
| `/Game/Assets/Materials/MPC_DmAssets` | `PlanetCenter`, `SunDir` (vectors), `NightOverride` (< 0: from the sun), `Glow` (scalars). Written once per frame by the towns / nature actors (6.1). |
| `/Game/Assets/Materials/M_DmKit` | Opaque master for every family (section 3). Usage flags: instanced static meshes, skeletal meshes, Nanite. |
| `/Game/Assets/Materials/M_DmKitFoliage` | Masked (D.a), two-sided foliage shading, dithered LOD transitions: leaf cards, impostors, hedges, crops. |
| `/Game/Assets/Materials/M_DmKitGlow` | Additive unlit: the Sunwell's light shaft. |
| `/Game/Assets/<Family>/Textures/T_*` | D sRGB; N `TC_NormalMap` with the green channel flipped (every source is OpenGL +Y); ORM and M `TC_Masks` linear. |
| `/Game/Assets/<Family>/Materials/MI_<Family>_<Set>` | One base instance per family material, with that material's textures and the family preset (3.2). |
| `/Game/Assets/<Family>/Materials/MI_..._<Culture / Style / Biome>` | Every variant the manifest names, as a child of its base, with only its tints and offsets. |
| `/Game/Assets/<Family>/Meshes/SM_*` | Static meshes: every LOD, slots set to the base instances, sockets kept (`SOCKET_*` empties import as mesh sockets). |
| `/Game/Assets/Animal/Skeletal/SK_*` | Skeletal animals, morph targets, every action as an animation sequence. |
| `/Game/Assets/Shrine/SM_Shrine_*` | The shrine, through `art/tools/ue_import.py` run in the same session (its own `M_DmAsset` masters). |

Families: `Age0`..`Age6`, `Culture`, `Wonder` (wonders-a and wonders-b), `Prop`, `Ship`, `Unit`, `Animal`, `Tree`,
`Shrine`.

### 2.2 Conventions it enforces

- **Scale.** `convert_scene_unit` on, uniform scale 1. Some families wrote centimetres (FBX unit scale 100), some
  metres; both arrive in centimetres. Checked per mesh: the imported height against the manifest's `height_m`
  (warning beyond 6 %).
- **Facing.** The towns convention is front (door, street side) -Y and the long axis along X. The towns unit drawing
  and the nature critters move meshes along +X (`DmTownsActor.cpp`, units: the instance matrix rows are forward,
  right, up). The visual identity of the FBX import maps Blender (x, y, z) to Unreal (x, -y, z), so a Blender front
  -Y arrives at +Y. The tool imports the first asymmetric mesh of each family with no rotation, compares its Unreal
  bounds with the manifest's Blender bounds for 0 / 90 / 180 / 270° (and the mirrored hypothesis, which would be an
  error), and sets `import_rotation` for the family: 180° for buildings, 270° for movers on the current art. Families
  without manifest bounds take the measured default. Every later mesh with asymmetric bounds is checked again
  (`facing_err` in the report).
- **LODs.** LOD groups import with *Import Mesh LODs*; trees import LOD0 and add `_LOD1..3` files. Screen sizes come
  from the profile (5.1), not from the FBX.
- **Collision.** Buildings and wonders get one simple box (camera and picking traces), with `CTF_UseSimpleAsComplex`.
  Props, movers, animals and trees get none. The shrine keeps its UCX hulls. Instanced components must still set
  `NoCollision` (the towns and nature code already does): a per-instance physics body on 10,000 instances costs
  milliseconds.
- **Nanite.** Off by default, `-Nanite` turns it on for buildings, wonders, culture overlays and hard-surface props
  (never foliage materials). See 5.2 for why the towns owner should test it first.
- **Naming.** Mesh names are the manifests' (`SM_Age2_House_A_Intact`). Base instances are `MI_` plus the family
  material without `M_` (`MI_Age2_Limestone`). Variant names are exactly the manifests' (`MI_Age2_Limestone_A`,
  `MI_Age0_Structure_Arid`, `MI_Ship_Structure_C3`, `MI_Unit_Detail_Teal`, `MI_Animal_GrazerBison_Tundra`).

---

## 3. The material contract

### 3.1 Per-instance custom data (unchanged for the towns)

`M_DmKit` reads the towns layout when `CdMode = 0`: 0..2 wall tint, 3..5 roof tint (applied with `TintAmt` 0.3 on the
slot the base instance marks as wall (`TintChannel 1`, Structure) or roof (`2`, Roof)), 6 lit fraction of the
windows, 7 state (0 intact, 1 burnt or ruined: darkened, no lights). `CdMode = 1` is the nature tree layout
(0 burn, 1 snow, 2 dry); `CdMode = 2` the grazer coat tint (0..2). A plain static mesh component reads the defaults
(no tint, 60 % lit, intact). **No factory needs a new custom-data layout to start.** Proposed later: towns floats 8..10
for the culture colour of units and ships (today the unit drawing writes it into the roof tint, 3..5).

### 3.2 How each family's masks map onto the master

The master has RGBA selectors on the mask texture M (`NightSel`, `CultureSel`, `AccentSel`, `MossSel`, `AlwaysSel`,
`EmisSel`, `RandMSel`) and on the vertex colour (`VCSootSel`, `VCAOSel`, `VCCultureSel`, `VCMossSel`, `RandVCSel`),
plus switches (`HasD`, `HasM`, `HasORM`, `HasN`, `VCMul`, `VCDecode`, `UseRand`, `RandInvert`, `NightBinary`,
`BaseUV1`, `VOffset`, `GlowOnly`) and look parameters (`Tint`, `CultureColor`, `AccentColor`, `MossColor`,
`MossAmount`, `RoughAdd`, `RoughScale`, `RoughOverride`, `MetalScale`, `MetalOverride`, `Saturation`, `Desaturate`,
`WindowColor`, `EmisColor2`, `NightStrength`, `DayGlow`, `AlwaysStrength`, `GlowColor`, `GlowStrength`, `Specular`).

| Family | M texture | Vertex colour | Culture variation |
|---|---|---|---|
| Age0 | R night, G culture paint, B moss, A embers (always on) | — | climate MIs `_Temperate/_Arid/_Tropical/_Cold` (Tint, moss) + culture colour |
| Age1 | Trim: R night | RGB multiply (sRGB bytes) | MIs `_CliffTerrace/_LagoonWeaver/_RingBuilder` (Valley = base), `_Burnt` |
| Age2 | Trim: R night | RGB multiply | MIs `_A/_B/_C` (tint, saturation; C swaps thatch/shingle/tile to slate) |
| Age3 | Structure G plaster, Detail R night + G paint | RGB multiply (sRGB) | MIs `_Base/_CultureA/B/C`: stone tint, plaster colour on M.G, roof band (V offset 0.25 k), paint |
| Age4 | Trim: R night | R per-window random, G soot, B culture accent | MIs `_Base/_A/_B/_C` (tint, desaturate, buff brick for C) |
| Age5 | Facade: R window rank (lit if rank > 1 - lit), G culture | R soot, G ground occlusion | MIs `_Neutral/_CultureA/B/C` per slot |
| Age6 | R night, G culture tint, B accent | RGB multiply | MIs `_<Mat>_Default/CliffTerrace/LagoonWeaver/RingBuilder` |
| Culture | Trim: R night, G accent, B cloth | — | every instance is culture-specific already (`MI_Culture_*_A/B/C`) |
| Wonder (a) | Detail: R night (fires 1/8 by day), G culture | R flicker phase, G flame | style MIs `_Temperate/_Arid/_Tropical/_Cold` |
| Wonder (b) | R night, G emissive colour A/B, B accent | — | MIs `_A/_B/_C` (base and accent tints) + ornament meshes |
| Prop | — | RGB multiply (bytes), A moss | MIs per material: `_Valley/_CliffTerrace/...`, rock biomes `_Granite/_Sandstone/...`, glow `_Fire/_Oil/_Gas/_Sodium/_LED` |
| Ship, Unit | R night windows, G culture tint, B always-on glow | — | `MI_Ship_<Slot>_C0..C5`, `MI_Unit_Detail_<Colour>` (the six culture colours) |
| Animal | — (coat baked into D) | coat colour (ignored by M_DmKit) | biome MIs `MI_Animal_<Asset>_<Biome>` |
| Tree | Bark D + N; leaves and impostors masked | — | none; burn / snow / dry per instance |

Props' rocks and cliffs carry a baked normal and AO on UV0 and the tiling rock colour on UV1: their slots get
per-mesh instances `MI_Prop_Rock_<Asset>` (baked N / ORM) under `MI_Prop_Rock` (`BaseUV1 = 1`).

### 3.3 Night and the planet

Night is computed per pixel from the planet frame, exactly like the towns' own materials: the sun below this
point's local horizon on the round planet, or `NightOverride` when it is ≥ 0. The frame comes from the collection, so
nobody has to push parameters into 400 instances: the towns actor sets `PlanetCenter` and `SunDir` once per frame
(it already computes both for `SetFrame`), and `NightOverride` / `Glow` where it sets them today.

---

## 4. Mapping: simulation → meshes

Vocabulary: **age** `0 Stone, 1 Bronze, 2 Iron, 3 Sail, 4 Industry, 5 Atomic, 6 Space`; **tier** 0 camp .. 4
metropolis; **style** = the towns planner's climate style (`EStyle`: Temperate, Arid, Tropical, Cold, from the
settlement's temperature and moisture); **culture style** = which of the four art cultures a culture builds in
(4.2). Every building asset has the states Intact / Construction / Damaged / Ruin on one footprint (some add Burnt).
Mesh names below drop the `SM_` prefix and the `_<State>` suffix.

### 4.1 Towns kit (`EDmTownMesh`, variant) → asset by age

| Kit id (variants today) | Planner uses it | Age 0 | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|---|---|
| Tent (3) | camps, tier 0 | Age0_LeanTo_A (v0, v2), Age0_Hut_C (v1) | | | | | | |
| Hut (4) | age 0-2 | Age0_Hut_A/B/C (v0-2), Hut_B (v3) | same | same (tropical) | | | | |
| Longhouse (2) | age 0-2, cold | Age0_Longhouse_A/B | Age0_Longhouse_A/B | Age2_House_B | | | | |
| MudHouse (4) | age 1-3, arid | | Age1_House_A/B/C/D | Age1_House_A-D (Age2 tints) | Age1_House_A-D | | | |
| Cottage (6) | age 1-5 | | Age1_House_A/B/D | Age2_House_A/B/C/D | Age2_House_A-D (old core) | Age2_House_A-D (fringe) | Age2_House_D (farm) | |
| Townhouse (6) | age 2-5 cores | | | Age2_House_C | Age3_Townhouse_A-D (v4, v5 = A, C mirrored by yaw) | Age3_Townhouse_A-D | Age3_Townhouse (old core) | |
| Rowhouse (4) | age 4-6 | | | | | Age4_Terrace_A/B/C/D | Age4_Terrace_A-D | Age4_Terrace (old core) |
| Apartment (4) | age 4-6 | | | | | Age4_Terrace_C/D (grid) | Age5_Apartment{Brick,Point,Slab}_{Low,Mid,High} | Age6_GreenBlock_A/B/C/D |
| Tower (5) | age 5-6, tier ≥ 3 | | | | | | Age5_Office{Glass,Ribbon,Round}_{Low,Mid,High} | Age6_ArcologyTower_A..E (E = capital core) |
| Villa (4) | age 5-6 suburbs | | | | | | **gap** | **gap** |
| Barn (3) | fringe farms ≤ age 4 | | Age2_House_D | Age2_House_D | Age2_House_D | **gap** | | |
| Campfire (1) | camps | Age0_FirePit_A | | | | | | |
| Stones (1) | shrine at age 0 | Age0_StandingStones_A | | | | | | |
| Shrine (2) | b.shrine | | Shrine (v0) | Shrine (v0) | Shrine (v1) … | | | |
| TempleEarly (2) | b.temple age ≤ 1 | Age1_Temple_A + Age1_TempleCourt_A | same | | | | | |
| TempleClassic (2) | b.temple age 2-3 | | | Age2_Temple (v0) | Age3_CivicDome (v1) | | | |
| TempleLate (2) | b.temple age ≥ 4 | | | | | Age3_CivicDome (**gap**: no modern temple) | | |
| Granary (2) | b.granary | Age0_StoragePit_A/B | Age1_Granary_A | Age1_Granary_A | Age1_Granary_A | **gap** (v1 silo) | | |
| Market (2) | b.market age ≤ 3 | | | Age2_Market + Age2_MarketStall_A/B/C | same | | | |
| MarketHall (1) | b.market age ≥ 4 | | | | | **gap** | | |
| WallSeg (3) | b.walls | Age0_Palisade_Seg | Age1_Rampart_Straight (towns), Age1_Palisade_Straight (villages) | Age2_WallSeg | Age2_WallSeg (**gap**: no Sail wall) | | | |
| WallTower (3) | b.walls | | Age1_Rampart_End | Age2_WallTower_A | Age2_WallTower_B | | | |
| Gatehouse (2) | b.walls | Age0_Palisade_Gate | Age1_Gate_A | Age2_Gatehouse | Age2_Gatehouse | | | |
| Pier (2) | b.port | | Age1_Landing_A | Age3_Pier | Age3_Pier (+ Age3_Quay_Straight/Corner/Stairs frontage) | Age3_Quay_* (v1) | | |
| Lighthouse (1) | ports age ≥ 2 | | | Age3_Lighthouse | Age3_Lighthouse | | | |
| Crane (1) | ports age ≥ 4 | | | | Age3_PortCrane | Age3_PortCrane (**gap**: modern crane) | | |
| Warehouse (3) | docks, industry | | | | Age3_Warehouse_A/B (v0) | Age4_Warehouse_A/B (v1) | Age4_Warehouse_B (v2) | |
| Factory (3), Stack (2) | b.factory age ≥ 4 | | | | | Age4_Factory_A/B, Age4_Stack_A | same | |
| MineHead (2), Spoil (1) | b.mine | | **gap** (v0) | **gap** | **gap** | Age4_MineHead_A (v1), Age4_Spoil_A/B | | |
| Academy (2) | b.academy | | | Age2_Academy (v0) | Age2_Academy | | Age5_University_Std (v1) | |
| Reactor (1), CoolingTower (1) | b.plant age ≥ 5 | | | | | | Age5_PowerPlant_Std, Age5_CoolingTower_Std, + Age5_Switchyard_Std, Age5_Pylon_Std | |
| Gantry (1), Rocket (1) | b.launch | | | | | | | Age6_LaunchPad, Age6_Rocket (Z scaled by ark progress), + Age6_AssemblyHall, ControlBunker, Crawlerway (layout in the age6 manifest `launch_site_layout`) |
| Wonder (5) | b.wonder | Wonder_Hearth | Wonder_GreatHive | Wonder_SevenWatchers | Wonder_SeaGate | Wonder_ChoirOfChimneys | Wonder_EarOfTheSky | Wonder_Ladder (+ Ladder_Tether, Climber) |
| Ruin (3) | dead towns | each asset's own `_Ruin` (6.1); generated Ruin only for kit ids without an asset | | | | | | |
| Scaffold (1) | new buildings | each asset's own `_Construction` (6.1) | | | | | | |
| Windmill (1) | age 3 fields | | | | **gap** | | | |
| Boat (2) | moorings, sea units | Ship_ReedBoat_A (v0) | Ship_Canoe_A (v1); Age1_ReedBoat_A/B as moored props | Ship_Canoe_A | | | | |
| Galley (1) | age 1-2 | | Ship_Galley_A | Ship_Galley_A | | | | |
| SailShip (2) | age 2-4 | | | Ship_Cog_A (v0) | Ship_Cog_A / Ship_SailShip_A (v1) | Ship_SailShip_A | | |
| Steamer (1) | age 4 | | | | | Ship_Steamship_A | | |
| CargoShip (2) | age 5-6 | | | | | | Ship_ContainerShip_A (v0) | Ship_FastFreighter_A (v1) |

Not in the kit yet (new ids or placement rules for the towns owner): Age1 fields (`Field_Ripe/Growing/Ploughed/Stubble`,
close-range crop meshes over the ground overlay), Age3 quays, Age4 bridges and the railway station, Age5 highways
(`HighwayGrade/Viaduct/Interchange`, 60 m along X), Age6 maglev (`MaglevTrack_Straight/Curve`, `MaglevStation_A`) and
solar fields, the age-specific street lamps (`Prop_Lamp_Age0..6`), fences, hedges, carts, goods, statues, signs.

Variant choice inside a cell (several assets for one kit variant): the planner keeps its hash `Hv`; the factory
holds the list per (kit id, variant, age) and picks `Hv % list.Num()`. Age5 apartments and offices pick the height
class from the tier (tier ≤ 2 Low, 3 Mid, 4 High), the style from the hash.

### 4.2 Cultures

The simulation has cultures with a colour slot (six Okabe-Ito colours) and traits (piety, aggression, curiosity,
seafaring), but no building style. The art has four styles: **Valley** (the default; Age1 "Valley", Age2 "Valley",
Age3 "Base", Age4 "Base", Age5 "Neutral", Age6 "Default"), **A Cliff-Terrace** (warm canyon), **B Lagoon-Weaver**
(lagoon, piles) and **C Ring-Builder** (cold highland). Proposed rule, client-side and deterministic, computed once per
culture from its capital (no sim change):

| Culture style | When |
|---|---|
| B Lagoon-Weaver | traits.seafaring ≥ 0.62 and the capital is coastal (port) |
| C Ring-Builder | the capital's style is Cold, or its elevation is in the top quarter of the land |
| A Cliff-Terrace | the capital's style is Arid, or its mean slope is steep (above the planner's terrace threshold) |
| Valley | otherwise |

A daughter culture (`parent ≥ 0`) keeps its parent's style. What a style changes, per age:

- **Material instances** on the town's components: Age1 `MI_Age1_*_{CliffTerrace,LagoonWeaver,RingBuilder}`, Age2
  `MI_Age2_*_{A,B,C}`, Age3 `MI_Age3_*_Culture{A,B,C}`, Age4 `MI_Age4_*_{A,B,C}` (`_Base` for Valley), Age5
  `MI_Age5_*_Culture{A,B,C}`, Age6 `MI_Age6_*_{CliffTerrace,LagoonWeaver,RingBuilder}`, props
  `MI_Prop_{Wood,Stone,Cloth}_<Style>`, wonders-b `MI_Wonder_*_{A,B,C}`.
- **Overlay meshes** (Age2 and Age3 houses, `art/culture`): A adds `Culture_A2RoofHouse<X>` / `A3RoofTownhouse<X>` and
  door ornaments; B adds `B2RoofHouse<X>` + `B2StiltsHouse<X>` (raise host and stilts 1.8 m together over water) and
  walkways, bridges, rafts, breakwater; C replaces the host body with `C2DrumHouse` / `C3DrumHouse` + `C*RoofCone`
  and adds ring walls and standing stones. Overlays are modelled in host space: same transform as the host, same
  state (host `_Damaged` ↔ overlay `_Damaged`). Ornaments are Intact only: show them with Intact and Damaged hosts.
- **Temples by culture:** A `Culture_ATempleNiche`, B `Culture_BTempleSpiral`, C `Culture_CSpire`, Valley the age's temple.
- **Shrine:** `Shrine_CultureA/B/C` for A, B, C; `Shrine_Intact` for Valley.
- **Wonder ornaments:** wonders-b `<Wonder>_CapA/B/C`, `_CollarA/B/C`, `_CrownA/B/C`, `_CanopyA/B/C`, `_DiscA/B/C`
  on the wonder's socket.
- **Age1 ornaments:** LagoonWeaver `Age1_Orn_GableHorns`, RingBuilder `Age1_Orn_Finial` on the ridge sockets.
- **Age5 crowns:** `Age5_Crown_Culture{A,B,C}` on `SOCKET_Crown` of any tower.

The culture **colour** (one of six) is separate from the style: it tints cloth, paint and unit liveries through
`CultureColor` (ships and units by instance: 6.3).

### 4.3 Climate styles

`EStyle` from the planner selects the climate instances where a family has them: Age0 `MI_Age0_*_{Temperate,Arid,
Tropical,Cold}`, wonders-a `MI_Wonder_*_{Temperate,Arid,Tropical,Cold}`. Elsewhere the style already drives the
planner's palette, which arrives as the wall / roof tint (custom data 0..5) and stays a 30 % modulation.

### 4.4 States

| State | Today (generated) | With assets |
|---|---|---|
| Intact | the kit mesh | `<asset>_Intact` |
| Construction | a separate Scaffold mesh for new keys (`FDmTownPlan::NewKeys`) | the asset's own `_Construction` for the construction time, then `_Intact` |
| Damaged | none (state custom data darkens) | `_Damaged` for a share of the buildings after war, quake, meteor, lightning or a siege near the town (a new `FDmTownInput::Damage` 0..1, set by the towns actor from the sim events it already receives) |
| Ruin | generic Ruin mesh scaled to the footprint | the asset's own `_Ruin` (keeps the footprint and the street) |
| Burnt | darkening only | Age0 and the shrine have `_Burnt`: use it when the cause is fire (lava, lightning, war); others `_Ruin` + state 1 |
| Drowned | none | the shrine `_Drowned` for `Cause::Drowned / Sea / Wave`; others are under water anyway |

### 4.5 Units (drawn by `ADmTowns::UpdateUnits`)

| Sim | Age 0-1 | 2-3 | 4-5 | 6 |
|---|---|---|---|---|
| Settler (land) | Unit_Settlers_Ancient | Unit_Settlers_Classical | Unit_Settlers_Modern | Unit_Settlers_Space |
| Trader (land) | Unit_Traders_Ancient | Unit_Traders_Classical | Unit_Traders_Modern | Unit_Traders_Space |
| Army (land) | Unit_Army_Ancient | Unit_Army_Classical | Unit_Army_Modern | Unit_Army_Space |
| Prophet | Unit_Prophet_A | same | same | same |
| any unit at sea | age 0 ReedBoat_A, 1 Galley_A | 2 Galley_A / Cog_A, 3 SailShip_A | 4 Steamship_A, 5 ContainerShip_A | FastFreighter_A |

Decorative: `Unit_Aircraft_Prop` (age 5) and `Unit_Aircraft_Jet` (5-6) on air routes between large towns, with
`Unit_GroundRing_A` below (x1.9) at far zoom; launch: `Unit_Rocket_A_Pad` → `Unit_Rocket_A_Launch`; the Ark
`Unit_Ark_A_Construction / _Intact / _Damaged`. The units manifest's `suggested_unit_scale` (0.71 for the jet) is the
readability scale the concept sheet used; the drawing already scales units with distance (1.5-20x).

### 4.6 Trees per biome (nature)

The scatter picks a kind from temperature and moisture weights (`DmNatureScatter.cpp`, `Wk[0..5]`), which follows
the biomes:

| Kind (`EDmTreeKind`) | Biomes where it dominates | Assets (variant 0-3) |
|---|---|---|
| Conifer | Taiga, cold Forest, Tundra edge, highlands | Tree_Conifer_0 mature spruce, _1 old tall spruce, _2 young spruce, _3 fir spire |
| Broadleaf | Forest, Wetland, Grassland groves, Rainforest canopy | Tree_Broadleaf_0 mature oak, _1 tall beech, _2 young, _3 old spreading oak |
| Palm | hot coasts, Beach, Rainforest edges | Tree_Palm_0 coconut, _1 young, _2 tall leaning, _3 coastal curve |
| Acacia | Savanna, dry Grassland | Tree_Acacia_0 umbrella thorn, _1 two-tiered, _2 young, _3 multi-stemmed |
| Shrub | forest edges, open land, Tundra, Desert margins | Tree_Shrub_0 round, _1 low spreading, _2 tall upright, _3 small |
| Dead | burnt land, deserts | **gap**: keep the generated dead tree (no asset) |

Nature draws 4 variants per kind already (`NumVariants`), so the assets drop in 1:1.

### 4.7 Wildlife (nature critters)

| Critter | Biome | Asset (biome instance) |
|---|---|---|
| Grazer | Savanna | Animal_Grazer_Wildebeest (Savanna), Animal_Grazer_Antelope (Savanna); predators Animal_BigCat_Lion/Lioness (Savanna) |
| Grazer | Grassland / steppe | Animal_Grazer_Bison (Steppe / Prairie), Wildebeest (Steppe) |
| Grazer | Desert, dry | Antelope (Arid), Wildebeest (Dry), BigCat (Desert) |
| Grazer | Forest, Taiga, highlands | Animal_Deer_Stag / Doe (Temperate / Boreal / Highland) |
| Grazer | Tundra | Bison (Tundra), BigCat (Snow) |
| Bird | land, murmurations over shallows | Animal_Bird_Starling (Default / Rook), flock scale 3 |
| Bird | wetlands, deltas, V flights | Animal_Bird_Egret (Default / Grey) |
| Fish | shoals, coasts | Animal_Fish_Sardine (Default / Tropical), shoal scale 3 |
| (new) | open ocean | Animal_Whale_Humpback (Default / Cold); calf = same mesh at 0.45 |

The animals face +X after import (normalised), so herds keep their current orientation. Birds and fish stand-ins
faced +Y: their circle-path material assumes +Y, so either the critter factory yaws the new meshes 90° in the
instance transform or the material's path axis changes (animals README).

### 4.8 Props (towns placement)

Lamps by age along lit streets (`Prop_Lamp_Age0..6`, glow MIs `_Fire/_Oil/_Gas/_Sodium/_LED`), fences by age and
style along fields (`Fence_Wattle` age 0-1, `Fence_Rail`, `Fence_DryStone` Ring-Builder and cold, `Fence_Iron` age
4+), hedges (temperate fields), carts and goods on quays and markets, hay at farmsteads, statues on plazas
(`Statue_Totem` 0, `_Idol` 1-2, `_Obelisk` 2-3, `_Figure` 3-4, `_Monument` 4+), signs at crossroads, rocks and cliffs
as scatter on steep ground (nature or planet owner, with the rock biome MIs).

### 4.9 Gaps (no asset)

Villa (age 5-6 suburbs), Windmill (age 3), MarketHall (age ≥ 4), TempleLate (age ≥ 4), Granary v1 (modern silo),
MineHead v0 (age < 4), Barn at age 4, Sail-age town walls, a modern harbour crane, the dead tree. Keep the generated
stand-in for each until an asset exists (the factory falls back per id, 6.1). These are the asset engineers' next
queue; the Blender scripts and the conventions in ASSETS.md apply unchanged.

---

## 5. LOD, instancing and material-slot rules

### 5.1 LOD screen sizes (the importer's `game` profile)

UE's screen size is the bounds sphere's projected diameter over the screen height. The god camera has a 35° vertical
field of view, so a mesh of bounds radius R metres at distance d metres has screen size 3.17 R / d. The profile
switches LODs by projected size, so every class keeps its detail where it can be seen:

| Class | LOD1 below | LOD2 below | LOD3 below | e.g. a 7 m house: LOD1 / LOD2 / LOD3 from |
|---|---|---|---|---|
| building, culture overlay | 0.12 (~130 px) | 0.04 (~43 px) | 0.012 (~13 px) | 185 m / 555 m / 1.85 km |
| wonder | 0.16 | 0.05 | 0.015 | (80 m radius) 1.6 km / 5 km / 17 km |
| prop, mover | 0.10 | 0.035 | 0.012 | |
| animal | 0.08 | 0.025 | 0.008 | |
| tree | 0.22 | 0.075 | 0.025 (impostor) | (10 m crown) 145 m / 420 m / 1.27 km |

`-LodProfile art` keeps the manifests' values (for close-up lookdev). The towns' own generated meshes use 1 / 0.006 /
0.0018, which only works because their LOD1 is "massing plus windows" and the component LOD is per settlement (5.2).

### 5.2 Instancing (the decision for the towns owner)

The towns draw one plain `UInstancedStaticMeshComponent` per (settlement, mesh, variant), because a HISM loses
per-instance custom data when its tree rebuild re-sorts instances (measured in 5.7, `towns-cdtest`). A plain ISM picks
**one LOD for the whole component** from the component's bounds (the whole settlement). With the generated kit that is
harmless. With real LOD chains a city renders LOD0 everywhere until the city is small on screen. Two fixes, in
order of preference:

1. **Nanite for building-class meshes** (`import_assets.ps1 -Nanite`). Nanite picks detail per cluster, keeps
   per-instance custom data, and its shadows are cheap under virtual shadow maps, which the project uses. The LOD
   chain then only serves as fallback and for ray tracing. The materials qualify: opaque, no world-position offset.
   Check one thing before switching: the towns-cdtest scenario with a Nanite mesh (custom data and window lights).
2. **Cell components:** split each settlement's instances per (mesh, variant, 150 m cell). Per-component LOD then
   follows the cell. It costs about 4-10x the components of today: still under the draw budget in 5.4.

Either way: `NoCollision`, `bAffectDistanceFieldLighting = false` for small meshes, `ShadowCacheInvalidationBehavior
= Rigid`, and cull distances per class (towns `CullM`, nature `CullM`) stay as they are.

### 5.3 Material slots

- At most three slots, `Structure`, `Roof`, `Detail` (trees: `Bark`, `Leaves`, `Impostor`). The factory never relies
  on slot order: it sets materials by **slot name** (`UStaticMesh::GetMaterialIndex(FName)`), because the FBX names the
  slots after the family materials and some meshes drop an empty slot.
- Today's towns sections are body / glass / glow. Real assets fold glass into the mask texture (night windows) and
  have no glow section: light pools and flames stay with the generated glow meshes or move to the fx module.
- Culture and climate are **component-level material overrides** (`SetMaterial` on the town's ISM), never a different
  mesh: every town is its own set of components already, so this costs no instancing. Pool reuse must reset the
  overrides (`TakePooled` hands out components that showed another mesh).
- Per-instance variation stays in custom data: wall and roof tint, lit fraction, state.

### 5.4 Performance budget per view (1080p, RTX 3060, 60 fps)

The frame is 16.7 ms. The towns, nature and units together get **5 ms of GPU** (base pass, shadows, Lumen surface
cache updates) and **1.5 ms of game thread** per frame (instance updates, plans applied on quiet frames as today).

| View | Eye height | What is drawn | Triangles (main view) | Shadow casters | Components (draw groups) | Instances |
|---|---|---|---|---|---|---|
| Close | 0.3-1.5 km | 1-2 towns, their fields, units, a few km² of forest and herds | ≤ 3.0 M | ≤ 1.5 M (VSM, within 2 km) | ≤ 1,500 | ≤ 60 k |
| Medium | 1.5-20 km | 5-25 towns, forests to the horizon | ≤ 2.5 M | ≤ 1.0 M | ≤ 2,500 | ≤ 250 k |
| Orbit | > 100 km | planet, clouds, city lights; towns, trees and units culled | ≤ 0.3 M | none | ≤ 200 | 0 |

How the assets fit (manifest averages, LOD0 / LOD1 / LOD2 / LOD3 triangles): houses 1.4-4.2 k / 0.6-2.6 k / 0.15-0.9 k
/ 20-190; wonders 16-30 k / 6-8 k / 2 k / 0.3-0.6 k; ships 2.8 k / 1.1 k / 0.4 k / 0.24 k; units 2.0 k / 0.8 k / 0.4 k /
0.1 k; trees 8.4 k / 3.4 k / 1.0 k / 12. With the game profile a city of 1,500 buildings seen from 1.5 km is mostly
LOD2 (~0.5 k each): 0.75 M triangles plus its wonder and civic buildings at LOD1: about 1 M. **Trees are the risk**:
a conifer is 21 k / 9.4 k / 2.1 k triangles. A forest view with 20,000 trees inside 1.3 km would be 20-40 M triangles
on LOD2. The nature owner must keep the impostor (LOD3, 12 triangles) for everything beyond about 600 m, which means
tree screen sizes of about 1.0 / 0.35 / 0.12 / 0.05 for forests (a per-component override, as today's `ForcedLod`), and
cap LOD0 / LOD1 trees to the nearest few hundred.

Texture memory: 832 MB if every texture were resident at full size; texture streaming keeps what is seen. Wonders
(2048² sets) are 469 MB of it and only the built ones are ever in view. Pool budget: keep `r.Streaming.PoolSize` at
the project default (or 1500 MB on 12 GB cards).

---

## 6. Factory changes, per module owner

Nothing below was changed by this track: the code belongs to the towns, nature and units owners, the content to
whoever runs the import on main.

### 6.1 Towns (`Game/Towns`)

1. **Kit table.** In `UDmTownsFactory::Build()`, before generating a kit id, look up an asset list for (id, variant):
   a static table `{ EDmTownMesh, variant, age range, culture style, asset name }` from 4.1 / 4.2, loaded with
   `LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Assets/<Family>/Meshes/<Name>.<Name>"))`. Keep the generator as the
   fallback for every id the table does not cover (4.9). `FDmKitInfo` gets its footprint from the mesh bounds
   (W = extent X x 2, D = extent Y x 2, H = max Z), `Tris` from LOD0, `bLit` from the base instance's `NightSel`.
2. **Age in the key.** The kit is keyed by (id, variant) today and the age chooses the variant in the planner. Add the
   age (and the culture style) to the batch key: `MeshKey = id << 8 | variant` becomes a struct
   `{ id, variant, age, style, state }` mapped to a mesh pointer, so Age2 and Age3 cottages are different meshes.
3. **States.** Keep one mesh per state (the assets have them on the same footprint): Scaffold → the asset's
   `_Construction` for new keys, the dead-town pass → the asset's `_Ruin` (drop the generic Ruin scaling for kit ids
   that have an asset), a new `FDmTownInput::Damage` → `_Damaged` for a hashed share of buildings.
4. **Materials.** Components get the culture style's instances as overrides (`SetMaterial(Index, MI)` by slot name),
   reset in `ReturnPooled` / `TakePooled`. Remove nothing from the generated path: the generated meshes keep
   `M_TownBody / Glass / Glow`.
5. **Frame.** In `SetFrame`, `SetNightOverride` and `SetGlow`, also write `MPC_DmAssets` with
   `UKismetMaterialLibrary::SetVectorParameterValue(World, MPC, "PlanetCenter" | "SunDir", ...)` and the scalars
   `NightOverride`, `Glow` (window glow).
6. **LOD.** Either import with `-Nanite` and keep plain ISMs (preferred, test first with towns-cdtest), or split
   components per 150 m cell (5.2). Remove the generated kit's 1 / 0.006 / 0.0018 assumption from any shared code.
7. **Overlays.** For a culture style A / B / C on Age2 and Age3 houses, add the overlay mesh with the host's transform
   and state (art/culture README); C replaces the host body with the drum house. Ornaments only with Intact / Damaged.
8. **Sockets.** Age4 / Age5 meshes carry `SOCKET_Smoke_*` (smoke anchors: feed `FDmTownPlan::Smoke` from them instead
   of the hard-coded chimney list), `SOCKET_Ornament_*` and `SOCKET_Crown_*` (culture crowns).
9. **Units.** In the unit drawing, map (type, age, sea) per 4.5 to the units-land and ships meshes; write the culture
   colour into custom data 3..5 as today (the unit and ship instances read it as `CultureColor` once the base
   instance sets `CultureSel`; proposed: floats 8..10, 3.1). Movers face +X after import: no change to the matrix.

### 6.2 Nature (`Game/Nature`)

1. **Trees.** In `UDmNatureFactory::Build()`, load `/Game/Assets/Tree/Meshes/SM_Tree_<Kind>_<v>` for Conifer,
   Broadleaf, Palm, Acacia and Shrub (variants 0-3); keep the generated Dead tree. `FDmNatureMeshInfo.Height` and
   `Radius` from the manifest (height, crown_radius) or the bounds.
2. **Materials.** The imported trees use `MI_Tree_Bark_<Kind>` (opaque, `CdMode 1`), `MI_Tree_Leaves` and
   `MI_Tree_Impostor` (masked foliage). They read burn / snow / dry from custom data 0..2 like today. They have **no
   wind** yet: the nature owner either adds the wind world-position offset to `M_DmKitFoliage` (re-run the importer
   with `-OnlyMats` after editing the script) or parents the tree instances to `M_NatureLeaf` (its wind reads UV1
   weights the Blender trees do not write; check `art/trees/treelib/export.py` first).
3. **LOD.** Use the tree profile (5.1) for single trees and a forest override (about 1.0 / 0.35 / 0.12 / 0.05)
   beyond the near ring; keep `ForcedLod` for far tiles, and the shadow distance rule (only near tiles cast).
4. **Critters.** Grazer → the biome's herd species (4.7), with the biome instance; birds and fish need a 90° yaw in the
   instance transform (or the path axis in the material). The critter motion stays in the material: the Blender
   animals write the same UV1 weights (`AnimWeights`) as the stand-ins.
5. **Frame.** `SetPlanetFrame` also writes `PlanetCenter` into `MPC_DmAssets` (snow on top of crowns uses it).

### 6.3 Units (drawn by the towns actor: 6.1 item 9)

The skeletal animals (`/Game/Assets/Animal/Skeletal`) are for close-ups and cinematics only. Crowds stay instanced
static meshes with material motion.

### 6.4 Whoever runs the import on main

1. The tool is already in `Unreal/Scripts/assets/` on main (two new files, identical to the lane's tested copy). Do not
   sync the lane: its `Content/Assets` is a partial test import and its `assetcheck` scenario is lane-only.
2. `import_assets.ps1 -Plan` (must report 0 issues), then `import_assets.ps1` (about an hour for everything on an idle
   machine; `-Families` to split it), then commit nothing from `Content/Assets` until the factories use it.
3. Read `Saved/AssetImport/report.json`: no errors, `scale_check` 1.0 ± 0.06, `facing_err` < 0.08 everywhere.

---

## 7. Test in the lane (2026-11-20)

Lane `Unreal/Lanes/assetimport` (owned list `Scripts/assets/**`, `Content/Assets/**`; nothing synced), built clean.
Every run went through `tools/render_slot.py` on a machine shared with other agents (CPU 75-80 %).

| Run | What | Result |
|---|---|---|
| plan | all families | 825 meshes, 11 skeletal, 418 instances, 358 textures, every FBX on disk planned, **0 issues** |
| import, age2 x 2 | first contact | PASS; facing probe: plain import adds 0°, so buildings import at 180° |
| import, 37 meshes | one or more per family, both skeletal animals, the shrine | PASS, 0 errors; skeletal: 5 animation takes each; shrine via `ue_import.py` without errors |
| import, 24 meshes | wonders-b, age4, age6, units-land, culture, props (4 each) | PASS, 0 errors; probes: age5, age6, culture also 0° (errors 0.000-0.002, mirrored hypothesis rejected) |

Checks per mesh (report.json): LOD counts equal the FBX's LOD children; triangles per LOD equal the manifests'
numbers; heights equal the manifests' within 1 % (the culture overlays and stilts give their Z extent, which the
check accepts); facing error 0.000 wherever the manifest has asymmetric bounds. Two manifest discrepancies found:
`SM_Age1_House_A_Ruin` is 1.39 m tall, its manifest says 2.30 m; age4 states inherit the Intact height in the
asset-level field (the tool now reads the per-state `height`).

Timing: textures 145-155 s for all 358; masters and 418 instances about 70 s; meshes 0.1-25 s each depending on
the machine's load (FBX triangulation of the un-triangulated age0 files and the mesh rebuild after each edit dominate).
Expect **1.5-3 hours for all 825 meshes on the loaded machine**, well under an hour when idle; split it with `-Families`.
Two fixes came out of the test: the session must not generate distance fields (they rebuilt after every mesh edit,
~100 s per mesh), and slot materials must be set with `SetMaterial` or a fresh `StaticMaterial` array (writing the
fetched structs back does not stick; the tool now verifies every slot and fails the run if one is left on the
engine's grid material).

Check scene: a lane-only scenario `assetcheck` (`Source/Demiurge/Game/AssetCheck/DmAssetCheckScenario.cpp` in the
lane, never synced) lines meshes up on a sunlit ground. Every mesh is drawn through an instanced component with the
towns' 8 custom-data floats, and the night shots pin `MPC_DmAssets.NightOverride`, so they exercise the exact
contract the towns factory will use. Shots (`shots/`):

| Shot | Shows |
|---|---|
| `ue-assetcheck-houses.png` | one house per age, Stone → Space (right to left): every material resolved, doors face the camera (front -Y) |
| `ue-assetcheck-close.png` | Age2 house, Age3 townhouse (wall tint from custom data), Age4 terrace at ~70 m: textures, trims, windows |
| `ue-assetcheck-houses-night.png` | the same at night, lit fraction 0.8: per-window random lighting (age4 vertex R, age5 window rank) works through the instance data |
| `ue-assetcheck-civic.png` | granary, Iron temple, Sail dome, Atomic office tower: porticos face the camera |
| `ue-assetcheck-states.png` | Construction / Intact / Damaged / Ruin of Age2 House_A and Age1 House_A on one footprint |
| `ue-assetcheck-culture.png` | Age2 House_A plain, Cliff-Terrace overlay, Lagoon-Weaver roof + stilts (raised 1.8 m), Ring-Builder drum + cone: overlays sit exactly on the host |
| `ue-assetcheck-movers.png` | ship, army, caravan, bison, stag (all bows / heads toward +X), trees, cliff, lamp, barrel |
| `ue-assetcheck-wonder.png` | the Great Hive and the Choir of Chimneys beside a house and the shrine (scale) |

Look notes for the art direction (not import defects): the procedural textures read clean but a little flat and
uniform at 70 m (brick, slate, thatch). That is what the Flow re-texture of the families is for: the tool picks up new
PNGs on the next run, nothing else changes. Pale plaster (the Great Hive) needs a lower albedo or the game's exposure
to avoid clipping in full sun.

First launch after an import: the game builds distance fields and Lumen cards for every mesh it loads (30-35 s each
under load, in parallel) before the first frame; later launches read them from the DDC. Whoever imports on main
should run one warm-up scenario before measuring anything.

---

## 8. Open questions for the lead

1. Nanite for the towns (5.2 option 1) or cell components (option 2)? The import supports both; the towns owner
   should run towns-cdtest with a Nanite mesh before deciding.
2. The culture-style rule (4.2) is a proposal: should the style be stored in the sim (a `Culture::style` written by
   Names at creation) so the chronicle and the UI can name it?
3. The gaps in 4.9: queue them for the asset engineers (Blender), or accept the generated stand-ins for now?
4. Tree wind: extend `M_DmKitFoliage` (this tool) or adapt the Blender tree export to the nature UV conventions?
