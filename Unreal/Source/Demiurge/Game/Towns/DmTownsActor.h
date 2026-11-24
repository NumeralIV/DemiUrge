// DEMIURGE — Game/Towns/DmTownsActor.h : the towns scene part (C5 towns, SPEC-UNREAL 2).
//
// ADmTowns is registered as scene part "towns" (World kind, priority 0). It draws every settlement of the
// world (DmTownsPlan.h lays them out) and the units moving between them:
//   - per settlement: one instanced static mesh component (plain ISM) per (kit mesh, variant) it uses, all
//     relative to the settlement's centre (cm precision), plus the draped ground overlay (fields, streets with
//     night lamps, plazas, quays); the whole settlement is hidden beyond HideKm or behind the planet's limb.
//     Not HISM: in UE 5.7 a HISM loses its per-instance custom data (tints, lit fraction, ruin state) when its
//     async tree build re-sorts the instances; measured with scenario towns-cdtest (ISM rows correct, HISM
//     rows read zeros). Settlements are compact, so per-component LOD is acceptable;
//   - rebuilds: a settlement is re-planned when its signature (tier, culture age, buildings, population
//     step, captured / destroyed, ark) changes; sim events (settlement:*, culture:age) only make that check
//     immediate. Plans run on worker threads; the first wave of a world too (nearest first, applied under the
//     frame budget: scenarios wait for GetPlan / IsSettled; -townblock blocks for the settlements in view instead). New buildings stand as scaffolds for
//     ConstructionSeconds before they appear; a destroyed settlement turns into ruins that fade with the
//     sim's urban field;
//   - night: windows (per-instance lit fraction), light pools and street lamps follow the sun per pixel;
//   - units: ships at sea, caravans, settlers, armies, prophets, damped toward their path position
//     (old spec RENDER 9), scaled up with camera distance so they stay readable, hidden from orbit;
//   - everything follows UDmSimSubsystem::GetPlanetToWorld() (the planet turns).
// Ground: FDmTownsGround (provider > placeholder planet mesh > sim surface).
//
// Integration (C1 / C10): register the planet's ground once, e.g. in ADmPlanet::BeginPlay:
//     FDmTownsGround::SetProvider([P](const FVector& Dir) { return P->GroundRadiusUU(Dir); });
// (thread-safe and pure; called from planning tasks). Without it towns read the bridge's placeholder planet
// mesh if present, else the sim surface (which can differ from a detailed planet by tens of metres).
// FX: SmokeAnchors() gives chimney / stack / campfire positions; TownCentreWorld() a settlement's plaza.
//
// Switches: -notowns (no actor), -nounits, -townage=<0..6> (plan every settlement at this age; debug),
// -townscale=<x> (radius multiplier), -townhidekm=<km>, -townnight=<0..1> (force night lights),
// -townglow=<windows>,<pools>,<streets>, -townlog (per-plan log lines), -townfields=1|0|-1 (field quads always /
// never / only without the planet module's farmland; default 1).
// Verifier changes (2026-11-19): the first wave blocks only for settlements in view (the rest plan in the background);
// a new ground snapshot re-plans only settlements whose ground moved; scaffolds only where nothing stood before;
// the field overlay fades out by 7-12 km and at night (M_TownGround is masked, dithered).
// Game-thread budget (wave 3 towns-perf, 2026-11-20; DmTownsPerf.h, scenario towns-hitch): nothing heavy runs on the
// game thread any more. Planning tasks also prepare what an apply needs (component batches, the ground overlay's render
// data, the construction diff); finished plans are applied nearest first under ApplyBudgetMs per frame; components
// are reused in place (content-hashed), taken from / returned to a pool of registered hidden components warmed at
// BeginPlay (one per kit mesh, so no first-use registration cost mid-game); hidden settlements' components are frozen
// (absolute transforms) so the turning planet only moves visible ones; ISM bounds are cached in local space; the
// ground is our own planet-terrain capture (~1 ms) prepared and probed on workers (no 10-30 ms copy of the planet's);
// old plans / grounds are freed on workers; the first wave is asynchronous (-townblock restores the blocking one).
// Measured: towns tick avg 0.3-0.8 ms, max 2.7-3.5 ms per frame over static / 0.25-8x / age change / war / sweep.
// Wave 4 towns (2026-11-20): plans are sliced jobs (FDmTownPlanStepper, ~SliceMs of work per task) at background priority
// (in view background-normal, out of view background-low; they took foreground workers at Normal for 1-7 s and starved the
// sky's weather builds and the renderer's own tasks); exit and world changes abandon jobs instead of waiting (cancel flag
// polled per slice); unit instances update as GPU-scene deltas (no proxy rebuild per frame); the real Blender assets stand in
// for the generated kit by (id, variant, age, tier, culture style) with their own construction and ruin states
// (DmTownsAssets.h; the generated meshes stay as the fallback); fields are organic parcels (DmTownsPlan.cpp, PlanFields).
#pragma once

