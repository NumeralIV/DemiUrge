// DEMIURGE — Game/Towns/DmTownsActor.cpp : see the header.

#include "Game/Towns/DmTownsActor.h"
#include "Game/Towns/DmTownsKit.h"
#include "Game/Towns/DmTownsAssets.h"
#include "Materials/MaterialInterface.h"
#include "Game/Towns/DmTownsPerf.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmScene.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Game/Planet/DmPlanet.h"
#include "Game/Planet/DmPlanetTerrain.h"

#include "Async/ParallelFor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"
#include "Misc/Crc.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sim/State.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmTowns, Log, All);

namespace
{
	double NowS() { return FPlatformTime::Seconds(); }

	uint32 Mix(uint32 H, uint32 V) { return DmTowns::Hash(H, V, 0x9E37u); }

	float CullS(float S)
	{
		if (S < 14.f) return 26000.f;
		if (S < 40.f) return 45000.f;
		return 90000.f;
	}

	constexpr uint32 ScaffoldBit = 0x80000000u;

	/** (towns-perf) A hidden settlement's components are switched to absolute transforms: USceneComponent::UpdateChildTransforms
	 *  skips fully absolute children, so the planet turning (the towns actor moves every frame in a live world) no longer
	 *  walks ~1700 components, only the visible ones. Unfreezing re-attaches them to the planet frame. */
	void FreezeComp(USceneComponent* C, bool bFreeze)
	{
		if (!C) return;
		if (C->IsUsingAbsoluteLocation() == bFreeze && C->IsUsingAbsoluteRotation() == bFreeze && C->IsUsingAbsoluteScale() == bFreeze) return;
		C->SetUsingAbsoluteLocation(bFreeze);
		C->SetUsingAbsoluteRotation(bFreeze);
		C->SetUsingAbsoluteScale(bFreeze);
		if (!bFreeze) C->UpdateComponentToWorld();
	}

	/** Did the ground under a settlement move between two snapshots (5 cm), or does a moved sea now reach it? Thread-safe. */
	bool GroundMovedAt(const FDmTownsGround& Old, const FDmTownsGround& G, const FVector& Centre, double RadiusM, double ExtentM)
	{
		const FVector U = Centre.GetSafeNormal();
		FVector E, N;
		DmTownsPlan::Frame(U, E, N);
		const double Rr[3] = { 0.0, FMath::Max(RadiusM * 0.6, 40.0), FMath::Max(ExtentM * 0.7, 120.0) };
		double MinR = 1e30;
		for (int32 Ri = 0; Ri < 3; ++Ri)
			for (int32 K = 0; K < (Ri == 0 ? 1 : 8); ++K)
			{
				const double A = K * UE_DOUBLE_TWO_PI / 8.0 + Ri * 0.39;
				const FVector D = (U * 1.0e7 + (E * FMath::Cos(A) + N * FMath::Sin(A)) * Rr[Ri] * 100.0).GetSafeNormal();
				const double Rn = G.Radius(D);
				MinR = FMath::Min(MinR, Rn);
				if (FMath::Abs(Old.Radius(D) - Rn) > 5.0) return true;   // 5 cm
			}
		// (integration) a rising sea moves the planet's sea level in 0.5 m steps several times a second at 8x in the late
		// ages; only towns whose ground comes within 30 m of the new sea are re-planned (shore, docks, piers)
		return FMath::Abs(Old.SeaRadiusUU - G.SeaRadiusUU) > 5.0 && MinR - G.SeaRadiusUU < 3000.0;
	}

	/** (towns-perf) Everything the game thread would otherwise do per apply, done by the planning task: instances grouped
	 *  into component batches, the ground overlay's render data, and the construction diff against the plan replaced. */
	void PreparePlan(FDmTownPlan& P, const FDmTownPlan* Old, bool bGroundData)
	{
		const double T0 = FPlatformTime::Seconds();
		TMap<uint32, int32> Bi;
		P.Batches.Reset();
		for (int32 I = 0; I < P.Inst.Num(); ++I)
		{
			const FDmTownInst& In = P.Inst[I];
			const uint32 Key = DmTownsBatchKey::Make(In.Mesh, In.Var, In.Sub, In.State);   // (wave 4 towns) + asset alternative and state
			int32& B = Bi.FindOrAdd(Key, INDEX_NONE);
			if (B == INDEX_NONE) { B = P.Batches.Num(); P.Batches.AddDefaulted_GetRef().Key = Key; }
			FDmTownBatch& Bt = P.Batches[B];
			Bt.Xf.Add(FTransform(FQuat(In.Rot), FVector(In.Pos), FVector(In.Scale)));
			Bt.Cd.Append(In.Cd, UDmTownsFactory::NumCustomData);
			Bt.Idx.Add(I);
		}
		P.Batches.Sort([](const FDmTownBatch& A, const FDmTownBatch& B) { return A.Key < B.Key; });
		if (bGroundData && P.Ground.NumTris() > 0) P.GroundData = DmTowns::BuildMeshData({ &P.Ground }, 1, { 1.f }, false, true);
		P.NewKeys.Reset();
		if (Old && !P.Inst.IsEmpty())
		{
			// (verifier fix) a building is new only where nothing stood before. Slot keys hash the exact position, and a
			// re-plan shifts the streets slightly whenever the radius or a neighbour changes, so the key diff turned 76% of
			// all instances into scaffolds on every re-plan of a live world (towns-live: 264k of 348k). Match by place
			// instead: a new instance within 7 m of any old one (any kind: rebuilt or upgraded in place) is not under
			// construction.
			TSet<uint32> OldKeys;
			OldKeys.Reserve(Old->Inst.Num());
			TMultiMap<FIntVector, FVector> OldAt;
			constexpr double CellUU = 1000.0, NearUU = 700.0;
			for (const FDmTownInst& I : Old->Inst)
			{
				OldKeys.Add(I.Key);
				const FVector A = Old->Centre + FVector(I.Pos);
				OldAt.Add(FIntVector(FMath::FloorToInt(A.X / CellUU), FMath::FloorToInt(A.Y / CellUU), FMath::FloorToInt(A.Z / CellUU)), A);
			}
			auto StoodThere = [&](const FVector& A) -> bool
			{
				const FIntVector C0(FMath::FloorToInt(A.X / CellUU), FMath::FloorToInt(A.Y / CellUU), FMath::FloorToInt(A.Z / CellUU));
				TArray<FVector, TInlineAllocator<16>> Hits;
				for (int32 Dz = -1; Dz <= 1; ++Dz)
					for (int32 Dy = -1; Dy <= 1; ++Dy)
						for (int32 Dx = -1; Dx <= 1; ++Dx)
						{
							Hits.Reset();
							OldAt.MultiFind(C0 + FIntVector(Dx, Dy, Dz), Hits);
							for (const FVector& H : Hits) if (FVector::DistSquared(H, A) < NearUU * NearUU) return true;
						}
				return false;
			};
			bool bRuins = false;
			for (const FDmTownInst& I : P.Inst) if (I.Cd[7] > 0.5f) { bRuins = true; break; }
			if (!bRuins)
				for (const FDmTownInst& I : P.Inst)
					if (!OldKeys.Contains(I.Key) && I.Mesh != EDmTownMesh::Boat && I.Mesh < EDmTownMesh::Galley && !StoodThere(P.Centre + FVector(I.Pos)))
						P.NewKeys.Add(I.Key);
		}
		P.bPrepared = true;
		P.PrepMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	}

	/** (wave 4 towns) One slice of a planning job, then the next slice is launched at the job's current priority (raised when
	 *  its settlement comes into view): the plan gives its worker back every SliceMs, so higher-priority work (the sky's weather
	 *  build, the planet's patches) never waits behind a whole plan. A cancelled job stops at its next slice. */
	void RunPlanSlice(TSharedPtr<ADmTowns::FPlanJob, ESPMode::ThreadSafe> J)
	{
		ADmTowns::FPlanJob& Jb = *J;
		if (Jb.Cancel.load(std::memory_order_relaxed))
		{
			Jb.Stepper.Reset();
			Jb.Plan.Reset();
			Jb.Finished = FPlatformTime::Seconds();
			Jb.bDone.store(true, std::memory_order_release);
			return;
		}
		if (!Jb.Stepper.IsValid())
		{
			Jb.Started = FPlatformTime::Seconds();
			Jb.Plan = MakeShared<FDmTownPlan, ESPMode::ThreadSafe>();
			Jb.Stepper = MakeUnique<FDmTownPlanStepper>(Jb.In, *Jb.Snap, *Jb.Ground, *Jb.Kit, *Jb.Plan, &Jb.Cancel);
		}
		const double Cpu0 = FDmTownsPerf::ThreadCpuMs();
		const bool bLast = Jb.Stepper->Step(Jb.SliceMs);
		if (Jb.Plan.IsValid()) Jb.Plan->MaxSliceCpuMs = FMath::Max(Jb.Plan->MaxSliceCpuMs, FDmTownsPerf::ThreadCpuMs() - Cpu0);
		if (bLast)
		{
			const bool bCancelled = Jb.Stepper->WasCancelled();
			Jb.Stepper.Reset();
			if (bCancelled) Jb.Plan.Reset();
			else
			{
				Jb.Plan->Sig = Jb.Sig;
				// the prepare (batches, ground render data, construction diff) is one more slice
				const double T0 = FPlatformTime::Seconds(), C0 = FDmTownsPerf::ThreadCpuMs();
				PreparePlan(*Jb.Plan, Jb.OldPlan.Get(), true);
				Jb.Plan->MaxSliceMs = FMath::Max(Jb.Plan->MaxSliceMs, (FPlatformTime::Seconds() - T0) * 1000.0);
				Jb.Plan->MaxSliceCpuMs = FMath::Max(Jb.Plan->MaxSliceCpuMs, FDmTownsPerf::ThreadCpuMs() - C0);
			}
			Jb.Finished = FPlatformTime::Seconds();
			Jb.bDone.store(true, std::memory_order_release);
			return;
		}
		UE::Tasks::Launch(UE_SOURCE_LOCATION, [J]() { RunPlanSlice(J); }, UE::Tasks::ETaskPriority(Jb.Pri.load(std::memory_order_relaxed)));
	}
}

// ------------------------------------------------------------------ UDmTownsISM

FBoxSphereBounds UDmTownsISM::CalcBounds(const FTransform& LocalToWorld) const
{
	if (bLocalValid && LocalBox.IsValid) return FBoxSphereBounds(LocalBox.TransformBy(LocalToWorld));
	return Super::CalcBounds(LocalToWorld);
}

bool UDmTownsISM::SetInstances(const TArray<FTransform>& Xf, const TArray<float>& Cd, bool bLive)
{
	const int32 N = Xf.Num();
	uint32 H = FCrc::MemCrc32(Xf.GetData(), Xf.Num() * sizeof(FTransform), uint32(N));
	H = FCrc::MemCrc32(Cd.GetData(), Cd.Num() * sizeof(float), H);
	if (bLive && H == ContentHash && GetInstanceCount() == N) return false;
	ContentHash = H;
	bLocalValid = false;
	if (bLive && N > 0 && GetInstanceCount() == N) BatchUpdateInstancesTransforms(0, Xf, false, true, true);
	else
	{
		ClearInstances();
		if (N > 0) { PreAllocateInstancesMemory(N); AddInstances(Xf, false, false, false); }
	}
	if (N > 0 && Cd.Num() >= N * NumCustomDataFloats && NumCustomDataFloats > 0) SetCustomData(0, N - 1, TConstArrayView<float>(Cd.GetData(), N * NumCustomDataFloats), bLive);
	// bounds in component space once: every later move (the planet turns) is then O(1)
	LocalBox = N > 0 ? Super::CalcBounds(FTransform::Identity).GetBox() : FBox(ForceInit);
	bLocalValid = N > 0;
	if (bLive) { UpdateBounds(); MarkRenderStateDirty(); }
	return true;
}

ADmTowns::ADmTowns()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;
}

ADmTowns* ADmTowns::Find(const UObject* WorldContext)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) return nullptr;
	for (TActorIterator<ADmTowns> It(W); It; ++It) return *It;
	return nullptr;
}

