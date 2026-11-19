// LifeLab: textures painted procedurally in C++ at startup (leaf atlas, bark, building and ground detail).
#pragma once
#include "CoreMinimal.h"

class UTexture2D;

/** Leaf atlas tiles (2x2). A card's UV rect is Origin + (s+0.5, 1-t) * 0.5 with s across [-0.5,0.5] and t along the twig [0,1]. */
enum class ELeafTile : uint8 { Broadleaf = 0, Needles = 1, Frond = 2, Leaflets = 3 };
inline FVector2f LeafTileOrigin(ELeafTile T)
{
	const int32 I = int32(T);
	return FVector2f((I % 2) * 0.5f, (I / 2) * 0.5f);
}

struct FLifeTextures
{
	UTexture2D* LeafAtlas = nullptr;   // sRGB colour + alpha mask, coverage-preserving mips
	UTexture2D* Bark = nullptr;        // sRGB detail (left half: fissured bark, right half: palm rings)
	UTexture2D* BarkN = nullptr;       // linear normal map (0.5 + 0.5 n)
	UTexture2D* PalmBark = nullptr;    // sRGB ringed palm trunk
	UTexture2D* PalmBarkN = nullptr;
	UTexture2D* Wall = nullptr;        // linear masks: R plaster mottle, G stone masonry, B brick, A wood grain
	UTexture2D* WallN = nullptr;       // linear normal map for masonry (stone in RG, brick in BA)
	UTexture2D* Roof = nullptr;        // linear masks: R thatch, G clay tiles, B slate, A metal seams
	UTexture2D* RoofN = nullptr;       // linear normal: thatch RG, tiles BA
	UTexture2D* Ground = nullptr;      // linear masks: R fine mottle, G crown clumps, B pebbles/dirt, A strata
	UTexture2D* WaterN = nullptr;      // linear normal map for ripples

	void Build();
	void AddReferencedObjects(TArray<UObject*>& Out) const;
};

/** Makes a transient texture with a full mip chain. Pixels are BGRA (FColor). */
UTexture2D* LifeMakeTexture(const FString& Name, int32 W, int32 H, TArray<FColor>&& Mip0, bool bSRGB, bool bAlphaCoverage, float AlphaRef = 0.4f, bool bDilate = false, bool bClamp = false);
