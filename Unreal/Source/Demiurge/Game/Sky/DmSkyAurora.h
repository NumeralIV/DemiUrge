// DEMIURGE — Game/Sky/DmSkyAurora.h : the aurora (wave 3 'aurora' track; SPEC-UNREAL 2 C2 "aurora at the
// poles"). Owner: the aurora track (lanes/aurora3.owned); the rest of Game/Sky belongs to the sky track.
//
// Scene part "aurora" (World, priority 0). Curtains of light over both magnetic poles, on the night side only:
// green from a sharp lower border about 3 km up, violet tops 5 to 12 km up, vertical rays drifting along
// three folded arcs around an oval that widens toward local midnight, plus a faint diffuse glow.
//
// How: one open spherical cap (radius sea + TopKm, CapDeg around each magnetic pole) carries M_Aurora
// (Scripts/aurora.py, Scripts/aurora/aurora.hlsl), an unlit additive material that ray-marches its view ray
// through the aurora slab in the planet frame, clipped by the planet and the scene depth. Being a volume (not
// ribbons), it reads correctly from every distance: a glowing band with bright folds from orbit, tall curtains
// with violet tops at the limb, and curtains with rays seen from a few hundred km over the pole.
//
// The actor follows the planet (UDmSimSubsystem::GetPlanetToWorld) and the sun (GetSunDirection) every frame.
// Strength = the sky's FDmSkyTune::AuroraGain (-aurora=<gain>) x (1 + 2 x boost). Boost comes from Boost()
// here and, until the sky track routes ADmSky::BoostAurora to this actor, from the sky's own aurora strength
// parameter (so the FX "aurora" surge, comet impacts and nukes keep working). The sky's old ribbon aurora
// (ADmSky::Aurora) is hidden at runtime by this actor.
//
// Command line: -noaurora (none at all), -aurora=<gain> (read by the sky), -aurorasteps=<n> (march steps,
// default 40), -auroraboost=<0..1> (a held surge, for shots), -auroratrace (a DmAurora line every 60 frames),
// -auroradebug (the material tints every early-out of the march: red, orange, yellow, blue, magenta; grey = marched).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DmSkyAurora.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;

/** Tuning (defaults are the shipped look). */
struct FDmAuroraTune
{
	double BaseKm = 3.0;        // lower border of the curtains above the sea (clouds top out near 1.9 km)
	double TopKm = 14.0;        // top of the slab the march covers (violet tops fade out below it)
	double CapDeg = 75.0;       // angular radius of each cap around its magnetic pole (the oval sits near 19; grazing rays
	                            // at the limb enter the slab up to ~27 deg before passing over it, so the caps reach far)
	double Steps = 40.0;        // march steps per pixel (jittered; TSR resolves)
	double DefaultGain = 0.7;   // when there is no sky to read FDmSkyTune::AuroraGain from
	double BoostGain = 2.0;     // strength x (1 + BoostGain x boost)
	double PoleLat[2] = { 81.0, -81.0 };    // magnetic poles (sim latitude / longitude), as the sky had them
	double PoleLon[2] = { 60.0, -130.0 };
};

UCLASS()
class DEMIURGE_API ADmAurora : public AActor
{
	GENERATED_BODY()

public:
	ADmAurora();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** The aurora of this world (nullptr if none). */
	static ADmAurora* Get(const UObject* WorldContext);

	/** A surge: 0..1, decaying back to the ambient aurora over Seconds. */
	void Boost(float Amount, float Seconds = 20.f);

	/** Current material strength (0 when off), boost (0..1) and whether the material loaded. */
	float GetStrength() const { return Strength; }
	float GetBoost() const { return float(FMath::Max(BoostNow, SkyBoost)); }
	bool IsActive() const { return MID != nullptr; }
	/** M_Aurora compiled for this scene's shader platform (false while compiling or after a compile error, when
	 *  Unreal would draw the default material on the caps). */
	bool IsMaterialReady() const;
	/** Magnetic pole of a hemisphere (0 north, 1 south) as a planet-frame direction. */
	FVector GetPole(int32 Hemi) const;
	/** Night weight the material applies at a planet-frame direction (1 = full night; 0 on the day side). */
	static double NightFactor(const FVector& PlanetDir, const FVector& SunPlanet);
	/** Whether the sky's old ribbon aurora has been hidden. */
	bool HidSkyRibbons() const { return bSkyHidden; }
	/** Radius of the cap mesh, km (sea radius + TopKm). */
	double GetShellRadiusKm() const { return ShellRadiusKm; }

	FDmAuroraTune Tune;

	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Shell;

private:
	void BuildShell();
	void HideSkyRibbons();
	double ReadSkyBoost(double& OutGain) const;

	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MID;
	double BoostNow = 0.0, BoostRate = 0.0, SkyBoost = 0.0, HeldBoost = 0.0;
	float Strength = 0.f;
	double ShellRadiusKm = 0.0;
	bool bSkyHidden = false;
	bool bOff = false;
	bool bTrace = false;
	bool bDebug = false;   // -auroradebug: the material colours each early-out
	int32 Frames = 0;
};
