// DEMIURGE — Game/Audio/DmAudioMusic.cpp : the generative score (sound-direction 1). Owner: C9 audio.
#include "Game/Audio/DmAudioMusic.h"

using namespace DmDsp;

namespace
{
	// 1.2: tempo range, onset jitter and hall send per age (Stone .. Space)
	const float GTempo[7][2] = { { 56.f, 66.f }, { 60.f, 70.f }, { 64.f, 76.f }, { 66.f, 80.f }, { 72.f, 92.f }, { 76.f, 96.f }, { 52.f, 64.f } };
	const float GJitter[7] = { 0.040f, 0.040f, 0.030f, 0.025f, 0.010f, 0.010f, 0.015f };
	const float GHall[7] = { 0.12f, 0.12f, 0.18f, 0.25f, 0.22f, 0.22f, 0.15f };
	constexpr float SendScale = 2.4f;     // direction send fractions -> this engine's reverb input
	// layer levels re the voice (dB): colour, ground, pulse (the previews' balance)
	const float GLayerDb[7][3] = { { -4.f, -7.f, -3.f }, { -3.f, -7.f, -4.f }, { 0.f, -1.f, -3.f }, { -3.f, -6.f, 0.f }, { -6.f, -9.f, 0.f }, { -8.f, -8.f, 0.f }, { -6.f, -12.f, 0.f } };
	// whole-score trim per age (dB), calibrated by the music-age-* offline renders to the 5.1 targets
	// (-30 Stone and Space, -29 Bronze, -28 Iron and Sail, -26 Industry and Atomic, music bus alone)
	const float GAgeTrimDb[7] = { -3.8f, -1.8f, -3.4f, -0.7f, 4.5f, 5.8f, -2.4f };
	const float GMoodIn[4] = { 6.f, 10.f, 10.f, 6.f };   // fade-in seconds: war, plague, golden, doom (1.3); out 15 s

	// Output level of each instrument at velocity 1 (normalises their raw loudness; calibrated by the
	// inst-* offline renders so a held note reads about -18 dBFS over its loudest 400 ms)
	const float GLevel[int32(EDmInst::Count)] = {
		/* Flute */ 0.17f, /* HandDrum */ 0.30f, /* Lyre */ 0.30f, /* Reed */ 0.30f, /* Strings */ 0.10f, /* Bell */ 0.13f,
		/* Brass */ 0.10f, /* Piano */ 0.15f, /* Pad */ 0.05f, /* Glass */ 0.12f, /* Cello */ 0.10f, /* Taiko */ 0.30f,
		/* Breath */ 0.10f, /* Choir */ 0.10f, /* Organ */ 0.12f, /* Perc */ 0.25f, /* Cluster */ 0.10f, /* Rafters */ 0.30f,
	};

	struct FVoiceCfg { EDmInst Inst; uint8 Var; int32 Oct, Lo, Hi; };
	const FVoiceCfg GVoice[7] = {
		{ EDmInst::Flute, 0, 5, 0, 4 },      // Stone: bone flute, range a 6th
		{ EDmInst::Flute, 0, 5, 0, 5 },      // Bronze: an octave
		{ EDmInst::Reed, 0, 4, 2, 9 },       // Iron: reed
		{ EDmInst::Strings, 0, 5, -3, 7 },   // Sail: solo string, about 1.5 octaves
		{ EDmInst::Piano, 0, 4, 2, 11 },     // Industry: piano
		{ EDmInst::Piano, 1, 4, 2, 10 },     // Atomic: piano in octaves
		{ EDmInst::Glass, 0, 5, 0, 9 },      // Space: glass lead
	};

	inline float CentsMul(float C) { return 1.f + 0.00057779f * C; }

	inline float Adsr(float& Env, bool& bRel, float T, float Gate, float A, float D, float S, float RelMul)
	{
		if (T < Gate)
		{
			if (T < A) Env = FMath::Max(Env, T / A);
			else if (T < A + D) Env = 1.f - (1.f - S) * (T - A) / FMath::Max(D, 1e-4f);
			else Env = S;
		}
		else { Env *= RelMul; bRel = true; }
		return Env;
	}
}

const TCHAR* FDmMusic::InstName(EDmInst I)
{
	static const TCHAR* Names[int32(EDmInst::Count)] = { TEXT("flute"), TEXT("hand-drum"), TEXT("lyre"), TEXT("reed"), TEXT("strings"),
		TEXT("bell"), TEXT("brass"), TEXT("piano"), TEXT("pad"), TEXT("glass"), TEXT("cello"), TEXT("taiko"), TEXT("breath"), TEXT("choir"),
		TEXT("organ"), TEXT("perc"), TEXT("cluster"), TEXT("rafters") };
	const int32 K = int32(I);
	return (K >= 0 && K < int32(EDmInst::Count)) ? Names[K] : TEXT("?");
}

const TCHAR* FDmMusic::MoodName(EDmMood M)
{
	static const TCHAR* Names[4] = { TEXT("war"), TEXT("plague"), TEXT("golden"), TEXT("doom") };
	const int32 K = int32(M);
	return (K >= 0 && K < 4) ? Names[K] : TEXT("?");
}

void FDmMusic::Init(float InSr, uint32 Seed)
{
	Sr = InSr;
	InvSr = 1.f / Sr;
	Rng = FRng(Seed ^ 0xA5A5F00Du);
	Rng.Next();
	MelRng = FRng(Seed * 7u + 1u);
	MelRng.Next();
	SeedNow = Seed;
	Notes.SetNum(MaxNotes);
	KsPool.SetNumZeroed(MaxNotes * KsLen);
	for (int32 I = 0; I < MaxNotes; ++I) { Notes[I] = FNote(); Notes[I].Ks = KsPool.GetData() + I * KsLen; }
	for (FSched& S : Sched) S.bOn = false;
	for (FPending& P : Pending) P.bOn = false;
	EchoL.Init(int32(Sr * 0.8f)); EchoR.Init(int32(Sr * 0.8f));
	EchoLpL.Set(1500.f, Sr); EchoLpR.Set(1500.f, Sr);
	Clock = 0; NextChordAt = 0; NextPhraseAt = 2.0; PhraseEnd = 0; NextBarAt = 0.5; Bar = 0; PhraseK = 0;
	NextTollAt = 3.0; NextSparkleAt = 2.0;
	ChordRoot = 0; ChordStart = 0; ChordEnd = 0; bChordForce = false; GroundSig = 0; bHandOff = false; Group0 = FPhrase(); LastPhrase = FPhrase();
	CurAge = 0; PendingAge = -1; AgeChangedAt = -100;
	for (float& W : MoodW) W = 0.f;
	for (float& G : LayerG) G = 1.f;
	Density = 0.5f; TensionS = 0.f; OutGain = 1.f; StingDuck = 1.f; ExtDuck = 1.f;
	StingerUntil = -1; NextStingerOk = 0; LastFounding = LastWrath = LastMiracle = -100;
	bFirstUpdate = true; bGenesis = false; bEnded = false; GenesisPhase = -1;
	VoiceBusy = 0; ScoreTime = 0;
	StingersPlayed = StingersDropped = PhrasesPlayed = 0;
	for (int32& C : NotesPlayed) C = 0;
	Scale = FDmScale::ForAge(Seed, 0);
	Harm = Scale.Harmony();
}

int32 FDmMusic::ActiveNotes() const
{
	int32 C = 0;
	for (const FNote& N : Notes) C += N.bOn ? 1 : 0;
	return C;
}

void FDmMusic::Rescale()
{
	Scale = FDmScale::ForAge(SeedNow, CurAge);
	if (MoodOn(EDmMood::War)) Scale = Scale.Darkened();
	Harm = Scale.Harmony();
	if (MoodOn(EDmMood::War)) Harm = Harm.Darkened();
}

void FDmMusic::Update(const FDmMusicParams& P, float BlockSeconds)
{
	Prm = P;
	const int32 Age = FMath::Clamp(P.Age, 0, 6);
	if (P.Seed != SeedNow) { SeedNow = P.Seed; Rescale(); }
	auto TempoFor = [this](int32 A)
	{
		FRng R(SeedNow * 2246822519u + uint32(A) * 3266489917u + 17u);
		R.Next();
		return R.Range(GTempo[A][0], GTempo[A][1]);
	};
	if (bFirstUpdate)
	{
		bFirstUpdate = false;
		CurAge = Age;
		for (int32 M = 0; M < int32(EDmMood::Count); ++M) MoodW[M] = Sat01(P.Mood[M]);
		TempoBase = TempoFor(CurAge);
		Jitter = GJitter[CurAge];
		TensionS = Sat01(P.Tension);
		Rescale();
		TempoNow = TempoBase;
	}
	if (Age != CurAge && Age != PendingAge) PendingAge = Age;
	if (PendingAge == CurAge) PendingAge = -1;
	bGenesis = P.Phase < 4;
	bEnded = P.Phase >= 5;
	TensionS += SmoothCoef(4.f, 1.f / BlockSeconds) * (Sat01(P.Tension) - TensionS);

	// moods: linear fades (in 6 or 10 s, out 15 s)
	for (int32 M = 0; M < int32(EDmMood::Count); ++M)
	{
		const float Tgt = Sat01(P.Mood[M]);
		if (MoodW[M] < Tgt) MoodW[M] = FMath::Min(Tgt, MoodW[M] + BlockSeconds / GMoodIn[M]);
		else MoodW[M] = FMath::Max(Tgt, MoodW[M] - BlockSeconds / 15.f);
	}
	const float GoldenEff = Mood(EDmMood::Golden) * (1.f - FMath::Max(Mood(EDmMood::War), Mood(EDmMood::Doom)));

	// zoom thinning (1.1): fades of 8 s
	Zoom01 = 1.f - Sat01(P.Altitude01);
	float Tg[int32(ELayer::Count)];
	Tg[int32(ELayer::Voice)] = bGenesis ? 0.f : (Zoom01 < 0.2f ? 0.5f : 1.f);
	Tg[int32(ELayer::Colour)] = (bGenesis || Zoom01 < 0.2f) ? 0.f : 1.f;
	Tg[int32(ELayer::Ground)] = 1.f;
	Tg[int32(ELayer::Pulse)] = (bGenesis || Zoom01 <= 0.6f) ? 0.f : (1.f - Mood(EDmMood::Plague)) * (1.f - 0.5f * Mood(EDmMood::Doom));
	Tg[int32(ELayer::Fx)] = 1.f;
	Tg[int32(ELayer::Stinger)] = 1.f;
	for (int32 I = 0; I < int32(ELayer::Count); ++I)
	{
		const float D = BlockSeconds / 8.f;
		LayerG[I] = LayerG[I] < Tg[I] ? FMath::Min(Tg[I], LayerG[I] + D) : FMath::Max(Tg[I], LayerG[I] - D);
	}

	// tempo: the age's tempo times the moods, gliding over 8 s
	const float TempoT = TempoBase * (1.f + 0.08f * Mood(EDmMood::War)) * (1.f - 0.10f * Mood(EDmMood::Plague)) * (1.f + 0.05f * GoldenEff) * (1.f - 0.15f * Mood(EDmMood::Doom));
	TempoNow += SmoothCoef(8.f / 3.f, 1.f / BlockSeconds) * (TempoT - TempoNow);
	if (PendingAge < 0 && FMath::Abs(TempoBase - TempoFor(CurAge)) > 0.01f) TempoBase = TempoFor(CurAge);

	Density = (bGenesis ? 0.3f : 0.62f) * (Zoom01 < 0.2f ? 0.4f : 1.f) * (P.bPaused ? 0.5f : 1.f);
	const float GainT = P.bPaused ? 0.7f : 1.f;
	OutGain += SmoothCoef(1.5f, 1.f / BlockSeconds) * (GainT - OutGain);
	// stingers duck the rest of the music by 8 dB (300 ms attack, 4 s release)
	const bool bSting = Clock < StingerUntil;
	const float DuckT = bSting ? DbToGain(-8.f) : 1.f;
	StingDuck += SmoothCoef(bSting ? 0.1f : 1.33f, 1.f / BlockSeconds) * (DuckT - StingDuck);

	// a change of the ground's character (a mood crossing half way, a genesis step) re-voices it now
	const int32 GPhase = bGenesis ? FMath::Clamp(P.Phase, 0, 3) : 7;
	const uint32 Sig = (MoodOn(EDmMood::War) ? 1u : 0u) | (MoodOn(EDmMood::Plague) ? 2u : 0u) | (GoldenEff > 0.5f ? 4u : 0u) | (MoodOn(EDmMood::Doom) ? 8u : 0u) | (uint32(GPhase) << 4);
	if (Sig != GroundSig) { GroundSig = Sig; bChordForce = true; }
}

