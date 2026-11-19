// DEMIURGE — Game/Camera/DmCameraScenarios.cpp : camera scenarios (C3 camera-input).
//
//   cam-descent   orbit -> just above a valley as a settled screenshot series (one shot per zoom stop),
//                 the pawn's own camera: shots/ue-cam-descent-z000 ... -z100.png and a log line per stop
//                 (altitude, tilt, eye -> target distance, clearance, frame time).
//                 -focus=lat,lon (default the lab's forested valley -24.16,-113.67) | valley | peak -heading=deg
//                 -zooms=0,0.3,... -settle=frames (between stops, default 150)
//   cam-fly       the same descent as one continuous 16 s zoom (streaming, no settling), shots on the way
//                 and frame-time statistics (worst frames) for the whole flight
//   cam-orbit     zoom 0 over the settled region: the whole planet and the atmosphere shell in frame (also
//                 run with -res=390x844 for the portrait phone fit)
//   cam-pole      the camera dragged straight across the north pole at mid zoom: heading by parallel
//                 transport (no flip), a shot at the pole
//   cam-math      the rig without rendering (-noshot): framing at zoom 0 (landscape and portrait), the
//                 altitude / tilt curves, never below the ground over 4000 random views on the real
//                 world, grab-drag accuracy, poles (no NaN, no heading flip), fly-to, inertia, shake
//                 (DmCamTest lines, RESULT PASS / FAIL, exit code 0 / 1)

#include "Game/Camera/DmPawn.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmGameMode.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmSimSubsystem.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"

#include "Sim/State.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmCamScn, Log, All);

namespace
{
	/** lat / lon of the population-weighted centre of the settlements, or (20, 30) (like the bridge's orbit). */
	void SettledCentre(const dm::World* W, double& OutLat, double& OutLon)
	{
		OutLat = 20.0; OutLon = 30.0;
		if (!W || W->settlements.empty()) return;
		FVector Sum = FVector::ZeroVector;
		for (const dm::Settlement& S : W->settlements)
			if (!S.dead) Sum += DmCoords::CellDir(*W, S.cell) * FMath::Sqrt(FMath::Max(S.pop, 1.0));
		if (Sum.IsNearlyZero()) return;
		DmCoords::DirToLatLon(Sum.GetSafeNormal(), OutLat, OutLon);
		OutLat = FMath::Clamp(OutLat, -55.0, 55.0);
	}

	ADmPawn* SpawnPawn(FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene(DmPartsWorld | uint32(EDmPartKind::Control));
		Ctx.UseHarnessCamera(false);
		ADmPawn* P = ADmPawn::Find(Ctx.World);
		if (!P) Ctx.Log(TEXT("no camera pawn (scene part \"camera\" missing?)"));
		else P->bAdoptHarnessView = false;
		return P;
	}

