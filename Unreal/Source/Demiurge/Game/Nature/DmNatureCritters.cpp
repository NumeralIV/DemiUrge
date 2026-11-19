// DEMIURGE — Game/Nature/DmNatureCritters.cpp : see the header.

#include "Game/Nature/DmNatureCritters.h"

namespace
{
	using namespace DmNature;
	const FVector3f Up(0, 0, 1);

	/** A curved, tapered grass blade (two-sided material), 3 segments. */
	void Blade(FDmNatureMesh& M, FRand& R, const FVector3f& Base, float H, float W, const FColor& Root, const FColor& Tip, int32 Segs)
	{
		const float Az = R.R(0.f, 2.f * PI);
		const FVector3f Lean(FMath::Cos(Az), FMath::Sin(Az), 0.f);
		const FVector3f Side = FVector3f::CrossProduct(Up, Lean).GetSafeNormal();
		const float Bend = R.R(0.15f, 0.55f);
		const int32 B0 = M.V.Num();
		for (int32 K = 0; K <= Segs; ++K)
		{
			const float T = float(K) / Segs;
			const FVector3f C = Base + Up * (H * T) + Lean * (H * Bend * T * T);
			const float Hw = 0.5f * W * (1.f - 0.92f * T);
			const FVector3f Tan = (Up + Lean * (2.f * Bend * T)).GetSafeNormal();
			const FVector3f N = FVector3f::CrossProduct(Side, Tan).GetSafeNormal();
			const FColor Cc = FColor(uint8(FMath::Lerp(float(Root.R), float(Tip.R), T)), uint8(FMath::Lerp(float(Root.G), float(Tip.G), T)),
				uint8(FMath::Lerp(float(Root.B), float(Tip.B), T)), uint8(FMath::Lerp(120.f, 255.f, T)));
			const FVector2f Uv1(0.12f * H * T * T + 0.02f, 0.f);
			M.Add(C - Side * Hw, (N + Up * 0.6f).GetSafeNormal(), FVector2f(0.f, T), Cc, Uv1);
			M.Add(C + Side * Hw, (N + Up * 0.6f).GetSafeNormal(), FVector2f(1.f, T), Cc, Uv1);
		}
		for (int32 K = 0; K < Segs; ++K)
		{
			const uint32 a = B0 + 2 * K, b = a + 1, c = a + 3, d = a + 2;
			const FVector3f N = M.V[a].N;
			M.Quad(0, a, b, c, d, N);
		}
	}

	void BuildGrass(int32 Variant, TArray<FDmNatureMesh>& Out, float& Size)
	{
		FRand R(901 + Variant * 13);
		const bool bDry = (Variant & 1) != 0;
		const FColor Root = bDry ? Col(0.36f, 0.33f, 0.20f) : Col(0.20f, 0.27f, 0.11f);
		const FColor Tip = bDry ? Col(0.70f, 0.62f, 0.38f) : Col(0.46f, 0.56f, 0.24f);
		const int32 NB[3] = { 30, 18, 9 };
		const int32 Sg[3] = { 3, 2, 1 };
		const float Rad = R.R(0.25f, 0.4f);
		Size = 0.8f;
		Out.SetNum(3);
		for (int32 L = 0; L < 3; ++L)
		{
			FDmNatureMesh& M = Out[L];
			M = FDmNatureMesh(1);
			FRand Rb(77 + Variant * 5);
			for (int32 I = 0; I < NB[L]; ++I)
			{
				const float A = Rb.R(0.f, 2.f * PI), D = Rad * FMath::Sqrt(Rb.F());
				const float H = Rb.R(0.35f, 0.75f) * (L == 2 ? 0.9f : 1.f);
				const float W = Rb.R(0.022f, 0.04f) * (L == 0 ? 1.f : (L == 1 ? 1.35f : 1.9f));
				Blade(M, Rb, FVector3f(FMath::Cos(A) * D, FMath::Sin(A) * D, -0.05f), H, W, Root, Tip, Sg[L]);
			}
		}
	}

