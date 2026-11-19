// DEMIURGE — Game/Planet/DmPlanetMaps.cpp : see the header. Owner: C1 planet.

#include "Game/Planet/DmPlanetMaps.h"
#include "Game/Planet/DmPlanetNoise.h"
#include "Game/Planet/DmPlanetTerrain.h"
#include "Sim/State.h"

#include "Async/ParallelFor.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformTime.h"
#include "RHI.h"
#include "TextureResource.h"

using namespace DmPlanetNoise;

namespace
{
	inline FVector3d Nrm(const FVector3d& V) { return V * (1.0 / FMath::Sqrt(FMath::Max(V | V, 1e-300))); }
	inline FVector3d UEToSim(const FVector3d& V) { return FVector3d(V.X, V.Z, V.Y); }
	inline FVector3d SimToUE(const FVector3d& V) { return FVector3d(V.X, V.Z, V.Y); }
	inline uint8 Q8(double V) { return uint8(FMath::Clamp(FMath::RoundToInt(V * 255.0), 0, 255)); }
	inline FVector3f Lerp3(const FVector3f& A, const FVector3f& B, double T) { return A + (B - A) * float(FMath::Clamp(T, 0.0, 1.0)); }

	// Biome ids (dm::Biome)
	enum : uint8 { BSea, BShallows, BBeach, BDesert, BSavanna, BGrassland, BForest, BRainforest, BTaiga, BTundra, BIce, BRock, BSnowpeak, BWetland, BBarren, BSeaIce };

	void SmoothField(const FDmGeoLocator& G, TArray<float>& F, TArray<float>& Tmp, int32 Passes)
	{
		for (int32 P = 0; P < Passes; ++P)
		{
			Tmp = F;
			ParallelFor(G.N, [&](int32 C)
			{
				const int32 Deg = G.NbrN[C];
				if (!Deg) return;
				double S = 0; for (int32 K = 0; K < Deg; ++K) S += Tmp[G.Nbr[6 * C + K]];
				F[C] = 0.5f * Tmp[C] + 0.5f * float(S / Deg);
			}, EParallelForFlags::BackgroundPriority);
		}
	}

	const double kAgeGlow[7] = { 0.25, 0.35, 0.45, 0.6, 1.0, 1.2, 1.4 };

	FVector3f Ramp(const FVector3f* Stops, int32 NStops, double T)
	{
		T = FMath::Clamp(T, 0.0, 1.0) * (NStops - 1);
		const int32 I = FMath::Min(int32(T), NStops - 2);
		return Lerp3(Stops[I], Stops[I + 1], T - I);
	}

	void MakeMips(const TArray<FColor>& Mip0, int32 Size, TArray<TArray<FColor>>& Out)
	{
		Out.Reset();
		Out.Add(Mip0);
		for (int32 S = Size / 2; S >= 1; S /= 2)
		{
			const TArray<FColor>& Prev = Out.Last();
			TArray<FColor> Next; Next.SetNumUninitialized(S * S);
			const int32 P = S * 2;
			for (int32 Y = 0; Y < S; ++Y)
				for (int32 X = 0; X < S; ++X)
				{
					const FColor& A = Prev[(2 * Y) * P + 2 * X]; const FColor& B = Prev[(2 * Y) * P + 2 * X + 1];
					const FColor& C = Prev[(2 * Y + 1) * P + 2 * X]; const FColor& D = Prev[(2 * Y + 1) * P + 2 * X + 1];
					Next[Y * S + X] = FColor(uint8((A.R + B.R + C.R + D.R + 2) / 4), uint8((A.G + B.G + C.G + D.G + 2) / 4), uint8((A.B + B.B + C.B + D.B + 2) / 4), uint8((A.A + B.A + C.A + D.A + 2) / 4));
				}
			Out.Add(MoveTemp(Next));
		}
	}

	UTexture2D* NewTexture(UObject* Outer, int32 Size, const TArray<TArray<FColor>>& Mips, const TCHAR* Name)
	{
		UTexture2D* T = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8, FName(Name));
		if (!T) return nullptr;
		T->SRGB = false;
		T->Filter = TF_Trilinear;
		T->AddressX = TA_Clamp; T->AddressY = TA_Clamp;
		T->NeverStream = true;
		T->LODGroup = TEXTUREGROUP_UI;   // no LOD bias / streaming
		FTexturePlatformData* PD = T->GetPlatformData();
		for (int32 M = 0; M < Mips.Num(); ++M)
		{
			const int32 S = FMath::Max(Size >> M, 1);
			FTexture2DMipMap* Mip;
			if (M == 0) Mip = &PD->Mips[0];
			else { Mip = new FTexture2DMipMap(S, S, 1); PD->Mips.Add(Mip); }
			void* Dst = Mip->BulkData.Lock(LOCK_READ_WRITE);
			if (M > 0) Dst = Mip->BulkData.Realloc(int64(S) * S * 4);
			FMemory::Memcpy(Dst, Mips[M].GetData(), int64(S) * S * 4);
			Mip->BulkData.Unlock();
		}
		T->UpdateResource();
		return T;
	}
}

// ------------------------------------------------------------------ octahedral mapping

