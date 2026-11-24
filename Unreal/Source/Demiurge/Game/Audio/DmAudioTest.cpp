// DEMIURGE — Game/Audio/DmAudioTest.cpp : scripted offline renders and their checks. Owner: C9 audio.
//
// The checks turn docs/content/sound-direction.md into numbers: 5.1 loudness targets (BS.1770 LUFS per bus alone),
// 5.4 must-never-happen rules (clipping, NaN, DC, sub-bass under 25 %, 2-5 kHz under 30 % in any 3 s window, nothing
// above 12 kHz for long, short-term never above -18 LUFS for 20 s, never silent), 1.1 (the voice silent at least half
// the time, zoom thinning), 1.3 moods, 2 ambience by zoom and biome (at most 6 beds), 3.2 cue levels "re music",
// 4 UI levels, 5.2 ducking and 5.3 voice and rate limits.
#include "Game/Audio/DmAudioTest.h"
#include "Game/Audio/DmAudioMusic.h"
#include <cmath>

namespace
{
	struct FEv
	{
		double T = 0;
		uint8 Kind = 0;          // 0 cue, 1 stinger, 2 age, 3 tension, 4 snapshot, 5 day, 6 altitude, 7 mood, 8 debug note, 9 phase
		EDmCue Cue = EDmCue::Click;
		FDmCueParams P;
		EDmStinger St = EDmStinger::Age;
		float Value = 0.f;
		int32 Index = 0, Var = 0;
		float Midi = 60.f, Gate = 1.f;
		FString Label;
		bool bDone = false;
	};

	struct FScript
	{
		FString Help;
		FDmAudioContext Ctx;
		FDmAudioVolumes Vol;
		TArray<FEv> Events;
		TArray<EDmLayer> Layers;
		bool bMusicTrack = true;
		double AltRampFrom = -1, AltRampTo = -1;   // zoom-sweep: Altitude01 ramps linearly over the script
	};

	FEv Cue(double T, EDmCue C, float Vol = 1.f, float Param = 0.f, float Muffle = 0.f, float Pan = 0.f, const TCHAR* Label = nullptr, uint8 Variant = 0)
	{
		FEv E; E.T = T; E.Kind = 0; E.Cue = C; E.P.Vol = Vol; E.P.Param = Param; E.P.Muffle = Muffle; E.P.Pan = Pan; E.P.Variant = Variant;
		E.Label = Label ? FString(Label) : FString(DmAudio::CueName(C)).ToUpper();
		return E;
	}
	FEv Sting(double T, EDmStinger S)
	{
		FEv E; E.T = T; E.Kind = 1; E.St = S; E.Label = FString(DmAudio::StingerName(S)).ToUpper();
		return E;
	}
	FEv SetAge(double T, int32 Age) { FEv E; E.T = T; E.Kind = 2; E.Value = float(Age); E.Label = FString::Printf(TEXT("AGE %d"), Age); return E; }
	FEv SetTension(double T, float V) { FEv E; E.T = T; E.Kind = 3; E.Value = V; E.Label = FString::Printf(TEXT("TENSION %.1f"), V); return E; }
	FEv Snap(double T) { FEv E; E.T = T; E.Kind = 4; return E; }
	FEv SetDay(double T, float V) { FEv E; E.T = T; E.Kind = 5; E.Value = V; E.Label = FString::Printf(TEXT("DAY %.2f"), V); return E; }
	FEv SetAltitude(double T, float V) { FEv E; E.T = T; E.Kind = 6; E.Value = V; E.Label = FString::Printf(TEXT("ALT %.2f"), V); return E; }
	FEv SetMood(double T, EDmMood M, float V) { FEv E; E.T = T; E.Kind = 7; E.Index = int32(M); E.Value = V; E.Label = FString::Printf(TEXT("%s %.0f"), *FString(FDmMusic::MoodName(M)).ToUpper(), V); return E; }
	FEv Note(double T, EDmInst I, int32 Var, float Midi, float Vel, float Gate) { FEv E; E.T = T; E.Kind = 8; E.Index = int32(I); E.Var = Var; E.Midi = Midi; E.Value = Vel; E.Gate = Gate; return E; }
	FEv SetPhase(double T, int32 Ph) { FEv E; E.T = T; E.Kind = 9; E.Value = float(Ph); E.Label = FString::Printf(TEXT("PHASE %d"), Ph); return E; }

	/** Largest level rise, in dB, from the WinSec seconds before a point to the WinSec seconds after it (only where
	 *  the level ends above -50 dBFS). Band-limited noise fluctuates by about 5 dB at 20 ms; a click or a
	 *  one-sample step reads 10 dB or more, so the check uses 8 dB. (A 5 ms window is too noisy to use.) */
	float MaxLevelRiseDb(const FDmAudioRender& R, double WinSec)
	{
		const int32 W = FMath::Max(4, int32(WinSec * R.Sr));
		const int32 Hop = W / 4;
		TArray<double> E;
		for (int32 I = 0; I + W <= R.L.Num(); I += Hop)
		{
			double P = 0;
			for (int32 K = I; K < I + W; ++K) P += double(R.L[K]) * R.L[K] + double(R.R[K]) * R.R[K];
			E.Add(P / (2.0 * W) + 1e-20);
		}
		float Best = 0.f;
		for (int32 J = 4; J + 4 <= E.Num(); ++J)
		{
			double Before = 0, After = 0;
			for (int32 K = 0; K < 4; ++K) { Before += E[J - 4 + K]; After += E[J + K]; }
			Before *= 0.25; After *= 0.25;
			if (10.0 * std::log10(After) < -50.0) continue;
			Best = FMath::Max(Best, float(10.0 * std::log10(After / FMath::Max(Before, 1e-20))));
		}
		return Best;
	}

	float RangeDb(const FDmAudioRender& R, double T0, double T1)
	{
		const int32 K0 = FMath::Clamp(int32(T0 * R.Sr), 0, R.L.Num()), K1 = FMath::Clamp(int32(T1 * R.Sr), K0, R.L.Num());
		double P = 0;
		for (int32 K = K0; K < K1; ++K) P += double(R.L[K]) * R.L[K] + double(R.R[K]) * R.R[K];
		P /= FMath::Max(1.0, 2.0 * double(K1 - K0));
		return float(10.0 * std::log10(FMath::Max(P, 1e-20)));
	}

	// ---------------------------------------------------------------------------------------- cue variants and targets
	struct FCueSpec { const TCHAR* Name; EDmCue Cue; uint8 Var; float Param; float Muffle; float TargetSt; double Sec; };
	// TargetSt: the peak short-term loudness (LUFS) of 3.2 "re music" on a -28 LUFS music bus; 0 = UI (checked by 4's rules)
	const FCueSpec GCues[] = {
		{ TEXT("hover"), EDmCue::Hover, 0, 0.f, 0.f, 0.f, 1.0 },
		{ TEXT("click"), EDmCue::Click, 0, 0.f, 0.f, 0.f, 1.0 },
		{ TEXT("arm"), EDmCue::Arm, DmCueVar::ArmSky, 0.f, 0.f, 0.f, 1.2 },
		{ TEXT("arm-wrath"), EDmCue::Arm, DmCueVar::ArmWrath, 0.f, 0.f, 0.f, 1.2 },
		{ TEXT("deny"), EDmCue::Deny, 0, 0.f, 0.f, 0.f, 1.0 },
		{ TEXT("unlock"), EDmCue::Unlock, 0, 0.f, 0.f, 0.f, 2.5 },
		{ TEXT("toast"), EDmCue::Toast, 0, 0.f, 0.f, 0.f, 1.2 },
		{ TEXT("chronicle"), EDmCue::Chronicle, 0, 0.f, 0.f, 0.f, 1.5 },
		{ TEXT("thunder"), EDmCue::Thunder, 0, 0.f, 0.f, -22.f, 8.5 },
		{ TEXT("thunder-far"), EDmCue::Thunder, 0, 0.f, 0.7f, -28.f, 8.5 },
		{ TEXT("impact"), EDmCue::Impact, 0, 1.f, 0.f, -20.f, 10.0 },
		{ TEXT("rumble"), EDmCue::Rumble, DmCueVar::RumbleEruption, 1.f, 0.f, -24.f, 14.0 },
		{ TEXT("rumble-quake"), EDmCue::Rumble, DmCueVar::RumbleQuake, 1.f, 0.f, -25.f, 12.0 },
		{ TEXT("rumble-raise"), EDmCue::Rumble, DmCueVar::RumbleRaise, 0.2f, 0.f, -32.f, 3.0 },
		{ TEXT("wave"), EDmCue::Wave, DmCueVar::WaveTsunami, 0.f, 0.f, -23.f, 11.0 },
		{ TEXT("wave-comet"), EDmCue::Wave, DmCueVar::WaveComet, 0.f, 0.f, -28.f, 9.0 },
		{ TEXT("rainfall"), EDmCue::Rainfall, 0, 4.f, 0.f, -30.f, 8.0 },
		{ TEXT("shimmer"), EDmCue::Shimmer, DmCueVar::ShimmerBloom, 0.f, 0.f, -31.f, 6.0 },
		{ TEXT("shimmer-inspire"), EDmCue::Shimmer, DmCueVar::ShimmerInspire, 0.f, 0.f, -31.f, 6.0 },
		{ TEXT("chime"), EDmCue::Chime, DmCueVar::ChimeHeal, 0.f, 0.f, -30.f, 7.0 },
		{ TEXT("chime-peace"), EDmCue::Chime, DmCueVar::ChimePeace, 0.f, 0.f, -30.f, 7.0 },
		{ TEXT("choir"), EDmCue::Choir, DmCueVar::ChoirProphet, 0.f, 0.f, -31.f, 8.0 },
		{ TEXT("choir-awaken"), EDmCue::Choir, DmCueVar::ChoirAwaken, 0.f, 0.f, -36.f, 7.0 },
		{ TEXT("horn"), EDmCue::Horn, 0, 2.f, 0.f, -28.f, 6.0 },
		{ TEXT("horn-sail"), EDmCue::Horn, 0, 3.f, 0.f, -28.f, 6.0 },
		{ TEXT("horn-industry"), EDmCue::Horn, 0, 4.f, 0.f, -31.f, 6.0 },
		{ TEXT("horn-space"), EDmCue::Horn, 0, 6.f, 0.f, -28.f, 6.0 },
		{ TEXT("toll"), EDmCue::Toll, 0, 0.f, 0.f, -32.f, 8.0 },
		{ TEXT("roar"), EDmCue::Roar, DmCueVar::RoarLaunch, 0.f, 0.f, -22.f, 21.0 },
		{ TEXT("roar-comet"), EDmCue::Roar, DmCueVar::RoarComet, 0.f, 0.f, -28.f, 5.0 },
		{ TEXT("crackle"), EDmCue::Crackle, 0, 0.f, 0.f, -36.f, 3.0 },
		{ TEXT("fall"), EDmCue::Fall, 0, 2.5f, 0.f, -24.f, 4.5 },   // (verify) 3.5 s cut its sky tail at -46..-50 dBFS on 3 of 4 seeds
	};
	const FCueSpec* FindCue(const FString& Name)
	{
		for (const FCueSpec& C : GCues) if (Name == C.Name) return &C;
		return nullptr;
	}

