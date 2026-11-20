// DEMIURGE — Game/Planet/DmPlanetTerrain.cpp : see the header. Owner: C1 planet.

#include "Game/Planet/DmPlanetTerrain.h"
#include "Game/Planet/DmPlanetNoise.h"
#include "Game/Planet/DmPlanetMaps.h"
#include "Sim/State.h"
#include "Game/Core/DmScenario.h"
#include "Sim/Hydro.h"
#include "HAL/PlatformTime.h"
#include "Algo/Reverse.h"

using namespace DmPlanetNoise;

DEFINE_LOG_CATEGORY_STATIC(LogDmPlanetTerrain, Log, All);

const int32 FDmGeoLocator::IcoTris[60] = {
	// north cap, band down, band up, south cap (js/core/geo.js ICO_TRIS = dm::Geo level 0)
	0, 2, 1, 0, 3, 2, 0, 4, 3, 0, 5, 4, 0, 1, 5,
	1, 2, 6, 2, 3, 7, 3, 4, 8, 4, 5, 9, 5, 1, 10,
	6, 2, 7, 7, 3, 8, 8, 4, 9, 9, 5, 10, 10, 1, 6,
	6, 7, 11, 7, 8, 11, 8, 9, 11, 9, 10, 11, 10, 6, 11,
};

namespace
{
	inline FVector3d Nrm(const FVector3d& V) { return V * (1.0 / FMath::Sqrt(FMath::Max(V | V, 1e-300))); }
	inline FVector3d SimToUE(const FVector3d& V) { return FVector3d(V.X, V.Z, V.Y); }
}

// ================================================================================================ locator

bool FDmGeoLocator::Build(const dm::World& W)
{
	if (!W.geo || W.geo->n <= 12 || W.geo->nTri <= 0) return false;
	const dm::Geo& G = *W.geo;
	L = G.L; N = G.n; NTri = G.nTri;
	Pos.SetNumUninitialized(N);
	for (int32 C = 0; C < N; ++C) Pos[C] = Nrm(FVector3d(G.pos[size_t(3 * C)], G.pos[size_t(3 * C + 1)], G.pos[size_t(3 * C + 2)]));
	Tris.SetNumUninitialized(NTri * 3);
	for (int32 I = 0; I < NTri * 3; ++I) Tris[I] = G.tris[size_t(I)];
	Nbr.SetNumUninitialized(N * 6);
	NbrN.SetNumUninitialized(N);
	for (int32 C = 0; C < N; ++C)
	{
		NbrN[C] = G.nbrN[size_t(C)];
		for (int32 K = 0; K < 6; ++K) Nbr[6 * C + K] = G.nbr[size_t(6 * C + K)];
	}
	// Icosahedron exactly as js/core/geo.js builds it (closed forms, double precision).
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
		IcoMaxErr = 0.0;
		for (int32 K = 0; K < 12; ++K) IcoMaxErr = FMath::Max(IcoMaxErr, (Ico[K] - Pos[K]).Length());
	}
	// The recursive locator must reproduce the grid's triangle numbering.
	CheckMismatches = 0; CheckCount = 0;
	for (int32 T = 0; T < NTri; T += 97)
	{
		const FVector3d Ctr = Nrm(Pos[Tris[3 * T]] + Pos[Tris[3 * T + 1]] + Pos[Tris[3 * T + 2]]);
		int32 C[3]; double Wt[3];
		Locate(Ctr, C, Wt);
		++CheckCount;
		if (C[0] != int32(Tris[3 * T]) || C[1] != int32(Tris[3 * T + 1]) || C[2] != int32(Tris[3 * T + 2])) ++CheckMismatches;
	}
	return true;
}

static void Bary(const FVector3d& P, const FVector3d& A, const FVector3d& B, const FVector3d& C, double Wo[3])
{
	const double Vol = A | (B ^ C);
	const double Wa = FMath::Max(0.0, (P | (B ^ C)) / Vol), Wb = FMath::Max(0.0, (P | (C ^ A)) / Vol), Wc = FMath::Max(0.0, (P | (A ^ B)) / Vol);
	const double Sum = FMath::Max(Wa + Wb + Wc, 1e-12);
	Wo[0] = Wa / Sum; Wo[1] = Wb / Sum; Wo[2] = Wc / Sum;
}

void FDmGeoLocator::LocateFine(const FVector3d& P, const TArray<uint32>& FineTris, int32 Extra, int32 C[3], double Wt[3], int32 Cf[3], double Wf[3], FVector3d* OutFineCorners) const
{
	int32 Face = 0; double Best = -1e30;
	for (int32 F = 0; F < 20; ++F)
	{
		const FVector3d& A = Ico[IcoTris[3 * F]]; const FVector3d& B = Ico[IcoTris[3 * F + 1]]; const FVector3d& Cc = Ico[IcoTris[3 * F + 2]];
		const double S = FMath::Min3(P | (A ^ B), P | (B ^ Cc), P | (Cc ^ A));
		if (S > Best) { Best = S; Face = F; }
	}
	FVector3d A = Ico[IcoTris[3 * Face]], B = Ico[IcoTris[3 * Face + 1]], Cc = Ico[IcoTris[3 * Face + 2]];
	int64 Idx = Face;
	const int32 Levels = L + FMath::Max(Extra, 0);
	for (int32 Lv = 0; Lv <= Levels; ++Lv)
	{
		if (Lv == L)
		{
			const int64 T = FMath::Clamp<int64>(Idx, 0, NTri - 1);
			C[0] = int32(Tris[3 * T]); C[1] = int32(Tris[3 * T + 1]); C[2] = int32(Tris[3 * T + 2]);
			Bary(P, A, B, Cc, Wt);
		}
		if (Lv == Levels) break;
		const FVector3d AB = Nrm(A + B), BC = Nrm(B + Cc), CA = Nrm(Cc + A);
		const FVector3d N0 = AB ^ CA, N1 = BC ^ AB, N2 = CA ^ BC;
		int32 Ch;
		if ((P | N0) * (A | N0) >= 0.0) { Ch = 0; B = AB; Cc = CA; }
		else if ((P | N1) * (B | N1) >= 0.0) { Ch = 1; A = B; B = BC; Cc = AB; }
		else if ((P | N2) * (Cc | N2) >= 0.0) { Ch = 2; A = Cc; B = CA; Cc = BC; }
		else { Ch = 3; A = AB; B = BC; Cc = CA; }
		Idx = Idx * 4 + Ch;
	}
	if (Extra > 0 && Cf && Wf)
	{
		const int64 NF = FineTris.Num() / 3;
		const int64 T = FMath::Clamp<int64>(Idx, 0, NF - 1);
		Cf[0] = int32(FineTris[3 * T]); Cf[1] = int32(FineTris[3 * T + 1]); Cf[2] = int32(FineTris[3 * T + 2]);
		Bary(P, A, B, Cc, Wf);
		if (OutFineCorners) { OutFineCorners[0] = A; OutFineCorners[1] = B; OutFineCorners[2] = Cc; }
	}
}