	FString ViewLine(const ADmPawn* P)
	{
		const FDmCameraView& V = P->Rig().View();
		const double Clear = V.Eye.Size() - P->GroundKm(V.Eye.GetSafeNormal());
		double Lat, Lon;
		DmCoords::DirToLatLon(V.FocusDir, Lat, Lon);
		// sky band: how far the horizon (of the ground sphere under the eye) sits below the top of the frame
		const double Ge = P->GroundKm(V.Eye.GetSafeNormal());
		const double Dip = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Ge / V.Eye.Size(), -1.0, 1.0)));
		const double TopBelowHorizontal = 90.0 - FMath::RadiansToDegrees(V.TiltRad) - FMath::RadiansToDegrees(V.VFov) * 0.5;
		return FString::Printf(TEXT("zoom %.3f altitude %.3f km clearance %.3f km tilt %.1f deg eye->target %.2f km heading %.1f deg hfov %.1f focus %.2f,%.2f eye-r %.3f km; horizon %.1f deg below the frame top (%s)"),
			V.Zoom01, V.AltitudeKm, Clear, FMath::RadiansToDegrees(V.TiltRad), V.TargetDistKm, FMath::RadiansToDegrees(P->Rig().HeadingRad()), V.HFovDeg, Lat, Lon, V.Eye.Size(),
			Dip - TopBelowHorizontal, Dip > TopBelowHorizontal ? TEXT("sky in frame") : TEXT("ground only"));
	}

	struct FDescent
	{
		TArray<double> Zooms;
		int32 Settle = 150;
		int32 First = 240;
		int32 Stage = -1;
		double Lat = -24.16, Lon = -113.67, Heading = 20.0;
	};
	FDescent GDescent;

	/** The deepest land valley of the world: a cell with ridges on both sides (the higher of the two
	 *  perpendicular azimuth bins' maxima within ~5 km) and an open axis; heading looks down the valley
	 *  (toward the lower end of the axis). Returns the score (side ridge height above the floor, sim units). */
	double FindValley(const dm::World& W, double& OutLat, double& OutLon, double& OutHeadingDeg)
	{
		if (!W.geo) return 0.0;
		const dm::Geo& Gq = *W.geo;
		const double A = FMath::Max(5.0 * Gq.step, 0.04);
		std::vector<int32_t> Near;
		double Best = 0.0;
		int32 BestCell = -1, BestAxis = 0;
		bool bBestFwd = true;
		const int32 Stride = FMath::Max(1, W.n / 40000);
		for (int32 C = 0; C < W.n; C += Stride)
		{
			const double E0 = W.elev[size_t(C)];
			if (E0 < W.seaLevel + 0.6 || FMath::Abs(Gq.lat[size_t(C)]) > 0.7) continue;   // land, |lat| < 40 deg
			if (W.water[size_t(C)] > 0.03) continue;                                       // not a lake
			const uint8 Bi = W.biome[size_t(C)];
			if (Bi != dm::Biome::SAVANNA && Bi != dm::Biome::GRASSLAND && Bi != dm::Biome::FOREST && Bi != dm::Biome::RAINFOREST && Bi != dm::Biome::TAIGA) continue;
			Gq.within(C, A, Near);
			int32 Lakes = 0;
			for (const int32 M : Near) if (W.water[size_t(M)] > 0.03 || W.elev[size_t(M)] < W.seaLevel) ++Lakes;
			if (Lakes * 10 > int32(Near.size())) continue;                               // a dry valley, not a coast or a lake district
			const FVector Up = DmCoords::CellDir(W, C);
			FVector Ea, Na;
			DmCoords::Basis(Up, Ea, Na);
			double H[8];
			for (double& X : H) X = -1e9;
			for (const int32 M : Near)
			{
				if (M == C) continue;
				const FVector D = DmCoords::CellDir(W, M) - Up;
				if (D.SizeSquared() < FMath::Square(0.5 * A)) continue;   // the annulus only
				const double Az = FMath::Atan2(FVector::DotProduct(D, Ea), FVector::DotProduct(D, Na));
				const int32 Bin = (int32(FMath::FloorToInt((Az + UE_PI) / (UE_TWO_PI / 8.0) + 0.5)) % 8 + 8) % 8;
				H[Bin] = FMath::Max(H[Bin], double(W.elev[size_t(M)]));
			}
			for (int32 Ax = 0; Ax < 4; ++Ax)
			{
				const double Side = FMath::Min(H[(Ax + 2) % 8], H[(Ax + 6) % 8]);
				const double Along = FMath::Max(H[Ax], H[Ax + 4]);
				const double S = (Side - E0) - 0.5 * FMath::Max(Along - E0, 0.0);
				if (S > Best && Side > W.seaLevel + 2.0)
				{
					Best = S; BestCell = C; BestAxis = Ax; bBestFwd = H[Ax] < H[Ax + 4];
				}
			}
		}
		if (BestCell < 0) return 0.0;
		DmCoords::DirToLatLon(DmCoords::CellDir(W, BestCell), OutLat, OutLon);
		// bin b covers azimuth -pi + b * 45 deg
		const double AzDeg = -180.0 + 45.0 * BestAxis + (bBestFwd ? 0.0 : 180.0);
		OutHeadingDeg = FMath::Fmod(AzDeg + 360.0, 360.0);
		return Best;
	}

	void ReadFocus(FDmScenarioContext& Ctx, double& Lat, double& Lon, double& Heading)
	{
		FString F;
		if (FDmShotArgs::Value(TEXT("-focus="), F) && F.Equals(TEXT("valley"), ESearchCase::IgnoreCase) && Ctx.Sim && Ctx.Sim->Sim())
		{
			double La = Lat, Lo = Lon, Hd = Heading;
			const double S = FindValley(*Ctx.Sim->Sim(), La, Lo, Hd);
			if (S > 0.0)
			{
				Lat = La; Lon = Lo; Heading = Hd;
				Ctx.Log(FString::Printf(TEXT("valley finder: %.2f,%.2f heading %.0f deg, ridges %.2f elev units (%.2f km) above the floor"), Lat, Lon, Heading, S, S * 0.2));
			}
			FDmShotArgs::Value(TEXT("-heading="), Heading);
			return;
		}
		if (FDmShotArgs::Value(TEXT("-focus="), F) && F.Equals(TEXT("peak"), ESearchCase::IgnoreCase) && Ctx.Sim && Ctx.Sim->Sim())
		{
			// the highest cell of the world (close-to-the-ground worst case: steep relief around the eye)
			const dm::World& Wp = *Ctx.Sim->Sim();
			int32 Peak = 0;
			for (int32 C = 0; C < Wp.n; ++C) if (Wp.elev[size_t(C)] > Wp.elev[size_t(Peak)]) Peak = C;
			DmCoords::DirToLatLon(DmCoords::CellDir(Wp, Peak), Lat, Lon);
			Ctx.Log(FString::Printf(TEXT("focus: the highest peak %.2f,%.2f (elev %.2f = %.2f km)"), Lat, Lon, Wp.elev[size_t(Peak)], Wp.elev[size_t(Peak)] * 0.2));
			FDmShotArgs::Value(TEXT("-heading="), Heading);
			return;
		}
		double LL[2] = { Lat, Lon };
		if (FDmShotArgs::List(TEXT("-focus="), LL, 2) >= 2) { Lat = LL[0]; Lon = LL[1]; }
		FDmShotArgs::Value(TEXT("-heading="), Heading);
	}

	void PinSun(FDmScenarioContext& Ctx, double Lat, double Lon)
	{
		if (Ctx.Args().bSun) return;
		// an afternoon sun from the west (sim longitude grows westward), kept north of the equator: the
		// placeholder sky lights nothing when the sun is below the north pole's horizon (surface lab pitfall 1)
		Ctx.SetSubsolar(FMath::Clamp(Lat * 0.4 + 18.0, 6.0, 23.0), Lon + 52.0);
	}
}

