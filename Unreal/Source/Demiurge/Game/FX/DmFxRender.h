// DEMIURGE — Game/FX/DmFxRender.h : turns this frame's quads into one sorted procedural-mesh draw.
// Owner: C6 fx.
//
// Every quad is written camera-relative (the mesh component sits at the camera, so float vertex
// precision is best where it matters, at any distance from the 1e7 uu planet). Quads are sorted back
// to front (premultiplied AlphaComposite blending needs order), quads thinner than MinPixels on screen
// are widened to MinPixels and dimmed by the same factor (energy is kept, so a spark seen from orbit
// is a faint pixel, not a flickering hole), and the section only grows in power-of-two steps (no
// per-frame reallocation once warm). The per-vertex layout is the contract with Scripts/fx/fx.hlsl.
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "Game/FX/DmFxTypes.h"

class UProceduralMeshComponent;
class UMaterialInterface;

struct FDmFxView
{
	FVector CamPos = FVector::ZeroVector;
	FVector Fwd = FVector::ForwardVector, Right = FVector::RightVector, Up = FVector::UpVector;
	float TanHalfFov = 0.5f;      // tan(horizontal fov / 2)
	float ScreenW = 1920.f;
	/** Pixels per uu at distance D: ScreenW / (2 D TanHalfFov). */
	float PxPerUU(double D) const { return float(double(ScreenW) / (2.0 * FMath::Max(D, 1.0) * double(TanHalfFov))); }
};

class FDmFxRenderer
{
public:
	void Init(UProceduralMeshComponent* InMesh, UMaterialInterface* InMat);
	void Begin(const FDmFxView& InView);
	void Add(const DmFx::FQuad& Q) { if (Quads.Num() < HardCap) Quads.Add(Q); }
	/** Sort, clamp, write the vertex arrays and upload. Returns the number of quads drawn. */
	int32 End();
	const FDmFxView& View() const { return CurView; }
	int32 NumPending() const { return Quads.Num(); }

	int32 MaxQuads = 12000;       // drawn per frame (the nearest win when there are more)
	float MinPixels = 0.8f;       // thinnest on-screen half-width before widening
	int32 LastDrawn = 0;
	double LastMs = 0;

private:
	void Grow(int32 NewCapacity);

	TWeakObjectPtr<UProceduralMeshComponent> Mesh;
	TWeakObjectPtr<UMaterialInterface> Mat;
	FDmFxView CurView;
	TArray<DmFx::FQuad> Quads;
	TArray<TPair<float, int32>> Sort;
	TArray<int32> Order;
	TArray<float> Keys;
	int32 HardCap = 60000;

	int32 Capacity = 0;           // quads the section holds
	int32 UsedLast = 0;           // quads written last frame (the rest are degenerate)
	int32 LowFrames = 0;          // consecutive frames using under a quarter of Capacity (shrink after 5 s)
	TArray<FVector> Pos;
	TArray<int32> Tris;
	TArray<FVector> Nrm;
	TArray<FVector2D> UV0, UV1, UV2, UV3;
	TArray<FColor> Col;
};
