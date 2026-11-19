#include "CloudWeather.h"
#include "LabPlanet.h"
#include "Engine/Texture2D.h"
#include "Engine/VolumeTexture.h"
#include "TextureResource.h"
#include "Async/ParallelFor.h"
#include "Math/UnrealMathUtility.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformTime.h"

namespace
{
	constexpr double Pi = UE_DOUBLE_PI;
	constexpr double Deg = UE_DOUBLE_PI / 180.0;

	inline double Sq(double X) { return X * X; }
	inline double Sat(double X) { return FMath::Clamp(X, 0.0, 1.0); }
	inline double Smooth(double E0, double E1, double X) { const double T = Sat((X - E0) / (E1 - E0)); return T * T * (3.0 - 2.0 * T); }
	inline double WrapPi(double A) { A = FMath::Fmod(A + Pi, 2.0 * Pi); if (A < 0) A += 2.0 * Pi; return A - Pi; }

	inline FVector DirOf(double Lon, double Lat)
	{
		const double C = FMath::Cos(Lat);
		return FVector(C * FMath::Cos(Lon), C * FMath::Sin(Lon), FMath::Sin(Lat));
	}

	/** fBm of the engine Perlin noise, roughly in [-1, 1]. */
	inline double Fbm(const FVector& P, int32 Oct, double Lac = 2.03, double Gain = 0.5)
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

	/** Rotate V about unit axis A by angle T (Rodrigues). */
	inline FVector RotateAbout(const FVector& V, const FVector& A, double T)
	{
		const double C = FMath::Cos(T), S = FMath::Sin(T);
		return V * C + FVector::CrossProduct(A, V) * S + A * (FVector::DotProduct(A, V) * (1.0 - C));
	}

	inline uint32 Hash3(int32 X, int32 Y, int32 Z, uint32 Seed)
	{
		uint32 H = uint32(X) * 0x8da6b343u ^ uint32(Y) * 0xd8163841u ^ uint32(Z) * 0xcb1ab31fu ^ Seed * 0x9e3779b9u;
		H ^= H >> 13; H *= 0x85ebca6bu; H ^= H >> 16; H *= 0xc2b2ae35u; H ^= H >> 16;
		return H;
	}
	inline double HashF(uint32 H) { return double(H & 0xffffffu) / double(0x1000000); }
}

// ---------------------------------------------------------------------------------------------
// Climate grids
// ---------------------------------------------------------------------------------------------

void FCloudWeather::BuildGrids(const FLabPlanetData& D)
{
	GW = 720; GH = 360; // 0.5 degree
	const int32 NG = GW * GH;
	TArray<double> WSum; WSum.SetNumZeroed(NG);
	TArray<double> SM, ST, SL, SE; SM.SetNumZeroed(NG); ST.SetNumZeroed(NG); SL.SetNumZeroed(NG); SE.SetNumZeroed(NG);
	const double Sigma = 0.9 * Deg, Reach = 2.4 * Deg;
	for (int32 C = 0; C < D.N; ++C)
	{
		const FVector3f Ps = D.Pos[C];
		const FVector Dir = FVector(Ps.X, Ps.Z, Ps.Y).GetSafeNormal(); // sim +Y north -> Unreal +Z
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
			const FVector GDirBase = FVector::ZeroVector;
			for (int32 XX = X0; XX <= X1; ++XX)
			{
				const int32 X = ((XX % GW) + GW) % GW;
				const double GLon = (X + 0.5) / GW * 2 * Pi - Pi;
				const double Ang = FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir, DirOf(GLon, GLat)), -1.0, 1.0));
				if (Ang > Reach) continue;
				const double Wt = FMath::Exp(-0.5 * Sq(Ang / Sigma));
				const int32 I = Y * GW + X;
				WSum[I] += Wt; SM[I] += Wt * D.Moist[C]; ST[I] += Wt * D.Temp[C]; SL[I] += Wt * Land; SE[I] += Wt * D.Elev[C];
			}
		}
	}
	GMoist.SetNumUninitialized(NG); GTemp.SetNumUninitialized(NG); GLand.SetNumUninitialized(NG); GElev.SetNumUninitialized(NG);
	for (int32 I = 0; I < NG; ++I)
	{
		const double Wt = FMath::Max(WSum[I], 1e-9);
		GMoist[I] = float(SM[I] / Wt); GTemp[I] = float(ST[I] / Wt); GLand[I] = float(SL[I] / Wt); GElev[I] = float(SE[I] / Wt);
	}
	// "Land to the east" over ocean: cold upwelling coasts west of continents carry stratocumulus decks.
	GLandEast.SetNumUninitialized(NG);
	for (int32 Y = 0; Y < GH; ++Y)
		for (int32 X = 0; X < GW; ++X)
		{
			double S = 0.0, Ws = 0.0;
			for (int32 K = 2; K <= 30; K += 2) // 1..15 degrees east
			{
				const double Wt = 1.0 - K / 34.0;
				S += Wt * GLand[Y * GW + (X + K) % GW]; Ws += Wt;
			}
			GLandEast[Y * GW + X] = float(S / Ws);
		}
}

