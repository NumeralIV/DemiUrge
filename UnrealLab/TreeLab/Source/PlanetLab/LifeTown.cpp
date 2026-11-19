// LifeLab: settlements. A village with fields and hedgerows, a walled medieval town on a hill and an
// industrial port city, laid out on the terrain and built as generated meshes (walls, roofs, windows, lamps).
#include "LifeWorld.h"
#include "LifeMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"

namespace
{
	enum ESec : int32 { SW = 0, SR = 1, SG = 2, SL = 3, SP = 4 }; // wall, roof, glass, lamp, additive glow pool
	enum EWallStyle : int32 { WPlaster = 0, WStone = 1, WBrick = 2, WWood = 3, WConcrete = 4 };
	enum ERoofStyle : int32 { RThatch = 0, RTile = 1, RSlate = 2, RMetal = 3 };
	FVector2f St(int32 S) { return FVector2f(float(S), 0.f); }
	const FVector3f Z3(0, 0, 1);

	/** Quad with metric-ish UVs supplied, normal forced towards WantN. */
	void Q4(FLifeMesh& M, int32 S, const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& D, const FColor& Col,
		const FVector2f& Ua, const FVector2f& Ub, const FVector2f& Uc, const FVector2f& Ud, const FVector2f& U1, const FVector3f& WantN)
	{
		FVector3f N = FVector3f::CrossProduct(B - A, D - A).GetSafeNormal();
		if (FVector3f::DotProduct(N, WantN) < 0.f) N = -N;
		const int32 a = M.Add(A, N, Ua, Col, U1), b = M.Add(B, N, Ub, Col, U1), c = M.Add(C, N, Uc, Col, U1), d = M.Add(D, N, Ud, Col, U1);
		M.Quad(S, a, b, c, d, N);
	}
	/** Quad with UVs projected onto its own plane in metres. */
	void Q4m(FLifeMesh& M, int32 S, const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& D, const FColor& Col, const FVector2f& U1, const FVector3f& WantN)
	{
		FVector3f N = FVector3f::CrossProduct(B - A, D - A).GetSafeNormal();
		if (FVector3f::DotProduct(N, WantN) < 0.f) N = -N;
		FVector3f Ux = (B - A).GetSafeNormal();
		if (FMath::Abs(FVector3f::DotProduct(Ux, N)) > 0.9f) Ux = FVector3f(1, 0, 0);
		const FVector3f Vy = FVector3f::CrossProduct(N, Ux).GetSafeNormal();
		auto UV = [&](const FVector3f& P) { return FVector2f(FVector3f::DotProduct(P, Ux), FVector3f::DotProduct(P, Vy)); };
		Q4(M, S, A, B, C, D, Col, UV(A), UV(B), UV(C), UV(D), U1, N);
	}
	void T3m(FLifeMesh& M, int32 S, const FVector3f& A, const FVector3f& B, const FVector3f& C, const FColor& Col, const FVector2f& U1, const FVector3f& WantN)
	{
		FVector3f N = FVector3f::CrossProduct(B - A, C - A).GetSafeNormal();
		if (FVector3f::DotProduct(N, WantN) < 0.f) N = -N;
		FVector3f Ux = (B - A).GetSafeNormal();
		const FVector3f Vy = FVector3f::CrossProduct(N, Ux).GetSafeNormal();
		auto UV = [&](const FVector3f& P) { return FVector2f(FVector3f::DotProduct(P, Ux), FVector3f::DotProduct(P, Vy)); };
		const int32 a = M.Add(A, N, UV(A), Col, U1), b = M.Add(B, N, UV(B), Col, U1), c = M.Add(C, N, UV(C), Col, U1);
		M.Tri(S, a, b, c, N);
	}
	/** Additive light pool: a disc fan, bright warm centre fading to black at the rim (only visible at night). */
	void GlowDisc(FLifeMesh& M, const FVector3f& C, float R, const FColor& Col, int32 Seg = 14)
	{
		// centre, a mid ring at 40% radius with ~40% intensity, black rim: a soft, roughly quadratic falloff
		const int32 C0 = M.Add(C, Z3, FVector2f(0, 0), Col);
		const int32 B0 = M.V.Num();
		const FColor Mid = LifeColMul(Col, 0.42f);
		for (int32 I = 0; I <= Seg; ++I)
		{
			const float A = 2.f * PI * I / Seg;
			const FVector3f D(FMath::Cos(A), FMath::Sin(A), 0.f);
			M.Add(C + D * (R * 0.4f), Z3, FVector2f(0, 0), Mid);
			M.Add(C + D * R, Z3, FVector2f(0, 0), FColor(0, 0, 0, 255));
		}
		for (int32 I = 0; I < Seg; ++I)
		{
			const int32 a = B0 + 2 * I, b = a + 2;
			M.Tri(SP, C0, a, b, Z3);
			M.Quad(SP, a, b, b + 1, a + 1, Z3);
		}
	}
	FColor Jit(FLifeRand& R, const FColor& C, float Amt = 0.06f)
	{
		const float K = 1.f + R.R(-Amt, Amt);
		return LifeCol(C.R / 255.f * K * (1.f + R.R(-0.02f, 0.02f)), C.G / 255.f * K, C.B / 255.f * K * (1.f + R.R(-0.02f, 0.02f)));
	}
	FVector2D Perp(const FVector2D& D) { return FVector2D(-D.Y, D.X); }
	FVector2D Dir2(double A) { return FVector2D(FMath::Cos(A), FMath::Sin(A)); }
	double Ang(const FVector2D& D) { return FMath::Atan2(D.Y, D.X); }
	double Ang2(const FVector2D& D) { return FMath::Atan2(D.Y, D.X); }
}

struct ALifeWorld::FBuildingSpec
{
	enum EKind : uint8 { House, Church, RoundTower, Keep, WallSeg, Pier, Crane, Stack, Ship, Lamp, Gatehouse, ClockTower } Kind = House;
	enum ERoof : uint8 { Gable, Hip, Flat, Sawtooth } Roof = Gable;
	FVector2D C = FVector2D::ZeroVector;
	double Yaw = 0.0;
	double W = 8.0, D = 6.0;       // local X (ridge) and Y extents
	int32 Storeys = 1;
	double StoreyH = 3.0;
	double Pitch = FMath::DegreesToRadians(42.0);
	double Overhang = 0.4;
	int32 WallStyle = WPlaster, RoofStyle = RTile;
	FColor WallCol = FColor(220, 210, 190), RoofCol = FColor(130, 70, 50);
	bool bTimber = false, bSills = true;
	int32 Chimneys = 0;
	int32 DoorFace = 0;             // 0:-Y 1:+X 2:+Y 3:-X
	float LitFrac = 0.4f;
	FLinearColor LitCol = FLinearColor(1.f, 0.66f, 0.34f);
	double WinW = 0.9, WinH = 1.2, WinSpacing = 3.0;
	double Extra = 0.0;             // tower height, wall height, stack height...
	FVector2D P0 = FVector2D::ZeroVector, P1 = FVector2D::ZeroVector;
	bool bWing = false;
	double WingW = 0.0, WingD = 0.0;
	uint32 Seed = 0;
};

struct ALifeWorld::FTownPlan
{
	int32 S = 0;
	TArray<FBuildingSpec> B;
	TArray<FVector2D> Gates;
};

namespace
{
	using FSpec = ALifeWorld::FBuildingSpec;

	// ------------------------------------------------------------------ building geometry (local frame, z=0 at floor)
	void WallsAndWindows(FLifeMesh& M, const FSpec& S, double Zf, double E, FLifeRand& R)
	{
		const float hw = float(S.W * 0.5), hd = float(S.D * 0.5);
		const FVector2f Ws = St(S.WallStyle);
		const FVector3f C[4] = { FVector3f(-hw, -hd, 0), FVector3f(hw, -hd, 0), FVector3f(hw, hd, 0), FVector3f(-hw, hd, 0) };
		float UOff = 0.f;
		for (int32 F = 0; F < 4; ++F)
		{
			const FVector3f A = C[F], B = C[(F + 1) % 4];
			const FVector3f U = (B - A).GetSafeNormal();
			const FVector3f N = FVector3f::CrossProduct(U, Z3).GetSafeNormal() * -1.f; // outward
			const FVector3f Nn = (FVector3f::DotProduct(N, (A + B) * 0.5f) < 0.f) ? -N : N;
			const float L = (B - A).Size();
			const FVector3f Zb(0, 0, float(Zf)), Zt(0, 0, float(E));
			Q4(M, SW, A + Zb, B + Zb, B + Zt, A + Zt, S.WallCol, FVector2f(UOff, float(Zf)), FVector2f(UOff + L, float(Zf)), FVector2f(UOff + L, float(E)), FVector2f(UOff, float(E)), Ws, Nn);
			UOff += L;
			// plinth
			if (S.WallStyle != WConcrete && S.Kind == FSpec::House)
			{
				const FVector3f Po = Nn * 0.07f;
				const FColor Pc = LifeColMul(S.WallStyle == WStone ? S.WallCol : FColor(150, 140, 125), 0.8f);
				Q4m(M, SW, A + Zb + Po - U * 0.07f, B + Zb + Po + U * 0.07f, B + Po + U * 0.07f + FVector3f(0, 0, 0.45f), A + Po - U * 0.07f + FVector3f(0, 0, 0.45f), Pc, St(WStone), Nn);
				Q4m(M, SW, A + Po - U * 0.07f + FVector3f(0, 0, 0.45f), B + Po + U * 0.07f + FVector3f(0, 0, 0.45f), B + U * 0.07f + FVector3f(0, 0, 0.45f), A - U * 0.07f + FVector3f(0, 0, 0.45f), Pc, St(WStone), Z3);
			}
			// windows
			int32 Nw = FMath::FloorToInt((L - 1.2f) / float(S.WinSpacing));
			if (Nw < 1) Nw = (L > 2.4f) ? 1 : 0;
			const bool bDoorFace = (F == S.DoorFace);
			for (int32 Sy = 0; Sy < S.Storeys; ++Sy)
			{
				const float Zc = float(Sy * S.StoreyH + (Sy == 0 ? 0.95 : 0.85) + S.WinH * 0.5);
				if (Zc + S.WinH * 0.5 > E - 0.15) continue;
				for (int32 K = 0; K < Nw; ++K)
				{
					const float Al = (K + 0.5f) * L / Nw;
					const FVector3f P = A + U * Al;
					if (bDoorFace && Sy == 0 && K == Nw / 2 && S.Kind == FSpec::House)
					{
						if (R.F() < S.LitFrac * 0.7f)
						{
							M.Box(SL, P + Nn * 0.15f + U * 0.8f + FVector3f(0, 0, 2.3f), FVector3f(0.12f, 0.12f, 0.16f), 0.f, LifeCol(1.f, 0.7f, 0.35f), FVector2f(0, 0));
							GlowDisc(M, P + Nn * 2.2f + FVector3f(0, 0, 0.12f), 3.6f, LifeCol(0.45f, 0.30f, 0.15f), 10);
						}
						const FColor Dc = LifeCol(0.22f, 0.15f, 0.10f);
						const FVector3f Dn = Nn * 0.04f;
						Q4(M, SW, P - U * 0.55f + Dn, P + U * 0.55f + Dn, P + U * 0.55f + Dn + FVector3f(0, 0, 2.1f), P - U * 0.55f + Dn + FVector3f(0, 0, 2.1f), Dc,
							FVector2f(0, 0), FVector2f(1.1f, 0), FVector2f(1.1f, 2.1f), FVector2f(0, 2.1f), St(WWood), Nn);
						continue;
					}
					const bool bLit = R.F() < S.LitFrac;
					const float Lk = R.R(0.55f, 1.f);
					const FColor Gc = bLit ? LifeCol(S.LitCol.R * Lk, S.LitCol.G * Lk, S.LitCol.B * Lk) : FColor(0, 0, 0, 255);
					const float ww = float(S.WinW * 0.5), wh = float(S.WinH * 0.5);
					const FVector3f O = Nn * 0.035f;
					Q4(M, SG, P - U * ww + FVector3f(0, 0, Zc - wh) + O, P + U * ww + FVector3f(0, 0, Zc - wh) + O, P + U * ww + FVector3f(0, 0, Zc + wh) + O, P - U * ww + FVector3f(0, 0, Zc + wh) + O, Gc,
						FVector2f(0, 0), FVector2f(1, 0), FVector2f(1, 1), FVector2f(0, 1), FVector2f(0, 0), Nn);
					if (S.bSills)
					{
						const FColor Sc = LifeCol(0.78f, 0.75f, 0.68f);
						M.Box(SW, P + FVector3f(0, 0, Zc - wh - 0.05f) + Nn * 0.07f, FVector3f(ww + 0.08f, 0.07f, 0.05f), FMath::Atan2(U.Y, U.X), Sc, St(WStone));
						M.Box(SW, P + FVector3f(0, 0, Zc + wh + 0.07f) + Nn * 0.05f, FVector3f(ww + 0.06f, 0.05f, 0.07f), FMath::Atan2(U.Y, U.X), S.bTimber ? LifeCol(0.2f, 0.14f, 0.1f) : Sc, St(S.bTimber ? WWood : WStone));
					}
				}
			}
			// half-timbering
			if (S.bTimber)
			{
				const FColor Tc = LifeCol(0.20f, 0.14f, 0.10f);
				const float Yaw = FMath::Atan2(U.Y, U.X);
				for (int32 Sy = 0; Sy <= S.Storeys; ++Sy)
				{
					const float Zb2 = FMath::Min(float(Sy * S.StoreyH), float(E) - 0.1f);
					M.Box(SW, (A + B) * 0.5f + FVector3f(0, 0, Zb2) + Nn * 0.04f, FVector3f(L * 0.5f, 0.05f, 0.1f), Yaw, Tc, St(WWood));
				}
				for (int32 K = 0; K <= Nw; ++K)
				{
					const float Al = (Nw > 0) ? K * L / Nw : 0.f;
					M.Box(SW, A + U * FMath::Clamp(Al, 0.1f, L - 0.1f) + FVector3f(0, 0, float(E) * 0.5f) + Nn * 0.04f, FVector3f(0.09f, 0.05f, float(E) * 0.5f), Yaw, Tc, St(WWood));
				}
				// corner braces on each storey
				for (int32 Sy = 0; Sy < S.Storeys; ++Sy)
				{
					const float Z0 = float(Sy * S.StoreyH) + 0.15f, Z1 = float((Sy + 1) * S.StoreyH) - 0.15f;
					if (Z1 > E) break;
					M.Beam(SW, A + U * 0.15f + Nn * 0.04f + FVector3f(0, 0, Z0), A + U * 1.25f + Nn * 0.04f + FVector3f(0, 0, Z1), 0.1f, 0.15f, Tc, St(WWood));
					M.Beam(SW, B - U * 0.15f + Nn * 0.04f + FVector3f(0, 0, Z0), B - U * 1.25f + Nn * 0.04f + FVector3f(0, 0, Z1), 0.1f, 0.15f, Tc, St(WWood));
				}
			}
		}
	}

