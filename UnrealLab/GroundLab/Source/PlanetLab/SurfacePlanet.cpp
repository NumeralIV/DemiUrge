#include "SurfacePlanet.h"
#include "SurfaceNoise.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Async/ParallelFor.h"
#include "Async/Async.h"
#include "HAL/PlatformTime.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"

namespace
{
	constexpr double DatumM = FSurfaceTerrain::DatumM;
	inline FVector3d Nrm(const FVector3d& V) { return V * (1.0 / FMath::Sqrt(V | V)); }
	// The sim -> UE axis swap mirrors the mesh; the sim's counter-clockwise triangles come out
	// front-facing in Unreal as they are.
	constexpr bool bFlipWinding = false;
}

int32 ASurfacePlanet::DebugMode = 0;

ASurfacePlanet::ASurfacePlanet()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

bool ASurfacePlanet::Init(const FString& DataPath, UMaterialInterface* InLand, UMaterialInterface* InWater)
{
	LandMat = InLand; WaterMat = InWater;
	const double T0 = FPlatformTime::Seconds();
	if (!T.Load(DataPath)) { UE_LOG(LogTemp, Error, TEXT("SurfacePlanet: cannot load %s"), *DataPath); return false; }
	T.Prepare();
	UE_LOG(LogTemp, Display, TEXT("SurfacePlanet: terrain prepared in %.0f ms"), (FPlatformTime::Seconds() - T0) * 1000.0);
	BuildRiverTexture(4096);
	if (LandMat)
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(LandMat, this);
		Mid->SetTextureParameterValue(TEXT("RiverTex"), RiverTex);
		LandMat = Mid;
		LandMID = Mid;
	}
	Nodes.Reserve(65536);
	for (int32 F = 0; F < 20; ++F)
	{
		FQNode& N = Nodes.AddDefaulted_GetRef();
		for (int32 K3 = 0; K3 < 3; ++K3) N.C[K3] = T.Ico[FSurfaceTerrain::IcoTris[3 * F + K3]];
		N.Ctr = Nrm(N.C[0] + N.C[1] + N.C[2]);
		N.Ang = FMath::Acos(FMath::Min3(N.Ctr | N.C[0], N.Ctr | N.C[1], N.Ctr | N.C[2]));
	}
	return true;
}

double ASurfacePlanet::HeightUE(const FVector& DirUE) const
{
	return T.Height(Nrm(ToSim(DirUE)), 0.5);
}

void ASurfacePlanet::SetView(const FVector& CamPos, const FVector& CamFwd, double FovDeg, double Aspect, int32 ScreenW)
{
	CamSim = ToSim(CamPos);
	FwdSim = Nrm(ToSim(CamFwd));
	TanHalfH = FMath::Tan(FMath::DegreesToRadians(FovDeg * 0.5));
	TanHalfV = TanHalfH / Aspect;
	PxPerRad = (ScreenW * 0.5) / TanHalfH;
}

void ASurfacePlanet::EnsureChildren(int32 Ni)
{
	if (Nodes[Ni].Child >= 0) return;
	const FVector3d A = Nodes[Ni].C[0], B = Nodes[Ni].C[1], C = Nodes[Ni].C[2];
	const FVector3d AB = Nrm(A + B), BC = Nrm(B + C), CA = Nrm(C + A);
	const FVector3d Corners[4][3] = { { A, AB, CA }, { B, BC, AB }, { C, CA, BC }, { AB, BC, CA } };
	const int32 First = Nodes.Num();
	const int32 Level = Nodes[Ni].Level + 1;
	const float MinH = Nodes[Ni].MinH, MaxH = Nodes[Ni].MaxH;
	for (int32 Ch = 0; Ch < 4; ++Ch)
	{
		FQNode& N = Nodes.AddDefaulted_GetRef();
		for (int32 K3 = 0; K3 < 3; ++K3) N.C[K3] = Corners[Ch][K3];
		N.Ctr = Nrm(N.C[0] + N.C[1] + N.C[2]);
		N.Ang = FMath::Acos(FMath::Min3(N.Ctr | N.C[0], N.Ctr | N.C[1], N.Ctr | N.C[2]));
		N.Level = Level; N.Parent = Ni; N.MinH = MinH; N.MaxH = MaxH;
	}
	Nodes[Ni].Child = First;
}

bool ASurfacePlanet::AboveHorizon(const FQNode& N) const
{
	const double Dc = CamSim.Length();
	const double Ro = RadiusCm - 5000.0; // the sea surface hides everything below it
	if (Dc <= Ro) return true;
	const double Rp = FMath::Max(RadiusCm + double(N.MaxH) * 100.0, Ro);
	const double Theta = FMath::Acos(FMath::Clamp((CamSim / Dc) | N.Ctr, -1.0, 1.0));
	return Theta - N.Ang < FMath::Acos(Ro / Dc) + FMath::Acos(Ro / Rp) + 0.002;
}

bool ASurfacePlanet::InFrustum(const FQNode& N) const
{
	const FVector3d Pc = N.Ctr * (RadiusCm + 50.0 * double(N.MinH + N.MaxH));
	const double R = 2.0 * FMath::Sin(N.Ang * 0.5) * RadiusCm + 50.0 * double(N.MaxH - N.MinH) + 1000.0;
	const FVector3d V = Pc - CamSim;
	const double Dist = V.Length();
	if (Dist <= R) return true;
	const double Alpha = FMath::Acos(FMath::Clamp((V / Dist) | FwdSim, -1.0, 1.0));
	const double Cone = FMath::Atan(FMath::Sqrt(TanHalfH * TanHalfH + TanHalfV * TanHalfV));
	return Alpha - FMath::Asin(FMath::Min(R / Dist, 1.0)) < Cone + FMath::DegreesToRadians(4.0);
}

