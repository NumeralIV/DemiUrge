// DEMIURGE — Game/Towns/DmTownsKit.cpp : see the header. The building generators.

#include "Game/Towns/DmTownsKit.h"
#include "Game/Towns/DmTownsAssets.h"
#include "Game/Towns/DmTownsPlan.h"

#include "Engine/World.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/PackageName.h"

#include "Engine/StaticMesh.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmTownsKit, Log, All);

using namespace DmTowns;

namespace
{
	const FVector3f Z3(0, 0, 1);
	constexpr int32 SB = DmTownSec::Body;
	constexpr float Foot = -3.f;   // walls reach this far below the floor

	FColor WallC() { return ColT(0.86f, 0.84f, 0.80f, DmTownTint::Wall); }
	FColor RoofC() { return ColT(0.80f, 0.80f, 0.80f, DmTownTint::Roof); }
	FColor Fix(float R, float G, float B) { return ColT(R, G, B, DmTownTint::Fixed); }
	FColor LitWarm(FDmTownRand& R) { const float K = R.R(0.6f, 1.f); return Col(1.f * K, 0.62f * K, 0.30f * K); }
	FColor LitCool(FDmTownRand& R) { const float K = R.R(0.6f, 1.f); return Col(0.80f * K, 0.86f * K, 1.f * K); }
	uint8 RB(FDmTownRand& R) { return uint8(R.I(0, 255)); }

	enum class ERoof : uint8 { Gable, GableY, Hip, Flat, Sawtooth, Cone, None };

	struct FHouse
	{
		float W = 8.f, D = 6.f;
		int32 Storeys = 1;
		float StoreyH = 3.f;
		ERoof Roof = ERoof::Gable;
		float Pitch = 40.f;              // degrees
		float Overhang = 0.4f;
		EDmTownPat WallPat = EDmTownPat::Plaster, RoofPat = EDmTownPat::Tile;
		FColor Wall = WallC(), RoofCol = RoofC();
		bool bTimber = false, bSills = true, bPlinth = true, bDoor = true, bWindows = true;
		bool bCoolLight = false;
		int32 Chimneys = 0;
		float WinW = 0.9f, WinH = 1.2f, WinSpacing = 3.f;
		float Parapet = 0.9f;
		uint32 Seed = 1;
	};

	FVector2f P1(EDmTownPat P) { return Pat(P); }

	void Walls(FDmTownMesh& M, const FHouse& S, float E, int32 Lod, FDmTownRand& R)
	{
		const float hw = S.W * 0.5f, hd = S.D * 0.5f;
		const FVector3f C[4] = { FVector3f(-hw, -hd, 0), FVector3f(hw, -hd, 0), FVector3f(hw, hd, 0), FVector3f(-hw, hd, 0) };
		for (int32 F = 0; F < 4; ++F)
		{
			const FVector3f A = C[F], B = C[(F + 1) % 4];
			const FVector3f U = (B - A).GetSafeNormal();
			FVector3f Nn = FVector3f::CrossProduct(U, Z3).GetSafeNormal();
			if (FVector3f::DotProduct(Nn, (A + B) * 0.5f) < 0.f) Nn = -Nn;
			const float L = (B - A).Size();
			const FVector3f Zb(0, 0, Foot), Zt(0, 0, E);
			M.Q4(SB, A + Zb, B + Zb, B + Zt, A + Zt, S.Wall, P1(S.WallPat), Nn);
			if (S.bPlinth && Lod == 0)
			{
				const FVector3f Po = Nn * 0.07f;
				const FColor Pc = Fix(0.42f, 0.40f, 0.37f);
				M.Q4(SB, A + Zb + Po - U * 0.07f, B + Zb + Po + U * 0.07f, B + Po + U * 0.07f + FVector3f(0, 0, 0.45f), A + Po - U * 0.07f + FVector3f(0, 0, 0.45f), Pc, P1(EDmTownPat::Stone), Nn);
			}
			if (!S.bWindows) continue;
			int32 Nw = FMath::FloorToInt((L - 1.0f) / S.WinSpacing);
			if (Nw < 1) Nw = (L > 2.2f) ? 1 : 0;
			const bool bDoorFace = (F == 0) && S.bDoor;
			for (int32 Sy = 0; Sy < S.Storeys; ++Sy)
			{
				const float Zc = Sy * S.StoreyH + (Sy == 0 ? 0.95f : 0.85f) + S.WinH * 0.5f;
				if (Zc + S.WinH * 0.5f > E - 0.15f) continue;
				for (int32 K = 0; K < Nw; ++K)
				{
					const FVector3f P = A + U * ((K + 0.5f) * L / Nw);
					if (bDoorFace && Sy == 0 && K == Nw / 2)
					{
						const FVector3f Dn = Nn * 0.05f;
						M.Q4(SB, P - U * 0.55f + Dn, P + U * 0.55f + Dn, P + U * 0.55f + Dn + FVector3f(0, 0, 2.1f), P - U * 0.55f + Dn + FVector3f(0, 0, 2.1f), Fix(0.20f, 0.14f, 0.10f), P1(EDmTownPat::Wood), Nn);
						// door lantern (lit with the house)
						M.Window(P + U * 0.85f + FVector3f(0, 0, 2.25f), U, Nn, 0.22f, 0.3f, Col(1.f, 0.7f, 0.35f), RB(R));
						continue;
					}
					M.Window(P + FVector3f(0, 0, Zc), U, Nn, S.WinW, S.WinH, S.bCoolLight && R.P(0.4f) ? LitCool(R) : LitWarm(R), RB(R));
					if (S.bSills && Lod == 0)
						M.Box(SB, P + FVector3f(0, 0, Zc - S.WinH * 0.5f - 0.05f) + Nn * 0.07f, FVector3f(S.WinW * 0.5f + 0.08f, 0.07f, 0.05f), FMath::Atan2(U.Y, U.X), Fix(0.75f, 0.72f, 0.66f), P1(EDmTownPat::Stone));
				}
			}
			if (S.bTimber && Lod == 0)
			{
				const FColor Tc = Fix(0.20f, 0.14f, 0.10f);
				const float Yaw = FMath::Atan2(U.Y, U.X);
				for (int32 Sy = 1; Sy <= S.Storeys; ++Sy)
					M.Box(SB, (A + B) * 0.5f + FVector3f(0, 0, FMath::Min(Sy * S.StoreyH, E - 0.1f)) + Nn * 0.04f, FVector3f(L * 0.5f, 0.05f, 0.1f), Yaw, Tc, P1(EDmTownPat::Wood));
				for (int32 K = 0; K <= Nw; ++K)
				{
					const float Al = (Nw > 0) ? K * L / Nw : 0.f;
					M.Box(SB, A + U * FMath::Clamp(Al, 0.1f, L - 0.1f) + FVector3f(0, 0, E * 0.5f) + Nn * 0.04f, FVector3f(0.09f, 0.05f, E * 0.5f), Yaw, Tc, P1(EDmTownPat::Wood));
				}
			}
		}
	}

	float GableRoof(FDmTownMesh& M, const FHouse& S, float E, int32 Lod, bool bAlongY)
	{
		// ridge along X (or along Y for bAlongY: gable end facing the street)
		const float hw = (bAlongY ? S.D : S.W) * 0.5f, hd = (bAlongY ? S.W : S.D) * 0.5f, o = S.Overhang;
		auto P = [bAlongY](float X, float Y, float Zz) { return bAlongY ? FVector3f(Y, X, Zz) : FVector3f(X, Y, Zz); };
		const float tp = FMath::Tan(FMath::DegreesToRadians(S.Pitch));
		const float t = S.RoofPat == EDmTownPat::Thatch ? 0.35f : 0.14f;
		const float Rh = hd * tp, zr = E + Rh, ye = hd + o, ze = E - o * tp, xv = hw + o * 0.6f;
		const FColor Under = Fix(0.22f, 0.19f, 0.16f);
		const FVector2f Rp = P1(S.RoofPat);
		for (int32 Sd = -1; Sd <= 1; Sd += 2)
		{
			const FVector3f A = P(-xv, Sd * ye, ze), B = P(xv, Sd * ye, ze), C = P(xv, 0, zr), D = P(-xv, 0, zr);
			const FVector3f Out = P(0, float(Sd) * tp, 1.f);
			M.Q4(SB, A, B, C, D, S.RoofCol, Rp, Out);
			if (Lod <= 1)
			{
				const FVector3f Dn(0, 0, -t);
				M.Q4(SB, A + Dn, B + Dn, C + Dn, D + Dn, Under, Rp, -Out);
				M.Q4(SB, A, B, B + Dn, A + Dn, Under, Rp, P(0, float(Sd), 0));
				M.Q4(SB, A, D, D + Dn, A + Dn, Under, Rp, P(-1, 0, 0));
				M.Q4(SB, B, C, C + Dn, B + Dn, Under, Rp, P(1, 0, 0));
			}
		}
		for (int32 Sx = -1; Sx <= 1; Sx += 2)
			M.T3(SB, P(Sx * hw, -hd, E), P(Sx * hw, hd, E), P(Sx * hw, 0, zr - 0.05f), S.Wall, P1(S.WallPat), P(float(Sx), 0, 0));
		if (Lod == 0)
			M.Box(SB, P(0, 0, zr + 0.02f), bAlongY ? FVector3f(0.14f, xv, 0.08f) : FVector3f(xv, 0.14f, 0.08f), 0.f, Mul(S.RoofCol, 0.8f), Rp);
		return zr;
	}

	float HipRoof(FDmTownMesh& M, const FHouse& S, float E)
	{
		const bool bSwap = S.D > S.W;
		const float hw = (bSwap ? S.D : S.W) * 0.5f, hd = (bSwap ? S.W : S.D) * 0.5f, o = S.Overhang;
		const float tp = FMath::Tan(FMath::DegreesToRadians(S.Pitch));
		const float Rh = hd * tp, zr = E + Rh, ze = E - o * tp, hr = FMath::Max(0.f, hw - hd);
		auto P = [bSwap](float X, float Y, float Zz) { return bSwap ? FVector3f(Y, X, Zz) : FVector3f(X, Y, Zz); };
		const float Ex = hw + o, Ey = hd + o;
		const FVector2f Rp = P1(S.RoofPat);
		for (int32 Sd = -1; Sd <= 1; Sd += 2)
		{
			M.Q4(SB, P(-Ex, Sd * Ey, ze), P(Ex, Sd * Ey, ze), P(hr, 0, zr), P(-hr, 0, zr), S.RoofCol, Rp, P(0, float(Sd) * tp, 1.f));
			M.T3(SB, P(Sd * Ex, -Ey, ze), P(Sd * Ex, Ey, ze), P(Sd * hr, 0, zr), S.RoofCol, Rp, P(float(Sd) * tp, 0, 1.f));
		}
		M.Q4(SB, P(-Ex, -Ey, ze - 0.05f), P(Ex, -Ey, ze - 0.05f), P(Ex, Ey, ze - 0.05f), P(-Ex, Ey, ze - 0.05f), Fix(0.2f, 0.18f, 0.16f), Rp, -Z3);
		return zr;
	}

	float FlatRoof(FDmTownMesh& M, const FHouse& S, float E, int32 Lod, FDmTownRand& R)
	{
		const float hw = S.W * 0.5f, hd = S.D * 0.5f;
		M.Q4(SB, FVector3f(-hw, -hd, E), FVector3f(hw, -hd, E), FVector3f(hw, hd, E), FVector3f(-hw, hd, E), S.RoofCol, P1(S.RoofPat), Z3);
		const float Ph = S.Parapet, T = 0.25f;
		if (Ph > 0.f)
		{
			const FVector2f Wp = P1(S.WallPat);
			M.Box(SB, FVector3f(0, -hd + T, E + Ph * 0.5f), FVector3f(hw, T, Ph * 0.5f), 0.f, S.Wall, Wp);
			M.Box(SB, FVector3f(0, hd - T, E + Ph * 0.5f), FVector3f(hw, T, Ph * 0.5f), 0.f, S.Wall, Wp);
			M.Box(SB, FVector3f(-hw + T, 0, E + Ph * 0.5f), FVector3f(T, hd - 2 * T, Ph * 0.5f), 0.f, S.Wall, Wp);
			M.Box(SB, FVector3f(hw - T, 0, E + Ph * 0.5f), FVector3f(T, hd - 2 * T, Ph * 0.5f), 0.f, S.Wall, Wp);
		}
		if (Lod <= 1)
		{
			const int32 Nc = R.I(1, 3);
			for (int32 I = 0; I < Nc; ++I)
			{
				const float Bx = R.R(1.f, 3.f), By = R.R(1.f, 3.f), Bz = R.R(0.8f, 2.6f);
				if (hw < Bx + 1.f || hd < By + 1.f) continue;
				M.Box(SB, FVector3f(R.R(-hw + Bx + 0.6f, hw - Bx - 0.6f), R.R(-hd + By + 0.6f, hd - By - 0.6f), E + Bz * 0.5f), FVector3f(Bx * 0.5f, By * 0.5f, Bz * 0.5f), 0.f, Fix(0.52f, 0.51f, 0.49f), P1(EDmTownPat::Concrete));
			}
		}
		return E + Ph;
	}

	void SawtoothRoof(FDmTownMesh& M, const FHouse& S, float E, FDmTownRand& R)
	{
		const float hw = S.W * 0.5f, hd = S.D * 0.5f;
		const int32 N = FMath::Max(1, FMath::RoundToInt(S.W / 9.f));
		const float P = S.W / N, Ht = 3.2f;
		const FVector2f Rp = P1(EDmTownPat::Metal);
		for (int32 I = 0; I < N; ++I)
		{
			const float X0 = -hw + I * P, X1 = X0 + P * 0.82f, X2 = X0 + P;
			M.Q4(SB, FVector3f(X0, -hd, E), FVector3f(X0, hd, E), FVector3f(X1, hd, E + Ht), FVector3f(X1, -hd, E + Ht), S.RoofCol, Rp, FVector3f(-0.4f, 0, 1));
			M.Window(FVector3f(X1 + 0.02f, 0, E + Ht * 0.5f), FVector3f(0, 1, 0), FVector3f(1, 0, 0), S.D - 0.8f, Ht - 0.5f, Col(0.55f, 0.62f, 0.7f), RB(R));
			M.Q4(SB, FVector3f(X1, -hd, E), FVector3f(X2, -hd, E), FVector3f(X2, hd, E), FVector3f(X1, hd, E), Mul(S.RoofCol, 0.85f), Rp, Z3);
			for (int32 Sd = -1; Sd <= 1; Sd += 2)
				M.T3(SB, FVector3f(X0, Sd * hd, E), FVector3f(X1, Sd * hd, E), FVector3f(X1, Sd * hd, E + Ht), S.Wall, P1(S.WallPat), FVector3f(0, float(Sd), 0));
		}
	}

	void Chimneys(FDmTownMesh& M, const FHouse& S, float E, float RidgeZ, int32 Lod, FDmTownRand& R)
	{
		for (int32 I = 0; I < S.Chimneys; ++I)
		{
			const float X = S.W * (S.Chimneys == 1 ? R.R(-0.3f, 0.3f) : (I == 0 ? -0.36f : 0.36f));
			const float Y = R.R(-0.25f, 0.25f) * S.D;
			const float Top = RidgeZ + R.R(0.6f, 1.1f);
			const FColor Cc = R.P(0.5f) ? Fix(0.45f, 0.28f, 0.22f) : Fix(0.52f, 0.50f, 0.46f);
			M.Box(SB, FVector3f(X, Y, (E + Top) * 0.5f), FVector3f(0.32f, 0.32f, (Top - E) * 0.5f), 0.f, Cc, P1(EDmTownPat::Brick));
			if (Lod == 0) M.Box(SB, FVector3f(X, Y, Top + 0.06f), FVector3f(0.42f, 0.42f, 0.06f), 0.f, Mul(Cc, 0.7f), P1(EDmTownPat::Stone));
		}
	}

