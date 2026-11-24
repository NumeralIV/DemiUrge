// DEMIURGE — Game/Towns/DmTownsGround.cpp : see the header.

#include "Game/Towns/DmTownsGround.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/Placeholder/DmPlaceholderPlanet.h"
#include "Game/Planet/DmPlanet.h"
#include "Game/Planet/DmPlanetTerrain.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "ProceduralMeshComponent.h"
#include "Sim/State.h"

namespace
{
	FCriticalSection GProviderLock;
	FDmTownsGroundFn GProvider;
	bool GHasProvider = false;

	ADmPlaceholderPlanet* FindPlaceholder(UWorld* World)
	{
		if (!World) return nullptr;
		for (TActorIterator<ADmPlaceholderPlanet> It(World); It; ++It) return *It;
		return nullptr;
	}

	/** Ray from the planet centre along D against triangle (A, B, C): distance or -1. */
	double RayTri(const FVector& D, const FVector3f& A, const FVector3f& B, const FVector3f& C)
	{
		const FVector a(A), b(B), c(C);
		const FVector E1 = b - a, E2 = c - a;
		const FVector P = FVector::CrossProduct(D, E2);
		const double Det = FVector::DotProduct(E1, P);
		if (FMath::Abs(Det) < 1e-6) return -1.0;
		const double Inv = 1.0 / Det;
		const FVector Tv = -a;
		const double U = FVector::DotProduct(Tv, P) * Inv;
		if (U < -1e-7 || U > 1.0 + 1e-7) return -1.0;
		const FVector Q = FVector::CrossProduct(Tv, E1);
		const double Vv = FVector::DotProduct(D, Q) * Inv;
		if (Vv < -1e-7 || U + Vv > 1.0 + 1e-7) return -1.0;
		const double T = FVector::DotProduct(E2, Q) * Inv;
		return T > 0.0 ? T : -1.0;
	}
}

void FDmTownsGround::SetProvider(FDmTownsGroundFn Fn)
{
	FScopeLock L(&GProviderLock);
	GProvider = MoveTemp(Fn);
	GHasProvider = bool(GProvider);
}

void FDmTownsGround::ClearProvider()
{
	FScopeLock L(&GProviderLock);
	GProvider = nullptr;
	GHasProvider = false;
}

bool FDmTownsGround::HasProvider()
{
	FScopeLock L(&GProviderLock);
	return GHasProvider;
}

const TCHAR* FDmTownsGround::SourceName() const
{
	switch (Source)
	{
	case ESource::Provider: return TEXT("provider");
	case ESource::PlanetTerrain: return TEXT("planet terrain");
	case ESource::PlaceholderMesh: return TEXT("placeholder mesh");
	default: return TEXT("sim surface");
	}
}

uint64 FDmTownsGround::Signature(UWorld* World)
{
	if (HasProvider()) return 1;
	if (ADmPlanet* Pl = ADmPlanet::Find(World)) return Pl->Terrain() ? (uint64(uint32(Pl->Terrain()->Version)) << 8) | 5 : 6;
	ADmPlaceholderPlanet* P = FindPlaceholder(World);
	if (!P || !P->Land || P->Land->GetNumSections() == 0) return 2;
	const FProcMeshSection* S = P->Land->GetProcMeshSection(0);
	if (!S || S->ProcVertexBuffer.Num() == 0) return 3;
	uint64 H = uint64(S->ProcVertexBuffer.Num()) * 0x9E3779B97F4A7C15ull;
	const int32 N = S->ProcVertexBuffer.Num();
	for (int32 K = 0; K < 64; ++K)
	{
		const FVector& Pv = S->ProcVertexBuffer[(int64(K) * 7919 * 131) % N].Position;
		H ^= (uint64(FMath::RoundToInt64(Pv.Size())) + 0x9E37ull + (H << 6) + (H >> 2));
	}
	return H | 4;
}

TSharedPtr<const FDmTownsGround, ESPMode::ThreadSafe> FDmTownsGround::Snapshot(UWorld* World, const dm::World& W, bool& bOutPending)
{
	return SnapshotDeferred(World, W, bOutPending, nullptr);
}