double ASurfacePlanet::ErrorPx(const FQNode& N) const
{
	const double S = FMath::Acos(FMath::Clamp(N.C[0] | N.C[1], -1.0, 1.0)) * RadiusCm / K; // cm
	const FVector3d Pc = N.Ctr * (RadiusCm + 50.0 * double(N.MinH + N.MaxH));
	const double R = 2.0 * FMath::Sin(N.Ang * 0.5) * RadiusCm + 50.0 * double(N.MaxH - N.MinH);
	const double Dist = FMath::Max(FVector3d::Distance(Pc, CamSim) - R, 100.0);
	return S / Dist * PxPerRad;
}

void ASurfacePlanet::AddBuiltDescendants(int32 Ni)
{
	const FQNode& N = Nodes[Ni];
	if (N.Comp >= 0) { Draw.Add(Ni); return; }
	if (N.Child >= 0) for (int32 Ch = 0; Ch < 4; ++Ch) AddBuiltDescendants(N.Child + Ch);
}

bool ASurfacePlanet::Ready(int32 Ni) const
{
	const FQNode& N = Nodes[Ni];
	if (N.Comp >= 0 || !AboveHorizon(N)) return true;
	if (N.Child < 0) return false;
	for (int32 Ch = 0; Ch < 4; ++Ch) if (!Ready(N.Child + Ch)) return false;
	return true;
}

void ASurfacePlanet::Visit(int32 Ni)
{
	if (!AboveHorizon(Nodes[Ni])) return;
	// Hysteresis: a split patch merges back only when its error drops well below the split threshold,
	// so LOD borders do not flicker back and forth (each flip re-stitches up to three neighbours).
	const double Thr = Nodes[Ni].bSplit ? TargetPx * 0.7 : TargetPx;
	const bool bSplit = Nodes[Ni].Level < MaxLevel && InFrustum(Nodes[Ni]) && ErrorPx(Nodes[Ni]) > Thr;
	Nodes[Ni].bSplit = bSplit;
	if (bSplit)
	{
		EnsureChildren(Ni);
		const int32 C0 = Nodes[Ni].Child;
		bool bReady = true;
		for (int32 Ch = 0; Ch < 4; ++Ch)
		{
			if (!Ready(C0 + Ch)) { bReady = false; if (!Nodes[C0 + Ch].bBuilding && Nodes[C0 + Ch].Comp < 0) Need.AddUnique(C0 + Ch); }
		}
		if (bReady || Nodes[Ni].Comp < 0)
		{
			if (bReady) { for (int32 Ch = 0; Ch < 4; ++Ch) Visit(C0 + Ch); return; }
		}
		else { Draw.Add(Ni); return; }
	}
	if (Nodes[Ni].Comp >= 0) { Draw.Add(Ni); return; }
	if (!Nodes[Ni].bBuilding) Need.AddUnique(Ni);
	AddBuiltDescendants(Ni);
}

void ASurfacePlanet::Select()
{
	Draw.Reset(); Need.Reset();
	for (int32 F = 0; F < 20; ++F) Visit(F);
	DrawSet.Reset();
	DrawSet.Append(Draw);
	// Coarser neighbour across each edge of every drawn patch; a change means a re-stitch.
	for (int32 Ni : Draw)
	{
		FQNode& N = Nodes[Ni];
		for (int32 E = 0; E < 3; ++E)
		{
			const FVector3d M = Nrm(N.C[E] + N.C[(E + 1) % 3]);
			const int32 Nb = DrawnAt(Nrm(M + (M - N.Ctr) * 0.05));
			N.WantNb[E] = (Nb >= 0 && Nodes[Nb].Level < N.Level) ? Nb : -1;
		}
		if (!N.bBuilding && (N.WantNb[0] != N.BuiltNb[0] || N.WantNb[1] != N.BuiltNb[1] || N.WantNb[2] != N.BuiltNb[2])) Need.AddUnique(Ni);
	}
	for (int32 Ni = 0; Ni < Nodes.Num(); ++Ni)
	{
		FQNode& N = Nodes[Ni];
		if (N.Comp < 0) continue;
		const bool bDraw = DrawSet.Contains(Ni);
		if (!bDraw && Ni >= 20) Release(Ni);
		else Comps[N.Comp]->SetVisibility(bDraw);
	}
	DrawPatches = Draw.Num(); DrawLandTris = 0; DrawWaterTris = 0; MaxDrawLevel = 0;
	FMemory::Memzero(LevelCount, sizeof(LevelCount));
	for (int32 Ni : Draw)
	{
		DrawLandTris += Nodes[Ni].LandTris; DrawWaterTris += Nodes[Ni].WaterTris;
		MaxDrawLevel = FMath::Max(MaxDrawLevel, Nodes[Ni].Level);
		LevelCount[FMath::Min(Nodes[Ni].Level, 19)]++;
	}
}

void ASurfacePlanet::Release(int32 Ni)
{
	FQNode& N = Nodes[Ni];
	if (N.Comp < 0) return;
	UProceduralMeshComponent* C = Comps[N.Comp];
	C->ClearAllMeshSections();
	C->SetVisibility(false);
	FreeComps.Add(N.Comp);
	N.Comp = -1;
}

