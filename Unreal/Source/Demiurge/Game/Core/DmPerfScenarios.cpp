// DEMIURGE — Game/Core/DmPerfScenarios.cpp : GPU A/B measurement over the views a player sees (wave3 perf track).
// Owner: perf (lane perf3).
//
//   perf-ab   a real history world (seed -seed=, default 21, run headless with the scripted god to -pyear=, default 2400),
//             the whole game on screen (planet, sky, nature, towns, fx, HUD), the sim paused (-abspeed=<x> lets it run).
//             For every view of -views= (default all: orbit, term, mid, low, close, night) and every config of -ab=
//             (default "base"), interleaved over -abrounds= rounds (default 3) so slow drift on a shared GPU cancels
//             out: apply the config, let it settle 24 frames, record -abwin= frames (default 90) of GPU time
//             (RHIGetGPUFrameCycles), restore. Logs one line per view and config:
//               DmAB <view> <config>: gpu median 12.34 ms (p90 13.10, n 270) delta -1.20 ms vs base | frame 15.2 ms
//             -abgenesis: a new world at its molten start instead (seed -seed=), for the genesis views.
//             -abprofile: one ProfileGPU per view and config (round 0) into the log; -abshots: one picture per view and
//             config (shots/ue-ab-<view>-<config>.png, HUD included).
//
// Configs (comma list in -ab=):
//   base             nothing changed
//   tier0 .. tier3   every scalability group at that level (Config/DefaultScalability.ini), as the settings screen does
//   hide-<x>         hide every actor whose class name contains "Dm<x>" (hide-nature, hide-towns, hide-planet, hide-fx,
//                    hide-hud is the HUD actor; hide-sky hides the sun, atmosphere and clouds)
//   <name>           a named cvar set from -abfile=<path> (lines "name: r.Foo=1 r.Bar=2"; '#' comments), or a built-in
//                    one below (kept as the record of what was measured)

#include "Game/Core/DmScenario.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmGameMode.h"
#include "Game/Core/DmRenderTiers.h"
#include "Game/Core/DmSimSubsystem.h"

#include "Game/Camera/DmPawn.h"
#include "Game/Flow/DmFlow.h"
#include "Game/Flow/DmGodPolicy.h"
#include "Game/Input/DmControl.h"
#include "Game/Planet/DmPlanet.h"
#include "Game/UI/Screens/DmScreenSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Scalability.h"

#include "Sim/Civ.h"
#include "Sim/Sim.h"
#include "Sim/State.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmAB, Log, All);

namespace
{
	struct FAbView { const TCHAR* Name; double Zoom; double Heading; double SunElev; };
	// the perf-play stops (orbit, mid, low, close) plus the terminator from orbit and a city at night up close
	const FAbView GAbViews[] = {
		{ TEXT("orbit"), 0.0, 0.0, 35.0 },
		{ TEXT("term"), 0.0, 0.0, 1.0 },
		{ TEXT("mid"), 0.45, 0.0, 35.0 },
		{ TEXT("low"), 0.75, 40.0, 35.0 },
		{ TEXT("close"), 0.92, 120.0, 35.0 },
		{ TEXT("night"), 0.92, 120.0, -25.0 },
	};

	struct FAbConfig
	{
		FString Name;
		TArray<TPair<FString, FString>> CVars;   // name, value
		int32 Tier = -1;
		FString Hide;                            // class-name fragment ("DmNature")
	};