void ADmTowns::BeginPlay()
{
	Super::BeginPlay();
	FDmTownsPerf::ThreadCpuMs();   // (wave 4 towns) calibrate the thread CPU clock here, before the planning workers read it
	Factory = NewObject<UDmTownsFactory>(this);
	Factory->Build(GetWorld());
	KitTable = MakeShared<FDmKitTable, ESPMode::ThreadSafe>();
	Factory->FillKitTable(*KitTable);   // (wave 4 towns) + the real assets' footprints per age
	{ double Sl = SliceMs; if (FDmShotArgs::Value(TEXT("-townslice="), Sl)) SliceMs = float(FMath::Clamp(Sl, 0.5, 1000.0)); }

	double D = 0;
	int32 Iv = -1;
	if (FDmShotArgs::Value(TEXT("-townage="), Iv)) AgeOverride = FMath::Clamp(Iv, 0, 6);
	if (FDmShotArgs::Value(TEXT("-townscale="), D)) Scale = float(FMath::Clamp(D, 0.2, 5.0));
	if (FDmShotArgs::Value(TEXT("-townhidekm="), D)) HideKm = float(FMath::Max(D, 1.0));
	if (FDmShotArgs::Value(TEXT("-townhidetier="), D)) TierHideScale = float(FMath::Max(D, 0.0));
	if (FDmShotArgs::Value(TEXT("-townground="), D)) GroundReplanEvery = FMath::Clamp(D, 0.0, 120.0);
	if (FDmShotArgs::Value(TEXT("-townnight="), D)) NightOverride = float(D);
	if (FDmShotArgs::Param(TEXT("nounits"))) bUnits = false;
	if (FDmShotArgs::Value(TEXT("-townfields="), Iv)) FieldMode = FMath::Clamp(Iv, -1, 1);
	bLog = FDmShotArgs::Param(TEXT("townlog"));
	bBlockFirstWave = FDmShotArgs::Param(TEXT("townblock"));
	if (FDmShotArgs::Value(TEXT("-townbudget="), D)) ApplyBudgetMs = float(FMath::Clamp(D, 0.1, 100.0));
	double Gl[3] = { 3.0, 1.6, 1.4 };
	FDmShotArgs::List(TEXT("-townglow="), Gl, 3);
	Factory->SetGlow(float(Gl[0]), float(Gl[1]), float(Gl[2]));
	Factory->SetNightOverride(NightOverride);

	WarmComponents();
	if (UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
	{
		DirtyHandle = Sim->Dirty().OnDirty.AddUObject(this, &ADmTowns::OnDirty);
		WorldHandle = Sim->OnWorldChanged.AddUObject(this, &ADmTowns::OnWorldChanged);
		EventHandle = Sim->OnSimEvent.AddUObject(this, &ADmTowns::OnSimEvent);
	}
	FollowPlanet();
}

void ADmTowns::EndPlay(const EEndPlayReason::Type Reason)
{
	UE_LOG(LogDmTowns, Display, TEXT("DmTowns: EndPlay: %d planning jobs done in %d slices (longest slice %.1f ms CPU, %.1f ms wall; mean job %.0f ms wall)"),
		JobsDone, SlicesDone, MaxSliceCpuMs, MaxSliceMs, JobsDone ? JobWallMs / JobsDone : 0.0);
	JoinAll();
	if (UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
	{
		Sim->Dirty().OnDirty.Remove(DirtyHandle);
		Sim->OnWorldChanged.Remove(WorldHandle);
		Sim->OnSimEvent.Remove(EventHandle);
	}
	Super::EndPlay(Reason);
}

void ADmTowns::JoinAll()
{
	// (wave 4 towns) never wait. Exit waited 50-60 s on a loaded machine for plans already running (the old cancel flag only
	// stopped plans not yet started). Planning jobs own everything they read (shared snapshots), so they are abandoned: the
	// cancel flag stops each at its next slice (a few ms of work), and the last reference frees it on its worker. The ground
	// job and late probes are handed to a cleanup task that keeps them alive until they finish (they read their own copies).
	const double T0 = NowS();
	int32 Running = 0;
	for (TPair<int32, FPending>& P : Pending)
		if (P.Value.Job.IsValid())
		{
			P.Value.Job->Cancel.store(true, std::memory_order_relaxed);
			Running += P.Value.Job->bDone.load(std::memory_order_acquire) ? 0 : 1;
		}
	const int32 NumJobs = Pending.Num();
	Pending.Reset();
	int32 NumGround = 0;
	if (GroundJob.IsValid())
	{
		TSharedPtr<FGroundJob, ESPMode::ThreadSafe> J = MoveTemp(GroundJob);
		NumGround += (J->Task.IsCompleted() && J->Probe.IsCompleted()) ? 0 : 1;
		UE::Tasks::Launch(UE_SOURCE_LOCATION, [J]() mutable { J.Reset(); }, UE::Tasks::Prerequisites(J->Task, J->Probe), UE::Tasks::ETaskPriority::BackgroundLow);
	}
	for (TSharedPtr<FLateProbe, ESPMode::ThreadSafe>& L : LateProbes)
	{
		NumGround += L->Task.IsCompleted() ? 0 : 1;
		TSharedPtr<FLateProbe, ESPMode::ThreadSafe> Lp = MoveTemp(L);
		UE::Tasks::Launch(UE_SOURCE_LOCATION, [Lp]() mutable { Lp.Reset(); }, UE::Tasks::Prerequisites(Lp->Task), UE::Tasks::ETaskPriority::BackgroundLow);
	}
	LateProbes.Reset();
	// (wave 4 towns) not even at exit: a bounded wait (2 s) for the cancelled jobs was tried against the engine's exit assertion
	// "Work->GetRefCount() == 0" (QueuedThreadPoolWrapper.cpp:167, GThreadPool torn down with queued work). It did not help: the
	// assertion also fires in lanes whose towns joined every task (camera4b-verify, perf4, audio4b-verify, planet4-verify), and
	// the queued slices did not get a worker within the 2 s anyway. It is GThreadPool work (planet patches, the sky's weather
	// build) left queued at teardown: reported to the Core / bridge owner.
	if (NumJobs + NumGround > 0)
		UE_LOG(LogDmTowns, Display, TEXT("DmTowns: abandoned %d planning jobs (%d were running) and %d ground jobs in %.2f ms (not waited for; each stops at its next slice)"),
			NumJobs, Running, NumGround, (NowS() - T0) * 1000.0);
}

// ------------------------------------------------------------------ notifications

void ADmTowns::OnDirty(const FDmDirtyFlags& F)
{
	if (F.bObjects) bCheckAll = true;
	if (F.bColor || F.bObjects || F.bRoads) bSnapDirty = true;
	if (F.bElev || F.bWater) { bSnapDirty = true; LastGroundCheck = -1e9; bElevDirty = true; }
}

void ADmTowns::OnWorldChanged(const dm::World*)
{
	bWorldPending = true;
}

void ADmTowns::OnSimEvent(const dm::Event& E)
{
	if (const dm::EvSettlementFounded* A = E.as<dm::EvSettlementFounded>()) EventTouched.Add(A->id);
	else if (const dm::EvSettlementTier* B = E.as<dm::EvSettlementTier>()) EventTouched.Add(B->id);
	else if (const dm::EvSettlementBuilding* C = E.as<dm::EvSettlementBuilding>()) EventTouched.Add(C->id);
	else if (const dm::EvSettlementDestroyed* D = E.as<dm::EvSettlementDestroyed>()) EventTouched.Add(D->id);
	else if (const dm::EvSettlementCaptured* F = E.as<dm::EvSettlementCaptured>()) EventTouched.Add(F->id);
	else if (E.as<dm::EvCultureAge>() || E.as<dm::EvWonderBuilt>() || E.as<dm::EvArkStarted>()) bCheckAll = true;
}

void ADmTowns::ResetAll()
{
	JoinAll();
	for (TPair<int32, FTown>& T : Towns) DestroyComponents(T.Value);
	Towns.Reset();
	// (towns-perf) unit components and the pool are per mesh, not per world: kept (warmed once, in BeginPlay)
	for (UInstancedStaticMeshComponent* C : UnitComps) if (C) C->ClearInstances();
	Movers.Reset();
	Ground.Reset();
	Snap.Reset();
	CultureStyles.Reset();
	ElevQ = SlopeQ = -1.f;
	bSnapDirty = true;
	GroundSig = 0;
	FirstTick = -1;
}

// ------------------------------------------------------------------ frame

void ADmTowns::FollowPlanet()
{
	const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	if (!Sim) return;
	const FTransform T = Sim->GetPlanetToWorld();
	if (!T.Equals(LastPlanet, 1e-6))
	{
		LastPlanet = T;
		SetActorTransform(T);
	}
	if (Factory) Factory->SetFrame(T.GetLocation(), Sim->GetSunDirection());
}

bool ADmTowns::CameraPlanet(FVector& Out) const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->PlayerCameraManager) return false;
	Out = LastPlanet.InverseTransformPosition(PC->PlayerCameraManager->GetCameraLocation());
	return !Out.IsNearlyZero();
}

bool ADmTowns::EnsureGround(const dm::World& W)
{
	const double Now = NowS();
	if (FirstTick < 0) FirstTick = Now;
	if (LateProbes.Num()) FinishLateProbes();
	// (towns-perf) a finished ground job (planet terrain captured, prepared and probed on workers) is adopted here
	if (GroundJob.IsValid())
	{
		if (!GroundJob->Task.IsCompleted() || !GroundJob->Probe.IsCompleted()) return Ground.IsValid();
		FinishGroundJob();
	}
	if (Ground.IsValid() && Now - LastGroundCheck > 2.0)
	{
		LastGroundCheck = Now;
		const uint64 S = FDmTownsGround::Signature(GetWorld());
		if (S != GroundSig && SameHeights())
		{
			// (towns-perf) the planet re-captured for roads, water or colours: the heights towns stand on did not change, so
			// no capture at all (the common case in a live world: ~2 re-versions a second, almost none move the ground)
			GroundSig = S;
			++GroundSkips;
		}
		else if (S != GroundSig && !LaunchGroundJob(W, false, TEXT("ground changed")))
		{
			bool bPend = false;
			FDmTownsGroundPtr G = FDmTownsGround::Snapshot(GetWorld(), W, bPend);
			if (!bPend) AdoptGround(G, S, TEXT("ground changed"));
		}
	}
	// a registered planet provider has no signature to watch: re-ground the settlements after terrain edits
	// (sculpting, craters, volcanoes), at most every 5 s (plans are progressive and on worker threads)
	if (Ground.IsValid() && bElevDirty && Ground->GetSource() == FDmTownsGround::ESource::Provider && Now - LastElevReground > 5.0)
	{
		bool bPend = false;
		FDmTownsGroundPtr G = FDmTownsGround::Snapshot(GetWorld(), W, bPend);
		bElevDirty = false;
		LastElevReground = Now;
		AdoptGround(G, GroundSig, TEXT("terrain edited"));
	}
	if (Ground.IsValid()) return true;
	if (LaunchGroundJob(W, true, TEXT("ground"))) return false;
	bool bPend = false;
	FDmTownsGroundPtr G = FDmTownsGround::Snapshot(GetWorld(), W, bPend);
	if (bPend && Now - FirstTick < 15.0) return false;   // the placeholder planet builds its mesh in its first tick
	Ground = G;
	GroundSig = G->Sig;
	LastGroundCheck = Now;
	UE_LOG(LogDmTowns, Display, TEXT("DmTowns: ground from the %s (%d triangles, %.0f ms)"), Ground->SourceName(), Ground->Triangles, Ground->BuildMs);
	return true;
}

bool ADmTowns::SameHeights() const
{
	// Height() reads the subdivided elevation (from Elev), Slope, the sea level, the lakes and the rivers: equal inputs,
	// equal ground. Read-only on the game thread (the planet's snapshot is only replaced in the planet's own tick).
	const FDmPlanetTerrain* Mine = Ground.IsValid() ? Ground->PlanetTerrain() : nullptr;
	const ADmPlanet* Pl = Mine ? ADmPlanet::Find(this) : nullptr;
	const FDmPlanetTerrain* Src = Pl ? Pl->Terrain() : nullptr;
	if (!Mine || !Src) return false;
	auto Eq = [](const TArray<float>& A, const TArray<float>& B) { return A.Num() == B.Num() && FMemory::Memcmp(A.GetData(), B.GetData(), A.Num() * sizeof(float)) == 0; };
	return Mine->SeaLevel == Src->SeaLevel && Mine->RiverHash == Src->RiverHash && Eq(Mine->Elev, Src->Elev) && Eq(Mine->Slope, Src->Slope)
		&& Eq(Mine->LakeMask, Src->LakeMask) && Eq(Mine->LakeNearM, Src->LakeNearM);
}

bool ADmTowns::LaunchGroundJob(const dm::World& W, bool bInitial, const TCHAR* Why)
{
	// (towns-perf) the planet's terrain was COPIED on the game thread (9-15 ms, up to 34 ms on a busy machine, every
	// planet re-version: ~2 s in a live world). Instead the towns capture their own terrain from the world exactly as the
	// planet does (FDmPlanetTerrain::Capture: plain field copies, ~1 ms) and prepare it on a worker (Prepare, ~130 ms
	// there): the same heights, rivers and lakes the planet draws, nothing shared with the planet, nothing to join.
	if (FDmTownsGround::HasProvider()) return false;
	if (!ADmPlanet::Find(this)) return false;
	bool bPend = false;
	const FDmPlanetTerrain* Src = nullptr;
	TSharedPtr<FDmTownsGround, ESPMode::ThreadSafe> G = FDmTownsGround::SnapshotDeferred(GetWorld(), W, bPend, &Src);
	if (bPend) return bInitial && NowS() - FirstTick < 15.0;   // the planet has no terrain yet: try again next frame
	if (!Src || !Src->Loc.IsValid()) return false;             // not the planet terrain after all (synchronous path)
	const double T0 = NowS();
	TSharedPtr<FDmPlanetTerrain, ESPMode::ThreadSafe> NewT = MakeShared<FDmPlanetTerrain, ESPMode::ThreadSafe>();
	NewT->Capture(W, Src->Loc);
	NewT->Version = Src->Version;
	TSharedPtr<FGroundJob, ESPMode::ThreadSafe> J = MakeShared<FGroundJob, ESPMode::ThreadSafe>();
	J->G = G;
	J->Old = Ground;
	J->Sig = G->Sig;
	J->Why = Why;
	J->bInitial = bInitial;
	J->CaptureMs = (NowS() - T0) * 1000.0;
	if (!bInitial && Ground.IsValid())
		for (const TPair<int32, FTown>& T : Towns)
			if (T.Value.Plan.IsValid())
				J->Probes.Add({ T.Key, T.Value.Plan, T.Value.Plan->Centre, T.Value.Plan->RadiusM, T.Value.Plan->ExtentM });
	FGroundJob* Jp = J.Get();
	// the initial ground gates the first wave; later ones only refresh it. (wave 4 towns) Never above background-normal: the
	// sky's weather builds and the planet's patch jobs run there (GThreadPool's Normal maps to it)
	const UE::Tasks::ETaskPriority GroundPri = bInitial ? UE::Tasks::ETaskPriority::BackgroundNormal : UE::Tasks::ETaskPriority::BackgroundLow;
	J->Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Jp, NewT]()
	{
		const double T1 = FPlatformTime::Seconds();
		NewT->Prepare();
		Jp->G->AdoptPlanetTerrain(NewT);
		Jp->G->BuildMs = (FPlatformTime::Seconds() - T1) * 1000.0;
	}, GroundPri);
	J->Probe = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Jp]()
	{
		if (Jp->Old.IsValid() && Jp->Old->GetSource() == Jp->G->GetSource())
		{
			const double T1 = FPlatformTime::Seconds();
			for (const FGroundProbe& P : Jp->Probes)
				if (GroundMovedAt(*Jp->Old, *Jp->G, P.Centre, P.RadiusM, P.ExtentM)) Jp->Moved.Add(P.Id);
			Jp->ProbeMs = (FPlatformTime::Seconds() - T1) * 1000.0;
		}
	}, UE::Tasks::Prerequisites(J->Task), GroundPri);
	GroundJob = J;
	return true;
}

