#include "LifeWorld.h"
#include "LifeMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Async/ParallelFor.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// ============================================================ grid

float FLifeGrid::Bilinear(double X, double Y) const
{
	const double Fx = (X + Half) / Step, Fy = (Y + Half) / Step;
	const int32 I = FMath::FloorToInt(Fx), J = FMath::FloorToInt(Fy);
	const float U = float(Fx - I), W = float(Fy - J);
	return FMath::Lerp(FMath::Lerp(At(I, J), At(I + 1, J), U), FMath::Lerp(At(I, J + 1), At(I + 1, J + 1), U), W);
}

float FLifeGrid::Cubic(double X, double Y) const
{
	const double Fx = (X + Half) / Step, Fy = (Y + Half) / Step;
	const int32 I = FMath::FloorToInt(Fx), J = FMath::FloorToInt(Fy);
	const float U = float(Fx - I), W = float(Fy - J);
	auto CR = [](float P0, float P1, float P2, float P3, float T)
	{
		return P1 + 0.5f * T * (P2 - P0 + T * (2.f * P0 - 5.f * P1 + 4.f * P2 - P3 + T * (3.f * (P1 - P2) + P3 - P0)));
	};
	float Row[4];
	for (int32 K = 0; K < 4; ++K)
		Row[K] = CR(At(I - 1, J - 1 + K), At(I, J - 1 + K), At(I + 1, J - 1 + K), At(I + 2, J - 1 + K), U);
	return CR(Row[0], Row[1], Row[2], Row[3], W);
}

void FLifeGrid::Blur(int32 Radius)
{
	if (Radius <= 0) return;
	TArray<float> Wt;
	const float Sigma = Radius * 0.5f + 0.5f;
	for (int32 K = -Radius; K <= Radius; ++K) Wt.Add(FMath::Exp(-0.5f * K * K / (Sigma * Sigma)));
	TArray<float> Tmp; Tmp.SetNumUninitialized(V.Num());
	const FLifeGrid& Cg = *this;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		ParallelFor(N, [&](int32 J)
		{
			for (int32 I = 0; I < N; ++I)
			{
				float S = 0.f, Ws = 0.f;
				for (int32 K = -Radius; K <= Radius; ++K)
				{
					const float W = Wt[K + Radius];
					S += W * (Pass == 0 ? Cg.At(I + K, J) : Cg.At(J, I + K)); // second pass is transposed
					Ws += W;
				}
				if (Pass == 0) Tmp[J * N + I] = S / Ws; else Tmp[I * N + J] = S / Ws;
			}
		});
		V = Tmp;
	}
}

// ============================================================ actor

ALifeWorld::ALifeWorld()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);
}

FVector3d ALifeWorld::SurfaceM(double X, double Y, double H) const
{
	const FVector3d Dir = FVector3d(X, Y, Rm).GetSafeNormal();
	return Dir * (Rm + H) - FVector3d(0, 0, Rm);
}

FQuat4d ALifeWorld::UpQuat(double X, double Y) const
{
	return FQuat4d::FindBetweenNormals(FVector3d(0, 0, 1), FVector3d(X, Y, Rm).GetSafeNormal());
}

float ALifeWorld::BaseHeightAt(double X, double Y) const
{
	const float Base = GElev.Cubic(X, Y) * 200.f;
	const float Land = LifeSmooth(-30.f, 40.f, Base);
	const float N1 = LifeFbm2(float(X / 2600.0), float(Y / 2600.0), 5, 11) * 30.f * (0.3f + 0.7f * Land);
	const float Ridge = (1.f - FMath::Abs(LifeFbm2(float(X / 1100.0), float(Y / 1100.0), 4, 13)) * 2.f) - 0.35f;
	const float Hilly = LifeSmooth(20.f, 260.f, Base) + 0.3f;
	return Base + N1 + Ridge * 34.f * Hilly * Land;
}

float ALifeWorld::HeightAt(double X, double Y) const
{
	float H = BaseHeightAt(X, Y);
	const float Land = LifeSmooth(-10.f, 15.f, H);
	H += LifeFbm2(float(X / 170.0), float(Y / 170.0), 3, 17) * 2.4f * Land + LifeFbm2(float(X / 37.0), float(Y / 37.0), 2, 19) * 0.35f;
	// gentle beaches: compress the band around sea level
	H -= 0.45f * H * FMath::Exp(-(H / 5.f) * (H / 5.f));
	for (const FPad& P : Pads)
	{
		const double Dd = FVector2D::Distance(P.C, FVector2D(X, Y));
		if (Dd >= P.R1) continue;
		const float W = LifeSmooth(float(P.R1), float(P.R0), float(Dd));
		const float Target = P.H + LifeFbm2(float(X / 170.0), float(Y / 170.0), 3, 17) * 0.6f;
		H = FMath::Lerp(H, Target, W * LifeSmooth(-1.5f, 0.8f, H)); // the sea stays: the pad edge becomes a quay
	}
	return H;
}

FVector3f ALifeWorld::Slope(double X, double Y, double Eps) const
{
	const float Hx = HeightAt(X + Eps, Y) - HeightAt(X - Eps, Y);
	const float Hy = HeightAt(X, Y + Eps) - HeightAt(X, Y - Eps);
	return FVector3f(-Hx, -Hy, float(2.0 * Eps)).GetSafeNormal();
}

bool ALifeWorld::Build(const FString& DataPath, int32 SiteCell)
{
	const double T0 = FPlatformTime::Seconds();
	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("-treehalf="), TreeHalf);
	FParse::Value(Cmd, TEXT("-treedensity="), TreeDensity);
	FParse::Value(Cmd, TEXT("-treecull="), CullTrees);
	bTowns = !FParse::Param(Cmd, TEXT("notowns"));
	bTrees = !FParse::Param(Cmd, TEXT("notrees"));

	if (!D.Load(DataPath)) { UE_LOG(LogTemp, Error, TEXT("LifeWorld: cannot load %s"), *DataPath); return false; }
	if (!SampleFields(SiteCell)) return false;
	const double T1 = FPlatformTime::Seconds();

	LuRoad.SetNumZeroed(LuN * LuN); LuPaved.SetNumZeroed(LuN * LuN); LuField.SetNumZeroed(LuN * LuN);
	LuYard.SetNumZeroed(LuN * LuN); LuNoTree.SetNumZeroed(LuN * LuN); LuBuilt.SetNumZeroed(LuN * LuN);
	LuFieldCol.Init(FColor(0, 0, 0, 0), LuN * LuN);

	Tex.Build();
	MakeMaterials();
	if (bTowns) PlanSettlements();
	if (FParse::Param(Cmd, TEXT("lineup"))) StampDisc(LuNoTree, FVector2D(0, 0), 140.0, 1);
	const double T2 = FPlatformTime::Seconds();
	BuildTerrain();
	BuildWater();
	const double T3 = FPlatformTime::Seconds();
	if (bTrees) BuildTrees();
	const double T4 = FPlatformTime::Seconds();
	if (bTowns) BuildSettlements();
	MakeLandUseTextures();
	const double T5 = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Display, TEXT("LIFESTATS build: fields %.2fs, tex+plan %.2fs, terrain+water %.2fs, trees %.2fs, towns+landuse %.2fs"),
		T1 - T0, T2 - T1, T3 - T2, T4 - T3, T5 - T4);
	UE_LOG(LogTemp, Display, TEXT("LIFESTATS %s"), *StatsLine());
	return true;
}

