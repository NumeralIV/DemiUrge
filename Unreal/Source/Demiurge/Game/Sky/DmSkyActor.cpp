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
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
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
	// (integration) per-pixel atmosphere transmittance: without it one transmittance (the one at the planet
	// centre's view) tints every land and sea pixel seen from orbit pink / tan under a low sun
	Sun->bPerPixelAtmosphereTransmittance = true;
	Sun->ForwardShadingPriority = 1;   // (integration) the sun wins forward shading over the moonlight (on-screen warning)
	Sun->SetCastShadows(true);
	Sun->SetLightSourceAngle(0.53f);
	Sun->bCastCloudShadows = true;
	Sun->CloudShadowStrength = 1.f;
	// (wave 4) 0.85: a cloud shadow keeps a little sun, so it reads as shade (green, a little cool), not as a navy hole lit only
	// by the strong blue of the scaled sky (shots/ue-power-volcano.png)
	Sun->CloudShadowOnSurfaceStrength = 0.85f;
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

	// Sky fill (wave 3): without a sky light nothing fills the shadows (Lumen gathers no sky), so low-sun shadows went
	// black. A real-time capture renders only the sky (atmosphere and clouds) from its own position, so its capture point
	// is kept on the ground the camera looks at (UpdateFill): it sees that place's sky, which is dark on the night side.
	// A capture from orbit sees the sunlit atmosphere and would light the night side (docs/lab/clouds.md pitfall 4), so
	// the fill fades out with altitude.
	Fill = CreateDefaultSubobject<USkyLightComponent>(TEXT("Fill"));
	Fill->SetupAttachment(RootComponent);
	Fill->SetMobility(EComponentMobility::Movable);
	Fill->bRealTimeCapture = true;
	Fill->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	Fill->bLowerHemisphereIsBlack = true;     // the ground below is real geometry (Lumen bounces it); do not count it twice
	Fill->bCloudAmbientOcclusion = false;
	Fill->CubemapResolution = 64;             // diffuse fill and rough sea reflections: 64^2 faces are plenty
	Fill->SetLightColor(FLinearColor(1.0f, 0.95f, 0.9f));   // the scaled air's sky is a strong blue: keep fill-lit shade green, not teal
	Fill->SetIntensity(0.f);
	Fill->SetVisibility(false);

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
	FDmShotArgs::Value(TEXT("-cloudmidscale="), MidScale);
	FDmShotArgs::Value(TEXT("-atmodrop="), AtmoDropKm);
	FDmShotArgs::Value(TEXT("-nightamb="), NightAmbGain);
	ArgList(TEXT("-cloudshadoworbit="), OrbitShadow, 2);
	bTrace = FDmShotArgs::Param(TEXT("skytrace"));
	bWeatherPool = FDmShotArgs::Param(TEXT("skywxpool"));   // (wave 4 A/B) the weather build in the shared thread pool again
	bOldLook = FDmShotArgs::Param(TEXT("skyold"));
	FDmShotArgs::Value(TEXT("-skyfill="), FillGainArg);
	FDmShotArgs::Value(TEXT("-goldenev="), GoldenEv);
	FDmShotArgs::Value(TEXT("-moonnear="), MoonNear);
	bNightV4 = !FDmShotArgs::Param(TEXT("nightv3"));
	FDmShotArgs::Value(TEXT("-nightevnear="), NightEvNear);
	FDmShotArgs::Value(TEXT("-cloudnight="), CloudNightBoost);
	FDmShotArgs::Value(TEXT("-moonnearlux="), MoonNearLux);
	FDmShotArgs::Value(TEXT("-moonminel="), MoonMinEl);
	{ double Mc[3]; if (FDmShotArgs::List(TEXT("-mooncolor="), Mc, 3) == 3) for (int32 K = 0; K < 3; ++K) MoonNearColor[K] = float(Mc[K]); }
	FDmShotArgs::Value(TEXT("-finecubic="), FineCubicArg);
	FDmShotArgs::Value(TEXT("-nighttone="), NightTone);
	if (FDmShotArgs::Param(TEXT("nodusk"))) bFillDuskGate = false;
	FillGain = bOldLook ? 0.0 : FillGainArg;
	FDmShotArgs::Value(TEXT("-apveil="), VeilOrbit);
	FDmShotArgs::Value(TEXT("-apveilnear="), VeilNear);
	FDmShotArgs::Value(TEXT("-apnear="), ApNear);
	bNoClouds = FDmShotArgs::Param(TEXT("noclouds"));
	bNoCyclones = FDmShotArgs::Param(TEXT("nocyclones"));
	bTurnWorld = FDmShotArgs::Param(TEXT("skyturn"));
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
		// (wave 4) The shadow map is traced at half its resolution by the temporal filter and magnified near the ground: one
		// dilation / smoothing pass left hard square shadows (texels of 70-90 m at 5-20 km, known issue 1c). Three passes
		// (the engine allows four) round them off for a fraction of a millisecond on a 768^2 map.
		double Sf = 3.0;
		FDmShotArgs::Value(TEXT("-cloudshadowfilter="), Sf);
		SetCVar(TEXT("r.VolumetricCloud.ShadowMap.SpatialFiltering"), float(Sf));
		// stride over clear air (zero conservative density). Measured: > 1 lands the first sample deep inside dense
		// cloud, whose long shadow ray then darkens whole frontal sheets (sky-front), for no real saving: keep 1
		double Stride = 1.0;
		FDmShotArgs::Value(TEXT("-cloudstride="), Stride);
		SetCVar(TEXT("r.VolumetricCloud.StepSizeOnZeroConservativeDensity"), float(Stride));
	}

	Sun->SetIntensity(float(Tune.SunLux));
	{
		// -pptrans=0: the sun without per-pixel transmittance (before / after comparisons only; see the constructor)
		double Pp = 1.0;
		if (FDmShotArgs::Value(TEXT("-pptrans="), Pp) && Pp <= 0.0) { Sun->bPerPixelAtmosphereTransmittance = false; Sun->MarkRenderStateDirty(); }
	}
	{
		double Cs = 1.0;
		if (FDmShotArgs::Value(TEXT("-cloudshadow="), Cs)) { Sun->CloudShadowOnSurfaceStrength = float(Cs); Sun->bCastCloudShadows = Cs > 0.0; Sun->MarkRenderStateDirty(); }
	}
	Atmosphere->SetAtmosphereHeight(float(60.0 * S));
	{
		// (integration) aerial perspective strength: the scaled atmosphere's full aerial perspective turned every
		// forest teal-grey from orbit (verified against docs/concept/nature/descent-1-400km.png); -apscale= tunes it
		FDmShotArgs::Value(TEXT("-apscale="), ApScale);
		Atmosphere->SetAerialPespectiveViewDistanceScale(float(ApScale));
	}
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
		// a build still in flight: free its upload copies (they are only freed on upload otherwise).
		// (wave3-final) Never wait for it here: the job sits in the shared thread pool behind the planet's patch jobs,
		// and on a busy machine Get() held the game's exit for over 60 s ("did not exit 60 s after it asked to quit"
		// in playthrough, powers-check and screen-flow). The job captures no actor state, so a build that has not
		// finished frees its own copies when it does.
		if (Pending.IsReady())
		{
			TSharedPtr<FBuildResult, ESPMode::ThreadSafe> R = Pending.Get();
			if (R.IsValid()) { FMemory::Free(R->CoverUpload); FMemory::Free(R->WarpUpload); R->CoverUpload = R->WarpUpload = nullptr; }
		}
		else
		{
			Pending.Then([](TFuture<TSharedPtr<FBuildResult, ESPMode::ThreadSafe>> F)
			{
				TSharedPtr<FBuildResult, ESPMode::ThreadSafe> R = F.Get();
				if (R.IsValid()) { FMemory::Free(R->CoverUpload); FMemory::Free(R->WarpUpload); R->CoverUpload = R->WarpUpload = nullptr; }
			});
			UE_LOG(LogDmSky, Display, TEXT("DmSky: EndPlay left a weather build running (not waited for)"));
		}
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
	CloudScalar(TEXT("FineCubic"), float(FineCubicArg));   // (wave 4) -finecubic=0: the old bilinear cell lookup (A/B)
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
	const FQuat Q = FRotator(0.0, FMath::RadiansToDegrees(GetPlanetYaw()), 0.0).Quaternion();
	for (int32 I = 0; I < FDmSkyStorms::FlashSlots; ++I)
	{
		if (!FlashMID[I]) continue;
		const FDmFlash& F = Storms.Flashes[I];
		const double In = F.Intensity();
		if (In < 0.01 || bNoClouds) { if (FlashDisc[I]->IsVisible()) FlashDisc[I]->SetVisibility(false); continue; }
		const FVector Up = Q.RotateVector(F.Dir);
		const double Night = Smooth(0.1, -0.15, FVector::DotProduct(Up, SunWorld));
		// (wave 4) by day a stroke is not seen from above the cloud (the glow drew soft grey-white discs over the hurricane's
		// overcast and, before a storm had formed, over clear sea): night and dusk only, and only over cloud
		const double Over = Smooth(0.25, 0.5, double(CoverAt(F.Dir).R));
		if (Night * Over < 0.02) { if (FlashDisc[I]->IsVisible()) FlashDisc[I]->SetVisibility(false); continue; }
		const double RadKm = 3.0 + 2.5 * FMath::Frac(F.Dir.X * 977.0);   // one storm cell lit from inside
		FlashDisc[I]->SetWorldLocationAndRotation(Up * (SeaRadiusKm + 1.95) * DmCoords::UUPerKm, FRotationMatrix::MakeFromZ(Up).ToQuat());
		FlashDisc[I]->SetWorldScale3D(FVector(RadKm * DmCoords::UUPerKm / 50.0));
		// the night side is shown about 2.6 EV brighter (UpdateGrade): keep the flash from clipping into a flat disc
		FlashMID[I]->SetScalarParameterValue(TEXT("Intensity"), float(In * FlashGain * Night * Over / (1.0 + 4.0 * Night)));
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
	// (wave 4) its own thread at the OS's normal priority (TPri_SlightlyBelowNormal; the build's ParallelFor still lends it idle
	// workers): in the shared thread pool, and on a below-normal thread, the build queued behind the planet's and towns' jobs
	// and never landed on a busy machine (sky-cyclone / sky-check -skytrace: "maps 0 pending 1" for every frame, so a sim
	// storm stayed at its first, zero strength, no cyclone on screen and the weather froze)
	else if (bWeatherPool) Pending = Async(EAsyncExecution::ThreadPool, MoveTemp(Job));
	else Pending = AsyncThread(MoveTemp(Job), 0, TPri_SlightlyBelowNormal);
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
			if (RealTime - LastLog > 10.0 || MapsBuilt == 1)
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
			// the fixed sun of the turning frame: static frame, sun = Rz(-Yaw) x fixed sun, so the fixed sun's azimuth is
			// the current one plus the yaw
			SunAzimuth = FMath::Atan2(SunWorld.Y, SunWorld.X) + (bTurnWorld ? 0.0 : Yaw);
			SunDecl = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(SunWorld.Z, -1.0, 1.0)));
			bWasPinned = false;
		}
		Yaw -= SkyDt * 2.0 * UE_DOUBLE_PI / Tune.DayLength;   // eastward: the sim's longitude grows westward
		Yaw = FMath::UnwindRadians(Yaw);
		SunDecl = RealDt > 0.0 ? FMath::FInterpTo(SunDecl, SeasonDeclDeg(W), RealDt, 0.6) : SunDecl;
		const double Dc = SunDecl * Deg;
		SunWorld = FVector(FMath::Cos(Dc) * FMath::Cos(SunAzimuth), FMath::Cos(Dc) * FMath::Sin(SunAzimuth), FMath::Sin(Dc));
		// (wave 4) static frame: the planet stands still in the world and the sun (and the sky) turn round it instead
		if (!bTurnWorld) SunWorld = FRotator(0.0, -FMath::RadiansToDegrees(Yaw), 0.0).RotateVector(SunWorld);
		Sim->SetSunDirection(SunWorld);
	}
	// (wave 4) The planet frame is the world frame (identity) unless -skyturn: a planet turning in the world moved the camera
	// and every town, tree and cloud kilometres per frame in world space (9 deg/s at 1x). The clouds' temporal reconstruction
	// reprojects with the camera's motion as if the clouds stood still in the world, so in live play it threw its history away
	// every frame and the quarter-resolution trace showed as a dotted dither at every cloud edge and in the cloud shadows
	// (docs/handoff/release-notes.md known issue 1c, shots/ue-power-volcano.png); the cloud shadow map, snapped to the world,
	// crawled over the ground; and every primitive on the planet was re-transformed each frame. Turning the sun instead gives
	// the same picture in the planet frame.
	Sim->SetPlanetToWorld(bTurnWorld ? FTransform(FRotator(0.0, FMath::RadiansToDegrees(Yaw), 0.0)) : FTransform::Identity);
}

