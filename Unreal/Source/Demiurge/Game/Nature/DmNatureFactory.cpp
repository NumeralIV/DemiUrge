// DEMIURGE — Game/Nature/DmNatureFactory.cpp : see the header.

#include "Game/Nature/DmNatureFactory.h"
#include "Game/Nature/DmNatureCritters.h"
#include "Game/Nature/DmNatureTrees.h"

#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformTime.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmNatureFactory, Log, All);

int32 UDmNatureFactory::NumVariants(EDmNatureMesh Id)
{
	switch (Id)
	{
	case EDmNatureMesh::Bird: case EDmNatureMesh::Fish: return 2;
	case EDmNatureMesh::Num: return 0;
	default: return 4;
	}
}

const TCHAR* UDmNatureFactory::Name(EDmNatureMesh Id)
{
	if (IsTree(Id)) return DmNature::TreeName(EDmTreeKind(uint8(Id)));
	if (Id < EDmNatureMesh::Num) return DmNature::CritterName(EDmCritter(uint8(Id) - uint8(EDmNatureMesh::Grass)));
	return TEXT("?");
}

UMaterialInstanceDynamic* UDmNatureFactory::LoadMat(const TCHAR* MatName)
{
	const FString Path = FString::Printf(TEXT("/Game/Nature/%s.%s"), MatName, MatName);
	UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *Path);
	if (!M)
	{
		UE_LOG(LogDmNatureFactory, Error, TEXT("DmNature: missing material %s (run Scripts/nature.py)"), *Path);
		bMissing = true;
		M = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	return UMaterialInstanceDynamic::Create(M, this);
}

void UDmNatureFactory::Build()
{
	if (bBuilt) return;
	bBuilt = true;
	const double T0 = FPlatformTime::Seconds();
	Tex.Build();
	TArray<UObject*> Objs; Tex.AddReferencedObjects(Objs);
	for (UObject* O : Objs) Keep.Add(O);

	MatLeaf = LoadMat(TEXT("M_NatureLeaf"));
	MatBark = LoadMat(TEXT("M_NatureBark"));
	MatPalmBark = LoadMat(TEXT("M_NatureBark"));
	MatGrass = LoadMat(TEXT("M_NatureGrass"));
	MatGrazer = LoadMat(TEXT("M_NatureCreature"));
	MatBird = LoadMat(TEXT("M_NatureCreature"));
	MatFish = LoadMat(TEXT("M_NatureCreature"));
	{
		UMaterialInterface* C = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Nature/M_NatureCanopy.M_NatureCanopy"));
		if (C) MatCanopy = UMaterialInstanceDynamic::Create(C, this);
		else { UE_LOG(LogDmNatureFactory, Error, TEXT("DmNature: missing /Game/Nature/M_NatureCanopy (run Scripts/nature.py)")); bMissing = true; }
	}
	MatLeaf->SetTextureParameterValue(TEXT("Atlas"), Tex.LeafAtlas);
	MatBark->SetTextureParameterValue(TEXT("BarkTex"), Tex.Bark);
	MatBark->SetTextureParameterValue(TEXT("BarkN"), Tex.BarkN);
	MatPalmBark->SetTextureParameterValue(TEXT("BarkTex"), Tex.PalmBark);
	MatPalmBark->SetTextureParameterValue(TEXT("BarkN"), Tex.PalmBarkN);
	MatPalmBark->SetScalarParameterValue(TEXT("BarkTileU"), 0.8f);
	MatPalmBark->SetScalarParameterValue(TEXT("BarkTileV"), 0.45f);
	MatGrazer->SetScalarParameterValue(TEXT("AnimMode"), 0.f);
	MatBird->SetScalarParameterValue(TEXT("AnimMode"), 1.f);
	MatFish->SetScalarParameterValue(TEXT("AnimMode"), 2.f);
	MatLeaf->SetVectorParameterValue(TEXT("TintA"), FLinearColor(0.72f, 0.88f, 0.78f));
	MatLeaf->SetVectorParameterValue(TEXT("TintB"), FLinearColor(1.02f, 1.06f, 0.80f));
	MatLeaf->SetVectorParameterValue(TEXT("Transmission"), FLinearColor(0.75f, 0.9f, 0.3f));
	SetLeafLook(1.1f, 0.05f, 0.8f, 1.f);
	SetWind(1.f, 1.f);
	SetPlanetFrame(FVector::ZeroVector, FVector::UpVector);

	// ---- trees: 6 kinds x 4 variants, 4 LODs
	const TArray<float> TreeScreen = { 1.f, 0.22f, 0.075f, 0.025f };
	for (int32 K = 0; K < int32(EDmTreeKind::Num); ++K)
	{
		const EDmNatureMesh Id = EDmNatureMesh(K);
		for (int32 V = 0; V < NumVariants(Id); ++V)
		{
			FDmTreeBuild B;
			DmNature::BuildTree(EDmTreeKind(K), 1000 + V * 17 + K * 3, B);
			TArray<const FDmNatureMesh*> L;
			for (int32 I = 0; I < DmTreeLODs; ++I) L.Add(&B.LOD[I]);
			UMaterialInterface* BarkM = EDmTreeKind(K) == EDmTreeKind::Palm ? MatPalmBark.Get() : MatBark.Get();
			FDmNatureMeshInfo Info;
			Info.Mesh = DmNature::MakeStaticMesh(this, FString::Printf(TEXT("SM_Nature_%s_%d"), Name(Id), V), L, { BarkM, MatLeaf.Get() }, TreeScreen);
			Info.Height = B.Height;
			Info.Radius = B.CrownRadius;
			Info.Tris = B.LOD[0].NumTris();
			Keep.Add(Info.Mesh);
			Infos[K].Add(Info);
		}
	}
	// ---- ground cover and wildlife
	for (int32 C = 0; C < int32(EDmCritter::Num); ++C)
	{
		const EDmNatureMesh Id = EDmNatureMesh(uint8(EDmNatureMesh::Grass) + C);
		UMaterialInterface* Mat = Id == EDmNatureMesh::Grass ? MatGrass.Get() : Id == EDmNatureMesh::Grazer ? MatGrazer.Get() : Id == EDmNatureMesh::Bird ? MatBird.Get() : MatFish.Get();
		const float Extend = Id == EDmNatureMesh::Bird ? 130.f : Id == EDmNatureMesh::Fish ? 30.f : Id == EDmNatureMesh::Grazer ? 3.f : 0.f;
		const TArray<float> Screen = Id == EDmNatureMesh::Grass ? TArray<float>{ 1.f, 0.035f, 0.012f } : TArray<float>{ 1.f, 0.04f, 0.01f };
		for (int32 V = 0; V < NumVariants(Id); ++V)
		{
			TArray<FDmNatureMesh> LODs; float Size = 1.f;
			DmNature::BuildCritter(EDmCritter(C), V, LODs, Size);
			TArray<const FDmNatureMesh*> L;
			for (const FDmNatureMesh& M : LODs) L.Add(&M);
			FDmNatureMeshInfo Info;
			Info.Mesh = DmNature::MakeStaticMesh(this, FString::Printf(TEXT("SM_Nature_%s_%d"), Name(Id), V), L, { Mat }, Screen, Extend);
			Info.Height = Size;
			Info.Radius = Size * 0.5f;
			Info.Tris = LODs.Num() ? LODs[0].NumTris() : 0;
			Keep.Add(Info.Mesh);
			Infos[int32(Id)].Add(Info);
		}
	}
	BuildSeconds = FPlatformTime::Seconds() - T0;
	FString Tris;
	for (int32 I = 0; I < int32(EDmNatureMesh::Num); ++I)
		if (Infos[I].Num()) Tris += FString::Printf(TEXT(" %s=%d"), Name(EDmNatureMesh(I)), Infos[I][0].Tris);
	UE_LOG(LogDmNatureFactory, Display, TEXT("DmNature: factory built in %.2f s (LOD0 tris:%s)%s"), BuildSeconds, *Tris, bMissing ? TEXT(" MATERIALS MISSING") : TEXT(""));
}