template <typename A, typename B>
void ADmTowns::ReleaseOnWorker(A&& X, B&& Y)
{
	UE::Tasks::Launch(UE_SOURCE_LOCATION, [X = MoveTemp(X), Y = MoveTemp(Y)]() mutable { X.Reset(); Y.Reset(); }, UE::Tasks::ETaskPriority::BackgroundLow);
}

void ADmTowns::FinishGroundJob()
{
	TSharedPtr<FGroundJob, ESPMode::ThreadSafe> J = MoveTemp(GroundJob);
	GroundJob.Reset();
	if (!J.IsValid()) return;
	J->Task.Wait();
	J->Probe.Wait();
	const double Now = NowS();
	if (J->bInitial || !Ground.IsValid())
	{
		Ground = J->G;
		GroundSig = J->Sig;
		LastGroundCheck = Now;
		UE_LOG(LogDmTowns, Display, TEXT("DmTowns: ground from the %s (captured in %.1f ms, prepared on a worker in %.0f ms; %d river points, %d river grid cells)"), Ground->SourceName(), J->CaptureMs, Ground->BuildMs,
			Ground->PlanetTerrain() ? Ground->PlanetTerrain()->RPts.Num() : -1, Ground->PlanetTerrain() ? Ground->PlanetTerrain()->RGrid.Num() : -1);
		return;
	}
	const double T0 = NowS();
	FDmTownsGroundPtr Old = Ground;
	const bool bSameSource = Old.IsValid() && Old->GetSource() == J->G->GetSource();
	int32 Moved = 0, Late = 0;
	if (!bSameSource)
	{
		for (TPair<int32, FTown>& T : Towns) { T.Value.bDirty = true; ++T.Value.GroundGen; ++Moved; }
	}
	else
	{
		TSet<int32> MovedSet(J->Moved);
		TMap<int32, const FDmTownPlan*> Probed;
		for (const FGroundProbe& P : J->Probes) Probed.Add(P.Id, P.Plan.Get());
		TSharedPtr<FLateProbe, ESPMode::ThreadSafe> LP;
		for (TPair<int32, FTown>& T : Towns)
		{
			FTown& Tw = T.Value;
			if (!Tw.Plan.IsValid()) continue;   // waiting for a plan (planned against the newest ground when its task starts)
			const FDmTownPlan* const* Pp = Probed.Find(T.Key);
			if (!Pp || *Pp != Tw.Plan.Get())
			{
				// (towns-perf) a plan applied after the job started was not probed: probe it on a worker too (a ground job can
				// take seconds on a busy machine, and probing a dozen re-planned settlements here cost 1-5 ms)
				if (!LP.IsValid()) { LP = MakeShared<FLateProbe, ESPMode::ThreadSafe>(); LP->Old = Old; LP->G = J->G; }
				LP->Probes.Add({ T.Key, Tw.Plan, Tw.Plan->Centre, Tw.Plan->RadiusM, Tw.Plan->ExtentM });
				++Late;
				continue;
			}
			if (MovedSet.Contains(T.Key)) { MarkGround(Tw); ++Moved; }
		}
		if (LP.IsValid())
		{
			FLateProbe* Lp = LP.Get();
			LP->Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Lp]()
			{
				for (const FGroundProbe& P : Lp->Probes)
					if (GroundMovedAt(*Lp->Old, *Lp->G, P.Centre, P.RadiusM, P.ExtentM)) Lp->Moved.Add(P.Id);
			}, UE::Tasks::ETaskPriority::BackgroundLow);
			LateProbes.Add(LP);
		}
	}
	Ground = J->G;
	GroundSig = J->Sig;
	UE_LOG(LogDmTowns, Display, TEXT("DmTowns: %s (%s, captured in %.1f ms, prepared and probed on workers in %.0f + %.1f ms): ground moved under %d of %d settlements, re-planning those, %d probed late (adopt %.2f ms; %d planet re-versions without height changes skipped so far)"),
		J->Why, Ground->SourceName(), J->CaptureMs, Ground->BuildMs, J->ProbeMs, Moved, Towns.Num(), Late, (NowS() - T0) * 1000.0, GroundSkips);
	// (towns-perf) the old ground (a whole planet terrain: thousands of arrays) and the job are freed on a worker
	ReleaseOnWorker(MoveTemp(Old), MoveTemp(J));
}

void ADmTowns::FinishLateProbes()
{
	for (int32 K = LateProbes.Num() - 1; K >= 0; --K)
	{
		if (!LateProbes[K]->Task.IsCompleted()) continue;
		TSharedPtr<FLateProbe, ESPMode::ThreadSafe> LP = LateProbes[K];
		LateProbes.RemoveAtSwap(K);
		TMap<int32, const FDmTownPlan*> PlanOf;
		for (const FGroundProbe& P : LP->Probes) PlanOf.Add(P.Id, P.Plan.Get());
		for (int32 Id : LP->Moved)
			if (FTown* Tw = Towns.Find(Id))
				if (Tw->Plan.Get() == PlanOf.FindRef(Id)) MarkGround(*Tw);   // still the plan probed
		ReleaseOnWorker(FDmTownsGroundPtr(), MoveTemp(LP));
	}
}


void ADmTowns::AdoptGround(const FDmTownsGroundPtr& G, uint64 NewSig, const TCHAR* Why)
{
	if (!G.IsValid()) return;
	// (verifier fix) the planet re-captures its terrain for any edit anywhere (and once right after startup
	// with unchanged data); re-planning all ~100 settlements each time cost ~20 s of worker time and a wave of
	// component rebuilds. Probe the old and new ground under each settlement and re-plan only where it moved.
	// (towns-perf) with the planet terrain this runs on a worker (LaunchGroundJob); this is the synchronous path.
	const double T0 = NowS();
	int32 Moved = 0;
	const FDmTownsGroundPtr Old = Ground;
	for (TPair<int32, FTown>& T : Towns)
	{
		FTown& Tw = T.Value;
		// (integration) a town without a plan yet is already waiting for one (planned against the newest ground when
		// its task starts); marking it and bumping its generation threw away every in-flight plan on each re-capture
		if (!Tw.Plan.IsValid() && Old.IsValid() && Old->GetSource() == G->GetSource()) continue;
		const bool bMoved = !Old.IsValid() || !Tw.Plan.IsValid() || Old->GetSource() != G->GetSource()
			|| GroundMovedAt(*Old, *G, Tw.Plan->Centre, Tw.Plan->RadiusM, Tw.Plan->ExtentM);
		if (bMoved) { if (Tw.Plan.IsValid() && Old.IsValid() && Old->GetSource() == G->GetSource()) MarkGround(Tw); else { Tw.bDirty = true; Tw.bGroundOnly = false; ++Tw.GroundGen; } ++Moved; }
	}
	Ground = G;
	GroundSig = NewSig;
	UE_LOG(LogDmTowns, Display, TEXT("DmTowns: %s (%s, %d triangles, %.0f ms): ground moved under %d of %d settlements, re-planning those (probe %.1f ms)"),
		Why, Ground->SourceName(), Ground->Triangles, Ground->BuildMs, Moved, Towns.Num(), (NowS() - T0) * 1000.0);
}

void ADmTowns::EnsureSnap(const dm::World& W, bool bForce)
{
	const double Now = NowS();
	if (Snap.IsValid() && !bForce && !(bSnapDirty && Now - SnapTime > 4.0)) return;
	TSharedPtr<FDmTownsWorldSnap, ESPMode::ThreadSafe> S = MakeShared<FDmTownsWorldSnap, ESPMode::ThreadSafe>();
	S->Geo = W.geo;
	S->N = W.n;
	auto CopyF = [&W](const std::vector<float>& V, TArray<float>& O) { O.SetNumUninitialized(int32(V.size())); FMemory::Memcpy(O.GetData(), V.data(), V.size() * sizeof(float)); };
	CopyF(W.water, S->Water); CopyF(W.farm, S->Farm); CopyF(W.urban, S->Urban); CopyF(W.temp, S->Temp); CopyF(W.moist, S->Moist); CopyF(W.slope, S->Slope); CopyF(W.elev, S->Elev);
	S->Biome.SetNumUninitialized(W.n); FMemory::Memcpy(S->Biome.GetData(), W.biome.data(), W.biome.size());
	S->Owner.SetNumUninitialized(W.n); FMemory::Memcpy(S->Owner.GetData(), W.owner.data(), W.owner.size() * sizeof(int16));
	S->SeaLevel = W.seaLevel;
	S->Year = W.year;
	Snap = S;
	SnapTime = Now;
	bSnapDirty = false;
}

// ------------------------------------------------------------------ signatures and inputs

bool ADmTowns::MakeInput(const dm::World& W, int32 Sid, FDmTownInput& In) const
{
	if (Sid < 0 || Sid >= int32(W.settlements.size())) return false;
	const dm::Settlement& S = W.settlements[size_t(Sid)];
	if (!W.valid(S.cell)) return false;
	In.Id = Sid;
	In.Cell = S.cell;
	In.Dir = DmCoords::CellDir(W, S.cell);
	In.Tier = S.tier;
	In.Pop = S.pop;
	In.Culture = S.culture;
	const dm::Culture* Cu = dm::state::culture(W, S.culture);
	In.Age = Cu ? Cu->age : W.age;
	if (Cu) In.Color = FLinearColor(float(Cu->color[0]), float(Cu->color[1]), float(Cu->color[2]));
	for (int32 K = 0; K < dm::Building::COUNT && K < 16; ++K) In.B[K] = S.b[size_t(K)];
	In.bCapital = S.capital;
	In.Ark = S.ark;
	In.bDead = S.dead;
	In.Ruin = S.dead ? FMath::Clamp(W.urban[size_t(S.cell)] * 2.f, 0.f, 1.f) : 0.f;
	In.Plague = float(S.plague);
	In.Scale = Scale;
	In.CultureStyle = CultureStyleOf(W, S.culture);   // (wave 4 towns) the culture's building style (material instances)
	In.bFieldOverlay = FieldMode > 0 || (FieldMode < 0 && !(Ground.IsValid() && Ground->GetSource() == FDmTownsGround::ESource::PlanetTerrain));
	if (AgeOverride >= 0) In.Age = AgeOverride;
	if (const FOverride* O = Overrides.Find(Sid))
	{
		if (O->Tier >= 0) In.Tier = O->Tier;
		if (O->Pop >= 0) In.Pop = O->Pop;
		if (O->Age >= 0) In.Age = O->Age;
	}
	// neighbours within ~14 km (0.14 rad), nearest first; a road when both centres carry one. (towns-perf) the settlement
	// directions are cached once per frame: CheckSignatures / LaunchPlans call this for every settlement (O(n^2))
	if (DirCacheFrame != GFrameCounter || DirCache.Num() != int32(W.settlements.size()))
	{
		DirCacheFrame = GFrameCounter;
		DirCache.SetNumUninitialized(int32(W.settlements.size()));
		for (int32 K = 0; K < DirCache.Num(); ++K)
		{
			const dm::Settlement& O = W.settlements[size_t(K)];
			DirCache[K] = (!O.dead && W.valid(O.cell)) ? DmCoords::CellDir(W, O.cell) : FVector::ZeroVector;
		}
	}
	static const double CosLim = FMath::Cos(0.14);
	struct FN { double Dot; FVector Dir; uint8 Road; };
	TArray<FN, TInlineAllocator<16>> Ns;
	const FVector& Me = DirCache[Sid];
	for (int32 K = 0; K < DirCache.Num(); ++K)
	{
		if (K == Sid || DirCache[K].IsZero()) continue;
		const double Dt = FVector::DotProduct(Me, DirCache[K]);
		if (Dt < CosLim) continue;
		const dm::Settlement& O = W.settlements[size_t(K)];
		if (O.id == S.id) continue;
		Ns.Add({ Dt, DirCache[K], uint8(W.road[size_t(S.cell)] > 0 && W.road[size_t(O.cell)] > 0 ? 1 : 0) });
	}
	Ns.Sort([](const FN& A, const FN& B) { return A.Dot > B.Dot; });
	for (const FN& N : Ns) { In.Nbrs.Add(N.Dir); In.NbrRoad.Add(N.Road); }
	return true;
}

