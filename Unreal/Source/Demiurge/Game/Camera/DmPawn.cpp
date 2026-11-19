// DEMIURGE — Game/Camera/DmPawn.cpp : see DmPawn.h. Owner: C3 camera-input.

#include "Game/Camera/DmPawn.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmGameMode.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmScene.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Game/Input/DmControl.h"
#include "Game/Input/DmInputRouter.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmPawn, Log, All);

namespace
{
	FDmGroundProvider GGroundProvider;
}

// the "camera" scene part: spawn the pawn and let the player controller possess it
static FDmScenePartRegistrar GDmCameraPart(TEXT("camera"), EDmPartKind::Control, 0,
	[](const FDmSceneContext& S) -> AActor*
	{
		if (!S.World) return nullptr;
		if (ADmPawn* Existing = ADmPawn::Find(S.World)) return Existing;
		ADmPawn* P = S.World->SpawnActor<ADmPawn>(FVector::ZeroVector, FRotator::ZeroRotator);
		APlayerController* PC = S.PC ? S.PC : S.World->GetFirstPlayerController();
		if (P && PC) PC->Possess(P);
		return P;
	}, TEXT("Game/Camera/DmPawn.cpp"));

ADmPawn::ADmPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	// After every PrePhysics publisher (the sky turns the planet in TG_PrePhysics, in no fixed order with
	// other actors), before the player camera manager reads the view (after TG_PostPhysics): the camera is
	// always carried by THIS frame's planet transform, so a turning planet never shakes the close view.
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetConstraintAspectRatio(false);
	Camera->bUsePawnControlRotation = false;
	Router = CreateDefaultSubobject<UDmInputRouter>(TEXT("Input"));
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	SetActorEnableCollision(false);
}

ADmPawn* ADmPawn::Find(const UObject* WorldContext)
{
	UWorld* W = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!W) return nullptr;
	for (TActorIterator<ADmPawn> It(W); It; ++It) return *It;
	return nullptr;
}

void ADmPawn::SetGroundProvider(FDmGroundProvider Provider) { GGroundProvider = MoveTemp(Provider); }
void ADmPawn::ClearGroundProvider() { GGroundProvider = nullptr; }

void ADmPawn::BeginPlay()
{
	Super::BeginPlay();
	if (UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
		SimHandle = Sim->OnSimEvent.AddUObject(this, &ADmPawn::OnSimEvent);
	if (UDmControlSubsystem* C = UDmControlSubsystem::Get(this)) C->RegisterPawn(this);
	UpdateViewport();
	CameraRig.Reset();
	ApplyView();
	UE_LOG(LogDmPawn, Display, TEXT("DmPawn: ready (orbit distance %.1f km, zoom-1 height %.2f km, vfov %.0f)"),
		CameraRig.OrbitDistance(), CameraRig.Tune.NearHeightKm, CameraRig.Tune.VFovDeg);
}

void ADmPawn::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
		if (SimHandle.IsValid()) Sim->OnSimEvent.Remove(SimHandle);
	SimHandle.Reset();
	Super::EndPlay(Reason);
}

void ADmPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	if (APlayerController* PC = Cast<APlayerController>(NewController))
	{
		AddTickPrerequisiteActor(PC);    // input first, then the camera
		Router->AddMappingContext(PC);
		PC->SetViewTarget(this);
	}
}

void ADmPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	Router->BindInput(Cast<UEnhancedInputComponent>(PlayerInputComponent));
}

void ADmPawn::OnSimEvent(const dm::Event& E)
{
	if (const dm::EvCameraShake* S = E.as<dm::EvCameraShake>()) Shake(S->amount);
}

double ADmPawn::GroundKm(const FVector& PlanetDir) const
{
	const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	double R = DmCoords::RadiusUU;
	if (Sim && Sim->Sim()) R = Sim->SurfaceRadiusUU(PlanetDir, GroundHint);
	if (GGroundProvider)
	{
		const double P = GGroundProvider(PlanetDir);
		if (FMath::IsFinite(P) && P > R) R = P;
	}
	return (FMath::IsFinite(R) && R > 1.0 ? R : DmCoords::RadiusUU) / DmCoords::UUPerKm;
}

FVector ADmPawn::PlanetKmToWorld(const FVector& Km) const
{
	const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const FTransform P2W = Sim ? Sim->GetPlanetToWorld() : FTransform::Identity;
	return P2W.TransformPosition(Km * DmCoords::UUPerKm);
}

FVector ADmPawn::WorldToPlanetDir(const FVector& World) const
{
	const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const FTransform P2W = Sim ? Sim->GetPlanetToWorld() : FTransform::Identity;
	return P2W.InverseTransformPosition(World).GetSafeNormal();
}

void ADmPawn::UpdateViewport()
{
	int32 W = 0, H = 0;
	if (const APlayerController* PC = Cast<APlayerController>(GetController())) PC->GetViewportSize(W, H);
	if ((W <= 0 || H <= 0) && GEngine && GEngine->GameViewport)
	{
		FVector2D S;
		GEngine->GameViewport->GetViewportSize(S);
		W = int32(S.X); H = int32(S.Y);
	}
	if (W > 0 && H > 0) CameraRig.SetViewport(W, H);
}

