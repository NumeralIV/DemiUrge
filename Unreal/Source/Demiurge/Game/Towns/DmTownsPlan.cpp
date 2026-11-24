// DEMIURGE â€” Game/Towns/DmTownsPlan.cpp : see the header.

#include "Game/Towns/DmTownsPlan.h"
#include "Game/Towns/DmTownsAssets.h"
#include "Game/Core/DmCoords.h"

#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"
#include "Algo/Reverse.h"
#include "Sim/State.h"

#include <limits>

using namespace DmTowns;

int32 FDmTownsWorldSnap::CellAt(const FVector& Dir, int32 Hint) const
{
	if (!Geo) return -1;
	const dm::Vec3 S = DmCoords::UEToSim(Dir);
	return Geo->cellAt(S.x, S.y, S.z, Hint);
}

const FDmKitInfo& FDmKitTable::Get(EDmTownMesh Id, int32 V) const
{
	static const FDmKitInfo None;
	const int32 K = int32(Id);
	if (K < 0 || K >= int32(EDmTownMesh::Num) || Infos[K].Num() == 0) return None;
	return Infos[K][FMath::Clamp(V, 0, Infos[K].Num() - 1)];
}

void DmTownsPlan::Frame(const FVector& Dir, FVector& E, FVector& N)
{
	// DmCoords::Basis is a reflection (east x north = -up); towns use a proper rotation frame: N = up x east
	DmCoords::Basis(Dir, E, N);
	N = FVector::CrossProduct(Dir.GetSafeNormal(), E).GetSafeNormal();
}

namespace
{
	constexpr double kRuu = 1.0e7;
	enum class EStyle : uint8 { Temperate, Arid, Tropical, Cold };
	enum class EStreet : uint8 { Main, Ring, Lane, Grid, Country, Highway };
	enum EOcc : uint8 { OFree = 0, OStreet = 1, OBuilt = 2, OBlocked = 3, OPlaza = 4, OWall = 5 };

	// ---------------------------------------------------------------- tables (index = dm::Age)
	const float kMainW[7] = { 4.f, 5.f, 6.f, 7.f, 10.f, 14.f, 16.f };
	const float kRingW[7] = { 3.5f, 4.5f, 5.f, 6.f, 9.f, 12.f, 13.f };
	const float kLaneW[7] = { 2.5f, 3.f, 3.5f, 4.f, 6.f, 8.f, 9.f };
	const float kGridW[7] = { 6.f, 6.f, 6.f, 7.f, 11.f, 14.f, 16.f };
	const float kCountryW[7] = { 2.5f, 3.f, 3.5f, 4.f, 6.f, 8.f, 9.f };
	const float kLamp[7] = { 0.f, 0.05f, 0.09f, 0.15f, 0.8f, 1.f, 1.1f };   // torches and lanterns, then electric light
	const float kLit[7] = { 0.45f, 0.42f, 0.36f, 0.42f, 0.5f, 0.58f, 0.62f };
	const float kPPH[7] = { 4.f, 5.f, 5.f, 6.f, 8.f, 14.f, 20.f };
	const int32 kNMin[5] = { 3, 8, 30, 110, 380 };
	const int32 kNMax[5] = { 9, 60, 240, 800, 2400 };
	// land per building (m2, streets and yards included) by tier, and its growth with the age (bigger buildings, sprawl)
	const float kLot[5] = { 120.f, 1500.f, 650.f, 480.f, 420.f };
	const float kLotAge[7] = { 1.f, 1.f, 1.f, 1.1f, 1.5f, 2.2f, 2.6f };
	const float kPlazaR[5] = { 0.f, 12.f, 20.f, 30.f, 42.f };
	const float kRings[7] = { 110.f, 200.f, 340.f, 560.f, 900.f, 1350.f, 1900.f };
	const float kFieldL[7] = { 30.f, 70.f, 110.f, 140.f, 200.f, 300.f, 360.f };
	const float kFieldW[7] = { 16.f, 24.f, 34.f, 44.f, 60.f, 90.f, 110.f };
	const float kPierL[7] = { 12.f, 18.f, 26.f, 36.f, 70.f, 110.f, 140.f };

	FLinearColor Lc(float R, float G, float B) { return FLinearColor(R, G, B, 1.f); }

	// wall tints by surface pattern (multipliers of the ~0.85 vertex albedo)
	const FLinearColor kPlaster[] = { Lc(1.0f, 0.95f, 0.84f), Lc(1.06f, 1.04f, 1.0f), Lc(1.0f, 0.84f, 0.58f), Lc(1.02f, 0.86f, 0.8f), Lc(0.86f, 0.93f, 1.0f), Lc(0.9f, 0.96f, 0.82f), Lc(0.98f, 0.9f, 0.72f) };
	const FLinearColor kPlasterArid[] = { Lc(1.06f, 1.04f, 1.0f), Lc(1.0f, 0.93f, 0.8f), Lc(0.98f, 0.86f, 0.66f), Lc(1.04f, 1.0f, 0.94f) };
	const FLinearColor kPlasterTrop[] = { Lc(1.0f, 0.78f, 0.5f), Lc(0.7f, 0.92f, 0.9f), Lc(1.0f, 0.72f, 0.7f), Lc(1.04f, 1.02f, 0.96f), Lc(0.9f, 0.98f, 0.7f) };
	const FLinearColor kStone[] = { Lc(0.82f, 0.8f, 0.75f), Lc(0.92f, 0.86f, 0.75f), Lc(0.72f, 0.7f, 0.68f), Lc(0.86f, 0.8f, 0.7f) };
	const FLinearColor kBrick[] = { Lc(0.72f, 0.38f, 0.28f), Lc(0.8f, 0.46f, 0.34f), Lc(0.62f, 0.34f, 0.28f), Lc(0.86f, 0.58f, 0.44f), Lc(0.55f, 0.3f, 0.26f) };
	const FLinearColor kMud[] = { Lc(0.98f, 0.82f, 0.6f), Lc(1.04f, 0.9f, 0.68f), Lc(0.9f, 0.72f, 0.52f), Lc(0.96f, 0.86f, 0.72f) };
	const FLinearColor kWood[] = { Lc(0.64f, 0.5f, 0.36f), Lc(0.52f, 0.39f, 0.28f), Lc(0.72f, 0.58f, 0.42f), Lc(0.7f, 0.28f, 0.2f) };
	const FLinearColor kConcrete[] = { Lc(0.94f, 0.94f, 0.92f), Lc(0.84f, 0.84f, 0.82f), Lc(1.02f, 1.0f, 0.95f), Lc(0.9f, 0.86f, 0.8f) };
	const FLinearColor kCurtain[] = { Lc(0.95f, 1.05f, 1.15f), Lc(0.75f, 0.85f, 0.9f), Lc(1.05f, 1.0f, 0.9f), Lc(0.7f, 0.78f, 0.8f) };
	const FLinearColor kPaint[] = { Lc(1.08f, 1.08f, 1.06f), Lc(1.04f, 0.98f, 0.88f), Lc(0.96f, 1.02f, 1.08f), Lc(1.06f, 0.96f, 0.86f) };
	const FLinearColor kHide[] = { Lc(1.0f, 0.92f, 0.78f), Lc(0.9f, 0.8f, 0.66f), Lc(1.06f, 1.0f, 0.88f) };
	// roof tints
	const FLinearColor kThatch[] = { Lc(1.0f, 0.88f, 0.62f), Lc(0.9f, 0.78f, 0.54f), Lc(0.8f, 0.7f, 0.5f), Lc(0.72f, 0.64f, 0.48f) };
	const FLinearColor kTile[] = { Lc(0.86f, 0.36f, 0.22f), Lc(0.76f, 0.3f, 0.2f), Lc(0.92f, 0.46f, 0.28f), Lc(0.62f, 0.27f, 0.2f), Lc(0.8f, 0.4f, 0.26f) };
	const FLinearColor kSlate[] = { Lc(0.46f, 0.48f, 0.53f), Lc(0.38f, 0.39f, 0.43f), Lc(0.54f, 0.55f, 0.58f), Lc(0.44f, 0.42f, 0.44f) };
	const FLinearColor kMetal[] = { Lc(0.7f, 0.71f, 0.74f), Lc(0.56f, 0.64f, 0.6f), Lc(0.8f, 0.42f, 0.34f), Lc(0.62f, 0.62f, 0.66f) };

	template <int32 N> const FLinearColor& PickC(const FLinearColor (&A)[N], uint32 H) { return A[H % uint32(N)]; }

	struct FStreet
	{
		TArray<FVector2D> P;
		float W = 4.f;
		EStreet Kind = EStreet::Lane;
		bool bGridZone = false;
	};

	struct FPlaced
	{
		EDmTownMesh M; int32 V; FVector2D C; double Yaw; FVector3f Scale; FLinearColor Wall, Roof; float Lit; float State; uint32 Key;
		bool bWaterLevel = false;   // piers and ships stand on the sea surface
		float ZOff = 0.f;           // metres above the chosen floor
		bool bRuinAsset = false;    // (wave 4 towns) a dead town's building drawn as its real asset's ruin
	};

	struct FFieldQuad { FVector2D C[4]; FLinearColor Col; float RowAng; bool bCircle = false; FVector2D Ctr; float Rad = 0.f; };
	// (wave 4 towns) organic field parcels: convex polygons in the town's local metres
	struct FParcel { TArray<FVector2D> Poly; FVector2D C = FVector2D::ZeroVector; float Ang = 0.f; FLinearColor Col = FLinearColor::White; uint32 H = 0; bool bCircle = false; float Rad = 0.f; };
	struct FHedge { FVector2D A, B; };

	/** Keeps the part of a convex polygon where dot(N, x) <= D (Sutherland-Hodgman, one plane). */
	void ClipHalf(TArray<FVector2D>& P, const FVector2D& N, double D)
	{
		if (P.Num() < 3) { P.Reset(); return; }
		TArray<FVector2D, TInlineAllocator<24>> O;
		for (int32 I = 0; I < P.Num(); ++I)
		{
			const FVector2D& A = P[I];
			const FVector2D& B = P[(I + 1) % P.Num()];
			const double Da = FVector2D::DotProduct(N, A) - D, Db = FVector2D::DotProduct(N, B) - D;
			if (Da <= 0.0) O.Add(A);
			if ((Da <= 0.0) != (Db <= 0.0)) O.Add(A + (B - A) * (Da / (Da - Db)));
		}
		P.Reset();
		P.Append(O.GetData(), O.Num());
		if (P.Num() < 3) P.Reset();
	}
	double PolySigned(const TArray<FVector2D>& P)
	{
		double A = 0.0;
		for (int32 I = 0; I < P.Num(); ++I) { const FVector2D& U = P[I]; const FVector2D& V = P[(I + 1) % P.Num()]; A += U.X * V.Y - V.X * U.Y; }
		return 0.5 * A;
	}
	double PolyArea(const TArray<FVector2D>& P) { return FMath::Abs(PolySigned(P)); }
	FVector2D PolyCentroid(const TArray<FVector2D>& P)
	{
		double A = 0.0, Cx = 0.0, Cy = 0.0;
		for (int32 I = 0; I < P.Num(); ++I)
		{
			const FVector2D& U = P[I]; const FVector2D& V = P[(I + 1) % P.Num()];
			const double K = U.X * V.Y - V.X * U.Y;
			A += K; Cx += (U.X + V.X) * K; Cy += (U.Y + V.Y) * K;
		}
		if (FMath::Abs(A) < 1e-9) { FVector2D S = FVector2D::ZeroVector; for (const FVector2D& Q : P) S += Q; return P.Num() ? S / double(P.Num()) : S; }
		return FVector2D(Cx / (3.0 * A), Cy / (3.0 * A));
	}
	/** Shrinks a convex polygon by B metres (every edge moved inward). */
	void Inset(TArray<FVector2D>& P, double B)
	{
		if (P.Num() < 3) return;
		if (PolySigned(P) < 0.0) Algo::Reverse(P);
		const TArray<FVector2D> E = P;
		for (int32 I = 0; I < E.Num() && P.Num() >= 3; ++I)
		{
			const FVector2D& A = E[I];
			const FVector2D D = (E[(I + 1) % E.Num()] - A);
			const double Len = D.Size();
			if (Len < 1e-6) continue;
			const FVector2D Nout(D.Y / Len, -D.X / Len);   // outward for a counter-clockwise polygon
			ClipHalf(P, Nout, FVector2D::DotProduct(Nout, A) - B);
		}
	}

	float Sat(float X) { return FMath::Clamp(X, 0.f, 1.f); }

	class FPlanner
	{
	public:
		FPlanner(const FDmTownInput& InIn, const FDmTownsWorldSnap& InSnap, const FDmTownsGround& InG, const FDmKitTable& InKit, FDmTownPlan& InOut)
			: In(InIn), Snap(InSnap), G(InG), Kit(InKit), Out(InOut) {}

		void Run();
		/** (wave 4 towns) one time slice of the plan; true when complete (or cancelled) */
		bool Step(double BudgetMs);
		const std::atomic<bool>* Cancel = nullptr;
		bool bCancelled = false;
		bool Cancelled() const { return Cancel && Cancel->load(std::memory_order_relaxed); }

	private:
		int32 Stage = 0;
		double WorkMs = 0.0, GridMs = 0.0;
		double StageMs[9] = {};                  // (wave 4 towns) CPU per plan phase
		// height grid build state (sliced in bands of rows)
		static constexpr int32 LSt = 8;
		int32 LN = 0, GridHint = -1, GridRow = 0;
		// (verifier) the raster and the field overlay are sliced too (rows / parcels): unsliced they were the longest slices
		// (raster median 123 ms, p90 3.0 s wall; overlay median 171 ms, p90 1.2 s wall on the loaded machine)
		int32 RasterRow = -1, OverlayParcel = -1;
		int32 NumRiverSegs = 0, RiverCells = 0; double RiverBankDbg = 0;   // (verifier) diagnostics: river reaches near the town, grid nodes they block
		bool bDeadDone = false;
		TArray<uint8> LkC;
		bool bAnyLake = false;
		const FDmTownInput& In;
		const FDmTownsWorldSnap& Snap;
		const FDmTownsGround& G;
		const FDmKitTable& Kit;
		FDmTownPlan& Out;

		FVector U0, E0, N0;
		EStyle Style = EStyle::Temperate;
		int32 Age = 0, Tier = 0;
		uint32 Seed = 1;
		float R = 100.f, Ex = 300.f, ExBase = 300.f, SeaM = 0.f;
		int32 Target = 10;
		FVector2D Oc = FVector2D::ZeroVector;    // plaza centre
		float PlazaR = 0.f;

		// height grid
		int32 GN = 0; float GS = 10.f;
		TArray<float> Hg; TArray<uint8> Lg;
		// occupancy raster
		int32 ON = 0; float OS = 2.f, OE = 100.f;
		TArray<uint8> Occ;
		// streets + point hash
		TArray<FStreet> Streets;
		struct FPt { FVector2D P; int32 S; };
		TArray<FPt> Pts; TArray<int32> Head, NextPt; int32 HN = 0; float HS = 16.f;
		TArray<double> SpokeAng;
		bool bCoastal = false; double SeaAng = 0.0; float SeaDist = 1e9f;
		TArray<FVector2D> GridWedges;            // (a0, a1) of grid districts
		bool bIndustry = false; double IndAng = 0.0;
		TArray<FPlaced> Placed;
		TArray<FFieldQuad> FieldQ;
		TArray<FParcel> Parcels;                 // (wave 4 towns) organic field parcels
		TArray<FHedge> Hedges;
		float WarpA = 0.f, WarpL = 1.f;          // the shared boundary warp of the fields (amplitude, wavelength, m)
		FVector2D Warp(const FVector2D& P) const
		{
			if (WarpA <= 0.f) return P;
			const float X = float(P.X / WarpL), Y = float(P.Y / WarpL);
			return P + FVector2D(Noise(X, Y, 901u), Noise(X + 7.3f, Y - 3.1f, 902u)) * double(WarpA);
		}
		struct FQuay { TArray<FVector2D> P; };
		TArray<FQuay> Quays;
		float BuiltR = 0.f;
		bool bFarPort = false;                   // a port whose drawn shore lies beyond the town (harbour quarter at the coast)
		float ShoreR = 0.f;                      // how far the shore search reaches

		// ---------------------------------------------------------------- geometry
		FVector DirAt(const FVector2D& P) const { return (U0 * kRuu + (E0 * P.X + N0 * P.Y) * 100.0).GetSafeNormal(); }
		float GridH(const FVector2D& P) const
		{
			const float Fx = FMath::Clamp(float((P.X + Ex) / GS), 0.f, float(GN - 1) - 0.001f), Fy = FMath::Clamp(float((P.Y + Ex) / GS), 0.f, float(GN - 1) - 0.001f);
			const int32 X0 = int32(Fx), Y0 = int32(Fy); const float Tx = Fx - X0, Ty = Fy - Y0;
			auto H = [&](int32 X, int32 Y) { return Hg[Y * GN + X]; };
			return FMath::Lerp(FMath::Lerp(H(X0, Y0), H(X0 + 1, Y0), Tx), FMath::Lerp(H(X0, Y0 + 1), H(X0 + 1, Y0 + 1), Tx), Ty);
		}
		bool InGrid(const FVector2D& P) const { return FMath::Abs(P.X) < Ex - GS && FMath::Abs(P.Y) < Ex - GS; }
		bool LandAt(const FVector2D& P) const
		{
			if (!InGrid(P)) return false;
			const int32 X = FMath::RoundToInt((P.X + Ex) / GS), Y = FMath::RoundToInt((P.Y + Ex) / GS);
			if (X < 0 || Y < 0 || X >= GN || Y >= GN) return false;
			return Lg[Y * GN + X] != 0 && GridH(P) > SeaM + 0.6f;
		}
		float SlopeAt(const FVector2D& P) const
		{
			const float D = GS;
			const float Hx = GridH(P + FVector2D(D, 0)) - GridH(P - FVector2D(D, 0));
			const float Hy = GridH(P + FVector2D(0, D)) - GridH(P - FVector2D(0, D));
			return FMath::Sqrt(Hx * Hx + Hy * Hy) / (2.f * D);
		}
		FVector2D GradAt(const FVector2D& P) const
		{
			const float D = GS;
			return FVector2D(GridH(P + FVector2D(D, 0)) - GridH(P - FVector2D(D, 0)), GridH(P + FVector2D(0, D)) - GridH(P - FVector2D(0, D))) / (2.f * D);
		}
		float Noise(float X, float Y, uint32 S) const { return Fbm2(X, Y, 3, Seed * 31u + S); }

