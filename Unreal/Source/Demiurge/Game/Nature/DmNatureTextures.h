// DEMIURGE — Game/Nature/DmNatureTextures.h : textures painted in C++ at startup for the generated plants (C4).
//
// Leaf atlas (4096 x 2048, eight 1024 tiles: broadleaf twig, fir needle spray, palm frond, bipinnate leaflets, and
// the far-LOD clumps: broadleaf mass (two), dense needle tier, leaflet mass) with
// push-pull colour dilation and alpha-coverage-preserving mips (without them canopies thin out and turn
// see-through at 1-3 km), fissured bark and ringed palm bark with normal maps. Painted once (~0.4 s) and
// bound to the nature materials through dynamic instances. Ported from the LifeLab (LifeTex.cpp).
// An asset pass replaces these together with the meshes (DmNatureFactory).
#pragma once

#include "CoreMinimal.h"

class UTexture2D;

/** Leaf atlas tiles (4 x 2 tiles of 1024 px in a 4096 x 2048 atlas). Near LODs use the detailed sprays (twig,
 *  needles, frond, leaflets); far LODs use the dense CLUMP tiles (a whole mass of leaves with its own
 *  self-shadowing painted in), so a crown made of 8-30 cards still reads as a solid volume from 1-5 km.
 *  A card's UV is Origin + ((s + 0.5) * kDmLeafTileU, (1 - t) * kDmLeafTileV), s across [-0.5, 0.5], t along [0, 1]. */
enum class EDmLeafTile : uint8
{
	Broadleaf = 0, Needles = 1, Frond = 2, Leaflets = 3,
	BroadClump = 4, NeedleClump = 5, LeafletClump = 6, BroadClumpB = 7,
	Num
};
inline constexpr float kDmLeafTileU = 0.25f;
inline constexpr float kDmLeafTileV = 0.5f;
inline FVector2f DmLeafTileOrigin(EDmLeafTile T)
{
	const int32 I = int32(T);
	return FVector2f((I % 4) * kDmLeafTileU, (I / 4) * kDmLeafTileV);
}
/** Atlas UV of tile coordinates (S across 0..1 left to right, T along 0..1 bottom to top). */
inline FVector2f DmLeafUV(EDmLeafTile Tile, float S01, float T01)
{
	return DmLeafTileOrigin(Tile) + FVector2f(S01 * kDmLeafTileU, (1.f - T01) * kDmLeafTileV);
}

struct DEMIURGE_API FDmNatureTextures
{
	UTexture2D* LeafAtlas = nullptr;   // sRGB colour + alpha mask (coverage-preserving mips, per tile)
	UTexture2D* Bark = nullptr;        // sRGB fissured bark detail
	UTexture2D* BarkN = nullptr;       // linear normal (0.5 + 0.5 n)
	UTexture2D* PalmBark = nullptr;    // sRGB ringed palm trunk
	UTexture2D* PalmBarkN = nullptr;

	void Build();
	void AddReferencedObjects(TArray<UObject*>& Out) const;
};

namespace DmNature
{
	/** Transient texture with a full, hand-filled mip chain (BGRA8). bAlphaCoverage keeps the fraction of texels
	 *  above AlphaRef constant over the mips; bDilate fills transparent texels with nearby colour. */
	DEMIURGE_API UTexture2D* MakeTexture(const FString& Name, int32 W, int32 H, TArray<FColor>&& Mip0, bool bSRGB,
		bool bAlphaCoverage, float AlphaRef = 0.4f, bool bDilate = false, bool bClampX = false, bool bClampY = false,
		int32 CoverageTilesX = 1, int32 CoverageTilesY = 1);
}
