#include "LabPlanet.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Math/UnrealMathUtility.h"

namespace
{
	// Biome ids follow DM.BIOME in js/sim/state.js.
	enum : uint8 { SEA, SHALLOWS, BEACH, DESERT, SAVANNA, GRASSLAND, FOREST, RAINFOREST, TAIGA, TUNDRA, ICE, ROCK, SNOWPEAK, WETLAND, BARREN, SEAICE, NBIOME };

	// Satellite-style albedo, sRGB 0..1.
	const FVector3f BiomeSRGB[NBIOME] = {
		{0.20f, 0.24f, 0.20f}, // SEA (seabed)
		{0.62f, 0.60f, 0.48f}, // SHALLOWS
		{0.80f, 0.74f, 0.56f}, // BEACH
		{0.82f, 0.66f, 0.44f}, // DESERT
		{0.60f, 0.55f, 0.32f}, // SAVANNA
		{0.40f, 0.47f, 0.24f}, // GRASSLAND
		{0.17f, 0.29f, 0.13f}, // FOREST
		{0.10f, 0.23f, 0.09f}, // RAINFOREST
		{0.18f, 0.25f, 0.17f}, // TAIGA
		{0.50f, 0.47f, 0.40f}, // TUNDRA
		{0.93f, 0.95f, 0.98f}, // ICE
		{0.42f, 0.38f, 0.34f}, // ROCK
		{0.96f, 0.96f, 0.98f}, // SNOWPEAK
		{0.22f, 0.30f, 0.19f}, // WETLAND
		{0.55f, 0.50f, 0.43f}, // BARREN
		{0.90f, 0.93f, 0.97f}, // SEAICE
	};

	float Fbm(const FVector& P, int32 Octaves)
	{
		float Sum = 0.f, Amp = 0.5f, Norm = 0.f;
		FVector Q = P;
		for (int32 O = 0; O < Octaves; ++O)
		{
			Sum += Amp * FMath::PerlinNoise3D(Q);
			Norm += Amp;
			Amp *= 0.5f;
			Q *= 2.03;
		}
		return Sum / FMath::Max(Norm, 1e-4f);
	}

	struct FSrc { int32 A, B, C; float WA, WB, WC; };
}

bool FLabPlanetData::Load(const FString& Path)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() < 20) return false;
	const uint8* P = Bytes.GetData();
	int64 Off = 0;
	auto Rd = [&](void* Dst, int64 Size) { if (Off + Size > Bytes.Num()) return false; FMemory::Memcpy(Dst, P + Off, Size); Off += Size; return true; };
	if (!Rd(&N, 4) || !Rd(&NTri, 4) || !Rd(&R, 4) || !Rd(&SeaLevel, 4) || !Rd(&RiverT, 4)) return false;
	if (N <= 0 || NTri <= 0) return false;
	Pos.SetNumUninitialized(N);
	Tris.SetNumUninitialized(NTri * 3);
	if (!Rd(Pos.GetData(), int64(N) * 12) || !Rd(Tris.GetData(), int64(NTri) * 12)) return false;
	for (TArray<float>* F : { &Elev, &Temp, &Moist, &Veg, &Flow, &Ice, &Hab, &Slope, &Water })
	{
		F->SetNumUninitialized(N);
		if (!Rd(F->GetData(), int64(N) * 4)) return false;
	}
	Biome.SetNumUninitialized(N);
	return Rd(Biome.GetData(), N);
}

ALabPlanet::ALabPlanet()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Land = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Land"));
	Land->SetupAttachment(RootComponent);
	Ocean = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Ocean"));
	Ocean->SetupAttachment(RootComponent);
	for (UProceduralMeshComponent* M : { Land.Get(), Ocean.Get() })
	{
		M->bUseAsyncCooking = true;
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetCastShadow(true);
	}
}

double ALabPlanet::SeaRadiusUU() const
{
	return RadiusUU + double(D.SeaLevel) * KmPerElevUnit * 1.0e5;
}

