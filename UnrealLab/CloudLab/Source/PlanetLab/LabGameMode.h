// DEMIURGE CloudLab: the PlanetLab orbit scene plus weather-driven volumetric clouds, stars and a moon.
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CloudWeather.h"
#include "LabGameMode.generated.h"

class ALabPlanet;
class ACameraActor;
class ADirectionalLight;
class UMaterialInstanceDynamic;
class UTexture2D;
class UVolumeTexture;
class UVolumetricCloudComponent;

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
	void SetupClouds();
	void SetupStarsAndMoon();
	void PlaceMoon();

	UPROPERTY() TObjectPtr<ALabPlanet> Planet;
	UPROPERTY() TObjectPtr<ACameraActor> Camera;
	UPROPERTY() TObjectPtr<ADirectionalLight> Sun;
	UPROPERTY() TObjectPtr<ADirectionalLight> MoonLight;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CloudMID;
	UPROPERTY() TObjectPtr<UTexture2D> WeatherTex;
	UPROPERTY() TObjectPtr<UVolumeTexture> ShapeTex;
	UPROPERTY() TObjectPtr<UVolumeTexture> DetailTex;
	UPROPERTY() TObjectPtr<UVolumetricCloudComponent> Cloud;

	FCloudWeather Weather;
	FString ShotPath;
	int32 Frame = 0, WarmFrames = 240, ShotFrame = -1, WaitFrames = 0;
	double CamDistR = 5.0, CamYaw = 20.0, CamPitch = 15.0, CamFov = 30.0;
	double SunYaw = 110.0, SunPitch = 10.0;
	double SpinDegPerSec = 0.0;
	// -pov=lat,lon,altKm,headingDeg,pitchDeg,fov : a camera at a point above the surface
	bool bPov = false;
	double PovLat = 0, PovLon = 0, PovAltKm = 1, PovHeading = 0, PovPitch = -30, PovFov = 60;
	// perf
	double SumDt = 0.0, SumGpu = 0.0, MaxDt = 0.0; int32 NPerf = 0;
	bool bProfileGpu = false;
};
