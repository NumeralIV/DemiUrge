// DEMIURGE — Game/Camera/DmCameraVerify.cpp : the verifier's adversarial camera tests (C3 camera-input).
//
//   cam-verify    run with -noshot. Harder cases than cam-math, one "DmCamVerify PASS|FAIL <name>: detail" line
//                 each, then "DmCamVerify RESULT ...", exit code 0 / 1:
//                   terrain-ride      key-rate panning at zoom 1 / 0.9 across the most rugged ground: vertical
//                                     bob of the eye (it must not ride every bump of the ground), clearance,
//                                     how often a ridge hides the focus point
//                   zoom-anchor       wheel zoom toward a pointer (centre, near the horizon, a corner) at several
//                                     zooms: the point under the pointer stays near it, no focus jumps
//                   abuse             NaN / infinite / huge inputs, hitch frames, garbage ground answers
//                   fly-peak          fly-to onto the highest peak at zoom 1: clearance on every frame, no jumps
//                   planet-turns      the live pawn while the planet turns (as the sky publishes it, at the
//                                     -spin= multiple of the 40 s day): the rendered camera must follow THIS
//                                     frame's planet transform (no one-frame lag, which would shake the view)

#include "Game/Camera/DmPawn.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmSimSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Engine/EngineBaseTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/PlayerController.h"

#include "Sim/State.h"

#include <limits>

DEFINE_LOG_CATEGORY_STATIC(LogDmCamVerify, Log, All);

