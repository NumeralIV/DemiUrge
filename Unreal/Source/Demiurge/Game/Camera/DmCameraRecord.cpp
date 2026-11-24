// DEMIURGE — Game/Camera/DmCameraRecord.cpp : wave 4b camera-feel proof (C3 camera, lane camera4b).
//
//   cam-zoomrec   drives the REAL input chain (wheel and keys through APlayerController::InputKey -> the
//                 mapping context -> UDmInputRouter, the pointer through SetVirtualPointer) through every way a
//                 player moves the camera, and records the camera path per frame:
//                   wheel-in     a fast mouse-wheel spin (18 notches in ~0.9 s) from full orbit to the ground,
//                                toward a point off-centre under the pointer
//                   wheel-out    the same spin back out to orbit
//                   pad-in/out   trackpad-style scrolling (0.12 notch every frame for 2.5 s), in and out
//                   key-zoom     R held 2.6 s (keyboard zoom in)
//                   key-pan      W held 2 s, glide, Shift+D 1.5 s, glide (low altitude)
//                   edge-pan     the pointer resting at the right edge, then the top edge
//                   key-out      F held 2.6 s (keyboard zoom out)
//                   peak         over the world's highest peak: wheel down to the ground, W held 4 s across the
//                                mountains at ground level (clearance against the RENDERED planet), wheel out
//                 CSV: Saved/CamPaths/<-camrec=name, default zoomrec>.csv, one row per frame (time, frame ms, phase,
//                 zoom, target, zoom velocity, altitude, clearance over the sim and the rendered ground, eye
//                 position in km (planet frame), tilt, heading, drift of the ground point grabbed under the
//                 pointer at the start of a wheel burst, pan velocity, shot flag).
//                 Log: DmCamRec lines per phase (frames, frame ms avg / p95 / worst excluding shot frames, min
//                 clearance, worst pointer drift, eye pops) and a RESULT PASS / FAIL line.
//                 Switches: -camshots=0 (no screenshots: clean frame times), -camrec=<name>, -edgepan= etc.

#include "Game/Camera/DmPawn.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Game/Input/DmInputRouter.h"
#include "Game/Input/DmControl.h"
#include "Game/Planet/DmPlanet.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#include "Sim/State.h"

namespace
{
	void RecKey(APlayerController* PC, const FKey& K, EInputEvent E)
	{
		if (!PC) return;
		FViewport* VP = (GEngine && GEngine->GameViewport) ? GEngine->GameViewport->Viewport : nullptr;
		const FInputDeviceId Dev = FInputDeviceId::CreateFromInternalId(0);
		FInputKeyEventArgs Args(VP, Dev, K, E, 1.0f, false, FPlatformTime::Cycles64());
		PC->InputKey(Args);
	}
	void RecWheel(APlayerController* PC, float Delta)
	{
		if (!PC) return;
		FViewport* VP = (GEngine && GEngine->GameViewport) ? GEngine->GameViewport->Viewport : nullptr;
		const FInputDeviceId Dev = FInputDeviceId::CreateFromInternalId(0);
		FInputKeyEventArgs Args(VP, Dev, EKeys::MouseWheelAxis, Delta, 1.0f / 60.0f, 1, FPlatformTime::Cycles64());
		PC->InputKey(Args);
	}

	struct FRow
	{
		double T, Ms; int32 Phase; double Zoom, TZoom, ZVel, Alt, ClearSim, ClearRen; FVector Eye; double Tilt, Heading, Drift; FVector2D Pan; bool bShot; double CamUs = 0; int32 Anchor = 0; double AnchorErr = -1;
	};

	struct FPhase
	{
		FString Name;
		double Seconds = 0;     // minimum duration; settle phases also wait for the zoom to rest
		bool bSettle = false;
		TFunction<void(double /*Local*/, int32 /*LocalFrame*/)> Tick;
		TArray<double> ShotZooms;   // shots when the zoom crosses these (in either direction)
		bool bShotEnd = false;
	};

	struct FRec
	{
		APlayerController* PC = nullptr;
		TArray<FPhase> Phases;
		int32 Cur = -1;
		double PhaseT0 = 0; int32 PhaseF0 = 0;
		int32 ShotIdx = 0;
		TArray<FRow> Rows;
		bool bShots = true, bDone = false;
		FString Name = TEXT("zoomrec");
		double FocusLat = 20, FocusLon = 30, PeakLat = 0, PeakLon = 0;
		FVector2D Pointer = FVector2D::ZeroVector;
		// drift: the ground point under the pointer when a wheel burst starts
		bool bDriftRef = false; FVector DriftDir = FVector::ZeroVector; double DriftR = 100;
		double LastZoom = 0;
		int32 SinceShot = 100;
		double FloorUs = 0;
		double PrevDt = 0;
		double Ctx_HorizonY = -1;   // (verifier) horizon row found by aim-horizon
		bool bBreak = false;        // -camset=break
		bool bLimbOnly = false;     // -camset=limb (the limb phases of break only)
	};
	FRec GRec;

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

