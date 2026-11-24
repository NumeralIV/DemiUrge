// DEMIURGE — Game/Towns/DmTownsAssets.h : the real Blender assets behind the towns kit (wave 4 towns, docs/ASSET-INTEGRATION.md 4 and 6.1).
//
// The planner keeps placing kit ids (EDmTownMesh, variant); this table maps (kit id, variant, age, tier) to the imported
// meshes under /Game/Assets/<Family>/Meshes (Unreal/Scripts/assets/import_assets.ps1), with their states (Intact,
// Construction, Ruin), the culture-style and climate material instances of their slots, and footprints from their bounds.
// A kit id with no asset (the gaps of 4.9, or an asset not imported) keeps the generated stand-in: Find returns null.
// Several assets for one (id, variant, age) are alternatives: the planner picks one per building by hash (FDmTownInst::Sub).
// Everything is loaded synchronously once, when the towns actor starts (world load), so nothing loads mid-game.
// -townassets=0 turns the assets off (stand-ins only; A/B tests).
#pragma once

#include "CoreMinimal.h"
#include "Game/Towns/DmTownsKit.h"

class UStaticMesh;
class UMaterialInterface;

/** Building state of an asset (ASSET-INTEGRATION 4.4); the state bits of a towns batch key. */
enum class EDmTownState : uint8 { Intact = 0, Construction = 1, Ruin = 2, Num = 3 };

/** Culture building styles of the art (ASSET-INTEGRATION 4.2). */
enum class EDmCultureStyle : uint8 { Valley = 0, CliffTerrace = 1, LagoonWeaver = 2, RingBuilder = 3, Num = 4 };

/** One imported asset that can stand for a kit (id, variant) at an age. */
struct FDmTownAsset
{
	FString Name;                                   // e.g. Age2_House_A (no SM_ prefix, no state suffix)
	UStaticMesh* Mesh[int32(EDmTownState::Num)] = {}; // null where that state was not imported
	float W = 8.f, D = 6.f, H = 6.f;                // footprint (m, symmetric about the pivot) and height
	int32 Tris = 0;                                 // LOD0 triangles of the intact mesh
	/** Material overrides per slot: index [state][culture style][climate style] -> per slot of THAT state's mesh (null keeps the
	 *  mesh's own). (verifier fix) per state: 43 of the 132 imported assets have _Construction / _Ruin meshes with fewer or
	 *  other slots than _Intact (Age3_Townhouse_A_Construction: Structure, Detail), so the Intact list put roof tiles on them. */
	TArray<UMaterialInterface*> Mats[int32(EDmTownState::Num)][int32(EDmCultureStyle::Num)][4];
	bool HasState(EDmTownState S) const { return Mesh[int32(S)] != nullptr; }
};

class FDmTownsAssets
{
public:
	/** Loads every mapped asset (game thread, once). Keep receives every loaded object (GC roots of the owner). */
	void Build(TArray<TObjectPtr<UObject>>& Keep);
	/** The alternatives for a kit (id, variant) at an age; Tier picks the height class of the Atomic towers and slabs. */
	const TArray<FDmTownAsset>* Find(EDmTownMesh Id, int32 V, int32 Age, int32 Tier) const;
	/** One look: the asset alternative Sub, in a state (falls back to Intact), or null (stand-in). */
	const FDmTownAsset* Pick(EDmTownMesh Id, int32 V, int32 Age, int32 Tier, int32 Sub) const;
	bool IsEnabled() const { return bEnabled; }
	/** Every distinct asset mesh (pool warming). */
	const TArray<UStaticMesh*>& AllMeshes() const { return Unique; }

	int32 NumAssets = 0, NumMeshes = 0, NumMissing = 0, NumMats = 0;
	double Seconds = 0;
	FString MissingList;

private:
	static uint32 Key(EDmTownMesh Id, int32 V, int32 Age, int32 HClass) { return (uint32(Id) << 16) | (uint32(V & 0xff) << 8) | (uint32(Age & 0xf) << 4) | uint32(HClass & 0xf); }
	TMap<uint32, TArray<FDmTownAsset>> Map;
	TArray<UStaticMesh*> Unique;
	bool bEnabled = true;
};