		// ---------------------------------------------------------------- raster
		bool ToCell(const FVector2D& P, int32& X, int32& Y) const
		{
			X = int32(FMath::FloorToInt((P.X + OE) / OS)); Y = int32(FMath::FloorToInt((P.Y + OE) / OS));
			return X >= 0 && Y >= 0 && X < ON && Y < ON;
		}
		FVector2D CellC(int32 X, int32 Y) const { return FVector2D(-OE + (X + 0.5) * OS, -OE + (Y + 0.5) * OS); }
		uint8 OccAt(const FVector2D& P) const { int32 X, Y; return ToCell(P, X, Y) ? Occ[Y * ON + X] : uint8(OBlocked); }
		void MarkCapsule(const FVector2D& A, const FVector2D& B, float Hw, uint8 V, bool bOnlyFree = false)
		{
			const FVector2D Lo(FMath::Min(A.X, B.X) - Hw, FMath::Min(A.Y, B.Y) - Hw), Hi(FMath::Max(A.X, B.X) + Hw, FMath::Max(A.Y, B.Y) + Hw);
			int32 X0, Y0, X1, Y1;
			ToCell(Lo, X0, Y0); ToCell(Hi, X1, Y1);
			X0 = FMath::Max(X0, 0); Y0 = FMath::Max(Y0, 0); X1 = FMath::Min(X1, ON - 1); Y1 = FMath::Min(Y1, ON - 1);
			const FVector2D D = B - A; const double L2 = FMath::Max(D.SizeSquared(), 1e-6);
			for (int32 Y = Y0; Y <= Y1; ++Y)
				for (int32 X = X0; X <= X1; ++X)
				{
					const FVector2D C = CellC(X, Y);
					const double T = FMath::Clamp(FVector2D::DotProduct(C - A, D) / L2, 0.0, 1.0);
					if (FVector2D::DistSquared(C, A + D * T) <= double(Hw) * Hw)
					{
						uint8& O = Occ[Y * ON + X];
						if (!bOnlyFree || O == OFree) O = V;
					}
				}
		}
		void MarkDisc(const FVector2D& C, float Rr, uint8 V) { MarkCapsule(C, C + FVector2D(0.01, 0), Rr, V); }
		/** Rectangle (centre, yaw, half extents): all raster cells whose centres lie inside the rectangle
		 *  shrunk by Shrink must be free (and inside the raster). */
		bool RectFree(const FVector2D& C, double Yaw, float Hw, float Hd, float Shrink = 1.05f) const
		{
			const FVector2D Ux(FMath::Cos(Yaw), FMath::Sin(Yaw)), Uy(-FMath::Sin(Yaw), FMath::Cos(Yaw));
			const float Ew = FMath::Max(Hw - Shrink, 0.2f), Ed = FMath::Max(Hd - Shrink, 0.2f);
			const float Ext = FMath::Sqrt(Hw * Hw + Hd * Hd);
			int32 X0, Y0, X1, Y1;
			if (!ToCell(C - FVector2D(Ext, Ext), X0, Y0) || !ToCell(C + FVector2D(Ext, Ext), X1, Y1)) return false;
			for (int32 Y = Y0; Y <= Y1; ++Y)
				for (int32 X = X0; X <= X1; ++X)
				{
					const FVector2D Q = CellC(X, Y) - C;
					if (FMath::Abs(FVector2D::DotProduct(Q, Ux)) <= Ew && FMath::Abs(FVector2D::DotProduct(Q, Uy)) <= Ed && Occ[Y * ON + X] != OFree) return false;
				}
			// the centre and corners must also be inside the raster on land
			return true;
		}
		void MarkRect(const FVector2D& C, double Yaw, float Hw, float Hd, uint8 V)
		{
			const FVector2D Ux(FMath::Cos(Yaw), FMath::Sin(Yaw)), Uy(-FMath::Sin(Yaw), FMath::Cos(Yaw));
			const float Ext = FMath::Sqrt(Hw * Hw + Hd * Hd);
			int32 X0, Y0, X1, Y1;
			ToCell(C - FVector2D(Ext, Ext), X0, Y0); ToCell(C + FVector2D(Ext, Ext), X1, Y1);
			X0 = FMath::Max(X0, 0); Y0 = FMath::Max(Y0, 0); X1 = FMath::Min(X1, ON - 1); Y1 = FMath::Min(Y1, ON - 1);
			for (int32 Y = Y0; Y <= Y1; ++Y)
				for (int32 X = X0; X <= X1; ++X)
				{
					const FVector2D Q = CellC(X, Y) - C;
					if (FMath::Abs(FVector2D::DotProduct(Q, Ux)) <= Hw && FMath::Abs(FVector2D::DotProduct(Q, Uy)) <= Hd) Occ[Y * ON + X] = V;
				}
		}

		// ---------------------------------------------------------------- street points
		void HashAdd(const FVector2D& P, int32 S)
		{
			const int32 X = FMath::Clamp(int32((P.X + Ex) / HS), 0, HN - 1), Y = FMath::Clamp(int32((P.Y + Ex) / HS), 0, HN - 1);
			const int32 I = Pts.Add({ P, S });
			NextPt.Add(Head[Y * HN + X]);
			Head[Y * HN + X] = I;
		}
		/** Nearest street point within Rr of P not on street Skip; -1 if none. */
		int32 NearPt(const FVector2D& P, float Rr, int32 Skip) const
		{
			const int32 Cx = int32((P.X + Ex) / HS), Cy = int32((P.Y + Ex) / HS);
			const int32 Span = int32(Rr / HS) + 1;
			int32 Best = -1; double BestD = double(Rr) * Rr;
			for (int32 Y = Cy - Span; Y <= Cy + Span; ++Y)
				for (int32 X = Cx - Span; X <= Cx + Span; ++X)
				{
					if (X < 0 || Y < 0 || X >= HN || Y >= HN) continue;
					for (int32 I = Head[Y * HN + X]; I >= 0; I = NextPt[I])
					{
						if (Pts[I].S == Skip) continue;
						const double D2 = FVector2D::DistSquared(Pts[I].P, P);
						if (D2 < BestD) { BestD = D2; Best = I; }
					}
				}
			return Best;
		}
		int32 AddStreet(FStreet&& S)
		{
			if (S.P.Num() < 2) return -1;
			const int32 Id = Streets.Add(MoveTemp(S));
			for (const FVector2D& P : Streets[Id].P) HashAdd(P, Id);
			return Id;
		}

		/** Terrain-following walker. Pull: desired heading (radians) or NaN. */
		TArray<FVector2D> Walk(FVector2D P, double H, double MaxLen, double Pull, uint32 S, double Step, bool bStopOnHit, int32 Self, double MaxR) const
		{
			TArray<FVector2D> Pts2;
			Pts2.Add(P);
			double Len = 0;
			const double Offs[5] = { -0.5, -0.25, 0.0, 0.25, 0.5 };
			int32 K = 0;
			while (Len < MaxLen)
			{
				double BestC = 1e30; FVector2D BestP = P; double BestH = H;
				const double Wob = 0.35 * Noise(float(K) * 0.11f, 0.37f, S);
				for (double O : Offs)
				{
					const double Hh = H + O;
					const FVector2D Q = P + FVector2D(FMath::Cos(Hh), FMath::Sin(Hh)) * Step;
					if (!LandAt(Q) || Q.Size() > MaxR) continue;
					const double Grade = FMath::Abs(GridH(Q) - GridH(P)) / Step;
					double C = 9.0 * Grade + 0.45 * FMath::Abs(O) + 0.25 * FMath::Abs(FMath::FindDeltaAngleRadians(Hh, H + Wob));
					if (FMath::IsFinite(Pull)) C += 0.7 * FMath::Abs(FMath::FindDeltaAngleRadians(Hh, Pull));
					if (Grade > 0.35) C += 50.0;
					if (C < BestC) { BestC = C; BestP = Q; BestH = Hh; }
				}
				if (BestC >= 1e29) break;
				P = BestP; H = BestH; Len += Step; ++K;
				Pts2.Add(P);
				if (bStopOnHit && Len > 14.0)
				{
					const int32 Hit = NearPt(P, 9.f, Self);
					if (Hit >= 0) { Pts2.Add(this->Pts[Hit].P); break; }
				}
			}
			return Pts2;
		}

		// ---------------------------------------------------------------- steps
		void Setup();
		void BuildGridBegin();
		bool BuildGridRows(double T0, double BudgetMs);
		void BuildGridEnd();
		void BuildRaster();
		bool BuildRasterRows(double T0, double BudgetMs);
		void FindCoast();
		void PlanStreets();
		void RasterStreets();
		void PlanWalls();
		void PlanDocks();
		void PlanCivic();
		void PlanHouses();
		void PlanFields();
		void BuildOverlay();
		bool BuildOverlayFields(double T0, double BudgetMs);
		int32 OverlayVert(const FVector2D& P, float Lift, const FVector2f& Uv0, const FColor& C, const FVector2f& Uv1);
		void EmitInstances();

		bool Place(EDmTownMesh M, int32 V, const FVector2D& C, double Yaw, float Scale = 1.f, float Margin = 0.6f, bool bTest = true, uint32 Key = 0);
		bool PlaceNear(EDmTownMesh M, int32 V, const FVector2D& Want, double Yaw, float MaxDist, float Scale = 1.f);
		void Tints(EDmTownMesh M, int32 V, uint32 H, FLinearColor& Wall, FLinearColor& Roof) const;
		void PickHome(float R01, uint32 H, bool bGrid, EDmTownMesh& M, int32& V) const;
	};

	// ================================================================ setup and terrain

	void FPlanner::Setup()
	{
		U0 = In.Dir.GetSafeNormal();
		DmTownsPlan::Frame(U0, E0, N0);
		Age = FMath::Clamp(In.Age, 0, 6);
		Tier = FMath::Clamp(In.Tier, 0, 4);
		Seed = Hash(uint32(In.Id + 1), In.Seed);
		SeaM = float(Snap.SeaLevel * 200.0);
		// climate style from the centre cell
		const int32 C = In.Cell;
		const float T = (C >= 0 && C < Snap.N) ? Snap.Temp[C] : 12.f;
		const float Mo = (C >= 0 && C < Snap.N) ? Snap.Moist[C] : 0.5f;
		Style = (T < 1.f) ? EStyle::Cold : ((Mo < 0.3f && T > 14.f) ? EStyle::Arid : ((T > 21.f && Mo > 0.55f) ? EStyle::Tropical : EStyle::Temperate));
		// size from the population
		Target = FMath::Clamp(FMath::RoundToInt(float(In.Pop) / kPPH[Age]), kNMin[Tier], kNMax[Tier]);
		R = DmTownsPlan::BuiltRadius(Tier, In.Pop, Age) * In.Scale;
		PlazaR = kPlazaR[Tier];
		Ex = FMath::Clamp(R * 1.4f + 1150.f, 500.f, 3400.f);
		if (Tier == 0) Ex = 260.f;
		ExBase = Ex;
		// a port whose sea lies beyond the planned area: widen the area so the harbour quarter fits (verifier fix)
		if (In.B[dm::Building::PORT] && Tier >= 1 && Snap.Geo && C >= 0 && C < Snap.N)
		{
			double Best = 1e9;
			TArray<int32> Q; TSet<int32> Seen;
			Q.Add(C); Seen.Add(C);
			for (int32 H = 0; H < Q.Num() && Q.Num() < 300; ++H)
			{
				const int32 Ci = Q[H];
				const float* Pc = &Snap.Geo->pos[size_t(Ci) * 3];
				const double Ang = FMath::Acos(FMath::Clamp(FVector::DotProduct(DmCoords::SimToUE(Pc[0], Pc[1], Pc[2]).GetSafeNormal(), U0), -1.0, 1.0));
				if (Ang > 0.045) continue;
				if (Snap.Elev[Ci] < Snap.SeaLevel) { Best = FMath::Min(Best, Ang * kRuu * 0.01); continue; }
				for (int32 K = 0; K < Snap.Geo->nbrN[size_t(Ci)]; ++K)
				{
					const int32 M = Snap.Geo->nbr[size_t(6 * Ci + K)];
					if (!Seen.Contains(M)) { Seen.Add(M); Q.Add(M); }
				}
			}
			// the sea cell's centre is ~1 km past its shore; leave room for the piers
			if (Best < 1e8) Ex = FMath::Clamp(FMath::Max(Ex, float(Best) + 500.f), Ex, 3400.f);
		}
	}

	void FPlanner::BuildGridBegin()
	{
		// a port's widened area keeps the grid spacing of its town (at most 360^2 samples)
		GS = FMath::Max(FMath::Clamp(ExBase / 110.f, 5.f, 22.f), Ex / 180.f);
		GN = int32(2.f * Ex / GS) + 1;
		Hg.SetNumUninitialized(GN * GN);
		Lg.SetNumUninitialized(GN * GN);
		// lakes vary per sim cell (km): a coarse pass every 8th node says where the exact test is needed
		LN = (GN + LSt - 1) / LSt + 1;
		LkC.SetNumZeroed(LN * LN);
		bAnyLake = false;
		for (int32 Y = 0; Y < LN; ++Y)
			for (int32 X = 0; X < LN; ++X)
				if (G.LakeSurfaceM(DirAt(FVector2D(-Ex + FMath::Min(X * LSt, GN - 1) * GS, -Ex + FMath::Min(Y * LSt, GN - 1) * GS))) > -1.0e8) { LkC[Y * LN + X] = 1; bAnyLake = true; }
		GridHint = In.Cell;
		GridRow = 0;
	}

	bool FPlanner::BuildGridRows(double T0, double BudgetMs)
	{
		// (wave 4 towns) in bands of rows, so a time slice ends inside the grid (a city's 360^2 samples are its longest phase)
		auto LakeNear = [&](int32 X, int32 Y) { const int32 Cx = X / LSt, Cy = Y / LSt; return LkC[Cy * LN + Cx] | LkC[Cy * LN + Cx + 1] | LkC[(Cy + 1) * LN + Cx] | LkC[(Cy + 1) * LN + Cx + 1]; };
		int32& Hint = GridHint;
		const bool bPlanetLakes = G.GetSource() == FDmTownsGround::ESource::PlanetTerrain;
		for (; GridRow < GN; ++GridRow)
		{
			if ((GridRow & 7) == 0 && GridRow > 0 && ((FPlatformTime::Seconds() - T0) * 1000.0 >= BudgetMs || Cancelled())) return false;
			const int32 Y = GridRow;
			for (int32 X = 0; X < GN; ++X)
			{
				const FVector2D P(-Ex + X * GS, -Ex + Y * GS);
				const FVector D = DirAt(P);
				// full detail: a band-limited sample (G.HeightM(D, GS / 2), ~25% cheaper) differed by up to 7 m at coasts,
				// which put a plaza that the grid called dry 6 m under the sea (verifier, tried and reverted)
				const float H = float(G.HeightM(D));
				bool bLand = H > SeaM + 0.6f;
				if (bLand && !bPlanetLakes)
				{
					// sim lake cells (the planet terrain's own drawn lakes are tested exactly below instead)
					const int32 C = Snap.CellAt(D, Hint);
					Hint = C;
					if (C >= 0 && C < Snap.N && Snap.Elev[C] >= Snap.SeaLevel && Snap.Water[C] > 0.06f) bLand = false;
				}
				// (verifier fix) the lakes the planet draws (fields, houses and streets were laid over them)
				if (bLand && bAnyLake && LakeNear(X, Y) && G.LakeSurfaceM(D) > double(H) - 0.2) bLand = false;
				Hg[Y * GN + X] = H;
				Lg[Y * GN + X] = bLand ? 1 : 0;
			}
		}
		return true;
	}

	void FPlanner::BuildGridEnd()
	{
		// (verifier fix) the rivers the planet draws (fields and houses were laid across them): every reach near the
		// settlement is rasterised into the land grid as a capsule of its half-width plus a margin
		{
			TArray<FVector> Ra, Rb; TArray<float> Rw;
			G.RiverSegments(U0, double(Ex) * 1.45 / 1.0e5, Ra, Rb, Rw);
			NumRiverSegs = Ra.Num();
			RiverBankDbg = G.RiverBankM(U0);
			auto Local = [&](const FVector& D) { const double K = 1.0e5 / FMath::Max(FVector::DotProduct(D, U0), 0.5); return FVector2D(FVector::DotProduct(D, E0) * K, FVector::DotProduct(D, N0) * K); };
			for (int32 I = 0; I < Ra.Num(); ++I)
			{
				const FVector2D A = Local(Ra[I]), B = Local(Rb[I]);
				const float Hw = Rw[I] + GS * 0.6f + 3.f;
				const int32 X0 = FMath::Max(0, FMath::FloorToInt((FMath::Min(A.X, B.X) - Hw + Ex) / GS)), X1 = FMath::Min(GN - 1, FMath::CeilToInt((FMath::Max(A.X, B.X) + Hw + Ex) / GS));
				const int32 Y0 = FMath::Max(0, FMath::FloorToInt((FMath::Min(A.Y, B.Y) - Hw + Ex) / GS)), Y1 = FMath::Min(GN - 1, FMath::CeilToInt((FMath::Max(A.Y, B.Y) + Hw + Ex) / GS));
				const FVector2D Dd = B - A; const double L2 = FMath::Max(Dd.SizeSquared(), 1e-6);
				for (int32 Y = Y0; Y <= Y1; ++Y)
					for (int32 X = X0; X <= X1; ++X)
					{
						const FVector2D P(-Ex + X * GS, -Ex + Y * GS);
						const double T = FMath::Clamp(FVector2D::DotProduct(P - A, Dd) / L2, 0.0, 1.0);
						if (FVector2D::DistSquared(P, A + Dd * T) < double(Hw) * Hw) { RiverCells += Lg[Y * GN + X]; Lg[Y * GN + X] = 0; }
					}
			}
		}
		HS = 16.f;
		HN = int32(2.f * Ex / HS) + 1;
		Head.Init(-1, HN * HN);
	}

