// DEMIURGE — Game/Sky/DmSkyScenarios.cpp : the sky module's scenarios (SPEC-UNREAL 0.5). Owner: C2 sky.
//
//   shot.ps1 sky-orbit        the whole planet: weather systems, a hurricane, clear deserts, moon and stars
//   shot.ps1 sky-terminator   half lit: dusk on the clouds, city lights, moon, stars, milky way
//   shot.ps1 sky-cyclone      the tropical cyclone from 250 km, oblique
//   shot.ps1 sky-storm        the fixture's sim storm (a dm::Disaster) as a cyclone
//   shot.ps1 sky-front        a mid-latitude low with its fronts from 450 km
//   shot.ps1 sky-ground       under the clouds, 40 m above the sea, looking along the coast
//   shot.ps1 sky-shadows      cloud shadows on land from 9 km
//   shot.ps1 sky-night        the night side: moonlight, aurora, lightning in the storm
//   shot.ps1 sky-molten       genesis: the molten world (heat 1), ash veil glowing from below
//   shot.ps1 sky-water        genesis: young seas, thin clouds, comet rain in the black
//   shot.ps1 sky-time -seq=60,300,600,900   the sky clock running at 1x: the planet turns, weather moves
//   shot.ps1 sky-check -noshot  numeric checks of the frame / sun / storm bookkeeping (DmCheck lines)
// Every scenario also takes the sky switches of DmSkyActor.h (-skyrate=, -moon=, -cyclone=, -mapout=, ...).

#include "Game/Core/DmCoords.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Game/Sky/DmSkyActor.h"
#include "Sim/State.h"

#include "Engine/World.h"
#include "Misc/Paths.h"

namespace
{
	/** The strongest climatological tropical cyclone (or any tropical system); false if none. */
	bool FindCyclone(FDmScenarioContext& Ctx, double& Lat, double& Lon, double& RadDeg)
	{
		ADmSky* Sky = ADmSky::Get(Ctx.World);
		if (!Sky) return false;
		const FDmCyclone* Best = nullptr;
		// the world's own storm first (climatological hurricanes give way to it)
		for (const FDmCyclone& C : Sky->GetStorms().Systems)
			if (C.SimId >= 0) { Lat = C.LatDeg; Lon = C.LonDeg; RadDeg = FMath::RadiansToDegrees(C.RadiusRad); return true; }
		for (const FDmCyclone& C : Sky->GetStorms().Systems)
			if (C.Type == EDmCycloneType::Tropical && (!Best || C.Strength * (C.Sign > 0 ? 2.0 : 1.0) > Best->Strength * (Best->Sign > 0 ? 2.0 : 1.0))) Best = &C;
		if (!Best) return false;
		Lat = Best->LatDeg; Lon = Best->LonDeg; RadDeg = FMath::RadiansToDegrees(Best->RadiusRad);
		return true;
	}

	/** A sim storm in the world (fixture), or -1. */
	int32 SimStormCell(const dm::World* W)
	{
		if (!W) return -1;
		for (const dm::Disaster& D : W->disasters) if (D.type == dm::DisasterType::Storm && W->valid(D.cell)) return D.cell;
		return -1;
	}

	void CellLatLon(const dm::World* W, int32 Cell, double& Lat, double& Lon)
	{
		Lat = 0; Lon = 0;
		if (W && W->valid(Cell)) DmCoords::DirToLatLon(DmCoords::CellDir(*W, Cell), Lat, Lon);
	}

	/** Land cell nearest to (lat, lon) whose land fraction around is high: for shadow / ground shots. */
	int32 CoastCell(const dm::World* W, double Lat, double Lon, bool bWantLand)
	{
		if (!W || !W->geo) return -1;
		const dm::Vec3 S = DmCoords::UEToSim(DmCoords::LatLonToDir(Lat, Lon));
		const int32 Start = W->geo->cellAt(S.x, S.y, S.z);
		std::vector<int32_t> Ring;
		W->geo->ring(Start, 30, Ring);
		for (int32_t C : Ring)
		{
			const bool bSea = dm::state::isSea(*W, C);
			if (bSea != bWantLand) return C;
		}
		return Start;
	}


	void Log(FDmScenarioContext& Ctx, const TCHAR* What, double Lat, double Lon)
	{
		Ctx.Log(FString::Printf(TEXT("sky: %s at lat %.1f lon %.1f"), What, Lat, Lon));
	}
}

// ------------------------------------------------------------------------------------------------ orbit

