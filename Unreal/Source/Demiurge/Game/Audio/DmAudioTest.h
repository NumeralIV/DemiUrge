// DEMIURGE — Game/Audio/DmAudioTest.h : scripted offline renders of the whole mix (old SPEC-UI 7
// Audio.renderTest). Owner: C9 audio.
//
//   FDmAudioRender Out;
//   DmAudioTest::Render(TEXT("coast-day"), 20.0, Out);     // engine at 48 kHz, scripted context and events
//   FDmAudioStats S = DmAudioAnalysis::Analyse(Out.L.GetData(), Out.R.GetData(), Out.L.Num(), Out.Sr);
//
// Scripts: orbit, coast-day, forest-night, city, storm, meteor, age-advance, stone, industry, space (the
// old spec's list), plus genesis-molten, genesis-water, war, and "cue-<name>" / "stinger-<name>" (one
// cue or stinger alone on its bus). Checks() turns the numbers into pass / fail lines.
#pragma once

#include "CoreMinimal.h"
#include "Game/Audio/DmAudioEngine.h"
#include "Game/Audio/DmAudioAnalysis.h"

struct FDmAudioRender
{
	float Sr = 48000.f;
	TArray<float> L, R;
	TArray<FDmAudioTrack> Tracks;
	TArray<FDmAudioMarker> Markers;
	FDmAudioLevels End;          // engine state at the end
	FDmAudioLevels AtMarker[8];  // engine state just before / after scripted moments (script-specific)
	int32 NumAtMarker = 0;
	double RenderSeconds = 0;    // wall time spent inside FDmAudioEngine::Render
	int32 MaxVoices = 0, MaxMusicVoices = 0;
	float MeanMusicVoices = 0.f;
	FString Help;
	uint32 Hash = 0;             // FNV of the 16-bit rendering (determinism)
};

namespace DmAudioTest
{
	/** All script names, in report order. */
	DEMIURGE_API TArray<FString> Scripts();
	DEMIURGE_API bool Render(const FString& Script, double Seconds, FDmAudioRender& Out, uint32 Seed = 7);
	/** Default length of a script (seconds). */
	DEMIURGE_API double DefaultSeconds(const FString& Script);
	DEMIURGE_API TArray<FDmAudioCheck> Checks(const FString& Script, const FDmAudioRender& R, const FDmAudioStats& S);
}