void FDmGeoLocator::Locate(const FVector3d& P, int32 C[3], double Wt[3]) const
{
	static const TArray<uint32> None;
	LocateFine(P, None, 0, C, Wt, nullptr, nullptr);
}

int32 FDmGeoLocator::NearestCell(const FVector3d& P) const
{
	int32 C[3]; double Wt[3];
	Locate(P, C, Wt);
	return Wt[0] >= Wt[1] && Wt[0] >= Wt[2] ? C[0] : (Wt[1] >= Wt[2] ? C[1] : C[2]);
}

// ================================================================================================ capture / prepare

void FDmPlanetTerrain::Capture(const dm::World& W, const TSharedPtr<const FDmGeoLocator, ESPMode::ThreadSafe>& InLoc)
{
	Loc = InLoc;
	const int32 N = W.n;
	// in 0.5 m steps: a live world's sea level drifts every tick (ice, climate), and any change at all redraws
	// every patch of the planet (ChangedCells: global)
	SeaLevel = FMath::RoundToDouble(W.seaLevel / 0.0025) * 0.0025;
	SeaM = SeaLevel * MPerElev;
	// (wave 3) the ground's datum: the sea the world was calibrated with (vars.seaBase) while the sea stands above
	// it. A rising sea then floods the ground where it lies (old coast, drowned hills, the depth growing toward the
	// old shore) instead of re-mapping everything below the new sea onto a flat, shallow shelf. Below the base (ice
	// ages, genesis before calibration) the datum is the sea itself, as before.
	RefLevel = SeaLevel;
	static const bool bNoDatum = FDmShotArgs::Param(TEXT("planetnodatum"));   // A/B: the old mapping (ground re-shaped against the sea)
	if (!bNoDatum && W.vars.seaBase.has_value() && FMath::IsFinite(*W.vars.seaBase))
		RefLevel = FMath::Min(SeaLevel, FMath::RoundToDouble(*W.vars.seaBase / 0.0025) * 0.0025);
	RefM = RefLevel * MPerElev;
	Elev.SetNumUninitialized(N); Water.SetNumUninitialized(N); Slope.SetNumUninitialized(N); Flow.SetNumUninitialized(N);
	for (int32 C = 0; C < N; ++C)
	{
		const float E = W.elev[size_t(C)];
		Elev[C] = FMath::IsFinite(E) ? E : 0.f;
		const float Wa = W.water[size_t(C)];
		Water[C] = FMath::IsFinite(Wa) ? FMath::Max(Wa, 0.f) : 0.f;
		const float Sl = W.slope[size_t(C)];
		Slope[C] = FMath::IsFinite(Sl) ? Sl : 0.f;
		const float F = W.flow[size_t(C)];
		Flow[C] = FMath::IsFinite(F) ? FMath::Max(F, 0.f) : 0.f;
	}
	RiverT = FMath::Max(1e-3, dm::hydro::riverT(W));
	// roads: the same smoothed network the road map uses, kept in the sim frame for the per-vertex query
	Road.SetNumUninitialized(N);
	for (int32 C = 0; C < N; ++C) Road[C] = W.road[size_t(C)];
	{
		TArray<FDmRoadSeg> Segs;
		FDmPlanetMaps::CaptureRoads(W, Segs);
		RoadSegs.Reset(Segs.Num());
		for (const FDmRoadSeg& S : Segs) RoadSegs.Add({ SimToUE(S.A), SimToUE(S.B), S.Level });   // the swap is its own inverse
	}
	// Rivers: the flow is quantised to quarter octaves of the river threshold (river widths in ~9 % steps), so
	// the discharge jitter of a live world does not redraw them; each river keeps a hash of its cells and
	// quantised flows, so a change rebuilds only the patches along the rivers that changed (ChangedCells).
	RiverCells.Reset(); RiverFlow.Reset(); RiverHashes.Reset();
	const float RiverTq = float(FMath::Pow(2.0, FMath::RoundToDouble(FMath::Log2(RiverT) * 4.0) / 4.0));
	uint32 H = 2166136261u;
	H = (H ^ uint32(FMath::RoundToInt(FMath::Log2(RiverT) * 4.0) + 1000)) * 16777619u;
	for (const dm::River& R : W.rivers)
	{
		if (R.cells.size() < 2) continue;
		TArray<int32>& Cs = RiverCells.AddDefaulted_GetRef();
		TArray<float>& Fs = RiverFlow.AddDefaulted_GetRef();
		uint32 Hr = 2166136261u;
		for (size_t I = 0; I < R.cells.size(); ++I)
		{
			const int32 C = R.cells[I];
			if (C < 0 || C >= N) continue;
			Cs.Add(C);
			float F = I < R.flow.size() ? R.flow[I] : Flow[C];
			F = FMath::IsFinite(F) ? FMath::Max(F, 0.f) : 0.f;
			const int32 Q = F > 1e-3f ? FMath::RoundToInt(FMath::Log2(F / RiverTq) * 4.0f) : -999;
			Fs.Add(Q > -999 ? RiverTq * FMath::Pow(2.f, Q / 4.f) : 0.f);
			Hr = (Hr ^ uint32(C)) * 16777619u;
			Hr = (Hr ^ uint32(Q + 2000)) * 16777619u;
		}
		RiverHashes.Add(Hr);
		H = (H ^ Hr) * 16777619u;
	}
	RiverTqM = RiverTq;
	RiverHash = H;
}

