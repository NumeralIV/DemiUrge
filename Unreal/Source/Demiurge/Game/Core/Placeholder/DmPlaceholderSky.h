// DEMIURGE — Game/Core/Placeholder/DmPlaceholderSky.h : TEMPORARY sky (C0 bridge).
//
// The lab's sky (UnrealLab/PlanetLab/LabGameMode.cpp): a movable sun (10 lux, atmosphere sun light,
// cloud shadows), Earth's sky atmosphere scaled by S = 1/6.15 in height around the planet centre,
// the engine's simple volumetric clouds on a scaled layer, and an unbound post-process with manual
// exposure (-ev), gentle bloom and vignette. Registered as scene part "sky" at priority -100 (the sky
// module C2 replaces it). The sun follows UDmSimSubsystem::GetSunDirection(); when nobody pinned it,
// this placeholder publishes a fixed subsolar point (lat 12, lon 75).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DmPlaceholderSky.generated.h"

class UDirectionalLightComponent;
class USkyAtmosphereComponent;
class UVolumetricCloudComponent;
class UPostProcessComponent;

UCLASS()
class DEMIURGE_API ADmPlaceholderSky : public AActor
{
	GENERATED_BODY()

public:
	ADmPlaceholderSky();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere) TObjectPtr<UDirectionalLightComponent> Sun;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USkyAtmosphereComponent> Atmosphere;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UVolumetricCloudComponent> Clouds;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPostProcessComponent> Post;

private:
	void Apply();
	double LastSeaLevel = 1e30;
};
