#include "GroundLand.h"
#include "SurfaceTerrain.h"
#include "SurfaceNoise.h"
#include "Async/ParallelFor.h"
#include "HAL/PlatformTime.h"
#include "Algo/Reverse.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

using namespace SurfNoise;

namespace
{
	constexpr double R = 100000.0; // planet datum radius, m
	inline double Sat(double X) { return FMath::Clamp(X, 0.0, 1.0); }
	inline double Smooth(double A, double B, double X) { const double T = Sat((X - A) / (B - A)); return T * T * (3.0 - 2.0 * T); }
	inline FVector3d Nrm(const FVector3d& V) { return V * (1.0 / FMath::Sqrt(V | V)); }
	inline double Rnd(uint32 H) { return double(H & 0xFFFFFFu) / double(0xFFFFFF); }
	enum : uint8 { SEA, SHALLOWS, BEACH, DESERT, SAVANNA, GRASSLAND, FOREST, RAINFOREST, TAIGA, TUNDRA, ICE, ROCK, SNOWPEAK, WETLAND, BARREN, SEAICE };
	FIntVector GridKey(const FVector3d& D, double Cell)
	{
		return FIntVector(FMath::FloorToInt(D.X / Cell), FMath::FloorToInt(D.Y / Cell), FMath::FloorToInt(D.Z / Cell));
	}
}

// ---------------------------------------------------------------------------------------------- settlements

void FGroundLand::PlaceSettlements(const FSurfaceTerrain& T)
{
	const double T0 = FPlatformTime::Seconds();
	struct FCand { double Score; int32 Cell; };
	TArray<FCand> Cands;
	for (int32 Ci = 0; Ci < T.N; ++Ci)
	{
		const double El = T.Elev[Ci] - T.SeaLevel;
		if (El < 0.03) continue;
		const uint8 B = T.Biome[Ci];
		if (B == ROCK || B == SNOWPEAK || B == ICE || B == SEAICE) continue;
		const double Tc = T.Temp[Ci];
		if (Tc < -3.0) continue;
		bool bRiver = T.Flow[Ci] > T.RiverT, bCoast = false;
		for (int32 Nb : T.Nbr[Ci])
		{
			if (T.Elev[Nb] < T.SeaLevel) bCoast = true;
			else if (T.Flow[Nb] > T.RiverT) bRiver = true;
		}
		double Suit = Sat((T.Hab[Ci] - 0.2) / 0.5) * Sat(1.25 - T.Slope[Ci] / 1.0) * Sat((Tc + 3.0) / 9.0) * (1.0 - 0.7 * Sat(T.Marsh[Ci]));
		if (T.Moist[Ci] < 0.25 && !bRiver) Suit *= 0.25; // dry land: only oases and river valleys
		const uint32 H = Hash(Ci, 17, 3);
		const double Score = Suit * (1.0 + 0.8 * bRiver + 0.35 * bCoast) * (0.7 + 0.6 * Rnd(H));
		if (Score > 0.32) Cands.Add({ Score, Ci });
	}
	Cands.Sort([](const FCand& A, const FCand& B) { return A.Score > B.Score; });
	const double MinSep = 0.058; // 5.8 km between settlements
	TMap<FIntVector, TArray<int32>> Grid;
	for (const FCand& Cd : Cands)
	{
		const FVector3d P0 = Nrm(FVector3d(T.Pos[Cd.Cell]));
		const FIntVector K = GridKey(P0, MinSep);
		bool bOk = true;
		for (int32 Z = -1; Z <= 1 && bOk; ++Z) for (int32 Y = -1; Y <= 1 && bOk; ++Y) for (int32 X = -1; X <= 1 && bOk; ++X)
			if (const TArray<int32>* L = Grid.Find(K + FIntVector(X, Y, Z)))
				for (int32 S : *L) if (FVector3d::Distance(Sets[S].Dir, P0) < MinSep) { bOk = false; break; }
		if (!bOk) continue;
		// exact site: a few jittered tries inside the cell; dry ground above the river bank, flat, near water
		const FVector3d Ax = FMath::Abs(P0.Y) < 0.9 ? FVector3d(0, 1, 0) : FVector3d(1, 0, 0);
		const FVector3d T1 = Nrm(Ax ^ P0), T2 = P0 ^ T1;
		FVector3d Best = P0; double BestS = -1e9;
		for (int32 Tr = 0; Tr < 8; ++Tr)
		{
			const uint32 H = Hash(Cd.Cell, Tr, 91);
			const double A = Rnd(H) * 2.0 * PI, Rr = 0.0075 * FMath::Sqrt(Rnd(H >> 3 ^ 0x9E3779B9u));
			const FVector3d D = Nrm(P0 + (T1 * FMath::Cos(A) + T2 * FMath::Sin(A)) * Rr);
			FSurfAttr At;
			const double H0 = T.Height(D, 50.0, &At);
			const double Hx = T.Height(Nrm(D + T1 * 0.0006), 50.0), Hy = T.Height(Nrm(D + T2 * 0.0006), 50.0);
			const double Sl = FMath::Sqrt(FMath::Square(Hx - H0) + FMath::Square(Hy - H0)) / 60.0;
			const double Rd = FMath::Abs(At.RiverD) - At.RiverW;
			double S = -Sl * 6.0;
			if (H0 < 3.0 || Rd < 40.0) S -= 100.0;
			if (At.RiverW > 0.5 && Rd < 1500.0) S += 1.0 - Smooth(150.0, 900.0, Rd);
			if (S > BestS) { BestS = S; Best = D; }
		}
		if (BestS < -50.0) continue;
		FSettlement St;
		St.Dir = Best;
		St.Size = Sat(Cd.Score / 1.7) * 0.8 + 0.2 * Rnd(Hash(Cd.Cell, 5, 77));
		St.FarmR = 1700.0 + 2500.0 * St.Size;
		St.CoreR = 90.0 + 230.0 * St.Size;
		const int32 Id = Sets.Add(St);
		Grid.FindOrAdd(GridKey(Best, MinSep)).Add(Id);
	}
	for (int32 I = 0; I < Sets.Num(); ++I) SetGrid.FindOrAdd(GridKey(Sets[I].Dir, SetCell)).Add(I);
	Stats[0] = Sets.Num();
	UE_LOG(LogTemp, Display, TEXT("Ground: %d settlements from %d candidates in %.0f ms"), Sets.Num(), Cands.Num(), (FPlatformTime::Seconds() - T0) * 1000.0);
}

