// DEMIURGE — Game/Audio/DmAudioDsp.h : small, allocation-free DSP building blocks for the synthesised
// audio (SPEC-UNREAL 2, C9). Owner: C9 audio.
//
// Everything here is plain C++ on floats (no engine audio types) so the same code runs on the audio
// render thread (through UDmSynthWave) and offline in the analysis scenarios. No object allocates
// after Init(); nothing here is thread-safe by itself (one engine = one thread at a time).
#pragma once

#include "CoreMinimal.h"
#include <cmath>
#include <cstring>

namespace DmDsp
{
	inline constexpr float kPiF = 3.14159265358979f;
	inline constexpr float kTauF = 6.28318530717959f;

	inline float DbToGain(float Db) { return std::pow(10.f, Db * 0.05f); }
	inline float GainToDb(float G) { return 20.f * std::log10(G > 1e-12f ? G : 1e-12f); }
	inline float MidiToHz(float Note) { return 440.f * std::exp2((Note - 69.f) / 12.f); }
	inline float Clamp(float X, float A, float B) { return X < A ? A : (X > B ? B : X); }
	inline float Sat01(float X) { return Clamp(X, 0.f, 1.f); }
	inline float Lerp(float A, float B, float T) { return A + (B - A) * T; }
	inline float SStep(float E0, float E1, float X)
	{
		const float T = Sat01((X - E0) / (E1 - E0));
		return T * T * (3.f - 2.f * T);
	}
	/** Per-sample multiplier that decays by 60 dB in Seconds. */
	inline float DecayMul(float Seconds, float Sr) { return Seconds > 0.f ? std::exp(-6.9077553f / (Seconds * Sr)) : 0.f; }
	/** One-pole smoothing coefficient for a time constant in seconds at a given update rate. */
	inline float SmoothCoef(float Seconds, float Rate) { return Seconds > 0.f ? 1.f - std::exp(-1.f / (Seconds * Rate)) : 1.f; }
	/** Gentle saturation (odd, symmetric, unity slope at 0, bounded by 1). */
	inline float SoftSat(float X)
	{
		const float A = Clamp(X, -3.f, 3.f);
		return A * (27.f + A * A) / (27.f + 9.f * A * A);
	}

	/** xorshift32: the audio's own random numbers (never the sim's rng). */
	struct FRng
	{
		uint32 S = 0x9E3779B9u;
		FRng() = default;
		explicit FRng(uint32 Seed) : S(Seed ? Seed : 0x9E3779B9u) {}
		uint32 Next() { S ^= S << 13; S ^= S >> 17; S ^= S << 5; return S; }
		float Uni() { return float(Next() >> 8) * (1.f / 16777216.f); }
		float Bi() { return Uni() * 2.f - 1.f; }
		float Range(float A, float B) { return A + (B - A) * Uni(); }
		int32 Int(int32 N) { return N > 0 ? int32(Next() % uint32(N)) : 0; }
		bool Chance(float P) { return Uni() < P; }
	};

	/** 4096-point sine table with linear interpolation; phase in cycles. */
	struct FSineTable
	{
		static constexpr int32 N = 4096;
		float T[N + 1];
		FSineTable() { for (int32 I = 0; I <= N; ++I) T[I] = std::sin(kTauF * float(I) / float(N)); }
	};
	inline const FSineTable& SineTab() { static const FSineTable Tab; return Tab; }
	inline float Sin01(float Phase)
	{
		const FSineTable& Tab = SineTab();
		const float P = Phase - std::floor(Phase);
		const float X = P * float(FSineTable::N);
		int32 I = int32(X);
		if (I >= FSineTable::N) I = FSineTable::N - 1;
		const float F = X - float(I);
		return Tab.T[I] + (Tab.T[I + 1] - Tab.T[I]) * F;
	}
	inline float Wrap01(float P) { return P - std::floor(P); }