const FDmNatureMeshInfo& UDmNatureFactory::Get(EDmNatureMesh Id, int32 Variant) const
{
	static const FDmNatureMeshInfo None;
	const int32 I = int32(Id);
	if (I < 0 || I >= int32(EDmNatureMesh::Num) || Infos[I].Num() == 0) return None;
	return Infos[I][FMath::Clamp(Variant, 0, Infos[I].Num() - 1)];
}

void UDmNatureFactory::SetWind(float Strength, float Speed)
{
	for (UMaterialInstanceDynamic* M : { MatLeaf.Get(), MatBark.Get(), MatPalmBark.Get(), MatGrass.Get() })
	{
		if (!M) continue;
		M->SetScalarParameterValue(TEXT("WindStrength"), 100.f * Strength);
		M->SetScalarParameterValue(TEXT("WindSpeed"), Speed);
	}
}

void UDmNatureFactory::SetPlanetFrame(const FVector& CenterWorld, const FVector& AxisWorld)
{
	const FLinearColor C(float(CenterWorld.X), float(CenterWorld.Y), float(CenterWorld.Z), 0.f);
	const FVector A = AxisWorld.GetSafeNormal();
	const FLinearColor Ax(float(A.X), float(A.Y), float(A.Z), 0.f);
	for (UMaterialInstanceDynamic* M : { MatLeaf.Get(), MatBark.Get(), MatPalmBark.Get(), MatGrass.Get(), MatGrazer.Get(), MatBird.Get(), MatFish.Get() })
	{
		if (!M) continue;
		M->SetVectorParameterValue(TEXT("PlanetCenter"), C);
		M->SetVectorParameterValue(TEXT("PlanetAxis"), Ax);
	}
}

void UDmNatureFactory::SetLeafLook(float Brightness, float Spec, float Rough, float Transmission)
{
	if (!MatLeaf) return;
	MatLeaf->SetScalarParameterValue(TEXT("Brightness"), Brightness);
	MatLeaf->SetScalarParameterValue(TEXT("LeafSpec"), Spec);
	MatLeaf->SetScalarParameterValue(TEXT("LeafRough"), Rough);
	MatLeaf->SetScalarParameterValue(TEXT("TransmissionScale"), Transmission);
}
