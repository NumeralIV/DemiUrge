// DEMIURGE — Game/FX/DmFx.cpp : the effects actor (see DmFx.h). Owner: C6 fx.

#include "Game/FX/DmFx.h"
#include "Game/FX/DmFxEffects.h"

#include "Game/Core/DmCoords.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmScene.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Game/Sky/DmSkyActor.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

#include <algorithm>

#include "Sim/Civ.h"
#include "Sim/State.h"
#include "Sim/Units.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmFx, Log, All);

using namespace DmFx;

namespace
{
	constexpr int32 kLightPool = 8;
	constexpr double kKm = 1.0e5;
	constexpr int32 kMaxFireCells = 160;
	// lit smoke considers only the strongest FX lights (verifier fix: every burning cell asks for a light, and a
	// 300-cell wildfire made LightAt loop over ~180 lights for each of 6,000 particles, ~4 ms a frame)
	constexpr int32 kSmokeLights = 24;
	constexpr float kMotePx = 3.5f;   // largest on-screen half size of a mote (wave 4b)
	// wave 4b retry: smallest on-screen half size of any particle. Sparks and embers 10-50 m across are a fraction
	// of a pixel at strategy distances: only the quads that happened to cover a pixel centre were drawn, so a burning
	// forest or a meteor's ejecta showed as a sparse field of random hot pixels ("golden grain",
	// shots/ue-fx-meteor-4b-f300). Below this size a particle is drawn at this size with its light and opacity
	// scaled down by the area it gained (energy kept): a soft faint glow instead of aliased grain.
	constexpr float kMinPx = 0.9f;
	float GSunScale = 1.f;      // -fxsun: sunlit smoke brightness trim
	float GLightScale = 1.f;    // -fxlight: point-light intensity trim
}

ADmFx::ADmFx()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;   // after the camera update: billboards face this frame's camera
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FxMesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetUsingAbsoluteLocation(true);
	Mesh->SetUsingAbsoluteRotation(true);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->bAffectDistanceFieldLighting = false;
	Mesh->SetAffectDynamicIndirectLighting(false);
	Mesh->bReceivesDecals = false;
	Mesh->SetTranslucentSortPriority(100);
	Mesh->bUseAsyncCooking = false;

	OverlayMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FxOverlayMesh"));
	OverlayMesh->SetupAttachment(RootComponent);
	OverlayMesh->SetUsingAbsoluteLocation(true);
	OverlayMesh->SetUsingAbsoluteRotation(true);
	OverlayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OverlayMesh->SetCastShadow(false);
	OverlayMesh->bAffectDistanceFieldLighting = false;
	OverlayMesh->SetAffectDynamicIndirectLighting(false);
	OverlayMesh->bReceivesDecals = false;
	OverlayMesh->SetTranslucentSortPriority(101);

	for (int32 I = 0; I < kLightPool; ++I)
	{
		UPointLightComponent* L = CreateDefaultSubobject<UPointLightComponent>(*FString::Printf(TEXT("FxLight%d"), I));
		L->SetupAttachment(RootComponent);
		L->SetUsingAbsoluteLocation(true);
		L->SetMobility(EComponentMobility::Movable);
		L->SetCastShadows(false);
		L->bUseInverseSquaredFalloff = false;
		L->LightFalloffExponent = 2.f;
		L->SetIntensityUnits(ELightUnits::Unitless);
		L->SetSourceRadius(0.f);
		L->bAffectTranslucentLighting = false;
		L->SetVisibility(false);
		LightPool.Add(L);
	}
}

ADmFx* ADmFx::Find(const UWorld* World)
{
	if (!World) return nullptr;
	for (TActorIterator<ADmFx> It(const_cast<UWorld*>(World)); It; ++It) return *It;
	return nullptr;
}

const dm::World* ADmFx::W() const { return SimSub ? SimSub->Sim() : nullptr; }

void ADmFx::BeginPlay()
{
	Super::BeginPlay();
	SimSub = UDmSimSubsystem::Get(this);
	double V = 1.0;
	if (FDmShotArgs::Value(TEXT("-fxgain="), V)) Gain = float(V);
	V = 1.0; if (FDmShotArgs::Value(TEXT("-fxsun="), V)) GSunScale = float(V);
	V = 1.0; if (FDmShotArgs::Value(TEXT("-fxlight="), V)) GLightScale = float(V);
	int32 Q = 2;
	if (FDmShotArgs::Value(TEXT("-fxquality="), Q)) Quality = FMath::Clamp(Q, 0, 2);
	MaxParticles = Quality == 0 ? 2000 : Quality == 1 ? 4000 : 6000;
	Renderer.MaxQuads = Quality == 0 ? 5000 : Quality == 1 ? 9000 : 14000;
	bStats = FDmShotArgs::Param(TEXT("fxstats"));
	bTest = FDmShotArgs::Param(TEXT("fxtest"));
	bRingOverlay = FDmShotArgs::Param(TEXT("fxringoverlay"));
	Particles.Reserve(MaxParticles);

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/FX/M_Fx.M_Fx"));
	if (!Base) UE_LOG(LogDmFx, Error, TEXT("DmFx: /Game/FX/M_Fx missing (run Scripts/fx.py); effects will not draw"));
	if (Base)
	{
		Mid = UMaterialInstanceDynamic::Create(Base, this);
		Mid->SetScalarParameterValue(TEXT("Gain"), Gain);
	}
	Renderer.Init(Mesh, Mid ? static_cast<UMaterialInterface*>(Mid) : Base);
	UMaterialInterface* Ov = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/FX/M_FxOverlay.M_FxOverlay"));
	if (Ov)
	{
		OverlayMid = UMaterialInstanceDynamic::Create(Ov, this);
		OverlayMid->SetScalarParameterValue(TEXT("Gain"), Gain);
	}
	Overlay.MaxQuads = 256;
	Overlay.Init(OverlayMesh, OverlayMid ? static_cast<UMaterialInterface*>(OverlayMid) : Ov);
	if (SimSub)
	{
		EventHandle = SimSub->OnSimEvent.AddUObject(this, &ADmFx::OnSimEvent);
		WorldHandle = SimSub->OnWorldChanged.AddUObject(this, &ADmFx::OnWorldChanged);
	}
	UE_LOG(LogDmFx, Display, TEXT("DmFx: ready (quality %d, %d particles, %d quads, gain %.2f, material %s)"), Quality, MaxParticles, Renderer.MaxQuads, Gain, Base ? TEXT("ok") : TEXT("MISSING"));
}

void ADmFx::EndPlay(const EEndPlayReason::Type Reason)
{
	if (SimSub)
	{
		SimSub->OnSimEvent.Remove(EventHandle);
		SimSub->OnWorldChanged.Remove(WorldHandle);
	}
	Effects.Reset();
	Particles.Reset();
	Super::EndPlay(Reason);
}

void ADmFx::OnWorldChanged(const dm::World*)
{
	Effects.Reset();
	Particles.Reset();
	Tracked.Reset();
	FireCells.Reset();
	FireHeat.Reset();
	FireScanAt = -1;
	BeaconCell = -1;
	EmitAcc.Reset();
}

void ADmFx::SetQuiet(bool bInQuiet)
{
	bQuiet = bInQuiet;
	if (bQuiet) { Particles.Reset(); Effects.Reset(); }
}

void ADmFx::SetAurora(float Strength, float Seconds)
{
	AuroraTarget = FMath::Max(AuroraTarget, FMath::Clamp(Strength, 0.f, 1.f));
	AuroraHold = FMath::Max(AuroraHold, Seconds);
	// the sky (C2) owns the aurora and exposes BoostAurora for exactly this ("the FX aurora effect"): a surge
	// brightens its rayed ovals instead of adding a second, differently placed set of curtains (verifier fix:
	// both drew at once, a smooth neon tube beside the sky's rays). Our own curtains remain the fallback when
	// a scene has no sky.
	if (ADmSky* Sky = ADmSky::Get(this)) Sky->BoostAurora(FMath::Clamp(Strength, 0.f, 1.f), FMath::Max(Seconds, 1.f) + 20.f);
}

DmFx::FParticle* ADmFx::Emit(const FParticle& P)
{
	if (bQuiet || Particles.Num() >= MaxParticles) return nullptr;
	FParticle& N = Particles.Add_GetRef(P);
	if (N.Seed == 0) N.Seed = Rng.Byte();
	return &N;
}

