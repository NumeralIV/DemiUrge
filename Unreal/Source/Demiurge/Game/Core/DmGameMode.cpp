// DEMIURGE — Game/Core/DmGameMode.cpp : see DmGameMode.h. Owner: C0 bridge.

#include "Game/Core/DmGameMode.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmPlayerController.h"
#include "Game/Core/DmSimSubsystem.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "UnrealClient.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmMode, Log, All);

namespace
{
	// The engine's per-frame unit timers (what "stat unit" shows) live in RenderCore and RHI, which
	// Demiurge.Build.cs does not link (the file is not C0's; adding "RenderCore", "RHI" there is
	// requested in the bridge report). Until then they are looked up in the loaded editor DLLs.
	// The engine's per-frame unit timers (what "stat unit" shows), from RenderCore and RHI (Demiurge.Build.cs lists them
	// since the integration; this works in the editor and in the packaged game alike)
	struct FUnitTimers
	{
		const uint32* Game = &GGameThreadTime;
		const uint32* Render = &GRenderThreadTime;
		const uint32* Rhi = &GRHIThreadTime;
		uint32 (*GpuCycles)(uint32) = &RHIGetGPUFrameCycles;
		bool bInit = false;

		void Init()
		{
			bInit = true;
			UE_LOG(LogDmMode, Display, TEXT("DmStats: unit timers game=%d render=%d rhi=%d gpu=%d"), Game != nullptr, Render != nullptr, Rhi != nullptr, GpuCycles != nullptr);
		}
		static double Ms(const uint32* P) { return P ? FPlatformTime::ToMilliseconds(*P) : 0.0; }
	};
	FUnitTimers GUnit;
}

ADmGameMode::ADmGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ADmPlayerController::StaticClass();
}

FString ADmGameMode::ShotBase() const
{
	const FDmShotArgs& A = FDmShotArgs::Get();
	if (!A.Shot.IsEmpty()) return A.Shot;
	// <root>/shots next to the project's ancestors (lanes live three levels below the root)
	FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	for (int32 Up = 0; Up < 6; ++Up)
	{
		if (FPaths::DirectoryExists(FPaths::Combine(Dir, TEXT("shots"))) && FPaths::DirectoryExists(FPaths::Combine(Dir, TEXT("docs"))))
			return FPaths::Combine(Dir, TEXT("shots"), TEXT("ue-") + (Scenario ? Scenario->Name : FString(TEXT("play"))) + TEXT(".png"));
		const FString Parent = FPaths::GetPath(Dir.EndsWith(TEXT("/")) ? Dir.LeftChop(1) : Dir);
		if (Parent.IsEmpty() || Parent == Dir) break;
		Dir = Parent;
	}
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("ue-shot.png"));
}

void ADmGameMode::SetupWorld(UDmSimSubsystem* Sim)
{
	const FDmShotArgs& A = FDmShotArgs::Get();
	if (A.bSeed)
	{
		Sim->CreateWorld(A.Seed, FString(), A.bSkip, 0.0);
	}
	else if (!A.bFixtureArg && (A.Scenario.IsEmpty() || A.Scenario.Equals(TEXT("play"), ESearchCase::IgnoreCase) || A.Scenario.StartsWith(TEXT("playthrough")) || A.Scenario.StartsWith(TEXT("flow"))))
	{
		// (integration) the game itself runs the real simulation: the flow makes the title's world (no fixture)
	}
	else if (!A.Fixture.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		FDmFixtureOptions O;
		if (A.FixtureAge >= 0) O.Age = FMath::Clamp(A.FixtureAge, 0, 6);
		if (A.FixtureTowns > 0) O.Settlements = A.FixtureTowns;
		if (A.FixtureSeed >= 0) O.Seed = uint32(A.FixtureSeed);
		if (A.bYear) O.Year = A.Year;
		Sim->LoadFixture(A.Fixture, O, A.bLive);
	}
	if (A.bYear && Sim->SimMutable()) Sim->SimMutable()->year = A.Year;
	Sim->SetSpeed(A.Speed);
}

