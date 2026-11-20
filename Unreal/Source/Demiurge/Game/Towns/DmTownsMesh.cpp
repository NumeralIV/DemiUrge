// DEMIURGE — Game/Towns/DmTownsMesh.cpp : see the header.

#include "Game/Towns/DmTownsMesh.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "StaticMeshResources.h"

namespace
{
	const FVector3f Z3(0, 0, 1);
}

void FDmTownMesh::Tri(int32 S, uint32 A, uint32 B, uint32 C, const FVector3f& WantN)
{
	const FVector3f Fn = FVector3f::CrossProduct(V[C].P - V[A].P, V[B].P - V[A].P);
	TArray<uint32>& I = Sec[S];
	if (FVector3f::DotProduct(Fn, WantN) >= 0.f) I.Append({ A, B, C });
	else I.Append({ A, C, B });
}

int32 FDmTownMesh::NumTris() const
{
	int32 N = 0;
	for (const TArray<uint32>& S : Sec) N += S.Num() / 3;
	return N;
}

void FDmTownMesh::Q4uv(int32 S, const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& D, const FColor& Col,
	const FVector2f& Ua, const FVector2f& Ub, const FVector2f& Uc, const FVector2f& Ud, const FVector2f& U1, const FVector3f& WantN)
{
	FVector3f N = (FVector3f::CrossProduct(B - A, D - A) + FVector3f::CrossProduct(D - C, B - C)).GetSafeNormal();
	if (N.IsNearlyZero()) N = WantN.GetSafeNormal();
	if (FVector3f::DotProduct(N, WantN) < 0.f) N = -N;
	const int32 a = Add(A, N, Ua, Col, U1), b = Add(B, N, Ub, Col, U1), c = Add(C, N, Uc, Col, U1), d = Add(D, N, Ud, Col, U1);
	Quad(S, a, b, c, d, N);
}

void FDmTownMesh::Q4(int32 S, const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& D, const FColor& Col, const FVector2f& U1, const FVector3f& WantN)
{
	FVector3f N = (FVector3f::CrossProduct(B - A, D - A) + FVector3f::CrossProduct(D - C, B - C)).GetSafeNormal();
	if (N.IsNearlyZero()) N = WantN.GetSafeNormal();
	if (FVector3f::DotProduct(N, WantN) < 0.f) N = -N;
	FVector3f Ux = (B - A).GetSafeNormal();
	if (Ux.IsNearlyZero() || FMath::Abs(FVector3f::DotProduct(Ux, N)) > 0.9f) Ux = FMath::Abs(N.Z) < 0.9f ? FVector3f::CrossProduct(Z3, N).GetSafeNormal() : FVector3f(1, 0, 0);
	const FVector3f Vy = FVector3f::CrossProduct(N, Ux).GetSafeNormal();
	auto UV = [&](const FVector3f& P) { return FVector2f(FVector3f::DotProduct(P, Ux), FVector3f::DotProduct(P, Vy)); };
	Q4uv(S, A, B, C, D, Col, UV(A), UV(B), UV(C), UV(D), U1, N);
}

void FDmTownMesh::T3(int32 S, const FVector3f& A, const FVector3f& B, const FVector3f& C, const FColor& Col, const FVector2f& U1, const FVector3f& WantN)
{
	FVector3f N = FVector3f::CrossProduct(B - A, C - A).GetSafeNormal();
	if (N.IsNearlyZero()) N = WantN.GetSafeNormal();
	if (FVector3f::DotProduct(N, WantN) < 0.f) N = -N;
	FVector3f Ux = (B - A).GetSafeNormal();
	if (Ux.IsNearlyZero()) Ux = FVector3f(1, 0, 0);
	const FVector3f Vy = FVector3f::CrossProduct(N, Ux).GetSafeNormal();
	auto UV = [&](const FVector3f& P) { return FVector2f(FVector3f::DotProduct(P, Ux), FVector3f::DotProduct(P, Vy)); };
	const int32 a = Add(A, N, UV(A), Col, U1), b = Add(B, N, UV(B), Col, U1), c = Add(C, N, UV(C), Col, U1);
	Tri(S, a, b, c, N);
}

