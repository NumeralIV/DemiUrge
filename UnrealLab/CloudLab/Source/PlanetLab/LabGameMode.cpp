#include "LabGameMode.h"
#include "LabPlanet.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/VolumeTexture.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/PostProcessVolume.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Engine/Engine.h"
#include "DynamicRHI.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace
{
	// Atmosphere and clouds are Earth's, with every height scaled by S (and coefficients by 1/S),
	// so a 100 km planet keeps Earth-like colours with a thicker, more visible rim.
	constexpr double PlanetKm = 100.0;
	constexpr double S = 1.0 / 6.15;

	void ParseList(const TCHAR* Key, TArray<double>& Out)
	{
		FString V;
		if (!FParse::Value(FCommandLine::Get(), Key, V, false)) return; // false: do not stop at the commas
		TArray<FString> Parts;
		V.ParseIntoArray(Parts, TEXT(","));
		for (int32 I = 0; I < Parts.Num() && I < Out.Num(); ++I) Out[I] = FCString::Atod(*Parts[I]);
	}

	FVector DirLatLon(double LatDeg, double LonDeg)
	{
		const double La = FMath::DegreesToRadians(LatDeg), Lo = FMath::DegreesToRadians(LonDeg);
		return FVector(FMath::Cos(La) * FMath::Cos(Lo), FMath::Cos(La) * FMath::Sin(Lo), FMath::Sin(La));
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
	bProfileGpu = FParse::Param(Cmd, TEXT("profilegpu"));
	{ TArray<double> C = { CamDistR, CamYaw, CamPitch, CamFov }; ParseList(TEXT("-cam="), C); CamDistR = C[0]; CamYaw = C[1]; CamPitch = C[2]; CamFov = C[3]; }
	{ TArray<double> L = { SunYaw, SunPitch }; ParseList(TEXT("-sun="), L); SunYaw = L[0]; SunPitch = L[1]; }
	{
		TArray<double> V = { PovLat, PovLon, PovAltKm, PovHeading, PovPitch, PovFov };
		FString Tmp;
		if (FParse::Value(Cmd, TEXT("-pov="), Tmp)) { bPov = true; ParseList(TEXT("-pov="), V); }
		PovLat = V[0]; PovLon = V[1]; PovAltKm = V[2]; PovHeading = V[3]; PovPitch = V[4]; PovFov = V[5];
	}
	double Ev = 0.0; FParse::Value(Cmd, TEXT("-ev="), Ev);
	double SunLux = 10.0; FParse::Value(Cmd, TEXT("-lux="), SunLux);
	double KmPerElev = 0.2; FParse::Value(Cmd, TEXT("-relief="), KmPerElev);
	int32 Subdiv = 4; FParse::Value(Cmd, TEXT("-subdiv="), Subdiv);

	{
		FString Res = TEXT("1920x1080");
		FParse::Value(Cmd, TEXT("-res="), Res);
		GEngine->Exec(W, *FString::Printf(TEXT("r.SetRes %sw"), *Res));
		GEngine->Exec(W, TEXT("t.MaxFPS 0"));
		GEngine->Exec(W, TEXT("r.VSync 0"));
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
		L->SetAtmosphereSunLightIndex(0);
		L->SetCastShadows(true);
		L->SetLightSourceAngle(0.53f);
		// Cloud shadows: one orthographic shadow map along the light.
		L->bCastCloudShadows = true;
		// The map is centred on the ground point under the camera and is 2*Extent wide; 1.3 R covers the visible disc.
		// Resolution = 512 * scale, capped by r.VolumetricCloud.ShadowMap.MaxResolution (raised in DefaultEngine.ini).
		// Near the ground use a tight extent: the shadow map's depth range is 4x the extent, and at 130 km the
		// shadows come out blurred AND displaced. Production: update this from camera altitude every frame.
		{
			FString PovS; double AltKm = 1.0e9;
			TArray<double> V = { 0, 0, 1.0e9 };
			if (FParse::Value(Cmd, TEXT("-pov="), PovS)) { ParseList(TEXT("-pov="), V); AltKm = V[2]; }
			// Texel size = 2*Extent/2048, traced at half res (temporal), and stretched by 1/sin(sun elevation) on
			// the ground: at 8 km altitude with a 31 km extent the shadows came out as 200 m rectangles.
			L->CloudShadowExtent = float(FMath::Clamp(AltKm * 1.2 + 8.0, 12.0, PlanetKm * 1.3));
			L->CloudShadowMapResolutionScale = 4.0f; // 2048^2; cost ~2-2.7 ms at 1080p on an RTX 3060
		}
		FParse::Value(Cmd, TEXT("-shadowext="), L->CloudShadowExtent);
		FParse::Value(Cmd, TEXT("-shadowres="), L->CloudShadowMapResolutionScale);
		L->CloudShadowRaySampleCountScale = 2.0f;
		L->CloudShadowStrength = 1.0f;
		L->CloudShadowOnSurfaceStrength = 1.0f;
		L->CloudShadowOnAtmosphereStrength = 0.0f; // >0 draws streaky light shafts across the whole disc from orbit
		FParse::Value(Cmd, TEXT("-shafts="), L->CloudShadowOnAtmosphereStrength);
		FParse::Value(Cmd, TEXT("-cloudshadow="), L->CloudShadowStrength);
		L->MarkRenderStateDirty();
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

	if (!FParse::Param(Cmd, TEXT("noclouds"))) SetupClouds();

	// Sky light: real-time capture of atmosphere + clouds gives the shadowed side of clouds its blue fill.
	int32 bSkyLight = 0; FParse::Value(Cmd, TEXT("-skylight="), bSkyLight);
	if (bSkyLight)
	{
		if (ASkyLight* SL = W->SpawnActor<ASkyLight>(FVector::ZeroVector, FRotator::ZeroRotator))
		{
			USkyLightComponent* C = SL->GetLightComponent();
			C->SetMobility(EComponentMobility::Movable);
			C->bRealTimeCapture = true;
			C->SourceType = SLS_CapturedScene;
			C->bLowerHemisphereIsBlack = false;
			float SkyInt = 1.0f; FParse::Value(Cmd, TEXT("-skyint="), SkyInt);
			C->SetIntensity(SkyInt);
			C->MarkRenderStateDirty();
		}
	}

	SetupStarsAndMoon();

	// Post-processing: manual exposure, gentle bloom
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
	Camera->GetCameraComponent()->SetFieldOfView(float(bPov ? PovFov : CamFov));
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	PlaceCamera();
	PlaceMoon();
}

void ALabGameMode::PlaceMoon()
{
	// -moon=azDeg,elDeg,distR,radiusKm : a moon placed relative to the camera's view (az right, el up),
	// so a shot can frame it beside the planet. Lit by the sun like everything else (phase is geometric).
	FString Tmp;
	if (!FParse::Value(FCommandLine::Get(), TEXT("-moon="), Tmp)) return;
	TArray<double> M = { -12.0, 5.0, 30.0, 27.0 }; ParseList(TEXT("-moon="), M);
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* MM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/CloudLab/M_Moon.M_Moon"));
	if (!Sphere || !MM) return;
	const FTransform CT = Camera->GetActorTransform();
	const FVector Dir = CT.TransformVectorNoScale(FRotator(M[1], M[0], 0.0).Vector());
	const FVector Pos = CT.GetLocation() + Dir * (M[2] * Planet->RadiusUU);
	AStaticMeshActor* A = GetWorld()->SpawnActor<AStaticMeshActor>(Pos, FRotator(10, 40, 0));
	A->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	A->GetStaticMeshComponent()->SetStaticMesh(Sphere);
	A->GetStaticMeshComponent()->SetMaterial(0, MM);
	A->SetActorScale3D(FVector(M[3] * 1.0e5 / 50.0));
}

void ALabGameMode::SetupClouds()
{
	UWorld* W = GetWorld();
	const TCHAR* Cmd = FCommandLine::Get();
	const FLabPlanetData& D = Planet->Data();

	FCloudWeatherParams WP;
	FParse::Value(Cmd, TEXT("-wxres="), WP.Width); WP.Height = WP.Width / 2;
	FParse::Value(Cmd, TEXT("-wxseed="), WP.Seed);
	FParse::Value(Cmd, TEXT("-itcz="), WP.ItczLatDeg);
	FParse::Value(Cmd, TEXT("-cover="), WP.CoverScale);
	{ TArray<double> T = { WP.TropicalLatDeg, WP.TropicalLonDeg, WP.TropicalRadiusDeg }; ParseList(TEXT("-hurricane="), T); WP.TropicalLatDeg = T[0]; WP.TropicalLonDeg = T[1]; WP.TropicalRadiusDeg = T[2]; }
	Weather.Build(D, WP);
	FString MapOut;
	if (FParse::Value(Cmd, TEXT("-mapout="), MapOut)) Weather.SavePng(MapOut);

	const double T0 = FPlatformTime::Seconds();
	int32 ShapeRes = 128, DetailRes = 64;
	FParse::Value(Cmd, TEXT("-shaperes="), ShapeRes);
	FParse::Value(Cmd, TEXT("-detailres="), DetailRes);
	WeatherTex = Weather.MakeTexture(this);
	ShapeTex = CloudNoise::MakeShapeVolume(this, ShapeRes, 11);
	DetailTex = CloudNoise::MakeDetailVolume(this, DetailRes, 23);
	UE_LOG(LogTemp, Display, TEXT("CloudLab: noise volumes %d^3 + %d^3 built in %.2f s"), ShapeRes, DetailRes, FPlatformTime::Seconds() - T0);

	AVolumetricCloud* CloudA = W->SpawnActor<AVolumetricCloud>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!CloudA) return;
	Cloud = CloudA->FindComponentByClass<UVolumetricCloudComponent>();
	if (!Cloud) return;

	double Bottom = 0.25, Height = 1.6; // km: Earth's ~1.5 km base / ~10 km tops scaled by S
	{ TArray<double> L = { Bottom, Height }; ParseList(TEXT("-layer="), L); Bottom = L[0]; Height = L[1]; }
	Cloud->SetPlanetRadius(float(Planet->SeaRadiusUU() / 1.0e5));
	Cloud->SetLayerBottomAltitude(float(Bottom));
	Cloud->SetLayerHeight(float(Height));
	Cloud->SetTracingStartMaxDistance(float(PlanetKm * 20.0));
	Cloud->SetTracingMaxDistance(float(PlanetKm * 3.0));
	Cloud->SetbUsePerSampleAtmosphericLightTransmittance(true);
	// Sample budget. View samples = min(96 * ViewScale, r.VolumetricCloud.ViewRaySampleMaxCount) spread over
	// r.VolumetricCloud.DistanceToSampleMaxCount km of ray; secondary shadow rays use 10 * ShadowScale samples
	// over ShadowDist km. Our layer is only 1.6 km thick, so both distances must be short or the steps get
	// longer than the clouds (grainy, flat lighting).
	// Production: switch these by camera altitude every frame (orbit profile vs low-altitude profile).
	const double CamAltKm = bPov ? PovAltKm : (CamDistR * PlanetKm - PlanetKm);
	const bool bNear = CamAltKm < 15.0;
	float ViewScale = bNear ? 2.0f : 1.0f, ShadowScale = 2.0f, ShadowDist = 4.0f;
	GEngine->Exec(W, *FString::Printf(TEXT("r.VolumetricCloud.DistanceToSampleMaxCount %.1f"), FMath::Clamp(CamAltKm * 0.3, 3.0, 15.0)));
	FParse::Value(Cmd, TEXT("-viewsamples="), ViewScale);
	FParse::Value(Cmd, TEXT("-shadowsamples="), ShadowScale);
	FParse::Value(Cmd, TEXT("-shadowdist="), ShadowDist);
	Cloud->SetViewSampleCountScale(ViewScale);
	Cloud->SetShadowViewSampleCountScale(ShadowScale);
	Cloud->SetShadowTracingDistance(ShadowDist);
	Cloud->SetSkyLightCloudBottomOcclusion(0.5f);

	UMaterialInterface* CM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/CloudLab/M_WeatherClouds.M_WeatherClouds"));
	if (!CM)
	{
		UE_LOG(LogTemp, Error, TEXT("CloudLab: M_WeatherClouds missing; run Scripts/make_clouds.py"));
		return;
	}
	CloudMID = UMaterialInstanceDynamic::Create(CM, this);
	CloudMID->SetTextureParameterValue(TEXT("Weather"), WeatherTex);
	CloudMID->SetTextureParameterValue(TEXT("ShapeTex"), ShapeTex);
	CloudMID->SetTextureParameterValue(TEXT("DetailTex"), DetailTex);
	// -cp=Name:Value,Name:Value overrides material scalars
	FString CP;
	if (FParse::Value(Cmd, TEXT("-cp="), CP, false))
	{
		TArray<FString> Items; CP.ParseIntoArray(Items, TEXT(","));
		for (const FString& It : Items)
		{
			FString K, V;
			if (It.Split(TEXT(":"), &K, &V)) { CloudMID->SetScalarParameterValue(FName(*K), FCString::Atof(*V)); UE_LOG(LogTemp, Display, TEXT("CloudLab: %s = %s"), *K, *V); }
		}
	}
	Cloud->SetMaterial(CloudMID);
}

void ALabGameMode::SetupStarsAndMoon()
{
	UWorld* W = GetWorld();
	const TCHAR* Cmd = FCommandLine::Get();
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!Sphere) return;

	if (!FParse::Param(Cmd, TEXT("nostars")))
	{
		if (UMaterialInterface* SM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/CloudLab/M_StarSky.M_StarSky")))
		{
			AStaticMeshActor* A = W->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator);
			A->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			A->GetStaticMeshComponent()->SetStaticMesh(Sphere);
			A->GetStaticMeshComponent()->SetMaterial(0, SM);
			A->GetStaticMeshComponent()->SetCastShadow(false);
			A->GetStaticMeshComponent()->bAffectDistanceFieldLighting = false;
			A->GetStaticMeshComponent()->SetVisibleInRayTracing(false);
			A->SetActorScale3D(FVector(1.0e7)); // basic sphere radius 50 uu -> 5e8 uu = 5,000 km
			float Bright = 1.0f; FParse::Value(Cmd, TEXT("-stars="), Bright);
			UMaterialInstanceDynamic* MI = UMaterialInstanceDynamic::Create(SM, this);
			MI->SetScalarParameterValue(TEXT("StarBrightness"), Bright);
			A->GetStaticMeshComponent()->SetMaterial(0, MI);
		}
	}

	// Moon: a second atmospheric light (index 1), faint and cool, so the night side reads.
	float MoonLux = 0.0f; FParse::Value(Cmd, TEXT("-moonlux="), MoonLux);
	if (MoonLux > 0.f)
	{
		TArray<double> M = { SunYaw + 180.0, 10.0 }; ParseList(TEXT("-moonlight="), M); // yaw,pitch toward the moon
		MoonLight = W->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(MoonLight->GetLightComponent()))
		{
			L->SetMobility(EComponentMobility::Movable);
			L->SetIntensity(MoonLux);
			L->SetLightColor(FLinearColor(0.75f, 0.85f, 1.0f));
			L->SetAtmosphereSunLight(true);
			L->SetAtmosphereSunLightIndex(1);
			L->SetCastShadows(true);
			L->bCastCloudShadows = false;
			L->SetLightSourceAngle(0.53f);
			L->MarkRenderStateDirty();
		}
		const FRotator ToMoon(M[1], M[0], 0.0);
		MoonLight->SetActorRotation((-ToMoon.Vector()).Rotation());
	}
}

