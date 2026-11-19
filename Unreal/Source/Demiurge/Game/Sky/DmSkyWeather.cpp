// DEMIURGE — Game/Sky/DmSkyWeather.cpp : see the header. Owner: C2 sky.
// Regimes, constants and pitfalls follow UnrealLab/CloudLab/Source/PlanetLab/CloudWeather.cpp (the lab
// reference the owner approved); the comments there carry the reasoning.

#include "Game/Sky/DmSkyWeather.h"
#include "Game/Sky/DmSkyStorms.h"
#include "Game/Core/DmCoords.h"
#include "Sim/State.h"

#include "Async/ParallelFor.h"
#include "Engine/Texture2D.h"
#include "Engine/VolumeTexture.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/FileHelper.h"
#include "TextureResource.h"

namespace
{
	constexpr double Pi = UE_DOUBLE_PI;
	constexpr double Deg = UE_DOUBLE_PI / 180.0;

	inline double Sq(double X) { return X * X; }
	inline double Sat(double X) { return FMath::Clamp(X, 0.0, 1.0); }
	inline double Smooth(double E0, double E1, double X) { const double T = Sat((X - E0) / (E1 - E0)); return T * T * (3.0 - 2.0 * T); }

	inline FVector DirOf(double Lon, double Lat)
	{
		const double C = FMath::Cos(Lat);
		return FVector(C * FMath::Cos(Lon), C * FMath::Sin(Lon), FMath::Sin(Lat));
	}

	inline uint32 Hash3(int32 X, int32 Y, int32 Z, uint32 Seed)
	{
		uint32 H = uint32(X) * 0x8da6b343u ^ uint32(Y) * 0xd8163841u ^ uint32(Z) * 0xcb1ab31fu ^ Seed * 0x9e3779b9u;
		H ^= H >> 13; H *= 0x85ebca6bu; H ^= H >> 16; H *= 0xc2b2ae35u; H ^= H >> 16;
		return H;
	}
	inline double HashF(uint32 H) { return double(H & 0xffffffu) / double(0x1000000); }
	inline uint8 B8(double V) { return uint8(FMath::RoundToInt(Sat(V) * 255.0)); }
	inline uint8 Enc(double V) { return B8(0.5 + 0.5 * V); }    // signed noise -> byte (0.5 = 0)

	/** Analytic stand-in climate when there is no world: wet tropics and mid-latitudes, dry subtropics. */
	void ZonalClimate(double LatDeg, float& Moist, float& Temp)
	{
		const double A = FMath::Abs(LatDeg);
		Moist = float(Sat(0.25 + 0.6 * FMath::Exp(-Sq(LatDeg / 12.0)) - 0.25 * FMath::Exp(-Sq((A - 26.0) / 9.0)) + 0.25 * FMath::Exp(-Sq((A - 52.0) / 12.0))));
		Temp = float(28.0 - 50.0 * Sq(A / 90.0));
	}
}

double DmSkyNoise::Fbm(const FVector& P, int32 Oct, double Lac, double Gain)
{
	double Sum = 0.0, Amp = 1.0, Norm = 0.0;
	FVector Q = P;
	for (int32 O = 0; O < Oct; ++O)
	{
		Sum += Amp * FMath::PerlinNoise3D(Q);
		Norm += Amp;
		Amp *= Gain;
		Q = Q * Lac + FVector(17.13, -3.71, 9.2);
	}
	return Sum / Norm;
}

// ---------------------------------------------------------------------------------------------- snapshot

void FDmClimateSnapshot::FromWorld(const dm::World* W)
{
	bValid = false;
	N = 0;
	if (!W || !W->geo || W->n <= 0) return;
	N = W->n;
	Dir.SetNumUninitialized(N);
	Moist.SetNumUninitialized(N);
	Temp.SetNumUninitialized(N);
	Elev.SetNumUninitialized(N);
	SeaLevel = float(W->seaLevel);
	int32 Sea = 0;
	for (int32 C = 0; C < N; ++C)
	{
		const float* Ps = &W->geo->pos[size_t(C) * 3];
		Dir[C] = FVector3f(DmCoords::SimToUE(Ps[0], Ps[1], Ps[2]).GetSafeNormal());
		const float M = W->moist[size_t(C)], T = W->temp[size_t(C)], E = W->elev[size_t(C)];
		Moist[C] = FMath::IsFinite(M) ? M : 0.f;
		Temp[C] = FMath::IsFinite(T) ? T : 0.f;
		Elev[C] = FMath::IsFinite(E) ? E : 0.f;
		if (Elev[C] < SeaLevel) ++Sea;
	}
	SeaFraction = float(Sea) / float(FMath::Max(1, N));
	bValid = true;
}

// ---------------------------------------------------------------------------------------------- grids