	/** Set the pointer and remember the ground point under it (drift reference) for a wheel burst. */
	void AimPointer(ADmPawn* P, const FVector2D& Px)
	{
		GRec.Pointer = Px;
		if (P->Router) P->Router->SetVirtualPointer(Px);
		FVector O, D;
		P->Rig().ScreenRay(Px, O, D);
		GRec.bDriftRef = false;
		// (verifier) a convergent march of the rig's ray against the camera's ground (sim surface): step by half the height
		// above the ground, then bisect. The old 4-pass sphere iteration does not converge at grazing angles (the limb, the
		// horizon) and left a reference point above or below the ground: false drift by parallax.
		// When the player view already shows the rig's view (not right after a jump), the game's own pick is the reference:
		// it is exactly the point the zoom anchors on (at a grazing limb, tens of metres between two ground definitions
		// are tens of pixels at 11 km).
		if (const UDmSimSubsystem* S = UDmSimSubsystem::Get(P))
		{
			FVector CamLoc; FRotator CamRot;
			FDmPick Pk;
			if (GRec.PC)
			{
				GRec.PC->GetPlayerViewPoint(CamLoc, CamRot);
				const FVector EyeW = S->GetPlanetToWorld().TransformPosition(O * DmCoords::UUPerKm);
				if (FVector::Dist(CamLoc, EyeW) < 100.0 && S->PickScreen(GRec.PC, Px, Pk))
				{
					const FVector Hit = O + D * (Pk.Distance / DmCoords::UUPerKm);
					GRec.DriftDir = Hit.GetSafeNormal();
					GRec.DriftR = Hit.Size();
					GRec.bDriftRef = true;
					return;
				}
			}
		}
		const double B = FVector::DotProduct(O, D);
		const double Shell = DmCoords::RadiusKm + 12.0;
		double T = 0.0;
		if (O.Size() > Shell)
		{
			const double C = O.SizeSquared() - Shell * Shell, Disc = B * B - C;
			if (Disc < 0) return;
			T = -B - FMath::Sqrt(Disc);
			if (T < 0) return;
		}
		auto Height = [&](double S) { const FVector X = O + D * S; return X.Size() - P->GroundKm(X.GetSafeNormal()); };
		double Prev = T;
		bool bHit = false;
		for (int32 It = 0; It < 20000 && T < 3000.0; ++It)
		{
			const double H = Height(T);
			if (H <= 0.0) { bHit = true; break; }
			if (O.Size() > Shell && (O + D * T).Size() > Shell + 1.0 && FVector::DotProduct(O + D * T, D) > 0.0) break;   // left the shell
			Prev = T;
			T += FMath::Max(0.5 * H, 0.002);
		}
		if (!bHit) return;
		double Lo = Prev, Hi = T;
		for (int32 It = 0; It < 40; ++It) { const double M = 0.5 * (Lo + Hi); (Height(M) > 0.0 ? Lo : Hi) = M; }
		const FVector Hit = O + D * Hi;
		GRec.DriftDir = Hit.GetSafeNormal();
		GRec.DriftR = Hit.Size();
		GRec.bDriftRef = true;
	}

	double PointerDrift(const ADmPawn* P)
	{
		if (!GRec.bDriftRef) return -1;
		FVector2D Px;
		if (!P->Rig().Project(GRec.DriftDir * GRec.DriftR, Px)) return 1e4;
		return FVector2D::Distance(Px, GRec.Pointer);
	}

