// DEMIURGE — Game/Planet/DmPlanet.cpp : see the header. Owner: C1 planet.

#include "Game/Planet/DmPlanet.h"
#include "Game/Planet/DmPlanetNoise.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmScene.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Sim/State.h"

#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "SceneInterface.h"
#include "ProceduralMeshComponent.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmPlanet, Log, All);

namespace
{
	constexpr double DatumM = FDmPlanetTerrain::DatumM;
	constexpr double RadiusCm = 1.0e7;
	inline FVector3d Nrm(const FVector3d& V) { return V * (1.0 / FMath::Sqrt(FMath::Max(V | V, 1e-300))); }
	inline FVector3d ToSim(const FVector3d& V) { return FVector3d(V.X, V.Z, V.Y); }
	inline FVector ToUE(const FVector3d& V) { return FVector(V.X, V.Z, V.Y); }
	// The sim -> Unreal axis swap mirrors the mesh; the sim's counter-clockwise triangles come out
	// front-facing in Unreal as they are (lab).
	constexpr bool bFlipWinding = false;
	constexpr int32 MaxNodes = 400000;
}

// ================================================================================================ lifecycle

ADmPlanet::ADmPlanet()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

ADmPlanet* ADmPlanet::Find(const UObject* WorldContext)
{
	const UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) return nullptr;
	for (TActorIterator<ADmPlanet> It(const_cast<UWorld*>(W)); It; ++It) return *It;
	return nullptr;
}

void ADmPlanet::BeginPlay()
{
	Super::BeginPlay();
	{
		int32 D = 0;
		if (FDmShotArgs::Value(TEXT("-planetdebug="), D)) DebugMode = D;
		double Px = 0;
		if (FDmShotArgs::Value(TEXT("-planetlodpx="), Px) && Px > 1.0) TargetPx = Px;
		int32 Ml = 0;
		if (FDmShotArgs::Value(TEXT("-planetmaxlevel="), Ml) && Ml > 0) MaxLevel = FMath::Clamp(Ml, 1, 16);
		bLogLod = FDmShotArgs::Param(TEXT("planetlog"));
		bNoCull = FDmShotArgs::Param(TEXT("planetnocull"));   // debug: split out-of-view patches too
		FString Ov;
		if (FDmShotArgs::Value(TEXT("-overlay="), Ov))
		{
			Ov = Ov.ToLower();
			if (Ov.IsNumeric()) Overlay = FMath::Clamp(FCString::Atoi(*Ov), 0, 5);
			else if (Ov == TEXT("owner") || Ov == TEXT("cultures")) Overlay = 1;
			else if (Ov == TEXT("temp") || Ov == TEXT("temperature")) Overlay = 2;
			else if (Ov == TEXT("rain") || Ov == TEXT("rainfall") || Ov == TEXT("moist")) Overlay = 3;
			else if (Ov == TEXT("devotion")) Overlay = 4;
			else if (Ov == TEXT("hab") || Ov == TEXT("habitability")) Overlay = 5;
		}
	}
	UMaterialInterface* LandM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Planet/M_PlanetLand.M_PlanetLand"));
	UMaterialInterface* WaterM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Planet/M_PlanetWater.M_PlanetWater"));
	if (!LandM) UE_LOG(LogDmPlanet, Error, TEXT("DmPlanet: /Game/Planet/M_PlanetLand missing (run Scripts/planet.py)"));
	if (!WaterM) UE_LOG(LogDmPlanet, Error, TEXT("DmPlanet: /Game/Planet/M_PlanetWater missing (run Scripts/planet.py)"));
	if (!LandM) LandM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
	if (!WaterM) WaterM = LandM;
	LandMid = LandM ? UMaterialInstanceDynamic::Create(LandM, this) : nullptr;
	SeabedM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Planet/M_PlanetSeabed.M_PlanetSeabed"));
	WaterMid = WaterM ? UMaterialInstanceDynamic::Create(WaterM, this) : nullptr;
	// Materials compile asynchronously in -game: a shot taken before they finish shows the default
	// material (lab pitfall 4). Finish them now (the shader cache makes this instant after the first run).
	if (GShaderCompilingManager)
	{
		const double T0 = FPlatformTime::Seconds();
		GShaderCompilingManager->FinishAllCompilation();
		UE_LOG(LogDmPlanet, Display, TEXT("DmPlanet: shaders ready in %.0f ms"), (FPlatformTime::Seconds() - T0) * 1000.0);
	}
	if (UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
	{
		DirtyHandle = Sim->Dirty().OnDirty.AddUObject(this, &ADmPlanet::OnDirty);
		WorldHandle = Sim->OnWorldChanged.AddUObject(this, &ADmPlanet::OnWorldChanged);
		SetActorTransform(Sim->GetPlanetToWorld());
	}
	bWorldPending = true;
	BuildFromWorld();
}

void ADmPlanet::CheckMaterials()
{
	// A Custom-node HLSL error does not stop the game: the engine silently draws the default material.
	// Once the compiler is idle, say so loudly (with the compiler's messages), so a broken material never
	// passes for a look.
	if (bMaterialsChecked || !GetWorld() || !GetWorld()->Scene) return;
	if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0) { bSawCompile = true; IdleFrames = 0; return; }
	// compiles are queued a few frames after load, and a failure is finalised a few frames after the last job
	if (++IdleFrames < (bSawCompile ? 10 : 90)) return;
	bMaterialsChecked = true;
	for (UMaterialInstanceDynamic* Mid : { LandMid.Get(), WaterMid.Get() })
	{
		UMaterial* Base = Mid ? Mid->GetMaterial() : nullptr;
		const FMaterialResource* Res = Base ? Base->GetMaterialResource(GetWorld()->Scene->GetShaderPlatform()) : nullptr;
		if (!Res) continue;
		const TArray<FString>& Errs = Res->GetCompileErrors();
		if (Errs.Num() == 0) UE_LOG(LogDmPlanet, Display, TEXT("DmPlanet: material %s compiled"), *Base->GetName());
		for (const FString& E : Errs) UE_LOG(LogDmPlanet, Error, TEXT("DmPlanet: material %s: %s"), *Base->GetName(), *E);
	}
}

void ADmPlanet::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
	{
		Sim->Dirty().OnDirty.Remove(DirtyHandle);
		Sim->OnWorldChanged.Remove(WorldHandle);
	}
	// wait for worker jobs that reference our data (they only hold shared pointers, so this is just tidy)
	Super::EndPlay(Reason);
}

void ADmPlanet::OnDirty(const FDmDirtyFlags& F)
{
	if (F.bElev || F.bWater || F.bRivers || F.bRoads) bTerrainDirty = true;
	if (F.bColor || F.bWater || F.bElev || F.bRoads || F.bObjects) bColorDirty = true;
	if (F.bRivers || F.bRoads) bRiverMapDirty = true;
	if (Overlay > 0 && (F.bColor || F.bObjects)) bOverlayDirty = true;
}

void ADmPlanet::OnWorldChanged(const dm::World*)
{
	bWorldPending = true;
}

void ADmPlanet::ResetWorld()
{
	for (int32 Ni = 0; Ni < Nodes.Num(); ++Ni) Release(Ni);
	Nodes.Reset(); Draw.Reset(); Need.Reset(); DrawSet.Reset();
	Terr.Reset(); TerrainJob.Reset(); RiverJob.Reset(); MapJob.Reset(); Loc.Reset(); FreeBlocks.Reset();
	Maps = FDmPlanetMaps();
	MapTextures.Reset();
	RiverTex = nullptr;
}