static FDmScenarioRegistrar GSkyOrbit(TEXT("sky-orbit"), TEXT("sky"),
	TEXT("the whole planet from orbit: weather systems, a hurricane, clear deserts, moon and stars"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		double Lat = 15, Lon = 20, R = 9;
		FindCyclone(Ctx, Lat, Lon, R);
		Log(Ctx, TEXT("tropical cyclone"), Lat, Lon);
		// frame the cyclone off-centre, sun high and to the west (sim longitude grows westward)
		Ctx.LookAt(FMath::Clamp(Lat - 12.0, -40.0, 40.0), Lon - 18.0, 700.0, 0.0, 90.0, 30.0);
		Ctx.SetSubsolar(FMath::Clamp(Lat - 8.0, -23.0, 23.0), Lon + 30.0);
		// the moon in the black beside the planet (unless the command line places it)
		if (ADmSky* Sky = ADmSky::Get(Ctx.World)) { FString M; if (!FDmShotArgs::Value(TEXT("-moon="), M)) Sky->SetMoonInView(-11.5, 5.0, 25.0, 27.0); }
	});

static FDmScenarioRegistrar GSkyTerminator(TEXT("sky-terminator"), TEXT("sky"),
	TEXT("half lit from 400 km: dusk on the clouds, city lights, the moon and the milky way"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		double Lat = 15, Lon = 20, R = 9;
		FindCyclone(Ctx, Lat, Lon, R);
		Ctx.LookAt(Lat - 5.0, Lon - 8.0, 400.0, 0.0, 90.0, 30.0);
		Ctx.SetSubsolar(8.0, Lon + 85.0);
		if (ADmSky* Sky = ADmSky::Get(Ctx.World)) { FString M; if (!FDmShotArgs::Value(TEXT("-moon="), M)) Sky->SetMoonInView(-13.0, 6.5, 25.0, 27.0); }
	});

// ------------------------------------------------------------------------------------------------ cyclone

static FDmScenarioRegistrar GSkyCyclone(TEXT("sky-cyclone"), TEXT("sky"),
	TEXT("the tropical cyclone from 250 km, looking down at an angle, morning sun"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		double Lat = 15, Lon = 20, R = 9;
		FindCyclone(Ctx, Lat, Lon, R);
		Log(Ctx, TEXT("tropical cyclone"), Lat, Lon);
		Ctx.LookAt(Lat, Lon, 250.0, 0.0, 70.0, 45.0);
		Ctx.SetSubsolar(FMath::Clamp(Lat - 10.0, -23.0, 23.0), Lon + 40.0);
	});

static FDmScenarioRegistrar GSkyCycloneTop(TEXT("sky-cyclone-top"), TEXT("sky"),
	TEXT("the tropical cyclone straight down from 120 km, north up (checks the spin sense)"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		double Lat = 15, Lon = 20, R = 9;
		FindCyclone(Ctx, Lat, Lon, R);
		Log(Ctx, TEXT("tropical cyclone"), Lat, Lon);
		Ctx.LookAt(Lat, Lon, 120.0, 0.0, 90.0, 40.0);
		Ctx.SetSubsolar(FMath::Clamp(Lat, -23.0, 23.0), Lon + 25.0);
	});

static FDmScenarioRegistrar GSkyFront(TEXT("sky-front"), TEXT("sky"),
	TEXT("a mid-latitude low with its fronts (comma head, cold front, warm-front shield) from 450 km"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		double Lat = 50, Lon = 0;
		if (ADmSky* Sky = ADmSky::Get(Ctx.World))
		{
			const FDmCyclone* Best = nullptr;
			for (const FDmCyclone& C : Sky->GetStorms().Systems)
				if (C.Type == EDmCycloneType::Extratropical && (!Best || C.Strength * (C.Sign > 0 ? 1.5 : 1.0) > Best->Strength * (Best->Sign > 0 ? 1.5 : 1.0))) Best = &C;
			if (Best) { Lat = Best->LatDeg; Lon = Best->LonDeg; }
		}
		Log(Ctx, TEXT("low"), Lat, Lon);
		// the fronts trail equatorward: aim a little toward the equator, sun from the south-west
		const double Eq = Lat > 0 ? -1.0 : 1.0;
		Ctx.LookAt(Lat + Eq * 9.0, Lon, 450.0, 0.0, 90.0, 40.0);
		Ctx.SetSubsolar(FMath::Clamp(Lat + Eq * 35.0, -23.0, 23.0), Lon + 25.0);
	});