void ADmSky::UpdateAtmosphere(const dm::World* W)
{
	const double Heat = MoltenHeat(W);
	const double Dust = W ? FMath::Clamp(W->dust, 0.0, 1.0) : 0.0;
	// Earth's coefficients times 1/S; a molten world or a dusty one gets a thick, reddish haze
	// (integration) the molten haze was 1 + 5 x heat: a pale peach veil over the whole lit disc that drowned the
	// glowing seams (docs/concept/genesis: dark slag, bright cracks); a thin smoky haze keeps the seams readable
	const double Haze = 1.0 + 1.2 * Heat + 3.0 * Dust;
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
	// (integration) a molten world sits under a pall of smoke: the sun is dimmed so the glowing seams carry the
	// picture (docs/concept/genesis 1-2: dark slag), and comes back as the world cools
	Sun->SetIntensity(float(Tune.SunLux * (1.0 - 0.45 * Dust) * (1.0 - 0.8 * MoltenHeat(W))));

	// cloud shadow map: centred under the camera, sized by altitude (blurred and displaced if too wide near the ground)
	const double Alt = FMath::Max(0.0, CameraAltKm(CamPos));
	LastCamAltKm = Alt;
	UpdateHaze(Alt);
	const float Ext = float(FMath::Clamp(Alt * 1.2 + 8.0, 12.0, 130.0));
	// from orbit the shadows are a few pixels wide: half the shadow-map resolution and ray samples there
	// (above 200 km a 512^2 map with half the rays: 2.2 -> 0.4 ms at the terminator, mean image change 0.5 grey levels)
	// (integration, performance) below 80 km the 2048^2 map with 2x rays cost 9.4 ms of GPU at a 15 km view (ProfileGPU,
	// RTX 3060), the largest pass in the frame; 1024^2 with 1x rays costs about a quarter and the soft cloud shadows
	// read the same at these sizes. Below 4 km the clouds have thinned to wisps (UpdateClouds): no cloud shadows at all.
	const float ResScale = Alt > 200.0 ? float(OrbitShadow[0]) : (Alt > 80.0 ? 2.f : 1.5f);
	const float RayScale = Alt > 200.0 ? float(OrbitShadow[1]) : 1.f;
	{
		double CsArg = 1.0;
		FDmShotArgs::Value(TEXT("-cloudshadow="), CsArg);
		const bool bWantCloudShadow = (Alt > 4.0 || FDmShotArgs::Param(TEXT("keepclouds"))) && CsArg > 0.0;
		if (Sun->bCastCloudShadows != bWantCloudShadow) { Sun->bCastCloudShadows = bWantCloudShadow; Sun->MarkRenderStateDirty(); }
		// (wave 3) never with -noclouds: SetupClouds did not run, so the component would trace the engine's default
		// material (a thick white haze over everything below 2.5 km of camera altitude)
		const bool bWantClouds = !bNoClouds && (Alt > 2.5 || FDmShotArgs::Param(TEXT("keepclouds")));
		if (Clouds->IsVisible() != bWantClouds) Clouds->SetVisibility(bWantClouds);
	}
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
	// (wave 3 verifier) near the ground the moonlit night was black (sky-look 5km-night / 300m-night: mean luma 1 of 255,
	// forest and hills invisible) while the night side from orbit reads: MoonNear x the moonlight below ~8 km, back to 1x
	// by 60 km (sky-look sv3e: 4 lux showed the forest and the hills under a dark sky)
	FVector FillDir = (MoonDirWorld * Illum - SunWorld * (1.5 - Illum)).GetSafeNormal();
	NearT = (bOldLook || !bNightV4) ? 0.0 : 1.0 - Smooth(8.0, 60.0, Alt);
	double Lux = Tune.MoonLux * Phase * Night;
	MoonAmbient = Tune.MoonLux * Phase / FMath::Max(0.1, Tune.SunLux);
	if (bOldLook) { /* the look before wave 3: as from orbit everywhere */ }
	else if (!bNightV4)
	{
		const double NearMoon = FMath::Lerp(MoonNear, 1.0, Smooth(8.0, 60.0, Alt));
		Lux *= NearMoon;
	}
	else
	{
		// (wave 4) Near the ground the night was a lottery: the moonlight's strength followed the moon's phase (x0.4 .. x1) and
		// the wave-3 x4 boost, and its direction followed the moon and the antisolar point, so one night view was brighter than
		// the day (perf-ab night at a city: mean luma 171 against 144 by day, cream "daylight") and another black (sky-look
		// 5km-night: luma 0.2, the moon 10 deg above the hills, everything in shadow). Below ~60 km the moonlight is now a
		// fixed, moderate key (MoonNearLux, a little brighter at full moon) from at least MoonMinEl above the ground in view,
		// on the moon's side; the exposure lift and the cool, desaturated night grade (UpdateGrade) make it read as moonlight.
		if (NearT > 0.0)
		{
			FVector H = FillDir - Up * FVector::DotProduct(FillDir, Up);
			if (!H.Normalize()) { FVector E, N; DmCoords::Basis(Up, E, N); H = N; }
			const double El = FMath::Asin(FMath::Clamp(FVector::DotProduct(FillDir, Up), -1.0, 1.0));
			const double WantEl = FMath::Max(El, MoonMinEl * Deg);
			const FVector Dn = (Up * FMath::Sin(WantEl) + H * FMath::Cos(WantEl)).GetSafeNormal();
			FillDir = FMath::Lerp(FillDir, Dn, NearT).GetSafeNormal();
			Lux = FMath::Lerp(Lux, MoonNearLux * (0.75 + 0.25 * Illum) * Night, NearT);
		}
	}
	LastMoonLux = Lux;
	LastMoonEl = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(FillDir, Up), -1.0, 1.0)));
	if (Lux > 0.003)
	{
		if (!MoonLight->IsVisible()) MoonLight->SetVisibility(true);
		MoonLight->SetIntensity(float(Lux));
		MoonLight->SetWorldRotation((-FillDir).Rotation());
		// (wave 4) near the ground the moonlight itself is blue (the Purkinje look: blue-black land), so the grade can leave the
		// warm city lights warm (UpdateGrade)
		const FLinearColor MoonCol = FMath::Lerp(FLinearColor(0.75f, 0.85f, 1.0f), FLinearColor(MoonNearColor[0], MoonNearColor[1], MoonNearColor[2]), float(NearT));
		if (!MoonLight->GetLightColor().Equals(MoonCol, 0.004f)) MoonLight->SetLightColor(MoonCol);
	}
	else if (MoonLight->IsVisible()) MoonLight->SetVisibility(false);
}

