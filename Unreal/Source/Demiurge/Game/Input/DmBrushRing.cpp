// DEMIURGE — Game/Input/DmBrushRing.cpp : see DmBrushRing.h. Owner: C3 camera-input.

#include "Game/Input/DmBrushRing.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmSimSubsystem.h"

#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmRing, Log, All);

ADmBrushRing::ADmBrushRing()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Ring"));
	RootComponent = Mesh;
	Mesh->SetCastShadow(false);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->bUseAsyncCooking = false;
	Mesh->SetVisibility(false);
	Mesh->bAffectDistanceFieldLighting = false;
	Mesh->SetAffectDynamicIndirectLighting(false);
}

void ADmBrushRing::BeginPlay()
{
	Super::BeginPlay();
	UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/CameraInput/M_BrushRing.M_BrushRing"));
	UMaterialInterface* X = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/CameraInput/M_BrushRingXRay.M_BrushRingXRay"));
	bMaterials = M && X;
	if (!bMaterials)
	{
		UE_LOG(LogDmRing, Error, TEXT("DmRing: /Game/CameraInput ring materials missing (run Scripts/camera-input.py)"));
		return;
	}
	double G = Glow;
	if (FDmShotArgs::Value(TEXT("-ringglow="), G) && FMath::IsFinite(G) && G > 0.0) Glow = float(G);
	Mat = UMaterialInstanceDynamic::Create(M, this);
	MatXRay = UMaterialInstanceDynamic::Create(X, this);
	// bright enough to read on a sunlit continent, low enough to keep the group colour (no white clip)
	Mat->SetScalarParameterValue(TEXT("Glow"), Glow);
	UE_LOG(LogDmRing, Display, TEXT("DmRing: materials ready (glow %.2f)"), Glow);
	MatXRay->SetScalarParameterValue(TEXT("Glow"), Glow);
}

void ADmBrushRing::Hide()
{
	if (!bShown) return;
	bShown = false;
	Mesh->SetVisibility(false);
}

void ADmBrushRing::Show(const FVector& PlanetDir, float Ang, const FLinearColor& Color, bool bDashed, double CameraDistKm, const FVector* EyePlanetKm)
{
	bEye = EyePlanetKm && !EyePlanetKm->ContainsNaN();
	if (bEye) EyeKm = *EyePlanetKm;
	const FVector D = PlanetDir.GetSafeNormal();
	if (D.IsNearlyZero() || !(Ang > 0.f) || !bMaterials) { Hide(); return; }
	Centre = D;
	CurAng = Ang;
	CurColor = Color;
	bCurDashed = bDashed;
	CurCamKm = FMath::Max(CameraDistKm, 0.05);
	if (Mat) Mat->SetVectorParameterValue(TEXT("Color"), Color);
	if (MatXRay) MatXRay->SetVectorParameterValue(TEXT("Color"), Color);
	Build(!bCreated);
	PlaceActor();
	if (!bShown)
	{
		bShown = true;
		Mesh->SetVisibility(true);
	}
}

void ADmBrushRing::PlaceActor()
{
	const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const FTransform P2W = Sim ? Sim->GetPlanetToWorld() : FTransform::Identity;
	SetActorLocationAndRotation(P2W.TransformPosition(Centre * CentreR), P2W.GetRotation());
}

void ADmBrushRing::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bShown) PlaceActor();
}

