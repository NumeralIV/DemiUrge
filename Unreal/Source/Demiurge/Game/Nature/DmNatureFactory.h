// DEMIURGE — Game/Nature/DmNatureFactory.h : the nature mesh factory (C4 nature, SPEC-UNREAL 0.4).
//
// Every visual of the nature module comes from here as (mesh id, variant) -> UStaticMesh + metadata, so an
// asset pass (Fab / Blender meshes) is a one-place change: replace Build() for an id with a LoadObject of the
// imported mesh (keeping the material slot order: 0 bark / body, 1 leaves) and the scatter, LOD, culling,
// wind and per-instance data keep working. Materials come from Scripts/nature.py (/Game/Nature/M_*),
// parameterised through dynamic instances; textures are painted at startup (DmNatureTextures).
//
// Per-instance custom data (all nature meshes, NumCustomData floats):
//   trees / shrubs / grass:  0 burn (0..1, charred), 1 snow (0..1, snow on top of crowns), 2 dry (0..1, autumn / dry-season tint)
//   grazer:                  0..2 coat tint (multiplier)
//   bird / fish:             0 circle radius (m), 1 speed (m/s, sign = sense), 2 tint (multiplier)
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Game/Nature/DmNatureTextures.h"
#include "DmNatureFactory.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;

enum class EDmNatureMesh : uint8
{
	Conifer, Broadleaf, Palm, Acacia, Shrub, Dead,   // EDmTreeKind order
	Grass, Grazer, Bird, Fish,                       // EDmCritter order
	Num
};

struct FDmNatureMeshInfo
{
	UStaticMesh* Mesh = nullptr;
	float Height = 1.f;     // metres (trees: top of the crown)
	float Radius = 1.f;     // metres (crown / body radius)
	int32 Tris = 0;         // LOD0 triangles
};

UCLASS(Transient)
class DEMIURGE_API UDmNatureFactory : public UObject
{
	GENERATED_BODY()

public:
	static constexpr int32 NumCustomData = 3;
	static int32 NumVariants(EDmNatureMesh Id);
	static const TCHAR* Name(EDmNatureMesh Id);
	static bool IsTree(EDmNatureMesh Id) { return uint8(Id) <= uint8(EDmNatureMesh::Dead); }

	/** Paints the textures, loads the materials and builds every mesh (about 1 s). Idempotent. */
	void Build();
	bool IsBuilt() const { return bBuilt; }
	const FDmNatureMeshInfo& Get(EDmNatureMesh Id, int32 Variant) const;

	/** Shared tunables pushed to every nature material (wind, leaf look, planet frame for the wind direction). */
	void SetWind(float Strength, float Speed);
	void SetPlanetFrame(const FVector& CenterWorld, const FVector& AxisWorld);
	void SetLeafLook(float Brightness, float Spec, float Rough, float Transmission);

	UMaterialInstanceDynamic* GetCanopyMaterial() const { return MatCanopy; }
	bool MaterialsMissing() const { return bMissing; }
	double BuildSeconds = 0;

private:
	UMaterialInstanceDynamic* LoadMat(const TCHAR* Name);
	bool bBuilt = false;
	bool bMissing = false;
	FDmNatureTextures Tex;
	TArray<FDmNatureMeshInfo> Infos[int32(EDmNatureMesh::Num)];

	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatLeaf;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatBark;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatPalmBark;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatGrass;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatGrazer;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatBird;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatFish;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatCanopy;
	UPROPERTY() TArray<TObjectPtr<UObject>> Keep;
};