	// music alone: the seven ages and the moods (5.1 targets, the previews' mood levels)
	const float GAgeLufs[7] = { -30.f, -29.f, -28.f, -28.f, -26.f, -26.f, -30.f };
	struct FMoodSpec { const TCHAR* Name; EDmMood Mood; int32 Age; float Lufs; };
	const FMoodSpec GMoods[] = {
		{ TEXT("music-mood-peace"), EDmMood::Count, 3, -28.f },
		{ TEXT("music-mood-war"), EDmMood::War, 3, -27.f },
		{ TEXT("music-mood-plague"), EDmMood::Plague, 3, -29.f },
		{ TEXT("music-mood-golden"), EDmMood::Golden, 3, -27.f },
		{ TEXT("music-mood-doom"), EDmMood::Doom, 5, -29.f },
	};
	const FMoodSpec* FindMood(const FString& Name) { for (const FMoodSpec& M : GMoods) if (Name == M.Name) return &M; return nullptr; }

	struct FInstSpec { const TCHAR* Name; EDmInst Inst; int32 Var; int32 Midi; float Gate; };
	const FInstSpec GInst[] = {
		{ TEXT("inst-flute"), EDmInst::Flute, 0, 74, 1.5f }, { TEXT("inst-drum"), EDmInst::HandDrum, 0, 40, 0.2f }, { TEXT("inst-lyre"), EDmInst::Lyre, 0, 62, 0.5f },
		{ TEXT("inst-reed"), EDmInst::Reed, 0, 62, 1.5f }, { TEXT("inst-string"), EDmInst::Strings, 0, 74, 1.5f }, { TEXT("inst-pad"), EDmInst::Strings, 1, 50, 4.f },
		{ TEXT("inst-synthstring"), EDmInst::Strings, 3, 50, 4.f }, { TEXT("inst-bell"), EDmInst::Bell, 0, 62, 3.f }, { TEXT("inst-toll"), EDmInst::Bell, 1, 50, 6.f },
		{ TEXT("inst-brass"), EDmInst::Brass, 0, 50, 1.5f }, { TEXT("inst-piano"), EDmInst::Piano, 0, 62, 1.f }, { TEXT("inst-spacepad"), EDmInst::Pad, 0, 50, 5.f },
		{ TEXT("inst-glass"), EDmInst::Glass, 0, 74, 1.5f }, { TEXT("inst-bowed"), EDmInst::Cello, 0, 38, 4.f }, { TEXT("inst-wardrum"), EDmInst::Taiko, 0, 40, 0.3f },
		{ TEXT("inst-hum"), EDmInst::Breath, 0, 38, 4.f }, { TEXT("inst-choir"), EDmInst::Choir, 0, 50, 3.f }, { TEXT("inst-organ"), EDmInst::Organ, 0, 62, 3.f },
		{ TEXT("inst-cluster"), EDmInst::Cluster, 0, 50, 5.f }, { TEXT("inst-rafters"), EDmInst::Rafters, 0, 60, 6.f },
	};
	const FInstSpec* FindInst(const FString& Name) { for (const FInstSpec& I : GInst) if (Name == I.Name) return &I; return nullptr; }

	void Land(FDmAudioContext& C) { C.Forest = 0.3f; C.Open = 0.3f; C.Coast = 0.25f; C.Sea = 0.25f; }

