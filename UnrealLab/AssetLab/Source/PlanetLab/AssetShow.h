// AssetLab: places imported real assets (art/ pipeline, /Game/Assets) on the LifeLab terrain beside the generated
// stand-in from the towns mesh factory, exactly as the towns module would: one instanced component per mesh with the
// towns' per-instance custom data (0..2 wall tint, 3..5 roof tint, 6 lit, 7 state) plus 8..10 culture colour.
//
//   -assets=SM_Shrine_Intact,standin,...   meshes in a row (names under /Game/Assets/<Asset>/, or "standin")
//   -assetat=x,y      site (metres in the patch frame; default: beside the first village)
//   -assetgap=m       spacing along the row (default 13)     -assetyaw=deg   front direction (default 200: toward -Y-ish)
//   -culture=r,g,b    culture colour (linear) for the cloth slot (default: Umari #e69f00)
//   -assetstate=f     custom data 7 (settlement state)        -assetlit=f    custom data 6 (fire / embers)
//   -water=h          adds a water plane h metres above the site floor (drowned shots)
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AssetShow.generated.h"

class ALifeWorld;
class UInstancedStaticMeshComponent;
class UDmTownsFactory;

UCLASS()
class PLANETLAB_API AAssetShow : public AActor
{
	GENERATED_BODY()
public:
	AAssetShow();
	/** Reads the command line and decides the site (call before the world builds its terrain, so it can be flattened). */
	static bool Wanted();
	static FVector2D ChooseSite(const ALifeWorld* W);
	void Build(ALifeWorld* W, const FVector2D& Site);
	void SetSun(const FVector& SunDirWorld);

	FVector2D Site = FVector2D::ZeroVector;
	TArray<FVector2D> Slots;     // placed positions (patch metres), in -assets order
	TArray<FString> Names;

private:
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Comps;
	UPROPERTY() TObjectPtr<UDmTownsFactory> StandIn;
	UPROPERTY() TArray<TObjectPtr<UObject>> Keep;
};
