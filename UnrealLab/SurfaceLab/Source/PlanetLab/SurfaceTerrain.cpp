#include "SurfaceTerrain.h"
#include "SurfaceNoise.h"
#include "Misc/FileHelper.h"
#include "Algo/Reverse.h"

using namespace SurfNoise;

const int32 FSurfaceTerrain::IcoTris[60] = {
	// north cap, band down, band up, south cap (js/core/geo.js ICO_TRIS)
	0, 2, 1, 0, 3, 2, 0, 4, 3, 0, 5, 4, 0, 1, 5,
	1, 2, 6, 2, 3, 7, 3, 4, 8, 4, 5, 9, 5, 1, 10,
	6, 2, 7, 7, 3, 8, 8, 4, 9, 9, 5, 10, 10, 1, 6,
	6, 7, 11, 7, 8, 11, 8, 9, 11, 9, 10, 11, 10, 6, 11,
};

namespace
{
	inline double Sat(double X) { return FMath::Clamp(X, 0.0, 1.0); }
	inline double Smooth(double A, double B, double X) { const double T = Sat((X - A) / (B - A)); return T * T * (3.0 - 2.0 * T); }
	inline FVector3d Nrm(const FVector3d& V) { return V * (1.0 / FMath::Sqrt(V | V)); }
	inline FVector3f Lerp3(const FVector3f& A, const FVector3f& B, double T) { return A + (B - A) * float(T); }
	enum : uint8 { SEA, SHALLOWS, BEACH, DESERT, SAVANNA, GRASSLAND, FOREST, RAINFOREST, TAIGA, TUNDRA, ICE, ROCK, SNOWPEAK, WETLAND, BARREN, SEAICE };
}

bool FSurfaceTerrain::Load(const FString& Path)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() < 20) return false;
	const uint8* P = Bytes.GetData();
	int64 Off = 0;
	auto Rd = [&](void* Dst, int64 Size) { if (Off + Size > Bytes.Num()) return false; FMemory::Memcpy(Dst, P + Off, Size); Off += Size; return true; };
	if (!Rd(&N, 4) || !Rd(&NTri, 4) || !Rd(&SimR, 4) || !Rd(&SeaLevel, 4) || !Rd(&RiverT, 4)) return false;
	if (N <= 0 || NTri != 81920) return false; // the locator assumes level 6
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

