// DEMIURGE — Game/Audio/DmAudioCues.cpp : the cue patches (sound-direction 3.2 and 4) and the voice that plays them.
// Owner: C9 audio.
#include "Game/Audio/DmAudioCues.h"

using namespace DmDsp;

namespace
{
	// Level trim per cue (dB), calibrated by the cue-* offline renders so each cue's peak short-term loudness
	// meets its "re music" level of 3.2 on a -28 LUFS music bus (UI sounds: peak <= -14 dBFS, short-term <= -30 LUFS).
	const float GCueTrimDb[int32(EDmCue::Count)] = {
		/* hover */ 0.f, /* click */ 0.f, /* arm */ 0.f, /* deny */ 0.f, /* unlock */ 0.f, /* thunder */ 7.f, /* impact */ 14.5f,
		/* rumble */ 0.f, /* wave */ -6.9f, /* rainfall */ -9.6f, /* shimmer */ 2.9f, /* chime */ -1.5f, /* choir */ 0.f, /* horn */ -5.5f,
		/* toll */ -4.3f, /* roar */ -3.4f, /* crackle */ 4.9f, /* fall */ 0.f, /* toast */ 0.f, /* chronicle */ 0.f,
	};

	FDmCueOsc& Bell(FDmCuePatch& P, float Hz, float Amp, float Delay, float Decay, float Index, float Ratio, float IndexDecay, float Floor = 0.f)
	{
		FDmCueOsc& O = P.AddOsc();
		O.Wave = FDmCueOsc::Bell; O.Hz = Hz; O.Amp = Amp; O.Delay = Delay; O.Attack = 0.002f; O.Decay = Decay;
		O.FmRatio = Ratio; O.FmIndex = Index; O.FmDecay = IndexDecay; O.FmFloor = Floor;
		return O;
	}
	FDmCueOsc& Tone(FDmCuePatch& P, uint8 Wave, float Hz, float Amp, float Delay, float Attack, float Decay)
	{
		FDmCueOsc& O = P.AddOsc();
		O.Wave = Wave; O.Hz = Hz; O.Amp = Amp; O.Delay = Delay; O.Attack = Attack; O.Decay = Decay;
		return O;
	}
	FDmCueNoise& Noise(FDmCuePatch& P, uint8 Color, uint8 Mode, float Amp, float Hz0, float Hz1, float Sweep, float Q,
		float Attack, float Hold, float Decay, float Delay = 0.f)
	{
		FDmCueNoise& N = P.AddNoise();
		N.Color = Color; N.Mode = Mode; N.Amp = Amp; N.Hz0 = Hz0; N.Hz1 = Hz1; N.Sweep = Sweep; N.Q = Q;
		N.Attack = Attack; N.Hold = Hold; N.Decay = Decay; N.Delay = Delay;
		return N;
	}
	bool IsTonal(EDmCue C)
	{
		switch (C)
		{
		case EDmCue::Hover: case EDmCue::Click: case EDmCue::Arm: case EDmCue::Deny: case EDmCue::Unlock: case EDmCue::Shimmer:
		case EDmCue::Chime: case EDmCue::Choir: case EDmCue::Horn: case EDmCue::Toll: case EDmCue::Toast: case EDmCue::Chronicle: return true;
		default: return false;
		}
	}
}

