// DEMIURGE — Game/Sky/DmSkyActor.h : the sky (C2, SPEC-UNREAL 2; old SPEC-RENDER 7). Owner: C2 sky.
//
// Scene part "sky" (priority 0; replaces the bridge's placeholder). One actor owns:
//   - time: "sky seconds" advance with the sim (ticks per second / 8, capped at SpinRateCap so the planet
//     does not strobe at 8x); paused or static worlds stand still (-skyrate= forces a rate for scenarios);
//   - the planet frame and the sun (SPEC-UNREAL 1): the planet turns eastward once per DayLength sky
//     seconds under a fixed sun; seasons tilt the sun +-23 deg over SeasonYears sim years. Published through
//     UDmSimSubsystem::SetPlanetToWorld / SetSunDirection every frame before anything renders. A pinned sun
//     (-sun=, scenarios' SetSubsolar) freezes the time of day: the sky keeps the pinned direction and the
//     planet stops turning; UnpinSun() resumes from there without a jump;
//   - sun light (10 lux, cloud shadows sized by camera altitude), sky atmosphere (Earth's scaled by
//     S = 1/6.15 around the sea radius; molten worlds and dust thicken and redden it);
//   - weather-driven volumetric clouds (M_SkyClouds): cover and warp maps from the sim's moisture, temperature
//     and land plus the moving weather systems (DmSkyWeather.h, DmSkyStorms.h: climatology + the sim's storms +
//     rain), rebuilt on a worker thread every WeatherInterval sky seconds (or when a system moved while time
//     stands still) and crossfaded; the static cell texture is sheared by the warp map and advected along the
//     wind belts in the material; cirrus; lightning flashes; sample budget by camera altitude;
//   - stars and the milky way on a world-fixed sphere (M_SkyStars; comet rain during the Water phase), a
//     moon on an inclined orbit (lit by the sun; -moon= places it in the camera's view for shots) with a
//     faint moonlight that only switches on when the camera looks at the night side, aurora curtains around
//     both poles on the night side (M_SkyAurora);
//   - exposure and grading: manual exposure (-ev), brighter at night near the ground, warm at golden hour,
//     a gentle grade per mood of the world (sim::mood of the believers) and for the molten genesis.
// Command line (scenarios / tuning): -skyrate=<r> -skytime=<s> -daylen=<s> -moon=az,el,distR,radiusKm
// -moonlux=<lux> -nomoon -nostars -noclouds -noaurora -aurora=<gain> -cyclone=lat,lon[,radiusDeg]
// -wxseed=<n> -mapout=<png> -cp=Param:Value,... (cloud material) -lux=<sun lux> -stars=<gain> -cover=<scale>
// -nightamb=<gain> (moonlit cloud ambient) -cloudviewscale=<s> -cloudshadowscale=<s> -cloudorbitscale=<s> (sample budgets)
// -atmodrop=<km> (atmosphere ground below the sea; hides a sagging far sea mesh, see the verify report) -skytrace (clock,
// map swaps and any sky tick over 4 ms in the log).
// Wave 3 look switches: -skyfill=<gain> (sky fill light; 0 = off) -skyold (today's look before wave 3: no sky fill, the old
// aerial-perspective curve) -pptrans=0 (sun without per-pixel atmosphere transmittance, for comparisons) -apveil=<k> (haze
// luminance from orbit, see UpdateHaze). Verifier: -goldenev=<EV> (exposure lift for a low sun near the ground, default 1)
// -moonnear=<gain> (moonlight near the ground, default 4).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Async/Future.h"
#include "Game/Sky/DmSkyStorms.h"
#include "Game/Sky/DmSkyWeather.h"
#include "DmSkyActor.generated.h"

class UDirectionalLightComponent;
class USkyLightComponent;
class USkyAtmosphereComponent;
class UVolumetricCloudComponent;
class UPostProcessComponent;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
class UVolumeTexture;
class UDmSimSubsystem;
struct FDmDirtyFlags;
namespace dm { struct World; struct Event; }