void FGroundLand::FarmAt(const FSurfaceTerrain& T, const FVector3d& Dir, double& OutFarm, double& OutCore, double& OutTempC) const
{
	int32 Cc[3]; double W[3];
	T.Locate(Dir, Cc, W);
	auto L = [&](const TArray<float>& F) { return W[0] * F[Cc[0]] + W[1] * F[Cc[1]] + W[2] * F[Cc[2]]; };
	OutTempC = L(T.Temp);
	OutFarm = 0.0; OutCore = 0.0;
	const double El = L(T.Elev) - T.SeaLevel;
	if (El < 0.0) return;
	// the cultivated area is not a disc: warp the direction by ~1.2 km of low-frequency noise
	const FVector3d Wp = Dir * 60.0;
	const FVector3d Warp(Fbm(Wp + FVector3d(3.1, 0.7, 5.3), 3), Fbm(Wp + FVector3d(9.2, 4.4, 1.6), 3), Fbm(Wp + FVector3d(0.4, 7.7, 2.8), 3));
	const FVector3d Dw = Nrm(Dir + Warp * 0.016);
	const FIntVector K = GridKey(Dir, SetCell);
	double Infl = 0.0, Core = 0.0;
	for (int32 Z = -1; Z <= 1; ++Z) for (int32 Y = -1; Y <= 1; ++Y) for (int32 X = -1; X <= 1; ++X)
		if (const TArray<int32>* Ls = SetGrid.Find(K + FIntVector(X, Y, Z)))
			for (int32 Si : *Ls)
			{
				const FSettlement& S = Sets[Si];
				const double D = FVector3d::Distance(Dw, S.Dir) * R;
				Infl = FMath::Max(Infl, (1.0 - Smooth(0.30 * S.FarmR, S.FarmR, D)) * (0.6 + 0.4 * S.Size));
				const double Dc = FVector3d::Distance(Nrm(Dir + Warp * 0.002), S.Dir) * R;
				Core = FMath::Max(Core, 1.0 - Smooth(0.35 * S.CoreR, 1.1 * S.CoreR, Dc));
			}
	if (Infl <= 0.0 && Core <= 0.0) return;
	const double Tc = OutTempC, Mo = L(T.Moist), Sl = L(T.Slope), Ma = L(T.Marsh);
	double Suit = Sat((Tc + 2.0) / 7.0) * Sat(1.35 - Sl / 0.9) * (1.0 - 0.75 * Sat(Ma)) * Smooth(0.0, 0.04, El);
	// dry climates: only irrigated land along rivers
	double Rd = 1e9, Hw = 0.0; T.RiverQuery(Dir, Rd, Hw);
	const double Irr = Hw > 0.5 ? 1.0 - Smooth(400.0, 1800.0, Rd) : 0.0;
	Suit *= FMath::Max(Sat(Mo * 3.0 - 0.15), Irr);
	OutFarm = Sat(Infl * Suit);
	OutCore = Sat(Core) * Smooth(0.0, 0.03, El);
}

