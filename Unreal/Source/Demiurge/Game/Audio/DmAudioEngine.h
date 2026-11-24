// DEMIURGE — Game/Audio/DmAudioEngine.h : the synthesised mix (SPEC-UNREAL 2 C9, docs/content/sound-direction.md).
// Owner: C9 audio.
//
// FDmAudioEngine renders the whole game mix procedurally:
//   ambience (2): beds chosen by zoom (space hum, high air, wind and clouds, the land) and by what lies under
//     the focus (open sea, surf, forest birds and leaves, crickets and an owl by night, rainforest insects,
//     drips and frogs, grass, desert wind and sand, tundra wind and ice, mountain rockfalls, wetland bubbles,
//     rain, fire, lava, the town of each age); at most 6 beds sound at once, gains smoothed with tau 1.5 s;
//   one-shot cues (3, 4): the binding names of SPEC-UI 7 plus "fall", "toast" and "chronicle", each with the
//     recipe variants of 3.2 (FDmCueParams::Variant), placed by distance (low-pass 20 kHz x (1 - 0.85 d) and
//     a far echo of 80 + 400 d ms);
//   the generative score (1, DmAudioMusic.h).
// Mix (5): buses music, sfx, ambience, ui (each with a 10 Hz DC blocker) -> hall and sky reverbs -> master:
// 30 Hz high-pass (12 dB/oct), glue compressor (-18 dB, 2:1, 30 / 300 ms, 6 dB knee), look-ahead limiter at
// -1 dBFS. Ducking (5.2) takes the deepest request, never the sum. Voices (5.3): at most 24 one-shots (the
// oldest quietest stolen with a 30 ms fade), per-name limits, at most 12 effect starts per second.
// No assets, no allocation while rendering.
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
	Toast,       // a notification (4: proposed, flagged)
	Chronicle,   // a chronicle entry: the soft quill (4: proposed, flagged)
	Count
};

/** Recipe variants (sound-direction 3.2), FDmCueParams::Variant. */
namespace DmCueVar
{
	enum : uint8
	{
		RumbleEruption = 0, RumbleQuake = 1, RumbleRaise = 2, RumbleLower = 3, RumbleAge = 4,
		RoarLaunch = 0, RoarComet = 1,
		WaveTsunami = 0, WaveComet = 1,
		ChimeHeal = 0, ChimePeace = 1,
		ChoirProphet = 0, ChoirAwaken = 1,
		ShimmerBloom = 0, ShimmerInspire = 1,
		ArmShape = 0, ArmSky = 1, ArmWrath = 2, ArmGrace = 3, ArmGenesis = 4, ArmDisarm = 8,   // ArmDisarm is a flag
	};
}

enum class EDmStinger : uint8
{
	Age, Founding, Miracle, Wrath,
	EndingBeloved, EndingFeared, EndingForgotten, EndingParting, EndingSilence, EndingAsh, EndingQuiet,
	Count
};

enum class EDmBus : uint8 { Music, Sfx, Ambience, Ui, Count };

/** Ambience beds (diagnostics and tests read their gains). */
enum class EDmLayer : uint8
{
	Space, Wind, Surf, Sea, Birds, Crickets, Rain, Fire, Lava, TownEarly, TownMiddle, TownIndustry, TownModern, Leaves,
	HighAir, Grass, Desert, Cold, Rockfall, Wetland, Rainforest,
	Count
};

namespace DmAudio
{
	DEMIURGE_API const TCHAR* CueName(EDmCue C);
	/** Case-insensitive; the binding names of SPEC-UI 7 plus "fall", "toast", "chronicle". False for unknown names. */
	DEMIURGE_API bool CueFromName(const FString& Name, EDmCue& Out);
	DEMIURGE_API const TCHAR* StingerName(EDmStinger S);
	/** "age", "founding", "miracle", "wrath", "ending-beloved" ... "ending-silence", "ending-ash", "ending-quiet". */
	DEMIURGE_API bool StingerFromName(const FString& Name, EDmStinger& Out);
	DEMIURGE_API const TCHAR* LayerName(EDmLayer L);
	DEMIURGE_API EDmBus CueBus(EDmCue C);
	/** At most this many of one cue at once (5.3). */
	DEMIURGE_API int32 CueLimit(EDmCue C);
}