void FDmSkyWeather::BuildGrids(const FDmClimateSnapshot& D)
{
	GW = 720; GH = 360;   // 0.5 degree
	const int32 NG = GW * GH;
	GMoist.SetNumUninitialized(NG); GTemp.SetNumUninitialized(NG); GLand.SetNumUninitialized(NG); GElev.SetNumUninitialized(NG);
	if (!D.bValid)
	{
		for (int32 Y = 0; Y < GH; ++Y)
		{
			float M, T;
			ZonalClimate(90.0 - (Y + 0.5) / GH * 180.0, M, T);
			for (int32 X = 0; X < GW; ++X) { const int32 I = Y * GW + X; GMoist[I] = M; GTemp[I] = T; GLand[I] = 0.f; GElev[I] = -4.f; }
		}
	}
	else
	{
		TArray<double> WSum, SM, ST, SL, SE;
		WSum.SetNumZeroed(NG); SM.SetNumZeroed(NG); ST.SetNumZeroed(NG); SL.SetNumZeroed(NG); SE.SetNumZeroed(NG);
		const double Sigma = 0.9 * Deg, Reach = 2.4 * Deg, CosReach = FMath::Cos(Reach);
		for (int32 C = 0; C < D.N; ++C)
		{
			const FVector Dir = FVector(D.Dir[C]);
			const double Lat = FMath::Asin(FMath::Clamp(Dir.Z, -1.0, 1.0));
			const double Lon = FMath::Atan2(Dir.Y, Dir.X);
			const double Land = D.Elev[C] > D.SeaLevel ? 1.0 : 0.0;
			const int32 Y0 = FMath::FloorToInt((Pi / 2 - (Lat + Reach)) / Pi * GH), Y1 = FMath::CeilToInt((Pi / 2 - (Lat - Reach)) / Pi * GH);
			for (int32 Y = FMath::Max(0, Y0); Y <= FMath::Min(GH - 1, Y1); ++Y)
			{
				const double GLat = Pi / 2 - (Y + 0.5) / GH * Pi;
				const double CosL = FMath::Max(FMath::Cos(GLat), 0.02);
				const double LonReach = FMath::Min(Pi, Reach / CosL);
				const int32 X0 = FMath::FloorToInt((Lon - LonReach + Pi) / (2 * Pi) * GW), X1 = FMath::CeilToInt((Lon + LonReach + Pi) / (2 * Pi) * GW);
				for (int32 XX = X0; XX <= X1; ++XX)
				{
					const int32 X = ((XX % GW) + GW) % GW;
					const double GLon = (X + 0.5) / GW * 2 * Pi - Pi;
					const double Cd = FVector::DotProduct(Dir, DirOf(GLon, GLat));
					if (Cd < CosReach) continue;
					const double Ang = FMath::Acos(FMath::Clamp(Cd, -1.0, 1.0));
					const double Wt = FMath::Exp(-0.5 * Sq(Ang / Sigma));
					const int32 I = Y * GW + X;
					WSum[I] += Wt; SM[I] += Wt * D.Moist[C]; ST[I] += Wt * D.Temp[C]; SL[I] += Wt * Land; SE[I] += Wt * D.Elev[C];
				}
			}
		}
		for (int32 I = 0; I < NG; ++I)
		{
			const double Wt = FMath::Max(WSum[I], 1e-9);
			GMoist[I] = float(SM[I] / Wt); GTemp[I] = float(ST[I] / Wt); GLand[I] = float(SL[I] / Wt); GElev[I] = float(SE[I] / Wt);
		}
	}
	// "Land to the east": cold upwelling coasts west of continents carry stratocumulus decks. East is
	// DECREASING longitude here (the sim's longitude increases westward).
	GLandEast.SetNumUninitialized(NG);
	for (int32 Y = 0; Y < GH; ++Y)
		for (int32 X = 0; X < GW; ++X)
		{
			double S = 0.0, Ws = 0.0;
			for (int32 K = 2; K <= 30; K += 2)   // 1..15 degrees east
			{
				const double Wt = 1.0 - K / 34.0;
				S += Wt * GLand[Y * GW + ((X - K) % GW + GW) % GW]; Ws += Wt;
			}
			GLandEast[Y * GW + X] = float(S / Ws);
		}
}

float FDmSkyWeather::SampleGrid(const TArray<float>& G, double Lon, double Lat) const
{
	const double FX = (Lon + Pi) / (2 * Pi) * GW - 0.5, FY = (Pi / 2 - Lat) / Pi * GH - 0.5;
	const int32 X0 = FMath::FloorToInt(FX), Y0 = FMath::FloorToInt(FY);
	const double TX = FX - X0, TY = FY - Y0;
	auto At = [&](int32 X, int32 Y) { X = ((X % GW) + GW) % GW; Y = FMath::Clamp(Y, 0, GH - 1); return double(G[Y * GW + X]); };
	return float(FMath::Lerp(FMath::Lerp(At(X0, Y0), At(X0 + 1, Y0), TX), FMath::Lerp(At(X0, Y0 + 1), At(X0 + 1, Y0 + 1), TX), TY));
}

double FDmSkyWeather::LandAround(double Lon, double Lat, double RadDeg) const
{
	if (!HasGrids()) return 0.0;
	double S = 0.0; int32 Nn = 0;
	for (double DY = -RadDeg; DY <= RadDeg; DY += 1.0)
		for (double DX = -RadDeg; DX <= RadDeg; DX += 1.0)
		{
			if (DX * DX + DY * DY > RadDeg * RadDeg) continue;
			S += SampleGrid(GLand, Lon + DX * Deg / FMath::Max(0.2, FMath::Cos(Lat)), FMath::Clamp(Lat + DY * Deg, -Pi / 2, Pi / 2)); ++Nn;
		}
	return Nn ? S / Nn : 0.0;
}

// ---------------------------------------------------------------------------------------------- weather maps

namespace
{
	/** One system, prepared for the texel loop. Local frame: E = compass east, Nn = north; structures are
	 *  written in (x = east, y = poleward) so the southern hemisphere is the mirror image (spin included). */
	struct FSys
	{
		FVector A, E, Nn;
		double Rho, CosReach, Sign, Phase, St;
		EDmCycloneType Type;
		FVector Off;
		// per-system shape variation (so no two lows are the same template)
		double Rot = 0, Cs = 1, Sn = 0, Curve = 0.07, Slope = 0.55, Len = 1.0, Spread = 1.0;
	};

	struct FMix
	{
		double C = 0, Top = 0, Cum = 0, Thk = 0, Ci = 0;
	};

	/** Tropical cyclone (port of the CloudLab structure, docs/lab/clouds.md 1.1). R in radii, Th cyclonic angle. */
	void Tropical(const FSys& S, double R, double Th, double Nw, double Nb, double Sg, double Rf,
		double& TC, double& TCTop, double& TCCum, double& TCThick, double& Ci, double& Moat)
	{
		const double St = S.St;
		const double LnR = FMath::Loge(FMath::Max(R, 1e-3));
		const double Eye = Smooth(0.05, 0.085, R);
		const double Cdo = FMath::Exp(-Sq(R / 0.5)) * (1.0 - Smooth(1.0, 1.6, R));
		// rain bands: log spirals that turn anticyclonically traced outward (Th grows cyclonically)
		const double Sp = 2.0 * (Th + 1.9 * LnR) - S.Phase + Nb + 2.2 * Nw;
		const double Seg = Smooth(-0.35, 0.15, Sg + 0.1);
		const double Cb = 0.5 + 0.5 * FMath::Cos(Sp);
		const double Band = FMath::Pow(Cb, 1.1) * Smooth(0.12, 0.35, R) * FMath::Exp(-Sq(R / 1.6)) * (1.0 - Smooth(1.7, 2.6, R)) * Seg;
		const double Wall = FMath::Exp(-Sq((R - 0.11) / 0.045));
		const double C = Eye * FMath::Max(FMath::Max(Cdo, Band * (0.9 + 0.5 * Nb)), Wall);
		TC = FMath::Max(TC, Sat(C * 1.3) * St);
		TCTop = FMath::Max(TCTop, Eye * FMath::Max(Cdo, Band * 0.75) * St);
		TCCum = FMath::Max(TCCum, (Band * 0.9 + Wall * 0.6) * St);
		TCThick = FMath::Max(TCThick, Eye * FMath::Max(FMath::Sqrt(Cdo), Band * 0.6) * St);
		// outflow cirrus: filaments spiralling out of the CDO (the warp shears the ridge noise)
		Ci = FMath::Max(Ci, St * Smooth(0.25, 0.55, R) * FMath::Exp(-Sq(R / 1.8)) * (1.0 - Smooth(1.8, 2.8, R)) * (0.05 + 0.95 * Rf) * 0.6);
		// sinking air around the storm: a ring of clearer sky
		Moat = FMath::Max(Moat, St * FMath::Exp(-Sq((R - 2.0) / 0.75)));
	}