void FSurfaceTerrain::Prepare()
{
	// Icosahedron exactly as js/core/geo.js builds it (closed forms).
	{
		const double S5 = FMath::Sqrt(5.0), CH = 2.0 / S5, SH = 1.0 / S5;
		const double C72 = (S5 - 1.0) / 4.0, S72 = FMath::Sqrt(10.0 + 2.0 * S5) / 4.0;
		const double C36 = (S5 + 1.0) / 4.0, S36 = FMath::Sqrt(10.0 - 2.0 * S5) / 4.0;
		const double UpC[5] = { 1, C72, -C36, -C36, C72 }, UpS[5] = { 0, S72, S36, -S36, -S72 };
		const double LoC[5] = { C36, -C72, -1, -C72, C36 }, LoS[5] = { S36, S72, 0, -S72, -S36 };
		Ico[0] = FVector3d(0, 1, 0); Ico[11] = FVector3d(0, -1, 0);
		for (int32 K = 0; K < 5; ++K)
		{
			Ico[1 + K] = FVector3d(CH * UpC[K], SH, CH * UpS[K]);
			Ico[6 + K] = FVector3d(CH * LoC[K], -SH, CH * LoS[K]);
		}
	}

	// Neighbours
	Nbr.SetNum(N);
	for (int32 T = 0; T < NTri; ++T)
		for (int32 E0 = 0; E0 < 3; ++E0)
		{
			const int32 A = Tris[3 * T + E0], B = Tris[3 * T + (E0 + 1) % 3];
			Nbr[A].AddUnique(B); Nbr[B].AddUnique(A);
		}

	// One pass of neighbour smoothing (as the reference lab).
	{
		TArray<float> Sm = Elev;
		for (int32 C = 0; C < N; ++C)
		{
			double S = 0; for (int32 B : Nbr[C]) S += Elev[B];
			Sm[C] = 0.5f * Elev[C] + 0.5f * float(S / Nbr[C].Num());
		}
		Elev = MoveTemp(Sm);
	}

	// Two passes of neighbour smoothing on the climate fields that drive colour (removes the
	// cell-by-cell steps of the classifier; the colour lookup is domain-warped as well).
	Marsh.SetNumZeroed(N);
	for (int32 C = 0; C < N; ++C) Marsh[C] = Biome[C] == WETLAND ? 1.f : 0.f;
	for (TArray<float>* F : { &Veg, &Moist, &Temp, &Marsh })
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			TArray<float> Sm = *F;
			for (int32 C = 0; C < N; ++C)
			{
				double S = 0; for (int32 B : Nbr[C]) S += (*F)[B];
				Sm[C] = 0.5f * (*F)[C] + 0.5f * float(S / Nbr[C].Num());
			}
			*F = MoveTemp(Sm);
		}

	// Least-squares tangent gradient of elevation per cell.
	Grad.SetNumZeroed(N);
	for (int32 C = 0; C < N; ++C)
	{
		const FVector3d Pc(Pos[C]);
		const FVector3d Ax = FMath::Abs(Pc.Y) < 0.9 ? FVector3d(0, 1, 0) : FVector3d(1, 0, 0);
		const FVector3d T1 = Nrm(Ax ^ Pc), T2 = Pc ^ T1;
		double Sxx = 0, Sxy = 0, Syy = 0, Sxe = 0, Sye = 0;
		for (int32 B : Nbr[C])
		{
			const FVector3d D = FVector3d(Pos[B]) - Pc;
			const double X = D | T1, Y = D | T2, De = Elev[B] - Elev[C];
			Sxx += X * X; Sxy += X * Y; Syy += Y * Y; Sxe += X * De; Sye += Y * De;
		}
		const double Det = Sxx * Syy - Sxy * Sxy;
		if (FMath::Abs(Det) < 1e-20) continue;
		const double Gx = (Syy * Sxe - Sxy * Sye) / Det, Gy = (Sxx * Sye - Sxy * Sxe) / Det;
		Grad[C] = T1 * Gx + T2 * Gy;
	}

	// Base elevation: two rounds of Loop subdivision of the (smoothed) sim elevation, level 6 -> 8
	// (400 m spacing). Linear interpolation of this finer grid has no visible facets once relief is
	// added, and unlike a blend of tangent planes it does not terrace (plateaus at cells, steps between).
	{
		const double T0 = FPlatformTime::Seconds();
		TArray<uint32> Tri = Tris;
		TArray<float> Val = Elev;
		for (int32 Lev = 0; Lev < 2; ++Lev)
		{
			const int32 NVo = Val.Num();
			struct FEdge { int32 Mid = -1; int32 Opp[2] = { -1, -1 }; int32 A = 0, B = 0; };
			TMap<uint64, int32> EdgeId; EdgeId.Reserve(Tri.Num());
			TArray<FEdge> Edges; Edges.Reserve(Tri.Num() / 2 + 16);
			auto EdgeOf = [&](uint32 A, uint32 B) -> int32
			{
				const uint64 Key = (uint64(FMath::Min(A, B)) << 32) | uint64(FMath::Max(A, B));
				if (const int32* F = EdgeId.Find(Key)) return *F;
				const int32 Id = Edges.Num();
				FEdge& E = Edges.AddDefaulted_GetRef(); E.A = int32(A); E.B = int32(B);
				EdgeId.Add(Key, Id);
				return Id;
			};
			const int32 NT = Tri.Num() / 3;
			TArray<int32> TE; TE.SetNumUninitialized(NT * 3);
			for (int32 T = 0; T < NT; ++T)
				for (int32 K = 0; K < 3; ++K)
				{
					const uint32 A = Tri[3 * T + K], B = Tri[3 * T + (K + 1) % 3], C = Tri[3 * T + (K + 2) % 3];
					const int32 Ei = EdgeOf(A, B);
					FEdge& E = Edges[Ei];
					if (E.Opp[0] < 0) E.Opp[0] = int32(C); else E.Opp[1] = int32(C);
					TE[3 * T + K] = Ei;
				}
			TArray<float> NewV; NewV.SetNumUninitialized(NVo + Edges.Num());
			TArray<double> Sum; Sum.SetNumZeroed(NVo);
			TArray<int32> Cnt; Cnt.SetNumZeroed(NVo);
			for (FEdge& E : Edges)
			{
				Sum[E.A] += Val[E.B]; Sum[E.B] += Val[E.A]; Cnt[E.A]++; Cnt[E.B]++;
			}
			for (int32 V = 0; V < NVo; ++V)
			{
				const int32 Nn = Cnt[V];
				const double Beta = Nn == 3 ? 3.0 / 16.0 : 3.0 / (8.0 * Nn);
				NewV[V] = float((1.0 - Nn * Beta) * Val[V] + Beta * Sum[V]);
			}
			for (int32 Ei = 0; Ei < Edges.Num(); ++Ei)
			{
				FEdge& E = Edges[Ei];
				E.Mid = NVo + Ei;
				const double Opp = E.Opp[1] >= 0 ? Val[E.Opp[0]] + Val[E.Opp[1]] : 2.0 * Val[E.Opp[0]];
				NewV[E.Mid] = float(0.375 * (Val[E.A] + Val[E.B]) + 0.125 * Opp);
			}
			TArray<uint32> NewT; NewT.SetNumUninitialized(NT * 12);
			for (int32 T = 0, W = 0; T < NT; ++T)
			{
				const uint32 A = Tri[3 * T], B = Tri[3 * T + 1], C = Tri[3 * T + 2];
				const uint32 AB = Edges[TE[3 * T]].Mid, BC = Edges[TE[3 * T + 1]].Mid, CA = Edges[TE[3 * T + 2]].Mid;
				// child order of js/core/geo.js: (a,ab,ca) (b,bc,ab) (c,ca,bc) (ab,bc,ca)
				const uint32 Ch[12] = { A, AB, CA, B, BC, AB, C, CA, BC, AB, BC, CA };
				for (int32 K = 0; K < 12; ++K) NewT[W++] = Ch[K];
			}
			Tri = MoveTemp(NewT);
			Val = MoveTemp(NewV);
		}
		Tris8 = MoveTemp(Tri);
		Elev8 = MoveTemp(Val);
		UE_LOG(LogTemp, Display, TEXT("SurfaceTerrain: base grid level 8, %d vertices, %d triangles, %.0f ms"), Elev8.Num(), Tris8.Num() / 3, (FPlatformTime::Seconds() - T0) * 1000.0);
	}

	// Check that the recursive locator reproduces the simulation's triangle numbering.
	{
		int32 Bad = 0;
		for (int32 T = 0; T < NTri; T += 997)
		{
			const FVector3d Ctr = Nrm(FVector3d(Pos[Tris[3 * T]]) + FVector3d(Pos[Tris[3 * T + 1]]) + FVector3d(Pos[Tris[3 * T + 2]]));
			int32 C[3]; double W[3];
			Locate(Ctr, C, W);
			if (C[0] != int32(Tris[3 * T]) || C[1] != int32(Tris[3 * T + 1]) || C[2] != int32(Tris[3 * T + 2])) ++Bad;
		}
		UE_LOG(LogTemp, Display, TEXT("SurfaceTerrain: locator check, %d mismatches of %d"), Bad, NTri / 997 + 1);
	}

	BuildRivers();
}