uint32 ADmTowns::SignatureOf(const dm::World& W, int32 Sid, uint32* OutStruct) const
{
	FDmTownInput In;
	if (!MakeInput(W, Sid, In)) return 0;
	uint32 H = Mix(uint32(In.Tier + 1), uint32(In.Age + 1));
	H = Mix(H, uint32(In.Culture + 3));
	for (int32 K = 0; K < dm::Building::COUNT; ++K) H = Mix(H, In.B[K] ? uint32(K + 11) : 0u);
	H = Mix(H, In.bDead ? 7u : 3u);
	H = Mix(H, In.bCapital ? 5u : 2u);
	H = Mix(H, uint32(FMath::RoundToInt(In.Scale * 100.f)));
	H = Mix(H, uint32(In.CultureStyle) + 17u);
	if (OutStruct) *OutStruct = H | 1u;
	H = Mix(H, uint32(FMath::FloorToInt(FMath::Loge(FMath::Max(In.Pop, 1.0)) * 5.0)));
	H = Mix(H, uint32(FMath::FloorToInt(In.Ruin * 6.f)));
	H = Mix(H, uint32(FMath::FloorToInt(FMath::Clamp(In.Ark, 0.0, 1.0) * 8.0)));
	H = Mix(H, uint32(FMath::RoundToInt(In.Scale * 100.f)));
	H = Mix(H, uint32(In.Nbrs.Num()));
	return H | 1u;
}

void ADmTowns::CheckSignatures(const dm::World& W, bool bAll)
{
	TSet<int32> Alive;
	for (int32 Sid = 0; Sid < int32(W.settlements.size()); ++Sid)
	{
		const dm::Settlement& S = W.settlements[size_t(Sid)];
		const bool bShow = !S.dead || (W.valid(S.cell) && W.urban[size_t(S.cell)] > 0.02f);
		if (!bShow) continue;
		Alive.Add(Sid);
		FTown* T = Towns.Find(Sid);
		if (!T) { T = &Towns.Add(Sid); T->Id = Sid; T->bDirty = true; T->FirstSeen = NowS(); continue; }
		if (!bAll && !EventTouched.Contains(Sid)) continue;
		uint32 Struct = 0;
		const uint32 Sig = SignatureOf(W, Sid, &Struct);
		// (towns-perf verifier) when the drawn plan's structure first fell behind the sim's (coverage check)
		if (T->Plan.IsValid() && Struct != T->PlanStruct) { if (T->StructSeen <= 0) T->StructSeen = NowS(); }
		else T->StructSeen = 0;
		if (Sig == T->Sig || Sig == T->PendingSig) continue;
		// growth alone (population step, neighbours, ruins fading) waits for GrowthEvery; structure changes at once
		if (Struct == T->Struct && NowS() - T->LastPlan < GrowthEvery) continue;
		T->bDirty = true;
		T->bGroundOnly = false;
	}
	TArray<int32> Gone;
	for (TPair<int32, FTown>& T : Towns) if (!Alive.Contains(T.Key)) Gone.Add(T.Key);
	for (int32 G : Gone) { DestroyComponents(Towns[G]); Towns.Remove(G); }
	EventTouched.Reset();
	bCheckAll = false;
}

// ------------------------------------------------------------------ planning

void ADmTowns::LaunchPlans(const dm::World& W)
{
	if (!Ground.IsValid() || !Snap.IsValid()) return;
	// nearest dirty settlements first
	FVector Cam = FVector::ZeroVector;
	const bool bCam = CameraPlanet(Cam);
	const double ViewUU = double(HideKm) * DmCoords::UUPerKm;
	const double Now = NowS();
	// (wave 4 towns) a settlement that came into view while its job runs in an out-of-view slot is promoted in place: its next
	// slices run at the in-view priority (the old way orphaned the task and planned it again from scratch)
	int32 NearNow = 0, FarNow = 0;
	for (const TPair<int32, FPending>& Pp : Pending) { NearNow += Pp.Value.bNear ? 1 : 0; FarNow += Pp.Value.bNear ? 0 : 1; }
	if (bCam && NearNow < MaxNearTasks)
		for (TPair<int32, FPending>& Pp : Pending)
		{
			if (NearNow >= MaxNearTasks) break;
			if (Pp.Value.bNear || !Pp.Value.Job.IsValid() || Pp.Value.Job->bDone.load(std::memory_order_acquire) || Pp.Key >= int32(W.settlements.size())) continue;
			if (FVector::Dist(Cam, DmCoords::CellSurfacePos(W, W.settlements[size_t(Pp.Key)].cell)) >= ViewUU) continue;
			Pp.Value.Job->Pri.store(int32(UE::Tasks::ETaskPriority::BackgroundNormal), std::memory_order_relaxed);
			Pp.Value.bNear = true;
			++NearNow; --FarNow;
		}
	TArray<TPair<double, int32>> Order;
	for (TPair<int32, FTown>& T : Towns)
	{
		if (!T.Value.bDirty || Pending.Contains(T.Key)) continue;
		// (towns-perf verifier) a settlement whose only reason is the ground moving again (a rising sea steps every ~1 s at 8x
		// in the late ages: 40-94 coastal settlements re-planned per step, ~1 s of worker time each) is re-planned at most every
		// GroundReplanEvery seconds; structure changes are not delayed
		if (T.Value.bGroundOnly && T.Value.Plan.IsValid() && Now - T.Value.LastPlan < GroundReplanEvery) continue;
		const dm::Settlement& S = W.settlements[size_t(T.Key)];
		Order.Add({ bCam ? FVector::Dist(Cam, DmCoords::CellSurfacePos(W, S.cell)) : 0.0, T.Key });
	}
	Order.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
	int32 Launched = 0;
	// (wave 4 towns) no extra jobs for the first wave: 12 + 3 plans at once held every background worker for seconds and the
	// sky's first weather builds waited behind them (sky-check failed with towns on); the slices keep the in-view ones quick
	const int32 FarMax = MaxTasks;
	for (const TPair<double, int32>& O : Order)
	{
		// (towns-perf) more tasks while the first wave fills the world. Background workers run at a below-normal OS priority:
		// on a busy machine they starved (the first wave of a city view had planned 14 of 96 settlements after 30 s), so
		// up to MaxNearTasks settlements that can be seen (within HideKm of the camera, nearest first) are planned on
		// foreground workers; the game thread never waits for any of them
		// (towns-perf verifier) in-view settlements have their own MaxNearTasks slots: with one shared limit, six slow
		// background plans (out of view) blocked every launch, and a metropolis in view kept an out-of-date plan for 90 s
		if (Launched >= MaxLaunchesPerFrame) break;
		const bool bNear = O.Key < ViewUU && NearNow < MaxNearTasks;
		if (!bNear && FarNow >= FarMax) { if (NearNow >= MaxNearTasks) break; continue; }
		++Launched;
		FTown& T = Towns[O.Value];
		FDmTownInput In;
		if (!MakeInput(W, O.Value, In)) { T.bDirty = false; continue; }
		uint32 St = 0;
		const uint32 Sig = SignatureOf(W, O.Value, &St);
		T.bDirty = false;
		T.bGroundOnly = false;
		T.PendingSig = Sig;
		TSharedPtr<const FDmTownsWorldSnap, ESPMode::ThreadSafe> SnapP = Snap;
		FDmTownsGroundPtr GroundP = Ground;
		TSharedPtr<FDmKitTable, ESPMode::ThreadSafe> KitP = KitTable;
		// (towns-perf) the plan it replaces: the construction diff runs on the worker (immutable once applied)
		TSharedPtr<const FDmTownPlan, ESPMode::ThreadSafe> OldP = T.Plan;
		FPending P;
		P.Sig = Sig;
		P.GroundGen = T.GroundGen;
		P.Struct = St;
		P.OldPlan = T.Plan.Get();
		// (wave 4 towns) a sliced job at background priority: in view background-normal, level with the sky's weather builds and the
		// planet's patches (GThreadPool's Normal) and never ahead of them for more than one slice; out of view background-low.
		// (All at background-low starved even the in-view plans behind the editor's distance-field builds: no town in 360 frames.)
		TSharedPtr<FPlanJob, ESPMode::ThreadSafe> J = MakeShared<FPlanJob, ESPMode::ThreadSafe>();
		J->In = MoveTemp(In);
		J->Snap = SnapP; J->Ground = GroundP; J->Kit = KitP; J->OldPlan = OldP;
		J->Sig = Sig;
		J->SliceMs = SliceMs;
		const UE::Tasks::ETaskPriority Pri = bNear ? UE::Tasks::ETaskPriority::BackgroundNormal : UE::Tasks::ETaskPriority::BackgroundLow;
		J->Pri.store(int32(Pri));
		UE::Tasks::Launch(UE_SOURCE_LOCATION, [J]() { RunPlanSlice(J); }, Pri);
		P.Job = J;
		P.bNear = bNear;
		P.Launched = Now;
		(bNear ? NearNow : FarNow) += 1;
		Pending.Add(O.Value, MoveTemp(P));
	}
}

void ADmTowns::ApplyDone(double TickStart)
{
	// (towns-perf) finished plans are applied under the frame budget of the whole tick, nearest first; plans
	// arrive prepared (batches, ground render data, construction diff), so an apply is component updates only
	FVector Cam = FVector::ZeroVector;
	const bool bCam = CameraPlanet(Cam);
	TArray<TPair<double, int32>, TInlineAllocator<16>> Ready;
	for (TPair<int32, FPending>& P : Pending)
		if (P.Value.Job.IsValid() && P.Value.Job->bDone.load(std::memory_order_acquire))
		{
			const FDmTownPlan* Pl = P.Value.Job->Plan.Get();
			Ready.Add({ bCam && Pl ? FVector::DistSquared(Cam, Pl->Centre) : 0.0, P.Key });
		}
	if (Ready.Num() == 0) return;
	Ready.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
	int32 Applied = 0;
	for (const TPair<double, int32>& R : Ready)
	{
		// (towns-perf) the budget counts the whole tick (a ground capture or launches earlier this frame leave less for
		// applies); the first apply of a frame only starts while less than 3/4 of the budget is used
		const double Used = (NowS() - TickStart) * 1000.0;
		if (Applied >= MaxAppliesPerFrame || Used >= double(ApplyBudgetMs) * (Applied == 0 ? 0.75 : 1.0)) break;
		FPending& Pd = Pending[R.Value];
		TSharedPtr<FDmTownPlan, ESPMode::ThreadSafe> Plan = Pd.Job->Plan;
		if (!Plan.IsValid()) { if (FTown* Tc = Towns.Find(R.Value)) Tc->bDirty = true; Pending.Remove(R.Value); continue; }
		++NumPlans; PlanMs += Plan->Ms;
		// (wave 4 towns) slice statistics: the longest slice is what another task can wait behind on a worker
		++JobsDone; SlicesDone += Plan->Slices; MaxSliceMs = FMath::Max(MaxSliceMs, Plan->MaxSliceMs); MaxSliceCpuMs = FMath::Max(MaxSliceCpuMs, Plan->MaxSliceCpuMs);
		JobWallMs += (Pd.Job->Finished - Pd.Job->Started) * 1000.0;
		FTown* T = Towns.Find(R.Value);
		if (!T || Pd.Sig != T->PendingSig) { if (T) { T->bDirty = true; T->bGroundOnly = false; } }
		else
		{
			// (towns-perf verifier) a plan whose ground moved again while it was planned is applied anyway (one ground snapshot
			// old, newer than what is drawn) and the settlement stays dirty for a ground re-plan: discarding it threw away
			// every coastal plan while the sea rose (a ground job every ~1.3 s at 8x after an age change; plans take 1-3 s)
			if (Pd.GroundGen != T->GroundGen && !T->bDirty) { T->bDirty = true; T->bGroundOnly = true; }
			const bool bConstruct = T->Plan.IsValid() && Pd.OldPlan == T->Plan.Get();
			ApplyPlan(*T, Plan, bConstruct);
			T->PlanStruct = Pd.Struct;
			if (T->Struct == Pd.Struct) T->StructSeen = 0;
			else if (T->StructSeen <= 0) T->StructSeen = NowS();
			++Applied;
		}
		Pending.Remove(R.Value);
	}
}

