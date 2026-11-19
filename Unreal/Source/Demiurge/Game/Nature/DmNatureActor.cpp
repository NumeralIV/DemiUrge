// DEMIURGE — Game/Nature/DmNatureActor.cpp : see the header.

#include "Game/Nature/DmNatureActor.h"
#include "Game/Nature/DmNatureFactory.h"
#include "Game/Nature/DmNatureFauna.h"
#include "Game/Nature/DmNatureTrees.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmScene.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Game/Core/Placeholder/DmPlaceholderPlanet.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/DecalComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Sim/State.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmNature, Log, All);

namespace
{
	struct FLevelSpec { float Keep; bool bShrubs; int32 Variants; };
	const FLevelSpec kTreeLevels[4] = { { 1.f, true, 4 }, { 0.5f, false, 4 }, { 0.25f, false, 2 }, { 0.125f, false, 2 } };
	const double kTreeDistM[4] = { 1500.0, 3000.0, 5000.0, 7000.0 };
	const double kGrassDistM = 260.0;
	const double kHysteresisM = 200.0;

	double CullM(EDmNatureMesh Id, float DistScale)
	{
		switch (Id)
		{
		case EDmNatureMesh::Shrub: return 1300.0 * DistScale;
		case EDmNatureMesh::Grass: return 300.0 * DistScale;
		case EDmNatureMesh::Grazer: return 5000.0;
		case EDmNatureMesh::Bird: return 4500.0;
		case EDmNatureMesh::Fish: return 1600.0;
		default: return 7600.0 * DistScale;
		}
	}
}

ADmNature::ADmNature()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;
	CanopyDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("Canopy"));
	CanopyDecal->SetupAttachment(Root);
	CanopyDecal->DecalSize = FVector(1.06e7);
	CanopyDecal->SetFadeScreenSize(0.f);
	CanopyDecal->SetVisibility(false);
}

void ADmNature::BeginPlay()
{
	Super::BeginPlay();
	double Dv = Density, Ds = DistScale;
	FDmShotArgs::Value(TEXT("-naturedensity="), Dv);
	FDmShotArgs::Value(TEXT("-naturedist="), Ds);
	Density = float(Dv); DistScale = float(Ds);
	Density = FMath::Clamp(Density, 0.f, 4.f);
	DistScale = FMath::Clamp(DistScale, 0.1f, 4.f);
	bCanopy = !FDmShotArgs::Param(TEXT("nocanopy"));
	bTrees = !FDmShotArgs::Param(TEXT("notrees"));
	bGrass = !FDmShotArgs::Param(TEXT("nograss"));
	bFauna = !FDmShotArgs::Param(TEXT("nofauna"));
	bLog = FDmShotArgs::Param(TEXT("naturelog"));
	{ double Wd = WpoDistM; if (FDmShotArgs::Value(TEXT("-naturewpo="), Wd)) WpoDistM = FMath::Max(0.0, Wd); }
	{ double Sd = ShadowDistM; if (FDmShotArgs::Value(TEXT("-natureshadow="), Sd)) ShadowDistM = FMath::Max(0.0, Sd); }
	double Cs = 1.0;
	if (FDmShotArgs::Value(TEXT("-canopystrength="), Cs)) { CanopyStrength = float(Cs); bCanopyStrengthArg = true; }

	Factory = NewObject<UDmNatureFactory>(this);
	Factory->Build();
	if (UMaterialInstanceDynamic* M = Factory->GetCanopyMaterial()) CanopyDecal->SetDecalMaterial(M);

	if (UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
	{
		DirtyHandle = Sim->Dirty().OnDirty.AddUObject(this, &ADmNature::OnDirty);
		WorldHandle = Sim->OnWorldChanged.AddUObject(this, &ADmNature::OnWorldChanged);
		SeenWorld = Sim->Sim();
	}
	bFieldsDirty = bForceFields = true;
	FollowPlanet();
	UE_LOG(LogDmNature, Display, TEXT("DmNature: up (density %.2f, distance x%.2f, canopy %d trees %d grass %d fauna %d)"), Density, DistScale, bCanopy, bTrees, bGrass, bFauna);
}

void ADmNature::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
	{
		Sim->Dirty().OnDirty.Remove(DirtyHandle);
		Sim->OnWorldChanged.Remove(WorldHandle);
	}
	Pending.Reset();
	Super::EndPlay(Reason);
}

void ADmNature::OnDirty(const FDmDirtyFlags& F)
{
	if (F.bColor || F.bObjects || F.bWater || F.bElev) bFieldsDirty = true;
	if (F.bElev || F.bWater) GroundSig ^= 0x9E3779B97F4A7C15ull;   // re-check (and rebuild) the ground
}