#include "CoreMinimal.h"
#include <atomic>
#include "GameFramework/Actor.h"
#include "Game/Core/DmDirtyRouter.h"
#include "Game/Towns/DmTownsPlan.h"
#include "Tasks/Task.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/EngineBaseTypes.h"
#include "DmTownsActor.generated.h"

/** (towns-perf) The towns' instanced component: plain ISM (see above) whose bounds are cached in local space when its
 *  instances change, so moving it (the planet turns every frame) costs O(1) instead of a loop over every instance. */
UCLASS()
class DEMIURGE_API UDmTownsISM : public UInstancedStaticMeshComponent
{
	GENERATED_BODY()
public:
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	/** Replace every instance (transforms relative to the component, NumCustomData floats each). bRegistered: update a live
	 *  component in place (skipped when the content is unchanged). Returns false when nothing changed. */
	bool SetInstances(const TArray<FTransform>& Xf, const TArray<float>& Cd, bool bLive);
	/** (wave 4 towns) the material overrides applied (a real asset's culture / climate instances; null: the mesh's own) */
	const void* MatsKey = nullptr;
private:
	FBox LocalBox = FBox(ForceInit);
	bool bLocalValid = false;
	uint32 ContentHash = 0;
};

class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UDmTownsFactory;
namespace dm { struct World; struct Event; }

UCLASS()
class DEMIURGE_API ADmTowns : public AActor
{
	GENERATED_BODY()

public:
	ADmTowns();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	static ADmTowns* Find(const UObject* WorldContext);

	// ---- tunables
	float HideKm = 70.f;                 // settlements farther than this are not drawn (houses < 1 px)
	/** (wave 4 towns) scale of the per-tier building hide distances (10 / 18 / 28 / 45 / 70 km); 0 = off (-townhidetier=) */
	float TierHideScale = 1.f;
	float ConstructionSeconds = 6.f;     // real seconds a new building stands as a scaffold
	/** (wave 4 towns) planning job slots out of view (background-low priority) and in view (background-normal: level with the
	 *  sky's weather builds and the planet's patches, GThreadPool's Normal, and never ahead of them by more than one slice) */
	int32 MaxTasks = 3;
	int32 MaxNearTasks = 3;
	/** (wave 4 towns) a planning job works in slices of about this many ms of CPU, then hands its worker back (-townslice=) */
	float SliceMs = 6.f;
	int32 MaxAppliesPerFrame = 8;
	int32 MaxLaunchesPerFrame = 4;       // (towns-perf) planning tasks started per frame
	/** (towns-perf) game-thread budget for applying finished plans per frame (at least one is applied); -townbudget=<ms> */
	float ApplyBudgetMs = 2.0f;
	/** (towns-perf) plan the first wave blocking for the settlements in view (-townblock); default: on workers, applied
	 *  under the budget nearest first (a whole city view blocked the game thread for 100-300 ms) */
	bool bBlockFirstWave = false;
	double CheckEvery = 0.5;             // seconds between signature checks on a live world
	double GrowthEvery = 4.0;            // a settlement that only grew or shrank is re-planned at most this often
	/** (towns-perf verifier) a settlement whose ground moved (a rising sea, erosion) is re-planned at most this often (s; -townground=) */
	double GroundReplanEvery = 6.0;
	bool bUnits = true;
	/** Field quads of the ground overlay: 1 always, 0 never, -1 auto (only when no planet module paints the farmland). */
	int32 FieldMode = 1;

	// ---- debug / scenario controls
	/** Plan every settlement at this culture age (-1 = the sim's). Re-plans everything. */
	void SetAgeOverride(int32 Age);
	/** Plan one settlement as if it had this tier and population (-1 = the sim's). */
	void SetOverride(int32 SettlementId, int32 Tier, double Pop, int32 Age = -1);
	void ClearOverrides();
	/** Re-plan everything now; bBlocking waits for the plans and applies them. */
	void RebuildAll(bool bBlocking, bool bNearOnly = false);
	void SetNightOverride(float Night);