void ADmTowns::RebuildAll(bool bBlocking, bool bNearOnly)
{
	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const dm::World* W = Sim ? Sim->Sim() : nullptr;
	if (!W) return;
	if (!bBlocking || !Ground.IsValid())
	{
		for (TPair<int32, FTown>& T : Towns) T.Value.bDirty = true;
		return;
	}
	// (wave 4 towns) running jobs are abandoned (re-planned below, blocking)
	for (TPair<int32, FPending>& P : Pending) if (P.Value.Job.IsValid()) P.Value.Job->Cancel.store(true);
	Pending.Reset();
	EnsureSnap(*W, false);
	CheckSignatures(*W, true);
	TArray<int32> Ids;
	TArray<FDmTownInput> Ins;
	TArray<uint32> Sigs;
	// (verifier fix) the first wave of a world blocks only for the settlements the camera can see; the others are
	// planned on worker threads, nearest first (a whole Space-age world blocked the game thread for 1.8-3.1 s, and
	// from orbit nothing at all is drawn)
	FVector Cam = FVector::ZeroVector;
	const bool bNear = bNearOnly && CameraPlanet(Cam);
	int32 Deferred = 0;
	for (TPair<int32, FTown>& T : Towns)
	{
		if (bNear)
		{
			const FVector C = DmCoords::CellSurfacePos(*W, W->settlements[size_t(T.Key)].cell);
			const FVector D = Cam - C;
			const double Dist = D.Size(), Ext = 3.6 * DmCoords::UUPerKm;
			const bool bVis = FVector::DotProduct(D, C.GetSafeNormal()) > -0.05 * Dist - Ext * 0.05 && Dist - Ext < double(HideKm) * DmCoords::UUPerKm;
			if (!bVis) { T.Value.bDirty = true; ++Deferred; continue; }
		}
		FDmTownInput In;
		if (!MakeInput(*W, T.Key, In)) continue;
		uint32 St = 0;
		Ids.Add(T.Key); Sigs.Add(SignatureOf(*W, T.Key, &St)); Ins.Add(MoveTemp(In));
		T.Value.PlanStruct = St; T.Value.StructSeen = 0;
	}
	TArray<TSharedPtr<FDmTownPlan, ESPMode::ThreadSafe>> Plans;
	Plans.SetNum(Ids.Num());
	const double T0 = NowS();
	const FDmTownsWorldSnap& S = *Snap;
	const FDmTownsGround& G = *Ground;
	const FDmKitTable& K = *KitTable;
	ParallelFor(Ids.Num(), [&](int32 I)
	{
		Plans[I] = MakeShared<FDmTownPlan, ESPMode::ThreadSafe>();
		DmTownsPlan::PlanTown(Ins[I], S, G, K, *Plans[I]);
		Plans[I]->Sig = Sigs[I];
		PreparePlan(*Plans[I], nullptr, true);
	});
	const double T1 = NowS();
	for (int32 I = 0; I < Ids.Num(); ++I)
	{
		FTown& T = Towns[Ids[I]];
		T.PendingSig = Sigs[I];
		T.bDirty = false;
		++NumPlans; PlanMs += Plans[I]->Ms;
		ApplyPlan(T, Plans[I], false);
	}
	const double T2 = NowS();
	UE_LOG(LogDmTowns, Display, TEXT("DmTowns: planned %d settlements in %.0f ms (parallel; %.0f ms of plan time), built components in %.0f ms; %d out of view planned in the background; %s"),
		Ids.Num(), (T1 - T0) * 1000.0, PlanMs, (T2 - T1) * 1000.0, Deferred, *Describe());
}

