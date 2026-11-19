// DEMIURGE — Game/Core/DmDirtyRouter.h : fans the sim's dirty flags out to listeners once per frame.
// Owner: C0 bridge.
//
// The sim only ever SETS w.dirty.{elev, color, water, objects, rivers, roads} (SPEC-SIM 0.7). Once
// per frame, after the sim ticked, UDmSimSubsystem calls Flush: the router copies the flags, clears
// them in the world and broadcasts the copy (only when at least one is set). Listeners rebuild only
// what their flags say:
//
//   Sim->Dirty().OnDirty.AddUObject(this, &ADmPlanet::OnSimDirty);   // void OnSimDirty(const FDmDirtyFlags& F)
//   if (F.bElev || F.bWater) RebuildMesh();  if (F.bColor) RecolourVertices();
//
// A listener that subscribes after a world was created calls Sim->Dirty().RequestAll() (or reads the
// world at once): the next flush then reports everything dirty. A new / loaded / fixture world is
// always all-dirty on its first flush. Listeners must not write the world.
#pragma once

#include "CoreMinimal.h"

namespace dm { struct World; }

struct DEMIURGE_API FDmDirtyFlags
{
	bool bElev = false;      // terrain height changed (mesh)
	bool bColor = false;     // per-cell colours (biome, veg, ice, farm, urban, scorch, fire, lava, owner...)
	bool bWater = false;     // sea level, lakes, sea ice
	bool bObjects = false;   // trees / buildings / units must be re-instanced
	bool bRivers = false;    // w.rivers changed
	bool bRoads = false;     // w.road changed
	bool Any() const { return bElev || bColor || bWater || bObjects || bRivers || bRoads; }
	static FDmDirtyFlags All() { FDmDirtyFlags F; F.bElev = F.bColor = F.bWater = F.bObjects = F.bRivers = F.bRoads = true; return F; }
	FString ToString() const;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FDmOnDirty, const FDmDirtyFlags&);

class DEMIURGE_API FDmDirtyRouter
{
public:
	FDmOnDirty OnDirty;

	/** Copy + clear the world's flags (plus any requested ones) and broadcast. Returns what was sent. */
	FDmDirtyFlags Flush(dm::World& W);
	/** Report every flag on the next flush (late listeners, debug). */
	void RequestAll() { Pending = FDmDirtyFlags::All(); }
	/** Count of flushes that broadcast something (tests). */
	int64 Broadcasts() const { return NumBroadcasts; }
	const FDmDirtyFlags& Last() const { return LastSent; }

private:
	FDmDirtyFlags Pending;
	FDmDirtyFlags LastSent;
	int64 NumBroadcasts = 0;
};
