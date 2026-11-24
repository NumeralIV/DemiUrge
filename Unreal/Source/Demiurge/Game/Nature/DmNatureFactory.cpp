// DEMIURGE — Game/Nature/DmNatureFactory.cpp : see the header.

#include "Game/Nature/DmNatureFactory.h"
#include "Game/Nature/DmNatureCritters.h"
#include "Game/Nature/DmNatureTrees.h"
#include "Game/Core/DmScenario.h"

#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformTime.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "StaticMeshResources.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmNatureFactory, Log, All);

int32 UDmNatureFactory::NumVariants(EDmNatureMesh Id)
{
	switch (Id)
	{
	case EDmNatureMesh::Bird: case EDmNatureMesh::Fish: return 2;
	case EDmNatureMesh::Grazer: return int32(EDmGrazer::Num);
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
	MatLeaf->SetScalarParameterValue(TEXT("Saturation"), 1.f);   // the generated atlas keeps its look (the trim is for the Blender leaves)

	// ---- trees: the Blender families (5 kinds x 4 variants), then the generated stand-ins for whatever is missing
	if (!FDmShotArgs::Param(TEXT("gentrees"))) BuildAssetTrees();
	const TArray<float> TreeScreen = { 1.f, 0.22f, 0.075f, 0.025f };
	for (int32 K = 0; K < int32(EDmTreeKind::Num); ++K)
	{
		const EDmNatureMesh Id = EDmNatureMesh(K);
		if (Infos[K].Num() == NumVariants(Id)) continue;   // assets
		Infos[K].Reset();
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
	// ---- ground cover and wildlife: the Blender animals, then generated stand-ins for whatever is missing (and grass)
	if (!FDmShotArgs::Param(TEXT("genfauna"))) BuildAssetFauna();
	for (int32 C = 0; C < int32(EDmCritter::Num); ++C)
	{
		const EDmNatureMesh Id = EDmNatureMesh(uint8(EDmNatureMesh::Grass) + C);
		if (Infos[int32(Id)].Num() == NumVariants(Id)) continue;   // assets
		Infos[int32(Id)].Reset();
		UMaterialInterface* Mat = Id == EDmNatureMesh::Grass ? MatGrass.Get() : Id == EDmNatureMesh::Grazer ? MatGrazer.Get() : Id == EDmNatureMesh::Bird ? MatBird.Get() : MatFish.Get();
		const float Extend = Id == EDmNatureMesh::Bird ? 130.f : Id == EDmNatureMesh::Fish ? 30.f : Id == EDmNatureMesh::Grazer ? 3.f : 0.f;
		const TArray<float> Screen = Id == EDmNatureMesh::Grass ? TArray<float>{ 1.f, 0.035f, 0.012f } : TArray<float>{ 1.f, 0.04f, 0.01f };
		for (int32 V = 0; V < NumVariants(Id); ++V)
		{
			TArray<FDmNatureMesh> LODs; float Size = 1.f;
			DmNature::BuildCritter(EDmCritter(C), Id == EDmNatureMesh::Grazer ? V % 4 : V, LODs, Size);
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
	// shared tunables last, so the asset materials made above get them too
	SetLeafLook(1.1f, 0.05f, 0.8f, 1.f);
	{
		double Wind = 1.0;   // -naturewind=<strength> (storm-wind checks: trunks, limbs and leaves of the Blender trees)
		FDmShotArgs::Value(TEXT("-naturewind="), Wind);
		SetWind(float(Wind), 1.f);
	}
	SetPlanetFrame(FVector::ZeroVector, FVector::UpVector);
	BuildSeconds = FPlatformTime::Seconds() - T0;
	FString Tris;
	for (int32 I = 0; I < int32(EDmNatureMesh::Num); ++I)
		if (Infos[I].Num()) Tris += FString::Printf(TEXT(" %s=%d"), Name(EDmNatureMesh(I)), Infos[I][0].Tris);
	UE_LOG(LogDmNatureFactory, Display, TEXT("DmNature: factory built in %.2f s (LOD0 tris:%s; %d Blender meshes, trees %s)%s"), BuildSeconds, *Tris, AssetMeshes,
		bTreeAssets ? TEXT("assets") : TEXT("generated"), bMissing ? TEXT(" MATERIALS MISSING") : TEXT(""));
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
	TArray<UMaterialInstanceDynamic*> Ms = { MatLeaf.Get(), MatBark.Get(), MatPalmBark.Get(), MatGrass.Get(), MatAssetLeaf.Get(), MatAssetImpostor.Get() };
	for (UMaterialInstanceDynamic* B : MatAssetBarks) Ms.Add(B);
	for (UMaterialInstanceDynamic* M : Ms)
	{
		if (!M) continue;
		M->SetScalarParameterValue(TEXT("WindStrength"), 100.f * Strength);
		M->SetScalarParameterValue(TEXT("BranchStrength"), 100.f * Strength);
		M->SetScalarParameterValue(TEXT("WindSpeed"), Speed);
	}
}

void UDmNatureFactory::SetPlanetFrame(const FVector& CenterWorld, const FVector& AxisWorld)
{
	const FLinearColor C(float(CenterWorld.X), float(CenterWorld.Y), float(CenterWorld.Z), 0.f);
	const FVector A = AxisWorld.GetSafeNormal();
	const FLinearColor Ax(float(A.X), float(A.Y), float(A.Z), 0.f);
	TArray<UMaterialInstanceDynamic*> Ms = { MatLeaf.Get(), MatBark.Get(), MatPalmBark.Get(), MatGrass.Get(), MatGrazer.Get(), MatBird.Get(), MatFish.Get(),
		MatAssetLeaf.Get(), MatAssetImpostor.Get() };
	for (UMaterialInstanceDynamic* B : MatAssetBarks) Ms.Add(B);
	for (UMaterialInstanceDynamic* B : MatAssetCreatures) Ms.Add(B);
	for (UMaterialInstanceDynamic* M : Ms)
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
	// the Blender leaf atlas and its impostors: the same look, their own brightness scale
	for (UMaterialInstanceDynamic* M : { MatAssetLeaf.Get(), MatAssetImpostor.Get() })
	{
		if (!M) continue;
		M->SetScalarParameterValue(TEXT("Brightness"), Brightness * AssetLeafBrightness);
		M->SetScalarParameterValue(TEXT("LeafSpec"), Spec);
		M->SetScalarParameterValue(TEXT("LeafRough"), Rough);
		M->SetScalarParameterValue(TEXT("TransmissionScale"), Transmission);
	}
}

// ---------------------------------------------------------------- asset pass (Blender families)

namespace
{
	FString NatPath(const TCHAR* Dir, const FString& Name) { return FString::Printf(TEXT("/Game/Nature/%s/%s.%s"), Dir, *Name, *Name); }

	/** Bounds extension for meshes whose material moves them far from their bounds (bird circles, fish paths). */
	void ExtendBounds(UStaticMesh* SM, float Metres)
	{
		if (!SM || Metres <= 0.f) return;
		const FVector Ext(Metres * 100.f);
		SM->SetPositiveBoundsExtension(Ext);
		SM->SetNegativeBoundsExtension(Ext);
		SM->CalculateExtendedBounds();
	}

	FLinearColor ArgColor(const TCHAR* Key, const FLinearColor& Def)
	{
		FString V; TArray<FString> P;
		if (FDmShotArgs::Value(Key, V) && V.ParseIntoArray(P, TEXT(",")) == 3) return FLinearColor(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]));
		return Def;
	}
}

void UDmNatureFactory::BuildAssetTrees()
{
	UTexture2D* LeafAtlas = LoadObject<UTexture2D>(nullptr, *NatPath(TEXT("Trees/Textures"), TEXT("T_TreeLeafAtlas")));
	UTexture2D* ImpAtlas = LoadObject<UTexture2D>(nullptr, *NatPath(TEXT("Trees/Textures"), TEXT("T_TreeImpostors")));
	UTexture2D* ImpNormal = LoadObject<UTexture2D>(nullptr, *NatPath(TEXT("Trees/Textures"), TEXT("T_TreeImpostorsN")));
	if (!LeafAtlas || !ImpAtlas || !ImpNormal)
	{
		UE_LOG(LogDmNatureFactory, Display, TEXT("DmNature: no Blender tree textures in /Game/Nature/Trees (run Scripts/nature-assets.ps1): generated trees"));
		return;
	}
	{
		double B = AssetLeafBrightness; FDmShotArgs::Value(TEXT("-assetleafbright="), B); AssetLeafBrightness = float(B);
	}
	MatAssetLeaf = LoadMat(TEXT("M_NatureLeaf"));
	MatAssetLeaf->SetTextureParameterValue(TEXT("Atlas"), LeafAtlas);
	MatAssetImpostor = LoadMat(TEXT("M_NatureImpostor"));
	MatAssetImpostor->SetTextureParameterValue(TEXT("Atlas"), ImpAtlas);
	MatAssetImpostor->SetTextureParameterValue(TEXT("NormalAtlas"), ImpNormal);
	// leaf look (docs/lab/trees.md 11 / 12.4): golden-hour lit leaves went lime with transmission (0.75, 0.9, 0.3) and a
	// blue-crushing TintB; a warmer, less green transmission, a neutral TintB and a saturation trim bring the lit tops
	// towards the concept (R ~ G). -leafsat= / -leaftrans=r,g,b / -leaftinta= / -leaftintb= override (lookdev).
	double Sat = LeafSaturation; FDmShotArgs::Value(TEXT("-leafsat="), Sat);
	// wave4 verification: transmission (0.62, 0.66, 0.36) and TintB (1.14, 1.0, 0.78) with saturation 0.6 move the lit
	// golden-hour tops from R/G 0.77 to 0.81 and canopy saturation 0.62 -> 0.56 (forest 300 m, -sunel=6)
	const FLinearColor Trans = ArgColor(TEXT("-leaftrans="), FLinearColor(0.62f, 0.66f, 0.36f));
	const FLinearColor TintA = ArgColor(TEXT("-leaftinta="), FLinearColor(0.80f, 0.86f, 0.78f));
	const FLinearColor TintB = ArgColor(TEXT("-leaftintb="), FLinearColor(1.14f, 1.0f, 0.78f));
	for (UMaterialInstanceDynamic* M : { MatAssetLeaf.Get(), MatAssetImpostor.Get() })
	{
		M->SetScalarParameterValue(TEXT("Saturation"), float(Sat));
		M->SetVectorParameterValue(TEXT("Transmission"), Trans);
		M->SetVectorParameterValue(TEXT("TintA"), TintA);
		M->SetVectorParameterValue(TEXT("TintB"), TintB);
	}
	{
		// impostor bark texels (grey limbs, cream palm trunks) at the meshes' bark brightness: -impbarkdim= (lookdev)
		double Dim = 0.5; FDmShotArgs::Value(TEXT("-impbarkdim="), Dim);
		MatAssetImpostor->SetScalarParameterValue(TEXT("ImpostorBarkDim"), float(Dim));
	}
	// LOD screen sizes: the assets carry 1 / 0.45 / 0.2 / 0.08 (docs/lab/trees.md 5), which on the game's crowns (bounds
	// sphere ~12-14 m) keeps LOD2 (1-2 k triangles) out to ~600 m: a 300 m forest view drew ~150 k LOD2 trees and cost
	// 8.5 ms of GPU against 3.7 ms for the stand-ins. The game uses 1 / 0.6 / 0.3 / 0.15 (LOD1 from ~80 m, LOD2 from
	// ~160 m, the baked-normal impostor from ~320 m; the impostor matches LOD2 since the lab's verification, 12.2).
	// -treelods=a,b,c overrides them for this run (lookdev, budget tests).
	TArray<float> Screen = { 1.f, 0.6f, 0.3f, 0.15f };
	{
		FString V; TArray<FString> P;
		if (FDmShotArgs::Value(TEXT("-treelods="), V) && V.ParseIntoArray(P, TEXT(",")) == 3) Screen = { 1.f, FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]) };
	}
	int32 Kinds = 0;
	for (int32 K = 0; K < int32(EDmTreeKind::Num); ++K)
	{
		if (EDmTreeKind(K) == EDmTreeKind::Dead) continue;   // no Blender dead tree yet: generated
		const FString Kn = DmNature::TreeName(EDmTreeKind(K));
		TArray<FDmNatureMeshInfo> Got;
		UMaterialInstanceDynamic* Bark = nullptr;
		for (int32 V = 0; V < NumVariants(EDmNatureMesh(K)); ++V)
		{
			UStaticMesh* SM = LoadObject<UStaticMesh>(nullptr, *NatPath(TEXT("Trees"), FString::Printf(TEXT("SM_Tree_%s_%d"), *Kn, V)));
			if (!SM || !SM->GetRenderData() || SM->GetRenderData()->LODResources.Num() == 0) break;
			if (!Bark)
			{
				UTexture2D* Bt = LoadObject<UTexture2D>(nullptr, *NatPath(TEXT("Trees/Textures"), FString::Printf(TEXT("T_Bark_%s"), *Kn)));
				UTexture2D* Bn = LoadObject<UTexture2D>(nullptr, *NatPath(TEXT("Trees/Textures"), FString::Printf(TEXT("T_Bark_%s_N"), *Kn)));
				Bark = LoadMat(TEXT("M_NatureBark"));
				if (Bt) Bark->SetTextureParameterValue(TEXT("BarkTex"), Bt);
				if (Bn) Bark->SetTextureParameterValue(TEXT("BarkN"), Bn);
				Bark->SetScalarParameterValue(TEXT("BarkTileU"), 1.f);   // asset bark UVs are in tile units already
				Bark->SetScalarParameterValue(TEXT("BarkTileV"), 1.f);
				Bark->SetScalarParameterValue(TEXT("BarkAO"), 1.f);      // vertex-alpha AO (limbs inside the crown)
				Bark->SetScalarParameterValue(TEXT("BarkBright"), 0.55f); // the baked bark atlases read pale grey from the god camera (pale forks at 250 m)
				MatAssetBarks.Add(Bark);
			}
			FStaticMeshRenderData* RD = SM->GetRenderData();
			if (Screen.Num() == 4)
				for (int32 L = 1; L < FMath::Min(RD->LODResources.Num(), 4); ++L) RD->ScreenSize[L].Default = Screen[L];
			FDmNatureMeshInfo Info;
			Info.Mesh = SM;
			const FBox B = SM->GetBoundingBox();
			Info.Height = float(B.Max.Z / 100.0);
			Info.Radius = float(FMath::Max(B.Max.X - B.Min.X, B.Max.Y - B.Min.Y) / 200.0);
			Info.Tris = RD->LODResources[0].GetNumTriangles();
			Info.bAsset = true;
			// slots by name (docs/ASSET-INTEGRATION.md 5.3): Bark, Leaves, Impostor
			Info.Materials.Init(nullptr, SM->GetStaticMaterials().Num());
			auto Slot = [&](const TCHAR* SlotName, UMaterialInterface* M) { const int32 I = SM->GetMaterialIndex(FName(SlotName)); if (Info.Materials.IsValidIndex(I)) Info.Materials[I] = M; };
			Slot(TEXT("Bark"), Bark);
			Slot(TEXT("Leaves"), MatAssetLeaf);
			Slot(TEXT("Impostor"), MatAssetImpostor);
			Keep.Add(SM);
			Got.Add(Info);
		}
		if (Got.Num() == NumVariants(EDmNatureMesh(K)))
		{
			Infos[K] = MoveTemp(Got);
			AssetMeshes += Infos[K].Num();
			++Kinds;
		}
		else UE_LOG(LogDmNatureFactory, Warning, TEXT("DmNature: Blender %s trees incomplete (%d of %d): generated stand-ins for that kind"), *Kn, Got.Num(), NumVariants(EDmNatureMesh(K)));
	}
	bTreeAssets = Kinds == 5;
}