void ADmNature::OnWorldChanged(const dm::World* W)
{
	SeenWorld = W;
	bFieldsDirty = bForceFields = true;
	GroundSig = 0;
	Ground.Reset();
	Fields.Reset();
	ClearAllTiles();
	for (UHierarchicalInstancedStaticMeshComponent* C : FaunaComps) if (C) C->DestroyComponent();
	FaunaComps.Reset();
	FaunaCenter = FVector(1e30);
}

void ADmNature::FollowPlanet()
{
	const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	if (!Sim) return;
	const FTransform T = Sim->GetPlanetToWorld();
	if (T.Equals(LastPlanet, 1e-6)) return;
	LastPlanet = T;
	SetActorTransform(T);
	const FVector C = T.GetLocation();
	if (Factory) Factory->SetPlanetFrame(C, T.TransformVectorNoScale(FVector::UpVector));
	if (Factory)
		if (UMaterialInstanceDynamic* M = Factory->GetCanopyMaterial())
		{
			auto V = [](const FVector& X) { return FLinearColor(float(X.X), float(X.Y), float(X.Z), 0.f); };
			M->SetVectorParameterValue(TEXT("PlanetCenter"), V(C));
			M->SetVectorParameterValue(TEXT("PlanetX"), V(T.TransformVectorNoScale(FVector(1, 0, 0))));
			M->SetVectorParameterValue(TEXT("PlanetY"), V(T.TransformVectorNoScale(FVector(0, 1, 0))));
			M->SetVectorParameterValue(TEXT("PlanetZ"), V(T.TransformVectorNoScale(FVector(0, 0, 1))));
		}
}

bool ADmNature::TileShadowWanted(const FDmTileKey& Key) const
{
	if (Key.Layer != uint8(EDmNatureLayer::Trees) || !Fields) return false;
	double AngR = 0;
	const FVector Cd = Key.CenterDir(&AngR);
	const double DistM = FMath::Max(0.0, ((ShadowCam - Cd * Fields->SeaRadiusUU).Size() - AngR * DmCoords::RadiusUU) / 100.0);
	return DistM < ShadowDistM;
}

void ADmNature::UpdateShadows(const FVector& Cam)
{
	ShadowCam = Cam;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastShadowUpdate < 0.2) return;
	LastShadowUpdate = Now;
	for (TPair<uint64, FDmNatureTileComps>& Kv : TileComps)
	{
		const FTileState* St = Tiles.Find(Kv.Key);
		if (!St) continue;
		const bool bWant = TileShadowWanted(St->Key);
		for (UHierarchicalInstancedStaticMeshComponent* C : Kv.Value.Comps)
			if (C && C->CastShadow != bWant && C->GetStaticMesh() && !C->GetStaticMesh()->GetName().Contains(TEXT("Grass")))
				C->SetCastShadow(bWant);
	}
}

bool ADmNature::CameraPlanet(FVector& Out) const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->PlayerCameraManager) return false;
	Out = LastPlanet.InverseTransformPosition(PC->PlayerCameraManager->GetCameraLocation());
	return !Out.IsNearlyZero();
}

// ---------------------------------------------------------------- fields and ground

void ADmNature::UpdateFields(const dm::World& W)
{
	const double Now = FPlatformTime::Seconds();
	if (bFieldsTask && FieldsTask.IsCompleted())
	{
		bFieldsTask = false;
		TSharedPtr<FDmNatureFields, ESPMode::ThreadSafe> F = FieldsTask.GetResult();
		if (F.IsValid() && F->Canopy.Num())
		{
			Fields = F;
			FieldsVersion = F->Version;
			UploadCanopy();
			UE_LOG(LogDmNature, Display, TEXT("DmNature: fields v%u baked in %.0f ms (%d clearings)"), F->Version, F->BuildMs, F->Clearings.Num());
		}
	}
	if (bFieldsDirty && !bFieldsTask && (bForceFields || Now - LastFields >= RebakeEvery))
	{
		bFieldsDirty = false;
		bForceFields = false;
		LastFields = Now;
		TSharedPtr<FDmNatureFields, ESPMode::ThreadSafe> F = FDmNatureFields::Copy(W, FieldsVersion + 1);
		FieldsTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [F]() { F->Bake(); return F; });
		bFieldsTask = true;
	}
}

