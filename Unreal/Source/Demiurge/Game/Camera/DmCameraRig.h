// DEMIURGE — Game/Camera/DmCameraRig.h : the orbit -> surface camera as pure math (old SPEC-UI 1, DM.Camera).
// Owner: C3 camera-input.
//
// No UObject, no world access: ADmPawn feeds it input and a ground query and copies View() into its
// camera component every frame, and the cam-math scenario tests it directly. Everything is in the
// PLANET FRAME (DmCoords.h: origin at the planet centre, +Z north) and in KILOMETRES (the sim's
// kPlanetR = 100 units = 100 km, so one old SPEC-UI distance unit is one km here).
//
// State (Camera.state): Focus (unit direction the camera orbits), Zoom 0..1 (0 = far orbit, 1 =
// closest), Fwd (unit tangent at Focus = the heading: which way is "up" on screen). Fwd is moved by
// parallel transport whenever Focus moves (never derived from a global up), so the poles cause no
// flips and no NaN.
//
// Geometry (old SPEC-UI 1, numbers in km; where a number refers to height near the ground it is
// scaled by the relief factor 0.2 km per sim elevation unit of SPEC-UNREAL 1, see FDmCameraTune):
//   vertical fov 35 deg; eye distance from the centre at zoom 0
//     D0 = 1.08 x (R + 17) / sin(atan(tan(fov/2) x min(1, aspect)))     (~420 km landscape, ~880 km on a phone)
//   so the planet and its atmosphere shell fit in any orientation. The eye's height above the ground
//   UNDER THE EYE runs exponentially from D0 - R at zoom 0 to NearHeightKm at zoom 1 and is never less
//   than MinClearKm above the ground around the eye. The view looks straight down at the focus when
//   far and tilts toward the horizon as it closes in (0 deg until zoom 0.35, rising smoothly to
//   TiltMaxDeg at zoom 1), orbiting the terrain point under the focus.
//   Near / Far (km) follow the old formulas and are reported in View() for anyone who needs them;
//   Unreal renders with a reversed-Z infinite projection, so the pawn does not apply them.
#pragma once

#include "CoreMinimal.h"

/** Ground query: planet-frame unit direction -> radius in km of what the camera must stay above (the
 *  surface you would stand or float on: sea level on sea, land + lakes elsewhere). */
using FDmGroundFn = TFunctionRef<double(const FVector& /*PlanetDir*/)>;

struct FDmCameraTune
{
	double VFovDeg = 35.0;          // vertical field of view
	double RadiusKm = 100.0;        // datum radius (sim kPlanetR = 100 units)
	double ShellKm = 17.0;          // atmosphere shell that must fit at zoom 0 (old R + 17)
	double FitMargin = 1.08;
	double NearHeightKm = 1.2;      // height above the ground under the eye at zoom 1 (old 6 units x 0.2 km relief)
	double MinClearKm = 0.3;        // never closer to the ground than this anywhere (old 1.5 units x 0.2)
	double TiltStartZoom = 0.35;    // straight down until here
	// Tilt from the vertical at zoom 1. Old SPEC-UI: about 58 deg at 6 units above a 100-unit planet,
	// which put the horizon (dip 19.4 deg) 4.9 deg below the top of a 35 deg frame. At NearHeightKm the
	// dip is only 8.9 deg, so 58 deg would show no sky at all; 68 deg keeps the old framing (a band of
	// sky and the curved horizon at the top of the frame).
	double TiltMaxDeg = 68.0;
	double ZoomLambda = 7.0;        // smoothing rates (1/s) toward the zoom / heading targets
	double TurnLambda = 10.0;
	double TurnMaxSpeed = 6.0;      // rad/s: the heading never turns faster than about a turn per second
	double InertiaDecay = 3.0;      // orbit inertia after a drag (1/s)
	double InertiaMin = 0.002;      // rad/s below which inertia stops
	double ShakeDecay = 2.4;        // shake amplitude decay (1/s)
	double ShakeScale = 1.0;        // the 'shake' setting (0..1)
	double ShakeMaxDeg = 0.8;       // view jitter at amplitude 1
	double FlyArc = 0.30;           // zoom dip per radian of flight (long flights rise toward orbit)
	double GrabMaxGrazeDeg = 84.0;  // grab-drag falls back to pixel scale beyond this ray-to-normal angle
	// Ground reference (verifier, 2026-11-19): the eye keeps its height above a SMOOTHED ground, not the raw
	// ground under it, so it glides over rugged land instead of riding every ridge (raw: 0.37 km of bob at
	// 1.2 km). Spatial: the ground averaged over a disc of GroundAvgEye x height around the eye (and
	// GroundAvgTarget x height around the look-at point); temporal: exponential, faster when the ground rises.
	// The hard floor (MinClearKm above the real ground under and around the eye) is applied after it.
	double GroundAvgEye = 1.5;
	double GroundAvgTarget = 1.0;
	double GroundRiseLambda = 6.0;  // 1/s
	double GroundFallLambda = 2.5;  // 1/s
	double ZoomAnchorMaxRad = 0.05; // zoom toward the pointer: largest focus correction per frame (5 km)
};