	void GableRoof(FLifeMesh& M, const FSpec& S, double E, double& OutRidgeZ)
	{
		const float hw = float(S.W * 0.5), hd = float(S.D * 0.5), o = float(S.Overhang);
		const float tp = FMath::Tan(float(S.Pitch));
		const float t = S.RoofStyle == RThatch ? 0.35f : 0.14f;
		const float Rh = hd * tp, zr = float(E) + Rh, ye = hd + o, ze = float(E) - o * tp, xv = hw + o * 0.6f;
		OutRidgeZ = zr;
		const FVector2f Rs = St(S.RoofStyle);
		const float Sl = ye / FMath::Cos(float(S.Pitch));
		const FColor Under = LifeColMul(S.RoofCol, 0.55f);
		for (int32 Sd = -1; Sd <= 1; Sd += 2)
		{
			const FVector3f A(-xv, Sd * ye, ze), B(xv, Sd * ye, ze), C(xv, 0, zr), D(-xv, 0, zr);
			const FVector3f Out(0, float(Sd) * tp, 1.f);
			Q4(M, SR, A, B, C, D, S.RoofCol, FVector2f(-xv, Sl), FVector2f(xv, Sl), FVector2f(xv, 0), FVector2f(-xv, 0), Rs, Out);
			const FVector3f Dn(0, 0, -t);
			Q4(M, SR, A + Dn, B + Dn, C + Dn, D + Dn, Under, FVector2f(-xv, Sl), FVector2f(xv, Sl), FVector2f(xv, 0), FVector2f(-xv, 0), Rs, -Out);
			Q4m(M, SR, A, B, B + Dn, A + Dn, Under, Rs, FVector3f(0, float(Sd), 0));
			Q4m(M, SR, A, D, D + Dn, A + Dn, Under, Rs, FVector3f(-1, 0, 0));
			Q4m(M, SR, B, C, C + Dn, B + Dn, Under, Rs, FVector3f(1, 0, 0));
		}
		for (int32 Sx = -1; Sx <= 1; Sx += 2)
			T3m(M, SW, FVector3f(Sx * hw, -hd, float(E)), FVector3f(Sx * hw, hd, float(E)), FVector3f(Sx * hw, 0, zr - 0.05f), S.WallCol, St(S.WallStyle), FVector3f(float(Sx), 0, 0));
		M.Box(SR, FVector3f(0, 0, zr + 0.02f), FVector3f(xv, S.RoofStyle == RThatch ? 0.3f : 0.16f, S.RoofStyle == RThatch ? 0.16f : 0.08f), 0.f, LifeColMul(S.RoofCol, 0.8f), Rs);
	}

	void HipRoof(FLifeMesh& M, const FSpec& S, double E, double& OutRidgeZ)
	{
		const bool bSwap = S.D > S.W;
		const float hw = float((bSwap ? S.D : S.W) * 0.5), hd = float((bSwap ? S.W : S.D) * 0.5), o = float(S.Overhang);
		const float tp = FMath::Tan(float(S.Pitch));
		const float Rh = hd * tp, zr = float(E) + Rh, ze = float(E) - o * tp, hr = hw - hd;
		OutRidgeZ = zr;
		auto P = [bSwap](float X, float Y, float Zz) { return bSwap ? FVector3f(Y, X, Zz) : FVector3f(X, Y, Zz); };
		const FVector2f Rs = St(S.RoofStyle);
		const float Ex = hw + o, Ey = hd + o, Sl = Ey / FMath::Cos(float(S.Pitch));
		for (int32 Sd = -1; Sd <= 1; Sd += 2)
		{
			const FVector3f Out = P(0, float(Sd) * tp, 1.f);
			Q4(M, SR, P(-Ex, Sd * Ey, ze), P(Ex, Sd * Ey, ze), P(hr, 0, zr), P(-hr, 0, zr), S.RoofCol, FVector2f(-Ex, Sl), FVector2f(Ex, Sl), FVector2f(hr, 0), FVector2f(-hr, 0), Rs, Out);
			const FVector3f A = P(Sd * Ex, -Ey, ze), B = P(Sd * Ex, Ey, ze), C = P(Sd * hr, 0, zr);
			const FVector3f On = P(float(Sd) * tp, 0, 1.f);
			FVector3f N = FVector3f::CrossProduct(B - A, C - A).GetSafeNormal(); if (FVector3f::DotProduct(N, On) < 0.f) N = -N;
			const int32 a = M.Add(A, N, FVector2f(-Ey, Sl), S.RoofCol, Rs), b = M.Add(B, N, FVector2f(Ey, Sl), S.RoofCol, Rs), c = M.Add(C, N, FVector2f(0, 0), S.RoofCol, Rs);
			M.Tri(SR, a, b, c, N);
		}
		Q4m(M, SR, P(-Ex, -Ey, ze - 0.05f), P(Ex, -Ey, ze - 0.05f), P(Ex, Ey, ze - 0.05f), P(-Ex, Ey, ze - 0.05f), LifeColMul(S.RoofCol, 0.5f), Rs, -Z3);
		if (hr > 0.05f) M.Box(SR, P(0, 0, zr + 0.02f), bSwap ? FVector3f(0.14f, hr, 0.07f) : FVector3f(hr, 0.14f, 0.07f), 0.f, LifeColMul(S.RoofCol, 0.8f), Rs);
	}

	void FlatRoof(FLifeMesh& M, const FSpec& S, double E, FLifeRand& R, double& OutTop)
	{
		const float hw = float(S.W * 0.5), hd = float(S.D * 0.5), e = float(E);
		Q4m(M, SR, FVector3f(-hw, -hd, e), FVector3f(hw, -hd, e), FVector3f(hw, hd, e), FVector3f(-hw, hd, e), LifeCol(0.30f, 0.30f, 0.30f), St(RMetal), Z3);
		const float Ph = 0.9f, T = 0.25f;
		const FVector2f Ws = St(S.WallStyle);
		M.Box(SW, FVector3f(0, -hd + T, e + Ph * 0.5f), FVector3f(hw, T, Ph * 0.5f), 0.f, S.WallCol, Ws);
		M.Box(SW, FVector3f(0, hd - T, e + Ph * 0.5f), FVector3f(hw, T, Ph * 0.5f), 0.f, S.WallCol, Ws);
		M.Box(SW, FVector3f(-hw + T, 0, e + Ph * 0.5f), FVector3f(T, hd, Ph * 0.5f), 0.f, S.WallCol, Ws);
		M.Box(SW, FVector3f(hw - T, 0, e + Ph * 0.5f), FVector3f(T, hd, Ph * 0.5f), 0.f, S.WallCol, Ws);
		// cornice band
		M.Box(SW, FVector3f(0, 0, e - 0.25f), FVector3f(hw + 0.12f, hd + 0.12f, 0.12f), 0.f, LifeColMul(S.WallCol, 0.85f), Ws);
		const int32 Nc = R.I(1, 3);
		for (int32 I = 0; I < Nc; ++I)
		{
			const float Bx = R.R(1.f, 2.5f), By = R.R(1.f, 2.5f), Bz = R.R(0.8f, 2.6f);
			if (hw < Bx + 1.f || hd < By + 1.f) continue;
			M.Box(SW, FVector3f(R.R(-hw + Bx + 0.6f, hw - Bx - 0.6f), R.R(-hd + By + 0.6f, hd - By - 0.6f), e + Bz * 0.5f), FVector3f(Bx * 0.5f, By * 0.5f, Bz * 0.5f), 0.f, LifeCol(0.55f, 0.54f, 0.52f), St(WConcrete));
		}
		if (R.F() < 0.25f && hw > 4.f && hd > 4.f)
			M.Cylinder(SW, FVector3f(R.R(-hw + 2.f, hw - 2.f), R.R(-hd + 2.f, hd - 2.f), e + 1.5f), 1.3f, 1.3f, 2.2f, 10, LifeCol(0.36f, 0.27f, 0.2f), true, St(WWood));
		OutTop = e + Ph;
	}

	void SawtoothRoof(FLifeMesh& M, const FSpec& S, double E, FLifeRand& R)
	{
		const float hw = float(S.W * 0.5), hd = float(S.D * 0.5), e = float(E);
		const int32 N = FMath::Max(1, FMath::RoundToInt(S.W / 9.0));
		const float P = float(S.W) / N, Ht = 3.2f;
		const FVector2f Rs = St(RMetal);
		for (int32 I = 0; I < N; ++I)
		{
			const float X0 = -hw + I * P, X1 = X0 + P * 0.82f, X2 = X0 + P;
			Q4m(M, SR, FVector3f(X0, -hd, e), FVector3f(X0, hd, e), FVector3f(X1, hd, e + Ht), FVector3f(X1, -hd, e + Ht), S.RoofCol, Rs, FVector3f(-0.4f, 0, 1));
			const bool bLit = R.F() < S.LitFrac;
			const FColor Gc = bLit ? LifeCol(0.45f, 0.52f, 0.6f) : FColor(0, 0, 0, 255);
			Q4(M, SG, FVector3f(X1, -hd + 0.3f, e + 0.2f), FVector3f(X1, hd - 0.3f, e + 0.2f), FVector3f(X1, hd - 0.3f, e + Ht - 0.15f), FVector3f(X1, -hd + 0.3f, e + Ht - 0.15f), Gc,
				FVector2f(0, 0), FVector2f(1, 0), FVector2f(1, 1), FVector2f(0, 1), FVector2f(0, 0), FVector3f(1, 0, 0));
			Q4m(M, SW, FVector3f(X1, -hd, e), FVector3f(X1, -hd + 0.3f, e), FVector3f(X1, -hd + 0.3f, e + Ht), FVector3f(X1, -hd, e + Ht), S.WallCol, St(S.WallStyle), FVector3f(1, 0, 0));
			Q4m(M, SW, FVector3f(X1, hd - 0.3f, e), FVector3f(X1, hd, e), FVector3f(X1, hd, e + Ht), FVector3f(X1, hd - 0.3f, e + Ht), S.WallCol, St(S.WallStyle), FVector3f(1, 0, 0));
			Q4m(M, SR, FVector3f(X1, -hd, e), FVector3f(X2, -hd, e), FVector3f(X2, hd, e), FVector3f(X1, hd, e), LifeColMul(S.RoofCol, 0.8f), Rs, Z3);
			for (int32 Sd = -1; Sd <= 1; Sd += 2)
				T3m(M, SW, FVector3f(X0, Sd * hd, e), FVector3f(X1, Sd * hd, e), FVector3f(X1, Sd * hd, e + Ht), S.WallCol, St(S.WallStyle), FVector3f(0, float(Sd), 0));
		}
	}

	void Chimneys(FLifeMesh& M, const FSpec& S, double E, double RidgeZ, FLifeRand& R)
	{
		for (int32 I = 0; I < S.Chimneys; ++I)
		{
			const float X = float(S.W * (S.Chimneys == 1 ? R.R(-0.3f, 0.3f) : (I == 0 ? -0.36f : 0.36f)));
			const float Y = R.R(-0.4f, 0.4f);
			const float Top = float(RidgeZ) + R.R(0.6f, 1.1f);
			const float Z0 = float(E);
			const FColor Cc = R.F() < 0.5f ? LifeCol(0.48f, 0.30f, 0.24f) : LifeCol(0.55f, 0.52f, 0.47f);
			M.Box(SW, FVector3f(X, Y, (Z0 + Top) * 0.5f), FVector3f(0.32f, 0.32f, (Top - Z0) * 0.5f), 0.f, Cc, St(WBrick));
			M.Box(SW, FVector3f(X, Y, Top + 0.06f), FVector3f(0.42f, 0.42f, 0.06f), 0.f, LifeColMul(Cc, 0.7f), St(WStone));
		}
	}

	void GenHouse(FLifeMesh& M, const FSpec& S, double Zf, FLifeRand& R)
	{
		const double E = S.Storeys * S.StoreyH;
		WallsAndWindows(M, S, Zf, E, R);
		double RidgeZ = E;
		switch (S.Roof)
		{
		case FSpec::Gable: GableRoof(M, S, E, RidgeZ); break;
		case FSpec::Hip: HipRoof(M, S, E, RidgeZ); break;
		case FSpec::Flat: FlatRoof(M, S, E, R, RidgeZ); break;
		case FSpec::Sawtooth: SawtoothRoof(M, S, E, R); RidgeZ = E + 3.2; break;
		}
		if (S.Roof == FSpec::Gable || S.Roof == FSpec::Hip) Chimneys(M, S, E, RidgeZ, R);
		if (S.bWing)
		{
			FSpec Wg = S;
			Wg.bWing = false; Wg.W = S.WingW; Wg.D = S.WingD; Wg.Chimneys = 0; Wg.DoorFace = 2;
			Wg.Storeys = FMath::Max(1, S.Storeys - (R.F() < 0.5f ? 1 : 0));
			FLifeMesh Tmp(5);
			GenHouse(Tmp, Wg, Zf, R);
			// wing: ridge perpendicular, attached at the back corner
			const FTransform3f T(FQuat4f(FVector3f(0, 0, 1), PI * 0.5f), FVector3f(float(S.W * 0.5 - S.WingD * 0.5), float(S.D * 0.5 + S.WingW * 0.5 - 0.6), 0.f));
			M.Append(Tmp, T);
		}
	}