void ADmGameMode::StartPlay()
{
	Super::StartPlay();
	UWorld* W = GetWorld();
	const FDmShotArgs& A = FDmShotArgs::Get();
	GEngine->Exec(W, *FString::Printf(TEXT("r.SetRes %sw"), *A.Res));

	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	if (!Sim) { UE_LOG(LogDmMode, Error, TEXT("DmMode: no UDmSimSubsystem")); return; }
	SetupWorld(Sim);

	Camera = W->SpawnActor<ACameraActor>(FVector(8.0e7, 0, 0), FRotator(0, 180, 0));
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	Camera->GetCameraComponent()->SetFieldOfView(30.f);

	const FString Name = A.Scenario.IsEmpty() ? FString(TEXT("play")) : A.Scenario;
	Scenario = FDmScenarios::Find(Name);
	UE_LOG(LogDmMode, Display, TEXT("DmMode: scenarios [%s]; parts [%s]"), *FString::Join(FDmScenarios::Names(), TEXT(", ")), *FDmScene::Describe());
	if (!Scenario)
	{
		UE_LOG(LogDmMode, Error, TEXT("DmScenario: unknown scenario \"%s\" (known: %s)"), *Name, *FString::Join(FDmScenarios::Names(), TEXT(", ")));
		ExitCode = 3;
		Quit(TEXT("unknown scenario"));
		return;
	}
	if (!Scenario->Name.Equals(Name, ESearchCase::IgnoreCase))
		UE_LOG(LogDmMode, Display, TEXT("DmScenario: \"%s\" runs scenario \"%s\""), *Name, *Scenario->Name);

	Ctx.World = W;
	Ctx.Sim = Sim;
	Ctx.Mode = this;
	Ctx.PC = W->GetFirstPlayerController();
	UE_LOG(LogDmMode, Display, TEXT("DmScenario: %s (%s) setup"), *Scenario->Name, *Scenario->Area);
	if (Scenario->Setup) Scenario->Setup(Ctx);
	for (AActor* Act : SceneActors) SceneActorRefs.Add(Act);

	// command-line overrides
	if (A.bCam) HarnessLookAt(A.Cam[0], A.Cam[1], A.Cam[2], A.Cam[3], A.Cam[4], A.Cam[5]);
	if (A.bSun) Sim->PinSunAt(A.Sun[0], A.Sun[1]);
	if (!bViewSet && bHarnessView) HarnessLookAt(20.0, 30.0, 700.0, 0.0, 90.0, 30.0);

	if (!A.Shot.IsEmpty() || !A.Seq.IsEmpty())
	{
		ShotsExpected = A.Seq.Num();
		if (!A.Shot.IsEmpty() && !A.Seq.Contains(A.Warm)) ++ShotsExpected;
	}
}

void ADmGameMode::HarnessLookAt(double LatDeg, double LonDeg, double DistKm, double HeadingDeg, double PitchDeg, double FovDeg)
{
	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const FTransform P2W = Sim ? Sim->GetPlanetToWorld() : FTransform::Identity;
	const FVector Up = DmCoords::LatLonToDir(LatDeg, LonDeg);
	double SurfH = 0.0;
	if (Sim && Sim->Sim() && Sim->Sim()->geo)
	{
		const dm::Vec3 S = DmCoords::UEToSim(Up);
		SurfH = DmCoords::SurfaceHeight(*Sim->Sim(), Sim->Sim()->geo->cellAt(S.x, S.y, S.z));
	}
	const FVector Target = Up * DmCoords::HeightToRadiusUU(SurfH);
	FVector East, North;
	DmCoords::Basis(Up, East, North);
	const double H = FMath::DegreesToRadians(HeadingDeg), Pt = FMath::DegreesToRadians(FMath::Clamp(PitchDeg, -89.9, 90.0));
	const FVector Horizontal = North * FMath::Cos(H) + East * FMath::Sin(H);
	const FVector Forward = (Horizontal * FMath::Cos(Pt) - Up * FMath::Sin(Pt)).GetSafeNormal();
	const FVector Loc = Target - Forward * DistKm * DmCoords::UUPerKm;
	// keep "north up" on screen when looking straight down
	const FVector CamUp = (FMath::Abs(PitchDeg) > 89.0) ? Horizontal : Up;
	const FRotator Rot = FRotationMatrix::MakeFromXZ(P2W.TransformVectorNoScale(Forward), P2W.TransformVectorNoScale(CamUp)).Rotator();
	HarnessSetView(P2W.TransformPosition(Loc), Rot, FovDeg);
	UE_LOG(LogDmMode, Display, TEXT("DmScenario: camera at lat %.2f lon %.2f, %.1f km, heading %.0f, pitch %.0f, fov %.0f"), LatDeg, LonDeg, DistKm, HeadingDeg, PitchDeg, FovDeg);
}

