// DEMIURGE — Game/Core/DmPicking.h : the cell picking service (screen point / ray -> cell).
// Owner: C0 bridge. Used through UDmSimSubsystem::PickScreen / PickRay (input, HUD tooltips, brush).
//
// Picks against the simulation's own surface: one vertex per cell at its surface height (sea level
// on sea, elev + water on land; DmCoords: 0.2 km per elevation unit), linearly interpolated over the
// sim triangles, so a pick returns the cell nearest to the point the player sees, independent of the
// detail a renderer adds. The ray is marched through the shell between the lowest and highest
// surface with Lipschitz-safe steps (never stepping over a ridge, even at grazing angles near the
// ground) and refined by bisection.
#pragma once

#include "CoreMinimal.h"

namespace dm { struct World; }

struct DEMIURGE_API FDmPick
{
	int32 Cell = -1;               // -1 = missed the planet
	bool bSea = false;
	FVector WorldPos = FVector::ZeroVector;    // hit point, world space
	FVector PlanetDir = FVector::ZeroVector;   // unit direction of the hit, planet frame
	double Distance = 0;           // from the ray origin, uu
	double LatDeg = 0, LonDeg = 0;
};

class DEMIURGE_API FDmPicker
{
public:
	/** Recompute the shell bounds (call when elev / water / seaLevel changed: dirty.elev or dirty.water). */
	void Rebuild(const dm::World& W);
	/** Ray in planet-frame uu (origin, unit direction). */
	bool PickPlanetRay(const dm::World& W, const FVector& Origin, const FVector& Dir, FDmPick& Out) const;
	double MinRadiusUU() const { return MinR; }
	/** Radius (uu) of the pick surface along a planet-frame direction; OutCell = nearest cell. */
	double SurfaceRadius(const dm::World& W, const FVector& PlanetDir, int32& OutCell) const { return SurfaceRadiusAt(W, PlanetDir * DmPickUnit, OutCell); }
	static constexpr double DmPickUnit = 1.0e7;
	double MaxRadiusUU() const { return MaxR; }

private:
	double SurfaceRadiusAt(const dm::World& W, const FVector& P, int32& InOutCell) const;
	double MinR = 0, MaxR = 0;
	double Lipschitz = 1;   // bound on d(height above surface)/d(distance along any ray)
	bool bValid = false;
};
