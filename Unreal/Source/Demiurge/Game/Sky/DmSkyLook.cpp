// DEMIURGE — Game/Sky/DmSkyLook.cpp : the sky's look-development sweep (wave 3). Owner: C2 sky.
//
//   shot.ps1 sky-look [-views=orbit-day,50km-golden,...] [-site=lat,lon] [-hold=<frames>] [-skyold] [-ab]
//                     [-variants=apNear:veilNear:veilOrbit:fill[:goldenEv[:moonNearLux:nightEvNear:cloudNight]],...]   (look development: each view is shot once per
//                     variant, <view>-v<i>, 150 frames apart; "-" keeps a value) [-lookprofile] (ProfileGPU at each shot)
//
// One launch, one forest site, every altitude the player sees (orbit 700 km, a terminator pass at 400 km, 50 km,
// 5 km, 300 m) at three times of day (day: sun 40 deg, golden hour: 6 deg, night: -25 deg). Each view is held until
// clouds, Lumen, TSR and the nature streaming have settled, then shot as shots/ue-<shot>-<view>.png. The log carries,
// per view, the frame / game / render / GPU ms averaged over the 60 frames before the shot ("DmSkyLook: view ...")
// and the sky fill state, so a run with -skyold (today's look: no sky fill, the old haze curve) and one without
// give the before / after pictures. -ab does both in one launch: each view is first settled and measured with the old
// look (shot <view>-old), then switched to the new look, settled again and measured (shot <view>), so the frame-time
// difference is taken under the same machine load ("DmSkyLook: ab <view> old ... new ... delta").
//
// (wave 4) -turn=<rate>: the sky clock runs at <rate> over the last 90 frames of each view (live play turns the planet).
//
//   shot.ps1 sky-lookcheck -noshot   numeric checks of the wave-3 look (sky fill on by day near the ground and off
//                                    from orbit and at night, capture point on the ground, haze curve, star sphere
//                                    hidden by day in the air and shown in space and at night, -skyold restores)
//
// Site: the nature module's forest site (a forested valley among hills near a coast, lat -22.1 lon -145.3 on the
// default fixture); -site=lat,lon picks another place.

#include "Game/Core/DmCoords.h"
#include "Game/Core/DmGameMode.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Game/Sky/DmSkyActor.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/App.h"

namespace
{
	struct FLookView
	{
		const TCHAR* Name;
		double DistKm, Pitch, Fov;
		double SunEl, SunAz;        // sun elevation / compass azimuth at the site, degrees
		int32 Hold;                 // frames before the shot
		bool bStorm = false;        // (wave 4) framed on the world's own storm (else the site), the storm grown at once
	};

	// heading 35 (the nature views' framing); low suns come from the front-left (long shadows toward the camera, glint)
	const FLookView kLookViews[] = {
		{ TEXT("orbit-day"), 700.0, 90.0, 30.0, 40.0, -90.0, 240 },
		{ TEXT("orbit-golden"), 700.0, 90.0, 30.0, 6.0, -70.0, 240 },
		{ TEXT("orbit-night"), 700.0, 90.0, 30.0, -25.0, -90.0, 240 },
		{ TEXT("terminator"), 400.0, 70.0, 40.0, -2.0, -90.0, 240 },
		{ TEXT("50km-day"), 50.0, 45.0, 50.0, 40.0, -90.0, 260 },
		{ TEXT("50km-golden"), 50.0, 45.0, 50.0, 6.0, -60.0, 260 },
		{ TEXT("50km-night"), 50.0, 45.0, 50.0, -25.0, -90.0, 260 },
		{ TEXT("5km-day"), 5.0, 26.0, 55.0, 40.0, -90.0, 320 },
		{ TEXT("5km-golden"), 5.0, 26.0, 55.0, 6.0, -60.0, 320 },
		{ TEXT("5km-night"), 5.0, 26.0, 55.0, -25.0, -90.0, 320 },
		{ TEXT("300m-day"), 1.0, 18.0, 60.0, 40.0, -90.0, 420 },
		{ TEXT("300m-golden"), 1.0, 18.0, 60.0, 6.0, -60.0, 420 },
		{ TEXT("300m-night"), 1.0, 18.0, 60.0, -25.0, -90.0, 420 },
		// in the forest, a few metres up (the hardest case for shadow fill); not part of the default sweep's targets
		{ TEXT("floor-day"), 0.3, 10.0, 60.0, 40.0, -90.0, 360 },
		{ TEXT("floor-golden"), 0.3, 10.0, 60.0, 12.0, -60.0, 360 },
		// (verifier) the sky fill's worst case: the capture point (the site) is in low sun with the sun behind the camera,
		// and the frame runs on across the terminator into the night (one capture lights the whole frame)
		{ TEXT("50km-away"), 60.0, 25.0, 60.0, 10.0, 215.0, 260 },
		{ TEXT("20km-away"), 25.0, 15.0, 60.0, 6.0, 215.0, 300 },
		// (wave 4) the storm: from 250 km at mid morning, at dusk from 400 km (where the eye read as a hard black dot), and close
		{ TEXT("cyclone"), 250.0, 70.0, 45.0, 45.0, -90.0, 240, true },
		{ TEXT("cyclone-dusk"), 400.0, 70.0, 40.0, 4.0, -90.0, 240, true },
		{ TEXT("cyclone-close"), 70.0, 55.0, 55.0, 35.0, -90.0, 260, true },
	};
	constexpr double kHeading = 35.0;

