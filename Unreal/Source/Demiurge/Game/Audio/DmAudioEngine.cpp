// DEMIURGE — Game/Audio/DmAudioEngine.cpp : mixer, ambience layers, command queue. Owner: C9 audio.
#include "Game/Audio/DmAudioEngine.h"
#include "Game/Audio/DmAudioDsp.h"
#include "Game/Audio/DmAudioCues.h"
#include "Game/Audio/DmAudioMusic.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"

using namespace DmDsp;

// ------------------------------------------------------------------------------------------------ names

namespace DmAudio
{
	static const TCHAR* GCueNames[int32(EDmCue::Count)] = {
		TEXT("hover"), TEXT("click"), TEXT("arm"), TEXT("deny"), TEXT("unlock"), TEXT("thunder"), TEXT("impact"), TEXT("rumble"),
		TEXT("wave"), TEXT("rainfall"), TEXT("shimmer"), TEXT("chime"), TEXT("choir"), TEXT("horn"), TEXT("toll"), TEXT("roar"),
		TEXT("crackle"), TEXT("fall"),
	};
	static const TCHAR* GStingerNames[int32(EDmStinger::Count)] = {
		TEXT("age"), TEXT("founding"), TEXT("miracle"), TEXT("wrath"), TEXT("ending-beloved"), TEXT("ending-feared"),
		TEXT("ending-forgotten"), TEXT("ending-parting"), TEXT("ending-silence"), TEXT("ending-ash"), TEXT("ending-quiet"),
	};
	static const TCHAR* GLayerNames[int32(EDmLayer::Count)] = {
		TEXT("space"), TEXT("wind"), TEXT("surf"), TEXT("sea"), TEXT("birds"), TEXT("crickets"), TEXT("rain"), TEXT("fire"),
		TEXT("lava"), TEXT("town-early"), TEXT("town-middle"), TEXT("town-industry"), TEXT("town-modern"), TEXT("leaves"),
	};

	const TCHAR* CueName(EDmCue C) { const int32 K = int32(C); return (K >= 0 && K < int32(EDmCue::Count)) ? GCueNames[K] : TEXT("?"); }
	bool CueFromName(const FString& Name, EDmCue& Out)
	{
		for (int32 K = 0; K < int32(EDmCue::Count); ++K)
			if (Name.Equals(GCueNames[K], ESearchCase::IgnoreCase)) { Out = EDmCue(K); return true; }
		return false;
	}
	const TCHAR* StingerName(EDmStinger S) { const int32 K = int32(S); return (K >= 0 && K < int32(EDmStinger::Count)) ? GStingerNames[K] : TEXT("?"); }
	bool StingerFromName(const FString& Name, EDmStinger& Out)
	{
		for (int32 K = 0; K < int32(EDmStinger::Count); ++K)
			if (Name.Equals(GStingerNames[K], ESearchCase::IgnoreCase)) { Out = EDmStinger(K); return true; }
		return false;
	}
	const TCHAR* LayerName(EDmLayer L) { const int32 K = int32(L); return (K >= 0 && K < int32(EDmLayer::Count)) ? GLayerNames[K] : TEXT("?"); }
	EDmBus CueBus(EDmCue C)
	{
		switch (C)
		{
		case EDmCue::Hover: case EDmCue::Click: case EDmCue::Arm: case EDmCue::Deny: case EDmCue::Unlock: return EDmBus::Ui;
		default: return EDmBus::Sfx;
		}
	}
}

// ------------------------------------------------------------------------------------------------ ambience

namespace
{
	constexpr int32 MaxBlock = 256;
	constexpr int32 NumLayers = int32(EDmLayer::Count);

	struct FPing { bool bOn = false; float Ph[3] = {}; float Inc[3] = {}; float Amp[3] = {}; float Mul[3] = {}; float Pan = 0.f; };
	struct FChirp
	{
		bool bOn = false;
		int32 Species = 0, NotesLeft = 0;
		float T = 0.f, NoteDur = 0.1f, Gap = 0.05f, F0 = 2000.f, F1 = 2500.f, Amp = 0.3f, Pan = 0.f, Ph = 0.f;
		bool bGap = false;
	};
	struct FCricket { float Hz = 4000.f, Ph = 0.f, Timer = 0.f, PulseT = 0.f, Pan = 0.f, Amp = 0.5f; int32 Pulses = 0; };
	struct FGrainA { bool bOn = false; float Ph = 0.f, Inc = 0.f, Amp = 0.f, Mul = 0.f, Pan = 0.f, Glide = 1.f; FLp1 Lp; FHp1 Hp; };
	struct FSurfWave
	{
		float T = 0.f, Period = 8.f, Pan = 0.f, Size = 1.f;
		FBrown Br; FPink Pk; FSvf Lp; FSvf Hiss;
	};

	class FAmbience
	{
	public:
		float Gain[NumLayers] = {};
		float Target[NumLayers] = {};

		void Init(float InSr, uint32 Seed)
		{
			Sr = InSr; InvSr = 1.f / Sr;
			R = FRng(Seed ^ 0x77AB1234u);
			for (int32 C = 0; C < 2; ++C)
			{
				SpBp[C].Set(420.f, 0.7f, Sr); SpAir[C].Set(1500.f, 1.1f, Sr);
				WBp[C].Set(500.f, 0.9f, Sr);
				SeaLp[C].Set(380.f, 0.6f, Sr);
				RnBp[C].Set(1700.f, 0.45f, Sr); RnLp[C].Set(520.f, 0.6f, Sr);
				FireLp[C].Set(320.f, 0.7f, Sr);
				LavaBp[C].Set(110.f, 0.7f, Sr);
				SurfHp[C].Set(55.f, Sr);
				LfBp[C].Set(C ? 2100.f : 1900.f, 0.7f, Sr); LfLo[C].Set(C ? 650.f : 600.f, 0.8f, Sr);
				MurA[C].Set(520.f, 3.f, Sr); MurB[C].Set(1450.f, 4.f, Sr);
				ChuffLp[C].Set(450.f, 0.8f, Sr);
				TrafLp[C].Set(650.f, 0.6f, Sr);
			}
			SpDrift.Init(0.05f, Sr / 32.f);
			Gust1.Init(0.15f, Sr / 32.f); Gust2.Init(0.04f, Sr / 32.f); GustPan.Init(0.08f, Sr / 32.f);
			SeaDrift.Init(0.12f, Sr / 32.f);
			FireDrift.Init(1.5f, Sr / 32.f);
			LavaDrift.Init(0.3f, Sr / 32.f);
			Syll.Init(4.f, Sr / 32.f); Crowd.Init(0.2f, Sr / 32.f);
			Traffic.Init(0.18f, Sr / 32.f);
			LfDrift.Init(0.35f, Sr / 32.f); LfDrift2.Init(0.07f, Sr / 32.f);
			for (int32 I = 0; I < 2; ++I)
			{
				Waves[I].Period = R.Range(6.5f, 10.f); Waves[I].T = R.Range(0.f, Waves[I].Period); Waves[I].Pan = I ? 0.45f : -0.45f;
				Waves[I].Lp.Set(400.f, 0.7f, Sr); Waves[I].Hiss.Set(2600.f, 0.5f, Sr);
			}
			for (int32 I = 0; I < 3; ++I) { Crk[I].Hz = R.Range(3400.f, 4300.f); Crk[I].Timer = R.Range(0.f, 0.8f); Crk[I].Pan = R.Range(-0.7f, 0.7f); Crk[I].Amp = R.Range(0.5f, 1.f); }
			RootHz = 98.f;
			bSnap = true;
		}

