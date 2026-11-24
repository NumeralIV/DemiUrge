// DEMIURGE — Game/Towns/DmTownsAssets.cpp : see the header. The table is docs/ASSET-INTEGRATION.md 4.1 (kit id x age -> asset).

#include "Game/Towns/DmTownsAssets.h"
#include "Game/Core/DmScenario.h"

#include "Engine/StaticMesh.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmTownsAssets, Log, All);

namespace
{
	/** One row: a kit id (variant V, -1 = every variant) over the ages A0..A1 stands as one of Names (comma list; "{H}"
	 *  expands to Low / Mid / High by the settlement's tier: tier <= 2, 3, 4). */
	struct FRow { EDmTownMesh Id; int8 V; int8 A0, A1; const TCHAR* Names; };

	using M = EDmTownMesh;
	const FRow GRows[] = {
		// dwellings
		{ M::Tent, 0, 0, 6, TEXT("Age0_LeanTo_A") }, { M::Tent, 1, 0, 6, TEXT("Age0_Hut_C") }, { M::Tent, 2, 0, 6, TEXT("Age0_LeanTo_A") },
		{ M::Hut, 0, 0, 6, TEXT("Age0_Hut_A") }, { M::Hut, 1, 0, 6, TEXT("Age0_Hut_B") }, { M::Hut, 2, 0, 6, TEXT("Age0_Hut_C") }, { M::Hut, 3, 0, 6, TEXT("Age0_Hut_B") },
		{ M::Longhouse, 0, 0, 1, TEXT("Age0_Longhouse_A") }, { M::Longhouse, 1, 0, 1, TEXT("Age0_Longhouse_B") }, { M::Longhouse, -1, 2, 6, TEXT("Age2_House_B") },
		{ M::MudHouse, 0, 0, 6, TEXT("Age1_House_A") }, { M::MudHouse, 1, 0, 6, TEXT("Age1_House_B") }, { M::MudHouse, 2, 0, 6, TEXT("Age1_House_C") }, { M::MudHouse, 3, 0, 6, TEXT("Age1_House_D") },
		{ M::Cottage, -1, 0, 1, TEXT("Age1_House_A,Age1_House_B,Age1_House_D") }, { M::Cottage, -1, 2, 4, TEXT("Age2_House_A,Age2_House_B,Age2_House_C,Age2_House_D") },
		{ M::Cottage, -1, 5, 6, TEXT("Age2_House_D") },
		{ M::Townhouse, -1, 0, 2, TEXT("Age2_House_C") },
		{ M::Townhouse, 0, 3, 6, TEXT("Age3_Townhouse_A") }, { M::Townhouse, 1, 3, 6, TEXT("Age3_Townhouse_B") }, { M::Townhouse, 2, 3, 6, TEXT("Age3_Townhouse_C") },
		{ M::Townhouse, 3, 3, 6, TEXT("Age3_Townhouse_D") }, { M::Townhouse, 4, 3, 6, TEXT("Age3_Townhouse_A") }, { M::Townhouse, 5, 3, 6, TEXT("Age3_Townhouse_C") },
		{ M::Rowhouse, 0, 0, 6, TEXT("Age4_Terrace_A") }, { M::Rowhouse, 1, 0, 6, TEXT("Age4_Terrace_B") }, { M::Rowhouse, 2, 0, 6, TEXT("Age4_Terrace_C") }, { M::Rowhouse, 3, 0, 6, TEXT("Age4_Terrace_D") },
		{ M::Apartment, -1, 0, 4, TEXT("Age4_Terrace_C,Age4_Terrace_D") },
		{ M::Apartment, -1, 5, 5, TEXT("Age5_ApartmentBrick_{H},Age5_ApartmentPoint_{H},Age5_ApartmentSlab_{H}") },
		{ M::Apartment, 0, 6, 6, TEXT("Age6_GreenBlock_A") }, { M::Apartment, 1, 6, 6, TEXT("Age6_GreenBlock_B") }, { M::Apartment, 2, 6, 6, TEXT("Age6_GreenBlock_C") }, { M::Apartment, 3, 6, 6, TEXT("Age6_GreenBlock_D") },
		{ M::Tower, -1, 0, 5, TEXT("Age5_OfficeGlass_{H},Age5_OfficeRibbon_{H},Age5_OfficeRound_{H}") },
		{ M::Tower, 0, 6, 6, TEXT("Age6_ArcologyTower_A") }, { M::Tower, 1, 6, 6, TEXT("Age6_ArcologyTower_B") }, { M::Tower, 2, 6, 6, TEXT("Age6_ArcologyTower_C") },
		{ M::Tower, 3, 6, 6, TEXT("Age6_ArcologyTower_D") }, { M::Tower, 4, 6, 6, TEXT("Age6_ArcologyTower_E") },
		{ M::Barn, -1, 0, 4, TEXT("Age2_House_D") },
		// civic, faith, trade, crafts
		{ M::Campfire, -1, 0, 6, TEXT("Age0_FirePit_A") },
		{ M::Stones, -1, 0, 6, TEXT("Age0_StandingStones_A") },
		{ M::Shrine, -1, 0, 6, TEXT("Shrine") },
		{ M::TempleEarly, -1, 0, 6, TEXT("Age1_Temple_A") },
		{ M::TempleClassic, 0, 0, 6, TEXT("Age2_Temple") }, { M::TempleClassic, 1, 0, 6, TEXT("Age3_CivicDome") },
		{ M::Granary, -1, 0, 0, TEXT("Age0_StoragePit_A,Age0_StoragePit_B") }, { M::Granary, 0, 1, 6, TEXT("Age1_Granary_A") },
		{ M::Market, -1, 0, 6, TEXT("Age2_Market") },
		{ M::WallSeg, -1, 0, 0, TEXT("Age0_Palisade_Seg") }, { M::WallSeg, -1, 1, 1, TEXT("Age1_Rampart_Straight") }, { M::WallSeg, -1, 2, 6, TEXT("Age2_WallSeg") },
		{ M::WallTower, -1, 0, 1, TEXT("Age1_Rampart_End") }, { M::WallTower, -1, 2, 2, TEXT("Age2_WallTower_A") }, { M::WallTower, -1, 3, 6, TEXT("Age2_WallTower_B") },
		{ M::Gatehouse, -1, 0, 0, TEXT("Age0_Palisade_Gate") }, { M::Gatehouse, -1, 1, 1, TEXT("Age1_Gate_A") }, { M::Gatehouse, -1, 2, 6, TEXT("Age2_Gatehouse") },
		// (piers stay generated: the planner stretches a pier along X to the water depth; Age3_Pier / Age1_Landing_A run along Y at a fixed length)
		{ M::Lighthouse, -1, 0, 6, TEXT("Age3_Lighthouse") },
		{ M::Crane, -1, 0, 6, TEXT("Age3_PortCrane") },
		{ M::Warehouse, 0, 0, 6, TEXT("Age3_Warehouse_A,Age3_Warehouse_B") }, { M::Warehouse, 1, 0, 6, TEXT("Age4_Warehouse_A,Age4_Warehouse_B") }, { M::Warehouse, 2, 0, 6, TEXT("Age4_Warehouse_B") },
		{ M::Factory, 0, 0, 6, TEXT("Age4_Factory_A") }, { M::Factory, 1, 0, 6, TEXT("Age4_Factory_B") }, { M::Factory, 2, 0, 6, TEXT("Age4_Factory_A") },
		{ M::Stack, -1, 0, 6, TEXT("Age4_Stack_A") },
		{ M::MineHead, 1, 0, 6, TEXT("Age4_MineHead_A") }, { M::Spoil, -1, 0, 6, TEXT("Age4_Spoil_A,Age4_Spoil_B") },
		{ M::Academy, 0, 0, 6, TEXT("Age2_Academy") }, { M::Academy, 1, 0, 6, TEXT("Age5_University_Std") },
		{ M::Reactor, -1, 0, 6, TEXT("Age5_PowerPlant_Std") }, { M::CoolingTower, -1, 0, 6, TEXT("Age5_CoolingTower_Std") },
		{ M::Gantry, -1, 0, 6, TEXT("Age6_LaunchPad") }, { M::Rocket, -1, 0, 6, TEXT("Age6_Rocket") },
		{ M::Wonder, -1, 0, 0, TEXT("Wonder_Hearth") }, { M::Wonder, -1, 1, 1, TEXT("Wonder_GreatHive") }, { M::Wonder, -1, 2, 2, TEXT("Wonder_SevenWatchers") },
		{ M::Wonder, -1, 3, 3, TEXT("Wonder_SeaGate") }, { M::Wonder, -1, 4, 4, TEXT("Wonder_ChoirOfChimneys") }, { M::Wonder, -1, 5, 5, TEXT("Wonder_EarOfTheSky") },
		{ M::Wonder, -1, 6, 6, TEXT("Wonder_Ladder") },
		// ships (moored and at sea) and land units
		{ M::Boat, 0, 0, 6, TEXT("Ship_ReedBoat_A") }, { M::Boat, 1, 0, 6, TEXT("Ship_Canoe_A") },
		{ M::Galley, -1, 0, 6, TEXT("Ship_Galley_A") },
		{ M::SailShip, 0, 0, 6, TEXT("Ship_Cog_A") }, { M::SailShip, 1, 0, 6, TEXT("Ship_SailShip_A") },
		{ M::Steamer, -1, 0, 6, TEXT("Ship_Steamship_A") },
		{ M::CargoShip, 0, 0, 6, TEXT("Ship_ContainerShip_A") }, { M::CargoShip, 1, 0, 6, TEXT("Ship_FastFreighter_A") },
		{ M::Settlers, -1, 0, 1, TEXT("Unit_Settlers_Ancient") }, { M::Settlers, -1, 2, 3, TEXT("Unit_Settlers_Classical") },
		{ M::Settlers, -1, 4, 5, TEXT("Unit_Settlers_Modern") }, { M::Settlers, -1, 6, 6, TEXT("Unit_Settlers_Space") },
		{ M::Caravan, -1, 0, 1, TEXT("Unit_Traders_Ancient") }, { M::Caravan, -1, 2, 3, TEXT("Unit_Traders_Classical") },
		{ M::Caravan, -1, 4, 5, TEXT("Unit_Traders_Modern") }, { M::Caravan, -1, 6, 6, TEXT("Unit_Traders_Space") },
		{ M::Army, -1, 0, 1, TEXT("Unit_Army_Ancient") }, { M::Army, -1, 2, 3, TEXT("Unit_Army_Classical") },
		{ M::Army, -1, 4, 5, TEXT("Unit_Army_Modern") }, { M::Army, -1, 6, 6, TEXT("Unit_Army_Space") },
		{ M::Prophet, -1, 0, 6, TEXT("Unit_Prophet_A") },
	};