FVector2D DmPlanetOct::UV(const FVector3d& D)
{
	const double L1 = FMath::Max(FMath::Abs(D.X) + FMath::Abs(D.Y) + FMath::Abs(D.Z), 1e-30);
	FVector2D P(D.X / L1, D.Y / L1);
	if (D.Z < 0.0)
	{
		const double Px = P.X, Py = P.Y;
		P.X = (1.0 - FMath::Abs(Py)) * (Px >= 0.0 ? 1.0 : -1.0);
		P.Y = (1.0 - FMath::Abs(Px)) * (Py >= 0.0 ? 1.0 : -1.0);
	}
	return P * 0.5 + FVector2D(0.5, 0.5);
}

FVector3d DmPlanetOct::Dir(const FVector2D& UV)
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

// ------------------------------------------------------------------ texel cache

void FDmPlanetMaps::Init(const TSharedPtr<const FDmGeoLocator, ESPMode::ThreadSafe>& InLoc)
{
	const double T0 = FPlatformTime::Seconds();
	TSharedPtr<FCache, ESPMode::ThreadSafe> Nc = MakeShared<FCache, ESPMode::ThreadSafe>();
	FCache& C = *Nc;
	C.Loc = InLoc;
	const int32 S = StateSize;
	C.Direct.SetNumUninitialized(S * S);
	C.Warped.SetNumUninitialized(S * S);
	C.Wob.SetNumUninitialized(S * S);
	C.Nearest.SetNumUninitialized(S * S);
	for (TArray<FColor>& Cp : Cpu) Cp.Init(FColor(0, 0, 0, 0), S * S);
	const FDmGeoLocator& G = *InLoc;
	ParallelFor(S, [&](int32 Y)
	{
		for (int32 X = 0; X < S; ++X)
		{
			const int32 I = Y * S + X;
			const FVector3d Ue = DmPlanetOct::Dir(FVector2D((X + 0.5) / S, (Y + 0.5) / S));
			const FVector3d Dir = UEToSim(Ue);
			int32 Cc[3]; double Wt[3];
			G.Locate(Dir, Cc, Wt);
			for (int32 K = 0; K < 3; ++K) { C.Direct[I].C[K] = Cc[K]; C.Direct[I].W[K] = float(Wt[K]); }
			C.Nearest[I] = Wt[0] >= Wt[1] && Wt[0] >= Wt[2] ? Cc[0] : (Wt[1] >= Wt[2] ? Cc[1] : Cc[2]);
			// Climate colours are read at a domain-warped direction, so biome borders come out organic
			// instead of following the 1.9 km triangles of the simulation grid (lab SurfaceTerrain).
			const FVector3d Wp = Dir * 31.0;
			const FVector3d Wq = Dir * 140.0;
			const FVector3d Warp(Fbm(Wp + FVector3d(1.3, 7.1, 2.9), 3) + 0.35 * Fbm(Wq + FVector3d(4.2, 0.3, 5.5), 2),
				Fbm(Wp + FVector3d(8.4, 2.2, 6.6), 3) + 0.35 * Fbm(Wq + FVector3d(2.7, 9.9, 1.1), 2),
				Fbm(Wp + FVector3d(5.5, 3.3, 0.7), 3) + 0.35 * Fbm(Wq + FVector3d(6.1, 4.4, 8.8), 2));
			G.Locate(Nrm(Dir + Warp * 0.022), Cc, Wt);
			for (int32 K = 0; K < 3; ++K) { C.Warped[I].C[K] = Cc[K]; C.Warped[I].W[K] = float(Wt[K]); }
			C.Wob[I] = float(0.06 * Fbm(Dir * 25.0 + FVector3d(9.1), 3));
		}
	}, EParallelForFlags::BackgroundPriority);
	Cache = Nc;
	InitMs = (FPlatformTime::Seconds() - T0) * 1000.0;
}

// ------------------------------------------------------------------ state maps

