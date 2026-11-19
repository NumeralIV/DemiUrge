# Close-up assets with no additional spending

T-003 · Astra · checked **2026-11-19** · owner-directed revision of PR #1.

## Binding direction and recommendation

**Additional asset/tool budget: $0. No paid asset packs, new subscriptions, credit top-ups or contractor purchases.** Use existing tools and entitlements, original Blender work, and genuinely free assets. This owner instruction supersedes the earlier purchase recommendations and cost allowances in this PR. No purchases were made.

The task assigned by Claude was asset-source/licence research, category shortlists, cost estimates and a Blender alternative. It did not authorise spending. Meshy and Tripo belong in the research because generated 3D can supply initial forms and selected props. Their availability in the owner's accounts is not yet verified; existing AI subscriptions do not establish separate service entitlements. No credits have been consumed for this research.

Recommended order:

1. Build coherent original architecture and foliage systems in Blender using existing assistant tools.
2. Use CC0 materials and props where they improve quality without new fees.
3. Evaluate Meshy or Tripo only with already available, export-enabled entitlements or a usable free tier. Make one asset before attempting a library.
4. Judge the imported model in DemiUrge's lighting and camera range. Keep it only if it improves quality and reduces total cleanup work.

This document is research, not a production asset delivery. No candidate was imported, cooked, rendered or benchmarked in UE 5.7. No downloaded asset files are committed. Only `docs/research/assets.md` changes; the lead still owns implementation and integration.

### Project fit

[CONCEPT](../CONCEPT.md) requires Stone → Bronze → Iron → Sail → Industry → Atomic → Space. [SPEC-UNREAL §0.4/C4/C5](../SPEC-UNREAL.md) requires reusable mesh factories, culture/age variation, instancing and distance handling. The [approved orbit image](../../shots/ue-cloudlab-terminator-final.png) establishes natural surfaces and atmospheric scale; the same standard must survive close-ups.

The [LifeLab report](../lab/vegetation-and-towns.md) found conventional foliage LODs faster than its tested Nanite foliage on the RTX 3060, with dense rainforest already over budget. These are the lab author's measurements, not tests performed for this research. A generated or scanned tree still needs wind, LODs and a matching distant canopy. Do not substitute dense individual meshes for the existing planet-scale representations.

## 1. Meshy and Tripo

### What the current plans and licences actually allow

Checked **2026-11-19** against primary sources. Generation, download access, commercial rights and privacy are separate checks. Account-specific allowances are unverified.

| Tool / route | Commercial use and attribution | Export/access limits relevant to $0 additional spend | Decision for DemiUrge |
|---|---|---|---|
| **Meshy Free** | [Terms §3.2][meshy-terms] grants free-plan output under [CC BY 4.0][ccby], with Meshy credit. | [Current free-plan documentation][meshy-free] lists 100 monthly credits and 10 monthly downloads of **untextured Meshy 6 Lite** models. Textured models, other listed model generations and 3D Agent results cannot be downloaded on this tier. Credits alone do not unlock downloads; free API access is unavailable. | A possible source of initial geometry, with materials/cleanup done locally. Do not promise free textured production exports. Check the account's eligible download before generating. |
| **Meshy, if already subscribed / entitled** | [Terms §3.2][meshy-terms] assigns paid output rights to the customer to the extent possible under law; privacy is an available setting. Input rights still matter. | Use only the remaining included entitlement. Supported [export formats][meshy-formats] include FBX and GLB. Confirm model/version and download access in the actual account. | Worth a small image-to-3D or prop test if already available. No upgrade recommendation or automatic API billing. |
| **Tripo Free** | **Official pages conflict.** The [Terms, updated November 13, 2026, §5.2.1][tripo-terms] provide a public waiver/fallback licence allowing commercial exploitation of qualifying Free Outputs without attribution. The [current pricing page][tripo-pricing] still labels Free models non-commercial. This is Tripo's custom grant, not a claimed CC0 licence. | Pricing lists 200 monthly credits and limited exports (15, H2.5 only, in its comparison). Check the live account; these are generation/export ceilings, not a guaranteed count of usable assets. | Potential experiment, but **commercial production clearance remains unresolved** until the applicable account/service terms are reconciled. Do not state categorically that all free outputs are banned or all are cleared. Use Blender/CC0 meanwhile. |
| **Tripo, if already commercially entitled** | [Terms §5.2.2][tripo-terms] permits commercial use of Paid Content subject to input/third-party rights; that status survives subscription expiry. | Confirm entitlement, exports and available credits. Pricing advertises editing, retopology, segmentation and DCC bridges, but features depend on plan. | Worth the same small test if already available. Do not subscribe or top up. Free outputs do not automatically acquire paid status later. |