	void BuildGrazer(int32 Variant, TArray<FDmNatureMesh>& Out, float& Size)
	{
		// 0 deer, 1 antelope, 2 bison, 3 reindeer: body proportions and coat (belly lighter via vertex colour)
		struct FSpec { float Len, Girth, Leg, Neck, HeadL; FColor Back, Belly; bool bHorns, bHump; };
		const FSpec Specs[4] = {
			{ 0.78f, 0.24f, 0.78f, 0.55f, 0.24f, Col(0.42f, 0.28f, 0.17f), Col(0.70f, 0.60f, 0.48f), false, false },
			{ 0.70f, 0.21f, 0.72f, 0.50f, 0.22f, Col(0.62f, 0.45f, 0.26f), Col(0.90f, 0.86f, 0.78f), true, false },
			{ 1.20f, 0.48f, 0.62f, 0.30f, 0.34f, Col(0.24f, 0.17f, 0.11f), Col(0.33f, 0.25f, 0.17f), true, true },
			{ 0.85f, 0.29f, 0.74f, 0.52f, 0.26f, Col(0.40f, 0.36f, 0.31f), Col(0.78f, 0.75f, 0.70f), true, false },
		};
		const FSpec& S = Specs[FMath::Clamp(Variant, 0, 3)];
		Size = S.Len * 2.f;
		const int32 Seg[3] = { 12, 8, 5 }, Rings[3] = { 8, 6, 4 }, LegSides[3] = { 6, 4, 3 };
		Out.SetNum(3);
		for (int32 L = 0; L < 3; ++L)
		{
			FDmNatureMesh& M = Out[L];
			M = FDmNatureMesh(1);
			const float BodyZ = S.Leg + S.Girth * 0.7f;
			// body (front of the animal = +X)
			const int32 B0 = M.V.Num();
			M.Ellipsoid(0, FVector3f(0, 0, BodyZ), FVector3f(S.Len, S.Girth * 0.9f, S.Girth), FQuat4f::Identity, Seg[L], Rings[L], S.Back);
			if (S.bHump) M.Ellipsoid(0, FVector3f(S.Len * 0.4f, 0, BodyZ + S.Girth * 0.55f), FVector3f(S.Len * 0.45f, S.Girth * 0.7f, S.Girth * 0.65f), FQuat4f::Identity, Seg[L], Rings[L], S.Back);
			for (int32 I = B0; I < M.V.Num(); ++I)
			{
				// lighter belly
				const float T = FMath::Clamp((BodyZ - M.V[I].P.Z) / S.Girth * 1.4f, 0.f, 1.f);
				M.V[I].C = FColor(uint8(FMath::Lerp(float(S.Back.R), float(S.Belly.R), T)), uint8(FMath::Lerp(float(S.Back.G), float(S.Belly.G), T)),
					uint8(FMath::Lerp(float(S.Back.B), float(S.Belly.B), T)), 255);
			}
			// legs
			for (int32 K = 0; K < 4; ++K)
			{
				const float X = (K < 2 ? 0.62f : -0.62f) * S.Len, Y = (K % 2 ? 0.45f : -0.45f) * S.Girth;
				M.Limb(0, FVector3f(X, Y, BodyZ - S.Girth * 0.3f), FVector3f(X + (K < 2 ? 0.02f : -0.04f), Y, 0.f), S.Girth * 0.28f, S.Girth * 0.12f, LegSides[L], ColMul(S.Back, 0.8f));
			}
			// neck and head (head weight in UV1.x for the grazing bob)
			const int32 H0 = M.V.Num();
			const FVector3f NeckA(S.Len * 0.78f, 0, BodyZ + S.Girth * 0.3f);
			const FVector3f NeckB = NeckA + FVector3f(S.Neck * 0.45f, 0, S.Neck * 0.85f);
			M.Limb(0, NeckA, NeckB, S.Girth * 0.42f, S.Girth * 0.26f, LegSides[L] + 2, S.Back);
			const FVector3f HeadC = NeckB + FVector3f(S.HeadL * 0.55f, 0, -S.HeadL * 0.1f);
			M.Ellipsoid(0, HeadC, FVector3f(S.HeadL * 0.62f, S.Girth * 0.3f, S.Girth * 0.32f), FQuat4f(FVector3f(0, 1, 0), 0.35f), Seg[L] * 2 / 3, Rings[L] * 2 / 3 + 1, ColMul(S.Back, 0.9f));
			if (L < 2)
			{
				for (int32 Sd = -1; Sd <= 1; Sd += 2)
				{
					const FVector3f E0 = NeckB + FVector3f(0.02f, Sd * S.Girth * 0.22f, S.Girth * 0.18f);
					M.Limb(0, E0, E0 + FVector3f(-0.05f, Sd * 0.12f, 0.1f), 0.03f, 0.01f, 3, ColMul(S.Back, 0.8f));
					if (S.bHorns) M.Limb(0, E0 + FVector3f(0.04f, -Sd * 0.04f, 0.02f), E0 + FVector3f(-0.12f, Sd * 0.1f, 0.32f), 0.025f, 0.008f, 3, Col(0.25f, 0.22f, 0.18f));
				}
			}
			for (int32 I = H0; I < M.V.Num(); ++I)
				M.V[I].UV1 = FVector2f(FMath::Clamp((M.V[I].P.X - S.Len * 0.7f) / (S.Neck + S.HeadL), 0.f, 1.f), 0.f);
			// tail
			M.Limb(0, FVector3f(-S.Len * 0.95f, 0, BodyZ + S.Girth * 0.3f), FVector3f(-S.Len * 1.1f, 0, BodyZ - S.Girth * 0.2f), 0.05f, 0.02f, 3, S.Back);
		}
	}