void ADmTowns::ApplyPlan(FTown& T, const TSharedPtr<FDmTownPlan, ESPMode::ThreadSafe>& P, bool bConstruct)
{
	const double Now = NowS();
	if (!P->bPrepared) PreparePlan(*P, bConstruct ? T.Plan.Get() : nullptr, true);
	// new buildings stand as scaffolds (the diff against the old plan was made by the planning task)
	if (bConstruct && ConstructionSeconds > 0.f && P->NewKeys.Num())
	{
		for (uint32 K : P->NewKeys) T.Building.Add(K, Now);
		T.NextFinish = FMath::Min(T.NextFinish, Now + ConstructionSeconds);
	}
	if (T.Building.Num() > 0)
	{
		TSet<uint32> Keys;
		Keys.Reserve(P->Inst.Num());
		for (const FDmTownInst& I : P->Inst) Keys.Add(I.Key);
		for (auto It = T.Building.CreateIterator(); It; ++It) if (!Keys.Contains(It.Key())) It.RemoveCurrent();
	}
	TSharedPtr<FDmTownPlan, ESPMode::ThreadSafe> OldPlan = MoveTemp(T.Plan);
	T.Plan = P;
	T.Sig = P->Sig;
	T.LastPlan = Now;
	if (OldPlan.IsValid()) ReleaseOnWorker(MoveTemp(OldPlan), FDmTownsGroundPtr());   // (towns-perf) MBs of arrays
	if (UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
		if (const dm::World* W = Sim->Sim()) SignatureOf(*W, T.Id, &T.Struct);
	BuildComponents(T);
	if (bLog) UE_LOG(LogDmTowns, Display, TEXT("DmTowns: plan %d (%s): %d instances, %d houses, %d civic, %d fields, %d streets, %d piers, %d ships, %d wall pieces, %d under construction, %.1f ms (+%.1f ms prepare), apply %.2f ms"),
		T.Id, *P->Note, P->Inst.Num(), P->Houses, P->Civic, P->Fields, P->Streets, P->Piers, P->Ships, P->WallPieces, T.Building.Num(), P->Ms, P->PrepMs, (NowS() - Now) * 1000.0);
}

UDmTownsISM* ADmTowns::NewPooled(UStaticMesh* Mesh)
{
	// plain ISM: a HISM loses its per-instance custom data when its async tree build re-sorts the instances (UE 5.7,
	// measured with towns-cdtest); settlements are compact, so per-component LOD is fine
	UDmTownsISM* C = NewObject<UDmTownsISM>(this);
	C->SetMobility(EComponentMobility::Movable);
	C->SetupAttachment(Root);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCanEverAffectNavigation(false);
	C->SetGenerateOverlapEvents(false);
	C->SetCastShadow(true);
	C->bAffectDistanceFieldLighting = false;
	C->bReceivesDecals = false;
	C->ShadowCacheInvalidationBehavior = EShadowCacheInvalidationBehavior::Rigid;
	C->SetNumCustomDataFloats(UDmTownsFactory::NumCustomData);
	if (Mesh) C->SetStaticMesh(Mesh);
	C->SetVisibility(false);
	C->RegisterComponent();
	FreezeComp(C, true);
	Keep.Add(C);
	return C;
}

UDmTownsISM* ADmTowns::TakePooled(UStaticMesh* Mesh)
{
	// one that already shows this mesh (no mesh change on a registered component), else any
	for (int32 K = Pool.Num() - 1; K >= 0; --K)
		if (IsValid(Pool[K]) && Pool[K]->GetStaticMesh() == Mesh) { UDmTownsISM* C = Pool[K]; Pool.RemoveAtSwap(K, EAllowShrinking::No); return C; }
	while (Pool.Num() > 0)
	{
		UDmTownsISM* C = Pool.Pop(EAllowShrinking::No);
		if (IsValid(C)) return C;
	}
	return NewPooled();
}

void ADmTowns::ReturnPooled(UInstancedStaticMeshComponent* C)
{
	UDmTownsISM* D = Cast<UDmTownsISM>(C);
	if (!D || Pool.Num() >= PoolMax) { Keep.Remove(C); if (C) C->DestroyComponent(); return; }
	D->SetVisibility(false);
	FreezeComp(D, true);
	D->SetInstances(TArray<FTransform>(), TArray<float>(), true);
	Pool.Add(D);
}

void ADmTowns::RefillPool()
{
	// a few components per frame while nothing else is applied, so first waves and re-plans find them ready
	const double T0 = NowS();
	int32 Made = 0;
	while (Pool.Num() < PoolTarget && (NowS() - T0) * 1000.0 < PoolBudgetMs) { Pool.Add(NewPooled()); ++Made; }
	FDmTownsPerf::Get().NoteComponents(Made, 0);
}

void ADmTowns::DestroyComponents(FTown& T, bool bInstancesOnly)
{
	int32 N = 0;
	for (UInstancedStaticMeshComponent* C : T.Comps) if (C) { ReturnPooled(C); ++N; }
	T.Comps.Reset();
	T.CompKeys.Reset();
	FDmTownsPerf::Get().NoteComponents(0, N);
	if (bInstancesOnly) return;
	if (T.GroundComp) { Keep.Remove(T.GroundComp); T.GroundComp->DestroyComponent(); T.GroundComp = nullptr; }
	if (T.GroundMesh) { Keep.Remove(T.GroundMesh); T.GroundMesh = nullptr; }
}

void ADmTowns::BuildComponents(FTown& T, bool bInstancesOnly)
{
	if (!T.Plan.IsValid() || !Factory) { DestroyComponents(T); return; }
	FDmTownPlan& P = *T.Plan;
	if (!P.bPrepared) PreparePlan(P, nullptr, !bInstancesOnly);
	const double Now = NowS();
	// (towns-perf) the batches come prepared from the planning task; only buildings under construction are moved into a
	// scaffold batch here (a few new buildings: the copy is cheap)
	struct FWant { uint32 Key; const TArray<FTransform>* Xf; const TArray<float>* Cd; };
	TArray<FWant, TInlineAllocator<64>> Want;
	TArray<TArray<FTransform>> TmpX;
	TArray<TArray<float>> TmpC;
	bool bAnyCons = false;
	for (const TPair<uint32, double>& B : T.Building) if (Now - B.Value < ConstructionSeconds) { bAnyCons = true; break; }
	if (!bAnyCons)
	{
		for (const FDmTownBatch& B : P.Batches) Want.Add({ B.Key, &B.Xf, &B.Cd });
	}
	else
	{
		constexpr int32 Nf = UDmTownsFactory::NumCustomData;
		TmpX.Reserve(2 * P.Batches.Num() + 1); TmpC.Reserve(2 * P.Batches.Num() + 1);
		TArray<FTransform> ScX; TArray<float> ScC;
		for (const FDmTownBatch& B : P.Batches)
		{
			// (wave 4 towns) a real asset under construction is its own _Construction mesh on the same footprint
			const bool bAssetCons = ResolveLook(P, B.Key).bAssetCons;
			TArray<FTransform>& X = TmpX.AddDefaulted_GetRef();
			TArray<float>& C = TmpC.AddDefaulted_GetRef();
			TArray<FTransform>* AcX = nullptr; TArray<float>* AcC = nullptr;
			X.Reserve(B.Xf.Num()); C.Reserve(B.Cd.Num());
			for (int32 J = 0; J < B.Idx.Num(); ++J)
			{
				const FDmTownInst& In = P.Inst[B.Idx[J]];
				const double* St = T.Building.Find(In.Key);
				if (St && Now - *St < ConstructionSeconds && bAssetCons)
				{
					if (!AcX) { AcX = &TmpX.AddDefaulted_GetRef(); AcC = &TmpC.AddDefaulted_GetRef(); }
					AcX->Add(B.Xf[J]); AcC->Append(&B.Cd[J * Nf], Nf);
				}
				else if (St && Now - *St < ConstructionSeconds)
				{
					const float Prog = float(FMath::Clamp((Now - *St) / FMath::Max(ConstructionSeconds, 0.1f), 0.0, 1.0));
					const FVector3f S(FMath::Max(In.W, 3.f) / 10.f, FMath::Max(In.D, 3.f) / 8.f, FMath::Max(In.H, 3.f) / 6.f * (0.45f + 0.4f * Prog));
					ScX.Add(FTransform(FQuat(In.Rot), FVector(In.Pos), FVector(S)));
					ScC.Append(In.Cd, Nf);
				}
				else { X.Add(B.Xf[J]); C.Append(&B.Cd[J * Nf], Nf); }
			}
			Want.Add({ B.Key, &X, &C });
			if (AcX) Want.Add({ DmTownsBatchKey::Make(DmTownsBatchKey::Mesh(B.Key), DmTownsBatchKey::Var(B.Key), DmTownsBatchKey::Sub(B.Key), int32(EDmTownState::Construction)), AcX, AcC });
		}
		if (ScX.Num())
		{
			TmpX.Add(MoveTemp(ScX)); TmpC.Add(MoveTemp(ScC));
			Want.Add({ ScaffoldBit | (uint32(EDmTownMesh::Scaffold) << 8), &TmpX.Last(), &TmpC.Last() });
		}
	}
	// (towns-perf) components are kept per batch key and updated in place (skipped when unchanged): a re-plan no longer
	// destroys and recreates every component of the settlement (UObject churn, render-state rebuilds, GC)
	const double MsWant = (NowS() - Now) * 1000.0;
	TMap<uint32, int32> OldIdx;
	for (int32 I = 0; I < T.CompKeys.Num() && I < T.Comps.Num(); ++I) OldIdx.Add(T.CompKeys[I], I);
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> NewComps;
	TArray<uint32> NewKeys;
	int32 Registered = 0, Updated = 0, NewInst = 0, UpdInst = 0;
	double MsInst = 0, MsReg = 0;
	for (const FWant& Wn : Want)
	{
		if (Wn.Xf->Num() == 0) continue;
		// (wave 4 towns) the real asset of this batch at the plan's age, tier and culture (or the stand-in)
		const FLook Look = ResolveLook(P, Wn.Key);
		if (!Look.Mesh) continue;
		auto ApplyMats = [&Look](UDmTownsISM* Cm)
		{
			if (Cm->MatsKey == Look.Mats) return;
			Cm->EmptyOverrideMaterials();
			if (Look.Mats)
				for (int32 Mi = 0; Mi < Look.Mats->Num(); ++Mi)
					if (UMaterialInterface* Mt = (*Look.Mats)[Mi]) Cm->SetMaterial(Mi, Mt);
			Cm->MatsKey = Look.Mats;
		};
		UDmTownsISM* C = nullptr;
		if (const int32* Oi = OldIdx.Find(Wn.Key))
		{
			C = Cast<UDmTownsISM>(T.Comps[*Oi]);
			T.Comps[*Oi] = nullptr;
			OldIdx.Remove(Wn.Key);
		}
		if (C)
		{
			if (!C->GetRelativeLocation().Equals(P.Centre, 0.01))
			{
				if (T.bVisible) C->SetRelativeLocation(P.Centre);
				else C->SetRelativeLocation_Direct(P.Centre);   // frozen: applied by the unfreeze when the town is shown
			}
			// (wave 4 towns) the same batch key at a new age can be another asset
			if (C->GetStaticMesh() != Look.Mesh) C->SetStaticMesh(Look.Mesh);
			ApplyMats(C);
			const double Ti = NowS();
			if (C->SetInstances(*Wn.Xf, *Wn.Cd, true)) { ++Updated; UpdInst += Wn.Xf->Num(); }
			MsInst += (NowS() - Ti) * 1000.0;
		}
		else
		{
			// (towns-perf) from the pool of registered, hidden, frozen components (made in quiet frames): creating and
			// registering a component cost ~110 us on the game thread, 25-50 of them for a settlement's first apply
			const double Tr = NowS();
			C = TakePooled(Look.Mesh);
			C->SetRelativeLocation_Direct(P.Centre);
			if (C->GetStaticMesh() != Look.Mesh) C->SetStaticMesh(Look.Mesh);
			ApplyMats(C);
			const int32 Cull = int32(CullS(Look.Size) * 100.0);
			C->SetCullDistances(int32(Cull * 0.8), Cull);
			MsReg += (NowS() - Tr) * 1000.0;
			const double Ti = NowS();
			C->SetInstances(*Wn.Xf, *Wn.Cd, true);
			const double Tv = NowS();
			MsInst += (Tv - Ti) * 1000.0;
			NewInst += Wn.Xf->Num();
			if (T.bVisible) { FreezeComp(C, false); C->SetVisibility(true); }
			MsReg += (NowS() - Tv) * 1000.0;
			++Registered;
			if (bLog && NewComps.Num() == 0)
				UE_LOG(LogDmTowns, Display, TEXT("DmTowns: comp %s: %d instances, %d custom floats, data %d"), *Look.Mesh->GetName(), C->GetInstanceCount(), C->NumCustomDataFloats, C->PerInstanceSMCustomData.Num());
		}
		NewComps.Add(C);
		NewKeys.Add(Wn.Key);
	}
	int32 Destroyed = 0;
	for (UInstancedStaticMeshComponent* C : T.Comps) if (C) { ReturnPooled(C); ++Destroyed; }
	T.Comps = MoveTemp(NewComps);
	T.CompKeys = MoveTemp(NewKeys);
	FDmTownsPerf::Get().NoteComponents(Registered, Destroyed);
	const double MsComps = (NowS() - Now) * 1000.0;
	if (MsComps > 3.0 || bLog)
		UE_LOG(LogDmTowns, Display, TEXT("DmTowns: components of %d: %.2f ms (batches %.2f ms; %d new with %d instances, %d updated with %d instances: instances %.2f ms, take / show %.2f ms; %d returned)"),
			T.Id, MsComps, MsWant, Registered, NewInst, Updated, UpdInst, MsInst, MsReg, Destroyed);
	if (bInstancesOnly && T.GroundComp) return;   // (verifier fix) finishing scaffolds no longer rebuilds the overlay mesh
	// ground overlay: render data prepared by the planning task (consumed here); the component is reused
	const double Tg = NowS();
	UStaticMesh* NewMesh = nullptr;
	if (Factory->GroundMat())
	{
		const FString Name = FString::Printf(TEXT("SM_TownGround_%d"), T.Id);
		if (P.GroundData.IsValid() && P.GroundData->RD.IsValid()) NewMesh = DmTowns::MakeStaticMesh(this, Name, *P.GroundData, { Factory->GroundMat() });
		else if (P.Ground.NumTris() > 0) NewMesh = DmTowns::MakeStaticMesh(this, Name, { &P.Ground }, { Factory->GroundMat() }, { 1.f }, false);
		P.GroundData.Reset();
	}
	if (!NewMesh)
	{
		if (T.GroundComp) { Keep.Remove(T.GroundComp); T.GroundComp->DestroyComponent(); T.GroundComp = nullptr; }
		if (T.GroundMesh) { Keep.Remove(T.GroundMesh); T.GroundMesh = nullptr; }
		return;
	}
	if (T.GroundMesh) Keep.Remove(T.GroundMesh);
	T.GroundMesh = NewMesh;
	Keep.Add(NewMesh);
	if (T.GroundComp)
	{
		if (T.bGroundVis) T.GroundComp->SetRelativeLocation(P.Centre);
		else T.GroundComp->SetRelativeLocation_Direct(P.Centre);
		T.GroundComp->SetStaticMesh(NewMesh);
	}
	else
	{
		UStaticMeshComponent* G = NewObject<UStaticMeshComponent>(this);
		G->SetMobility(EComponentMobility::Movable);
		G->SetupAttachment(Root);
		G->SetRelativeLocation(P.Centre);
		G->SetStaticMesh(NewMesh);
		G->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		G->SetCanEverAffectNavigation(false);
		G->SetGenerateOverlapEvents(false);
		G->SetCastShadow(false);
		G->bAffectDistanceFieldLighting = false;
		G->bReceivesDecals = false;
		G->SetVisibility(T.bGroundVis);
		G->RegisterComponent();
		if (!T.bGroundVis) FreezeComp(G, true);
		T.GroundComp = G;
		Keep.Add(G);
	}
	FDmTownsPerf::Get().NoteGroundMesh((NowS() - Tg) * 1000.0);
}

void ADmTowns::UpdateVisibility()
{
	FVector Cam;
	if (!CameraPlanet(Cam)) return;
	const double Hide = double(HideKm) * DmCoords::UUPerKm;
	// (wave 4 towns) the buildings of small settlements are hidden nearer than HideKm: a village's 6-10 m houses are under a
	// pixel beyond ~18 km, but every drawn component follows the turning planet every frame (game-thread transform updates
	// and render-thread primitive updates for each). The ground overlay (fields: 100-300 m) stays out to HideKm. 5 % hysteresis.
	static const double kTierKm[5] = { 10.0, 18.0, 28.0, 45.0, 70.0 };
	int32 Vis = 0;
	VisibleComps = 0;
	for (TPair<int32, FTown>& T : Towns)
	{
		if (!T.Value.Plan.IsValid()) continue;
		const FVector C = T.Value.Plan->Centre;
		const FVector D = Cam - C;
		const double Dist = D.Size();
		const double Ext = double(T.Value.Plan->ExtentM) * 100.0;
		const bool bAbove = FVector::DotProduct(D, C.GetSafeNormal()) > -0.05 * Dist - Ext * 0.05;
		const double HideB = FMath::Min(Hide, kTierKm[FMath::Clamp(int32(T.Value.Plan->Tier), 0, 4)] * (TierHideScale > 0.f ? TierHideScale : 1000.0) * DmCoords::UUPerKm) * (T.Value.bVisible ? 1.05 : 1.0);
		const bool bVis = bAbove && Dist - Ext < HideB;
		const bool bGVis = bAbove && Dist - Ext < Hide;
		if (bVis) { ++Vis; VisibleComps += T.Value.Comps.Num(); }
		// unfreeze before showing (the proxy is created where the planet is now); freeze after hiding
		if (bVis != T.Value.bVisible)
		{
			T.Value.bVisible = bVis;
			for (UInstancedStaticMeshComponent* Cp : T.Value.Comps) if (Cp) { if (bVis) FreezeComp(Cp, false); Cp->SetVisibility(bVis); if (!bVis) FreezeComp(Cp, true); }
		}
		if (bGVis != T.Value.bGroundVis)
		{
			T.Value.bGroundVis = bGVis;
			if (T.Value.GroundComp) { if (bGVis) FreezeComp(T.Value.GroundComp, false); T.Value.GroundComp->SetVisibility(bGVis); if (!bGVis) FreezeComp(T.Value.GroundComp, true); }
		}
	}
	VisibleTowns = Vis;
}

void ADmTowns::UpdateConstruction(double BudgetMs)
{
	// (towns-perf) finished buildings replace their scaffolds (components are rebuilt only then): one settlement at a time
	// within what the frame's apply budget left (it cost 1.6-2.2 ms on top of an apply); the others wait for the next pass
	const double T0 = NowS();
	const double Now = T0;
	for (TPair<int32, FTown>& T : Towns)
	{
		if (T.Value.Building.Num() == 0 || Now < T.Value.NextFinish) continue;
		if ((NowS() - T0) * 1000.0 >= BudgetMs) break;
		bool bChanged = false;
		double Next = 1e30;
		for (auto It = T.Value.Building.CreateIterator(); It; ++It)
		{
			if (Now - It.Value() >= ConstructionSeconds) { It.RemoveCurrent(); bChanged = true; }
			else Next = FMath::Min(Next, It.Value() + ConstructionSeconds);
		}
		T.Value.NextFinish = Next;
		if (bChanged) BuildComponents(T.Value, true);
	}
}

// ------------------------------------------------------------------ tick

void ADmTowns::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double TickStart = NowS();
	// (verifier) phase times of this tick; a tick over 50 ms logs its breakdown so hitches can be attributed
	double Ph[8] = {};
	int32 PhN = 0;
	double PhT = TickStart;
	// (towns-perf verifier) with the monitor on, also the game thread's CPU time per phase (wall minus CPU = a stall)
	const bool bCpu = FDmTownsPerf::Get().IsEnabled();
	const double CpuStart = bCpu ? FDmTownsPerf::ThreadCpuMs() : 0.0;
	double PhC[8] = {};
	double PhCT = CpuStart;
	auto Mark = [&]()
	{
		const double N2 = NowS();
		if (PhN < 8)
		{
			if (bCpu) { const double C2 = FDmTownsPerf::ThreadCpuMs(); PhC[PhN] = C2 - PhCT; PhCT = C2; }
			Ph[PhN++] = (N2 - PhT) * 1000.0;
		}
		PhT = N2;
	};
	ON_SCOPE_EXIT
	{
		const double Ms = (NowS() - TickStart) * 1000.0;
		MaxTickMs = FMath::Max(MaxTickMs, Ms);
		FDmTownsPerf::Get().NoteTick(Ms, Ph, PhN);
		if (bCpu) FDmTownsPerf::Get().NoteTickCpu(FDmTownsPerf::ThreadCpuMs() - CpuStart, PhC, PhN);
		if (Ms > 20.0)
			UE_LOG(LogDmTowns, Display, TEXT("DmTowns: slow tick %.0f ms (follow planet %.1f, world/ground %.1f, snap %.1f, signatures+launch %.1f, apply %.1f, construction %.1f, visibility %.1f, units %.1f)"),
				Ms, Ph[0], Ph[1], Ph[2], Ph[3], Ph[4], Ph[5], Ph[6], Ph[7]);
	};
	FollowPlanet();
	Mark();
	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const dm::World* W = Sim ? Sim->Sim() : nullptr;
	if (!W || !W->geo) { if (Towns.Num()) ResetAll(); SeenWorld = nullptr; return; }
	if (bWorldPending || SeenWorld != W)
	{
		ResetAll();
		SeenWorld = W;
		bWorldPending = false;
		bFirstWave = true;
		bCheckAll = true;
	}
	if (!EnsureGround(*W)) return;
	Mark();
	EnsureSnap(*W, false);
	Mark();
	const double Now = NowS();
	FVector CamProbe;
	if (bFirstWave && !CameraPlanet(CamProbe) && ++FirstWaveWait < 8) return;   // the camera manager has no view in the first frames
	if (bFirstWave)
	{
		bFirstWave = false;
		FirstWaveWait = 0;
		if (bBlockFirstWave) RebuildAll(true, true);
		else
		{
			// (towns-perf) the first wave is planned on workers (nearest first) and applied under the budget like any re-plan
			bFirstWaveFill = true;
			FirstWaveStart = Now;
			FirstWavePlanMs = PlanMs;
			CheckSignatures(*W, true);
			LaunchPlans(*W);
			UE_LOG(LogDmTowns, Display, TEXT("DmTowns: first wave of %d settlements planned in the background (nearest first, %.1f ms apply budget per frame)"), Towns.Num(), ApplyBudgetMs);
		}
		LastCheck = Now;
		Mark();
	}
	else
	{
		if (bCheckAll || EventTouched.Num() > 0 || Now - LastCheck > CheckEvery * 4.0)
		{
			CheckSignatures(*W, true);
			LastCheck = Now;
		}
		LaunchPlans(*W);
		Mark();
		ApplyDone(TickStart);
	}
	Mark();
	if (bFirstWaveFill && (Pending.Num() == 0 || Now - FirstWaveStart > 30.0))
	{
		int32 Planned = 0;
		for (const TPair<int32, FTown>& T : Towns) Planned += T.Value.Plan.IsValid() ? 1 : 0;
		if (Planned == Towns.Num() || Now - FirstWaveStart > 30.0)
		{
			bFirstWaveFill = false;
			UE_LOG(LogDmTowns, Display, TEXT("DmTowns: first wave complete: %d of %d settlements in %.2f s (%.0f ms of plan time on workers; %d jobs in %d slices, longest slice %.1f ms CPU / %.1f ms wall, mean job %.0f ms wall); %s"),
				Planned, Towns.Num(), Now - FirstWaveStart, PlanMs - FirstWavePlanMs, JobsDone, SlicesDone, MaxSliceCpuMs, MaxSliceMs, JobsDone ? JobWallMs / JobsDone : 0.0, *Describe());
		}
	}
	{
		// (towns-perf) construction and the pool refill share what the apply left of the frame budget
		double Left = double(ApplyBudgetMs) - (NowS() - TickStart) * 1000.0;
		if (Now - LastConstruction > 2.0) Left = FMath::Max(Left, 0.5);   // never starved by a long run of applies
		if (Left > 0.3 && Now - LastConstruction > 0.2) { LastConstruction = Now; UpdateConstruction(Left); }
		if ((NowS() - TickStart) * 1000.0 < 1.0) RefillPool();
	}
	Mark();
	UpdateVisibility();
	Mark();
	if (bUnits) UpdateUnits(*W, DeltaSeconds);
	Mark();
}

// ------------------------------------------------------------------ units

