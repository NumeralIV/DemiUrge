// DEMIURGE — Game/Planet/DmPlanetScenarios.cpp : the planet module's scenarios (SPEC-UNREAL 0.5). Owner: C1.
//
// Each scenario is a series of views; each view sets the camera (relative to the RENDERED ground, so
// near-ground shots never start inside a hill), pins the sun at a chosen local elevation / azimuth,
// settles the LOD, waits for Lumen / TSR, then shoots shots/ue-<scenario>-<view>.png. Run them with
// -noshot (they end themselves):
//
//   Lanes/planet/shot.ps1 planet-orbit -noshot        day, night, terminator, dusk from orbit
//   Lanes/planet/shot.ps1 planet-coast -noshot        a coast with a river mouth at mid zoom
//   Lanes/planet/shot.ps1 planet-mountains -noshot    the highest range from 2.5 km and 600 m
//   Lanes/planet/shot.ps1 planet-delta -noshot        the largest river delta, 30 km and 6 km
//   Lanes/planet/shot.ps1 planet-rivers -noshot       rivers from orbit (400 km) and 120 km
//   Lanes/planet/shot.ps1 planet-descent -noshot      400 km -> 80 -> 12 -> 1.3 -> 0.3 km over a valley
//   Lanes/planet/shot.ps1 planet-states -noshot       every per-cell state written into a region, before / after,
//                                                     plus a sculpt (incremental rebuild timings, hitches)
//   Lanes/planet/shot.ps1 planet-lod -noshot          LOD levels and skirts (crack check) near the ground
//   Lanes/planet/shot.ps1 planet-overlay -noshot      culture borders, temperature, habitability overlays
//   Lanes/planet/shot.ps1 planet-molten -noshot       the molten genesis crust
//   Lanes/planet/shot.ps1 planet-fly -noshot          a streamed 20 s descent (no settle): hitch statistics
//   Lanes/planet3/shot.ps1 planet-fixes -noshot       (wave 3) farmland 40/10/2 km, a sim volcano's scorch 60/15/4.5 km,
//                                                     a late sea rise 400/60/12/2.5 km (-rise=, logs a depth profile)
// Options: -views=a,b (only these views), -hold=<frames> per view (default 150), -apscale=<aerial perspective>,
// -profilegpu (one ProfileGPU capture 20 frames before each shot: the BasePass line is the land material's cost).
// Every shot also logs the view's GPU time (median / min / p90 of the last 60 frames, stat unit).
// The reference series is shot with -ev=-1 (exposure bias of the placeholder sky, as the surface lab did): at
// 0 EV the 10 lux sun overexposes the land from orbit and the tonemapper washes it out.
//
// Lighting assist: while the bridge's placeholder sky is the scene's sky, these scenarios add what the sky
// module (C2) will own (docs/lab/surface.md section 7): per-pixel atmosphere transmittance on the sun, a
// real-time sky light kept above the ground under the camera, and aerial perspective scale 0.5. With a real
// sky part in the scene they change nothing.

#include "Game/Planet/DmPlanet.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmGameMode.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Sim/State.h"
#include "Sim/Civ.h"
#include "Sim/Sim.h"

#include "Components/DirectionalLightComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmPlanetScn, Log, All);

namespace
{
	struct FView
	{
		FString Name;
		double Lat = 0, Lon = 0;      // target (sim lat / lon, degrees)
		double DistKm = 700;          // camera distance from the target along the view ray
		double Heading = 0, Pitch = 90, Fov = 30;
		double SunElev = 35, SunAz = 135;   // sun at the target: elevation above the horizon, compass azimuth
		bool bSunAbs = false; double SunLat = 0, SunLon = 0;   // or an absolute subsolar point
		TFunction<void(FDmScenarioContext&)> Before;          // world changes before this view
		int32 Hold = -1;
		// (wave 4) called on each of the first DuringFrames frames of the view (a live genesis wave advancing a tick
		// per frame); the shot waits for the maps to catch up after the last call
		TFunction<void(FDmScenarioContext&, int32)> During;
		int32 DuringFrames = 0;
	};

	struct FRun
	{
		TArray<FView> Views;
		int32 Cur = -1;
		int32 ViewFrame = 0;
		int32 ShotAt = -1;
		int32 Hold = 150;
		TArray<float> Frames;
		TArray<float> Gpu;           // (wave 3) GPU ms per frame (stat unit), for per-view cost comparisons
		bool bDone = false;
		TWeakObjectPtr<ASkyLight> Sky;
		double MaxFrameAfterChange = 0; int32 ChangeFrames = 0;
		// (verify) a world change (eruption, sea rise) must reach the colour maps before the next shot: IsSettled() does
		// not see a dirty map that has not started its job yet, and under load the map worker took up to 28 s, so the
		// scorch-60 / -15 shots showed no scar at all. Shots wait for a map update after MapsBase that has stood 30 frames.
		int32 MapsBase = -1, MapsSeen = -1, MapsStable = 0;
		bool bShotTaken = false;
	};
	FRun GRun;
	/** (verify) true once the colour maps have been updated after the last world change and stood for 30 frames. */
	bool MapsCaughtUp(const ADmPlanet* P)
	{
		if (!P || GRun.MapsBase < 0) return true;
		if (P->MapUpdates != GRun.MapsSeen) { GRun.MapsSeen = P->MapUpdates; GRun.MapsStable = 0; }
		else ++GRun.MapsStable;
		return P->MapUpdates > GRun.MapsBase && GRun.MapsStable >= 30;
	}

	FVector Dir(double Lat, double Lon) { return DmCoords::LatLonToDir(Lat, Lon); }

	void CellLatLon(const dm::World& W, int32 C, double& Lat, double& Lon) { DmCoords::DirToLatLon(DmCoords::CellDir(W, C), Lat, Lon); }

	ADmPlanet* Planet(FDmScenarioContext& Ctx) { return ADmPlanet::Find(Ctx.World); }

	/** World-space sun direction for a local elevation / azimuth at a planet direction -> subsolar lat / lon. */
	void SubsolarFor(const FVector& Up, double ElevDeg, double AzDeg, double& OutLat, double& OutLon)
	{
		FVector E, N; DmCoords::Basis(Up, E, N);
		const double El = FMath::DegreesToRadians(ElevDeg), Az = FMath::DegreesToRadians(AzDeg);
		const FVector S = (N * FMath::Cos(Az) + E * FMath::Sin(Az)) * FMath::Cos(El) + Up * FMath::Sin(El);
		DmCoords::DirToLatLon(S.GetSafeNormal(), OutLat, OutLon);
	}

	void LightingAssist(FDmScenarioContext& Ctx)
	{
		bool bPlaceholderSky = false;
		for (TActorIterator<AActor> It(Ctx.World); It; ++It)
			if (It->GetClass()->GetName().Contains(TEXT("PlaceholderSky"))) bPlaceholderSky = true;
		for (TObjectIterator<UDirectionalLightComponent> It; It; ++It)
			if (It->GetWorld() == Ctx.World && !It->bPerPixelAtmosphereTransmittance)
			{
				It->bPerPixelAtmosphereTransmittance = true;
				It->MarkRenderStateDirty();
				UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: lighting assist: per-pixel atmosphere transmittance on %s"), *It->GetOwner()->GetName());
			}
		if (!bPlaceholderSky) return;
		double ApScale = 0.5;
		FDmShotArgs::Value(TEXT("-apscale="), ApScale);
		for (TObjectIterator<USkyAtmosphereComponent> It; It; ++It)
			if (It->GetWorld() == Ctx.World) { It->AerialPespectiveViewDistanceScale = float(ApScale); It->MarkRenderStateDirty(); }
		bool bHasSkyLight = false;
		for (TObjectIterator<USkyLightComponent> It; It; ++It) if (It->GetWorld() == Ctx.World) bHasSkyLight = true;
		if (!bHasSkyLight)
		{
			ASkyLight* SL = Ctx.World->SpawnActor<ASkyLight>(FVector(0, 0, 1.2e7), FRotator::ZeroRotator);
			if (SL && SL->GetLightComponent())
			{
				USkyLightComponent* C = SL->GetLightComponent();
				C->SetMobility(EComponentMobility::Movable);
				C->bRealTimeCapture = true;
				C->SourceType = ESkyLightSourceType::SLS_CapturedScene;
				C->SetIntensity(1.0f);
				C->MarkRenderStateDirty();
				GRun.Sky = SL;
			}
		}
		UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: lighting assist for the placeholder sky (sky light %s, aerial perspective %.2f)"), bHasSkyLight ? TEXT("present") : TEXT("added"), ApScale);
	}

