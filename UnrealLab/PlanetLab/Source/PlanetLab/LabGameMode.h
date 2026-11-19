// DEMIURGE graphics lab: builds the orbit scene in code and can take an automatic screenshot.
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "LabGameMode.generated.h"

class ALabPlanet;
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
	void PlaceSun();

	UPROPERTY() TObjectPtr<ALabPlanet> Planet;
	UPROPERTY() TObjectPtr<ACameraActor> Camera;
	UPROPERTY() TObjectPtr<ADirectionalLight> Sun;

	FString ShotPath;
	int32 Frame = 0, WarmFrames = 240, ShotFrame = -1;
	double CamDistR = 5.0, CamYaw = 20.0, CamPitch = 15.0, CamFov = 30.0;
	double SunYaw = 110.0, SunPitch = 10.0;
	double SpinDegPerSec = 0.0;
};
