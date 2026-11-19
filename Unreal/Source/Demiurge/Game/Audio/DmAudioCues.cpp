// DEMIURGE — Game/Audio/DmAudioCues.cpp : the cue patches and the voice that plays them. Owner: C9 audio.
#include "Game/Audio/DmAudioCues.h"

using namespace DmDsp;

namespace
{
	FDmCueOsc& Bell(FDmCuePatch& P, float Hz, float Amp, float Delay, float Decay, float Index = 1.4f, float Ratio = 1.4f)
	{
		FDmCueOsc& O = P.AddOsc();
		O.Wave = FDmCueOsc::Bell; O.Hz = Hz; O.Amp = Amp; O.Delay = Delay; O.Attack = 0.002f; O.Decay = Decay;
		O.FmRatio = Ratio; O.FmIndex = Index; O.FmDecay = Decay * 0.3f;
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
}

void BuildCuePatch(EDmCue Cue, const FDmCueParams& Prm, const FDmScale& S, FRng& R, FDmCuePatch& P)
{
	P = FDmCuePatch();
	P.Bus = DmAudio::CueBus(Cue);
	const float Power = Clamp(Prm.Param > 0.f ? Prm.Param : 1.f, 0.05f, 1.5f);
	switch (Cue)
	{
	case EDmCue::Hover:
	{
		Tone(P, FDmCueOsc::Sine, S.Hz(4, 5), 1.f, 0.f, 0.002f, 0.07f);
		Tone(P, FDmCueOsc::Sine, S.Hz(4, 6), 0.15f, 0.f, 0.002f, 0.035f);
		P.Dur = 0.12f; P.Gain = 0.045f; P.Send = 0.1f;
		break;
	}
	case EDmCue::Click:
	{
		FDmCueOsc& A = Tone(P, FDmCueOsc::Sine, 620.f, 1.f, 0.f, 0.001f, 0.08f); A.GlideStart = 1.25f; A.GlideTime = 0.012f;
		Tone(P, FDmCueOsc::Sine, 1390.f, 0.28f, 0.f, 0.001f, 0.035f);
		Noise(P, FDmCueNoise::White, FDmCueNoise::Bp, 0.25f, 2200.f, 1800.f, 0.02f, 1.2f, 0.0005f, 0.f, 0.012f);
		P.Dur = 0.16f; P.Gain = 0.11f; P.Send = 0.12f;
		break;
	}
	case EDmCue::Arm:
	{
		Bell(P, S.Hz(4, 5), 1.f, 0.f, 0.9f, 1.1f);
		Bell(P, S.Hz(0, 6), 0.9f, 0.085f, 1.1f, 1.1f);
		P.Dur = 1.3f; P.Gain = 0.085f; P.Send = 0.35f;
		break;
	}
	case EDmCue::Deny:
	{
		Tone(P, FDmCueOsc::Tri, S.Hz(1, 3), 1.f, 0.f, 0.006f, 0.28f);
		Tone(P, FDmCueOsc::Tri, S.Hz(0, 3), 1.f, 0.13f, 0.006f, 0.4f);
		P.ToneFilter = FDmCuePatch::ToneLp; P.ToneHz0 = 900.f; P.ToneHz1 = 600.f; P.ToneSweep = 0.5f;
		P.Dur = 0.65f; P.Gain = 0.16f; P.Send = 0.15f;
		break;
	}
	case EDmCue::Unlock:
	{
		Bell(P, S.Hz(0, 5), 1.f, 0.f, 1.4f, 1.2f);
		Bell(P, S.Hz(2, 5), 0.9f, 0.12f, 1.5f, 1.2f);
		Bell(P, S.Hz(4, 5), 0.9f, 0.24f, 2.0f, 1.2f);
		Bell(P, S.Hz(0, 6), 0.5f, 0.36f, 2.2f, 0.8f);
		P.Grains.Kind = FDmCueGrains::Tone; P.Grains.Rate = 14.f; P.Grains.RampIn = 0.3f; P.Grains.Peak = 0.5f; P.Grains.RampOut = 0.8f;
		P.Grains.bScale = true; P.Grains.OctLo = 6; P.Grains.OctHi = 6; P.Grains.Decay = 0.25f; P.Grains.Amp = 0.18f;
		P.Dur = 2.4f; P.Gain = 0.085f; P.Send = 0.45f;
		break;
	}
	case EDmCue::Thunder:
	{
		const float Far = Prm.Muffle;
		Noise(P, FDmCueNoise::White, FDmCueNoise::Lp, 0.9f * (1.f - 0.8f * Far), 5200.f * (1.f - 0.6f * Far), 650.f, 0.3f, 0.7f, 0.002f, 0.03f, 0.55f);
		FDmCueNoise& Roll = Noise(P, FDmCueNoise::Brown, FDmCueNoise::Lp, 1.25f, 320.f, 170.f, 4.f, 0.75f, 0.12f + 0.2f * Far, 0.4f, 5.5f, 0.02f);
		Roll.Roll = 0.85f; Roll.RollHz = 1.6f;
		P.Dur = 6.6f; P.Release = 0.6f; P.Gain = 0.36f; P.Send = 0.3f;
		break;
	}
	case EDmCue::Impact:
	{
		FDmCueOsc& Boom = Tone(P, FDmCueOsc::Sine, 58.f, 1.f, 0.f, 0.003f, 2.4f); Boom.GlideStart = 2.1f; Boom.GlideTime = 0.25f;
		FDmCueOsc& Body = Tone(P, FDmCueOsc::Sine, 92.f, 0.55f, 0.f, 0.004f, 1.6f); Body.GlideStart = 1.8f; Body.GlideTime = 0.18f;
		Noise(P, FDmCueNoise::Pink, FDmCueNoise::Lp, 1.1f, 4200.f, 320.f, 1.1f, 0.7f, 0.002f, 0.06f, 1.7f);
		FDmCueNoise& Tail = Noise(P, FDmCueNoise::Brown, FDmCueNoise::Lp, 1.2f, 320.f, 150.f, 5.f, 0.75f, 0.2f, 0.5f, 6.f);
		Tail.Roll = 0.55f; Tail.RollHz = 2.2f;
		P.Dur = 7.5f; P.Release = 0.8f; P.Gain = 0.3f * Power; P.Send = 0.3f;
		break;
	}
	case EDmCue::Rumble:
	{
		const bool bShort = Prm.Param > 0.f && Prm.Param < 0.35f;   // sculpting
		FDmCueNoise& N = Noise(P, FDmCueNoise::Brown, FDmCueNoise::Bp, 1.6f, 170.f, 120.f, 2.f, 0.6f, bShort ? 0.08f : 0.35f, bShort ? 0.25f : 1.2f, bShort ? 0.7f : 2.6f);
		N.Roll = 0.8f; N.RollHz = bShort ? 9.f : 6.5f;
		FDmCueOsc& Lo = Tone(P, FDmCueOsc::Tri, 82.f, 0.2f, 0.f, 0.3f, bShort ? 0.8f : 3.f); Lo.VibHz = 7.f; Lo.VibDepth = 0.01f;
		Noise(P, FDmCueNoise::Pink, FDmCueNoise::Bp, bShort ? 0.18f : 0.12f, 700.f, 380.f, 1.5f, 0.9f, 0.05f, bShort ? 0.15f : 0.8f, 1.2f);
		P.Dur = bShort ? 1.3f : 4.2f; P.Release = bShort ? 0.3f : 0.7f; P.Gain = (bShort ? 0.16f : 0.42f) * (bShort ? 1.f : Power); P.Send = 0.2f;
		break;
	}
	case EDmCue::Wave:
	{
		Noise(P, FDmCueNoise::Pink, FDmCueNoise::Bp, 0.9f, 240.f, 1300.f, 1.6f, 0.7f, 1.3f, 0.35f, 3.5f);
		FDmCueNoise& B = Noise(P, FDmCueNoise::Brown, FDmCueNoise::Lp, 1.f, 900.f, 260.f, 3.5f, 0.7f, 1.5f, 0.3f, 3.2f);
		B.Roll = 0.3f; B.RollHz = 1.5f;
		P.Dur = 5.8f; P.Release = 0.8f; P.Gain = 0.3f; P.Send = 0.25f;
		break;
	}
	case EDmCue::Rainfall:
	{
		Noise(P, FDmCueNoise::Pink, FDmCueNoise::Bp, 0.55f, 1500.f, 1500.f, 1.f, 0.45f, 0.35f, 0.7f, 1.1f);
		Noise(P, FDmCueNoise::Brown, FDmCueNoise::Lp, 0.45f, 650.f, 650.f, 1.f, 0.7f, 0.35f, 0.7f, 1.1f);
		P.Grains.Kind = FDmCueGrains::Tone; P.Grains.Rate = 55.f; P.Grains.RampIn = 0.3f; P.Grains.Peak = 1.0f; P.Grains.RampOut = 0.6f;
		P.Grains.HzLo = 1300.f; P.Grains.HzHi = 3200.f; P.Grains.Decay = 0.012f; P.Grains.Amp = 0.22f;
		P.Dur = 2.0f; P.Attack = 0.3f; P.Release = 0.6f; P.Gain = 0.3f; P.Send = 0.2f;
		break;
	}
	case EDmCue::Shimmer:
	{
		FDmCueOsc& A = Tone(P, FDmCueOsc::Sine, S.Hz(0, 5), 0.35f, 0.f, 0.8f, 2.6f); A.VibHz = 4.f; A.VibDepth = 0.002f;
		FDmCueOsc& B = Tone(P, FDmCueOsc::Sine, S.Hz(4, 5), 0.28f, 0.15f, 0.8f, 2.6f); B.VibHz = 4.5f; B.VibDepth = 0.002f;
		P.Grains.Kind = FDmCueGrains::Tone; P.Grains.Rate = 22.f; P.Grains.RampIn = 0.8f; P.Grains.Peak = 1.2f; P.Grains.RampOut = 1.2f;
		P.Grains.bScale = true; P.Grains.OctLo = 6; P.Grains.OctHi = 7; P.Grains.Decay = 0.4f; P.Grains.Amp = 0.32f;
		P.Dur = 3.2f; P.Attack = 0.05f; P.Release = 0.6f; P.Gain = 0.15f; P.Send = 0.5f;
		break;
	}
	case EDmCue::Chime:
	{
		Bell(P, S.Hz(0, 5), 1.f, 0.f, 3.4f, 1.3f);
		Bell(P, S.Hz(2, 5), 0.8f, 0.05f, 3.2f, 1.3f);
		Bell(P, S.Hz(4, 5), 0.8f, 0.10f, 3.0f, 1.3f);
		Tone(P, FDmCueOsc::Sine, S.Hz(0, 4), 0.35f, 0.f, 0.01f, 3.5f);
		P.Dur = 4.2f; P.Release = 0.5f; P.Gain = 0.12f; P.Send = 0.45f;
		break;
	}
	case EDmCue::Choir:
	{
		const int32 Deg[3] = { 0, 4, 7 };
		for (int32 I = 0; I < 3; ++I)
		{
			const float Hz = S.Hz(Deg[I], 3);
			for (int32 D = 0; D < 2; ++D)
			{
				FDmCueOsc& O = Tone(P, FDmCueOsc::Saw, Hz * (D ? 1.0046f : 0.9954f), 0.5f, 0.f, 0.02f, 30.f);
				O.VibHz = 4.6f + 0.4f * float(I) + 0.2f * float(D); O.VibDepth = 0.004f; O.Pan = (D ? 0.25f : -0.25f);
			}
		}
		P.ToneFilter = FDmCuePatch::ToneFormant;
		P.Dur = 4.4f; P.Attack = 1.1f; P.Release = 1.7f; P.Gain = 0.16f; P.Send = 0.55f;
		break;
	}
	case EDmCue::Horn:
	{
		const float Root = S.Hz(0, 2) < 80.f ? S.Hz(0, 3) : S.Hz(0, 2);
		FDmCueOsc& A = Tone(P, FDmCueOsc::Saw, Root, 0.6f, 0.f, 0.02f, 30.f); A.GlideStart = 0.93f; A.GlideTime = 0.12f; A.VibHz = 4.6f; A.VibDepth = 0.004f;
		FDmCueOsc& B = Tone(P, FDmCueOsc::Saw, Root * 1.4983f, 0.45f, 0.05f, 0.02f, 30.f); B.GlideStart = 0.94f; B.GlideTime = 0.12f; B.VibHz = 4.9f; B.VibDepth = 0.004f;
		FDmCueOsc& C = Tone(P, FDmCueOsc::Saw, Root * 1.0035f, 0.4f, 0.f, 0.02f, 30.f); C.GlideStart = 0.93f; C.GlideTime = 0.12f;
		P.ToneFilter = FDmCuePatch::ToneLp; P.ToneHz0 = 280.f; P.ToneHz1 = 1250.f; P.ToneSweep = 0.45f; P.ToneQ = 0.85f;
		P.Dur = 2.7f; P.Attack = 0.22f; P.Release = 0.9f; P.Gain = 0.16f; P.Send = 0.4f;
		break;
	}
	case EDmCue::Toll:
	{
		const float F = S.Hz(0, 3);
		static const float Ratio[8] = { 0.5f, 1.f, 1.19f, 1.5f, 2.f, 2.5f, 3.f, 4.2f };
		static const float Amp[8] = { 0.55f, 1.f, 0.5f, 0.3f, 0.42f, 0.18f, 0.12f, 0.06f };
		static const float Dec[8] = { 6.5f, 4.5f, 3.2f, 2.6f, 2.1f, 1.5f, 1.1f, 0.7f };
		for (int32 I = 0; I < 8; ++I) Tone(P, FDmCueOsc::Sine, F * Ratio[I], Amp[I], 0.f, 0.002f, Dec[I]);
		Noise(P, FDmCueNoise::Pink, FDmCueNoise::Lp, 0.25f, 2200.f, 900.f, 0.05f, 0.7f, 0.001f, 0.f, 0.06f);
		P.Dur = 6.5f; P.Release = 0.8f; P.Gain = 0.16f; P.Send = 0.5f;
		break;
	}
	case EDmCue::Roar:
	{
		FDmCueNoise& A = Noise(P, FDmCueNoise::Brown, FDmCueNoise::Lp, 1.1f, 160.f, 950.f, 3.f, 0.7f, 1.6f, 2.6f, 3.f);
		A.Roll = 0.35f; A.RollHz = 9.f;
		Noise(P, FDmCueNoise::Pink, FDmCueNoise::Bp, 0.4f, 550.f, 1500.f, 4.f, 0.6f, 1.8f, 2.4f, 3.f);
		P.Grains.Kind = FDmCueGrains::Tick; P.Grains.Rate = 35.f; P.Grains.RampIn = 1.5f; P.Grains.Peak = 4.f; P.Grains.RampOut = 2.f;
		P.Grains.HzLo = 700.f; P.Grains.HzHi = 2500.f; P.Grains.Decay = 0.005f; P.Grains.Amp = 0.25f;
		FDmCueOsc& Lo = Tone(P, FDmCueOsc::Tri, 72.f, 0.25f, 0.f, 1.5f, 6.f); Lo.VibHz = 11.f; Lo.VibDepth = 0.02f;
		P.Dur = 7.5f; P.Attack = 0.4f; P.Release = 1.5f; P.Gain = 0.28f; P.Send = 0.25f;
		break;
	}
	case EDmCue::Crackle:
	{
		FDmCueNoise& N = Noise(P, FDmCueNoise::Brown, FDmCueNoise::Bp, 0.5f, 320.f, 260.f, 1.f, 0.6f, 0.1f, 0.5f, 1.6f);
		N.Roll = 0.5f; N.RollHz = 5.f;
		P.Grains.Kind = FDmCueGrains::Tick; P.Grains.Rate = 40.f; P.Grains.RampIn = 0.1f; P.Grains.Peak = 0.5f; P.Grains.RampOut = 1.3f;
		P.Grains.HzLo = 900.f; P.Grains.HzHi = 3200.f; P.Grains.Decay = 0.004f; P.Grains.Amp = 0.55f;
		P.Dur = 2.1f; P.Release = 0.4f; P.Gain = 0.28f; P.Send = 0.2f;
		break;
	}
	case EDmCue::Fall:
	{
		const float Eta = Clamp(Prm.Param > 0.f ? Prm.Param : 2.5f, 0.6f, 8.f);
		Noise(P, FDmCueNoise::Pink, FDmCueNoise::Bp, 1.f, 2400.f, 320.f, Eta, 1.3f, Eta * 0.7f, Eta * 0.3f, 0.35f);
		Noise(P, FDmCueNoise::Brown, FDmCueNoise::Lp, 0.6f, 500.f, 160.f, Eta, 0.7f, Eta * 0.8f, Eta * 0.2f, 0.4f);
		FDmCueOsc& W = Tone(P, FDmCueOsc::Sine, 180.f, 0.18f, 0.f, Eta * 0.8f, 1.f); W.GlideStart = 2.2f; W.GlideTime = Eta * 0.4f;
		P.Dur = Eta + 0.4f; P.Attack = 0.05f; P.Release = 0.35f; P.Gain = 0.36f; P.Send = 0.25f;
		break;
	}
	default: break;
	}
	(void)R;
}

// ------------------------------------------------------------------------------------------------ voice

void FDmCueVoice::Start(EDmCue InCue, const FDmCuePatch& InPatch, const FDmCueParams& P, const FDmScale& InScale, float InSr, uint32 Seed, int64 Order)
{
	Cue = InCue;
	Patch = InPatch;
	Scale = InScale;
	Sr = InSr;
	InvSr = 1.f / Sr;
	T = 0.f;
	Sample = 0;
	Delay = int64(FMath::Max(0.f, P.Delay) * Sr);
	Vol = FMath::Max(0.f, P.Vol);
	Pan = Clamp(P.Pan, -1.f, 1.f);
	Rng = FRng(Seed);
	StartOrder = Order;
	LastLevel = 1.f;
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
		OscFm[I] = O.FmIndex;
		OscFmMul[I] = DecayMul(FMath::Max(0.01f, O.FmDecay), Sr);
		OscVib[I] = Rng.Uni();
		PanGains(Clamp(Pan + O.Pan, -1.f, 1.f), OscGl[I], OscGr[I]);
	}
	ToneSvf.Reset();
	for (int32 I = 0; I < 3; ++I) { FormSvf[I].Reset(); FormSvf[I].Set(Patch.Formant[I], 5.f + 2.f * float(I), Sr); }
	for (int32 I = 0; I < Patch.NumNoise; ++I)
	{
		NzEnv[I] = 1.f;
		NzMul[I] = DecayMul(Patch.Noise[I].Decay, Sr);
		NzSvf[I].Reset();
		NzPink[I] = FPink();
		NzBrown[I] = FBrown();
		NzRoll[I].Init(FMath::Max(0.1f, Patch.Noise[I].RollHz), Sr);
	}
	for (FGrain& G : Grains) G.bOn = false;
	const float MuffleHz = 16000.f * std::pow(0.06f, Clamp(P.Muffle, 0.f, 1.f));
	MuffleL.Reset(); MuffleR.Reset();
	MuffleL.Set(MuffleHz, Sr); MuffleR.Set(MuffleHz, Sr);
	PanGains(Pan, Gl, Gr);
	Control();
}

