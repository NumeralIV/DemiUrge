// DEMIURGE — Game/Core/Placeholder/DmPlaceholderSky.cpp : see the header. TEMPORARY (C0 bridge).

#include "Game/Core/Placeholder/DmPlaceholderSky.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmScene.h"
#include "Game/Core/DmSimSubsystem.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"

namespace
{
	constexpr double S = 1.0 / 6.15;   // Earth's atmosphere heights scaled (SPEC-UNREAL 1)
}

ADmPlaceholderSky::ADmPlaceholderSky()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(RootComponent);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetIntensity(10.f);
	Sun->SetAtmosphereSunLight(true);
	Sun->SetCastShadows(true);
	Sun->bCastCloudShadows = true;
	Sun->SetLightSourceAngle(0.53f);

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(RootComponent);
	Atmosphere->TransformMode = ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;

	Clouds = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("Clouds"));
	Clouds->SetupAttachment(RootComponent);

	Post = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Post"));
	Post->SetupAttachment(RootComponent);
	Post->bUnbound = true;
}

void ADmPlaceholderSky::BeginPlay()
{
	Super::BeginPlay();
	double Ev = 0;
	FDmShotArgs::Value(TEXT("-ev="), Ev);
	double Lux = 10.0;
	FDmShotArgs::Value(TEXT("-lux="), Lux);
	Sun->SetIntensity(float(Lux));

	Atmosphere->SetAtmosphereHeight(float(60.0 * S));
	Atmosphere->SetRayleighExponentialDistribution(float(8.0 * S));
	Atmosphere->SetRayleighScatteringScale(float(0.0331 / S));
	Atmosphere->SetMieExponentialDistribution(float(1.2 * S));
	Atmosphere->SetMieScatteringScale(float(0.003996 / S));
	Atmosphere->SetMieAbsorptionScale(float(0.000444 / S));
	Atmosphere->SetOtherAbsorptionScale(float(0.001881 / S));

	Clouds->SetLayerBottomAltitude(float(3.0 * S));
	Clouds->SetLayerHeight(float(8.0 * S));
	Clouds->SetTracingStartMaxDistance(float(DmCoords::RadiusKm * 20.0));
	Clouds->SetTracingMaxDistance(float(DmCoords::RadiusKm * 3.0));
	Clouds->SetbUsePerSampleAtmosphericLightTransmittance(true);
	if (UMaterialInterface* CM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst")))
		Clouds->SetMaterial(CM);
	if (FDmShotArgs::Param(TEXT("noclouds"))) Clouds->SetVisibility(false);

	FPostProcessSettings& P = Post->Settings;
	P.bOverride_AutoExposureMethod = true; P.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	P.bOverride_AutoExposureApplyPhysicalCameraExposure = true; P.AutoExposureApplyPhysicalCameraExposure = false;
	P.bOverride_AutoExposureBias = true; P.AutoExposureBias = float(Ev);
	P.bOverride_BloomIntensity = true; P.BloomIntensity = 0.35f;
	P.bOverride_VignetteIntensity = true; P.VignetteIntensity = 0.25f;
	P.bOverride_MotionBlurAmount = true; P.MotionBlurAmount = 0.f;

	if (UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
		if (!Sim->IsSunPinned()) Sim->SetSunDirection(Sim->GetPlanetToWorld().TransformVectorNoScale(DmCoords::LatLonToDir(12.0, 75.0)));
	Apply();
}

void ADmPlaceholderSky::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Apply();
}

void ADmPlaceholderSky::Apply()
{
	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	if (!Sim) return;
	const FTransform P2W = Sim->GetPlanetToWorld();
	SetActorLocation(P2W.GetLocation());
	const FVector ToSun = Sim->GetSunDirection();
	Sun->SetWorldRotation((-ToSun).Rotation());
	const double Sea = Sim->Sim() ? Sim->Sim()->seaLevel : 0.0;
	if (Sea != LastSeaLevel)
	{
		LastSeaLevel = Sea;
		const float BottomKm = float(DmCoords::HeightToRadiusUU(Sea) / DmCoords::UUPerKm);
		Atmosphere->SetBottomRadius(BottomKm);
		Clouds->SetPlanetRadius(BottomKm);
	}
}

static FDmScenePartRegistrar GDmPlaceholderSkyPart(TEXT("sky"), EDmPartKind::World, -100,
	[](const FDmSceneContext& Ctx) -> AActor*
	{
		return Ctx.World ? Ctx.World->SpawnActor<ADmPlaceholderSky>() : nullptr;
	}, TEXT("Core/Placeholder (lab sky)"));