void FDmPlanetMaps::CaptureInputs(const dm::World& W, int32 Overlay, FDmMapInputs& In)
{
	const int32 N = W.n;
	auto Fin = [](float V) { return FMath::IsFinite(V) ? V : 0.f; };
	auto Copy = [N, &Fin](const std::vector<float>& Src, TArray<float>& Dst)
	{
		Dst.SetNumUninitialized(N);
		for (int32 C = 0; C < N; ++C) Dst[C] = Fin(Src[size_t(C)]);
	};
	In.N = N; In.SeaLevel = float(W.seaLevel); In.Overlay = Overlay;
	In.Barren = W.phase == dm::Phase::Molten ? 1.f : W.phase == dm::Phase::Water ? 0.85f : W.phase == dm::Phase::Life ? 0.55f : 0.f;
	Copy(W.veg, In.Veg); Copy(W.moist, In.Moist); Copy(W.temp, In.Temp); Copy(W.ice, In.Ice); Copy(W.urban, In.Urban);
	Copy(W.water, In.Water); Copy(W.elev, In.Elev); Copy(W.wet, In.Wet); Copy(W.farm, In.Farm); Copy(W.scorch, In.Scorch);
	Copy(W.lava, In.Lava); Copy(W.fire, In.Fire); Copy(W.rad, In.Rad); Copy(W.hab, In.Hab);
	In.Biome.SetNumUninitialized(N); In.Road.SetNumUninitialized(N);
	for (int32 C = 0; C < N; ++C) { In.Biome[C] = W.biome[size_t(C)]; In.Road[C] = W.road[size_t(C)]; }

	// night lights per cell: settlements (population, tier, culture age) and roads (placeholder rule, kept).
	// On the game thread: Geo::ring uses scratch inside the Geo.
	In.Lights.Init(0.f, N);
	{
		std::vector<int32_t> Ring;
		for (const dm::Settlement& S : W.settlements)
		{
			if (S.dead || !W.valid(S.cell)) continue;
			const int32 Age = (S.culture >= 0 && S.culture < int32(W.cultures.size())) ? FMath::Clamp(W.cultures[size_t(S.culture)].age, 0, 6) : 0;
			const double Amp = FMath::Clamp(FMath::Sqrt(FMath::Max(S.pop, 0.0) / 4000.0), 0.1, 1.2) * kAgeGlow[Age];
			const double Sigma = 0.45 + 0.35 * S.tier;
			W.geo->ring(S.cell, 1 + FMath::Clamp(S.tier, 0, 4), Ring);
			for (int32_t C : Ring)
			{
				if (dm::state::isSea(W, C)) continue;
				const double D = W.geo->angle(C, S.cell) / FMath::Max(W.geo->step, 1e-9);
				In.Lights[C] += float(Amp * FMath::Exp(-(D * D) / (Sigma * Sigma)));
			}
		}
		const double RoadGlow = 0.04 * kAgeGlow[FMath::Clamp(W.age, 0, 6)];
		for (int32 C = 0; C < N; ++C) In.Lights[C] = FMath::Min(1.f, 0.6f * In.Lights[C] + float(RoadGlow * (In.Road[C] > 0 ? 1.0 : 0.0)) + 0.5f * In.Urban[C]);
	}

	// overlay: culture of each cell's owner, its devotion, the culture colours
	In.Culture.Reset(); In.Devotion.Reset(); In.CultureColor.Reset();
	if (Overlay == 1 || Overlay == 4)
	{
		In.Culture.SetNumUninitialized(N); In.Devotion.SetNumUninitialized(N);
		for (int32 C = 0; C < N; ++C)
		{
			const int32 Ow = W.owner[size_t(C)];
			int32 Cu = -1; float Dv = 0.f;
			if (Ow >= 0 && Ow < int32(W.settlements.size()) && !W.settlements[size_t(Ow)].dead)
			{
				Cu = W.settlements[size_t(Ow)].culture;
				Dv = float(FMath::Clamp(W.settlements[size_t(Ow)].devotion, 0.0, 1.0));
			}
			In.Culture[C] = Cu; In.Devotion[C] = Dv;
		}
		for (const dm::Culture& Cc : W.cultures) In.CultureColor.Add(FVector3f(float(Cc.color[0]), float(Cc.color[1]), float(Cc.color[2])));
	}
}