bool ADmPlanet::BuildFromWorld()
{
	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const dm::World* W = Sim ? Sim->Sim() : nullptr;
	if (!W || !W->geo) return false;
	bWorldPending = false;
	ResetWorld();
	const double T0 = FPlatformTime::Seconds();

	TSharedPtr<FDmGeoLocator, ESPMode::ThreadSafe> NewLoc = MakeShared<FDmGeoLocator, ESPMode::ThreadSafe>();
	if (!NewLoc->Build(*W)) { UE_LOG(LogDmPlanet, Error, TEXT("DmPlanet: world has no usable grid")); return false; }
	Loc = NewLoc;
	UE_LOG(LogDmPlanet, Display, TEXT("DmPlanet: grid L %d, %d cells; locator check %d mismatches of %d; icosahedron error %.2e"),
		Loc->L, Loc->N, Loc->CheckMismatches, Loc->CheckCount, Loc->IcoMaxErr);

	TSharedPtr<FDmPlanetTerrain, ESPMode::ThreadSafe> T = MakeShared<FDmPlanetTerrain, ESPMode::ThreadSafe>();
	T->Capture(*W, Loc);
	T->Version = ++TerrainVersion;
	T->Prepare();
	Terr = T;
	UE_LOG(LogDmPlanet, Display, TEXT("DmPlanet: terrain prepared in %.0f ms (sea %.1f m, %d river chains + %d delta branches, %d points, riverT %.2f)"),
		T->PrepareMs, T->SeaM, T->NumChains, T->NumDeltaBranches, T->RPts.Num(), T->RiverT);

	Maps.Init(Loc);
	Maps.ComputeAll(this, *W, Overlay);
	MapTextures.Reset();
	for (UTexture2D* Tx : Maps.Textures) MapTextures.Add(Tx);
	UE_LOG(LogDmPlanet, Display, TEXT("DmPlanet: state maps %d^2: texel cache %.0f ms, compute %.0f ms, upload %.0f ms"),
		FDmPlanetMaps::StateSize, Maps.InitMs, Maps.LastComputeMs, Maps.LastUploadMs);

	{
		const double T1 = FPlatformTime::Seconds();
		TArray<FDmRoadSeg> Roads;
		FDmPlanetMaps::CaptureRoads(*W, Roads);
		TArray<TArray<FColor>> Mips; int32 Near = 0;
		FDmPlanetMaps::BuildRiverMipData(*T, Roads, Mips, Near);
		RiverTex = FDmPlanetMaps::MakeRiverTexture(this, Mips);
		RiverMapHash = T->RiverHash ^ (FDmPlanetMaps::RoadHash(*W) * 31u);
		LastRiverMapMs = (FPlatformTime::Seconds() - T1) * 1000.0;
		UE_LOG(LogDmPlanet, Display, TEXT("DmPlanet: river map %d^2 (%d texels near a river, %d road segments) in %.0f ms"), FDmPlanetMaps::RiverSize, Near, Roads.Num(), LastRiverMapMs);
	}
	SetMaterialTextures();

	Nodes.Reserve(65536);
	for (int32 F = 0; F < 20; ++F)
	{
		FDmQNode& N = Nodes.AddDefaulted_GetRef();
		for (int32 K3 = 0; K3 < 3; ++K3) N.C[K3] = Loc->Ico[FDmGeoLocator::IcoTris[3 * F + K3]];
		N.Ctr = Nrm(N.C[0] + N.C[1] + N.C[2]);
		N.Ang = FMath::Acos(FMath::Min3(N.Ctr | N.C[0], N.Ctr | N.C[1], N.Ctr | N.C[2]));
		N.MinH = float(T->MinH); N.MaxH = float(T->MaxH);
	}
	bTerrainDirty = bColorDirty = bRiverMapDirty = bOverlayDirty = false;
	LastColorTime = FPlatformTime::Seconds();
	UE_LOG(LogDmPlanet, Display, TEXT("DmPlanet: built from world \"%s\" in %.0f ms"), UTF8_TO_TCHAR(W->name.c_str()), (FPlatformTime::Seconds() - T0) * 1000.0);
	return true;
}

void ADmPlanet::SetMaterialTextures()
{
	static const TCHAR* Names[FDmPlanetMaps::NumMaps] = { TEXT("MapA"), TEXT("MapB"), TEXT("MapC"), TEXT("MapD"), TEXT("MapE") };
	for (UMaterialInstanceDynamic* M : { LandMid.Get(), WaterMid.Get() })
	{
		if (!M) continue;
		for (int32 I = 0; I < FDmPlanetMaps::NumMaps && I < MapTextures.Num(); ++I)
			if (MapTextures[I]) M->SetTextureParameterValue(Names[I], MapTextures[I]);
		if (RiverTex) M->SetTextureParameterValue(TEXT("RiverTex"), RiverTex);
	}
}

void ADmPlanet::UpdateMaterialParams()
{
	const FTransform& X = GetActorTransform();
	const FVector Ax = X.TransformVectorNoScale(FVector(1, 0, 0)), Ay = X.TransformVectorNoScale(FVector(0, 1, 0)), Az = X.TransformVectorNoScale(FVector(0, 0, 1));
	const FVector C = X.GetLocation();
	const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const dm::World* W = Sim ? Sim->Sim() : nullptr;
	const float Heat = W && W->phase == dm::Phase::Molten ? float(FMath::Clamp(W->heat, 0.0, 1.0)) : 0.f;
	const double SeaR = RadiusCm + (Terr ? Terr->SeaM : 0.0) * 100.0;
	for (UMaterialInstanceDynamic* M : { LandMid.Get(), WaterMid.Get() })
	{
		if (!M) continue;
		M->SetVectorParameterValue(TEXT("RX"), FLinearColor(float(Ax.X), float(Ax.Y), float(Ax.Z), 0.f));
		M->SetVectorParameterValue(TEXT("RY"), FLinearColor(float(Ay.X), float(Ay.Y), float(Ay.Z), 0.f));
		M->SetVectorParameterValue(TEXT("RZ"), FLinearColor(float(Az.X), float(Az.Y), float(Az.Z), 0.f));
		M->SetVectorParameterValue(TEXT("Center"), FLinearColor(float(C.X), float(C.Y), float(C.Z), 0.f));
		M->SetScalarParameterValue(TEXT("SeaR"), float(SeaR));
		M->SetScalarParameterValue(TEXT("Heat"), Heat);
		M->SetScalarParameterValue(TEXT("Overlay"), Overlay > 0 ? 1.f : 0.f);
		M->SetScalarParameterValue(TEXT("Debug"), float(DebugMode));
		M->SetScalarParameterValue(TEXT("Age"), W ? float(FMath::Clamp(W->age, 0, 6)) : 3.f);
	}
}

// ================================================================================================ data updates

void ADmPlanet::PumpData()
{
	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const dm::World* W = Sim ? Sim->Sim() : nullptr;
	if (bWorldPending) { BuildFromWorld(); return; }
	if (!W || !Loc.IsValid() || W->n != Loc->N) return;

	// terrain (elevation, water, rivers): re-capture on the game thread, prepare on a worker
	if (TerrainJob.IsValid() && TerrainJob->bDone.load())
	{
		TSharedPtr<FDmPlanetTerrain, ESPMode::ThreadSafe> NewT = TerrainJob->T;
		TerrainJob.Reset();
		ApplyTerrain(NewT);
	}
	if (bTerrainDirty && !TerrainJob.IsValid())
	{
		bTerrainDirty = false;
		TSharedPtr<FDmTerrainJob, ESPMode::ThreadSafe> Job = MakeShared<FDmTerrainJob, ESPMode::ThreadSafe>();
		Job->T = MakeShared<FDmPlanetTerrain, ESPMode::ThreadSafe>();
		Job->T->Capture(*W, Loc);
		Job->T->Version = ++TerrainVersion;
		TerrainJob = Job;
		Async(EAsyncExecution::ThreadPool, [Job]() { Job->T->Prepare(); Job->bDone.store(true); });
	}

	// river / road map
	if (RiverJob.IsValid() && RiverJob->bDone.load())
	{
		RiverTex = FDmPlanetMaps::MakeRiverTexture(this, RiverJob->Mips);
		LastRiverMapMs = RiverJob->Ms;
		RiverJob.Reset();
		SetMaterialTextures();
	}
	if (bRiverMapDirty && !RiverJob.IsValid() && !TerrainJob.IsValid()) StartRiverMap();

	// colour maps (throttled to ColorEvery): the world's per-cell fields are copied here (~1 ms), the maps and
	// their mips are computed on a worker (~25 ms), and the result is handed to the render thread. (Computing
	// them here cost the game thread 25-55 ms six times a second in a live world: a steady stutter.)
	if (MapJob.IsValid() && MapJob->bDone.load())
	{
		const double T0 = FPlatformTime::Seconds();
		if (bSnapshot) MapJob->R.Mask &= ~0xFu;   // a time-lapse snapshot took over the colours meanwhile
		Maps.ApplyResult(this, MapJob->R);
		LastMapMs = MapJob->R.ComputeMs;
		MapJob.Reset();
		++MapUpdates;
		LastMapApplyMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	}
	const double Now = FPlatformTime::Seconds();
	if ((bColorDirty || bOverlayDirty) && !MapJob.IsValid() && Maps.IsReady() && Now - LastColorTime >= ColorEvery)
	{
		uint32 Mask = 0;
		if (bColorDirty && !bSnapshot) Mask |= 0xF;
		if (bOverlayDirty || (bColorDirty && Overlay > 0)) Mask |= 0x10;
		bColorDirty = bOverlayDirty = false;
		LastColorTime = Now;
		if (Mask)
		{
			const double T0 = FPlatformTime::Seconds();
			TSharedPtr<FDmMapJob, ESPMode::ThreadSafe> Job = MakeShared<FDmMapJob, ESPMode::ThreadSafe>();
			FDmPlanetMaps::CaptureInputs(*W, Overlay, Job->In);
			TSharedPtr<const FDmPlanetMaps::FCache, ESPMode::ThreadSafe> Cache = Maps.GetCache();
			MapJob = Job;
			Async(EAsyncExecution::ThreadPool, [Job, Cache, Mask]()
			{
				FDmPlanetMaps::Compute(*Cache, Job->In, Mask, Job->R);
				Job->bDone.store(true);
			});
			LastMapCaptureMs = (FPlatformTime::Seconds() - T0) * 1000.0;
		}
	}
}