Meshy's non-Enterprise training policy allows use of inputs/outputs unless opted out as described in [§2.9][meshy-terms]; private output ownership is not itself a no-training guarantee. Tripo excludes paid inputs/outputs from training in [§5.3][tripo-terms]; its free outputs are public by default under §5.2.1. Use original, non-sensitive references and record the generation date, model, plan and applicable terms. A licence to use output is not a guarantee of copyright exclusivity or clearance of third-party designs.

**Recommendation:** prefer whichever service already provides usable exports and the appropriate rights. There is no project-specific evidence yet that Meshy or Tripo produces better DemiUrge assets. If neither is already available, the project still has a complete Blender + CC0 route without another subscription.

### Where generated 3D can help

These are production judgements to test, not measured claims about either vendor's latest model.

| Asset | Potential use of Meshy / Tripo | What still needs deliberate authoring |
|---|---|---|
| Rocks, pottery, baskets, decorative shrine pieces | Good first candidates: compact objects, readable silhouettes, few moving parts. | Remove floaters, inspect the back/underside, correct scale, normals, materials and collision. |
| A complete house or temple | Useful initial form or a distant one-off building. | Consistent doors/storeys, clean roof edges, modular walls, separate windows, foundations, construction/ruin states and culture variants. Blender is the primary tool for repeatable architecture. |
| Trees and foliage | Possible source of a trunk or a visually interesting specimen. | Leaf structure, wind weights, silhouette-preserving LODs, seasonal states and thousands of efficient instances. Prefer controlled Blender families and free botanical sources for forests. |
| Boats and carts | Potential hull/body starting point. | Thin rigging, planks, wheels, sails, pivots and moving parts may need rebuilding. Make one boat, not a complete harbour in one generation. |
| People and wildlife | Possible starting meshes where an existing entitlement includes useful rigging. | Deformation, animation, proportions, materials, LODs and crowd cost require separate review. Neither a good thumbnail nor an auto-rig proves these are finished. |

For cinematic quality, use one original visual reference with clear shape and neutral lighting when suitable. Keep proportions and material palette consistent across generations. Evaluate ordinary roughness/normal response under moving light: painted-in shadows or attractive preview lighting must not conceal poor geometry. Meshy's own [game-readiness guidance][meshy-ready] calls for post-export optimisation; it describes single-mesh exports rather than an automatic complete LOD chain.

### Proposed first experiment: one shrine

Use a small stone-and-timber village shrine with a clear silhouette, separate roof and open doorway. It exercises architecture without spending attempts on an entire settlement.

- Produce an original brief/reference with an already included tool, or a simple Blender blockout if no suitable image entitlement is available.
- Use **one initial generation and at most one revision** on an eligible existing/free account; stop earlier if the account's included allowance would be exceeded. No calls have been made in this task.
- Inspect front, back and underside in Blender; record cleanup time, material count, triangle count and visible defects. Create proper scale, ground pivot, collision and reduced-distance versions.
- Have the lead render it in the actual golden-hour, storm and night scenes beside an original Blender version. Judge silhouette, material response and whether it belongs in the village.
- Expand only if the usable result is worth its generation attempts and cleanup time. Reuse the accepted material/shape vocabulary across the next buildings.

The currently exposed tools include Blender scripting/inspection, but **no dedicated Meshy or Tripo generation connector is exposed in this session**. This does not prove anything about the owner's accounts. Use an existing supported service workflow when available; do not assume a chat subscription includes external API billing. The research task does not authorise generating or importing assets into protected implementation paths.

## 2. Source comparison

All licence checks below: **2026-11-19**. “Ready” means a candidate to validate, not runtime-certified. The table retains the requested coverage while excluding paid purchasing routes.

