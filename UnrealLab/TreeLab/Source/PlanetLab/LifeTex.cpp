#include "LifeTex.h"
#include "LifeMesh.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Async/ParallelFor.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"

namespace
{
	// ------------------------------------------------------------ periodic noise
	float PHash(int32 X, int32 Y, int32 P, uint32 S)
	{
		X = ((X % P) + P) % P; Y = ((Y % P) + P) % P;
		return LifeHash01(X, Y, S);
	}
	/** Value noise, period P cells, X/Y in cell units. Returns -1..1. */
	float PNoise(float X, float Y, int32 P, uint32 S)
	{
		const int32 Xi = FMath::FloorToInt(X), Yi = FMath::FloorToInt(Y);
		const float Fx = X - Xi, Fy = Y - Yi;
		const float Ux = Fx * Fx * (3.f - 2.f * Fx), Uy = Fy * Fy * (3.f - 2.f * Fy);
		const float A = PHash(Xi, Yi, P, S), B = PHash(Xi + 1, Yi, P, S), C = PHash(Xi, Yi + 1, P, S), D = PHash(Xi + 1, Yi + 1, P, S);
		return FMath::Lerp(FMath::Lerp(A, B, Ux), FMath::Lerp(C, D, Ux), Uy) * 2.f - 1.f;
	}
	/** Periodic fbm over a unit square (U,V in 0..1), base frequency Fx x Fy cells. */
	float PFbm(float U, float V, int32 Fx, int32 Fy, int32 Oct, uint32 S)
	{
		float Sum = 0.f, Amp = 0.5f, Norm = 0.f;
		for (int32 O = 0; O < Oct; ++O)
		{
			// anisotropic periods: use a common period on a stretched lattice
			const int32 Px = Fx << O, Py = Fy << O;
			const float X = U * Px, Y = V * Py;
			// emulate two periods with one hash: hash on (X mod Px, Y mod Py)
			const int32 Xi = FMath::FloorToInt(X), Yi = FMath::FloorToInt(Y);
			const float fx = X - Xi, fy = Y - Yi;
			const float ux = fx * fx * (3.f - 2.f * fx), uy = fy * fy * (3.f - 2.f * fy);
			auto H = [&](int32 a, int32 b) { a = ((a % Px) + Px) % Px; b = ((b % Py) + Py) % Py; return LifeHash01(a, b, S + O * 977u); };
			const float n = FMath::Lerp(FMath::Lerp(H(Xi, Yi), H(Xi + 1, Yi), ux), FMath::Lerp(H(Xi, Yi + 1), H(Xi + 1, Yi + 1), ux), uy) * 2.f - 1.f;
			Sum += Amp * n; Norm += Amp; Amp *= 0.5f;
		}
		return Sum / Norm;
	}
	/** Periodic cellular noise: returns F1 distance (in cell units) and the cell hash. */
	float PCell(float U, float V, int32 P, uint32 S, float* OutId = nullptr, float* OutF2 = nullptr)
	{
		const float X = U * P, Y = V * P;
		const int32 Xi = FMath::FloorToInt(X), Yi = FMath::FloorToInt(Y);
		float F1 = 9.f, F2 = 9.f, Id = 0.f;
		for (int32 dy = -1; dy <= 1; ++dy)
			for (int32 dx = -1; dx <= 1; ++dx)
			{
				const int32 Cx = Xi + dx, Cy = Yi + dy;
				const float Jx = PHash(Cx, Cy, P, S), Jy = PHash(Cx, Cy, P, S + 17u);
				const float Px = Cx + 0.1f + 0.8f * Jx, Py = Cy + 0.1f + 0.8f * Jy;
				const float D = FMath::Sqrt((X - Px) * (X - Px) + (Y - Py) * (Y - Py));
				if (D < F1) { F2 = F1; F1 = D; Id = PHash(Cx, Cy, P, S + 33u); }
				else if (D < F2) F2 = D;
			}
		if (OutId) *OutId = Id;
		if (OutF2) *OutF2 = F2;
		return F1;
	}

	// ------------------------------------------------------------ RGBA float canvas with straight alpha
	struct FLeafCanvas
	{
		int32 W = 0, H = 0;
		TArray<FLinearColor> P;
		FLeafCanvas(int32 InW, int32 InH) : W(InW), H(InH) { P.Init(FLinearColor(0, 0, 0, 0), W * H); }
		void Over(int32 X, int32 Y, const FLinearColor& C, float A)
		{
			if (X < 0 || Y < 0 || X >= W || Y >= H || A <= 0.f) return;
			FLinearColor& D = P[Y * W + X];
			const float Ao = A + D.A * (1.f - A);
			if (Ao <= 1e-6f) return;
			D.R = (C.R * A + D.R * D.A * (1.f - A)) / Ao;
			D.G = (C.G * A + D.G * D.A * (1.f - A)) / Ao;
			D.B = (C.B * A + D.B * D.A * (1.f - A)) / Ao;
			D.A = Ao;
		}
	};

	/** Tapered line with AA (clipped to a rect). */
	void DrawLine(FLeafCanvas& Cv, const FIntRect& Clip, FVector2f A, FVector2f B, float W0, float W1, const FLinearColor& C0, const FLinearColor& C1)
	{
		const float Wm = FMath::Max(W0, W1) * 0.5f + 1.f;
		const int32 X0 = FMath::Max(Clip.Min.X, FMath::FloorToInt(FMath::Min(A.X, B.X) - Wm)), X1 = FMath::Min(Clip.Max.X - 1, FMath::CeilToInt(FMath::Max(A.X, B.X) + Wm));
		const int32 Y0 = FMath::Max(Clip.Min.Y, FMath::FloorToInt(FMath::Min(A.Y, B.Y) - Wm)), Y1 = FMath::Min(Clip.Max.Y - 1, FMath::CeilToInt(FMath::Max(A.Y, B.Y) + Wm));
		const FVector2f D = B - A;
		const float L2 = FMath::Max(D.SizeSquared(), 1e-6f);
		for (int32 Y = Y0; Y <= Y1; ++Y)
			for (int32 X = X0; X <= X1; ++X)
			{
				const FVector2f Pp(X + 0.5f, Y + 0.5f);
				const float T = FMath::Clamp(FVector2f::DotProduct(Pp - A, D) / L2, 0.f, 1.f);
				const float Dist = (Pp - (A + D * T)).Size();
				const float Hw = 0.5f * FMath::Lerp(W0, W1, T);
				const float Cov = FMath::Clamp(Hw - Dist + 0.5f, 0.f, 1.f);
				if (Cov > 0.f) Cv.Over(X, Y, FMath::Lerp(C0, C1, T), Cov);
			}
	}

