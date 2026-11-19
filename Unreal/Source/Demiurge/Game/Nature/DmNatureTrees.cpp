// DEMIURGE — Game/Nature/DmNatureTrees.cpp : see the header. Conifer, broadleaf, palm, acacia and shrub
// ported from the LifeLab (LifeTrees.cpp); the dead tree is new.

#include "Game/Nature/DmNatureTrees.h"
#include "Game/Nature/DmNatureTextures.h"

namespace
{
	const FVector3f Up(0.f, 0.f, 1.f);

	using FSwayFn = TFunctionRef<float(const FVector3f&)>;
	using FVecFn = TFunctionRef<FVector3f(const FVector3f&)>;
	using FScalarFn = TFunctionRef<float(const FVector3f&)>;

	/** Tapered tube along a polyline with parallel-transported frames. Smooth normals; UV u around (m), v along (m). */
	void Tube(FDmNatureMesh& M, const TArray<FVector3f>& Pts, const TArray<float>& Rad, int32 Sides, const FColor& C, FSwayFn Sway, FScalarFn AO)
	{
		const int32 N = Pts.Num();
		if (N < 2 || Sides < 3) return;
		FVector3f T0 = (Pts[1] - Pts[0]).GetSafeNormal();
		FVector3f Nr = FMath::Abs(T0.Z) < 0.9f ? FVector3f::CrossProduct(T0, Up) : FVector3f::CrossProduct(T0, FVector3f(1, 0, 0));
		Nr.Normalize();
		const int32 Base = M.V.Num();
		const float Circ = FMath::Max(2.f * PI * Rad[0], 0.25f);
		float Len = 0.f;
		for (int32 I = 0; I < N; ++I)
		{
			FVector3f T = (I == 0) ? Pts[1] - Pts[0] : (I == N - 1) ? Pts[N - 1] - Pts[N - 2] : Pts[I + 1] - Pts[I - 1];
			T.Normalize();
			Nr = (Nr - T * FVector3f::DotProduct(Nr, T)).GetSafeNormal();
			const FVector3f B = FVector3f::CrossProduct(T, Nr);
			if (I > 0) Len += (Pts[I] - Pts[I - 1]).Size();
			const float Ao = AO(Pts[I]);
			const FColor Cc = DmNature::ColMul(C, Ao);
			for (int32 S = 0; S <= Sides; ++S)
			{
				const float A = 2.f * PI * S / Sides;
				const FVector3f D = Nr * FMath::Cos(A) + B * FMath::Sin(A);
				M.Add(Pts[I] + D * Rad[I], D, FVector2f(Circ * S / Sides, Len), Cc, FVector2f(Sway(Pts[I]), 0.f));
			}
		}
		for (int32 I = 0; I < N - 1; ++I)
			for (int32 S = 0; S < Sides; ++S)
			{
				const uint32 a = Base + I * (Sides + 1) + S, b = a + 1, d = a + (Sides + 1), c = d + 1;
				M.Quad(0, a, b, c, d, M.V[a].N + M.V[b].N);
			}
	}

	/** A leaf card: base, twig direction DirT, across DirS. Two copies (front/back) share a volumetric normal. */
	void Card(FDmNatureMesh& M, const FVector3f& Base, const FVector3f& DirT, const FVector3f& DirS, float Len, float Wid, EDmLeafTile Tile, const FColor& Tint,
		FVecFn Radial, float RadialBlend, FScalarFn AO, FSwayFn Sway, float Bend, int32 Segs, float Flutter = 0.06f)
	{
		const FVector3f Face = FVector3f::CrossProduct(DirS, DirT).GetSafeNormal();
		for (int32 Copy = 0; Copy < 2; ++Copy)
		{
			const int32 B0 = M.V.Num();
			for (int32 K = 0; K <= Segs; ++K)
			{
				const float T = float(K) / Segs;
				const FVector3f Pc = Base + DirT * (Len * T) - Up * (Bend * Len * T * T);
				for (int32 J = 0; J < 2; ++J)
				{
					const float S = J ? 0.5f : -0.5f;
					const FVector3f P = Pc + DirS * (Wid * S);
					const FVector3f Rn = Radial(P);
					const FVector3f F = FVector3f::DotProduct(Face, Rn) >= 0.f ? Face : -Face;
					const FVector3f N = (Rn * RadialBlend + F * (1.f - RadialBlend)).GetSafeNormal();
					FColor C = Tint;
					C.A = uint8(FMath::Clamp(AO(P), 0.f, 1.f) * 255.f + 0.5f);
					M.Add(P, N, DmLeafUV(Tile, S + 0.5f, T), C, FVector2f(Sway(P), Flutter * T + 0.01f));
				}
			}
			const FVector3f Want = Copy == 0 ? Face : -Face;
			for (int32 K = 0; K < Segs; ++K)
			{
				const uint32 a = B0 + 2 * K, b = a + 1, c = a + 3, d = a + 2;
				M.Quad(1, a, b, c, d, Want);
			}
		}
	}

	FVector3f Mul(const FVector3f& A, const FVector3f& B) { return FVector3f(A.X * B.X, A.Y * B.Y, A.Z * B.Z); }
	FVector3f Div(const FVector3f& A, const FVector3f& B) { return FVector3f(A.X / B.X, A.Y / B.Y, A.Z / B.Z); }

	FVector3f RotateAbout(const FVector3f& V, const FVector3f& Axis, float Ang)
	{
		return FQuat4f(Axis.GetSafeNormal(), Ang).RotateVector(V);
	}

	struct FBranch
	{
		TArray<FVector3f> P;
		TArray<float> R;
		int32 Level = 0;
	};

	/** A curving branch from Start along Dir, bending towards Bias each step. */
	FBranch Grow(DmNature::FRand& Rn, const FVector3f& Start, FVector3f Dir, float Len, int32 Steps, float R0, float R1, const FVector3f& Bias, float BiasK, float Wobble, int32 Level)
	{
		FBranch B; B.Level = Level;
		FVector3f P = Start;
		B.P.Add(P); B.R.Add(R0);
		const float Step = Len / Steps;
		for (int32 K = 1; K <= Steps; ++K)
		{
			Dir = (Dir + Bias * BiasK + Rn.Unit() * Wobble).GetSafeNormal();
			P += Dir * Step;
			B.P.Add(P);
			B.R.Add(FMath::Lerp(R0, R1, float(K) / Steps));
		}
		return B;
	}