// ------------------------------------------------------------------------------------------------ notes

void FDmMusic::PlayNote(EDmInst I, uint8 Var, ELayer Layer, float Midi, float Vel, float GateSec, float Pan, int8 MoodIdx, float XF, float XFRate)
{
	// a free slot, else steal the quietest non-stinger note
	int32 Best = -1;
	float BestScore = 1e9f;
	for (int32 K = 0; K < MaxNotes; ++K)
	{
		const FNote& N = Notes[K];
		if (!N.bOn) { Best = K; break; }
		const float Score = (N.bStinger ? 10.f : 0.f) + (N.Layer == ELayer::Ground ? 2.f : 0.f) + N.Env * N.Vel * N.XF + (N.bReleased ? 0.f : 0.5f);
		if (Score < BestScore) { BestScore = Score; Best = K; }
	}
	if (Best < 0) return;
	FNote& N = Notes[Best];
	float* KsBuf = N.Ks;
	N = FNote();
	N.Ks = KsBuf;
	N.bOn = true;
	N.bStinger = Layer == ELayer::Stinger;
	N.Inst = I;
	N.Var = Var;
	N.Layer = Layer;
	N.Mood = MoodIdx;
	N.Hz = MidiToHz(Midi);
	N.Vel = Clamp(Vel, 0.f, 1.f);
	N.Gate = FMath::Max(0.02f, GateSec);
	N.Pan = Clamp(Pan, -1.f, 1.f);
	N.XF = XF; N.XFRate = XFRate;
	N.Rng = FRng(Rng.Next() | 1u);
	N.Order = ++NoteOrder;
	++NotesPlayed[int32(I)];
	for (float& Ph : N.Ph) Ph = N.Rng.Uni();
	N.Env = 0.f;
	const int32 A = FMath::Clamp(CurAge, 0, 6);
	const float Golden = Mood(EDmMood::Golden) * (1.f - FMath::Max(Mood(EDmMood::War), Mood(EDmMood::Doom)));
	N.Hall = (GHall[A] + 0.10f * Golden) * SendScale;
	N.Sky = 0.f;
	switch (I)
	{
	case EDmInst::Flute:
		N.F[0].Set(N.Hz * 2.f, 1.5f, Sr);
		N.Aux[0] = -N.Rng.Range(30.f, 60.f);   // slides up into the note
		break;
	case EDmInst::HandDrum:
		N.F[0].Set(Var ? 1600.f : 1100.f, 0.8f, Sr);
		N.Env = 1.f; N.Aux[1] = 1.f; N.Aux[2] = 0.5f;
		break;
	case EDmInst::Taiko:
		N.F[0].Set(400.f, 0.7f, Sr);
		N.Hp.Set(45.f, Sr);
		N.Env = 1.f; N.Aux[1] = 1.f; N.Aux[2] = 0.3f;
		break;
	case EDmInst::Lyre:
	{
		N.KsDelay = FMath::Clamp(Sr / N.Hz - 0.5f, 4.f, float(KsLen - 4));
		static const float T60[3] = { 1.8f, 1.2f, 0.25f };
		N.KsRho = FMath::Min(0.9985f, std::pow(10.f, -3.f / (T60[FMath::Min<int32>(Var, 2)] * N.Hz)));
		FLp1 Ex; Ex.Set(5000.f, Sr);
		const int32 Len = int32(N.KsDelay) + 2;
		for (int32 K = 0; K < KsLen; ++K) N.Ks[K] = K < Len ? Ex.Tick(N.Rng.Bi()) * 1.4f : 0.f;
		N.KsPos = Len;
		N.Env = 1.f;
		N.Lp.Set(Var == 2 ? 2500.f : 6000.f, Sr);
		break;
	}
	case EDmInst::Reed:
		N.F[0].Set(1200.f, 2.f, Sr);
		N.F[1].Set(2600.f, 3.f, Sr);
		N.F[2].Set(3500.f, 0.7f, Sr);
		break;
	case EDmInst::Strings:
		if (Var == 0) { N.F[0].Set(800.f, 0.7f, Sr); N.F[1].Set(900.f, 1.2f, Sr); }
		else if (Var == 3) { N.F[0].Set(700.f, 0.7f, Sr); N.F[1].Set(700.f, 0.7f, Sr); }
		else { const float C = Var == 2 ? 900.f : 1200.f; N.F[0].Set(C, 0.7f, Sr); N.F[1].Set(C, 0.7f, Sr); }
		break;
	case EDmInst::Cello:
		N.F[0].Set(Var ? 500.f : 400.f, 0.7f, Sr);
		break;
	case EDmInst::Brass:
		N.F[0].Set(400.f, 0.9f, Sr);
		N.Vel = FMath::Min(N.Vel, 0.6f);   // never louder than mezzo-forte
		break;
	case EDmInst::Pad:
		N.F[0].Set(1500.f, 0.7f, Sr); N.F[1].Set(1500.f, 0.7f, Sr);
		N.Sky = 0.30f * SendScale; N.Hall = 0.f;
		break;
	case EDmInst::Bell:
		N.Env = 1.f; N.Aux[1] = 1.f; N.Aux[2] = 1.f; N.Aux[3] = 1.f; N.Aux[4] = 1.f;
		if (Var == 1) N.Hall = 0.35f * SendScale;
		if (Var == 2) N.Hall = 0.40f * SendScale;
		break;
	case EDmInst::Piano:
		N.Env = 1.f;
		for (int32 P = 0; P < 6; ++P) N.Aux[P] = 1.f;
		N.F[0].Set(2000.f + 4000.f * N.Vel, 0.6f, Sr);
		N.F[1].Set(3000.f, 1.f, Sr);
		break;
	case EDmInst::Glass:
		N.Sky = 0.35f * SendScale; N.Hall = 0.f;
		break;
	case EDmInst::Breath:
		N.Lp.Set(Var == 1 ? 900.f : 600.f, Sr);
		if (Var == 1) { N.Sky = 0.30f * SendScale; N.Hall = 0.f; }
		break;
	case EDmInst::Choir:
		N.F[0].Set(700.f, 6.f, Sr);
		N.F[1].Set(1220.f, 8.f, Sr);
		N.F[2].Set(2600.f, 10.f, Sr);
		break;
	case EDmInst::Perc:
		N.Env = 1.f;
		if (Var == 0) N.F[0].Set(6000.f, 1.5f, Sr);
		if (Var == 2) N.Hp.Set(40.f, Sr);
		if (Var == 3) { N.Sky = 0.30f * SendScale; N.Hall = 0.f; }
		break;
	case EDmInst::Cluster:
		N.Lp.Set(700.f, Sr);
		break;
	case EDmInst::Rafters:
		N.F[0].Set(180.f, 4.f, Sr);
		break;
	default: break;
	}
	if (N.bStinger && (I == EDmInst::Bell || I == EDmInst::Choir) && Var == 0) { N.Sky = FMath::Max(N.Sky, 0.40f * SendScale); }
}

void FDmMusic::Schedule(double At, EDmInst I, uint8 Var, ELayer Layer, float Midi, float Vel, float Dur, float Pan, int8 MoodIdx, float XF, float XFRate)
{
	for (FSched& S : Sched)
	{
		if (S.bOn) continue;
		S.bOn = true; S.At = FMath::Max(At, Clock); S.Inst = I; S.Var = Var; S.Layer = Layer; S.Mood = MoodIdx;
		S.Midi = Midi; S.Vel = Vel; S.Dur = Dur; S.Pan = Pan; S.XF = XF; S.XFRate = XFRate;
		return;
	}
}

void FDmMusic::DebugNote(EDmInst I, uint8 Var, float Midi, float Vel, float Gate)
{
	PlayNote(I, Var, ELayer::Stinger, Midi, Vel, Gate, 0.f);
}

// ------------------------------------------------------------------------------------------------ score

void FDmMusic::NewChord()
{
	Rescale();
	const bool bFirst = ChordEnd <= 0.0;
	const double Len = bGenesis ? Rng.Range(20.f, 30.f) : Rng.Range(16.f, 32.f);
	if (!bFirst && !bGenesis && !MoodOn(EDmMood::Plague))
	{
		if (CurAge <= 1) ChordRoot = ChordRoot == 0 ? 4 : 0;          // drone ages: 1 <-> 5
		else
		{
			switch (ChordRoot)
			{
			case 0: ChordRoot = Rng.Chance(0.5f) ? 3 : 4; break;     // 1 -> 4 or 5
			case 3: case 4: ChordRoot = 5; break;                    // -> 6
			default: ChordRoot = 0; break;                           // -> 1
			}
		}
	}
	if (bGenesis || MoodOn(EDmMood::Plague)) ChordRoot = 0;
	ChordStart = Clock;
	ChordEnd = Clock + Len;
	NextChordAt = ChordEnd;
	bChordForce = false;
	Ground(Clock, Len);
}

