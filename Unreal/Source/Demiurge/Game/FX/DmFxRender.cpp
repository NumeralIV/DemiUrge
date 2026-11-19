// DEMIURGE — Game/FX/DmFxRender.cpp : see the header. Owner: C6 fx.

#include "Game/FX/DmFxRender.h"

#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

#include <algorithm>

using namespace DmFx;

void FDmFxRenderer::Init(UProceduralMeshComponent* InMesh, UMaterialInterface* InMat)
{
	Mesh = InMesh;
	Mat = InMat;
	Capacity = 0;
	UsedLast = 0;
	Quads.Reserve(4096);
}

void FDmFxRenderer::Begin(const FDmFxView& InView)
{
	CurView = InView;
	Quads.Reset();
}

void FDmFxRenderer::Grow(int32 NewCapacity)
{
	UProceduralMeshComponent* M = Mesh.Get();
	if (!M) return;
	Capacity = NewCapacity;
	const int32 NV = Capacity * 4;
	Pos.SetNumZeroed(NV);
	Nrm.Init(FVector(0, 0, 1), NV);
	UV0.SetNumZeroed(NV);
	UV1.SetNumZeroed(NV);
	UV2.SetNumZeroed(NV);
	UV3.SetNumZeroed(NV);
	Col.SetNumZeroed(NV);
	Tris.SetNumUninitialized(Capacity * 6);
	for (int32 Q = 0; Q < Capacity; ++Q)
	{
		const int32 B = Q * 4;
		int32* T = &Tris[Q * 6];
		T[0] = B; T[1] = B + 1; T[2] = B + 2;
		T[3] = B; T[4] = B + 2; T[5] = B + 3;
	}
	const TArray<FProcMeshTangent> NoTangents;
	M->CreateMeshSection(0, Pos, Tris, Nrm, UV0, UV1, UV2, UV3, Col, NoTangents, false);
	if (Mat.IsValid()) M->SetMaterial(0, Mat.Get());
	UsedLast = Capacity;   // force a full rewrite on the next upload
}

