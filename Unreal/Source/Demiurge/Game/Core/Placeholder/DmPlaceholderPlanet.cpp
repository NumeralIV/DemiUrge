// DEMIURGE — Game/Core/Placeholder/DmPlaceholderPlanet.cpp : see the header. TEMPORARY (C0 bridge).

#include "Game/Core/Placeholder/DmPlaceholderPlanet.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmScene.h"
#include "Game/Core/DmSimSubsystem.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

#include <cmath>
#include <vector>

DEFINE_LOG_CATEGORY_STATIC(LogDmPlaceholder, Log, All);

namespace
{
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

	// k*k sub-triangles per sim triangle, shared edge vertices (lab code)
	void Subdivide(const dm::World& W, int32 K, TArray<FSrc>& S, TArray<int32>& Idx)
	{
		const int32 N = W.n;
		const int32 NTri = W.geo->nTri;
		const std::vector<uint32_t>& Tris = W.geo->tris;
		S.Reset(); Idx.Reset();
		S.Reserve(N * K * K);
		for (int32 C = 0; C < N; ++C) S.Add({ C, C, C, 1.f, 0.f, 0.f });
		TMap<uint64, int32> EdgeBase;
		EdgeBase.Reserve(NTri * 3 / 2);
		auto EdgeVert = [&](int32 A, int32 B, int32 T) -> int32
		{
			const int32 Lo = FMath::Min(A, B), Hi = FMath::Max(A, B);
			const uint64 Key = (uint64(Lo) << 32) | uint64(Hi);
			int32 Base;
			if (const int32* Found = EdgeBase.Find(Key)) Base = *Found;
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
		for (int32 T = 0; T < NTri; ++T)
		{
			const int32 A = int32(Tris[size_t(3 * T)]), B = int32(Tris[size_t(3 * T + 1)]), C = int32(Tris[size_t(3 * T + 2)]);
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
	}

	TArray<FVector> Normals(const TArray<FVector>& V, const TArray<int32>& Idx)
	{
		TArray<FVector> Nrm;
		Nrm.SetNumZeroed(V.Num());
		for (int32 T = 0; T + 2 < Idx.Num(); T += 3)
		{
			const FVector& A = V[Idx[T]]; const FVector& B = V[Idx[T + 1]]; const FVector& C = V[Idx[T + 2]];
			FVector Fn = FVector::CrossProduct(B - A, C - A);
			if (FVector::DotProduct(Fn, A + B + C) < 0) Fn = -Fn;
			Nrm[Idx[T]] += Fn; Nrm[Idx[T + 1]] += Fn; Nrm[Idx[T + 2]] += Fn;
		}
		for (int32 I = 0; I < V.Num(); ++I) Nrm[I] = Nrm[I].GetSafeNormal(1e-12, V[I].GetSafeNormal());
		return Nrm;
	}

	const double kAgeGlow[7] = { 0.25, 0.35, 0.45, 0.6, 1.0, 1.2, 1.4 };

	// per-cell night light from settlements (pop, tier, culture age) and roads
	std::vector<float> CityLights(const dm::World& W)
	{
		std::vector<float> L(size_t(W.n), 0.f);
		std::vector<int32_t> Ring;
		for (const dm::Settlement& S : W.settlements)
		{
			if (S.dead || !W.valid(S.cell)) continue;
			const int Age = (S.culture >= 0 && S.culture < int(W.cultures.size())) ? FMath::Clamp(W.cultures[size_t(S.culture)].age, 0, 6) : 0;
			const double Amp = FMath::Clamp(FMath::Sqrt(S.pop / 4000.0), 0.1, 1.2) * kAgeGlow[Age];
			const double Sigma = 0.45 + 0.35 * S.tier;
			W.geo->ring(S.cell, 1 + S.tier, Ring);
			for (int32_t C : Ring)
			{
				if (dm::state::isSea(W, C)) continue;
				const double D = W.geo->angle(C, S.cell) / FMath::Max(W.geo->step, 1e-9);
				L[size_t(C)] += float(Amp * FMath::Exp(-(D * D) / (Sigma * Sigma)));
			}
		}
		const double RoadGlow = 0.03 * kAgeGlow[FMath::Clamp(W.age, 0, 6)];
		for (int32 C = 0; C < W.n; ++C)
			L[size_t(C)] = FMath::Min(1.5f, L[size_t(C)] + float(RoadGlow * W.road[size_t(C)]));
		return L;
	}

	FVector3f CultureColor(const dm::World& W, int32 Owner)
	{
		if (Owner < 0 || Owner >= int32(W.settlements.size())) return FVector3f(1, 1, 1);
		const int32 Cu = W.settlements[size_t(Owner)].culture;
		if (Cu < 0 || Cu >= int32(W.cultures.size())) return FVector3f(1, 1, 1);
		const auto& Col = W.cultures[size_t(Cu)].color;
		return FVector3f(float(Col[0]), float(Col[1]), float(Col[2]));
	}
}

ADmPlaceholderPlanet::ADmPlaceholderPlanet()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
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

void ADmPlaceholderPlanet::BeginPlay()
{
	Super::BeginPlay();
	if (UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
	{
		DirtyHandle = Sim->Dirty().OnDirty.AddUObject(this, &ADmPlaceholderPlanet::OnDirty);
		WorldHandle = Sim->OnWorldChanged.AddUObject(this, &ADmPlaceholderPlanet::OnWorldChanged);
		SetActorTransform(Sim->GetPlanetToWorld());
	}
	bPending = true;   // built in the first Tick (after the subsystem's first dirty flush)
}

void ADmPlaceholderPlanet::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
	{
		Sim->Dirty().OnDirty.Remove(DirtyHandle);
		Sim->OnWorldChanged.Remove(WorldHandle);
	}
	Super::EndPlay(Reason);
}

void ADmPlaceholderPlanet::OnDirty(const FDmDirtyFlags& F)
{
	if (F.bElev || F.bColor || F.bWater) bPending = true;
}

void ADmPlaceholderPlanet::OnWorldChanged(const dm::World*)
{
	bPending = true;
	bForce = true;
}

void ADmPlaceholderPlanet::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
		SetActorTransform(Sim->GetPlanetToWorld());
	if (bPending && (bForce || FPlatformTime::Seconds() - LastBuild >= RebuildEvery)) Rebuild();
}

void ADmPlaceholderPlanet::Rebuild()
{
	bPending = false;
	bForce = false;
	LastBuild = FPlatformTime::Seconds();
	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const dm::World* WP = Sim ? Sim->Sim() : nullptr;
	if (!WP || !WP->geo) { Land->ClearAllMeshSections(); Ocean->ClearAllMeshSections(); return; }
	const dm::World& W = *WP;
	const double T0 = FPlatformTime::Seconds();
	const double UUPerElev = DmCoords::UUPerElev;
	const float Sea = float(W.seaLevel);
	FString Overlay;
	FDmShotArgs::Value(TEXT("-overlay="), Overlay);

	// one pass of neighbour smoothing on elevation (keeps the coast's sign mostly intact)
	std::vector<float> Elev(W.elev.begin(), W.elev.end());
	{
		std::vector<float> Sm = Elev;
		for (int32 C = 0; C < W.n; ++C)
		{
			const int32 Deg = W.geo->nbrN[size_t(C)];
			if (Deg == 0) continue;
			double Sum = 0;
			for (int32 K = 0; K < Deg; ++K) Sum += W.elev[size_t(W.geo->nbr[size_t(6 * C + K)])];
			Sm[size_t(C)] = 0.5f * W.elev[size_t(C)] + 0.5f * float(Sum / Deg);
		}
		Elev.swap(Sm);
	}
	const std::vector<float> Lights = CityLights(W);
	std::vector<float> Farm(W.farm.begin(), W.farm.end()), Urban(W.urban.begin(), W.urban.end());
	std::vector<float> Water(W.water.begin(), W.water.end());

	auto Lerp3 = [](const std::vector<float>& F, const FSrc& Sv) { return Sv.WA * F[size_t(Sv.A)] + Sv.WB * F[size_t(Sv.B)] + Sv.WC * F[size_t(Sv.C)]; };
	auto DirOf = [&W](const FSrc& Sv)
	{
		const float* PA = &W.geo->pos[size_t(3 * Sv.A)];
		const float* PB = &W.geo->pos[size_t(3 * Sv.B)];
		const float* PC = &W.geo->pos[size_t(3 * Sv.C)];
		const double X = PA[0] * Sv.WA + PB[0] * Sv.WB + PC[0] * Sv.WC;
		const double Y = PA[1] * Sv.WA + PB[1] * Sv.WB + PC[1] * Sv.WC;
		const double Z = PA[2] * Sv.WA + PB[2] * Sv.WB + PC[2] * Sv.WC;
		return DmCoords::SimToUE(X, Y, Z).GetSafeNormal();
	};

	// ---- land
	{
		TArray<FSrc> S; TArray<int32> Idx;
		Subdivide(W, FMath::Max(1, Subdiv), S, Idx);
		TArray<FVector> V; V.SetNumUninitialized(S.Num());
		TArray<FLinearColor> Col; Col.SetNumUninitialized(S.Num());
		TArray<FVector2D> UV; UV.SetNumUninitialized(S.Num());
		for (int32 I = 0; I < S.Num(); ++I)
		{
			const FSrc& Sv = S[I];
			const FVector Dir = DirOf(Sv);
			const float Slope = Lerp3(W.slope, Sv);
			float E = Lerp3(Elev, Sv);
			const float Nz = Fbm(Dir * 60.0, 5);
			const float Ridge = 1.f - FMath::Abs(Fbm(Dir * 140.0 + FVector(13.1), 4));
			const float Above = FMath::Clamp((E - Sea) / 3.f, 0.f, 1.f);
			E += 0.35f * Nz + Above * (0.6f + 2.f * FMath::Min(Slope, 1.f)) * (Ridge - 0.5f) * 0.9f;
			V[I] = Dir * (DmCoords::RadiusUU + double(E) * UUPerElev);

			const float Temp = Lerp3(W.temp, Sv);
			const float Moist = Lerp3(W.moist, Sv);
			const float Veg = Lerp3(W.veg, Sv);
			const float Wet = Lerp3(Water, Sv);
			const float Wob = 0.08f * Fbm(Dir * 45.0 + FVector(9.1), 3);
			const FVector3f Sand(0.80f, 0.66f, 0.45f), Steppe(0.57f, 0.52f, 0.37f), Grass(0.40f, 0.47f, 0.24f),
				Forest(0.14f, 0.26f, 0.11f), Jungle(0.09f, 0.21f, 0.08f), Taiga(0.16f, 0.23f, 0.16f),
				Tundra(0.47f, 0.45f, 0.39f), Snow(0.95f, 0.96f, 0.98f), Marsh(0.21f, 0.28f, 0.17f), Rock(0.40f, 0.37f, 0.33f),
				Fields(0.47f, 0.44f, 0.25f), Town(0.36f, 0.34f, 0.32f);
			FVector3f Bare = FMath::Lerp(Sand, Steppe, FMath::Clamp(Moist * 1.6f + Wob, 0.f, 1.f));
			FVector3f Green = FMath::Lerp(Grass, Forest, FMath::Clamp(Veg * 1.4f - 0.35f + Wob, 0.f, 1.f));
			Green = FMath::Lerp(Green, Jungle, FMath::Clamp((Temp - 20.f) / 8.f, 0.f, 1.f) * FMath::Clamp(Moist * 1.5f - 0.5f, 0.f, 1.f));
			Green = FMath::Lerp(Green, Taiga, FMath::Clamp((4.f - Temp) / 8.f, 0.f, 1.f));
			FVector3f C3 = FMath::Lerp(Bare, Green, FMath::Clamp(Veg * 1.5f + Wob, 0.f, 1.f));
			C3 = FMath::Lerp(C3, Marsh, FMath::Clamp(Wet * 2.f - 0.2f, 0.f, 0.7f));
			C3 = FMath::Lerp(C3, Tundra, FMath::Clamp((-4.f - Temp) / 8.f + Wob, 0.f, 1.f));
			// people: patchwork fields and grey town cores
			const float Fm = Lerp3(Farm, Sv);
			const float Patch = 0.5f + 0.5f * Fbm(Dir * 900.0 + FVector(2.2), 2);
			C3 = FMath::Lerp(C3, Fields * (0.85f + 0.3f * Patch), FMath::Clamp(Fm * 0.55f, 0.f, 0.55f));
			C3 = FMath::Lerp(C3, Town, FMath::Clamp(Lerp3(Urban, Sv) * 0.8f, 0.f, 0.8f));
			const float Coast = 1.f - FMath::Clamp((E - Sea) / 0.12f, 0.f, 1.f);
			C3 = FMath::Lerp(C3, Sand, Coast * FMath::Clamp(1.2f - Moist, 0.f, 0.8f));
			const float Rocky = FMath::Clamp((Slope - 0.35f) * 2.f + Above * 0.6f * (Ridge - 0.4f), 0.f, 0.85f);
			C3 = FMath::Lerp(C3, Rock, Rocky);
			const float Snowy = FMath::Clamp((-13.f - Temp) / 6.f + (E - 5.5f) * 0.5f * (Ridge + 0.2f) + Wob, 0.f, 1.f);
			C3 = FMath::Lerp(C3, Snow, Snowy);
			C3 *= 1.f + 0.12f * Fbm(Dir * 220.0 + FVector(5.3), 3);

			if (!Overlay.IsEmpty())
			{
				const int32 Near = Sv.WA >= Sv.WB && Sv.WA >= Sv.WC ? Sv.A : (Sv.WB >= Sv.WC ? Sv.B : Sv.C);
				FVector3f Tint = C3;
				if (Overlay == TEXT("owner")) { const int32 O = W.owner[size_t(Near)]; if (O >= 0) Tint = FMath::Lerp(C3, CultureColor(W, O), 0.65f); }
				else if (Overlay == TEXT("hab")) { const float H = W.hab[size_t(Near)]; Tint = FVector3f(H, H * 0.8f + 0.1f, 0.15f); }
				else if (Overlay == TEXT("road")) { if (W.road[size_t(Near)]) Tint = FVector3f(1.f, 0.2f, 0.1f); }
				C3 = Tint;
			}
			Col[I] = FLinearColor(FMath::Clamp(C3.X, 0.f, 1.f), FMath::Clamp(C3.Y, 0.f, 1.f), FMath::Clamp(C3.Z, 0.f, 1.f), 1.f);

			// night lights: settlements and roads, broken into specks (streets) around a solid core
			const float Speck = FMath::Square(FMath::Clamp((Fbm(Dir * 1100.0 + FVector(3.7f), 2) + 0.02f) * 4.f, 0.f, 1.f));
			const float Core = Lerp3(Urban, Sv);
			const float Li = (E > Sea + 0.02f) ? FMath::Clamp(0.45f * Lerp3(Lights, Sv) * Speck + 0.5f * Core * Core, 0.f, 0.9f) : 0.f;
			UV[I] = FVector2D(Li, Rocky);
		}
		const TArray<FVector> Nrm = Normals(V, Idx);
		Land->CreateMeshSection_LinearColor(0, V, Idx, Nrm, UV, {}, {}, {}, Col, {}, false, false);
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Bridge/M_PlaceholderLand.M_PlaceholderLand"));
		if (!M) M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
		if (M) Land->SetMaterial(0, M);
		UE_LOG(LogDmPlaceholder, Display, TEXT("DmPlaceholderPlanet: land %d verts %d tris (material %s)"), V.Num(), Idx.Num() / 3, M ? *M->GetName() : TEXT("none"));
	}

	// ---- ocean: a smooth shell at sea level, tinted by depth
	{
		TArray<FSrc> S; TArray<int32> Idx;
		Subdivide(W, FMath::Max(1, Subdiv / 2), S, Idx);
		TArray<FVector> V; V.SetNumUninitialized(S.Num());
		TArray<FLinearColor> Col; Col.SetNumUninitialized(S.Num());
		TArray<FVector2D> UV; UV.SetNumUninitialized(S.Num());
		const double SeaR = DmCoords::HeightToRadiusUU(W.seaLevel);
		for (int32 I = 0; I < S.Num(); ++I)
		{
			const FSrc& Sv = S[I];
			const FVector Dir = DirOf(Sv);
			V[I] = Dir * SeaR;
			const float Depth = FMath::Max(0.f, Sea - Lerp3(W.elev, Sv));
			const float T = FMath::Clamp(Depth / 3.f, 0.f, 1.f);
			FVector3f C3 = FMath::Lerp(FVector3f(0.10f, 0.42f, 0.46f), FVector3f(0.02f, 0.07f, 0.16f), FMath::Pow(T, 0.5f));
			const float SeaIce = FMath::Clamp(Lerp3(W.ice, Sv) * 2.f, 0.f, 1.f);
			C3 = FMath::Lerp(C3, FVector3f(0.90f, 0.93f, 0.97f), SeaIce);
			Col[I] = FLinearColor(C3.X, C3.Y, C3.Z, 1.f);
			UV[I] = FVector2D(SeaIce, T);
		}
		const TArray<FVector> Nrm = Normals(V, Idx);
		Ocean->CreateMeshSection_LinearColor(0, V, Idx, Nrm, UV, {}, {}, {}, Col, {}, false, false);
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Bridge/M_PlaceholderOcean.M_PlaceholderOcean"));
		if (!M) M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
		if (M) Ocean->SetMaterial(0, M);
	}
	UE_LOG(LogDmPlaceholder, Display, TEXT("DmPlaceholderPlanet: rebuilt in %.0f ms"), (FPlatformTime::Seconds() - T0) * 1000.0);
}

static FDmScenePartRegistrar GDmPlaceholderPlanetPart(TEXT("planet"), EDmPartKind::World, -100,
	[](const FDmSceneContext& S) -> AActor*
	{
		return S.World ? S.World->SpawnActor<ADmPlaceholderPlanet>() : nullptr;
	}, TEXT("Core/Placeholder (lab planet)"));
