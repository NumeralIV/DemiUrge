// LifeLab: a tiny CPU mesh builder that turns generated geometry into UStaticMesh (with LODs) at runtime.
#pragma once
#include "CoreMinimal.h"

class UStaticMesh;
class UMaterialInterface;

/** One vertex. Colours are sRGB bytes (materials linearise with pow 2.2); UV1 carries wind weights or style ids. */
struct FLifeVert
{
	FVector3f P;
	FVector3f N;
	FVector2f UV0;
	FVector2f UV1;
	FColor C;
};

struct FLifeMesh
{
	TArray<FLifeVert> V;
	TArray<TArray<uint32>> Sec; // triangle indices per material section

	explicit FLifeMesh(int32 NumSections = 1) { Sec.SetNum(NumSections); }

	int32 Add(const FVector3f& P, const FVector3f& N, const FVector2f& UV0, const FColor& C, const FVector2f& UV1 = FVector2f::ZeroVector)
	{
		return V.Add({ P, N, UV0, UV1, C });
	}
	/** Adds a triangle whose front face points along WantN (winding fixed automatically). */
	void Tri(int32 S, uint32 A, uint32 B, uint32 C, const FVector3f& WantN);
	/** Quad A,B,C,D in order around its rim; front face along WantN. */
	void Quad(int32 S, uint32 A, uint32 B, uint32 C, uint32 D, const FVector3f& WantN)
	{
		Tri(S, A, B, C, WantN);
		Tri(S, A, C, D, WantN);
	}
	int32 NumTris() const;
	bool IsEmpty() const { return NumTris() == 0; }

	// ---- shape helpers (flat-shaded unless noted) ----
	/** Planar quad with its own vertices (flat normal). UVs given per corner. */
	void FlatQuad(int32 S, const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& D, const FColor& Col,
		const FVector2f& UA, const FVector2f& UB, const FVector2f& UC, const FVector2f& UD, const FVector2f& UV1 = FVector2f::ZeroVector);
	/** Flat quad with UVs from world-space metres along the two edge directions (good for walls/roofs). */
	void FlatQuadMetric(int32 S, const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& D, const FColor& Col, const FVector2f& UV1 = FVector2f::ZeroVector, float UOffset = 0.f, float VOffset = 0.f);
	void FlatTri(int32 S, const FVector3f& A, const FVector3f& B, const FVector3f& C, const FColor& Col, const FVector2f& UV1 = FVector2f::ZeroVector);
	/** Oriented box: centre, half extents, yaw in radians (about Z). All six faces, metric UVs. bBottom=false skips the bottom face. */
	void Box(int32 S, const FVector3f& Center, const FVector3f& Half, float Yaw, const FColor& Col, const FVector2f& UV1 = FVector2f::ZeroVector, bool bBottom = false);
	/** Box between two base points (a beam): from A to B with width W and height H, base at A.Z/B.Z. */
	void Beam(int32 S, const FVector3f& A, const FVector3f& B, float W, float H, const FColor& Col, const FVector2f& UV1 = FVector2f::ZeroVector);
	/** Smooth-shaded vertical cylinder (open bottom), optional top cap. */
	void Cylinder(int32 S, const FVector3f& Base, float R0, float R1, float H, int32 Sides, const FColor& Col, bool bCap, const FVector2f& UV1 = FVector2f::ZeroVector);
	/** Smooth cone from a ring of radius R at Base up to an apex at Base+H. */
	void Cone(int32 S, const FVector3f& Base, float R, float H, int32 Sides, const FColor& Col, const FVector2f& UV1 = FVector2f::ZeroVector);
	/** Pyramid with a rectangular base (yawed), smooth-ish: flat faces. */
	void Pyramid(int32 S, const FVector3f& Base, float HalfX, float HalfY, float Yaw, float H, const FColor& Col, const FVector2f& UV1 = FVector2f::ZeroVector);

	void Append(const FLifeMesh& O, const FTransform3f& T);
};

/** Builds a transient UStaticMesh. LODs[i] are the LOD meshes; Mats[s] is the material of section s; ScreenSizes per LOD. */
UStaticMesh* LifeMakeStaticMesh(UObject* Outer, const FString& Name, const TArray<const FLifeMesh*>& LODs,
	const TArray<UMaterialInterface*>& Mats, const TArray<float>& ScreenSizes, int32 NaniteMode = -1);
/** NaniteMode: -1 = classic mesh with the given LODs (fast runtime build); 0/1/2 = Nanite (editor-only full build) with
 *  shape preservation None / PreserveArea / Voxelize, built from LOD0 only. */

/** Deterministic hash-based random helpers. */
struct FLifeRand
{
	uint32 S;
	explicit FLifeRand(uint32 Seed) : S(Seed * 747796405u + 2891336453u) { Next(); }
	uint32 Next() { S ^= S << 13; S ^= S >> 17; S ^= S << 5; return S; }
	float F() { return float(Next() & 0xFFFFFF) / float(0x1000000); }
	float R(float A, float B) { return A + (B - A) * F(); }
	int32 I(int32 A, int32 B) { return A + int32(Next() % uint32(B - A + 1)); } // inclusive
	FVector3f Unit() { const float Z = R(-1.f, 1.f), A = R(0.f, 2.f * PI), Q = FMath::Sqrt(FMath::Max(0.f, 1.f - Z * Z)); return FVector3f(Q * FMath::Cos(A), Q * FMath::Sin(A), Z); }
};

inline uint32 LifeHash(int32 X, int32 Y, uint32 Seed = 0)
{
	uint32 H = uint32(X) * 0x8da6b343u ^ uint32(Y) * 0xd8163841u ^ Seed * 0xcb1ab31fu;
	H ^= H >> 13; H *= 0x5bd1e995u; H ^= H >> 15;
	return H;
}
inline float LifeHash01(int32 X, int32 Y, uint32 Seed = 0) { return float(LifeHash(X, Y, Seed) & 0xFFFFFF) / float(0x1000000); }

/** Smooth value noise / fbm in 2D (fast, CPU). */
float LifeNoise2(float X, float Y, uint32 Seed = 0);
float LifeFbm2(float X, float Y, int32 Oct, uint32 Seed = 0);
inline float LifeSmooth(float E0, float E1, float X) { const float T = FMath::Clamp((X - E0) / (E1 - E0), 0.f, 1.f); return T * T * (3.f - 2.f * T); }
inline FColor LifeCol(float R, float G, float B, float A = 1.f)
{
	return FColor(uint8(FMath::Clamp(R, 0.f, 1.f) * 255.f + 0.5f), uint8(FMath::Clamp(G, 0.f, 1.f) * 255.f + 0.5f), uint8(FMath::Clamp(B, 0.f, 1.f) * 255.f + 0.5f), uint8(FMath::Clamp(A, 0.f, 1.f) * 255.f + 0.5f));
}
inline FColor LifeColMul(const FColor& C, float K, float A = -1.f)
{
	return LifeCol(C.R / 255.f * K, C.G / 255.f * K, C.B / 255.f * K, A < 0.f ? C.A / 255.f : A);
}