float FCloudWeather::SampleGrid(const TArray<float>& G, double Lon, double Lat) const
{
	const double FX = (Lon + Pi) / (2 * Pi) * GW - 0.5, FY = (Pi / 2 - Lat) / Pi * GH - 0.5;
	const int32 X0 = FMath::FloorToInt(FX), Y0 = FMath::FloorToInt(FY);
	const double TX = FX - X0, TY = FY - Y0;
	auto At = [&](int32 X, int32 Y) { X = ((X % GW) + GW) % GW; Y = FMath::Clamp(Y, 0, GH - 1); return double(G[Y * GW + X]); };
	return float(FMath::Lerp(FMath::Lerp(At(X0, Y0), At(X0 + 1, Y0), TX), FMath::Lerp(At(X0, Y0 + 1), At(X0 + 1, Y0 + 1), TX), TY));
}

double FCloudWeather::LandAround(double Lon, double Lat, double RadDeg) const
{
	double S = 0.0; int32 Nn = 0;
	for (double DY = -RadDeg; DY <= RadDeg; DY += 1.0)
		for (double DX = -RadDeg; DX <= RadDeg; DX += 1.0)
		{
			if (DX * DX + DY * DY > RadDeg * RadDeg) continue;
			S += SampleGrid(GLand, Lon + DX * Deg / FMath::Max(0.2, FMath::Cos(Lat)), Lat + DY * Deg); ++Nn;
		}
	return Nn ? S / Nn : 0.0;
}

// ---------------------------------------------------------------------------------------------
// Weather map
// ---------------------------------------------------------------------------------------------