	void GenChurch(FLifeMesh& M, const FSpec& S, double Zf, FLifeRand& R)
	{
		FSpec N = S;
		N.Kind = FSpec::House; N.Storeys = 1; N.StoreyH = S.Extra > 0 ? S.Extra : 9.0; N.WinW = 1.3; N.WinH = 3.6; N.WinSpacing = 4.5;
		N.Pitch = FMath::DegreesToRadians(52.0); N.bTimber = false; N.Chimneys = 0; N.DoorFace = 3; N.bSills = true;
		N.LitFrac = 0.6f; N.LitCol = FLinearColor(1.f, 0.75f, 0.45f);
		GenHouse(M, N, Zf, R);
		// west tower with spire
		const float Tw = float(S.D * 0.55), Th = float(N.StoreyH * 2.1 + 4.0);
		const FVector3f Tc(float(-S.W * 0.5 - Tw * 0.5 + 0.3), 0, 0);
		FSpec T = N; T.W = Tw; T.D = Tw; T.Storeys = 1; T.StoreyH = Th; T.WinW = 0.8; T.WinH = 2.2; T.WinSpacing = 3.0; T.LitFrac = 0.2f;
		FLifeMesh Tmp(5);
		WallsAndWindows(Tmp, T, Zf, Th, R);
		Tmp.Pyramid(SR, FVector3f(0, 0, Th), Tw * 0.55f, Tw * 0.55f, 0.f, Tw * 2.4f, S.RoofCol, St(RSlate));
		M.Append(Tmp, FTransform3f(FQuat4f::Identity, Tc));
	}

	void GenRoundTower(FLifeMesh& M, const FSpec& S, double Zf)
	{
		const float R0 = float(S.W * 0.5), H = float(S.Extra);
		M.Cylinder(SW, FVector3f(0, 0, float(Zf)), R0 * 1.05f, R0, H - float(Zf), 18, S.WallCol, false, St(WStone));
		M.Cylinder(SW, FVector3f(0, 0, H - 0.6f), R0 + 0.35f, R0 + 0.35f, 0.6f, 18, LifeColMul(S.WallCol, 0.9f), true, St(WStone));
		M.Cone(SR, FVector3f(0, 0, H), R0 + 0.7f, R0 * 1.9f, 18, S.RoofCol, St(S.RoofStyle));
		// arrow slits
		FLifeRand R(S.Seed);
		for (int32 I = 0; I < 6; ++I)
		{
			const float A = I * PI / 3.f + 0.3f, Z = R.R(3.f, H - 3.f);
			const FVector3f D(FMath::Cos(A), FMath::Sin(A), 0), U(-FMath::Sin(A), FMath::Cos(A), 0);
			const FVector3f P = D * (R0 + 0.04f);
			const FColor Gc = R.F() < 0.3f ? LifeCol(1.f, 0.6f, 0.3f) : FColor(0, 0, 0, 255);
			Q4(M, SG, P - U * 0.18f + FVector3f(0, 0, Z), P + U * 0.18f + FVector3f(0, 0, Z), P + U * 0.18f + FVector3f(0, 0, Z + 1.2f), P - U * 0.18f + FVector3f(0, 0, Z + 1.2f), Gc,
				FVector2f(0, 0), FVector2f(1, 0), FVector2f(1, 1), FVector2f(0, 1), FVector2f(0, 0), D);
		}
	}

	void Merlons(FLifeMesh& M, const FVector3f& A, const FVector3f& B, const FVector3f& Out, float Z, const FColor& C)
	{
		const float L = (B - A).Size();
		const int32 N = FMath::Max(1, int32(L / 1.7f));
		const FVector3f U = (B - A).GetSafeNormal();
		for (int32 K = 0; K < N; ++K)
		{
			const FVector3f P = A + U * ((K + 0.5f) * L / N) + Out;
			M.Box(SW, FVector3f(P.X, P.Y, Z + 0.45f), FVector3f(0.45f, 0.25f, 0.45f), FMath::Atan2(U.Y, U.X), C, St(WStone));
		}
	}

	void GenKeep(FLifeMesh& M, const FSpec& S, double Zf, FLifeRand& R)
	{
		FSpec K = S; K.Kind = FSpec::Keep; K.Storeys = 1; K.StoreyH = S.Extra; K.WinW = 0.6; K.WinH = 1.4; K.WinSpacing = 4.0; K.LitFrac = 0.3f; K.bSills = false;
		WallsAndWindows(M, K, Zf, S.Extra, R);
		const float hw = float(S.W * 0.5), hd = float(S.D * 0.5), H = float(S.Extra);
		Q4m(M, SR, FVector3f(-hw, -hd, H), FVector3f(hw, -hd, H), FVector3f(hw, hd, H), FVector3f(-hw, hd, H), LifeCol(0.4f, 0.38f, 0.35f), St(RSlate), Z3);
		const FVector3f C4[4] = { FVector3f(-hw, -hd, 0), FVector3f(hw, -hd, 0), FVector3f(hw, hd, 0), FVector3f(-hw, hd, 0) };
		for (int32 I = 0; I < 4; ++I)
		{
			const FVector3f A = C4[I], B = C4[(I + 1) % 4];
			const FVector3f Out = FVector3f::CrossProduct(B - A, Z3).GetSafeNormal() * 0.2f;
			Merlons(M, A, B, Out, H, S.WallCol);
			FSpec T = S; T.W = 5.0; T.Extra = H + 5.0; T.Seed = S.Seed + I;
			FLifeMesh Tmp(5);
			GenRoundTower(Tmp, T, Zf);
			M.Append(Tmp, FTransform3f(FQuat4f::Identity, A));
		}
	}

	/** A stretch of town wall between two points; heights at both ends given relative to the base. */
	void GenWallSeg(FLifeMesh& M, const FSpec& S, float Dz0, float Dz1)
	{
		const float L = float(S.W) * 0.5f + 0.4f, T = float(S.D) * 0.5f, Hw = float(S.Extra);
		const FVector2f Ws = St(WStone);
		auto Pt = [&](float X, float Y, float Zz) { return FVector3f(X, Y, Zz); };
		const float B0 = FMath::Min(Dz0, Dz1) - 3.f;
		for (int32 Sd = -1; Sd <= 1; Sd += 2)
			Q4m(M, SW, Pt(-L, Sd * T, B0), Pt(L, Sd * T, B0), Pt(L, Sd * T, Dz1 + Hw), Pt(-L, Sd * T, Dz0 + Hw), S.WallCol, Ws, FVector3f(0, float(Sd), 0));
		Q4m(M, SW, Pt(-L, -T, Dz0 + Hw), Pt(L, -T, Dz1 + Hw), Pt(L, T, Dz1 + Hw), Pt(-L, T, Dz0 + Hw), LifeColMul(S.WallCol, 0.9f), Ws, Z3);
		for (int32 Sd = -1; Sd <= 1; Sd += 2)
			Q4m(M, SW, Pt(Sd * L, -T, B0), Pt(Sd * L, T, B0), Pt(Sd * L, T, (Sd < 0 ? Dz0 : Dz1) + Hw), Pt(Sd * L, -T, (Sd < 0 ? Dz0 : Dz1) + Hw), S.WallCol, Ws, FVector3f(float(Sd), 0, 0));
		// merlons along the outer (-Y) edge, following the slope
		const int32 N = FMath::Max(1, int32(2.f * L / 1.8f));
		for (int32 K = 0; K < N; ++K)
		{
			const float F = (K + 0.5f) / N;
			const float X = -L + 2.f * L * F, Zt = FMath::Lerp(Dz0, Dz1, F) + Hw;
			M.Box(SW, FVector3f(X, -T + 0.25f, Zt + 0.5f), FVector3f(0.5f, 0.25f, 0.5f), 0.f, S.WallCol, Ws);
		}
		// inner parapet
		M.Beam(SW, Pt(-L, T - 0.2f, Dz0 + Hw), Pt(L, T - 0.2f, Dz1 + Hw), 0.35f, 0.9f, S.WallCol, Ws);
	}

	void GenPier(FLifeMesh& M, const FSpec& S)
	{
		const float L = float(S.W) * 0.5f, Hw = float(S.D) * 0.5f;
		const FColor Deck = LifeCol(0.52f, 0.50f, 0.47f);
		M.Box(SW, FVector3f(0, 0, 1.8f), FVector3f(L, Hw, 0.5f), 0.f, Deck, St(WConcrete), true);
		for (float X = -L + 3.f; X < L; X += 9.f)
			for (int32 Sd = -1; Sd <= 1; Sd += 2)
				M.Cylinder(SW, FVector3f(X, Sd * (Hw - 0.8f), -9.f), 0.45f, 0.4f, 10.3f, 7, LifeCol(0.33f, 0.32f, 0.30f), false, St(WConcrete));
		// bollards and a lamp row
		for (float X = -L + 6.f; X < L; X += 12.f)
		{
			M.Cylinder(SW, FVector3f(X, -Hw + 0.5f, 2.3f), 0.18f, 0.15f, 0.5f, 6, LifeCol(0.15f, 0.15f, 0.15f), true, St(WConcrete));
			M.Cylinder(SW, FVector3f(X, Hw - 0.5f, 2.3f), 0.18f, 0.15f, 0.5f, 6, LifeCol(0.15f, 0.15f, 0.15f), true, St(WConcrete));
		}
	}

	void GenCrane(FLifeMesh& M, const FSpec& S, FLifeRand& R)
	{
		const FColor Cc = R.F() < 0.5f ? LifeCol(0.70f, 0.30f, 0.12f) : LifeCol(0.75f, 0.60f, 0.16f);
		const float H = 14.f;
		for (int32 I = 0; I < 4; ++I)
		{
			const float X = (I & 1) ? 4.f : -4.f, Y = (I & 2) ? 4.f : -4.f;
			M.Box(SW, FVector3f(X, Y, H * 0.5f), FVector3f(0.3f, 0.3f, H * 0.5f), 0.f, Cc, St(WConcrete));
			M.Beam(SW, FVector3f(X, Y, 1.f), FVector3f(X * 0.2f, Y * 0.2f, H - 1.f), 0.2f, 0.2f, Cc, St(WConcrete));
		}
		M.Box(SW, FVector3f(0, 0, H + 2.f), FVector3f(3.5f, 3.f, 2.f), 0.f, Cc, St(WConcrete));
		M.Box(SW, FVector3f(-6.f, 0, H + 3.f), FVector3f(2.f, 2.5f, 1.5f), 0.f, LifeCol(0.3f, 0.3f, 0.3f), St(WConcrete));
		M.Beam(SW, FVector3f(-8.f, 0, H + 4.f), FVector3f(30.f, 0, H + 3.2f), 1.2f, 1.6f, Cc, St(WConcrete));
		M.Beam(SW, FVector3f(0, 0, H + 4.f), FVector3f(0, 0, H + 10.f), 0.6f, 0.6f, Cc, St(WConcrete));
		M.Beam(SW, FVector3f(0, 0, H + 10.f), FVector3f(28.f, 0, H + 5.2f), 0.15f, 0.15f, LifeCol(0.2f, 0.2f, 0.2f), St(WConcrete));
		M.Box(SL, FVector3f(0, 0, H + 10.4f), FVector3f(0.25f, 0.25f, 0.25f), 0.f, LifeCol(1.f, 0.25f, 0.15f), FVector2f(0, 0));
	}

	void GenShip(FLifeMesh& M, const FSpec& S, FLifeRand& R)
	{
		const float L = float(S.W) * 0.5f, B = float(S.D) * 0.5f;
		const TArray<FVector2f> Plan = { { -L, -B }, { L - 14.f, -B }, { L - 4.f, -B * 0.55f }, { L, 0 }, { L - 4.f, B * 0.55f }, { L - 14.f, B }, { -L, B }, { -L - 1.5f, 0 } };
		const FColor Hull = R.F() < 0.5f ? LifeCol(0.12f, 0.13f, 0.16f) : LifeCol(0.35f, 0.12f, 0.10f);
		const float Zb = -4.f, Zd = 4.5f;
		for (int32 I = 0; I < Plan.Num(); ++I)
		{
			const FVector2f A = Plan[I], Bp = Plan[(I + 1) % Plan.Num()];
			const FVector3f N = FVector3f(Bp.Y - A.Y, -(Bp.X - A.X), 0).GetSafeNormal();
			Q4m(M, SW, FVector3f(A.X, A.Y, Zb), FVector3f(Bp.X, Bp.Y, Zb), FVector3f(Bp.X, Bp.Y, Zd), FVector3f(A.X, A.Y, Zd), Hull, St(WConcrete), N);
		}
		const int32 C0 = M.Add(FVector3f(0, 0, Zd), Z3, FVector2f(0, 0), LifeCol(0.35f, 0.32f, 0.28f), St(WWood));
		for (int32 I = 0; I < Plan.Num(); ++I)
		{
			const FVector2f A = Plan[I], Bp = Plan[(I + 1) % Plan.Num()];
			const int32 a = M.Add(FVector3f(A.X, A.Y, Zd), Z3, FVector2f(A.X, A.Y), LifeCol(0.35f, 0.32f, 0.28f), St(WWood));
			const int32 b = M.Add(FVector3f(Bp.X, Bp.Y, Zd), Z3, FVector2f(Bp.X, Bp.Y), LifeCol(0.35f, 0.32f, 0.28f), St(WWood));
			M.Tri(SW, C0, a, b, Z3);
		}
		FSpec Sup; Sup.W = 12.0; Sup.D = B * 1.6; Sup.Storeys = 3; Sup.StoreyH = 2.6; Sup.WallStyle = WConcrete; Sup.WallCol = LifeCol(0.86f, 0.86f, 0.83f);
		Sup.WinW = 1.0; Sup.WinH = 0.9; Sup.WinSpacing = 2.0; Sup.LitFrac = 0.6f; Sup.bSills = false; Sup.Kind = FSpec::Ship; Sup.Roof = FSpec::Flat;
		FLifeMesh Tmp(5);
		WallsAndWindows(Tmp, Sup, 0.0, 7.8, R);
		Tmp.Box(SW, FVector3f(0, 0, 7.9f), FVector3f(6.3f, float(B * 0.82f), 0.12f), 0.f, Sup.WallCol, St(WConcrete));
		M.Append(Tmp, FTransform3f(FQuat4f::Identity, FVector3f(-L + 9.f, 0, Zd)));
		M.Cylinder(SW, FVector3f(-L + 9.f, 0, Zd + 7.9f), 1.4f, 1.3f, 5.f, 10, LifeCol(0.12f, 0.12f, 0.12f), true, St(WConcrete));
		M.Cylinder(SW, FVector3f(-L + 9.f, 0, Zd + 11.f), 1.32f, 1.32f, 1.2f, 10, LifeCol(0.7f, 0.15f, 0.1f), false, St(WConcrete));
		// cargo
		for (float X = -L + 18.f; X < L - 16.f; X += 6.5f)
			if (R.F() < 0.7f)
			{
				const int32 Hn = R.I(1, 3);
				for (int32 K = 0; K < Hn; ++K)
				{
					const FColor Cc = Jit(R, R.F() < 0.33f ? LifeCol(0.55f, 0.18f, 0.12f) : (R.F() < 0.5f ? LifeCol(0.18f, 0.3f, 0.45f) : LifeCol(0.6f, 0.55f, 0.45f)), 0.1f);
					M.Box(SW, FVector3f(X, R.R(-1.f, 1.f), Zd + 1.3f + K * 2.6f), FVector3f(3.f, B * 0.7f, 1.3f), 0.f, Cc, St(WConcrete));
				}
			}
	}

