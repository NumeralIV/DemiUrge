// DEMIURGE — Game/Sky/DmSkyAuroraScenarios.cpp : aurora shots and the aurora regression check.
// Owner: the aurora track (lanes/aurora3.owned).
//
//   shot.ps1 aurora-orbit          the whole planet from 700 km, northern winter night: the oval on the globe
//   shot.ps1 aurora-terminator     the same with the terminator through the oval (no aurora on the day side)
//   shot.ps1 aurora-pole           300 km straight above the north magnetic pole at night
//   shot.ps1 aurora-curtains       300 km from the pole, looking across the oval at 40 deg: curtains and rays
//   shot.ps1 aurora-limb           the night limb from 500 km: curtains standing against space
//   shot.ps1 aurora-ground         about 5 km up, south of the oval, looking north: curtains over the horizon
//                                  (the near-ground view the player gets when zoomed in at night)
//   shot.ps1 aurora-check -noshot  regression check (ends with "DmAuroraCheck RESULT PASS"):
//       the aurora part spawned and loaded M_Aurora, the sky's ribbon aurora is hidden, the cap follows the sea,
//       strength = the sky's gain, the FX surge path (ADmSky::BoostAurora) and Boost() raise it and decay,
//       the night weight is 1 over a pole in polar night and 0 under the sun.
// Any of them takes -auroraboost=<0..1> (a held surge), -aurorasteps=<n>, -noaurora, -aurora=<gain>.

#include "Game/Sky/DmSkyAurora.h"
#include "Game/Sky/DmSkyActor.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Sim/Sim.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "ProceduralMeshComponent.h"

namespace
{
	constexpr double PoleLat = 81.0, PoleLon = 60.0;   // FDmAuroraTune defaults (north)

	/** Sun over the far side of the north magnetic pole, south of the equator: polar night in the north. */
	void NorthernNight(FDmScenarioContext& Ctx, double SunLat = -20.0, double LonOffset = 180.0)
	{
		if (!Ctx.Sim || !Ctx.Sim->IsSunPinned()) Ctx.SetSubsolar(SunLat, PoleLon + LonOffset);
	}

	ADmAurora* FindAurora(UWorld* W)
	{
		if (ADmAurora* A = ADmAurora::Get(W)) return A;
		for (TActorIterator<ADmAurora> It(W); It; ++It) return *It;
		return nullptr;
	}