void FDmMusic::Ground(double T0, double Len)
{
	// release whatever ground is held (its own release crossfades into the new one)
	for (FNote& N : Notes)
		if (N.bOn && !N.bStinger && N.XFRate >= 0.f && (N.Layer == ELayer::Ground || (N.Layer == ELayer::Colour && N.Gate > 6.f) || N.Layer == ELayer::Fx) && N.Inst != EDmInst::Bell)
			N.Gate = FMath::Min(N.Gate, N.T + 0.3f);
	const float Gate = float(Len) + 1.0f;
	const int32 R = ChordRoot;
	const bool bFresh = Clock - AgeChangedAt < 1.0;
	const float XF0 = bFresh ? 0.f : 1.f, XFR = bFresh ? 1.f / 12.f : 0.f;
	const float BeatS = Beat();
	auto G = [&](EDmInst I, uint8 Var, float Midi, float Vel, float GateS, float Pan, ELayer Ly = ELayer::Ground, int8 Md = -1)
	{
		PlayNote(I, Var, Ly, Midi, Vel, GateS, Pan, Md, XF0, XFR);
	};

	if (bGenesis)
	{
		// a held breath: tonic and 5th, one more partial per genesis step (3rd, then 9th, then 6th)
		const int32 Ph = FMath::Clamp(Prm.Phase, 0, 3);
		G(EDmInst::Breath, 2, Harm.Midi(0, 2), 0.9f, Gate, -0.2f);
		G(EDmInst::Breath, 2, Harm.Midi(4, 2), 0.6f, Gate, 0.2f);
		if (Ph >= 1) G(EDmInst::Breath, 2, Harm.Midi(2, 3), 0.40f, Gate, 0.f);
		if (Ph >= 2) G(EDmInst::Breath, 2, Harm.Midi(8, 3), 0.30f, Gate, -0.35f);
		if (Ph >= 3) G(EDmInst::Breath, 2, Harm.Midi(5, 3), 0.28f, Gate, 0.35f);
		return;
	}

	const bool bPlague = MoodOn(EDmMood::Plague), bDoom = MoodOn(EDmMood::Doom);
	const bool bGolden = Mood(EDmMood::Golden) * (1.f - FMath::Max(Mood(EDmMood::War), Mood(EDmMood::Doom))) > 0.5f;
	int32 Degs[3] = { R, R + 4, R + 2 };
	int32 NumDeg = bGolden ? 3 : 2;
	if (bPlague) { Degs[0] = 0; NumDeg = 1; }

	if (bDoom)
	{
		// the ground becomes a cluster: tonic plus a minor 2nd, beating at about 3 Hz
		G(EDmInst::Cluster, 0, Harm.Midi(0, 3), 0.9f, Gate, 0.f);
	}
	else
	{
		switch (CurAge)
		{
		case 0: G(EDmInst::Breath, 0, Harm.Midi(0, 2), 1.f, Gate, 0.f); break;                         // a hummed drone on the tonic
		case 1: G(EDmInst::Breath, 0, Harm.Midi(bPlague ? 0 : R, 2), 1.f, Gate, 0.f); break;           // moves 1 <-> 5
		case 2: for (int32 J = 0; J < NumDeg; ++J) G(EDmInst::Cello, 0, Harm.Midi(Degs[J], 2), 1.f, Gate, J == 0 ? -0.2f : 0.2f); break;   // a bowed fifth
		case 3: for (int32 J = 0; J < NumDeg; ++J) G(EDmInst::Strings, 1, Harm.Midi(Degs[J], 3), 1.f, Gate, J == 0 ? -0.3f : (J == 1 ? 0.3f : 0.f)); break;
		case 4: for (int32 J = 0; J < NumDeg; ++J) G(EDmInst::Strings, 2, Harm.Midi(Degs[J], 3), 1.f, Gate, J == 0 ? -0.3f : (J == 1 ? 0.3f : 0.f)); break;
		case 5: for (int32 J = 0; J < NumDeg; ++J) G(EDmInst::Strings, 3, Harm.Midi(Degs[J], J == 0 ? 2 : 3), 1.f, Gate, (J % 2) ? 0.2f : -0.2f); break;
		case 6: G(EDmInst::Breath, 1, Harm.Midi(0, 3), 1.f, Gate, 0.f); break;                        // sub-free: an octave up
		default: break;
		}
	}

	// colour held on chord tones (ages 4-6)
	if (CurAge == 4 && !bPlague)
	{
		for (int32 J = 0; J < 2; ++J)
			Schedule(T0 + 2.0 * BeatS + 0.1 * J, EDmInst::Brass, 0, ELayer::Colour, Harm.Midi(R + 2 + 2 * J, 3), 0.55f, 8.f * BeatS, J ? 0.25f : -0.25f, -1, XF0, XFR);
		if (Len > 12.0)
			for (int32 J = 0; J < 2; ++J)
				Schedule(T0 + Len * 0.55 + 0.12 * J, EDmInst::Brass, 0, ELayer::Colour, Harm.Midi(R + 4 * J, 3), 0.45f, 6.f * BeatS, J ? 0.25f : -0.25f, -1, XF0, XFR);
	}
	else if (CurAge == 5)
	{
		static const float Pans[3] = { -0.35f, 0.f, 0.35f };
		for (int32 J = 0; J < 3; ++J) Schedule(T0 + 1.0, EDmInst::Organ, 0, ELayer::Colour, Harm.Midi(R + 2 * J, 4), 0.5f, float(Len) - 1.5f, Pans[J], -1, XF0, XFR);
		if (!bDoom)
			for (int32 J = 0; J < 2; ++J)
				Schedule(T0 + Len * 0.4 + 0.1 * J, EDmInst::Brass, 0, ELayer::Colour, Harm.Midi(R + 2 + 2 * J, 3), 0.5f, 6.f * BeatS, J ? 0.25f : -0.25f, -1, XF0, XFR);
	}
	else if (CurAge == 6)
	{
		static const float Pans[3] = { -0.4f, 0.4f, 0.f };
		for (int32 J = 0; J < 3; ++J) PlayNote(EDmInst::Pad, 0, ELayer::Colour, Harm.Midi(R + 2 * J, J == 0 ? 3 : 4), 0.8f, float(Len), Pans[J], -1, XF0, XFR);
	}

	// mood layers on the ground
	if (Mood(EDmMood::War) > 0.02f)
	{
		// a low bowed pedal on degree 5, and the darker layer: a low string on the tonic
		PlayNote(EDmInst::Cello, 1, ELayer::Fx, Harm.Midi(4, 2), 0.45f, Gate, 0.15f, int8(EDmMood::War));
		PlayNote(EDmInst::Cello, 1, ELayer::Fx, Harm.Midi(0, 2), 0.30f, Gate, -0.15f, int8(EDmMood::War));
	}
	if (Mood(EDmMood::Doom) > 0.02f) PlayNote(EDmInst::Rafters, 0, ELayer::Fx, 60.f, 1.f, Gate, 0.f, int8(EDmMood::Doom));
}

void FDmMusic::Pulse(double T0)
{
	const float B = Beat();
	const int32 R = ChordRoot;
	const ELayer P = ELayer::Pulse;
	auto J = [this]() { return Rng.Range(-Jitter, Jitter); };
	switch (CurAge)
	{
	case 0: case 1:
	{
		// 2-4 hits per bar, never on every beat, one ghost hit at -14 dB
		static const float W[8] = { 5.f, 0.5f, 2.f, 1.f, 3.f, 0.6f, 2.f, 1.2f };
		bool Pick[8] = {};
		const int32 Hits = 2 + Rng.Int(3);
		for (int32 H = 0; H < Hits; ++H)
		{
			float Tot = 0.f;
			for (int32 S = 0; S < 8; ++S) Tot += Pick[S] ? 0.f : W[S];
			float U = Rng.Uni() * Tot;
			for (int32 S = 0; S < 8; ++S) { if (Pick[S]) continue; U -= W[S]; if (U <= 0.f) { Pick[S] = true; break; } }
		}
		if (Pick[0] && Pick[2] && Pick[4] && Pick[6]) Pick[6] = false;
		int32 Ghost = -1;
		for (int32 S = 0; S < 8; ++S)
		{
			if (Pick[S]) Schedule(T0 + S * B * 0.5 + J(), EDmInst::HandDrum, 0, P, 40.f, Rng.Range(0.75f, 1.f), 0.2f, -0.1f);
			else if (Ghost < 0 && Rng.Chance(0.3f)) Ghost = S;
		}
		if (Ghost >= 0) Schedule(T0 + Ghost * B * 0.5 + J(), EDmInst::HandDrum, 0, P, 40.f, 0.2f, 0.2f, 0.15f);
		if (CurAge == 1)
			for (int32 S = 1; S < 8; S += 2)
				if (Rng.Chance(0.45f)) Schedule(T0 + S * B * 0.5 + J(), EDmInst::HandDrum, 1, P, 40.f, Rng.Range(0.4f, 0.6f), 0.2f, 0.3f);
		break;
	}
	case 2:
		if ((Bar % 2) == 0) Schedule(T0, EDmInst::Taiko, 1, P, 40.f, 1.f, 0.3f, 0.f);
		for (int32 S = 0; S < 8; ++S)
			if (Rng.Chance(S % 2 == 0 ? 0.8f : 0.4f)) Schedule(T0 + S * B * 0.5 + Rng.Range(-0.01f, 0.01f), EDmInst::Perc, 0, P, 60.f, S % 2 == 0 ? 0.063f : 0.04f, 0.05f, 0.4f);
		break;
	case 3:
	{
		const int32 Pat[4] = { R, R + 4, R + 2, R + 4 };
		for (int32 S = 0; S < 4; ++S) Schedule(T0 + S * B + J(), EDmInst::Lyre, 1, P, Harm.Midi(Pat[S], 4), S == 0 ? 0.125f : 0.1f, 0.3f, -0.4f);
		break;
	}
	case 4:
		for (int32 S = 0; S < 8; ++S)
			Schedule(T0 + S * B * 0.5 + J(), EDmInst::Lyre, 2, P, Harm.Midi(S % 2 == 0 ? R : R + 4, 3), S % 4 == 0 ? 0.1f : 0.075f, 0.2f, S % 2 ? 0.35f : -0.35f);
		break;
	case 5:
		for (int32 S = 0; S < 4; ++S) Schedule(T0 + S * B, EDmInst::Perc, 1, P, 60.f, 0.04f, 0.05f, 0.5f);
		if (!MoodOn(EDmMood::Doom))
		{
			Schedule(T0, EDmInst::Perc, 2, P, 60.f, 0.1f, 0.2f, 0.f);
			Schedule(T0 + 0.28 * B, EDmInst::Perc, 2, P, 60.f, 0.065f, 0.2f, 0.f);
		}
		break;
	case 6:
	{
		const int32 Pat[4] = { R, R + 2, R + 4, R + 7 };
		for (int32 S = 0; S < 4; ++S) Schedule(T0 + S * B * 0.5, EDmInst::Perc, 3, P, Harm.Midi(Pat[S], 5), 0.08f, 0.1f, -0.5f + 0.33f * S);
		break;
	}
	default: break;
	}
	// war: a low drum on every bar
	if (Mood(EDmMood::War) > 0.02f) Schedule(T0, EDmInst::Taiko, 0, ELayer::Fx, 40.f, 0.9f, 0.3f, 0.f, int8(EDmMood::War));
}

void FDmMusic::MakePhrase(int32 Lo, int32 Hi, bool bShort, bool bSingle, FPhrase& Out)
{
	FRng& R = MelRng;
	const FDmScale& VS = Scale;
	const int32 S3 = VS.IndexOf(VS.Count == 7 ? VS.Steps[2] : 4), S5 = VS.IndexOf(7);
	const int32 Starts[3] = { 0, S3, S5 };
	const int32 Ends[3] = { 0, VS.IndexOf(2), S5 };
	int32 N = bShort ? 2 + R.Int(3) : 2 + R.Int(6);
	if (bSingle) N = 1;
	N = FMath::Min(N, MaxPhrase);
	int32 D = Starts[R.Int(3)];
	while (D < Lo) D += VS.Count;
	while (D > Hi) D -= VS.Count;
	Out.N = N;
	Out.Deg[0] = D;
	bool bLeap = false;
	static const int32 Moves[6] = { -1, 1, -2, 2, -1, 1 };
	for (int32 I = 1; I < N; ++I)
	{
		int32 Mv = Moves[R.Int(6)];
		if (!bLeap && R.Chance(0.18f)) { Mv = (R.Chance(0.5f) ? -1 : 1) * (3 + R.Int(VS.Count == 7 ? 3 : 1)); bLeap = true; }
		int32 Nd = Out.Deg[I - 1] + Mv;
		if (Nd < Lo || Nd > Hi) Nd = Out.Deg[I - 1] - Mv;
		Out.Deg[I] = FMath::Clamp(Nd, Lo, Hi);
	}
	if (N >= 2)
	{
		// end on 1, 2 or 5, the candidate nearest the previous note
		const int32 Last = Out.Deg[N - 2];
		int32 Best = Out.Deg[N - 1], BestD = 999;
		for (int32 E = 0; E < 3; ++E)
			for (int32 O = -2; O <= 2; ++O)
			{
				const int32 C = Ends[E] + VS.Count * O;
				if (C < Lo || C > Hi) continue;
				const int32 Dist = FMath::Abs(C - Last) * 2 + (C > Last ? 1 : 0);
				if (Dist < BestD) { BestD = Dist; Best = C; }
			}
		Out.Deg[N - 1] = Best;
	}
	static const float Lens[4] = { 1.f, 1.f, 2.f, 3.f };
	for (int32 I = 0; I < N; ++I) Out.Len[I] = Lens[R.Int(4)];
	Out.Len[N - 1] = FMath::Max(Out.Len[N - 1], 2.f);
	if (bSingle) Out.Len[0] = R.Range(5.f, 8.f);
}

bool FDmMusic::Transposed(const FPhrase& In, int32 Lo, int32 Hi, FPhrase& Out) const
{
	// the fourth phrase repeats the first a 4th or 5th away
	static const int32 Opt7[4] = { 3, 4, -3, -4 };
	static const int32 Opt5[2] = { 3, -2 };
	const int32* Opt = Scale.Count == 7 ? Opt7 : Opt5;
	const int32 NOpt = Scale.Count == 7 ? 4 : 2;
	for (int32 K = 0; K < NOpt; ++K)
	{
		int32 Mn = 999, Mx = -999;
		for (int32 I = 0; I < In.N; ++I) { Mn = FMath::Min(Mn, In.Deg[I] + Opt[K]); Mx = FMath::Max(Mx, In.Deg[I] + Opt[K]); }
		if (Mn >= Lo && Mx <= Hi)
		{
			Out = In;
			for (int32 I = 0; I < In.N; ++I) Out.Deg[I] = In.Deg[I] + Opt[K];
			return true;
		}
	}
	Out = In;
	return false;
}