	struct FLookRun
	{
		TArray<const FLookView*> Views;
		int32 Index = -1;
		int32 StartFrame = 0;
		double SiteLat = -22.093, SiteLon = -145.277;
		double ViewLat = 0.0, ViewLon = 0.0;   // where the current view looks (the site, or the storm)
		int32 HoldArg = -1;
		TArray<double> Frame, Game, Render, Gpu;
		double SumGpu = 0.0, SumFrame = 0.0;
		int32 Shots = 0;
		bool bAB = false;
		int32 Phase = 0;               // -ab: 0 old look, 1 new look; -variants: index of the variant
		TArray<TArray<double>> Variants;
		double OldFrame = 0, OldGpu = 0, SumDFrame = 0, SumDGpu = 0;
		double TurnRate = 0.0;        // (wave 4) -turn=: sky clock rate over the last 90 frames of each view
		int32 SwitchFrame = -1;       // (verifier) frame at which the next -ab phase / variant is applied (after its shot is written)
	};
	FLookRun GLook;

	/** Subsolar point that puts the sun at (elevation, compass azimuth) over the site. */
	void SubsolarFor(double Lat, double Lon, double ElDeg, double AzDeg, double& OutLat, double& OutLon)
	{
		const FVector Up = DmCoords::LatLonToDir(Lat, Lon);
		FVector E, N;
		DmCoords::Basis(Up, E, N);
		const double El = FMath::DegreesToRadians(ElDeg), Az = FMath::DegreesToRadians(AzDeg);
		const FVector S = (N * FMath::Cos(Az) + E * FMath::Sin(Az)) * FMath::Cos(El) + Up * FMath::Sin(El);
		DmCoords::DirToLatLon(S.GetSafeNormal(), OutLat, OutLon);
	}

	double Mean(const TArray<double>& V) { double S = 0; for (double X : V) S += X; return V.Num() ? S / V.Num() : 0.0; }