// ---------------------------------------------------------------------------------------------- tile

void FGroundLand::BuildTile(const FSurfaceTerrain& T, const FVector3d& CenterDir, double HalfM, int32 CoarseN, int32 FineN)
{
	const double T0 = FPlatformTime::Seconds();
	C = Nrm(CenterDir);
	E = Nrm(FVector3d(0, 1, 0) ^ C);
	N = C ^ E;
	Half = HalfM; CN = CoarseN; FN = FineN;
	const double Cs = 2.0 * Half / CN;
	HC.SetNumUninitialized(CN * CN);
	TArray<float> RivD, RivW; RivD.SetNumUninitialized(CN * CN); RivW.SetNumUninitialized(CN * CN);
	ParallelFor(CN, [&](int32 J)
	{
		for (int32 I = 0; I < CN; ++I)
		{
			const FVector3d D = TileDir((I + 0.5) * Cs - Half, (J + 0.5) * Cs - Half);
			HC[J * CN + I] = float(T.Height(D, Cs));
			double Rd = 1e9, Hw = 0.0;
			T.RiverQuery(D, Rd, Hw);
			RivD[J * CN + I] = float(FMath::Min(Rd, 5000.0)); RivW[J * CN + I] = float(Hw);
		}
	});
	Stats[1] = (FPlatformTime::Seconds() - T0) * 1000.0;
	UE_LOG(LogTemp, Display, TEXT("Ground: tile %.0f km, coarse %d^2 (%.1f m) heights in %.0f ms"), 2.0 * Half / 1000.0, CN, Cs, Stats[1]);
	RouteRoads(T, RivD, RivW);
	RasterFine(T);
}