	void GenStack(FLifeMesh& M, const FSpec& S, double Zf)
	{
		const float H = float(S.Extra), R0 = float(S.W * 0.5);
		M.Cylinder(SW, FVector3f(0, 0, float(Zf)), R0, R0 * 0.62f, H - float(Zf), 14, S.WallCol, false, St(WBrick));
		M.Cylinder(SW, FVector3f(0, 0, H - 3.f), R0 * 0.68f, R0 * 0.66f, 3.f, 14, LifeCol(0.13f, 0.12f, 0.12f), true, St(WBrick));
		M.Cylinder(SW, FVector3f(0, 0, float(Zf)), R0 * 1.6f, R0 * 1.6f, 4.f - float(Zf), 4, S.WallCol, true, St(WBrick));
		M.Box(SL, FVector3f(R0 * 0.66f, 0, H - 1.f), FVector3f(0.2f, 0.2f, 0.2f), 0.f, LifeCol(1.f, 0.2f, 0.1f), FVector2f(0, 0));
	}

	void GenLamp(FLifeMesh& M)
	{
		const FColor Pc = LifeCol(0.16f, 0.16f, 0.17f);
		M.Cylinder(SW, FVector3f(0, 0, -0.3f), 0.09f, 0.06f, 7.3f, 6, Pc, false, St(WConcrete));
		M.Box(SW, FVector3f(0.6f, 0, 7.0f), FVector3f(0.65f, 0.05f, 0.05f), 0.f, Pc, St(WConcrete));
		M.Box(SL, FVector3f(1.2f, 0, 6.85f), FVector3f(0.35f, 0.18f, 0.08f), 0.f, LifeCol(1.f, 0.78f, 0.5f), FVector2f(0, 0));
		GlowDisc(M, FVector3f(1.2f, 0, 0.15f), 11.f, LifeCol(0.34f, 0.24f, 0.12f));
	}

	// ------------------------------------------------------------------ layout helpers
	TArray<FVector2D> Meander(const FVector2D& A, const FVector2D& B, double Step, double Amp, uint32 Seed)
	{
		TArray<FVector2D> P;
		const double L = FVector2D::Distance(A, B);
		const int32 N = FMath::Max(2, int32(L / Step));
		const FVector2D Pd = Perp((B - A) / FMath::Max(L, 1.0));
		for (int32 I = 0; I <= N; ++I)
		{
			const double T = double(I) / N;
			const double W = FMath::Sin(PI * T);
			P.Add(FMath::Lerp(A, B, T) + Pd * (Amp * W * LifeFbm2(float(T * L / 600.0), 0.37f, 3, Seed)));
		}
		return P;
	}

	/** Walks a polyline and calls F(point, direction) every Step metres (Step re-drawn by the callback's return value). */
	template <typename Fn>
	void WalkPolyline(const TArray<FVector2D>& P, double Start, double End, Fn&& F)
	{
		double Acc = 0.0, Next = Start;
		for (int32 I = 0; I + 1 < P.Num(); ++I)
		{
			const double L = FVector2D::Distance(P[I], P[I + 1]);
			if (L < 1e-6) continue;
			const FVector2D D = (P[I + 1] - P[I]) / L;
			while (Next <= Acc + L && Next <= End)
			{
				const double Step = F(P[I] + D * (Next - Acc), D);
				Next += FMath::Max(Step, 1.0);
			}
			Acc += L;
		}
	}
}

// ====================================================================== occupancy / placement

namespace
{
	int32 GReject[5] = {};
	bool TryPlace(ALifeWorld& W, const FSpec& S, double Margin, double MaxSlope, bool bMark = true)
	{
		const FVector2D Ux = Dir2(S.Yaw), Uy = Perp(Ux);
		const double Hw = S.W * 0.5 + Margin, Hd = S.D * 0.5 + Margin + (S.bWing ? S.WingW : 0.0) * 0.5;
		const FVector2D Cc = S.C + Uy * ((S.bWing ? S.WingW : 0.0) * 0.5);
		float Hmin = 1e9f, Hmax = -1e9f;
		for (int32 K = 0; K < 5; ++K)
		{
			const FVector2D P = K == 4 ? S.C : Cc + Ux * (K & 1 ? Hw : -Hw) + Uy * (K & 2 ? Hd : -Hd);
			int32 I, J;
			if (!W.LuIndex(P.X, P.Y, I, J)) { GReject[0]++; return false; }
			const float H = W.HeightAt(P.X, P.Y);
			Hmin = FMath::Min(Hmin, H); Hmax = FMath::Max(Hmax, H);
		}
		if (Hmin < 1.0f) { GReject[1]++; return false; }
		if (Hmax - Hmin > MaxSlope) { GReject[2]++; return false; }
		const double R = FMath::Sqrt(Hw * Hw + Hd * Hd);
		int32 I0, J0, I1, J1;
		W.LuIndex(Cc.X - R, Cc.Y - R, I0, J0); W.LuIndex(Cc.X + R, Cc.Y + R, I1, J1);
		I0 = FMath::Max(I0, 0); J0 = FMath::Max(J0, 0); I1 = FMath::Min(I1, ALifeWorld::LuN - 1); J1 = FMath::Min(J1, ALifeWorld::LuN - 1);
		auto Inside = [&](int32 I, int32 J, double Extra)
		{
			const FVector2D P(-ALifeWorld::LuHalf + (I + 0.5) * ALifeWorld::LuStep, -ALifeWorld::LuHalf + (J + 0.5) * ALifeWorld::LuStep);
			const FVector2D Q = P - Cc;
			return FMath::Abs(FVector2D::DotProduct(Q, Ux)) <= Hw + Extra && FMath::Abs(FVector2D::DotProduct(Q, Uy)) <= Hd + Extra;
		};
		for (int32 J = J0; J <= J1; ++J)
			for (int32 I = I0; I <= I1; ++I)
				// test only cells whose centres are >1.05 m inside the footprint: on a 2 m raster, terraced neighbours
				// 0.3 m apart would otherwise collide through rasterisation and whole street fronts went missing
				if (Inside(I, J, -(Margin + 1.05)))
				{
					const int32 Ix = J * ALifeWorld::LuN + I;
					if (W.LuBuilt[Ix]) { GReject[3]++; return false; }
					if (W.LuPaved[Ix] > 140 || W.LuRoad[Ix] > 140)
					{
						if (GReject[4]++ < 12)
						{
							const FVector2D P(-ALifeWorld::LuHalf + (I + 0.5) * ALifeWorld::LuStep, -ALifeWorld::LuHalf + (J + 0.5) * ALifeWorld::LuStep);
							UE_LOG(LogTemp, Display, TEXT("LifeTown: street reject kind %d C(%.1f,%.1f) W %.1f D %.1f yaw %.2f cell (%.1f,%.1f) local (%.1f,%.1f) paved %d road %d"),
								int32(S.Kind), S.C.X, S.C.Y, S.W, S.D, S.Yaw, P.X, P.Y, FVector2D::DotProduct(P - Cc, Ux), FVector2D::DotProduct(P - Cc, Uy), W.LuPaved[Ix], W.LuRoad[Ix]);
						}
						return false;
					}
				}
		if (!bMark) return true;
		W.LuIndex(Cc.X - R - 4, Cc.Y - R - 4, I0, J0); W.LuIndex(Cc.X + R + 4, Cc.Y + R + 4, I1, J1);
		I0 = FMath::Max(I0, 0); J0 = FMath::Max(J0, 0); I1 = FMath::Min(I1, ALifeWorld::LuN - 1); J1 = FMath::Min(J1, ALifeWorld::LuN - 1);
		for (int32 J = J0; J <= J1; ++J)
			for (int32 I = I0; I <= I1; ++I)
			{
				const int32 Ix = J * ALifeWorld::LuN + I;
				if (Inside(I, J, -Margin)) W.LuBuilt[Ix] = 1;
				if (Inside(I, J, 3.0 - Margin)) { W.LuNoTree[Ix] = 1; W.LuYard[Ix] = FMath::Max<uint8>(W.LuYard[Ix], 150); }
			}
		return true;
	}

	FColor PickCrop(FLifeRand& R)
	{
		const FColor C[] = { LifeCol(0.70f, 0.60f, 0.35f), LifeCol(0.76f, 0.68f, 0.44f), LifeCol(0.34f, 0.42f, 0.20f), LifeCol(0.40f, 0.46f, 0.26f),
			LifeCol(0.40f, 0.31f, 0.22f), LifeCol(0.50f, 0.46f, 0.32f), LifeCol(0.68f, 0.62f, 0.42f), LifeCol(0.30f, 0.38f, 0.18f), LifeCol(0.78f, 0.72f, 0.26f) };
		const float W[] = { 2.f, 1.5f, 2.f, 2.f, 1.3f, 1.f, 1.5f, 1.2f, 0.25f };
		float Sum = 0.f; for (float X : W) Sum += X;
		float P = R.F() * Sum;
		for (int32 I = 0; I < 9; ++I)
			if (P < W[I])
			{
				// satellite photos of farmland are less saturated than one expects: pull 20% towards grey
				const FColor J = Jit(R, C[I], 0.05f);
				const float L = (J.R * 0.3f + J.G * 0.59f + J.B * 0.11f) / 255.f;
				return LifeCol(FMath::Lerp(J.R / 255.f, L, 0.2f), FMath::Lerp(J.G / 255.f, L, 0.2f), FMath::Lerp(J.B / 255.f, L, 0.2f));
			}
			else P -= W[I];
		return C[0];
	}

	/** Patchwork of fields around a centre between radii R0 and R1: three sectors, each a rotated, brick-bonded grid of
	 *  long strips (merged now and then), with jittered corners, a ragged outer edge and hedgerows on some edges. */
	void PlanFields(ALifeWorld& W, const FVector2D& C, double R0, double R1, uint32 Seed, double SkipFrac)
	{
		FLifeRand R(Seed);
		const double A0 = R.R(0.f, 2.f * PI);
		for (int32 Sec = 0; Sec < 3; ++Sec)
		{
			const double Theta = R.R(0.f, PI);
			const FVector2D U = Dir2(Theta), V = Perp(U);
			const double Cu = R.R(120.f, 190.f), Cv = R.R(45.f, 85.f);
			const int32 Ni = int32(R1 / Cu) + 2, Nj = int32(R1 / Cv) + 2;
			for (int32 J = -Nj; J <= Nj; ++J)
			{
				const double Shift = R.R(0.f, 1.f) * Cu;
				for (int32 I = -Ni; I <= Ni; ++I)
				{
					double Len = Cu * (R.F() < 0.22f ? 2.0 : 1.0) * R.R(0.8f, 1.15f);
					const FVector2D Ctr = C + U * (I * Cu + Shift) + V * (J * Cv);
					const FVector2D D = Ctr - C;
					const double Rr = D.Size();
					double Ang = FMath::Fmod(Ang2(D) - A0 + 6.0 * PI, 2.0 * PI);
					if (int32(Ang / (2.0 * PI / 3.0)) != Sec) continue;
					const double Edge = R1 * (0.8 + 0.35 * (0.5 + 0.5 * LifeFbm2(float(FMath::Cos(Ang) * 2.0), float(FMath::Sin(Ang) * 2.0), 3, Seed)));
					if (Rr < R0 || Rr > Edge || R.F() < SkipFrac) continue;
					const double Hu = Len * 0.5 - 1.5, Hv = Cv * 0.5 - 1.5;
					auto Jc = [&](double Su, double Sv) { return Ctr + U * (Su * Hu) + V * (Sv * Hv) + FVector2D(R.R(-7.f, 7.f), R.R(-7.f, 7.f)); };
					TArray<FVector2D> Poly = { Jc(-1, -1), Jc(1, -1), Jc(1, 1), Jc(-1, 1) };
					bool bOk = true;
					float Hmin = 1e9f, Hmax = -1e9f;
					for (const FVector2D& P : Poly)
					{
						int32 Ii, Jj;
						if (!W.LuIndex(P.X, P.Y, Ii, Jj)) { bOk = false; break; }
						const float H = W.HeightAt(P.X, P.Y);
						Hmin = FMath::Min(Hmin, H); Hmax = FMath::Max(Hmax, H);
					}
					for (int32 Sa = 0; Sa < 16 && bOk; ++Sa)
					{
						const double U0 = (Sa % 4 + 0.5) / 4.0, V0 = (Sa / 4 + 0.5) / 4.0;
						const FVector2D Mid = FMath::Lerp(FMath::Lerp(Poly[0], Poly[3], U0), FMath::Lerp(Poly[1], Poly[2], U0), V0);
						if (W.LuGet(W.LuYard, Mid.X, Mid.Y) || W.LuGet(W.LuBuilt, Mid.X, Mid.Y) || W.LuGet(W.LuField, Mid.X, Mid.Y) || W.LuGet(W.LuPaved, Mid.X, Mid.Y) > 100 || W.HeightAt(Mid.X, Mid.Y) < 2.f) bOk = false;
					}
					if (!bOk || Hmin < 2.f || Hmax - Hmin > 18.f) continue;
					const double RowA = FMath::Fmod((R.F() < 0.75f ? Theta : Theta + PI * 0.5) + 4.0 * PI, PI) / PI;
					W.StampField(Poly, PickCrop(R), float(RowA));
					for (int32 E = 0; E < 4; ++E)
						if (R.F() < (E % 2 ? 0.35f : 0.6f)) W.Hedges.Add(FVector4d(Poly[E].X, Poly[E].Y, Poly[(E + 1) % 4].X, Poly[(E + 1) % 4].Y));
				}
			}
		}
	}