void ASurfacePlanet::Apply(int32 Ni, FPatchData& D)
{
	FQNode& N = Nodes[Ni];
	N.MinH = D.MinH; N.MaxH = D.MaxH; N.LandTris = D.LandTris; N.WaterTris = D.WaterTris;
	for (int32 E = 0; E < 3; ++E) N.BuiltNb[E] = D.BuiltNb[E];
	BuildMsTotal += D.Ms; ++BuildsTotal;
	int32 Ci;
	if (N.Comp >= 0) Ci = N.Comp; // rebuild in place (new stitching)
	else if (FreeComps.Num()) Ci = FreeComps.Pop();
	else
	{
		UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(this);
		C->SetupAttachment(RootComponent);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(true);
		C->bUseAsyncCooking = true;
		C->SetAffectDistanceFieldLighting(false);
		C->RegisterComponent();
		Ci = Comps.Add(C);
	}
	UProceduralMeshComponent* C = Comps[Ci];
	C->SetWorldLocation(D.Origin);
	C->CreateMeshSection_LinearColor(0, D.V, D.I, D.Nn, D.U0, D.U1, D.U2, D.U3, D.Col, {}, false, false);
	C->SetMaterial(0, LandMat);
	if (D.WI.Num())
	{
		C->CreateMeshSection_LinearColor(1, D.WV, D.WI, D.WN, D.WU0, D.WU1, {}, {}, {}, {}, false, false);
		C->SetMaterial(1, WaterMat);
	}
	else C->ClearMeshSection(1);
	C->SetVisibility(true);
	N.Comp = Ci;
}

FVector3d ASurfacePlanet::GridDir(const FVector3d C[3], int32 K, int32 I, int32 J)
{
	// One formula for every patch: shared edges evaluate bit-identical directions (k / K is exact for
	// K a power of two and a zero-weighted corner contributes an exact zero).
	const double Inv = 1.0 / K;
	return Nrm(C[0] * (double(K - I - J) * Inv) + C[1] * (double(I) * Inv) + C[2] * (double(J) * Inv));
}

FVector3d ASurfacePlanet::NormalAt(const FSurfaceTerrain& T, const FVector3d& D, double H0, double S, bool bRiv)
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