	FVector3f PointOn(const FBranch& B, float T, float* OutR = nullptr, FVector3f* OutDir = nullptr)
	{
		const float F = FMath::Clamp(T, 0.f, 0.999f) * (B.P.Num() - 1);
		const int32 I = FMath::FloorToInt(F);
		const float U = F - I;
		if (OutR) *OutR = FMath::Lerp(B.R[I], B.R[I + 1], U);
		if (OutDir) *OutDir = (B.P[I + 1] - B.P[I]).GetSafeNormal();
		return FMath::Lerp(B.P[I], B.P[I + 1], U);
	}

	void EmitBranches(FDmNatureMesh& M, const TArray<FBranch>& Br, const int32 SidesByLevel[3], const FColor& Bark, FSwayFn Sway, FScalarFn AO)
	{
		for (const FBranch& B : Br)
		{
			const int32 Sd = SidesByLevel[FMath::Min(B.Level, 2)];
			if (Sd >= 3) Tube(M, B.P, B.R, Sd, Bark, Sway, AO);
		}
	}

	/** Far-LOD clump card centred at P, its plane facing Nn (the crown's outward direction), the tile's lit top turned
	 *  towards the sky, rolled by Roll radians about Nn. */
	void ClumpCard(FDmNatureMesh& M, const FVector3f& P, const FVector3f& Nn, float Roll, float Len, float Wid, EDmLeafTile Tile, const FColor& Tint,
		FVecFn Radial, float RadialBlend, FScalarFn AO, FSwayFn Sway)
	{
		FVector3f Dt = Up - Nn * FVector3f::DotProduct(Up, Nn);
		if (Dt.SizeSquared() < 1e-4f) Dt = FVector3f::CrossProduct(Nn, FVector3f(1, 0, 0));
		Dt = RotateAbout(Dt.GetSafeNormal(), Nn, Roll);
		const FVector3f Ds = FVector3f::CrossProduct(Nn, Dt).GetSafeNormal();
		Card(M, P - Dt * (Len * 0.5f), Dt, Ds, Len, Wid, Tile, Tint, Radial, RadialBlend, AO, Sway, 0.f, 1, 0.02f);
	}

	/** Camera-facing clump card (far LODs): a square-ish quad around Centre that M_NatureLeaf turns towards the camera
	 *  every frame (UV2..UV4, see FDmNatureVert), lit top up; two back-to-back copies so one always faces the view.
	 *  Normals: the crown's radial direction at the centre, tilted up at the top corners and down at the bottom, so
	 *  the card shades like part of a round mass. */
	void BillboardCard(FDmNatureMesh& M, const FVector3f& Centre, float Wid, float Len, EDmLeafTile Tile, const FColor& Tint,
		FVecFn Radial, FScalarFn AO, FSwayFn Sway)
	{
		const FVector3f Ds(0.f, 1.f, 0.f), Dt(0.f, 0.f, 1.f);   // initial plane (faces +X); the material re-orients it
		const FVector3f Rn = Radial(Centre);
		const float Ao = FMath::Clamp(AO(Centre), 0.f, 1.f);
		for (int32 Copy = 0; Copy < 2; ++Copy)
		{
			const int32 B0 = M.V.Num();
			for (int32 T = 0; T < 2; ++T)
				for (int32 S = 0; S < 2; ++S)
				{
					const FVector3f P = Centre + Ds * (Wid * (S - 0.5f)) + Dt * (Len * (T - 0.5f));
					// mostly sky-facing: a camera-facing card turns, its normal does not, so a purely radial normal would
					// leave half the crown in shade whatever the sun does
					const FVector3f N = (Rn * 0.55f + Up * (0.75f + 0.5f * (T - 0.5f))).GetSafeNormal();
					FColor C = Tint;
					C.A = uint8(FMath::Clamp(Ao * (T ? 1.f : 0.86f), 0.f, 1.f) * 255.f + 0.5f);
					const int32 I = M.Add(P, N, DmLeafUV(Tile, float(S), float(T)), C, FVector2f(Sway(Centre), 0.01f));
					const FVector3f D = Centre - P;
					M.V[I].UV2 = FVector2f(D.X, D.Y);
					M.V[I].UV3 = FVector2f(D.Z, 1.f + S + 2.f * T);
					M.V[I].UV4 = FVector2f(Wid, Len);
				}
			// corners: 0 (s0,t0) 1 (s1,t0) 2 (s0,t1) 3 (s1,t1); front of copy 0 = cross(Ds, Dt) = +X (the material maps
			// Ds -> camera right, Dt -> up, so +X -> towards the camera)
			const FVector3f Face = FVector3f::CrossProduct(Ds, Dt);
			M.Quad(1, B0 + 0, B0 + 1, B0 + 3, B0 + 2, Copy == 0 ? Face : -Face);
		}
	}