void FDmTownMesh::Box(int32 S, const FVector3f& Ctr, const FVector3f& H, float Yaw, const FColor& Col, const FVector2f& U1, bool bBottom)
{
	const float Cs = FMath::Cos(Yaw), Sn = FMath::Sin(Yaw);
	const FVector3f X(Cs * H.X, Sn * H.X, 0.f), Y(-Sn * H.Y, Cs * H.Y, 0.f), Z(0.f, 0.f, H.Z);
	auto P = [&](float a, float b, float c) { return Ctr + X * a + Y * b + Z * c; };
	const FVector3f Xn = X.GetSafeNormal(), Yn = Y.GetSafeNormal();
	Q4(S, P(-1, -1, -1), P(1, -1, -1), P(1, -1, 1), P(-1, -1, 1), Col, U1, -Yn);
	Q4(S, P(1, -1, -1), P(1, 1, -1), P(1, 1, 1), P(1, -1, 1), Col, U1, Xn);
	Q4(S, P(1, 1, -1), P(-1, 1, -1), P(-1, 1, 1), P(1, 1, 1), Col, U1, Yn);
	Q4(S, P(-1, 1, -1), P(-1, -1, -1), P(-1, -1, 1), P(-1, 1, 1), Col, U1, -Xn);
	Q4(S, P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1), Col, U1, Z3);
	if (bBottom) Q4(S, P(-1, 1, -1), P(1, 1, -1), P(1, -1, -1), P(-1, -1, -1), Col, U1, -Z3);
}

void FDmTownMesh::Beam(int32 S, const FVector3f& A, const FVector3f& B, float W, float H, const FColor& Col, const FVector2f& U1)
{
	FVector3f D = B - A;
	const float L = D.Size();
	if (L < 1e-4f) return;
	D /= L;
	FVector3f Side = FVector3f::CrossProduct(Z3, D);
	if (Side.SizeSquared() < 1e-6f) Side = FVector3f(1, 0, 0);
	Side = Side.GetSafeNormal() * (W * 0.5f);
	const FVector3f Up = FVector3f::CrossProduct(D, Side).GetSafeNormal() * H;
	const FVector3f a0 = A - Side, a1 = A + Side, b0 = B - Side, b1 = B + Side;
	const FVector3f Sn = Side.GetSafeNormal(), Un = Up.GetSafeNormal();
	Q4(S, a0, b0, b0 + Up, a0 + Up, Col, U1, -Sn);
	Q4(S, b1, a1, a1 + Up, b1 + Up, Col, U1, Sn);
	Q4(S, a0 + Up, b0 + Up, b1 + Up, a1 + Up, Col, U1, Un);
	Q4(S, a1, b1, b0, a0, Col, U1, -Un);
	Q4(S, a1, a0, a0 + Up, a1 + Up, Col, U1, -D);
	Q4(S, b0, b1, b1 + Up, b0 + Up, Col, U1, D);
}

void FDmTownMesh::Cylinder(int32 S, const FVector3f& Base, float R0, float R1, float H, int32 Sides, const FColor& Col, bool bCap, const FVector2f& U1)
{
	Sides = FMath::Max(3, Sides);
	const int32 First = V.Num();
	const float Circ = 2.f * PI * FMath::Max(R0, R1);
	const float Slope = (R0 - R1) / FMath::Max(H, 1e-3f);
	for (int32 I = 0; I <= Sides; ++I)
	{
		const float A = 2.f * PI * I / Sides;
		const FVector3f Dir(FMath::Cos(A), FMath::Sin(A), 0.f);
		const FVector3f N = (Dir + FVector3f(0, 0, Slope)).GetSafeNormal();
		Add(Base + Dir * R0, N, FVector2f(Circ * I / Sides, 0.f), Col, U1);
		Add(Base + Dir * R1 + FVector3f(0, 0, H), N, FVector2f(Circ * I / Sides, H), Col, U1);
	}
	for (int32 I = 0; I < Sides; ++I)
	{
		const uint32 a = First + 2 * I, b = a + 2, c = a + 3, d = a + 1;
		const FVector3f Out = (V[a].P + V[b].P) * 0.5f - Base;
		Quad(S, a, b, c, d, FVector3f(Out.X, Out.Y, 0.f));
	}
	if (bCap && R1 > 0.f)
	{
		const FVector3f Top = Base + FVector3f(0, 0, H);
		const int32 C0 = Add(Top, Z3, FVector2f(0, 0), Col, U1);
		const int32 Ring = V.Num();
		for (int32 I = 0; I <= Sides; ++I)
		{
			const float A = 2.f * PI * I / Sides;
			Add(Top + FVector3f(FMath::Cos(A), FMath::Sin(A), 0.f) * R1, Z3, FVector2f(FMath::Cos(A) * R1, FMath::Sin(A) * R1), Col, U1);
		}
		for (int32 I = 0; I < Sides; ++I) Tri(S, C0, Ring + I, Ring + I + 1, Z3);
	}
}