struct FDmCameraView
{
	FVector Eye = FVector(420, 0, 0);       // km, planet frame
	FVector Target = FVector(100, 0, 0);    // km: the terrain point under the focus
	FVector Forward = FVector(-1, 0, 0);    // unit view direction
	FVector Up = FVector(0, 0, 1);          // unit screen up
	FVector Right = FVector(0, 1, 0);       // unit screen right (Unreal convention: Y axis of MakeFromXZ(Forward, Up))
	FVector FocusDir = FVector(1, 0, 0);
	double VFov = 0.0;              // radians, vertical
	double HFovDeg = 0.0;           // degrees, horizontal (what UCameraComponent::FieldOfView takes)
	double Aspect = 16.0 / 9.0;
	double NearKm = 0.25, FarKm = 500.0;
	double Zoom01 = 0.0;
	double AltitudeKm = 320.0;      // height of the eye above the ground beneath it
	double TiltRad = 0.0;
	double TargetDistKm = 320.0;    // eye -> target
};

class DEMIURGE_API FDmCameraRig
{
public:
	FDmCameraRig() { Reset(); }

	FDmCameraTune Tune;

	// ---------------------------------------------------------------- Camera API (old SPEC-UI 1)
	void Reset();
	void SetViewport(double WidthPx, double HeightPx);
	/** Per frame: fly-to, inertia, smoothing toward the zoom / heading targets, then the view. */
	void Update(double Dt, FDmGroundFn Ground);
	const FDmCameraView& View() const { return V; }
	/** Drag by pixels (screen x right, y down): the surface under the view follows the pointer. */
	void Orbit(double DxPx, double DyPx);
	/** Keyboard pan: fractions of the screen height per call along screen right / screen up. */
	void Pan(double RightFrac, double UpFrac);
	/** Positive = closer. Optional unit direction (planet frame) to zoom toward (pointer). */
	void ZoomBy(double Amount, const FVector* TowardDir = nullptr);
	/** Change heading (radians, positive = view turns clockwise, compass). Smoothed. */
	void Turn(double DAng);
	/** Eased great-circle flight; Zoom < 0 keeps the current zoom. */
	void FlyTo(const FVector& Dir, double Zoom = -1.0, double Seconds = 1.6);
	/** No animation. HeadingRad is a compass heading (0 = north up, clockwise). */
	void Jump(const FVector& Dir, double Zoom, double HeadingRad = 0.0);
	/** 0..1, decays; scaled by Tune.ShakeScale. */
	void Shake(double Amount);
	/** True while flying. */
	bool Busy() const { return bFlying; }

