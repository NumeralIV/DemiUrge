#include "LifeMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

UStaticMesh* LifeMakeStaticMeshImpl(UObject* Outer, const FString& Name, const TArray<const FLifeMesh*>& LODs,
	const TArray<UMaterialInterface*>& Mats, const TArray<float>& ScreenSizes, int32 NaniteMode);

void FLifeMesh::Tri(int32 S, uint32 A, uint32 B, uint32 C, const FVector3f& WantN)
{
	// Unreal's front face normal is cross(C - A, B - A).
	const FVector3f Fn = FVector3f::CrossProduct(V[C].P - V[A].P, V[B].P - V[A].P);
	TArray<uint32>& I = Sec[S];
	if (FVector3f::DotProduct(Fn, WantN) >= 0.f) I.Append({ A, B, C });
	else I.Append({ A, C, B });
}

int32 FLifeMesh::NumTris() const
{
	int32 N = 0;
	for (const TArray<uint32>& S : Sec) N += S.Num() / 3;
	return N;
}

void FLifeMesh::FlatQuad(int32 S, const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& D, const FColor& Col,
	const FVector2f& UA, const FVector2f& UB, const FVector2f& UC, const FVector2f& UD, const FVector2f& UV1)
{
	// Normal from the corner order A,B,C,D taken counter-clockwise when seen from the front (right-handed sense).
	FVector3f N = FVector3f::CrossProduct(B - A, D - A) + FVector3f::CrossProduct(D - C, B - C);
	N = N.GetSafeNormal();
	const int32 I0 = Add(A, N, UA, Col, UV1), I1 = Add(B, N, UB, Col, UV1), I2 = Add(C, N, UC, Col, UV1), I3 = Add(D, N, UD, Col, UV1);
	Quad(S, I0, I1, I2, I3, N);
}

void FLifeMesh::FlatQuadMetric(int32 S, const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& D, const FColor& Col, const FVector2f& UV1, float UOff, float VOff)
{
	const FVector3f Ux = (B - A).GetSafeNormal();
	FVector3f N = FVector3f::CrossProduct(B - A, D - A).GetSafeNormal();
	const FVector3f Vy = FVector3f::CrossProduct(N, Ux);
	auto UV = [&](const FVector3f& P) { const FVector3f Q = P - A; return FVector2f(UOff + FVector3f::DotProduct(Q, Ux), VOff + FVector3f::DotProduct(Q, Vy)); };
	FlatQuad(S, A, B, C, D, Col, UV(A), UV(B), UV(C), UV(D), UV1);
}

void FLifeMesh::FlatTri(int32 S, const FVector3f& A, const FVector3f& B, const FVector3f& C, const FColor& Col, const FVector2f& UV1)
{
	const FVector3f N = FVector3f::CrossProduct(B - A, C - A).GetSafeNormal();
	const FVector3f Ux = (B - A).GetSafeNormal();
	const FVector3f Vy = FVector3f::CrossProduct(N, Ux);
	auto UV = [&](const FVector3f& P) { const FVector3f Q = P - A; return FVector2f(FVector3f::DotProduct(Q, Ux), FVector3f::DotProduct(Q, Vy)); };
	const int32 I0 = Add(A, N, UV(A), Col, UV1), I1 = Add(B, N, UV(B), Col, UV1), I2 = Add(C, N, UV(C), Col, UV1);
	Tri(S, I0, I1, I2, N);
}

void FLifeMesh::Box(int32 S, const FVector3f& Ctr, const FVector3f& H, float Yaw, const FColor& Col, const FVector2f& UV1, bool bBottom)
{
	const float Cs = FMath::Cos(Yaw), Sn = FMath::Sin(Yaw);
	const FVector3f X(Cs * H.X, Sn * H.X, 0.f), Y(-Sn * H.Y, Cs * H.Y, 0.f), Z(0.f, 0.f, H.Z);
	auto P = [&](float a, float b, float c) { return Ctr + X * a + Y * b + Z * c; };
	// sides (counter-clockwise from outside)
	FlatQuadMetric(S, P(-1, -1, -1), P(1, -1, -1), P(1, -1, 1), P(-1, -1, 1), Col, UV1);
	FlatQuadMetric(S, P(1, -1, -1), P(1, 1, -1), P(1, 1, 1), P(1, -1, 1), Col, UV1);
	FlatQuadMetric(S, P(1, 1, -1), P(-1, 1, -1), P(-1, 1, 1), P(1, 1, 1), Col, UV1);
	FlatQuadMetric(S, P(-1, 1, -1), P(-1, -1, -1), P(-1, -1, 1), P(-1, 1, 1), Col, UV1);
	FlatQuadMetric(S, P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1), Col, UV1);
	if (bBottom) FlatQuadMetric(S, P(-1, 1, -1), P(1, 1, -1), P(1, -1, -1), P(-1, -1, -1), Col, UV1);
}