	/** PolyBLEP correction for a discontinuity at phase 0 (T = phase, Dt = increment). */
	inline float PolyBlep(float T, float Dt)
	{
		if (T < Dt) { const float X = T / Dt; return X + X - X * X - 1.f; }
		if (T > 1.f - Dt) { const float X = (T - 1.f) / Dt; return X * X + X + X + 1.f; }
		return 0.f;
	}
	/** Band-limited sawtooth from a phase in [0,1). */
	inline float BlepSaw(float Phase, float Dt) { return (2.f * Phase - 1.f) - PolyBlep(Phase, Dt); }
	/** Band-limited pulse (Width in (0,1)). */
	inline float BlepPulse(float Phase, float Dt, float Width)
	{
		float V = Phase < Width ? 1.f : -1.f;
		V += PolyBlep(Phase, Dt);
		V -= PolyBlep(Wrap01(Phase - Width + 1.f), Dt);
		return V;
	}
	/** Triangle (naive is fine: its harmonics fall at 12 dB / octave). */
	inline float Tri(float Phase) { return 1.f - 4.f * std::fabs(Phase - 0.5f); }

	struct FLp1
	{
		float Z = 0.f, A = 1.f;
		void Set(float Hz, float Sr) { A = 1.f - std::exp(-kTauF * Clamp(Hz, 1.f, Sr * 0.49f) / Sr); }
		float Tick(float X) { Z += A * (X - Z); return Z; }
		void Reset(float V = 0.f) { Z = V; }
	};
	struct FHp1
	{
		FLp1 L;
		void Set(float Hz, float Sr) { L.Set(Hz, Sr); }
		float Tick(float X) { return X - L.Tick(X); }
	};

	/** Topology-preserving state variable filter (Cytomic / Simper). Set() costs a tan: call it at control rate. */
	struct FSvf
	{
		float Ic1 = 0.f, Ic2 = 0.f, A1 = 0.f, A2 = 0.f, A3 = 0.f, K = 1.f;
		void Set(float Hz, float Q, float Sr)
		{
			const float F = Clamp(Hz, 8.f, Sr * 0.45f);
			const float G = std::tan(kPiF * F / Sr);
			K = 1.f / (Q > 0.05f ? Q : 0.05f);
			A1 = 1.f / (1.f + G * (G + K));
			A2 = G * A1;
			A3 = G * A2;
		}
		void Tick(float V0, float& Lp, float& Bp, float& Hp)
		{
			const float V3 = V0 - Ic2;
			const float V1 = A1 * Ic1 + A2 * V3;
			const float V2 = Ic2 + A2 * Ic1 + A3 * V3;
			Ic1 = 2.f * V1 - Ic1;
			Ic2 = 2.f * V2 - Ic2;
			Lp = V2; Bp = V1; Hp = V0 - K * V1 - V2;
		}
		float Low(float X) { float L, B, H; Tick(X, L, B, H); return L; }
		/** Band-pass normalised to unity gain at the centre. */
		float Band(float X) { float L, B, H; Tick(X, L, B, H); return B * K; }
		float High(float X) { float L, B, H; Tick(X, L, B, H); return H; }
		void Reset() { Ic1 = Ic2 = 0.f; }
	};

	/** Pink noise (Paul Kellet's economy filter), roughly unit RMS 0.25..0.3. */
	struct FPink
	{
		float B0 = 0.f, B1 = 0.f, B2 = 0.f;
		float Tick(float White)
		{
			B0 = 0.99765f * B0 + White * 0.0990460f;
			B1 = 0.96300f * B1 + White * 0.2965164f;
			B2 = 0.57000f * B2 + White * 1.0526913f;
			return (B0 + B1 + B2 + White * 0.1848f) * 0.22f;
		}
	};
	/** Brown(ian) noise: a leaky integrator of white noise. */
	struct FBrown
	{
		float Z = 0.f;
		float Tick(float White) { Z = (Z + 0.02f * White) * (1.f / 1.02f); return Z * 3.2f; }
	};