	bool MakeScript(const FString& Name, FScript& S)
	{
		FDmAudioContext& C = S.Ctx;
		C.Phase = 4; C.Age = 3; C.Day = 0.8f;
		if (Name == TEXT("orbit"))
		{
			S.Help = TEXT("whole planet in view, day side");
			C.Altitude01 = 1.f; C.Day = 0.6f; C.Sea = 0.65f; C.Coast = 0.15f; C.Forest = 0.15f; C.Open = 0.1f;
			S.Layers = { EDmLayer::Space, EDmLayer::HighAir, EDmLayer::Wind, EDmLayer::Surf, EDmLayer::Birds };
		}
		else if (Name == TEXT("coast-day"))
		{
			S.Help = TEXT("low over a sunny coast");
			C.Altitude01 = 0.14f; C.Day = 1.f; C.Coast = 0.85f; C.Sea = 0.5f; C.Open = 0.35f; C.Forest = 0.15f;
			S.Events = { Cue(5.0, EDmCue::Hover), Cue(5.3, EDmCue::Click) };
			S.Layers = { EDmLayer::Surf, EDmLayer::Sea, EDmLayer::Birds, EDmLayer::Wind, EDmLayer::Grass };
		}
		else if (Name == TEXT("forest-night"))
		{
			S.Help = TEXT("low over a forest at night: crickets, an owl, leaves");
			C.Altitude01 = 0.12f; C.Day = 0.03f; C.Forest = 0.9f; C.Open = 0.1f;
			S.Layers = { EDmLayer::Crickets, EDmLayer::Leaves, EDmLayer::Birds, EDmLayer::Wind };
		}
		else if (Name == TEXT("city"))
		{
			S.Help = TEXT("over a sail-age harbour town by day");
			C.Altitude01 = 0.16f; C.Day = 0.85f; C.Town = 0.85f; C.TownAge = 3; C.Coast = 0.3f; C.Sea = 0.1f; C.Open = 0.2f;
			S.Events = { Cue(3.0, EDmCue::Click), Cue(6.0, EDmCue::Chime) };
			S.Layers = { EDmLayer::TownMiddle, EDmLayer::Surf, EDmLayer::Birds, EDmLayer::Wind };
		}
		else if (Name == TEXT("storm"))
		{
			S.Help = TEXT("inside a storm over the sea");
			C.Altitude01 = 0.3f; C.Day = 0.4f; C.Storm = 0.9f; C.Rain = 0.9f; C.Sea = 0.5f; C.Coast = 0.3f;
			S.Events = { Cue(2.0, EDmCue::Thunder, 1.f, 0.f, 0.1f, -0.3f), Cue(9.0, EDmCue::Thunder, 0.8f, 0.f, 0.5f, 0.4f) };
			S.Layers = { EDmLayer::Rain, EDmLayer::Wind, EDmLayer::Surf };
		}
		else if (Name == TEXT("meteor"))
		{
			S.Help = TEXT("a meteor falls, strikes, the sea answers");
			C.Altitude01 = 0.45f; C.Day = 0.8f; C.Sea = 0.4f; C.Coast = 0.3f; C.Forest = 0.3f;
			S.Events = { Cue(1.0, EDmCue::Fall, 1.f, 3.f), Cue(4.0, EDmCue::Impact, 1.f, 1.f), Sting(4.0, EDmStinger::Wrath),
				Cue(6.5, EDmCue::Wave, 0.9f, 0.f, 0.f, 0.f, nullptr, DmCueVar::WaveTsunami) };
			S.Layers = { EDmLayer::Wind, EDmLayer::Surf, EDmLayer::Birds };
		}
		else if (Name == TEXT("age-advance"))
		{
			S.Help = TEXT("iron age becomes the age of sail at 8 s");
			C.Altitude01 = 0.3f; C.Age = 2; Land(C); C.Town = 0.3f;
			S.Events = { Snap(7.9), SetAge(8.0, 3), Sting(8.0, EDmStinger::Age), Snap(23.9) };
			S.Layers = { EDmLayer::Wind, EDmLayer::Surf, EDmLayer::Grass };
		}
		else if (Name == TEXT("stone"))
		{
			S.Help = TEXT("stone age: bone flute, frame drum, a hummed drone; a camp by the fire");
			C.Altitude01 = 0.18f; C.Age = 0; C.Forest = 0.45f; C.Open = 0.3f; C.Town = 0.35f; C.TownAge = 0;
			S.Layers = { EDmLayer::TownEarly, EDmLayer::Birds, EDmLayer::Wind };
		}
		else if (Name == TEXT("industry"))
		{
			S.Help = TEXT("industry: piano, a soft horn section, a dark pad; machines in town");
			C.Altitude01 = 0.16f; C.Age = 4; C.Town = 0.6f; C.TownAge = 4; C.Coast = 0.2f; C.Day = 0.7f;
			S.Layers = { EDmLayer::TownIndustry, EDmLayer::Wind, EDmLayer::Surf };
		}
		else if (Name == TEXT("space"))
		{
			S.Help = TEXT("space age: glass lead and wide pads; city hum at dusk");
			C.Altitude01 = 0.18f; C.Age = 6; C.Town = 0.45f; C.TownAge = 6; C.Day = 0.35f; C.Sea = 0.3f;
			S.Layers = { EDmLayer::TownModern, EDmLayer::Space, EDmLayer::Wind };
		}
		else if (Name == TEXT("genesis-molten"))
		{
			S.Help = TEXT("the molten world: lava roar and a held breath");
			C.Phase = 0; C.Age = 0; C.Altitude01 = 0.85f; C.Day = 0.6f;
			S.Layers = { EDmLayer::Lava, EDmLayer::Space, EDmLayer::Wind };
		}
		else if (Name == TEXT("genesis-water"))
		{
			S.Help = TEXT("comets fill the seas; the held breath gains its 3rd");
			C.Phase = 1; C.Age = 0; C.Altitude01 = 0.8f; C.Sea = 0.6f; C.Day = 0.7f;
			S.Events = { Cue(1.0, EDmCue::Roar, 0.9f, 0.f, 0.f, 0.f, nullptr, DmCueVar::RoarComet), Cue(3.5, EDmCue::Wave, 0.6f, 0.f, 0.f, 0.f, nullptr, DmCueVar::WaveComet),
				Cue(10.0, EDmCue::Roar, 0.9f, 0.f, 0.f, 0.f, nullptr, DmCueVar::RoarComet), Cue(12.5, EDmCue::Wave, 0.6f, 0.f, 0.f, 0.f, nullptr, DmCueVar::WaveComet), Cue(15.0, EDmCue::Shimmer, 0.8f) };
			S.Layers = { EDmLayer::Space, EDmLayer::HighAir, EDmLayer::Sea };
		}
		else if (Name == TEXT("war"))
		{
			S.Help = TEXT("war in the iron age: drums, a low pedal, the darker mode, a horn");
			C.Altitude01 = 0.3f; C.Age = 2; C.Town = 0.4f; C.TownAge = 2; C.Day = 0.6f; C.Open = 0.3f;
			S.Events = { SetMood(3.0, EDmMood::War, 1.f), Cue(4.0, EDmCue::Horn, 1.f, 2.f), Cue(10.0, EDmCue::Toll), Cue(14.0, EDmCue::Horn, 0.8f, 2.f, 0.3f, 0.5f), Snap(23.9) };
			S.Layers = { EDmLayer::TownMiddle, EDmLayer::Wind, EDmLayer::Grass };
		}
		else if (Name == TEXT("probe-surf"))
		{
			S.Help = TEXT("surf alone (click probe): low over a sunny coast, no score, no sea");
			C.Altitude01 = 0.14f; C.Day = 1.f; C.Coast = 1.f; C.Sea = 0.f; C.Open = 0.f; C.Forest = 0.f; C.Town = 0.f; C.bScore = false;
			S.Vol.Music = 0.f;
			S.Layers = { EDmLayer::Surf, EDmLayer::Wind };
		}
		else if (Name == TEXT("stress-all"))
		{
			S.Help = TEXT("worst case: every bed asked for near the ground, two dense bursts of cues and stingers on top");
			C.Altitude01 = 0.f; C.Day = 0.5f; C.Sea = 1.f; C.Coast = 1.f; C.Forest = 1.f; C.Rainforest = 1.f; C.Open = 1.f; C.Wetland = 1.f; C.Desert = 1.f; C.Cold = 1.f;
			C.Mountain = 1.f; C.Town = 1.f; C.TownAge = 6; C.Fire = 1.f; C.Lava = 1.f; C.Storm = 1.f; C.Rain = 1.f; C.Tension = 1.f;
			C.Mood[0] = 1.f; C.Mood[1] = 1.f; C.Mood[3] = 1.f;
			C.Age = 6; C.Phase = 4;
			const double Bursts[2] = { 2.0, 9.0 };
			for (double T0 : Bursts)
			{
				S.Events.Add(Cue(T0, EDmCue::Impact, 1.f, 1.2f)); S.Events.Add(Cue(T0, EDmCue::Rumble, 1.f, 1.f));
				S.Events.Add(Cue(T0, EDmCue::Roar)); S.Events.Add(Cue(T0, EDmCue::Wave));
				S.Events.Add(Cue(T0, EDmCue::Thunder)); S.Events.Add(Cue(T0 + 0.05, EDmCue::Thunder, 1.f, 0.f, 0.3f));
				S.Events.Add(Cue(T0 + 0.1, EDmCue::Toll)); S.Events.Add(Cue(T0 + 0.1, EDmCue::Choir)); S.Events.Add(Cue(T0 + 0.1, EDmCue::Horn));
				S.Events.Add(Cue(T0 + 0.1, EDmCue::Chime)); S.Events.Add(Cue(T0 + 0.1, EDmCue::Crackle)); S.Events.Add(Cue(T0 + 0.1, EDmCue::Shimmer));
				S.Events.Add(Cue(T0 + 0.1, EDmCue::Rainfall)); S.Events.Add(Cue(T0 + 0.1, EDmCue::Fall, 1.f, 2.5f));
				S.Events.Add(Cue(T0 + 0.1, EDmCue::Impact, 1.f, 1.2f)); S.Events.Add(Cue(T0 + 0.1, EDmCue::Arm)); S.Events.Add(Cue(T0 + 0.1, EDmCue::Unlock));
				S.Events.Add(Sting(T0 + 0.1, EDmStinger::Wrath)); S.Events.Add(Sting(T0 + 0.2, EDmStinger::Miracle));
			}
			S.Layers = { EDmLayer::Rain, EDmLayer::Wind, EDmLayer::Surf, EDmLayer::Sea, EDmLayer::Birds, EDmLayer::Fire, EDmLayer::Lava, EDmLayer::TownModern, EDmLayer::Leaves };
		}
		else if (Name == TEXT("fast-time"))
		{
			S.Help = TEXT("8x time: a cue every 25 ms for 30 s (every cue), a stinger every 2 s, the age changing every 3 s");
			C.Altitude01 = 0.3f; C.Day = 0.7f; C.Forest = 0.4f; C.Coast = 0.3f; C.Sea = 0.3f; C.Town = 0.5f; C.TownAge = 2;
			const int32 NC = int32(EDmCue::Count);
			for (int32 I = 0; I < 1200; ++I)
			{
				const EDmCue Cu = EDmCue(I % NC);
				const float Param = Cu == EDmCue::Fall ? 2.5f : ((Cu == EDmCue::Impact || Cu == EDmCue::Rumble) ? 1.f : 0.f);
				S.Events.Add(Cue(0.5 + 0.025 * I, Cu, 0.5f + 0.5f * float((I * 7) % 5) / 4.f, Param, 0.f, float((I % 9) - 4) / 5.f));
			}
			for (int32 K = 0; K < 15; ++K) S.Events.Add(Sting(1.0 + 2.0 * K, EDmStinger(K % 4)));
			for (int32 K = 0; K < 10; ++K) S.Events.Add(SetAge(1.5 + 3.0 * K, K % 7));
		}
		else if (Name == TEXT("ground-night"))
		{
			S.Help = TEXT("near the ground at night over a space-age city beside a forest: crickets, the electric hum, leaves");
			C.Altitude01 = 0.f; C.Day = 0.f; C.Forest = 0.5f; C.Open = 0.3f; C.Town = 1.f; C.TownAge = 6; C.Age = 6; C.Coast = 0.f; C.Sea = 0.f;
			S.Events = { Cue(6.0, EDmCue::Crackle, 0.6f), Cue(12.0, EDmCue::Chime, 0.8f) };
			S.Layers = { EDmLayer::TownModern, EDmLayer::Crickets, EDmLayer::Leaves, EDmLayer::Wind };
		}
		else if (Name == TEXT("marathon"))
		{
			S.Help = TEXT("five minutes of change: ages, day and night, altitude, moods, 150 cues and 15 stingers");
			C.Altitude01 = 0.3f; C.Day = 0.5f; C.Forest = 0.4f; C.Open = 0.2f; C.Coast = 0.4f; C.Sea = 0.3f; C.Town = 0.5f; C.TownAge = 3; C.Age = 0;
			for (int32 K = 0; K < 15; ++K)
			{
				const double T = 20.0 * K;
				S.Events.Add(SetAge(T, K % 7));
				S.Events.Add(SetDay(T + 0.5, (K % 2) ? 0.f : 1.f));
				S.Events.Add(SetAltitude(T + 1.0, (K % 3) == 0 ? 0.9f : ((K % 3) == 1 ? 0.1f : 0.5f)));
				S.Events.Add(SetMood(T + 2.0, EDmMood(K % 4), (K % 2) ? 1.f : 0.f));
				S.Events.Add(Sting(T + 3.0, EDmStinger(K % 4)));
			}
			for (int32 K = 0; K < 150; ++K)
				S.Events.Add(Cue(1.0 + 2.0 * K, EDmCue(K % int32(EDmCue::Count)), 0.8f, (K % 7) == 0 ? 1.f : 0.f, 0.f, float(K % 5 - 2) / 4.f));
			S.Layers = { EDmLayer::Space, EDmLayer::Wind, EDmLayer::Surf, EDmLayer::Birds, EDmLayer::Crickets, EDmLayer::TownMiddle, EDmLayer::Rain };
		}
		// ---------------------------------------------------------------- 1: the score alone (music bus, default volumes)
		else if (Name.StartsWith(TEXT("music-age-")))
		{
			const int32 Age = FCString::Atoi(*Name.Mid(10, 1));
			if (Age < 0 || Age > 6) return false;
			static const TCHAR* Names[7] = { TEXT("stone"), TEXT("bronze"), TEXT("iron"), TEXT("sail"), TEXT("industry"), TEXT("atomic"), TEXT("space") };
			S.Help = FString::Printf(TEXT("the %s-age score alone at landscape zoom (all four layers)"), Names[Age]);
			C.Age = Age; C.Altitude01 = 0.3f; Land(C);
			S.Vol.Ambience = 0.f;
		}
		else if (const FMoodSpec* M = FindMood(Name))
		{
			S.Help = FString::Printf(TEXT("mood %s over the %s score, alone"), *Name.Mid(11), M->Age == 5 ? TEXT("atomic") : TEXT("sail"));
			C.Age = M->Age; C.Altitude01 = 0.3f; Land(C);
			if (M->Mood != EDmMood::Count) C.Mood[int32(M->Mood)] = 1.f;
			S.Vol.Ambience = 0.f;
		}
		else if (Name == TEXT("zoom-sweep"))
		{
			S.Help = TEXT("the camera falls from orbit to a town over 48 s: space hum, high air, wind, the land, the town; layers thin and fill");
			C.Age = 3; C.Altitude01 = 1.f; Land(C); C.Town = 0.6f; C.TownAge = 3;
			S.AltRampFrom = 1.0; S.AltRampTo = 0.0;
			S.Events = { Snap(5.0), Snap(47.0) };
			S.Layers = { EDmLayer::Space, EDmLayer::HighAir, EDmLayer::Wind, EDmLayer::Surf, EDmLayer::Birds, EDmLayer::TownMiddle };
		}
		// ---------------------------------------------------------------- 2: ambience alone
		else if (Name == TEXT("ambience-space"))
		{
			S.Help = TEXT("ambience alone: space (zoom01 < 0.12)");
			C.Altitude01 = 0.95f; C.bScore = false; C.Sea = 0.6f; S.Vol.Music = 0.f;
			S.Layers = { EDmLayer::Space, EDmLayer::HighAir };
		}
		else if (Name == TEXT("ambience-orbit"))
		{
			S.Help = TEXT("ambience alone: high orbit (zoom01 0.12-0.3)");
			C.Altitude01 = 0.78f; C.bScore = false; C.Sea = 0.6f; S.Vol.Music = 0.f;
			S.Layers = { EDmLayer::Space, EDmLayer::HighAir, EDmLayer::Wind };
		}
		else if (Name == TEXT("ambience-landscape"))
		{
			S.Help = TEXT("ambience alone: landscape (zoom01 0.5-0.8), a temperate forest on a coast by day");
			C.Altitude01 = 0.32f; C.bScore = false; C.Day = 1.f; C.Coast = 0.6f; C.Sea = 0.4f; C.Forest = 0.6f; C.Open = 0.2f; S.Vol.Music = 0.f;
			S.Layers = { EDmLayer::Surf, EDmLayer::Sea, EDmLayer::Birds, EDmLayer::Leaves, EDmLayer::Wind };
		}
		else if (Name == TEXT("ambience-town"))
		{
			S.Help = TEXT("ambience alone: a sail-age harbour town (zoom01 > 0.8)");
			C.Altitude01 = 0.1f; C.bScore = false; C.Day = 1.f; C.Coast = 0.6f; C.Sea = 0.3f; C.Forest = 0.3f; C.Town = 0.9f; C.TownAge = 3; S.Vol.Music = 0.f;
			S.Layers = { EDmLayer::TownMiddle, EDmLayer::Surf, EDmLayer::Birds, EDmLayer::Wind };
		}
		else if (Name == TEXT("desert-night"))
		{
			S.Help = TEXT("a desert at night: where silence lives (wind at -8 dB), under the stone-age score");
			C.Altitude01 = 0.3f; C.Age = 0; C.Day = 0.f; C.Desert = 1.f;
			S.Layers = { EDmLayer::Desert, EDmLayer::Wind, EDmLayer::Crickets };
		}
		else if (Name == TEXT("tundra"))
		{
			S.Help = TEXT("tundra and ice by night: thin whistling wind, ice creaks, a deep groan");
			C.Altitude01 = 0.3f; C.Age = 1; C.Day = 0.1f; C.Cold = 1.f; C.Mountain = 0.3f;
			S.Layers = { EDmLayer::Cold, EDmLayer::Wind, EDmLayer::Rockfall };
		}
		else if (Name == TEXT("rainforest"))
		{
			S.Help = TEXT("rainforest by day: dense birds, a dove, insects, drips");
			C.Altitude01 = 0.28f; C.Age = 2; C.Day = 1.f; C.Rainforest = 1.f;
			S.Layers = { EDmLayer::Rainforest, EDmLayer::Birds, EDmLayer::Leaves };
		}
		else if (Name == TEXT("wetland-night"))
		{
			S.Help = TEXT("a wetland at night: bubbles and a chorus of frogs");
			C.Altitude01 = 0.28f; C.Age = 3; C.Day = 0.f; C.Wetland = 1.f; C.Open = 0.2f;
			S.Layers = { EDmLayer::Wetland, EDmLayer::Crickets, EDmLayer::Grass };
		}
		else if (Name == TEXT("mountain"))
		{
			S.Help = TEXT("mountains by day: gusty wind and a distant rockfall");
			C.Altitude01 = 0.3f; C.Age = 4; C.Day = 0.9f; C.Mountain = 1.f; C.Cold = 0.2f;
			S.Layers = { EDmLayer::Rockfall, EDmLayer::Wind, EDmLayer::Cold };
		}
		// ---------------------------------------------------------------- 5.2 ducking, endings
		else if (Name == TEXT("ducking"))
		{
			S.Help = TEXT("the sail score over a coast: an impact at 4 s ducks music 10 dB and ambience 6 dB (held while it rings, 3 s release); a choir at 19 s ducks music 4 dB");
			C.Altitude01 = 0.3f; Land(C);
			S.Events = { Snap(3.9), Cue(4.0, EDmCue::Impact, 1.f, 1.f), Snap(4.6), Snap(18.0), Cue(19.0, EDmCue::Choir), Snap(20.5) };
			S.Layers = { EDmLayer::Surf, EDmLayer::Wind };
		}
		else if (Name == TEXT("ending-silence"))
		{
			S.Help = TEXT("the ending of silence: the genesis drone, the 5th drops at 8 s, the wind alone from 16 s, silence from 24 s");
			C.Altitude01 = 0.3f; Land(C);
			S.Events = { Sting(0.5, EDmStinger::EndingSilence), SetPhase(1.0, 5), Cue(5.0, EDmCue::Hover), Cue(5.5, EDmCue::Click) };
			S.Layers = { EDmLayer::Wind, EDmLayer::Surf, EDmLayer::Birds };
		}
		// ---------------------------------------------------------------- one sound alone
		else if (const FInstSpec* I = FindInst(Name))
		{
			S.Help = FString::Printf(TEXT("calibration: one %s note alone (velocity 0.8)"), FDmMusic::InstName(I->Inst));
			C.Altitude01 = 0.5f; C.bScore = false; S.Vol.Ambience = 0.f;
			S.Events = { Note(0.2, I->Inst, I->Var, float(I->Midi), 0.8f, I->Gate) };
		}
		else if (Name.StartsWith(TEXT("cue-")))
		{
			const FCueSpec* Cs = FindCue(Name.Mid(4));
			if (!Cs) return false;
			S.Help = FString::Printf(TEXT("the %s cue alone"), *Name.Mid(4));
			C.Altitude01 = 0.5f; C.bScore = false;
			S.Vol.Ambience = 0.f; S.Vol.Music = 0.f;
			S.Events = { Cue(0.3, Cs->Cue, 1.f, Cs->Param, Cs->Muffle, 0.f, nullptr, Cs->Var) };
			S.bMusicTrack = false;
		}
		else if (Name.StartsWith(TEXT("stinger-")))
		{
			EDmStinger St = EDmStinger::Age;
			if (!DmAudio::StingerFromName(Name.Mid(8), St)) return false;
			S.Help = FString::Printf(TEXT("the %s stinger alone"), DmAudio::StingerName(St));
			C.Altitude01 = 0.5f; C.bScore = false;
			S.Vol.Ambience = 0.f;
			S.Events = { Sting(0.3, St) };
		}
		else return false;
		return true;
	}

