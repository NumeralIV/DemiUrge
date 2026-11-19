// DEMIURGE — Game/Audio/DmAudioTest.cpp : scripted offline renders and their checks. Owner: C9 audio.
#include "Game/Audio/DmAudioTest.h"
#include "Game/Audio/DmAudioMusic.h"
#include <cmath>

namespace
{
	struct FEv
	{
		double T = 0;
		uint8 Kind = 0;          // 0 cue, 1 stinger, 2 age, 3 tension, 4 snapshot
		EDmCue Cue = EDmCue::Click;
		FDmCueParams P;
		EDmStinger St = EDmStinger::Age;
		float Value = 0.f;
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
	};

	FEv Cue(double T, EDmCue C, float Vol = 1.f, float Param = 0.f, float Muffle = 0.f, float Pan = 0.f, const TCHAR* Label = nullptr)
	{
		FEv E; E.T = T; E.Kind = 0; E.Cue = C; E.P.Vol = Vol; E.P.Param = Param; E.P.Muffle = Muffle; E.P.Pan = Pan;
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
	// verifier additions: the sunlight and the altitude change mid-script (Kind 5 = Day, 6 = Altitude01)
	FEv SetDay(double T, float V) { FEv E; E.T = T; E.Kind = 5; E.Value = V; E.Label = FString::Printf(TEXT("DAY %.2f"), V); return E; }
	FEv SetAltitude(double T, float V) { FEv E; E.T = T; E.Kind = 6; E.Value = V; E.Label = FString::Printf(TEXT("ALT %.2f"), V); return E; }

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

	double CueSeconds(EDmCue C)
	{
		static const double Sec[int32(EDmCue::Count)] = { 1.5, 1.5, 2.5, 2.0, 3.5, 8.5, 10.0, 6.5, 8.0, 3.8, 5.5, 6.5, 7.0, 5.0, 9.0, 10.0, 4.0, 4.5 };
		const int32 K = int32(C);
		return (K >= 0 && K < int32(EDmCue::Count)) ? Sec[K] : 4.0;
	}
	double StingerSeconds(EDmStinger S)
	{
		switch (S)
		{
		case EDmStinger::Age: return 10.0;
		case EDmStinger::Founding: return 6.0;
		case EDmStinger::Miracle: return 9.0;
		case EDmStinger::Wrath: return 9.0;
		default: return 17.0;
		}
	}

	bool MakeScript(const FString& Name, FScript& S)
	{
		FDmAudioContext& C = S.Ctx;
		C.Phase = 4; C.Age = 3; C.Day = 0.8f;
		if (Name == TEXT("orbit"))
		{
			S.Help = TEXT("whole planet in view, day side");
			C.Altitude01 = 1.f; C.Day = 0.6f; C.Sea = 0.65f; C.Coast = 0.15f; C.Forest = 0.15f; C.Open = 0.1f;
			S.Layers = { EDmLayer::Space, EDmLayer::Wind, EDmLayer::Surf, EDmLayer::Birds };
		}
		else if (Name == TEXT("coast-day"))
		{
			S.Help = TEXT("low over a sunny coast");
			C.Altitude01 = 0.14f; C.Day = 1.f; C.Coast = 0.85f; C.Sea = 0.5f; C.Open = 0.35f; C.Forest = 0.15f;
			S.Events = { Cue(5.0, EDmCue::Hover), Cue(5.3, EDmCue::Click) };
			S.Layers = { EDmLayer::Surf, EDmLayer::Sea, EDmLayer::Birds, EDmLayer::Wind };
		}
		else if (Name == TEXT("forest-night"))
		{
			S.Help = TEXT("low over a forest at night");
			C.Altitude01 = 0.12f; C.Day = 0.03f; C.Forest = 0.9f; C.Open = 0.1f;
			S.Layers = { EDmLayer::Crickets, EDmLayer::Leaves, EDmLayer::Birds, EDmLayer::Wind };
		}
		else if (Name == TEXT("city"))
		{
			S.Help = TEXT("over a sail-age town by day");
			C.Altitude01 = 0.16f; C.Day = 0.85f; C.Town = 0.85f; C.TownAge = 3; C.Coast = 0.2f; C.Sea = 0.1f; C.Open = 0.2f;
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
				Cue(4.3, EDmCue::Rumble, 1.f, 1.f), Cue(6.5, EDmCue::Wave, 0.9f), SetTension(4.0, 0.6f) };
			S.Layers = { EDmLayer::Wind, EDmLayer::Surf, EDmLayer::Birds };
		}
		else if (Name == TEXT("age-advance"))
		{
			S.Help = TEXT("iron age becomes the age of sail at 8 s");
			C.Altitude01 = 0.5f; C.Age = 2; C.Forest = 0.3f; C.Town = 0.3f; C.Coast = 0.3f; C.Sea = 0.3f;
			S.Events = { Snap(7.9), SetAge(8.0, 3), Sting(8.0, EDmStinger::Age), Snap(23.9) };
			S.Layers = { EDmLayer::Wind, EDmLayer::Surf, EDmLayer::TownMiddle };
		}
		else if (Name == TEXT("stone"))
		{
			S.Help = TEXT("stone age: bone flute, hand drum, a breathing drone");
			C.Altitude01 = 0.42f; C.Age = 0; C.Forest = 0.45f; C.Open = 0.3f; C.Town = 0.35f; C.TownAge = 0;
			S.Layers = { EDmLayer::TownEarly, EDmLayer::Birds, EDmLayer::Wind };
		}
		else if (Name == TEXT("industry"))
		{
			S.Help = TEXT("industry: brass, piano, strings; engines in town");
			C.Altitude01 = 0.3f; C.Age = 4; C.Town = 0.6f; C.TownAge = 4; C.Coast = 0.2f; C.Day = 0.7f;
			S.Layers = { EDmLayer::TownIndustry, EDmLayer::Wind, EDmLayer::Surf };
		}
		else if (Name == TEXT("space"))
		{
			S.Help = TEXT("space age: synth pads and glass; city hum at dusk");
			C.Altitude01 = 0.45f; C.Age = 6; C.Town = 0.45f; C.TownAge = 6; C.Day = 0.35f; C.Sea = 0.3f;
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
			S.Help = TEXT("comets fill the seas");
			C.Phase = 1; C.Age = 0; C.Altitude01 = 0.8f; C.Sea = 0.6f; C.Day = 0.7f;
			S.Events = { Cue(1.0, EDmCue::Fall, 0.9f, 2.5f), Cue(3.5, EDmCue::Impact, 0.6f, 0.5f), Cue(3.8, EDmCue::Wave, 0.6f),
				Cue(10.0, EDmCue::Fall, 0.9f, 2.5f), Cue(12.5, EDmCue::Impact, 0.6f, 0.5f) };
			S.Layers = { EDmLayer::Space, EDmLayer::Wind, EDmLayer::Sea };
		}
		else if (Name == TEXT("war"))
		{
			S.Help = TEXT("war in the iron age: the darker layer");
			C.Altitude01 = 0.45f; C.Age = 2; C.Town = 0.4f; C.TownAge = 2; C.Day = 0.6f; C.Open = 0.3f;
			S.Events = { SetTension(3.0, 0.9f), Cue(4.0, EDmCue::Horn), Cue(10.0, EDmCue::Toll), Cue(14.0, EDmCue::Horn, 0.8f, 0.f, 0.3f, 0.5f), Snap(23.9) };
			S.Layers = { EDmLayer::TownMiddle, EDmLayer::Wind };
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
			S.Help = TEXT("worst case: every layer at full near the ground, two dense bursts of cues and stingers on top");
			C.Altitude01 = 0.f; C.Day = 0.5f; C.Sea = 1.f; C.Coast = 1.f; C.Forest = 1.f; C.Open = 1.f; C.Desert = 1.f; C.Cold = 1.f;
			C.Mountain = 1.f; C.Town = 1.f; C.TownAge = 6; C.Fire = 1.f; C.Lava = 1.f; C.Storm = 1.f; C.Rain = 1.f; C.Tension = 1.f;
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
			S.Layers = { EDmLayer::Rain, EDmLayer::Wind, EDmLayer::Surf, EDmLayer::Sea, EDmLayer::Birds, EDmLayer::Crickets, EDmLayer::Fire,
				EDmLayer::Lava, EDmLayer::TownModern, EDmLayer::Leaves, EDmLayer::Space };
		}
		else if (Name == TEXT("fast-time"))
		{
			S.Help = TEXT("8x time: a cue every 25 ms for 30 s (all 18 cues), a stinger every 2 s, the age changing every 3 s");
			C.Altitude01 = 0.5f; C.Day = 0.7f; C.Forest = 0.4f; C.Coast = 0.3f; C.Sea = 0.3f; C.Town = 0.5f; C.TownAge = 2;
			static const EDmCue All[18] = { EDmCue::Hover, EDmCue::Click, EDmCue::Arm, EDmCue::Deny, EDmCue::Unlock, EDmCue::Thunder, EDmCue::Impact,
				EDmCue::Rumble, EDmCue::Wave, EDmCue::Rainfall, EDmCue::Shimmer, EDmCue::Chime, EDmCue::Choir, EDmCue::Horn, EDmCue::Toll,
				EDmCue::Roar, EDmCue::Crackle, EDmCue::Fall };
			for (int32 I = 0; I < 1200; ++I)
			{
				const EDmCue Cu = All[I % 18];
				const float Param = Cu == EDmCue::Fall ? 2.5f : ((Cu == EDmCue::Impact || Cu == EDmCue::Rumble) ? 1.f : 0.f);
				S.Events.Add(Cue(0.5 + 0.025 * I, Cu, 0.5f + 0.5f * float((I * 7) % 5) / 4.f, Param, 0.f, float((I % 9) - 4) / 5.f));
			}
			for (int32 K = 0; K < 15; ++K) S.Events.Add(Sting(1.0 + 2.0 * K, EDmStinger(K % int32(EDmStinger::Count))));
			for (int32 K = 0; K < 10; ++K) S.Events.Add(SetAge(1.5 + 3.0 * K, K % 7));
		}
		else if (Name == TEXT("ground-night"))
		{
			S.Help = TEXT("near the ground at night over a modern city beside a forest: crickets, traffic, leaves");
			C.Altitude01 = 0.f; C.Day = 0.f; C.Forest = 0.5f; C.Open = 0.3f; C.Town = 1.f; C.TownAge = 6; C.Age = 6; C.Coast = 0.f; C.Sea = 0.f;
			S.Events = { Cue(6.0, EDmCue::Crackle, 0.6f), Cue(12.0, EDmCue::Chime, 0.8f), Sting(14.0, EDmStinger::Age) };
			S.Layers = { EDmLayer::TownModern, EDmLayer::Crickets, EDmLayer::Leaves, EDmLayer::Wind };
		}
		else if (Name == TEXT("marathon"))
		{
			S.Help = TEXT("five minutes of change: ages, day and night, altitude, war, 150 cues and 15 stingers");
			C.Altitude01 = 0.3f; C.Day = 0.5f; C.Forest = 0.4f; C.Open = 0.2f; C.Coast = 0.4f; C.Sea = 0.3f; C.Town = 0.5f; C.TownAge = 3; C.Age = 0;
			for (int32 K = 0; K < 15; ++K)
			{
				const double T = 20.0 * K;
				S.Events.Add(SetAge(T, K % 7));
				S.Events.Add(SetDay(T + 0.5, (K % 2) ? 0.f : 1.f));
				S.Events.Add(SetAltitude(T + 1.0, (K % 3) == 0 ? 0.9f : ((K % 3) == 1 ? 0.1f : 0.5f)));
				S.Events.Add(SetTension(T + 2.0, (K % 4) == 3 ? 0.9f : 0.2f));
				S.Events.Add(Sting(T + 3.0, EDmStinger(K % 9)));
			}
			for (int32 K = 0; K < 150; ++K)
				S.Events.Add(Cue(1.0 + 2.0 * K, EDmCue(K % int32(EDmCue::Count)), 0.8f, (K % 7) == 0 ? 1.f : 0.f, 0.f, float(K % 5 - 2) / 4.f));
			S.Layers = { EDmLayer::Space, EDmLayer::Wind, EDmLayer::Surf, EDmLayer::Birds, EDmLayer::Crickets, EDmLayer::TownMiddle, EDmLayer::Rain };
		}
		else if (Name.StartsWith(TEXT("cue-")))
		{
			EDmCue Cu = EDmCue::Click;
			if (!DmAudio::CueFromName(Name.Mid(4), Cu)) return false;
			S.Help = FString::Printf(TEXT("the %s cue alone"), DmAudio::CueName(Cu));
			C.Altitude01 = 0.5f; C.bScore = false;
			S.Vol.Ambience = 0.f; S.Vol.Music = 0.f;
			const float Param = Cu == EDmCue::Fall ? 2.5f : ((Cu == EDmCue::Impact || Cu == EDmCue::Rumble) ? 1.f : 0.f);
			S.Events = { Cue(0.3, Cu, 1.f, Param) };
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
		const int32 K0 = FMath::Max(0, N - int32(Sec * R.Sr));
		double P = 0;
		for (int32 K = K0; K < N; ++K) P += double(R.L[K]) * R.L[K] + double(R.R[K]) * R.R[K];
		P /= FMath::Max(1.0, 2.0 * double(N - K0));
		return float(10.0 * std::log10(FMath::Max(P, 1e-20)));
	}
}

TArray<FString> DmAudioTest::Scripts()
{
	TArray<FString> Out = { TEXT("orbit"), TEXT("coast-day"), TEXT("forest-night"), TEXT("city"), TEXT("storm"), TEXT("meteor"), TEXT("age-advance"),
		TEXT("stone"), TEXT("industry"), TEXT("space"), TEXT("genesis-molten"), TEXT("genesis-water"), TEXT("war"),
		// verifier's harder scripts (audio-verify): click probe, worst-case stack, 8x event storm, night at ground level, 5 minutes of change
		TEXT("probe-surf"), TEXT("stress-all"), TEXT("fast-time"), TEXT("ground-night"), TEXT("marathon") };
	for (int32 K = 0; K < int32(EDmCue::Count); ++K) Out.Add(FString(TEXT("cue-")) + DmAudio::CueName(EDmCue(K)));
	for (int32 K = 0; K < int32(EDmStinger::Count); ++K) Out.Add(FString(TEXT("stinger-")) + DmAudio::StingerName(EDmStinger(K)));
	return Out;
}

double DmAudioTest::DefaultSeconds(const FString& Script)
{
	if (Script.StartsWith(TEXT("cue-")))
	{
		EDmCue C = EDmCue::Click;
		return DmAudio::CueFromName(Script.Mid(4), C) ? CueSeconds(C) : 4.0;
	}
	if (Script.StartsWith(TEXT("stinger-")))
	{
		EDmStinger S = EDmStinger::Age;
		return DmAudio::StingerFromName(Script.Mid(8), S) ? StingerSeconds(S) : 10.0;
	}
	if (Script == TEXT("age-advance") || Script == TEXT("stone") || Script == TEXT("industry") || Script == TEXT("space") || Script == TEXT("war")) return 24.0;
	if (Script == TEXT("stress-all")) return 14.0;
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

	const int32 Step = 2400;   // 50 ms
	double MusicSum = 0;
	int32 Steps = 0;
	for (int64 Pos = 0; Pos < Total; Pos += Step)
	{
		const double T = double(Pos) / Sr;
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
			default: break;
			}
			if (E.Kind != 4 && !E.Label.IsEmpty()) { FDmAudioMarker M; M.Time = E.T; M.Label = E.Label; Out.Markers.Add(M); }
		}
		const int32 N = int32(FMath::Min<int64>(Step, Total - Pos));
		Eng.RenderSplit(Out.L.GetData() + Pos, Out.R.GetData() + Pos, N);
		const FDmAudioLevels Lv = Eng.GetLevels();
		for (int32 I = 0; I < S.Layers.Num(); ++I) Out.Tracks[I].Values.Add(Lv.Layer[int32(S.Layers[I])]);
		if (MusicTrack >= 0) Out.Tracks[MusicTrack].Values.Add(FMath::Min(1.f, float(Lv.MusicVoices) / 16.f));
		Out.Tracks[VoiceTrack].Values.Add(FMath::Min(1.f, float(Lv.Voices) / 8.f));
		Out.MaxVoices = FMath::Max(Out.MaxVoices, Lv.Voices);
		Out.MaxMusicVoices = FMath::Max(Out.MaxMusicVoices, Lv.MusicVoices);
		MusicSum += Lv.MusicVoices;
		++Steps;
	}
	Out.End = Eng.GetLevels();
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