	/** Extratropical low with its fronts (northern-hemisphere layout in x = east, y = poleward, radii). */
	void Extratropical(const FSys& S, double X0, double Y0, double Nw, double Nb, double Rf, double Wx, double Wy, FMix& L, double& Inf)
	{
		const double St = S.St;
		const double R0 = FMath::Sqrt(X0 * X0 + Y0 * Y0);
		// the template is turned, stretched and domain-warped per system: fronts bend, no straight edges
		double X = (X0 * S.Cs - Y0 * S.Sn) / S.Spread, Y = (X0 * S.Sn + Y0 * S.Cs) / S.Spread;
		const double Wf0 = Smooth(0.15, 0.6, R0);
		X += 0.9 * Wx * Wf0; Y += 0.9 * Wy * Wf0;
		const double R = FMath::Sqrt(X * X + Y * Y);
		const double Th = FMath::Atan2(Y, X);
		// cold front: from the triple point east of the low, trailing equatorward and westward, a little wavy
		const double Cfx = 0.35 + S.Slope * Y + S.Curve * Y * Y + 0.16 * Nw + 0.05 * Nb;
		const double Slope = S.Slope + 2.0 * S.Curve * Y;
		const double Perp = (X - Cfx) / FMath::Sqrt(1.0 + Slope * Slope);
		const double Along = Smooth(-3.4 * S.Len, -2.0 * S.Len, Y) * (1.0 - Smooth(0.05, 0.4, Y));
		// a long, narrow band: crisp on the cold (west) side, frayed on the warm side
		const double Cf = Smooth(-0.12, -0.04, Perp) * (1.0 - Smooth(0.05, 0.26 + 0.12 * Nb, Perp)) * Along * Sat(0.8 + 0.5 * Nb);
		// warm front: east of the triple point; a broad stratiform shield on its poleward side
		const double Yw = 0.08 - 0.22 * (X - 0.35) + 0.08 * Nw;
		const double Dyw = Y - Yw;
		const double WfX = Smooth(0.15, 0.5, X) * (1.0 - Smooth(1.7, 2.6, X));
		const double Wf = Smooth(-0.07, 0.03, Dyw) * (1.0 - Smooth(0.25, 0.95 + 0.25 * Nb, Dyw)) * WfX;
		// comma head: a band spiralling cyclonically in from the warm-front shield, plus the cloudy centre
		double Tp = Th - 0.3;
		if (Tp < 0.0) Tp += 2.0 * Pi;
		const double Rs = 0.95 * FMath::Exp(-0.21 * Tp), Wd = 0.2 * FMath::Exp(-0.1 * Tp) + 0.04;
		double Head = FMath::Exp(-Sq((R - Rs) / Wd)) * (1.0 - Smooth(4.6, 5.8, Tp)) * Sat(0.75 + 0.5 * Nb);
		Head = FMath::Max(Head, 0.9 * FMath::Exp(-Sq(R / 0.22)));
		// dry slot: clear air wrapping in from the south-west behind the cold front
		const double Dry = FMath::Exp(-Sq((Th + 1.75) / 0.5)) * Smooth(0.12, 0.3, R) * (1.0 - Smooth(0.9, 1.4, R));
		// warm sector: broken low cloud; cold air behind the front: open-cell cumulus
		const double Warm = Smooth(0.25, 0.5, Perp) * Smooth(0.05, 0.3, -Dyw) * (1.0 - Smooth(1.6, 2.4, R));
		const double Cold = Smooth(-0.15, -0.45, Perp) * (1.0 - Smooth(1.6, 2.7, R)) * (1.0 - Smooth(0.3, 0.9, Y));

		const double Parts[5] = { Cf, Wf * 0.9, Head, Warm * 0.4, Cold * 0.36 };
		const double Tops[5] = { 0.8, 0.62, 0.75, 0.35, 0.3 };
		const double Cums[5] = { 0.5, 0.15, 0.3, 0.75, 1.0 };
		const double Thks[5] = { 0.5, 0.4, 0.55, 0.15, 0.05 };
		double Not = 1.0, Ws = 1e-6, Tw = 0, Cw = 0, Kw = 0;
		for (int32 I = 0; I < 5; ++I)
		{
			const double C = Sat(Parts[I]);
			Not *= 1.0 - C; Ws += C; Tw += C * Tops[I]; Cw += C * Cums[I]; Kw += C * Thks[I];
		}
		L.C = (1.0 - Not) * (1.0 - 0.9 * Dry);
		L.Top = Tw / Ws; L.Cum = Cw / Ws; L.Thk = Kw / Ws;
		// cirrus ahead of the fronts: a shield east of the cold front and poleward of the warm front
		const double CiCf = Smooth(-0.02, 0.15, Perp) * (1.0 - Smooth(0.3, 0.8, Perp)) * Along;
		const double CiWf = Smooth(0.15, 0.5, Dyw) * (1.0 - Smooth(0.9, 1.5, Dyw)) * WfX;
		L.Ci = Sat(0.75 * CiCf + 0.6 * CiWf + 0.35 * Head) * (0.2 + 0.9 * Rf);
		Inf = St * (1.0 - Smooth(2.0, 3.0, R0));
	}
}