void FLifeMesh::Beam(int32 S, const FVector3f& A, const FVector3f& B, float W, float H, const FColor& Col, const FVector2f& UV1)
{
	FVector3f D = B - A;
	const float L = D.Size();
	if (L < 1e-4f) return;
	D /= L;
	FVector3f Side = FVector3f::CrossProduct(FVector3f(0, 0, 1), D);
	if (Side.SizeSquared() < 1e-6f) Side = FVector3f(1, 0, 0);
	Side = Side.GetSafeNormal() * (W * 0.5f);
	const FVector3f Up = FVector3f::CrossProduct(D, Side).GetSafeNormal() * H;
	const FVector3f a0 = A - Side, a1 = A + Side, b0 = B - Side, b1 = B + Side;
	FlatQuadMetric(S, a0, b0, b0 + Up, a0 + Up, Col, UV1);
	FlatQuadMetric(S, b1, a1, a1 + Up, b1 + Up, Col, UV1);
	FlatQuadMetric(S, a0 + Up, b0 + Up, b1 + Up, a1 + Up, Col, UV1);
	FlatQuadMetric(S, a1, b1, b0, a0, Col, UV1);
	FlatQuadMetric(S, a1, a0, a0 + Up, a1 + Up, Col, UV1);
	FlatQuadMetric(S, b0, b1, b1 + Up, b0 + Up, Col, UV1);
}

void FLifeMesh::Cylinder(int32 S, const FVector3f& Base, float R0, float R1, float H, int32 Sides, const FColor& Col, bool bCap, const FVector2f& UV1)
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
		const FVector3f Out = ((V[a].P + V[b].P) * 0.5f - Base);
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

void FLifeMesh::Cone(int32 S, const FVector3f& Base, float R, float H, int32 Sides, const FColor& Col, const FVector2f& UV1)
{
	const float SlopeLen = FMath::Sqrt(R * R + H * H);
	for (int32 I = 0; I < Sides; ++I)
	{
		const float A0 = 2.f * PI * I / Sides, A1 = 2.f * PI * (I + 1) / Sides, Am = 0.5f * (A0 + A1);
		auto Nrm = [&](float A) { return FVector3f(FMath::Cos(A) * H, FMath::Sin(A) * H, R).GetSafeNormal(); };
		const FVector3f P0 = Base + FVector3f(FMath::Cos(A0), FMath::Sin(A0), 0) * R, P1 = Base + FVector3f(FMath::Cos(A1), FMath::Sin(A1), 0) * R;
		const float U0 = R * A0, U1 = R * A1;
		const int32 a = Add(P0, Nrm(A0), FVector2f(U0, 0), Col, UV1);
		const int32 b = Add(P1, Nrm(A1), FVector2f(U1, 0), Col, UV1);
		const int32 c = Add(Base + FVector3f(0, 0, H), Nrm(Am), FVector2f(0.5f * (U0 + U1), SlopeLen), Col, UV1);
		Tri(S, a, b, c, Nrm(Am));
	}
}

void FLifeMesh::Pyramid(int32 S, const FVector3f& Base, float HX, float HY, float Yaw, float H, const FColor& Col, const FVector2f& UV1)
{
	const float Cs = FMath::Cos(Yaw), Sn = FMath::Sin(Yaw);
	const FVector3f X(Cs * HX, Sn * HX, 0.f), Y(-Sn * HY, Cs * HY, 0.f);
	const FVector3f C[4] = { Base - X - Y, Base + X - Y, Base + X + Y, Base - X + Y };
	const FVector3f Apex = Base + FVector3f(0, 0, H);
	for (int32 I = 0; I < 4; ++I) FlatTri(S, C[I], C[(I + 1) % 4], Apex, Col, UV1);
}