	// ---- state for scenarios and tests
	bool IsSettled() const;
	bool HasGround() const { return Ground.IsValid(); }
	const FDmTownPlan* GetPlan(int32 SettlementId) const;
	FString Describe() const;
	int32 TotalInstances() const;
	int32 NumComponents() const;
	int32 NumVisibleTowns() const { return VisibleTowns; }
	int32 NumUnitsDrawn() const { return UnitsDrawn; }
	int64 PlansDone() const { return NumPlans; }
	/** Longest game-thread time of this actor's Tick (ms) since the last call; resets it. */
	double TakeMaxTickMs() { const double M = MaxTickMs; MaxTickMs = 0; return M; }
	double PlanMsTotal() const { return PlanMs; }
	UDmTownsFactory* GetFactory() const { return Factory; }
	/** World-space smoke anchors (chimneys, stacks, campfires) of visible settlements, for FX. */
	TArray<FVector> SmokeAnchors() const;
	/** World-space positions of the units drawn last frame (with their unit ids). */
	void UnitPositionsWorld(TArray<FVector>& OutPos, TArray<int32>& OutIds) const;
	/** World-space position of a settlement's plan centre (its plaza), or zero. */
	FVector TownCentreWorld(int32 SettlementId) const;
	/** Ground radius (uu, planet frame) under a unit planet-frame direction: the towns' ground, else the sim surface. */
	double GroundRadius(const FVector& PlanetDir) const;
	/** (towns-perf, test) every planned settlement draws exactly its plan: instance totals, meshes, ground overlay, visibility,
	 *  attachment to the planet. Returns the number of problems (Out: summary and the first few). */
	int32 CheckConsistency(FString& Out) const;

private:
	struct FTown
	{
		int32 Id = -1;
		uint32 Sig = 0;
		uint32 PendingSig = 0;
		uint32 Struct = 0;               // structural part of the signature (tier, age, buildings, dead, culture)
		double LastPlan = -1e9;          // real seconds of the last applied plan
		TSharedPtr<FDmTownPlan, ESPMode::ThreadSafe> Plan;
		TArray<TObjectPtr<UInstancedStaticMeshComponent>> Comps;
		TArray<uint32> CompKeys;         // (towns-perf) batch key per component (components are reused across re-plans)
		TObjectPtr<UStaticMeshComponent> GroundComp = nullptr;
		TObjectPtr<UStaticMesh> GroundMesh = nullptr;
		TMap<uint32, double> Building;   // instance key -> construction start (real seconds)
		double NextFinish = 1e30;        // (towns-perf) earliest scaffold finish (construction passes skip the town before it)
		bool bVisible = true;            // its buildings are drawn (wave 4 towns: hidden beyond a distance by tier)
		bool bGroundVis = true;          // (wave 4 towns) its ground overlay (fields, streets) is drawn: out to HideKm
		bool bDirty = false;             // re-plan wanted
		uint32 GroundGen = 0;            // bumped when the ground under this settlement changed (stale in-flight plans)
		bool bGroundOnly = false;        // (towns-perf verifier) dirty only because the ground moved (re-planned at most every GroundReplanEvery)
		double FirstSeen = 0;            // (towns-perf verifier) real seconds the settlement was first seen (coverage check)
		double StructSeen = 0;           // (towns-perf verifier) real seconds its structure was first seen differing from the drawn plan's
		uint32 PlanStruct = 0;           // (towns-perf verifier) structural signature the drawn plan was made from
	};
public:
	/** (wave 4 towns) One settlement's planning job: a chain of short tasks (FDmTownPlanStepper slices), each re-launched at the
	 *  job's current priority; owns everything it reads, so an abandoned job (exit, world change) finishes on its own. */
	struct FPlanJob
	{
		FDmTownInput In;
		TSharedPtr<const FDmTownsWorldSnap, ESPMode::ThreadSafe> Snap;
		FDmTownsGroundPtr Ground;
		TSharedPtr<FDmKitTable, ESPMode::ThreadSafe> Kit;
		TSharedPtr<const FDmTownPlan, ESPMode::ThreadSafe> OldPlan;
		uint32 Sig = 0;
		float SliceMs = 6.f;
		std::atomic<bool> Cancel{ false };
		std::atomic<bool> bDone{ false };
		std::atomic<int32> Pri{ 0 };                     // UE::Tasks::ETaskPriority of the next slice
		TSharedPtr<FDmTownPlan, ESPMode::ThreadSafe> Plan;  // valid once bDone (null: cancelled)
		TUniquePtr<FDmTownPlanStepper> Stepper;
		double Started = 0, Finished = 0;                // real seconds (first slice started, last slice ended)
	};
private:
	struct FPending
	{
		TSharedPtr<FPlanJob, ESPMode::ThreadSafe> Job;
		uint32 Sig = 0;
		uint32 GroundGen = 0;
		const FDmTownPlan* OldPlan = nullptr;   // the plan it replaces (construction diff made on the worker against it)
		bool bNear = false;                     // planned at the in-view priority (a settlement the camera can see)
		uint32 Struct = 0;                      // (towns-perf verifier) structural signature at launch
		double Launched = 0;                    // (towns-perf verifier) real seconds
	};
	/** (towns-perf verifier) a ground move marks the settlement for a ground-only re-plan (unless something else already did). */
	static void MarkGround(FTown& T) { if (!T.bDirty) T.bGroundOnly = true; T.bDirty = true; ++T.GroundGen; }
	/** (wave 4 towns) the look of a batch: the real asset (state, culture-style materials) or the generated stand-in. */
	struct FLook { UStaticMesh* Mesh = nullptr; const TArray<UMaterialInterface*>* Mats = nullptr; float Size = 8.f; bool bAssetCons = false; };
	FLook ResolveLook(const FDmTownPlan& P, uint32 Key) const;
	UStaticMesh* UnitMesh(EDmTownMesh M, int32 V, int32 Age) const;
	uint8 CultureStyleOf(const dm::World& W, int32 Culture) const;
	mutable TMap<int32, uint8> CultureStyles;   // per culture id (this world)
	mutable float ElevQ = -1.f, SlopeQ = -1.f;  // land elevation 75th / slope 80th percentile (this world)
	/** (wave 4 towns) slice and job statistics (towns perf lines) */
	double MaxSliceMs = 0, MaxSliceCpuMs = 0, JobWallMs = 0;
	int32 JobsDone = 0, SlicesDone = 0;
	/** (towns-perf) a new ground snapshot completed off the game thread: our own capture of the planet terrain prepared on a
	 *  worker, then the probe of which settlements it moved. Adopted by the tick that finds both tasks done. */
	struct FGroundProbe { int32 Id = -1; TSharedPtr<const FDmTownPlan, ESPMode::ThreadSafe> Plan; FVector Centre = FVector::ZeroVector; float RadiusM = 0, ExtentM = 0; };
	/** Settlements re-planned while a ground job ran: probed against the adopted ground on a worker afterwards. */
	struct FLateProbe
	{
		UE::Tasks::FTask Task;
		FDmTownsGroundPtr Old, G;
		TArray<FGroundProbe> Probes;
		TArray<int32> Moved;
	};
	struct FGroundJob
	{
		UE::Tasks::FTask Task;                 // FDmPlanetTerrain::Prepare of our capture
		UE::Tasks::FTask Probe;                // which settlements it moved (reads only our copies)
		TSharedPtr<FDmTownsGround, ESPMode::ThreadSafe> G;
		FDmTownsGroundPtr Old;
		uint64 Sig = 0;
		const TCHAR* Why = TEXT("");
		bool bInitial = false;
		TArray<FGroundProbe> Probes;
		TArray<int32> Moved;
		double ProbeMs = 0, CaptureMs = 0;
	};

