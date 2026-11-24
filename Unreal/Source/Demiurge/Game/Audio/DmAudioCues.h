// DEMIURGE — Game/Audio/DmAudioCues.h : one-shot cue synthesis (SPEC-UI 7 names, sound-direction 3 and 4). Owner: C9 audio.
//
// Every cue is a small "patch": up to 8 oscillators (sine, band-limited saw, triangle, FM bell, a plucked
// tone) with their own delay / attack / decay / pitch glide / vibrato, an optional tone filter (low-pass
// sweep, retriggered per call, or a vowel formant bank), up to 3 filtered noise layers (white / pink /
// brown, filter sweep, swell peak, random rolls or shake) and a grain cloud (droplets, sparks, crackle,
// rock cracks). FDmCueVoice renders a patch; BuildCuePatch makes the patch of each cue name and variant.
// All fixed size: building and playing a cue never allocates.
#pragma once

#include "CoreMinimal.h"
#include "Game/Audio/DmAudioDsp.h"
#include "Game/Audio/DmAudioScale.h"
#include "Game/Audio/DmAudioEngine.h"

struct FDmCueOsc
{
	enum : uint8 { Sine, Saw, Tri, Bell, Pluck };
	uint8 Wave = Sine;
	float Hz = 440.f, Amp = 1.f;
	float Delay = 0.f, Attack = 0.003f, Decay = 1.f;     // Decay: seconds to -60 dB after the attack
	float Hold = 0.f;                                   // seconds at full level before the decay
	float GlideStart = 1.f, GlideTime = 0.05f;          // frequency starts at Hz * GlideStart and approaches Hz
	float VibHz = 0.f, VibDepth = 0.f;                  // depth as a frequency ratio (0.004 = 7 cents)
	float FmRatio = 1.4f, FmIndex = 0.f, FmDecay = 0.5f, FmFloor = 0.f;   // index decays from FmIndex toward FmFloor
	float Harm3 = 0.f;                                  // Sine: add a 3rd harmonic; Pluck: brightness
	float Pan = 0.f;                                    // per-oscillator offset added to the voice pan
};

struct FDmCueNoise
{
	enum : uint8 { White, Pink, Brown };
	enum : uint8 { Lp, Bp, Hp };
	uint8 Color = Pink, Mode = Lp;
	float Amp = 0.f;
	float Delay = 0.f, Attack = 0.01f, Hold = 0.f, Decay = 1.f;
	float Hz0 = 1000.f, Hz1 = 1000.f, Sweep = 1.f, Q = 0.7f;   // exponential sweep Hz0 -> Hz1 over Sweep seconds (after Delay)
	float Roll = 0.f, RollHz = 3.f;                             // random amplitude rolls (thunder, rumble)
	float Shake = 0.f, ShakeHz0 = 5.f, ShakeHz1 = 5.f;          // slow amplitude shake (3.1 rule 3): depth, rate start -> end
	float HpHz = 0.f;                                           // an extra one-pole high-pass (35-45 Hz rules)
};

struct FDmCueGrains
{
	enum : uint8 { None, Tone, Tick, Pluck };
	uint8 Kind = None;
	float Rate = 0.f;            // grains per second at the peak
	float Delay = 0.f;
	float RampIn = 0.f, Peak = 0.f, RampOut = 1.f;   // rate envelope: 0 -> Rate over RampIn, stays until Peak, falls to 0 over RampOut
	float HzLo = 1000.f, HzHi = 3000.f;               // Tone / Pluck: frequency range; Tick: band-pass range
	float Decay = 0.02f, Amp = 0.3f;
	float Glide = 1.f;           // Tone: frequency ratio reached over the grain (upward bubbles > 1)
	bool bScale = false;          // Tone grains snap to the scale (shimmer)
	int32 OctLo = 6, OctHi = 7;
};

struct FDmCuePatch
{
	static constexpr int32 MaxOsc = 8;
	static constexpr int32 MaxNoise = 3;
	FDmCueOsc Osc[MaxOsc];
	int32 NumOsc = 0;
	FDmCueNoise Noise[MaxNoise];
	int32 NumNoise = 0;
	FDmCueGrains Grains;
	enum : uint8 { ToneNone, ToneLp, ToneFormant };
	uint8 ToneFilter = ToneNone;
	float ToneHz0 = 2000.f, ToneHz1 = 2000.f, ToneSweep = 1.f, ToneQ = 0.7f, ToneRetrig = 0.f;   // ToneRetrig > 0: the sweep restarts every ToneRetrig s
	float Formant[3] = { 700.f, 1220.f, 2600.f };
	float FormantQ[3] = { 6.f, 8.f, 10.f };
	float FormantGain[3] = { 1.f, 0.5f, 0.2f };
	float Dur = 1.f, Attack = 0.002f, Release = 0.05f;   // whole-voice envelope
	float Gain = 0.2f, Send = 0.25f;
	bool bHall = false;          // send to the hall reverb (choir, horn, toll), else the sky reverb (3.1 rule 4)
	float FarEcho = 0.f;         // level of the far echo (3.1 rule 2), 0 = none
	EDmBus Bus = EDmBus::Sfx;
	FDmCueOsc& AddOsc() { return Osc[FMath::Min(NumOsc++, MaxOsc - 1)]; }
	FDmCueNoise& AddNoise() { return Noise[FMath::Min(NumNoise++, MaxNoise - 1)]; }
};