	struct FCheck
	{
		int32 Fails = 0;
		int32 Checks = 0;
		void Expect(FDmScenarioContext& Ctx, bool bOk, const FString& What)
		{
			++Checks;
			if (!bOk) ++Fails;
			Ctx.Log(FString::Printf(TEXT("DmAuroraCheck %s %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What));
		}
	};
	FCheck GCheck;
	float GStrength0 = 0.f, GStrengthSurge = 0.f;
}

static FDmScenarioRegistrar GAuroraOrbit(TEXT("aurora-orbit"), TEXT("aurora"),
	TEXT("the whole planet from 700 km in northern winter night: the auroral oval on the globe"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		Ctx.LookAt(50.0, PoleLon, 700.0, 0.0, 90.0, 30.0);
		NorthernNight(Ctx, -20.0, 210.0);
	});

static FDmScenarioRegistrar GAuroraTerminator(TEXT("aurora-terminator"), TEXT("aurora"),
	TEXT("the whole planet from 700 km with the terminator through the oval: only its night half glows"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		Ctx.LookAt(55.0, PoleLon, 700.0, 0.0, 90.0, 30.0);
		NorthernNight(Ctx, -15.0, 90.0);
	});

static FDmScenarioRegistrar GAuroraPole(TEXT("aurora-pole"), TEXT("aurora"),
	TEXT("300 km straight above the north magnetic pole at night: the oval, its folds and rays"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		Ctx.LookAt(PoleLat, PoleLon, 300.0, 0.0, 90.0, 40.0);
		NorthernNight(Ctx);
	});

static FDmScenarioRegistrar GAuroraCurtains(TEXT("aurora-curtains"), TEXT("aurora"),
	TEXT("300 km from the north magnetic pole, looking across the oval at 40 deg: curtains with violet tops"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		Ctx.LookAt(PoleLat - 6.0, PoleLon, 300.0, 0.0, 40.0, 50.0);
		NorthernNight(Ctx);
	});

static FDmScenarioRegistrar GAuroraLimb(TEXT("aurora-limb"), TEXT("aurora"),
	TEXT("the night limb from 500 km: curtains standing against space over the northern horizon"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		Ctx.LookAt(52.0, PoleLon, 500.0, 0.0, 22.0, 40.0);
		NorthernNight(Ctx);
	});

static FDmScenarioRegistrar GAuroraGround(TEXT("aurora-ground"), TEXT("aurora"),
	TEXT("about 5 km up, south of the oval, looking north at night: curtains with violet tops over the horizon"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		Ctx.LookAt(46.0, PoleLon, 40.0, 0.0, 7.0, 60.0);
		NorthernNight(Ctx);
	});

static FDmScenarioRegistrar GAuroraCheck(TEXT("aurora-check"), TEXT("aurora"),
	TEXT("regression check of the aurora part (shot.ps1 aurora-check -noshot; ends with DmAuroraCheck RESULT)"),
	[](FDmScenarioContext& Ctx)
	{
		GCheck = FCheck();
		Ctx.BuildScene();
		Ctx.HoldExit();
		Ctx.LookAt(PoleLat, PoleLon, 300.0, 0.0, 90.0, 40.0);
		Ctx.SetSubsolar(-20.0, PoleLon + 180.0);
	},
	[](FDmScenarioContext& Ctx, float)
	{
		ADmAurora* A = FindAurora(Ctx.World);
		ADmSky* Sky = ADmSky::Get(Ctx.World);
		if (Ctx.Frame == 20)
		{
			GCheck.Expect(Ctx, A != nullptr, TEXT("scene part 'aurora' spawned"));
			if (!A) { Ctx.Log(TEXT("DmAuroraCheck RESULT FAIL (no aurora actor)")); Ctx.SetExitCode(1); Ctx.Finish(); return; }
			GCheck.Expect(Ctx, A->IsActive(), TEXT("M_Aurora loaded (/Game/Sky/Aurora/M_Aurora)"));
			GCheck.Expect(Ctx, A->IsMaterialReady(), TEXT("M_Aurora compiled for this shader platform (not the default material)"));
			GCheck.Expect(Ctx, A->Shell && A->Shell->GetNumSections() == 2 && A->Shell->IsVisible(), TEXT("two polar caps, visible"));
			GCheck.Expect(Ctx, !Sky || !Sky->Aurora || !Sky->Aurora->IsVisible(), TEXT("the sky's old ribbon aurora is hidden"));
			const dm::World* W = Ctx.Sim ? Ctx.Sim->Sim() : nullptr;
			const double SeaKm = (W ? DmCoords::HeightToRadiusUU(W->seaLevel) : DmCoords::RadiusUU) / DmCoords::UUPerKm;
			GCheck.Expect(Ctx, FMath::Abs(A->GetShellRadiusKm() - (SeaKm + A->Tune.TopKm + 0.1)) < 0.05,
				FString::Printf(TEXT("cap radius %.2f km = sea %.2f + top %.1f + 0.1"), A->GetShellRadiusKm(), SeaKm, A->Tune.TopKm));
			const double Gain = Sky ? Sky->Tune.AuroraGain : A->Tune.DefaultGain;
			GStrength0 = A->GetStrength();
			GCheck.Expect(Ctx, FMath::Abs(GStrength0 - Gain) < 1e-3 || FDmShotArgs::Param(TEXT("auroraboost")),
				FString::Printf(TEXT("ambient strength %.3f = gain %.3f"), GStrength0, Gain));
			// the planet frame: the actor follows the planet
			const FQuat Q = Ctx.Sim ? Ctx.Sim->GetPlanetToWorld().GetRotation() : FQuat::Identity;
			GCheck.Expect(Ctx, A->GetActorQuat().AngularDistance(Q) < 1e-4, TEXT("the caps turn with the planet"));
			// night weights from the pinned sun
			const FVector SunP = Q.UnrotateVector(Ctx.Sim ? Ctx.Sim->GetSunDirection() : FVector(1, 0, 0));
			const double NN = ADmAurora::NightFactor(A->GetPole(0), SunP), NS = ADmAurora::NightFactor(A->GetPole(1), SunP);
			GCheck.Expect(Ctx, NN > 0.99, FString::Printf(TEXT("north pole in polar night: night weight %.3f"), NN));
			GCheck.Expect(Ctx, NS < 0.01, FString::Printf(TEXT("south pole in polar day: night weight %.3f (no aurora)"), NS));
			const FVector Sub = SunP.GetSafeNormal();
			GCheck.Expect(Ctx, ADmAurora::NightFactor(Sub, SunP) == 0.0 && ADmAurora::NightFactor(-Sub, SunP) == 1.0,
				TEXT("night weight 0 under the sun, 1 at midnight"));
			// the FX surge path: the sky's BoostAurora (DmFx::SetAurora calls it)
			if (Sky) Sky->BoostAurora(1.f, 4.f);
			else A->Boost(1.f, 4.f);
		}
		if (Ctx.Frame == 24 && A)
		{
			GStrengthSurge = A->GetStrength();
			GCheck.Expect(Ctx, A->GetBoost() > 0.9f && GStrengthSurge > GStrength0 * 2.5f,
				FString::Printf(TEXT("surge: boost %.2f, strength %.3f -> %.3f"), A->GetBoost(), GStrength0, GStrengthSurge));
		}
		if (A && Ctx.Frame == 400)
		{
			// the sky's surge lasts 4 s: by frame 400 it has decayed
			GCheck.Expect(Ctx, A->GetStrength() < GStrengthSurge - 1e-3,
				FString::Printf(TEXT("surge decays: strength %.3f after %.1f s"), A->GetStrength(), Ctx.Time));
			// own Boost() API
			A->Boost(1.f, 10.f);
		}
		if (Ctx.Frame == 402 && A)
		{
			GCheck.Expect(Ctx, A->GetStrength() >= GStrengthSurge * 0.97f, FString::Printf(TEXT("Boost(): strength %.3f"), A->GetStrength()));
			const bool bPass = GCheck.Fails == 0;
			Ctx.Log(FString::Printf(TEXT("DmAuroraCheck RESULT %s (%d checks, %d failed)"), bPass ? TEXT("PASS") : TEXT("FAIL"), GCheck.Checks, GCheck.Fails));
			Ctx.SetExitCode(bPass ? 0 : 1);
			Ctx.Finish();
		}
	});