void ADmSky::UpdateHaze(double Alt)
{
	if (bOldLook)
	{
		// (integration) aerial perspective: 0.6 near the ground (depth, horizon haze), ApScale from orbit, where the
		// full strength washed forests teal-grey; the ocean keeps the blue of the air above it
		const float Ap = float(FMath::Lerp(0.6, ApScale, FMath::Clamp((Alt - 40.0) / 200.0, 0.0, 1.0)));
		if (FMath::Abs(Ap - LastAp) > 0.01f) { LastAp = Ap; Atmosphere->SetAerialPespectiveViewDistanceScale(Ap); }
		if (LastVeil != 1.f)
		{
			LastVeil = 1.f;
			Atmosphere->SetSkyAndAerialPerspectiveLuminanceFactor(FLinearColor::White);
			Atmosphere->SetSkyLuminanceFactor(FLinearColor::White);
		}
		return;
	}
	const float Ap = float(FMath::Lerp(ApNear, ApScale, FMath::Clamp((Alt - 40.0) / 200.0, 0.0, 1.0)));
	if (FMath::Abs(Ap - LastAp) > 0.01f) { LastAp = Ap; Atmosphere->SetAerialPespectiveViewDistanceScale(Ap); }
	// The haze veil: the scaled air is 6x denser per km than Earth's, so from 10 km up the in-scattered light laid over
	// the ground (aerial perspective luminance) turned forests teal-grey and the land pale (docs/concept/nature descent
	// 1-2 keep deep greens under a thin blue veil). Dim only that luminance: SkyAndAerialPerspectiveLuminanceFactor
	// scales sky and haze, SkyLuminanceFactor (1 / veil) gives the sky, the limb, the sky fill and the clouds' distant
	// sky light (its LUT takes both factors, SkyAtmosphere.usf RenderDistantSkyLightLutCS) their light back.
	const float Veil = float(FMath::Lerp(VeilNear, VeilOrbit, Smooth(40.0, 200.0, Alt)));
	if (FMath::Abs(Veil - LastVeil) > 0.005f)
	{
		LastVeil = Veil;
		Atmosphere->SetSkyAndAerialPerspectiveLuminanceFactor(FLinearColor(Veil, Veil, Veil, 1.f));
		Atmosphere->SetSkyLuminanceFactor(FLinearColor(1.f / Veil, 1.f / Veil, 1.f / Veil, 1.f));
	}
}