void FDmPlanetMaps::Compute(const FCache& Ca, const FDmMapInputs& In, uint32 Mask, FResult& Out)
{
	const double T0 = FPlatformTime::Seconds();
	Out.Mask = 0;
	if (!Ca.Loc.IsValid() || In.N != Ca.Loc->N || Ca.Direct.Num() == 0) return;
	const FDmGeoLocator& G = *Ca.Loc;
	const int32 N = G.N;
	const int32 S = StateSize;
	TArray<FColor> Mip0[NumMaps];

	if (Mask & 0xF)
	{
		// smoothed climate fields for the colour (two passes, as the lab)
		TArray<float> SVeg = In.Veg, SMoist = In.Moist, STemp = In.Temp, SMarsh, SBarren, SIce = In.Ice, SUrban, Tmp;
		SMarsh.SetNumUninitialized(N); SBarren.SetNumUninitialized(N);
		for (int32 C = 0; C < N; ++C)
		{
			SMarsh[C] = In.Biome[C] == BWetland ? 1.f : 0.f;
			SBarren[C] = In.Biome[C] == BBarren ? 1.f : 0.f;
		}
		SmoothField(G, SVeg, Tmp, 2); SmoothField(G, SMoist, Tmp, 2); SmoothField(G, STemp, Tmp, 2); SmoothField(G, SMarsh, Tmp, 2); SmoothField(G, SBarren, Tmp, 1);
		// ice sheets read at the warped direction (organic edges); built-up ground smoothed once so a town is a
		// rounded patch around its centre instead of the triangle fan of its cells (the material breaks it up)
		SmoothField(G, SIce, Tmp, 1);
		SUrban.SetNumUninitialized(N);
		for (int32 C = 0; C < N; ++C)
		{
			const int32 Deg = G.NbrN[C];
			if (!Deg) { SUrban[C] = In.Urban[C]; continue; }
			double Sm = 0; for (int32 K = 0; K < Deg; ++K) Sm += In.Urban[G.Nbr[6 * C + K]];
			SUrban[C] = FMath::Max(0.65f * In.Urban[C] + 0.35f * float(Sm / Deg), 0.6f * float(Sm / Deg));
		}
		for (int32 M = 0; M < 4; ++M) Mip0[M].SetNumUninitialized(S * S);
		const float SeaLevel = In.SeaLevel;
		ParallelFor(S, [&](int32 Y)
		{
			for (int32 X = 0; X < S; ++X)
			{
				const int32 I = Y * S + X;
				const FCache::FTexelLoc& D = Ca.Direct[I];
				const FCache::FTexelLoc& Wl = Ca.Warped[I];
				auto La = [&](const TArray<float>& F) { return double(D.W[0]) * F[D.C[0]] + double(D.W[1]) * F[D.C[1]] + double(D.W[2]) * F[D.C[2]]; };
				auto Lw = [&](const TArray<float>& F) { return double(Wl.W[0]) * F[Wl.C[0]] + double(Wl.W[1]) * F[Wl.C[1]] + double(Wl.W[2]) * F[Wl.C[2]]; };
				const double Tm = Lw(STemp), Mo = Lw(SMoist), Vg = Lw(SVeg), Marsh = Lw(SMarsh), Barren = La(SBarren);
				const double Wob_ = Ca.Wob[I];
				double Wa = 0.0;
				for (int32 K = 0; K < 3; ++K)
				{
					const int32 C = D.C[K];
					if (In.Elev[C] >= SeaLevel) Wa += D.W[K] * FMath::Min(In.Water[C], 0.5f);
				}
				const double WetSim = La(In.Wet);

				// ---- macro albedo (sRGB), lab palette
				const FVector3f Sand(0.66f, 0.56f, 0.40f), Steppe(0.50f, 0.45f, 0.32f), Grass(0.36f, 0.42f, 0.22f),
					Forest(0.13f, 0.21f, 0.10f), Jungle(0.08f, 0.17f, 0.07f), Taiga(0.13f, 0.19f, 0.13f),
					Tundra(0.42f, 0.40f, 0.34f), MarshC(0.19f, 0.25f, 0.15f), Scrub(0.40f, 0.38f, 0.26f), BarrenC(0.37f, 0.34f, 0.31f);
				FVector3f Bare = Lerp3(Sand, Steppe, Mo * 1.8 + Wob_);
				Bare = Lerp3(Bare, Scrub, Mo * 2.0 - 0.6);
				// (integration) a lifeless world is dark basalt and ash (docs/concept/genesis 2-4), not desert sand
				Bare = Lerp3(Bare, FVector3f(0.15f, 0.14f, 0.13f) + FVector3f(0.05f, 0.04f, 0.03f) * float(Wob_ + 0.5), In.Barren);
				FVector3f Green = Lerp3(Grass, Forest, Vg * 1.4 - 0.35 + Wob_);
				Green = Lerp3(Green, Jungle, Sat((Tm - 20.0) / 8.0) * Sat(Mo * 1.5 - 0.5));
				Green = Lerp3(Green, Taiga, (4.0 - Tm) / 8.0);
				FVector3f Col = Lerp3(Bare, Green, Vg * 1.6 + 0.15 * Mo + Wob_);
				// drought: vegetated land that lost its moisture yellows at once (before life catches up)
				const double Dry = Sat((0.32 - Mo) / 0.22) * Sat(Vg * 2.0);
				Col = Lerp3(Col, Col * FVector3f(1.35f, 1.18f, 0.62f) + FVector3f(0.03f, 0.02f, 0.0f), 0.65 * Dry);
				const double Wet = 0.3 * Sat(Wa * 1.5) + 0.7 * Marsh;   // lakes themselves are drawn as water
				Col = Lerp3(Col, MarshC, FMath::Min(Wet, 0.7));
				Col = Lerp3(Col, Tundra, (-4.0 - Tm) / 8.0 + Wob_);
				Col = Lerp3(Col, BarrenC, 0.8 * Barren);
				Col *= float(1.0 - 0.22 * Sat(WetSim));   // rain-soaked ground is darker
				const double ForestD = Sat(Vg * 1.6 - 0.4 + Wob_) * Sat((Tm + 6.0) / 10.0) * (1.0 - Sat(Wet)) * (1.0 - 0.7 * Dry);
				Mip0[0][I] = FColor(Q8(Col.X), Q8(Col.Y), Q8(Col.Z), Q8(ForestD));

				// ---- climate
				const double Arid = Sat(1.0 - Mo * 1.6) * Sat(1.0 - Vg * 2.0);
				const double Ice = FMath::Max(Lw(SIce), 0.85 * La(In.Ice));
				// G = standing wetness (marsh, lake shores) only: rain-soaked ground is already darker in map 0
				Mip0[1][I] = FColor(Q8((Tm + 50.0) / 90.0), Q8(Sat(Wet)), Q8(Arid), Q8(Ice));

				// ---- people
				double Road = 0.0;
				for (int32 K = 0; K < 3; ++K) Road += D.W[K] * (In.Road[D.C[K]] > 0 ? 1.0 : 0.0);
				// farmland follows the warped direction (organic field edges instead of the cell hexagons)
				Mip0[2][I] = FColor(Q8(0.5 * (Lw(In.Farm) + La(In.Farm))), Q8(La(SUrban)), Q8(Road), Q8(La(In.Lights)));

				// ---- damage
				Mip0[3][I] = FColor(Q8(La(In.Scorch)), Q8(Sat(La(In.Lava) / 0.6)), Q8(La(In.Fire)), Q8(La(In.Rad)));
			}
		}, EParallelForFlags::BackgroundPriority);
		Out.Mask |= 0xF;
	}

	if (Mask & 0x10)
	{
		TArray<FColor>& O = Mip0[4];
		O.SetNumUninitialized(S * S);
		const int32 Mode = In.Overlay;
		if (Mode <= 0) { for (FColor& C : O) C = FColor(0, 0, 0, 0); }
		else
		{
			const bool bCult = (Mode == 1 || Mode == 4) && In.Culture.Num() == N;
			TArray<int32> Cult;
			if (bCult) { Cult.SetNumUninitialized(S * S); for (int32 I = 0; I < S * S; ++I) Cult[I] = In.Culture[Ca.Nearest[I]]; }
			ParallelFor(S, [&](int32 Y)
			{
				for (int32 X = 0; X < S; ++X)
				{
					const int32 I = Y * S + X;
					const FCache::FTexelLoc& D = Ca.Direct[I];
					auto La = [&](const TArray<float>& F) { return double(D.W[0]) * F[D.C[0]] + double(D.W[1]) * F[D.C[1]] + double(D.W[2]) * F[D.C[2]]; };
					const int32 Nc = Ca.Nearest[I];
					const bool bSea = In.Elev[Nc] < In.SeaLevel;
					FVector3f Col(0.f); double A = 0.0;
					if (bCult)
					{
						const int32 Cu = Cult[I];
						bool bBorder = false;
						const int32 Off[4][2] = { { 2, 0 }, { -2, 0 }, { 0, 2 }, { 0, -2 } };
						for (const auto& Of : Off)
						{
							const int32 Xx = FMath::Clamp(X + Of[0], 0, S - 1), Yy = FMath::Clamp(Y + Of[1], 0, S - 1);
							if (Cult[Yy * S + Xx] != Cu) bBorder = true;
						}
						if (Cu >= 0 && Cu < In.CultureColor.Num())
						{
							Col = In.CultureColor[Cu];
							A = bBorder ? 0.95 : 0.45;
							if (Mode == 4)
							{
								const FVector3f Stops[3] = { FVector3f(0.25f, 0.22f, 0.45f), FVector3f(0.75f, 0.55f, 0.85f), FVector3f(1.0f, 0.82f, 0.30f) };
								Col = Ramp(Stops, 3, In.Devotion[Nc]);
								A = bBorder ? 0.95 : 0.7;
							}
							if (bBorder) Col *= 0.55f;
						}
						else if (bBorder && !bSea) { Col = FVector3f(0.1f); A = 0.6; }
					}
					else if (Mode == 2)
					{
						const FVector3f Stops[5] = { FVector3f(0.15f, 0.25f, 0.75f), FVector3f(0.35f, 0.70f, 0.90f), FVector3f(0.95f, 0.92f, 0.55f), FVector3f(0.95f, 0.55f, 0.20f), FVector3f(0.75f, 0.12f, 0.10f) };
						Col = Ramp(Stops, 5, (La(In.Temp) + 25.0) / 60.0);
						A = bSea ? 0.35 : 0.8;
					}
					else if (Mode == 3)
					{
						const FVector3f Stops[4] = { FVector3f(0.62f, 0.45f, 0.25f), FVector3f(0.85f, 0.80f, 0.40f), FVector3f(0.30f, 0.70f, 0.35f), FVector3f(0.15f, 0.35f, 0.85f) };
						Col = Ramp(Stops, 4, La(In.Moist));
						A = bSea ? 0.25 : 0.8;
					}
					else if (Mode == 5)
					{
						const FVector3f Stops[3] = { FVector3f(0.45f, 0.12f, 0.10f), FVector3f(0.95f, 0.80f, 0.30f), FVector3f(0.20f, 0.75f, 0.30f) };
						Col = Ramp(Stops, 3, La(In.Hab));
						A = bSea ? 0.0 : 0.8;
					}
					O[I] = FColor(Q8(Col.X), Q8(Col.Y), Q8(Col.Z), Q8(A));
				}
			}, EParallelForFlags::BackgroundPriority);
		}
		Out.Mask |= 0x10;
	}

	for (int32 M = 0; M < NumMaps; ++M)
		if (Out.Mask & (1u << M)) MakeMips(Mip0[M], S, Out.Mips[M]);
	Out.ComputeMs = (FPlatformTime::Seconds() - T0) * 1000.0;
}