	FColor Pastel(FLifeRand& R)
	{
		const FColor C[] = { LifeCol(0.88f, 0.84f, 0.74f), LifeCol(0.91f, 0.90f, 0.86f), LifeCol(0.84f, 0.72f, 0.52f), LifeCol(0.86f, 0.74f, 0.66f), LifeCol(0.80f, 0.80f, 0.70f), LifeCol(0.78f, 0.62f, 0.46f) };
		return Jit(R, C[R.I(0, 5)], 0.04f);
	}
}

// ====================================================================== sites

FVector2D ALifeWorld::FindSite(FLifeSettlement::EKind Kind, const TArray<FVector2D>& Avoid) const
{
	const double Lim = 3100.0, Step = 80.0;
	double Best = -1e18; FVector2D BestP(0, 0);
	auto Rough = [this](const FVector2D& C, double R)
	{
		double S = 0, S2 = 0; int32 N = 0;
		for (int32 K = 0; K < 12; ++K) { const double H = BaseHeightAt(C.X + R * FMath::Cos(K * PI / 6), C.Y + R * FMath::Sin(K * PI / 6)); S += H; S2 += H * H; ++N; }
		const double M = S / N; return FMath::Sqrt(FMath::Max(0.0, S2 / N - M * M));
	};
	auto Ring = [this](const FVector2D& C, double R, double* OutMean, double* OutSeaFrac, double* OutMin)
	{
		double S = 0, Mn = 1e9; int32 Sea = 0;
		for (int32 K = 0; K < 24; ++K) { const double H = BaseHeightAt(C.X + R * FMath::Cos(K * PI / 12), C.Y + R * FMath::Sin(K * PI / 12)); S += H; Mn = FMath::Min(Mn, H); if (H < -2) ++Sea; }
		if (OutMean) *OutMean = S / 24; if (OutSeaFrac) *OutSeaFrac = Sea / 24.0; if (OutMin) *OutMin = Mn;
	};
	for (double Y = -Lim; Y <= Lim; Y += Step)
		for (double X = -Lim; X <= Lim; X += Step)
		{
			const FVector2D C(X, Y);
			bool bFar = true;
			for (const FVector2D& A : Avoid) if (FVector2D::Distance(A, C) < (Kind == FLifeSettlement::Village ? 1350.0 : 1600.0)) bFar = false;
			if (!bFar) continue;
			const double H = BaseHeightAt(X, Y);
			double Score = -1e18;
			if (Kind == FLifeSettlement::City)
			{
				if (H < 2 || H > 30) continue;
				double SeaF = 0, Mn = 0; Ring(C, 480, nullptr, &SeaF, nullptr); Ring(C, 220, nullptr, nullptr, &Mn);
				if (SeaF < 0.12 || SeaF > 0.5 || Mn < 1.0) continue;
				Score = -Rough(C, 350) * 1.5 - FMath::Abs(SeaF - 0.3) * 25.0 - C.Size() * 0.0015;
			}
			else if (Kind == FLifeSettlement::WalledTown)
			{
				if (H < 8) continue;
				double Mean = 0, Mn = 0; Ring(C, 480, &Mean, nullptr, nullptr); Ring(C, 260, nullptr, nullptr, &Mn);
				if (Mn < 3) continue;
				Score = (H - Mean) - Rough(C, 110) * 2.5 - C.Size() * 0.002;
			}
			else
			{
				if (H < 4 || H > 150) continue;
				double Mn = 0; Ring(C, 320, nullptr, nullptr, &Mn);
				if (Mn < 2) continue;
				Score = -Rough(C, 260) * 2.0 + 8.0 * HabAt(X, Y) - C.Size() * 0.002;
			}
			if (Score > Best) { Best = Score; BestP = C; }
		}
	return BestP;
}

void ALifeWorld::PlanSettlements()
{
	const TCHAR* Cmd = FCommandLine::Get();
	auto Opt = [Cmd](const TCHAR* Key, FVector2D& P)
	{
		FString V;
		if (!FParse::Value(Cmd, Key, V, false)) return false;
		TArray<FString> Parts; V.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() < 2) return false;
		P = FVector2D(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]));
		return true;
	};
	FVector2D CityC, TownC, VillC;
	if (!Opt(TEXT("-city="), CityC)) CityC = FindSite(FLifeSettlement::City, {});
	if (!Opt(TEXT("-town="), TownC)) TownC = FindSite(FLifeSettlement::WalledTown, { CityC });
	if (!Opt(TEXT("-village="), VillC)) VillC = FindSite(FLifeSettlement::Village, { CityC, TownC });
	UE_LOG(LogTemp, Display, TEXT("LifeTown: city (%.0f, %.0f) town (%.0f, %.0f) village (%.0f, %.0f)"), CityC.X, CityC.Y, TownC.X, TownC.Y, VillC.X, VillC.Y);

	Pads.Add({ CityC, 650.0, 1000.0, FMath::Max(3.5f, BaseHeightAt(CityC.X, CityC.Y)) });
	Clearings.Add(FVector3d(CityC.X, CityC.Y, 1150.0));
	Clearings.Add(FVector3d(TownC.X, TownC.Y, 900.0));
	Clearings.Add(FVector3d(VillC.X, VillC.Y, 900.0));

	auto Make = [this](FLifeSettlement::EKind K, const FVector2D& C, const TCHAR* Name)
	{
		FLifeSettlement S; S.Kind = K; S.C = C; S.Name = Name;
		return Settlements.Add(S);
	};
	const int32 Ic = Make(FLifeSettlement::City, CityC, TEXT("city"));
	const int32 It = Make(FLifeSettlement::WalledTown, TownC, TEXT("town"));
	const int32 Iv = Make(FLifeSettlement::Village, VillC, TEXT("village"));
	PlanCity(Settlements[Ic]);
	PlanWalledTown(Settlements[It]);
	PlanVillage(Settlements[Iv]);

	// country roads between settlements (town gates when available)
	auto GateToward = [this](int32 S, const FVector2D& Target)
	{
		const FVector2D C = Settlements[S].C;
		for (const TSharedPtr<FTownPlan>& P : Plans)
			if (P->S == S && P->Gates.Num())
			{
				FVector2D Best = P->Gates[0];
				for (const FVector2D& G : P->Gates) if (FVector2D::Distance(G, Target) < FVector2D::Distance(Best, Target)) Best = G;
				return Best;
			}
		return C;
	};
	auto Road = [&](int32 A, int32 B, uint32 Seed)
	{
		const FVector2D Pa = GateToward(A, Settlements[B].C), Pb = GateToward(B, Settlements[A].C);
		TArray<FVector2D> P = Meander(Pa, Pb, 15.0, FMath::Min(140.0, FVector2D::Distance(Pa, Pb) * 0.07), Seed);
		// keep out of the sea
		for (FVector2D& Q : P) for (int32 It2 = 0; It2 < 6 && HeightAt(Q.X, Q.Y) < 1.5f; ++It2) Q = FMath::Lerp(Q, (Pa + Pb) * 0.5, 0.15);
		StampRoad(P, 6.0, false);
	};
	Road(Iv, It, 3);
	Road(It, Ic, 5);
	Road(Iv, Ic, 7);
	for (const FLifeSettlement& S : Settlements) UE_LOG(LogTemp, Display, TEXT("LifeTown: %s planned with %d buildings"), *S.Name, S.Buildings);
}

// ====================================================================== village

void ALifeWorld::PlanVillage(FLifeSettlement& S)
{
	TSharedPtr<FTownPlan> Plan = MakeShared<FTownPlan>();
	Plan->S = Settlements.IndexOfByPredicate([&S](const FLifeSettlement& O) { return &O == &S; });
	FLifeRand R(4242);
	const FVector2D C = S.C;
	// main street along the contour
	const double Gx = BaseHeightAt(C.X + 50, C.Y) - BaseHeightAt(C.X - 50, C.Y), Gy = BaseHeightAt(C.X, C.Y + 50) - BaseHeightAt(C.X, C.Y - 50);
	const double Axis = (FMath::Abs(Gx) + FMath::Abs(Gy) > 0.5) ? FMath::Atan2(Gy, Gx) + PI * 0.5 : R.R(0.f, PI);
	S.Axis = Axis;
	const FVector2D Dm = Dir2(Axis), Pm = Perp(Dm);
	TArray<FVector2D> Main;
	for (double T = -700; T <= 700; T += 12)
		Main.Add(C + Dm * T + Pm * (28.0 * FMath::Sin(T / 170.0 + 1.3) + 12.0 * LifeFbm2(float(T / 120.0), 0.5f, 3, 77)));
	StampRoad(Main, 5.0, false);
	TArray<TArray<FVector2D>> Front;
	{
		TArray<FVector2D> Core;
		for (const FVector2D& P : Main) if (FVector2D::Distance(P, C) < 190) Core.Add(P);
		Front.Add(Core);
	}
	for (int32 L = 0; L < 2; ++L)
	{
		const double T0 = (L == 0 ? 1 : -1) * R.R(30.f, 80.f);
		const FVector2D A = C + Dm * T0;
		const double Aa = Axis + (L == 0 ? 1 : -1) * FMath::DegreesToRadians(R.R(65.f, 110.f));
		TArray<FVector2D> Lane = Meander(A, A + Dir2(Aa) * 330.0, 10.0, 25.0, 90 + L);
		StampRoad(Lane, 4.0, false);
		TArray<FVector2D> Core;
		for (const FVector2D& P : Lane) if (FVector2D::Distance(P, A) < 170) Core.Add(P);
		Front.Add(Core);
	}
	StampDisc(LuYard, C + Pm * 6.0, 22.0, 90);

	// church by the green
	{
		FSpec Ch; Ch.Kind = FSpec::Church; Ch.C = C + Pm * 34.0; Ch.Yaw = Axis; Ch.W = 22; Ch.D = 9.5; Ch.Extra = 7.5;
		Ch.WallStyle = WStone; Ch.WallCol = LifeCol(0.64f, 0.60f, 0.53f); Ch.RoofStyle = RSlate; Ch.RoofCol = LifeCol(0.33f, 0.34f, 0.37f); Ch.Seed = R.Next();
		FSpec Probe = Ch; Probe.W += 8; Probe.C -= Dm * 4.0;
		if (TryPlace(*this, Probe, 1.0, 5.0)) Plan->B.Add(Ch);
	}
	int32 Hn = 0;
	for (const TArray<FVector2D>& Fp : Front)
		WalkPolyline(Fp, 6.0, 1e9, [&](const FVector2D& P, const FVector2D& Dd) -> double
		{
			for (int32 Sd = -1; Sd <= 1; Sd += 2)
			{
				if (R.F() < 0.18f) continue;
				FSpec H; H.Kind = FSpec::House; H.Seed = R.Next();
				H.W = R.R(7.f, 11.5f); H.D = R.R(5.f, 6.8f);
				H.Storeys = R.F() < 0.3f ? 2 : 1; H.StoreyH = H.Storeys == 2 ? 2.6 : 2.9;
				const bool bThatch = R.F() < 0.55f;
				H.RoofStyle = bThatch ? RThatch : (R.F() < 0.7f ? RTile : RSlate);
				H.RoofCol = bThatch ? Jit(R, LifeCol(0.50f, 0.41f, 0.27f), 0.08f) : (H.RoofStyle == RTile ? Jit(R, LifeCol(0.52f, 0.27f, 0.18f), 0.1f) : Jit(R, LifeCol(0.33f, 0.34f, 0.37f)));
				H.Roof = R.F() < 0.72f ? FSpec::Gable : FSpec::Hip;
				H.Pitch = FMath::DegreesToRadians(bThatch ? R.R(48.f, 55.f) : R.R(38.f, 46.f));
				H.Overhang = bThatch ? 0.6 : 0.35;
				const float Wr = R.F();
				H.WallStyle = Wr < 0.55f ? WPlaster : (Wr < 0.85f ? WStone : WPlaster);
				H.bTimber = Wr >= 0.85f || (H.WallStyle == WPlaster && R.F() < 0.2f);
				H.WallCol = H.WallStyle == WStone ? Jit(R, LifeCol(0.62f, 0.58f, 0.51f)) : Pastel(R);
				H.Chimneys = R.F() < 0.65f ? 1 : 0;
				H.WinW = 0.8; H.WinH = 1.0; H.WinSpacing = 2.9; H.LitFrac = 0.45f;
				H.bWing = R.F() < 0.22f; H.WingW = R.R(5.f, 7.f); H.WingD = R.R(4.5f, 5.5f);
				H.Yaw = Ang(Dd) + (Sd > 0 ? PI : 0.0) + R.R(-0.08f, 0.08f);
				H.DoorFace = 0; // local -Y faces the road
				H.C = P - Perp(Dd) * (Sd * (3.5 + H.D * 0.5 + R.R(1.f, 4.f)));
				if (FVector2D::DotProduct(H.C - P, Perp(Dir2(H.Yaw))) < 0) H.Yaw += PI; // make local -Y point to the road
				if (TryPlace(*this, H, 1.2, 3.5))
				{
					Plan->B.Add(H); ++Hn;
					if (R.F() < 0.35f)
					{
						FSpec Bn; Bn.Kind = FSpec::House; Bn.Seed = R.Next(); Bn.W = R.R(10.f, 16.f); Bn.D = R.R(6.f, 8.f); Bn.Storeys = 1; Bn.StoreyH = 3.6;
						Bn.WallStyle = WWood; Bn.WallCol = Jit(R, LifeCol(0.34f, 0.25f, 0.18f), 0.1f); Bn.RoofStyle = R.F() < 0.5f ? RThatch : RMetal;
						Bn.RoofCol = Bn.RoofStyle == RThatch ? Jit(R, LifeCol(0.48f, 0.40f, 0.27f)) : Jit(R, LifeCol(0.42f, 0.30f, 0.25f)); Bn.Pitch = FMath::DegreesToRadians(Bn.RoofStyle == RThatch ? 48.0 : 30.0);
						Bn.WinSpacing = 6.0; Bn.LitFrac = 0.05f; Bn.bSills = false; Bn.Yaw = H.Yaw + (R.F() < 0.5f ? PI * 0.5 : 0.0);
						Bn.C = H.C - Perp(Dir2(H.Yaw)) * -(H.D * 0.5 + 7.0 + Bn.D * 0.5);
						if (TryPlace(*this, Bn, 1.0, 3.0)) Plan->B.Add(Bn);
					}
				}
			}
			return R.R(12.f, 20.f);
		});
	// outlying farmsteads
	for (int32 K = 0; K < 10; ++K)
	{
		const FVector2D P = C + Dir2(R.R(0.f, 2.f * PI)) * R.R(260.f, 650.f);
		FSpec H; H.Kind = FSpec::House; H.Seed = R.Next(); H.W = R.R(9.f, 13.f); H.D = R.R(5.5f, 7.f); H.Storeys = 1; H.StoreyH = 2.9;
		H.RoofStyle = RThatch; H.RoofCol = Jit(R, LifeCol(0.50f, 0.41f, 0.27f)); H.Pitch = FMath::DegreesToRadians(50.0); H.Overhang = 0.6;
		H.WallStyle = WPlaster; H.WallCol = Pastel(R); H.Chimneys = 1; H.WinW = 0.8; H.WinH = 1.0; H.LitFrac = 0.5f; H.Yaw = R.R(0.f, 2.f * PI);
		H.C = P; H.bWing = true; H.WingW = 6; H.WingD = 5;
		if (TryPlace(*this, H, 1.5, 3.0)) { Plan->B.Add(H); StampDisc(LuYard, P, 16.0, 110); }
	}
	FMemory::Memzero(GReject, sizeof(GReject));
	UE_LOG(LogTemp, Display, TEXT("LifeTown: village rejects: outside %d, water %d, slope %d, overlap %d"), GReject[0], GReject[1], GReject[2], GReject[3]);
	PlanFields(*this, C, 95.0, 820.0, 31, 0.12);
	S.Buildings = Plan->B.Num();
	Plans.Add(Plan);
}

