// DEMIURGE — Game/Nature/DmNatureScatter.cpp : see the header.

#include "Game/Nature/DmNatureScatter.h"
#include "Game/Nature/DmNatureMesh.h"
#include "Game/Core/DmCoords.h"

#include "HAL/PlatformTime.h"

using DmNature::Smooth;
using DmNature::Hash01;
using DmNature::Hash3;

FVector FDmTileKey::CenterDir(double* OutAngRadius) const
{
	const EDmNatureLayer L = EDmNatureLayer(Layer);
	const double T = TilesPerFace(L);
	const double U0 = -1.0 + 2.0 * X / T, U1 = -1.0 + 2.0 * (X + 1) / T;
	const double V0 = -1.0 + 2.0 * Y / T, V1 = -1.0 + 2.0 * (Y + 1) / T;
	const FVector C = DmCube::FromFace(Face, 0.5 * (U0 + U1), 0.5 * (V0 + V1));
	if (OutAngRadius)
	{
		double A = 0;
		for (int32 K = 0; K < 4; ++K)
		{
			const FVector P = DmCube::FromFace(Face, (K & 1) ? U1 : U0, (K & 2) ? V1 : V0);
			A = FMath::Max(A, FMath::Acos(FMath::Clamp(FVector::DotProduct(P, C), -1.0, 1.0)));
		}
		*OutAngRadius = A;
	}
	return C;
}

FDmTileKey FDmTileKey::At(EDmNatureLayer L, const FVector& Dir)
{
	double U, V;
	FDmTileKey K;
	K.Layer = uint8(L);
	K.Face = uint8(DmCube::ToFace(Dir.GetSafeNormal(), U, V));
	const int32 T = TilesPerFace(L);
	K.X = uint16(FMath::Clamp(int32((U + 1.0) * 0.5 * T), 0, T - 1));
	K.Y = uint16(FMath::Clamp(int32((V + 1.0) * 0.5 * T), 0, T - 1));
	return K;
}

float DmNatureScatter::ForestAt(const FDmNatureFields& F, const FVector& Dir)
{
	if (F.Canopy.Num() == 0) return 0.f;
	return FDmNatureFields::CanopyValue(F.Sample(F.Canopy, Dir).X, Dir);
}