	void BuildPhases(ADmPawn* P)
	{
		const double W = P->Rig().ViewportW(), H = P->Rig().ViewportH();
		APlayerController* PC = GRec.PC;
		const FVector2D Aim(0.5 * W + 0.2 * H, 0.5 * H - 0.15 * H);   // off-centre, on the planet at zoom 0
		auto Burst = [PC](double Sign, int32 Notches, int32 Every) {
			return [PC, Sign, Notches, Every](double, int32 F) { if (F % Every == 0 && F / Every < Notches) RecWheel(PC, float(Sign)); };
		};
		auto Pad = [PC](double Step, int32 Frames) {
			return [PC, Step, Frames](double, int32 F) { if (F < Frames) RecWheel(PC, float(Step)); };
		};
		auto Hold = [PC](TArray<FKey> Keys, double Seconds) {
			return [PC, Keys, Seconds](double T, int32 F) {
				if (F == 0) for (const FKey& K : Keys) RecKey(PC, K, IE_Pressed);
				static TMap<int32, bool> Released;   // released once per hold (keyed by the phase start frame)
				if (T >= Seconds && !Released.FindRef(GRec.PhaseF0)) { Released.Add(GRec.PhaseF0, true); for (const FKey& K : Keys) RecKey(PC, K, IE_Released); }
			};
		};
		TArray<FPhase>& Ph = GRec.Phases;
		Ph.Add({ TEXT("settle"), 1.0, false, [P, Aim](double, int32 F) { if (F == 0) AimPointer(P, Aim); } });
		{ FPhase X{ TEXT("wheel-in"), 1.0, true, Burst(+1, 18, 3) }; X.ShotZooms = { 0.15, 0.35, 0.55, 0.75, 0.9 }; X.bShotEnd = true; Ph.Add(X); }
		{ FPhase X{ TEXT("hold-ground"), 1.2, false, [](double, int32) {} }; Ph.Add(X); }
		{ FPhase X{ TEXT("wheel-out"), 1.0, true, Burst(-1, 18, 3) }; X.ShotZooms = { 0.75, 0.45, 0.15 }; X.bShotEnd = true; Ph.Add(X); }
		Ph.Add({ TEXT("re-aim"), 0.3, false, [P, Aim](double, int32 F) { if (F == 0) AimPointer(P, Aim); } });
		{ FPhase X{ TEXT("pad-in"), 2.6, true, Pad(+0.12, 150) }; Ph.Add(X); }
		Ph.Add({ TEXT("re-aim2"), 0.3, false, [P, Aim](double, int32 F) { if (F == 0) AimPointer(P, Aim); } });
		{ FPhase X{ TEXT("pad-out"), 2.6, true, Pad(-0.12, 150) }; Ph.Add(X); }
		Ph.Add({ TEXT("centre"), 0.3, false, [P, W, H](double, int32 F) { if (F == 0) { GRec.bDriftRef = false; if (P->Router) P->Router->SetVirtualPointer(FVector2D(0.5 * W, 0.5 * H)); GRec.Pointer = FVector2D(0.5 * W, 0.5 * H); } } });
		{ FPhase X{ TEXT("key-zoom"), 2.6, true, Hold({ EKeys::R }, 1.9) }; X.bShotEnd = true; Ph.Add(X); }
		{ FPhase X{ TEXT("key-pan"), 2.0 + 1.2, false, Hold({ EKeys::W }, 2.0) }; Ph.Add(X); }
		{ FPhase X{ TEXT("key-pan-fast"), 1.5 + 1.2, false, Hold({ EKeys::LeftShift, EKeys::D }, 1.5) }; X.bShotEnd = true; Ph.Add(X); }
		{ FPhase X{ TEXT("edge-right"), 2.0, false, [P, W, H](double, int32 F) { if (F == 0 && P->Router) P->Router->SetVirtualPointer(FVector2D(W - 2.0, 0.5 * H)); } }; Ph.Add(X); }
		{ FPhase X{ TEXT("edge-top"), 1.5, false, [P, W](double, int32 F) { if (F == 0 && P->Router) P->Router->SetVirtualPointer(FVector2D(0.5 * W, 1.0)); } }; X.bShotEnd = true; Ph.Add(X); }
		{ FPhase X{ TEXT("edge-release"), 1.2, false, [P, W, H](double, int32 F) { if (F == 0 && P->Router) P->Router->SetVirtualPointer(FVector2D(0.5 * W, 0.5 * H)); } }; Ph.Add(X); }
		{ FPhase X{ TEXT("key-out"), 2.8, true, Hold({ EKeys::F }, 2.4) }; X.bShotEnd = true; Ph.Add(X); }
		Ph.Add({ TEXT("to-peak"), 1.2, false, [P, W, H](double, int32 F) {
			if (F == 0) { P->JumpLatLon(GRec.PeakLat, GRec.PeakLon, 0.0, 0.0); AimPointer(P, FVector2D(0.5 * W, 0.5 * H)); } } });
		{ FPhase X{ TEXT("peak-in"), 1.0, true, Burst(+1, 18, 3) }; X.bShotEnd = true; Ph.Add(X); }
		{ FPhase X{ TEXT("peak-cross"), 4.0 + 1.0, false, Hold({ EKeys::W }, 4.0) }; X.bShotEnd = true; Ph.Add(X); }
		{ FPhase X{ TEXT("peak-out"), 1.0, true, [P, W, H, PC](double, int32 F) {
			if (F == 0) AimPointer(P, FVector2D(0.5 * W, 0.62 * H));
			if (F % 3 == 0 && F / 3 < 18) RecWheel(PC, -1.f); } }; Ph.Add(X); }
	}

	/** (verifier, wave 4b) First pixel row (from the top, at screen x) whose ray hits the camera's ground; -1 if none. */
	double HorizonRow(ADmPawn* P, double X)
	{
		const FDmCameraRig& R = P->Rig();
		const double H = R.ViewportH();
		for (double Y = 0.0; Y < H; Y += 1.0)
		{
			FVector O, D;
			R.ScreenRay(FVector2D(X, Y), O, D);
			const double Rg = P->GroundKm(O.GetSafeNormal());
			const double B = FVector::DotProduct(O, D), C = O.SizeSquared() - Rg * Rg;
			if (B * B - C >= 0.0 && -B - FMath::Sqrt(B * B - C) > 0.0) return Y;
		}
		return -1.0;
	}

