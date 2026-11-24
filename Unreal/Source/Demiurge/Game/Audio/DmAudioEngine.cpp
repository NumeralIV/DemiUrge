// DEMIURGE — Game/Audio/DmAudioEngine.cpp : mixer, ambience beds, ducking, voice limits, command queue. Owner: C9 audio.
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
		TEXT("crackle"), TEXT("fall"), TEXT("toast"), TEXT("chronicle"),
	};
	static const TCHAR* GStingerNames[int32(EDmStinger::Count)] = {
		TEXT("age"), TEXT("founding"), TEXT("miracle"), TEXT("wrath"), TEXT("ending-beloved"), TEXT("ending-feared"),
		TEXT("ending-forgotten"), TEXT("ending-parting"), TEXT("ending-silence"), TEXT("ending-ash"), TEXT("ending-quiet"),
	};
	static const TCHAR* GLayerNames[int32(EDmLayer::Count)] = {
		TEXT("space"), TEXT("wind"), TEXT("surf"), TEXT("sea"), TEXT("birds"), TEXT("crickets"), TEXT("rain"), TEXT("fire"),
		TEXT("lava"), TEXT("town-early"), TEXT("town-middle"), TEXT("town-industry"), TEXT("town-modern"), TEXT("leaves"),
		TEXT("high-air"), TEXT("grass"), TEXT("desert"), TEXT("cold"), TEXT("rockfall"), TEXT("wetland"), TEXT("rainforest"),
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
		case EDmCue::Hover: case EDmCue::Click: case EDmCue::Arm: case EDmCue::Deny: case EDmCue::Unlock: case EDmCue::Toast: case EDmCue::Chronicle: return EDmBus::Ui;
		default: return EDmBus::Sfx;
		}
	}
	int32 CueLimit(EDmCue C)
	{
		switch (C)
		{
		case EDmCue::Thunder: return 4;
		case EDmCue::Impact: return 2;
		case EDmCue::Rumble: return 3;
		case EDmCue::Roar: return 1;      // per variant
		case EDmCue::Wave: return 2;
		case EDmCue::Crackle: return 6;
		default: return 2;
		}
	}
}

// ------------------------------------------------------------------------------------------------ ambience

namespace
{
	constexpr int32 MaxBlock = 256;
	constexpr int32 NumLayers = int32(EDmLayer::Count);
	constexpr int32 MaxBeds = 6;     // 2: at most 6 beds audible at once

	// Level trim per bed (dB), calibrated by the offline renders: ambience alone about -32 LUFS at landscape zoom, -38 in space.
	const float GBedTrimDb[NumLayers] = {
		/* space */ -7.6f, /* wind */ -4.f, /* surf */ -7.f, /* sea */ -4.f, /* birds */ -5.f, /* crickets */ -2.f, /* rain */ -4.f, /* fire */ -2.f,
		/* lava */ 0.f, /* town-early */ 4.5f, /* town-middle */ 4.5f, /* town-industry */ 4.5f, /* town-modern */ 4.5f, /* leaves */ -5.f,
		/* high-air */ -6.f, /* grass */ -5.f, /* desert */ -4.f, /* cold */ -4.f, /* rockfall */ -3.f, /* wetland */ -4.f, /* rainforest */ -5.f,
	};

	/** A short pitched event: up to 3 partials with glides and decays (bells, gulls, whistles, owls, drips). */
	struct FPing
	{
		bool bOn = false; float Ph[3] = {}; float Inc[3] = {}; float Amp[3] = {}; float Mul[3] = {}; float Glide[3] = { 1.f, 1.f, 1.f };
		float Pan = 0.f, T = 0.f, Attack = 0.002f, Dur = 10.f, Fm = 0.f, FmRatio = 1.f, FmMul = 1.f, FmPh = 0.f, Am = 0.f, AmHz = 0.f, AmPh = 0.f;
		float Hold = 0.f;
	};
	struct FChirp
	{
		bool bOn = false;
		int32 Species = 0, NotesLeft = 0, NoteIdx = 0;
		float T = 0.f, NoteDur = 0.1f, Gap = 0.05f, F0 = 2000.f, F1 = 2500.f, Amp = 0.3f, Pan = 0.f, Ph = 0.f, AmPh = 0.f;
		bool bGap = false;
	};
	struct FCricket { float Hz = 4500.f, Ph = 0.f, AmPh = 0.f, Timer = 0.f, PulseT = 0.f, Pan = 0.f, Amp = 0.5f; int32 Pulses = 0; };
	struct FGrainA { bool bOn = false; float Ph = 0.f, Inc = 0.f, Amp = 0.f, Mul = 0.f, Pan = 0.f, Glide = 1.f; FLp1 Lp; FHp1 Hp; };
	struct FSurfWave
	{
		float T = 0.f, Period = 8.f, Pan = 0.f, Size = 1.f;
		FBrown Br; FPink Pk; FSvf Lp; FSvf Hiss;
	};
	struct FBurst { bool bOn = false; float T = 0.f, Dur = 0.3f, Amp = 0.f, Pan = 0.f; FSvf A, B; FPink Pk; };

	class FAmbience
	{
	public:
		float Gain[NumLayers] = {};
		float Target[NumLayers] = {};
		int32 Beds = 0;
		float EndT = -1.f;          // seconds into an ending's silence (< 0: none)

		void Init(float InSr, uint32 Seed)
		{
			Sr = InSr; InvSr = 1.f / Sr;
			R = FRng(Seed ^ 0x77AB1234u);
			for (int32 C = 0; C < 2; ++C)
			{
				SpLp[C].Set(300.f, 0.7f, Sr); SpHp[C].Set(40.f, Sr);
				HaBp[C].Set(700.f, 0.7f, Sr);
				WBp[C].Set(450.f, 0.9f, Sr); WhBp[C].Set(1200.f, 1.f, Sr);
				SeaLp[C].Set(400.f, 0.6f, Sr);
				RnHp[C].Set(500.f, 0.7f, Sr); RnLp[C].Set(8000.f, 0.7f, Sr); RnBody[C].Set(300.f, 0.7f, Sr);
				FireLp[C].Set(300.f, 0.7f, Sr); RoarLp[C].Set(200.f, 0.7f, Sr); RoarHp[C].Set(50.f, Sr);
				LavaBp[C].Set(110.f, 0.7f, Sr);
				SurfHp[C].Set(55.f, Sr);
				LfBp[C].Set(C ? 3100.f : 2900.f, 0.8f, Sr);
				GrBp[C].Set(C ? 2050.f : 1950.f, 0.8f, Sr);
				DsBp[C].Set(C ? 310.f : 290.f, 0.8f, Sr); SandHp[C].Set(5000.f, 0.7f, Sr);
				CoBp[C].Set(C ? 1420.f : 1380.f, 3.f, Sr);
				MachBp[C].Set(400.f, 1.2f, Sr); MachHp[C].Set(45.f, Sr);
				TrafLp[C].Set(250.f, 0.6f, Sr);
				MagBp[C].Set(300.f, 1.2f, Sr);
				InsHp[C].Set(1500.f, Sr);
				MurA[C].Set(C ? 420.f : 400.f, 2.f, Sr); MurB[C].Set(C ? 950.f : 900.f, 3.f, Sr);
			}
			SpDrift[0].Init(0.04f, Sr / 32.f); SpDrift[1].Init(0.04f, Sr / 32.f);
			HaDrift.Init(0.05f, Sr / 32.f);
			WDrift.Init(0.06f, Sr / 32.f);
			SeaHz = R.Range(0.08f, 0.12f);
			FireDrift.Init(1.5f, Sr / 32.f);
			LavaDrift.Init(0.3f, Sr / 32.f);
			LfDrift.Init(0.35f, Sr / 32.f); LfDrift2.Init(0.07f, Sr / 32.f);
			GrDrift.Init(0.12f, Sr / 32.f);
			DsDrift.Init(0.08f, Sr / 32.f);
			CoDrift.Init(0.1f, Sr / 32.f);
			TrafDrift.Init(0.2f, Sr / 32.f);
			MurDrift.Init(0.3f, Sr / 32.f);
			for (int32 I = 0; I < 2; ++I)
			{
				Waves[I].Period = R.Range(6.f, 10.f); Waves[I].T = R.Range(0.f, Waves[I].Period); Waves[I].Pan = I ? 0.3f : -0.3f;
				Waves[I].Lp.Set(400.f, 0.7f, Sr); Waves[I].Hiss.Set(2600.f, 0.5f, Sr);
			}
			for (int32 I = 0; I < 3; ++I) { Crk[I].Hz = R.Range(4350.f, 4650.f); Crk[I].Timer = R.Range(0.f, 0.8f); Crk[I].Pan = R.Range(-0.7f, 0.7f); Crk[I].Amp = R.Range(0.5f, 1.f); }
			for (int32 I = 0; I < 6; ++I) { Frogs[I].Hz = R.Range(950.f, 1250.f); Frogs[I].Timer = R.Range(0.f, 1.5f); Frogs[I].Pan = R.Range(-0.8f, 0.8f); Frogs[I].Amp = R.Range(0.4f, 1.f); FrogBp[I].Set(Frogs[I].Hz * 1.5f, 2.f, Sr); }
			for (int32 S = 0; S < 5; ++S) SpeciesTimer[S] = R.Range(0.3f, 4.f);
			OwlTimer = R.Range(5.f, 30.f);
			GustNext = R.Range(1.f, 6.f);
			WhooshNext = R.Range(2.f, 8.f);
			SandNext = R.Range(2.f, 8.f);
			CreakNext = R.Range(3.f, 20.f); GroanNext = R.Range(10.f, 60.f);
			RockNext = R.Range(2.f, 10.f);
			VoiceNext = R.Range(1.f, 6.f); GoatNext = R.Range(1.f, 8.f); HammerNext = R.Range(1.f, 8.f); ChurchNext = R.Range(4.f, 20.f);
			GullNext = R.Range(1.f, 6.f); WhistleNext = R.Range(6.f, 30.f); HornNext = R.Range(3.f, 15.f); MaglevNext = R.Range(3.f, 12.f);
			RootHz = 73.4f;
			bSnap = true;
		}

		void SetRoot(float Hz) { RootHz = Hz; }