	/** A leaf blade: base point, unit direction, length and half-width in pixels. Pointed tip, rounded base. */
	void DrawLeaf(FLeafCanvas& Cv, const FIntRect& Clip, FVector2f Base, FVector2f Dir, float Len, float HalfW, const FLinearColor& Col, float Tip = 0.7f, float Seed = 0.f)
	{
		Dir = Dir.GetSafeNormal();
		const FVector2f Perp(-Dir.Y, Dir.X);
		const FVector2f Tipp = Base + Dir * Len;
		const float M = HalfW + 2.f;
		const int32 X0 = FMath::Max(Clip.Min.X, FMath::FloorToInt(FMath::Min(Base.X, Tipp.X) - M)), X1 = FMath::Min(Clip.Max.X - 1, FMath::CeilToInt(FMath::Max(Base.X, Tipp.X) + M));
		const int32 Y0 = FMath::Max(Clip.Min.Y, FMath::FloorToInt(FMath::Min(Base.Y, Tipp.Y) - M)), Y1 = FMath::Min(Clip.Max.Y - 1, FMath::CeilToInt(FMath::Max(Base.Y, Tipp.Y) + M));
		for (int32 Y = Y0; Y <= Y1; ++Y)
			for (int32 X = X0; X <= X1; ++X)
			{
				const FVector2f Q = FVector2f(X + 0.5f, Y + 0.5f) - Base;
				const float A = FVector2f::DotProduct(Q, Dir) / Len;
				const float B = FVector2f::DotProduct(Q, Perp);
				if (A < -0.02f || A > 1.02f) continue;
				const float Ac = FMath::Clamp(A, 0.f, 1.f);
				const float Wa = HalfW * FMath::Pow(FMath::Sin(PI * FMath::Clamp(Ac * 0.92f + 0.04f, 0.f, 1.f)), Tip) * (1.f - 0.25f * Ac);
				const float Cov = FMath::Clamp(Wa - FMath::Abs(B) + 0.5f, 0.f, 1.f) * FMath::Clamp((A + 0.02f) * Len, 0.f, 1.f) * FMath::Clamp((1.02f - A) * Len, 0.f, 1.f);
				if (Cov <= 0.f) continue;
				const float Rel = FMath::Abs(B) / FMath::Max(Wa, 0.5f);
				float Shade = 0.82f + 0.28f * (1.f - Rel * Rel);                 // rounded blade
				Shade *= (B > 0.f) ? 1.04f : 0.94f;                               // two halves catch light differently
				if (FMath::Abs(B) < 0.9f && Ac < 0.92f) Shade *= 1.18f;            // midrib
				Shade *= 0.88f + 0.18f * Ac;                                       // darker near the stalk
				Shade *= 0.96f + 0.08f * LifeHash01(X, Y, uint32(Seed * 1000.f)); // grain
				Cv.Over(X, Y, Col * Shade, Cov);
			}
	}

	FLinearColor SRGB(float R, float G, float B) { return FLinearColor(R, G, B, 1.f); } // canvas holds sRGB values

	FVector2f Bezier(FVector2f A, FVector2f C, FVector2f B, float T)
	{
		const float U = 1.f - T;
		return A * (U * U) + C * (2.f * U * T) + B * (T * T);
	}

	// ------------------------------------------------------------ leaf atlas painters (tile rect R, tile size T px)
	void PaintBroadleaf(FLeafCanvas& Cv, const FIntRect& R, FLifeRand& Rn)
	{
		const float T = float(R.Width());
		const FVector2f O(R.Min.X, R.Min.Y);
		auto Px = [&](float S, float Tt) { return O + FVector2f((S + 0.5f) * T, (1.f - Tt) * T); };
		const FLinearColor Twig = SRGB(0.30f, 0.24f, 0.17f);
		// main twig as a bezier
		const FVector2f A = Px(0.f, 0.02f), B = Px(Rn.R(-0.06f, 0.06f), 0.86f), C = Px(Rn.R(-0.12f, 0.12f), 0.45f);
		struct FTw { FVector2f A, C, B; float W; };
		TArray<FTw> Twigs;
		Twigs.Add({ A, C, B, 7.f });
		const int32 NSide = Rn.I(6, 8);
		for (int32 I = 0; I < NSide; ++I)
		{
			const float Tt = FMath::Lerp(0.12f, 0.72f, (I + Rn.R(0.f, 0.6f)) / NSide);
			const FVector2f P0 = Bezier(A, C, B, Tt);
			const FVector2f Tan = (Bezier(A, C, B, Tt + 0.01f) - P0).GetSafeNormal();
			const float Side = (I % 2) ? 1.f : -1.f;
			const float Ang = FMath::DegreesToRadians(Rn.R(38.f, 62.f)) * Side;
			const FVector2f Dir(Tan.X * FMath::Cos(Ang) - Tan.Y * FMath::Sin(Ang), Tan.X * FMath::Sin(Ang) + Tan.Y * FMath::Cos(Ang));
			const float Len = T * FMath::Lerp(0.40f, 0.24f, Tt) * Rn.R(0.85f, 1.1f);
			const FVector2f End = P0 + Dir * Len;
			const FVector2f Ctl = P0 + Dir * (Len * 0.5f) + Tan * (Len * 0.15f);
			Twigs.Add({ P0, Ctl, End, 4.f });
		}
		// twigs first
		for (const FTw& Tw : Twigs)
		{
			FVector2f Prev = Tw.A;
			for (int32 K = 1; K <= 16; ++K)
			{
				const FVector2f Cur = Bezier(Tw.A, Tw.C, Tw.B, K / 16.f);
				DrawLine(Cv, R, Prev, Cur, Tw.W * (1.f - 0.6f * (K - 1) / 16.f), Tw.W * (1.f - 0.6f * K / 16.f), Twig, Twig);
				Prev = Cur;
			}
		}
		// leaves along twigs (alternate), plus a terminal leaf
		for (int32 Ti = 0; Ti < Twigs.Num(); ++Ti)
		{
			const FTw& Tw = Twigs[Ti];
			const float Start = Ti == 0 ? 0.30f : 0.18f;
			int32 Side = 1;
			for (float Tt = Start; Tt <= 1.0f; Tt += Rn.R(0.11f, 0.16f))
			{
				const FVector2f P0 = Bezier(Tw.A, Tw.C, Tw.B, Tt);
				const FVector2f Tan = (Bezier(Tw.A, Tw.C, Tw.B, FMath::Min(Tt + 0.01f, 1.f)) - Bezier(Tw.A, Tw.C, Tw.B, FMath::Max(Tt - 0.01f, 0.f))).GetSafeNormal();
				const bool bTerm = Tt > 0.93f;
				const float Ang = bTerm ? 0.f : FMath::DegreesToRadians(Rn.R(32.f, 58.f)) * Side;
				Side = -Side;
				const FVector2f Dir(Tan.X * FMath::Cos(Ang) - Tan.Y * FMath::Sin(Ang), Tan.X * FMath::Sin(Ang) + Tan.Y * FMath::Cos(Ang));
				const float Len = T * Rn.R(0.12f, 0.16f) * (bTerm ? 1.1f : 1.f);
				const float G = Rn.R(0.f, 1.f);
				FLinearColor Col = FMath::Lerp(SRGB(0.21f, 0.31f, 0.12f), SRGB(0.31f, 0.39f, 0.15f), G);
				if (Rn.F() < 0.08f) Col = SRGB(0.42f, 0.47f, 0.17f);
				if (Rn.F() < 0.05f) Col = SRGB(0.19f, 0.30f, 0.11f);
				const FVector2f Stalk = P0 + Dir * (T * 0.012f);
				DrawLine(Cv, R, P0, Stalk, 2.5f, 2.f, Twig, Twig);
				DrawLeaf(Cv, R, Stalk, Dir, Len, Len * Rn.R(0.22f, 0.27f), Col, 0.75f, Rn.F());
				if (bTerm) break;
			}
		}
	}