	// built-in cvar sets: what was measured for the tiers (docs/handoff/done-ue/wave3-perf.built.txt)
	const TCHAR* GBuiltIn[] = {
		TEXT("tsrhist100: r.TSR.History.ScreenPercentage=100"),
		TEXT("tsr2: r.TSR.History.ScreenPercentage=100 r.TSR.History.UpdateQuality=2 r.TSR.RejectionAntiAliasingQuality=1"),
		TEXT("sp100: r.ScreenPercentage=100"),
		TEXT("sp67: r.ScreenPercentage=67"),
		TEXT("sp58: r.ScreenPercentage=58"),
		TEXT("sp50: r.ScreenPercentage=50"),
		TEXT("gi2: r.Lumen.ScreenProbeGather.DownsampleFactor=32 r.Lumen.ScreenProbeGather.NumAdaptiveProbes=16 r.Lumen.ScreenProbeGather.IrradianceFormat=1 r.Lumen.ScreenProbeGather.StochasticInterpolation=1 r.Lumen.ScreenProbeGather.RadianceCache.ProbeResolution=16 r.LumenScene.Radiosity.ProbeSpacing=8 r.LumenScene.Radiosity.HemisphereProbeResolution=3 r.Lumen.TranslucencyVolume.GridPixelSize=64 r.Lumen.TranslucencyVolume.TraceFromVolume=0"),
		TEXT("gioff: r.Lumen.DiffuseIndirect.Allow=0"),
		TEXT("refl2: r.Lumen.Reflections.DownsampleFactor=2 r.Lumen.Reflections.ScreenSpaceReconstruction.NumSamples=3"),
		TEXT("refloff: r.Lumen.Reflections.Allow=0"),
		TEXT("translucvol0: r.Lumen.TranslucencyVolume.Enable=0"),
		TEXT("vsm2: r.Shadow.Virtual.ResolutionLodBiasDirectional=0 r.Shadow.Virtual.ResolutionLodBiasDirectionalMoving=0"),
		TEXT("vsmbias1: r.Shadow.Virtual.ResolutionLodBiasDirectional=1 r.Shadow.Virtual.ResolutionLodBiasDirectionalMoving=1"),
		TEXT("smrt4: r.Shadow.Virtual.SMRT.RayCountDirectional=4 r.Shadow.Virtual.SMRT.SamplesPerRayDirectional=2"),
		TEXT("novsm: r.Shadow.Virtual.Enable=0"),
		TEXT("noshadow: r.ShadowQuality=0"),
		TEXT("noclouds: r.VolumetricCloud=0"),
		TEXT("cloudsmp: r.VolumetricCloud.ShadowMap.MaxResolution=2048"),
		TEXT("cloudrays256: r.VolumetricCloud.ViewRaySampleMaxCount=256"),
		TEXT("noatmo: r.SkyAtmosphere=0"),
		TEXT("pp2: r.Bloom.ScreenPercentage=25 r.MotionBlurQuality=3 r.DepthOfFieldQuality=1 r.LensFlareQuality=2 r.SceneColorFringeQuality=1"),
		TEXT("nodecals: r.DBuffer=0 r.DeferredDecals=0"),
		TEXT("nodf: r.DistanceFieldAO=0 r.DistanceFieldShadowing=0"),
		TEXT("nowater: r.Water.SingleLayer=0"),
		TEXT("slwrefl0: r.Water.SingleLayer.Reflection=0"),
		TEXT("hzbocc: r.HZBOcclusion=1"),
		TEXT("asynclumen0: r.Lumen.DiffuseIndirect.AsyncCompute=0 r.Lumen.Reflections.AsyncCompute=0"),
		TEXT("vrs: r.VRS.Enable=1 r.VRS.ContrastAdaptiveShading=1"),
		TEXT("vrsfull: r.VRS.Enable=1 r.VRS.ContrastAdaptiveShading=1 r.VRS.BasePass=1"),
		// the engine's Epic before the tiers of Config/DefaultScalability.ini (TSR history 200 %, Epic screen-probe gather)
		TEXT("oldepic: r.TSR.History.ScreenPercentage=200 r.Lumen.ScreenProbeGather.DownsampleFactor=16 r.Lumen.ScreenProbeGather.NumAdaptiveProbes=8 r.Lumen.ScreenProbeGather.IrradianceFormat=0 r.Lumen.ScreenProbeGather.StochasticInterpolation=0 r.Lumen.ScreenProbeGather.RadianceCache.ProbeResolution=32 r.LumenScene.Radiosity.ProbeSpacing=4 r.LumenScene.Radiosity.HemisphereProbeResolution=4 r.Lumen.TranslucencyVolume.GridPixelSize=32 r.Lumen.TranslucencyVolume.TraceFromVolume=1"),
		// what the new High changed against oldepic, measured on the old Epic
		TEXT("hi1: r.TSR.History.ScreenPercentage=100 r.Lumen.ScreenProbeGather.DownsampleFactor=32 r.Lumen.ScreenProbeGather.NumAdaptiveProbes=16 r.Lumen.ScreenProbeGather.IrradianceFormat=1 r.Lumen.ScreenProbeGather.StochasticInterpolation=1 r.Lumen.ScreenProbeGather.RadianceCache.ProbeResolution=16 r.LumenScene.Radiosity.ProbeSpacing=8 r.LumenScene.Radiosity.HemisphereProbeResolution=3 r.Lumen.TranslucencyVolume.GridPixelSize=64 r.Lumen.TranslucencyVolume.TraceFromVolume=0"),
	};