	/** (verifier, wave 4b) -camset=break: adversarial inputs (reversals, hitches, horizon / limb / sky pointers, pan + zoom). */
	void BuildBreakPhases(ADmPawn* P)
	{
		const double W = P->Rig().ViewportW(), H = P->Rig().ViewportH();
		APlayerController* PC = GRec.PC;
		const FVector2D Aim(0.5 * W + 0.2 * H, 0.5 * H - 0.15 * H);
		TArray<FPhase>& Ph = GRec.Phases;
		auto Hitch = [](int32 F) { if (F % 7 == 3) FPlatformProcess::Sleep(0.12f); };   // a 120+ ms frame every 7th
		Ph.Add({ TEXT("settle"), 1.0, false, [P, Aim](double, int32 F) { if (F == 0) AimPointer(P, Aim); } });
		if (!GRec.bLimbOnly)
		{
		// reversal mid-flight: 8 in (every 2nd frame), 5 out, 8 in
		{ FPhase X{ TEXT("wheel-rev"), 1.0, true, [PC](double, int32 F) {
			if (F % 2 == 0 && F < 16) RecWheel(PC, 1.f);
			else if (F % 2 == 0 && F >= 16 && F < 26) RecWheel(PC, -1.f);
			else if (F % 2 == 0 && F >= 26 && F < 42) RecWheel(PC, 1.f); } }; X.bShotEnd = true; Ph.Add(X); }
		Ph.Add({ TEXT("re-aim"), 0.3, false, [P, Aim](double, int32 F) { if (F == 0) AimPointer(P, Aim); } });
		{ FPhase X{ TEXT("wheel-out-hitch"), 1.0, true, [PC, Hitch](double, int32 F) { Hitch(F); if (F % 3 == 0 && F / 3 < 18) RecWheel(PC, -1.f); } }; Ph.Add(X); }
		Ph.Add({ TEXT("re-aim2"), 0.3, false, [P, Aim](double, int32 F) { if (F == 0) AimPointer(P, Aim); } });
		{ FPhase X{ TEXT("wheel-in-hitch"), 1.0, true, [PC, Hitch](double, int32 F) { Hitch(F); if (F % 3 == 0 && F / 3 < 18) RecWheel(PC, 1.f); } }; X.bShotEnd = true; Ph.Add(X); }
		// at the ground: the pointer just below the horizon (a point tens of km away), zoom out then back in
		Ph.Add({ TEXT("aim-horizon"), 0.4, false, [P, W, H](double, int32 F) {
			if (F != 0) return;
			const double Y = HorizonRow(P, 0.5 * W + 0.15 * H);
			GRec.Ctx_HorizonY = Y;
			AimPointer(P, FVector2D(0.5 * W + 0.15 * H, Y < 0 ? 0.3 * H : FMath::Min(Y + 0.03 * H, H - 2.0)));
		} });
		{ FPhase X{ TEXT("hz-out"), 1.0, true, [PC](double, int32 F) { if (F % 3 == 0 && F / 3 < 6) RecWheel(PC, -1.f); } }; X.bShotEnd = true; Ph.Add(X); }
		{ FPhase X{ TEXT("hz-in"), 1.0, true, [PC](double, int32 F) { if (F % 3 == 0 && F / 3 < 8) RecWheel(PC, 1.f); } }; X.bShotEnd = true; Ph.Add(X); }
		// the pointer in the sky: no anchor, the zoom goes about the screen centre
		Ph.Add({ TEXT("aim-sky"), 0.3, false, [P, W, H](double, int32 F) { if (F == 0) { GRec.bDriftRef = false; GRec.Pointer = FVector2D(0.5 * W, 4.0); if (P->Router) P->Router->SetVirtualPointer(GRec.Pointer); } } });
		{ FPhase X{ TEXT("sky-out"), 1.0, true, [PC](double, int32 F) { if (F % 3 == 0 && F / 3 < 5) RecWheel(PC, -1.f); } }; Ph.Add(X); }
		// pan and zoom together (W held, wheel out)
		Ph.Add({ TEXT("centre"), 0.2, false, [P, W, H](double, int32 F) { if (F == 0) { GRec.bDriftRef = false; GRec.Pointer = FVector2D(0.5 * W, 0.5 * H); if (P->Router) P->Router->SetVirtualPointer(GRec.Pointer); } } });
		{ FPhase X{ TEXT("mix-panzoom"), 2.0 + 1.2, false, [PC](double T, int32 F) {
			if (F == 0) RecKey(PC, EKeys::W, IE_Pressed);
			if (F % 4 == 0 && F / 4 < 10) RecWheel(PC, -1.f);
			static int32 Rel = -1; if (T >= 2.0 && Rel != GRec.PhaseF0) { Rel = GRec.PhaseF0; RecKey(PC, EKeys::W, IE_Released); } } }; X.bShotEnd = true; Ph.Add(X); }
		}
		// back to orbit, then zoom toward a point at the planet's limb
		{ FPhase X{ TEXT("to-orbit"), 1.0, true, [PC](double, int32 F) { if (F % 2 == 0 && F / 2 < 20) RecWheel(PC, -1.f); } }; Ph.Add(X); }
		Ph.Add({ TEXT("aim-limb"), 0.3, false, [P, W, H](double, int32 F) {
			if (F != 0) return;
			// the outermost pixel on the centre row (to the right) still on the ground, minus 1 % of the height
			double Xl = 0.5 * W;
			for (double X = W - 1.0; X > 0.5 * W; X -= 1.0)
			{
				FVector O, D; P->Rig().ScreenRay(FVector2D(X, 0.5 * H), O, D);
				const double Rg = P->GroundKm(O.GetSafeNormal());
				const double B = FVector::DotProduct(O, D), C = O.SizeSquared() - Rg * Rg;
				if (B * B - C >= 0.0) { Xl = X; break; }
			}
			AimPointer(P, FVector2D(Xl - 0.01 * H, 0.5 * H));
		} });
		{ FPhase X{ TEXT("wheel-limb"), 1.0, true, [PC](double, int32 F) { if (F % 3 == 0 && F / 3 < 10) RecWheel(PC, 1.f); } }; X.bShotEnd = true; Ph.Add(X); }
		// (verifier) hold where the limb zoom landed: does the ground there fill in (planet streaming), shots at 0.1 / 1.5 / 3 s
		{ FPhase X{ TEXT("limb-hold"), 3.0, false, [P](double T, int32 F) {
			static int32 Done = 0; if (F == 0) Done = 0;
			const double Marks[3] = { 0.1, 1.5, 3.0 };
			while (Done < 3 && T >= Marks[Done]) { ++Done; if (ADmPlanet* Pl = ADmPlanet::Find(P)) 
			{
				UE_LOG(LogTemp, Display, TEXT("DmCamRec limb-hold %.1f s: %s"), T, *Pl->StatsLine());
				const FDmCameraView& V = P->Rig().View();
				FVector CamLoc(0); FRotator CamRot(0);
				if (GRec.PC && GRec.PC->PlayerCameraManager) { CamLoc = GRec.PC->PlayerCameraManager->GetCameraLocation(); CamRot = GRec.PC->PlayerCameraManager->GetCameraRotation(); }
				const UDmSimSubsystem* Ss = UDmSimSubsystem::Get(P); const FTransform X = Ss ? Ss->GetPlanetToWorld() : FTransform::Identity;
				const FVector CamP = X.InverseTransformPosition(CamLoc) / DmCoords::UUPerKm;
				const FVector FwdP = X.InverseTransformVectorNoScale(CamRot.Vector());
				UE_LOG(LogTemp, Display, TEXT("DmCamRec limb-hold view: rig eye r %.3f km, down-dot %.3f | camera manager r %.3f km, down-dot %.3f, eye gap %.3f km"),
					V.Eye.Size(), FVector::DotProduct(V.Forward, -V.Eye.GetSafeNormal()), CamP.Size(), FVector::DotProduct(FwdP, -CamP.GetSafeNormal()), FVector::Dist(CamP, V.Eye));
			} }
		} }; X.bShotEnd = true; Ph.Add(X); }
		{ FPhase X{ TEXT("end-out"), 1.0, true, [PC](double, int32 F) { if (F % 2 == 0 && F / 2 < 20) RecWheel(PC, -1.f); } }; Ph.Add(X); }
	}

