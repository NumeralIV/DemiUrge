// DEMIURGE — Game/Sky/DmSkyStorms.cpp : see the header. Owner: C2 sky.

#include "Game/Sky/DmSkyStorms.h"
#include "Game/Sky/DmSkyWeather.h"
#include "Game/Core/DmCoords.h"
#include "Sim/State.h"

namespace
{
	constexpr double Deg = UE_DOUBLE_PI / 180.0;
	inline double Smooth(double E0, double E1, double X) { const double T = FMath::Clamp((X - E0) / (E1 - E0), 0.0, 1.0); return T * T * (3.0 - 2.0 * T); }
	inline double WrapLon(double L) { L = FMath::Fmod(L + 180.0, 360.0); if (L < 0) L += 360.0; return L - 180.0; }

	FVector CellDirOf(const dm::World* W, int32 Cell)
	{
		return W ? DmCoords::CellDir(*W, Cell) : FVector::ZeroVector;
	}

	void SetLatLon(FDmCyclone& C, double Lat, double Lon)
	{
		C.LatDeg = FMath::Clamp(Lat, -85.0, 85.0);
		C.LonDeg = WrapLon(Lon);
		C.Dir = DmCoords::LatLonToDir(C.LatDeg, C.LonDeg);
	}

	void SetDir(FDmCyclone& C, const FVector& D)
	{
		C.Dir = D.GetSafeNormal();
		DmCoords::DirToLatLon(C.Dir, C.LatDeg, C.LonDeg);
	}

	/** Planet-frame (radians) lon / lat of the sim lat / lon in degrees, as the weather grids use them. */
	inline void GridLonLat(double LatDeg, double LonDeg, double& Lon, double& Lat) { Lon = LonDeg * Deg; Lat = LatDeg * Deg; }
}

double FDmFlash::Intensity() const
{
	if (Gain <= 0.0 || T > 1.2) return 0.0;
	// a stroke: bright return stroke, then one or two re-strokes
	double I = FMath::Exp(-T / 0.05);
	if (T > 0.13) I += 0.65 * FMath::Exp(-(T - 0.13) / 0.04);
	if (T > 0.34) I += 0.4 * FMath::Exp(-(T - 0.34) / 0.06);
	return Gain * I;
}

void FDmSkyStorms::Spawn(FDmCyclone& C, bool bTropical, double Hemi, const FDmSkyWeather& Weather, double SunDeclDeg, bool bStagger)
{
	C = FDmCyclone();
	C.bClimate = true;
	C.Id = NextId++;
	C.Sign = Hemi;
	C.Phase = Rng.FRandRange(0.0, 2.0 * UE_DOUBLE_PI);
	if (bTropical)
	{
		C.Type = EDmCycloneType::Tropical;
		// form over warm, open ocean at 10-20 deg: try candidates, keep the most open one
		double BestScore = 1e9, BestLat = 14.0 * Hemi, BestLon = 0.0;
		for (int32 K = 0; K < 24; ++K)
		{
			const double Lat = Hemi * Rng.FRandRange(10.0, 19.0), Lon = Rng.FRandRange(-180.0, 180.0);
			double GLon, GLat; GridLonLat(Lat, Lon, GLon, GLat);
			const double Land = Weather.LandAround(GLon, GLat, 7.0);
			// room to the west (the track heads that way: increasing longitude)
			double LonW, LatW; GridLonLat(Lat + Hemi * 4.0, Lon + 15.0, LonW, LatW);
			const double Score = Land * 10.0 + Weather.LandAround(LonW, LatW, 6.0) * 4.0 + Rng.FRand() * 0.3;
			if (Score < BestScore) { BestScore = Score; BestLat = Lat; BestLon = Lon; }
		}
		SetLatLon(C, BestLat, BestLon);
		// larger than Earth's relative to the planet, so the storm reads from orbit (the planet is 100 km)
		C.RadiusRad = Rng.FRandRange(10.0, 12.5) * Deg;
		// hurricane season: the summer hemisphere's cyclone is the strong one
		const bool bSummer = (Hemi > 0) == (SunDeclDeg >= 0.0);
		C.Peak = bSummer ? 1.0 : 0.72;
		if (!bSummer) C.RadiusRad *= 0.78;
		C.Life = Rng.FRandRange(260.0, 380.0);
	}
	else
	{
		C.Type = EDmCycloneType::Extratropical;
		SetLatLon(C, Hemi * Rng.FRandRange(44.0, 58.0), Rng.FRandRange(-180.0, 180.0));
		C.RadiusRad = Rng.FRandRange(11.0, 15.0) * Deg;
		C.Peak = Rng.FRandRange(0.8, 1.0);
		C.Life = Rng.FRandRange(200.0, 320.0);
	}
	// staggered start: anywhere in the mature part of the life cycle
	C.Age = bStagger ? Rng.FRandRange(0.25, 0.6) * C.Life : 0.0;
}

