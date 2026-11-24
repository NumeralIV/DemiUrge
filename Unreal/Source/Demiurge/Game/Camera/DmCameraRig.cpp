// DEMIURGE — Game/Camera/DmCameraRig.cpp : see DmCameraRig.h. Owner: C3 camera-input.

#include "Game/Camera/DmCameraRig.h"
#include "Game/Core/DmCoords.h"

namespace
{
	inline double Smooth01(double T) { T = FMath::Clamp(T, 0.0, 1.0); return T * T * (3.0 - 2.0 * T); }
	inline double Smoother01(double T) { T = FMath::Clamp(T, 0.0, 1.0); return T * T * T * (T * (T * 6.0 - 15.0) + 10.0); }
	inline bool Finite(const FVector& V) { return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z); }

	/** Great-circle interpolation of unit vectors; exact opposites go through Via (a tangent of A). */
	FVector SlerpDir(const FVector& A, const FVector& B, double T, const FVector& Via)
	{
		const double C = FMath::Clamp(FVector::DotProduct(A, B), -1.0, 1.0);
		const double Ang = FMath::Acos(C);
		if (Ang < 1e-9) return B;
		FVector Perp = B - A * C;
		if (Perp.SizeSquared() < 1e-12) Perp = Via;   // antipodal: any great circle, the heading's
		Perp.Normalize();
		return (A * FMath::Cos(Ang * T) + Perp * FMath::Sin(Ang * T)).GetSafeNormal();
	}

	/** Rotation taking unit A to unit B along their great circle (identity when equal). */
	FQuat Between(const FVector& A, const FVector& B)
	{
		const FQuat Q = FQuat::FindBetweenNormals(A, B);
		return Q.ContainsNaN() ? FQuat::Identity : Q;
	}
}

void FDmCameraRig::Reset()
{
	Focus = DmCoords::LatLonToDir(20.0, 30.0);
	FVector E, N;
	DmCoords::Basis(Focus, E, N);
	Fwd = N;
	Zoom = TargetZoom = 0.0;
	ZoomVel = 0.0;
	PanIn = PanVel = FVector2D::ZeroVector;
	PendingTurn = 0.0;
	bZoomAnchor = false;
	Omega = DragAccum = FVector::ZeroVector;
	bGrab = bGrabValid = bFlying = false;
	ShakeAmp = 0.0;
	TargetGround = Tune.RadiusKm;
	bSnapGround = true;
	// a first view without a ground query (datum sphere)
	ComputeView([this](const FVector&) { return Tune.RadiusKm; }, 0.0);
	bSnapGround = true;   // the first real ground snaps (no glide up from the datum)
}

void FDmCameraRig::SetViewport(double WidthPx, double HeightPx)
{
	if (WidthPx >= 1.0 && HeightPx >= 1.0) { VpW = WidthPx; VpH = HeightPx; }
}

double FDmCameraRig::OrbitDistance() const
{
	const double Half = FMath::DegreesToRadians(Tune.VFovDeg) * 0.5;
	const double Aspect = VpW / FMath::Max(VpH, 1.0);
	const double FitHalf = FMath::Atan(FMath::Tan(Half) * FMath::Min(1.0, Aspect));
	return Tune.FitMargin * (Tune.RadiusKm + Tune.ShellKm) / FMath::Max(FMath::Sin(FitHalf), 1e-3);
}

double FDmCameraRig::HeightAt(double Z) const
{
	const double H0 = FMath::Max(OrbitDistance() - Tune.RadiusKm, Tune.NearHeightKm + 1e-3);
	const double H1 = FMath::Max(Tune.NearHeightKm, 1e-3);
	return H0 * FMath::Pow(H1 / H0, FMath::Clamp(Z, 0.0, 1.0));
}

double FDmCameraRig::TiltAt(double Z) const
{
	const double S = (Z - Tune.TiltStartZoom) / FMath::Max(1.0 - Tune.TiltStartZoom, 1e-3);
	return FMath::DegreesToRadians(Tune.TiltMaxDeg) * Smooth01(S);
}

double FDmCameraRig::KmPerPixel() const
{
	return 2.0 * V.TargetDistKm * FMath::Tan(V.VFov * 0.5) / FMath::Max(VpH, 1.0);
}

double FDmCameraRig::HeadingRad() const
{
	FVector E, N;
	DmCoords::Basis(Focus, E, N);
	return FMath::Atan2(FVector::DotProduct(Fwd, E), FVector::DotProduct(Fwd, N));
}

