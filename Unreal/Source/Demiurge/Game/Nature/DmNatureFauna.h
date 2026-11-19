// DEMIURGE — Game/Nature/DmNatureFauna.h : wildlife placement near the camera (C4 nature).
//
// Herds on grassland, savanna, forest edges and tundra (from the sim's fauna and biome per cell), flocks of
// large birds circling over land and gulls over coasts, fish shoals leaping on shallow, rich seas. Decorative
// and deterministic (hashed by cell): purely visual life, no sim state. Built on a worker for the cells within
// RadiusKm of the camera's ground point; all motion is in the material (DmNatureCritters.h).
#pragma once

#include "CoreMinimal.h"
#include "Game/Nature/DmNatureScatter.h"

namespace DmNatureFauna
{
	/** Batches relative to Center = CenterDir * sea radius (planet frame). */
	DEMIURGE_API TSharedPtr<FDmTileResult, ESPMode::ThreadSafe> Build(const FDmNatureFields& F, const FDmNatureGround& G, const FVector& CenterDir, double RadiusKm, float Density);
}
