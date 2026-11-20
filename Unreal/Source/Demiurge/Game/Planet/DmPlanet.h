// DEMIURGE — Game/Planet/DmPlanet.h : the planet's land and ocean surfaces (SPEC-UNREAL 2, C1).
// Owner: C1 planet. Scene part "planet" (priority 0; replaces the bridge's placeholder).
//
// A chunked-LOD planet (docs/lab/surface.md): each of the 20 icosahedron faces is the root of a quadtree
// of triangular patches (recursive edge midpoints, the same subdivision as the sim grid). A patch is a
// K x K triangle grid sampled from FDmPlanetTerrain::Height at its level's spacing; it splits while its
// vertex spacing projects to more than TargetPx pixels, and every patch is stitched exactly to coarser
// neighbours (crack-free), with tiny safety skirts. Builds run on the thread pool against an immutable
// terrain snapshot; a patch keeps drawing until its replacement is ready (no holes, no hitches).
//
// Everything that does not move the ground (biome colour, vegetation, drought, wetness, snow, ice, sea
// ice, farms, urban, roads, night lights, scorch, lava, fire, fallout, overlays) is a planet-wide state
// texture (DmPlanetMaps.h) sampled by the materials: a colour change costs a texture update, not a mesh
// rebuild. Elevation / water / river changes re-capture the terrain on a worker thread and rebuild only
// the patches near the changed cells.
//
// Other modules (nature, towns, camera, fx) place things on the rendered ground through the queries:
//
//   if (ADmPlanet* P = ADmPlanet::Find(this))
//   {
//       const double H = P->GroundHeightM(PlanetDir);          // m above the 100 km datum, full detail
//       const FVector W = P->GroundPointWorld(PlanetDir);      // world-space point on the ground
//       const float Forest = P->ForestAt(PlanetDir);           // painted canopy density 0..1
//   }
//
// The rendered ground differs from the sim's surface (DmCoords) by the relief the planet adds (up to a
// few hundred metres in mountains): use these queries, not CellSurfacePos, for anything that must touch
// the ground. Planet-frame directions are Unreal axes (DmCoords::SimToUE); world = PlanetToWorld.
//
// Roads: the mesh carries the signed distance across the nearest road in the vertex colour (R, +-60 m; G =
// level; A = 1 only for debug colours), so roads are crisp continuous bands close up; the road map draws them
// beyond. Deep-sea patches (all > 150 m deep) keep a coarse floor with the cheap M_PlanetSeabed material (the
// water needs a scene depth behind it) and open-sea water uses every 4th grid line.
//
// Debug switches: -planetdebug=1 (skirts magenta), 2 (no skirts), 3 (colour by LOD level), 4 (by patch),
// 10 / 11 / 12 (material masks: rock-forest-beach / snow-pond-soil / wet shore-seabed-farmland), 13 (road from
// the map / road from the mesh / river), 14 (mesh river distance / slope / mesh road distance);
// -planetlodpx=<px> (split threshold, default 8), -planetmaxlevel=<n> (default 13), -overlay=<0..5|name>,
// -planetnocull (split out-of-view patches too), -planetlog (LOD, terrain-change and prune log lines),
// -planetnodatum (A/B: shape the ground against the current sea instead of vars.seaBase, the pre-wave-3 mapping).
// Material debug -planetdebug=15: sea rise (R), drowned land (G), under water (B).
//
// Sea rise (wave 3): the ground is shaped against the calibrated base sea (FDmPlanetTerrain::RefLevel), so a rising
// sea floods the land where it lies (drowned hills as islands, the depth growing toward the old coast) and the
// materials get SeaRise (m): drowned land is silt and drowned meadow under turbid green shallows with breakers.
// The sky's atmosphere ground must sit below the sea for any of the water to show (DmSkyActor AtmoDropKm, see
// docs/handoff/done-ue/wave3-planet-fixes.built.txt). (verify, 2026-11-20) Do not just drop it: with -atmodrop=0.5 / 2 the
// shelf and open sea render pale grey (the seabed shows through with almost no depth absorption, and a constant red
// base colour at Opacity 1 renders navy), so the water material must be fixed first: docs/handoff/done-ue/wave3-planet-fixes.txt.
// Scenario planet-look (DmPlanetScenarios.cpp) renders any view, with -probe=x,y height profiles.
//
// Live updates (verification 2026-11-19): colour maps are computed on a worker from inputs copied on the game
// thread; terrain changes re-capture on the game thread, prepare on a worker, and rebuild only the patches
// near changed cells (ground: 5 rings; roads: 3 rings, fine patches only; a river: the cells of the rivers
// that changed, flows quantised to quarter octaves; sea level in 0.5 m steps). Idle quadtree nodes are pruned
// and their slots reused (a generation counter keeps old stitches from matching a reused slot).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include <atomic>
#include "Game/Core/DmDirtyRouter.h"
#include "Game/Planet/DmPlanetMaps.h"
#include "Game/Planet/DmPlanetTerrain.h"
#include "DmPlanet.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;
namespace dm { struct World; struct Snapshot; }