		void SetRoot(float Hz) { RootHz = Hz; }

		void ComputeTargets(const FDmAudioContext& C)
		{
			const float A = Sat01(C.Altitude01);
			const float Near = 1.f - SStep(0.30f, 0.62f, A);
			const float Mid = SStep(0.12f, 0.45f, A) * (1.f - SStep(0.70f, 0.92f, A));
			const float Far = SStep(0.55f, 0.90f, A);
			const float Day = Sat01(C.Day), Night = 1.f - Day;
			const float Storm = Sat01(C.Storm);
			auto& T = Target;
			T[int32(EDmLayer::Space)] = Sat01(Far * 0.95f + (C.Phase <= 0 ? 0.1f : 0.f));
			T[int32(EDmLayer::Wind)] = Sat01((0.18f + 0.45f * Mid + 0.45f * C.Mountain * (1.f - Far) + 0.3f * C.Cold + 0.65f * Storm + 0.2f * C.Desert * Near)
				* (1.f - 0.72f * Far) * (1.f - 0.35f * Sat01(C.Forest) * Near));
			T[int32(EDmLayer::Surf)] = Sat01(C.Coast * 1.15f) * (1.f - SStep(0.35f, 0.68f, A));
			T[int32(EDmLayer::Sea)] = Sat01(C.Sea) * (1.f - 0.6f * Sat01(C.Coast)) * (1.f - SStep(0.30f, 0.62f, A)) * 0.85f;
			const float Calm = (1.f - Storm) * (1.f - 0.6f * Sat01(C.Rain));
			T[int32(EDmLayer::Birds)] = Sat01((C.Forest + 0.45f * C.Open) * std::pow(Day, 1.5f) * Near * Calm * 1.2f);
			T[int32(EDmLayer::Crickets)] = Sat01((0.8f * C.Forest + C.Open) * std::pow(Night, 1.5f) * Near * Calm * (1.f - C.Cold) * 1.2f);
			T[int32(EDmLayer::Rain)] = Sat01(FMath::Max(C.Rain, Storm * 0.9f)) * (1.f - 0.9f * Far);
			T[int32(EDmLayer::Fire)] = Sat01(C.Fire * 1.5f) * (1.f - SStep(0.35f, 0.70f, A));
			T[int32(EDmLayer::Lava)] = FMath::Max(Sat01(C.Lava * 1.5f) * (1.f - SStep(0.40f, 0.75f, A)), C.Phase == 0 ? 0.85f : 0.f);
			const int32 TA = C.TownAge >= 0 ? C.TownAge : C.Age;
			const float Town = Sat01(C.Town * 1.3f) * Near * (0.55f + 0.45f * Day);
			T[int32(EDmLayer::TownEarly)] = TA <= 1 ? Town : 0.f;
			T[int32(EDmLayer::TownMiddle)] = (TA == 2 || TA == 3) ? Town : 0.f;
			T[int32(EDmLayer::TownIndustry)] = TA == 4 ? Town : 0.f;
			T[int32(EDmLayer::TownModern)] = TA >= 5 ? Town : 0.f;
			T[int32(EDmLayer::Leaves)] = Sat01(C.Forest * 1.2f + 0.3f * C.Open) * Near * (0.6f + 0.4f * Storm);
			if (bSnap) { for (int32 I = 0; I < NumLayers; ++I) Gain[I] = Target[I]; bSnap = false; }
		}

		void Render(float* L, float* R2, int32 N, float BlockSec)
		{
			const float Coef = SmoothCoef(1.1f, 1.f / BlockSec);
			for (int32 I = 0; I < NumLayers; ++I)
			{
				const float G0 = Gain[I];
				Gain[I] += Coef * (Target[I] - Gain[I]);
				if (G0 < 1e-4f && Gain[I] < 1e-4f) continue;
				FMemory::Memzero(TmpL, sizeof(float) * N);
				FMemory::Memzero(TmpR, sizeof(float) * N);
				switch (EDmLayer(I))
				{
				case EDmLayer::Space: RenderSpace(N); break;
				case EDmLayer::Wind: RenderWind(N); break;
				case EDmLayer::Surf: RenderSurf(N); SurfHighPass(N); break;
				case EDmLayer::Sea: RenderSea(N); break;
				case EDmLayer::Birds: RenderBirds(N); break;
				case EDmLayer::Crickets: RenderCrickets(N); break;
				case EDmLayer::Rain: RenderRain(N); break;
				case EDmLayer::Fire: RenderFire(N); break;
				case EDmLayer::Lava: RenderLava(N); break;
				case EDmLayer::TownEarly: RenderTown(N, 0); break;
				case EDmLayer::TownMiddle: RenderTown(N, 1); break;
				case EDmLayer::TownIndustry: RenderTown(N, 2); break;
				case EDmLayer::TownModern: RenderTown(N, 3); break;
				case EDmLayer::Leaves: RenderLeaves(N); break;
				default: break;
				}
				const float G1 = Gain[I];
				const float Step = (G1 - G0) / float(N);
				float G = G0;
				for (int32 K = 0; K < N; ++K) { G += Step; L[K] += TmpL[K] * G; R2[K] += TmpR[K] * G; }
			}
		}

	private:
		float Sr = 48000.f, InvSr = 1.f / 48000.f;
		FRng R;
		bool bSnap = true;
		float RootHz = 98.f;
		float TmpL[MaxBlock] = {}, TmpR[MaxBlock] = {};

