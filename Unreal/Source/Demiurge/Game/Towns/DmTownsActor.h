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
//     immediate. Plans run on worker threads; the first wave of a world is planned in parallel and blocking
//     (scenarios see a complete world in their first frame). New buildings stand as scaffolds for
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
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/Core/DmDirtyRouter.h"
#include "Game/Towns/DmTownsPlan.h"
#include "Tasks/Task.h"
#include "DmTownsActor.generated.h"

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
	float ConstructionSeconds = 6.f;     // real seconds a new building stands as a scaffold
	int32 MaxTasks = 6;
	int32 MaxAppliesPerFrame = 2;
	double CheckEvery = 0.5;             // seconds between signature checks on a live world
	double GrowthEvery = 4.0;            // a settlement that only grew or shrank is re-planned at most this often
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
		TObjectPtr<UStaticMeshComponent> GroundComp = nullptr;
		TObjectPtr<UStaticMesh> GroundMesh = nullptr;
		TMap<uint32, double> Building;   // instance key -> construction start (real seconds)
		bool bVisible = true;
		bool bDirty = false;             // re-plan wanted
		uint32 GroundGen = 0;            // bumped when the ground under this settlement changed (stale in-flight plans)
	};
	struct FPending
	{
		UE::Tasks::TTask<TSharedPtr<FDmTownPlan, ESPMode::ThreadSafe>> Task;
		uint32 Sig = 0;
		uint32 GroundGen = 0;
	};

	void OnDirty(const FDmDirtyFlags& F);
	void OnWorldChanged(const dm::World* W);
	void OnSimEvent(const dm::Event& E);
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
	void ApplyDone();
	void ApplyPlan(FTown& T, const TSharedPtr<FDmTownPlan, ESPMode::ThreadSafe>& P, bool bConstruct);
	/** bInstancesOnly: keep the ground overlay (construction finishing changes only the instances). */
	void BuildComponents(FTown& T, bool bInstancesOnly = false);
	void DestroyComponents(FTown& T, bool bInstancesOnly = false);
	void UpdateVisibility();
	void UpdateConstruction();
	bool CameraPlanet(FVector& Out) const;

	// units
	void UpdateUnits(const dm::World& W, float Dt);

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UDmTownsFactory> Factory;
	UPROPERTY() TArray<TObjectPtr<UObject>> Keep;   // components and meshes of the towns (GC roots)
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> UnitComps;

	TMap<int32, FTown> Towns;
	TMap<int32, FPending> Pending;
	TSharedPtr<FDmKitTable, ESPMode::ThreadSafe> KitTable;
	FDmTownsGroundPtr Ground;
	TSharedPtr<const FDmTownsWorldSnap, ESPMode::ThreadSafe> Snap;
	double SnapTime = -1e9;
	bool bSnapDirty = true;
	uint64 GroundSig = 0;
	double LastGroundCheck = -1e9, LastCheck = -1e9, LastConstruction = -1e9, LastElevReground = -1e9;
	bool bElevDirty = false;
	double FirstTick = -1;
	bool bWorldPending = true;
	bool bFirstWave = true;
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
	int64 NumPlans = 0;
	double PlanMs = 0;
	double LastVis = -1e9;
	double MaxTickMs = 0;

	// units: smoothed positions per unit id
	struct FMover { FVector Pos = FVector::ZeroVector; FVector Fwd = FVector::ForwardVector; bool bInit = false; int32 Comp = -1; int32 Seen = 0; };
	TMap<int32, FMover> Movers;
	TArray<int32> UnitCompMesh;     // EDmTownMesh * 8 + variant per unit component
	TArray<TArray<FTransform>> UnitXf;   // per unit component, reused every frame
	TArray<TArray<float>> UnitCd;
	int32 UnitsDrawn = 0;
	int32 UnitFrame = 0;
};