	// =========================================================== broadleaf
	void BuildBroadleaf(DmNature::FRand& R, FDmTreeBuild& Out, bool bShrub)
	{
		const float H = bShrub ? R.R(1.2f, 2.4f) : R.R(11.f, 17.f);
		const float Tr = bShrub ? 0.035f : H * R.R(0.022f, 0.03f);
		const FVector3f CC(R.R(-0.08f, 0.08f) * H, R.R(-0.08f, 0.08f) * H, bShrub ? H * 0.5f : H * 0.63f);
		const FVector3f CR = bShrub ? FVector3f(H * R.R(0.55f, 0.8f), H * R.R(0.55f, 0.8f), H * 0.5f)
			: FVector3f(H * R.R(0.32f, 0.4f), H * R.R(0.32f, 0.4f), H * R.R(0.28f, 0.33f));
		Out.Height = CC.Z + CR.Z;
		Out.CrownRadius = FMath::Max(CR.X, CR.Y);
		const FColor BarkC = bShrub ? DmNature::Col(0.36f, 0.31f, 0.26f) : DmNature::Col(0.50f, 0.47f, 0.42f);
		const float Hs = H;
		auto Sway = [Hs](const FVector3f& P) { const float Z = FMath::Clamp(P.Z / Hs, 0.f, 1.2f); return 0.018f * Hs * Z * Z + 0.025f * FVector2f(P.X, P.Y).Size(); };
		auto Ell = [CC, CR](const FVector3f& P) { return Div(P - CC, CR).Size(); };
		auto AOb = [Ell, CC, CR](const FVector3f& P) { const float E = Ell(P); return FMath::Clamp(0.25f + 0.75f * E, 0.f, 1.f) * (0.82f + 0.18f * FMath::Clamp((P.Z - CC.Z) / CR.Z * 0.5f + 0.5f, 0.f, 1.f)); };
		auto AOt = [Ell, Hs](const FVector3f& P) { return FMath::Clamp(0.55f + 0.45f * FMath::Min(P.Z / (Hs * 0.35f), 1.f), 0.f, 1.f) * FMath::Clamp(0.5f + 0.5f * Ell(P), 0.6f, 1.f); };
		auto Radial = [CC, CR](const FVector3f& P) { return (Div(P - CC, Mul(CR, CR)).GetSafeNormal() + Up * 0.35f).GetSafeNormal(); };

		TArray<FBranch> Br;
		if (!bShrub)
		{
			const float ForkZ = H * R.R(0.36f, 0.48f);
			const FVector3f Lean(R.R(-0.04f, 0.04f) * H, R.R(-0.04f, 0.04f) * H, 0.f);
			FBranch T; T.Level = 0;
			for (int32 K = 0; K <= 6; ++K)
			{
				const float F = K / 6.f;
				T.P.Add(FVector3f(Lean.X * F * F + R.R(-0.04f, 0.04f), Lean.Y * F * F + R.R(-0.04f, 0.04f), F * ForkZ));
				T.R.Add(Tr * (1.f - 0.25f * F) * (K == 0 ? 1.45f : K == 1 ? 1.1f : 1.f));
			}
			Br.Add(T);
			const FVector3f Fork = T.P.Last();
			const int32 NL = R.I(3, 5);
			const float Az0 = R.R(0.f, 2.f * PI);
			for (int32 I = 0; I < NL; ++I)
			{
				const float Az = Az0 + 2.f * PI * (I + R.R(-0.2f, 0.2f)) / NL;
				const float El = FMath::DegreesToRadians(R.R(25.f, 50.f));
				const FVector3f D0(FMath::Sin(El) * FMath::Cos(Az), FMath::Sin(El) * FMath::Sin(Az), FMath::Cos(El));
				const float L = H * R.R(0.36f, 0.46f);
				FBranch L1 = Grow(R, Fork - FVector3f(0, 0, R.R(0.f, 0.6f)), D0, L, 5, Tr * 0.62f, Tr * 0.1f, Up, 0.08f, 0.12f, 1);
				Br.Add(L1);
				const int32 NS = R.I(3, 4);
				for (int32 S = 0; S < NS; ++S)
				{
					float Rr = 0.f; FVector3f Dd;
					const float Tt = R.R(0.35f, 0.9f);
					const FVector3f Bp = PointOn(L1, Tt, &Rr, &Dd);
					const FVector3f Out2 = RotateAbout(FVector3f(Dd.X, Dd.Y, 0.f).GetSafeNormal(), Up, FMath::DegreesToRadians(R.R(25.f, 65.f)) * (R.F() < 0.5f ? -1.f : 1.f));
					const FVector3f D2 = (Dd * 0.4f + Out2 + Up * R.R(0.f, 0.5f)).GetSafeNormal();
					Br.Add(Grow(R, Bp, D2, L * R.R(0.32f, 0.5f), 3, Rr * 0.6f, 0.02f, Up, 0.1f, 0.15f, 2));
				}
			}
			Br.Add(Grow(R, Fork, (Up + R.Unit() * 0.2f).GetSafeNormal(), H * 0.35f, 4, Tr * 0.5f, 0.05f, Up, 0.05f, 0.1f, 1));
		}
		else
		{
			const int32 NS = R.I(4, 7);
			for (int32 I = 0; I < NS; ++I)
			{
				const float Az = 2.f * PI * (I + R.R(-0.3f, 0.3f)) / NS;
				const FVector3f D0 = FVector3f(FMath::Cos(Az) * 0.6f, FMath::Sin(Az) * 0.6f, 1.f).GetSafeNormal();
				Br.Add(Grow(R, FVector3f(R.R(-0.1f, 0.1f), R.R(-0.1f, 0.1f), 0.f), D0, H * R.R(0.6f, 0.9f), 3, Tr, 0.01f, Up, 0.05f, 0.2f, 1));
			}
		}

		// lobe centres for the far LODs: the branch tips pulled onto the outer half of the crown, plus a crown top
		TArray<FVector3f> Lobes;
		for (const FBranch& B : Br)
		{
			if (B.Level < 1 || B.P.Num() < 2) continue;
			const FVector3f D = Div(B.P.Last() - CC, CR);
			const float E = D.Size();
			if (E < 1e-3f) continue;
			Lobes.Add(CC + Mul(D / E, CR) * FMath::Clamp(E, 0.55f, 0.8f));
		}
		Lobes.Add(CC + FVector3f(0.f, 0.f, CR.Z * 0.62f));
		if (Lobes.Num() > 24) { TArray<FVector3f> Kp; for (int32 I = 0; I < Lobes.Num(); I += 2) Kp.Add(Lobes[I]); Lobes = Kp; }
		const int32 Sides[DmTreeLODs][3] = { { 9, 6, 4 }, { 7, 4, 3 }, { 5, 3, 0 }, { 3, 0, 0 } };
		const int32 NCards[DmTreeLODs] = { bShrub ? 85 : 230, bShrub ? 40 : 110, bShrub ? 12 : 30, bShrub ? 6 : 13 };
		const float SizeMul[DmTreeLODs] = { 1.f, 1.4f, 2.1f, 2.9f };
		const float Inner[DmTreeLODs] = { 0.25f, 0.35f, 0.35f, 0.2f };
		const int32 Segs[DmTreeLODs] = { 2, 2, 1, 1 };
		const EDmLeafTile Tile = (bShrub && R.F() < 0.5f) ? EDmLeafTile::Leaflets : EDmLeafTile::Broadleaf;
		const float Tone = R.R(0.88f, 1.05f);
		const FColor Tint = bShrub ? DmNature::Col(0.80f * Tone, 0.82f * Tone, 0.68f * Tone) : DmNature::Col(0.96f * Tone, 1.f * Tone, 0.88f * Tone);
		const float CardBase = bShrub ? R.R(1.0f, 1.35f) : (H / 14.f) * 2.3f;
		const uint32 CardSeed = R.Next();
		for (int32 L = 0; L < DmTreeLODs; ++L)
		{
			FDmNatureMesh& M = Out.LOD[L];
			EmitBranches(M, Br, Sides[L], BarkC, Sway, AOt);
			DmNature::FRand Rc(CardSeed + L * 7919u);
			if (L >= 2)
			{
				// far LODs: camera-facing leaf masses on lobes at the branch tips (an irregular, lumpy crown with gaps,
				// never a ball), plus a few larger ones filling the core
				const float Rmax = FMath::Max(CR.X, CR.Y);
				const EDmLeafTile Ct0 = (bShrub && Tile == EDmLeafTile::Leaflets) ? EDmLeafTile::LeafletClump : EDmLeafTile::BroadClump;
				const int32 Step = L == 2 ? 1 : 2;
				int32 Made = 0;
				for (int32 Li = 0; Li < Lobes.Num(); Li += Step)
				{
					const FVector3f P = Lobes[Li] + Rc.Unit() * (0.08f * Rmax);
					const float Sz = Rmax * (L == 2 ? Rc.R(0.5f, 0.95f) : Rc.R(0.75f, 1.1f));
					const EDmLeafTile Ct = (Ct0 == EDmLeafTile::BroadClump && Rc.F() < 0.5f) ? EDmLeafTile::BroadClumpB : Ct0;
					BillboardCard(M, P, Sz, Sz * Rc.R(0.85f, 1.f), Ct, Tint, Radial, AOb, Sway);
					++Made;
				}
				const int32 Fill = L == 2 ? 4 : 2;
				for (int32 I = 0; I < Fill; ++I)
				{
					FVector3f U = Rc.Unit(); U.Z = FMath::Abs(U.Z) * 0.6f; U.Normalize();
					const FVector3f P = CC + Mul(U, CR) * Rc.R(0.25f, 0.45f);
					const float Sz = Rmax * Rc.R(0.9f, 1.1f);
					BillboardCard(M, P, Sz, Sz * 0.9f, Ct0, Tint, Radial, AOb, Sway);
				}
				continue;
			}
			for (int32 I = 0; I < NCards[L]; ++I)
			{
				FVector3f U = Rc.Unit();
				if (U.Z < -0.5f) U.Z = -0.5f + (U.Z + 0.5f) * 0.3f;
				U.Normalize();
				const float Rr = FMath::Lerp(Inner[L], 1.f, FMath::Sqrt(Rc.F()));
				const FVector3f P = CC + Mul(U, CR) * Rr;
				const FVector3f Dt = (U + Rc.Unit() * 0.55f + Up * 0.25f).GetSafeNormal();
				const FVector3f Ds = FVector3f::CrossProduct(Dt, Rc.Unit()).GetSafeNormal();
				const float Len = CardBase * Rc.R(0.85f, 1.15f) * SizeMul[L];
				Card(M, P - Dt * (Len * 0.4f), Dt, Ds, Len, Len, Tile, Tint, Radial, 0.78f, AOb, Sway, 0.12f, Segs[L]);
			}
		}
	}