void ADmFx::AddEffect(FDmFxEffect* E)
{
	if (!E) return;
	if (bQuiet) { delete E; return; }
	Effects.Add(TUniquePtr<FDmFxEffect>(E));
}

bool ADmFx::HasEffectFor(int32 DisasterId) const
{
	for (const TUniquePtr<FDmFxEffect>& E : Effects) if (E && E->DisasterId == DisasterId) return true;
	return false;
}

FDmFxEffect* ADmFx::FindEffect(const TCHAR* Kind) const
{
	for (int32 I = Effects.Num() - 1; I >= 0; --I)
		if (Effects[I] && Effects[I]->Kind && FCString::Strcmp(Effects[I]->Kind, Kind) == 0) return Effects[I].Get();
	return nullptr;
}

// ------------------------------------------------------------------ geometry helpers

void ADmFx::TangentBasis(const FVector& Up, FVector& East, FVector& North) { DmCoords::Basis(Up, East, North); }

FVector ADmFx::CellDir(int32 Cell) const
{
	const dm::World* Wd = W();
	return Wd ? DmCoords::CellDir(*Wd, Cell) : FVector::UpVector;
}

FVector ADmFx::CellSurface(int32 Cell, double Lift) const
{
	const dm::World* Wd = W();
	if (!Wd || Cell < 0 || Cell >= Wd->n) return FVector::ZeroVector;
	const FVector D = DmCoords::CellDir(*Wd, Cell);
	return D * (DmCoords::HeightToRadiusUU(DmCoords::SurfaceHeight(*Wd, Cell)) + Lift);
}

FVector ADmFx::CellGround(int32 Cell, double Lift) const
{
	const dm::World* Wd = W();
	if (!Wd || Cell < 0 || Cell >= Wd->n) return FVector::ZeroVector;
	const FVector D = DmCoords::CellDir(*Wd, Cell);
	return D * (DmCoords::HeightToRadiusUU(Wd->elev[size_t(Cell)]) + Lift);
}

bool ADmFx::IsSeaCell(int32 Cell) const
{
	const dm::World* Wd = W();
	return Wd && Cell >= 0 && Cell < Wd->n && dm::state::isSea(*Wd, Cell);
}

int32 ADmFx::CellAt(const FVector& Dir) const
{
	const dm::World* Wd = W();
	if (!Wd || !Wd->geo) return -1;
	const dm::Vec3 S = DmCoords::UEToSim(Dir.GetSafeNormal());
	return Wd->geo->cellAt(S.x, S.y, S.z);
}

bool ADmFx::IsLandAt(const FVector& Dir) const
{
	const dm::World* Wd = W();
	if (!Wd || !Wd->geo) return false;
	const dm::Vec3 S = DmCoords::UEToSim(Dir.GetSafeNormal());
	const int32 C = Wd->geo->cellAt(S.x, S.y, S.z);
	if (C < 0 || C >= Wd->n) return false;
	const double D[3] = { S.x, S.y, S.z };
	auto Triple = [&D](const float* Av, const float* Bv)
	{
		return D[0] * (double(Av[1]) * Bv[2] - double(Av[2]) * Bv[1]) + D[1] * (double(Av[2]) * Bv[0] - double(Av[0]) * Bv[2]) + D[2] * (double(Av[0]) * Bv[1] - double(Av[1]) * Bv[0]);
	};
	auto H = [Wd](int32 K) { return double(Wd->elev[size_t(K)]) - Wd->seaLevel; };
	const float* PC = &Wd->geo->pos[size_t(3 * C)];
	const int32 Deg = Wd->geo->nbrN[size_t(C)];
	for (int32 K = 0; K < Deg; ++K)
	{
		const int32 A = Wd->geo->nbr[size_t(6 * C + K)];
		const int32 B = Wd->geo->nbr[size_t(6 * C + (K + 1) % Deg)];
		const float* PA = &Wd->geo->pos[size_t(3 * A)];
		const float* PB = &Wd->geo->pos[size_t(3 * B)];
		const double WC = Triple(PA, PB), WA = Triple(PB, PC), WB = Triple(PC, PA);
		const bool bPos = WC >= 0 && WA >= 0 && WB >= 0, bNeg = WC <= 0 && WA <= 0 && WB <= 0;
		if (!bPos && !bNeg) continue;
		const double Sum = WC + WA + WB;
		if (FMath::Abs(Sum) < 1e-300) break;
		return (WC * H(C) + WA * H(A) + WB * H(B)) / Sum > 0.0;
	}
	return H(C) > 0.0;
}

double ADmFx::SurfaceR(const FVector& Dir) const
{
	if (!SimSub || !W()) return DmCoords::RadiusUU;
	int32 Hint = -1;
	const double R = SimSub->SurfaceRadiusUU(Dir.GetSafeNormal(), Hint);
	return R > 0.0 ? R : DmCoords::RadiusUU;
}

FVector ADmFx::Offset(const FVector& CentreDir, double Ang, double Az) const
{
	const FVector C = CentreDir.GetSafeNormal();
	FVector E, N;
	TangentBasis(C, E, N);
	return (C * FMath::Cos(Ang) + (N * FMath::Cos(Az) + E * FMath::Sin(Az)) * FMath::Sin(Ang)).GetSafeNormal();
}

FVector ADmFx::SunPlanet() const { return SunP; }

float ADmFx::DayAt(const FVector& P) const
{
	const double Rr = P.Size();
	if (Rr < 1.0) return 1.f;
	const FVector Up = P / Rr;
	const double AltKm = FMath::Max(0.0, (Rr - DmCoords::RadiusUU) / kKm);
	const double Mu0 = -FMath::Sqrt(2.0 * AltKm / DmCoords::RadiusKm);
	const double Mu = FVector::DotProduct(Up, SunP);
	return Smooth01(float((Mu - (Mu0 - 0.04)) / 0.16));
}

FLinearColor ADmFx::LightAt(const FVector& P) const
{
	const double Rr = FMath::Max(P.Size(), 1.0);
	const FVector Up = P / Rr;
	const double AltKm = FMath::Max(0.0, (Rr - DmCoords::RadiusUU) / kKm);
	const double Mu0 = -FMath::Sqrt(2.0 * AltKm / DmCoords::RadiusKm);
	const double Mu = FVector::DotProduct(Up, SunP);
	const float Day = Smooth01(float((Mu - (Mu0 - 0.04)) / 0.16));
	const float Warm = Smooth01(float((Mu - Mu0) * 4.0));
	const float KSun = 3.2f * GSunScale;
	// wave 4b: a softer sunset tint (1, 0.36, 0.14 turned dark ash salmon-pink at dusk)
	FLinearColor L = FMath::Lerp(FLinearColor(1.0f, 0.52f, 0.3f), FLinearColor(1.0f, 0.96f, 0.90f), Warm) * (Day * KSun);
	L += FLinearColor(0.35f, 0.50f, 0.85f) * (0.10f * KSun * FMath::Min(1.f, Day * 1.5f));
	// night floor (moon and stars): smoke stays faintly readable; exempt from the exposure compensation (the
	// moonlit ground it sits on is lifted by the same exposure)
	L += FLinearColor(0.030f, 0.036f, 0.055f) * (1.f / FMath::Max(ExposureComp, 0.05f));
	// FX light is mostly a night effect, like the point lights (ApplyLights): by day a vent's 14-23 units made
	// the whole ash column pink, a second sun (verifier fix: 0.25 by day .. 1 at night)
	const float FxDay = 0.1f + 0.9f * (1.f - Day);   // wave 4b: 0.25 by day still turned grey smoke salmon-pink
	const int32 NL = FMath::Min(LightReqs.Num(), kSmokeLights);
	for (int32 I = 0; I < NL; ++I)
	{
		const FDmFxLightReq& R = LightReqs[I];
		const double Reach = double(R.Radius) * 2.5;
		const double D2 = FVector::DistSquared(P, R.P);
		if (D2 > Reach * Reach) continue;
		const float X = float(FMath::Sqrt(D2) / FMath::Max(1.0, double(R.Radius)));
		// FX light on smoke, in units of the point lights' intensity: 0.12 keeps a vent's glow an underlight,
		// not a second sun (lit smoke is albedo x light; the sun is 3.2)
		const float F = 0.12f * R.Intensity / (1.f + X * X * 3.f) * FMath::Clamp(2.5f - X, 0.f, 1.f) * FxDay;
		L += R.Col * F;
	}
	L.A = 1.f;
	return L;
}