FString ALifeWorld::StatsLine() const
{
	int32 Total = 0;
	FString S;
	for (int32 K = 0; K < int32(ETreeKind::Num); ++K)
	{
		Total += TreeCount[K];
		S += FString::Printf(TEXT("%s=%d(LOD0 %d tris) "), LifeTreeName(ETreeKind(K)), TreeCount[K], TreeLOD0Tris[K]);
	}
	return FString::Printf(TEXT("trees total=%d [%s] terrainTris=%d waterTris=%d townBuildings=%d townTris=%d"), Total, *S, TerrainTris, WaterTris, TownBuildings, TownTris);
}

// ============================================================ planet fields

bool ALifeWorld::SampleFields(int32 SiteCell)
{
	if (SiteCell < 0 || SiteCell >= D.N) SiteCell = 0;
	auto UE = [this](int32 C) { const FVector3f& P = D.Pos[C]; return FVector3d(P.X, P.Z, P.Y).GetSafeNormal(); };
	const FVector3d S = UE(SiteCell);
	SiteQ = FQuat4d::FindBetweenNormals(S, FVector3d(0, 0, 1));

	// gnomonic coordinates (metres) of every cell on the near side
	TArray<FVector2D> G; G.SetNumUninitialized(D.N);
	TArray<uint8> Ok; Ok.SetNumZeroed(D.N);
	for (int32 C = 0; C < D.N; ++C)
	{
		const FVector3d P = SiteQ.RotateVector(UE(C));
		if (P.Z > 0.5) { G[C] = FVector2D(P.X / P.Z * Rm, P.Y / P.Z * Rm); Ok[C] = 1; }
	}
	// bucket triangles
	const double BHalf = 80000.0, BSize = 2500.0;
	const int32 NB = int32(2 * BHalf / BSize);
	TArray<TArray<int32>> Buckets; Buckets.SetNum(NB * NB);
	for (int32 T = 0; T < D.NTri; ++T)
	{
		const int32 A = D.Tris[3 * T], B = D.Tris[3 * T + 1], C = D.Tris[3 * T + 2];
		if (!Ok[A] || !Ok[B] || !Ok[C]) continue;
		const double X0 = FMath::Min3(G[A].X, G[B].X, G[C].X), X1 = FMath::Max3(G[A].X, G[B].X, G[C].X);
		const double Y0 = FMath::Min3(G[A].Y, G[B].Y, G[C].Y), Y1 = FMath::Max3(G[A].Y, G[B].Y, G[C].Y);
		if (X1 < -BHalf || Y1 < -BHalf || X0 > BHalf || Y0 > BHalf) continue;
		const int32 I0 = FMath::Clamp(int32((X0 + BHalf) / BSize), 0, NB - 1), I1 = FMath::Clamp(int32((X1 + BHalf) / BSize), 0, NB - 1);
		const int32 J0 = FMath::Clamp(int32((Y0 + BHalf) / BSize), 0, NB - 1), J1 = FMath::Clamp(int32((Y1 + BHalf) / BSize), 0, NB - 1);
		for (int32 J = J0; J <= J1; ++J) for (int32 I = I0; I <= I1; ++I) Buckets[J * NB + I].Add(T);
	}
	const int32 N = 561; const double Half = 70000.0;
	for (FLifeGrid* Gr : { &GElev, &GTemp, &GMoist, &GVeg, &GHab }) Gr->Init(N, Half);
	ParallelFor(N, [&](int32 J)
	{
		for (int32 I = 0; I < N; ++I)
		{
			const FVector2D P(-Half + I * GElev.Step, -Half + J * GElev.Step);
			const int32 Bi = FMath::Clamp(int32((P.X + BHalf) / BSize), 0, NB - 1), Bj = FMath::Clamp(int32((P.Y + BHalf) / BSize), 0, NB - 1);
			for (int32 T : Buckets[Bj * NB + Bi])
			{
				const int32 A = D.Tris[3 * T], B = D.Tris[3 * T + 1], C = D.Tris[3 * T + 2];
				const FVector2D V0 = G[B] - G[A], V1 = G[C] - G[A], V2 = P - G[A];
				const double Den = V0.X * V1.Y - V1.X * V0.Y;
				if (FMath::Abs(Den) < 1e-9) continue;
				const double Wb = (V2.X * V1.Y - V1.X * V2.Y) / Den, Wc = (V0.X * V2.Y - V2.X * V0.Y) / Den, Wa = 1.0 - Wb - Wc;
				if (Wa < -1e-6 || Wb < -1e-6 || Wc < -1e-6) continue;
				auto L3 = [&](const TArray<float>& F) { return float(Wa * F[A] + Wb * F[B] + Wc * F[C]); };
				GElev.At(I, J) = L3(D.Elev); GTemp.At(I, J) = L3(D.Temp); GMoist.At(I, J) = L3(D.Moist); GVeg.At(I, J) = L3(D.Veg); GHab.At(I, J) = L3(D.Hab);
				break;
			}
		}
	});
	GElev.Blur(4); GTemp.Blur(4); GMoist.Blur(4); GVeg.Blur(3); GHab.Blur(3);
	UE_LOG(LogTemp, Display, TEXT("LifeWorld: site cell %d elev %.2f temp %.1f moist %.2f veg %.2f hab %.2f"), SiteCell,
		GElev.Bilinear(0, 0), GTemp.Bilinear(0, 0), GMoist.Bilinear(0, 0), GVeg.Bilinear(0, 0), GHab.Bilinear(0, 0));
	return true;
}

// ============================================================ materials