void FDmTownsGround::AdoptPlanetTerrain(const TSharedPtr<const FDmPlanetTerrain, ESPMode::ThreadSafe>& NewT)
{
	Planet = NewT;
	Source = ESource::PlanetTerrain;
	if (NewT.IsValid()) SeaRadiusUU = 1.0e7 + NewT->SeaM * 100.0;
}

void FDmTownsGround::AdoptPlanetCopy(const FDmPlanetTerrain& Src)
{
	const double T0 = FPlatformTime::Seconds();
	Planet = MakeShared<FDmPlanetTerrain, ESPMode::ThreadSafe>(Src);
	BuildMs += (FPlatformTime::Seconds() - T0) * 1000.0;
}

TSharedPtr<FDmTownsGround, ESPMode::ThreadSafe> FDmTownsGround::SnapshotDeferred(UWorld* World, const dm::World& W, bool& bOutPending, const FDmPlanetTerrain** OutDeferred)
{
	if (OutDeferred) *OutDeferred = nullptr;
	bOutPending = false;
	const double T0 = FPlatformTime::Seconds();
	TSharedPtr<FDmTownsGround, ESPMode::ThreadSafe> G = MakeShared<FDmTownsGround, ESPMode::ThreadSafe>();
	G->Geo = W.geo;
	G->Elev.SetNumUninitialized(W.n);
	for (int32 C = 0; C < W.n; ++C) G->Elev[C] = W.elev[size_t(C)];
	G->SeaRadiusUU = DmCoords::HeightToRadiusUU(W.seaLevel);
	{
		FScopeLock L(&GProviderLock);
		if (GHasProvider) { G->Provider = GProvider; G->Source = ESource::Provider; }
	}
	if (G->Source != ESource::Provider)
		if (ADmPlanet* Pl = ADmPlanet::Find(World))
		{
			if (const FDmPlanetTerrain* PT = Pl->Terrain())
			{
				// a private copy: plain data, pure Height(). (towns-perf) The copy costs ~10 ms; with OutDeferred the caller makes it
				// on a worker (AdoptPlanetCopy) and guarantees the planet keeps this snapshot alive meanwhile
				if (OutDeferred) *OutDeferred = PT;
				else G->Planet = MakeShared<FDmPlanetTerrain, ESPMode::ThreadSafe>(*PT);
				G->Source = ESource::PlanetTerrain;
				G->SeaRadiusUU = 1.0e7 + PT->SeaM * 100.0;
			}
			else bOutPending = true;
		}
	if (G->Source != ESource::Provider && G->Source != ESource::PlanetTerrain && !bOutPending)
	{
		ADmPlaceholderPlanet* P = FindPlaceholder(World);
		if (P && P->Land)
		{
			const FProcMeshSection* S = P->Land->GetNumSections() > 0 ? P->Land->GetProcMeshSection(0) : nullptr;
			if (!S || S->ProcVertexBuffer.Num() == 0 || S->ProcIndexBuffer.Num() < 3) bOutPending = true;
			else if (W.geo)
			{
				const int32 Nv = S->ProcVertexBuffer.Num();
				G->V.SetNumUninitialized(Nv);
				for (int32 I = 0; I < Nv; ++I) G->V[I] = FVector3f(S->ProcVertexBuffer[I].Position);
				G->T.Append(S->ProcIndexBuffer);
				const int32 Nt = G->T.Num() / 3;
				// bucket every triangle by the nearest cell of its centroid (CSR)
				TArray<int32> CellOf; CellOf.SetNumUninitialized(Nt);
				TArray<uint32> Count; Count.SetNumZeroed(W.n + 1);
				for (int32 Ti = 0; Ti < Nt; ++Ti)
				{
					const FVector3f Cc = (G->V[G->T[3 * Ti]] + G->V[G->T[3 * Ti + 1]] + G->V[G->T[3 * Ti + 2]]);
					const dm::Vec3 Sd = DmCoords::UEToSim(FVector(Cc).GetSafeNormal());
					const int32 C = W.geo->cellAt(Sd.x, Sd.y, Sd.z, Ti > 0 ? CellOf[Ti - 1] : -1);
					CellOf[Ti] = C;
					if (C >= 0 && C < W.n) ++Count[C];
				}
				G->Start.SetNumZeroed(W.n + 1);
				for (int32 C = 0; C < W.n; ++C) G->Start[C + 1] = G->Start[C] + Count[C];
				G->Items.SetNumUninitialized(int32(G->Start[W.n]));
				TArray<uint32> Fill = G->Start;
				for (int32 Ti = 0; Ti < Nt; ++Ti)
				{
					const int32 C = CellOf[Ti];
					if (C >= 0 && C < W.n) G->Items[int32(Fill[C]++)] = uint32(Ti);
				}
				G->Triangles = Nt;
				G->Source = ESource::PlaceholderMesh;
			}
		}
	}
	G->Sig = Signature(World);
	G->BuildMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	return G;
}

