// DEMIURGE — Game/Nature/DmNatureFields.cpp : see the header.

#include "Game/Nature/DmNatureFields.h"
#include "Game/Nature/DmNatureGround.h"
#include "Game/Nature/DmNatureMesh.h"
#include "Game/Core/DmCoords.h"

#include "Async/ParallelFor.h"
#include "HAL/PlatformTime.h"
#include "Sim/Core/Geo.h"
#include "Sim/Schema.h"
#include "Sim/State.h"

using DmNature::Smooth;

namespace
{
	inline uint8 Q8(float V) { return uint8(FMath::Clamp(V, 0.f, 1.f) * 255.f + 0.5f); }
	// clearing radius by settlement tier (camp .. metropolis), metres
	constexpr double kClearM[5] = { 160.0, 420.0, 700.0, 1100.0, 1700.0 };
}

TSharedPtr<FDmNatureFields, ESPMode::ThreadSafe> FDmNatureFields::Copy(const dm::World& Wd, uint32 InVersion)
{
	TSharedPtr<FDmNatureFields, ESPMode::ThreadSafe> F = MakeShared<FDmNatureFields, ESPMode::ThreadSafe>();
	F->Version = InVersion;
	F->Geo = Wd.geo;
	F->N = Wd.n;
	F->SeaLevel = Wd.seaLevel;
	F->SeaRadiusUU = DmCoords::HeightToRadiusUU(Wd.seaLevel);
	auto Cp = [&Wd](TArray<float>& Out, const std::vector<float>& In) { Out.SetNumUninitialized(Wd.n); for (int32 I = 0; I < Wd.n; ++I) Out[I] = In[size_t(I)]; };
	Cp(F->CElev, Wd.elev); Cp(F->CVeg, Wd.veg); Cp(F->CTemp, Wd.temp); Cp(F->CMoist, Wd.moist); Cp(F->CIce, Wd.ice);
	Cp(F->CFarm, Wd.farm); Cp(F->CUrban, Wd.urban); Cp(F->CWater, Wd.water); Cp(F->CScorch, Wd.scorch); Cp(F->CFauna, Wd.fauna);
	F->Fauna = F->CFauna; F->Veg = F->CVeg; F->Elev = F->CElev; F->Temp = F->CTemp;
	F->Biome.SetNumUninitialized(Wd.n);
	for (int32 I = 0; I < Wd.n; ++I) F->Biome[I] = Wd.biome[size_t(I)];
	for (const dm::Settlement& S : Wd.settlements)
	{
		if (S.dead || !Wd.valid(S.cell)) continue;
		F->Clearings.Add({ DmCoords::CellDir(Wd, S.cell), kClearM[FMath::Clamp(S.tier, 0, 4)] * (0.85 + 0.3 * FMath::Clamp(S.pop / 20000.0, 0.0, 1.0)) });
	}
	return F;
}

FVector2D FDmNatureFields::UV(const FVector& D)
{
	const double U = FMath::Atan2(D.Y, D.X) / (2.0 * UE_DOUBLE_PI) + 0.5;
	const double V = FMath::Acos(FMath::Clamp(D.Z, -1.0, 1.0)) / UE_DOUBLE_PI;
	return FVector2D(U, V);
}

FVector4f FDmNatureFields::Sample(const TArray<FColor>& Grid, const FVector& Dir) const
{
	const FVector2D Uv = UV(Dir);
	const double X = Uv.X * W - 0.5, Y = Uv.Y * H - 0.5;
	const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
	const float Fx = float(X - X0), Fy = float(Y - Y0);
	auto At = [&Grid](int32 I, int32 J)
	{
		I = ((I % W) + W) % W;
		J = FMath::Clamp(J, 0, H - 1);
		const FColor& C = Grid[J * W + I];
		return FVector4f(C.R, C.G, C.B, C.A) / 255.f;
	};
	const FVector4f A = At(X0, Y0), B = At(X0 + 1, Y0), C = At(X0, Y0 + 1), D = At(X0 + 1, Y0 + 1);
	return (A * (1.f - Fx) + B * Fx) * (1.f - Fy) + (C * (1.f - Fx) + D * Fx) * Fy;
}

float FDmNatureFields::CanopyValue(float R, const FVector& Dir)
{
	// metres on the datum sphere; twin of NatureCanopy() in Scripts/nature.py
	const float Px = float(Dir.X * 100000.0), Py = float(Dir.Y * 100000.0), Pz = float(Dir.Z * 100000.0);
	const float N = 0.5f * DmNature::Fbm3(Px / 520.f, Py / 520.f, Pz / 520.f, 4) + 0.12f * DmNature::Fbm3(Px / 90.f + 3.1f, Py / 90.f + 3.1f, Pz / 90.f + 3.1f, 2);
	return Smooth(0.42f, 0.62f, R + 0.6f * N);
}

void FDmNatureFields::NearClearings(const FVector& Dir, double AngleRad, TArray<int32>& Out) const
{
	Out.Reset();
	for (int32 I = 0; I < Clearings.Num(); ++I)
	{
		const double A = FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir, Clearings[I].Dir), -1.0, 1.0));
		if (A < AngleRad + Clearings[I].RadiusM * 1.4 / (DmCoords::RadiusKm * 1000.0)) Out.Add(I);
	}
}