void ADmNature::UpdateGround(const dm::World& W)
{
	const double Now = FPlatformTime::Seconds();
	if (bGroundTask && GroundTask.IsCompleted())
	{
		bGroundTask = false;
		Ground = GroundTask.GetResult();
		GroundSig = PendingGroundSig;
		++GroundVersion;
		UE_LOG(LogDmNature, Display, TEXT("DmNature: ground v%u from %s (%d triangles, %.0f ms)"), GroundVersion, Ground->SourceName(), Ground->Triangles, Ground->BuildMs);
	}
	if (bGroundTask || Now - LastGroundCheck < 0.5) return;
	LastGroundCheck = Now;

	// 1. a registered provider wins
	if (FDmNatureGround::HasProvider())
	{
		const uint64 Sig = 0x1000 ^ (GroundSig & 0xFFFF0000ull);
		if (!Ground || Ground->GetSource() != FDmNatureGround::ESource::Provider || GroundSig == 0)
		{
			PendingGroundSig = Sig | 1;
			FDmNatureGroundPtr G = FDmNatureGround::FromProvider(W);
			GroundTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [G]() { return G; });
			bGroundTask = true;
		}
		return;
	}
	// 2. the real planet (C1): its own surface function on a private snapshot (rebuilt when elev / water change)
	{
		bool bPlanet = false;
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
			if (It->GetClass()->GetName() == TEXT("DmPlanet")) { bPlanet = true; break; }
		if (bPlanet)
		{
			const uint64 Sig = (GroundSig & ~0xFull) | 4;
			if (Ground && Ground->GetSource() == FDmNatureGround::ESource::PlanetTerrain && Sig == GroundSig && GroundSig != 0) return;
			PendingGroundSig = Sig == 0 ? 4 : Sig;
			TSharedPtr<FDmNatureGround, ESPMode::ThreadSafe> Shell = FDmNatureGround::PlanetShell(W);
			GroundTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Shell]() -> FDmNatureGroundPtr { return FDmNatureGround::FinishPlanet(Shell); });
			bGroundTask = true;
			return;
		}
	}
	// 3. the bridge's placeholder planet: read its land mesh back
	ADmPlaceholderPlanet* PP = nullptr;
	for (TActorIterator<ADmPlaceholderPlanet> It(GetWorld()); It; ++It) { PP = *It; break; }
	if (PP && PP->Land)
	{
		const FProcMeshSection* S = PP->Land->GetProcMeshSection(0);
		if (!S || S->ProcVertexBuffer.Num() == 0) { if (Now - FirstTickTime < 3.0) return; }
		else
		{
			uint64 Sig = uint64(S->ProcVertexBuffer.Num()) * 0x100000001B3ull;
			const int32 NV = S->ProcVertexBuffer.Num();
			for (int32 K = 0; K < 32; ++K)
			{
				const FVector& P = S->ProcVertexBuffer[(int64(K) * 7919 * 131) % NV].Position;
				Sig = (Sig ^ uint64(int64(P.X * 7.0) ^ int64(P.Y * 11.0) ^ int64(P.Z * 13.0))) * 0x100000001B3ull;
			}
			Sig |= 2;
			if (Sig == GroundSig && Ground && Ground->GetSource() == FDmNatureGround::ESource::PlaceholderMesh) return;
			TArray<FVector3f> Verts; Verts.SetNumUninitialized(NV);
			for (int32 I = 0; I < NV; ++I) Verts[I] = FVector3f(S->ProcVertexBuffer[I].Position);
			TArray<uint32> Idx = S->ProcIndexBuffer;
			PendingGroundSig = Sig;
			TSharedPtr<TArray<FVector3f>> Vp = MakeShared<TArray<FVector3f>>(MoveTemp(Verts));
			TSharedPtr<TArray<uint32>> Ip = MakeShared<TArray<uint32>>(MoveTemp(Idx));
			// FromSim copies the world's elevation on this thread; the bucket build runs on the worker
			TSharedPtr<FDmNatureGround, ESPMode::ThreadSafe> Shell = ConstCastSharedPtr<FDmNatureGround>(FDmNatureGround::FromSim(W));
			GroundTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Shell, Vp, Ip]() -> FDmNatureGroundPtr
			{
				return FDmNatureGround::FromMeshShell(Shell, MoveTemp(*Vp), MoveTemp(*Ip));
			});
			bGroundTask = true;
			return;
		}
	}
	// 4. the sim surface
	const uint64 Sig = (GroundSig == 0 || !Ground) ? 3 : GroundSig;
	if (!Ground || Ground->GetSource() != FDmNatureGround::ESource::SimSurface || Sig != GroundSig || GroundSig == 0)
	{
		PendingGroundSig = 3;
		FDmNatureGroundPtr G = FDmNatureGround::FromSim(W);
		GroundTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [G]() { return G; });
		bGroundTask = true;
	}
}