TSharedPtr<FDmTileResult, ESPMode::ThreadSafe> DmNatureScatter::BuildTile(const FDmNatureFields& F, const FDmNatureGround& G, const FDmTileKey& Key, const FDmScatterSettings& S)
{
	const double T0 = FPlatformTime::Seconds();
	TSharedPtr<FDmTileResult, ESPMode::ThreadSafe> Out = MakeShared<FDmTileResult, ESPMode::ThreadSafe>();
	Out->Key = Key;
	Out->FieldsVersion = F.Version;
	const EDmNatureLayer Layer = EDmNatureLayer(Key.Layer);
	const int32 T = FDmTileKey::TilesPerFace(Layer), C = FDmTileKey::CellsPerTile(Layer);
	const double Gn = double(T) * C;
	double AngR = 0;
	const FVector CDir = Key.CenterDir(&AngR);
	const double SeaR = F.SeaRadiusUU;
	Out->Center = CDir * SeaR;
	if (F.Canopy.Num() == 0) return Out;

	// cell area (m^2) from the tile's corners
	const double U0 = -1.0 + 2.0 * Key.X / T, U1 = -1.0 + 2.0 * (Key.X + 1) / T;
	const double V0 = -1.0 + 2.0 * Key.Y / T, V1 = -1.0 + 2.0 * (Key.Y + 1) / T;
	const double Rm = DmCoords::RadiusKm * 1000.0;
	const FVector P00 = DmCube::FromFace(Key.Face, U0, V0) * Rm, P10 = DmCube::FromFace(Key.Face, U1, V0) * Rm, P01 = DmCube::FromFace(Key.Face, U0, V1) * Rm;
	const double CellArea = FVector::CrossProduct(P10 - P00, P01 - P00).Size() / (double(C) * C);

	// quick reject: no forest potential and no grass anywhere near this tile
	{
		float MaxR = 0.f, MaxScorch = 0.f, MinMo = 1.f;
		for (int32 K = 0; K < 9; ++K)
		{
			const FVector D = DmCube::FromFace(Key.Face, FMath::Lerp(U0, U1, (K % 3) * 0.5), FMath::Lerp(V0, V1, (K / 3) * 0.5));
			MaxR = FMath::Max(MaxR, F.Sample(F.Canopy, D).X);
			const FVector4f Cl = F.Sample(F.Climate, D);
			MaxScorch = FMath::Max(MaxScorch, Cl.W);
			MinMo = FMath::Min(MinMo, Cl.Y);
		}
		const bool bEmpty = Layer == EDmNatureLayer::Grass ? MaxR < 0.04f : (MaxR < 0.003f && MaxScorch < 0.05f && MinMo > 0.3f);
		if (bEmpty) { Out->Ms = (FPlatformTime::Seconds() - T0) * 1000.0; return Out; }
	}

	TArray<int32> Near;
	F.NearClearings(CDir, AngR, Near);

	TMap<int32, int32> BatchOf;   // Id * 8 + variant -> batch index
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
		FDmTileBatch& B = Out->Batches[*Bi];
		B.Xf.Add(X);
		B.Custom.Append({ C0, C1, C2 });
		Out->PerKind[int32(Id)]++;
		Out->Instances++;
	};
	auto Place = [&](const FVector& D, double Rg, float Yaw, float Tilt, float TiltAz, const FVector& Sc)
	{
		const FQuat Up = FQuat::FindBetweenNormals(FVector::UpVector, D);
		const FQuat Q = Up * FQuat(FVector::UpVector, Yaw) * FQuat(FVector(FMath::Cos(TiltAz), FMath::Sin(TiltAz), 0.0), Tilt);
		return FTransform(Q, D * Rg - Out->Center, Sc);
	};

	const float Dens = S.Density;
	const int32 NV = FMath::Clamp(S.MaxVariants, 1, 4);
	const uint32 FaceSeed = uint32(Key.Face) * 0x9E3779B9u;

	if (Layer == EDmNatureLayer::Trees)
	{
		const double AreaK = CellArea / 41.0;        // rules are per 6.4 m cell
		const double ShrubK = CellArea / 36.0;
		for (int32 J = 0; J < C; ++J)
			for (int32 I = 0; I < C; ++I)
			{
				const int32 Gx = Key.X * C + I, Gy = Key.Y * C + J;
				++Out->Candidates;
				const uint32 Hb = Hash3(Gx, Gy, Key.Face, 0x7111u);
				const bool bTreeKeep = Hash01(Hash3(Gx, Gy, Key.Face, 0x51u)) < S.Keep;
				const bool bShrubTry = S.bShrubs;
				if (!bTreeKeep && !bShrubTry) continue;
				const double U = -1.0 + 2.0 * (Gx + 0.15 + 0.7 * Hash01(Hb)) / Gn;
				const double V = -1.0 + 2.0 * (Gy + 0.15 + 0.7 * Hash01(Hb * 747796405u + 1u)) / Gn;
				const FVector D = DmCube::FromFace(Key.Face, U, V);
				const FVector4f Cn = F.Sample(F.Canopy, D);
				const FVector4f Cl = F.Sample(F.Climate, D);
				const float Scorch = Cl.W;
				if (Cn.X < 0.004f && Scorch < 0.05f && Cl.Y > 0.3f) continue;   // nothing grows here (and it is not desert)
				const float Rp = Cn.X, Con = Cn.Y, Wet = Cn.Z, Dry = Cn.W;
				const float Tc = Cl.X * 80.f - 40.f, Mo = Cl.Y, Snow = Cl.Z;
				const float Hot = Smooth(17.f, 22.f, Tc), DryK = Smooth(0.5f, 0.3f, Mo);
				const float Rnd = Hash01(Hash3(Gx, Gy, Key.Face, 0x2203u));
				const float RndS = Hash01(Hash3(Gx, Gy, Key.Face, 0x5309u));
				// unburnt potential (burnt forests keep standing, charred)
				const float RpU = FMath::Min(1.f, Rp / FMath::Max(0.08f, 1.f - 0.92f * Scorch));
				const float Can = Rp > 0.02f || Scorch > 0.05f ? FDmNatureFields::CanopyValue(Scorch > 0.05f ? RpU : Rp, D) : 0.f;
				// lone trees on open land grow in loose groups and copses (seed shadow, hollows), never evenly spread
				float Open = (0.012f + 0.03f * Rp + 0.1f * Hot * DryK * Rp);
				if (Can < 0.95f)
				{
					const float Cx = float(D.X * 100000.0) / 160.f, Cy = float(D.Y * 100000.0) / 160.f, Cz = float(D.Z * 100000.0) / 160.f;
					Open *= 2.4f * Smooth(-0.05f, 0.4f, DmNature::Fbm3(Cx + 11.f, Cy, Cz, 2));
				}
				const float Desert = 0.004f * Smooth(0.25f, 0.08f, Mo) * Smooth(6.f, 16.f, Tc);
				const float Burn = Smooth(0.08f, 0.4f, Scorch);
				float P0 = (Can * 0.62f * (1.f - 0.15f * Wet) * (1.f - Burn) + Can * 0.45f * Burn + (1.f - Can) * Open + 0.06f * Hot + Desert) * float(AreaK) * Dens;
				// shrubs: forest edges and open land; few under a closed canopy (they would not be seen)
				const float Edge = 1.f - FMath::Abs(Can * 2.f - 1.f);
				const float Ps0 = bShrubTry ? (0.025f + 0.07f * Rp * (1.f - Can) + 0.3f * Edge * Smooth(0.02f, 0.1f, Rp) + 0.08f * Smooth(-12.f, -2.f, Tc) * Smooth(0.f, 0.1f, Rp + 0.05f)) * float(ShrubK) * Dens : 0.f;
				const bool bTreeTry = bTreeKeep && Rnd < P0;
				const bool bShrub = bShrubTry && RndS < Ps0;
				if (!bTreeTry && !bShrub) continue;
				// clearings, ground, water, slope
				const float Clear = Near.Num() ? F.ClearingAt(D, Near) : 1.f;
				if (Clear <= 0.01f) continue;
				FVector Nrm;
				const double Rg = G.Radius(D, &Nrm);
				const float Hm = float((Rg - SeaR) / 100.0);
				if (Hm < 0.8f) continue;
				const FVector4f Ld = F.Sample(F.Land, D);
				const float LakeK = 1.f - Smooth(0.1f, 0.5f, Ld.Z);
				const float Cos = float(FVector::DotProduct(Nrm, D));
				const float SlopeK = Smooth(0.62f, 0.8f, Cos);
				const float HeightK = Smooth(1.5f, 4.f, Hm);
				const float Coast = Smooth(14.f, 2.f, Hm) * Hot;
				const float Base = Clear * LakeK * SlopeK;
				if (bTreeTry)
				{
					const float CanH = Can * HeightK;
					const float P = (CanH * 0.62f * (1.f - 0.15f * Wet) * (1.f - Burn) + CanH * 0.45f * Burn + (1.f - CanH) * Open + Coast * 0.06f + Desert) * float(AreaK) * Dens * Base;
					if (Rnd < P)
					{
						const float Px = float(D.X * 100000.0), Py = float(D.Y * 100000.0), Pz = float(D.Z * 100000.0);
						const float Patch = DmNature::Fbm3(Px / 700.f + 7.f, Py / 700.f, Pz / 700.f, 3);
						float Wk[6];
						Wk[0] = Smooth(13.f, 5.f, Tc) + 0.6f * Smooth(0.05f, 0.35f, Patch) * Smooth(19.f, 11.f, Tc) + 0.4f * Con * Smooth(0.f, 0.2f, Patch + 0.1f);
						Wk[1] = Smooth(3.f, 10.f, Tc) * Smooth(0.2f, 0.4f, Mo) * (1.f - 0.85f * Coast) * FMath::Lerp(1.f, Smooth(0.3f, 0.55f, Mo), Hot);
						Wk[2] = Hot * (Smooth(25.f, 3.f, Hm) * 4.f + 0.35f * Smooth(0.6f, 0.8f, Mo));
						Wk[3] = Smooth(16.f, 21.f, Tc) * FMath::Max(DryK, Dry) * (CanH < 0.5f ? 3.f : 0.6f);
						Wk[4] = 0.f;
						Wk[5] = 0.012f + Desert * 400.f + 0.3f * Smooth(-6.f, -12.f, Tc);
						float Sum = 0.f;
						for (float X : Wk) Sum += X;
						int32 Kind = 5;
						if (Burn > 0.f && Hash01(Hash3(Gx, Gy, Key.Face, 0x6661u)) < Burn) Kind = 5;
						else if (Sum > 1e-4f)
						{
							float Pick = Hash01(Hash3(Gx, Gy, Key.Face, 0x10u)) * Sum;
							for (Kind = 0; Kind < 5; ++Kind) { if (Pick < Wk[Kind]) break; Pick -= Wk[Kind]; }
						}
						const EDmNatureMesh Id = EDmNatureMesh(Kind);
						const int32 Var = int32(Hash3(Gx, Gy, Key.Face, 0x11u) % uint32(NV));
						const float H1 = Hash01(Hash3(Gx, Gy, Key.Face, 0x12u));
						float Scale = FMath::Lerp(0.72f, 1.22f, H1);
						if (CanH < 0.3f) Scale *= 0.9f + 0.25f * Hash01(Hash3(Gx, Gy, Key.Face, 0x13u));   // lone trees spread wide
						Scale *= FMath::Lerp(0.72f, 1.f, Smooth(-7.f, 2.f, Tc));                          // stunted in the cold
						if (Id == EDmNatureMesh::Broadleaf) Scale *= 1.f + 0.35f * Wet + (Hash01(Hash3(Gx, Gy, Key.Face, 0x17u)) < 0.04f * Wet ? 0.6f : 0.f);
						if (Id == EDmNatureMesh::Dead && Burn <= 0.f) Scale *= 0.85f;
						const float Stretch = FMath::Lerp(0.9f, 1.1f, Hash01(Hash3(Gx, Gy, Key.Face, 0x16u)));
						const float Yaw = Hash01(Hash3(Gx, Gy, Key.Face, 0x14u)) * 2.f * PI;
						const float Tilt = (Hash01(Hash3(Gx, Gy, Key.Face, 0x15u)) - 0.5f) * 0.06f;
						const FTransform X = Place(D, Rg - 20.0, Yaw, Tilt, Yaw * 1.7f, FVector(Scale, Scale, Scale * Stretch));
						const float SnowK = Snow * (0.65f + 0.35f * Hash01(Hb >> 3));
						const float DryTint = FMath::Clamp(Dry * 0.9f + 0.5f * Smooth(0.35f, 0.15f, Mo), 0.f, 1.f) * (Id == EDmNatureMesh::Conifer ? 0.3f : 1.f);
						const float BurnC = Id == EDmNatureMesh::Dead ? FMath::Max(Burn, 0.f) : 0.f;
						Emit(Id, Var, X, BurnC, SnowK, DryTint);
					}
				}
				if (bShrub && Hm > 0.6f)
				{
					const float Ps = Ps0 * Base;
					if (RndS < Ps)
					{
						const int32 Var = int32(Hash3(Gx, Gy, Key.Face, 0x21u) % 4u);
						const float Sc = FMath::Lerp(0.6f, 1.4f, Hash01(Hash3(Gx, Gy, Key.Face, 0x22u))) * FMath::Lerp(0.7f, 1.f, Smooth(-10.f, 0.f, Tc));
						const float Yaw = Hash01(Hash3(Gx, Gy, Key.Face, 0x23u)) * 2.f * PI;
						const FVector Ds = DmCube::FromFace(Key.Face, U + 1.1 / Gn, V - 0.9 / Gn);   // offset from the tree in the same cell
						FVector Ns;
						const double Rs = G.Radius(Ds, &Ns);
						if (Rs - SeaR > 60.0)
							Emit(EDmNatureMesh::Shrub, Var, Place(Ds, Rs - 10.0, Yaw, 0.f, 0.f, FVector(Sc)), Burn, Snow * 0.8f, FMath::Clamp(Dry + 0.4f * DryK, 0.f, 1.f));
					}
				}
			}
	}
	else
	{
		// grass tufts: open, vegetated, dry or green
		const double AreaK = CellArea / 1.5;
		for (int32 J = 0; J < C; ++J)
			for (int32 I = 0; I < C; ++I)
			{
				const int32 Gx = Key.X * C + I, Gy = Key.Y * C + J;
				++Out->Candidates;
				const uint32 Hb = Hash3(Gx, Gy, Key.Face ^ FaceSeed, 0x9001u);
				const double U = -1.0 + 2.0 * (Gx + 0.1 + 0.8 * Hash01(Hb)) / Gn;
				const double V = -1.0 + 2.0 * (Gy + 0.1 + 0.8 * Hash01(Hb * 747796405u + 7u)) / Gn;
				const FVector D = DmCube::FromFace(Key.Face, U, V);
				const FVector4f Cn = F.Sample(F.Canopy, D);
				if (Cn.X < 0.05f) continue;
				const FVector4f Cl = F.Sample(F.Climate, D);
				const float Can = FDmNatureFields::CanopyValue(Cn.X, D);
				const float Patchy = Smooth(-0.2f, 0.3f, DmNature::Fbm3(float(D.X * 100000.0 / 9.0), float(D.Y * 100000.0 / 9.0), float(D.Z * 100000.0 / 9.0), 2));
				const float P = 0.55f * Smooth(0.08f, 0.3f, Cn.X) * (1.f - 0.75f * Can) * (1.f - Cl.Z) * (1.f - Cl.W) * (0.35f + 0.65f * Patchy) * float(AreaK) * Dens;
				if (Hash01(Hash3(Gx, Gy, Key.Face, 0x9002u)) >= P) continue;
				FVector Nrm;
				const double Rg = G.Radius(D, &Nrm);
				if (Rg - SeaR < 80.0) continue;
				if (FVector::DotProduct(Nrm, D) < 0.6) continue;
				const float DryK = FMath::Clamp(Cn.W + Smooth(0.4f, 0.15f, Cl.Y), 0.f, 1.f);
				const int32 Var = (Hash01(Hb >> 5) < DryK ? 1 : 0) + 2 * int32(Hash3(Gx, Gy, Key.Face, 0x9003u) & 1u);
				const float Sc = FMath::Lerp(0.7f, 1.35f, Hash01(Hash3(Gx, Gy, Key.Face, 0x9004u)));
				const float Yaw = Hash01(Hash3(Gx, Gy, Key.Face, 0x9005u)) * 2.f * PI;
				Emit(EDmNatureMesh::Grass, Var, Place(D, Rg - 3.0, Yaw, 0.f, 0.f, FVector(Sc)), 0.f, Cl.Z, DryK);
			}
	}
	Out->Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
	return Out;
}