void ALifeWorld::MakeMaterials()
{
	auto Load = [this](const TCHAR* Path) -> UMaterialInstanceDynamic*
	{
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, Path);
		if (!M) { UE_LOG(LogTemp, Error, TEXT("LifeWorld: missing material %s"), Path); M = UMaterial::GetDefaultMaterial(MD_Surface); }
		return UMaterialInstanceDynamic::Create(M, this);
	};
	MatLeaf = Load(TEXT("/Game/Life/M_LifeLeaf.M_LifeLeaf"));
	MatBark = Load(TEXT("/Game/Life/M_LifeBark.M_LifeBark"));
	MatPalmBark = Load(TEXT("/Game/Life/M_LifeBark.M_LifeBark"));
	MatTerrain = Load(TEXT("/Game/Life/M_LifeTerrain.M_LifeTerrain"));
	MatWater = Load(TEXT("/Game/Life/M_LifeWater.M_LifeWater"));
	MatWall = Load(TEXT("/Game/Life/M_LifeWall.M_LifeWall"));
	MatRoof = Load(TEXT("/Game/Life/M_LifeRoof.M_LifeRoof"));
	MatWindow = Load(TEXT("/Game/Life/M_LifeWindow.M_LifeWindow"));
	MatLamp = Load(TEXT("/Game/Life/M_LifeLamp.M_LifeLamp"));
	MatGlow = Load(TEXT("/Game/Life/M_LifeGlow.M_LifeGlow"));
	MatLeaf->SetTextureParameterValue(TEXT("Atlas"), Tex.LeafAtlas);
	MatBark->SetTextureParameterValue(TEXT("BarkTex"), Tex.Bark);
	MatBark->SetTextureParameterValue(TEXT("BarkN"), Tex.BarkN);
	MatPalmBark->SetTextureParameterValue(TEXT("BarkTex"), Tex.PalmBark);
	MatPalmBark->SetTextureParameterValue(TEXT("BarkN"), Tex.PalmBarkN);
	MatTerrain->SetTextureParameterValue(TEXT("GroundTex"), Tex.Ground);
	MatWater->SetTextureParameterValue(TEXT("WaterN"), Tex.WaterN);
	MatWall->SetTextureParameterValue(TEXT("WallTex"), Tex.Wall);
	MatWall->SetTextureParameterValue(TEXT("WallN"), Tex.WallN);
	MatRoof->SetTextureParameterValue(TEXT("RoofTex"), Tex.Roof);
	MatRoof->SetTextureParameterValue(TEXT("RoofN"), Tex.RoofN);
	float Wind = 1.f; FParse::Value(FCommandLine::Get(), TEXT("-wind="), Wind);
	for (UMaterialInstanceDynamic* M : { MatLeaf.Get(), MatBark.Get(), MatPalmBark.Get() })
		M->SetScalarParameterValue(TEXT("WindStrength"), 100.f * Wind);
	{
		float Bright = 0.85f; FParse::Value(FCommandLine::Get(), TEXT("-leafbright="), Bright);
		MatLeaf->SetScalarParameterValue(TEXT("Brightness"), Bright);
		float Spec = 0.04f, Rough = 0.85f, Trans = 1.f;
		FParse::Value(FCommandLine::Get(), TEXT("-leafspec="), Spec); FParse::Value(FCommandLine::Get(), TEXT("-leafrough="), Rough); FParse::Value(FCommandLine::Get(), TEXT("-leaftrans="), Trans);
		MatLeaf->SetScalarParameterValue(TEXT("LeafSpec"), Spec); MatLeaf->SetScalarParameterValue(TEXT("LeafRough"), Rough); MatLeaf->SetScalarParameterValue(TEXT("TransmissionScale"), Trans);
		MatLeaf->SetVectorParameterValue(TEXT("TintA"), FLinearColor(0.70f, 0.82f, 0.80f));
		MatLeaf->SetVectorParameterValue(TEXT("TintB"), FLinearColor(1.12f, 1.02f, 0.70f));
		MatLeaf->SetVectorParameterValue(TEXT("Transmission"), FLinearColor(0.75f, 0.9f, 0.3f));
	}
	TArray<UObject*> Objs; Tex.AddReferencedObjects(Objs);
	for (UObject* O : Objs) Keep.Add(O);
}

void ALifeWorld::SetNight(float Night, float WindowGlow)
{
	if (MatWindow) { MatWindow->SetScalarParameterValue(TEXT("Night"), Night); MatWindow->SetScalarParameterValue(TEXT("Glow"), WindowGlow); }
	if (MatLamp) MatLamp->SetScalarParameterValue(TEXT("Night"), Night);
	if (MatGlow) MatGlow->SetScalarParameterValue(TEXT("Night"), Night);
}

UStaticMeshComponent* ALifeWorld::AddMeshComp(const FString& Name, const FLifeMesh& Mesh, const TArray<UMaterialInterface*>& Mats, const FVector3d& OriginM, bool bShadow)
{
	if (Mesh.IsEmpty()) return nullptr;
	UStaticMesh* SM = LifeMakeStaticMesh(this, Name, { &Mesh }, Mats, { 1.f });
	Keep.Add(SM);
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, FName(*(Name + TEXT("_C"))));
	C->SetMobility(EComponentMobility::Static);
	C->SetupAttachment(RootComponent);
	C->SetRelativeLocation(FVector(OriginM * 100.0));
	C->SetStaticMesh(SM);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadow);
	C->bAffectDistanceFieldLighting = false;
	C->RegisterComponent();
	return C;
}

// ============================================================ terrain

namespace
{
	FVector3f Lerp3(const FVector3f& A, const FVector3f& B, float T) { return A + (B - A) * FMath::Clamp(T, 0.f, 1.f); }
}

