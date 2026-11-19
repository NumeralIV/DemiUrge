// DEMIURGE — Game/Audio/DmAudioScenarios.cpp : the audio module's scenarios. Owner: C9 audio.
//
//   shot.ps1 audio-render -noshot [-only=coast-day,cue-thunder] [-panels=mix|all|none]
//       renders every script of DmAudioTest offline (48 kHz), measures it, writes
//       shots/ue-audio-<script>.png panels, shots/ue-audio-summary.png, ue-audio-summary-cues.png and
//       WAVs under <project>/Saved/Audio/; exit code 1 if any check failed.
//   shot.ps1 audio-live -noshot
//       the real game: fixture world, planet, sky and the audio director; the camera visits orbit, a
//       coast, a forest by day and by night, the largest town and the storm; sim events are injected
//       at each stop; the director's mix is captured on the game thread (-nosound has no device) and
//       analysed (shots/ue-audio-live.png), with a screenshot per stop (ue-audio-live-<stop>.png).
#include "CoreMinimal.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmScene.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Game/Audio/DmAudioDirector.h"
#include "Game/Audio/DmAudioTest.h"
#include "Game/Audio/DmAudioAnalysis.h"
#include "Game/Audio/DmAudioMusic.h"
#include "Game/Core/DmCoords.h"
#include "Sim/State.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include <cmath>