	float TailDb(const FDmAudioRender& R, double Sec)
	{
		const int32 N = R.L.Num();
		return RangeDb(R, double(FMath::Max(0, N - int32(Sec * R.Sr))) / R.Sr, double(N) / R.Sr);
	}
}

TArray<FString> DmAudioTest::Scripts()
{
	TArray<FString> Out = { TEXT("orbit"), TEXT("coast-day"), TEXT("forest-night"), TEXT("city"), TEXT("storm"), TEXT("meteor"), TEXT("age-advance"),
		TEXT("stone"), TEXT("industry"), TEXT("space"), TEXT("genesis-molten"), TEXT("genesis-water"), TEXT("war"),
		TEXT("probe-surf"), TEXT("stress-all"), TEXT("fast-time"), TEXT("ground-night"), TEXT("marathon"),
		TEXT("zoom-sweep"), TEXT("ducking"), TEXT("ending-silence"),
		TEXT("ambience-space"), TEXT("ambience-orbit"), TEXT("ambience-landscape"), TEXT("ambience-town"),
		TEXT("desert-night"), TEXT("tundra"), TEXT("rainforest"), TEXT("wetland-night"), TEXT("mountain") };
	for (int32 A = 0; A < 7; ++A) Out.Add(FString::Printf(TEXT("music-age-%d"), A));
	for (const FMoodSpec& M : GMoods) Out.Add(M.Name);
	for (const FCueSpec& C : GCues) Out.Add(FString(TEXT("cue-")) + C.Name);
	for (int32 K = 0; K < int32(EDmStinger::Count); ++K) Out.Add(FString(TEXT("stinger-")) + DmAudio::StingerName(EDmStinger(K)));
	for (const FInstSpec& I : GInst) Out.Add(I.Name);
	return Out;
}

