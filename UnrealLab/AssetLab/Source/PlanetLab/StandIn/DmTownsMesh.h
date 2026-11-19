// AssetLab copy (read-only reference) of Unreal/Source/Demiurge/Game/Towns: the generated stand-in the real asset replaces.
// Refreshed by UnrealLab/AssetLab/standin.ps1; never edit here.
// DEMIURGE — Game/Towns/DmTownsMesh.h : CPU mesh builder for generated buildings and ground overlays (C5 towns).
//
// FDmTownMesh holds vertices (metres, Z up, origin at the base centre) and one index list per material
// section. Sections of every town mesh:  0 body (walls, roofs, trims: M_TownBody), 1 glass (windows, lit at
// night: M_TownGlass), 2 glow (additive light pools and flames: M_TownGlow). Vertex conventions:
//   body   C.rgb = sRGB albedo, C.a = tint channel (0 = takes the instance wall tint, 128 = roof tint,
//          255 = fixed colour); UV0 = metric surface coordinates; UV1.x = surface pattern id (EDmTownPat),
//          UV1.y = height above the floor in metres, for ground-contact darkening.
//   glass  C.rgb = the colour of the window when lit (sRGB), C.a = per-window random 0..255 (lit test).
//   glow   C.rgb = additive colour (sRGB, black rim = soft falloff), C.a = 255 always on (flames), 0 night only.
// Helpers fix the winding from a wanted normal (Unreal's front face is cross(C - A, B - A)).
// MakeStaticMesh fills FStaticMeshRenderData directly (Engine module only, works in every build).
#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class UMaterialInterface;

/** Surface patterns (UV1.x of body vertices; M_TownBody selects colour noise, courses and roughness by it). */
enum class EDmTownPat : uint8
{
	Plaster = 0, Stone = 1, Brick = 2, Wood = 3, Concrete = 4, Thatch = 5, Tile = 6, Slate = 7, Metal = 8,
	Curtain = 9, Hide = 10, Mud = 11, Paint = 12,
};

struct FDmTownVert
{
	FVector3f P;
	FVector3f N;
	FVector2f UV0;
	FVector2f UV1;
	FColor C;
};

namespace DmTownSec { enum : int32 { Body = 0, Glass = 1, Glow = 2, Num = 3 }; }

/** Tint channel values (body C.a). */
namespace DmTownTint { constexpr uint8 Wall = 0, Roof = 128, Fixed = 255; }

struct PLANETLAB_API FDmTownMesh
{
	TArray<FDmTownVert> V;
	TArray<TArray<uint32>> Sec;

	explicit FDmTownMesh(int32 NumSections = DmTownSec::Num) { Sec.SetNum(NumSections); }

	int32 Add(const FVector3f& P, const FVector3f& N, const FVector2f& UV0, const FColor& C, const FVector2f& UV1 = FVector2f::ZeroVector)
	{
		return V.Add({ P, N, UV0, UV1, C });
	}
	void Tri(int32 S, uint32 A, uint32 B, uint32 C, const FVector3f& WantN);
	void Quad(int32 S, uint32 A, uint32 B, uint32 C, uint32 D, const FVector3f& WantN) { Tri(S, A, B, C, WantN); Tri(S, A, C, D, WantN); }
	int32 NumTris() const;
	bool IsEmpty() const { return NumTris() == 0; }
	void Reset() { V.Reset(); for (TArray<uint32>& I : Sec) I.Reset(); }

	/** Flat quad A,B,C,D (around the rim), normal toward WantN, UVs projected on its plane in metres. */
	void Q4(int32 S, const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& D, const FColor& Col, const FVector2f& U1, const FVector3f& WantN);
	/** Same with explicit UV0 per corner. */
	void Q4uv(int32 S, const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& D, const FColor& Col,
		const FVector2f& Ua, const FVector2f& Ub, const FVector2f& Uc, const FVector2f& Ud, const FVector2f& U1, const FVector3f& WantN);
	void T3(int32 S, const FVector3f& A, const FVector3f& B, const FVector3f& C, const FColor& Col, const FVector2f& U1, const FVector3f& WantN);
	/** Oriented box (centre, half extents, yaw radians about Z); bBottom adds the bottom face. */
	void Box(int32 S, const FVector3f& Ctr, const FVector3f& Half, float Yaw, const FColor& Col, const FVector2f& U1, bool bBottom = false);
	/** A beam of width W and height H from A to B (base line A..B). */
	void Beam(int32 S, const FVector3f& A, const FVector3f& B, float W, float H, const FColor& Col, const FVector2f& U1);
	/** Smooth vertical cylinder / frustum (no bottom), optional top cap. */
	void Cylinder(int32 S, const FVector3f& Base, float R0, float R1, float H, int32 Sides, const FColor& Col, bool bCap, const FVector2f& U1);
	/** Smooth cone from a ring of radius R at Base up to Base + H. */
	void Cone(int32 S, const FVector3f& Base, float R, float H, int32 Sides, const FColor& Col, const FVector2f& U1);
	/** Rectangular pyramid (yawed), flat faces. */
	void Pyramid(int32 S, const FVector3f& Base, float HalfX, float HalfY, float Yaw, float H, const FColor& Col, const FVector2f& U1);
	/** Smooth half dome of radius R on Base (Rings x Sides). */
	void Dome(int32 S, const FVector3f& Base, float R, float HScale, int32 Sides, int32 Rings, const FColor& Col, const FVector2f& U1);
	/** Additive light pool: a soft disc on the ground (glow section). */
	void GlowDisc(const FVector3f& C, float R, const FColor& Col, bool bAlways = false, int32 Seg = 12);
	/** A window quad on a wall (glass section): centre P, wall direction U, outward N, size W x H. */
	void Window(const FVector3f& P, const FVector3f& U, const FVector3f& N, float W, float H, const FColor& LitCol, uint8 Rand);