void FDmCameraRig::Reorthonormalise()
{
	Focus = Focus.GetSafeNormal();
	if (Focus.IsNearlyZero()) Focus = FVector(1, 0, 0);
	FVector F = Fwd - Focus * FVector::DotProduct(Fwd, Focus);
	if (F.SizeSquared() < 1e-12)
	{
		FVector E, N;
		DmCoords::Basis(Focus, E, N);
		F = N;
	}
	Fwd = F.GetSafeNormal();
}

void FDmCameraRig::MoveFocus(const FQuat& Q)
{
	Focus = Q.RotateVector(Focus);
	Fwd = Q.RotateVector(Fwd);
	Reorthonormalise();
}

void FDmCameraRig::StopMotion()
{
	Omega = DragAccum = FVector::ZeroVector;
	PendingTurn = 0.0;
	bFlying = false;
	bZoomAnchor = false;
	PanIn = PanVel = FVector2D::ZeroVector;
}

// ------------------------------------------------------------------ input

void FDmCameraRig::Orbit(double DxPx, double DyPx)
{
	if (DxPx == 0.0 && DyPx == 0.0) return;
	bFlying = false;
	FVector RightT = V.Right - Focus * FVector::DotProduct(V.Right, Focus);
	if (RightT.SizeSquared() < 1e-12) RightT = FVector::CrossProduct(Focus, Fwd);
	RightT.Normalize();
	// the camera moves opposite to the surface: drag right -> camera left, drag down -> camera forward
	const FVector M = (-RightT * DxPx + Fwd * DyPx) * KmPerPixel();
	const double Ang = M.Size() / FMath::Max(TargetGround, 1.0);
	if (Ang < 1e-12) return;
	const FVector Dir = M / M.Size();
	const FVector NewF = (Focus * FMath::Cos(Ang) + Dir * FMath::Sin(Ang)).GetSafeNormal();
	const FQuat Q = Between(Focus, NewF);
	ZoomAnchorR = -1.0;   // a settling zoom anchor travels with the drag (re-derived from the moved view)
	if (bGrab)
	{
		FVector Axis; double A;
		Q.ToAxisAndAngle(Axis, A);
		DragAccum += Axis * A;
		SinceDragMove = 0.0;
	}
	MoveFocus(Q);
}

void FDmCameraRig::Pan(double RightFrac, double UpFrac)
{
	Orbit(-RightFrac * VpH, UpFrac * VpH);
}

void FDmCameraRig::ZoomBy(double Amount, const FVector* TowardDir)
{
	if (Amount == 0.0) return;
	bFlying = false;
	if (!FMath::IsFinite(Amount)) return;
	TargetZoom = FMath::Clamp(TargetZoom + Amount, 0.0, 1.0);
	if (TowardDir && !TowardDir->ContainsNaN() && !TowardDir->IsNearlyZero())
	{
		const FVector NewDir = TowardDir->GetSafeNormal();
		// (verifier, wave 4b) a notch that lands while an anchor is still held under the same pointer pixel keeps that
		// anchor: re-anchoring on whatever the pointer covers NOW baked any residual of the previous notch into the zoom
		// (zooming at the limb from orbit, the point under the pointer ended 700-900 px away at 11 km)
		bool bKeep = false;
		if (bZoomAnchor && ZoomAnchorR > 0.0)
		{
			FVector2D Px;
			bKeep = Project(NewDir * ZoomAnchorR, Px) && FVector2D::Distance(Px, ZoomAnchorPx) < 4.0;
		}
		if (!bKeep)
		{
			// the pixel it sits under is taken from a fresh view at the next Update (ZoomAnchorR < 0: pending)
			ZoomAnchor = NewDir;
			ZoomAnchorR = -1.0;
		}
		bZoomAnchor = true;
	}
	else bZoomAnchor = false;
}

void FDmCameraRig::Turn(double DAng)
{
	if (!FMath::IsFinite(DAng)) return;
	// at most two full turns pending: a garbage input must not spin the view for seconds
	PendingTurn = FMath::Clamp(PendingTurn + DAng, -2.0 * UE_TWO_PI, 2.0 * UE_TWO_PI);
}

