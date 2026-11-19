// DEMIURGE — Game/Audio/DmAudioMusic.cpp : the generative score. Owner: C9 audio.
#include "Game/Audio/DmAudioMusic.h"

using namespace DmDsp;

namespace
{
	// Weight of each instrument per age (Stone .. Space). Columns follow EDmInst up to Breath.
	//                                 Flute Drum  Lyre  Reed  Str   Bell  Brass Piano Pad   Glass Cello Taiko Breath
	const float GAgeW[7][13] = {
		/* Stone    */ { 1.00f, 0.80f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.0f, 0.0f, 1.00f },
		/* Bronze   */ { 0.45f, 0.70f, 1.00f, 0.80f, 0.00f, 0.15f, 0.00f, 0.00f, 0.00f, 0.00f, 0.0f, 0.0f, 0.55f },
		/* Iron     */ { 0.20f, 0.50f, 0.70f, 0.45f, 0.90f, 0.60f, 0.00f, 0.00f, 0.00f, 0.00f, 0.0f, 0.0f, 0.00f },
		/* Sail     */ { 0.30f, 0.40f, 0.55f, 0.20f, 1.00f, 0.80f, 0.25f, 0.00f, 0.00f, 0.00f, 0.0f, 0.0f, 0.00f },
		/* Industry */ { 0.00f, 0.30f, 0.00f, 0.00f, 0.70f, 0.45f, 0.90f, 1.00f, 0.00f, 0.00f, 0.0f, 0.0f, 0.00f },
		/* Atomic   */ { 0.00f, 0.15f, 0.00f, 0.00f, 0.45f, 0.40f, 0.35f, 0.90f, 0.70f, 0.30f, 0.0f, 0.0f, 0.00f },
		/* Space    */ { 0.00f, 0.00f, 0.00f, 0.00f, 0.30f, 0.50f, 0.00f, 0.30f, 1.00f, 1.00f, 0.0f, 0.0f, 0.00f },
	};
	const float GTempo[7] = { 56.f, 58.f, 60.f, 62.f, 64.f, 66.f, 68.f };

	// Output level of each instrument (balances their very different raw loudness).
	const float GLevel[int32(EDmInst::Count)] = {
		/* Flute */ 0.17f, /* HandDrum */ 0.30f, /* Lyre */ 0.30f, /* Reed */ 0.085f, /* Strings */ 0.060f, /* Bell */ 0.13f,
		/* Brass */ 0.075f, /* Piano */ 0.15f, /* Pad */ 0.040f, /* Glass */ 0.10f, /* Cello */ 0.075f, /* Taiko */ 0.30f,
		/* Breath */ 0.075f, /* Choir */ 0.10f,
	};
	const float GSend[int32(EDmInst::Count)] = {
		0.55f, 0.25f, 0.45f, 0.45f, 0.55f, 0.65f, 0.45f, 0.45f, 0.6f, 0.7f, 0.4f, 0.35f, 0.6f, 0.65f,
	};
}

const TCHAR* FDmMusic::InstName(EDmInst I)
{
	static const TCHAR* Names[int32(EDmInst::Count)] = { TEXT("flute"), TEXT("hand-drum"), TEXT("lyre"), TEXT("reed"), TEXT("strings"),
		TEXT("bell"), TEXT("brass"), TEXT("piano"), TEXT("pad"), TEXT("glass"), TEXT("cello"), TEXT("taiko"), TEXT("breath"), TEXT("choir") };
	const int32 K = int32(I);
	return (K >= 0 && K < int32(EDmInst::Count)) ? Names[K] : TEXT("?");
}

void FDmMusic::Init(float InSr, uint32 Seed)
{
	Sr = InSr;
	InvSr = 1.f / Sr;
	Rng = FRng(Seed ^ 0xA5A5F00Du);
	Rng.Next();
	SeedNow = Seed;
	BaseScale = FDmScale::FromSeed(Seed);
	Scale = BaseScale;
	Notes.SetNum(MaxNotes);
	KsPool.SetNumZeroed(MaxNotes * KsLen);
	for (int32 I = 0; I < MaxNotes; ++I) { Notes[I] = FNote(); Notes[I].Ks = KsPool.GetData() + I * KsLen; }
	for (FSched& S : Sched) S.bOn = false;
	Clock = 0; NextEighth = 0.5; Eighth = 0;
	ChordDeg = 0; MelDeg = 4; PhraseLeft = 0; ArpIdx = 0;
	CurAge = PrevAge = 0; AgeBlend = 1.f;
	Density = 0.5f; TensionS = 0.f; FarS = 0.f; OutGain = 1.f; DuckS = 1.f;
	StingerUntil = -1;
	bFirstUpdate = true;
	for (int32& C : NotesPlayed) C = 0;
}

float FDmMusic::Weight(EDmInst I) const
{
	const int32 K = int32(I);
	if (K >= 13) return 0.f;
	const float A = GAgeW[FMath::Clamp(PrevAge, 0, 6)][K], B = GAgeW[FMath::Clamp(CurAge, 0, 6)][K];
	return Lerp(A, B, Sat01(AgeBlend));
}

int32 FDmMusic::ActiveNotes() const
{
	int32 C = 0;
	for (const FNote& N : Notes) C += N.bOn ? 1 : 0;
	return C;
}

void FDmMusic::Update(const FDmMusicParams& P, float BlockSeconds)
{
	Prm = P;
	if (P.Seed != SeedNow)
	{
		SeedNow = P.Seed;
		BaseScale = FDmScale::FromSeed(P.Seed);
		Scale = BaseScale;
	}
	const int32 Age = FMath::Clamp(P.Age, 0, 6);
	if (bFirstUpdate) { bFirstUpdate = false; CurAge = PrevAge = Age; AgeBlend = 1.f; TensionS = Sat01(P.Tension); }
	if (Age != CurAge)
	{
		PrevAge = AgeBlend >= 0.5f ? CurAge : PrevAge;
		CurAge = Age;
		AgeBlend = 0.f;
	}
	AgeBlend = FMath::Min(1.f, AgeBlend + BlockSeconds / 10.f);
	bGenesis = P.Phase < 4;
	TensionS += SmoothCoef(4.f, 1.f / BlockSeconds) * (Sat01(P.Tension) - TensionS);
	const float Far = SStep(0.55f, 0.95f, P.Altitude01);
	FarS += SmoothCoef(2.f, 1.f / BlockSeconds) * (Far - FarS);
	Density = (bGenesis ? 0.3f : 0.62f) * (1.f - 0.7f * FarS) * (P.bPaused ? 0.5f : 1.f);
	const float GainT = (1.f - 0.35f * FarS) * (P.bPaused ? 0.7f : 1.f);
	OutGain += SmoothCoef(1.5f, 1.f / BlockSeconds) * (GainT - OutGain);
	const float DuckT = Clock < StingerUntil ? 0.35f : 1.f;
	DuckS += SmoothCoef(0.4f, 1.f / BlockSeconds) * (DuckT - DuckS);
	Tempo = GTempo[CurAge];
}