static FDmScenarioRegistrar GSkyStorm(TEXT("sky-storm"), TEXT("sky"),
	TEXT("the world's own storm (a sim disaster) seen from 180 km"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		const dm::World* W = Ctx.Sim ? Ctx.Sim->Sim() : nullptr;
		const int32 Cell = SimStormCell(W);
		double Lat = 10, Lon = 0;
		if (Cell >= 0) CellLatLon(W, Cell, Lat, Lon);
		else Ctx.Log(TEXT("sky: no storm in this world"));
		Log(Ctx, TEXT("sim storm"), Lat, Lon);
		// the storm follows a disaster that appears in the first frames: let it fade in before the shot
		Ctx.LookAt(Lat - 2.0, Lon, 180.0, 0.0, 75.0, 45.0);
		Ctx.SetSubsolar(FMath::Clamp(Lat - 6.0, -23.0, 23.0), Lon + 35.0);
	});

// ------------------------------------------------------------------------------------------------ low views

static FDmScenarioRegistrar GSkyGround(TEXT("sky-ground"), TEXT("sky"),
	TEXT("under the clouds, 40 m above the sea near a coast, looking toward land"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		const dm::World* W = Ctx.Sim ? Ctx.Sim->Sim() : nullptr;
		// a mid-latitude coast in the cloudy belt
		const int32 Sea = CoastCell(W, 48.0, 30.0, false);
		double Lat = 48, Lon = 30;
		CellLatLon(W, Sea, Lat, Lon);
		Log(Ctx, TEXT("coast"), Lat, Lon);
		Ctx.LookAt(Lat, Lon, 0.6, 30.0, 4.0, 75.0);
		Ctx.SetSubsolar(10.0, Lon + 55.0);
	});

static FDmScenarioRegistrar GSkyShadows(TEXT("sky-shadows"), TEXT("sky"),
	TEXT("cloud shadows on land from 9 km, low sun"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		const dm::World* W = Ctx.Sim ? Ctx.Sim->Sim() : nullptr;
		int32 Land = CoastCell(W, 45.0, 20.0, true);
		// look for land under broken cumulus (cover near the edge value, heaps): that is where shadows read
		if (ADmSky* Sky = ADmSky::Get(Ctx.World))
			if (W)
			{
				double Best = -1.0;
				for (int32 C = 0; C < W->n; C += 3)
				{
					if (dm::state::isSea(*W, C)) continue;
					const FVector D = DmCoords::CellDir(*W, C);
					const double La = FMath::RadiansToDegrees(FMath::Asin(D.Z));
					if (FMath::Abs(La) < 10.0 || FMath::Abs(La) > 60.0) continue;
					const FLinearColor Cv = Sky->CoverAt(D);
					const double Score = (1.0 - FMath::Abs(Cv.R - 0.45) * 3.0) + 0.6 * Cv.B - 0.4 * Cv.A;
					if (Score > Best) { Best = Score; Land = C; }
				}
			}
		double Lat = 45, Lon = 20;
		CellLatLon(W, Land, Lat, Lon);
		Log(Ctx, TEXT("land"), Lat, Lon);
		Ctx.LookAt(Lat, Lon, 14.0, 0.0, 40.0, 60.0);
		Ctx.SetSubsolar(15.0, Lon + 60.0);
	});

// ------------------------------------------------------------------------------------------------ night

static FDmScenarioRegistrar GSkyNight(TEXT("sky-night"), TEXT("sky"),
	TEXT("the night side from 500 km: moonlit clouds, aurora, lightning in the storm, city lights"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		double Lat = 15, Lon = 20, R = 9;
		FindCyclone(Ctx, Lat, Lon, R);
		// look at the cyclone from the north-east, the sun on the far side of the planet
		Ctx.LookAt(Lat + 22.0, Lon, 500.0, 180.0, 55.0, 40.0);
		Ctx.SetSubsolar(-5.0, Lon + 180.0);
		if (ADmSky* Sky = ADmSky::Get(Ctx.World))
			if (Sky->Tune.MoonLux <= 0.0) Sky->Tune.MoonLux = 0.6;
	},
	[](FDmScenarioContext& Ctx, float)
	{
		// keep strokes going in the cyclone even though the fixture's clock is stopped
		if (ADmSky* Sky = ADmSky::Get(Ctx.World))
			if (Ctx.Frame % 7 == 0)
			{
				double Lat = 15, Lon = 20, R = 9;
				if (FindCyclone(Ctx, Lat, Lon, R))
				{
					FRandomStream Rs(Ctx.Frame);
					const FVector D = DmCoords::LatLonToDir(Lat + Rs.FRandRange(-0.5, 0.5) * R, Lon + Rs.FRandRange(-0.5, 0.5) * R);
					Sky->GetStormsMutable().Strike(D, Rs.FRandRange(0.6, 1.0));
				}
			}
	});