void ADmSky::UpdateFill(UDmSimSubsystem* Sim, const FVector& CamPos, const FRotator& CamRot)
{
	if (!Fill) return;
	const double Alt = FMath::Max(0.0, CameraAltKm(CamPos));
	// the capture point: the ground the camera looks at (forward ray against the sea sphere), else the ground below it
	const double Rs = SeaRadiusKm * DmCoords::UUPerKm;
	FVector Dir = CamPos.GetSafeNormal();
	const FVector F = CamRot.Vector();
	const double B = FVector::DotProduct(CamPos, F), C = CamPos.SizeSquared() - Rs * Rs, Disc = B * B - C;
	if (Disc > 0.0)
	{
		const double T = -B - FMath::Sqrt(Disc);
		if (T > 0.0) Dir = (CamPos + F * T).GetSafeNormal();
	}
	FillSunEl = FVector::DotProduct(Dir, SunWorld);
	// From orbit one capture point would light the whole disc (and the night side) with its sky: fade out with altitude.
	// At night the captured sky is black anyway, but a present sky light also replaces the sky Lumen reflections fall
	// back to: the moonlit sea and the night ground went darker (sky-look 50km-night / 5km-night, luma 30 -> 18), so
	// the fill bows out through nautical twilight (sun 3 to 12 deg below the horizon at the capture point).
	// (wave 3 verifier) The frame's own dusk: one capture lights (and is reflected by) everything on screen, so from a few
	// km up a frame that runs on across the terminator showed the day-side capture's sky on the dusk and night sea (sky-look
	// 50km-golden: the sea beyond the terminator turned pale grey with a hard, cell-stepped edge, luma 21 -> 38; sky-descent
	// d03 / d04). Gate the fill by the lowest sun at the frame's corners (the ground there, or the horizon when a corner
	// ray misses the planet): full while the sun stands above about -1 deg everywhere in the frame, off by about -7 deg.
	// Near the ground (below 2 km) the dusk is a thin band at the horizon and the gate eases off.
	FrameMinSunSin = FillSunEl;
	{
		double Fov = 60.0, Aspect = 16.0 / 9.0;
		if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
			if (PC->PlayerCameraManager) Fov = FMath::Clamp(double(PC->PlayerCameraManager->GetFOVAngle()), 5.0, 170.0);
		if (GEngine && GEngine->GameViewport) { FVector2D Vs; GEngine->GameViewport->GetViewportSize(Vs); if (Vs.Y > 1.0) Aspect = Vs.X / Vs.Y; }
		const double Tx = FMath::Tan(FMath::DegreesToRadians(Fov * 0.5)), Ty = Tx / Aspect;
		const FVector Rt = FRotationMatrix(CamRot).GetUnitAxis(EAxis::Y), Upv = FRotationMatrix(CamRot).GetUnitAxis(EAxis::Z);
		const FVector Ch = CamPos.GetSafeNormal();
		const double Cr = CamPos.Size(), Horizon = FMath::Acos(FMath::Clamp(Rs / FMath::Max(Cr, Rs + 1.0), -1.0, 1.0));
		for (int32 K = 0; K < 4; ++K)
		{
			const FVector D = (F + Rt * (Tx * ((K & 1) ? 1.0 : -1.0)) + Upv * (Ty * ((K & 2) ? 1.0 : -1.0))).GetSafeNormal();
			FVector P;
			const double Bk = FVector::DotProduct(CamPos, D), Dk = Bk * Bk - C;
			if (Dk > 0.0 && -Bk - FMath::Sqrt(Dk) > 0.0) P = (CamPos + D * (-Bk - FMath::Sqrt(Dk))).GetSafeNormal();
			else
			{
				FVector Dh = D - Ch * FVector::DotProduct(D, Ch);
				if (!Dh.Normalize()) continue;
				P = Ch * FMath::Cos(Horizon) + Dh * FMath::Sin(Horizon);   // the horizon in that direction
			}
			FrameMinSunSin = FMath::Min(FrameMinSunSin, double(FVector::DotProduct(P, SunWorld)));
		}
	}
	const double DuskGate = bFillDuskGate ? FMath::Lerp(1.0, Smooth(-0.12, -0.02, FrameMinSunSin), Smooth(2.0, 8.0, Alt)) : 1.0;
	const float I = float(FillGain * (1.0 - Smooth(30.0, 180.0, Alt)) * Smooth(-0.2, -0.05, FillSunEl) * DuskGate);
	if (I < 0.01f)
	{
		if (Fill->IsVisible()) { Fill->SetVisibility(false); AtmoRefresh = 2; ++NumFillToggles; }   // invisible: no capture cost either
		LastFill = 0.f;
		return;
	}
	double R = Rs;
	if (Sim && Sim->Sim())
	{
		const FVector PlanetDir = Sim->GetPlanetToWorld().InverseTransformVectorNoScale(Dir);
		R = FMath::Max(R, Sim->SurfaceRadiusUU(PlanetDir, FillCell));
	}
	R += 0.05 * DmCoords::UUPerKm;   // 50 m above the ground (inside the atmosphere, under the clouds)
	FillAltKm = R / DmCoords::UUPerKm - SeaRadiusKm;
	const FVector Pos = Dir * R;
	if (FVector::DistSquared(Pos, Fill->GetComponentLocation()) > FMath::Square(1000.0)) Fill->SetWorldLocation(Pos);
	if (!Fill->IsVisible()) { Fill->SetVisibility(true); AtmoRefresh = 2; ++NumFillToggles; }
	if (FMath::Abs(I - LastFill) > 0.01f) { LastFill = I; Fill->SetIntensity(I); AtmoRefresh = 2; ++NumFillSets; }
}