void FSurfaceTerrain::Locate(const FVector3d& P, int32 Out[3], double W[3], int32* Out8, double* W8) const
{
	int32 Face = 0; double Best = -1e30;
	for (int32 F = 0; F < 20; ++F)
	{
		const FVector3d& A = Ico[IcoTris[3 * F]]; const FVector3d& B = Ico[IcoTris[3 * F + 1]]; const FVector3d& C = Ico[IcoTris[3 * F + 2]];
		const double S = FMath::Min3(P | (A ^ B), P | (B ^ C), P | (C ^ A));
		if (S > Best) { Best = S; Face = F; }
	}
	FVector3d A = Ico[IcoTris[3 * Face]], B = Ico[IcoTris[3 * Face + 1]], C = Ico[IcoTris[3 * Face + 2]];
	int32 Idx = Face;
	auto Bary = [&P](const FVector3d& A_, const FVector3d& B_, const FVector3d& C_, double Wo[3])
	{
		const double Vol = A_ | (B_ ^ C_);
		double Wa = FMath::Max(0.0, (P | (B_ ^ C_)) / Vol), Wb = FMath::Max(0.0, (P | (C_ ^ A_)) / Vol), Wc = FMath::Max(0.0, (P | (A_ ^ B_)) / Vol);
		const double Sum = FMath::Max(Wa + Wb + Wc, 1e-12);
		Wo[0] = Wa / Sum; Wo[1] = Wb / Sum; Wo[2] = Wc / Sum;
	};
	const int32 Levels = Out8 ? 8 : 6;
	for (int32 L = 0; L < Levels; ++L)
	{
		if (L == 6)
		{
			Out[0] = Tris[3 * Idx]; Out[1] = Tris[3 * Idx + 1]; Out[2] = Tris[3 * Idx + 2];
			Bary(A, B, C, W);
		}
		const FVector3d AB = Nrm(A + B), BC = Nrm(B + C), CA = Nrm(C + A);
		const FVector3d N0 = AB ^ CA, N1 = BC ^ AB, N2 = CA ^ BC;
		int32 Ch;
		if ((P | N0) * (A | N0) >= 0.0) { Ch = 0; B = AB; C = CA; }
		else if ((P | N1) * (B | N1) >= 0.0) { Ch = 1; A = B; B = BC; C = AB; }
		else if ((P | N2) * (C | N2) >= 0.0) { Ch = 2; A = C; B = CA; C = BC; }
		else { Ch = 3; A = AB; B = BC; C = CA; }
		Idx = Idx * 4 + Ch;
	}
	if (Out8)
	{
		Out8[0] = Tris8[3 * Idx]; Out8[1] = Tris8[3 * Idx + 1]; Out8[2] = Tris8[3 * Idx + 2];
		Bary(A, B, C, W8);
	}
	else
	{
		Out[0] = Tris[3 * Idx]; Out[1] = Tris[3 * Idx + 1]; Out[2] = Tris[3 * Idx + 2];
		Bary(A, B, C, W);
	}
}