double FDmPlanetTerrain::MapElev(double ERel) const
{
	if (ERel >= 0.0) return ERel * MPerElev;
	const double A = -ERel;
	return -MPerElev * A * (0.5 + 0.5 * Smooth(0.1, 0.8, A)); // continental shelf: shallows near the coast
}

void FDmPlanetTerrain::Prepare()
{
	const double T0 = FPlatformTime::Seconds();
	if (!Loc.IsValid()) return;
	const FDmGeoLocator& G = *Loc;
	const int32 N = G.N;
	auto IsLake = [this](int32 C) { return Elev[C] >= SeaLevel && Water[C] > 0.03f; };

	// One pass of neighbour smoothing on elevation (as the lab), keeping lake cells at their bed.
	ElevS = Elev;
	for (int32 C = 0; C < N; ++C)
	{
		const int32 Deg = G.NbrN[C];
		if (!Deg) continue;
		double S = 0; for (int32 K = 0; K < Deg; ++K) S += Elev[G.Nbr[6 * C + K]];
		ElevS[C] = 0.5f * Elev[C] + 0.5f * float(S / Deg);
	}

	// Continental shelf: the sim's sea floor drops to its full depth one cell (1.9 km) off the coast, which
	// leaves no shallow water to see from orbit. Sea cells within three hops of land are held up to a
	// shelf (about 25 / 60 / 110 m), so coasts and islands sit in a band of shallow, turquoise water.
	{
		TArray<uint8> Hop; Hop.Init(255, N);
		TArray<int32> Front, Next;
		for (int32 C = 0; C < N; ++C) if (Elev[C] >= RefLevel) { Hop[C] = 0; Front.Add(C); }
		for (uint8 D = 1; D <= 3 && Front.Num(); ++D)
		{
			Next.Reset();
			for (int32 C : Front)
				for (int32 K = 0; K < G.NbrN[C]; ++K)
				{
					const int32 M = G.Nbr[6 * C + K];
					if (Hop[M] == 255) { Hop[M] = D; Next.Add(M); }
				}
			Swap(Front, Next);
		}
		static const float ShelfMax[4] = { 0.f, 0.22f, 0.45f, 0.75f };
		for (int32 C = 0; C < N; ++C)
			if (Elev[C] < RefLevel && Hop[C] >= 1 && Hop[C] <= 3)
				ElevS[C] = FMath::Max(ElevS[C], float(RefLevel) - ShelfMax[Hop[C]]);
		// two smoothing passes over the shelf and its edge, so the shelf break is a slope, not a step that
		// outlines the cells (sea cells only, so the coastline itself does not move)
		for (int32 Pass = 0; Pass < 3; ++Pass)
		{
			TArray<float> Sm = ElevS;
			for (int32 C = 0; C < N; ++C)
			{
				if (Elev[C] >= RefLevel || Hop[C] < 1) continue;   // every sea cell near land (255 = beyond 3 hops: the shelf break)
				const int32 Deg = G.NbrN[C];
				double Sum = 0; int32 Cnt = 0;
				for (int32 K = 0; K < Deg; ++K) { const int32 M = G.Nbr[6 * C + K]; if (Elev[M] < RefLevel) { Sum += ElevS[M]; ++Cnt; } }
				if (Cnt) Sm[C] = FMath::Min(0.5f * ElevS[C] + 0.5f * float(Sum / Cnt), float(RefLevel) - 0.05f);
			}
			ElevS = MoveTemp(Sm);
		}
	}

	// Lakes: surface level per lake cell (m above datum), mask smoothed once.
	LakeLevelM.Init(-1.0e9f, N);
	LakeMask.Init(0.f, N);
	for (int32 C = 0; C < N; ++C)
		if (IsLake(C))
		{
			LakeLevelM[C] = float(RefM + MapElev(double(Elev[C]) + double(Water[C]) - RefLevel));
			LakeMask[C] = 1.f;
			// a lake bed is below its surface: keep the smoothed bed below the lake level
			ElevS[C] = FMath::Min(ElevS[C], Elev[C] + 0.6f * Water[C]);
		}
	{
		TArray<float> Sm = LakeMask;
		for (int32 C = 0; C < N; ++C)
		{
			const int32 Deg = G.NbrN[C];
			if (!Deg || LakeMask[C] >= 1.f) continue;
			double S = 0; for (int32 K = 0; K < Deg; ++K) S += LakeMask[G.Nbr[6 * C + K]];
			Sm[C] = float(S / Deg) * 0.8f;
		}
		LakeMask = MoveTemp(Sm);
	}
	// Lake level near a lake (lake cells and their first ring), interpolated in Height over the corners that
	// have one: continuous across triangle edges, so the bed never steps where the triangles around a lake
	// end (the max over the corners of each triangle did, and a lake read as the triangle fan of its cells).
	LakeNearM = LakeLevelM;
	for (int32 C = 0; C < N; ++C)
	{
		if (LakeLevelM[C] > -1e8f) continue;
		float Best = -1.0e9f;
		for (int32 K = 0; K < G.NbrN[C]; ++K) Best = FMath::Max(Best, LakeLevelM[G.Nbr[6 * C + K]]);
		LakeNearM[C] = Best;
	}

	// Base elevation: two rounds of Loop subdivision of the smoothed sim elevation, level L -> L+2
	// (linear interpolation of the finer grid has no visible facets once relief is added, and unlike a
	// blend of tangent planes it does not terrace).
	{
		TArray<uint32> Tri = G.Tris;
		TArray<float> Val = ElevS;
		TArray<FVector3f> Pv; Pv.SetNumUninitialized(N);   // vertex directions (sim frame), for the gradients below
		for (int32 C = 0; C < N; ++C) Pv[C] = FVector3f(G.Pos[C]);
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
			for (const FEdge& E : Edges) { Sum[E.A] += Val[E.B]; Sum[E.B] += Val[E.A]; Cnt[E.A]++; Cnt[E.B]++; }
			for (int32 V = 0; V < NVo; ++V)
			{
				const int32 Nn = FMath::Max(Cnt[V], 1);
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
			Pv.SetNum(NVo + Edges.Num());
			for (int32 Ei = 0; Ei < Edges.Num(); ++Ei) Pv[NVo + Ei] = (Pv[Edges[Ei].A] + Pv[Edges[Ei].B]).GetSafeNormal();
			TArray<uint32> NewT; NewT.SetNumUninitialized(NT * 12);
			for (int32 T = 0, Wi = 0; T < NT; ++T)
			{
				const uint32 A = Tri[3 * T], B = Tri[3 * T + 1], C = Tri[3 * T + 2];
				const uint32 AB = uint32(Edges[TE[3 * T]].Mid), BC = uint32(Edges[TE[3 * T + 1]].Mid), CA = uint32(Edges[TE[3 * T + 2]].Mid);
				// child order of js/core/geo.js: (a,ab,ca) (b,bc,ab) (c,ca,bc) (ab,bc,ca)
				const uint32 Ch[12] = { A, AB, CA, B, BC, AB, C, CA, BC, AB, BC, CA };
				for (int32 K = 0; K < 12; ++K) NewT[Wi++] = Ch[K];
			}
			Tri = MoveTemp(NewT);
			Val = MoveTemp(NewV);
		}
		Tris2 = MoveTemp(Tri);
		Elev2 = MoveTemp(Val);

		// Per-vertex gradient of the base (area-weighted mean of the faces around it, in the tangent plane).
		// Height blends the linear interpolation with the vertices' tangent planes (Phong-style): linear
		// interpolation of the 475 m triangles has a crease at every edge, and on big mountain flanks under a
		// low sun the creases showed as large triangular facets of light and shade.
		const int32 NV2 = Elev2.Num();
		TArray<FVector3f> Gs; Gs.Init(FVector3f::ZeroVector, NV2);
		TArray<float> Ws; Ws.Init(0.f, NV2);
		const int32 NT2 = Tris2.Num() / 3;
		for (int32 T = 0; T < NT2; ++T)
		{
			const uint32 Ia = Tris2[3 * T], Ib = Tris2[3 * T + 1], Ic = Tris2[3 * T + 2];
			const FVector3f Pa = Pv[Ia], E1 = Pv[Ib] - Pa, E2 = Pv[Ic] - Pa;
			const FVector3f Nf = E1 ^ E2;
			const float N2 = Nf.SizeSquared();
			if (N2 < 1e-20f) continue;
			const FVector3f Gf = ((E2 ^ Nf) * (Elev2[Ib] - Elev2[Ia]) + (Nf ^ E1) * (Elev2[Ic] - Elev2[Ia])) / N2;
			const float A = FMath::Sqrt(N2);
			for (uint32 I : { Ia, Ib, Ic }) { Gs[I] += Gf * A; Ws[I] += A; }
		}
		Grad2.SetNumUninitialized(NV2);
		for (int32 V = 0; V < NV2; ++V)
		{
			FVector3f Gv = Ws[V] > 0.f ? Gs[V] / Ws[V] : FVector3f::ZeroVector;
			Gv -= Pv[V] * (Gv | Pv[V]);
			Grad2[V] = Gv;
		}
	}

	float Lo = 1e9f, Hi = -1e9f;
	for (int32 C = 0; C < N; ++C) { Lo = FMath::Min(Lo, Elev[C]); Hi = FMath::Max(Hi, Elev[C]); }
	MinH = RefM + MapElev(double(Lo) - RefLevel) - 200.0;
	MaxH = RefM + MapElev(double(Hi) - RefLevel) + 1200.0;

	BuildRivers();
	RoadGrid.Reset();
	for (int32 Si = 0; Si < RoadSegs.Num(); ++Si)
	{
		const FVector3d Mid = (RoadSegs[Si].A + RoadSegs[Si].B) * 0.5;
		RoadGrid.FindOrAdd(FIntVector(FMath::FloorToInt(Mid.X / RiverGridCell), FMath::FloorToInt(Mid.Y / RiverGridCell), FMath::FloorToInt(Mid.Z / RiverGridCell))).Add(Si);
	}
	PrepareMs = (FPlatformTime::Seconds() - T0) * 1000.0;
}

