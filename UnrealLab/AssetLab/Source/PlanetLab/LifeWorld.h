// LifeLab: a local patch of the DEMIURGE planet (curved, real scale) with generated forests and settlements.
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LabPlanet.h"
#include "LifeTex.h"
#include "LifeTrees.h"
#include "LifeWorld.generated.h"

class UStaticMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
struct FLifeMesh;

/** Regular grid of a scalar field over the patch (metres, centred on the site). */
struct FLifeGrid
{
	int32 N = 0;
	double Half = 0.0, Step = 1.0;
	TArray<float> V;
	void Init(int32 InN, double InHalf) { N = InN; Half = InHalf; Step = 2.0 * Half / (N - 1); V.SetNumZeroed(N * N); }
	float& At(int32 I, int32 J) { return V[J * N + I]; }
	float At(int32 I, int32 J) const { return V[FMath::Clamp(J, 0, N - 1) * N + FMath::Clamp(I, 0, N - 1)]; }
	float Bilinear(double X, double Y) const;
	float Cubic(double X, double Y) const;
	void Blur(int32 Radius);
};

/** One settlement as planned (layout lives in LifeTown.cpp). */
struct FLifeSettlement
{
	enum EKind : uint8 { Village, WalledTown, City } Kind = Village;
	FVector2D C = FVector2D::ZeroVector; // centre, metres
	double Radius = 200.0;
	double Axis = 0.0;                    // main orientation (radians)
	FString Name;
	int32 Buildings = 0;
	int32 Tris = 0;
};

UCLASS()
class PLANETLAB_API ALifeWorld : public AActor
{
	GENERATED_BODY()
public:
	ALifeWorld();

	/** Builds everything around planet cell SiteCell. */
	bool Build(const FString& DataPath, int32 SiteCell);

	// ---- geometry of the patch (metres; X east, Y north in the site's tangent plane, H above sea level)
	static constexpr double Rm = 100000.0;  // planet sea-level radius in metres (100 km)
	float HeightAt(double X, double Y) const;
	/** Height before settlement flattening and fine detail (used to plan towns). */
	float BaseHeightAt(double X, double Y) const;
	FVector3d SurfaceM(double X, double Y, double H) const; // metres, relative to the site origin (which is at world (0,0,Rm))
	FQuat4d UpQuat(double X, double Y) const;               // rotates local +Z to the surface normal direction at (X,Y)
	FVector WorldCm(double X, double Y, double H) const { return FVector(SurfaceM(X, Y, H) * 100.0) + GetActorLocation(); }
	FVector3f Slope(double X, double Y, double Eps = 2.0) const; // terrain normal in the local frame

	// ---- climate at a point (from the planet simulation)
	float TempAt(double X, double Y) const { return GTemp.Bilinear(X, Y) - 0.0065f * FMath::Max(0.f, HeightAt(X, Y)) * 5.f; }
	float MoistAt(double X, double Y) const { return GMoist.Bilinear(X, Y); }
	float VegAt(double X, double Y) const { return GVeg.Bilinear(X, Y); }
	float HabAt(double X, double Y) const { return GHab.Bilinear(X, Y); }

