// DEMIURGE — Game/Audio/DmAudio.h : the audio API for every other client module (SPEC-UI 7 Audio.*).
// Owner: C9 audio.
//
//   #include "Game/Audio/DmAudio.h"
//   FDmAudio::Play(this, TEXT("click"));                    // UI cue (hover, click, arm, deny, unlock)
//   FDmAudio::Play(this, TEXT("thunder"), Cell, 0.8f);       // placed at a cell: volume / pan / muffle from the camera
//   FDmAudio::Stinger(this, TEXT("miracle"));
//   FDmAudio::ApplySettings(this, Settings);                 // dm::save::UserSettings (C8 screens, on change)
//   FDmAudio::Suspend(this, true);                           // pause menu, window hidden
//
// Every call is safe at any time: before the audio part exists, with -nosound, with no world. Unknown
// cue names are ignored (as the old Audio.play). The audio director (scene part "audio", kind Audio)
// already maps every sim event and UI event to its cue, so most modules never need to call this; the
// HUD calls Play("hover") itself, since hovering is not a sim event.
#pragma once

#include "CoreMinimal.h"

class UObject;
namespace dm { namespace save { struct UserSettings; } }

class DEMIURGE_API FDmAudio
{
public:
	/** One-shot cue by its SPEC-UI 7 name; Cell = -1 plays it unplaced (UI). Returns false if ignored. */
	static bool Play(const UObject* WorldContext, const TCHAR* Name, int32 Cell = -1, float Vol = 1.f);
	/** "age", "founding", "miracle", "wrath", "ending-<mood>". */
	static bool Stinger(const UObject* WorldContext, const TCHAR* Name);
	static void ApplySettings(const UObject* WorldContext, const dm::save::UserSettings& S);
	static void SetVolumes(const UObject* WorldContext, float Master, float Music, float Sfx, float Ambience);
	static void Suspend(const UObject* WorldContext, bool bSuspend);
};