void FDmTownMesh::Cone(int32 S, const FVector3f& Base, float R, float H, int32 Sides, const FColor& Col, const FVector2f& U1)
{
	Sides = FMath::Max(3, Sides);
	const float SlopeLen = FMath::Sqrt(R * R + H * H);
	auto Nrm = [&](float A) { return FVector3f(FMath::Cos(A) * H, FMath::Sin(A) * H, R).GetSafeNormal(); };
	for (int32 I = 0; I < Sides; ++I)
	{
		const float A0 = 2.f * PI * I / Sides, A1 = 2.f * PI * (I + 1) / Sides, Am = 0.5f * (A0 + A1);
		const FVector3f P0 = Base + FVector3f(FMath::Cos(A0), FMath::Sin(A0), 0) * R, P1 = Base + FVector3f(FMath::Cos(A1), FMath::Sin(A1), 0) * R;
		const int32 a = Add(P0, Nrm(A0), FVector2f(R * A0, 0), Col, U1);
		const int32 b = Add(P1, Nrm(A1), FVector2f(R * A1, 0), Col, U1);
		const int32 c = Add(Base + FVector3f(0, 0, H), Nrm(Am), FVector2f(R * Am, SlopeLen), Col, U1);
		Tri(S, a, b, c, Nrm(Am));
	}
}

void FDmTownMesh::Pyramid(int32 S, const FVector3f& Base, float HX, float HY, float Yaw, float H, const FColor& Col, const FVector2f& U1)
{
	const float Cs = FMath::Cos(Yaw), Sn = FMath::Sin(Yaw);
	const FVector3f X(Cs * HX, Sn * HX, 0.f), Y(-Sn * HY, Cs * HY, 0.f);
	const FVector3f C[4] = { Base - X - Y, Base + X - Y, Base + X + Y, Base - X + Y };
	const FVector3f Apex = Base + FVector3f(0, 0, H);
	for (int32 I = 0; I < 4; ++I)
	{
		const FVector3f Mid = (C[I] + C[(I + 1) % 4]) * 0.5f - Base;
		T3(S, C[I], C[(I + 1) % 4], Apex, Col, U1, Mid + FVector3f(0, 0, 0.3f * Mid.Size()));
	}
}

void FDmTownMesh::Dome(int32 S, const FVector3f& Base, float R, float HScale, int32 Sides, int32 Rings, const FColor& Col, const FVector2f& U1)
{
	Sides = FMath::Max(4, Sides); Rings = FMath::Max(2, Rings);
	const int32 First = V.Num();
	for (int32 J = 0; J <= Rings; ++J)
	{
		const float Ph = 0.5f * PI * J / Rings;
		const float Rr = R * FMath::Cos(Ph), Zz = R * HScale * FMath::Sin(Ph);
		for (int32 I = 0; I <= Sides; ++I)
		{
			const float A = 2.f * PI * I / Sides;
			const FVector3f D(FMath::Cos(A) * FMath::Cos(Ph), FMath::Sin(A) * FMath::Cos(Ph), FMath::Sin(Ph) / FMath::Max(HScale, 0.05f));
			Add(Base + FVector3f(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, Zz), D.GetSafeNormal(), FVector2f(R * A, R * Ph), Col, U1);
		}
	}
	for (int32 J = 0; J < Rings; ++J)
		for (int32 I = 0; I < Sides; ++I)
		{
			const uint32 a = First + J * (Sides + 1) + I, b = a + 1, d = a + Sides + 1, c = d + 1;
			Quad(S, a, b, c, d, V[a].N + V[c].N);
		}
}