	void FPlanner::BuildRaster()
	{
		// occupancy raster over the built area (and out to a far harbour)
		OE = FMath::Min(Ex - GS * 2.f, FMath::Max(R * 1.45f + 90.f, bFarPort ? SeaDist + 140.f : 0.f));
		OS = OE > 1600.f ? 3.f : 2.f;
		ON = int32(2.f * OE / OS) + 1;
		Occ.SetNumZeroed(ON * ON);
		RasterRow = 0;
	}

	bool FPlanner::BuildRasterRows(double T0, double BudgetMs)
	{
		// (verifier) in bands of rows, like the height grid: a city's raster is up to ~1,000^2 land and slope tests
		for (; RasterRow < ON; ++RasterRow)
		{
			if ((RasterRow & 7) == 0 && RasterRow > 0 && ((FPlatformTime::Seconds() - T0) * 1000.0 >= BudgetMs || Cancelled())) return false;
			const int32 Y = RasterRow;
			for (int32 X = 0; X < ON; ++X)
			{
				const FVector2D C = CellC(X, Y);
				if (!LandAt(C) || SlopeAt(C) > 0.42f) Occ[Y * ON + X] = OBlocked;
			}
		}
		return true;
	}

	void FPlanner::FindCoast()
	{
		// the nearest water along 48 rays from the centre; a port looks further: the sim calls a cell coastal when
		// the sea is one cell (~2 km) away, and the drawn shore often lies beyond the town (verifier fix: 45 of 73
		// ports of the fixture had no harbour at all)
		const bool bPortB = In.B[dm::Building::PORT] != 0 && Tier >= 1;
		const double Reach = bPortB ? double(Ex) - 4.0 * GS : FMath::Min(double(Ex) - 2.0 * GS, R * 1.6 + 200.0);
		for (int32 K = 0; K < 48; ++K)
		{
			const double A = K * UE_DOUBLE_TWO_PI / 48.0;
			const FVector2D D(FMath::Cos(A), FMath::Sin(A));
			for (double Rr = 10.0; Rr < Reach; Rr += 8.0)
			{
				const FVector2D P = D * Rr;
				if (!InGrid(P)) break;
				if (GridH(P) < SeaM - 0.2f)
				{
					if (Rr < SeaDist) { SeaDist = float(Rr); SeaAng = A; }
					break;
				}
			}
		}
		bCoastal = SeaDist < R * 1.4f + 150.f;
		if (!bCoastal && bPortB && SeaDist < 1e8f) { bCoastal = true; bFarPort = true; }
		ShoreR = bFarPort ? SeaDist + 420.f : R * 1.5f + 250.f;
		// plaza: the centre cell's point, moved onto dry, gentle ground if needed
		Oc = FVector2D::ZeroVector;
		if (!LandAt(Oc) || SlopeAt(Oc) > 0.2f)
		{
			// the centre cell is wet or steep on the drawn ground (coasts differ from the sim's cells): the town
			// stands on the nearest dry, gentle ground, preferring room around it
			// (verifier fix) the spiral stopped at 4000 steps = 380 m, far short of MaxR, and a town without a roomy
			// spot stayed in the water (6 of the fixture's 96 plazas were at or below sea level; one 5 m under it).
			// Search out to MaxR and fall back to the dry spot with the most room.
			const double MaxR = FMath::Min(double(Ex) * 0.55, 1200.0);
			int32 BestRoom = -1; FVector2D BestP = Oc;
			for (int32 K = 0; K < 40000; ++K)
			{
				const double A = K * 2.399963, Rr = 6.0 * FMath::Sqrt(double(K));
				if (Rr > MaxR) break;
				const FVector2D P(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr);
				if (!LandAt(P) || SlopeAt(P) > 0.2f) continue;
				int32 Room = 0;
				for (int32 J = 0; J < 8; ++J) { const double B = J * UE_DOUBLE_PI / 4.0; if (LandAt(P + FVector2D(FMath::Cos(B), FMath::Sin(B)) * (R * 0.6 + 20.0))) ++Room; }
				if (Room >= 5) { BestP = P; BestRoom = Room; break; }
				if (Room > BestRoom) { BestRoom = Room; BestP = P; }
			}
			if (BestRoom >= 0) Oc = BestP;
		}
	}

	// ================================================================ streets

	void FPlanner::PlanStreets()
	{
		if (Tier == 0) return;   // a camp has trampled paths only (drawn as the plaza)
		// spoke headings: toward neighbours (road-connected first), then the harbour, then fill the gaps
		TArray<double> Want;
		for (int32 I = 0; I < In.Nbrs.Num() && Want.Num() < 6; ++I)
		{
			const FVector D = In.Nbrs[I];
			const FVector2D L(FVector::DotProduct(D, E0), FVector::DotProduct(D, N0));
			if (L.IsNearlyZero()) continue;
			const double A = FMath::Atan2(L.Y, L.X);
			bool bNear = false;
			for (double W : Want) if (FMath::Abs(FMath::FindDeltaAngleRadians(W, A)) < FMath::DegreesToRadians(40.0)) bNear = true;
			if (!bNear && (In.NbrRoad.IsValidIndex(I) && In.NbrRoad[I] ? true : Want.Num() < 3)) Want.Add(A);
		}
		if (bCoastal && In.B[dm::Building::PORT])
		{
			bool bNear = false;
			for (double W : Want) if (FMath::Abs(FMath::FindDeltaAngleRadians(W, SeaAng)) < FMath::DegreesToRadians(30.0)) bNear = true;
			if (!bNear) Want.Add(SeaAng);
		}
		const int32 MinSpokes[5] = { 0, 2, 3, 4, 5 };
		FDmTownRand Rg(Seed ^ 0x51u);
		while (Want.Num() < MinSpokes[Tier])
		{
			if (Want.Num() == 0) { Want.Add(Rg.R(0.f, UE_TWO_PI)); continue; }
			Want.Sort();
			double BestGap = -1, BestA = 0;
			for (int32 I = 0; I < Want.Num(); ++I)
			{
				const double A0 = Want[I], A1 = (I + 1 < Want.Num()) ? Want[I + 1] : Want[0] + UE_DOUBLE_TWO_PI;
				if (A1 - A0 > BestGap) { BestGap = A1 - A0; BestA = A0 + (A1 - A0) * (0.42 + 0.16 * Rg.F()); }
			}
			Want.Add(FMath::UnwindRadians(BestA));
		}
		SpokeAng = Want;

		const float MainW = kMainW[Age], RingW = kRingW[Age], LaneW = kLaneW[Age];
		const double StreetR = R * 1.12;
		const double CountryR = FMath::Min(double(Ex) - 3.0 * GS, StreetR + 1400.0);
		// spokes: inside the town as main streets, beyond as country roads to the edge of the fields
		TArray<int32> Spokes;
		for (int32 I = 0; I < Want.Num(); ++I)
		{
			const FVector2D S0 = Oc + FVector2D(FMath::Cos(Want[I]), FMath::Sin(Want[I])) * (PlazaR + 2.0);
			TArray<FVector2D> P = Walk(S0, Want[I], CountryR, Want[I], Seed + 101u * I, 12.0, false, -1, CountryR);
			P.Insert(Oc, 0);
			FStreet In1, Out1;
			In1.W = MainW; In1.Kind = EStreet::Main;
			Out1.W = kCountryW[Age]; Out1.Kind = EStreet::Country;
			for (const FVector2D& Q : P)
			{
				if (Q.Size() <= StreetR) In1.P.Add(Q);
				else { if (Out1.P.Num() == 0 && In1.P.Num() > 0) Out1.P.Add(In1.P.Last()); Out1.P.Add(Q); }
			}
			const int32 Id = AddStreet(MoveTemp(In1));
			if (Id >= 0) Spokes.Add(Id);
			AddStreet(MoveTemp(Out1));
		}
		// ring streets at fixed radii (a town grows into the rings it will have as a city)
		TArray<int32> Rings;
		if (Tier >= 2)
		{
			auto AddRing = [&](float Rr, float W, EStreet Kind)
			{
				// an organic loop, not a target: elliptical, wobbling, broken into arcs (some arcs never built)
				const int32 Ns = FMath::Max(16, int32(UE_TWO_PI * Rr / 12.f));
				FStreet Cur; Cur.W = W; Cur.Kind = Kind;
				const uint32 Rs = Seed + uint32(Rr);
				const double Phi = Hash01(Rs, 3u) * UE_DOUBLE_PI, Ecc = Kind == EStreet::Highway ? 0.08 : 0.1 + 0.12 * Hash01(Rs, 5u);
				const int32 ArcLen = 7 + int32(Hash(Rs, 9u) % 6u);
				for (int32 K = 0; K <= Ns; ++K)
				{
					const double A = K * UE_DOUBLE_TWO_PI / Ns;
					const double Rk = Rr * (1.0 + 0.2 * Noise(float(FMath::Cos(A)) * 1.1f, float(FMath::Sin(A)) * 1.1f, Rs) + Ecc * FMath::Cos(2.0 * (A - Phi)));
					const FVector2D P = Oc + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rk;
					const bool bArc = Kind == EStreet::Highway || Hash01(Rs, 100u + uint32(K / ArcLen)) > 0.3f;
					if (bArc && LandAt(P) && SlopeAt(P) < 0.3f) Cur.P.Add(P);
					else { if (Cur.P.Num() >= 3) { const int32 Id = AddStreet(MoveTemp(Cur)); if (Id >= 0) Rings.Add(Id); } Cur = FStreet(); Cur.W = W; Cur.Kind = Kind; }
				}
				if (Cur.P.Num() >= 3) { const int32 Id = AddStreet(MoveTemp(Cur)); if (Id >= 0) Rings.Add(Id); }
			};
			for (float Rr : kRings)
			{
				const float Rs = Rr * In.Scale;
				if (Rs < PlazaR + 40.f || Rs > R * 1.02f) continue;
				AddRing(Rs, RingW, EStreet::Ring);
			}
			if (Age >= 5 && Tier >= 4) AddRing(R * 1.08f, 22.f, EStreet::Highway);
		}
		// planned grid districts from the Industry age
		if (Age >= 4 && Tier >= 3 && SpokeAng.Num() >= 2)
		{
			TArray<double> A = SpokeAng;
			A.Sort();
			struct FW { double A0, A1, Score; };
			TArray<FW> Wedges;
			for (int32 I = 0; I < A.Num(); ++I)
			{
				const double A0 = A[I], A1 = (I + 1 < A.Num()) ? A[I + 1] : A[0] + UE_DOUBLE_TWO_PI;
				if (A1 - A0 < FMath::DegreesToRadians(50.0)) continue;
				double Score = 0;
				for (int32 K = 0; K < 24; ++K)
				{
					const double Aa = FMath::Lerp(A0, A1, (K % 6 + 0.5) / 6.0), Rr = R * FMath::Lerp(0.5, 1.0, (K / 6 + 0.5) / 4.0);
					const FVector2D P = Oc + FVector2D(FMath::Cos(Aa), FMath::Sin(Aa)) * Rr;
					if (LandAt(P) && SlopeAt(P) < 0.12f) Score += 1.0;
				}
				Wedges.Add({ A0, A1, Score * (A1 - A0) });
			}
			Wedges.Sort([](const FW& X, const FW& Y) { return X.Score > Y.Score; });
			const int32 NW = FMath::Min(Wedges.Num(), Tier >= 4 ? 2 : 1);
			const float Bx = Age >= 6 ? 130.f : (Age >= 5 ? 120.f : 96.f), By = Age >= 6 ? 100.f : (Age >= 5 ? 90.f : 72.f);
			for (int32 Wi = 0; Wi < NW; ++Wi)
			{
				const FW& Wd = Wedges[Wi];
				GridWedges.Add(FVector2D(Wd.A0, Wd.A1));
				const double Al = 0.5 * (Wd.A0 + Wd.A1);
				const FVector2D Ux(FMath::Cos(Al), FMath::Sin(Al)), Uy(-FMath::Sin(Al), FMath::Cos(Al));
				const FVector2D Org = Oc + Ux * (R * 0.75);
				auto Inside = [&](const FVector2D& P) -> bool
				{
					const FVector2D Q = P - Oc;
					const double Rr = Q.Size();
					if (Rr < R * 0.45 || Rr > R * 1.08) return false;
					const double Aa = FMath::Atan2(Q.Y, Q.X);
					const double Dd = FMath::UnwindRadians(Aa - Wd.A0);
					const double Span = Wd.A1 - Wd.A0;
					const double Dp = Dd < 0 ? Dd + UE_DOUBLE_TWO_PI : Dd;
					return Dp > FMath::DegreesToRadians(5.0) && Dp < Span - FMath::DegreesToRadians(5.0) && LandAt(P) && SlopeAt(P) < 0.16f;
				};
				const int32 Nu = int32(R * 0.8 / Bx) + 2, Nv = int32(R * 1.2 / By) + 2;
				for (int32 Pass = 0; Pass < 2; ++Pass)
					for (int32 L = -(Pass == 0 ? Nu : Nv); L <= (Pass == 0 ? Nu : Nv); ++L)
					{
						FStreet Cur; Cur.W = kGridW[Age]; Cur.Kind = EStreet::Grid; Cur.bGridZone = true;
						const double Span = Pass == 0 ? R * 1.3 : R * 0.9;
						for (double T = -Span; T <= Span; T += 12.0)
						{
							const FVector2D P = Pass == 0 ? Org + Ux * (L * Bx) + Uy * T : Org + Uy * (L * By) + Ux * T;
							if (Inside(P)) Cur.P.Add(P);
							else { if (Cur.P.Num() >= 3) AddStreet(MoveTemp(Cur)); Cur = FStreet(); Cur.W = kGridW[Age]; Cur.Kind = EStreet::Grid; Cur.bGridZone = true; }
						}
						if (Cur.P.Num() >= 3) AddStreet(MoveTemp(Cur));
					}
			}
		}
		// lanes branching off the spokes and rings, stopping where they meet another street
		TArray<int32> Bases = Spokes;
		Bases.Append(Rings);
		const int32 Levels = Tier >= 3 ? 2 : 1;
		for (int32 Lv = 0; Lv < Levels; ++Lv)
		{
			TArray<int32> NewLanes;
			for (int32 Bi : Bases)
			{
				const TArray<FVector2D> P = Streets[Bi].P;   // copy: AddStreet may reallocate
				double Acc = 0, Next = (Lv == 0 ? 30.0 : 40.0) + 20.0 * Hash01(Seed, uint32(Bi));
				for (int32 I = 0; I + 1 < P.Num(); ++I)
				{
					const double L = FVector2D::Distance(P[I], P[I + 1]);
					if (L < 1e-3) continue;
					const FVector2D D = (P[I + 1] - P[I]) / L;
					while (Next <= Acc + L)
					{
						const FVector2D Q = P[I] + D * (Next - Acc);
						const double R01 = (Q - Oc).Size() / FMath::Max(R, 1.f);
						const uint32 Hb = Hash(Seed, uint32(Bi) * 977u + uint32(Next), uint32(Lv));
						const double Sp = (Tier == 1 ? 70.0 : FMath::Lerp(42.0, 95.0, FMath::Min(R01, 1.0))) * (0.8 + 0.4 * Hash01(Hb, 3));
						if (R01 < 1.0)
						{
							const double Prob = Tier == 1 ? 0.55 : (0.9 - 0.35 * R01) * (Lv == 0 ? 1.0 : 0.55);
							for (int32 Side = -1; Side <= 1; Side += 2)
							{
								if (Hash01(Hb, uint32(Side + 5)) > Prob) continue;
								const double H0 = FMath::Atan2(D.Y, D.X) + Side * (UE_DOUBLE_HALF_PI + (Hash01(Hb, uint32(Side + 9)) - 0.5) * 0.8);
								const double Len = (Tier == 1 ? FMath::Lerp(45.0, 95.0, Hash01(Hb, 11)) : FMath::Lerp(170.0, 70.0, FMath::Min(R01, 1.0)) * (0.6 + 0.7 * Hash01(Hb, 13)));
								const int32 SelfId = Streets.Num();
								TArray<FVector2D> W = Walk(Q, H0, Len, std::numeric_limits<double>::quiet_NaN(), Hb, 10.0, true, Bi, R * 1.05);
								// own points are not in the hash yet; hits against the base street are excluded by Skip = Bi
								(void)SelfId;
								if (W.Num() >= 3)
								{
									FStreet S; S.P = MoveTemp(W); S.W = LaneW; S.Kind = EStreet::Lane;
									const int32 Id = AddStreet(MoveTemp(S));
									if (Id >= 0) NewLanes.Add(Id);
								}
							}
						}
						Next += Sp;
					}
					Acc += L;
				}
			}
			Bases = NewLanes;
		}
		Out.Streets = Streets.Num();
	}

	void FPlanner::RasterStreets()
	{
		MarkDisc(Oc, PlazaR + (Tier == 0 ? 10.f : 0.f), OPlaza);
		for (const FStreet& S : Streets)
		{
			if (S.Kind == EStreet::Country) continue;
			for (int32 I = 0; I + 1 < S.P.Num(); ++I) MarkCapsule(S.P[I], S.P[I + 1], S.W * 0.5f + 1.2f, OStreet);
		}
		// country roads only where the raster reaches
		for (const FStreet& S : Streets)
			if (S.Kind == EStreet::Country)
				for (int32 I = 0; I + 1 < S.P.Num(); ++I) MarkCapsule(S.P[I], S.P[I + 1], S.W * 0.5f + 1.2f, OStreet, true);
	}

	// ================================================================ placing