void ADmGameMode::HarnessSetView(const FVector& Loc, const FRotator& Rot, double FovDeg)
{
	if (!Camera) return;
	Camera->SetActorLocationAndRotation(Loc, Rot);
	Camera->GetCameraComponent()->SetFieldOfView(float(FMath::Clamp(FovDeg, 1.0, 170.0)));
	bViewSet = true;
	bHarnessView = true;
}

void ADmGameMode::HarnessShot(const FString& Suffix)
{
	const FString Base = ShotBase();
	const FString Path = Suffix.IsEmpty() ? Base : FPaths::Combine(FPaths::GetPath(Base), FPaths::GetBaseFilename(Base) + TEXT("-") + Suffix + TEXT(".png"));
	++ShotsExpected;
	QueueShot(Path);
}

void ADmGameMode::QueueShot(const FString& Path, bool bWithUI)
{
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	IFileManager::Get().Delete(*Path, false, true, true);
	ShotQueue.Add(Path);
	if (bWithUI) UiShots.Add(Path);
}

void ADmGameMode::HarnessShotPath(const FString& Path, bool bWithUI)
{
	++ShotsExpected;
	QueueShot(Path, bWithUI);
}

FString ADmGameMode::ShotsDir() const
{
	return FPaths::GetPath(ShotBase());
}

void ADmGameMode::PumpShots()
{
	// at most one request in flight (FScreenshotRequest is a single global)
	for (int32 I = InFlight.Num() - 1; I >= 0; --I)
	{
		FPendingShot& P = InFlight[I];
		if (IFileManager::Get().FileSize(*P.Path) > 0)
		{
			UE_LOG(LogDmMode, Display, TEXT("DmShot %s (frame %d)"), *P.Path, P.RequestedFrame);
			++ShotsTaken;
			InFlight.RemoveAt(I);
		}
		else if (Frame - P.RequestedFrame > 600)
		{
			UE_LOG(LogDmMode, Error, TEXT("DmShot FAILED %s (not written after 600 frames)"), *P.Path);
			++ShotsTaken;
			InFlight.RemoveAt(I);
		}
	}
	if (InFlight.Num() == 0 && ShotQueue.Num() > 0 && !FScreenshotRequest::IsScreenshotRequested())
	{
		if (!bStatsLogged) LogStats();
		const FString Path = ShotQueue[0];
		ShotQueue.RemoveAt(0);
		FScreenshotRequest::RequestScreenshot(Path, UiShots.Contains(Path), false);
		UiShots.Remove(Path);
		InFlight.Add({ Path, Frame });
	}
}

void ADmGameMode::GetLastUnitMs(double& OutGame, double& OutRender, double& OutRhi, double& OutGpu) const
{
	OutGame = GameMs.Num() ? GameMs.Last() : 0.0;
	OutRender = RenderMs.Num() ? RenderMs.Last() : 0.0;
	OutRhi = RhiMs.Num() ? RhiMs.Last() : 0.0;
	OutGpu = GpuMs.Num() ? GpuMs.Last() : 0.0;
}

