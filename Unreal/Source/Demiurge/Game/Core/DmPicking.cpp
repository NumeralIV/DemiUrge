// DEMIURGE — Game/Core/DmPicking.cpp : see DmPicking.h. Owner: C0 bridge.

#include "Game/Core/DmPicking.h"
#include "Game/Core/DmCoords.h"
#include "Sim/State.h"

void FDmPicker::Rebuild(const dm::World& W)
{
	bValid = false;
	if (!W.geo || W.n <= 0) return;
	double Lo = 1e30, Hi = -1e30;
	for (int32 C = 0; C < W.n; ++C)
	{
		const double H = DmCoords::SurfaceHeight(W, C);
		Lo = FMath::Min(Lo, H);
		Hi = FMath::Max(Hi, H);
	}
	MinR = DmCoords::HeightToRadiusUU(Lo) - 10.0;
	MaxR = DmCoords::HeightToRadiusUU(Hi) + 10.0;
	// steepest edge of the interpolated surface (uu per uu); skinny triangles can be up to ~2x steeper
	double SMax = 0;
	for (int32 C = 0; C < W.n; ++C)
	{
		const double HC = DmCoords::SurfaceHeight(W, C);
		for (int32 K = 0; K < W.geo->nbrN[size_t(C)]; ++K)
		{
			const int32 M = W.geo->nbr[size_t(6 * C + K)];
			const double Run = FMath::Max(W.geo->angle(C, M) * DmCoords::RadiusUU, 1.0);
			SMax = FMath::Max(SMax, FMath::Abs(DmCoords::SurfaceHeight(W, M) - HC) * DmCoords::UUPerElev / Run);
		}
	}
	Lipschitz = 1.0 + 2.0 * SMax;
	bValid = true;
}

double FDmPicker::SurfaceRadiusAt(const dm::World& W, const FVector& P, int32& InOutCell) const
{
	// nearest cell, then the height interpolated over the sim triangle (cell, nbr k, nbr k+1) that
	// contains the direction (gnomonic barycentrics: triple products), like a mesh with one vertex
	// per cell; sea cells contribute sea level
	const dm::Vec3 S = DmCoords::UEToSim(P);
	const int32 C = W.geo->cellAt(S.x, S.y, S.z, InOutCell);
	InOutCell = C;
	const double HC = DmCoords::SurfaceHeight(W, C);
	const float* PC = &W.geo->pos[size_t(3 * C)];
	const int32 Deg = W.geo->nbrN[size_t(C)];
	auto Triple = [](const double* Dv, const float* Av, const float* Bv)
	{
		const double X = double(Av[1]) * Bv[2] - double(Av[2]) * Bv[1];
		const double Y = double(Av[2]) * Bv[0] - double(Av[0]) * Bv[2];
		const double Z = double(Av[0]) * Bv[1] - double(Av[1]) * Bv[0];
		return Dv[0] * X + Dv[1] * Y + Dv[2] * Z;
	};
	const double D[3] = { S.x, S.y, S.z };
	for (int32 K = 0; K < Deg; ++K)
	{
		const int32 A = W.geo->nbr[size_t(6 * C + K)];
		const int32 B = W.geo->nbr[size_t(6 * C + (K + 1) % Deg)];
		const float* PA = &W.geo->pos[size_t(3 * A)];
		const float* PB = &W.geo->pos[size_t(3 * B)];
		const double WC = Triple(D, PA, PB), WA = Triple(D, PB, PC), WB = Triple(D, PC, PA);
		const bool bPos = WC >= 0 && WA >= 0 && WB >= 0, bNeg = WC <= 0 && WA <= 0 && WB <= 0;
		if (!bPos && !bNeg) continue;
		const double Sum = WC + WA + WB;
		if (FMath::Abs(Sum) < 1e-300) break;
		const double H = (WC * HC + WA * DmCoords::SurfaceHeight(W, A) + WB * DmCoords::SurfaceHeight(W, B)) / Sum;
		return DmCoords::HeightToRadiusUU(H);
	}
	return DmCoords::HeightToRadiusUU(HC);
}

bool FDmPicker::PickPlanetRay(const dm::World& W, const FVector& Origin, const FVector& InDir, FDmPick& Out) const
{
	Out = FDmPick();
	if (!bValid || !W.geo) return false;
	const FVector D = InDir.GetSafeNormal();
	if (D.IsNearlyZero()) return false;

	// entry / exit of the outer shell sphere
	const double B = FVector::DotProduct(Origin, D);
	const double C = Origin.SizeSquared() - MaxR * MaxR;
	const double Disc = B * B - C;
	if (Disc < 0) return false;
	const double Sq = FMath::Sqrt(Disc);
	double T0 = -B - Sq, T1 = -B + Sq;
	if (T1 < 0) return false;
	T0 = FMath::Max(T0, 0.0);
	// the inner sphere bounds the march (every surface is above it)
	const double Ci = Origin.SizeSquared() - MinR * MinR;
	const double DiscI = B * B - Ci;
	if (DiscI >= 0)
	{
		const double Ti = -B - FMath::Sqrt(DiscI);
		if (Ti > T0) T1 = FMath::Min(T1, Ti + 1.0);
	}

	const double CellUU = W.geo->step * DmCoords::RadiusUU;
	const double MaxStep = CellUU * 0.5, MinStep = FMath::Max(CellUU / 512.0, 1.0);
	int32 Cell = -1;
	auto Above = [&](double T, int32& InOutCell) -> double
	{
		const FVector P = Origin + D * T;
		return P.Size() - SurfaceRadiusAt(W, P, InOutCell);
	};
	double TPrev = T0;
	double FPrev = Above(T0, Cell);
	if (FPrev <= 0)
	{
		// origin already below the surface (camera clipping into a mountain): report the cell under it
		Out.Cell = Cell;
		Out.Distance = T0;
	}
	else
	{
		const int32 MaxSteps = 20000;
		bool bHit = false;
		double THit = 0;
		for (int32 I = 1; I <= MaxSteps; ++I)
		{
			// f(t) = |p| - surface(p) changes at most Lipschitz per uu along the ray: a step of f / L cannot cross the surface
			const double T = FMath::Min(TPrev + FMath::Clamp(FPrev / Lipschitz, MinStep, MaxStep), T1);
			int32 CellT = Cell;
			const double F = Above(T, CellT);
			if (F <= 0)
			{
				double A = TPrev, Bt = T;
				for (int32 K = 0; K < 24; ++K)
				{
					const double M = 0.5 * (A + Bt);
					int32 CellM = CellT;
					if (Above(M, CellM) <= 0) Bt = M; else A = M;
				}
				THit = Bt;
				bHit = true;
				break;
			}
			Cell = CellT;
			TPrev = T;
			FPrev = F;
			if (T >= T1) break;
		}
		if (!bHit) return false;
		int32 CellH = Cell;
		Above(THit, CellH);
		Out.Cell = CellH;
		Out.Distance = THit;
	}
	const FVector Hit = Origin + D * Out.Distance;
	Out.PlanetDir = Hit.GetSafeNormal();
	Out.WorldPos = Hit;   // planet frame; the subsystem converts to world space
	Out.bSea = Out.Cell >= 0 && dm::state::isSea(W, Out.Cell);
	DmCoords::DirToLatLon(Out.PlanetDir, Out.LatDeg, Out.LonDeg);
	return Out.Cell >= 0;
}