void ADmNature::UploadCanopy()
{
	if (!Fields || !Factory) return;
	TArray<FColor> Px = Fields->Canopy;
	CanopyTex = DmNature::MakeTexture(TEXT("T_NatureCanopy"), FDmNatureFields::W, FDmNatureFields::H, MoveTemp(Px), false, false, 0.4f, false, false, true);
	TArray<FColor> Pc = Fields->Climate;
	ClimateTex = DmNature::MakeTexture(TEXT("T_NatureClimate"), FDmNatureFields::W, FDmNatureFields::H, MoveTemp(Pc), false, false, 0.4f, false, false, true);
	if (UMaterialInstanceDynamic* M = Factory->GetCanopyMaterial())
	{
		M->SetTextureParameterValue(TEXT("CanopyTex"), CanopyTex);
		M->SetTextureParameterValue(TEXT("ClimateTex"), ClimateTex);
		M->SetScalarParameterValue(TEXT("SeaRadius"), float(Fields->SeaRadiusUU));
		// The real planet (C1, class ADmPlanet, found by name: no compile-time dependency) paints a forest tint of its
		// own into the ground colour; over it the canopy decal adds the crown texture and the exact tree-placement
		// edges at reduced opacity so forests are not darkened twice. -canopystrength= overrides both cases.
		float Strength = CanopyStrength;
		if (!bCanopyStrengthArg)
			for (TActorIterator<AActor> It(GetWorld()); It; ++It)
				if (It->GetClass()->GetName() == TEXT("DmPlanet")) { Strength *= PlanetCanopyScale; break; }
		M->SetScalarParameterValue(TEXT("Strength"), Strength);
		UE_LOG(LogDmNature, Display, TEXT("DmNature: canopy decal strength %.2f"), Strength);
	}
	CanopyDecal->SetVisibility(bCanopy && Factory->GetCanopyMaterial() != nullptr);
}

void ADmNature::SetCanopyVisible(bool bVisible)
{
	bCanopy = bVisible;
	if (CanopyDecal) CanopyDecal->SetVisibility(bVisible && CanopyTex != nullptr);
}

// ---------------------------------------------------------------- streaming

int32 ADmNature::LevelFor(EDmNatureLayer L, double DistM, int32 Current, float DS)
{
	if (L == EDmNatureLayer::Grass)
	{
		const double Lim = kGrassDistM * DS + (Current == 0 ? kHysteresisM * 0.25 : 0.0);
		return DistM < Lim ? 0 : -1;
	}
	for (int32 K = 0; K < 4; ++K)
	{
		// a tile keeps its finer level a little longer (hysteresis)
		const double Lim = kTreeDistM[K] * DS + (Current >= 0 && Current <= K ? kHysteresisM : 0.0);
		if (DistM < Lim) return K;
	}
	return -1;
}