	void BuildBird(int32 Variant, TArray<FDmNatureMesh>& Out, float& Size)
	{
		// facing +Y (the circle's tangent at local +X of the centre); wingspan ~1.8 m (large birds read from the god camera)
		const bool bWhite = (Variant & 1) != 0;
		const FColor Body = bWhite ? Col(0.86f, 0.86f, 0.84f) : Col(0.16f, 0.15f, 0.14f);
		const FColor WingTip = bWhite ? Col(0.18f, 0.18f, 0.18f) : Col(0.10f, 0.09f, 0.09f);
		const float Span = bWhite ? 1.7f : 1.9f;
		Size = Span;
		Out.SetNum(2);
		for (int32 L = 0; L < 2; ++L)
		{
			FDmNatureMesh& M = Out[L];
			M = FDmNatureMesh(1);
			M.Ellipsoid(0, FVector3f::ZeroVector, FVector3f(0.09f, 0.32f, 0.09f), FQuat4f::Identity, L ? 6 : 10, L ? 4 : 6, Body);
			M.Ellipsoid(0, FVector3f(0, 0.33f, 0.03f), FVector3f(0.055f, 0.08f, 0.055f), FQuat4f::Identity, 6, 4, Body);
			// wings: two segments each (inner, outer), flap weight grows outward
			for (int32 Sd = -1; Sd <= 1; Sd += 2)
			{
				const int32 W0 = M.V.Num();
				const float Xs[3] = { 0.06f, Span * 0.25f, Span * 0.5f };
				const float Chord[3] = { 0.34f, 0.30f, 0.14f };
				for (int32 K = 0; K < 3; ++K)
				{
					const float X = Sd * Xs[K];
					const FColor C = K == 2 ? WingTip : Body;
					const FVector2f Uv1(Xs[K] / (Span * 0.5f), 0.f);
					M.Add(FVector3f(X, 0.12f, 0.f), Up, FVector2f(0, 0), C, Uv1);
					M.Add(FVector3f(X, 0.12f - Chord[K], 0.01f * K), Up, FVector2f(0, 1), C, Uv1);
				}
				for (int32 K = 0; K < 2; ++K)
				{
					const uint32 a = W0 + 2 * K, b = a + 1, c = a + 3, d = a + 2;
					M.Quad(0, a, b, c, d, Up);
				}
			}
			// tail
			const int32 T0 = M.V.Num();
			M.Add(FVector3f(-0.05f, -0.28f, 0), Up, FVector2f(0, 0), Body);
			M.Add(FVector3f(0.05f, -0.28f, 0), Up, FVector2f(1, 0), Body);
			M.Add(FVector3f(0.11f, -0.48f, 0), Up, FVector2f(1, 1), Body);
			M.Add(FVector3f(-0.11f, -0.48f, 0), Up, FVector2f(0, 1), Body);
			M.Quad(0, T0, T0 + 1, T0 + 2, T0 + 3, Up);
		}
	}