// ====================================================================== walled town

void ALifeWorld::PlanWalledTown(FLifeSettlement& S)
{
	TSharedPtr<FTownPlan> Plan = MakeShared<FTownPlan>();
	Plan->S = Settlements.IndexOfByPredicate([&S](const FLifeSettlement& O) { return &O == &S; });
	FLifeRand R(777);
	const FVector2D C = S.C;
	const double Rw = 215.0;
	S.Radius = Rw;
	const int32 NP = 44;
	TArray<FVector2D> WallP;
	for (int32 I = 0; I < NP; ++I)
	{
		const double A = 2.0 * PI * I / NP;
		const double Rr = Rw * (1.0 + 0.13 * LifeFbm2(float(FMath::Cos(A) * 1.6), float(FMath::Sin(A) * 1.6), 3, 55));
		WallP.Add(C + Dir2(A) * Rr);
	}
	StampPoly(LuNoTree, WallP, 1);
	StampPoly(LuYard, WallP, 185);
	// gates: towards the other settlements plus one more
	TArray<int32> GateIdx;
	for (const FLifeSettlement& O : Settlements)
		if (&O != &S)
		{
			const double A = Ang(O.C - C);
			GateIdx.AddUnique(int32(FMath::RoundToInt(FMath::Fmod(A + 4.0 * PI, 2.0 * PI) / (2.0 * PI) * NP)) % NP);
		}
	GateIdx.AddUnique((GateIdx.Num() ? GateIdx[0] + NP / 2 : 0) % NP);
	for (int32 G : GateIdx) Plan->Gates.Add(WallP[G] + (WallP[G] - C).GetSafeNormal() * 12.0);

	// streets: square, main streets to the gates, ring street, lanes
	StampDisc(LuPaved, C, 30.0, 230);
	StampDisc(LuNoTree, C, 33.0, 1);
	TArray<TArray<FVector2D>> Streets;
	TArray<double> StreetW;
	for (int32 G : GateIdx)
	{
		TArray<FVector2D> P = Meander(C, WallP[G] + (WallP[G] - C).GetSafeNormal() * 10.0, 6.0, 18.0, 300 + G);
		StampRoad(P, 7.0, true);
		Streets.Add(P); StreetW.Add(7.0);
	}
	{
		TArray<FVector2D> Ring;
		for (int32 I = 0; I <= NP; ++I) Ring.Add(C + (WallP[I % NP] - C) * 0.83);
		StampRoad(Ring, 5.0, true);
		Streets.Add(Ring); StreetW.Add(5.0);
	}
	for (int32 K = 0; K < 7; ++K)
	{
		const double A = R.R(0.f, 2.f * PI);
		const int32 Wi = int32(FMath::Fmod(A + 4 * PI, 2 * PI) / (2 * PI) * NP) % NP;
		TArray<FVector2D> P = Meander(C + Dir2(A) * 34.0, C + (WallP[Wi] - C) * 0.83, 6.0, 14.0, 400 + K);
		StampRoad(P, 4.0, true);
		Streets.Add(P); StreetW.Add(4.0);
	}
	// cathedral on the square, keep at the highest point by the wall
	{
		const double A = R.R(0.f, 2.f * PI);
		FSpec Ch; Ch.Kind = FSpec::Church; Ch.Seed = R.Next(); Ch.W = 40; Ch.D = 14; Ch.Extra = 14.0; Ch.Yaw = A;
		Ch.C = C + Dir2(A + PI * 0.5) * 48.0;
		Ch.WallStyle = WStone; Ch.WallCol = LifeCol(0.70f, 0.66f, 0.58f); Ch.RoofStyle = RSlate; Ch.RoofCol = LifeCol(0.30f, 0.32f, 0.35f);
		FSpec Probe = Ch; Probe.W += 12; Probe.C -= Dir2(A) * 6.0;
		if (TryPlace(*this, Probe, 1.0, 8.0)) Plan->B.Add(Ch);
		double BestH = -1e9; FVector2D BestP = C;
		for (int32 I = 0; I < NP; ++I)
		{
			const FVector2D P = C + (WallP[I] - C) * 0.62;
			const double H = HeightAt(P.X, P.Y);
			if (H > BestH) { BestH = H; BestP = P; }
		}
		FSpec Kp; Kp.Kind = FSpec::Keep; Kp.Seed = R.Next(); Kp.W = 17; Kp.D = 17; Kp.Extra = 24; Kp.C = BestP; Kp.Yaw = Ang(BestP - C);
		Kp.WallStyle = WStone; Kp.WallCol = LifeCol(0.62f, 0.59f, 0.53f); Kp.RoofStyle = RSlate; Kp.RoofCol = LifeCol(0.30f, 0.32f, 0.35f);
		FSpec Probe2 = Kp; Probe2.W += 6; Probe2.D += 6;
		if (TryPlace(*this, Probe2, 1.0, 12.0)) Plan->B.Add(Kp);
	}
	// town houses: gable ends to the street, tightly packed
	for (int32 Si = 0; Si < Streets.Num(); ++Si)
	{
		const double Hs = StreetW[Si] * 0.5;
		WalkPolyline(Streets[Si], 0.0, 1e9, [&](const FVector2D& P, const FVector2D& Dd) -> double
		{
			const double Front = R.R(5.f, 8.f);
			if (FVector2D::Distance(P, C) < 34.0) return Front;
			for (int32 Sd = -1; Sd <= 1; Sd += 2)
			{
				FSpec H; H.Kind = FSpec::House; H.Seed = R.Next();
				H.W = R.R(9.f, 14.f); H.D = Front - 0.2; // W is depth (ridge runs away from the street)
				H.Storeys = R.I(2, 4); H.StoreyH = 3.0;
				H.Roof = FSpec::Gable; H.Pitch = FMath::DegreesToRadians(R.R(48.f, 56.f)); H.Overhang = 0.3;
				H.RoofStyle = R.F() < 0.72f ? RTile : RSlate;
				H.RoofCol = H.RoofStyle == RTile ? Jit(R, R.F() < 0.5f ? LifeCol(0.55f, 0.28f, 0.19f) : LifeCol(0.47f, 0.25f, 0.18f), 0.1f) : Jit(R, LifeCol(0.31f, 0.32f, 0.35f));
				const float Wr = R.F();
				H.WallStyle = Wr < 0.62f ? WPlaster : (Wr < 0.88f ? WStone : WBrick);
				H.WallCol = H.WallStyle == WPlaster ? Pastel(R) : (H.WallStyle == WStone ? Jit(R, LifeCol(0.66f, 0.62f, 0.55f)) : Jit(R, LifeCol(0.55f, 0.33f, 0.25f)));
				H.bTimber = H.WallStyle == WPlaster && R.F() < 0.45f;
				H.Chimneys = R.I(0, 2);
				H.WinW = 0.85; H.WinH = 1.3; H.WinSpacing = 2.4; H.LitFrac = 0.33f;
				const FVector2D Out = -Perp(Dd) * double(Sd);
				H.C = P + Out * (Hs + 0.4 + H.W * 0.5);
				H.Yaw = Ang(Out); // local +X points away from the street, so -X faces it
				H.DoorFace = 3;
				TryPlace(*this, H, 0.15, 7.0) ? (void)Plan->B.Add(H) : (void)0;
			}
			return Front;
		});
	}
	// fill the remaining interior with back-lot houses
	for (int32 K = 0; K < 500; ++K)
	{
		const FVector2D P = C + Dir2(R.R(0.f, 2.f * PI)) * (Rw * FMath::Sqrt(R.F()) * 0.78);
		FSpec H; H.Kind = FSpec::House; H.Seed = R.Next(); H.W = R.R(7.f, 11.f); H.D = R.R(5.f, 7.f); H.Storeys = R.I(1, 3); H.StoreyH = 3.0;
		H.Roof = R.F() < 0.6f ? FSpec::Gable : FSpec::Hip; H.Pitch = FMath::DegreesToRadians(R.R(45.f, 54.f)); H.RoofStyle = RTile; H.RoofCol = Jit(R, LifeCol(0.52f, 0.27f, 0.18f), 0.12f);
		H.WallStyle = WPlaster; H.WallCol = Pastel(R); H.bTimber = R.F() < 0.4f; H.Chimneys = R.I(0, 1); H.WinW = 0.8; H.WinH = 1.2; H.WinSpacing = 2.6; H.LitFrac = 0.4f;
		H.Yaw = Ang(P - C) + (R.F() < 0.5f ? 0.0 : PI * 0.5) + R.R(-0.1f, 0.1f);
		H.C = P;
		if (TryPlace(*this, H, 0.3, 7.0)) Plan->B.Add(H);
	}
	// the wall: segments, towers, gatehouses
	const FColor WallC = LifeCol(0.66f, 0.62f, 0.55f);
	for (int32 I = 0; I < NP; ++I)
	{
		FVector2D A = WallP[I], B = WallP[(I + 1) % NP];
		const bool bGa = GateIdx.Contains(I), bGb = GateIdx.Contains((I + 1) % NP);
		if (bGa) A = A + (B - A).GetSafeNormal() * 6.0;
		if (bGb) B = B + (A - B).GetSafeNormal() * 6.0;
		const double L = FVector2D::Distance(A, B);
		const int32 N = FMath::Max(1, int32(FMath::CeilToDouble(L / 9.0)));
		for (int32 K = 0; K < N; ++K)
		{
			FSpec Wl; Wl.Kind = FSpec::WallSeg; Wl.P0 = FMath::Lerp(A, B, double(K) / N); Wl.P1 = FMath::Lerp(A, B, double(K + 1) / N);
			Wl.C = (Wl.P0 + Wl.P1) * 0.5; Wl.W = FVector2D::Distance(Wl.P0, Wl.P1); Wl.D = 2.8; Wl.Extra = 9.0;
			Wl.Yaw = Ang(Wl.P1 - Wl.P0);
			if (FVector2D::DotProduct(Perp(Dir2(Wl.Yaw)), Wl.C - C) > 0) { Wl.Yaw += PI; Swap(Wl.P0, Wl.P1); } // local -Y faces outwards
			Wl.WallCol = Jit(R, WallC, 0.03f); Wl.Seed = R.Next();
			Plan->B.Add(Wl);
			const FVector2D Mid = Wl.C;
			StampRoad({ Wl.P0, Wl.P1 }, 4.0, false); // trodden ground along the wall
			int32 Ii, Jj; if (LuIndex(Mid.X, Mid.Y, Ii, Jj)) LuBuilt[Jj * LuN + Ii] = 1;
		}
		if (bGa)
		{
			const FVector2D T = (WallP[(I + 1) % NP] - WallP[(I + NP - 1) % NP]).GetSafeNormal();
			for (int32 Sd = -1; Sd <= 1; Sd += 2)
			{
				FSpec Tw; Tw.Kind = FSpec::RoundTower; Tw.C = WallP[I] + T * (Sd * 7.5); Tw.W = 10.0; Tw.Extra = 16.0; Tw.WallCol = WallC; Tw.RoofStyle = RTile; Tw.RoofCol = LifeCol(0.50f, 0.26f, 0.18f); Tw.Seed = R.Next();
				Plan->B.Add(Tw);
			}
			FSpec Gh; Gh.Kind = FSpec::Gatehouse; Gh.C = WallP[I]; Gh.Yaw = Ang(T); Gh.W = 12.0; Gh.D = 6.0; Gh.Extra = 11.0; Gh.WallCol = WallC; Gh.Seed = R.Next();
			Plan->B.Add(Gh);
		}
		else if (I % 4 == 0)
		{
			FSpec Tw; Tw.Kind = FSpec::RoundTower; Tw.C = WallP[I]; Tw.W = R.R(8.5f, 10.f); Tw.Extra = R.R(13.f, 16.f); Tw.WallCol = Jit(R, WallC, 0.03f);
			Tw.RoofStyle = R.F() < 0.6f ? RTile : RSlate; Tw.RoofCol = Tw.RoofStyle == RTile ? LifeCol(0.50f, 0.26f, 0.18f) : LifeCol(0.30f, 0.32f, 0.35f); Tw.Seed = R.Next();
			Plan->B.Add(Tw);
		}
	}
	// suburbs along the roads outside the gates
	for (int32 G = 0; G < Plan->Gates.Num(); ++G)
	{
		const FVector2D Gp = Plan->Gates[G], Gd = (Gp - C).GetSafeNormal();
		TArray<FVector2D> Out = Meander(Gp, Gp + Gd * 260.0, 8.0, 15.0, 500 + G);
		StampRoad(Out, 5.0, false);
		WalkPolyline(Out, 25.0, 230.0, [&](const FVector2D& P, const FVector2D& Dd) -> double
		{
			for (int32 Sd = -1; Sd <= 1; Sd += 2)
			{
				if (R.F() < 0.35f) continue;
				FSpec H; H.Kind = FSpec::House; H.Seed = R.Next(); H.W = R.R(7.f, 10.f); H.D = R.R(5.f, 6.5f); H.Storeys = R.I(1, 2); H.StoreyH = 2.8;
				H.Roof = FSpec::Gable; H.Pitch = FMath::DegreesToRadians(R.R(44.f, 52.f)); H.RoofStyle = R.F() < 0.5f ? RThatch : RTile;
				H.RoofCol = H.RoofStyle == RThatch ? Jit(R, LifeCol(0.50f, 0.41f, 0.27f)) : Jit(R, LifeCol(0.52f, 0.27f, 0.18f)); H.Overhang = H.RoofStyle == RThatch ? 0.6 : 0.35;
				H.WallStyle = WPlaster; H.WallCol = Pastel(R); H.bTimber = R.F() < 0.3f; H.Chimneys = 1; H.WinW = 0.8; H.WinH = 1.0; H.LitFrac = 0.4f;
				H.Yaw = Ang(Dd) + (Sd > 0 ? PI : 0.0);
				H.C = P - Perp(Dd) * (Sd * (3.0 + H.D * 0.5 + R.R(0.5f, 3.f)));
				if (FVector2D::DotProduct(H.C - P, Perp(Dir2(H.Yaw))) < 0) H.Yaw += PI;
				if (TryPlace(*this, H, 1.0, 4.0)) Plan->B.Add(H);
			}
			return R.R(14.f, 24.f);
		});
	}
	PlanFields(*this, C, Rw + 90.0, Rw + 650.0, 41, 0.15);
	S.Buildings = Plan->B.Num();
	Plans.Add(Plan);
}