	float GenHouse(FDmTownMesh& M, const FHouse& S, int32 Lod)
	{
		FDmTownRand R(S.Seed);
		const float E = S.Storeys * S.StoreyH;
		Walls(M, S, E, Lod, R);
		float Top = E;
		switch (S.Roof)
		{
		case ERoof::Gable: Top = GableRoof(M, S, E, Lod, false); break;
		case ERoof::GableY: Top = GableRoof(M, S, E, Lod, true); break;
		case ERoof::Hip: Top = HipRoof(M, S, E); break;
		case ERoof::Flat: Top = FlatRoof(M, S, E, Lod, R); break;
		case ERoof::Sawtooth: SawtoothRoof(M, S, E, R); Top = E + 3.2f; break;
		case ERoof::Cone: { const float Rr = FMath::Max(S.W, S.D) * 0.5f + S.Overhang; const float H = Rr * FMath::Tan(FMath::DegreesToRadians(S.Pitch)); M.Cone(SB, FVector3f(0, 0, E - 0.2f), Rr, H, Lod == 0 ? 14 : 8, S.RoofCol, P1(S.RoofPat)); Top = E + H; } break;
		case ERoof::None: break;
		}
		if (S.Roof == ERoof::Gable || S.Roof == ERoof::GableY || S.Roof == ERoof::Hip) Chimneys(M, S, E, Top, Lod, R);
		return Top;
	}

	// ------------------------------------------------------------------ homes

	void Tent(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		const int32 Sides = Lod == 0 ? 12 : 7;
		const FColor Hide = ColT(0.62f, 0.50f, 0.36f, DmTownTint::Roof);
		I.WallPat = EDmTownPat::Hide; I.RoofPat = EDmTownPat::Hide; I.bLit = false;
		if (V == 0)          // tipi with poles
		{
			M.Cone(SB, FVector3f(0, 0, -0.3f), 2.6f, 5.0f, Sides, Hide, P1(EDmTownPat::Hide));
			if (Lod == 0) for (int32 K = 0; K < 5; ++K) { const float A = K * 1.256f; M.Beam(SB, FVector3f(0, 0, 3.6f), FVector3f(FMath::Cos(A) * 0.5f, FMath::Sin(A) * 0.5f, 5.6f), 0.08f, 0.08f, Fix(0.3f, 0.22f, 0.14f), P1(EDmTownPat::Wood)); }
			I.W = I.D = 5.2f; I.H = 5.2f;
		}
		else if (V == 1)     // yurt: low drum + shallow dome
		{
			M.Cylinder(SB, FVector3f(0, 0, -0.5f), 3.2f, 3.2f, 2.3f, Sides, ColT(0.80f, 0.74f, 0.62f, DmTownTint::Wall), false, P1(EDmTownPat::Hide));
			M.Dome(SB, FVector3f(0, 0, 1.8f), 3.3f, 0.45f, Sides, Lod == 0 ? 4 : 2, Hide, P1(EDmTownPat::Hide));
			I.W = I.D = 6.6f; I.H = 3.3f;
		}
		else                 // hide lean-to ridge tent
		{
			FHouse S; S.W = 5.f; S.D = 3.6f; S.Storeys = 1; S.StoreyH = 0.2f; S.Roof = ERoof::Gable; S.Pitch = 52.f; S.Overhang = 0.f;
			S.WallPat = S.RoofPat = EDmTownPat::Hide; S.RoofCol = Hide; S.Wall = Hide; S.bWindows = false; S.bPlinth = false;
			GenHouse(M, S, Lod);
			I.W = 5.f; I.D = 3.6f; I.H = 2.5f;
		}
	}

	void Hut(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		I.WallPat = EDmTownPat::Mud; I.RoofPat = EDmTownPat::Thatch;
		const int32 Sides = Lod == 0 ? 14 : 8;
		const float R0 = 2.6f + 0.5f * V;
		if (V < 3)   // round wattle-and-daub hut, conical thatch
		{
			M.Cylinder(SB, FVector3f(0, 0, Foot), R0, R0, 1.9f - Foot, Sides, WallC(), false, P1(EDmTownPat::Mud));
			M.Cone(SB, FVector3f(0, 0, 1.7f), R0 + 0.6f, R0 * 1.35f + 0.6f * V, Sides, ColT(0.78f, 0.70f, 0.52f, DmTownTint::Roof), P1(EDmTownPat::Thatch));
			FDmTownRand R(17u + V);
			M.Q4(SB, FVector3f(-0.45f, -R0 - 0.03f, 0), FVector3f(0.45f, -R0 - 0.03f, 0), FVector3f(0.45f, -R0 - 0.03f, 1.6f), FVector3f(-0.45f, -R0 - 0.03f, 1.6f), Fix(0.08f, 0.06f, 0.05f), P1(EDmTownPat::Wood), FVector3f(0, -1, 0));
			M.Window(FVector3f(0, -R0 - 0.06f, 0.8f), FVector3f(1, 0, 0), FVector3f(0, -1, 0), 0.8f, 1.5f, Col(1.f, 0.55f, 0.22f), RB(R));
			I.W = I.D = 2.f * R0 + 1.2f; I.H = 1.7f + R0 * 1.35f;
		}
		else         // rectangular hut, steep thatch
		{
			FHouse S; S.W = 7.f; S.D = 4.5f; S.StoreyH = 2.0f; S.Roof = ERoof::Gable; S.Pitch = 50.f; S.Overhang = 0.6f;
			S.WallPat = EDmTownPat::Mud; S.RoofPat = EDmTownPat::Thatch; S.RoofCol = ColT(0.78f, 0.70f, 0.52f, DmTownTint::Roof); S.WinW = 0.6f; S.WinH = 0.7f; S.WinSpacing = 3.2f; S.bSills = false; S.bPlinth = false; S.Seed = 31;
			I.H = GenHouse(M, S, Lod); I.W = 7.f; I.D = 4.5f;
		}
	}

	void Longhouse(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		FHouse S; S.W = V == 0 ? 16.f : 22.f; S.D = 6.5f; S.StoreyH = 2.2f; S.Roof = ERoof::Gable; S.Pitch = 52.f; S.Overhang = 0.7f;
		S.WallPat = EDmTownPat::Wood; S.RoofPat = EDmTownPat::Thatch; S.RoofCol = ColT(0.74f, 0.66f, 0.50f, DmTownTint::Roof); S.WinW = 0.6f; S.WinH = 0.6f; S.WinSpacing = 5.f; S.bSills = false; S.bPlinth = false; S.Seed = 41 + V;
		I.H = GenHouse(M, S, Lod); I.W = S.W; I.D = S.D; I.WallPat = EDmTownPat::Wood; I.RoofPat = EDmTownPat::Thatch;
	}

	void MudHouse(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		FDmTownRand R(51u + V);
		FHouse S; S.W = 6.f + 1.5f * V; S.D = 5.5f + (V % 2) * 1.5f; S.Storeys = 1 + (V >= 2 ? 1 : 0); S.StoreyH = 2.8f; S.Roof = ERoof::Flat; S.Parapet = 0.5f;
		S.WallPat = EDmTownPat::Mud; S.RoofPat = EDmTownPat::Mud; S.RoofCol = ColT(0.72f, 0.62f, 0.48f, DmTownTint::Wall); S.WinW = 0.6f; S.WinH = 0.8f; S.WinSpacing = 2.6f; S.bSills = false; S.bPlinth = false; S.Seed = 51 + V;
		I.H = GenHouse(M, S, Lod); I.W = S.W; I.D = S.D; I.WallPat = EDmTownPat::Mud; I.RoofPat = EDmTownPat::Mud;
		if (Lod == 0) // roof beams sticking out
			for (int32 K = 0; K < 4; ++K) M.Box(SB, FVector3f(-S.W * 0.35f + K * S.W * 0.23f, -S.D * 0.5f - 0.2f, S.Storeys * S.StoreyH - 0.3f), FVector3f(0.08f, 0.3f, 0.08f), 0.f, Fix(0.3f, 0.22f, 0.15f), P1(EDmTownPat::Wood));
		if (V == 3) { FHouse T = S; T.W = 3.f; T.D = 3.f; T.Storeys = 1; T.bDoor = false; FDmTownMesh Tmp; GenHouse(Tmp, T, Lod); M.Append(Tmp, FTransform3f(FQuat4f::Identity, FVector3f(S.W * 0.25f, S.D * 0.15f, S.Storeys * S.StoreyH))); I.H += 2.8f; }
	}

	void Cottage(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		FHouse S; S.Seed = 61 + V;
		static const float Ws[6] = { 8.f, 9.5f, 7.f, 11.f, 9.f, 10.f };
		static const float Ds[6] = { 6.f, 6.5f, 5.5f, 7.f, 7.5f, 6.5f };
		S.W = Ws[V]; S.D = Ds[V]; S.Storeys = (V == 1 || V == 3 || V == 5) ? 2 : 1; S.StoreyH = 2.9f;
		S.Roof = (V == 4) ? ERoof::Hip : ERoof::Gable; S.Pitch = 38.f + 4.f * (V % 3); S.Chimneys = 1 + (V == 3 ? 1 : 0);
		S.bTimber = (V == 2 || V == 5); S.WallPat = (V == 3) ? EDmTownPat::Stone : EDmTownPat::Plaster; S.RoofPat = (V == 4) ? EDmTownPat::Slate : EDmTownPat::Tile;
		I.H = GenHouse(M, S, Lod); I.W = S.W; I.D = S.D; I.WallPat = S.WallPat; I.RoofPat = S.RoofPat;
		if (V == 1 || V == 3)   // L-wing at the back
		{
			FHouse Wg = S; Wg.W = 5.f; Wg.D = 5.f; Wg.Storeys = 1; Wg.Chimneys = 0; Wg.bDoor = false; Wg.Seed = S.Seed + 7;
			FDmTownMesh Tmp; GenHouse(Tmp, Wg, Lod);
			M.Append(Tmp, FTransform3f(FQuat4f(Z3, PI * 0.5f), FVector3f(S.W * 0.5f - 2.5f, S.D * 0.5f + 2.2f, 0.f)));
			I.D = S.D + 4.4f;
		}
	}

	void Townhouse(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		FHouse S; S.Seed = 71 + V;
		S.W = 6.f + (V % 3) * 1.2f; S.D = 10.f + (V % 2) * 2.f; S.Storeys = 3 + (V % 3); S.StoreyH = 3.0f;
		S.Roof = (V == 5) ? ERoof::Hip : ERoof::GableY; S.Pitch = 50.f; S.Overhang = 0.3f; S.Chimneys = 1;
		S.bTimber = (V == 1 || V == 4); S.WallPat = (V == 2) ? EDmTownPat::Brick : (V == 3 ? EDmTownPat::Stone : EDmTownPat::Plaster);
		S.RoofPat = (V == 3) ? EDmTownPat::Slate : EDmTownPat::Tile; S.WinSpacing = 2.2f; S.WinH = 1.4f;
		I.H = GenHouse(M, S, Lod); I.W = S.W; I.D = S.D; I.WallPat = S.WallPat; I.RoofPat = S.RoofPat;
	}

	void Rowhouse(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		const int32 Units = 4 + V % 3;
		const float Uw = 5.5f;
		FHouse S; S.Seed = 81 + V; S.W = Units * Uw; S.D = 9.f; S.Storeys = 2 + (V >= 2 ? 1 : 0); S.StoreyH = 3.0f;
		S.Roof = (V == 3) ? ERoof::Flat : ERoof::Gable; S.Pitch = 35.f; S.Overhang = 0.25f; S.Chimneys = 0; S.Parapet = 0.8f;
		S.WallPat = EDmTownPat::Brick; S.RoofPat = EDmTownPat::Slate; S.WinSpacing = 1.9f; S.WinH = 1.5f; S.bCoolLight = true; S.bDoor = false;
		const float Top = GenHouse(M, S, Lod);
		FDmTownRand R(S.Seed);
		for (int32 K = 0; K < Units; ++K)
		{
			const float X = -S.W * 0.5f + (K + 0.5f) * Uw;
			M.Q4(SB, FVector3f(X - 0.5f, -S.D * 0.5f - 0.05f, 0), FVector3f(X + 0.5f, -S.D * 0.5f - 0.05f, 0), FVector3f(X + 0.5f, -S.D * 0.5f - 0.05f, 2.1f), FVector3f(X - 0.5f, -S.D * 0.5f - 0.05f, 2.1f), Fix(0.15f, 0.12f, 0.12f), P1(EDmTownPat::Wood), FVector3f(0, -1, 0));
			if (S.Roof != ERoof::Flat && K < Units - 1)
			{
				const float Xc = X + Uw * 0.5f;
				M.Box(SB, FVector3f(Xc, 0, Top - 0.6f), FVector3f(0.35f, 0.5f, 1.4f), 0.f, Fix(0.42f, 0.26f, 0.2f), P1(EDmTownPat::Brick));
			}
		}
		I.H = Top; I.W = S.W; I.D = S.D; I.WallPat = EDmTownPat::Brick; I.RoofPat = EDmTownPat::Slate;
	}

	void Apartment(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		FHouse S; S.Seed = 91 + V; S.W = 26.f + 6.f * (V % 2); S.D = 13.f; S.Storeys = 5 + 2 * V; S.StoreyH = 3.0f;
		S.Roof = ERoof::Flat; S.Parapet = 0.6f; S.WallPat = (V == 1) ? EDmTownPat::Brick : EDmTownPat::Concrete; S.RoofPat = EDmTownPat::Concrete;
		S.RoofCol = Fix(0.36f, 0.36f, 0.36f); S.WinSpacing = 2.4f; S.WinH = 1.5f; S.WinW = 1.4f; S.bCoolLight = true; S.bPlinth = false; S.bSills = V != 3;
		I.H = GenHouse(M, S, Lod); I.W = S.W; I.D = S.D; I.WallPat = S.WallPat; I.RoofPat = EDmTownPat::Concrete;
		if (Lod <= 1 && V >= 2)   // stair cores on the roof
			M.Box(SB, FVector3f(0, 0, I.H + 1.2f), FVector3f(2.f, 2.f, 1.4f), 0.f, WallC(), P1(S.WallPat));
	}

	void CurtainBox(FDmTownMesh& M, float W, float D, float Z0, float Z1, int32 Lod, FDmTownRand& R, bool bCool)
	{
		// glass curtain wall: a body box in curtain pattern plus window bands per storey (lit at night)
		const float hw = W * 0.5f, hd = D * 0.5f;
		M.Box(SB, FVector3f(0, 0, (Z0 + Z1) * 0.5f), FVector3f(hw, hd, (Z1 - Z0) * 0.5f), 0.f, ColT(0.55f, 0.62f, 0.68f, DmTownTint::Wall), P1(EDmTownPat::Curtain));
		const int32 Floors = FMath::Max(1, int32((Z1 - Z0) / 3.6f));
		const int32 Step = Lod == 0 ? 1 : (Lod == 1 ? 2 : 4);
		const FVector3f C[4] = { FVector3f(-hw, -hd, 0), FVector3f(hw, -hd, 0), FVector3f(hw, hd, 0), FVector3f(-hw, hd, 0) };
		for (int32 F = 0; F < 4; ++F)
		{
			const FVector3f A = C[F], B = C[(F + 1) % 4];
			const FVector3f U = (B - A).GetSafeNormal();
			FVector3f Nn = FVector3f::CrossProduct(U, Z3).GetSafeNormal();
			if (FVector3f::DotProduct(Nn, (A + B) * 0.5f) < 0.f) Nn = -Nn;
			const float L = (B - A).Size();
			const int32 Bays = FMath::Max(1, int32(L / (Lod == 0 ? 4.f : 8.f)));
			for (int32 Fl = 0; Fl < Floors; Fl += Step)
				for (int32 K = 0; K < Bays; ++K)
				{
					const FVector3f P = A + U * ((K + 0.5f) * L / Bays) + FVector3f(0, 0, Z0 + (Fl + 0.5f) * 3.6f);
					M.Window(P, U, Nn, L / Bays - 0.4f, 3.6f * Step - 0.9f, bCool ? LitCool(R) : (R.P(0.5f) ? LitCool(R) : LitWarm(R)), RB(R));
				}
		}
	}