double FDmMusic::PlacePhrase(const FPhrase& Ph, double T0, EDmInst Inst, uint8 Var, int32 Oct)
{
	const float B = Beat();
	double T = T0;
	const bool bDoom = MoodOn(EDmMood::Doom);
	static const float Rel[int32(EDmInst::Count)] = { 0.4f, 0.2f, 0.9f, 0.3f, 0.9f, 1.5f, 0.5f, 0.5f, 3.f, 2.f, 1.f, 0.3f, 1.f, 1.f, 1.f, 0.1f, 1.f, 1.f };
	double LastOff = T0;
	for (int32 I = 0; I < Ph.N; ++I)
	{
		const double On = T + Rng.Range(-Jitter, Jitter);
		float Gate = Ph.Len[I] * B * 0.92f;
		if (bDoom) Gate = Ph.Len[I] * B;
		const float Vel = (I == 0 ? 0.85f : 0.7f) * Rng.Range(0.85f, 1.f);
		const float Midi = Scale.Midi(Ph.Deg[I], Oct);
		const float Pan = Rng.Range(-0.15f, 0.15f);
		Schedule(On, Inst, Var, ELayer::Voice, Midi, Vel, Gate, Pan);
		if (Inst == EDmInst::Piano && Var == 1) Schedule(On, EDmInst::Piano, 0, ELayer::Voice, Midi - 12.f, Vel * 0.6f, Gate, -Pan);   // phrases in octaves
		LastOff = FMath::Max(LastOff, On + Gate + 0.5f * Rel[int32(Inst)]);
		T += Ph.Len[I] * B;
	}
	PhraseEnd = LastOff;
	return T;
}

void FDmMusic::Answer(double TEnd, const FPhrase& Ph)
{
	const float B = Beat();
	const int32 R = ChordRoot;
	const int32 Tri[3] = { R, R + 2, R + 4 };
	const double T = TEnd + B;
	const bool bFull = Mood(EDmMood::Golden) * (1.f - FMath::Max(Mood(EDmMood::War), Mood(EDmMood::Doom))) > 0.5f;
	const ELayer C = ELayer::Colour;
	if (CurAge == 1 || CurAge == 2)
	{
		// the lyre answers one beat after the phrase: a 3-note strum, 25 ms apart, low to high
		for (int32 I = 0; I < 3; ++I) Schedule(T + 0.025 * I, EDmInst::Lyre, 0, C, Harm.Midi(Tri[I], 4), 0.6f, 0.5f, -0.3f + 0.3f * I);
		if (CurAge == 2)
		{
			const int32 Times = 1 + Rng.Int(2);
			for (int32 K = 0; K < Times; ++K)
			{
				const double TT = T + B * (2 + 2 * K);
				Schedule(TT, EDmInst::Lyre, 0, C, Harm.Midi(R, 3) + 12.f, 0.5f, 0.5f, 0.2f);
				Schedule(TT, EDmInst::Lyre, 0, C, Harm.Midi(R + 4, 4), 0.5f, 0.5f, 0.2f);
			}
		}
		else if (Ph.N > 0) Schedule(T + 2.0 * B, EDmInst::Lyre, 0, C, Scale.Midi(Ph.Deg[Ph.N - 1], 4), 0.45f, 0.5f, 0.35f);
	}
	else if (CurAge == 3)
	{
		// bells answer on chord tones (full chords in a golden age)
		if (bFull) for (int32 I = 0; I < 3; ++I) Schedule(T + 0.06 * I, EDmInst::Bell, 0, C, Harm.Midi(Tri[I], 4), 0.55f, 3.f, Rng.Range(-0.5f, 0.5f));
		else
		{
			Schedule(T, EDmInst::Bell, 0, C, Harm.Midi(Tri[Rng.Int(3)], 4), 0.55f, 3.f, Rng.Range(-0.5f, 0.5f));
			if (Rng.Chance(0.6f)) Schedule(T + 2.0 * B, EDmInst::Bell, 0, C, Harm.Midi(Tri[Rng.Int(3)], 4), 0.4f, 3.f, Rng.Range(-0.5f, 0.5f));
		}
	}
}

void FDmMusic::StartPhrase()
{
	const FVoiceCfg& Cfg = GVoice[FMath::Clamp(CurAge, 0, 6)];
	const bool bShort = MoodOn(EDmMood::War), bSingle = MoodOn(EDmMood::Doom);
	FPhrase Ph;
	if (bHandOff && LastPhrase.N > 0)
	{
		// the new voice's first phrase is the old age's last phrase, on the new instrument
		Ph = LastPhrase;
		for (int32 I = 0; I < Ph.N; ++I)
		{
			while (Ph.Deg[I] < Cfg.Lo) Ph.Deg[I] += Scale.Count;
			while (Ph.Deg[I] > Cfg.Hi) Ph.Deg[I] -= Scale.Count;
			Ph.Deg[I] = FMath::Clamp(Ph.Deg[I], Cfg.Lo, Cfg.Hi);
		}
		bHandOff = false;
	}
	else if (PhraseK % 4 == 3 && Group0.N > 0) Transposed(Group0, Cfg.Lo, Cfg.Hi, Ph);
	else MakePhrase(Cfg.Lo, Cfg.Hi, bShort, bSingle, Ph);
	if (PhraseK % 4 == 0) Group0 = Ph;

	EDmInst Inst = Cfg.Inst;
	uint8 Var = Cfg.Var;
	int32 Oct = Cfg.Oct;
	if (CurAge == 6 && PhraseK % 4 == 2) { Inst = EDmInst::Flute; Var = 0; Oct = 5; }   // the bone flute returns, one phrase in four
	else if (CurAge == 6 && Scale.Count == 7 && Ph.N > 1 && MelRng.Chance(0.7f))
	{
		// Lydian: the raised 4th is the wonder note, at the top of the phrase
		int32 Top = 0;
		for (int32 I = 1; I < Ph.N; ++I) if (Ph.Deg[I] > Ph.Deg[Top]) Top = I;
		int32 Best = Ph.Deg[Top], BestD = 99;
		for (int32 O = -1; O <= 1; ++O) { const int32 C = 3 + Scale.Count * O; if (FMath::Abs(C - Ph.Deg[Top]) < BestD) { BestD = FMath::Abs(C - Ph.Deg[Top]); Best = C; } }
		Ph.Deg[Top] = Best;
	}
	const double Start = Clock + 0.02;
	const double End = PlacePhrase(Ph, Start, Inst, Var, Oct);
	Answer(End, Ph);
	const double Sounded = PhraseEnd - Start;
	float RestMul = MoodOn(EDmMood::War) ? 1.5f : 1.f;
	if (Zoom01 < 0.2f) RestMul *= 2.5f;    // far out: very sparse
	NextPhraseAt = FMath::Max(End + double(MelRng.Range(4.f, 12.f)) * RestMul, Start + Sounded * 2.15);
	LastPhrase = Ph;
	++PhraseK;
	++PhrasesPlayed;
}

void FDmMusic::ChangeAge(int32 NewAge)
{
	CurAge = FMath::Clamp(NewAge, 0, 6);
	PendingAge = -1;
	AgeChangedAt = Clock;
	{
		FRng R(SeedNow * 2246822519u + uint32(CurAge) * 3266489917u + 17u);
		R.Next();
		TempoBase = R.Range(GTempo[CurAge][0], GTempo[CurAge][1]);
	}
	Jitter = GJitter[CurAge];
	// the old ground, colour and pulse fade out over 12 s while the new ground fades in
	for (FNote& N : Notes)
		if (N.bOn && !N.bStinger && N.Layer != ELayer::Voice) { N.XFRate = -1.f / 12.f; N.Gate = FMath::Max(N.Gate, N.T + 12.f); }
	for (FSched& S : Sched)
		if (S.bOn && S.Layer != ELayer::Stinger && S.Layer != ELayer::Voice) S.bOn = false;
	Rescale();
	if (!bGenesis && !bEnded)
	{
		if (Clock >= NextStingerOk) PlayStingerNow(EDmStinger::Age);
		else
		{
			for (int32 I = MaxPending - 1; I > 0; --I) Pending[I] = Pending[I - 1];
			Pending[0].bOn = true; Pending[0].S = EDmStinger::Age; Pending[0].At = Clock;
		}
	}
	bHandOff = LastPhrase.N > 0;
	// the new ground (Ground() fades it in over 12 s because AgeChangedAt is now)
	if (ChordEnd <= 0.0) ChordEnd = 1.0;
	NewChord();
	NextPhraseAt = FMath::Max(NextPhraseAt, Clock + 6.5);
	NextBarAt = Clock + 6.0;
}

void FDmMusic::Score()
{
	ScoreTime = Clock;
	// stinger queue: one every 6 s; a stinger more than 20 s late is dropped (endings never are)
	if (Clock >= NextStingerOk)
	{
		int32 Oldest = -1;
		for (int32 I = 0; I < MaxPending; ++I)
			if (Pending[I].bOn && (Oldest < 0 || Pending[I].At < Pending[Oldest].At)) Oldest = I;
		if (Oldest >= 0)
		{
			FPending P = Pending[Oldest];
			Pending[Oldest].bOn = false;
			const bool bEnding = P.S >= EDmStinger::EndingBeloved;
			if (!bEnding && Clock - P.At > 20.0) ++StingersDropped;
			else PlayStingerNow(P.S);
		}
	}

	if (!Prm.bScore || bEnded)
	{
		// the score falls silent (stingers still play): release whatever it holds
		for (FNote& N : Notes) if (N.bOn && !N.bStinger && !N.bReleased) N.Gate = FMath::Min(N.Gate, N.T);
		for (FSched& S : Sched) if (S.bOn && S.Layer != ELayer::Stinger) S.bOn = false;
		NextChordAt = Clock;
		return;
	}
	if (PendingAge >= 0 && Clock >= PhraseEnd) ChangeAge(PendingAge);
	if (Clock >= NextChordAt || bChordForce) NewChord();
	if (bGenesis) return;
	if (Clock >= NextBarAt)
	{
		const double T0 = FMath::Max(NextBarAt, Clock);
		Pulse(T0);
		NextBarAt = T0 + 4.0 * double(Beat());
		++Bar;
	}
	if (Clock >= NextPhraseAt && PendingAge < 0) StartPhrase();

	// plague: a distant toll every 16-24 s at -10 dB re music
	if (Mood(EDmMood::Plague) > 0.05f)
	{
		if (Clock >= NextTollAt)
		{
			Schedule(Clock, EDmInst::Bell, 1, ELayer::Fx, Harm.Midi(0, 3), 0.32f, 6.f, 0.2f, int8(EDmMood::Plague));
			NextTollAt = Clock + Rng.Range(16.f, 24.f);
		}
	}
	else NextTollAt = Clock + Rng.Range(2.f, 5.f);
	// golden age: a high sparkle every 3-6 s at -24 dB
	if (Mood(EDmMood::Golden) * (1.f - FMath::Max(Mood(EDmMood::War), Mood(EDmMood::Doom))) > 0.05f)
	{
		if (Clock >= NextSparkleAt)
		{
			Schedule(Clock, EDmInst::Bell, 2, ELayer::Fx, 69.f + 12.f * std::log2(Rng.Range(2000.f, 4000.f) / 440.f), 0.07f, 2.f, Rng.Range(-0.7f, 0.7f), int8(EDmMood::Golden));
			NextSparkleAt = Clock + Rng.Range(3.f, 6.f);
		}
	}
	else NextSparkleAt = Clock + Rng.Range(1.f, 3.f);
}