	void OnDirty(const FDmDirtyFlags& F);
	void OnWorldChanged(const dm::World* W);
	void OnSimEvent(const dm::Event& E);
	/** Launches the ground job (deferred planet copy); false when the planet path is not available (then synchronous). */
	bool LaunchGroundJob(const dm::World& W, bool bInitial, const TCHAR* Why);
	void FinishGroundJob();
	void JoinAll();
	void FinishLateProbes();
	/** The planet's current terrain has the same height inputs as our ground's (no re-capture needed). */
	bool SameHeights() const;
	/** Drops two shared references on a background worker (whatever they free is freed there, not on the game thread). */
	template <typename A, typename B> static void ReleaseOnWorker(A&& X, B&& Y);
	void ResetAll();
	void FollowPlanet();
	bool EnsureGround(const dm::World& W);
	/** Adopt a new ground snapshot: only settlements whose ground actually moved are re-planned. */
	void AdoptGround(const FDmTownsGroundPtr& NewGround, uint64 NewSig, const TCHAR* Why);
	void EnsureSnap(const dm::World& W, bool bForce);
	uint32 SignatureOf(const dm::World& W, int32 Sid, uint32* OutStruct = nullptr) const;
	bool MakeInput(const dm::World& W, int32 Sid, FDmTownInput& Out) const;
	void CheckSignatures(const dm::World& W, bool bAll);
	void LaunchPlans(const dm::World& W);
	void ApplyDone(double TickStart);
	void ApplyPlan(FTown& T, const TSharedPtr<FDmTownPlan, ESPMode::ThreadSafe>& P, bool bConstruct);
	/** bInstancesOnly: keep the ground overlay (construction finishing changes only the instances). */
	void BuildComponents(FTown& T, bool bInstancesOnly = false);
	void DestroyComponents(FTown& T, bool bInstancesOnly = false);
	/** (towns-perf) pool of registered, hidden, frozen, empty components: taken by applies, returned instead of destroyed. */
	UDmTownsISM* NewPooled(UStaticMesh* Mesh = nullptr);
	void WarmComponents();
	UDmTownsISM* TakePooled(UStaticMesh* Mesh);
	void ReturnPooled(UInstancedStaticMeshComponent* C);
	void RefillPool();
	void UpdateVisibility();
	void UpdateConstruction(double BudgetMs);
	bool CameraPlanet(FVector& Out) const;

