// DEMIURGE — Game/Towns/DmTownsGround.h : where the drawn ground is, for everything towns put on it (C5).
//
// Buildings, fields and streets must touch the surface the planet module DRAWS, not the sim's coarse
// triangles (the planet adds tens to hundreds of metres of relief). Sources, best first:
//   1. A provider registered by the planet module or the integration engineer (one line for C1 / C10):
//        FDmTownsGround::SetProvider([P](const FVector& Dir) { return P->GroundRadiusUU(Dir); });
//      Dir is a unit planet-frame direction; return the ground radius in uu (planet frame). It is called
//      from worker threads (town planning), so it must be thread-safe and pure.
//   2. The planet module's terrain (C1 ADmPlanet): its immutable FDmPlanetTerrain snapshot is COPIED (plain
//      data, pure Height()), so planning threads evaluate exactly the full-detail ground the planet draws;
//      re-captured when the planet's terrain version changes.
//   3. The bridge's placeholder planet (ADmPlaceholderPlanet): its land mesh is read back once and
//      ray-cast exactly (triangles bucketed by nearest sim cell).
//   4. The sim surface: elevation interpolated linearly over the sim triangles (always available).
// A snapshot is immutable once built and shared with the planning tasks (thread-safe reads).
#pragma once

#include "CoreMinimal.h"
#include <memory>

namespace dm { struct World; class Geo; }
class UWorld;

using FDmTownsGroundFn = TFunction<double(const FVector& PlanetDir)>;

class DEMIURGE_API FDmTownsGround
{
public:
	enum class ESource : uint8 { Provider, PlanetTerrain, PlaceholderMesh, SimSurface };

	static void SetProvider(FDmTownsGroundFn Fn);
	static void ClearProvider();
	static bool HasProvider();

	/** Build the best snapshot available now (game thread). bOutPending: a placeholder planet exists but
	 *  has no mesh yet (try again next frame). */
	static TSharedPtr<const FDmTownsGround, ESPMode::ThreadSafe> Snapshot(UWorld* World, const dm::World& W, bool& bOutPending);
	/** (towns-perf) The same, but when the source is the planet terrain its (expensive, ~10 ms) copy is left to the caller:
	 *  OutDeferred is set to the planet's current snapshot (valid on the game thread now only: read its Loc and Version);
	 *  the caller completes the snapshot with AdoptPlanetTerrain or AdoptPlanetCopy before sharing it. */
	static TSharedPtr<FDmTownsGround, ESPMode::ThreadSafe> SnapshotDeferred(UWorld* World, const dm::World& W, bool& bOutPending, const class FDmPlanetTerrain** OutDeferred);
	/** Completes a deferred snapshot (before it is shared): copies the planet terrain. Thread-safe on an unshared snapshot. */
	void AdoptPlanetCopy(const class FDmPlanetTerrain& Src);
	/** Completes a deferred snapshot with a planet terrain of our own (captured from the world like the planet's, then
	 *  prepared on a worker: identical heights, no copy, no dependency on the planet's snapshot). Unshared snapshot only. */
	void AdoptPlanetTerrain(const TSharedPtr<const class FDmPlanetTerrain, ESPMode::ThreadSafe>& T);
	/** A cheap signature of what Snapshot would read (changes when the planet mesh is rebuilt). */
	static uint64 Signature(UWorld* World);

	/** Ground radius (uu, planet frame) along a unit planet-frame direction. Thread-safe. */
	double Radius(const FVector& Dir) const { return RadiusBand(Dir, 0.5); }
	/** The same, band-limited for samples SpacingM apart (cheaper: the planet terrain skips finer octaves). */
	double RadiusBand(const FVector& Dir, double SpacingM) const;
	/** Ground height in metres above the datum. */
	double HeightM(const FVector& Dir) const { return (Radius(Dir) - 1.0e7) * 0.01; }
	double HeightM(const FVector& Dir, double SpacingM) const { return (RadiusBand(Dir, SpacingM) - 1.0e7) * 0.01; }
	/** (verifier) The planet module's drawn rivers and lakes (always "none" without the planet terrain); thread-safe.
	 *  RiverBankM: metres from a unit planet-frame direction to the nearest river bank (negative inside, 1e9 none).
	 *  LakeSurfaceM: lake surface in metres above the datum there, or -1e9. RiversNear: any river within AngRad. */
	double RiverBankM(const FVector& Dir) const;
	double LakeSurfaceM(const FVector& Dir) const;
	bool RiversNear(const FVector& Dir, double AngRad) const;
	/** River reaches within AngRad of Dir: (A, B) unit planet-frame directions and the half-width (m) of each. */
	void RiverSegments(const FVector& Dir, double AngRad, TArray<FVector>& OutA, TArray<FVector>& OutB, TArray<float>& OutHalfW) const;
	/** Ground normal (planet frame) from 3 samples Step metres apart. */
	FVector Normal(const FVector& Dir, double StepM = 6.0) const;

	ESource GetSource() const { return Source; }
	/** The planet terrain this snapshot evaluates (PlanetTerrain source), else nullptr. */
	const class FDmPlanetTerrain* PlanetTerrain() const { return Planet.Get(); }
	const TCHAR* SourceName() const;
	double SeaRadiusUU = 1.0e7;
	double SeaHeightM() const { return (SeaRadiusUU - 1.0e7) * 0.01; }
	double BuildMs = 0;
	int32 Triangles = 0;
	uint64 Sig = 0;

private:
	double SimRadius(const FVector& Dir) const;
	bool MeshRadius(const FVector& Dir, double& OutR) const;

	ESource Source = ESource::SimSurface;
	std::shared_ptr<const dm::Geo> Geo;
	TArray<float> Elev;            // sim elevation per cell
	TArray<FVector3f> V;           // placeholder land vertices (planet frame, uu)
	TArray<uint32> T;              // 3 per triangle
	TArray<uint32> Start;          // per cell: CSR offsets (n + 1)
	TArray<uint32> Items;          // triangle ids
	FDmTownsGroundFn Provider;
	TSharedPtr<const class FDmPlanetTerrain, ESPMode::ThreadSafe> Planet;
};

using FDmTownsGroundPtr = TSharedPtr<const FDmTownsGround, ESPMode::ThreadSafe>;