void ALifeWorld::BuildTerrain()
{
	struct FRing { double Half, Step, Hole; int32 Chunk; float Skirt; };
	const FRing Rings[3] = { { 4096.0, 5.12, 0.0, 200, 12.f }, { 16384.0, 32.0, 4096.0, 128, 80.f }, { 65536.0, 256.0, 16384.0, 64, 600.f } };
	for (const FRing& Rg : Rings)
	{
		const int32 NQ = int32(FMath::RoundToInt(2.0 * Rg.Half / Rg.Step));
		const int32 NC = NQ / Rg.Chunk;
		struct FJob { int32 Ci, Cj; FLifeMesh M; FVector3d Origin; };
		TArray<FJob> Jobs;
		for (int32 Cj = 0; Cj < NC; ++Cj)
			for (int32 Ci = 0; Ci < NC; ++Ci)
			{
				const double X0 = -Rg.Half + Ci * Rg.Chunk * Rg.Step, Y0 = -Rg.Half + Cj * Rg.Chunk * Rg.Step;
				const double X1 = X0 + Rg.Chunk * Rg.Step, Y1 = Y0 + Rg.Chunk * Rg.Step;
				if (Rg.Hole > 0.0 && X0 >= -Rg.Hole - 1 && X1 <= Rg.Hole + 1 && Y0 >= -Rg.Hole - 1 && Y1 <= Rg.Hole + 1) continue;
				FJob& J = Jobs.AddDefaulted_GetRef();
				J.Ci = Ci; J.Cj = Cj; J.M = FLifeMesh(1);
			}
		ParallelFor(Jobs.Num(), [&](int32 Jx)
		{
			FJob& Job = Jobs[Jx];
			const int32 K = Rg.Chunk;
			const double X0 = -Rg.Half + Job.Ci * K * Rg.Step, Y0 = -Rg.Half + Job.Cj * K * Rg.Step;
			const double Xc = X0 + 0.5 * K * Rg.Step, Yc = Y0 + 0.5 * K * Rg.Step;
			Job.Origin = SurfaceM(Xc, Yc, 0.0);
			FLifeMesh& M = Job.M;
			M.V.Reserve((K + 1) * (K + 1) + 4 * (K + 1));
			TArray<float> Hs; Hs.SetNumUninitialized((K + 3) * (K + 3));
			for (int32 J = -1; J <= K + 1; ++J)
				for (int32 I = -1; I <= K + 1; ++I)
					Hs[(J + 1) * (K + 3) + (I + 1)] = HeightAt(X0 + I * Rg.Step, Y0 + J * Rg.Step);
			auto Hg = [&](int32 I, int32 J) { return Hs[(J + 1) * (K + 3) + (I + 1)]; };
			for (int32 J = 0; J <= K; ++J)
				for (int32 I = 0; I <= K; ++I)
				{
					const double X = X0 + I * Rg.Step, Y = Y0 + J * Rg.Step;
					const float H = Hg(I, J);
					const FVector3f Nl = FVector3f(-(Hg(I + 1, J) - Hg(I - 1, J)), -(Hg(I, J + 1) - Hg(I, J - 1)), float(2.0 * Rg.Step)).GetSafeNormal();
					const FVector3f N = FVector3f(UpQuat(X, Y).RotateVector(FVector3d(Nl)));
					// --- ground colour from climate, height, slope and forest cover
					const float T = TempAt(X, Y), Mo = MoistAt(X, Y), Vg = VegAt(X, Y);
					const float Wob = LifeFbm2(float(X / 700.0), float(Y / 700.0), 3, 51);
					const FVector3f Lush(0.26f, 0.32f, 0.17f), Meadow(0.37f, 0.39f, 0.24f), Dry(0.57f, 0.51f, 0.36f), Sand(0.74f, 0.67f, 0.50f),
						Rock(0.43f, 0.41f, 0.38f), Tundra(0.47f, 0.45f, 0.38f), Snow(0.92f, 0.93f, 0.96f), Seabed(0.60f, 0.56f, 0.44f);
					FVector3f C = Lerp3(Meadow, Lush, LifeSmooth(0.3f, 0.7f, Mo + 0.15f * Wob));
					C = Lerp3(C, Dry, LifeSmooth(0.42f, 0.18f, Mo + 0.1f * Wob) * LifeSmooth(8.f, 20.f, T));
					C = Lerp3(C, Tundra, LifeSmooth(0.f, -8.f, T));
					C = Lerp3(C, Rock, LifeSmooth(0.85f, 0.65f, Nl.Z));
					C = Lerp3(C, Sand, LifeSmooth(3.5f, 1.0f, H) * LifeSmooth(0.75f, 0.9f, Nl.Z));
					C = Lerp3(C, Seabed, LifeSmooth(0.5f, -0.5f, H));
					C = Lerp3(C, Snow, LifeSmooth(-9.f, -14.f, T));
					C *= 1.f + 0.1f * Wob;
					{
						// meadow patchiness at 40-200 m: lusher and drier swathes, mown and grazed patches
						const float P1 = LifeFbm2(float(X / 55.0), float(Y / 55.0), 3, 53), P2 = LifeFbm2(float(X / 180.0), float(Y / 180.0), 3, 57);
						C = Lerp3(C, Dry, FMath::Max(0.f, P2) * 0.35f * LifeSmooth(2.f, 6.f, H));
						C = Lerp3(C, Lush, FMath::Max(0.f, -P2) * 0.3f * LifeSmooth(2.f, 6.f, H));
						C *= 1.f + 0.13f * P1;
					}
					// forest canopy (same function the scatter uses)
					float Canopy = 0.f;
					{
						const float F = LifeFbm2(float(X / 520.0), float(Y / 520.0), 5, 31) * 0.5f + LifeFbm2(float(X / 90.0), float(Y / 90.0), 2, 37) * 0.12f;
						Canopy = LifeSmooth(0.42f, 0.62f, Vg + F * 0.6f) * LifeSmooth(1.5f, 4.f, H) * LifeSmooth(0.7f, 0.85f, Nl.Z) * LifeSmooth(-7.f, -3.f, T) * ClearingAt(X, Y);
						if (LuGet(LuNoTree, X, Y)) Canopy = 0.f;
					}
					const FVector3f Crown = Lerp3(Lerp3(FVector3f(0.13f, 0.20f, 0.08f), FVector3f(0.09f, 0.15f, 0.09f), LifeSmooth(12.f, 5.f, T)), FVector3f(0.11f, 0.21f, 0.08f), LifeSmooth(20.f, 25.f, T));
					C = Lerp3(C, Crown, Canopy);
					const FVector3d P = SurfaceM(X, Y, H) - Job.Origin;
					const FVector2f Uv1 = FVector2f(float((X + LuHalf) / (2.0 * LuHalf)), float((Y + LuHalf) / (2.0 * LuHalf)));
					M.Add(FVector3f(P), N, FVector2f(float(X), float(Y)), LifeCol(C.X, C.Y, C.Z, Canopy), Uv1);
				}
			const int32 R = K + 1;
			for (int32 J = 0; J < K; ++J)
				for (int32 I = 0; I < K; ++I)
				{
					const uint32 a = J * R + I, b = a + 1, c = a + R + 1, d = a + R;
					const FVector3f Up = M.V[a].N;
					if ((I + J) & 1) M.Quad(0, a, b, c, d, Up); else M.Quad(0, b, c, d, a, Up);
				}
			// skirts on all four sides
			auto Skirt = [&](int32 I0, int32 J0, int32 DI, int32 DJ)
			{
				const uint32 Base = M.V.Num();
				for (int32 S = 0; S <= K; ++S)
				{
					const FLifeVert& Src = M.V[(J0 + DJ * S) * R + (I0 + DI * S)];
					FLifeVert V = Src;
					const FVector3f Down = -FVector3f(FVector3d(Xc, Yc, Rm).GetSafeNormal());
					V.P = Src.P + Down * Rg.Skirt;
					M.V.Add(V);
				}
				for (int32 S = 0; S < K; ++S)
				{
					const uint32 a = (J0 + DJ * S) * R + (I0 + DI * S), b = (J0 + DJ * (S + 1)) * R + (I0 + DI * (S + 1));
					const uint32 c = Base + S + 1, d = Base + S;
					// face outward from the chunk: both windings are fine visually; pick one facing away from the chunk centre
					const FVector3f Mid = (M.V[a].P + M.V[b].P) * 0.5f;
					M.Quad(0, a, b, c, d, FVector3f(Mid.X, Mid.Y, 0.f));
				}
			};
			Skirt(0, 0, 1, 0); Skirt(0, K, 1, 0); Skirt(0, 0, 0, 1); Skirt(K, 0, 0, 1);
		});
		for (FJob& Job : Jobs)
		{
			TerrainTris += Job.M.NumTris();
			AddMeshComp(FString::Printf(TEXT("Terrain_%d_%d_%d"), int32(Rg.Step), Job.Ci, Job.Cj), Job.M, { MatTerrain }, Job.Origin, true);
		}
	}
}

void ALifeWorld::BuildWater()
{
	struct FRing { double Half, Step, Hole; int32 Chunk; };
	const FRing Rings[3] = { { 4096.0, 8.0, 0.0, 128 }, { 16384.0, 64.0, 4096.0, 64 }, { 65536.0, 512.0, 16384.0, 32 } };
	for (const FRing& Rg : Rings)
	{
		const int32 NQ = int32(FMath::RoundToInt(2.0 * Rg.Half / Rg.Step));
		const int32 NC = NQ / Rg.Chunk;
		for (int32 Cj = 0; Cj < NC; ++Cj)
			for (int32 Ci = 0; Ci < NC; ++Ci)
			{
				const int32 K = Rg.Chunk;
				const double X0 = -Rg.Half + Ci * K * Rg.Step, Y0 = -Rg.Half + Cj * K * Rg.Step;
				const double X1 = X0 + K * Rg.Step, Y1 = Y0 + K * Rg.Step;
				if (Rg.Hole > 0.0 && X0 >= -Rg.Hole - 1 && X1 <= Rg.Hole + 1 && Y0 >= -Rg.Hole - 1 && Y1 <= Rg.Hole + 1) continue;
				const FVector3d Origin = SurfaceM(0.5 * (X0 + X1), 0.5 * (Y0 + Y1), 0.0);
				FLifeMesh M(1);
				TArray<float> Hs; Hs.SetNumUninitialized((K + 1) * (K + 1));
				ParallelFor(K + 1, [&](int32 J) { for (int32 I = 0; I <= K; ++I) Hs[J * (K + 1) + I] = HeightAt(X0 + I * Rg.Step, Y0 + J * Rg.Step); });
				bool bAny = false;
				for (float H : Hs) if (H < 0.5f) { bAny = true; break; }
				if (!bAny) continue;
				for (int32 J = 0; J <= K; ++J)
					for (int32 I = 0; I <= K; ++I)
					{
						const double X = X0 + I * Rg.Step, Y = Y0 + J * Rg.Step;
						const float Depth = -Hs[J * (K + 1) + I];
						const FVector3f Shallow(0.20f, 0.42f, 0.40f), Mid(0.05f, 0.20f, 0.24f), Deep(0.015f, 0.06f, 0.10f);
						FVector3f C = Lerp3(Shallow, Mid, LifeSmooth(0.5f, 9.f, Depth));
						C = Lerp3(C, Deep, LifeSmooth(9.f, 45.f, Depth));
						const float Foam = LifeSmooth(1.2f, 0.f, Depth) * (0.55f + 0.45f * LifeFbm2(float(X / 9.0), float(Y / 9.0), 2, 61));
						const FVector3d P = SurfaceM(X, Y, 0.0) - Origin;
						const FVector3f N = FVector3f(FVector3d(X, Y, Rm).GetSafeNormal());
						M.Add(FVector3f(P), N, FVector2f(float(X), float(Y)), LifeCol(C.X, C.Y, C.Z, FMath::Clamp(Foam, 0.f, 1.f)));
					}
				const int32 R = K + 1;
				for (int32 J = 0; J < K; ++J)
					for (int32 I = 0; I < K; ++I)
					{
						const uint32 a = J * R + I, b = a + 1, c = a + R + 1, d = a + R;
						if (Hs[a] > 1.5f && Hs[b] > 1.5f && Hs[c] > 1.5f && Hs[d] > 1.5f) continue;
						M.Quad(0, a, b, c, d, M.V[a].N);
					}
				if (M.IsEmpty()) continue;
				WaterTris += M.NumTris();
				AddMeshComp(FString::Printf(TEXT("Water_%d_%d_%d"), int32(Rg.Step), Ci, Cj), M, { MatWater }, Origin, false);
			}
	}
}