	void StartPhase(FDmScenarioContext& Ctx, int32 I)
	{
		GRec.Cur = I;
		// phases start on the NEXT scenario tick (local frame 0 there)
		GRec.PhaseT0 = Ctx.Time;
		GRec.PhaseF0 = Ctx.Frame + 1;
		GRec.ShotIdx = 0;
		Ctx.Log(FString::Printf(TEXT("phase %d %s"), I, *GRec.Phases[I].Name));
	}

	void Shot(FDmScenarioContext& Ctx, const FString& Suffix)
	{
		if (!GRec.bShots) return;
		Ctx.Shot(Suffix);
		GRec.SinceShot = 0;
	}

	void Summarise(FDmScenarioContext& Ctx)
	{
		// CSV
		FString Csv = TEXT("t,ms,phase,zoom,tzoom,zvel,alt_km,clear_sim_km,clear_render_km,eye_x,eye_y,eye_z,tilt_deg,heading_deg,drift_px,pan_vx,pan_vy,shot,cam_us,anchor,anchor_err\n");
		for (const FRow& R : GRec.Rows)
			Csv += FString::Printf(TEXT("%.4f,%.2f,%s,%.5f,%.5f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.2f,%.2f,%.1f,%.4f,%.4f,%d,%.1f,%d,%.1f\n"), R.T, R.Ms, *GRec.Phases[R.Phase].Name,
				R.Zoom, R.TZoom, R.ZVel, R.Alt, R.ClearSim, R.ClearRen, R.Eye.X, R.Eye.Y, R.Eye.Z, R.Tilt, R.Heading, R.Drift, R.Pan.X, R.Pan.Y, R.bShot ? 1 : 0, R.CamUs, R.Anchor, R.AnchorErr);
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("CamPaths");
		IFileManager::Get().MakeDirectory(*Dir, true);
		const FString Path = FPaths::ConvertRelativePathToFull(Dir / (GRec.Name + TEXT(".csv")));
		FFileHelper::SaveStringToFile(Csv, *Path);
		Ctx.Log(FString::Printf(TEXT("camera path: %d frames -> %s"), GRec.Rows.Num(), *Path));

		bool bPass = true;
		double AllMinRen = 1e9, AllMinSim = 1e9;
		int32 AllPops = 0;
		for (int32 P = 0; P < GRec.Phases.Num(); ++P)
		{
			TArray<double> Ms, Cam;
			double MinRen = 1e9, MinSim = 1e9, Drift = 0;
			int32 Pops = 0, N = 0;
			for (int32 I = 0; I < GRec.Rows.Num(); ++I)
			{
				const FRow& R = GRec.Rows[I];
				if (R.Phase != P) continue;
				++N;
				// frame times without screenshot frames (a shot stalls its frame and the next two)
				bool bNearShot = R.bShot;
				for (int32 K = 1; K <= 3 && I - K >= 0; ++K) bNearShot |= GRec.Rows[I - K].bShot;
				if (!bNearShot) Ms.Add(R.Ms);
				Cam.Add(R.CamUs);
				MinRen = FMath::Min(MinRen, R.ClearRen);
				MinSim = FMath::Min(MinSim, R.ClearSim);
				if (R.Drift >= 0 && (R.Zoom < 0.999 || R.TZoom < 0.999)) Drift = FMath::Max(Drift, R.Drift);
				// an eye pop: this frame's altitude-normalised eye speed is over 3x both neighbours' (and fast)
				if (I >= 2 && I + 1 < GRec.Rows.Num())
				{
					auto Spd = [](const FRow& A, const FRow& B) { return FVector::Dist(A.Eye, B.Eye) / FMath::Max(B.T - A.T, 1e-4) / FMath::Max(B.Alt, 0.05); };
					const double S0 = Spd(GRec.Rows[I - 2], GRec.Rows[I - 1]), S1 = Spd(GRec.Rows[I - 1], R), S2 = Spd(R, GRec.Rows[I + 1]);
					// ... and big enough to see: over 1 % of the altitude in that one frame
					const double Step = FVector::Dist(GRec.Rows[I - 1].Eye, R.Eye) / FMath::Max(R.Alt, 0.05);
					if (S1 > 0.6 && S1 > 3.0 * S0 && S1 > 3.0 * S2 && Step > 0.01 && GRec.Phases[P].Name != TEXT("to-peak")) ++Pops;
				}
			}
			Ms.Sort(); Cam.Sort();
			const double CamP95 = Cam.Num() ? Cam[FMath::Min(Cam.Num() - 1, int32(Cam.Num() * 0.95))] : 0, CamMax = Cam.Num() ? Cam.Last() : 0;
			double Sum = 0; for (double X : Ms) Sum += X;
			const double Avg = Ms.Num() ? Sum / Ms.Num() : 0, P95 = Ms.Num() ? Ms[FMath::Min(Ms.Num() - 1, int32(Ms.Num() * 0.95))] : 0, Worst = Ms.Num() ? Ms.Last() : 0;
			const int32 Over = Ms.FilterByPredicate([](double X) { return X > 16.7; }).Num();
			Ctx.Log(FString::Printf(TEXT("DmCamRec %-13s frames %4d | ms avg %5.1f p95 %5.1f worst %6.1f, %3d over 16.7 | clearance min %.3f km rendered / %.3f km sim | pointer drift max %5.1f px | pops %d | camera cpu p95 %.0f us max %.0f us"),
				*GRec.Phases[P].Name, N, Avg, P95, Worst, Over, MinRen, MinSim, Drift, Pops, CamP95, CamMax));
			AllMinRen = FMath::Min(AllMinRen, MinRen); AllMinSim = FMath::Min(AllMinSim, MinSim); AllPops += Pops;
			if (GRec.Phases[P].Name.StartsWith(TEXT("wheel")) || GRec.Phases[P].Name.StartsWith(TEXT("pad")))
				if (Drift > 24.0) { bPass = false; Ctx.Log(FString::Printf(TEXT("DmCamRec FAIL %s: the ground under the pointer drifted %.1f px"), *GRec.Phases[P].Name, Drift)); }
		}
		if (AllMinRen < 0.1) { bPass = false; Ctx.Log(FString::Printf(TEXT("DmCamRec FAIL: eye within %.3f km of the rendered ground"), AllMinRen)); }
		if (AllPops > 0) { bPass = false; Ctx.Log(FString::Printf(TEXT("DmCamRec FAIL: %d eye pops"), AllPops)); }
		Ctx.Log(FString::Printf(TEXT("DmCamRec floor query %.2f us each; min clearance %.3f km rendered, %.3f km sim; RESULT %s"), GRec.FloorUs, AllMinRen, AllMinSim, bPass ? TEXT("PASS") : TEXT("FAIL")));
		Ctx.SetExitCode(bPass ? 0 : 1);
	}
}