	// ---------------------------------------------------------------- grab-drag (mouse / finger)
	/** Start a drag at a screen point: grabs the point of the ground sphere under it. */
	void GrabBegin(const FVector2D& Px);
	/** Move the drag: rotates the camera so the grabbed point stays under Px. */
	void GrabMove(const FVector2D& Px);
	/** End the drag; the recent drag velocity carries on as inertia. */
	void GrabEnd();
	bool IsGrabbing() const { return bGrab; }
	void StopMotion();   // kill inertia, pending turn and flight

	// ---------------------------------------------------------------- queries
	FVector GetFocus() const { return Focus; }
	FVector GetFwd() const { return Fwd; }
	double GetZoom() const { return Zoom; }
	double GetTargetZoom() const { return TargetZoom; }
	/** Compass heading of Fwd (0 = north, clockwise), radians. */
	double HeadingRad() const;
	/** Eye height above the ground at a zoom (km), for the current viewport. */
	double HeightAt(double Zoom01) const;
	double TiltAt(double Zoom01) const;
	/** Eye distance from the centre at zoom 0 (km). */
	double OrbitDistance() const;
	/** View ray through a screen pixel from the current view (planet frame, km). */
	void ScreenRay(const FVector2D& Px, FVector& OutOrigin, FVector& OutDir) const;
	/** Project a planet-frame point (km) to pixels with the current view; false if behind. */
	bool Project(const FVector& P, FVector2D& OutPx) const;
	/** Inertia angular speed (rad/s). */
	double InertiaSpeed() const { return Omega.Size(); }
	double ShakeAmplitude() const { return ShakeAmp; }
	double ViewportW() const { return VpW; }
	double ViewportH() const { return VpH; }
	/** km on the ground per pixel at the target distance. */
	double KmPerPixel() const;
	/** True if every vector in the view is finite. */
	bool IsFinite() const;

private:
	void MoveFocus(const FQuat& Q);           // rotate Focus and transport Fwd (and the grab point)
	void Reorthonormalise();
	/** Dt > 0 advances the ground smoothing; Dt = 0 recomputes the view with the smoothing held. */
	void ComputeView(FDmGroundFn Ground, double Dt);
	/** Keep the zoom anchor under its pixel: rotate the focus so the pointer ray meets the anchor again. */
	void CorrectZoomAnchor(FDmGroundFn Ground);
	bool RayHitSphere(const FVector& O, const FVector& D, double Radius, FVector& OutHitDir) const;

	FVector Focus = FVector(1, 0, 0);
	FVector Fwd = FVector(0, 0, 1);
	double Zoom = 0.0, TargetZoom = 0.0;
	double PendingTurn = 0.0;
	FVector ZoomAnchor = FVector::ZeroVector;   // zoom toward this direction while the zoom settles
	FVector2D ZoomAnchorPx = FVector2D::ZeroVector;   // ... keeping it under this pixel
	double ZoomAnchorR = -1.0;                  // ground radius at the anchor (km), < 0 until queried
	bool bZoomAnchor = false;

	// smoothed ground references (km): under the eye and under the focus; snapped after a jump
	double EyeGroundS = 100.0, TargetGroundS = 100.0;
	bool bSnapGround = true;

	// inertia (rotation vector, rad/s, planet frame)
	FVector Omega = FVector::ZeroVector;
	FVector DragAccum = FVector::ZeroVector;    // rotation vector accumulated since the last Update
	double SinceDragMove = 1e9;

	// grab
	bool bGrab = false, bGrabValid = false;
	FVector GrabDir = FVector::ZeroVector;
	double GrabRadius = 100.0;
	FVector2D GrabLastPx = FVector2D::ZeroVector;

	// fly-to
	bool bFlying = false;
	FVector FlyFrom = FVector(1, 0, 0), FlyTo_ = FVector(1, 0, 0);
	double FlyZ0 = 0, FlyZ1 = 0, FlyT = 0, FlySeconds = 1.6, FlyAngle = 0;

	// shake
	double ShakeAmp = 0.0, ShakeTime = 0.0;

	double VpW = 1920.0, VpH = 1080.0;
	double TargetGround = 100.0;     // radius of the ground under the focus (km)
	FDmCameraView V;
};
