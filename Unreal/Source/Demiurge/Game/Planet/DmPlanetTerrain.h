// DEMIURGE — Game/Planet/DmPlanetTerrain.h : the planet surface as a pure function of direction.
// Owner: C1 planet. Port of UnrealLab/SurfaceLab SurfaceTerrain (docs/lab/surface.md section 3), fed
// from the live dm::World instead of a lab file.
//
// FDmGeoLocator   : finds the sim triangle (and the triangle of a finer recursive grid) containing a
//                   direction, with barycentric weights. Built once per world geometry.
// FDmPlanetTerrain: an IMMUTABLE snapshot of everything the mesh depends on (elevation, lakes, slope,
//                   sea level, rivers), so patch builds on worker threads never read the live world.
//                   Height(dir, spacing) is the single source of truth for every LOD level: two patches
//                   that evaluate the same direction at the same spacing get bit-identical heights, and
//                   coarser levels only drop the octaves they cannot resolve (the material adds them back
//                   as normals). Heights are metres above the DATUM radius (100 km); the sea surface is
//                   at SeaM = seaLevel * 200 m (DmCoords: 0.2 km per elevation unit).
//
// Frames: the locator works in the sim frame (+Y north); the noise in the Unreal planet frame (+Z north,
// DmCoords::SimToUE), because that is the frame the material sees.
#pragma once

#include "CoreMinimal.h"

namespace dm { struct World; }

class FDmGeoLocator
{
public:
	/** Copies the grid of W (positions, triangles). False if the world has no usable geometry. */
	bool Build(const dm::World& W);

	/** Level-L sim triangle containing the unit sim-frame direction P: corner cells and barycentrics. */
	void Locate(const FVector3d& P, int32 C[3], double Wt[3]) const;
	/** Also the triangle of a finer grid FineTris (Extra levels below L, recursive child order). */
	void LocateFine(const FVector3d& P, const TArray<uint32>& FineTris, int32 Extra, int32 C[3], double Wt[3], int32 Cf[3], double Wf[3], FVector3d* OutFineCorners = nullptr) const;
	/** Nearest of the three corners. */
	int32 NearestCell(const FVector3d& P) const;

	int32 L = 0, N = 0, NTri = 0;
	TArray<FVector3d> Pos;        // sim frame, unit
	TArray<uint32> Tris;          // level-L triangles (dm::Geo::tris)
	TArray<int32> Nbr;            // 6 per cell, -1 padded
	TArray<uint8> NbrN;
	FVector3d Ico[12];
	static const int32 IcoTris[60];
	int32 CheckMismatches = 0, CheckCount = 0;   // locator vs geo triangle numbering
	double IcoMaxErr = 0.0;
};

/** Per-vertex attributes besides height (all smooth fields). */
struct FDmSurfAttr
{
	float ReliefE = 0.f;      // m, amplitude of the eroded relief fractal
	float ReliefR = 0.f;      // m, amplitude of the ridged relief fractal
	float RiverD = 2000.f;    // signed m to the nearest river centreline (clamped to +-2000)
	float RiverW = 0.f;       // half-width of that river, m
	float BeachZone = 0.f;    // 1 where the smooth base height is near sea level (coastal plain)
	float DH = 0.f;           // m, relief above the smooth base (snow lapse in the material)
	float AboveLake = 1.0e4f; // m above the nearest lake surface (1e4 = no lake nearby)
	float LakeM = -1.0e9f;    // m above datum of the lake surface here (-1e9 = none)
	double BaseH = 0.0;       // m above datum, smooth height before noise
};

struct FDmRiverPt { FVector3d P; float HalfW; };

class FDmPlanetTerrain
{
public:
	/** Game thread: copy what the mesh needs from the world. */
	void Capture(const dm::World& W, const TSharedPtr<const FDmGeoLocator, ESPMode::ThreadSafe>& InLoc);
	/** Any thread: smoothing, Loop subdivision of the base grid, lakes, rivers. */
	void Prepare();