void FDmPlanetMaps::ApplyResult(UObject* Outer, FResult& R)
{
	const double T0 = FPlatformTime::Seconds();
	if (Textures.Num() != NumMaps) Textures.Init(nullptr, NumMaps);
	for (int32 M = 0; M < NumMaps; ++M)
	{
		if (!(R.Mask & (1u << M)) || R.Mips[M].Num() == 0) continue;
		Cpu[M] = R.Mips[M][0];
		UploadMips(Outer, M, R.Mips[M]);
	}
	LastComputeMs = R.ComputeMs;
	LastUploadMs = (FPlatformTime::Seconds() - T0) * 1000.0;
}

void FDmPlanetMaps::ComputeAll(UObject* Outer, const dm::World& W, int32 Overlay)
{
	if (!IsReady()) return;
	FDmMapInputs In;
	CaptureInputs(W, Overlay, In);
	FResult R;
	Compute(*Cache, In, 0x1F, R);
	ApplyResult(Outer, R);
}

void FDmPlanetMaps::ComputeSnapshot(const dm::World& W, const dm::Snapshot& Sn)
{
	if (!IsReady() || int32(Sn.biome.size()) != Cache->Loc->N) return;
	const FCache& Ca = *Cache;
	static const FVector3f BiomeCol[16] = {
		FVector3f(0.20f, 0.24f, 0.20f), FVector3f(0.55f, 0.52f, 0.40f), FVector3f(0.66f, 0.56f, 0.40f), FVector3f(0.68f, 0.56f, 0.38f),
		FVector3f(0.52f, 0.47f, 0.28f), FVector3f(0.36f, 0.42f, 0.22f), FVector3f(0.13f, 0.21f, 0.10f), FVector3f(0.08f, 0.17f, 0.07f),
		FVector3f(0.13f, 0.19f, 0.13f), FVector3f(0.42f, 0.40f, 0.34f), FVector3f(0.85f, 0.87f, 0.90f), FVector3f(0.38f, 0.35f, 0.31f),
		FVector3f(0.90f, 0.91f, 0.93f), FVector3f(0.19f, 0.25f, 0.15f), FVector3f(0.37f, 0.34f, 0.31f), FVector3f(0.88f, 0.90f, 0.93f) };
	const int32 S = StateSize;
	ParallelFor(S, [&](int32 Y)
	{
		for (int32 X = 0; X < S; ++X)
		{
			const int32 I = Y * S + X;
			const FCache::FTexelLoc& D = Ca.Warped[I];
			FVector3f Col(0.f);
			double Forest = 0, Urb = 0, IceV = 0;
			for (int32 K = 0; K < 3; ++K)
			{
				const int32 C = D.C[K];
				const uint8 B = FMath::Min<uint8>(Sn.biome[size_t(C)], 15);
				Col += BiomeCol[B] * D.W[K];
				Forest += D.W[K] * ((B == BForest || B == BRainforest || B == BTaiga) ? 1.0 : 0.0);
				IceV += D.W[K] * ((B == BIce || B == BSeaIce || B == BSnowpeak) ? 1.0 : 0.0);
				Urb += D.W[K] * Sn.urban[size_t(C)] / 255.0;
			}
			const int32 Cu = Sn.owner[size_t(Ca.Nearest[I])];
			if (Cu >= 0 && Cu < int32(W.cultures.size()))
			{
				const auto& Cc = W.cultures[size_t(Cu)].color;
				Col = Lerp3(Col, FVector3f(float(Cc[0]), float(Cc[1]), float(Cc[2])), 0.35);
			}
			Cpu[0][I] = FColor(Q8(Col.X), Q8(Col.Y), Q8(Col.Z), Q8(Forest));
			// a temperate 15 C (the old 0.5 code read as -5 C and drew a snow line over every snapshot)
			Cpu[1][I] = FColor(Q8(65.0 / 90.0), 0, 0, Q8(IceV));
			Cpu[2][I] = FColor(0, Q8(Urb), 0, Q8(Urb));
			Cpu[3][I] = FColor(0, 0, 0, 0);
		}
	}, EParallelForFlags::BackgroundPriority);
}