void ADmNature::UpdateStreaming(const FVector& Cam)
{
	const double Now = FPlatformTime::Seconds();
	// the wanted set: recomputed when the camera moved or every 0.25 s
	if ((Cam - LastWantCam).Size() > 3000.0 || Now - LastWantTime > 0.25)
	{
		LastWantCam = Cam;
		LastWantTime = Now;
		Wanted.Reset();
		const FVector Sub = Cam.GetSafeNormal();
		const double GroundR = Ground->Radius(Sub);
		const double AltM = FMath::Max(0.0, (Cam.Size() - GroundR) / 100.0);
		TSet<uint64> Seen;
		for (int32 L = 0; L < 2; ++L)
		{
			const EDmNatureLayer Layer = EDmNatureLayer(L);
			if (Layer == EDmNatureLayer::Trees && !bTrees) continue;
			if (Layer == EDmNatureLayer::Grass && !bGrass) continue;
			const double MaxD = (Layer == EDmNatureLayer::Trees ? kTreeDistM[3] : kGrassDistM) * DistScale + kHysteresisM;
			if (AltM >= MaxD) continue;
			const double TileM = 2.0 * UE_DOUBLE_PI * DmCoords::RadiusKm * 1000.0 / 4.0 / FDmTileKey::TilesPerFace(Layer);
			const double Rg = FMath::Sqrt(FMath::Max(0.0, MaxD * MaxD - AltM * AltM)) + TileM;
			const double StepM = TileM * 0.45;
			FVector E, N;
			DmCoords::Basis(Sub, E, N);
			const int32 NR = FMath::CeilToInt(Rg / StepM);
			for (int32 Ri = 0; Ri <= NR; ++Ri)
			{
				const double Rr = Ri * StepM;
				const int32 NA = FMath::Max(1, FMath::CeilToInt(2.0 * UE_DOUBLE_PI * Rr / StepM));
				for (int32 Ai = 0; Ai < NA; ++Ai)
				{
					const double A = 2.0 * UE_DOUBLE_PI * Ai / NA;
					const FVector D = (Sub + (E * FMath::Cos(A) + N * FMath::Sin(A)) * (Rr / (DmCoords::RadiusKm * 1000.0))).GetSafeNormal();
					const FDmTileKey K = FDmTileKey::At(Layer, D);
					const uint64 P = K.Packed();
					if (Seen.Contains(P)) continue;
					Seen.Add(P);
					double AngR = 0;
					const FVector Cd = K.CenterDir(&AngR);
					const double DistM = FMath::Max(0.0, ((Cam - Cd * Fields->SeaRadiusUU).Size() - AngR * DmCoords::RadiusUU) / 100.0);
					const FTileState* St = Tiles.Find(P);
					const int32 Lv = LevelFor(Layer, DistM, St ? St->Level : -1, DistScale);
					if (Lv >= 0) Wanted.Add({ K, Lv, DistM });
				}
			}
		}
		Wanted.Sort([](const FWant& A, const FWant& B) { return A.Dist < B.Dist; });
		for (const FWant& Wn : Wanted)
			if (FTileState* St = Tiles.Find(Wn.Key.Packed())) St->LastWanted = Now;
	}

	// launch builds, nearest first
	for (const FWant& Wn : Wanted)
	{
		if (Pending.Num() >= MaxTasks) break;
		const uint64 P = Wn.Key.Packed();
		if (Pending.Contains(P)) continue;
		const FTileState* St = Tiles.Find(P);
		if (St && St->Level == Wn.Level && St->FieldsVersion == FieldsVersion && St->GroundVersion == GroundVersion) continue;
		FDmScatterSettings S;
		S.Density = Density;
		if (Wn.Key.Layer == uint8(EDmNatureLayer::Trees))
		{
			S.Keep = kTreeLevels[Wn.Level].Keep;
			S.bShrubs = kTreeLevels[Wn.Level].bShrubs;
			S.MaxVariants = kTreeLevels[Wn.Level].Variants;
		}
		FPending& Pd = Pending.Add(P);
		Pd.Level = Wn.Level;
		Pd.GroundVersion = GroundVersion;
		FDmNatureFieldsPtr Fp = Fields;
		FDmNatureGroundPtr Gp = Ground;
		const FDmTileKey Key = Wn.Key;
		Pd.Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Fp, Gp, Key, S]() { return DmNatureScatter::BuildTile(*Fp, *Gp, Key, S); });
	}

	// drop tiles nobody wants any more (after a grace period)
	TArray<uint64> Drop;
	for (const TPair<uint64, FTileState>& Kv : Tiles)
		if (Now - Kv.Value.LastWanted > 1.5) Drop.Add(Kv.Key);
	for (uint64 K : Drop) ClearTile(K);
}

void ADmNature::ApplyResults()
{
	int32 Applied = 0;
	TArray<uint64> Done;
	TSet<uint64> WantSet;
	TMap<uint64, int32> WantLevel;
	for (const FWant& Wn : Wanted) { WantSet.Add(Wn.Key.Packed()); WantLevel.Add(Wn.Key.Packed(), Wn.Level); }
	for (TPair<uint64, FPending>& Kv : Pending)
	{
		if (!Kv.Value.Task.IsCompleted()) continue;
		Done.Add(Kv.Key);
		TSharedPtr<FDmTileResult, ESPMode::ThreadSafe> R = Kv.Value.Task.GetResult();
		++TilesBuilt;
		TileMsTotal += R->Ms;
		if (bLog) UE_LOG(LogDmNature, Display, TEXT("DmNature: tile L%d f%d %d,%d level %d: %d of %d candidates in %.1f ms"), R->Key.Layer, R->Key.Face, R->Key.X, R->Key.Y, Kv.Value.Level, R->Instances, R->Candidates, R->Ms);
		if (WantSet.Contains(Kv.Key) && R->FieldsVersion == FieldsVersion && Kv.Value.GroundVersion == GroundVersion)
		{
			ApplyTile(*R, Kv.Value.Level, Kv.Value.GroundVersion);
			if (++Applied >= MaxAppliesPerFrame) break;
		}
	}
	for (uint64 K : Done) Pending.Remove(K);
}