void ADmPlanet::StartRiverMap()
{
	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const dm::World* W = Sim ? Sim->Sim() : nullptr;
	if (!W || !Terr) return;
	bRiverMapDirty = false;
	const uint32 Hash = Terr->RiverHash ^ (FDmPlanetMaps::RoadHash(*W) * 31u);
	if (Hash == RiverMapHash) return;   // same rivers and roads: nothing to redraw
	RiverMapHash = Hash;
	TSharedPtr<FDmRiverJob, ESPMode::ThreadSafe> Job = MakeShared<FDmRiverJob, ESPMode::ThreadSafe>();
	TSharedPtr<TArray<FDmRoadSeg>, ESPMode::ThreadSafe> Roads = MakeShared<TArray<FDmRoadSeg>, ESPMode::ThreadSafe>();
	FDmPlanetMaps::CaptureRoads(*W, *Roads);
	TSharedPtr<const FDmPlanetTerrain, ESPMode::ThreadSafe> T = Terr;
	RiverJob = Job;
	Async(EAsyncExecution::ThreadPool, [Job, Roads, T]()
	{
		const double T0 = FPlatformTime::Seconds();
		FDmPlanetMaps::BuildRiverMipData(*T, *Roads, Job->Mips, Job->Near);
		Job->Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
		Job->bDone.store(true);
	});
}

void ADmPlanet::ApplyTerrain(const TSharedPtr<FDmPlanetTerrain, ESPMode::ThreadSafe>& NewT)
{
	const double T0 = FPlatformTime::Seconds();
	TArray<int32> Changed, RoadOnly;
	const bool bLocal = Terr.IsValid() && NewT->ChangedCells(*Terr, Changed, &RoadOnly);
	const bool bRiversChanged = !Terr.IsValid() || NewT->RiverHash != Terr->RiverHash;
	if (bLogLod && Terr.IsValid() && Terr->Elev.Num() == NewT->Elev.Num())
	{
		int32 Ce = 0, Cs = 0, Cw = 0, Cr = 0; float MaxDe = 0.f;
		for (int32 C = 0; C < NewT->Elev.Num(); ++C)
		{
			if (Terr->Elev[C] != NewT->Elev[C]) { ++Ce; MaxDe = FMath::Max(MaxDe, FMath::Abs(Terr->Elev[C] - NewT->Elev[C])); }
			Cs += Terr->Slope[C] != NewT->Slope[C]; Cw += Terr->Water[C] != NewT->Water[C]; Cr += Terr->Road[C] != NewT->Road[C];
		}
		UE_LOG(LogDmPlanet, Display, TEXT("DmPlanet: terrain v%d: %s, %d changed + %d road-only cells (elev %d, max %.5f; slope %d; water %d; road %d; sea %.4f -> %.4f; rivers %s)"),
			NewT->Version, bLocal ? TEXT("local") : TEXT("global"), Changed.Num(), RoadOnly.Num(), Ce, MaxDe, Cs, Cw, Cr, Terr->SeaLevel, NewT->SeaLevel, bRiversChanged ? TEXT("changed") : TEXT("same"));
	}
	Terr = NewT;
	++TerrainRebuilds;
	LastTerrainMs = NewT->PrepareMs;
	if (bRiversChanged) bRiverMapDirty = true;
	int32 Stale = 0;
	if (!bLocal || Changed.Num() > 3000)
	{
		for (FDmQNode& N : Nodes) { N.Version = -1; }
		Stale = Nodes.Num();
	}
	else if (Changed.Num() + RoadOnly.Num() > 0)
	{
		// Which built patches does the change reach? Flag the cells within a margin of the changed cells on the
		// grid (bit 0: ground, which smoothing and Loop subdivision spread ~2 rings, plus slope-driven relief:
		// 5 rings; bit 1: roads, whose smoothed segments reach ~2 rings, and only fine patches carry them:
		// 3 rings), one more ring for the extent of a patch, then test each patch by the cell under its centre.
		// Patches wider than a cell are tested by direction. (Testing every patch against every changed cell
		// cost the game thread tens of ms for large changes.)
		const int32 NC = Loc->N;
		const double CellAng = 0.0189 * FMath::Pow(2.0, 6.0 - Loc->L);
		TArray<uint8> Flag; Flag.Init(0, NC);
		TArray<int32> Front, Next;
		auto Spread = [&](const TArray<int32>& Seeds, int32 Rings, uint8 Bit)
		{
			Front.Reset();
			for (int32 C : Seeds) if (!(Flag[C] & Bit)) { Flag[C] |= Bit; Front.Add(C); }
			for (int32 R = 0; R < Rings && Front.Num(); ++R)
			{
				Next.Reset();
				for (int32 C : Front)
					for (int32 K3 = 0; K3 < Loc->NbrN[C]; ++K3)
					{
						const int32 M = Loc->Nbr[6 * C + K3];
						if (M >= 0 && !(Flag[M] & Bit)) { Flag[M] |= Bit; Next.Add(M); }
					}
				Swap(Front, Next);
			}
		};
		Spread(Changed, 6, 1);
		Spread(RoadOnly, 4, 2);
		TArray<FVector3d> GeoDirs, RoadDirs;
		for (int32 C : Changed) GeoDirs.Add(Loc->Pos[C]);
		for (int32 C : RoadOnly) RoadDirs.Add(Loc->Pos[C]);
		for (FDmQNode& N : Nodes)
		{
			if (N.Comp < 0 && !N.bBuilding) continue;
			const double S = FMath::Acos(FMath::Clamp(N.C[0] | N.C[1], -1.0, 1.0)) * FDmPlanetTerrain::DatumM / K;
			const bool bFine = S < 40.0;   // BuildPatch writes roads only into these
			bool bStale = false;
			if (N.Ang > 0.7 * CellAng)
			{
				auto Near = [&N](const TArray<FVector3d>& Dirs, double Margin)
				{
					const double CosLim = FMath::Cos(FMath::Min(N.Ang + Margin, PI));
					for (const FVector3d& D : Dirs) if ((D | N.Ctr) >= CosLim) return true;
					return false;
				};
				bStale = Near(GeoDirs, 5.0 * CellAng) || (bFine && Near(RoadDirs, 3.0 * CellAng));
			}
			else
			{
				if (N.Cell < 0) N.Cell = Loc->NearestCell(N.Ctr);
				bStale = (Flag[N.Cell] & (bFine ? 3 : 1)) != 0;
			}
			if (bStale) { N.Version = -1; ++Stale; }
		}
	}
	// nodes untouched by the change keep their mesh: re-tag them with the new version
	for (FDmQNode& N : Nodes) if (N.Version >= 0) N.Version = NewT->Version;
	StalePatches += Stale;
	LastApplyTerrainMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	UE_LOG(LogDmPlanet, Verbose, TEXT("DmPlanet: terrain v%d applied (%d + %d changed cells, %d stale patches, prepare %.0f ms, apply %.1f ms)"),
		NewT->Version, bLocal ? Changed.Num() : -1, RoadOnly.Num(), Stale, NewT->PrepareMs, LastApplyTerrainMs);
}

// ================================================================================================ queries

double ADmPlanet::GroundHeightM(const FVector& PlanetDir) const
{
	if (!Terr) return 0.0;
	return Terr->Height(Nrm(ToSim(FVector3d(PlanetDir))), 0.5);
}

double ADmPlanet::SeaHeightM() const { return Terr ? Terr->SeaM : 0.0; }

double ADmPlanet::WaterHeightM(const FVector& PlanetDir) const
{
	if (!Terr) return -1e9;
	const FVector3d D = Nrm(ToSim(FVector3d(PlanetDir)));
	FDmSurfAttr A;
	const double G = Terr->Height(D, 0.5, &A);
	if (G < Terr->SeaM) return Terr->SeaM;
	return (A.LakeM > -1e8f && G < A.LakeM) ? double(A.LakeM) : -1e9;
}

double ADmPlanet::SurfaceHeightM(const FVector& PlanetDir) const
{
	return FMath::Max(GroundHeightM(PlanetDir), WaterHeightM(PlanetDir));
}

FVector ADmPlanet::GroundPointWorld(const FVector& PlanetDir) const
{
	return GetActorTransform().TransformPosition(GroundPointPlanet(PlanetDir));
}

float ADmPlanet::ForestAt(const FVector& PlanetDir) const
{
	return Maps.ForestAt(FVector3d(PlanetDir));
}

