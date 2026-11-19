// DEMIURGE — Game/Nature/DmNatureMesh.h : CPU mesh builder for generated nature meshes (C4 nature).
//
// FDmNatureMesh collects vertices (metres, Z up, origin at the base) and one index list per material
// section; DmNature::MakeStaticMesh turns a set of LOD meshes into a transient UStaticMesh with hand-made
// LODs (fast runtime build, no asset). Ported from the LifeLab (docs/lab/vegetation-and-towns.md §3):
//   - every triangle helper takes the WANTED outward normal and fixes the winding (Unreal's front face is
//     cross(C - A, B - A); getting this wrong was the lab's most common bug);
//   - an empty section keeps one degenerate triangle (the fast build skips empty sections and the
//     section -> material mapping shifts otherwise);
//   - vertex colour is sRGB (materials linearise with pow 2.2), vertex alpha = ambient occlusion;
//   - UV1 carries wind weights (x = sway metres per unit wind, y = flutter) or animation weights;
//   - UV2..UV4 describe camera-facing clump cards (see FDmNatureVert), which M_NatureLeaf turns towards the camera.
// Also the deterministic hash / noise helpers shared by the nature generators.
#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class UMaterialInterface;

struct FDmNatureVert
{
	FVector3f P;
	FVector3f N;
	FVector2f UV0;
	FVector2f UV1;
	FColor C;
	// camera-facing clump cards (far LODs): UV2 = (dx, dy), UV3 = (dz, corner code) of the offset from this corner to
	// the card centre (metres, mesh space), code = 1 + s + 2 t (0 = an ordinary vertex); UV4 = (width, length) m
	FVector2f UV2 = FVector2f::ZeroVector;
	FVector2f UV3 = FVector2f::ZeroVector;
	FVector2f UV4 = FVector2f::ZeroVector;
};

struct DEMIURGE_API FDmNatureMesh
{
	TArray<FDmNatureVert> V;
	TArray<TArray<uint32>> Sec;   // triangle indices per material section

	explicit FDmNatureMesh(int32 NumSections = 1) { Sec.SetNum(NumSections); }

	int32 Add(const FVector3f& P, const FVector3f& N, const FVector2f& UV0, const FColor& C, const FVector2f& UV1 = FVector2f::ZeroVector)
	{
		return V.Add({ P, N, UV0, UV1, C });
	}
	/** Triangle whose front face points along WantN (winding fixed automatically). */
	void Tri(int32 S, uint32 A, uint32 B, uint32 C, const FVector3f& WantN);
	/** Quad A,B,C,D in order around its rim; front face along WantN. */
	void Quad(int32 S, uint32 A, uint32 B, uint32 C, uint32 D, const FVector3f& WantN) { Tri(S, A, B, C, WantN); Tri(S, A, C, D, WantN); }
	int32 NumTris() const;
	bool IsEmpty() const { return NumTris() == 0; }

	/** Smooth vertical cylinder (open bottom), optional top cap; UV in metres. */
	void Cylinder(int32 S, const FVector3f& Base, float R0, float R1, float H, int32 Sides, const FColor& Col, bool bCap, const FVector2f& UV1 = FVector2f::ZeroVector);
	/** Smooth ellipsoid (UV sphere) at Center with radii Rad, rotated by Rot. */
	void Ellipsoid(int32 S, const FVector3f& Center, const FVector3f& Rad, const FQuat4f& Rot, int32 Seg, int32 Rings, const FColor& Col, const FVector2f& UV1 = FVector2f::ZeroVector);
	/** Smooth tapered limb between two points (a short tube with end caps rounded by the next part). */
	void Limb(int32 S, const FVector3f& A, const FVector3f& B, float RA, float RB, int32 Sides, const FColor& Col, const FVector2f& UV1 = FVector2f::ZeroVector);
	void Append(const FDmNatureMesh& O, const FTransform3f& T);
	/** Sets UV1 of every vertex added since index From. */
	void SetUV1From(int32 From, const FVector2f& UV1) { for (int32 I = From; I < V.Num(); ++I) V[I].UV1 = UV1; }
};