	/** Camera looking at the rendered surface point (lat, lon) from DistKm along a ray with heading / pitch. */
	void ApplyView(FDmScenarioContext& Ctx, const FView& V)
	{
		ADmPlanet* P = Planet(Ctx);
		const FTransform P2W = Ctx.Sim ? Ctx.Sim->GetPlanetToWorld() : FTransform::Identity;
		const FVector Up = Dir(V.Lat, V.Lon);
		const double Hm = P ? P->SurfaceHeightM(Up) : 0.0;
		const FVector Target = Up * (1.0e7 + Hm * 100.0);
		FVector East, North; DmCoords::Basis(Up, East, North);
		const double H = FMath::DegreesToRadians(V.Heading), Pt = FMath::DegreesToRadians(FMath::Clamp(V.Pitch, -89.9, 90.0));
		const FVector Horizontal = North * FMath::Cos(H) + East * FMath::Sin(H);
		const FVector Forward = (Horizontal * FMath::Cos(Pt) - Up * FMath::Sin(Pt)).GetSafeNormal();
		FVector Loc = Target - Forward * V.DistKm * 1.0e5;
		// never below the rendered ground: keep 25 m of clearance (or 10 % of the view distance)
		if (P)
		{
			const FVector Ld = Loc.GetSafeNormal();
			const double G = FMath::Max(P->SurfaceHeightM(Ld), P->GroundHeightM(Ld));
			const double Need = 1.0e7 + (G + FMath::Max(25.0, 5.0 * V.DistKm)) * 100.0;
			if (Loc.Length() < Need) Loc = Ld * Need;
		}
		const FVector CamUp = (FMath::Abs(V.Pitch) > 89.0) ? Horizontal : Up;
		const FVector Fw = (Target - Loc).GetSafeNormal();
		const FRotator Rot = FRotationMatrix::MakeFromXZ(P2W.TransformVectorNoScale(Fw), P2W.TransformVectorNoScale(CamUp)).Rotator();
		const FVector WLoc = P2W.TransformPosition(Loc);
		Ctx.SetView(WLoc, Rot, V.Fov);
		double SLat = V.SunLat, SLon = V.SunLon;
		if (!V.bSunAbs) SubsolarFor(Up, V.SunElev, V.SunAz, SLat, SLon);
		Ctx.SetSubsolar(SLat, SLon);
		if (Ctx.Sim)
		{
			const FVector Sd = P2W.InverseTransformVectorNoScale(Ctx.Sim->GetSunDirection());
			FVector E2, N2; DmCoords::Basis(Up, E2, N2);
			UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: sun check: subsolar %.1f %.1f, sun.up %.3f sun.east %.3f sun.north %.3f; screen right . east %.3f"),
				SLat, SLon, Sd | Up, Sd | E2, Sd | N2, FRotationMatrix(Rot).GetScaledAxis(EAxis::Y) | P2W.TransformVectorNoScale(E2));
		}
		if (P)
		{
			P->SetView(WLoc, Rot.Vector(), V.Fov, 16.0 / 9.0, 1920);
			P->Settle();
			const double Alt = (Loc.Length() - 1.0e7) / 100.0 - (P ? P->GroundHeightM(Loc.GetSafeNormal()) : 0.0);
			UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: view %s: target lat %.2f lon %.2f (surface %.0f m), %.2f km away, heading %.0f pitch %.0f fov %.0f, camera %.0f m above ground; sun elev %.0f az %.0f; settle %.0f ms: %s"),
				*V.Name, V.Lat, V.Lon, Hm, V.DistKm, V.Heading, V.Pitch, V.Fov, Alt, V.SunElev, V.SunAz, P->LastSettleMs, *P->StatsLine());
		}
	}

	void KeepSkyLightUnderCamera(FDmScenarioContext& Ctx)
	{
		ASkyLight* SL = GRun.Sky.Get();
		ADmPlanet* P = Planet(Ctx);
		if (!SL || !P || !Ctx.PC || !Ctx.PC->PlayerCameraManager) return;
		const FTransform P2W = Ctx.Sim ? Ctx.Sim->GetPlanetToWorld() : FTransform::Identity;
		const FVector Cam = P2W.InverseTransformPosition(Ctx.PC->PlayerCameraManager->GetCameraLocation());
		const FVector D = Cam.GetSafeNormal();
		const double G = P->SurfaceHeightM(D);
		SL->SetActorLocation(P2W.TransformPosition(D * (1.0e7 + (G + 30.0) * 100.0)));
		// the capture is one point on the ground: from high up it would light the whole disc (and the
		// clouds on the night side) with that point's sky, so it fades out with altitude
		const double AltKm = (Cam.Length() - 1.0e7) / 1.0e5 - G / 1000.0;
		const float I = float(1.0 - FMath::SmoothStep(25.0, 160.0, AltKm));
		if (USkyLightComponent* C = SL->GetLightComponent()) if (FMath::Abs(C->Intensity - I) > 0.01f) C->SetIntensity(I);
	}

	bool ViewAllowed(const FString& Name)
	{
		FString List;
		if (!FDmShotArgs::Value(TEXT("-views="), List) || List.IsEmpty()) return true;
		TArray<FString> Parts; List.ParseIntoArray(Parts, TEXT(","));
		return Parts.Contains(Name);
	}

	void StartRun(FDmScenarioContext& Ctx, TArray<FView> Views)
	{
		GRun = FRun();
		for (FView& V : Views) if (ViewAllowed(V.Name)) GRun.Views.Add(MoveTemp(V));
		FDmShotArgs::Value(TEXT("-hold="), GRun.Hold);
		Ctx.HoldExit();
		LightingAssist(Ctx);
		UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: %d views, %d frames each"), GRun.Views.Num(), GRun.Hold);
	}

	void TickRun(FDmScenarioContext& Ctx, float Dt)
	{
		if (GRun.bDone) return;
		KeepSkyLightUnderCamera(Ctx);
		if (GRun.Frames.Num() >= 60) GRun.Frames.RemoveAt(0);
		GRun.Frames.Add(FApp::GetDeltaTime() * 1000.0);
		if (const ADmGameMode* GM = Ctx.World ? Ctx.World->GetAuthGameMode<ADmGameMode>() : nullptr)
		{
			double Gm, Rm, Hm, Gp; GM->GetLastUnitMs(Gm, Rm, Hm, Gp);
			if (GRun.Gpu.Num() >= 60) GRun.Gpu.RemoveAt(0);
			if (Gp > 0.0) GRun.Gpu.Add(float(Gp));
		}
		// -profilegpu: one ProfileGPU capture 20 frames before each shot (pass timings in the log: the base pass of
		// the land material is what a material change costs, nearly independent of other GPU users' load)
		if (GRun.Cur >= 0 && GRun.ShotAt < 0 && FDmShotArgs::Param(TEXT("profilegpu")) && GEngine && Ctx.World)
		{
			const int32 Hold = GRun.Views[GRun.Cur].Hold > 0 ? GRun.Views[GRun.Cur].Hold : GRun.Hold;
			if (GRun.ViewFrame == Hold - 20) { UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: profilegpu %s"), *GRun.Views[GRun.Cur].Name); GEngine->Exec(Ctx.World, TEXT("ProfileGPU")); }
		}
		if (GRun.Cur >= 0 && GRun.Cur < GRun.Views.Num() && GRun.Views[GRun.Cur].During && GRun.ViewFrame < GRun.Views[GRun.Cur].DuringFrames)
		{
			GRun.Views[GRun.Cur].During(Ctx, GRun.ViewFrame);
			if (ADmPlanet* Pd = Planet(Ctx)) GRun.MapsBase = Pd->MapUpdates;
		}
		if (GRun.ChangeFrames > 0) { GRun.MaxFrameAfterChange = FMath::Max(GRun.MaxFrameAfterChange, FApp::GetDeltaTime() * 1000.0); --GRun.ChangeFrames; }
		ADmPlanet* P = Planet(Ctx);
		if (GRun.ShotAt >= 0)
		{
			// the screenshot is taken at the end of the frame that requested it: keep the view a few frames
			if (GRun.ViewFrame < GRun.ShotAt + 4) { ++GRun.ViewFrame; return; }
			GRun.ShotAt = -1;
		}
		else if (GRun.Cur >= 0 && GRun.ViewFrame >= (GRun.Views[GRun.Cur].Hold > 0 ? GRun.Views[GRun.Cur].Hold : GRun.Hold) && ((MapsCaughtUp(P) && (!P || P->IsSettled())) || GRun.ViewFrame > 2400))
		{
			double Avg = 0, Mx = 0;
			for (float F : GRun.Frames) { Avg += F; Mx = FMath::Max(Mx, double(F)); }
			Avg /= FMath::Max(GRun.Frames.Num(), 1);
			UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: shot %s: frame %.2f ms avg (max %.2f) over %d frames | %s"), *GRun.Views[GRun.Cur].Name, Avg, Mx, GRun.Frames.Num(), P ? *P->StatsLine() : TEXT(""));
			// (verify) under heavy machine load the old 900-frame cap fired before the terrain and maps had caught up with
			// a sea rise (a level-0 planet in the shot): the cap is 2400 frames now, and a forced shot says so
			if (!MapsCaughtUp(P) || (P && !P->IsSettled()))
				UE_LOG(LogDmPlanetScn, Warning, TEXT("DmPlanetScn: shot %s forced at the frame cap: planet NOT settled (the picture may be stale)"), *GRun.Views[GRun.Cur].Name);
			if (GRun.Gpu.Num())
			{
				TArray<float> G = GRun.Gpu; G.Sort();
				UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: gpu %s: median %.2f ms, min %.2f, p90 %.2f over %d frames"), *GRun.Views[GRun.Cur].Name,
					G[G.Num() / 2], G[0], G[FMath::Min(G.Num() - 1, G.Num() * 9 / 10)], G.Num());
			}
			Ctx.Shot(GRun.Views[GRun.Cur].Name);
			GRun.ShotAt = GRun.ViewFrame;
			GRun.bShotTaken = true;
			++GRun.ViewFrame;
			return;
		}
		// (wave 4) a view moves on only after its shot: when the planet had settled but its maps had not caught up yet, the
		// old test skipped the view without a picture
		if (GRun.Cur < 0 || (GRun.bShotTaken && GRun.ViewFrame > 0 && GRun.ShotAt < 0 && GRun.ViewFrame >= (GRun.Views[GRun.Cur].Hold > 0 ? GRun.Views[GRun.Cur].Hold : GRun.Hold) && (!P || P->IsSettled() || GRun.ViewFrame > 2400)))
		{
			++GRun.Cur;
			GRun.ViewFrame = 0;
			GRun.bShotTaken = false;
			if (GRun.Cur >= GRun.Views.Num()) { GRun.bDone = true; Ctx.Finish(); return; }
			UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: view %s starts"), *GRun.Views[GRun.Cur].Name);
			if (GRun.Views[GRun.Cur].Before) GRun.Views[GRun.Cur].Before(Ctx);
			ApplyView(Ctx, GRun.Views[GRun.Cur]);
			GRun.Frames.Reset();
			GRun.Gpu.Reset();
		}
		++GRun.ViewFrame;
	}

	// ---------------------------------------------------------------- targets found in the world

	/** The land cell with the most river cells within ~6 cells (lat / lon). */
	void RiverRich(const dm::World& W, double& Lat, double& Lon)
	{
		TArray<int32> Score; Score.Init(0, W.n);
		for (const dm::River& R : W.rivers) for (int32 C : R.cells) if (W.valid(C)) Score[C]++;
		int32 Best = 0; int32 BestC = 0;
		std::vector<int32_t> Ring;
		for (int32 C = 0; C < W.n; C += 7)
		{
			if (dm::state::isSea(W, C)) continue;
			W.geo->ring(C, 6, Ring);
			int32 S = 0; for (int32_t M : Ring) S += Score[M];
			if (S > Best) { Best = S; BestC = C; }
		}
		CellLatLon(W, BestC, Lat, Lon);
	}

	/** Mouth cell of the river with the largest discharge into the OPEN sea (not a pocket); -1 if none. */
	int32 BigMouth(const dm::World& W, int32* OutUp = nullptr)
	{
		float Best = 0; int32 BestC = -1, Up = -1;
		std::vector<int32_t> Ring;
		for (const dm::River& R : W.rivers)
		{
			if (R.cells.size() < 3) continue;
			const int32 Last = R.cells.back();
			if (!dm::state::isSea(W, Last)) continue;
			W.geo->ring(Last, 4, Ring);
			int32 SeaN = 0; for (int32_t M : Ring) if (dm::state::isSea(W, M)) ++SeaN;
			if (SeaN < int32(Ring.size()) / 2) continue;
			const float F = R.flow.size() >= 2 ? R.flow[R.flow.size() - 2] : 0.f;
			if (F > Best) { Best = F; BestC = Last; Up = R.cells[R.cells.size() >= 4 ? R.cells.size() - 4 : 0]; }
		}
		if (OutUp) *OutUp = Up;
		UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: largest open-sea mouth: cell %d, discharge %.1f"), BestC, Best);
		return BestC;
	}

	/** The highest temperate land cell with steep neighbours (a mountain range with forests below the snow). */
	int32 HighRange(const dm::World& W)
	{
		double Best = -1e9; int32 BestC = 0;
		for (int32 C = 0; C < W.n; ++C)
		{
			if (dm::state::isSea(W, C)) continue;
			if (FMath::Abs(FMath::RadiansToDegrees(W.geo->lat[size_t(C)])) > 50.0 || W.temp[size_t(C)] < -6.0f) continue;
			const double S = W.elev[size_t(C)] + 2.0 * W.slope[size_t(C)];
			if (S > Best) { Best = S; BestC = C; }
		}
		return BestC;
	}

	/** Heading (compass degrees) from cell A toward cell B. */
	double HeadingTo(const dm::World& W, int32 A, int32 B)
	{
		const FVector Ua = DmCoords::CellDir(W, A), Ub = DmCoords::CellDir(W, B);
		FVector E, N; DmCoords::Basis(Ua, E, N);
		const FVector D = (Ub - Ua);
		return FMath::RadiansToDegrees(FMath::Atan2(D | E, D | N));
	}

	/** A coastal land cell near the most populated region (rivers and towns at mid zoom). */
	int32 SettledCoast(const dm::World& W)
	{
		int32 BestS = -1; double BestPop = -1;
		for (const dm::Settlement& S : W.settlements)
			if (!S.dead && W.valid(S.cell) && W.coast[size_t(S.cell)] <= 2 && S.pop > BestPop) { BestPop = S.pop; BestS = S.cell; }
		return BestS >= 0 ? BestS : 0;
	}
}

