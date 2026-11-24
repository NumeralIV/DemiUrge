// DEMIURGE — Game/Nature/DmNatureFactory.h : the nature mesh factory (C4 nature, SPEC-UNREAL 0.4).
//
// Every visual of the nature module comes from here as (mesh id, variant) -> UStaticMesh + metadata, so an
// asset pass (Fab / Blender meshes) is a one-place change: replace Build() for an id with a LoadObject of the
// imported mesh (keeping the material slot order: 0 bark / body, 1 leaves) and the scatter, LOD, culling,
// wind and per-instance data keep working. Materials come from Scripts/nature.py (/Game/Nature/M_*),
// parameterised through dynamic instances; textures are painted at startup (DmNatureTextures).
//
// Asset pass (wave 4, docs/ASSET-INTEGRATION.md 6.2, docs/lab/trees.md 9): the Blender families are used when they are
// in the project (/Game/Nature/Trees, /Game/Nature/Animals, made by Scripts/nature_assets.py from the import tool's
// output): conifer, broadleaf, palm, acacia and shrub (4 variants each, LOD0-2 + a LOD3 impostor), the herd species,
// birds and fish. The dead tree and grass stay generated; every id falls back to the generated stand-in when its
// asset is missing, and -gentrees / -genfauna force the stand-ins (A/B shots).
//
// Per-instance custom data (all nature meshes, NumCustomData floats):
//   trees / shrubs / grass:  0 burn (0..1, charred), 1 snow (0..1, snow on top of crowns), 2 dry (0..1, dry-season tint),
//                            3 autumn (0..1, copper / yellow accent; the scatter sets it on temperate broadleaf only)
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

/** Grazer variants (the herd species of the asset pass; the generated stand-ins have 4 and map V % 4). */
enum class EDmGrazer : uint8 { Doe = 0, Antelope, Wildebeest, Bison, Stag, Lioness, Lion, Num };

struct FDmNatureMeshInfo
{
	UStaticMesh* Mesh = nullptr;
	float Height = 1.f;     // metres (trees: top of the crown)
	float Radius = 1.f;     // metres (crown / body radius)
	int32 Tris = 0;         // LOD0 triangles
	bool bAsset = false;    // a Blender asset (not a generated stand-in)
	float YawDeg = 0.f;     // extra yaw for every instance (the asset birds and fish face +X, the paths expect +Y)
	TArray<UMaterialInterface*> Materials;   // per slot (kept alive by the factory); empty = the mesh's own
};

UCLASS(Transient)
class DEMIURGE_API UDmNatureFactory : public UObject
{
	GENERATED_BODY()

public:
	static constexpr int32 NumCustomData = 4;
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
	/** The Blender trees are in use (the scatter uses real-scale density and scale rules for them). */
	bool TreeAssets() const { return bTreeAssets; }
	int32 NumAssetMeshes() const { return AssetMeshes; }
	double BuildSeconds = 0;

private:
	UMaterialInstanceDynamic* LoadMat(const TCHAR* Name);
	bool bBuilt = false;
	bool bMissing = false;
	bool bTreeAssets = false;
	float AssetLeafBrightness = 1.2f;    // Blender leaf atlas / impostor brightness relative to SetLeafLook (-assetleafbright=); the lab's 0.78 read near-black under the game's sky (no sky-light fill)
	float LeafSaturation = 0.6f;         // saturation trim of the Blender leaves and impostors (-leafsat=): 0.85 gave 0.60 at golden hour, 0.75 0.62 / 0.6 + warmer tint 0.56 (verification; concept 0.42, the rest is the dark sky-less shadows)
	int32 AssetMeshes = 0;
	void BuildAssetTrees();
	void BuildAssetFauna();
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
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatAssetLeaf;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatAssetImpostor;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> MatAssetCreatures;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> MatAssetBarks;
	UPROPERTY() TArray<TObjectPtr<UObject>> Keep;
};