	// =========================================================== conifer
	void BuildConifer(DmNature::FRand& R, FDmTreeBuild& Out)
	{
		const float H = R.R(14.f, 24.f);
		const float Tr = H * R.R(0.016f, 0.02f);
		const float Z0 = H * R.R(0.1f, 0.22f);
		const float Rb = H * R.R(0.19f, 0.25f);
		Out.Height = H; Out.CrownRadius = Rb;
		const FColor BarkC = DmNature::Col(0.46f, 0.36f, 0.29f);
		auto CrownR = [=](float Z) { return Rb * FMath::Pow(FMath::Clamp((H - Z) / (H - Z0), 0.f, 1.f), 0.95f) + 0.25f; };
		auto Sway = [H](const FVector3f& P) { const float Z = FMath::Clamp(P.Z / H, 0.f, 1.2f); return 0.016f * H * Z * Z + 0.03f * FVector2f(P.X, P.Y).Size(); };
		auto AOc = [=](const FVector3f& P)
		{
			const float Rel = FVector2f(P.X, P.Y).Size() / FMath::Max(CrownR(P.Z), 0.3f);
			return FMath::Clamp(0.25f + 0.75f * FMath::Pow(FMath::Clamp(Rel, 0.f, 1.f), 0.8f), 0.f, 1.f) * (0.7f + 0.3f * FMath::Clamp(P.Z / H, 0.f, 1.f));
		};
		auto AOt = [=](const FVector3f& P) { return FMath::Clamp(0.45f + 0.55f * P.Z / H, 0.f, 1.f); };
		auto Radial = [=](const FVector3f& P) { return (FVector3f(P.X, P.Y, 0.f).GetSafeNormal() + Up * 0.45f).GetSafeNormal(); };

		// trunk (straight with a slight lean)
		const FVector3f Lean(R.R(-0.015f, 0.015f) * H, R.R(-0.015f, 0.015f) * H, 0.f);
		FBranch T; T.Level = 0;
		for (int32 K = 0; K <= 8; ++K)
		{
			const float F = K / 8.f;
			T.P.Add(FVector3f(Lean.X * F, Lean.Y * F, F * H * 0.9f));
			T.R.Add(FMath::Lerp(Tr, 0.02f, FMath::Pow(F, 0.9f)) * (K == 0 ? 1.35f : 1.f));
		}
		// whorls
		struct FWh { FVector3f P, D; float L; };
		TArray<FWh> Whorls;
		const float Dz = (H / 18.f) * R.R(0.36f, 0.44f);
		float Phase = R.R(0.f, 2.f * PI);
		for (float Z = Z0; Z < H * 0.94f; Z += Dz * R.R(0.85f, 1.15f))
		{
			const int32 NB = R.I(5, 7);
			const float Rz = CrownR(Z);
			for (int32 B = 0; B < NB; ++B)
			{
				const float Az = Phase + 2.f * PI * (B + R.R(-0.2f, 0.2f)) / NB;
				const FVector3f D(FMath::Cos(Az), FMath::Sin(Az), R.R(-0.05f, 0.12f));
				Whorls.Add({ FVector3f(Lean.X * Z / H, Lean.Y * Z / H, Z), D.GetSafeNormal(), Rz * R.R(0.85f, 1.08f) });
			}
			Phase += 2.39996f; // golden angle
		}
		const int32 TrunkSides[DmTreeLODs] = { 8, 6, 4, 3 };
		const int32 Every[DmTreeLODs] = { 1, 2, 0, 0 };
		const float SizeMul[DmTreeLODs] = { 1.f, 1.35f, 1.8f, 1.f };
		const float Tone = R.R(0.9f, 1.05f);
		const FColor Tint = DmNature::Col(0.92f * Tone, 1.f * Tone, 0.95f * Tone);
		const uint32 CardSeed = R.Next();
		for (int32 L = 0; L < DmTreeLODs; ++L)
		{
			FDmNatureMesh& M = Out.LOD[L];
			FBranch TT = T;
			Tube(M, TT.P, TT.R, TrunkSides[L], BarkC, Sway, AOt);
			DmNature::FRand Rc(CardSeed + L * 131u);
			if (Every[L] == 0)
			{
				// far LODs: stacked camera-facing branch tiers (the NeedleClump tile is a drooping skirt), 2-3 per tier
				// set around the trunk and turned by the golden angle: a layered, ragged spruce spire from every side
				const int32 NT = L == 2 ? 7 : 5;
				const int32 Per = L == 2 ? 3 : 2;
				float Az0 = Rc.R(0.f, 2.f * PI);
				for (int32 K = 0; K < NT; ++K)
				{
					const float F = (K + 0.5f) / NT;
					const float Z = Z0 + (H * 0.97f - Z0) * FMath::Pow(F, 0.95f);
					const float Rz = FMath::Max(CrownR(Z), 0.7f);
					const float TierH = (H * 0.97f - Z0) / NT * 1.75f;
					for (int32 C = 0; C < Per; ++C)
					{
						const float Az = Az0 + 2.f * PI * C / Per;
						const FVector3f Off(FMath::Cos(Az) * Rz * 0.3f, FMath::Sin(Az) * Rz * 0.3f, Rc.R(-0.1f, 0.1f) * TierH);
						const FVector3f P = FVector3f(Lean.X * Z / H, Lean.Y * Z / H, Z) + Off;
						BillboardCard(M, P, Rz * 2.f * Rc.R(0.92f, 1.08f), TierH, EDmLeafTile::NeedleClump, Tint, Radial, AOc, Sway);
					}
					Az0 += 2.39996f;
				}
				continue;
			}
			for (int32 W = 0; W < Whorls.Num(); W += Every[L])
			{
				const FWh& Wh = Whorls[W];
				// branch with droop
				const FVector3f Tip = Wh.P + Wh.D * Wh.L - Up * (Wh.L * Rc.R(0.18f, 0.32f));
				if (L == 0 && Wh.P.Z < H * 0.75f)
				{
					TArray<FVector3f> Bp = { Wh.P, FMath::Lerp(Wh.P, Tip, 0.5f) + Up * (Wh.L * 0.06f), Tip };
					TArray<float> Brr = { FMath::Max(0.025f, Tr * 0.25f * (1.f - Wh.P.Z / H)), 0.02f, 0.01f };
					Tube(M, Bp, Brr, 3, BarkC, Sway, AOt);
				}
				const int32 NSpray = (L == 0) ? (Wh.L > 2.f ? 4 : 2) : (L == 1 ? 2 : 1);
				for (int32 S = 0; S < NSpray; ++S)
				{
					const float F0 = (NSpray == 1) ? 0.15f : FMath::Lerp(0.1f, 0.55f, float(S) / (NSpray - 1));
					const FVector3f B0 = FMath::Lerp(Wh.P, Tip, F0);
					FVector3f Dt = (Tip - B0).GetSafeNormal();
					FVector3f Ds = FVector3f::CrossProduct(Up, Dt).GetSafeNormal();
					Ds = RotateAbout(Ds, Dt, FMath::DegreesToRadians(Rc.R(-25.f, 25.f)));
					const float Len = FMath::Max(0.9f, (Tip - B0).Size() * 1.25f) * (S == NSpray - 1 ? 1.f : 0.9f) * SizeMul[L];
					Card(M, B0 - Dt * 0.15f, Dt, Ds, Len, Len * 1.05f, EDmLeafTile::Needles, Tint, Radial, 0.7f, AOc, Sway, 0.08f, L == 0 ? 2 : 1, 0.04f);
				}
			}
			// leader
			for (int32 K = 0; K < 2; ++K)
			{
				const FVector3f Ds = RotateAbout(FVector3f(1, 0, 0), Up, PI * 0.5f * K + 0.4f);
				Card(M, FVector3f(Lean.X, Lean.Y, H * 0.8f), Up, Ds, H * 0.2f, H * 0.12f, EDmLeafTile::Needles, Tint, Radial, 0.7f, AOc, Sway, 0.f, 1, 0.03f);
			}
		}
	}