#define DM_PLANET_SCENARIO(Name, Help, Body) \
	static FDmScenarioRegistrar GDmPlanetScn_##Body(TEXT(Name), TEXT("planet"), TEXT(Help), \
		[](FDmScenarioContext& Ctx) { Ctx.BuildScene(); const dm::World* W = Ctx.Sim ? Ctx.Sim->Sim() : nullptr; if (!W) { Ctx.SetExitCode(2); Ctx.Finish(); return; } StartRun(Ctx, Body(Ctx, *W)); }, \
		[](FDmScenarioContext& Ctx, float Dt) { TickRun(Ctx, Dt); });

// ------------------------------------------------------------------ orbit: day, night, terminator, dusk

static TArray<FView> OrbitViews(FDmScenarioContext& Ctx, const dm::World& W)
{
	double Lat, Lon;
	const int32 C = SettledCoast(W);
	CellLatLon(W, C, Lat, Lon);
	Lat = FMath::Clamp(Lat, -40.0, 40.0);
	TArray<FView> V;
	FView Day; Day.Name = TEXT("day"); Day.Lat = Lat; Day.Lon = Lon; Day.DistKm = 520; Day.Pitch = 90; Day.Fov = 40; Day.SunElev = 62; Day.SunAz = 120; V.Add(Day);
	FView Term = Day; Term.Name = TEXT("terminator"); Term.SunElev = 4; Term.SunAz = 95; V.Add(Term);
	FView Dusk = Day; Dusk.Name = TEXT("dusk"); Dusk.SunElev = -6; Dusk.SunAz = 270; V.Add(Dusk);
	FView Night = Day; Night.Name = TEXT("night"); Night.SunElev = -60; Night.SunAz = 0; V.Add(Night);
	FView Full = Day; Full.Name = TEXT("full"); Full.DistKm = 900; Full.Fov = 30; Full.SunElev = 50; Full.SunAz = 250; V.Add(Full);
	return V;
}
DM_PLANET_SCENARIO("planet-orbit", "the whole planet from orbit: day, terminator, dusk, night, full disc", OrbitViews)

// ------------------------------------------------------------------ coast at mid zoom

static TArray<FView> CoastViews(FDmScenarioContext& Ctx, const dm::World& W)
{
	int32 Up = -1;
	int32 M = BigMouth(W, &Up);
	if (M < 0) M = SettledCoast(W);
	double Lat, Lon; CellLatLon(W, M, Lat, Lon);
	const double Hd = Up >= 0 ? HeadingTo(W, Up, M) : 30.0;   // looking downstream, out to sea
	TArray<FView> V;
	FView A; A.Name = TEXT("mid"); A.Lat = Lat; A.Lon = Lon; A.DistKm = 90; A.Heading = Hd + 25; A.Pitch = 38; A.Fov = 45; A.SunElev = 28; A.SunAz = Hd + 160; V.Add(A);
	FView B = A; B.Name = TEXT("low"); B.DistKm = 22; B.Pitch = 22; B.Fov = 55; V.Add(B);
	FView C = A; C.Name = TEXT("glint"); C.DistKm = 60; C.Pitch = 30; C.SunElev = 18; C.SunAz = Hd + 25; V.Add(C);
	return V;
}
DM_PLANET_SCENARIO("planet-coast", "a coast with a river mouth at mid zoom (90 km, 22 km, sun glint)", CoastViews)

// ------------------------------------------------------------------ mountains near the ground

static TArray<FView> MountainViews(FDmScenarioContext& Ctx, const dm::World& W)
{
	const int32 C = HighRange(W);
	double Lat, Lon; CellLatLon(W, C, Lat, Lon);
	TArray<FView> V;
	FView A; A.Name = TEXT("range"); A.Lat = Lat; A.Lon = Lon; A.DistKm = 14; A.Heading = 40; A.Pitch = 14; A.Fov = 60; A.SunElev = 22; A.SunAz = 140; V.Add(A);
	FView B = A; B.Name = TEXT("ridge"); B.DistKm = 3.2; B.Pitch = 9; B.Heading = 70; B.SunElev = 18; V.Add(B);
	FView D = A; D.Name = TEXT("valley"); D.DistKm = 1.1; D.Pitch = 12; D.Heading = 200; D.SunElev = 30; D.SunAz = 110; V.Add(D);
	FView E = A; E.Name = TEXT("above"); E.DistKm = 60; E.Pitch = 50; E.Heading = 0; E.SunElev = 25; E.SunAz = 120; V.Add(E);
	return V;
}
DM_PLANET_SCENARIO("planet-mountains", "the highest mountain range near the ground (60 km, 14 km, 3 km, 1 km)", MountainViews)

