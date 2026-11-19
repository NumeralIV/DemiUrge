// DEMIURGE — Game/Nature/DmNatureTextures.cpp : see the header. Painters ported from the LifeLab (LifeTex.cpp).

#include "Game/Nature/DmNatureTextures.h"
#include "Game/Nature/DmNatureMesh.h"

#include "Async/ParallelFor.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformTime.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmNatureTex, Log, All);

namespace
{
	using FLifeRand = DmNature::FRand;
	inline float LifeHash01(int32 X, int32 Y, uint32 S = 0) { return DmNature::Hash01(DmNature::Hash2(X, Y, S)); }
	inline float LifeSmooth(float E0, float E1, float X) { return DmNature::Smooth(E0, E1, X); }
}

namespace
{
	// ------------------------------------------------------------ periodic noise
	float PHash(int32 X, int32 Y, int32 P, uint32 S)
	{
		X = ((X % P) + P) % P; Y = ((Y % P) + P) % P;
		return LifeHash01(X, Y, S);
	}
	/** Value noise, period P cells, X/Y in cell units. Returns -1..1. */
	[[maybe_unused]] float PNoise(float X, float Y, int32 P, uint32 S)
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
	[[maybe_unused]] float PCell(float U, float V, int32 P, uint32 S, float* OutId = nullptr, float* OutF2 = nullptr)
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
				const float Wa = HalfW * FMath::Pow(FMath::Max(0.f, FMath::Sin(PI * FMath::Clamp(Ac * 0.92f + 0.04f, 0.f, 1.f))), Tip) * (1.f - 0.25f * Ac);
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
					const FLinearColor Base = FMath::Lerp(SRGB(0.14f, 0.26f, 0.12f), SRGB(0.21f, 0.34f, 0.15f), G);
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
			const float Lw = 0.86f * FMath::Pow(FMath::Max(0.f, FMath::Sin(PI * FMath::Clamp(Tt * 0.92f + 0.06f, 0.f, 1.f))), 0.55f) * (CardW * 0.5f / 0.86f) * 0.98f;
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

	// ------------------------------------------------------------ far-LOD clumps
	/** Irregular blob: union of soft discs (tile units 0..1, origin top-left of the tile). */
	struct FBlob
	{
		TArray<FVector3f> Discs;   // x, y, radius
		float Value(const FVector2f& P) const
		{
			float V = -1.f;
			for (const FVector3f& D : Discs) V = FMath::Max(V, 1.f - FVector2f(P.X - D.X, P.Y - D.Y).Size() / D.Z);
			return V;
		}
	};
	FBlob MakeBlob(FLifeRand& Rn, float Cx, float Cy, float Spread, float R0, float R1, int32 N, float SquashY)
	{
		FBlob B;
		B.Discs.Add(FVector3f(Cx, Cy, R1));
		for (int32 I = 1; I < N; ++I)
		{
			const float A = Rn.R(0.f, 2.f * PI), D = Spread * FMath::Sqrt(Rn.F());
			B.Discs.Add(FVector3f(Cx + FMath::Cos(A) * D, Cy + FMath::Sin(A) * D * SquashY, Rn.R(R0, R1)));
		}
		return B;
	}