	void FPlanner::Tints(EDmTownMesh M, int32 V, uint32 H, FLinearColor& Wall, FLinearColor& Roof) const
	{
		const FDmKitInfo& I = Kit.GetAt(Age, M, V);
		const uint32 Cu = Hash(uint32(In.Culture + 7), 991u);   // the culture prefers some palette entries
		const uint32 Hw = (H % 3u == 0u) ? Cu : H;
		switch (I.WallPat)
		{
		case EDmTownPat::Plaster: Wall = Style == EStyle::Arid ? PickC(kPlasterArid, Hw) : (Style == EStyle::Tropical ? PickC(kPlasterTrop, Hw) : PickC(kPlaster, Hw)); break;
		case EDmTownPat::Stone: Wall = PickC(kStone, Hw); break;
		case EDmTownPat::Brick: Wall = PickC(kBrick, Hw); break;
		case EDmTownPat::Mud: Wall = PickC(kMud, Hw); break;
		case EDmTownPat::Wood: Wall = PickC(kWood, Hw); break;
		case EDmTownPat::Concrete: Wall = PickC(kConcrete, Hw); break;
		case EDmTownPat::Curtain: Wall = PickC(kCurtain, Hw); break;
		case EDmTownPat::Paint: Wall = PickC(kPaint, Hw); break;
		case EDmTownPat::Hide: Wall = PickC(kHide, Hw); break;
		default: Wall = FLinearColor::White; break;
		}
		const uint32 Hr = Hash(H, 17u);
		const uint32 Hrc = (Hr % 2u == 0u) ? Cu + 1u : Hr;
		switch (I.RoofPat)
		{
		case EDmTownPat::Thatch: Roof = PickC(kThatch, Hrc); break;
		case EDmTownPat::Tile: Roof = (Style == EStyle::Cold) ? PickC(kSlate, Hrc) : PickC(kTile, Hrc); break;
		case EDmTownPat::Slate: Roof = PickC(kSlate, Hrc); break;
		case EDmTownPat::Metal: Roof = PickC(kMetal, Hrc); break;
		case EDmTownPat::Hide: Roof = PickC(kHide, Hrc); break;
		case EDmTownPat::Mud: Roof = PickC(kMud, Hrc); break;
		default: Roof = FLinearColor::White; break;
		}
		// early ages: thatch on what will later be tiled
		if (Age <= 1 && I.RoofPat == EDmTownPat::Tile) Roof = PickC(kThatch, Hrc);
		// a touch of the culture's colour on roofs (and banners / sails / tents, which use the roof tint)
		const FLinearColor Cc = In.Color;
		Roof = FMath::Lerp(Roof, Roof * 0.55f + Cc * 0.6f, 0.22f);
		const float J = 0.92f + 0.16f * Hash01(H, 23u);
		Wall *= J; Roof *= (0.9f + 0.2f * Hash01(H, 29u));
	}

	bool FPlanner::Place(EDmTownMesh M, int32 V, const FVector2D& C, double Yaw, float Scale, float Margin, bool bTest, uint32 Key)
	{
		const FDmKitInfo& I = Kit.GetAt(Age, M, V);
		const float Hw = I.W * 0.5f * Scale, Hd = I.D * 0.5f * Scale;
		if (bTest)
		{
			if (!LandAt(C)) return false;
			if (!RectFree(C, Yaw, Hw + Margin, Hd + Margin)) return false;
		}
		MarkRect(C, Yaw, Hw + Margin * 0.5f, Hd + Margin * 0.5f, OBuilt);
		FPlaced P;
		P.M = M; P.V = V; P.C = C; P.Yaw = Yaw; P.Scale = FVector3f(Scale);
		const uint32 H = Key ? Key : Hash(uint32(FMath::RoundToInt(C.X * 2.0)), uint32(FMath::RoundToInt(C.Y * 2.0)), Seed);
		P.Key = Hash(H, uint32(M), uint32(V));
		Tints(M, V, H, P.Wall, P.Roof);
		P.Lit = I.bLit ? Sat(kLit[Age] * (0.65f + 0.7f * Hash01(H, 31u))) : 0.f;
		P.State = 0.f;
		Placed.Add(P);
		BuiltR = FMath::Max(BuiltR, float((C - Oc).Size()) + FMath::Max(Hw, Hd));
		return true;
	}

	bool FPlanner::PlaceNear(EDmTownMesh M, int32 V, const FVector2D& Want, double Yaw, float MaxDist, float Scale)
	{
		for (int32 K = 0; K < 220; ++K)
		{
			const double A = K * 2.399963, Rr = 4.0 * FMath::Sqrt(double(K));
			if (Rr > MaxDist) break;
			const FVector2D P = Want + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rr;
			if (Place(M, V, P, Yaw, Scale, 1.5f)) return true;
		}
		return false;
	}

	void FPlanner::PickHome(float R01, uint32 H, bool bGrid, EDmTownMesh& M, int32& V) const
	{
		const float U = Hash01(H, 41u), U2 = Hash01(H, 43u);
		const int32 Hv = int32(Hash(H, 47u) & 0xffff);
		int32 A = Age;
		if (A >= 2 && R01 < 0.4f && U2 < 0.25f) A -= 1;            // the old core keeps some older houses
		if (R01 > 1.0f && A >= 2 && U2 < 0.35f) A = FMath::Min(A, 3); // farms at the fringe stay rural
		const bool bArid = Style == EStyle::Arid, bTrop = Style == EStyle::Tropical, bCold = Style == EStyle::Cold;
		auto Set = [&](EDmTownMesh Mm, int32 Vv) { M = Mm; V = Vv; };
		const int32 NCot = UDmTownsFactory::NumVariants(EDmTownMesh::Cottage);
		switch (A)
		{
		case 0:
			if (Tier == 0 || U < 0.15f) Set(EDmTownMesh::Tent, Hv % 3);
			else if (!bArid && !bTrop && U < 0.3f) Set(EDmTownMesh::Longhouse, Hv % 2);
			else Set(EDmTownMesh::Hut, Hv % 3);
			break;
		case 1:
			if (bArid) Set(U < 0.85f ? EDmTownMesh::MudHouse : EDmTownMesh::Hut, U < 0.85f ? Hv % 3 : 3);
			else if (bTrop) Set(U < 0.6f ? EDmTownMesh::Hut : EDmTownMesh::Cottage, U < 0.6f ? Hv % 3 : 0);
			else if (U < 0.35f) Set(EDmTownMesh::Hut, 3);
			else if (U < 0.55f) Set(EDmTownMesh::Longhouse, Hv % 2);
			else Set(EDmTownMesh::Cottage, (Hv % 2) ? 0 : 2);
			break;
		case 2:
			if (bArid) Set(U < 0.8f ? EDmTownMesh::MudHouse : EDmTownMesh::Cottage, U < 0.8f ? Hv % 4 : 3);
			else if (Tier >= 2 && R01 < 0.35f && U < 0.3f) Set(EDmTownMesh::Townhouse, Hv % 6);
			else if (bCold && U < 0.2f) Set(EDmTownMesh::Longhouse, Hv % 2);
			else if (bTrop && U < 0.25f) Set(EDmTownMesh::Hut, Hv % 3);
			else Set(EDmTownMesh::Cottage, Hv % NCot);
			break;
		case 3:
			if (bArid && U < 0.6f && R01 > 0.3f) Set(EDmTownMesh::MudHouse, Hv % 4);
			else if (Tier >= 2 && R01 < 0.45f && U < 0.8f) Set(EDmTownMesh::Townhouse, Hv % 6);
			else if (Tier >= 2 && R01 < 0.75f && U < 0.45f) Set(EDmTownMesh::Townhouse, Hv % 6);
			else Set(EDmTownMesh::Cottage, Hv % NCot);
			break;
		case 4:
			if (bGrid) Set(U < 0.8f ? EDmTownMesh::Rowhouse : EDmTownMesh::Apartment, U < 0.8f ? Hv % 4 : 0);
			else if (Tier >= 2 && R01 < 0.35f) Set(U < 0.6f ? EDmTownMesh::Townhouse : EDmTownMesh::Rowhouse, U < 0.6f ? Hv % 6 : Hv % 4);
			else if (Tier >= 2 && R01 < 0.75f) Set(U < 0.7f ? EDmTownMesh::Rowhouse : EDmTownMesh::Townhouse, U < 0.7f ? Hv % 4 : Hv % 6);
			else Set(U < 0.65f ? EDmTownMesh::Cottage : EDmTownMesh::Rowhouse, U < 0.65f ? Hv % NCot : Hv % 4);
			break;
		case 5:
			if (Tier <= 1) Set(U < 0.6f ? EDmTownMesh::Villa : EDmTownMesh::Cottage, U < 0.6f ? Hv % 4 : Hv % NCot);
			else if (bGrid) Set(U < 0.75f ? EDmTownMesh::Apartment : EDmTownMesh::Rowhouse, Hv % 4);
			else if (R01 < 0.3f && Tier >= 3 && U < (Tier >= 4 ? 0.35f : 0.15f)) Set(EDmTownMesh::Tower, Hv % 3);
			else if (R01 < 0.45f) Set(U < 0.55f ? EDmTownMesh::Apartment : EDmTownMesh::Townhouse, U < 0.55f ? Hv % 4 : Hv % 6);
			else if (R01 < 0.75f) Set(U < 0.55f ? EDmTownMesh::Apartment : EDmTownMesh::Rowhouse, Hv % 4);
			else Set(U < 0.7f ? EDmTownMesh::Villa : EDmTownMesh::Apartment, Hv % 4);
			break;
		default:
			if (Tier <= 1) Set(EDmTownMesh::Villa, Hv % 4);
			else if (R01 < 0.32f && Tier >= 3 && U < (Tier >= 4 ? 0.75f : 0.4f)) Set(EDmTownMesh::Tower, Hv % 5);
			else if (bGrid || R01 < 0.7f) Set(U < 0.72f ? EDmTownMesh::Apartment : (U < 0.85f && Tier >= 3 ? EDmTownMesh::Tower : EDmTownMesh::Rowhouse), Hv % 4);
			else Set(U < 0.62f ? EDmTownMesh::Villa : EDmTownMesh::Apartment, Hv % 4);
			break;
		}
		if (R01 > 1.02f && A <= 4 && U2 > 0.7f) Set(EDmTownMesh::Barn, Hv % 3);
	}

	// ================================================================ walls, docks, civic

	void FPlanner::PlanWalls()
	{
		if (!In.B[dm::Building::WALLS] || Tier < 2 || Age < 1) return;
		// the wall hugs the old town: per angular sector, just outside most of its houses (cities keep their
		// old walls around the historic core); houses on the wall line give way, suburbs stay outside
		auto IsHome = [](EDmTownMesh M) { return M <= EDmTownMesh::Barn; };
		const double CoreR = (Tier == 2 ? 1.0 : (Tier == 3 ? 0.6 : 0.42)) * R;
		constexpr int32 NSec = 24;
		TArray<float> SecR; SecR.Init(0.f, NSec);
		TArray<TArray<float>> SecD; SecD.SetNum(NSec);
		for (const FPlaced& Pl : Placed)
		{
			if (!IsHome(Pl.M)) continue;
			const FVector2D D = Pl.C - Oc;
			const double Dd = D.Size();
			if (Dd > CoreR) continue;
			double A = FMath::Atan2(D.Y, D.X); if (A < 0) A += UE_DOUBLE_TWO_PI;
			SecD[FMath::Clamp(int32(A / UE_DOUBLE_TWO_PI * NSec), 0, NSec - 1)].Add(float(Dd));
		}
		TArray<float> All;
		for (int32 Si = 0; Si < NSec; ++Si) { SecD[Si].Sort(); if (SecD[Si].Num()) { SecR[Si] = SecD[Si][FMath::Min(SecD[Si].Num() - 1, int32(SecD[Si].Num() * 0.85f))]; All.Add(SecR[Si]); } }
		if (All.Num() < 6) return;
		All.Sort();
		const float Med = All[All.Num() / 2];
		for (int32 Si = 0; Si < NSec; ++Si) SecR[Si] = FMath::Clamp(SecR[Si] > 0.f ? SecR[Si] : Med, Med * 0.7f, Med * 1.35f);
		TArray<float> Sm = SecR;
		for (int32 Si = 0; Si < NSec; ++Si) Sm[Si] = 0.25f * SecR[(Si + NSec - 1) % NSec] + 0.5f * SecR[Si] + 0.25f * SecR[(Si + 1) % NSec];
		const float Rw = Med + 14.f;
		auto WallR = [&](double A) -> double
		{
			const double F = A / UE_DOUBLE_TWO_PI * NSec - 0.5;
			const int32 I0 = (int32(FMath::FloorToDouble(F)) % NSec + NSec) % NSec, I1 = (I0 + 1) % NSec;
			const double T = F - FMath::FloorToDouble(F);
			return FMath::Lerp(double(Sm[I0]), double(Sm[I1]), T) + 14.0;
		};
		const int32 WV = Age <= 1 ? 0 : (Age == 2 ? 1 : 2);
		const int32 TV = Age <= 1 ? 0 : (Age == 2 ? 1 : 2);
		const int32 GV = Age <= 1 ? 0 : 1;
		const int32 K = FMath::Clamp(int32(UE_TWO_PI * Rw / 16.f), 24, 220);
		TArray<FVector2D> P; P.SetNum(K);
		TArray<uint8> Ok; Ok.SetNumZeroed(K);
		for (int32 I = 0; I < K; ++I)
		{
			const double A = I * UE_DOUBLE_TWO_PI / K;
			const double Rr = WallR(A) * (1.0 + 0.04 * Noise(float(FMath::Cos(A)) * 2.6f, float(FMath::Sin(A)) * 2.6f, 777u));
			P[I] = Oc + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rr;
			Ok[I] = (LandAt(P[I]) && SlopeAt(P[I]) < 0.35f) ? 1 : 0;
		}
		// houses standing on the wall line give way
		{
			TArray<FPlaced> Keep;
			for (const FPlaced& Pl : Placed)
			{
				bool bHit = false;
				if (IsHome(Pl.M))
				{
					const FDmKitInfo& Ki = Kit.GetAt(Age, Pl.M, Pl.V);
					const double Rad = 0.5 * FMath::Sqrt(Ki.W * Ki.W + Ki.D * Ki.D) + 4.0;
					for (int32 I = 0; I < K && !bHit; ++I)
					{
						const FVector2D A2 = P[I], B2 = P[(I + 1) % K], D2 = B2 - A2;
						const double T = FMath::Clamp(FVector2D::DotProduct(Pl.C - A2, D2) / FMath::Max(D2.SizeSquared(), 1e-6), 0.0, 1.0);
						if (FVector2D::Distance(Pl.C, A2 + D2 * T) < Rad) bHit = true;
					}
				}
				if (!bHit) Keep.Add(Pl);
			}
			Out.Houses -= Placed.Num() - Keep.Num();
			Placed = MoveTemp(Keep);
		}
		// gates where the spokes cross the ring
		TArray<int32> Gates;
		for (const FStreet& S : Streets)
		{
			if (S.Kind != EStreet::Main) continue;
			for (const FVector2D& Q : S.P)
			{
				const FVector2D D = Q - Oc;
				const double A = FMath::Atan2(D.Y, D.X);
				const int32 Iw = (int32(FMath::RoundToInt((A < 0 ? A + UE_DOUBLE_TWO_PI : A) / UE_DOUBLE_TWO_PI * K)) % K + K) % K;
				if (D.Size() >= (P[Iw] - Oc).Size()) { Gates.AddUnique(Iw); break; }
			}
		}
		auto NearGate = [&](int32 I) { for (int32 Gi : Gates) { const int32 Dd = FMath::Abs(Gi - I); if (FMath::Min(Dd, K - Dd) <= 1) return true; } return false; };
		for (int32 I = 0; I < K; ++I)
		{
			const int32 J = (I + 1) % K;
			if (!Ok[I] || !Ok[J] || NearGate(I) || NearGate(J)) continue;
			const FVector2D A = P[I], B = P[J];
			const FVector2D D = B - A;
			const float L = float(D.Size());
			const FVector2D Mid = (A + B) * 0.5;
			FPlaced W; W.M = EDmTownMesh::WallSeg; W.V = WV; W.C = Mid; W.Yaw = FMath::Atan2(D.Y, D.X); W.Scale = FVector3f((L + 1.2f) / 16.f, 1.f, 1.f);
			W.Wall = PickC(Age <= 1 ? kMud : kStone, Seed); W.Roof = W.Wall; W.Lit = 0.f; W.State = 0.f; W.Key = Hash(Seed, 5000u + I);
			Placed.Add(W);
			MarkCapsule(A, B, 3.f, OWall);
			++Out.WallPieces;
		}
		for (int32 I = 0; I < K; ++I)
		{
			if (!Ok[I]) continue;
			const FVector2D D = P[(I + 1) % K] - P[(I + K - 1) % K];
			const double Yaw = FMath::Atan2(D.Y, D.X);
			if (Gates.Contains(I))
			{
				FPlaced Gt; Gt.M = EDmTownMesh::Gatehouse; Gt.V = GV; Gt.C = P[I]; Gt.Yaw = Yaw; Gt.Scale = FVector3f(1.f);
				Gt.Wall = PickC(Age <= 1 ? kMud : kStone, Seed); Gt.Roof = PickC(kSlate, Seed); Gt.Lit = 0.5f; Gt.State = 0.f; Gt.Key = Hash(Seed, 6000u + I);
				Placed.Add(Gt); MarkDisc(P[I], 10.f, OWall); ++Out.WallPieces;
				continue;
			}
			const bool bEnd = !Ok[(I + 1) % K] || !Ok[(I + K - 1) % K];
			if ((I % 6 == 0 || bEnd) && !NearGate(I))
			{
				FPlaced T; T.M = EDmTownMesh::WallTower; T.V = TV; T.C = P[I]; T.Yaw = Yaw; T.Scale = FVector3f(1.f);
				T.Wall = PickC(Age <= 1 ? kMud : kStone, Seed); T.Roof = PickC(kSlate, Seed + 1); T.Lit = 0.35f; T.State = 0.f; T.Key = Hash(Seed, 7000u + I);
				Placed.Add(T); MarkDisc(P[I], 6.f, OWall); ++Out.WallPieces;
			}
		}
	}