bool ALabPlanet::Build(const FString& DataPath, UMaterialInterface* LandMat, UMaterialInterface* OceanMat)
{
	if (!D.Load(DataPath))
	{
		UE_LOG(LogTemp, Error, TEXT("LabPlanet: cannot load %s"), *DataPath);
		return false;
	}
	const double UUPerElev = KmPerElevUnit * 1.0e5;
	{
		// One pass of neighbour smoothing on elevation (keeps the sign at the coast mostly intact).
		TArray<float> Sum; Sum.SetNumZeroed(D.N);
		TArray<int32> Cnt; Cnt.SetNumZeroed(D.N);
		for (int32 T = 0; T < D.NTri; ++T)
			for (int32 E0 = 0; E0 < 3; ++E0)
			{
				const int32 A = D.Tris[3 * T + E0], B = D.Tris[3 * T + (E0 + 1) % 3];
				Sum[A] += D.Elev[B]; Cnt[A]++;
				Sum[B] += D.Elev[A]; Cnt[B]++;
			}
		TArray<float> Sm = D.Elev;
		for (int32 C = 0; C < D.N; ++C) if (Cnt[C]) Sm[C] = 0.5f * D.Elev[C] + 0.5f * Sum[C] / Cnt[C];
		D.Elev = MoveTemp(Sm);
	}

	// Build a subdivided copy of the simulation icosphere: k*k sub-triangles per cell triangle, shared edge vertices.
	auto Subdivide = [this](int32 K, TArray<FSrc>& S, TArray<int32>& Idx)
	{
		S.Reset(); Idx.Reset();
		S.Reserve(D.N * K * K);
		for (int32 C = 0; C < D.N; ++C) S.Add({ C, C, C, 1.f, 0.f, 0.f });
		TMap<uint64, int32> EdgeBase;
		EdgeBase.Reserve(D.NTri * 3 / 2);
		auto EdgeVert = [&](int32 A, int32 B, int32 T) -> int32
		{
			const int32 Lo = FMath::Min(A, B), Hi = FMath::Max(A, B);
			const uint64 Key = (uint64(Lo) << 32) | uint64(Hi);
			int32* Found = EdgeBase.Find(Key);
			int32 Base;
			if (Found) Base = *Found;
			else
			{
				Base = S.Num();
				for (int32 Si = 1; Si < K; ++Si) { const float U = float(Si) / K; S.Add({ Lo, Hi, Lo, 1.f - U, U, 0.f }); }
				EdgeBase.Add(Key, Base);
			}
			const int32 Si = (A == Lo) ? T : K - T;
			return Base + Si - 1;
		};
		TArray<int32> Grid;
		Grid.SetNum((K + 1) * (K + 1));
		for (int32 T = 0; T < D.NTri; ++T)
		{
			const int32 A = D.Tris[3 * T], B = D.Tris[3 * T + 1], C = D.Tris[3 * T + 2];
			for (int32 I = 0; I <= K; ++I)
				for (int32 J = 0; I + J <= K; ++J)
				{
					int32 V;
					if (I == 0 && J == 0) V = A;
					else if (I == K) V = B;
					else if (J == K) V = C;
					else if (J == 0) V = EdgeVert(A, B, I);
					else if (I == 0) V = EdgeVert(A, C, J);
					else if (I + J == K) V = EdgeVert(B, C, J);
					else { V = S.Num(); S.Add({ A, B, C, float(K - I - J) / K, float(I) / K, float(J) / K }); }
					Grid[I * (K + 1) + J] = V;
				}
			auto G = [&](int32 I, int32 J) { return Grid[I * (K + 1) + J]; };
			for (int32 I = 0; I < K; ++I)
				for (int32 J = 0; I + J < K; ++J)
				{
					Idx.Append({ G(I, J), G(I + 1, J), G(I, J + 1) });
					if (I + J < K - 1) Idx.Append({ G(I + 1, J), G(I + 1, J + 1), G(I, J + 1) });
				}
		}
	};

	auto Lerp3 = [this](const TArray<float>& F, const FSrc& Sv) { return Sv.WA * F[Sv.A] + Sv.WB * F[Sv.B] + Sv.WC * F[Sv.C]; };
	auto DirOf = [this](const FSrc& Sv)
	{
		const FVector3f P = D.Pos[Sv.A] * Sv.WA + D.Pos[Sv.B] * Sv.WB + D.Pos[Sv.C] * Sv.WC;
		const FVector V(P.X, P.Z, P.Y); // sim +Y is north; Unreal +Z is up
		return V.GetSafeNormal();
	};
	auto Normals = [](const TArray<FVector>& V, const TArray<int32>& Idx)
	{
		TArray<FVector> Nrm;
		Nrm.SetNumZeroed(V.Num());
		for (int32 T = 0; T < Idx.Num(); T += 3)
		{
			const FVector& A = V[Idx[T]]; const FVector& B = V[Idx[T + 1]]; const FVector& C = V[Idx[T + 2]];
			FVector Fn = FVector::CrossProduct(B - A, C - A);
			if (FVector::DotProduct(Fn, A + B + C) < 0) Fn = -Fn;
			Nrm[Idx[T]] += Fn; Nrm[Idx[T + 1]] += Fn; Nrm[Idx[T + 2]] += Fn;
		}
		for (int32 I = 0; I < V.Num(); ++I) Nrm[I] = Nrm[I].GetSafeNormal(1e-12, V[I].GetSafeNormal());
		return Nrm;
	};

	// ---- land
	{
		TArray<FSrc> S; TArray<int32> Idx;
		Subdivide(FMath::Max(1, Subdiv), S, Idx);
		TArray<FVector> V; V.SetNumUninitialized(S.Num());
		TArray<FLinearColor> Col; Col.SetNumUninitialized(S.Num());
		TArray<FVector2D> UV; UV.SetNumUninitialized(S.Num());
		for (int32 I = 0; I < S.Num(); ++I)
		{
			const FSrc& Sv = S[I];
			const FVector Dir = DirOf(Sv);
			const float Slope = Lerp3(D.Slope, Sv);
			float E = Lerp3(D.Elev, Sv);
			// Fine relief: fractal coastlines everywhere, ridged detail that grows with height and slope.
			const float Nz = Fbm(Dir * 60.0, 5);
			const float Ridge = 1.f - FMath::Abs(Fbm(Dir * 140.0 + FVector(13.1), 4));
			const float Above = FMath::Clamp((E - D.SeaLevel) / 3.f, 0.f, 1.f);
			E += 0.35f * Nz + Above * (0.6f + 2.f * FMath::Min(Slope, 1.f)) * (Ridge - 0.5f) * 0.9f;
			V[I] = Dir * (RadiusUU + double(E) * UUPerElev);

			// Albedo from continuous climate fields (no discrete biome patches).
			const float Temp = Lerp3(D.Temp, Sv);
			const float Moist = Lerp3(D.Moist, Sv);
			const float Veg = Lerp3(D.Veg, Sv);
			const float Wet = Lerp3(D.Water, Sv);
			const float Wob = 0.08f * Fbm(Dir * 45.0 + FVector(9.1), 3);
			const FVector3f Sand(0.80f, 0.66f, 0.45f), Steppe(0.57f, 0.52f, 0.37f), Grass(0.40f, 0.47f, 0.24f),
				Forest(0.14f, 0.26f, 0.11f), Jungle(0.09f, 0.21f, 0.08f), Taiga(0.16f, 0.23f, 0.16f),
				Tundra(0.47f, 0.45f, 0.39f), Snow(0.95f, 0.96f, 0.98f), Marsh(0.21f, 0.28f, 0.17f), Rock(0.40f, 0.37f, 0.33f);
			FVector3f Bare = FMath::Lerp(Sand, Steppe, FMath::Clamp(Moist * 1.6f + Wob, 0.f, 1.f));
			FVector3f Green = FMath::Lerp(Grass, Forest, FMath::Clamp(Veg * 1.4f - 0.35f + Wob, 0.f, 1.f));
			Green = FMath::Lerp(Green, Jungle, FMath::Clamp((Temp - 20.f) / 8.f, 0.f, 1.f) * FMath::Clamp(Moist * 1.5f - 0.5f, 0.f, 1.f));
			Green = FMath::Lerp(Green, Taiga, FMath::Clamp((4.f - Temp) / 8.f, 0.f, 1.f));
			FVector3f C3 = FMath::Lerp(Bare, Green, FMath::Clamp(Veg * 1.5f + Wob, 0.f, 1.f));
			C3 = FMath::Lerp(C3, Marsh, FMath::Clamp(Wet * 2.f - 0.2f, 0.f, 0.7f));
			C3 = FMath::Lerp(C3, Tundra, FMath::Clamp((-4.f - Temp) / 8.f + Wob, 0.f, 1.f));
			const float Coast = 1.f - FMath::Clamp((E - D.SeaLevel) / 0.12f, 0.f, 1.f);
			C3 = FMath::Lerp(C3, Sand, Coast * FMath::Clamp(1.2f - Moist, 0.f, 0.8f));
			const float Rocky = FMath::Clamp((Slope - 0.35f) * 2.f + Above * 0.6f * (Ridge - 0.4f), 0.f, 0.85f);
			C3 = FMath::Lerp(C3, Rock, Rocky);
			const float Snowy = FMath::Clamp((-13.f - Temp) / 6.f + (E - 5.5f) * 0.5f * (Ridge + 0.2f) + Wob, 0.f, 1.f);
			C3 = FMath::Lerp(C3, Snow, Snowy);
			C3 *= 1.f + 0.12f * Fbm(Dir * 220.0 + FVector(5.3), 3);
			Col[I] = FLinearColor(FMath::Clamp(C3.X, 0.f, 1.f), FMath::Clamp(C3.Y, 0.f, 1.f), FMath::Clamp(C3.Z, 0.f, 1.f), 1.f);

			// City-light mask (placeholder until the simulation places settlements).
			const float Hab = Lerp3(D.Hab, Sv);
			const float Cluster = FMath::Clamp((Fbm(Dir * 70.0 + FVector(71.f), 3) - 0.05f) * 5.f, 0.f, 1.f);
			const float Speck = FMath::Clamp((Fbm(Dir * 900.0 + FVector(3.7f), 2) - 0.18f) * 6.f, 0.f, 1.f);
			const float Lights = (E > D.SeaLevel + 0.05f) ? FMath::Clamp((Hab - 0.5f) * 3.f, 0.f, 1.f) * Cluster * Speck : 0.f;
			UV[I] = FVector2D(Lights, Rocky);
		}
		const TArray<FVector> Nrm = Normals(V, Idx);
		Land->CreateMeshSection_LinearColor(0, V, Idx, Nrm, UV, {}, {}, {}, Col, {}, false, false);
		if (LandMat) Land->SetMaterial(0, LandMat);
		UE_LOG(LogTemp, Display, TEXT("LabPlanet: land %d verts %d tris"), V.Num(), Idx.Num() / 3);
	}

	// ---- ocean: a smooth shell at sea level, tinted by depth
	{
		TArray<FSrc> S; TArray<int32> Idx;
		Subdivide(FMath::Max(1, Subdiv / 2), S, Idx);
		TArray<FVector> V; V.SetNumUninitialized(S.Num());
		TArray<FLinearColor> Col; Col.SetNumUninitialized(S.Num());
		TArray<FVector2D> UV; UV.SetNumUninitialized(S.Num());
		const double SeaR = SeaRadiusUU();
		for (int32 I = 0; I < S.Num(); ++I)
		{
			const FSrc& Sv = S[I];
			const FVector Dir = DirOf(Sv);
			V[I] = Dir * SeaR;
			const float Depth = FMath::Max(0.f, D.SeaLevel - Lerp3(D.Elev, Sv));
			const float T = FMath::Clamp(Depth / 3.f, 0.f, 1.f);
			FVector3f C3 = FMath::Lerp(FVector3f(0.10f, 0.42f, 0.46f), FVector3f(0.02f, 0.07f, 0.16f), FMath::Pow(T, 0.5f));
			const float SeaIce = FMath::Clamp(Lerp3(D.Ice, Sv) * 2.f, 0.f, 1.f);
			C3 = FMath::Lerp(C3, BiomeSRGB[SEAICE], SeaIce);
			Col[I] = FLinearColor(C3.X, C3.Y, C3.Z, 1.f);
			UV[I] = FVector2D(SeaIce, T);
		}
		const TArray<FVector> Nrm = Normals(V, Idx);
		Ocean->CreateMeshSection_LinearColor(0, V, Idx, Nrm, UV, {}, {}, {}, Col, {}, false, false);
		if (OceanMat) Ocean->SetMaterial(0, OceanMat);
	}
	return true;
}