// ------------------------------------------------------------------ cam-descent

static FDmScenarioRegistrar GDmCamDescent(TEXT("cam-descent"), TEXT("camera"),
	TEXT("orbit -> just above a valley, one settled shot per zoom stop (-focus=lat,lon -heading= -zooms= -settle=)"),
	[](FDmScenarioContext& Ctx)
	{
		GDescent = FDescent();
		ReadFocus(Ctx, GDescent.Lat, GDescent.Lon, GDescent.Heading);
		double Z[16];
		const int32 N = FDmShotArgs::List(TEXT("-zooms="), Z, 16);
		if (N > 0) for (int32 I = 0; I < N; ++I) GDescent.Zooms.Add(FMath::Clamp(Z[I], 0.0, 1.0));
		else GDescent.Zooms = { 0.0, 0.3, 0.5, 0.65, 0.8, 0.9, 1.0 };
		FDmShotArgs::Value(TEXT("-settle="), GDescent.Settle);
		GDescent.First = Ctx.Args().Warm;
		ADmPawn* P = SpawnPawn(Ctx);
		if (!P) return;
		P->JumpLatLon(GDescent.Lat, GDescent.Lon, GDescent.Zooms[0], GDescent.Heading);
		PinSun(Ctx, GDescent.Lat, GDescent.Lon);
		Ctx.HoldExit();
		const dm::World* W = Ctx.Sim->Sim();
		if (W && W->geo)
		{
			const FVector D = DmCoords::LatLonToDir(GDescent.Lat, GDescent.Lon);
			const dm::Vec3 S = DmCoords::UEToSim(D);
			const int32 C = W->geo->cellAt(S.x, S.y, S.z);
			Ctx.Log(FString::Printf(TEXT("descent focus %.2f,%.2f cell %d elev %.2f sea %.2f biome %d; %d stops"), GDescent.Lat, GDescent.Lon, C,
				W->elev[size_t(C)], W->seaLevel, int32(W->biome[size_t(C)]), GDescent.Zooms.Num()));
		}
	},
	[](FDmScenarioContext& Ctx, float Dt)
	{
		ADmPawn* P = ADmPawn::Find(Ctx.World);
		if (!P) { Ctx.SetExitCode(1); Ctx.Finish(); return; }
		const int32 NS = GDescent.Zooms.Num();
		for (int32 K = 0; K < NS; ++K)
		{
			const int32 ShotFrame = GDescent.First + K * GDescent.Settle;
			if (K > 0 && Ctx.Frame == ShotFrame - GDescent.Settle + 1)
			{
				// next stop: same focus and heading, new zoom
				P->Jump(P->Rig().GetFocus(), GDescent.Zooms[K], P->Rig().HeadingRad());
			}
			if (Ctx.Frame == ShotFrame)
			{
				const FString Suffix = FString::Printf(TEXT("z%03d"), FMath::RoundToInt(GDescent.Zooms[K] * 100.0));
				Ctx.Log(FString::Printf(TEXT("stop %s: %s | frame %.1f ms"), *Suffix, *ViewLine(P), FApp::GetDeltaTime() * 1000.0));
				Ctx.Shot(Suffix);
			}
		}
		if (Ctx.Frame == GDescent.First + (NS - 1) * GDescent.Settle + 2)
		{
			Ctx.Log(FString::Printf(TEXT("descent done: min altitude seen %.3f km"), P->MinAltitudeSeenKm));
			Ctx.Finish();
		}
	});

// ------------------------------------------------------------------ cam-fly

namespace
{
	struct FFly
	{
		double T0 = -1, Seconds = 16.0;
		int32 Start = 240;
		TArray<float> Frames;
		int32 NextShot = 0;
		double Lat = -24.16, Lon = -113.67, Heading = 20.0;
		bool bDone = false;
	};
	FFly GFly;
}

