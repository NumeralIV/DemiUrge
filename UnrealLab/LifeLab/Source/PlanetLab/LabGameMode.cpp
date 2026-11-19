#include "LabGameMode.h"
#include "LifeWorld.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
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
#include "RenderTimer.h"
#include "DynamicRHI.h"

namespace
{
	constexpr double PlanetKm = 100.0;
	constexpr double S = 1.0 / 6.15;

	void ParseList(const TCHAR* Key, TArray<double>& Out)
	{
		FString V;
		if (!FParse::Value(FCommandLine::Get(), Key, V, false)) return;
		TArray<FString> Parts;
		V.ParseIntoArray(Parts, TEXT(","));
		for (int32 I = 0; I < Parts.Num() && I < Out.Num(); ++I) Out[I] = FCString::Atod(*Parts[I]);
	}
	double CamDist = 600, CamYaw = 210, CamPitch = 25, CamFov = 40, AimDz = 0;
	FVector2D Focus(0, 0);
	bool bDirectCam = false;
	TArray<double> DirectCam = { 0, 0, 100, 0, -20 };
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
	int32 Site = 38500; FParse::Value(Cmd, TEXT("-site="), Site);
	const bool bNight = FParse::Param(Cmd, TEXT("night"));
	double SunYaw = 135.0, SunPitch = 21.0;
	{ TArray<double> L = { SunYaw, bNight ? -25.0 : SunPitch }; ParseList(TEXT("-sun="), L); SunYaw = L[0]; SunPitch = L[1]; }
	double Ev = bNight ? 3.5 : 0.0; FParse::Value(Cmd, TEXT("-ev="), Ev);
	double SunLux = 10.0; FParse::Value(Cmd, TEXT("-lux="), SunLux);
	const bool bClouds = !FParse::Param(Cmd, TEXT("noclouds"));
	double CloudBaseKm = 1.6, CloudHeightKm = 2.2; FParse::Value(Cmd, TEXT("-cloudbase="), CloudBaseKm); FParse::Value(Cmd, TEXT("-cloudh="), CloudHeightKm);
	{
		FString Res = TEXT("1920x1080");
		FParse::Value(Cmd, TEXT("-res="), Res);
		GEngine->Exec(W, *FString::Printf(TEXT("r.SetRes %sw"), *Res));
		GEngine->Exec(W, TEXT("t.MaxFPS 0"));
		GEngine->Exec(W, TEXT("r.VSync 0"));
	}

	// World patch
	World = W->SpawnActor<ALifeWorld>(FVector(0, 0, ALifeWorld::Rm * 100.0), FRotator::ZeroRotator);
	World->Build(DataPath, Site);

	// Focus / camera
	{
		FString F;
		if (FParse::Value(Cmd, TEXT("-focus="), F, false))
		{
			bool bFound = false;
			for (const FLifeSettlement& St : World->Settlements)
				if (St.Name.Equals(F, ESearchCase::IgnoreCase)) { Focus = St.C; bFound = true; }
			if (!bFound) { TArray<double> P = { 0, 0 }; ParseList(TEXT("-focus="), P); Focus = FVector2D(P[0], P[1]); }
		}
		TArray<double> V = { CamDist, CamYaw, CamPitch }; ParseList(TEXT("-view="), V); CamDist = V[0]; CamYaw = V[1]; CamPitch = V[2];
		FParse::Value(Cmd, TEXT("-fov="), CamFov);
		FParse::Value(Cmd, TEXT("-aim="), AimDz);
		FString Dc; if (FParse::Value(Cmd, TEXT("-cam="), Dc)) { bDirectCam = true; ParseList(TEXT("-cam="), DirectCam); }
	}