		void ComputeTargets(const FDmAudioContext& C)
		{
			const float Z = 1.f - Sat01(C.Altitude01);            // zoom01 (2.1)
			const float Day = Sat01(C.Day), Night = 1.f - Day;
			const float Storm = Sat01(C.Storm);
			const float Calm = (1.f - Storm) * (1.f - 0.6f * Sat01(C.Rain));
			DayNow = Day; Ctx = C;
			auto& T = Target;
			// space hum < 0.12; -6 dB in high orbit; gone by the clouds
			T[int32(EDmLayer::Space)] = (1.f - 0.5f * SStep(0.08f, 0.16f, Z)) * (1.f - SStep(0.28f, 0.42f, Z)) + (C.Phase <= 0 ? 0.1f : 0.f);
			T[int32(EDmLayer::HighAir)] = SStep(0.08f, 0.18f, Z) * (1.f - SStep(0.32f, 0.48f, Z));
			// wind is the main sound in the clouds (0.3-0.5) and drops 8 dB over the land
			const float Clouds = SStep(0.22f, 0.34f, Z);
			const float WindBase = Clouds * (1.f - 0.6f * SStep(0.46f, 0.56f, Z));
			const float Land = SStep(0.42f, 0.54f, Z);
			T[int32(EDmLayer::Wind)] = Sat01(WindBase * (1.f + 0.6f * Sat01(C.Mountain) + 0.4f * Sat01(C.Cold)) + Storm * 0.5f * Clouds);
			WindStorm = Storm;
			WhooshAmt = Clouds * (1.f - SStep(0.46f, 0.54f, Z));
			// the land: biome beds at full in the landscape, -4 dB in a town
			// the town zone: zoom01 above ~0.7 (the camera within ~5 km of a settlement; the 2.1 table says 0.8 on its own scale,
			// the live game's town views sit at 3-6 km = zoom01 0.66-0.75 on the director's altitude scale)
			const float TownZone = SStep(0.6f, 0.7f, Z) * Sat01(C.Town * 1.3f);
			const float B = Land * (1.f - 0.37f * TownZone);
			T[int32(EDmLayer::Surf)] = Sat01(C.Coast * 1.15f) * B;
			T[int32(EDmLayer::Sea)] = Sat01(C.Sea) * (1.f - 0.6f * Sat01(C.Coast)) * B * (0.71f + 0.29f * Day);
			BirdForest = Sat01(C.Forest); BirdRain = Sat01(C.Rainforest); BirdOpen = Sat01(C.Open); BirdWet = Sat01(C.Wetland);
			T[int32(EDmLayer::Birds)] = Sat01((C.Forest + 1.2f * C.Rainforest + 0.35f * C.Open + 0.5f * C.Wetland) * std::pow(Day, 1.5f) * Calm * 1.2f) * B;
			T[int32(EDmLayer::Crickets)] = Sat01((0.8f * C.Forest + C.Open + 0.4f * C.Rainforest) * std::pow(Night, 1.5f) * Calm * (1.f - Sat01(C.Cold)) * 1.2f) * B;
			T[int32(EDmLayer::Leaves)] = Sat01(C.Forest * 1.2f + 0.6f * C.Rainforest) * B * (0.6f + 0.4f * Storm);
			T[int32(EDmLayer::Grass)] = Sat01(C.Open * 1.1f) * B;
			T[int32(EDmLayer::Desert)] = Sat01(C.Desert * 1.1f) * B * (0.4f + 0.6f * Day);       // the desert night: wind at -8 dB
			T[int32(EDmLayer::Cold)] = Sat01(C.Cold * 1.1f) * B;
			T[int32(EDmLayer::Rockfall)] = Sat01(C.Mountain) * B * (1.f - 0.37f * Night);
			T[int32(EDmLayer::Wetland)] = Sat01(C.Wetland * 1.2f) * B;
			T[int32(EDmLayer::Rainforest)] = Sat01(C.Rainforest * 1.2f) * B;
			RainAmt = Sat01(FMath::Max(C.Rain, Storm * 0.9f));
			T[int32(EDmLayer::Rain)] = RainAmt * SStep(0.2f, 0.4f, Z);
			T[int32(EDmLayer::Fire)] = Sat01(C.Fire * 1.5f) * Land;
			T[int32(EDmLayer::Lava)] = FMath::Max(Sat01(C.Lava * 1.5f) * Land, C.Phase == 0 ? 0.85f : 0.f);
			TownAge = FMath::Clamp(C.TownAge >= 0 ? C.TownAge : C.Age, 0, 6);
			const float Town = TownZone * (0.55f + 0.45f * Day);
			T[int32(EDmLayer::TownEarly)] = TownAge <= 1 ? Town : 0.f;
			T[int32(EDmLayer::TownMiddle)] = (TownAge == 2 || TownAge == 3) ? Town : 0.f;
			T[int32(EDmLayer::TownIndustry)] = TownAge == 4 ? Town : 0.f;
			T[int32(EDmLayer::TownModern)] = TownAge >= 5 ? Town : 0.f;
			for (int32 I = 0; I < NumLayers; ++I) T[I] = Sat01(T[I]);

			// the ending of silence: everything but the wind goes; then the wind fades to -60 dB by 24 s; 4 s of true silence
			if (EndT >= 0.f)
			{
				const float W = EndT < 16.f ? FMath::Max(0.5f, T[int32(EDmLayer::Wind)]) : (EndT < 24.f ? 0.5f * FMath::Square(1.f - (EndT - 16.f) / 8.f) : 0.f);
				for (int32 I = 0; I < NumLayers; ++I) T[I] = 0.f;
				T[int32(EDmLayer::Wind)] = W;
			}

			// at most 6 beds: the quietest are dropped
			int32 Order[NumLayers];
			for (int32 I = 0; I < NumLayers; ++I) Order[I] = I;
			for (int32 I = 1; I < NumLayers; ++I)
				for (int32 J = I; J > 0 && T[Order[J]] > T[Order[J - 1]]; --J) Swap(Order[J], Order[J - 1]);
			Beds = 0;
			for (int32 I = 0; I < NumLayers; ++I)
			{
				if (T[Order[I]] < 0.01f) { T[Order[I]] = 0.f; continue; }
				if (Beds >= MaxBeds) T[Order[I]] = 0.f; else ++Beds;
			}
			if (bSnap) { for (int32 I = 0; I < NumLayers; ++I) Gain[I] = Target[I]; bSnap = false; }
		}

		void Render(float* L, float* R2, int32 N, float BlockSec)
		{
			const float Coef = SmoothCoef(EndT >= 0.f ? 0.6f : 1.5f, 1.f / BlockSec);   // tau 1.5 s: beds never pop
			if (EndT >= 0.f) EndT += BlockSec;
			for (int32 I = 0; I < NumLayers; ++I)
			{
				const float G0 = Gain[I];
				Gain[I] += Coef * (Target[I] - Gain[I]);
				if (G0 < 1e-4f && Gain[I] < 1e-4f) { KeepTimers(EDmLayer(I), BlockSec); continue; }
				FMemory::Memzero(TmpL, sizeof(float) * N);
				FMemory::Memzero(TmpR, sizeof(float) * N);
				switch (EDmLayer(I))
				{
				case EDmLayer::Space: RenderSpace(N); break;
				case EDmLayer::HighAir: RenderHighAir(N); break;
				case EDmLayer::Wind: RenderWind(N); break;
				case EDmLayer::Surf: RenderSurf(N); SurfHighPass(N); break;
				case EDmLayer::Sea: RenderSea(N); break;
				case EDmLayer::Birds: RenderBirds(N); break;
				case EDmLayer::Crickets: RenderCrickets(N); break;
				case EDmLayer::Rain: RenderRain(N); break;
				case EDmLayer::Fire: RenderFire(N); break;
				case EDmLayer::Lava: RenderLava(N); break;
				case EDmLayer::TownEarly: case EDmLayer::TownMiddle: case EDmLayer::TownIndustry: case EDmLayer::TownModern: RenderTown(N); break;
				case EDmLayer::Leaves: RenderLeaves(N); break;
				case EDmLayer::Grass: RenderGrass(N); break;
				case EDmLayer::Desert: RenderDesert(N); break;
				case EDmLayer::Cold: RenderCold(N); break;
				case EDmLayer::Rockfall: RenderRockfall(N); break;
				case EDmLayer::Wetland: RenderWetland(N); break;
				case EDmLayer::Rainforest: RenderRainforest(N); break;
				default: break;
				}
				const float Trim = DbToGain(GBedTrimDb[I]);
				const float G1 = Gain[I] * Trim;
				const float Step = (G1 - G0 * Trim) / float(N);
				float G = G0 * Trim;
				for (int32 K = 0; K < N; ++K) { G += Step; L[K] += TmpL[K] * G; R2[K] += TmpR[K] * G; }
			}
		}

	private:
		float Sr = 48000.f, InvSr = 1.f / 48000.f;
		FRng R;
		bool bSnap = true;
		float RootHz = 73.4f;
		float TmpL[MaxBlock] = {}, TmpR[MaxBlock] = {};
		FDmAudioContext Ctx;
		float DayNow = 1.f, WindStorm = 0.f, WhooshAmt = 0.f, RainAmt = 0.f;
		float BirdForest = 0.f, BirdRain = 0.f, BirdOpen = 0.f, BirdWet = 0.f;
		int32 TownAge = 0;

		FPink SpPink[2]; FSvf SpLp[2]; FHp1 SpHp[2]; FDrift SpDrift[2]; float SpPh[4] = {}; float SpNow[2] = { 0.f, 0.f }; int32 SpCtrl = 0;
		FPink HaPink[2]; FSvf HaBp[2]; FDrift HaDrift; int32 HaCtrl = 0;
		FPink WPink[2]; FBrown WBrown[2]; FSvf WBp[2]; FDrift WDrift; int32 WCtrl = 0;
		float GustT = 0.f, GustDur = 0.f, GustNext = 3.f, GustNow = 0.f;
		FSvf WhBp[2]; FPink WhPk[2]; float WhooshT = -1.f, WhooshNext = 5.f;
		FPink LfPk[2]; FSvf LfBp[2]; FDrift LfDrift, LfDrift2; float LfNow = 0.5f; int32 LfCtrl = 0;
		FPink GrPk[2]; FSvf GrBp[2]; FDrift GrDrift; float GrNow = 0.5f; int32 GrCtrl = 0;
		FSurfWave Waves[2];
		FHp1 SurfHp[2];
		FBrown SeaBr[2]; FSvf SeaLp[2]; float SeaHz = 0.1f, SeaPh = 0.f;
		FChirp Chirps[6]; float SpeciesTimer[5] = {};
		FCricket Crk[3]; FPing Owl; float OwlTimer = 20.f;
		FPink RnPk[2]; FSvf RnHp[2], RnLp[2]; FBrown RnBr[2]; FSvf RnBody[2]; FGrainA Drops[16];
		FBrown FireBr[2]; FSvf FireLp[2]; FBrown RoarBr[2]; FSvf RoarLp[2]; FHp1 RoarHp[2]; FDrift FireDrift; FGrainA Pops[10]; float FireNow = 0.5f, PopNext = 1.f; int32 FireCtrl = 0;
		FBrown LavaBr[2]; FSvf LavaBp[2]; FDrift LavaDrift; FGrainA Blorps[4]; float LavaNow = 0.5f; int32 LavaCtrl = 0;
		FBrown DsBr[2]; FSvf DsBp[2]; FDrift DsDrift; FSvf SandHp[2]; float DsNow = 0.5f, SandT = -1.f, SandNext = 5.f; int32 DsCtrl = 0;
		FPink CoPk[2]; FSvf CoBp[2]; FDrift CoDrift; float CoNow = 0.5f; int32 CoCtrl = 0; FPing Creak, Groan; float CreakNext = 10.f, GroanNext = 30.f;
		FGrainA Rocks[6]; float RockNext = 5.f; int32 RockLeft = 0; float RockGap = 0.f;
		FGrainA Bubbles[6];
		FCricket Frogs[6]; FSvf FrogBp[6];
		FGrainA Drips[6]; float InsPh = 0.f, InsAm = 0.f; FHp1 InsHp[2];
		// town
		FBurst Voices[2]; float VoiceNext = 4.f; int32 VoicePair = 0;
		FPing Goat, Church, Gull, Whistle, Horn, Hammer[3]; float GoatNext = 5.f, ChurchNext = 20.f, GullNext = 5.f, WhistleNext = 30.f, HornNext = 10.f, HammerNext = 5.f, HammerGap = 0.f; int32 HammerLeft = 0;
		FPink MachPk[2]; FSvf MachBp[2]; FHp1 MachHp[2]; float MachPh = 0.f, HumPh[3] = {};
		FBrown TrBr[2]; FSvf TrafLp[2]; FDrift TrafDrift; float TrafNow = 0.5f; int32 TrafCtrl = 0;
		FPink MagPk[2]; FSvf MagBp[2]; float MaglevT = -1.f, MaglevNext = 8.f;
		FGrainA TownSparks[4];