	void FPlanner::PlanDocks()
	{
		if (!bCoastal || Tier < 1) return;
		const bool bPort = In.B[dm::Building::PORT] != 0;
		// shore points around the harbour bearing
		struct FShore { FVector2D P; FVector2D Out; double A; };
		TArray<FShore> Shore;
		const int32 NA = 72;
		for (int32 K = 0; K < NA; ++K)
		{
			const double A = SeaAng + (K - NA / 2) * FMath::DegreesToRadians(2.2);
			const FVector2D D(FMath::Cos(A), FMath::Sin(A));
			FVector2D Last = Oc;
			bool bHit = false;
			for (double Rr = 6.0; Rr < ShoreR; Rr += 5.0)
			{
				const FVector2D Q = Oc + D * Rr;
				if (!InGrid(Q)) break;
				if (GridH(Q) < SeaM - 0.3f) { bHit = true; break; }
				Last = Q;
			}
			if (!bHit || (Last - Oc).Size() < PlazaR + 10.0) continue;
			const FVector2D Gr = GradAt(Last);
			FVector2D Outv = Gr.IsNearlyZero() ? D : -Gr.GetSafeNormal();
			if (FVector2D::DotProduct(Outv, D) < 0.3) Outv = D;
			Shore.Add({ Last, Outv, A });
		}
		if (Shore.Num() < 3) return;
		// fishing boats off any coastal village
		FDmTownRand Rb(Seed ^ 0xB0A7u);
		const int32 NBoats = (Age <= 4 && (!bFarPort || bPort)) ? FMath::Min(2 + 2 * Tier, 10) : 0;
		for (int32 K = 0; K < NBoats; ++K)
		{
			const FShore& S = Shore[Rb.I(0, Shore.Num() - 1)];
			const FVector2D P = S.P + S.Out * Rb.R(40.f, 260.f) + FVector2D(S.Out.Y, -S.Out.X) * Rb.R(-80.f, 80.f);
			if (!InGrid(P) || GridH(P) > SeaM - 0.8f) continue;
			FPlaced B; B.M = EDmTownMesh::Boat; B.V = Age >= 1 ? 1 : 0; B.C = P; B.Yaw = Rb.R(0.f, UE_TWO_PI); B.Scale = FVector3f(1.f);
			Tints(B.M, B.V, Rb.Next(), B.Wall, B.Roof); B.Lit = 0.f; B.State = 0.f; B.bWaterLevel = true; B.Key = Hash(Seed, 8000u + K);
			Placed.Add(B); ++Out.Ships;
		}
		if (!bPort) return;
		// piers fanned along the shore from the harbour bearing outward
		const int32 NPier = FMath::Clamp(int32((1 + Tier * 2) * (1.f + 0.15f * Age)), 1, 14);
		const float PierL = kPierL[Age] * (Tier >= 3 ? 1.2f : 1.f);
		const int32 PV = Age <= 3 ? 0 : 1;
		const float Gap = Age >= 4 ? 70.f : 40.f;
		TArray<int32> Order;
		for (int32 K = 0; K < Shore.Num(); ++K) Order.Add(K);
		Order.Sort([&](int32 A, int32 B) { return FMath::Abs(Shore[A].A - SeaAng) < FMath::Abs(Shore[B].A - SeaAng); });
		TArray<FVector2D> Roots;
		FQuay Quay;
		for (int32 Oi : Order)
		{
			if (Roots.Num() >= NPier) break;
			const FShore& S = Shore[Oi];
			bool bFar = true;
			for (const FVector2D& Rt : Roots) if (FVector2D::Distance(Rt, S.P) < Gap) bFar = false;
			if (!bFar) continue;
			// a pier runs out until the water is deep enough (gentle beaches need longer piers)
			float PierLs = PierL;
			while (PierLs < PierL * 3.f && InGrid(S.P + S.Out * PierLs) && GridH(S.P + S.Out * PierLs) > SeaM - 0.5f) PierLs += 6.f;
			const FVector2D End = S.P + S.Out * PierLs;
			if (!InGrid(End) || GridH(End) > SeaM - 0.5f) continue;
			Roots.Add(S.P);
			FPlaced Pr; Pr.M = EDmTownMesh::Pier; Pr.V = PV; Pr.C = S.P - S.Out * 3.0; Pr.Yaw = FMath::Atan2(S.Out.Y, S.Out.X); Pr.Scale = FVector3f((PierLs + 3.f) / 20.f, 1.f, 1.f);
			Pr.Wall = FLinearColor::White; Pr.Roof = FLinearColor::White; Pr.Lit = 0.f; Pr.State = 0.f; Pr.bWaterLevel = true; Pr.Key = Hash(Seed, 9000u + Roots.Num());
			Placed.Add(Pr); ++Out.Piers;
			// a moored ship beside it
			EDmTownMesh SM = EDmTownMesh::Boat; int32 SV = 1;
			switch (Age) { case 0: SM = EDmTownMesh::Boat; SV = 0; break; case 1: SM = EDmTownMesh::Galley; SV = 0; break; case 2: SM = (Roots.Num() % 2) ? EDmTownMesh::Galley : EDmTownMesh::SailShip; SV = 0; break;
				case 3: SM = EDmTownMesh::SailShip; SV = Roots.Num() % 2; break; case 4: SM = (Roots.Num() % 3 == 0) ? EDmTownMesh::SailShip : EDmTownMesh::Steamer; SV = 1; break;
				case 5: SM = EDmTownMesh::CargoShip; SV = 0; break; default: SM = EDmTownMesh::CargoShip; SV = Roots.Num() % 2; break; }
			const FDmKitInfo& Si = Kit.GetAt(Age, SM, SV);
			const float PierHw = PV == 0 ? 2.5f : 7.f;
			const FVector2D Side(-S.Out.Y, S.Out.X);
			const float Sd = (Roots.Num() % 2) ? 1.f : -1.f;
			const FVector2D ShipC = S.P + S.Out * FMath::Max(PierLs - FMath::Max(PierL * 0.45f, Si.W * 0.5f), Si.W * 0.5f + 4.f) + Side * Sd * (PierHw + Si.D * 0.5f + 2.f);
			if (InGrid(ShipC) && GridH(ShipC) < SeaM - 1.0f && Hash01(Seed, uint32(Roots.Num())) < 0.85f)
			{
				FPlaced Sh; Sh.M = SM; Sh.V = SV; Sh.C = ShipC; Sh.Yaw = FMath::Atan2(S.Out.Y, S.Out.X) + (Hash01(Seed, 77u + Roots.Num()) < 0.5f ? PI : 0.0); Sh.Scale = FVector3f(1.f);
				Tints(SM, SV, Hash(Seed, 99u + Roots.Num()), Sh.Wall, Sh.Roof); Sh.Lit = Si.bLit ? 0.5f : 0.f; Sh.State = 0.f; Sh.bWaterLevel = true; Sh.Key = Hash(Seed, 9500u + Roots.Num());
				Placed.Add(Sh); ++Out.Ships;
			}
			// warehouse and crane at the root
			const FVector2D In2 = S.P - S.Out * 22.0;
			if (Age >= 3) Place(EDmTownMesh::Warehouse, Age >= 5 ? 2 : (Age >= 4 ? 1 : 0), In2 + Side * 6.0, FMath::Atan2(Side.Y, Side.X), 1.f, 1.f);
			if (Age >= 4)
			{
				FPlaced Cr; Cr.M = EDmTownMesh::Crane; Cr.V = 0; Cr.C = S.P + S.Out * 8.0 + Side * (-Sd) * (PierHw + 2.0); Cr.Yaw = FMath::Atan2(S.Out.Y, S.Out.X); Cr.Scale = FVector3f(1.f);
				Cr.Wall = FLinearColor::White; Cr.Roof = FLinearColor::White; Cr.Lit = 0.3f; Cr.State = 0.f; Cr.bWaterLevel = true; Cr.ZOff = 1.6f; Cr.Key = Hash(Seed, 9800u + Roots.Num());
				Placed.Add(Cr);
			}
			Quay.P.Add(S.P - S.Out * 4.0);
			MarkCapsule(S.P - S.Out * 10.0, S.P, 7.f, OPlaza);
		}
		if (Quay.P.Num() >= 2)
		{
			Quay.P.Sort([&](const FVector2D& A, const FVector2D& B) { return FMath::Atan2((A - Oc).Y, (A - Oc).X) < FMath::Atan2((B - Oc).Y, (B - Oc).X); });
			for (int32 I = 0; I + 1 < Quay.P.Num(); ++I) if (FVector2D::Distance(Quay.P[I], Quay.P[I + 1]) < Gap * 2.5f) MarkCapsule(Quay.P[I], Quay.P[I + 1], 6.f, OPlaza);
			Quays.Add(Quay);
		}
		// lighthouse on the most seaward shore point
		if (Age >= 2 && Roots.Num() > 0)
		{
			int32 Best = -1; double BestD = -1;
			for (int32 K = 0; K < Shore.Num(); ++K) { const double D = (Shore[K].P - Oc).Size() + 30.0 * FMath::Abs(FMath::FindDeltaAngleRadians(Shore[K].A, SeaAng)); if (D > BestD && (Shore[K].P - Oc).Size() < ShoreR) { BestD = D; Best = K; } }
			if (Best >= 0) PlaceNear(EDmTownMesh::Lighthouse, 0, Shore[Best].P - Shore[Best].Out * 6.0, 0.0, 30.f);
		}
	}

	void FPlanner::PlanCivic()
	{
		const uint8* B = In.B;
		// camps: a fire at the heart, people sleep around it
		if (Tier == 0 || Age == 0) Place(EDmTownMesh::Campfire, 0, Oc, 0.0, 1.f, 0.f, false);
		// around the plaza, facing it, in the gaps between the spokes
		TArray<TPair<EDmTownMesh, int32>> Ring;
		if (B[dm::Building::WONDER] && Age >= 1) Ring.Add({ EDmTownMesh::Wonder, Age <= 1 ? 0 : (Age == 2 ? 1 : (Age == 3 ? 2 : (Age == 4 ? 3 : 4))) });
		if (B[dm::Building::TEMPLE]) Ring.Add({ Age <= 1 ? EDmTownMesh::TempleEarly : (Age <= 3 ? EDmTownMesh::TempleClassic : EDmTownMesh::TempleLate), Age <= 1 ? (Tier >= 3 ? 1 : 0) : (Age <= 3 ? (Age == 3 ? 1 : 0) : (Tier >= 3 ? 1 : 0)) });
		if (B[dm::Building::SHRINE]) Ring.Add({ Age == 0 ? EDmTownMesh::Stones : EDmTownMesh::Shrine, Age <= 2 ? 0 : 1 });
		if (B[dm::Building::MARKET]) Ring.Add({ Age <= 3 ? EDmTownMesh::Market : EDmTownMesh::MarketHall, Tier >= 3 ? 1 : 0 });
		if (B[dm::Building::ACADEMY]) Ring.Add({ EDmTownMesh::Academy, Age >= 5 ? 1 : 0 });
		int32 Slot = 0;
		for (const TPair<EDmTownMesh, int32>& C : Ring)
		{
			const FDmKitInfo& I = Kit.GetAt(Age, C.Key, C.Value);
			bool bDone = false;
			for (int32 Try = 0; Try < 24 && !bDone; ++Try)
			{
				// angles between the spokes first
				double A = (Slot * 2.1 + Try * 0.61);
				if (SpokeAng.Num() >= 2)
				{
					TArray<double> Sa = SpokeAng; Sa.Sort();
					const int32 Gi = (Slot + Try) % Sa.Num();
					const double A0 = Sa[Gi], A1 = (Gi + 1 < Sa.Num()) ? Sa[Gi + 1] : Sa[0] + UE_DOUBLE_TWO_PI;
					A = FMath::Lerp(A0, A1, 0.5 + 0.18 * ((Try / Sa.Num()) % 3 - 1));
				}
				const double Rr = PlazaR + I.D * 0.5 + 3.0 + (Try / 6) * 14.0;
				const FVector2D P = Oc + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rr;
				const double Yaw = A + UE_DOUBLE_HALF_PI;   // front (-Y) toward the plaza
				if (Place(C.Key, C.Value, P, Yaw, 1.f, 1.5f)) { bDone = true; ++Out.Civic; }
			}
			++Slot;
		}
		// granary at the edge of the core, on a street
		if (B[dm::Building::GRANARY] && Streets.Num() > 0)
		{
			const FStreet& S = Streets[Hash(Seed, 3u) % uint32(FMath::Max(1, FMath::Min(Streets.Num(), SpokeAng.Num())))];
			for (int32 I = 0; I < S.P.Num(); ++I)
			{
				if ((S.P[I] - Oc).Size() < R * 0.55) continue;
				const FVector2D D = (I + 1 < S.P.Num() ? S.P[I + 1] - S.P[I] : S.P[I] - S.P[FMath::Max(0, I - 1)]).GetSafeNormal();
				const FVector2D Nn(-D.Y, D.X);
				const FDmKitInfo& Gi = Kit.GetAt(Age, EDmTownMesh::Granary, Age >= 4 ? 1 : 0);
				if (Place(EDmTownMesh::Granary, Age >= 4 ? 1 : 0, S.P[I] + Nn * (S.W * 0.5 + Gi.D * 0.5 + 3.0), FMath::Atan2(D.Y, D.X), 1.f, 1.f)) { ++Out.Civic; break; }
			}
		}
		// far sites: mine on the steepest ground, power plant and launch site on flat land
		auto FarSite = [&](float R0, float R1, bool bSteep, uint32 S) -> FVector2D
		{
			FVector2D Best(1e9, 1e9); float BestS = -1e9f;
			for (int32 K = 0; K < 160; ++K)
			{
				const double A = Hash01(Seed, S, uint32(K)) * UE_DOUBLE_TWO_PI, Rr = FMath::Lerp(double(R0), double(R1), double(Hash01(Seed, S + 1, uint32(K))));
				const FVector2D P = Oc + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rr;
				if (!LandAt(P) || !InGrid(P)) continue;
				if (NearPt(P, 40.f, -1) >= 0) continue;
				const float Sl = SlopeAt(P);
				const float Score = bSteep ? Sl : -Sl - 0.002f * float(Rr);
				if (Score > BestS) { BestS = Score; Best = P; }
			}
			return Best;
		};
		auto PlaceFar = [&](EDmTownMesh M, int32 V, const FVector2D& P, double Yaw, float Scale = 1.f)
		{
			if (P.X > 1e8) return false;
			FPlaced F; F.M = M; F.V = V; F.C = P; F.Yaw = Yaw; F.Scale = FVector3f(Scale);
			Tints(M, V, Hash(Seed, uint32(M)), F.Wall, F.Roof); F.Lit = Kit.GetAt(Age, M, V).bLit ? 0.5f : 0.f; F.State = 0.f; F.Key = Hash(Seed, 10000u + uint32(M) * 7u + uint32(V));
			Placed.Add(F); MarkDisc(P, Kit.GetAt(Age, M, V).W * 0.5f * Scale, OBuilt); ++Out.Civic;
			return true;
		};
		if (B[dm::Building::MINE])
		{
			const FVector2D P = FarSite(R * 0.8f, FMath::Min(R * 1.7f + 200.f, Ex - 60.f), true, 50u);
			if (PlaceFar(EDmTownMesh::MineHead, Age >= 4 ? 1 : 0, P, Hash01(Seed, 51u) * UE_TWO_PI))
				PlaceFar(EDmTownMesh::Spoil, 0, P + FVector2D(30.0, 18.0), 0.0, Age >= 4 ? 1.f : 0.55f);
		}
		if (B[dm::Building::PLANT] && Age >= 5)
		{
			const FVector2D P = FarSite(R * 1.15f, FMath::Min(R * 1.8f + 300.f, Ex - 120.f), false, 60u);
			if (PlaceFar(EDmTownMesh::Reactor, 0, P, 0.3))
			{
				PlaceFar(EDmTownMesh::CoolingTower, 0, P + FVector2D(80.0, -10.0), 0.0);
				PlaceFar(EDmTownMesh::CoolingTower, 0, P + FVector2D(80.0, 62.0), 0.0);
			}
		}
		if (B[dm::Building::LAUNCH])
		{
			const FVector2D P = FarSite(R * 1.3f, FMath::Min(R * 2.2f + 300.f, Ex - 120.f), false, 70u);
			if (PlaceFar(EDmTownMesh::Gantry, 0, P, 0.0))
			{
				FPlaced Rk; Rk.M = EDmTownMesh::Rocket; Rk.V = 0; Rk.C = P; Rk.Yaw = 0.0;
				const float H = 0.3f + 0.7f * Sat(float(In.Ark));
				Rk.Scale = FVector3f(1.f, 1.f, H); Rk.Wall = FLinearColor::White; Rk.Roof = FLinearColor::White; Rk.Lit = 0.f; Rk.State = 0.f; Rk.ZOff = 1.5f; Rk.Key = Hash(Seed, 12000u);
				Placed.Add(Rk);
			}
		}
		// industry: a sector of factories and warehouses, toward the sea when there is one
		if (B[dm::Building::FACTORY] && Age >= 4)
		{
			bIndustry = true;
			IndAng = bCoastal ? SeaAng + 0.6 : Hash01(Seed, 81u) * UE_DOUBLE_TWO_PI;
		}
	}

	// ================================================================ houses