void FDmCameraRig::FlyTo(const FVector& Dir, double InZoom, double Seconds)
{
	const FVector D = Dir.GetSafeNormal();
	if (D.IsNearlyZero()) return;
	Omega = DragAccum = FVector::ZeroVector;
	bZoomAnchor = false;
	ZoomVel = 0.0;
	PanIn = PanVel = FVector2D::ZeroVector;
	bFlying = true;
	FlyFrom = Focus;
	FlyTo_ = D;
	FlyZ0 = Zoom;
	FlyZ1 = InZoom < 0.0 ? TargetZoom : FMath::Clamp(InZoom, 0.0, 1.0);
	FlyAngle = FMath::Acos(FMath::Clamp(FVector::DotProduct(FlyFrom, FlyTo_), -1.0, 1.0));
	FlySeconds = FMath::Max(Seconds, 0.05);
	FlyT = 0.0;
}

void FDmCameraRig::Jump(const FVector& Dir, double InZoom, double HeadingRadians)
{
	const FVector D = Dir.GetSafeNormal();
	if (D.IsNearlyZero()) return;
	StopMotion();
	bGrab = bGrabValid = false;
	Focus = D;
	FVector E, N;
	DmCoords::Basis(Focus, E, N);
	const double Hd = FMath::IsFinite(HeadingRadians) ? HeadingRadians : 0.0;
	Fwd = N * FMath::Cos(Hd) + E * FMath::Sin(Hd);
	Reorthonormalise();
	Zoom = TargetZoom = FMath::IsFinite(InZoom) ? FMath::Clamp(InZoom, 0.0, 1.0) : 0.0;
	ZoomVel = 0.0;
	bSnapGround = true;
}

void FDmCameraRig::Shake(double Amount)
{
	if (!(Amount > 0.0)) return;
	ShakeAmp = FMath::Clamp(ShakeAmp + Amount, 0.0, 1.0);
}

bool FDmCameraRig::RayHitSphere(const FVector& O, const FVector& D, double Radius, FVector& OutHitDir) const
{
	const double B = FVector::DotProduct(O, D);
	const double C = O.SizeSquared() - Radius * Radius;
	const double Disc = B * B - C;
	if (Disc < 0.0) return false;
	const double S = FMath::Sqrt(Disc);
	double T = -B - S;
	if (T < 0.0) T = -B + S;
	if (T < 0.0) return false;
	OutHitDir = (O + D * T).GetSafeNormal();
	return !OutHitDir.IsNearlyZero();
}

void FDmCameraRig::GrabBegin(const FVector2D& Px)
{
	bFlying = false;
	bZoomAnchor = false;
	Omega = DragAccum = FVector::ZeroVector;
	PendingTurn = 0.0;
	bGrab = true;
	SinceDragMove = 1e9;
	GrabLastPx = Px;
	GrabRadius = FMath::Max(TargetGround, 1.0);
	FVector O, D;
	ScreenRay(Px, O, D);
	bGrabValid = RayHitSphere(O, D, GrabRadius, GrabDir);
}

void FDmCameraRig::GrabMove(const FVector2D& Px)
{
	if (!bGrab) return;
	const FVector2D Delta = Px - GrabLastPx;
	GrabLastPx = Px;
	if (Delta.IsNearlyZero()) return;
	FVector O, D, Hit;
	ScreenRay(Px, O, D);
	if (bGrabValid && RayHitSphere(O, D, GrabRadius, Hit))
	{
		const double CosGraze = FVector::DotProduct(-D, Hit);
		if (CosGraze > FMath::Cos(FMath::DegreesToRadians(Tune.GrabMaxGrazeDeg)))
		{
			FQuat Q = Between(Hit, GrabDir);
			FVector Axis; double A;
			Q.ToAxisAndAngle(Axis, A);
			if (A > 0.6) { A = 0.6; Q = FQuat(Axis, A); }
			if (A > 1e-12)
			{
				DragAccum += Axis * A;
				SinceDragMove = 0.0;
				ZoomAnchorR = -1.0;
				MoveFocus(Q);
			}
			return;
		}
	}
	// off the planet or grazing: pixel scale, and grab again where the pointer is now
	Orbit(Delta.X, Delta.Y);
	bGrabValid = RayHitSphere(O, D, GrabRadius, GrabDir);
}

void FDmCameraRig::GrabEnd()
{
	if (!bGrab) return;
	bGrab = false;
	bGrabValid = false;
	if (SinceDragMove > 0.08) Omega = FVector::ZeroVector;   // the pointer rested before release: no fling
	DragAccum = FVector::ZeroVector;
}