		/** Timers keep running for silent beds so a bed that fades in does not fire everything at once. */
		void KeepTimers(EDmLayer L, float Sec) { (void)L; (void)Sec; }

		static float Pan01(float P, bool bRight) { float Gl, Gr; PanGains(P, Gl, Gr); return bRight ? Gr : Gl; }

		void StartPing(FPing& P, float Hz, const float* Ratios, const float* Amps, const float* Decays, float Pan, float Attack = 0.002f, float Dur = 10.f)
		{
			P.bOn = true; P.Pan = Pan; P.T = 0.f; P.Attack = Attack; P.Dur = Dur; P.Fm = 0.f; P.Am = 0.f; P.Hold = 0.f;
			for (int32 I = 0; I < 3; ++I) { P.Ph[I] = R.Uni(); P.Inc[I] = Hz * Ratios[I] * InvSr; P.Amp[I] = Amps[I]; P.Mul[I] = DecayMul(Decays[I], Sr); P.Glide[I] = 1.f; }
		}
		void RenderPing(FPing& P, int32 N, float Level)
		{
			if (!P.bOn) return;
			float Gl, Gr;
			PanGains(P.Pan, Gl, Gr);
			for (int32 K = 0; K < N; ++K)
			{
				P.T += InvSr;
				if (P.T > P.Dur) { P.bOn = false; break; }
				float Fm = 0.f;
				if (P.Fm > 0.f) { P.FmPh = Wrap01(P.FmPh + P.Inc[0] * P.FmRatio); Fm = P.Fm * 0.159155f * Sin01(P.FmPh); P.Fm *= P.FmMul; }
				float V = 0.f;
				for (int32 I = 0; I < 3; ++I)
				{
					P.Inc[I] *= P.Glide[I];
					P.Ph[I] = Wrap01(P.Ph[I] + P.Inc[I]);
					V += Sin01(P.Ph[I] + (I == 0 ? Fm : 0.f)) * P.Amp[I];
					if (P.T > P.Attack + P.Hold) P.Amp[I] *= P.Mul[I];
				}
				if (P.Am > 0.f) { P.AmPh = Wrap01(P.AmPh + P.AmHz * InvSr); V *= 1.f - P.Am * (0.5f + 0.5f * Sin01(P.AmPh)); }
				V *= Level * Sat01(P.T / P.Attack);
				TmpL[K] += V * Gl; TmpR[K] += V * Gr;
			}
			if (P.Amp[0] < 1e-5f && P.Amp[1] < 1e-5f && P.Amp[2] < 1e-5f) P.bOn = false;
		}
		void SpawnGrain(FGrainA* Pool, int32 Num, float Hz, float Amp, float Decay, float Pan, float Glide = 1.f, float Band = 0.f)
		{
			for (int32 I = 0; I < Num; ++I)
			{
				FGrainA& G = Pool[I];
				if (G.bOn) continue;
				G.bOn = true; G.Ph = R.Uni(); G.Inc = Hz * InvSr; G.Amp = Amp; G.Mul = DecayMul(Decay, Sr); G.Pan = Pan; G.Glide = Glide;
				if (Band > 0.f) { G.Lp.Reset(); G.Lp.Set(Band * 1.5f, Sr); G.Hp.Set(Band * 0.6f, Sr); }
				return;
			}
		}
		/** Sine grains (Band 0) or band-passed noise grains (Band > 0). */
		void RenderGrains(FGrainA* Pool, int32 Num, int32 N, bool bNoise)
		{
			for (int32 I = 0; I < Num; ++I)
			{
				FGrainA& G = Pool[I];
				if (!G.bOn) continue;
				float Gl, Gr;
				PanGains(G.Pan, Gl, Gr);
				for (int32 K = 0; K < N; ++K)
				{
					float V;
					if (bNoise) V = G.Lp.Tick(G.Hp.Tick(R.Bi())) * 2.f;
					else { G.Ph = Wrap01(G.Ph + G.Inc); G.Inc *= G.Glide; V = Sin01(G.Ph); }
					V *= G.Amp;
					G.Amp *= G.Mul;
					TmpL[K] += V * Gl; TmpR[K] += V * Gr;
				}
				if (G.Amp < 1e-5f) G.bOn = false;
			}
		}

		void RenderSpace(int32 N)
		{
			// two sines at 55 and 82.5 Hz (a 5th; the 55 Hz one 6 dB down for the sub-bass rule), each drifting +/- 3 dB at
			// 0.04 Hz, with soft 2nd harmonics; pink noise below 300 Hz at -30 dB; everything above 40 Hz
			for (int32 K = 0; K < N; ++K)
			{
				if (++SpCtrl >= 32) { SpCtrl = 0; SpNow[0] = SpDrift[0].Tick(R); SpNow[1] = SpDrift[1].Tick(R); }
				SpPh[0] = Wrap01(SpPh[0] + 55.f * InvSr); SpPh[1] = Wrap01(SpPh[1] + 82.5f * InvSr);
				SpPh[2] = Wrap01(SpPh[2] + 110.f * InvSr); SpPh[3] = Wrap01(SpPh[3] + 165.f * InvSr);
				const float A = 1.f + 0.41f * SpNow[0], B = 1.f + 0.41f * SpNow[1];
				const float Hum = (0.4f * A * (Sin01(SpPh[0]) + 0.2f * Sin01(SpPh[2])) + B * (Sin01(SpPh[1]) + 0.2f * Sin01(SpPh[3]))) * 0.03f;
				for (int32 C = 0; C < 2; ++C)
				{
					const float P = SpLp[C].Low(SpPink[C].Tick(R.Bi())) * 0.03f * 0.6f;
					(C ? TmpR : TmpL)[K] = SpHp[C].Tick(Hum + P);
				}
			}
		}

		void RenderHighAir(int32 N)
		{
			// wind from a great height: pink noise through a band at 700 Hz (Q 0.7), the centre wandering 500-900 Hz
			for (int32 K = 0; K < N; ++K)
			{
				if (++HaCtrl >= 32)
				{
					HaCtrl = 0;
					const float D = HaDrift.Tick(R);
					HaBp[0].Set(700.f + 200.f * D, 0.7f, Sr); HaBp[1].Set(700.f - 180.f * D, 0.7f, Sr);
				}
				for (int32 C = 0; C < 2; ++C) (C ? TmpR : TmpL)[K] = HaBp[C].Band(HaPink[C].Tick(R.Bi())) * 0.09f;
			}
		}

