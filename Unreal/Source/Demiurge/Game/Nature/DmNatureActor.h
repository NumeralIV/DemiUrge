// DEMIURGE — Game/Nature/DmNatureActor.h : the nature scene part (C4 nature, SPEC-UNREAL 2).
//
// ADmNature is registered as scene part "nature" (World kind, priority 0). It draws:
//   - the forest canopy over the whole planet as ONE deferred decal (M_NatureCanopy) that samples the
//     canopy texture baked from the world (DmNatureFields): dark crowns, stand and clump mottling, conifer /
//     broadleaf / tropical / dry tints, snow on taiga, faded by pixel footprint. This is what makes forests
//     read as forests from orbit and between and beyond the instanced trees;
//   - instanced trees, shrubs and dead trees in tiles around the camera (HISM per tile and mesh variant,
//     density thinned with distance: 1, 1/2, 1/4, 1/8 within 1.5 / 3 / 5 / 7 km), grass tufts within ~250 m,
//     and wildlife (herds, birds, fish shoals) within a few km; all built on worker threads;
//   - everything follows UDmSimSubsystem::GetPlanetToWorld() (the planet turns).
// Rebuilds: a new / loaded world rebuilds at once; dirty color / objects / elev / water rebake the fields at
// most every RebakeEvery seconds and replace tiles progressively (old tiles stay until their replacement is
// ready, so nothing pops out). Ground: DmNatureGround (provider > placeholder mesh > sim surface).
//
// Switches: -nonature (no actor), -nocanopy, -notrees, -nograss, -nofauna, -naturedensity=<x>,
// -naturedist=<x> (distance multiplier), -naturelog (per-tile build log).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/Core/DmDirtyRouter.h"
#include "Game/Nature/DmNatureScatter.h"
#include "Tasks/Task.h"
#include "DmNatureActor.generated.h"

class UDecalComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UTexture2D;
class UDmNatureFactory;
namespace dm { struct World; }

USTRUCT()
struct FDmNatureTileComps
{
	GENERATED_BODY()
	UPROPERTY() TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Comps;
};

UCLASS()
class DEMIURGE_API ADmNature : public AActor
{
	GENERATED_BODY()

public:
	ADmNature();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- tunables
	float Density = 1.f;
	float DistScale = 1.f;
	bool bCanopy = true, bTrees = true, bGrass = true, bFauna = true;
	double RebakeEvery = 4.0;        // seconds between field rebakes on a live world
	int32 MaxTasks = 6;              // tile builds in flight
	int32 MaxAppliesPerFrame = 3;    // finished tiles turned into components per frame
	double ApplyBudgetMs = 4.0;      // ... and at most this much game-thread time per frame on it (at least one tile)
	float CanopyStrength = 1.f;
	float PlanetCanopyScale = 0.6f;  // canopy decal opacity over the real planet, which paints a forest tint itself
	bool bCanopyStrengthArg = false;
	FLinearColor CanopyFarTint = FLinearColor(0.80f, 1.20f, 0.55f);   // canopy colour multiplier seen from high up (-canopyfartint=r,g,b)
	float CanopyFarStrength = 1.0f;  // canopy decal opacity seen from high up, over any planet (-canopyfar=)
	double WpoDistM = 0.0;           // trees: WPO (wind, camera-facing far cards) disabled beyond this; 0 = never (-naturewpo=)
	double ShadowDistM = 700.0;      // tree tiles nearer than this cast shadows (-natureshadow=<m>)

	// ---- state for scenarios and tests
	/** Fields and ground ready, nothing in flight, every wanted tile at its level. */
	bool IsSettled() const;
	bool HasFields() const { return Fields.IsValid(); }
	FDmNatureFieldsPtr GetFields() const { return Fields; }
	FDmNatureGroundPtr GetGround() const { return Ground; }
	UDmNatureFactory* GetFactory() const { return Factory; }
	FString Describe() const;
	int32 CountInstances(EDmNatureMesh Id) const;
	int32 NumComponents() const;
	/** Every kind and variant in rows on cleared ground around Dir (judging shapes); trees are cleared around it. */
	void BuildLineup(const FVector& PlanetDir);
	void SetCanopyVisible(bool bVisible);
	/** Force a full rebuild (fields, ground, tiles). */
	void RequestRebuild() { bFieldsDirty = true; bForceFields = true; GroundSig = 0; }
	/** Count of ground builds adopted (tests: an elevation / water change must rebuild the ground once it settles). */
	uint32 GetGroundVersion() const { return GroundVersion; }
	bool IsGroundBuilding() const { return bGroundTask; }

private:
	struct FTileState
	{
		FDmTileKey Key;
		int32 Level = -1;                 // level shown
		uint32 FieldsVersion = 0, GroundVersion = 0;
		int32 Instances = 0;
		int32 PerKind[int32(EDmNatureMesh::Num)] = {};
		double LastWanted = 0;
	};
	struct FWant { FDmTileKey Key; int32 Level; double Dist; };
	struct FPending
	{
		UE::Tasks::TTask<TSharedPtr<FDmTileResult, ESPMode::ThreadSafe>> Task;
		int32 Level = 0;
		uint32 GroundVersion = 0;
	};

