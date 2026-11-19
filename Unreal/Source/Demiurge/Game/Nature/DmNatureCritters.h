// DEMIURGE — Game/Nature/DmNatureCritters.h : ground cover and wildlife stand-in meshes (C4 nature).
//
// Clean, smooth-shaded stand-ins for an asset pass (SPEC-UNREAL 0.4, owner direction: no polish on close-up
// meshes). Their animation lives entirely in the materials (Scripts/nature.py), driven by UV1 weights and
// per-instance custom data, so nothing is updated per frame on the CPU:
//   grass tuft  UV1.x = sway weight (metres per unit wind)
//   herd animal UV1.x = head weight (grazing bob), custom data 0..2 = coat tint
//   bird        UV1.x = wing weight (flap), custom data 0 = circle radius (m), 1 = speed (m/s, sign = sense),
//               2 = tint; the instance sits ON the circle, facing along it; the centre is local -X * radius
//   fish        UV1.x = tail weight (wiggle), custom data like the bird; the path dips below and leaps above
//               the instance's plane (the sea surface)
#pragma once

#include "CoreMinimal.h"
#include "Game/Nature/DmNatureMesh.h"

enum class EDmCritter : uint8 { Grass, Grazer, Bird, Fish, Num };

namespace DmNature
{
	/** LOD meshes (1..3) of one critter variant, metres, origin at the feet / centre. Section 0 only. */
	DEMIURGE_API void BuildCritter(EDmCritter Kind, int32 Variant, TArray<FDmNatureMesh>& OutLODs, float& OutSize);
	DEMIURGE_API const TCHAR* CritterName(EDmCritter Kind);
}