bool DmAudioTest::IsCalibration(const FString& Script) { return Script.StartsWith(TEXT("inst-")); }

double DmAudioTest::DefaultSeconds(const FString& Script)
{
	if (Script.StartsWith(TEXT("cue-"))) { const FCueSpec* C = FindCue(Script.Mid(4)); return C ? C->Sec : 4.0; }
	if (Script.StartsWith(TEXT("inst-"))) { const FInstSpec* I = FindInst(Script); return I ? FMath::Max(3.0, double(I->Gate) + 4.0) : 4.0; }
	if (Script.StartsWith(TEXT("stinger-")))
	{
		EDmStinger S = EDmStinger::Age;
		if (!DmAudio::StingerFromName(Script.Mid(8), S)) return 10.0;
		switch (S)
		{
		case EDmStinger::Age: return 10.0;
		case EDmStinger::Founding: return 5.0;
		case EDmStinger::Miracle: return 9.0;
		case EDmStinger::Wrath: return 9.0;
		case EDmStinger::EndingBeloved: return 25.0;
		case EDmStinger::EndingSilence: case EDmStinger::EndingAsh: case EDmStinger::EndingQuiet: return 28.0;
		default: return 22.0;
		}
	}
	if (Script.StartsWith(TEXT("music-"))) return 60.0;
	if (Script == TEXT("zoom-sweep")) return 48.0;
	if (Script == TEXT("ending-silence")) return 29.0;
	if (Script.StartsWith(TEXT("ambience-"))) return 30.0;
	if (Script == TEXT("age-advance") || Script == TEXT("stone") || Script == TEXT("industry") || Script == TEXT("space") || Script == TEXT("war")) return 24.0;
	if (Script == TEXT("stress-all")) return 14.0;
	if (Script == TEXT("ducking")) return 24.0;
	if (Script == TEXT("fast-time")) return 32.0;
	if (Script == TEXT("marathon")) return 300.0;
	return 20.0;
}

bool DmAudioTest::Render(const FString& Script, double Seconds, FDmAudioRender& Out, uint32 Seed)
{
	FScript S;
	if (!MakeScript(Script, S)) return false;
	S.Ctx.Seed = Seed;
	const float Sr = 48000.f;
	Out = FDmAudioRender();
	Out.Sr = Sr;
	Out.Help = S.Help;
	const int64 Total = int64(Seconds * Sr);
	Out.L.SetNumZeroed(int32(Total));
	Out.R.SetNumZeroed(int32(Total));

	FDmAudioEngine Eng(Sr, Seed);
	Eng.SetVolumes(S.Vol);
	FDmAudioContext Ctx = S.Ctx;
	Eng.SetContext(Ctx);

	for (EDmLayer Ly : S.Layers)
	{
		FDmAudioTrack T;
		T.Name = FString(DmAudio::LayerName(Ly)).ToUpper();
		static const FColor Cols[6] = { FColor(110, 190, 200), FColor(232, 199, 122), FColor(132, 200, 120), FColor(220, 120, 200), FColor(240, 150, 80), FColor(200, 200, 200) };
		T.Color = Cols[Out.Tracks.Num() % 6];
		Out.Tracks.Add(T);
	}
	const int32 MusicTrack = S.bMusicTrack ? Out.Tracks.Num() : -1;
	if (S.bMusicTrack) { FDmAudioTrack T; T.Name = TEXT("MUSIC NOTES/16"); T.Color = FColor(240, 226, 196); Out.Tracks.Add(T); }
	const int32 VoiceTrack = Out.Tracks.Num();
	{ FDmAudioTrack T; T.Name = TEXT("CUE VOICES/8"); T.Color = FColor(232, 96, 80); Out.Tracks.Add(T); }
	const int32 DuckTrack = Out.Tracks.Num();
	{ FDmAudioTrack T; T.Name = TEXT("MUSIC DUCK (0..-12 DB)"); T.Color = FColor(150, 150, 255); Out.Tracks.Add(T); }

	const int32 Step = 2400;   // 50 ms
	double MusicSum = 0;
	int32 Steps = 0;
	for (int64 Pos = 0; Pos < Total; Pos += Step)
	{
		const double T = double(Pos) / Sr;
		if (S.AltRampFrom >= 0.0)
		{
			Ctx.Altitude01 = float(S.AltRampFrom + (S.AltRampTo - S.AltRampFrom) * FMath::Clamp(T / FMath::Max(1.0, Seconds - 2.0), 0.0, 1.0));
			Eng.SetContext(Ctx);
		}
		for (FEv& E : S.Events)
		{
			if (E.bDone || T < E.T) continue;
			E.bDone = true;
			switch (E.Kind)
			{
			case 0: Eng.Play(E.Cue, E.P); break;
			case 1: Eng.Stinger(E.St); break;
			case 2: Ctx.Age = int32(E.Value); Eng.SetContext(Ctx); break;
			case 3: Ctx.Tension = E.Value; Eng.SetContext(Ctx); break;
			case 4: if (Out.NumAtMarker < 8) Out.AtMarker[Out.NumAtMarker++] = Eng.GetLevels(); break;
			case 5: Ctx.Day = E.Value; Eng.SetContext(Ctx); break;
			case 6: Ctx.Altitude01 = E.Value; Eng.SetContext(Ctx); break;
			case 7: Ctx.Mood[FMath::Clamp(E.Index, 0, 3)] = E.Value; Eng.SetContext(Ctx); break;
			case 8: Eng.DebugMusicNote(E.Index, E.Var, E.Midi, E.Value, E.Gate); break;
			case 9: Ctx.Phase = int32(E.Value); Eng.SetContext(Ctx); break;
			default: break;
			}
			if (E.Kind != 4 && E.Kind != 8 && !E.Label.IsEmpty()) { FDmAudioMarker M; M.Time = E.T; M.Label = E.Label; Out.Markers.Add(M); }
		}
		const int32 N = int32(FMath::Min<int64>(Step, Total - Pos));
		Eng.RenderSplit(Out.L.GetData() + Pos, Out.R.GetData() + Pos, N);
		const FDmAudioLevels Lv = Eng.GetLevels();
		for (int32 I = 0; I < S.Layers.Num(); ++I) Out.Tracks[I].Values.Add(Lv.Layer[int32(S.Layers[I])]);
		if (MusicTrack >= 0) Out.Tracks[MusicTrack].Values.Add(FMath::Min(1.f, float(Lv.MusicVoices) / 16.f));
		Out.Tracks[VoiceTrack].Values.Add(FMath::Min(1.f, float(Lv.Voices) / 8.f));
		Out.Tracks[DuckTrack].Values.Add(FMath::Clamp(-Lv.MusicDuckDb / 12.f, 0.f, 1.f));
		Out.MaxVoices = FMath::Max(Out.MaxVoices, Lv.Voices);
		Out.MaxMusicVoices = FMath::Max(Out.MaxMusicVoices, Lv.MusicVoices);
		Out.MaxBeds = FMath::Max(Out.MaxBeds, Lv.Beds);
		MusicSum += Lv.MusicVoices;
		++Steps;
	}
	Out.End = Eng.GetLevels();
	// one more block so the shared snapshot holds the last block's numbers
	{
		float L[256], R[256];
		Eng.RenderSplit(L, R, 256);
		Out.End = Eng.GetLevels();
	}
	Out.RenderSeconds = Out.End.RenderSeconds;
	Out.MeanMusicVoices = Steps ? float(MusicSum / Steps) : 0.f;
	uint32 H = 2166136261u;
	for (int32 K = 0; K < Out.L.Num(); ++K)
	{
		const int32 A = FMath::RoundToInt(FMath::Clamp(Out.L[K], -1.f, 1.f) * 32767.f), B = FMath::RoundToInt(FMath::Clamp(Out.R[K], -1.f, 1.f) * 32767.f);
		H = (H ^ uint32(A & 0xFFFF)) * 16777619u;
		H = (H ^ uint32(B & 0xFFFF)) * 16777619u;
	}
	Out.Hash = H;
	return true;
}