void FDmMusic::PlayNote(EDmInst I, float Midi, float Vel, float GateSec, float Pan, bool bStinger)
{
	// a free slot, else steal the quietest non-stinger note
	int32 Best = -1;
	float BestScore = 1e9f;
	for (int32 K = 0; K < MaxNotes; ++K)
	{
		const FNote& N = Notes[K];
		if (!N.bOn) { Best = K; break; }
		const float Score = (N.bStinger ? 10.f : 0.f) + N.Env * N.Vel + (N.bReleased ? 0.f : 0.5f);
		if (Score < BestScore) { BestScore = Score; Best = K; }
	}
	if (Best < 0) return;
	FNote& N = Notes[Best];
	float* KsBuf = N.Ks;
	N = FNote();
	N.Ks = KsBuf;
	N.bOn = true;
	N.bStinger = bStinger;
	N.Inst = I;
	N.Hz = MidiToHz(Midi);
	N.Vel = Clamp(Vel, 0.f, 1.f);
	N.Gate = FMath::Max(0.02f, GateSec);
	N.Pan = Clamp(Pan, -1.f, 1.f);
	N.Rng = FRng(Rng.Next() | 1u);
	N.Order = ++NoteOrder;
	++NotesPlayed[int32(I)];
	for (float& Ph : N.Ph) Ph = N.Rng.Uni();
	N.Env = 0.f;
	switch (I)
	{
	case EDmInst::Flute:
		N.F[0].Set(N.Hz * 2.2f, 1.6f, Sr);
		break;
	case EDmInst::HandDrum:
		N.F[0].Set(850.f, 1.0f, Sr);
		N.Env = 1.f;
		break;
	case EDmInst::Taiko:
		N.F[0].Set(420.f, 0.7f, Sr);
		N.Env = 1.f;
		break;
	case EDmInst::Lyre:
	{
		N.KsDelay = FMath::Clamp(Sr / N.Hz - 0.5f, 4.f, float(KsLen - 4));
		const float T60 = 3.2f * std::sqrt(Clamp(220.f / N.Hz, 0.45f, 1.6f));
		N.KsRho = std::pow(10.f, -3.f / (T60 * N.Hz));
		FLp1 Ex; Ex.Set(2600.f, Sr);
		const int32 Len = int32(N.KsDelay) + 2;
		for (int32 K = 0; K < KsLen; ++K) N.Ks[K] = K < Len ? Ex.Tick(N.Rng.Bi()) * 1.6f : 0.f;
		N.KsPos = Len;
		N.Env = 1.f;
		N.Lp.Set(3500.f, Sr);
		break;
	}
	case EDmInst::Reed:
		N.F[0].Set(FMath::Min(N.Hz * 4.5f, 2000.f), 0.8f, Sr);
		N.F[1].Set(1150.f, 3.f, Sr);
		break;
	case EDmInst::Strings:
		N.F[0].Set(900.f, 0.6f, Sr);
		break;
	case EDmInst::Cello:
		N.F[0].Set(520.f, 0.7f, Sr);
		break;
	case EDmInst::Brass:
		N.F[0].Set(350.f, 0.9f, Sr);
		break;
	case EDmInst::Pad:
		N.F[0].Set(1100.f, 0.7f, Sr);
		N.F[1].Set(1100.f, 0.7f, Sr);
		break;
	case EDmInst::Bell:
		N.Env = 1.f;
		N.Aux[0] = 2.2f;   // FM index
		break;
	case EDmInst::Piano:
		N.Env = 1.f;
		N.Lp.Set(2200.f, Sr);
		break;
	case EDmInst::Glass:
		N.Env = 1.f;
		N.Lp.Set(3200.f, Sr);
		break;
	case EDmInst::Breath:
		N.F[0].Set(N.Hz * 3.f, 4.f, Sr);
		break;
	case EDmInst::Choir:
		N.F[0].Set(730.f, 5.f, Sr);
		N.F[1].Set(1090.f, 7.f, Sr);
		N.F[2].Set(2440.f, 9.f, Sr);
		break;
	default: break;
	}
}

void FDmMusic::Schedule(float At, EDmInst I, float Midi, float Vel, float Dur, float Pan)
{
	for (FSched& S : Sched)
	{
		if (S.bOn) continue;
		S.bOn = true; S.At = float(Clock) + At; S.Inst = I; S.Midi = Midi; S.Vel = Vel; S.Dur = Dur; S.Pan = Pan;
		return;
	}
}

EDmInst FDmMusic::MelodicFor(float Pick) const
{
	static const EDmInst Mel[7] = { EDmInst::Flute, EDmInst::Reed, EDmInst::Lyre, EDmInst::Brass, EDmInst::Piano, EDmInst::Glass, EDmInst::Bell };
	static const float Mul[7] = { 1.f, 1.f, 0.5f, 0.6f, 1.f, 0.6f, 0.25f };
	float Tot = 0.f;
	for (int32 I = 0; I < 7; ++I) Tot += Weight(Mel[I]) * Mul[I];
	if (Tot <= 0.f) return EDmInst::Flute;
	float U = Pick * Tot;
	for (int32 I = 0; I < 7; ++I)
	{
		U -= Weight(Mel[I]) * Mul[I];
		if (U <= 0.f) return Mel[I];
	}
	return Mel[6];
}

