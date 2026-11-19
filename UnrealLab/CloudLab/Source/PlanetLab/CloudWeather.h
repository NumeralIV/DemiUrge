// CloudLab: weather-driven cloud cover for a small planet.
// FCloudWeather builds an equirectangular "weather map" (RGBA8) from the planet's climate fields:
//   R = cloud cover (0..1)        G = cloud-top height in the layer (0 = low deck, 1 = full layer)
//   B = cumuliform (0 = stratiform sheet, 1 = heaped convective)   A = extra optical thickness (storm cores)
// sampled in the volumetric cloud material by turning the sample's world position into lat/lon.
// CloudNoise builds the tileable 3D noise volumes (Perlin-Worley shape, Worley detail) the material erodes with.
#pragma once
#include "CoreMinimal.h"

struct FLabPlanetData;
class UTexture2D;
class UVolumeTexture;

struct FCloudCyclone
{
	FVector Center = FVector::ZAxisVector; // unit direction (Unreal axes)
	double RadiusRad = 0.1;                 // angular radius of the system
	double Sign = 1.0;                      // +1 northern hemisphere (counter-clockwise), -1 southern
	bool bTropical = false;
	double Phase = 0.0;                     // arm phase (radians)
};

struct FCloudWeatherParams
{
	int32 Width = 4096, Height = 2048;
	uint32 Seed = 7;
	double ItczLatDeg = 5.0;       // mean latitude of the ITCZ (season shifts it)
	double CoverScale = 1.0;       // global multiplier on cover
	int32 ExtratropicalPerHemi = 3;
	double TropicalLatDeg = 17.0;  // where the hurricane spawns (searched over open ocean)
	double TropicalLonDeg = 15.0;  // preferred longitude (nearest open-ocean spot is used)
	double TropicalRadiusDeg = 9.0;
};

class FCloudWeather
{
public:
	void Build(const FLabPlanetData& D, const FCloudWeatherParams& P);
	UTexture2D* MakeTexture(UObject* Outer) const;
	bool SavePng(const FString& Path) const;

	int32 W = 0, H = 0;
	TArray<FColor> Pixels; // BGRA in memory, matches PF_B8G8R8A8
	TArray<FCloudCyclone> Cyclones;
	double BuildSeconds = 0.0;

private:
	// Coarse climate grids (equirectangular), bilinear sampled.
	int32 GW = 0, GH = 0;
	TArray<float> GMoist, GTemp, GLand, GLandEast, GElev;
	void BuildGrids(const FLabPlanetData& D);
	float SampleGrid(const TArray<float>& G, double Lon, double Lat) const;
	double LandAround(double Lon, double Lat, double RadDeg) const;
};

namespace CloudNoise
{
	/** Tileable cloud noise volume. Shape: R = Perlin-Worley, G/B/A = Worley at 2x/4x/8x frequency. */
	UVolumeTexture* MakeShapeVolume(UObject* Outer, int32 Size, uint32 Seed);
	/** Tileable detail volume: R/G/B = Worley at 1x/2x/4x, A = Perlin. */
	UVolumeTexture* MakeDetailVolume(UObject* Outer, int32 Size, uint32 Seed);
}