FVector ADmFx::WindAt(const FVector& P) const
{
	const double Rr = FMath::Max(P.Size(), 1.0);
	const FVector Up = P / Rr;
	FVector E, N;
	TangentBasis(Up, E, N);
	const double Lat = FMath::Asin(FMath::Clamp(Up.Z, -1.0, 1.0));
	const double AltKm = FMath::Max(0.0, (Rr - DmCoords::RadiusUU) / kKm);
	const double Zonal = -FMath::Cos(3.0 * Lat);   // easterly trades, westerlies, polar easterlies
	const double Speed = 0.15 * kKm * (1.0 + 0.45 * AltKm);   // visual drift (the world runs fast): 0.15 km/s at the ground
	return (E * Zonal + N * 0.15 * FMath::Sin(6.0 * Lat)) * Speed;
}

float ADmFx::UUPerPixel(const FVector& P) const
{
	return 1.f / FMath::Max(1e-9f, Renderer.View().PxPerUU(FVector::Dist(P, CamP)));
}

// ------------------------------------------------------------------ quads

void ADmFx::Submit(FQuad& Q, bool bLit, const FVector& PlanetC)
{
	if (bQuiet || !VisibleFromCamera(PlanetC)) return;   // the far side of the planet (the shader's occlusion is lenient)
	if (bLit)
	{
		const FLinearColor L = LightAt(PlanetC);
		Q.Col.R *= L.R * Q.Col.A; Q.Col.G *= L.G * Q.Col.A; Q.Col.B *= L.B * Q.Col.A;   // premultiplied
		const float Day = DayAt(PlanetC);
		const FVector UpW = P2W.TransformVectorNoScale(PlanetC.GetSafeNormal());
		const FVector Corner[4] = { -Q.U - Q.V, Q.U - Q.V, Q.U + Q.V, -Q.U + Q.V };
		for (int32 K = 0; K < 4; ++K)
		{
			const FVector D = Corner[K].GetSafeNormal();
			Q.Light[K] = 0.5f * (0.80f + 0.42f * Day * float(FVector::DotProduct(D, SunW)) + 0.12f * float(FVector::DotProduct(D, UpW)));
		}
	}
	Renderer.Add(Q);
}

void ADmFx::AddPlanetQuad(const FVector& C, const FVector& U, const FVector& V, EShape Shape, const FLinearColor& Col, bool bLit, uint8 Seed, uint8 Param, float Soft, float Age, float U0, float U1)
{
	FQuad Q;
	Q.C = P2W.TransformPosition(C);
	Q.U = P2W.TransformVectorNoScale(U);
	Q.V = P2W.TransformVectorNoScale(V);
	Q.Col = Col;
	Q.Shape = Shape;
	Q.Seed = Seed;
	Q.Param = Param;
	Q.Soft = Soft;
	Q.Age = Age;
	Q.U0x[0] = U0; Q.U0x[1] = U1;
	Submit(Q, bLit, C);
}

void ADmFx::AddFacing(const FVector& P, float Half, EShape Shape, const FLinearColor& Col, bool bLit, uint8 Seed, uint8 Param, float Rot, float Age)
{
	const FDmFxView& Vw = Renderer.View();
	const float Cs = FMath::Cos(Rot), Sn = FMath::Sin(Rot);
	FQuad Q;
	Q.C = P2W.TransformPosition(P);
	Q.U = (Vw.Right * Cs + Vw.Up * Sn) * Half;
	Q.V = (Vw.Up * Cs - Vw.Right * Sn) * Half;
	Q.Col = Col; Q.Shape = Shape; Q.Seed = Seed; Q.Param = Param; Q.Age = Age;
	Submit(Q, bLit, P);
}

bool ADmFx::VisibleFromCamera(const FVector& P) const
{
	// hidden when the segment camera -> P passes inside the planet (radius a little under the datum)
	const double Rs = DmCoords::RadiusUU - 1.0 * kKm;
	const FVector D = P - CamP;
	const double L2 = D.SizeSquared();
	if (L2 < 1.0) return true;
	const double Tc = FMath::Clamp(-FVector::DotProduct(CamP, D) / L2, 0.0, 1.0);
	return (CamP + D * Tc).Size() > Rs || Tc >= 1.0;
}

void ADmFx::AddFlash(const FVector& P, float Half, const FLinearColor& Col)
{
	if (bQuiet || !VisibleFromCamera(P)) return;
	const FDmFxView& Vw = Overlay.View();
	FQuad Q;
	Q.C = P2W.TransformPosition(P);
	Q.U = Vw.Right * Half;
	Q.V = Vw.Up * Half;
	Q.Col = FLinearColor(Col.R, Col.G, Col.B, 0.f);
	Q.Shape = EShape::Glow;
	Overlay.Add(Q);
}

void ADmFx::AddAxis(const FVector& P0, const FVector& P1, float HalfWidth, EShape Shape, const FLinearColor& Col, bool bLit, uint8 Seed, uint8 Param, float Age)
{
	const FVector C = (P0 + P1) * 0.5;
	FQuad Q;
	Q.C = P2W.TransformPosition(C);
	Q.V = P2W.TransformVectorNoScale((P1 - P0) * 0.5);
	const FVector ToCam = (Renderer.View().CamPos - Q.C).GetSafeNormal();
	FVector Side = FVector::CrossProduct(Q.V, ToCam).GetSafeNormal();
	if (Side.IsNearlyZero()) Side = Renderer.View().Right;
	Q.U = Side * HalfWidth;
	Q.Col = Col; Q.Shape = Shape; Q.Seed = Seed; Q.Param = Param; Q.Age = Age;
	Submit(Q, bLit, C);
}

void ADmFx::AddFlat(const FVector& P, float HalfU, float HalfV, float Rot, EShape Shape, const FLinearColor& Col, bool bLit, uint8 Seed, uint8 Param, float Soft, float Age)
{
	FVector E, N;
	TangentBasis(P.GetSafeNormal(), E, N);
	const float Cs = FMath::Cos(Rot), Sn = FMath::Sin(Rot);
	AddPlanetQuad(P, (E * Cs + N * Sn) * HalfU, (N * Cs - E * Sn) * HalfV, Shape, Col, bLit, Seed, Param, Soft, Age);
}

void ADmFx::AddUpright(const FVector& Base, float HalfW, float HalfH, EShape Shape, const FLinearColor& Col, bool bLit, uint8 Seed, uint8 Param, float Age)
{
	const FVector Up = Base.GetSafeNormal();
	const FVector C = Base + Up * HalfH;
	FQuad Q;
	Q.C = P2W.TransformPosition(C);
	const FVector UpW = P2W.TransformVectorNoScale(Up);
	Q.V = UpW * HalfH;
	const FVector ToCam = (Renderer.View().CamPos - Q.C).GetSafeNormal();
	FVector Side = FVector::CrossProduct(UpW, ToCam).GetSafeNormal();
	if (Side.IsNearlyZero()) Side = Renderer.View().Right;
	Q.U = Side * HalfW;
	Q.Col = Col; Q.Shape = Shape; Q.Seed = Seed; Q.Param = Param; Q.Age = Age;
	Submit(Q, bLit, C);
}