bool FDmPlanetTerrain::ChangedCells(const FDmPlanetTerrain& O, TArray<int32>& Out, TArray<int32>* OutRoadOnly) const
{
	Out.Reset();
	if (OutRoadOnly) OutRoadOnly->Reset();
	if (O.Elev.Num() != Elev.Num() || O.SeaLevel != SeaLevel || O.RefLevel != RefLevel || O.RiverTqM != RiverTqM || O.Loc != Loc) return false;
	if (O.RiverHash != RiverHash)
	{
		// the cells of every river that is not in both sets (rivers are compared whole: a changed cell shifts the
		// meanders of everything downstream of it); a delta or a tributary ending on it is within the margin
		TSet<uint32> Old(O.RiverHashes), New(RiverHashes);
		TSet<int32> Cells;
		for (int32 R = 0; R < RiverHashes.Num(); ++R) if (!Old.Contains(RiverHashes[R])) Cells.Append(RiverCells[R]);
		for (int32 R = 0; R < O.RiverHashes.Num(); ++R) if (!New.Contains(O.RiverHashes[R])) Cells.Append(O.RiverCells[R]);
		Out.Append(Cells.Array());
	}
	TBitArray<> InOut(false, Elev.Num());
	for (int32 C : Out) InOut[C] = true;
	// Only what moves the mesh: elevation, slope (mountain relief) and roads exactly; water only where it makes
	// or changes a lake (Height reads water through IsLake and the lake level). A live world rewrites water
	// every tick, and comparing it exactly made every rain shower rebuild the patches under it.
	auto LakeOf = [](const FDmPlanetTerrain& T, int32 C) { return T.Elev[C] >= T.SeaLevel && T.Water[C] > 0.03f ? T.Elev[C] + T.Water[C] : -1.0e9f; };
	for (int32 C = 0; C < Elev.Num(); ++C)
	{
		bool bCh = O.Elev[C] != Elev[C] || FMath::Abs(O.Slope[C] - Slope[C]) > 0.01f;
		if (!bCh)
		{
			const float La = LakeOf(O, C), Lb = LakeOf(*this, C);
			bCh = (La > -1e8f) != (Lb > -1e8f) || FMath::Abs(La - Lb) > 0.0025f;   // 0.5 m of lake level
		}
		if (bCh) { if (!InOut[C]) Out.Add(C); }
		// a road only changes the vertex colours of fine patches (BuildPatch: spacing < 40 m) near it
		else if (O.Road[C] != Road[C]) { if (OutRoadOnly) OutRoadOnly->Add(C); else Out.Add(C); }
	}
	return true;
}