ADmTowns::FLook ADmTowns::ResolveLook(const FDmTownPlan& P, uint32 Key) const
{
	FLook L;
	if (!Factory) return L;
	if (Key & ScaffoldBit)
	{
		const FDmKitInfo& S = Factory->Get(EDmTownMesh::Scaffold, 0);
		L.Mesh = S.Mesh; L.Size = FMath::Max3(S.W, S.D, S.H);
		return L;
	}
	const EDmTownMesh Id = DmTownsBatchKey::Mesh(Key);
	const int32 V = DmTownsBatchKey::Var(Key), Sub = DmTownsBatchKey::Sub(Key), St = DmTownsBatchKey::State(Key);
	if (const FDmTownAsset* A = Factory->GetAssets().Pick(Id, V, P.Age, P.Tier, Sub))
	{
		const int32 Su = (St >= 0 && St < int32(EDmTownState::Num) && A->Mesh[St]) ? St : 0;   // (verifier fix) the state drawn
		L.Mesh = A->Mesh[Su];
		const TArray<UMaterialInterface*>& M = A->Mats[Su][FMath::Clamp(int32(P.CultureStyle), 0, 3)][FMath::Clamp(int32(P.Climate), 0, 3)];
		for (UMaterialInterface* Mi : M) if (Mi) { L.Mats = &M; break; }
		L.Size = FMath::Max3(A->W, A->D, A->H);
		L.bAssetCons = A->HasState(EDmTownState::Construction);
		return L;
	}
	const FDmKitInfo& I = Factory->Get(Id, V);
	L.Mesh = I.Mesh; L.Size = FMath::Max3(I.W, I.D, I.H);
	return L;
}

UStaticMesh* ADmTowns::UnitMesh(EDmTownMesh M, int32 V, int32 Age) const
{
	if (!Factory) return nullptr;
	if (const FDmTownAsset* A = Factory->GetAssets().Pick(M, V, Age, 0, 0)) if (A->Mesh[0]) return A->Mesh[0];
	return Factory->Get(M, V).Mesh;
}

uint8 ADmTowns::CultureStyleOf(const dm::World& W, int32 CultureId) const
{
	// (wave 4 towns) ASSET-INTEGRATION 4.2, client-side and deterministic, from the culture's capital: Lagoon-Weaver for seafaring
	// peoples with a harbour capital, Ring-Builder in the cold or the high quarter of the land, Cliff-Terrace in dry or steep
	// country, else Valley. A daughter culture keeps its parent's style.
	if (const uint8* C = CultureStyles.Find(CultureId)) return *C;
	const dm::Culture* Cu = dm::state::culture(W, CultureId);
	uint8 Out = uint8(EDmCultureStyle::Valley);
	if (Cu && Cu->parent >= 0 && Cu->parent != CultureId && CultureStyles.Num() < 4096)
	{
		CultureStyles.Add(CultureId, Out);   // guards a cycle
		Out = CultureStyleOf(W, Cu->parent);
		CultureStyles.Add(CultureId, Out);
		return Out;
	}
	if (ElevQ < -0.5f)
	{
		TArray<float> E, S;
		for (int32 C = 0; C < W.n; ++C) if (!dm::state::isSea(W, C)) { E.Add(W.elev[size_t(C)]); S.Add(W.slope[size_t(C)]); }
		E.Sort(); S.Sort();
		ElevQ = E.Num() ? E[(E.Num() * 3) / 4] : 1e9f;
		SlopeQ = S.Num() ? S[(S.Num() * 4) / 5] : 1e9f;
	}
	if (Cu && Cu->capital >= 0 && Cu->capital < int32(W.settlements.size()))
	{
		const dm::Settlement& S = W.settlements[size_t(Cu->capital)];
		if (W.valid(S.cell))
		{
			const size_t C = size_t(S.cell);
			const float T = W.temp[C], Mo = W.moist[C];
			const bool bCold = T < 1.f, bArid = Mo < 0.3f && T > 14.f;
			if (Cu->traits.seafaring >= 0.62 && S.b[size_t(dm::Building::PORT)]) Out = uint8(EDmCultureStyle::LagoonWeaver);
			else if (bCold || W.elev[C] > ElevQ) Out = uint8(EDmCultureStyle::RingBuilder);
			else if (bArid || W.slope[C] > SlopeQ) Out = uint8(EDmCultureStyle::CliffTerrace);
		}
	}
	CultureStyles.Add(CultureId, Out);
	return Out;
}

int32 ADmTowns::UnitComp(UStaticMesh* Mesh)
{
	if (!Mesh) return -1;
	const int32 Idx = UnitCompMesh.IndexOfByKey(Mesh);
	if (Idx != INDEX_NONE) return Idx;
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
	C->SetMobility(EComponentMobility::Movable);
	C->SetupAttachment(Root);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCanEverAffectNavigation(false);
	C->SetCastShadow(true);
	C->bReceivesDecals = false;
	C->bAffectDistanceFieldLighting = false;
	C->SetNumCustomDataFloats(UDmTownsFactory::NumCustomData);
	C->RegisterComponent();
	UnitComps.Add(C);
	UnitCompMesh.Add(Mesh);
	UnitXf.AddDefaulted(); UnitCd.AddDefaulted();
	return UnitComps.Num() - 1;
}

void ADmTowns::WarmComponents()
{
	// (towns-perf) the first component of a kit mesh paid ~15 ms on the game thread (a new age's ship appearing at 8x:
	// first registration of that mesh, PSO precaching): every unit look and every kit mesh gets a registered component
	// while the world loads; the kit ones (hidden, frozen, empty) then serve as the component pool
	const double T0 = NowS();
	static const TPair<EDmTownMesh, int32> UnitLooks[] = {
		{ EDmTownMesh::Boat, 1 }, { EDmTownMesh::Galley, 0 }, { EDmTownMesh::SailShip, 0 }, { EDmTownMesh::Steamer, 0 }, { EDmTownMesh::CargoShip, 0 },
		{ EDmTownMesh::Settlers, 0 }, { EDmTownMesh::Caravan, 0 }, { EDmTownMesh::Caravan, 1 }, { EDmTownMesh::Army, 0 }, { EDmTownMesh::Army, 1 }, { EDmTownMesh::Prophet, 0 } };
	// (wave 4 towns) every age's look (real assets differ by age)
	for (int32 Age = 0; Age < 7; ++Age)
		for (const TPair<EDmTownMesh, int32>& U : UnitLooks) UnitComp(UnitMesh(U.Key, U.Value, Age));
	int32 N = 0;
	for (int32 K = 0; K < int32(EDmTownMesh::Num); ++K)
		for (int32 V = 0; V < UDmTownsFactory::NumVariants(EDmTownMesh(K)); ++V)
			if (UStaticMesh* M = Factory->Get(EDmTownMesh(K), V).Mesh) { Pool.Add(NewPooled(M)); ++N; }
	// (wave 4 towns) and one per real asset mesh (first registration of a mesh costs ~15 ms: never mid-game)
	int32 Na = 0;
	for (UStaticMesh* M : Factory->GetAssets().AllMeshes()) if (M) { Pool.Add(NewPooled(M)); ++Na; }
	PoolMax = FMath::Max(PoolMax, Pool.Num() + 64);
	UE_LOG(LogDmTowns, Display, TEXT("DmTowns: warmed %d unit components, %d kit and %d real-asset components (the pool) in %.0f ms"), UnitComps.Num(), N, Na, (NowS() - T0) * 1000.0);
}

void ADmTowns::UpdateUnits(const dm::World& W, float Dt)
{
	FVector Cam;
	const bool bCam = CameraPlanet(Cam);
	const double SeaR = DmCoords::HeightToRadiusUU(W.seaLevel);
	const float K = 1.f - FMath::Exp(-10.f * FMath::Max(Dt, 0.f));
	++UnitFrame;
	// per component: transforms and custom data this frame
	// (verifier fix) per-component scratch kept between frames: no per-frame allocation (SPEC-UNREAL 0.6)
	TArray<TArray<FTransform>>& Xf = UnitXf; TArray<TArray<float>>& Cd = UnitCd;
	Xf.SetNum(UnitComps.Num()); Cd.SetNum(UnitComps.Num());
	for (TArray<FTransform>& A : Xf) A.Reset();
	for (TArray<float>& A : Cd) A.Reset();
	auto CompFor = [this](EDmTownMesh M, int32 V, int32 Age) -> int32 { return UnitComp(UnitMesh(M, V, Age)); };
	int32 Drawn = 0;
	const double U0 = NowS();
	for (const dm::Unit& U : W.units)
	{
		if (U.path.empty()) continue;
		const int32 I0 = FMath::Clamp(U.i, 0, int32(U.path.size()) - 1);
		const int32 I1 = FMath::Min(I0 + 1, int32(U.path.size()) - 1);
		const int32 Ca = U.path[size_t(I0)], Cb = U.path[size_t(I1)];
		if (!W.valid(Ca) || !W.valid(Cb)) continue;
		const FVector Da = DmCoords::CellDir(W, Ca), Db = DmCoords::CellDir(W, Cb);
		const FVector Dir = FMath::Lerp(Da, Db, float(FMath::Clamp(U.t, 0.0, 1.0))).GetSafeNormal();
		const bool bSea = U.sea || (dm::state::isSea(W, Ca) && dm::state::isSea(W, Cb));
		const double Rr = bSea ? SeaR : (Ground.IsValid() ? Ground->Radius(Dir) : DmCoords::HeightToRadiusUU(W.elev[size_t(Ca)]));
		const FVector Target = Dir * (bSea ? SeaR : Rr + 50.0);
		FMover& Mv = Movers.FindOrAdd(U.id);
		Mv.Seen = UnitFrame;
		if (!Mv.bInit || FVector::Dist(Mv.Pos, Target) > 3.0e6) { Mv.Pos = Target; Mv.bInit = true; }
		else Mv.Pos = FMath::Lerp(Mv.Pos, Target, K);
		FVector Fwd = (Db - Da);
		Fwd = (Fwd - Dir * FVector::DotProduct(Fwd, Dir)).GetSafeNormal();
		if (!Fwd.IsNearlyZero()) Mv.Fwd = FMath::Lerp(Mv.Fwd, Fwd, K).GetSafeNormal();
		const double CamD = bCam ? FVector::Dist(Cam, Mv.Pos) : 1e9;
		if (CamD > 160.0 * DmCoords::UUPerKm) continue;
		const dm::Culture* Cu = dm::state::culture(W, U.culture);
		const int32 Age = FMath::Clamp(AgeOverride >= 0 ? AgeOverride : (Cu ? Cu->age : W.age), 0, 6);
		EDmTownMesh M = EDmTownMesh::Settlers; int32 V = 0;
		if (bSea)
		{
			static const EDmTownMesh Ships[7] = { EDmTownMesh::Boat, EDmTownMesh::Galley, EDmTownMesh::Galley, EDmTownMesh::SailShip, EDmTownMesh::Steamer, EDmTownMesh::CargoShip, EDmTownMesh::CargoShip };
			M = Ships[Age]; V = (Age == 0) ? 1 : 0;
		}
		else switch (U.type)
		{
		case dm::UnitType::Settler: M = EDmTownMesh::Settlers; break;
		case dm::UnitType::Trader: M = EDmTownMesh::Caravan; V = Age >= 4 ? 1 : 0; break;
		case dm::UnitType::Army: M = EDmTownMesh::Army; V = Age >= 4 ? 1 : 0; break;
		case dm::UnitType::Prophet: M = EDmTownMesh::Prophet; break;
		}
		const int32 Ci = CompFor(M, V, Age);
		if (Ci < 0) continue;
		// readable at every zoom: scaled up with distance (a 20 m caravan is a few pixels from 10 km)
		const float S = float(FMath::Clamp(CamD / DmCoords::UUPerKm * 1.5, 1.5, 20.0)) * (bSea ? 0.7f : 1.f);
		const FVector Up = Mv.Pos.GetSafeNormal();
		const FVector Right = FVector::CrossProduct(Up, Mv.Fwd).GetSafeNormal();
		const FVector F2 = FVector::CrossProduct(Right, Up).GetSafeNormal();
		const FMatrix Rm(F2, Right, Up, FVector::ZeroVector);
		Xf[Ci].Add(FTransform(FQuat(Rm), Mv.Pos, FVector(S)));
		const FLinearColor Cc = Cu ? FLinearColor(float(Cu->color[0]), float(Cu->color[1]), float(Cu->color[2])) : FLinearColor::White;
		const float C8[UDmTownsFactory::NumCustomData] = { 1.f, 1.f, 1.f, Cc.R * 1.2f + 0.1f, Cc.G * 1.2f + 0.1f, Cc.B * 1.2f + 0.1f, 0.6f, 0.f };
		Cd[Ci].Append(C8, UDmTownsFactory::NumCustomData);
		++Drawn;
	}
	const double U1 = NowS();
	int32 Recount = 0, Updated = 0;
	double WorstMs = 0; int32 WorstCi = -1, WorstOld = 0;
	for (int32 Ci = 0; Ci < UnitComps.Num(); ++Ci)
	{
		UInstancedStaticMeshComponent* C = UnitComps[Ci];
		if (!C) continue;
		const double C0 = NowS();
		const int32 OldN = C->GetInstanceCount();
		ON_SCOPE_EXIT { const double Ms = (NowS() - C0) * 1000.0; if (Ms > WorstMs) { WorstMs = Ms; WorstCi = Ci; WorstOld = OldN; } };
		// (wave 4 towns, perf4 T3) a fixed capacity per component: a unit appearing or leaving no longer clears and re-adds every
		// instance (that recreated the render state: 17 ms frames). Spare slots are hidden (scale 0 at a drawn unit's place, so
		// the bounds stay tight); the capacity only grows, in powers of two, and an empty component keeps its hidden slots.
		const int32 Want = Xf[Ci].Num();
		if (UnitLastN.Num() < UnitComps.Num()) UnitLastN.SetNumZeroed(UnitComps.Num());
		if (Want == 0 && UnitLastN[Ci] == 0) continue;   // already all hidden
		UnitLastN[Ci] = Want;
		if (Want > OldN)
		{
			++Recount;
			const int32 NewCap = FMath::Max(8, int32(FMath::RoundUpToPowerOfTwo(uint32(Want))));
			const FTransform Hidden(FQuat::Identity, Xf[Ci][0].GetLocation(), FVector(0.f));
			TArray<FTransform> Add;
			Add.Init(Hidden, NewCap - OldN);
			C->AddInstances(Add, false, false, false);
		}
		else ++Updated;
		const int32 Cap = C->GetInstanceCount();
		if (Want < Cap)
		{
			FVector At = Want > 0 ? Xf[Ci][0].GetLocation() : FVector::ZeroVector;
			if (Want == 0) { FTransform T0; if (C->GetInstanceTransform(0, T0, false)) At = T0.GetLocation(); }
			const FTransform Hidden(FQuat::Identity, At, FVector(0.f));
			Xf[Ci].Reserve(Cap);
			while (Xf[Ci].Num() < Cap) Xf[Ci].Add(Hidden);
			Cd[Ci].AddZeroed((Cap - Want) * UDmTownsFactory::NumCustomData);
		}
		// (wave 4 towns) instance deltas only: bMarkRenderStateDirty = true rebuilt every unit component's render proxy every
		// frame (UE 5.7's instance data manager already sends transform and custom-data changes as GPU-scene deltas)
		if (Cap > 0)
		{
			C->BatchUpdateInstancesTransforms(0, Xf[Ci], false, false, true);
			// custom data (culture colour, lit fraction) changes rarely: only when it did
			const TArray<float>& Old = C->PerInstanceSMCustomData;
			if (Old.Num() != Cd[Ci].Num() || FMemory::Memcmp(Old.GetData(), Cd[Ci].GetData(), Cd[Ci].Num() * sizeof(float)) != 0)
				C->SetCustomData(0, Xf[Ci].Num() - 1, TConstArrayView<float>(Cd[Ci]), false);
			// the moved instances' bounds reach the proxy with a transform update (cheap), not a proxy rebuild
			C->UpdateBounds();
			C->MarkRenderTransformDirty();
		}
	}
	for (auto It = Movers.CreateIterator(); It; ++It) if (It.Value().Seen != UnitFrame) It.RemoveCurrent();
	UnitsDrawn = Drawn;
	// (towns-perf verifier) units spikes of 8-24 ms were seen in a few frames per run: where do they go
	const double U2 = NowS();
	if ((U2 - U0) * 1000.0 > 3.0)
		UE_LOG(LogDmTowns, Display, TEXT("DmTowns: slow units %.1f ms: %d sim units, %d drawn, movers %d; loop %.1f ms, components %.1f ms (%d recounted, %d updated; worst %.1f ms: component %d, %d -> %d instances)"),
			(U2 - U0) * 1000.0, int32(W.units.size()), Drawn, Movers.Num(), (U1 - U0) * 1000.0, (U2 - U1) * 1000.0, Recount, Updated, WorstMs, WorstCi, WorstOld, WorstCi >= 0 ? Xf[WorstCi].Num() : 0);
}

