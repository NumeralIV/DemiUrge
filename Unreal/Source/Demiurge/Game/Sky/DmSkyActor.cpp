// DEMIURGE — Game/Sky/DmSkyActor.cpp : see the header. Owner: C2 sky.

#include "Game/Sky/DmSkyActor.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmScene.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Sim/Sim.h"

#include "Async/Async.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/VolumeTexture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogDmSky, Log, All);

namespace
{
	constexpr double S = 1.0 / 6.15;   // Earth's atmosphere / cloud heights scaled (SPEC-UNREAL 1)
	constexpr double Deg = UE_DOUBLE_PI / 180.0;
	constexpr int32 FineW = 4096, FineH = 2048;
	inline double Smooth(double E0, double E1, double X) { const double T = FMath::Clamp((X - E0) / (E1 - E0), 0.0, 1.0); return T * T * (3.0 - 2.0 * T); }

	TWeakObjectPtr<ADmSky> GActiveSky;

	/** The molten look only while the world is molten (fixtures and loaded History worlds may carry heat 1). */
	double MoltenHeat(const dm::World* W)
	{
		return (W && W->phase == dm::Phase::Molten) ? FMath::Clamp(W->heat, 0.0, 1.0) : 0.0;
	}

	/** Comma list that does not stop at the commas (FParse::Value would; docs/lab/clouds.md pitfall 1). */
	int32 ArgList(const TCHAR* Key, double* Out, int32 Max) { return FDmShotArgs::List(Key, Out, Max); }

	void SetCVar(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name)) V->Set(Value, ECVF_SetByCode);
	}

	// ------------------------------------------------------------------ mesh factories (one place to swap in assets)

	/** Smooth unit UV sphere (positions = normals), for the moon. */
	void MakeSphere(UProceduralMeshComponent* P, int32 Rings, int32 Segs)
	{
		TArray<FVector> V; TArray<int32> T; TArray<FVector> N; TArray<FVector2D> UV; TArray<FColor> C; TArray<FProcMeshTangent> Tg;
		for (int32 R = 0; R <= Rings; ++R)
		{
			const double Th = UE_DOUBLE_PI * R / Rings;
			for (int32 Sg = 0; Sg <= Segs; ++Sg)
			{
				const double Ph = 2.0 * UE_DOUBLE_PI * Sg / Segs;
				const FVector D(FMath::Sin(Th) * FMath::Cos(Ph), FMath::Sin(Th) * FMath::Sin(Ph), FMath::Cos(Th));
				V.Add(D); N.Add(D); UV.Add(FVector2D(double(Sg) / Segs, double(R) / Rings));
				Tg.Add(FProcMeshTangent(FVector(-FMath::Sin(Ph), FMath::Cos(Ph), 0.0), false));
			}
		}
		for (int32 R = 0; R < Rings; ++R)
			for (int32 Sg = 0; Sg < Segs; ++Sg)
			{
				const int32 A = R * (Segs + 1) + Sg, B = A + Segs + 1;
				T.Append({ A, A + 1, B, A + 1, B + 1, B });
			}
		P->CreateMeshSection(0, V, T, N, UV, C, Tg, false);
	}

	/** Aurora ribbons around both (magnetic) poles in the planet frame: folded curtains from 2.4 to 9 km
	 *  above the datum (clouds top out near 1.9 km). UV.x along the ring, UV.y foot (0) to top (1). */
	void MakeAuroraRings(UProceduralMeshComponent* P)
	{
		for (int32 Hemi = 0; Hemi < 2; ++Hemi)
		{
			const double H = Hemi == 0 ? 1.0 : -1.0;
			// magnetic pole: 9 deg off the spin axis
			const FVector Pole = DmCoords::LatLonToDir(H * 81.0, Hemi == 0 ? 60.0 : -130.0);
			FVector E, N;
			DmCoords::Basis(Pole, E, N);
			TArray<FVector> V; TArray<int32> T; TArray<FVector> Nn; TArray<FVector2D> UV; TArray<FColor> C; TArray<FProcMeshTangent> Tg;
			constexpr int32 Segs = 720;
			const double R0 = DmCoords::RadiusUU + 2.4 * DmCoords::UUPerKm, R1 = DmCoords::RadiusUU + 9.0 * DmCoords::UUPerKm;
			for (int32 K = 0; K <= Segs; ++K)
			{
				const double U = double(K) / Segs, Ph = 2.0 * UE_DOUBLE_PI * U;
				// the oval: colatitude about 19 deg, folded
				const double Colat = (19.0 + 2.6 * FMath::Sin(3.0 * Ph + 1.0) + 1.3 * FMath::Sin(7.0 * Ph + 0.4) + 0.6 * FMath::Sin(17.0 * Ph)) * Deg;
				const FVector Around = E * FMath::Cos(Ph) + N * FMath::Sin(Ph);
				const FVector D = (Pole * FMath::Cos(Colat) + Around * FMath::Sin(Colat)).GetSafeNormal();
				V.Add(D * R0); V.Add(D * R1);
				const FVector Out = Around.GetSafeNormal();
				Nn.Add(Out); Nn.Add(Out);
				UV.Add(FVector2D(U, 0.0)); UV.Add(FVector2D(U, 1.0));
				const FProcMeshTangent Tan(FVector::CrossProduct(D, Out), false);
				Tg.Add(Tan); Tg.Add(Tan);
			}
			for (int32 K = 0; K < Segs; ++K)
			{
				const int32 A = 2 * K;
				T.Append({ A, A + 2, A + 1, A + 1, A + 2, A + 3 });
			}
			// the oval seen from above: a glowing band lying just above the clouds (UV.y 2..3 across it), so the
			// aurora reads from orbit too (the curtains are edge-on from there)
			const int32 B0 = V.Num();
			const double Rb = DmCoords::RadiusUU + 2.3 * DmCoords::UUPerKm;
			for (int32 K = 0; K <= Segs; ++K)
			{
				const double U = double(K) / Segs, Ph = 2.0 * UE_DOUBLE_PI * U;
				const double Colat = (19.0 + 2.6 * FMath::Sin(3.0 * Ph + 1.0) + 1.3 * FMath::Sin(7.0 * Ph + 0.4) + 0.6 * FMath::Sin(17.0 * Ph)) * Deg;
				const double Hw = (1.6 + 0.8 * FMath::Sin(5.0 * Ph + 2.0)) * Deg;
				const FVector Around = E * FMath::Cos(Ph) + N * FMath::Sin(Ph);
				for (int32 Side = 0; Side < 2; ++Side)
				{
					const double Cl = Colat + (Side ? Hw : -Hw * 0.6);
					const FVector D = (Pole * FMath::Cos(Cl) + Around * FMath::Sin(Cl)).GetSafeNormal();
					V.Add(D * Rb); Nn.Add(D); UV.Add(FVector2D(U, 2.0 + Side));
					Tg.Add(FProcMeshTangent(Around, false));
				}
			}
			for (int32 K = 0; K < Segs; ++K)
			{
				const int32 A = B0 + 2 * K;
				T.Append({ A, A + 2, A + 1, A + 1, A + 2, A + 3 });
			}
			P->CreateMeshSection(Hemi, V, T, Nn, UV, C, Tg, false);
		}
	}
}

// ====================================================================================================