// ------------------------------------------------------------------------------------------------ stingers

bool FDmMusic::Stinger(EDmStinger S)
{
	const bool bEnding = S >= EDmStinger::EndingBeloved;
	if (S == EDmStinger::Age && (PendingAge >= 0 || Clock - AgeChangedAt < 15.0)) return true;   // the age change plays it
	if (S == EDmStinger::Founding && Clock - LastFounding < 30.0) { ++StingersDropped; return false; }
	if (S == EDmStinger::Wrath && Clock - LastWrath < 20.0) { ++StingersDropped; return false; }
	if (S == EDmStinger::Miracle && Clock - LastMiracle < 20.0) { ++StingersDropped; return false; }
	for (int32 I = 0; I < MaxPending; ++I) if (Pending[I].bOn && Pending[I].S == S) return true;   // already waiting
	if (bEnding)
	{
		for (FPending& P : Pending) P.bOn = false;
		PlayStingerNow(S);
		return true;
	}
	if (S == EDmStinger::Founding) LastFounding = Clock;
	if (S == EDmStinger::Wrath) LastWrath = Clock;
	if (S == EDmStinger::Miracle) LastMiracle = Clock;
	int32 Slot = -1;
	for (int32 I = 0; I < MaxPending; ++I) if (!Pending[I].bOn) { Slot = I; break; }
	if (Slot < 0)
	{
		// full: the oldest waiting one gives way
		Slot = 0;
		for (int32 I = 1; I < MaxPending; ++I) if (Pending[I].At < Pending[Slot].At) Slot = I;
		++StingersDropped;
	}
	Pending[Slot].bOn = true; Pending[Slot].S = S; Pending[Slot].At = Clock;
	return true;
}

void FDmMusic::PlayStingerNow(EDmStinger S)
{
	const int32 A = FMath::Clamp(CurAge, 0, 6);
	const bool bDark = S == EDmStinger::Wrath || S == EDmStinger::EndingFeared;
	const FDmScale Sc = bDark ? Scale.Darkened() : Scale;
	const FDmScale H = bDark ? Harm.Darkened() : Harm;
	const int32 C = Sc.Count;
	const int32 F5 = Sc.IndexOf(7);
	const ELayer St = ELayer::Stinger;
	const FVoiceCfg& V = GVoice[A];
	const double T = Clock;
	float Len = 5.f;
	float SV = 1.f;   // stinger velocity scale (the six-instrument ending is kept under -18 LUFS short-term)
	auto Sch = [&](double At, EDmInst I, uint8 Var, float Midi, float Vel, float Dur, float Pan = 0.f) { Schedule(T + At, I, Var, St, Midi, Vel * SV, Dur, Pan); };
	// the colour instrument of an age, for stingers
	auto Colour = [&](double At, float Midi, float Vel, float Dur, float Pan)
	{
		switch (A)
		{
		case 0: Sch(At, EDmInst::Breath, 0, Midi, Vel, Dur, Pan); break;
		case 1: case 2: Sch(At, EDmInst::Lyre, 0, Midi, Vel, Dur, Pan); break;
		case 3: Sch(At, EDmInst::Bell, 0, Midi, Vel, Dur, Pan); break;
		case 4: Sch(At, EDmInst::Brass, 0, Midi, Vel, Dur, Pan); break;
		case 5: Sch(At, EDmInst::Organ, 0, Midi, Vel, Dur, Pan); break;
		default: Sch(At, EDmInst::Pad, 0, Midi, Vel, Dur, Pan); break;
		}
	};
	switch (S)
	{
	case EDmStinger::Age:
	{
		// the age motif (1-5-8) on the new voice, the colour holding 3 and 5, the ground swelling under it
		const int32 Oct = V.Oct;
		Sch(0.0, V.Inst, 0, Sc.Midi(0, Oct), 0.75f, 0.5f, -0.1f);
		Sch(0.45, V.Inst, 0, Sc.Midi(F5, Oct), 0.75f, 0.5f, 0.f);
		Sch(0.9, V.Inst, 0, Sc.Midi(C, Oct), 0.8f, 1.8f, 0.1f);
		const bool bPluck = A == 1 || A == 2 || A == 3;
		Colour(1.0, H.Midi(2, 4), 0.55f, bPluck ? 2.5f : 3.5f, -0.3f);
		Colour(1.0, H.Midi(4, 4), 0.5f, bPluck ? 2.5f : 3.5f, 0.3f);
		if (bPluck) { Colour(2.4, H.Midi(2, 4), 0.4f, 2.f, -0.3f); Colour(2.4, H.Midi(4, 4), 0.35f, 2.f, 0.3f); }
		Sch(0.0, EDmInst::Breath, 0, H.Midi(0, 2), 1.f, 4.f, 0.f);
		Len = 6.f;
		break;
	}
	case EDmStinger::Founding:
	{
		// small and wry: the colour plays 5-6-5 softly
		const int32 Oct = 4;
		Colour(0.0, Sc.Midi(F5, Oct), 0.45f, 0.35f, -0.15f);
		Colour(0.35, Sc.Midi(F5 + 1, Oct), 0.45f, 0.35f, 0.f);
		Colour(0.7, Sc.Midi(F5, Oct), 0.5f, 1.0f, 0.15f);
		Len = 2.5f;
		LastFounding = Clock;
		break;
	}
	case EDmStinger::Miracle:
	{
		// a bright open chord (1-5-9-3, two octaves up) as a bell cluster, a choir at 1 and 5
		const int32 Ch[4] = { 0, 4, 8, 9 };
		for (int32 I = 0; I < 4; ++I) Sch(0.06 * I, EDmInst::Bell, 0, H.Midi(Ch[I], 4), 0.5f, 3.5f, -0.45f + 0.3f * I);   // two octaves above the D2-G2 tonic
		Sch(0.1, EDmInst::Choir, 0, H.Midi(0, 3), 0.6f, 4.f, -0.3f);
		Sch(0.1, EDmInst::Choir, 0, H.Midi(4, 3), 0.55f, 4.f, 0.3f);
		Len = 5.f;
		LastMiracle = Clock;
		break;
	}
	case EDmStinger::Wrath:
	{
		// a low unison on 1 and b2 for 1 s, then a falling minor 3rd in the brass (or the reed before the sail age)
		Sch(0.0, EDmInst::Cello, 0, H.Midi(0, 2), 0.85f, 1.0f, -0.2f);
		Sch(0.0, EDmInst::Cello, 0, H.Midi(0, 2) + 1.f, 0.6f, 1.0f, 0.2f);
		const EDmInst Lead = A >= 3 ? EDmInst::Brass : EDmInst::Reed;
		Sch(1.2, Lead, 0, H.Midi(0, 3) + 3.f, 0.6f, 1.2f, 0.f);
		Sch(2.4, Lead, 0, H.Midi(0, 3), 0.55f, 2.0f, 0.f);
		Len = 5.f;
		LastWrath = Clock;
		break;
	}
	case EDmStinger::EndingBeloved:
	{
		// the age motif by every instrument of every age in turn, held in a final Lydian chord with the 3rd on top
		SV = 0.55f;
		FDmScale Ly = Scale; Ly.SetMode(FDmScale::Lydian);
		const EDmInst Inst[6] = { EDmInst::Flute, EDmInst::Lyre, EDmInst::Reed, EDmInst::Strings, EDmInst::Piano, EDmInst::Glass };
		const int32 Oct[6] = { 5, 4, 4, 4, 4, 5 };
		for (int32 K = 0; K < 6; ++K)
		{
			const double T0 = 2.0 * K;
			const float Hold = FMath::Max(1.0f, float(14.0 - T0));
			const float Pan = -0.5f + 0.2f * K;
			Sch(T0, Inst[K], 0, Ly.Midi(0, Oct[K]), 0.55f, 0.45f, Pan);
			Sch(T0 + 0.5, Inst[K], 0, Ly.Midi(4, Oct[K]), 0.55f, 0.45f, Pan);
			Sch(T0 + 1.0, Inst[K], 0, Ly.Midi(7, Oct[K]), 0.5f, Inst[K] == EDmInst::Lyre || Inst[K] == EDmInst::Piano ? 1.f : Hold, Pan);
		}
		Sch(10.0, EDmInst::Strings, 1, Ly.Midi(0, 3), 0.7f, 6.f, -0.3f);
		Sch(10.0, EDmInst::Strings, 1, Ly.Midi(4, 3), 0.6f, 6.f, 0.3f);
		Sch(10.0, EDmInst::Strings, 1, Ly.Midi(9, 3), 0.55f, 6.f, 0.f);    // the 3rd on top
		// the bone flute ends it alone
		Sch(16.5, EDmInst::Flute, 0, Ly.Midi(0, 5), 0.5f, 0.5f, 0.f);
		Sch(17.0, EDmInst::Flute, 0, Ly.Midi(4, 5), 0.5f, 0.5f, 0.f);
		Sch(17.5, EDmInst::Flute, 0, Ly.Midi(7, 5), 0.45f, 1.8f, 0.f);
		Len = 20.f;
		break;
	}
	case EDmStinger::EndingFeared:
	{
		// the motif descending (8-5-1) in the low brass over a doom cluster; the launch roar far away; unresolved on 2
		Sch(0.0, EDmInst::Cluster, 0, H.Midi(0, 3), 0.8f, 10.f, 0.f);
		Sch(0.5, EDmInst::Brass, 0, H.Midi(7, 2), 0.55f, 1.2f, -0.2f);
		Sch(1.8, EDmInst::Brass, 0, H.Midi(4, 2), 0.55f, 1.2f, 0.f);
		Sch(3.1, EDmInst::Brass, 0, H.Midi(0, 2), 0.5f, 2.6f, 0.2f);
		Sch(7.5, EDmInst::Brass, 0, H.Midi(1, 3), 0.45f, 5.f, 0.f);
		Sch(7.5, EDmInst::Cello, 1, H.Midi(1, 2), 0.4f, 7.f, 0.f);
		EndingReq = int32(S);
		Len = 18.f;
		break;
	}
	case EDmStinger::EndingForgotten:
	{
		// the motif once on the glass lead, then with one note missing, then with two; a lone tonic fading over 6 s
		Sch(0.0, EDmInst::Glass, 0, Sc.Midi(0, 5), 0.45f, 0.5f, -0.2f);
		Sch(0.6, EDmInst::Glass, 0, Sc.Midi(F5, 5), 0.45f, 0.5f, 0.f);
		Sch(1.2, EDmInst::Glass, 0, Sc.Midi(C, 5), 0.42f, 1.2f, 0.2f);
		Sch(4.0, EDmInst::Glass, 0, Sc.Midi(0, 5), 0.38f, 0.5f, -0.2f);
		Sch(5.2, EDmInst::Glass, 0, Sc.Midi(C, 5), 0.35f, 1.2f, 0.2f);
		Sch(8.0, EDmInst::Glass, 0, Sc.Midi(0, 5), 0.3f, 1.2f, 0.f);
		Sch(9.0, EDmInst::Pad, 0, H.Midi(0, 3), 0.6f, 1.5f, 0.f);
		Len = 16.f;
		break;
	}
	case EDmStinger::EndingParting:
	{
		// the bone flute and the glass lead in canon a beat apart, diverging, ending on an open 5th
		const double B = 0.8;
		const int32 Mot[3] = { 0, F5, C };
		for (int32 I = 0; I < 3; ++I)
		{
			Sch(B * I, EDmInst::Flute, 0, Sc.Midi(Mot[I], 5), 0.5f, 0.7f, -0.35f);
			Sch(B * (I + 1), EDmInst::Glass, 0, Sc.Midi(Mot[I], 5), 0.45f, 0.7f, 0.35f);
		}
		for (int32 I = 0; I < 4; ++I)
		{
			Sch(B * (4 + I), EDmInst::Flute, 0, Sc.Midi(C - 1 - I, 5), 0.45f - 0.05f * I, 0.7f, -0.45f);
			Sch(B * (5 + I), EDmInst::Glass, 0, Sc.Midi(C + 1 + I, 5), 0.4f - 0.05f * I, 0.7f, 0.45f);
		}
		Sch(8.5, EDmInst::Strings, 1, H.Midi(0, 3), 0.6f, 5.f, -0.3f);
		Sch(8.5, EDmInst::Strings, 1, H.Midi(4, 3), 0.55f, 5.f, 0.3f);
		Len = 18.f;
		break;
	}
	case EDmStinger::EndingSilence:
	case EDmStinger::EndingQuiet:
	case EDmStinger::EndingAsh:
	{
		// only the genesis drone, tonic and 5th; the 5th drops out at 8 s; then the wind alone (the engine)
		Sch(0.0, EDmInst::Breath, 2, H.Midi(0, 2), 0.9f, 12.f, 0.f);
		Sch(0.0, EDmInst::Breath, 2, H.Midi(4, 2), 0.6f, 5.f, 0.15f);
		EndingReq = int32(S);
		Len = 24.f;
		break;
	}
	default: break;
	}
	StingerUntil = Clock + Len;
	NextStingerOk = Clock + 6.0;
	++StingersPlayed;
}