void FDmTownMesh::GlowDisc(const FVector3f& C, float R, const FColor& Col, bool bAlways, int32 Seg)
{
	FColor Cc = Col; Cc.A = bAlways ? 255 : 0;
	FColor Mid = DmTowns::Mul(Col, 0.42f); Mid.A = Cc.A;
	const FColor Rim(0, 0, 0, Cc.A);
	const int32 C0 = Add(C, Z3, FVector2f(0, 0), Cc);
	const int32 B0 = V.Num();
	for (int32 I = 0; I <= Seg; ++I)
	{
		const float A = 2.f * PI * I / Seg;
		const FVector3f D(FMath::Cos(A), FMath::Sin(A), 0.f);
		Add(C + D * (R * 0.4f), Z3, FVector2f(0, 0), Mid);
		Add(C + D * R, Z3, FVector2f(0, 0), Rim);
	}
	for (int32 I = 0; I < Seg; ++I)
	{
		const int32 a = B0 + 2 * I, b = a + 2;
		Tri(DmTownSec::Glow, C0, a, b, Z3);
		Quad(DmTownSec::Glow, a, b, b + 1, a + 1, Z3);
	}
}

void FDmTownMesh::Window(const FVector3f& P, const FVector3f& U, const FVector3f& N, float W, float H, const FColor& LitCol, uint8 Rand)
{
	FColor C = LitCol; C.A = Rand;
	const FVector3f O = N * 0.04f, Hu = U * (W * 0.5f), Hz(0, 0, H * 0.5f);
	const FVector3f A = P - Hu - Hz + O, B = P + Hu - Hz + O, Cc = P + Hu + Hz + O, D = P - Hu + Hz + O;
	const int32 a = Add(A, N, FVector2f(0, 0), C), b = Add(B, N, FVector2f(1, 0), C), c = Add(Cc, N, FVector2f(1, 1), C), d = Add(D, N, FVector2f(0, 1), C);
	Quad(DmTownSec::Glass, a, b, c, d, N);
}

void FDmTownMesh::Append(const FDmTownMesh& O, const FTransform3f& T)
{
	const uint32 Base = V.Num();
	V.Reserve(V.Num() + O.V.Num());
	for (const FDmTownVert& Vx : O.V)
	{
		FDmTownVert W = Vx;
		W.P = T.TransformPosition(Vx.P);
		W.N = T.TransformVectorNoScale(Vx.N).GetSafeNormal();
		V.Add(W);
	}
	if (Sec.Num() < O.Sec.Num()) Sec.SetNum(O.Sec.Num());
	for (int32 S = 0; S < O.Sec.Num(); ++S)
	{
		TArray<uint32>& I = Sec[S];
		I.Reserve(I.Num() + O.Sec[S].Num());
		for (uint32 Idx : O.Sec[S]) I.Add(Base + Idx);
	}
}

void FDmTownMesh::SetHeightFraction(float Z0, float Z1)
{
	if (Sec.Num() == 0) return;
	// UV1.y = height above the floor in metres (materials darken the foot of the walls); Z1 unused
	(void)Z1;
	TBitArray<> InBody(false, V.Num());
	for (uint32 I : Sec[DmTownSec::Body]) InBody[int32(I)] = true;
	for (int32 I = 0; I < V.Num(); ++I)
		if (InBody[I]) V[I].UV1.Y = V[I].P.Z - Z0;
}

FBox3f FDmTownMesh::Bounds() const
{
	FBox3f B(ForceInit);
	for (const FDmTownVert& Vx : V) B += Vx.P;
	return B;
}

// ------------------------------------------------------------------ noise

float DmTowns::Noise2(float X, float Y, uint32 Seed)
{
	const float Fx0 = FMath::FloorToFloat(X), Fy0 = FMath::FloorToFloat(Y);
	const int32 Xi = int32(Fx0), Yi = int32(Fy0);
	const float Fx = X - Fx0, Fy = Y - Fy0;
	const float Ux = Fx * Fx * (3.f - 2.f * Fx), Uy = Fy * Fy * (3.f - 2.f * Fy);
	const float A = Hash01(uint32(Xi), uint32(Yi), Seed), B = Hash01(uint32(Xi + 1), uint32(Yi), Seed);
	const float C = Hash01(uint32(Xi), uint32(Yi + 1), Seed), D = Hash01(uint32(Xi + 1), uint32(Yi + 1), Seed);
	return FMath::Lerp(FMath::Lerp(A, B, Ux), FMath::Lerp(C, D, Ux), Uy) * 2.f - 1.f;
}

float DmTowns::Fbm2(float X, float Y, int32 Octaves, uint32 Seed)
{
	Octaves = FMath::Clamp(Octaves, 1, 8);
	float Sum = 0.f, Amp = 0.5f, Norm = 0.f;
	for (int32 O = 0; O < Octaves; ++O)
	{
		const float Rx = X * 0.8f - Y * 0.6f, Ry = X * 0.6f + Y * 0.8f;
		Sum += Amp * Noise2(Rx, Ry, Seed + uint32(O) * 101u);
		Norm += Amp;
		Amp *= 0.5f;
		X = Rx * 2.03f; Y = Ry * 2.03f;
	}
	return Sum / Norm;
}