	bool ParseConfigLine(const FString& Line, FAbConfig& Out)
	{
		FString L = Line.TrimStartAndEnd();
		if (L.IsEmpty() || L.StartsWith(TEXT("#"))) return false;
		FString Name, Rest;
		if (!L.Split(TEXT(":"), &Name, &Rest)) return false;
		Out = FAbConfig();
		Out.Name = Name.TrimStartAndEnd();
		TArray<FString> Toks;
		Rest.ParseIntoArrayWS(Toks);
		for (const FString& T : Toks)
		{
			FString K, V;
			if (T.Split(TEXT("="), &K, &V)) Out.CVars.Add({ K, V });
		}
		return !Out.Name.IsEmpty();
	}

	bool FindConfig(const FString& Name, const TArray<FAbConfig>& FromFile, FAbConfig& Out)
	{
		Out = FAbConfig();
		Out.Name = Name;
		if (Name == TEXT("base")) return true;
		if (Name.StartsWith(TEXT("tier")) && Name.Len() == 5 && FChar::IsDigit(Name[4])) { Out.Tier = Name[4] - TEXT('0'); return true; }
		if (Name.StartsWith(TEXT("hide-")))
		{
			FString X = Name.Mid(5);
			if (!X.IsEmpty()) X[0] = FChar::ToUpper(X[0]);
			Out.Hide = TEXT("Dm") + X;
			return true;
		}
		for (const FAbConfig& C : FromFile) if (C.Name == Name) { Out = C; return true; }
		for (const TCHAR* B : GBuiltIn)
		{
			FAbConfig C;
			if (ParseConfigLine(B, C) && C.Name == Name) { Out = C; return true; }
		}
		return false;
	}

	struct FAbSample { TArray<float> Gpu, Frame; };

	struct FAb
	{
		TArray<int32> Views;
		TArray<FAbConfig> Configs;
		int32 Rounds = 3, Win = 90, Settle = 24, ViewSettle = 240;
		int32 Cell = -1;
		TUniquePtr<FDmGodPolicy> God;
		bool bProfile = false, bShots = false;
		float Speed = 0.f;

		// state
		int32 ViewIdx = 0, Round = 0, CfgIdx = 0;
		int32 Phase = 0;           // 0 = new view (jump), 1 = view settling, 2 = config settling, 3 = measuring
		int32 PhaseFrame = 0;
		TArray<TPair<FString, FString>> Restore;   // cvar, previous value
		Scalability::FQualityLevels SavedLevels;
		bool bTierApplied = false;
		TArray<TWeakObjectPtr<AActor>> Hidden;
		TMap<FString, FAbSample> Samples;          // "<view>|<config>"
	};
	TUniquePtr<FAb> GAb;

	FVector SunFor(const FVector& Dir, double ElevDeg, double AzDeg)
	{
		FVector E, N;
		DmCoords::Basis(Dir, E, N);
		const double El = FMath::DegreesToRadians(ElevDeg), Az = FMath::DegreesToRadians(AzDeg);
		return (Dir * FMath::Sin(El) + (N * FMath::Cos(Az) + E * FMath::Sin(Az)) * FMath::Cos(El)).GetSafeNormal();
	}

	void ApplyConfig(FDmScenarioContext& Ctx, FAb& A, const FAbConfig& C)
	{
		A.Restore.Reset();
		for (const TPair<FString, FString>& KV : C.CVars)
		{
			FString Prev;
			if (DmRenderTiers::SetCVarKeepPriority(KV.Key, KV.Value, &Prev)) A.Restore.Add({ KV.Key, Prev });
			else UE_LOG(LogDmAB, Warning, TEXT("DmAB: no console variable %s (config %s)"), *KV.Key, *C.Name);
		}
		if (C.Tier >= 0)
		{
			A.SavedLevels = Scalability::GetQualityLevels();
			A.bTierApplied = true;
			DmRenderTiers::ApplyTier(C.Tier);
		}
		if (!C.Hide.IsEmpty())
		{
			for (TActorIterator<AActor> It(Ctx.World); It; ++It)
			{
				AActor* Ac = *It;
				if (Ac->GetClass()->GetName().Contains(C.Hide) && !Ac->IsHidden()) { Ac->SetActorHiddenInGame(true); A.Hidden.Add(Ac); }
			}
			if (A.Hidden.Num() == 0) UE_LOG(LogDmAB, Warning, TEXT("DmAB: config %s hid no actor"), *C.Name);
		}
	}