	void OnDirty(const FDmDirtyFlags& F);
	void OnWorldChanged(const dm::World* W);
	void FollowPlanet();
	void UpdateFields(const dm::World& W);
	void UpdateGround(const dm::World& W);
	void UpdateStreaming(const FVector& CamPlanet);
	void UpdateFauna(const FVector& CamPlanet);
	void ApplyResults();
	void ApplyTile(const FDmTileResult& R, int32 Level, uint32 GroundVersion);
	void ClearTile(uint64 Key);
	void ClearAllTiles();
	void UploadCanopy();
	TArray<UHierarchicalInstancedStaticMeshComponent*> MakeComps(const FDmTileResult& R, bool bFauna, bool bTileShadows = true);
	static int32 LevelFor(EDmNatureLayer L, double DistM, int32 Current, float DistScale);
	bool CameraPlanet(FVector& Out) const;
	bool TileShadowWanted(const FDmTileKey& Key) const;
	void UpdateShadows(const FVector& Cam);

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UDecalComponent> CanopyDecal;
	UPROPERTY() TObjectPtr<UTexture2D> CanopyTex;
	UPROPERTY() TObjectPtr<UTexture2D> ClimateTex;
	UPROPERTY() TObjectPtr<UDmNatureFactory> Factory;
	UPROPERTY() TMap<uint64, FDmNatureTileComps> TileComps;
	UPROPERTY() TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> FaunaComps;
	UPROPERTY() TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> LineupComps;

	TMap<uint64, FTileState> Tiles;
	TMap<uint64, FPending> Pending;
	TArray<FWant> Wanted;
	double LastWantTime = -1e9;
	FVector ShadowCam = FVector(1e30);
	double LastShadowUpdate = -1e9;
	FVector LastWantCam = FVector(1e30);

	FDmNatureFieldsPtr Fields;
	UE::Tasks::TTask<TSharedPtr<FDmNatureFields, ESPMode::ThreadSafe>> FieldsTask;
	bool bFieldsTask = false, bFieldsDirty = true, bForceFields = true;
	double LastFields = -1e9;
	uint32 FieldsVersion = 0;

	FDmNatureGroundPtr Ground;
	UE::Tasks::TTask<FDmNatureGroundPtr> GroundTask;
	bool bGroundTask = false;
	uint64 GroundSig = 0, PendingGroundSig = 0;   // what the built ground was made from: (source << 32) | key
	uint32 GroundEpoch = 0;                       // bumped by every elevation / water change (a counter: a parity flag misses two changes in a row)
	double LastEpochTime = -1e9;                  // when the last elevation / water change came in (the ground waits for them to settle)
	double MaxApplyMs = 0;                        // worst game-thread time of one tile apply (HISM registration), since start
	double MaxTickMs = 0;                         // worst game-thread time of one nature tick, since start
	double LastGroundCheck = -1e9;
	uint32 GroundVersion = 0;
	double FirstTickTime = -1;

	UE::Tasks::TTask<TSharedPtr<FDmTileResult, ESPMode::ThreadSafe>> FaunaTask;
	bool bFaunaTask = false;
	FVector FaunaCenter = FVector(1e30);
	uint32 FaunaFieldsVersion = 0, FaunaGroundVersion = 0;
	int32 FaunaInstances = 0;
	int32 FaunaPerKind[int32(EDmNatureMesh::Num)] = {};

	FTransform LastPlanet = FTransform(FVector(1e30));
	FDelegateHandle DirtyHandle, WorldHandle;
	const dm::World* SeenWorld = nullptr;
	double LastLog = 0;
	bool bLog = false;
	bool bLineup = false;
	FVector LineupDir = FVector::ZeroVector;
	int64 TilesBuilt = 0;
	double TileMsTotal = 0;
};