ADmSky::ADmSky()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(RootComponent);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetAtmosphereSunLight(true);
	Sun->SetAtmosphereSunLightIndex(0);
	Sun->SetCastShadows(true);
	Sun->SetLightSourceAngle(0.53f);
	Sun->bCastCloudShadows = true;
	Sun->CloudShadowStrength = 1.f;
	Sun->CloudShadowOnSurfaceStrength = 1.f;
	Sun->CloudShadowOnAtmosphereStrength = 0.f;   // > 0 draws streaky shafts across the whole disc from orbit
	Sun->CloudShadowRaySampleCountScale = 2.f;
	Sun->CloudShadowMapResolutionScale = 4.f;    // 2048^2 (cvar cap raised in BeginPlay)
	Sun->CloudShadowExtent = 130.f;

	MoonLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("MoonLight"));
	MoonLight->SetupAttachment(RootComponent);
	MoonLight->SetMobility(EComponentMobility::Movable);
	// not an atmospheric light: a second one doubles the cloud tracing cost (measured 7 -> 14 ms). The clouds get
	// their moonlight as a faint ambient in the material (NightAmb) instead; the ground and sea get this light.
	MoonLight->SetAtmosphereSunLight(false);
	MoonLight->SetLightColor(FLinearColor(0.75f, 0.85f, 1.0f));
	MoonLight->SetCastShadows(false);
	MoonLight->bCastCloudShadows = false;
	MoonLight->SetLightSourceAngle(0.53f);
	MoonLight->SetIntensity(0.f);
	MoonLight->SetVisibility(false);

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(RootComponent);
	Atmosphere->TransformMode = ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;

	Clouds = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("Clouds"));
	Clouds->SetupAttachment(RootComponent);

	Post = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Post"));
	Post->SetupAttachment(RootComponent);
	Post->bUnbound = true;

	Stars = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Stars"));
	Stars->SetupAttachment(RootComponent);
	Stars->SetMobility(EComponentMobility::Movable);
	Stars->SetCastShadow(false);
	Stars->bAffectDistanceFieldLighting = false;
	Stars->SetVisibleInRayTracing(false);
	Stars->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Moon = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Moon"));
	Moon->SetupAttachment(RootComponent);
	Moon->SetMobility(EComponentMobility::Movable);
	Moon->SetCastShadow(false);
	Moon->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Aurora = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Aurora"));
	Aurora->SetupAttachment(RootComponent);
	Aurora->SetMobility(EComponentMobility::Movable);
	Aurora->SetCastShadow(false);
	Aurora->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Aurora->bAffectDistanceFieldLighting = false;

	for (int32 I = 0; I < FDmSkyStorms::FlashSlots; ++I)
	{
		UStaticMeshComponent* F = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Flash%d"), I));
		F->SetupAttachment(RootComponent);
		F->SetMobility(EComponentMobility::Movable);
		F->SetCastShadow(false);
		F->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		F->bAffectDistanceFieldLighting = false;
		F->SetVisibility(false);
		FlashDisc[I] = F;
	}
}

ADmSky* ADmSky::Get(const UObject* WorldContext)
{
	ADmSky* A = GActiveSky.Get();
	if (A && WorldContext && A->GetWorld() != WorldContext->GetWorld()) return nullptr;
	return A;
}

void ADmSky::ReadArgs()
{
	FDmShotArgs::Value(TEXT("-ev="), Ev);
	FDmShotArgs::Value(TEXT("-lux="), Tune.SunLux);
	FDmShotArgs::Value(TEXT("-daylen="), Tune.DayLength);
	Tune.DayLength = FMath::Max(1.0, Tune.DayLength);
	double R = -1.0;
	if (FDmShotArgs::Value(TEXT("-skyrate="), R)) ForcedRate = R;
	FDmShotArgs::Value(TEXT("-skytime="), SkyTime);
	FDmShotArgs::Value(TEXT("-cover="), CoverArg);
	FDmShotArgs::Value(TEXT("-aurora="), Tune.AuroraGain);
	FDmShotArgs::Value(TEXT("-flash="), FlashGain);   // default 2.5: brighter clips into a flat white disc
	if (FDmShotArgs::Value(TEXT("-moonlux="), MoonLuxArg)) Tune.MoonLux = MoonLuxArg;
	int32 Seed = -1;
	if (FDmShotArgs::Value(TEXT("-wxseed="), Seed)) { WxSeed = uint32(Seed); bSeedArg = true; }
	FString Tmp;
	if (FDmShotArgs::Value(TEXT("-moon="), Tmp)) { bMoonOverride = true; ArgList(TEXT("-moon="), MoonArgs, 4); }
	FDmShotArgs::Value(TEXT("-cloudorbitscale="), OrbitScale);
	FDmShotArgs::Value(TEXT("-cloudviewscale="), ForceViewScale);
	FDmShotArgs::Value(TEXT("-atmodrop="), AtmoDropKm);
	FDmShotArgs::Value(TEXT("-nightamb="), NightAmbGain);
	ArgList(TEXT("-cloudshadoworbit="), OrbitShadow, 2);
	bTrace = FDmShotArgs::Param(TEXT("skytrace"));
	bNoClouds = FDmShotArgs::Param(TEXT("noclouds"));
	bNoCyclones = FDmShotArgs::Param(TEXT("nocyclones"));
	// the planet's yaw at sky time 0 is 0, so -skytime= turns it too (scenarios that want a time of day)
	Yaw = -SkyTime * 2.0 * UE_DOUBLE_PI / Tune.DayLength;
}