/** What the listener hears from: filled by ADmAudioDirector from the camera and the world each frame,
 *  or by a scripted offline scenario. All fractions are 0..1. */
struct FDmAudioContext
{
	float Altitude01 = 1.f;     // 0 = at the ground, 1 = far orbit (log of the camera altitude); zoom01 = 1 - Altitude01
	float Day = 1.f;            // sunlight at the focus (0 night .. 1 day)
	float Sea = 0.f;            // open water around the focus
	float Coast = 0.f;          // shoreline around the focus
	float Forest = 0.f;         // temperate forest / taiga, weighted by vegetation
	float Rainforest = 0.f;     // rainforest
	float Open = 0.f;           // grassland / savanna
	float Wetland = 0.f;        // wetland
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
	float Tension = 0.f;        // war and disaster (diagnostics)
	float Mood[4] = {};         // music moods (sound-direction 1.3): war, plague, golden age, doom (0..1)
	bool bPaused = false;
	bool bScore = true;         // false: the generative score is silent (stingers still play)
	uint32 Seed = 1;            // the world's seed: picks the tonic, the scale family and the tempi
};

struct FDmAudioVolumes
{
	// sound-direction 5.1 default user volumes
	float Master = 0.8f, Music = 0.7f, Sfx = 0.9f, Ambience = 0.8f, Ui = 0.6f;
};

struct FDmCueParams
{
	float Vol = 1.f;            // linear gain (already including distance attenuation)
	float Pan = 0.f;            // -1 left .. 1 right
	float Muffle = 0.f;         // 0 = clear (at the focus) .. 1 = far away / behind the planet (d of 3.1)
	float Param = 0.f;          // cue-specific (Fall: seconds until impact; Impact / Rumble: power 0..1; Hover: button index;
	                            // Horn: the age; Rumble raise / lower: seconds held; Rainfall: seconds held)
	float Delay = 0.f;          // seconds before it starts (sound travelling)
	uint8 Variant = 0;          // DmCueVar
};

/** A snapshot of the engine's state for diagnostics (copied under a lock; cheap). */
struct FDmAudioLevels
{
	float Layer[int32(EDmLayer::Count)] = {};
	int32 Voices = 0, MusicVoices = 0, Beds = 0;
	int64 CuesStarted = 0, CuesDropped = 0, CuesStolen = 0, StingersStarted = 0, StingersDropped = 0, FramesRendered = 0;
	float LimiterMinGain = 1.f;
	float GlueMinGainDb = 0.f;
	float GovernorDb = 0.f;     // the deepest the loudness governor pulled the master (5.4 rule 4)
	int64 SafetyClips = 0;
	float BlockPeak = 0.f;
	double RenderSeconds = 0;   // wall time spent in Render
	int32 MusicAge = 0;
	float MusicDensity = 0.f;
	float MusicTension = 0.f;
	float MusicTempo = 0.f;
	float MusicMood[4] = {};
	float MusicDuckDb = 0.f, AmbDuckDb = 0.f;   // current effects ducking
	float MinMusicDuckDb = 0.f, MinAmbDuckDb = 0.f;
	double VoiceBusySec = 0, ScoreSec = 0;     // the voice layer's sounding time over the score's time
	int32 Phrases = 0;
	int32 MaxStartsPerSec = 0;
	int32 ScaleRoot = 0, ScaleMode = 0;
	int32 InstNotes[20] = {};   // notes started per music instrument (EDmInst order, DmAudioMusic.h)
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
	/** Calibration (tests): one music note of instrument Inst (EDmInst), variant Var, alone. */
	void DebugMusicNote(int32 Inst, int32 Var, float Midi, float Vel, float Gate);

	// ---- the one render consumer
	/** Stereo interleaved floats (L R L R ...). */
	void Render(float* Interleaved, int32 Frames);
	void RenderSplit(float* OutL, float* OutR, int32 Frames);

	struct FImpl;
private:
	float Sr = 48000.f;
	FImpl* Impl = nullptr;
};