	void StartView(FDmScenarioContext& Ctx)
	{
		FLookRun& R = GLook;
		const FLookView& V = *R.Views[R.Index];
		R.ViewLat = R.SiteLat; R.ViewLon = R.SiteLon;
		if (V.bStorm)
			if (ADmSky* Sky = ADmSky::Get(Ctx.World))
			{
				// the world's own storm first, else the strongest climatological hurricane
				const FDmCyclone* Best = nullptr;
				for (const FDmCyclone& C : Sky->GetStorms().Systems)
					if (C.Type == EDmCycloneType::Tropical && (!Best || (C.SimId >= 0 && Best->SimId < 0) || ((C.SimId >= 0) == (Best->SimId >= 0) && C.Strength > Best->Strength))) Best = &C;
				if (Best) { R.ViewLat = Best->LatDeg; R.ViewLon = Best->LonDeg; }
				Sky->GetStormsMutable().SettleSimStorms();
			}
		Ctx.LookAt(R.ViewLat, R.ViewLon, V.DistKm, kHeading, V.Pitch, V.Fov);
		double SLat, SLon;
		SubsolarFor(R.ViewLat, R.ViewLon, V.SunEl, V.SunAz, SLat, SLon);
		Ctx.SetSubsolar(SLat, SLon);
		if (R.TurnRate > 0.0) if (ADmSky* Sky = ADmSky::Get(Ctx.World)) Sky->SetForcedRate(-1.0);
		R.StartFrame = Ctx.Frame;
		R.Phase = 0;
		if (R.bAB) if (ADmSky* Sky = ADmSky::Get(Ctx.World)) Sky->SetOldLook(true);
		if (R.Variants.Num()) if (ADmSky* Sky = ADmSky::Get(Ctx.World)) { const TArray<double>& Vr = R.Variants[0]; Sky->SetLookParams(Vr[0], Vr[1], Vr[2], Vr[3], Vr[4]); Sky->SetNightParams(Vr[5], Vr[6], Vr[7]); }
		R.Frame.Reset(); R.Game.Reset(); R.Render.Reset(); R.Gpu.Reset();
		Ctx.Log(FString::Printf(TEXT("DmSkyLook: start %s: %.2f km, pitch %.0f, fov %.0f, sun el %.0f az %.0f (subsolar %.1f %.1f)"),
			V.Name, V.DistKm, V.Pitch, V.Fov, V.SunEl, V.SunAz, SLat, SLon));
	}
}