namespace
{
	struct FVerify
	{
		int32 Pass = 0, Fail = 0;
		void Check(bool bOk, const TCHAR* Name, const FString& Detail)
		{
			(bOk ? Pass : Fail)++;
			UE_LOG(LogDmCamVerify, Display, TEXT("DmCamVerify %s %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), Name, *Detail);
		}
	};
	FVerify GV;

	/** The cell whose ground varies most within ~4 km (the most rugged place), and the highest cell. */
	void FindRugged(const dm::World& W, int32& OutRugged, int32& OutPeak)
	{
		OutRugged = 0; OutPeak = 0;
		double Best = -1;
		for (int32 C = 0; C < W.n; ++C)
		{
			if (W.elev[size_t(C)] > W.elev[size_t(OutPeak)]) OutPeak = C;
			double Lo = 1e9, Hi = -1e9;
			const int32 Deg = W.geo->nbrN[size_t(C)];
			for (int32 K = 0; K < Deg; ++K)
			{
				const int32 M = W.geo->nbr[size_t(6 * C + K)];
				Lo = FMath::Min(Lo, double(W.elev[size_t(M)]));
				Hi = FMath::Max(Hi, double(W.elev[size_t(M)]));
			}
			if (Hi - Lo > Best && Lo > W.seaLevel) { Best = Hi - Lo; OutRugged = C; }
		}
	}

	/** Does the ground hide the surface point at the focus (what the screen centre aims at) from the eye? */
	bool FocusOccluded(const FDmCameraView& V, FDmGroundFn Ground)
	{
		const FVector Tgt = V.FocusDir * Ground(V.FocusDir);
		for (int32 I = 1; I < 48; ++I)
		{
			const double T = I / 48.0 * 0.97;
			const FVector P = V.Eye + (Tgt - V.Eye) * T;
			const double R = P.Size();
			if (R < Ground(P / FMath::Max(R, 1e-9)) - 1e-3) return true;
		}
		return false;
	}

	void RunVerify(FDmScenarioContext& Ctx)
	{
		const UDmSimSubsystem* Sim = Ctx.Sim;
		const dm::World* W = Sim ? Sim->Sim() : nullptr;
		int32 Hint = -1;
		auto Ground = [Sim, W, &Hint](const FVector& D) -> double
		{
			return W ? Sim->SurfaceRadiusUU(D, Hint) / DmCoords::UUPerKm : 100.0;
		};
		int32 Rugged = -1, Peak = -1;
		if (W && W->geo) FindRugged(*W, Rugged, Peak);

		// ------------------------------------------------------------ terrain ride
		if (W && Rugged >= 0)
		{
			for (const double Z : { 1.0, 0.9 })
			{
				double MaxV = 0, MaxBob = 0, MinClear = 1e9, MaxStep = 0;
				int32 Occluded = 0, Frames = 0;
				for (int32 Hd = 0; Hd < 4; ++Hd)
				{
					FDmCameraRig R;
					R.SetViewport(1920, 1080);
					R.Jump(DmCoords::CellDir(*W, Rugged), Z, Hd * UE_HALF_PI);
					R.Update(1.0 / 60.0, Ground);
					const double Dt = 1.0 / 60.0;
					TArray<double> Rs;
					for (int32 F = 0; F < 480; ++F)
					{
						R.Pan(0.0, 0.6 * Dt);    // W held (KeyPanRate 0.6 screen heights per second)
						R.Update(Dt, Ground);
						const FDmCameraView& V = R.View();
						Rs.Add(V.Eye.Size());
						MinClear = FMath::Min(MinClear, V.Eye.Size() - Ground(V.Eye.GetSafeNormal()));
						Occluded += FocusOccluded(V, Ground) ? 1 : 0;
						++Frames;
					}
					for (int32 F = 1; F < Rs.Num(); ++F)
					{
						MaxV = FMath::Max(MaxV, FMath::Abs(Rs[F] - Rs[F - 1]) / Dt);
						MaxStep = FMath::Max(MaxStep, FMath::Abs(Rs[F] - Rs[F - 1]));
					}
					// bob: the eye radius minus its own 1 s moving average (what reads as the camera bouncing)
					for (int32 F = 30; F + 30 < Rs.Num(); ++F)
					{
						double S = 0;
						for (int32 K = -30; K <= 30; ++K) S += Rs[F + K];
						MaxBob = FMath::Max(MaxBob, FMath::Abs(Rs[F] - S / 61.0));
					}
				}
				const double H = FDmCameraRig().HeightAt(Z);
				GV.Check(MinClear >= 0.3 - 1e-6 && MaxBob < 0.15 * H, Z > 0.95 ? TEXT("terrain-ride-z1") : TEXT("terrain-ride-z09"),
					FString::Printf(TEXT("%d frames panning over the most rugged land at zoom %.1f (height %.2f km): bob %.3f km (limit %.3f), vertical speed up to %.2f km/s, step %.4f km/frame, min clearance %.3f km, focus hidden by a ridge in %d frames (%.1f%%)"),
						Frames, Z, H, MaxBob, 0.15 * H, MaxV, MaxStep, MinClear, Occluded, 100.0 * Occluded / FMath::Max(Frames, 1)));
			}
		}

		// ------------------------------------------------------------ zoom toward the pointer
		{
			double WorstDrift = 0, WorstJumpKm = 0;
			FString Worst;
			const FVector2D Ptrs[3] = { FVector2D(960, 540), FVector2D(960, 260), FVector2D(1700, 950) };
			for (const double Z0 : { 0.2, 0.5, 0.75, 0.9 })
				for (const FVector2D& Px : Ptrs)
				{
					FDmCameraRig R;
					R.SetViewport(1920, 1080);
					R.Jump(DmCoords::LatLonToDir(-24.16, -113.67), Z0, 0.4);
					R.Update(1.0 / 60.0, Ground);
					FVector O, D;
					R.ScreenRay(Px, O, D);
					const double Rg = R.View().Target.Size();
					const double B = FVector::DotProduct(O, D), C = O.SizeSquared() - Rg * Rg;
					if (B * B - C < 0) continue;
					const FVector G = O + D * (-B - FMath::Sqrt(B * B - C));
					const FVector A = G.GetSafeNormal();
					R.ZoomBy(0.06, &A);      // one wheel notch
					double Drift = 0, Jump = 0;
					FVector PrevF = R.GetFocus();
					for (int32 F = 0; F < 90; ++F)
					{
						R.Update(1.0 / 60.0, Ground);
						FVector2D S;
						if (R.Project(G, S)) Drift = FMath::Max(Drift, FVector2D::Distance(S, Px));
						else Drift = 1e4;
						Jump = FMath::Max(Jump, FMath::Acos(FMath::Clamp(FVector::DotProduct(PrevF, R.GetFocus()), -1.0, 1.0)) * Rg);
						PrevF = R.GetFocus();
					}
					if (Drift > WorstDrift) { WorstDrift = Drift; Worst = FString::Printf(TEXT("zoom %.2f pointer (%.0f,%.0f)"), Z0, Px.X, Px.Y); }
					WorstJumpKm = FMath::Max(WorstJumpKm, Jump);
				}
			GV.Check(WorstDrift < 60.0, TEXT("zoom-anchor"),
				FString::Printf(TEXT("one wheel notch toward 3 pointers at 4 zooms: the grabbed ground drifts at most %.1f px from the pointer (worst: %s), largest focus step %.3f km/frame"),
					WorstDrift, *Worst, WorstJumpKm));
		}

		// ------------------------------------------------------------ abuse
		{
			FDmCameraRig R;
			R.SetViewport(1920, 1080);
			R.Jump(DmCoords::LatLonToDir(30, 40), 0.7, 0.0);
			const double Nan = std::numeric_limits<double>::quiet_NaN(), Inf = std::numeric_limits<double>::infinity();
			bool bFinite = true;
			auto Step = [&](double Dt) { R.Update(Dt, Ground); bFinite &= R.IsFinite(); };
			R.Orbit(Nan, 3.0); Step(0.016);
			R.Orbit(1e9, -1e9); Step(0.016);
			R.ZoomBy(Nan); Step(0.016);
			R.ZoomBy(1e12); Step(0.016);
			R.Turn(Nan); Step(0.016);
			R.Turn(1e9); Step(0.016);
			const FVector NanV(Nan, 0, 0);
			R.FlyTo(NanV); Step(0.016);
			R.ZoomBy(-0.3, &NanV); Step(0.016);
			R.Jump(FVector::ZeroVector, 0.5); Step(0.016);
			R.GrabBegin(FVector2D(Nan, 10)); R.GrabMove(FVector2D(1e7, -1e7)); R.GrabMove(FVector2D(Nan, Nan)); R.GrabEnd(); Step(0.016);
			R.Shake(Inf); Step(0.016);
			for (const double Dt : { 0.0, -1.0, Nan, Inf, 5.0, 1e-7 }) Step(Dt);
			R.SetViewport(0, 0); R.SetViewport(Nan, 10); Step(0.016);
			for (int32 F = 0; F < 600; ++F) Step(1.0 / 60.0);
			const bool bZoomOk = R.GetZoom() >= 0.0 && R.GetZoom() <= 1.0;
			// garbage ground answers: NaN, 0, negative, infinite
			int32 BadGround = 0;
			for (int32 K = 0; K < 4; ++K)
			{
				const double Gv = K == 0 ? Nan : (K == 1 ? 0.0 : (K == 2 ? -50.0 : Inf));
				FDmCameraRig Q;
				Q.SetViewport(1920, 1080);
				Q.Jump(DmCoords::LatLonToDir(10, 10), 1.0, 0.0);
				Q.Update(0.016, [Gv](const FVector&) { return Gv; });
				BadGround += Q.IsFinite() ? 0 : 1;
			}
			const double PendingSpin = R.HeadingRad();
			GV.Check(bFinite && bZoomOk && BadGround == 0 && FMath::IsFinite(PendingSpin), TEXT("abuse"),
				FString::Printf(TEXT("NaN / inf / 1e9 inputs, hitch and negative dt, zero viewport, garbage ground: finite %d, zoom %.3f in [0,1], garbage-ground failures %d"),
					bFinite ? 1 : 0, R.GetZoom(), BadGround));
			// a huge turn must not spin the view for seconds
			FDmCameraRig T;
			T.SetViewport(1920, 1080);
			T.Jump(DmCoords::LatLonToDir(0, 0), 0.5, 0.0);
			T.Turn(1e6);
			T.Update(1.0 / 60.0, Ground);
			double Spin = 0, PrevH = T.HeadingRad();
			for (int32 F = 0; F < 60; ++F) { T.Update(1.0 / 60.0, Ground); Spin = FMath::Max(Spin, FMath::Abs(FMath::UnwindRadians(T.HeadingRad() - PrevH))); PrevH = T.HeadingRad(); }
			GV.Check(Spin < 0.6, TEXT("turn-bounded"), FString::Printf(TEXT("Turn(1e6) then 1 s: largest heading step %.3f rad/frame"), Spin));
		}

		// ------------------------------------------------------------ fly onto the highest peak
		if (W && Peak >= 0)
		{
			FDmCameraRig R;
			R.SetViewport(1920, 1080);
			const FVector PeakDir = DmCoords::CellDir(*W, Peak);
			FVector E, N;
			DmCoords::Basis(PeakDir, E, N);
			R.Jump((PeakDir + E * 0.12).GetSafeNormal(), 1.0, 0.0);   // 12 km away, at zoom 1
			R.Update(1.0 / 60.0, Ground);
			R.FlyTo(PeakDir, 1.0, 1.6);
			double MinClear = 1e9, MaxStepRel = 0;
			FVector Prev = R.View().Eye;
			for (int32 F = 0; F < 140; ++F)
			{
				R.Update(1.0 / 60.0, Ground);
				const FDmCameraView& V = R.View();
				MinClear = FMath::Min(MinClear, V.Eye.Size() - Ground(V.Eye.GetSafeNormal()));
				MaxStepRel = FMath::Max(MaxStepRel, FVector::Dist(V.Eye, Prev) / FMath::Max(V.AltitudeKm, 0.1));
				Prev = V.Eye;
			}
			GV.Check(MinClear >= 0.3 - 1e-6 && MaxStepRel < 0.5 && !R.Busy(), TEXT("fly-peak"),
				FString::Printf(TEXT("12 km fly-to onto the highest peak (elev %.2f) at zoom 1: min clearance %.3f km, largest eye step %.3f x altitude per frame, landed %d"),
					W->elev[size_t(Peak)], MinClear, MaxStepRel, R.Busy() ? 0 : 1));
		}
	}

	// ------------------------------------------------------------ planet-turns (live pawn)
	struct FTurnTest
	{
		double Yaw = 0, Spin = 1.0, MaxErrKm = 0, SumErrKm = 0, MaxAlt = 0;
		int32 Samples = 0;
		FDelegateHandle Post;
		bool bOn = false, bLate = false;
	};
	FTurnTest GT;

	void PublishSpin(UDmSimSubsystem* Sim, float Dt)
	{
		if (!Sim) return;
		GT.Yaw -= Dt * 2.0 * UE_DOUBLE_PI / 40.0 * GT.Spin;
		Sim->SetPlanetToWorld(FTransform(FRotator(0.0, FMath::RadiansToDegrees(GT.Yaw), 0.0)));
	}

	/** -late: publish the planet's turn from TG_DuringPhysics, i.e. AFTER a PrePhysics camera would have read it
	 *  (what happens when the sky actor happens to tick after the pawn inside TG_PrePhysics). */
	struct FDmLateSpinTick : public FTickFunction
	{
		UDmSimSubsystem* Sim = nullptr;
		virtual void ExecuteTick(float DeltaTime, ELevelTick, ENamedThreads::Type, const FGraphEventRef&) override { if (GT.bLate) PublishSpin(Sim, DeltaTime); }
		virtual FString DiagnosticMessage() override { return TEXT("DmLateSpinTick"); }
	};
	FDmLateSpinTick GLateTick;
}

static FDmScenarioRegistrar GDmCamVerify(TEXT("cam-verify"), TEXT("camera"),
	TEXT("verifier's adversarial camera tests (run with -noshot): DmCamVerify lines; -spin= planet turn rate in days per 40 s"),
	[](FDmScenarioContext& Ctx)
	{
		GV = FVerify();
		GT = FTurnTest();
		FDmShotArgs::Value(TEXT("-spin="), GT.Spin);
		GT.bLate = FParse::Param(FCommandLine::Get(), TEXT("late"));
		if (GT.bLate && Ctx.World && Ctx.World->PersistentLevel)
		{
			GLateTick.Sim = Ctx.Sim;
			GLateTick.bCanEverTick = true;
			GLateTick.bTickEvenWhenPaused = true;
			GLateTick.TickGroup = TG_DuringPhysics;
			GLateTick.RegisterTickFunction(Ctx.World->PersistentLevel);
		}
		Ctx.BuildScene(DmPartsWorld | uint32(EDmPartKind::Control));
		Ctx.UseHarnessCamera(false);
		Ctx.HoldExit();
		if (ADmPawn* P = ADmPawn::Find(Ctx.World))
		{
			P->bAdoptHarnessView = false;
			P->JumpLatLon(-24.16, -113.67, 1.0, 20.0);
		}
		GT.Post = FWorldDelegates::OnWorldPostActorTick.AddLambda([](UWorld* InWorld, ELevelTick, float)
		{
			if (!GT.bOn || !InWorld || !InWorld->IsGameWorld()) return;
			ADmPawn* P = ADmPawn::Find(InWorld);
			APlayerController* PC = InWorld->GetFirstPlayerController();
			if (!P || !PC || !PC->PlayerCameraManager) return;
			// what will be rendered this frame vs where the rig's eye is on the planet as it stands this frame
			const FVector Rendered = PC->PlayerCameraManager->GetCameraLocation();
			const FVector Want = P->PlanetKmToWorld(P->Rig().View().Eye);
			const double ErrKm = FVector::Dist(Rendered, Want) / DmCoords::UUPerKm;
			GT.MaxErrKm = FMath::Max(GT.MaxErrKm, ErrKm);
			GT.SumErrKm += ErrKm;
			GT.MaxAlt = FMath::Max(GT.MaxAlt, P->Rig().View().AltitudeKm);
			++GT.Samples;
		});
	},
	[](FDmScenarioContext& Ctx, float Dt)
	{
		if (Ctx.Frame == 3) RunVerify(Ctx);
		// the planet turns like the sky publishes it (eastward, one day per 40 s at 1x, times -spin=)
		if (Ctx.Frame >= 5 && Ctx.Sim)
		{
			if (!GT.bLate) PublishSpin(Ctx.Sim, Dt);
			GT.bOn = Ctx.Frame >= 10;
		}
		if (Ctx.Frame == 310)
		{
			GT.bOn = false;
			FWorldDelegates::OnWorldPostActorTick.Remove(GT.Post);
			if (GLateTick.IsTickFunctionRegistered()) GLateTick.UnRegisterTickFunction();
			GT.bLate = false;
			const double Mean = GT.Samples ? GT.SumErrKm / GT.Samples : 0.0;
			GV.Check(GT.Samples > 100 && GT.MaxErrKm < 0.01, TEXT("planet-turns"),
				FString::Printf(TEXT("%d frames at zoom 1 (eye %.2f km up) while the planet turns at %.1fx (%s publisher): rendered camera vs this frame's planet: max %.3f km, mean %.3f km off"),
					GT.Samples, GT.MaxAlt, GT.Spin, GLateTick.Sim ? TEXT("TG_DuringPhysics") : TEXT("TG_PrePhysics"), GT.MaxErrKm, Mean));
			UE_LOG(LogDmCamVerify, Display, TEXT("DmCamVerify RESULT %s %d passed, %d failed"), GV.Fail == 0 ? TEXT("PASS") : TEXT("FAIL"), GV.Pass, GV.Fail);
			Ctx.SetExitCode(GV.Fail == 0 ? 0 : 1);
			Ctx.Finish();
		}
	});