	// =========================================================== palm
	void BuildPalm(DmNature::FRand& R, FDmTreeBuild& Out)
	{
		const float H = R.R(8.f, 13.f);
		const float LeanAz = R.R(0.f, 2.f * PI), Lean = R.R(0.6f, 2.6f);
		const FVector3f LD(FMath::Cos(LeanAz), FMath::Sin(LeanAz), 0.f);
		const FColor BarkC = DmNature::Col(0.60f, 0.55f, 0.47f);
		auto TrunkAt = [=](float F) { return LD * (Lean * FMath::Pow(F, 1.8f)) + Up * (F * H); };
		const FVector3f Top = TrunkAt(1.f);
		Out.Height = H + 1.f; Out.CrownRadius = 4.5f;
		auto Sway = [H](const FVector3f& P) { const float Z = FMath::Clamp(P.Z / H, 0.f, 1.3f); return 0.03f * H * Z * Z; };
		auto AOt = [H](const FVector3f& P) { return FMath::Clamp(0.6f + 0.4f * P.Z / H, 0.f, 1.f) * (P.Z > H * 0.97f ? 0.6f : 1.f); };
		auto AOf = [Top](const FVector3f& P) { return FMath::Clamp(0.45f + 0.55f * FMath::Min((P - Top).Size() / 3.f, 1.f), 0.f, 1.f); };
		auto Radial = [Top](const FVector3f& P) { return ((P - Top).GetSafeNormal() + Up * 0.6f).GetSafeNormal(); };

		struct FFrond { float Az, El, L, Droop; bool bDead; };
		TArray<FFrond> Fr;
		const int32 NF = R.I(13, 17);
		for (int32 I = 0; I < NF; ++I)
			Fr.Add({ 2.f * PI * (I + R.R(-0.25f, 0.25f)) / NF, FMath::DegreesToRadians(R.R(-15.f, 65.f)), R.R(4.f, 5.6f), FMath::DegreesToRadians(R.R(10.f, 17.f)), false });
		const int32 ND = R.I(2, 4);
		for (int32 I = 0; I < ND; ++I)
			Fr.Add({ R.R(0.f, 2.f * PI), FMath::DegreesToRadians(R.R(-75.f, -55.f)), R.R(2.8f, 3.6f), FMath::DegreesToRadians(3.f), true });

		const int32 TrunkSides[DmTreeLODs] = { 10, 7, 5, 3 };
		const int32 FrondSegs[DmTreeLODs] = { 7, 4, 3, 2 };
		const int32 FrondEvery[DmTreeLODs] = { 1, 1, 2, 2 };
		const float Tone = R.R(0.9f, 1.05f);
		for (int32 L = 0; L < DmTreeLODs; ++L)
		{
			FDmNatureMesh& M = Out.LOD[L];
			TArray<FVector3f> Tp; TArray<float> Trr;
			const int32 NT = L == 0 ? 14 : L == 1 ? 8 : 4;
			for (int32 K = 0; K <= NT; ++K)
			{
				const float F = float(K) / NT;
				Tp.Add(TrunkAt(F));
				Trr.Add(FMath::Lerp(0.22f, 0.16f, F) * (K == 0 ? 1.5f : (F < 0.1f ? 1.15f : 1.f)));
			}
			Tube(M, Tp, Trr, TrunkSides[L], BarkC, Sway, AOt);
			if (L <= 1)
			{
				// leaf-base bulb and a few coconuts under the crown
				TArray<FVector3f> Bp = { Top - Up * 0.6f, Top, Top + Up * 0.35f };
				TArray<float> Brr = { 0.2f, 0.27f, 0.12f };
				Tube(M, Bp, Brr, TrunkSides[L], DmNature::Col(0.42f, 0.40f, 0.26f), Sway, AOt);
				DmNature::FRand Rk(77 + L);
				const int32 NC = L == 0 ? R.I(3, 7) : 3;
				for (int32 C = 0; C < NC; ++C)
				{
					const float A = Rk.R(0.f, 2.f * PI);
					const FVector3f Cp = Top + FVector3f(FMath::Cos(A) * 0.28f, FMath::Sin(A) * 0.28f, Rk.R(-0.45f, -0.2f));
					M.Cylinder(0, Cp - Up * 0.11f, 0.11f, 0.12f, 0.2f, 6, DmNature::Col(0.40f, 0.42f, 0.20f), true, FVector2f(Sway(Cp), 0.f));
				}
			}
			for (int32 I = 0; I < Fr.Num(); I += FrondEvery[L])
			{
				const FFrond& F = Fr[I];
				const FVector3f Az(FMath::Cos(F.Az), FMath::Sin(F.Az), 0.f);
				const FVector3f Side = FVector3f::CrossProduct(Up, Az).GetSafeNormal();
				const int32 Sg = FrondSegs[L];
				const float W = (F.bDead ? 1.1f : 1.8f) * (L == 2 ? 1.3f : L == 3 ? 1.6f : 1.f);
				const FColor Tint = F.bDead ? DmNature::Col(0.85f, 0.62f, 0.38f) : DmNature::Col(0.88f * Tone, 0.86f * Tone, 0.72f * Tone);
				const float Fold = FMath::DegreesToRadians(F.bDead ? 5.f : 28.f);
				for (int32 Copy = 0; Copy < 2; ++Copy)
				{
					const int32 B0 = M.V.Num();
					FVector3f P = Top + Up * 0.2f;
					float El = F.El;
					for (int32 K = 0; K <= Sg; ++K)
					{
						const float T = float(K) / Sg;
						const FVector3f Dir = Az * FMath::Cos(El) + Up * FMath::Sin(El);
						const FVector3f FUp = FVector3f::CrossProduct(Dir, Side).GetSafeNormal() * (FVector3f::DotProduct(FVector3f::CrossProduct(Dir, Side), Up) < 0.f ? -1.f : 1.f);
						const float Hw = 0.5f * W * FMath::Pow(FMath::Max(0.f, FMath::Sin(PI * FMath::Clamp(0.08f + 0.92f * T, 0.f, 1.f))), 0.35f);
						for (int32 J = 0; J < 3; ++J)
						{
							const float S = (J - 1) * 1.f;
							const FVector3f Off = Side * (S * Hw * FMath::Cos(Fold)) + FUp * (FMath::Abs(S) * Hw * FMath::Sin(Fold));
							const FVector3f Q = P + Off;
							const FVector3f N = (Radial(Q) * 0.55f + FUp * 0.45f).GetSafeNormal();
							FColor C = Tint; C.A = uint8(FMath::Clamp(AOf(Q), 0.f, 1.f) * 255.f);
							// card width covers the whole tile even where the blade is narrower; UV across stays proportional
							M.Add(Q, N, DmLeafUV(EDmLeafTile::Frond, S * 0.5f + 0.5f, T), C, FVector2f(Sway(Q), 0.12f * T));
						}
						P += Dir * (F.L / Sg);
						El -= F.Droop * (0.6f + T);
					}
					for (int32 K = 0; K < Sg; ++K)
						for (int32 J = 0; J < 2; ++J)
						{
							const uint32 a = B0 + 3 * K + J, b = a + 1, c = b + 3, d = a + 3;
							const FVector3f Fn = FVector3f::CrossProduct(M.V[b].P - M.V[a].P, M.V[d].P - M.V[a].P);
							const FVector3f Want = (FVector3f::DotProduct(Fn, Up) >= 0.f ? Fn : -Fn) * (Copy == 0 ? 1.f : -1.f);
							M.Quad(1, a, b, c, d, Want);
						}
				}
			}
		}
	}