TArray<UHierarchicalInstancedStaticMeshComponent*> ADmNature::MakeComps(const FDmTileResult& R, bool bIsFauna, bool bTileShadows)
{
	TArray<UHierarchicalInstancedStaticMeshComponent*> Out;
	const double LineupR2 = FMath::Square(240.0 * 100.0);
	for (const FDmTileBatch& B : R.Batches)
	{
		const FDmNatureMeshInfo& Info = Factory->Get(B.Id, B.Variant);
		if (!Info.Mesh || B.Xf.Num() == 0) continue;
		const TArray<FTransform>* Xf = &B.Xf;
		const TArray<float>* Cu = &B.Custom;
		TArray<FTransform> FXf; TArray<float> FCu;
		if (bLineup && UDmNatureFactory::IsTree(B.Id))
		{
			const FVector L = LineupDir * Fields->SeaRadiusUU - R.Center;
			for (int32 I = 0; I < B.Xf.Num(); ++I)
				if (FVector::DistSquared(FVector(B.Xf[I].GetLocation().X, B.Xf[I].GetLocation().Y, B.Xf[I].GetLocation().Z), L) > LineupR2)
				{
					FXf.Add(B.Xf[I]);
					FCu.Append(&B.Custom[I * UDmNatureFactory::NumCustomData], UDmNatureFactory::NumCustomData);
				}
			if (FXf.Num() == 0) continue;
			Xf = &FXf; Cu = &FCu;
		}
		UHierarchicalInstancedStaticMeshComponent* C = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
		C->SetMobility(EComponentMobility::Movable);
		C->SetupAttachment(Root);
		C->SetRelativeLocation(R.Center);
		C->SetStaticMesh(Info.Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCanEverAffectNavigation(false);
		C->SetGenerateOverlapEvents(false);
		const bool bGrassId = B.Id == EDmNatureMesh::Grass;
		// shadows: virtual shadow map pages of hundreds of thousands of trees are the biggest cost of a forest view
		// (and the planet turns, so they are re-rendered every frame): tree tiles cast them only within ShadowDistM
		// of the camera (UpdateShadows toggles them as the camera moves); beyond, crowns shade themselves through
		// their AO, painted clump shading and volumetric normals
		const bool bShadow = !bGrassId && B.Id != EDmNatureMesh::Fish && (bIsFauna || bTileShadows);
		C->SetCastShadow(bShadow);
		C->bAffectDistanceFieldLighting = false;
		C->bReceivesDecals = false;
		C->ShadowCacheInvalidationBehavior = EShadowCacheInvalidationBehavior::Rigid;
		const int32 Cull = int32(CullM(B.Id, DistScale) * 100.0);
		C->SetCullDistances(int32(Cull * 0.85), Cull);
		// trees keep WPO at every distance: their far LODs are camera-facing clump cards (M_NatureLeaf); grass stops at 300 m
		C->SetWorldPositionOffsetDisableDistance(bGrassId ? int32(300.0 * 100.0) : int32(WpoDistM * 100.0));
		if (B.ForcedLod >= 0) C->SetForcedLodModel(B.ForcedLod + 1);
		C->SetNumCustomDataFloats(UDmNatureFactory::NumCustomData);
		C->PreAllocateInstancesMemory(Xf->Num());
		C->AddInstances(*Xf, false, false, false);
		if (Cu->Num() == Xf->Num() * UDmNatureFactory::NumCustomData)
			static_cast<UInstancedStaticMeshComponent*>(C)->SetCustomData(0, Xf->Num() - 1, TConstArrayView<float>(*Cu), false);
		C->RegisterComponent();
		Out.Add(C);
	}
	return Out;
}

void ADmNature::ApplyTile(const FDmTileResult& R, int32 Level, uint32 InGroundVersion)
{
	const uint64 P = R.Key.Packed();
	if (FDmNatureTileComps* Old = TileComps.Find(P))
	{
		for (UHierarchicalInstancedStaticMeshComponent* C : Old->Comps) if (C) C->DestroyComponent();
		Old->Comps.Reset();
	}
	FDmNatureTileComps& Tc = TileComps.FindOrAdd(P);
	for (UHierarchicalInstancedStaticMeshComponent* C : MakeComps(R, false, TileShadowWanted(R.Key))) Tc.Comps.Add(C);
	FTileState& St = Tiles.FindOrAdd(P);
	St.Key = R.Key;
	St.Level = Level;
	St.FieldsVersion = R.FieldsVersion;
	St.GroundVersion = InGroundVersion;
	St.Instances = R.Instances;
	FMemory::Memcpy(St.PerKind, R.PerKind, sizeof(St.PerKind));
	St.LastWanted = FPlatformTime::Seconds();
}

void ADmNature::ClearTile(uint64 Key)
{
	if (FDmNatureTileComps* Old = TileComps.Find(Key))
		for (UHierarchicalInstancedStaticMeshComponent* C : Old->Comps) if (C) C->DestroyComponent();
	TileComps.Remove(Key);
	Tiles.Remove(Key);
}

void ADmNature::ClearAllTiles()
{
	TArray<uint64> Keys;
	Tiles.GetKeys(Keys);
	for (uint64 K : Keys) ClearTile(K);
	for (TPair<uint64, FDmNatureTileComps>& Kv : TileComps)
		for (UHierarchicalInstancedStaticMeshComponent* C : Kv.Value.Comps) if (C) C->DestroyComponent();
	TileComps.Reset();
	Pending.Reset();
	Wanted.Reset();
}

void ADmNature::UpdateFauna(const FVector& Cam)
{
	if (bFaunaTask && FaunaTask.IsCompleted())
	{
		bFaunaTask = false;
		TSharedPtr<FDmTileResult, ESPMode::ThreadSafe> R = FaunaTask.GetResult();
		for (UHierarchicalInstancedStaticMeshComponent* C : FaunaComps) if (C) C->DestroyComponent();
		FaunaComps.Reset();
		if (R->FieldsVersion == FieldsVersion)
		{
			for (UHierarchicalInstancedStaticMeshComponent* C : MakeComps(*R, true)) FaunaComps.Add(C);
			FaunaInstances = R->Instances;
			FMemory::Memcpy(FaunaPerKind, R->PerKind, sizeof(FaunaPerKind));
		}
	}
	if (!bFauna || bFaunaTask) return;
	const FVector Sub = Cam.GetSafeNormal();
	const double AltM = (Cam.Size() - Ground->Radius(Sub)) / 100.0;
	if (AltM > 8000.0)
	{
		if (FaunaComps.Num())
		{
			for (UHierarchicalInstancedStaticMeshComponent* C : FaunaComps) if (C) C->DestroyComponent();
			FaunaComps.Reset();
			FaunaInstances = 0;
			FMemory::Memzero(FaunaPerKind, sizeof(FaunaPerKind));
			FaunaCenter = FVector(1e30);
		}
		return;
	}
	const double MovedM = FMath::Acos(FMath::Clamp(FVector::DotProduct(Sub, FaunaCenter.GetSafeNormal()), -1.0, 1.0)) * DmCoords::RadiusKm * 1000.0;
	if (MovedM < 1500.0 && FaunaFieldsVersion == FieldsVersion && FaunaGroundVersion == GroundVersion) return;
	FaunaCenter = Sub;
	FaunaFieldsVersion = FieldsVersion;
	FaunaGroundVersion = GroundVersion;
	FDmNatureFieldsPtr Fp = Fields;
	FDmNatureGroundPtr Gp = Ground;
	const float Ds = Density;
	FaunaTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Fp, Gp, Sub, Ds]() { return DmNatureFauna::Build(*Fp, *Gp, Sub, 5.5, FMath::Min(Ds, 1.5f)); });
	bFaunaTask = true;
}