		FPink SpPink[2]; FSvf SpBp[2], SpAir[2]; FDrift SpDrift; float SpPh[3] = {}; int32 SpCtrl = 0;
		FPink WPink[2]; FBrown WBrown[2]; FSvf WBp[2]; FDrift Gust1, Gust2, GustPan; int32 WCtrl = 0; float GustNow = 0.5f;
		FPink LfPk[2]; FSvf LfBp[2], LfLo[2]; FDrift LfDrift, LfDrift2; float LfNow = 0.5f; int32 LfCtrl = 0;
		FSurfWave Waves[2];
		FHp1 SurfHp[2];
		FBrown SeaBr[2]; FSvf SeaLp[2]; FDrift SeaDrift; float SeaNow = 0.5f; int32 SeaCtrl = 0;
		FChirp Chirps[5]; float BirdTimer = 0.5f;
		FCricket Crk[3]; float TrillPh = 0.f, TrillAm = 0.f;
		FPink RnPk[2]; FSvf RnBp[2]; FBrown RnBr[2]; FSvf RnLp[2]; FGrainA Drops[12];
		FBrown FireBr[2]; FSvf FireLp[2]; FDrift FireDrift; FGrainA Pops[10]; float FireNow = 0.5f; int32 FireCtrl = 0;
		FBrown LavaBr[2]; FSvf LavaBp[2]; FDrift LavaDrift; FGrainA Blorps[4]; float LavaNow = 0.5f; int32 LavaCtrl = 0;
		FPink MurPk[2]; FSvf MurA[2], MurB[2]; FDrift Syll, Crowd; float SyllNow = 0.f, CrowdNow = 0.f; int32 MurCtrl = 0;
		FPing Pings[4]; float PingTimer = 1.f; int32 PingBurst = 0; float BellTimer = 6.f; FPing Bell;
		FPink ChPk[2]; FSvf ChuffLp[2]; float ChuffPh = 0.f; float HumPh[3] = {};
		FBrown TrBr[2]; FPink TrPk[2]; FSvf TrafLp[2]; FDrift Traffic; float TrafNow = 0.f; int32 TrafCtrl = 0;
		FGrainA Knocks[3]; float KnockTimer = 1.f;

		void RenderSpace(int32 N)
		{
			for (int32 K = 0; K < N; ++K)
			{
				if (++SpCtrl >= 32)
				{
					SpCtrl = 0;
					const float D = SpDrift.Tick(R);
					SpBp[0].Set(420.f + 160.f * D, 0.7f, Sr); SpBp[1].Set(440.f - 150.f * D, 0.7f, Sr);
				}
				SpPh[0] = Wrap01(SpPh[0] + RootHz * InvSr);
				SpPh[1] = Wrap01(SpPh[1] + RootHz * 1.5f * 1.0013f * InvSr);
				SpPh[2] = Wrap01(SpPh[2] + 0.055f * InvSr);
				const float Hum = (Sin01(SpPh[0]) * 0.6f + Sin01(SpPh[1]) * 0.35f) * (0.55f + 0.45f * Sin01(SpPh[2])) * 0.05f;
				for (int32 C = 0; C < 2; ++C)
				{
					const float P = SpPink[C].Tick(R.Bi());
					const float V = SpBp[C].Band(P) * 0.11f + SpAir[C].Band(P) * 0.025f + Hum;
					(C ? TmpR : TmpL)[K] = V;
				}
			}
		}

		void RenderWind(int32 N)
		{
			for (int32 K = 0; K < N; ++K)
			{
				if (++WCtrl >= 32)
				{
					WCtrl = 0;
					const float G1 = 0.5f + 0.5f * Gust1.Tick(R), G2 = 0.5f + 0.5f * Gust2.Tick(R);
					GustNow = Sat01(0.25f + 0.55f * G1 * (0.4f + 0.6f * G2) + 0.3f * G2 * G2);
					const float Pn = GustPan.Tick(R) * 0.25f;
					WBp[0].Set(260.f + 700.f * GustNow * (1.f + Pn), 0.8f + 0.5f * GustNow, Sr);
					WBp[1].Set(270.f + 720.f * GustNow * (1.f - Pn), 0.8f + 0.5f * GustNow, Sr);
				}
				const float Amp = 0.045f + 0.085f * GustNow * GustNow;
				for (int32 C = 0; C < 2; ++C)
				{
					const float W = R.Bi();
					const float Src = WPink[C].Tick(W) * 2.2f + WBrown[C].Tick(W) * 0.6f;
					(C ? TmpR : TmpL)[K] = WBp[C].Band(Src) * Amp;
				}
			}
		}

		void RenderSurf(int32 N)
		{
			for (FSurfWave& W : Waves)
			{
				float Gl, Gr;
				PanGains(W.Pan, Gl, Gr);
				for (int32 K = 0; K < N; ++K)
				{
					W.T += InvSr;
					if (W.T >= W.Period) { W.T -= W.Period; W.Period = R.Range(6.5f, 10.5f); W.Size = R.Range(0.6f, 1.f); }
					const float U = W.T / W.Period;
					// swell (slow rise), the break at 58 %, the wash running up and back
					// Every term is continuous across the cycle (verifier fix: the break used to jump to full in one
					// sample and the wash stepped at the wrap; the probe measured a 12.7 dB level step in 20 ms on the
					// old formula, 5.1 dB (the noise floor) on this one: scripts probe-surf in DmAudioTest.cpp).
					const float Swell = SStep(0.f, 0.58f, U) * (1.f - SStep(0.58f, 0.66f, U));
					const float Fade = 1.f - SStep(0.9f, 1.f, U);                  // all the break and wash has died away before the wrap
					const float Tb = (U - 0.58f) * W.Period;                       // seconds from the break (negative: building into it)
					const float Crash = (Tb < 0.f ? SStep(-0.3f, 0.f, Tb) : std::exp(-Tb * 1.6f)) * Fade;
					const float Wash = std::exp(-(U - 0.6f) * W.Period * 0.45f) * SStep(0.58f, 0.7f, U) * Fade;
					if ((K & 31) == 0) W.Lp.Set(260.f + 1600.f * Crash + 500.f * Wash, 0.7f, Sr);
					const float Wh = R.Bi();
					const float Src = W.Br.Tick(Wh) * 0.7f + W.Pk.Tick(Wh) * 1.4f;
					const float Body = W.Lp.Low(Src) * (0.25f * Swell + 0.9f * Crash + 0.45f * Wash);
					const float Fizz = W.Hiss.Band(Wh) * 0.05f * (Crash * 0.6f + Wash);
					const float V = (Body + Fizz) * 0.36f * W.Size;
					TmpL[K] += V * Gl; TmpR[K] += V * Gr;
				}
			}
		}