	const TCHAR* const GStateSuffix[] = { TEXT("_Intact"), TEXT("_Construction"), TEXT("_Ruin") };
	// culture-style instance suffixes, in the order the families name them (ASSET-INTEGRATION 4.2)
	const TCHAR* const GStyleSfx[4][3] = {
		{ TEXT("_Base"), TEXT("_Neutral"), TEXT("_Default") },
		{ TEXT("_A"), TEXT("_CultureA"), TEXT("_CliffTerrace") },
		{ TEXT("_B"), TEXT("_CultureB"), TEXT("_LagoonWeaver") },
		{ TEXT("_C"), TEXT("_CultureC"), TEXT("_RingBuilder") } };
	const TCHAR* const GClimSfx[4] = { TEXT("_Temperate"), TEXT("_Arid"), TEXT("_Tropical"), TEXT("_Cold") };

	FString FamilyOf(const FString& Name)
	{
		int32 U = INDEX_NONE;
		return Name.FindChar(TEXT('_'), U) ? Name.Left(U) : Name;
	}

	/** Object path for a package path (/Game/A/B -> /Game/A/B.B). */
	FString ObjPath(const FString& Pkg) { return Pkg + TEXT(".") + FPackageName::GetShortName(Pkg); }
}

void FDmTownsAssets::Build(TArray<TObjectPtr<UObject>>& Keep)
{
	const double T0 = FPlatformTime::Seconds();
	int32 Iv = 1;
	if (FDmShotArgs::Value(TEXT("-townassets="), Iv)) bEnabled = Iv != 0;
	Map.Reset(); Unique.Reset();
	if (!bEnabled) { UE_LOG(LogDmTownsAssets, Display, TEXT("DmTowns: real assets off (-townassets=0): generated stand-ins only")); return; }

	TMap<FString, bool> Exists;
	auto Has = [&Exists](const FString& Pkg) -> bool
	{
		if (const bool* B = Exists.Find(Pkg)) return *B;
		const bool B = FPackageName::DoesPackageExist(Pkg);
		Exists.Add(Pkg, B);
		return B;
	};
	TMap<FString, UObject*> Loaded;
	auto Load = [&](const FString& Pkg) -> UObject*
	{
		if (UObject** O = Loaded.Find(Pkg)) return *O;
		UObject* O = Has(Pkg) ? LoadObject<UObject>(nullptr, *ObjPath(Pkg)) : nullptr;
		Loaded.Add(Pkg, O);
		if (O) Keep.Add(O);
		return O;
	};
	auto MeshPkg = [](const FString& Name, const TCHAR* Sfx) -> FString
	{
		const FString Fam = FamilyOf(Name);
		if (Fam == TEXT("Shrine")) return FString::Printf(TEXT("/Game/Assets/Shrine/SM_%s%s"), *Name, Sfx);
		return FString::Printf(TEXT("/Game/Assets/%s/Meshes/SM_%s%s"), *Fam, *Name, Sfx);
	};
	// one asset, all its states and material variants (cached by name + height class)
	TMap<FString, FDmTownAsset> Made;
	TSet<FString> Missing;
	auto MakeAsset = [&](const FString& Name, FDmTownAsset& Out) -> bool
	{
		if (const FDmTownAsset* A = Made.Find(Name)) { Out = *A; return A->Mesh[0] != nullptr; }
		FDmTownAsset A;
		A.Name = Name;
		for (int32 S = 0; S < int32(EDmTownState::Num); ++S) A.Mesh[S] = Cast<UStaticMesh>(Load(MeshPkg(Name, GStateSuffix[S])));
		if (!A.Mesh[0]) A.Mesh[0] = Cast<UStaticMesh>(Load(MeshPkg(Name, TEXT(""))));   // props and units have no state suffix
		if (!A.Mesh[0] && FamilyOf(Name) == TEXT("Shrine")) A.Mesh[0] = Cast<UStaticMesh>(Load(FString::Printf(TEXT("/Game/Assets/Shrine/Meshes/SM_%s_Intact"), *Name)));
		if (A.Mesh[0])
		{
			const FBox B = A.Mesh[0]->GetBoundingBox();
			A.W = float(2.0 * FMath::Max(FMath::Abs(B.Min.X), FMath::Abs(B.Max.X)) / 100.0);
			A.D = float(2.0 * FMath::Max(FMath::Abs(B.Min.Y), FMath::Abs(B.Max.Y)) / 100.0);
			A.H = float(FMath::Max(B.Max.Z, 50.0) / 100.0);
			A.Tris = A.Mesh[0]->GetNumTriangles(0);
			// material variants of every slot: climate first (Age0, wonders-a), else the culture style. (verifier fix) per state,
			// from that state's own slots: the _Construction / _Ruin meshes of 43 assets drop or reorder slots
			for (int32 St = 0; St < int32(EDmTownState::Num); ++St)
			{
				if (!A.Mesh[St]) continue;
				const TArray<FStaticMaterial>& Sm = A.Mesh[St]->GetStaticMaterials();
				for (int32 Cs = 0; Cs < int32(EDmCultureStyle::Num); ++Cs)
					for (int32 Cl = 0; Cl < 4; ++Cl)
					{
						TArray<UMaterialInterface*>& Mats = A.Mats[St][Cs][Cl];
						Mats.SetNumZeroed(Sm.Num());
						for (int32 I = 0; I < Sm.Num(); ++I)
						{
							const UMaterialInterface* Base = Sm[I].MaterialInterface;
							if (!Base) continue;
							const FString BasePkg = Base->GetPackage()->GetName();
							UMaterialInterface* Pick = Cast<UMaterialInterface>(Load(BasePkg + GClimSfx[Cl]));
							for (int32 K = 0; K < 3 && !Pick; ++K) Pick = Cast<UMaterialInterface>(Load(BasePkg + GStyleSfx[Cs][K]));
							Mats[I] = Pick;
							NumMats += Pick ? 1 : 0;
						}
					}
			}
			for (int32 S = 0; S < int32(EDmTownState::Num); ++S) if (A.Mesh[S]) { Unique.AddUnique(A.Mesh[S]); ++NumMeshes; }
		}
		else Missing.Add(Name);
		Made.Add(Name, A);
		Out = A;
		return A.Mesh[0] != nullptr;
	};

	for (const FRow& R : GRows)
	{
		TArray<FString> Names;
		FString(R.Names).ParseIntoArray(Names, TEXT(","));
		const bool bH = FString(R.Names).Contains(TEXT("{H}"));
		const int32 Nv = UDmTownsFactory::NumVariants(R.Id);
		for (int32 V = 0; V < Nv; ++V)
		{
			if (R.V >= 0 && R.V != V) continue;
			for (int32 Hc = bH ? 1 : 0; Hc <= (bH ? 3 : 0); ++Hc)
			{
				static const TCHAR* const HName[4] = { TEXT(""), TEXT("Low"), TEXT("Mid"), TEXT("High") };
				TArray<FDmTownAsset> Alts;
				for (const FString& N0 : Names)
				{
					FDmTownAsset A;
					if (MakeAsset(N0.Replace(TEXT("{H}"), HName[Hc]), A)) Alts.Add(MoveTemp(A));
				}
				if (Alts.Num() == 0) continue;
				for (int32 Age = R.A0; Age <= R.A1; ++Age) Map.Add(Key(R.Id, V, Age, Hc), Alts);
			}
		}
	}
	NumAssets = 0;
	for (const TPair<FString, FDmTownAsset>& A : Made) NumAssets += A.Value.Mesh[0] ? 1 : 0;
	NumMissing = Missing.Num();
	int32 NumNanite = 0;
	for (const UStaticMesh* Um : Unique) NumNanite += (Um && Um->HasValidNaniteData()) ? 1 : 0;
	MissingList = FString::Join(Missing.Array(), TEXT(", "));
	Seconds = FPlatformTime::Seconds() - T0;
	UE_LOG(LogDmTownsAssets, Display, TEXT("DmTowns: real assets: %d of %d mapped assets loaded (%d meshes with their states, %d Nanite, %d slot variants), %d table entries, %.2f s%s%s"),
		NumAssets, Made.Num(), NumMeshes, NumNanite, NumMats, Map.Num(), Seconds, NumMissing ? TEXT("; not imported (stand-ins draw): ") : TEXT(""), *MissingList);
}

const TArray<FDmTownAsset>* FDmTownsAssets::Find(EDmTownMesh Id, int32 V, int32 Age, int32 Tier) const
{
	if (!bEnabled || Map.Num() == 0) return nullptr;
	const int32 A = FMath::Clamp(Age, 0, 6);
	const int32 Hc = Tier <= 2 ? 1 : (Tier == 3 ? 2 : 3);
	if (const TArray<FDmTownAsset>* L = Map.Find(Key(Id, V, A, Hc))) return L;
	return Map.Find(Key(Id, V, A, 0));
}

const FDmTownAsset* FDmTownsAssets::Pick(EDmTownMesh Id, int32 V, int32 Age, int32 Tier, int32 Sub) const
{
	const TArray<FDmTownAsset>* L = Find(Id, V, Age, Tier);
	if (!L || L->Num() == 0) return nullptr;
	return &(*L)[FMath::Abs(Sub) % L->Num()];
}