void BuildCuePatch(EDmCue Cue, const FDmCueParams& Prm, const FDmScale& S, FRng& R, FDmCuePatch& P)
{
	P = FDmCuePatch();
	P.Bus = DmAudio::CueBus(Cue);
	const float Power = Clamp(Prm.Param > 0.f ? Prm.Param : 1.f, 0.05f, 1.5f);
	const float D = Sat01(Prm.Muffle);   // distance d of 3.1
	const int32 F5 = S.IndexOf(7);
	switch (Cue)
	{
	// ---------------------------------------------------------------------------------------- UI (4): dry, short, soft
	case EDmCue::Hover:
	{
		// a sine at 2.2 kHz (1 / 25 ms) and its 2nd harmonic at -14 dB; the pitch steps by scale degree with the button's index
		const int32 Idx = FMath::Max(0, int32(Prm.Param + 0.5f)) % FMath::Max(1, S.Count);
		const float Hz = 2200.f * std::exp2(float(S.Steps[Idx]) / 12.f);
		Tone(P, FDmCueOsc::Sine, Hz, 1.f, 0.f, 0.001f, 0.025f);
		Tone(P, FDmCueOsc::Sine, Hz * 2.f, 0.2f, 0.f, 0.001f, 0.02f);
		P.Dur = 0.05f; P.Release = 0.01f; P.Gain = 0.05f; P.Send = 0.f;
		break;
	}
	case EDmCue::Click:
	{
		// a plucked "tok" at 1.1 kHz (120 ms) and a 2 ms noise tick at 4 kHz, -12 dB
		FDmCueOsc& O = Tone(P, FDmCueOsc::Pluck, 1100.f, 1.f, 0.f, 0.0008f, 0.12f); O.Harm3 = 0.6f;
		Noise(P, FDmCueNoise::White, FDmCueNoise::Bp, 0.25f, 4000.f, 4000.f, 1.f, 1.5f, 0.0005f, 0.f, 0.002f);
		P.Dur = 0.15f; P.Release = 0.02f; P.Gain = 0.11f; P.Send = 0.f;
		break;
	}
	case EDmCue::Arm:
	{
		// two grains a 5th apart, 30 ms apart (3 ms / 250 ms, sine + 20 % 3rd), a soft whoosh at 1.5 kHz; pitch by power group;
		// disarming plays it reversed, 4 dB softer
		const int32 Group = Prm.Variant & 7;
		const bool bDisarm = (Prm.Variant & DmCueVar::ArmDisarm) != 0;
		float A = S.Hz(0, 5), B = S.Hz(F5, 5);
		if (Group == DmCueVar::ArmShape) { A = S.Hz(0, 4); B = S.Hz(F5, 4); }
		else if (Group == DmCueVar::ArmWrath) { A = S.Hz(0, 4); B = A * 1.18921f; }     // a minor-3rd pair, octave 4
		else if (Group == DmCueVar::ArmGrace) { A = S.Hz(0, 5); B = A * 1.25992f; }     // a major-3rd pair, octave 5
		if (bDisarm) Swap(A, B);
		FDmCueOsc& G1 = Tone(P, FDmCueOsc::Sine, A, 1.f, 0.f, 0.003f, 0.25f); G1.Harm3 = 0.2f;
		FDmCueOsc& G2 = Tone(P, FDmCueOsc::Sine, B, 0.9f, 0.03f, 0.003f, 0.25f); G2.Harm3 = 0.2f;
		Noise(P, FDmCueNoise::Pink, FDmCueNoise::Bp, 0.35f, 1500.f, 1500.f, 1.f, 1.f, 0.04f, 0.f, 0.08f);
		P.Dur = 0.32f; P.Release = 0.05f; P.Gain = (bDisarm ? 0.63f : 1.f) * 0.07f; P.Send = 0.f;
		break;
	}
	case EDmCue::Deny:
	{
		// two low sines a minor 2nd apart (220 and 233 Hz), 5 ms / 160 ms, through a soft 1.2 kHz low-pass
		Tone(P, FDmCueOsc::Tri, 220.f, 1.f, 0.f, 0.005f, 0.16f);
		Tone(P, FDmCueOsc::Tri, 233.f, 0.9f, 0.f, 0.005f, 0.16f);
		P.ToneFilter = FDmCuePatch::ToneLp; P.ToneHz0 = 1200.f; P.ToneHz1 = 1200.f; P.ToneSweep = 0.1f;
		P.Dur = 0.2f; P.Release = 0.03f; P.Gain = 0.14f; P.Send = 0.f;
		break;
	}
	case EDmCue::Unlock:
	{
		// the age motif (1-5-8) on the lyre, 70 ms apart, and a bell at 8 with a 1.5 s decay
		const int32 Deg[3] = { 0, F5, S.Count };
		for (int32 I = 0; I < 3; ++I) { FDmCueOsc& O = Tone(P, FDmCueOsc::Pluck, S.Hz(Deg[I], 5), 0.9f, 0.07f * I, 0.001f, 0.6f); O.Harm3 = 0.5f; O.Pan = -0.3f + 0.3f * I; }
		Bell(P, S.Hz(S.Count, 5), 0.6f, 0.14f, 1.5f, 1.2f, 3.5f, 0.3f);
		P.Dur = 1.8f; P.Release = 0.2f; P.Gain = 0.08f; P.Send = 0.f;
		break;
	}
	case EDmCue::Toast:
	{
		// the click at 0.7 and a single bell grain at degree 3 (FM 1 : 3.5, 1 ms / 600 ms)
		FDmCueOsc& O = Tone(P, FDmCueOsc::Pluck, 1100.f, 0.7f, 0.f, 0.0008f, 0.12f); O.Harm3 = 0.6f;
		Bell(P, S.Hz(2, 5), 0.8f, 0.01f, 0.6f, 1.2f, 3.5f, 0.2f);
		P.Dur = 0.6f; P.Release = 0.1f; P.Gain = 0.06f; P.Send = 0.f;
		break;
	}
	case EDmCue::Chronicle:
	{
		// a soft quill: pink noise at 3.5 kHz (3 / 90 ms) three times, 60-110 ms apart, and a low pluck on degree 1, room reverb 20 %
		const float T2 = R.Range(0.06f, 0.11f), T3 = T2 + R.Range(0.06f, 0.11f);
		Noise(P, FDmCueNoise::Pink, FDmCueNoise::Bp, 0.5f, 3500.f, 3500.f, 1.f, 1.5f, 0.003f, 0.f, 0.09f, 0.f);
		Noise(P, FDmCueNoise::Pink, FDmCueNoise::Bp, 0.4f, 3300.f, 3300.f, 1.f, 1.5f, 0.003f, 0.f, 0.09f, T2);
		Noise(P, FDmCueNoise::Pink, FDmCueNoise::Bp, 0.35f, 3700.f, 3700.f, 1.f, 1.5f, 0.003f, 0.f, 0.09f, T3);
		FDmCueOsc& O = Tone(P, FDmCueOsc::Pluck, S.Hz(0, 3), 0.8f, 0.f, 0.002f, 0.4f); O.Harm3 = 0.3f;
		P.Dur = 0.55f; P.Release = 0.1f; P.Gain = 0.05f; P.Send = 0.2f; P.bHall = true;
		break;
	}

	// ---------------------------------------------------------------------------------------- powers and disasters (3.2)
	case EDmCue::Thunder:
	{
		// close: a bright crack (white noise 1 / 35 ms above 1.5 kHz, three impulses 4-9 ms apart); then a long rolling
		// grumble (brown noise below 400 Hz in 6-10 shrinking lumps), darkening to 200 Hz into the sky; far: only the roll,
		// starting 100-600 ms late (the sound travels)
		const bool bClose = D < 0.3f;
		const float Late = bClose ? 0.f : R.Range(0.1f, 0.6f);
		if (bClose)
		{
			Noise(P, FDmCueNoise::White, FDmCueNoise::Hp, 0.9f, 1500.f, 1500.f, 1.f, 0.7f, 0.001f, 0.f, 0.035f);
			P.Grains.Kind = FDmCueGrains::Tick; P.Grains.Rate = 220.f; P.Grains.RampIn = 0.f; P.Grains.Peak = 0.018f; P.Grains.RampOut = 0.004f;
			P.Grains.HzLo = 1500.f; P.Grains.HzHi = 9000.f; P.Grains.Decay = 0.004f; P.Grains.Amp = 0.9f;
		}
		FDmCueNoise& Roll = Noise(P, FDmCueNoise::Brown, FDmCueNoise::Lp, 1.3f, 400.f, 200.f, 6.f, 0.7f, bClose ? 0.04f : 0.25f, 1.6f, 5.f, Late + (bClose ? 0.03f : 0.f));
		Roll.Roll = 0.85f; Roll.RollHz = 3.5f; Roll.HpHz = 35.f;
		P.Dur = 7.f; P.Release = 0.8f; P.Gain = 0.4f * (bClose ? 1.f : 0.48f); P.Send = 0.35f; P.FarEcho = 0.3f;
		break;
	}
	case EDmCue::Impact:
	{
		// a sine 70 -> 38 Hz over 80 ms, a mid thump at 180 Hz (3 / 120 ms), a broadband crack above 800 Hz (-6 dB);
		// the body swells to 0.6 s with a slow shake (5 -> 3 Hz, +/- 4 dB); 40-80 debris clicks over 3 s; sky 40 %
		FDmCueOsc& Boom = Tone(P, FDmCueOsc::Sine, 38.f, 0.15f, 0.f, 0.002f, 1.2f);   // felt more than heard, but under the sub-bass budget
		Boom.GlideStart = 70.f / 38.f; Boom.GlideTime = 0.03f;   // (wave 4b verify) 70 -> 38 Hz within 80 ms (tau 30 ms): this line sat inside the comment, so the boom never fell
		Noise(P, FDmCueNoise::White, FDmCueNoise::Bp, 1.1f, 180.f, 180.f, 1.f, 1.f, 0.003f, 0.f, 0.12f).HpHz = 35.f;
		Noise(P, FDmCueNoise::White, FDmCueNoise::Bp, 0.6f, 2800.f, 2800.f, 1.f, 0.45f, 0.002f, 0.f, 0.06f);   // the crack: 800 Hz - 5 kHz
		FDmCueNoise& Body = Noise(P, FDmCueNoise::Brown, FDmCueNoise::Lp, 1.4f, 250.f, 160.f, 6.f, 0.7f, 0.6f, 0.1f, 5.f);
		Body.Shake = 0.4f; Body.ShakeHz0 = 5.f; Body.ShakeHz1 = 3.f; Body.HpHz = 60.f;   // weight in the low mids (3.1 rule 3)
		// (three noise layers: the crack and the thump share their short lives; the body is the third)
		P.Grains.Kind = FDmCueGrains::Tick; P.Grains.Rate = 30.f; P.Grains.Delay = 0.08f; P.Grains.RampIn = 0.05f; P.Grains.Peak = 0.3f; P.Grains.RampOut = 2.7f;
		P.Grains.HzLo = 1500.f; P.Grains.HzHi = 4000.f; P.Grains.Decay = 0.005f; P.Grains.Amp = 0.25f;
		P.Dur = 9.f; P.Release = 1.f; P.Gain = 0.32f * Power; P.Send = 0.4f; P.FarEcho = 0.4f;
		break;
	}
	case EDmCue::Rumble:
	{
		switch (Prm.Variant)
		{
		case DmCueVar::RumbleQuake:
		{
			// the whole surface moving: brown noise at 90 Hz with an irregular 6-9 Hz shake, a grinding band at 350 Hz (-10 dB),
			// rock cracks (plucks at 150-300 Hz, 0.3 s) 1-3 a second; 600 ms swell, 2 s fade
			const float Dur = Clamp(4.f + 6.f * Power, 4.f, 12.f);
			FDmCueNoise& N = Noise(P, FDmCueNoise::Brown, FDmCueNoise::Bp, 1.8f, 90.f, 90.f, 1.f, 1.2f, 0.6f, Dur - 2.8f, 2.f);
			N.Shake = 0.5f; N.ShakeHz0 = 6.f; N.ShakeHz1 = 9.f; N.Roll = 0.3f; N.RollHz = 0.5f; N.HpHz = 40.f;
			Noise(P, FDmCueNoise::White, FDmCueNoise::Bp, 0.32f, 350.f, 350.f, 1.f, 4.f, 0.6f, Dur - 2.8f, 2.f);
			P.Grains.Kind = FDmCueGrains::Pluck; P.Grains.Rate = 2.f; P.Grains.RampIn = 0.3f; P.Grains.Peak = Dur - 1.5f; P.Grains.RampOut = 1.f;
			P.Grains.HzLo = 150.f; P.Grains.HzHi = 300.f; P.Grains.Decay = 0.3f; P.Grains.Amp = 0.5f;
			P.Dur = Dur; P.Release = 0.6f; P.Gain = 0.47f; P.Send = 0.25f; P.FarEcho = 0.3f;
			break;
		}
		case DmCueVar::RumbleRaise:
		case DmCueVar::RumbleLower:
		case DmCueVar::RumbleAge:
		{
			// clay under a thumb: brown noise at 140 Hz rising (raise) or 100 Hz falling (lower) 10 % a second, a faint grit at 2 kHz
			const bool bRaise = Prm.Variant != DmCueVar::RumbleLower;
			const float Hz0 = bRaise ? 140.f : 100.f;
			const float Dur = Prm.Variant == DmCueVar::RumbleAge ? 3.f : 1.4f;
			Noise(P, FDmCueNoise::Brown, FDmCueNoise::Bp, 1.6f, Hz0, Hz0 * (bRaise ? 1.15f : 0.87f), Dur, 1.f, 0.2f, Dur - 0.8f, 0.6f).HpHz = 40.f;
			Noise(P, FDmCueNoise::White, FDmCueNoise::Bp, 0.08f, 2000.f, 2000.f, 1.f, 1.f, 0.2f, Dur - 0.8f, 0.6f);
			P.Dur = Dur; P.Release = 0.6f; P.Gain = Prm.Variant == DmCueVar::RumbleAge ? 0.25f : 0.34f; P.Send = 0.15f;
			break;
		}
		default:
		{
			// the volcano: no transient, a 1.5 s swell; brown noise below 180 Hz shaken at 4 Hz, irregular deep chuffs at
			// 120 Hz (every 0.8-2 s), crackle at -8 dB, a 4 s fade into the sky
			const float Dur = Clamp(8.f + 4.f * (Power - 0.5f), 8.f, 14.f);
			FDmCueNoise& N = Noise(P, FDmCueNoise::Brown, FDmCueNoise::Lp, 1.6f, 180.f, 180.f, 1.f, 0.7f, 1.5f, Dur - 5.5f, 4.f);
			N.Shake = 0.35f; N.ShakeHz0 = 4.f; N.ShakeHz1 = 4.f; N.HpHz = 45.f;
			FDmCueNoise& Ch = Noise(P, FDmCueNoise::Brown, FDmCueNoise::Bp, 1.4f, 120.f, 120.f, 1.f, 1.2f, 1.5f, Dur - 5.5f, 4.f);
			Ch.Roll = 1.f; Ch.RollHz = 0.8f; Ch.HpHz = 45.f;
			P.Grains.Kind = FDmCueGrains::Tick; P.Grains.Rate = 30.f; P.Grains.RampIn = 1.5f; P.Grains.Peak = Dur - 4.f; P.Grains.RampOut = 3.f;
			P.Grains.HzLo = 1500.f; P.Grains.HzHi = 5000.f; P.Grains.Decay = 0.002f; P.Grains.Amp = 0.2f;
			P.Dur = Dur; P.Attack = 0.2f; P.Release = 1.f; P.Gain = 0.38f * FMath::Min(1.f, Power); P.Send = 0.3f; P.FarEcho = 0.3f;
			break;
		}
		}
		break;
	}
	case EDmCue::Wave:
	{
		// the sea standing up: a rising wall (pink noise 300 -> 900 Hz, +15 dB over 2 s), the crash (broadband below 6 kHz,
		// peaking at 2.2 s, and brown noise below 250 Hz), the receding hiss (4 kHz -> 700 Hz) with gurgles (10 a second, fading)
		const bool bComet = Prm.Variant == DmCueVar::WaveComet;
		Noise(P, FDmCueNoise::Pink, FDmCueNoise::Bp, 0.9f, 300.f, 900.f, 2.f, 0.9f, 2.f, 0.2f, 1.8f);
		Noise(P, FDmCueNoise::Pink, FDmCueNoise::Lp, 0.9f, 6000.f, 700.f, 7.f, 0.7f, 0.4f, 0.5f, 6.f, 1.8f);
		Noise(P, FDmCueNoise::Brown, FDmCueNoise::Lp, 1.6f, 250.f, 250.f, 1.f, 0.7f, 0.4f, 1.f, 3.f, 1.8f).HpHz = 40.f;
		P.Grains.Kind = FDmCueGrains::Tone; P.Grains.Rate = 10.f; P.Grains.Delay = 4.5f; P.Grains.RampIn = 0.3f; P.Grains.Peak = 0.8f; P.Grains.RampOut = 4.f;
		P.Grains.HzLo = 300.f; P.Grains.HzHi = 900.f; P.Grains.Decay = 0.04f; P.Grains.Amp = 0.12f; P.Grains.Glide = 1.3f;
		P.Dur = bComet ? 8.f : 10.f; P.Release = 1.f; P.Gain = bComet ? 0.17f : 0.3f; P.Send = 0.3f; P.FarEcho = bComet ? 0.f : 0.3f;
		break;
	}
	case EDmCue::Rainfall:
	{
		// cool rain where it is painted: the rain bed (pink noise between 500 Hz and 8 kHz, a patter of drops) after a 400 ms
		// attack, and 15-30 first drops (2.5-5 kHz) in the first 600 ms; a 2 s release
		const float Held = Prm.Param > 0.f ? Clamp(Prm.Param, 0.2f, 10.f) : 0.6f;
		Noise(P, FDmCueNoise::Pink, FDmCueNoise::Bp, 0.55f, 2000.f, 2000.f, 1.f, 0.35f, 0.4f, Held, 1.8f);
		FDmCueNoise& Pat = Noise(P, FDmCueNoise::White, FDmCueNoise::Bp, 0.22f, 3500.f, 3500.f, 1.f, 1.5f, 0.4f, Held, 1.8f);
		Pat.Roll = 0.9f; Pat.RollHz = 30.f;
		P.Grains.Kind = FDmCueGrains::Tone; P.Grains.Rate = 38.f; P.Grains.RampIn = 0.f; P.Grains.Peak = 0.6f; P.Grains.RampOut = 0.1f;
		P.Grains.HzLo = 2500.f; P.Grains.HzHi = 5000.f; P.Grains.Decay = 0.012f; P.Grains.Amp = 0.35f;
		P.Dur = Held + 2.4f; P.Attack = 0.05f; P.Release = 0.6f; P.Gain = 0.22f; P.Send = 0.2f;
		break;
	}
	case EDmCue::Shimmer:
	{
		// six sine grains rising on degrees 1-2-3-5-6-8 two octaves above the tonic (D6 for a D world), 40 ms apart
		// (5 ms / 600 ms), over a breathy pad at 2.5 kHz swelling over 1 s, sky 50 %; inspire: a 5th higher and a held bell
		const bool bInspire = Prm.Variant == DmCueVar::ShimmerInspire;
		const int32 Up = bInspire ? F5 : 0;
		const int32 Semis[6] = { 0, 2, 4, 7, 9, 12 };
		for (int32 I = 0; I < 6; ++I)
		{
			const int32 Deg = (Semis[I] == 12 ? S.Count : S.IndexOf(Semis[I] == 4 && S.Count == 5 && S.Steps[1] == 3 ? 3 : Semis[I])) + Up;
			FDmCueOsc& O = Tone(P, FDmCueOsc::Sine, S.Hz(Deg, 6), 0.55f, 0.04f * I, 0.005f, 0.6f);
			O.Pan = -0.5f + 0.2f * I;
		}
		if (bInspire) Bell(P, S.Hz(Up + S.Count, 5), 0.5f, 0.3f, 2.5f, 1.2f, 3.5f, 0.5f);
		Noise(P, FDmCueNoise::Pink, FDmCueNoise::Bp, 0.12f, 2500.f, 2500.f, 1.f, 2.f, 1.f, 0.5f, 1.8f);
		P.Dur = 4.f; P.Release = 0.6f; P.Gain = 0.11f; P.Send = 0.5f;
		break;
	}
	case EDmCue::Chime:
	{
		// a warm bell that blooms (FM 1 : 1.4, index 2 -> 0.2 over 1 s) on degree 5, a reply from the tonic an octave lower
		// 120 ms later, a soft pad on 1 and 3, sky 40 %; Omen of Peace: three bells 5, 3, 1 descending, 250 ms apart
		if (Prm.Variant == DmCueVar::ChimePeace)
		{
			Bell(P, S.Hz(F5, 5), 1.f, 0.f, 4.f, 2.f, 1.4f, 0.35f, 0.2f);
			Bell(P, S.Hz(S.IndexOf(4), 5), 0.9f, 0.25f, 4.f, 2.f, 1.4f, 0.35f, 0.2f);
			Bell(P, S.Hz(0, 5), 0.9f, 0.5f, 4.5f, 2.f, 1.4f, 0.35f, 0.2f);
		}
		else
		{
			Bell(P, S.Hz(F5, 5), 1.f, 0.f, 4.f, 2.f, 1.4f, 0.35f, 0.2f);
			Bell(P, S.Hz(0, 4), 0.9f, 0.12f, 4.5f, 2.f, 1.4f, 0.35f, 0.2f);
		}
		Tone(P, FDmCueOsc::Sine, S.Hz(0, 4), 0.18f, 0.1f, 0.4f, 3.f);
		Tone(P, FDmCueOsc::Sine, S.Hz(S.IndexOf(4), 4), 0.14f, 0.1f, 0.4f, 3.f);
		P.Dur = Prm.Variant == DmCueVar::ChimePeace ? 5.5f : 5.f; P.Release = 0.6f; P.Gain = 0.1f; P.Send = 0.4f;
		break;
	}
	case EDmCue::Choir:
	{
		// distant people on an open "ah": sawtooths +/- 8 cents with vibrato 5 Hz +/- 10 cents through formants at 700, 1220 and
		// 2600 Hz; a 1-5 dyad moving to 1-6 half way; hall 2 s; awaken: a single voice, very soft
		const bool bAwaken = Prm.Variant == DmCueVar::ChoirAwaken;
		const float Root = S.Hz(0, 3);
		auto Voice = [&](float Hz, float Amp, float Delay, float Hold, float Decay, float Det)
		{
			FDmCueOsc& O = Tone(P, FDmCueOsc::Saw, Hz * std::exp2(Det / 1200.f), Amp, Delay, 0.8f, Decay);
			O.Hold = Hold; O.VibHz = 4.8f + R.Range(0.f, 0.5f); O.VibDepth = 0.0058f; O.Pan = R.Range(-0.4f, 0.4f);
		};
		if (bAwaken) Voice(Root, 0.6f, 0.f, 3.2f, 2.f, 0.f);
		else
		{
			Voice(Root, 0.5f, 0.f, 4.f, 2.f, R.Range(-8.f, 8.f));
			Voice(Root, 0.45f, 0.f, 4.f, 2.f, R.Range(-8.f, 8.f));
			Voice(S.Hz(F5, 3), 0.45f, 0.f, 1.6f, 1.2f, R.Range(-8.f, 8.f));
			Voice(S.Hz(S.IndexOf(9), 3), 0.42f, 2.6f, 1.8f, 1.5f, R.Range(-8.f, 8.f));
		}
		P.ToneFilter = FDmCuePatch::ToneFormant;
		P.Dur = bAwaken ? 5.5f : 6.5f; P.Attack = 0.05f; P.Release = 1.2f; P.Gain = bAwaken ? 0.155f : 0.175f; P.Send = 0.55f; P.bHall = true;
		break;
	}
	case EDmCue::Horn:
	{
		// a long horn call over hills: two calls (the tonic two octaves above the root, then a 4th higher), each through a low-pass
		// swelling 500 Hz -> 2 kHz (Q 1.2); hall 2 s and the far echo. The age decides the horn: Stone-Iron a horn, Sail brass,
		// Industry-Atomic a very soft two-tone, Space a long synthesised brass pad
		const int32 Age = FMath::Clamp(int32(Prm.Param + 0.5f), 0, 6);
		const float F1 = S.TonicHz() * 4.f, F2 = F1 * 1.33484f;
		if (Age <= 2)
		{
			FDmCueOsc& A = Tone(P, FDmCueOsc::Saw, F1, 0.6f, 0.f, 0.15f, 0.6f); A.Hold = 1.4f; A.GlideStart = 0.97f; A.GlideTime = 0.1f; A.VibHz = 4.5f; A.VibDepth = 0.003f;
			FDmCueOsc& B = Tone(P, FDmCueOsc::Saw, F2, 0.55f, 1.9f, 0.15f, 0.8f); B.Hold = 1.f; B.GlideStart = 0.97f; B.GlideTime = 0.1f; B.VibHz = 4.5f; B.VibDepth = 0.003f;
		}
		else if (Age == 3)
		{
			for (int32 K = 0; K < 2; ++K)
			{
				const float Det = K ? 1.0023f : 0.9977f;
				FDmCueOsc& A = Tone(P, FDmCueOsc::Saw, F1 * Det, 0.4f, 0.f, 0.15f, 0.6f); A.Hold = 1.4f;
				FDmCueOsc& B = Tone(P, FDmCueOsc::Saw, F2 * Det, 0.38f, 1.9f, 0.15f, 0.8f); B.Hold = 1.f;
			}
		}
		else if (Age <= 5)
		{
			FDmCueOsc& A = Tone(P, FDmCueOsc::Tri, F1, 0.5f, 0.f, 0.3f, 0.5f); A.Hold = 1.2f;
			FDmCueOsc& B = Tone(P, FDmCueOsc::Tri, F2, 0.5f, 1.6f, 0.3f, 0.8f); B.Hold = 1.f;
		}
		else
		{
			for (int32 K = 0; K < 3; ++K)
			{
				const float Det = std::exp2(float(K - 1) * 10.f / 1200.f);
				FDmCueOsc& A = Tone(P, FDmCueOsc::Saw, F1 * Det, 0.3f, 0.f, 0.6f, 1.f); A.Hold = 1.4f;
				FDmCueOsc& B = Tone(P, FDmCueOsc::Saw, F2 * Det, 0.28f, 1.9f, 0.6f, 1.2f); B.Hold = 0.8f;
			}
		}
		P.ToneFilter = FDmCuePatch::ToneLp; P.ToneHz0 = 500.f; P.ToneHz1 = 2000.f; P.ToneSweep = 0.5f; P.ToneQ = 1.2f; P.ToneRetrig = 1.9f;
		P.Dur = 4.2f; P.Attack = 0.05f; P.Release = 0.8f; P.Gain = (Age >= 4 && Age <= 5) ? 0.12f : 0.14f; P.Send = 0.4f; P.bHall = true; P.FarEcho = 0.3f;
		break;
	}
	case EDmCue::Toll:
	{
		// one low bell far away: FM 1 : 2.76 (index 3 -> 0.5) on the tonic an octave above the root (3 ms attack), a hum partial
		// at 0.5x and a tierce at 1.2x, a 6 s decay into the hall
		const float F = S.TonicHz() * 2.f;
		Bell(P, F, 1.f, 0.f, 6.f, 3.f, 2.76f, 0.8f, 0.5f).Attack = 0.003f;
		Tone(P, FDmCueOsc::Sine, F * 0.5f, 0.5f, 0.f, 0.003f, 6.5f);
		Tone(P, FDmCueOsc::Sine, F * 1.2f, 0.35f, 0.f, 0.003f, 4.5f);
		P.Dur = 7.f; P.Release = 0.8f; P.Gain = 0.13f; P.Send = 0.5f; P.bHall = true;
		break;
	}
	case EDmCue::Crackle:
	{
		// fire eating dry wood: 20-60 impulses a second (1-3 ms of noise at 1.5-5 kHz), a soft brown bed below 300 Hz, pops
		FDmCueNoise& N = Noise(P, FDmCueNoise::Brown, FDmCueNoise::Lp, 0.5f, 300.f, 300.f, 1.f, 0.7f, 0.15f, 1.3f, 0.6f);
		N.Roll = 0.4f; N.RollHz = 3.f; N.HpHz = 45.f;
		FDmCueNoise& Pop = Noise(P, FDmCueNoise::White, FDmCueNoise::Bp, 0.35f, 800.f, 800.f, 1.f, 1.f, 0.05f, 1.4f, 0.5f);
		Pop.Roll = 1.f; Pop.RollHz = 6.f;
		P.Grains.Kind = FDmCueGrains::Tick; P.Grains.Rate = R.Range(20.f, 60.f); P.Grains.RampIn = 0.1f; P.Grains.Peak = 1.4f; P.Grains.RampOut = 0.5f;
		P.Grains.HzLo = 1500.f; P.Grains.HzHi = 5000.f; P.Grains.Decay = 0.002f; P.Grains.Amp = 0.55f;
		P.Dur = 2.1f; P.Release = 0.4f; P.Gain = 0.22f; P.Send = 0.2f;
		break;
	}
	case EDmCue::Roar:
	{
		if (Prm.Variant == DmCueVar::RoarComet)
		{
			// the comet: a soft roar whose band falls from 5 kHz to 1.2 kHz over 2.5 s
			Noise(P, FDmCueNoise::Pink, FDmCueNoise::Bp, 0.55f, 5000.f, 1200.f, 2.5f, 1.5f, 1.5f, 0.5f, 1.f);
			Noise(P, FDmCueNoise::Brown, FDmCueNoise::Lp, 1.5f, 500.f, 300.f, 2.5f, 0.7f, 1.8f, 0.3f, 1.f).HpHz = 45.f;   // a soft roar: the body carries it, so the high band never turns harsh
			P.Dur = 3.5f; P.Release = 0.6f; P.Gain = 0.31f; P.Send = 0.35f;   // -28 LUFS short-term "re music" (3.2)
		}
		else
		{
			// the launch: an ignition thump (60 Hz, 20 / 300 ms), then a deep crackling roar fading in over 1.5 s (brown + pink
			// 1 : 0.5 below 1.2 kHz, above 45 Hz, crackle 80-200 a second at 2 kHz, a 7 Hz shake), its low-pass falling to 500 Hz
			// as it climbs; then distance takes over (to 250 Hz) and the sky rings
			Tone(P, FDmCueOsc::Sine, 60.f, 0.8f, 0.f, 0.02f, 0.3f);
			FDmCueNoise& A = Noise(P, FDmCueNoise::Brown, FDmCueNoise::Lp, 1.3f, 1200.f, 250.f, 16.f, 0.7f, 1.5f, 9.f, 7.f, 0.25f);
			A.Shake = 0.22f; A.ShakeHz0 = 7.f; A.ShakeHz1 = 7.f; A.HpHz = 45.f;
			Noise(P, FDmCueNoise::Pink, FDmCueNoise::Lp, 0.65f, 1200.f, 250.f, 16.f, 0.7f, 1.5f, 9.f, 7.f, 0.25f).HpHz = 45.f;
			P.Grains.Kind = FDmCueGrains::Tick; P.Grains.Rate = 140.f; P.Grains.Delay = 0.5f; P.Grains.RampIn = 1.5f; P.Grains.Peak = 10.f; P.Grains.RampOut = 4.f;
			P.Grains.HzLo = 1600.f; P.Grains.HzHi = 2500.f; P.Grains.Decay = 0.002f; P.Grains.Amp = 0.12f;
			P.Dur = 18.f; P.Attack = 0.01f; P.Release = 2.f; P.Gain = 0.3f; P.Send = 0.45f; P.FarEcho = 0.25f;
		}
		break;
	}
	case EDmCue::Fall:
	{
		// something enormous tearing through the air: pink noise through a band falling 3 kHz -> 400 Hz (the descent, never a
		// whistle), a growing rumble below 300 Hz, rising 18 dB; cut by the impact (masked by its attack)
		const float Eta = Clamp(Prm.Param > 0.f ? Prm.Param : 2.5f, 0.6f, 8.f);
		Noise(P, FDmCueNoise::Pink, FDmCueNoise::Bp, 1.f, 3000.f, 400.f, Eta, 1.5f, Eta, 0.f, 0.3f);
		Noise(P, FDmCueNoise::Brown, FDmCueNoise::Lp, 0.8f, 300.f, 300.f, 1.f, 0.7f, Eta, 0.f, 0.3f).HpHz = 40.f;
		P.Dur = Eta + 0.06f; P.Attack = FMath::Min(1.5f, Eta * 0.6f); P.Release = 0.05f; P.Gain = 0.32f; P.Send = 0.25f;
		break;
	}
	default: break;
	}

	// no exact repeats (0, 3.1 rule 5): +/- 3 % pitch on noise cues, +/- 5 cents on tonal ones (they stay in key), +/- 1.5 dB
	const float Pitch = IsTonal(Cue) ? std::exp2(R.Range(-5.f, 5.f) / 1200.f) : R.Range(0.97f, 1.03f);
	for (int32 I = 0; I < P.NumOsc; ++I) P.Osc[I].Hz *= Pitch;
	for (int32 I = 0; I < P.NumNoise; ++I) { P.Noise[I].Hz0 *= Pitch; P.Noise[I].Hz1 *= Pitch; }
	P.Grains.HzLo *= Pitch; P.Grains.HzHi *= Pitch;
	P.Gain *= DbToGain(R.Range(-1.5f, 1.5f) + GCueTrimDb[int32(Cue)]);
}