// ---------------------------------------------------------------- lineup

void ADmNature::BuildLineup(const FVector& PlanetDir)
{
	bLineup = true;
	LineupDir = PlanetDir.GetSafeNormal();
	for (UHierarchicalInstancedStaticMeshComponent* C : LineupComps) if (C) C->DestroyComponent();
	LineupComps.Reset();
	// existing tiles must drop the trees on the lawn: rebuild them
	for (TPair<uint64, FTileState>& Kv : Tiles) Kv.Value.FieldsVersion = 0;
	if (!Ground || !Fields || !Factory) return;
	FDmTileResult R;
	R.Center = LineupDir * Fields->SeaRadiusUU;
	FVector E, N;
	DmCoords::Basis(LineupDir, E, N);
	auto Add = [&](EDmNatureMesh Id, int32 V, double Xm, double Ym, float Yaw, const TArray<float>& Cd)
	{
		const FVector D = (LineupDir + (E * Xm + N * Ym) / (DmCoords::RadiusKm * 1000.0)).GetSafeNormal();
		const double Rg = Ground->Radius(D);
		const FQuat Q = FQuat::FindBetweenNormals(FVector::UpVector, D) * FQuat(FVector::UpVector, Yaw);
		FDmTileBatch& B = R.Batches.AddDefaulted_GetRef();
		B.Id = Id; B.Variant = V;
		B.Xf.Add(FTransform(Q, D * (Rg - 20.0) - R.Center));
		B.Custom = Cd;
	};
	// -lineuplods: columns are LOD 0..3 of variant 0 (judging the far LODs side by side); default: the 4 variants
	const bool bLods = FDmShotArgs::Param(TEXT("lineuplods"));
	for (int32 K = 0; K < int32(EDmTreeKind::Num); ++K)
		for (int32 V = 0; V < 4; ++V)
		{
			Add(EDmNatureMesh(K), bLods ? 0 : V, (V - 1.5) * 26.0, (K - 2.5) * 30.0, bLods ? 0.6f : V * 1.3f, { 0.f, 0.f, 0.f });
			if (bLods) R.Batches.Last().ForcedLod = V;
		}
	for (int32 V = 0; V < 4; ++V)
		Add(EDmNatureMesh::Grazer, V, (V - 1.5) * 26.0, -105.0, 1.2f, { 1.f, 1.f, 1.f });
	for (const FDmTileBatch& B : R.Batches) R.Instances += B.Xf.Num();
	const bool bWas = bLineup;
	bLineup = false;   // MakeComps must not clear the lineup itself
	for (UHierarchicalInstancedStaticMeshComponent* C : MakeComps(R, true)) LineupComps.Add(C);
	bLineup = bWas;
	UE_LOG(LogDmNature, Display, TEXT("DmNature: lineup of %d meshes"), R.Instances);
}

