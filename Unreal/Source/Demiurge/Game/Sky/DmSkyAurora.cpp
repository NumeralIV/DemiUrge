// DEMIURGE — Game/Sky/DmSkyAurora.cpp : see the header. Owner: the aurora track (lanes/aurora3.owned).

#include "Game/Sky/DmSkyAurora.h"
#include "Game/Sky/DmSkyActor.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmScene.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Sim/Sim.h"

#include "Engine/World.h"
#include "Materials/Material.h"
#include "SceneInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmAurora, Log, All);

namespace
{
	TWeakObjectPtr<ADmAurora> GActiveAurora;

	FLinearColor Lc(const FVector& V) { return FLinearColor(float(V.X), float(V.Y), float(V.Z), 0.f); }

	/** Open spherical caps of unit radius around each pole (positions = normals), CapDeg wide, 1 deg rings.
	 *  The component is scaled to the slab's outer radius; the material only uses the fragment's position to
	 *  find its view ray, so the cap is a launcher for the march, never drawn as a surface itself. */
	void MakeCaps(UProceduralMeshComponent* P, const FVector Poles[2], double CapDeg)
	{
		const int32 Rings = FMath::Max(8, FMath::CeilToInt32(CapDeg));
		constexpr int32 Segs = 192;
		for (int32 Hemi = 0; Hemi < 2; ++Hemi)
		{
			FVector E, N;
			DmCoords::Basis(Poles[Hemi], E, N);
			TArray<FVector> V; TArray<int32> T; TArray<FVector> Nn; TArray<FVector2D> UV; TArray<FColor> C; TArray<FProcMeshTangent> Tg;
			V.Reserve(1 + Rings * Segs);
			V.Add(Poles[Hemi]); Nn.Add(Poles[Hemi]); UV.Add(FVector2D(0.0, 0.0)); Tg.Add(FProcMeshTangent(E, false));
			for (int32 R = 1; R <= Rings; ++R)
			{
				const double Cl = FMath::DegreesToRadians(CapDeg * R / Rings);
				for (int32 S = 0; S < Segs; ++S)
				{
					const double Ph = 2.0 * UE_DOUBLE_PI * S / Segs;
					const FVector Around = E * FMath::Cos(Ph) + N * FMath::Sin(Ph);
					const FVector D = (Poles[Hemi] * FMath::Cos(Cl) + Around * FMath::Sin(Cl)).GetSafeNormal();
					V.Add(D); Nn.Add(D); UV.Add(FVector2D(double(S) / Segs, double(R) / Rings));
					Tg.Add(FProcMeshTangent(FVector::CrossProduct(D, Around).GetSafeNormal(), false));
				}
			}
			for (int32 S = 0; S < Segs; ++S) T.Append({ 0, 1 + (S + 1) % Segs, 1 + S });
			for (int32 R = 1; R < Rings; ++R)
				for (int32 S = 0; S < Segs; ++S)
				{
					const int32 A = 1 + (R - 1) * Segs + S, B = 1 + (R - 1) * Segs + (S + 1) % Segs;
					const int32 A2 = A + Segs, B2 = B + Segs;
					T.Append({ A, B, A2, B, B2, A2 });
				}
			P->CreateMeshSection(Hemi, V, T, Nn, UV, C, Tg, false);
		}
	}
}

ADmAurora::ADmAurora()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;   // after the sky published the planet frame and the sun
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;

	Shell = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("AuroraShell"));
	Shell->SetupAttachment(RootComponent);
	Shell->SetMobility(EComponentMobility::Movable);
	Shell->SetCastShadow(false);
	Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shell->bAffectDistanceFieldLighting = false;
	Shell->SetVisibleInRayTracing(false);
	Shell->SetReceivesDecals(false);
	Shell->bUseAsOccluder = false;
}

ADmAurora* ADmAurora::Get(const UObject* WorldContext)
{
	ADmAurora* A = GActiveAurora.Get();
	if (A && WorldContext && A->GetWorld() != WorldContext->GetWorld()) return nullptr;
	return A;
}

FVector ADmAurora::GetPole(int32 Hemi) const
{
	const int32 H = FMath::Clamp(Hemi, 0, 1);
	return DmCoords::LatLonToDir(Tune.PoleLat[H], Tune.PoleLon[H]);
}