	// Sun and moon
	auto MakeLight = [&](double Yaw, double Pitch, double Lux, FLinearColor Col, int32 Index, bool bShadows) -> ADirectionalLight*
	{
		ADirectionalLight* A = W->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator::ZeroRotator);
		UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(A->GetLightComponent());
		L->SetMobility(EComponentMobility::Movable);
		L->SetIntensity(float(Lux));
		L->SetLightColor(Col);
		L->SetAtmosphereSunLight(true);
		L->SetAtmosphereSunLightIndex(Index);
		L->SetCastShadows(bShadows);
		L->bCastCloudShadows = true;
		L->SetLightSourceAngle(Index == 0 ? 0.53f : 0.6f);
		const FRotator ToSun(Pitch, Yaw, 0.0);
		A->SetActorRotation((-ToSun.Vector()).Rotation());
		return A;
	};
	Sun = MakeLight(SunYaw, SunPitch, SunLux, FLinearColor::White, 0, true);
	if (bNight)
	{
		double MoonLux = 0.06; FParse::Value(Cmd, TEXT("-moonlux="), MoonLux);
		TArray<double> M = { SunYaw + 150.0, 32.0 }; ParseList(TEXT("-moon="), M);
		Moon = MakeLight(M[0], M[1], MoonLux, FLinearColor(0.62f, 0.72f, 1.0f), 1, true);
	}
	float Glow = 0.45f; FParse::Value(Cmd, TEXT("-glow="), Glow);
	World->SetNight(bNight ? 1.f : 0.f, Glow);

	// Sky light (real-time capture of the atmosphere) so shadows are not black
	if (ASkyLight* SL = W->SpawnActor<ASkyLight>(FVector(0, 0, ALifeWorld::Rm * 100.0 + 50000.0), FRotator::ZeroRotator))
	{
		USkyLightComponent* C = SL->GetLightComponent();
		C->SetMobility(EComponentMobility::Movable);
		C->SetRealTimeCapture(true);
		C->bLowerHemisphereIsBlack = false;
		C->SetLowerHemisphereColor(FLinearColor(0.05f, 0.05f, 0.04f));
		double SkyI = 0.85; FParse::Value(Cmd, TEXT("-sky="), SkyI);
		C->SetIntensity(float(SkyI));
		C->RecaptureSky();
	}

	// Atmosphere centred on the planet (same scaling as the orchestrator's lab)
	if (ASkyAtmosphere* SkyA = W->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator))
	{
		USkyAtmosphereComponent* A = SkyA->GetComponent();
		A->TransformMode = ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;
		A->SetBottomRadius(float(PlanetKm));
		A->SetAtmosphereHeight(float(60.0 * S));
		A->SetRayleighExponentialDistribution(float(8.0 * S));
		A->SetRayleighScatteringScale(float(0.0331 / S));
		A->SetMieExponentialDistribution(float(1.2 * S));
		A->SetMieScatteringScale(float(0.003996 / S));
		A->SetMieAbsorptionScale(float(0.000444 / S));
		A->SetOtherAbsorptionScale(float(0.001881 / S));
		double Haze = 0.4; FParse::Value(Cmd, TEXT("-haze="), Haze);
		A->SetAerialPespectiveViewDistanceScale(float(Haze));
		A->MarkRenderStateDirty();
	}

	if (bClouds)
	{
		if (AVolumetricCloud* CloudA = W->SpawnActor<AVolumetricCloud>(FVector::ZeroVector, FRotator::ZeroRotator))
		{
			if (UVolumetricCloudComponent* C = CloudA->FindComponentByClass<UVolumetricCloudComponent>())
			{
				C->SetLayerBottomAltitude(float(CloudBaseKm));
				C->SetLayerHeight(float(CloudHeightKm));
				C->SetTracingStartMaxDistance(float(PlanetKm * 3.0));
				C->SetTracingMaxDistance(float(80.0));
				C->SetPlanetRadius(float(PlanetKm));
				C->SetbUsePerSampleAtmosphericLightTransmittance(true);
				if (UMaterialInterface* EM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst")))
					C->SetMaterial(EM);
			}
		}
	}

	if (APostProcessVolume* PP = W->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator))
	{
		PP->bUnbound = true;
		FPostProcessSettings& P = PP->Settings;
		P.bOverride_AutoExposureMethod = true; P.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
		P.bOverride_AutoExposureApplyPhysicalCameraExposure = true; P.AutoExposureApplyPhysicalCameraExposure = false;
		P.bOverride_AutoExposureBias = true; P.AutoExposureBias = float(Ev);
		double Bloom = 0.35; FParse::Value(Cmd, TEXT("-bloom="), Bloom);
		P.bOverride_BloomIntensity = true; P.BloomIntensity = float(Bloom);
		P.bOverride_VignetteIntensity = true; P.VignetteIntensity = 0.3f;
		P.bOverride_MotionBlurAmount = true; P.MotionBlurAmount = 0.f;
		double Dof = 0.0; FParse::Value(Cmd, TEXT("-dof="), Dof);
		if (Dof > 0.0)
		{
			P.bOverride_DepthOfFieldFocalDistance = true; P.DepthOfFieldFocalDistance = float(Dof * CamDist * 100.0);
			P.bOverride_DepthOfFieldFstop = true; P.DepthOfFieldFstop = 2.8f;
			P.bOverride_DepthOfFieldSensorWidth = true; P.DepthOfFieldSensorWidth = 36.f;
		}
	}

	Camera = W->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	Camera->GetCameraComponent()->SetFieldOfView(float(CamFov));
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	PlaceCamera();
}