void ADmPawn::Jump(const FVector& PlanetDir, double Zoom, double HeadingRad)
{
	CameraRig.Jump(PlanetDir, Zoom, HeadingRad);
	CameraRig.Update(0.0, [this](const FVector& D) { return GroundKm(D); });
	ApplyView();
}

void ADmPawn::JumpLatLon(double LatDeg, double LonDeg, double Zoom, double HeadingDeg)
{
	Jump(DmCoords::LatLonToDir(LatDeg, LonDeg), Zoom, FMath::DegreesToRadians(HeadingDeg));
}

void ADmPawn::FlyTo(const FVector& PlanetDir, double Zoom, double Seconds)
{
	CameraRig.FlyTo(PlanetDir, Zoom, Seconds);
}

void ADmPawn::Shake(double Amount)
{
	CameraRig.Shake(Amount);
}

void ADmPawn::AdoptView(const FVector& WorldLoc, const FRotator& WorldRot)
{
	const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const FTransform P2W = Sim ? Sim->GetPlanetToWorld() : FTransform::Identity;
	const FVector EyeKm = P2W.InverseTransformPosition(WorldLoc) / DmCoords::UUPerKm;
	const FVector Dir = P2W.InverseTransformVectorNoScale(WorldRot.Vector()).GetSafeNormal();
	const FVector UpV = P2W.InverseTransformVectorNoScale(FRotationMatrix(WorldRot).GetUnitAxis(EAxis::Z)).GetSafeNormal();
	// where the view axis meets the datum sphere (or the point under the eye if it misses)
	const double B = FVector::DotProduct(EyeKm, Dir);
	const double C = EyeKm.SizeSquared() - DmCoords::RadiusKm * DmCoords::RadiusKm;
	const double Disc = B * B - C;
	const FVector Focus = Disc >= 0.0 ? (EyeKm + Dir * (-B - FMath::Sqrt(Disc))).GetSafeNormal() : EyeKm.GetSafeNormal();
	// zoom from the height above the ground: h = h0 (h1 / h0)^z
	const double H = EyeKm.Size() - GroundKm(EyeKm.GetSafeNormal());
	const double H0 = CameraRig.HeightAt(0.0), H1 = CameraRig.HeightAt(1.0);
	const double Z = FMath::Clamp(FMath::Loge(FMath::Max(H, H1) / H0) / FMath::Loge(H1 / H0), 0.0, 1.0);
	// heading: screen up projected on the tangent plane (looking down) or the view direction (tilted)
	FVector T = (FMath::Abs(FVector::DotProduct(Dir, Focus)) > 0.95 ? UpV : Dir);
	T = (T - Focus * FVector::DotProduct(T, Focus)).GetSafeNormal();
	FVector E, N;
	DmCoords::Basis(Focus, E, N);
	const double Heading = T.IsNearlyZero() ? 0.0 : FMath::Atan2(FVector::DotProduct(T, E), FVector::DotProduct(T, N));
	Jump(Focus, Z, Heading);
	UE_LOG(LogDmPawn, Display, TEXT("DmPawn: adopted view (height %.1f km -> zoom %.3f, heading %.0f deg)"), H, Z, FMath::RadiansToDegrees(Heading));
}

void ADmPawn::ApplyView()
{
	const FDmCameraView& V = CameraRig.View();
	const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const FTransform P2W = Sim ? Sim->GetPlanetToWorld() : FTransform::Identity;
	const FVector Loc = P2W.TransformPosition(V.Eye * DmCoords::UUPerKm);
	const FVector Fw = P2W.TransformVectorNoScale(V.Forward);
	const FVector Up = P2W.TransformVectorNoScale(V.Up);
	const FQuat Q = FRotationMatrix::MakeFromXZ(Fw, Up).ToQuat();
	SetActorLocationAndRotation(Loc, Q);
	Camera->SetFieldOfView(float(FMath::Clamp(V.HFovDeg, 5.0, 170.0)));
}

void ADmPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	++FramesTicked;
	UpdateViewport();

	// default "play": take over the harness camera's view once
	if (!bAdopted && bAdoptHarnessView)
	{
		bAdopted = true;
		const FDmShotArgs& A = FDmShotArgs::Get();
		const bool bPlay = A.Scenario.IsEmpty() || A.Scenario.Equals(TEXT("play"), ESearchCase::IgnoreCase);
		if (bPlay && !A.bCam)
			if (ADmGameMode* M = GetWorld() ? Cast<ADmGameMode>(GetWorld()->GetAuthGameMode()) : nullptr)
				if (ACameraActor* Cam = M->GetHarnessCamera())
				{
					AdoptView(Cam->GetActorLocation(), Cam->GetActorRotation());
					M->HarnessUseCamera(false);
					if (APlayerController* PC = Cast<APlayerController>(GetController())) PC->SetViewTarget(this);
				}
	}

	if (const UDmControlSubsystem* C = UDmControlSubsystem::Get(this)) CameraRig.Tune.ShakeScale = C->ShakeSetting;
	Router->ProcessFrame(DeltaSeconds);
	CameraRig.Update(DeltaSeconds, [this](const FVector& D) { return GroundKm(D); });
	if (!CameraRig.IsFinite())
	{
		UE_LOG(LogDmPawn, Error, TEXT("DmPawn: non-finite view, resetting the rig"));
		CameraRig.Reset();
	}
	MinAltitudeSeenKm = FMath::Min(MinAltitudeSeenKm, CameraRig.View().AltitudeKm);
	ApplyView();
}
