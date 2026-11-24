// DEMIURGE — Game/Audio/DmAudioMusic.h : the slow generative score (docs/content/sound-direction.md 1).
// Owner: C9 audio.
//
// Four layers (1.1): the ground (a drone or pedal), the voice (the age's lead, playing phrases), the
// colour (a second instrument answering the voice or holding chord tones) and the pulse (percussion or
// an ostinato). Per age (1.2): Stone bone flute + frame drum over a hummed drone; Bronze adds the lyre;
// Iron reed, lyre fifths, a bowed fifth, low drum and shaker; Sail solo string, FM bells, a string pad,
// a soft lyre ostinato; Industry piano, a soft horn section, a dark pad, a muted plucked figure; Atomic
// piano in octaves, organ, brass, a clock tick and a heartbeat thump; Space glass lead, a wide pad,
// a rising arpeggio, a drone an octave up and the bone flute returning one phrase in four.
//
// Harmony: the world's tonic and family come from its seed, the mode from the age (DmAudioScale.h);
// chords change every 16-32 s (the ground moves between degrees 1, 4 or 5 and 6). Phrases follow the
// grammar of 1.1 (start on 1, 3 or 5; end on 1, 2 or 5; steps of 1-2 degrees with one leap; lengths
// of 1, 1, 2, 3 beats; the fourth phrase restates the first a 4th or 5th away) and the voice rests at
// least as long as it sang, so it is silent over half of any minute.
//
// Zoom thinning: zoom01 = 1 - Altitude01; < 0.2 ground and sparse voice at -6 dB; 0.2-0.6 adds the
// colour; > 0.6 all four layers. Layers fade over 8 s. Moods (1.3) overlay the age music with their
// own fades: war, plague, golden age, doom (war and doom take priority over golden; plague combines);
// tempo glides over 8 s. Age changes wait for the phrase to end, play the age stinger, crossfade the
// ground over 12 s and hand the last phrase to the new voice. Genesis is a held breath that gains a
// partial with each step. Stingers (1.4) queue at one per 6 s (dropped if 20 s late, except endings)
// and duck the rest of the music by 8 dB (300 ms / 4 s); effects never duck a stinger.
//
// Rendered sample by sample from a fixed pool of notes; no allocation after Init.
#pragma once

#include "CoreMinimal.h"
#include "Game/Audio/DmAudioDsp.h"
#include "Game/Audio/DmAudioScale.h"
#include "Game/Audio/DmAudioEngine.h"

enum class EDmInst : uint8
{
	Flute, HandDrum, Lyre, Reed, Strings, Bell, Brass, Piano, Pad, Glass, Cello, Taiko, Breath, Choir,
	Organ, Perc, Cluster, Rafters,
	Count
};

enum class EDmMood : uint8 { War, Plague, Golden, Doom, Count };

struct FDmMusicParams
{
	int32 Age = 0;
	int32 Phase = 4;
	float Tension = 0.f;
	float Mood[int32(EDmMood::Count)] = {};   // 0..1 targets (sound-direction 1.3)
	float Altitude01 = 0.5f;
	bool bPaused = false;
	bool bScore = true;
	uint32 Seed = 1;
};

class FDmMusic
{
public:
	enum class ELayer : uint8 { Voice, Colour, Ground, Pulse, Fx, Stinger, Count };

	void Init(float InSr, uint32 Seed);
	/** Called once per block before Render (control rate). */
	void Update(const FDmMusicParams& P, float BlockSeconds);
	/** Queue a stinger. Returns false when it is refused outright (rate limits of 1.4). */
	bool Stinger(EDmStinger S);
	/** Effects ducking (sound-direction 5.2), linear gain, applied to the score but never to stingers. */
	void SetDuck(float G) { ExtDuck = G; }
	/** Adds into the music buffers, the hall send and the sky send. */
	void Render(float* L, float* R, float* HallL, float* HallR, float* SkyL, float* SkyR, int32 N);

	int32 ActiveNotes() const;
	float GetDensity() const { return Density; }
	float GetTension() const { return TensionS; }
	int32 GetAge() const { return CurAge; }
	const FDmScale& GetScale() const { return Scale; }
	float GetGain() const { return OutGain; }
	float GetTempo() const { return TempoNow; }
	float GetMood(EDmMood M) const { return MoodW[int32(M)]; }
	float GetLayerGain(ELayer L) const { return LayerG[int32(L)]; }
	/** Seconds the voice layer has sounded / been silent since Init (the >= 50 % silence rule). */
	double VoiceBusySeconds() const { return VoiceBusy; }
	double ScoreSeconds() const { return ScoreTime; }
	int32 StingersPlayed = 0, StingersDropped = 0;
	int32 PhrasesPlayed = 0;
	/** Notes started per instrument since Init (tests read what the score played). */
	int32 NotesPlayed[int32(EDmInst::Count)] = {};
	/** Engine hook: an ending stinger that needs effects (feared: the distant launch roar; ash: crackle). */
	int32 TakeEndingRequest() { const int32 R = EndingReq; EndingReq = -1; return R; }

	static const TCHAR* InstName(EDmInst I);
	static const TCHAR* MoodName(EDmMood M);
	/** Debug / calibration: one note of an instrument (variant Var) at Midi, alone. */
	void DebugNote(EDmInst I, uint8 Var, float Midi, float Vel, float Gate);

private:
	static constexpr int32 MaxNotes = 40;
	static constexpr int32 KsLen = 2048;
	static constexpr int32 MaxSched = 160;
	static constexpr int32 MaxPending = 6;
	static constexpr int32 MaxPhrase = 8;