// A* over the coarse height grid with a 16-neighbour stencil. The cost grows with the square of the
// grade, so roads take valleys and wind along contours instead of climbing; rivers cost a bridge to cross,
// and moving along an existing road is cheap, so later links join the network instead of duplicating it.
void FGroundLand::RouteRoads(const FSurfaceTerrain& T, const TArray<float>& RivD, const TArray<float>& RivW)
{
	const double T0 = FPlatformTime::Seconds();
	const double Cs = 2.0 * Half / CN;
	struct FNodeSet { int32 Id; int32 I, J; };
	TArray<FNodeSet> Ls;
	for (int32 S = 0; S < Sets.Num(); ++S)
	{
		double X, Y;
		if (!TileXY(Sets[S].Dir, X, Y) || FMath::Abs(X) > Half - 800.0 || FMath::Abs(Y) > Half - 800.0) continue;
		Ls.Add({ S, FMath::Clamp(int32((X + Half) / Cs), 0, CN - 1), FMath::Clamp(int32((Y + Half) / Cs), 0, CN - 1) });
	}
	TSet<uint64> LinkSet; TArray<TPair<int32, int32>> Links;
	for (int32 A = 0; A < Ls.Num(); ++A)
	{
		TArray<TPair<double, int32>> Nb;
		for (int32 B = 0; B < Ls.Num(); ++B)
			if (B != A) Nb.Add({ FVector3d::Distance(Sets[Ls[A].Id].Dir, Sets[Ls[B].Id].Dir) * R, B });
		Nb.Sort([](const TPair<double, int32>& X, const TPair<double, int32>& Y) { return X.Key < Y.Key; });
		for (int32 K = 0; K < Nb.Num() && K < 3; ++K)
		{
			if (Nb[K].Key > 15000.0) break;
			const int32 Lo = FMath::Min(A, Nb[K].Value), Hi = FMath::Max(A, Nb[K].Value);
			const uint64 Key = (uint64(Lo) << 32) | uint64(Hi);
			if (!LinkSet.Contains(Key)) { LinkSet.Add(Key); Links.Add({ Lo, Hi }); }
		}
	}
	Links.Sort([&](const TPair<int32, int32>& X, const TPair<int32, int32>& Y)
	{
		return FVector3d::Distance(Sets[Ls[X.Key].Id].Dir, Sets[Ls[X.Value].Id].Dir) < FVector3d::Distance(Sets[Ls[Y.Key].Id].Dir, Sets[Ls[Y.Value].Id].Dir);
	});
	NumLinks = Links.Num();
	if (FParse::Param(FCommandLine::Get(), TEXT("groundlist")))
		for (const FNodeSet& L : Ls)
		{
			const FSettlement& St = Sets[L.Id];
			const FVector3d U(St.Dir.X, St.Dir.Z, St.Dir.Y); // UE frame
			double Rd = 1e9, Hw = 0; T.RiverQuery(St.Dir, Rd, Hw);
			double Sea = 1e9;
			for (int32 Dj = -48; Dj <= 48; Dj += 2) for (int32 Di = -48; Di <= 48; Di += 2)
			{
				const int32 I = L.I + Di, J = L.J + Dj;
				if (I < 0 || J < 0 || I >= CN || J >= CN) continue;
				if (HC[J * CN + I] < 0.f) Sea = FMath::Min(Sea, Cs * FMath::Sqrt(double(Di * Di + Dj * Dj)));
			}
			UE_LOG(LogTemp, Display, TEXT("Ground: site lat %.3f lon %.3f size %.2f river %.0f m (hw %.0f) sea %.0f m h %.0f"),
				FMath::RadiansToDegrees(FMath::Asin(U.Z)), FMath::RadiansToDegrees(FMath::Atan2(U.Y, U.X)), St.Size, Rd, Hw, Sea, HC[L.J * CN + L.I]);
		}

	const int32 NN = CN * CN;
	// terrain "roughness" the height field does not carry (woods, boulders, bogs, property): a slowly varying
	// cost multiplier, so roads on flat ground wander instead of running dead straight along the stencil
	TArray<float> Rough; Rough.SetNumUninitialized(NN);
	ParallelFor(CN, [&](int32 J)
	{
		for (int32 I = 0; I < CN; ++I)
		{
			const FVector3d D = TileDir((I + 0.5) * Cs - Half, (J + 0.5) * Cs - Half);
			const double Nz = 0.6 * Noised(D * (R / 900.0) + FVector3d(3.7, 1.1, 8.2)).X + 0.4 * Noised(D * (R / 2600.0) + FVector3d(0.3, 5.9, 2.4)).X;
			Rough[J * CN + I] = float(1.0 + 1.6 * Sat(Nz + 0.35));
		}
	});
	TArray<float> G; G.Init(0.f, NN);
	TArray<int32> Par; Par.Init(-1, NN);
	TArray<int32> Stamp; Stamp.Init(-1, NN);
	TArray<uint8> Closed; Closed.Init(0, NN);
	TArray<uint8> Use; Use.Init(0, NN);
	TArray<TArray<int32>> Paths;
	static const int32 Mv[16][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1},{2,1},{2,-1},{-2,1},{-2,-1},{1,2},{1,-2},{-1,2},{-1,-2} };
	auto InRiver = [&](int32 V) { return RivW[V] > 0.5f && RivD[V] - RivW[V] < 14.f; };
	int64 Expanded = 0;
	for (int32 Li = 0; Li < Links.Num(); ++Li)
	{
		const FNodeSet& A = Ls[Links[Li].Key]; const FNodeSet& B = Ls[Links[Li].Value];
		const int32 Src = A.J * CN + A.I, Dst = B.J * CN + B.I;
		const int32 M = int32(4000.0 / Cs);
		const int32 I0 = FMath::Max(FMath::Min(A.I, B.I) - M, 0), I1 = FMath::Min(FMath::Max(A.I, B.I) + M, CN - 1);
		const int32 J0 = FMath::Max(FMath::Min(A.J, B.J) - M, 0), J1 = FMath::Min(FMath::Max(A.J, B.J) + M, CN - 1);
		struct FItem { float F; int32 V; };
		TArray<FItem> Heap;
		auto Pred = [](const FItem& X, const FItem& Y) { return X.F < Y.F; };
		auto Hh = [&](int32 V) { const int32 I = V % CN, J = V / CN; return float(0.42 * Cs * FMath::Sqrt(double(FMath::Square(I - B.I) + FMath::Square(J - B.J)))); };
		Stamp[Src] = Li; G[Src] = 0.f; Par[Src] = -1; Closed[Src] = 0;
		Heap.HeapPush({ Hh(Src), Src }, Pred);
		bool bFound = false;
		while (Heap.Num())
		{
			FItem It; Heap.HeapPop(It, Pred);
			const int32 U = It.V;
			if (Closed[U] && Stamp[U] == Li) continue;
			Closed[U] = 1;
			++Expanded;
			if (U == Dst) { bFound = true; break; }
			const int32 Ui = U % CN, Uj = U / CN;
			for (const auto& D : Mv)
			{
				const int32 Vi = Ui + D[0], Vj = Uj + D[1];
				if (Vi < I0 || Vi > I1 || Vj < J0 || Vj > J1) continue;
				const int32 V = Vj * CN + Vi;
				if (HC[V] < 1.5f && V != Dst) continue; // sea
				const double Len = Cs * FMath::Sqrt(double(D[0] * D[0] + D[1] * D[1]));
				const double Gr = FMath::Abs(double(HC[V]) - double(HC[U])) / Len;
				double Cst = Len * (0.5 * (Rough[U] + Rough[V]) + FMath::Square(Gr / 0.05) + (Gr > 0.16 ? 25.0 * (Gr - 0.16) / 0.04 : 0.0));
				// knight moves pass between two cells: check the one they cross for water and grade too
				const int32 Mi = Ui + (D[0] / 2 != 0 ? D[0] / 2 : (FMath::Abs(D[1]) == 2 ? D[0] : 0)), Mj = Uj + (D[1] / 2 != 0 ? D[1] / 2 : (FMath::Abs(D[0]) == 2 ? D[1] : 0));
				const int32 Mid = Mj * CN + Mi;
				if (HC[Mid] < 1.5f) continue;
				const bool bRivV = InRiver(V) || InRiver(Mid), bRivU = InRiver(U);
				if (bRivV && !bRivU) Cst += 900.0;          // a bridge or a ford
				if (bRivV && bRivU) Cst += 3.0 * Len;      // never run along the river bed
				if (Use[V]) Cst *= 0.42;                   // follow the existing network
				const float Gv = G[U] + float(Cst);
				if (Stamp[V] != Li) { Stamp[V] = Li; Closed[V] = 0; G[V] = Gv; Par[V] = U; Heap.HeapPush({ Gv + Hh(V), V }, Pred); }
				else if (!Closed[V] && Gv < G[V]) { G[V] = Gv; Par[V] = U; Heap.HeapPush({ Gv + Hh(V), V }, Pred); }
			}
		}
		if (!bFound) continue;
		TArray<int32> P;
		for (int32 V = Dst; V >= 0; V = Par[V]) P.Add(V);
		Algo::Reverse(P);
		for (int32 V : P) Use[V] = uint8(FMath::Min(Use[V] + 1, 250));
		Paths.Add(MoveTemp(P));
	}
	Stats[2] = double(Expanded);

	// Graph of grid nodes; chains between junctions (degree != 2) are smoothed separately with fixed ends,
	// so shared stretches are drawn once and roads meet exactly at junctions.
	TMap<int32, TSet<int32>> Adj;
	for (const TArray<int32>& P : Paths)
		for (int32 K = 0; K + 1 < P.Num(); ++K) { Adj.FindOrAdd(P[K]).Add(P[K + 1]); Adj.FindOrAdd(P[K + 1]).Add(P[K]); }
	TSet<int32> Ends;
	for (const FNodeSet& S : Ls) Ends.Add(S.J * CN + S.I);
	TSet<uint64> DoneEdge;
	auto EKey = [](int32 A, int32 B) { return (uint64(uint32(FMath::Min(A, B))) << 32) | uint64(uint32(FMath::Max(A, B))); };
	double TotalKm = 0.0;
	for (const auto& Kv : Adj)
	{
		const int32 Start = Kv.Key;
		if (Kv.Value.Num() == 2 && !Ends.Contains(Start)) continue;
		for (int32 Nx : Kv.Value)
		{
			if (DoneEdge.Contains(EKey(Start, Nx))) continue;
			TArray<int32> Ch = { Start };
			int32 Prev = Start, Cur = Nx;
			DoneEdge.Add(EKey(Start, Nx));
			while (true)
			{
				Ch.Add(Cur);
				const TSet<int32>& Nb = Adj[Cur];
				if (Nb.Num() != 2 || Ends.Contains(Cur)) break;
				int32 Next = -1; for (int32 Q : Nb) if (Q != Prev) Next = Q;
				if (Next < 0 || DoneEdge.Contains(EKey(Cur, Next))) break;
				DoneEdge.Add(EKey(Cur, Next));
				Prev = Cur; Cur = Next;
			}
			if (Ch.Num() < 2) continue;
			// smooth (ends fixed): removes the 22.5-degree stencil zigzag but keeps the routed line
			TArray<FVector2D> Q; int32 MaxUse = 0;
			for (int32 V : Ch) { Q.Add(FVector2D(((V % CN) + 0.5) * Cs - Half, ((V / CN) + 0.5) * Cs - Half)); MaxUse = FMath::Max<int32>(MaxUse, Use[V]); }
			for (int32 Pass = 0; Pass < 4; ++Pass)
			{
				TArray<FVector2D> Sm = Q;
				for (int32 K = 1; K + 1 < Q.Num(); ++K) Sm[K] = Q[K] * 0.5 + (Q[K - 1] + Q[K + 1]) * 0.25;
				Q = MoveTemp(Sm);
			}
			const float Hw = MaxUse >= 3 ? 3.6f : (MaxUse == 2 ? 2.9f : 2.3f);
			FRoad Rd;
			for (int32 K = 0; K + 1 < Q.Num(); ++K)
			{
				const FVector2D P0 = K > 0 ? Q[K - 1] : Q[K] * 2.0 - Q[K + 1], P1 = Q[K], P2 = Q[K + 1];
				const FVector2D P3 = K + 2 < Q.Num() ? Q[K + 2] : Q[K + 1] * 2.0 - Q[K];
				const double Sl = FVector2D::Distance(P1, P2);
				const int32 Ns = FMath::Max(1, FMath::CeilToInt(Sl / 8.0));
				for (int32 S = 0; S < Ns; ++S)
				{
					const double U = double(S) / Ns, U2 = U * U, U3 = U2 * U;
					const FVector2D Pt = 0.5 * ((2.0 * P1) + (P2 - P0) * U + (2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3) * U2 + (3.0 * P1 - P0 - 3.0 * P2 + P3) * U3);
					Rd.P.Add(TileDir(Pt.X, Pt.Y)); Rd.HalfW.Add(Hw);
				}
				TotalKm += Sl / 1000.0;
			}
			Rd.P.Add(TileDir(Q.Last().X, Q.Last().Y)); Rd.HalfW.Add(Hw);
			Roads.Add(MoveTemp(Rd));
		}
	}
	Stats[3] = TotalKm;
	// Evidence that roads follow the terrain: mean grade and total climb along each routed chain, against a
	// straight line between the same two ends (both sampled every 8 m on the smooth height grid).
	{
		auto HAt = [&](double X, double Y)
		{
			const double Fx = FMath::Clamp((X + Half) / Cs - 0.5, 0.0, CN - 1.001), Fy = FMath::Clamp((Y + Half) / Cs - 0.5, 0.0, CN - 1.001);
			const int32 I = int32(Fx), J = int32(Fy); const double A = Fx - I, B = Fy - J;
			return (1 - A) * (1 - B) * HC[J * CN + I] + A * (1 - B) * HC[J * CN + I + 1] + (1 - A) * B * HC[(J + 1) * CN + I] + A * B * HC[(J + 1) * CN + I + 1];
		};
		double RoadLen = 0, RoadGr = 0, RoadClimb = 0, LineLen = 0, LineGr = 0, LineClimb = 0, RoadMax = 0, LineMax = 0;
		for (const FRoad& Rd : Roads)
		{
			if (Rd.P.Num() < 3) continue;
			TArray<FVector2D> Xy;
			for (const FVector3d& P : Rd.P) Xy.Add(FVector2D(100000.0 * (P | E) / (P | C), 100000.0 * (P | N) / (P | C)));
			double Hp = HAt(Xy[0].X, Xy[0].Y);
			for (int32 K = 1; K < Xy.Num(); ++K)
			{
				const double L = FVector2D::Distance(Xy[K], Xy[K - 1]); if (L < 1e-3) continue;
				const double H = HAt(Xy[K].X, Xy[K].Y);
				RoadLen += L; RoadGr += FMath::Abs(H - Hp); RoadClimb += FMath::Max(H - Hp, 0.0); RoadMax = FMath::Max(RoadMax, FMath::Abs(H - Hp) / L); Hp = H;
			}
			const FVector2D A = Xy[0], B = Xy.Last();
			const double Lt = FVector2D::Distance(A, B); const int32 Ns = FMath::Max(1, int32(Lt / 8.0));
			Hp = HAt(A.X, A.Y);
			for (int32 K = 1; K <= Ns; ++K)
			{
				const FVector2D Q = A + (B - A) * (double(K) / Ns);
				const double H = HAt(Q.X, Q.Y);
				const double L = Lt / Ns;
				LineLen += L; LineGr += FMath::Abs(H - Hp); LineClimb += FMath::Max(H - Hp, 0.0); LineMax = FMath::Max(LineMax, FMath::Abs(H - Hp) / L); Hp = H;
			}
		}
		Stats[6] = RoadLen > 0 ? RoadGr / RoadLen : 0; Stats[7] = LineLen > 0 ? LineGr / LineLen : 0;
		UE_LOG(LogTemp, Display, TEXT("Ground: road grade: routed %.1f km mean %.2f %% max %.1f %% climb %.0f m | straight %.1f km mean %.2f %% max %.1f %% climb %.0f m"),
			RoadLen / 1000, 100 * Stats[6], 100 * RoadMax, RoadClimb, LineLen / 1000, 100 * Stats[7], 100 * LineMax, LineClimb);
	}
	Stats[4] = (FPlatformTime::Seconds() - T0) * 1000.0;
	UE_LOG(LogTemp, Display, TEXT("Ground: %d settlements in tile, %d links, %d routed, %d road chains, %.0f km of road, %lld A* expansions, %.0f ms"),
		Ls.Num(), Links.Num(), Paths.Num(), Roads.Num(), TotalKm, Expanded, Stats[4]);
}

