// DEMIURGE — Game/Core/Placeholder/DmPlaceholderPlanet.h : TEMPORARY planet visual (C0 bridge).
//
// The orchestrator's lab planet (UnrealLab/PlanetLab/LabPlanet.cpp) rebuilt from the live dm::World
// instead of a lab file: subdivided icosphere with shared edges, smoothed relief plus fractal detail,
// albedo from the continuous climate fields, farmland and urban tint from the world, city lights
// from the settlements (population, tier, culture age) and roads, a depth-tinted ocean shell.
// Registered as scene part "planet" at priority -100: the planet module (C1) registers "planet" at
// priority 0 and replaces it everywhere. Rebuilds on dirty elev / color / water (whole mesh, about
// 0.9 s, so a live world rebuilds at most every RebuildEvery = 8 s: this is a placeholder, not the
// LOD planet). Follows UDmSimSubsystem::GetPlanetToWorld() every frame.
// Debug: -overlay=owner|hab|road tints the land.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/Core/DmDirtyRouter.h"
#include "DmPlaceholderPlanet.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
namespace dm { struct World; }

UCLASS()
class DEMIURGE_API ADmPlaceholderPlanet : public AActor
{
	GENERATED_BODY()

public:
	ADmPlaceholderPlanet();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Sub-triangles per sim triangle edge (k*k per triangle). */
	int32 Subdiv = 4;

	void Rebuild();

	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Land;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Ocean;

private:
	void OnDirty(const FDmDirtyFlags& F);
	void OnWorldChanged(const dm::World* W);
	FDelegateHandle DirtyHandle, WorldHandle;
	bool bPending = true;
	bool bForce = true;          // world replaced: rebuild at once
	double LastBuild = -1e9;     // a whole-mesh rebuild costs ~0.9 s: live worlds rebuild at most every RebuildEvery s
public:
	double RebuildEvery = 8.0;
};