/** Tuning values (defaults are the shipped look). */
struct FDmSkyTune
{
	double DayLength = 40.0;          // sky seconds per planet day (= real seconds at 1x)
	double SpinRateCap = 2.0;         // sky seconds per real second at most (8x would strobe)
	double SeasonYears = 400.0;       // sim years per seasonal cycle (a History game sees about three)
	double TiltDeg = 23.0;
	double SunLux = 10.0;
	double WeatherInterval = 1.5;     // sky seconds between weather-map rebuilds (crossfaded; ~0.15 s on workers)
	double MoonPeriodDays = 9.0;
	double MoonDistR = 25.0;          // planet radii
	double MoonRadiusKm = 27.0;
	double MoonInclDeg = 9.0;
	double MoonLux = 0.6;             // moonlight at full moon (artistic: real moonlight is far fainter; the night side must read)
	double NightEv = 2.6;             // extra exposure when the camera looks at the night side (full near the ground)
	double AuroraGain = 0.7;
};

UCLASS()
class DEMIURGE_API ADmSky : public AActor
{
	GENERATED_BODY()

public:
	ADmSky();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** The sky of this world (nullptr if none). */
	static ADmSky* Get(const UObject* WorldContext);

	// ------------------------------------------------------------ API for FX / integration
	/** A lightning flash in the clouds over a cell (sim lightning events do this already). */
	void Strike(int32 Cell, float Gain = 1.f);
	/** Extra aurora (the FX "aurora" effect): 0 = ambient only. Decays back over Seconds. */
	void BoostAurora(float Amount, float Seconds = 20.f);
	/** Override the sky clock rate (sky seconds per real second); negative = follow the sim. */
	void SetForcedRate(double Rate) { ForcedRate = Rate; }
	double GetSkyTime() const { return SkyTime; }
	/** Planet yaw in radians (world = Rz(yaw) * planet): 0 in the static frame (wave 4 default: the planet stands still in the
	 *  world and the sun and sky turn round it), the time-of-day angle with -skyturn. */
	double GetPlanetYaw() const { return bTurnWorld ? Yaw : 0.0; }
	/** The time-of-day angle in radians (decreases as the planet turns eastward), in either frame. */
	double GetDayAngle() const { return Yaw; }
	bool IsWorldTurning() const { return bTurnWorld; }
	/** Put the moon in the camera's view (azimuth / elevation degrees relative to the view, distance in planet
	 *  radii, radius km), for shots; it is still lit by the sun. Negative distance: back on its orbit. */
	void SetMoonInView(double AzDeg, double ElDeg, double DistR = 25.0, double RadiusKm = 27.0);
	/** The newest cover map at a planet-frame direction: R cover, G top, B cumuliform, A thickness (0..1). */
	FLinearColor CoverAt(const FVector& PlanetDir) const;
	/** Weather-map generations uploaded since BeginPlay (after the first). */
	int64 GetMapsBuilt() const { return MapsBuilt; }
	/** Write a PNG of the current base weather map (cover over land / sea, mirrored to read like the globe). */
	bool SaveWeatherPreview(const FString& Path) const;
	/** One line for logs: sky fill intensity / capture altitude, aerial perspective scale and haze luminance. */
	FString DescribeLook() const;
	/** The look state this frame (checks): sky fill, haze, stars. */
	struct FLookState { bool bFill = false; float Fill = 0.f; double FillAltKm = 0.0; double FillSunSin = 0.0; float Ap = 0.f; float Veil = 1.f; bool bStars = false; double FillGain = 0.0; double ApNear = 0.0, ApOrbit = 0.0, VeilNear = 1.0, VeilOrbit = 1.0; double Lift = 0.0, GoldenEv = 0.0; int32 FillSets = 0, FillToggles = 0, AtmoDirty = 0; double FrameMinSunSin = 0.0;
		double MoonLux = 0.0, MoonEl = 0.0, MoonNearLux = 0.0, NightEvNear = 0.0, MoonMinEl = 0.0; bool bNightV4 = false; };
	FLookState GetLookState() const;
	/** Switch between the wave-3 look and the look before it at run time (frame-time A/B under the same load). */
	void SetOldLook(bool bOld);
	bool IsOldLook() const { return bOldLook; }
	/** Look tuning at run time (look development): aerial perspective near the ground, haze luminance near the ground and
	 *  from orbit, sky fill gain. Negative = keep. */
	void SetLookParams(double InApNear, double InVeilNear, double InVeilOrbit, double InFill, double InGoldenEv = -1.0);
	/** (wave 4) night near the ground at run time: moonlight (lux), exposure lift (EV), cloud ambient boost. Negative = keep. */
	void SetNightParams(double InMoonNearLux, double InNightEvNear, double InCloudNightBoost);
	const FDmSkyStorms& GetStorms() const { return Storms; }
	FDmSkyStorms& GetStormsMutable() { return Storms; }
	FDmSkyTune Tune;