void FDmMusic::NewChord()
{
	Scale = TensionS > 0.5f ? BaseScale.Darkened() : BaseScale;
	const int32 Cnt = Scale.Count;
	if (Cnt == 7)
	{
		static const float Tr[7][7] = {
			{ 0, 1, 0, 3, 2, 3, 1 },
			{ 1, 0, 0, 0, 3, 0, 1 },
			{ 1, 0, 0, 2, 0, 2, 0 },
			{ 3, 1, 0, 0, 3, 0, 1 },
			{ 4, 0, 0, 1, 0, 2, 0 },
			{ 1, 1, 0, 3, 2, 0, 0 },
			{ 3, 0, 0, 1, 1, 0, 0 },
		};
		const int32 From = ((ChordDeg % 7) + 7) % 7;
		float Tot = 0.f;
		for (int32 K = 0; K < 7; ++K) Tot += Tr[From][K];
		float U = Rng.Uni() * Tot;
		int32 Next = 0;
		for (int32 K = 0; K < 7; ++K) { U -= Tr[From][K]; if (U <= 0.f) { Next = K; break; } }
		ChordDeg = Next;
		Chord[0] = ChordDeg; Chord[1] = ChordDeg + 2; Chord[2] = ChordDeg + 4; Chord[3] = ChordDeg + 7;
	}
	else
	{
		int32 Next = Rng.Int(Cnt + 2);
		if (Next >= Cnt) Next = 0;
		if (Next == ChordDeg) Next = (Next + 2) % Cnt;
		ChordDeg = Next;
		Chord[0] = ChordDeg; Chord[1] = ChordDeg + 2; Chord[2] = ChordDeg + 3; Chord[3] = ChordDeg + Cnt;
	}

	const float BarSec = 8.f * 30.f / Tempo;
	const float Gate = 2.f * BarSec + 0.4f;
	const float WStr = Weight(EDmInst::Strings), WPad = Weight(EDmInst::Pad), WBrass = Weight(EDmInst::Brass);
	const float WBell = Weight(EDmInst::Bell), WBreath = Weight(EDmInst::Breath), WPiano = Weight(EDmInst::Piano);
	const float Thin = 1.f - 0.5f * FarS;
	if (WBreath > 0.05f && Rng.Chance(0.9f))
	{
		// the stone-age drone: tonic and fifth of the world's scale, breathing
		PlayNote(EDmInst::Breath, Scale.Midi(0, 3), 0.55f * WBreath, Gate, -0.2f);
		if (Rng.Chance(0.7f * Thin)) PlayNote(EDmInst::Breath, Scale.Midi(Cnt == 7 ? 4 : 3, 3), 0.4f * WBreath, Gate, 0.25f);
	}
	if (WStr > 0.05f && Rng.Chance(FMath::Min(1.f, WStr * 1.1f)))
	{
		const float V = 0.55f * WStr;
		PlayNote(EDmInst::Strings, Scale.Midi(Chord[0], 3), V, Gate, -0.35f);
		PlayNote(EDmInst::Strings, Scale.Midi(Chord[1], 3), V * 0.9f, Gate, 0.1f);
		if (Rng.Chance(Thin)) PlayNote(EDmInst::Strings, Scale.Midi(Chord[2], 3), V * 0.85f, Gate, 0.4f);
		if (CurAge >= 2 && Rng.Chance(0.8f)) PlayNote(EDmInst::Strings, Scale.Midi(Chord[0], 2), V * 0.8f, Gate, 0.f);
	}
	if (WPad > 0.05f && Rng.Chance(FMath::Min(1.f, WPad * 1.1f)))
	{
		const float V = 0.6f * WPad;
		PlayNote(EDmInst::Pad, Scale.Midi(Chord[0], 3), V, Gate + 1.f, -0.4f);
		PlayNote(EDmInst::Pad, Scale.Midi(Chord[1], 4), V * 0.85f, Gate + 1.f, 0.4f);
		if (Rng.Chance(Thin)) PlayNote(EDmInst::Pad, Scale.Midi(Chord[2], 3), V * 0.8f, Gate + 1.f, 0.f);
		if (Rng.Chance(0.7f)) PlayNote(EDmInst::Pad, Scale.Midi(Chord[0], 2), V * 0.7f, Gate + 1.f, 0.f);
	}
	if (WBrass > 0.05f && Rng.Chance(0.55f * WBrass * Thin))
	{
		const float V = 0.5f * WBrass;
		PlayNote(EDmInst::Brass, Scale.Midi(Chord[0], 3), V, BarSec * 1.2f, -0.2f);
		PlayNote(EDmInst::Brass, Scale.Midi(Chord[2], 3), V * 0.8f, BarSec * 1.2f, 0.2f);
	}
	if (WPiano > 0.05f && Rng.Chance(0.6f * WPiano)) PlayNote(EDmInst::Piano, Scale.Midi(Chord[0], 2), 0.45f * WPiano, BarSec * 2.f, 0.f);
	if (WBell > 0.05f && Rng.Chance(0.45f * WBell * Thin)) PlayNote(EDmInst::Bell, Scale.Midi(Chord[Rng.Int(3)], 5), 0.35f * WBell, 4.f, Rng.Bi() * 0.5f);
	if (TensionS > 0.15f)
	{
		// the darker layer: a low cello drone on the tonic, and the chord root
		PlayNote(EDmInst::Cello, Scale.Midi(0, 2), 0.7f * TensionS, Gate, -0.15f);
		if (Rng.Chance(TensionS)) PlayNote(EDmInst::Cello, Scale.Midi(Chord[0], 2) + 12.f, 0.45f * TensionS, Gate, 0.2f);
	}
}

