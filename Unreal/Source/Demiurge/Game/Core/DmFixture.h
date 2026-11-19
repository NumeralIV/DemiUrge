// DEMIURGE — Game/Core/DmFixture.h : a believable, populated world without the full simulation.
// Owner: C0 bridge.
//
// Loads a planet exported from the finished JS terrain / climate / life modules
// (UnrealLab/data/planet_<seed>.lab, format in UnrealLab/export_planet.js: int32 n, int32 nTri,
// f32 R, f32 seaLevel, f32 riverT, pos f32[3n], tris u32[3 nTri], then elev, temp, moist, veg, flow,
// ice, hab, slope, water f32[n] each, biome u8[n]) into a dm::World, derives the remaining fields
// (coast, fresh, down, rivers, fauna, fert, landmass caches) itself, and then generates history that
// follows the habitability field: cultures (one awakened, the rest daughters with related languages,
// regions by travel cost), settlements on the best habitable sites (tiers, population, territory,
// farms, urban cores, buildings by age), relations (contact, trade, one war), roads between
// neighbouring towns, units on real paths (caravans, ships at sea, settlers, an army, a prophet), a
// storm, a chronicle and the god's state. Deterministic for a given (file, options).
//
// The result is a static snapshot (phase History). UDmSimSubsystem::MakeLive() hands it to the real
// sim (sim::use) if a scenario wants time to pass. Nothing here calls a sim module except Names.
#pragma once

#include "CoreMinimal.h"
#include <memory>
#include "Sim/State.h"

struct FDmFixtureOptions
{
	uint32 Seed = 7;                 // history rng seed (the planet comes from the file)
	int32 Age = 3;                   // dm::Age of the leading culture (3 = Sail); others trail by 0..2
	int32 Settlements = 96;          // target live settlements (fewer if the land runs out)
	int32 Cultures = 6;              // 1..6
	int32 Units = 40;                // target units
	double Year = 2240.0;            // world year
	bool bDisasters = true;          // one storm over warm sea
};

struct FDmFixtureReport
{
	int32 Settlements = 0, Cultures = 0, Units = 0, Rivers = 0, RoadCells = 0, Disasters = 0, Chronicle = 0;
	int32 Tiers[5] = { 0, 0, 0, 0, 0 };
	double Pop = 0, MeanHabSettled = 0, MeanHabLand = 0, BuildMs = 0;
	int32 SaveBytes = 0;
	FString Summary;
};

namespace DmFixture
{
	/** "planet_7" (or "7") -> <project ancestors>/UnrealLab/data/planet_7.lab; a path is returned as is.
	 *  Searches ProjectDir and its parents, so lanes (Unreal/Lanes/<key>/) find the same data. */
	DEMIURGE_API FString ResolveLabPath(const FString& NameOrPath);

	/** Builds the fixture world, or nullptr with OutError set. */
	DEMIURGE_API std::unique_ptr<dm::World> Build(const FString& LabPath, const FDmFixtureOptions& Opts, FString& OutError, FDmFixtureReport* OutReport = nullptr);

	/** Contract checks: finite fields, ids == index, ownership, settlements on habitable land, unit
	 *  paths made of neighbours, serialize -> deserialize -> serialize identical. "" when it holds. */
	DEMIURGE_API FString Validate(const dm::World& W);
}
