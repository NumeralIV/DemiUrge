// DEMIURGE — Game/Nature/DmNatureFauna.cpp : see the header.

#include "Game/Nature/DmNatureFauna.h"
#include "Game/Nature/DmNatureMesh.h"
#include "Game/Core/DmCoords.h"

#include "Sim/Core/Geo.h"
#include "Sim/Schema.h"

using DmNature::Hash01;
using DmNature::Hash3;

TSharedPtr<FDmTileResult, ESPMode::ThreadSafe> DmNatureFauna::Build(const FDmNatureFields& F, const FDmNatureGround& G, const FVector& CenterDir, double RadiusKm, float Density)
{
	TSharedPtr<FDmTileResult, ESPMode::ThreadSafe> Out = MakeShared<FDmTileResult, ESPMode::ThreadSafe>();
	Out->Center = CenterDir * F.SeaRadiusUU;
	Out->FieldsVersion = F.Version;
	if (!F.Geo || F.N <= 0) return Out;
	const dm::Geo& Geo = *F.Geo;
	const double CosMax = FMath::Cos(RadiusKm / DmCoords::RadiusKm);
	const double Step = Geo.step;
	const double SeaR = F.SeaRadiusUU;
	TMap<int32, int32> BatchOf;
	auto Emit = [&](EDmNatureMesh Id, int32 Var, const FTransform& X, float C0, float C1, float C2)
	{
		const int32 Slot = int32(Id) * 8 + Var;
		int32* Bi = BatchOf.Find(Slot);
		if (!Bi)
		{
			FDmTileBatch& B = Out->Batches.AddDefaulted_GetRef();
			B.Id = Id; B.Variant = Var;
			Bi = &BatchOf.Add(Slot, Out->Batches.Num() - 1);
		}
		Out->Batches[*Bi].Xf.Add(X);
		Out->Batches[*Bi].Custom.Append({ C0, C1, C2, 0.f });
		Out->PerKind[int32(Id)]++;
		Out->Instances++;
	};
	// a point at (east, north) metres from Dir, on the ground (or the sea) with its tangent frame
	auto Offset = [](const FVector& Dir, double EastM, double NorthM)
	{
		FVector E, N;
		DmCoords::Basis(Dir, E, N);
		return (Dir + (E * EastM + N * NorthM) / (DmCoords::RadiusKm * 1000.0)).GetSafeNormal();
	};
	auto Frame = [](const FVector& Up, double HeadingRad)
	{
		// local X = right of the heading, Y = heading (forward), Z = up
		FVector E, N;
		DmCoords::Basis(Up, E, N);
		const FVector Fwd = N * FMath::Cos(HeadingRad) + E * FMath::Sin(HeadingRad);
		const FVector Right = FVector::CrossProduct(Fwd, Up);
		return FMatrix(Right, Fwd, Up, FVector::ZeroVector).ToQuat();
	};

	for (int32 C = 0; C < F.N; ++C)
	{
		const float* Pp = &Geo.pos[size_t(C) * 3];
		const FVector Dir = DmCoords::SimToUE(Pp[0], Pp[1], Pp[2]).GetSafeNormal();
		if (FVector::DotProduct(Dir, CenterDir) < CosMax) continue;
		const float Fa = F.Fauna[C] * Density;
		const uint8 B = F.Biome[C];
		const bool bSea = F.Elev[C] < F.SeaLevel;
		// ---- herds
		const bool bGrazeLand = B == dm::Biome::SAVANNA || B == dm::Biome::GRASSLAND || B == dm::Biome::TUNDRA || B == dm::Biome::FOREST
			|| B == dm::Biome::TAIGA || B == dm::Biome::WETLAND;
		if (!bSea && bGrazeLand && Fa > 0.2f && Hash01(Hash3(C, 1, 0, 0xFA01u)) < 0.4f * Fa)
		{
			// herd species by biome (docs/ASSET-INTEGRATION.md 4.7; EDmGrazer): savanna wildebeest and antelope (now and
			// then a pride of lions nearby), grassland / steppe bison, wildebeest and antelope, tundra bison, forest,
			// taiga and wetland deer (does with a stag)
			int32 Var = int32(EDmGrazer::Doe);
			const float Hs = Hash01(Hash3(C, 2, 0, 0xFA01u));
			if (B == dm::Biome::SAVANNA) Var = int32(Hs < 0.6f ? EDmGrazer::Wildebeest : EDmGrazer::Antelope);
			else if (B == dm::Biome::GRASSLAND) Var = int32(Hs < 0.55f ? EDmGrazer::Bison : Hs < 0.8f ? EDmGrazer::Wildebeest : EDmGrazer::Antelope);
			else if (B == dm::Biome::TUNDRA) Var = int32(EDmGrazer::Bison);
			const bool bDeer = Var == int32(EDmGrazer::Doe);
			const FVector Hc = Offset(Dir, (Hash01(Hash3(C, 3, 0, 0xFA01u)) - 0.5) * Step * 60000.0, (Hash01(Hash3(C, 4, 0, 0xFA01u)) - 0.5) * Step * 60000.0);
			const int32 Count = bDeer ? 4 + int32(9.f * Fa * Hash01(Hash3(C, 5, 0, 0xFA01u)))
				: 6 + int32(26.f * Fa * Hash01(Hash3(C, 5, 0, 0xFA01u))) + (Var == int32(EDmGrazer::Wildebeest) ? 10 : 0);
			const double Spread = 25.0 + 70.0 * Hash01(Hash3(C, 6, 0, 0xFA01u));
			const double Heading = Hash01(Hash3(C, 7, 0, 0xFA01u)) * 2.0 * UE_DOUBLE_PI;
			const float Tint = 0.88f + 0.24f * Hash01(Hash3(C, 8, 0, 0xFA01u));
			for (int32 K = 0; K < Count; ++K)
			{
				const double A = Hash01(Hash3(C, K, 1, 0xFA02u)) * 2.0 * UE_DOUBLE_PI;
				const double R = Spread * FMath::Sqrt(Hash01(Hash3(C, K, 2, 0xFA02u)));
				const FVector D = Offset(Hc, FMath::Cos(A) * R, FMath::Sin(A) * R * 0.6);
				FVector Nrm;
				const double Rg = G.Radius(D, &Nrm);
				if (Rg - SeaR < 150.0 || FVector::DotProduct(Nrm, D) < 0.85) continue;
				const double H = Heading + (Hash01(Hash3(C, K, 3, 0xFA02u)) - 0.5) * 1.4;
				const float Sc = 0.88f + 0.24f * Hash01(Hash3(C, K, 4, 0xFA02u));
				const float Tk = Tint * (0.94f + 0.12f * Hash01(Hash3(C, K, 5, 0xFA02u)));
				const int32 V = bDeer && K == 0 ? int32(EDmGrazer::Stag) : Var;
				Emit(EDmNatureMesh::Grazer, V, FTransform(Frame(D, H), D * (Rg - 4.0) - Out->Center, FVector(Sc)), Tk, Tk, Tk * (0.97f + 0.06f * Hash01(Hash3(C, K, 6, 0xFA02u))));
			}
			// a pride resting at the edge of a savanna herd (about 1 herd in 6)
			if (B == dm::Biome::SAVANNA && Hash01(Hash3(C, 9, 0, 0xFA01u)) < 0.17f)
			{
				const double Pa = Heading + UE_DOUBLE_PI * 0.5;
				const FVector Pc = Offset(Hc, FMath::Cos(Pa) * (Spread + 60.0), FMath::Sin(Pa) * (Spread + 60.0));
				const int32 NP = 2 + int32(3.f * Hash01(Hash3(C, 10, 0, 0xFA01u)));
				for (int32 K = 0; K < NP; ++K)
				{
					const double A = Hash01(Hash3(C, K, 7, 0xFA02u)) * 2.0 * UE_DOUBLE_PI;
					const double R = 3.0 + 9.0 * Hash01(Hash3(C, K, 8, 0xFA02u));
					const FVector D = Offset(Pc, FMath::Cos(A) * R, FMath::Sin(A) * R);
					FVector Nrm;
					const double Rg = G.Radius(D, &Nrm);
					if (Rg - SeaR < 150.0 || FVector::DotProduct(Nrm, D) < 0.85) continue;
					const float Tk = 0.92f + 0.16f * Hash01(Hash3(C, K, 9, 0xFA02u));
					Emit(EDmNatureMesh::Grazer, int32(K == 0 ? EDmGrazer::Lion : EDmGrazer::Lioness), FTransform(Frame(D, A * 1.7), D * (Rg - 4.0) - Out->Center, FVector(1.0)), Tk, Tk, Tk);
				}
			}
		}
		// ---- birds: soaring flocks over land, gulls along coasts
		const bool bCoast = FMath::Abs(F.Elev[C] - F.SeaLevel) < 0.35;
		const float PBird = bSea ? 0.f : (bCoast ? 0.35f : 0.06f + 0.22f * F.Veg[C]);
		if (PBird > 0.f && Hash01(Hash3(C, 1, 0, 0xB1D5u)) < PBird * Density)
		{
			const FVector Fc = Offset(Dir, (Hash01(Hash3(C, 2, 0, 0xB1D5u)) - 0.5) * Step * 70000.0, (Hash01(Hash3(C, 3, 0, 0xB1D5u)) - 0.5) * Step * 70000.0);
			const double Rg = G.Radius(Fc);
			const double Alt = (60.0 + 160.0 * Hash01(Hash3(C, 4, 0, 0xB1D5u))) * 100.0;
			const int32 Count = 3 + int32(9.f * Hash01(Hash3(C, 5, 0, 0xB1D5u)));
			const int32 Var = bCoast ? 1 : 0;
			const float Sense = Hash01(Hash3(C, 6, 0, 0xB1D5u)) < 0.5f ? -1.f : 1.f;
			for (int32 K = 0; K < Count; ++K)
			{
				const float R = 20.f + 60.f * Hash01(Hash3(C, K, 1, 0xB1D6u));
				const float Spd = Sense * (8.f + 6.f * Hash01(Hash3(C, K, 2, 0xB1D6u)));
				const double Ph = Hash01(Hash3(C, K, 3, 0xB1D6u)) * 2.0 * UE_DOUBLE_PI;
				// the bird sits on its circle: local -X points to the centre, +Y is the flight direction
				FVector E, N;
				DmCoords::Basis(Fc, E, N);
				const FVector Radial = E * FMath::Cos(Ph) + N * FMath::Sin(Ph);
				const FVector Tangent = FVector::CrossProduct(Fc, Radial).GetSafeNormal();
				const FVector Pos = Fc * (FMath::Max(Rg, SeaR) + Alt + Hash01(Hash3(C, K, 4, 0xB1D6u)) * 1500.0) + Radial * (R * 100.0);
				const FQuat Q = FMatrix(Radial, Tangent, Fc, FVector::ZeroVector).ToQuat();
				Emit(EDmNatureMesh::Bird, Var, FTransform(Q, Pos - Out->Center, FVector(1.0)), R, Spd, 0.9f + 0.2f * Hash01(Hash3(C, K, 5, 0xB1D6u)));
			}
		}
		// ---- fish shoals on shallow, rich seas
		if (bSea && F.Elev[C] > F.SeaLevel - 1.2 && Fa > 0.2f && Hash01(Hash3(C, 1, 0, 0xF15Au)) < 0.5f * Fa)
		{
			const FVector Sc = Offset(Dir, (Hash01(Hash3(C, 2, 0, 0xF15Au)) - 0.5) * Step * 60000.0, (Hash01(Hash3(C, 3, 0, 0xF15Au)) - 0.5) * Step * 60000.0);
			const int32 Count = 8 + int32(14.f * Hash01(Hash3(C, 4, 0, 0xF15Au)));
			const float Sense = Hash01(Hash3(C, 5, 0, 0xF15Au)) < 0.5f ? -1.f : 1.f;
			const int32 Var = Hash01(Hash3(C, 6, 0, 0xF15Au)) < 0.3f ? 1 : 0;
			for (int32 K = 0; K < Count; ++K)
			{
				const float R = 3.f + 7.f * Hash01(Hash3(C, K, 1, 0xF15Bu));
				const float Spd = Sense * (2.5f + 2.f * Hash01(Hash3(C, K, 2, 0xF15Bu)));
				const double Ph = Hash01(Hash3(C, K, 3, 0xF15Bu)) * 2.0 * UE_DOUBLE_PI;
				FVector E, N;
				DmCoords::Basis(Sc, E, N);
				const FVector Radial = E * FMath::Cos(Ph) + N * FMath::Sin(Ph);
				const FVector Tangent = FVector::CrossProduct(Sc, Radial).GetSafeNormal();
				const FVector Pos = Sc * SeaR + Radial * (R * 100.0);
				const FQuat Q = FMatrix(Radial, Tangent, Sc, FVector::ZeroVector).ToQuat();
				Emit(EDmNatureMesh::Fish, Var, FTransform(Q, Pos - Out->Center, FVector(1.0)), R, Spd, 0.9f + 0.2f * Hash01(Hash3(C, K, 4, 0xF15Bu)));
			}
		}
	}
	return Out;
}
