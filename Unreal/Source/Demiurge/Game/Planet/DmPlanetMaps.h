// DEMIURGE — Game/Planet/DmPlanetMaps.h : per-cell world state as planet-wide textures. Owner: C1 planet.
//
// Every per-cell quantity the sim can change that does NOT move the ground (biome colour, vegetation,
// dryness, wetness, snow and ice, sea ice, farmland, urban, roads, night lights, scorch, lava, fire,
// fallout, and the overlay modes) lives in five octahedral RGBA8 maps of the whole planet
// (StateSize^2 texels, ~350 m each, about 5 per cell spacing). The land and water materials sample
// them by direction, so a colour change is a texture update (a few ms), never a mesh rebuild, and it
// looks the same at every LOD level. Each texel caches the sim triangle (and barycentrics) of its
// direction and of a domain-warped direction (organic biome borders instead of the hexagonal cells),
// so an update is a weighted lookup per texel (ParallelFor). Live updates run on a worker from inputs
// copied on the game thread (CaptureInputs / Compute / ApplyResult): the game thread only pays the copy and
// the texture upload.
//
//   Map 0  RGB macro albedo (sRGB), A forest canopy density
//   Map 1  R temperature ((T + 50) / 90), G wetness, B aridity, A ice (glacier on land, sea ice on sea)
//   Map 2  R farmland, G urban, B road presence, A night lights
//   Map 3  R scorch, G lava, B fire, A fallout (rad)
//   Map 4  overlay RGB + strength (Planet overlay modes 1..5)
//
// The river map (RiverSize^2 octahedral, ~87 m texels) holds R = SIGNED distance to the nearest river
// centreline (EncodeSigned: linear within +-125 m, compressed out to +-400 m), G = its half-width / 64 m,
// B = river coverage (box-filtered mips for orbit), A = signed distance to the nearest road (mip 0) / road
// coverage (mips >= 1). Signed, so bilinear filtering keeps thin lines continuous (land.hlsl).
//
// Octahedral mapping of a unit planet-frame (Unreal axes) direction, identical in the material.
#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class FDmGeoLocator;
class FDmPlanetTerrain;
namespace dm { struct World; struct Snapshot; }

namespace DmPlanetOct
{
	FVector2D UV(const FVector3d& D);
	FVector3d Dir(const FVector2D& UV);
}

struct FDmRoadSeg { FVector3d A, B; float Level; };

/** Everything the state and overlay maps read from the world, copied on the game thread (a few per-cell
 *  arrays, about 1 ms) so the maps can be computed on a worker while the sim keeps running. */
struct FDmMapInputs
{
	int32 N = 0;
	float SeaLevel = 0.f;
	int32 Overlay = 0;
	TArray<float> Veg, Moist, Temp, Ice, Urban, Water, Elev, Wet, Farm, Scorch, Lava, Fire, Rad, Hab, Lights;
	TArray<uint8> Biome, Road;
	TArray<int32> Culture;          // overlay 1 / 4: culture of the cell's owner (-1 none)
	TArray<float> Devotion;         // overlay 4: devotion of the cell's owner
	TArray<FVector3f> CultureColor;
};

class FDmPlanetMaps
{
public:
	static constexpr int32 StateSize = 1024;
	static constexpr int32 RiverSize = 4096;
	static constexpr double RiverRangeM = 400.0;
	static constexpr int32 NumMaps = 5;

	/** Computed maps (all mips) for the maps in Mask, ready to upload. */
	struct FResult
	{
		uint32 Mask = 0;
		TArray<TArray<FColor>> Mips[NumMaps];
		double ComputeMs = 0.0;
	};
	/** The immutable per-texel cache of a grid (shared with worker jobs). */
	struct FCache
	{
		struct FTexelLoc { int32 C[3]; float W[3]; };
		TSharedPtr<const FDmGeoLocator, ESPMode::ThreadSafe> Loc;
		TArray<FTexelLoc> Direct, Warped;
		TArray<float> Wob;               // static colour wobble per texel
		TArray<int32> Nearest;           // nearest cell per texel
	};

	/** Texel cache for a grid (ParallelFor, ~0.1 s at 1024^2). */
	void Init(const TSharedPtr<const FDmGeoLocator, ESPMode::ThreadSafe>& Loc);
	bool IsReady() const { return Cache.IsValid() && Cache->Direct.Num() > 0; }
	TSharedPtr<const FCache, ESPMode::ThreadSafe> GetCache() const { return Cache; }

	/** Game thread: copy what maps 0..3 (and the overlay map, for Overlay > 0) read from the world. */
	static void CaptureInputs(const dm::World& W, int32 Overlay, FDmMapInputs& Out);
	/** Any thread: maps in Mask (bits 0..3 state, bit 4 overlay) with their mips, from captured inputs. */
	static void Compute(const FCache& C, const FDmMapInputs& In, uint32 Mask, FResult& Out);
	/** Game thread: keep mip 0 (ForestAt, snapshots) and upload every map of R. */
	void ApplyResult(UObject* Outer, FResult& R);

	/** Synchronous convenience (world load): capture, compute and upload maps 0..4. */
	void ComputeAll(UObject* Outer, const dm::World& W, int32 Overlay);
	/** Time-lapse colouring (maps 0..3 from a snapshot: biome, owner, urban); upload with Upload(0xF). */
	void ComputeSnapshot(const dm::World& W, const dm::Snapshot& S);

	/** Create the textures (once) and upload the CPU buffers of the maps in Mask (bit i = map i). */
	void Upload(UObject* Outer, uint32 Mask);
	UTexture2D* Tex(int32 I) const { return Textures[I]; }
	/** Map 0 alpha (forest canopy) at a planet-frame (Unreal axes) direction, nearest texel. */
	float ForestAt(const FVector3d& UEDir) const;
	/** Hash of the inputs of the river map (rivers + roads), to skip identical rebuilds. */
	static uint32 RoadHash(const dm::World& W);

	/** River + road map (pure function of the terrain's river polylines and the road segments; any thread). */
	static void BuildRiverMipData(const FDmPlanetTerrain& T, const TArray<FDmRoadSeg>& Roads, TArray<TArray<FColor>>& OutMips, int32& OutNear);
	static UTexture2D* MakeRiverTexture(UObject* Outer, const TArray<TArray<FColor>>& Mips);
	static void CaptureRoads(const dm::World& W, TArray<FDmRoadSeg>& Out);
	/** 8-bit encoding of a signed distance (m) for the river map; decoded by land.hlsl DecodeSigned. */
	static uint8 EncodeSigned(double D);

	double LastComputeMs = 0.0, LastUploadMs = 0.0, InitMs = 0.0;
	TArray<UTexture2D*> Textures;   // owned (referenced) by ADmPlanet

private:
	void UploadMips(UObject* Outer, int32 M, TArray<TArray<FColor>>& Mips);
	TSharedPtr<FCache, ESPMode::ThreadSafe> Cache;
	TArray<FColor> Cpu[NumMaps];     // mip 0 of each map
};