	void BuildFish(int32 Variant, TArray<FDmNatureMesh>& Out, float& Size)
	{
		// facing +Y; 0.7-1.2 m (tuna-like), silver blue back
		const float Len = Variant ? 1.15f : 0.75f;
		Size = Len;
		const FColor Back = Variant ? Col(0.10f, 0.16f, 0.24f) : Col(0.24f, 0.32f, 0.36f);
		const FColor Belly = Col(0.78f, 0.80f, 0.80f);
		Out.SetNum(2);
		for (int32 L = 0; L < 2; ++L)
		{
			FDmNatureMesh& M = Out[L];
			M = FDmNatureMesh(1);
			const int32 B0 = M.V.Num();
			M.Ellipsoid(0, FVector3f::ZeroVector, FVector3f(Len * 0.09f, Len * 0.5f, Len * 0.13f), FQuat4f::Identity, L ? 6 : 10, L ? 4 : 6, Back);
			for (int32 I = B0; I < M.V.Num(); ++I)
			{
				const float T = FMath::Clamp(-M.V[I].P.Z / (Len * 0.13f) + 0.3f, 0.f, 1.f);
				M.V[I].C = FColor(uint8(FMath::Lerp(float(Back.R), float(Belly.R), T)), uint8(FMath::Lerp(float(Back.G), float(Belly.G), T)), uint8(FMath::Lerp(float(Back.B), float(Belly.B), T)), 255);
				M.V[I].UV1 = FVector2f(FMath::Clamp(-M.V[I].P.Y / (Len * 0.5f), 0.f, 1.f), 0.f);
			}
			// tail fin (vertical)
			const int32 T0 = M.V.Num();
			const FVector3f Side(1, 0, 0);
			M.Add(FVector3f(0, -Len * 0.45f, 0), Side, FVector2f(0, 0), Back, FVector2f(1, 0));
			M.Add(FVector3f(0, -Len * 0.66f, Len * 0.16f), Side, FVector2f(1, 0), Back, FVector2f(1, 0));
			M.Add(FVector3f(0, -Len * 0.58f, 0), Side, FVector2f(1, 1), Back, FVector2f(1, 0));
			M.Add(FVector3f(0, -Len * 0.66f, -Len * 0.16f), Side, FVector2f(0, 1), Back, FVector2f(1, 0));
			M.Quad(0, T0, T0 + 1, T0 + 2, T0 + 3, Side);
		}
	}
}

const TCHAR* DmNature::CritterName(EDmCritter Kind)
{
	switch (Kind)
	{
	case EDmCritter::Grass: return TEXT("Grass");
	case EDmCritter::Grazer: return TEXT("Grazer");
	case EDmCritter::Bird: return TEXT("Bird");
	case EDmCritter::Fish: return TEXT("Fish");
	default: return TEXT("?");
	}
}

void DmNature::BuildCritter(EDmCritter Kind, int32 Variant, TArray<FDmNatureMesh>& OutLODs, float& OutSize)
{
	OutLODs.Reset();
	OutSize = 1.f;
	switch (Kind)
	{
	case EDmCritter::Grass: BuildGrass(Variant, OutLODs, OutSize); break;
	case EDmCritter::Grazer: BuildGrazer(Variant, OutLODs, OutSize); break;
	case EDmCritter::Bird: BuildBird(Variant, OutLODs, OutSize); break;
	case EDmCritter::Fish: BuildFish(Variant, OutLODs, OutSize); break;
	default: break;
	}
}