void FCloudWeather::Build(const FLabPlanetData& D, const FCloudWeatherParams& P)
{
	const double T0 = FPlatformTime::Seconds();
	W = P.Width; H = P.Height;
	BuildGrids(D);

	// ---- weather systems
	Cyclones.Reset();
	FRandomStream Rng(int32(P.Seed * 7919 + 13));
	{
		// Tropical cyclones: search the given latitude for the open-ocean longitude nearest the preferred one.
		auto Spawn = [&](double LatDeg, double PrefLonDeg, double RadDeg, double Sign)
		{
			double Best = 1e9, BestLon = PrefLonDeg;
			for (double L = -180; L < 180; L += 3)
			{
				const double Land = LandAround(L * Deg, LatDeg * Deg, RadDeg * 0.8);
				const double Score = Land * 10.0 + FMath::Abs(WrapPi((L - PrefLonDeg) * Deg)) * 0.15;
				if (Score < Best) { Best = Score; BestLon = L; }
			}
			FCloudCyclone C;
			C.Center = DirOf(BestLon * Deg, LatDeg * Deg);
			C.RadiusRad = RadDeg * Deg; C.Sign = Sign; C.bTropical = true; C.Phase = Rng.FRandRange(0, 2 * Pi);
			Cyclones.Add(C);
			UE_LOG(LogTemp, Display, TEXT("CloudLab: tropical cyclone at lat %.1f lon %.1f r %.1f deg (land %.2f)"), LatDeg, BestLon, RadDeg, LandAround(BestLon * Deg, LatDeg * Deg, RadDeg * 0.8));
		};
		Spawn(P.TropicalLatDeg, P.TropicalLonDeg, P.TropicalRadiusDeg, 1.0);
		Spawn(-P.TropicalLatDeg + 2.0, P.TropicalLonDeg + 150.0, P.TropicalRadiusDeg * 0.7, -1.0);
	}
	for (int32 Hemi = 0; Hemi < 2; ++Hemi)
	{
		const double Sign = Hemi == 0 ? 1.0 : -1.0;
		const double Off = Rng.FRandRange(0, 360);
		for (int32 K = 0; K < P.ExtratropicalPerHemi; ++K)
		{
			const double LonDeg = Off + K * 360.0 / P.ExtratropicalPerHemi + Rng.FRandRange(-25, 25);
			const double LatDeg = Sign * Rng.FRandRange(44, 58);
			FCloudCyclone C;
			C.Center = DirOf(LonDeg * Deg, LatDeg * Deg);
			C.RadiusRad = Rng.FRandRange(10, 15) * Deg; C.Sign = Sign; C.bTropical = false; C.Phase = Rng.FRandRange(0, 2 * Pi);
			Cyclones.Add(C);
			UE_LOG(LogTemp, Display, TEXT("CloudLab: extratropical low at lat %.1f lon %.1f r %.1f deg"), LatDeg, FMath::Fmod(LonDeg + 540.0, 360.0) - 180.0, C.RadiusRad / Deg);
		}
	}

	// Per-cyclone local frames
	struct FFrame { FVector C, E, N; };
	TArray<FFrame> Fr;
	for (const FCloudCyclone& C : Cyclones)
	{
		FVector E = FVector::CrossProduct(FVector::ZAxisVector, C.Center).GetSafeNormal();
		if (E.IsNearlyZero()) E = FVector::YAxisVector;
		Fr.Add({ C.Center, E, FVector::CrossProduct(C.Center, E) });
	}

	Pixels.SetNumUninitialized(W * H);
	const double ItczLat = P.ItczLatDeg;
	ParallelFor(H, [&](int32 Y)
	{
		const double Lat = Pi / 2 - (Y + 0.5) / H * Pi;
		const double LatD = Lat / Deg, ALat = FMath::Abs(LatD);
		for (int32 X = 0; X < W; ++X)
		{
			const double Lon = (X + 0.5) / W * 2 * Pi - Pi;
			const FVector Dir = DirOf(Lon, Lat);
			const double Moist = SampleGrid(GMoist, Lon, Lat);
			const double Temp = SampleGrid(GTemp, Lon, Lat);
			const double Land = SampleGrid(GLand, Lon, Lat);
			const double LandE = SampleGrid(GLandEast, Lon, Lat);
			const double Elev = SampleGrid(GElev, Lon, Lat);
			const double Sea = 1.0 - Land;

			// Domain warp: cyclones shear the noise into spirals; a large-scale warp keeps shapes organic.
			FVector Pw = Dir;
			double TC = 0.0, TCTop = 0.0, TCCum = 0.0, TCThick = 0.0; // tropical-cyclone contribution
			double EC = 0.0;                                         // extratropical contribution
			for (int32 K = 0; K < Cyclones.Num(); ++K)
			{
				const FCloudCyclone& Cy = Cyclones[K];
				const double Cd = FVector::DotProduct(Dir, Fr[K].C);
				const double Ang = FMath::Acos(FMath::Clamp(Cd, -1.0, 1.0));
				const double R = Ang / Cy.RadiusRad;
				if (R > 3.0) continue;
				// Differential rotation: strong near the core, fading outward (sign gives the hemisphere's sense).
				const double Swirl = (Cy.bTropical ? 7.0 : 3.2) * FMath::Exp(-R * (Cy.bTropical ? 1.6 : 1.1)) * (1.0 - Smooth(1.8, 3.0, R));
				Pw = RotateAbout(Pw, Fr[K].C, -Cy.Sign * Swirl);
			}
			const FVector Wq = Pw * 3.0;
			const FVector Warp(Fbm(Wq + FVector(3.1, 0, 0), 3), Fbm(Wq + FVector(0, 7.7, 0), 3), Fbm(Wq + FVector(0, 0, 5.3), 3));
			Pw = Pw + Warp * 0.05;

			for (int32 K = 0; K < Cyclones.Num(); ++K)
			{
				const FCloudCyclone& Cy = Cyclones[K];
				const double Cd = FVector::DotProduct(Dir, Fr[K].C);
				const double Ang = FMath::Acos(FMath::Clamp(Cd, -1.0, 1.0));
				const double R = Ang / Cy.RadiusRad;
				if (R > 3.0) continue;
				const FVector V = Dir - Fr[K].C * Cd;
				const double Th = FMath::Atan2(FVector::DotProduct(V, Fr[K].N), FVector::DotProduct(V, Fr[K].E));
				const double LnR = FMath::Loge(FMath::Max(R, 1e-3));
				const double Nb = Fbm(Pw * 40.0 + FVector(K * 11.0), 3);
				if (Cy.bTropical)
				{
					const double Eye = Smooth(0.05, 0.085, R);
					const double Cdo = FMath::Exp(-Sq(R / 0.36));
					// Rain bands: two log-spiral arms, wavy (low-frequency phase noise) and broken into segments.
					const double Nw = Fbm(Pw * 14.0 + FVector(K * 5.0), 3);
					const double S = 2.0 * (Th + Cy.Sign * 1.9 * LnR) - Cy.Phase + 1.0 * Nb + 2.2 * Nw;
					const double Seg = Smooth(-0.25, 0.25, Fbm(Pw * 22.0 + FVector(K * 9.0 + 3.0), 3) + 0.1);
					const double Band = FMath::Pow(0.5 + 0.5 * FMath::Cos(S), 2.0) * Smooth(0.15, 0.4, R) * FMath::Exp(-Sq(R / 1.3)) * Seg;
					const double Wall = FMath::Exp(-Sq((R - 0.11) / 0.045)); // bright eyewall ring
					const double C = Eye * FMath::Max(FMath::Max(Cdo, Band * (0.75 + 0.5 * Nb)), Wall);
					TC = FMath::Max(TC, Sat(C * 1.15));
					TCTop = FMath::Max(TCTop, Eye * FMath::Max(Cdo * 1.0, Band * 0.75));
					TCCum = FMath::Max(TCCum, Band * 0.9 + Wall * 0.6);
					TCThick = FMath::Max(TCThick, Eye * FMath::Max(FMath::Sqrt(Cdo), Band * 0.6));
				}
				else
				{
					// Comma cloud: a single long logarithmic arm (cold front / conveyor belt) and a head near the low.
					const double S = Th + Cy.Sign * 1.05 * LnR - Cy.Phase;
					const double Dl = WrapPi(S + 0.6 * Nb);
					const double Wd = 0.38 / FMath::Max(0.35, R);
					const double Arm = FMath::Exp(-Sq(Dl / Wd)) * Smooth(0.08, 0.3, R) * (1.0 - Smooth(1.3, 2.6, R));
					const double Head = FMath::Exp(-Sq(R / 0.42)) * Smooth(0.0, 0.6, 0.5 + 0.5 * FMath::Cos(Th - Cy.Phase - 1.2 * Cy.Sign));
					EC = FMath::Max(EC, Sat(FMath::Max(Arm, Head * 0.9) * (0.75 + 0.6 * Nb)));
				}
			}

			// ---- climatological regimes
			const double MoistC = Smooth(0.12, 0.85, Moist);
			const double ItczLatHere = ItczLat + 3.0 * FMath::Sin(Lon * 2.0 + 1.0) + 2.0 * Land * FMath::Sign(ItczLat + 0.01);
			const double Itcz = FMath::Exp(-Sq((LatD - ItczLatHere) / 8.0));
			const double Subtrop = FMath::Exp(-Sq((ALat - 26.0) / 9.0));
			const double Storm = FMath::Exp(-Sq((ALat - 52.0) / 13.0));
			const double Polar = Smooth(55.0, 72.0, ALat);

			const double N1 = Fbm(Pw * 3.0, 4);                                  // synoptic
			const double N2 = Fbm(Pw * 11.0 + FVector(4.4), 4);                   // mesoscale
			const double N3 = Fbm(Pw * 45.0 + FVector(-8.1), 3);                  // cell scale
			const FVector Pst(Pw.X * 7.0, Pw.Y * 7.0, Pw.Z * 20.0);                // zonally stretched (jet streaks)
			const double NS = Fbm(Pst + FVector(1.7), 4);

			// Deep tropical convection: clusters (MCS) along the ITCZ, more over moist land.
			const double Conv = Itcz * MoistC * Sat(0.35 + 1.7 * N1 + 1.0 * N2 + 0.3 * Land);
			// Trade-wind cumulus: scattered small heaps over warm ocean outside the ITCZ.
			const double Trade = Sea * Smooth(8.0, 18.0, ALat) * (1.0 - Smooth(34.0, 42.0, ALat)) * Sat(-0.05 + 1.8 * N3 + 0.9 * N2);
			// Marine stratocumulus decks off west coasts in the subtropics.
			const double Deck = Sea * Subtrop * Smooth(0.15, 0.5, LandE) * Sat(0.75 + 1.2 * N2 + 0.6 * N1);
			// Mid-latitude frontal / stratiform sheets, streaky along the jet.
			const double Mid = Storm * Sat(0.3 + 0.8 * MoistC + 1.6 * NS + 0.8 * N1);
			// Polar stratus.
			const double Pol = Polar * Sat(0.5 + 1.2 * N1 + 0.5 * N2) * 0.9;
			// Orographic: moist air forced over high ground.
			const double Oro = Land * Smooth(2.5, 6.0, Elev) * MoistC * Sat(0.4 + 1.5 * N2);
			// Dry subsidence over hot deserts clears the sky.
			const double DryLand = Land * (1.0 - Smooth(0.12, 0.4, Moist));
			const double Clear = 1.0 - 0.92 * DryLand * (1.0 - Itcz);

			// Combine (probabilistic union) and attribute height / type.
			double Cv[7] = { Conv, Trade * 0.75, Deck, Mid, Pol, Oro, EC };
			const double Tops[7] = { 0.95, 0.3, 0.22, 0.6, 0.3, 0.65, 0.7 };
			const double Cums[7] = { 1.0, 0.95, 0.35, 0.25, 0.1, 0.7, 0.3 };
			const double Thick[7] = { 0.55, 0.0, 0.1, 0.35, 0.1, 0.4, 0.5 }; // keep < 0.62: A above that marks hurricane cores (cirrus canopy)
			double NotC = 1.0, TopW = 0.0, CumW = 0.0, ThW = 0.0, WSum = 1e-6;
			for (int32 I = 0; I < 7; ++I)
			{
				const double C = Sat(Cv[I]);
				NotC *= 1.0 - C;
				TopW += C * Tops[I]; CumW += C * Cums[I]; ThW += C * Thick[I]; WSum += C;
			}
			double Cover = (1.0 - NotC) * Clear;
			double Top = TopW / WSum, Cum = CumW / WSum, Thk = ThW / WSum;
			// Tropical cyclones override everything inside their circulation.
			if (TC > 0.0)
			{
				Cover = FMath::Max(Cover * (1.0 - 0.7 * Smooth(0.2, 0.9, TCTop)), TC);
				Top = FMath::Lerp(Top, 1.0, Sat(TCTop * 1.4));
				Cum = FMath::Lerp(Cum, TCCum, Sat(TC));
				Thk = FMath::Max(Thk, TCThick);
			}
			Cover = Sat(Cover * P.CoverScale);
			// Presence field: 0.5 is the cloud edge. Fine noise carries the cloud cells (heaps get high-contrast puffs,
			// sheets get only small holes), so the map itself reads like a satellite photo at texel scale.
			{
				const double F1 = Fbm(Pw * 70.0 + FVector(2.2), 3);
				const double F2 = Fbm(Pw * 190.0 + FVector(-5.9), 3);
				const double F3 = 1.0 - 2.0 * FMath::Abs(Fbm(Pw * 420.0 + FVector(8.8), 2)); // billowy
				const double Nf = 0.55 * F1 + 0.35 * F2 + 0.1 * F3;
				const double Amp = FMath::Lerp(0.35, 1.1, Cum);
				Cover = Sat(0.1 + Cover * 0.9 + Amp * Nf * (0.5 + Cover));
			}
			// Cold air holds less water: thinner, lower decks toward the poles.
			Top *= FMath::Lerp(0.7, 1.0, Smooth(-20.0, 15.0, Temp));

			Pixels[Y * W + X] = FColor(uint8(FMath::RoundToInt(Sat(Cover) * 255.0)), uint8(FMath::RoundToInt(Sat(Top) * 255.0)),
				uint8(FMath::RoundToInt(Sat(Cum) * 255.0)), uint8(FMath::RoundToInt(Sat(Thk) * 255.0)));
		}
	});
	BuildSeconds = FPlatformTime::Seconds() - T0;
	UE_LOG(LogTemp, Display, TEXT("CloudLab: weather map %dx%d built in %.2f s"), W, H, BuildSeconds);
}