	void Tower(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		FDmTownRand R(101u + V);
		const float W = 22.f + 4.f * (V % 3), H = 60.f + 30.f * V;
		I.W = I.D = W; I.WallPat = EDmTownPat::Curtain; I.RoofPat = EDmTownPat::Concrete;
		if (V == 2)       // cylinder tower
		{
			M.Cylinder(SB, FVector3f(0, 0, Foot), W * 0.5f, W * 0.46f, H - Foot, Lod == 0 ? 24 : 12, ColT(0.55f, 0.62f, 0.68f, DmTownTint::Wall), true, P1(EDmTownPat::Curtain));
			const int32 Floors = int32(H / 3.6f);
			for (int32 Fl = 0; Fl < Floors; Fl += (Lod == 0 ? 1 : 3))
				for (int32 K = 0; K < 12; ++K)
				{
					const float A = K * PI / 6.f; const float Rr = FMath::Lerp(W * 0.5f, W * 0.46f, Fl / float(Floors)) + 0.02f;
					M.Window(FVector3f(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, (Fl + 0.5f) * 3.6f), FVector3f(-FMath::Sin(A), FMath::Cos(A), 0), FVector3f(FMath::Cos(A), FMath::Sin(A), 0), Rr * 0.45f, 2.6f, LitCool(R), RB(R));
				}
			I.H = H;
		}
		else
		{
			CurtainBox(M, W, W, Foot, H * 0.7f, Lod, R, V == 0);
			if (V == 1 || V == 4)   // setbacks
			{
				CurtainBox(M, W * 0.75f, W * 0.75f, H * 0.7f, H * 0.9f, Lod, R, false);
				CurtainBox(M, W * 0.5f, W * 0.5f, H * 0.9f, H, Lod, R, false);
			}
			else CurtainBox(M, W * 0.92f, W * 0.92f, H * 0.7f, H, Lod, R, true);
			M.Box(SB, FVector3f(0, 0, H + 1.f), FVector3f(W * 0.2f, W * 0.2f, 1.f), 0.f, Fix(0.4f, 0.4f, 0.4f), P1(EDmTownPat::Concrete));
			I.H = H + 2.f;
			if (V == 4) { M.Cylinder(SB, FVector3f(0, 0, H + 2.f), 0.6f, 0.15f, 28.f, 6, Fix(0.6f, 0.6f, 0.62f), true, P1(EDmTownPat::Metal)); M.Window(FVector3f(0, 0.2f, H + 29.f), FVector3f(1, 0, 0), FVector3f(0, -1, 0), 0.6f, 0.6f, Col(1.f, 0.15f, 0.1f), 0); I.H = H + 30.f; }
		}
		// podium
		if (Lod <= 1) M.Box(SB, FVector3f(0, 0, 2.f), FVector3f(W * 0.62f, W * 0.62f, 5.f), 0.f, Fix(0.48f, 0.47f, 0.45f), P1(EDmTownPat::Concrete));
		I.W = I.D = W * 1.24f;
	}

	void Villa(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		FHouse S; S.Seed = 111 + V; S.W = 12.f + V; S.D = 9.f; S.Storeys = 1 + V % 2; S.StoreyH = 3.f;
		S.Roof = (V == 3) ? ERoof::Flat : ERoof::Hip; S.Pitch = 28.f; S.Overhang = 0.6f; S.Parapet = 0.3f; S.WinW = 1.6f; S.WinH = 1.4f; S.WinSpacing = 3.2f;
		S.WallPat = EDmTownPat::Paint; S.RoofPat = EDmTownPat::Tile; S.Chimneys = V == 0 ? 1 : 0;
		I.H = GenHouse(M, S, Lod); I.W = S.W; I.D = S.D; I.WallPat = S.WallPat; I.RoofPat = S.RoofPat;
		if (Lod <= 1)   // garage
		{
			FHouse G = S; G.W = 6.f; G.D = 6.f; G.Storeys = 1; G.Roof = ERoof::Flat; G.Chimneys = 0; G.bDoor = false; G.bWindows = false; G.Seed = S.Seed + 3;
			FDmTownMesh Tmp; GenHouse(Tmp, G, Lod);
			M.Append(Tmp, FTransform3f(FQuat4f::Identity, FVector3f(S.W * 0.5f + 3.f, 0.5f, 0)));
			I.W += 12.f;
		}
	}

	void Barn(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		FHouse S; S.Seed = 121 + V; S.W = 14.f + 3.f * V; S.D = 8.f + V; S.StoreyH = 4.5f; S.Roof = ERoof::Gable; S.Pitch = 45.f;
		S.WallPat = EDmTownPat::Wood; S.RoofPat = V == 2 ? EDmTownPat::Metal : EDmTownPat::Thatch; S.WinSpacing = 6.f; S.WinW = 2.8f; S.WinH = 3.f; S.bSills = false;
		I.H = GenHouse(M, S, Lod); I.W = S.W; I.D = S.D; I.WallPat = S.WallPat; I.RoofPat = S.RoofPat; I.bLit = false;
	}

	void Warehouse(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		FHouse S; S.Seed = 131 + V; S.W = 28.f + 6.f * V; S.D = 15.f; S.Storeys = 1 + (V == 1 ? 1 : 0); S.StoreyH = 5.f; S.Roof = V == 2 ? ERoof::Flat : ERoof::Gable; S.Pitch = 22.f;
		S.WallPat = V == 0 ? EDmTownPat::Wood : EDmTownPat::Brick; S.RoofPat = EDmTownPat::Metal; S.WinSpacing = 5.f; S.WinW = 1.6f; S.WinH = 1.f; S.bCoolLight = true;
		I.H = GenHouse(M, S, Lod); I.W = S.W; I.D = S.D; I.WallPat = S.WallPat; I.RoofPat = S.RoofPat;
	}

	void Stack(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		const float H = V == 0 ? 42.f : 60.f, R0 = V == 0 ? 1.8f : 2.4f;
		const int32 Sides = Lod == 0 ? 14 : 8;
		M.Cylinder(SB, FVector3f(0, 0, Foot), R0, R0 * 0.62f, H - Foot, Sides, ColT(0.58f, 0.34f, 0.26f, DmTownTint::Fixed), false, P1(EDmTownPat::Brick));
		M.Cylinder(SB, FVector3f(0, 0, H - 3.f), R0 * 0.68f, R0 * 0.66f, 3.f, Sides, Fix(0.12f, 0.11f, 0.11f), true, P1(EDmTownPat::Brick));
		M.Cylinder(SB, FVector3f(0, 0, Foot), R0 * 1.6f, R0 * 1.6f, 4.f - Foot, 4, Fix(0.5f, 0.32f, 0.25f), true, P1(EDmTownPat::Brick));
		M.Window(FVector3f(R0 * 0.67f, 0, H - 1.f), FVector3f(0, 1, 0), FVector3f(1, 0, 0), 0.4f, 0.4f, Col(1.f, 0.2f, 0.1f), 0);
		I.W = I.D = R0 * 3.2f; I.H = H; I.WallPat = EDmTownPat::Brick; I.RoofPat = EDmTownPat::Brick;
	}

	void Factory(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		FHouse S; S.Seed = 141 + V; S.W = 36.f + 8.f * V; S.D = 26.f; S.StoreyH = 7.f; S.Roof = ERoof::Sawtooth;
		S.WallPat = EDmTownPat::Brick; S.RoofPat = EDmTownPat::Metal; S.RoofCol = ColT(0.5f, 0.5f, 0.52f, DmTownTint::Roof); S.WinSpacing = 4.f; S.WinW = 2.2f; S.WinH = 3.f; S.bCoolLight = true; S.bDoor = false;
		GenHouse(M, S, Lod);
		FDmTownMesh St; FDmKitInfo Si; Stack(St, V == 2 ? 1 : 0, Lod, Si);
		M.Append(St, FTransform3f(FQuat4f::Identity, FVector3f(S.W * 0.5f - 4.f, S.D * 0.5f + 4.f, 0)));
		if (V >= 1) M.Append(St, FTransform3f(FQuat4f::Identity, FVector3f(S.W * 0.5f - 12.f, S.D * 0.5f + 4.f, 0)));
		I.W = S.W; I.D = S.D + 10.f; I.H = Si.H; I.WallPat = EDmTownPat::Brick; I.RoofPat = EDmTownPat::Metal;
	}

	// ------------------------------------------------------------------ civic

	void Campfire(FDmTownMesh& M, int32, int32 Lod, FDmKitInfo& I)
	{
		for (int32 K = 0; K < (Lod == 0 ? 9 : 5); ++K)
		{
			const float A = K * 2.f * PI / (Lod == 0 ? 9 : 5);
			M.Box(SB, FVector3f(FMath::Cos(A) * 1.1f, FMath::Sin(A) * 1.1f, 0.1f), FVector3f(0.25f, 0.2f, 0.2f), A, Fix(0.35f, 0.33f, 0.3f), P1(EDmTownPat::Stone));
		}
		M.Cone(SB, FVector3f(0, 0, -0.1f), 0.7f, 1.0f, 5, Fix(0.2f, 0.12f, 0.06f), P1(EDmTownPat::Wood));
		// flames (always on) and the warm pool around
		M.GlowDisc(FVector3f(0, 0, 0.6f), 1.2f, Col(1.f, 0.55f, 0.15f), true, 8);
		M.GlowDisc(FVector3f(0, 0, 0.08f), 9.f, Col(0.5f, 0.28f, 0.1f), false, 12);
		I.W = I.D = 3.f; I.H = 1.f; I.WallPat = EDmTownPat::Stone; I.bLit = true;
	}

	void Stones(FDmTownMesh& M, int32, int32 Lod, FDmKitInfo& I)
	{
		FDmTownRand R(151);
		const int32 N = Lod == 0 ? 12 : 8;
		for (int32 K = 0; K < N; ++K)
		{
			const float A = K * 2.f * PI / N;
			const float H = R.R(2.2f, 3.6f);
			M.Box(SB, FVector3f(FMath::Cos(A) * 7.f, FMath::Sin(A) * 7.f, H * 0.5f - 0.5f), FVector3f(0.45f, 0.7f, H * 0.5f + 0.5f), A + R.R(-0.15f, 0.15f), Fix(0.46f, 0.45f, 0.42f), P1(EDmTownPat::Stone));
		}
		M.Box(SB, FVector3f(0, 0, 0.3f), FVector3f(1.6f, 0.8f, 0.6f), 0.4f, Fix(0.42f, 0.41f, 0.38f), P1(EDmTownPat::Stone));
		I.W = I.D = 16.f; I.H = 3.6f; I.WallPat = EDmTownPat::Stone; I.bLit = false;
	}

	void Shrine(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		FHouse S; S.Seed = 161 + V; S.W = 5.f; S.D = 5.f; S.StoreyH = 3.2f; S.Roof = V == 0 ? ERoof::Hip : ERoof::Cone; S.Pitch = 45.f;
		S.WallPat = EDmTownPat::Stone; S.RoofPat = V == 0 ? EDmTownPat::Tile : EDmTownPat::Slate; S.WinSpacing = 4.f; S.Overhang = 0.5f;
		I.H = GenHouse(M, S, Lod);
		M.Box(SB, FVector3f(0, 0, -0.5f), FVector3f(3.6f, 3.6f, 0.7f), 0.f, Fix(0.55f, 0.53f, 0.49f), P1(EDmTownPat::Stone));
		M.GlowDisc(FVector3f(0, -4.f, 0.1f), 5.f, Col(0.55f, 0.36f, 0.14f));
		I.W = I.D = 7.2f; I.WallPat = EDmTownPat::Stone; I.RoofPat = S.RoofPat;
	}

	void TempleEarly(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		// stepped ziggurat with a sanctuary on top and a front stair
		const int32 Steps = V == 0 ? 4 : 6;
		const float B = V == 0 ? 26.f : 34.f, Hs = 3.4f;
		float Z = Foot;
		for (int32 K = 0; K < Steps; ++K)
		{
			const float H = (K == 0) ? Hs - Foot : Hs;
			const float Hw = B * 0.5f * (1.f - K * 0.14f);
			M.Box(SB, FVector3f(0, 0, Z + H * 0.5f), FVector3f(Hw, Hw, H * 0.5f), 0.f, ColT(0.74f, 0.64f, 0.48f, DmTownTint::Wall), P1(EDmTownPat::Mud));
			Z += H;
		}
		const float Top = Z;
		M.Box(SB, FVector3f(0, 0, Top + 2.f), FVector3f(3.5f, 3.f, 2.f), 0.f, ColT(0.82f, 0.74f, 0.58f, DmTownTint::Wall), P1(EDmTownPat::Plaster));
		M.Pyramid(SB, FVector3f(0, 0, Top + 4.f), 3.9f, 3.4f, 0.f, 1.6f, RoofC(), P1(EDmTownPat::Tile));
		M.Beam(SB, FVector3f(0, -B * 0.5f - 6.f, 0.f), FVector3f(0, -3.f, Top), 4.f, 0.6f, Fix(0.62f, 0.55f, 0.43f), P1(EDmTownPat::Stone));
		FDmTownRand R(171u + V);
		M.Window(FVector3f(0, -3.05f, Top + 1.4f), FVector3f(1, 0, 0), FVector3f(0, -1, 0), 1.4f, 2.4f, Col(1.f, 0.6f, 0.25f), RB(R));
		if (Lod == 0) M.GlowDisc(FVector3f(0, -B * 0.5f - 4.f, 0.1f), 8.f, Col(0.5f, 0.3f, 0.12f));
		I.W = I.D = B + 4.f; I.H = Top + 5.6f; I.WallPat = EDmTownPat::Mud; I.RoofPat = EDmTownPat::Tile;
	}

	void Columns(FDmTownMesh& M, float W, float D, float H, int32 Lod)
	{
		const int32 Nx = Lod == 0 ? 8 : 5, Ny = Lod == 0 ? 5 : 3;
		const int32 Sides = Lod == 0 ? 10 : 6;
		for (int32 X = 0; X < Nx; ++X)
			for (int32 Y = 0; Y < Ny; ++Y)
			{
				if (X != 0 && X != Nx - 1 && Y != 0 && Y != Ny - 1) continue;
				const float Px = -W * 0.5f + W * X / (Nx - 1), Py = -D * 0.5f + D * Y / (Ny - 1);
				M.Cylinder(SB, FVector3f(Px, Py, 0.8f), 0.55f, 0.48f, H, Sides, ColT(0.92f, 0.90f, 0.85f, DmTownTint::Wall), false, P1(EDmTownPat::Stone));
			}
	}