void ADmSky::BeginPlay()
{
	Super::BeginPlay();
	GActiveSky = this;
	const double T0 = FPlatformTime::Seconds();
	ReadArgs();
	SetActorLocation(FVector::ZeroVector);

	// cloud shadow map and ray budget (docs/lab/clouds.md 1.4); Config/DefaultEngine.ini belongs to the bridge
	if (!FDmShotArgs::Param(TEXT("noskycvars")))
	{
		SetCVar(TEXT("r.VolumetricCloud.ViewRaySampleMaxCount"), 512.f);
		SetCVar(TEXT("r.VolumetricCloud.ShadowMap.MaxResolution"), 4096.f);
		SetCVar(TEXT("r.VolumetricCloud.ShadowMap.TemporalFiltering.NewFrameWeight"), 0.25f);
		// stride over clear air (zero conservative density). Measured: > 1 lands the first sample deep inside dense
		// cloud, whose long shadow ray then darkens whole frontal sheets (sky-front), for no real saving: keep 1
		double Stride = 1.0;
		FDmShotArgs::Value(TEXT("-cloudstride="), Stride);
		SetCVar(TEXT("r.VolumetricCloud.StepSizeOnZeroConservativeDensity"), float(Stride));
	}

	Sun->SetIntensity(float(Tune.SunLux));
	{
		double Cs = 1.0;
		if (FDmShotArgs::Value(TEXT("-cloudshadow="), Cs)) { Sun->CloudShadowOnSurfaceStrength = float(Cs); Sun->bCastCloudShadows = Cs > 0.0; Sun->MarkRenderStateDirty(); }
	}
	Atmosphere->SetAtmosphereHeight(float(60.0 * S));
	Atmosphere->SetRayleighExponentialDistribution(float(8.0 * S));
	Atmosphere->SetMieExponentialDistribution(float(1.2 * S));
	UpdateAtmosphere(nullptr);

	FPostProcessSettings& P = Post->Settings;
	P.bOverride_AutoExposureMethod = true; P.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	P.bOverride_AutoExposureApplyPhysicalCameraExposure = true; P.AutoExposureApplyPhysicalCameraExposure = false;
	P.bOverride_AutoExposureBias = true; P.AutoExposureBias = float(Ev);
	P.bOverride_BloomIntensity = true; P.BloomIntensity = 0.35f;
	P.bOverride_VignetteIntensity = true; P.VignetteIntensity = 0.25f;
	P.bOverride_MotionBlurAmount = true; P.MotionBlurAmount = 0.f;
	P.bOverride_WhiteTemp = true; P.WhiteTemp = 6500.f;
	P.bOverride_ColorSaturation = true; P.ColorSaturation = FVector4(1, 1, 1, 1);
	P.bOverride_ColorContrast = true; P.ColorContrast = FVector4(1, 1, 1, 1);

	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	if (Sim)
	{
		EventHandle = Sim->OnSimEvent.AddUObject(this, &ADmSky::OnSimEvent);
		DirtyHandle = Sim->Dirty().OnDirty.AddUObject(this, &ADmSky::OnDirty);
		WorldHandle = Sim->OnWorldChanged.AddUObject(this, &ADmSky::OnWorldChanged);
		if (Sim->Sim() && !bSeedArg) WxSeed = Sim->Sim()->seed * 31u + 7u;
		// a default sun (the scenario usually pins one right after the scene is built)
		const FVector Sub = DmCoords::LatLonToDir(0.0, 75.0);
		SunAzimuth = FMath::Atan2(Sub.Y, Sub.X);
		SunDecl = SeasonDeclDeg(Sim->Sim());
		bWasPinned = Sim->IsSunPinned();
		UpdateTime(Sim, 0.0);
	}

	if (!bNoClouds) SetupClouds();
	else Clouds->SetVisibility(false);
	SetupStarsMoonAurora();

#if WITH_EDITOR
	// -game from editor binaries compiles material shaders asynchronously: finish them now, or the first
	// shots have no clouds (docs/lab/clouds.md pitfall 2)
	if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
	{
		const double C0 = FPlatformTime::Seconds();
		const int32 Jobs = GShaderCompilingManager->GetNumRemainingJobs();
		GShaderCompilingManager->FinishAllCompilation();
		UE_LOG(LogDmSky, Display, TEXT("DmSky: waited %.1f s for %d shader jobs"), FPlatformTime::Seconds() - C0, Jobs);
	}
#endif
	UE_LOG(LogDmSky, Display, TEXT("DmSky: ready in %.2f s (day %.0f s, sky rate %s, seed %u, moon %s)"), FPlatformTime::Seconds() - T0, Tune.DayLength,
		ForcedRate >= 0 ? *FString::Printf(TEXT("%.2f forced"), ForcedRate) : TEXT("sim"), WxSeed, bMoonOverride ? TEXT("in view") : TEXT("orbit"));
}