/** Builds the patch of a cue (deterministic for a given rng state). */
void BuildCuePatch(EDmCue Cue, const FDmCueParams& P, const FDmScale& Scale, DmDsp::FRng& Rng, FDmCuePatch& Out);

class FDmCueVoice
{
public:
	bool bActive = false;
	EDmCue Cue = EDmCue::Click;
	uint8 Variant = 0;
	int64 StartOrder = 0;
	float Muffle = 0.f, VolIn = 1.f, StartSec = 0.f, DurSec = 0.f;

	void Start(EDmCue InCue, const FDmCuePatch& InPatch, const FDmCueParams& P, const FDmScale& InScale, float InSr, uint32 Seed, int64 Order);
	/** Adds into the bus buffers and the reverb send (hall or sky by the patch) and the far-echo send; false when finished. */
	bool Render(float* L, float* R, float* HallL, float* HallR, float* SkyL, float* SkyR, float* EchoL, float* EchoR, int32 N);
	float Level() const { return LastLevel; }
	/** Fade out over Sec, then stop (voice stealing, 5.3: never a cut). */
	void Kill(float Sec);
	bool IsDying() const { return KillMul < 1.f; }
	bool IsDelayed() const { return Delay > 0; }

private:
	struct FGrain { bool bOn = false; float Ph = 0.f, Inc = 0.f, IncMul = 1.f, Amp = 0.f, Mul = 0.f; float Pan = 0.f; DmDsp::FLp1 Lp; DmDsp::FHp1 Hp; float* Ks = nullptr; int32 KsLen = 0, KsPos = 0; float KsRho = 0.99f; };
	static constexpr int32 MaxGrains = 16;
	static constexpr int32 GrainKs = 512;

	FDmCuePatch Patch;
	float Sr = 48000.f, InvSr = 1.f / 48000.f;
	float T = 0.f;
	int64 Sample = 0, Delay = 0;
	float Vol = 1.f, Pan = 0.f, Gl = 0.7f, Gr = 0.7f;
	float LastLevel = 0.f;
	float KillMul = 1.f, KillStep = 0.f;

	float OscPh[FDmCuePatch::MaxOsc] = {}, OscMod[FDmCuePatch::MaxOsc] = {}, OscEnv[FDmCuePatch::MaxOsc] = {};
	float OscMul[FDmCuePatch::MaxOsc] = {}, OscGlide[FDmCuePatch::MaxOsc] = {}, OscGlideMul[FDmCuePatch::MaxOsc] = {};
	float OscFm[FDmCuePatch::MaxOsc] = {}, OscFmMul[FDmCuePatch::MaxOsc] = {}, OscVib[FDmCuePatch::MaxOsc] = {};
	float OscGl[FDmCuePatch::MaxOsc] = {}, OscGr[FDmCuePatch::MaxOsc] = {};
	DmDsp::FLp1 OscLp[FDmCuePatch::MaxOsc];
	DmDsp::FSvf ToneSvf, FormSvf[3];
	float NzEnv[FDmCuePatch::MaxNoise] = {}, NzMul[FDmCuePatch::MaxNoise] = {}, NzShakePh[FDmCuePatch::MaxNoise] = {};
	DmDsp::FSvf NzSvf[FDmCuePatch::MaxNoise];
	DmDsp::FPink NzPink[FDmCuePatch::MaxNoise];
	DmDsp::FBrown NzBrown[FDmCuePatch::MaxNoise];
	DmDsp::FDrift NzRoll[FDmCuePatch::MaxNoise];
	DmDsp::FHp1 NzHp[FDmCuePatch::MaxNoise];
	FGrain Grains[MaxGrains];
	float GrainBuf[MaxGrains * GrainKs] = {};
	DmDsp::FRng Rng;
	DmDsp::FLp1 MuffleL, MuffleR;
	FDmScale Scale;
	int32 CtrlCount = 0;

	void Control();
	void SpawnGrain();
};
