#include "LabGameMode.h"
#include "SurfacePlanet.h"
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
#include "Misc/App.h"
#include "UnrealClient.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "Engine/Engine.h"
#include "RenderCore.h"
#include "RHI.h"
#include "ShaderCompiler.h"
#include "Engine/SkyLight.h"

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

	void Summ(const TArray<float>& A, float& Avg, float& Mx)
	{
		Avg = 0.f; Mx = 0.f;
		for (float V : A) { Avg += V; Mx = FMath::Max(Mx, V); }
		if (A.Num()) Avg /= A.Num();
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
	{ TArray<double> L = { LookLat, LookLon, LookDistKm, LookHeading, LookPitch, Fov }; ParseList(TEXT("-look="), L);
	  LookLat = L[0]; LookLon = L[1]; LookDistKm = L[2]; LookHeading = L[3]; LookPitch = L[4]; Fov = L[5]; }
	{ TArray<double> L = { SunElev, SunAz }; ParseList(TEXT("-sunl="), L); SunElev = L[0]; SunAz = L[1]; }
	double Ev = 0.0; FParse::Value(Cmd, TEXT("-ev="), Ev);
	double SunLux = 10.0; FParse::Value(Cmd, TEXT("-lux="), SunLux);
	double SkyInt = 1.0; FParse::Value(Cmd, TEXT("-sky="), SkyInt);
	const bool bClouds = FParse::Param(Cmd, TEXT("clouds"));
	FParse::Value(Cmd, TEXT("-fly="), FlySeconds);
	FParse::Value(Cmd, TEXT("-flyshots="), FlyShots);
	{ TArray<double> L = { FlyStartKm, FlyEndKm, FlyStartPitch }; ParseList(TEXT("-flyrange="), L); FlyStartKm = L[0]; FlyEndKm = L[1]; FlyStartPitch = L[2]; }

	{
		FString Res = TEXT("1920x1080");
		FParse::Value(Cmd, TEXT("-res="), Res);
		GEngine->Exec(W, *FString::Printf(TEXT("r.SetRes %sw"), *Res));
		GEngine->Exec(W, TEXT("r.VSync 0"));
		GEngine->Exec(W, TEXT("t.MaxFPS 0"));
	}

	// Planet
	Planet = W->SpawnActor<ASurfacePlanet>(FVector::ZeroVector, FRotator::ZeroRotator);
	FParse::Value(Cmd, TEXT("-lodpx="), Planet->TargetPx);
	FParse::Value(Cmd, TEXT("-k="), Planet->K);
	FParse::Value(Cmd, TEXT("-maxlevel="), Planet->MaxLevel);
	FParse::Value(Cmd, TEXT("-surfdebug="), ASurfacePlanet::DebugMode);
	FString LandName = TEXT("/Game/Surface/M_SurfLand.M_SurfLand"), WaterName = TEXT("/Game/Surface/M_SurfWater.M_SurfWater");
	FParse::Value(Cmd, TEXT("-landmat="), LandName);
	FParse::Value(Cmd, TEXT("-watermat="), WaterName);
	UMaterialInterface* LandMat = LoadObject<UMaterialInterface>(nullptr, *LandName);
	UMaterialInterface* WaterMat = LoadObject<UMaterialInterface>(nullptr, *WaterName);
	UE_LOG(LogTemp, Display, TEXT("Lab: land material %s, water material %s"), LandMat ? *LandMat->GetName() : TEXT("NONE"), WaterMat ? *WaterMat->GetName() : TEXT("NONE"));
	Planet->Init(DataPath, LandMat, WaterMat);

	// Look target
	TargetDir = FRotator(LookLat, LookLon, 0.0).Vector();
	const double Hm = Planet->HeightUE(TargetDir);
	TargetPos = TargetDir * (Planet->RadiusCm + FMath::Max(Hm, 0.0) * 100.0);
	UE_LOG(LogTemp, Display, TEXT("Lab: target lat %.2f lon %.2f ground %.1f m"), LookLat, LookLon, Hm);
	if (FParse::Param(Cmd, TEXT("profile")))
	{
		// Height profile eastwards from the target (debug)
		const FVector East = FVector::CrossProduct(FVector(0, 0, 1), TargetDir).GetSafeNormal();
		FString Line;
		for (int32 I = 0; I <= 60; ++I)
		{
			const double Km = I * 0.5;
			const FVector D = (TargetDir + East * (Km / PlanetKm)).GetSafeNormal();
			FSurfAttr A;
			const double H = Planet->T.Height(ASurfacePlanet::ToSim(D).GetSafeNormal(), 0.5, &A);
			Line += FString::Printf(TEXT(" %.1f:%.0f/%.0f"), Km, H, A.BaseH);
		}
		UE_LOG(LogTemp, Display, TEXT("PROFILE%s"), *Line);
	}

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
		// Without this the sun colour is the atmosphere transmittance at the planet's north pole
		// (UE's global light transmittance), so any sun below the pole's horizon lights nothing.
		L->bPerPixelAtmosphereTransmittance = true;
		L->MarkRenderStateDirty();
	}
	PlaceSun();

	// Sky light, captured from the atmosphere in real time (ambient for shadowed valleys).
	Sky = W->SpawnActor<ASkyLight>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (USkyLightComponent* SL = Sky->GetLightComponent())
	{
		SL->SetMobility(EComponentMobility::Movable);
		SL->bRealTimeCapture = true;
		SL->SourceType = ESkyLightSourceType::SLS_CapturedScene;
		SL->SetIntensity(float(SkyInt));
		SL->bLowerHemisphereIsBlack = false;
		SL->MarkRenderStateDirty();
	}

	// Atmosphere centred on the planet
	if (ASkyAtmosphere* SkyA = FParse::Param(Cmd, TEXT("noatmo")) ? nullptr : W->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator))
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
		// The 1/S density makes ground-level haze ~6x Earth's; this scales only the aerial perspective
		// applied to geometry (the sky and the limb keep the full density).
		double ApScale = 1.0; FParse::Value(Cmd, TEXT("-apscale="), ApScale);
		A->AerialPespectiveViewDistanceScale = float(ApScale);
		A->MarkRenderStateDirty();
	}

	if (bClouds)
	{
		if (AVolumetricCloud* CloudA = W->SpawnActor<AVolumetricCloud>(FVector::ZeroVector, FRotator::ZeroRotator))
		{
			if (UVolumetricCloudComponent* C = CloudA->FindComponentByClass<UVolumetricCloudComponent>())
			{
				C->SetLayerBottomAltitude(float(3.0 * S));
				C->SetLayerHeight(float(8.0 * S));
				C->SetTracingStartMaxDistance(float(PlanetKm * 20.0));
				C->SetTracingMaxDistance(float(PlanetKm * 3.0));
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
		P.bOverride_BloomIntensity = true; P.BloomIntensity = 0.3f;
		P.bOverride_VignetteIntensity = true; P.VignetteIntensity = 0.2f;
		P.bOverride_MotionBlurAmount = true; P.MotionBlurAmount = 0.f;
	}

	Camera = W->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	Camera->GetCameraComponent()->SetFieldOfView(float(Fov));
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	PlaceCamera(FlySeconds > 0.0 ? FlyStartKm : LookDistKm, LookHeading, FlySeconds > 0.0 ? FlyStartPitch : LookPitch);

	if (FlySeconds <= 0.0)
	{
		Planet->Settle();
		UE_LOG(LogTemp, Display, TEXT("LOD settle %.0f ms: %s"), Planet->LastSettleMs, *Planet->StatsLine());
	}
	else
	{
		Planet->Settle(); // the start of the flight is built up front, the rest streams in
		UE_LOG(LogTemp, Display, TEXT("LOD settle %.0f ms: %s"), Planet->LastSettleMs, *Planet->StatsLine());
	}
}

void ALabGameMode::PlaceSun()
{
	const FVector Up = TargetDir.IsNearlyZero() ? FVector(1, 0, 0) : TargetDir;
	const FVector East = FVector::CrossProduct(FVector(0, 0, 1), Up).GetSafeNormal();
	const FVector North = FVector::CrossProduct(Up, East);
	const double El = FMath::DegreesToRadians(SunElev), Az = FMath::DegreesToRadians(SunAz);
	const FVector ToSun = (FMath::Cos(El) * (FMath::Cos(Az) * North + FMath::Sin(Az) * East) + FMath::Sin(El) * Up).GetSafeNormal();
	Sun->SetActorRotation((-ToSun).Rotation());
}

void ALabGameMode::PlaceCamera(double DistKm, double Heading, double Pitch)
{
	const FVector Up = TargetDir;
	const FVector East = FVector::CrossProduct(FVector(0, 0, 1), Up).GetSafeNormal();
	const FVector North = FVector::CrossProduct(Up, East);
	const double H = FMath::DegreesToRadians(Heading), P = FMath::DegreesToRadians(Pitch);
	const FVector Fwd = (FMath::Cos(P) * (FMath::Cos(H) * North + FMath::Sin(H) * East) - FMath::Sin(P) * Up).GetSafeNormal();
	const FVector Pos = TargetPos - Fwd * (DistKm * 1.0e5);
	Camera->SetActorLocation(Pos);
	const FVector RollUp = FMath::Abs(Pitch) > 89.0 ? (FMath::Cos(H) * North + FMath::Sin(H) * East) : Up;
	Camera->SetActorRotation(FRotationMatrix::MakeFromXZ(Fwd, RollUp).Rotator());
	if (Planet)
	{
		FIntPoint Size(1920, 1080);
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport) Size = GEngine->GameViewport->Viewport->GetSizeXY();
		if (Size.X <= 0) Size = FIntPoint(1920, 1080);
		Planet->SetView(Pos, Fwd, Fov, double(Size.X) / FMath::Max(Size.Y, 1), Size.X);
	}
	const double Ground = FMath::Max(Planet->HeightUE(Pos.GetSafeNormal()), 0.0);
	const double CamH = (Pos.Length() - Planet->RadiusCm) / 100.0 - Ground;
	// The real-time sky capture is taken at the sky light's own position: keep it just above the
	// ground under the camera (at the planet centre it would capture the inside of the planet).
	if (Sky) Sky->SetActorLocation(Pos.GetSafeNormal() * (Planet->RadiusCm + (Ground + 30.0) * 100.0));
	UE_LOG(LogTemp, Verbose, TEXT("Lab: camera %.1f m above ground"), CamH);
}

void ALabGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Materials changed by the script compile asynchronously in -game; do not count warm-up frames
	// (or take the shot) while shaders are still compiling, or the shot shows the default material.
	if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0 && Frame < 30000)
	{
		if (++ShaderWaitFrames % 120 == 1) UE_LOG(LogTemp, Display, TEXT("Lab: waiting for %d shader jobs"), GShaderCompilingManager->GetNumRemainingJobs());
		return;
	}
	++Frame;
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		if (PC->GetViewTarget() != Camera) PC->SetViewTarget(Camera);

	const float DtMs = float(FApp::GetDeltaTime() * 1000.0);
	if (FlySeconds > 0.0)
	{
		// Exponential descent in distance, pitch eases from straight down to the final view.
		if (Frame > 30) FlyT = FMath::Min(FlyT + DeltaSeconds / FlySeconds, 1.0);
		const double U = FlyT * FlyT * (3.0 - 2.0 * FlyT);
		const double Dist = FlyStartKm * FMath::Pow(FlyEndKm / FlyStartKm, U);
		const double Pitch = FMath::Lerp(FlyStartPitch, LookPitch, FMath::Clamp((U - 0.3) / 0.7, 0.0, 1.0));
		PlaceCamera(Dist, LookHeading, Pitch);
		Planet->Step(64);
		if (Frame > 40 && FlyT < 1.0)
		{
			FrameMs.Add(DtMs);
			if (DtMs > 33.f) { ++FlyHitches; UE_LOG(LogTemp, Display, TEXT("FLY hitch %.1f ms at t=%.3f"), DtMs, FlyT); }
			FlyWorstMs = FMath::Max(FlyWorstMs, DtMs);
		}
		if (Frame % 30 == 0)
			UE_LOG(LogTemp, Display, TEXT("FLY t=%.2f dist=%.2f km frame %.1f ms | %s"), FlyT, Dist, DtMs, *Planet->StatsLine());
		if (FlyShots > 0 && !ShotPath.IsEmpty() && FlyShotIdx < FlyShots)
		{
			const double Want = double(FlyShotIdx + 1) / FlyShots;
			if (FlyT >= Want - 1e-6 && (FlyT >= 1.0 || FlyShotIdx < FlyShots - 1))
			{
				FString P = ShotPath.Replace(TEXT(".png"), *FString::Printf(TEXT("-%d.png"), FlyShotIdx));
				FScreenshotRequest::RequestScreenshot(P, false, false);
				++FlyShotIdx;
			}
		}
		if (FlyT >= 1.0)
		{
			if (ShotFrame < 0) { ShotFrame = Frame; float A, M; Summ(FrameMs, A, M);
				UE_LOG(LogTemp, Display, TEXT("FLY DONE frames %d avg %.2f ms worst %.1f ms, frames over 33 ms: %d | %s"), FrameMs.Num(), A, M, FlyHitches, *Planet->StatsLine()); }
			else if (Frame > ShotFrame + 120) FPlatformMisc::RequestExit(false, TEXT("LabFly"));
		}
		return;
	}

	if (Frame > WarmFrames - 100 && Frame <= WarmFrames)
	{
		FrameMs.Add(DtMs);
		GameMs.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
		RenderMs.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
		GpuMs.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
	}
	if (!ShotPath.IsEmpty())
	{
		if (Frame == WarmFrames)
		{
			float FA, FM, GA, GM, RA, RM, PA, PM;
			Summ(FrameMs, FA, FM); Summ(GameMs, GA, GM); Summ(RenderMs, RA, RM); Summ(GpuMs, PA, PM);
			UE_LOG(LogTemp, Display, TEXT("STATS frame %.2f ms (max %.2f) game %.2f render %.2f gpu %.2f ms (max %.2f) | %s"), FA, FM, GA, RA, PA, PM, *Planet->StatsLine());
			FScreenshotRequest::RequestScreenshot(ShotPath, false, false);
			ShotFrame = Frame;
		}
		else if (ShotFrame > 0 && Frame > ShotFrame + 20)
		{
			FPlatformMisc::RequestExit(false, TEXT("LabShot"));
		}
	}
}
