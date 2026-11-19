// DEMIURGE — Game/Input/DmBrushRing.h : the brush ring (old SPEC-UI 2 "Ring and feedback", RENDER uBrush).
// Owner: C3 camera-input.
//
// A thin glowing ribbon draped on the sim surface (sea level on sea, land + lakes elsewhere) around a
// planet-frame direction, with a soft halo inside it and a dark contrast band under it; its width and lift
// scale with the distance from the eye to each point of the ring (given the eye; else to the centre), so the
// far arc of a ring seen at a grazing angle near the ground stays as wide on screen as the near arc. When the
// ring is much larger than the view (a 6 km brush seen from 1.2 km; radius over ~12x the reticle), a small aim reticle (~20 px) marks the
// centre under the pointer, so the player always sees where the power lands. Invalid targets draw it dashed at
// alpha 0.35. Two sections share the geometry: one depth-tested (M_BrushRing), one that ignores depth
// at low opacity (M_BrushRingXRay), so the arc behind a ridge still shows faintly. Fixed topology
// (dashes are alpha), updated in place: no allocation while the pointer moves. Materials come from
// Scripts/camera-input.py (/Game/CameraInput). Follows UDmSimSubsystem::GetPlanetToWorld().
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DmBrushRing.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class DEMIURGE_API ADmBrushRing : public AActor
{
	GENERATED_BODY()

public:
	ADmBrushRing();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Show the ring around PlanetDir (unit, planet frame) with angular radius Ang (radians). */
	void Show(const FVector& PlanetDir, float Ang, const FLinearColor& Color, bool bDashed, double CameraDistKm, const FVector* EyePlanetKm = nullptr);
	void Hide();
	bool IsShown() const { return bShown; }
	FVector GetCentre() const { return Centre; }
	float GetAng() const { return CurAng; }
	bool IsDashed() const { return bCurDashed; }
	FLinearColor GetColor() const { return CurColor; }
	bool HasMaterials() const { return bMaterials; }
	/** 0..1: how visible the centre reticle is (0 when the ring itself is small enough to read). */
	float GetReticleAlpha() const { return ReticleAlpha; }

	static constexpr int32 Segments = 128;
	static constexpr int32 DashEvery = 4;     // segments per dash and per gap
	/** Emissive scale of the ring materials (manual exposure: 1 = mid grey). */
	float Glow = 1.4f;

	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Mesh;

private:
	void Build(bool bCreate);
	void PlaceActor();

	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Mat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatXRay;

	bool bShown = false, bMaterials = false, bCreated = false;
	FVector Centre = FVector(1, 0, 0);
	double CentreR = 1.0e7;               // uu
	float CurAng = 0.06f;
	FLinearColor CurColor = FLinearColor::White;
	bool bCurDashed = false;
	double CurCamKm = 100.0;
	FVector EyeKm = FVector::ZeroVector;  // planet frame
	bool bEye = false;
	float ReticleAlpha = 0.f;

	TArray<FVector> Verts;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<int32> Tris;
};
