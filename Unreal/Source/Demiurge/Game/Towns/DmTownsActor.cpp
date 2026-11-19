// DEMIURGE — Game/Towns/DmTownsActor.cpp : see the header.

#include "Game/Towns/DmTownsActor.h"
#include "Game/Towns/DmTownsKit.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmScene.h"
#include "Game/Core/DmSimSubsystem.h"

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
#include "Materials/MaterialInstanceDynamic.h"
#include "Sim/State.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmTowns, Log, All);

namespace
{
	double NowS() { return FPlatformTime::Seconds(); }

	uint32 Mix(uint32 H, uint32 V) { return DmTowns::Hash(H, V, 0x9E37u); }

	float CullM(const FDmKitInfo& I)
	{
		const float S = FMath::Max3(I.W, I.D, I.H);
		if (S < 14.f) return 26000.f;
		if (S < 40.f) return 45000.f;
		return 90000.f;
	}
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
	Factory = NewObject<UDmTownsFactory>(this);
	Factory->Build();
	KitTable = MakeShared<FDmKitTable, ESPMode::ThreadSafe>();
	for (int32 K = 0; K < int32(EDmTownMesh::Num); ++K) KitTable->Infos[K] = Factory->Infos(EDmTownMesh(K));

	double D = 0;
	int32 Iv = -1;
	if (FDmShotArgs::Value(TEXT("-townage="), Iv)) AgeOverride = FMath::Clamp(Iv, 0, 6);
	if (FDmShotArgs::Value(TEXT("-townscale="), D)) Scale = float(FMath::Clamp(D, 0.2, 5.0));
	if (FDmShotArgs::Value(TEXT("-townhidekm="), D)) HideKm = float(FMath::Max(D, 1.0));
	if (FDmShotArgs::Value(TEXT("-townnight="), D)) NightOverride = float(D);
	if (FDmShotArgs::Param(TEXT("nounits"))) bUnits = false;
	if (FDmShotArgs::Value(TEXT("-townfields="), Iv)) FieldMode = FMath::Clamp(Iv, -1, 1);
	bLog = FDmShotArgs::Param(TEXT("townlog"));
	double Gl[3] = { 3.0, 1.6, 1.4 };
	FDmShotArgs::List(TEXT("-townglow="), Gl, 3);
	Factory->SetGlow(float(Gl[0]), float(Gl[1]), float(Gl[2]));
	Factory->SetNightOverride(NightOverride);

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
	for (TPair<int32, FPending>& P : Pending) P.Value.Task.Wait();
	Pending.Reset();
	if (UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
	{
		Sim->Dirty().OnDirty.Remove(DirtyHandle);
		Sim->OnWorldChanged.Remove(WorldHandle);
		Sim->OnSimEvent.Remove(EventHandle);
	}
	Super::EndPlay(Reason);
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
	for (TPair<int32, FPending>& P : Pending) P.Value.Task.Wait();
	Pending.Reset();
	for (TPair<int32, FTown>& T : Towns) DestroyComponents(T.Value);
	Towns.Reset();
	for (UInstancedStaticMeshComponent* C : UnitComps) if (C) C->DestroyComponent();
	UnitComps.Reset();
	UnitCompMesh.Reset();
	Movers.Reset();
	Ground.Reset();
	Snap.Reset();
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
	if (Ground.IsValid() && Now - LastGroundCheck > 2.0)
	{
		LastGroundCheck = Now;
		const uint64 S = FDmTownsGround::Signature(GetWorld());
		if (S != GroundSig)
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
	bool bPend = false;
	FDmTownsGroundPtr G = FDmTownsGround::Snapshot(GetWorld(), W, bPend);
	if (bPend && Now - FirstTick < 15.0) return false;   // the placeholder planet builds its mesh in its first tick
	Ground = G;
	GroundSig = G->Sig;
	LastGroundCheck = Now;
	UE_LOG(LogDmTowns, Display, TEXT("DmTowns: ground from the %s (%d triangles, %.0f ms)"), Ground->SourceName(), Ground->Triangles, Ground->BuildMs);
	return true;
}

void ADmTowns::AdoptGround(const FDmTownsGroundPtr& G, uint64 NewSig, const TCHAR* Why)
{
	if (!G.IsValid()) return;
	// (verifier fix) the planet re-captures its terrain for any edit anywhere (and once right after startup
	// with unchanged data); re-planning all ~100 settlements each time cost ~20 s of worker time and a wave of
	// component rebuilds. Probe the old and new ground under each settlement and re-plan only where it moved.
	const double T0 = NowS();
	int32 Moved = 0;
	const FDmTownsGroundPtr Old = Ground;
	for (TPair<int32, FTown>& T : Towns)
	{
		FTown& Tw = T.Value;
		// (integration) a town without a plan yet is already waiting for one (planned against the newest ground when
		// its task starts); marking it and bumping its generation threw away every in-flight plan on each re-capture,
		// so under a rising sea at 8x the ~100 towns were planned over and over (30-230 ms game-thread frames)
		if (!Tw.Plan.IsValid() && Old.IsValid() && Old->GetSource() == G->GetSource()) continue;
		bool bMoved = !Old.IsValid() || !Tw.Plan.IsValid() || Old->GetSource() != G->GetSource();
		if (!bMoved)
		{
			const FVector U = Tw.Plan->Centre.GetSafeNormal();
			FVector E, N;
			DmTownsPlan::Frame(U, E, N);
			const double Rr[3] = { 0.0, FMath::Max(double(Tw.Plan->RadiusM) * 0.6, 40.0), FMath::Max(double(Tw.Plan->ExtentM) * 0.7, 120.0) };
			double MinR = 1e30;
			for (int32 Ri = 0; Ri < 3 && !bMoved; ++Ri)
				for (int32 K = 0; K < (Ri == 0 ? 1 : 8) && !bMoved; ++K)
				{
					const double A = K * UE_DOUBLE_TWO_PI / 8.0 + Ri * 0.39;
					const FVector D = (U * 1.0e7 + (E * FMath::Cos(A) + N * FMath::Sin(A)) * Rr[Ri] * 100.0).GetSafeNormal();
					const double Rn = G->Radius(D);
					MinR = FMath::Min(MinR, Rn);
					if (FMath::Abs(Old->Radius(D) - Rn) > 5.0) bMoved = true;   // 5 cm
				}
			// (integration) a rising sea moves the planet's sea level in 0.5 m steps several times a second at 8x in the
			// late ages; re-planning every town for it (it was all ~100 each time) cost 30-230 ms game-thread frames.
			// Only towns whose ground comes within 30 m of the new sea are re-planned (shore, docks, piers)
			if (!bMoved && FMath::Abs(Old->SeaRadiusUU - G->SeaRadiusUU) > 5.0 && MinR - G->SeaRadiusUU < 3000.0) bMoved = true;
		}
		if (bMoved) { Tw.bDirty = true; ++Tw.GroundGen; ++Moved; }
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
	In.bFieldOverlay = FieldMode > 0 || (FieldMode < 0 && !(Ground.IsValid() && Ground->GetSource() == FDmTownsGround::ESource::PlanetTerrain));
	if (AgeOverride >= 0) In.Age = AgeOverride;
	if (const FOverride* O = Overrides.Find(Sid))
	{
		if (O->Tier >= 0) In.Tier = O->Tier;
		if (O->Pop >= 0) In.Pop = O->Pop;
		if (O->Age >= 0) In.Age = O->Age;
	}
	// neighbours within ~14 km, nearest first; a road when both centres carry one
	struct FN { double Ang; FVector Dir; uint8 Road; };
	TArray<FN> Ns;
	for (const dm::Settlement& O : W.settlements)
	{
		if (O.dead || O.id == S.id || !W.valid(O.cell)) continue;
		const double A = W.geo->angle(S.cell, O.cell);
		if (A > 0.14) continue;
		Ns.Add({ A, DmCoords::CellDir(W, O.cell), uint8(W.road[size_t(S.cell)] > 0 && W.road[size_t(O.cell)] > 0 ? 1 : 0) });
	}
	Ns.Sort([](const FN& A, const FN& B) { return A.Ang < B.Ang; });
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
		if (!T) { T = &Towns.Add(Sid); T->Id = Sid; T->bDirty = true; continue; }
		if (!bAll && !EventTouched.Contains(Sid)) continue;
		uint32 Struct = 0;
		const uint32 Sig = SignatureOf(W, Sid, &Struct);
		if (Sig == T->Sig || Sig == T->PendingSig) continue;
		// growth alone (population step, neighbours, ruins fading) waits for GrowthEvery; structure changes at once
		if (Struct == T->Struct && NowS() - T->LastPlan < GrowthEvery) continue;
		T->bDirty = true;
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
	TArray<TPair<double, int32>> Order;
	for (TPair<int32, FTown>& T : Towns)
	{
		if (!T.Value.bDirty || Pending.Contains(T.Key)) continue;
		const dm::Settlement& S = W.settlements[size_t(T.Key)];
		Order.Add({ bCam ? FVector::Dist(Cam, DmCoords::CellSurfacePos(W, S.cell)) : 0.0, T.Key });
	}
	Order.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
	for (const TPair<double, int32>& O : Order)
	{
		if (Pending.Num() >= MaxTasks) break;
		FTown& T = Towns[O.Value];
		FDmTownInput In;
		if (!MakeInput(W, O.Value, In)) { T.bDirty = false; continue; }
		const uint32 Sig = SignatureOf(W, O.Value);
		T.bDirty = false;
		T.PendingSig = Sig;
		TSharedPtr<const FDmTownsWorldSnap, ESPMode::ThreadSafe> SnapP = Snap;
		FDmTownsGroundPtr GroundP = Ground;
		TSharedPtr<FDmKitTable, ESPMode::ThreadSafe> KitP = KitTable;
		FPending P;
		P.Sig = Sig;
		P.GroundGen = T.GroundGen;
		P.Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, [In, SnapP, GroundP, KitP, Sig]()
		{
			TSharedPtr<FDmTownPlan, ESPMode::ThreadSafe> Plan = MakeShared<FDmTownPlan, ESPMode::ThreadSafe>();
			DmTownsPlan::PlanTown(In, *SnapP, *GroundP, *KitP, *Plan);
			Plan->Sig = Sig;
			return Plan;
		}, UE::Tasks::ETaskPriority::BackgroundNormal);
		Pending.Add(O.Value, MoveTemp(P));
	}
}

void ADmTowns::ApplyDone()
{
	int32 Applied = 0;
	TArray<int32> Done;
	for (TPair<int32, FPending>& P : Pending)
	{
		if (!P.Value.Task.IsCompleted()) continue;
		Done.Add(P.Key);
		TSharedPtr<FDmTownPlan, ESPMode::ThreadSafe> Plan = P.Value.Task.GetResult();
		++NumPlans; PlanMs += Plan->Ms;
		FTown* T = Towns.Find(P.Key);
		if (!T || P.Value.Sig != T->PendingSig || P.Value.GroundGen != T->GroundGen) { if (T) T->bDirty = true; continue; }
		ApplyPlan(*T, Plan, T->Plan.IsValid());
		if (++Applied >= MaxAppliesPerFrame) break;
	}
	for (int32 K : Done) Pending.Remove(K);
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
	for (TPair<int32, FPending>& P : Pending) P.Value.Task.Wait();
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
		Ids.Add(T.Key); Sigs.Add(SignatureOf(*W, T.Key)); Ins.Add(MoveTemp(In));
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
	if (bConstruct && T.Plan.IsValid() && ConstructionSeconds > 0.f && !P->Inst.IsEmpty())
	{
		// (verifier fix) a building is new only where nothing stood before. Slot keys hash the exact position, and a
		// re-plan shifts the streets slightly whenever the radius or a neighbour changes, so the key diff turned
		// 76% of all instances into scaffolds on every re-plan of a live world (towns-live: 264k of 348k). Match
		// by place instead: a new instance within 7 m of any old one (any kind: rebuilt or upgraded in place) is
		// not under construction.
		TSet<uint32> Old;
		TMultiMap<FIntVector, FVector> OldAt;
		constexpr double CellUU = 1000.0, NearUU = 700.0;
		for (const FDmTownInst& I : T.Plan->Inst)
		{
			Old.Add(I.Key);
			const FVector A = T.Plan->Centre + FVector(I.Pos);
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
		for (const FDmTownInst& I : P->Inst) if (I.Cd[7] > 0.5f) bRuins = true;
		if (!bRuins)
			for (const FDmTownInst& I : P->Inst)
				if (!Old.Contains(I.Key) && I.Mesh != EDmTownMesh::Boat && I.Mesh < EDmTownMesh::Galley && !StoodThere(P->Centre + FVector(I.Pos)))
					T.Building.Add(I.Key, Now);
	}
	TSet<uint32> Now2;
	for (const FDmTownInst& I : P->Inst) Now2.Add(I.Key);
	for (auto It = T.Building.CreateIterator(); It; ++It) if (!Now2.Contains(It.Key())) It.RemoveCurrent();
	T.Plan = P;
	T.Sig = P->Sig;
	T.LastPlan = Now;
	if (UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
		if (const dm::World* W = Sim->Sim()) SignatureOf(*W, T.Id, &T.Struct);
	BuildComponents(T);
	if (bLog) UE_LOG(LogDmTowns, Display, TEXT("DmTowns: plan %d (%s): %d instances, %d houses, %d civic, %d fields, %d streets, %d piers, %d ships, %d wall pieces, %d under construction, %.1f ms"),
		T.Id, *P->Note, P->Inst.Num(), P->Houses, P->Civic, P->Fields, P->Streets, P->Piers, P->Ships, P->WallPieces, T.Building.Num(), P->Ms);
}

void ADmTowns::DestroyComponents(FTown& T, bool bInstancesOnly)
{
	for (UInstancedStaticMeshComponent* C : T.Comps) if (C) { Keep.Remove(C); C->DestroyComponent(); }
	T.Comps.Reset();
	if (bInstancesOnly) return;
	if (T.GroundComp) { Keep.Remove(T.GroundComp); T.GroundComp->DestroyComponent(); T.GroundComp = nullptr; }
	if (T.GroundMesh) { Keep.Remove(T.GroundMesh); T.GroundMesh = nullptr; }
}

void ADmTowns::BuildComponents(FTown& T, bool bInstancesOnly)
{
	DestroyComponents(T, bInstancesOnly && T.GroundComp != nullptr);
	if (!T.Plan.IsValid() || !Factory) return;
	const FDmTownPlan& P = *T.Plan;
	const double Now = NowS();
	// group by (mesh, variant); buildings under construction stand as scaffolds of their footprint
	TMap<uint32, TArray<int32>> Groups;
	TArray<FTransform> Xf; Xf.SetNum(P.Inst.Num());
	for (int32 I = 0; I < P.Inst.Num(); ++I)
	{
		const FDmTownInst& In = P.Inst[I];
		EDmTownMesh M = In.Mesh; int32 V = In.Var;
		FVector3f S = In.Scale;
		if (const double* St = T.Building.Find(In.Key))
			if (Now - *St < ConstructionSeconds)
			{
				M = EDmTownMesh::Scaffold; V = 0;
				const float Prog = float(FMath::Clamp((Now - *St) / FMath::Max(ConstructionSeconds, 0.1f), 0.0, 1.0));
				S = FVector3f(FMath::Max(In.W, 3.f) / 10.f, FMath::Max(In.D, 3.f) / 8.f, FMath::Max(In.H, 3.f) / 6.f * (0.45f + 0.4f * Prog));
			}
		Xf[I] = FTransform(FQuat(In.Rot), FVector(In.Pos), FVector(S));
		Groups.FindOrAdd((uint32(M) << 8) | uint32(V)).Add(I);
	}
	for (TPair<uint32, TArray<int32>>& Gp : Groups)
	{
		const EDmTownMesh M = EDmTownMesh(Gp.Key >> 8);
		const int32 V = int32(Gp.Key & 0xff);
		const FDmKitInfo& Info = Factory->Get(M, V);
		if (!Info.Mesh) continue;
		TArray<FTransform> X; X.Reserve(Gp.Value.Num());
		TArray<float> Cd; Cd.Reserve(Gp.Value.Num() * UDmTownsFactory::NumCustomData);
		for (int32 I : Gp.Value)
		{
			X.Add(Xf[I]);
			Cd.Append(P.Inst[I].Cd, UDmTownsFactory::NumCustomData);
		}
		// plain ISM: a HISM loses its per-instance custom data when its async tree build re-sorts the instances (UE 5.7,
		// measured with towns-cdtest); settlements are compact, so per-component LOD is fine
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
		C->SetMobility(EComponentMobility::Movable);
		C->SetupAttachment(Root);
		C->SetRelativeLocation(P.Centre);
		C->SetStaticMesh(Info.Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCanEverAffectNavigation(false);
		C->SetGenerateOverlapEvents(false);
		C->SetCastShadow(true);
		C->bAffectDistanceFieldLighting = false;
		C->bReceivesDecals = false;
		C->ShadowCacheInvalidationBehavior = EShadowCacheInvalidationBehavior::Rigid;
		const int32 Cull = int32(CullM(Info) * 100.0);
		C->SetCullDistances(int32(Cull * 0.8), Cull);
		C->SetNumCustomDataFloats(UDmTownsFactory::NumCustomData);
		C->PreAllocateInstancesMemory(X.Num());
		C->AddInstances(X, false, false, false);
		C->SetCustomData(0, X.Num() - 1, TConstArrayView<float>(Cd), false);
		C->SetVisibility(T.bVisible);
		C->RegisterComponent();
		if (bLog && T.Comps.Num() == 0)
			UE_LOG(LogDmTowns, Display, TEXT("DmTowns: comp %s: %d instances, %d custom floats, data %d: %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f"), *Info.Mesh->GetName(), C->GetInstanceCount(), C->NumCustomDataFloats, C->PerInstanceSMCustomData.Num(),
				C->PerInstanceSMCustomData.Num() > 7 ? C->PerInstanceSMCustomData[0] : -1.f, C->PerInstanceSMCustomData.Num() > 7 ? C->PerInstanceSMCustomData[1] : -1.f, C->PerInstanceSMCustomData.Num() > 7 ? C->PerInstanceSMCustomData[2] : -1.f, C->PerInstanceSMCustomData.Num() > 7 ? C->PerInstanceSMCustomData[3] : -1.f,
				C->PerInstanceSMCustomData.Num() > 7 ? C->PerInstanceSMCustomData[4] : -1.f, C->PerInstanceSMCustomData.Num() > 7 ? C->PerInstanceSMCustomData[5] : -1.f, C->PerInstanceSMCustomData.Num() > 7 ? C->PerInstanceSMCustomData[6] : -1.f, C->PerInstanceSMCustomData.Num() > 7 ? C->PerInstanceSMCustomData[7] : -1.f);
		T.Comps.Add(C);
		Keep.Add(C);
	}
	if (bInstancesOnly && T.GroundComp) return;   // (verifier fix) finishing scaffolds no longer rebuilds the overlay mesh
	if (P.Ground.NumTris() > 0 && Factory->GroundMat())
	{
		T.GroundMesh = DmTowns::MakeStaticMesh(this, FString::Printf(TEXT("SM_TownGround_%d"), T.Id), { &P.Ground }, { Factory->GroundMat() }, { 1.f }, false);
		UStaticMeshComponent* G = NewObject<UStaticMeshComponent>(this);
		G->SetMobility(EComponentMobility::Movable);
		G->SetupAttachment(Root);
		G->SetRelativeLocation(P.Centre);
		G->SetStaticMesh(T.GroundMesh);
		G->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		G->SetCanEverAffectNavigation(false);
		G->SetGenerateOverlapEvents(false);
		G->SetCastShadow(false);
		G->bAffectDistanceFieldLighting = false;
		G->bReceivesDecals = false;
		G->SetVisibility(T.bVisible);
		G->RegisterComponent();
		T.GroundComp = G;
		Keep.Add(G);
		Keep.Add(T.GroundMesh);
	}
}

void ADmTowns::UpdateVisibility()
{
	FVector Cam;
	if (!CameraPlanet(Cam)) return;
	const double Hide = double(HideKm) * DmCoords::UUPerKm;
	int32 Vis = 0;
	for (TPair<int32, FTown>& T : Towns)
	{
		if (!T.Value.Plan.IsValid()) continue;
		const FVector C = T.Value.Plan->Centre;
		const FVector D = Cam - C;
		const double Dist = D.Size();
		const double Ext = double(T.Value.Plan->ExtentM) * 100.0;
		const bool bAbove = FVector::DotProduct(D, C.GetSafeNormal()) > -0.05 * Dist - Ext * 0.05;
		const bool bVis = bAbove && Dist - Ext < Hide;
		if (bVis) ++Vis;
		if (bVis == T.Value.bVisible) continue;
		T.Value.bVisible = bVis;
		for (UInstancedStaticMeshComponent* Cp : T.Value.Comps) if (Cp) Cp->SetVisibility(bVis);
		if (T.Value.GroundComp) T.Value.GroundComp->SetVisibility(bVis);
	}
	VisibleTowns = Vis;
}

void ADmTowns::UpdateConstruction()
{
	const double Now = NowS();
	int32 Rebuilt = 0;
	for (TPair<int32, FTown>& T : Towns)
	{
		if (T.Value.Building.Num() == 0 || Rebuilt >= 3) continue;
		bool bChanged = false;
		for (auto It = T.Value.Building.CreateIterator(); It; ++It)
			if (Now - It.Value() >= ConstructionSeconds) { It.RemoveCurrent(); bChanged = true; }
		// finished buildings replace their scaffolds (components are rebuilt only then)
		if (bChanged) { BuildComponents(T.Value, true); ++Rebuilt; }
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
	auto Mark = [&]() { const double N2 = NowS(); if (PhN < 8) Ph[PhN++] = (N2 - PhT) * 1000.0; PhT = N2; };
	ON_SCOPE_EXIT
	{
		const double Ms = (NowS() - TickStart) * 1000.0;
		MaxTickMs = FMath::Max(MaxTickMs, Ms);
		if (Ms > 50.0)
			UE_LOG(LogDmTowns, Display, TEXT("DmTowns: slow tick %.0f ms (world/ground %.1f, snap %.1f, signatures+launch %.1f, apply %.1f, construction %.1f, visibility %.1f, units %.1f)"),
				Ms, Ph[0], Ph[1], Ph[2], Ph[3], Ph[4], Ph[5], Ph[6]);
	};
	FollowPlanet();
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
		RebuildAll(true, true);
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
		ApplyDone();
	}
	Mark();
	if (Now - LastConstruction > 0.5) { LastConstruction = Now; UpdateConstruction(); }
	Mark();
	UpdateVisibility();
	Mark();
	if (bUnits) UpdateUnits(*W, DeltaSeconds);
	Mark();
}

// ------------------------------------------------------------------ units

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
	auto CompFor = [&](EDmTownMesh M, int32 V) -> int32
	{
		const int32 Key = int32(M) * 8 + V;
		int32 Idx = UnitCompMesh.IndexOfByKey(Key);
		if (Idx != INDEX_NONE) return Idx;
		const FDmKitInfo& Info = Factory->Get(M, V);
		if (!Info.Mesh) return -1;
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
		C->SetMobility(EComponentMobility::Movable);
		C->SetupAttachment(Root);
		C->SetStaticMesh(Info.Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCanEverAffectNavigation(false);
		C->SetCastShadow(true);
		C->bReceivesDecals = false;
		C->bAffectDistanceFieldLighting = false;
		C->SetNumCustomDataFloats(UDmTownsFactory::NumCustomData);
		C->RegisterComponent();
		UnitComps.Add(C);
		UnitCompMesh.Add(Key);
		Xf.AddDefaulted(); Cd.AddDefaulted();
		return UnitComps.Num() - 1;
	};
	int32 Drawn = 0;
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
		const int32 Ci = CompFor(M, V);
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
	for (int32 Ci = 0; Ci < UnitComps.Num(); ++Ci)
	{
		UInstancedStaticMeshComponent* C = UnitComps[Ci];
		if (!C) continue;
		if (C->GetInstanceCount() != Xf[Ci].Num())
		{
			C->ClearInstances();
			if (Xf[Ci].Num()) { C->AddInstances(Xf[Ci], false, false, false); C->SetCustomData(0, Xf[Ci].Num() - 1, TConstArrayView<float>(Cd[Ci]), true); }
		}
		else if (Xf[Ci].Num())
		{
			C->BatchUpdateInstancesTransforms(0, Xf[Ci], false, true, true);
			C->SetCustomData(0, Xf[Ci].Num() - 1, TConstArrayView<float>(Cd[Ci]), true);
		}
	}
	for (auto It = Movers.CreateIterator(); It; ++It) if (It.Value().Seen != UnitFrame) It.RemoveCurrent();
	UnitsDrawn = Drawn;
}

// ------------------------------------------------------------------ debug / queries

void ADmTowns::SetAgeOverride(int32 Age) { AgeOverride = Age < 0 ? -1 : FMath::Clamp(Age, 0, 6); bCheckAll = true; for (TPair<int32, FTown>& T : Towns) T.Value.bDirty = true; }
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
	return FString::Printf(TEXT("%d settlements, %d instances (%d houses), %d fields, %d piers, %d wall pieces, %d overlay tris, %d components, ground %s"),
		Towns.Num(), Inst, Houses, Fields, Piers, Walls, Tris, NumComponents(), Ground.IsValid() ? Ground->SourceName() : TEXT("none"));
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

static FDmScenePartRegistrar GDmTownsPart(TEXT("towns"), EDmPartKind::World, 0,
	[](const FDmSceneContext& S) -> AActor*
	{
		if (!S.World || FDmShotArgs::Param(TEXT("notowns"))) return nullptr;
		return S.World->SpawnActor<ADmTowns>();
	}, TEXT("Game/Towns"));