void FLifeMesh::Append(const FLifeMesh& O, const FTransform3f& T)
{
	const uint32 Base = V.Num();
	V.Reserve(V.Num() + O.V.Num());
	for (const FLifeVert& Vx : O.V)
	{
		FLifeVert W = Vx;
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

UStaticMesh* LifeMakeStaticMesh(UObject* Outer, const FString& Name, const TArray<const FLifeMesh*>& LODs,
	const TArray<UMaterialInterface*>& Mats, const TArray<float>& ScreenSizes, int32 NaniteMode)
{
#if !WITH_EDITOR
	NaniteMode = -1;
#endif
	TArray<const FLifeMesh*> UseLODs = LODs;
	if (NaniteMode >= 0) UseLODs.SetNum(1);
	return LifeMakeStaticMeshImpl(Outer, Name, UseLODs, Mats, ScreenSizes, NaniteMode);
}

UStaticMesh* LifeMakeStaticMeshImpl(UObject* Outer, const FString& Name, const TArray<const FLifeMesh*>& LODs,
	const TArray<UMaterialInterface*>& Mats, const TArray<float>& ScreenSizes, int32 NaniteMode)
{
	constexpr float CmPerM = 100.f;
	TArray<FMeshDescription> Descs;
	Descs.SetNum(LODs.Num());
	for (int32 L = 0; L < LODs.Num(); ++L)
	{
		const FLifeMesh& M = *LODs[L];
		FMeshDescription& MD = Descs[L];
		FStaticMeshAttributes Attr(MD);
		Attr.Register();
		Attr.GetVertexInstanceUVs().SetNumChannels(2);
		TVertexAttributesRef<FVector3f> Pos = Attr.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Nrm = Attr.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> Tan = Attr.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> Sign = Attr.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
		TVertexInstanceAttributesRef<FVector4f> Cols = Attr.GetVertexInstanceColors();
		TPolygonGroupAttributesRef<FName> Slots = Attr.GetPolygonGroupMaterialSlotNames();

		// Per-vertex tangents from UV0 (accumulated over triangles).
		TArray<FVector3f> TanU; TanU.SetNumZeroed(M.V.Num());
		TArray<FVector3f> TanV; TanV.SetNumZeroed(M.V.Num());
		for (const TArray<uint32>& I : M.Sec)
			for (int32 T = 0; T + 2 < I.Num(); T += 3)
			{
				const FLifeVert& A = M.V[I[T]]; const FLifeVert& B = M.V[I[T + 1]]; const FLifeVert& C = M.V[I[T + 2]];
				const FVector3f E1 = B.P - A.P, E2 = C.P - A.P;
				const FVector2f D1 = B.UV0 - A.UV0, D2 = C.UV0 - A.UV0;
				const float Det = D1.X * D2.Y - D2.X * D1.Y;
				if (FMath::Abs(Det) < 1e-10f) continue;
				const float R = 1.f / Det;
				const FVector3f Tu = (E1 * D2.Y - E2 * D1.Y) * R;
				const FVector3f Tv = (E2 * D1.X - E1 * D2.X) * R;
				for (int32 K = 0; K < 3; ++K) { TanU[I[T + K]] += Tu; TanV[I[T + K]] += Tv; }
			}

		MD.ReserveNewVertices(M.V.Num());
		MD.ReserveNewVertexInstances(M.V.Num());
		MD.ReserveNewTriangles(M.NumTris());
		TArray<FVertexInstanceID> VI;
		VI.SetNumUninitialized(M.V.Num());
		for (int32 I = 0; I < M.V.Num(); ++I)
		{
			const FLifeVert& Vx = M.V[I];
			const FVertexID Vid = MD.CreateVertex();
			Pos[Vid] = Vx.P * CmPerM;
			const FVertexInstanceID Iid = MD.CreateVertexInstance(Vid);
			VI[I] = Iid;
			const FVector3f N = Vx.N.GetSafeNormal(1e-8f, FVector3f(0, 0, 1));
			FVector3f T = TanU[I] - N * FVector3f::DotProduct(N, TanU[I]);
			if (T.SizeSquared() < 1e-12f) T = FVector3f::CrossProduct(N, FMath::Abs(N.Z) < 0.9f ? FVector3f(0, 0, 1) : FVector3f(1, 0, 0));
			T = T.GetSafeNormal();
			Nrm[Iid] = N;
			Tan[Iid] = T;
			Sign[Iid] = FVector3f::DotProduct(FVector3f::CrossProduct(N, T), TanV[I]) < 0.f ? -1.f : 1.f;
			UVs.Set(Iid, 0, Vx.UV0);
			UVs.Set(Iid, 1, Vx.UV1);
			const FLinearColor Lc = FLinearColor::FromSRGBColor(Vx.C);
			Cols[Iid] = FVector4f(Lc.R, Lc.G, Lc.B, Lc.A);
		}
		for (int32 S = 0; S < M.Sec.Num(); ++S)
		{
			const FPolygonGroupID G = MD.CreatePolygonGroup();
			Slots[G] = FName(*FString::Printf(TEXT("S%d"), S));
			const TArray<uint32>& I = M.Sec[S];
			for (int32 T = 0; T + 2 < I.Num(); T += 3)
				MD.CreateTriangle(G, { VI[I[T]], VI[I[T + 1]], VI[I[T + 2]] });
			// An empty section would be skipped by the fast build and shift the section->material mapping
			// (seen as leaf cards drawn with the bark material on LODs without branches): keep a degenerate triangle.
			if (I.Num() < 3 && VI.Num() > 0)
				MD.CreateTriangle(G, { VI[0], VI[0], VI[0] });
		}
	}

	UStaticMesh* SM = NewObject<UStaticMesh>(Outer, FName(*Name), RF_Transient);
	for (int32 S = 0; S < Mats.Num(); ++S)
	{
		const FName Slot(*FString::Printf(TEXT("S%d"), S));
		SM->GetStaticMaterials().Add(FStaticMaterial(Mats[S], Slot, Slot));
	}
	UStaticMesh::FBuildMeshDescriptionsParams P;
	P.bFastBuild = NaniteMode < 0;
	P.bCommitMeshDescription = NaniteMode >= 0;
	P.bMarkPackageDirty = false;
	P.bBuildSimpleCollision = false;
#if WITH_EDITOR
	if (NaniteMode >= 0)
	{
		FMeshNaniteSettings NS = SM->GetNaniteSettings();
		NS.bEnabled = true;
		NS.ShapePreservation = NaniteMode == 2 ? ENaniteShapePreservation::Voxelize : (NaniteMode == 1 ? ENaniteShapePreservation::PreserveArea : ENaniteShapePreservation::None);
		SM->SetNaniteSettings(NS);
	}
#endif
	TArray<const FMeshDescription*> Ptrs;
	for (const FMeshDescription& MD : Descs) Ptrs.Add(&MD);
	SM->BuildFromMeshDescriptions(Ptrs, P);
#if WITH_EDITOR
	if (NaniteMode >= 0) { FStaticMeshCompilingManager::Get().FinishCompilation({ SM }); return SM; }
#endif
	if (FStaticMeshRenderData* RD = SM->GetRenderData())
		for (int32 L = 0; L < LODs.Num() && L < ScreenSizes.Num(); ++L) RD->ScreenSize[L].Default = ScreenSizes[L];
	return SM;
}

// ---------------------------------------------------------------- noise

float LifeNoise2(float X, float Y, uint32 Seed)
{
	const int32 Xi = FMath::FloorToInt(X), Yi = FMath::FloorToInt(Y);
	const float Fx = X - Xi, Fy = Y - Yi;
	const float Ux = Fx * Fx * (3.f - 2.f * Fx), Uy = Fy * Fy * (3.f - 2.f * Fy);
	const float A = LifeHash01(Xi, Yi, Seed), B = LifeHash01(Xi + 1, Yi, Seed), C = LifeHash01(Xi, Yi + 1, Seed), D = LifeHash01(Xi + 1, Yi + 1, Seed);
	return FMath::Lerp(FMath::Lerp(A, B, Ux), FMath::Lerp(C, D, Ux), Uy) * 2.f - 1.f;
}

float LifeFbm2(float X, float Y, int32 Oct, uint32 Seed)
{
	float Sum = 0.f, Amp = 0.5f, Norm = 0.f;
	for (int32 O = 0; O < Oct; ++O)
	{
		// rotate each octave to hide the lattice
		const float Rx = X * 0.8f - Y * 0.6f, Ry = X * 0.6f + Y * 0.8f;
		Sum += Amp * LifeNoise2(Rx, Ry, Seed + O * 101u);
		Norm += Amp;
		Amp *= 0.5f;
		X = Rx * 2.03f; Y = Ry * 2.03f;
	}
	return Sum / Norm;
}