	// =========================================================== acacia (umbrella thorn)
	void BuildAcacia(DmNature::FRand& R, FDmTreeBuild& Out)
	{
		const float H = R.R(6.f, 9.5f);
		const float Tr = R.R(0.16f, 0.24f);
		const float Ht = H * R.R(0.22f, 0.38f);
		const FColor BarkC = DmNature::Col(0.38f, 0.33f, 0.28f);
		auto Sway = [H](const FVector3f& P) { const float Z = FMath::Clamp(P.Z / H, 0.f, 1.2f); return 0.02f * H * Z * Z + 0.02f * FVector2f(P.X, P.Y).Size(); };
		struct FClump { FVector3f C; float R, Th; };
		TArray<FClump> Cl;
		TArray<FBranch> Br;
		FBranch T = Grow(R, FVector3f::ZeroVector, (Up + R.Unit() * 0.12f).GetSafeNormal(), Ht, 3, Tr * 1.2f, Tr * 0.85f, Up, 0.f, 0.06f, 0);
		T.R[0] = Tr * 1.5f;
		Br.Add(T);
		const FVector3f Fork = T.P.Last();
		const int32 NL = R.I(2, 4);
		const float Az0 = R.R(0.f, 2.f * PI);
		float MaxR = 0.f;
		for (int32 I = 0; I < NL; ++I)
		{
			const float Az = Az0 + 2.f * PI * (I + R.R(-0.15f, 0.15f)) / NL;
			const float El = FMath::DegreesToRadians(R.R(48.f, 66.f));
			const FVector3f D0(FMath::Cos(El) * FMath::Cos(Az), FMath::Cos(El) * FMath::Sin(Az), FMath::Sin(El));
			const float L = (H * 0.86f - Fork.Z) / FMath::Sin(El) * R.R(0.9f, 1.05f);
			FBranch L1 = Grow(R, Fork, D0, L, 4, Tr * 0.7f, Tr * 0.25f, FVector3f(D0.X, D0.Y, 0.f), 0.05f, 0.08f, 1);
			Br.Add(L1);
			const FVector3f End = L1.P.Last();
			const int32 NS = R.I(3, 4);
			for (int32 S = 0; S < NS; ++S)
			{
				const FVector3f D2 = RotateAbout(FVector3f(D0.X, D0.Y, 0.f).GetSafeNormal(), Up, FMath::DegreesToRadians(R.R(-50.f, 50.f))) + Up * R.R(0.1f, 0.35f);
				const float L2 = H * R.R(0.12f, 0.22f);
				FBranch L2b = Grow(R, End, D2.GetSafeNormal(), L2, 3, Tr * 0.3f, 0.05f, -Up, 0.06f, 0.1f, 2);
				Br.Add(L2b);
				const FVector3f Cc = L2b.P.Last() + Up * R.R(0.2f, 0.6f);
				Cl.Add({ Cc, R.R(2.3f, 3.4f), R.R(0.9f, 1.4f) });
				MaxR = FMath::Max(MaxR, FVector2f(Cc.X, Cc.Y).Size() + 2.5f);
			}
		}
		Out.Height = H; Out.CrownRadius = MaxR;
		auto Nearest = [&Cl](const FVector3f& P) -> const FClump&
		{
			int32 Best = 0; float Bd = 1e9f;
			for (int32 I = 0; I < Cl.Num(); ++I) { const float D = (P - Cl[I].C).SizeSquared(); if (D < Bd) { Bd = D; Best = I; } }
			return Cl[Best];
		};
		auto Radial = [&Nearest](const FVector3f& P) { const FClump& C = Nearest(P); return (Div(P - C.C, FVector3f(C.R * C.R, C.R * C.R, C.Th * C.Th)).GetSafeNormal() * 0.5f + Up * 0.7f).GetSafeNormal(); };
		auto AOa = [&Nearest](const FVector3f& P) { const FClump& C = Nearest(P); return FMath::Clamp(0.45f + 0.55f * ((P.Z - C.C.Z) / C.Th * 0.5f + 0.5f), 0.2f, 1.f); };
		auto AOt = [H](const FVector3f& P) { return FMath::Clamp(0.6f + 0.4f * P.Z / H, 0.f, 1.f); };

		const int32 Sides[DmTreeLODs][3] = { { 8, 5, 4 }, { 6, 4, 3 }, { 4, 3, 0 }, { 3, 0, 0 } };
		const int32 NPer[DmTreeLODs] = { 42, 20, 8, 3 };
		const float SizeMul[DmTreeLODs] = { 1.f, 1.4f, 2.0f, 2.6f };
		const float Tone = R.R(0.88f, 1.04f);
		const FColor Tint = DmNature::Col(0.95f * Tone, 0.96f * Tone, 0.82f * Tone);
		const uint32 CardSeed = R.Next();
		for (int32 L = 0; L < DmTreeLODs; ++L)
		{
			FDmNatureMesh& M = Out.LOD[L];
			EmitBranches(M, Br, Sides[L], BarkC, Sway, AOt);
			DmNature::FRand Rc(CardSeed + L * 313u);
			if (L >= 2)
			{
				// far LODs: a few near-horizontal leaflet-mass cards per clump: the flat umbrella stays solid
				for (const FClump& C : Cl)
					for (int32 I = 0; I < (L == 2 ? 4 : 2); ++I)
					{
						const float A = Rc.R(0.f, 2.f * PI), Rr = C.R * 0.35f * FMath::Sqrt(Rc.F());
						const FVector3f P = C.C + FVector3f(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, Rc.R(-0.3f, 0.3f) * C.Th);
						const FVector3f Nn = (Up + Rc.Unit() * 0.3f).GetSafeNormal();
						const float Len = C.R * (L == 2 ? 1.55f : 2.0f) * Rc.R(0.9f, 1.1f);
						ClumpCard(M, P, Nn, Rc.R(0.f, 2.f * PI), Len, Len, EDmLeafTile::LeafletClump, Tint, Radial, 0.75f, AOa, Sway);
					}
				continue;
			}
			for (const FClump& C : Cl)
				for (int32 I = 0; I < NPer[L]; ++I)
				{
					const float A = Rc.R(0.f, 2.f * PI), Rr = C.R * FMath::Sqrt(Rc.F()) * (L >= 2 ? 0.6f : 0.85f);
					const FVector3f P = C.C + FVector3f(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, Rc.R(-0.5f, 0.5f) * C.Th);
					FVector3f Dt = FVector3f(FMath::Cos(A), FMath::Sin(A), 0.f);
					Dt = RotateAbout(Dt, Up, Rc.R(-0.8f, 0.8f));
					const FVector3f Ds0 = FVector3f::CrossProduct(Up, Dt).GetSafeNormal();
					const FVector3f Ds = RotateAbout(Ds0, Dt, Rc.R(-0.35f, 0.35f));
					const FVector3f Dt2 = RotateAbout(Dt, Ds, Rc.R(-0.55f, 0.55f));
					const float Len = Rc.R(1.5f, 2.1f) * SizeMul[L];
					Card(M, P - Dt2 * (Len * 0.5f), Dt2, Ds, Len, Len, EDmLeafTile::Leaflets, Tint, Radial, 0.75f, AOa, Sway, 0.05f, 1, 0.05f);
				}
		}
	}

