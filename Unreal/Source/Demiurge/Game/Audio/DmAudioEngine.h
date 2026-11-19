// DEMIURGE — Game/Audio/DmAudioEngine.h : the synthesised mix (SPEC-UNREAL 2 C9, old SPEC-UI 7).
// Owner: C9 audio.
//
// FDmAudioEngine renders the whole game mix procedurally: ambience by zoom and place (space hum,
// wind, surf, open sea, birds by day, crickets by night, rain, fire crackle, lava, town life by age),
// one-shot cues (the binding names of SPEC-UI 7 plus "fall"), and the generative score (DmAudioMusic.h).
// Buses: music, sfx, ambience, ui -> reverb sends -> master (28 Hz high-pass, DC blocker, -1 dBFS
// look-ahead limiter). No assets, no allocation while rendering.
//
// Threads: SetContext / SetVolumes / Play / Stinger / Suspend may be called from any thread (the game
// thread) at any time; Render is called by exactly one consumer (the audio render thread through
// UDmSynthWave, or a scenario rendering offline). Commands are queued in a fixed ring and taken at the
// next block. Everything is deterministic for a given seed and command sequence (offline tests).
#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

enum class EDmCue : uint8
{
	Hover, Click, Arm, Deny, Unlock, Thunder, Impact, Rumble, Wave, Rainfall, Shimmer, Chime, Choir, Horn, Toll, Roar, Crackle,
	Fall,        // meteor / comet falling through the sky (Param = seconds until impact)
	Count
};

enum class EDmStinger : uint8
{
	Age, Founding, Miracle, Wrath,
	EndingBeloved, EndingFeared, EndingForgotten, EndingParting, EndingSilence, EndingAsh, EndingQuiet,
	Count
};

enum class EDmBus : uint8 { Music, Sfx, Ambience, Ui, Count };

/** Ambience layers (diagnostics and tests read their gains). */
enum class EDmLayer : uint8
{
	Space, Wind, Surf, Sea, Birds, Crickets, Rain, Fire, Lava, TownEarly, TownMiddle, TownIndustry, TownModern, Leaves,
	Count
};

namespace DmAudio
{
	DEMIURGE_API const TCHAR* CueName(EDmCue C);
	/** Case-insensitive; the binding names of SPEC-UI 7 and "fall". False for unknown names. */
	DEMIURGE_API bool CueFromName(const FString& Name, EDmCue& Out);
	DEMIURGE_API const TCHAR* StingerName(EDmStinger S);
	/** "age", "founding", "miracle", "wrath", "ending-beloved" ... "ending-silence", "ending-ash", "ending-quiet". */
	DEMIURGE_API bool StingerFromName(const FString& Name, EDmStinger& Out);
	DEMIURGE_API const TCHAR* LayerName(EDmLayer L);
	DEMIURGE_API EDmBus CueBus(EDmCue C);
}

/** What the listener hears from: filled by ADmAudioDirector from the camera and the world each frame,
 *  or by a scripted offline scenario. All fractions are 0..1. */
struct FDmAudioContext
{
	float Altitude01 = 1.f;     // 0 = at the ground, 1 = far orbit (log of the camera altitude)
	float Day = 1.f;            // sunlight at the focus (0 night .. 1 day)
	float Sea = 0.f;            // open water around the focus
	float Coast = 0.f;          // shoreline around the focus
	float Forest = 0.f;         // forest / rainforest / taiga, weighted by vegetation
	float Open = 0.f;           // grassland / savanna / wetland
	float Desert = 0.f;         // desert / barren / beach
	float Cold = 0.f;           // ice / tundra / snow / sea ice
	float Mountain = 0.f;       // rock / snow peaks / steep slopes
	float Town = 0.f;           // settlements and urban land around the focus
	float Fire = 0.f;           // burning cells around the focus
	float Lava = 0.f;           // lava around the focus
	float Storm = 0.f;          // storm cells around the focus
	float Rain = 0.f;           // rain (wet ground, storms, the rain brush)
	int32 Age = 0;              // world age (0 Stone .. 6 Space)
	int32 TownAge = -1;         // age of the nearest settlement's culture (-1 = use Age)
	int32 Phase = 4;            // dm::Phase as int: 0 Molten, 1 Water, 2 Life, 3 People, 4 History, 5 Ended
	float Tension = 0.f;        // war and disaster (darker music)
	bool bPaused = false;
	bool bScore = true;         // false: the generative score is silent (stingers still play)
	uint32 Seed = 1;            // the world's seed: picks the musical scale
};

struct FDmAudioVolumes
{
	float Master = 0.8f, Music = 0.7f, Sfx = 0.9f, Ambience = 0.8f, Ui = 0.9f;
};

struct FDmCueParams
{
	float Vol = 1.f;            // linear gain (already including distance attenuation)
	float Pan = 0.f;            // -1 left .. 1 right
	float Muffle = 0.f;         // 0 = clear .. 1 = far away / behind the planet (darker, softer highs)
	float Param = 0.f;          // cue-specific (Fall: seconds until impact; Impact / Rumble: power 0..1)
	float Delay = 0.f;          // seconds before it starts (sound travelling)
};

/** A snapshot of the engine's state for diagnostics (copied under a lock; cheap). */
struct FDmAudioLevels
{
	float Layer[int32(EDmLayer::Count)] = {};
	int32 Voices = 0, MusicVoices = 0;
	int64 CuesStarted = 0, StingersStarted = 0, FramesRendered = 0;
	float LimiterMinGain = 1.f;
	int64 SafetyClips = 0;
	float BlockPeak = 0.f;
	double RenderSeconds = 0;   // wall time spent in Render
	int32 MusicAge = 0;
	float MusicDensity = 0.f;
	float MusicTension = 0.f;
	int32 ScaleRoot = 0, ScaleMode = 0;
	int32 InstNotes[14] = {};   // notes started per music instrument (EDmInst order, DmAudioMusic.h)
};

class DEMIURGE_API FDmAudioEngine
{
public:
	explicit FDmAudioEngine(float SampleRate = 48000.f, uint32 Seed = 1);
	~FDmAudioEngine();
	FDmAudioEngine(const FDmAudioEngine&) = delete;
	FDmAudioEngine& operator=(const FDmAudioEngine&) = delete;

	// ---- any thread
	void SetContext(const FDmAudioContext& Ctx);
	void SetVolumes(const FDmAudioVolumes& V);
	void Play(EDmCue Cue, const FDmCueParams& P = FDmCueParams());
	void Stinger(EDmStinger S);
	/** Fade everything out (true) or back in (false). */
	void Suspend(bool bSuspend);
	FDmAudioLevels GetLevels() const;
	float GetSampleRate() const { return Sr; }

	// ---- the one render consumer
	/** Stereo interleaved floats (L R L R ...). */
	void Render(float* Interleaved, int32 Frames);
	void RenderSplit(float* OutL, float* OutR, int32 Frames);

	struct FImpl;
private:
	float Sr = 48000.f;
	FImpl* Impl = nullptr;
};