// ================================================================================================ height

double FDmPlanetTerrain::LakeAt(const FVector3d& DirSim) const
{
	// the same lake region the mesh draws water in (Height's noisy shoreline)
	if (!Loc.IsValid()) return -1e9;
	FDmSurfAttr A;
	Height(DirSim, 0.5, &A, false);
	return A.LakeM;
}

double FDmPlanetTerrain::Height(const FVector3d& Dir, double S, FDmSurfAttr* At, bool bRivers) const
{
	if (!Loc.IsValid() || Elev2.Num() == 0) return SeaM;
	int32 C[3]; double W[3]; int32 C2[3]; double W2[3]; FVector3d P2[3];
	Loc->LocateFine(Dir, Tris2, 2, C, W, C2, W2, P2);
	// base: the fine triangle's linear interpolation blended 3/4 toward the corners' tangent planes
	double E = 0.0;
	{
		double Lin = 0.0, Ph = 0.0;
		for (int32 K = 0; K < 3; ++K)
		{
			const double Ek = Elev2[C2[K]];
			Lin += W2[K] * Ek;
			const FVector3f& Gk = Grad2[C2[K]];
			Ph += W2[K] * (Ek + double(Gk.X) * (Dir.X - P2[K].X) + double(Gk.Y) * (Dir.Y - P2[K].Y) + double(Gk.Z) * (Dir.Z - P2[K].Z));
		}
		E = FMath::Lerp(Lin, Ph, 0.75) - RefLevel;   // above the datum (the base sea; see Capture)
	}
	auto Lc = [&](const TArray<float>& F) { return W[0] * F[C[0]] + W[1] * F[C[1]] + W[2] * F[C[2]]; };
	const double Sl = Lc(Slope);
	const FVector3d Up = SimToUE(Dir);
	// Lakes: the level is interpolated over the corners that have one (continuous across triangle edges), and
	// the mask gets a noisy, multi-scale shoreline so a lake is never the hexagon of its sim cells.
	double Lm = Lc(LakeMask);
	double LakeS = -1.0e9;
	{
		double Ws = 0.0, Ls = 0.0;
		for (int32 K = 0; K < 3; ++K) if (LakeNearM[C[K]] > -1e8f) { Ws += W[K]; Ls += W[K] * LakeNearM[C[K]]; }
		if (Ws > 1e-9) LakeS = Ls / Ws;
		else Lm = 0.0;
		// (the wobble fades in with the raw mask, so it never jumps where the mask region ends)
		if (Lm > 0.0)
			Lm += Sat(Lm / 0.12) * (0.30 * Fbm(Up * (DatumM / 1600.0) + FVector3d(3.7, 1.9, 8.2), 3) + 0.12 * Fbm(Up * (DatumM / 420.0) + FVector3d(9.1, 4.4, 2.6), 2));
	}

	// Relief amplitudes: lowland hills everywhere, ridged mountains where the sim has high, steep ground.
	const double M = Sat((E - 0.3) / 2.0) * (0.5 + 0.5 * Sat(Sl / 1.4));   // mountainness
	double RAe, RAr;
	// high ranges get taller, sharper ridges (up to ~1.2 km of ridged relief over 10 elevation units)
	if (E >= 0.0) { RAe = 70.0 + 140.0 * M; RAr = (650.0 + 550.0 * Sat((E - 4.0) / 6.0)) * M * M; }
	// Sea floor: continuous with the land at the sim's coastline (E = 0), where the coastal fade below leaves
	// 0.35 x 70 m. (It used to start at 53 m: a step of (53 - 24.5) m x the noise along every E = 0 contour,
	// a dashed cliff line across coastal plains and shallows.)
	else { RAe = FMath::Lerp(24.5, 45.0 * (1.0 - Sat(-E / 1.2)) + 8.0, Sat(-E / 0.25)); RAr = 0.0; }
	// Coasts keep the simulation's outline: relief fades in over the first metres above sea level.
	if (E >= 0.0 && E < 0.25) RAe *= 0.35 + 0.65 * Sat(E / 0.25);
	// (wave 3) a risen sea: the new coast keeps the simulation's outline the same way (relief faded over 0.25 units
	// either side of it); between the two coasts the drowned land keeps its hills, which stand out as islands and
	// headlands with flooded valleys between them
	const double Rise = SeaLevel - RefLevel;
	if (Rise > 0.01 && E >= 0.0)
	{
		const double Ec = FMath::Abs(E - Rise);
		if (Ec < 0.25) RAe *= 0.35 + 0.65 * Sat(Ec / 0.25);
	}
	// Lakes keep a calm, flat basin.
	const double LakeQ = Smooth(0.15, 0.55, Lm);
	RAe *= 1.0 - 0.9 * LakeQ; RAr *= 1.0 - LakeQ;

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
	Relief(Up, S, Re, Rr);
	const double FA = FineAmp(RAe, RAr);
	const double Hb = RefM + MapElev(E);
	double H = Hb + RAe * Re + RAr * Rr + FA * Fine(Up, S) - Carve;
	if (LakeS > -1e8)
	{
		// lake bed: below the surface wherever the lake mask says "inside"
		// lakes deepen quickly off the shore (a few metres at the edge, 30+ m inside): dark water, not a
		// bright turquoise film over a shallow bed
		const double Bed = LakeS - (2.5 + 32.0 * Sat((Lm - 0.4) / 0.6));
		H = FMath::Lerp(H, FMath::Min(H, Bed), Smooth(0.30, 0.62, Lm));
	}

	if (At)
	{
		At->BaseH = Hb; At->ReliefE = float(RAe); At->ReliefR = float(RAr);
		At->BeachZone = float(1.0 - Smooth(4.0, 18.0, Hb - SeaM));
		At->RiverW = float(HW);
		At->RiverD = float(FMath::Clamp(SD, -2000.0, 2000.0));
		At->DH = float(H - Hb);
		// lake water only inside the (noisy) shoreline region: outside it the ground keeps its relief, and low
		// ground there must not flood
		const bool bLakeHere = LakeS > -1e8 && Lm > 0.22;
		At->LakeM = bLakeHere ? float(LakeS) : -1.0e9f;
		// (outside the lake region, ground below the level is a dry hollow, not lake bed: no shore band there)
		At->AboveLake = LakeS > -1e8 ? float(FMath::Clamp(bLakeHere ? H - LakeS : FMath::Max(H - LakeS, 4.0), -1000.0, 1.0e4)) : 1.0e4f;
	}
	return H;
}