struct FDmPatchData
{
	FVector Origin = FVector::ZeroVector;   // planet frame (Unreal axes), cm
	TArray<FVector> V, Nn;
	TArray<int32> I;
	TArray<FVector2D> U0, U1, U2, U3;
	TArray<FLinearColor> Col;
	TArray<FVector> WV, WN;
	TArray<int32> WI;
	TArray<FVector2D> WU0, WU1;
	float MinH = 0.f, MaxH = 0.f;           // m above datum
	double Ms = 0.0;
	int32 LandTris = 0, WaterTris = 0;
	int32 BuiltNb[3] = { -1, -1, -1 };
	int32 BuiltGen[3] = { -1, -1, -1 };
	int32 Version = -1;
	bool bSeabed = false;   // deep-sea patch: a coarse floor with the cheap seabed material
};

/** A coarser neighbour along one edge: the patch moves its edge vertices onto that neighbour's edge. */
struct FDmStitch
{
	bool bOn = false;
	int32 Node = -1;
	int32 Gen = -1;          // generation of that node slot (slots are reused after pruning)
	FVector3d CC[3];
	double Sc = 0.0;
};

struct FDmPendingPatch
{
	FDmPatchData D;
	std::atomic<bool> bDone{ false };
};

struct FDmQNode
{
	FVector3d C[3];          // corners, sim frame
	FVector3d Ctr;
	double Ang = 0.0;
	int32 Level = 0, Parent = -1, Child = -1;
	float MinH = -1500.f, MaxH = 2600.f;
	int32 Comp = -1;
	int32 LandTris = 0, WaterTris = 0;
	int32 Version = -1;      // terrain version of the current mesh (stale when != the terrain's)
	TSharedPtr<FDmPendingPatch, ESPMode::ThreadSafe> Pending;
	bool bBuilding = false;
	bool bSplit = false;
	int32 BuiltNb[3] = { -1, -1, -1 };
	int32 WantNb[3] = { -1, -1, -1 };
	int32 BuiltGen[3] = { -1, -1, -1 };
	int32 Gen = 0;           // bumped each time the slot is freed: a stitch to a reused slot is not the same stitch
	uint64 LastUsed = 0;     // frame of the last selection that reached this node
	int32 Cell = -1;         // sim cell nearest the centre (lazy; ApplyTerrain)
	bool bFree = false;      // slot in the free list (its 4-block was pruned)
};

struct FDmTerrainJob
{
	TSharedPtr<FDmPlanetTerrain, ESPMode::ThreadSafe> T;
	std::atomic<bool> bDone{ false };
};

struct FDmMapJob
{
	FDmMapInputs In;
	FDmPlanetMaps::FResult R;
	std::atomic<bool> bDone{ false };
};

struct FDmRiverJob
{
	TArray<TArray<FColor>> Mips;
	int32 Near = 0;
	double Ms = 0.0;
	std::atomic<bool> bDone{ false };
};