	void FPlanner::PlanHouses()
	{
		struct FSlot { FVector2D C; double Yaw; EDmTownMesh M; int32 V; float Pri; uint32 Key; float Hw, Hd; };
		TArray<FSlot> Slots;
		FDmTownRand Rr(Seed ^ 0x4045u);
		// camps and stone-age villages: huts around the fire, no street frontage
		if (Tier == 0 || Age == 0)
		{
			const int32 N = Target;
			for (int32 K = 0; K < N * 6 && Slots.Num() < N * 3; ++K)
			{
				const double A = K * 2.399963 + Rr.R(-0.2f, 0.2f), Rad = PlazaR + 12.0 + 5.5 * FMath::Sqrt(double(K)) * (Tier == 0 ? 1.0 : 1.6);
				const FVector2D P = Oc + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rad;
				EDmTownMesh M; int32 V; const uint32 H = Hash(Seed, uint32(K), 3u);
				PickHome(float(Rad / FMath::Max(R, 1.f)), H, false, M, V);
				const FDmKitInfo& I = Kit.GetAt(Age, M, V);
				Slots.Add({ P, A + UE_DOUBLE_HALF_PI, M, V, float(Rad), H, I.W * 0.5f, I.D * 0.5f });
			}
		}
		// along both sides of every street, facing it
		for (int32 Si = 0; Si < Streets.Num(); ++Si)
		{
			const FStreet& S = Streets[Si];
			if (S.Kind == EStreet::Highway) continue;
			const bool bCountry = S.Kind == EStreet::Country;
			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				double Acc = 0, Next = 3.0 + 6.0 * Hash01(Seed, uint32(Si), uint32(Side + 2));
				for (int32 I = 0; I + 1 < S.P.Num(); ++I)
				{
					const double L = FVector2D::Distance(S.P[I], S.P[I + 1]);
					if (L < 1e-3) continue;
					const FVector2D D = (S.P[I + 1] - S.P[I]) / L;
					const FVector2D Nl(-D.Y, D.X);
					while (Next <= Acc + L)
					{
						const FVector2D Q = S.P[I] + D * (Next - Acc);
						const float R01 = float((Q - Oc).Size() / FMath::Max(R, 1.f));
						const uint32 H = Hash(uint32(FMath::RoundToInt(Q.X)), uint32(FMath::RoundToInt(Q.Y)), Seed + uint32(Side + 3));
						bool bGrid = S.bGridZone;
						EDmTownMesh M; int32 V;
						PickHome(R01, H, bGrid, M, V);
						// the industrial sector
						if (bIndustry)
						{
							const double A = FMath::Atan2((Q - Oc).Y, (Q - Oc).X);
							if (FMath::Abs(FMath::FindDeltaAngleRadians(A, IndAng)) < 0.55 && R01 > 0.5f && R01 < 1.1f)
							{
								const float U = Hash01(H, 91u);
								M = U < 0.55f ? EDmTownMesh::Factory : (U < 0.85f ? EDmTownMesh::Warehouse : EDmTownMesh::Stack); V = int32(H % 3u);
							}
						}
						const FDmKitInfo& Ki = Kit.GetAt(Age, M, V);
						const bool bTerrace = (M == EDmTownMesh::Townhouse || M == EDmTownMesh::Rowhouse) && R01 < 0.8f;
						const double Gap = bTerrace ? 0.4 : (Tier == 1 ? 6.0 + 16.0 * Hash01(H, 5u) : (R01 < 0.4f ? 2.0 + 3.0 * Hash01(H, 5u) : 4.0 + 10.0 * Hash01(H, 5u) * FMath::Min(R01, 1.5f)));
						const double Setback = bTerrace ? 0.8 : 2.0 + (R01 < 0.5f ? 1.0 : 4.0) * Hash01(H, 7u);
						const FVector2D C = Q + Nl * (Side * (S.W * 0.5 + 1.2 + Setback + Ki.D * 0.5));
						const double Yaw = FMath::Atan2(D.Y, D.X) + (Side > 0 ? 0.0 : PI);
						float Pri = R01 + 0.18f * (Hash01(H, 9u) - 0.5f) + (S.Kind == EStreet::Lane ? 0.05f : 0.f) + (bCountry ? 0.25f : 0.f);
						if (!bCountry || R01 < 1.6f) Slots.Add({ C, Yaw, M, V, Pri, H, Ki.W * 0.5f, Ki.D * 0.5f });
						Next += Ki.W + Gap;
					}
					Acc += L;
				}
			}
		}
		Slots.Sort([](const FSlot& A, const FSlot& B) { return A.Pri < B.Pri; });
		int32 Homes = 0;
		for (const FSlot& S : Slots)
		{
			if (Homes >= Target) break;
			if (S.Pri > 1.25f && Homes > Target / 2) break;
			if (!LandAt(S.C) || SlopeAt(S.C) > 0.34f) continue;
			if (Place(S.M, S.V, S.C, S.Yaw, 1.f, 0.5f, true, S.Key))
			{
				if (S.M != EDmTownMesh::Factory && S.M != EDmTownMesh::Warehouse && S.M != EDmTownMesh::Stack) ++Homes;
			}
		}
		// back-lot fill behind the frontages of towns and cities
		if (Tier >= 2 && Homes < Target)
		{
			for (int32 K = 0; K < Target * 6 && Homes < Target; ++K)
			{
				if (Streets.Num() == 0) break;
				const FStreet& S = Streets[Rr.I(0, Streets.Num() - 1)];
				if (S.Kind == EStreet::Country || S.P.Num() < 2) continue;
				const int32 I = Rr.I(0, S.P.Num() - 2);
				const FVector2D D = (S.P[I + 1] - S.P[I]).GetSafeNormal();
				const FVector2D Q = S.P[I] + FVector2D(-D.Y, D.X) * (Rr.P(0.5f) ? 1.0 : -1.0) * Rr.R(16.f, 46.f);
				const float R01 = float((Q - Oc).Size() / FMath::Max(R, 1.f));
				if (R01 > 0.95f) continue;
				EDmTownMesh M; int32 V; const uint32 H = Rr.Next();
				PickHome(R01, H, false, M, V);
				if (Place(M, V, Q, FMath::Atan2(D.Y, D.X) + (Rr.P(0.5f) ? 0.0 : PI), 1.f, 0.8f)) ++Homes;
			}
		}
		Out.Houses = Homes;
	}

	// ================================================================ fields

	void FPlanner::PlanFields()
	{
		if (Tier < 1) return;
		// farmed area from the sim (owned cells' farm), drawn at half of it, in a ring outside the town
		double Farm = 0;
		int32 Owned = 0;
		if (Snap.Geo)
		{
			// cells around the centre (a small ring walk over neighbours, thread-safe)
			TArray<int32> Q; TSet<int32> Seen;
			Q.Add(In.Cell); Seen.Add(In.Cell);
			for (int32 H = 0; H < Q.Num() && Q.Num() < 400; ++H)
			{
				const int32 C = Q[H];
				if (C < 0 || C >= Snap.N) continue;
				if (Snap.Owner[C] == In.Id) { Farm += Snap.Farm[C]; ++Owned; }
				const float* Pc = &Snap.Geo->pos[size_t(C) * 3];
				const FVector Dc = DmCoords::SimToUE(Pc[0], Pc[1], Pc[2]);
				if (FMath::Acos(FMath::Clamp(FVector::DotProduct(Dc, U0), -1.0, 1.0)) > 0.12) continue;
				for (int32 K = 0; K < Snap.Geo->nbrN[size_t(C)]; ++K)
				{
					const int32 M = Snap.Geo->nbr[size_t(6 * C + K)];
					if (!Seen.Contains(M)) { Seen.Add(M); Q.Add(M); }
				}
			}
		}
		const double CellArea = 3.1e6;
		const double AgeFarm[7] = { 0.06, 0.3, 0.6, 0.8, 1.0, 1.0, 1.0 };   // farming spreads with the ages
		const double Area = FMath::Clamp(Farm * CellArea * 0.4, 0.0, 9.0e6) * AgeFarm[Age];
		if (Area < 5000.0) return;
		const double R0 = FMath::Max(double(BuiltR), double(R) * 0.7) + 12.0;
		const double R1 = FMath::Min(FMath::Sqrt(R0 * R0 + Area / PI), FMath::Min(R0 + 1500.0, double(Ex) - 3.0 * GS));
		// (wave 4 towns) organic parcels that follow the terrain (docs/lab/ground.md 5.1-5.2): holdings are warped Voronoi
		// cells; each takes its field direction from the contours on slopes (strips run along them) or a smooth regional
		// direction shared with its neighbours, and is cut into furlongs of parallel strips whose ends line up, or into
		// irregular polygons. Each field decides on its own whether it is tilled (the sim's farm field, frayed by noise),
		// so farmland thins out field by field into meadow and wild land. Balks between fields and tracks / hedges between
		// holdings show the ground; every vertex is displaced by one smooth warp, so boundaries wander but stay shared.
		static const float kHold[7] = { 170.f, 230.f, 290.f, 350.f, 460.f, 600.f, 700.f };
		static const float kS0[7] = { 45.f, 60.f, 80.f, 100.f, 150.f, 220.f, 270.f };
		const float A01 = float(Age) / 6.f;
		const double Hs = kHold[Age];
		WarpL = 6.f * kS0[Age] + 100.f;
		WarpA = 0.12f * kS0[Age] + 4.f;
		const bool bPivot = Age >= 5 && Style == EStyle::Arid;
		auto SiteAt = [&](int32 I, int32 J) -> FVector2D
		{
			const uint32 H = Hash(uint32(I + 5000), uint32(J + 5000), Seed * 13u + 7u);
			return Oc + FVector2D((I + 0.15 + 0.7 * Hash01(H, 1u)) * Hs, (J + 0.15 + 0.7 * Hash01(H, 2u)) * Hs);
		};
		auto Ragged = [&](const FVector2D& P) { return R1 * (0.78 + 0.4 * (0.5 + 0.5 * Noise(float(P.X / 1100.0), float(P.Y / 1100.0), 411u))); };
		auto ToLocal = [](const FVector2D& P, const FVector2D& O, const FVector2D& Ux, const FVector2D& Uy) { const FVector2D D = P - O; return FVector2D(FVector2D::DotProduct(D, Ux), FVector2D::DotProduct(D, Uy)); };
		auto ToWorld = [](const FVector2D& L, const FVector2D& O, const FVector2D& Ux, const FVector2D& Uy) { return O + Ux * L.X + Uy * L.Y; };
		// crop classes (linear albedo, ground.md 5.3, high summer) by climate
		auto CropColour = [&](uint32 H, const FVector2D& P) -> FLinearColor
		{
			const float Rg = 0.5f + 0.5f * Noise(float(P.X / 2600.0), float(P.Y / 2600.0), 512u);   // regional mix
			const float Rc = FMath::Frac(Hash01(H, 40u) + 0.35f * Rg);
			const float Dry = Style == EStyle::Arid ? 1.f : 0.f, Cold = Style == EStyle::Cold ? 1.f : 0.f, Trop = Style == EStyle::Tropical ? 1.f : 0.f;
			// soils: brown loam, red earth, grey clay (paler where dry)
			const float Ns = 0.5f + 0.5f * Noise(float(P.X / 1800.0), float(P.Y / 1800.0), 513u);
			FLinearColor Soil = Ns < 0.4f ? FLinearColor(0.135f, 0.100f, 0.070f) : (Ns < 0.7f ? FLinearColor(0.150f, 0.088f, 0.058f) : FLinearColor(0.115f, 0.105f, 0.088f));
			if (Dry > 0.f) Soil *= 1.4f;
			const float PCereal = 0.36f + 0.1f * Dry - 0.15f * Trop - 0.12f * Cold, PPast = PCereal + 0.22f + 0.15f * Cold - 0.05f * Dry;
			const float PHay = PPast + 0.10f + 0.06f * Cold, PRow = PHay + 0.12f + 0.2f * Trop - 0.05f * Cold, PFallow = PRow + 0.12f;
			FLinearColor C;
			if (Rc < PCereal)
			{
				const float Ripe = Hash01(H, 41u);
				C = Ripe < 0.62f ? FMath::Lerp(FLinearColor(0.235f, 0.200f, 0.098f), FLinearColor(0.270f, 0.230f, 0.125f), Hash01(H, 42u))
					: (Ripe < 0.82f ? FLinearColor(0.235f, 0.210f, 0.145f) : FLinearColor(0.075f, 0.112f, 0.036f));
				if (Cold > 0.f) C = FMath::Lerp(C, FLinearColor(0.08f, 0.11f, 0.04f), 0.35f);
			}
			else if (Rc < PPast) C = Dry > 0.f ? FLinearColor(0.19f, 0.17f, 0.085f) : FLinearColor(0.085f, 0.125f, 0.048f);
			else if (Rc < PHay) C = FLinearColor(0.15f, 0.16f, 0.07f);
			else if (Rc < PRow) C = FLinearColor(0.040f, 0.080f, 0.026f);
			else if (Rc < PFallow) C = FMath::Lerp(Soil, FLinearColor(0.11f, 0.105f, 0.06f), Hash01(H, 43u) * 0.5f);
			else C = (Cold > 0.f || Hash01(H, 44u) < 0.5f) ? FLinearColor(0.085f, 0.125f, 0.048f) : FLinearColor(0.48f, 0.40f, 0.045f);   // oilseed is rare
			if (Trop > 0.f) C = C * FLinearColor(1.06f, 1.f, 0.9f);
			C *= 0.88f + 0.24f * Hash01(H, 45u);
			// the overlay's vertex colour is sRGB (the material squares it back)
			return FLinearColor(FMath::Pow(FMath::Max(C.R, 0.f), 1.f / 2.2f), FMath::Pow(FMath::Max(C.G, 0.f), 1.f / 2.2f), FMath::Pow(FMath::Max(C.B, 0.f), 1.f / 2.2f), 1.f);
		};
		const double Balk = Age >= 4 ? 2.4 : 1.6;     // grass between fields
		const double Track = 3.2;                       // track / hedge line between holdings
		const bool bHedges = Style == EStyle::Temperate && Age >= 1 && Age <= 4;
		TSet<FIntPoint> HedgeSeen;
		const int32 Nh = int32((R1 + Hs) / Hs) + 2;
		for (int32 J = -Nh; J <= Nh; ++J)
			for (int32 I = -Nh; I <= Nh; ++I)
			{
				if (Cancelled()) return;
				const FVector2D S = SiteAt(I, J);
				const double Ds = (S - Oc).Size();
				if (Ds < R0 - Hs * 0.6 || Ds > Ragged(S) + Hs * 0.5) continue;
				// the holding: Voronoi cell of its site among the jittered lattice (CCW square clipped by bisectors)
				TArray<FVector2D> Cell = { S + FVector2D(-1.6 * Hs, -1.6 * Hs), S + FVector2D(1.6 * Hs, -1.6 * Hs), S + FVector2D(1.6 * Hs, 1.6 * Hs), S + FVector2D(-1.6 * Hs, 1.6 * Hs) };
				for (int32 Dj = -2; Dj <= 2; ++Dj)
					for (int32 Di = -2; Di <= 2; ++Di)
					{
						if (Di == 0 && Dj == 0) continue;
						const FVector2D Sj = SiteAt(I + Di, J + Dj);
						const FVector2D Nn = Sj - S;
						ClipHalf(Cell, Nn, FVector2D::DotProduct(Nn, (S + Sj) * 0.5));
					}
				if (Cell.Num() < 3) continue;
				const uint32 Hh = Hash(uint32(I + 5000), uint32(J + 5000), Seed * 29u + 3u);
				// orientation: along the contours on slopes, else the regional grain (+-17 degrees per holding)
				FVector2D Gr = FVector2D::ZeroVector;
				int32 Ng = 0;
				for (int32 K = 0; K < 5; ++K)
				{
					const FVector2D Q = S + (K == 0 ? FVector2D::ZeroVector : FVector2D(FMath::Cos(K * UE_HALF_PI), FMath::Sin(K * UE_HALF_PI)) * Hs * 0.3);
					if (InGrid(Q)) { Gr += GradAt(Q); ++Ng; }
				}
				Gr /= double(FMath::Max(Ng, 1));
				const double Gm = Gr.Size();
				const bool bContour = Gm > 0.03;
				const double Al = bContour ? FMath::Atan2(Gr.Y, Gr.X) + UE_DOUBLE_HALF_PI
					: 2.2 * UE_DOUBLE_PI * Noise(float(S.X / 3500.0), float(S.Y / 3500.0), 77u) + (Hash01(Hh, 3u) - 0.5) * 0.6;
				const FVector2D Ux(FMath::Cos(Al), FMath::Sin(Al)), Uy(-FMath::Sin(Al), FMath::Cos(Al));
				const bool bStrips = Hash01(Hh, 4u) < (bContour ? 0.75f : FMath::Lerp(0.45f, 0.75f, A01));
				const float Sz = kS0[Age] * FMath::Lerp(0.8f, 1.3f, Hash01(Hh, 5u)) * FMath::Lerp(0.8f, 1.25f, 0.5f + 0.5f * Noise(float(S.X / 5000.0), float(S.Y / 5000.0), 78u));
				const double W = bStrips ? Sz * FMath::Lerp(1.5f, 2.8f, Hash01(Hh, 6u)) : Sz * FMath::Lerp(1.f, 1.6f, Hash01(Hh, 6u));
				const double H = bStrips ? Sz * FMath::Lerp(0.5f, 0.75f, Hash01(Hh, 7u)) : Sz * FMath::Lerp(0.75f, 1.f, Hash01(Hh, 7u));
				// tracks and hedges between holdings: the holding shrinks by half a track
				TArray<FVector2D> Hold = Cell;
				Inset(Hold, Track * 0.5);
				if (Hold.Num() < 3) continue;
				if (bHedges)
					for (int32 K = 0; K < Cell.Num(); ++K)
					{
						const FVector2D A = Cell[K], B = Cell[(K + 1) % Cell.Num()];
						const FVector2D Mid = (A + B) * 0.5;
						const FIntPoint Mk(FMath::RoundToInt(Mid.X / 4.0), FMath::RoundToInt(Mid.Y / 4.0));
						if (HedgeSeen.Contains(Mk)) continue;
						HedgeSeen.Add(Mk);
						const float Bocage = FMath::Clamp(0.35f + 0.5f * Noise(float(Mid.X / 2000.0), float(Mid.Y / 2000.0), 514u) - 0.3f * A01, 0.f, 1.f);
						const double Dm = (Mid - Oc).Size();
						if (Hash01(uint32(Mk.X + 70000), uint32(Mk.Y + 70000), Seed) < Bocage && Dm < R1 && Dm > R0) Hedges.Add({ A, B });
					}
				// the holding in its own frame
				TArray<FVector2D> L;
				for (const FVector2D& P : Hold) L.Add(ToLocal(P, S, Ux, Uy));
				double Ua0 = 1e30, Ua1 = -1e30, Va0 = 1e30, Va1 = -1e30;
				for (const FVector2D& P : L) { Ua0 = FMath::Min(Ua0, P.X); Ua1 = FMath::Max(Ua1, P.X); Va0 = FMath::Min(Va0, P.Y); Va1 = FMath::Max(Va1, P.Y); }
				TArray<TArray<FVector2D>> Pieces;
				if (bStrips)
				{
					// furlongs: block ends shared by every row, 30% of strips split once more
					const int32 K0 = FMath::FloorToInt(Ua0 / W) - 1, K1 = FMath::CeilToInt(Ua1 / W) + 1;
					auto EndAt = [&](int32 K) { return W * (K + 0.2 + 0.6 * Hash01(Hh, uint32(K + 1000), 90u)); };
					const int32 R0i = FMath::FloorToInt(Va0 / H), R1i = FMath::CeilToInt(Va1 / H);
					for (int32 K = K0; K < K1; ++K)
					{
						const double Ua = EndAt(K), Ub = EndAt(K + 1);
						if (Ub < Ua0 || Ua > Ua1) continue;
						for (int32 Rw = R0i; Rw < R1i; ++Rw)
						{
							const double Va = Rw * H, Vb = (Rw + 1) * H;
							const uint32 Hs2 = Hash(Hh, uint32(K + 2000), uint32(Rw + 3000));
							const bool bSplit = Hash01(Hs2, 1u) < 0.3f;
							const double Um = FMath::Lerp(Ua, Ub, 0.35 + 0.3 * Hash01(Hs2, 2u));
							for (int32 Pc = 0; Pc < (bSplit ? 2 : 1); ++Pc)
							{
								const double Pa = bSplit ? (Pc == 0 ? Ua : Um) : Ua, Pb = bSplit ? (Pc == 0 ? Um : Ub) : Ub;
								TArray<FVector2D> Pp = L;
								ClipHalf(Pp, FVector2D(-1, 0), -Pa); ClipHalf(Pp, FVector2D(1, 0), Pb);
								ClipHalf(Pp, FVector2D(0, -1), -Va); ClipHalf(Pp, FVector2D(0, 1), Vb);
								if (Pp.Num() >= 3) Pieces.Add(MoveTemp(Pp));
							}
						}
					}
				}
				else
				{
					// irregular polygons: an anisotropic Voronoi in (u / W, v / H), jitter 0.02..0.98
					const int32 A0i = FMath::FloorToInt(Ua0 / W) - 1, A1i = FMath::CeilToInt(Ua1 / W) + 1, B0i = FMath::FloorToInt(Va0 / H) - 1, B1i = FMath::CeilToInt(Va1 / H) + 1;
					auto Q = [&](int32 A, int32 B) { const uint32 Hq = Hash(Hh, uint32(A + 4000), uint32(B + 5000)); return FVector2D((A + 0.02 + 0.96 * Hash01(Hq, 1u)) * W, (B + 0.02 + 0.96 * Hash01(Hq, 2u)) * H); };
					auto Sc = [&](const FVector2D& P) { return FVector2D(P.X / W, P.Y / H); };
					for (int32 Bq = B0i; Bq <= B1i; ++Bq)
						for (int32 Aq = A0i; Aq <= A1i; ++Aq)
						{
							const FVector2D Qs = Sc(Q(Aq, Bq));
							TArray<FVector2D> Pp;
							for (const FVector2D& P : L) Pp.Add(Sc(P));
							for (int32 Db = -1; Db <= 1 && Pp.Num() >= 3; ++Db)
								for (int32 Da = -1; Da <= 1 && Pp.Num() >= 3; ++Da)
								{
									if (Da == 0 && Db == 0) continue;
									const FVector2D Qn = Sc(Q(Aq + Da, Bq + Db));
									const FVector2D Nn = Qn - Qs;
									ClipHalf(Pp, Nn, FVector2D::DotProduct(Nn, (Qs + Qn) * 0.5));
								}
							if (Pp.Num() < 3) continue;
							for (FVector2D& P : Pp) P = FVector2D(P.X * W, P.Y * H);
							Pieces.Add(MoveTemp(Pp));
						}
				}
				// every field decides on its own (at its centre)
				for (int32 Pi = 0; Pi < Pieces.Num(); ++Pi)
				{
					TArray<FVector2D>& Pp = Pieces[Pi];
					if (PolyArea(Pp) < W * H * 0.12) continue;
					Inset(Pp, Balk * 0.5);
					if (Pp.Num() < 3 || PolyArea(Pp) < 300.0) continue;
					FParcel F;
					for (const FVector2D& P : Pp) F.Poly.Add(ToWorld(P, S, Ux, Uy));
					F.C = ToWorld(PolyCentroid(Pp), S, Ux, Uy);
					F.H = Hash(Hh, uint32(Pi + 1), 0x5EEDu);
					const double Dc = (F.C - Oc).Size();
					if (Dc < R0 || Dc > Ragged(F.C)) continue;
					if (!LandAt(F.C) || SlopeAt(F.C) > 0.18f) continue;
					// the sim's farm field, frayed field by field
					const int32 Cl = Snap.CellAt(DirAt(F.C), In.Cell);
					const float FarmC = (Cl >= 0 && Cl < Snap.N) ? Snap.Farm[Cl] : 0.f;
					const float Fray = 0.22f * Noise(float(F.C.X / 700.0), float(F.C.Y / 700.0), 79u) + 0.12f * Noise(float(F.C.X / 260.0), float(F.C.Y / 260.0), 80u);
					const float Edge = 1.f - Smooth(float(Ragged(F.C) * 0.8), float(Ragged(F.C)), float(Dc));
					const float Pc = FMath::Clamp(0.35f + 1.6f * FarmC + Fray, 0.05f, 0.95f) * FMath::Lerp(0.45f, 1.f, Edge);
					if (Hash01(F.H, 9u) > Pc) continue;
					bool bOk = true;
					for (const FVector2D& P : F.Poly)
					{
						if (!LandAt(P)) { bOk = false; break; }
						if (FMath::Abs(P.X) < OE && FMath::Abs(P.Y) < OE) { const uint8 O = OccAt(P); if (O != OFree && O != OBlocked) { bOk = false; break; } }
						if (NearPt(P, 7.f, -1) >= 0) { bOk = false; break; }
					}
					if (!bOk || NearPt(F.C, 12.f, -1) >= 0) continue;
					F.Ang = float(Al + (((F.H & 16u) && !bStrips) ? UE_DOUBLE_HALF_PI : 0.0));
					F.Col = CropColour(F.H, F.C);
					if (bPivot && Hash01(F.H, 8u) < 0.4f)
					{
						// centre-pivot circles in dry lands from the Atomic age
						double Rm = 1e30;
						for (int32 K = 0; K < F.Poly.Num(); ++K) Rm = FMath::Min(Rm, FMath::PointDistToSegment(FVector(F.C, 0.0), FVector(F.Poly[K], 0.0), FVector(F.Poly[(K + 1) % F.Poly.Num()], 0.0)));
						if (Rm > 25.0) { F.bCircle = true; F.Rad = float(Rm - 2.0); F.Col = FLinearColor(0.42f, 0.5f, 0.26f); }
					}
					Parcels.Add(MoveTemp(F));
				}
			}
		// the nearest fields only (overlay triangles grow with the field count)
		const int32 MaxFields = Age == 0 ? 40 : (Tier == 1 ? 320 : (Tier == 2 ? 480 : 700));
		if (Parcels.Num() > MaxFields)
		{
			Parcels.Sort([this](const FParcel& A, const FParcel& B) { return FVector2D::DistSquared(A.C, Oc) < FVector2D::DistSquared(B.C, Oc); });
			Parcels.SetNum(MaxFields);
		}
		for (const FParcel& F : Parcels)
		{
			// quads for the farmsteads and windmills below (a corner and the row direction)
			FFieldQuad Q;
			const int32 N = F.Poly.Num();
			for (int32 K = 0; K < 4; ++K) Q.C[K] = F.Poly[(K * N) / 4];
			Q.RowAng = F.Ang;
			Q.Col = F.Col;
			FieldQ.Add(Q);
		}
		Out.Fields = FieldQ.Num();
		// farmsteads among the fields, windmills in the Sail age
		FDmTownRand Rf(Seed ^ 0xFA4Du);
		const int32 NFarm = FMath::Min(FieldQ.Num() / 9, 30);
		for (int32 K = 0; K < NFarm; ++K)
		{
			const FFieldQuad& F = FieldQ[Rf.I(0, FieldQ.Num() - 1)];
			const FVector2D P = F.C[Rf.I(0, 3)];
			if (!InGrid(P) || !LandAt(P) || NearPt(P, 12.f, -1) >= 0) continue;
			FPlaced B; B.M = (Age >= 5 && Rf.P(0.5f)) ? EDmTownMesh::Villa : EDmTownMesh::Barn; B.V = Rf.I(0, 2); B.C = P; B.Yaw = F.RowAng; B.Scale = FVector3f(1.f);
			Tints(B.M, B.V, Rf.Next(), B.Wall, B.Roof); B.Lit = B.M == EDmTownMesh::Villa ? 0.5f : 0.f; B.State = 0.f; B.Key = Hash(Seed, 20000u + K);
			Placed.Add(B);
			if (Age >= 1 && Age <= 4)
			{
				FPlaced Hh; Hh.M = Age >= 2 ? EDmTownMesh::Cottage : EDmTownMesh::Longhouse; Hh.V = Rf.I(0, 1); Hh.C = P + FVector2D(FMath::Cos(F.RowAng), FMath::Sin(F.RowAng)) * 18.0; Hh.Yaw = F.RowAng + UE_DOUBLE_HALF_PI; Hh.Scale = FVector3f(1.f);
				Tints(Hh.M, Hh.V, Rf.Next(), Hh.Wall, Hh.Roof); Hh.Lit = kLit[Age]; Hh.State = 0.f; Hh.Key = Hash(Seed, 21000u + K);
				if (LandAt(Hh.C) && NearPt(Hh.C, 10.f, -1) < 0) Placed.Add(Hh);
			}
		}
		if (Age == 3 && Style != EStyle::Tropical && FieldQ.Num() > 0)
			for (int32 K = 0; K < 1 + Tier; ++K)
			{
				const FFieldQuad& F = FieldQ[Rf.I(0, FieldQ.Num() - 1)];
				const FVector2D P = (F.C[0] + F.C[2]) * 0.5;
				FPlaced W; W.M = EDmTownMesh::Windmill; W.V = 0; W.C = P + (P - Oc).GetSafeNormal() * 4.0; W.Yaw = Rf.R(0.f, UE_TWO_PI); W.Scale = FVector3f(1.f);
				Tints(W.M, 0, Rf.Next(), W.Wall, W.Roof); W.Lit = 0.3f; W.State = 0.f; W.Key = Hash(Seed, 22000u + K);
				Placed.Add(W);
			}
	}

	// ================================================================ overlay mesh and instances

	int32 FPlanner::OverlayVert(const FVector2D& P, float Lift, const FVector2f& Uv0, const FColor& C, const FVector2f& Uv1)
	{
		const FVector D = DirAt(P);
		const double Rg = G.Radius(D);
		const FVector Pos = D * (Rg + Lift * 100.0) - Out.Centre;
		const FVector2D Gr = GradAt(P);
		const FVector Nrm = (D - E0 * Gr.X - N0 * Gr.Y).GetSafeNormal();
		return Out.Ground.Add(FVector3f(Pos * 0.01), FVector3f(Nrm), Uv0, C, Uv1);   // metres (MakeStaticMesh scales to cm)
	}

	bool FPlanner::BuildOverlayFields(double T0, double BudgetMs)
	{
		FDmTownMesh& M = Out.Ground;
		M.Sec.SetNum(1);
		auto Vert = [this](const FVector2D& P, float Lift, const FVector2f& Uv0, const FColor& C, const FVector2f& Uv1) -> int32 { return OverlayVert(P, Lift, Uv0, C, Uv1); };
		auto ToC = [](const FLinearColor& L, float A = 0.f) { return Col(L.R, L.G, L.B, A); };
		// (wave 4 towns) fields: each parcel polygon is cut by a grid in its own row frame (cells of ~18 m, so the overlay
		// follows the terrain closely), every vertex warped by the shared field warp and draped on the ground; crop rows
		// from UV1.y = row angle. Centre-pivot circles are fans. (verifier) sliced by parcels: this was the longest phase.
		const float Cell = Age >= 4 ? 24.f : 18.f;
		if (OverlayParcel < 0) OverlayParcel = 0;
		for (; OverlayParcel < Parcels.Num(); ++OverlayParcel)
		{
			if (!In.bFieldOverlay) { OverlayParcel = Parcels.Num(); break; }
			if ((OverlayParcel & 3) == 0 && OverlayParcel > 0 && ((FPlatformTime::Seconds() - T0) * 1000.0 >= BudgetMs || Cancelled())) return false;
			const FParcel& F = Parcels[OverlayParcel];
			const FColor C = ToC(F.Col);
			if (F.bCircle)
			{
				const int32 Seg = 24;
				const int32 C0 = Vert(F.C, 0.35f, FVector2f(F.C), C, FVector2f(0.f, F.Ang));
				TArray<int32> Ring;
				for (int32 K = 0; K <= Seg; ++K)
				{
					const double A = K * UE_DOUBLE_TWO_PI / Seg;
					const FVector2D P = F.C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * F.Rad;
					Ring.Add(Vert(P, 0.35f, FVector2f(P), C, FVector2f(0.f, F.Ang)));
				}
				for (int32 K = 0; K < Seg; ++K) M.Tri(0, C0, Ring[K], Ring[K + 1], M.V[C0].N);
				continue;
			}
			const FVector2D Ux(FMath::Cos(F.Ang), FMath::Sin(F.Ang)), Uy(-FMath::Sin(F.Ang), FMath::Cos(F.Ang));
			TArray<FVector2D> L;
			double Ua0 = 1e30, Ua1 = -1e30, Va0 = 1e30, Va1 = -1e30;
			for (const FVector2D& P : F.Poly)
			{
				const FVector2D D = P - F.C;
				const FVector2D Q(FVector2D::DotProduct(D, Ux), FVector2D::DotProduct(D, Uy));
				L.Add(Q);
				Ua0 = FMath::Min(Ua0, Q.X); Ua1 = FMath::Max(Ua1, Q.X); Va0 = FMath::Min(Va0, Q.Y); Va1 = FMath::Max(Va1, Q.Y);
			}
			const int32 Nu = FMath::Clamp(FMath::CeilToInt((Ua1 - Ua0) / Cell), 1, 40), Nv = FMath::Clamp(FMath::CeilToInt((Va1 - Va0) / Cell), 1, 40);
			const double Su = (Ua1 - Ua0) / Nu, Sv = (Va1 - Va0) / Nv;
			TArray<FVector2D> Pc;
			TArray<int32, TInlineAllocator<16>> Ix;
			for (int32 J = 0; J < Nv; ++J)
				for (int32 I = 0; I < Nu; ++I)
				{
					Pc = L;
					const double Ua = Ua0 + I * Su, Ub = Ua + Su, Va = Va0 + J * Sv, Vb = Va + Sv;
					if (I > 0) ClipHalf(Pc, FVector2D(-1, 0), -Ua);
					if (I < Nu - 1) ClipHalf(Pc, FVector2D(1, 0), Ub);
					if (J > 0) ClipHalf(Pc, FVector2D(0, -1), -Va);
					if (J < Nv - 1) ClipHalf(Pc, FVector2D(0, 1), Vb);
					if (Pc.Num() < 3) continue;
					Ix.Reset();
					for (const FVector2D& Q : Pc)
					{
						const FVector2D P = Warp(F.C + Ux * Q.X + Uy * Q.Y);
						Ix.Add(Vert(P, 0.35f, FVector2f(P), C, FVector2f(0.f, F.Ang)));
					}
					for (int32 K = 1; K + 1 < Ix.Num(); ++K) M.Tri(0, Ix[0], Ix[K], Ix[K + 1], M.V[Ix[0]].N);
				}
		}
		return true;
	}

	void FPlanner::BuildOverlay()
	{
		FDmTownMesh& M = Out.Ground;
		M.Sec.SetNum(1);
		auto Vert = [this](const FVector2D& P, float Lift, const FVector2f& Uv0, const FColor& C, const FVector2f& Uv1) -> int32 { return OverlayVert(P, Lift, Uv0, C, Uv1); };
		auto ToC = [](const FLinearColor& L, float A = 0.f) { return Col(L.R, L.G, L.B, A); };
		// hedgerows along the boundaries of some holdings (temperate, before mechanised farming): warped like the fields
		if (In.bFieldOverlay)
			for (const FHedge& Hd : Hedges)
			{
				const FColor Hc = Col(0.12f, 0.2f, 0.08f);
				const float Hw = 1.3f;
				const int32 Ns = FMath::Max(1, int32(FVector2D::Distance(Hd.A, Hd.B) / 10.0));
				const FVector2D D0 = (Hd.B - Hd.A).GetSafeNormal();
				int32 Prev0 = -1, Prev1 = -1;
				for (int32 K = 0; K <= Ns; ++K)
				{
					const FVector2D P0 = FMath::Lerp(Hd.A, Hd.B, double(K) / Ns);
					const FVector2D Pa = Warp(P0), Pb = Warp(P0 + D0 * 2.0);
					const FVector2D D = (Pb - Pa).GetSafeNormal(), Nn(-D.Y, D.X);
					const int32 V0 = Vert(Pa - Nn * Hw, 0.6f, FVector2f(Pa), Hc, FVector2f(3.f, 0.f)), V1 = Vert(Pa + Nn * Hw, 0.6f, FVector2f(Pa), Hc, FVector2f(3.f, 0.f));
					if (Prev0 >= 0) M.Quad(0, Prev0, V0, V1, Prev1, M.V[V0].N);
					Prev0 = V0; Prev1 = V1;
				}
			}
		// streets: ribbons, u = metres along (lamp pools), v = -1..1 across; VC.a = lamp level
		const float Lamp = kLamp[Age];
		// streets are drawn where the town is: within the built radius (country roads run on to the fields)
		const double ClipR = FMath::Max(double(BuiltR), double(PlazaR) + 30.0) + 30.0;
		TArray<FStreet> Draw;
		for (const FStreet& S0 : Streets)
		{
			if (S0.Kind == EStreet::Country || S0.Kind == EStreet::Highway) { Draw.Add(S0); continue; }
			FStreet Cur = S0; Cur.P.Reset();
			for (const FVector2D& Q : S0.P)
			{
				if ((Q - Oc).Size() <= ClipR) Cur.P.Add(Q);
				else
				{
					if (S0.Kind == EStreet::Main && Cur.P.Num() > 0) { Cur.P.Add(Q); }
					if (Cur.P.Num() >= 2) Draw.Add(Cur);
					Cur.P.Reset();
				}
			}
			if (Cur.P.Num() >= 2) Draw.Add(Cur);
		}
		// main spokes keep running out to their country road
		for (const FStreet& S0 : Streets)
		{
			if (S0.Kind != EStreet::Main || S0.P.Num() < 2) continue;
			FStreet Link; Link.W = kCountryW[Age]; Link.Kind = EStreet::Country;
			for (const FVector2D& Q : S0.P) if ((Q - Oc).Size() > ClipR - 15.0) Link.P.Add(Q);
			if (Link.P.Num() >= 2) Draw.Add(Link);
		}
		for (const FStreet& S : Draw)
		{
			FLinearColor Sc;
			const bool bPaved = Age >= 4 && S.Kind != EStreet::Lane && S.Kind != EStreet::Country;
			if (Age <= 1) Sc = Lc(0.40f, 0.34f, 0.25f);
			else if (Age <= 3) Sc = (S.Kind == EStreet::Country) ? Lc(0.44f, 0.38f, 0.28f) : Lc(0.42f, 0.40f, 0.36f);
			else Sc = bPaved || S.Kind == EStreet::Country ? Lc(0.27f, 0.27f, 0.28f) : Lc(0.40f, 0.38f, 0.35f);
			if (In.bDead) Sc = Lc(0.36f, 0.33f, 0.26f);
			const float La = In.bDead ? 0.f : (S.Kind == EStreet::Country ? Lamp * 0.25f : (S.Kind == EStreet::Lane ? Lamp * 0.6f : Lamp));
			const FColor C = ToC(Sc, La);
			const float Kind = (Age >= 4 && S.Kind != EStreet::Lane) ? 2.f : 1.f;   // 1 = dirt / cobble, 2 = asphalt
			const float Spacing = Age >= 4 ? 32.f : 60.f;
			double Along = 0;
			int32 Prev0 = -1, Prev1 = -1;
			for (int32 I = 0; I < S.P.Num(); ++I)
			{
				const FVector2D Dprev = I > 0 ? (S.P[I] - S.P[I - 1]).GetSafeNormal() : (S.P[1] - S.P[0]).GetSafeNormal();
				const FVector2D Dnext = I + 1 < S.P.Num() ? (S.P[I + 1] - S.P[I]).GetSafeNormal() : Dprev;
				FVector2D D = (Dprev + Dnext).GetSafeNormal();
				if (D.IsNearlyZero()) D = Dnext;
				const FVector2D Nn(-D.Y, D.X);
				if (I > 0) Along += FVector2D::Distance(S.P[I], S.P[I - 1]);
				const float Lift = S.Kind == EStreet::Highway ? 0.6f : 0.45f;
				const float Hw = S.W * (S.Kind == EStreet::Country || S.Kind == EStreet::Lane ? 0.5f : 0.38f);
				const int32 V0 = Vert(S.P[I] - Nn * Hw, Lift, FVector2f(float(Along), -1.f), C, FVector2f(Kind, Spacing));
				const int32 V1 = Vert(S.P[I] + Nn * Hw, Lift, FVector2f(float(Along), 1.f), C, FVector2f(Kind, Spacing));
				if (Prev0 >= 0) M.Quad(0, Prev0, V0, V1, Prev1, M.V[V0].N);
				Prev0 = V0; Prev1 = V1;
			}
		}
		// plaza and quays
		// (wave 4 towns) the plaza in rings of ~7 m that drape over the ground (one fan of 10-40 triangles was a flat slab
		// across the slope), a ragged worn edge, and a few percent of tone variation per vertex (it read as one pale decagon)
		// (verifier fix) uv0.y is the ring fraction (0 centre .. 1 rim): M_TownGround puts the plaza lamps on the rim by it;
		// every ring at 1 lit the whole disc at night with a dark spot over the centre fan
		auto Disc = [&](const FVector2D& C, float Rr, const FLinearColor& Cl, float La)
		{
			const int32 Seg = FMath::Clamp(int32(Rr / 2.5f), 14, 48);
			const int32 NR = FMath::Clamp(int32(Rr / 7.f), 2, 8);
			auto Tone = [&](const FVector2D& P, double Edge) -> FColor
			{
				const float J = 0.94f + 0.12f * Hash01(uint32(FMath::RoundToInt(P.X * 3.0) + 900000), uint32(FMath::RoundToInt(P.Y * 3.0) + 900000), uint32(Seed) + 77u);
				const float E = float(FMath::Lerp(1.0, 0.86, FMath::Clamp(Edge, 0.0, 1.0)));   // worn, darker toward the edge
				return ToC(FLinearColor(Cl.R * J * E, Cl.G * J * E, Cl.B * J * E), La * (1.f - 0.5f * float(Edge)));
			};
			const int32 C0 = Vert(C, 0.5f, FVector2f(0.f, 0.f), Tone(C, 0.0), FVector2f(4.f, 0.f));
			TArray<int32> Prev, Ring;
			for (int32 Rg = 1; Rg <= NR; ++Rg)
			{
				const double F = double(Rg) / NR;
				Ring.Reset();
				for (int32 K = 0; K <= Seg; ++K)
				{
					const int32 Kw = K % Seg;
					const double A = Kw * UE_DOUBLE_TWO_PI / Seg;
					const double Rag = Rg == NR ? 0.07 * FMath::Sin(A * 3.0 + Seed) + 0.05 * (Hash01(uint32(Kw), uint32(Seed), 79u) - 0.5) : 0.04 * FMath::Sin(A * 3.0 + Seed);
					const double Rk = Rr * F * (1.0 + Rag);
					const FVector2D P = C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rk;
					const int32 Vi = K == Seg ? int32(Ring[0]) : Vert(P, 0.5f, FVector2f(float(Rk), float(F)), Tone(P, (F - 0.55) / 0.45), FVector2f(4.f, 0.f));
					Ring.Add(Vi);
				}
				for (int32 K = 0; K < Seg; ++K)
				{
					if (Rg == 1) M.Tri(0, C0, Ring[K], Ring[K + 1], M.V[C0].N);
					else M.Quad(0, Prev[K], Ring[K], Ring[K + 1], Prev[K + 1], M.V[Ring[K]].N);
				}
				Prev = Ring;
			}
		};
		const FLinearColor Pave = Age <= 1 ? Lc(0.40f, 0.34f, 0.25f) : (Age <= 3 ? Lc(0.44f, 0.41f, 0.37f) : Lc(0.43f, 0.43f, 0.42f));
		// (wave 4 towns) half the old lamp light: at night the plaza glowed as one white disc
		if (PlazaR > 0.f || Tier == 0) Disc(Oc, Tier == 0 ? 22.f : PlazaR + 2.f, Pave, In.bDead ? 0.f : kLamp[Age] * 0.6f);
		for (const FQuay& Q : Quays)
			for (int32 I = 0; I + 1 < Q.P.Num(); ++I)
			{
				if (FVector2D::Distance(Q.P[I], Q.P[I + 1]) > 220.0) continue;
				const FVector2D D = (Q.P[I + 1] - Q.P[I]).GetSafeNormal(), Nn(-D.Y, D.X);
				const FColor Qc = ToC(Age <= 3 ? Lc(0.46f, 0.44f, 0.40f) : Lc(0.42f, 0.42f, 0.42f), In.bDead ? 0.f : kLamp[Age]);
				const int32 V0 = Vert(Q.P[I] - Nn * 6.0, 0.5f, FVector2f(0.f, -1.f), Qc, FVector2f(4.f, 0.f)), V1 = Vert(Q.P[I] + Nn * 6.0, 0.5f, FVector2f(0.f, 1.f), Qc, FVector2f(4.f, 0.f));
				const int32 V2 = Vert(Q.P[I + 1] + Nn * 6.0, 0.5f, FVector2f(1.f, 1.f), Qc, FVector2f(4.f, 0.f)), V3 = Vert(Q.P[I + 1] - Nn * 6.0, 0.5f, FVector2f(1.f, -1.f), Qc, FVector2f(4.f, 0.f));
				M.Quad(0, V0, V3, V2, V1, M.V[V0].N);
			}
	}

	void FPlanner::EmitInstances()
	{
		FMatrix Bm(E0, N0, U0, FVector::ZeroVector);
		const FQuat Q0(Bm);
		const double SeaR = 1.0e7 + double(SeaM) * 100.0;
		Out.Inst.Reserve(Placed.Num());
		for (const FPlaced& P : Placed)
		{
			const FDmKitInfo& I = Kit.GetAt(Age, P.M, P.V);
			const FVector D = DirAt(P.C);
			double Floor;
			if (P.bWaterLevel) Floor = SeaR;
			else
			{
				// floor between the lowest and highest corner: walls reach 3 m below it
				const FVector2D Ux(FMath::Cos(P.Yaw), FMath::Sin(P.Yaw)), Uy(-FMath::Sin(P.Yaw), FMath::Cos(P.Yaw));
				const float Hw = I.W * 0.5f * P.Scale.X, Hd = I.D * 0.5f * P.Scale.Y;
				double Lo = G.Radius(D), Hi = Lo;
				if (Hw * Hd > 4.f)
					for (int32 K = 0; K < 4; ++K)
					{
						const FVector2D Cn = P.C + Ux * ((K & 1) ? Hw : -Hw) * 0.9 + Uy * ((K & 2) ? Hd : -Hd) * 0.9;
						const double Rk = G.Radius(DirAt(Cn));
						Lo = FMath::Min(Lo, Rk); Hi = FMath::Max(Hi, Rk);
					}
				Floor = FMath::Lerp(Lo, Hi, 0.3);
			}
			Floor += double(P.ZOff) * 100.0;
			const FVector Pos = D * Floor - Out.Centre;
			const FQuat Q = FQuat::FindBetweenNormals(U0, D) * Q0 * FQuat(FVector::UpVector, P.Yaw);
			FDmTownInst T;
			T.Mesh = P.M; T.Var = uint8(P.V); T.Pos = FVector3f(Pos); T.Rot = FQuat4f(Q); T.Scale = P.Scale; T.Key = P.Key;
			float State = P.State;
			FLinearColor Wl = P.Wall, Rf = P.Roof; float Lit = P.Lit;
			if (In.bDead) { State = 0.55f; Lit = 0.f; }
			T.Cd[0] = Wl.R; T.Cd[1] = Wl.G; T.Cd[2] = Wl.B; T.Cd[3] = Rf.R; T.Cd[4] = Rf.G; T.Cd[5] = Rf.B; T.Cd[6] = Lit; T.Cd[7] = State;
			T.W = I.W * P.Scale.X; T.D = I.D * P.Scale.Y; T.H = I.H * P.Scale.Z;
			// (wave 4 towns) which real asset stands here (stable per slot), and its own ruin in a dead town
			T.Sub = I.NumAlt > 1 ? uint8(Hash(P.Key, 0x51u) % uint32(I.NumAlt)) : 0;
			T.State = P.bRuinAsset ? uint8(EDmTownState::Ruin) : 0;
			Out.Inst.Add(T);
			if (P.M == EDmTownMesh::Stack || P.M == EDmTownMesh::Factory || P.M == EDmTownMesh::Campfire || P.M == EDmTownMesh::CoolingTower)
				Out.Smoke.Add(Out.Centre + Pos + Q.RotateVector(FVector(0, 0, I.H * 100.0 * P.Scale.Z)));
		}
	}

	void FPlanner::Run()
	{
		while (!Step(1e9)) {}
	}

	bool FPlanner::Step(double BudgetMs)
	{
		// (wave 4 towns) the plan in phases; a slice runs phases (and bands of the height grid) until its budget is used
		const double T0 = FPlatformTime::Seconds();
		ON_SCOPE_EXIT
		{
			const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
			WorkMs += Ms;
			++Out.Slices;
			Out.MaxSliceMs = FMath::Max(Out.MaxSliceMs, Ms);
		};
		do
		{
			// (wave 4 towns) CPU per phase (the longest phase is the longest slice a worker can be held for)
			const int32 PhaseNow = Stage;
			const double PhaseT0 = FPlatformTime::Seconds();
			ON_SCOPE_EXIT { if (PhaseNow >= 0 && PhaseNow < 9) StageMs[PhaseNow] += (FPlatformTime::Seconds() - PhaseT0) * 1000.0; };
			if (Cancelled()) { bCancelled = true; const int32 Sl = Out.Slices; Out = FDmTownPlan(); Out.Slices = Sl; Stage = 99; return true; }
			switch (Stage)
			{
			case 0:
				if (In.Dir.IsNearlyZero()) { Stage = 99; return true; }
				Setup();
				Out.Id = In.Id;
				BuildGridBegin();
				Stage = 1;
				break;
			case 1:
				if (!BuildGridRows(T0, BudgetMs)) return false;
				BuildGridEnd();
				GridMs = WorkMs + (FPlatformTime::Seconds() - T0) * 1000.0;
				Stage = 2;
				break;
			case 2:
				if (RasterRow < 0)
				{
					FindCoast();
					BuildRaster();
					Out.Centre = DirAt(Oc) * G.Radius(DirAt(Oc));
					break;   // (verifier) the rows in the next passes (they check the budget)
				}
				if (!BuildRasterRows(T0, BudgetMs)) return false;
				Stage = 3;
				break;
			case 3: PlanStreets(); RasterStreets(); Stage = 4; break;
			case 4: PlanDocks(); PlanCivic(); Stage = 5; break;
			case 5: PlanHouses(); Stage = 6; break;
			case 6: PlanWalls(); PlanFields(); Stage = 7; break;
			case 7:
				// a dead settlement: every building is a ruin of its footprint, a share of them gone
				if (In.bDead && !bDeadDone)
				{
					TArray<FPlaced> Keep;
					for (const FPlaced& P : Placed)
					{
						if (Hash01(P.Key, 3u) > FMath::Clamp(In.Ruin * 1.4f, 0.f, 0.9f)) continue;
						if (P.bWaterLevel && P.M != EDmTownMesh::Pier) continue;
						FPlaced Q = P;
						const FDmKitInfo& I = Kit.GetAt(Age, P.M, P.V);
						const bool bKeepShape = P.M == EDmTownMesh::WallSeg || P.M == EDmTownMesh::WallTower || P.M == EDmTownMesh::Gatehouse || P.M == EDmTownMesh::Pier
							|| P.M == EDmTownMesh::Wonder || P.M == EDmTownMesh::TempleEarly || P.M == EDmTownMesh::Stones || P.M == EDmTownMesh::Spoil;
						// (wave 4 towns) a real asset falls into its own ruin on the same footprint (ASSET-INTEGRATION 4.4)
						if (I.NumAlt > 0 && I.bAssetRuin) Q.bRuinAsset = true;
						else if (!bKeepShape)
						{
							Q.M = EDmTownMesh::Ruin; Q.V = int32(P.Key % 3u);
							Q.Scale = FVector3f(FMath::Max(0.4f, I.W / 10.f), FMath::Max(0.4f, I.D / 8.f), FMath::Clamp(I.H / 8.f, 0.5f, 2.f));
						}
						Q.Wall = FLinearColor(0.55f, 0.52f, 0.48f); Q.Roof = Q.Wall;
						Keep.Add(Q);
					}
					Placed = MoveTemp(Keep);
					FieldQ.Reset();
					Parcels.Reset();
					Hedges.Reset();
				}
				bDeadDone = true;
				// (verifier) the fields in slices of parcels, then the streets, plaza and quays
				if (!BuildOverlayFields(T0, BudgetMs)) return false;
				BuildOverlay();
				Stage = 8;
				break;
			case 8:
				EmitInstances();
				Out.RadiusM = FMath::Max(BuiltR, R);
				Out.ExtentM = Ex;
				Out.Age = int8(Age); Out.Tier = int8(Tier); Out.CultureStyle = int8(In.CultureStyle); Out.Climate = int8(Style);
				Out.Ms = WorkMs + (FPlatformTime::Seconds() - T0) * 1000.0;
				Out.Note = FString::Printf(TEXT("tier %d age %d style %d culture style %d pop %.0f target %d R %.0f m ext %.0f m grid %d^2 (%.0f ms) coast %d%s ground %.0f m sea %.0f m parcels %d rivers %d (%d nodes, bank %.0f m); phases ms setup %.1f grid %.1f raster %.1f streets %.1f civic %.1f houses %.1f walls+fields %.1f overlay %.1f"),
					Tier, Age, int32(Style), int32(In.CultureStyle), In.Pop, Target, R, Ex, GN, GridMs, bCoastal ? 1 : 0, bFarPort ? TEXT(" far") : TEXT(""), GridH(Oc), SeaM, Parcels.Num(), NumRiverSegs, RiverCells, RiverBankDbg,
					StageMs[0], StageMs[1], StageMs[2], StageMs[3], StageMs[4], StageMs[5], StageMs[6], StageMs[7]);
				Stage = 9;
				return true;
			default:
				return true;
			}
		} while ((FPlatformTime::Seconds() - T0) * 1000.0 < BudgetMs);
		return false;
	}
}