	void RestoreConfig(FAb& A)
	{
		for (int32 I = A.Restore.Num() - 1; I >= 0; --I) DmRenderTiers::SetCVarKeepPriority(A.Restore[I].Key, A.Restore[I].Value);
		A.Restore.Reset();
		if (A.bTierApplied) { Scalability::SetQualityLevels(A.SavedLevels, true); A.bTierApplied = false; }
		for (TWeakObjectPtr<AActor>& Ac : A.Hidden) if (Ac.IsValid()) Ac->SetActorHiddenInGame(false);
		A.Hidden.Reset();
	}

	double Pct(TArray<float> V, double Q)
	{
		if (V.Num() == 0) return 0.0;
		V.Sort();
		return double(V[FMath::Clamp(int32(Q * (V.Num() - 1) + 0.5), 0, V.Num() - 1)]);
	}

	void ReportView(FAb& A, int32 ViewIdx)
	{
		const FAbView& V = GAbViews[A.Views[ViewIdx]];
		const FAbSample* Base = A.Samples.Find(FString(V.Name) + TEXT("|") + A.Configs[0].Name);
		// on a GPU shared with other renders, contention only ever adds time: the low percentiles (p10, p25) are the
		// closest to this game's own cost, so the deltas are given for those as well as the median
		const double BaseMed = Base ? Pct(Base->Gpu, 0.5) : 0.0, Base10 = Base ? Pct(Base->Gpu, 0.1) : 0.0, Base25 = Base ? Pct(Base->Gpu, 0.25) : 0.0;
		for (const FAbConfig& C : A.Configs)
		{
			const FAbSample* S = A.Samples.Find(FString(V.Name) + TEXT("|") + C.Name);
			if (!S) continue;
			const double Med = Pct(S->Gpu, 0.5), P10 = Pct(S->Gpu, 0.1), P25 = Pct(S->Gpu, 0.25);
			UE_LOG(LogDmAB, Display, TEXT("DmAB %s %s: gpu p10 %.2f p25 %.2f median %.2f p90 %.2f ms (n %d) | delta p10 %+.2f p25 %+.2f median %+.2f ms vs %s | frame median %.2f ms"),
				V.Name, *C.Name, P10, P25, Med, Pct(S->Gpu, 0.9), S->Gpu.Num(), P10 - Base10, P25 - Base25, Med - BaseMed, *A.Configs[0].Name, Pct(S->Frame, 0.5));
		}
	}
}