		void RenderWind(int32 N)
		{
			// brown + pink noise (1 : 0.4) through a band wandering 250-700 Hz; gusts of 2-6 s every 5-15 s at +4 dB (more in
			// storms and on mountains); a cloud whoosh (1.2 kHz, Q 1, a 3 s swell) as clouds pass in the cloud band
			const float Bs = float(N) * InvSr;
			if (GustDur > 0.f) { GustT += Bs; if (GustT >= GustDur) { GustDur = 0.f; GustNext = R.Range(5.f, 15.f) * (1.f - 0.5f * WindStorm); } }
			else { GustNext -= Bs; if (GustNext <= 0.f) { GustT = 0.f; GustDur = R.Range(2.f, 6.f); } }
			const float GustAmt = GustDur > 0.f ? FMath::Square(Sin01(0.5f * GustT / GustDur)) : 0.f;
			const float GustG = 1.f + (0.58f + 0.5f * WindStorm + 0.4f * Sat01(Ctx.Mountain)) * GustAmt;
			if (WhooshT >= 0.f) { WhooshT += Bs; if (WhooshT > 3.f) WhooshT = -1.f; }
			else if (WhooshAmt > 0.05f) { WhooshNext -= Bs; if (WhooshNext <= 0.f) { WhooshT = 0.f; WhooshNext = R.Range(8.f, 20.f); } }
			const float Wh = WhooshT >= 0.f ? FMath::Square(Sin01(0.5f * WhooshT / 3.f)) * WhooshAmt : 0.f;
			for (int32 K = 0; K < N; ++K)
			{
				if (++WCtrl >= 32)
				{
					WCtrl = 0;
					const float D = 0.5f + 0.5f * WDrift.Tick(R);
					const float Hz = 250.f + 450.f * D * (0.7f + 0.3f * GustAmt);
					WBp[0].Set(Hz, 0.9f, Sr); WBp[1].Set(Hz * 1.06f, 0.9f, Sr);
				}
				for (int32 C = 0; C < 2; ++C)
				{
					const float W = R.Bi();
					const float Src = WBrown[C].Tick(W) + 0.4f * WPink[C].Tick(W) * 3.f;
					float V = WBp[C].Band(Src) * 0.05f * GustG;
					if (Wh > 0.f) V += WhBp[C].Band(WhPk[C].Tick(R.Bi())) * 0.12f * Wh;
					(C ? TmpR : TmpL)[K] = V;
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
					if (W.T >= W.Period) { W.T -= W.Period; W.Period = R.Range(6.f, 10.f); W.Size = R.Range(0.6f, 1.f); W.Pan = -W.Pan; PanGains(W.Pan, Gl, Gr); }
					const float U = W.T / W.Period;
					// swell (a 1.5 s rise), the break at 58 %, the wash running up and back; every term continuous across the cycle
					// (the verifier's click fix: probe-surf measures the 20 ms level rise)
					const float Swell = SStep(0.f, 0.58f, U) * (1.f - SStep(0.58f, 0.66f, U));
					const float Fade = 1.f - SStep(0.9f, 1.f, U);
					const float Tb = (U - 0.58f) * W.Period;
					const float Crash = (Tb < 0.f ? SStep(-0.3f, 0.f, Tb) : std::exp(-Tb * 1.6f)) * Fade;
					const float Wash = std::exp(-(U - 0.6f) * W.Period * 0.45f) * SStep(0.58f, 0.7f, U) * Fade;
					// (wave 4b verify) 2.2: the hiss tail's low-pass falls from 4 kHz to 1 kHz (it stopped at 1.9 kHz, so the
					// surf had almost nothing above 4 kHz: 0.2 % of the landscape bed against the preview's 10 %)
					if ((K & 31) == 0) W.Lp.Set(300.f + 3700.f * Crash + 900.f * Wash, 0.7f, Sr);
					const float Wh = R.Bi();
					const float Src = W.Br.Tick(Wh) * 0.7f + W.Pk.Tick(Wh) * 1.4f;
					const float Body = W.Lp.Low(Src) * (0.25f * Swell + 0.9f * Crash + 0.45f * Wash);
					const float Fizz = W.Hiss.Band(Wh) * 0.1f * (Crash * 0.6f + Wash);
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
			// leaf rustle: noise through a band at 3 kHz with gusts
			for (int32 K = 0; K < N; ++K)
			{
				if (++LfCtrl >= 32)
				{
					LfCtrl = 0;
					const float A1 = 0.5f + 0.5f * LfDrift.Tick(R), A2 = 0.5f + 0.5f * LfDrift2.Tick(R);
					LfNow = 0.2f + 0.8f * A1 * A1 * (0.5f + 0.5f * A2);
				}
				for (int32 C = 0; C < 2; ++C) (C ? TmpR : TmpL)[K] = LfBp[C].Band(LfPk[C].Tick(R.Bi())) * 0.06f * LfNow;
			}
		}

		void RenderGrass(int32 N)
		{
			// wind through grass: pink noise through a band at 2 kHz with slow gusts
			for (int32 K = 0; K < N; ++K)
			{
				if (++GrCtrl >= 32) { GrCtrl = 0; const float A = 0.5f + 0.5f * GrDrift.Tick(R); GrNow = 0.25f + 0.75f * A * A; }
				for (int32 C = 0; C < 2; ++C) (C ? TmpR : TmpL)[K] = GrBp[C].Band(GrPk[C].Tick(R.Bi())) * 0.05f * GrNow;
			}
		}

		void RenderSea(int32 N)
		{
			// low swell: brown noise below 400 Hz with a 0.08-0.12 Hz wave (+/- 5 dB)
			for (int32 K = 0; K < N; ++K)
			{
				SeaPh = Wrap01(SeaPh + SeaHz * InvSr);
				const float Wv = DbToGain(5.f * Sin01(SeaPh));
				if (SeaPh < InvSr * SeaHz * 2.f) SeaHz = R.Range(0.08f, 0.12f);
				for (int32 C = 0; C < 2; ++C)
					(C ? TmpR : TmpL)[K] = SeaLp[C].Low(SeaBr[C].Tick(R.Bi())) * 0.16f * Wv;
			}
		}

		void StartChirp(FChirp& Ch, int32 Species)
		{
			Ch.bOn = true;
			Ch.T = 0.f; Ch.bGap = false; Ch.Ph = 0.f; Ch.AmPh = 0.f; Ch.NoteIdx = 0;
			Ch.Pan = R.Range(-0.75f, 0.75f);
			Ch.Amp = R.Range(0.4f, 1.f);
			Ch.Species = Species;
			switch (Species)
			{
			case 0: Ch.NotesLeft = 3; Ch.NoteDur = 0.12f; Ch.Gap = 0.06f; Ch.F0 = 4000.f; Ch.F1 = 3000.f; break;          // (a) a falling glide, x3
			case 1: Ch.NotesLeft = 2; Ch.NoteDur = 0.2f; Ch.Gap = 0.05f; Ch.F0 = 2200.f; Ch.F1 = 2200.f; break;           // (b) a two-note whistle
			case 2: Ch.NotesLeft = 1; Ch.NoteDur = 0.6f; Ch.Gap = 0.05f; Ch.F0 = 3500.f; Ch.F1 = 3500.f; break;           // (c) a rapid trill
			case 3: Ch.NotesLeft = 2 + R.Int(2); Ch.NoteDur = 0.35f; Ch.Gap = 0.18f; Ch.F0 = 600.f; Ch.F1 = 560.f; Ch.Amp *= 1.6f; break;   // the dove
			default: Ch.NotesLeft = 3; Ch.NoteDur = 0.12f; Ch.Gap = 0.08f; Ch.F0 = 2000.f; Ch.F1 = 1500.f; break;         // wading birds: (a) an octave lower
			}
			const float J = R.Range(0.95f, 1.05f);
			Ch.F0 *= J; Ch.F1 *= J;
		}

		void RenderBirds(int32 N)
		{
			// three species (a falling triple glide, a two-note whistle, a trill), each calling every 4-12 s; the rainforest is
			// denser and adds a dove; grassland has only (b), sparse; wetland: (a) an octave lower
			const float Bs = float(N) * InvSr;
			const float Wt[5] = { BirdForest + 1.5f * BirdRain, BirdForest + 1.5f * BirdRain + 0.6f * BirdOpen, BirdForest + 1.5f * BirdRain, 1.5f * BirdRain + 0.4f * BirdForest, BirdWet };
			for (int32 S = 0; S < 5; ++S)
			{
				if (Wt[S] < 0.05f) continue;
				SpeciesTimer[S] -= Bs * FMath::Min(2.f, Wt[S]);
				if (SpeciesTimer[S] <= 0.f)
				{
					SpeciesTimer[S] = R.Range(4.f, 12.f);
					for (FChirp& Ch : Chirps) if (!Ch.bOn) { StartChirp(Ch, S); break; }
				}
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
							Ch.bGap = false; Ch.T = 0.f; ++Ch.NoteIdx;
							if (--Ch.NotesLeft <= 0) { Ch.bOn = false; break; }
							if (Ch.Species == 1) { Ch.F0 = Ch.F1 = 2900.f * R.Range(0.97f, 1.03f); }
						}
						continue;
					}
					if (Ch.T >= Ch.NoteDur) { Ch.bGap = true; Ch.T = 0.f; continue; }
					const float U = Ch.T / Ch.NoteDur;
					const float Hz = Ch.F0 + (Ch.F1 - Ch.F0) * U;
					Ch.Ph = Wrap01(Ch.Ph + Hz * InvSr);
					float Env = Sin01(U * 0.5f);
					Env *= Env;
					if (Ch.Species == 2) { Ch.AmPh = Wrap01(Ch.AmPh + 25.f * InvSr); Env *= 0.5f + 0.5f * Sin01(Ch.AmPh); }
					const float V = Sin01(Ch.Ph) * Env * Ch.Amp * (Ch.Species == 3 ? 0.05f : 0.035f);
					TmpL[K] += V * Gl; TmpR[K] += V * Gr;
				}
			}
		}

		void RenderCrickets(int32 N)
		{
			// crickets: a sine at 4.5 kHz with AM at 30 Hz, 3 pulses of 50 ms every 0.8 s, at -24 dB; an owl every 30-60 s
			// (380 Hz, two hoots of 300 ms with a 15 % pitch drop)
			for (FCricket& C : Crk)
			{
				float Gl, Gr;
				PanGains(C.Pan, Gl, Gr);
				for (int32 K = 0; K < N; ++K)
				{
					C.Timer -= InvSr;
					if (C.Timer <= 0.f) { C.Timer = R.Range(0.75f, 0.85f); C.Pulses = 3; C.PulseT = 0.f; }
					float Env = 0.f;
					if (C.Pulses > 0)
					{
						C.PulseT += InvSr;
						const float U = C.PulseT / 0.05f;
						if (U < 1.f) { const float E = Sin01(U * 0.5f); Env = E * E; }
						if (C.PulseT >= 0.075f) { C.PulseT = 0.f; --C.Pulses; }
					}
					C.Ph = Wrap01(C.Ph + C.Hz * InvSr);
					C.AmPh = Wrap01(C.AmPh + 30.f * InvSr);
					const float V = Sin01(C.Ph) * (0.5f + 0.5f * Sin01(C.AmPh)) * Env * C.Amp * 0.03f;
					TmpL[K] += V * Gl; TmpR[K] += V * Gr;
				}
			}
			OwlTimer -= float(N) * InvSr;
			if (OwlTimer <= 0.f && !Owl.bOn)
			{
				OwlTimer = R.Range(30.f, 60.f);
				static const float Ra[3] = { 1.f, 2.f, 1.f }, Am[3] = { 0.05f, 0.008f, 0.f }, De[3] = { 0.35f, 0.25f, 0.1f };
				StartPing(Owl, 380.f, Ra, Am, De, R.Range(-0.6f, 0.6f), 0.04f, 0.9f);
				Owl.Hold = 0.2f;
				for (float& G : Owl.Glide) G = std::pow(0.85f, 1.f / (0.3f * Sr));
				OwlSecond = 0.45f;
			}
			if (OwlSecond > 0.f)
			{
				OwlSecond -= float(N) * InvSr;
				if (OwlSecond <= 0.f && !Owl.bOn)
				{
					static const float Ra[3] = { 1.f, 2.f, 1.f }, Am[3] = { 0.045f, 0.007f, 0.f }, De[3] = { 0.35f, 0.25f, 0.1f };
					StartPing(Owl, 375.f, Ra, Am, De, Owl.Pan, 0.04f, 0.9f);
					Owl.Hold = 0.2f;
					for (float& G : Owl.Glide) G = std::pow(0.85f, 1.f / (0.3f * Sr));
				}
			}
			RenderPing(Owl, N, 1.f);
		}
		float OwlSecond = -1.f;

		void RenderRain(int32 N)
		{
			// pink noise between 500 Hz and 8 kHz, a drop layer (2-5 kHz, 60-300 drops a second with intensity), and at heavy
			// rain a low body (brown noise below 300 Hz)
			const float Density = 60.f + 240.f * RainAmt;
			const float Heavy = SStep(0.6f, 0.9f, RainAmt);
			for (int32 K = 0; K < N; ++K)
			{
				if (R.Uni() < Density * InvSr) SpawnGrain(Drops, 16, R.Range(2000.f, 5000.f), R.Range(0.2f, 1.f) * 0.02f, R.Range(0.004f, 0.01f), R.Bi() * 0.8f);
				for (int32 C = 0; C < 2; ++C)
				{
					const float W = R.Bi();
					const float V = RnLp[C].Low(RnHp[C].High(RnPk[C].Tick(W))) * 0.11f + RnBody[C].Low(RnBr[C].Tick(W)) * 0.12f * Heavy;
					(C ? TmpR : TmpL)[K] = V;
				}
			}
			RenderGrains(Drops, 16, N, false);
		}

		void RenderFire(int32 N)
		{
			// crackle (20-60 impulses a second, 1.5-5 kHz) over a soft brown bed, a low roar (below 200 Hz, above 50 Hz), pops
			const float Bs = float(N) * InvSr;
			PopNext -= Bs;
			if (PopNext <= 0.f) { PopNext = R.Range(1.f, 3.f); SpawnGrain(Pops, 10, 0.f, 0.12f, 0.01f, R.Bi() * 0.6f, 1.f, 800.f); }
			for (int32 K = 0; K < N; ++K)
			{
				if (++FireCtrl >= 32) { FireCtrl = 0; FireNow = 0.6f + 0.4f * FireDrift.Tick(R); }
				if (R.Uni() < 40.f * FireNow * InvSr) SpawnGrain(Pops, 10, 0.f, R.Range(0.2f, 1.f) * 0.06f, R.Range(0.001f, 0.003f), R.Bi() * 0.7f, 1.f, R.Range(1500.f, 5000.f));
				for (int32 C = 0; C < 2; ++C)
				{
					const float W = R.Bi();
					const float V = FireLp[C].Low(FireBr[C].Tick(W)) * 0.1f * FireNow + RoarHp[C].Tick(RoarLp[C].Low(RoarBr[C].Tick(W))) * 0.12f;
					(C ? TmpR : TmpL)[K] = V;
				}
			}
			RenderGrains(Pops, 10, N, true);
		}