static FDmScenarioRegistrar GSkyLook(TEXT("sky-look"), TEXT("sky"),
	TEXT("look sweep: orbit / terminator / 50 km / 5 km / 300 m at day, golden hour and night over a forest (per-view frame times)"),
	[](FDmScenarioContext& Ctx)
	{
		GLook = FLookRun();
		FLookRun& R = GLook;
		Ctx.BuildScene();
		Ctx.HoldExit();
		double Site[2] = { R.SiteLat, R.SiteLon };
		if (FDmShotArgs::List(TEXT("-site="), Site, 2) == 2) { R.SiteLat = Site[0]; R.SiteLon = Site[1]; }
		FDmShotArgs::Value(TEXT("-hold="), R.HoldArg);
		R.bAB = FDmShotArgs::Param(TEXT("ab"));
		FDmShotArgs::Value(TEXT("-turn="), R.TurnRate);
		FString VarList;
		if (FDmShotArgs::Value(TEXT("-variants="), VarList))
		{
			TArray<FString> Items; VarList.ParseIntoArray(Items, TEXT(","));
			for (const FString& It : Items)
			{
				TArray<FString> F; It.ParseIntoArray(F, TEXT(":"), false);
				TArray<double> V;
				for (int32 K = 0; K < 8; ++K) V.Add(K < F.Num() && F[K] != TEXT("-") && !F[K].IsEmpty() ? FCString::Atod(*F[K]) : -1.0);
				R.Variants.Add(V);
			}
			R.bAB = false;
		}
		FString List;
		TArray<FString> Want;
		if (FDmShotArgs::Value(TEXT("-views="), List)) List.ParseIntoArray(Want, TEXT(","));
		for (const FLookView& V : kLookViews)
			if (Want.Num() == 0 || Want.Contains(V.Name)) R.Views.Add(&V);
		Ctx.Log(FString::Printf(TEXT("DmSkyLook: %d views at site lat %.3f lon %.3f%s"), R.Views.Num(), R.SiteLat, R.SiteLon,
			FDmShotArgs::Param(TEXT("skyold")) ? TEXT(" (-skyold: today's sky)") : TEXT("")));
		if (R.Views.Num() == 0) { Ctx.SetExitCode(1); Ctx.Finish(); return; }
		R.Index = 0;
		StartView(Ctx);
	},
	[](FDmScenarioContext& Ctx, float Dt)
	{
		FLookRun& R = GLook;
		if (R.Index < 0 || R.Index >= R.Views.Num()) return;
		const FLookView& V = *R.Views[R.Index];
		const int32 Hold = R.HoldArg > 0 ? R.HoldArg : V.Hold;
		const int32 Age = Ctx.Frame - R.StartFrame;
		// (verifier) the shot queued at Age == Hold is only captured when a later frame renders: switching the look in the same
		// tick put the NEXT phase's instant settings (exposure, haze, grade, stars) into the previous phase's picture (the
		// -variants pictures were each one variant late, and every -old picture of -ab carried the new haze and grade).
		// Switch 12 frames later instead.
		if (R.SwitchFrame >= 0)
		{
			if (Ctx.Frame < R.SwitchFrame) return;
			R.SwitchFrame = -1;
			ADmSky* Sky = ADmSky::Get(Ctx.World);
			if (R.Variants.Num())
			{
				const TArray<double>& Vr = R.Variants[R.Phase];
				if (Sky) { Sky->SetLookParams(Vr[0], Vr[1], Vr[2], Vr[3], Vr[4]); Sky->SetNightParams(Vr[5], Vr[6], Vr[7]); }
				R.StartFrame = Ctx.Frame - FMath::Max(0, Hold - 150);
			}
			else
			{
				if (Sky) Sky->SetOldLook(false);
				R.StartFrame = Ctx.Frame - FMath::Max(0, Hold - 180);   // 180 more frames, the last 60 measured
			}
			R.Frame.Reset(); R.Game.Reset(); R.Render.Reset(); R.Gpu.Reset();
			return;
		}
		// re-aim every 30 frames: the harness camera keeps its place but the planet frame may be republished
		if (Age > 0 && Age % 30 == 0 && Age < Hold - 60) Ctx.LookAt(R.ViewLat, R.ViewLon, V.DistKm, kHeading, V.Pitch, V.Fov);
		// (wave 4) -turn=<sky seconds per second>: the last 90 frames before the shot run the sky clock (the sun is unpinned), as in
		// live play, so the clouds' temporal reconstruction and the cloud shadows are seen while the planet turns
		if (R.TurnRate > 0.0 && Age == Hold - 90 && Ctx.Sim)
		{
			Ctx.Sim->UnpinSun();
			if (ADmSky* Sky = ADmSky::Get(Ctx.World)) Sky->SetForcedRate(R.TurnRate);
		}
		if (Age >= Hold - 60 && Age < Hold)
		{
			double G = 0, Rn = 0, Rhi = 0, Gpu = 0;
			if (ADmGameMode* M = Ctx.Mode) M->GetLastUnitMs(G, Rn, Rhi, Gpu);
			R.Frame.Add(FApp::GetDeltaTime() * 1000.0); R.Game.Add(G); R.Render.Add(Rn); R.Gpu.Add(Gpu);
		}
		if (Age == Hold && R.Variants.Num())
		{
			Ctx.Shot(FString::Printf(TEXT("%s-v%d"), V.Name, R.Phase));
			ADmSky* Sky = ADmSky::Get(Ctx.World);
			Ctx.Log(FString::Printf(TEXT("DmSkyLook: view %s v%d gpu %.2f ms | %s"), V.Name, R.Phase, Mean(R.Gpu), Sky ? *Sky->DescribeLook() : TEXT("")));
			if (++R.Phase < R.Variants.Num()) { R.SwitchFrame = Ctx.Frame + 12; return; }
			++R.Shots;
		}
		else if (Age == Hold)
		{
			const bool bOldPhase = R.bAB && R.Phase == 0;
			Ctx.Shot(bOldPhase ? FString(V.Name) + TEXT("-old") : FString(V.Name));
			// -lookprofile: a GPU profile of this frame in the log, old and new (the sky light capture passes)
			if (FDmShotArgs::Param(TEXT("lookprofile")) && GEngine) { Ctx.Log(FString::Printf(TEXT("DmSkyLook: profile %s%s"), V.Name, bOldPhase ? TEXT(" old") : TEXT(""))); GEngine->Exec(Ctx.World, TEXT("ProfileGPU")); }
			FString Fill = TEXT("none");
			if (ADmSky* Sky = ADmSky::Get(Ctx.World)) Fill = Sky->DescribeLook();
			Ctx.Log(FString::Printf(TEXT("DmSkyLook: view %s%s frame %.2f game %.2f render %.2f gpu %.2f ms | %s"),
				V.Name, bOldPhase ? TEXT(" (old)") : TEXT(""), Mean(R.Frame), Mean(R.Game), Mean(R.Render), Mean(R.Gpu), *Fill));
			if (bOldPhase)
			{
				// switch to the new look and settle again (Lumen picks the fill up over a few dozen frames)
				R.OldFrame = Mean(R.Frame); R.OldGpu = Mean(R.Gpu);
				R.Phase = 1;
				R.SwitchFrame = Ctx.Frame + 12;
				return;
			}
			++R.Shots;
			R.SumGpu += Mean(R.Gpu); R.SumFrame += Mean(R.Frame);
			if (R.bAB)
			{
				R.SumDFrame += Mean(R.Frame) - R.OldFrame; R.SumDGpu += Mean(R.Gpu) - R.OldGpu;
				Ctx.Log(FString::Printf(TEXT("DmSkyLook: ab %s old frame %.2f gpu %.2f | new frame %.2f gpu %.2f | delta frame %+.2f gpu %+.2f ms"),
					V.Name, R.OldFrame, R.OldGpu, Mean(R.Frame), Mean(R.Gpu), Mean(R.Frame) - R.OldFrame, Mean(R.Gpu) - R.OldGpu));
			}
		}
		if (Age >= Hold + 20)
		{
			++R.Index;
			if (R.Index >= R.Views.Num())
			{
				Ctx.Log(FString::Printf(TEXT("DmSkyLook: done, %d views, mean frame %.2f ms, mean gpu %.2f ms%s"), R.Shots,
					R.SumFrame / FMath::Max(1, R.Shots), R.SumGpu / FMath::Max(1, R.Shots),
					R.bAB ? *FString::Printf(TEXT(", mean delta (new - old) frame %+.2f gpu %+.2f ms"), R.SumDFrame / FMath::Max(1, R.Shots), R.SumDGpu / FMath::Max(1, R.Shots)) : TEXT("")));
				Ctx.Finish();
				return;
			}
			StartView(Ctx);
		}
	});