// ------------------------------------------------------------------ static mesh

// (towns-perf) the CPU half of a transient static mesh (tangents, vertex / index buffers): pure data, safe on a worker
// thread, so a settlement's ground overlay is prepared by its planning task and the game thread only wraps it
FDmTownsMeshData::FDmTownsMeshData() = default;
FDmTownsMeshData::~FDmTownsMeshData() = default;

TSharedPtr<FDmTownsMeshData, ESPMode::ThreadSafe> DmTowns::BuildMeshData(const TArray<const FDmTownMesh*>& LODs, int32 NumMats,
	const TArray<float>& ScreenSizes, bool bShadows, bool bColors)
{
	constexpr float CmPerM = 100.f;
	const int32 NumLODs = FMath::Clamp(LODs.Num(), 1, int32(MAX_STATIC_MESH_LODS));
	TSharedPtr<FDmTownsMeshData, ESPMode::ThreadSafe> Out = MakeShared<FDmTownsMeshData, ESPMode::ThreadSafe>();
	TUniquePtr<FStaticMeshRenderData> RD = MakeUnique<FStaticMeshRenderData>();
	RD->AllocateLODResources(NumLODs);
	FBox Box(ForceInit);
	for (int32 L = 0; L < NumLODs; ++L)
	{
		const FDmTownMesh& M = *LODs[L];
		FStaticMeshLODResources& LR = RD->LODResources[L];
		TArray<FVector3f> TanU; TanU.SetNumZeroed(M.V.Num());
		TArray<FVector3f> TanV; TanV.SetNumZeroed(M.V.Num());
		for (const TArray<uint32>& I : M.Sec)
			for (int32 T = 0; T + 2 < I.Num(); T += 3)
			{
				const FDmTownVert& A = M.V[I[T]]; const FDmTownVert& B = M.V[I[T + 1]]; const FDmTownVert& C = M.V[I[T + 2]];
				const FVector3f E1 = B.P - A.P, E2 = C.P - A.P;
				const FVector2f D1 = B.UV0 - A.UV0, D2 = C.UV0 - A.UV0;
				const float Det = D1.X * D2.Y - D2.X * D1.Y;
				if (FMath::Abs(Det) < 1e-10f) continue;
				const float R = 1.f / Det;
				const FVector3f Tu = (E1 * D2.Y - E2 * D1.Y) * R;
				const FVector3f Tv = (E2 * D1.X - E1 * D2.X) * R;
				for (int32 K = 0; K < 3; ++K) { TanU[I[T + K]] += Tu; TanV[I[T + K]] += Tv; }
			}
		TArray<FStaticMeshBuildVertex> BV;
		BV.SetNumZeroed(FMath::Max(1, M.V.Num()));
		for (int32 I = 0; I < M.V.Num(); ++I)
		{
			const FDmTownVert& Vx = M.V[I];
			FStaticMeshBuildVertex& O = BV[I];
			O.Position = Vx.P.ContainsNaN() ? FVector3f::ZeroVector : Vx.P * CmPerM;
			const FVector3f N = Vx.N.GetSafeNormal(1e-8f, FVector3f(0, 0, 1));
			FVector3f T = TanU[I] - N * FVector3f::DotProduct(N, TanU[I]);
			if (T.SizeSquared() < 1e-12f) T = FVector3f::CrossProduct(N, FMath::Abs(N.Z) < 0.9f ? FVector3f(0, 0, 1) : FVector3f(1, 0, 0));
			T = T.GetSafeNormal();
			FVector3f Bn = FVector3f::CrossProduct(N, T);
			if (FVector3f::DotProduct(Bn, TanV[I]) < 0.f) Bn = -Bn;
			O.TangentX = T; O.TangentY = Bn; O.TangentZ = N;
			O.UVs[0] = Vx.UV0; O.UVs[1] = Vx.UV1;
			O.Color = Vx.C;
			if (L == 0) Box += FVector(O.Position);
		}
		LR.VertexBuffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(true);
		LR.VertexBuffers.PositionVertexBuffer.Init(BV);
		LR.VertexBuffers.StaticMeshVertexBuffer.Init(BV, 2, FStaticMeshVertexBufferFlags());
		if (bColors) { LR.VertexBuffers.ColorVertexBuffer.Init(BV); LR.bHasColorVertexData = true; }
		TArray<uint32> Idx;
		TArray<int32>& SecMat = Out->SectionMats.AddDefaulted_GetRef();
		for (int32 S = 0; S < M.Sec.Num(); ++S)
		{
			// every section exists in every LOD (an empty one keeps one degenerate triangle) so that the
			// section -> material mapping never shifts between LODs
			const TArray<uint32>& I = M.Sec[S];
			FStaticMeshSection& Sec = LR.Sections.AddDefaulted_GetRef();
			Sec.MaterialIndex = FMath::Min(S, FMath::Max(0, NumMats - 1));
			Sec.FirstIndex = Idx.Num();
			const bool bEmpty = I.Num() < 3;
			Sec.NumTriangles = bEmpty ? 1 : I.Num() / 3;
			uint32 Lo = bEmpty ? 0 : MAX_uint32, Hi = 0;
			for (uint32 X : I) { Lo = FMath::Min(Lo, X); Hi = FMath::Max(Hi, X); }
			Sec.MinVertexIndex = Lo; Sec.MaxVertexIndex = Hi;
			SecMat.Add(Sec.MaterialIndex);
			Sec.bEnableCollision = false;
			Sec.bCastShadow = bShadows && S != DmTownSec::Glow;
			Sec.bVisibleInRayTracing = false;
			Sec.bAffectDistanceFieldLighting = false;
			if (bEmpty) Idx.Append({ 0u, 0u, 0u });
			else Idx.Append(I.GetData(), (I.Num() / 3) * 3);
		}
		if (Idx.Num() == 0)
		{
			FStaticMeshSection& Sec = LR.Sections.AddDefaulted_GetRef();
			Sec.NumTriangles = 1;
			Idx.Append({ 0u, 0u, 0u });
		}
		LR.IndexBuffer.SetIndices(Idx, EIndexBufferStride::AutoDetect);
		RD->ScreenSize[L].Default = L < ScreenSizes.Num() ? ScreenSizes[L] : FMath::Pow(0.3f, float(L));
	}
	if (!Box.IsValid) Box = FBox(FVector(-100), FVector(100));
	RD->Bounds = FBoxSphereBounds(Box);
	RD->NumInlinedLODs = uint8(NumLODs);
	Out->RD = MoveTemp(RD);
	return Out;
}