		void SurfHighPass(int32 N)
		{
			for (int32 K = 0; K < N; ++K) { TmpL[K] = SurfHp[0].Tick(TmpL[K]); TmpR[K] = SurfHp[1].Tick(TmpR[K]); }
		}

		void RenderLeaves(int32 N)
		{
			for (int32 K = 0; K < N; ++K)
			{
				if (++LfCtrl >= 32)
				{
					LfCtrl = 0;
					const float A1 = 0.5f + 0.5f * LfDrift.Tick(R), A2 = 0.5f + 0.5f * LfDrift2.Tick(R);
					LfNow = 0.25f + 0.75f * A1 * A1 * (0.5f + 0.5f * A2);
				}
				for (int32 C = 0; C < 2; ++C)
				{
					const float P = LfPk[C].Tick(R.Bi());
					const float V = (LfBp[C].Band(P) * 0.05f + LfLo[C].Band(P) * 0.04f) * LfNow;
					(C ? TmpR : TmpL)[K] = V;
				}
			}
		}

		void RenderSea(int32 N)
		{
			for (int32 K = 0; K < N; ++K)
			{
				if (++SeaCtrl >= 32) { SeaCtrl = 0; SeaNow = 0.55f + 0.45f * SeaDrift.Tick(R); }
				for (int32 C = 0; C < 2; ++C)
					(C ? TmpR : TmpL)[K] = SeaLp[C].Low(SeaBr[C].Tick(R.Bi())) * 0.2f * SeaNow;
			}
		}

		void StartChirp(FChirp& Ch)
		{
			Ch.bOn = true;
			Ch.T = 0.f; Ch.bGap = false; Ch.Ph = 0.f;
			Ch.Pan = R.Range(-0.75f, 0.75f);
			Ch.Amp = R.Range(0.25f, 1.f);
			const float U = R.Uni();
			if (U < 0.5f)       // warbler: quick bright notes
			{
				Ch.Species = 0; Ch.NotesLeft = 3 + R.Int(5);
				Ch.NoteDur = R.Range(0.05f, 0.09f); Ch.Gap = R.Range(0.03f, 0.07f);
				Ch.F0 = R.Range(2300.f, 3300.f); Ch.F1 = Ch.F0 * R.Range(0.8f, 1.25f);
			}
			else if (U < 0.8f)  // thrush-like phrase
			{
				Ch.Species = 1; Ch.NotesLeft = 2 + R.Int(3);
				Ch.NoteDur = R.Range(0.12f, 0.22f); Ch.Gap = R.Range(0.06f, 0.12f);
				Ch.F0 = R.Range(1500.f, 2400.f); Ch.F1 = Ch.F0 * R.Range(1.05f, 1.35f);
			}
			else                // dove: low coos
			{
				Ch.Species = 2; Ch.NotesLeft = 2 + R.Int(2);
				Ch.NoteDur = R.Range(0.3f, 0.42f); Ch.Gap = R.Range(0.12f, 0.25f);
				Ch.F0 = R.Range(470.f, 560.f); Ch.F1 = Ch.F0 * 0.9f;
				Ch.Amp *= 1.6f;
			}
		}

		void RenderBirds(int32 N)
		{
			BirdTimer -= float(N) * InvSr;
			if (BirdTimer <= 0.f)
			{
				BirdTimer = R.Range(0.4f, 2.2f);
				for (FChirp& Ch : Chirps) if (!Ch.bOn) { StartChirp(Ch); break; }
			}
			for (FChirp& Ch : Chirps)
			{
				if (!Ch.bOn) continue;
				float Gl, Gr;
				PanGains(Ch.Pan, Gl, Gr);
				for (int32 K = 0; K < N; ++K)
				{
					Ch.T += InvSr;
					if (Ch.bGap)
					{
						if (Ch.T >= Ch.Gap)
						{
							Ch.bGap = false; Ch.T = 0.f;
							if (--Ch.NotesLeft <= 0) { Ch.bOn = false; break; }
							Ch.F0 *= R.Range(0.92f, 1.08f); Ch.F1 = Ch.F0 * R.Range(0.85f, 1.25f);
						}
						continue;
					}
					if (Ch.T >= Ch.NoteDur) { Ch.bGap = true; Ch.T = 0.f; continue; }
					const float U = Ch.T / Ch.NoteDur;
					const float Hz = Ch.F0 + (Ch.F1 - Ch.F0) * U + (Ch.Species == 2 ? Ch.F0 * 0.02f * Sin01(U * 3.f) : 0.f);
					Ch.Ph = Wrap01(Ch.Ph + Hz * InvSr);
					const float Env = Sin01(U * 0.5f);   // sin(pi u)
					const float V = Sin01(Ch.Ph) * Env * Env * Ch.Amp * (Ch.Species == 2 ? 0.05f : 0.035f);
					TmpL[K] += V * Gl; TmpR[K] += V * Gr;
				}
			}
		}

		void RenderCrickets(int32 N)
		{
			for (FCricket& C : Crk)
			{
				float Gl, Gr;
				PanGains(C.Pan, Gl, Gr);
				for (int32 K = 0; K < N; ++K)
				{
					C.Timer -= InvSr;
					if (C.Timer <= 0.f) { C.Timer = R.Range(0.45f, 0.85f); C.Pulses = 3 + R.Int(2); C.PulseT = 0.f; }
					float Env = 0.f;
					if (C.Pulses > 0)
					{
						C.PulseT += InvSr;
						const float PulseLen = 0.032f;
						const float U = C.PulseT / PulseLen;
						if (U < 0.45f) { const float E = Sin01(U / 0.45f * 0.5f); Env = E * E; }
						if (C.PulseT >= PulseLen) { C.PulseT = 0.f; --C.Pulses; }
					}
					C.Ph = Wrap01(C.Ph + C.Hz * InvSr);
					const float V = Sin01(C.Ph) * Env * C.Amp * 0.03f;
					TmpL[K] += V * Gl; TmpR[K] += V * Gr;
				}
			}
			// a soft distant trill under the chirps
			for (int32 K = 0; K < N; ++K)
			{
				TrillPh = Wrap01(TrillPh + 2900.f * InvSr);
				TrillAm = Wrap01(TrillAm + 38.f * InvSr);
				const float A = 0.5f + 0.5f * Sin01(TrillAm);
				const float V = Sin01(TrillPh) * A * A * 0.008f;
				TmpL[K] += V * 0.8f; TmpR[K] += V * 0.6f;
			}
		}