void ADmFx::AddRing(const FVector& Centre, double Ang, double Height, float HalfWidth, EShape Shape, const FLinearColor& Col,
	bool bLit, int32 Segments, int32 Mask, uint8 Param, float Soft, uint8 Seed)
{
	if (Ang <= 1e-6 || Segments < 3) return;
	const FVector C = Centre.GetSafeNormal();
	const dm::World* Wd = W();
	const double Step = 2.0 * PI / Segments;
	for (int32 I = 0; I < Segments; ++I)
	{
		const double Az = (I + 0.5) * Step;
		const FVector D = Offset(C, Ang, Az);
		if (Mask != 0 && Wd)
		{
			const int32 Cl = CellAt(D);
			const bool bSea = Cl >= 0 && dm::state::isSea(*Wd, Cl);
			if ((Mask == 1 && !bSea) || (Mask == 2 && bSea)) continue;
		}
		const double Rr = SurfaceR(D) + Height;
		const FVector P = D * Rr;
		// tangent along the ring and radial (away from the centre) directions at P
		const FVector Ahead = Offset(C, Ang, Az + Step * 0.5) * Rr;
		const FVector Behind = Offset(C, Ang, Az - Step * 0.5) * Rr;
		// overlap so the Bolt shape's end ramps (the last 1/6 of each half) coincide and sum to a constant: with
		// 1.18 the overlaps doubled up and every ring showed bright ticks at its segment joins (verifier fix)
		const FVector Along = (Ahead - Behind) * 0.5 * (12.0 / 11.0);
		FVector Radial = FVector::CrossProduct(Along, D).GetSafeNormal();
		if (FVector::DotProduct(Radial, D - C) < 0) Radial = -Radial;
		if (bRingOverlay)
		{
			FQuad Q;
			Q.C = P2W.TransformPosition(P); Q.U = P2W.TransformVectorNoScale(Radial * HalfWidth); Q.V = P2W.TransformVectorNoScale(Along);
			Q.Col = FLinearColor(0.f, 3.f, 6.f, 0.f); Q.Shape = EShape::Bolt;
			Overlay.Add(Q);
			continue;
		}
		AddPlanetQuad(P, Radial * HalfWidth, Along, Shape, Col, bLit, uint8(Seed + I * 37), Param, Soft);
	}
}

void ADmFx::AddLight(const FVector& P, const FLinearColor& Col, float Intensity, float Radius)
{
	if (bQuiet || !(Intensity > 0.f) || !(Radius > 0.f)) return;
	FDmFxLightReq R;
	R.P = P; R.Col = Col; R.Intensity = Intensity; R.Radius = Radius;
	LightReqs.Add(R);
}

void ADmFx::Shake(float Amount, const FVector& P, double RangeUU)
{
	const double D = CamDist(P);
	const float A = Amount * float(FMath::Clamp(1.0 - D / FMath::Max(RangeUU, 1.0), 0.0, 1.0));
	if (A > 0.01f) PendingShake.Add(FMath::Min(A, 1.f));
}

// ------------------------------------------------------------------ frame

void ADmFx::UpdateView()
{
	FDmFxView V;
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	float FovDeg = 30.f;
	if (PC && PC->PlayerCameraManager)
	{
		V.CamPos = PC->PlayerCameraManager->GetCameraLocation();
		const FRotator Rot = PC->PlayerCameraManager->GetCameraRotation();
		const FRotationMatrix M(Rot);
		V.Fwd = M.GetUnitAxis(EAxis::X);
		V.Right = M.GetUnitAxis(EAxis::Y);
		V.Up = M.GetUnitAxis(EAxis::Z);
		FovDeg = PC->PlayerCameraManager->GetFOVAngle();
		int32 SX = 0, SY = 0;
		PC->GetViewportSize(SX, SY);
		if (SX > 0) V.ScreenW = float(SX);
	}
	V.TanHalfFov = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(FovDeg, 1.f, 170.f) * 0.5f));
	Renderer.Begin(V);
	Overlay.Begin(V);
	CamP = P2W.InverseTransformPosition(V.CamPos);
	CamRightP = P2W.InverseTransformVectorNoScale(V.Right);
	CamUpP = P2W.InverseTransformVectorNoScale(V.Up);
}

void ADmFx::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double T0 = FPlatformTime::Seconds();
	const float Dt = FMath::Clamp(DeltaSeconds, 0.f, 0.1f);
	Clock += Dt;
	if (!SimSub) SimSub = UDmSimSubsystem::Get(this);
	const dm::World* Wd = W();
	if (Wd && Wd->n != LastWorldN) { LastWorldN = Wd->n; OnWorldChanged(Wd); }

	if (SimSub)
	{
		P2W = SimSub->GetPlanetToWorld();
		SunW = SimSub->GetSunDirection().GetSafeNormal();
		SunP = P2W.InverseTransformVectorNoScale(SunW).GetSafeNormal();
	}
	UpdateView();
	UpdateExposure();
	LightReqs.Reset();
	bLightsSorted = false;

	if (!bQuiet && Wd)
	{
		ScanDisasters();
		for (int32 I = 0; I < Effects.Num();)
		{
			FDmFxEffect* E = Effects[I].Get();
			E->T += Dt;
			if (!E->Tick(*this, Dt)) { Effects.RemoveAtSwap(I); continue; }
			++I;
		}
		UpdateAmbient(Dt);
		if (bTest) DrawTest();
		PrepareLights();   // strongest first: lit particles use the first kSmokeLights, the pool the first 8
		SimulateParticles(Dt);
		DrawParticles();
	}
	ApplyLights();
	Renderer.End();
	Overlay.End();

	if (PendingShake.Num() > 0)
	{
		float A = 0.f;
		for (float S : PendingShake) A = FMath::Max(A, S);
		PendingShake.Reset();
		if (dm::World* MW = SimSub ? SimSub->SimMutable() : nullptr)
		{
			MW->bus.emit(dm::EvCameraShake{ double(A) });
			UE_LOG(LogDmFx, Verbose, TEXT("DmFx: camera:shake %.2f"), A);
		}
	}

	if (bStats && Clock - StatsAt > 2.0)
	{
		StatsAt = Clock;
		UE_LOG(LogDmFx, Display, TEXT("DmFxStats effects %d particles %d quads %d lights %d exposure comp %.2f | tick %.2f ms (render build %.2f ms)"),
			Effects.Num(), Particles.Num(), Renderer.LastDrawn, LightReqs.Num(), ExposureComp, (FPlatformTime::Seconds() - T0) * 1000.0, Renderer.LastMs);
	}
}

void ADmFx::UpdateExposure()
{
	// the sky's exposure lift above the base exposure (-ev=): night side and molten heat (see GetExposureComp)
	float Lift = 0.f;
	if (const ADmSky* Sky = ADmSky::Get(this))
		if (Sky->Post) Lift = Sky->Post->Settings.AutoExposureBias - float(FDmShotArgs::Get().Ev);
	Lift = FMath::Clamp(Lift, 0.f, 6.f);
	ExposureComp = FMath::Pow(2.f, -0.7f * Lift);
	LightComp = FMath::Pow(2.f, -0.5f * Lift);
	const float G = Gain * ExposureComp;
	if (FMath::Abs(G - GainSet) > 0.004f * FMath::Max(G, 0.01f))
	{
		GainSet = G;
		if (Mid) Mid->SetScalarParameterValue(TEXT("Gain"), G);
		if (OverlayMid) OverlayMid->SetScalarParameterValue(TEXT("Gain"), G);
	}
}

void ADmFx::PrepareLights()
{
	if (bLightsSorted) return;
	bLightsSorted = true;
	// strongest requests first (weight: intensity x radius, nearer the camera first): they win the point-light
	// pool and are the ones lit smoke considers
	LightReqs.Sort([this](const FDmFxLightReq& A, const FDmFxLightReq& B)
	{
		const double WA = A.Intensity * A.Radius / (1.0 + CamDist(A.P) / (A.Radius * 4.0));
		const double WB = B.Intensity * B.Radius / (1.0 + CamDist(B.P) / (B.Radius * 4.0));
		return WA > WB;
	});
}

void ADmFx::ApplyLights()
{
	PrepareLights();
	for (int32 I = 0; I < LightPool.Num(); ++I)
	{
		UPointLightComponent* L = LightPool[I];
		if (!L) continue;
		if (I < LightReqs.Num() && !bQuiet)
		{
			const FDmFxLightReq& R = LightReqs[I];
			L->SetWorldLocation(P2W.TransformPosition(R.P));
			L->SetLightColor(FLinearColor(R.Col.R, R.Col.G, R.Col.B, 1.f), false);
			// mostly a night effect: by day the sun dominates, and strong point lights on the planet's land
			// material showed as a grainy speckle (Lumen / material noise), so they are scaled down by day
			// wave 4b: 0.06 by day (was 0.2): a lightning strike's or a wildfire's light still showed as a disc of golden
			// speckle on the sunlit land (fx-lightning-4b); by day the effect's own glow carries it
			L->SetIntensity(R.Intensity * GLightScale * LightComp * (0.06f + 0.94f * (1.f - DayAt(R.P))));
			L->SetAttenuationRadius(R.Radius * 2.5f);
			if (!L->IsVisible()) L->SetVisibility(true);
		}
		else if (L->IsVisible()) L->SetVisibility(false);
	}
}

