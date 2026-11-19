// DEMIURGE surface lab: the planet surface as a pure function of direction.
// Height(dir, spacing) is the single source of truth for every LOD level: any two patches that
// evaluate the same direction at the same level get bit-identical heights, and coarser levels
// only drop the noise octaves they cannot resolve (the material adds those back as normals).
#pragma once
#include "CoreMinimal.h"

/** Per-vertex attributes besides height (all smooth fields, no high-frequency noise). */
struct FSurfAttr
{
	FVector3f Albedo = FVector3f(0.5f); // sRGB 0..1, vegetation / soil / sand macro colour
	float Forest = 0.f;    // canopy density 0..1
	float Lights = 0.f;    // city-light mask 0..1
	float Wet = 0.f;       // marsh / lake wetness 0..1
	float ReliefE = 0.f;   // m, amplitude of the eroded relief fractal at this point
	float ReliefR = 0.f;   // m, amplitude of the ridged relief fractal
	float Snow = 0.f;      // climate snow factor 0..1 (before slope rules)
	float RiverD = 2000.f; // signed m to the nearest river centreline (clamped to +-2000)
	float RiverW = 0.f;    // half-width of that river, m
	float Arid = 0.f;      // 0 lush .. 1 desert (for dunes / soil in the material)
	float BeachZone = 0.f; // 1 where the smooth base height is near sea level (coastal plain), 0 inland
	double BaseH = 0.0;    // m, smooth height before noise
};

struct FRiverPt { FVector3d P; float HalfW; };

class FSurfaceTerrain
{
public:
	bool Load(const FString& Path);
	void Prepare(); // smoothing, gradients, rivers

	/** Height above datum (m) of the unit direction Dir (sim frame: +Y north), band-limited for vertex spacing S (m). */
	double Height(const FVector3d& Dir, double S, FSurfAttr* Attr = nullptr, bool bRivers = true) const;

	/** True if a river may influence anything inside the cap (Dir, angular radius Ang). */
	bool RiversNear(const FVector3d& Dir, double Ang) const;

	double SeaM() const { return 0.0; } // sea level in metres above datum (sim sea level maps to 0)

	// Simulation data (sim frame)
	int32 N = 0, NTri = 0;
	float SimR = 100.f, SeaLevel = 0.f, RiverT = 5.f;
	TArray<FVector3f> Pos;
	TArray<uint32> Tris;
	TArray<float> Elev, Temp, Moist, Veg, Flow, Ice, Hab, Slope, Water;
	TArray<uint8> Biome;

	// Derived
	TArray<float> Marsh; // 1 on wetland cells, smoothed
	TArray<FVector3d> Grad; // tangent gradient of Elev per cell (elev units per unit-sphere distance)
	TArray<TArray<int32>> Nbr;
	TArray<FRiverPt> RPts;          // dense river polylines, chains stored back to back
	TArray<int32> RSeg;             // segment i runs RPts[RSeg[i]] -> RPts[RSeg[i]+1]
	TMap<FIntVector, TArray<int32>> RGrid;
	double RGridCell = 0.012;       // unit-sphere units (1.2 km)
	int32 NumRiverCells = 0, NumChains = 0;

	static constexpr double DatumM = 100000.0; // 100 km
	static constexpr double MPerElev = 200.0;  // 0.2 km per sim elevation unit

	// Icosahedron (identical to js/core/geo.js level 0)
	FVector3d Ico[12];
	static const int32 IcoTris[60];

	/** Level-6 sim triangle containing Dir; Corner ids and gnomonic barycentrics. */
	void Locate(const FVector3d& Dir, int32 Out[3], double W[3], int32* Out8 = nullptr, double* W8 = nullptr) const;
	TArray<uint32> Tris8; // level-8 triangles in recursive order (Loop-subdivided base grid)
	TArray<float> Elev8;

	void RiverQuery(const FVector3d& Dir, double& OutDistM, double& OutHalfW, double* OutSigned = nullptr) const;

private:
	void BuildRivers();
	double MapElev(double E) const;
};