void FDmCueVoice::Control()
{
	auto SweepHz = [](float Hz0, float Hz1, float Sweep, float Local)
	{
		const float U = Sweep > 0.f ? Sat01(Local / Sweep) : 1.f;
		return Hz0 * std::pow(FMath::Max(Hz1, 1.f) / FMath::Max(Hz0, 1.f), U);
	};
	if (Patch.ToneFilter == FDmCuePatch::ToneLp) ToneSvf.Set(SweepHz(Patch.ToneHz0, Patch.ToneHz1, Patch.ToneSweep, T), Patch.ToneQ, Sr);
	for (int32 I = 0; I < Patch.NumNoise; ++I)
	{
		const FDmCueNoise& N = Patch.Noise[I];
		NzSvf[I].Set(SweepHz(N.Hz0, N.Hz1, N.Sweep, FMath::Max(0.f, T - N.Delay)), N.Q, Sr);
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
		G.Lp.Reset(); G.Lp.Set(Hz * 1.6f, Sr);
		G.Hp.Set(Hz * 0.6f, Sr);
		return;
	}
}

bool FDmCueVoice::Render(float* L, float* R, float* SendL, float* SendR, int32 N)
{
	if (!bActive) return false;
	float Peak = 0.f;
	const FDmCuePatch& P = Patch;
	const int64 AttackS = FMath::Max<int64>(1, int64(P.Attack * Sr));
	const int64 DurS = int64(P.Dur * Sr);
	const int64 RelS = FMath::Max<int64>(1, int64(P.Release * Sr));
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

		// oscillators
		float ToneL = 0.f, ToneR = 0.f, ToneM = 0.f;
		for (int32 I = 0; I < P.NumOsc; ++I)
		{
			const FDmCueOsc& O = P.Osc[I];
			if (T < O.Delay) continue;
			const float Local = T - O.Delay;
			float Env;
			if (Local < O.Attack) Env = Local / O.Attack;
			else { Env = OscEnv[I]; OscEnv[I] *= OscMul[I]; }
			if (Env < 1e-5f && Local > O.Attack) continue;
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
			case FDmCueOsc::Bell:
			{
				OscMod[I] += Inc * O.FmRatio;
				if (OscMod[I] >= 1.f) OscMod[I] -= 1.f;
				const float Idx = OscFm[I];
				OscFm[I] *= OscFmMul[I];
				V = Sin01(OscPh[I] + Idx * 0.159155f * Sin01(OscMod[I]));
				break;
			}
			default: V = Sin01(OscPh[I]); break;
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
			const FDmCueNoise& D = P.Noise[I];
			if (T < D.Delay) continue;
			const float Local = T - D.Delay;
			float Env;
			if (Local < D.Attack) Env = Local / D.Attack;
			else if (Local < D.Attack + D.Hold) Env = 1.f;
			else { Env = NzEnv[I]; NzEnv[I] *= NzMul[I]; }
			const float W = Rng.Bi();
			const float Src = D.Color == FDmCueNoise::White ? W : (D.Color == FDmCueNoise::Pink ? NzPink[I].Tick(W) * 3.f : NzBrown[I].Tick(W));
			float F;
			if (D.Mode == FDmCueNoise::Bp) F = NzSvf[I].Band(Src);
			else if (D.Mode == FDmCueNoise::Hp) F = NzSvf[I].High(Src);
			else F = NzSvf[I].Low(Src);
			if (D.Roll > 0.f) Env *= 1.f - D.Roll + D.Roll * (0.5f + 0.5f * NzRoll[I].Tick(Rng)) * 1.4f;
			Nz += F * D.Amp * Env;
		}

		// grains
		float GrL = 0.f, GrR = 0.f;
		if (P.Grains.Kind != FDmCueGrains::None)
		{
			const FDmCueGrains& D = P.Grains;
			float Rate = D.Rate;
			if (T < D.RampIn) Rate *= T / FMath::Max(D.RampIn, 1e-3f);
			else if (T > D.Peak) Rate *= FMath::Max(0.f, 1.f - (T - D.Peak) / FMath::Max(D.RampOut, 1e-3f));
			if (Rng.Uni() < Rate * InvSr) SpawnGrain();
			for (FGrain& Gn : Grains)
			{
				if (!Gn.bOn) continue;
				float V;
				if (D.Kind == FDmCueGrains::Tick) V = Gn.Lp.Tick(Gn.Hp.Tick(Rng.Bi())) * 2.5f;
				else { Gn.Ph += Gn.Inc; if (Gn.Ph >= 1.f) Gn.Ph -= 1.f; V = Sin01(Gn.Ph); }
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
		const float A = FMath::Max(std::fabs(OL), std::fabs(OR));
		Peak = A > Peak ? A : Peak;
		++Sample;
		T += InvSr;
	}
	LastLevel = Peak;
	return bActive;
}