		void RenderLava(int32 N)
		{
			for (int32 K = 0; K < N; ++K)
			{
				if (++LavaCtrl >= 32) { LavaCtrl = 0; LavaNow = 0.6f + 0.4f * LavaDrift.Tick(R); }
				if (R.Uni() < 3.f * InvSr) SpawnGrain(Blorps, 4, R.Range(75.f, 150.f), R.Range(0.4f, 1.f) * 0.06f, R.Range(0.12f, 0.25f), R.Bi() * 0.6f, 1.f + R.Range(4.f, 9.f) * InvSr);
				for (int32 C = 0; C < 2; ++C)
					(C ? TmpR : TmpL)[K] = LavaBp[C].Band(LavaBr[C].Tick(R.Bi())) * 0.22f * LavaNow;
			}
			RenderGrains(Blorps, 4, N, false);
		}

		void RenderDesert(int32 N)
		{
			// very dry wind: brown noise through a band at 300 Hz; now and then a sand hiss (above 5 kHz, a 2 s swell, -26 dB)
			const float Bs = float(N) * InvSr;
			if (SandT >= 0.f) { SandT += Bs; if (SandT > 2.f) SandT = -1.f; }
			else { SandNext -= Bs; if (SandNext <= 0.f) { SandT = 0.f; SandNext = R.Range(6.f, 15.f); } }
			const float Sand = (SandT >= 0.f ? FMath::Square(Sin01(0.5f * SandT / 2.f)) : 0.f) * DayNow;
			for (int32 K = 0; K < N; ++K)
			{
				if (++DsCtrl >= 32) { DsCtrl = 0; const float A = 0.5f + 0.5f * DsDrift.Tick(R); DsNow = 0.4f + 0.6f * A; }
				for (int32 C = 0; C < 2; ++C)
				{
					const float W = R.Bi();
					float V = DsBp[C].Band(DsBr[C].Tick(W)) * 0.06f * DsNow;
					if (Sand > 0.f) V += SandHp[C].High(W) * 0.05f * 0.6f * Sand;
					(C ? TmpR : TmpL)[K] = V;
				}
			}
		}

		void RenderCold(int32 N)
		{
			// thin high wind (a band at 1.4 kHz, Q 3, whistling); ice creaks (120 Hz, 1.2 s, the pitch falling 20 %) every 20-60 s;
			// at night a deep ice groan (70 -> 50 Hz over 2 s, above 45 Hz) every 60-120 s
			const float Bs = float(N) * InvSr;
			CreakNext -= Bs;
			if (CreakNext <= 0.f && !Creak.bOn)
			{
				CreakNext = R.Range(20.f, 60.f);
				static const float Ra[3] = { 1.f, 2.03f, 3.1f }, Am[3] = { 0.035f, 0.02f, 0.012f }, De[3] = { 1.2f, 0.8f, 0.5f };
				StartPing(Creak, 120.f * R.Range(0.95f, 1.05f), Ra, Am, De, R.Range(-0.6f, 0.6f), 0.01f, 1.5f);
				Creak.Am = 0.6f; Creak.AmHz = 23.f;
				for (float& G : Creak.Glide) G = std::pow(0.8f, 1.f / (1.2f * Sr));
			}
			GroanNext -= Bs * (1.f - DayNow);
			if (GroanNext <= 0.f && !Groan.bOn)
			{
				GroanNext = R.Range(60.f, 120.f);
				static const float Ra[3] = { 1.f, 2.f, 3.f }, Am[3] = { 0.05f, 0.025f, 0.01f }, De[3] = { 2.5f, 2.f, 1.5f };
				StartPing(Groan, 70.f, Ra, Am, De, R.Range(-0.4f, 0.4f), 0.3f, 2.5f);
				for (float& G : Groan.Glide) G = std::pow(50.f / 70.f, 1.f / (2.f * Sr));
			}
			for (int32 K = 0; K < N; ++K)
			{
				if (++CoCtrl >= 32) { CoCtrl = 0; const float A = 0.5f + 0.5f * CoDrift.Tick(R); CoNow = 0.3f + 0.7f * A; }
				for (int32 C = 0; C < 2; ++C) (C ? TmpR : TmpL)[K] = CoBp[C].Band(CoPk[C].Tick(R.Bi())) * 0.035f * CoNow;
			}
			RenderPing(Creak, N, 1.f);
			RenderPing(Groan, N, 1.f);
		}

		void RenderRockfall(int32 N)
		{
			// a distant rockfall now and then: 3-6 low clicks (noise below 600 Hz, 10 ms) over 1 s
			const float Bs = float(N) * InvSr;
			if (RockLeft > 0)
			{
				RockGap -= Bs;
				if (RockGap <= 0.f) { --RockLeft; RockGap = R.Range(0.1f, 0.3f); SpawnGrain(Rocks, 6, 0.f, R.Range(0.3f, 1.f) * 0.12f, 0.01f, R.Bi() * 0.5f, 1.f, R.Range(250.f, 450.f)); }
			}
			else { RockNext -= Bs; if (RockNext <= 0.f) { RockNext = R.Range(8.f, 25.f); RockLeft = 3 + R.Int(4); RockGap = 0.f; } }
			RenderGrains(Rocks, 6, N, true);
		}

		void RenderWetland(int32 N)
		{
			// a gentle bubble pool (sine blips 500 Hz - 1.5 kHz gliding up, 20 ms); frogs at night, loud: the main night sound here
			for (int32 K = 0; K < N; ++K)
				if (R.Uni() < 4.f * InvSr) SpawnGrain(Bubbles, 6, R.Range(500.f, 1500.f), R.Range(0.3f, 1.f) * 0.03f, 0.02f, R.Bi() * 0.7f, 1.f + 30.f * InvSr);
			RenderGrains(Bubbles, 6, N, false);
			RenderFrogs(N, 1.f - DayNow, 1.f);
		}

		void RenderFrogs(int32 N, float Night, float Level)
		{
			// a 1.1 kHz square wave through a band, with AM at 18 Hz, in 150 ms croaks, in chorus
			if (Night < 0.05f) return;
			for (int32 I = 0; I < 6; ++I)
			{
				FCricket& F = Frogs[I];
				float Gl, Gr;
				PanGains(F.Pan, Gl, Gr);
				for (int32 K = 0; K < N; ++K)
				{
					F.Timer -= InvSr;
					if (F.Timer <= 0.f) { F.Timer = R.Range(0.6f, 2.f); F.PulseT = 0.f; F.Pulses = 1; }
					float Env = 0.f;
					if (F.Pulses > 0)
					{
						F.PulseT += InvSr;
						const float U = F.PulseT / 0.15f;
						if (U < 1.f) Env = Sin01(U * 0.5f); else F.Pulses = 0;
					}
					F.Ph = Wrap01(F.Ph + F.Hz * InvSr);
					F.AmPh = Wrap01(F.AmPh + 18.f * InvSr);
					const float Sq = BlepPulse(F.Ph, F.Hz * InvSr, 0.5f);
					const float V = FrogBp[I].Band(Sq) * (0.5f + 0.5f * Sin01(F.AmPh)) * Env * F.Amp * 0.025f * Night * Level;
					TmpL[K] += V * Gl; TmpR[K] += V * Gr;
				}
			}
		}

		void RenderRainforest(int32 N)
		{
			// an insect drone (5.5 kHz with AM at 90 Hz, -30 dB) and drips (plucked blips at 2-3 kHz, 50 ms, 3 a second) by day;
			// frogs in chorus at night
			const float Day = DayNow;
			for (int32 K = 0; K < N; ++K)
			{
				if (R.Uni() < 3.f * InvSr) SpawnGrain(Drips, 6, R.Range(2000.f, 3000.f), R.Range(0.3f, 1.f) * 0.025f, 0.05f, R.Bi() * 0.8f, 1.f - 6.f * InvSr);
				InsPh = Wrap01(InsPh + 5500.f * InvSr);
				InsAm = Wrap01(InsAm + 90.f * InvSr);
				const float Ins = Sin01(InsPh) * (0.5f + 0.5f * Sin01(InsAm)) * 0.004f * Day;
				TmpL[K] += Ins; TmpR[K] += Ins * 0.8f;
			}
			RenderGrains(Drips, 6, N, false);
			RenderFrogs(N, 1.f - Day, 0.8f);
		}