void UDmNatureFactory::BuildAssetFauna()
{
	// herd species (EDmGrazer order), birds (0 land, 1 coast / wetland), fish (0, 1: the shoal fish at two sizes)
	const TCHAR* Herd[int32(EDmGrazer::Num)] = { TEXT("Deer_Doe"), TEXT("Grazer_Antelope"), TEXT("Grazer_Wildebeest"), TEXT("Grazer_Bison"),
		TEXT("Deer_Stag"), TEXT("BigCat_Lioness"), TEXT("BigCat_Lion") };
	struct FA { EDmNatureMesh Id; const TCHAR* Name; float Scale; };
	TArray<FA> List;
	for (const TCHAR* H : Herd) List.Add({ EDmNatureMesh::Grazer, H, 1.f });
	// the god camera sees birds and fish from 100 m - 2 km: shown larger than life (ASSET-INTEGRATION 4.7: flock / shoal
	// scale ~3, the stand-ins' 1.8 m wingspan), through the material (MeshScale) so the paths keep their size
	List.Add({ EDmNatureMesh::Bird, TEXT("Bird_Starling"), 4.f });
	List.Add({ EDmNatureMesh::Bird, TEXT("Bird_Egret"), 1.3f });
	List.Add({ EDmNatureMesh::Fish, TEXT("Fish_Sardine"), 3.f });
	List.Add({ EDmNatureMesh::Fish, TEXT("Fish_Sardine"), 3.6f });
	TArray<FDmNatureMeshInfo> Got[int32(EDmNatureMesh::Num)];
	for (const FA& A : List)
	{
		UStaticMesh* SM = LoadObject<UStaticMesh>(nullptr, *NatPath(TEXT("Animals"), FString(TEXT("SM_Animal_")) + A.Name));
		if (!SM || !SM->GetRenderData() || SM->GetRenderData()->LODResources.Num() == 0) continue;
		FDmNatureMeshInfo Info;
		Info.Mesh = SM;
		const FBox B = SM->GetBoundingBox();
		Info.Height = float((B.Max.Z - B.Min.Z) / 100.0) * A.Scale;
		Info.Radius = float(FMath::Max(B.Max.X - B.Min.X, B.Max.Y - B.Min.Y) / 200.0) * A.Scale;
		Info.Tris = SM->GetRenderData()->LODResources[0].GetNumTriangles();
		Info.bAsset = true;
		UMaterialInterface* M = MatGrazer;
		if (A.Id != EDmNatureMesh::Grazer)
		{
			// one dynamic instance per (kind, scale): MeshScale is a material parameter
			UMaterialInstanceDynamic* D = LoadMat(TEXT("M_NatureCreature"));
			D->SetScalarParameterValue(TEXT("AnimMode"), A.Id == EDmNatureMesh::Bird ? 1.f : 2.f);
			D->SetScalarParameterValue(TEXT("MeshYaw90"), 1.f);
			D->SetScalarParameterValue(TEXT("MeshScale"), A.Scale);
			MatAssetCreatures.Add(D);
			M = D;
			Info.YawDeg = 90.f;
			ExtendBounds(SM, A.Id == EDmNatureMesh::Bird ? 130.f * 1.2f : 30.f);
		}
		else ExtendBounds(SM, 3.f);
		Info.Materials.Init(M, FMath::Max(1, SM->GetStaticMaterials().Num()));
		Keep.Add(SM);
		Got[int32(A.Id)].Add(Info);
	}
	for (EDmNatureMesh Id : { EDmNatureMesh::Grazer, EDmNatureMesh::Bird, EDmNatureMesh::Fish })
	{
		if (Got[int32(Id)].Num() == NumVariants(Id)) { Infos[int32(Id)] = MoveTemp(Got[int32(Id)]); AssetMeshes += Infos[int32(Id)].Num(); }
		else UE_LOG(LogDmNatureFactory, Display, TEXT("DmNature: Blender %s incomplete (%d of %d): generated stand-ins"), Name(Id), Got[int32(Id)].Num(), NumVariants(Id));
	}
}