// ------------------------------------------------------------------ per frame

void FDmCameraRig::Update(double Dt, FDmGroundFn Ground)
{
	Dt = FMath::Clamp(FMath::IsFinite(Dt) ? Dt : 0.0, 0.0, 0.25);

	if (bFlying)
	{
		FlyT += Dt / FlySeconds;
		const double E = Smoother01(FlyT);
		const FVector NewF = SlerpDir(FlyFrom, FlyTo_, E, Fwd);
		MoveFocus(Between(Focus, NewF));
		const double Dip = Tune.FlyArc * FlyAngle * FMath::Sin(UE_PI * E);
		Zoom = TargetZoom = FMath::Clamp(FMath::Lerp(FlyZ0, FlyZ1, E) - Dip, 0.0, 1.0);
		if (FlyT >= 1.0)
		{
			bFlying = false;
			Zoom = TargetZoom = FlyZ1;
		}
	}

	// drag velocity (rotation vector per second), then inertia once released
	if (bGrab)
	{
		SinceDragMove += Dt;
		if (Dt > 0.0)
		{
			const FVector Inst = DragAccum / Dt;
			const double K = 1.0 - FMath::Exp(-Dt * 18.0);
			Omega = Omega + (Inst - Omega) * K;
		}
		DragAccum = FVector::ZeroVector;
	}
	else if (!Omega.IsNearlyZero())
	{
		const double W = Omega.Size();
		if (W < Tune.InertiaMin || !FMath::IsFinite(W)) Omega = FVector::ZeroVector;
		else
		{
			MoveFocus(FQuat(Omega / W, W * Dt));
			Omega *= FMath::Exp(-Tune.InertiaDecay * Dt);
		}
	}

	// smoothed pan (keyboard / edge): ease toward the requested velocity, glide out when released
	{
		const FVector2D Want = PanIn;
		PanIn = FVector2D::ZeroVector;
		if (bGrab || bFlying) PanVel = FVector2D::ZeroVector;
		else if (Dt > 0.0)
		{
			const bool bHeld = !Want.IsNearlyZero();
			const double L = bHeld ? Tune.PanAccel : Tune.PanDecel;
			PanVel += (Want - PanVel) * (1.0 - FMath::Exp(-L * Dt));
			if (!bHeld && PanVel.Size() < 2e-3) PanVel = FVector2D::ZeroVector;
			if (!PanVel.IsNearlyZero()) Pan(PanVel.X * Dt, PanVel.Y * Dt);
		}
	}

	// heading: rotate Fwd about the focus normal (compass: north -> east is positive)
	if (PendingTurn != 0.0)
	{
		double Step = PendingTurn * (1.0 - FMath::Exp(-Tune.TurnLambda * Dt));
		if (FMath::Abs(PendingTurn) < 1e-5) Step = PendingTurn;
		Step = FMath::Clamp(Step, -Tune.TurnMaxSpeed * Dt, Tune.TurnMaxSpeed * Dt);
		PendingTurn -= Step;
		ZoomAnchorR = -1.0;
		FVector E, N;
		DmCoords::Basis(Focus, E, N);
		const double H = FMath::Atan2(FVector::DotProduct(Fwd, E), FVector::DotProduct(Fwd, N)) + Step;
		Fwd = N * FMath::Cos(H) + E * FMath::Sin(H);
		Reorthonormalise();
	}

	// zoom toward the target, keeping the ground under the pointer under the pointer (true zoom-to-cursor:
	// after each step the focus is rotated so the pointer's ray meets the anchor again, like grab-drag)
	{
		if (bZoomAnchor && ZoomAnchorR < 0.0)
		{
			ComputeView(Ground, 0.0);   // a fresh view of the current focus
			const double R = Ground(ZoomAnchor);
			ZoomAnchorR = (FMath::IsFinite(R) && R > 1.0) ? R : Tune.RadiusKm;
			FVector2D Px;
			const bool bOn = Project(ZoomAnchor * ZoomAnchorR, Px) && Px.X > -0.25 * VpW && Px.X < 1.25 * VpW && Px.Y > -0.25 * VpH && Px.Y < 1.25 * VpH;
			if (bOn) ZoomAnchorPx = Px;
			else bZoomAnchor = false;
		}
		const double Z0 = Zoom;
		if (!bFlying && Dt > 0.0)
		{
			// critically damped spring toward the target (exact step for a constant target), then the speed cap
			const double W = FMath::Max(Tune.ZoomOmega, 0.1);
			const double X = Zoom - TargetZoom;
			const double Ex = FMath::Exp(-W * Dt);
			const double C = ZoomVel + W * X;
			double NewX = (X + C * Dt) * Ex;
			double NewV = (ZoomVel - W * C * Dt) * Ex;
			const double MaxStep = FMath::Max(Tune.ZoomMaxRate, 0.01) * Dt;
			if (FMath::Abs(NewX - X) > MaxStep)
			{
				NewX = X + FMath::Sign(NewX - X) * MaxStep;
				NewV = FMath::Sign(NewX - X) * Tune.ZoomMaxRate;
			}
			Zoom = TargetZoom + NewX;
			ZoomVel = NewV;
			if (Zoom <= 0.0 || Zoom >= 1.0) { Zoom = FMath::Clamp(Zoom, 0.0, 1.0); ZoomVel = 0.0; }
			if (FMath::Abs(Zoom - TargetZoom) < 1e-4 && FMath::Abs(ZoomVel) < 2e-3) { Zoom = TargetZoom; ZoomVel = 0.0; }
			if (!FMath::IsFinite(Zoom)) { Zoom = TargetZoom; ZoomVel = 0.0; }
		}
		bAnchorPass = bZoomAnchor && Zoom != Z0;
		// the correction this frame may move the focus by about what the zoom step itself displaced the anchor
		// (angle to the anchor x ln(height ratio) per zoom unit, ~3 rad / zoom at most, with margin): a residual
		// left by rugged ground then drains over the next frames instead of snapping in one
		// (verifier, wave 4b) ... divided by the cosine of the pointer ray's incidence on the ground at the anchor: a
		// grazing anchor (near the limb from orbit, near the horizon at the ground) moves that much more per zoom step
		double Graze = 1.0;
		if (bAnchorPass && ZoomAnchorR > 0.0)
		{
			FVector O, D;
			ScreenRay(ZoomAnchorPx, O, D);
			Graze = 1.0 / FMath::Clamp(-FVector::DotProduct(D, ZoomAnchor), 0.12, 1.0);
		}
		AnchorMaxThisFrame = 8.0 * Graze * FMath::Abs(Zoom - Z0) + 0.002;
		if (Zoom == TargetZoom && !bAnchorPass) bZoomAnchor = false;
	}

	ShakeTime += Dt;
	ShakeAmp *= FMath::Exp(-Tune.ShakeDecay * Dt);
	if (ShakeAmp < 1e-4) ShakeAmp = 0.0;

	// advance the ground smoothing FIRST, then pin the zoom anchor under its pixel against that final eye height
	// (correcting before the smoothing moved the eye left the anchor drifting over rising ground)
	ComputeView(Ground, Dt);
	if (bAnchorPass)
	{
		for (int32 It = 0; It < 3; ++It)
		{
			CorrectZoomAnchor(Ground);
			ComputeView(Ground, 0.0);
		}
		bAnchorPass = false;
		if (Zoom == TargetZoom) bZoomAnchor = false;
	}
}