void ADmFx::DrawTest()
{
	// -fxtest: one quad of every shape in a row in front of the camera (shader debugging)
	const FVector Fwd = P2W.InverseTransformVectorNoScale(Renderer.View().Fwd);
	const double D = 20.0 * kKm;
	const float Half = float(D * 0.05);
	for (int32 K = 0; K < 12; ++K)
	{
		const FVector P = CamP + Fwd * D + CamRightP * (double(K - 5.5) * Half * 2.2) + CamUpP * (Half * 1.2);
		const bool bLitShape = K == 1 || K == 10 || K == 11;
		FLinearColor C = bLitShape ? FLinearColor(0.6f, 0.6f, 0.6f, 0.9f) : FLinearColor(2.f, 1.2f, 0.6f, 0.f);
		AddFacing(P, Half, EShape(K), C, bLitShape, uint8(K * 17), 128, 0.5f, 0.3f);
		AddFacing(P - CamUpP * (Half * 2.4), Half, EShape(K), C, bLitShape, uint8(K * 17 + 5), 128, 0.f, 0.3f);
	}
	// rings around the point under the camera at 0.2 / 0.5 / 1 / 2 km above the sim surface (depth tested),
	// and one at 0.05 km through the overlay (no depth test)
	{
		const FVector Look = CamP.GetSafeNormal();
		const double Hs[4] = { 0.2, 0.5, 1.0, 2.0 };
		for (int32 K = 0; K < 4; ++K)
		{
			AddRing(Look, 0.03 + 0.03 * K, Hs[K] * kKm, float(0.25 * kKm), EShape::Bolt, FLinearColor(6.f, 6.f - K * 1.5f, 1.f + K * 1.5f, 0.f), false, 128, 0, 60);
			// facing glows and flat discs at the same heights, side by side west of the centre
			FVector E, N;
			TangentBasis(Look, E, N);
			const FVector Df = (Look - E * 0.02 + N * (0.012 * (K - 1.5))).GetSafeNormal();
			AddFacing(Df * (SurfaceR(Df) + Hs[K] * kKm), float(0.4 * kKm), EShape::Glow, FLinearColor(0.f, 6.f, 0.f, 0.f));
			const FVector Dg = (Look - E * 0.04 + N * (0.012 * (K - 1.5))).GetSafeNormal();
			AddFlat(Dg * (SurfaceR(Dg) + Hs[K] * kKm), float(0.4 * kKm), float(0.4 * kKm), 0.f, EShape::Disc, FLinearColor(6.f, 0.f, 0.f, 0.f), false, 1, 0);
		}
		for (int32 I = 0; I < 128; ++I)
		{
			const FVector D0 = Offset(Look, 0.15, I * 6.2831853 / 128), D1 = Offset(Look, 0.15, (I + 1) * 6.2831853 / 128);
			FQuad Rq;
			Rq.C = P2W.TransformPosition((D0 * (SurfaceR(D0) + 0.05 * kKm) + D1 * (SurfaceR(D1) + 0.05 * kKm)) * 0.5);
			Rq.V = P2W.TransformVectorNoScale((D1 * SurfaceR(D1) - D0 * SurfaceR(D0)) * 0.6);
			Rq.U = P2W.TransformVectorNoScale(D0 * (0.2 * kKm));
			Rq.Col = FLinearColor(6.f, 6.f, 6.f, 0.f);
			Rq.Shape = EShape::Bolt;
			Overlay.Add(Rq);
		}
		static int32 Logged = 0;
		if (Logged++ == 100)
			for (int32 K = 0; K < 8; ++K)
			{
				const FVector Dk = Offset(Look, 0.09, K * 0.785);
				const int32 Cl = CellAt(Dk);
				const dm::World* Wd = W();
				UE_LOG(LogDmFx, Display, TEXT("DmFxTest: az %d surfaceR %.0f (above datum %.3f km) cell %d elev %.3f sea %d seaLevel %.3f"),
					K, SurfaceR(Dk), (SurfaceR(Dk) - DmCoords::RadiusUU) / kKm, Cl, Wd ? Wd->elev[size_t(Cl)] : 0.f, IsSeaCell(Cl) ? 1 : 0, Wd ? Wd->seaLevel : 0.0);
			}
	}
}
// ------------------------------------------------------------------ particles

void ADmFx::SimulateParticles(float Dt)
{
	for (int32 I = 0; I < Particles.Num();)
	{
		FParticle& P = Particles[I];
		P.Age += Dt;
		if (P.Age >= P.Life) { Particles.RemoveAtSwap(I, EAllowShrinking::No); continue; }
		const double Rr = FMath::Max(P.P.Size(), 1.0);
		const FVector Up = P.P / Rr;
		P.V += Up * (double(P.Rise) * Dt);
		if (P.Drag > 0.f)
		{
			const FVector Target = P.Wind > 0.f ? WindAt(P.P) * double(P.Wind) : FVector::ZeroVector;
			P.V = Target + (P.V - Target) * double(FMath::Exp(-P.Drag * Dt));
		}
		else if (P.Wind > 0.f) P.V += WindAt(P.P) * double(P.Wind) * double(Dt);
		P.P += P.V * double(Dt);
		P.Rot += P.Spin * Dt;
		if (P.bGround || P.Rise < -1.f)
		{
			// the rendered terrain carries extra relief above the sim surface: keep a margin
			const double Sr = SurfaceR(Up) + (P.bGround ? 0.15 * kKm : 0.0);
			const double Pr = P.P.Size();
			if (Pr < Sr)
			{
				if (P.bGround)
				{
					P.P = P.P.GetSafeNormal() * Sr;
					const FVector U2 = P.P.GetSafeNormal();
					const double In = FVector::DotProduct(P.V, U2);
					if (In < 0) P.V -= U2 * In;
				}
				else { Particles.RemoveAtSwap(I, EAllowShrinking::No); continue; }   // ballistic debris lands
			}
		}
		++I;
	}
}