// ------------------------------------------------------------------ river delta

static TArray<FView> DeltaViews(FDmScenarioContext& Ctx, const dm::World& W)
{
	int32 Up = -1;
	int32 M = BigMouth(W, &Up);
	if (M < 0) M = 0;
	double Lat, Lon; CellLatLon(W, M, Lat, Lon);
	const double Hd = Up >= 0 ? HeadingTo(W, Up, M) : 0.0;
	TArray<FView> V;
	FView A; A.Name = TEXT("high"); A.Lat = Lat; A.Lon = Lon; A.DistKm = 45; A.Heading = Hd; A.Pitch = 60; A.Fov = 50; A.SunElev = 40; A.SunAz = Hd + 140; V.Add(A);
	FView B = A; B.Name = TEXT("mid"); B.DistKm = 14; B.Pitch = 32; V.Add(B);
	FView C = A; C.Name = TEXT("low"); C.DistKm = 3.5; C.Pitch = 16; C.Fov = 60; V.Add(C);
	return V;
}
DM_PLANET_SCENARIO("planet-delta", "the largest river delta (45 km, 14 km, 3.5 km)", DeltaViews)

// ------------------------------------------------------------------ rivers from orbit

static TArray<FView> RiverViews(FDmScenarioContext& Ctx, const dm::World& W)
{
	double Lat, Lon; RiverRich(W, Lat, Lon);
	TArray<FView> V;
	FView A; A.Name = TEXT("orbit"); A.Lat = Lat; A.Lon = Lon; A.DistKm = 400; A.Pitch = 90; A.Fov = 40; A.SunElev = 55; A.SunAz = 150; V.Add(A);
	FView B = A; B.Name = TEXT("high"); B.DistKm = 120; B.Pitch = 70; B.Fov = 45; V.Add(B);
	return V;
}
DM_PLANET_SCENARIO("planet-rivers", "rivers visible from orbit (400 km) and from 120 km", RiverViews)

// ------------------------------------------------------------------ descent series

static TArray<FView> DescentViews(FDmScenarioContext& Ctx, const dm::World& W)
{
	double Lat = 0, Lon = 0;
	FString Tgt;
	double LL[2] = { 0, 0 };
	if (FDmShotArgs::List(TEXT("-target="), LL, 2) == 2) { Lat = LL[0]; Lon = LL[1]; }
	else
	{
		// a forested valley with a river below a range: the river cell nearest the highest range
		const int32 H = HighRange(W);
		int32 Best = H; double BestA = 1e9;
		for (const dm::River& R : W.rivers)
			for (int32 C : R.cells)
			{
				if (!W.valid(C) || dm::state::isSea(W, C)) continue;
				const double A = W.geo->angle(C, H);
				if (A > 0.04 && A < BestA) { BestA = A; Best = C; }
			}
		CellLatLon(W, Best, Lat, Lon);
	}
	TArray<FView> V;
	const double D[5] = { 400, 79, 12, 1.3, 0.29 };
	const double Pt[5] = { 90, 55, 30, 18, 12 };
	const double Fv[5] = { 40, 50, 55, 60, 65 };
	for (int32 I = 0; I < 5; ++I)
	{
		FView A; A.Name = FString::Printf(TEXT("%d"), I + 1); A.Lat = Lat; A.Lon = Lon; A.DistKm = D[I]; A.Pitch = Pt[I]; A.Fov = Fv[I];
		A.Heading = 30; A.SunElev = 30; A.SunAz = 150; V.Add(A);
	}
	return V;
}
DM_PLANET_SCENARIO("planet-descent", "a 5-altitude descent to a river valley (400, 79, 12, 1.3, 0.29 km)", DescentViews)

// ------------------------------------------------------------------ per-cell states

static TArray<FView> StateViews(FDmScenarioContext& Ctx, const dm::World& W0)
{
	const int32 C = SettledCoast(W0);
	double Lat, Lon; CellLatLon(W0, C, Lat, Lon);
	TArray<FView> V;
	FView A; A.Name = TEXT("before"); A.Lat = Lat; A.Lon = Lon; A.DistKm = 70; A.Heading = 20; A.Pitch = 55; A.Fov = 50; A.SunElev = 35; A.SunAz = 140; V.Add(A);
	FView B = A; B.Name = TEXT("after");
	B.Before = [C](FDmScenarioContext& Cx)
	{
		dm::World* W = Cx.Sim ? Cx.Sim->SimMutable() : nullptr;
		if (!W) return;
		const double T0 = FPlatformTime::Seconds();
		std::vector<int32_t> Ring;
		// six sectors around the town: farms + town, scorch, lava, fire, drought, ice; and a sculpted hill and bay
		const dm::Geo& G = *W->geo;
		G.within(C, 0.14, Ring);
		FVector E, N; DmCoords::Basis(DmCoords::CellDir(*W, C), E, N);
		const FVector Uc = DmCoords::CellDir(*W, C);
		int32 Counts[8] = {};
		for (int32_t M : Ring)
		{
			const FVector D = DmCoords::CellDir(*W, M) - Uc;
			const double Ang = FMath::RadiansToDegrees(FMath::Atan2(D | E, D | N));
			const int32 Sec = int32(FMath::Fmod(Ang + 360.0, 360.0) / 60.0);
			const double R = G.angle(C, M) / 0.14;
			const bool bSea = dm::state::isSea(*W, M);
			switch (Sec)
			{
			case 0: if (!bSea) { W->farm[size_t(M)] = 1.f; if (R < 0.35) W->urban[size_t(M)] = 1.f; W->road[size_t(M)] = R < 0.7 ? 2 : 0; } ++Counts[0]; break;
			case 1: if (!bSea) W->scorch[size_t(M)] = 1.f; ++Counts[1]; break;
			case 2: if (!bSea && R < 0.6) W->lava[size_t(M)] = 1.5f; ++Counts[2]; break;
			case 3: if (!bSea) { W->fire[size_t(M)] = 0.9f; W->scorch[size_t(M)] = 0.5f; } ++Counts[3]; break;
			case 4: W->moist[size_t(M)] *= 0.15f; W->rad[size_t(M)] = R < 0.4 ? 1.f : 0.f; ++Counts[4]; break;
			default: W->ice[size_t(M)] = 1.f; ++Counts[5]; break;
			}
		}
		// sculpt: a 1.2 km hill on the farm side, a drowned bay on the fire side
		G.within(C, 0.06, Ring);
		for (int32_t M : Ring)
		{
			const double R = G.angle(C, M) / 0.06;
			const FVector D = DmCoords::CellDir(*W, M) - Uc;
			const double Side = D | N;
			W->elev[size_t(M)] += float((Side > 0 ? 6.0 : -3.0) * FMath::Square(FMath::Cos(R * UE_HALF_PI)));
			++Counts[6];
		}
		W->dirty.elev = W->dirty.color = W->dirty.water = true;
		UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: states written in %.1f ms: farms/town %d, scorch %d, lava %d, fire %d, drought+fallout %d, ice %d cells; sculpted %d cells"),
			(FPlatformTime::Seconds() - T0) * 1000.0, Counts[0], Counts[1], Counts[2], Counts[3], Counts[4], Counts[5], Counts[6]);
		GRun.MaxFrameAfterChange = 0; GRun.ChangeFrames = 120;
	};
	B.Hold = 200;
	V.Add(B);
	FView Cn = B; Cn.Name = TEXT("after-night"); Cn.Before = nullptr; Cn.SunElev = -30; Cn.Hold = 150; V.Add(Cn);
	FView D = B; D.Name = TEXT("after-close"); D.Before = [](FDmScenarioContext&)
	{
		UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: worst frame in the 120 frames after the state change: %.1f ms"), GRun.MaxFrameAfterChange);
	};
	D.DistKm = 16; D.Pitch = 35; D.Heading = 60; D.SunElev = 30; V.Add(D);
	return V;
}
DM_PLANET_SCENARIO("planet-states", "every per-cell state written into a region around a coastal town, before / after", StateViews)

// ------------------------------------------------------------------ LOD debug

static TArray<FView> LodViews(FDmScenarioContext& Ctx, const dm::World& W)
{
	const int32 C = HighRange(W);
	double Lat, Lon; CellLatLon(W, C, Lat, Lon);
	TArray<FView> V;
	FView A; A.Name = TEXT("levels"); A.Lat = Lat; A.Lon = Lon; A.DistKm = 4; A.Heading = 40; A.Pitch = 10; A.Fov = 70; A.SunElev = 35; A.SunAz = 140;
	A.Before = [](FDmScenarioContext& Cx) { if (ADmPlanet* P = Planet(Cx)) P->SetDebugMode(3); };
	V.Add(A);
	FView B = A; B.Name = TEXT("skirts"); B.Before = [](FDmScenarioContext& Cx) { if (ADmPlanet* P = Planet(Cx)) P->SetDebugMode(1); }; V.Add(B);
	FView D = A; D.Name = TEXT("shaded"); D.Before = [](FDmScenarioContext& Cx) { if (ADmPlanet* P = Planet(Cx)) P->SetDebugMode(0); }; V.Add(D);
	return V;
}
DM_PLANET_SCENARIO("planet-lod", "LOD levels, skirts and the shaded view near the ground (crack check)", LodViews)