double FSurfaceTerrain::MapElev(double E) const
{
	E -= SeaLevel;
	if (E >= 0.0) return E * MPerElev;
	const double A = -E;
	return -MPerElev * A * (0.5 + 0.5 * Smooth(0.1, 0.8, A)); // continental shelf: shallows near the coast
}

double FSurfaceTerrain::Height(const FVector3d& Dir, double S, FSurfAttr* At, bool bRivers) const
{
	int32 C[3]; double W[3]; int32 C8[3]; double W8[3];
	Locate(Dir, C, W, C8, W8);
	const double E = W8[0] * Elev8[C8[0]] + W8[1] * Elev8[C8[1]] + W8[2] * Elev8[C8[2]] - SeaLevel;
	auto L = [&](const TArray<float>& F) { return W[0] * F[C[0]] + W[1] * F[C[1]] + W[2] * F[C[2]]; };
	const double Sl = L(Slope);

	// Relief amplitudes: lowland hills everywhere, ridged mountains where the sim has high, steep ground.
	const double M = Sat((E - 0.3) / 2.0) * (0.5 + 0.5 * Sat(Sl / 1.4)); // mountainness
	double RAe, RAr;
	if (E >= 0.0) { RAe = 70.0 + 140.0 * M; RAr = 650.0 * M * M; }
	else { RAe = 45.0 * (1.0 - Sat(-E / 1.2)) + 8.0; RAr = 0.0; }
	// Coasts keep the simulation's outline: relief fades in over the first metres above sea level.
	if (E >= 0.0 && E < 0.25) RAe *= 0.35 + 0.65 * Sat(E / 0.25);

	double DR = 1e9, HW = 0.0, Carve = 0.0, SD = 1e9;
	if (bRivers && RPts.Num()) RiverQuery(Dir, DR, HW, &SD);
	if (DR < 2500.0)
	{
		const double Vf = Smooth(HW + 30.0, 4.0 * HW + 650.0, DR); // flat valley floor along the river
		RAe *= Vf; RAr *= Vf;
		Carve = (2.0 + 0.10 * HW) * (1.0 - Smooth(0.7 * HW, 1.6 * HW, DR));
		Carve *= Smooth(-0.04, 0.02, E); // no channel cut into the sea floor past the mouth
	}
	double Re, Rr;
	Relief(Dir, S, Re, Rr);
	const double FA = FineAmp(RAe, RAr);
	const double Hb = MapElev(E + SeaLevel);
	const double H = Hb + RAe * Re + RAr * Rr + FA * Fine(Dir, S) - Carve;

	if (At)
	{
		At->BaseH = Hb; At->ReliefE = float(RAe); At->ReliefR = float(RAr);
		At->BeachZone = float(1.0 - Smooth(4.0, 18.0, Hb));
		At->RiverW = float(HW);
		At->RiverD = float(FMath::Clamp(SD, -2000.0, 2000.0));
		// Climate fields are read at a domain-warped direction, so biome borders come out organic
		// instead of following the 1.6 km triangles of the simulation grid.
		const FVector3d Wp = Dir * 31.0;
		const FVector3d Wq = Dir * 140.0;
		const FVector3d Warp(Fbm(Wp + FVector3d(1.3, 7.1, 2.9), 3) + 0.35 * Fbm(Wq + FVector3d(4.2, 0.3, 5.5), 2),
			Fbm(Wp + FVector3d(8.4, 2.2, 6.6), 3) + 0.35 * Fbm(Wq + FVector3d(2.7, 9.9, 1.1), 2),
			Fbm(Wp + FVector3d(5.5, 3.3, 0.7), 3) + 0.35 * Fbm(Wq + FVector3d(6.1, 4.4, 8.8), 2));
		int32 Cw[3]; double Ww[3];
		Locate(Nrm(Dir + Warp * 0.022), Cw, Ww);
		auto Lw = [&](const TArray<float>& F) { return Ww[0] * F[Cw[0]] + Ww[1] * F[Cw[1]] + Ww[2] * F[Cw[2]]; };
		const double Tm = Lw(Temp), Mo = Lw(Moist), Vg = Lw(Veg), Hab_ = Lw(Hab), Wa = L(Water);
		const double Wob = 0.06 * Fbm(Dir * 25.0 + FVector3d(9.1), 3);
		const FVector3f Sand(0.66f, 0.56f, 0.40f), Steppe(0.50f, 0.45f, 0.32f), Grass(0.36f, 0.42f, 0.22f),
			Forest(0.13f, 0.21f, 0.10f), Jungle(0.08f, 0.17f, 0.07f), Taiga(0.13f, 0.19f, 0.13f),
			Tundra(0.42f, 0.40f, 0.34f), MarshC(0.19f, 0.25f, 0.15f), Scrub(0.40f, 0.38f, 0.26f);
		FVector3f Bare = Lerp3(Sand, Steppe, Sat(Mo * 1.8 + Wob));
		Bare = Lerp3(Bare, Scrub, Sat(Mo * 2.0 - 0.6));
		FVector3f Green = Lerp3(Grass, Forest, Sat(Vg * 1.4 - 0.35 + Wob));
		Green = Lerp3(Green, Jungle, Sat((Tm - 20.0) / 8.0) * Sat(Mo * 1.5 - 0.5));
		Green = Lerp3(Green, Taiga, Sat((4.0 - Tm) / 8.0));
		FVector3f Col = Lerp3(Bare, Green, Sat(Vg * 1.6 + 0.15 * Mo + Wob));
		const double Wet = Sat(Wa * 6.0) + 0.6 * Lw(Marsh);
		Col = Lerp3(Col, MarshC, FMath::Min(Wet, 0.7));
		Col = Lerp3(Col, Tundra, Sat((-4.0 - Tm) / 8.0 + Wob));
		At->Albedo = Col;
		At->Wet = float(Sat(Wet));
		At->Arid = float(Sat(1.0 - Mo * 1.6) * Sat(1.0 - Vg * 2.0));
		At->Forest = float(Sat(Vg * 1.6 - 0.4 + Wob) * Sat((Tm + 6.0) / 10.0) * (1.0 - Sat(Wet)));
		// Temperature falls with the relief added on top of the cell's mean height (exaggerated lapse).
		const double TAdj = Tm - 0.012 * (H - Hb);
		At->Snow = float(Sat((-7.0 - TAdj) / 7.0));
		const double Cluster = Sat((Fbm(Dir * 70.0 + FVector3d(71.0), 3) - 0.05) * 5.0);
		At->Lights = (H > 3.0) ? float(Sat((Hab_ - 0.5) * 3.0) * Cluster) : 0.f;
	}
	return H;
}