	/** A dense mass of leaves (a broadleaf crown part or an acacia leaflet layer) with painted self-shadowing:
	 *  leaves drawn back to front, deeper ones darker, the underside darker than the sunlit top, dark twigs
	 *  showing through the gaps. */
	void PaintClump(FLeafCanvas& Cv, const FIntRect& R, FLifeRand& Rn, bool bLeaflets)
	{
		const float T = float(R.Width());
		const FVector2f O(R.Min.X, R.Min.Y);
		const FBlob Blob = bLeaflets ? MakeBlob(Rn, 0.5f, 0.5f, 0.27f, 0.10f, 0.2f, 12, 0.8f) : MakeBlob(Rn, 0.5f, 0.5f, 0.27f, 0.09f, 0.19f, 13, 0.95f);
		const FLinearColor Twig = SRGB(0.22f, 0.18f, 0.13f);
		for (int32 I = 0; I < 7; ++I)
		{
			const float A = FMath::DegreesToRadians(Rn.R(-160.f, -20.f));
			const FVector2f P0 = O + FVector2f(0.5f, 0.92f) * T;
			const FVector2f P1 = O + FVector2f(0.5f + FMath::Cos(A) * Rn.R(0.2f, 0.38f), 0.6f + FMath::Sin(A) * Rn.R(0.15f, 0.3f)) * T;
			DrawLine(Cv, R, P0, P1, 9.f, 3.f, Twig, Twig);
		}
		struct FL { FVector2f P; FVector2f D; float Depth, Len; };
		TArray<FL> Leaves;
		const int32 N = bLeaflets ? 5200 : 1500;
		const float Len0 = bLeaflets ? 0.020f : 0.052f;
		for (int32 Tries = 0; Leaves.Num() < N && Tries < N * 8; ++Tries)
		{
			const FVector2f P(Rn.F(), Rn.F());
			// ragged outline: leaf density falls off over a wide band at the edge, and noise holes open small gaps
			const float V = Blob.Value(P) + 0.2f * PFbm(P.X, P.Y, 6, 6, 4, 401);
			const float Keep = LifeSmooth(-0.18f, 0.16f, V) * LifeSmooth(-0.55f, -0.2f, PFbm(P.X, P.Y, 10, 10, 2, 409));
			if (Rn.F() > Keep) continue;
			FL L;
			L.P = P;
			L.Depth = FMath::Clamp(0.25f + 0.45f * (1.f - FMath::Clamp(V * 1.6f, 0.f, 1.f)) + Rn.R(0.f, 0.6f), 0.f, 1.f);
			const FVector2f Out = (P - FVector2f(0.5f, 0.55f)).GetSafeNormal();
			L.D = (Out * Rn.R(0.3f, 1.f) + FVector2f(Rn.R(-1.f, 1.f), Rn.R(-1.f, 1.f))).GetSafeNormal();
			L.Len = Len0 * Rn.R(0.75f, 1.2f);
			Leaves.Add(L);
		}
		Leaves.Sort([](const FL& A, const FL& B) { return A.Depth < B.Depth; });
		for (const FL& L : Leaves)
		{
			const float G = Rn.F();
			FLinearColor Col = bLeaflets ? FMath::Lerp(SRGB(0.24f, 0.33f, 0.13f), SRGB(0.36f, 0.42f, 0.17f), G)
				: FMath::Lerp(SRGB(0.20f, 0.30f, 0.11f), SRGB(0.31f, 0.40f, 0.15f), G);
			if (Rn.F() < 0.06f) Col = bLeaflets ? SRGB(0.42f, 0.44f, 0.20f) : SRGB(0.40f, 0.46f, 0.16f);
			// light from above: the top of the clump is lit, the underside and the deep leaves are in shade
			const float Lit = FMath::Clamp(1.05f - 0.75f * L.P.Y, 0.35f, 1.f);
			const float Shade = FMath::Lerp(0.38f, 0.98f, L.Depth) * FMath::Lerp(0.58f, 1.f, Lit);
			const FVector2f Base = O + L.P * T;
			DrawLeaf(Cv, R, Base, L.D, L.Len * T, L.Len * T * (bLeaflets ? 0.3f : Rn.R(0.24f, 0.3f)), Col * Shade, 0.7f, Rn.F());
		}
	}