static FDmScenarioRegistrar GDmCamZoomRec(TEXT("cam-zoomrec"), TEXT("camera"),
	TEXT("wheel / trackpad / keys / edge pan through the real input chain, camera path CSV, frame times, clearance (-camshots=0 -camrec=name)"),
	[](FDmScenarioContext& Ctx)
	{
		GRec = FRec();
		int32 Sh = 1;
		if (FDmShotArgs::Value(TEXT("-camshots="), Sh)) GRec.bShots = Sh != 0;
		FDmShotArgs::Value(TEXT("-camrec="), GRec.Name);
		{
			FString Set;
			FDmShotArgs::Value(TEXT("-camset="), Set);
			GRec.bBreak = Set == TEXT("break") || Set == TEXT("limb");
			GRec.bLimbOnly = Set == TEXT("limb");
		}
		Ctx.BuildScene(DmPartsWorld | uint32(EDmPartKind::Control));
		Ctx.UseHarnessCamera(false);
		ADmPawn* P = ADmPawn::Find(Ctx.World);
		if (!P) { Ctx.Log(TEXT("no camera pawn")); return; }
		P->bAdoptHarnessView = false;
		GRec.PC = Ctx.PC ? Ctx.PC : Ctx.World->GetFirstPlayerController();
		const dm::World* W = Ctx.Sim ? Ctx.Sim->Sim() : nullptr;
		SettledCentre(W, GRec.FocusLat, GRec.FocusLon);
		if (W)
		{
			int32 Peak = 0;
			for (int32 C = 0; C < W->n; ++C) if (W->elev[size_t(C)] > W->elev[size_t(Peak)]) Peak = C;
			DmCoords::DirToLatLon(DmCoords::CellDir(*W, Peak), GRec.PeakLat, GRec.PeakLon);
		}
		P->JumpLatLon(GRec.FocusLat, GRec.FocusLon, 0.0, 0.0);
		if (!Ctx.Args().bSun) Ctx.SetSubsolar(FMath::Clamp(GRec.FocusLat * 0.4 + 14.0, 6.0, 23.0), GRec.FocusLon + 40.0);
		Ctx.HoldExit();
		Ctx.Log(FString::Printf(TEXT("cam-zoomrec: focus %.2f,%.2f peak %.2f,%.2f shots %s"), GRec.FocusLat, GRec.FocusLon, GRec.PeakLat, GRec.PeakLon, GRec.bShots ? TEXT("on") : TEXT("off")));
	},
	[](FDmScenarioContext& Ctx, float Dt)
	{
		ADmPawn* P = ADmPawn::Find(Ctx.World);
		if (!P || GRec.bDone) return;
		if (Ctx.Frame < Ctx.Args().Warm) return;
		if (GRec.Cur < 0)
		{
			// the cost of one rendered-ground query (the camera makes a few dozen per frame)
			const double T0 = FPlatformTime::Seconds();
			double Acc = 0;
			for (int32 I = 0; I < 2000; ++I) Acc += P->RenderedGroundKm(DmCoords::LatLonToDir(-60.0 + I * 0.06, I * 0.17));
			GRec.FloorUs = (FPlatformTime::Seconds() - T0) * 1e6 / 2000.0;
			Ctx.Log(FString::Printf(TEXT("rendered-ground query: %.2f us (checksum %.1f)"), GRec.FloorUs, Acc / 2000.0));
			if (GRec.bBreak) BuildBreakPhases(P); else BuildPhases(P);
			StartPhase(Ctx, 0);
			GRec.PhaseF0 = Ctx.Frame;   // the first phase starts now
		}
		FPhase& Ph = GRec.Phases[GRec.Cur];
		const double Local = Ctx.Time - GRec.PhaseT0;
		const int32 LocalF = Ctx.Frame - GRec.PhaseF0;
		if (Ph.Tick) Ph.Tick(Local, LocalF);
		if (LocalF % 30 == 5)
		{
			const UDmControlSubsystem* C = UDmControlSubsystem::Get(Ctx.World);
			FVector2D Ptr(-1, -1);
			const bool bPtr = P->Router && P->Router->GetPointer(Ptr);
			Ctx.Log(FString::Printf(TEXT("  [%s] busy %d grab %d blocked %d text %d ptr %d (%.0f,%.0f) vp %.0fx%.0f edge %.2f,%.2f panv %.3f,%.3f zoom %.3f/%.3f W %d R %d"),
				*Ph.Name, P->Rig().Busy() ? 1 : 0, P->Rig().IsGrabbing() ? 1 : 0, C && C->IsInputBlocked() ? 1 : 0, UDmControlSubsystem::IsTextFieldFocused() ? 1 : 0,
				bPtr ? 1 : 0, Ptr.X, Ptr.Y, P->Rig().ViewportW(), P->Rig().ViewportH(), P->Router ? P->Router->LastEdgePan.X : 0.0, P->Router ? P->Router->LastEdgePan.Y : 0.0,
				P->Rig().GetPanVelocity().X, P->Rig().GetPanVelocity().Y, P->Rig().GetZoom(), P->Rig().GetTargetZoom(),
				GRec.PC && GRec.PC->IsInputKeyDown(EKeys::W) ? 1 : 0, GRec.PC && GRec.PC->IsInputKeyDown(EKeys::R) ? 1 : 0));
		}

		// record (the pawn ticks after this scenario tick in the same frame: rows hold the previous frame's view,
		// consistently, so speeds are exact)
		const FDmCameraRig& R = P->Rig();
		const FDmCameraView& V = R.View();
		const FVector Ed = V.Eye.GetSafeNormal();
		const double Ren = P->RenderedGroundKm(Ed);
		FRow Row;
		// the view read here was made by the pawn in the PREVIOUS frame: time-stamp it with that frame's end and
		// frame time, so eye speeds divide by the time step that produced them
		Row.T = Ctx.Time - Dt; Row.Ms = GRec.PrevDt * 1000.0; Row.Phase = GRec.Cur;
		GRec.PrevDt = Dt;
		Row.Zoom = R.GetZoom(); Row.TZoom = R.GetTargetZoom(); Row.ZVel = R.GetZoomVelocity();
		Row.Alt = V.AltitudeKm; Row.ClearSim = V.Eye.Size() - P->GroundKm(Ed); Row.ClearRen = Ren > 0 ? V.Eye.Size() - Ren : Row.ClearSim;
		Row.Eye = V.Eye; Row.Tilt = FMath::RadiansToDegrees(V.TiltRad); Row.Heading = FMath::RadiansToDegrees(R.HeadingRad());
		Row.Drift = PointerDrift(P); Row.Pan = R.GetPanVelocity(); Row.bShot = false;
		Row.CamUs = P->LastCameraUs;
		{
			FVector2D APx; FVector ADir; double AR;
			// anchor_err: how far the rig's anchor point projects from the pixel it is held under (its own residual)
			FVector2D Now;
			if (R.GetZoomAnchor(APx, ADir, AR)) { Row.Anchor = 1; Row.AnchorErr = R.Project(ADir * AR, Now) ? FVector2D::Distance(APx, Now) : 1e4; }
		}

		// shots when the zoom crosses a mark, and at the end of the phase
		bool bEnd = Local >= Ph.Seconds && (!Ph.bSettle || (FMath::Abs(R.GetZoom() - R.GetTargetZoom()) < 1e-4 && Local > 0.4));
		++GRec.SinceShot;
		for (const double Zm : Ph.ShotZooms)
			if ((GRec.LastZoom < Zm) != (R.GetZoom() < Zm))
			{
				Shot(Ctx, FString::Printf(TEXT("%s-z%03d"), *Ph.Name, FMath::RoundToInt(Zm * 100.0)));
				Row.bShot = GRec.bShots;
			}
		if (bEnd && Ph.bShotEnd) { Shot(Ctx, FString::Printf(TEXT("%s-end"), *Ph.Name)); Row.bShot = GRec.bShots; }
		GRec.LastZoom = R.GetZoom();
		GRec.Rows.Add(Row);

		if (bEnd)
		{
			if (Ph.bShotEnd || Ph.Name == TEXT("wheel-in"))
				Ctx.Log(FString::Printf(TEXT("%s end: zoom %.3f altitude %.3f km tilt %.1f deg clearance %.3f km (rendered)"), *Ph.Name, R.GetZoom(), V.AltitudeKm, Row.Tilt, Row.ClearRen));
			if (GRec.Cur + 1 < GRec.Phases.Num()) StartPhase(Ctx, GRec.Cur + 1);
			else
			{
				GRec.bDone = true;
				if (P->Router) P->Router->ClearVirtualPointer();
				Summarise(Ctx);
				Ctx.Finish();
			}
		}
	});
