// DEMIURGE — Game/Nature/DmNatureGround.h : where the visible ground is, for everything nature stands on (C4).
//
// Trees must stand on the surface the planet module DRAWS, not on the sim's coarse triangles (the planet adds
// relief of tens to hundreds of metres). Three sources, best first:
//   1. An external height function registered by the planet module or the integration engineer:
//        FDmNatureGround::SetProvider([](const FVector& PlanetDir, FVector* OutNormal) { return RadiusUU; });
//      PlanetDir is a unit planet-frame direction; return the ground radius in uu (planet frame, centre at 0)
//      and optionally the ground normal. Must be thread-safe (the scatter calls it from worker threads) and
//      pure (same answer for the same direction). One line for C1/C10: register FSurfaceTerrain::Height.
//   2. The real planet (C1 ADmPlanet, Game/Planet): its own surface function FDmPlanetTerrain::Height
//      is evaluated on a private snapshot captured from the world (bit-identical to the rendered ground at
//      full detail, rivers and lakes count as water), rebuilt when elevation or water changes.
//   3. The bridge's placeholder planet (ADmPlaceholderPlanet): its land mesh is read back and ray-cast
//      exactly (cube-map triangle buckets, ~0.3 s to build for 1.3 M triangles).
//   4. The sim surface: elevation interpolated linearly over the sim triangles (always available).
// A ground snapshot is immutable once built and shared with the worker tasks (TSharedPtr, thread-safe).
#pragma once

#include "CoreMinimal.h"
#include <atomic>
#include <memory>

namespace dm { struct World; class Geo; }
class FDmPlanetTerrain;

using FDmNatureGroundFn = TFunction<double(const FVector& PlanetDir, FVector* OutNormal)>;

/** Cube-sphere addressing shared by the ground buckets and the nature tiles (equal-angle mapping). */
namespace DmCube
{
	/** Face 0..5 (+X, -X, +Y, -Y, +Z, -Z) and its axes N (normal), U, V. */
	DEMIURGE_API void FaceAxes(int32 Face, FVector& N, FVector& U, FVector& V);
	/** Direction -> face and equal-angle coordinates u, v in [-1, 1]. */
	DEMIURGE_API int32 ToFace(const FVector& Dir, double& OutU, double& OutV);
	/** Face + (u, v) in [-1, 1] -> unit direction. */
	DEMIURGE_API FVector FromFace(int32 Face, double U, double V);
	/** Equal-angle coordinates of a direction on a given face (valid when Dir . N > 0). */
	DEMIURGE_API bool OnFace(int32 Face, const FVector& Dir, double& OutU, double& OutV);
}

/** Linear interpolation over the sim's triangles: which 3 cells and weights hold a direction. */
struct DEMIURGE_API FDmCellInterp
{
	int32 C[3] = { 0, 0, 0 };
	float W[3] = { 1.f, 0.f, 0.f };
	/** Thread-safe (Geo::cellAt is const and uses no scratch). */
	static FDmCellInterp At(const dm::Geo& G, const FVector& PlanetDir, int32 Hint = -1);
};

class DEMIURGE_API FDmNatureGround
{
public:
	enum class ESource : uint8 { Provider, PlanetTerrain, PlaceholderMesh, SimSurface };

	static void SetProvider(FDmNatureGroundFn Fn);
	static void ClearProvider();
	static bool HasProvider();

	/** Ground radius (uu, planet frame) along a unit planet-frame direction, and the ground normal. Thread-safe. */
	double Radius(const FVector& Dir, FVector* OutNormal = nullptr) const;
	ESource GetSource() const { return Source; }
	const TCHAR* SourceName() const;
	double SeaRadiusUU = 1.0e7;    // sea level radius (uu)
	double BuildMs = 0;
	int32 Triangles = 0;

	/** From the sim's elevation (always works). Copies what it needs. */
	static TSharedPtr<const FDmNatureGround, ESPMode::ThreadSafe> FromSim(const dm::World& W);
	/** From a land mesh in planet space (copied). Falls back to the sim surface for directions it misses. */
	static TSharedPtr<const FDmNatureGround, ESPMode::ThreadSafe> FromMesh(const dm::World& W, TArray<FVector3f>&& Verts, TArray<uint32>&& Tris);
	/** Same, on a shell made by FromSim on the game thread (the bucket build can then run on a worker). */
	static TSharedPtr<const FDmNatureGround, ESPMode::ThreadSafe> FromMeshShell(TSharedPtr<FDmNatureGround, ESPMode::ThreadSafe> Shell, TArray<FVector3f>&& Verts, TArray<uint32>&& Tris);
	/** Game thread: a shell that captures the world for the planet's surface function; FinishPlanet (any
	 *  thread) prepares it (smoothing, subdivision, lakes, rivers: ~0.1-0.5 s). */
	static TSharedPtr<FDmNatureGround, ESPMode::ThreadSafe> PlanetShell(const dm::World& W);
	static TSharedPtr<const FDmNatureGround, ESPMode::ThreadSafe> FinishPlanet(TSharedPtr<FDmNatureGround, ESPMode::ThreadSafe> Shell);
	/** The registered provider (sea level from the world). */
	static TSharedPtr<const FDmNatureGround, ESPMode::ThreadSafe> FromProvider(const dm::World& W);

	int64 MeshMisses() const { return Misses.load(); }

private:
	double SimRadius(const FVector& Dir, FVector* OutNormal) const;
	bool MeshRadius(const FVector& Dir, double& OutR, FVector* OutNormal) const;

	ESource Source = ESource::SimSurface;
	std::shared_ptr<const dm::Geo> Geo;
	TArray<float> Elev;            // sim elevation per cell (smoothed like nothing: the sim surface)
	// mesh buckets (CSR over 6 * G * G cube cells)
	int32 G = 0;
	TArray<FVector3f> V;
	TArray<uint32> T;              // 3 per triangle
	TArray<uint32> Start;          // 6*G*G + 1
	TArray<uint32> Items;          // triangle ids
	FDmNatureGroundFn Provider;
	TSharedPtr<FDmPlanetTerrain, ESPMode::ThreadSafe> PTerr;
	double PlanetRadius(const FVector& Dir, FVector* OutNormal) const;
	mutable std::atomic<int64> Misses{ 0 };
};

using FDmNatureGroundPtr = TSharedPtr<const FDmNatureGround, ESPMode::ThreadSafe>;