void FDmMusic::Step()
{
	const int64 E = Eighth++;
	const int32 Pos = int32(E % 8);
	const int64 Bar = E / 8;
	const float EighthSec = 30.f / Tempo;
	if (!Prm.bScore) return;

	if (bGenesis)
	{
		// held breath: one breathing chord every two bars, nothing busy
		if (Pos == 0 && (Bar % 2) == 0)
		{
			Scale = BaseScale;
			const float Gate = 16.f * EighthSec + 1.f;
			const int32 Oct = Prm.Phase == 0 ? 2 : 3;
			PlayNote(EDmInst::Breath, Scale.Midi(0, Oct), 0.6f, Gate, -0.3f);
			PlayNote(EDmInst::Breath, Scale.Midi(Scale.Count == 7 ? 4 : 3, Oct), 0.45f, Gate, 0.3f);
			if (Prm.Phase >= 1) PlayNote(EDmInst::Breath, Scale.Midi(Scale.Count + 1, Oct), 0.32f, Gate, 0.f);
		}
		if (Pos == 4 && Prm.Phase >= 1 && Rng.Chance(0.16f * (1.f - 0.5f * FarS)))
			PlayNote(EDmInst::Glass, Scale.Midi(Rng.Int(Scale.Count), 5), 0.3f, 1.f, Rng.Bi() * 0.6f);
		if (Pos == 2 && Prm.Phase >= 2 && Rng.Chance(0.12f))
			PlayNote(EDmInst::Flute, Scale.Midi(Rng.Int(Scale.Count), 5), 0.32f, 3.f * EighthSec, Rng.Bi() * 0.4f);
		if (Prm.Phase == 3 && (Pos == 0 || Pos == 1) && Rng.Chance(0.5f))
			PlayNote(EDmInst::HandDrum, Scale.Midi(0, 3), Pos == 0 ? 0.28f : 0.18f, 0.2f, 0.f);
		return;
	}

	if (E % 16 == 0) NewChord();
	if (Clock < StingerUntil) return;

	// melody phrases
	if (PhraseLeft <= 0 && Pos == 0 && Rng.Chance(Density * 0.75f))
	{
		PhraseLeft = Rng.Chance(0.6f) ? 8 : 16;
		PhraseInst = MelodicFor(Rng.Uni());
	}
	if (PhraseLeft > 0)
	{
		--PhraseLeft;
		static const float PosP[8] = { 0.75f, 0.12f, 0.45f, 0.15f, 0.6f, 0.12f, 0.4f, 0.2f };
		if (Rng.Chance(PosP[Pos]))
		{
			const float U = Rng.Uni();
			int32 Move = U < 0.25f ? -1 : (U < 0.5f ? 1 : (U < 0.65f ? -2 : (U < 0.8f ? 2 : (U < 0.9f ? 0 : (Rng.Chance(0.5f) ? 4 : -3)))));
			MelDeg = FMath::Clamp(MelDeg + Move, 0, Scale.Count * 2);
			if ((Pos == 0 || Pos == 4) && Rng.Chance(0.6f))
			{
				// snap to the nearest chord tone
				int32 BestD = MelDeg, BestDist = 99;
				for (int32 K = 0; K < 3; ++K)
					for (int32 O = -1; O <= 2; ++O)
					{
						const int32 D = Chord[K] + O * Scale.Count;
						const int32 Dist = FMath::Abs(D - MelDeg);
						if (Dist < BestDist && D >= 0 && D <= Scale.Count * 2) { BestDist = Dist; BestD = D; }
					}
				MelDeg = BestD;
			}
			int32 Oct = 5;
			if (PhraseInst == EDmInst::Reed || PhraseInst == EDmInst::Lyre || PhraseInst == EDmInst::Brass) Oct = 4;
			const float Len = EighthSec * float(2 + Rng.Int(3)) * (PhraseInst == EDmInst::Flute || PhraseInst == EDmInst::Reed ? 1.3f : 1.f);
			const float Vel = (0.45f + 0.25f * Rng.Uni()) * (Pos == 0 ? 1.f : 0.85f);
			PlayNote(PhraseInst, Scale.Midi(MelDeg, Oct), Vel, Len, Rng.Range(-0.3f, 0.3f));
		}
	}

	// arpeggios
	EDmInst ArpInst = EDmInst::Lyre;
	float ArpW = Weight(EDmInst::Lyre);
	if (Weight(EDmInst::Piano) * 0.7f > ArpW) { ArpInst = EDmInst::Piano; ArpW = Weight(EDmInst::Piano) * 0.7f; }
	if (Weight(EDmInst::Glass) > ArpW) { ArpInst = EDmInst::Glass; ArpW = Weight(EDmInst::Glass); }
	if (ArpW > 0.05f && Rng.Chance(ArpW * Density * 0.5f))
	{
		++ArpIdx;
		const int32 D = Chord[ArpIdx % 3] + ((ArpIdx / 3) % 2) * Scale.Count;
		PlayNote(ArpInst, Scale.Midi(D, 4), 0.3f + 0.15f * Rng.Uni(), EighthSec * 1.5f, ((ArpIdx % 3) - 1) * 0.4f);
	}

	// hand drum
	const float WDrum = Weight(EDmInst::HandDrum) * Density * (1.f - FarS);
	static const float DrumP[8] = { 0.85f, 0.05f, 0.1f, 0.35f, 0.5f, 0.05f, 0.3f, 0.15f };
	if (WDrum > 0.01f && Rng.Chance(WDrum * DrumP[Pos] * 1.3f))
	{
		const bool bLow = Pos == 0 || Pos == 4;
		PlayNote(EDmInst::HandDrum, bLow ? 47.f : 52.f, (bLow ? 0.7f : 0.45f) * (0.8f + 0.2f * Rng.Uni()), 0.2f, bLow ? -0.1f : 0.25f);
	}

	// the darker layer: slow taiko
	if (TensionS > 0.1f && (Pos == 0 || (Pos == 4 && Rng.Chance(0.35f))) && Rng.Chance(0.85f * TensionS))
		PlayNote(EDmInst::Taiko, 41.f, (Pos == 0 ? 0.75f : 0.5f) * TensionS, 0.3f, 0.f);
}

