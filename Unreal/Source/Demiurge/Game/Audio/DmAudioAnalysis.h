// DEMIURGE — Game/Audio/DmAudioAnalysis.h : numbers and pictures of rendered audio. Owner: C9 audio.
//
// Nobody can listen, so every scenario renders its mix offline and measures it: peak, RMS, the loudest
// and quietest 400 ms windows, clipped samples, NaNs, DC offset, energy per band (sub < 60 Hz, low
// 60-250, mid 250-4k, high > 4k), the harsh band (2.5-6 kHz), spectral centroid, zero crossings,
// silence, crest factor, stereo correlation. Panels (PNG) show the waveform, a spectrogram, the average
// spectrum, named tracks (layer gains over time), event markers, the numbers and the pass / fail checks.
#pragma once

#include "CoreMinimal.h"

struct FDmAudioStats
{
	double Seconds = 0;
	float Peak = 0.f, PeakDb = -200.f;
	float Rms = 0.f, RmsDb = -200.f;
	float LoudestDb = -200.f, QuietestDb = -200.f;   // 400 ms windows
	int64 Clipped = 0;           // |x| >= 0.999
	int64 NaN = 0;
	float Dc = 0.f;              // max |mean| of the two channels
	float Sub = 0.f, Low = 0.f, Mid = 0.f, High = 0.f;   // fractions of the energy above 15 Hz
	float Harsh = 0.f;           // fraction in 2.5 .. 6 kHz
	float Centroid = 0.f;        // Hz
	float ZeroCross = 0.f;       // per second (left)
	float Silent = 0.f;          // fraction of 100 ms windows below -60 dBFS
	float CrestDb = 0.f;
	float Corr = 0.f;            // L/R correlation
	TArray<float> SpecHz, SpecDb;   // average spectrum, log-spaced points
};

struct FDmAudioCheck
{
	FString Name;
	bool bPass = true;
	FString Detail;
};

struct FDmAudioTrack
{
	FString Name;
	FColor Color = FColor::White;
	TArray<float> Values;        // 0..1, evenly spaced over the whole buffer
};

struct FDmAudioMarker
{
	double Time = 0;
	FString Label;
};

namespace DmAudioAnalysis
{
	DEMIURGE_API FDmAudioStats Analyse(const float* L, const float* R, int64 N, float Sr);
	DEMIURGE_API bool WriteWav(const FString& Path, const float* L, const float* R, int64 N, float Sr);
	/** One scenario panel (1600 x 1120 PNG). */
	DEMIURGE_API bool WritePanel(const FString& Path, const FString& Title, const FString& Subtitle, const float* L, const float* R, int64 N, float Sr,
		const FDmAudioStats& S, const TArray<FDmAudioCheck>& Checks, const TArray<FDmAudioTrack>& Tracks, const TArray<FDmAudioMarker>& Markers);

	struct FRow
	{
		FString Name;
		FDmAudioStats Stats;
		bool bPass = true;
		FString Note;
	};
	/** A table of many scenarios (1600 px wide). */
	DEMIURGE_API bool WriteSummary(const FString& Path, const FString& Title, const TArray<FRow>& Rows);
	DEMIURGE_API FString Describe(const FDmAudioStats& S);
}