bool ADmPlanet::RaycastWorld(const FVector& Origin, const FVector& Dir, double MaxDist, FVector& OutHit, bool bWater) const
{
	if (!Terr) return false;
	const FTransform& X = GetActorTransform();
	const FVector3d O(X.InverseTransformPosition(Origin));
	const FVector3d Dv = Nrm(FVector3d(X.InverseTransformVectorNoScale(Dir)));
	const double Top = RadiusCm + (Terr->MaxH + 50.0) * 100.0;
	// enter the shell
	double T = 0.0;
	{
		const double B = O | Dv, Cc = (O | O) - Top * Top;
		if (Cc > 0.0)
		{
			const double Disc = B * B - Cc;
			if (Disc < 0.0 || B > 0.0) return false;
			T = -B - FMath::Sqrt(Disc);
		}
	}
	for (int32 It = 0; It < 600 && T <= MaxDist; ++It)
	{
		const FVector3d P = O + Dv * T;
		const double R = P.Length();
		if (R > Top + 1.0 && (P | Dv) > 0.0) return false;
		const FVector Pd = FVector(P / R);
		const double H = bWater ? SurfaceHeightM(Pd) : GroundHeightM(Pd);
		const double Alt = R - (RadiusCm + H * 100.0);
		if (Alt < 30.0) { OutHit = X.TransformPosition(FVector(P)); return true; }
		T += FMath::Max(Alt * 0.6, 30.0);
	}
	return false;
}

// ================================================================================================ display

void ADmPlanet::SetOverlay(int32 Mode)
{
	Overlay = FMath::Clamp(Mode, 0, 5);
	bOverlayDirty = true;
	LastColorTime = -1e9;
}

void ADmPlanet::ShowSnapshot(const dm::Snapshot* S)
{
	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const dm::World* W = Sim ? Sim->Sim() : nullptr;
	if (!W) return;
	if (S)
	{
		bSnapshot = true;
		Maps.ComputeSnapshot(*W, *S);
		Maps.Upload(this, 0xF);
	}
	else
	{
		bSnapshot = false;
		bColorDirty = true;
		LastColorTime = -1e9;
	}
}

void ADmPlanet::SetDebugMode(int32 Mode)
{
	if (Mode == DebugMode) return;
	DebugMode = Mode;
	for (FDmQNode& N : Nodes) N.Version = -1;   // debug colours are baked into the vertices
}

// ================================================================================================ LOD

void ADmPlanet::SetView(const FVector& CamPos, const FVector& CamFwd, double FovDeg, double Aspect, int32 ScreenW)
{
	const FTransform& X = GetActorTransform();
	CamSim = ToSim(FVector3d(X.InverseTransformPosition(CamPos)));
	FwdSim = Nrm(ToSim(FVector3d(X.InverseTransformVectorNoScale(CamFwd))));
	TanHalfH = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(FovDeg, 1.0, 170.0) * 0.5));
	TanHalfV = TanHalfH / FMath::Max(Aspect, 0.1);
	PxPerRad = (FMath::Max(ScreenW, 64) * 0.5) / TanHalfH;
	bViewSet = true;
}

void ADmPlanet::ReadPlayerView()
{
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->PlayerCameraManager) return;
	const FVector Loc_ = PC->PlayerCameraManager->GetCameraLocation();
	const FRotator Rot = PC->PlayerCameraManager->GetCameraRotation();
	const float Fov = PC->PlayerCameraManager->GetFOVAngle();
	// a camera manager that has not been placed yet sits at the world origin, inside the planet: selecting for
	// it would release every built patch for one frame
	if (Loc_.IsNearlyZero()) return;
	int32 Sx = 1920, Sy = 1080;
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FIntPoint S = GEngine->GameViewport->Viewport->GetSizeXY();
		if (S.X > 0 && S.Y > 0) { Sx = S.X; Sy = S.Y; }
	}
	SetView(Loc_, Rot.Vector(), Fov, double(Sx) / double(Sy), Sx);
	LastCamWorld = Loc_;
}

void ADmPlanet::EnsureChildren(int32 Ni)
{
	if (Nodes[Ni].Child >= 0 || (FreeBlocks.Num() == 0 && Nodes.Num() + 4 > MaxNodes)) return;
	const FVector3d A = Nodes[Ni].C[0], B = Nodes[Ni].C[1], C = Nodes[Ni].C[2];
	const FVector3d AB = Nrm(A + B), BC = Nrm(B + C), CA = Nrm(C + A);
	const FVector3d Corners[4][3] = { { A, AB, CA }, { B, BC, AB }, { C, CA, BC }, { AB, BC, CA } };
	// reuse a pruned block (slots keep their generation counter), else append
	int32 First;
	if (FreeBlocks.Num()) First = FreeBlocks.Pop(EAllowShrinking::No);
	else { First = Nodes.Num(); Nodes.AddDefaulted(4); }
	const int32 Level = Nodes[Ni].Level + 1;
	const float MinH = Nodes[Ni].MinH, MaxH = Nodes[Ni].MaxH;
	for (int32 Ch = 0; Ch < 4; ++Ch)
	{
		FDmQNode& N = Nodes[First + Ch];
		const int32 Gen = N.Gen;
		N = FDmQNode();
		N.Gen = Gen;
		for (int32 K3 = 0; K3 < 3; ++K3) N.C[K3] = Corners[Ch][K3];
		N.Ctr = Nrm(N.C[0] + N.C[1] + N.C[2]);
		N.Ang = FMath::Acos(FMath::Min3(N.Ctr | N.C[0], N.Ctr | N.C[1], N.Ctr | N.C[2]));
		N.Level = Level; N.Parent = Ni; N.MinH = MinH; N.MaxH = MaxH;
		N.LastUsed = GFrameCounter;
	}
	Nodes[Ni].Child = First;
}

bool ADmPlanet::AboveHorizon(const FDmQNode& N) const
{
	const double Dc = CamSim.Length();
	const double SeaCm = RadiusCm + (Terr ? Terr->SeaM : 0.0) * 100.0;
	const double Ro = FMath::Min(SeaCm, RadiusCm + double(N.MinH) * 100.0) - 5000.0;   // the sea (or the patch's own floor) hides what is below it
	if (Dc <= Ro) return true;
	const double Rp = FMath::Max(RadiusCm + double(N.MaxH) * 100.0, Ro);
	const double Theta = FMath::Acos(FMath::Clamp((CamSim / Dc) | N.Ctr, -1.0, 1.0));
	return Theta - N.Ang < FMath::Acos(FMath::Clamp(Ro / Dc, -1.0, 1.0)) + FMath::Acos(FMath::Clamp(Ro / Rp, -1.0, 1.0)) + 0.002;
}

bool ADmPlanet::InFrustum(const FDmQNode& N) const
{
	if (bNoCull) return true;
	const FVector3d Pc = N.Ctr * (RadiusCm + 50.0 * double(N.MinH + N.MaxH));
	const double R = 2.0 * FMath::Sin(N.Ang * 0.5) * RadiusCm + 50.0 * double(N.MaxH - N.MinH) + 1000.0;
	const FVector3d V = Pc - CamSim;
	const double Dist = V.Length();
	if (Dist <= R) return true;
	// ground right around the camera keeps its full detail even out of view: it casts the shadows that land
	// in view, and a coarse patch there sits metres above the fine ground and shades it in flat triangles
	const double AltCm = FMath::Max(CamSim.Length() - (RadiusCm + double(N.MaxH) * 100.0), 0.0);
	if (Dist - R < 1.2 * AltCm + 30000.0) return true;
	const double Alpha = FMath::Acos(FMath::Clamp((V / Dist) | FwdSim, -1.0, 1.0));
	const double Cone = FMath::Atan(FMath::Sqrt(TanHalfH * TanHalfH + TanHalfV * TanHalfV));
	return Alpha - FMath::Asin(FMath::Min(R / Dist, 1.0)) < Cone + FMath::DegreesToRadians(4.0);
}

double ADmPlanet::ErrorPx(const FDmQNode& N) const
{
	const double S = FMath::Acos(FMath::Clamp(N.C[0] | N.C[1], -1.0, 1.0)) * RadiusCm / K; // cm
	const FVector3d Pc = N.Ctr * (RadiusCm + 50.0 * double(N.MinH + N.MaxH));
	const double R = 2.0 * FMath::Sin(N.Ang * 0.5) * RadiusCm + 50.0 * double(N.MaxH - N.MinH);
	const double Dist = FMath::Max(FVector3d::Distance(Pc, CamSim) - R, 100.0);
	return S / Dist * PxPerRad;
}

void ADmPlanet::AddBuiltDescendants(int32 Ni)
{
	const FDmQNode& N = Nodes[Ni];
	if (N.Comp >= 0) { Draw.Add(Ni); return; }
	if (N.Child >= 0) for (int32 Ch = 0; Ch < 4; ++Ch) AddBuiltDescendants(N.Child + Ch);
}

void ADmPlanet::MarkKeep(int32 Ni)
{
	const FDmQNode& N = Nodes[Ni];
	if (N.Comp >= 0) { Keep.Add(Ni); return; }
	if (N.Child >= 0) for (int32 Ch = 0; Ch < 4; ++Ch) MarkKeep(N.Child + Ch);
}

bool ADmPlanet::Ready(int32 Ni) const
{
	const FDmQNode& N = Nodes[Ni];
	if (N.Comp >= 0 || !AboveHorizon(N)) return true;
	if (N.Child < 0) return false;
	for (int32 Ch = 0; Ch < 4; ++Ch) if (!Ready(N.Child + Ch)) return false;
	return true;
}

