// DEMIURGE — Game/FX/DmFx.h : the effects module (SPEC-UNREAL 2, C6 fx; old SPEC-RENDER 10).
// Owner: C6 fx.
//
// One actor, ADmFx, registered as the scene part "fx" (EDmPartKind::World), so every scenario that
// builds the world has it. It listens to the sim (UDmSimSubsystem::OnSimEvent) and to world state
// (disasters, fires, plague, prayers, prophets) and draws every power and disaster as a readable,
// cinematic effect: comet and meteor entry, impact flash, fireball, shockwave, ejecta, glowing crater,
// steam, tsunami fronts, volcano eruptions (ash plume, lava fountain, lava glow, volcanic lightning),
// lightning, forest fire and smoke, rain and storms, bloom and seed waves, earthquake ripples and
// dust, plague miasma, blessing columns, prophet halos, the omen of peace, aurora, nuclear flash and
// mushroom cloud, the Ark's launch, battles, founding sparkles, age rays, miracle motes.
//
// How it draws: a pooled CPU particle system plus immediate quads (rings, bolts, beams), all turned
// into camera-facing / oriented quads each frame and drawn in ONE sorted procedural-mesh section with
// the material /Game/FX/M_Fx (Scripts/fx.py, shader Scripts/fx/fx.hlsl): premultiplied blending, so
// additive glow (alpha 0) and lit smoke share one draw. Smoke is unlit in the engine sense and lit by
// this module on the CPU: sun (with the terminator), sky ambient and the module's own light pulses
// (an ash plume glows orange above its lava at night). Light pulses are a small pool of point lights
// that light the real terrain. Camera shake goes out as the sim's UI event camera:shake (the camera
// module listens). Particles live in the planet frame and turn with the planet.
// Occlusion: M_Fx has the hardware depth test OFF and fades itself against SceneDepth with a
// tolerance that grows with distance (the depth test hid effects hugging the ground: the planet's
// rendered relief sits up to ~1 km above the sim surface and depth error grows with range); quads on
// the far side of the planet are culled on the CPU (VisibleFromCamera). M_FxOverlay (no occlusion at
// all) carries flash halos, which are light scattered in the air in front of the ground.
// Wildfires: a sim cell burns for two ticks only, so the module keeps a visual heat per cell that
// decays over ~8 s (flames first, then smouldering smoke) and draws the nearest 160 cells.
//
// Other modules:
//   ADmFx* Fx = ADmFx::Find(GetWorld());
//   Fx->Spawn(TEXT("smoke"), PlanetFramePos, 8);          // 'smoke' | 'dust' | 'sparks' | 'splash' | 'steam' | 'motes'
//   Fx->SetQuiet(true);                                  // time-lapse / snapshot: draw nothing
//   Fx->SetAurora(1.f, 30.f);                            // aurora surge for 30 s (also after comet impacts and nukes)
//   Fx->Play(TEXT("meteor"), Cell);                      // preview any effect without the sim (tools, scenarios)
// Switches: -fxgain=<x> (brightness trim), -fxquality=0|1|2 (particle caps 2000 / 4000 / 6000),
// -fxstats (log counts and timings every 2 s).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/FX/DmFxRender.h"
#include "Game/FX/DmFxTypes.h"
#include "DmFx.generated.h"

namespace dm { struct World; struct Event; }
class UDmSimSubsystem;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UProceduralMeshComponent;
class ADmFx;

/** One running effect. Tick returns false when it has finished (its particles fade on their own). */
struct FDmFxEffect
{
	virtual ~FDmFxEffect() {}
	virtual bool Tick(ADmFx& Fx, float Dt) = 0;
	/** A held brush's next pulse (wave 4b): continue this effect at Cell instead of starting another (false: not supported). */
	virtual bool Refresh(int32 Cell) { return false; }
	float T = 0.f;           // seconds since start (advanced by the actor before each Tick)
	int32 DisasterId = -1;   // effects that follow a sim disaster
	const TCHAR* Kind = TEXT("fx");
};

struct FDmFxLightReq
{
	FVector P = FVector::ZeroVector;     // planet frame
	FLinearColor Col = FLinearColor::White;
	float Intensity = 0.f;               // scene units at the centre
	float Radius = 1.f;                  // uu
};

