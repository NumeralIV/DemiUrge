// DEMIURGE LifeLab: builds the vegetation/settlement test scene in code and takes an automatic screenshot.
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "LabGameMode.generated.h"

class ALabPlanet;
class ALifeWorld;
class ACameraActor;
class ADirectionalLight;

UCLASS()
class PLANETLAB_API ALabGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ALabGameMode();
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void PlaceCamera();

	UPROPERTY() TObjectPtr<ALifeWorld> World;
	UPROPERTY() TObjectPtr<ACameraActor> Camera;
	UPROPERTY() TObjectPtr<ADirectionalLight> Sun;
	UPROPERTY() TObjectPtr<ADirectionalLight> Moon;

	FString ShotPath;
	int32 Frame = 0, WarmFrames = 240, ShotFrame = -1;
	// timing over the frames before the shot
	double AccFrame = 0, AccGame = 0, AccRender = 0, AccGpu = 0, AccRhi = 0;
	int32 AccN = 0;
	TArray<float> GpuSamples;
};