void FDmMusic::Stinger(EDmStinger S)
{
	const bool bDark = S == EDmStinger::Wrath || S == EDmStinger::EndingFeared || S == EDmStinger::EndingAsh;
	const FDmScale Sc = bDark ? BaseScale.Darkened() : BaseScale;
	const int32 C = Sc.Count;
	const int32 Fifth = C == 7 ? 4 : 3;
	auto M = [&Sc](int32 Deg, int32 Oct) { return Sc.Midi(Deg, Oct); };
	float Len = 4.f;
	PhraseLeft = 0;
	switch (S)
	{
	case EDmStinger::Age:
	{
		const int32 Up[4] = { 0, 2, Fifth, C };
		for (int32 I = 0; I < 4; ++I) Schedule(0.35f * float(I), EDmInst::Bell, M(Up[I], 5), 0.55f, 3.f, -0.4f + 0.27f * float(I));
		Schedule(0.f, EDmInst::Strings, M(0, 3), 0.55f, 3.6f, -0.3f);
		Schedule(0.f, EDmInst::Strings, M(2, 3), 0.5f, 3.6f, 0.1f);
		Schedule(0.f, EDmInst::Strings, M(Fifth, 3), 0.5f, 3.6f, 0.35f);
		Schedule(0.f, EDmInst::Taiko, 41.f, 0.45f, 0.3f);
		Schedule(1.05f, EDmInst::Choir, M(0, 4), 0.5f, 2.6f, -0.2f);
		Schedule(1.05f, EDmInst::Choir, M(Fifth, 3), 0.45f, 2.6f, 0.2f);
		Len = 5.5f;
		break;
	}
	case EDmStinger::Founding:
	{
		const EDmInst I = MelodicFor(0.3f);
		Schedule(0.f, I, M(Fifth, 5), 0.5f, 0.45f, -0.2f);
		Schedule(0.26f, I, M(Fifth + 1, 5), 0.5f, 0.45f, 0.f);
		Schedule(0.52f, I, M(C, 5), 0.6f, 1.3f, 0.2f);
		Schedule(0.52f, EDmInst::Bell, M(0, 5), 0.3f, 2.f, 0.3f);
		Len = 2.6f;
		break;
	}
	case EDmStinger::Miracle:
	{
		const int32 Ch[4] = { 0, Fifth, C, C + 1 };
		for (int32 I = 0; I < 4; ++I) Schedule(0.06f * float(I), EDmInst::Bell, M(Ch[I], 5), 0.45f, 3.5f, -0.45f + 0.3f * float(I));
		Schedule(0.1f, EDmInst::Choir, M(0, 3), 0.5f, 3.f, -0.3f);
		Schedule(0.1f, EDmInst::Choir, M(2, 3), 0.45f, 3.f, 0.f);
		Schedule(0.1f, EDmInst::Choir, M(Fifth, 3), 0.45f, 3.f, 0.3f);
		for (int32 I = 0; I < 6; ++I) Schedule(0.5f + 0.11f * float(I), EDmInst::Glass, M(I * 2 % (C * 2), 6), 0.25f, 0.8f, (I % 2 ? 0.5f : -0.5f));
		Len = 4.5f;
		break;
	}
	case EDmStinger::Wrath:
	{
		Schedule(0.f, EDmInst::Taiko, 40.f, 0.95f, 0.3f);
		Schedule(0.6f, EDmInst::Taiko, 40.f, 0.6f, 0.3f);
		Schedule(0.f, EDmInst::Cello, M(0, 2), 0.8f, 3.f, -0.2f);
		Schedule(0.f, EDmInst::Cello, M(0, 2) + 1.f, 0.4f, 2.f, 0.2f);
		Schedule(0.1f, EDmInst::Brass, M(0, 3), 0.55f, 2.4f, -0.2f);
		Schedule(0.1f, EDmInst::Brass, M(Fifth, 3), 0.45f, 2.4f, 0.2f);
		Len = 4.f;
		break;
	}
	case EDmStinger::EndingBeloved:
	{
		const int32 Prog[3] = { 3, Fifth, 0 };
		const float At[3] = { 0.f, 2.6f, 5.2f };
		const float Dur[3] = { 2.8f, 2.8f, 5.5f };
		for (int32 K = 0; K < 3; ++K)
		{
			const int32 D = Prog[K];
			Schedule(At[K], EDmInst::Choir, M(D, 3), 0.5f, Dur[K], -0.3f);
			Schedule(At[K], EDmInst::Choir, M(D + 2, 3), 0.45f, Dur[K], 0.f);
			Schedule(At[K], EDmInst::Choir, M(D + 4, 3), 0.42f, Dur[K], 0.3f);
			Schedule(At[K], EDmInst::Strings, M(D, 2), 0.5f, Dur[K], 0.f);
		}
		for (int32 I = 0; I < 4; ++I) Schedule(5.2f + 0.4f * float(I), EDmInst::Bell, M(I == 3 ? C : I * 2, 5), 0.45f, 4.f, -0.4f + 0.27f * float(I));
		Len = 11.f;
		break;
	}
	case EDmStinger::EndingFeared:
	case EDmStinger::EndingAsh:
	{
		const int32 Prog[3] = { 0, C == 7 ? 5 : 3, 0 };
		for (int32 K = 0; K < 3; ++K)
		{
			const float At = 3.f * float(K);
			Schedule(At, EDmInst::Strings, M(Prog[K], 2), 0.55f, 3.2f, -0.2f);
			Schedule(At, EDmInst::Strings, M(Prog[K] + 2, 3), 0.45f, 3.2f, 0.2f);
			Schedule(At, EDmInst::Bell, M(Prog[K], 3), 0.4f, 4.f, 0.f);
		}
		Schedule(0.f, EDmInst::Cello, M(0, 2), 0.7f, 9.f, 0.f);
		Schedule(6.f, EDmInst::Taiko, 40.f, 0.7f, 0.3f);
		if (S == EDmStinger::EndingAsh) Schedule(1.f, EDmInst::Breath, M(0, 2), 0.5f, 7.f);
		Len = 10.5f;
		break;
	}
	case EDmStinger::EndingForgotten:
	{
		const int32 Mel[4] = { Fifth, 2, 1, 0 };
		for (int32 I = 0; I < 4; ++I) Schedule(1.2f * float(I), EDmInst::Flute, M(Mel[I], 5), 0.5f - 0.09f * float(I), 1.3f, 0.f);
		Schedule(0.f, EDmInst::Breath, M(0, 3), 0.35f, 6.f, 0.f);
		Len = 7.5f;
		break;
	}
	case EDmStinger::EndingParting:
	{
		Schedule(0.f, EDmInst::Choir, M(0, 3), 0.5f, 4.f, -0.3f);
		Schedule(0.f, EDmInst::Choir, M(3, 3), 0.45f, 4.f, 0.f);
		Schedule(0.f, EDmInst::Choir, M(Fifth, 3), 0.42f, 4.f, 0.3f);
		Schedule(4.f, EDmInst::Choir, M(0, 3), 0.5f, 5.f, -0.3f);
		Schedule(4.f, EDmInst::Choir, M(2, 3), 0.45f, 5.f, 0.f);
		Schedule(4.f, EDmInst::Choir, M(Fifth, 3), 0.42f, 5.f, 0.3f);
		for (int32 I = 0; I < 5; ++I) Schedule(4.f + 0.5f * float(I), EDmInst::Glass, M(I + 2, 5), 0.3f, 1.2f, -0.4f + 0.2f * float(I));
		Len = 9.5f;
		break;
	}
	case EDmStinger::EndingSilence:
	case EDmStinger::EndingQuiet:
	{
		Schedule(0.f, EDmInst::Breath, M(0, 2), 0.9f, 6.f, 0.f);
		Schedule(0.f, EDmInst::Breath, M(0, 3), 0.55f, 5.f, 0.f);
		if (S == EDmStinger::EndingQuiet) Schedule(0.f, EDmInst::Breath, M(Fifth, 3), 0.45f, 6.f, 0.f);
		Len = 8.f;
		break;
	}
	default: break;
	}
	StingerStart = Clock;
	StingerUntil = Clock + Len;
}

