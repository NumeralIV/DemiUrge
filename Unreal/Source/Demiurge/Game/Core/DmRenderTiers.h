// DEMIURGE — Game/Core/DmRenderTiers.h : graphics quality tiers and render-setting helpers (wave3 perf track).
// Owner: perf (lane perf3), with the bridge's Config/*.
//
// The tiers themselves are data: Config/DefaultScalability.ini redefines the engine's scalability levels for this
// game (one 100 km planet seen from orbit to 300 m), so that
//   level 3 (Epic) and 2 (High)  = the cinematic look the owner approved, tuned to hold 60 fps at 1080p on an RTX 3060
//   level 1 (Medium)             = the same look with cheaper reflections / shadows / TSR / post and 62 % resolution
//   level 0 (Low)                = 50 % resolution, Lumen reflections off, cheaper VSM filtering (Lumen GI stays: it
//                                  carries the night cities)
// The settings screen (UDmScreenSubsystem::ApplyGraphics) maps Low / Medium / High to levels 0 / 1 / 2 and Auto to the
// engine's own (Epic on a first run), so every choice there lands on one of these.
//
// Command line (any scenario, the packaged game too): -tier=low|medium|high|epic|0..3 applies that tier to every
// scalability group once the first game world starts. Console: "dm.tier <0..3>" (no argument: print the levels).
#pragma once
#include "CoreMinimal.h"

namespace DmRenderTiers
{
	/** Tier from "low" / "medium" / "high" / "epic" / "0".."3"; -1 when not recognised. */
	DEMIURGE_API int32 ParseTier(const FString& S);

	/** Apply one level to every scalability group (and the matching resolution quality), like the settings screen.
	 *  bTemporary (the -tier= switch): the engine's temporary quality levels, so the tier is not written to the
	 *  player's GameUserSettings at exit and a benchmark never changes the next launch; the settings screen cannot move
	 *  the levels while it is active. */
	DEMIURGE_API void ApplyTier(int32 Tier, bool bTemporary = false);

	/** The tier in force: the lowest level of the scalability groups (0 low .. 3 epic). Modules that scale their own
	 *  budgets (planet shader octaves, cloud samples, tree density) can read it; it changes only on a settings change. */
	DEMIURGE_API int32 GetTier();

	/** "sg levels: res 100 view 3 aa 3 shadow 3 gi 3 refl 3 pp 3 tex 3 fx 3 foliage 3 shading 3". */
	DEMIURGE_API FString Describe();

	/** Set a console variable at its current priority (so a later scalability change can still move it) and return
	 *  the previous value; a cvar that does not exist returns false. */
	DEMIURGE_API bool SetCVarKeepPriority(const FString& Name, const FString& Value, FString* OutPrevious = nullptr);
}
