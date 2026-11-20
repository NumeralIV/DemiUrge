// DEMIURGE — Game/Core/DmRenderTiers.cpp : see DmRenderTiers.h. Owner: perf (lane perf3).

#include "Game/Core/DmRenderTiers.h"

#include "Containers/Ticker.h"
#include "DynamicRHI.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "RenderTimer.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/Parse.h"
#include "Scalability.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmTier, Log, All);

int32 DmRenderTiers::ParseTier(const FString& S)
{
	const FString T = S.TrimStartAndEnd().ToLower();
	if (T == TEXT("low") || T == TEXT("0")) return 0;
	if (T == TEXT("medium") || T == TEXT("med") || T == TEXT("1")) return 1;
	if (T == TEXT("high") || T == TEXT("2")) return 2;
	if (T == TEXT("epic") || T == TEXT("3")) return 3;
	return -1;
}

void DmRenderTiers::ApplyTier(int32 Tier, bool bTemporary)
{
	Tier = FMath::Clamp(Tier, 0, 3);
	Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();
	Levels.SetFromSingleQualityLevel(Tier);   // also the resolution quality (Config/DefaultScalability.ini PerfIndexValues)
	if (bTemporary && !Scalability::IsTemporaryQualityLevelActive()) Scalability::ToggleTemporaryQualityLevels(true, Levels);
	Scalability::SetQualityLevels(Levels, true);
	UE_LOG(LogDmTier, Display, TEXT("DmTier: applied tier %d%s -> %s"), Tier, bTemporary ? TEXT(" (temporary: not saved)") : TEXT(""), *Describe());
}

namespace
{
	// the sg.* cvars themselves: Scalability::GetQualityLevels() returns the saved levels while -tier= holds temporary ones
	int32 Sg(const TCHAR* Name)
	{
		IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name);
		return V ? V->GetInt() : -1;
	}
}

int32 DmRenderTiers::GetTier()
{
	const TCHAR* Groups[] = { TEXT("sg.ViewDistanceQuality"), TEXT("sg.AntiAliasingQuality"), TEXT("sg.ShadowQuality"), TEXT("sg.GlobalIlluminationQuality"),
		TEXT("sg.ReflectionQuality"), TEXT("sg.PostProcessQuality"), TEXT("sg.TextureQuality"), TEXT("sg.EffectsQuality"), TEXT("sg.FoliageQuality"), TEXT("sg.ShadingQuality") };
	int32 Min = 3;
	for (const TCHAR* G : Groups) Min = FMath::Min(Min, Sg(G));
	return FMath::Clamp(Min, 0, 3);
}

FString DmRenderTiers::Describe()
{
	static IConsoleVariable* Sp = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	static IConsoleVariable* Rq = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.ResolutionQuality"));
	return FString::Printf(TEXT("sg levels: res %.0f (r.ScreenPercentage %.1f) view %d aa %d shadow %d gi %d refl %d pp %d tex %d fx %d foliage %d shading %d%s"),
		Rq ? Rq->GetFloat() : -1.f, Sp ? Sp->GetFloat() : -1.f, Sg(TEXT("sg.ViewDistanceQuality")), Sg(TEXT("sg.AntiAliasingQuality")), Sg(TEXT("sg.ShadowQuality")),
		Sg(TEXT("sg.GlobalIlluminationQuality")), Sg(TEXT("sg.ReflectionQuality")), Sg(TEXT("sg.PostProcessQuality")), Sg(TEXT("sg.TextureQuality")),
		Sg(TEXT("sg.EffectsQuality")), Sg(TEXT("sg.FoliageQuality")), Sg(TEXT("sg.ShadingQuality")),
		Scalability::IsTemporaryQualityLevelActive() ? TEXT(" (temporary)") : TEXT(""));
}

bool DmRenderTiers::SetCVarKeepPriority(const FString& Name, const FString& Value, FString* OutPrevious)
{
	IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(*Name);
	if (!V) return false;
	if (OutPrevious) *OutPrevious = V->GetString();
	const EConsoleVariableFlags Prio = EConsoleVariableFlags(uint32(V->GetFlags()) & uint32(ECVF_SetByMask));
	V->Set(*Value, Prio);
	return true;
}