// ================================================================================================ rivers

void FDmPlanetTerrain::RiverQuery(const FVector3d& Dir, double& OutD, double& OutHW, double* OutSigned) const
{
	const FIntVector K(FMath::FloorToInt(Dir.X / RiverGridCell), FMath::FloorToInt(Dir.Y / RiverGridCell), FMath::FloorToInt(Dir.Z / RiverGridCell));
	double Best = 1e30; double BestW = 0.0; int32 BestSeg = -1; double BestT = 0.0;
	for (int32 Dz = -1; Dz <= 1; ++Dz)
		for (int32 Dy = -1; Dy <= 1; ++Dy)
			for (int32 Dx = -1; Dx <= 1; ++Dx)
			{
				const TArray<int32>* Cell = RGrid.Find(K + FIntVector(Dx, Dy, Dz));
				if (!Cell) continue;
				for (int32 Si : *Cell)
				{
					const FDmRiverPt& A = RPts[Si]; const FDmRiverPt& B = RPts[Si + 1];
					const FVector3d AB = B.P - A.P;
					const double T = FMath::Clamp(((Dir - A.P) | AB) / FMath::Max(AB | AB, 1e-30), 0.0, 1.0);
					const FVector3d Q = A.P + AB * T - Dir;
					const double D2 = Q | Q;
					// the nearest reach by distance to its bank, so a small creek next to a big river does not win
					const double Hw = A.HalfW + (B.HalfW - A.HalfW) * T;
					const double Db = FMath::Sqrt(D2) * DatumM - Hw;
					if (Db < Best) { Best = Db; BestW = Hw; BestSeg = Si; BestT = T; }
				}
			}
	if (BestSeg < 0) { OutD = 1e9; OutHW = 0.0; if (OutSigned) *OutSigned = 1e9; return; }
	OutD = FMath::Max(Best + BestW, 0.0);
	OutHW = BestW;
	if (OutSigned)
	{
		// Signed distance across the river (left negative): linear across a straight reach, so the mesh
		// can interpolate it exactly and the material draws a crisp channel at any vertex spacing.
		double Sd = OutD;
		const FVector3d AB = RPts[BestSeg + 1].P - RPts[BestSeg].P;
		const FVector3d Cp = RPts[BestSeg].P + AB * BestT;
		if ((((AB ^ (Dir - Cp)) | Dir)) < 0.0) Sd = -Sd;
		*OutSigned = Sd;
	}
}

bool FDmPlanetTerrain::RoadsNear(const FVector3d& Dir, double Ang) const
{
	if (RoadGrid.Num() == 0) return false;
	if (Ang > 0.03) return true;
	const double R = Ang + RiverGridCell;
	const FIntVector Lo(FMath::FloorToInt((Dir.X - R) / RiverGridCell), FMath::FloorToInt((Dir.Y - R) / RiverGridCell), FMath::FloorToInt((Dir.Z - R) / RiverGridCell));
	const FIntVector Hi(FMath::FloorToInt((Dir.X + R) / RiverGridCell), FMath::FloorToInt((Dir.Y + R) / RiverGridCell), FMath::FloorToInt((Dir.Z + R) / RiverGridCell));
	for (int32 Z = Lo.Z; Z <= Hi.Z; ++Z)
		for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
			for (int32 X = Lo.X; X <= Hi.X; ++X)
				if (RoadGrid.Contains(FIntVector(X, Y, Z))) return true;
	return false;
}

void FDmPlanetTerrain::RoadQuery(const FVector3d& Dir, double& OutSigned, double& OutLevel) const
{
	OutSigned = 1e9; OutLevel = 0.0;
	const FIntVector K(FMath::FloorToInt(Dir.X / RiverGridCell), FMath::FloorToInt(Dir.Y / RiverGridCell), FMath::FloorToInt(Dir.Z / RiverGridCell));
	double Best = 1e30; int32 BestSeg = -1; double BestT = 0.0;
	for (int32 Dz = -1; Dz <= 1; ++Dz)
		for (int32 Dy = -1; Dy <= 1; ++Dy)
			for (int32 Dx = -1; Dx <= 1; ++Dx)
			{
				const TArray<int32>* Cell = RoadGrid.Find(K + FIntVector(Dx, Dy, Dz));
				if (!Cell) continue;
				for (int32 Si : *Cell)
				{
					const FRoadSeg& S = RoadSegs[Si];
					const FVector3d AB = S.B - S.A;
					const double T = FMath::Clamp(((Dir - S.A) | AB) / FMath::Max(AB | AB, 1e-30), 0.0, 1.0);
					const double D2 = (S.A + AB * T - Dir).SizeSquared();
					if (D2 < Best) { Best = D2; BestSeg = Si; BestT = T; }
				}
			}
	if (BestSeg < 0) return;
	const FRoadSeg& S = RoadSegs[BestSeg];
	const FVector3d AB = S.B - S.A;
	const FVector3d Cp = S.A + AB * BestT;
	double Sd = FMath::Sqrt(Best) * DatumM;
	// signed across the segment (linear across a straight reach, so the mesh interpolates it exactly);
	// past a segment's end (a junction or a bend) the unsigned distance is the honest answer
	if (BestT > 0.0 && BestT < 1.0 && (((AB ^ (Dir - Cp)) | Dir) < 0.0)) Sd = -Sd;
	OutSigned = Sd; OutLevel = S.Level;
}