static FDmScenarioRegistrar GDmScenarioPerfAB(TEXT("perf-ab"), TEXT("perf"),
	TEXT("GPU A/B of render configs over the player's views (orbit, terminator, mid, low, close, night); DmAB lines"),
	[](FDmScenarioContext& Ctx)
	{
		GAb = MakeUnique<FAb>();
		FAb& A = *GAb;
		int32 Seed = 21;
		FDmShotArgs::Value(TEXT("-seed="), Seed);
		double Year = 2400.0;
		FDmShotArgs::Value(TEXT("-pyear="), Year);
		FDmShotArgs::Value(TEXT("-abrounds="), A.Rounds);
		FDmShotArgs::Value(TEXT("-abwin="), A.Win);
		FDmShotArgs::Value(TEXT("-absettle="), A.Settle);
		FDmShotArgs::Value(TEXT("-abviewsettle="), A.ViewSettle);
		double Sp = 0.0;
		if (FDmShotArgs::Value(TEXT("-abspeed="), Sp)) A.Speed = float(Sp);
		A.bProfile = FDmShotArgs::Param(TEXT("abprofile"));
		A.bShots = FDmShotArgs::Param(TEXT("abshots"));
		FString Views = TEXT("all");
		FDmShotArgs::Value(TEXT("-views="), Views);
		TArray<FString> VList;
		Views.ParseIntoArray(VList, TEXT(","));
		for (int32 I = 0; I < UE_ARRAY_COUNT(GAbViews); ++I)
			if (Views == TEXT("all") || VList.Contains(GAbViews[I].Name)) A.Views.Add(I);

		TArray<FAbConfig> FromFile;
		FString File;
		if (FDmShotArgs::Value(TEXT("-abfile="), File))
		{
			TArray<FString> Lines;
			if (FFileHelper::LoadFileToStringArray(Lines, *File))
				for (const FString& L : Lines) { FAbConfig C; if (ParseConfigLine(L, C)) FromFile.Add(C); }
			else UE_LOG(LogDmAB, Warning, TEXT("DmAB: cannot read -abfile=%s"), *File);
		}
		FString Cfgs = TEXT("base");
		FDmShotArgs::Value(TEXT("-ab="), Cfgs);
		TArray<FString> CList;
		Cfgs.ParseIntoArray(CList, TEXT(","));
		if (!CList.Contains(TEXT("base"))) CList.Insert(TEXT("base"), 0);
		for (const FString& N : CList)
		{
			FAbConfig C;
			if (FindConfig(N.TrimStartAndEnd(), FromFile, C)) A.Configs.Add(C);
			else UE_LOG(LogDmAB, Warning, TEXT("DmAB: unknown config '%s' (skipped)"), *N);
		}

		Ctx.HoldExit();
		// -abgenesis: the molten world of the first genesis step instead of a history (the glowing crust and ash veil)
		const bool bGenesis = FDmShotArgs::Param(TEXT("abgenesis"));
		Ctx.Sim->CreateWorld(uint32(Seed), TEXT("Ithe"), !bGenesis, 0.0);
		Ctx.BuildScene(DmPartsAll);
		ADmFlow* F = Ctx.World->SpawnActor<ADmFlow>();
		F->bAutosave = false;
		F->bAutoSlowAllowed = false;
		Ctx.UseHarnessCamera(false);
		if (ADmPawn* Pw = ADmPawn::Find(Ctx.World)) Pw->bAdoptHarnessView = false;
		A.God = MakeUnique<FDmGodPolicy>(uint32(Seed));
		UDmControlSubsystem* Control = UDmControlSubsystem::Get(Ctx.World);
		FDmGodPolicy* God = A.God.Get();
		Ctx.Sim->BeforeTick = [God, Control](dm::World& Wd) { God->Before(Wd, Control); };
		const double T0 = FPlatformTime::Seconds();
		while (!bGenesis && Ctx.Sim->Sim() && Ctx.Sim->Sim()->year < Year && Ctx.Sim->Sim()->phase == dm::Phase::History) Ctx.Sim->StepTicks(16);
		Ctx.Sim->BeforeTick = nullptr;
		const dm::World* W = Ctx.Sim->Sim();
		double BestPop = -1.0;
		if (W) for (const dm::Settlement& S : W->settlements) if (!S.dead && S.pop > BestPop) { BestPop = S.pop; A.Cell = S.cell; }
		if (A.Cell < 0) A.Cell = 0;
		Ctx.Log(FString::Printf(TEXT("DmAB: world at year %.0f (age %d, %d settlements) in %.1f s; city cell %d; %d views x %d configs x %d rounds; %s"),
			W ? W->year : 0.0, W ? W->age : 0, W ? int32(dm::civ::live(*W).size()) : 0, FPlatformTime::Seconds() - T0, A.Cell,
			A.Views.Num(), A.Configs.Num(), A.Rounds, *DmRenderTiers::Describe()));
		F->StartTitle();
		if (UDmScreenSubsystem* Sc = UDmScreenSubsystem::Get(Ctx.World)) Sc->CloseAll();
	},
	[](FDmScenarioContext& Ctx, float Dt)
	{
		if (!GAb.IsValid()) return;
		FAb& A = *GAb;
		if (Ctx.Frame == 4)
		{
			if (ADmFlow* F = ADmFlow::Find(Ctx.World)) F->EnterPlayForTests();
		}
		if (Ctx.Frame < 6 || A.ViewIdx >= A.Views.Num()) return;
		// the sim stays where the history left it (stable frames for A/B), unless -abspeed= lets it run
		Ctx.Sim->SetSpeed(A.Speed > 0.f ? A.Speed : 1.f);
		Ctx.Sim->SetPaused(A.Speed <= 0.f);

		const FAbView& V = GAbViews[A.Views[A.ViewIdx]];
		const FAbConfig& C = A.Configs[A.CfgIdx];
		++A.PhaseFrame;
		switch (A.Phase)
		{
		case 0:
		{
			const dm::World* W = Ctx.Sim->Sim();
			if (W && W->valid(A.Cell))
			{
				const FVector Dir = DmCoords::CellDir(*W, A.Cell);
				if (ADmPawn* Pw = ADmPawn::Find(Ctx.World)) { Pw->Rig().StopMotion(); Pw->Jump(Dir, V.Zoom, FMath::DegreesToRadians(V.Heading)); }
				if (ADmPlanet* Pl = ADmPlanet::Find(Ctx.World)) Pl->Settle(40);
				double Lat, Lon;
				DmCoords::DirToLatLon(SunFor(Dir, V.SunElev, 200.0), Lat, Lon);
				Ctx.Sim->PinSunAt(Lat, Lon);
			}
			A.Phase = 1; A.PhaseFrame = 0;
			break;
		}
		case 1:
			if (A.PhaseFrame >= A.ViewSettle) { ApplyConfig(Ctx, A, C); A.Phase = 2; A.PhaseFrame = 0; }
			break;
		case 2:
			if (A.PhaseFrame == A.Settle - 4 && A.Round == 0 && A.bProfile)
			{
				GEngine->Exec(Ctx.World, TEXT("r.ProfileGPU.ShowUI 0"));
				GEngine->Exec(Ctx.World, TEXT("ProfileGPU"));
				UE_LOG(LogDmAB, Display, TEXT("DmAB profile: view %s config %s (the ProfileGPU dump follows)"), V.Name, *C.Name);
			}
			if (A.PhaseFrame >= A.Settle) { A.Phase = 3; A.PhaseFrame = 0; }
			break;
		case 3:
		{
			double G = 0, R = 0, Rh = 0, Gp = 0;
			Ctx.Mode->GetLastUnitMs(G, R, Rh, Gp);
			FAbSample& S = A.Samples.FindOrAdd(FString(V.Name) + TEXT("|") + C.Name);
			S.Gpu.Add(float(Gp));
			S.Frame.Add(float(FApp::GetDeltaTime() * 1000.0));
			if (A.PhaseFrame >= A.Win)
			{
				if (A.bShots && A.Round == 0)
					Ctx.ShotPath(FPaths::Combine(Ctx.Mode->ShotsDir(), FString::Printf(TEXT("ue-ab-%s-%s.png"), V.Name, *C.Name)), true);
				RestoreConfig(A);
				A.Phase = 2; A.PhaseFrame = 0;
				if (++A.CfgIdx >= A.Configs.Num())
				{
					A.CfgIdx = 0;
					if (++A.Round >= A.Rounds)
					{
						ReportView(A, A.ViewIdx);
						A.Round = 0;
						A.Phase = 0;
						if (++A.ViewIdx >= A.Views.Num())
						{
							UE_LOG(LogDmAB, Display, TEXT("DmAB: RESULT done (%d views, %d configs)"), A.Views.Num(), A.Configs.Num());
							Ctx.Finish();
							return;
						}
						break;
					}
				}
				ApplyConfig(Ctx, A, A.Configs[A.CfgIdx]);
			}
			break;
		}
		default: break;
		}
	});