void ADmGameMode::LogStats()
{
	bStatsLogged = true;
	auto Avg = [](const TArray<float>& V) { double S = 0; for (float X : V) S += X; return V.Num() ? S / V.Num() : 0.0; };
	auto Mx = [](const TArray<float>& V) { float M = 0; for (float X : V) M = FMath::Max(M, X); return double(M); };
	const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	UE_LOG(LogDmMode, Display, TEXT("DmStats frame %.2f ms (max %.2f, %.0f fps) game %.2f render %.2f rhi %.2f gpu %.2f ms (max %.2f) over %d frames | sim %.3f ms/tick, %lld ticks, %lld dropped, %lld events"),
		Avg(FrameMs), Mx(FrameMs), Avg(FrameMs) > 0 ? 1000.0 / Avg(FrameMs) : 0.0, Avg(GameMs), Avg(RenderMs), Avg(RhiMs), Avg(GpuMs), Mx(GpuMs), FrameMs.Num(),
		Sim ? Sim->GetSimMsPerTick() : 0.0, Sim ? Sim->GetTicksRun() : 0ll, Sim ? Sim->GetTicksDropped() : 0ll, Sim ? Sim->GetEventsRouted() : 0ll);
}

void ADmGameMode::Quit(const TCHAR* Why)
{
	if (bQuitting) return;
	bQuitting = true;
	UE_LOG(LogDmMode, Display, TEXT("DmMode: exit (%s) at frame %d, code %d"), Why, Frame, ExitCode);
	FPlatformMisc::RequestExitWithStatus(false, uint8(FMath::Clamp(ExitCode, 0, 255)), TEXT("DmShot"));
}

void ADmGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	++Frame;
	const FDmShotArgs& A = FDmShotArgs::Get();

	if (Scenario)
	{
		Ctx.Frame = Frame;
		Ctx.Time += DeltaSeconds;
		if (!Ctx.PC) Ctx.PC = GetWorld()->GetFirstPlayerController();
		if (Scenario->Tick) Scenario->Tick(Ctx, DeltaSeconds);
	}
	if (bHarnessView && Camera)
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
			if (PC->GetViewTarget() != Camera) PC->SetViewTarget(Camera);

	// statistics: keep the last 60 frames
	auto Push = [](TArray<float>& V, double X) { if (V.Num() >= 60) V.RemoveAt(0); V.Add(float(X)); };
	Push(FrameMs, FApp::GetDeltaTime() * 1000.0);
	if (!GUnit.bInit) GUnit.Init();
	Push(GameMs, FUnitTimers::Ms(GUnit.Game));
	Push(RenderMs, FUnitTimers::Ms(GUnit.Render));
	Push(RhiMs, FUnitTimers::Ms(GUnit.Rhi));
	Push(GpuMs, GUnit.GpuCycles ? FPlatformTime::ToMilliseconds(GUnit.GpuCycles(0)) : 0.0);

	if (A.Seq.Contains(Frame)) QueueShot(FPaths::Combine(FPaths::GetPath(ShotBase()), FPaths::GetBaseFilename(ShotBase()) + FString::Printf(TEXT("-f%d.png"), Frame)));
	// -shotui: the main shot keeps the interface (the title screen of a packaged build, the HUD)
	if (!A.Shot.IsEmpty() && Frame == A.Warm && !A.Seq.Contains(A.Warm)) QueueShot(ShotBase(), FDmShotArgs::Param(TEXT("shotui")));
	PumpShots();

	if (Frame == A.Warm && !bStatsLogged) LogStats();
	if (!bQuitting)
	{
		const bool bShotsDone = ShotsExpected > 0 && ShotsTaken >= ShotsExpected && ShotQueue.Num() == 0 && InFlight.Num() == 0;
		const int32 LastFrame = FMath::Max(A.Warm, A.Seq.Num() ? A.Seq.Last() : 0);
		if (bShotsDone && Frame >= LastFrame && !bHoldExit)
		{
			if (QuitFrame < 0) QuitFrame = Frame + 5;
			if (Frame >= QuitFrame) Quit(TEXT("shots written"));
		}
		if (bFinishRequested && ShotQueue.Num() == 0 && InFlight.Num() == 0 && (ShotsExpected == 0 || bShotsDone)) Quit(TEXT("scenario finished"));
		if (A.ExitAfter > 0 && Frame >= A.ExitAfter) Quit(TEXT("-exitafter"));
	}
}