namespace
{
	FString ShotsDir()
	{
		FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		for (int32 I = 0; I < 6; ++I)
		{
			if (IFileManager::Get().DirectoryExists(*FPaths::Combine(Dir, TEXT("docs"))) && IFileManager::Get().DirectoryExists(*FPaths::Combine(Dir, TEXT("shots"))))
				return FPaths::Combine(Dir, TEXT("shots"));
			Dir = FPaths::GetPath(Dir.EndsWith(TEXT("/")) ? Dir.LeftChop(1) : Dir);
		}
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Shots"));
	}
	FString AudioDir() { return FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()), TEXT("Audio")); }

	bool IsMix(const FString& S) { return !S.StartsWith(TEXT("cue-")) && !S.StartsWith(TEXT("stinger-")); }

	void RunRender(FDmScenarioContext& Ctx)
	{
		FString Only, Panels = TEXT("all");
		FDmShotArgs::Value(TEXT("-only="), Only);
		FDmShotArgs::Value(TEXT("-panels="), Panels);
		TArray<FString> Want;
		Only.ParseIntoArray(Want, TEXT(","));
		const FString Shots = ShotsDir();
		TArray<DmAudioAnalysis::FRow> MixRows, CueRows;
		bool bAllPass = true;
		double Audio = 0, Wall = 0;
		const double T0 = FPlatformTime::Seconds();
		for (const FString& Name : DmAudioTest::Scripts())
		{
			if (Want.Num() && !Want.Contains(Name)) continue;
			FDmAudioRender R;
			const double Sec = DmAudioTest::DefaultSeconds(Name);
			if (!DmAudioTest::Render(Name, Sec, R)) { Ctx.Log(FString::Printf(TEXT("DmAudio %s: unknown script"), *Name)); bAllPass = false; continue; }
			const FDmAudioStats S = DmAudioAnalysis::Analyse(R.L.GetData(), R.R.GetData(), R.L.Num(), R.Sr);
			const TArray<FDmAudioCheck> Checks = DmAudioTest::Checks(Name, R, S);
			bool bPass = true;
			FString Failed;
			for (const FDmAudioCheck& K : Checks) if (!K.bPass) { bPass = false; Failed += FString::Printf(TEXT(" [%s: %s]"), *K.Name, *K.Detail); }
			bAllPass &= bPass;
			Audio += S.Seconds; Wall += R.RenderSeconds;
			FString Inst;
			for (int32 I = 0; I < int32(EDmInst::Count) && I < 14; ++I)
				if (R.End.InstNotes[I] > 0) Inst += FString::Printf(TEXT("%s%s %d"), Inst.IsEmpty() ? TEXT("") : TEXT(", "), FDmMusic::InstName(EDmInst(I)), R.End.InstNotes[I]);
			FString Layers;
			for (int32 I = 0; I < int32(EDmLayer::Count); ++I)
				if (R.End.Layer[I] > 0.02f) Layers += FString::Printf(TEXT("%s%s %.2f"), Layers.IsEmpty() ? TEXT("") : TEXT(", "), DmAudio::LayerName(EDmLayer(I)), R.End.Layer[I]);
			Ctx.Log(FString::Printf(TEXT("DmAudio %s: %s | %s | cpu %.1f%% | cues %lld, stingers %lld, voices max %d, music notes max %d mean %.1f | limiter min %.2f clamps %lld | scale %s %s | layers [%s] | notes [%s]%s"),
				*Name, bPass ? TEXT("PASS") : TEXT("FAIL"), *DmAudioAnalysis::Describe(S), 100.0 * R.RenderSeconds / FMath::Max(1e-6, S.Seconds),
				R.End.CuesStarted, R.End.StingersStarted, R.MaxVoices, R.MaxMusicVoices, R.MeanMusicVoices, R.End.LimiterMinGain, R.End.SafetyClips,
				*FString::Printf(TEXT("root %d"), R.End.ScaleRoot), FDmScale::ModeName(R.End.ScaleMode), *Layers, *Inst, *Failed));

			DmAudioAnalysis::WriteWav(FPaths::Combine(AudioDir(), Name + TEXT(".wav")), R.L.GetData(), R.R.GetData(), R.L.Num(), R.Sr);
			const bool bPanel = Panels == TEXT("all") || (Panels == TEXT("mix") && IsMix(Name));
			if (bPanel)
			{
				const FString Title = FString::Printf(TEXT("AUDIO  %s"), *Name.ToUpper());
				const FString Sub = FString::Printf(TEXT("%s   %.0f S AT %.0f HZ, SEED 7, OFFLINE RENDER OF THE FULL MIX   CPU %.1f%%"), *R.Help.ToUpper(), Sec, R.Sr, 100.0 * R.RenderSeconds / FMath::Max(1e-6, S.Seconds));
				DmAudioAnalysis::WritePanel(FPaths::Combine(Shots, FString::Printf(TEXT("ue-audio-%s.png"), *Name)), Title, Sub, R.L.GetData(), R.R.GetData(), R.L.Num(), R.Sr, S, Checks, R.Tracks, R.Markers);
			}
			DmAudioAnalysis::FRow Row;
			Row.Name = Name.ToUpper(); Row.Stats = S; Row.bPass = bPass;
			Row.Note = bPass ? FString() : Failed.Left(60).ToUpper();
			(IsMix(Name) ? MixRows : CueRows).Add(Row);
		}

		// determinism: the same script and seed render the same bits
		if (!Want.Num() || Want.Contains(TEXT("coast-day")))
		{
			FDmAudioRender A, B;
			DmAudioTest::Render(TEXT("coast-day"), 6.0, A);
			DmAudioTest::Render(TEXT("coast-day"), 6.0, B);
			FDmAudioRender C2;
			DmAudioTest::Render(TEXT("coast-day"), 6.0, C2, 8);
			const bool bSame = A.Hash == B.Hash, bDiffers = A.Hash != C2.Hash;
			bAllPass &= bSame && bDiffers;
			Ctx.Log(FString::Printf(TEXT("DmAudio determinism: %s (seed 7 twice: %08x %08x; seed 8: %08x %s)"), (bSame && bDiffers) ? TEXT("PASS") : TEXT("FAIL"), A.Hash, B.Hash, C2.Hash, bDiffers ? TEXT("differs") : TEXT("SAME")));
		}
		if (MixRows.Num()) DmAudioAnalysis::WriteSummary(FPaths::Combine(Shots, TEXT("ue-audio-summary.png")), TEXT("AUDIO  MIX SCENARIOS (OFFLINE, 48 KHZ)"), MixRows);
		if (CueRows.Num()) DmAudioAnalysis::WriteSummary(FPaths::Combine(Shots, TEXT("ue-audio-summary-cues.png")), TEXT("AUDIO  CUES AND STINGERS, EACH ALONE"), CueRows);
		Ctx.Log(FString::Printf(TEXT("DmAudio RESULT %s: %d mix + %d cue/stinger scripts, %.0f s of audio rendered in %.2f s (%.1f%% of real time), whole run %.1f s"),
			bAllPass ? TEXT("PASS") : TEXT("FAIL"), MixRows.Num(), CueRows.Num(), Audio, Wall, 100.0 * Wall / FMath::Max(1e-6, Audio), FPlatformTime::Seconds() - T0));
		Ctx.SetExitCode(bAllPass ? 0 : 1);
		Ctx.Finish();
	}

	FDmScenarioRegistrar GAudioRender(TEXT("audio-render"), TEXT("audio"),
		TEXT("offline renders of every audio script with measurements, panels and WAVs (-only=a,b -panels=mix|all|none)"),
		[](FDmScenarioContext& Ctx)
		{
			Ctx.HoldExit();
			Ctx.LookAt(20.0, 30.0, 700.0);
		},
		[](FDmScenarioContext& Ctx, float)
		{
			if (Ctx.Frame == 3) RunRender(Ctx);
		});

	// ------------------------------------------------------------------------------------------------ device
	// The real-time path: the director on a real audio device (run without -nosound, e.g. -nullrhi):
	// the mixer must pull the engine through UDmSynthWave at the device rate.
	FDmScenarioRegistrar GAudioDevice(TEXT("audio-device"), TEXT("audio"),
		TEXT("the director on a real audio device: the mixer pulls the synth wave in real time (run without -nosound)"),
		[](FDmScenarioContext& Ctx)
		{
			Ctx.HoldExit();
			Ctx.BuildScene(uint32(EDmPartKind::Audio));
			Ctx.LookAt(20.0, 30.0, 700.0);
		},
		[](FDmScenarioContext& Ctx, float)
		{
			static double Start = -1;
			static int64 Frames0 = 0;
			ADmAudioDirector* D = ADmAudioDirector::Find(Ctx.World);
			if (!D) { if (Ctx.Time > 2.0) { Ctx.Log(TEXT("DmAudioDevice RESULT FAIL: no director")); Ctx.SetExitCode(1); Ctx.Finish(); } return; }
			if (Start < 0) { FDmAudioVolumes Mute; Mute.Master = 0.f; D->SetVolumes(Mute); }
			if (Start < 0 && Ctx.Time > 1.0)
			{
				Start = FPlatformTime::Seconds();   // wall time: the mixer runs in real time, game time may not
				Frames0 = D->GetEngine()->GetLevels().FramesRendered;
				// silent on the speakers (master 0): the mixer still pulls every frame, which is what is measured
				FDmAudioVolumes V; V.Master = 0.f;
				D->SetVolumes(V);
				D->PlayCue(EDmCue::Chime);
				Ctx.Log(FString::Printf(TEXT("DmAudioDevice: device %s, %.0f Hz"), D->HasDevice() ? TEXT("yes") : TEXT("no"), D->GetSampleRate()));
			}
			if (Start >= 0 && FPlatformTime::Seconds() - Start > 4.0)
			{
				const FDmAudioLevels Lv = D->GetEngine()->GetLevels();
				const double Sec = FPlatformTime::Seconds() - Start;
				const double Rate = double(Lv.FramesRendered - Frames0) / Sec;
				const bool bPass = D->HasDevice() && Rate > 0.8 * D->GetSampleRate() && Rate < 1.25 * D->GetSampleRate() && Lv.CuesStarted >= 1;
				Ctx.Log(FString::Printf(TEXT("DmAudioDevice RESULT %s: %s, %.0f frames/s pulled by the mixer over %.1f s (device %.0f Hz), cues %lld, render %.2f ms per second of audio, block peak %.3f"),
					bPass ? TEXT("PASS") : (D->HasDevice() ? TEXT("FAIL") : TEXT("SKIP (no audio device)")), D->HasDevice() ? TEXT("device") : TEXT("no device"), Rate, Sec, D->GetSampleRate(),
					Lv.CuesStarted, Lv.FramesRendered > 0 ? 1000.0 * Lv.RenderSeconds / (double(Lv.FramesRendered) / D->GetSampleRate()) : 0.0, Lv.BlockPeak));
				Ctx.SetExitCode(bPass || !D->HasDevice() ? 0 : 1);
				Ctx.Finish();
				Start = 1e30;
			}
		});

	// ------------------------------------------------------------------------------------------------ live

	struct FStop
	{
		FString Name;
		int32 Cell = -1;
		double Lat = 0, Lon = 0, DistKm = 700, Heading = 0, Pitch = 90, Fov = 30;
		bool bNight = false;
		double Start = 0;
		FDmAudioLevels At;
		FDmAudioContext CtxAt;
		bool bLogged = false;
		uint32 Fired = 0;
	};
	struct FLive
	{
		TArray<FStop> Stops;
		int32 Cur = -1;
		double StopLen = 6.0;
		TWeakObjectPtr<ADmAudioDirector> Dir;
		TArray<FDmAudioMarker> Markers;
		TArray<FDmAudioTrack> Tracks;
		double NextSample = 0;
		int64 CuesBefore = 0;
		TArray<FString> EventLog;
		bool bDone = false;
		int32 StormCell = -1, CityId = -1;
	};
	FLive GLive;

	double CellLat(const dm::World& W, int32 C) { return double(W.geo->lat[size_t(C)]) * 180.0 / 3.14159265358979; }
	double CellLon(const dm::World& W, int32 C) { return double(W.geo->lon[size_t(C)]) * 180.0 / 3.14159265358979; }

	int32 FindCell(const dm::World& W, TFunctionRef<double(int32)> Score)
	{
		int32 Best = -1;
		double BestS = 0;
		for (int32 C = 0; C < W.n; C += 7)
		{
			const double S = Score(C);
			if (S > BestS) { BestS = S; Best = C; }
		}
		return Best;
	}

	void PlanStops(FDmScenarioContext& Ctx)
	{
		GLive = FLive();
		const dm::World* W = Ctx.Sim ? Ctx.Sim->Sim() : nullptr;
		if (!W || !W->geo) return;
		std::vector<int32_t> Near;
		auto Frac = [&](int32 C, double Ang, TFunctionRef<bool(int32)> Pred)
		{
			const int32 N = W->geo->within(C, Ang, Near);
			int32 K = 0;
			for (int32 I = 0; I < N; ++I) K += Pred(Near[size_t(I)]) ? 1 : 0;
			return N ? double(K) / N : 0.0;
		};
		auto Wild = [&](int32 C) { return W->owner[size_t(C)] < 0 && W->urban[size_t(C)] < 0.01f; };
		const int32 Coast = FindCell(*W, [&](int32 C)
		{
			if (dm::state::isSea(*W, C) || W->coast[size_t(C)] != 1 || !Wild(C)) return 0.0;
			const double S = Frac(C, 0.04, [&](int32 M) { return dm::state::isSea(*W, M); });
			return 1.0 - FMath::Abs(S - 0.5) * 2.0 + (FMath::Abs(CellLat(*W, C)) < 45.0 ? 0.2 : 0.0);
		});
		const int32 Forest = FindCell(*W, [&](int32 C)
		{
			const uint8 B = W->biome[size_t(C)];
			if (!(B == dm::Biome::FOREST || B == dm::Biome::RAINFOREST || B == dm::Biome::TAIGA) || !Wild(C) || W->coast[size_t(C)] < 3) return 0.0;
			return Frac(C, 0.05, [&](int32 M) { const uint8 Bm = W->biome[size_t(M)]; return Bm == dm::Biome::FOREST || Bm == dm::Biome::RAINFOREST || Bm == dm::Biome::TAIGA; }) + W->veg[size_t(C)] * 0.2;
		});
		int32 City = -1;
		double Pop = 0;
		for (const dm::Settlement& S : W->settlements) if (!S.dead && S.pop > Pop && S.cell >= 0) { Pop = S.pop; City = S.id; }
		for (const dm::Disaster& D : W->disasters) if (D.type == dm::DisasterType::Storm && D.cell >= 0) { GLive.StormCell = D.cell; break; }
		GLive.CityId = City;

		auto Add = [&](const TCHAR* Name, int32 Cell, double Dist, double Pitch, bool bNight)
		{
			if (Cell < 0) { Ctx.Log(FString::Printf(TEXT("DmAudioLive: no cell for stop %s"), Name)); return; }
			FStop S;
			S.Name = Name; S.Cell = Cell; S.Lat = CellLat(*W, Cell); S.Lon = CellLon(*W, Cell); S.DistKm = Dist; S.Pitch = Pitch; S.bNight = bNight;
			S.Heading = 30.0;
			GLive.Stops.Add(S);
		};
		{
			FStop S; S.Name = TEXT("orbit"); S.Lat = 20.0; S.Lon = 30.0; S.DistKm = 700.0; S.Pitch = 90.0;
			const dm::Vec3 D = DmCoords::UEToSim(DmCoords::LatLonToDir(S.Lat, S.Lon));
			S.Cell = W->geo->cellAt(D.x, D.y, D.z);
			GLive.Stops.Add(S);
		}
		Add(TEXT("coast"), Coast, 6.0, 32.0, false);
		Add(TEXT("forest-day"), Forest, 5.0, 35.0, false);
		Add(TEXT("forest-night"), Forest, 5.0, 35.0, true);
		Add(TEXT("city"), City >= 0 ? W->settlements[size_t(City)].cell : -1, 7.0, 40.0, false);
		Add(TEXT("storm"), GLive.StormCell, 60.0, 60.0, false);
		for (const FStop& S : GLive.Stops)
			Ctx.Log(FString::Printf(TEXT("DmAudioLive: stop %s at cell %d (lat %.2f lon %.2f), %.0f km, pitch %.0f%s"), *S.Name, S.Cell, S.Lat, S.Lon, S.DistKm, S.Pitch, S.bNight ? TEXT(", night") : TEXT("")));
	}

	void Inject(FDmScenarioContext& Ctx, FStop& S, double Local)
	{
		dm::World* W = Ctx.Sim ? Ctx.Sim->SimMutable() : nullptr;
		if (!W) return;
		auto Due = [&S, Local](int32 Bit, double At) { if (Local < At || (S.Fired & (1u << Bit))) return false; S.Fired |= 1u << Bit; return true; };
		auto Log = [&](const TCHAR* What) { GLive.EventLog.Add(FString::Printf(TEXT("%s@%.1f %s"), *S.Name, Local, What)); FDmAudioMarker M; M.Time = Ctx.Time; M.Label = What; GLive.Markers.Add(M); };
		if (S.Name == TEXT("orbit"))
		{
			if (Due(1, 1.0)) { W->bus.emit(dm::EvMeteorLaunched{ S.Cell, 2.5, dm::ImpactKind::Meteor, 99 }); Log(TEXT("METEOR:LAUNCHED")); }
			if (Due(2, 3.5)) { W->bus.emit(dm::EvMeteorImpact{ S.Cell, 0.05, dm::ImpactKind::Meteor, false }); Log(TEXT("METEOR:IMPACT")); }
		}
		else if (S.Name == TEXT("coast"))
		{
			if (Due(3, 2.0)) { W->bus.emit(dm::EvUiPower{ std::string("rain") }); Log(TEXT("UI:POWER")); }
			if (Due(4, 3.0)) { W->bus.emit(dm::EvPowerUsed{ std::string("rain"), S.Cell, 0.1, true }); Log(TEXT("POWER:USED RAIN")); }
		}
		else if (S.Name == TEXT("forest-day"))
		{
			if (Due(5, 2.5)) { W->bus.emit(dm::EvPowerUsed{ std::string("bloom"), S.Cell, 0.08, true }); Log(TEXT("POWER:USED BLOOM")); }
		}
		else if (S.Name == TEXT("forest-night"))
		{
			if (Due(6, 3.0)) { W->bus.emit(dm::EvFireStart{ S.Cell }); Log(TEXT("FIRE:START")); }
		}
		else if (S.Name == TEXT("city"))
		{
			if (Due(7, 1.5)) { W->bus.emit(dm::EvPrayerAnswered{ GLive.CityId, dm::PrayerKind::Rain }); Log(TEXT("PRAYER:ANSWERED")); }
			if (Due(8, 3.0)) { W->bus.emit(dm::EvWorldAge{ 4 }); Log(TEXT("WORLD:AGE")); }
		}
		else if (S.Name == TEXT("storm"))
		{
			if (Due(9, 1.5)) { W->bus.emit(dm::EvLightning{ S.Cell, false }); Log(TEXT("LIGHTNING")); }
		}
	}

	FDmScenarioRegistrar GAudioLive(TEXT("audio-live"), TEXT("audio"),
		TEXT("the director in the real game: camera stops at orbit, coast, forest day / night, city, storm; injected events; captured mix analysed"),
		[](FDmScenarioContext& Ctx)
		{
			Ctx.HoldExit();
			Ctx.BuildScene(DmPartsWorld | uint32(EDmPartKind::Audio));
			PlanStops(Ctx);
			GLive.Dir = ADmAudioDirector::Find(Ctx.World);
			if (!GLive.Dir.IsValid()) { Ctx.Log(TEXT("DmAudioLive: FAIL no audio director spawned")); Ctx.SetExitCode(1); Ctx.Finish(); return; }
			GLive.Dir->StartCapture(GLive.StopLen * GLive.Stops.Num() + 2.0);
			static const TCHAR* LayerTracks[7] = { TEXT("space"), TEXT("wind"), TEXT("surf"), TEXT("birds"), TEXT("crickets"), TEXT("rain"), TEXT("town") };
			static const FColor Cols[7] = { FColor(110, 190, 200), FColor(200, 200, 200), FColor(90, 150, 240), FColor(132, 200, 120), FColor(220, 120, 200), FColor(240, 150, 80), FColor(232, 199, 122) };
			for (int32 I = 0; I < 7; ++I) { FDmAudioTrack T; T.Name = FString(LayerTracks[I]).ToUpper(); T.Color = Cols[I]; GLive.Tracks.Add(T); }
		},
		[](FDmScenarioContext& Ctx, float)
		{
			if (GLive.bDone || !GLive.Dir.IsValid() || GLive.Stops.Num() == 0) return;
			ADmAudioDirector* D = GLive.Dir.Get();
			const int32 Want = FMath::Min(GLive.Stops.Num() - 1, int32(Ctx.Time / GLive.StopLen));
			if (Want != GLive.Cur)
			{
				GLive.Cur = Want;
				FStop& S = GLive.Stops[Want];
				S.Start = Ctx.Time;
				Ctx.LookAt(S.Lat, S.Lon, S.DistKm, S.Heading, S.Pitch, 40.0);
				if (S.bNight) Ctx.SetSubsolar(-S.Lat, S.Lon + 180.0);
				else Ctx.SetSubsolar(S.Lat * 0.6, S.Lon + 25.0);
				D->SenseNow();
				FDmAudioMarker M; M.Time = Ctx.Time; M.Label = S.Name.ToUpper(); GLive.Markers.Add(M);
			}
			FStop& S = GLive.Stops[GLive.Cur];
			const double Local = Ctx.Time - S.Start;
			Inject(Ctx, S, Local);
			if (!S.bLogged && Local >= GLive.StopLen - 0.6)
			{
				S.bLogged = true;
				S.At = D->GetEngine()->GetLevels();
				S.CtxAt = D->GetContext();
				const FDmAudioContext& C = S.CtxAt;
				FString L;
				for (int32 I = 0; I < int32(EDmLayer::Count); ++I) if (S.At.Layer[I] > 0.02f) L += FString::Printf(TEXT(" %s %.2f"), DmAudio::LayerName(EDmLayer(I)), S.At.Layer[I]);
				Ctx.Log(FString::Printf(TEXT("DmAudioLive %s: alt %.1f km (a01 %.2f) focus %d day %.2f sea %.2f coast %.2f forest %.2f open %.2f town %.2f (age %d) storm %.2f rain %.2f tension %.2f | layers%s | cues %lld voices %d music %d"),
					*S.Name, D->GetAltitudeKm(), C.Altitude01, D->GetFocusCell(), C.Day, C.Sea, C.Coast, C.Forest, C.Open, C.Town, C.TownAge, C.Storm, C.Rain, C.Tension, *L,
					S.At.CuesStarted, S.At.Voices, S.At.MusicVoices));
				Ctx.Shot(S.Name);
			}
			if (Ctx.Time >= GLive.NextSample)
			{
				GLive.NextSample = Ctx.Time + 0.1;
				const FDmAudioLevels Lv = D->GetEngine()->GetLevels();
				const float Town = FMath::Max(FMath::Max(Lv.Layer[int32(EDmLayer::TownEarly)], Lv.Layer[int32(EDmLayer::TownMiddle)]), FMath::Max(Lv.Layer[int32(EDmLayer::TownIndustry)], Lv.Layer[int32(EDmLayer::TownModern)]));
				const float V[7] = { Lv.Layer[int32(EDmLayer::Space)], Lv.Layer[int32(EDmLayer::Wind)], Lv.Layer[int32(EDmLayer::Surf)], Lv.Layer[int32(EDmLayer::Birds)], Lv.Layer[int32(EDmLayer::Crickets)], Lv.Layer[int32(EDmLayer::Rain)], Town };
				for (int32 I = 0; I < 7; ++I) GLive.Tracks[I].Values.Add(V[I]);
			}
			if (Ctx.Time >= GLive.StopLen * GLive.Stops.Num() + 0.5)
			{
				GLive.bDone = true;
				D->StopCapture();
				const TArray<float>& L = D->CaptureL();
				const TArray<float>& R = D->CaptureR();
				const FDmAudioStats St = DmAudioAnalysis::Analyse(L.GetData(), R.GetData(), L.Num(), D->GetSampleRate());
				TArray<FDmAudioCheck> Checks;
				auto Add = [&Checks](const FString& Name, bool bPass, const FString& Detail) { FDmAudioCheck K; K.Name = Name; K.bPass = bPass; K.Detail = Detail; Checks.Add(K); };
				Add(TEXT("CAPTURED"), St.Seconds > GLive.StopLen * GLive.Stops.Num() * 0.9, FString::Printf(TEXT("%.1f S"), St.Seconds));
				Add(TEXT("NO NAN"), St.NaN == 0, FString::Printf(TEXT("%lld"), St.NaN));
				Add(TEXT("NO CLIPPING"), St.Clipped == 0, FString::Printf(TEXT("%lld"), St.Clipped));
				Add(TEXT("PEAK <= -0.9 DBFS"), St.PeakDb <= -0.9f, FString::Printf(TEXT("%.2f"), St.PeakDb));
				Add(TEXT("SUB < 25%"), St.Sub < 0.25f, FString::Printf(TEXT("%.1f%%"), St.Sub * 100.f));
				Add(TEXT("NEVER SILENT"), St.Silent < 0.05f, FString::Printf(TEXT("%.1f%%"), St.Silent * 100.f));
				Add(TEXT("NOT HARSH (< 12%)"), St.Harsh < 0.12f, FString::Printf(TEXT("%.1f%%"), St.Harsh * 100.f));
				auto Ly = [](const FStop& S2, EDmLayer Lr) { return S2.At.Layer[int32(Lr)]; };
				for (const FStop& S2 : GLive.Stops)
				{
					if (S2.Name == TEXT("orbit")) Add(TEXT("ORBIT: SPACE >= 0.5"), Ly(S2, EDmLayer::Space) >= 0.5f, FString::Printf(TEXT("%.2f"), Ly(S2, EDmLayer::Space)));
					if (S2.Name == TEXT("coast")) Add(TEXT("COAST: SURF >= 0.4"), Ly(S2, EDmLayer::Surf) >= 0.4f, FString::Printf(TEXT("%.2f"), Ly(S2, EDmLayer::Surf)));
					if (S2.Name == TEXT("forest-day")) Add(TEXT("FOREST DAY: BIRDS >= 0.3"), Ly(S2, EDmLayer::Birds) >= 0.3f, FString::Printf(TEXT("%.2f (day %.2f)"), Ly(S2, EDmLayer::Birds), S2.CtxAt.Day));
					if (S2.Name == TEXT("forest-night")) Add(TEXT("FOREST NIGHT: CRICKETS >= 0.3"), Ly(S2, EDmLayer::Crickets) >= 0.3f, FString::Printf(TEXT("%.2f (day %.2f)"), Ly(S2, EDmLayer::Crickets), S2.CtxAt.Day));
					if (S2.Name == TEXT("city"))
					{
						const float T = FMath::Max(FMath::Max(Ly(S2, EDmLayer::TownEarly), Ly(S2, EDmLayer::TownMiddle)), FMath::Max(Ly(S2, EDmLayer::TownIndustry), Ly(S2, EDmLayer::TownModern)));
						Add(TEXT("CITY: TOWN >= 0.3"), T >= 0.3f, FString::Printf(TEXT("%.2f (age %d)"), T, S2.CtxAt.TownAge));
					}
					if (S2.Name == TEXT("storm")) Add(TEXT("STORM: RAIN >= 0.4"), Ly(S2, EDmLayer::Rain) >= 0.4f, FString::Printf(TEXT("%.2f"), Ly(S2, EDmLayer::Rain)));
				}
				const FDmAudioLevels End = D->GetEngine()->GetLevels();
				Add(TEXT("EVENTS BECAME CUES"), End.CuesStarted >= 6, FString::Printf(TEXT("%lld CUES, %lld STINGERS, EVENTS HEARD %lld"), End.CuesStarted, End.StingersStarted, D->GetEventsHeard()));
				bool bAll = true;
				FString Failed;
				for (const FDmAudioCheck& K : Checks) if (!K.bPass) { bAll = false; Failed += FString::Printf(TEXT(" [%s: %s]"), *K.Name, *K.Detail); }
				FString Recent = FString::Join(D->GetRecent(), TEXT(", "));
				Ctx.Log(FString::Printf(TEXT("DmAudioLive capture: %s | %s | cues played: %s | events: %s%s"), bAll ? TEXT("PASS") : TEXT("FAIL"), *DmAudioAnalysis::Describe(St), *Recent, *FString::Join(GLive.EventLog, TEXT("; ")), *Failed));
				Ctx.Log(FString::Printf(TEXT("DmAudioLive cost: director Sense %.3f ms per call over %lld calls (10 Hz); engine render %.2f ms per second of audio (game-thread capture); events heard %lld"),
					D->GetSenseMsAvg(), D->GetSenseCalls(), End.RenderSeconds * 1000.0 / FMath::Max(1e-6, St.Seconds), D->GetEventsHeard()));
				DmAudioAnalysis::WriteWav(FPaths::Combine(AudioDir(), TEXT("live.wav")), L.GetData(), R.GetData(), L.Num(), D->GetSampleRate());
				DmAudioAnalysis::WritePanel(FPaths::Combine(ShotsDir(), TEXT("ue-audio-live.png")), TEXT("AUDIO  LIVE GAME CAPTURE"),
					TEXT("FIXTURE WORLD, REAL DIRECTOR: CAMERA STOPS AND INJECTED SIM EVENTS, MIX RENDERED ON THE GAME THREAD (-NOSOUND)"),
					L.GetData(), R.GetData(), L.Num(), D->GetSampleRate(), St, Checks, GLive.Tracks, GLive.Markers);
				Ctx.Log(FString::Printf(TEXT("DmAudioLive RESULT %s"), bAll ? TEXT("PASS") : TEXT("FAIL")));
				Ctx.SetExitCode(bAll ? 0 : 1);
				Ctx.Finish();
			}
		});
}