void ADmPlanet::Visit(int32 Ni)
{
	if (!AboveHorizon(Nodes[Ni])) return;
	Nodes[Ni].LastUsed = GFrameCounter;
	// Hysteresis: a split patch merges back only when its error drops well below the split threshold,
	// so LOD borders do not flicker (each flip re-stitches up to three neighbours).
	const double Thr = Nodes[Ni].bSplit ? TargetPx * 0.7 : TargetPx;
	const bool bSplit = Nodes[Ni].Level < MaxLevel && InFrustum(Nodes[Ni]) && ErrorPx(Nodes[Ni]) > Thr && (Nodes[Ni].Child >= 0 || FreeBlocks.Num() > 0 || Nodes.Num() + 4 <= MaxNodes);
	Nodes[Ni].bSplit = bSplit;
	if (bSplit)
	{
		EnsureChildren(Ni);
		const int32 C0 = Nodes[Ni].Child;
		bool bReady = true;
		for (int32 Ch = 0; Ch < 4; ++Ch)
			if (!Ready(C0 + Ch)) { bReady = false; if (!Nodes[C0 + Ch].bBuilding && Nodes[C0 + Ch].Comp < 0) Need.AddUnique(C0 + Ch); }
		if (bReady) { for (int32 Ch = 0; Ch < 4; ++Ch) Visit(C0 + Ch); return; }
		// not all children are ready: keep the ones already built (hidden) so they are not released and
		// rebuilt over and over while their siblings stream in
		for (int32 Ch = 0; Ch < 4; ++Ch) MarkKeep(C0 + Ch);
		if (Nodes[Ni].Comp >= 0) { Draw.Add(Ni); return; }
	}
	if (Nodes[Ni].Comp >= 0) { Draw.Add(Ni); return; }
	if (!Nodes[Ni].bBuilding) Need.AddUnique(Ni);
	AddBuiltDescendants(Ni);
}

void ADmPlanet::Select()
{
	Draw.Reset(); Need.Reset(); Keep.Reset();
	for (int32 F = 0; F < 20 && F < Nodes.Num(); ++F) Visit(F);
	DrawSet.Reset();
	DrawSet.Append(Draw);
	const int32 Ver = Terr ? Terr->Version : 0;
	// coarser neighbour across each edge of every drawn patch; a change means a re-stitch
	for (int32 Ni : Draw)
	{
		FDmQNode& N = Nodes[Ni];
		for (int32 E = 0; E < 3; ++E)
		{
			const FVector3d M = Nrm(N.C[E] + N.C[(E + 1) % 3]);
			const int32 Nb = DrawnAt(Nrm(M + (M - N.Ctr) * 0.05));
			N.WantNb[E] = (Nb >= 0 && Nodes[Nb].Level < N.Level) ? Nb : -1;
		}
		bool bRestitch = N.Version != Ver;
		for (int32 E = 0; E < 3; ++E)
			bRestitch |= N.WantNb[E] != N.BuiltNb[E] || (N.WantNb[E] >= 0 && Nodes[N.WantNb[E]].Gen != N.BuiltGen[E]);
		if (!N.bBuilding && bRestitch) Need.AddUnique(Ni);
	}
	for (int32 Ni = 0; Ni < Nodes.Num(); ++Ni)
	{
		FDmQNode& N = Nodes[Ni];
		if (N.Comp < 0) continue;
		const bool bDraw = DrawSet.Contains(Ni);
		if (!bDraw && Ni >= 20 && !Keep.Contains(Ni)) Release(Ni);
		else Comps[N.Comp]->SetVisibility(bDraw);
	}
	if (bLogLod && Draw.Num() * 2 < DrawPatches)
		UE_LOG(LogDmPlanet, Display, TEXT("DmPlanet: selection dropped from %d to %d patches (frame %llu, camera %.0f m from the centre, %d nodes, %d free)"),
			DrawPatches, Draw.Num(), GFrameCounter, CamSim.Length() / 100.0, Nodes.Num(), FreeBlocks.Num() * 4);
	DrawPatches = Draw.Num(); DrawLandTris = 0; DrawWaterTris = 0; MaxDrawLevel = 0;
	FMemory::Memzero(LevelCount, sizeof(LevelCount));
	for (int32 Ni : Draw)
	{
		DrawLandTris += Nodes[Ni].LandTris; DrawWaterTris += Nodes[Ni].WaterTris;
		MaxDrawLevel = FMath::Max(MaxDrawLevel, Nodes[Ni].Level);
		LevelCount[FMath::Min(Nodes[Ni].Level, 19)]++;
	}
}

void ADmPlanet::Prune()
{
	// The quadtree only ever grew: every place the camera had visited kept its nodes, and Step / Select walk all
	// of them each frame (and splitting stopped for good at MaxNodes). Give back the 4-blocks of children that
	// are idle leaves (no mesh, nothing building) not reached by the selection for a few seconds, deepest
	// first. Slots are reused by EnsureChildren; their generation counter keeps old stitches from matching.
	const uint64 Frame = GFrameCounter;
	constexpr uint64 PruneIdleFrames = 240;
	for (int32 Pass = 0; Pass < 20; ++Pass)
	{
		bool bAny = false;
		for (int32 Ni = 0; Ni < Nodes.Num(); ++Ni)
		{
			FDmQNode& N = Nodes[Ni];
			if (N.bFree || N.Child < 0) continue;
			// the children of a node that is split right now stay, even when they are all beyond the horizon (they
			// count as ready there): re-created, they would inherit the parent's height range, count as visible
			// and not ready, and the selection would fall back to the roots for a frame
			if (N.bSplit && Frame - N.LastUsed <= PruneIdleFrames) continue;
			bool bIdle = true;
			for (int32 Ch = 0; Ch < 4 && bIdle; ++Ch)
			{
				const FDmQNode& C = Nodes[N.Child + Ch];
				bIdle = C.Child < 0 && C.Comp < 0 && !C.bBuilding && !C.Pending.IsValid() && Frame - C.LastUsed > PruneIdleFrames;
			}
			if (!bIdle) continue;
			if (bLogLod && Pass == 0 && NodesFreed < 64)
				UE_LOG(LogDmPlanet, Display, TEXT("DmPlanet: prune under node %d (level %d, comp %d, split %d, building %d, last used %llu frames ago, above horizon %d)"),
					Ni, N.Level, N.Comp, N.bSplit ? 1 : 0, N.bBuilding ? 1 : 0, Frame - N.LastUsed, AboveHorizon(N) ? 1 : 0);
			const int32 First = N.Child;
			N.Child = -1;
			N.bSplit = false;
			for (int32 Ch = 0; Ch < 4; ++Ch)
			{
				FDmQNode& C = Nodes[First + Ch];
				const int32 Gen = C.Gen + 1;
				C = FDmQNode();
				C.Gen = Gen;
				C.bFree = true;
			}
			FreeBlocks.Add(First);
			NodesFreed += 4;
			bAny = true;
		}
		if (!bAny) break;
	}
}

void ADmPlanet::Release(int32 Ni)
{
	FDmQNode& N = Nodes[Ni];
	if (N.Comp < 0) return;
	UProceduralMeshComponent* C = Comps[N.Comp];
	C->ClearAllMeshSections();
	C->SetVisibility(false);
	FreeComps.Add(N.Comp);
	N.Comp = -1;
	N.Version = -1;
}

void ADmPlanet::Apply(int32 Ni, FDmPatchData& D)
{
	FDmQNode& N = Nodes[Ni];
	N.MinH = D.MinH; N.MaxH = D.MaxH; N.LandTris = D.LandTris; N.WaterTris = D.WaterTris;
	for (int32 E = 0; E < 3; ++E) { N.BuiltNb[E] = D.BuiltNb[E]; N.BuiltGen[E] = D.BuiltGen[E]; }
	N.Version = D.Version;
	BuildMsTotal += D.Ms; ++BuildsTotal;
	int32 Ci;
	if (N.Comp >= 0) Ci = N.Comp;   // rebuild in place (new stitching or new terrain)
	else if (FreeComps.Num()) Ci = FreeComps.Pop();
	else
	{
		UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(this);
		C->SetupAttachment(RootComponent);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(true);
		C->bUseAsyncCooking = true;
		C->SetAffectDistanceFieldLighting(false);
		C->bCastDynamicShadow = true;
		C->RegisterComponent();
		Ci = Comps.Add(C);
	}
	UProceduralMeshComponent* C = Comps[Ci];
	C->SetRelativeLocation(D.Origin);
	if (D.I.Num()) { C->CreateMeshSection_LinearColor(0, D.V, D.I, D.Nn, D.U0, D.U1, D.U2, D.U3, D.Col, {}, false, false); C->SetMaterial(0, D.bSeabed && SeabedM ? SeabedM.Get() : LandMid.Get()); }
	else C->ClearMeshSection(0);
	if (D.WI.Num())
	{
		C->CreateMeshSection_LinearColor(1, D.WV, D.WI, D.WN, D.WU0, D.WU1, {}, {}, {}, {}, false, false);
		C->SetMaterial(1, WaterMid);
	}
	else C->ClearMeshSection(1);
	C->SetVisibility(true);
	N.Comp = Ci;
}