UTexture2D* FCloudWeather::MakeTexture(UObject* Outer) const
{
	TConstArrayView64<uint8> Bytes(reinterpret_cast<const uint8*>(Pixels.GetData()), int64(Pixels.Num()) * 4);
	UTexture2D* T = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8, TEXT("CloudWeatherMap"), Bytes);
	if (!T) return nullptr;
	// Full mip chain (2x2 box filter). The material reads a blurred mip for cloud-top heights and for
	// distant samples; without mips, transient textures only have level 0.
	FTexturePlatformData* PD = T->GetPlatformData();
	TArray<FColor> Prev = Pixels;
	int32 MW = W, MH = H;
	while (MW > 1 && MH > 1)
	{
		const int32 NW = MW / 2, NH = MH / 2;
		TArray<FColor> Next; Next.SetNumUninitialized(NW * NH);
		for (int32 Y = 0; Y < NH; ++Y)
			for (int32 X = 0; X < NW; ++X)
			{
				const FColor& A = Prev[(2 * Y) * MW + 2 * X]; const FColor& B = Prev[(2 * Y) * MW + 2 * X + 1];
				const FColor& C = Prev[(2 * Y + 1) * MW + 2 * X]; const FColor& Dd = Prev[(2 * Y + 1) * MW + 2 * X + 1];
				Next[Y * NW + X] = FColor(uint8((A.R + B.R + C.R + Dd.R + 2) / 4), uint8((A.G + B.G + C.G + Dd.G + 2) / 4),
					uint8((A.B + B.B + C.B + Dd.B + 2) / 4), uint8((A.A + B.A + C.A + Dd.A + 2) / 4));
			}
		FTexture2DMipMap* Mip = new FTexture2DMipMap(NW, NH, 1);
		PD->Mips.Add(Mip);
		Mip->BulkData.Lock(LOCK_READ_WRITE);
		void* Dst = Mip->BulkData.Realloc(int64(NW) * NH * 4);
		FMemory::Memcpy(Dst, Next.GetData(), int64(NW) * NH * 4);
		Mip->BulkData.Unlock();
		Prev = MoveTemp(Next); MW = NW; MH = NH;
	}
	T->SRGB = false;
	T->Filter = TF_Trilinear;
	T->AddressX = TA_Wrap;
	T->AddressY = TA_Clamp;
	T->CompressionSettings = TC_VectorDisplacementmap;
	T->NeverStream = true;
	T->UpdateResource();
	return T;
}