double ADmAurora::NightFactor(const FVector& PlanetDir, const FVector& SunPlanet)
{
	// mirrors aurora.hlsl: smoothstep(0.0, -0.21, dot(n, sun))
	const double X = FMath::Clamp((FVector::DotProduct(PlanetDir.GetSafeNormal(), SunPlanet.GetSafeNormal()) - 0.0) / (-0.21), 0.0, 1.0);
	return X * X * (3.0 - 2.0 * X);
}

void ADmAurora::BeginPlay()
{
	Super::BeginPlay();
	GActiveAurora = this;
	bOff = FDmShotArgs::Param(TEXT("noaurora"));
	bTrace = FDmShotArgs::Param(TEXT("auroratrace"));
	bDebug = FDmShotArgs::Param(TEXT("auroradebug"));
	FDmShotArgs::Value(TEXT("-aurorasteps="), Tune.Steps);
	Tune.Steps = FMath::Clamp(Tune.Steps, 4.0, 256.0);
	FDmShotArgs::Value(TEXT("-auroraboost="), HeldBoost);
	HeldBoost = FMath::Clamp(HeldBoost, 0.0, 1.0);

	UMaterialInterface* M = bOff ? nullptr : LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Sky/Aurora/M_Aurora.M_Aurora"));
	if (!M)
	{
		if (!bOff) UE_LOG(LogDmAurora, Warning, TEXT("DmAurora: /Game/Sky/Aurora/M_Aurora missing (run Scripts/aurora.py): no aurora"));
		Shell->SetVisibility(false);
		return;
	}
	BuildShell();
	MID = UMaterialInstanceDynamic::Create(M, this);
	Shell->SetMaterial(0, MID);
	Shell->SetMaterial(1, MID);
	const FVector Pn = GetPole(0), Ps = GetPole(1);
	FVector En, Nn, Es, Ns;
	DmCoords::Basis(Pn, En, Nn);
	DmCoords::Basis(Ps, Es, Ns);
	MID->SetVectorParameterValue(TEXT("PoleN"), Lc(Pn));
	MID->SetVectorParameterValue(TEXT("EN"), Lc(En));
	MID->SetVectorParameterValue(TEXT("NN"), Lc(Nn));
	MID->SetVectorParameterValue(TEXT("PoleS"), Lc(Ps));
	MID->SetVectorParameterValue(TEXT("ES"), Lc(Es));
	MID->SetVectorParameterValue(TEXT("NS"), Lc(Ns));
	UE_LOG(LogDmAurora, Display, TEXT("DmAurora: ready (caps %.0f deg around (%.0f,%.0f) and (%.0f,%.0f), slab %.1f..%.1f km, %d steps)"),
		Tune.CapDeg, Tune.PoleLat[0], Tune.PoleLon[0], Tune.PoleLat[1], Tune.PoleLon[1], Tune.BaseKm, Tune.TopKm, int32(Tune.Steps));
}

bool ADmAurora::IsMaterialReady() const
{
	UMaterial* Base = MID ? MID->GetMaterial() : nullptr;
	const UWorld* W = GetWorld();
	if (!Base || !W || !W->Scene) return false;
	return !Base->IsCompilingOrHadCompileError(W->Scene->GetShaderPlatform());
}

void ADmAurora::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GActiveAurora.Get() == this) GActiveAurora.Reset();
	Super::EndPlay(Reason);
}

void ADmAurora::BuildShell()
{
	const FVector Poles[2] = { GetPole(0), GetPole(1) };
	MakeCaps(Shell, Poles, Tune.CapDeg);
}

void ADmAurora::Boost(float Amount, float Seconds)
{
	BoostNow = FMath::Max(BoostNow, double(FMath::Clamp(Amount, 0.f, 1.f)));
	BoostRate = BoostNow / FMath::Max(1.0, double(Seconds));
}

void ADmAurora::HideSkyRibbons()
{
	ADmSky* Sky = ADmSky::Get(this);
	if (Sky && Sky->Aurora && Sky->Aurora->IsVisible())
	{
		Sky->Aurora->SetVisibility(false);
		bSkyHidden = true;
		UE_LOG(LogDmAurora, Display, TEXT("DmAurora: the sky's ribbon aurora is hidden (this actor draws the aurora)"));
	}
	else if (Sky && Sky->Aurora) bSkyHidden = true;
}