void FDmCameraRig::CorrectZoomAnchor(FDmGroundFn Ground)
{
	if (!bZoomAnchor || ZoomAnchorR < 0.0) return;
	FVector O, D, Hit;
	ScreenRay(ZoomAnchorPx, O, D);
	if (!RayHitSphere(O, D, ZoomAnchorR, Hit)) return;   // the pixel looks past the limb now: leave it
	FQuat Q = Between(Hit, ZoomAnchor);
	FVector Axis; double A;
	Q.ToAxisAndAngle(Axis, A);
	if (!(A > 1e-12) || !FMath::IsFinite(A)) return;
	const double MaxA = AnchorMaxThisFrame;   // the budget left this frame (Update)
	if (A > MaxA) Q = FQuat(Axis, MaxA);
	AnchorMaxThisFrame = FMath::Max(AnchorMaxThisFrame - FMath::Min(A, MaxA), 0.0);
	MoveFocus(Q);
}

void FDmCameraRig::ComputeView(FDmGroundFn Ground, double Dt)
{
	const double Aspect = VpW / FMath::Max(VpH, 1.0);
	const double VFov = FMath::DegreesToRadians(Tune.VFovDeg);
	const double Tilt = TiltAt(Zoom);
	const double H = HeightAt(Zoom);

	auto G = [&Ground, this](const FVector& D)
	{
		const double R = Ground(D);
		return (FMath::IsFinite(R) && R > 1.0) ? R : Tune.RadiusKm;
	};
	// the higher of the sim ground and the rendered ground (FloorFn): for the hard floor and the altitude
	auto GF = [&G, this](const FVector& D)
	{
		double R = G(D);
		if (FloorFn)
		{
			const double F = FloorFn(D);
			if (FMath::IsFinite(F) && F > R) R = F;
		}
		return R;
	};

	// the ground averaged over a disc of RhoKm around a direction (centre 0.4, six around it 0.1 each)
	auto GAvg = [&G, this](const FVector& D, double RhoKm)
	{
		const double A = RhoKm / FMath::Max(Tune.RadiusKm, 1.0);
		if (!(A > 1e-6)) return G(D);
		FVector E, N;
		DmCoords::Basis(D, E, N);
		const double CA = FMath::Cos(A), SA = FMath::Sin(A);
		double S = 0.4 * G(D);
		for (int32 K = 0; K < 6; ++K)
		{
			const double Phi = K * (UE_TWO_PI / 6.0);
			S += 0.1 * G((D * CA + (E * FMath::Cos(Phi) + N * FMath::Sin(Phi)) * SA).GetSafeNormal());
		}
		return S;
	};
	// temporal smoothing: snapped after a jump, held when Dt = 0, faster when the ground rises
	auto Follow = [this, Dt](double& S, double Raw)
	{
		if (bSnapGround || !FMath::IsFinite(S)) { S = Raw; return; }
		if (!(Dt > 0.0)) return;
		const double L = Raw > S ? Tune.GroundRiseLambda : Tune.GroundFallLambda;
		S += (Raw - S) * (1.0 - FMath::Exp(-L * Dt));
	};

	Follow(TargetGroundS, GAvg(Focus, FMath::Clamp(Tune.GroundAvgTarget * H, 0.0, 20.0)));
	TargetGround = TargetGroundS;
	const FVector T = Focus * TargetGround;
	const FVector Back = (Focus * FMath::Cos(Tilt) - Fwd * FMath::Sin(Tilt)).GetSafeNormal();

	// distance along Back that puts the eye RefR from the centre (|T + Back D| = RefR), never in front of
	// the target (the floor below lifts it if the ground under the eye is that high)
	const double DMin = 0.35 * H;
	auto SolveD = [&T, &Back, DMin](double RefR)
	{
		const double B = FVector::DotProduct(T, Back);
		const double Disc = B * B - (T.SizeSquared() - RefR * RefR);
		const double Dv = Disc >= 0.0 ? -B + FMath::Sqrt(Disc) : DMin;
		return FMath::Clamp(FMath::IsFinite(Dv) ? Dv : DMin, DMin, 1.0e5);
	};
	// the smoothed ground under the eye (two fixed-point passes: the eye's direction barely depends on it)
	const double RhoE = FMath::Clamp(Tune.GroundAvgEye * H, 0.0, 30.0);
	double D = H / FMath::Max(FMath::Cos(Tilt), 0.2);
	FVector Eye = T + Back * D;
	for (int32 It = 0; It < 2; ++It)
	{
		D = SolveD(GAvg(Eye.GetSafeNormal(), RhoE) + H);
		Eye = T + Back * D;
	}
	// the smoothed reference also rises over RENDERED relief under the eye (one sample: the temporal smoothing
	// glides over it), so the hard floor below rarely has to act
	Follow(EyeGroundS, FMath::Max(GAvg(Eye.GetSafeNormal(), RhoE), GF(Eye.GetSafeNormal()) - 0.5 * Tune.NearHeightKm));
	bSnapGround = false;
	D = SolveD(EyeGroundS + H);
	Eye = T + Back * D;

	// never closer than MinClear to the ground under the eye, nor half of it to the ground around it
	{
		const FVector Ed = Eye.GetSafeNormal();
		double MinR = GF(Ed) + Tune.MinClearKm;
		FVector E, N;
		DmCoords::Basis(Ed, E, N);
		const double Off = Tune.MinClearKm;
		for (int32 K = 0; K < 4; ++K)
		{
			const double A = K * UE_HALF_PI;
			const FVector S = (Ed * Eye.Size() + (E * FMath::Cos(A) + N * FMath::Sin(A)) * Off).GetSafeNormal();
			MinR = FMath::Max(MinR, GF(S) + 0.5 * Tune.MinClearKm);
		}
		if (Eye.Size() < MinR) Eye = Ed * MinR;
	}

	FVector Forward = (T - Eye).GetSafeNormal();
	if (Forward.IsNearlyZero()) Forward = -Focus;
	FVector Up = Fwd * FMath::Cos(Tilt) + Focus * FMath::Sin(Tilt);
	Up = (Up - Forward * FVector::DotProduct(Up, Forward)).GetSafeNormal();
	if (Up.IsNearlyZero()) Up = Fwd;

	// shake: small view jitter (no eye movement, so it can never put the eye in the ground)
	const double A = ShakeAmp * FMath::Clamp(Tune.ShakeScale, 0.0, 1.0);
	if (A > 1e-4)
	{
		const double Amp = FMath::DegreesToRadians(Tune.ShakeMaxDeg) * A;
		const float Tm = float(ShakeTime);
		const double Yaw = Amp * FMath::PerlinNoise1D(Tm * 13.1f);
		const double Pitch = Amp * FMath::PerlinNoise1D(Tm * 11.7f + 31.3f);
		const double Roll = 0.5 * Amp * FMath::PerlinNoise1D(Tm * 9.3f + 71.9f);
		const FVector R0 = FRotationMatrix::MakeFromXZ(Forward, Up).GetUnitAxis(EAxis::Y);
		const FQuat Q = FQuat(Up, Yaw) * FQuat(R0, Pitch) * FQuat(Forward, Roll);
		Forward = Q.RotateVector(Forward).GetSafeNormal();
		Up = Q.RotateVector(Up).GetSafeNormal();
	}

	V.Eye = Eye;
	V.Target = T;
	V.Forward = Forward;
	V.Up = Up;
	V.Right = FRotationMatrix::MakeFromXZ(Forward, Up).GetUnitAxis(EAxis::Y);
	V.FocusDir = Focus;
	V.VFov = VFov;
	V.Aspect = Aspect;
	V.HFovDeg = FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(VFov * 0.5) * Aspect));
	V.Zoom01 = Zoom;
	V.TiltRad = Tilt;
	V.TargetDistKm = FVector::Dist(Eye, T);
	const double EyeR = Eye.Size();
	V.AltitudeKm = EyeR - GF(Eye / FMath::Max(EyeR, 1e-9));
	V.NearKm = FMath::Clamp(0.05 * V.AltitudeKm, 0.25, 150.0);
	V.FarKm = FMath::Sqrt(FMath::Max(EyeR * EyeR - 93.0 * 93.0, 0.0)) + 75.0;
}