void ADmBrushRing::Build(bool bCreate)
{
	const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const bool bWorld = Sim && Sim->Sim();
	int32 Hint = -1;
	auto SurfUU = [Sim, bWorld, &Hint](const FVector& Dir) -> double
	{
		const double R = bWorld ? Sim->SurfaceRadiusUU(Dir, Hint) : 0.0;
		return (FMath::IsFinite(R) && R > 1.0) ? R : DmCoords::RadiusUU;
	};

	constexpr int32 N = Segments;
	const double A = CurAng;
	FVector E, Nn;
	DmCoords::Basis(Centre, E, Nn);
	auto DirAt = [&](double Phi, double Ang)
	{
		const double SA = FMath::Sin(FMath::Max(Ang, 1e-5)), CA = FMath::Cos(FMath::Max(Ang, 1e-5));
		return (Centre * CA + (E * FMath::Cos(Phi) + Nn * FMath::Sin(Phi)) * SA).GetSafeNormal();
	};
	// half ribbon width (radians) and lift (uu) for a point DistKm from the eye: a constant share of the view
	auto HalfWAt = [](double Km) { return FMath::Max(0.0021 * Km * DmCoords::UUPerKm, 2500.0) / DmCoords::RadiusUU; };
	auto LiftAt = [](double Km) { return FMath::Max(0.0022 * Km * DmCoords::UUPerKm, 1500.0); };
	auto DistTo = [this](const FVector& PointUU) { return bEye ? FMath::Max(FVector::Dist(EyeKm, PointUU / DmCoords::UUPerKm), 0.05) : CurCamKm; };

	const double CentreSurf = SurfUU(Centre);
	const double DistC = DistTo(Centre * CentreSurf);
	CentreR = CentreSurf + LiftAt(DistC);

	// the aim reticle: ~20 px at the centre, faded in once the ring is more than ~12x its size (radius beyond
	// about a quarter of the screen height, where the ring itself stops telling where the power lands)
	const double Ar = 0.011 * DistC / DmCoords::RadiusKm;
	ReticleAlpha = float(FMath::SmoothStep(10.0, 16.0, A / FMath::Max(Ar, 1e-9)));

	// per ring station: the surface radius, the lift and half width from its own distance to the eye
	double Surf[N + 1], Rad[N + 1], HW[N + 1], SurfR[N + 1], RadR[N + 1];
	for (int32 I = 0; I <= N; ++I)
	{
		const double Phi = UE_TWO_PI * I / N;
		const FVector D = DirAt(Phi, A);
		Surf[I] = SurfUU(D);
		const double Dk = DistTo(D * Surf[I]);
		Rad[I] = Surf[I] + LiftAt(Dk);
		// a flat ribbon seen at a grazing angle is foreshortened across its width (the far arc of a big ring
		// near the ground): widen it by 1 / sin(angle between the line of sight and the ring's outward
		// direction), capped at 4x, so it keeps about the same width on screen all the way round
		double Fore = 1.0;
		if (bEye)
		{
			const FVector Out = (D - Centre * FVector::DotProduct(D, Centre)).GetSafeNormal();
			const FVector View = (D * Surf[I] / DmCoords::UUPerKm - EyeKm).GetSafeNormal();
			const double C = FVector::DotProduct(Out, View);
			Fore = 1.0 / FMath::Max(FMath::Sqrt(FMath::Max(1.0 - C * C, 0.0)), 0.25);
		}
		HW[I] = HalfWAt(Dk) * Fore;
		SurfR[I] = SurfUU(DirAt(Phi, Ar));
		RadR[I] = SurfR[I] + LiftAt(DistC);
	}
	const double HWC = HalfWAt(DistC);

	// five ribbons, drawn in this order: 0 a dark contrast band under the ring (readable on snow, cloud and
	// sunlit desert alike), 1 the soft halo inside it, 2 the bright core; 3 and 4 the reticle's dark band and core
	constexpr int32 Ribbons = 5;
	const int32 VertsPerSeg = 4 * Ribbons;
	if (bCreate || Verts.Num() != N * VertsPerSeg)
	{
		Verts.SetNumUninitialized(N * VertsPerSeg);
		Normals.SetNumUninitialized(N * VertsPerSeg);
		UVs.SetNumUninitialized(N * VertsPerSeg);
		Colors.SetNumUninitialized(N * VertsPerSeg);
		Tris.Reset(N * 6 * Ribbons);
		for (int32 Rb = 0; Rb < Ribbons; ++Rb)
			for (int32 S = 0; S < N; ++S)
			{
				const int32 B = (Rb * N + S) * 4;   // 0 inner-a, 1 outer-a, 2 inner-b, 3 outer-b
				Tris.Append({ B + 0, B + 2, B + 1, B + 1, B + 2, B + 3 });
			}
	}

	// vertex R = emissive mask (0 dark band, 1 lit), A = opacity; the colour itself is the material's
	const float DashA = bCurDashed ? 0.35f : 1.f;
	const FLinearColor Dark(0.f, 0.f, 0.f, 0.55f * DashA);
	const FLinearColor Halo(1.f, 1.f, 1.f, bCurDashed ? 0.08f : 0.20f);
	const FLinearColor Core(1.f, 1.f, 1.f, DashA);
	const FLinearColor RDark(0.f, 0.f, 0.f, 0.55f * DashA * ReticleAlpha);
	const FLinearColor RCore(1.f, 1.f, 1.f, DashA * ReticleAlpha);
	for (int32 S = 0; S < N; ++S)
	{
		const bool bGap = bCurDashed && ((S / DashEvery) % 2 == 1);
		const double P0 = UE_TWO_PI * S / N, P1 = UE_TWO_PI * (S + 1) / N;
		for (int32 Rb = 0; Rb < Ribbons; ++Rb)
		{
			const bool bRet = Rb >= 3;
			const double Ang = bRet ? Ar : A;
			// the ribbon's cross-section at both ends of the segment (its own width there)
			double In[2], Out[2];
			for (int32 K = 0; K < 2; ++K)
			{
				const double Hw = bRet ? 0.8 * HWC : HW[S + K];
				switch (Rb)
				{
				case 0: In[K] = A - 2.6 * Hw; Out[K] = A + 2.6 * Hw; break;
				case 1: In[K] = FMath::Max(A - 8.0 * Hw, A * 0.55); Out[K] = A - 0.5 * Hw; break;
				case 2: In[K] = A - Hw; Out[K] = A + Hw; break;
				case 3: In[K] = FMath::Max(Ang - 2.6 * Hw, 0.0); Out[K] = Ang + 2.6 * Hw; break;
				default: In[K] = FMath::Max(Ang - Hw, 0.0); Out[K] = Ang + Hw; break;
				}
			}
			// lifted radius at both ends; the halo and the dark bands sit a little lower (a share of the lift)
			const double Drop = Rb == 1 ? 0.3 : ((Rb == 0 || Rb == 3) ? 0.1 : 0.0);
			const double Ra = bRet ? RadR[S] - Drop * (RadR[S] - SurfR[S]) : Rad[S] - Drop * (Rad[S] - Surf[S]);
			const double Rb2 = bRet ? RadR[S + 1] - Drop * (RadR[S + 1] - SurfR[S + 1]) : Rad[S + 1] - Drop * (Rad[S + 1] - Surf[S + 1]);
			const FLinearColor Cv = Rb == 2 ? Core : (Rb == 0 ? Dark : (Rb == 1 ? Halo : (Rb == 3 ? RDark : RCore)));
			const FLinearColor C = bGap ? FLinearColor(Cv.R, Cv.G, Cv.B, 0.f) : Cv;
			const int32 B = (Rb * N + S) * 4;
			const FVector DA = DirAt(P0, Ang), DB = DirAt(P1, Ang);
			const FVector Pts[4] = { DirAt(P0, In[0]) * Ra, DirAt(P0, Out[0]) * Ra, DirAt(P1, In[1]) * Rb2, DirAt(P1, Out[1]) * Rb2 };
			const FVector Ups[4] = { DA, DA, DB, DB };
			const float U0 = float(S) / N, U1 = float(S + 1) / N;
			const FVector2D Uv[4] = { FVector2D(U0, 0), FVector2D(U0, 1), FVector2D(U1, 0), FVector2D(U1, 1) };
			for (int32 K = 0; K < 4; ++K)
			{
				Verts[B + K] = Pts[K] - Centre * CentreR;
				Normals[B + K] = Ups[K];
				UVs[B + K] = Uv[K];
				Colors[B + K] = C;
			}
		}
	}

	static const TArray<FProcMeshTangent> NoTangents;
	if (bCreate || !bCreated)
	{
		Mesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, Colors, NoTangents, false);
		Mesh->CreateMeshSection_LinearColor(1, Verts, Tris, Normals, UVs, Colors, NoTangents, false);
		Mesh->SetMaterial(0, Mat);
		Mesh->SetMaterial(1, MatXRay);
		bCreated = true;
	}
	else
	{
		Mesh->UpdateMeshSection_LinearColor(0, Verts, Normals, UVs, Colors, NoTangents);
		Mesh->UpdateMeshSection_LinearColor(1, Verts, Normals, UVs, Colors, NoTangents);
	}
}