void ALabGameMode::PlaceCamera()
{
	// Everything is in the site's local frame (metres); near the site, local axes ~ world axes.
	FVector3d CamM, AimM;
	if (bDirectCam)
	{
		const double X = DirectCam[0], Y = DirectCam[1];
		const double H = FMath::Max(0.0, double(World->HeightAt(X, Y))) + DirectCam[2];
		CamM = World->SurfaceM(X, Y, H);
		const FRotator R(DirectCam[4], DirectCam[3], 0.0);
		AimM = CamM + FVector3d(R.Vector()) * 100.0;
	}
	else
	{
		const double Fh = FMath::Max(0.0, double(World->HeightAt(Focus.X, Focus.Y))) + AimDz;
		const double Yr = FMath::DegreesToRadians(CamYaw), Pr = FMath::DegreesToRadians(CamPitch);
		const FVector3d Off(CamDist * FMath::Cos(Pr) * FMath::Cos(Yr), CamDist * FMath::Cos(Pr) * FMath::Sin(Yr), CamDist * FMath::Sin(Pr));
		const double Cx = Focus.X + Off.X, Cy = Focus.Y + Off.Y;
		const double Ground = FMath::Max(0.0, double(World->HeightAt(Cx, Cy)));
		CamM = World->SurfaceM(Cx, Cy, FMath::Max(Fh + Off.Z, Ground + 2.0));
		AimM = World->SurfaceM(Focus.X, Focus.Y, Fh);
	}
	const FVector CamW = FVector(CamM * 100.0) + World->GetActorLocation();
	const FVector AimW = FVector(AimM * 100.0) + World->GetActorLocation();
	Camera->SetActorLocation(CamW);
	// keep the horizon level relative to the local up
	const FVector Up = CamW.GetSafeNormal();
	Camera->SetActorRotation(FRotationMatrix::MakeFromXZ(AimW - CamW, Up).Rotator());
	UE_LOG(LogTemp, Display, TEXT("LifeCam at (%.0f, %.0f, %.0f m) looking at (%.0f, %.0f, %.0f m)"), CamM.X, CamM.Y, CamM.Z, AimM.X, AimM.Y, AimM.Z);
}

void ALabGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	++Frame;
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		if (PC->GetViewTarget() != Camera) PC->SetViewTarget(Camera);

	// Timing over the 60 frames before the shot (scene has settled by then).
	if (Frame > WarmFrames - 60 && Frame <= WarmFrames)
	{
		AccFrame += DeltaSeconds * 1000.0;
		AccGame += FPlatformTime::ToMilliseconds(GGameThreadTime);
		AccRender += FPlatformTime::ToMilliseconds(GRenderThreadTime);
		AccRhi += FPlatformTime::ToMilliseconds(GRHIThreadTime);
		const float Gpu = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
		AccGpu += Gpu;
		GpuSamples.Add(Gpu);
		++AccN;
	}
	if (!ShotPath.IsEmpty())
	{
		if (Frame == WarmFrames)
		{
			GpuSamples.Sort();
			const float Med = GpuSamples.Num() ? GpuSamples[GpuSamples.Num() / 2] : 0.f;
			UE_LOG(LogTemp, Display, TEXT("LIFESTATS timing over %d frames: frame %.2f ms, game %.2f, render %.2f, rhi %.2f, gpu %.2f (median %.2f)"),
				AccN, AccFrame / AccN, AccGame / AccN, AccRender / AccN, AccRhi / AccN, AccGpu / AccN, Med);
			UE_LOG(LogTemp, Display, TEXT("LIFESTATS %s"), *World->StatsLine());
			FScreenshotRequest::RequestScreenshot(ShotPath, false, false);
			ShotFrame = Frame;
		}
		else if (ShotFrame > 0 && Frame > ShotFrame + 20)
		{
			FPlatformMisc::RequestExit(false, TEXT("LabShot"));
		}
	}
}
