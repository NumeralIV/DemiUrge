// DEMIURGE surface lab: chunked LOD planet. Each of the 20 icosahedron faces is the root of a
// quadtree of triangular patches (recursive edge midpoints, the same subdivision as the sim grid).
// A patch is a K x K triangle grid sampled from FSurfaceTerrain::Height at its level's spacing,
// with skirts that hide the T-junction cracks between levels. Patches split while their vertex
// spacing projects to more than TargetPx pixels.
#pragma once
#include "CoreMinimal.h"
#include <atomic>
#include "GameFramework/Actor.h"
#include "SurfaceTerrain.h"
#include "GroundLand.h"
#include "SurfacePlanet.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UTexture2D;

struct FPatchData
{
	FVector Origin = FVector::ZeroVector; // UE, cm
	TArray<FVector> V, Nn;
	TArray<int32> I;
	TArray<FVector2D> U0, U1, U2, U3;
	TArray<FLinearColor> Col;
	TArray<FVector> WV, WN;
	TArray<int32> WI;
	TArray<FVector2D> WU0, WU1;
	float MinH = 0.f, MaxH = 0.f;
	double Ms = 0.0;
	int32 LandTris = 0, WaterTris = 0;
	int32 BuiltNb[3] = { -1, -1, -1 };
};

/** A coarser neighbour along one edge: the patch moves its edge vertices onto that neighbour's edge. */
struct FStitch
{
	bool bOn = false;
	int32 Node = -1;
	FVector3d CC[3];
	double Sc = 0.0;
};

struct FPendingPatch
{
	FPatchData D;
	std::atomic<bool> bDone{ false };
};

struct FQNode
{
	FVector3d C[3];
	FVector3d Ctr;
	double Ang = 0.0;
	int32 Level = 0, Parent = -1, Child = -1;
	float MinH = -1500.f, MaxH = 2600.f;
	int32 Comp = -1;
	int32 LandTris = 0, WaterTris = 0;
	TSharedPtr<FPendingPatch, ESPMode::ThreadSafe> Pending;
	bool bBuilding = false;
	bool bSplit = false;
	int32 BuiltNb[3] = { -1, -1, -1 }; // coarser neighbours the current mesh is stitched to
	int32 WantNb[3] = { -1, -1, -1 };  // coarser neighbours in the current selection
};

UCLASS()
class PLANETLAB_API ASurfacePlanet : public AActor
{
	GENERATED_BODY()
public:
	ASurfacePlanet();

	bool Init(const FString& DataPath, UMaterialInterface* InLand, UMaterialInterface* InWater);

	/** Camera in UE world space (cm); FovDeg horizontal. */
	void SetView(const FVector& CamPos, const FVector& CamFwd, double FovDeg, double Aspect, int32 ScreenW);
	/** Select and build (blocking, all threads) until nothing more is needed. */
	void Settle(int32 MaxIters = 60);
	/** One incremental step: select, apply finished builds, start at most MaxStarts new async builds. */
	void Step(int32 MaxStarts);

	/** Terrain height (m above sea) under a UE direction. */
	double HeightUE(const FVector& DirUE) const;
	static FVector3d ToSim(const FVector& V) { return FVector3d(V.X, V.Z, V.Y); }
	static FVector ToUE(const FVector3d& V) { return FVector(V.X, V.Z, V.Y); }

	FString StatsLine() const;

	/** Octahedral mapping of a unit direction (UE frame) to 0..1 UV, as in Scripts/hlsl/land.hlsl. */
	static FVector2D OctUV(const FVector3d& D);
	static FVector3d OctDir(const FVector2D& UV);
	static constexpr double RiverSdfRangeM = 400.0;
	UPROPERTY() TObjectPtr<UTexture2D> RiverTex;
	/** Ground lab: settlements, farm field, roads and the camera-centred ground tile (see GroundLand.h). */
	void BuildGround(const FVector& CenterDirUE, double HalfKm, float Season, float Age);
	FGroundLand Land;
	UPROPERTY() TObjectPtr<UTexture2D> LandTex;   // global octahedral: R farm, G settlement core, B road coverage, A temperature
	UPROPERTY() TObjectPtr<UTexture2D> TileHTex;  // tile smooth height (R32F)
	UPROPERTY() TObjectPtr<UTexture2D> TileFTex;  // tile road signed / unsigned distance, road half-width, river distance (RGBA16F)
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> LandMID;

	int32 K = 32;
	double TargetPx = 8.0;
	int32 MaxLevel = 13;
	bool bFreeze = false;
	int32 MaxAppliesPerFrame = 32;
	/** 1: skirts magenta, 2: no skirts, 3: colour by level */
	static int32 DebugMode;

	FSurfaceTerrain T;
	double RadiusCm = 1.0e7;

	// stats of the last selection
	int32 DrawPatches = 0, DrawLandTris = 0, DrawWaterTris = 0, MaxDrawLevel = 0, BuildsTotal = 0;
	int32 LevelCount[20] = {};
	double LastSettleMs = 0.0, BuildMsTotal = 0.0;

private:
	void Visit(int32 Ni);
	bool Ready(int32 Ni) const;
	void Select();
	void EnsureChildren(int32 Ni);
	void Apply(int32 Ni, FPatchData& D);
	void Release(int32 Ni);
	bool AboveHorizon(const FQNode& N) const;
	bool InFrustum(const FQNode& N) const;
	double ErrorPx(const FQNode& N) const;
	void AddBuiltDescendants(int32 Ni);
	static void BuildPatch(const FSurfaceTerrain& T, const FQNode& N, int32 K, double RadiusCm, const FStitch St[3], FPatchData& Out);
	static FVector3d GridDir(const FVector3d C[3], int32 K, int32 I, int32 J);
	static FVector3d NormalAt(const FSurfaceTerrain& T, const FVector3d& D, double H0, double S, bool bRiv);
	static bool Contains(const FQNode& N, const FVector3d& P);
	int32 DrawnAt(const FVector3d& P) const;
	void MakeStitch(int32 Ni, FStitch Out[3]) const;
	void BuildRiverTexture(int32 Size);

	UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> Comps;
	TArray<int32> FreeComps;
	UPROPERTY() TObjectPtr<UMaterialInterface> LandMat;
	UPROPERTY() TObjectPtr<UMaterialInterface> WaterMat;

	TArray<FQNode> Nodes;
	TArray<int32> Draw, Need;
	TSet<int32> DrawSet;

	FVector3d CamSim = FVector3d(0, 0, 5e7);    // cm, sim frame
	FVector3d FwdSim = FVector3d(0, 0, -1);
	double TanHalfH = 0.27, TanHalfV = 0.15, PxPerRad = 3600.0;
};
