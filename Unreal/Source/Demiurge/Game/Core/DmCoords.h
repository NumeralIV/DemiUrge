// DEMIURGE — Game/Core/DmCoords.h : world scale and coordinates (SPEC-UNREAL section 1, binding).
// Owner: C0 bridge.
//
//   Planet datum radius R_uu = 100 km = 1.0e7 uu (the sim's kPlanetR = 100 maps to it).
//   Elevation: 0.2 km (2.0e4 uu) per sim elevation unit (relief exaggerated about 10x).
//   Sim direction (x, y, z) with +Y north  ->  Unreal (x, z, y) with +Z north (planet frame).
//   Latitude = asin(sim y), longitude = atan2(sim z, sim x) (as dm::Geo::lat / lon), in degrees here.
//
// "Planet frame" = the planet actor's local space (origin at the planet centre). The planet turns with
// sim time, so world space = UDmSimSubsystem::GetPlanetToWorld() applied to planet-frame positions.
#pragma once

#include "CoreMinimal.h"
#include "Sim/State.h"

namespace DmCoords
{
	inline constexpr double RadiusUU = 1.0e7;        // datum radius (100 km)
	inline constexpr double UUPerElev = 2.0e4;       // 0.2 km per elevation unit
	inline constexpr double UUPerKm = 1.0e5;
	inline constexpr double RadiusKm = 100.0;

	/** Sim direction -> planet-frame Unreal direction (x, z, y). */
	inline FVector SimToUE(double X, double Y, double Z) { return FVector(X, Z, Y); }
	inline FVector SimToUE(const dm::Vec3& V) { return FVector(V.x, V.z, V.y); }
	/** Planet-frame Unreal direction -> sim direction. */
	inline dm::Vec3 UEToSim(const FVector& V) { return dm::Vec3{ V.X, V.Z, V.Y }; }

	/** Radius in uu of a sim radius-relative height h (elev, or seaLevel): R_uu + h * 0.2 km. */
	inline double HeightToRadiusUU(double H) { return RadiusUU + H * UUPerElev; }

	/** Unit planet-frame direction of a cell (zero vector for an invalid cell). */
	inline FVector CellDir(const dm::World& W, int32 Cell)
	{
		if (!W.geo || Cell < 0 || Cell >= W.n) return FVector::ZeroVector;
		const float* P = &W.geo->pos[size_t(Cell) * 3];
		return SimToUE(P[0], P[1], P[2]).GetSafeNormal();
	}

	/** Height (sim units) of what you would stand or float on: sea level on sea, elev + water on land. */
	inline double SurfaceHeight(const dm::World& W, int32 Cell)
	{
		if (Cell < 0 || Cell >= W.n) return 0.0;
		return dm::state::surfaceR(W, Cell) - dm::kPlanetR;
	}

	/** Planet-frame position of a cell's surface (standing / floating point). */
	inline FVector CellSurfacePos(const dm::World& W, int32 Cell)
	{
		return CellDir(W, Cell) * HeightToRadiusUU(SurfaceHeight(W, Cell));
	}

	/** Planet-frame position of a cell's ground (sea floor on sea cells). */
	inline FVector CellGroundPos(const dm::World& W, int32 Cell)
	{
		if (Cell < 0 || Cell >= W.n) return FVector::ZeroVector;
		return CellDir(W, Cell) * HeightToRadiusUU(W.elev[size_t(Cell)]);
	}

	/** Degrees -> planet-frame unit direction. */
	inline FVector LatLonToDir(double LatDeg, double LonDeg)
	{
		const double La = FMath::DegreesToRadians(LatDeg), Lo = FMath::DegreesToRadians(LonDeg);
		return SimToUE(FMath::Cos(La) * FMath::Cos(Lo), FMath::Sin(La), FMath::Cos(La) * FMath::Sin(Lo));
	}

	/** Planet-frame direction -> latitude / longitude in degrees. */
	inline void DirToLatLon(const FVector& Dir, double& OutLatDeg, double& OutLonDeg)
	{
		const FVector D = Dir.GetSafeNormal();
		const dm::Vec3 S = UEToSim(D);
		OutLatDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(S.y, -1.0, 1.0)));
		OutLonDeg = FMath::RadiansToDegrees(FMath::Atan2(S.z, S.x));
	}

	/** Local frame at a planet-frame direction, the same as dm::v3::basis mapped to Unreal: North =
	 *  toward +Z (increasing latitude), East = to the right of North seen from outside the planet
	 *  (compass headings turn clockwise). Note: the sim's longitude (atan2(z, x)) INCREASES WESTWARD
	 *  (seen from outside, with north up, a larger longitude is to the left), while MapImg maps draw
	 *  longitude increasing to the right: maps are mirror images of the globe. At the poles East = +X. */
	inline void Basis(const FVector& Up, FVector& OutEast, FVector& OutNorth)
	{
		const FVector N = Up.GetSafeNormal();
		FVector E = FVector::CrossProduct(N, FVector::UpVector);
		if (E.SizeSquared() < 1e-12) E = FVector(1, 0, 0);
		E.Normalize();
		OutEast = E;
		OutNorth = FVector::CrossProduct(E, N).GetSafeNormal();
	}
}