ADmSky::FLookState ADmSky::GetLookState() const
{
	FLookState L;
	L.bFill = Fill && Fill->IsVisible();
	L.Fill = Fill ? Fill->Intensity : 0.f;
	L.FillAltKm = FillAltKm;
	L.FillSunSin = FillSunEl;
	L.Ap = Atmosphere ? Atmosphere->AerialPespectiveViewDistanceScale : 0.f;
	L.Veil = LastVeil < 0.f ? 1.f : LastVeil;
	L.bStars = Stars && Stars->IsVisible();
	L.FillGain = FillGain;
	L.ApNear = bOldLook ? 0.6 : ApNear; L.ApOrbit = ApScale;
	L.VeilNear = bOldLook ? 1.0 : VeilNear; L.VeilOrbit = bOldLook ? 1.0 : VeilOrbit;
	L.Lift = Post ? double(Post->Settings.AutoExposureBias) - Ev : 0.0;
	L.GoldenEv = bOldLook ? 0.0 : GoldenEv;
	L.FrameMinSunSin = FrameMinSunSin;
	L.FillSets = NumFillSets; L.FillToggles = NumFillToggles; L.AtmoDirty = NumAtmoDirty;
	L.MoonLux = MoonLight && MoonLight->IsVisible() ? LastMoonLux : 0.0; L.MoonEl = LastMoonEl;
	L.MoonNearLux = MoonNearLux; L.NightEvNear = NightEvNear; L.MoonMinEl = MoonMinEl; L.bNightV4 = bNightV4 && !bOldLook;
	return L;
}