// ------------------------------------------------------------------------------------------------ genesis

static FDmScenarioRegistrar GSkyMolten(TEXT("sky-molten"), TEXT("sky"),
	TEXT("genesis: the molten world (heat 1) under its ash veil"),
	[](FDmScenarioContext& Ctx)
	{
		if (dm::World* W = Ctx.Sim ? Ctx.Sim->SimMutable() : nullptr) { W->heat = 1.0; W->phase = dm::Phase::Molten; W->dust = 0.6; }
		Ctx.BuildScene();
		Ctx.LookAt(10.0, 40.0, 600.0, 0.0, 90.0, 30.0);
		Ctx.SetSubsolar(10.0, 95.0);
	});

static FDmScenarioRegistrar GSkyWater(TEXT("sky-water"), TEXT("sky"),
	TEXT("genesis: the Water phase, thin clouds over young seas, comet rain in the black"),
	[](FDmScenarioContext& Ctx)
	{
		if (dm::World* W = Ctx.Sim ? Ctx.Sim->SimMutable() : nullptr) { W->heat = 0.0; W->phase = dm::Phase::Water; }
		Ctx.BuildScene();
		Ctx.LookAt(10.0, 40.0, 900.0, 0.0, 90.0, 40.0);
		Ctx.SetSubsolar(10.0, 80.0);
	});

// ------------------------------------------------------------------------------------------------ time

static FDmScenarioRegistrar GSkyTime(TEXT("sky-time"), TEXT("sky"),
	TEXT("the sky clock at 1x (-skyrate=1 unless given): the planet turns under the sun, the weather moves"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		ADmSky* Sky = ADmSky::Get(Ctx.World);
		double R = -1;
		if (Sky && !FDmShotArgs::Value(TEXT("-skyrate="), R)) Sky->SetForcedRate(1.0);
		Ctx.LookAt(20.0, 30.0, 700.0, 0.0, 90.0, 30.0);
		if (Ctx.Sim) Ctx.Sim->UnpinSun();
	});

// ------------------------------------------------------------------------------------------------ checks

