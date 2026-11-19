// DEMIURGE — Game/Nature/DmNatureFields.h : what grows where, baked from the world (C4 nature).
//
// An immutable snapshot of the sim fields nature needs, resampled onto equirectangular grids (2048 x 1024,
// ~300 m per texel at the equator) with smoothing and a domain warp, so biome borders never follow the
// hexagonal sim cells. Built on a worker thread from a copy of the world's arrays; shared read-only with the
// tile scatter tasks. The Canopy grid IS the canopy texture the orbit decal samples (same bytes, same
// bilinear filter): the CPU scatter and the GPU canopy agree on every forest edge, which also uses the
// same noise (CanopyValue here == NatureCanopy() in Scripts/nature.py).
//
//   Canopy  R forest potential (veg on plantable land: no sea, ice, farm, town, lake, burn, frost)
//           G conifer share   B tropical-wet share   A dry (savanna) share
//   Climate R temperature ((T + 40) / 80)   G moisture   B snow on trees   A scorch
//   Land    R farm   G urban   B lake   A fauna
#pragma once

#include "CoreMinimal.h"
#include <memory>

namespace dm { struct World; class Geo; }

struct DEMIURGE_API FDmNatureFields
{
	static constexpr int32 W = 2048;
	static constexpr int32 H = 1024;
	TArray<FColor> Canopy, Climate, Land;

	struct FClearing { FVector Dir; double RadiusM; };
	TArray<FClearing> Clearings;       // settlements (towns sit in farmland, not in forest)

	// per-cell copies for wildlife
	std::shared_ptr<const dm::Geo> Geo;
	int32 N = 0;
	TArray<float> Fauna, Veg, Elev, Temp;
	TArray<uint8> Biome;
	double SeaLevel = 0, SeaRadiusUU = 1.0e7;
	double BuildMs = 0;
	uint32 Version = 0;

	/** Copies the world's arrays (game thread, cheap); Bake() does the heavy work (any thread). */
	static TSharedPtr<FDmNatureFields, ESPMode::ThreadSafe> Copy(const dm::World& W, uint32 Version);
	void Bake();

	/** Equirectangular texture coordinates of a unit planet-frame direction (u wraps, v = 0 at the north pole). */
	static FVector2D UV(const FVector& Dir);
	/** Bilinear sample (0..1 per channel), exactly like the GPU's filter on mip 0. */
	FVector4f Sample(const TArray<FColor>& Grid, const FVector& Dir) const;
	/** The shared forest-edge function: canopy fraction 0..1 from the forest potential R at Dir. */
	static float CanopyValue(float R, const FVector& Dir);
	/** Settlement clearing factor (1 = free to grow, 0 = cleared) at Dir. */
	float ClearingAt(const FVector& Dir, const TArray<int32>& Near) const;
	/** Clearings within AngleRad (+ their own radius) of Dir. */
	void NearClearings(const FVector& Dir, double AngleRad, TArray<int32>& Out) const;

private:
	TArray<float> CElev, CVeg, CTemp, CMoist, CIce, CFarm, CUrban, CWater, CScorch, CFauna;
};

using FDmNatureFieldsPtr = TSharedPtr<const FDmNatureFields, ESPMode::ThreadSafe>;