// ====================================================================== industrial port city

void ALifeWorld::PlanCity(FLifeSettlement& S)
{
	TSharedPtr<FTownPlan> Plan = MakeShared<FTownPlan>();
	Plan->S = Settlements.IndexOfByPredicate([&S](const FLifeSettlement& O) { return &O == &S; });
	FLifeRand R(9001);
	const FVector2D C = S.C;
	// seaward direction
	double BestA = 0, BestH = 1e9;
	for (int32 K = 0; K < 36; ++K)
	{
		const double A = K * PI / 18.0;
		double Hs = 0; for (double Rr = 250; Rr <= 650; Rr += 100) Hs += BaseHeightAt(C.X + Rr * FMath::Cos(A), C.Y + Rr * FMath::Sin(A));
		if (Hs < BestH) { BestH = Hs; BestA = A; }
	}
	FMemory::Memzero(GReject, sizeof(GReject));
	const FVector2D Sea = Dir2(BestA), V = -Sea, U = Perp(V);
	S.Axis = Ang(U);
	S.Radius = 900.0;
	const double Bu = 92, Bv = 72, Sw = 14, Av = 22;
	auto LineU = [&](int32 K) { return K * (Bu + Sw) + (K % 4 == 0 ? 0.0 : 0.0); };
	auto LineV = [&](int32 J) { return J * (Bv + Sw); };
	const int32 K0 = -7, K1 = 7, J0 = -3, J1 = 10;
	auto W2 = [&](double Uu, double Vv) { return C + U * Uu + V * Vv; };
	auto Land = [&](const FVector2D& P) { return HeightAt(P.X, P.Y) > 1.6f; };
	auto InCity = [&](double Uu, double Vv) { return (Uu * Uu) / (820.0 * 820.0) + ((Vv - 300) * (Vv - 300)) / (700.0 * 700.0) < 1.0; };
	// streets
	TArray<TPair<FVector2D, FVector2D>> StreetSegs;
	TArray<bool> StreetAv;
	for (int32 J = J0; J <= J1; ++J)
		for (int32 K = K0; K < K1; ++K)
		{
			const double Vv = LineV(J), Ua = LineU(K), Ub = LineU(K + 1);
			const FVector2D A = W2(Ua, Vv), B = W2(Ub, Vv);
			if (!InCity((Ua + Ub) * 0.5, Vv) || !Land(A) || !Land(B) || !Land((A + B) * 0.5)) continue;
			StreetSegs.Add({ A, B }); StreetAv.Add(J == 4);
		}
	for (int32 K = K0; K <= K1; ++K)
		for (int32 J = J0; J < J1; ++J)
		{
			const double Uu = LineU(K), Va = LineV(J), Vb = LineV(J + 1);
			const FVector2D A = W2(Uu, Va), B = W2(Uu, Vb);
			if (!InCity(Uu, (Va + Vb) * 0.5) || !Land(A) || !Land(B) || !Land((A + B) * 0.5)) continue;
			StreetSegs.Add({ A, B }); StreetAv.Add(K == 0);
		}
	for (int32 Si = 0; Si < StreetSegs.Num(); ++Si)
	{
		const TPair<FVector2D, FVector2D>& Sg = StreetSegs[Si];
		const bool bAvenue = StreetAv[Si]; // the avenues are the line u = 0 and the line v = LineV(4)
		StampRoad({ Sg.Key, Sg.Value }, bAvenue ? Av : Sw, true);
		// lamps on both sides every ~32 m
		const FVector2D Sd2 = (Sg.Value - Sg.Key).GetSafeNormal();
		const double L = FVector2D::Distance(Sg.Key, Sg.Value);
		for (double T = 10; T < L - 6; T += 32)
			for (int32 Sd = -1; Sd <= 1; Sd += 2)
			{
				FSpec Lp; Lp.Kind = FSpec::Lamp; Lp.C = Sg.Key + Sd2 * T + Perp(Sd2) * (Sd * ((bAvenue ? Av : Sw) * 0.5 - 0.8)); Lp.Yaw = Ang(-Perp(Sd2) * double(Sd));
				Plan->B.Add(Lp);
			}
	}
	// blocks
	int32 Nb = 0;
	const int32 CivicK = 0, CivicJ = 4;
	for (int32 J = J0; J < J1; ++J)
		for (int32 K = K0; K < K1; ++K)
		{
			auto Half = [&](bool bAv) { return (bAv ? Av : Sw) * 0.5 + 0.8; };
			const double Ua = LineU(K) + Half(K == 0), Ub = LineU(K + 1) - Half(K + 1 == 0);
			const double Va = LineV(J) + Half(J == 4), Vb = LineV(J + 1) - Half(J + 1 == 4);
			const double Uc = (Ua + Ub) * 0.5, Vc = (Va + Vb) * 0.5;
			if (!InCity(Uc, Vc)) continue;
			const FVector2D Bc = W2(Uc, Vc);
			if (!Land(Bc)) continue;
			const bool bIndustrial = Vc < 200.0 && FMath::Abs(Uc) > 120.0;
			const bool bDowntown = FMath::Abs(Uc) < 330.0 && Vc > 180.0 && Vc < 640.0;
			if (!bIndustrial && R.F() < 0.06f) { Hedges.Add(FVector4d(W2(Ua, Va).X, W2(Ua, Va).Y, W2(Ub, Va).X, W2(Ub, Va).Y)); Hedges.Add(FVector4d(W2(Ua, Vb).X, W2(Ua, Vb).Y, W2(Ub, Vb).X, W2(Ub, Vb).Y)); continue; } // a park
			++Nb;
			StampPoly(LuNoTree, { W2(Ua, Va), W2(Ub, Va), W2(Ub, Vb), W2(Ua, Vb) }, 1);
			StampPoly(LuYard, { W2(Ua, Va), W2(Ub, Va), W2(Ub, Vb), W2(Ua, Vb) }, 165);
			if (K == CivicK && J == CivicJ)
			{
				FSpec Cv; Cv.Kind = FSpec::House; Cv.Seed = R.Next(); Cv.C = Bc; Cv.Yaw = Ang(U); Cv.W = (Ub - Ua) * 0.8; Cv.D = (Vb - Va) * 0.7; Cv.Storeys = 3; Cv.StoreyH = 5.0;
				Cv.Roof = FSpec::Hip; Cv.Pitch = FMath::DegreesToRadians(30.0); Cv.RoofStyle = RSlate; Cv.RoofCol = LifeCol(0.28f, 0.34f, 0.33f);
				Cv.WallStyle = WStone; Cv.WallCol = LifeCol(0.78f, 0.75f, 0.68f); Cv.WinW = 1.6; Cv.WinH = 3.0; Cv.WinSpacing = 4.0; Cv.LitFrac = 0.7f; Cv.LitCol = FLinearColor(1.f, 0.85f, 0.6f);
				if (TryPlace(*this, Cv, 0.5, 6.0)) Plan->B.Add(Cv);
				FSpec Ct; Ct.Kind = FSpec::ClockTower; Ct.C = Bc + V * (Cv.D * 0.5 + 4.0) * -1.0; Ct.Yaw = Cv.Yaw; Ct.W = 11; Ct.D = 11; Ct.Extra = 48; Ct.WallStyle = WStone; Ct.WallCol = Cv.WallCol;
				Ct.RoofStyle = RSlate; Ct.RoofCol = Cv.RoofCol; Ct.Seed = R.Next();
				Plan->B.Add(Ct);
				continue;
			}
			if (bIndustrial)
			{
				// one or two big sheds, a chimney stack, a warehouse
				const int32 Nf = R.I(1, 2);
				for (int32 F = 0; F < Nf; ++F)
				{
					FSpec Fc; Fc.Kind = FSpec::House; Fc.Seed = R.Next(); Fc.Yaw = Ang(U);
					Fc.W = (Ub - Ua) * R.R(0.55f, 0.85f); Fc.D = (Vb - Va) * (Nf == 1 ? R.R(0.6f, 0.8f) : 0.42);
					Fc.C = W2(Uc + R.R(-5.f, 5.f), Nf == 1 ? Vc : (F == 0 ? Va + Fc.D * 0.5 + 1 : Vb - Fc.D * 0.5 - 1));
					Fc.Storeys = 1; Fc.StoreyH = R.R(8.f, 11.f);
					Fc.Roof = R.F() < 0.6f ? FSpec::Sawtooth : FSpec::Gable; Fc.Pitch = FMath::DegreesToRadians(20.0);
					Fc.RoofStyle = RMetal; Fc.RoofCol = Jit(R, R.F() < 0.5f ? LifeCol(0.42f, 0.42f, 0.43f) : LifeCol(0.45f, 0.30f, 0.25f));
					Fc.WallStyle = WBrick; Fc.WallCol = Jit(R, LifeCol(0.50f, 0.30f, 0.23f)); Fc.bSills = false;
					Fc.WinW = 2.4; Fc.WinH = 3.2; Fc.WinSpacing = 5.0; Fc.LitFrac = 0.22f; Fc.LitCol = FLinearColor(0.55f, 0.62f, 0.7f);
					if (TryPlace(*this, Fc, 0.5, 5.0))
					{
						Plan->B.Add(Fc);
						if (R.F() < 0.75f)
						{
							FSpec Sk; Sk.Kind = FSpec::Stack; Sk.Seed = R.Next(); Sk.W = R.R(3.6f, 5.f); Sk.Extra = R.R(42.f, 68.f); Sk.WallCol = Jit(R, LifeCol(0.48f, 0.29f, 0.22f));
							Sk.C = Fc.C + U * (Fc.W * 0.5 + 4.5) * (R.F() < 0.5f ? -1.0 : 1.0);
							Sk.D = Sk.W;
							if (TryPlace(*this, Sk, 0.2, 6.0)) Plan->B.Add(Sk);
						}
					}
				}
				continue;
			}
			// perimeter block
			const int32 Smin = bDowntown ? 5 : 3, Smax = bDowntown ? 11 : 6;
			const FVector2D Corners[4] = { W2(Ua, Va), W2(Ub, Va), W2(Ub, Vb), W2(Ua, Vb) };
			const float StyleR = R.F();
			for (int32 E = 0; E < 4; ++E)
			{
				const FVector2D A = Corners[E], B = Corners[(E + 1) % 4];
				const FVector2D Ed = (B - A).GetSafeNormal();
				const FVector2D In = (Bc - (A + B) * 0.5).GetSafeNormal();
				const double L = FVector2D::Distance(A, B);
				double T = 0.0;
				while (T < L - 6.0)
				{
					FSpec H; H.Kind = FSpec::House; H.Seed = R.Next();
					const double Fr = FMath::Min(double(R.R(12.f, 26.f)), L - T);
					H.W = Fr; H.D = R.R(11.f, 15.f);
					H.C = A + Ed * (T + Fr * 0.5) + In * (H.D * 0.5 + 0.6);
					H.Yaw = Ang(Ed); if (FVector2D::DotProduct(Perp(Dir2(H.Yaw)), In) < 0) H.Yaw += PI; // local -Y faces the street
					H.DoorFace = 0;
					H.Storeys = R.I(Smin, Smax); H.StoreyH = 3.3;
					const float Rr = R.F();
					H.Roof = bDowntown ? (Rr < 0.8f ? FSpec::Flat : FSpec::Hip) : (Rr < 0.5f ? FSpec::Flat : (Rr < 0.8f ? FSpec::Hip : FSpec::Gable));
					H.Pitch = FMath::DegreesToRadians(H.Roof == FSpec::Gable ? 38.0 : 32.0);
					H.RoofStyle = R.F() < 0.6f ? RSlate : RTile;
					H.RoofCol = H.RoofStyle == RSlate ? Jit(R, LifeCol(0.30f, 0.31f, 0.34f)) : Jit(R, LifeCol(0.50f, 0.28f, 0.20f));
					const float Ws = (StyleR + R.R(-0.25f, 0.25f));
					H.WallStyle = Ws < 0.45f ? WBrick : (Ws < 0.8f ? WStone : (bDowntown ? WConcrete : WPlaster));
					H.WallCol = H.WallStyle == WBrick ? Jit(R, R.F() < 0.5f ? LifeCol(0.52f, 0.31f, 0.23f) : LifeCol(0.45f, 0.27f, 0.21f), 0.08f)
						: H.WallStyle == WStone ? Jit(R, LifeCol(0.74f, 0.70f, 0.62f)) : H.WallStyle == WConcrete ? Jit(R, LifeCol(0.62f, 0.62f, 0.60f)) : Pastel(R);
					H.WinW = 1.2; H.WinH = 1.7; H.WinSpacing = 3.1; H.LitFrac = bDowntown ? 0.38f : 0.28f; H.bSills = H.Storeys < 7;
					H.LitCol = R.F() < 0.25f ? FLinearColor(0.85f, 0.9f, 1.f) : FLinearColor(1.f, 0.72f, 0.42f);
					H.Chimneys = H.Roof == FSpec::Flat ? 0 : R.I(1, 3);
					if (TryPlace(*this, H, 0.1, 6.0)) Plan->B.Add(H);
					T += Fr + 0.3;
				}
			}
		}
	// docks: piers into the sea along the coast, cranes, warehouses and ships
	for (double Uu = -720; Uu <= 720; Uu += R.R(110.f, 150.f))
	{
		FVector2D Shore; bool bFound = false;
		for (double Vv = 300; Vv > -700; Vv -= 5)
		{
			const FVector2D P = W2(Uu, Vv);
			if (HeightAt(P.X, P.Y) < 0.3f) { Shore = P; bFound = true; break; }
		}
		if (!bFound) continue;
		const double Len = R.R(110.f, 180.f);
		const FVector2D End = Shore + Sea * Len;
		if (HeightAt(End.X, End.Y) > -3.f) continue;
		FSpec Pr; Pr.Kind = FSpec::Pier; Pr.C = (Shore - Sea * 8.0 + End) * 0.5; Pr.Yaw = BestA; Pr.W = Len + 8.0; Pr.D = 15.0; Pr.Seed = R.Next();
		Plan->B.Add(Pr);
		StampRoad({ Shore - Sea * 40.0, Shore }, 15.0, true);
		FSpec Cr; Cr.Kind = FSpec::Crane; Cr.C = Shore + Sea * R.R(25.f, 60.f) + U * 3.0; Cr.Yaw = BestA + PI * 0.5 * (R.F() < 0.5f ? 1 : -1); Cr.Seed = R.Next();
		Plan->B.Add(Cr);
		if (R.F() < 0.7f)
		{
			FSpec Sh; Sh.Kind = FSpec::Ship; Sh.W = R.R(60.f, 95.f); Sh.D = R.R(11.f, 15.f); Sh.Seed = R.Next();
			Sh.C = Shore + Sea * (Len * 0.55) + U * ((R.F() < 0.5f ? -1.0 : 1.0) * (7.5 + Sh.D * 0.5 + 2.0)); Sh.Yaw = BestA + (R.F() < 0.5f ? PI : 0.0);
			if (HeightAt(Sh.C.X, Sh.C.Y) < -4.f) Plan->B.Add(Sh);
		}
		FSpec Wh; Wh.Kind = FSpec::House; Wh.Seed = R.Next(); Wh.W = R.R(45.f, 70.f); Wh.D = R.R(20.f, 26.f); Wh.Storeys = 1; Wh.StoreyH = 9.0;
		Wh.C = Shore - Sea * (Wh.D * 0.5 + 18.0) + U * R.R(-20.f, 20.f); Wh.Yaw = Ang(U);
		Wh.Roof = FSpec::Gable; Wh.Pitch = FMath::DegreesToRadians(18.0); Wh.RoofStyle = RMetal; Wh.RoofCol = Jit(R, LifeCol(0.40f, 0.41f, 0.43f));
		Wh.WallStyle = R.F() < 0.6f ? WBrick : WConcrete; Wh.WallCol = Wh.WallStyle == WBrick ? Jit(R, LifeCol(0.48f, 0.29f, 0.22f)) : Jit(R, LifeCol(0.6f, 0.6f, 0.58f));
		Wh.WinW = 3.5; Wh.WinH = 4.5; Wh.WinSpacing = 8.0; Wh.LitFrac = 0.2f; Wh.bSills = false; Wh.LitCol = FLinearColor(0.85f, 0.9f, 1.f);
		if (TryPlace(*this, Wh, 0.5, 4.0)) Plan->B.Add(Wh);
	}
	S.Buildings = Plan->B.Num();
	UE_LOG(LogTemp, Display, TEXT("LifeTown: city blocks %d; rejects: outside %d, water %d, slope %d, built %d, street %d"), Nb, GReject[0], GReject[1], GReject[2], GReject[3], GReject[4]);
	Plans.Add(Plan);
}