void FDmSkyStorms::Reset(uint32 Seed, const FDmSkyWeather& Weather, double SunDeclDeg)
{
	Rng.Initialize(int32(Seed * 7919u + 13u));
	Systems.Reset();
	NextId = 1;
	for (int32 Hemi = 0; Hemi < 2; ++Hemi)
	{
		const double H = Hemi == 0 ? 1.0 : -1.0;
		FDmCyclone& T = Systems.AddDefaulted_GetRef();
		Spawn(T, true, H, Weather, SunDeclDeg, true);
		// three lows per hemisphere, spread around the belt (a fresh spawn later goes anywhere)
		const double Lon0 = Rng.FRandRange(-180.0, 180.0);
		for (int32 K = 0; K < 3; ++K)
		{
			FDmCyclone& E = Systems.AddDefaulted_GetRef();
			Spawn(E, false, H, Weather, SunDeclDeg, true);
			SetLatLon(E, E.LatDeg, Lon0 + 120.0 * K + Rng.FRandRange(-25.0, 25.0));
		}
	}
	for (FDmCyclone& C : Systems)
	{
		const double A = C.Age / C.Life;
		C.Strength = C.Peak * Smooth(0.0, 0.18, A) * (1.0 - Smooth(0.75, 1.0, A));
	}
	if (bPinned) PinTropical(PinLat, PinLon, PinRad);
	for (FDmFlash& F : Flashes) F = FDmFlash();
}

void FDmSkyStorms::PinTropical(double LatDeg, double LonDeg, double RadiusDeg)
{
	bPinned = true; PinLat = LatDeg; PinLon = LonDeg; PinRad = RadiusDeg;
	for (FDmCyclone& C : Systems)
		if (C.bClimate && C.Type == EDmCycloneType::Tropical && C.Sign == (LatDeg >= 0 ? 1.0 : -1.0))
		{
			SetLatLon(C, LatDeg, LonDeg);
			C.RadiusRad = RadiusDeg * Deg;
			C.Peak = 1.0;
			C.Age = 0.45 * C.Life;
			C.Strength = 1.0;
			return;
		}
}