TArray<FDmAudioCheck> DmAudioTest::Checks(const FString& Script, const FDmAudioRender& R, const FDmAudioStats& S)
{
	TArray<FDmAudioCheck> C;
	auto Add = [&C](const FString& Name, bool bPass, const FString& Detail) { FDmAudioCheck K; K.Name = Name; K.bPass = bPass; K.Detail = Detail; C.Add(K); };
	const bool bCue = Script.StartsWith(TEXT("cue-")), bSting = Script.StartsWith(TEXT("stinger-"));
	const bool bMix = !bCue && !bSting;

	Add(TEXT("NO NAN"), S.NaN == 0, FString::Printf(TEXT("%lld"), S.NaN));
	Add(TEXT("NO CLIPPING"), S.Clipped == 0, FString::Printf(TEXT("%lld SAMPLES >= 0.999"), S.Clipped));
	Add(TEXT("PEAK <= -0.9 DBFS"), S.PeakDb <= -0.9f, FString::Printf(TEXT("%.2f"), S.PeakDb));
	Add(TEXT("DC < 0.002"), S.Dc < 0.002f, FString::Printf(TEXT("%.5f"), S.Dc));
	Add(TEXT("SUB < 25%"), S.Sub < 0.25f, FString::Printf(TEXT("%.1f%%"), S.Sub * 100.f));
	const double Cpu = R.RenderSeconds / FMath::Max(1e-6, S.Seconds);
	Add(TEXT("CPU < 15% OF REAL TIME"), Cpu < 0.15, FString::Printf(TEXT("%.1f%%"), Cpu * 100.0));
	if (bMix)
	{
		Add(TEXT("RMS -42..-14 DBFS"), S.RmsDb >= -42.f && S.RmsDb <= -14.f, FString::Printf(TEXT("%.1f"), S.RmsDb));
		Add(TEXT("NEVER SILENT (<5% OF 100MS)"), S.Silent < 0.05f, FString::Printf(TEXT("%.1f%%"), S.Silent * 100.f));
		Add(TEXT("QUIETEST 400MS > -50 DBFS"), S.QuietestDb > -50.f, FString::Printf(TEXT("%.1f"), S.QuietestDb));
		Add(TEXT("NOT HARSH (2.5-6K < 12%)"), S.Harsh < 0.12f, FString::Printf(TEXT("%.1f%%"), S.Harsh * 100.f));
		Add(TEXT("WARM (CENTROID < 2500 HZ)"), S.Centroid < 2500.f, FString::Printf(TEXT("%.0f HZ"), S.Centroid));
	}
	if (bCue || bSting)
	{
		Add(TEXT("AUDIBLE (PEAK > -36 DBFS)"), S.PeakDb > -36.f, FString::Printf(TEXT("%.1f"), S.PeakDb));
		const float Tail = TailDb(R, 0.4);
		Add(TEXT("ENDS (LAST 400MS < -50 DBFS)"), Tail < -50.f, FString::Printf(TEXT("%.1f"), Tail));
		const bool bGrainy = Script == TEXT("cue-rainfall") || Script == TEXT("cue-crackle") || Script == TEXT("cue-shimmer") || Script == TEXT("cue-thunder");
		const float Lim = bGrainy ? 0.25f : 0.15f;
		Add(FString::Printf(TEXT("NOT HARSH (< %.0f%%)"), Lim * 100.f), S.Harsh < Lim, FString::Printf(TEXT("%.1f%%"), S.Harsh * 100.f));
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
	if (Script == TEXT("orbit")) { LayerCheck(EDmLayer::Space, true, 0.5f); LayerCheck(EDmLayer::Birds, false, 0.05f); LayerCheck(EDmLayer::Surf, false, 0.05f); }
	else if (Script == TEXT("coast-day")) { LayerCheck(EDmLayer::Surf, true, 0.6f); LayerCheck(EDmLayer::Birds, true, 0.2f); LayerCheck(EDmLayer::Crickets, false, 0.05f); LayerCheck(EDmLayer::Space, false, 0.05f); }
	else if (Script == TEXT("forest-night")) { LayerCheck(EDmLayer::Crickets, true, 0.5f); LayerCheck(EDmLayer::Birds, false, 0.05f); }
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
	}
	else if (Script == TEXT("age-advance"))
	{
		const int32 Before = R.NumAtMarker > 0 ? R.AtMarker[0].MusicAge : -1, After = R.End.MusicAge;
		Add(TEXT("MUSIC AGE 2 -> 3"), Before == 2 && After == 3, FString::Printf(TEXT("%d -> %d"), Before, After));
		Add(TEXT("AGE STINGER PLAYED"), R.End.StingersStarted == 1, FString::Printf(TEXT("%lld"), R.End.StingersStarted));
		InstCheck(EDmInst::Strings, true);
	}
	else if (Script == TEXT("stone"))
	{
		InstCheck(EDmInst::Flute, true); InstCheck(EDmInst::Breath, true); InstCheck(EDmInst::HandDrum, true);
		InstCheck(EDmInst::Piano, false); InstCheck(EDmInst::Pad, false); InstCheck(EDmInst::Strings, false);
		LayerCheck(EDmLayer::TownEarly, true, 0.2f);
	}
	else if (Script == TEXT("industry"))
	{
		InstCheck(EDmInst::Piano, true); InstCheck(EDmInst::Brass, true); InstCheck(EDmInst::Flute, false); InstCheck(EDmInst::Pad, false);
		LayerCheck(EDmLayer::TownIndustry, true, 0.3f);
	}
	else if (Script == TEXT("space"))
	{
		InstCheck(EDmInst::Pad, true); InstCheck(EDmInst::Glass, true); InstCheck(EDmInst::Flute, false); InstCheck(EDmInst::Lyre, false);
		LayerCheck(EDmLayer::TownModern, true, 0.1f);
	}
	else if (Script == TEXT("genesis-molten"))
	{
		LayerCheck(EDmLayer::Lava, true, 0.7f);
		InstCheck(EDmInst::Breath, true); InstCheck(EDmInst::Flute, false); InstCheck(EDmInst::HandDrum, false); InstCheck(EDmInst::Strings, false);
	}
	else if (Script == TEXT("genesis-water"))
	{
		InstCheck(EDmInst::Breath, true); InstCheck(EDmInst::HandDrum, false);
		Add(TEXT("COMET CUES >= 5"), R.End.CuesStarted >= 5, FString::Printf(TEXT("%lld"), R.End.CuesStarted));
	}
	else if (Script == TEXT("war"))
	{
		InstCheck(EDmInst::Cello, true); InstCheck(EDmInst::Taiko, true);
		Add(TEXT("TENSION FOLLOWED (> 0.6)"), R.End.MusicTension > 0.6f, FString::Printf(TEXT("%.2f"), R.End.MusicTension));
	}
	else if (Script == TEXT("probe-surf"))
	{
		// the surf crash must build and break smoothly: no level step (a click) within 20 ms
		const float Rise = MaxLevelRiseDb(R, 0.02);
		Add(TEXT("NO CLICKS (20 MS LEVEL RISE < 8 DB)"), Rise < 8.f, FString::Printf(TEXT("%.1f DB"), Rise));
	}
	else if (Script == TEXT("stress-all"))
	{
		Add(TEXT("LIMITER HELD (NO SAFETY CLAMP)"), R.End.SafetyClips == 0, FString::Printf(TEXT("%lld CLAMPS, MIN GAIN %.2f"), R.End.SafetyClips, R.End.LimiterMinGain));
		Add(TEXT("CUES BURST PLAYED (>= 34 SCRIPTED)"), R.End.CuesStarted >= 34, FString::Printf(TEXT("%lld"), R.End.CuesStarted));
		Add(TEXT("STINGERS PLAYED (>= 4)"), R.End.StingersStarted >= 4, FString::Printf(TEXT("%lld"), R.End.StingersStarted));
		Add(TEXT("VOICES CAPPED (<= 32)"), R.MaxVoices <= 32, FString::Printf(TEXT("%d"), R.MaxVoices));
	}
	else if (Script == TEXT("fast-time"))
	{
		Add(TEXT("ALL 1200 CUES STARTED"), R.End.CuesStarted == 1200, FString::Printf(TEXT("%lld"), R.End.CuesStarted));
		Add(TEXT("ALL 15 STINGERS STARTED"), R.End.StingersStarted == 15, FString::Printf(TEXT("%lld"), R.End.StingersStarted));
		Add(TEXT("LIMITER HELD (NO SAFETY CLAMP)"), R.End.SafetyClips == 0, FString::Printf(TEXT("%lld CLAMPS"), R.End.SafetyClips));
		Add(TEXT("VOICES CAPPED (<= 32)"), R.MaxVoices <= 32, FString::Printf(TEXT("%d"), R.MaxVoices));
	}
	else if (Script == TEXT("ground-night"))
	{
		LayerCheck(EDmLayer::TownModern, true, 0.5f); LayerCheck(EDmLayer::Crickets, true, 0.3f); LayerCheck(EDmLayer::Space, false, 0.05f);
	}
	else if (Script == TEXT("marathon"))
	{
		Add(TEXT("150 CUES STARTED"), R.End.CuesStarted >= 150, FString::Printf(TEXT("%lld"), R.End.CuesStarted));
		Add(TEXT("15 STINGERS STARTED"), R.End.StingersStarted == 15, FString::Printf(TEXT("%lld"), R.End.StingersStarted));
		Add(TEXT("LIMITER HELD (NO SAFETY CLAMP)"), R.End.SafetyClips == 0, FString::Printf(TEXT("%lld CLAMPS"), R.End.SafetyClips));
	}
	return C;
}