UCLASS()
class DEMIURGE_API ADmPlanet : public AActor
{
	GENERATED_BODY()

public:
	ADmPlanet();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** The planet of this world (nullptr if the scene has none). */
	static ADmPlanet* Find(const UObject* WorldContext);

	// ---------------------------------------------------------------- queries (game thread)
	/** Ground height (m above the 100 km datum) under a planet-frame direction, full detail. */
	double GroundHeightM(const FVector& PlanetDir) const;
	/** Water surface (sea or lake, m above datum) under a direction, or -1e9 on dry land. */
	double WaterHeightM(const FVector& PlanetDir) const;
	/** max(ground, water): what you would stand or float on. */
	double SurfaceHeightM(const FVector& PlanetDir) const;
	double GroundRadiusUU(const FVector& PlanetDir) const { return 1.0e7 + GroundHeightM(PlanetDir) * 100.0; }
	FVector GroundPointPlanet(const FVector& PlanetDir) const { return PlanetDir.GetSafeNormal() * GroundRadiusUU(PlanetDir); }
	FVector GroundPointWorld(const FVector& PlanetDir) const;
	/** Sea surface m above datum. */
	double SeaHeightM() const;
	/** Painted forest canopy density 0..1 (map 0 alpha; the nature module's tree density should agree). */
	float ForestAt(const FVector& PlanetDir) const;
	/** First hit of a world-space ray with the rendered ground or water (sphere-traced). */
	bool RaycastWorld(const FVector& Origin, const FVector& Dir, double MaxDist, FVector& OutHit, bool bWater = true) const;

	// ---------------------------------------------------------------- display
	/** 0 none, 1 cultures (owner borders), 2 temperature, 3 rainfall, 4 devotion, 5 habitability. */
	void SetOverlay(int32 Mode);
	int32 GetOverlay() const { return Overlay; }
	/** Time-lapse colouring from a snapshot; nullptr restores the live colours. */
	void ShowSnapshot(const dm::Snapshot* S);
	void SetDebugMode(int32 Mode);

	// ---------------------------------------------------------------- LOD
	/** Camera in world space (cm), horizontal FOV in degrees. Tick reads the player's camera itself. */
	void SetView(const FVector& CamPos, const FVector& CamFwd, double FovDeg, double Aspect, int32 ScreenW);
	/** Select and build (blocking, all threads) until nothing more is needed. */
	void Settle(int32 MaxIters = 60);
	/** True when nothing is building or waiting to be built for the current view. */
	bool IsSettled() const;
	FString StatsLine() const;
	/** Terrain snapshot in use (read-only; valid on the game thread). */
	const FDmPlanetTerrain* Terrain() const { return Terr.Get(); }

	int32 K = 32;
	double TargetPx = 8.0;
	int32 MaxLevel = 13;
	int32 MaxAppliesPerFrame = 48;
	int32 MaxStartsPerFrame = 64;
	bool bFreeze = false;
	bool bLogLod = false;
	bool bNoCull = false;
	/** Seconds between colour-map refreshes while the sim keeps them dirty (old spec: ~6 Hz). */
	double ColorEvery = 0.16;

	// stats of the last selection
	int32 DrawPatches = 0, DrawLandTris = 0, DrawWaterTris = 0, MaxDrawLevel = 0, BuildsTotal = 0, InFlight = 0;
	int32 LevelCount[20] = {};
	double LastSettleMs = 0.0, BuildMsTotal = 0.0;
	int32 TerrainRebuilds = 0, MapUpdates = 0, StalePatches = 0, NodesFreed = 0;
	double LastTerrainMs = 0.0, LastMapMs = 0.0, LastRiverMapMs = 0.0, LastMapCaptureMs = 0.0, LastMapApplyMs = 0.0, LastApplyTerrainMs = 0.0;
	/** Game-thread time of the data pump (terrain capture / apply, colour-map capture / upload), worst of the last 120 frames. */
	double PumpMsMax() const;

private:
	// world / data
	void CheckMaterials();
	bool bMaterialsChecked = false, bSawCompile = false;
	int32 IdleFrames = 0;
	void OnDirty(const FDmDirtyFlags& F);
	void OnWorldChanged(const dm::World* W);
	void ResetWorld();
	bool BuildFromWorld();
	void PumpData();
	void ApplyTerrain(const TSharedPtr<FDmPlanetTerrain, ESPMode::ThreadSafe>& NewT);
	void StartRiverMap();
	void UpdateMaterialParams();
	void SetMaterialTextures();