		void RenderRain(int32 N)
		{
			for (int32 K = 0; K < N; ++K)
			{
				if (R.Uni() < 70.f * InvSr)
					for (FGrainA& D : Drops) if (!D.bOn)
					{
						D.bOn = true; D.Ph = 0.f; D.Inc = R.Range(1400.f, 3400.f) * InvSr; D.Amp = R.Range(0.2f, 1.f) * 0.025f;
						D.Mul = DecayMul(R.Range(0.006f, 0.014f), Sr); D.Pan = R.Bi() * 0.8f; D.Glide = R.Range(0.9997f, 1.0006f);
						break;
					}
				float DL = 0.f, DR = 0.f;
				for (FGrainA& D : Drops)
				{
					if (!D.bOn) continue;
					D.Ph = Wrap01(D.Ph + D.Inc); D.Inc *= D.Glide;
					const float V = Sin01(D.Ph) * D.Amp;
					D.Amp *= D.Mul;
					if (D.Amp < 1e-5f) D.bOn = false;
					DL += V * (0.7f - 0.3f * D.Pan); DR += V * (0.7f + 0.3f * D.Pan);
				}
				for (int32 C = 0; C < 2; ++C)
				{
					const float W = R.Bi();
					const float V = RnBp[C].Band(RnPk[C].Tick(W)) * 0.16f + RnLp[C].Low(RnBr[C].Tick(W)) * 0.12f;
					(C ? TmpR : TmpL)[K] = V + (C ? DR : DL);
				}
			}
		}

		void RenderFire(int32 N)
		{
			for (int32 K = 0; K < N; ++K)
			{
				if (++FireCtrl >= 32) { FireCtrl = 0; FireNow = 0.6f + 0.4f * FireDrift.Tick(R); }
				if (R.Uni() < 26.f * InvSr)
					for (FGrainA& P : Pops) if (!P.bOn)
					{
						P.bOn = true; P.Amp = (R.Chance(0.12f) ? 1.f : 0.35f) * R.Range(0.3f, 1.f) * 0.2f;
						P.Mul = DecayMul(R.Range(0.002f, 0.006f), Sr); P.Pan = R.Bi() * 0.7f;
						const float Hz = R.Range(800.f, 3000.f);
						P.Lp.Reset(); P.Lp.Set(Hz * 1.5f, Sr); P.Hp.Set(Hz * 0.5f, Sr);
						break;
					}
				float PL = 0.f, PR = 0.f;
				for (FGrainA& P : Pops)
				{
					if (!P.bOn) continue;
					const float V = P.Lp.Tick(P.Hp.Tick(R.Bi())) * P.Amp * 2.f;
					P.Amp *= P.Mul;
					if (P.Amp < 1e-5f) P.bOn = false;
					PL += V * (0.7f - 0.3f * P.Pan); PR += V * (0.7f + 0.3f * P.Pan);
				}
				for (int32 C = 0; C < 2; ++C)
					(C ? TmpR : TmpL)[K] = FireLp[C].Low(FireBr[C].Tick(R.Bi())) * 0.16f * FireNow + (C ? PR : PL);
			}
		}

		void RenderLava(int32 N)
		{
			for (int32 K = 0; K < N; ++K)
			{
				if (++LavaCtrl >= 32) { LavaCtrl = 0; LavaNow = 0.6f + 0.4f * LavaDrift.Tick(R); }
				if (R.Uni() < 3.f * InvSr)
					for (FGrainA& B : Blorps) if (!B.bOn)
					{
						B.bOn = true; B.Ph = 0.f; B.Inc = R.Range(75.f, 150.f) * InvSr; B.Glide = 1.f + R.Range(4.f, 9.f) * InvSr;
						B.Amp = R.Range(0.4f, 1.f) * 0.06f; B.Mul = DecayMul(R.Range(0.12f, 0.25f), Sr); B.Pan = R.Bi() * 0.6f;
						break;
					}
				float BL = 0.f, BR = 0.f;
				for (FGrainA& B : Blorps)
				{
					if (!B.bOn) continue;
					B.Ph = Wrap01(B.Ph + B.Inc); B.Inc *= B.Glide;
					const float V = Sin01(B.Ph) * B.Amp;
					B.Amp *= B.Mul;
					if (B.Amp < 1e-5f) B.bOn = false;
					BL += V * (0.7f - 0.3f * B.Pan); BR += V * (0.7f + 0.3f * B.Pan);
				}
				for (int32 C = 0; C < 2; ++C)
					(C ? TmpR : TmpL)[K] = LavaBp[C].Band(LavaBr[C].Tick(R.Bi())) * 0.22f * LavaNow + (C ? BR : BL);
			}
		}

		void StartPing(FPing& P, float Hz, const float* Ratios, const float* Amps, const float* Decays, float Pan)
		{
			P.bOn = true; P.Pan = Pan;
			for (int32 I = 0; I < 3; ++I) { P.Ph[I] = R.Uni(); P.Inc[I] = Hz * Ratios[I] * InvSr; P.Amp[I] = Amps[I]; P.Mul[I] = DecayMul(Decays[I], Sr); }
		}
		void RenderPing(FPing& P, int32 N, float Level)
		{
			if (!P.bOn) return;
			float Gl, Gr;
			PanGains(P.Pan, Gl, Gr);
			for (int32 K = 0; K < N; ++K)
			{
				float V = 0.f;
				for (int32 I = 0; I < 3; ++I) { P.Ph[I] = Wrap01(P.Ph[I] + P.Inc[I]); V += Sin01(P.Ph[I]) * P.Amp[I]; P.Amp[I] *= P.Mul[I]; }
				V *= Level;
				TmpL[K] += V * Gl; TmpR[K] += V * Gr;
			}
			if (P.Amp[0] < 1e-4f && P.Amp[1] < 1e-4f && P.Amp[2] < 1e-4f) P.bOn = false;
		}