void FDmMusic::Render(float* L, float* R, float* SendL, float* SendR, int32 N)
{
	// clock: eighth-note steps and scheduled stinger notes, at block boundaries (~5 ms)
	int32 Guard = 0;
	while (Clock >= NextEighth && Guard++ < 8)
	{
		Step();
		NextEighth += 30.0 / double(Tempo);
	}
	if (Clock >= NextEighth) NextEighth = Clock + 30.0 / double(Tempo);
	for (FSched& S : Sched)
		if (S.bOn && Clock >= S.At)
		{
			S.bOn = false;
			PlayNote(S.Inst, S.Midi, S.Vel, S.Dur, S.Pan, true);
		}
	for (FNote& Nt : Notes)
		if (Nt.bOn) RenderNote(Nt, L, R, SendL, SendR, N);
	Clock += double(N) * double(InvSr);
}

void FDmMusic::RenderNote(FNote& Nt, float* L, float* R, float* SendL, float* SendR, int32 N)
{
	float Gl, Gr;
	PanGains(Nt.Pan, Gl, Gr);
	// stingers are moments: they stand 6 dB over the score they interrupt
	const float Lvl = GLevel[int32(Nt.Inst)] * Nt.Vel * (Nt.bStinger ? 2.f : OutGain * DuckS);
	const float Snd = GSend[int32(Nt.Inst)];
	const float Hz = Nt.Hz;
	const float Inc = Hz * InvSr;

	float Peak = 0.f;
	switch (Nt.Inst)
	{
	case EDmInst::Flute:
	{
		const float Ca = 1.f - std::exp(-InvSr * 3.f / 0.09f), Rm = std::exp(-InvSr * 6.9f / 0.35f);
		for (int32 K = 0; K < N; ++K)
		{
			if (Nt.T < Nt.Gate) Nt.Env += (0.9f - Nt.Env) * Ca; else { Nt.Env *= Rm; Nt.bReleased = true; }
			Nt.Ph[1] += 4.8f * InvSr;
			const float Vib = 1.f + 0.0025f * Sin01(Nt.Ph[1]) * SStep(0.25f, 0.6f, Nt.T);
			Nt.Ph[0] = Wrap01(Nt.Ph[0] + Inc * Vib);
			const float P = Nt.Ph[0];
			const float Tone = Sin01(P) + 0.10f * Sin01(2.f * P) + 0.035f * Sin01(3.f * P);
			const float Chiff = 0.25f * std::exp(-Nt.T * 25.f);
			const float Breath = Nt.F[0].Band(Nt.Rng.Bi()) * (0.05f + Chiff);
			const float S = (Tone + Breath) * Nt.Env * Lvl;
			L[K] += S * Gl; R[K] += S * Gr; SendL[K] += S * Gl * Snd; SendR[K] += S * Gr * Snd;
			Peak = FMath::Max(Peak, Nt.Env);
			Nt.T += InvSr;
		}
		break;
	}
	case EDmInst::HandDrum:
	case EDmInst::Taiko:
	{
		const bool bTaiko = Nt.Inst == EDmInst::Taiko;
		const float Mul = DecayMul(bTaiko ? 1.2f : 0.5f, Sr);
		const float DropT = bTaiko ? 0.05f : 0.03f, Drop = bTaiko ? 0.7f : 0.6f;
		for (int32 K = 0; K < N; ++K)
		{
			const float F = Hz * (1.f + Drop * std::exp(-Nt.T / DropT));
			Nt.Ph[0] = Wrap01(Nt.Ph[0] + F * InvSr);
			const float Slap = Nt.F[0].Band(Nt.Rng.Bi()) * (bTaiko ? 0.45f : 0.35f) * std::exp(-Nt.T * (bTaiko ? 14.f : 30.f));
			const float S = (Sin01(Nt.Ph[0]) * Nt.Env + Slap) * Lvl;
			Nt.Env *= Mul;
			L[K] += S * Gl; R[K] += S * Gr; SendL[K] += S * Gl * Snd; SendR[K] += S * Gr * Snd;
			Nt.T += InvSr;
		}
		Peak = Nt.Env;
		break;
	}
	case EDmInst::Lyre:
	{
		const float Rho = Nt.KsRho;
		const float D = Nt.KsDelay;
		const int32 Mask = KsLen - 1;
		for (int32 K = 0; K < N; ++K)
		{
			const int32 I0 = int32(D);
			const float Fr = D - float(I0);
			const float A = Nt.Ks[(Nt.KsPos - I0) & Mask], B = Nt.Ks[(Nt.KsPos - I0 - 1) & Mask], C = Nt.Ks[(Nt.KsPos - I0 - 2) & Mask];
			const float X0 = A + (B - A) * Fr, X1 = B + (C - B) * Fr;
			const float Y = Rho * (0.55f * X0 + 0.45f * X1);
			Nt.Ks[Nt.KsPos & Mask] = Y;
			Nt.KsPos = (Nt.KsPos + 1) & Mask;
			const float S = Nt.Lp.Tick(Y) * Lvl;
			L[K] += S * Gl; R[K] += S * Gr; SendL[K] += S * Gl * Snd; SendR[K] += S * Gr * Snd;
			Peak = FMath::Max(Peak, std::fabs(Y));
			Nt.T += InvSr;
		}
		if (Nt.T < 0.05f) Peak = 1.f;
		break;
	}
	case EDmInst::Reed:
	{
		const float Ca = 1.f - std::exp(-InvSr * 3.f / 0.07f), Rm = std::exp(-InvSr * 6.9f / 0.2f);
		for (int32 K = 0; K < N; ++K)
		{
			if (Nt.T < Nt.Gate) Nt.Env += (0.9f - Nt.Env) * Ca; else { Nt.Env *= Rm; Nt.bReleased = true; }
			Nt.Ph[1] += 5.4f * InvSr;
			const float Vib = 1.f + 0.003f * Sin01(Nt.Ph[1]) * SStep(0.2f, 0.5f, Nt.T);
			const float I = Inc * Vib;
			Nt.Ph[0] = Wrap01(Nt.Ph[0] + I);
			const float Raw = BlepPulse(Nt.Ph[0], I, 0.32f);
			const float Body = Nt.F[0].Low(Raw);
			const float Nasal = Nt.F[1].Band(Body) * 0.4f;
			const float S = (Body * 0.8f + Nasal) * Nt.Env * Lvl;
			L[K] += S * Gl; R[K] += S * Gr; SendL[K] += S * Gl * Snd; SendR[K] += S * Gr * Snd;
			Peak = FMath::Max(Peak, Nt.Env);
			Nt.T += InvSr;
		}
		break;
	}
	case EDmInst::Strings:
	case EDmInst::Cello:
	{
		const bool bCello = Nt.Inst == EDmInst::Cello;
		const float Att = bCello ? 2.5f : 1.3f, Rel = bCello ? 2.8f : 2.2f;
		const float Ca = 1.f - std::exp(-InvSr * 3.f / Att), Rm = std::exp(-InvSr * 6.9f / Rel);
		const float Cut = bCello ? 480.f : 700.f + 1000.f * Nt.Env;
		if (++Nt.Ctrl >= 1) { Nt.Ctrl = 0; Nt.F[0].Set(FMath::Min(Cut, Hz * 7.f + 300.f), 0.6f, Sr); }
		for (int32 K = 0; K < N; ++K)
		{
			if (Nt.T < Nt.Gate) Nt.Env += (1.f - Nt.Env) * Ca; else { Nt.Env *= Rm; Nt.bReleased = true; }
			Nt.Ph[3] += (bCello ? 4.4f : 5.1f) * InvSr;
			const float Vib = 1.f + 0.0018f * Sin01(Nt.Ph[3]);
			const float I0 = Inc * Vib * 0.99596f, I1 = Inc * Vib, I2 = Inc * Vib * 1.00347f;
			Nt.Ph[0] = Wrap01(Nt.Ph[0] + I0); Nt.Ph[1] = Wrap01(Nt.Ph[1] + I1); Nt.Ph[2] = Wrap01(Nt.Ph[2] + I2);
			const float A = BlepSaw(Nt.Ph[0], I0), B = BlepSaw(Nt.Ph[1], I1), C = BlepSaw(Nt.Ph[2], I2);
			const float F = Nt.F[0].Low(A + B + C);
			const float S = F * Nt.Env * Lvl;
			const float Wl = (A - C) * 0.04f * Nt.Env * Lvl;
			L[K] += S * Gl + Wl; R[K] += S * Gr - Wl; SendL[K] += S * Gl * Snd; SendR[K] += S * Gr * Snd;
			Peak = FMath::Max(Peak, Nt.Env);
			Nt.T += InvSr;
		}
		break;
	}
	case EDmInst::Brass:
	{
		const float Ca = 1.f - std::exp(-InvSr * 3.f / 0.14f), Rm = std::exp(-InvSr * 6.9f / 0.35f);
		const float Bright = Sat01(Nt.T / 0.2f) * (Nt.T < 0.2f ? 1.f : FMath::Max(0.65f, 1.f - (Nt.T - 0.2f) * 0.6f));
		Nt.F[0].Set(FMath::Min(320.f + (Hz * 3.5f + 500.f) * Bright * Nt.Env, 2200.f), 0.9f, Sr);
		for (int32 K = 0; K < N; ++K)
		{
			if (Nt.T < Nt.Gate) Nt.Env += (0.9f - Nt.Env) * Ca; else { Nt.Env *= Rm; Nt.bReleased = true; }
			Nt.Ph[2] += 4.9f * InvSr;
			const float Vib = 1.f + 0.0025f * Sin01(Nt.Ph[2]) * SStep(0.3f, 0.7f, Nt.T);
			const float I0 = Inc * Vib * 0.9977f, I1 = Inc * Vib * 1.0023f;
			Nt.Ph[0] = Wrap01(Nt.Ph[0] + I0); Nt.Ph[1] = Wrap01(Nt.Ph[1] + I1);
			const float F = Nt.F[0].Low(BlepSaw(Nt.Ph[0], I0) + BlepSaw(Nt.Ph[1], I1));
			const float S = F * Nt.Env * Lvl;
			L[K] += S * Gl; R[K] += S * Gr; SendL[K] += S * Gl * Snd; SendR[K] += S * Gr * Snd;
			Peak = FMath::Max(Peak, Nt.Env);
			Nt.T += InvSr;
		}
		break;
	}
	case EDmInst::Pad:
	{
		const float Ca = 1.f - std::exp(-InvSr * 3.f / 2.4f), Rm = std::exp(-InvSr * 6.9f / 3.5f);
		Nt.Aux[1] += float(N) * InvSr * 0.07f;
		const float Cut = 900.f + 450.f * Sin01(Nt.Aux[1] + Nt.Ph[5]);
		Nt.F[0].Set(Cut, 0.7f, Sr);
		Nt.F[1].Set(Cut * 1.08f, 0.7f, Sr);
		static const float Det[5] = { 0.99309f, 0.99712f, 1.f, 1.00347f, 1.00637f };
		for (int32 K = 0; K < N; ++K)
		{
			if (Nt.T < Nt.Gate) Nt.Env += (1.f - Nt.Env) * Ca; else { Nt.Env *= Rm; Nt.bReleased = true; }
			float Sl = 0.f, Srr = 0.f;
			for (int32 V = 0; V < 5; ++V)
			{
				const float I = Inc * Det[V];
				Nt.Ph[V] = Wrap01(Nt.Ph[V] + I);
				const float W = BlepSaw(Nt.Ph[V], I);
				if (V & 1) Sl += W; else Srr += W;
				if (V == 2) { Sl += W * 0.5f; Srr += W * 0.5f; }
			}
			const float FL = Nt.F[0].Low(Sl), FR = Nt.F[1].Low(Srr);
			const float E = Nt.Env * Lvl;
			L[K] += FL * E * (0.5f + 0.5f * Gl); R[K] += FR * E * (0.5f + 0.5f * Gr);
			SendL[K] += FL * E * Snd * 0.7f; SendR[K] += FR * E * Snd * 0.7f;
			Peak = FMath::Max(Peak, Nt.Env);
			Nt.T += InvSr;
		}
		break;
	}
	case EDmInst::Bell:
	{
		const float Mul = DecayMul(4.5f, Sr), IdxMul = DecayMul(1.6f, Sr), TierMul = DecayMul(1.3f, Sr);
		for (int32 K = 0; K < N; ++K)
		{
			Nt.Ph[0] = Wrap01(Nt.Ph[0] + Inc);
			Nt.Ph[1] = Wrap01(Nt.Ph[1] + Inc * 1.4f);
			Nt.Ph[2] = Wrap01(Nt.Ph[2] + Inc * 2.76f);
			const float V = Sin01(Nt.Ph[0] + Nt.Aux[0] * 0.159155f * Sin01(Nt.Ph[1])) + 0.15f * Nt.Aux[2] * Sin01(Nt.Ph[2]);
			if (Nt.T < InvSr * 1.5f) Nt.Aux[2] = 1.f;
			Nt.Aux[0] *= IdxMul;
			Nt.Aux[2] *= TierMul;
			const float Att = Sat01(Nt.T / 0.002f);
			const float S = V * Nt.Env * Att * Lvl;
			Nt.Env *= Mul;
			L[K] += S * Gl; R[K] += S * Gr; SendL[K] += S * Gl * Snd; SendR[K] += S * Gr * Snd;
			Nt.T += InvSr;
		}
		Peak = Nt.Env;
		break;
	}
	case EDmInst::Piano:
	{
		const float Base = 3.f * std::sqrt(Clamp(220.f / Hz, 0.35f, 1.8f));
		const float Damp = Nt.T > Nt.Gate ? 0.35f : 1.f;
		float Muls[6];
		for (int32 P = 0; P < 6; ++P) Muls[P] = DecayMul(Base * Damp / (1.f + 0.6f * float(P)), Sr);
		if (Nt.T < InvSr) for (int32 P = 0; P < 4; ++P) Nt.Aux[P] = 1.f;
		for (int32 K = 0; K < N; ++K)
		{
			float V = 0.f;
			for (int32 P = 0; P < 6; ++P)
			{
				const float Np = float(P + 1);
				Nt.Ph[P] = Wrap01(Nt.Ph[P] + Inc * Np * std::sqrt(1.f + 0.0003f * Np * Np));
				const float Amp = 1.f / std::pow(Np, 1.4f);
				V += Sin01(Nt.Ph[P]) * Amp * (P < 4 ? Nt.Aux[P] : Nt.Aux[3] * Nt.Aux[3]);
			}
			for (int32 P = 0; P < 4; ++P) Nt.Aux[P] *= Muls[P];
			const float Hammer = Nt.Rng.Bi() * 0.08f * std::exp(-Nt.T * 70.f);
			const float S = Nt.Lp.Tick(V * Sat01(Nt.T / 0.003f) + Hammer) * Lvl;
			L[K] += S * Gl; R[K] += S * Gr; SendL[K] += S * Gl * Snd; SendR[K] += S * Gr * Snd;
			Nt.T += InvSr;
		}
		Peak = Nt.Aux[0];
		if (Nt.T > Nt.Gate) Nt.bReleased = true;
		break;
	}
	case EDmInst::Glass:
	{
		const float Mul = DecayMul(1.0f, Sr);
		for (int32 K = 0; K < N; ++K)
		{
			Nt.Ph[0] = Wrap01(Nt.Ph[0] + Inc);
			Nt.Ph[1] = Wrap01(Nt.Ph[1] + Inc * 2.f);
			const float V = Tri(Nt.Ph[0]) * 0.8f + 0.3f * Sin01(Nt.Ph[1]);
			const float S = Nt.Lp.Tick(V) * Nt.Env * Sat01(Nt.T / 0.004f) * Lvl;
			Nt.Env *= Mul;
			L[K] += S * Gl; R[K] += S * Gr; SendL[K] += S * Gl * Snd; SendR[K] += S * Gr * Snd;
			Nt.T += InvSr;
		}
		Peak = Nt.Env;
		break;
	}
	case EDmInst::Breath:
	{
		const float Ca = 1.f - std::exp(-InvSr * 3.f / 3.5f), Rm = std::exp(-InvSr * 6.9f / 5.f);
		for (int32 K = 0; K < N; ++K)
		{
			if (Nt.T < Nt.Gate) Nt.Env += (1.f - Nt.Env) * Ca; else { Nt.Env *= Rm; Nt.bReleased = true; }
			Nt.Ph[0] = Wrap01(Nt.Ph[0] + Inc);
			Nt.Ph[1] = Wrap01(Nt.Ph[1] + Inc * 1.0029f);
			Nt.Ph[2] += 0.11f * InvSr;
			const float Breathe = 0.62f + 0.38f * Sin01(Nt.Ph[2] + Nt.Ph[4]);
			const float Air = Nt.F[0].Band(Nt.Rng.Bi()) * 0.06f;
			const float V = Sin01(Nt.Ph[0]) * 0.8f + 0.25f * Tri(Nt.Ph[1]) + Air;
			const float S = V * Nt.Env * Breathe * Lvl;
			L[K] += S * Gl; R[K] += S * Gr; SendL[K] += S * Gl * Snd; SendR[K] += S * Gr * Snd;
			Peak = FMath::Max(Peak, Nt.Env);
			Nt.T += InvSr;
		}
		break;
	}
	case EDmInst::Choir:
	{
		const float Ca = 1.f - std::exp(-InvSr * 3.f / 0.8f), Rm = std::exp(-InvSr * 6.9f / 1.5f);
		for (int32 K = 0; K < N; ++K)
		{
			if (Nt.T < Nt.Gate) Nt.Env += (1.f - Nt.Env) * Ca; else { Nt.Env *= Rm; Nt.bReleased = true; }
			Nt.Ph[3] += 5.2f * InvSr;
			const float Vib = 1.f + 0.0035f * Sin01(Nt.Ph[3]);
			const float I0 = Inc * Vib * 0.9954f, I1 = Inc * Vib * 1.0046f, I2 = Inc * Vib;
			Nt.Ph[0] = Wrap01(Nt.Ph[0] + I0); Nt.Ph[1] = Wrap01(Nt.Ph[1] + I1); Nt.Ph[2] = Wrap01(Nt.Ph[2] + I2);
			const float Src = BlepSaw(Nt.Ph[0], I0) + BlepSaw(Nt.Ph[1], I1) + 0.6f * BlepSaw(Nt.Ph[2], I2);
			const float V = Nt.F[0].Band(Src) + 0.5f * Nt.F[1].Band(Src) + 0.15f * Nt.F[2].Band(Src);
			const float S = V * Nt.Env * Lvl;
			L[K] += S * Gl; R[K] += S * Gr; SendL[K] += S * Gl * Snd; SendR[K] += S * Gr * Snd;
			Peak = FMath::Max(Peak, Nt.Env);
			Nt.T += InvSr;
		}
		break;
	}
	default:
		Peak = 0.f;
		break;
	}
	const bool bPerc = Nt.Inst == EDmInst::HandDrum || Nt.Inst == EDmInst::Taiko || Nt.Inst == EDmInst::Bell || Nt.Inst == EDmInst::Glass
		|| Nt.Inst == EDmInst::Piano || Nt.Inst == EDmInst::Lyre;
	if (bPerc ? Peak < 2e-4f : (Nt.T > Nt.Gate && Peak < 2e-4f)) Nt.bOn = false;
	if (Nt.T > Nt.Gate + 30.f) Nt.bOn = false;
}