void FDmPlanetMaps::UploadMips(UObject* Outer, int32 M, TArray<TArray<FColor>>& Mips)
{
	static const TCHAR* Names[NumMaps] = { TEXT("DmPlanetAlbedo"), TEXT("DmPlanetClimate"), TEXT("DmPlanetPeople"), TEXT("DmPlanetDamage"), TEXT("DmPlanetOverlay") };
	if (!Textures[M])
	{
		Textures[M] = NewTexture(Outer, StateSize, Mips, Names[M]);
		return;
	}
	UTexture2D* T = Textures[M];
	for (int32 L = 0; L < Mips.Num(); ++L)
	{
		const int32 S = FMath::Max(StateSize >> L, 1);
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, uint32(S), uint32(S));
		// hand the buffer to the render thread (no copy): it is freed when the upload is done
		TArray<FColor>* Owned = new TArray<FColor>(MoveTemp(Mips[L]));
		T->UpdateTextureRegions(L, 1, Region, uint32(S * 4), 4, reinterpret_cast<uint8*>(Owned->GetData()), [Owned](uint8*, const FUpdateTextureRegion2D* Regions)
		{
			delete Owned;
			delete Regions;
		});
	}
}

void FDmPlanetMaps::Upload(UObject* Outer, uint32 Mask)
{
	const double T0 = FPlatformTime::Seconds();
	if (Textures.Num() != NumMaps) Textures.Init(nullptr, NumMaps);
	for (int32 M = 0; M < NumMaps; ++M)
	{
		if (!(Mask & (1u << M))) continue;
		TArray<TArray<FColor>> Mips;
		MakeMips(Cpu[M], StateSize, Mips);
		UploadMips(Outer, M, Mips);
	}
	LastUploadMs = (FPlatformTime::Seconds() - T0) * 1000.0;
}

float FDmPlanetMaps::ForestAt(const FVector3d& UEDir) const
{
	if (Cpu[0].Num() != StateSize * StateSize) return 0.f;
	const FVector2D Uv = DmPlanetOct::UV(Nrm(UEDir));
	const int32 X = FMath::Clamp(int32(Uv.X * StateSize), 0, StateSize - 1), Y = FMath::Clamp(int32(Uv.Y * StateSize), 0, StateSize - 1);
	return Cpu[0][Y * StateSize + X].A / 255.f;
}


uint32 FDmPlanetMaps::RoadHash(const dm::World& W)
{
	uint32 H = 2166136261u;
	for (int32 C = 0; C < W.n; ++C) if (W.road[size_t(C)]) { H = (H ^ uint32(C)) * 16777619u; H = (H ^ W.road[size_t(C)]) * 16777619u; }
	return H;
}

// ------------------------------------------------------------------ river + road map