// ------------------------------------------------------------------ stepper (wave 4 towns)

struct FDmTownPlanStepper::FImpl
{
	FPlanner P;
	FImpl(const FDmTownInput& In, const FDmTownsWorldSnap& Snap, const FDmTownsGround& G, const FDmKitTable& Kit, FDmTownPlan& Out) : P(In, Snap, G, Kit, Out) {}
};

FDmTownPlanStepper::FDmTownPlanStepper(const FDmTownInput& In, const FDmTownsWorldSnap& Snap, const FDmTownsGround& Ground, const FDmKitTable& Kit, FDmTownPlan& Out,
	const std::atomic<bool>* Cancel)
{
	Out = FDmTownPlan();
	Impl = MakeUnique<FImpl>(In, Snap, Ground, Kit, Out);
	Impl->P.Cancel = Cancel;
	if (In.Dir.IsNearlyZero()) Impl->P.Cancel = nullptr;
}

FDmTownPlanStepper::~FDmTownPlanStepper() = default;

bool FDmTownPlanStepper::Step(double BudgetMs) { return Impl->P.Step(BudgetMs); }
bool FDmTownPlanStepper::WasCancelled() const { return Impl->P.bCancelled; }

const FDmKitInfo& FDmKitTable::GetAt(int32 Age, EDmTownMesh Id, int32 V) const
{
	const int32 A = FMath::Clamp(Age, 0, 6), K = int32(Id);
	if (K >= 0 && K < int32(EDmTownMesh::Num) && Aged[A][K].Num() > 0) return Aged[A][K][FMath::Clamp(V, 0, Aged[A][K].Num() - 1)];
	return Get(Id, V);
}

float DmTownsPlan::BuiltRadius(int32 Tier, double Pop, int32 Age)
{
	Tier = FMath::Clamp(Tier, 0, 4); Age = FMath::Clamp(Age, 0, 6);
	const int32 N = FMath::Clamp(FMath::RoundToInt(float(Pop) / kPPH[Age]), kNMin[Tier], kNMax[Tier]);
	if (Tier == 0) return 18.f + 3.f * N;
	return FMath::Sqrt(N * kLot[Tier] * kLotAge[Age] / PI) + kPlazaR[Tier];
}

void DmTownsPlan::PlanTown(const FDmTownInput& In, const FDmTownsWorldSnap& Snap, const FDmTownsGround& Ground, const FDmKitTable& Kit, FDmTownPlan& Out)
{
	Out = FDmTownPlan();
	if (In.Dir.IsNearlyZero()) return;
	FPlanner P(In, Snap, Ground, Kit, Out);
	P.Run();
}