// ------------------------------------------------------------------------------------------------ render

void FDmMusic::Render(float* L, float* R, float* HallL, float* HallR, float* SkyL, float* SkyR, int32 N)
{
	Score();
	const double BlockEnd = Clock + double(N) * double(InvSr);
	for (FSched& S : Sched)
		if (S.bOn && S.At < BlockEnd)
		{
			S.bOn = false;
			PlayNote(S.Inst, S.Var, S.Layer, S.Midi, S.Vel, S.Dur, S.Pan, S.Mood, S.XF, S.XFRate);
		}
	const int32 NN = FMath::Min(N, 256);
	FMemory::Memzero(EchoSendL, sizeof(float) * NN);
	FMemory::Memzero(EchoSendR, sizeof(float) * NN);
	bool bVoice = false;
	for (FNote& Nt : Notes)
	{
		if (!Nt.bOn) continue;
		if (Nt.Layer == ELayer::Voice && Nt.T < Nt.Gate + 0.1f && Nt.T > 0.f) bVoice = true;
		RenderNote(Nt, L, R, HallL, HallR, SkyL, SkyR, NN);
	}
	if (bVoice) VoiceBusy += double(NN) * InvSr;
	// the valley answers (Stone and Bronze): echoes at 380 and 760 ms, low-passed
	const int32 D1 = int32(0.38f * Sr), D2 = int32(0.76f * Sr);
	for (int32 K = 0; K < NN; ++K)
	{
		EchoL.Push(EchoSendL[K]); EchoR.Push(EchoSendR[K]);
		const float EL = EchoLpL.Tick(EchoL.Read(D1) * 0.5f + EchoL.Read(D2) * 0.2f);
		const float ER = EchoLpR.Tick(EchoR.Read(D1) * 0.5f + EchoR.Read(D2) * 0.2f);
		L[K] += ER; R[K] += EL;   // the answer comes back from the other side
	}
	Clock = BlockEnd;
}