double ADmAurora::ReadSkyBoost(double& OutGain) const
{
	OutGain = Tune.DefaultGain;
	const ADmSky* Sky = ADmSky::Get(this);
	if (!Sky) return 0.0;
	OutGain = Sky->Tune.AuroraGain;
	// the sky still owns BoostAurora (the FX surge calls it): its ribbon material carries Gain x (1 + 3 x boost)
	const UMaterialInstance* MI = Sky->Aurora ? Cast<UMaterialInstance>(Sky->Aurora->GetMaterial(0)) : nullptr;
	float S = 0.f;
	if (!MI || OutGain <= 1e-6 || !MI->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Strength")), S)) return 0.0;
	return FMath::Clamp((double(S) / OutGain - 1.0) / 3.0, 0.0, 1.0);
}

void ADmAurora::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	++Frames;
	HideSkyRibbons();
	if (!MID) return;

	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const dm::World* W = Sim ? Sim->Sim() : nullptr;
	const FTransform P2W = Sim ? Sim->GetPlanetToWorld() : FTransform::Identity;
	const FVector SunWorld = Sim ? Sim->GetSunDirection() : FVector(1, 0, 0);
	SetActorLocationAndRotation(P2W.GetLocation(), P2W.GetRotation());

	// the slab sits on the sea (comets raise it during genesis)
	const double SeaKm = (W ? DmCoords::HeightToRadiusUU(W->seaLevel) : DmCoords::RadiusUU) / DmCoords::UUPerKm;
	const double OuterKm = SeaKm + Tune.TopKm;
	if (FMath::Abs(OuterKm + 0.1 - ShellRadiusKm) > 0.02)
	{
		ShellRadiusKm = OuterKm + 0.1;   // just outside the analytic sphere: chords never cut inside it
		Shell->SetRelativeScale3D(FVector(ShellRadiusKm * DmCoords::UUPerKm));
	}

	BoostNow = FMath::Max(0.0, BoostNow - BoostRate * double(DeltaSeconds));
	double Gain = Tune.DefaultGain;
	SkyBoost = ReadSkyBoost(Gain);
	const double B = FMath::Max3(BoostNow, SkyBoost, HeldBoost);
	Strength = float(Gain * (1.0 + Tune.BoostGain * B));

	const FQuat Q = P2W.GetRotation();
	MID->SetVectorParameterValue(TEXT("PC"), Lc(P2W.GetLocation()));
	MID->SetVectorParameterValue(TEXT("PX"), Lc(Q.RotateVector(FVector::XAxisVector)));
	MID->SetVectorParameterValue(TEXT("PY"), Lc(Q.RotateVector(FVector::YAxisVector)));
	MID->SetVectorParameterValue(TEXT("PZ"), Lc(Q.RotateVector(FVector::ZAxisVector)));
	MID->SetVectorParameterValue(TEXT("SunDir"), Lc(SunWorld.GetSafeNormal()));
	MID->SetVectorParameterValue(TEXT("Shape"), FLinearColor(float(SeaKm), float(Tune.BaseKm), float(Tune.TopKm),
		float(FMath::Cos(FMath::DegreesToRadians(Tune.CapDeg)))));
	MID->SetVectorParameterValue(TEXT("Anim"), FLinearColor(float(B), Strength, float(Tune.Steps), bDebug ? -1.f : float(FMath::Clamp(B, 0.0, 1.0))));

	if (bTrace && Frames % 60 == 1)
	{
		const FVector SunP = Q.UnrotateVector(SunWorld);
		UE_LOG(LogDmAurora, Display, TEXT("DmAurora: frame %d strength %.3f boost %.2f (own %.2f sky %.2f held %.2f) sea %.2f km night N %.2f S %.2f"),
			Frames, Strength, B, BoostNow, SkyBoost, HeldBoost, SeaKm, NightFactor(GetPole(0), SunP), NightFactor(GetPole(1), SunP));
	}
}

// ---------------------------------------------------------------------------------------------- scene part

static FDmScenePartRegistrar GDmAuroraPart(TEXT("aurora"), EDmPartKind::World, 0,
	[](const FDmSceneContext& S) -> AActor* { return S.World ? S.World->SpawnActor<ADmAurora>() : nullptr; },
	TEXT("Game/Sky/DmSkyAurora.cpp"));
