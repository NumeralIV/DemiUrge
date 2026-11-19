// DEMIURGE — Game/Nature/DmNatureMesh.cpp : see the header.

#include "Game/Nature/DmNatureMesh.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "StaticMeshResources.h"

void FDmNatureMesh::Tri(int32 S, uint32 A, uint32 B, uint32 C, const FVector3f& WantN)
{
	const FVector3f Fn = FVector3f::CrossProduct(V[C].P - V[A].P, V[B].P - V[A].P);
	TArray<uint32>& I = Sec[S];
	if (FVector3f::DotProduct(Fn, WantN) >= 0.f) I.Append({ A, B, C });
	else I.Append({ A, C, B });
}

int32 FDmNatureMesh::NumTris() const
{
	int32 N = 0;
	for (const TArray<uint32>& S : Sec) N += S.Num() / 3;
	return N;
}

void FDmNatureMesh::Cylinder(int32 S, const FVector3f& Base, float R0, float R1, float H, int32 Sides, const FColor& Col, bool bCap, const FVector2f& UV1)
{
	const int32 First = V.Num();
	const float Circ = 2.f * PI * FMath::Max(R0, R1);
	const float Slope = (R0 - R1) / FMath::Max(H, 1e-3f);
	for (int32 I = 0; I <= Sides; ++I)
	{
		const float A = 2.f * PI * I / Sides;
		const FVector3f Dir(FMath::Cos(A), FMath::Sin(A), 0.f);
		const FVector3f N = (Dir + FVector3f(0, 0, Slope)).GetSafeNormal();
		Add(Base + Dir * R0, N, FVector2f(Circ * I / Sides, 0.f), Col, UV1);
		Add(Base + Dir * R1 + FVector3f(0, 0, H), N, FVector2f(Circ * I / Sides, H), Col, UV1);
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
		const int32 C0 = Add(Top, FVector3f(0, 0, 1), FVector2f(0, 0), Col, UV1);
		const int32 Ring = V.Num();
		for (int32 I = 0; I <= Sides; ++I)
		{
			const float A = 2.f * PI * I / Sides;
			Add(Top + FVector3f(FMath::Cos(A), FMath::Sin(A), 0.f) * R1, FVector3f(0, 0, 1), FVector2f(FMath::Cos(A) * R1, FMath::Sin(A) * R1), Col, UV1);
		}
		for (int32 I = 0; I < Sides; ++I) Tri(S, C0, Ring + I, Ring + I + 1, FVector3f(0, 0, 1));
	}
}

void FDmNatureMesh::Ellipsoid(int32 S, const FVector3f& Center, const FVector3f& Rad, const FQuat4f& Rot, int32 Seg, int32 Rings, const FColor& Col, const FVector2f& UV1)
{
	const int32 First = V.Num();
	for (int32 R = 0; R <= Rings; ++R)
	{
		const float Th = PI * R / Rings;
		for (int32 K = 0; K <= Seg; ++K)
		{
			const float Ph = 2.f * PI * K / Seg;
			const FVector3f U(FMath::Sin(Th) * FMath::Cos(Ph), FMath::Sin(Th) * FMath::Sin(Ph), FMath::Cos(Th));
			const FVector3f P(U.X * Rad.X, U.Y * Rad.Y, U.Z * Rad.Z);
			const FVector3f N = FVector3f(U.X / Rad.X, U.Y / Rad.Y, U.Z / Rad.Z).GetSafeNormal();
			Add(Center + Rot.RotateVector(P), Rot.RotateVector(N), FVector2f(float(K) / Seg, float(R) / Rings), Col, UV1);
		}
	}
	for (int32 R = 0; R < Rings; ++R)
		for (int32 K = 0; K < Seg; ++K)
		{
			const uint32 a = First + R * (Seg + 1) + K, b = a + 1, d = a + (Seg + 1), c = d + 1;
			const FVector3f Out = (V[a].P + V[c].P) * 0.5f - Center;
			if (R == 0) Tri(S, a, c, d, Out);
			else if (R == Rings - 1) Tri(S, a, b, c, Out);
			else Quad(S, a, b, c, d, Out);
		}
}

void FDmNatureMesh::Limb(int32 S, const FVector3f& A, const FVector3f& B, float RA, float RB, int32 Sides, const FColor& Col, const FVector2f& UV1)
{
	FVector3f T = B - A;
	const float L = T.Size();
	if (L < 1e-4f) return;
	T /= L;
	FVector3f Nr = FMath::Abs(T.Z) < 0.9f ? FVector3f::CrossProduct(T, FVector3f(0, 0, 1)) : FVector3f::CrossProduct(T, FVector3f(1, 0, 0));
	Nr.Normalize();
	const FVector3f Bn = FVector3f::CrossProduct(T, Nr);
	const int32 First = V.Num();
	for (int32 E = 0; E < 2; ++E)
		for (int32 K = 0; K <= Sides; ++K)
		{
			const float Ang = 2.f * PI * K / Sides;
			const FVector3f D = Nr * FMath::Cos(Ang) + Bn * FMath::Sin(Ang);
			Add((E ? B : A) + D * (E ? RB : RA), D, FVector2f(float(K) / Sides, E ? L : 0.f), Col, UV1);
		}
	for (int32 K = 0; K < Sides; ++K)
	{
		const uint32 a = First + K, b = a + 1, d = a + Sides + 1, c = d + 1;
		Quad(S, a, b, c, d, V[a].N + V[b].N);
	}
}