// ============================================================ trees

void ALifeWorld::BuildTrees()
{
	constexpr int32 NVar = 4;
	const int32 NK = int32(ETreeKind::Num);
	int32 NaniteTrees = -1; FParse::Value(FCommandLine::Get(), TEXT("-nanitetrees="), NaniteTrees);
	const bool bRigidShadows = !FParse::Param(FCommandLine::Get(), TEXT("swayshadows"));
	// meshes
	TArray<UHierarchicalInstancedStaticMeshComponent*> Comp; Comp.SetNum(NK * NVar);
	TArray<float> KindH; KindH.SetNum(NK * NVar);
	// TreeLab: by default the Blender-built tree families imported to /Game/Trees (art/trees, Scripts/import_trees.py);
	// -gentrees uses the LifeLab generated stand-ins for comparison.
	const bool bAssetTrees = !FParse::Param(FCommandLine::Get(), TEXT("gentrees"));
	TArray<float> LodSizes;
	{
		FString Ls;
		if (FParse::Value(FCommandLine::Get(), TEXT("-lodsizes="), Ls, false))
		{
			TArray<FString> Parts; Ls.ParseIntoArray(Parts, TEXT(","));
			for (const FString& P : Parts) LodSizes.Add(FCString::Atof(*P));
		}
	}
	float AssetCanopyDensity = 0.5f; FParse::Value(FCommandLine::Get(), TEXT("-canopydensity="), AssetCanopyDensity);
	TMap<UMaterialInterface*, UMaterialInstanceDynamic*> Mids;
	auto Mid = [&](UMaterialInterface* M) -> UMaterialInterface*
	{
		if (!M) return M;
		if (UMaterialInstanceDynamic** F = Mids.Find(M)) return *F;
		UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(M, this);
		float Wind = 1.f; FParse::Value(FCommandLine::Get(), TEXT("-wind="), Wind);
		D->SetScalarParameterValue(TEXT("WindStrength"), 100.f * Wind);
		D->SetScalarParameterValue(TEXT("BranchStrength"), 100.f * Wind);
		float Bright = -1.f; FParse::Value(FCommandLine::Get(), TEXT("-treebright="), Bright);
		if (Bright > 0.f) D->SetScalarParameterValue(TEXT("Brightness"), Bright);
		float Trans = -1.f; FParse::Value(FCommandLine::Get(), TEXT("-leaftrans="), Trans);
		if (Trans >= 0.f) D->SetScalarParameterValue(TEXT("TransmissionScale"), Trans);
		Keep.Add(D);
		Mids.Add(M, D);
		return D;
	};
	for (int32 K = 0; K < NK; ++K)
		for (int32 V = 0; V < NVar; ++V)
		{
			UStaticMesh* SM = nullptr;
			FTreeBuild B;
			if (bAssetTrees)
			{
				const FString Kn = LifeTreeName(ETreeKind(K));
				const FString Path = FString::Printf(TEXT("/Game/Trees/%s/SM_Tree_%s_%d.SM_Tree_%s_%d"), *Kn, *Kn, V, *Kn, V);
				SM = LoadObject<UStaticMesh>(nullptr, *Path);
				if (!SM) UE_LOG(LogTemp, Error, TEXT("LifeTrees: missing %s (run import.ps1)"), *Path);
			}
			if (SM)
			{
				B.Height = float(SM->GetBounds().GetBox().Max.Z / 100.0);
				if (FStaticMeshRenderData* RD = SM->GetRenderData())
				{
					if (V == 0 && RD->LODResources.Num()) TreeLOD0Tris[K] = RD->LODResources[0].GetNumTriangles();
					for (int32 I = 0; I < LodSizes.Num() && I < RD->LODResources.Num(); ++I) RD->ScreenSize[I].Default = LodSizes[I];
					if (V == 0)
					{
						FString Info;
						for (int32 I = 0; I < RD->LODResources.Num(); ++I) Info += FString::Printf(TEXT(" LOD%d %d tris @%.3f"), I, RD->LODResources[I].GetNumTriangles(), RD->ScreenSize[I].Default);
						UE_LOG(LogTemp, Display, TEXT("LifeTrees: asset %s H %.1f m%s"), *SM->GetName(), B.Height, *Info);
					}
				}
			}
			else
			{
				LifeBuildTree(ETreeKind(K), 1000 + V * 17 + K * 3, B);
				TArray<const FLifeMesh*> L;
				for (int32 I = 0; I < LifeTreeLODs; ++I) L.Add(&B.LOD[I]);
				UMaterialInterface* BarkM = (ETreeKind(K) == ETreeKind::Palm) ? MatPalmBark.Get() : MatBark.Get();
				SM = LifeMakeStaticMesh(this, FString::Printf(TEXT("Tree_%s_%d"), LifeTreeName(ETreeKind(K)), V), L, { BarkM, MatLeaf }, { 1.f, 0.30f, 0.10f, 0.03f }, NaniteTrees);
				Keep.Add(SM);
				if (V == 0) TreeLOD0Tris[K] = B.LOD[0].NumTris();
			}
			UHierarchicalInstancedStaticMeshComponent* C = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, FName(*FString::Printf(TEXT("HISM_%s_%d"), LifeTreeName(ETreeKind(K)), V)));
			C->SetMobility(EComponentMobility::Static);
			C->SetupAttachment(RootComponent);
			C->SetStaticMesh(SM);
			if (bAssetTrees && SM)
				for (int32 Mi = 0; Mi < SM->GetStaticMaterials().Num(); ++Mi) C->SetMaterial(Mi, Mid(SM->GetMaterial(Mi)));
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetCastShadow(true);
			C->bAffectDistanceFieldLighting = false;
			const float Cull = (ETreeKind(K) == ETreeKind::Shrub) ? FMath::Min(CullTrees, 2500.f) : CullTrees;
			C->SetCullDistances(0, int32(Cull * 100.f));
			C->SetWorldPositionOffsetDisableDistance(int32(1500 * 100));
			if (bRigidShadows) C->ShadowCacheInvalidationBehavior = EShadowCacheInvalidationBehavior::Rigid;
			C->RegisterComponent();
			Comp[K * NVar + V] = C;
			KindH[K * NVar + V] = B.Height;
			TreeComps.Add(C);
			TreeCompKind.Add(K);
		}

	// optional lineup of every kind and variant on a cleared lawn at the origin (for judging shapes)
	if (FParse::Param(FCommandLine::Get(), TEXT("lineup")))
	{
		for (int32 K = 0; K < NK; ++K)
			for (int32 V = 0; V < NVar; ++V)
			{
				const double X = (V - 1.5) * (bAssetTrees ? 30.0 : 24.0), Y = (K - 2) * (bAssetTrees ? 30.0 : 26.0);
				const float H = HeightAt(X, Y);
				const FQuat4d Q = UpQuat(X, Y) * FQuat4d(FVector3d(0, 0, 1), V * 1.3);
				Comp[K * NVar + V]->AddInstance(FTransform(FQuat(Q), FVector(SurfaceM(X, Y, H - 0.2) * 100.0), FVector(1.0)));
				TreeCount[K] += 1;
			}
	}

	// scatter: jittered grid, one candidate per cell, rows in parallel
	const double Cell = 4.5;
	const int32 NCell = int32(2.0 * TreeHalf / Cell);
	TArray<TArray<TArray<FTransform>>> Rows; Rows.SetNum(NCell);
	ParallelFor(NCell, [&](int32 J)
	{
		TArray<TArray<FTransform>>& Out = Rows[J];
		Out.SetNum(NK * NVar);
		for (int32 I = 0; I < NCell; ++I)
		{
			const uint32 Hh = LifeHash(I, J, 7);
			const double X = -TreeHalf + (I + 0.15 + 0.7 * LifeHash01(I, J, 8)) * Cell;
			const double Y = -TreeHalf + (J + 0.15 + 0.7 * LifeHash01(I, J, 9)) * Cell;
			if (LuGet(LuNoTree, X, Y)) continue;
			const float Rnd = float(Hh & 0xFFFF) / 65536.f;
			const float H = HeightAt(X, Y);
			if (H < 0.8f) continue;
			const float T = TempAt(X, Y), Mo = MoistAt(X, Y), Vg = VegAt(X, Y);
			if (T < -7.f) continue;
			const FVector3f Nl = Slope(X, Y, 3.0);
			const float F = LifeFbm2(float(X / 520.0), float(Y / 520.0), 5, 31) * 0.5f + LifeFbm2(float(X / 90.0), float(Y / 90.0), 2, 37) * 0.12f;
			const float Canopy = LifeSmooth(0.42f, 0.62f, Vg + F * 0.6f) * LifeSmooth(1.5f, 4.f, H) * LifeSmooth(0.7f, 0.85f, Nl.Z) * LifeSmooth(-7.f, -3.f, T) * ClearingAt(X, Y);
			const float Hot = LifeSmooth(17.f, 22.f, T), DryK = LifeSmooth(0.5f, 0.3f, Mo);
			const float Open = (0.012f + 0.03f * Vg + 0.1f * Hot * DryK * Vg) * LifeSmooth(0.6f, 0.8f, Nl.Z) * (0.4f + 0.6f * ClearingAt(X, Y));
			const float Coast = LifeSmooth(14.f, 2.f, H) * Hot;
			const float Wet = LifeSmooth(20.f, 25.f, T) * LifeSmooth(0.55f, 0.75f, Mo); // tropical rainforest: fewer, larger crowns
			// real-scale assets have 12-18 m crowns: closed canopy at ~8 m spacing instead of 5.7 m
			const float CanopyK = bAssetTrees ? AssetCanopyDensity : 1.f;
			const float P = (Canopy * 0.62f * CanopyK * (1.f - 0.45f * Wet) + (1.f - Canopy) * Open + Coast * 0.06f) * TreeDensity;
			if (Rnd >= P) continue;
			// kind
			const float Patch = LifeFbm2(float(X / 700.0), float(Y / 700.0), 3, 41);
			float W[4];
			W[int32(ETreeKind::Conifer)] = LifeSmooth(13.f, 5.f, T) + 0.6f * LifeSmooth(0.05f, 0.35f, Patch) * LifeSmooth(19.f, 11.f, T);
			W[int32(ETreeKind::Broadleaf)] = LifeSmooth(3.f, 10.f, T) * LifeSmooth(0.2f, 0.4f, Mo) * (1.f - 0.85f * Coast) * FMath::Lerp(1.f, LifeSmooth(0.3f, 0.55f, Mo), Hot);
			W[int32(ETreeKind::Palm)] = Hot * (LifeSmooth(25.f, 3.f, H) * 4.f + 0.35f * LifeSmooth(0.6f, 0.8f, Mo));
			W[int32(ETreeKind::Acacia)] = LifeSmooth(16.f, 21.f, T) * DryK * (Canopy < 0.5f ? 3.f : 0.6f);
			float Sum = 0.f; for (float Wk : W) Sum += Wk;
			if (Sum <= 1e-4f) continue;
			float Pick = LifeHash01(I, J, 10) * Sum;
			int32 Kind = 0;
			for (; Kind < 3; ++Kind) { if (Pick < W[Kind]) break; Pick -= W[Kind]; }
			const int32 Var = int32(LifeHash(I, J, 11) % NVar);
			// real-scale assets already cover the size range with their variants: narrower jitter
			float Sc = bAssetTrees ? FMath::Lerp(0.85f, 1.12f, LifeHash01(I, J, 12)) : FMath::Lerp(0.72f, 1.22f, LifeHash01(I, J, 12));
			if (Canopy < 0.3f) Sc *= bAssetTrees ? 0.95f + 0.12f * LifeHash01(I, J, 13) : 0.9f + 0.25f * LifeHash01(I, J, 13); // lone trees spread wide
			Sc *= FMath::Lerp(0.75f, 1.f, LifeSmooth(-7.f, 2.f, T));
			if (Kind == int32(ETreeKind::Broadleaf)) Sc *= 1.f + 0.35f * Wet + (LifeHash01(I, J, 17) < 0.04f * Wet ? 0.6f : 0.f); // emergents
			const FQuat4d Q = UpQuat(X, Y) * FQuat4d(FVector3d(0, 0, 1), LifeHash01(I, J, 14) * 2.0 * PI) * FQuat4d(FVector3d(1, 0, 0), (LifeHash01(I, J, 15) - 0.5) * 0.06);
			const FVector3d Loc = SurfaceM(X, Y, H - 0.2) * 100.0;
			Out[Kind * NVar + Var].Add(FTransform(FQuat(Q), FVector(Loc), FVector(Sc, Sc, Sc * FMath::Lerp(0.9f, 1.1f, LifeHash01(I, J, 16)))));
		}
	});
	// shrubs: a separate, finer layer in the open and at forest edges
	const double SCell = 6.0;
	const int32 NS = int32(2.0 * FMath::Min(TreeHalf, 3000.0) / SCell);
	const double SHalf = NS * SCell * 0.5;
	TArray<TArray<FTransform>> ShrubRows; ShrubRows.SetNum(NS);
	ParallelFor(NS, [&](int32 J)
	{
		for (int32 I = 0; I < NS; ++I)
		{
			const double X = -SHalf + (I + LifeHash01(I, J, 21)) * SCell, Y = -SHalf + (J + LifeHash01(I, J, 22)) * SCell;
			if (LuGet(LuNoTree, X, Y) && !LuGet(LuYard, X, Y)) continue;
			const float H = HeightAt(X, Y);
			if (H < 0.6f) continue;
			const float Vg = VegAt(X, Y), T = TempAt(X, Y);
			if (T < -9.f) continue;
			const float F = LifeFbm2(float(X / 520.0), float(Y / 520.0), 5, 31) * 0.5f + LifeFbm2(float(X / 90.0), float(Y / 90.0), 2, 37) * 0.12f;
			const float Edge = 1.f - FMath::Abs(LifeSmooth(0.42f, 0.62f, Vg + F * 0.6f) * ClearingAt(X, Y) * 2.f - 1.f);
			const float P = (0.05f + 0.12f * Vg + 0.35f * Edge) * TreeDensity * LifeSmooth(0.6f, 0.85f, Slope(X, Y, 3.0).Z);
			if (LifeHash01(I, J, 23) >= P) continue;
			const FQuat4d Q = UpQuat(X, Y) * FQuat4d(FVector3d(0, 0, 1), LifeHash01(I, J, 24) * 2.0 * PI);
			const float Sc = FMath::Lerp(0.6f, 1.4f, LifeHash01(I, J, 25));
			ShrubRows[J].Add(FTransform(FQuat(Q), FVector(SurfaceM(X, Y, H - 0.1) * 100.0), FVector(Sc)));
		}
	});
	// hedgerow shrubs and trees recorded by the settlement planner sit in LuYard==0 && LuNoTree==2 cells: handled in BuildSettlements.

	TArray<TArray<FTransform>> All; All.SetNum(NK * NVar);
	// hedgerows: dense shrubs with the odd broadleaf tree along field edges
	{
		int32 Hn = 0;
		for (const FVector4d& Hg : Hedges)
		{
			const FVector2D A(Hg.X, Hg.Y), B(Hg.Z, Hg.W);
			const double L = FVector2D::Distance(A, B);
			const int32 N = FMath::Max(1, int32(L / 3.2));
			for (int32 K = 0; K <= N; ++K, ++Hn)
			{
				const FVector2D P = FMath::Lerp(A, B, double(K) / N) + FVector2D(LifeHash01(Hn, 1, 91) - 0.5, LifeHash01(Hn, 2, 91) - 0.5) * 1.5;
				if (LuGet(LuRoad, P.X, P.Y) > 60 || LuGet(LuPaved, P.X, P.Y) > 60 || LuGet(LuBuilt, P.X, P.Y)) continue;
				const float H = HeightAt(P.X, P.Y);
				if (H < 0.8f) continue;
				const bool bTree = LifeHash01(Hn, 3, 91) < 0.1f;
				const FQuat4d Q = UpQuat(P.X, P.Y) * FQuat4d(FVector3d(0, 0, 1), LifeHash01(Hn, 4, 91) * 6.28);
				const float Sc = bTree ? FMath::Lerp(0.75f, 1.1f, LifeHash01(Hn, 5, 91)) : FMath::Lerp(0.9f, 1.6f, LifeHash01(Hn, 5, 91));
				const int32 Kind = bTree ? int32(ETreeKind::Broadleaf) : int32(ETreeKind::Shrub);
				All[Kind * NVar + int32(LifeHash(Hn, 6, 91) % NVar)].Add(FTransform(FQuat(Q), FVector(SurfaceM(P.X, P.Y, H - 0.15) * 100.0), FVector(Sc)));
			}
		}
	}
	for (const TArray<TArray<FTransform>>& R : Rows) for (int32 C = 0; C < NK * NVar; ++C) All[C].Append(R[C]);
	for (int32 J = 0; J < NS; ++J)
		for (int32 I = 0; I < ShrubRows[J].Num(); ++I)
			All[int32(ETreeKind::Shrub) * NVar + int32(LifeHash(I, J, 26) % NVar)].Add(ShrubRows[J][I]);
	// log, per kind, the densest 300 m cell near the site (handy for framing shots)
	for (int32 K = 0; K < NK; ++K)
	{
		TMap<FIntPoint, int32> Cnt;
		for (int32 V = 0; V < NVar; ++V)
			for (const FTransform& T : All[K * NVar + V])
			{
				const FVector P = T.GetLocation() / 100.0;
				Cnt.FindOrAdd(FIntPoint(FMath::FloorToInt(P.X / 300.0), FMath::FloorToInt(P.Y / 300.0)))++;
			}
		FIntPoint Best(0, 0); int32 Bn = 0;
		for (const TPair<FIntPoint, int32>& Kv : Cnt) if (Kv.Value > Bn && FVector2D(Kv.Key.X, Kv.Key.Y).Size() < 10.0) { Bn = Kv.Value; Best = Kv.Key; }
		UE_LOG(LogTemp, Display, TEXT("LifeTrees: densest %s cell at (%.0f, %.0f) with %d"), LifeTreeName(ETreeKind(K)), (Best.X + 0.5) * 300.0, (Best.Y + 0.5) * 300.0, Bn);
	}
	for (int32 C = 0; C < NK * NVar; ++C)
	{
		UHierarchicalInstancedStaticMeshComponent* Hc = Comp[C];
		if (All[C].Num())
		{
			Hc->PreAllocateInstancesMemory(All[C].Num());
			Hc->AddInstances(All[C], false, false, false);
		}
		TreeCount[C / NVar] += All[C].Num();
	}
}

