// DEMIURGE ground lab: land use for mid-altitude realism (2-20 km).
// Stand-in for what the simulation provides in production (settlements, a per-cell farm field, roads):
//  - settlements placed on habitable, fertile ground, preferably by rivers and coasts;
//  - a smooth farm-intensity field around them (the material turns it into individual fields);
//  - roads between neighbouring settlements, routed by A* over a fine height grid with a slope cost so
//    they follow valleys and contours, cross rivers rarely, and merge into a network;
//  - a camera-centred "ground tile" (gnomonic projection on the tangent plane at the view target):
//    smooth height (R32F, contour coordinate for fields) and road + river distance fields (RGBA16F).
// All directions here are in the sim frame (+Y north), as FSurfaceTerrain.
#pragma once
#include "CoreMinimal.h"

class FSurfaceTerrain;

struct FSettlement
{
	FVector3d Dir = FVector3d(0, 1, 0);
	double Size = 0.5;    // 0 hamlet .. 1 town
	double FarmR = 2500;  // m, radius of the cultivated land around it
	double CoreR = 300;   // m, radius of the built-up core
};

struct FRoad
{
	TArray<FVector3d> P;  // dense points on the unit sphere (sim frame), ~8 m apart
	TArray<float> HalfW;  // m
};

class FGroundLand
{
public:
	/** Place settlements over the whole planet. */
	void PlaceSettlements(const FSurfaceTerrain& T);
	/** Build the ground tile around CenterDir (sim frame, unit) with half-size HalfM metres. */
	void BuildTile(const FSurfaceTerrain& T, const FVector3d& CenterDir, double HalfM, int32 CoarseN, int32 FineN);

	/** Smooth farm intensity 0..1 and settlement-core density 0..1 at a direction (sim frame). */
	void FarmAt(const FSurfaceTerrain& T, const FVector3d& Dir, double& OutFarm, double& OutCore, double& OutTempC) const;

	// tile frame (sim frame unit vectors), gnomonic: x = R (d.E)/(d.C), y = R (d.N)/(d.C)
	FVector3d C = FVector3d(0, 1, 0), E = FVector3d(1, 0, 0), N = FVector3d(0, 0, 1);
	double Half = 32000.0;
	int32 CN = 0, FN = 0;          // coarse (height) and fine (distance fields) grid sizes
	TArray<float> HC;              // coarse smooth height, m (cell centred, CN x CN)
	TArray<FFloat16Color> Fine;    // R road signed dist, G road unsigned dist, B road half-width, A river distance (m)
	TArray<FRoad> Roads;
	TArray<FSettlement> Sets;
	TMap<FIntVector, TArray<int32>> SetGrid;
	double SetCell = 0.05;         // unit-sphere units (5 km)

	FVector3d TileDir(double X, double Y) const { const FVector3d P = C * 100000.0 + E * X + N * Y; return P / P.Length(); }
	bool TileXY(const FVector3d& D, double& X, double& Y) const
	{
		const double Dc = D | C; if (Dc <= 0.2) return false;
		X = 100000.0 * (D | E) / Dc; Y = 100000.0 * (D | N) / Dc;
		return FMath::Abs(X) < Half && FMath::Abs(Y) < Half;
	}
	double Stats[8] = {};
	int32 NumLinks = 0;
private:
	void RouteRoads(const FSurfaceTerrain& T, const TArray<float>& RivD, const TArray<float>& RivW);
	void RasterFine(const FSurfaceTerrain& T);
};