	void PaintNeedles(FLeafCanvas& Cv, const FIntRect& R, FLifeRand& Rn)
	{
		const float T = float(R.Width());
		const FVector2f O(R.Min.X, R.Min.Y);
		auto Px = [&](float S, float Tt) { return O + FVector2f((S + 0.5f) * T, (1.f - Tt) * T); };
		const FLinearColor Twig = SRGB(0.28f, 0.22f, 0.15f);
		struct FTw { FVector2f A, C, B; float W; float NeedleLen; };
		TArray<FTw> Twigs;
		const FVector2f A = Px(0.f, 0.02f), B = Px(Rn.R(-0.04f, 0.04f), 0.95f), C = Px(Rn.R(-0.06f, 0.06f), 0.5f);
		Twigs.Add({ A, C, B, 5.f, 0.055f });
		const int32 NSide = Rn.I(8, 11);
		for (int32 I = 0; I < NSide; ++I)
		{
			const float Tt = FMath::Lerp(0.1f, 0.82f, (I + Rn.R(0.f, 0.5f)) / NSide);
			const FVector2f P0 = Bezier(A, C, B, Tt);
			const FVector2f Tan = (Bezier(A, C, B, Tt + 0.01f) - P0).GetSafeNormal();
			const float Side = (I % 2) ? 1.f : -1.f;
			const float Ang = FMath::DegreesToRadians(Rn.R(48.f, 64.f)) * Side;
			const FVector2f Dir(Tan.X * FMath::Cos(Ang) - Tan.Y * FMath::Sin(Ang), Tan.X * FMath::Sin(Ang) + Tan.Y * FMath::Cos(Ang));
			const float Len = T * FMath::Lerp(0.40f, 0.18f, Tt) * Rn.R(0.85f, 1.1f);
			Twigs.Add({ P0, P0 + Dir * (Len * 0.5f) + Tan * (Len * 0.1f), P0 + Dir * Len, 3.f, 0.042f });
		}
		for (const FTw& Tw : Twigs)
		{
			FVector2f Prev = Tw.A;
			for (int32 K = 1; K <= 12; ++K)
			{
				const FVector2f Cur = Bezier(Tw.A, Tw.C, Tw.B, K / 12.f);
				DrawLine(Cv, R, Prev, Cur, Tw.W, Tw.W * 0.7f, Twig, Twig);
				Prev = Cur;
			}
			const float ArcLen = (Tw.B - Tw.A).Size() * 1.05f;
			const int32 N = FMath::Max(4, int32(ArcLen / 3.2f));
			for (int32 K = 0; K < N; ++K)
			{
				const float Tt = (K + 0.5f) / N;
				const FVector2f P0 = Bezier(Tw.A, Tw.C, Tw.B, Tt);
				const FVector2f Tan = (Bezier(Tw.A, Tw.C, Tw.B, FMath::Min(Tt + 0.01f, 1.f)) - P0).GetSafeNormal();
				for (int32 Sd = -1; Sd <= 1; Sd += 2)
				{
					const float Ang = FMath::DegreesToRadians(Rn.R(45.f, 70.f)) * Sd;
					const FVector2f Dir(Tan.X * FMath::Cos(Ang) - Tan.Y * FMath::Sin(Ang), Tan.X * FMath::Sin(Ang) + Tan.Y * FMath::Cos(Ang));
					const float Len = T * Tw.NeedleLen * Rn.R(0.8f, 1.15f) * (1.f - 0.4f * Tt * Tt);
					const float G = Rn.F();
					const FLinearColor Base = FMath::Lerp(SRGB(0.10f, 0.20f, 0.12f), SRGB(0.16f, 0.27f, 0.15f), G);
					const FLinearColor TipC = (Tt > 0.8f) ? SRGB(0.26f, 0.38f, 0.17f) : Base * 1.2f;
					DrawLine(Cv, R, P0, P0 + Dir * Len, 3.2f, 1.4f, Base, TipC);
				}
			}
		}
	}