// ============================================================ land use

void ALifeWorld::StampDisc(TArray<uint8>& A, const FVector2D& C, double R, uint8 V)
{
	int32 I0, J0, I1, J1;
	LuIndex(C.X - R, C.Y - R, I0, J0); LuIndex(C.X + R, C.Y + R, I1, J1);
	for (int32 J = FMath::Max(0, J0); J <= FMath::Min(LuN - 1, J1); ++J)
		for (int32 I = FMath::Max(0, I0); I <= FMath::Min(LuN - 1, I1); ++I)
		{
			const FVector2D P(-LuHalf + (I + 0.5) * LuStep, -LuHalf + (J + 0.5) * LuStep);
			if (FVector2D::Distance(P, C) <= R) A[J * LuN + I] = FMath::Max(A[J * LuN + I], V);
		}
}

void ALifeWorld::StampRoad(const TArray<FVector2D>& Pts, double Width, bool bPaved)
{
	TArray<uint8>& A = bPaved ? LuPaved : LuRoad;
	for (int32 S = 0; S + 1 < Pts.Num(); ++S)
	{
		const FVector2D P0 = Pts[S], P1 = Pts[S + 1];
		const double M = Width * 0.5 + 3.0;
		int32 I0, J0, I1, J1;
		LuIndex(FMath::Min(P0.X, P1.X) - M, FMath::Min(P0.Y, P1.Y) - M, I0, J0);
		LuIndex(FMath::Max(P0.X, P1.X) + M, FMath::Max(P0.Y, P1.Y) + M, I1, J1);
		const FVector2D Dv = P1 - P0; const double L2 = FMath::Max(Dv.SizeSquared(), 1e-6);
		for (int32 J = FMath::Max(0, J0); J <= FMath::Min(LuN - 1, J1); ++J)
			for (int32 I = FMath::Max(0, I0); I <= FMath::Min(LuN - 1, I1); ++I)
			{
				const FVector2D P(-LuHalf + (I + 0.5) * LuStep, -LuHalf + (J + 0.5) * LuStep);
				const double T = FMath::Clamp(FVector2D::DotProduct(P - P0, Dv) / L2, 0.0, 1.0);
				const double Dist = FVector2D::Distance(P, P0 + Dv * T);
				const float Wob = 0.6f * LifeNoise2(float(P.X / 7.0), float(P.Y / 7.0), 71);
				const float Cov = FMath::Clamp(float(Width * 0.5 - Dist) / 1.5f + 0.5f + Wob * (bPaved ? 0.1f : 0.5f), 0.f, 1.f);
				const uint8 V = uint8(Cov * 255.f);
				A[J * LuN + I] = FMath::Max(A[J * LuN + I], V);
				if (Dist < Width * 0.5 + 2.0) LuNoTree[J * LuN + I] = 1;
			}
	}
}