	// =========================================================== dead tree (bare skeleton or broken snag)
	void BuildDead(DmNature::FRand& R, FDmTreeBuild& Out, bool bSnag)
	{
		const FColor BarkC = bSnag ? DmNature::Col(0.56f, 0.54f, 0.50f) : DmNature::Col(0.47f, 0.44f, 0.40f);
		TArray<FBranch> Br;
		float H;
		if (bSnag)
		{
			// a standing dead conifer: straight trunk with a jagged broken top and short branch stubs in a spiral
			H = R.R(8.f, 16.f);
			const float Tr = H * R.R(0.018f, 0.024f);
			const float Top = H * R.R(0.72f, 0.92f);
			const FVector3f Lean(R.R(-0.03f, 0.03f) * H, R.R(-0.03f, 0.03f) * H, 0.f);
			FBranch T; T.Level = 0;
			for (int32 K = 0; K <= 7; ++K)
			{
				const float F = K / 7.f;
				T.P.Add(FVector3f(Lean.X * F * F + R.R(-0.03f, 0.03f), Lean.Y * F * F + R.R(-0.03f, 0.03f), F * Top));
				T.R.Add(FMath::Lerp(Tr, Tr * 0.38f, F) * (K == 0 ? 1.4f : 1.f));
			}
			Br.Add(T);
			// splinter at the break
			Br.Add(Grow(R, T.P.Last(), (Up + R.Unit() * 0.25f).GetSafeNormal(), H * R.R(0.05f, 0.1f), 2, Tr * 0.3f, 0.01f, Up, 0.f, 0.1f, 2));
			float Az = R.R(0.f, 2.f * PI);
			for (float Z = H * R.R(0.25f, 0.35f); Z < Top * 0.95f; Z += H * R.R(0.045f, 0.08f))
			{
				Az += 2.39996f;
				float Rr = 0.f; FVector3f Dd;
				const FVector3f Bp = PointOn(T, Z / Top, &Rr, &Dd);
				const FVector3f D(FMath::Cos(Az), FMath::Sin(Az), R.R(-0.35f, 0.05f));
				const float L = H * R.R(0.04f, 0.14f) * (1.f - 0.5f * Z / H);
				Br.Add(Grow(R, Bp, D.GetSafeNormal(), L, 2, FMath::Max(0.02f, Rr * 0.28f), 0.012f, -Up, 0.05f, 0.08f, 2));
			}
		}
		else
		{
			// a bare broadleaf skeleton: gnarled limbs, forks and twigs, no leaves
			H = R.R(7.f, 13.f);
			const float Tr = H * R.R(0.024f, 0.032f);
			const float ForkZ = H * R.R(0.32f, 0.45f);
			FBranch T = Grow(R, FVector3f::ZeroVector, (Up + R.Unit() * 0.08f).GetSafeNormal(), ForkZ, 5, Tr, Tr * 0.75f, Up, 0.02f, 0.07f, 0);
			T.R[0] = Tr * 1.5f; T.R[1] = Tr * 1.12f;
			Br.Add(T);
			const FVector3f Fork = T.P.Last();
			const int32 NL = R.I(3, 5);
			const float Az0 = R.R(0.f, 2.f * PI);
			for (int32 I = 0; I < NL; ++I)
			{
				const float Az = Az0 + 2.f * PI * (I + R.R(-0.25f, 0.25f)) / NL;
				const float El = FMath::DegreesToRadians(R.R(22.f, 55.f));
				const FVector3f D0(FMath::Sin(El) * FMath::Cos(Az), FMath::Sin(El) * FMath::Sin(Az), FMath::Cos(El));
				const float L = H * R.R(0.3f, 0.48f);
				FBranch L1 = Grow(R, Fork, D0, L, 5, Tr * 0.6f, Tr * 0.12f, Up, 0.04f, 0.22f, 1);
				Br.Add(L1);
				const int32 NS = R.I(2, 4);
				for (int32 S = 0; S < NS; ++S)
				{
					float Rr = 0.f; FVector3f Dd;
					const FVector3f Bp = PointOn(L1, R.R(0.3f, 0.92f), &Rr, &Dd);
					const FVector3f Out2 = RotateAbout(FVector3f(Dd.X, Dd.Y, 0.f).GetSafeNormal(), Up, FMath::DegreesToRadians(R.R(30.f, 75.f)) * (R.F() < 0.5f ? -1.f : 1.f));
					const FVector3f D2 = (Dd * 0.3f + Out2 + Up * R.R(-0.1f, 0.6f)).GetSafeNormal();
					FBranch L2 = Grow(R, Bp, D2, L * R.R(0.3f, 0.55f), 3, Rr * 0.6f, 0.018f, Up, 0.06f, 0.25f, 2);
					Br.Add(L2);
					// twigs
					for (int32 Tw = 0; Tw < 2; ++Tw)
					{
						float Rt = 0.f; FVector3f Dt;
						const FVector3f Tp = PointOn(L2, R.R(0.4f, 0.95f), &Rt, &Dt);
						Br.Add(Grow(R, Tp, (Dt + R.Unit() * 0.8f + Up * 0.3f).GetSafeNormal(), L * R.R(0.12f, 0.22f), 2, FMath::Max(0.012f, Rt * 0.6f), 0.008f, Up, 0.05f, 0.3f, 2));
					}
				}
			}
			// a broken stub on the trunk
			float Rs = 0.f; FVector3f Ds;
			const FVector3f Sp = PointOn(T, R.R(0.4f, 0.8f), &Rs, &Ds);
			Br.Add(Grow(R, Sp, FVector3f(R.R(-1.f, 1.f), R.R(-1.f, 1.f), 0.3f).GetSafeNormal(), H * 0.08f, 1, Rs * 0.45f, Rs * 0.3f, Up, 0.f, 0.f, 1));
		}
		Out.Height = H;
		Out.CrownRadius = H * 0.35f;
		const float Hs = H;
		auto Sway = [Hs](const FVector3f& P) { const float Z = FMath::Clamp(P.Z / Hs, 0.f, 1.2f); return 0.008f * Hs * Z * Z + 0.015f * FVector2f(P.X, P.Y).Size(); };
		auto AOt = [Hs](const FVector3f& P) { return FMath::Clamp(0.55f + 0.45f * FMath::Min(P.Z / (Hs * 0.4f), 1.f), 0.f, 1.f); };
		// dead trees are all silhouette: keep the limbs (and some twigs) to the far LODs
		const int32 Sides[DmTreeLODs][3] = { { 8, 5, 4 }, { 6, 4, 3 }, { 4, 3, 3 }, { 3, 3, 0 } };
		for (int32 L = 0; L < DmTreeLODs; ++L)
			EmitBranches(Out.LOD[L], Br, Sides[L], BarkC, Sway, AOt);
	}
}