	UPROPERTY(VisibleAnywhere) TObjectPtr<UDirectionalLightComponent> Sun;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UDirectionalLightComponent> MoonLight;
	/** Sky fill: a real-time captured sky light whose capture point sits on the ground the camera looks at (see UpdateFill). */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USkyLightComponent> Fill;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USkyAtmosphereComponent> Atmosphere;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UVolumetricCloudComponent> Clouds;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPostProcessComponent> Post;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Stars;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Moon;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Aurora;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> FlashDisc[FDmSkyStorms::FlashSlots];

private:
	struct FBuildResult
	{
		TArray<FColor> Pixels;
		TArray<FFloat16Color> Warp;
		void* CoverUpload = nullptr;      // FMemory::Malloc copies, handed to the render thread
		void* WarpUpload = nullptr;
		TSharedPtr<FDmSkyWeather, ESPMode::ThreadSafe> Weather;   // set when the grids were rebuilt
		double MorphTime = 0.0;
		double Seconds = 0.0;
		float SeaFraction = -1.f;
	};

	void ReadArgs();
	void SetupClouds();
	void SetupStarsMoonAurora();
	void StartWeather(bool bSync);
	void PumpWeather(double RealDt);
	void UpdateTime(UDmSimSubsystem* Sim, double RealDt);
	void UpdateLights(UDmSimSubsystem* Sim, const FVector& CamPos);
	void UpdateAtmosphere(const dm::World* W);
	void UpdateFill(UDmSimSubsystem* Sim, const FVector& CamPos, const FRotator& CamRot);
	void UpdateHaze(double AltKm);
	void UpdateClouds(const dm::World* W, const FVector& CamPos, double RealDt);
	void UpdateMoon(const FVector& CamPos, const FRotator& CamRot);
	void UpdateGrade(const dm::World* W, const FVector& CamPos, double RealDt);
	void OnSimEvent(const dm::Event& E);
	void OnDirty(const FDmDirtyFlags& F);
	void OnWorldChanged(const dm::World* W);
	double SeasonDeclDeg(const dm::World* W) const;
	double CameraAltKm(const FVector& CamPos) const;
	bool WantSystems(const dm::World* W) const;

	// time and frame
	double SkyTime = 0.0, SkyDt = 0.0, SkyRate = 0.0, ForcedRate = -1.0;
	double Yaw = 0.0;                 // radians: the time-of-day angle (the planet's yaw in the world only with -skyturn)
	bool bTurnWorld = false;          // -skyturn: the planet turns in the world under a fixed sun (before wave 4)
	double SunAzimuth = 0.0;          // world azimuth of the fixed sun (radians)
	double SunDecl = 0.0;             // current declination (degrees), eased toward the season
	bool bWasPinned = true;
	FVector SunWorld = FVector(1, 0, 0);
	double SeaRadiusKm = 100.0;
	double LastSeaLevel = 1e30;