	/** A slowly wandering random value in [-1, 1] (smoothed sample-and-hold), updated per call. */
	struct FDrift
	{
		float Cur = 0.f, Target = 0.f, Coef = 0.001f;
		int32 Count = 0, Period = 1000;
		void Init(float Hz, float Rate) { Period = int32(FMath::Max(1.f, Rate / FMath::Max(Hz, 0.001f))); Coef = SmoothCoef(1.f / FMath::Max(Hz, 0.001f) * 0.5f, Rate); }
		float Tick(FRng& R)
		{
			if (--Count <= 0) { Count = Period; Target = R.Bi(); }
			Cur += Coef * (Target - Cur);
			return Cur;
		}
	};

	/** Fixed power-of-two delay line (allocated once by Init). */
	struct FDelay
	{
		TArray<float> Buf;
		int32 Mask = 0, W = 0;
		void Init(int32 MaxSamples)
		{
			int32 N = 1;
			while (N < MaxSamples + 2) N <<= 1;
			Buf.SetNumZeroed(N);
			Mask = N - 1;
			W = 0;
		}
		void Clear() { if (Buf.Num()) FMemory::Memzero(Buf.GetData(), Buf.Num() * sizeof(float)); }
		void Push(float X) { Buf[W] = X; W = (W + 1) & Mask; }
		/** Sample written D pushes ago (D >= 1). */
		float Read(int32 D) const { return Buf[(W - D) & Mask]; }
		float ReadFrac(float D) const
		{
			const int32 I = int32(D);
			const float F = D - float(I);
			const float A = Buf[(W - I) & Mask], B = Buf[(W - I - 1) & Mask];
			return A + (B - A) * F;
		}
	};

	/** 8-line feedback delay network reverb, stereo in / out, warm damping. */
	struct FReverb
	{
		static constexpr int32 NumLines = 8;
		FDelay Lines[NumLines];
		int32 Len[NumLines] = {};
		float Fb[NumLines] = {};
		FLp1 Damp[NumLines];
		FDelay PreL, PreR;
		int32 PreDelay = 1;
		FHp1 InHp;
		FLp1 InLp;
		float Sr = 48000.f;
		FRng Rng{ 0x5EEDu };
		float ModPhase = 0.f;

		void Init(float InSr, float Rt60, float DampHz, float PreDelaySec)
		{
			Sr = InSr;
			static const float BaseMs[NumLines] = { 31.7f, 37.1f, 41.3f, 47.9f, 53.3f, 59.9f, 67.1f, 73.7f };
			for (int32 I = 0; I < NumLines; ++I)
			{
				Len[I] = int32(BaseMs[I] * 0.001f * Sr * 1.35f);
				Lines[I].Init(Len[I] + 64);
				Damp[I].Set(DampHz, Sr);
			}
			SetTime(Rt60);
			PreDelay = FMath::Max(1, int32(PreDelaySec * Sr));
			PreL.Init(PreDelay + 4);
			PreR.Init(PreDelay + 4);
			InHp.Set(140.f, Sr);
			InLp.Set(7000.f, Sr);
		}
		void SetTime(float Rt60)
		{
			for (int32 I = 0; I < NumLines; ++I) Fb[I] = std::pow(10.f, -3.f * float(Len[I]) / (FMath::Max(Rt60, 0.1f) * Sr));
		}
		void Clear() { for (FDelay& D : Lines) D.Clear(); PreL.Clear(); PreR.Clear(); }
		void Tick(float InL, float InR, float& OutL, float& OutR)
		{
			PreL.Push(InL); PreR.Push(InR);
			const float Mono = InLp.Tick(InHp.Tick(0.5f * (PreL.Read(PreDelay) + PreR.Read(PreDelay))));
			float X[NumLines];
			float Sum = 0.f;
			ModPhase += 0.37f / Sr;
			if (ModPhase >= 1.f) ModPhase -= 1.f;
			const float Mod = Sin01(ModPhase) * 6.f;
			for (int32 I = 0; I < NumLines; ++I)
			{
				const float D = float(Len[I]) + ((I & 3) == 1 ? Mod : ((I & 3) == 2 ? -Mod : 0.f));
				X[I] = Damp[I].Tick(Lines[I].ReadFrac(D)) * Fb[I];
				Sum += X[I];
			}
			const float H = Sum * (2.f / float(NumLines));
			static const float InSign[NumLines] = { 1, -1, 1, -1, 1, 1, -1, -1 };
			for (int32 I = 0; I < NumLines; ++I) Lines[I].Push(X[I] - H + Mono * InSign[I] * 0.35f);
			OutL = (X[0] - X[2] + X[4] + X[6] - X[1] * 0.5f) * 0.42f;
			OutR = (X[1] - X[3] + X[5] + X[7] - X[2] * 0.5f) * 0.42f;
		}
	};