	// LOD
	void Visit(int32 Ni);
	bool Ready(int32 Ni) const;
	void MarkKeep(int32 Ni);
	void Select();
	void Step();
	void EnsureChildren(int32 Ni);
	void Apply(int32 Ni, FDmPatchData& D);
	void Release(int32 Ni);
	bool AboveHorizon(const FDmQNode& N) const;
	bool InFrustum(const FDmQNode& N) const;
	double ErrorPx(const FDmQNode& N) const;
	void AddBuiltDescendants(int32 Ni);
	void Prune();
	int32 DrawnAt(const FVector3d& P) const;
	void MakeStitch(int32 Ni, FDmStitch Out[3]) const;
	void ReadPlayerView();
	static void BuildPatch(const FDmPlanetTerrain& T, const FDmQNode& N, int32 K, const FDmStitch St[3], int32 Debug, FDmPatchData& Out);
	static FVector3d GridDir(const FVector3d C[3], int32 K, int32 I, int32 J);
	static FVector3d NormalAt(const FDmPlanetTerrain& T, const FVector3d& D, double H0, double S, bool bRiv);
	static bool Contains(const FDmQNode& N, const FVector3d& P);

	UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> Comps;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> LandMid;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> WaterMid;
	UPROPERTY() TObjectPtr<UMaterialInterface> SeabedM;
	UPROPERTY() TArray<TObjectPtr<UTexture2D>> MapTextures;
	UPROPERTY() TObjectPtr<UTexture2D> RiverTex;
	TArray<int32> FreeComps;

	TArray<FDmQNode> Nodes;
	TArray<int32> Draw, Need;
	TSet<int32> DrawSet, Keep;

	TSharedPtr<const FDmGeoLocator, ESPMode::ThreadSafe> Loc;
	TSharedPtr<const FDmPlanetTerrain, ESPMode::ThreadSafe> Terr;
	TSharedPtr<FDmTerrainJob, ESPMode::ThreadSafe> TerrainJob;
	TSharedPtr<FDmRiverJob, ESPMode::ThreadSafe> RiverJob;
	TSharedPtr<FDmMapJob, ESPMode::ThreadSafe> MapJob;
	TArray<int32> FreeBlocks;        // first index of each free 4-node block
	float PumpHist[120] = {};
	int32 PumpHistAt = 0;
	FDmPlanetMaps Maps;
	uint32 RiverMapHash = 0;
	int32 TerrainVersion = 0;

	FDelegateHandle DirtyHandle, WorldHandle;
	bool bWorldPending = true;
	bool bTerrainDirty = false, bColorDirty = false, bOverlayDirty = false, bRiverMapDirty = false;
	double LastColorTime = -1e9;
	double LastTerrainStart = -1e9, LastRiverStart = -1e9;   // (integration) throttles, see PumpData
	int32 Overlay = 0;
	bool bSnapshot = false;
	int32 DebugMode = 0;
	float LastHeat = -1.f;

	FVector3d CamSim = FVector3d(0, 0, 5e7);   // cm, sim frame (planet)
	FVector3d FwdSim = FVector3d(0, 0, -1);
	double TanHalfH = 0.27, TanHalfV = 0.15, PxPerRad = 3600.0;
	bool bViewSet = false;
	bool bViewOverride = false;   // SetView called explicitly this frame
	FVector LastCamWorld = FVector(1e30);
};
