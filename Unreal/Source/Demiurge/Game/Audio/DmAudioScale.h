// DEMIURGE — Game/Audio/DmAudioScale.h : the world's musical scale (chosen from its seed). Owner: C9 audio.
// Music, bells and UI chimes all play in it, so the whole mix stays in one key.
#pragma once

#include "CoreMinimal.h"
#include "Game/Audio/DmAudioDsp.h"

struct FDmScale
{
	enum : int32 { Dorian, Aeolian, Mixolydian, Ionian, Lydian, PentaMinor, PentaMajor, Phrygian, NumModes };

	int32 Root = 2;          // pitch class (0 = C): the tonic
	int32 Mode = Dorian;
	int32 Steps[7] = { 0, 2, 3, 5, 7, 9, 10 };
	int32 Count = 7;

	static const TCHAR* ModeName(int32 M)
	{
		static const TCHAR* Names[NumModes] = { TEXT("dorian"), TEXT("aeolian"), TEXT("mixolydian"), TEXT("ionian"), TEXT("lydian"), TEXT("penta-minor"), TEXT("penta-major"), TEXT("phrygian") };
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
		};
		Mode = FMath::Clamp(M, 0, int32(NumModes) - 1);
		Count = (Mode == PentaMinor || Mode == PentaMajor) ? 5 : 7;
		for (int32 I = 0; I < 7; ++I) Steps[I] = Tab[Mode][I];
	}

	/** A world's scale: tonic D..A (warm registers), mode weighted toward the mythic ones. */
	static FDmScale FromSeed(uint32 Seed)
	{
		DmDsp::FRng R(Seed * 2654435761u + 0x1234567u);
		R.Next(); R.Next();
		FDmScale S;
		static const int32 Roots[6] = { 2, 3, 4, 5, 7, 9 };        // D, Eb, E, F, G, A
		S.Root = Roots[R.Int(6)];
		static const int32 Modes[10] = { Dorian, Dorian, Aeolian, Aeolian, Mixolydian, Ionian, Lydian, PentaMinor, PentaMajor, Dorian };
		S.SetMode(Modes[R.Int(10)]);
		return S;
	}

	/** The darker sibling (war, disaster, feared endings). */
	FDmScale Darkened() const
	{
		FDmScale D = *this;
		static const int32 Dark[NumModes] = { Aeolian, Phrygian, Dorian, Aeolian, Dorian, PentaMinor, PentaMinor, Phrygian };
		D.SetMode(Dark[Mode]);
		return D;
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
};