// ------------------------------------------------------------------ overlays

static TArray<FView> OverlayViews(FDmScenarioContext& Ctx, const dm::World& W)
{
	double Lat, Lon; CellLatLon(W, SettledCoast(W), Lat, Lon);
	TArray<FView> V;
	const TCHAR* Names[5] = { TEXT("cultures"), TEXT("temperature"), TEXT("rainfall"), TEXT("devotion"), TEXT("habitability") };
	for (int32 I = 0; I < 5; ++I)
	{
		FView A; A.Name = Names[I]; A.Lat = Lat; A.Lon = Lon; A.DistKm = 600; A.Pitch = 90; A.Fov = 40; A.SunElev = 60; A.SunAz = 140;
		const int32 Mode = I + 1;
		A.Before = [Mode](FDmScenarioContext& Cx) { if (ADmPlanet* P = Planet(Cx)) P->SetOverlay(Mode); };
		A.Hold = 60;
		V.Add(A);
	}
	return V;
}
DM_PLANET_SCENARIO("planet-overlay", "the five overlay modes from orbit", OverlayViews)

// ------------------------------------------------------------------ molten genesis

static TArray<FView> MoltenViews(FDmScenarioContext& Ctx, const dm::World& W)
{
	double Lat, Lon; CellLatLon(W, SettledCoast(W), Lat, Lon);
	TArray<FView> V;
	FView A; A.Name = TEXT("orbit"); A.Lat = Lat; A.Lon = Lon; A.DistKm = 600; A.Pitch = 90; A.Fov = 40; A.SunElev = 20; A.SunAz = 140;
	A.Before = [](FDmScenarioContext& Cx) { if (dm::World* Wm = Cx.Sim->SimMutable()) { Wm->phase = dm::Phase::Molten; Wm->heat = 0.85; } };
	V.Add(A);
	FView B = A; B.Name = TEXT("close"); B.DistKm = 30; B.Pitch = 30; B.Before = nullptr; V.Add(B);
	return V;
}
DM_PLANET_SCENARIO("planet-molten", "the molten genesis crust (heat 0.85)", MoltenViews)

// ------------------------------------------------------------------ any view (verification and look development)
// -look=lat,lon,distKm[,heading,pitch,fov] -sunl=elev,az [-lookdebug=<planet debug mode>]; several views:
// -look2=..., -look3=... (each takes the same sun). Shots: <name>-1, -2, -3.

static TArray<FView> LookViews(FDmScenarioContext& Ctx, const dm::World& W)
{
	TArray<FView> V;
	double Sun[2] = { 30, 150 };
	FDmShotArgs::List(TEXT("-sunl="), Sun, 2);
	int32 Dbg = 0;
	FDmShotArgs::Value(TEXT("-lookdebug="), Dbg);
	const TCHAR* Keys[3] = { TEXT("-look="), TEXT("-look2="), TEXT("-look3=") };
	for (int32 I = 0; I < 3; ++I)
	{
		double L[6] = { 0, 0, 50, 0, 60, 50 };
		const int32 N = FDmShotArgs::List(Keys[I], L, 6);
		if (N < 3) continue;
		FView A; A.Name = FString::Printf(TEXT("%d"), I + 1); A.Lat = L[0]; A.Lon = L[1]; A.DistKm = L[2];
		if (N > 3) A.Heading = L[3];
		if (N > 4) A.Pitch = L[4];
		if (N > 5) A.Fov = L[5];
		A.SunElev = Sun[0]; A.SunAz = Sun[1];
		if (I == 0 && Dbg) A.Before = [Dbg](FDmScenarioContext& Cx) { if (ADmPlanet* P = Planet(Cx)) P->SetDebugMode(Dbg); };
		V.Add(A);
	}
	// -probe=x,y (screen pixel of the first view): logs where it hits and height profiles through it (N-S, E-W,
	// NE-SW, NW-SE, +-200 m every 5 m) so a shading artefact can be traced to the height function
	double Pr[2];
	if (V.Num() && FDmShotArgs::List(TEXT("-probe="), Pr, 2) == 2)
	{
		FView Probe = V[0]; Probe.Name = TEXT("probe"); Probe.Hold = 30;
		const double Px = Pr[0], Py = Pr[1];
		Probe.Before = [Px, Py](FDmScenarioContext& Cx)
		{
			ADmPlanet* P = Planet(Cx);
			if (!P || !Cx.PC) return;
			FVector O, D;
			if (!Cx.PC->DeprojectScreenPositionToWorld(Px, Py, O, D)) { UE_LOG(LogDmPlanetScn, Warning, TEXT("DmPlanetScn: probe: deproject failed")); return; }
			FVector Hit;
			if (!P->RaycastWorld(O, D, 1.0e9, Hit, false)) { UE_LOG(LogDmPlanetScn, Warning, TEXT("DmPlanetScn: probe: no hit")); return; }
			const FTransform P2W = Cx.Sim ? Cx.Sim->GetPlanetToWorld() : FTransform::Identity;
			const FVector Up = P2W.InverseTransformPosition(Hit).GetSafeNormal();
			double La, Lo; DmCoords::DirToLatLon(Up, La, Lo);
			FVector E, N; DmCoords::Basis(Up, E, N);
			UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: probe (%.0f, %.0f): lat %.5f lon %.5f ground %.2f m (%.0f m from the camera)"), Px, Py, La, Lo, P->GroundHeightM(Up), (Hit - O).Length() / 100.0);
			const FVector Axes[4] = { N, E, (N + E).GetSafeNormal(), (N - E).GetSafeNormal() };
			const TCHAR* AxN[4] = { TEXT("N"), TEXT("E"), TEXT("NE"), TEXT("SE") };
			for (int32 A = 0; A < 4; ++A)
			{
				FString Line;
				for (int32 S = -40; S <= 40; ++S)
				{
					const FVector Dd = (Up + Axes[A] * (S * 5.0 / 1.0e5)).GetSafeNormal();
					Line += FString::Printf(TEXT(" %.2f"), P->GroundHeightM(Dd));
				}
				UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: probe profile %s (-200..200 m, 5 m):%s"), AxN[A], *Line);
				if (A == 0 && P->Terrain())
					for (int32 S = -3; S <= 3; ++S)
					{
						const FVector Dd = (Up + Axes[A] * (S * 5.0 / 1.0e5)).GetSafeNormal();
						FDmSurfAttr At;
						const double H = P->Terrain()->Height(FVector3d(Dd.X, Dd.Z, Dd.Y), 0.5, &At);
						UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: probe N %+d m: h %.2f base %.2f relief E %.1f R %.1f river d %.0f hw %.1f lake %.1f above %.1f beach %.2f"),
							S * 5, H, At.BaseH, At.ReliefE, At.ReliefR, At.RiverD, At.RiverW, At.LakeM, At.AboveLake, At.BeachZone);
					}
			}
		};
		V.Insert(MoveTemp(Probe), 1);   // right after the view it probes (the camera is still there)
	}
	return V;
}
DM_PLANET_SCENARIO("planet-look", "any view: -look=lat,lon,distKm,heading,pitch,fov -sunl=elev,az [-look2= -look3= -lookdebug=]", LookViews)

// ------------------------------------------------------------------ streamed descent (no settle)

namespace
{
	struct FFly { double T = 0, Dur = 20; double Lat = 0, Lon = 0; TArray<float> Ms; int32 Shots = 0; bool bDone = false; double WorstMs = 0; int32 Over33 = 0; };
	FFly GFly;
}