void ASurfacePlanet::BuildPatch(const FSurfaceTerrain& T, const FQNode& N, int32 K, double RadiusCm, const FStitch St[3], FPatchData& O)
{
	const double T0 = FPlatformTime::Seconds();
	const double S = FMath::Acos(FMath::Clamp(N.C[0] | N.C[1], -1.0, 1.0)) * DatumM / K; // vertex spacing, m
	const bool bRiv = T.RiversNear(N.Ctr, N.Ang + 0.01);
	const int32 NV = (K + 1) * (K + 2) / 2;
	auto Idx = [K](int32 I, int32 J) { return I * (K + 1) - I * (I - 1) / 2 + J; };
	TArray<FVector3d> Dir; Dir.SetNumUninitialized(NV);
	TArray<FVector3d> PosA; PosA.SetNumUninitialized(NV);  // absolute land position, sim frame, cm
	TArray<FVector3d> WPosA; WPosA.SetNumUninitialized(NV); // absolute water position
	TArray<double> H; H.SetNumUninitialized(NV);
	TArray<FVector3d> Nm; Nm.SetNumUninitialized(NV);
	TArray<FSurfAttr> At; At.SetNum(NV);
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
			WPosA[V] = D * RadiusCm;
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
					CW[Z] = Dc * RadiusCm;
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
	auto Put = [&](int32 Dst, int32 Src, double DropCm)
	{
		O.V[Dst] = ToUE(PosA[Src] - Dir[Src] * DropCm - OriginSim);
		O.Nn[Dst] = ToUE(Nm[Src]);
		const FSurfAttr& A = At[Src];
		// Attribute layout shared with Scripts/hlsl/land.hlsl
		// (PMC gives 4 half-float UV pairs + one 8-bit colour. Never pack two fields into one
		// interpolated channel: interpolation across a packing step decodes garbage.)
		O.U0[Dst] = FVector2D(A.BeachZone, A.Arid);
		O.U1[Dst] = FVector2D(A.ReliefE, A.ReliefR);
		O.U2[Dst] = FVector2D(LogS, A.Snow - A.Wet);
		O.U3[Dst] = FVector2D(A.RiverD, A.RiverW);
		O.Col[Dst] = FLinearColor(A.Albedo.X, A.Albedo.Y, A.Albedo.Z, A.Forest);
	};
	for (int32 V = 0; V < NV; ++V) Put(V, V, 0.0);
	for (int32 E = 0; E < 3; ++E)
		for (int32 Q = 0; Q <= K; ++Q)
		{
			Put(NV + E * (K + 1) + Q, Edge[E][Q], DebugMode == 2 ? 0.0 : SkirtCm);
			if (DebugMode == 1) O.Col[NV + E * (K + 1) + Q] = FLinearColor(1, 0, 1, 0);
		}
	if (DebugMode == 4) { const uint32 Hh = SurfNoise::Hash(int32(N.Ctr.X * 1e6), int32(N.Ctr.Y * 1e6), int32(N.Ctr.Z * 1e6)); for (int32 V = 0; V < NV; ++V) O.Col[V] = FLinearColor((Hh & 255) / 255.f, ((Hh >> 8) & 255) / 255.f, ((Hh >> 16) & 255) / 255.f, 0); }
	if (DebugMode == 3) for (int32 V = 0; V < NV; ++V) O.Col[V] = FLinearColor(FMath::Frac(N.Level * 0.37), FMath::Frac(N.Level * 0.61 + 0.3), FMath::Frac(N.Level * 0.17 + 0.6), 0);
	O.I.Reserve(K * K * 3 + 3 * K * 12);
	auto Tri = [&](TArray<int32>& Out, int32 A, int32 B, int32 C)
	{
		if (bFlipWinding) Out.Append({ A, C, B }); else Out.Append({ A, B, C });
	};
	for (int32 I = 0; I < K; ++I)
		for (int32 J = 0; I + J < K; ++J)
		{
			Tri(O.I, Idx(I, J), Idx(I + 1, J), Idx(I, J + 1));
			if (I + J < K - 1) Tri(O.I, Idx(I + 1, J), Idx(I + 1, J + 1), Idx(I, J + 1));
		}
	for (int32 E = 0; E < 3; ++E)
		for (int32 Q = 0; Q < K; ++Q)
		{
			const int32 A = Edge[E][Q], B = Edge[E][Q + 1], As = NV + E * (K + 1) + Q, Bs = As + 1;
			Tri(O.I, A, As, B); Tri(O.I, B, As, Bs); // both windings: the skirt is seen from either side
			Tri(O.I, A, B, As); Tri(O.I, B, Bs, As);
		}
	O.LandTris = O.I.Num() / 3;

	// ---- water: only where the ground dips below (or near) sea level
	if (MinH < 2.0)
	{
		TArray<int32> Map; Map.Init(-1, NV);
		auto WVert = [&](int32 Src) -> int32
		{
			if (Map[Src] >= 0) return Map[Src];
			const int32 Id = O.WV.Num();
			O.WV.Add(ToUE(WPosA[Src] - OriginSim));
			O.WN.Add(ToUE(Dir[Src]));
			O.WU0.Add(FVector2D(-H[Src], At[Src].RiverD));
			O.WU1.Add(FVector2D(LogS, 0.0));
			Map[Src] = Id;
			return Id;
		};
		auto Wet = [&](int32 A, int32 B, int32 C) { return FMath::Min3(H[A], H[B], H[C]) < 1.0; };
		for (int32 I = 0; I < K; ++I)
			for (int32 J = 0; I + J < K; ++J)
			{
				int32 A = Idx(I, J), B = Idx(I + 1, J), C = Idx(I, J + 1);
				if (Wet(A, B, C)) Tri(O.WI, WVert(A), WVert(B), WVert(C));
				if (I + J < K - 1) { A = Idx(I + 1, J); B = Idx(I + 1, J + 1); C = Idx(I, J + 1); if (Wet(A, B, C)) Tri(O.WI, WVert(A), WVert(B), WVert(C)); }
			}
		for (int32 E = 0; E < 3; ++E)
			for (int32 Q = 0; Q < K; ++Q)
			{
				const int32 A = Edge[E][Q], B = Edge[E][Q + 1];
				if (FMath::Min(H[A], H[B]) >= 1.0) continue;
				const int32 Wa = WVert(A), Wb = WVert(B);
				const int32 Sa = O.WV.Num();
				for (int32 Src : { A, B })
				{
					O.WV.Add(ToUE(WPosA[Src] - Dir[Src] * SkirtCm - OriginSim));
					O.WN.Add(ToUE(Dir[Src]));
					O.WU0.Add(FVector2D(-H[Src], At[Src].RiverD));
					O.WU1.Add(FVector2D(LogS, 0.0));
				}
				Tri(O.WI, Wa, Sa, Wb); Tri(O.WI, Wb, Sa, Sa + 1);
				Tri(O.WI, Wa, Wb, Sa); Tri(O.WI, Wb, Sa + 1, Sa);
			}
		O.WaterTris = O.WI.Num() / 3;
	}
	for (int32 E = 0; E < 3; ++E) O.BuiltNb[E] = St[E].Node;
	O.Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
}

bool ASurfacePlanet::Contains(const FQNode& N, const FVector3d& P)
{
	const double Eps = -1e-13;
	return (P | (N.C[0] ^ N.C[1])) >= Eps && (P | (N.C[1] ^ N.C[2])) >= Eps && (P | (N.C[2] ^ N.C[0])) >= Eps;
}

int32 ASurfacePlanet::DrawnAt(const FVector3d& P) const
{
	int32 Ni = -1;
	for (int32 F = 0; F < 20; ++F) if (Contains(Nodes[F], P)) { Ni = F; break; }
	while (Ni >= 0)
	{
		if (DrawSet.Contains(Ni)) return Ni;
		const FQNode& N = Nodes[Ni];
		if (N.Child < 0) return -1;
		int32 Next = -1;
		for (int32 Ch = 0; Ch < 4; ++Ch) if (Contains(Nodes[N.Child + Ch], P)) { Next = N.Child + Ch; break; }
		Ni = Next;
	}
	return -1;
}

void ASurfacePlanet::MakeStitch(int32 Ni, FStitch Out[3]) const
{
	for (int32 E = 0; E < 3; ++E)
	{
		Out[E] = FStitch();
		const int32 Nb = Nodes[Ni].WantNb[E];
		if (Nb < 0) continue;
		Out[E].bOn = true; Out[E].Node = Nb;
		for (int32 K3 = 0; K3 < 3; ++K3) Out[E].CC[K3] = Nodes[Nb].C[K3];
		Out[E].Sc = FMath::Acos(FMath::Clamp(Nodes[Nb].C[0] | Nodes[Nb].C[1], -1.0, 1.0)) * DatumM / K;
	}
}