void ALabGameMode::PlaceSun()
{
	// The light points along its forward vector; aim it from the sun direction towards the planet.
	const FRotator ToSun(SunPitch, SunYaw, 0.0);
	Sun->SetActorRotation((-ToSun.Vector()).Rotation());
}

void ALabGameMode::PlaceCamera()
{
	if (bPov)
	{
		// Local frame at (lat, lon): up, east, north. Heading 0 = north, 90 = east; pitch < 0 looks down.
		const FVector Up = DirLatLon(PovLat, PovLon);
		FVector East = FVector::CrossProduct(FVector::ZAxisVector, Up).GetSafeNormal();
		if (East.IsNearlyZero()) East = FVector::YAxisVector;
		const FVector North = FVector::CrossProduct(Up, East);
		const FVector Pos = Up * (Planet->SeaRadiusUU() + PovAltKm * 1.0e5);
		const double H = FMath::DegreesToRadians(PovHeading), Pt = FMath::DegreesToRadians(PovPitch);
		const FVector Horiz = North * FMath::Cos(H) + East * FMath::Sin(H);
		const FVector Fwd = Horiz * FMath::Cos(Pt) + Up * FMath::Sin(Pt);
		Camera->SetActorLocation(Pos);
		Camera->SetActorRotation(FRotationMatrix::MakeFromXZ(Fwd, Up).Rotator());
		return;
	}
	const double Dist = CamDistR * Planet->RadiusUU;
	const FRotator Dir(CamPitch, CamYaw, 0.0);
	const FVector Pos = Dir.Vector() * Dist;
	Camera->SetActorLocation(Pos);
	Camera->SetActorRotation((-Pos).Rotation());
}

void ALabGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if WITH_EDITOR
	// Do not start the warm-up count until every shader (the cloud material especially) has compiled.
	if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) { if ((++WaitFrames % 120) == 0) UE_LOG(LogTemp, Display, TEXT("CloudLab: waiting for %d shader jobs"), GShaderCompilingManager->GetNumRemainingJobs()); return; }
#endif
	++Frame;
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		if (PC->GetViewTarget() != Camera) PC->SetViewTarget(Camera);
	if (SpinDegPerSec != 0.0 && Planet) Planet->AddActorWorldRotation(FRotator(0.0, SpinDegPerSec * DeltaSeconds, 0.0));

	// Perf: average over the 100 frames before the shot (shaders and TSR have settled by then).
	if (Frame > WarmFrames - 100 && Frame <= WarmFrames)
	{
		SumDt += DeltaSeconds; MaxDt = FMath::Max(MaxDt, double(DeltaSeconds)); ++NPerf;
		SumGpu += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
	}
	if (bProfileGpu && Frame == WarmFrames - 10) GEngine->Exec(GetWorld(), TEXT("ProfileGPU"));

	if (!ShotPath.IsEmpty())
	{
		if (Frame == WarmFrames)
		{
			UE_LOG(LogTemp, Display, TEXT("CloudLabPerf: frame %.2f ms avg (max %.2f), GPU %.2f ms avg over %d frames"),
				1000.0 * SumDt / FMath::Max(1, NPerf), 1000.0 * MaxDt, SumGpu / FMath::Max(1, NPerf), NPerf);
			FScreenshotRequest::RequestScreenshot(ShotPath, false, false);
			ShotFrame = Frame;
		}
		else if (ShotFrame > 0 && Frame > ShotFrame + 20)
		{
			FPlatformMisc::RequestExit(false, TEXT("LabShot"));
		}
	}
}
