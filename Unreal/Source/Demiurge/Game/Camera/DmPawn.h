// DEMIURGE — Game/Camera/DmPawn.h : the player's camera pawn (old SPEC-UI 1 + 2). Owner: C3 camera-input.
//
// Registered as scene part "camera" (EDmPartKind::Control): the factory spawns the pawn and the player
// controller possesses it. The pawn owns the camera rig (FDmCameraRig: orbit -> surface descent,
// tilt, inertia, fly-to, shake, never below the ground) and the input router (UDmInputRouter: Enhanced
// Input actions and mapping context created in C++, mouse / keyboard / touch, hover picking, power
// arming, brush and click semantics, the brush ring, selection). Each frame (after the controller
// processed input): router -> rig.Update(dt, ground) -> the camera component follows the rig's view,
// carried by the turning planet (UDmSimSubsystem::GetPlanetToWorld), so the view stays over the same
// land while the sun and the terminator move.
//
// Scenarios drive it with Ctx.UseHarnessCamera(false) and ADmPawn::Find(World)->Jump(...). In the
// default "play" scenario (no -scenario, no -cam) the pawn adopts the harness camera's view on its
// first frame and takes over the view.
//
// Ground: the camera stays above UDmSimSubsystem::SurfaceRadiusUU (the sim surface: sea level on sea,
// land + lakes elsewhere). A renderer with more relief than the sim (the planet module's LOD surface)
// can register its own height with SetGroundProvider; the camera keeps above the higher of the two.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Game/Camera/DmCameraRig.h"
#include "DmPawn.generated.h"

class UCameraComponent;
class UDmInputRouter;
class ADmPlanet;
namespace dm { struct Event; struct World; }

/** Optional renderer height: planet-frame unit direction -> surface radius in uu (<= 0: unknown). */
using FDmGroundProvider = TFunction<double(const FVector& /*PlanetDir*/)>;

UCLASS()
class DEMIURGE_API ADmPawn : public APawn
{
	GENERATED_BODY()

public:
	ADmPawn();

	/** The world's camera pawn (the first one), or nullptr. */
	static ADmPawn* Find(const UObject* WorldContext);
	static void SetGroundProvider(FDmGroundProvider Provider);
	static void ClearGroundProvider();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// ------------------------------------------------------------ camera services (DM.Camera)
	FDmCameraRig& Rig() { return CameraRig; }
	const FDmCameraRig& Rig() const { return CameraRig; }
	void Jump(const FVector& PlanetDir, double Zoom, double HeadingRad = 0.0);
	void JumpLatLon(double LatDeg, double LonDeg, double Zoom, double HeadingDeg = 0.0);
	void FlyTo(const FVector& PlanetDir, double Zoom = -1.0, double Seconds = 1.6);
	void Shake(double Amount);
	/** Adopt an arbitrary world-space view (harness camera): focus = where it looks on the planet. */
	void AdoptView(const FVector& WorldLoc, const FRotator& WorldRot);
	/** Planet-frame (km) <-> world (uu) using the planet transform. */
	FVector PlanetKmToWorld(const FVector& Km) const;
	FVector WorldToPlanetDir(const FVector& World) const;
	/** Ground radius (km) under a planet-frame direction: the max of the sim surface and the provider. */
	double GroundKm(const FVector& PlanetDir) const;
	/** Rendered ground radius (km) under a direction: the planet module's LOD surface (ground or water,
	 *  full detail), or <= 0 when the scene has no planet. The rig keeps its hard floor above it. */
	double RenderedGroundKm(const FVector& PlanetDir) const;
	/** Rendered-ground queries made by the camera (tests: cost per frame). */
	mutable int64 FloorQueries = 0;
	/** Apply the rig's view to the camera component now (scenarios after a Jump). */
	void ApplyView();

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UDmInputRouter> Router;

	/** Frame statistics for tests: minimum clearance seen (km above the sim ground under the eye). */
	double MinAltitudeSeenKm = 1e9;
	int64 FramesTicked = 0;
	/** Game-thread cost of the last frame's camera work (input router + rig update + view), microseconds. */
	double LastCameraUs = 0.0;
	bool bAdoptHarnessView = true;

private:
	void OnSimEvent(const dm::Event& E);
	void UpdateViewport();

	FDmCameraRig CameraRig;
	FDelegateHandle SimHandle;
	mutable int32 GroundHint = -1;
	mutable TWeakObjectPtr<ADmPlanet> PlanetCache;
	mutable double PlanetLookupTime = -1e9;
	bool bAdopted = false;
};