		void RenderTown(int32 N, int32 Era)
		{
			const float Bs = float(N) * InvSr;
			// murmur of people (every era; softer in the modern city where traffic covers it)
			const float MurLevel = Era == 0 ? 0.055f : (Era == 1 ? 0.05f : (Era == 2 ? 0.03f : 0.022f));
			for (int32 K = 0; K < N; ++K)
			{
				if (++MurCtrl >= 32) { MurCtrl = 0; SyllNow = 0.5f + 0.5f * Syll.Tick(R); CrowdNow = 0.6f + 0.4f * Crowd.Tick(R); }
				for (int32 C = 0; C < 2; ++C)
				{
					const float P = MurPk[C].Tick(R.Bi());
					const float V = (MurA[C].Band(P) + 0.45f * MurB[C].Band(P)) * (0.4f + 0.6f * SyllNow) * CrowdNow * MurLevel;
					(C ? TmpR : TmpL)[K] += V;
				}
			}
			if (Era == 0)
			{
				// wood knocks and the odd drum
				KnockTimer -= Bs;
				if (KnockTimer <= 0.f)
				{
					KnockTimer = R.Range(0.5f, 2.f);
					for (FGrainA& Kn : Knocks) if (!Kn.bOn)
					{
						Kn.bOn = true; Kn.Ph = 0.f; Kn.Inc = R.Range(260.f, 480.f) * InvSr; Kn.Amp = R.Range(0.3f, 1.f) * 0.05f;
						Kn.Mul = DecayMul(R.Range(0.05f, 0.1f), Sr); Kn.Pan = R.Bi() * 0.7f; Kn.Glide = 0.99996f;
						break;
					}
				}
				for (FGrainA& Kn : Knocks)
				{
					if (!Kn.bOn) continue;
					for (int32 K = 0; K < N; ++K)
					{
						Kn.Ph = Wrap01(Kn.Ph + Kn.Inc); Kn.Inc *= Kn.Glide;
						const float V = Sin01(Kn.Ph) * Kn.Amp;
						Kn.Amp *= Kn.Mul;
						TmpL[K] += V * (0.7f - 0.3f * Kn.Pan); TmpR[K] += V * (0.7f + 0.3f * Kn.Pan);
					}
					if (Kn.Amp < 1e-5f) Kn.bOn = false;
				}
			}
			else if (Era == 1)
			{
				// anvils in bursts, a distant bell now and then
				PingTimer -= Bs;
				if (PingTimer <= 0.f)
				{
					if (PingBurst <= 0) PingBurst = 3 + R.Int(4);
					--PingBurst;
					PingTimer = PingBurst > 0 ? R.Range(0.38f, 0.48f) : R.Range(2.5f, 6.f);
					static const float Ra[3] = { 1.f, 2.76f, 5.4f }, Am[3] = { 0.04f, 0.022f, 0.008f }, De[3] = { 0.45f, 0.25f, 0.12f };
					for (FPing& P : Pings) if (!P.bOn) { StartPing(P, R.Range(820.f, 900.f), Ra, Am, De, R.Range(-0.5f, 0.5f)); break; }
				}
				BellTimer -= Bs;
				if (BellTimer <= 0.f)
				{
					BellTimer = R.Range(10.f, 18.f);
					static const float Ra[3] = { 1.f, 2.f, 2.4f }, Am[3] = { 0.06f, 0.03f, 0.02f }, De[3] = { 4.5f, 2.5f, 1.8f };
					StartPing(Bell, RootHz * 4.f, Ra, Am, De, R.Range(-0.4f, 0.4f));
				}
				for (FPing& P : Pings) RenderPing(P, N, 1.f);
				RenderPing(Bell, N, 1.f);
			}
			else if (Era == 2)
			{
				// steam engines chuffing, a low machine hum
				for (int32 K = 0; K < N; ++K)
				{
					ChuffPh = Wrap01(ChuffPh + 3.4f * InvSr);
					const float E = ChuffPh < 0.35f ? Sin01(ChuffPh / 0.35f * 0.5f) : 0.f;
					HumPh[0] = Wrap01(HumPh[0] + RootHz * InvSr);
					HumPh[1] = Wrap01(HumPh[1] + RootHz * 2.f * InvSr);
					HumPh[2] = Wrap01(HumPh[2] + RootHz * 3.f * InvSr);
					const float Hum = (Sin01(HumPh[0]) * 0.6f + Sin01(HumPh[1]) * 0.3f + Sin01(HumPh[2]) * 0.12f) * 0.022f;
					for (int32 C = 0; C < 2; ++C)
					{
						const float V = ChuffLp[C].Low(ChPk[C].Tick(R.Bi())) * E * E * 0.32f + Hum;
						(C ? TmpR : TmpL)[K] += V;
					}
				}
			}
			else
			{
				// traffic: a broad swelling hum of passing engines
				for (int32 K = 0; K < N; ++K)
				{
					if (++TrafCtrl >= 32) { TrafCtrl = 0; TrafNow = 0.55f + 0.45f * Traffic.Tick(R); }
					HumPh[0] = Wrap01(HumPh[0] + RootHz * 1.5f * InvSr);
					const float Hum = Sin01(HumPh[0]) * 0.008f;
					for (int32 C = 0; C < 2; ++C)
					{
						const float W = R.Bi();
						const float V = TrafLp[C].Low(TrBr[C].Tick(W) * 0.8f + TrPk[C].Tick(W) * 0.8f) * 0.12f * TrafNow + Hum;
						(C ? TmpR : TmpL)[K] += V;
					}
				}
			}
		}
	};

	struct FCmd
	{
		uint8 Kind = 0;      // 0 cue, 1 stinger, 2 suspend
		EDmCue Cue = EDmCue::Click;
		EDmStinger Sting = EDmStinger::Age;
		FDmCueParams P;
		bool bFlag = false;
	};
	constexpr int32 MaxCmds = 128;
	constexpr int32 MaxVoices = 32;
}

// ------------------------------------------------------------------------------------------------ engine

struct FDmAudioEngine::FImpl
{
	float Sr = 48000.f;
	uint32 Seed = 1;

	mutable FCriticalSection Lock;
	FDmAudioContext PendingCtx;
	bool bCtxDirty = false;
	FDmAudioVolumes PendingVol;
	bool bVolDirty = false;
	FCmd Ring[MaxCmds];
	int32 RingN = 0;
	FDmAudioLevels Shared;

	FCmd Local[MaxCmds];
	FDmAudioContext Ctx;
	bool bHaveCtx = false;
	FDmAudioVolumes Vol;

	FAmbience Amb;
	FDmMusic Music;
	FDmCueVoice Voices[MaxVoices];
	FDmCuePatch PatchScratch;
	int64 Order = 0;
	FRng Rng;

	FReverb Rev;
	FSvf HpL, HpR;
	FLp1 TopL, TopR;
	FDcBlock DcL, DcR;
	FLimiter Lim;
	float Fade = 1.f, FadeTarget = 1.f, FadeCoef = 0.f;
	float ThunderTimer = 3.f;