void ASurfacePlanet::Settle(int32 MaxIters)
{
	const double T0 = FPlatformTime::Seconds();
	for (int32 It = 0; It < MaxIters; ++It)
	{
		Select();
		UE_LOG(LogTemp, Verbose, TEXT("Settle it %d: draw %d need %d maxlevel %d nodes %d"), It, Draw.Num(), Need.Num(), MaxDrawLevel, Nodes.Num());
		if (Need.Num() == 0) break;
		TArray<int32> Work = Need;
		TArray<FPatchData> Out; Out.SetNum(Work.Num());
		TArray<FStitch> Sts; Sts.SetNum(Work.Num() * 3);
		for (int32 W = 0; W < Work.Num(); ++W) MakeStitch(Work[W], &Sts[W * 3]);
		ParallelFor(Work.Num(), [&](int32 W) { BuildPatch(T, Nodes[Work[W]], K, RadiusCm, &Sts[W * 3], Out[W]); });
		for (int32 W = 0; W < Work.Num(); ++W) Apply(Work[W], Out[W]);
	}
	Select();
	LastSettleMs = (FPlatformTime::Seconds() - T0) * 1000.0;
}

void ASurfacePlanet::Step(int32 MaxStarts)
{
	// Apply finished async builds (a bounded number per frame: each one re-creates a render proxy).
	int32 Applied = 0;
	for (int32 Ni = 0; Ni < Nodes.Num() && Applied < MaxAppliesPerFrame; ++Ni)
	{
		FQNode& N = Nodes[Ni];
		if (N.bBuilding && N.Pending.IsValid() && N.Pending->bDone.load())
		{
			Apply(Ni, N.Pending->D);
			N.Pending.Reset();
			N.bBuilding = false;
			++Applied;
		}
	}
	if (bFreeze) return;
	Select();
	// Start new builds: first-time builds (they unblock splits) before re-stitches, nearest first.
	Need.Sort([this](int32 A, int32 B)
	{
		const bool NewA = Nodes[A].Comp < 0, NewB = Nodes[B].Comp < 0;
		if (NewA != NewB) return NewA;
		return ErrorPx(Nodes[A]) > ErrorPx(Nodes[B]);
	});
	for (int32 W = 0; W < Need.Num() && W < MaxStarts; ++W)
	{
		const int32 Ni = Need[W];
		FQNode& N = Nodes[Ni];
		N.bBuilding = true;
		TSharedPtr<FPendingPatch, ESPMode::ThreadSafe> P = MakeShared<FPendingPatch, ESPMode::ThreadSafe>();
		N.Pending = P;
		FQNode Copy; for (int32 K3 = 0; K3 < 3; ++K3) Copy.C[K3] = N.C[K3];
		Copy.Ctr = N.Ctr; Copy.Ang = N.Ang; Copy.Level = N.Level;
		FStitch St[3]; MakeStitch(Ni, St);
		const FStitch S0 = St[0], S1 = St[1], S2 = St[2];
		const FSurfaceTerrain* TP = &T;
		const int32 KK = K; const double Rc = RadiusCm;
		Async(EAsyncExecution::ThreadPool, [TP, Copy, KK, Rc, P, S0, S1, S2]()
		{
			const FStitch Sx[3] = { S0, S1, S2 };
			BuildPatch(*TP, Copy, KK, Rc, Sx, P->D);
			P->bDone.store(true);
		});
	}
}

FString ASurfacePlanet::StatsLine() const
{
	FString Lv;
	for (int32 L = 0; L < 20; ++L) if (LevelCount[L]) Lv += FString::Printf(TEXT(" L%d:%d"), L, LevelCount[L]);
	return FString::Printf(TEXT("patches %d (max level %d) land tris %d water tris %d total %d | nodes %d comps %d builds %d (%.1f ms avg/patch/thread) |%s"),
		DrawPatches, MaxDrawLevel, DrawLandTris, DrawWaterTris, DrawLandTris + DrawWaterTris, Nodes.Num(), Comps.Num(), BuildsTotal,
		BuildsTotal ? BuildMsTotal / BuildsTotal : 0.0, *Lv);
}

// ---------------------------------------------------------------------------------------------- river map
// Octahedral map of the whole planet (UE frame): R = distance to the nearest river centreline
// (0..RiverSdfRangeM), G = that river's half-width / 64 m, B = river area coverage (box-filtered in the
// mips, for orbit views), A = 1. The material reads the distance field at mip 0 for crisp lines at mid
// range and the coverage at the mip of the pixel footprint from orbit.

FVector2D ASurfacePlanet::OctUV(const FVector3d& D)
{
	const double L1 = FMath::Abs(D.X) + FMath::Abs(D.Y) + FMath::Abs(D.Z);
	FVector2D P(D.X / L1, D.Y / L1);
	if (D.Z < 0.0)
	{
		const double Px = P.X, Py = P.Y;
		P.X = (1.0 - FMath::Abs(Py)) * (Px >= 0.0 ? 1.0 : -1.0);
		P.Y = (1.0 - FMath::Abs(Px)) * (Py >= 0.0 ? 1.0 : -1.0);
	}
	return P * 0.5 + FVector2D(0.5, 0.5);
}