bool FCloudWeather::SavePng(const FString& Path) const
{
	// Preview: cover in white over a land / sea map, cloud tops tinting toward warm white.
	const int32 PW = 2048, PH = 1024;
	TArray<FColor> Img; Img.SetNumUninitialized(PW * PH);
	for (int32 Y = 0; Y < PH; ++Y)
		for (int32 X = 0; X < PW; ++X)
		{
			const FColor S = Pixels[(Y * H / PH) * W + X * W / PW];
			const double Lon = (X + 0.5) / PW * 2 * Pi - Pi, Lat = Pi / 2 - (Y + 0.5) / PH * Pi;
			const double Land = SampleGrid(GLand, Lon, Lat);
			const FLinearColor Base = FMath::Lerp(FLinearColor(0.03f, 0.08f, 0.2f), FLinearColor(0.25f, 0.3f, 0.15f), float(Land));
			const float Cv = float(Smooth(0.42, 0.62, S.R / 255.0)), Top = S.G / 255.f; // presence field: 0.5 = edge
			const FLinearColor Cl = FLinearColor(0.75f + 0.25f * Top, 0.75f + 0.25f * Top, 0.8f + 0.2f * Top);
			Img[Y * PW + X] = FMath::Lerp(Base, Cl, Cv).ToFColor(false);
		}
	IImageWrapperModule& M = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Png = M.CreateImageWrapper(EImageFormat::PNG);
	if (!Png->SetRaw(Img.GetData(), Img.Num() * 4, PW, PH, ERGBFormat::BGRA, 8)) return false;
	return FFileHelper::SaveArrayToFile(Png->GetCompressed(), *Path);
}