double FDmTownsGround::SimRadius(const FVector& Dir) const
{
	if (!Geo || Elev.Num() != Geo->n) return 1.0e7;
	const dm::Vec3 S = DmCoords::UEToSim(Dir);
	const int32 C = Geo->cellAt(S.x, S.y, S.z);
	if (C < 0) return 1.0e7;
	auto P = [this](int32 K) { const float* Q = &Geo->pos[size_t(K) * 3]; return DmCoords::SimToUE(Q[0], Q[1], Q[2]); };
	const FVector Pc = P(C);
	const int32 Deg = Geo->nbrN[size_t(C)];
	for (int32 K = 0; K < Deg; ++K)
	{
		const int32 A = Geo->nbr[size_t(6 * C + K)], B = Geo->nbr[size_t(6 * C + (K + 1) % Deg)];
		const FVector Pa = P(A), Pb = P(B);
		// barycentric of the ray through the planar triangle (C, A, B)
		const FVector E1 = Pa - Pc, E2 = Pb - Pc;
		const FVector Pv = FVector::CrossProduct(Dir, E2);
		const double Det = FVector::DotProduct(E1, Pv);
		if (FMath::Abs(Det) < 1e-12) continue;
		const double Inv = 1.0 / Det;
		const FVector Tv = -Pc;
		const double U = FVector::DotProduct(Tv, Pv) * Inv;
		const FVector Q = FVector::CrossProduct(Tv, E1);
		const double Vv = FVector::DotProduct(Dir, Q) * Inv;
		if (U < -1e-6 || Vv < -1e-6 || U + Vv > 1.0 + 1e-6) continue;
		const double H = (1.0 - U - Vv) * Elev[C] + U * Elev[A] + Vv * Elev[B];
		return DmCoords::HeightToRadiusUU(H);
	}
	return DmCoords::HeightToRadiusUU(Elev[C]);
}

bool FDmTownsGround::MeshRadius(const FVector& Dir, double& OutR) const
{
	if (!Geo || Start.Num() != Geo->n + 1) return false;
	const dm::Vec3 S = DmCoords::UEToSim(Dir);
	const int32 C = Geo->cellAt(S.x, S.y, S.z);
	if (C < 0) return false;
	auto TestCell = [&](int32 K) -> bool
	{
		for (uint32 It = Start[K]; It < Start[K + 1]; ++It)
		{
			const uint32 Ti = Items[int32(It)];
			const double D = RayTri(Dir, V[T[3 * Ti]], V[T[3 * Ti + 1]], V[T[3 * Ti + 2]]);
			if (D > 0.0) { OutR = D; return true; }
		}
		return false;
	};
	if (TestCell(C)) return true;
	const int32 Deg = Geo->nbrN[size_t(C)];
	for (int32 K = 0; K < Deg; ++K) if (TestCell(Geo->nbr[size_t(6 * C + K)])) return true;
	return false;
}

double FDmTownsGround::RadiusBand(const FVector& Dir, double SpacingM) const
{
	const FVector D = Dir.GetSafeNormal();
	if (Source == ESource::Provider && Provider) return Provider(D);
	if (Source == ESource::PlanetTerrain && Planet) return 1.0e7 + Planet->Height(FVector3d(D.X, D.Z, D.Y).GetSafeNormal(), FMath::Max(SpacingM, 0.5)) * 100.0;
	if (Source == ESource::PlaceholderMesh)
	{
		double R;
		if (MeshRadius(D, R)) return R;
	}
	return SimRadius(D);
}