void ADmFx::DrawParticles()
{
	const FDmFxView& Vw = Renderer.View();
	for (const FParticle& P : Particles)
	{
		if (!VisibleFromCamera(P.P)) continue;
		const float T = FMath::Clamp(P.Age / FMath::Max(P.Life, 1e-3f), 0.f, 1.f);
		float S = FMath::Lerp(P.Size0, P.Size1, FMath::Pow(T, P.SizePow));
		// wave 4b: motes are points of light (pollen, dust in a sunbeam, sparks of grace): at most kMotePx pixels
		// across however close the camera is (Bloom's 45 m motes filled a close view with giant stars)
		const float PxUU = FMath::Max(1e-9f, Vw.PxPerUU(FVector::Dist(P.P, CamP)));
		if (P.Shape == EShape::Mote) S = FMath::Min(S, kMotePx / PxUU);
		float Thin = 1.f;   // energy kept when a sub-pixel particle is drawn larger (kMinPx)
		if (S * PxUU < kMinPx && S > 0.f)
		{
			const float Grow = kMinPx / (S * PxUU);
			// a velocity-stretched streak keeps its length: only its width grows
			Thin = P.Orient == EOrient::Velocity ? 1.f / Grow : 1.f / (Grow * Grow);
			S *= Grow;
		}
		FLinearColor C = FMath::Lerp(P.C0, P.C1, FMath::Pow(T, P.ColorPow));
		C.A *= Thin;
		const float Fade = FMath::Clamp(T / FMath::Max(P.FadeIn, 1e-3f), 0.f, 1.f) * FMath::Clamp((1.f - T) / FMath::Max(P.FadeOut, 1e-3f), 0.f, 1.f);
		C.A *= Fade;
		float Flick = 1.f;
		if (P.Flicker > 0.f) Flick = FMath::Max(0.f, 1.f + P.Flicker * FMath::PerlinNoise1D(float(Clock) * 7.f + P.Seed * 0.61f) * 1.6f);

		FQuad Q;
		Q.C = P2W.TransformPosition(P.P);
		const FVector ToCam = (Vw.CamPos - Q.C).GetSafeNormal();
		const FVector UpW = P2W.TransformVectorNoScale(P.P.GetSafeNormal());
		switch (P.Orient)
		{
		case EOrient::Facing:
		{
			const float Cs = FMath::Cos(P.Rot), Sn = FMath::Sin(P.Rot);
			Q.U = (Vw.Right * Cs + Vw.Up * Sn) * S;
			Q.V = (Vw.Up * Cs - Vw.Right * Sn) * S;
			break;
		}
		case EOrient::Velocity:
		{
			const FVector Vw3 = P2W.TransformVectorNoScale(P.V);
			const double Sp = Vw3.Size();
			const FVector Dir = Sp > 1e-3 ? Vw3 / Sp : UpW;
			// (verifier fx4b-verify: Len uses the GROWN size, so a widened sub-pixel streak is also Grow x longer and
			// keeps Grow x its energy, not 1 x as Thin assumes. Tried keeping the pre-grow length: the embers became
			// sub-pixel-long dots and the meteor's burning forest turned into a carpet of golden grain
			// (ue-fx-meteor-main-v4b-f300). The longer, dimmer streak reads right; left as is.)
			const double Len = double(S) * P.Aspect + Sp * P.Stretch;
			FVector Side = FVector::CrossProduct(Dir, ToCam).GetSafeNormal();
			if (Side.IsNearlyZero()) Side = Vw.Right;
			Q.U = Dir * Len;
			Q.V = Side * S;
			Q.C -= Dir * (Len - S);   // the head sits on the particle
			break;
		}
		case EOrient::Flat:
		{
			FVector E, N;
			TangentBasis(UpW, E, N);
			const float Cs = FMath::Cos(P.Rot), Sn = FMath::Sin(P.Rot);
			Q.U = (E * Cs + N * Sn) * S * P.Aspect;
			Q.V = (N * Cs - E * Sn) * S;
			break;
		}
		case EOrient::Upright:
		{
			FVector Side = FVector::CrossProduct(UpW, ToCam).GetSafeNormal();
			if (Side.IsNearlyZero()) Side = Vw.Right;
			Q.U = Side * S;
			Q.V = UpW * S * P.Aspect;
			Q.C += Q.V;   // anchored at the base
			break;
		}
		case EOrient::Axis:
		{
			Q.V = P2W.TransformVectorNoScale(P.Axis);
			FVector Side = FVector::CrossProduct(Q.V, ToCam).GetSafeNormal();
			if (Side.IsNearlyZero()) Side = Vw.Right;
			Q.U = Side * S;
			break;
		}
		}

		Q.Shape = P.Shape;
		Q.Seed = P.Seed;
		Q.Param = P.Param;
		Q.Age = T;
		if (P.bLit)
		{
			// albedo x light, premultiplied by the opacity
			const FLinearColor L = LightAt(P.P);
			Q.Col = FLinearColor(C.R * L.R * C.A, C.G * L.G * C.A, C.B * L.B * C.A, C.A);
			const float Day = DayAt(P.P);
			const FVector Corner[4] = { -Q.U - Q.V, Q.U - Q.V, Q.U + Q.V, -Q.U + Q.V };
			for (int32 K = 0; K < 4; ++K)
			{
				const FVector D = Corner[K].GetSafeNormal();
				Q.Light[K] = 0.5f * (0.80f + 0.42f * Day * float(FVector::DotProduct(D, SunW)) + 0.12f * float(FVector::DotProduct(D, UpW)));
			}
		}
		else
		{
			// emissive colour (already premultiplied: it is light), opacity as given
			Q.Col = FLinearColor(C.R * Fade * Flick * Thin, C.G * Fade * Flick * Thin, C.B * Fade * Flick * Thin, C.A);
		}
		if (P.Temp0 > 0.f && P.Glow > 0.f)
		{
			// incandescence on top (cooling embers, fireballs, lava bombs): brightness ~ T^4
			const float Tk = FMath::Lerp(P.Temp0, P.Temp1, FMath::Pow(T, P.ColorPow));
			const float Hot = FMath::Pow(FMath::Max(Tk, 1.f) / P.Temp0, 4.f);
			const FLinearColor Bb = Blackbody(Tk);
			// wave 4b: no glow below ~900 K (dull red light added to grey smoke read as salmon-pink plumes)
			const float Inc = P.Glow * Hot * Fade * Flick * Thin * Smooth01((Tk - 850.f) / 450.f);
			Q.Col.R += Bb.R * Inc; Q.Col.G += Bb.G * Inc; Q.Col.B += Bb.B * Inc;
		}
		Renderer.Add(Q);
	}
}

// ------------------------------------------------------------------ world state

void ADmFx::ScanDisasters()
{
	const dm::World* Wd = W();
	if (!Wd) return;
	for (const dm::Disaster& D : Wd->disasters)
	{
		if (Tracked.Contains(D.id)) continue;
		Tracked.Add(D.id);
		FDmFxEffect* E = nullptr;
		switch (D.type)
		{
		case dm::DisasterType::Comet:
		case dm::DisasterType::Meteor:
			E = DmFxFx::MakeFlight(*this, D.id, D.cell, D.type == dm::DisasterType::Comet, float(D.eta), float(D.flight));
			break;
		case dm::DisasterType::Volcano: E = DmFxFx::MakeVolcano(*this, D.id, D.cell); break;
		case dm::DisasterType::Tsunami: E = DmFxFx::MakeTsunami(*this, D.id, D.cell); break;
		case dm::DisasterType::Storm: E = DmFxFx::MakeStorm(*this, D.id); break;
		case dm::DisasterType::Drought: E = DmFxFx::MakeDrought(*this, D.id); break;
		default: break;
		}
		if (E) { E->DisasterId = D.id; AddEffect(E); }
	}
	// forget ids that are gone (keeps the set small over a long game)
	if (Tracked.Num() > 64)
	{
		TSet<int32> Live;
		for (const dm::Disaster& D : Wd->disasters) Live.Add(D.id);
		for (auto It = Tracked.CreateIterator(); It; ++It) if (!Live.Contains(*It) && !HasEffectFor(*It)) It.RemoveCurrent();
	}
}