UStaticMesh* DmTowns::MakeStaticMesh(UObject* Outer, const FString& Name, FDmTownsMeshData& Data, const TArray<UMaterialInterface*>& Mats)
{
	if (!Data.RD.IsValid()) return nullptr;
	UStaticMesh* SM = NewObject<UStaticMesh>(Outer, MakeUniqueObjectName(Outer, UStaticMesh::StaticClass(), FName(*Name)), RF_Transient);
	for (int32 S = 0; S < Mats.Num(); ++S)
	{
		const FName Slot(*FString::Printf(TEXT("S%d"), S));
#if WITH_EDITORONLY_DATA
		SM->GetStaticMaterials().Add(FStaticMaterial(Mats[S], Slot, Slot));
#else
		SM->GetStaticMaterials().Add(FStaticMaterial(Mats[S], Slot));   // (integration) the game target has no imported slot name
#endif
	}
#if WITH_EDITORONLY_DATA
	for (int32 L = 0; L < Data.SectionMats.Num(); ++L)
		for (int32 S = 0; S < Data.SectionMats[L].Num(); ++S) SM->GetSectionInfoMap().Set(L, S, FMeshSectionInfo(Data.SectionMats[L][S]));
#endif
	SM->SetRenderData(MoveTemp(Data.RD));
	SM->CalculateExtendedBounds();
	SM->InitResources();
	return SM;
}

UStaticMesh* DmTowns::MakeStaticMesh(UObject* Outer, const FString& Name, const TArray<const FDmTownMesh*>& LODs,
	const TArray<UMaterialInterface*>& Mats, const TArray<float>& ScreenSizes, bool bShadows, bool bColors)
{
	TSharedPtr<FDmTownsMeshData, ESPMode::ThreadSafe> D = BuildMeshData(LODs, Mats.Num(), ScreenSizes, bShadows, bColors);
	return MakeStaticMesh(Outer, Name, *D, Mats);
}