static FDmScenarioRegistrar GDmPlanetFly(TEXT("planet-fly"), TEXT("planet"), TEXT("a streamed 20 s descent from 400 km to 0.8 km (async LOD, hitch statistics)"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		const dm::World* W = Ctx.Sim ? Ctx.Sim->Sim() : nullptr;
		GFly = FFly();
		if (W) { const int32 H = HighRange(*W); CellLatLon(*W, H, GFly.Lat, GFly.Lon); }
		FDmShotArgs::Value(TEXT("-flytime="), GFly.Dur);
		Ctx.HoldExit();
		GRun = FRun();
		LightingAssist(Ctx);
		FView V; V.Name = TEXT("fly"); V.Lat = GFly.Lat; V.Lon = GFly.Lon; V.DistKm = 400; V.Pitch = 80; V.Fov = 50; V.SunElev = 30; V.SunAz = 150;
		ApplyView(Ctx, V);   // the start is settled, the rest streams
	},
	[](FDmScenarioContext& Ctx, float Dt)
	{
		if (GFly.bDone) return;
		KeepSkyLightUnderCamera(Ctx);
		GFly.T += FMath::Min(Dt, 0.1f);
		const double U = FMath::Clamp(GFly.T / GFly.Dur, 0.0, 1.0);
		const double Dist = 400.0 * FMath::Pow(0.85 / 400.0, U);
		FView V; V.Name = TEXT("fly"); V.Lat = GFly.Lat; V.Lon = GFly.Lon; V.DistKm = Dist; V.Pitch = FMath::Lerp(80.0, 14.0, U); V.Fov = 50; V.Heading = 40.0 * U; V.SunElev = 30; V.SunAz = 150;
		// move the camera only (no settle): reuse ApplyView's maths through the harness camera
		{
			ADmPlanet* P = ADmPlanet::Find(Ctx.World);
			const FTransform P2W = Ctx.Sim->GetPlanetToWorld();
			const FVector Up = DmCoords::LatLonToDir(V.Lat, V.Lon);
			const double Hm = P ? P->SurfaceHeightM(Up) : 0.0;
			const FVector Target = Up * (1.0e7 + Hm * 100.0);
			FVector East, North; DmCoords::Basis(Up, East, North);
			const double H = FMath::DegreesToRadians(V.Heading), Pt = FMath::DegreesToRadians(V.Pitch);
			const FVector Hz = North * FMath::Cos(H) + East * FMath::Sin(H);
			const FVector Fw = (Hz * FMath::Cos(Pt) - Up * FMath::Sin(Pt)).GetSafeNormal();
			const FVector Loc = Target - Fw * Dist * 1.0e5;
			const FRotator Rot = FRotationMatrix::MakeFromXZ(P2W.TransformVectorNoScale(Fw), P2W.TransformVectorNoScale(Up)).Rotator();
			Ctx.SetView(P2W.TransformPosition(Loc), Rot, V.Fov);
		}
		const double Ms = FApp::GetDeltaTime() * 1000.0;
		if (Ctx.Frame > 5) { GFly.Ms.Add(float(Ms)); GFly.WorstMs = FMath::Max(GFly.WorstMs, Ms); if (Ms > 33.3) ++GFly.Over33; }
		const int32 Want = FMath::Min(4, int32(U * 4.0 + 0.001));
		if (Want > GFly.Shots && U < 1.0) { ++GFly.Shots; Ctx.Shot(FString::Printf(TEXT("%d"), GFly.Shots)); }
		if (U >= 1.0)
		{
			double Avg = 0; for (float F : GFly.Ms) Avg += F; Avg /= FMath::Max(GFly.Ms.Num(), 1);
			ADmPlanet* P = ADmPlanet::Find(Ctx.World);
			UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: FLY DONE %.1f s, %d frames, avg %.2f ms, worst %.1f ms, %d frames over 33 ms (shots included) | %s"),
				GFly.T, GFly.Ms.Num(), Avg, GFly.WorstMs, GFly.Over33, P ? *P->StatsLine() : TEXT(""));
			Ctx.Shot(TEXT("end"));
			GFly.bDone = true;
			Ctx.Finish();
		}
	});

// ------------------------------------------------------------------ wave-3 planet fixes: farmland, volcano scorch, sea rise
// One run, three groups (each view runs its group's world change once, so -views= may pick any subset):
//   farm-40 / farm-10 / farm-2        the most farmed temperate settlement (docs/concept/nature descent-3 / descent-4)
//   scorch-60 / scorch-15 / scorch-4  a real sim volcano (powers::use, then 14 years of ticks) on forested ground
//   sea-400 / sea-60 / sea-12 / sea-2 a late-game sea rise (-rise=, default +0.32 elevation units = 64 m over
//                                     vars.seaBase) over the largest low coastal plain, with the sim's own
//                                     consequences (flooded cells lose vegetation, farms, towns; biome SHALLOWS)
// Run: Lanes/planet3/shot.ps1 planet-fixes -noshot -ev=-1   (shots ue-planet-fixes-<view>.png)

namespace
{
	struct FFixes { int32 Farm = -1, Vent = -1, Plain = -1, NewCoast = -1; bool bVolcano = false, bSea = false; double Rise = 0.32; };
	FFixes GFix;

	int32 FarmTarget(const dm::World& W)
	{
		int32 Best = -1; double BestS = -1;
		std::vector<int32_t> Ring;
		for (const dm::Settlement& S : W.settlements)
		{
			if (S.dead || !W.valid(S.cell)) continue;
			const double T = W.temp[size_t(S.cell)];
			if (T < 4.0 || T > 22.0) continue;
			W.geo->ring(S.cell, 3, Ring);
			double F = 0; for (int32_t M : Ring) F += W.farm[size_t(M)];
			if (F > BestS) { BestS = F; Best = S.cell; }
		}
		UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: fixes: farm target cell %d (farm sum %.1f within 3 rings)"), Best, BestS);
		return Best >= 0 ? Best : SettledCoast(W);
	}

	/** A forested inland hill (the vent): high vegetation, land all around, temperate, nobody living there. */
	int32 VentTarget(const dm::World& W)
	{
		int32 Best = -1; double BestS = -1e9;
		std::vector<int32_t> Ring;
		for (int32 C = 0; C < W.n; C += 3)
		{
			if (dm::state::isSea(W, C) || W.coast[size_t(C)] < 4) continue;
			const double T = W.temp[size_t(C)];
			if (T < 4.0 || T > 24.0 || W.urban[size_t(C)] > 0.05f || W.farm[size_t(C)] > 0.2f) continue;
			W.geo->ring(C, 3, Ring);
			double V = 0; int32 Bad = 0;
			for (int32_t M : Ring)
			{
				V += W.veg[size_t(M)];
				if (dm::state::isSea(W, M) || W.water[size_t(M)] > 0.03f || W.urban[size_t(M)] > 0.05f) ++Bad;
			}
			const double S = V - 5.0 * Bad + 0.3 * W.elev[size_t(C)];
			if (S > BestS) { BestS = S; Best = C; }
		}
		return Best >= 0 ? Best : 0;
	}

	/** The largest low coastal plain: land within Rise of the base sea, near the coast. */
	int32 PlainTarget(const dm::World& W, double Base, double Rise)
	{
		int32 Best = -1; int32 BestN = -1;
		std::vector<int32_t> Ring;
		for (int32 C = 0; C < W.n; C += 2)
		{
			const double E = W.elev[size_t(C)];
			if (E < Base || E >= Base + Rise || W.coast[size_t(C)] > 3) continue;
			W.geo->ring(C, 5, Ring);
			int32 N = 0;
			for (int32_t M : Ring) { const double Em = W.elev[size_t(M)]; if (Em >= Base && Em < Base + Rise) ++N; }
			if (N > BestN) { BestN = N; Best = C; }
		}
		UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: fixes: low plain cell %d (%d low cells within 5 rings)"), Best, BestN);
		return Best >= 0 ? Best : SettledCoast(W);
	}

	void EnsureVolcano(FDmScenarioContext& Cx)
	{
		if (GFix.bVolcano || !Cx.Sim) return;
		GFix.bVolcano = true;
		Cx.Sim->MakeLive();
		dm::World* W = Cx.Sim->SimMutable();
		if (!W) return;
		const double T0 = FPlatformTime::Seconds();
		W->god.aweMax = FMath::Max(W->god.aweMax, 1.0e6);
		W->god.awe = W->god.aweMax;
		// (verify) powers past Bronze unlock by age (sim v3) and the fixture is in the Sail age, so the volcano (Industry)
		// was refused on main and the scorch views showed no scar at all: unlock it for this one call, and fail the run
		// when it is still refused (a scorch view without a scar proves nothing)
		const int32 OldAge = W->age;
		W->age = FMath::Max(W->age, int32(dm::Age::INDUSTRY));
		const bool bOk = Cx.Sim->UsePower(TEXT("volcano"), GFix.Vent, 0.25f);
		W->age = OldAge;
		if (!bOk) Cx.SetExitCode(1);
		if (const ADmPlanet* P = Planet(Cx)) GRun.MapsBase = P->MapUpdates;
		// 14 years: the eruption (12 years) and the fires it lit; (verify) -volcanoticks=N stops earlier (lava still out)
		int32 Want = 56;
		FDmShotArgs::Value(TEXT("-volcanoticks="), Want);
		const int32 Ticks = Cx.Sim->StepTicks(FMath::Clamp(Want, 0, 400));
		Cx.Sim->SetPaused(true);
		int32 NS = 0, NL = 0; double MaxS = 0, SumS = 0;
		for (int32 C = 0; C < W->n; ++C)
		{
			const float S = W->scorch[size_t(C)];
			if (S > 0.05f) { ++NS; SumS += S; MaxS = FMath::Max(MaxS, double(S)); }
			if (W->lava[size_t(C)] > 0.01f) ++NL;
		}
		UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: fixes: volcano at cell %d %s, %d ticks in %.0f ms: %d scorched cells (max %.2f, mean %.2f), %d with lava"),
			GFix.Vent, bOk ? TEXT("started") : TEXT("REFUSED"), Ticks, (FPlatformTime::Seconds() - T0) * 1000.0, NS, MaxS, NS ? SumS / NS : 0.0, NL);
	}

	void EnsureSeaRise(FDmScenarioContext& Cx)
	{
		if (GFix.bSea || !Cx.Sim) return;
		GFix.bSea = true;
		if (Cx.Sim->IsLive()) Cx.Sim->SetPaused(true);
		dm::World* W = Cx.Sim->SimMutable();
		if (!W) return;
		const double Base = W->vars.seaBase.has_value() ? *W->vars.seaBase : W->seaLevel;
		W->vars.seaBase = Base;
		// (verify) -riseapply=0: the same views over the unrisen world (A/B); -riseraw: only the sea level moves (the
		// flooded cells keep their fields, forest, roads and towns, as in the first years of a real climate rise)
		double Apply = 1.0;
		FDmShotArgs::Value(TEXT("-riseapply="), Apply);
		W->seaLevel = Base + GFix.Rise * Apply;
		const bool bRaw = FDmShotArgs::Param(TEXT("riseraw"));
		if (const ADmPlanet* P = Planet(Cx)) GRun.MapsBase = P->MapUpdates;
		int32 Flooded = 0, Towns = 0;
		for (int32 C = 0; C < W->n; ++C)
		{
			const size_t I = size_t(C);
			if (!bRaw && W->elev[I] >= Base && W->elev[I] < W->seaLevel)
			{
				++Flooded;
				W->veg[I] = 0.f; W->farm[I] = 0.f; W->urban[I] = 0.f; W->fire[I] = 0.f; W->road[I] = 0;
				W->biome[I] = uint8(dm::Biome::SHALLOWS);
			}
		}
		for (dm::Settlement& S : W->settlements)
			if (!S.dead && W->valid(S.cell) && W->elev[size_t(S.cell)] < W->seaLevel) { S.dead = true; ++Towns; }
		W->dirty.elev = W->dirty.color = W->dirty.water = W->dirty.objects = W->dirty.roads = W->dirty.rivers = true;
		UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: fixes: sea rise %.3f -> %.3f (+%.0f m): %d cells flooded, %d settlements drowned"),
			Base, W->seaLevel, GFix.Rise * 200.0, Flooded, Towns);
	}
}

