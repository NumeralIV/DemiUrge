// DEMIURGE — Game/Audio/DmAudioMusic.h : the slow generative score (old SPEC-UI 7, music.js). Owner: C9 audio.
//
// Instrumentation and harmony evolve with the world's age (bone flute and hand drum -> lyre and reed
// -> strings and bells -> brass and piano -> synth pads), in a scale chosen per world from its seed
// (DmAudioScale.h); the score thins out when the camera is far away, a darker layer (low cello drone,
// slow taiko, a darker mode) rises with war and disaster, and genesis is a held breath (one breathing
// chord). Stingers: age, founding, miracle, wrath and one per ending mood. Sparse rather than busy.
// Rendered sample by sample from a fixed pool of notes; no allocation after Init.
#pragma once

#include "CoreMinimal.h"
#include "Game/Audio/DmAudioDsp.h"
#include "Game/Audio/DmAudioScale.h"
#include "Game/Audio/DmAudioEngine.h"

enum class EDmInst : uint8
{
	Flute, HandDrum, Lyre, Reed, Strings, Bell, Brass, Piano, Pad, Glass, Cello, Taiko, Breath, Choir,
	Count
};

struct FDmMusicParams
{
	int32 Age = 0;
	int32 Phase = 4;
	float Tension = 0.f;
	float Altitude01 = 0.5f;
	bool bPaused = false;
	bool bScore = true;
	uint32 Seed = 1;
};

class FDmMusic
{
public:
	void Init(float InSr, uint32 Seed);
	/** Called once per block before Render (control rate). */
	void Update(const FDmMusicParams& P, float BlockSeconds);
	void Stinger(EDmStinger S);
	/** Adds into the music buffers and the reverb send. */
	void Render(float* L, float* R, float* SendL, float* SendR, int32 N);

	int32 ActiveNotes() const;
	float GetDensity() const { return Density; }
	float GetTension() const { return TensionS; }
	int32 GetAge() const { return CurAge; }
	const FDmScale& GetScale() const { return Scale; }
	float GetGain() const { return OutGain; }
	/** Notes started per instrument since Init (tests read what the score played). */
	int32 NotesPlayed[int32(EDmInst::Count)] = {};

	static const TCHAR* InstName(EDmInst I);

private:
	static constexpr int32 MaxNotes = 48;
	static constexpr int32 KsLen = 2048;
	static constexpr int32 MaxSched = 64;

	struct FNote
	{
		bool bOn = false;
		bool bStinger = false;
		EDmInst Inst = EDmInst::Flute;
		float Hz = 220.f, Vel = 0.5f, Gate = 1.f, Pan = 0.f;
		float T = 0.f;
		float Env = 0.f;
		bool bReleased = false;
		float Ph[6] = {};
		float Aux[4] = {};
		DmDsp::FSvf F[3];
		DmDsp::FLp1 Lp;
		DmDsp::FPink Pink;
		float* Ks = nullptr;   // Karplus-Strong line (lyre), KsLen floats
		int32 KsPos = 0;
		float KsDelay = 100.f;
		float KsRho = 0.996f;
		DmDsp::FRng Rng;
		int32 Ctrl = 0;
		int64 Order = 0;
	};
	struct FSched { bool bOn = false; float At = 0.f; EDmInst Inst = EDmInst::Bell; float Midi = 60.f; float Vel = 0.5f; float Dur = 1.f; float Pan = 0.f; };

	float Sr = 48000.f, InvSr = 1.f / 48000.f;
	DmDsp::FRng Rng;
	FDmScale BaseScale, Scale;
	uint32 SeedNow = 0;
	TArray<FNote> Notes;
	TArray<float> KsPool;
	FSched Sched[MaxSched];
	int64 NoteOrder = 0;

	// clock
	double Clock = 0;            // seconds of music time
	double NextEighth = 0;
	int64 Eighth = 0;
	float Tempo = 60.f;
	// harmony
	int32 ChordDeg = 0;
	int32 Chord[4] = { 0, 2, 4, 7 };
	int32 MelDeg = 4;
	int32 PhraseLeft = 0;        // eighths left in the current phrase
	EDmInst PhraseInst = EDmInst::Flute;
	int32 ArpIdx = 0;
	// state
	FDmMusicParams Prm;
	int32 CurAge = 0, PrevAge = 0;
	float AgeBlend = 1.f;
	float Density = 0.5f, TensionS = 0.f, FarS = 0.f, OutGain = 1.f, DuckS = 1.f;
	double StingerUntil = -1;
	double StingerStart = 0;
	bool bGenesis = false;
	bool bFirstUpdate = true;

	float Weight(EDmInst I) const;
	void Step();                 // one eighth note
	void NewChord();
	void PlayNote(EDmInst I, float Midi, float Vel, float GateSec, float Pan, bool bStinger = false);
	void Schedule(float At, EDmInst I, float Midi, float Vel, float Dur, float Pan = 0.f);
	void RenderNote(FNote& Nt, float* L, float* R, float* SendL, float* SendR, int32 N);
	EDmInst MelodicFor(float Pick) const;
};