static FDmScenarioRegistrar GDmCamFly(TEXT("cam-fly"), TEXT("camera"),
	TEXT("continuous 16 s descent orbit -> valley (wheel-like zoom smoothing), shots on the way and frame times"),
	[](FDmScenarioContext& Ctx)
	{
		GFly = FFly();
		ReadFocus(Ctx, GFly.Lat, GFly.Lon, GFly.Heading);
		FDmShotArgs::Value(TEXT("-seconds="), GFly.Seconds);
		GFly.Start = Ctx.Args().Warm;
		ADmPawn* P = SpawnPawn(Ctx);
		if (!P) return;
		P->JumpLatLon(GFly.Lat, GFly.Lon, 0.0, GFly.Heading);
		PinSun(Ctx, GFly.Lat, GFly.Lon);
		Ctx.HoldExit();
	},
	[](FDmScenarioContext& Ctx, float Dt)
	{
		ADmPawn* P = ADmPawn::Find(Ctx.World);
		if (!P || GFly.bDone) return;
		if (Ctx.Frame < GFly.Start) return;
		if (GFly.T0 < 0) GFly.T0 = Ctx.Time;
		const double T = (Ctx.Time - GFly.T0) / GFly.Seconds;
		GFly.Frames.Add(Dt * 1000.f);
		// the player's way down: the zoom target moves like a held zoom key, the rig smooths it
		const double Want = FMath::Clamp(T, 0.0, 1.0);
		P->Rig().ZoomBy(Want - P->Rig().GetTargetZoom());
		const double Marks[5] = { 0.2, 0.4, 0.6, 0.8, 1.02 };
		if (GFly.NextShot < 5 && T >= Marks[GFly.NextShot])
		{
			const FString Suffix = FString::Printf(TEXT("t%d"), GFly.NextShot);
			Ctx.Log(FString::Printf(TEXT("fly %s at %.1f s: %s"), *Suffix, Ctx.Time - GFly.T0, *ViewLine(P)));
			Ctx.Shot(Suffix);
			++GFly.NextShot;
		}
		if (T >= 1.1)
		{
			GFly.bDone = true;
			TArray<float> S = GFly.Frames;
			S.Sort();
			double Sum = 0; for (float X : S) Sum += X;
			const int32 Over33 = S.FilterByPredicate([](float X) { return X > 33.f; }).Num();
			Ctx.Log(FString::Printf(TEXT("fly done: %d frames, avg %.1f ms, p95 %.1f ms, worst %.1f ms, %d over 33 ms (shot frames included); min altitude %.3f km"),
				S.Num(), S.Num() ? Sum / S.Num() : 0.0, S.Num() ? S[FMath::Min(S.Num() - 1, int32(S.Num() * 0.95))] : 0.f, S.Num() ? S.Last() : 0.f, Over33, P->MinAltitudeSeenKm));
			Ctx.Finish();
		}
	});

// ------------------------------------------------------------------ cam-orbit

static FDmScenarioRegistrar GDmCamOrbit(TEXT("cam-orbit"), TEXT("camera"),
	TEXT("zoom 0 over the settled region: planet and atmosphere in frame (try -res=390x844)"),
	[](FDmScenarioContext& Ctx)
	{
		ADmPawn* P = SpawnPawn(Ctx);
		if (!P) return;
		double Lat, Lon;
		SettledCentre(Ctx.Sim->Sim(), Lat, Lon);
		double Hd = 0.0;
		ReadFocus(Ctx, Lat, Lon, Hd);
		P->JumpLatLon(Lat, Lon, 0.0, Hd);
		if (!Ctx.Args().bSun) Ctx.SetSubsolar(FMath::Clamp(Lat * 0.5 + 8.0, -23.0, 23.0), Lon + 55.0);
	},
	[](FDmScenarioContext& Ctx, float)
	{
		if (Ctx.Frame != Ctx.Args().Warm - 1) return;
		if (ADmPawn* P = ADmPawn::Find(Ctx.World))
		{
			const FDmCameraView& V = P->Rig().View();
			const double D = V.Eye.Size();
			const double HalfV = V.VFov * 0.5;
			const double HalfH = FMath::Atan(FMath::Tan(HalfV) * V.Aspect);
			const double Shell = FMath::Asin(FMath::Min(1.0, (P->Rig().Tune.RadiusKm + P->Rig().Tune.ShellKm) / D));
			Ctx.Log(FString::Printf(TEXT("orbit fit: viewport %.0fx%.0f eye %.1f km; shell half-angle %.2f deg vs half fov %.2f (v) %.2f (h): %s"),
				P->Rig().ViewportW(), P->Rig().ViewportH(), D, FMath::RadiansToDegrees(Shell), FMath::RadiansToDegrees(HalfV), FMath::RadiansToDegrees(HalfH),
				Shell < FMath::Min(HalfV, HalfH) ? TEXT("fits") : TEXT("DOES NOT FIT")));
		}
	});

