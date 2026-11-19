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
// Options: -views=a,b (only these views), -hold=<frames> per view (default 150), -apscale=<aerial perspective>.
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

#include "Components/DirectionalLightComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
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
	};

	struct FRun
	{
		TArray<FView> Views;
		int32 Cur = -1;
		int32 ViewFrame = 0;
		int32 ShotAt = -1;
		int32 Hold = 150;
		TArray<float> Frames;
		bool bDone = false;
		TWeakObjectPtr<ASkyLight> Sky;
		double MaxFrameAfterChange = 0; int32 ChangeFrames = 0;
	};
	FRun GRun;

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
		if (GRun.ChangeFrames > 0) { GRun.MaxFrameAfterChange = FMath::Max(GRun.MaxFrameAfterChange, FApp::GetDeltaTime() * 1000.0); --GRun.ChangeFrames; }
		ADmPlanet* P = Planet(Ctx);
		if (GRun.ShotAt >= 0)
		{
			// the screenshot is taken at the end of the frame that requested it: keep the view a few frames
			if (GRun.ViewFrame < GRun.ShotAt + 4) { ++GRun.ViewFrame; return; }
			GRun.ShotAt = -1;
		}
		else if (GRun.Cur >= 0 && GRun.ViewFrame >= (GRun.Views[GRun.Cur].Hold > 0 ? GRun.Views[GRun.Cur].Hold : GRun.Hold) && (!P || P->IsSettled() || GRun.ViewFrame > 900))
		{
			double Avg = 0, Mx = 0;
			for (float F : GRun.Frames) { Avg += F; Mx = FMath::Max(Mx, double(F)); }
			Avg /= FMath::Max(GRun.Frames.Num(), 1);
			UE_LOG(LogDmPlanetScn, Display, TEXT("DmPlanetScn: shot %s: frame %.2f ms avg (max %.2f) over %d frames | %s"), *GRun.Views[GRun.Cur].Name, Avg, Mx, GRun.Frames.Num(), P ? *P->StatsLine() : TEXT(""));
			Ctx.Shot(GRun.Views[GRun.Cur].Name);
			GRun.ShotAt = GRun.ViewFrame;
			++GRun.ViewFrame;
			return;
		}
		if (GRun.Cur < 0 || (GRun.ViewFrame > 0 && GRun.ShotAt < 0 && GRun.ViewFrame >= (GRun.Views[GRun.Cur].Hold > 0 ? GRun.Views[GRun.Cur].Hold : GRun.Hold) && (!P || P->IsSettled() || GRun.ViewFrame > 900)))
		{
			++GRun.Cur;
			GRun.ViewFrame = 0;
			if (GRun.Cur >= GRun.Views.Num()) { GRun.bDone = true; Ctx.Finish(); return; }
			if (GRun.Views[GRun.Cur].Before) GRun.Views[GRun.Cur].Before(Ctx);
			ApplyView(Ctx, GRun.Views[GRun.Cur]);
			GRun.Frames.Reset();
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