void FDmSkyWeather::BuildMaps(const FDmWeatherParams& P, const TArray<FDmSystemSnap>& Systems, TArray<FColor>& Out, TArray<FFloat16Color>& OutWarp, bool bBackground) const
{
	const double T0 = FPlatformTime::Seconds();
	const int32 W = P.Width, H = P.Height;
	Out.SetNumUninitialized(W * H);
	OutWarp.SetNumUninitialized(W * H);
	if (!HasGrids())
	{
		for (FColor& C : Out) C = FColor(0, 0, 0, 0);
		for (FFloat16Color& C : OutWarp) C = FFloat16Color(FLinearColor(0, 0, 0, 0));
		return;
	}
	// The noise domain slides with sky time (different rates per scale: cells change in about a minute,
	// synoptic patterns in a few), so successive maps evolve; no shear, no seams.
	const double Tm = P.MorphTime;
	const FVector Dir1 = FVector(0.31, -0.17, 0.12).GetSafeNormal(), Dir2 = FVector(-0.11, 0.27, -0.19).GetSafeNormal();
	const FVector O1 = Dir1 * (Tm * 0.0020), O2 = Dir2 * (Tm * 0.0010), O3 = Dir1 * (Tm * 0.00045), OS = Dir2 * (Tm * 0.0015), OW = Dir1 * (Tm * 0.0012);
	const double ItczLat = P.ItczLatDeg;
	const uint32 SeedOff = P.Seed % 1000u;
	const FVector Sd(SeedOff * 0.731, SeedOff * -0.517, SeedOff * 0.293);

	TArray<FSys> Sys;
	for (const FDmSystemSnap& S : Systems)
	{
		if (S.Strength <= 0.002) continue;
		FSys& F = Sys.AddDefaulted_GetRef();
		F.A = S.Dir.GetSafeNormal();
		DmCoords::Basis(F.A, F.E, F.Nn);
		F.Rho = FMath::Max(S.RadiusRad, 0.01);
		F.CosReach = FMath::Cos(FMath::Min(3.0 * F.Rho, Pi));
		F.Sign = S.Sign >= 0.0 ? 1.0 : -1.0;
		F.Phase = S.Phase;
		F.St = S.Strength;
		F.Type = S.Type;
		F.Off = FVector(double(S.Id % 97u) * 1.71, double(S.Id % 89u) * -2.33, double(S.Id % 83u) * 0.97);
		auto H01 = [&S](uint32 K) { uint32 V = S.Id * 0x9E3779B1u + K * 0x85EBCA77u; V ^= V >> 15; V *= 0x2C1B3C6Du; V ^= V >> 12; return double(V & 0xffffu) / 65535.0; };
		F.Rot = (H01(1) - 0.5) * 0.7;                // +-20 deg
		F.Cs = FMath::Cos(F.Rot); F.Sn = FMath::Sin(F.Rot);
		F.Curve = 0.03 + 0.1 * H01(2);
		F.Slope = 0.4 + 0.35 * H01(3);
		F.Len = 0.75 + 0.45 * H01(4);
		F.Spread = 0.85 + 0.3 * H01(5);
	}

	ParallelFor(H, [&](int32 Y)
	{
		const double Lat = Pi / 2 - (Y + 0.5) / H * Pi;
		const double LatD = Lat / Deg, ALat = FMath::Abs(LatD);
		const double V0 = (Y + 0.5) / H;
		TArray<int32, TInlineAllocator<16>> Near;
		for (int32 X = 0; X < W; ++X)
		{
			const double Lon = (X + 0.5) / W * 2 * Pi - Pi;
			const double U0 = (X + 0.5) / W;
			const FVector Dir = DirOf(Lon, Lat);
			const double Moist = SampleGrid(GMoist, Lon, Lat);
			const double Temp = SampleGrid(GTemp, Lon, Lat);
			const double Land = SampleGrid(GLand, Lon, Lat);
			const double LandE = SampleGrid(GLandEast, Lon, Lat);
			const double Elev = SampleGrid(GElev, Lon, Lat);
			const double Sea = 1.0 - Land;

			FVector Pw = Dir + Sd;
			const FVector Wq = (Dir + OW) * 3.0 + Sd;
			const FVector Warp(DmSkyNoise::Fbm(Wq + FVector(3.1, 0, 0), 3), DmSkyNoise::Fbm(Wq + FVector(0, 7.7, 0), 3), DmSkyNoise::Fbm(Wq + FVector(0, 0, 5.3), 3));
			Pw = Pw + Warp * 0.05;

			const double MoistC = Smooth(0.12, 0.85, Moist);
			const double ItczLatHere = ItczLat + 3.0 * FMath::Sin(Lon * 2.0 + 1.0) + 2.0 * Land * FMath::Sign(ItczLat + 0.01);
			const double Itcz = FMath::Exp(-Sq((LatD - ItczLatHere) / 8.0));
			const double Subtrop = FMath::Exp(-Sq((ALat - 26.0) / 9.0));
			const double Storm = FMath::Exp(-Sq((ALat - 52.0) / 13.0));
			const double Polar = Smooth(55.0, 72.0, ALat);

			const double N1 = DmSkyNoise::Fbm((Pw + O1) * 3.0, 4);                         // synoptic
			const double N2 = DmSkyNoise::Fbm((Pw + O2) * 11.0 + FVector(4.4), 4);          // mesoscale
			const double N3 = DmSkyNoise::Fbm((Pw + O3) * 45.0 + FVector(-8.1), 3);         // cell scale
			const FVector Ps = Pw + OS;
			const double NS = DmSkyNoise::Fbm(FVector(Ps.X * 7.0, Ps.Y * 7.0, Ps.Z * 20.0) + FVector(1.7), 4);   // jet streaks

			// regimes. The mid-latitude background is broken: the frontal systems carry the organised cloud there.
			const double Conv = Itcz * MoistC * Sat(0.3 + 1.8 * N1 + 1.0 * N2 + 0.3 * Land);
			const double Trade = Sea * Smooth(8.0, 18.0, ALat) * (1.0 - Smooth(34.0, 42.0, ALat)) * Sat(-0.55 + 2.2 * N3 + 1.4 * N2);   // patches, not a uniform field
			const double Deck = Sea * Subtrop * Smooth(0.15, 0.5, LandE) * Sat(0.75 + 1.2 * N2 + 0.6 * N1);
			const double Mid = Storm * Sat(0.05 + 0.55 * MoistC + 1.5 * NS + 0.9 * N1) * 0.75;
			const double Pol = Polar * Sat(0.5 + 1.2 * N1 + 0.5 * N2) * 0.9;
			const double Oro = Land * Smooth(2.5, 6.0, Elev) * MoistC * Sat(0.4 + 1.5 * N2);
			const double DryLand = Land * (1.0 - Smooth(0.12, 0.4, Moist));
			const double Clear = 1.0 - 0.95 * DryLand * (1.0 - Itcz);

			const double Cv[6] = { Conv, Trade * 0.5, Deck, Mid, Pol, Oro };
			const double Tops[6] = { 0.95, 0.3, 0.22, 0.55, 0.3, 0.65 };
			const double Cums[6] = { 1.0, 0.95, 0.35, 0.4, 0.1, 0.7 };
			const double Thick[6] = { 0.55, 0.0, 0.12, 0.3, 0.1, 0.4 };   // keep < 0.62: above marks hurricane cores (cirrus canopy)
			double NotC = 1.0, TopW = 0.0, CumW = 0.0, ThW = 0.0, WSum = 1e-6;
			for (int32 I = 0; I < 6; ++I)
			{
				const double C = Sat(Cv[I]);
				NotC *= 1.0 - C;
				TopW += C * Tops[I]; CumW += C * Cums[I]; ThW += C * Thick[I]; WSum += C;
			}
			double Cover = Sat((1.0 - NotC) * Clear);
			double Top = TopW / WSum, Cum = CumW / WSum, Thk = ThW / WSum;

			// background cirrus: zonal jet-stream streaks (noise stretched along the latitude circles in seamless
			// cylinder coordinates) and anvils over the deep convection
			double Ci = 0.0;
			{
				const double Jet = FMath::Exp(-Sq((ALat - 40.0) / 10.0)) * Smooth(0.15, 0.6, Moist);
				if (Jet > 0.02)
				{
					const double LatW = Lat + 0.05 * N1 + 0.02 * N2;
					const FVector Q(FMath::Cos(Lon) * 7.0, FMath::Sin(Lon) * 7.0, LatW * 75.0);
					const double Streak = 1.0 - 2.0 * FMath::Abs(DmSkyNoise::Fbm(Q + Sd + O2 * 40.0, 3));
					Ci = FMath::Max(Ci, Jet * Sat(1.6 * Streak * Streak * Streak - 0.3) * Sat(0.3 + 1.8 * N1) * 0.5);
				}
				if (Conv > 0.05)
				{
					const double An = DmSkyNoise::Fbm((Pw + O2) * 22.0 + FVector(-3.3), 3);
					Ci = FMath::Max(Ci, Conv * Sat(0.25 + 2.2 * An) * 0.6);
				}
			}

			// ---- weather systems
			Near.Reset();
			for (int32 K = 0; K < Sys.Num(); ++K) if (FVector::DotProduct(Dir, Sys[K].A) > Sys[K].CosReach) Near.Add(K);
			FVector Dq = Dir;
			if (Near.Num())
			{
				// pass 1: shear the cell-texture lookup into spirals. Rotating the LOOKUP clockwise by an angle that
				// grows inward turns the pattern counter-clockwise inside: NH rain bands then turn clockwise traced
				// outward, as on Earth. The angle fades to exactly 0 by 3 radii (no seam).
				for (int32 K : Near)
				{
					const FSys& S = Sys[K];
					if (S.Type == EDmCycloneType::Blob) continue;
					const double Cd = FVector::DotProduct(Dq, S.A);
					const double R = FMath::Acos(FMath::Clamp(Cd, -1.0, 1.0)) / S.Rho;
					const bool bTrop = S.Type == EDmCycloneType::Tropical;
					// (a tropical storm's shear stays inside its bands: further out it would comb the trade cumulus into a record groove)
					const double Sw = (bTrop ? 5.0 * (1.0 - Smooth(1.0, 2.4, R)) : 3.2 * (1.0 - Smooth(1.8, 3.0, R))) * FMath::Exp(-R * (bTrop ? 1.6 : 1.1)) * Sat(S.St * 1.5);
					const double Ang = -S.Sign * Sw;
					const FVector V = Dq - S.A * Cd;
					const double Vx = FVector::DotProduct(V, S.E), Vy = FVector::DotProduct(V, S.Nn);
					const double Cs = FMath::Cos(Ang), Sn = FMath::Sin(Ang);
					Dq = (S.A * Cd + S.E * (Vx * Cs - Vy * Sn) + S.Nn * (Vx * Sn + Vy * Cs)).GetSafeNormal();
				}
				const FVector Qn = Dq + Sd;
				const double Nw = DmSkyNoise::Fbm(Qn * 14.0 + FVector(5.0), 3);
				const double Nb = DmSkyNoise::Fbm(Qn * 40.0 + FVector(12.0), 3);
				const double Sg = DmSkyNoise::Fbm(Qn * 14.0 + FVector(-21.7, 3.3, 8.1), 3);
				const double Rf0 = 1.0 - 2.0 * FMath::Abs(DmSkyNoise::Fbm(Qn * 60.0 + FVector(2.9), 3));
				const double Rf = Rf0 * Rf0 * Rf0;

				double TC = 0, TCTop = 0, TCCum = 0, TCThick = 0, TCi = 0, Moat = 0, BL = 0;
				for (int32 K : Near)
				{
					const FSys& S = Sys[K];
					const double Cd = FVector::DotProduct(Dir, S.A);
					const double R = FMath::Acos(FMath::Clamp(Cd, -1.0, 1.0)) / S.Rho;
					if (R > 3.0) continue;
					const FVector V = Dir - S.A * Cd;
					const double Xl = FVector::DotProduct(V, S.E) / S.Rho, Yl = S.Sign * FVector::DotProduct(V, S.Nn) / S.Rho;
					const double Th = FMath::Atan2(Yl, Xl);
					if (S.Type == EDmCycloneType::Blob)
					{
						BL = FMath::Max(BL, FMath::Exp(-R * R * 2.2) * S.St * (0.8 + 0.4 * Nb));
					}
					else if (S.Type == EDmCycloneType::Tropical)
					{
						Tropical(S, R, Th, Nw, Nb, Sg, Rf, TC, TCTop, TCCum, TCThick, TCi, Moat);
					}
					else
					{
						// each low its own front wiggles
						const double Nws = DmSkyNoise::Fbm(Dir * 9.0 + S.Off, 3);
						const double Wx = DmSkyNoise::Fbm(Dir * 5.0 + S.Off * 1.3 + FVector(4.1), 3);
						const double Wy = DmSkyNoise::Fbm(Dir * 5.0 + S.Off * 1.3 + FVector(-7.9, 2.2, 0.5), 3);
						FMix L; double Inf = 0.0;
						Extratropical(S, Xl, Yl, Nws, Nb, Rf, Wx, Wy, L, Inf);
						Cover = FMath::Lerp(Cover, L.C, Inf);
						Top = FMath::Lerp(Top, L.Top, Inf);
						Cum = FMath::Lerp(Cum, L.Cum, Inf);
						Thk = FMath::Lerp(Thk, L.Thk, Inf);
						Ci = FMath::Lerp(Ci, L.Ci, Inf);
					}
				}
				if (BL > 0.0)
				{
					const double Ws = Cover + BL + 1e-4;
					Top = (Top * Cover + 0.9 * BL) / Ws; Cum = (Cum * Cover + 1.0 * BL) / Ws; Thk = FMath::Max(Thk, (Thk * Cover + 0.5 * BL) / Ws);
					Cover = 1.0 - (1.0 - Cover) * (1.0 - BL);
					Ci = FMath::Max(Ci, 0.5 * BL);
				}
				Cover *= 1.0 - 0.3 * Moat;
				Ci *= 1.0 - 0.6 * Moat;
				if (TC > 0.0)
				{
					Cover = FMath::Max(Cover * (1.0 - 0.7 * Smooth(0.2, 0.9, TCTop)), TC);
					Top = FMath::Lerp(Top, 1.0, Sat(TCTop * 1.4));
					Cum = FMath::Lerp(Cum, TCCum, Sat(TC));
					Thk = FMath::Max(Thk, TCThick);
				}
				Ci = FMath::Max(Ci, TCi);
			}
			Cover = Sat(Cover * P.CoverScale);
			Top *= FMath::Lerp(0.7, 1.0, Smooth(-20.0, 15.0, Temp));   // cold air: thinner, lower decks
			Ci = Sat(Ci * P.CoverScale);
			Out[Y * W + X] = FColor(B8(Cover), B8(Top), B8(Cum), B8(Thk));

			double Du = 0.0, Dv = 0.0;
			if (Dq != Dir)
			{
				const double Uq = FMath::Atan2(Dq.Y, Dq.X) / (2.0 * Pi) + 0.5;
				const double Vq = 0.5 - FMath::Asin(FMath::Clamp(Dq.Z, -1.0, 1.0)) / Pi;
				Du = Uq - U0; Du -= FMath::RoundToDouble(Du);   // the short way round
				Dv = Vq - V0;
			}
			OutWarp[Y * W + X] = FFloat16Color(FLinearColor(float(Du), float(Dv), float(Ci), 0.f));
		}
	}, bBackground ? EParallelForFlags::BackgroundPriority : EParallelForFlags::None);
	LastBuildSeconds = FPlatformTime::Seconds() - T0;
}