// ------------------------------------------------------------------------------------------------ voice

void FDmCueVoice::Start(EDmCue InCue, const FDmCuePatch& InPatch, const FDmCueParams& P, const FDmScale& InScale, float InSr, uint32 Seed, int64 Order)
{
	Cue = InCue;
	Variant = P.Variant;
	Patch = InPatch;
	Scale = InScale;
	Sr = InSr;
	InvSr = 1.f / Sr;
	T = 0.f;
	Sample = 0;
	Delay = int64(FMath::Max(0.f, P.Delay) * Sr);
	Vol = FMath::Max(0.f, P.Vol);
	VolIn = Vol;
	Muffle = Sat01(P.Muffle);
	DurSec = Patch.Dur + FMath::Max(0.f, P.Delay);
	Pan = Clamp(P.Pan, -1.f, 1.f);
	Rng = FRng(Seed);
	StartOrder = Order;
	LastLevel = 1.f;
	KillMul = 1.f; KillStep = 0.f;
	bActive = true;
	CtrlCount = 0;

	for (int32 I = 0; I < Patch.NumOsc; ++I)
	{
		const FDmCueOsc& O = Patch.Osc[I];
		OscPh[I] = Rng.Uni();
		OscMod[I] = 0.f;
		OscEnv[I] = 1.f;
		OscMul[I] = DecayMul(O.Decay, Sr);
		OscGlide[I] = 1.f;
		OscGlideMul[I] = O.GlideTime > 0.f ? std::exp(-1.f / (O.GlideTime * Sr)) : 0.f;
		OscFm[I] = FMath::Max(0.f, O.FmIndex - O.FmFloor);
		OscFmMul[I] = std::exp(-1.f / (FMath::Max(0.01f, O.FmDecay) * Sr));
		OscVib[I] = Rng.Uni();
		OscLp[I].Reset();
		OscLp[I].Set(FMath::Min(O.Hz * 8.f, 12000.f), Sr);
		PanGains(Clamp(Pan + O.Pan, -1.f, 1.f), OscGl[I], OscGr[I]);
	}
	ToneSvf.Reset();
	for (int32 I = 0; I < 3; ++I) { FormSvf[I].Reset(); FormSvf[I].Set(Patch.Formant[I], Patch.FormantQ[I], Sr); }
	for (int32 I = 0; I < Patch.NumNoise; ++I)
	{
		NzEnv[I] = 1.f;
		NzMul[I] = DecayMul(Patch.Noise[I].Decay, Sr);
		NzSvf[I].Reset();
		NzPink[I] = FPink();
		NzBrown[I] = FBrown();
		NzRoll[I].Init(FMath::Max(0.1f, Patch.Noise[I].RollHz), Sr);
		NzShakePh[I] = Rng.Uni();
		NzHp[I] = FHp1();
		NzHp[I].Set(FMath::Max(1.f, Patch.Noise[I].HpHz), Sr);
	}
	for (int32 G = 0; G < MaxGrains; ++G) { Grains[G].bOn = false; Grains[G].Ks = GrainBuf + G * GrainKs; }
	// distance (3.1 rule 2): low-pass at 20 kHz x (1 - 0.85 d)
	const float MuffleHz = FMath::Max(900.f, 20000.f * (1.f - 0.85f * Muffle));
	MuffleL.Reset(); MuffleR.Reset();
	MuffleL.Set(MuffleHz, Sr); MuffleR.Set(MuffleHz, Sr);
	PanGains(Pan, Gl, Gr);
	Control();
}

