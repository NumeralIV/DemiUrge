// DEMIURGE — Game/Nature/DmNatureGround.cpp : see the header.

#include "Game/Nature/DmNatureGround.h"
#include "Game/Planet/DmPlanetTerrain.h"
#include "Game/Core/DmCoords.h"

#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "Sim/Core/Geo.h"
#include "Sim/State.h"

namespace
{
	FCriticalSection GProviderLock;
	FDmNatureGroundFn GProvider;
	constexpr double kQuarterPi = UE_DOUBLE_PI * 0.25;

	/** Ray from the origin along unit Dir against triangle (A, B, C) (double Moller-Trumbore). */
	bool RayTri(const FVector& Dir, const FVector& A, const FVector& B, const FVector& C, double& OutT, double Eps = 1e-9)
	{
		const FVector E1 = B - A, E2 = C - A;
		const FVector P = FVector::CrossProduct(Dir, E2);
		const double Det = FVector::DotProduct(E1, P);
		if (FMath::Abs(Det) < 1e-30) return false;
		const double Inv = 1.0 / Det;
		const FVector S = -A;
		const double U = FVector::DotProduct(S, P) * Inv;
		if (U < -Eps || U > 1.0 + Eps) return false;
		const FVector Q = FVector::CrossProduct(S, E1);
		const double W = FVector::DotProduct(Dir, Q) * Inv;
		if (W < -Eps || U + W > 1.0 + Eps) return false;
		OutT = FVector::DotProduct(E2, Q) * Inv;
		return OutT > 0.0;
	}
}

// ---------------------------------------------------------------- cube addressing

void DmCube::FaceAxes(int32 Face, FVector& N, FVector& U, FVector& V)
{
	switch (Face)
	{
	case 0: N = FVector(1, 0, 0); U = FVector(0, 1, 0); V = FVector(0, 0, 1); break;
	case 1: N = FVector(-1, 0, 0); U = FVector(0, -1, 0); V = FVector(0, 0, 1); break;
	case 2: N = FVector(0, 1, 0); U = FVector(-1, 0, 0); V = FVector(0, 0, 1); break;
	case 3: N = FVector(0, -1, 0); U = FVector(1, 0, 0); V = FVector(0, 0, 1); break;
	case 4: N = FVector(0, 0, 1); U = FVector(0, 1, 0); V = FVector(-1, 0, 0); break;
	default: N = FVector(0, 0, -1); U = FVector(0, 1, 0); V = FVector(1, 0, 0); break;
	}
}

bool DmCube::OnFace(int32 Face, const FVector& Dir, double& OutU, double& OutV)
{
	FVector N, U, V;
	FaceAxes(Face, N, U, V);
	const double D = FVector::DotProduct(Dir, N);
	if (D <= 1e-6) return false;
	OutU = FMath::Atan(FVector::DotProduct(Dir, U) / D) / kQuarterPi;
	OutV = FMath::Atan(FVector::DotProduct(Dir, V) / D) / kQuarterPi;
	return true;
}

int32 DmCube::ToFace(const FVector& Dir, double& OutU, double& OutV)
{
	const double Ax = FMath::Abs(Dir.X), Ay = FMath::Abs(Dir.Y), Az = FMath::Abs(Dir.Z);
	int32 F;
	if (Ax >= Ay && Ax >= Az) F = Dir.X >= 0 ? 0 : 1;
	else if (Ay >= Az) F = Dir.Y >= 0 ? 2 : 3;
	else F = Dir.Z >= 0 ? 4 : 5;
	OnFace(F, Dir, OutU, OutV);
	OutU = FMath::Clamp(OutU, -1.0, 1.0);
	OutV = FMath::Clamp(OutV, -1.0, 1.0);
	return F;
}

FVector DmCube::FromFace(int32 Face, double U, double V)
{
	FVector N, Ua, Va;
	FaceAxes(Face, N, Ua, Va);
	return (N + Ua * FMath::Tan(U * kQuarterPi) + Va * FMath::Tan(V * kQuarterPi)).GetSafeNormal();
}

// ---------------------------------------------------------------- sim triangle interpolation