void ALifeWorld::StampPoly(TArray<uint8>& A, const TArray<FVector2D>& Poly, uint8 V, double Shrink)
{
	if (Poly.Num() < 3) return;
	FBox2D B(ForceInit);
	for (const FVector2D& P : Poly) B += P;
	int32 I0, J0, I1, J1;
	LuIndex(B.Min.X, B.Min.Y, I0, J0); LuIndex(B.Max.X, B.Max.Y, I1, J1);
	for (int32 J = FMath::Max(0, J0); J <= FMath::Min(LuN - 1, J1); ++J)
		for (int32 I = FMath::Max(0, I0); I <= FMath::Min(LuN - 1, I1); ++I)
		{
			const FVector2D P(-LuHalf + (I + 0.5) * LuStep, -LuHalf + (J + 0.5) * LuStep);
			bool bIn = false; double MinD = 1e9;
			for (int32 K = 0, L = Poly.Num() - 1; K < Poly.Num(); L = K++)
			{
				const FVector2D& Pk = Poly[K]; const FVector2D& Pl = Poly[L];
				if (((Pk.Y > P.Y) != (Pl.Y > P.Y)) && (P.X < (Pl.X - Pk.X) * (P.Y - Pk.Y) / (Pl.Y - Pk.Y) + Pk.X)) bIn = !bIn;
				const FVector2D Dv = Pl - Pk; const double T = FMath::Clamp(FVector2D::DotProduct(P - Pk, Dv) / FMath::Max(Dv.SizeSquared(), 1e-6), 0.0, 1.0);
				MinD = FMath::Min(MinD, FVector2D::Distance(P, Pk + Dv * T));
			}
			if (bIn && MinD >= Shrink) A[J * LuN + I] = FMath::Max(A[J * LuN + I], V);
		}
}