UCLASS()
class DEMIURGE_API ADmFx : public AActor
{
	GENERATED_BODY()

public:
	ADmFx();
	static ADmFx* Find(const UWorld* World);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	// ------------------------------------------------------------ for other modules
	/** Route one sim event (OnSimEvent calls this; tools may call it with synthetic events). */
	void HandleEvent(const dm::Event& E);
	/** Generic emitter (old FX.spawn): 'smoke' | 'dust' | 'sparks' | 'splash' | 'steam' | 'motes'; unknown types are ignored. */
	void Spawn(const FString& Type, const FVector& PlanetPos, int32 Count = 8, float Size = 1.f, const FLinearColor* Color = nullptr, float RiseScale = 1.f);
	/** Preview an effect at a cell without the sim: meteor, comet, seaimpact, lightning, quake, nuke, heal, inspire,
	 *  prophet, peace, bloom, seed, rain, plague, sculpt, founded, age, battle, ark, destroyed, splash, aurora, fire. */
	bool Play(const FString& What, int32 Cell, float Strength = 1.f);
	void SetQuiet(bool bInQuiet);
	bool IsQuiet() const { return bQuiet; }
	/** Aurora surge: strength 0..1 held for Seconds, then fading. */
	void SetAurora(float Strength, float Seconds);
	float GetAurora() const { return AuroraNow; }
	int32 GetParticleCount() const { return Particles.Num(); }
	int32 GetEffectCount() const { return Effects.Num(); }
	int32 GetQuadCount() const { return Renderer.LastDrawn; }
	int32 GetPendingQuads() const { return Renderer.NumPending(); }

	// ------------------------------------------------------------ for effects (planet frame, uu, seconds)
	const dm::World* W() const;
	UDmSimSubsystem* GetSim() const { return SimSub; }
	DmFx::FRand& R() { return Rng; }
	double Now() const { return Clock; }
	/** Add a particle (nullptr when the cap is reached). */
	DmFx::FParticle* Emit(const DmFx::FParticle& P);
	void AddEffect(FDmFxEffect* E);
	bool HasEffectFor(int32 DisasterId) const;
	/** The newest running effect of this Kind (the TEXT literal its factory set), or nullptr. */
	FDmFxEffect* FindEffect(const TCHAR* Kind) const;

	/** Quads from planet-frame geometry. bLit: Col.rgb is an albedo, lit by sun / sky / FX lights. */
	void AddFacing(const FVector& P, float Half, DmFx::EShape Shape, const FLinearColor& Col, bool bLit = false, uint8 Seed = 0, uint8 Param = 0, float Rot = 0.f, float Age = 0.f);
	/** A camera-facing glow drawn WITHOUT depth test (flash halos: light scattered in the air in front of the
	 *  ground); skipped when P is behind the planet as seen from the camera. */
	void AddFlash(const FVector& P, float Half, const FLinearColor& Col);
	bool VisibleFromCamera(const FVector& P) const;
	void AddAxis(const FVector& P0, const FVector& P1, float HalfWidth, DmFx::EShape Shape, const FLinearColor& Col, bool bLit = false, uint8 Seed = 0, uint8 Param = 0, float Age = 0.f);
	void AddFlat(const FVector& P, float HalfU, float HalfV, float Rot, DmFx::EShape Shape, const FLinearColor& Col, bool bLit = false, uint8 Seed = 0, uint8 Param = 0, float Soft = -1.f, float Age = 0.f);
	void AddUpright(const FVector& Base, float HalfW, float HalfH, DmFx::EShape Shape, const FLinearColor& Col, bool bLit = false, uint8 Seed = 0, uint8 Param = 0, float Age = 0.f);
	/** A planet-frame quad (centre and half axes in the planet frame). */
	void AddPlanetQuad(const FVector& C, const FVector& U, const FVector& V, DmFx::EShape Shape, const FLinearColor& Col, bool bLit, uint8 Seed, uint8 Param, float Soft = -1.f, float Age = 0.f, float U0 = 0.f, float U1 = 1.f);
	/** A ring of Segments quads around the unit direction Centre at angular radius Ang (rad), Height uu above
	 *  the local surface (or above the sea when bSea), HalfWidth radial; Mask: 0 everywhere, 1 sea only, 2 land only. */
	void AddRing(const FVector& Centre, double Ang, double Height, float HalfWidth, DmFx::EShape Shape, const FLinearColor& Col,
		bool bLit, int32 Segments, int32 Mask = 0, uint8 Param = 0, float Soft = -1.f, uint8 Seed = 0);
	void AddLight(const FVector& P, const FLinearColor& Col, float Intensity, float Radius);
	/** Ask the camera for a shake (0..1), scaled down with the camera's distance from P (planet frame). */
	void Shake(float Amount, const FVector& P, double RangeUU);

	FLinearColor LightAt(const FVector& P) const;   // light reaching a planet-frame point (sun + sky + FX lights)
	float DayAt(const FVector& P) const;           // 0 night .. 1 day (soft terminator, higher points see the sun longer)
	FVector WindAt(const FVector& P) const;        // planet-frame drift velocity, uu/s
	FVector SunPlanet() const;                     // planet-frame unit vector toward the sun
	FVector CamPlanet() const { return CamP; }     // camera position, planet frame
	FVector CamRightPlanet() const { return CamRightP; }   // screen right / up, planet frame
	FVector CamUpPlanet() const { return CamUpP; }
	double CamDist(const FVector& P) const { return FVector::Dist(P, CamP); }
	/** uu per screen pixel at the distance of P. */
	float UUPerPixel(const FVector& P) const;