	void PaintFrond(FLeafCanvas& Cv, const FIntRect& R, FLifeRand& Rn)
	{
		// The frond card is ~1.8 m wide and 5 m long, squeezed into a square tile: draw in metres.
		const float T = float(R.Width());
		const float CardW = 1.8f, CardL = 5.f;
		const FVector2f O(R.Min.X, R.Min.Y);
		auto Px = [&](float Xm, float Ym) { return O + FVector2f((Xm / CardW + 0.5f) * T, (1.f - Ym / CardL) * T); };
		const FLinearColor Rachis = SRGB(0.40f, 0.38f, 0.20f);
		DrawLine(Cv, R, Px(0.f, 0.f), Px(0.f, CardL * 0.98f), 9.f, 3.f, Rachis, Rachis);
		for (float Ym = 0.35f; Ym < CardL * 0.97f; Ym += Rn.R(0.055f, 0.075f))
		{
			const float Tt = Ym / CardL;
			const float Lw = 0.86f * FMath::Pow(FMath::Sin(PI * FMath::Clamp(Tt * 0.92f + 0.06f, 0.f, 1.f)), 0.55f) * (CardW * 0.5f / 0.86f) * 0.98f;
			for (int32 Sd = -1; Sd <= 1; Sd += 2)
			{
				if (Rn.F() < 0.07f) continue; // torn leaflets
				const float Ang = FMath::DegreesToRadians(Rn.R(52.f, 66.f));
				const float Len = Lw * Rn.R(0.85f, 1.02f) / FMath::Sin(Ang);
				const float Ex = Sd * FMath::Sin(Ang) * Len, Ey = FMath::Cos(Ang) * Len;
				const FVector2f P0 = Px(0.f, Ym), P1 = Px(Ex * 0.55f, Ym + Ey * 0.5f + 0.02f), P2 = Px(Ex, Ym + Ey - 0.05f);
				const float G = Rn.F();
				FLinearColor Col = FMath::Lerp(SRGB(0.22f, 0.36f, 0.13f), SRGB(0.33f, 0.44f, 0.16f), G);
				const FLinearColor TipC = Rn.F() < 0.25f ? SRGB(0.52f, 0.50f, 0.25f) : Col * 1.1f;
				DrawLine(Cv, R, P0, P1, 7.f, 6.f, Col, FMath::Lerp(Col, TipC, 0.4f));
				DrawLine(Cv, R, P1, P2, 6.f, 1.5f, FMath::Lerp(Col, TipC, 0.4f), TipC);
			}
		}
	}

	void PaintLeaflets(FLeafCanvas& Cv, const FIntRect& R, FLifeRand& Rn)
	{
		// A roughly round spray of bipinnate twigs with tiny leaflets (acacia, shrubs, olive-like trees).
		const float T = float(R.Width());
		const FVector2f O(R.Min.X, R.Min.Y);
		const FVector2f Ctr = O + FVector2f(0.5f * T, 0.55f * T);
		const FLinearColor Twig = SRGB(0.30f, 0.25f, 0.19f);
		const int32 NB = Rn.I(9, 12);
		for (int32 I = 0; I < NB; ++I)
		{
			const float Ang = (I + Rn.R(-0.3f, 0.3f)) / NB * 2.f * PI;
			const FVector2f Dir(FMath::Cos(Ang), FMath::Sin(Ang));
			const float Len = T * Rn.R(0.30f, 0.42f);
			const FVector2f P0 = Ctr + Dir * (T * 0.03f);
			const FVector2f P1 = Ctr + Dir * Len;
			DrawLine(Cv, R, P0, P1, 4.f, 1.5f, Twig, Twig);
			const FVector2f Perp(-Dir.Y, Dir.X);
			for (float S = 0.18f; S < 1.f; S += Rn.R(0.07f, 0.1f))
			{
				const FVector2f Q = FMath::Lerp(P0, P1, S);
				for (int32 Sd = -1; Sd <= 1; Sd += 2)
				{
					// pinna: a short side twig with paired leaflets
					const FVector2f Pd = (Perp * float(Sd) + Dir * 0.55f).GetSafeNormal();
					const float PL = T * Rn.R(0.06f, 0.085f) * (1.15f - 0.5f * S);
					DrawLine(Cv, R, Q, Q + Pd * PL, 1.8f, 1.f, Twig, Twig);
					const FVector2f PPerp(-Pd.Y, Pd.X);
					for (float U = 0.15f; U <= 1.f; U += 0.17f)
					{
						const FVector2f B = Q + Pd * (PL * U);
						const float G = Rn.F();
						const FLinearColor Col = FMath::Lerp(SRGB(0.25f, 0.34f, 0.13f), SRGB(0.36f, 0.42f, 0.17f), G);
						for (int32 Sl = -1; Sl <= 1; Sl += 2)
						{
							const FVector2f Ld = (PPerp * float(Sl) + Pd * 0.5f).GetSafeNormal();
							DrawLeaf(Cv, R, B, Ld, T * Rn.R(0.022f, 0.03f), T * 0.0075f, Col, 0.6f, Rn.F());
						}
					}
				}
			}
		}
	}

	// ------------------------------------------------------------ helpers
	FColor ToColor(const FLinearColor& C) // values already in target space
	{
		return FColor(uint8(FMath::Clamp(C.R, 0.f, 1.f) * 255.f + 0.5f), uint8(FMath::Clamp(C.G, 0.f, 1.f) * 255.f + 0.5f),
			uint8(FMath::Clamp(C.B, 0.f, 1.f) * 255.f + 0.5f), uint8(FMath::Clamp(C.A, 0.f, 1.f) * 255.f + 0.5f));
	}