// ---------------------------------------------------------------------------------------------- rivers

void FSurfaceTerrain::RiverQuery(const FVector3d& Dir, double& OutD, double& OutHW, double* OutSigned) const
{
	const FIntVector K(FMath::FloorToInt(Dir.X / RGridCell), FMath::FloorToInt(Dir.Y / RGridCell), FMath::FloorToInt(Dir.Z / RGridCell));
	double Best = 1e30; double BestW = 0.0; int32 BestSeg = -1; double BestT = 0.0;
	for (int32 Dz = -1; Dz <= 1; ++Dz)
		for (int32 Dy = -1; Dy <= 1; ++Dy)
			for (int32 Dx = -1; Dx <= 1; ++Dx)
			{
				const TArray<int32>* Cell = RGrid.Find(K + FIntVector(Dx, Dy, Dz));
				if (!Cell) continue;
				for (int32 Si : *Cell)
				{
					const FRiverPt& A = RPts[Si]; const FRiverPt& B = RPts[Si + 1];
					const FVector3d AB = B.P - A.P;
					const double T = FMath::Clamp(((Dir - A.P) | AB) / FMath::Max(AB | AB, 1e-30), 0.0, 1.0);
					const FVector3d Q = A.P + AB * T - Dir;
					const double D2 = Q | Q;
					if (D2 < Best) { Best = D2; BestW = A.HalfW + (B.HalfW - A.HalfW) * T; BestSeg = Si; BestT = T; }
				}
			}
	OutD = FMath::Sqrt(Best) * DatumM;
	OutHW = BestW;
	if (OutSigned)
	{
		// Signed distance across the river (left negative): linear across a straight reach, so the
		// mesh can interpolate it exactly and the material draws a crisp channel at any vertex spacing.
		double Sd = OutD;
		if (BestSeg >= 0)
		{
			const FVector3d AB = RPts[BestSeg + 1].P - RPts[BestSeg].P;
			const FVector3d C = RPts[BestSeg].P + AB * BestT;
			if ((((AB ^ (Dir - C)) | Dir)) < 0.0) Sd = -Sd;
		}
		*OutSigned = Sd;
	}
}