void ADmSky::SetOldLook(bool bOld)
{
	bOldLook = bOld;
	FillGain = bOld ? 0.0 : FillGainArg;
	LastAp = -1.f;
	LastVeil = -1.f;
}

void ADmSky::SetNightParams(double InMoonNearLux, double InNightEvNear, double InCloudNightBoost)
{
	if (InMoonNearLux >= 0.0) MoonNearLux = InMoonNearLux;
	if (InNightEvNear >= 0.0) NightEvNear = InNightEvNear;
	if (InCloudNightBoost >= 0.0) CloudNightBoost = InCloudNightBoost;
}

void ADmSky::SetLookParams(double InApNear, double InVeilNear, double InVeilOrbit, double InFill, double InGoldenEv)
{
	// (the setters only push values that changed: UpdateHaze compares against LastAp / LastVeil)
	if (InApNear >= 0.0) ApNear = InApNear;
	if (InVeilNear > 0.0) VeilNear = InVeilNear;
	if (InVeilOrbit > 0.0) VeilOrbit = InVeilOrbit;
	if (InFill >= 0.0) { FillGainArg = InFill; if (!bOldLook) FillGain = InFill; }
	if (InGoldenEv >= 0.0) GoldenEv = InGoldenEv;
}

FString ADmSky::DescribeLook() const
{
	return FString::Printf(TEXT("moon %.3f lux el %.0f, night amb %.3f, fill %.2f%s (capture %.2f km up, sun sin %.2f, frame min %.2f), ap %.2f, veil %.2f, lift %.2f EV, pptrans %d, clouds %s cover %.2f, stars %d, camera %.2f km%s"),
		MoonLight && MoonLight->IsVisible() ? LastMoonLux : 0.0, LastMoonEl, LastNightAmb, LastFill < 0.f ? 0.f : LastFill,
		Fill && Fill->IsVisible() ? TEXT("") : TEXT(" off"), FillAltKm, FillSunEl, FrameMinSunSin, LastAp, LastVeil < 0.f ? 1.f : LastVeil, Post ? double(Post->Settings.AutoExposureBias) - Ev : 0.0,
		Sun && Sun->bPerPixelAtmosphereTransmittance ? 1 : 0, Clouds && Clouds->IsVisible() ? TEXT("on") : TEXT("off"), LastCover,
		Stars && Stars->IsVisible() ? 1 : 0, LastCamAltKm, bOldLook ? TEXT(", old look") : TEXT(""));
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
	// (wave 4 verifier) 1x is kept only up to 20 km: from 40 km the mid scale (0.6) is enough. ProfileGPU VolumetricCloud, 1x ->
	// 0.6: 50km-day 3.3 -> 1.8 ms, 50km-night 6.2 -> 3.0 ms, cyclone-close (62 km, the hurricane's overcast) 7.9 -> 3.7 ms, with a
	// mean image change of 0.7-1.8 grey levels (shots/ue-s4v-prof-* vs ue-s4v-vs06-*); those views were over the 16.7 ms budget.
	// -cloudmidscale=1 restores the old curve (A/B).
	const double SampleT = FMath::Clamp((Alt - 2.0) / 6.0, 0.0, 1.0), MidT = FMath::Clamp((Alt - 20.0) / 20.0, 0.0, 1.0), FarT = FMath::Clamp((Alt - 120.0) / 130.0, 0.0, 1.0);
	float ViewScale = float(FMath::Lerp(FMath::Lerp(FMath::Lerp(2.0, 1.0, SampleT), FMath::Max(MidScale, OrbitScale), MidT), OrbitScale, FarT));
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
	// (integration) the god looks through the weather: the thin scaled cloud layer (0.25 - 1.85 km) sits right where
	// the close views are (a village at 2.6 km was all cloud), so the cover thins as the camera comes down, to a
	// few wisps by 3 km; from 28 km up the weather is whole (old SPEC-RENDER: clouds fade on zoom)
	if (!FDmShotArgs::Param(TEXT("keepclouds"))) Cover *= FMath::Lerp(0.08, 1.0, Smooth(3.0, 28.0, Alt));
	CloudScalar(TEXT("Yaw"), float(GetPlanetYaw()));
	CloudScalar(TEXT("SkyTime"), float(FMath::Fmod(SkyTime, 86400.0)));
	CloudScalar(TEXT("Blend"), float(Blend));
	CloudScalar(TEXT("CoverScale"), float(Cover));
	LastCover = float(Cover);
	CloudScalar(TEXT("Heat"), float(Heat));
	// moonlit night clouds: the material's ambient on the night side, a little blue (NightAmb scales SkyAmb)
	// (scaled by how much the view is a night view: the same ambient beside the lit day side, at no exposure lift,
	// turned the night half of a terminator shot into white daylight clouds)
	LastNightAmb = FMath::Clamp(MoonAmbient * NightAmbGain * (0.12 + 0.88 * NightView), 0.0, 0.75);
	CloudScalar(TEXT("NightAmb"), float(LastNightAmb));   // moonlit clouds outshine the ground
	// (wave 4) near the ground at night the thin cloud read as dark blotches over the moonlit land (the moonlight is not an
	// atmospheric light, so the clouds only get the faint night sky ambient): lift their ambient there
	{
		const float Amb = float(0.8 * (1.0 + CloudNightBoost * NightView * NearT));
		if (FMath::Abs(Amb - LastSkyAmb) > 0.01f) { LastSkyAmb = Amb; CloudScalar(TEXT("SkyAmb"), Amb); }
	}
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
		if (!bTurnWorld) Pos = FRotator(0.0, -FMath::RadiansToDegrees(Yaw), 0.0).RotateVector(Pos);   // (wave 4) static frame: the sky turns
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
	// (wave 3 verifier) a low sun near the ground: the fixed exposure left golden-hour views underexposed by 2-3 EV
	// (sky-look 300m-golden / floor-golden: canopy and forest floor near black even with the sky fill, against the warm,
	// readable docs/concept/nature descent-5). Lift up to GoldenEv as the sun sinks below ~20 deg; the night lift takes
	// over below the horizon (max, so the two join without a step). Low: none from orbit or at the terminator pass.
	const double LowSun = bOldLook ? 0.0 : Low * Smooth(0.35, 0.06, E);
	// (wave 4) near the ground the night lift is small and the moonlight carries the picture (UpdateLights): the +2.6 EV lift
	// multiplied every emissive by 6 (the planet's orbital city glow became a cream sheet over the ground round a city at 3 km,
	// perf-ab night, luma 171), and the town windows into white blobs
	const double NightEvHere = FMath::Lerp(Tune.NightEv, NightEvNear, NearT);
	const double Lift = FMath::Max(NightEvHere * Night, GoldenEv * LowSun) + 0.6 * Heat;
	P.AutoExposureBias = float(Ev + Lift);
	// the sky behind the planet is not part of the night side: keep the star field and the milky way from rising with
	// the night exposure (at +2 EV the galactic band turned into grey smoke across the black)
	// The star sphere is a mesh 5,000 km out: inside the lit atmosphere the sky over it is drawn as aerial perspective on
	// a distant object, not as sky, which turned the day sky near the ground a deep, banded navy with stars showing
	// (and, as its occlusion culling came and went, flipped it between that and the real pale sky: sky-look s3e / s3h).
	// By day below 80-120 km the stars are not seen anyway: fade them out there and hide the sphere.
	const double StarFade = bOldLook ? 1.0 : FMath::Max(Smooth(80.0, 120.0, Alt), Smooth(-0.02, -0.12, E));
	if (Stars->GetStaticMesh() && StarMID)
	{
		const bool bWantStars = StarFade > 0.002;
		if (Stars->IsVisible() != bWantStars) Stars->SetVisibility(bWantStars);
	}
	if (StarMID && (FMath::Abs(Lift - LastStarLift) > 0.02 || FMath::Abs(StarFade - LastStarFade) > 0.01))
	{
		LastStarLift = Lift;
		LastStarFade = StarFade;
		StarMID->SetScalarParameterValue(TEXT("StarBrightness"), float(StarFade * StarBright * FMath::Pow(2.0, -0.75 * Lift)));
		StarMID->SetScalarParameterValue(TEXT("Galaxy"), float(StarFade * FMath::Pow(2.0, -Lift)));
	}
	// (wave 4) near the ground the night's blue comes from the moonlight's own colour (UpdateLights), not from the grade: a
	// cooler white point, a blue scene tint and a strong desaturation turned the warm (orange) city glow and town windows
	// cream-white, the "bright cream daylight" of release-notes known issue 1a (docs/concept/night: blue-black land, WARM lights)
	const double NightCool = FMath::Lerp(700.0, 250.0, NearT);
	P.WhiteTemp = float(6500.0 + MoodWarm + 900.0 * Golden - NightCool * Night + 900.0 * Heat);
	if (P.bOverride_SceneColorTint) { P.bOverride_SceneColorTint = false; P.SceneColorTint = FLinearColor::White; }
	// Split toning instead: the dark and mid tones (the moonlit land and sea) go blue and pale, the highlights (city glow,
	// windows, fires) keep their warmth (the planet material's own night ambient left the moonlit land olive green, s5j)
	{
		const float Nt = float(Night * NearT * NightTone);
		const auto Mix4 = [Nt](float R, float G, float B, float A) { return FVector4(FMath::Lerp(1.f, R, Nt), FMath::Lerp(1.f, G, Nt), FMath::Lerp(1.f, B, Nt), FMath::Lerp(1.f, A, Nt)); };
		// (the moonlit land sits far below 0.1 of scene luminance; the city glow is above it: toning the midtones too turned the
		// glow pale again, perf-ab night s5o)
		P.bOverride_ColorSaturationShadows = P.bOverride_ColorGainShadows = P.bOverride_ColorCorrectionShadowsMax = true;
		P.ColorSaturationShadows = Mix4(1.f, 1.f, 1.f, 0.45f);
		P.ColorGainShadows = Mix4(0.76f, 0.92f, 1.34f, 1.f);
		P.ColorCorrectionShadowsMax = FMath::Lerp(0.09f, 0.12f, Nt);
	}
	// (wave 3) from high up the land read pale olive against docs/concept/world-states/state-healthy.png (mean saturation
	// 0.33 vs 0.49 in sky-look orbit-day): the thin remaining haze still greys it, so the grade adds a little colour back
	// with altitude (none below 10 km, +12 % from 120 km)
	const double HighSat = bOldLook ? 0.0 : 0.12 * Smooth(10.0, 120.0, Alt);
	const float Sat = float(MoodSat * (1.0 + 0.08 * Golden - FMath::Lerp(0.25, 0.1, NearT) * Night + HighSat));
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
	if (bTrace && Frames % 120 == 0) UE_LOG(LogDmSky, Display, TEXT("DmSkyTrace: look near %.2f: %s"), NearT, *DescribeLook());
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
	UpdateFill(Sim, CamPos, CamRot);
	// Adding, removing or re-scaling the sky light could leave the sky over the star sphere in one of two states
	// (pale, hazy sky or a deep blue one with stars showing, depending on the order of the render-state updates;
	// sky-look -variants runs s3e / s3f): refreshing the atmosphere's render state right after a fill change gives
	// the same sky as a scene that never changed.
	if (AtmoRefresh > 0 && --AtmoRefresh == 0) { Atmosphere->MarkRenderStateDirty(); ++NumAtmoDirty; }
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
	// (wave 4) static frame: the star field turns round the planet (M_SkyStars rotates the view direction by StarYaw)
	if (StarMID) StarMID->SetScalarParameterValue(TEXT("StarYaw"), bTurnWorld ? 0.f : float(Yaw));

	// aurora turns with the planet, glows on the night side
	if (AuroraMID)
	{
		{ const FRotator Ar(0.0, FMath::RadiansToDegrees(GetPlanetYaw()), 0.0); if (!Aurora->GetComponentRotation().Equals(Ar, 1e-4)) Aurora->SetWorldRotation(Ar); }
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