// ====================================================================== meshes

void ALifeWorld::BuildSettlements()
{
	const double Chunk = 300.0;
	TArray<UMaterialInterface*> Mats = { MatWall, MatRoof, MatWindow, MatLamp, MatGlow };
	for (const TSharedPtr<FTownPlan>& Plan : Plans)
	{
		TMap<FIntPoint, TUniquePtr<FLifeMesh>> Chunks;
		TMap<FIntPoint, FVector3d> Origins;
		int32 Nb = 0;
		for (const FSpec& S : Plan->B)
		{
			const FIntPoint Key(FMath::FloorToInt(S.C.X / Chunk), FMath::FloorToInt(S.C.Y / Chunk));
			if (!Chunks.Contains(Key))
			{
				Chunks.Add(Key, MakeUnique<FLifeMesh>(5));
				Origins.Add(Key, SurfaceM((Key.X + 0.5) * Chunk, (Key.Y + 0.5) * Chunk, 0.0));
			}
			FLifeMesh& M = *Chunks[Key];
			FLifeRand R(S.Seed ? S.Seed : uint32(Nb * 7919 + 13));
			// ground under the footprint
			const FVector2D Ux = Dir2(S.Yaw), Uy = Perp(Ux);
			float Gmin = 1e9f, Gmax = -1e9f;
			const double Hw = S.W * 0.5, Hd = S.D * 0.5 + (S.bWing ? S.WingW : 0.0);
			for (int32 K = 0; K < 5; ++K)
			{
				const FVector2D P = K == 4 ? S.C : S.C + Ux * (K & 1 ? Hw : -Hw) + Uy * (K & 2 ? Hd : -(S.D * 0.5));
				const float H = HeightAt(P.X, P.Y);
				Gmin = FMath::Min(Gmin, H); Gmax = FMath::Max(Gmax, H);
			}
			double Base = Gmax + 0.25;
			double Zf = Gmin - Base - 0.6;
			FLifeMesh Tmp(5);
			switch (S.Kind)
			{
			case FSpec::House: GenHouse(Tmp, S, Zf, R); ++Nb; break;
			case FSpec::Church: GenChurch(Tmp, S, Zf, R); ++Nb; break;
			case FSpec::Keep: GenKeep(Tmp, S, Zf, R); ++Nb; break;
			case FSpec::RoundTower:
			{
				const float Hc = HeightAt(S.C.X, S.C.Y);
				Base = Hc; Zf = Gmin - Base - 2.0;
				GenRoundTower(Tmp, S, Zf); break;
			}
			case FSpec::ClockTower:
			{
				FSpec T = S; T.Storeys = 1; T.StoreyH = S.Extra; T.WinW = 1.0; T.WinH = 2.0; T.WinSpacing = 3.5; T.LitFrac = 0.4f; T.bSills = false; T.Kind = FSpec::ClockTower;
				WallsAndWindows(Tmp, T, Zf, S.Extra, R);
				Tmp.Box(SW, FVector3f(0, 0, float(S.Extra) + 0.3f), FVector3f(float(S.W * 0.5) + 0.4f, float(S.D * 0.5) + 0.4f, 0.3f), 0.f, LifeColMul(S.WallCol, 0.85f), St(WStone));
				for (int32 F = 0; F < 4; ++F)
				{
					const float A = F * PI * 0.5f;
					const FVector3f N(FMath::Cos(A), FMath::Sin(A), 0);
					Tmp.Box(SL, N * float(S.W * 0.5 + 0.06) + FVector3f(0, 0, float(S.Extra) - 5.f), FVector3f(1.6f, 0.03f, 1.6f), A + PI * 0.5f, LifeCol(1.f, 0.9f, 0.7f), FVector2f(0, 0));
				}
				Tmp.Pyramid(SR, FVector3f(0, 0, float(S.Extra) + 0.6f), float(S.W * 0.55), float(S.D * 0.55), 0.f, float(S.W) * 1.5f, S.RoofCol, St(RSlate));
				++Nb; break;
			}
			case FSpec::WallSeg:
			{
				const float H0 = HeightAt(S.P0.X, S.P0.Y), H1 = HeightAt(S.P1.X, S.P1.Y), Hc = HeightAt(S.C.X, S.C.Y);
				Base = Hc;
				GenWallSeg(Tmp, S, H0 - Hc, H1 - Hc);
				break;
			}
			case FSpec::Gatehouse:
			{
				Base = HeightAt(S.C.X, S.C.Y);
				const float L = float(S.W * 0.5), T = float(S.D * 0.5), H = float(S.Extra);
				Tmp.Box(SW, FVector3f(0, 0, H - 2.f), FVector3f(L, T, 2.f), 0.f, S.WallCol, St(WStone), true);
				Merlons(Tmp, FVector3f(-L, -T, 0), FVector3f(L, -T, 0), FVector3f(0, 0.2f, 0), H, S.WallCol);
				Merlons(Tmp, FVector3f(-L, T, 0), FVector3f(L, T, 0), FVector3f(0, -0.2f, 0), H, S.WallCol);
				Tmp.Box(SL, FVector3f(0, -T - 0.05f, H - 5.f), FVector3f(0.3f, 0.05f, 0.3f), 0.f, LifeCol(1.f, 0.6f, 0.3f), FVector2f(0, 0));
				break;
			}
			case FSpec::Pier: Base = 0.0; GenPier(Tmp, S); break;
			case FSpec::Crane: Base = FMath::Max(0.0, double(HeightAt(S.C.X, S.C.Y))) + (HeightAt(S.C.X, S.C.Y) < 0.5f ? 2.3 : 0.0); GenCrane(Tmp, S, R); break;
			case FSpec::Ship: Base = 0.0; GenShip(Tmp, S, R); break;
			case FSpec::Stack: Base = HeightAt(S.C.X, S.C.Y); GenStack(Tmp, S, Gmin - Base - 1.0); break;
			case FSpec::Lamp: Base = HeightAt(S.C.X, S.C.Y); GenLamp(Tmp); break;
			}
			for (const FLifeVert& Vx : Tmp.V)
				if (Vx.P.ContainsNaN()) { UE_LOG(LogTemp, Warning, TEXT("LifeTown: NaN in kind %d roof %d W %.1f D %.1f"), int32(S.Kind), int32(S.Roof), S.W, S.D); break; }
			const FQuat4d Q = UpQuat(S.C.X, S.C.Y) * FQuat4d(FVector3d(0, 0, 1), S.Yaw);
			const FVector3d T = SurfaceM(S.C.X, S.C.Y, Base) - Origins[Key];
			M.Append(Tmp, FTransform3f(FQuat4f(Q), FVector3f(T)));
		}
		TownBuildings += Nb;
		int32 Ci = 0;
		for (TPair<FIntPoint, TUniquePtr<FLifeMesh>>& Kv : Chunks)
		{
			TownTris += Kv.Value->NumTris();
			Settlements[Plan->S].Tris += Kv.Value->NumTris();
			AddMeshComp(FString::Printf(TEXT("Town_%s_%d"), *Settlements[Plan->S].Name, Ci++), *Kv.Value, Mats, Origins[Kv.Key], true);
		}
		UE_LOG(LogTemp, Display, TEXT("LifeTown: %s built: %d buildings, %d specs, %d tris, %d chunks"), *Settlements[Plan->S].Name, Nb, Plan->B.Num(), Settlements[Plan->S].Tris, Chunks.Num());
	}
}