	/** A conifer branch tier seen from the side: a drooping skirt (apex at the top centre = the trunk above, the
	 *  branch tips hanging to the lower corners), needle sprays along each branch, darker underneath and towards the
	 *  trunk. Stacked camera-facing tiers make the far-LOD spruce. */
	void PaintNeedleClump(FLeafCanvas& Cv, const FIntRect& R, FLifeRand& Rn)
	{
		const float T = float(R.Width());
		const FVector2f O(R.Min.X, R.Min.Y);
		auto Px = [&](float X01, float Y01) { return O + FVector2f(X01 * T, Y01 * T); };   // y down
		const FLinearColor Twig = SRGB(0.22f, 0.17f, 0.12f);
		DrawLine(Cv, R, Px(0.5f, 0.02f), Px(0.5f, 0.98f), 14.f, 9.f, Twig, Twig);
		const int32 NB = 12;
		for (int32 Pass = 0; Pass < 2; ++Pass)
			for (int32 Bi = 0; Bi < NB; ++Bi)
				for (int32 Sd = -1; Sd <= 1; Sd += 2)
				{
					const float F = (Bi + Rn.R(0.f, 0.7f)) / NB;                // 0 top .. 1 bottom
					const float Y0 = 0.04f + 0.62f * F + (Pass ? 0.03f : 0.f);
					const float Reach = (0.12f + 0.36f * FMath::Pow(F, 0.75f)) * Rn.R(0.85f, 1.05f);
					const float Drop = Reach * Rn.R(0.35f, 0.6f) + 0.04f;
					const FVector2f A = Px(0.5f + Sd * 0.01f, Y0);
					const FVector2f B = Px(0.5f + Sd * Reach, FMath::Min(0.95f, Y0 + Drop));
					const FVector2f C = Px(0.5f + Sd * Reach * 0.6f, Y0 + Drop * 0.15f);
					FVector2f Prev = A;
					for (int32 K = 1; K <= 10; ++K)
					{
						const FVector2f Cur = Bezier(A, C, B, K / 10.f);
						DrawLine(Cv, R, Prev, Cur, 5.f, 2.5f, Twig, Twig);
						Prev = Cur;
					}
					const float ArcLen = (B - A).Size();
					const int32 N = FMath::Max(6, int32(ArcLen / 2.2f));
					for (int32 K = 0; K < N; ++K)
					{
						const float Tt = (K + 0.5f) / N;
						const FVector2f P0 = Bezier(A, C, B, Tt);
						const FVector2f Tan = (Bezier(A, C, B, FMath::Min(Tt + 0.01f, 1.f)) - P0).GetSafeNormal();
						for (int32 Nd = -1; Nd <= 1; Nd += 2)
						{
							const float An = FMath::DegreesToRadians(Rn.R(35.f, 80.f)) * Nd;
							const FVector2f Dir(Tan.X * FMath::Cos(An) - Tan.Y * FMath::Sin(An), Tan.X * FMath::Sin(An) + Tan.Y * FMath::Cos(An));
							const float Len = T * 0.06f * Rn.R(0.8f, 1.2f) * (1.f - 0.4f * Tt * Tt) * (0.75f + 0.5f * F);
							// lit from above: the upper side of each branch and the outer ends are lighter
							const float Lit = (Dir.Y < 0.f ? 1.08f : 0.78f) * FMath::Lerp(0.62f, 1.1f, Tt) * (Pass ? 0.85f : 1.f) * FMath::Lerp(1.05f, 0.8f, F);
							const FLinearColor Base = FMath::Lerp(SRGB(0.14f, 0.25f, 0.11f), SRGB(0.21f, 0.34f, 0.14f), Rn.F()) * Lit;
							const FLinearColor TipC = (Tt > 0.85f) ? SRGB(0.29f, 0.41f, 0.20f) * Lit : Base * 1.12f;
							DrawLine(Cv, R, P0, P0 + Dir * Len, 3.4f, 1.4f, Base, TipC);
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

UTexture2D* DmNature::MakeTexture(const FString& Name, int32 W, int32 H, TArray<FColor>&& Mip0, bool bSRGB, bool bAlphaCoverage, float AlphaRef, bool bDilate, bool bClampX, bool bClampY,
	int32 CoverageTilesX, int32 CoverageTilesY)
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

	// Alpha coverage is preserved per tile (an atlas mixes sparse sprays and dense clumps: one global scale
	// would thicken the sprays and thin the clumps).
	const int32 Tx = FMath::Max(1, CoverageTilesX), Ty = FMath::Max(1, CoverageTilesY);
	auto CoverageOf = [AlphaRef](const TArray<FColor>& P, int32 Pw, const FIntRect& Rc, float Scale)
	{
		int64 N = 0, All = 0;
		for (int32 Y = Rc.Min.Y; Y < Rc.Max.Y; ++Y)
			for (int32 X = Rc.Min.X; X < Rc.Max.X; ++X) { ++All; if (P[Y * Pw + X].A / 255.f * Scale > AlphaRef) ++N; }
		return double(N) / FMath::Max<int64>(1, All);
	};
	auto TileRect = [Tx, Ty](int32 Pw, int32 Ph, int32 I, int32 J) { return FIntRect(I * Pw / Tx, J * Ph / Ty, (I + 1) * Pw / Tx, (J + 1) * Ph / Ty); };
	TArray<double> Target; Target.Init(0.0, Tx * Ty);
	if (bAlphaCoverage)
		for (int32 J = 0; J < Ty; ++J)
			for (int32 I = 0; I < Tx; ++I) Target[J * Tx + I] = CoverageOf(Mip0, W, TileRect(W, H, I, J), 1.f);

	UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8, MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), FName(*Name)));
	Tex->SRGB = bSRGB;
	Tex->LODGroup = TEXTUREGROUP_World;
	Tex->NeverStream = true;
	Tex->AddressX = bClampX ? TA_Clamp : TA_Wrap; Tex->AddressY = bClampY ? TA_Clamp : TA_Wrap;
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
		if (bAlphaCoverage && Nw >= Tx * 2 && Nh >= Ty * 2)
		{
			// scale alpha so the fraction of texels passing the clip matches mip 0 (keeps canopies dense far away)
			for (int32 J = 0; J < Ty; ++J)
				for (int32 I = 0; I < Tx; ++I)
				{
					const double Tg = Target[J * Tx + I];
					if (Tg <= 0.0) continue;
					const FIntRect Rc = TileRect(Nw, Nh, I, J);
					float Lo = 0.5f, Hi = 8.f;
					for (int32 It = 0; It < 16; ++It) { const float Mid = 0.5f * (Lo + Hi); if (CoverageOf(Cur, Nw, Rc, Mid) < Tg) Lo = Mid; else Hi = Mid; }
					const float Sc = 0.5f * (Lo + Hi);
					for (int32 Y = Rc.Min.Y; Y < Rc.Max.Y; ++Y)
						for (int32 X = Rc.Min.X; X < Rc.Max.X; ++X) { FColor& C = Cur[Y * Nw + X]; C.A = uint8(FMath::Clamp(C.A * Sc, 0.f, 255.f)); }
				}
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

void FDmNatureTextures::AddReferencedObjects(TArray<UObject*>& Out) const
{
	for (UTexture2D* T : { LeafAtlas, Bark, BarkN, PalmBark, PalmBarkN }) if (T) Out.Add(T);
}

namespace
{
	/** Writes a downscaled BMP (alpha shown over grey) for judging the painters (-naturedumptex=<dir>). */
	void DumpBmp(const FString& Path, int32 W, int32 H, const TArray<FColor>& Px, int32 Down)
	{
		const int32 Ow = W / Down, Oh = H / Down;
		TArray<FColor> Out; Out.SetNumUninitialized(Ow * Oh);
		for (int32 Y = 0; Y < Oh; ++Y)
			for (int32 X = 0; X < Ow; ++X)
			{
				const FColor& C = Px[(Y * Down) * W + X * Down];
				const float A = C.A / 255.f;
				Out[Y * Ow + X] = FColor(uint8(C.R * A + 90 * (1 - A)), uint8(C.G * A + 90 * (1 - A)), uint8(C.B * A + 90 * (1 - A)), 255);
			}
		FFileHelper::CreateBitmap(*Path, Ow, Oh, Out.GetData());
	}
}

void FDmNatureTextures::Build()
{
	FString DumpDir;
	FParse::Value(FCommandLine::Get(), TEXT("-naturedumptex="), DumpDir);
	const double T0 = FPlatformTime::Seconds();
	// ---- leaf atlas 4096 x 2048 (8 tiles of 1024, see EDmLeafTile)
	{
		const int32 Sw = 4096, Sh = 2048, Tl = 1024, Pad = 20;
		FLeafCanvas Cv(Sw, Sh);
		ParallelFor(int32(EDmLeafTile::Num), [&](int32 I)
		{
			FLifeRand R2(777 + I * 31);
			const FIntRect Rc(FIntPoint((I % 4) * Tl + Pad, (I / 4) * Tl + Pad), FIntPoint((I % 4) * Tl + Tl - Pad, (I / 4) * Tl + Tl - Pad));
			switch (EDmLeafTile(I))
			{
			case EDmLeafTile::Broadleaf: PaintBroadleaf(Cv, Rc, R2); break;
			case EDmLeafTile::Needles: PaintNeedles(Cv, Rc, R2); break;
			case EDmLeafTile::Frond: PaintFrond(Cv, Rc, R2); break;
			case EDmLeafTile::Leaflets: PaintLeaflets(Cv, Rc, R2); break;
			case EDmLeafTile::BroadClump: case EDmLeafTile::BroadClumpB: PaintClump(Cv, Rc, R2, false); break;
			case EDmLeafTile::NeedleClump: PaintNeedleClump(Cv, Rc, R2); break;
			case EDmLeafTile::LeafletClump: PaintClump(Cv, Rc, R2, true); break;
			default: break;
			}
		});
		TArray<FColor> Px; Px.SetNumUninitialized(Sw * Sh);
		for (int32 I = 0; I < Sw * Sh; ++I) Px[I] = ToColor(Cv.P[I]);
		if (!DumpDir.IsEmpty()) DumpBmp(DumpDir / TEXT("nature-leafatlas.bmp"), Sw, Sh, Px, 2);
		LeafAtlas = DmNature::MakeTexture(TEXT("T_LeafAtlas"), Sw, Sh, MoveTemp(Px), true, true, 0.4f, true, false, false, 4, 2);
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
		Bark = DmNature::MakeTexture(TEXT("T_Bark"), S, S, MoveTemp(Px), true, false);
		TArray<FVector2f> N; HeightToNormal(Hh, S, S, 6.f, N);
		TArray<FColor> Np; Np.SetNumUninitialized(S * S);
		for (int32 I = 0; I < S * S; ++I) { Np[I].R = Enc(N[I].X); Np[I].G = Enc(N[I].Y); Np[I].B = 255; Np[I].A = 255; }
		BarkN = DmNature::MakeTexture(TEXT("T_BarkN"), S, S, MoveTemp(Np), false, false);
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
		PalmBark = DmNature::MakeTexture(TEXT("T_PalmBark"), S, S, MoveTemp(Px), true, false);
		TArray<FVector2f> N; HeightToNormal(Hh, S, S, 5.f, N);
		TArray<FColor> Np; Np.SetNumUninitialized(S * S);
		for (int32 I = 0; I < S * S; ++I) Np[I] = FColor(Enc(N[I].X), Enc(N[I].Y), 255, 255);
		PalmBarkN = DmNature::MakeTexture(TEXT("T_PalmBarkN"), S, S, MoveTemp(Np), false, false);
	}
	UE_LOG(LogDmNatureTex, Display, TEXT("DmNature: textures painted in %.2f s"), FPlatformTime::Seconds() - T0);
}