// Fine distance fields over the tile: road distance signed by side (linear across a straight reach, so
// bilinear filtering keeps a 5 m road crisp on 16 m texels), unsigned (to reject sign flips at junctions),
// half-width, and the distance to the nearest river (field and wood margins along banks).
void FGroundLand::RasterFine(const FSurfaceTerrain& T)
{
	const double T0 = FPlatformTime::Seconds();
	const double Ft = 2.0 * Half / FN;
	Fine.SetNumUninitialized(FN * FN);
	TArray<float> Sd, Ud, Hw;
	Sd.Init(200.f, FN * FN); Ud.Init(200.f, FN * FN); Hw.Init(0.f, FN * FN);
	const double Rng = 90.0;
	const int32 Rt = FMath::CeilToInt(Rng / Ft) + 1;
	for (const FRoad& Rd : Roads)
	{
		TArray<FVector2D> Xy;
		for (const FVector3d& P : Rd.P) { double X, Y; TileXY(P, X, Y); X = 100000.0 * (P | E) / (P | C); Y = 100000.0 * (P | N) / (P | C); Xy.Add(FVector2D(X, Y)); }
		for (int32 K = 0; K + 1 < Xy.Num(); ++K)
		{
			const FVector2D A = Xy[K], B = Xy[K + 1], AB = B - A;
			const double L2 = FMath::Max(AB.SizeSquared(), 1e-6);
			const FVector2D Mid = (A + B) * 0.5;
			const int32 Ci = int32((Mid.X + Half) / Ft), Cj = int32((Mid.Y + Half) / Ft);
			for (int32 J = FMath::Max(Cj - Rt, 0); J <= FMath::Min(Cj + Rt, FN - 1); ++J)
				for (int32 I = FMath::Max(Ci - Rt, 0); I <= FMath::Min(Ci + Rt, FN - 1); ++I)
				{
					const FVector2D P((I + 0.5) * Ft - Half, (J + 0.5) * Ft - Half);
					const double U = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / L2, 0.0, 1.0);
					const FVector2D Q = A + AB * U;
					const double D = FVector2D::Distance(P, Q);
					const int32 Ix = J * FN + I;
					if (D < Ud[Ix])
					{
						Ud[Ix] = float(D);
						const double Side = FVector2D::CrossProduct(AB, P - A);
						Sd[Ix] = float(Side < 0.0 ? -D : D);
						Hw[Ix] = Rd.HalfW[K];
					}
				}
		}
	}
	const FVector3d Cc = C, Ee = E, Nn = N;
	ParallelFor(FN, [&](int32 J)
	{
		for (int32 I = 0; I < FN; ++I)
		{
			const FVector3d D = TileDir((I + 0.5) * Ft - Half, (J + 0.5) * Ft - Half);
			double Rd = 1e9, Rw = 0.0;
			T.RiverQuery(D, Rd, Rw);
			const int32 Ix = J * FN + I;
			Fine[Ix] = FFloat16Color(FLinearColor(Sd[Ix], Ud[Ix], Hw[Ix], float(FMath::Min(Rd, 2000.0))));
		}
	});
	Stats[5] = (FPlatformTime::Seconds() - T0) * 1000.0;
	UE_LOG(LogTemp, Display, TEXT("Ground: fine tile %d^2 (%.1f m texels) in %.0f ms"), FN, Ft, Stats[5]);
}