FDmCellInterp FDmCellInterp::At(const dm::Geo& G, const FVector& PlanetDir, int32 Hint)
{
	FDmCellInterp R;
	const dm::Vec3 S = DmCoords::UEToSim(PlanetDir);
	const int32 C = G.cellAt(S.x, S.y, S.z, Hint);
	R.C[0] = R.C[1] = R.C[2] = FMath::Max(C, 0);
	if (C < 0) return R;
	auto P = [&G](int32 I) { const float* Q = &G.pos[size_t(I) * 3]; return FVector(Q[0], Q[1], Q[2]); };
	const FVector D(S.x, S.y, S.z);
	const FVector Pc = P(C);
	const int32 Deg = G.nbrN[size_t(C)];
	for (int32 K = 0; K < Deg; ++K)
	{
		const int32 A = G.nbr[size_t(6 * C + K)], B = G.nbr[size_t(6 * C + (K + 1) % Deg)];
		if (A < 0 || B < 0) continue;
		const FVector Pa = P(A), Pb = P(B);
		// barycentrics of the ray through D in the plane of (C, A, B)
		const FVector E1 = Pa - Pc, E2 = Pb - Pc;
		const FVector Pv = FVector::CrossProduct(D, E2);
		const double Det = FVector::DotProduct(E1, Pv);
		if (FMath::Abs(Det) < 1e-20) continue;
		const double Inv = 1.0 / Det;
		const FVector Sv = -Pc;
		const double U = FVector::DotProduct(Sv, Pv) * Inv;
		const FVector Q = FVector::CrossProduct(Sv, E1);
		const double W = FVector::DotProduct(D, Q) * Inv;
		if (U >= -1e-6 && W >= -1e-6 && U + W <= 1.0 + 1e-6)
		{
			const double Uc = FMath::Clamp(U, 0.0, 1.0), Wc = FMath::Clamp(W, 0.0, 1.0 - Uc);
			R.C[1] = A; R.C[2] = B;
			R.W[0] = float(1.0 - Uc - Wc); R.W[1] = float(Uc); R.W[2] = float(Wc);
			return R;
		}
	}
	return R;
}

// ---------------------------------------------------------------- provider registry

void FDmNatureGround::SetProvider(FDmNatureGroundFn Fn) { FScopeLock L(&GProviderLock); GProvider = MoveTemp(Fn); }
void FDmNatureGround::ClearProvider() { FScopeLock L(&GProviderLock); GProvider = nullptr; }
bool FDmNatureGround::HasProvider() { FScopeLock L(&GProviderLock); return bool(GProvider); }

const TCHAR* FDmNatureGround::SourceName() const
{
	switch (Source)
	{
	case ESource::Provider: return TEXT("provider");
	case ESource::PlanetTerrain: return TEXT("planet-terrain");
	case ESource::PlaceholderMesh: return TEXT("placeholder-mesh");
	default: return TEXT("sim-surface");
	}
}

// ---------------------------------------------------------------- builds

FDmNatureGroundPtr FDmNatureGround::FromSim(const dm::World& W)
{
	TSharedPtr<FDmNatureGround, ESPMode::ThreadSafe> G = MakeShared<FDmNatureGround, ESPMode::ThreadSafe>();
	G->Source = ESource::SimSurface;
	G->Geo = W.geo;
	G->Elev.SetNumUninitialized(W.n);
	for (int32 I = 0; I < W.n; ++I) G->Elev[I] = W.elev[size_t(I)];
	G->SeaRadiusUU = DmCoords::HeightToRadiusUU(W.seaLevel);
	return G;
}

FDmNatureGroundPtr FDmNatureGround::FromProvider(const dm::World& W)
{
	TSharedPtr<FDmNatureGround, ESPMode::ThreadSafe> G = ConstCastSharedPtr<FDmNatureGround>(FromSim(W));
	{
		FScopeLock L(&GProviderLock);
		G->Provider = GProvider;
	}
	if (G->Provider) G->Source = ESource::Provider;
	return G;
}

TSharedPtr<FDmNatureGround, ESPMode::ThreadSafe> FDmNatureGround::PlanetShell(const dm::World& W)
{
	TSharedPtr<FDmNatureGround, ESPMode::ThreadSafe> G = ConstCastSharedPtr<FDmNatureGround>(FromSim(W));
	TSharedPtr<FDmGeoLocator, ESPMode::ThreadSafe> Loc = MakeShared<FDmGeoLocator, ESPMode::ThreadSafe>();
	if (!Loc->Build(W)) return G;   // stays a sim-surface ground
	G->PTerr = MakeShared<FDmPlanetTerrain, ESPMode::ThreadSafe>();
	G->PTerr->Capture(W, Loc);
	return G;
}

FDmNatureGroundPtr FDmNatureGround::FinishPlanet(TSharedPtr<FDmNatureGround, ESPMode::ThreadSafe> Shell)
{
	if (Shell && Shell->PTerr)
	{
		const double T0 = FPlatformTime::Seconds();
		Shell->PTerr->Prepare();
		Shell->Source = ESource::PlanetTerrain;
		Shell->SeaRadiusUU = DmCoords::RadiusUU + Shell->PTerr->SeaM * 100.0;
		Shell->BuildMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	}
	return Shell;
}