uint8 FDmPlanetMaps::EncodeSigned(double D)
{
	// linear within +-125 m (0.125 .. 0.875, 1.3 m steps: bilinear filtering stays exact near a line, and a
	// texel next to a line is at most ~123 m from it), compressed beyond, out to +-400 m (riparian bands).
	// Decoded in Scripts/planet/land.hlsl (DecodeSigned).
	const double A = FMath::Abs(D);
	const double E = A <= 125.0 ? 0.375 * A / 125.0 : 0.375 + 0.125 * FMath::Min((A - 125.0) / 275.0, 1.0);
	return uint8(FMath::Clamp(128 + FMath::RoundToInt((D < 0.0 ? -E : E) * 254.0), 0, 255));   // 128 = on the line
}

void FDmPlanetMaps::CaptureRoads(const dm::World& W, TArray<FDmRoadSeg>& Out)
{
	Out.Reset();
	if (!W.geo) return;
	const dm::Geo& G = *W.geo;
	auto IsRoad = [&W](int32 C) { return W.road[size_t(C)] > 0 && !dm::state::isSea(W, C); };
	// road nodes at the cell centres, smoothed toward their road neighbours (two Laplacian passes) so the
	// network curves instead of turning in the grid's 60-degree steps
	TMap<int32, FVector3d> P;
	for (int32 C = 0; C < W.n; ++C)
		if (IsRoad(C)) P.Add(C, Nrm(FVector3d(G.pos[size_t(3 * C)], G.pos[size_t(3 * C + 1)], G.pos[size_t(3 * C + 2)])));
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		TMap<int32, FVector3d> Q = P;
		for (const TPair<int32, FVector3d>& It : P)
		{
			const int32 C = It.Key;
			FVector3d Sum(0.0); int32 Cnt = 0;
			for (int32 K = 0; K < G.nbrN[size_t(C)]; ++K)
			{
				const int32 M = G.nbr[size_t(6 * C + K)];
				if (const FVector3d* Pm = P.Find(M)) { Sum += *Pm; ++Cnt; }
			}
			if (Cnt == 0 || Cnt > 3) continue;   // ends and busy junctions stay put
			Q[C] = Nrm(It.Value * 0.5 + Sum * (0.5 / Cnt));
		}
		P = MoveTemp(Q);
	}
	for (const TPair<int32, FVector3d>& It : P)
	{
		const int32 C = It.Key;
		for (int32 K = 0; K < G.nbrN[size_t(C)]; ++K)
		{
			const int32 M = G.nbr[size_t(6 * C + K)];
			if (M <= C) continue;
			const FVector3d* Pm = P.Find(M);
			if (!Pm) continue;
			Out.Add({ SimToUE(It.Value), SimToUE(*Pm), float(FMath::Min(W.road[size_t(C)], W.road[size_t(M)])) });
		}
	}
}