	/** Look-ahead peak limiter: the output never exceeds Ceiling (a final clamp catches the rest and is counted). */
	struct FLimiter
	{
		static constexpr int32 MaxLook = 256;
		float BufL[MaxLook] = {}, BufR[MaxLook] = {};
		float Win[MaxLook] = {};
		int32 Look = 96, Pos = 0;
		float Ceiling = 0.89f;
		float Gain = 1.f, AttackC = 0.1f, ReleaseC = 0.0001f;
		float MinGain = 1.f;            // lowest gain since the last ResetStats
		int64 SafetyClips = 0;          // samples the final clamp had to touch
		void Init(float Sr, float CeilingDb = -1.f, float LookSec = 0.002f, float ReleaseSec = 0.18f)
		{
			Look = FMath::Clamp(int32(LookSec * Sr), 8, MaxLook);
			Ceiling = DbToGain(CeilingDb);
			AttackC = SmoothCoef(float(Look) / Sr / 6.f, Sr);
			ReleaseC = SmoothCoef(ReleaseSec, Sr);
			Gain = 1.f;
			Pos = 0;
			for (int32 I = 0; I < MaxLook; ++I) { BufL[I] = BufR[I] = 0.f; Win[I] = 1.f; }
		}
		void ResetStats() { MinGain = 1.f; SafetyClips = 0; }
		void Tick(float& L, float& R)
		{
			const float Pk = FMath::Max(std::fabs(L), std::fabs(R));
			const float Aim = Ceiling * 0.985f;
			const float Need = Pk > Aim ? Aim / Pk : 1.f;
			Win[Pos] = Need;
			BufL[Pos] = L; BufR[Pos] = R;
			// minimum required gain over the window (Look is small; a linear scan is cheap and branch-free enough)
			float Target = 1.f;
			for (int32 I = 0; I < Look; ++I) Target = Win[I] < Target ? Win[I] : Target;
			Gain += (Target < Gain ? AttackC : ReleaseC) * (Target - Gain);
			if (Gain < MinGain) MinGain = Gain;
			const int32 Out = (Pos + 1) % Look;
			float OL = BufL[Out] * Gain, OR = BufR[Out] * Gain;
			if (OL > Ceiling) { OL = Ceiling; ++SafetyClips; } else if (OL < -Ceiling) { OL = -Ceiling; ++SafetyClips; }
			if (OR > Ceiling) { OR = Ceiling; ++SafetyClips; } else if (OR < -Ceiling) { OR = -Ceiling; ++SafetyClips; }
			Pos = Out;
			L = OL; R = OR;
		}
	};

	/** DC blocker (about 4 Hz). */
	struct FDcBlock
	{
		float X1 = 0.f, Y1 = 0.f, R = 0.9995f;
		float Tick(float X) { const float Y = X - X1 + R * Y1; X1 = X; Y1 = Y; return Y; }
	};

	/** Constant-power pan, P in [-1, 1]. */
	inline void PanGains(float P, float& Gl, float& Gr)
	{
		const float A = (Clamp(P, -1.f, 1.f) + 1.f) * 0.125f;  // 0 .. 0.25 cycles (a quarter sine)
		Gl = Sin01(0.25f + A);    // cos
		Gr = Sin01(A);            // sin
	}
}