// ---------------------------------------------------------------------------------------------- fine cells

void FDmSkyWeather::BuildFine(int32 W, int32 H, uint32 Seed, TArray<FColor>& Out)
{
	Out.SetNumUninitialized(W * H);
	const FVector Sd(double(Seed % 997u) * 0.37, double(Seed % 991u) * -0.23, double(Seed % 983u) * 0.41);
	ParallelFor(H, [&](int32 Y)
	{
		const double Lat = Pi / 2 - (Y + 0.5) / H * Pi;
		for (int32 X = 0; X < W; ++X)
		{
			const double Lon = (X + 0.5) / W * 2 * Pi - Pi;
			const FVector Dir = DirOf(Lon, Lat);
			const FVector Wq = Dir * 3.0 + Sd;
			const FVector Warp(DmSkyNoise::Fbm(Wq + FVector(3.1, 0, 0), 3), DmSkyNoise::Fbm(Wq + FVector(0, 7.7, 0), 3), DmSkyNoise::Fbm(Wq + FVector(0, 0, 5.3), 3));
			const FVector Pw = Dir + Warp * 0.05 + Sd;
			auto Cells = [&Pw](const FVector& Off)
			{
				const double F1 = DmSkyNoise::Fbm(Pw * 70.0 + FVector(2.2) + Off, 3);
				const double F2 = DmSkyNoise::Fbm(Pw * 190.0 + FVector(-5.9) + Off, 3);
				const double F3 = 1.0 - 2.0 * FMath::Abs(DmSkyNoise::Fbm(Pw * 420.0 + FVector(8.8) + Off, 2));   // billowy
				return 0.55 * F1 + 0.35 * F2 + 0.1 * F3;
			};
			const double NA = Cells(FVector::ZeroVector);
			const double NB = Cells(FVector(31.7, -12.9, 47.3));
			const double Nw = DmSkyNoise::Fbm(Pw * 14.0 + FVector(5.0), 3);         // rain-band phase wobble
			const double Nb = DmSkyNoise::Fbm(Pw * 40.0 + FVector(12.0), 3);        // rain-band roughness (segments: B at an offset)
			Out[Y * W + X] = FColor(Enc(NA), Enc(NB), Enc(Nw), Enc(Nb));
		}
	}, EParallelForFlags::BackgroundPriority);
}