FVector3d ASurfacePlanet::OctDir(const FVector2D& UV)
{
	const FVector2D P = UV * 2.0 - FVector2D(1.0, 1.0);
	FVector3d N(P.X, P.Y, 1.0 - FMath::Abs(P.X) - FMath::Abs(P.Y));
	if (N.Z < 0.0)
	{
		const double Nx = N.X, Ny = N.Y;
		N.X = (1.0 - FMath::Abs(Ny)) * (Nx >= 0.0 ? 1.0 : -1.0);
		N.Y = (1.0 - FMath::Abs(Nx)) * (Ny >= 0.0 ? 1.0 : -1.0);
	}
	return Nrm(N);
}

void ASurfacePlanet::BuildRiverTexture(int32 Size)
{
	const double T0 = FPlatformTime::Seconds();
	const double RangeM = RiverSdfRangeM;
	const double TexelM = FMath::Sqrt(4.0 * PI * DatumM * DatumM / (double(Size) * Size));
	TArray<float> Dist; Dist.Init(float(RangeM), Size * Size);
	TArray<float> HalfW; HalfW.Init(0.f, Size * Size);
	TArray<float> Cov; Cov.Init(0.f, Size * Size);
	const int32 R = FMath::CeilToInt(RangeM / TexelM) + 1;
	for (int32 Si : T.RSeg)
	{
		const FVector3d A = ToUE(T.RPts[Si].P), B = ToUE(T.RPts[Si + 1].P);
		const double Hw = 0.5 * (T.RPts[Si].HalfW + T.RPts[Si + 1].HalfW);
		const FVector2D Uv = OctUV(Nrm(A + B));
		const int32 Cx = FMath::FloorToInt(Uv.X * Size), Cy = FMath::FloorToInt(Uv.Y * Size);
		// coverage: river area of this segment over the texel area, exaggerated 5x for readability
		const double SegM = FVector3d::Distance(A, B) * DatumM;
		if (Cx >= 0 && Cy >= 0 && Cx < Size && Cy < Size) Cov[Cy * Size + Cx] += float(5.0 * 2.0 * Hw * SegM / (TexelM * TexelM));
		const FVector3d AB = B - A;
		for (int32 Y = FMath::Max(Cy - R, 0); Y <= FMath::Min(Cy + R, Size - 1); ++Y)
			for (int32 X = FMath::Max(Cx - R, 0); X <= FMath::Min(Cx + R, Size - 1); ++X)
			{
				const FVector3d D = OctDir(FVector2D((X + 0.5) / Size, (Y + 0.5) / Size));
				const double T_ = FMath::Clamp(((D - A) | AB) / FMath::Max(AB | AB, 1e-30), 0.0, 1.0);
				const double Dm = (A + AB * T_ - D).Length() * DatumM;
				const int32 I = Y * Size + X;
				if (Dm < Dist[I]) { Dist[I] = float(Dm); HalfW[I] = float(Hw); }
			}
	}
	// mip chain
	TArray<TArray<FColor>> Mips;
	Mips.AddDefaulted();
	Mips[0].SetNumUninitialized(Size * Size);
	for (int32 I = 0; I < Size * Size; ++I)
		Mips[0][I] = FColor(uint8(FMath::Clamp(FMath::RoundToInt(Dist[I] / RangeM * 255.0), 0, 255)),
			uint8(FMath::Clamp(FMath::RoundToInt(HalfW[I] / 64.0 * 255.0), 0, 255)),
			uint8(FMath::Clamp(FMath::RoundToInt(Cov[I] * 255.0), 0, 255)), 255);
	TArray<float> CovM = Cov;
	for (float& V : CovM) V = FMath::Min(V, 1.f);
	for (int32 S = Size / 2; S >= 1; S /= 2)
	{
		TArray<float> Next; Next.SetNumUninitialized(S * S);
		for (int32 Y = 0; Y < S; ++Y)
			for (int32 X = 0; X < S; ++X)
			{
				const int32 P = S * 2;
				Next[Y * S + X] = 0.25f * (CovM[(2 * Y) * P + 2 * X] + CovM[(2 * Y) * P + 2 * X + 1] + CovM[(2 * Y + 1) * P + 2 * X] + CovM[(2 * Y + 1) * P + 2 * X + 1]);
			}
		CovM = MoveTemp(Next);
		TArray<FColor>& M = Mips.AddDefaulted_GetRef();
		M.SetNumUninitialized(S * S);
		for (int32 I = 0; I < S * S; ++I) M[I] = FColor(255, 0, uint8(FMath::Clamp(FMath::RoundToInt(CovM[I] * 255.0), 0, 255)), 255);
	}
	{
		// debug: count texels near rivers
		int32 Near = 0, Covd = 0; for (int32 I = 0; I < Size * Size; ++I) { if (Mips[0][I].R < 255) ++Near; if (Mips[0][I].B > 0) ++Covd; }
		UE_LOG(LogTemp, Display, TEXT("SurfacePlanet: river map texels near a river %d, with coverage %d"), Near, Covd);
	}
	RiverTex = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8, TEXT("RiverMap"));
	RiverTex->SRGB = false;
	RiverTex->Filter = TF_Trilinear;
	RiverTex->AddressX = TA_Clamp; RiverTex->AddressY = TA_Clamp;
	RiverTex->NeverStream = true;
	FTexturePlatformData* PD = RiverTex->GetPlatformData();
	for (int32 M = 0; M < Mips.Num(); ++M)
	{
		const int32 S = Size >> M;
		FTexture2DMipMap* Mip;
		if (M == 0) Mip = &PD->Mips[0];
		else { Mip = new FTexture2DMipMap(S, S, 1); PD->Mips.Add(Mip); }
		void* Dst = Mip->BulkData.Lock(LOCK_READ_WRITE);
		if (M > 0) Dst = Mip->BulkData.Realloc(int64(S) * S * 4);
		FMemory::Memcpy(Dst, Mips[M].GetData(), int64(S) * S * 4);
		Mip->BulkData.Unlock();
	}
	RiverTex->UpdateResource();
	UE_LOG(LogTemp, Display, TEXT("SurfacePlanet: river map %dx%d (%.1f m/texel, %d mips) in %.0f ms"), Size, Size, TexelM, Mips.Num(), (FPlatformTime::Seconds() - T0) * 1000.0);
}