int32 FDmFxRenderer::End()
{
	const double T0 = FPlatformTime::Seconds();
	UProceduralMeshComponent* M = Mesh.Get();
	if (!M) { Quads.Reset(); return 0; }

	const FVector Cam = CurView.CamPos;
	const int32 N = Quads.Num();

	// cull (behind the camera) and sort back to front (Sort: (distance, quad index), members: no
	// per-frame allocation once warm)
	Sort.Reset();
	for (int32 I = 0; I < N; ++I)
	{
		const FQuad& Q = Quads[I];
		const FVector D = Q.C - Cam;
		const double Rad = Q.U.Size() + Q.V.Size();
		if (FVector::DotProduct(D, CurView.Fwd) < -Rad) continue;
		Sort.Add(TPair<float, int32>(float(D.Size()), I));
	}
	int32 Count = Sort.Num();
	if (Count > 1)
		std::sort(Sort.GetData(), Sort.GetData() + Count, [](const TPair<float, int32>& A, const TPair<float, int32>& B)
		{
			return A.Key > B.Key || (A.Key == B.Key && A.Value < B.Value);
		});
	Order.SetNumUninitialized(Count, EAllowShrinking::No);
	Keys.SetNumUninitialized(Count, EAllowShrinking::No);
	for (int32 I = 0; I < Count; ++I) { Order[I] = Sort[I].Value; Keys[I] = Sort[I].Key; }
	int32 First = 0;
	if (Count > MaxQuads) { First = Count - MaxQuads; Count = MaxQuads; }   // keep the nearest

	if (Count == 0)
	{
		if (M->IsVisible()) M->SetVisibility(false);
		LastDrawn = 0;
		LastMs = (FPlatformTime::Seconds() - T0) * 1000.0;
		return 0;
	}
	if (Count > Capacity)
	{
		int32 NewCap = FMath::Max(256, Capacity);
		while (NewCap < Count) NewCap *= 2;
		Grow(FMath::Min(NewCap, FMath::Max(MaxQuads, 256)));
		Count = FMath::Min(Count, Capacity);
		LowFrames = 0;
	}
	else if (Capacity > 1024 && Count * 4 < Capacity)
	{
		// verifier fix: UpdateMeshSection copies and uploads the WHOLE section every frame, so after one big
		// event (a meteor grows it to 4-16k quads) every later frame paid for the peak. Shrink once usage has
		// stayed under a quarter for 150 frames (one reallocation per peak, never per frame).
		if (++LowFrames > 150)
		{
			int32 NewCap = 256;
			while (NewCap < Count * 2) NewCap *= 2;
			Grow(NewCap);
			LowFrames = 0;
		}
	}
	else LowFrames = 0;
	if (!M->IsVisible()) M->SetVisibility(true);
	M->SetWorldLocationAndRotation(Cam, FRotator::ZeroRotator);

	const float MinPx = MinPixels;
	for (int32 K = 0; K < Count; ++K)
	{
		const FQuad& Q = Quads[Order[First + K]];
		const double Dist = FMath::Max(double(Keys[First + K]), 1.0);
		const float Px = CurView.PxPerUU(Dist);
		FVector U = Q.U, V = Q.V;
		float Dim = 1.f;
		const float LU = float(U.Size()) * Px, LV = float(V.Size()) * Px;
		if (LU < MinPx && LU > 1e-6f) { const float F = MinPx / LU; U *= F; Dim /= F; }
		if (LV < MinPx && LV > 1e-6f) { const float F = MinPx / LV; V *= F; Dim /= F; }
		// fade quads that swallow the camera (avoid a full-screen smear)
		const double Big = FMath::Max(U.Size(), V.Size());
		if (Dist < Big * 1.5) Dim *= float(FMath::Clamp((Dist / (Big * 1.5) - 0.35) / 0.65, 0.0, 1.0));

		const FVector C = Q.C - Cam;
		const int32 B = K * 4;
		Pos[B + 0] = C - U - V;
		Pos[B + 1] = C + U - V;
		Pos[B + 2] = C + U + V;
		Pos[B + 3] = C - U + V;
		UV0[B + 0] = FVector2D(Q.U0x[0], 0.0);
		UV0[B + 1] = FVector2D(Q.U0x[1], 0.0);
		UV0[B + 2] = FVector2D(Q.U0x[1], 1.0);
		UV0[B + 3] = FVector2D(Q.U0x[0], 1.0);
		const double SoftUU = Q.Soft > 0.f ? double(Q.Soft) : 0.6 * FMath::Min(Q.U.Size(), Q.V.Size());
		const FVector2D S1(double(uint8(Q.Shape)), SoftUU * 1.0e-5);
		const FVector2D S2(Q.Col.R * Dim, Q.Col.G * Dim);
		const FVector2D S3(Q.Col.B * Dim, FMath::Clamp(Q.Col.A * Dim, 0.f, 1.f));
		const uint8 AgeB = uint8(FMath::Clamp(Q.Age, 0.f, 1.f) * 255.f + 0.5f);
		for (int32 J = 0; J < 4; ++J)
		{
			UV1[B + J] = S1;
			UV2[B + J] = S2;
			UV3[B + J] = S3;
			Col[B + J] = FColor(uint8(FMath::Clamp(Q.Light[J], 0.f, 1.f) * 255.f + 0.5f), AgeB, Q.Seed, Q.Param);
		}
	}
	// collapse what was drawn last frame and is unused now
	for (int32 K = Count; K < UsedLast && K < Capacity; ++K)
	{
		const int32 B = K * 4;
		Pos[B] = Pos[B + 1] = Pos[B + 2] = Pos[B + 3] = FVector::ZeroVector;
		UV3[B] = UV3[B + 1] = UV3[B + 2] = UV3[B + 3] = FVector2D::ZeroVector;
		UV2[B] = UV2[B + 1] = UV2[B + 2] = UV2[B + 3] = FVector2D::ZeroVector;
	}
	UsedLast = Count;

	const TArray<FVector> NoNormals;
	const TArray<FProcMeshTangent> NoTangents;
	M->UpdateMeshSection(0, Pos, NoNormals, UV0, UV1, UV2, UV3, Col, NoTangents);
	LastDrawn = Count;
	LastMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	return Count;
}