	// ---- land use (2 m raster over the inner patch)
	static constexpr int32 LuN = 4096;
	static constexpr double LuHalf = 4096.0;
	static constexpr double LuStep = 2.0;
	TArray<uint8> LuRoad, LuPaved, LuField, LuYard, LuNoTree, LuBuilt;
	/** Hedgerows along field edges (x0, y0, x1, y1), planted by BuildTrees. */
	TArray<FVector4d> Hedges;
	/** Cleared land around settlements (x, y, radius): forests fade out inside. */
	TArray<FVector3d> Clearings;
	float ClearingAt(double X, double Y) const
	{
		float K = 1.f;
		if (!Clearings.Num()) return K;
		const float Wob = 1.f + 0.3f * LifeFbm2(float(X / 420.0), float(Y / 420.0), 3, 61);
		for (const FVector3d& C : Clearings) K *= LifeSmooth(float(C.Z * 0.7), float(C.Z), float(FVector2D::Distance(FVector2D(C.X, C.Y), FVector2D(X, Y))) * Wob);
		return K;
	}
	TArray<FColor> LuFieldCol; // crop colour (sRGB) + row angle in A
	bool LuIndex(double X, double Y, int32& I, int32& J) const
	{
		I = FMath::FloorToInt((X + LuHalf) / LuStep); J = FMath::FloorToInt((Y + LuHalf) / LuStep);
		return I >= 0 && J >= 0 && I < LuN && J < LuN;
	}
	uint8 LuGet(const TArray<uint8>& A, double X, double Y) const { int32 I, J; return LuIndex(X, Y, I, J) ? A[J * LuN + I] : 0; }
	void StampRoad(const TArray<FVector2D>& Pts, double Width, bool bPaved);
	void StampDisc(TArray<uint8>& A, const FVector2D& C, double R, uint8 V);
	void StampPoly(TArray<uint8>& A, const TArray<FVector2D>& Poly, uint8 V, double Shrink = 0.0);
	void StampField(const TArray<FVector2D>& Poly, const FColor& Col, float Angle01);

	/** Flattening pads for towns: height is blended towards a smooth base inside. */
	struct FPad { FVector2D C; double R0, R1; float H; };
	TArray<FPad> Pads;

	// ---- runtime options
	double TreeHalf = 4000.0;       // trees are scattered within this half-extent
	float TreeDensity = 1.f;
	float CullTrees = 9000.f;       // metres
	bool bTowns = true;
	bool bTrees = true;
	TArray<FLifeSettlement> Settlements;
	/** AssetLab: showcase site (flattened, cleared of trees, beaten-earth yard). */
	bool bAssetSite = false;
	FVector2D AssetSite = FVector2D::ZeroVector;
	double AssetSiteR = 30.0;

	void SetNight(float Night, float WindowGlow);
	FString StatsLine() const;

	// shared materials
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatLeaf;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatBark;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatPalmBark;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatTerrain;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatWater;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatWall;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatRoof;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatWindow;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatLamp;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatGlow;

	UPROPERTY() TArray<TObjectPtr<UObject>> Keep;
	UPROPERTY() TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> TreeComps;
	TArray<int32> TreeCompKind;
	int32 TreeCount[int32(ETreeKind::Num)] = {};
	int32 TerrainTris = 0, WaterTris = 0, TownTris = 0, TownBuildings = 0;
	int32 TreeLOD0Tris[int32(ETreeKind::Num)] = {};

	/** Adds a static mesh component holding a generated mesh (metres, relative to Origin in the local frame). */
	UStaticMeshComponent* AddMeshComp(const FString& Name, const FLifeMesh& Mesh, const TArray<UMaterialInterface*>& Mats, const FVector3d& OriginM, bool bShadow = true);

	FLifeTextures Tex;

private:
	FLabPlanetData D;
	FQuat4d SiteQ;          // rotates the planet frame so the site is at +Z
	FLifeGrid GElev, GTemp, GMoist, GVeg, GHab;

	bool SampleFields(int32 SiteCell);
	void MakeMaterials();
	void BuildTerrain();
	void BuildWater();
	void BuildTrees();
	void MakeLandUseTextures();

	// LifeTown.cpp
	void PlanSettlements();
	void BuildSettlements();
	FVector2D FindSite(FLifeSettlement::EKind Kind, const TArray<FVector2D>& Avoid) const;
	void PlanVillage(FLifeSettlement& S);
	void PlanWalledTown(FLifeSettlement& S);
	void PlanCity(FLifeSettlement& S);

public:
	struct FBuildingSpec;
	struct FTownPlan;
private:
	TArray<TSharedPtr<FTownPlan>> Plans;
};