// ---------------------------------------------------------------------------------------------- ground lab

namespace
{
	UTexture2D* MakeTex(UObject* Outer, int32 Size, EPixelFormat Fmt, int32 Bpp, const TArray<TArray<uint8>>& Mips, const TCHAR* Name, bool bMips)
	{
		UTexture2D* Tx = UTexture2D::CreateTransient(Size, Size, Fmt, Name);
		Tx->SRGB = false;
		Tx->Filter = bMips ? TF_Trilinear : TF_Bilinear;
		Tx->AddressX = TA_Clamp; Tx->AddressY = TA_Clamp;
		Tx->NeverStream = true;
		FTexturePlatformData* PD = Tx->GetPlatformData();
		for (int32 M = 0; M < Mips.Num(); ++M)
		{
			const int32 S = Size >> M;
			FTexture2DMipMap* Mip;
			if (M == 0) Mip = &PD->Mips[0];
			else { Mip = new FTexture2DMipMap(S, S, 1); PD->Mips.Add(Mip); }
			void* Dst = Mip->BulkData.Lock(LOCK_READ_WRITE);
			if (M > 0) Dst = Mip->BulkData.Realloc(int64(S) * S * Bpp);
			FMemory::Memcpy(Dst, Mips[M].GetData(), int64(S) * S * Bpp);
			Mip->BulkData.Unlock();
		}
		Tx->UpdateResource();
		return Tx;
	}
}