	void TempleClassic(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		const float W = V == 0 ? 30.f : 40.f, D = V == 0 ? 16.f : 22.f, H = V == 0 ? 9.f : 11.f;
		const FColor Marble = ColT(0.92f, 0.90f, 0.85f, DmTownTint::Wall);
		M.Box(SB, FVector3f(0, 0, (Foot + 0.8f) * 0.5f), FVector3f(W * 0.5f + 2.f, D * 0.5f + 2.f, (0.8f - Foot) * 0.5f), 0.f, Marble, P1(EDmTownPat::Stone));   // stylobate
		M.Box(SB, FVector3f(0, 0, 0.8f + H * 0.5f), FVector3f(W * 0.36f, D * 0.32f, H * 0.5f), 0.f, Marble, P1(EDmTownPat::Stone));                        // cella
		Columns(M, W, D, H, Lod);
		M.Box(SB, FVector3f(0, 0, 0.8f + H + 0.6f), FVector3f(W * 0.5f + 0.6f, D * 0.5f + 0.6f, 0.6f), 0.f, Marble, P1(EDmTownPat::Stone));                 // entablature
		FHouse S; S.W = W + 1.2f; S.D = D + 1.2f; S.Roof = ERoof::Gable; S.Pitch = 16.f; S.Overhang = 0.4f; S.RoofPat = EDmTownPat::Tile; S.Wall = Marble; S.WallPat = EDmTownPat::Stone;
		FDmTownMesh Tmp; GableRoof(Tmp, S, 0.f, Lod, false);
		M.Append(Tmp, FTransform3f(FQuat4f::Identity, FVector3f(0, 0, 0.8f + H + 1.2f)));
		if (V == 1 && Lod <= 1) { M.Cylinder(SB, FVector3f(0, 0, 0.8f + H + 1.2f), 7.f, 7.f, 3.f, 16, Marble, false, P1(EDmTownPat::Stone)); M.Dome(SB, FVector3f(0, 0, 0.8f + H + 4.2f), 7.f, 0.8f, 16, 5, RoofC(), P1(EDmTownPat::Metal)); }
		FDmTownRand R(181u + V);
		for (int32 K = -1; K <= 1; ++K) M.Window(FVector3f(K * 4.f, -D * 0.32f - 0.05f, 4.f), FVector3f(1, 0, 0), FVector3f(0, -1, 0), 1.6f, 4.f, Col(1.f, 0.62f, 0.3f), RB(R));
		M.GlowDisc(FVector3f(0, -D * 0.5f - 5.f, 0.1f), 9.f, Col(0.5f, 0.32f, 0.14f));
		I.W = W + 4.f; I.D = D + 4.f; I.H = 0.8f + H + 1.2f + D * 0.15f + (V == 1 ? 9.f : 0.f); I.WallPat = EDmTownPat::Stone; I.RoofPat = EDmTownPat::Tile;
	}

	void TempleLate(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		// cathedral: tall nave, transept, west tower(s) with spire
		const float L = V == 0 ? 44.f : 60.f, Nw = V == 0 ? 13.f : 16.f, Hn = V == 0 ? 16.f : 22.f;
		FHouse N; N.Seed = 191 + V; N.W = L; N.D = Nw; N.StoreyH = Hn; N.Roof = ERoof::Gable; N.Pitch = 55.f; N.WallPat = EDmTownPat::Stone; N.RoofPat = EDmTownPat::Slate;
		N.WinW = 1.6f; N.WinH = 6.f; N.WinSpacing = 5.f; N.bDoor = false;
		const float Ridge = GenHouse(M, N, Lod);
		FHouse T = N; T.W = Nw * 1.8f; T.D = Nw * 0.9f; T.Seed += 3; T.Roof = ERoof::GableY;
		FDmTownMesh Tmp; GenHouse(Tmp, T, Lod);
		M.Append(Tmp, FTransform3f(FQuat4f::Identity, FVector3f(L * 0.15f, 0, 0)));
		const float Tw = Nw * 0.8f, Th = Hn * 2.1f;
		for (int32 K = 0; K < (V == 1 ? 2 : 1); ++K)
		{
			const float Y = V == 1 ? (K == 0 ? -Nw * 0.38f : Nw * 0.38f) : 0.f;
			FHouse W2 = N; W2.W = Tw; W2.D = Tw; W2.StoreyH = Th; W2.Roof = ERoof::None; W2.WinH = 3.f; W2.WinSpacing = 3.f; W2.Seed += 5 + K;
			FDmTownMesh Tw2; GenHouse(Tw2, W2, Lod);
			Tw2.Pyramid(SB, FVector3f(0, 0, Th), Tw * 0.55f, Tw * 0.55f, 0.f, Tw * 2.6f, RoofC(), P1(EDmTownPat::Slate));
			M.Append(Tw2, FTransform3f(FQuat4f::Identity, FVector3f(-L * 0.5f - Tw * 0.5f + 0.5f, Y, 0)));
		}
		FDmTownRand R(197u + V);
		M.Window(FVector3f(-L * 0.5f - Tw - 0.1f, 0, 6.f), FVector3f(0, 1, 0), FVector3f(-1, 0, 0), 3.f, 7.f, Col(1.f, 0.7f, 0.4f), RB(R));
		M.GlowDisc(FVector3f(-L * 0.5f - Tw - 6.f, 0, 0.1f), 11.f, Col(0.5f, 0.34f, 0.16f));
		I.W = L + Tw + 2.f; I.D = Nw * 1.8f; I.H = FMath::Max(Ridge, Th + Tw * 2.6f); I.WallPat = EDmTownPat::Stone; I.RoofPat = EDmTownPat::Slate;
	}

	void Granary(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		const int32 Sides = Lod == 0 ? 14 : 8;
		if (V == 0)   // round granaries on a platform
		{
			for (int32 K = 0; K < 3; ++K)
			{
				const FVector3f C(-6.f + 6.f * K, 0, 0);
				M.Cylinder(SB, C + FVector3f(0, 0, Foot), 2.4f, 2.2f, 4.5f - Foot, Sides, WallC(), false, P1(EDmTownPat::Mud));
				M.Cone(SB, C + FVector3f(0, 0, 4.4f), 2.8f, 3.f, Sides, RoofC(), P1(EDmTownPat::Thatch));
			}
			I.W = 18.f; I.D = 6.f; I.H = 7.5f; I.WallPat = EDmTownPat::Mud; I.RoofPat = EDmTownPat::Thatch; I.bLit = false;
		}
		else          // tall silos + store
		{
			for (int32 K = 0; K < 4; ++K)
			{
				const FVector3f C(-7.5f + 5.f * K, 2.f, 0);
				M.Cylinder(SB, C + FVector3f(0, 0, Foot), 2.3f, 2.3f, 18.f - Foot, Sides, Fix(0.72f, 0.71f, 0.68f), false, P1(EDmTownPat::Concrete));
				M.Dome(SB, C + FVector3f(0, 0, 18.f), 2.3f, 0.4f, Sides, 3, Fix(0.6f, 0.6f, 0.6f), P1(EDmTownPat::Metal));
			}
			FHouse S; S.W = 20.f; S.D = 7.f; S.StoreyH = 6.f; S.Roof = ERoof::Gable; S.Pitch = 25.f; S.WallPat = EDmTownPat::Brick; S.RoofPat = EDmTownPat::Metal; S.WinSpacing = 6.f;
			FDmTownMesh Tmp; GenHouse(Tmp, S, Lod);
			M.Append(Tmp, FTransform3f(FQuat4f::Identity, FVector3f(0, -5.f, 0)));
			I.W = 22.f; I.D = 17.f; I.H = 19.f; I.WallPat = EDmTownPat::Brick; I.RoofPat = EDmTownPat::Metal;
		}
	}

	void Market(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		// a cluster of stalls with coloured awnings around a well
		FDmTownRand R(201u + V);
		static const FColor Aw[5] = { Col(0.62f, 0.16f, 0.12f), Col(0.84f, 0.62f, 0.18f), Col(0.20f, 0.36f, 0.56f), Col(0.86f, 0.84f, 0.78f), Col(0.30f, 0.46f, 0.24f) };
		const int32 N = V == 0 ? 8 : 12;
		for (int32 K = 0; K < N; ++K)
		{
			const float A = K * 2.f * PI / N + R.R(-0.1f, 0.1f);
			const float Rr = V == 0 ? 9.f : 12.f;
			const FVector3f C(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, 0);
			const float Yaw = A + PI * 0.5f;
			M.Box(SB, C + FVector3f(0, 0, 0.5f), FVector3f(1.6f, 0.9f, 0.9f), Yaw, Fix(0.42f, 0.3f, 0.2f), P1(EDmTownPat::Wood));
			FColor Ac = Aw[R.I(0, 4)]; Ac.A = DmTownTint::Fixed;
			FHouse S; S.W = 3.6f; S.D = 2.6f; S.Roof = ERoof::Gable; S.Pitch = 22.f; S.Overhang = 0.f; S.RoofCol = Ac; S.RoofPat = EDmTownPat::Hide;
			FDmTownMesh Tmp; GableRoof(Tmp, S, 0.f, 1, false);
			M.Append(Tmp, FTransform3f(FQuat4f(Z3, Yaw), C + FVector3f(0, 0, 2.3f)));
			if (Lod == 0) for (int32 P = -1; P <= 1; P += 2) M.Box(SB, C + FVector3f(FMath::Cos(Yaw) * 1.7f * P, FMath::Sin(Yaw) * 1.7f * P, 1.1f), FVector3f(0.06f, 0.06f, 1.2f), 0.f, Fix(0.3f, 0.22f, 0.15f), P1(EDmTownPat::Wood));
		}
		M.Cylinder(SB, FVector3f(0, 0, -0.5f), 1.3f, 1.3f, 1.4f, 10, Fix(0.5f, 0.48f, 0.44f), true, P1(EDmTownPat::Stone));
		M.GlowDisc(FVector3f(0, 0, 0.1f), 14.f, Col(0.45f, 0.30f, 0.14f));
		I.W = I.D = (V == 0 ? 22.f : 28.f); I.H = 3.f; I.WallPat = EDmTownPat::Wood; I.RoofPat = EDmTownPat::Hide;
	}

	void MarketHall(FDmTownMesh& M, int32, int32 Lod, FDmKitInfo& I)
	{
		FHouse S; S.Seed = 211; S.W = 40.f; S.D = 20.f; S.StoreyH = 8.f; S.Roof = ERoof::Gable; S.Pitch = 28.f; S.WallPat = EDmTownPat::Brick; S.RoofPat = EDmTownPat::Metal;
		S.WinW = 2.6f; S.WinH = 4.5f; S.WinSpacing = 4.f; S.bCoolLight = true;
		I.H = GenHouse(M, S, Lod);
		if (Lod <= 1) M.Box(SB, FVector3f(0, 0, I.H + 1.f), FVector3f(S.W * 0.4f, 1.2f, 1.f), 0.f, Fix(0.6f, 0.65f, 0.7f), P1(EDmTownPat::Curtain));
		I.W = S.W; I.D = S.D; I.WallPat = EDmTownPat::Brick; I.RoofPat = EDmTownPat::Metal;
	}

	void WallSeg(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		// one 16 m stretch along X (scaled by the planner to the real length), outer face toward -Y
		const float L = 8.f, T = V == 0 ? 1.4f : 1.6f, Hw = V == 0 ? 5.f : (V == 1 ? 8.f : 10.f);
		const EDmTownPat P = V == 0 ? EDmTownPat::Mud : EDmTownPat::Stone;
		const FColor C = V == 0 ? ColT(0.70f, 0.60f, 0.45f, DmTownTint::Wall) : ColT(0.62f, 0.60f, 0.56f, DmTownTint::Wall);
		M.Box(SB, FVector3f(0, 0, (Foot - 2.f + Hw) * 0.5f), FVector3f(L, T, (Hw - Foot + 2.f) * 0.5f), 0.f, C, P1(P));
		const int32 N = Lod == 0 ? 9 : (Lod == 1 ? 5 : 0);
		for (int32 K = 0; K < N; ++K)
			M.Box(SB, FVector3f(-L + (K + 0.5f) * 2.f * L / N, -T + 0.3f, Hw + 0.45f), FVector3f(0.5f, 0.3f, 0.45f), 0.f, C, P1(P));
		if (Lod <= 1) M.Box(SB, FVector3f(0, T - 0.2f, Hw + 0.4f), FVector3f(L, 0.2f, 0.4f), 0.f, C, P1(P));
		I.W = 2.f * L; I.D = 2.f * T; I.H = Hw + 0.9f; I.WallPat = P; I.RoofPat = P; I.bLit = false;
	}

	void WallTower(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		const EDmTownPat P = V == 0 ? EDmTownPat::Mud : EDmTownPat::Stone;
		const FColor C = V == 0 ? ColT(0.70f, 0.60f, 0.45f, DmTownTint::Wall) : ColT(0.62f, 0.60f, 0.56f, DmTownTint::Wall);
		FDmTownRand R(221u + V);
		if (V == 2)   // round tower with a conical roof
		{
			const float R0 = 4.f, H = 15.f; const int32 Sides = Lod == 0 ? 18 : 10;
			M.Cylinder(SB, FVector3f(0, 0, Foot - 2.f), R0 * 1.05f, R0, H - Foot + 2.f, Sides, C, false, P1(P));
			M.Cylinder(SB, FVector3f(0, 0, H - 0.6f), R0 + 0.35f, R0 + 0.35f, 0.6f, Sides, C, true, P1(P));
			M.Cone(SB, FVector3f(0, 0, H), R0 + 0.7f, R0 * 1.9f, Sides, RoofC(), P1(EDmTownPat::Slate));
			for (int32 K = 0; K < 4; ++K) { const float A = K * PI * 0.5f + 0.4f; M.Window(FVector3f(FMath::Cos(A) * (R0 + 0.05f), FMath::Sin(A) * (R0 + 0.05f), H * 0.6f), FVector3f(-FMath::Sin(A), FMath::Cos(A), 0), FVector3f(FMath::Cos(A), FMath::Sin(A), 0), 0.4f, 1.2f, Col(1.f, 0.6f, 0.3f), RB(R)); }
			I.W = I.D = 2.f * R0 + 1.4f; I.H = H + R0 * 1.9f;
		}
		else          // square tower with merlons
		{
			const float Hw = V == 0 ? 4.f : 5.f, H = V == 0 ? 8.f : 13.f;
			M.Box(SB, FVector3f(0, 0, (Foot - 2.f + H) * 0.5f), FVector3f(Hw, Hw, (H - Foot + 2.f) * 0.5f), 0.f, C, P1(P));
			if (Lod <= 1) for (int32 K = 0; K < 4; ++K) for (int32 J = 0; J < 3; ++J)
			{
				const float A = K * PI * 0.5f; const FVector3f D(FMath::Cos(A), FMath::Sin(A), 0), U(-FMath::Sin(A), FMath::Cos(A), 0);
				M.Box(SB, D * (Hw - 0.3f) + U * ((J - 1) * Hw * 0.62f) + FVector3f(0, 0, H + 0.5f), FVector3f(0.5f, 0.5f, 0.5f), A, C, P1(P));
			}
			M.Window(FVector3f(0, -Hw - 0.05f, H * 0.65f), FVector3f(1, 0, 0), FVector3f(0, -1, 0), 0.5f, 1.3f, Col(1.f, 0.6f, 0.3f), RB(R));
			I.W = I.D = 2.f * Hw; I.H = H + 1.f;
		}
		I.WallPat = P; I.RoofPat = EDmTownPat::Slate;
	}

	void Gatehouse(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		// two towers framing an arch, the road passes along Y
		const EDmTownPat P = V == 0 ? EDmTownPat::Mud : EDmTownPat::Stone;
		const FColor C = V == 0 ? ColT(0.70f, 0.60f, 0.45f, DmTownTint::Wall) : ColT(0.62f, 0.60f, 0.56f, DmTownTint::Wall);
		const float H = V == 0 ? 9.f : 14.f, Tw = 4.5f, Gap = 4.f;
		for (int32 S = -1; S <= 1; S += 2)
		{
			M.Box(SB, FVector3f(S * (Gap + Tw), 0, (Foot + H) * 0.5f), FVector3f(Tw, Tw, (H - Foot) * 0.5f), 0.f, C, P1(P));
			if (V == 1) M.Pyramid(SB, FVector3f(S * (Gap + Tw), 0, H), Tw + 0.4f, Tw + 0.4f, 0.f, 6.f, RoofC(), P1(EDmTownPat::Slate));
		}
		M.Box(SB, FVector3f(0, 0, H - 2.f), FVector3f(Gap, Tw * 0.8f, 2.f), 0.f, C, P1(P));
		FDmTownRand R(231u + V);
		for (int32 S = -1; S <= 1; S += 2) M.Window(FVector3f(S * (Gap + Tw), -Tw - 0.05f, H * 0.6f), FVector3f(1, 0, 0), FVector3f(0, -1, 0), 0.6f, 1.3f, Col(1.f, 0.6f, 0.3f), RB(R));
		M.GlowDisc(FVector3f(0, -Tw - 4.f, 0.1f), 7.f, Col(0.55f, 0.34f, 0.14f));
		M.GlowDisc(FVector3f(0, Tw + 4.f, 0.1f), 7.f, Col(0.55f, 0.34f, 0.14f));
		I.W = 2.f * (Gap + 2.f * Tw); I.D = 2.f * Tw; I.H = H + (V == 1 ? 6.f : 0.f); I.WallPat = P; I.RoofPat = EDmTownPat::Slate;
	}