void ADmSky::EndPlay(const EEndPlayReason::Type Reason)
{
	if (bPending)
	{
		// a build still in flight: wait for it and free its upload copies (they are only freed on upload otherwise)
		TSharedPtr<FBuildResult, ESPMode::ThreadSafe> R = Pending.Get();
		if (R.IsValid()) { FMemory::Free(R->CoverUpload); FMemory::Free(R->WarpUpload); R->CoverUpload = R->WarpUpload = nullptr; }
	}
	bPending = false;
	if (UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
	{
		Sim->OnSimEvent.Remove(EventHandle);
		Sim->Dirty().OnDirty.Remove(DirtyHandle);
		Sim->OnWorldChanged.Remove(WorldHandle);
	}
	if (GActiveSky.Get() == this) GActiveSky.Reset();
	Super::EndPlay(Reason);
}

// ---------------------------------------------------------------------------------------------- setup

void ADmSky::SetupClouds()
{
	const double T0 = FPlatformTime::Seconds();
	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const dm::World* W = Sim ? Sim->Sim() : nullptr;

	// km: Earth's ~1.5 km base and ~10 km tops scaled by S. The layer is measured from the ATMOSPHERE's ground (UE
	// ignores the cloud component's planet radius when a SkyAtmosphere exists), so -atmodrop= is compensated here.
	Clouds->SetLayerBottomAltitude(float(0.25 + AtmoDropKm));
	Clouds->SetLayerHeight(1.6f);
	Clouds->SetTracingStartMaxDistance(float(DmCoords::RadiusKm * 20.0));
	Clouds->SetTracingMaxDistance(float(DmCoords::RadiusKm * 3.0));
	Clouds->SetbUsePerSampleAtmosphericLightTransmittance(true);
	Clouds->SetSkyLightCloudBottomOcclusion(0.5f);
	{
		double Ss = 2.0;
		FDmShotArgs::Value(TEXT("-cloudshadowscale="), Ss);
		Clouds->SetShadowViewSampleCountScale(float(Ss));
	}
	Clouds->SetShadowTracingDistance(4.f);

	// static textures: cell noise, 3D noise volumes
	TArray<FColor> Fine;
	FDmSkyWeather::BuildFine(FineW, FineH, WxSeed, Fine);
	FineTex = FDmSkyWeather::MakeTexture(this, Fine, FineW, FineH, TEXT("DmSkyFine"), true);
	const double T1 = FPlatformTime::Seconds();
	ShapeTex = DmSkyNoise::MakeShapeVolume(this, 128, 11);
	DetailTex = DmSkyNoise::MakeDetailVolume(this, 64, 23);
	const double T2 = FPlatformTime::Seconds();

	// climate grids and the weather systems, then the first maps, synchronously, into both slots
	Snapshot.FromWorld(W);
	SeaFraction = Snapshot.bValid ? Snapshot.SeaFraction : 0.7f;
	Weather = MakeShared<FDmSkyWeather, ESPMode::ThreadSafe>();
	Weather->BuildGrids(Snapshot);
	Storms.Reset(WxSeed, *Weather, SunDecl);
	{
		double C[3] = { 15.0, 20.0, 11.0 };
		const int32 NC = ArgList(TEXT("-cyclone="), C, 3);
		if (NC >= 2) Storms.PinTropical(C[0], C[1], C[2]);
	}
	// the sim's storms of a static fixture appear at once (Step with no time passing snaps strengths)
	Storms.Step(0.0, 0.0, 0.0, W, *Weather, SunDecl, Tune.DayLength);
	bStormsReady = true;
	WxParams.Seed = WxSeed;
	WxParams.MorphTime = SkyTime;
	WxParams.ItczLatDeg = 5.0 + 5.0 * SunDecl / Tune.TiltDeg;
	WxParams.CoverScale = CoverArg;
	TArray<FDmSystemSnap> Sys;
	if (WantSystems(W)) Storms.Snapshot(Sys);
	Weather->BuildMaps(WxParams, Sys, LastBase, LastWarp, false);
	LastMorph = SkyTime;
	LastBuildReal = 0.0;
	LastFingerprint = Storms.Fingerprint();
	bClimateDirty = false;
	LastClimateReal = 0.0;
	for (int32 K = 0; K < 2; ++K)
	{
		WeatherTex[K] = FDmSkyWeather::MakeTexture(this, LastBase, WxParams.Width, WxParams.Height, K ? TEXT("DmSkyWeatherB") : TEXT("DmSkyWeatherA"), false);
		WarpTex[K] = FDmSkyWeather::MakeFloatTexture(this, LastWarp, WxParams.Width, WxParams.Height, K ? TEXT("DmSkyWarpB") : TEXT("DmSkyWarpA"));
	}
	Cur = 1;
	Blend = 1.0;
	UE_LOG(LogDmSky, Display, TEXT("DmSky: fine cells %dx%d %.2f s, noise volumes %.2f s, weather maps %dx%d %.2f s (grids from %s, sea %.0f%%)"),
		FineW, FineH, T1 - T0, T2 - T1, WxParams.Width, WxParams.Height, Weather->LastBuildSeconds, Snapshot.bValid ? TEXT("the world") : TEXT("a zonal stand-in"), 100.0 * SeaFraction);
	UE_LOG(LogDmSky, Display, TEXT("DmSky: systems %s"), *Storms.Describe());

	FString MapOut;
	if (FDmShotArgs::Value(TEXT("-mapout="), MapOut)) UE_LOG(LogDmSky, Display, TEXT("DmSky: weather preview %s: %s"), *MapOut, SaveWeatherPreview(MapOut) ? TEXT("written") : TEXT("FAILED"));

	UMaterialInterface* CM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Sky/M_SkyClouds.M_SkyClouds"));
	if (!CM)
	{
		UE_LOG(LogDmSky, Error, TEXT("DmSky: /Game/Sky/M_SkyClouds missing; run Scripts/sky.py"));
		return;
	}
	CloudMID = UMaterialInstanceDynamic::Create(CM, this);
	// the molten genesis variant: the same density plus an emissive glow from the magma below (emission costs
	// several ms on the view pass, so it only exists while the world is molten)
	if (UMaterialInterface* CMM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Sky/M_SkyCloudsMolten.M_SkyCloudsMolten")))
		MoltenMID = UMaterialInstanceDynamic::Create(CMM, this);
	CloudTex(TEXT("WeatherA"), WeatherTex[1 - Cur]);
	CloudTex(TEXT("WeatherB"), WeatherTex[Cur]);
	CloudTex(TEXT("WarpA"), WarpTex[1 - Cur]);
	CloudTex(TEXT("WarpB"), WarpTex[Cur]);
	CloudTex(TEXT("Fine"), FineTex);
	CloudTex(TEXT("ShapeTex"), ShapeTex);
	CloudTex(TEXT("DetailTex"), DetailTex);
	FString CP;
	if (FDmShotArgs::Value(TEXT("-cp="), CP))
	{
		TArray<FString> Items; CP.ParseIntoArray(Items, TEXT(","));
		for (const FString& It : Items)
		{
			FString K, V;
			if (It.Split(TEXT(":"), &K, &V)) { CloudScalar(FName(*K), FCString::Atof(*V)); UE_LOG(LogDmSky, Display, TEXT("DmSky: cloud %s = %s"), *K, *V); }
		}
	}
	Clouds->SetMaterial(CloudMID);
}

void ADmSky::CloudTex(FName Name, UTexture* T)
{
	if (CloudMID) CloudMID->SetTextureParameterValue(Name, T);
	if (MoltenMID) MoltenMID->SetTextureParameterValue(Name, T);
}

void ADmSky::CloudScalar(FName Name, float V)
{
	if (CloudMID) CloudMID->SetScalarParameterValue(Name, V);
	if (MoltenMID) MoltenMID->SetScalarParameterValue(Name, V);
}

void ADmSky::CloudVec(FName Name, const FLinearColor& V)
{
	if (CloudMID) CloudMID->SetVectorParameterValue(Name, V);
	if (MoltenMID) MoltenMID->SetVectorParameterValue(Name, V);
}

void ADmSky::SetupStarsMoonAurora()
{
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* SM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Sky/M_SkyStars.M_SkyStars"));
	if (Sphere && SM && !FDmShotArgs::Param(TEXT("nostars")))
	{
		Stars->SetStaticMesh(Sphere);
		Stars->SetWorldScale3D(FVector(1.0e7));   // basic sphere radius 50 uu -> 5,000 km, world-fixed (the planet turns, the stars do not)
		StarMID = UMaterialInstanceDynamic::Create(SM, this);
		double Bright = 3.0;
		FDmShotArgs::Value(TEXT("-stars="), Bright);
		StarBright = Bright;
		StarMID->SetScalarParameterValue(TEXT("StarBrightness"), float(Bright));
		Stars->SetMaterial(0, StarMID);
	}
	else Stars->SetVisibility(false);

	if (UMaterialInterface* MM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Sky/M_SkyMoon.M_SkyMoon")); MM && !FDmShotArgs::Param(TEXT("nomoon")))
	{
		MakeSphere(Moon, 64, 128);
		MoonMID = UMaterialInstanceDynamic::Create(MM, this);
		Moon->SetMaterial(0, MoonMID);
		Moon->SetWorldScale3D(FVector((bMoonOverride ? MoonArgs[3] : Tune.MoonRadiusKm) * DmCoords::UUPerKm));
		Moon->SetWorldRotation(FRotator(10, 40, 0));
	}
	else { Moon->SetVisibility(false); Tune.MoonLux = 0.0; }

	if (UMaterialInterface* AM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Sky/M_SkyAurora.M_SkyAurora")); AM && !FDmShotArgs::Param(TEXT("noaurora")))
	{
		MakeAuroraRings(Aurora);
		AuroraMID = UMaterialInstanceDynamic::Create(AM, this);
		Aurora->SetMaterial(0, AuroraMID);
		Aurora->SetMaterial(1, AuroraMID);
	}
	else Aurora->SetVisibility(false);

	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UMaterialInterface* FM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Sky/M_SkyFlash.M_SkyFlash"));
	for (int32 I = 0; I < FDmSkyStorms::FlashSlots; ++I)
		if (Plane && FM)
		{
			FlashDisc[I]->SetStaticMesh(Plane);
			FlashMID[I] = UMaterialInstanceDynamic::Create(FM, this);
			FlashMID[I]->SetScalarParameterValue(TEXT("Seed"), 0.37f * I);
			FlashDisc[I]->SetMaterial(0, FlashMID[I]);
		}
}

void ADmSky::UpdateFlashes()
{
	// a lightning stroke lights the cloud around it: an additive glow laid just above the cloud tops
	const FQuat Q = FRotator(0.0, FMath::RadiansToDegrees(Yaw), 0.0).Quaternion();
	for (int32 I = 0; I < FDmSkyStorms::FlashSlots; ++I)
	{
		if (!FlashMID[I]) continue;
		const FDmFlash& F = Storms.Flashes[I];
		const double In = F.Intensity();
		if (In < 0.01 || bNoClouds) { if (FlashDisc[I]->IsVisible()) FlashDisc[I]->SetVisibility(false); continue; }
		const FVector Up = Q.RotateVector(F.Dir);
		const double Night = Smooth(0.1, -0.15, FVector::DotProduct(Up, SunWorld));
		const double RadKm = 3.0 + 2.5 * FMath::Frac(F.Dir.X * 977.0);   // one storm cell lit from inside
		FlashDisc[I]->SetWorldLocationAndRotation(Up * (SeaRadiusKm + 1.95) * DmCoords::UUPerKm, FRotationMatrix::MakeFromZ(Up).ToQuat());
		FlashDisc[I]->SetWorldScale3D(FVector(RadKm * DmCoords::UUPerKm / 50.0));
		// the night side is shown about 2.6 EV brighter (UpdateGrade): keep the flash from clipping into a flat disc
		FlashMID[I]->SetScalarParameterValue(TEXT("Intensity"), float(In * FlashGain * (0.25 + 0.75 * Night) / (1.0 + 4.0 * Night)));
		if (!FlashDisc[I]->IsVisible()) FlashDisc[I]->SetVisibility(true);
	}
}

// ---------------------------------------------------------------------------------------------- weather

void ADmSky::StartWeather(bool bSync)
{
	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const dm::World* W = Sim ? Sim->Sim() : nullptr;
	FDmWeatherParams Pm = WxParams;
	Pm.Seed = WxSeed;
	Pm.MorphTime = SkyTime;
	Pm.ItczLatDeg = 5.0 + 5.0 * SunDecl / Tune.TiltDeg;
	Pm.CoverScale = CoverArg;
	LastMorph = SkyTime;
	LastBuildReal = RealTime;
	LastFingerprint = Storms.Fingerprint();
	TArray<FDmSystemSnap> Sys;
	if (WantSystems(W)) Storms.Snapshot(Sys);
	const bool bGrids = bClimateDirty;
	TSharedPtr<FDmClimateSnapshot, ESPMode::ThreadSafe> Snap;
	if (bGrids)
	{
		Snap = MakeShared<FDmClimateSnapshot, ESPMode::ThreadSafe>();
		Snap->FromWorld(W);
		bClimateDirty = false;
		LastClimateReal = RealTime;
	}
	TSharedPtr<FDmSkyWeather, ESPMode::ThreadSafe> Old = Weather;
	auto Job = [Pm, Snap, Old, Sys = MoveTemp(Sys)]() -> TSharedPtr<FBuildResult, ESPMode::ThreadSafe>
	{
		const double T0 = FPlatformTime::Seconds();
		TSharedPtr<FBuildResult, ESPMode::ThreadSafe> R = MakeShared<FBuildResult, ESPMode::ThreadSafe>();
		TSharedPtr<FDmSkyWeather, ESPMode::ThreadSafe> Wx = Old;
		if (Snap.IsValid())
		{
			Wx = MakeShared<FDmSkyWeather, ESPMode::ThreadSafe>();
			Wx->BuildGrids(*Snap);
			R->Weather = Wx;
			R->SeaFraction = Snap->bValid ? Snap->SeaFraction : 0.7f;
		}
		if (Wx.IsValid())
		{
			Wx->BuildMaps(Pm, Sys, R->Pixels, R->Warp, true);
			// upload copies made here, so the game thread only queues them
			const int64 CB = int64(R->Pixels.Num()) * 4, WB = int64(R->Warp.Num()) * 8;
			R->CoverUpload = FMemory::Malloc(CB); FMemory::Memcpy(R->CoverUpload, R->Pixels.GetData(), CB);
			R->WarpUpload = FMemory::Malloc(WB); FMemory::Memcpy(R->WarpUpload, R->Warp.GetData(), WB);
		}
		R->MorphTime = Pm.MorphTime;
		R->Seconds = FPlatformTime::Seconds() - T0;
		return R;
	};
	if (bSync)
	{
		TPromise<TSharedPtr<FBuildResult, ESPMode::ThreadSafe>> Pr;
		Pending = Pr.GetFuture();
		Pr.SetValue(Job());
	}
	else Pending = Async(EAsyncExecution::ThreadPool, MoveTemp(Job));
	bPending = true;
}

void ADmSky::PumpWeather(double RealDt)
{
	if (!CloudMID) return;
	// crossfade over about one rebuild interval of real time (half a second while time stands still)
	const double BlendDur = SkyRate > 0.05 ? FMath::Clamp(Tune.WeatherInterval / SkyRate, 0.4, 4.0) : 0.5;
	Blend = FMath::Min(1.0, Blend + RealDt / BlendDur);
	if (bPending && Pending.IsReady() && Blend >= 1.0)
	{
		TSharedPtr<FBuildResult, ESPMode::ThreadSafe> R = Pending.Get();
		bPending = false;
		const int32 Np = WxParams.Width * WxParams.Height;
		if (R.IsValid() && R->Pixels.Num() == Np && R->Warp.Num() == Np)
		{
			const int32 Next = 1 - Cur;
			FDmSkyWeather::UploadMip0(WeatherTex[Next], R->CoverUpload, WxParams.Width, WxParams.Height, 4);
			FDmSkyWeather::UploadMip0(WarpTex[Next], R->WarpUpload, WxParams.Width, WxParams.Height, 8);
			R->CoverUpload = R->WarpUpload = nullptr;
			CloudTex(TEXT("WeatherA"), WeatherTex[Cur]);
			CloudTex(TEXT("WeatherB"), WeatherTex[Next]);
			CloudTex(TEXT("WarpA"), WarpTex[Cur]);
			CloudTex(TEXT("WarpB"), WarpTex[Next]);
			Cur = Next;
			Blend = 0.0;
			LastBase = MoveTemp(R->Pixels);
			LastWarp = MoveTemp(R->Warp);
			++MapsBuilt; MapSecondsSum += R->Seconds; MapSecondsMax = FMath::Max(MapSecondsMax, R->Seconds);
			if (bTrace) { UE_LOG(LogDmSky, Display, TEXT("DmSkyTrace: map %lld into slot %d: %s"), MapsBuilt, Next, *Storms.Describe()); }
			if (R->Weather.IsValid())
			{
				Weather = R->Weather;
				SeaFraction = R->SeaFraction;
				if (!bStormsReady) { Storms.Reset(WxSeed, *Weather, SunDecl); bStormsReady = true; }
			}
			if (RealTime - LastLog > 10.0)
			{
				LastLog = RealTime;
				UE_LOG(LogDmSky, Display, TEXT("DmSky: weather maps #%lld (sky %.1f s) built in %.2f s (mean %.3f, max %.3f)%s; systems %s"), MapsBuilt, R->MorphTime, R->Seconds,
					MapSecondsSum / FMath::Max<int64>(1, MapsBuilt), MapSecondsMax, R->Weather.IsValid() ? TEXT(" with new climate grids") : TEXT(""), *Storms.Describe());
			}
		}
		if (R.IsValid()) { FMemory::Free(R->CoverUpload); FMemory::Free(R->WarpUpload); }
	}
	// rebuild on sky time, when a system moved while time stands still (rain, a sim storm), or on a climate change
	const bool bWant = (SkyTime - LastMorph >= Tune.WeatherInterval)
		|| (RealTime - LastBuildReal >= 0.5 && Storms.Fingerprint() != LastFingerprint)
		|| (bClimateDirty && RealTime - LastClimateReal > 10.0);
	if (!bPending && bWant) StartWeather(false);
}

FLinearColor ADmSky::CoverAt(const FVector& PlanetDir) const
{
	const int32 W = WxParams.Width, H = WxParams.Height;
	if (LastBase.Num() != W * H) return FLinearColor::Transparent;
	const FVector D = PlanetDir.GetSafeNormal();
	const double U = FMath::Atan2(D.Y, D.X) / (2.0 * UE_DOUBLE_PI) + 0.5, V = 0.5 - FMath::Asin(FMath::Clamp(D.Z, -1.0, 1.0)) / UE_DOUBLE_PI;
	const int32 X = FMath::Clamp(int32(U * W), 0, W - 1), Y = FMath::Clamp(int32(V * H), 0, H - 1);
	const FColor C = LastBase[Y * W + X];
	return FLinearColor(C.R / 255.f, C.G / 255.f, C.B / 255.f, C.A / 255.f);
}

bool ADmSky::SaveWeatherPreview(const FString& Path) const
{
	return Weather.IsValid() && Weather->SavePreview(Path, LastBase, LastWarp, WxParams.Width, WxParams.Height);
}

// ---------------------------------------------------------------------------------------------- per frame

double ADmSky::SeasonDeclDeg(const dm::World* W) const
{
	const double Year = W ? W->year : 0.0;
	return Tune.TiltDeg * FMath::Sin(2.0 * UE_DOUBLE_PI * (Year / FMath::Max(1.0, Tune.SeasonYears) + 0.08));
}

bool ADmSky::WantSystems(const dm::World* W) const
{
	// a molten world has no seas and no weather systems: only its ash veil (no hurricanes over magma)
	return !bNoCyclones && !(W && W->phase == dm::Phase::Molten);
}

double ADmSky::CameraAltKm(const FVector& CamPos) const
{
	return (CamPos.Size() / DmCoords::UUPerKm) - SeaRadiusKm;
}

void ADmSky::UpdateTime(UDmSimSubsystem* Sim, double RealDt)
{
	const dm::World* W = Sim->Sim();
	SkyRate = ForcedRate >= 0.0 ? ForcedRate : FMath::Min(Sim->GetTickRate() / UDmSimSubsystem::TicksPerSecondAt1x, Tune.SpinRateCap);
	SkyDt = RealDt * SkyRate;
	SkyTime += SkyDt;
	if (Sim->IsSunPinned())
	{
		// pinned: the time of day is frozen (the planet does not turn under the scenario's sun)
		SunWorld = Sim->GetSunDirection();
		bWasPinned = true;
	}
	else
	{
		if (bWasPinned)
		{
			// resume from wherever the sun was (no jump)
			SunWorld = Sim->GetSunDirection().GetSafeNormal();
			SunAzimuth = FMath::Atan2(SunWorld.Y, SunWorld.X);
			SunDecl = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(SunWorld.Z, -1.0, 1.0)));
			bWasPinned = false;
		}
		Yaw -= SkyDt * 2.0 * UE_DOUBLE_PI / Tune.DayLength;   // eastward: the sim's longitude grows westward
		Yaw = FMath::UnwindRadians(Yaw);
		SunDecl = RealDt > 0.0 ? FMath::FInterpTo(SunDecl, SeasonDeclDeg(W), RealDt, 0.6) : SunDecl;
		const double Dc = SunDecl * Deg;
		SunWorld = FVector(FMath::Cos(Dc) * FMath::Cos(SunAzimuth), FMath::Cos(Dc) * FMath::Sin(SunAzimuth), FMath::Sin(Dc));
		Sim->SetSunDirection(SunWorld);
	}
	Sim->SetPlanetToWorld(FTransform(FRotator(0.0, FMath::RadiansToDegrees(Yaw), 0.0)));
}

void ADmSky::UpdateAtmosphere(const dm::World* W)
{
	const double Heat = MoltenHeat(W);
	const double Dust = W ? FMath::Clamp(W->dust, 0.0, 1.0) : 0.0;
	// Earth's coefficients times 1/S; a molten world or a dusty one gets a thick, reddish haze
	const double Haze = 1.0 + 5.0 * Heat + 3.0 * Dust;
	Atmosphere->SetRayleighScatteringScale(float(0.0331 / S * (1.0 - 0.45 * Heat)));
	Atmosphere->SetMieScatteringScale(float(0.003996 / S * Haze));
	Atmosphere->SetMieAbsorptionScale(float(0.000444 / S * (1.0 + 10.0 * Heat + 4.0 * Dust)));
	Atmosphere->SetMieAbsorption(FMath::Lerp(FLinearColor(1.f, 1.f, 1.f), FLinearColor(0.35f, 0.75f, 1.0f), float(Heat)));
	Atmosphere->SetOtherAbsorptionScale(float(0.001881 / S));
	// a molten world's air: smoky, scattering orange instead of blue
	Atmosphere->SetRayleighScattering(FMath::Lerp(FLinearColor(0.175287f, 0.409607f, 1.0f), FLinearColor(0.55f, 0.36f, 0.24f), float(Heat)));
	Atmosphere->SetMieScattering(FMath::Lerp(FLinearColor::White, FLinearColor(1.0f, 0.62f, 0.4f), float(FMath::Max(Heat, 0.6 * Dust))));
	if (W && W->seaLevel != LastSeaLevel)
	{
		LastSeaLevel = W->seaLevel;
		SeaRadiusKm = DmCoords::HeightToRadiusUU(W->seaLevel) / DmCoords::UUPerKm;
		Atmosphere->SetBottomRadius(float(SeaRadiusKm - AtmoDropKm));
		Clouds->SetPlanetRadius(float(SeaRadiusKm));
	}
	else if (!W)
	{
		Atmosphere->SetBottomRadius(float(SeaRadiusKm - AtmoDropKm));
		Clouds->SetPlanetRadius(float(SeaRadiusKm));
	}
}

void ADmSky::UpdateLights(UDmSimSubsystem* Sim, const FVector& CamPos)
{
	const dm::World* W = Sim->Sim();
	const double Dust = W ? FMath::Clamp(W->dust, 0.0, 1.0) : 0.0;
	Sun->SetWorldRotation((-SunWorld).Rotation());
	Sun->SetIntensity(float(Tune.SunLux * (1.0 - 0.45 * Dust)));

	// cloud shadow map: centred under the camera, sized by altitude (blurred and displaced if too wide near the ground)
	const double Alt = FMath::Max(0.0, CameraAltKm(CamPos));
	const float Ext = float(FMath::Clamp(Alt * 1.2 + 8.0, 12.0, 130.0));
	// from orbit the shadows are a few pixels wide: half the shadow-map resolution and ray samples there
	// (above 200 km a 512^2 map with half the rays: 2.2 -> 0.4 ms at the terminator, mean image change 0.5 grey levels)
	const float ResScale = Alt > 200.0 ? float(OrbitShadow[0]) : (Alt > 80.0 ? 2.f : 4.f);
	const float RayScale = Alt > 200.0 ? float(OrbitShadow[1]) : (Alt > 80.0 ? 1.f : 2.f);
	if (LastShadowExtent < 0.f || FMath::Abs(Ext - LastShadowExtent) > 0.05f * LastShadowExtent || ResScale != Sun->CloudShadowMapResolutionScale || RayScale != Sun->CloudShadowRaySampleCountScale)
	{
		LastShadowExtent = Ext;
		Sun->CloudShadowExtent = Ext;
		Sun->CloudShadowMapResolutionScale = ResScale;
		Sun->CloudShadowRaySampleCountScale = RayScale;
		Sun->MarkRenderStateDirty();
	}

	// moonlight: only when the camera looks at the night side (a second atmospheric light costs 2-5 ms)
	const FVector Up = CamPos.GetSafeNormal();
	const double Night = Smooth(0.15, -0.35, FVector::DotProduct(Up, SunWorld));
	const double Illum = 0.5 * (1.0 - FVector::DotProduct(SunWorld, MoonDirWorld));   // full when opposite the sun
	// The night side must stay readable at every moon phase (old SPEC-RENDER 0): the fill never drops below 40 % and
	// comes from the night side's sky even at new moon (the real moon is then on the day side and lights nothing there)
	const double Phase = 0.4 + 0.6 * Illum;
	const double Lux = Tune.MoonLux * Phase * Night;
	MoonAmbient = Tune.MoonLux * Phase / FMath::Max(0.1, Tune.SunLux);
	const FVector FillDir = (MoonDirWorld * Illum - SunWorld * (1.5 - Illum)).GetSafeNormal();
	if (Lux > 0.003)
	{
		if (!MoonLight->IsVisible()) MoonLight->SetVisibility(true);
		MoonLight->SetIntensity(float(Lux));
		MoonLight->SetWorldRotation((-FillDir).Rotation());
	}
	else if (MoonLight->IsVisible()) MoonLight->SetVisibility(false);
}

void ADmSky::UpdateClouds(const dm::World* W, const FVector& CamPos, double RealDt)
{
	if (!CloudMID) return;
	// sample budget by altitude (docs/lab/clouds.md 1.4): thin 1.6 km layer, so short ray distances
	const double Alt = FMath::Max(0.0, CameraAltKm(CamPos));
	// from orbit a traced pixel spans hundreds of metres: fewer samples are enough (TSR resolves the rest)
	// Continuous in altitude (a step pops the cloud grain): 2x only right under / inside the 1.6 km layer, where few
	// samples per km show as grain from below; 1x from 8 km (measured at 9 km: 2x cost 7-11 ms of view pass, 1x 4 ms,
	// mean image change 1 grey level); the orbit scale far out.
	const double NearT = FMath::Clamp((Alt - 2.0) / 6.0, 0.0, 1.0), FarT = FMath::Clamp((Alt - 120.0) / 130.0, 0.0, 1.0);
	float ViewScale = float(FMath::Lerp(FMath::Lerp(2.0, 1.0, NearT), OrbitScale, FarT));
	ViewScale = float(FMath::RoundToDouble(ViewScale * 20.0) / 20.0);   // quantised: no per-frame component update while the camera moves
	if (ForceViewScale > 0.0) ViewScale = float(ForceViewScale);
	if (ViewScale != LastViewScale) { LastViewScale = ViewScale; Clouds->SetViewSampleCountScale(ViewScale); }
	const float Dist = float(FMath::Clamp(Alt * 0.3, 3.0, 15.0));
	if (FMath::Abs(Dist - LastSampleDist) > 0.25f) { LastSampleDist = Dist; SetCVar(TEXT("r.VolumetricCloud.DistanceToSampleMaxCount"), Dist); }

	// weather systems (drawn into the next weather maps)
	const double Decl = SunDecl;
	if (bStormsReady && Weather.IsValid()) Storms.Step(SkyDt, RealDt, SkyRate, W, *Weather, Decl, Tune.DayLength);

	// genesis and cloudiness: a molten world has a thin, dark ash veil glowing from below; while the seas
	// fill, clouds thicken with the ocean (old SPEC-RENDER uFx.y)
	const double Heat = MoltenHeat(W);
	const bool bGenesis = W && (W->phase == dm::Phase::Molten || W->phase == dm::Phase::Water || W->phase == dm::Phase::Life || W->phase == dm::Phase::People);
	double Cover = 1.0;
	if (bGenesis) Cover = FMath::Lerp(0.3, 1.0, Smooth(0.1, 0.6, double(SeaFraction)));
	if (W && W->phase == dm::Phase::Molten) Cover = FMath::Lerp(Cover, 0.75, Heat);
	CloudScalar(TEXT("Yaw"), float(Yaw));
	CloudScalar(TEXT("SkyTime"), float(FMath::Fmod(SkyTime, 86400.0)));
	CloudScalar(TEXT("Blend"), float(Blend));
	CloudScalar(TEXT("CoverScale"), float(Cover));
	CloudScalar(TEXT("Heat"), float(Heat));
	// moonlit night clouds: the material's ambient on the night side, a little blue (NightAmb scales SkyAmb)
	// (scaled by how much the view is a night view: the same ambient beside the lit day side, at no exposure lift,
	// turned the night half of a terminator shot into white daylight clouds)
	CloudScalar(TEXT("NightAmb"), float(FMath::Clamp(MoonAmbient * NightAmbGain * (0.12 + 0.88 * NightView), 0.0, 0.75)));   // moonlit clouds outshine the ground
	// water cloud a touch cool (sunlight through the scaled air yellows the tops); a molten world's ash veil warm grey
	const FLinearColor Alb = FMath::Lerp(FLinearColor(0.93f, 0.955f, 1.0f, 1.f), FLinearColor(0.32f, 0.31f, 0.30f, 1.f), float(Heat));
	CloudVec(TEXT("Albedo"), Alb);
	UMaterialInterface* Want = (Heat > 0.01 && MoltenMID) ? static_cast<UMaterialInterface*>(MoltenMID) : static_cast<UMaterialInterface*>(CloudMID);
	if (Clouds->GetMaterial() != Want) Clouds->SetMaterial(Want);
}

void ADmSky::SetMoonInView(double AzDeg, double ElDeg, double DistR, double RadiusKm)
{
	bMoonOverride = DistR > 0.0;
	MoonArgs[0] = AzDeg; MoonArgs[1] = ElDeg; MoonArgs[2] = DistR; MoonArgs[3] = RadiusKm;
	if (Moon) Moon->SetWorldScale3D(FVector((bMoonOverride ? MoonArgs[3] : Tune.MoonRadiusKm) * DmCoords::UUPerKm));
}

void ADmSky::UpdateMoon(const FVector& CamPos, const FRotator& CamRot)
{
	FVector Pos;
	if (bMoonOverride)
	{
		const FVector Dir = CamRot.RotateVector(FRotator(MoonArgs[1], MoonArgs[0], 0.0).Vector());
		Pos = CamPos + Dir * (MoonArgs[2] * DmCoords::RadiusUU);
	}
	else
	{
		const double Th = 1.3 + SkyTime * 2.0 * UE_DOUBLE_PI / (Tune.MoonPeriodDays * Tune.DayLength);
		const double I = Tune.MoonInclDeg * Deg;
		Pos = FVector(FMath::Cos(Th), FMath::Sin(Th) * FMath::Cos(I), FMath::Sin(Th) * FMath::Sin(I)) * (Tune.MoonDistR * DmCoords::RadiusUU);
	}
	MoonDirWorld = Pos.GetSafeNormal();
	if (MoonMID) MoonMID->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(float(SunWorld.X), float(SunWorld.Y), float(SunWorld.Z), 0.f));
	Moon->SetWorldLocation(Pos);
}

void ADmSky::UpdateGrade(const dm::World* W, const FVector& CamPos, double RealDt)
{
	// mood of the world (History): pop-weighted devotion and love of the living settlements -> sim::mood
	MoodTimer -= RealDt;
	if (W && MoodTimer <= 0.0)
	{
		MoodTimer = 2.0;
		TargetWarm = 0.0; TargetSat = 1.0; TargetContrast = 1.0;
		if (W->phase == dm::Phase::History || W->phase == dm::Phase::Ended)
		{
			double Pop = 0, Dev = 0, Love = 0;
			for (const dm::Settlement& St : W->settlements)
			{
				if (St.dead || !(St.pop > 0)) continue;
				Pop += St.pop; Dev += St.pop * St.devotion; Love += St.pop * St.love;
			}
			if (Pop > 0)
			{
				switch (dm::sim::mood(Dev / Pop, Love / Pop))
				{
				case dm::Mood::Beloved: TargetWarm = 250.0; TargetSat = 1.06; break;
				case dm::Mood::Feared: TargetWarm = -300.0; TargetSat = 0.86; TargetContrast = 1.07; break;
				case dm::Mood::Forgotten: TargetSat = 0.9; break;
				case dm::Mood::Ash: TargetWarm = -150.0; TargetSat = 0.72; TargetContrast = 1.05; break;
				default: break;
				}
			}
		}
	}
	MoodWarm = FMath::FInterpTo(MoodWarm, TargetWarm, RealDt, 0.4);
	MoodSat = FMath::FInterpTo(MoodSat, TargetSat, RealDt, 0.4);
	MoodContrast = FMath::FInterpTo(MoodContrast, TargetContrast, RealDt, 0.4);

	// time of day near the ground: night needs more exposure (and reads cooler), golden hour warms
	const double Alt = FMath::Max(0.0, CameraAltKm(CamPos));
	const double Low = 1.0 - Smooth(25.0, 160.0, Alt);
	const double E = FVector::DotProduct(CamPos.GetSafeNormal(), SunWorld);   // sine of the sun's elevation under the camera
	// the night side must stay readable (old SPEC-RENDER 0): lift exposure whenever the camera looks at it
	const double Night = FMath::Lerp(0.75, 1.0, Low) * (1.0 - Smooth(-0.3, 0.02, E));
	const double Golden = Low * Smooth(-0.06, 0.04, E) * (1.0 - Smooth(0.12, 0.4, E));
	const double Heat = MoltenHeat(W);

	FPostProcessSettings& P = Post->Settings;
	NightView = Night;
	const double Lift = Tune.NightEv * Night + 0.6 * Heat;
	P.AutoExposureBias = float(Ev + Lift);
	// the sky behind the planet is not part of the night side: keep the star field and the milky way from rising with
	// the night exposure (at +2 EV the galactic band turned into grey smoke across the black)
	if (StarMID && FMath::Abs(Lift - LastStarLift) > 0.02)
	{
		LastStarLift = Lift;
		StarMID->SetScalarParameterValue(TEXT("StarBrightness"), float(StarBright * FMath::Pow(2.0, -0.75 * Lift)));
		StarMID->SetScalarParameterValue(TEXT("Galaxy"), float(FMath::Pow(2.0, -Lift)));
	}
	P.WhiteTemp = float(6500.0 + MoodWarm + 900.0 * Golden - 700.0 * Night + 900.0 * Heat);
	const float Sat = float(MoodSat * (1.0 + 0.08 * Golden - 0.25 * Night));
	P.ColorSaturation = FVector4(Sat, Sat, Sat, 1.0);
	const float Con = float(MoodContrast * (1.0 + 0.04 * Heat));
	P.ColorContrast = FVector4(Con, Con, Con, 1.0);
	P.BloomIntensity = float(0.35 + 0.3 * Heat);
}

void ADmSky::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double TickT0 = FPlatformTime::Seconds();
	++Frames;
#if WITH_EDITOR
	// editor binaries in -game compile the cloud / star shaders on first use, asynchronously: block on them in
	// the first frames so a shot never shows the engine's fallback material (docs/lab/clouds.md pitfall 2)
	if (Frames <= 30 && GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
	{
		const double C0 = FPlatformTime::Seconds();
		const int32 Jobs = GShaderCompilingManager->GetNumRemainingJobs();
		GShaderCompilingManager->FinishAllCompilation();
		UE_LOG(LogDmSky, Display, TEXT("DmSky: frame %d waited %.1f s for %d shader jobs"), Frames, FPlatformTime::Seconds() - C0, Jobs);
	}
#endif
	if (Frames == 200 && FDmShotArgs::Param(TEXT("skyprofile"))) GEngine->Exec(GetWorld(), TEXT("ProfileGPU"));
	if (bTrace && Frames % 15 == 0)
		UE_LOG(LogDmSky, Display, TEXT("DmSkyTrace: frame %d sky %.4f rate %.3f yaw %.5f blend %.3f cur %d maps %lld pending %d fp %u"), Frames, SkyTime, SkyRate, Yaw, Blend, Cur, MapsBuilt, bPending ? 1 : 0, Storms.Fingerprint());
	const double RealDt = FMath::Clamp(double(DeltaSeconds), 0.0, 0.25);
	RealTime += RealDt;
	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	if (!Sim) return;
	const dm::World* W = Sim->Sim();

	UpdateTime(Sim, RealDt);
	FVector CamPos(8.0e7, 0, 0);
	FRotator CamRot(0, 180, 0);
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		if (PC->PlayerCameraManager)
		{
			CamPos = PC->PlayerCameraManager->GetCameraLocation();
			CamRot = PC->PlayerCameraManager->GetCameraRotation();
		}
	UpdateAtmosphere(W);
	UpdateMoon(CamPos, CamRot);
	UpdateLights(Sim, CamPos);
	if (!bNoClouds)
	{
		PumpWeather(RealDt);
		UpdateClouds(W, CamPos, RealDt);
		UpdateFlashes();
	}

	// stars: comet rain while the seas are being filled
	const double RainTarget = (W && W->phase == dm::Phase::Water) ? 1.0 : 0.0;
	CometRain = FMath::FInterpTo(CometRain, RainTarget, RealDt, 0.5);
	if (StarMID) StarMID->SetScalarParameterValue(TEXT("CometRain"), float(CometRain));

	// aurora turns with the planet, glows on the night side
	if (AuroraMID)
	{
		Aurora->SetWorldRotation(FRotator(0.0, FMath::RadiansToDegrees(Yaw), 0.0));
		AuroraBoost = FMath::Max(0.0, AuroraBoost - AuroraBoostRate * RealDt);
		AuroraMID->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(float(SunWorld.X), float(SunWorld.Y), float(SunWorld.Z), 0.f));
		AuroraMID->SetScalarParameterValue(TEXT("Strength"), float(Tune.AuroraGain * (1.0 + 3.0 * AuroraBoost)));
	}
	UpdateGrade(W, CamPos, RealDt);
	const double TickMs = (FPlatformTime::Seconds() - TickT0) * 1000.0;
	TickMsMax = FMath::Max(TickMsMax, TickMs);
	if (bTrace && TickMs > 4.0) UE_LOG(LogDmSky, Display, TEXT("DmSkyTrace: frame %d sky tick %.1f ms (maps %lld)"), Frames, TickMs, MapsBuilt);
}