	struct FNote
	{
		bool bOn = false;
		bool bStinger = false;
		EDmInst Inst = EDmInst::Flute;
		uint8 Var = 0;
		ELayer Layer = ELayer::Voice;
		int8 Mood = -1;          // >= 0: scaled by that mood's weight
		float Hz = 220.f, Vel = 0.5f, Gate = 1.f, Pan = 0.f;
		float T = 0.f;
		float Env = 0.f;
		bool bReleased = false;
		float XF = 1.f, XFRate = 0.f;   // age crossfade gain and its rate per second
		float Hall = 0.3f, Sky = 0.f;   // sends
		float Ph[8] = {};
		float Aux[6] = {};
		DmDsp::FSvf F[3];
		DmDsp::FLp1 Lp;
		DmDsp::FHp1 Hp;
		DmDsp::FPink Pink;
		DmDsp::FBrown Brown;
		float* Ks = nullptr;   // Karplus-Strong line (lyre), KsLen floats
		int32 KsPos = 0;
		float KsDelay = 100.f;
		float KsRho = 0.996f;
		DmDsp::FRng Rng;
		int32 Ctrl = 0;
		int64 Order = 0;
	};
	struct FSched
	{
		bool bOn = false; double At = 0; EDmInst Inst = EDmInst::Bell; uint8 Var = 0; ELayer Layer = ELayer::Voice; int8 Mood = -1;
		float Midi = 60.f; float Vel = 0.5f; float Dur = 1.f; float Pan = 0.f; float XF = 1.f, XFRate = 0.f;
	};
	struct FPhrase { int32 Deg[MaxPhrase] = {}; float Len[MaxPhrase] = {}; int32 N = 0; };
	struct FPending { bool bOn = false; EDmStinger S = EDmStinger::Age; double At = 0; };

	float Sr = 48000.f, InvSr = 1.f / 48000.f;
	DmDsp::FRng Rng, MelRng;
	FDmScale Scale, Harm;           // the voice scale and the harmony (parent mode) of the current age and mood
	uint32 SeedNow = 0;
	TArray<FNote> Notes;
	TArray<float> KsPool;
	FSched Sched[MaxSched];
	int64 NoteOrder = 0;
	DmDsp::FDelay EchoL, EchoR;     // the valley answering (Stone and Bronze)
	DmDsp::FLp1 EchoLpL, EchoLpR;
	float EchoSendL[256] = {}, EchoSendR[256] = {};

	// clock and tempo
	double Clock = 0;
	float TempoBase = 60.f, TempoNow = 60.f, Jitter = 0.04f;
	// harmony
	int32 ChordRoot = 0;
	double NextChordAt = 0, ChordStart = 0, ChordEnd = 0;
	bool bChordForce = false;
	uint32 GroundSig = 0;
	// phrases
	double NextPhraseAt = 2.0, PhraseEnd = 0;
	int32 PhraseK = 0;
	FPhrase Group0, LastPhrase;
	bool bHandOff = false;
	// pulse and overlays
	double NextBarAt = 0, NextTollAt = 0, NextSparkleAt = 0;
	int64 Bar = 0;
	// state
	FDmMusicParams Prm;
	int32 CurAge = 0, PendingAge = -1;
	double AgeChangedAt = -100;
	float MoodW[int32(EDmMood::Count)] = {};
	float LayerG[int32(ELayer::Count)] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
	float Density = 0.5f, TensionS = 0.f, OutGain = 1.f, StingDuck = 1.f, ExtDuck = 1.f, ExtDuckS = 1.f;
	float Zoom01 = 0.5f;
	double StingerUntil = -1, NextStingerOk = 0, LastFounding = -100, LastWrath = -100, LastMiracle = -100;
	FPending Pending[MaxPending];
	int32 EndingReq = -1;
	bool bGenesis = false, bEnded = false;
	int32 GenesisPhase = -1;
	bool bFirstUpdate = true;
	double VoiceBusy = 0, ScoreTime = 0;

	// score
	void Score();
	void NewChord();
	void Ground(double T0, double Len);
	void Pulse(double T0);
	void StartPhrase();
	void MakePhrase(int32 Lo, int32 Hi, bool bShort, bool bSingle, FPhrase& Out);
	bool Transposed(const FPhrase& In, int32 Lo, int32 Hi, FPhrase& Out) const;
	double PlacePhrase(const FPhrase& Ph, double T0, EDmInst Inst, uint8 Var, int32 Oct);
	void Answer(double TEnd, const FPhrase& Ph);
	void ChangeAge(int32 NewAge);
	void PlayStingerNow(EDmStinger S);
	void Rescale();

	float Beat() const { return 60.f / FMath::Max(20.f, TempoNow); }
	float Mood(EDmMood M) const { return MoodW[int32(M)]; }
	bool MoodOn(EDmMood M) const { return MoodW[int32(M)] > 0.5f; }

	void PlayNote(EDmInst I, uint8 Var, ELayer Layer, float Midi, float Vel, float GateSec, float Pan, int8 MoodIdx = -1, float XF = 1.f, float XFRate = 0.f);
	void Schedule(double At, EDmInst I, uint8 Var, ELayer Layer, float Midi, float Vel, float Dur, float Pan = 0.f, int8 MoodIdx = -1, float XF = 1.f, float XFRate = 0.f);
	void RenderNote(FNote& Nt, float* L, float* R, float* HallL, float* HallR, float* SkyL, float* SkyR, int32 N);
};
