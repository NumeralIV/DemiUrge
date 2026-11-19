// LifeLab: generated trees (tapered trunks and branches, layered two-sided leaf cards, 4 LODs).
#pragma once
#include "CoreMinimal.h"
#include "LifeMesh.h"

enum class ETreeKind : uint8 { Conifer, Broadleaf, Palm, Acacia, Shrub, Num };

static constexpr int32 LifeTreeLODs = 4;

struct FTreeBuild
{
	FLifeMesh LOD[LifeTreeLODs] = { FLifeMesh(2), FLifeMesh(2), FLifeMesh(2), FLifeMesh(2) }; // section 0 bark, 1 leaves
	float Height = 10.f;      // metres
	float CrownRadius = 3.f;  // metres
};

/** Builds one tree variant. Section 0 = bark (opaque), section 1 = leaf cards (masked, duplicated back faces). */
void LifeBuildTree(ETreeKind Kind, uint32 Seed, FTreeBuild& Out);

const TCHAR* LifeTreeName(ETreeKind Kind);