// ---------------------------------------------------------------------------------------------- textures

namespace
{
	void BuildMips(const TArray<FColor>& Pixels, int32 W, int32 H, TArray<TArray<FColor>>& Mips)
	{
		Mips.Reset();
		const TArray<FColor>* Prev = &Pixels;
		int32 MW = W, MH = H;
		while (MW > 1 && MH > 1)
		{
			const int32 NW = MW / 2, NH = MH / 2;
			TArray<FColor>& Next = Mips.AddDefaulted_GetRef();
			Next.SetNumUninitialized(NW * NH);
			const TArray<FColor>& Pv = *Prev;
			for (int32 Y = 0; Y < NH; ++Y)
				for (int32 X = 0; X < NW; ++X)
				{
					const FColor& A = Pv[(2 * Y) * MW + 2 * X]; const FColor& B = Pv[(2 * Y) * MW + 2 * X + 1];
					const FColor& C = Pv[(2 * Y + 1) * MW + 2 * X]; const FColor& Dd = Pv[(2 * Y + 1) * MW + 2 * X + 1];
					Next[Y * NW + X] = FColor(uint8((A.R + B.R + C.R + Dd.R + 2) / 4), uint8((A.G + B.G + C.G + Dd.G + 2) / 4),
						uint8((A.B + B.B + C.B + Dd.B + 2) / 4), uint8((A.A + B.A + C.A + Dd.A + 2) / 4));
				}
			Prev = &Next; MW = NW; MH = NH;
		}
	}