FVector3d ADmPlanet::GridDir(const FVector3d C[3], int32 K, int32 I, int32 J)
{
	// One formula for every patch: shared edges evaluate bit-identical directions (k / K is exact for
	// K a power of two and a zero-weighted corner contributes an exact zero).
	const double Inv = 1.0 / K;
	return Nrm(C[0] * (double(K - I - J) * Inv) + C[1] * (double(I) * Inv) + C[2] * (double(J) * Inv));
}

FVector3d ADmPlanet::NormalAt(const FDmPlanetTerrain& T, const FVector3d& D, double H0, double S, bool bRiv)
{
	// Forward differences in a fixed tangent frame: a pure function of (direction, spacing), so
	// neighbouring patches of one level agree on shared edges.
	const double Dlt = FMath::Max(0.5 * S, 0.25) / DatumM;
	const FVector3d Ax = FMath::Abs(D.Y) < 0.9 ? FVector3d(0, 1, 0) : FVector3d(1, 0, 0);
	const FVector3d E1 = Nrm(Ax ^ D), E2 = D ^ E1;
	const double H1 = T.Height(Nrm(D + E1 * Dlt), S, nullptr, bRiv);
	const double H2 = T.Height(Nrm(D + E2 * Dlt), S, nullptr, bRiv);
	const double Step = Dlt * DatumM;
	return Nrm(D - E1 * ((H1 - H0) / Step) - E2 * ((H2 - H0) / Step));
}