void FDmCueVoice::Kill(float Sec)
{
	if (!bActive || KillMul < 1.f) return;
	KillStep = 1.f / FMath::Max(1.f, Sec * Sr);
	KillMul = 1.f - KillStep;
}

void FDmCueVoice::Control()
{
	auto SweepHz = [](float Hz0, float Hz1, float Sweep, float Local)
	{
		const float U = Sweep > 0.f ? Sat01(Local / Sweep) : 1.f;
		return Hz0 * std::pow(FMath::Max(Hz1, 1.f) / FMath::Max(Hz0, 1.f), U);
	};
	if (Patch.ToneFilter == FDmCuePatch::ToneLp)
	{
		float Local = T;
		if (Patch.ToneRetrig > 0.f) Local = std::fmod(T, Patch.ToneRetrig);
		ToneSvf.Set(SweepHz(Patch.ToneHz0, Patch.ToneHz1, Patch.ToneSweep, Local), Patch.ToneQ, Sr);
	}
	for (int32 I = 0; I < Patch.NumNoise; ++I)
	{
		const FDmCueNoise& N = Patch.Noise[I];
		NzSvf[I].Set(SweepHz(N.Hz0, N.Hz1, N.Sweep, FMath::Max(0.f, T - N.Delay)), N.Q, Sr);
	}
	for (int32 I = 0; I < Patch.NumOsc; ++I)
		if (Patch.Osc[I].Wave == FDmCueOsc::Pluck)
		{
			// a plucked tone: bright at the attack, mellowing as it rings (the Karplus-Strong character)
			const FDmCueOsc& O = Patch.Osc[I];
			const float Local = FMath::Max(0.f, T - O.Delay);
			OscLp[I].Set(FMath::Min(12000.f, O.Hz * (1.5f + 10.f * O.Harm3 * std::exp(-Local * 12.f))), Sr);
		}
}

