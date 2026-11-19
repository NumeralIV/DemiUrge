#include "LabGameMode.h"
#include "LabPlanet.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/PostProcessVolume.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "Engine/Engine.h"

namespace
{
	// Atmosphere and clouds are Earth's, with every height scaled by S (and coefficients by 1/S),
	// so a 100 km planet keeps Earth-like colours with a thicker, more visible rim.
	constexpr double PlanetKm = 100.0;
	constexpr double S = 1.0 / 6.15;

	void ParseList(const TCHAR* Key, TArray<double>& Out)
	{
		FString V;
		if (!FParse::Value(FCommandLine::Get(), Key, V)) return;
		TArray<FString> Parts;
		V.ParseIntoArray(Parts, TEXT(","));
		for (int32 I = 0; I < Parts.Num() && I < Out.Num(); ++I) Out[I] = FCString::Atod(*Parts[I]);
	}
}

ALabGameMode::ALabGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = nullptr;
}

void ALabGameMode::StartPlay()
{
	Super::StartPlay();
	UWorld* W = GetWorld();
	const TCHAR* Cmd = FCommandLine::Get();

	FString DataPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../data/planet_7.lab"));
	FParse::Value(Cmd, TEXT("-planet="), DataPath);
	FParse::Value(Cmd, TEXT("-shot="), ShotPath);
	FParse::Value(Cmd, TEXT("-warm="), WarmFrames);
	FParse::Value(Cmd, TEXT("-spin="), SpinDegPerSec);
	{ TArray<double> C = { CamDistR, CamYaw, CamPitch, CamFov }; ParseList(TEXT("-cam="), C); CamDistR = C[0]; CamYaw = C[1]; CamPitch = C[2]; CamFov = C[3]; }
	{ TArray<double> L = { SunYaw, SunPitch }; ParseList(TEXT("-sun="), L); SunYaw = L[0]; SunPitch = L[1]; }
	double Ev = 0.0; FParse::Value(Cmd, TEXT("-ev="), Ev);
	double SunLux = 10.0; FParse::Value(Cmd, TEXT("-lux="), SunLux);
	double KmPerElev = 0.2; FParse::Value(Cmd, TEXT("-relief="), KmPerElev);
	int32 Subdiv = 4; FParse::Value(Cmd, TEXT("-subdiv="), Subdiv);
	const bool bClouds = !FParse::Param(Cmd, TEXT("noclouds"));

	{
		FString Res = TEXT("1920x1080");
		FParse::Value(Cmd, TEXT("-res="), Res);
		GEngine->Exec(W, *FString::Printf(TEXT("r.SetRes %sw"), *Res));
	}

	// Planet
	Planet = W->SpawnActor<ALabPlanet>(FVector::ZeroVector, FRotator::ZeroRotator);
	Planet->RadiusUU = PlanetKm * 1.0e5;
	Planet->KmPerElevUnit = KmPerElev;
	Planet->Subdiv = Subdiv;
	UMaterialInterface* LandMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lab/M_PlanetLand.M_PlanetLand"));
	UMaterialInterface* OceanMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lab/M_Ocean.M_Ocean"));
	Planet->Build(DataPath, LandMat, OceanMat);

	// Sun
	Sun = W->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
	{
		L->SetMobility(EComponentMobility::Movable);
		L->SetIntensity(SunLux);
		L->SetAtmosphereSunLight(true);
		L->SetCastShadows(true);
		L->bCastCloudShadows = true;
		L->SetLightSourceAngle(0.53f);
	}
	PlaceSun();

	// Atmosphere centred on the planet
	if (ASkyAtmosphere* SkyA = W->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator))
	{
		USkyAtmosphereComponent* A = SkyA->GetComponent();
		A->TransformMode = ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;
		A->SetBottomRadius(float(Planet->SeaRadiusUU() / 1.0e5));
		A->SetAtmosphereHeight(float(60.0 * S));
		A->SetRayleighExponentialDistribution(float(8.0 * S));
		A->SetRayleighScatteringScale(float(0.0331 / S));
		A->SetMieExponentialDistribution(float(1.2 * S));
		A->SetMieScatteringScale(float(0.003996 / S));
		A->SetMieAbsorptionScale(float(0.000444 / S));
		A->SetOtherAbsorptionScale(float(0.001881 / S));
		A->MarkRenderStateDirty();
	}

	// Volumetric clouds
	if (bClouds)
	{
		if (AVolumetricCloud* CloudA = W->SpawnActor<AVolumetricCloud>(FVector::ZeroVector, FRotator::ZeroRotator))
		{
			UVolumetricCloudComponent* C = CloudA->FindComponentByClass<UVolumetricCloudComponent>();
			if (C)
			{
				C->SetLayerBottomAltitude(float(3.0 * S));
				C->SetLayerHeight(float(8.0 * S));
				C->SetTracingStartMaxDistance(float(PlanetKm * 20.0));
				C->SetTracingMaxDistance(float(PlanetKm * 3.0));
				C->SetPlanetRadius(float(Planet->SeaRadiusUU() / 1.0e5));
				C->SetbUsePerSampleAtmosphericLightTransmittance(true);
				if (UMaterialInterface* CM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lab/M_Clouds.M_Clouds")))
					C->SetMaterial(CM);
				else if (UMaterialInterface* EM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst")))
					C->SetMaterial(EM);
			}
		}
	}

	// Post-processing: manual exposure, gentle bloom, no vignette/grain yet
	if (APostProcessVolume* PP = W->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator))
	{
		PP->bUnbound = true;
		FPostProcessSettings& P = PP->Settings;
		P.bOverride_AutoExposureMethod = true; P.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
		P.bOverride_AutoExposureApplyPhysicalCameraExposure = true; P.AutoExposureApplyPhysicalCameraExposure = false;
		P.bOverride_AutoExposureBias = true; P.AutoExposureBias = float(Ev);
		P.bOverride_BloomIntensity = true; P.BloomIntensity = 0.35f;
		P.bOverride_VignetteIntensity = true; P.VignetteIntensity = 0.25f;
		P.bOverride_MotionBlurAmount = true; P.MotionBlurAmount = 0.f;
	}

	// Camera
	Camera = W->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	Camera->GetCameraComponent()->SetFieldOfView(float(CamFov));
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	PlaceCamera();
}

void ALabGameMode::PlaceSun()
{
	// The light points along its forward vector; aim it from the sun direction towards the planet.
	const FRotator ToSun(SunPitch, SunYaw, 0.0);
	Sun->SetActorRotation((-ToSun.Vector()).Rotation());
}

void ALabGameMode::PlaceCamera()
{
	const double Dist = CamDistR * Planet->RadiusUU;
	const FRotator Dir(CamPitch, CamYaw, 0.0);
	const FVector Pos = Dir.Vector() * Dist;
	Camera->SetActorLocation(Pos);
	Camera->SetActorRotation((-Pos).Rotation());
}

void ALabGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	++Frame;
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		if (PC->GetViewTarget() != Camera) PC->SetViewTarget(Camera);
	if (SpinDegPerSec != 0.0 && Planet) Planet->AddActorWorldRotation(FRotator(0.0, SpinDegPerSec * DeltaSeconds, 0.0));

	if (!ShotPath.IsEmpty())
	{
		if (Frame == WarmFrames)
		{
			FScreenshotRequest::RequestScreenshot(ShotPath, false, false);
			ShotFrame = Frame;
		}
		else if (ShotFrame > 0 && Frame > ShotFrame + 20)
		{
			FPlatformMisc::RequestExit(false, TEXT("LabShot"));
		}
	}
}