double FDmNatureGround::PlanetRadius(const FVector& Dir, FVector* OutNormal) const
{
	// Unreal planet frame -> sim frame (DmCoords: sim (x, y, z) = Unreal (x, z, y))
	const FVector3d Ds = FVector3d(Dir.X, Dir.Z, Dir.Y).GetSafeNormal();
	FDmSurfAttr A;
	double H = PTerr->Height(Ds, 1.0, &A, true);
	// rivers and lakes are water: report the ground as under the sea so nothing stands in them
	const bool bWater = (A.RiverW > 0.f && A.RiverD < A.RiverW + 2.f) || (A.LakeM > -1.0e8f && A.LakeM > H - 0.3);
	if (bWater) H = FMath::Min(H, PTerr->SeaM - 2.0);
	if (OutNormal)
	{
		// finite differences over 3 m along two tangents (no river attributes needed)
		FVector E, N;
		DmCoords::Basis(Dir, E, N);
		const double Da = 3.0 / FDmPlanetTerrain::DatumM;
		const FVector De = (Dir + E * Da).GetSafeNormal(), Dn = (Dir + N * Da).GetSafeNormal();
		const double He = PTerr->Height(FVector3d(De.X, De.Z, De.Y), 1.0, nullptr, false);
		const double Hn = PTerr->Height(FVector3d(Dn.X, Dn.Z, Dn.Y), 1.0, nullptr, false);
		const double H0 = bWater ? PTerr->Height(Ds, 1.0, nullptr, false) : H;
		const FVector Nrm = (Dir * 3.0 - E * (He - H0) - N * (Hn - H0)).GetSafeNormal();
		*OutNormal = Nrm.IsNearlyZero() ? Dir : Nrm;
	}
	return DmCoords::RadiusUU + H * 100.0;
}

FDmNatureGroundPtr FDmNatureGround::FromMesh(const dm::World& W, TArray<FVector3f>&& Verts, TArray<uint32>&& Tris)
{
	return FromMeshShell(ConstCastSharedPtr<FDmNatureGround>(FromSim(W)), MoveTemp(Verts), MoveTemp(Tris));
}

FDmNatureGroundPtr FDmNatureGround::FromMeshShell(TSharedPtr<FDmNatureGround, ESPMode::ThreadSafe> G, TArray<FVector3f>&& Verts, TArray<uint32>&& Tris)
{
	const double T0 = FPlatformTime::Seconds();
	G->Source = ESource::PlaceholderMesh;
	G->V = MoveTemp(Verts);
	G->T = MoveTemp(Tris);
	const int32 NT = G->T.Num() / 3;
	G->Triangles = NT;
	G->G = 512;
	const int32 Gs = G->G;
	const int32 NB = 6 * Gs * Gs;
	G->Start.SetNumZeroed(NB + 1);

	// visit every (triangle, face, bucket) twice: count, then fill
	auto Visit = [&G, Gs](int32 Tri, TFunctionRef<void(int32)> Fn)
	{
		const FVector P[3] = { FVector(G->V[G->T[3 * Tri]]), FVector(G->V[G->T[3 * Tri + 1]]), FVector(G->V[G->T[3 * Tri + 2]]) };
		int32 Faces[3]; int32 NF = 0;
		for (int32 K = 0; K < 3; ++K)
		{
			double U, Vv;
			const int32 F = DmCube::ToFace(P[K].GetSafeNormal(), U, Vv);
			bool bHave = false;
			for (int32 J = 0; J < NF; ++J) bHave |= Faces[J] == F;
			if (!bHave) Faces[NF++] = F;
		}
		for (int32 J = 0; J < NF; ++J)
		{
			double U0 = 1e9, U1 = -1e9, V0 = 1e9, V1 = -1e9;
			bool bOk = true;
			for (int32 K = 0; K < 3 && bOk; ++K)
			{
				double U, Vv;
				bOk = DmCube::OnFace(Faces[J], P[K].GetSafeNormal(), U, Vv);
				U0 = FMath::Min(U0, U); U1 = FMath::Max(U1, U); V0 = FMath::Min(V0, Vv); V1 = FMath::Max(V1, Vv);
			}
			if (!bOk) continue;
			const int32 I0 = FMath::Clamp(int32((U0 + 1.0) * 0.5 * Gs), 0, Gs - 1), I1 = FMath::Clamp(int32((U1 + 1.0) * 0.5 * Gs), 0, Gs - 1);
			const int32 J0 = FMath::Clamp(int32((V0 + 1.0) * 0.5 * Gs), 0, Gs - 1), J1 = FMath::Clamp(int32((V1 + 1.0) * 0.5 * Gs), 0, Gs - 1);
			for (int32 Jv = J0; Jv <= J1; ++Jv)
				for (int32 Iu = I0; Iu <= I1; ++Iu)
					Fn((Faces[J] * Gs + Jv) * Gs + Iu);
		}
	};
	for (int32 Tr = 0; Tr < NT; ++Tr) Visit(Tr, [&G](int32 B) { G->Start[B + 1]++; });
	for (int32 B = 0; B < NB; ++B) G->Start[B + 1] += G->Start[B];
	G->Items.SetNumUninitialized(G->Start[NB]);
	TArray<uint32> Fill; Fill.SetNumZeroed(NB);
	for (int32 Tr = 0; Tr < NT; ++Tr) Visit(Tr, [&G, &Fill, Tr](int32 B) { G->Items[G->Start[B] + Fill[B]++] = uint32(Tr); });
	G->BuildMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	return G;
}