// ---------------------------------------------------------------------------------------------- events

void ADmSky::OnSimEvent(const dm::Event& E)
{
	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const dm::World* W = Sim ? Sim->Sim() : nullptr;
	if (const dm::EvLightning* L = E.as<dm::EvLightning>())
	{
		if (W) Storms.Strike(DmCoords::CellDir(*W, L->cell), 1.0);
	}
	else if (const dm::EvPowerUsed* P = E.as<dm::EvPowerUsed>())
	{
		if (P->id == "rain") Storms.AddRain(P->cell, P->ang, W);
	}
}

void ADmSky::OnDirty(const FDmDirtyFlags& F)
{
	if (F.bColor || F.bWater || F.bElev) bClimateDirty = true;
}

void ADmSky::OnWorldChanged(const dm::World* W)
{
	bClimateDirty = true;
	LastClimateReal = -1e9;
	bStormsReady = false;
	LastSeaLevel = 1e30;
	if (W && !bSeedArg) WxSeed = W->seed * 31u + 7u;
}

void ADmSky::Strike(int32 Cell, float Gain)
{
	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	if (Sim && Sim->Sim()) Storms.Strike(DmCoords::CellDir(*Sim->Sim(), Cell), Gain);
}

void ADmSky::BoostAurora(float Amount, float Seconds)
{
	AuroraBoost = FMath::Max(AuroraBoost, double(Amount));
	AuroraBoostRate = AuroraBoost / FMath::Max(1.0, double(Seconds));
}

// ---------------------------------------------------------------------------------------------- scene part

static FDmScenePartRegistrar GDmSkyPart(TEXT("sky"), EDmPartKind::World, 0,
	[](const FDmSceneContext& Ctx) -> AActor*
	{
		return Ctx.World ? Ctx.World->SpawnActor<ADmSky>() : nullptr;
	}, TEXT("Sky/DmSkyActor"));