| Source | Needed content | Visual fit | Additional price | Exact licence / text | Commercial game? | Attribution | Engine, redistribution and AI restrictions | UE 5.7 / Nanite readiness |
|---|---|---|---|---|---|---|---|---|
| Original Blender work | Buildings at every age, ships, props, tree families | Best control of a coherent culture and evolving settlement | $0 tool/asset purchases | [Blender artwork statement][blender-license]; application [GPL v3](https://www.gnu.org/licenses/gpl-3.0.html) does not impose GPL on original artwork | Yes for original output; inputs retain terms | No Blender credit required for original artwork | No engine lock imposed on original art; imported inputs keep their restrictions | Export/material setup needed; we control pivots, material slots and LODs. Nanite evaluated per opaque mesh. |
| [Poly Haven](https://polyhaven.com/models) | Conifers, rocks, forest clutter, props, wood/stone materials | Realistic; reduce overly heavy scans and unify scale/wear | $0 public assets | [CC0 1.0 Universal][cc0]; [publisher declaration](https://polyhaven.com/license) | Yes | None required for assets | No CC0 engine/raw/AI-specific restriction; other rights and website/API terms remain. Preview renders are not blanket CC0. | Generic sources need import/cleanup; Blender nodes do not transfer as UE materials. Foliage needs wind/LOD validation. |
| [ambientCG](https://ambientcg.com/) | Surfaces for roofs, walls, ground and original props | Useful realistic materials; avoid visible tiling | $0 public assets | [CC0][cc0]; [publisher declaration](https://docs.ambientcg.com/license/) | Yes | None required | Raw redistribution allowed under CC0; no engine/AI-specific clause; third-party rights remain | Material/channel setup required; texture sets are not Nanite meshes. |
| Meshy | Generated props, initial building/boat forms | Candidate only; assess consistency and cleanup cost | $0 only within eligible free/existing entitlement | [Meshy Terms][meshy-terms]; free output [CC BY 4.0][ccby] | Yes under the applicable output terms | Free output requires Meshy credit | No UE-only limitation identified; free output subject to CC BY conditions. Training/community terms separate. | FBX/GLB path; no 5.7 or Nanite certification. Free download restrictions above. |
| Tripo | Generated objects, initial forms and editable parts | Candidate only; assess actual exports | $0 only within free/existing entitlement | [Tripo Terms §§5.2–5.3][tripo-terms], custom licence | Paid entitlement permits commercial use; free-page conflict unresolved | Current Free Output grant says none; verify applicable terms | Free outputs public; no UE-only limitation identified. Restrictions on competing-model training and third-party inputs remain. | DCC bridges advertised; specific package/version/export needs verification. No 5.7 benchmark. |
| Free Fab / Quixel entries | Selected realistic trees, scan materials and clutter | Strong detail but may be heavy at forest density | $0 entries only; no paid replacements | [Fab Standard][fab] where selected; old acquisitions may instead use [Epic Content License Agreement][ecla] | Standard permits incorporated game use; verify actual entitlement | Standard does not require publisher credit | Standard is not engine-only; raw redistribution restricted. NoAI and legacy UE-only/education/evaluation terms may matter. | Entry-specific; alder lists only through UE 5.6. Conventional foliage LODs first. |
| [Kenney](https://kenney.nl/assets) | Buildings, nature, vehicles, props for prototypes | Mostly stylized; not the final close-up recommendation | $0 individual asset-page downloads | [CC0][cc0]; [publisher statement](https://kenney.nl/support) | Yes for covered assets | None required | CC0 does not include permission to use the Kenney logo as our branding | Generic import; little reason to select these for a photorealistic Nanite pass. |
| Sketchfab items explicitly CC0 / CC BY | Occasional historic props and boats | Uneven provenance/topology; no specific item approved | $0 downloadable entries only | Exact item grant: [CC0][cc0] or [CC BY 4.0][ccby] | Yes if rights authentic and conditions met | CC0 none; CC BY creator/notices/source/licence/modification credits | No assumption based on title alone. CC BY forbids added restrictions blocking licensed rights. Avoid NC/ND/SA for this shortlist. | Manual cleanup; no automatic LOD, wind or UE compatibility. |

## 3. Shortlist by category and age

| Category | $0 additional-spend route | Remaining work |
|---|---|---|
| Cold/temperate trees | [Poly Haven Pine Tree 01](https://polyhaven.com/a/pine_tree_01), [pine forest collection](https://polyhaven.com/collections/pine_forest/all/collection%3A%20pine_forest), optionally free [Quixel European Black Alder](https://www.fab.com/listings/9de7ce19-5813-42d2-a5f0-5e6447006f72) | Pine uses [CC0][cc0]; alder showed [Fab Standard][fab] and Free on 2026-11-19. Validate LODs/wind/material cost. Alder's listed versions end at 5.6. |
| Savanna/tropical/dead trees | Original Blender acacia, palm and broadleaf families; optional free [Date Palm atlas](https://www.fab.com/listings/9fda02ca-2912-4e7f-bbf6-ea6c05702618) | Atlas showed [Fab Standard][fab] on 2026-11-19; it is a texture input, not a whole tree. Prefer original/CC0 inputs for unrestricted source kits. |
| Stone | Original shelter/frame/thatch/storage/shrine kit | Roof thickness, irregular silhouettes, construction and ruin states. Meshy/Tripo may supply a selected decorative piece. |
| Bronze | Original earth/plaster/stone courtyard and temple/granary modules | Readable civic scale, culture-specific roof and entrance families. |
| Iron | Original masonry, wall, gate and roof kit | Narrow frontages, slope foundations, academy/market identity. |
| Sail | Extend original housing with docks, warehouses and civic buildings | Densification, harbour props, emissive window masks; no purchased medieval kit. |
| Industry | Original brick/metal factory shells, sawtooth roofs, stacks and cranes | Reuse trim sheets and measured modules; model characteristic silhouettes. |
| Atomic | Extend original concrete/steel/brick kit | Towers, civic buildings, power facilities and coherent city districts. |
| Space | Original launch site, gantry, rocket and advanced civic modules | Preserve continuity with earlier towns rather than switch to an unrelated sci-fi style. |
| Ships/vehicles | Original canoes, rowboats, sailing/cargo hulls, carts and wagons; optional eligible generated starting form | Rigging, wheels/sails, material separation, pivots and distant representations. |
| Props | [Wooden Barrels 01](https://polyhaven.com/a/wooden_barrels_01) plus original baskets, pottery, lamps, stalls and tools | Barrels use [CC0][cc0] per [Poly Haven](https://polyhaven.com/license), checked 2026-11-19. Reduce geometry/textures for repeated placement. |
| People/wildlife | Separate small experiment using existing tools and lawful inputs | Rig, deformation, animation and crowd cost remain unvalidated. No paid character/animation budget assumed. |

### Cost and usage control

| Resource | Additional cash allowed | Constraint |
|---|---:|---|
| Blender and existing assistant tools | $0 | Work within installed/available capabilities and subscription usage. |
| CC0 assets/materials | $0 | No paid bundle, early-access purchase or paid add-on dependency. |
| Free Fab assets | $0 | Verify the specific free entitlement and licence; stop if payment is required. |
| Meshy / Tripo | $0 | Existing/free eligible credits and export rights only. No upgrades, top-ups or unverified API spending. |
| Paid asset packs / commissioned art | $0 | Excluded by owner direction. |
| **Total incremental acquisition budget** | **$0** | This does not mean unlimited generations or zero production effort. |

Use bounded samples and reuse successful modules. If a service cannot export the result within the existing entitlement, choose the local route. Do not extrapolate vendor “models per month” into a usable-asset count; rejected generations and cleanup consume the real budget. No paid-labour quote or fixed full-game completion estimate is proposed.

## 4. Blender production workflow

1. Establish one accepted house/shrine/boat/tree set before scaling. Review proportions, palette and materials in golden-hour, storm and night scenes.
2. Agree metres-to-UE-centimetres conversion, ground pivots, footprint bounds, forward axis and sockets with the lead. Preserve the existing mesh factory and curved-planet placement.
3. Build reusable walls, roofs, plinths, openings and trim. Use scripts/Geometry Nodes to vary dimensions and assemble coherent families; keep editable `.blend` sources.
4. Use original or CC0 material inputs and shared trim sheets. Bake procedural shading, preserve normals, and expose culture tint, wetness, snow/ash and night emission. Generated textures require the same checks as other inputs.
5. Separate complete, construction, damaged and ruin states while preserving footprints. This is where controlled authoring is particularly valuable.
6. Make conventional LODs/impostors for foliage and repeated buildings. Keep far canopy colour and crown coverage consistent. Test Nanite on suitable opaque geometry rather than enabling it by default.
7. Let the lead import through repeatable UE content scripts. Check scale, bounds, collision, UVs, material slots, texture memory, wind, shadow behaviour and cooking.
8. Judge images and GPU cost at representative density on the RTX 3060. A service's preview or “game-ready” label is not runtime evidence.

The AI assistants can automate substantial modelling and cleanup work, but need visual feedback. Generated geometry can accelerate isolated forms; deterministic Blender authoring is stronger for exact modules, repeated architecture and change states. Choose based on a usable result, not how quickly a first preview appears.

## 5. Licence and provenance checks

Checked **2026-11-19**; links lead to the applicable legal text or publisher statement.

- **CC0:** commercial use and raw sharing are permitted within the affirmer's rights; trademark/patent/third-party rights are not automatically cleared. Keep a credible source record. [CC0 §§2–4][cc0].
- **CC BY:** retain creator/supplied notices, source and licence links, and modification information. Review packaging/EULA restrictions so recipients retain the licensed rights. [CC BY §§2–3][ccby].
- **Fab/Quixel:** free price is not public domain. Standard allows incorporated games but restricts raw sharing and extraction. Older Megascans plans can have different engine or commercial-use limits. Do not upload NoAI-tagged third-party content into generation workflows on an assumed permission. [Fab EULA §§4–6,16][fab]; [Epic Content License Agreement and Megascans Addendum][ecla].
- **Generated assets:** record the service, generation date/model, account entitlement, export allowance, reference provenance and output licence. Neither remeshing nor a Blender export removes source restrictions. Meshy/Tripo are not assumed interchangeable; the current Tripo free-page discrepancy remains explicit. [Meshy Terms][meshy-terms]; [Tripo Terms][tripo-terms].
- **Community assets:** a public preview is not proof of a commercial grant. A community model, its input image and the service's own interface may have different licences. Use the exact item terms; never assume another user's paid-model gallery grants commercial reuse. [Meshy §3.3][meshy-terms]; [Tripo §5.2.3][tripo-terms].
- **Original Blender work:** Blender's application licence does not require releasing original game art under GPL; third-party inputs and add-on assets retain their own conditions. [Blender artwork statement][blender-license].

Keep a per-asset manifest when implementation begins: source/model ID, author/service, generation/acquisition date, licence text/version, credits/entitlement used, attribution, modifications and test results. Keep account/payment details private. No generated models, reference uploads or asset downloads occurred for this revision.

### Decisions remaining for Claude

Adopt the owner's $0 additional-spend constraint; choose the first culture/age and one close-up test asset; establish available Meshy/Tripo entitlements before any generation; resolve Tripo Free's conflicting official wording if considering it for commercial output. Meanwhile the original Blender + CC0 route remains available. The helper stops after updating the research PR; the lead owns any implementation or board/spec changes.

[cc0]: https://creativecommons.org/publicdomain/zero/1.0/legalcode.en
[ccby]: https://creativecommons.org/licenses/by/4.0/legalcode.en
[fab]: https://www.fab.com/eula
[ecla]: https://www.unrealengine.com/eula/content
[blender-license]: https://www.blender.org/about/license/
[meshy-terms]: https://www.meshy.ai/terms-of-use
[meshy-free]: https://help.meshy.ai/en/articles/15696428-what-is-included-on-the-free-plan
[meshy-formats]: https://help.meshy.ai/en/articles/9991884-what-3d-file-formats-does-meshy-support-full-export-list
[meshy-ready]: https://help.meshy.ai/en/articles/15723950-how-to-make-meshy-models-game-ready
[tripo-terms]: https://www.tripo3d.ai/terms
[tripo-pricing]: https://www.tripo3d.ai/pricing