static FDmScenarioRegistrar GSkyCheck(TEXT("sky-check"), TEXT("sky"),
	TEXT("numeric checks: planet frame and sun publishing, pin / unpin continuity, day length, storms follow the sim"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		Ctx.HoldExit();
		Ctx.LookAt(20.0, 30.0, 700.0, 0.0, 90.0, 30.0);
	},
	[](FDmScenarioContext& Ctx, float)
	{
		static int32 Fails = 0;
		static double Yaw0 = 0, T0 = 0, SubLon0 = 0;
		static int64 Maps0 = 0;
		static FVector Sun0;
		ADmSky* Sky = ADmSky::Get(Ctx.World);
		UDmSimSubsystem* Sim = Ctx.Sim;
		if (!Sky || !Sim) { Ctx.Log(TEXT("DmCheck FAIL no sky")); Ctx.SetExitCode(1); Ctx.Finish(); return; }
		auto Check = [&Ctx](bool bOk, const FString& What) { Ctx.Log(FString::Printf(TEXT("DmCheck %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What)); if (!bOk) ++Fails; };
		if (Ctx.Frame == 5)
		{
			// static fixture: nothing turns
			Yaw0 = Sky->GetPlanetYaw(); T0 = Sky->GetSkyTime(); Sun0 = Sim->GetSunDirection();
			Check(FMath::IsNearlyEqual(Sim->GetPlanetToWorld().GetRotation().Rotator().Yaw, FMath::RadiansToDegrees(Yaw0), 1e-3), TEXT("planet frame published = sky yaw"));
			Check(Sim->GetPlanetToWorld().GetLocation().IsNearlyZero(), TEXT("planet centre at the world origin"));
			Sim->PinSunAt(10.0, 50.0);
		}
		if (Ctx.Frame == 30)
		{
			Check(FMath::IsNearlyEqual(Sky->GetPlanetYaw(), Yaw0, 1e-9), TEXT("static fixture: the planet does not turn"));
			const FVector Want = Sim->GetPlanetToWorld().TransformVectorNoScale(DmCoords::LatLonToDir(10.0, 50.0));
			Check(FVector::DotProduct(Want, Sim->GetSunDirection()) > 0.99999, TEXT("pinned sun kept"));
			Sun0 = Sim->GetSunDirection();
			{ double La; DmCoords::DirToLatLon(Sim->GetPlanetToWorld().InverseTransformVectorNoScale(Sun0), La, SubLon0); }
			Sim->UnpinSun();
			Sky->SetForcedRate(1.0);
			Yaw0 = Sky->GetPlanetYaw(); T0 = Sky->GetSkyTime();
			Maps0 = Sky->GetMapsBuilt();
		}
		if (Ctx.Frame == 31)
			Check(FVector::DotProduct(Sun0, Sim->GetSunDirection()) > 0.9999, TEXT("unpin resumes without a jump"));
		if (Ctx.Frame == 150)
		{
			const double Dt = Sky->GetSkyTime() - T0;
			const double Turned = FMath::UnwindRadians(Sky->GetPlanetYaw() - Yaw0);
			const double Expect = -Dt * 2.0 * UE_DOUBLE_PI / Sky->Tune.DayLength;
			Check(Dt > 0.5 && FMath::Abs(FMath::UnwindRadians(Turned - Expect)) < 1e-4, FString::Printf(TEXT("planet turns eastward once per %.0f sky s (%.3f s: %.4f rad, want %.4f)"), Sky->Tune.DayLength, Dt, Turned, Expect));
			// subsolar longitude must DEcrease... the sun moves west over the ground: sim longitude grows
			double La, Lo0 = SubLon0, Lo1;
			DmCoords::DirToLatLon(Sim->GetPlanetToWorld().InverseTransformVectorNoScale(Sim->GetSunDirection()), La, Lo1);
			Check(FMath::Fmod(Lo1 - Lo0 + 540.0, 360.0) - 180.0 > 0.0, FString::Printf(TEXT("the subsolar point moves west (sim lon %.2f -> %.2f)"), Lo0, Lo1));
			// a storm follows a sim disaster
			int32 Storms = 0, Followed = 0;
			if (const dm::World* W = Sim->Sim())
				for (const dm::Disaster& D : W->disasters)
					if (D.type == dm::DisasterType::Storm && W->valid(D.cell))
					{
						++Storms;
						for (const FDmCyclone& C : Sky->GetStorms().Systems)
							if (C.SimId == D.id && FVector::DotProduct(C.Dir, DmCoords::CellDir(*W, D.cell)) > FMath::Cos(FMath::DegreesToRadians(1.5)) && C.Strength > 0.3) ++Followed;
					}
			Check(Storms == Followed, FString::Printf(TEXT("sim storms drawn as cyclones (%d of %d)"), Followed, Storms));
			Check(Sky->GetMapsBuilt() > Maps0, FString::Printf(TEXT("weather maps rebuilt while sky time runs (%lld new)"), Sky->GetMapsBuilt() - Maps0));
			// the storm reads as a hurricane in the cover map: clear eye, dense overcast around it
			for (const FDmCyclone& C : Sky->GetStorms().Systems)
				if (C.SimId >= 0 && C.Strength > 0.5)
				{
					FVector E, N; DmCoords::Basis(C.Dir, E, N);
					const FVector Ring = (C.Dir + E * FMath::Tan(0.3 * C.RadiusRad)).GetSafeNormal();
					const float Eye = Sky->CoverAt(C.Dir).R, Cdo = Sky->CoverAt(Ring).R;
					// (the storm is still growing in the first seconds and the maps lag it a little: compare, do not threshold)
					Check(Eye < 0.35f && Cdo > Eye + 0.2f, FString::Printf(TEXT("storm: clear eye (cover %.2f) inside its overcast (%.2f)"), Eye, Cdo));
				}
			// deserts stay clear: dry land (moisture < 0.15) away from the equator
			if (const dm::World* W = Sim->Sim())
			{
				double Sum = 0; int32 Nd = 0;
				for (int32 C = 0; C < W->n; ++C)
				{
					if (dm::state::isSea(*W, C) || W->moist[C] >= 0.15f) continue;
					const FVector D = DmCoords::CellDir(*W, C);
					if (FMath::Abs(D.Z) < 0.2) continue;
					Sum += Sky->CoverAt(D).R; ++Nd;
				}
				const double Mean = Nd ? Sum / Nd : 0.0;
				Check(Nd == 0 || Mean < 0.2, FString::Printf(TEXT("deserts clear: mean cover %.3f over %d dry cells"), Mean, Nd));
			}
			const FString Map = FPaths::ProjectSavedDir() / TEXT("sky-check-weather.png");
			Check(Sky->SaveWeatherPreview(Map), TEXT("weather preview written ") + Map);
			Ctx.Log(FString::Printf(TEXT("DmCheck RESULT %s (%d failed)"), Fails ? TEXT("FAIL") : TEXT("PASS"), Fails));
			Ctx.SetExitCode(Fails ? 1 : 0);
			Ctx.Finish();
		}
	});