	// weather
	TSharedPtr<FDmSkyWeather, ESPMode::ThreadSafe> Weather;
	FDmClimateSnapshot Snapshot;
	FDmWeatherParams WxParams;
	TFuture<TSharedPtr<FBuildResult, ESPMode::ThreadSafe>> Pending;
	bool bPending = false;
	bool bClimateDirty = true;
	double LastClimateReal = -1e9, RealTime = 0.0;
	double LastMorph = -1e9;          // sky time of the newest requested map
	double LastBuildReal = -1e9;      // real time of the newest requested map
	uint32 LastFingerprint = 0;
	double Blend = 1.0;
	int32 Cur = 1;                    // index of the newest map (bound as WeatherB)
	float SeaFraction = 0.7f;
	TArray<FColor> LastBase;          // newest cover map (previews)
	TArray<FFloat16Color> LastWarp;   // newest warp map (previews)
	int64 MapsBuilt = 0;
	double MapSecondsSum = 0.0, MapSecondsMax = 0.0;
	FDmSkyStorms Storms;
	uint32 WxSeed = 7;
	bool bSeedArg = false;
	bool bStormsReady = false;

	UPROPERTY() TObjectPtr<UTexture2D> WeatherTex[2];
	UPROPERTY() TObjectPtr<UTexture2D> WarpTex[2];
	UPROPERTY() TObjectPtr<UTexture2D> FineTex;
	UPROPERTY() TObjectPtr<UVolumeTexture> ShapeTex;
	UPROPERTY() TObjectPtr<UVolumeTexture> DetailTex;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CloudMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MoltenMID;
	void CloudTex(FName Name, class UTexture* T);
	void CloudScalar(FName Name, float V);
	void CloudVec(FName Name, const FLinearColor& V);
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> StarMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MoonMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> AuroraMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FlashMID[FDmSkyStorms::FlashSlots];
	void UpdateFlashes();

	// moon
	bool bMoonOverride = false;
	double MoonArgs[4] = { -12.0, 5.0, 25.0, 27.0 };
	FVector MoonDirWorld = FVector(-1, 0, 0);
	double MoonLuxArg = -1.0;