/** Ground and water along the line from the new coast out over the drowned plain (log; checks the depth gradient). */
static void SeaProfile(FDmScenarioContext& Cx)
{
	ADmPlanet* P = Planet(Cx);
	const dm::World* W = Cx.Sim ? Cx.Sim->Sim() : nullptr;
	if (!P || !W || !P->Terrain() || GFix.NewCoast < 0 || GFix.Plain < 0) return;
	const FVector A = DmCoords::CellDir(*W, GFix.NewCoast), B = DmCoords::CellDir(*W, GFix.Plain);
	FString Line;
	for (int32 I = 0; I <= 24; ++I)
	{
		const FVector D = FMath::Lerp(A, B, I / 12.0).GetSafeNormal();   // to twice the distance to the plain cell
		Line += FString::Printf(TEXT(" %.0f"), P->GroundHeightM(D) - P->SeaHeightM());
	}
	UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: fixes: sea %.1f m, ground datum %.1f m (rise %.1f m); ground - sea from the new coast over the plain (%.1f km, 25 steps):%s"),
		P->SeaHeightM(), P->Terrain()->RefM, P->SeaHeightM() - P->Terrain()->RefM, FMath::Acos(FMath::Clamp(A | B, -1.0, 1.0)) * 200.0, *Line);
}

static TArray<FView> FixesViews(FDmScenarioContext& Ctx, const dm::World& W)
{
	GFix = FFixes();
	FDmShotArgs::Value(TEXT("-rise="), GFix.Rise);
	GFix.Farm = FarmTarget(W);
	GFix.Vent = VentTarget(W);
	const double Base = W.vars.seaBase.has_value() ? *W.vars.seaBase : W.seaLevel;
	GFix.Plain = PlainTarget(W, Base, GFix.Rise);
	// the new coast nearest the plain: land just above the risen sea, next to a cell that floods
	{
		double BestA = 1e9;
		for (int32 C = 0; C < W.n; ++C)
		{
			const double E = W.elev[size_t(C)];
			if (E < Base + GFix.Rise || E > Base + GFix.Rise + 0.15) continue;
			bool bNext = false;
			for (int32 K = 0; K < W.geo->nbrN[size_t(C)]; ++K)
			{
				const int32 M = W.geo->nbr[size_t(6 * C + K)];
				if (M >= 0 && W.elev[size_t(M)] >= Base && W.elev[size_t(M)] < Base + GFix.Rise) bNext = true;
			}
			if (!bNext) continue;
			const double A = W.geo->angle(C, GFix.Plain);
			if (A < BestA) { BestA = A; GFix.NewCoast = C; }
		}
		if (GFix.NewCoast < 0) GFix.NewCoast = GFix.Plain;
	}
	TArray<FView> V;
	double Lat, Lon;
	auto Add = [&V](FView A, TFunction<void(FDmScenarioContext&)> Ensure) { A.Before = MoveTemp(Ensure); V.Add(MoveTemp(A)); };
	CellLatLon(W, GFix.Farm, Lat, Lon);
	{
		FView A; A.Lat = Lat; A.Lon = Lon; A.SunElev = 28; A.SunAz = 255; A.Heading = 0;
		A.Name = TEXT("farm-40"); A.DistKm = 40; A.Pitch = 60; A.Fov = 45; Add(A, nullptr);
		A.Name = TEXT("farm-10"); A.DistKm = 10; A.Pitch = 62; A.Fov = 45; Add(A, nullptr);
		A.Name = TEXT("farm-2"); A.DistKm = 2.2; A.Pitch = 58; A.Fov = 45; Add(A, nullptr);
	}
	CellLatLon(W, GFix.Vent, Lat, Lon);
	{
		FView A; A.Lat = Lat; A.Lon = Lon; A.SunElev = 32; A.SunAz = 240; A.Heading = 10;
		A.Name = TEXT("scorch-60"); A.DistKm = 60; A.Pitch = 70; A.Fov = 45; Add(A, [](FDmScenarioContext& Cx) { EnsureVolcano(Cx); });
		A.Name = TEXT("scorch-15"); A.DistKm = 15; A.Pitch = 55; A.Fov = 50; Add(A, [](FDmScenarioContext& Cx) { EnsureVolcano(Cx); });
		A.Name = TEXT("scorch-4"); A.DistKm = 4.5; A.Pitch = 40; A.Fov = 55; Add(A, [](FDmScenarioContext& Cx) { EnsureVolcano(Cx); });
	}
	{
		CellLatLon(W, GFix.Plain, Lat, Lon);
		FView A; A.Lat = Lat; A.Lon = Lon; A.SunElev = 35; A.SunAz = 235; A.Heading = 0;
		A.Name = TEXT("sea-400"); A.DistKm = 400; A.Pitch = 90; A.Fov = 40; Add(A, [](FDmScenarioContext& Cx) { EnsureSeaRise(Cx); });
		A.Name = TEXT("sea-60"); A.DistKm = 60; A.Pitch = 55; A.Fov = 45; Add(A, [](FDmScenarioContext& Cx) { EnsureSeaRise(Cx); });
		CellLatLon(W, GFix.NewCoast, Lat, Lon);
		A.Lat = Lat; A.Lon = Lon;
		A.Heading = HeadingTo(W, GFix.NewCoast, GFix.Plain);
		A.Name = TEXT("sea-12"); A.DistKm = 12; A.Pitch = 45; A.Fov = 50; Add(A, [](FDmScenarioContext& Cx) { EnsureSeaRise(Cx); });
		A.Name = TEXT("sea-2"); A.DistKm = 2.5; A.Pitch = 35; A.Fov = 55; Add(A, [](FDmScenarioContext& Cx) { EnsureSeaRise(Cx); SeaProfile(Cx); });
	}
	UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: fixes: farm %d, vent %d, plain %d, new coast %d, rise %.2f"), GFix.Farm, GFix.Vent, GFix.Plain, GFix.NewCoast, GFix.Rise);
	return V;
}
DM_PLANET_SCENARIO("planet-fixes", "wave-3 planet fixes: farmland (40/10/2 km), a volcano's scorch (60/15/4.5 km), a late sea rise (400/60/12/2.5 km)", FixesViews)

// ------------------------------------------------------------------ (wave 4) the sea at four zooms
// A coast with a river mouth, looking out to sea with the sun ahead (glint), from orbit (the planet card's framing),
// 50 km, 5 km and 300 m. -seatarget=lat,lon,heading picks another coast; -seasun=elev,azOffset moves the sun.