	// units
	void UpdateUnits(const dm::World& W, float Dt);
	int32 UnitComp(UStaticMesh* Mesh);

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UDmTownsFactory> Factory;
	UPROPERTY() TArray<TObjectPtr<UObject>> Keep;   // components and meshes of the towns (GC roots)
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> UnitComps;
	UPROPERTY() TArray<TObjectPtr<UDmTownsISM>> Pool;
	int32 PoolTarget = 64, PoolMax = 320;
	double PoolBudgetMs = 0.6;

	mutable TArray<FVector> DirCache;   // (towns-perf) settlement directions, per frame (MakeInput's neighbour search)
	mutable uint64 DirCacheFrame = MAX_uint64;
	TMap<int32, FTown> Towns;
	TMap<int32, FPending> Pending;
	TSharedPtr<FDmKitTable, ESPMode::ThreadSafe> KitTable;
	FDmTownsGroundPtr Ground;
	TSharedPtr<const FDmTownsWorldSnap, ESPMode::ThreadSafe> Snap;
	TSharedPtr<FGroundJob, ESPMode::ThreadSafe> GroundJob;
	TArray<TSharedPtr<FLateProbe, ESPMode::ThreadSafe>> LateProbes;
	int32 GroundSkips = 0;

	double SnapTime = -1e9;
	bool bSnapDirty = true;
	uint64 GroundSig = 0;
	double LastGroundCheck = -1e9, LastCheck = -1e9, LastConstruction = -1e9, LastElevReground = -1e9;
	bool bElevDirty = false;
	double FirstTick = -1;
	bool bWorldPending = true;
	bool bFirstWave = true;
	bool bFirstWaveFill = false;          // (towns-perf) the first wave is still being planned in the background
	double FirstWaveStart = 0, FirstWavePlanMs = 0;
	int32 FirstWaveWait = 0;
	bool bCheckAll = true;
	TSet<int32> EventTouched;
	int32 AgeOverride = -1;
	struct FOverride { int32 Tier = -1; double Pop = -1; int32 Age = -1; };
	TMap<int32, FOverride> Overrides;
	float Scale = 1.f;
	float NightOverride = -1.f;
	bool bLog = false;
	FTransform LastPlanet = FTransform(FVector(1e30));
	FDelegateHandle DirtyHandle, WorldHandle, EventHandle;
	const dm::World* SeenWorld = nullptr;
	int32 VisibleTowns = 0;
	int32 VisibleComps = 0;              // (wave 4 towns) building components of the visible settlements
	int64 NumPlans = 0;
	double PlanMs = 0;
	double LastVis = -1e9;
	double MaxTickMs = 0;

	// units: smoothed positions per unit id
	struct FMover { FVector Pos = FVector::ZeroVector; FVector Fwd = FVector::ForwardVector; bool bInit = false; int32 Comp = -1; int32 Seen = 0; };
	TMap<int32, FMover> Movers;
	TArray<UStaticMesh*> UnitCompMesh;   // (wave 4 towns) the mesh of each unit component (a real asset or a stand-in)
	TArray<TArray<FTransform>> UnitXf;   // per unit component, reused every frame
	TArray<TArray<float>> UnitCd;
	TArray<int32> UnitLastN;             // (wave 4 towns) units drawn by each component last frame (0: all its slots hidden)
	int32 UnitsDrawn = 0;
	int32 UnitFrame = 0;
};
