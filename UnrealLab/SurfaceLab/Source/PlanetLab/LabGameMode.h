// DEMIURGE surface lab: builds the scene in code, places the camera relative to a surface point,
// settles the LOD, renders a screenshot and logs frame times and triangle counts.
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "LabGameMode.generated.h"

class ASurfacePlanet;
class ACameraActor;
class ADirectionalLight;
class ASkyLight;

UCLASS()
class PLANETLAB_API ALabGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ALabGameMode();
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void PlaceCamera(double DistKm, double Heading, double Pitch);
	void PlaceSun();

	UPROPERTY() TObjectPtr<ASurfacePlanet> Planet;
	UPROPERTY() TObjectPtr<ACameraActor> Camera;
	UPROPERTY() TObjectPtr<ADirectionalLight> Sun;
	UPROPERTY() TObjectPtr<ASkyLight> Sky;

	FString ShotPath;
	int32 Frame = 0, WarmFrames = 240, ShotFrame = -1, ShaderWaitFrames = 0;
	double Fov = 50.0;
	// look target
	double LookLat = 0.0, LookLon = 0.0, LookDistKm = 400.0, LookHeading = 0.0, LookPitch = 90.0;
	FVector TargetDir = FVector::ZeroVector;
	FVector TargetPos = FVector::ZeroVector;
	double SunElev = 35.0, SunAz = 120.0;
	// stats
	TArray<float> FrameMs, GameMs, RenderMs, GpuMs;
	// descent flight
	double FlySeconds = 0.0, FlyT = 0.0;
	int32 FlyShots = 0, FlyShotIdx = 0;
	double FlyStartKm = 400.0, FlyEndKm = 0.6, FlyStartPitch = 90.0;
	int32 FlyHitches = 0; float FlyWorstMs = 0.f;
};