void FDmNatureMesh::Append(const FDmNatureMesh& O, const FTransform3f& T)
{
	const uint32 Base = V.Num();
	V.Reserve(V.Num() + O.V.Num());
	for (const FDmNatureVert& Vx : O.V)
	{
		FDmNatureVert W = Vx;
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

UStaticMesh* DmNature::MakeStaticMesh(UObject* Outer, const FString& Name, const TArray<const FDmNatureMesh*>& LODs,
	const TArray<UMaterialInterface*>& Mats, const TArray<float>& ScreenSizes, float BoundsExtend)
{
	// Render data is filled directly (Engine module only: no MeshDescription dependency). One vertex buffer and
	// one index buffer per LOD, one FStaticMeshSection per non-empty material section (the material index is
	// explicit, so an empty section simply has no draw).
	constexpr float CmPerM = 100.f;
	const int32 NumLODs = FMath::Clamp(LODs.Num(), 1, int32(MAX_STATIC_MESH_LODS));
	UStaticMesh* SM = NewObject<UStaticMesh>(Outer, MakeUniqueObjectName(Outer, UStaticMesh::StaticClass(), FName(*Name)), RF_Transient);
	for (int32 S = 0; S < Mats.Num(); ++S)
	{
		const FName Slot(*FString::Printf(TEXT("S%d"), S));
		SM->GetStaticMaterials().Add(FStaticMaterial(Mats[S], Slot, Slot));
	}
	TUniquePtr<FStaticMeshRenderData> RD = MakeUnique<FStaticMeshRenderData>();
	RD->AllocateLODResources(NumLODs);
	FBox Box(ForceInit);
	for (int32 L = 0; L < NumLODs; ++L)
	{
		const FDmNatureMesh& M = *LODs[L];
		FStaticMeshLODResources& LR = RD->LODResources[L];
		// tangents from UV0, accumulated over triangles
		TArray<FVector3f> TanU; TanU.SetNumZeroed(M.V.Num());
		TArray<FVector3f> TanV; TanV.SetNumZeroed(M.V.Num());
		for (const TArray<uint32>& I : M.Sec)
			for (int32 T = 0; T + 2 < I.Num(); T += 3)
			{
				const FDmNatureVert& A = M.V[I[T]]; const FDmNatureVert& B = M.V[I[T + 1]]; const FDmNatureVert& C = M.V[I[T + 2]];
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
			const FDmNatureVert& Vx = M.V[I];
			FStaticMeshBuildVertex& O = BV[I];
			O.Position = Vx.P.ContainsNaN() ? FVector3f::ZeroVector : Vx.P * CmPerM;   // a NaN vertex would poison every instance's bounds
			const FVector3f N = Vx.N.GetSafeNormal(1e-8f, FVector3f(0, 0, 1));
			FVector3f T = TanU[I] - N * FVector3f::DotProduct(N, TanU[I]);
			if (T.SizeSquared() < 1e-12f) T = FVector3f::CrossProduct(N, FMath::Abs(N.Z) < 0.9f ? FVector3f(0, 0, 1) : FVector3f(1, 0, 0));
			T = T.GetSafeNormal();
			FVector3f Bn = FVector3f::CrossProduct(N, T);
			if (FVector3f::DotProduct(Bn, TanV[I]) < 0.f) Bn = -Bn;
			O.TangentX = T; O.TangentY = Bn; O.TangentZ = N;
			O.UVs[0] = Vx.UV0; O.UVs[1] = Vx.UV1; O.UVs[2] = Vx.UV2; O.UVs[3] = Vx.UV3; O.UVs[4] = Vx.UV4;
			O.Color = Vx.C;
			if (L == 0) Box += FVector(O.Position);
		}
		LR.VertexBuffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(true);
		LR.VertexBuffers.PositionVertexBuffer.Init(BV);
		LR.VertexBuffers.StaticMeshVertexBuffer.Init(BV, 5, FStaticMeshVertexBufferFlags());
		LR.VertexBuffers.ColorVertexBuffer.Init(BV);
		LR.bHasColorVertexData = true;
		TArray<uint32> Idx;
		for (int32 S = 0; S < M.Sec.Num(); ++S)
		{
			// every section exists in every LOD (an empty one keeps one degenerate triangle): editor builds look the
			// material up by SECTION index, so a missing bark section would draw the leaves with the bark material
			const TArray<uint32>& I = M.Sec[S];
			FStaticMeshSection& Sec = LR.Sections.AddDefaulted_GetRef();
			Sec.MaterialIndex = FMath::Min(S, FMath::Max(0, Mats.Num() - 1));
			Sec.FirstIndex = Idx.Num();
			const bool bEmpty = I.Num() < 3;
			Sec.NumTriangles = bEmpty ? 1 : I.Num() / 3;
			uint32 Lo = bEmpty ? 0 : MAX_uint32, Hi = 0;
			for (uint32 X : I) { Lo = FMath::Min(Lo, X); Hi = FMath::Max(Hi, X); }
			Sec.MinVertexIndex = Lo; Sec.MaxVertexIndex = Hi;
#if WITH_EDITORONLY_DATA
			SM->GetSectionInfoMap().Set(L, LR.Sections.Num() - 1, FMeshSectionInfo(Sec.MaterialIndex));
#endif
			Sec.bEnableCollision = false;
			Sec.bCastShadow = true;
			Sec.bVisibleInRayTracing = false;
			Sec.bAffectDistanceFieldLighting = false;
			if (bEmpty) Idx.Append({ 0u, 0u, 0u });
			else Idx.Append(I.GetData(), (I.Num() / 3) * 3);
		}
		if (Idx.Num() == 0)
		{
			// keep the LOD valid: one degenerate triangle in section 0
			FStaticMeshSection& Sec = LR.Sections.AddDefaulted_GetRef();
			Sec.NumTriangles = 1;
			Idx.Append({ 0u, 0u, 0u });
		}
		LR.IndexBuffer.SetIndices(Idx, EIndexBufferStride::AutoDetect);
		RD->ScreenSize[L].Default = L < ScreenSizes.Num() ? ScreenSizes[L] : FMath::Pow(0.3f, float(L));
	}
	if (!Box.IsValid) Box = FBox(FVector(-1), FVector(1));
	RD->Bounds = FBoxSphereBounds(Box);
	RD->NumInlinedLODs = uint8(NumLODs);
	SM->SetRenderData(MoveTemp(RD));
	if (BoundsExtend > 0.f)
	{
		const FVector Ext(BoundsExtend * CmPerM);
		SM->SetPositiveBoundsExtension(Ext);
		SM->SetNegativeBoundsExtension(Ext);
	}
	SM->CalculateExtendedBounds();
	SM->InitResources();
	return SM;
}

// ---------------------------------------------------------------- noise (twin of NOISE_HLSL in Scripts/nature.py)

namespace
{
	inline float NH01(int32 X, int32 Y, int32 Z)
	{
		uint32 H = uint32(X) * 0x8da6b343u ^ uint32(Y) * 0xd8163841u ^ uint32(Z) * 0xcb1ab31fu;
		H ^= H >> 13; H *= 0x5bd1e995u; H ^= H >> 15;
		return float(H & 0xFFFFFFu) / 16777216.0f;
	}
}

float DmNature::VNoise3(float X, float Y, float Z)
{
	const float Fx = FMath::FloorToFloat(X), Fy = FMath::FloorToFloat(Y), Fz = FMath::FloorToFloat(Z);
	const int32 Ix = int32(Fx), Iy = int32(Fy), Iz = int32(Fz);
	const float Tx = X - Fx, Ty = Y - Fy, Tz = Z - Fz;
	const float Ux = Tx * Tx * (3.f - 2.f * Tx), Uy = Ty * Ty * (3.f - 2.f * Ty), Uz = Tz * Tz * (3.f - 2.f * Tz);
	const float A = FMath::Lerp(NH01(Ix, Iy, Iz), NH01(Ix + 1, Iy, Iz), Ux);
	const float B = FMath::Lerp(NH01(Ix, Iy + 1, Iz), NH01(Ix + 1, Iy + 1, Iz), Ux);
	const float C = FMath::Lerp(NH01(Ix, Iy, Iz + 1), NH01(Ix + 1, Iy, Iz + 1), Ux);
	const float D = FMath::Lerp(NH01(Ix, Iy + 1, Iz + 1), NH01(Ix + 1, Iy + 1, Iz + 1), Ux);
	return FMath::Lerp(FMath::Lerp(A, B, Uy), FMath::Lerp(C, D, Uy), Uz) * 2.f - 1.f;
}

float DmNature::Fbm3(float X, float Y, float Z, int32 Octaves)
{
	Octaves = FMath::Clamp(Octaves, 1, 8);   // the lab's NaN bug: never trust an octave count
	float Sum = 0.f, Amp = 0.5f, Norm = 0.f;
	for (int32 O = 0; O < Octaves; ++O)
	{
		Sum += Amp * VNoise3(X, Y, Z);
		Norm += Amp;
		Amp *= 0.5f;
		X = X * 2.03f + 17.1f; Y = Y * 2.03f + 31.7f; Z = Z * 2.03f + 47.3f;
	}
	return Sum / Norm;
}