void ASurfacePlanet::BuildGround(const FVector& CenterDirUE, double HalfKm, float Season, float Age)
{
	const double T0 = FPlatformTime::Seconds();
	Land.PlaceSettlements(T);
	Land.BuildTile(T, Nrm(ToSim(CenterDirUE)), HalfKm * 1000.0, 1024, 4096);

	// Global land map (octahedral, UE frame): farm, settlement core, road coverage, temperature.
	const int32 Size = 2048;
	const double TexelM = FMath::Sqrt(4.0 * PI * DatumM * DatumM / (double(Size) * Size));
	TArray<FLinearColor> Px; Px.SetNumZeroed(Size * Size);
	ParallelFor(Size, [&](int32 Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			const FVector3d D = Nrm(ToSim(FVector(OctDir(FVector2D((X + 0.5) / Size, (Y + 0.5) / Size)))));
			double F, Co, Tc;
			Land.FarmAt(T, D, F, Co, Tc);
			Px[Y * Size + X] = FLinearColor(float(F), float(Co), 0.f, float(FMath::Clamp((Tc + 20.0) / 60.0, 0.0, 1.0)));
		}
	});
	for (const FRoad& Rd : Land.Roads)
		for (int32 Ki = 0; Ki + 1 < Rd.P.Num(); ++Ki)
		{
			const FVector3d A = ToUE(Rd.P[Ki]), B = ToUE(Rd.P[Ki + 1]);
			const FVector2D Uv = OctUV(Nrm(A + B));
			const int32 Cx = FMath::Clamp(int32(Uv.X * Size), 0, Size - 1), Cy = FMath::Clamp(int32(Uv.Y * Size), 0, Size - 1);
			Px[Cy * Size + Cx].B += float(4.0 * 2.0 * Rd.HalfW[Ki] * FVector3d::Distance(A, B) * DatumM / (TexelM * TexelM));
		}
	TArray<TArray<uint8>> Mips;
	TArray<FLinearColor> Cur = Px;
	for (int32 S = Size; S >= 1; S /= 2)
	{
		TArray<uint8>& M = Mips.AddDefaulted_GetRef();
		M.SetNumUninitialized(S * S * 4);
		for (int32 I = 0; I < S * S; ++I)
		{
			const FLinearColor& C = Cur[I];
			FColor Q(uint8(FMath::Clamp(FMath::RoundToInt(C.R * 255.f), 0, 255)), uint8(FMath::Clamp(FMath::RoundToInt(C.G * 255.f), 0, 255)),
				uint8(FMath::Clamp(FMath::RoundToInt(FMath::Min(C.B, 1.f) * 255.f), 0, 255)), uint8(FMath::Clamp(FMath::RoundToInt(C.A * 255.f), 0, 255)));
			FMemory::Memcpy(&M[I * 4], &Q, 4); // FColor is BGRA in memory, as PF_B8G8R8A8
		}
		if (S == 1) break;
		const int32 H = S / 2;
		TArray<FLinearColor> Nx; Nx.SetNumUninitialized(H * H);
		for (int32 Y = 0; Y < H; ++Y)
			for (int32 X = 0; X < H; ++X)
			{
				FLinearColor A = Cur[(2 * Y) * S + 2 * X], B = Cur[(2 * Y) * S + 2 * X + 1], C = Cur[(2 * Y + 1) * S + 2 * X], D = Cur[(2 * Y + 1) * S + 2 * X + 1];
				A.B = FMath::Min(A.B, 1.f); B.B = FMath::Min(B.B, 1.f); C.B = FMath::Min(C.B, 1.f); D.B = FMath::Min(D.B, 1.f);
				Nx[Y * H + X] = (A + B + C + D) * 0.25f;
			}
		Cur = MoveTemp(Nx);
	}
	LandTex = MakeTex(this, Size, PF_B8G8R8A8, 4, Mips, TEXT("LandMap"), true);

	// Tile textures
	{
		TArray<TArray<uint8>> M1; M1.AddDefaulted();
		M1[0].SetNumUninitialized(Land.CN * Land.CN * 4);
		FMemory::Memcpy(M1[0].GetData(), Land.HC.GetData(), Land.CN * Land.CN * 4);
		TileHTex = MakeTex(this, Land.CN, PF_R32_FLOAT, 4, M1, TEXT("TileH"), false);
		TArray<TArray<uint8>> M2; M2.AddDefaulted();
		M2[0].SetNumUninitialized(Land.FN * Land.FN * 8);
		FMemory::Memcpy(M2[0].GetData(), Land.Fine.GetData(), int64(Land.FN) * Land.FN * 8);
		TileFTex = MakeTex(this, Land.FN, PF_FloatRGBA, 8, M2, TEXT("TileF"), false);
	}
	if (LandMID)
	{
		const FVector Cu = ToUE(Land.C), Eu = ToUE(Land.E), Nu = ToUE(Land.N);
		LandMID->SetTextureParameterValue(TEXT("LandTex"), LandTex);
		LandMID->SetTextureParameterValue(TEXT("TileH"), TileHTex);
		LandMID->SetTextureParameterValue(TEXT("TileF"), TileFTex);
		LandMID->SetVectorParameterValue(TEXT("TileO"), FLinearColor(float(Cu.X), float(Cu.Y), float(Cu.Z), float(Land.Half)));
		LandMID->SetVectorParameterValue(TEXT("TileE"), FLinearColor(float(Eu.X), float(Eu.Y), float(Eu.Z), float(Land.CN)));
		LandMID->SetVectorParameterValue(TEXT("TileN"), FLinearColor(float(Nu.X), float(Nu.Y), float(Nu.Z), float(Land.FN)));
		LandMID->SetScalarParameterValue(TEXT("Season"), Season);
		LandMID->SetScalarParameterValue(TEXT("Age"), Age);
		float Dbg = 0.f; FParse::Value(FCommandLine::Get(), TEXT("-gdebug="), Dbg);
		LandMID->SetScalarParameterValue(TEXT("GDebug"), Dbg);
	}
	// debug map of the tile (1024^2 raw RGB): farm (green), settlement cores (red), roads (white), rivers (blue), sea (dark)
	if (FParse::Param(FCommandLine::Get(), TEXT("groundmap")))
	{
		const int32 M = 1024;
		TArray<uint8> Img; Img.SetNumZeroed(M * M * 3);
		ParallelFor(M, [&](int32 J)
		{
			for (int32 I = 0; I < M; ++I)
			{
				const double X = (I + 0.5) / M * 2.0 * Land.Half - Land.Half, Y = Land.Half - (J + 0.5) / M * 2.0 * Land.Half;
				const FVector3d D = Land.TileDir(X, Y);
				double F, Co, Tc; Land.FarmAt(T, D, F, Co, Tc);
				const int32 Fi = FMath::Clamp(int32((X + Land.Half) / (2.0 * Land.Half) * Land.FN), 0, Land.FN - 1), Fj = FMath::Clamp(int32((Y + Land.Half) / (2.0 * Land.Half) * Land.FN), 0, Land.FN - 1);
				const FLinearColor Fc = Land.Fine[Fj * Land.FN + Fi].GetFloats();
				const int32 Ci = FMath::Clamp(int32((X + Land.Half) / (2.0 * Land.Half) * Land.CN), 0, Land.CN - 1), Cj = FMath::Clamp(int32((Y + Land.Half) / (2.0 * Land.Half) * Land.CN), 0, Land.CN - 1);
				const float Hh = Land.HC[Cj * Land.CN + Ci];
				float Rr = 0.25f + 0.25f * FMath::Clamp(Hh / 400.f, 0.f, 1.f), Gg = Rr, Bb = Rr;
				if (Hh < 0.f) { Rr = 0.05f; Gg = 0.08f; Bb = 0.2f; }
				Gg += 0.5f * float(F); Rr += 0.7f * float(Co);
				if (Fc.A < 40.f) { Rr = 0.1f; Gg = 0.3f; Bb = 1.f; }
				if (Fc.G < 30.f && Fc.B > 0.f) { Rr = Gg = Bb = 1.f; }
				uint8* Px = &Img[(J * M + I) * 3];
				Px[0] = uint8(FMath::Clamp(Rr, 0.f, 1.f) * 255); Px[1] = uint8(FMath::Clamp(Gg, 0.f, 1.f) * 255); Px[2] = uint8(FMath::Clamp(Bb, 0.f, 1.f) * 255);
			}
		});
		const FString Path = FPaths::ProjectSavedDir() / TEXT("groundmap.rgb");
		FFileHelper::SaveArrayToFile(Img, *Path);
		UE_LOG(LogTemp, Display, TEXT("Ground: debug map %s"), *Path);
	}
	UE_LOG(LogTemp, Display, TEXT("Ground: built in %.0f ms (settlements %d, roads %d chains %.0f km)"), (FPlatformTime::Seconds() - T0) * 1000.0, Land.Sets.Num(), Land.Roads.Num(), Land.Stats[3]);
}
