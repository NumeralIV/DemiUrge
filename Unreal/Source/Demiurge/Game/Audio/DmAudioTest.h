// DEMIURGE — Game/Audio/DmAudioTest.h : scripted offline renders of the whole mix (old SPEC-UI 7
// Audio.renderTest). Owner: C9 audio.
//
//   FDmAudioRender Out;
//   DmAudioTest::Render(TEXT("coast-day"), 20.0, Out);     // engine at 48 kHz, scripted context and events
//   FDmAudioStats S = DmAudioAnalysis::Analyse(Out.L.GetData(), Out.R.GetData(), Out.L.Num(), Out.Sr);
//
// Scripts: orbit, coast-day, forest-night, city, storm, meteor, age-advance, stone, industry, space (the
// old spec's list), genesis-molten, genesis-water, war, the verifier's stress scripts, zoom-sweep, ducking,
// ending-silence, ambience-* (each zoom band alone), biomes (desert-night, tundra, rainforest, wetland-night, mountain),
// music-age-0..6 and music-mood-* (the score alone), "cue-<name>" / "stinger-<name>" (one cue or stinger alone on
// its bus) and inst-* (calibration). Checks() turns sound-direction.md into pass / fail lines.
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
	int32 MaxVoices = 0, MaxMusicVoices = 0, MaxBeds = 0;
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
	/** inst-* scripts: one music note alone, for level calibration (measured, not judged). */
	DEMIURGE_API bool IsCalibration(const FString& Script);
	/** bAcrossSeeds: the suite runs on several seeds, so a single render may sit off its level by the designed per-play
	 *  jitter (+/- 1.5 dB on every one-shot, the generative score's phrases): each render gets that much more room and
	 *  the mean over the seeds is held to the tight tolerance instead (LevelTarget). */
	DEMIURGE_API TArray<FDmAudioCheck> Checks(const FString& Script, const FDmAudioRender& R, const FDmAudioStats& S, bool bAcrossSeeds = false);
	/** The loudness a script is judged by (cues: peak short-term LUFS of 3.2; music: integrated LUFS of 5.1) and the
	 *  tolerance for the mean over several seeds; false when the script has no level target. bShortTerm: compare StMax. */
	DEMIURGE_API bool LevelTarget(const FString& Script, float& Target, float& MeanTol, bool& bShortTerm);
}