	FVector CellDir(int32 Cell) const;
	FVector CellSurface(int32 Cell, double Lift = 0.0) const;   // surface (sea level on sea) + Lift along up
	FVector CellGround(int32 Cell, double Lift = 0.0) const;    // ground (sea floor on sea) + Lift
	bool IsSeaCell(int32 Cell) const;
	int32 CellAt(const FVector& Dir) const;
	/** Land under a planet-frame direction at sub-cell precision: elevation above sea level interpolated over the
	 *  sim triangle that holds the direction (as the rendered relief is), not the nearest cell, whose Voronoi region
	 *  straddles the drawn coastline by up to a cell (~1.7 km). For effects that must stop at the shore. */
	bool IsLandAt(const FVector& Dir) const;
	/** Surface radius (uu) under a planet-frame direction: land elev + water, or the sea. */
	double SurfaceR(const FVector& Dir) const;
	/** A point on the surface at angular offset (Ang rad, toward azimuth Az rad from north) from Centre. */
	FVector Offset(const FVector& CentreDir, double Ang, double Az) const;
	static void TangentBasis(const FVector& Up, FVector& East, FVector& North);

	float Gain = 1.f;              // -fxgain
	int32 Quality = 2;             // -fxquality
	int32 MaxParticles = 6000;
	/** Exposure compensation (verifier fix): the sky lifts exposure up to ~2.6 EV on the night side (and in the
	 *  molten genesis), which turned flashes, FX-lit plumes and lava light into white / pink blow-outs at night.
	 *  Emissive output is scaled by 2^(-0.7 x lift), FX point lights by 2^(-0.5 x lift): night effects stay
	 *  brighter than by day (the eye has adapted) but keep their shape. 1 when the sky adds no lift. */
	float GetExposureComp() const { return ExposureComp; }

private:
	void OnSimEvent(const dm::Event& E);
	void OnWorldChanged(const dm::World* NewWorld);
	void UpdateView();
	void UpdateExposure();
	void PrepareLights();
	void ScanDisasters();
	void UpdateAmbient(float Dt);
	void SimulateParticles(float Dt);
	void DrawParticles();
	void ApplyLights();
	void DrawTest();
	void Submit(DmFx::FQuad& Q, bool bLit, const FVector& PlanetC);

	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Mesh;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> OverlayMesh;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> OverlayMid;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> LightPool;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Mid;

	UDmSimSubsystem* SimSub = nullptr;
	FDelegateHandle EventHandle, WorldHandle;
	FDmFxRenderer Renderer;
	FDmFxRenderer Overlay;
	TArray<DmFx::FParticle> Particles;
	TArray<TUniquePtr<FDmFxEffect>> Effects;
	TArray<FDmFxLightReq> LightReqs, LightPrev;
	TArray<float> PendingShake;
	DmFx::FRand Rng{ 0xF00DCAFEu };
	double Clock = 0;
	bool bQuiet = false;
	bool bStats = false;
	bool bTest = false;
	bool bRingOverlay = false;   // -fxringoverlay: debug, rings through the overlay
	double StatsAt = 0;
	float ExposureComp = 1.f;    // emissive scale against the sky's exposure lift (see GetExposureComp)
	float LightComp = 1.f;       // point-light scale against the same lift
	float GainSet = -1.f;        // last Gain x ExposureComp written to the materials
	bool bLightsSorted = false;  // LightReqs sorted strongest first this frame (PrepareLights)

	FTransform P2W = FTransform::Identity;
	FVector CamP = FVector::ZeroVector;      // camera, planet frame
	FVector CamRightP = FVector::RightVector, CamUpP = FVector::UpVector;
	FVector SunP = FVector(1, 0, 0);         // planet frame
	FVector SunW = FVector(1, 0, 0);         // world

	// ambient state
	double AmbientAcc = 0;
	TArray<int32> FireCells;                  // nearest burning cells (refreshed at 4 Hz)
	TMap<int32, float> FireHeat;              // visual heat per recently burning cell (decays over ~8 s)
	double FireScanAt = -1;
	float AuroraTarget = 0.f, AuroraHold = 0.f, AuroraNow = 0.f;
	int32 BeaconCell = -1;
	double LastBloomAt = -10;
	double BeaconScanAt = -1;
	TMap<int32, float> EmitAcc;               // per-key emission accumulators (fires, plague, ...)
	TSet<int32> Tracked;                      // disaster ids that have an effect
	int32 LastWorldN = -1;
};
