// DEMIURGE — Game/Sky/DmSkyStorms.h : the moving weather systems the cloud material draws (C2 sky).
// Owner: C2 sky.
//
// Every system is a centre, an angular radius, a spin sense, a type, an arm phase and a strength. The weather
// map builder (DmSkyWeather) draws a snapshot of them into the cloud-cover and warp maps, which are rebuilt
// every second or so on a worker thread and crossfaded. This class owns and moves them:
//   - climatology: one tropical cyclone per hemisphere (the summer one strong, with an eye; they form over
//     warm open ocean at 10-20 deg, track westward and poleward, recurve eastward past 25 deg and die over
//     land or after a few days) and three extratropical lows per hemisphere (frontal systems: comma head,
//     cold front trailing equatorward, warm-front shield, dry slot, cold-air cumulus; riding the westerlies
//     at 40-60 deg). Each lives a few planet days, fades in and out and respawns elsewhere;
//   - the sim's storms (dm::Disaster type Storm): followed cell by cell (smoothed), strength from the
//     disaster's power, faded out when the disaster ends;
//   - rain (power:used "rain"): a convective cluster (blob) over the brush that holds while the player rains
//     and decays afterwards;
//   - lightning: four flash slots, fed by sim lightning events and by random strokes inside storm cores.
// Time: "sky seconds" (the actor's clock: sim time, 1 planet day = DayLength sky seconds) drive motion and
// lifecycles; real seconds drive smoothing, rain decay and the flash pulses. Own RNG (render code never draws
// from world.rng); deterministic for a seed.
#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

class FDmSkyWeather;
namespace dm { struct World; }

enum class EDmCycloneType : uint8 { Extratropical = 0, Tropical = 1, Blob = 2 };

struct FDmCyclone
{
	double LatDeg = 0, LonDeg = 0;        // centre, planet frame (sim latitude / longitude)
	FVector Dir = FVector::UpVector;      // = DmCoords::LatLonToDir(LatDeg, LonDeg)
	double RadiusRad = 0.15;
	double Sign = 1.0;                    // +1 northern hemisphere, -1 southern
	EDmCycloneType Type = EDmCycloneType::Extratropical;
	double Phase = 0.0;                   // arm phase (radians)
	double Strength = 0.0;                // current 0..1 (what the material sees)
	double Peak = 1.0;
	double Age = 0.0, Life = 1.0;         // climatology: sky seconds
	bool bClimate = false;
	int32 SimId = -1;                     // sim storm: disaster id
	FVector Target = FVector::UpVector;   // sim storm / blob: where it is going (planet frame)
	bool bEnding = false;
	uint32 Id = 0;                        // stable per system (noise offsets)
	double Hold = 0.0;                    // blob: real seconds left at full strength
};

/** What the map builder needs of a system (plain data, copied to the worker thread). */
struct FDmSystemSnap
{
	FVector Dir = FVector::UpVector;      // planet frame
	double RadiusRad = 0.15;
	double Sign = 1.0;
	EDmCycloneType Type = EDmCycloneType::Extratropical;
	double Phase = 0.0;
	double Strength = 0.0;
	uint32 Id = 0;                        // noise offset (stable per system)
};

struct FDmFlash
{
	FVector Dir = FVector::UpVector;      // planet frame
	double T = 1e9;                       // real seconds since the stroke
	double Gain = 0.0;
	double Intensity() const;
};

class FDmSkyStorms
{
public:
	static constexpr int32 FlashSlots = 4;

	/** Fresh climatology for a seed, at staggered ages (so they do not all form at once). */
	void Reset(uint32 Seed, const FDmSkyWeather& Weather, double SunDeclDeg);
	/** Advance. SkyDt: sky seconds; RealDt: real seconds; SkyRate: sky seconds per real second now. */
	void Step(double SkyDt, double RealDt, double SkyRate, const dm::World* W, const FDmSkyWeather& Weather, double SunDeclDeg, double DayLength);
	void AddRain(int32 Cell, double AngRad, const dm::World* W);
	void Strike(const FVector& PlanetDir, double Gain = 1.0);
	/** The visible systems (strength > 0) for the weather map builder. */
	void Snapshot(TArray<struct FDmSystemSnap>& Out) const;
	/** A cheap fingerprint of the snapshot: changes whenever a rebuilt map would look different. */
	uint32 Fingerprint() const;
	/** -cyclone=lat,lon[,radiusDeg]: pin the NH tropical cyclone there (scenarios). */
	void PinTropical(double LatDeg, double LonDeg, double RadiusDeg);

	TArray<FDmCyclone> Systems;
	FDmFlash Flashes[FlashSlots];
	FString Describe() const;

private:
	FRandomStream Rng;
	uint32 NextId = 1;
	bool bPinned = false;
	double PinLat = 0, PinLon = 0, PinRad = 9;
	void Spawn(FDmCyclone& C, bool bTropical, double Hemi, const FDmSkyWeather& Weather, double SunDeclDeg, bool bStagger);
	void AmbientStrikes(double RealDt, double SkyRate);
};