	/** Builds a normal map (two channels packed into XY of the returned vector) from a periodic height field. */
	void HeightToNormal(const TArray<float>& Hh, int32 W, int32 H, float Strength, TArray<FVector2f>& Out)
	{
		Out.SetNumUninitialized(W * H);
		for (int32 Y = 0; Y < H; ++Y)
			for (int32 X = 0; X < W; ++X)
			{
				const float L = Hh[Y * W + (X + W - 1) % W], Rr = Hh[Y * W + (X + 1) % W];
				const float U = Hh[((Y + H - 1) % H) * W + X], D = Hh[((Y + 1) % H) * W + X];
				FVector3f N(-(Rr - L) * Strength, -(D - U) * Strength, 1.f);
				N.Normalize();
				Out[Y * W + X] = FVector2f(N.X, N.Y);
			}
	}
	inline uint8 Enc(float V) { return uint8(FMath::Clamp(V * 0.5f + 0.5f, 0.f, 1.f) * 255.f + 0.5f); }
	inline uint8 U8(float V) { return uint8(FMath::Clamp(V, 0.f, 1.f) * 255.f + 0.5f); }
}

UTexture2D* LifeMakeTexture(const FString& Name, int32 W, int32 H, TArray<FColor>&& Mip0, bool bSRGB, bool bAlphaCoverage, float AlphaRef, bool bDilate, bool bClamp)
{
	check(Mip0.Num() == W * H);
	// Optional push-pull dilation: transparent texels take the colour of nearby opaque ones (no dark fringes).
	if (bDilate)
	{
		struct FLvl { int32 W, H; TArray<FLinearColor> C; };
		TArray<FLvl> Ls;
		Ls.Add({ W, H, {} });
		Ls[0].C.SetNumUninitialized(W * H);
		for (int32 I = 0; I < W * H; ++I)
		{
			const float A = Mip0[I].A / 255.f;
			Ls[0].C[I] = FLinearColor(Mip0[I].R / 255.f * A, Mip0[I].G / 255.f * A, Mip0[I].B / 255.f * A, A);
		}
		while (Ls.Last().W > 1 && Ls.Last().H > 1)
		{
			const FLvl& P = Ls.Last();
			FLvl N{ P.W / 2, P.H / 2, {} };
			N.C.SetNumUninitialized(N.W * N.H);
			for (int32 Y = 0; Y < N.H; ++Y)
				for (int32 X = 0; X < N.W; ++X)
					N.C[Y * N.W + X] = (P.C[(2 * Y) * P.W + 2 * X] + P.C[(2 * Y) * P.W + 2 * X + 1] + P.C[(2 * Y + 1) * P.W + 2 * X] + P.C[(2 * Y + 1) * P.W + 2 * X + 1]) * 0.25f;
			Ls.Add(MoveTemp(N));
		}
		// pull: Full[L] = premult[L] + (1 - a[L]) * Full[L+1]
		TArray<FLinearColor> Full = Ls.Last().C;
		for (FLinearColor& C : Full) C = C.A > 1e-6f ? FLinearColor(C.R / C.A, C.G / C.A, C.B / C.A, 1.f) : FLinearColor(0.2f, 0.3f, 0.1f, 1.f);
		for (int32 L = Ls.Num() - 2; L >= 0; --L)
		{
			const FLvl& F = Ls[L]; const int32 Cw = Ls[L + 1].W, Ch = Ls[L + 1].H;
			TArray<FLinearColor> Nf; Nf.SetNumUninitialized(F.W * F.H);
			for (int32 Y = 0; Y < F.H; ++Y)
				for (int32 X = 0; X < F.W; ++X)
				{
					const FLinearColor& P = F.C[Y * F.W + X];
					const FLinearColor& S = Full[FMath::Min(Y / 2, Ch - 1) * Cw + FMath::Min(X / 2, Cw - 1)];
					Nf[Y * F.W + X] = FLinearColor(P.R + (1.f - P.A) * S.R, P.G + (1.f - P.A) * S.G, P.B + (1.f - P.A) * S.B, 1.f);
				}
			Full = MoveTemp(Nf);
		}
		for (int32 I = 0; I < W * H; ++I)
		{
			const FLinearColor& C = Full[I];
			Mip0[I].R = U8(C.R); Mip0[I].G = U8(C.G); Mip0[I].B = U8(C.B);
		}
	}

	auto CoverageOf = [AlphaRef](const TArray<FColor>& P, float Scale)
	{
		int64 N = 0;
		for (const FColor& C : P) if (C.A / 255.f * Scale > AlphaRef) ++N;
		return double(N) / FMath::Max(1, P.Num());
	};
	const double Target = bAlphaCoverage ? CoverageOf(Mip0, 1.f) : 0.0;

	UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8, MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), FName(*Name)));
	Tex->SRGB = bSRGB;
	Tex->LODGroup = TEXTUREGROUP_World;
	Tex->NeverStream = true;
	if (bClamp) { Tex->AddressX = TA_Clamp; Tex->AddressY = TA_Clamp; }
	FTexturePlatformData* PD = Tex->GetPlatformData();
	{
		FTexture2DMipMap& M = PD->Mips[0];
		void* D = M.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(D, Mip0.GetData(), int64(W) * H * 4);
		M.BulkData.Unlock();
	}
	TArray<FColor> Prev = MoveTemp(Mip0);
	int32 Pw = W, Ph = H;
	while (Pw > 1 || Ph > 1)
	{
		const int32 Nw = FMath::Max(1, Pw / 2), Nh = FMath::Max(1, Ph / 2);
		TArray<FColor> Cur; Cur.SetNumUninitialized(Nw * Nh);
		for (int32 Y = 0; Y < Nh; ++Y)
			for (int32 X = 0; X < Nw; ++X)
			{
				float R = 0, G = 0, B = 0, A = 0, Wsum = 0;
				for (int32 dy = 0; dy < 2; ++dy)
					for (int32 dx = 0; dx < 2; ++dx)
					{
						const FColor& S = Prev[FMath::Min(2 * Y + dy, Ph - 1) * Pw + FMath::Min(2 * X + dx, Pw - 1)];
						// alpha-weighted colour for alpha-tested textures (dilated colour keeps this stable)
						const float Wt = bAlphaCoverage ? (0.05f + S.A / 255.f) : 1.f;
						R += S.R * Wt; G += S.G * Wt; B += S.B * Wt; Wsum += Wt; A += S.A;
					}
				Cur[Y * Nw + X] = FColor(uint8(R / Wsum + 0.5f), uint8(G / Wsum + 0.5f), uint8(B / Wsum + 0.5f), uint8(A / 4.f + 0.5f));
			}
		if (bAlphaCoverage && Target > 0.0)
		{
			// scale alpha so the fraction of texels passing the clip matches mip 0 (keeps canopies dense far away)
			float Lo = 0.5f, Hi = 8.f;
			for (int32 It = 0; It < 18; ++It) { const float Mid = 0.5f * (Lo + Hi); if (CoverageOf(Cur, Mid) < Target) Lo = Mid; else Hi = Mid; }
			const float Sc = 0.5f * (Lo + Hi);
			for (FColor& C : Cur) C.A = uint8(FMath::Clamp(C.A * Sc, 0.f, 255.f));
		}
		FTexture2DMipMap* M = new FTexture2DMipMap(Nw, Nh, 1);
		PD->Mips.Add(M);
		M->BulkData.Lock(LOCK_READ_WRITE);
		void* D = M->BulkData.Realloc(int64(Nw) * Nh * 4);
		FMemory::Memcpy(D, Cur.GetData(), int64(Nw) * Nh * 4);
		M->BulkData.Unlock();
		Prev = MoveTemp(Cur);
		Pw = Nw; Ph = Nh;
	}
	Tex->UpdateResource();
	return Tex;
}