void FDmSkyStorms::Step(double SkyDt, double RealDt, double SkyRate, const dm::World* W, const FDmSkyWeather& Weather, double SunDeclDeg, double DayLength)
{
	// planet-day based speeds: degrees per planet day -> per sky second
	const double PerDay = 1.0 / FMath::Max(1.0, DayLength);
	for (int32 I = 0; I < Systems.Num(); ++I)
	{
		FDmCyclone& C = Systems[I];
		if (!C.bClimate) continue;
		const bool bHeld = bPinned && C.Type == EDmCycloneType::Tropical && C.Sign == (PinLat >= 0 ? 1.0 : -1.0);
		if (!bHeld) C.Age += SkyDt;
		if (C.Age >= C.Life) { const double H = C.Sign; Spawn(C, C.Type == EDmCycloneType::Tropical, H, Weather, SunDeclDeg, false); continue; }
		// a climatological hurricane gives way to the sim's own storm (never two side by side): it fades out in
		// real time and forms again somewhere else
		bool bYield = false;
		if (C.Type == EDmCycloneType::Tropical && !bHeld && W)
			for (const dm::Disaster& D : W->disasters)
				if (D.type == dm::DisasterType::Storm && W->valid(D.cell) && FVector::DotProduct(CellDirOf(W, D.cell), C.Dir) > FMath::Cos(60.0 * Deg)) { bYield = true; break; }
		if (bYield && C.Strength < 0.02) { const double H = C.Sign; Spawn(C, true, H, Weather, SunDeclDeg, true); C.Strength = 0.0; continue; }
		double GLon, GLat; GridLonLat(C.LatDeg, C.LonDeg, GLon, GLat);
		double Decay = 1.0;
		if (!bHeld && SkyDt > 0.0)
		{
			double DLat = 0.0, DLon = 0.0;   // degrees per planet day; +lon = westward
			if (C.Type == EDmCycloneType::Tropical)
			{
				const double A = FMath::Abs(C.LatDeg);
				const double Recurve = Smooth(22.0, 30.0, A);
				DLon = FMath::Lerp(5.0, -7.0, Recurve);       // trades push it west, then the westerlies east
				DLat = FMath::Lerp(1.6, 3.5, Recurve) * C.Sign;
			}
			else
			{
				DLon = -11.0;                                 // westerlies (eastward)
				DLat = 0.8 * C.Sign;
			}
			SetLatLon(C, C.LatDeg + DLat * SkyDt * PerDay, C.LonDeg + DLon * SkyDt * PerDay / FMath::Max(0.3, FMath::Cos(C.LatDeg * Deg)));
			C.Phase -= C.Sign * 0.9 * SkyDt * PerDay * 2.0 * UE_DOUBLE_PI / 4.0;   // the bands turn with the circulation
		}
		if (C.Type == EDmCycloneType::Tropical)
		{
			// tropical cyclones need warm open water: they fill over land and in cold seas
			const double Land = Weather.LandAround(GLon, GLat, 3.0);
			Decay = (1.0 - 0.75 * Smooth(0.15, 0.6, Land)) * (1.0 - 0.6 * Smooth(32.0, 42.0, FMath::Abs(C.LatDeg)));
			if (!bHeld && Decay < 0.45) C.Age += SkyDt * 2.0;   // dies faster
		}
		const double A = C.Age / C.Life;
		const double Target = bHeld ? 1.0 : (bYield ? 0.0 : C.Peak * Smooth(0.0, 0.18, A) * (1.0 - Smooth(0.75, 1.0, A)) * Decay);
		C.Strength = FMath::FInterpTo(C.Strength, Target, RealDt, 1.5);
		if (SkyDt <= 0.0 && RealDt <= 0.0) C.Strength = Target;
	}

	// ---- the sim's storms
	TArray<int32, TInlineAllocator<8>> Live;
	if (W)
	{
		for (const dm::Disaster& D : W->disasters)
		{
			if (D.type != dm::DisasterType::Storm || !W->valid(D.cell)) continue;
			Live.Add(D.id);
			FDmCyclone* C = Systems.FindByPredicate([&D](const FDmCyclone& X) { return X.SimId == D.id; });
			const FVector Dir = CellDirOf(W, D.cell);
			if (!C)
			{
				C = &Systems.AddDefaulted_GetRef();
				C->SimId = D.id;
				C->Id = NextId++;
				C->Type = EDmCycloneType::Tropical;
				C->Phase = Rng.FRandRange(0.0, 2.0 * UE_DOUBLE_PI);
				SetDir(*C, Dir);
				C->Strength = 0.0;
			}
			C->Target = Dir;
			C->bEnding = false;
			C->RadiusRad = FMath::Clamp(D.ang * 1.25, 0.08, 0.22);
			C->Peak = FMath::Clamp(0.55 + 0.6 * D.power, 0.5, 1.0);
		}
	}
	for (int32 I = Systems.Num() - 1; I >= 0; --I)
	{
		FDmCyclone& C = Systems[I];
		if (C.bClimate) continue;
		if (C.Type == EDmCycloneType::Blob)
		{
			if (C.Hold > 0.0) { C.Hold -= RealDt; C.Strength = FMath::FInterpTo(C.Strength, C.Peak, RealDt, 2.0); }
			else C.Strength -= RealDt / 12.0;
			if (C.Strength <= 0.0 && C.Hold <= 0.0) Systems.RemoveAt(I);
			continue;
		}
		if (!Live.Contains(C.SimId)) C.bEnding = true;
		if (C.bEnding)
		{
			C.Strength -= RealDt / 4.0;
			if (C.Strength <= 0.0) { Systems.RemoveAt(I); continue; }
		}
		else
		{
			C.Strength = FMath::FInterpTo(C.Strength, C.Peak, RealDt, 0.8);
			if (C.Strength < 0.05) C.Strength = FMath::Min(C.Peak, C.Strength + RealDt * 0.2);
		}
		// follow the storm cell (it hops cell by cell): exponential smoothing in real time
		const FVector D = FMath::Lerp(C.Dir, C.Target, 1.0 - FMath::Exp(-RealDt * 2.5)).GetSafeNormal();
		SetDir(C, D);
		C.Sign = C.LatDeg >= 0.0 ? 1.0 : -1.0;
		C.Phase -= C.Sign * SkyDt * 0.05;
	}
	for (FDmFlash& F : Flashes) F.T += RealDt;
	AmbientStrikes(RealDt, SkyRate);
}

void FDmSkyStorms::AddRain(int32 Cell, double AngRad, const dm::World* W)
{
	const FVector Dir = CellDirOf(W, Cell);
	if (Dir.IsNearlyZero()) return;
	const double Rad = FMath::Clamp(AngRad > 0.0 ? AngRad * 1.4 : 0.06, 0.03, 0.25);
	for (FDmCyclone& C : Systems)
		if (C.Type == EDmCycloneType::Blob && FVector::DotProduct(C.Dir, Dir) > FMath::Cos(C.RadiusRad * 0.6))
		{
			C.Hold = 6.0;
			C.RadiusRad = FMath::Max(C.RadiusRad, Rad);
			return;
		}
	FDmCyclone& C = Systems.AddDefaulted_GetRef();
	C.Id = NextId++;
	C.Type = EDmCycloneType::Blob;
	SetDir(C, Dir);
	C.Target = Dir;
	C.RadiusRad = Rad;
	C.Sign = C.LatDeg >= 0.0 ? 1.0 : -1.0;
	C.Peak = 1.0;
	C.Strength = 0.0;
	C.Hold = 6.0;
}