// ------------------------------------------------------------------ cam-pole

static FDmScenarioRegistrar GDmCamPole(TEXT("cam-pole"), TEXT("camera"),
	TEXT("drag across the north pole at mid zoom: parallel-transported heading, no flip"),
	[](FDmScenarioContext& Ctx)
	{
		ADmPawn* P = SpawnPawn(Ctx);
		if (!P) return;
		P->JumpLatLon(70.0, 30.0, 0.45, 0.0);    // heading north, toward the pole
		if (!Ctx.Args().bSun) Ctx.SetSubsolar(20.0, 30.0);
		Ctx.HoldExit();
	},
	[](FDmScenarioContext& Ctx, float Dt)
	{
		ADmPawn* P = ADmPawn::Find(Ctx.World);
		if (!P) return;
		static double LastH = 0, MaxJump = 0;
		static int32 Shots = 0;
		const int32 W0 = Ctx.Args().Warm;
		if (Ctx.Frame > 10 && Ctx.Frame < W0 + 300)
		{
			// keep moving "forward" (north, over the pole and down the other side)
			P->Rig().Pan(0.0, 0.01);
			const double H = P->Rig().HeadingRad();
			double Lat, Lon;
			DmCoords::DirToLatLon(P->Rig().GetFocus(), Lat, Lon);
			// heading relative to the direction of travel must stay continuous: measure the screen-space jump
			const double J = FMath::Abs(FMath::UnwindRadians(H - LastH));
			if (Ctx.Frame > 12 && Lat < 89.0) MaxJump = FMath::Max(MaxJump, J);   // the compass heading itself flips AT the pole, by definition
			LastH = H;
			if (!P->Rig().IsFinite()) Ctx.Log(TEXT("NaN in the view"));
		}
		if (Ctx.Frame == W0 + 60 && Shots == 0)
		{
			++Shots;
			Ctx.Log(FString::Printf(TEXT("pole shot: %s"), *ViewLine(P)));
			Ctx.Shot(TEXT("over"));
		}
		if (Ctx.Frame == W0 + 300)
		{
			double Lat, Lon;
			DmCoords::DirToLatLon(P->Rig().GetFocus(), Lat, Lon);
			Ctx.Log(FString::Printf(TEXT("pole pass done: focus %.2f,%.2f; largest per-frame compass change away from the pole %.4f rad; finite %d"), Lat, Lon, MaxJump, P->Rig().IsFinite() ? 1 : 0));
			Ctx.Shot(TEXT("after"));
			Ctx.Finish();
		}
	});

// ------------------------------------------------------------------ cam-math