// ---------------------------------------------------------------- queries

double FDmNatureGround::SimRadius(const FVector& Dir, FVector* OutNormal) const
{
	if (!Geo) return DmCoords::RadiusUU;
	const FDmCellInterp I = FDmCellInterp::At(*Geo, Dir);
	const double E = I.W[0] * Elev[I.C[0]] + I.W[1] * Elev[I.C[1]] + I.W[2] * Elev[I.C[2]];
	if (OutNormal)
	{
		auto P = [this](int32 C)
		{
			const float* Q = &Geo->pos[size_t(C) * 3];
			return DmCoords::SimToUE(Q[0], Q[1], Q[2]).GetSafeNormal() * DmCoords::HeightToRadiusUU(Elev[C]);
		};
		FVector N = FVector::CrossProduct(P(I.C[1]) - P(I.C[0]), P(I.C[2]) - P(I.C[0])).GetSafeNormal();
		if (N.IsNearlyZero()) N = Dir;
		if (FVector::DotProduct(N, Dir) < 0) N = -N;
		*OutNormal = N;
	}
	return DmCoords::HeightToRadiusUU(E);
}

bool FDmNatureGround::MeshRadius(const FVector& Dir, double& OutR, FVector* OutNormal) const
{
	double U, Vv;
	const int32 F = DmCube::ToFace(Dir, U, Vv);
	const int32 Iu = FMath::Clamp(int32((U + 1.0) * 0.5 * G), 0, G - 1), Jv = FMath::Clamp(int32((Vv + 1.0) * 0.5 * G), 0, G - 1);
	for (int32 Ring = 0; Ring <= 1; ++Ring)
		for (int32 Dj = -Ring; Dj <= Ring; ++Dj)
			for (int32 Di = -Ring; Di <= Ring; ++Di)
			{
				if (Ring && FMath::Abs(Di) != 1 && FMath::Abs(Dj) != 1) continue;
				const int32 I = Iu + Di, J = Jv + Dj;
				if (I < 0 || J < 0 || I >= G || J >= G) continue;
				const int32 B = (F * G + J) * G + I;
				for (uint32 K = Start[B]; K < Start[B + 1]; ++K)
				{
					const uint32 Tr = Items[K];
					const FVector A(V[T[3 * Tr]]), Bv(V[T[3 * Tr + 1]]), C(V[T[3 * Tr + 2]]);
					double Tt;
					if (RayTri(Dir, A, Bv, C, Tt, 1e-7))
					{
						OutR = Tt;
						if (OutNormal)
						{
							FVector N = FVector::CrossProduct(Bv - A, C - A).GetSafeNormal();
							if (FVector::DotProduct(N, Dir) < 0) N = -N;
							*OutNormal = N;
						}
						return true;
					}
				}
			}
	return false;
}

double FDmNatureGround::Radius(const FVector& Dir, FVector* OutNormal) const
{
	if (Source == ESource::Provider && Provider)
	{
		if (OutNormal) *OutNormal = Dir;
		return Provider(Dir, OutNormal);
	}
	if (Source == ESource::PlanetTerrain && PTerr) return PlanetRadius(Dir, OutNormal);
	if (Source == ESource::PlaceholderMesh && G > 0)
	{
		double R;
		if (MeshRadius(Dir, R, OutNormal)) return R;
		Misses.fetch_add(1);
	}
	return SimRadius(Dir, OutNormal);
}