bool FDmPlanetTerrain::RiversNear(const FVector3d& Dir, double Ang) const
{
	if (RGrid.Num() == 0) return false;
	if (Ang > 0.03) return true;
	const double R = Ang + RiverGridCell;
	const FIntVector Lo(FMath::FloorToInt((Dir.X - R) / RiverGridCell), FMath::FloorToInt((Dir.Y - R) / RiverGridCell), FMath::FloorToInt((Dir.Z - R) / RiverGridCell));
	const FIntVector Hi(FMath::FloorToInt((Dir.X + R) / RiverGridCell), FMath::FloorToInt((Dir.Y + R) / RiverGridCell), FMath::FloorToInt((Dir.Z + R) / RiverGridCell));
	for (int32 Z = Lo.Z; Z <= Hi.Z; ++Z)
		for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
			for (int32 X = Lo.X; X <= Hi.X; ++X)
				if (RGrid.Contains(FIntVector(X, Y, Z))) return true;
	return false;
}

void FDmPlanetTerrain::AddChain(TArray<FVector3d>& Ctrl, TArray<double>& CFlow, int32 Chain, double WidthScale, double MeanderScale, TArray<int32>* CellPt, const TArray<int32>* Cells)
{
	auto HalfW = [this](double F) { return 5.0 + 4.5 * FMath::Sqrt(FMath::Max(F, 0.0) / RiverT); };
	double Total = 0; for (int32 I = 0; I + 1 < Ctrl.Num(); ++I) Total += FVector3d::Distance(Ctrl[I], Ctrl[I + 1]) * DatumM;
	const int32 First = RPts.Num();
	double Arc = 0;
	for (int32 I = 0; I + 1 < Ctrl.Num(); ++I)
	{
		const FVector3d P0 = I > 0 ? Ctrl[I - 1] : Ctrl[I] * 2.0 - Ctrl[I + 1];
		const FVector3d P1 = Ctrl[I], P2 = Ctrl[I + 1];
		const FVector3d P3 = I + 2 < Ctrl.Num() ? Ctrl[I + 2] : Ctrl[I + 1] * 2.0 - Ctrl[I];
		const double SegLen = FVector3d::Distance(P1, P2) * DatumM;
		const int32 NSub = FMath::Clamp(FMath::CeilToInt(SegLen / 25.0), 2, 400);
		if (CellPt && Cells && I < Cells->Num()) (*CellPt)[(*Cells)[I]] = RPts.Num();
		for (int32 K = 0; K < NSub; ++K)
		{
			const double T = double(K) / NSub, T2 = T * T, T3 = T2 * T;
			FVector3d Q = 0.5 * ((2.0 * P1) + (P2 - P0) * T + (2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3) * T2 + (3.0 * P1 - P0 - 3.0 * P2 + P3) * T3);
			const FVector3d Tan = 0.5 * ((P2 - P0) + (2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3) * (2.0 * T) + (3.0 * P1 - P0 - 3.0 * P2 + P3) * (3.0 * T2));
			Q = Nrm(Q);
			const double F = FMath::Lerp(CFlow[I], CFlow[I + 1], T);
			// a river is born narrow: the first ~700 m of a chain widen from a third of its width (the sim's
			// river starts at its threshold discharge, and the channel used to begin as a blunt, full-width stub)
			const double Hw = HalfW(F) * WidthScale * (0.3 + 0.7 * Smooth(0.0, 700.0, Arc));
			// Meanders: a sideways offset from 1D noise along the arc, tapered to zero at both ends.
			const double Lam = FMath::Clamp(14.0 * Hw, 220.0, 1400.0);
			const double Amp = MeanderScale * FMath::Min(6.0 * Hw + 60.0, 0.30 * Lam) * Smooth(0.0, 900.0, Arc) * Smooth(0.0, 900.0, Total - Arc);
			const double Nz = 1.3 * Noised(FVector3d(Arc / Lam, Chain * 7.31, 0.37)).X + 0.2 * Noised(FVector3d(Arc / Lam * 2.3, Chain * 7.31 + 3.0, 1.1)).X;
			// plus a slow swing (2.5-4 km) that breaks the cell-to-cell straight reaches
			const double Lam2 = 2600.0 + 1400.0 * Sat(Hw / 40.0);
			const double Amp2 = MeanderScale * 260.0 * Smooth(0.0, 1500.0, Arc) * Smooth(0.0, 1500.0, Total - Arc);
			const double Nz2 = 1.4 * Noised(FVector3d(Arc / Lam2, Chain * 5.13, 3.3)).X;
			const FVector3d Side = Nrm(Q ^ Tan);
			Q = Nrm(Q + Side * ((Amp * Nz + Amp2 * Nz2) / DatumM));
			// width breathes along the course (pools, narrows)
			RPts.Add({ Q, float(Hw * (1.0 + 0.25 * Noised(FVector3d(Arc / 180.0, Chain * 3.17, 7.7)).X)) });
			Arc += SegLen / NSub;
		}
	}
	RPts.Add({ Nrm(Ctrl.Last()), float(HalfW(CFlow.Last()) * WidthScale) });
	if (CellPt && Cells && Cells->Num() == Ctrl.Num()) (*CellPt)[Cells->Last()] = RPts.Num() - 1;
	for (int32 I = First; I + 1 < RPts.Num(); ++I) RSeg.Add(I);
}

