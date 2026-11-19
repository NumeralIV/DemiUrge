// DEMIURGE — Game/Towns/DmTownsKit.h : the towns mesh factory (C5 towns, SPEC-UNREAL 0.4).
//
// Every visual of the towns module is a (mesh id, variant) pair from here, so the later asset pass
// (Fab / Blender meshes) is a one-place change: replace the generator of an id in UDmTownsFactory::Build
// with a LoadObject of the imported mesh, keep the material slot order (0 body, 1 glass, 2 glow) and the
// FDmKitInfo footprint, and the planner, instancing, LOD, culling, tints and night lights keep working.
//
// Local mesh space: metres in the generators (centimetres in the UStaticMesh), +Z up, origin at the centre
// of the footprint at floor level, the front (door / street side) toward -Y, the ridge or long axis along X.
// Walls run 3 m below the floor so buildings never float on slopes. Generated meshes are clean stand-ins
// (owner direction 2026-11-19): massing, roofs, windows and silhouettes, three hand-made LODs.
//
// Per-instance custom data (every town HISM, NumCustomData floats):
//   0..2 wall tint (linear multiplier of the vertex albedo where the vertex takes the wall tint)
//   3..5 roof tint
//   6    lit fraction of the windows at night (0..1)
//   7    state: 0 intact, 1 burnt / ruined (darkened, no lights); fractions blend
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Game/Towns/DmTownsMesh.h"
#include "DmTownsKit.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;

// name, variants
#define DM_TOWN_MESHES(X) \
	X(Tent, 3) X(Hut, 4) X(Longhouse, 2) X(MudHouse, 4) X(Cottage, 6) X(Townhouse, 6) X(Rowhouse, 4) \
	X(Apartment, 4) X(Tower, 5) X(Villa, 4) X(Barn, 3) X(Warehouse, 3) X(Factory, 3) X(Stack, 2) \
	X(Campfire, 1) X(Stones, 1) X(Shrine, 2) X(TempleEarly, 2) X(TempleClassic, 2) X(TempleLate, 2) \
	X(Granary, 2) X(Market, 2) X(MarketHall, 1) X(WallSeg, 3) X(WallTower, 3) X(Gatehouse, 2) X(Pier, 2) \
	X(Lighthouse, 1) X(Crane, 1) X(MineHead, 2) X(Spoil, 1) X(Academy, 2) X(CoolingTower, 1) X(Reactor, 1) \
	X(Gantry, 1) X(Rocket, 1) X(Wonder, 5) X(Ruin, 3) X(Scaffold, 1) X(Windmill, 1) \
	X(Boat, 2) X(Galley, 1) X(SailShip, 2) X(Steamer, 1) X(CargoShip, 2) \
	X(Settlers, 1) X(Caravan, 2) X(Army, 2) X(Prophet, 1)

enum class EDmTownMesh : uint8
{
#define DM_TOWN_ENUM(N, V) N,
	DM_TOWN_MESHES(DM_TOWN_ENUM)
#undef DM_TOWN_ENUM
	Num
};

struct FDmKitInfo
{
	UStaticMesh* Mesh = nullptr;
	float W = 8.f, D = 6.f, H = 6.f;     // footprint (local X, Y) and height, metres
	EDmTownPat WallPat = EDmTownPat::Plaster;
	EDmTownPat RoofPat = EDmTownPat::Tile;
	int32 Tris = 0;                      // LOD0 triangles
	bool bLit = true;                    // has windows that light up at night
};

UCLASS(Transient)
class DEMIURGE_API UDmTownsFactory : public UObject
{
	GENERATED_BODY()

public:
	static constexpr int32 NumCustomData = 8;
	static int32 NumVariants(EDmTownMesh Id);
	static const TCHAR* Name(EDmTownMesh Id);

	/** Generates every mesh (with LODs) and loads the materials (/Game/Towns, Scripts/towns.py). Idempotent. */
	void Build();
	bool IsBuilt() const { return bBuilt; }
	bool MaterialsMissing() const { return bMissing; }
	const FDmKitInfo& Get(EDmTownMesh Id, int32 Variant) const;
	/** Footprint info without the mesh (thread-safe after Build: plain data). */
	const TArray<FDmKitInfo>& Infos(EDmTownMesh Id) const { return Kit[int32(Id)]; }

	UMaterialInstanceDynamic* BodyMat() const { return MatBody; }
	UMaterialInstanceDynamic* GlassMat() const { return MatGlass; }
	UMaterialInstanceDynamic* GlowMat() const { return MatGlow; }
	UMaterialInstanceDynamic* GroundMat() const { return MatGround; }

	/** Shared shading inputs for every towns material: planet centre (world), direction to the sun (world). */
	void SetFrame(const FVector& PlanetCentreWorld, const FVector& SunDirWorld);
	void SetNightOverride(float Night);          // < 0: from the sun
	void SetGlow(float Windows, float Pools, float Streets);
	double BuildSeconds = 0;
	int32 TotalTris = 0;

	/** Generator entry (also used by tests): one LOD of one mesh. */
	static void Generate(EDmTownMesh Id, int32 Variant, int32 Lod, FDmTownMesh& Out, FDmKitInfo& Info);

private:
	UMaterialInstanceDynamic* LoadMat(const TCHAR* MatName);
	bool bBuilt = false;
	bool bMissing = false;
	TArray<FDmKitInfo> Kit[int32(EDmTownMesh::Num)];

	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatBody;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatGlass;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatGlow;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatGround;
	UPROPERTY() TArray<TObjectPtr<UStaticMesh>> Meshes;
};