void ADmPlanet::BuildPatch(const FDmPlanetTerrain& T, const FDmQNode& N, int32 K, const FDmStitch St[3], int32 Debug, FDmPatchData& O)
{
	const double T0 = FPlatformTime::Seconds();
	const double S = FMath::Acos(FMath::Clamp(N.C[0] | N.C[1], -1.0, 1.0)) * DatumM / K; // vertex spacing, m
	const bool bRiv = T.RiversNear(N.Ctr, N.Ang + 0.01);
	// roads per vertex only where a vertex spacing can carry them (the material uses the road map beyond)
	const bool bRoad = S < 40.0 && T.RoadsNear(N.Ctr, N.Ang + 0.01);
	const int32 NV = (K + 1) * (K + 2) / 2;
	auto Idx = [K](int32 I, int32 J) { return I * (K + 1) - I * (I - 1) / 2 + J; };
	TArray<FVector3d> Dir; Dir.SetNumUninitialized(NV);
	TArray<FVector3d> PosA; PosA.SetNumUninitialized(NV);   // absolute land position, sim frame, cm
	TArray<FVector3d> WPosA; WPosA.SetNumUninitialized(NV);  // absolute sea position
	TArray<double> H; H.SetNumUninitialized(NV);
	TArray<FVector3d> Nm; Nm.SetNumUninitialized(NV);
	TArray<FDmSurfAttr> At; At.SetNum(NV);
	TArray<FVector2f> RoadV; RoadV.Init(FVector2f(1e9f, 0.f), NV);
	const double SeaCm = RadiusCm + T.SeaM * 100.0;
	double MinH = 1e9, MaxH = -1e9;
	for (int32 I = 0; I <= K; ++I)
		for (int32 J = 0; I + J <= K; ++J)
		{
			const int32 V = Idx(I, J);
			const FVector3d D = GridDir(N.C, K, I, J);
			Dir[V] = D;
			const double H0 = T.Height(D, S, &At[V], bRiv);
			H[V] = H0;
			MinH = FMath::Min(MinH, H0); MaxH = FMath::Max(MaxH, H0);
			Nm[V] = NormalAt(T, D, H0, S, bRiv);
			PosA[V] = D * (RadiusCm + H0 * 100.0);
			WPosA[V] = D * SeaCm;
			if (bRoad) { double Sd, Lv; T.RoadQuery(D, Sd, Lv); RoadV[V] = FVector2f(float(Sd), float(Lv)); }
		}
	O.MinH = float(MinH); O.MaxH = float(MaxH);
	const FVector3d OriginSim = N.Ctr * RadiusCm;
	O.Origin = ToUE(OriginSim);

	// Edge loops (patch boundary, in order): 0 = C0->C1, 1 = C1->C2, 2 = C2->C0
	TArray<int32> Edge[3];
	for (int32 I = 0; I <= K; ++I) Edge[0].Add(Idx(I, 0));
	for (int32 J = 0; J <= K; ++J) Edge[1].Add(Idx(K - J, J));
	for (int32 J = K; J >= 0; --J) Edge[2].Add(Idx(0, J));

	// Stitching: along an edge that borders a coarser patch, move our edge vertices onto that patch's
	// own edge (the straight segments between its vertices, with its heights). Exactly crack-free.
	for (int32 E = 0; E < 3; ++E)
	{
		if (!St[E].bOn) continue;
		const FVector3d* CC = St[E].CC;
		const double Sc = St[E].Sc;
		const bool bRivC = T.RiversNear(Nrm(CC[0] + CC[1] + CC[2]), FMath::Acos(FMath::Clamp(CC[0] | CC[1], -1.0, 1.0)) + 0.01);
		const FVector3d EA = Dir[Edge[E][0]], EB = Dir[Edge[E][K]];
		int32 Ce = 0; double BestErr = 1e30;
		for (int32 Q = 0; Q < 3; ++Q)
		{
			const FVector3d Pn = Nrm(CC[Q] ^ CC[(Q + 1) % 3]);
			const double Err = FMath::Abs(EA | Pn) + FMath::Abs(EB | Pn);
			if (Err < BestErr) { BestErr = Err; Ce = Q; }
		}
		const FVector3d P = CC[Ce], Q2 = CC[(Ce + 1) % 3];
		auto CoarseIJ = [K, Ce](int32 Kk, int32& I, int32& J)
		{
			if (Ce == 0) { I = Kk; J = 0; }
			else if (Ce == 1) { I = K - Kk; J = Kk; }
			else { I = 0; J = K - Kk; }
		};
		int32 CachedK = -1; FVector3d CP[2], CN[2], CW[2];
		for (int32 Q = 0; Q <= K; ++Q)
		{
			const int32 V = Edge[E][Q];
			const FVector3d D = Dir[V];
			const double A = (D ^ P).Length(), B = (D ^ Q2).Length();
			const double X = A / FMath::Max(A + B, 1e-30) * K;
			const int32 K0 = FMath::Clamp(FMath::FloorToInt(X), 0, K - 1);
			const double F = FMath::Clamp(X - K0, 0.0, 1.0);
			if (K0 != CachedK)
			{
				for (int32 Z = 0; Z < 2; ++Z)
				{
					int32 I, J; CoarseIJ(K0 + Z, I, J);
					const FVector3d Dc = GridDir(CC, K, I, J);
					const double Hc = T.Height(Dc, Sc, nullptr, bRivC);
					CP[Z] = Dc * (RadiusCm + Hc * 100.0);
					CN[Z] = NormalAt(T, Dc, Hc, Sc, bRivC);
					CW[Z] = Dc * SeaCm;
				}
				CachedK = K0;
			}
			PosA[V] = CP[0] + (CP[1] - CP[0]) * F;
			WPosA[V] = CW[0] + (CW[1] - CW[0]) * F;
			Nm[V] = Nrm(CN[0] + (CN[1] - CN[0]) * F);
		}
	}

	// Small safety skirts (float rounding, corners shared with a same-level patch, async gaps).
	const double SkirtCm = (0.2 * S + 0.5) * 100.0;

	// ---- land
	const int32 NSk = 3 * (K + 1);
	O.V.SetNumUninitialized(NV + NSk); O.Nn.SetNumUninitialized(NV + NSk);
	O.U0.SetNumUninitialized(NV + NSk); O.U1.SetNumUninitialized(NV + NSk); O.U2.SetNumUninitialized(NV + NSk); O.U3.SetNumUninitialized(NV + NSk);
	O.Col.SetNumUninitialized(NV + NSk);
	const float LogS = float(FMath::Log2(S));
	FLinearColor DebugCol(0, 0, 0, 0);
	if (Debug == 3) DebugCol = FLinearColor(FMath::Frac(N.Level * 0.37f), FMath::Frac(N.Level * 0.61f + 0.3f), FMath::Frac(N.Level * 0.17f + 0.6f), 1.f);
	if (Debug == 4)
	{
		const uint32 Hh = DmPlanetNoise::Hash(int32(N.Ctr.X * 1e6), int32(N.Ctr.Y * 1e6), int32(N.Ctr.Z * 1e6));
		DebugCol = FLinearColor((Hh & 255) / 255.f, ((Hh >> 8) & 255) / 255.f, ((Hh >> 16) & 255) / 255.f, 1.f);
	}
	auto Put = [&](int32 Dst, int32 Src, double DropCm)
	{
		O.V[Dst] = ToUE(PosA[Src] - Dir[Src] * DropCm - OriginSim);
		O.Nn[Dst] = ToUE(Nm[Src]);
		const FDmSurfAttr& A = At[Src];
		// Attribute layout shared with Scripts/planet/land.hlsl. PMC gives 4 half-float UV pairs and one
		// 8-bit colour; never pack two fields into one interpolated channel (lab pitfall).
		O.U0[Dst] = FVector2D(A.BeachZone, A.DH);
		O.U1[Dst] = FVector2D(A.ReliefE, A.ReliefR);
		O.U2[Dst] = FVector2D(LogS, FMath::Clamp(A.AboveLake, -1000.f, 9000.f));
		O.U3[Dst] = FVector2D(A.RiverD, A.RiverW);
		// vertex colour: R = signed road distance (+-60 m), G = road level / 4, A = 0; debug colours set A = 1
		if (Debug == 3 || Debug == 4) O.Col[Dst] = DebugCol;
		else O.Col[Dst] = FLinearColor(FMath::Clamp(0.5f + RoadV[Src].X / 120.f, 0.f, 1.f), FMath::Clamp(RoadV[Src].Y / 4.f, 0.f, 1.f), 0.f, 0.f);
	};
	for (int32 V = 0; V < NV; ++V) Put(V, V, 0.0);
	for (int32 E = 0; E < 3; ++E)
		for (int32 Q = 0; Q <= K; ++Q)
		{
			Put(NV + E * (K + 1) + Q, Edge[E][Q], Debug == 2 ? 0.0 : SkirtCm);
			if (Debug == 1) O.Col[NV + E * (K + 1) + Q] = FLinearColor(1, 0, 1, 1);
		}
	auto Tri = [](TArray<int32>& Out, int32 A, int32 B, int32 C)
	{
		if (bFlipWinding) Out.Append({ A, C, B }); else Out.Append({ A, B, C });
	};
	bool bAnyLake = false;
	for (int32 V = 0; V < NV && !bAnyLake; ++V) bAnyLake = At[V].LakeM > -1e8f;
	// Open ocean: the sea floor under 150 m of water is never seen (the water absorbs it), but the water needs
	// it behind (Single Layer Water integrates absorption up to the scene depth; without a floor, deep patches
	// shade differently and their triangles show). So a deep patch keeps a coarse floor (every 4th grid line)
	// with a cheap material, and water far from any shore uses every 4th grid line too.
	const bool bDeepSea = !bAnyLake && MaxH < T.SeaM - 150.0 && Debug == 0;
	const bool bOpenSea = !bAnyLake && MaxH < T.SeaM - 30.0;
	O.bSeabed = bDeepSea;
	O.I.Reserve(K * K * 3 + 3 * K * 12);
	if (bDeepSea)
		for (int32 I = 0; I < K; I += 4)
			for (int32 J = 0; I + J < K; J += 4)
			{
				Tri(O.I, Idx(I, J), Idx(I + 4, J), Idx(I, J + 4));
				if (I + J < K - 4) Tri(O.I, Idx(I + 4, J), Idx(I + 4, J + 4), Idx(I, J + 4));
			}
	for (int32 I = 0; I < K && !bDeepSea; ++I)
		for (int32 J = 0; I + J < K; ++J)
		{
			Tri(O.I, Idx(I, J), Idx(I + 1, J), Idx(I, J + 1));
			if (I + J < K - 1) Tri(O.I, Idx(I + 1, J), Idx(I + 1, J + 1), Idx(I, J + 1));
		}
	for (int32 E = 0; E < 3 && !bDeepSea; ++E)
		for (int32 Q = 0; Q < K; ++Q)
		{
			const int32 A = Edge[E][Q], B = Edge[E][Q + 1], As = NV + E * (K + 1) + Q, Bs = As + 1;
			Tri(O.I, A, As, B); Tri(O.I, B, As, Bs);   // both windings: the skirt is seen from either side
			Tri(O.I, A, B, As); Tri(O.I, B, Bs, As);
		}
	O.LandTris = O.I.Num() / 3;

	// ---- water: the sea where the ground dips below (or near) sea level, lakes at their own level.
	// The depth the water material sees (foam, surf, shallows) uses ground heights averaged over the grid
	// neighbours, so the foam line follows the coast instead of the vertex-scale noise of the triangles.
	TArray<double> Hs; Hs.SetNumUninitialized(NV);
	for (int32 I = 0; I <= K; ++I)
		for (int32 J = 0; I + J <= K; ++J)
		{
			double Sum = 2.0 * H[Idx(I, J)]; double Wsum = 2.0;
			const int32 Nb[6][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, -1 }, { -1, 1 } };
			for (const auto& D : Nb)
			{
				const int32 Ii = I + D[0], Jj = J + D[1];
				if (Ii < 0 || Jj < 0 || Ii + Jj > K) continue;
				Sum += H[Idx(Ii, Jj)]; Wsum += 1.0;
			}
			Hs[Idx(I, J)] = Sum / Wsum;
		}
	if (MinH < T.SeaM + 2.0 || bAnyLake)
	{
		TMap<uint64, int32> Map;
		auto WVert = [&](int32 Src, int32 Kind, double LevelM) -> int32
		{
			const uint64 Key = (uint64(uint32(Src)) << 32) | uint64(uint32(Kind == 0 ? 0 : 1 + FMath::Clamp(FMath::RoundToInt((LevelM + 20000.0) * 4.0), 0, 0x7FFFFFF0)));
			if (const int32* F = Map.Find(Key)) return *F;
			const int32 Id = O.WV.Num();
			const FVector3d P = Kind == 0 ? WPosA[Src] : Nrm(PosA[Src]) * (RadiusCm + LevelM * 100.0);
			O.WV.Add(ToUE(P - OriginSim));
			O.WN.Add(ToUE(Dir[Src]));
			O.WU0.Add(FVector2D(float(FMath::Clamp(LevelM - Hs[Src], -1000.0, 9000.0)), At[Src].RiverD));
			O.WU1.Add(FVector2D(LogS, Kind == 0 ? 0.0 : 1.0));
			Map.Add(Key, Id);
			return Id;
		};
		auto WaterTri = [&](int32 A, int32 B, int32 C)
		{
			const double Hmin = FMath::Min3(H[A], H[B], H[C]);
			if (Hmin < T.SeaM + 1.0) { Tri(O.WI, WVert(A, 0, T.SeaM), WVert(B, 0, T.SeaM), WVert(C, 0, T.SeaM)); return; }
			const double Lv = FMath::Max3(At[A].LakeM, At[B].LakeM, At[C].LakeM);
			if (Lv > -1e8 && Hmin < Lv + 1.0) Tri(O.WI, WVert(A, 1, Lv), WVert(B, 1, Lv), WVert(C, 1, Lv));
		};
		const int32 Dw = bOpenSea ? 4 : 1;   // grid step of the water triangles
		for (int32 I = 0; I < K; I += Dw)
			for (int32 J = 0; I + J < K; J += Dw)
			{
				WaterTri(Idx(I, J), Idx(I + Dw, J), Idx(I, J + Dw));
				if (I + J < K - Dw) WaterTri(Idx(I + Dw, J), Idx(I + Dw, J + Dw), Idx(I, J + Dw));
			}
		// skirts along the sea's patch edges (chord sag and level changes between patches)
		for (int32 E = 0; E < 3; ++E)
			for (int32 Q = 0; Q < K; Q += Dw)
			{
				const int32 A = Edge[E][Q], B = Edge[E][Q + Dw];
				if (FMath::Min(H[A], H[B]) >= T.SeaM + 1.0) continue;
				const int32 Wa = WVert(A, 0, T.SeaM), Wb = WVert(B, 0, T.SeaM);
				const int32 Sa = O.WV.Num();
				for (int32 Src : { A, B })
				{
					O.WV.Add(ToUE(WPosA[Src] - Dir[Src] * SkirtCm - OriginSim));
					O.WN.Add(ToUE(Dir[Src]));
					O.WU0.Add(FVector2D(float(FMath::Clamp(T.SeaM - Hs[Src], -1000.0, 9000.0)), At[Src].RiverD));
					O.WU1.Add(FVector2D(LogS, 0.0));
				}
				Tri(O.WI, Wa, Sa, Wb); Tri(O.WI, Wb, Sa, Sa + 1);
				Tri(O.WI, Wa, Wb, Sa); Tri(O.WI, Wb, Sa + 1, Sa);
			}
		O.WaterTris = O.WI.Num() / 3;
		O.MinH = FMath::Min(O.MinH, float(T.SeaM));
	}
	for (int32 E = 0; E < 3; ++E) { O.BuiltNb[E] = St[E].Node; O.BuiltGen[E] = St[E].Gen; }
	O.Version = T.Version;
	O.Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
}