	void Pier(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		// 20 m unit along +X from the shore (x = 0) out to sea (x = 20); scaled by the planner
		const float L = 20.f, Hw = V == 0 ? 2.5f : 7.f;
		const FColor Deck = V == 0 ? Fix(0.40f, 0.31f, 0.22f) : Fix(0.50f, 0.49f, 0.46f);
		const EDmTownPat P = V == 0 ? EDmTownPat::Wood : EDmTownPat::Concrete;
		M.Box(SB, FVector3f(L * 0.5f, 0, 1.2f), FVector3f(L * 0.5f + 1.f, Hw, 0.35f), 0.f, Deck, P1(P), true);
		const int32 Np = Lod == 0 ? 4 : 2;
		for (int32 K = 0; K < Np; ++K)
			for (int32 S = -1; S <= 1; S += 2)
				M.Cylinder(SB, FVector3f(2.f + K * (L - 2.f) / FMath::Max(1, Np - 1), S * (Hw - 0.4f), -8.f), 0.3f, 0.28f, 9.f, 6, Fix(0.28f, 0.25f, 0.22f), false, P1(P));
		I.W = L; I.D = 2.f * Hw; I.H = 2.f; I.WallPat = P; I.RoofPat = P; I.bLit = false;
	}

	void Lighthouse(FDmTownMesh& M, int32, int32 Lod, FDmKitInfo& I)
	{
		const int32 Sides = Lod == 0 ? 16 : 8;
		M.Cylinder(SB, FVector3f(0, 0, Foot), 3.2f, 2.2f, 26.f - Foot, Sides, Fix(0.88f, 0.87f, 0.84f), false, P1(EDmTownPat::Plaster));
		if (Lod <= 1) for (int32 K = 0; K < 3; ++K) M.Cylinder(SB, FVector3f(0, 0, 5.f + K * 7.f), 3.05f - K * 0.33f, 2.95f - K * 0.33f, 2.5f, Sides, Fix(0.62f, 0.12f, 0.10f), false, P1(EDmTownPat::Paint));
		M.Cylinder(SB, FVector3f(0, 0, 26.f), 2.8f, 2.8f, 0.5f, Sides, Fix(0.2f, 0.2f, 0.2f), true, P1(EDmTownPat::Metal));
		for (int32 K = 0; K < 6; ++K) { const float A = K * PI / 3.f; M.Window(FVector3f(FMath::Cos(A) * 1.75f, FMath::Sin(A) * 1.75f, 28.f), FVector3f(-FMath::Sin(A), FMath::Cos(A), 0), FVector3f(FMath::Cos(A), FMath::Sin(A), 0), 1.6f, 2.f, Col(1.f, 0.92f, 0.7f), 0); }
		M.Cone(SB, FVector3f(0, 0, 29.2f), 2.2f, 2.f, Sides, Fix(0.15f, 0.15f, 0.15f), P1(EDmTownPat::Metal));
		I.W = I.D = 7.f; I.H = 31.f; I.WallPat = EDmTownPat::Plaster; I.RoofPat = EDmTownPat::Metal;
	}

	void Crane(FDmTownMesh& M, int32, int32 Lod, FDmKitInfo& I)
	{
		const FColor Cc = Fix(0.72f, 0.32f, 0.12f);
		const float H = 16.f;
		for (int32 K = 0; K < 4; ++K)
		{
			const float X = (K & 1) ? 4.f : -4.f, Y = (K & 2) ? 4.f : -4.f;
			M.Box(SB, FVector3f(X, Y, H * 0.5f), FVector3f(0.3f, 0.3f, H * 0.5f), 0.f, Cc, P1(EDmTownPat::Metal));
			if (Lod == 0) M.Beam(SB, FVector3f(X, Y, 1.f), FVector3f(X * 0.2f, Y * 0.2f, H - 1.f), 0.2f, 0.2f, Cc, P1(EDmTownPat::Metal));
		}
		M.Box(SB, FVector3f(0, 0, H + 2.f), FVector3f(3.5f, 3.f, 2.f), 0.f, Cc, P1(EDmTownPat::Metal));
		M.Beam(SB, FVector3f(-8.f, 0, H + 4.f), FVector3f(30.f, 0, H + 3.2f), 1.2f, 1.6f, Cc, P1(EDmTownPat::Metal));
		M.Beam(SB, FVector3f(0, 0, H + 4.f), FVector3f(0, 0, H + 10.f), 0.6f, 0.6f, Cc, P1(EDmTownPat::Metal));
		M.Window(FVector3f(0, 0.4f, H + 10.4f), FVector3f(1, 0, 0), FVector3f(0, -1, 0), 0.4f, 0.4f, Col(1.f, 0.2f, 0.1f), 0);
		I.W = 12.f; I.D = 10.f; I.H = H + 10.f; I.WallPat = EDmTownPat::Metal; I.RoofPat = EDmTownPat::Metal;
	}

	void MineHead(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		if (V == 0)   // adit entrance with timber frame and sheds
		{
			M.Box(SB, FVector3f(0, 0, 1.5f), FVector3f(2.f, 0.3f, 1.6f), 0.f, Fix(0.3f, 0.22f, 0.14f), P1(EDmTownPat::Wood));
			M.Box(SB, FVector3f(0, -0.4f, 1.2f), FVector3f(1.3f, 0.2f, 1.2f), 0.f, Fix(0.05f, 0.05f, 0.05f), P1(EDmTownPat::Stone));
			FHouse S; S.W = 8.f; S.D = 5.f; S.StoreyH = 3.f; S.WallPat = EDmTownPat::Wood; S.RoofPat = EDmTownPat::Wood; S.Seed = 241;
			FDmTownMesh Tmp; GenHouse(Tmp, S, Lod);
			M.Append(Tmp, FTransform3f(FQuat4f::Identity, FVector3f(9.f, -3.f, 0)));
			I.W = 22.f; I.D = 10.f; I.H = 6.f;
		}
		else          // headframe tower and winding house
		{
			const FColor Steel = Fix(0.30f, 0.30f, 0.32f);
			for (int32 K = 0; K < 4; ++K) { const float X = (K & 1) ? 3.f : -3.f, Y = (K & 2) ? 3.f : -3.f; M.Beam(SB, FVector3f(X, Y, 0), FVector3f(X * 0.4f, Y * 0.4f, 26.f), 0.5f, 0.5f, Steel, P1(EDmTownPat::Metal)); }
			M.Beam(SB, FVector3f(-1.5f, 0, 26.f), FVector3f(-14.f, 0, 0.f), 0.6f, 0.6f, Steel, P1(EDmTownPat::Metal));
			M.Cylinder(SB, FVector3f(0, 0, 26.f), 2.f, 2.f, 0.6f, 12, Steel, true, P1(EDmTownPat::Metal));
			FHouse S; S.W = 14.f; S.D = 9.f; S.StoreyH = 7.f; S.Roof = ERoof::Gable; S.Pitch = 25.f; S.WallPat = EDmTownPat::Brick; S.RoofPat = EDmTownPat::Metal; S.Seed = 243;
			FDmTownMesh Tmp; GenHouse(Tmp, S, Lod);
			M.Append(Tmp, FTransform3f(FQuat4f::Identity, FVector3f(-20.f, 0, 0)));
			I.W = 36.f; I.D = 12.f; I.H = 27.f;
		}
		I.WallPat = EDmTownPat::Wood; I.RoofPat = EDmTownPat::Metal;
	}

	void Spoil(FDmTownMesh& M, int32, int32 Lod, FDmKitInfo& I)
	{
		M.Cone(SB, FVector3f(0, 0, -2.f), 22.f, 16.f, Lod == 0 ? 16 : 8, Fix(0.26f, 0.24f, 0.22f), P1(EDmTownPat::Stone));
		I.W = I.D = 44.f; I.H = 14.f; I.WallPat = EDmTownPat::Stone; I.bLit = false;
	}

	void Academy(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		FHouse S; S.Seed = 251 + V; S.W = V == 0 ? 34.f : 50.f; S.D = 18.f; S.Storeys = V == 0 ? 2 : 4; S.StoreyH = 4.f; S.Roof = V == 0 ? ERoof::Hip : ERoof::Flat;
		S.Pitch = 26.f; S.WallPat = V == 0 ? EDmTownPat::Stone : EDmTownPat::Concrete; S.RoofPat = V == 0 ? EDmTownPat::Slate : EDmTownPat::Concrete; S.WinSpacing = 2.8f; S.WinH = 2.f; S.bCoolLight = V == 1;
		const float Top = GenHouse(M, S, Lod);
		// observatory dome / lecture rotunda
		const FVector3f Dc(S.W * 0.5f + 7.f, 0, 0);
		M.Cylinder(SB, Dc + FVector3f(0, 0, Foot), 7.f, 7.f, 9.f - Foot, Lod == 0 ? 20 : 10, Fix(0.85f, 0.84f, 0.8f), false, P1(EDmTownPat::Stone));
		M.Dome(SB, Dc + FVector3f(0, 0, 9.f), 7.3f, 1.f, Lod == 0 ? 20 : 10, Lod == 0 ? 6 : 3, ColT(0.55f, 0.62f, 0.58f, DmTownTint::Fixed), P1(EDmTownPat::Metal));
		I.W = S.W + 28.f; I.D = S.D; I.H = FMath::Max(Top, 16.3f); I.WallPat = S.WallPat; I.RoofPat = S.RoofPat;
	}

	void CoolingTower(FDmTownMesh& M, int32, int32 Lod, FDmKitInfo& I)
	{
		// hyperboloid: stacked frustums
		const int32 Sides = Lod == 0 ? 28 : 14;
		const float Rs[6] = { 30.f, 25.f, 21.f, 19.f, 19.5f, 21.f };
		const float Zs[6] = { -3.f, 20.f, 45.f, 70.f, 88.f, 100.f };
		for (int32 K = 0; K < 5; ++K) M.Cylinder(SB, FVector3f(0, 0, Zs[K]), Rs[K], Rs[K + 1], Zs[K + 1] - Zs[K], Sides, Fix(0.70f, 0.69f, 0.66f), false, P1(EDmTownPat::Concrete));
		M.Window(FVector3f(0, -30.1f, 6.f), FVector3f(1, 0, 0), FVector3f(0, -1, 0), 1.f, 1.f, Col(1.f, 0.2f, 0.1f), 0);
		I.W = I.D = 62.f; I.H = 100.f; I.WallPat = EDmTownPat::Concrete; I.RoofPat = EDmTownPat::Concrete;
	}

	void Reactor(FDmTownMesh& M, int32, int32 Lod, FDmKitInfo& I)
	{
		const int32 Sides = Lod == 0 ? 24 : 12;
		M.Cylinder(SB, FVector3f(0, 0, Foot), 20.f, 20.f, 30.f - Foot, Sides, Fix(0.74f, 0.73f, 0.70f), false, P1(EDmTownPat::Concrete));
		M.Dome(SB, FVector3f(0, 0, 30.f), 20.f, 0.55f, Sides, Lod == 0 ? 6 : 3, Fix(0.78f, 0.77f, 0.74f), P1(EDmTownPat::Concrete));
		FHouse S; S.W = 50.f; S.D = 24.f; S.Storeys = 4; S.StoreyH = 5.f; S.Roof = ERoof::Flat; S.WallPat = EDmTownPat::Concrete; S.RoofPat = EDmTownPat::Concrete; S.bCoolLight = true; S.Seed = 261;
		FDmTownMesh Tmp; GenHouse(Tmp, S, Lod);
		M.Append(Tmp, FTransform3f(FQuat4f::Identity, FVector3f(0, -34.f, 0)));
		I.W = 52.f; I.D = 94.f; I.H = 41.f; I.WallPat = EDmTownPat::Concrete; I.RoofPat = EDmTownPat::Concrete;
	}

	void Gantry(FDmTownMesh& M, int32, int32 Lod, FDmKitInfo& I)
	{
		const FColor Steel = Fix(0.55f, 0.20f, 0.14f);
		M.Box(SB, FVector3f(0, 0, 0.f), FVector3f(40.f, 40.f, 1.5f), 0.f, Fix(0.55f, 0.55f, 0.53f), P1(EDmTownPat::Concrete));
		const float H = 90.f;
		for (int32 K = 0; K < 4; ++K) { const float X = 16.f + ((K & 1) ? 6.f : -6.f), Y = (K & 2) ? 6.f : -6.f; M.Box(SB, FVector3f(X, Y, H * 0.5f), FVector3f(0.6f, 0.6f, H * 0.5f), 0.f, Steel, P1(EDmTownPat::Metal)); }
		const int32 Lv = Lod == 0 ? 12 : 5;
		for (int32 K = 0; K < Lv; ++K) M.Box(SB, FVector3f(16.f, 0, (K + 1) * H / (Lv + 1)), FVector3f(6.6f, 6.6f, 0.4f), 0.f, Steel, P1(EDmTownPat::Metal));
		for (int32 K = 0; K < 3; ++K) M.Box(SB, FVector3f(9.f, 0, 30.f + K * 22.f), FVector3f(1.f, 1.f, 0.4f), 0.f, Steel, P1(EDmTownPat::Metal));
		M.Window(FVector3f(16.f, -6.7f, H), FVector3f(1, 0, 0), FVector3f(0, -1, 0), 1.f, 1.f, Col(1.f, 0.2f, 0.1f), 0);
		M.GlowDisc(FVector3f(0, 0, 1.6f), 40.f, Col(0.6f, 0.62f, 0.7f));
		I.W = I.D = 80.f; I.H = H; I.WallPat = EDmTownPat::Metal; I.RoofPat = EDmTownPat::Concrete;
	}

	void Rocket(FDmTownMesh& M, int32, int32 Lod, FDmKitInfo& I)
	{
		// 80 m launcher at the origin (the planner scales its height by the ark's progress)
		const int32 Sides = Lod == 0 ? 20 : 10;
		const FColor W = Fix(0.90f, 0.90f, 0.88f), B = Fix(0.12f, 0.12f, 0.13f);
		M.Cylinder(SB, FVector3f(0, 0, 2.f), 4.5f, 4.5f, 55.f, Sides, W, false, P1(EDmTownPat::Metal));
		M.Cylinder(SB, FVector3f(0, 0, 57.f), 4.5f, 3.6f, 10.f, Sides, B, false, P1(EDmTownPat::Metal));
		M.Cone(SB, FVector3f(0, 0, 67.f), 3.6f, 13.f, Sides, W, P1(EDmTownPat::Metal));
		for (int32 K = 0; K < 4; ++K) { const float A = K * PI * 0.5f; M.Cylinder(SB, FVector3f(FMath::Cos(A) * 6.2f, FMath::Sin(A) * 6.2f, 2.f), 1.7f, 1.7f, 30.f, 10, W, false, P1(EDmTownPat::Metal)); M.Cone(SB, FVector3f(FMath::Cos(A) * 6.2f, FMath::Sin(A) * 6.2f, 32.f), 1.7f, 5.f, 10, W, P1(EDmTownPat::Metal)); }
		I.W = I.D = 16.f; I.H = 80.f; I.WallPat = EDmTownPat::Metal; I.RoofPat = EDmTownPat::Metal; I.bLit = false;
	}

