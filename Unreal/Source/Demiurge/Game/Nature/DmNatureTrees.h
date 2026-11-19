// DEMIURGE — Game/Nature/DmNatureTrees.h : generated trees (C4 nature).
//
// Tapered trunks and branches (parallel-transported tubes, root flare), layered leaf cards emitted back to
// back with one shared volumetric normal (crowns shade as volumes), AO in vertex alpha, wind weights in
// UV1, four hand-made LODs. Six kinds (SPEC-UNREAL C4): conifer, broadleaf, palm, savanna acacia, shrub and
// dead tree (bare skeletons and broken snags). Shapes from the LifeLab (docs/lab/vegetation-and-towns.md
// §3); the dead tree is new here. Stand-ins for a later asset pass (SPEC-UNREAL 0.4): only DmNatureFactory
// calls this.
#pragma once

#include "CoreMinimal.h"
#include "Game/Nature/DmNatureMesh.h"

enum class EDmTreeKind : uint8 { Conifer, Broadleaf, Palm, Acacia, Shrub, Dead, Num };

inline constexpr int32 DmTreeLODs = 4;

struct FDmTreeBuild
{
	FDmNatureMesh LOD[DmTreeLODs] = { FDmNatureMesh(2), FDmNatureMesh(2), FDmNatureMesh(2), FDmNatureMesh(2) };   // section 0 bark, 1 leaves
	float Height = 10.f;       // metres
	float CrownRadius = 3.f;   // metres
};

namespace DmNature
{
	/** One tree variant. Section 0 = bark (opaque), section 1 = leaf cards (masked). */
	DEMIURGE_API void BuildTree(EDmTreeKind Kind, uint32 Seed, FDmTreeBuild& Out);
	DEMIURGE_API const TCHAR* TreeName(EDmTreeKind Kind);
}