namespace
{
	struct FCamTest
	{
		int32 Pass = 0, Fail = 0;
		void Check(bool bOk, const FString& Name, const FString& Detail)
		{
			(bOk ? Pass : Fail)++;
			UE_LOG(LogDmCamScn, Display, TEXT("DmCamTest %s %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *Name, *Detail);
		}
	};

	void RunCamMath(FDmScenarioContext& Ctx)
	{
		FCamTest T;
		const UDmSimSubsystem* Sim = Ctx.Sim;
		const dm::World* W = Sim ? Sim->Sim() : nullptr;
		int32 Hint = -1;
		auto Ground = [Sim, W, &Hint](const FVector& D) -> double
		{
			return W ? Sim->SurfaceRadiusUU(D, Hint) / DmCoords::UUPerKm : 100.0;
		};
		const double T0 = FPlatformTime::Seconds();

		// 1. framing at zoom 0
		for (int32 K = 0; K < 2; ++K)
		{
			FDmCameraRig R;
			const bool bPortrait = K == 1;
			R.SetViewport(bPortrait ? 390 : 1920, bPortrait ? 844 : 1080);
			R.Jump(DmCoords::LatLonToDir(10, 20), 0.0, 0.0);
			R.Update(0.016, Ground);
			const FDmCameraView& V = R.View();
			const double D = V.Eye.Size();
			const double Shell = FMath::Asin((R.Tune.RadiusKm + R.Tune.ShellKm) / D);
			const double HalfV = V.VFov * 0.5, HalfH = FMath::Atan(FMath::Tan(HalfV) * V.Aspect);
			const double Expect = bPortrait ? 880.0 : 420.0;
			T.Check(FMath::Abs(D - Expect) < 0.06 * Expect, bPortrait ? TEXT("orbit-distance-portrait") : TEXT("orbit-distance-landscape"),
				FString::Printf(TEXT("eye %.1f km from the centre (spec about %.0f)"), D, Expect));
			T.Check(Shell < HalfV && Shell < HalfH, bPortrait ? TEXT("orbit-fit-portrait") : TEXT("orbit-fit-landscape"),
				FString::Printf(TEXT("shell %.2f deg inside half fov %.2f x %.2f deg"), FMath::RadiansToDegrees(Shell), FMath::RadiansToDegrees(HalfH), FMath::RadiansToDegrees(HalfV)));
			T.Check(FVector::DotProduct(V.Forward, -V.FocusDir) > 0.9999, TEXT("orbit-straight-down"), FString::Printf(TEXT("forward . -focus = %.6f"), FVector::DotProduct(V.Forward, -V.FocusDir)));
		}

		// 2. altitude and tilt curves
		{
			FDmCameraRig R;
			R.SetViewport(1920, 1080);
			bool bMono = true, bTilt = true;
			double Prev = 1e9, PrevT = -1;
			for (int32 I = 0; I <= 100; ++I)
			{
				const double Z = I / 100.0;
				const double H = R.HeightAt(Z), Tl = R.TiltAt(Z);
				bMono &= H < Prev;
				bTilt &= Tl >= PrevT - 1e-12;
				if (Z <= 0.35) bTilt &= Tl < 1e-9;
				Prev = H; PrevT = Tl;
			}
			T.Check(bMono && FMath::IsNearlyEqual(R.HeightAt(1.0), R.Tune.NearHeightKm, 1e-6), TEXT("altitude-curve"),
				FString::Printf(TEXT("exponential %.1f km -> %.2f km, h(0.5) = %.2f km"), R.HeightAt(0), R.HeightAt(1), R.HeightAt(0.5)));
			T.Check(bTilt && FMath::IsNearlyEqual(FMath::RadiansToDegrees(R.TiltAt(1.0)), R.Tune.TiltMaxDeg, 1e-6), TEXT("tilt-curve"),
				FString::Printf(TEXT("0 until 0.35, %.1f deg at 0.7, %.1f at 1"), FMath::RadiansToDegrees(R.TiltAt(0.7)), FMath::RadiansToDegrees(R.TiltAt(1.0))));
		}

		// 3. never below the ground: random views on the real world, including mountains and the poles
		if (W)
		{
			FDmCameraRig R;
			R.SetViewport(1920, 1080);
			dm::Rng Rn(7);
			double MinClear = 1e9, MinAltErr = 1e9, MaxAltErr = 0;
			int32 Bad = 0, NonFinite = 0;
			for (int32 I = 0; I < 4000; ++I)
			{
				const FVector D = FVector(Rn.f() * 2 - 1, Rn.f() * 2 - 1, Rn.f() * 2 - 1).GetSafeNormal();
				const double Z = I < 200 ? 1.0 : Rn.f();
				R.Jump(D.IsNearlyZero() ? FVector(0, 0, 1) : D, Z, Rn.f() * 6.28);
				R.Update(0.016, Ground);
				if (!R.IsFinite()) { ++NonFinite; continue; }
				const FDmCameraView& V = R.View();
				const double Clear = V.Eye.Size() - Ground(V.Eye.GetSafeNormal());
				MinClear = FMath::Min(MinClear, Clear);
				if (Clear < R.Tune.MinClearKm - 1e-6) ++Bad;
				const double Err = V.AltitudeKm - R.HeightAt(Z);
				MinAltErr = FMath::Min(MinAltErr, Err);
				MaxAltErr = FMath::Max(MaxAltErr, FMath::Abs(Err));
			}
			// the highest peak, at zoom 1, every heading
			int32 Peak = 0;
			for (int32 C = 0; C < W->n; ++C) if (W->elev[size_t(C)] > W->elev[size_t(Peak)]) Peak = C;
			for (int32 Hd = 0; Hd < 36; ++Hd)
			{
				R.Jump(DmCoords::CellDir(*W, Peak), 1.0, Hd * 10.0 * UE_PI / 180.0);
				R.Update(0.016, Ground);
				const FDmCameraView& V = R.View();
				const double Clear = V.Eye.Size() - Ground(V.Eye.GetSafeNormal());
				MinClear = FMath::Min(MinClear, Clear);
				if (Clear < R.Tune.MinClearKm - 1e-6) ++Bad;
			}
			T.Check(Bad == 0 && NonFinite == 0, TEXT("never-below-ground"),
				FString::Printf(TEXT("4036 views (200 at zoom 1, 36 over the highest peak, elev %.2f): min clearance %.3f km (limit %.2f), %d below, %d non-finite"),
					W->elev[size_t(Peak)], MinClear, R.Tune.MinClearKm, Bad, NonFinite));
			T.Check(MaxAltErr < 0.02 * R.HeightAt(0.0) || MinAltErr > -0.05, TEXT("altitude-above-ground"),
				FString::Printf(TEXT("eye height above the ground under it vs the curve: max |error| %.3f km (clearance pushes only raise it)"), MaxAltErr));
		}

		// 4. grab-drag: the grabbed ground stays under the pointer
		{
			FDmCameraRig R;
			R.SetViewport(1920, 1080);
			double WorstPx = 0;
			for (int32 Case = 0; Case < 3; ++Case)
			{
				const double Z = Case == 0 ? 0.0 : (Case == 1 ? 0.45 : 0.8);
				R.Jump(DmCoords::LatLonToDir(15, -40), Z, 0.3);
				R.Update(0.016, Ground);
				const FVector2D P0(760, 600);
				FVector O, D;
				R.ScreenRay(P0, O, D);
				const double Rg = R.View().Target.Size();
				const double B = FVector::DotProduct(O, D), C = O.SizeSquared() - Rg * Rg;
				const FVector G = O + D * (-B - FMath::Sqrt(FMath::Max(B * B - C, 0.0)));
				R.GrabBegin(P0);
				FVector2D Px = P0;
				for (int32 F = 1; F <= 20; ++F)
				{
					Px = P0 + FVector2D(14.0 * F, -6.0 * F);
					R.GrabMove(Px);
					R.Update(0.016, Ground);
				}
				FVector2D Seen;
				R.Project(G, Seen);
				WorstPx = FMath::Max(WorstPx, FVector2D::Distance(Seen, Px));
				R.GrabEnd();
			}
			T.Check(WorstPx < 6.0, TEXT("grab-follows-pointer"), FString::Printf(TEXT("280 px drags at zoom 0 / 0.45 / 0.8: grabbed point within %.2f px of the pointer"), WorstPx));
		}

		// 5. inertia after a fling decays
		{
			FDmCameraRig R;
			R.SetViewport(1920, 1080);
			R.Jump(DmCoords::LatLonToDir(0, 0), 0.3, 0.0);
			R.Update(0.016, Ground);
			R.GrabBegin(FVector2D(900, 540));
			for (int32 F = 1; F <= 10; ++F) { R.GrabMove(FVector2D(900 + 25.0 * F, 540)); R.Update(0.016, Ground); }
			R.GrabEnd();
			const double W0 = R.InertiaSpeed();
			const FVector F0 = R.GetFocus();
			for (int32 F = 0; F < 30; ++F) R.Update(0.016, Ground);
			const double Moved = FMath::Acos(FMath::Clamp(FVector::DotProduct(F0, R.GetFocus()), -1.0, 1.0));
			for (int32 F = 0; F < 300; ++F) R.Update(0.016, Ground);
			T.Check(W0 > 0.01 && Moved > 0.001 && R.InertiaSpeed() == 0.0, TEXT("inertia"),
				FString::Printf(TEXT("fling %.3f rad/s, coasted %.4f rad in 0.5 s, stopped after 5 s (%.5f)"), W0, Moved, R.InertiaSpeed()));
		}

		// 6. poles: heading transport across the pole, no NaN, no flip of the view
		{
			FDmCameraRig R;
			R.SetViewport(1920, 1080);
			R.Jump(DmCoords::LatLonToDir(80, 10), 0.5, 0.0);
			R.Update(0.016, Ground);
			double MaxUpJump = 0;
			FVector PrevUp = R.View().Up;
			bool bFinite = true;
			for (int32 F = 0; F < 400; ++F)
			{
				R.Pan(0.0, 0.01);
				R.Update(0.016, Ground);
				bFinite &= R.IsFinite();
				const FVector Up = R.View().Up;
				MaxUpJump = FMath::Max(MaxUpJump, FMath::Acos(FMath::Clamp(FVector::DotProduct(Up, PrevUp), -1.0, 1.0)));
				PrevUp = Up;
			}
			double Lat, Lon;
			DmCoords::DirToLatLon(R.GetFocus(), Lat, Lon);
			T.Check(bFinite && MaxUpJump < 0.05, TEXT("pole-crossing"),
				FString::Printf(TEXT("400 frames north across the pole to %.1f,%.1f: screen-up turns at most %.4f rad per frame, finite %d"), Lat, Lon, MaxUpJump, bFinite ? 1 : 0));
			// exactly at the pole
			FDmCameraRig P;
			P.SetViewport(1920, 1080);
			P.Jump(FVector(0, 0, 1), 0.8, 1.0);
			P.Update(0.016, Ground);
			P.Turn(0.5);
			for (int32 F = 0; F < 60; ++F) { P.Orbit(3.0, 2.0); P.Update(0.016, Ground); }
			T.Check(P.IsFinite(), TEXT("at-the-pole"), FString::Printf(TEXT("jump to (0,0,1), turn, orbit: finite %d"), P.IsFinite() ? 1 : 0));
		}

		// 7. fly-to (including the antipode) lands on the target; Busy while flying
		{
			FDmCameraRig R;
			R.SetViewport(1920, 1080);
			R.Jump(DmCoords::LatLonToDir(5, 5), 0.6, 0.0);
			R.Update(0.016, Ground);
			const FVector Target = -R.GetFocus();
			R.FlyTo(Target, 0.7, 1.6);
			const bool bBusy = R.Busy();
			double MinZ = 1;
			for (int32 F = 0; F < 120; ++F) { R.Update(1.0 / 60.0, Ground); MinZ = FMath::Min(MinZ, R.GetZoom()); }
			const double Err = FMath::Acos(FMath::Clamp(FVector::DotProduct(R.GetFocus(), Target), -1.0, 1.0));
			T.Check(bBusy && !R.Busy() && Err < 1e-4 && FMath::IsNearlyEqual(R.GetZoom(), 0.7, 1e-6) && MinZ < 0.3 && R.IsFinite(), TEXT("fly-to-antipode"),
				FString::Printf(TEXT("busy %d, landed %.6f rad from the target at zoom %.3f, rose to zoom %.3f on the way"), bBusy ? 1 : 0, Err, R.GetZoom(), MinZ));
		}

		// 8. zoom toward the pointer moves the focus toward it; shake jitters the view, not the eye, and decays
		{
			FDmCameraRig R;
			R.SetViewport(1920, 1080);
			R.Jump(DmCoords::LatLonToDir(0, 0), 0.2, 0.0);
			R.Update(0.016, Ground);
			const FVector Anchor = DmCoords::LatLonToDir(8, -8);
			const double A0 = FMath::Acos(FVector::DotProduct(R.GetFocus(), Anchor));
			R.ZoomBy(0.2, &Anchor);
			for (int32 F = 0; F < 120; ++F) R.Update(1.0 / 60.0, Ground);
			const double A1 = FMath::Acos(FVector::DotProduct(R.GetFocus(), Anchor));
			T.Check(A1 < A0 && FMath::IsNearlyEqual(R.GetZoom(), 0.4, 1e-3), TEXT("zoom-toward-pointer"),
				FString::Printf(TEXT("zoom 0.2 -> %.3f, anchor %.4f -> %.4f rad from the focus"), R.GetZoom(), A0, A1));
			for (int32 F = 0; F < 300; ++F) R.Update(1.0 / 60.0, Ground);   // let the smoothed ground settle first
			const FVector Eye0 = R.View().Eye, Fw0 = R.View().Forward;
			R.Shake(0.8);
			double MaxDev = 0;
			for (int32 F = 0; F < 20; ++F) { R.Update(1.0 / 60.0, Ground); MaxDev = FMath::Max(MaxDev, FMath::Acos(FMath::Clamp(FVector::DotProduct(R.View().Forward, Fw0), -1.0, 1.0))); }
			const double EyeMove = FVector::Dist(R.View().Eye, Eye0);
			for (int32 F = 0; F < 300; ++F) R.Update(1.0 / 60.0, Ground);
			T.Check(MaxDev > 1e-4 && MaxDev < FMath::DegreesToRadians(1.5) && EyeMove < 1e-6 && R.ShakeAmplitude() == 0.0, TEXT("shake"),
				FString::Printf(TEXT("view jitter up to %.3f deg, eye moved %.2g km, amplitude after 5 s %.4f"), FMath::RadiansToDegrees(MaxDev), EyeMove, R.ShakeAmplitude()));
		}

		// 9. cost
		{
			FDmCameraRig R;
			R.SetViewport(1920, 1080);
			R.Jump(DmCoords::LatLonToDir(-24.16, -113.67), 1.0, 0.0);
			const double C0 = FPlatformTime::Seconds();
			for (int32 F = 0; F < 1000; ++F) { R.Pan(0.001, 0.002); R.Update(1.0 / 60.0, Ground); }
			const double Us = (FPlatformTime::Seconds() - C0) * 1e6 / 1000.0;
			T.Check(Us < 200.0, TEXT("update-cost"), FString::Printf(TEXT("%.1f us per Update at zoom 1 with the sim ground (12 ground queries)"), Us));
		}

		UE_LOG(LogDmCamScn, Display, TEXT("DmCamTest RESULT %s %d passed, %d failed (%.0f ms)"), T.Fail == 0 ? TEXT("PASS") : TEXT("FAIL"), T.Pass, T.Fail,
			(FPlatformTime::Seconds() - T0) * 1000.0);
		Ctx.SetExitCode(T.Fail == 0 ? 0 : 1);
	}
}

static FDmScenarioRegistrar GDmCamMath(TEXT("cam-math"), TEXT("camera"),
	TEXT("camera rig tests without rendering (run with -noshot): DmCamTest lines"),
	[](FDmScenarioContext& Ctx) { Ctx.HoldExit(); },
	[](FDmScenarioContext& Ctx, float)
	{
		if (Ctx.Frame == 3)
		{
			RunCamMath(Ctx);
			Ctx.Finish();
		}
	});
