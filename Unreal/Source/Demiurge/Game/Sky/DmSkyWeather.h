// DEMIURGE — Game/Sky/DmSkyWeather.h : the cloud-cover textures generated from the sim's fields (C2 sky).
// Owner: C2 sky. Port of the CloudLab weather map (docs/lab/clouds.md 1.1, 1.2) split for a world in motion:
//
//   FDmClimateSnapshot   copy of the per-cell fields the weather needs (moisture, temperature, elevation,
//                        land / sea), taken on the game thread from the dm::World (cheap: 4 x 41k floats)
//   FDmSkyWeather        climate grids (Gaussian splat of the cells onto 0.5 deg grids; nearest-cell
//                        lookup would show Voronoi cells) and the WEATHER MAPS, equirectangular in the planet
//                        frame, rebuilt about once a second on a worker thread and crossfaded by the material:
//                        * cover map (RGBA8): R = cover 0..1, G = cloud-top height, B = cumuliform,
//                          A = optical thickness (> 0.62 marks hurricane cores: cirrus canopy). Regimes from the
//                          sim's fields (ITCZ convection, trade cumulus, marine stratocumulus decks off west
//                          coasts, mid-latitude cloud, polar stratus, orographic cloud; dry land cleared) and
//                          the moving weather systems drawn on top (FDmSystemSnap): tropical cyclones (eye,
//                          eyewall, central dense overcast, segmented spiral rain bands, a clear subsidence moat),
//                          extratropical lows (cold front trailing equatorward, warm-front shield, comma head,
//                          dry slot, open-cell cumulus in the cold air behind the front), rain clusters;
//                        * warp map (RGBA16F): RG = offset of the cell-texture lookup in uv (the cyclones shear the
//                          cells into spirals: the material samples the static cell texture at uv + offset, so
//                          the spiral needs no per-sample math), B = cirrus (thin high cloud: jet-stream streaks,
//                          storm outflow, frontal shields, convective anvils), A = spare.
//                        The noise domain of the regimes morphs with sky time, so successive maps evolve.
//   BuildFine            the static cell-scale texture (R/G two independent sets of the lab's 70/190/420
//                        cycle noise, B/A the low-frequency phase / roughness noise the rain bands need)
//   DmSkyNoise           tileable 3D shape (Perlin-Worley, equalised) and detail (Worley) volumes
//
// Every builder is a pure function of its inputs (no UObjects), so the actor runs rebuilds on a
// background thread and only creates / updates textures on the game thread.
#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class UVolumeTexture;
namespace dm { struct World; }

struct FDmClimateSnapshot
{
	int32 N = 0;
	TArray<FVector3f> Dir;        // planet-frame unit directions (Unreal axes)
	TArray<float> Moist, Temp, Elev;
	float SeaLevel = 0.f;
	float SeaFraction = 0.7f;     // share of sea cells
	bool bValid = false;          // false: no world (analytic zonal climate)

	void FromWorld(const dm::World* W);
};

struct FDmSystemSnap;

struct FDmWeatherParams
{
	int32 Width = 2048, Height = 1024;
	uint32 Seed = 7;
	double ItczLatDeg = 5.0;      // mean latitude of the ITCZ (seasons shift it)
	double CoverScale = 1.0;      // global multiplier on regime cover
	double MorphTime = 0.0;       // sky seconds: the noise domain slides with it (weather evolves)
};

class FDmSkyWeather
{
public:
	/** Splat the cells onto the coarse climate grids (call when the climate changed). */
	void BuildGrids(const FDmClimateSnapshot& S);
	/** The weather maps (cover: BGRA8 in memory = PF_B8G8R8A8; warp: PF_FloatRGBA). Thread-safe after BuildGrids. */
	void BuildMaps(const FDmWeatherParams& P, const TArray<FDmSystemSnap>& Systems, TArray<FColor>& OutCover, TArray<FFloat16Color>& OutWarp, bool bBackground) const;
	/** Land fraction around (lon, lat) in radians (planet frame), radius in degrees. */
	double LandAround(double Lon, double Lat, double RadDeg) const;
	double LandAt(double Lon, double Lat) const { return HasGrids() ? SampleGrid(GLand, Lon, Lat) : 0.0; }
	double MoistAt(double Lon, double Lat) const { return HasGrids() ? SampleGrid(GMoist, Lon, Lat) : 0.5; }
	bool HasGrids() const { return GW > 0; }

	/** Static cell-scale noise texture (see the header comment). */
	static void BuildFine(int32 W, int32 H, uint32 Seed, TArray<FColor>& Out);

	/** Transient linear texture (wrap in U, clamp in V); bMips: a full hand-built mip chain. */
	static UTexture2D* MakeTexture(UObject* Outer, const TArray<FColor>& Pixels, int32 W, int32 H, const TCHAR* Name, bool bMips);
	/** Transient half-float RGBA texture, one mip (wrap in U, clamp in V). */
	static UTexture2D* MakeFloatTexture(UObject* Outer, const TArray<FFloat16Color>& Pixels, int32 W, int32 H, const TCHAR* Name);
	/** Replace mip 0 of a one-mip texture: Bytes (W * H * BytesPerPixel, from FMemory::Malloc) is handed to the
	 *  render thread and freed there (no copy on the game thread). */
	static void UploadMip0(UTexture2D* Tex, void* Bytes, int32 W, int32 H, int32 BytesPerPixel);
	/** PNG preview: cover in white, cirrus in pale blue over land / sea (2048 x 1024, mirrored to read like the globe). */
	bool SavePreview(const FString& Path, const TArray<FColor>& Cover, const TArray<FFloat16Color>& Warp, int32 W, int32 H) const;

	mutable double LastBuildSeconds = 0.0;

private:
	int32 GW = 0, GH = 0;
	TArray<float> GMoist, GTemp, GLand, GLandEast, GElev;
	float SampleGrid(const TArray<float>& G, double Lon, double Lat) const;
};

namespace DmSkyNoise
{
	UVolumeTexture* MakeShapeVolume(UObject* Outer, int32 Size, uint32 Seed);
	UVolumeTexture* MakeDetailVolume(UObject* Outer, int32 Size, uint32 Seed);
	/** fBm of the engine Perlin noise, roughly in [-1, 1]. */
	double Fbm(const FVector& P, int32 Oct, double Lac = 2.03, double Gain = 0.5);
}