	// look state
	double Ev = 0.0;
	double AuroraBoost = 0.0, AuroraBoostRate = 0.0;
	double MoodWarm = 0.0, MoodSat = 1.0, MoodContrast = 1.0, MoodTimer = 0.0;
	double TargetWarm = 0.0, TargetSat = 1.0, TargetContrast = 1.0;
	double CometRain = 0.0;
	double FlashGain = 2.5;
	double MoonAmbient = 0.0;         // moonlight relative to sunlight (cloud ambient on the night side)
	double CoverArg = 1.0;
	double StarBright = 3.0, LastStarLift = -1.0, LastStarFade = -1.0;   // star gain (-stars=), exposure lift it was last compensated for
	double OrbitShadow[2] = { 1.0, 0.5 };   // cloud shadow map resolution / ray scale above 200 km (-cloudshadoworbit=r,s)
	double NightView = 0.0;           // 0..1: how much the camera looks at the night side (set by UpdateGrade)
	double NightAmbGain = 14.0;       // night cloud ambient per unit of moonlight / sunlight (-nightamb=)
	double OrbitScale = 0.45;         // cloud view samples from 250 km out (-cloudorbitscale=; 0.6 cost ~1.5 ms more at the terminator)
	double ForceViewScale = -1.0;     // -cloudviewscale= forces the view sample scale (tests)
	double MidScale = 0.6;            // (wave 4 verifier) cloud view samples from 40 to 120 km (-cloudmidscale=; 1 = before)
	double ApScale = 0.38;             // aerial perspective from orbit (-apscale=; 1 near the ground)
	float LastAp = -1.f;
	double AtmoDropKm = 0.0;          // the atmosphere's ground sits this far below the sea
	// wave 3 look: sky fill and haze
	bool bOldLook = false;            // -skyold: the look before wave 3 (comparisons)
	double FillGain = 3.0;            // -skyfill= (0 with the old look). 1 = the captured sky as is: too faint to read in
	double FillGainArg = 3.0;         // the shadows at the fixed exposure (sky-look floor-day / 300m-golden, gain 1 / 3 / 5)
	bool bFillDuskGate = true;         // -nodusk: (verifier) no frame-dusk gate on the sky fill (comparisons)
	double FrameMinSunSin = 0.0;      // lowest sun (sine) at the frame's corners, for the fill's dusk gate and logs
	double MoonNear = 4.0;           // -moonnear= (verifier) moonlight gain near the ground (1 = as from orbit)
	double GoldenEv = 1.0;           // -goldenev= (verifier) exposure lift for a low sun near the ground (sky-look sv3d: 0 / 0.8 / 1.4)
	// wave 4 night near the ground (UpdateLights): -nightv3 restores the wave-3 night (MoonNear x the phase-dependent moonlight)
	bool bNightV4 = true;
	double MoonNearLux = 2.0;        // -moonnearlux= moonlight below ~8 km (x0.75 at new moon .. x1 at full), with the 1 EV lift (sky-look s4d: 1.0 / 1.6 / 2.6)
	double MoonMinEl = 40.0;         // -moonminel= lowest moon elevation (deg) over the ground in view below ~8 km
	double NightEvNear = 1.0;        // -nightevnear= night exposure lift below ~8 km (Tune.NightEv from ~60 km up; = the golden-hour lift, so dusk has no step)
	float MoonNearColor[3] = { 0.52f, 0.68f, 1.0f };   // -mooncolor=r,g,b moonlight colour below ~8 km (wave 4)
	double FineCubicArg = 1.0;
	bool bWeatherPool = false;
	double NightTone = 1.0;          // -nighttone= strength of the near-ground night split toning (0 = off, A/B)       // -skywxpool: build the weather maps in the shared thread pool (before wave 4)       // -finecubic= cubic cell lookup in the cloud material (0 = the old bilinear, A/B)
	double CloudNightBoost = 3.0;    // -cloudnight= extra cloud sky ambient at night near the ground (x(1 + boost))
	double NearT = 0.0;              // 1 below ~8 km, 0 from ~60 km (UpdateLights)
	float LastSkyAmb = -1.f;
	double LastMoonLux = 0.0, LastMoonEl = 0.0, LastNightAmb = 0.0;   // for logs
	float LastFill = -1.f;
	double FillAltKm = 0.0;           // capture point altitude above the sea (km), for logs
	double FillSunEl = 0.0;           // sine of the sun elevation at the capture point, for logs
	int32 FillCell = -1;              // warm start for the surface lookup
	double ApNear = 0.35;             // -apnear= aerial perspective scale near the ground (0.6 before wave 3)
	double VeilNear = 0.75;           // -apveilnear= haze luminance near the ground (sky-look s3c / s3i / s3j variants)
	double VeilOrbit = 0.7;           // -apveil= haze luminance from orbit (0.5 tested; the ocean's blue from orbit is that haze)
	float LastVeil = -1.f;
	int32 NumFillSets = 0, NumFillToggles = 0, NumAtmoDirty = 0;   // (verifier) render-state churn counters (sky-descent)
	int32 AtmoRefresh = 0;            // frames until the atmosphere's render state is refreshed after a sky fill change
	double LastCamAltKm = 0.0;        // for logs
	float LastCover = 0.f;            // for logs
	double TickMsMax = 0.0;           // slowest game-thread tick of the sky so far
	bool bTrace = false;              // -skytrace: log the sky clock and map swaps every 15 frames
	float LastShadowExtent = -1.f, LastViewScale = -1.f, LastSampleDist = -1.f;
	bool bNoClouds = false;
	bool bNoCyclones = false;
	double LastLog = 0.0;
	int32 Frames = 0;

	FDelegateHandle EventHandle, DirtyHandle, WorldHandle;
};
