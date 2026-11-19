// DEMIURGE — Game/UI/Screens/DmScreenTypes.h : types shared by the screens module (SPEC-UI 5 and 6).
// Owner: C8 screens. Nothing here depends on Slate, so the game flow (C10) and the audio and HUD
// modules may include it freely.
#pragma once

#include "CoreMinimal.h"

/** The screens of SPEC-UI 5. The timelapse is an overlay on the planet; the rest are panels. */
enum class EDmScreen : uint8
{
	None,
	Title,
	Setup,
	Pause,
	Settings,
	Help,
	Save,        // write a slot (in game)
	Load,        // read a slot (title or pause)
	Ending,
	Timelapse,
	Card,
};

/** The calamity choices of the setup screen (SPEC-UI 5 setup(): calm / normal / wild). */
enum class EDmCalamity : uint8
{
	Calm,
	Normal,
	Wild,
};

/** What the setup screen asks for. */
struct FDmNewWorldRequest
{
	FString Name;                        // empty: the game names the world
	uint32 Seed = 0;                     // used when bRandomSeed is false
	bool bRandomSeed = true;             // the seed box was empty
	bool bQuickStart = false;            // straight to the first tribe (genesis skipped)
	EDmCalamity Calamity = EDmCalamity::Normal;

	/** The value for dm::World::settings.calamity (natural-disaster frequency; 0 = off). */
	double CalamityMultiplier() const
	{
		switch (Calamity)
		{
		case EDmCalamity::Calm: return 0.3;
		case EDmCalamity::Wild: return 2.5;
		default: return 1.0;
		}
	}
};

/** Game-flow notifications. The subsystem does the default action; the game flow (C10) listens. */
enum class EDmFlow : uint8
{
	NewWorld,       // a world was begun
	Continue,       // the latest save was loaded from the title
	Loaded,         // a slot was loaded
	Saved,          // a slot was written
	Resume,         // the screens closed and the game runs again
	ExitToTitle,    // the world was autosaved and the title shows
	KeepPlaying,    // the ending was dismissed
};