void ALifeWorld::StampField(const TArray<FVector2D>& Poly, const FColor& Col, float Angle01)
{
	if (Poly.Num() < 3) return;
	FBox2D B(ForceInit);
	for (const FVector2D& P : Poly) B += P;
	int32 I0, J0, I1, J1;
	LuIndex(B.Min.X, B.Min.Y, I0, J0); LuIndex(B.Max.X, B.Max.Y, I1, J1);
	const uint8 Ang = uint8(FMath::Clamp(Angle01, 0.f, 1.f) * 255.f);
	for (int32 J = FMath::Max(0, J0); J <= FMath::Min(LuN - 1, J1); ++J)
		for (int32 I = FMath::Max(0, I0); I <= FMath::Min(LuN - 1, I1); ++I)
		{
			const FVector2D P(-LuHalf + (I + 0.5) * LuStep, -LuHalf + (J + 0.5) * LuStep);
			bool bIn = false; double MinD = 1e9;
			for (int32 K = 0, L = Poly.Num() - 1; K < Poly.Num(); L = K++)
			{
				const FVector2D& Pk = Poly[K]; const FVector2D& Pl = Poly[L];
				if (((Pk.Y > P.Y) != (Pl.Y > P.Y)) && (P.X < (Pl.X - Pk.X) * (P.Y - Pk.Y) / (Pl.Y - Pk.Y) + Pk.X)) bIn = !bIn;
				const FVector2D Dv = Pl - Pk; const double T = FMath::Clamp(FVector2D::DotProduct(P - Pk, Dv) / FMath::Max(Dv.SizeSquared(), 1e-6), 0.0, 1.0);
				MinD = FMath::Min(MinD, FVector2D::Distance(P, Pk + Dv * T));
			}
			if (!bIn) continue;
			const int32 Ix = J * LuN + I;
			LuNoTree[Ix] = 1;
			if (MinD < 1.5) continue; // a grass margin between fields
			const float Cov = FMath::Clamp(float(MinD - 1.5) / 2.f, 0.f, 1.f);
			LuField[Ix] = FMath::Max(LuField[Ix], uint8(Cov * 255.f));
			// slight within-field variation (soil moisture)
			const float V = 1.f + 0.08f * LifeFbm2(float(P.X / 40.0), float(P.Y / 40.0), 3, 81);
			LuFieldCol[Ix] = FColor(uint8(FMath::Clamp(Col.R * V, 0.f, 255.f)), uint8(FMath::Clamp(Col.G * V, 0.f, 255.f)), uint8(FMath::Clamp(Col.B * V, 0.f, 255.f)), Ang);
		}
}

void ALifeWorld::MakeLandUseTextures()
{
	TArray<FColor> Lu; Lu.SetNumUninitialized(LuN * LuN);
	for (int32 I = 0; I < LuN * LuN; ++I) Lu[I] = FColor(LuRoad[I], LuPaved[I], LuField[I], LuYard[I]);
	// clear the border so clamped sampling outside the patch reads "nothing"
	for (int32 K = 0; K < LuN; ++K)
	{
		Lu[K] = Lu[(LuN - 1) * LuN + K] = Lu[K * LuN] = Lu[K * LuN + LuN - 1] = FColor(0, 0, 0, 0);
	}
	FString Dump;
	if (FParse::Value(FCommandLine::Get(), TEXT("-dumptex="), Dump))
	{
		const int32 Dn = LuN / 4;
		TArray<FColor> Small; Small.SetNumUninitialized(Dn * Dn);
		for (int32 J = 0; J < Dn; ++J) for (int32 I = 0; I < Dn; ++I)
		{
			const FColor& C = Lu[(J * 4) * LuN + I * 4];
			Small[J * Dn + I] = FColor(C.R, C.G, FMath::Max(C.B, C.A), 255);
		}
		FFileHelper::CreateBitmap(*(Dump / TEXT("landuse.bmp")), Dn, Dn, Small.GetData());
	}
	UTexture2D* LuT = LifeMakeTexture(TEXT("T_LandUse"), LuN, LuN, MoveTemp(Lu), false, false, 0.4f, false, true);
	TArray<FColor> Fc = LuFieldCol;
	UTexture2D* FcT = LifeMakeTexture(TEXT("T_FieldCol"), LuN, LuN, MoveTemp(Fc), true, false, 0.4f, false, true);
	Keep.Add(LuT); Keep.Add(FcT);
	MatTerrain->SetTextureParameterValue(TEXT("LandUse"), LuT);
	MatTerrain->SetTextureParameterValue(TEXT("FieldTex"), FcT);
}