namespace { FVector3d ToSimDir(const FVector& Dir) { const FVector D = Dir.GetSafeNormal(); return FVector3d(D.X, D.Z, D.Y).GetSafeNormal(); } }

double FDmTownsGround::RiverBankM(const FVector& Dir) const
{
	if (Source != ESource::PlanetTerrain || !Planet) return 1.0e9;
	double Dist = 1e9, HalfW = 0;
	Planet->RiverQuery(ToSimDir(Dir), Dist, HalfW);
	return Dist >= 1e8 ? 1.0e9 : Dist - HalfW;
}

double FDmTownsGround::LakeSurfaceM(const FVector& Dir) const
{
	if (Source != ESource::PlanetTerrain || !Planet) return -1.0e9;
	return Planet->LakeAt(ToSimDir(Dir));
}

bool FDmTownsGround::RiversNear(const FVector& Dir, double AngRad) const
{
	return Source == ESource::PlanetTerrain && Planet && Planet->RiversNear(ToSimDir(Dir), AngRad);
}

void FDmTownsGround::RiverSegments(const FVector& Dir, double AngRad, TArray<FVector>& OutA, TArray<FVector>& OutB, TArray<float>& OutHalfW) const
{
	OutA.Reset(); OutB.Reset(); OutHalfW.Reset();
	if (Source != ESource::PlanetTerrain || !Planet || Planet->RGrid.Num() == 0) return;
	const FVector3d S = ToSimDir(Dir);
	const double Cs = FDmPlanetTerrain::RiverGridCell, R = AngRad + Cs;
	const FIntVector Lo(FMath::FloorToInt((S.X - R) / Cs), FMath::FloorToInt((S.Y - R) / Cs), FMath::FloorToInt((S.Z - R) / Cs));
	const FIntVector Hi(FMath::FloorToInt((S.X + R) / Cs), FMath::FloorToInt((S.Y + R) / Cs), FMath::FloorToInt((S.Z + R) / Cs));
	TSet<int32> Seen;
	const double CosLim = FMath::Cos(AngRad + 0.002);
	for (int32 Z = Lo.Z; Z <= Hi.Z; ++Z)
		for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
			for (int32 X = Lo.X; X <= Hi.X; ++X)
				if (const TArray<int32>* Cell = Planet->RGrid.Find(FIntVector(X, Y, Z)))
					for (int32 Si : *Cell)
					{
						if (Si < 0 || Si + 1 >= Planet->RPts.Num()) continue;
						// (verifier fix) TSet::Add's flag is "already in the set": read as "new", it skipped every reach, so no
						// town ever saw a river (all 96 fixture plans: 0 reaches) and houses stood in the rivers
						bool bAlready = false;
						Seen.Add(Si, &bAlready);
						if (bAlready) continue;
						const FDmRiverPt& A = Planet->RPts[Si];
						const FDmRiverPt& B = Planet->RPts[Si + 1];
						if ((A.P | S) < CosLim && (B.P | S) < CosLim) continue;
						OutA.Add(FVector(A.P.X, A.P.Z, A.P.Y));
						OutB.Add(FVector(B.P.X, B.P.Z, B.P.Y));
						OutHalfW.Add(0.5f * (A.HalfW + B.HalfW));
					}
}

FVector FDmTownsGround::Normal(const FVector& Dir, double StepM) const
{
	const FVector D = Dir.GetSafeNormal();
	FVector E, N;
	DmCoords::Basis(D, E, N);
	const double Ang = StepM * 100.0 / 1.0e7;
	const FVector P0 = D * Radius(D);
	const FVector De = (D + E * Ang).GetSafeNormal(), Dn = (D + N * Ang).GetSafeNormal();
	const FVector Pe = De * Radius(De), Pn = Dn * Radius(Dn);
	FVector Nrm = FVector::CrossProduct(Pe - P0, Pn - P0).GetSafeNormal();
	if (FVector::DotProduct(Nrm, D) < 0) Nrm = -Nrm;
	return Nrm.IsNearlyZero() ? D : Nrm;
}