void FDmCueVoice::SpawnGrain()
{
	for (FGrain& G : Grains)
	{
		if (G.bOn) continue;
		const FDmCueGrains& D = Patch.Grains;
		G.bOn = true;
		G.Amp = D.Amp * Rng.Range(0.3f, 1.f);
		if (D.Kind == FDmCueGrains::Tick) G.Amp *= Rng.Chance(0.15f) ? 1.f : 0.45f;
		G.Mul = DecayMul(D.Decay * Rng.Range(0.6f, 1.4f), Sr);
		G.Pan = Rng.Bi() * 0.6f;
		G.Ph = Rng.Uni();
		float Hz;
		if (D.bScale)
		{
			const int32 Oct = D.OctLo + Rng.Int(FMath::Max(1, D.OctHi - D.OctLo + 1));
			Hz = Scale.Hz(Rng.Int(Scale.Count), Oct);
		}
		else Hz = D.HzLo * std::pow(D.HzHi / FMath::Max(D.HzLo, 1.f), Rng.Uni());
		G.Inc = Hz * InvSr;
		G.IncMul = D.Glide != 1.f ? std::pow(D.Glide, 1.f / FMath::Max(1.f, D.Decay * Sr)) : 1.f;
		G.Lp.Reset(); G.Lp.Set(Hz * 1.6f, Sr);
		G.Hp.Set(Hz * 0.6f, Sr);
		if (D.Kind == FDmCueGrains::Pluck)
		{
			G.KsLen = FMath::Clamp(int32(Sr / Hz), 8, GrainKs - 1);
			G.KsPos = 0;
			G.KsRho = FMath::Min(0.999f, std::pow(10.f, -3.f / (FMath::Max(0.02f, D.Decay) * Hz)));
			FLp1 Ex; Ex.Set(3000.f, Sr);
			for (int32 K = 0; K < G.KsLen; ++K) G.Ks[K] = Ex.Tick(Rng.Bi()) * 1.5f;
			G.Mul = DecayMul(D.Decay * 1.5f, Sr);
		}
		return;
	}
}

