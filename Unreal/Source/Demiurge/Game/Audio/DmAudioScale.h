// DEMIURGE — Game/Audio/DmAudioScale.h : the world's musical scale (chosen from its seed). Owner: C9 audio.
// Music, bells and UI chimes all play in it, so the whole mix stays in one key.
//
// sound-direction 0 and 1.1: the world's tonic is chosen from the seed among D2 .. G2 (73.4 .. 98 Hz);
// the seed's parity picks a scale family, and each age picks a mode inside it:
//
//   family  Ages 0-1           Ages 2-3     Ages 4-5   Age 6
//   even    major pentatonic   Mixolydian   Dorian     Lydian
//   odd     minor pentatonic   Dorian       Aeolian    Lydian (raised 2 used sparingly)
//
// Pentatonic ages harmonise in their parent mode (Ionian / Aeolian), as the previews do. War flattens
// the 3rd and the 7th (major -> minor colour). The tonic never changes inside a world: only the mode.
#pragma once

#include "CoreMinimal.h"
#include "Game/Audio/DmAudioDsp.h"

struct FDmScale
{
	enum : int32 { Dorian, Aeolian, Mixolydian, Ionian, Lydian, PentaMinor, PentaMajor, Phrygian, LydianSharp2, NumModes };

	int32 Root = 2;          // pitch class (0 = C): the tonic
	int32 Mode = Dorian;
	int32 Steps[7] = { 0, 2, 3, 5, 7, 9, 10 };
	int32 Count = 7;

	static const TCHAR* ModeName(int32 M)
	{
		static const TCHAR* Names[NumModes] = { TEXT("dorian"), TEXT("aeolian"), TEXT("mixolydian"), TEXT("ionian"), TEXT("lydian"), TEXT("penta-minor"),
			TEXT("penta-major"), TEXT("phrygian"), TEXT("lydian-#2") };
		return (M >= 0 && M < NumModes) ? Names[M] : TEXT("?");
	}

	void SetMode(int32 M)
	{
		static const int32 Tab[NumModes][7] = {
			{ 0, 2, 3, 5, 7, 9, 10 },   // dorian
			{ 0, 2, 3, 5, 7, 8, 10 },   // aeolian
			{ 0, 2, 4, 5, 7, 9, 10 },   // mixolydian
			{ 0, 2, 4, 5, 7, 9, 11 },   // ionian
			{ 0, 2, 4, 6, 7, 9, 11 },   // lydian
			{ 0, 3, 5, 7, 10, 0, 0 },   // minor pentatonic
			{ 0, 2, 4, 7, 9, 0, 0 },    // major pentatonic
			{ 0, 1, 3, 5, 7, 8, 10 },   // phrygian
			{ 0, 3, 4, 6, 7, 9, 11 },   // lydian with a raised 2 (the odd family's space age)
		};
		Mode = FMath::Clamp(M, 0, int32(NumModes) - 1);
		Count = (Mode == PentaMinor || Mode == PentaMajor) ? 5 : 7;
		for (int32 I = 0; I < 7; ++I) Steps[I] = Tab[Mode][I];
	}

	/** The seed's tonic pitch class: D, Eb, E, F, F#, G (D2 .. G2). */
	static int32 RootFromSeed(uint32 Seed)
	{
		DmDsp::FRng R(Seed * 2654435761u + 0x1234567u);
		R.Next(); R.Next();
		return 2 + R.Int(6);
	}
	/** 0 = even family, 1 = odd family. */
	static int32 Family(uint32 Seed) { return int32(Seed & 1u); }

	/** The mode of an age in a world (sound-direction 1.1). */
	static int32 AgeMode(uint32 Seed, int32 Age)
	{
		const int32 A = FMath::Clamp(Age, 0, 6);
		static const int32 Even[4] = { PentaMajor, Mixolydian, Dorian, Lydian };
		static const int32 Odd[4] = { PentaMinor, Dorian, Aeolian, LydianSharp2 };
		const int32 K = A <= 1 ? 0 : (A <= 3 ? 1 : (A <= 5 ? 2 : 3));
		return Family(Seed) ? Odd[K] : Even[K];
	}

	/** A world's scale in an age. */
	static FDmScale ForAge(uint32 Seed, int32 Age)
	{
		FDmScale S;
		S.Root = RootFromSeed(Seed);
		S.SetMode(AgeMode(Seed, Age));
		return S;
	}
	/** Kept for callers that only know the seed: the scale of the age of sail. */
	static FDmScale FromSeed(uint32 Seed) { return ForAge(Seed, 3); }

	/** The harmony of this scale: pentatonic modes harmonise in their parent seven-note mode. */
	FDmScale Harmony() const
	{
		FDmScale H = *this;
		if (Mode == PentaMajor) H.SetMode(Ionian);
		else if (Mode == PentaMinor) H.SetMode(Aeolian);
		else if (Mode == LydianSharp2) H.SetMode(Lydian);
		return H;
	}

	/** War (sound-direction 1.3): the 3rd and the 7th are flattened (major -> minor colour). */
	FDmScale Darkened() const
	{
		FDmScale D = *this;
		for (int32 I = 0; I < Count; ++I)
		{
			if (D.Steps[I] == 4) D.Steps[I] = 3;
			else if (D.Steps[I] == 11) D.Steps[I] = 10;
		}
		// keep the steps ascending and distinct (a 3 already present: the raised 2 of lydian-#2)
		for (int32 I = 1; I < Count; ++I) if (D.Steps[I] <= D.Steps[I - 1]) D.Steps[I] = D.Steps[I - 1] + 1;
		return D;
	}

	/** Scale index of the tone nearest to Semis above the tonic. */
	int32 IndexOf(int32 Semis) const
	{
		int32 Best = 0, BestD = 99;
		for (int32 I = 0; I < Count; ++I) { const int32 D = FMath::Abs(Steps[I] - Semis); if (D < BestD) { BestD = D; Best = I; } }
		return Best;
	}

	/** MIDI note of a scale degree (any integer; wraps into octaves) with the tonic in Octave (C4 = octave 4 = 60). */
	float Midi(int32 Degree, int32 Octave) const
	{
		const int32 N = Count;
		int32 Oct = Degree >= 0 ? Degree / N : -((-Degree + N - 1) / N);
		const int32 Idx = Degree - Oct * N;
		return float(12 * (Octave + 1) + Root + Steps[Idx] + 12 * Oct);
	}
	float Hz(int32 Degree, int32 Octave) const { return DmDsp::MidiToHz(Midi(Degree, Octave)); }
	/** The tonic at D2 .. G2 (sound-direction 0). */
	float TonicHz() const { return DmDsp::MidiToHz(float(36 + Root)); }
};
