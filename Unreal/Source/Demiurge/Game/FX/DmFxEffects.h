// DEMIURGE — Game/FX/DmFxEffects.h : the effect recipes of the fx module. Owner: C6 fx.
//
// Each Make* returns a new effect (ADmFx::AddEffect takes ownership); each Burst* emits particles
// once. Scales follow SPEC-UNREAL 1: 1 km = 1e5 uu, planet radius 100 km, clouds 0.5 - 1.8 km above
// the sea, atmosphere about 10 km. Every recipe reads the world only (never writes it).
#pragma once

#include "CoreMinimal.h"

class ADmFx;
struct FDmFxEffect;

namespace DmFxFx
{
	constexpr double Km = 1.0e5;

	// wrath
	/** Comet / meteor in flight: follows the sim disaster's eta (DisasterId >= 0), or flies on its own
	 *  for Eta seconds and then plays the impact (previews: DisasterId = -1). */
	FDmFxEffect* MakeFlight(ADmFx& Fx, int32 DisasterId, int32 Cell, bool bComet, float Eta, float Flight);
	FDmFxEffect* MakeImpact(ADmFx& Fx, int32 Cell, double Ang, bool bComet, bool bSea);
	/** A lightning strike at a planet-frame ground point (bVisualOnly: storm decoration, no fire flare). */
	FDmFxEffect* MakeLightning(ADmFx& Fx, const FVector& Ground, float Strength, bool bVisualOnly);
	FDmFxEffect* MakeVolcano(ADmFx& Fx, int32 DisasterId, int32 Cell);
	FDmFxEffect* MakeTsunami(ADmFx& Fx, int32 DisasterId, int32 Cell);
	FDmFxEffect* MakeStorm(ADmFx& Fx, int32 DisasterId);
	FDmFxEffect* MakeDrought(ADmFx& Fx, int32 DisasterId);
	FDmFxEffect* MakeQuake(ADmFx& Fx, int32 Cell, double Ang, float Power);
	FDmFxEffect* MakeNuke(ADmFx& Fx, int32 Cell);
	void BurstPlague(ADmFx& Fx, int32 Cell, float Strength);

	// grace and sky
	/** Kind: 0 heal, 1 inspire, 2 prayer answered / blessing. */
	FDmFxEffect* MakeBlessing(ADmFx& Fx, int32 Cell, int32 Kind);
	FDmFxEffect* MakeProphet(ADmFx& Fx, int32 Cell);
	FDmFxEffect* MakePeace(ADmFx& Fx, int32 Cell, double Ang);
	/** Bloom (brush pulse) and seed-life waves: green light spreading over Ang. */
	FDmFxEffect* MakeGreenWave(ADmFx& Fx, int32 Cell, double Ang, float Strength, bool bSeed);
	FDmFxEffect* MakeRain(ADmFx& Fx, int32 Cell, double Ang, float Seconds);
	FDmFxEffect* MakeAgeRays(ADmFx& Fx, int32 Cell, const FLinearColor& Tint);
	FDmFxEffect* MakeArk(ADmFx& Fx, int32 Cell);

	// small bursts
	void BurstSculpt(ADmFx& Fx, int32 Cell, double Ang, bool bRaise);
	void BurstFounded(ADmFx& Fx, int32 Cell, bool bAwaken);
	void BurstBattle(ADmFx& Fx, int32 Cell, bool bWon);
	void BurstDestroyed(ADmFx& Fx, int32 Cell, int32 Cause);
	void BurstSplash(ADmFx& Fx, int32 Cell, float Height);
	void BurstFireStart(ADmFx& Fx, int32 Cell);
	void BurstWitness(ADmFx& Fx, int32 Cell, bool bGood);

	// ambient (called every frame by ADmFx with an emission accumulator)
	void AmbientFire(ADmFx& Fx, int32 Cell, float Fire, float Dt, float& Acc);
	void AmbientMiasma(ADmFx& Fx, int32 Cell, float Plague, float Dt, float& Acc);
	void AmbientPrayer(ADmFx& Fx, int32 Cell, int32 Kind, uint8 Seed);
	void AmbientProphet(ADmFx& Fx, const FVector& Pos, float Dt, float& Acc, bool bWalking);
	void AmbientBeacon(ADmFx& Fx, int32 Cell);
	void AmbientAurora(ADmFx& Fx, float Strength);
}