// ------------------------------------------------------------------------------------------------ checks

static FDmScenarioRegistrar GSkyLookCheck(TEXT("sky-lookcheck"), TEXT("sky"),
	TEXT("numeric checks of the wave-3 sky look: sky fill by day / night / altitude, haze curve, stars, old-look switch"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		Ctx.HoldExit();
	},
	[](FDmScenarioContext& Ctx, float)
	{
		static int32 Fails = 0;
		const double Lat = -22.093, Lon = -145.277;
		ADmSky* Sky = ADmSky::Get(Ctx.World);
		if (!Sky) { Ctx.Log(TEXT("DmCheck FAIL no sky")); Ctx.SetExitCode(1); Ctx.Finish(); return; }
		auto Check = [&Ctx](bool bOk, const FString& What) { Ctx.Log(FString::Printf(TEXT("DmCheck %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What)); if (!bOk) ++Fails; };
		auto View = [&Ctx, Lat, Lon](double Dist, double Pitch, double SunEl)
		{
			Ctx.LookAt(Lat, Lon, Dist, kHeading, Pitch, 55.0);
			double SLat, SLon; SubsolarFor(Lat, Lon, SunEl, -90.0, SLat, SLon); Ctx.SetSubsolar(SLat, SLon);
		};
		const ADmSky::FLookState L = Sky->GetLookState();
		const FString St = FString::Printf(TEXT("(fill %s %.2f, capture %.2f km, sun sin %.2f, ap %.2f, veil %.2f, stars %d, lift %.2f EV)"), L.bFill ? TEXT("on") : TEXT("off"), L.Fill, L.FillAltKm, L.FillSunSin, L.Ap, L.Veil, L.bStars ? 1 : 0, L.Lift);
		switch (Ctx.Frame)
		{
		case 5: Fails = 0; View(5.0, 26.0, 40.0); break;
		case 60:
			Check(L.bFill && FMath::IsNearlyEqual(double(L.Fill), L.FillGain, 0.05), TEXT("5 km by day: sky fill on at full gain ") + St);
			Check(L.FillAltKm > 0.0 && L.FillAltKm < 3.0, TEXT("5 km by day: capture point on the ground under the view, not in the air ") + St);
			Check(FMath::IsNearlyEqual(double(L.Ap), L.ApNear, 0.02) && FMath::IsNearlyEqual(double(L.Veil), L.VeilNear, 0.02), TEXT("5 km: near-ground haze (ap / veil) ") + St);
			Check(!L.bStars, TEXT("5 km by day: star sphere hidden (the sky is the atmosphere's) ") + St);
			Check(FMath::Abs(L.Lift) < 0.05, TEXT("5 km by day (sun 40 deg): no exposure lift ") + St);
			View(700.0, 90.0, 40.0); break;
		case 120:
			Check(!L.bFill, TEXT("orbit 700 km: no sky fill (it would light the night side) ") + St);
			Check(FMath::IsNearlyEqual(double(L.Veil), L.VeilOrbit, 0.02) && FMath::IsNearlyEqual(double(L.Ap), L.ApOrbit, 0.02), TEXT("orbit: orbit haze (ap / veil) ") + St);
			Check(L.bStars, TEXT("orbit: stars shown ") + St);
			Check(FMath::Abs(L.Lift) < 0.05, TEXT("orbit by day: no exposure lift ") + St);
			View(50.0, 45.0, 40.0); break;
		case 180:
			Check(L.bFill && L.Fill > 0.5f * float(L.FillGain), TEXT("50 km by day: sky fill on (fading with altitude) ") + St);
			View(5.0, 26.0, -25.0); break;
		case 240:
			Check(!L.bFill, TEXT("5 km at night: no sky fill (keeps the moonlit sea and ground) ") + St);
			Check(L.bStars, TEXT("5 km at night: stars shown ") + St);
			if (L.bNightV4)
			{
				// (wave 4) near the ground the moonlight carries the night, the exposure lift is small (emissives not x6)
				const FString Ms = FString::Printf(TEXT(" (moon %.2f lux at %.0f deg)"), L.MoonLux, L.MoonEl);
				Check(FMath::IsNearlyEqual(L.Lift, FMath::Max(L.NightEvNear, L.GoldenEv), 0.1), TEXT("5 km at night: the near-ground night lift (1 EV), not the 2.6 EV from orbit ") + St);
				Check(L.MoonLux >= 0.74 * L.MoonNearLux && L.MoonLux <= 1.01 * L.MoonNearLux, TEXT("5 km at night: moonlight at the near-ground key, any moon phase") + Ms);
				Check(L.MoonEl >= L.MoonMinEl - 1.0, TEXT("5 km at night: the moon stands high over the ground in view (no black, shadowed land)") + Ms);
			}
			else Check(FMath::IsNearlyEqual(L.Lift, 2.6, 0.1), TEXT("5 km at night: the night lift (2.6 EV), not raised by the low-sun lift ") + St);
			View(700.0, 90.0, -25.0); break;
		case 270:
			// (wave 4) from orbit the night side keeps the wave-3 look: full night lift, moonlight from the moon's own side
			Check(!L.bNightV4 || L.Lift > 1.8, TEXT("orbit at night: the orbit night lift is kept ") + St);
			View(5.0, 26.0, 6.0); break;
		case 300:
			Check(L.bFill && L.FillSunSin > 0.05, TEXT("5 km golden hour: sky fill on ") + St);
			Check(L.Lift > 0.6 * L.GoldenEv && L.Lift <= L.GoldenEv + 0.01, TEXT("5 km golden hour: low-sun exposure lift (shade readable) ") + St);
			// (verifier) the sky-look 50km-golden framing: its right side runs on across the terminator into the dusk
			Ctx.LookAt(Lat, Lon, 50.0, kHeading, 45.0, 50.0);
			{ double SLat, SLon; SubsolarFor(Lat, Lon, 6.0, -60.0, SLat, SLon); Ctx.SetSubsolar(SLat, SLon); }
			break;
		case 360:
			Check(L.FrameMinSunSin < -0.12 && !L.bFill, TEXT("50 km golden hour with the dusk in frame: no sky fill (it lit the dusk sea pale) ") + St + FString::Printf(TEXT(" frame min sun sin %.2f"), L.FrameMinSunSin));
			Sky->SetOldLook(true); break;
		case 390:
			Check(!L.bFill && FMath::IsNearlyEqual(double(L.Ap), 0.6, 0.02) && FMath::IsNearlyEqual(double(L.Veil), 1.0, 0.01), TEXT("-skyold look restores no fill, ap 0.6, veil 1 ") + St);
			Sky->SetOldLook(false);
			Ctx.Log(FString::Printf(TEXT("DmCheck RESULT %s (%d failed)"), Fails ? TEXT("FAIL") : TEXT("PASS"), Fails));
			Ctx.SetExitCode(Fails ? 1 : 0);
			Ctx.Finish();
			break;
		default: break;
		}
	});

// ------------------------------------------------------------------------------------------------ descent (verifier)
//
//   shot.ps1 sky-descent [-sunel=8] [-skyold]   one continuous descent from 400 km to 1 km over the forest site in
//   600 frames (the sky fill fades in from 180 to 30 km, haze and exposure change continuously): per-frame GPU / frame
//   ms (mean, max, frames over 2x the mean), the fill's render-state churn (intensity sets, show / hide, atmosphere
//   refreshes) and a shot every 60 frames (ue-<shot>-d<i>.png) for a flicker / step check.

namespace
{
	struct FDescent { TArray<double> Gpu, Frame; int32 Start = 0; ADmSky::FLookState L0; };
	FDescent GDesc;
}

static FDmScenarioRegistrar GSkyDescent(TEXT("sky-descent"), TEXT("sky"),
	TEXT("verifier: a continuous 400 km -> 1 km descent at golden hour (fill fade, frame-time spikes, render-state churn)"),
	[](FDmScenarioContext& Ctx)
	{
		GDesc = FDescent();
		Ctx.BuildScene();
		Ctx.HoldExit();
	},
	[](FDmScenarioContext& Ctx, float)
	{
		const double Lat = -22.093, Lon = -145.277;
		double SunEl = 8.0;
		FDmShotArgs::Value(TEXT("-sunel="), SunEl);
		constexpr int32 Settle = 150, Steps = 600;
		const int32 F = Ctx.Frame - 5;
		if (F < 0) return;
		const double T = FMath::Clamp(double(F - Settle) / Steps, 0.0, 1.0);
		const double Dist = 400.0 * FMath::Pow(1.0 / 400.0, T);
		const double Pitch = FMath::Lerp(90.0, 20.0, FMath::SmoothStep(0.0, 1.0, T));
		Ctx.LookAt(Lat, Lon, Dist, kHeading, Pitch, 55.0);
		if (F == 0) { double SLat, SLon; SubsolarFor(Lat, Lon, SunEl, -60.0, SLat, SLon); Ctx.SetSubsolar(SLat, SLon); }
		ADmSky* Sky = ADmSky::Get(Ctx.World);
		if (F == Settle && Sky) GDesc.L0 = Sky->GetLookState();
		if (F > Settle && F <= Settle + Steps)
		{
			double G = 0, Rn = 0, Rhi = 0, Gpu = 0;
			if (ADmGameMode* M = Ctx.Mode) M->GetLastUnitMs(G, Rn, Rhi, Gpu);
			GDesc.Gpu.Add(Gpu); GDesc.Frame.Add(FApp::GetDeltaTime() * 1000.0);
			if ((F - Settle) % 60 == 0)
			{
				Ctx.Shot(FString::Printf(TEXT("d%02d"), (F - Settle) / 60));
				if (Sky) Ctx.Log(FString::Printf(TEXT("DmSkyDescent: step %d dist %.1f km | %s"), (F - Settle) / 60, Dist, *Sky->DescribeLook()));
			}
		}
		if (F == Settle + Steps + 30)
		{
			auto Stat = [](const TArray<double>& V, double& Mean, double& Max, int32& Spikes)
			{
				Mean = 0; Max = 0; Spikes = 0;
				for (double X : V) { Mean += X; Max = FMath::Max(Max, X); }
				Mean /= FMath::Max(1, V.Num());
				for (double X : V) if (X > 2.0 * Mean) ++Spikes;
			};
			double GM, GX, FM, FX; int32 GS, FS;
			Stat(GDesc.Gpu, GM, GX, GS); Stat(GDesc.Frame, FM, FX, FS);
			const ADmSky::FLookState L = Sky ? Sky->GetLookState() : ADmSky::FLookState();
			Ctx.Log(FString::Printf(TEXT("DmSkyDescent: done%s: gpu mean %.2f max %.2f (%d frames > 2x mean) | frame mean %.2f max %.2f (%d > 2x) | fill sets %d, show/hide %d, atmosphere refreshes %d over %d frames"),
				FDmShotArgs::Param(TEXT("skyold")) ? TEXT(" (old look)") : TEXT(""), GM, GX, GS, FM, FX, FS,
				L.FillSets - GDesc.L0.FillSets, L.FillToggles - GDesc.L0.FillToggles, L.AtmoDirty - GDesc.L0.AtmoDirty, Steps));
			Ctx.Finish();
		}
	});