	float AmbL[MaxBlock], AmbR[MaxBlock], MusL[MaxBlock], MusR[MaxBlock], SfxL[MaxBlock], SfxR[MaxBlock], UiL[MaxBlock], UiR[MaxBlock];
	float SAmbL[MaxBlock], SAmbR[MaxBlock], SMusL[MaxBlock], SMusR[MaxBlock], SSfxL[MaxBlock], SSfxR[MaxBlock], SUiL[MaxBlock], SUiR[MaxBlock];

	FDmAudioLevels Lv;
	double RenderSec = 0;

	void Init(float InSr, uint32 InSeed)
	{
		Sr = InSr;
		Seed = InSeed;
		Rng = FRng(InSeed ^ 0xC0FFEEu);
		Amb.Init(Sr, InSeed);
		Music.Init(Sr, InSeed);
		Rev.Init(Sr, 3.4f, 4200.f, 0.028f);
		HpL.Set(28.f, 0.707f, Sr); HpR.Set(28.f, 0.707f, Sr);
		TopL.Set(13500.f, Sr); TopR.Set(13500.f, Sr);
		Lim.Init(Sr, -1.f, 0.002f, 0.18f);
		FadeCoef = SmoothCoef(0.3f, Sr);
		Amb.SetRoot(Music.GetScale().Hz(0, 2) < 80.f ? Music.GetScale().Hz(0, 3) : Music.GetScale().Hz(0, 2));
	}

	void StartCue(const FCmd& C)
	{
		const FDmScale& Sc = Music.GetScale();
		BuildCuePatch(C.Cue, C.P, Sc, Rng, PatchScratch);
		int32 Slot = -1;
		int64 Oldest = TNumericLimits<int64>::Max();
		float Quietest = 1e9f;
		for (int32 I = 0; I < MaxVoices; ++I)
		{
			if (!Voices[I].bActive) { Slot = I; break; }
			const float Lvl = Voices[I].Level();
			if (Lvl < Quietest || (Lvl == Quietest && Voices[I].StartOrder < Oldest)) { Quietest = Lvl; Oldest = Voices[I].StartOrder; Slot = I; }
		}
		if (Slot < 0) return;
		Voices[Slot].Start(C.Cue, PatchScratch, C.P, Sc, Sr, Rng.Next() | 1u, ++Order);
		++Lv.CuesStarted;
	}

	void Block(float* OutL, float* OutR, int32 N)
	{
		// take the game thread's state and commands (never block the audio thread)
		int32 NumLocal = 0;
		if (Lock.TryLock())
		{
			if (bCtxDirty) { Ctx = PendingCtx; bCtxDirty = false; bHaveCtx = true; }
			if (bVolDirty) { Vol = PendingVol; bVolDirty = false; }
			NumLocal = RingN;
			for (int32 I = 0; I < RingN; ++I) Local[I] = Ring[I];
			RingN = 0;
			Shared = Lv;
			Lock.Unlock();
		}
		for (int32 I = 0; I < NumLocal; ++I)
		{
			const FCmd& C = Local[I];
			if (C.Kind == 0) StartCue(C);
			else if (C.Kind == 1) { Music.Stinger(C.Sting); ++Lv.StingersStarted; }
			else FadeTarget = C.bFlag ? 0.f : 1.f;
		}

		const float BlockSec = float(N) / Sr;
		UpdateMusic(BlockSec);
		Amb.ComputeTargets(Ctx);

		// storms throw distant thunder on their own
		const float StormG = Amb.Gain[int32(EDmLayer::Rain)] * Sat01(Ctx.Storm);
		ThunderTimer -= BlockSec;
		if (ThunderTimer <= 0.f)
		{
			ThunderTimer = Rng.Range(2.f, 6.f);
			if (StormG > 0.05f && Rng.Chance(0.25f + 0.6f * StormG))
			{
				FCmd C; C.Kind = 0; C.Cue = EDmCue::Thunder;
				C.P.Vol = Rng.Range(0.35f, 0.85f) * StormG; C.P.Muffle = Rng.Range(0.35f, 0.9f); C.P.Pan = Rng.Bi() * 0.8f;
				StartCue(C);
			}
		}

		const size_t Bytes = sizeof(float) * size_t(N);
		FMemory::Memzero(AmbL, Bytes); FMemory::Memzero(AmbR, Bytes); FMemory::Memzero(MusL, Bytes); FMemory::Memzero(MusR, Bytes);
		FMemory::Memzero(SfxL, Bytes); FMemory::Memzero(SfxR, Bytes); FMemory::Memzero(UiL, Bytes); FMemory::Memzero(UiR, Bytes);
		FMemory::Memzero(SAmbL, Bytes); FMemory::Memzero(SAmbR, Bytes); FMemory::Memzero(SMusL, Bytes); FMemory::Memzero(SMusR, Bytes);
		FMemory::Memzero(SSfxL, Bytes); FMemory::Memzero(SSfxR, Bytes); FMemory::Memzero(SUiL, Bytes); FMemory::Memzero(SUiR, Bytes);

		Amb.Render(AmbL, AmbR, N, BlockSec);
		for (int32 K = 0; K < N; ++K) { SAmbL[K] = AmbL[K] * 0.1f; SAmbR[K] = AmbR[K] * 0.1f; }
		Music.Render(MusL, MusR, SMusL, SMusR, N);
		int32 NumVoices = 0;
		for (FDmCueVoice& V : Voices)
		{
			if (!V.bActive) continue;
			++NumVoices;
			if (DmAudio::CueBus(V.Cue) == EDmBus::Ui) V.Render(UiL, UiR, SUiL, SUiR, N);
			else V.Render(SfxL, SfxR, SSfxL, SSfxR, N);
		}

		// bus trims (measured mix balance): ambience +3 dB, music -2 dB, so the score sits under the world
		const float VA = Vol.Ambience * 1.41f, VM = Vol.Music * 0.8f, VS = Vol.Sfx, VU = Vol.Ui, VMa = Vol.Master * 1.58f;   // +4 dB make-up: the limiter only meets the biggest events
		float Peak = 0.f;
		for (int32 K = 0; K < N; ++K)
		{
			float L = AmbL[K] * VA + MusL[K] * VM + SfxL[K] * VS + UiL[K] * VU;
			float R = AmbR[K] * VA + MusR[K] * VM + SfxR[K] * VS + UiR[K] * VU;
			const float SL = SAmbL[K] * VA + SMusL[K] * VM + SSfxL[K] * VS + SUiL[K] * VU;
			const float SR = SAmbR[K] * VA + SMusR[K] * VM + SSfxR[K] * VS + SUiR[K] * VU;
			float WL, WR;
			Rev.Tick(SL, SR, WL, WR);
			L += WL * 0.6f; R += WR * 0.6f;
			L = DcL.Tick(TopL.Tick(HpL.High(L)));
			R = DcR.Tick(TopR.Tick(HpR.High(R)));
			Fade += FadeCoef * (FadeTarget - Fade);
			const float G = VMa * Fade;
			L *= G; R *= G;
			if (!std::isfinite(L) || !std::isfinite(R)) { L = 0.f; R = 0.f; }
			Lim.Tick(L, R);
			OutL[K] = L; OutR[K] = R;
			Peak = FMath::Max(Peak, FMath::Max(std::fabs(L), std::fabs(R)));
		}

		for (int32 I = 0; I < NumLayers; ++I) Lv.Layer[I] = Amb.Gain[I];
		Lv.Voices = NumVoices;
		Lv.MusicVoices = Music.ActiveNotes();
		Lv.FramesRendered += N;
		Lv.LimiterMinGain = Lim.MinGain;
		Lv.SafetyClips = Lim.SafetyClips;
		Lv.BlockPeak = Peak;
		Lv.MusicAge = Music.GetAge();
		Lv.MusicDensity = Music.GetDensity();
		Lv.MusicTension = Music.GetTension();
		Lv.ScaleRoot = Music.GetScale().Root;
		Lv.ScaleMode = Music.GetScale().Mode;
		for (int32 I = 0; I < 14 && I < int32(EDmInst::Count); ++I) Lv.InstNotes[I] = Music.NotesPlayed[I];
	}