const TCHAR* DmNature::TreeName(EDmTreeKind Kind)
{
	switch (Kind)
	{
	case EDmTreeKind::Conifer: return TEXT("Conifer");
	case EDmTreeKind::Broadleaf: return TEXT("Broadleaf");
	case EDmTreeKind::Palm: return TEXT("Palm");
	case EDmTreeKind::Acacia: return TEXT("Acacia");
	case EDmTreeKind::Shrub: return TEXT("Shrub");
	case EDmTreeKind::Dead: return TEXT("Dead");
	default: return TEXT("?");
	}
}

void DmNature::BuildTree(EDmTreeKind Kind, uint32 Seed, FDmTreeBuild& Out)
{
	DmNature::FRand R(Seed * 2654435761u + uint32(Kind) * 97u + 11u);
	switch (Kind)
	{
	case EDmTreeKind::Conifer: BuildConifer(R, Out); break;
	case EDmTreeKind::Broadleaf: BuildBroadleaf(R, Out, false); break;
	case EDmTreeKind::Palm: BuildPalm(R, Out); break;
	case EDmTreeKind::Acacia: BuildAcacia(R, Out); break;
	case EDmTreeKind::Shrub: BuildBroadleaf(R, Out, true); break;
	case EDmTreeKind::Dead: BuildDead(R, Out, (Seed & 1u) != 0); break;
	default: break;
	}
}
