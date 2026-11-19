// DEMIURGE graphics lab: a planet built from an exported simulation snapshot.
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LabPlanet.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

struct FLabPlanetData
{
	int32 N = 0, NTri = 0;
	float R = 100.f, SeaLevel = 0.f, RiverT = 1.f;
	TArray<FVector3f> Pos;
	TArray<uint32> Tris;
	TArray<float> Elev, Temp, Moist, Veg, Flow, Ice, Hab, Slope, Water;
	TArray<uint8> Biome;
	bool Load(const FString& Path);
};

UCLASS()
class PLANETLAB_API ALabPlanet : public AActor
{
	GENERATED_BODY()
public:
	ALabPlanet();

	/** Planet radius in Unreal units (cm). */
	UPROPERTY(EditAnywhere) double RadiusUU = 600.0 * 1000.0 * 100.0;
	/** Height of one simulation elevation unit, in km. */
	UPROPERTY(EditAnywhere) double KmPerElevUnit = 1.5;
	/** Subdivisions of each simulation triangle (k*k sub-triangles). */
	UPROPERTY(EditAnywhere) int32 Subdiv = 4;

	bool Build(const FString& DataPath, UMaterialInterface* LandMat, UMaterialInterface* OceanMat);
	double SeaRadiusUU() const;

	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Land;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Ocean;

private:
	FLabPlanetData D;
};