// ------------------------------------------------------------------ perf-tiers (wave3 perf verification)
// The tier switches' regression check, no rendering needed (-noshot; ends with "DmTiers RESULT PASS|FAIL"):
//   - every tier sets its resolution (Low 50, Medium 62, High / Epic the display default r.ScreenPercentage 0) in any
//     order, including Low -> High and Low -> Auto as the settings screen does it (non-forced SetQualityLevels);
//   - GetTier() follows; High and Epic set every console variable of the tier file to the same value (the settings
//     screen's "High" and "Auto" on a first run must be one look); Medium and Low differ from High;
//   - temporary tiers (-tier=) leave the saved levels alone and restore them when switched off.
namespace
{
	// console variables whose value the tiers must agree on at High and Epic: every key of every group the tier file
	// or the engine's base file sets (read from the merged scalability ini, so a new key is checked without a code change)
	TArray<FString> TierCVarNames()
	{
		TSet<FString> Names;
		const TCHAR* Groups[] = { TEXT("ViewDistanceQuality"), TEXT("AntiAliasingQuality"), TEXT("ShadowQuality"), TEXT("GlobalIlluminationQuality"),
			TEXT("ReflectionQuality"), TEXT("PostProcessQuality"), TEXT("TextureQuality"), TEXT("EffectsQuality"), TEXT("FoliageQuality"), TEXT("ShadingQuality") };
		for (const TCHAR* G : Groups)
			for (int32 L = 0; L <= 3; ++L)
			{
				TArray<FString> Lines;
				if (!GConfig->GetSection(*FString::Printf(TEXT("%s@%d"), G, L), Lines, GScalabilityIni)) continue;
				for (const FString& Line : Lines) { FString K, V; if (Line.Split(TEXT("="), &K, &V)) Names.Add(K.TrimStartAndEnd()); }
			}
		TArray<FString> Out = Names.Array();
		Out.Sort();
		return Out;
	}