	/** Height above datum (m) of the unit sim-frame direction Dir, band-limited for vertex spacing S (m). */
	double Height(const FVector3d& DirSim, double S, FDmSurfAttr* Attr = nullptr, bool bRivers = true) const;
	/** True if a river may influence anything inside the cap (Dir, angular radius Ang). */
	bool RiversNear(const FVector3d& DirSim, double Ang) const;
	void RiverQuery(const FVector3d& DirSim, double& OutDistM, double& OutHalfW, double* OutSigned = nullptr) const;
	/** Roads: true if a road segment may be within the cap; signed distance (m) across the nearest segment
	 *  and its level (sim road value). OutSigned = 1e9 when there is none within ~1.2 km. */
	bool RoadsNear(const FVector3d& DirSim, double Ang) const;
	void RoadQuery(const FVector3d& DirSim, double& OutSigned, double& OutLevel) const;
	/** Lake surface (m above datum) at a direction, or -1e9. */
	double LakeAt(const FVector3d& DirSim) const;

	/** Cells whose captured inputs differ from Other in a way the mesh shows (elevation, slope, a lake's
	 *  presence or level); cells where only a road changed go to OutRoadOnly when given (else to Out).
	 *  False if a global quantity changed (sea level, grid, river set), meaning "everything". */
	bool ChangedCells(const FDmPlanetTerrain& Other, TArray<int32>& Out, TArray<int32>* OutRoadOnly = nullptr) const;

	static constexpr double DatumM = 100000.0;  // 100 km
	static constexpr double MPerElev = 200.0;   // 0.2 km per sim elevation unit
	static constexpr double RiverGridCell = 0.012;  // unit-sphere units (1.2 km)

	TSharedPtr<const FDmGeoLocator, ESPMode::ThreadSafe> Loc;
	int32 Version = 0;
	double SeaLevel = 0.0;     // sim units
	double SeaM = 0.0;         // m above datum
	double RiverT = 5.0;
	double MaxH = 0.0, MinH = 0.0;   // rough bounds (m above datum) for culling

	// captured inputs
	TArray<float> Elev, Water, Slope, Flow;
	TArray<uint8> Road;                       // per cell (sim road level)
	struct FRoadSeg { FVector3d A, B; float Level; };
	TArray<FRoadSeg> RoadSegs;                // sim frame, smoothed cell-to-cell segments
	TMap<FIntVector, TArray<int32>> RoadGrid;
	TArray<TArray<int32>> RiverCells;
	TArray<TArray<float>> RiverFlow;
	uint32 RiverHash = 0;
	TArray<uint32> RiverHashes;               // per river (cells + quantised flows)
	float RiverTqM = 0.f;                     // river threshold, quantised (a change redraws every river)

	// derived
	TArray<float> ElevS;        // smoothed elevation per cell
	TArray<uint32> Tris2;       // level L+2 triangles (recursive order)
	TArray<float> Elev2;        // Loop-subdivided elevation (level L+2 vertices)
	TArray<FVector3f> Grad2;    // its gradient per vertex (elevation units per unit of direction, tangent)
	TArray<float> LakeLevelM;   // per cell: lake surface m above datum, or -1e9
	TArray<float> LakeMask;     // per cell, 1 on lake cells, smoothed once
	TArray<float> LakeNearM;    // per cell: lake level of the cell or of its nearest lake neighbour (first ring), or -1e9
	TArray<FDmRiverPt> RPts;    // dense river polylines, chains back to back
	TArray<int32> RSeg;         // segment i runs RPts[RSeg[i]] -> RPts[RSeg[i] + 1]
	TMap<FIntVector, TArray<int32>> RGrid;
	int32 NumChains = 0, NumDeltaBranches = 0;
	double PrepareMs = 0.0;

	double MapElev(double ERel) const;

private:
	void BuildRivers();
	void AddChain(TArray<FVector3d>& Ctrl, TArray<double>& CFlow, int32 Chain, double WidthScale, double MeanderScale, TArray<int32>* CellPtOut, const TArray<int32>* Cells);
};