bool FDmCueVoice::Render(float* L, float* R, float* HallL, float* HallR, float* SkyL, float* SkyR, float* EchoL, float* EchoR, int32 N)
{
	if (!bActive) return false;
	float Peak = 0.f;
	const FDmCuePatch& P = Patch;
	const int64 AttackS = FMath::Max<int64>(1, int64(P.Attack * Sr));
	const int64 DurS = int64(P.Dur * Sr);
	const int64 RelS = FMath::Max<int64>(1, int64(P.Release * Sr));
	float* SendL = P.bHall ? HallL : SkyL;
	float* SendR = P.bHall ? HallR : SkyR;
	for (int32 K = 0; K < N; ++K)
	{
		if (Delay > 0) { --Delay; continue; }
		if (++CtrlCount >= 32) { CtrlCount = 0; Control(); }
		// whole-voice envelope
		float G = 1.f;
		if (Sample < AttackS) G = float(Sample) / float(AttackS);
		const int64 RelStart = DurS - RelS;
		if (Sample > RelStart) G *= FMath::Max(0.f, 1.f - float(Sample - RelStart) / float(RelS));
		if (Sample >= DurS) { bActive = false; break; }
		if (KillMul < 1.f)
		{
			KillMul -= KillStep;
			if (KillMul <= 0.f) { bActive = false; break; }
			G *= KillMul;
		}

		// oscillators
		float ToneL = 0.f, ToneR = 0.f, ToneM = 0.f;
		for (int32 I = 0; I < P.NumOsc; ++I)
		{
			const FDmCueOsc& O = P.Osc[I];
			if (T < O.Delay) continue;
			const float Local = T - O.Delay;
			float Env;
			if (Local < O.Attack) Env = Local / O.Attack;
			else if (Local < O.Attack + O.Hold) Env = 1.f;
			else { Env = OscEnv[I]; OscEnv[I] *= OscMul[I]; }
			if (Env < 1e-5f && Local > O.Attack + O.Hold) continue;
			OscGlide[I] *= OscGlideMul[I];
			float Hz = O.Hz * (1.f + (O.GlideStart - 1.f) * OscGlide[I]);
			if (O.VibDepth > 0.f)
			{
				OscVib[I] += O.VibHz * InvSr;
				if (OscVib[I] >= 1.f) OscVib[I] -= 1.f;
				Hz *= 1.f + O.VibDepth * Sin01(OscVib[I]) * SStep(0.f, 0.4f, Local);
			}
			const float Inc = Hz * InvSr;
			OscPh[I] += Inc;
			if (OscPh[I] >= 1.f) OscPh[I] -= 1.f;
			float V;
			switch (O.Wave)
			{
			case FDmCueOsc::Saw: V = BlepSaw(OscPh[I], Inc); break;
			case FDmCueOsc::Tri: V = Tri(OscPh[I]); break;
			case FDmCueOsc::Pluck: V = OscLp[I].Tick(BlepSaw(OscPh[I], Inc)) * 1.6f; break;
			case FDmCueOsc::Bell:
			{
				OscMod[I] += Inc * O.FmRatio;
				if (OscMod[I] >= 1.f) OscMod[I] -= 1.f;
				const float Idx = O.FmFloor + OscFm[I];
				OscFm[I] *= OscFmMul[I];
				V = Sin01(OscPh[I] + Idx * 0.159155f * Sin01(OscMod[I]));
				break;
			}
			default:
				V = Sin01(OscPh[I]);
				if (O.Harm3 > 0.f) V += O.Harm3 * Sin01(3.f * OscPh[I]);
				break;
			}
			const float S = V * O.Amp * Env;
			ToneL += S * OscGl[I];
			ToneR += S * OscGr[I];
			ToneM += S;
		}
		if (P.ToneFilter == FDmCuePatch::ToneLp)
		{
			const float F = ToneSvf.Low(ToneM);
			ToneL = F * Gl * 1.41f; ToneR = F * Gr * 1.41f;
		}
		else if (P.ToneFilter == FDmCuePatch::ToneFormant)
		{
			const float F = FormSvf[0].Band(ToneM) * P.FormantGain[0] + FormSvf[1].Band(ToneM) * P.FormantGain[1] + FormSvf[2].Band(ToneM) * P.FormantGain[2];
			const float Side = (ToneL - ToneR) * 0.15f;
			ToneL = F * Gl * 1.41f + Side; ToneR = F * Gr * 1.41f - Side;
		}

		// noise layers
		float Nz = 0.f;
		for (int32 I = 0; I < P.NumNoise; ++I)
		{
			const FDmCueNoise& Dn = P.Noise[I];
			if (T < Dn.Delay) continue;
			const float Local = T - Dn.Delay;
			float Env;
			if (Local < Dn.Attack) Env = Local / Dn.Attack;
			else if (Local < Dn.Attack + Dn.Hold) Env = 1.f;
			else { Env = NzEnv[I]; NzEnv[I] *= NzMul[I]; }
			if (Env < 1e-6f && Local > Dn.Attack + Dn.Hold) continue;
			const float W = Rng.Bi();
			const float Src = Dn.Color == FDmCueNoise::White ? W : (Dn.Color == FDmCueNoise::Pink ? NzPink[I].Tick(W) * 3.f : NzBrown[I].Tick(W));
			float F;
			if (Dn.Mode == FDmCueNoise::Bp) F = NzSvf[I].Band(Src);
			else if (Dn.Mode == FDmCueNoise::Hp) F = NzSvf[I].High(Src);
			else F = NzSvf[I].Low(Src);
			if (Dn.HpHz > 0.f) F = NzHp[I].Tick(F);
			if (Dn.Roll > 0.f) Env *= 1.f - Dn.Roll + Dn.Roll * (0.5f + 0.5f * NzRoll[I].Tick(Rng)) * 1.4f;
			if (Dn.Shake > 0.f)
			{
				const float Hz = Dn.ShakeHz0 + (Dn.ShakeHz1 - Dn.ShakeHz0) * Sat01(Local / FMath::Max(0.1f, Dn.Attack + Dn.Hold + Dn.Decay * 0.5f));
				NzShakePh[I] += Hz * InvSr;
				if (NzShakePh[I] >= 1.f) NzShakePh[I] -= 1.f;
				Env *= 1.f - Dn.Shake * (0.5f + 0.5f * Sin01(NzShakePh[I]));
			}
			Nz += F * Dn.Amp * Env;
		}

		// grains
		float GrL = 0.f, GrR = 0.f;
		if (P.Grains.Kind != FDmCueGrains::None && T >= P.Grains.Delay)
		{
			const FDmCueGrains& Dg = P.Grains;
			const float Tg = T - Dg.Delay;
			float Rate = Dg.Rate;
			if (Tg < Dg.RampIn) Rate *= Tg / FMath::Max(Dg.RampIn, 1e-3f);
			else if (Tg > Dg.Peak) Rate *= FMath::Max(0.f, 1.f - (Tg - Dg.Peak) / FMath::Max(Dg.RampOut, 1e-3f));
			if (Rng.Uni() < Rate * InvSr) SpawnGrain();
			for (FGrain& Gn : Grains)
			{
				if (!Gn.bOn) continue;
				float V;
				if (Dg.Kind == FDmCueGrains::Tick) V = Gn.Lp.Tick(Gn.Hp.Tick(Rng.Bi())) * 2.5f;
				else if (Dg.Kind == FDmCueGrains::Pluck)
				{
					const int32 Nx = Gn.KsPos + 1 >= Gn.KsLen ? 0 : Gn.KsPos + 1;
					const float Y = Gn.KsRho * 0.5f * (Gn.Ks[Gn.KsPos] + Gn.Ks[Nx]);
					Gn.Ks[Gn.KsPos] = Y;
					Gn.KsPos = Nx;
					V = Y;
				}
				else { Gn.Ph += Gn.Inc; Gn.Inc *= Gn.IncMul; if (Gn.Ph >= 1.f) Gn.Ph -= 1.f; V = Sin01(Gn.Ph); }
				V *= Gn.Amp;
				Gn.Amp *= Gn.Mul;
				if (Gn.Amp < 1e-4f) Gn.bOn = false;
				GrL += V * (0.7f - 0.3f * Gn.Pan);
				GrR += V * (0.7f + 0.3f * Gn.Pan);
			}
		}

		const float Amp = G * P.Gain * Vol;
		float OL = (ToneL + Nz * Gl + GrL) * Amp;
		float OR = (ToneR + Nz * Gr + GrR) * Amp;
		OL = MuffleL.Tick(OL);
		OR = MuffleR.Tick(OR);
		L[K] += OL; R[K] += OR;
		SendL[K] += OL * P.Send; SendR[K] += OR * P.Send;
		if (P.FarEcho > 0.f) { EchoL[K] += OL * P.FarEcho; EchoR[K] += OR * P.FarEcho; }
		const float A = FMath::Max(std::fabs(OL), std::fabs(OR));
		Peak = A > Peak ? A : Peak;
		++Sample;
		T += InvSr;
	}
	LastLevel = Peak;
	return bActive;
}