	void Append(const FDmTownMesh& O, const FTransform3f& T);
	/** Writes UV1.y of every body vertex: its height above Z0 in metres. */
	void SetHeightFraction(float Z0, float Z1);
	FBox3f Bounds() const;
};

/** Deterministic xorshift random (render side; never the world rng). */
struct FDmTownRand
{
	uint32 S;
	explicit FDmTownRand(uint32 Seed) : S(Seed * 747796405u + 2891336453u) { if (S == 0) S = 1; Next(); }
	uint32 Next() { S ^= S << 13; S ^= S >> 17; S ^= S << 5; return S; }
	float F() { return float(Next() & 0xFFFFFF) / float(0x1000000); }
	float R(float A, float B) { return A + (B - A) * F(); }
	int32 I(int32 A, int32 B) { return B <= A ? A : A + int32(Next() % uint32(B - A + 1)); }   // inclusive
	bool P(float Prob) { return F() < Prob; }
};

namespace DmTowns
{
	inline uint32 Hash(uint32 A, uint32 B = 0, uint32 C = 0)
	{
		uint32 H = A * 0x8da6b343u ^ B * 0xd8163841u ^ C * 0xcb1ab31fu;
		H ^= H >> 13; H *= 0x5bd1e995u; H ^= H >> 15;
		return H;
	}
	inline float Hash01(uint32 A, uint32 B = 0, uint32 C = 0) { return float(Hash(A, B, C) & 0xFFFFFF) / float(0x1000000); }
	/** Value noise / fbm in 2D, roughly -1..1. Octaves are clamped to 1..8. */
	PLANETLAB_API float Noise2(float X, float Y, uint32 Seed = 0);
	PLANETLAB_API float Fbm2(float X, float Y, int32 Octaves, uint32 Seed = 0);
	inline float Smooth(float E0, float E1, float X) { const float T = FMath::Clamp((X - E0) / (E1 - E0), 0.f, 1.f); return T * T * (3.f - 2.f * T); }
	inline FColor Col(float R, float G, float B, float A = 1.f)
	{
		return FColor(uint8(FMath::Clamp(R, 0.f, 1.f) * 255.f + 0.5f), uint8(FMath::Clamp(G, 0.f, 1.f) * 255.f + 0.5f),
			uint8(FMath::Clamp(B, 0.f, 1.f) * 255.f + 0.5f), uint8(FMath::Clamp(A, 0.f, 1.f) * 255.f + 0.5f));
	}
	/** A colour with an explicit tint channel (body alpha). */
	inline FColor ColT(float R, float G, float B, uint8 Tint) { FColor C = Col(R, G, B); C.A = Tint; return C; }
	inline FColor Mul(const FColor& C, float K) { return FColor(uint8(FMath::Clamp(C.R * K, 0.f, 255.f)), uint8(FMath::Clamp(C.G * K, 0.f, 255.f)), uint8(FMath::Clamp(C.B * K, 0.f, 255.f)), C.A); }
	inline FVector2f Pat(EDmTownPat P) { return FVector2f(float(uint8(P)), 0.f); }

	/** Builds a transient UStaticMesh from LODs (sections as above). ScreenSizes per LOD. */
	PLANETLAB_API UStaticMesh* MakeStaticMesh(UObject* Outer, const FString& Name, const TArray<const FDmTownMesh*>& LODs,
		const TArray<UMaterialInterface*>& Mats, const TArray<float>& ScreenSizes, bool bShadows = true, bool bColors = true);
}