// ------------------------------------------------------------------ projection

void FDmCameraRig::ScreenRay(const FVector2D& Px, FVector& OutOrigin, FVector& OutDir) const
{
	const double X = 2.0 * Px.X / FMath::Max(VpW, 1.0) - 1.0;
	const double Y = 1.0 - 2.0 * Px.Y / FMath::Max(VpH, 1.0);
	const double TY = FMath::Tan(V.VFov * 0.5);
	const double TX = TY * V.Aspect;
	OutOrigin = V.Eye;
	OutDir = (V.Forward + V.Right * (X * TX) + V.Up * (Y * TY)).GetSafeNormal();
}

bool FDmCameraRig::Project(const FVector& P, FVector2D& OutPx) const
{
	const FVector Rel = P - V.Eye;
	const double Z = FVector::DotProduct(Rel, V.Forward);
	if (Z <= 1e-9) return false;
	const double TY = FMath::Tan(V.VFov * 0.5);
	const double TX = TY * V.Aspect;
	const double X = FVector::DotProduct(Rel, V.Right) / Z / TX;
	const double Y = FVector::DotProduct(Rel, V.Up) / Z / TY;
	OutPx = FVector2D((X + 1.0) * 0.5 * VpW, (1.0 - Y) * 0.5 * VpH);
	return true;
}

bool FDmCameraRig::IsFinite() const
{
	return Finite(V.Eye) && Finite(V.Target) && Finite(V.Forward) && Finite(V.Up) && Finite(V.Right) && Finite(Focus) && Finite(Fwd)
		&& FMath::IsFinite(V.AltitudeKm) && FMath::IsFinite(Zoom) && FMath::IsFinite(V.HFovDeg);
}