// ---------------------------------------------------------------------------------------------
// Tileable noise volumes
// ---------------------------------------------------------------------------------------------

namespace
{
	/** Inverted Worley (1 at feature points), tileable with Cells cells per unit. */
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

	/** Tileable gradient noise in [-1, 1] with Cells lattice cells per unit. */
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

	/** Fills a volume from Fn (channels in [0,1]). Channels flagged in EqualizeMask are histogram-equalised,
	 *  so a threshold of (1 - c) in the material covers a fraction c of space: the weather map's cover then
	 *  means "fraction of sky covered", independent of the noise's own distribution. */
	UVolumeTexture* MakeVolume(UObject* Outer, int32 Size, const TCHAR* Name, uint32 EqualizeMask, TFunctionRef<FVector4(const FVector&)> Fn)
	{
		const int32 NV = Size * Size * Size;
		TArray<FVector4f> Val; Val.SetNumUninitialized(NV);
		ParallelFor(Size, [&](int32 Z)
		{
			for (int32 Y = 0; Y < Size; ++Y)
				for (int32 X = 0; X < Size; ++X)
					Val[(Z * Size + Y) * Size + X] = FVector4f(Fn(FVector((X + 0.5) / Size, (Y + 0.5) / Size, (Z + 0.5) / Size)));
		});
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
		UVolumeTexture* T = UVolumeTexture::CreateTransient(Size, Size, Size, PF_B8G8R8A8, Name);
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

	inline uint8 B8(double V) { return uint8(FMath::RoundToInt(Sat(V) * 255.0)); }
}

UVolumeTexture* CloudNoise::MakeShapeVolume(UObject* Outer, int32 Size, uint32 Seed)
{
	// R: billowy Perlin-Worley base shape (equalised). G/B/A: Worley fBm at 2x/4x/8x for optional erosion.
	return MakeVolume(Outer, Size, TEXT("CloudShapeNoise"), 0x1, [Seed](const FVector& U)
	{
		// Perlin fBm keeps soft, connected masses; Worley adds the cauliflower billows (pure Worley reads as polygons).
		const double Pn = 0.5 + 0.5 * (0.55 * Perlin(U, 4, Seed) + 0.28 * Perlin(U, 8, Seed + 3) + 0.17 * Perlin(U, 16, Seed + 4));
		const double W4 = WorleyFbm(U, 4, Seed + 10);
		const double W8 = WorleyFbm(U, 8, Seed + 20);
		const double PW = Pn - 0.3 * (1.0 - W4) - 0.12 * (1.0 - W8);
		return FVector4(Sat(0.5 + PW), W8, WorleyFbm(U, 16, Seed + 30), Worley(U, 32, Seed + 40));
	});
}

UVolumeTexture* CloudNoise::MakeDetailVolume(UObject* Outer, int32 Size, uint32 Seed)
{
	return MakeVolume(Outer, Size, TEXT("CloudDetailNoise"), 0x0, [Seed](const FVector& U)
	{
		return FVector4(WorleyFbm(U, 2, Seed + 50), WorleyFbm(U, 4, Seed + 60), WorleyFbm(U, 8, Seed + 70), Sat(0.5 + 0.7 * Perlin(U, 4, Seed + 80)));
	});
}