bool DmAudioTest::LevelTarget(const FString& Script, float& Target, float& MeanTol, bool& bShortTerm)
{
	if (Script.StartsWith(TEXT("cue-")))
	{
		const FCueSpec* Cs = FindCue(Script.Mid(4));
		if (!Cs || Cs->TargetSt == 0.f) return false;
		Target = Cs->TargetSt; MeanTol = 2.f; bShortTerm = true;
		return true;
	}
	if (Script.StartsWith(TEXT("music-age-")))
	{
		Target = GAgeLufs[FMath::Clamp(FCString::Atoi(*Script.Mid(10, 1)), 0, 6)]; MeanTol = 1.f; bShortTerm = false;
		return true;
	}
	if (const FMoodSpec* M = FindMood(Script)) { Target = M->Lufs; MeanTol = 1.f; bShortTerm = false; return true; }
	return false;
}

TArray<FDmAudioCheck> DmAudioTest::Checks(const FString& Script, const FDmAudioRender& R, const FDmAudioStats& S, bool bAcrossSeeds)
{
	TArray<FDmAudioCheck> C;
	auto Add = [&C](const FString& Name, bool bPass, const FString& Detail) { FDmAudioCheck K; K.Name = Name; K.bPass = bPass; K.Detail = Detail; C.Add(K); };
	const bool bCue = Script.StartsWith(TEXT("cue-")), bSting = Script.StartsWith(TEXT("stinger-")), bInst = Script.StartsWith(TEXT("inst-"));
	const bool bMix = !bCue && !bSting && !bInst;
	const bool bRainy = Script == TEXT("storm") || Script == TEXT("stress-all") || Script == TEXT("cue-rainfall") || Script == TEXT("fast-time") || Script == TEXT("marathon");

	// 5.4 rule 1
	Add(TEXT("NO NAN"), S.NaN == 0, FString::Printf(TEXT("%lld"), S.NaN));
	Add(TEXT("NO CLIPPING"), S.Clipped == 0, FString::Printf(TEXT("%lld SAMPLES >= 0.999"), S.Clipped));
	Add(TEXT("PEAK <= -0.9 DBFS"), S.PeakDb <= -0.9f, FString::Printf(TEXT("%.2f"), S.PeakDb));
	Add(TEXT("DC < 0.002"), S.Dc < 0.002f, FString::Printf(TEXT("%.5f"), S.Dc));
	// 5.4 rule 3
	Add(TEXT("SUB < 25%"), S.Sub < 0.25f, FString::Printf(TEXT("%.1f%%"), S.Sub * 100.f));
	const double Cpu = R.RenderSeconds / FMath::Max(1e-6, S.Seconds);
	// across seeds the suite judges the cheapest of the renders instead (wall time: other jobs on a loaded machine inflate one)
	if (!bAcrossSeeds) Add(TEXT("CPU < 15% OF REAL TIME"), Cpu < 0.15, FString::Printf(TEXT("%.1f%%"), Cpu * 100.0));
	if (bInst) return C;
	// 5.4 rule 2: nothing above 12 kHz for long (energy share), no harsh 3 s window except in rain
	Add(TEXT("LITTLE ABOVE 12 KHZ (< 1%)"), S.Air12k < 0.01f, FString::Printf(TEXT("%.2f%%"), S.Air12k * 100.f));
	if (bMix)
	{
		// 5.4 rules 2, 4 and 6
		if (!bRainy) Add(TEXT("2-5 KHZ < 30% IN EVERY 3 S"), S.PresenceWorst < 0.30f, FString::Printf(TEXT("%.1f%%"), S.PresenceWorst * 100.f));
		Add(TEXT("SHORT-TERM > -18 LUFS FOR < 20 S"), S.StAboveM18 < 20.f, FString::Printf(TEXT("%.1f S (MAX %.1f LUFS)"), S.StAboveM18, S.StMax));
		if (Script != TEXT("ending-silence") && Script != TEXT("desert-night"))
		{
			Add(TEXT("NEVER SILENT (<5% OF 100MS)"), S.Silent < 0.05f, FString::Printf(TEXT("%.1f%%"), S.Silent * 100.f));
			Add(TEXT("RMS > -60 DBFS"), S.RmsDb > -60.f, FString::Printf(TEXT("%.1f"), S.RmsDb));
		}
		Add(TEXT("WARM (CENTROID < 2500 HZ)"), S.Centroid < 2500.f, FString::Printf(TEXT("%.0f HZ"), S.Centroid));
		Add(TEXT("BEDS <= 6"), R.MaxBeds <= 6, FString::Printf(TEXT("%d"), R.MaxBeds));
	}
	if (bCue || bSting)
	{
		Add(TEXT("AUDIBLE (PEAK > -40 DBFS)"), S.PeakDb > -40.f, FString::Printf(TEXT("%.1f"), S.PeakDb));
		const float Tail = TailDb(R, 0.4);
		Add(TEXT("ENDS (LAST 400MS < -50 DBFS)"), Tail < -50.f, FString::Printf(TEXT("%.1f"), Tail));
	}
	if (bCue)
	{
		const FCueSpec* Cs = FindCue(Script.Mid(4));
		if (Cs && Cs->TargetSt == 0.f)
		{
			// 4: UI sounds fit under the music: peak -14 dBFS or lower, never above -30 LUFS short-term
			Add(TEXT("UI PEAK <= -14 DBFS"), S.PeakDb <= -14.f, FString::Printf(TEXT("%.1f"), S.PeakDb));
			Add(TEXT("UI SHORT-TERM <= -30 LUFS"), S.StMax <= -30.f, FString::Printf(TEXT("%.1f"), S.StMax));
		}
		else if (Cs)
		{
			// 3.2: the peak short-term loudness re music (on a -28 LUFS music bus); across seeds each play may also carry the
			// designed +/- 1.5 dB jitter (the mean over the seeds is held to +/- 2 by the suite)
			const float Tol = bAcrossSeeds ? 4.f : 2.5f;
			Add(FString::Printf(TEXT("LEVEL %.0f +/- %.1f LUFS (3.2)"), Cs->TargetSt, Tol), FMath::Abs(S.StMax - Cs->TargetSt) <= Tol, FString::Printf(TEXT("ST MAX %.1f"), S.StMax));
		}
	}
	if (bSting)
	{
		// stingers stay under the mix's peak and loudness rules (gameplay v4 review)
		Add(TEXT("STINGER SHORT-TERM <= -18 LUFS"), S.StMax <= -18.f, FString::Printf(TEXT("%.1f"), S.StMax));
		Add(TEXT("STINGER PLAYED"), R.End.StingersStarted == 1, FString::Printf(TEXT("%lld"), R.End.StingersStarted));
	}

	auto Layer = [&R](EDmLayer L) { return R.End.Layer[int32(L)]; };
	auto LayerCheck = [&](EDmLayer L, bool bHigh, float Thr)
	{
		const float V = Layer(L);
		Add(FString::Printf(TEXT("%s %s %.2f"), *FString(DmAudio::LayerName(L)).ToUpper(), bHigh ? TEXT(">=") : TEXT("<"), Thr), bHigh ? V >= Thr : V < Thr, FString::Printf(TEXT("%.2f"), V));
	};
	const int32* Notes = R.End.InstNotes;
	auto Inst = [Notes](EDmInst I) { return Notes[int32(I)]; };
	auto InstCheck = [&](EDmInst I, bool bSome)
	{
		const int32 V = Inst(I);
		Add(FString::Printf(TEXT("%s NOTES %s"), *FString(FDmMusic::InstName(I)).ToUpper(), bSome ? TEXT("> 0") : TEXT("= 0")), bSome ? V > 0 : V == 0, FString::Printf(TEXT("%d"), V));
	};
	auto LufsCheck = [&](float Target, float Tol)
	{
		Add(FString::Printf(TEXT("LOUDNESS %.0f +/- %.1f LUFS (5.1)"), Target, Tol), FMath::Abs(S.Lufs - Target) <= Tol, FString::Printf(TEXT("%.1f LUFS"), S.Lufs));
	};
	auto VoiceSilence = [&]()
	{
		const double Busy = R.End.ScoreSec > 0 ? R.End.VoiceBusySec / R.End.ScoreSec : 1.0;
		Add(TEXT("VOICE SILENT >= 50% (1.1)"), Busy <= 0.5, FString::Printf(TEXT("SILENT %.0f%%, %d PHRASES"), 100.0 * (1.0 - Busy), R.End.Phrases));
	};

	// (wave 4b verify) the 5.5 table: peak, RMS range, sub share and the upper bound of the > 4 kHz share, per scenario
	{
		struct F55 { const TCHAR* Name; float PeakMax, RmsLo, RmsHi, SubMax, HighMax; };
		static const F55 T55[] = {
			{ TEXT("orbit"), -12.f, -45.f, -30.f, 0.25f, 0.10f }, { TEXT("coast-day"), -8.f, -36.f, -24.f, 0.20f, 0.30f },
			{ TEXT("forest-night"), -10.f, -40.f, -28.f, 0.15f, 0.25f }, { TEXT("city"), -8.f, -36.f, -24.f, 0.20f, 0.25f },
			{ TEXT("storm"), -3.f, -30.f, -18.f, 0.25f, 0.40f }, { TEXT("meteor"), -1.f, -28.f, -16.f, 0.25f, 0.25f },
			{ TEXT("age-advance"), -4.f, -34.f, -22.f, 0.20f, 0.25f }, { TEXT("stone"), -10.f, -40.f, -28.f, 0.20f, 0.15f },
			{ TEXT("industry"), -8.f, -36.f, -24.f, 0.20f, 0.25f }, { TEXT("space"), -10.f, -40.f, -28.f, 0.15f, 0.20f },
		};
		for (const F55& E : T55)
		{
			if (Script != E.Name) continue;
			Add(FString::Printf(TEXT("5.5 PEAK <= %.0f DBFS"), E.PeakMax), S.PeakDb <= E.PeakMax, FString::Printf(TEXT("%.1f"), S.PeakDb));
			Add(FString::Printf(TEXT("5.5 RMS %.0f..%.0f DBFS"), E.RmsLo, E.RmsHi), S.RmsDb >= E.RmsLo - 0.5f && S.RmsDb <= E.RmsHi + 0.5f, FString::Printf(TEXT("%.1f"), S.RmsDb));
			Add(FString::Printf(TEXT("5.5 SUB < %.0f%%"), E.SubMax * 100.f), S.Sub < E.SubMax, FString::Printf(TEXT("%.1f%%"), S.Sub * 100.f));
			Add(FString::Printf(TEXT("5.5 > 4 KHZ < %.0f%%"), E.HighMax * 100.f), S.High < E.HighMax, FString::Printf(TEXT("%.1f%%"), S.High * 100.f));
		}
	}

	if (Script == TEXT("orbit")) { LayerCheck(EDmLayer::Space, true, 0.5f); LayerCheck(EDmLayer::Birds, false, 0.05f); LayerCheck(EDmLayer::Surf, false, 0.05f); }
	else if (Script == TEXT("coast-day")) { LayerCheck(EDmLayer::Surf, true, 0.6f); LayerCheck(EDmLayer::Birds, true, 0.2f); LayerCheck(EDmLayer::Crickets, false, 0.05f); LayerCheck(EDmLayer::Space, false, 0.05f); }
	else if (Script == TEXT("forest-night")) { LayerCheck(EDmLayer::Crickets, true, 0.5f); LayerCheck(EDmLayer::Birds, false, 0.05f); LayerCheck(EDmLayer::Leaves, true, 0.3f); }
	else if (Script == TEXT("city")) { LayerCheck(EDmLayer::TownMiddle, true, 0.5f); LayerCheck(EDmLayer::TownModern, false, 0.05f); }
	else if (Script == TEXT("storm"))
	{
		LayerCheck(EDmLayer::Rain, true, 0.6f);
		Add(TEXT("THUNDER CUES >= 3"), R.End.CuesStarted >= 3, FString::Printf(TEXT("%lld CUES"), R.End.CuesStarted));
	}
	else if (Script == TEXT("meteor"))
	{
		Add(TEXT("SPECTACLE (LOUD400 > -20 DBFS)"), S.LoudestDb > -20.f, FString::Printf(TEXT("%.1f"), S.LoudestDb));
		Add(TEXT("LIMITER HELD THE CEILING"), R.End.SafetyClips == 0, FString::Printf(TEXT("MIN GAIN %.2f, %lld SAFETY CLAMPS"), R.End.LimiterMinGain, R.End.SafetyClips));
		Add(TEXT("WRATH STINGER PLAYED"), R.End.StingersStarted == 1, FString::Printf(TEXT("%lld"), R.End.StingersStarted));
		Add(TEXT("IMPACT DUCKED THE MUSIC 10 DB (5.2)"), R.End.MinMusicDuckDb <= -9.f, FString::Printf(TEXT("%.1f DB"), R.End.MinMusicDuckDb));
	}
	else if (Script == TEXT("age-advance"))
	{
		const int32 Before = R.NumAtMarker > 0 ? R.AtMarker[0].MusicAge : -1, After = R.End.MusicAge;
		Add(TEXT("MUSIC AGE 2 -> 3"), Before == 2 && After == 3, FString::Printf(TEXT("%d -> %d"), Before, After));
		Add(TEXT("ONE AGE STINGER (THE EXPLICIT ONE MERGED)"), R.End.StingersStarted == 1, FString::Printf(TEXT("%lld"), R.End.StingersStarted));
		InstCheck(EDmInst::Strings, true); InstCheck(EDmInst::Reed, true);
	}
	else if (Script == TEXT("stone"))
	{
		InstCheck(EDmInst::Flute, true); InstCheck(EDmInst::Breath, true); InstCheck(EDmInst::HandDrum, true);
		InstCheck(EDmInst::Piano, false); InstCheck(EDmInst::Pad, false); InstCheck(EDmInst::Strings, false); InstCheck(EDmInst::Lyre, false);
		LayerCheck(EDmLayer::TownEarly, true, 0.2f);
	}
	else if (Script == TEXT("industry"))
	{
		InstCheck(EDmInst::Piano, true); InstCheck(EDmInst::Brass, true); InstCheck(EDmInst::Flute, false); InstCheck(EDmInst::Pad, false);
		LayerCheck(EDmLayer::TownIndustry, true, 0.3f);
	}
	else if (Script == TEXT("space"))
	{
		InstCheck(EDmInst::Pad, true); InstCheck(EDmInst::Glass, true); InstCheck(EDmInst::Lyre, false); InstCheck(EDmInst::Piano, false);
		LayerCheck(EDmLayer::TownModern, true, 0.1f);
	}
	else if (Script == TEXT("genesis-molten"))
	{
		LayerCheck(EDmLayer::Lava, true, 0.7f);
		InstCheck(EDmInst::Breath, true); InstCheck(EDmInst::Flute, false); InstCheck(EDmInst::HandDrum, false); InstCheck(EDmInst::Strings, false);
	}
	else if (Script == TEXT("genesis-water"))
	{
		InstCheck(EDmInst::Breath, true); InstCheck(EDmInst::HandDrum, false); InstCheck(EDmInst::Flute, false);
		Add(TEXT("COMET CUES >= 5"), R.End.CuesStarted >= 5, FString::Printf(TEXT("%lld"), R.End.CuesStarted));
		Add(TEXT("HELD BREATH GAINED ITS 3RD (3 DRONE PARTIALS)"), Inst(EDmInst::Breath) >= 3, FString::Printf(TEXT("%d"), Inst(EDmInst::Breath)));
	}
	else if (Script == TEXT("war"))
	{
		InstCheck(EDmInst::Cello, true); InstCheck(EDmInst::Taiko, true);
		Add(TEXT("WAR MOOD FADED IN (> 0.9)"), R.End.MusicMood[0] > 0.9f, FString::Printf(TEXT("%.2f"), R.End.MusicMood[0]));
	}
	else if (Script == TEXT("probe-surf"))
	{
		const float Rise = MaxLevelRiseDb(R, 0.02);
		Add(TEXT("NO CLICKS (20 MS LEVEL RISE < 8 DB)"), Rise < 8.f, FString::Printf(TEXT("%.1f DB"), Rise));
	}
	else if (Script == TEXT("stress-all"))
	{
		Add(TEXT("LIMITER HELD (NO SAFETY CLAMP)"), R.End.SafetyClips == 0, FString::Printf(TEXT("%lld CLAMPS, MIN GAIN %.2f"), R.End.SafetyClips, R.End.LimiterMinGain));
		Add(TEXT("EFFECT STARTS <= 12 PER SECOND (5.3)"), R.End.MaxStartsPerSec <= 12, FString::Printf(TEXT("%d"), R.End.MaxStartsPerSec));
		Add(TEXT("CUES PLAYED (>= 24)"), R.End.CuesStarted >= 24, FString::Printf(TEXT("%lld STARTED, %lld DROPPED, %lld STOLEN"), R.End.CuesStarted, R.End.CuesDropped, R.End.CuesStolen));
		Add(TEXT("STINGERS PLAYED (>= 2)"), R.End.StingersStarted >= 2, FString::Printf(TEXT("%lld"), R.End.StingersStarted));
		Add(TEXT("VOICES CAPPED (<= 24 + 8 FADING)"), R.MaxVoices <= 32, FString::Printf(TEXT("%d"), R.MaxVoices));
	}
	else if (Script == TEXT("fast-time"))
	{
		Add(TEXT("EFFECT STARTS <= 12 PER SECOND (5.3)"), R.End.MaxStartsPerSec <= 12, FString::Printf(TEXT("%d"), R.End.MaxStartsPerSec));
		Add(TEXT("NO MACHINE GUN (EXTRA STARTS DROPPED)"), R.End.CuesDropped > 0, FString::Printf(TEXT("%lld STARTED, %lld DROPPED"), R.End.CuesStarted, R.End.CuesDropped));
		Add(TEXT("STINGERS 6 S APART (<= 6 IN 32 S)"), R.End.StingersStarted >= 3 && R.End.StingersStarted <= 7, FString::Printf(TEXT("%lld PLAYED, %lld DROPPED"), R.End.StingersStarted, R.End.StingersDropped));
		Add(TEXT("LIMITER HELD (NO SAFETY CLAMP)"), R.End.SafetyClips == 0, FString::Printf(TEXT("%lld CLAMPS"), R.End.SafetyClips));
		Add(TEXT("VOICES CAPPED (<= 32 SLOTS)"), R.MaxVoices <= 32, FString::Printf(TEXT("%d"), R.MaxVoices));
	}
	else if (Script == TEXT("ground-night"))
	{
		LayerCheck(EDmLayer::TownModern, true, 0.5f); LayerCheck(EDmLayer::Crickets, true, 0.3f); LayerCheck(EDmLayer::Space, false, 0.05f);
	}
	else if (Script == TEXT("marathon"))
	{
		Add(TEXT("CUES STARTED (>= 120 OF 150)"), R.End.CuesStarted >= 120, FString::Printf(TEXT("%lld"), R.End.CuesStarted));
		Add(TEXT("STINGERS STARTED (>= 12)"), R.End.StingersStarted >= 12, FString::Printf(TEXT("%lld"), R.End.StingersStarted));
		Add(TEXT("LIMITER HELD (NO SAFETY CLAMP)"), R.End.SafetyClips == 0, FString::Printf(TEXT("%lld CLAMPS"), R.End.SafetyClips));
		Add(TEXT("WHOLE MIX -30..-18 LUFS INTEGRATED"), S.Lufs >= -30.f && S.Lufs <= -18.f, FString::Printf(TEXT("%.1f"), S.Lufs));
	}
	else if (Script == TEXT("zoom-sweep"))
	{
		const FDmAudioLevels& A = R.NumAtMarker > 0 ? R.AtMarker[0] : R.End;
		Add(TEXT("SPACE HUM IN ORBIT (>= 0.5)"), A.Layer[int32(EDmLayer::Space)] >= 0.5f, FString::Printf(TEXT("%.2f"), A.Layer[int32(EDmLayer::Space)]));
		Add(TEXT("SPACE HUM GONE AT THE TOWN"), R.End.Layer[int32(EDmLayer::Space)] < 0.05f, FString::Printf(TEXT("%.2f"), R.End.Layer[int32(EDmLayer::Space)]));
		LayerCheck(EDmLayer::TownMiddle, true, 0.3f);
		InstCheck(EDmInst::Strings, true);
	}
	else if (Script == TEXT("ducking"))
	{
		Add(TEXT("IMPACT: MUSIC -10 DB (DEEPEST, NOT SUMMED)"), R.End.MinMusicDuckDb <= -9.f && R.End.MinMusicDuckDb >= -10.5f, FString::Printf(TEXT("%.1f DB"), R.End.MinMusicDuckDb));
		Add(TEXT("IMPACT: AMBIENCE -6 DB"), R.End.MinAmbDuckDb <= -5.f && R.End.MinAmbDuckDb >= -6.5f, FString::Printf(TEXT("%.1f DB"), R.End.MinAmbDuckDb));
		const float Mid = R.NumAtMarker > 1 ? R.AtMarker[1].MusicDuckDb : 0.f;
		Add(TEXT("IMPACT DUCK REACHED IN 600 MS"), Mid <= -7.f, FString::Printf(TEXT("%.1f DB AT 4.6 S"), Mid));
		const float Rec = R.NumAtMarker > 2 ? R.AtMarker[2].MusicDuckDb : -99.f;
		Add(TEXT("RELEASED AFTER THE IMPACT"), Rec > -1.f, FString::Printf(TEXT("%.1f DB AT 18 S"), Rec));
		const float Ch = R.NumAtMarker > 3 ? R.AtMarker[3].MusicDuckDb : 0.f;
		Add(TEXT("CHOIR: MUSIC -4 DB"), Ch <= -3.f && Ch >= -4.5f, FString::Printf(TEXT("%.1f DB AT 20.5 S"), Ch));
	}
	else if (Script == TEXT("ending-silence"))
	{
		const float Drone = RangeDb(R, 2.0, 7.0), Wind = RangeDb(R, 17.0, 20.0), Silence = RangeDb(R, 25.0, 28.0);
		Add(TEXT("THE DRONE SOUNDS (2-7 S > -60 DBFS)"), Drone > -60.f, FString::Printf(TEXT("%.1f"), Drone));
		Add(TEXT("THE WIND ALONE (17-20 S > -70 DBFS)"), Wind > -70.f, FString::Printf(TEXT("%.1f"), Wind));
		Add(TEXT("TRUE SILENCE (25-28 S < -60 DBFS)"), Silence < -60.f, FString::Printf(TEXT("%.1f"), Silence));
		Add(TEXT("UI MUTED EXCEPT CLICK"), R.End.CuesDropped >= 1, FString::Printf(TEXT("%lld DROPPED"), R.End.CuesDropped));
	}
	else if (Script == TEXT("ambience-space")) { LufsCheck(-38.f, 2.f); LayerCheck(EDmLayer::Space, true, 0.8f); }
	else if (Script == TEXT("ambience-orbit")) { LufsCheck(-36.f, 2.f); LayerCheck(EDmLayer::HighAir, true, 0.5f); LayerCheck(EDmLayer::Space, true, 0.3f); }
	else if (Script == TEXT("ambience-landscape")) { LufsCheck(-32.f, 2.f); LayerCheck(EDmLayer::Birds, true, 0.3f); LayerCheck(EDmLayer::Surf, true, 0.5f); }
	else if (Script == TEXT("ambience-town")) { LufsCheck(-32.f, 2.f); LayerCheck(EDmLayer::TownMiddle, true, 0.6f); }
	else if (Script == TEXT("desert-night")) { LayerCheck(EDmLayer::Desert, true, 0.3f); LayerCheck(EDmLayer::Birds, false, 0.05f); Add(TEXT("QUIET BUT THERE (RMS > -60)"), S.RmsDb > -60.f, FString::Printf(TEXT("%.1f"), S.RmsDb)); }
	else if (Script == TEXT("tundra")) { LayerCheck(EDmLayer::Cold, true, 0.6f); LayerCheck(EDmLayer::Crickets, false, 0.05f); }
	else if (Script == TEXT("rainforest")) { LayerCheck(EDmLayer::Rainforest, true, 0.6f); LayerCheck(EDmLayer::Birds, true, 0.5f); }
	else if (Script == TEXT("wetland-night")) { LayerCheck(EDmLayer::Wetland, true, 0.6f); LayerCheck(EDmLayer::Birds, false, 0.05f); }
	else if (Script == TEXT("mountain")) { LayerCheck(EDmLayer::Rockfall, true, 0.6f); LayerCheck(EDmLayer::Wind, true, 0.3f); }
	else if (Script.StartsWith(TEXT("music-age-")))
	{
		const int32 Age = FCString::Atoi(*Script.Mid(10, 1));
		LufsCheck(GAgeLufs[FMath::Clamp(Age, 0, 6)], bAcrossSeeds ? 2.f : 1.5f);
		VoiceSilence();
		Add(TEXT("PHRASES >= 3"), R.End.Phrases >= 3, FString::Printf(TEXT("%d"), R.End.Phrases));
		static const float TLo[7] = { 56.f, 60.f, 64.f, 66.f, 72.f, 76.f, 52.f }, THi[7] = { 66.f, 70.f, 76.f, 80.f, 92.f, 96.f, 64.f };
		const int32 A = FMath::Clamp(Age, 0, 6);
		Add(FString::Printf(TEXT("TEMPO %.0f-%.0f BPM"), TLo[A], THi[A]), R.End.MusicTempo >= TLo[A] - 0.5f && R.End.MusicTempo <= THi[A] + 0.5f, FString::Printf(TEXT("%.1f"), R.End.MusicTempo));
		switch (A)
		{
		case 0: InstCheck(EDmInst::Flute, true); InstCheck(EDmInst::HandDrum, true); InstCheck(EDmInst::Breath, true); InstCheck(EDmInst::Lyre, false); break;
		case 1: InstCheck(EDmInst::Flute, true); InstCheck(EDmInst::Lyre, true); InstCheck(EDmInst::HandDrum, true); break;
		case 2: InstCheck(EDmInst::Reed, true); InstCheck(EDmInst::Lyre, true); InstCheck(EDmInst::Cello, true); InstCheck(EDmInst::Perc, true); break;
		case 3: InstCheck(EDmInst::Strings, true); InstCheck(EDmInst::Bell, true); InstCheck(EDmInst::Lyre, true); break;
		case 4: InstCheck(EDmInst::Piano, true); InstCheck(EDmInst::Brass, true); InstCheck(EDmInst::Lyre, true); break;
		case 5: InstCheck(EDmInst::Piano, true); InstCheck(EDmInst::Organ, true); InstCheck(EDmInst::Perc, true); break;
		default: InstCheck(EDmInst::Glass, true); InstCheck(EDmInst::Pad, true); InstCheck(EDmInst::Breath, true); InstCheck(EDmInst::Perc, true); break;
		}
	}
	else if (const FMoodSpec* M = FindMood(Script))
	{
		LufsCheck(M->Lufs, bAcrossSeeds ? 2.f : 1.5f);
		VoiceSilence();
		if (M->Mood != EDmMood::Count)
			Add(FString::Printf(TEXT("%s MOOD AT FULL"), *FString(FDmMusic::MoodName(M->Mood)).ToUpper()), R.End.MusicMood[int32(M->Mood)] > 0.95f, FString::Printf(TEXT("%.2f"), R.End.MusicMood[int32(M->Mood)]));
		switch (M->Mood)
		{
		case EDmMood::War: InstCheck(EDmInst::Taiko, true); InstCheck(EDmInst::Cello, true); break;
		case EDmMood::Plague: Add(TEXT("A TOLL EVERY 16-24 S (>= 2 BELLS)"), Inst(EDmInst::Bell) >= 2, FString::Printf(TEXT("%d"), Inst(EDmInst::Bell))); break;
		case EDmMood::Doom: InstCheck(EDmInst::Cluster, true); InstCheck(EDmInst::Rafters, true); break;
		case EDmMood::Golden: Add(TEXT("SPARKLES AND FULL BELL CHORDS (>= 10 BELLS)"), Inst(EDmInst::Bell) >= 10, FString::Printf(TEXT("%d"), Inst(EDmInst::Bell))); break;
		default: break;
		}
	}
	return C;
}
