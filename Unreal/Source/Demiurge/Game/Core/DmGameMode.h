// DEMIURGE — Game/Core/DmGameMode.h : the game mode (replaces the skeleton's) and the shot harness.
// Owner: C0 bridge.
//
// StartPlay: parse FDmShotArgs, make the world (fixture by default, -seed= for a real sim world),
// spawn the harness camera, run the scenario's Setup (default "play"), apply -cam / -sun. Tick: run
// the scenario's Tick, keep the harness camera as the view target (unless the scenario handed the
// view to a pawn), collect frame statistics, take the screenshots (-shot at -warm, -seq, Ctx.Shot)
// one per frame, and exit when they are on disk. DefaultPawnClass is none: the camera module (C3)
// registers a "camera" part (EDmPartKind::Control) that spawns and possesses its pawn.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Game/Core/DmScenario.h"
#include "DmGameMode.generated.h"

class ACameraActor;
class UDmSimSubsystem;

UCLASS()
class DEMIURGE_API ADmGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADmGameMode();
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// harness services (FDmScenarioContext forwards here)
	void HarnessLookAt(double LatDeg, double LonDeg, double DistKm, double HeadingDeg, double PitchDeg, double FovDeg);
	void HarnessSetView(const FVector& Loc, const FRotator& Rot, double FovDeg);
	void HarnessUseCamera(bool bUse) { bHarnessView = bUse; }
	void HarnessShot(const FString& Suffix);
	void HarnessShotPath(const FString& Path, bool bWithUI);
	/** <root>/shots (the folder the harness writes into). */
	FString ShotsDir() const;
	void HarnessFinish() { bFinishRequested = true; }
	void HarnessHoldExit() { bHoldExit = true; }
	void HarnessSetExitCode(int32 Code) { ExitCode = Code; }
	ACameraActor* GetHarnessCamera() const { return Camera; }
	const TArray<AActor*>& GetSceneActors() const { return SceneActors; }
	void AddSceneActors(const TArray<AActor*>& Actors) { SceneActors.Append(Actors); }
	/** Path of the main shot (-shot) or shots/ue-<scenario>.png when none was given. */
	FString ShotBase() const;
	/** (integration) The last frame's engine unit timers in ms (what "stat unit" shows): game, render, RHI, GPU. */
	void GetLastUnitMs(double& OutGame, double& OutRender, double& OutRhi, double& OutGpu) const;

private:
	void SetupWorld(UDmSimSubsystem* Sim);
	void QueueShot(const FString& Path, bool bWithUI = false);
	void PumpShots();
	void LogStats();
	void Quit(const TCHAR* Why);

	UPROPERTY() TObjectPtr<ACameraActor> Camera;
	UPROPERTY() TArray<TObjectPtr<AActor>> SceneActorRefs;
	TArray<AActor*> SceneActors;

	const FDmScenario* Scenario = nullptr;
	FDmScenarioContext Ctx;
	bool bHarnessView = true;
	bool bViewSet = false;
	bool bFinishRequested = false;
	bool bHoldExit = false;
	int32 ExitCode = 0;
	bool bQuitting = false;
	int32 Frame = 0;
	int32 QuitFrame = -1;

	struct FPendingShot { FString Path; int32 RequestedFrame = -1; };
	TArray<FString> ShotQueue;
	TSet<FString> UiShots;   // queued paths taken with the interface
	TArray<FPendingShot> InFlight;
	int32 ShotsTaken = 0, ShotsExpected = 0;
	bool bStatsLogged = false;

	// frame statistics (ring of the last 60 frames)
	TArray<float> FrameMs, GameMs, RenderMs, GpuMs, RhiMs;
};