	void Wonder(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		FDmTownRand R(271u + V);
		const int32 Sides = Lod == 0 ? 24 : 12;
		switch (V)
		{
		case 0:   // great pyramid
			M.Pyramid(SB, FVector3f(0, 0, Foot), 70.f, 70.f, 0.f, 95.f, Fix(0.80f, 0.72f, 0.56f), P1(EDmTownPat::Stone));
			M.Pyramid(SB, FVector3f(0, 0, 84.f), 8.4f, 8.4f, 0.f, 11.f, Fix(0.85f, 0.72f, 0.35f), P1(EDmTownPat::Metal));
			I.W = I.D = 140.f; I.H = 92.f; break;
		case 1:   // colossus on a plinth
			M.Box(SB, FVector3f(0, 0, (Foot + 12.f) * 0.5f), FVector3f(14.f, 14.f, (12.f - Foot) * 0.5f), 0.f, Fix(0.75f, 0.73f, 0.68f), P1(EDmTownPat::Stone));
			M.Cylinder(SB, FVector3f(-3.f, 0, 12.f), 3.2f, 2.8f, 22.f, 10, Fix(0.55f, 0.42f, 0.25f), false, P1(EDmTownPat::Metal));
			M.Cylinder(SB, FVector3f(3.f, 0, 12.f), 3.2f, 2.8f, 22.f, 10, Fix(0.55f, 0.42f, 0.25f), false, P1(EDmTownPat::Metal));
			M.Cylinder(SB, FVector3f(0, 0, 33.f), 7.f, 6.f, 24.f, 12, Fix(0.55f, 0.42f, 0.25f), true, P1(EDmTownPat::Metal));
			M.Dome(SB, FVector3f(0, 0, 57.f), 4.5f, 1.2f, 12, 4, Fix(0.55f, 0.42f, 0.25f), P1(EDmTownPat::Metal));
			M.Beam(SB, FVector3f(6.f, 0, 52.f), FVector3f(9.f, 0, 72.f), 1.6f, 1.6f, Fix(0.55f, 0.42f, 0.25f), P1(EDmTownPat::Metal));
			M.Window(FVector3f(9.f, -1.f, 73.f), FVector3f(1, 0, 0), FVector3f(0, -1, 0), 2.f, 2.f, Col(1.f, 0.7f, 0.3f), 0);
			M.GlowDisc(FVector3f(0, 0, 0.2f), 30.f, Col(0.55f, 0.35f, 0.15f));
			I.W = I.D = 30.f; I.H = 74.f; break;
		case 2:   // great domed basilica
			M.Box(SB, FVector3f(0, 0, (Foot + 24.f) * 0.5f), FVector3f(36.f, 36.f, (24.f - Foot) * 0.5f), 0.f, Fix(0.88f, 0.85f, 0.78f), P1(EDmTownPat::Stone));
			M.Cylinder(SB, FVector3f(0, 0, 24.f), 24.f, 24.f, 12.f, Sides, Fix(0.88f, 0.85f, 0.78f), false, P1(EDmTownPat::Stone));
			M.Dome(SB, FVector3f(0, 0, 36.f), 25.f, 1.f, Sides, Lod == 0 ? 8 : 4, Fix(0.42f, 0.55f, 0.50f), P1(EDmTownPat::Metal));
			M.Cylinder(SB, FVector3f(0, 0, 60.f), 3.f, 3.f, 8.f, 10, Fix(0.88f, 0.85f, 0.78f), true, P1(EDmTownPat::Stone));
			for (int32 K = 0; K < 4; ++K) { const float A = K * PI * 0.5f + PI * 0.25f; M.Cylinder(SB, FVector3f(FMath::Cos(A) * 46.f, FMath::Sin(A) * 46.f, Foot), 3.f, 2.4f, 60.f, 10, Fix(0.88f, 0.85f, 0.78f), true, P1(EDmTownPat::Stone)); M.Cone(SB, FVector3f(FMath::Cos(A) * 46.f, FMath::Sin(A) * 46.f, 57.f), 2.6f, 8.f, 10, Fix(0.42f, 0.55f, 0.50f), P1(EDmTownPat::Metal)); }
			for (int32 K = 0; K < 16; ++K) { const float A = K * PI / 8.f; M.Window(FVector3f(FMath::Cos(A) * 24.05f, FMath::Sin(A) * 24.05f, 30.f), FVector3f(-FMath::Sin(A), FMath::Cos(A), 0), FVector3f(FMath::Cos(A), FMath::Sin(A), 0), 2.f, 6.f, Col(1.f, 0.7f, 0.4f), RB(R)); }
			I.W = I.D = 100.f; I.H = 68.f; break;
		case 3:   // iron lattice tower
		{
			const FColor Iron = Fix(0.38f, 0.30f, 0.24f);
			const int32 Lv = Lod == 0 ? 10 : 5;
			for (int32 K = 0; K < 4; ++K)
			{
				const float A = K * PI * 0.5f + PI * 0.25f;
				const FVector3f D(FMath::Cos(A), FMath::Sin(A), 0);
				M.Beam(SB, D * 34.f, D * 10.f + FVector3f(0, 0, 60.f), 2.2f, 2.2f, Iron, P1(EDmTownPat::Metal));
				M.Beam(SB, D * 10.f + FVector3f(0, 0, 60.f), D * 2.f + FVector3f(0, 0, 150.f), 1.4f, 1.4f, Iron, P1(EDmTownPat::Metal));
			}
			for (int32 K = 1; K <= Lv; ++K) { const float Z = K * 150.f / (Lv + 1); const float Hw = FMath::Lerp(24.f, 2.f, Z / 150.f); M.Box(SB, FVector3f(0, 0, Z), FVector3f(Hw, Hw, 0.6f), 0.f, Iron, P1(EDmTownPat::Metal)); }
			M.Cylinder(SB, FVector3f(0, 0, 150.f), 1.2f, 0.2f, 20.f, 6, Iron, true, P1(EDmTownPat::Metal));
			for (int32 K = 0; K < 8; ++K) { const float A = K * PI * 0.25f; M.Window(FVector3f(FMath::Cos(A) * 12.f, FMath::Sin(A) * 12.f, 60.f + 1.f), FVector3f(-FMath::Sin(A), FMath::Cos(A), 0), FVector3f(FMath::Cos(A), FMath::Sin(A), 0), 5.f, 1.2f, Col(1.f, 0.8f, 0.5f), 0); }
			M.GlowDisc(FVector3f(0, 0, 0.2f), 45.f, Col(0.6f, 0.45f, 0.25f));
			I.W = I.D = 70.f; I.H = 170.f; break;
		}
		default:  // space needle with a ring
			M.Cylinder(SB, FVector3f(0, 0, Foot), 6.f, 3.f, 180.f - Foot, Sides, Fix(0.85f, 0.86f, 0.88f), false, P1(EDmTownPat::Concrete));
			M.Cylinder(SB, FVector3f(0, 0, 150.f), 24.f, 26.f, 10.f, Sides, Fix(0.75f, 0.78f, 0.82f), true, P1(EDmTownPat::Metal));
			for (int32 K = 0; K < 16; ++K) { const float A = K * PI / 8.f; M.Window(FVector3f(FMath::Cos(A) * 25.1f, FMath::Sin(A) * 25.1f, 155.f), FVector3f(-FMath::Sin(A), FMath::Cos(A), 0), FVector3f(FMath::Cos(A), FMath::Sin(A), 0), 8.f, 6.f, LitCool(R), 0); }
			M.Cylinder(SB, FVector3f(0, 0, 180.f), 1.f, 0.1f, 40.f, 6, Fix(0.8f, 0.8f, 0.82f), true, P1(EDmTownPat::Metal));
			M.Window(FVector3f(0, -0.6f, 219.f), FVector3f(1, 0, 0), FVector3f(0, -1, 0), 1.2f, 1.2f, Col(1.f, 0.2f, 0.1f), 0);
			M.GlowDisc(FVector3f(0, 0, 0.2f), 40.f, Col(0.45f, 0.55f, 0.7f));
			I.W = I.D = 52.f; I.H = 220.f; break;
		}
		I.WallPat = EDmTownPat::Stone; I.RoofPat = EDmTownPat::Metal;
	}

	void Ruin(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		// broken walls of a 10 x 8 m house (scaled to the footprint by the planner), no roof
		FDmTownRand R(281u + V);
		const float hw = 5.f, hd = 4.f, T = 0.35f;
		const FColor C = ColT(0.45f, 0.42f, 0.38f, DmTownTint::Wall);
		const FVector3f Cn[4] = { FVector3f(-hw, -hd, 0), FVector3f(hw, -hd, 0), FVector3f(hw, hd, 0), FVector3f(-hw, hd, 0) };
		for (int32 F = 0; F < 4; ++F)
		{
			const FVector3f A = Cn[F], B = Cn[(F + 1) % 4];
			const int32 Pieces = Lod == 0 ? 4 : 2;
			for (int32 K = 0; K < Pieces; ++K)
			{
				if (R.P(0.25f)) continue;
				const FVector3f P0 = FMath::Lerp(A, B, float(K) / Pieces), P1v = FMath::Lerp(A, B, float(K + 1) / Pieces);
				const float H = R.R(0.6f, V == 2 ? 5.f : 3.2f);
				const FVector3f Mid = (P0 + P1v) * 0.5f, U = (P1v - P0);
				M.Box(SB, Mid + FVector3f(0, 0, (H + Foot) * 0.5f), FVector3f(U.Size() * 0.5f - 0.1f, T, (H - Foot) * 0.5f), FMath::Atan2(U.Y, U.X), C, P1(EDmTownPat::Stone));
			}
		}
		if (Lod == 0) for (int32 K = 0; K < 5; ++K) M.Box(SB, FVector3f(R.R(-hw, hw), R.R(-hd, hd), 0.2f), FVector3f(R.R(0.3f, 0.8f), R.R(0.3f, 0.8f), 0.3f), R.R(0.f, 3.f), Mul(C, 0.85f), P1(EDmTownPat::Stone));
		I.W = 2.f * hw; I.D = 2.f * hd; I.H = 4.f; I.WallPat = EDmTownPat::Stone; I.bLit = false;
	}

	void Scaffold(FDmTownMesh& M, int32, int32 Lod, FDmKitInfo& I)
	{
		// unit 10 x 8 x 6 m frame (scaled to the footprint and height): poles, ledgers and a half-built core
		const FColor Wd = Fix(0.55f, 0.42f, 0.26f);
		const float hw = 5.f, hd = 4.f, H = 6.f;
		M.Box(SB, FVector3f(0, 0, (Foot + H * 0.45f) * 0.5f), FVector3f(hw - 0.6f, hd - 0.6f, (H * 0.45f - Foot) * 0.5f), 0.f, ColT(0.80f, 0.76f, 0.68f, DmTownTint::Wall), P1(EDmTownPat::Stone));
		const int32 Nx = Lod == 0 ? 4 : 2;
		for (int32 X = 0; X <= Nx; ++X)
			for (int32 S = -1; S <= 1; S += 2)
			{
				M.Box(SB, FVector3f(-hw + 2.f * hw * X / Nx, S * hd, H * 0.5f), FVector3f(0.08f, 0.08f, H * 0.5f), 0.f, Wd, P1(EDmTownPat::Wood));
				M.Box(SB, FVector3f(S * hw, -hd + 2.f * hd * X / Nx, H * 0.5f), FVector3f(0.08f, 0.08f, H * 0.5f), 0.f, Wd, P1(EDmTownPat::Wood));
			}
		for (int32 L = 1; L <= 3; ++L)
		{
			const float Z = L * H / 3.f;
			M.Box(SB, FVector3f(0, -hd, Z), FVector3f(hw, 0.07f, 0.07f), 0.f, Wd, P1(EDmTownPat::Wood));
			M.Box(SB, FVector3f(0, hd, Z), FVector3f(hw, 0.07f, 0.07f), 0.f, Wd, P1(EDmTownPat::Wood));
			M.Box(SB, FVector3f(-hw, 0, Z), FVector3f(0.07f, hd, 0.07f), 0.f, Wd, P1(EDmTownPat::Wood));
			M.Box(SB, FVector3f(hw, 0, Z), FVector3f(0.07f, hd, 0.07f), 0.f, Wd, P1(EDmTownPat::Wood));
		}
		I.W = 2.f * hw; I.D = 2.f * hd; I.H = H; I.WallPat = EDmTownPat::Stone; I.bLit = false;
	}

	void Windmill(FDmTownMesh& M, int32, int32 Lod, FDmKitInfo& I)
	{
		const int32 Sides = Lod == 0 ? 12 : 8;
		M.Cylinder(SB, FVector3f(0, 0, Foot), 4.f, 2.8f, 14.f - Foot, Sides, WallC(), false, P1(EDmTownPat::Plaster));
		M.Cone(SB, FVector3f(0, 0, 14.f), 3.2f, 3.5f, Sides, RoofC(), P1(EDmTownPat::Thatch));
		const FVector3f Hub(0, -3.4f, 14.f);
		for (int32 K = 0; K < 4; ++K)
		{
			const float A = K * PI * 0.5f + 0.4f;
			const FVector3f D(FMath::Cos(A), 0, FMath::Sin(A));
			M.Box(SB, Hub + D * 5.5f, FVector3f(0.3f, 0.1f, 0.3f), 0.f, Fix(0.3f, 0.22f, 0.15f), P1(EDmTownPat::Wood));
			const FVector3f Side = FVector3f(-D.Z, 0, D.X) * 0.9f;
			M.Q4(SB, Hub + D * 1.f, Hub + D * 10.5f, Hub + D * 10.5f + Side, Hub + D * 1.f + Side, Fix(0.85f, 0.82f, 0.74f), P1(EDmTownPat::Hide), FVector3f(0, -1, 0));
			M.Q4(SB, Hub + D * 1.f + FVector3f(0, 0.05f, 0), Hub + D * 10.5f + FVector3f(0, 0.05f, 0), Hub + D * 10.5f + Side + FVector3f(0, 0.05f, 0), Hub + D * 1.f + Side + FVector3f(0, 0.05f, 0), Fix(0.85f, 0.82f, 0.74f), P1(EDmTownPat::Hide), FVector3f(0, 1, 0));
		}
		FDmTownRand R(291);
		M.Window(FVector3f(0, -3.9f, 6.f), FVector3f(1, 0, 0), FVector3f(0, -1, 0), 0.7f, 1.f, Col(1.f, 0.6f, 0.3f), RB(R));
		I.W = I.D = 9.f; I.H = 24.f; I.WallPat = EDmTownPat::Plaster; I.RoofPat = EDmTownPat::Thatch;
	}

	// ------------------------------------------------------------------ boats and ships (waterline at z = 0, bow toward +X)

	void Hull(FDmTownMesh& M, float L, float B, float Zb, float Zd, const FColor& HullC, const FColor& DeckC, EDmTownPat P)
	{
		const float l = L * 0.5f, b = B * 0.5f;
		const TArray<FVector2f> Plan = { { -l, -b * 0.8f }, { l * 0.45f, -b }, { l * 0.85f, -b * 0.55f }, { l, 0 }, { l * 0.85f, b * 0.55f }, { l * 0.45f, b }, { -l, b * 0.8f } };
		for (int32 I = 0; I < Plan.Num(); ++I)
		{
			const FVector2f A = Plan[I], Bp = Plan[(I + 1) % Plan.Num()];
			const FVector3f N = FVector3f(Bp.Y - A.Y, -(Bp.X - A.X), 0).GetSafeNormal();
			M.Q4(SB, FVector3f(A.X * 0.9f, A.Y * 0.6f, Zb), FVector3f(Bp.X * 0.9f, Bp.Y * 0.6f, Zb), FVector3f(Bp.X, Bp.Y, Zd), FVector3f(A.X, A.Y, Zd), HullC, P1(P), N + FVector3f(0, 0, -0.3f));
		}
		const int32 C0 = M.Add(FVector3f(0, 0, Zd), Z3, FVector2f(0, 0), DeckC, P1(EDmTownPat::Wood));
		for (int32 I = 0; I < Plan.Num(); ++I)
		{
			const FVector2f A = Plan[I], Bp = Plan[(I + 1) % Plan.Num()];
			const int32 a = M.Add(FVector3f(A.X, A.Y, Zd), Z3, FVector2f(A.X, A.Y), DeckC, P1(EDmTownPat::Wood));
			const int32 b2 = M.Add(FVector3f(Bp.X, Bp.Y, Zd), Z3, FVector2f(Bp.X, Bp.Y), DeckC, P1(EDmTownPat::Wood));
			M.Tri(SB, C0, a, b2, Z3);
		}
	}