	void SetupMapTexture(UTexture2D* T)
	{
		T->SRGB = false;
		T->Filter = TF_Trilinear;
		T->AddressX = TA_Wrap;
		T->AddressY = TA_Clamp;
		T->CompressionSettings = TC_VectorDisplacementmap;   // linear sampler: must match the material's default texture
		T->NeverStream = true;
		T->UpdateResource();
	}
}

UTexture2D* FDmSkyWeather::MakeTexture(UObject* Outer, const TArray<FColor>& Pixels, int32 W, int32 H, const TCHAR* Name, bool bMips)
{
	if (Pixels.Num() != W * H) return nullptr;
	TConstArrayView64<uint8> Bytes(reinterpret_cast<const uint8*>(Pixels.GetData()), int64(Pixels.Num()) * 4);
	UTexture2D* T = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8, MakeUniqueObjectName(Outer, UTexture2D::StaticClass(), Name), Bytes);
	if (!T) return nullptr;
	if (bMips)
	{
		// full mip chain by hand (transient textures only get mip 0): distant cells average instead of aliasing
		TArray<TArray<FColor>> Mips;
		BuildMips(Pixels, W, H, Mips);
		FTexturePlatformData* PD = T->GetPlatformData();
		int32 MW = W, MH = H;
		for (const TArray<FColor>& M : Mips)
		{
			MW /= 2; MH /= 2;
			FTexture2DMipMap* Mip = new FTexture2DMipMap(MW, MH, 1);
			PD->Mips.Add(Mip);
			Mip->BulkData.Lock(LOCK_READ_WRITE);
			void* Dst = Mip->BulkData.Realloc(int64(MW) * MH * 4);
			FMemory::Memcpy(Dst, M.GetData(), int64(MW) * MH * 4);
			Mip->BulkData.Unlock();
		}
	}
	SetupMapTexture(T);
	return T;
}

UTexture2D* FDmSkyWeather::MakeFloatTexture(UObject* Outer, const TArray<FFloat16Color>& Pixels, int32 W, int32 H, const TCHAR* Name)
{
	if (Pixels.Num() != W * H) return nullptr;
	TConstArrayView64<uint8> Bytes(reinterpret_cast<const uint8*>(Pixels.GetData()), int64(Pixels.Num()) * 8);
	UTexture2D* T = UTexture2D::CreateTransient(W, H, PF_FloatRGBA, MakeUniqueObjectName(Outer, UTexture2D::StaticClass(), Name), Bytes);
	if (!T) return nullptr;
	SetupMapTexture(T);
	return T;
}

void FDmSkyWeather::UploadMip0(UTexture2D* Tex, void* Bytes, int32 W, int32 H, int32 BytesPerPixel)
{
	if (!Tex || !Bytes || Tex->GetSizeX() != W || Tex->GetSizeY() != H) { FMemory::Free(Bytes); return; }
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, uint32(W), uint32(H));
	Tex->UpdateTextureRegions(0, 1, Region, uint32(W * BytesPerPixel), uint32(BytesPerPixel), static_cast<uint8*>(Bytes),
		[](uint8* Data, const FUpdateTextureRegion2D* R) { FMemory::Free(Data); delete R; });
}

bool FDmSkyWeather::SavePreview(const FString& Path, const TArray<FColor>& Base, const TArray<FFloat16Color>& Warp, int32 W, int32 H) const
{
	if (Base.Num() != W * H) return false;
	const bool bWarp = Warp.Num() == W * H;
	const int32 PW = 2048, PH = 1024;
	TArray<FColor> Img; Img.SetNumUninitialized(PW * PH);
	for (int32 Y = 0; Y < PH; ++Y)
		for (int32 X = 0; X < PW; ++X)
		{
			const int32 I = (Y * H / PH) * W + X * W / PW;
			const FColor S = Base[I];
			const double Lon = (X + 0.5) / PW * 2 * Pi - Pi, Lat = Pi / 2 - (Y + 0.5) / PH * Pi;
			const double Land = HasGrids() ? SampleGrid(GLand, Lon, Lat) : 0.0;
			const FLinearColor Bg = FMath::Lerp(FLinearColor(0.03f, 0.08f, 0.2f), FLinearColor(0.25f, 0.3f, 0.15f), float(Land));
			const float Cv = float(S.R / 255.0), Top = S.G / 255.f;
			const FLinearColor Cl(0.7f + 0.3f * Top, 0.7f + 0.3f * Top, 0.75f + 0.25f * Top);
			FLinearColor Px = FMath::Lerp(Bg, Cl, Cv);
			if (bWarp) Px = FMath::Lerp(Px, FLinearColor(0.75f, 0.85f, 1.0f), 0.6f * FMath::Clamp(Warp[I].B.GetFloat(), 0.f, 1.f));
			// mirror horizontally so the preview reads like the globe seen from outside (sim longitude grows westward)
			Img[Y * PW + (PW - 1 - X)] = Px.ToFColor(false);
		}
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(PW, PH, TArrayView64<const FColor>(Img.GetData(), Img.Num()), Png);
	return FFileHelper::SaveArrayToFile(Png, *Path);
}

// ---------------------------------------------------------------------------------------------- noise volumes