void ADmFx::UpdateAmbient(float Dt)
{
	const dm::World* Wd = W();
	if (!Wd) return;

	// fires: a cell burns for only two sim ticks, so each burning cell keeps a visual heat that decays over
	// about 8 real seconds (flames, then smouldering smoke): a wildfire reads as fire at any game speed.
	// At most kMaxFireCells, the nearest to the camera (rescanned at 4 Hz).
	{
		const std::vector<int32_t>& Fc = Wd->cache.fires;
		for (int32_t C : Fc)
		{
			if (C < 0 || C >= Wd->n || !(Wd->fire[size_t(C)] > 0.f)) continue;
			float& H = FireHeat.FindOrAdd(int32(C));
			H = FMath::Max(H, 0.55f + 0.45f * FMath::Clamp(Wd->fire[size_t(C)], 0.f, 1.f));
		}
		for (auto It = FireHeat.CreateIterator(); It; ++It)
		{
			It.Value() -= Dt / 8.f;
			if (It.Value() <= 0.f) { EmitAcc.Remove(It.Key()); It.RemoveCurrent(); }
		}
		if (Clock - FireScanAt > 0.25 || FireScanAt < 0)
		{
			FireScanAt = Clock;
			FireCells.Reset();
			TArray<TPair<double, int32>> ByDist;
			ByDist.Reserve(FireHeat.Num());
			for (const auto& Fh : FireHeat) ByDist.Add(TPair<double, int32>(FVector::DistSquared(CellSurface(Fh.Key), CamP), Fh.Key));
			if (ByDist.Num() > kMaxFireCells)
			{
				std::nth_element(ByDist.GetData(), ByDist.GetData() + kMaxFireCells, ByDist.GetData() + ByDist.Num(),
					[](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
				ByDist.SetNum(kMaxFireCells);
			}
			for (const TPair<double, int32>& P : ByDist) FireCells.Add(P.Value);
		}
		for (int32 C : FireCells)
		{
			const float* H = FireHeat.Find(C);
			if (!H || *H <= 0.f) continue;
			float& Acc = EmitAcc.FindOrAdd(C);
			DmFxFx::AmbientFire(*this, C, *H, Dt, Acc);
		}
	}
	// settlements: plague miasma, prayer columns
	for (const dm::Settlement& S : Wd->settlements)
	{
		if (S.dead || S.cell < 0) continue;
		if (S.plague > 0)
		{
			float& Acc = EmitAcc.FindOrAdd(-1000000 - S.id);
			DmFxFx::AmbientMiasma(*this, S.cell, float(S.plague), Dt, Acc);
		}
		if (S.prayer.has_value()) DmFxFx::AmbientPrayer(*this, S.cell, int32(S.prayer->kind), uint8(S.id * 53));
	}

	// prophets: a halo over the town of each living prophet, a walking light on each prophet unit
	for (const dm::Culture& Cu : Wd->cultures)
	{
		if (Cu.dead || !Cu.prophet.has_value()) continue;
		const int32 Sid = Cu.prophet->settlement;
		if (Sid < 0 || Sid >= int32(Wd->settlements.size()) || Wd->settlements[size_t(Sid)].dead) continue;
		float& Acc = EmitAcc.FindOrAdd(-2000000 - Cu.id);
		DmFxFx::AmbientProphet(*this, CellSurface(Wd->settlements[size_t(Sid)].cell, 0.25 * kKm), Dt, Acc, false);
	}
	for (const dm::Unit& U : Wd->units)
	{
		if (U.type != dm::UnitType::Prophet) continue;
		dm::Vec3 D;
		if (dm::units::dirOf(*Wd, U, D) < 0) continue;
		const FVector Dir = DmCoords::SimToUE(D).GetSafeNormal();
		float& Acc = EmitAcc.FindOrAdd(-3000000 - U.id);
		DmFxFx::AmbientProphet(*this, Dir * (SurfaceR(Dir) + 0.06 * kKm), Dt, Acc, true);
	}

	// the beacon over the best site while the player chooses where the first tribe wakes
	if (Wd->phase == dm::Phase::People)
	{
		if (Clock - BeaconScanAt > 2.0 || BeaconScanAt < 0) { BeaconScanAt = Clock; BeaconCell = dm::civ::bestSite(*Wd); }
		if (BeaconCell >= 0) DmFxFx::AmbientBeacon(*this, BeaconCell);
	}

	// aurora surges
	if (AuroraHold > 0.f) AuroraHold -= Dt;
	else AuroraTarget = FMath::Max(0.f, AuroraTarget - Dt * 0.05f);
	AuroraNow += (AuroraTarget - AuroraNow) * FMath::Min(1.f, Dt * 0.6f);
	if (AuroraNow > 0.01f && !ADmSky::Get(this)) DmFxFx::AmbientAurora(*this, AuroraNow);   // fallback only (see SetAurora)
}

// ------------------------------------------------------------------ events

void ADmFx::OnSimEvent(const dm::Event& E) { HandleEvent(E); }

void ADmFx::HandleEvent(const dm::Event& E)
{
	if (bQuiet) return;
	const dm::World* Wd = W();
	if (!Wd) return;
	auto SettlementCell = [Wd](int32 Id) -> int32
	{
		return (Id >= 0 && Id < int32(Wd->settlements.size())) ? Wd->settlements[size_t(Id)].cell : -1;
	};
	switch (E.type)
	{
	case dm::Ev::MeteorImpact:
		if (const dm::EvMeteorImpact* I = E.as<dm::EvMeteorImpact>())
		{
			const bool bComet = I->kind == dm::ImpactKind::Comet;
			AddEffect(DmFxFx::MakeImpact(*this, I->cell, I->ang, bComet, I->sea));
			if (bComet) SetAurora(0.35f, 6.f);
		}
		break;
	case dm::Ev::Lightning:
		if (const dm::EvLightning* L = E.as<dm::EvLightning>()) AddEffect(DmFxFx::MakeLightning(*this, CellSurface(L->cell), 1.f, false));
		break;
	case dm::Ev::Quake:
		if (const dm::EvQuake* Q = E.as<dm::EvQuake>()) AddEffect(DmFxFx::MakeQuake(*this, Q->cell, Q->ang, float(Q->power)));
		break;
	case dm::Ev::TsunamiHit:
		if (const dm::EvTsunamiHit* H = E.as<dm::EvTsunamiHit>()) DmFxFx::BurstSplash(*this, H->cell, float(H->height));
		break;
	case dm::Ev::FireStart:
		if (const dm::EvFireStart* F = E.as<dm::EvFireStart>()) DmFxFx::BurstFireStart(*this, F->cell);
		break;
	case dm::Ev::PowerUsed:
		if (const dm::EvPowerUsed* P = E.as<dm::EvPowerUsed>())
		{
			const std::string& Id = P->id;
			if (Id == "rain") AddEffect(DmFxFx::MakeRain(*this, P->cell, P->ang > 0 ? P->ang : 0.1, 1.6f));
			else if (Id == "bloom")
			{
				// a held brush sends up to 4 pulses a second (wave 4b): the running wave follows the brush and holds
				// (a new wave per 0.7 s stacked concentric target rings); a new wave only after a pause
				FDmFxEffect* Wv = FindEffect(TEXT("bloom"));
				if (!(Wv && Wv->Refresh(P->cell))) { LastBloomAt = Clock; AddEffect(DmFxFx::MakeGreenWave(*this, P->cell, P->ang > 0 ? P->ang : 0.08, 0.8f, false)); }
			}
			else if (Id == "seed") AddEffect(DmFxFx::MakeGreenWave(*this, P->cell, P->ang > 0 ? P->ang : 0.12, 1.f, true));
			else if (Id == "raise" || Id == "lower") DmFxFx::BurstSculpt(*this, P->cell, P->ang > 0 ? P->ang : 0.06, Id == "raise");
			else if (Id == "heal") AddEffect(DmFxFx::MakeBlessing(*this, P->cell, 0));
			else if (Id == "inspire") AddEffect(DmFxFx::MakeBlessing(*this, P->cell, 1));
			else if (Id == "peace") AddEffect(DmFxFx::MakePeace(*this, P->cell, 0.22));
			// (wave3-final) Consecrate (v3 power) had no effect at all: the answered-prayer blessing over the town
			else if (Id == "consecrate") AddEffect(DmFxFx::MakeBlessing(*this, P->cell, 2));
		}
		break;
	case dm::Ev::SettlementFounded:
		if (const dm::EvSettlementFounded* F = E.as<dm::EvSettlementFounded>()) DmFxFx::BurstFounded(*this, F->cell, F->by == dm::FoundedBy::Awaken);
		break;
	case dm::Ev::SettlementDestroyed:
		if (const dm::EvSettlementDestroyed* D = E.as<dm::EvSettlementDestroyed>()) DmFxFx::BurstDestroyed(*this, D->cell, int32(D->cause));
		break;
	case dm::Ev::SettlementPlague:
		if (const dm::EvSettlementPlague* P = E.as<dm::EvSettlementPlague>()) if (P->start) DmFxFx::BurstPlague(*this, SettlementCell(P->id), 1.f);
		break;
	case dm::Ev::PrayerAnswered:
		if (const dm::EvPrayerAnswered* P = E.as<dm::EvPrayerAnswered>()) AddEffect(DmFxFx::MakeBlessing(*this, SettlementCell(P->id), 2));
		break;
	case dm::Ev::CultureAge:
		if (const dm::EvCultureAge* A = E.as<dm::EvCultureAge>())
		{
			if (A->id >= 0 && A->id < int32(Wd->cultures.size()) && A->age > A->prev)
			{
				const dm::Culture& Cu = Wd->cultures[size_t(A->id)];
				AddEffect(DmFxFx::MakeAgeRays(*this, SettlementCell(Cu.capital), FLinearColor(float(Cu.color[0]), float(Cu.color[1]), float(Cu.color[2]))));
			}
		}
		break;
	case dm::Ev::WonderBuilt:
		if (const dm::EvWonderBuilt* Wb = E.as<dm::EvWonderBuilt>())
		{
			const int32 C = SettlementCell(Wb->settlement);
			AddEffect(DmFxFx::MakeAgeRays(*this, C, FLinearColor(1.f, 0.8f, 0.45f)));
			DmFxFx::BurstFounded(*this, C, true);
		}
		break;
	case dm::Ev::Battle:
		if (const dm::EvBattle* B = E.as<dm::EvBattle>()) DmFxFx::BurstBattle(*this, B->cell, B->won);
		break;
	case dm::Ev::ProphetRisen:
		if (const dm::EvProphetRisen* P = E.as<dm::EvProphetRisen>()) AddEffect(DmFxFx::MakeProphet(*this, SettlementCell(P->settlement)));
		break;
	case dm::Ev::Nuke:
		if (const dm::EvNuke* N = E.as<dm::EvNuke>()) { AddEffect(DmFxFx::MakeNuke(*this, N->cell)); SetAurora(0.8f, 18.f); }
		break;
	case dm::Ev::ArkLaunched:
		if (const dm::EvArkLaunched* A = E.as<dm::EvArkLaunched>()) AddEffect(DmFxFx::MakeArk(*this, SettlementCell(A->settlement)));
		break;
	case dm::Ev::MiracleWitnessed:
		if (const dm::EvMiracleWitnessed* M = E.as<dm::EvMiracleWitnessed>())
		{
			if (M->good != 0.0)
				for (int32 Id : M->ids) DmFxFx::BurstWitness(*this, SettlementCell(Id), M->good > 0.0);
		}
		break;
	default: break;
	}
}

// ------------------------------------------------------------------ generic spawn and previews

void ADmFx::Spawn(const FString& Type, const FVector& PlanetPos, int32 Count, float Size, const FLinearColor* Color, float RiseScale)
{
	const FVector Up = PlanetPos.GetSafeNormal();
	const float S = 0.15f * float(kKm) * FMath::Max(Size, 0.01f);
	const int32 N = FMath::Clamp(Count, 0, 200);
	for (int32 I = 0; I < N; ++I)
	{
		FParticle P;
		P.P = PlanetPos + Rng.Dir() * (S * 0.3);
		P.Seed = Rng.Byte();
		P.Rot = Rng.Range(0.f, 6.28f);
		if (Type == TEXT("smoke") || Type == TEXT("steam") || Type == TEXT("dust"))
		{
			const bool bSteam = Type == TEXT("steam"), bDust = Type == TEXT("dust");
			P.Shape = EShape::Smoke; P.bLit = true;
			P.Life = Rng.Range(4.f, 8.f) * (bDust ? 0.6f : 1.f);
			P.Size0 = S * 0.5f; P.Size1 = S * Rng.Range(2.f, 3.5f); P.SizePow = 0.6f;
			P.V = Up * (S * (bSteam ? 1.2f : bDust ? 0.25f : 0.6f) * RiseScale) + Rng.Dir() * (S * 0.15);
			P.Drag = 0.4f; P.Wind = 0.6f; P.Spin = Rng.Sym(0.3f);
			const FLinearColor Def = bSteam ? FLinearColor(0.85f, 0.87f, 0.9f, 0.5f) : bDust ? FLinearColor(0.42f, 0.36f, 0.28f, 0.5f) : FLinearColor(0.16f, 0.15f, 0.14f, 0.6f);
			P.C0 = Color ? *Color : Def; P.C1 = P.C0; P.C1.A *= 0.6f;
			P.FadeIn = 0.1f; P.FadeOut = 0.6f;
		}
		else if (Type == TEXT("sparks"))
		{
			P.Shape = EShape::Streak; P.Orient = EOrient::Velocity;
			P.Life = Rng.Range(0.5f, 1.2f);
			P.Size0 = P.Size1 = S * 0.06f; P.Aspect = 2.f; P.Stretch = 0.05f;
			P.V = (Up * 1.2 + Rng.Dir()).GetSafeNormal() * (S * Rng.Range(4.f, 9.f) * RiseScale);
			P.Rise = -S * 9.f; P.Drag = 0.6f;
			P.C0 = Color ? *Color : FLinearColor(6.f, 2.6f, 0.8f, 0.f); P.C1 = P.C0 * 0.2f; P.C1.A = 0.f;
			P.FadeIn = 0.02f; P.FadeOut = 0.5f;
		}
		else if (Type == TEXT("splash"))
		{
			P.Shape = EShape::Dot; P.bLit = true; P.Orient = EOrient::Velocity;
			P.Life = Rng.Range(0.7f, 1.4f);
			P.Size0 = S * 0.08f; P.Size1 = S * 0.12f; P.Aspect = 1.5f; P.Stretch = 0.03f;
			P.V = (Up * 1.6 + Rng.Dir()).GetSafeNormal() * (S * Rng.Range(4.f, 8.f) * RiseScale);
			P.Rise = -S * 10.f;
			P.C0 = Color ? *Color : FLinearColor(0.8f, 0.85f, 0.9f, 0.9f); P.C1 = P.C0;
			P.FadeIn = 0.02f; P.FadeOut = 0.3f;
		}
		else if (Type == TEXT("motes"))
		{
			P.Shape = EShape::Mote;
			P.Life = Rng.Range(2.f, 4.f);
			P.Size0 = P.Size1 = S * 0.12f;
			P.V = Up * (S * 0.5f * RiseScale) + Rng.Dir() * (S * 0.2);
			P.Drag = 0.3f;
			P.C0 = Color ? *Color : FLinearColor(3.f, 2.3f, 1.0f, 0.f); P.C1 = P.C0;
			P.FadeIn = 0.2f; P.FadeOut = 0.4f;
		}
		else return;   // unknown types are ignored
		Emit(P);
	}
}

bool ADmFx::Play(const FString& What, int32 Cell, float Strength)
{
	const dm::World* Wd = W();
	if (!Wd || Cell < 0 || Cell >= Wd->n) return false;
	const FString K = What.ToLower();
	const bool bSea = IsSeaCell(Cell);
	if (K == TEXT("meteor")) AddEffect(DmFxFx::MakeFlight(*this, -1, Cell, false, 1.6f, 1.6f));
	else if (K == TEXT("comet")) AddEffect(DmFxFx::MakeFlight(*this, -1, Cell, true, 1.4f, 1.4f));
	else if (K == TEXT("impact")) AddEffect(DmFxFx::MakeImpact(*this, Cell, 0.05, false, bSea));
	else if (K == TEXT("lightning")) AddEffect(DmFxFx::MakeLightning(*this, CellSurface(Cell), Strength, false));
	else if (K == TEXT("quake")) AddEffect(DmFxFx::MakeQuake(*this, Cell, 0.08, Strength));
	else if (K == TEXT("nuke")) { AddEffect(DmFxFx::MakeNuke(*this, Cell)); SetAurora(0.8f, 18.f); }
	else if (K == TEXT("heal")) AddEffect(DmFxFx::MakeBlessing(*this, Cell, 0));
	else if (K == TEXT("inspire")) AddEffect(DmFxFx::MakeBlessing(*this, Cell, 1));
	else if (K == TEXT("bless")) AddEffect(DmFxFx::MakeBlessing(*this, Cell, 2));
	else if (K == TEXT("prophet")) AddEffect(DmFxFx::MakeProphet(*this, Cell));
	else if (K == TEXT("peace")) AddEffect(DmFxFx::MakePeace(*this, Cell, 0.22));
	else if (K == TEXT("bloom")) AddEffect(DmFxFx::MakeGreenWave(*this, Cell, 0.08, Strength, false));
	else if (K == TEXT("seed")) AddEffect(DmFxFx::MakeGreenWave(*this, Cell, 0.12, Strength, true));
	else if (K == TEXT("rain")) AddEffect(DmFxFx::MakeRain(*this, Cell, 0.1, 6.f * Strength));
	else if (K == TEXT("plague")) DmFxFx::BurstPlague(*this, Cell, Strength);
	else if (K == TEXT("sculpt")) DmFxFx::BurstSculpt(*this, Cell, 0.06, true);
	else if (K == TEXT("founded")) DmFxFx::BurstFounded(*this, Cell, Strength > 0.5f);
	else if (K == TEXT("age")) AddEffect(DmFxFx::MakeAgeRays(*this, Cell, FLinearColor(1.f, 0.85f, 0.55f)));
	else if (K == TEXT("battle")) DmFxFx::BurstBattle(*this, Cell, true);
	else if (K == TEXT("ark")) AddEffect(DmFxFx::MakeArk(*this, Cell));
	else if (K == TEXT("destroyed")) DmFxFx::BurstDestroyed(*this, Cell, int32(dm::Cause::War));
	else if (K == TEXT("splash")) DmFxFx::BurstSplash(*this, Cell, 1.2f * Strength);
	else if (K == TEXT("aurora")) SetAurora(Strength, 60.f);
	else if (K == TEXT("fire")) DmFxFx::BurstFireStart(*this, Cell);
	else return false;
	return true;
}

// ------------------------------------------------------------------ scene part

static FDmScenePartRegistrar GDmFxPart(TEXT("fx"), EDmPartKind::World, 0,
	[](const FDmSceneContext& Ctx) -> AActor*
	{
		return Ctx.World ? Ctx.World->SpawnActor<ADmFx>() : nullptr;
	}, TEXT("Game/FX (C6 fx)"));