void FDmPlanetTerrain::BuildRivers()
{
	RPts.Reset(); RSeg.Reset(); RGrid.Reset(); NumChains = 0; NumDeltaBranches = 0;
	if (!Loc.IsValid() || RiverCells.Num() == 0) return;
	const FDmGeoLocator& G = *Loc;
	const int32 N = G.N;
	auto IsSea = [this](int32 C) { return Elev[C] < SeaLevel; };

	// Parents first: order by the river's own largest discharge (its last cell is a confluence with,
	// or the mouth into, something bigger), so a tributary can end exactly on its parent's curve.
	TArray<int32> Order;
	TArray<double> OwnMax; OwnMax.SetNumZeroed(RiverCells.Num());
	for (int32 R = 0; R < RiverCells.Num(); ++R)
	{
		Order.Add(R);
		const TArray<float>& Fs = RiverFlow[R];
		double Mx = 0; for (int32 I = 0; I + 1 < Fs.Num(); ++I) Mx = FMath::Max(Mx, double(Fs[I]));
		if (Fs.Num() == 1) Mx = Fs[0];
		OwnMax[R] = Mx;
	}
	Order.Sort([&OwnMax](int32 A, int32 B) { return OwnMax[A] != OwnMax[B] ? OwnMax[A] > OwnMax[B] : A < B; });

	TArray<int32> CellPt; CellPt.Init(-1, N);
	for (int32 R : Order)
	{
		const TArray<int32>& Cells = RiverCells[R];
		TArray<FVector3d> Ctrl; TArray<double> CFlow;
		for (int32 I = 0; I < Cells.Num(); ++I) { Ctrl.Add(G.Pos[Cells[I]]); CFlow.Add(RiverFlow[R][I]); }
		const int32 EndCell = Cells.Last();
		const bool bJoin = CellPt[EndCell] >= 0 && !IsSea(EndCell);
		if (bJoin) Ctrl.Last() = RPts[CellPt[EndCell]].P;
		if (Ctrl.Num() < 2) continue;
		// round off the 60-degree turns of the cell-to-cell path (two Laplacian passes, ends fixed)
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			TArray<FVector3d> Sm = Ctrl;
			for (int32 I = 1; I + 1 < Ctrl.Num(); ++I) Sm[I] = Nrm(Ctrl[I] * 0.5 + (Ctrl[I - 1] + Ctrl[I + 1]) * 0.25);
			Ctrl = MoveTemp(Sm);
		}
		// do not overwrite the parent's points for the joined end cell
		TArray<int32> OwnCells = Cells;
		const int32 Chain = NumChains++;
		const int32 FirstPt = RPts.Num();
		if (bJoin)
		{
			const int32 Keep = CellPt[EndCell];
			AddChain(Ctrl, CFlow, Chain, 1.0, 1.0, &CellPt, &OwnCells);
			CellPt[EndCell] = Keep;
		}
		else AddChain(Ctrl, CFlow, Chain, 1.0, 1.0, &CellPt, &OwnCells);

		// Deltas: a big river reaching the sea fans out into distributaries across its last kilometres.
		// the mouth cell is a sea cell, whose own flow may be zero: the discharge is the last land cell's
		const double MouthFlow = CFlow.Num() >= 2 ? FMath::Max(CFlow.Last(), CFlow[CFlow.Num() - 2]) : CFlow.Last();
		if (IsSea(EndCell) && MouthFlow > 4.0 * RiverT && RPts.Num() - FirstPt > 40)
		{
			const int32 Last = RPts.Num() - 1;
			const FVector3d Mouth = RPts[Last].P;
			// coast-ward tangent from the last ~2 km of the main channel
			const int32 Back = FMath::Max(FirstPt, Last - 80);
			const FVector3d Tan = Nrm(Mouth - RPts[Back].P);
			const FVector3d Side = Nrm(Tan ^ Mouth);
			const double Scale = FMath::Clamp(FMath::Sqrt(MouthFlow / (6.0 * RiverT)), 0.8, 2.2);
			const int32 NBranch = MouthFlow > 14.0 * RiverT ? 4 : (MouthFlow > 7.0 * RiverT ? 3 : 2);
			for (int32 B = 0; B < NBranch; ++B)
			{
				// branch off 3..7 km upstream, end 1.5..5 km to either side of the main mouth
				const double Up = (3000.0 + 4000.0 * FMath::Frac(0.37 * (B + 1) + Chain * 0.11)) * Scale;
				const int32 StartPt = FMath::Max(FirstPt, Last - FMath::RoundToInt(Up / 25.0));
				const double SideSign = (B % 2 == 0) ? 1.0 : -1.0;
				const double Off = (1500.0 + 1700.0 * (B / 2) + 900.0 * FMath::Frac(0.61 * (B + 3) + Chain * 0.07)) * Scale;
				const FVector3d A0 = RPts[StartPt].P;
				FVector3d End = Nrm(Mouth + Side * (SideSign * Off / DatumM) + Tan * (600.0 * Scale / DatumM));
				// a distributary must reach the sea: walk the end point seaward (along the main channel's
				// direction) until it is over water, then 300 m further; drop the branch if the coast is too far
				{
					bool bWet = false;
					for (int32 Step = 0; Step < 6 && !bWet; ++Step)   // at most 1.5 km: a longer walk would cross the other branches
					{
						const int32 Cn = G.NearestCell(End);
						if (Elev[Cn] < SeaLevel) bWet = true;
						else End = Nrm(End + Tan * (250.0 / DatumM));
					}
					if (!bWet) continue;
					End = Nrm(End + Tan * (300.0 / DatumM));
				}
				const FVector3d Mid = Nrm((A0 + End) * 0.5 + Side * (SideSign * 0.25 * Off / DatumM));
				TArray<FVector3d> Bc = { A0, Mid, End };
				const double Fb = MouthFlow * (NBranch == 4 ? 0.22 : 0.35);
				TArray<double> Bf = { Fb, Fb, Fb };
				AddChain(Bc, Bf, Chain * 13 + B + 7, 0.95, 0.65, nullptr, nullptr);
				++NumDeltaBranches;
			}
		}
	}
	for (int32 Si = 0; Si < RSeg.Num(); ++Si)
	{
		const FVector3d Mid = (RPts[RSeg[Si]].P + RPts[RSeg[Si] + 1].P) * 0.5;
		RGrid.FindOrAdd(FIntVector(FMath::FloorToInt(Mid.X / RiverGridCell), FMath::FloorToInt(Mid.Y / RiverGridCell), FMath::FloorToInt(Mid.Z / RiverGridCell))).Add(RSeg[Si]);
	}
}