namespace
{
	FAutoConsoleCommand GDmTierCommand(
		TEXT("dm.tier"),
		TEXT("dm.tier <low|medium|high|epic|0..3>: apply a DEMIURGE quality tier to every scalability group; no argument prints the levels"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() == 0) { UE_LOG(LogDmTier, Display, TEXT("DmTier: %s"), *DmRenderTiers::Describe()); return; }
			const int32 T = DmRenderTiers::ParseTier(Args[0]);
			if (T < 0) { UE_LOG(LogDmTier, Warning, TEXT("DmTier: unknown tier '%s' (low, medium, high, epic)"), *Args[0]); return; }
			DmRenderTiers::ApplyTier(T);
		}));

	// High and Epic use resolution quality 0, "the engine's display-based screen percentage" (Config/DefaultScalability.ini).
	// The engine treats 0 as "leave r.ScreenPercentage alone", so after Low (50) or Medium (62) a switch back to High or
	// Auto would keep the low resolution. This sink, run whenever console variables changed, puts r.ScreenPercentage back
	// to 0 (the display-based default) when scalability set it and the resolution quality now says 0.
	void DmResolutionSink()
	{
		static IConsoleVariable* Rq = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.ResolutionQuality"));
		static IConsoleVariable* Sp = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
		if (!Rq || !Sp) return;
		const uint32 SetBy = uint32(Sp->GetFlags()) & uint32(ECVF_SetByMask);
		if (Rq->GetFloat() <= 0.f && SetBy == uint32(ECVF_SetByScalability) && Sp->GetFloat() > 0.f)
		{
			Sp->Set(0.f, ECVF_SetByScalability);
			UE_LOG(LogDmTier, Display, TEXT("DmTier: resolution quality 0 -> r.ScreenPercentage back to the display-based default"));
		}
	}

	// -gpulog=<seconds>: every that many seconds, one "DmGpu" line with the frame, game, render and GPU times of the frames
	// since the last line (works in any scenario and in the packaged game; the playthrough's shot lines date each view)
	struct FDmGpuLog
	{
		double Period = 1.0, T0 = 0.0, Start = 0.0;
		TArray<float> Frame, Gpu, Game, Render;

		static double Pct(TArray<float>& V, double Q)
		{
			if (V.Num() == 0) return 0.0;
			V.Sort();
			return double(V[FMath::Clamp(int32(Q * (V.Num() - 1) + 0.5), 0, V.Num() - 1)]);
		}
		static double Avg(const TArray<float>& V) { double S = 0; for (float X : V) S += X; return V.Num() ? S / V.Num() : 0.0; }

		bool Tick(float Dt)
		{
			const double Now = FPlatformTime::Seconds();
			if (Start == 0.0) { Start = T0 = Now; return true; }
			Frame.Add(Dt * 1000.f);
			Gpu.Add(float(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0))));
			Game.Add(float(FPlatformTime::ToMilliseconds(GGameThreadTime)));
			Render.Add(float(FPlatformTime::ToMilliseconds(GRenderThreadTime)));
			if (Now - T0 >= Period)
			{
				static IConsoleVariable* Sp = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
				const double FrameAvg = Avg(Frame);
				UE_LOG(LogDmTier, Display, TEXT("DmGpu %.1f s: %d frames, frame avg %.2f ms (%.0f fps) p95 %.2f | gpu avg %.2f p50 %.2f p95 %.2f | game avg %.2f | render avg %.2f | sp %.0f"),
					Now - Start, Frame.Num(), FrameAvg, FrameAvg > 0 ? 1000.0 / FrameAvg : 0.0, Pct(Frame, 0.95), Avg(Gpu), Pct(Gpu, 0.5), Pct(Gpu, 0.95),
					Avg(Game), Avg(Render), Sp ? Sp->GetFloat() : -1.f);
				Frame.Reset(); Gpu.Reset(); Game.Reset(); Render.Reset();
				T0 = Now;
			}
			return true;
		}
	};
	FDmGpuLog GDmGpuLog;

	// -tier= on the command line: applied once the engine is up (after the settings screen applied the player's saved
	// quality during game-instance start-up, so the command line wins for tests and benchmarks)
	FDelayedAutoRegisterHelper GDmTierFromCommandLine(EDelayedRegisterRunPhase::EndOfEngineInit, []()
	{
		IConsoleManager::Get().RegisterConsoleVariableSink_Handle(FConsoleCommandDelegate::CreateStatic(&DmResolutionSink));
		DmResolutionSink();
		double LogPeriod = 0.0;
		if (FParse::Value(FCommandLine::Get(), TEXT("-gpulog="), LogPeriod) && LogPeriod > 0.0)
		{
			GDmGpuLog.Period = LogPeriod;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float Dt) { return GDmGpuLog.Tick(Dt); }));
		}
		FString S;
		if (!FParse::Value(FCommandLine::Get(), TEXT("-tier="), S)) return;
		const int32 T = DmRenderTiers::ParseTier(S);
		if (T < 0) { UE_LOG(LogDmTier, Warning, TEXT("DmTier: unknown -tier=%s (low, medium, high, epic)"), *S); return; }
		DmRenderTiers::ApplyTier(T, true);
	});
}