bool FSurfaceTerrain::RiversNear(const FVector3d& Dir, double Ang) const
{
	if (RGrid.Num() == 0) return false;
	if (Ang > 0.03) return true;
	const double R = Ang + RGridCell;
	const FIntVector Lo(FMath::FloorToInt((Dir.X - R) / RGridCell), FMath::FloorToInt((Dir.Y - R) / RGridCell), FMath::FloorToInt((Dir.Z - R) / RGridCell));
	const FIntVector Hi(FMath::FloorToInt((Dir.X + R) / RGridCell), FMath::FloorToInt((Dir.Y + R) / RGridCell), FMath::FloorToInt((Dir.Z + R) / RGridCell));
	for (int32 Z = Lo.Z; Z <= Hi.Z; ++Z)
		for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
			for (int32 X = Lo.X; X <= Hi.X; ++X)
				if (RGrid.Contains(FIntVector(X, Y, Z))) return true;
	return false;
}

void FSurfaceTerrain::BuildRivers()
{
	const double Sea = SeaLevel;
	TArray<uint8> IsRiver; IsRiver.SetNumZeroed(N);
	for (int32 C = 0; C < N; ++C) IsRiver[C] = (Elev[C] >= Sea && Flow[C] > RiverT) ? 1 : 0;

	// Downstream neighbour of every river cell: a neighbour carrying at least as much water, the lowest one.
	TArray<int32> Down; Down.Init(-1, N);
	for (int32 C = 0; C < N; ++C)
	{
		if (!IsRiver[C]) continue;
		int32 Best = -1; double BestE = 1e30;
		for (int32 B : Nbr[C])
			if (Flow[B] >= Flow[C] && Elev[B] < BestE) { BestE = Elev[B]; Best = B; }
		Down[C] = Best;
	}
	TArray<TArray<int32>> Up; Up.SetNum(N);
	TArray<int32> MainUp; MainUp.Init(-1, N);
	TArray<int32> Mouths;
	for (int32 C = 0; C < N; ++C)
	{
		if (!IsRiver[C]) continue;
		++NumRiverCells;
		const int32 D = Down[C];
		if (D >= 0 && IsRiver[D]) Up[D].Add(C); else Mouths.Add(C);
	}
	for (int32 C = 0; C < N; ++C)
		for (int32 U : Up[C]) if (MainUp[C] < 0 || Flow[U] > Flow[MainUp[C]]) MainUp[C] = U;
	Mouths.Sort([this](int32 A, int32 B) { return Flow[A] > Flow[B]; });

	auto HalfW = [this](double F) { return 5.0 + 4.5 * FMath::Sqrt(FMath::Max(F, 0.0) / RiverT); };
	TArray<int32> CellPt; CellPt.Init(-1, N);

	struct FJob { int32 Start; int32 ParentPt; int32 EndCell; };
	TArray<FJob> Jobs;
	for (int32 M : Mouths) Jobs.Add({ M, -1, Down[M] });
	for (int32 J = 0; J < Jobs.Num(); ++J)
	{
		const FJob Job = Jobs[J];
		TArray<int32> Cells;
		for (int32 C = Job.Start; C >= 0; C = MainUp[C]) Cells.Add(C);
		Algo::Reverse(Cells); // head ... start
		TArray<FVector3d> Ctrl; TArray<double> CFlow;
		for (int32 C : Cells) { Ctrl.Add(FVector3d(Pos[C])); CFlow.Add(Flow[C]); }
		if (Job.ParentPt >= 0) { Ctrl.Add(RPts[Job.ParentPt].P); const double Lf = CFlow.Last(); CFlow.Add(Lf); }
		else if (Job.EndCell >= 0) { Ctrl.Add(FVector3d(Pos[Job.EndCell])); const double Lf = CFlow.Last(); CFlow.Add(Lf); }
		if (Ctrl.Num() < 2) continue;
		// Round off the 60-degree turns of the cell-to-cell path (two Laplacian passes, ends fixed),
		// otherwise rivers read as a hexagonal crack pattern from orbit.
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			TArray<FVector3d> Sm = Ctrl;
			for (int32 I = 1; I + 1 < Ctrl.Num(); ++I) Sm[I] = Nrm(Ctrl[I] * 0.5 + (Ctrl[I - 1] + Ctrl[I + 1]) * 0.25);
			Ctrl = MoveTemp(Sm);
		}

		double Total = 0; for (int32 I = 0; I + 1 < Ctrl.Num(); ++I) Total += FVector3d::Distance(Ctrl[I], Ctrl[I + 1]) * DatumM;
		const int32 Chain = NumChains++;
		const int32 First = RPts.Num();
		double Arc = 0;
		for (int32 I = 0; I + 1 < Ctrl.Num(); ++I)
		{
			const FVector3d P0 = I > 0 ? Ctrl[I - 1] : Ctrl[I] * 2.0 - Ctrl[I + 1];
			const FVector3d P1 = Ctrl[I], P2 = Ctrl[I + 1];
			const FVector3d P3 = I + 2 < Ctrl.Num() ? Ctrl[I + 2] : Ctrl[I + 1] * 2.0 - Ctrl[I];
			const double SegLen = FVector3d::Distance(P1, P2) * DatumM;
			const int32 NSub = FMath::Max(2, FMath::CeilToInt(SegLen / 25.0));
			if (I < Cells.Num()) CellPt[Cells[I]] = RPts.Num();
			for (int32 K = 0; K < NSub; ++K)
			{
				const double T = double(K) / NSub, T2 = T * T, T3 = T2 * T;
				FVector3d Q = 0.5 * ((2.0 * P1) + (P2 - P0) * T + (2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3) * T2 + (3.0 * P1 - P0 - 3.0 * P2 + P3) * T3);
				const FVector3d Tan = 0.5 * ((P2 - P0) + (2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3) * (2.0 * T) + (3.0 * P1 - P0 - 3.0 * P2 + P3) * (3.0 * T2));
				Q = Nrm(Q);
				const double F = FMath::Lerp(CFlow[I], CFlow[I + 1], T);
				const double Hw = HalfW(F);
				// Meanders: a sideways offset from 1D noise along the arc, tapered to zero at both ends of the chain.
				const double Lam = FMath::Clamp(14.0 * Hw, 220.0, 1400.0);
				const double Amp = FMath::Min(6.0 * Hw + 60.0, 0.30 * Lam) * Smooth(0.0, 900.0, Arc) * Smooth(0.0, 900.0, Total - Arc);
				const double Nz = 1.3 * Noised(FVector3d(Arc / Lam, Chain * 7.31, 0.37)).X + 0.2 * Noised(FVector3d(Arc / Lam * 2.3, Chain * 7.31 + 3.0, 1.1)).X;
				// plus a slow swing (2.5-4 km) that breaks the 1.6 km cell-to-cell straight reaches
				const double Lam2 = 2600.0 + 1400.0 * Sat(Hw / 40.0);
				const double Amp2 = 260.0 * Smooth(0.0, 1500.0, Arc) * Smooth(0.0, 1500.0, Total - Arc);
				const double Nz2 = 1.4 * Noised(FVector3d(Arc / Lam2, Chain * 5.13, 3.3)).X;
				const FVector3d Side = Nrm(Q ^ Tan);
				Q = Nrm(Q + Side * ((Amp * Nz + Amp2 * Nz2) / DatumM));
				// width breathes along the course (pools, narrows)
				RPts.Add({ Q, float(Hw * (1.0 + 0.25 * Noised(FVector3d(Arc / 180.0, Chain * 3.17, 7.7)).X)) });
				Arc += SegLen / NSub;
			}
		}
		RPts.Add({ Nrm(Ctrl.Last()), float(HalfW(CFlow.Last())) });
		if (Cells.Num() == Ctrl.Num()) CellPt[Cells.Last()] = RPts.Num() - 1;
		for (int32 I = First; I + 1 < RPts.Num(); ++I) RSeg.Add(I);
		// Tributaries join this chain where they meet it.
		for (int32 C : Cells)
			for (int32 U : Up[C])
				if (U != MainUp[C] && CellPt[C] >= 0) Jobs.Add({ U, CellPt[C], -1 });
	}
	for (int32 Si = 0; Si < RSeg.Num(); ++Si)
	{
		const FVector3d Mid = (RPts[RSeg[Si]].P + RPts[RSeg[Si] + 1].P) * 0.5;
		RGrid.FindOrAdd(FIntVector(FMath::FloorToInt(Mid.X / RGridCell), FMath::FloorToInt(Mid.Y / RGridCell), FMath::FloorToInt(Mid.Z / RGridCell))).Add(RSeg[Si]);
	}
	UE_LOG(LogTemp, Display, TEXT("SurfaceTerrain: %d river cells, %d chains, %d points, %d grid cells"), NumRiverCells, NumChains, RPts.Num(), RGrid.Num());
}