float FDmNatureFields::ClearingAt(const FVector& Dir, const TArray<int32>& Near) const
{
	float K = 1.f;
	for (int32 I : Near)
	{
		const FClearing& C = Clearings[I];
		const double DistM = FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir, C.Dir), -1.0, 1.0)) * DmCoords::RadiusKm * 1000.0;
		// ragged edge: +-30 % by noise
		const float Wob = 1.f + 0.3f * DmNature::Fbm3(float(Dir.X * 100000.0 / 300.0) + 11.f, float(Dir.Y * 100000.0 / 300.0), float(Dir.Z * 100000.0 / 300.0), 3);
		const float R = float(C.RadiusM) * Wob;
		K *= Smooth(R * 0.8f, R * 1.25f, float(DistM));
	}
	return K;
}

void FDmNatureFields::Bake()
{
	const double T0 = FPlatformTime::Seconds();
	if (!Geo || N <= 0) return;
	// one neighbour-smoothing pass on the climate fields (hex facets)
	auto SmoothPass = [this](TArray<float>& F)
	{
		TArray<float> Out = F;
		for (int32 C = 0; C < N; ++C)
		{
			const int32 Deg = Geo->nbrN[size_t(C)];
			if (!Deg) continue;
			double S = 0;
			for (int32 K = 0; K < Deg; ++K) S += F[Geo->nbr[size_t(6 * C + K)]];
			Out[C] = 0.5f * F[C] + 0.5f * float(S / Deg);
		}
		F = MoveTemp(Out);
	};
	SmoothPass(CVeg); SmoothPass(CTemp); SmoothPass(CMoist);

	Canopy.SetNumUninitialized(W * H);
	Climate.SetNumUninitialized(W * H);
	Land.SetNumUninitialized(W * H);
	const float Sea = float(SeaLevel);
	ParallelFor(H, [this, Sea](int32 J)
	{
		int32 Hint = -1;
		for (int32 I = 0; I < W; ++I)
		{
			const double U = (I + 0.5) / W, V = (J + 0.5) / H;
			const double Lon = (U - 0.5) * 2.0 * UE_DOUBLE_PI, Co = V * UE_DOUBLE_PI;
			const FVector D(FMath::Sin(Co) * FMath::Cos(Lon), FMath::Sin(Co) * FMath::Sin(Lon), FMath::Cos(Co));
			// domain warp (~1 km) so biome borders do not follow the sim's hexagons
			const float Wx = float(D.X * 60.0), Wy = float(D.Y * 60.0), Wz = float(D.Z * 60.0);
			const FVector Warp(DmNature::Fbm3(Wx, Wy, Wz, 3), DmNature::Fbm3(Wx + 5.2f, Wy + 1.3f, Wz, 3), DmNature::Fbm3(Wx, Wy + 9.7f, Wz + 2.8f, 3));
			const FVector Dw = (D + Warp * 0.011).GetSafeNormal();
			const FDmCellInterp In = FDmCellInterp::At(*Geo, Dw, Hint);
			Hint = In.C[0];
			auto L = [&In](const TArray<float>& F) { return In.W[0] * F[In.C[0]] + In.W[1] * F[In.C[1]] + In.W[2] * F[In.C[2]]; };
			const float E = L(CElev), Vg = L(CVeg), T = L(CTemp), Mo = L(CMoist), Ice = L(CIce);
			const float Farm = L(CFarm), Urb = L(CUrban), Wat = L(CWater), Sc = L(CScorch), Fa = L(CFauna);
			const float LandK = Smooth(Sea - 0.02f, Sea + 0.05f, E);
			const float Lake = Smooth(0.05f, 0.4f, Wat);
			const float R = Vg * LandK * (1.f - Ice) * Smooth(-7.f, -3.f, T) * (1.f - Smooth(0.15f, 0.6f, Farm)) * (1.f - Smooth(0.05f, 0.35f, Urb))
				* (1.f - Lake) * (1.f - 0.92f * Sc);
			const float Wc = Smooth(13.f, 5.f, T), Wb = Smooth(3.f, 10.f, T) * Smooth(0.2f, 0.4f, Mo);
			const float Con = Wc / FMath::Max(Wc + Wb, 1e-3f);
			const float Wet = Smooth(20.f, 25.f, T) * Smooth(0.55f, 0.75f, Mo);
			const float Dry = Smooth(17.f, 22.f, T) * Smooth(0.5f, 0.3f, Mo);
			const float Snow = Smooth(0.f, -8.f, T);
			const int32 K = J * W + I;
			Canopy[K] = FColor(Q8(R), Q8(Con), Q8(Wet), Q8(Dry));
			Climate[K] = FColor(Q8((T + 40.f) / 80.f), Q8(Mo), Q8(Snow), Q8(Sc));
			Land[K] = FColor(Q8(Farm), Q8(Urb), Q8(Lake), Q8(Fa));
		}
	}, EParallelForFlags::BackgroundPriority);
	BuildMs = (FPlatformTime::Seconds() - T0) * 1000.0;
}
