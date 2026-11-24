// DEMIURGE — Game/Nature/DmNatureScatter.h : placement rules and tile scatter (C4 nature).
//
// The planet is cut into tiles on an equal-angle cube sphere (face, x, y). Each tile is scattered on a worker
// thread from immutable inputs (fields + ground), deterministically: every candidate is a jittered cell of a
// GLOBAL per-face grid hashed by (face, gx, gy), so the same tree stands in the same place whatever the camera
// does, and a thinned far tile keeps a subset of the near tile's trees (Keep fraction tested on its own hash).
//
// Layers:  Trees  128 tiles per face edge (~1.2 km), 192 cells per tile edge (~6.4 m): trees, shrubs, dead trees
//          Grass  2048 tiles per face edge (~77 m), 64 cells per tile edge (~1.2 m): grass tufts near the camera
// Rules (docs/lab/vegetation-and-towns.md §5, adapted to the planet's fields; numbers are per 6.4 m cell):
//   Canopy = CanopyValue(forest potential) * height and slope terms * settlement clearings
//   Open   = (0.012 + 0.03 V + 0.1 Hot Dry V)        lone trees, savanna
//   P      = Canopy 0.62 (1 - 0.45 Wet) + (1 - Canopy) Open + coast palms + desert / burn snags
//   kinds by temperature, moisture, coast, dryness; burnt land -> charred dead trees; per-instance
//   burn / snow / dry tint, yaw, tilt, scale 0.72-1.22, rainforest emergents.
// Real-scale Blender trees (S.bAssetTrees, docs/lab/trees.md 7 and 12.3): the closed-canopy term is halved (crowns are
// 12-18 m wide), scale jitter 0.85-1.12, broadleaf crowns broadened and flattened a little and the forest-grown domed
// variants preferred under a closed canopy, a wider palm band on hot coasts, and an autumn accent (custom data 3) on
// about 1 temperate broadleaf crown in 25, never in the tropics or the cold.
#pragma once

#include "CoreMinimal.h"
#include "Game/Nature/DmNatureFactory.h"
#include "Game/Nature/DmNatureFields.h"
#include "Game/Nature/DmNatureGround.h"

enum class EDmNatureLayer : uint8 { Trees = 0, Grass = 1 };

struct DEMIURGE_API FDmTileKey
{
	uint8 Layer = 0, Face = 0;
	uint16 X = 0, Y = 0;
	uint64 Packed() const { return (uint64(Layer) << 40) | (uint64(Face) << 32) | (uint64(X) << 16) | uint64(Y); }
	static int32 TilesPerFace(EDmNatureLayer L) { return L == EDmNatureLayer::Trees ? 128 : 2048; }
	static int32 CellsPerTile(EDmNatureLayer L) { return L == EDmNatureLayer::Trees ? 192 : 64; }
	/** Centre direction, and the angular radius (rad) of the tile's corners around it. */
	FVector CenterDir(double* OutAngRadius = nullptr) const;
	static FDmTileKey At(EDmNatureLayer L, const FVector& Dir);
};

struct FDmTileBatch
{
	EDmNatureMesh Id = EDmNatureMesh::Conifer;
	int32 Variant = 0;
	TArray<FTransform> Xf;       // relative to the tile centre (planet frame, uu)
	TArray<float> Custom;        // UDmNatureFactory::NumCustomData per instance
	int32 ForcedLod = -1;        // >= 0: draw this LOD only (lineup diagnostics)
};

struct FDmTileResult
{
	FDmTileKey Key;
	int32 Level = 0;             // density level the tile was built at
	uint32 FieldsVersion = 0;
	FVector Center = FVector::ZeroVector;   // planet frame, uu
	TArray<FDmTileBatch> Batches;
	int32 Candidates = 0, Instances = 0;
	int32 PerKind[int32(EDmNatureMesh::Num)] = {};
	double Ms = 0;
};

struct FDmScatterSettings
{
	float Density = 1.f;          // global multiplier (-naturedensity=)
	float Keep = 1.f;             // fraction of trees kept (distance level)
	bool bShrubs = true;
	int32 MaxVariants = 4;        // far tiles use fewer variants (fewer components)
	bool bLineup = false;         // unused by the scatter; lineup is built by the actor
	bool bAssetTrees = false;     // the factory's trees are the real-scale Blender families
};

namespace DmNatureScatter
{
	DEMIURGE_API TSharedPtr<FDmTileResult, ESPMode::ThreadSafe> BuildTile(const FDmNatureFields& F, const FDmNatureGround& G, const FDmTileKey& Key, const FDmScatterSettings& S);
	/** Rough forest share 0..1 at a direction (for scenarios: find a forest, a savanna, ...). */
	DEMIURGE_API float ForestAt(const FDmNatureFields& F, const FVector& Dir);
}