	void Sail(FDmTownMesh& M, const FVector3f& Base, float W, float H, const FColor& C)
	{
		M.Q4(SB, Base + FVector3f(0, -W * 0.5f, 0), Base + FVector3f(0, W * 0.5f, 0), Base + FVector3f(0.6f, W * 0.45f, H), Base + FVector3f(0.6f, -W * 0.45f, H), C, P1(EDmTownPat::Hide), FVector3f(1, 0, 0));
		M.Q4(SB, Base + FVector3f(-0.05f, -W * 0.5f, 0), Base + FVector3f(-0.05f, W * 0.5f, 0), Base + FVector3f(0.55f, W * 0.45f, H), Base + FVector3f(0.55f, -W * 0.45f, H), C, P1(EDmTownPat::Hide), FVector3f(-1, 0, 0));
	}

	void Boat(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		Hull(M, V == 0 ? 7.f : 10.f, V == 0 ? 1.6f : 3.f, -0.4f, 0.6f, Fix(0.32f, 0.22f, 0.14f), Fix(0.45f, 0.34f, 0.22f), EDmTownPat::Wood);
		if (V == 1)
		{
			M.Cylinder(SB, FVector3f(0.5f, 0, 0.6f), 0.08f, 0.06f, 7.f, 5, Fix(0.3f, 0.22f, 0.14f), true, P1(EDmTownPat::Wood));
			Sail(M, FVector3f(0.5f, 0, 1.5f), 3.6f, 5.f, ColT(0.88f, 0.84f, 0.74f, DmTownTint::Roof));
		}
		I.W = V == 0 ? 7.f : 10.f; I.D = 3.f; I.H = V == 0 ? 1.f : 7.6f; I.WallPat = EDmTownPat::Wood; I.RoofPat = EDmTownPat::Hide; I.bLit = false;
	}

	void Galley(FDmTownMesh& M, int32, int32 Lod, FDmKitInfo& I)
	{
		Hull(M, 28.f, 5.f, -1.2f, 1.4f, Fix(0.30f, 0.20f, 0.12f), Fix(0.48f, 0.36f, 0.22f), EDmTownPat::Wood);
		if (Lod <= 1) for (int32 K = 0; K < (Lod == 0 ? 12 : 6); ++K) for (int32 S = -1; S <= 1; S += 2)
			M.Beam(SB, FVector3f(-10.f + K * (20.f / (Lod == 0 ? 12 : 6)), S * 2.2f, 1.2f), FVector3f(-10.5f + K * (20.f / (Lod == 0 ? 12 : 6)), S * 6.f, -0.2f), 0.12f, 0.12f, Fix(0.4f, 0.3f, 0.2f), P1(EDmTownPat::Wood));
		M.Cylinder(SB, FVector3f(1.f, 0, 1.4f), 0.2f, 0.15f, 12.f, 6, Fix(0.3f, 0.22f, 0.14f), true, P1(EDmTownPat::Wood));
		Sail(M, FVector3f(1.f, 0, 4.f), 9.f, 8.f, ColT(0.80f, 0.70f, 0.55f, DmTownTint::Roof));
		I.W = 28.f; I.D = 12.f; I.H = 13.f; I.WallPat = EDmTownPat::Wood; I.RoofPat = EDmTownPat::Hide; I.bLit = false;
	}

	void SailShip(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		const float L = V == 0 ? 34.f : 48.f;
		Hull(M, L, L * 0.26f, -3.f, 3.f, V == 0 ? Fix(0.22f, 0.16f, 0.11f) : Fix(0.12f, 0.12f, 0.13f), Fix(0.50f, 0.40f, 0.28f), EDmTownPat::Wood);
		const int32 Masts = V == 0 ? 2 : 3;
		for (int32 K = 0; K < Masts; ++K)
		{
			const float X = -L * 0.3f + K * L * 0.6f / FMath::Max(1, Masts - 1);
			const float H = (K == Masts / 2) ? L * 0.95f : L * 0.8f;
			M.Cylinder(SB, FVector3f(X, 0, 3.f), 0.35f, 0.2f, H, 6, Fix(0.3f, 0.22f, 0.14f), true, P1(EDmTownPat::Wood));
			const int32 Ns = Lod == 0 ? 3 : 2;
			for (int32 S = 0; S < Ns; ++S) Sail(M, FVector3f(X, 0, 6.f + S * H * 0.28f), L * 0.36f * (1.f - S * 0.18f), H * 0.24f, ColT(0.90f, 0.87f, 0.78f, DmTownTint::Roof));
		}
		FDmTownRand R(301u + V);
		for (int32 K = 0; K < 3; ++K) M.Window(FVector3f(-L * 0.5f - 0.02f, (K - 1) * 1.6f, 2.f), FVector3f(0, 1, 0), FVector3f(-1, 0, 0), 0.8f, 0.8f, Col(1.f, 0.65f, 0.3f), RB(R));
		I.W = L; I.D = L * 0.4f; I.H = L * 0.95f + 3.f; I.WallPat = EDmTownPat::Wood; I.RoofPat = EDmTownPat::Hide;
	}

	void Steamer(FDmTownMesh& M, int32, int32 Lod, FDmKitInfo& I)
	{
		const float L = 70.f;
		Hull(M, L, 11.f, -4.f, 4.f, Fix(0.10f, 0.10f, 0.11f), Fix(0.45f, 0.40f, 0.34f), EDmTownPat::Metal);
		FHouse S; S.W = 24.f; S.D = 8.f; S.Storeys = 2; S.StoreyH = 2.8f; S.Roof = ERoof::Flat; S.Parapet = 0.f; S.Wall = Fix(0.9f, 0.9f, 0.87f); S.WallPat = EDmTownPat::Paint; S.RoofCol = Fix(0.6f, 0.6f, 0.6f); S.bPlinth = false; S.bSills = false; S.WinSpacing = 2.f; S.WinW = 0.8f; S.WinH = 0.8f; S.bDoor = false; S.Seed = 311;
		FDmTownMesh Tmp; GenHouse(Tmp, S, Lod);
		M.Append(Tmp, FTransform3f(FQuat4f::Identity, FVector3f(-4.f, 0, 4.f)));
		for (int32 K = 0; K < 2; ++K) { M.Cylinder(SB, FVector3f(-8.f + K * 9.f, 0, 9.6f), 1.8f, 1.7f, 9.f, 10, Fix(0.15f, 0.15f, 0.15f), true, P1(EDmTownPat::Metal)); M.Cylinder(SB, FVector3f(-8.f + K * 9.f, 0, 15.6f), 1.75f, 1.75f, 1.5f, 10, Fix(0.7f, 0.15f, 0.1f), false, P1(EDmTownPat::Paint)); }
		I.W = L; I.D = 12.f; I.H = 19.f; I.WallPat = EDmTownPat::Paint; I.RoofPat = EDmTownPat::Metal;
	}

	void CargoShip(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		const float L = V == 0 ? 120.f : 180.f, B = L * 0.15f;
		Hull(M, L, B, -6.f, 5.f, V == 0 ? Fix(0.35f, 0.10f, 0.08f) : Fix(0.12f, 0.14f, 0.20f), Fix(0.38f, 0.38f, 0.36f), EDmTownPat::Metal);
		FHouse S; S.W = 12.f; S.D = B * 0.8f; S.Storeys = 4; S.StoreyH = 2.8f; S.Roof = ERoof::Flat; S.Parapet = 0.f; S.Wall = Fix(0.9f, 0.9f, 0.87f); S.WallPat = EDmTownPat::Paint; S.RoofCol = Fix(0.6f, 0.6f, 0.6f); S.bPlinth = false; S.bSills = false; S.WinSpacing = 2.f; S.WinW = 1.f; S.WinH = 0.9f; S.bDoor = false; S.Seed = 321 + V; S.bCoolLight = true;
		FDmTownMesh Tmp; GenHouse(Tmp, S, Lod);
		M.Append(Tmp, FTransform3f(FQuat4f::Identity, FVector3f(-L * 0.5f + 12.f, 0, 5.f)));
		FDmTownRand R(331u + V);
		static const FColor Cc[5] = { Col(0.55f, 0.18f, 0.12f), Col(0.18f, 0.30f, 0.45f), Col(0.60f, 0.55f, 0.45f), Col(0.22f, 0.42f, 0.24f), Col(0.75f, 0.62f, 0.2f) };
		const float Step = Lod == 0 ? 6.5f : 13.f;
		for (float X = -L * 0.5f + 22.f; X < L * 0.5f - 18.f; X += Step)
		{
			const int32 Hn = R.I(1, Lod == 0 ? 4 : 2);
			for (int32 K = 0; K < Hn; ++K) { FColor C = Cc[R.I(0, 4)]; C.A = DmTownTint::Fixed; M.Box(SB, FVector3f(X, 0, 5.f + 1.3f + K * 2.6f), FVector3f(Step * 0.46f, B * 0.38f, 1.3f), 0.f, C, P1(EDmTownPat::Metal)); }
		}
		I.W = L; I.D = B; I.H = 18.f; I.WallPat = EDmTownPat::Metal; I.RoofPat = EDmTownPat::Metal;
	}

	// ------------------------------------------------------------------ movers (drawn at an exaggerated scale by the units layer)

	void Settlers(FDmTownMesh& M, int32, int32 Lod, FDmKitInfo& I)
	{
		// covered wagon, oxen and a few walkers
		M.Box(SB, FVector3f(0, 0, 1.2f), FVector3f(2.2f, 1.f, 0.4f), 0.f, Fix(0.40f, 0.30f, 0.2f), P1(EDmTownPat::Wood));
		M.Dome(SB, FVector3f(0, 0, 1.6f), 1.f, 1.3f, 8, 3, ColT(0.88f, 0.84f, 0.74f, DmTownTint::Roof), P1(EDmTownPat::Hide));
		FDmTownMesh Tmp; Tmp.Dome(SB, FVector3f(0, 0, 1.6f), 1.f, 1.3f, 8, 3, ColT(0.88f, 0.84f, 0.74f, DmTownTint::Roof), P1(EDmTownPat::Hide));
		M.Append(Tmp, FTransform3f(FQuat4f::Identity, FVector3f(1.2f, 0, 0))); M.Append(Tmp, FTransform3f(FQuat4f::Identity, FVector3f(-1.2f, 0, 0)));
		for (int32 K = 0; K < 2; ++K) M.Box(SB, FVector3f(4.f, (K - 0.5f) * 1.f, 0.9f), FVector3f(1.f, 0.35f, 0.5f), 0.f, Fix(0.35f, 0.25f, 0.18f), P1(EDmTownPat::Hide));
		for (int32 K = 0; K < 4; ++K) M.Cylinder(SB, FVector3f(-3.5f - K * 1.3f, (K % 2) * 1.2f - 0.6f, 0.f), 0.25f, 0.2f, 1.7f, 5, ColT(0.6f, 0.5f, 0.4f, DmTownTint::Wall), true, P1(EDmTownPat::Hide));
		I.W = 12.f; I.D = 3.f; I.H = 3.f; I.bLit = false;
	}

	void Caravan(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		if (V == 0)   // pack animals in a line
		{
			for (int32 K = 0; K < 5; ++K)
			{
				const float X = -K * 3.2f;
				M.Box(SB, FVector3f(X, 0, 1.5f), FVector3f(1.2f, 0.4f, 0.45f), 0.f, Fix(0.55f, 0.44f, 0.3f), P1(EDmTownPat::Hide));
				M.Box(SB, FVector3f(X + 1.4f, 0, 2.f), FVector3f(0.35f, 0.25f, 0.5f), 0.f, Fix(0.55f, 0.44f, 0.3f), P1(EDmTownPat::Hide));
				M.Box(SB, FVector3f(X, 0, 2.1f), FVector3f(0.6f, 0.6f, 0.35f), 0.f, ColT(0.7f, 0.3f, 0.2f, DmTownTint::Roof), P1(EDmTownPat::Hide));
				for (int32 L = 0; L < 4; ++L) M.Box(SB, FVector3f(X + ((L & 1) ? 0.8f : -0.8f), (L & 2) ? 0.25f : -0.25f, 0.55f), FVector3f(0.1f, 0.1f, 0.55f), 0.f, Fix(0.45f, 0.36f, 0.25f), P1(EDmTownPat::Hide));
			}
			I.W = 16.f; I.D = 2.f; I.H = 2.6f;
		}
		else          // lorries
		{
			for (int32 K = 0; K < 3; ++K)
			{
				const float X = -K * 9.f;
				M.Box(SB, FVector3f(X + 3.f, 0, 1.6f), FVector3f(1.f, 1.2f, 1.1f), 0.f, Fix(0.75f, 0.74f, 0.70f), P1(EDmTownPat::Paint));
				M.Box(SB, FVector3f(X - 1.f, 0, 2.0f), FVector3f(3.f, 1.25f, 1.5f), 0.f, ColT(0.6f, 0.3f, 0.2f, DmTownTint::Roof), P1(EDmTownPat::Metal));
				M.Window(FVector3f(X + 4.02f, 0, 2.f), FVector3f(0, 1, 0), FVector3f(1, 0, 0), 1.8f, 0.4f, Col(1.f, 0.95f, 0.8f), 0);
			}
			I.W = 26.f; I.D = 2.6f; I.H = 3.6f;
		}
	}

	void Army(FDmTownMesh& M, int32 V, int32 Lod, FDmKitInfo& I)
	{
		if (V == 0)   // a block of soldiers with spears and a banner
		{
			const int32 N = Lod == 0 ? 5 : 3;
			for (int32 X = 0; X < N; ++X) for (int32 Y = 0; Y < N; ++Y)
			{
				const FVector3f P(-X * 1.4f, (Y - N * 0.5f) * 1.2f, 0);
				M.Cylinder(SB, P, 0.3f, 0.25f, 1.75f, 5, ColT(0.6f, 0.55f, 0.5f, DmTownTint::Roof), true, P1(EDmTownPat::Hide));
				if (Lod == 0) M.Box(SB, P + FVector3f(0.2f, 0.25f, 1.6f), FVector3f(0.03f, 0.03f, 1.2f), 0.f, Fix(0.4f, 0.35f, 0.3f), P1(EDmTownPat::Wood));
			}
			M.Box(SB, FVector3f(1.5f, 0, 2.5f), FVector3f(0.05f, 0.05f, 2.5f), 0.f, Fix(0.3f, 0.22f, 0.14f), P1(EDmTownPat::Wood));
			M.Q4(SB, FVector3f(1.5f, 0, 4.8f), FVector3f(1.5f, 1.6f, 4.8f), FVector3f(1.5f, 1.6f, 3.8f), FVector3f(1.5f, 0, 3.8f), ColT(0.8f, 0.8f, 0.8f, DmTownTint::Roof), P1(EDmTownPat::Hide), FVector3f(1, 0, 0));
			M.Q4(SB, FVector3f(1.48f, 0, 4.8f), FVector3f(1.48f, 1.6f, 4.8f), FVector3f(1.48f, 1.6f, 3.8f), FVector3f(1.48f, 0, 3.8f), ColT(0.8f, 0.8f, 0.8f, DmTownTint::Roof), P1(EDmTownPat::Hide), FVector3f(-1, 0, 0));
			I.W = 8.f; I.D = 7.f; I.H = 5.f;
		}
		else          // tanks
		{
			for (int32 K = 0; K < 3; ++K)
			{
				const FVector3f P(-K * 12.f, (K % 2) * 5.f - 2.5f, 0);
				M.Box(SB, P + FVector3f(0, 0, 1.f), FVector3f(3.5f, 1.8f, 0.8f), 0.f, ColT(0.35f, 0.38f, 0.30f, DmTownTint::Roof), P1(EDmTownPat::Metal));
				M.Box(SB, P + FVector3f(-0.4f, 0, 2.2f), FVector3f(1.5f, 1.2f, 0.5f), 0.f, ColT(0.35f, 0.38f, 0.30f, DmTownTint::Roof), P1(EDmTownPat::Metal));
				M.Beam(SB, P + FVector3f(1.f, 0, 2.3f), P + FVector3f(5.5f, 0, 2.4f), 0.25f, 0.25f, Fix(0.25f, 0.27f, 0.22f), P1(EDmTownPat::Metal));
			}
			I.W = 30.f; I.D = 9.f; I.H = 3.f;
		}
		I.bLit = false;
	}