void FDmMusic::RenderNote(FNote& Nt, float* L, float* R, float* HallL, float* HallR, float* SkyL, float* SkyR, int32 N)
{
	float Gl, Gr;
	PanGains(Nt.Pan, Gl, Gr);
	const int32 A = FMath::Clamp(CurAge, 0, 6);
	// layer and mood gains
	float LayerDb = 0.f;
	switch (Nt.Layer)
	{
	case ELayer::Colour: LayerDb = GLayerDb[A][0]; break;
	case ELayer::Ground: LayerDb = GLayerDb[A][1]; break;
	case ELayer::Pulse: LayerDb = GLayerDb[A][2]; break;
	default: break;
	}
	float MoodG = 1.f;
	if (Nt.Mood >= 0 && Nt.Mood < int32(EDmMood::Count))
	{
		MoodG = MoodW[Nt.Mood];
		if (Nt.Mood == int8(EDmMood::Golden)) MoodG *= 1.f - FMath::Max(MoodW[int32(EDmMood::War)], MoodW[int32(EDmMood::Doom)]);
	}
	const float BlockSec = float(N) * InvSr;
	if (Nt.XFRate != 0.f)
	{
		Nt.XF = Sat01(Nt.XF + Nt.XFRate * BlockSec);
		if (Nt.XF <= 0.f && Nt.XFRate < 0.f) { Nt.bOn = false; return; }
		if (Nt.XF >= 1.f && Nt.XFRate > 0.f) Nt.XFRate = 0.f;
	}
	const float Trim = DbToGain(GAgeTrimDb[A]);
	// (wave 4b verify) plague removes the pulse and thins the ground, so the score fell 1.5 dB under its -29 LUFS on every
	// seed tried (7, 8, 1234, 99: -30.3 .. -30.6): the rest of the score is lifted 1.2 dB while the plague mood is on
	const float PlagueLift = 1.f + (DbToGain(1.2f) - 1.f) * MoodW[int32(EDmMood::Plague)];
	const float Base = Nt.bStinger ? 2.f * Trim * OutGain : OutGain * StingDuck * ExtDuck * Trim * LayerG[int32(Nt.Layer)] * DbToGain(LayerDb) * MoodG * PlagueLift;
	const float Lvl = GLevel[int32(Nt.Inst)] * Nt.Vel * Nt.XF * Base;
	const float Hs = Nt.Hall, Ks = Nt.Sky;
	const float Ech = (Nt.Layer == ELayer::Voice && A <= 1) ? 0.25f : 0.f;
	// the plague mood: the voice is detuned -12 cents, drifting +/- 8 cents at 0.1 Hz
	const float PlagueC = Nt.Layer == ELayer::Voice ? MoodW[int32(EDmMood::Plague)] * (-12.f + 8.f * Sin01(float(Clock * 0.1))) : 0.f;
	const float Hz = Nt.Hz * CentsMul(PlagueC);
	const float Inc = Hz * InvSr;
	auto Out = [&](int32 K, float S)
	{
		const float SL = S * Gl, SR = S * Gr;
		L[K] += SL; R[K] += SR;
		HallL[K] += SL * Hs; HallR[K] += SR * Hs;
		SkyL[K] += SL * Ks; SkyR[K] += SR * Ks;
		if (Ech > 0.f) { EchoSendL[K] += SL * Ech; EchoSendR[K] += SR * Ech; }
	};
	if (Lvl <= 0.f && Nt.T > 0.5f && !Nt.bStinger && Nt.Layer != ELayer::Ground)
	{
		// silent (a thinned layer, a faded mood): just let time pass
		Nt.T += BlockSec;
		if (Nt.T > Nt.Gate + 1.f) Nt.bOn = false;
		return;
	}

	float Peak = 0.f;
	bool bPerc = false;
	switch (Nt.Inst)
	{
	case EDmInst::Flute:
	{
		// sine + 18 % 2nd + 6 % 3rd, breath at -20 dB, 80 ms / 0 / 1 / 400 ms, slides up into the note,
		// vibrato 4.5 Hz 12 cents after 300 ms
		const float RelMul = DecayMul(0.4f, Sr);
		const float Slide = Nt.Aux[0] * (1.f - SStep(0.f, 0.12f, Nt.T));
		const float VibD = 12.f * SStep(0.3f, 0.6f, Nt.T);
		const float BrA = SStep(0.f, 0.03f, Nt.T);
		for (int32 K = 0; K < N; ++K)
		{
			const float E = Adsr(Nt.Env, Nt.bReleased, Nt.T, Nt.Gate, 0.08f, 0.f, 1.f, RelMul);
			Nt.Ph[1] = Wrap01(Nt.Ph[1] + 4.5f * InvSr);
			Nt.Ph[0] = Wrap01(Nt.Ph[0] + Inc * CentsMul(Slide + VibD * Sin01(Nt.Ph[1])));
			const float P = Nt.Ph[0];
			const float Tone = Sin01(P) + 0.18f * Sin01(2.f * P) + 0.06f * Sin01(3.f * P);
			const float Br = Nt.F[0].Band(Nt.Pink.Tick(Nt.Rng.Bi())) * 0.4f * BrA;
			Out(K, (Tone + Br) * E * Lvl);
			Peak = FMath::Max(Peak, E);
			Nt.T += InvSr;
		}
		break;
	}
	case EDmInst::HandDrum:
	case EDmInst::Taiko:
	{
		// frame drum: a body 90 -> 70 Hz over 60 ms (2 ms / 220 ms) and a skin at 1.1 kHz (1 ms / 40 ms);
		// high drum 230 -> 180 Hz; war drum 70 Hz (3 ms / 300 ms); low drum 60 Hz, high-passed at 45 Hz
		bPerc = true;
		const bool bTaiko = Nt.Inst == EDmInst::Taiko;
		float F0, F1, Dec, Att;
		if (!bTaiko) { F0 = Nt.Var ? 230.f : 90.f; F1 = Nt.Var ? 180.f : 70.f; Dec = Nt.Var ? 0.16f : 0.22f; Att = 0.002f; }
		else if (Nt.Var == 0) { F0 = 100.f; F1 = 70.f; Dec = 0.3f; Att = 0.003f; }
		else { F0 = 85.f; F1 = 60.f; Dec = 0.5f; Att = 0.003f; }
		const float Mul = DecayMul(Dec, Sr), DropMul = std::exp(-InvSr / 0.02f), SkinMul = DecayMul(0.04f, Sr);
		for (int32 K = 0; K < N; ++K)
		{
			const float F = F1 + (F0 - F1) * Nt.Aux[1];
			Nt.Aux[1] *= DropMul;
			Nt.Ph[0] = Wrap01(Nt.Ph[0] + F * InvSr);
			const float Body = Sin01(Nt.Ph[0]) * Nt.Env * Sat01(Nt.T / Att);
			Nt.Env *= Mul;
			const float Skin = Nt.F[0].Band(Nt.Rng.Bi()) * Nt.Aux[2] * Sat01(Nt.T / 0.001f);
			Nt.Aux[2] *= SkinMul;
			float S = Body + Skin;
			if (bTaiko) S = Nt.Hp.Tick(S);
			Out(K, S * Lvl);
			Nt.T += InvSr;
		}
		Peak = Nt.Env;
		break;
	}
	case EDmInst::Lyre:
	{
		bPerc = true;
		const float Rho = Nt.KsRho;
		const float D = Nt.KsDelay;
		const int32 Mask = KsLen - 1;
		for (int32 K = 0; K < N; ++K)
		{
			const int32 I0 = int32(D);
			const float Fr = D - float(I0);
			const float Aa = Nt.Ks[(Nt.KsPos - I0) & Mask], Bb = Nt.Ks[(Nt.KsPos - I0 - 1) & Mask], Cc = Nt.Ks[(Nt.KsPos - I0 - 2) & Mask];
			const float X0 = Aa + (Bb - Aa) * Fr, X1 = Bb + (Cc - Bb) * Fr;
			const float Y = Rho * (0.5f * X0 + 0.5f * X1);
			Nt.Ks[Nt.KsPos & Mask] = Y;
			Nt.KsPos = (Nt.KsPos + 1) & Mask;
			Out(K, Nt.Lp.Tick(Y) * Lvl);
			Peak = FMath::Max(Peak, std::fabs(Y));
			Nt.T += InvSr;
		}
		if (Nt.T < 0.05f) Peak = 1.f;
		break;
	}
	case EDmInst::Reed:
	{
		// sawtooth through bands at 1.2 kHz (Q 2) and 2.6 kHz (Q 3), 1 : 0.4, low-passed at 3.5 kHz;
		// 50 ms / 100 ms / 0.8 / 300 ms; vibrato 5.5 Hz 15 cents
		const float RelMul = DecayMul(0.3f, Sr);
		const float VibD = 15.f * SStep(0.15f, 0.4f, Nt.T);
		for (int32 K = 0; K < N; ++K)
		{
			const float E = Adsr(Nt.Env, Nt.bReleased, Nt.T, Nt.Gate, 0.05f, 0.1f, 0.8f, RelMul);
			Nt.Ph[1] = Wrap01(Nt.Ph[1] + 5.5f * InvSr);
			const float I = Inc * CentsMul(VibD * Sin01(Nt.Ph[1]));
			Nt.Ph[0] = Wrap01(Nt.Ph[0] + I);
			const float Saw = BlepSaw(Nt.Ph[0], I);
			const float Y = Nt.F[2].Low(Nt.F[0].Band(Saw) + 0.4f * Nt.F[1].Band(Saw));
			Out(K, Y * E * Lvl);
			Peak = FMath::Max(Peak, E);
			Nt.T += InvSr;
		}
		break;
	}
	case EDmInst::Strings:
	{
		if (Nt.Var == 0)
		{
			// solo string: a low-pass following the envelope (800 Hz -> 2.5 kHz), a formant at 900 Hz;
			// 250 ms / 0 / 1 / 900 ms; vibrato 5 Hz 10 cents after 400 ms
			const float RelMul = DecayMul(0.9f, Sr);
			const float VibD = 10.f * SStep(0.4f, 0.7f, Nt.T);
			Nt.F[0].Set(800.f + 1700.f * Nt.Env, 0.7f, Sr);
			for (int32 K = 0; K < N; ++K)
			{
				const float E = Adsr(Nt.Env, Nt.bReleased, Nt.T, Nt.Gate, 0.25f, 0.f, 1.f, RelMul);
				Nt.Ph[2] = Wrap01(Nt.Ph[2] + 5.f * InvSr);
				const float I = Inc * CentsMul(VibD * Sin01(Nt.Ph[2]));
				const float I2 = I * CentsMul(4.f);
				Nt.Ph[0] = Wrap01(Nt.Ph[0] + I); Nt.Ph[1] = Wrap01(Nt.Ph[1] + I2);
				const float Src = BlepSaw(Nt.Ph[0], I) + BlepSaw(Nt.Ph[1], I2);
				const float Y = Nt.F[0].Low(Src) * 0.5f + Nt.F[1].Band(Src) * 0.25f;
				Out(K, Y * E * Lvl);
				Peak = FMath::Max(Peak, E);
				Nt.T += InvSr;
			}
		}
		else
		{
			// string pad: four sawtooths +/- 6 cents through 1.2 kHz (dark 900 Hz), attack 3 s; synth string:
			// two sawtooths +/- 5 cents through a two-pole 700 Hz, attack 2.5 s
			const bool bSynth = Nt.Var == 3;
			const float Att = bSynth ? 2.5f : 3.f, RelMul = DecayMul(bSynth ? 2.5f : 3.f, Sr);
			static const float DetPad[4] = { -6.f, -2.f, 2.f, 6.f };
			static const float DetSyn[2] = { -5.f, 5.f };
			const int32 NV = bSynth ? 2 : 4;
			const float* Det = bSynth ? DetSyn : DetPad;
			float Incs[4];
			for (int32 V = 0; V < NV; ++V) Incs[V] = Inc * CentsMul(Det[V]);
			for (int32 K = 0; K < N; ++K)
			{
				const float E = Adsr(Nt.Env, Nt.bReleased, Nt.T, Nt.Gate, Att, 0.f, 1.f, RelMul);
				float Sl = 0.f, Srr = 0.f;
				for (int32 V = 0; V < NV; ++V)
				{
					Nt.Ph[V] = Wrap01(Nt.Ph[V] + Incs[V]);
					const float W = BlepSaw(Nt.Ph[V], Incs[V]);
					if (V & 1) Srr += W; else Sl += W;
				}
				const float FL = Nt.F[0].Low(Sl), FR = Nt.F[1].Low(Srr);
				const float G = E * Lvl;
				const float SL = FL * G * (0.5f + 0.5f * Gl), SR = FR * G * (0.5f + 0.5f * Gr);
				L[K] += SL; R[K] += SR; HallL[K] += SL * Hs; HallR[K] += SR * Hs; SkyL[K] += SL * Ks; SkyR[K] += SR * Ks;
				Peak = FMath::Max(Peak, E);
				Nt.T += InvSr;
			}
		}
		break;
	}
	case EDmInst::Cello:
	{
		// bowed drone: two sawtooths +/- 3 cents through 400 Hz; the war pedal: +/- 4 cents through 500 Hz, slow
		const float Att = Nt.Var ? 4.f : 2.f, RelMul = DecayMul(Nt.Var ? 3.f : 2.5f, Sr);
		const float C = Nt.Var ? 4.f : 3.f;
		const float I0 = Inc * CentsMul(-C), I1 = Inc * CentsMul(C);
		for (int32 K = 0; K < N; ++K)
		{
			const float E = Adsr(Nt.Env, Nt.bReleased, Nt.T, Nt.Gate, Att, 0.f, 1.f, RelMul);
			Nt.Ph[0] = Wrap01(Nt.Ph[0] + I0); Nt.Ph[1] = Wrap01(Nt.Ph[1] + I1);
			const float Y = Nt.F[0].Low(BlepSaw(Nt.Ph[0], I0) + BlepSaw(Nt.Ph[1], I1));
			Out(K, Y * E * Lvl);
			Peak = FMath::Max(Peak, E);
			Nt.T += InvSr;
		}
		break;
	}
	case EDmInst::Brass:
	{
		// sawtooths through a low-pass swelling 400 Hz -> 1.8 kHz over 300 ms (Q 0.9); 120 ms / 0 / 1 / 500 ms
		const float RelMul = DecayMul(0.5f, Sr);
		Nt.F[0].Set(400.f + 1400.f * SStep(0.f, 0.3f, Nt.T), 0.9f, Sr);
		const float VibD = 6.f * SStep(0.3f, 0.7f, Nt.T);
		for (int32 K = 0; K < N; ++K)
		{
			const float E = Adsr(Nt.Env, Nt.bReleased, Nt.T, Nt.Gate, 0.12f, 0.f, 1.f, RelMul);
			Nt.Ph[2] = Wrap01(Nt.Ph[2] + 4.9f * InvSr);
			const float V = CentsMul(VibD * Sin01(Nt.Ph[2]));
			const float I0 = Inc * V * CentsMul(-3.f), I1 = Inc * V * CentsMul(3.f);
			Nt.Ph[0] = Wrap01(Nt.Ph[0] + I0); Nt.Ph[1] = Wrap01(Nt.Ph[1] + I1);
			const float Y = Nt.F[0].Low(BlepSaw(Nt.Ph[0], I0) + BlepSaw(Nt.Ph[1], I1)) * 0.5f;
			Out(K, Y * E * Lvl);
			Peak = FMath::Max(Peak, E);
			Nt.T += InvSr;
		}
		break;
	}
	case EDmInst::Pad:
	{
		// six sawtooths spread +/- 15 cents through 1.5 kHz, the filter swept +/- 400 Hz at 0.05 Hz; 4 s / 6 s
		const float RelMul = DecayMul(6.f, Sr);
		Nt.Ph[7] = Wrap01(Nt.Ph[7] + 0.05f * BlockSec);
		const float Cut = 1500.f + 400.f * Sin01(Nt.Ph[7]);
		Nt.F[0].Set(Cut, 0.7f, Sr); Nt.F[1].Set(Cut * 1.05f, 0.7f, Sr);
		static const float Det[6] = { -15.f, -9.f, -3.f, 3.f, 9.f, 15.f };
		float Incs[6];
		for (int32 V = 0; V < 6; ++V) Incs[V] = Inc * CentsMul(Det[V]);
		for (int32 K = 0; K < N; ++K)
		{
			const float E = Adsr(Nt.Env, Nt.bReleased, Nt.T, Nt.Gate, 4.f, 0.f, 1.f, RelMul);
			float Sl = 0.f, Srr = 0.f;
			for (int32 V = 0; V < 6; ++V)
			{
				Nt.Ph[V] = Wrap01(Nt.Ph[V] + Incs[V]);
				const float W = BlepSaw(Nt.Ph[V], Incs[V]);
				if (V & 1) Srr += W; else Sl += W;
			}
			const float G = E * Lvl;
			const float SL = Nt.F[0].Low(Sl) * G * (0.5f + 0.5f * Gl), SR = Nt.F[1].Low(Srr) * G * (0.5f + 0.5f * Gr);
			L[K] += SL; R[K] += SR; HallL[K] += SL * Hs; HallR[K] += SR * Hs; SkyL[K] += SL * Ks; SkyR[K] += SR * Ks;
			Peak = FMath::Max(Peak, E);
			Nt.T += InvSr;
		}
		break;
	}
	case EDmInst::Bell:
	{
		bPerc = true;
		if (Nt.Var == 1)
		{
			// the toll: FM 1 : 2.76, index 3 -> 0.5, 3 ms attack, a hum partial at 0.5x and a tierce at 1.2x, 6 s
			const float Mul = DecayMul(6.f, Sr), IdxMul = std::exp(-InvSr / 0.8f), HumMul = DecayMul(6.5f, Sr), TierMul = DecayMul(4.5f, Sr);
			for (int32 K = 0; K < N; ++K)
			{
				Nt.Ph[0] = Wrap01(Nt.Ph[0] + Inc); Nt.Ph[1] = Wrap01(Nt.Ph[1] + Inc * 2.76f);
				Nt.Ph[2] = Wrap01(Nt.Ph[2] + Inc * 0.5f); Nt.Ph[3] = Wrap01(Nt.Ph[3] + Inc * 1.2f);
				const float Idx = 0.5f + 2.5f * Nt.Aux[1];
				Nt.Aux[1] *= IdxMul;
				const float V = Sin01(Nt.Ph[0] + Idx * 0.159155f * Sin01(Nt.Ph[1])) * Nt.Env + 0.5f * Sin01(Nt.Ph[2]) * Nt.Aux[3] + 0.35f * Sin01(Nt.Ph[3]) * Nt.Aux[4];
				Nt.Env *= Mul; Nt.Aux[3] *= HumMul; Nt.Aux[4] *= TierMul;
				Out(K, V * Sat01(Nt.T / 0.003f) * Lvl * 0.6f);
				Nt.T += InvSr;
			}
			Peak = FMath::Max(Nt.Env, Nt.Aux[3]);
		}
		else if (Nt.Var == 2)
		{
			// golden-age sparkle: a bell partial pair at 2-4 kHz
			const float Mul = DecayMul(1.6f, Sr), Mul2 = DecayMul(0.9f, Sr);
			for (int32 K = 0; K < N; ++K)
			{
				Nt.Ph[0] = Wrap01(Nt.Ph[0] + Inc); Nt.Ph[1] = Wrap01(Nt.Ph[1] + Inc * 1.51f);
				const float V = Sin01(Nt.Ph[0]) * Nt.Env + 0.4f * Sin01(Nt.Ph[1]) * Nt.Aux[2];
				Nt.Env *= Mul; Nt.Aux[2] *= Mul2;
				Out(K, V * Sat01(Nt.T / 0.002f) * Lvl);
				Nt.T += InvSr;
			}
			Peak = Nt.Env;
		}
		else
		{
			// the sail bell: FM 1 : 3.5, index 2.5 -> 0.3 over 2 s, 1 ms / 3 s, a shimmer partial at 2.76x (-12 dB)
			const float Mul = DecayMul(3.f, Sr), IdxMul = std::exp(-InvSr / 0.6f), ShMul = DecayMul(4.f, Sr);
			for (int32 K = 0; K < N; ++K)
			{
				Nt.Ph[0] = Wrap01(Nt.Ph[0] + Inc); Nt.Ph[1] = Wrap01(Nt.Ph[1] + Inc * 3.5f); Nt.Ph[2] = Wrap01(Nt.Ph[2] + Inc * 2.76f);
				const float Idx = 0.3f + 2.2f * Nt.Aux[1];
				Nt.Aux[1] *= IdxMul;
				const float V = Sin01(Nt.Ph[0] + Idx * 0.159155f * Sin01(Nt.Ph[1])) * Nt.Env + 0.25f * Sin01(Nt.Ph[2]) * Nt.Aux[2];
				Nt.Env *= Mul; Nt.Aux[2] *= ShMul;
				Out(K, V * Sat01(Nt.T / 0.001f) * Lvl);
				Nt.T += InvSr;
			}
			Peak = Nt.Env;
		}
		break;
	}
	case EDmInst::Piano:
	{
		// eight stretched partials (n x (1 + 0.0004 n^2) x f), each decaying over 4 / n s, a 4 ms hammer at 3 kHz,
		// velocity opening a gentle low-pass from 2 to 6 kHz; the damper falls when the key is released
		bPerc = true;
		const bool bDamp = Nt.T > Nt.Gate;
		static const float Amp[8] = { 1.f, 0.5f, 0.33f, 0.24f, 0.18f, 0.13f, 0.1f, 0.08f };
		float Muls[8], Incs[8];
		for (int32 P = 0; P < 8; ++P)
		{
			const float Np = float(P + 1);
			Muls[P] = DecayMul(bDamp ? 0.35f : 4.f / Np, Sr);
			Incs[P] = Inc * Np * (1.f + 0.0004f * Np * Np);
		}
		// partial envelopes: Aux[0..5] and Env (6), Ph-free slots reuse Aux[5] for the 7th and 8th
		for (int32 K = 0; K < N; ++K)
		{
			float V = 0.f;
			for (int32 P = 0; P < 8; ++P)
			{
				Nt.Ph[P] = Wrap01(Nt.Ph[P] + Incs[P]);
				const float Ev = P < 6 ? Nt.Aux[P] : (P == 6 ? Nt.Env : Nt.Env * Nt.Env);
				V += Sin01(Nt.Ph[P]) * Amp[P] * Ev;
			}
			for (int32 P = 0; P < 6; ++P) Nt.Aux[P] *= Muls[P];
			Nt.Env *= Muls[6];
			const float Hammer = Nt.F[1].Band(Nt.Rng.Bi()) * 0.4f * std::exp(-Nt.T * 600.f);
			Out(K, Nt.F[0].Low(V * Sat01(Nt.T / 0.002f) * 0.55f + Hammer) * Lvl);
			Nt.T += InvSr;
		}
		Peak = Nt.Aux[0];
		if (Nt.T > Nt.Gate) Nt.bReleased = true;
		break;
	}
	case EDmInst::Organ:
	{
		// five sines at 1, 2, 3, 4 and 8 x f (1, .8, .5, .3, .2), a rotary wobble of 15 % at 6 Hz
		const float RelMul = DecayMul(1.f, Sr);
		static const float Rat[5] = { 1.f, 2.f, 3.f, 4.f, 8.f };
		static const float Amp[5] = { 1.f, 0.8f, 0.5f, 0.3f, 0.2f };
		for (int32 K = 0; K < N; ++K)
		{
			const float E = Adsr(Nt.Env, Nt.bReleased, Nt.T, Nt.Gate, 0.5f, 0.f, 1.f, RelMul);
			float V = 0.f;
			for (int32 P = 0; P < 5; ++P) { Nt.Ph[P] = Wrap01(Nt.Ph[P] + Inc * Rat[P]); V += Sin01(Nt.Ph[P]) * Amp[P]; }
			Nt.Ph[5] = Wrap01(Nt.Ph[5] + 6.f * InvSr);
			const float Rot = 1.f - 0.15f * (0.5f + 0.5f * Sin01(Nt.Ph[5]));
			Out(K, V * (1.f / 2.8f) * Rot * E * Lvl);
			Peak = FMath::Max(Peak, E);
			Nt.T += InvSr;
		}
		break;
	}
	case EDmInst::Glass:
	{
		// a sine plus a sine at 2.01 x f (-8 dB) beating slowly; 400 ms / 0 / 1 / 2 s
		const float RelMul = DecayMul(2.f, Sr);
		for (int32 K = 0; K < N; ++K)
		{
			const float E = Adsr(Nt.Env, Nt.bReleased, Nt.T, Nt.Gate, 0.4f, 0.f, 1.f, RelMul);
			Nt.Ph[0] = Wrap01(Nt.Ph[0] + Inc); Nt.Ph[1] = Wrap01(Nt.Ph[1] + Inc * 2.01f);
			Out(K, (Sin01(Nt.Ph[0]) + 0.4f * Sin01(Nt.Ph[1])) * E * Lvl);
			Peak = FMath::Max(Peak, E);
			Nt.T += InvSr;
		}
		break;
	}
	case EDmInst::Breath:
	{
		// hummed drone: two sines +/- 3 cents (and a soft 2nd harmonic) through 600 Hz, swelling +/- 2 dB at 0.07 Hz;
		// space drone (an octave up): 900 Hz, +/- 2.5 dB at 0.03 Hz; genesis: +/- 3 dB at 0.03 Hz, a 4 s breath in
		const float SwHz = Nt.Var == 0 ? 0.07f : 0.03f, SwD = Nt.Var == 0 ? 0.26f : (Nt.Var == 1 ? 0.33f : 0.41f);
		const float Att = Nt.Var == 0 ? 1.5f : (Nt.Var == 1 ? 3.f : 4.f), RelMul = DecayMul(Nt.Var == 2 ? 4.f : (Nt.Var == 1 ? 3.f : 2.5f), Sr);
		const float I0 = Inc * CentsMul(-3.f), I1 = Inc * CentsMul(3.f);
		const float H2 = Nt.Var == 1 ? 0.25f : 0.15f;
		Nt.Ph[3] = Wrap01(Nt.Ph[3] + SwHz * BlockSec);
		const float Swell = 1.f + SwD * Sin01(Nt.Ph[3]);
		for (int32 K = 0; K < N; ++K)
		{
			const float E = Adsr(Nt.Env, Nt.bReleased, Nt.T, Nt.Gate, Att, 0.f, 1.f, RelMul);
			Nt.Ph[0] = Wrap01(Nt.Ph[0] + I0); Nt.Ph[1] = Wrap01(Nt.Ph[1] + I1); Nt.Ph[2] = Wrap01(Nt.Ph[2] + Inc * 2.f);
			const float V = 0.5f * (Sin01(Nt.Ph[0]) + Sin01(Nt.Ph[1])) + H2 * Sin01(Nt.Ph[2]);
			Out(K, Nt.Lp.Tick(V) * Swell * E * Lvl);
			Peak = FMath::Max(Peak, E);
			Nt.T += InvSr;
		}
		break;
	}
	case EDmInst::Choir:
	{
		// sawtooths with detune and vibrato through the "ah" formants (700, 1220, 2600 Hz)
		const float RelMul = DecayMul(1.5f, Sr);
		for (int32 K = 0; K < N; ++K)
		{
			const float E = Adsr(Nt.Env, Nt.bReleased, Nt.T, Nt.Gate, 0.8f, 0.f, 1.f, RelMul);
			Nt.Ph[3] = Wrap01(Nt.Ph[3] + 5.f * InvSr);
			const float Vib = CentsMul(10.f * Sin01(Nt.Ph[3]));
			const float I0 = Inc * Vib * CentsMul(-8.f), I1 = Inc * Vib * CentsMul(8.f), I2 = Inc * Vib;
			Nt.Ph[0] = Wrap01(Nt.Ph[0] + I0); Nt.Ph[1] = Wrap01(Nt.Ph[1] + I1); Nt.Ph[2] = Wrap01(Nt.Ph[2] + I2);
			const float Src = BlepSaw(Nt.Ph[0], I0) + BlepSaw(Nt.Ph[1], I1) + 0.6f * BlepSaw(Nt.Ph[2], I2);
			const float V = Nt.F[0].Band(Src) + 0.5f * Nt.F[1].Band(Src) + 0.2f * Nt.F[2].Band(Src);
			Out(K, V * E * Lvl);
			Peak = FMath::Max(Peak, E);
			Nt.T += InvSr;
		}
		break;
	}
	case EDmInst::Perc:
	{
		// shaker (noise at 6 kHz, 1 / 30 ms), clock tick (2 kHz, 2 / 15 ms), heartbeat thump (55 Hz, high-passed
		// at 40 Hz), arpeggio blip (5 ms / 300 ms)
		bPerc = true;
		static const float Dec[4] = { 0.03f, 0.015f, 0.25f, 0.3f };
		static const float Att[4] = { 0.001f, 0.002f, 0.005f, 0.005f };
		const int32 Vr = FMath::Min<int32>(Nt.Var, 3);
		const float Mul = DecayMul(Dec[Vr], Sr);
		const float PInc = Vr == 1 ? 2000.f * InvSr : (Vr == 2 ? 55.f * InvSr : Inc);
		for (int32 K = 0; K < N; ++K)
		{
			float V;
			if (Vr == 0) V = Nt.F[0].Band(Nt.Rng.Bi());
			else { Nt.Ph[0] = Wrap01(Nt.Ph[0] + PInc); V = Sin01(Nt.Ph[0]); }
			V *= Nt.Env * Sat01(Nt.T / Att[Vr]);
			if (Vr == 2) V = Nt.Hp.Tick(V);
			Nt.Env *= Mul;
			Out(K, V * Lvl);
			Nt.T += InvSr;
		}
		Peak = Nt.Env;
		break;
	}
	case EDmInst::Cluster:
	{
		// doom: tonic plus a minor 2nd, very soft, with a copy of the tonic 3 Hz sharp so it beats at about 3 Hz
		const float RelMul = DecayMul(3.f, Sr);
		const float I1 = Inc * 1.05946f, I2 = (Hz + 3.f) * InvSr;
		for (int32 K = 0; K < N; ++K)
		{
			const float E = Adsr(Nt.Env, Nt.bReleased, Nt.T, Nt.Gate, 4.f, 0.f, 1.f, RelMul);
			Nt.Ph[0] = Wrap01(Nt.Ph[0] + Inc); Nt.Ph[1] = Wrap01(Nt.Ph[1] + I1); Nt.Ph[2] = Wrap01(Nt.Ph[2] + I2);
			Nt.Ph[3] = Wrap01(Nt.Ph[3] + Inc * 2.f); Nt.Ph[4] = Wrap01(Nt.Ph[4] + I2 * 2.f);
			const float V = Sin01(Nt.Ph[0]) + 0.7f * Sin01(Nt.Ph[1]) + 0.8f * Sin01(Nt.Ph[2]) + 0.2f * Sin01(Nt.Ph[3]) + 0.12f * Sin01(Nt.Ph[4]);
			Out(K, Nt.Lp.Tick(V) * (1.f / 2.8f) * E * Lvl);
			Peak = FMath::Max(Peak, E);
			Nt.T += InvSr;
		}
		break;
	}
	case EDmInst::Rafters:
	{
		// doom: wind through the rafters, brown noise through a band at 180 Hz (Q 4) swept +/- 40 Hz at 0.03 Hz
		const float RelMul = DecayMul(3.f, Sr);
		Nt.Ph[0] = Wrap01(Nt.Ph[0] + 0.03f * BlockSec);
		Nt.Ph[1] = Wrap01(Nt.Ph[1] + 0.2f * BlockSec);
		Nt.F[0].Set(180.f + 40.f * Sin01(Nt.Ph[0]), 4.f, Sr);
		const float Amp = 0.8f + 0.2f * Sin01(Nt.Ph[1]);
		for (int32 K = 0; K < N; ++K)
		{
			const float E = Adsr(Nt.Env, Nt.bReleased, Nt.T, Nt.Gate, 5.f, 0.f, 1.f, RelMul);
			Out(K, Nt.F[0].Band(Nt.Brown.Tick(Nt.Rng.Bi())) * Amp * E * Lvl);
			Peak = FMath::Max(Peak, E);
			Nt.T += InvSr;
		}
		break;
	}
	default:
		Peak = 0.f;
		Nt.T += BlockSec;
		break;
	}
	if (bPerc ? (Peak < 2e-4f && Nt.T > 0.01f) : (Nt.T > Nt.Gate && Peak < 2e-4f)) Nt.bOn = false;
	if (Nt.T > Nt.Gate + 30.f) Nt.bOn = false;
}