void FDmPlanetMaps::BuildRiverMipData(const FDmPlanetTerrain& T, const TArray<FDmRoadSeg>& Roads, TArray<TArray<FColor>>& Mips, int32& OutNear)
{
	const int32 Size = RiverSize;
	const double RadM = FDmPlanetTerrain::DatumM;
	const double RangeM = RiverRangeM;
	const double TexelM = FMath::Sqrt(4.0 * PI * RadM * RadM / (double(Size) * Size));
	TArray<float> Dist; Dist.Init(float(RangeM), Size * Size);
	TArray<float> SDist; SDist.Init(float(RangeM), Size * Size);    // signed (side of the nearest segment)
	TArray<float> HalfW; HalfW.Init(0.f, Size * Size);
	TArray<float> Cov; Cov.Init(0.f, Size * Size);
	TArray<float> RDist; RDist.Init(float(RangeM), Size * Size);
	TArray<float> RSDist; RSDist.Init(float(RangeM), Size * Size);
	TArray<float> RCov; RCov.Init(0.f, Size * Size);
	const int32 R = FMath::CeilToInt(RangeM / TexelM) + 1;
	auto Splat = [&](const FVector3d& A, const FVector3d& B, double Hw, TArray<float>& D, TArray<float>& SD, TArray<float>* Hws, TArray<float>& Cv, double CovGain)
	{
		const FVector2D Uv = DmPlanetOct::UV(Nrm(A + B));
		const int32 Cx = FMath::FloorToInt(Uv.X * Size), Cy = FMath::FloorToInt(Uv.Y * Size);
		const double SegM = FVector3d::Distance(A, B) * RadM;
		const int32 Rr = R + FMath::CeilToInt(0.5 * SegM / TexelM);
		if (Cx >= 0 && Cy >= 0 && Cx < Size && Cy < Size) Cv[Cy * Size + Cx] += float(CovGain * 2.0 * Hw * SegM / (TexelM * TexelM));
		const FVector3d AB = B - A;
		for (int32 Y = FMath::Max(Cy - Rr, 0); Y <= FMath::Min(Cy + Rr, Size - 1); ++Y)
			for (int32 X = FMath::Max(Cx - Rr, 0); X <= FMath::Min(Cx + Rr, Size - 1); ++X)
			{
				const FVector3d Dd = DmPlanetOct::Dir(FVector2D((X + 0.5) / Size, (Y + 0.5) / Size));
				const double Tt = FMath::Clamp(((Dd - A) | AB) / FMath::Max(AB | AB, 1e-30), 0.0, 1.0);
				const FVector3d Cp = A + AB * Tt;
				const double Dm = (Cp - Dd).Length() * RadM;
				const int32 I = Y * Size + X;
				if (Dm < D[I])
				{
					D[I] = float(Dm);
					// signed across the segment's line (also past its ends, so the outer side of a bend keeps its
					// sign): linear across a straight reach, so bilinear filtering puts the zero line exactly on the
					// river or road (an unsigned field filled the V between texels and broke thin lines into beads)
					SD[I] = (((AB ^ (Dd - Cp)) | Dd) < 0.0) ? -float(Dm) : float(Dm);
					if (Hws) (*Hws)[I] = float(Hw);
				}
			}
	};
	for (int32 Si : T.RSeg)
	{
		const FVector3d A = SimToUE(T.RPts[Si].P), B = SimToUE(T.RPts[Si + 1].P);
		Splat(A, B, 0.5 * (T.RPts[Si].HalfW + T.RPts[Si + 1].HalfW), Dist, SDist, &HalfW, Cov, 8.0);   // coverage x8: readable from orbit
	}
	for (const FDmRoadSeg& Rs : Roads)
	{
		// cell-to-cell road: split into ~200 m pieces so the splat radius stays small
		const double Len = FVector3d::Distance(Rs.A, Rs.B) * RadM;
		const int32 NP = FMath::Clamp(FMath::CeilToInt(Len / 200.0), 1, 64);
		for (int32 P = 0; P < NP; ++P)
		{
			const FVector3d A = Nrm(FMath::Lerp(Rs.A, Rs.B, double(P) / NP)), B = Nrm(FMath::Lerp(Rs.A, Rs.B, double(P + 1) / NP));
			Splat(A, B, 3.0 + 2.0 * Rs.Level, RDist, RSDist, nullptr, RCov, 4.0);
		}
	}
	Mips.Reset();
	TArray<FColor>& M0 = Mips.AddDefaulted_GetRef();
	M0.SetNumUninitialized(Size * Size);
	int32 Near = 0;
	for (int32 I = 0; I < Size * Size; ++I)
	{
		M0[I] = FColor(EncodeSigned(SDist[I]),
			uint8(FMath::Clamp(FMath::RoundToInt(HalfW[I] / 64.0 * 255.0), 0, 255)),
			uint8(FMath::Clamp(FMath::RoundToInt(Cov[I] * 255.0), 0, 255)),
			EncodeSigned(RSDist[I]));
		if (Dist[I] < RangeM) ++Near;
	}
	OutNear = Near;
	TArray<float> CovM = Cov, RCovM = RCov;
	for (float& V : CovM) V = FMath::Min(V, 1.f);
	for (float& V : RCovM) V = FMath::Min(V, 1.f);
	for (int32 S = Size / 2; S >= 1; S /= 2)
	{
		TArray<float> Next; Next.SetNumUninitialized(S * S);
		TArray<float> NextR; NextR.SetNumUninitialized(S * S);
		const int32 P = S * 2;
		for (int32 Y = 0; Y < S; ++Y)
			for (int32 X = 0; X < S; ++X)
			{
				Next[Y * S + X] = 0.25f * (CovM[(2 * Y) * P + 2 * X] + CovM[(2 * Y) * P + 2 * X + 1] + CovM[(2 * Y + 1) * P + 2 * X] + CovM[(2 * Y + 1) * P + 2 * X + 1]);
				NextR[Y * S + X] = 0.25f * (RCovM[(2 * Y) * P + 2 * X] + RCovM[(2 * Y) * P + 2 * X + 1] + RCovM[(2 * Y + 1) * P + 2 * X] + RCovM[(2 * Y + 1) * P + 2 * X + 1]);
			}
		CovM = MoveTemp(Next);
		RCovM = MoveTemp(NextR);
		TArray<FColor>& M = Mips.AddDefaulted_GetRef();
		M.SetNumUninitialized(S * S);
		for (int32 I = 0; I < S * S; ++I) M[I] = FColor(255, 0, uint8(FMath::Clamp(FMath::RoundToInt(CovM[I] * 255.0), 0, 255)), uint8(FMath::Clamp(FMath::RoundToInt(RCovM[I] * 255.0), 0, 255)));
	}
}

UTexture2D* FDmPlanetMaps::MakeRiverTexture(UObject* Outer, const TArray<TArray<FColor>>& Mips)
{
	return NewTexture(Outer, RiverSize, Mips, TEXT("DmPlanetRiverMap"));
}

bool FDmPlanetMaps::UpdateRiverTexture(UTexture2D* T, TArray<TArray<FColor>>& Mips)
{
	// (integration, performance) a new 4096^2 texture per river change cost the game thread 30 ms plus 5-17 ms
	// destroying the old one (hitch dumps at 8x): the same in-place, buffer-handover upload as the state maps
	if (!T || T->GetSizeX() != RiverSize || !T->GetResource() || Mips.Num() == 0 || T->GetNumMips() < Mips.Num()) return false;
	for (int32 L = 0; L < Mips.Num(); ++L)
	{
		const int32 S = FMath::Max(RiverSize >> L, 1);
		if (Mips[L].Num() != S * S) return false;
	}
	for (int32 L = 0; L < Mips.Num(); ++L)
	{
		const int32 S = FMath::Max(RiverSize >> L, 1);
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, uint32(S), uint32(S));
		TArray<FColor>* Owned = new TArray<FColor>(MoveTemp(Mips[L]));
		T->UpdateTextureRegions(L, 1, Region, uint32(S * 4), 4, reinterpret_cast<uint8*>(Owned->GetData()), [Owned](uint8*, const FUpdateTextureRegion2D* Regions)
		{
			delete Owned;
			delete Regions;
		});
	}
	return true;
}