	void UpdateMusic(float BlockSec)
	{
		FDmMusicParams P;
		P.Age = Ctx.Age; P.Phase = Ctx.Phase; P.Tension = Ctx.Tension; P.Altitude01 = Ctx.Altitude01; P.bPaused = Ctx.bPaused; P.bScore = Ctx.bScore; P.Seed = Ctx.Seed;
		const uint32 OldRoot = uint32(Music.GetScale().Root);
		Music.Update(P, BlockSec);
		if (uint32(Music.GetScale().Root) != OldRoot)
			Amb.SetRoot(Music.GetScale().Hz(0, 2) < 80.f ? Music.GetScale().Hz(0, 3) : Music.GetScale().Hz(0, 2));
	}
};

FDmAudioEngine::FDmAudioEngine(float SampleRate, uint32 Seed)
	: Sr(SampleRate > 8000.f ? SampleRate : 48000.f)
{
	Impl = new FImpl();
	Impl->Init(Sr, Seed);
	Impl->Ctx.Seed = Seed;
	Impl->PendingCtx.Seed = Seed;
}

FDmAudioEngine::~FDmAudioEngine()
{
	delete Impl;
	Impl = nullptr;
}

void FDmAudioEngine::SetContext(const FDmAudioContext& C)
{
	FScopeLock S(&Impl->Lock);
	Impl->PendingCtx = C;
	Impl->bCtxDirty = true;
}

void FDmAudioEngine::SetVolumes(const FDmAudioVolumes& V)
{
	FScopeLock S(&Impl->Lock);
	Impl->PendingVol = V;
	Impl->bVolDirty = true;
}

void FDmAudioEngine::Play(EDmCue Cue, const FDmCueParams& P)
{
	if (int32(Cue) < 0 || Cue >= EDmCue::Count) return;
	FScopeLock S(&Impl->Lock);
	if (Impl->RingN >= MaxCmds) return;
	FCmd& C = Impl->Ring[Impl->RingN++];
	C = FCmd();
	C.Kind = 0; C.Cue = Cue; C.P = P;
	if (!std::isfinite(C.P.Vol)) C.P.Vol = 0.f;
	if (!std::isfinite(C.P.Pan)) C.P.Pan = 0.f;
	if (!std::isfinite(C.P.Muffle)) C.P.Muffle = 0.f;
	if (!std::isfinite(C.P.Param)) C.P.Param = 0.f;
	if (!std::isfinite(C.P.Delay)) C.P.Delay = 0.f;
	C.P.Vol = FMath::Clamp(C.P.Vol, 0.f, 2.f);
	C.P.Delay = FMath::Clamp(C.P.Delay, 0.f, 4.f);
}

void FDmAudioEngine::Stinger(EDmStinger St)
{
	if (int32(St) < 0 || St >= EDmStinger::Count) return;
	FScopeLock S(&Impl->Lock);
	if (Impl->RingN >= MaxCmds) return;
	FCmd& C = Impl->Ring[Impl->RingN++];
	C = FCmd();
	C.Kind = 1; C.Sting = St;
}

void FDmAudioEngine::Suspend(bool bSuspend)
{
	FScopeLock S(&Impl->Lock);
	if (Impl->RingN >= MaxCmds) return;
	FCmd& C = Impl->Ring[Impl->RingN++];
	C = FCmd();
	C.Kind = 2; C.bFlag = bSuspend;
}

FDmAudioLevels FDmAudioEngine::GetLevels() const
{
	FScopeLock S(&Impl->Lock);
	FDmAudioLevels L = Impl->Shared;
	L.RenderSeconds = Impl->RenderSec;
	return L;
}

void FDmAudioEngine::RenderSplit(float* OutL, float* OutR, int32 Frames)
{
	if (!OutL || !OutR || Frames <= 0) return;
	const double T0 = FPlatformTime::Seconds();
	int32 Done = 0;
	while (Done < Frames)
	{
		const int32 N = FMath::Min(MaxBlock, Frames - Done);
		Impl->Block(OutL + Done, OutR + Done, N);
		Done += N;
	}
	Impl->RenderSec += FPlatformTime::Seconds() - T0;
}

void FDmAudioEngine::Render(float* Interleaved, int32 Frames)
{
	if (!Interleaved || Frames <= 0) return;
	float L[MaxBlock], R[MaxBlock];
	int32 Done = 0;
	while (Done < Frames)
	{
		const int32 N = FMath::Min(MaxBlock, Frames - Done);
		RenderSplit(L, R, N);
		for (int32 K = 0; K < N; ++K) { Interleaved[2 * (Done + K)] = L[K]; Interleaved[2 * (Done + K) + 1] = R[K]; }
		Done += N;
	}
}