	TMap<FString, FString> ReadCVars(const TArray<FString>& Names)
	{
		TMap<FString, FString> Out;
		for (const FString& N : Names)
			if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(*N)) Out.Add(N, V->GetString());
		return Out;
	}

	float ScreenPct()
	{
		IConsoleManager::Get().CallAllConsoleVariableSinks();   // what the engine does once per frame (the resolution sink)
		static IConsoleVariable* Sp = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
		return Sp ? Sp->GetFloat() : -1.f;
	}
}

static FDmScenarioRegistrar GDmScenarioPerfTiers(TEXT("perf-tiers"), TEXT("perf"),
	TEXT("regression check of the quality tiers: resolution in any order, GetTier, High == Epic, temporary tiers; DmTiers lines"),
	[](FDmScenarioContext& Ctx) { Ctx.HoldExit(); },
	[](FDmScenarioContext& Ctx, float Dt)
	{
		if (Ctx.Frame != 3) return;
		int32 Fails = 0;
		auto Check = [&](const FString& What, bool bOk)
		{
			UE_LOG(LogDmAB, Display, TEXT("DmTiers %s: %s"), *What, bOk ? TEXT("ok") : TEXT("FAIL"));
			if (!bOk) ++Fails;
		};
		UE_LOG(LogDmAB, Display, TEXT("DmTiers start: %s"), *DmRenderTiers::Describe());
		// a -tier= run holds temporary levels, under which the settings screen's (non-forced) changes only reach the
		// saved levels: the checks below are about the player's levels, so start from those
		if (Scalability::IsTemporaryQualityLevelActive())
		{
			Scalability::ToggleTemporaryQualityLevels(false, Scalability::FQualityLevels());
			ScreenPct();
			UE_LOG(LogDmAB, Display, TEXT("DmTiers -tier= levels switched off for the check: %s"), *DmRenderTiers::Describe());
		}
		const Scalability::FQualityLevels Start = Scalability::GetQualityLevels();
		const float Want[4] = { 50.f, 62.f, 0.f, 0.f };
		// every ordered pair of tiers, forced (dm.tier / -tier) applies
		for (int32 A = 0; A <= 3; ++A)
			for (int32 B = 0; B <= 3; ++B)
			{
				DmRenderTiers::ApplyTier(A);
				const float SpA = ScreenPct();
				DmRenderTiers::ApplyTier(B);
				const float SpB = ScreenPct();
				Check(FString::Printf(TEXT("tier %d -> %d: screen percentage %.0f then %.0f (want %.0f, %.0f), GetTier %d"), A, B, SpA, SpB, Want[A], Want[B], DmRenderTiers::GetTier()),
					FMath::IsNearlyEqual(SpA, Want[A]) && FMath::IsNearlyEqual(SpB, Want[B]) && DmRenderTiers::GetTier() == B);
			}
		// the settings screen's path: non-forced SetQualityLevels (Low, then Auto = the start levels, then High)
		{
			Scalability::FQualityLevels L = Scalability::GetQualityLevels();
			L.SetFromSingleQualityLevel(0);
			Scalability::SetQualityLevels(L);
			const float SpLow = ScreenPct();
			Scalability::SetQualityLevels(Start);
			const float SpAuto = ScreenPct();
			L.SetFromSingleQualityLevel(2);
			Scalability::SetQualityLevels(L);
			const float SpHigh = ScreenPct();
			Check(FString::Printf(TEXT("screens Low -> Auto -> High: %.0f / %.0f / %.0f (want 50 / %.0f / 0)"), SpLow, SpAuto, SpHigh, Start.ResolutionQuality),
				FMath::IsNearlyEqual(SpLow, 50.f) && FMath::IsNearlyEqual(SpAuto, Start.ResolutionQuality) && FMath::IsNearlyEqual(SpHigh, 0.f));
		}
		// High == Epic, Medium / Low differ
		{
			const TArray<FString> Names = TierCVarNames();
			// two passes in opposite orders: a tier whose result depends on the tier before it (a key that one level
			// of a group sets and another leaves alone) shows up as a difference between the passes
			TMap<FString, FString> ByTier[4], ByTierDown[4];
			for (int32 T = 0; T <= 3; ++T) { DmRenderTiers::ApplyTier(T); ScreenPct(); ByTier[T] = ReadCVars(Names); }
			for (int32 T = 3; T >= 0; --T) { DmRenderTiers::ApplyTier(T); ScreenPct(); ByTierDown[T] = ReadCVars(Names); }
			int32 Order = 0;
			for (int32 T = 0; T <= 3; ++T)
				for (const FString& N : Names)
				{
					const FString* U = ByTier[T].Find(N); const FString* D = ByTierDown[T].Find(N);
					if (U && D && *U != *D) { ++Order; UE_LOG(LogDmAB, Display, TEXT("DmTiers   tier %d %s=%s going up but %s going down"), T, *N, **U, **D); }
				}
			Check(TEXT("every tier sets the same values whatever tier came before"), Order == 0);
			int32 Diff23 = 0, Diff12 = 0, Diff01 = 0;
			for (const FString& N : Names)
			{
				const FString* V2 = ByTier[2].Find(N); const FString* V3 = ByTier[3].Find(N);
				if (V2 && V3 && *V2 != *V3) { ++Diff23; UE_LOG(LogDmAB, Display, TEXT("DmTiers   High %s=%s but Epic %s"), *N, **V2, **V3); }
				const FString* V1 = ByTier[1].Find(N); const FString* V0 = ByTier[0].Find(N);
				if (V1 && V2 && *V1 != *V2) ++Diff12;
				if (V0 && V1 && *V0 != *V1) ++Diff01;
			}
			Check(FString::Printf(TEXT("High and Epic set the same %d console variables"), Names.Num()), Diff23 == 0);
			Check(FString::Printf(TEXT("Medium differs from High in %d, Low from Medium in %d"), Diff12, Diff01), Diff12 > 0 && Diff01 > 0);
		}
		// temporary tiers: the saved levels stay, and come back
		{
			DmRenderTiers::ApplyTier(3);
			ScreenPct();
			DmRenderTiers::ApplyTier(0, true);
			const float SpTmp = ScreenPct();
			const bool bTmp = Scalability::IsTemporaryQualityLevelActive();
			const int32 TierTmp = DmRenderTiers::GetTier();
			Scalability::ToggleTemporaryQualityLevels(false, Scalability::FQualityLevels());
			const float SpBack = ScreenPct();
			Check(FString::Printf(TEXT("temporary Low: active %d, %.0f %%, GetTier %d; switched off: %.0f %%, GetTier %d"), bTmp ? 1 : 0, SpTmp, TierTmp, SpBack, DmRenderTiers::GetTier()),
				bTmp && FMath::IsNearlyEqual(SpTmp, 50.f) && TierTmp == 0 && FMath::IsNearlyEqual(SpBack, 0.f) && DmRenderTiers::GetTier() == 3);
		}
		Scalability::SetQualityLevels(Start, true);
		ScreenPct();
		UE_LOG(LogDmAB, Display, TEXT("DmTiers end: %s"), *DmRenderTiers::Describe());
		UE_LOG(LogDmAB, Display, TEXT("DmTiers RESULT %s (%d failed)"), Fails ? TEXT("FAIL") : TEXT("PASS"), Fails);
		if (Fails) Ctx.SetExitCode(1);
		Ctx.Finish();
	});