void FLifeTextures::AddReferencedObjects(TArray<UObject*>& Out) const
{
	for (UTexture2D* T : { LeafAtlas, Bark, BarkN, PalmBark, PalmBarkN, Wall, WallN, Roof, RoofN, Ground, WaterN }) if (T) Out.Add(T);
}

void FLifeTextures::Build()
{
	const double T0 = FPlatformTime::Seconds();
	// ---- leaf atlas 2048 (4 tiles of 1024)
	{
		const int32 S = 2048, Tl = 1024, Pad = 20;
		FLeafCanvas Cv(S, S);
		FLifeRand Rn(1234);
		ParallelFor(4, [&](int32 I)
		{
			FLifeRand R2(777 + I * 31);
			const FIntRect Rc(FIntPoint((I % 2) * Tl + Pad, (I / 2) * Tl + Pad), FIntPoint((I % 2) * Tl + Tl - Pad, (I / 2) * Tl + Tl - Pad));
			switch (I)
			{
			case 0: PaintBroadleaf(Cv, Rc, R2); break;
			case 1: PaintNeedles(Cv, Rc, R2); break;
			case 2: PaintFrond(Cv, Rc, R2); break;
			default: PaintLeaflets(Cv, Rc, R2); break;
			}
		});
		TArray<FColor> Px; Px.SetNumUninitialized(S * S);
		for (int32 I = 0; I < S * S; ++I) Px[I] = ToColor(Cv.P[I]);
		FString Dump;
		if (FParse::Value(FCommandLine::Get(), TEXT("-dumptex="), Dump))
		{
			TArray<FColor> Rgb = Px, Al = Px;
			for (FColor& C : Al) C = FColor(C.A, C.A, C.A, 255);
			for (FColor& C : Rgb) { const float A = C.A / 255.f; C = FColor(uint8(C.R * A + 128 * (1 - A)), uint8(C.G * A + 128 * (1 - A)), uint8(C.B * A + 128 * (1 - A)), 255); }
			FFileHelper::CreateBitmap(*(Dump / TEXT("leaf_rgb.bmp")), S, S, Rgb.GetData());
			FFileHelper::CreateBitmap(*(Dump / TEXT("leaf_alpha.bmp")), S, S, Al.GetData());
		}
		LeafAtlas = LifeMakeTexture(TEXT("T_LeafAtlas"), S, S, MoveTemp(Px), true, true, 0.4f, true);
	}
	// ---- bark: fissured (detail in sRGB, multiplied by vertex colour) + normal
	{
		const int32 S = 512;
		TArray<float> Hh; Hh.SetNumUninitialized(S * S);
		TArray<FColor> Px; Px.SetNumUninitialized(S * S);
		for (int32 Y = 0; Y < S; ++Y)
			for (int32 X = 0; X < S; ++X)
			{
				const float U = (X + 0.5f) / S, V = (Y + 0.5f) / S;
				const float Warp = 0.08f * PFbm(U, V, 4, 2, 3, 5);
				const float F = PFbm(U + Warp, V, 10, 2, 4, 11);
				const float Ridge = 1.f - FMath::Abs(F) * 2.2f;
				const float Fine = PFbm(U, V, 32, 16, 3, 23);
				const float Hgt = FMath::Clamp(Ridge, 0.f, 1.f) * 0.8f + 0.2f * (Fine * 0.5f + 0.5f);
				Hh[Y * S + X] = Hgt;
				const float L = 0.38f + 0.62f * FMath::Pow(Hgt, 0.8f);
				Px[Y * S + X] = FColor(U8(L * 0.98f), U8(L), U8(L * 1.02f), 255); // stored as BGRA
			}
		Bark = LifeMakeTexture(TEXT("T_Bark"), S, S, MoveTemp(Px), true, false);
		TArray<FVector2f> N; HeightToNormal(Hh, S, S, 6.f, N);
		TArray<FColor> Np; Np.SetNumUninitialized(S * S);
		for (int32 I = 0; I < S * S; ++I) { Np[I].R = Enc(N[I].X); Np[I].G = Enc(N[I].Y); Np[I].B = 255; Np[I].A = 255; }
		BarkN = LifeMakeTexture(TEXT("T_BarkN"), S, S, MoveTemp(Np), false, false);
	}
	// ---- palm trunk: leaf-scar rings every ~12 cm, fibrous between
	{
		const int32 S = 512;
		TArray<float> Hh; Hh.SetNumUninitialized(S * S);
		TArray<FColor> Px; Px.SetNumUninitialized(S * S);
		for (int32 Y = 0; Y < S; ++Y)
			for (int32 X = 0; X < S; ++X)
			{
				const float U = (X + 0.5f) / S, V = (Y + 0.5f) / S;
				// texture covers 1.25 m around x 2.2 m up (material tiling); 18 rings per tile
				const float Ring = FMath::Frac(V * 18.f + 0.15f * PFbm(U, V, 3, 2, 2, 141));
				const float Scar = LifeSmooth(0.0f, 0.25f, Ring) * (1.f - LifeSmooth(0.7f, 1.f, Ring));
				const float Fib = PFbm(U, V, 48, 6, 3, 143);
				const float Hgt = Scar * 0.8f + 0.2f * (Fib * 0.5f + 0.5f);
				Hh[Y * S + X] = Hgt;
				const float L = 0.55f + 0.4f * Hgt + 0.05f * PFbm(U, V, 8, 8, 2, 145);
				Px[Y * S + X] = FColor(U8(L), U8(L * 0.97f), U8(L * 0.9f), 255);
			}
		PalmBark = LifeMakeTexture(TEXT("T_PalmBark"), S, S, MoveTemp(Px), true, false);
		TArray<FVector2f> N; HeightToNormal(Hh, S, S, 5.f, N);
		TArray<FColor> Np; Np.SetNumUninitialized(S * S);
		for (int32 I = 0; I < S * S; ++I) Np[I] = FColor(Enc(N[I].X), Enc(N[I].Y), 255, 255);
		PalmBarkN = LifeMakeTexture(TEXT("T_PalmBarkN"), S, S, MoveTemp(Np), false, false);
	}
	// ---- walls: R plaster, G stone masonry, B brick, A wood. 1024 px covers 4 m.
	{
		const int32 S = 1024; const float M = 4.f; // metres per tile
		TArray<float> Hs; Hs.SetNumZeroed(S * S);
		TArray<float> Hb; Hb.SetNumZeroed(S * S);
		TArray<FColor> Px; Px.SetNumUninitialized(S * S);
		// stone course layout: rows of varying height, blocks of varying width
		TArray<float> RowY; { float Y = 0; FLifeRand Rr(5); while (Y < M - 0.2f) { RowY.Add(Y); Y += Rr.R(0.28f, 0.46f); } RowY.Add(M); }
		ParallelFor(S, [&](int32 Y)
		{
			for (int32 X = 0; X < S; ++X)
			{
				const float U = (X + 0.5f) / S, V = (Y + 0.5f) / S;
				const float Xm = U * M, Ym = V * M;
				const float Plaster = 0.78f + 0.12f * PFbm(U, V, 6, 6, 5, 41) + 0.05f * PFbm(U, V, 64, 64, 2, 43);
				// stone
				int32 Row = 0; while (Row + 1 < RowY.Num() && Ym >= RowY[Row + 1]) ++Row;
				const float Y0 = RowY[Row], Y1 = RowY[FMath::Min(Row + 1, RowY.Num() - 1)];
				FLifeRand Rr(1000 + Row);
				float Xs = -Rr.R(0.f, 0.6f), Xe = 0.f; int32 Blk = 0;
				while (true) { Xe = Xs + Rr.R(0.45f, 1.05f); if (Xm < Xe || Xe > M + 2.f) break; Xs = Xe; ++Blk; }
				if (Xe > M) Xe = M; // last block meets the wrap (approximate)
				const float Ed = FMath::Min(FMath::Min(Xm - Xs, Xe - Xm), FMath::Min(Ym - Y0, Y1 - Ym));
				const float Mortar = LifeSmooth(0.012f, 0.03f, Ed);
				const float Tone = 0.72f + 0.25f * LifeHash01(Row, Blk, 9) + 0.06f * PFbm(U, V, 24, 24, 3, 47);
				const float Bulge = FMath::Sqrt(FMath::Clamp(Ed / 0.12f, 0.f, 1.f));
				const float Stone = FMath::Lerp(0.42f, Tone, Mortar);
				Hs[Y * S + X] = Mortar * (0.6f + 0.4f * Bulge) + 0.05f * PFbm(U, V, 48, 48, 2, 51);
				// brick: 0.075 m courses, 0.225 m bricks, running bond
				const float Bh = 0.075f, Bw = 0.225f;
				const int32 Br = FMath::FloorToInt(Ym / Bh);
				const float Bx = Xm + (Br % 2) * (Bw * 0.5f);
				const int32 Bc = FMath::FloorToInt(Bx / Bw);
				const float Ex = FMath::Min(Bx - Bc * Bw, (Bc + 1) * Bw - Bx), Ey = FMath::Min(Ym - Br * Bh, (Br + 1) * Bh - Ym);
				const float BMort = LifeSmooth(0.004f, 0.009f, FMath::Min(Ex, Ey));
				const float BTone = 0.70f + 0.3f * LifeHash01(Bc, Br, 77) + 0.05f * PFbm(U, V, 96, 96, 2, 53);
				const float Brick = FMath::Lerp(0.55f, BTone, BMort);
				Hb[Y * S + X] = BMort * 0.8f;
				const float Wood = 0.72f + 0.16f * PFbm(U, V, 3, 96, 4, 61) + 0.06f * PFbm(U, V, 32, 4, 2, 63);
				Px[Y * S + X] = FColor(U8(Plaster), U8(Stone), U8(Brick), U8(Wood)); // FColor ctor is (R,G,B,A)
			}
		});
		Wall = LifeMakeTexture(TEXT("T_Wall"), S, S, MoveTemp(Px), false, false);
		TArray<FVector2f> Ns, Nb; HeightToNormal(Hs, S, S, 3.f, Ns); HeightToNormal(Hb, S, S, 3.f, Nb);
		TArray<FColor> Np; Np.SetNumUninitialized(S * S);
		for (int32 I = 0; I < S * S; ++I) Np[I] = FColor(Enc(Ns[I].X), Enc(Ns[I].Y), Enc(Nb[I].X), Enc(Nb[I].Y));
		WallN = LifeMakeTexture(TEXT("T_WallN"), S, S, MoveTemp(Np), false, false);
	}
	// ---- roofs: R thatch, G clay tiles, B slate, A metal seams. 1024 px covers 4 m; V runs down the slope.
	{
		const int32 S = 1024; const float M = 4.f;
		TArray<float> Ht; Ht.SetNumZeroed(S * S);
		TArray<float> Hc; Hc.SetNumZeroed(S * S);
		TArray<FColor> Px; Px.SetNumUninitialized(S * S);
		ParallelFor(S, [&](int32 Y)
		{
			for (int32 X = 0; X < S; ++X)
			{
				const float U = (X + 0.5f) / S, V = (Y + 0.5f) / S;
				const float Xm = U * M, Ym = V * M;
				// thatch: fine straw along the slope, coursed every ~0.33 m
				const float Straw = PFbm(U, V, 160, 8, 3, 71);
				const float Course = FMath::Frac(Ym / 0.333f);
				const float Thatch = 0.62f + 0.22f * Straw + 0.12f * PFbm(U, V, 8, 8, 3, 73) - 0.18f * LifeSmooth(0.75f, 1.f, Course);
				Ht[Y * S + X] = 0.5f + 0.3f * Straw - 0.4f * LifeSmooth(0.7f, 1.f, Course);
				// clay tiles: courses 0.25 m, barrel tiles 0.2 m wide
				const float Tc = Ym / 0.25f; const int32 Ti = FMath::FloorToInt(Tc); const float Tf = Tc - Ti;
				const float Tx = Xm / 0.2f + (Ti % 2) * 0.5f; const int32 Tj = FMath::FloorToInt(Tx); const float Txf = Tx - Tj;
				const float Barrel = FMath::Sin(PI * Txf);
				const float Lap = LifeSmooth(0.80f, 1.f, Tf);
				const float Tile = (0.62f + 0.3f * Barrel) * (1.f - 0.45f * Lap) * (0.82f + 0.25f * LifeHash01(Tj, Ti, 81)) + 0.04f * PFbm(U, V, 64, 64, 2, 83);
				Hc[Y * S + X] = Barrel * 0.6f + (1.f - Tf) * 0.4f;
				// slate: 0.2 m courses, plates 0.3 m
				const float Sc = Ym / 0.2f; const int32 Si = FMath::FloorToInt(Sc); const float Sf = Sc - Si;
				const float Sx = Xm / 0.3f + (Si % 2) * 0.5f; const int32 Sj = FMath::FloorToInt(Sx); const float Sxf = Sx - Sj;
				const float SEdge = LifeSmooth(0.0f, 0.04f, FMath::Min(Sxf, 1.f - Sxf)) * (1.f - 0.5f * LifeSmooth(0.85f, 1.f, Sf));
				const float Slate = (0.55f + 0.3f * LifeHash01(Sj, Si, 91)) * (0.5f + 0.5f * SEdge) + 0.04f * PFbm(U, V, 48, 48, 2, 93);
				// metal standing seams every 0.5 m
				const float Seam = FMath::Frac(Xm / 0.5f);
				const float Metal = 0.75f + 0.15f * PFbm(U, V, 4, 16, 3, 97) - 0.35f * LifeSmooth(0.94f, 1.f, Seam);
				Px[Y * S + X] = FColor(U8(Thatch), U8(Tile), U8(Slate), U8(Metal));
			}
		});
		Roof = LifeMakeTexture(TEXT("T_Roof"), S, S, MoveTemp(Px), false, false);
		TArray<FVector2f> Nt, Nc; HeightToNormal(Ht, S, S, 2.5f, Nt); HeightToNormal(Hc, S, S, 4.f, Nc);
		TArray<FColor> Np; Np.SetNumUninitialized(S * S);
		for (int32 I = 0; I < S * S; ++I) Np[I] = FColor(Enc(Nt[I].X), Enc(Nt[I].Y), Enc(Nc[I].X), Enc(Nc[I].Y));
		RoofN = LifeMakeTexture(TEXT("T_RoofN"), S, S, MoveTemp(Np), false, false);
	}
	// ---- ground detail masks: R fine mottle, G crown clumps (cellular), B pebbles, A broad variation
	{
		const int32 S = 1024;
		TArray<FColor> Px; Px.SetNumUninitialized(S * S);
		ParallelFor(S, [&](int32 Y)
		{
			for (int32 X = 0; X < S; ++X)
			{
				const float U = (X + 0.5f) / S, V = (Y + 0.5f) / S;
				const float Fine = 0.5f + 0.5f * PFbm(U, V, 32, 32, 5, 101);
				float Id = 0.f, F2 = 0.f;
				const float F1 = PCell(U, V, 14, 103, &Id, &F2);
				const float Crown = FMath::Clamp(1.f - F1 * 1.25f, 0.f, 1.f) * (0.75f + 0.25f * Id) * LifeSmooth(0.0f, 0.18f, F2 - F1);
				float Id2 = 0.f;
				const float P1 = PCell(U, V, 90, 107, &Id2);
				const float Peb = FMath::Clamp(1.f - P1 * 2.2f, 0.f, 1.f) * Id2;
				const float Broad = 0.5f + 0.5f * PFbm(U, V, 3, 3, 4, 109);
				Px[Y * S + X] = FColor(U8(Fine), U8(Crown), U8(Peb), U8(Broad));
			}
		});
		Ground = LifeMakeTexture(TEXT("T_Ground"), S, S, MoveTemp(Px), false, false);
	}
	// ---- water ripples normal
	{
		const int32 S = 512;
		TArray<float> Hh; Hh.SetNumUninitialized(S * S);
		for (int32 Y = 0; Y < S; ++Y)
			for (int32 X = 0; X < S; ++X)
			{
				const float U = (X + 0.5f) / S, V = (Y + 0.5f) / S;
				Hh[Y * S + X] = PFbm(U, V, 6, 6, 5, 131) + 0.3f * FMath::Sin(2.f * PI * (U * 3.f + V * 5.f + 0.3f * PFbm(U, V, 4, 4, 2, 133)));
			}
		TArray<FVector2f> N; HeightToNormal(Hh, S, S, 10.f, N);
		TArray<FColor> Np; Np.SetNumUninitialized(S * S);
		for (int32 I = 0; I < S * S; ++I) Np[I] = FColor(Enc(N[I].X), Enc(N[I].Y), 255, 255);
		WaterN = LifeMakeTexture(TEXT("T_WaterN"), S, S, MoveTemp(Np), false, false);
	}
	UE_LOG(LogTemp, Display, TEXT("LifeTex: textures painted in %.2fs"), FPlatformTime::Seconds() - T0);
}