bool ADmPlanet::Contains(const FDmQNode& N, const FVector3d& P)
{
	const double Eps = -1e-13;
	return (P | (N.C[0] ^ N.C[1])) >= Eps && (P | (N.C[1] ^ N.C[2])) >= Eps && (P | (N.C[2] ^ N.C[0])) >= Eps;
}

int32 ADmPlanet::DrawnAt(const FVector3d& P) const
{
	int32 Ni = -1;
	for (int32 F = 0; F < 20 && F < Nodes.Num(); ++F) if (Contains(Nodes[F], P)) { Ni = F; break; }
	while (Ni >= 0)
	{
		if (DrawSet.Contains(Ni)) return Ni;
		const FDmQNode& N = Nodes[Ni];
		if (N.Child < 0) return -1;
		int32 Next = -1;
		for (int32 Ch = 0; Ch < 4; ++Ch) if (Contains(Nodes[N.Child + Ch], P)) { Next = N.Child + Ch; break; }
		Ni = Next;
	}
	return -1;
}

void ADmPlanet::MakeStitch(int32 Ni, FDmStitch Out[3]) const
{
	for (int32 E = 0; E < 3; ++E)
	{
		Out[E] = FDmStitch();
		const int32 Nb = Nodes[Ni].WantNb[E];
		if (Nb < 0) continue;
		Out[E].bOn = true; Out[E].Node = Nb; Out[E].Gen = Nodes[Nb].Gen;
		for (int32 K3 = 0; K3 < 3; ++K3) Out[E].CC[K3] = Nodes[Nb].C[K3];
		Out[E].Sc = FMath::Acos(FMath::Clamp(Nodes[Nb].C[0] | Nodes[Nb].C[1], -1.0, 1.0)) * DatumM / K;
	}
}

void ADmPlanet::Settle(int32 MaxIters)
{
	if (!Terr || Nodes.Num() < 20) return;
	const double T0 = FPlatformTime::Seconds();
	// finish what is in flight first
	for (FDmQNode& N : Nodes)
		if (N.bBuilding && N.Pending.IsValid())
		{
			while (!N.Pending->bDone.load()) FPlatformProcess::Sleep(0.0005f);
		}
	for (int32 Ni = 0; Ni < Nodes.Num(); ++Ni)
	{
		FDmQNode& N = Nodes[Ni];
		if (N.bBuilding && N.Pending.IsValid()) { Apply(Ni, N.Pending->D); N.Pending.Reset(); N.bBuilding = false; }
	}
	const FDmPlanetTerrain& T = *Terr;
	for (int32 It = 0; It < MaxIters; ++It)
	{
		Select();
		if (Need.Num() == 0) break;
		TArray<int32> Work = Need;
		TArray<FDmPatchData> Out; Out.SetNum(Work.Num());
		TArray<FDmStitch> Sts; Sts.SetNum(Work.Num() * 3);
		for (int32 Wi = 0; Wi < Work.Num(); ++Wi) MakeStitch(Work[Wi], &Sts[Wi * 3]);
		const int32 Dbg = DebugMode;
		ParallelFor(Work.Num(), [&](int32 Wi) { BuildPatch(T, Nodes[Work[Wi]], K, &Sts[Wi * 3], Dbg, Out[Wi]); });
		for (int32 Wi = 0; Wi < Work.Num(); ++Wi) Apply(Work[Wi], Out[Wi]);
	}
	Select();
	LastSettleMs = (FPlatformTime::Seconds() - T0) * 1000.0;
}

bool ADmPlanet::IsSettled() const
{
	if (TerrainJob.IsValid() || RiverJob.IsValid() || MapJob.IsValid() || bTerrainDirty) return false;
	// materials compile asynchronously in -game: until they are done the default material is drawn
	if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0) return false;
	if (Need.Num() > 0) return false;
	for (const FDmQNode& N : Nodes) if (N.bBuilding) return false;
	return true;
}

void ADmPlanet::Step()
{
	// apply finished async builds (a bounded number per frame: each one re-creates a render proxy)
	int32 Applied = 0;
	InFlight = 0;
	for (int32 Ni = 0; Ni < Nodes.Num(); ++Ni)
	{
		FDmQNode& N = Nodes[Ni];
		if (!N.bBuilding || !N.Pending.IsValid()) continue;
		if (Applied < MaxAppliesPerFrame && N.Pending->bDone.load())
		{
			Apply(Ni, N.Pending->D);
			N.Pending.Reset();
			N.bBuilding = false;
			++Applied;
		}
		else ++InFlight;
	}
	if (bFreeze || !Terr) return;
	Select();
	if ((GFrameCounter % 60) == 0) Prune();
	// start new builds: first-time builds (they unblock splits) before re-stitches and stale patches, nearest first
	Need.Sort([this](int32 A, int32 B)
	{
		const bool NewA = Nodes[A].Comp < 0, NewB = Nodes[B].Comp < 0;
		if (NewA != NewB) return NewA;
		return ErrorPx(Nodes[A]) > ErrorPx(Nodes[B]);
	});
	const int32 Budget = FMath::Max(0, MaxStartsPerFrame - InFlight);
	TSharedPtr<const FDmPlanetTerrain, ESPMode::ThreadSafe> TP = Terr;
	const int32 Dbg = DebugMode;
	for (int32 Wi = 0; Wi < Need.Num() && Wi < Budget; ++Wi)
	{
		const int32 Ni = Need[Wi];
		FDmQNode& N = Nodes[Ni];
		N.bBuilding = true;
		TSharedPtr<FDmPendingPatch, ESPMode::ThreadSafe> P = MakeShared<FDmPendingPatch, ESPMode::ThreadSafe>();
		N.Pending = P;
		FDmQNode Copy;
		for (int32 K3 = 0; K3 < 3; ++K3) Copy.C[K3] = N.C[K3];
		Copy.Ctr = N.Ctr; Copy.Ang = N.Ang; Copy.Level = N.Level;
		FDmStitch St[3]; MakeStitch(Ni, St);
		const FDmStitch S0 = St[0], S1 = St[1], S2 = St[2];
		const int32 KK = K;
		Async(EAsyncExecution::ThreadPool, [TP, Copy, KK, P, S0, S1, S2, Dbg]()
		{
			const FDmStitch Sx[3] = { S0, S1, S2 };
			BuildPatch(*TP, Copy, KK, Sx, Dbg, P->D);
			P->bDone.store(true);
		});
		++InFlight;
	}
}

void ADmPlanet::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	CheckMaterials();
	if (UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this)) SetActorTransform(Sim->GetPlanetToWorld());
	{
		const double T0 = FPlatformTime::Seconds();
		PumpData();
		PumpHist[PumpHistAt] = float((FPlatformTime::Seconds() - T0) * 1000.0);
		PumpHistAt = (PumpHistAt + 1) % 120;
	}
	UpdateMaterialParams();
	if (!bViewOverride) ReadPlayerView();
	bViewOverride = false;
	if (bViewSet) Step();
	if (bLogLod && (GFrameCounter % 60) == 0)
		UE_LOG(LogDmPlanet, Display, TEXT("DmPlanet: frame %llu cam %s alt %.0f m need %d | %s"), GFrameCounter, *LastCamWorld.ToString(), (CamSim.Length() - RadiusCm) / 100.0, Need.Num(), *StatsLine());
}

double ADmPlanet::PumpMsMax() const
{
	float M = 0.f;
	for (float V : PumpHist) M = FMath::Max(M, V);
	return M;
}

FString ADmPlanet::StatsLine() const
{
	FString Lv;
	for (int32 L = 0; L < 20; ++L) if (LevelCount[L]) Lv += FString::Printf(TEXT(" L%d:%d"), L, LevelCount[L]);
	return FString::Printf(TEXT("patches %d (max level %d) land tris %d water tris %d total %d | nodes %d (%d free, %d freed) comps %d builds %d (%.1f ms avg/patch/thread) in flight %d | terrain v%d rebuilds %d (%.0f ms, apply %.1f ms) stale %d | maps %d updates (worker %.1f ms, capture %.1f ms, upload %.1f ms) river map %.0f ms | pump max %.1f ms |%s"),
		DrawPatches, MaxDrawLevel, DrawLandTris, DrawWaterTris, DrawLandTris + DrawWaterTris, Nodes.Num(), FreeBlocks.Num() * 4, NodesFreed, Comps.Num(), BuildsTotal,
		BuildsTotal ? BuildMsTotal / BuildsTotal : 0.0, InFlight, Terr ? Terr->Version : -1, TerrainRebuilds, LastTerrainMs, LastApplyTerrainMs, StalePatches,
		MapUpdates, LastMapMs, LastMapCaptureMs, LastMapApplyMs, LastRiverMapMs, PumpMsMax(), *Lv);
}

static FDmScenePartRegistrar GDmPlanetPart(TEXT("planet"), EDmPartKind::World, 0,
	[](const FDmSceneContext& S) -> AActor*
	{
		return S.World ? S.World->SpawnActor<ADmPlanet>() : nullptr;
	}, TEXT("Planet/DmPlanet (C1)"));