void FDmSkyStorms::Strike(const FVector& PlanetDir, double Gain)
{
	if (PlanetDir.IsNearlyZero()) return;
	// reuse the oldest slot
	int32 Best = 0;
	for (int32 I = 1; I < FlashSlots; ++I) if (Flashes[I].T > Flashes[Best].T) Best = I;
	Flashes[Best].Dir = PlanetDir.GetSafeNormal();
	Flashes[Best].T = 0.0;
	Flashes[Best].Gain = Gain;
}

void FDmSkyStorms::AmbientStrikes(double RealDt, double SkyRate)
{
	if (RealDt <= 0.0 || SkyRate <= 0.0) return;
	for (const FDmCyclone& C : Systems)
	{
		if (C.Strength < 0.5) continue;
		double Rate = 0.0;   // strokes per real second
		if (C.Type == EDmCycloneType::Tropical) Rate = (C.bClimate ? 1.2 : 2.5) * C.Strength;
		else if (C.Type == EDmCycloneType::Blob) Rate = 1.5 * C.Strength;
		else Rate = 0.25 * C.Strength;
		Rate *= FMath::Min(SkyRate, 2.0);
		if (Rng.FRand() >= 1.0 - FMath::Exp(-Rate * RealDt)) continue;
		// somewhere in the core / rain bands
		FVector E, N;
		DmCoords::Basis(C.Dir, E, N);
		const double R = C.RadiusRad * (C.Type == EDmCycloneType::Extratropical ? 1.2 : 0.65) * FMath::Sqrt(Rng.FRand());
		const double Th = Rng.FRandRange(0.0, 2.0 * UE_DOUBLE_PI);
		const FVector D = (C.Dir + (E * FMath::Cos(Th) + N * FMath::Sin(Th)) * FMath::Tan(R)).GetSafeNormal();
		Strike(D, Rng.FRandRange(0.4, 1.0));
	}
}

void FDmSkyStorms::Snapshot(TArray<FDmSystemSnap>& Out) const
{
	Out.Reset();
	for (const FDmCyclone& C : Systems)
	{
		if (C.Strength <= 0.002) continue;
		FDmSystemSnap& S = Out.AddDefaulted_GetRef();
		S.Dir = C.Dir; S.RadiusRad = C.RadiusRad; S.Sign = C.Sign; S.Type = C.Type;
		S.Phase = FMath::Fmod(C.Phase, 2.0 * UE_DOUBLE_PI); S.Strength = FMath::Clamp(C.Strength, 0.0, 1.0); S.Id = C.Id;
	}
}

uint32 FDmSkyStorms::Fingerprint() const
{
	// quantised positions / sizes / strengths: a rebuild is worth it only when one of them moved visibly
	uint32 H = 2166136261u;
	auto Mix = [&H](int64 V) { H = (H ^ uint32(V)) * 16777619u; H = (H ^ uint32(V >> 32)) * 16777619u; };
	for (const FDmCyclone& C : Systems)
	{
		if (C.Strength <= 0.002) continue;
		Mix(C.Id);
		Mix(FMath::RoundToInt64(C.Dir.X * 2000.0)); Mix(FMath::RoundToInt64(C.Dir.Y * 2000.0)); Mix(FMath::RoundToInt64(C.Dir.Z * 2000.0));
		Mix(FMath::RoundToInt64(C.RadiusRad * 2000.0)); Mix(FMath::RoundToInt64(C.Strength * 50.0)); Mix(FMath::RoundToInt64(C.Phase * 20.0));
	}
	return H;
}

FString FDmSkyStorms::Describe() const
{
	FString S;
	for (const FDmCyclone& C : Systems)
	{
		const TCHAR* T = C.Type == EDmCycloneType::Tropical ? TEXT("tropical") : (C.Type == EDmCycloneType::Blob ? TEXT("rain") : TEXT("low"));
		S += FString::Printf(TEXT("%s%s(lat %.1f lon %.1f r %.1f s %.2f%s) "), T, C.bClimate ? TEXT("") : TEXT("*"), C.LatDeg, C.LonDeg, C.RadiusRad / Deg, C.Strength,
			C.SimId >= 0 ? *FString::Printf(TEXT(" sim#%d"), C.SimId) : TEXT(""));
	}
	return S;
}