namespace DmNature
{
	/** Transient UStaticMesh from LOD meshes (LODs[i] = LOD i). Mats[s] = material of section s (slot "S<s>").
	 *  ScreenSizes per LOD. Bounds are extended by BoundsExtend (metres, all directions) for animated meshes. */
	DEMIURGE_API UStaticMesh* MakeStaticMesh(UObject* Outer, const FString& Name, const TArray<const FDmNatureMesh*>& LODs,
		const TArray<UMaterialInterface*>& Mats, const TArray<float>& ScreenSizes, float BoundsExtend = 0.f);

	/** Deterministic xorshift random (meshes, layouts). */
	struct FRand
	{
		uint32 S;
		explicit FRand(uint32 Seed) : S(Seed * 747796405u + 2891336453u) { Next(); }
		uint32 Next() { S ^= S << 13; S ^= S >> 17; S ^= S << 5; return S; }
		float F() { return float(Next() & 0xFFFFFF) / float(0x1000000); }
		float R(float A, float B) { return A + (B - A) * F(); }
		int32 I(int32 A, int32 B) { return A + int32(Next() % uint32(B - A + 1)); }   // inclusive
		FVector3f Unit()
		{
			const float Z = R(-1.f, 1.f), A = R(0.f, 2.f * PI), Q = FMath::Sqrt(FMath::Max(0.f, 1.f - Z * Z));
			return FVector3f(Q * FMath::Cos(A), Q * FMath::Sin(A), Z);
		}
	};

	inline uint32 Hash2(int32 X, int32 Y, uint32 Seed = 0)
	{
		uint32 H = uint32(X) * 0x8da6b343u ^ uint32(Y) * 0xd8163841u ^ Seed * 0xcb1ab31fu;
		H ^= H >> 13; H *= 0x5bd1e995u; H ^= H >> 15;
		return H;
	}
	inline uint32 Hash3(int32 X, int32 Y, int32 Z, uint32 Seed = 0)
	{
		uint32 H = uint32(X) * 0x8da6b343u ^ uint32(Y) * 0xd8163841u ^ uint32(Z) * 0xcb1ab31fu ^ Seed * 0x165667b1u;
		H ^= H >> 13; H *= 0x5bd1e995u; H ^= H >> 15;
		return H;
	}
	inline float Hash01(uint32 H) { return float(H & 0xFFFFFF) / float(0x1000000); }
	inline float Smooth(float E0, float E1, float X) { const float T = FMath::Clamp((X - E0) / (E1 - E0), 0.f, 1.f); return T * T * (3.f - 2.f * T); }
	inline FColor Col(float R, float G, float B, float A = 1.f)
	{
		return FColor(uint8(FMath::Clamp(R, 0.f, 1.f) * 255.f + 0.5f), uint8(FMath::Clamp(G, 0.f, 1.f) * 255.f + 0.5f),
			uint8(FMath::Clamp(B, 0.f, 1.f) * 255.f + 0.5f), uint8(FMath::Clamp(A, 0.f, 1.f) * 255.f + 0.5f));
	}
	inline FColor ColMul(const FColor& C, float K, float A = -1.f)
	{
		return Col(C.R / 255.f * K, C.G / 255.f * K, C.B / 255.f * K, A < 0.f ? C.A / 255.f : A);
	}

	/** 3D value noise and fbm. IDENTICAL to the HLSL in Scripts/nature.py (NOISE_HLSL): the CPU scatter and
	 *  the canopy decal agree on every forest edge. Input in noise cells; returns -1..1. */
	DEMIURGE_API float VNoise3(float X, float Y, float Z);
	DEMIURGE_API float Fbm3(float X, float Y, float Z, int32 Octaves);
}