	void Prophet(FDmTownMesh& M, int32, int32 Lod, FDmKitInfo& I)
	{
		M.Cone(SB, FVector3f(0, 0, 0), 0.6f, 1.6f, 8, Fix(0.92f, 0.90f, 0.84f), P1(EDmTownPat::Hide));
		M.Dome(SB, FVector3f(0, 0, 1.5f), 0.25f, 1.2f, 8, 3, Fix(0.75f, 0.6f, 0.45f), P1(EDmTownPat::Hide));
		M.Box(SB, FVector3f(0.4f, 0, 1.f), FVector3f(0.03f, 0.03f, 1.f), 0.f, Fix(0.35f, 0.25f, 0.15f), P1(EDmTownPat::Wood));
		for (int32 K = 0; K < 6; ++K) { const float A = K * PI / 3.f; M.Cylinder(SB, FVector3f(-1.6f + FMath::Cos(A) * 0.9f, FMath::Sin(A) * 1.2f, 0), 0.22f, 0.18f, 1.6f, 5, ColT(0.6f, 0.55f, 0.5f, DmTownTint::Roof), true, P1(EDmTownPat::Hide)); }
		M.GlowDisc(FVector3f(0, 0, 0.1f), 4.f, Col(0.9f, 0.8f, 0.5f), true);
		I.W = 4.f; I.D = 3.f; I.H = 2.f;
	}
}

// ====================================================================== factory

int32 UDmTownsFactory::NumVariants(EDmTownMesh Id)
{
	static const int32 V[] = {
#define DM_TOWN_VARS(N, Vn) Vn,
		DM_TOWN_MESHES(DM_TOWN_VARS)
#undef DM_TOWN_VARS
	};
	return (int32(Id) < int32(EDmTownMesh::Num)) ? V[int32(Id)] : 0;
}

const TCHAR* UDmTownsFactory::Name(EDmTownMesh Id)
{
	static const TCHAR* N[] = {
#define DM_TOWN_NAMES(Nm, Vn) TEXT(#Nm),
		DM_TOWN_MESHES(DM_TOWN_NAMES)
#undef DM_TOWN_NAMES
	};
	return (int32(Id) < int32(EDmTownMesh::Num)) ? N[int32(Id)] : TEXT("?");
}

void UDmTownsFactory::Generate(EDmTownMesh Id, int32 V, int32 Lod, FDmTownMesh& M, FDmKitInfo& I)
{
	V = FMath::Clamp(V, 0, NumVariants(Id) - 1);
	switch (Id)
	{
	case EDmTownMesh::Tent: Tent(M, V, Lod, I); break;
	case EDmTownMesh::Hut: Hut(M, V, Lod, I); break;
	case EDmTownMesh::Longhouse: Longhouse(M, V, Lod, I); break;
	case EDmTownMesh::MudHouse: MudHouse(M, V, Lod, I); break;
	case EDmTownMesh::Cottage: Cottage(M, V, Lod, I); break;
	case EDmTownMesh::Townhouse: Townhouse(M, V, Lod, I); break;
	case EDmTownMesh::Rowhouse: Rowhouse(M, V, Lod, I); break;
	case EDmTownMesh::Apartment: Apartment(M, V, Lod, I); break;
	case EDmTownMesh::Tower: Tower(M, V, Lod, I); break;
	case EDmTownMesh::Villa: Villa(M, V, Lod, I); break;
	case EDmTownMesh::Barn: Barn(M, V, Lod, I); break;
	case EDmTownMesh::Warehouse: Warehouse(M, V, Lod, I); break;
	case EDmTownMesh::Factory: Factory(M, V, Lod, I); break;
	case EDmTownMesh::Stack: Stack(M, V, Lod, I); break;
	case EDmTownMesh::Campfire: Campfire(M, V, Lod, I); break;
	case EDmTownMesh::Stones: Stones(M, V, Lod, I); break;
	case EDmTownMesh::Shrine: Shrine(M, V, Lod, I); break;
	case EDmTownMesh::TempleEarly: TempleEarly(M, V, Lod, I); break;
	case EDmTownMesh::TempleClassic: TempleClassic(M, V, Lod, I); break;
	case EDmTownMesh::TempleLate: TempleLate(M, V, Lod, I); break;
	case EDmTownMesh::Granary: Granary(M, V, Lod, I); break;
	case EDmTownMesh::Market: Market(M, V, Lod, I); break;
	case EDmTownMesh::MarketHall: MarketHall(M, V, Lod, I); break;
	case EDmTownMesh::WallSeg: WallSeg(M, V, Lod, I); break;
	case EDmTownMesh::WallTower: WallTower(M, V, Lod, I); break;
	case EDmTownMesh::Gatehouse: Gatehouse(M, V, Lod, I); break;
	case EDmTownMesh::Pier: Pier(M, V, Lod, I); break;
	case EDmTownMesh::Lighthouse: Lighthouse(M, V, Lod, I); break;
	case EDmTownMesh::Crane: Crane(M, V, Lod, I); break;
	case EDmTownMesh::MineHead: MineHead(M, V, Lod, I); break;
	case EDmTownMesh::Spoil: Spoil(M, V, Lod, I); break;
	case EDmTownMesh::Academy: Academy(M, V, Lod, I); break;
	case EDmTownMesh::CoolingTower: CoolingTower(M, V, Lod, I); break;
	case EDmTownMesh::Reactor: Reactor(M, V, Lod, I); break;
	case EDmTownMesh::Gantry: Gantry(M, V, Lod, I); break;
	case EDmTownMesh::Rocket: Rocket(M, V, Lod, I); break;
	case EDmTownMesh::Wonder: Wonder(M, V, Lod, I); break;
	case EDmTownMesh::Ruin: Ruin(M, V, Lod, I); break;
	case EDmTownMesh::Scaffold: Scaffold(M, V, Lod, I); break;
	case EDmTownMesh::Windmill: Windmill(M, V, Lod, I); break;
	case EDmTownMesh::Boat: Boat(M, V, Lod, I); break;
	case EDmTownMesh::Galley: Galley(M, V, Lod, I); break;
	case EDmTownMesh::SailShip: SailShip(M, V, Lod, I); break;
	case EDmTownMesh::Steamer: Steamer(M, V, Lod, I); break;
	case EDmTownMesh::CargoShip: CargoShip(M, V, Lod, I); break;
	case EDmTownMesh::Settlers: Settlers(M, V, Lod, I); break;
	case EDmTownMesh::Caravan: Caravan(M, V, Lod, I); break;
	case EDmTownMesh::Army: Army(M, V, Lod, I); break;
	case EDmTownMesh::Prophet: Prophet(M, V, Lod, I); break;
	default: M.Box(SB, FVector3f(0, 0, 2.f), FVector3f(2.f, 2.f, 2.f), 0.f, WallC(), P1(EDmTownPat::Plaster)); break;
	}
	M.SetHeightFraction(0.f, FMath::Max(I.H, 1.f));
}

UMaterialInstanceDynamic* UDmTownsFactory::LoadMat(const TCHAR* MatName)
{
	const FString Path = FString::Printf(TEXT("/Game/Towns/%s.%s"), MatName, MatName);
	UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *Path);
	if (!M)
	{
		UE_LOG(LogDmTownsKit, Error, TEXT("DmTowns: missing material %s (run Scripts/towns.py)"), *Path);
		bMissing = true;
		M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
		if (!M) return nullptr;
	}
	return UMaterialInstanceDynamic::Create(M, this);
}

void UDmTownsFactory::Build(UWorld* InWorld)
{
	if (bBuilt) return;
	World = InWorld;
	const double T0 = FPlatformTime::Seconds();
	// (wave 4 towns) the real assets first: loaded once here (world load), never mid-game
	Assets = MakeShared<FDmTownsAssets>();
	Assets->Build(AssetKeep);
	if (Assets->IsEnabled() && FPackageName::DoesPackageExist(TEXT("/Game/Assets/Materials/MPC_DmAssets")))
		AssetMPC = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/Assets/Materials/MPC_DmAssets.MPC_DmAssets"));
	MatBody = LoadMat(TEXT("M_TownBody"));
	MatGlass = LoadMat(TEXT("M_TownGlass"));
	MatGlow = LoadMat(TEXT("M_TownGlow"));
	MatGround = LoadMat(TEXT("M_TownGround"));
	const TArray<UMaterialInterface*> Mats = { MatBody.Get(), MatGlass.Get(), MatGlow.Get() };
	// LOD screen sizes: a house is only ~0.007 of the screen at the game's closest zoom (1.2 km up),
	// so the full detail must stay on down to small sizes; the far LOD is massing + windows
	const TArray<float> Sizes = { 1.f, 0.006f, 0.0018f };
	TotalTris = 0;
	for (int32 K = 0; K < int32(EDmTownMesh::Num); ++K)
	{
		const EDmTownMesh Id = EDmTownMesh(K);
		const int32 Nv = NumVariants(Id);
		Kit[K].SetNum(Nv);
		for (int32 V = 0; V < Nv; ++V)
		{
			FDmTownMesh L0, L1, L2;
			FDmKitInfo Info, Tmp1, Tmp2;
			Generate(Id, V, 0, L0, Info);
			Generate(Id, V, 1, L1, Tmp1);
			Generate(Id, V, 2, L2, Tmp2);
			const bool bMover = Id >= EDmTownMesh::Settlers;
			Info.Mesh = DmTowns::MakeStaticMesh(this, FString::Printf(TEXT("SM_Town_%s_%d"), Name(Id), V), { &L0, &L1, &L2 }, Mats, Sizes, !bMover);
			Info.Tris = L0.NumTris();
			TotalTris += Info.Tris;
			Meshes.Add(Info.Mesh);
			Kit[K][V] = Info;
		}
	}
	bBuilt = true;
	BuildSeconds = FPlatformTime::Seconds() - T0;
	UE_LOG(LogDmTownsKit, Display, TEXT("DmTowns: factory built %d meshes in %.2f s (%d LOD0 triangles)%s"), Meshes.Num(), BuildSeconds, TotalTris, bMissing ? TEXT(" MATERIALS MISSING") : TEXT(""));
}

const FDmKitInfo& UDmTownsFactory::Get(EDmTownMesh Id, int32 Variant) const
{
	static const FDmKitInfo None;
	const int32 K = int32(Id);
	if (K < 0 || K >= int32(EDmTownMesh::Num) || Kit[K].Num() == 0) return None;
	return Kit[K][FMath::Clamp(Variant, 0, Kit[K].Num() - 1)];
}

void UDmTownsFactory::SetFrame(const FVector& PlanetCentreWorld, const FVector& SunDirWorld)
{
	for (UMaterialInstanceDynamic* M : { MatBody.Get(), MatGlass.Get(), MatGlow.Get(), MatGround.Get() })
	{
		if (!M) continue;
		M->SetVectorParameterValue(TEXT("PlanetCenter"), FLinearColor(PlanetCentreWorld.X, PlanetCentreWorld.Y, PlanetCentreWorld.Z, 0.f));
		M->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(SunDirWorld.X, SunDirWorld.Y, SunDirWorld.Z, 0.f));
	}
	// (wave 4 towns) the real assets' master (M_DmKit) reads the planet frame from the collection (ASSET-INTEGRATION 3.3)
	if (AssetMPC && World.IsValid())
		if (UMaterialParameterCollectionInstance* I = World->GetParameterCollectionInstance(AssetMPC))
		{
			I->SetVectorParameterValue(TEXT("PlanetCenter"), FLinearColor(PlanetCentreWorld.X, PlanetCentreWorld.Y, PlanetCentreWorld.Z, 0.f));
			I->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(SunDirWorld.X, SunDirWorld.Y, SunDirWorld.Z, 0.f));
		}
}

void UDmTownsFactory::SetNightOverride(float Night)
{
	for (UMaterialInstanceDynamic* M : { MatBody.Get(), MatGlass.Get(), MatGlow.Get(), MatGround.Get() })
		if (M) M->SetScalarParameterValue(TEXT("NightOverride"), Night);
	if (AssetMPC && World.IsValid())
		if (UMaterialParameterCollectionInstance* I = World->GetParameterCollectionInstance(AssetMPC)) I->SetScalarParameterValue(TEXT("NightOverride"), Night);
}

void UDmTownsFactory::SetGlow(float Windows, float Pools, float Streets)
{
	if (MatGlass) MatGlass->SetScalarParameterValue(TEXT("Glow"), Windows);
	if (MatGlow) MatGlow->SetScalarParameterValue(TEXT("Glow"), Pools);
	if (MatGround) MatGround->SetScalarParameterValue(TEXT("Glow"), Streets);
	if (AssetMPC && World.IsValid())
		if (UMaterialParameterCollectionInstance* I = World->GetParameterCollectionInstance(AssetMPC)) I->SetScalarParameterValue(TEXT("Glow"), Windows / 3.f);   // the stand-ins' default 3 = the assets' 1
}

void UDmTownsFactory::FillKitTable(FDmKitTable& Out) const
{
	for (int32 K = 0; K < int32(EDmTownMesh::Num); ++K)
	{
		Out.Infos[K] = Kit[K];
		for (FDmKitInfo& I : Out.Infos[K]) I.Mesh = nullptr;   // plain data for the workers
	}
	if (!Assets.IsValid()) return;
	for (int32 Age = 0; Age < 7; ++Age)
		for (int32 K = 0; K < int32(EDmTownMesh::Num); ++K)
		{
			TArray<FDmKitInfo>& Row = Out.Aged[Age][K];
			Row = Out.Infos[K];
			bool bAny = false;
			for (int32 V = 0; V < Row.Num(); ++V)
			{
				// every height class (the planner places before it knows which one a tier picks): the largest footprint
				FDmKitInfo& I = Row[V];
				int32 NAlt = 0; bool bRuin = true, bCons = true;
				float W = 0, D = 0, H = 0; int32 Tris = 0;
				for (int32 Tier = 0; Tier <= 4; Tier += 2)
					if (const TArray<FDmTownAsset>* L = Assets->Find(EDmTownMesh(K), V, Age, Tier == 2 ? 3 : Tier))
					{
						NAlt = FMath::Max(NAlt, L->Num());
						for (const FDmTownAsset& A : *L)
						{
							W = FMath::Max(W, A.W); D = FMath::Max(D, A.D); H = FMath::Max(H, A.H); Tris = FMath::Max(Tris, A.Tris);
							bRuin &= A.HasState(EDmTownState::Ruin); bCons &= A.HasState(EDmTownState::Construction);
						}
					}
				if (NAlt == 0) continue;
				bAny = true;
				I.W = FMath::Max(W, 1.f); I.D = FMath::Max(D, 1.f); I.H = FMath::Max(H, 0.5f); I.Tris = Tris;
				I.NumAlt = uint8(FMath::Min(NAlt, 255)); I.bAssetRuin = bRuin; I.bAssetCons = bCons;
			}
			if (!bAny) Row.Reset();
		}
}