static TArray<FView> SeaViews(FDmScenarioContext& Ctx, const dm::World& W)
{
	int32 Up = -1;
	int32 M = BigMouth(W, &Up);
	if (M < 0) M = SettledCoast(W);
	double Lat, Lon; CellLatLon(W, M, Lat, Lon);
	double Hd = Up >= 0 ? HeadingTo(W, Up, M) : 30.0;   // downstream, out to sea
	double T[3];
	if (FDmShotArgs::List(TEXT("-seatarget="), T, 3) == 3) { Lat = T[0]; Lon = T[1]; Hd = T[2]; }
	double Sn[2] = { 24.0, 30.0 };
	FDmShotArgs::List(TEXT("-seasun="), Sn, 2);
	TArray<FView> V;
	FView O; O.Name = TEXT("orbit"); O.Lat = Lat; O.Lon = Lon; O.DistKm = 900; O.Pitch = 90; O.Fov = 30; O.SunElev = 50; O.SunAz = Hd + 200; V.Add(O);
	FView G = O; G.Name = TEXT("orbit-glint"); G.DistKm = 520; G.Fov = 40; G.SunElev = 62; G.SunAz = Hd; V.Add(G);
	FView A; A.Name = TEXT("50km"); A.Lat = Lat; A.Lon = Lon; A.DistKm = 50; A.Heading = Hd; A.Pitch = 34; A.Fov = 50; A.SunElev = Sn[0]; A.SunAz = Hd + Sn[1]; V.Add(A);
	FView B = A; B.Name = TEXT("5km"); B.DistKm = 5; B.Pitch = 20; B.Fov = 55; V.Add(B);
	FView C = A; C.Name = TEXT("300m"); C.DistKm = 0.3; C.Pitch = 13; C.Fov = 60; V.Add(C);
	UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: sea: coast cell %d lat %.2f lon %.2f heading %.0f"), M, Lat, Lon, Hd);
	return V;
}
DM_PLANET_SCENARIO("planet-sea", "(wave 4) the sea at a river mouth: orbit (card framing), orbit glint, 50 km, 5 km, 300 m", SeaViews)

// ------------------------------------------------------------------ (wave 4) genesis: molten crust and the life wave
// A new world (-gseed=, default 21) through the real genesis: the molten crust at heat 0.9 (orbit, 50 km, 5 km) and
// 0.35 (cooling), comets until the seas are full, then three seeds of life at the best site and the green wave after
// 2, 5 and 9 s of 1x play (orbit and 50 km).
namespace
{
	struct FGen { int32 Site = -1; int32 Seeds = 0; bool bSea = false; };
	FGen GGen;

	void GenStepTo(FDmScenarioContext& Cx, double Heat)
	{
		int32 Guard = 0;
		while (Cx.Sim->Sim() && Cx.Sim->Sim()->phase == dm::Phase::Molten && Cx.Sim->Sim()->heat > Heat && Guard++ < 400) Cx.Sim->StepTicks(1);
		UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: genesis: heat %.2f phase %s"), Cx.Sim->Sim()->heat, UTF8_TO_TCHAR(dm::toStr(Cx.Sim->Sim()->phase)));
	}

	/** Cool, fill the seas with comets (the real power), then enter the life phase. */
	void GenFillSeas(FDmScenarioContext& Cx)
	{
		if (GGen.bSea || !Cx.Sim) return;
		GGen.bSea = true;
		dm::World* W = Cx.Sim->SimMutable();
		int32 Guard = 0;
		while (W->phase == dm::Phase::Molten && Guard++ < 400) Cx.Sim->StepTicks(1);
		UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: genesis: cooled after %d ticks: phase %s, sea %.2f"), Guard, UTF8_TO_TCHAR(dm::toStr(W->phase)), W->seaLevel);
		FRandomStream R(7);
		int32 Refused = 0;
		int32 Comets = 0;
		Guard = 0;
		while (W->phase == dm::Phase::Water && W->seaLevel < -2.8 && Guard++ < 4000)
		{
			if ((Guard % 6) == 0)
				for (int32 Try = 0; Try < 30; ++Try)
				{
					const int32 C = R.RandRange(0, W->n - 1);
					if (Cx.Sim->UsePower(TEXT("comet"), C, 0.f)) { ++Comets; break; }
					++Refused;
				}
			Cx.Sim->StepTicks(1);
		}
		const bool bNext = W->phase == dm::Phase::Water && dm::sim::genesis::next(*W);
		GGen.Site = dm::civ::bestSite(*W);
		if (const ADmPlanet* P = Planet(Cx)) GRun.MapsBase = P->MapUpdates;
		UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: genesis: %d comets (%d refused), sea %.2f, life phase %d, best site %d"), Comets, Refused, W->seaLevel, bNext ? 1 : 0, GGen.Site);
	}

	/** The life wave: seeds at the best site (and two more near it), then one tick per frame. */
	void GenLife(FDmScenarioContext& Cx, int32 Frame)
	{
		dm::World* W = Cx.Sim ? Cx.Sim->SimMutable() : nullptr;
		if (!W || W->phase != dm::Phase::Life) return;
		if (GGen.Seeds == 0 && W->valid(GGen.Site))
		{
			std::vector<int32_t> Ring;
			W->geo->ring(GGen.Site, 9, Ring);
			int32 Placed = Cx.Sim->UsePower(TEXT("seed"), GGen.Site, 0.f) ? 1 : 0;
			for (size_t K = 0; K < Ring.size() && Placed < 2; K += 37)
				if (!dm::state::isSea(*W, Ring[K]) && Cx.Sim->UsePower(TEXT("seed"), Ring[K], 0.f)) ++Placed;
			GGen.Seeds = FMath::Max(Placed, 1);
			UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: genesis: %d seeds at site %d"), Placed, GGen.Site);
		}
		Cx.Sim->StepTicks(1);
		if ((Frame % 8) == 0)
		{
			const dm::sim::genesis::Progress G = dm::sim::genesis::progress(*W);
			UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: genesis: tick %lld veg cover %.3f"), (long long)W->tick, G.veg);
		}
	}
}

static FDmScenarioRegistrar GDmPlanetGenesis(TEXT("planet-genesis"), TEXT("planet"),
	TEXT("(wave 4) a new world's genesis: molten crust (orbit, 50 km, 5 km), cooling, then the life wave after 2 / 5 / 9 s"),
	[](FDmScenarioContext& Ctx)
	{
		GGen = FGen();
		int32 Sd = 21; FDmShotArgs::Value(TEXT("-gseed="), Sd);
		Ctx.Sim->CreateWorld(uint32(Sd), TEXT("Ithe"), false, 0.0);
		// (as genesis-look) the first heat is reached before the scene is built
		{ int32 Guard = 0; while (Ctx.Sim->Sim() && Ctx.Sim->Sim()->phase == dm::Phase::Molten && Ctx.Sim->Sim()->heat > 0.9 && Guard++ < 200) Ctx.Sim->StepTicks(1); }
		Ctx.Sim->SetPaused(true);
		Ctx.BuildScene();
		UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: genesis: scene built, heat %.2f"), Ctx.Sim->Sim() ? Ctx.Sim->Sim()->heat : -1.0);
		const dm::World* W = Ctx.Sim->Sim();
		if (!W) { Ctx.SetExitCode(2); Ctx.Finish(); return; }
		// the site where life will start (decided on the final seas: the molten views look at the same place)
		const int32 Site0 = dm::civ::bestSite(*W);
		double Lat = 0, Lon = 0;
		if (W->valid(Site0)) CellLatLon(*W, Site0, Lat, Lon);
		double T[2];
		if (FDmShotArgs::List(TEXT("-gtarget="), T, 2) == 2) { Lat = T[0]; Lon = T[1]; }
		TArray<FView> V;
		FView A; A.Lat = Lat; A.Lon = Lon; A.SunElev = 40; A.SunAz = 120; A.Heading = 0;
		A.Name = TEXT("molten-orbit"); A.DistKm = 700; A.Pitch = 90; A.Fov = 36; V.Add(A);
		A.Before = nullptr;
		A.Name = TEXT("molten-50km"); A.DistKm = 50; A.Pitch = 40; A.Fov = 50; V.Add(A);
		A.Name = TEXT("molten-5km"); A.DistKm = 5; A.Pitch = 22; A.Fov = 55; V.Add(A);
		A.Name = TEXT("cooling-orbit"); A.DistKm = 700; A.Pitch = 90; A.Fov = 36; A.Before = [](FDmScenarioContext& Cx) { GenStepTo(Cx, 0.35); }; V.Add(A);
		A.Name = TEXT("seas-orbit"); A.Before = [](FDmScenarioContext& Cx) { GenFillSeas(Cx); }; V.Add(A);
		A.Before = nullptr;
		const int32 Steps[3] = { 16, 24, 32 };   // 2, 5, 9 s of 1x play in all
		const TCHAR* Names[3] = { TEXT("life-2s"), TEXT("life-5s"), TEXT("life-9s") };
		for (int32 I = 0; I < 3; ++I)
		{
			FView L = A; L.Name = Names[I]; L.DistKm = I == 1 ? 140 : 700; L.Pitch = I == 1 ? 60 : 90; L.Fov = I == 1 ? 45 : 36;
			L.During = [](FDmScenarioContext& Cx, int32 F) { GenLife(Cx, F); };
			L.DuringFrames = Steps[I]; L.Hold = Steps[I] + 90;
			// look at the site the seeds go to (known once the seas are full)
			L.Before = [](FDmScenarioContext& Cx)
			{
				GenFillSeas(Cx);
				const dm::World* Wl = Cx.Sim->Sim();
				if (Wl && Wl->valid(GGen.Site) && GRun.Cur >= 0) { double La, Lo; CellLatLon(*Wl, GGen.Site, La, Lo); GRun.Views[GRun.Cur].Lat = La; GRun.Views[GRun.Cur].Lon = Lo; }
			};
			V.Add(L);
		}
		StartRun(Ctx, MoveTemp(V));
	},
	[](FDmScenarioContext& Ctx, float Dt) { TickRun(Ctx, Dt); });
