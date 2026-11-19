// DEMIURGE — Game/Core/DmDirtyRouter.cpp : see DmDirtyRouter.h. Owner: C0 bridge.

#include "Game/Core/DmDirtyRouter.h"
#include "Sim/State.h"

FString FDmDirtyFlags::ToString() const
{
	FString S;
	auto Add = [&S](bool bOn, const TCHAR* Name) { if (bOn) { if (!S.IsEmpty()) S += TEXT(","); S += Name; } };
	Add(bElev, TEXT("elev")); Add(bColor, TEXT("color")); Add(bWater, TEXT("water"));
	Add(bObjects, TEXT("objects")); Add(bRivers, TEXT("rivers")); Add(bRoads, TEXT("roads"));
	return S.IsEmpty() ? FString(TEXT("none")) : S;
}

FDmDirtyFlags FDmDirtyRouter::Flush(dm::World& W)
{
	FDmDirtyFlags F = Pending;
	Pending = FDmDirtyFlags();
	F.bElev |= W.dirty.elev;
	F.bColor |= W.dirty.color;
	F.bWater |= W.dirty.water;
	F.bObjects |= W.dirty.objects;
	F.bRivers |= W.dirty.rivers;
	F.bRoads |= W.dirty.roads;
	W.dirty = dm::Dirty{ false, false, false, false, false, false };
	if (F.Any())
	{
		LastSent = F;
		++NumBroadcasts;
		OnDirty.Broadcast(F);
	}
	return F;
}
