#include "AssetShow.h"
#include "LifeWorld.h"
#include "StandIn/DmTownsKit.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"

namespace
{
	TArray<double> List(const TCHAR* Key, TArray<double> Def)
	{
		FString V;
		if (!FParse::Value(FCommandLine::Get(), Key, V, false)) return Def;
		TArray<FString> P;
		V.ParseIntoArray(P, TEXT(","));
		for (int32 I = 0; I < P.Num() && I < Def.Num(); ++I) Def[I] = FCString::Atod(*P[I]);
		return Def;
	}
	TArray<FString> Assets()
	{
		FString V;
		TArray<FString> P;
		if (FParse::Value(FCommandLine::Get(), TEXT("-assets="), V, false)) V.ParseIntoArray(P, TEXT(","));
		return P;
	}
}

AAssetShow::AAssetShow()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

bool AAssetShow::Wanted() { return Assets().Num() > 0; }

FVector2D AAssetShow::ChooseSite(const ALifeWorld* W)
{
	FString V;
	if (FParse::Value(FCommandLine::Get(), TEXT("-assetat="), V, false))
	{
		const TArray<double> P = List(TEXT("-assetat="), { 0, 0 });
		return FVector2D(P[0], P[1]);
	}
	// default: the open green beside the first village's crossroads, with houses behind (site 38500)
	for (const FLifeSettlement& S : W->Settlements)
		if (S.Kind == FLifeSettlement::Village) return S.C + FVector2D(-20.0, -32.0);
	return FVector2D(0, 0);
}

void AAssetShow::SetSun(const FVector& SunDirWorld)
{
	if (StandIn) StandIn->SetFrame(FVector::ZeroVector, SunDirWorld);
}

void AAssetShow::Build(ALifeWorld* W, const FVector2D& InSite)
{
	Site = InSite;
	Names = Assets();
	const double Gap = List(TEXT("-assetgap="), { 13.0 })[0];
	const double FrontDeg = List(TEXT("-assetyaw="), { 250.0 })[0];
	const TArray<double> Cul = List(TEXT("-culture="), { 0.787, 0.349, 0.0 });
	const double State = List(TEXT("-assetstate="), { 0.0 })[0];
	const double Lit = List(TEXT("-assetlit="), { 1.0 })[0];
	const double WaterH = List(TEXT("-water="), { -1.0 })[0];
	// row perpendicular to the front direction
	const double F = FMath::DegreesToRadians(FrontDeg);
	const FVector2D Front(FMath::Cos(F), FMath::Sin(F));
	const FVector2D Row(-Front.Y, Front.X);
	const int32 N = Names.Num();
	for (int32 I = 0; I < N; ++I)
	{
		const FString& Nm = Names[I];
		const FVector2D P = Site + Row * ((I - (N - 1) * 0.5) * Gap);
		Slots.Add(P);
		UStaticMesh* Mesh = nullptr;
		float Cd[11] = { 1, 1, 1, 1, 1, 1, float(Lit), float(State), float(Cul[0]), float(Cul[1]), float(Cul[2]) };
		if (Nm.StartsWith(TEXT("standin")))
		{
			if (!StandIn) { StandIn = NewObject<UDmTownsFactory>(this); StandIn->Build(); }
			const int32 Var = Nm.Len() > 7 ? FCString::Atoi(*Nm.Mid(7)) : 0;
			Mesh = StandIn->Get(EDmTownMesh::Shrine, Var).Mesh;
			// towns palette for a stone shrine (wall stone tint, tile roof), lights off as the planner sets them
			const float S[8] = { 0.62f, 0.58f, 0.52f, 0.55f, 0.36f, 0.24f, 0.f, float(State) };
			FMemory::Memcpy(Cd, S, sizeof(S));
		}
		else
		{
			// SM_<Asset>_<Config> -> /Game/Assets/<Asset>/SM_<Asset>_<Config>
			TArray<FString> Parts;
			Nm.ParseIntoArray(Parts, TEXT("_"));
			const FString Folder = Parts.Num() > 1 ? Parts[1] : TEXT("");
			const FString Path = FString::Printf(TEXT("/Game/Assets/%s/%s.%s"), *Folder, *Nm, *Nm);
			Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
			if (!Mesh) UE_LOG(LogTemp, Error, TEXT("AssetShow: missing %s (run art/tools/ue_import.py)"), *Path);
		}
		if (!Mesh) continue;
		const double H = W->HeightAt(P.X, P.Y);
		const FVector Loc = W->WorldCm(P.X, P.Y, H);
		const FQuat Up = FQuat(W->UpQuat(P.X, P.Y));
		const FQuat Yaw(FVector::UpVector, F + UE_DOUBLE_HALF_PI);   // local -Y (the front) -> Front
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
		C->SetMobility(EComponentMobility::Movable);
		C->SetupAttachment(Root);
		C->SetStaticMesh(Mesh);
		C->SetWorldLocationAndRotation(Loc, Up * Yaw);
		C->SetCastShadow(true);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->ShadowCacheInvalidationBehavior = EShadowCacheInvalidationBehavior::Rigid;
		C->SetNumCustomDataFloats(11);
		C->AddInstance(FTransform::Identity, false);
		C->SetCustomData(0, TArrayView<const float>(Cd, 11), true);
		C->RegisterComponent();
		Comps.Add(C);
		// render data numbers for the report: triangles and screen size per LOD
		FString Lods;
		if (const FStaticMeshRenderData* RD = Mesh->GetRenderData())
			for (int32 L = 0; L < RD->LODResources.Num(); ++L)
				Lods += FString::Printf(TEXT(" LOD%d %d tris @%.4f"), L, RD->LODResources[L].GetNumTriangles(), RD->ScreenSize[L].Default);
		const FBox B = Mesh->GetBoundingBox();
		UE_LOG(LogTemp, Display, TEXT("ASSETSHOW %s at (%.1f, %.1f, h %.1f m) size %.2f x %.2f x %.2f m, %d slots,%s"), *Nm, P.X, P.Y, H,
			B.GetSize().X / 100.0, B.GetSize().Y / 100.0, B.GetSize().Z / 100.0, Mesh->GetStaticMaterials().Num(), *Lods);
	}
	if (WaterH > 0.0)
	{
		UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		UMaterialInterface* WM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/AssetLab/M_LabWater.M_LabWater"));
		if (Plane)
		{
			UStaticMeshComponent* Wc = NewObject<UStaticMeshComponent>(this);
			Wc->SetupAttachment(Root);
			Wc->SetStaticMesh(Plane);
			if (WM) Wc->SetMaterial(0, WM);
			const double H = W->HeightAt(Site.X, Site.Y);
			Wc->SetWorldLocationAndRotation(W->WorldCm(Site.X, Site.Y, H + WaterH), FQuat(W->UpQuat(Site.X, Site.Y)));
			Wc->SetWorldScale3D(FVector(N * Gap + 60.0, N * Gap + 60.0, 1.0));
			Wc->SetCastShadow(false);
			Wc->RegisterComponent();
			Keep.Add(Wc);
		}
	}
}