// ------------------------------------------------------------------ debug / queries

void ADmTowns::SetAgeOverride(int32 Age) { AgeOverride = Age < 0 ? -1 : FMath::Clamp(Age, 0, 6); bCheckAll = true; for (TPair<int32, FTown>& T : Towns) { T.Value.bDirty = true; T.Value.bGroundOnly = false; } }
void ADmTowns::SetOverride(int32 Sid, int32 Tier, double Pop, int32 Age) { FOverride& O = Overrides.FindOrAdd(Sid); O.Tier = Tier; O.Pop = Pop; O.Age = Age; if (FTown* T = Towns.Find(Sid)) T->bDirty = true; }
void ADmTowns::ClearOverrides() { Overrides.Reset(); for (TPair<int32, FTown>& T : Towns) T.Value.bDirty = true; }
void ADmTowns::SetNightOverride(float Night) { NightOverride = Night; if (Factory) Factory->SetNightOverride(Night); }

bool ADmTowns::IsSettled() const
{
	if (!Ground.IsValid() || Pending.Num() > 0 || bFirstWave) return false;
	for (const TPair<int32, FTown>& T : Towns) if (T.Value.bDirty || !T.Value.Plan.IsValid()) return false;
	return true;
}

const FDmTownPlan* ADmTowns::GetPlan(int32 Sid) const
{
	const FTown* T = Towns.Find(Sid);
	return (T && T->Plan.IsValid()) ? T->Plan.Get() : nullptr;
}

int32 ADmTowns::TotalInstances() const
{
	int32 N = 0;
	for (const TPair<int32, FTown>& T : Towns) if (T.Value.Plan.IsValid()) N += T.Value.Plan->Inst.Num();
	return N;
}

int32 ADmTowns::NumComponents() const
{
	int32 N = UnitComps.Num();
	for (const TPair<int32, FTown>& T : Towns) N += T.Value.Comps.Num() + (T.Value.GroundComp ? 1 : 0);
	return N;
}

FString ADmTowns::Describe() const
{
	int32 Houses = 0, Fields = 0, Inst = 0, Tris = 0, Piers = 0, Walls = 0;
	for (const TPair<int32, FTown>& T : Towns)
	{
		if (!T.Value.Plan.IsValid()) continue;
		const FDmTownPlan& P = *T.Value.Plan;
		Houses += P.Houses; Fields += P.Fields; Inst += P.Inst.Num(); Tris += P.Ground.NumTris(); Piers += P.Piers; Walls += P.WallPieces;
	}
	return FString::Printf(TEXT("%d settlements, %d instances (%d houses), %d fields, %d piers, %d wall pieces, %d overlay tris, %d components (%d visible, %d building components drawn), ground %s"),
		Towns.Num(), Inst, Houses, Fields, Piers, Walls, Tris, NumComponents(), VisibleTowns, VisibleComps, Ground.IsValid() ? Ground->SourceName() : TEXT("none"));
}

TArray<FVector> ADmTowns::SmokeAnchors() const
{
	TArray<FVector> Out;
	for (const TPair<int32, FTown>& T : Towns)
		if (T.Value.bVisible && T.Value.Plan.IsValid())
			for (const FVector& P : T.Value.Plan->Smoke) Out.Add(LastPlanet.TransformPosition(P));
	return Out;
}

void ADmTowns::UnitPositionsWorld(TArray<FVector>& OutPos, TArray<int32>& OutIds) const
{
	OutPos.Reset(); OutIds.Reset();
	for (const TPair<int32, FMover>& M : Movers)
		if (M.Value.bInit && M.Value.Seen == UnitFrame) { OutPos.Add(LastPlanet.TransformPosition(M.Value.Pos)); OutIds.Add(M.Key); }
}

FVector ADmTowns::TownCentreWorld(int32 Sid) const
{
	const FDmTownPlan* P = GetPlan(Sid);
	return P ? LastPlanet.TransformPosition(P->Centre) : FVector::ZeroVector;
}

int32 ADmTowns::CheckConsistency(FString& Out) const
{
	// (towns-perf) components are reused, pooled and frozen now: every settlement with a plan must still show exactly its
	// plan (instance totals, meshes, ground overlay), with visibility and planet attachment matching its visible state
	int32 Bad = 0, Checked = 0, Inst = 0;
	auto Fail = [&](int32 Id, const FString& Why) { if (++Bad <= 8) Out += FString::Printf(TEXT(" [%d: %s]"), Id, *Why); };
	for (const TPair<int32, FTown>& T : Towns)
	{
		const FTown& Tw = T.Value;
		if (!Tw.Plan.IsValid()) continue;
		++Checked;
		const FDmTownPlan& P = *Tw.Plan;
		int32 N = 0;
		for (int32 K = 0; K < Tw.Comps.Num(); ++K)
		{
			const UInstancedStaticMeshComponent* C = Tw.Comps[K];
			if (!C) { Fail(T.Key, TEXT("null component")); continue; }
			N += C->GetInstanceCount();
			const uint32 Key = Tw.CompKeys.IsValidIndex(K) ? Tw.CompKeys[K] : 0;
			if (Factory && C->GetStaticMesh() != ResolveLook(P, Key).Mesh) Fail(T.Key, TEXT("component mesh differs from its batch"));
			if (C->GetVisibleFlag() != Tw.bVisible) Fail(T.Key, TEXT("component visibility"));
			if (C->IsUsingAbsoluteLocation() == Tw.bVisible) Fail(T.Key, TEXT("frozen state"));
			if (Tw.bVisible && !C->GetComponentLocation().Equals(LastPlanet.TransformPosition(P.Centre), 10.0)) Fail(T.Key, TEXT("component not at the plan centre"));
		}
		Inst += N;
		if (N != P.Inst.Num()) Fail(T.Key, FString::Printf(TEXT("%d instances drawn, plan has %d"), N, P.Inst.Num()));
		const bool bWantGround = P.Ground.NumTris() > 0 && Factory && Factory->GroundMat();
		if (bWantGround != (Tw.GroundComp != nullptr && Tw.GroundComp->GetStaticMesh() != nullptr)) Fail(T.Key, TEXT("ground overlay"));
		if (Tw.GroundComp && Tw.GroundComp->GetVisibleFlag() != Tw.bGroundVis) Fail(T.Key, TEXT("ground visibility"));
		if (Tw.GroundComp && Tw.bGroundVis && !Tw.GroundComp->GetComponentLocation().Equals(LastPlanet.TransformPosition(P.Centre), 10.0)) Fail(T.Key, TEXT("ground not at the plan centre"));
	}
	// (towns-perf verifier) coverage and freshness. The checks above compare what is drawn with the plan, so a settlement that
	// never got a plan, or kept an out-of-date one, passed them (after an age change at 8x half the settlements in the city
	// view had no plan for the rest of the run). Unplanned: no plan for over 8 s; stale: its structure (tier, age, buildings,
	// captured / destroyed) changed over 8 s ago and no plan made from the new structure was applied. In view: a problem.
	int32 Unplanned = 0, UnplannedVis = 0, Stale = 0, StaleVis = 0;
	double OldestWait = 0, OldestStale = 0;
	const double NowC = NowS();
	FVector Cam = FVector::ZeroVector;
	const bool bCam = CameraPlanet(Cam);
	const UDmSimSubsystem* SimC = UDmSimSubsystem::Get(this);
	const dm::World* WC = SimC ? SimC->Sim() : nullptr;
	for (const TPair<int32, FTown>& T : Towns)
	{
		const FTown& Tw = T.Value;
		bool bInView = Tw.bVisible;
		if (!Tw.Plan.IsValid() && bCam && WC && T.Key < int32(WC->settlements.size()))
		{
			const FVector C = DmCoords::CellSurfacePos(*WC, WC->settlements[size_t(T.Key)].cell);
			const FVector D = Cam - C;
			bInView = FVector::DotProduct(D, C.GetSafeNormal()) > -0.05 * D.Size() && D.Size() < double(HideKm) * DmCoords::UUPerKm;
		}
		if (!Tw.Plan.IsValid())
		{
			const double Wait = NowC - Tw.FirstSeen;
			OldestWait = FMath::Max(OldestWait, Wait);
			if (Wait > 8.0) { ++Unplanned; if (bInView) { ++UnplannedVis; Fail(T.Key, FString::Printf(TEXT("in view and not drawn for %.0f s"), Wait)); } }
			continue;
		}
		if (WC)
		{
			uint32 St = 0;
			SignatureOf(*WC, T.Key, &St);
			if (St != Tw.PlanStruct && Tw.StructSeen > 0 && NowC - Tw.StructSeen > 8.0)
			{
				++Stale;
				OldestStale = FMath::Max(OldestStale, NowC - Tw.StructSeen);
				if (bInView)
				{
					++StaleVis;
					const FPending* Pp = Pending.Find(T.Key);
					const dm::Settlement& Ss = WC->settlements[size_t(T.Key)];
					Fail(T.Key, FString::Printf(TEXT("in view and out of date for %.0f s (dirty %d, pending %d, last plan %.0f s ago, sig %08x drawn %08x pending %08x, struct %08x plan %08x applied-struct %08x, tier %d dead %d culture %d)"),
						NowC - Tw.StructSeen, Tw.bDirty ? 1 : 0, Pp ? 1 : 0, NowC - Tw.LastPlan, SignatureOf(*WC, T.Key), Tw.Sig, Tw.PendingSig, St, Tw.PlanStruct, Tw.Struct, int32(Ss.tier), Ss.dead ? 1 : 0, int32(Ss.culture)));
				}
			}
		}
	}
	Out = FString::Printf(TEXT("%d settlements checked, %d instances drawn, %d problems; coverage: %d of %d settlements drawn, %d unplanned over 8 s (%d in view; oldest wait %.0f s), %d out of date over 8 s (%d in view; oldest %.0f s)"),
		Checked, Inst, Bad, Checked, Towns.Num(), Unplanned, UnplannedVis, OldestWait, Stale, StaleVis, OldestStale) + Out;
	return Bad;
}

double ADmTowns::GroundRadius(const FVector& Dir) const
{
	if (Ground.IsValid()) return Ground->Radius(Dir);
	return DmCoords::RadiusUU;
}

static FDmScenePartRegistrar GDmTownsPart(TEXT("towns"), EDmPartKind::World, 0,
	[](const FDmSceneContext& S) -> AActor*
	{
		if (!S.World || FDmShotArgs::Param(TEXT("notowns"))) return nullptr;
		return S.World->SpawnActor<ADmTowns>();
	}, TEXT("Game/Towns"));