// ---------------------------------------------------------------- tick

void ADmNature::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = FPlatformTime::Seconds();
	if (FirstTickTime < 0) FirstTickTime = Now;
	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const dm::World* W = Sim ? Sim->Sim() : nullptr;
	if (!W || !W->geo || !Factory) return;
	if (W != SeenWorld) OnWorldChanged(W);
	FollowPlanet();
	UpdateFields(*W);
	UpdateGround(*W);
	if (!Fields || !Ground) return;
	FVector Cam;
	if (CameraPlanet(Cam))
	{
		UpdateStreaming(Cam);
		ApplyResults();
		UpdateShadows(Cam);
		UpdateFauna(Cam);
	}
	if (bLineup && LineupComps.Num() == 0) BuildLineup(LineupDir);
	if (Now - LastLog > 10.0)
	{
		LastLog = Now;
		UE_LOG(LogDmNature, Display, TEXT("DmNature: %s"), *Describe());
	}
}

// ---------------------------------------------------------------- state

bool ADmNature::IsSettled() const
{
	if (!Fields || !Ground || bFieldsTask || bGroundTask || bFaunaTask || Pending.Num()) return false;
	for (const FWant& Wn : Wanted)
	{
		const FTileState* St = Tiles.Find(Wn.Key.Packed());
		if (!St || St->Level != Wn.Level || St->FieldsVersion != FieldsVersion || St->GroundVersion != GroundVersion) return false;
	}
	return true;
}

int32 ADmNature::CountInstances(EDmNatureMesh Id) const
{
	int32 N = 0;
	for (const TPair<uint64, FTileState>& Kv : Tiles) N += Kv.Value.PerKind[int32(Id)];
	return N + FaunaPerKind[int32(Id)];
}

int32 ADmNature::NumComponents() const
{
	int32 N = FaunaComps.Num() + LineupComps.Num();
	for (const TPair<uint64, FDmNatureTileComps>& Kv : TileComps) N += Kv.Value.Comps.Num();
	return N;
}

FString ADmNature::Describe() const
{
	int32 Lv[4] = {}, NGrass = 0, Inst = 0;
	for (const TPair<uint64, FTileState>& Kv : Tiles)
	{
		if (Kv.Value.Key.Layer == uint8(EDmNatureLayer::Grass)) ++NGrass;
		else if (Kv.Value.Level >= 0 && Kv.Value.Level < 4) ++Lv[Kv.Value.Level];
		Inst += Kv.Value.Instances;
	}
	FString Kinds;
	for (int32 I = 0; I < int32(EDmNatureMesh::Num); ++I)
	{
		const int32 N = CountInstances(EDmNatureMesh(I));
		if (N) Kinds += FString::Printf(TEXT(" %s=%d"), UDmNatureFactory::Name(EDmNatureMesh(I)), N);
	}
	return FString::Printf(TEXT("tiles %d+%d+%d+%d grass %d pending %d comps %d instances %d fauna %d [%s ] fields v%u %.0f ms ground %s v%u tiles built %lld avg %.1f ms%s"),
		Lv[0], Lv[1], Lv[2], Lv[3], NGrass, Pending.Num(), NumComponents(), Inst, FaunaInstances, *Kinds, FieldsVersion, Fields ? Fields->BuildMs : 0.0,
		Ground ? Ground->SourceName() : TEXT("none"), GroundVersion, TilesBuilt, TilesBuilt ? TileMsTotal / TilesBuilt : 0.0, IsSettled() ? TEXT(" SETTLED") : TEXT(""));
}

// ---------------------------------------------------------------- scene part

static FDmScenePartRegistrar GDmNaturePart(TEXT("nature"), EDmPartKind::World, 0,
	[](const FDmSceneContext& S) -> AActor*
	{
		if (!S.World || FDmShotArgs::Param(TEXT("nonature"))) return nullptr;
		return S.World->SpawnActor<ADmNature>();
	}, TEXT("Game/Nature"));