namespace
{
	double Worley(const FVector& U, int32 Cells, uint32 Seed)
	{
		const FVector P = U * double(Cells);
		const int32 IX = FMath::FloorToInt(P.X), IY = FMath::FloorToInt(P.Y), IZ = FMath::FloorToInt(P.Z);
		double DMin = 10.0;
		for (int32 DZ = -1; DZ <= 1; ++DZ)
			for (int32 DY = -1; DY <= 1; ++DY)
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					const int32 CX = IX + DX, CY = IY + DY, CZ = IZ + DZ;
					const int32 WX = ((CX % Cells) + Cells) % Cells, WY = ((CY % Cells) + Cells) % Cells, WZ = ((CZ % Cells) + Cells) % Cells;
					const uint32 Hh = Hash3(WX, WY, WZ, Seed);
					const FVector F(CX + HashF(Hh), CY + HashF(Hh * 0x27d4eb2du + 1u), CZ + HashF(Hh * 0x165667b1u + 7u));
					DMin = FMath::Min(DMin, FVector::DistSquared(P, F));
				}
		return 1.0 - Sat(FMath::Sqrt(DMin));
	}

	double Perlin(const FVector& U, int32 Cells, uint32 Seed)
	{
		const FVector P = U * double(Cells);
		const int32 IX = FMath::FloorToInt(P.X), IY = FMath::FloorToInt(P.Y), IZ = FMath::FloorToInt(P.Z);
		const FVector F = P - FVector(IX, IY, IZ);
		auto Fade = [](double T) { return T * T * T * (T * (T * 6 - 15) + 10); };
		const FVector Fd(Fade(F.X), Fade(F.Y), Fade(F.Z));
		static const FVector G[12] = { {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},{1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},{0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1} };
		double V[8];
		for (int32 K = 0; K < 8; ++K)
		{
			const int32 OX = K & 1, OY = (K >> 1) & 1, OZ = (K >> 2) & 1;
			const int32 WX = (IX + OX) % Cells, WY = (IY + OY) % Cells, WZ = (IZ + OZ) % Cells;
			const FVector& Gr = G[Hash3((WX + Cells) % Cells, (WY + Cells) % Cells, (WZ + Cells) % Cells, Seed) % 12];
			V[K] = FVector::DotProduct(Gr, F - FVector(OX, OY, OZ));
		}
		const double X00 = FMath::Lerp(V[0], V[1], Fd.X), X10 = FMath::Lerp(V[2], V[3], Fd.X);
		const double X01 = FMath::Lerp(V[4], V[5], Fd.X), X11 = FMath::Lerp(V[6], V[7], Fd.X);
		return FMath::Lerp(FMath::Lerp(X00, X10, Fd.Y), FMath::Lerp(X01, X11, Fd.Y), Fd.Z);
	}

	double WorleyFbm(const FVector& U, int32 Cells, uint32 Seed)
	{
		return 0.625 * Worley(U, Cells, Seed) + 0.25 * Worley(U, Cells * 2, Seed + 1) + 0.125 * Worley(U, Cells * 4, Seed + 2);
	}

	UVolumeTexture* MakeVolume(UObject* Outer, int32 Size, const TCHAR* Name, uint32 EqualizeMask, TFunctionRef<FVector4(const FVector&)> Fn)
	{
		const int32 NV = Size * Size * Size;
		TArray<FVector4f> Val; Val.SetNumUninitialized(NV);
		ParallelFor(Size, [&](int32 Z)
		{
			for (int32 Y = 0; Y < Size; ++Y)
				for (int32 X = 0; X < Size; ++X)
					Val[(Z * Size + Y) * Size + X] = FVector4f(Fn(FVector((X + 0.5) / Size, (Y + 0.5) / Size, (Z + 0.5) / Size)));
		}, EParallelForFlags::BackgroundPriority);
		for (int32 Ch = 0; Ch < 4; ++Ch)
		{
			if (!(EqualizeMask & (1u << Ch))) continue;
			constexpr int32 NB = 4096;
			TArray<int32> Hist; Hist.SetNumZeroed(NB);
			for (const FVector4f& V : Val) Hist[FMath::Clamp(int32(V[Ch] * (NB - 1)), 0, NB - 1)]++;
			TArray<float> Cdf; Cdf.SetNumUninitialized(NB);
			int64 Acc = 0;
			for (int32 B = 0; B < NB; ++B) { Acc += Hist[B]; Cdf[B] = float(double(Acc - Hist[B] / 2) / NV); }
			for (FVector4f& V : Val) V[Ch] = Cdf[FMath::Clamp(int32(V[Ch] * (NB - 1)), 0, NB - 1)];
		}
		TArray<FColor> Vox; Vox.SetNumUninitialized(NV);
		for (int32 I = 0; I < NV; ++I)
		{
			const FVector4f& V = Val[I];
			auto Q = [](float F) { return uint8(FMath::RoundToInt(FMath::Clamp(F, 0.f, 1.f) * 255.f)); };
			Vox[I] = FColor(Q(V.X), Q(V.Y), Q(V.Z), Q(V.W));
		}
		UVolumeTexture* T = UVolumeTexture::CreateTransient(Size, Size, Size, PF_B8G8R8A8, MakeUniqueObjectName(Outer, UVolumeTexture::StaticClass(), Name));
		if (!T) return nullptr;
		FTexture2DMipMap& Mip = T->GetPlatformData()->Mips[0];
		void* Dst = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Dst, Vox.GetData(), Vox.Num() * 4);
		Mip.BulkData.Unlock();
		T->SRGB = false;
		T->Filter = TF_Bilinear;
		T->AddressMode = TA_Wrap;
		T->CompressionSettings = TC_Masks;
		T->UpdateResource();
		return T;
	}
}

UVolumeTexture* DmSkyNoise::MakeShapeVolume(UObject* Outer, int32 Size, uint32 Seed)
{
	// Perlin fBm keeps soft connected masses; Worley adds billows (pure Worley reads as polygons). R equalised.
	return MakeVolume(Outer, Size, TEXT("DmSkyShapeNoise"), 0x1, [Seed](const FVector& U)
	{
		const double Pn = 0.5 + 0.5 * (0.55 * Perlin(U, 4, Seed) + 0.28 * Perlin(U, 8, Seed + 3) + 0.17 * Perlin(U, 16, Seed + 4));
		const double W4 = WorleyFbm(U, 4, Seed + 10);
		const double W8 = WorleyFbm(U, 8, Seed + 20);
		const double PW = Pn - 0.3 * (1.0 - W4) - 0.12 * (1.0 - W8);
		return FVector4(Sat(0.5 + PW), W8, WorleyFbm(U, 16, Seed + 30), Worley(U, 32, Seed + 40));
	});
}

UVolumeTexture* DmSkyNoise::MakeDetailVolume(UObject* Outer, int32 Size, uint32 Seed)
{
	return MakeVolume(Outer, Size, TEXT("DmSkyDetailNoise"), 0x0, [Seed](const FVector& U)
	{
		return FVector4(WorleyFbm(U, 2, Seed + 50), WorleyFbm(U, 4, Seed + 60), WorleyFbm(U, 8, Seed + 70), Sat(0.5 + 0.7 * Perlin(U, 4, Seed + 80)));
	});
}