		void RenderTown(int32 N)
		{
			// "a town heard from a hill", by the nearest culture's age (2.1): sparse one-shots from a pool, each within its rate
			// limit; at night everything at half rate and no voices; ages 4-6 keep a faint night hum at -6 dB
			const float Bs = float(N) * InvSr;
			const float Night = 1.f - DayNow;
			const float Rate = 1.f - 0.5f * SStep(0.3f, 0.8f, Night);
			const float Dt = Bs * Rate;
			const int32 A = TownAge;
			// voices: a formant blip pair (600 Hz and 1.1 kHz on a 0.3 s noise burst), 2 every 8-20 s
			if (Night < 0.6f)
			{
				VoiceNext -= Dt;
				if (VoiceNext <= 0.f)
				{
					if (VoicePair <= 0) VoicePair = 2;
					--VoicePair;
					VoiceNext = VoicePair > 0 ? R.Range(0.35f, 0.9f) : R.Range(8.f, 20.f);
					for (FBurst& V : Voices) if (!V.bOn)
					{
						V.bOn = true; V.T = 0.f; V.Dur = 0.3f * R.Range(0.8f, 1.2f); V.Amp = R.Range(0.5f, 1.f); V.Pan = R.Range(-0.6f, 0.6f);
						const float J = R.Range(0.85f, 1.2f);
						V.A.Set(600.f * J, 4.f, Sr); V.B.Set(1100.f * J, 5.f, Sr);
						break;
					}
				}
			}
			for (FBurst& V : Voices)
			{
				if (!V.bOn) continue;
				float Gl, Gr;
				PanGains(V.Pan, Gl, Gr);
				for (int32 K = 0; K < N; ++K)
				{
					V.T += InvSr;
					if (V.T >= V.Dur) { V.bOn = false; break; }
					const float E = Sin01(0.5f * V.T / V.Dur);
					const float P = V.Pk.Tick(R.Bi());
					const float S = (V.A.Band(P) + 0.6f * V.B.Band(P)) * E * E * V.Amp * 0.06f;
					TmpL[K] += S * Gl; TmpR[K] += S * Gr;
				}
			}
			// a soft murmur of the town under the one-shots (people by day, the hum of work at night is the age's own bed below)
			if (A <= 5)
			{
				const float Mur = (1.f - SStep(0.3f, 0.8f, Night)) * (A >= 4 ? 0.6f : 1.f);
				if (Mur > 0.01f)
					for (int32 K = 0; K < N; ++K)
					{
						if (++MurCtrl >= 32) { MurCtrl = 0; MurNow = 0.5f + 0.5f * MurDrift.Tick(R); }
						for (int32 C = 0; C < 2; ++C)
						{
							const float P = MurPk[C].Tick(R.Bi());
							(C ? TmpR : TmpL)[K] += (MurA[C].Band(P) + 0.5f * MurB[C].Band(P)) * 0.035f * Mur * (0.5f + 0.5f * MurNow);
						}
					}
			}
			// Stone: fire crackle at -8 dB and a goat bell (FM 1 : 2.4, index 1, 1 ms / 400 ms at 1.6 kHz)
			if (A <= 3)
			{
				for (int32 K = 0; K < N; ++K)
					if (A <= 1 && R.Uni() < 18.f * InvSr) SpawnGrain(TownSparks, 4, 0.f, R.Range(0.2f, 1.f) * 0.025f, 0.002f, R.Bi() * 0.5f, 1.f, R.Range(1500.f, 5000.f));
				RenderGrains(TownSparks, 4, N, true);
				GoatNext -= Dt;
				if (GoatNext <= 0.f && !Goat.bOn)
				{
					GoatNext = R.Range(6.f, 15.f);
					static const float Ra[3] = { 1.f, 2.76f, 5.4f }, Am[3] = { 0.03f, 0.008f, 0.003f }, De[3] = { 0.4f, 0.25f, 0.12f };
					StartPing(Goat, 1600.f * R.Range(0.97f, 1.03f), Ra, Am, De, R.Range(-0.7f, 0.7f), 0.001f, 0.6f);
					Goat.Fm = 1.f; Goat.FmRatio = 2.4f; Goat.FmMul = DecayMul(0.3f, Sr);
				}
				RenderPing(Goat, N, 1.f);
			}
			// Bronze to Sail: hammering (a pluck at 900 Hz, 0.15 s, plus a click) in groups of 3-5 hits 300 ms apart, every 10-25 s
			if (A >= 1 && A <= 3)
			{
				if (HammerLeft > 0)
				{
					HammerGap -= Bs;
					if (HammerGap <= 0.f)
					{
						--HammerLeft; HammerGap = 0.3f * R.Range(0.9f, 1.1f);
						static const float Ra[3] = { 1.f, 2.1f, 3.3f }, Am[3] = { 0.03f, 0.014f, 0.006f }, De[3] = { 0.15f, 0.08f, 0.04f };
						for (FPing& H : Hammer) if (!H.bOn) { StartPing(H, 900.f * R.Range(0.98f, 1.02f), Ra, Am, De, HammerPan, 0.0005f, 0.25f); break; }
					}
				}
				else { HammerNext -= Dt; if (HammerNext <= 0.f) { HammerNext = R.Range(10.f, 25.f); HammerLeft = 3 + R.Int(3); HammerGap = 0.f; HammerPan = R.Range(-0.6f, 0.6f); } }
				for (FPing& H : Hammer) RenderPing(H, N, 1.f);
			}
			// Sail on: a church-like bell every 40-80 s (degree 1 two octaves up, -10 dB) (it returns in the Space age);
			// gulls near the coast (a glide 1.8 -> 2.4 kHz and back over 300 ms, with noise)
			if (A == 3 || A == 6)
			{
				ChurchNext -= Dt;
				if (ChurchNext <= 0.f && !Church.bOn)
				{
					ChurchNext = R.Range(40.f, 80.f);
					static const float Ra[3] = { 1.f, 2.76f, 2.f }, Am[3] = { 0.03f, 0.01f, 0.008f }, De[3] = { 3.f, 2.f, 2.5f };
					StartPing(Church, RootHz * 4.f, Ra, Am, De, R.Range(-0.4f, 0.4f), 0.002f, 4.f);
					Church.Fm = 2.f; Church.FmRatio = 3.5f; Church.FmMul = DecayMul(1.5f, Sr);
				}
				RenderPing(Church, N, 1.f);
			}
			if (A >= 3 && Ctx.Coast > 0.2f)
			{
				GullNext -= Dt;
				if (GullNext <= 0.f && !Gull.bOn)
				{
					GullNext = R.Range(5.f, 15.f);
					static const float Ra[3] = { 1.f, 2.f, 1.f }, Am[3] = { 0.02f, 0.004f, 0.f }, De[3] = { 0.5f, 0.3f, 0.1f };
					StartPing(Gull, 1800.f, Ra, Am, De, R.Range(-0.8f, 0.8f), 0.03f, 0.32f);
					Gull.Hold = 0.25f; GullT = 0.f;
				}
				if (Gull.bOn)
				{
					// up for 150 ms, back down for 150 ms
					GullT += Bs;
					const float G = GullT < 0.15f ? std::pow(2400.f / 1800.f, 1.f / (0.15f * Sr)) : std::pow(1800.f / 2400.f, 1.f / (0.15f * Sr));
					for (float& Gx : Gull.Glide) Gx = G;
					Gull.Am = 0.2f; Gull.AmHz = 130.f;
				}
				RenderPing(Gull, N, 1.f);
			}
			// Industry: a machine loop (a 2 Hz noise pulse at 400 Hz and a 50 Hz hum, above 45 Hz, -14 dB) and a train whistle
			// (600 and 715 Hz, 1.5 s, every 60-120 s)
			if (A == 4)
			{
				const float Lvl = (1.f - 0.5f * Night);
				for (int32 K = 0; K < N; ++K)
				{
					MachPh = Wrap01(MachPh + 2.f * InvSr);
					const float E = MachPh < 0.3f ? Sin01(MachPh / 0.3f * 0.5f) : 0.f;
					HumPh[0] = Wrap01(HumPh[0] + 50.f * InvSr); HumPh[1] = Wrap01(HumPh[1] + 100.f * InvSr);
					const float Hum = (Sin01(HumPh[0]) * 0.5f + Sin01(HumPh[1]) * 0.35f) * 0.012f;
					for (int32 C = 0; C < 2; ++C)
						(C ? TmpR : TmpL)[K] += MachHp[C].Tick(MachBp[C].Band(MachPk[C].Tick(R.Bi())) * E * E * 0.1f + Hum) * Lvl;
				}
				WhistleNext -= Dt;
				if (WhistleNext <= 0.f && !Whistle.bOn)
				{
					WhistleNext = R.Range(60.f, 120.f);
					static const float Ra[3] = { 1.f, 715.f / 600.f, 2.f }, Am[3] = { 0.012f, 0.01f, 0.002f }, De[3] = { 0.4f, 0.4f, 0.3f };
					StartPing(Whistle, 600.f, Ra, Am, De, R.Range(-0.8f, 0.8f), 0.08f, 1.9f);
					Whistle.Hold = 1.4f;
				}
				RenderPing(Whistle, N, 1.f);
			}
			// Atomic: a traffic hum (brown noise below 250 Hz, a 0.2 Hz swell) and a distant horn (350 Hz, 0.4 s, every 20-40 s);
			// Space: a clean electric hum (120 and 240 Hz, -24 dB) and a soft maglev whoosh (300 Hz -> 1.5 kHz and back, 2 s)
			if (A >= 5)
			{
				const float NightHum = 1.f - 0.5f * Night;
				for (int32 K = 0; K < N; ++K)
				{
					if (++TrafCtrl >= 32) { TrafCtrl = 0; TrafNow = 0.55f + 0.45f * TrafDrift.Tick(R); }
					float Hum = 0.f;
					if (A == 6)
					{
						HumPh[0] = Wrap01(HumPh[0] + 120.f * InvSr); HumPh[1] = Wrap01(HumPh[1] + 240.f * InvSr);
						Hum = (Sin01(HumPh[0]) + 0.5f * Sin01(HumPh[1])) * 0.0035f;
					}
					for (int32 C = 0; C < 2; ++C)
					{
						const float T = A == 5 ? TrafLp[C].Low(TrBr[C].Tick(R.Bi())) * 0.12f * TrafNow : 0.f;
						(C ? TmpR : TmpL)[K] += (T + Hum) * NightHum;
					}
				}
				if (A == 5)
				{
					HornNext -= Dt;
					if (HornNext <= 0.f && !Horn.bOn)
					{
						HornNext = R.Range(20.f, 40.f);
						static const float Ra[3] = { 1.f, 2.f, 3.f }, Am[3] = { 0.012f, 0.006f, 0.003f }, De[3] = { 0.3f, 0.25f, 0.2f };
						StartPing(Horn, 350.f, Ra, Am, De, R.Range(-0.8f, 0.8f), 0.03f, 0.6f);
						Horn.Hold = 0.35f;
					}
					RenderPing(Horn, N, 1.f);
				}
				else
				{
					if (MaglevT >= 0.f) { MaglevT += Bs; if (MaglevT > 2.f) MaglevT = -1.f; }
					else { MaglevNext -= Dt; if (MaglevNext <= 0.f) { MaglevT = 0.f; MaglevNext = R.Range(15.f, 30.f); } }
					if (MaglevT >= 0.f)
					{
						const float U = MaglevT / 2.f;
						const float Hz = 300.f * std::pow(5.f, Sin01(0.5f * U));
						MagBp[0].Set(Hz, 1.2f, Sr); MagBp[1].Set(Hz * 1.03f, 1.2f, Sr);
						const float E = FMath::Square(Sin01(0.5f * U)) * 0.04f;
						for (int32 K = 0; K < N; ++K)
							for (int32 C = 0; C < 2; ++C) (C ? TmpR : TmpL)[K] += MagBp[C].Band(MagPk[C].Tick(R.Bi())) * E;
					}
				}
			}
		}
		float HammerPan = 0.f, GullT = 0.f;
		FPink MurPk[2]; FSvf MurA[2], MurB[2]; FDrift MurDrift; float MurNow = 0.5f; int32 MurCtrl = 0;
	};

	struct FCmd
	{
		uint8 Kind = 0;      // 0 cue, 1 stinger, 2 suspend, 3 debug music note
		EDmCue Cue = EDmCue::Click;
		EDmStinger Sting = EDmStinger::Age;
		FDmCueParams P;
		bool bFlag = false;
		int32 I0 = 0, I1 = 0;
		float F0 = 0.f, F1 = 0.f, F2 = 0.f;
	};
	constexpr int32 MaxCmds = 128;
	constexpr int32 MaxLive = 24;      // 5.3: at most 24 one-shot voices
	constexpr int32 MaxSlots = 32;     // + room for voices fading out after being stolen
	constexpr int32 RateMax = 12;      // 5.3: at most 12 effect starts per second

	/** 5.2: who ducks what (dB, attack and release seconds). */
	void DuckOf(const FDmCueVoice& V, float& MusicDb, float& AmbDb, float& Att, float& Rel)
	{
		MusicDb = 0.f; AmbDb = 0.f; Att = 0.2f; Rel = 2.f;
		const float Strength = FMath::Min(1.f, V.VolIn);
		switch (V.Cue)
		{
		case EDmCue::Impact: MusicDb = -10.f; AmbDb = -6.f; Att = 0.05f; Rel = 3.f; break;
		case EDmCue::Wave: if (V.Variant == DmCueVar::WaveTsunami) { MusicDb = -10.f; AmbDb = -6.f; Att = 0.05f; Rel = 3.f; } break;
		case EDmCue::Roar: if (V.Variant == DmCueVar::RoarLaunch) { MusicDb = -6.f; AmbDb = -6.f; Att = 0.05f; Rel = 3.f; } break;   // 3.2: the launch ducks only 6 dB
		case EDmCue::Thunder: if (V.Muffle < 0.3f) { MusicDb = -6.f; Att = 0.08f; Rel = 2.f; } break;
		case EDmCue::Rumble: if (V.Variant <= DmCueVar::RumbleQuake) { MusicDb = -6.f; Att = 0.08f; Rel = 2.f; } break;
		case EDmCue::Choir: case EDmCue::Chime: case EDmCue::Horn: case EDmCue::Toll: MusicDb = -4.f; Att = 0.2f; Rel = 2.f; break;
		default: break;
		}
		MusicDb *= Strength; AmbDb *= Strength;
	}

	/** Glue compressor (5.1): threshold -18 dB, 2 : 1, attack 30 ms, release 300 ms, knee 6 dB. */
	struct FGlue
	{
		float Env = 0.f, AttC = 0.f, RelC = 0.f, GainDb = 0.f, Gain = 1.f, MinGainDb = 0.f;
		int32 Count = 0;
		void Init(float Sr) { AttC = SmoothCoef(0.03f, Sr); RelC = SmoothCoef(0.3f, Sr); }
		static float Reduction(float Xdb)
		{
			const float T = -18.f, Rr = 2.f, W = 6.f;
			if (Xdb < T - W * 0.5f) return 0.f;
			if (Xdb > T + W * 0.5f) return (T + (Xdb - T) / Rr) - Xdb;
			const float D = Xdb - T + W * 0.5f;
			return (1.f / Rr - 1.f) * D * D / (2.f * W);
		}
		void Tick(float& L, float& R)
		{
			const float X = FMath::Max(std::fabs(L), std::fabs(R));
			Env += (X > Env ? AttC : RelC) * (X - Env);
			if (++Count >= 16)
			{
				Count = 0;
				GainDb = Reduction(GainToDb(Env));
				Gain = DbToGain(GainDb);
				MinGainDb = FMath::Min(MinGainDb, GainDb);
			}
			L *= Gain; R *= Gain;
		}
	};
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
	FDmCueVoice Voices[MaxSlots];
	FDmCuePatch PatchScratch;
	int64 Order = 0;
	FRng Rng;
	double Now = 0;
	double StartTimes[RateMax] = {};
	int32 StartPos = 0;
	double EndingUntil = -1;

	FReverb Hall, Sky;
	FDelay EchoNearL, EchoNearR, EchoFarL, EchoFarR;
	FLp1 EchoLpL, EchoLpR;
	FHp1 BusDc[8];
	FSvf HpL, HpR;
	FLp1 TopL, TopR;
	FDcBlock DcL, DcR;
	FGlue Glue;
	FLimiter Lim;
	float Fade = 1.f, FadeTarget = 1.f, FadeCoef = 0.f;
	float ThunderTimer = 10.f;
	float MusicDuckDb = 0.f, AmbDuckDb = 0.f, MusicRel = 2.f, AmbRel = 2.f;
	// 5.4 rule 4: the master short-term loudness never stays above -18 LUFS for more than 20 s (a slow governor)
	double GovMs = 0.0;
	float GovAbove = 0.f, GovDb = 0.f, GovTargetDb = 0.f;

	float AmbL[MaxBlock], AmbR[MaxBlock], MusL[MaxBlock], MusR[MaxBlock], SfxL[MaxBlock], SfxR[MaxBlock], UiL[MaxBlock], UiR[MaxBlock];
	float HallL[MaxBlock], HallR[MaxBlock], SkyL[MaxBlock], SkyR[MaxBlock];
	float MHallL[MaxBlock], MHallR[MaxBlock], MSkyL[MaxBlock], MSkyR[MaxBlock];
	float ENearL[MaxBlock], ENearR[MaxBlock], EFarL[MaxBlock], EFarR[MaxBlock];

	FDmAudioLevels Lv;
	double RenderSec = 0;

	void Init(float InSr, uint32 InSeed)
	{
		Sr = InSr;
		Seed = InSeed;
		Rng = FRng(InSeed ^ 0xC0FFEEu);
		Amb.Init(Sr, InSeed);
		Music.Init(Sr, InSeed);
		// 0: hall 3.2 s (pre-delay 30 ms, damping 6 kHz falling), sky 7 s (pre-delay 80 ms, damping 3 kHz falling)
		Hall.Init(Sr, 3.2f, 5000.f, 0.03f);
		Sky.Init(Sr, 7.f, 2200.f, 0.08f);
		EchoNearL.Init(int32(Sr * 0.3f)); EchoNearR.Init(int32(Sr * 0.3f));
		EchoFarL.Init(int32(Sr * 0.5f)); EchoFarR.Init(int32(Sr * 0.5f));
		EchoLpL.Set(1500.f, Sr); EchoLpR.Set(1500.f, Sr);
		for (FHp1& H : BusDc) H.Set(10.f, Sr);
		HpL.Set(30.f, 0.707f, Sr); HpR.Set(30.f, 0.707f, Sr);
		TopL.Set(13500.f, Sr); TopR.Set(13500.f, Sr);
		Glue.Init(Sr);
		Lim.Init(Sr, -1.f, 0.002f, 0.18f);
		FadeCoef = SmoothCoef(0.3f, Sr);
		for (double& T : StartTimes) T = -100.0;
		Amb.SetRoot(Music.GetScale().TonicHz());
	}

	void StartCueNow(const FCmd& C)
	{
		const bool bUi = DmAudio::CueBus(C.Cue) == EDmBus::Ui;
		// endings: every UI sound is muted except click
		if (bUi && Now < EndingUntil && C.Cue != EDmCue::Click) { ++Lv.CuesDropped; return; }
		// 5.3: at most 12 effect starts per second (UI sounds keep their own rate limits)
		if (!bUi && Now - StartTimes[StartPos] < 1.0) { ++Lv.CuesDropped; return; }
		const FDmScale& Sc = Music.GetScale();
		BuildCuePatch(C.Cue, C.P, Sc, Rng, PatchScratch);
		// per-name limits: the oldest of the same name gives way (with a fade)
		const int32 Limit = DmAudio::CueLimit(C.Cue);
		int32 Same = 0, OldestSame = -1, Live = 0;
		for (int32 I = 0; I < MaxSlots; ++I)
		{
			const FDmCueVoice& V = Voices[I];
			if (!V.bActive || V.IsDying()) continue;
			++Live;
			if (V.Cue != C.Cue || (C.Cue == EDmCue::Roar && V.Variant != C.P.Variant)) continue;
			++Same;
			if (OldestSame < 0 || V.StartOrder < Voices[OldestSame].StartOrder) OldestSame = I;
		}
		if (Same >= Limit && OldestSame >= 0) { Voices[OldestSame].Kill(0.03f); ++Lv.CuesStolen; --Live; }
		// at most 24 voices: the oldest quietest is stolen with a 30 ms fade, never a cut
		if (Live >= MaxLive)
		{
			int32 Victim = -1;
			float Score = 1e9f;
			for (int32 I = 0; I < MaxSlots; ++I)
			{
				const FDmCueVoice& V = Voices[I];
				if (!V.bActive || V.IsDying()) continue;
				const float S = V.Level() * 10.f + float(V.StartOrder - Order) * 1e-4f;
				if (S < Score) { Score = S; Victim = I; }
			}
			if (Victim >= 0) { Voices[Victim].Kill(0.03f); ++Lv.CuesStolen; }
		}
		int32 Slot = -1;
		for (int32 I = 0; I < MaxSlots; ++I) if (!Voices[I].bActive) { Slot = I; break; }
		if (Slot < 0)
			for (int32 I = 0; I < MaxSlots; ++I) if (Voices[I].IsDying()) { Slot = I; break; }
		if (Slot < 0) { ++Lv.CuesDropped; return; }
		Voices[Slot].Start(C.Cue, PatchScratch, C.P, Sc, Sr, Rng.Next() | 1u, ++Order);
		++Lv.CuesStarted;
		if (!bUi)
		{
			StartTimes[StartPos] = Now;
			StartPos = (StartPos + 1) % RateMax;
			int32 InSec = 0;
			for (double T : StartTimes) InSec += (Now - T < 1.0) ? 1 : 0;
			Lv.MaxStartsPerSec = FMath::Max(Lv.MaxStartsPerSec, InSec);
		}
	}

	void StartEnding(int32 S)
	{
		const EDmStinger St = EDmStinger(S);
		EndingUntil = Now + (St == EDmStinger::EndingBeloved ? 20.0 : (St >= EDmStinger::EndingSilence ? 28.0 : 18.0));
		if (St == EDmStinger::EndingSilence || St == EDmStinger::EndingQuiet || St == EDmStinger::EndingAsh) Amb.EndT = 0.f;
		if (St == EDmStinger::EndingFeared)
		{
			// the launch roar, filtered down to a distant rumble
			FCmd C; C.Kind = 0; C.Cue = EDmCue::Roar; C.P.Variant = DmCueVar::RoarLaunch; C.P.Vol = 0.5f; C.P.Muffle = 0.85f; C.P.Delay = 6.f;
			StartCueNow(C);
		}
		if (St == EDmStinger::EndingAsh)
		{
			// a low crackle bed at -30 dB for the first 12 s
			for (int32 K = 0; K < 6; ++K)
			{
				FCmd C; C.Kind = 0; C.Cue = EDmCue::Crackle; C.P.Vol = 0.18f; C.P.Delay = 2.f * K; C.P.Muffle = 0.4f;
				StartCueNow(C);
			}
		}
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
		// the score sees the new context first (an age change must be pending before an explicit age stinger arrives)
		const float BlockSec = float(N) / Sr;
		UpdateMusic(BlockSec);
		for (int32 I = 0; I < NumLocal; ++I)
		{
			const FCmd& C = Local[I];
			if (C.Kind == 0) StartCueNow(C);
			else if (C.Kind == 1) Music.Stinger(C.Sting);
			else if (C.Kind == 2) FadeTarget = C.bFlag ? 0.f : 1.f;
			else if (C.Kind == 3) Music.DebugNote(EDmInst(FMath::Clamp(C.I0, 0, int32(EDmInst::Count) - 1)), uint8(C.I1), C.F0, C.F1, C.F2);
		}
		Amb.ComputeTargets(Ctx);
		if (Amb.EndT > 28.f) Amb.EndT = -1.f;

		// storms throw distant thunder on their own: every 8-20 s at -6 dB
		const float StormG = Amb.Gain[int32(EDmLayer::Rain)] * Sat01(Ctx.Storm);
		ThunderTimer -= BlockSec;
		if (ThunderTimer <= 0.f)
		{
			ThunderTimer = Rng.Range(8.f, 20.f);
			if (StormG > 0.05f)
			{
				FCmd C; C.Kind = 0; C.Cue = EDmCue::Thunder;
				C.P.Vol = 0.5f * Rng.Range(0.7f, 1.f) * FMath::Min(1.f, StormG * 1.3f); C.P.Muffle = Rng.Range(0.4f, 0.9f); C.P.Pan = Rng.Bi() * 0.8f;
				StartCueNow(C);
			}
		}

		// ducking (5.2): the deepest request, never the sum
		float MusT = 0.f, AmbT = 0.f, MusAtt = 0.2f, AmbAtt = 0.2f;
		for (const FDmCueVoice& V : Voices)
		{
			if (!V.bActive || V.IsDelayed()) continue;
			float Md, Ad, At, Re;
			DuckOf(V, Md, Ad, At, Re);
			if (Md < MusT) { MusT = Md; MusAtt = At; MusicRel = Re; }
			if (Ad < AmbT) { AmbT = Ad; AmbAtt = At; AmbRel = Re; }
		}
		MusicDuckDb += SmoothCoef(MusT < MusicDuckDb ? MusAtt : MusicRel / 3.f, 1.f / BlockSec) * (MusT - MusicDuckDb);
		AmbDuckDb += SmoothCoef(AmbT < AmbDuckDb ? AmbAtt : AmbRel / 3.f, 1.f / BlockSec) * (AmbT - AmbDuckDb);
		Music.SetDuck(DbToGain(MusicDuckDb));
		const float AmbDuck = DbToGain(AmbDuckDb);
		Lv.MinMusicDuckDb = FMath::Min(Lv.MinMusicDuckDb, MusicDuckDb);
		Lv.MinAmbDuckDb = FMath::Min(Lv.MinAmbDuckDb, AmbDuckDb);

		const size_t Bytes = sizeof(float) * size_t(N);
		FMemory::Memzero(AmbL, Bytes); FMemory::Memzero(AmbR, Bytes); FMemory::Memzero(MusL, Bytes); FMemory::Memzero(MusR, Bytes);
		FMemory::Memzero(SfxL, Bytes); FMemory::Memzero(SfxR, Bytes); FMemory::Memzero(UiL, Bytes); FMemory::Memzero(UiR, Bytes);
		FMemory::Memzero(HallL, Bytes); FMemory::Memzero(HallR, Bytes); FMemory::Memzero(SkyL, Bytes); FMemory::Memzero(SkyR, Bytes);
		FMemory::Memzero(MHallL, Bytes); FMemory::Memzero(MHallR, Bytes); FMemory::Memzero(MSkyL, Bytes); FMemory::Memzero(MSkyR, Bytes);
		FMemory::Memzero(ENearL, Bytes); FMemory::Memzero(ENearR, Bytes); FMemory::Memzero(EFarL, Bytes); FMemory::Memzero(EFarR, Bytes);

		Amb.Render(AmbL, AmbR, N, BlockSec);
		Music.Render(MusL, MusR, MHallL, MHallR, MSkyL, MSkyR, N);
		{
			const int32 Req = Music.TakeEndingRequest();
			if (Req >= 0) StartEnding(Req);
		}
		int32 NumVoices = 0;
		float UiHallL[MaxBlock], UiHallR[MaxBlock];
		FMemory::Memzero(UiHallL, Bytes); FMemory::Memzero(UiHallR, Bytes);
		for (FDmCueVoice& V : Voices)
		{
			if (!V.bActive) continue;
			++NumVoices;
			const bool bFar = V.Muffle >= 0.5f;
			if (DmAudio::CueBus(V.Cue) == EDmBus::Ui) V.Render(UiL, UiR, UiHallL, UiHallR, UiHallL, UiHallR, ENearL, ENearR, N);
			else V.Render(SfxL, SfxR, HallL, HallR, SkyL, SkyR, bFar ? EFarL : ENearL, bFar ? EFarR : ENearR, N);
		}

		// bus trims (measured mix balance): ambience +3 dB, music -2 dB, so the score sits under the world
		const float VA = Vol.Ambience * 1.41f * AmbDuck, VM = Vol.Music * 0.8f, VS = Vol.Sfx, VU = Vol.Ui, VMa = Vol.Master * 1.58f;   // +4 dB make-up
		const float DNear = 0.08f + 0.4f * 0.25f, DFar = 0.08f + 0.4f * 0.75f;   // 3.1: 80 ms + 400 ms x d (two distance bins)
		const int32 NearS = int32(DNear * Sr), FarS = int32(DFar * Sr);
		float Peak = 0.f;
		const float GovGain = DbToGain(GovDb);
		double BlockMs = 0.0;
		for (int32 K = 0; K < N; ++K)
		{
			// per-bus DC blockers (5.4)
			const float aL = BusDc[0].Tick(AmbL[K]) * VA, aR = BusDc[1].Tick(AmbR[K]) * VA;
			const float mL = BusDc[2].Tick(MusL[K]) * VM, mR = BusDc[3].Tick(MusR[K]) * VM;
			const float sL = BusDc[4].Tick(SfxL[K]) * VS, sR = BusDc[5].Tick(SfxR[K]) * VS;
			const float uL = BusDc[6].Tick(UiL[K]) * VU, uR = BusDc[7].Tick(UiR[K]) * VU;
			float L = aL + mL + sL + uL;
			float R = aR + mR + sR + uR;
			// the far echo (3.1 rule 2): low-passed at 1.5 kHz
			EchoNearL.Push(ENearL[K] * VS); EchoNearR.Push(ENearR[K] * VS); EchoFarL.Push(EFarL[K] * VS); EchoFarR.Push(EFarR[K] * VS);
			L += EchoLpL.Tick(EchoNearR.Read(NearS) + EchoFarR.Read(FarS));
			R += EchoLpR.Tick(EchoNearL.Read(NearS) + EchoFarL.Read(FarS));
			// reverbs: hall (music, choir / horn / toll, ambience, the chronicle quill), sky (the godlike space)
			const float HL = MHallL[K] * VM + HallL[K] * VS + AmbL[K] * VA * 0.1f + UiHallL[K] * VU, HR = MHallR[K] * VM + HallR[K] * VS + AmbR[K] * VA * 0.1f + UiHallR[K] * VU;
			const float KL = MSkyL[K] * VM + SkyL[K] * VS, KR = MSkyR[K] * VM + SkyR[K] * VS;
			float WL, WR, YL, YR;
			Hall.Tick(HL, HR, WL, WR);
			Sky.Tick(KL, KR, YL, YR);
			L += WL * 0.6f + YL * 0.5f; R += WR * 0.6f + YR * 0.5f;
			// master: 30 Hz high-pass, DC blocker, glue compressor, limiter
			L = DcL.Tick(TopL.Tick(HpL.High(L)));
			R = DcR.Tick(TopR.Tick(HpR.High(R)));
			Fade += FadeCoef * (FadeTarget - Fade);
			const float G = VMa * Fade * GovGain;
			L *= G; R *= G;
			if (!std::isfinite(L) || !std::isfinite(R)) { L = 0.f; R = 0.f; }
			Glue.Tick(L, R);
			Lim.Tick(L, R);
			OutL[K] = L; OutR[K] = R;
			BlockMs += double(L) * L + double(R) * R;
			Peak = FMath::Max(Peak, FMath::Max(std::fabs(L), std::fabs(R)));
		}
		Now += BlockSec;
		{
			// the governor: a 3 s loudness estimate; after 10 s above -18.5 it pulls the master down 4 dB until it falls under -21
			GovMs += double(SmoothCoef(3.f, 1.f / BlockSec)) * (BlockMs / double(N) - GovMs);
			const float Lu = -0.691f + 10.f * std::log10(float(FMath::Max(GovMs, 1e-12)));
			if (Lu > -18.5f) GovAbove += BlockSec; else GovAbove = FMath::Max(0.f, GovAbove - 2.f * BlockSec);
			if (GovAbove > 10.f) GovTargetDb = -4.f;
			else if (Lu < -21.f) GovTargetDb = 0.f;
			GovDb += SmoothCoef(2.f, 1.f / BlockSec) * (GovTargetDb - GovDb);
			Lv.GovernorDb = FMath::Min(Lv.GovernorDb, GovDb);
		}

		for (int32 I = 0; I < NumLayers; ++I) Lv.Layer[I] = Amb.Gain[I];
		Lv.Beds = Amb.Beds;
		Lv.Voices = NumVoices;
		Lv.MusicVoices = Music.ActiveNotes();
		Lv.FramesRendered += N;
		Lv.LimiterMinGain = Lim.MinGain;
		Lv.GlueMinGainDb = Glue.MinGainDb;
		Lv.SafetyClips = Lim.SafetyClips;
		Lv.BlockPeak = Peak;
		Lv.MusicAge = Music.GetAge();
		Lv.MusicDensity = Music.GetDensity();
		Lv.MusicTension = Music.GetTension();
		Lv.MusicTempo = Music.GetTempo();
		for (int32 M = 0; M < 4; ++M) Lv.MusicMood[M] = Music.GetMood(EDmMood(M));
		Lv.MusicDuckDb = MusicDuckDb; Lv.AmbDuckDb = AmbDuckDb;
		Lv.VoiceBusySec = Music.VoiceBusySeconds(); Lv.ScoreSec = Music.ScoreSeconds();
		Lv.Phrases = Music.PhrasesPlayed;
		Lv.StingersStarted = Music.StingersPlayed;
		Lv.StingersDropped = Music.StingersDropped;
		Lv.ScaleRoot = Music.GetScale().Root;
		Lv.ScaleMode = Music.GetScale().Mode;
		for (int32 I = 0; I < 20 && I < int32(EDmInst::Count); ++I) Lv.InstNotes[I] = Music.NotesPlayed[I];
	}

	void UpdateMusic(float BlockSec)
	{
		FDmMusicParams P;
		P.Age = Ctx.Age; P.Phase = Ctx.Phase; P.Tension = Ctx.Tension; P.Altitude01 = Ctx.Altitude01; P.bPaused = Ctx.bPaused; P.bScore = Ctx.bScore; P.Seed = Ctx.Seed;
		for (int32 M = 0; M < 4; ++M) P.Mood[M] = Ctx.Mood[M];
		Music.Update(P, BlockSec);
		Amb.SetRoot(Music.GetScale().TonicHz());
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

void FDmAudioEngine::DebugMusicNote(int32 Inst, int32 Var, float Midi, float Vel, float Gate)
{
	FScopeLock S(&Impl->Lock);
	if (Impl->RingN >= MaxCmds) return;
	FCmd& C = Impl->Ring[Impl->RingN++];
	C = FCmd();
	C.Kind = 3; C.I0 = Inst; C.I1 = Var; C.F0 = Midi; C.F1 = Vel; C.F2 = Gate;
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
