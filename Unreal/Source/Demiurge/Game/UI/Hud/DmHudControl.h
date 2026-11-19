// DEMIURGE — Game/UI/Hud/DmHudControl.h : the HUD's door to the player's control state. Owner: C7 hud.
//
// The control state (armed power, brush, overlay, HUD hidden, selection, fly-to, toasts, the hover
// pick: old SPEC-UI 0 "Game" fields) belongs to C3 camera-input (UDmControlSubsystem, Game/Input/
// DmControl.h). The HUD reads and changes it ONLY through this class, so it builds with or without
// that module:
//   - when Game/Input/DmControl.h is in the build (compile-time __has_include), every call forwards to
//     UDmControlSubsystem;
//   - otherwise a local stand-in keeps the same state (and emits the same ui:* events on the sim bus),
//     so the HUD works and can be tested alone. A log line says which one is in use; if the stand-in is
//     used while a UDmControlSubsystem class exists at run time, it warns: DmHudControl.cpp was compiled
//     before camera-input arrived and must be rebuilt.
// Game actions that old SPEC-UI 0 gave to DM.Game and nobody owns in Unreal yet (Game.nextPhase,
// Game.setEdict) are here too (NextPhase, SetEdict) so the integration engineer can reroute them in one
// place; today they call dm::sim::genesis::next / dm::society::setEdict on the subsystem's world.
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "Sim/Powers.h"
#include "Sim/Schema.h"

class UDmSimSubsystem;
class UWorld;
class APlayerController;

/** What is under the pointer, as the HUD needs it. */
struct FDmHudHover
{
	bool bHit = false;
	int32 Cell = -1;
	int32 Settlement = -1;
	FVector2D ScreenPx = FVector2D::ZeroVector;    // viewport pixels
	bool bArmed = false;
	dm::powers::CanResult Can;
	bool bForced = false;                          // set by a scenario
};

class DEMIURGE_API FDmHudControl
{
public:
	void Bind(UWorld* InWorld);
	/** True when C3's UDmControlSubsystem drives the state (false = local stand-in). */
	static bool HasControlModule();
	FString Describe() const;

	// powers
	FString Armed() const;
	const dm::powers::PowerDef* ArmedDef() const;
	/** Game.armPower: the armed one again disarms. Locked / unknown ids are refused (a toast). */
	bool ArmPower(const FString& Id);
	void Disarm();
	float BrushAng() const;
	void SetBrush(float Ang);
	void ScaleBrush(float K) { SetBrush(BrushAng() * K); }

	// overlay and HUD
	int32 Overlay() const;
	void SetOverlay(int32 Mode);
	bool HudHidden() const;
	void HideHud(bool bHidden);

	// selection, camera, menu
	void Select(int32 SettlementId);
	int32 Selected() const;
	void FlyToCell(int32 Cell, float Zoom = -1.f);
	void RequestMenu();
	int32 MenuRequests() const { return LocalMenuRequests; }

	// time
	void SetSpeed(float Speed);
	void SetPaused(bool bPaused);

	// messages (every toast arrives at the HUD as a ui:toast event)
	void Toast(const FString& Text, const FString& Kind = TEXT("info"));

	// game actions without an owner yet (see the header comment)
	bool NextPhase();
	bool SetEdict(int32 CultureId, dm::Edict Edict);

	// hover
	bool GetHover(FDmHudHover& Out) const;
	void ForceHover(int32 Cell, const FVector2D& ScreenPx);
	void ClearForcedHover() { bForced = false; }

	UDmSimSubsystem* Sim() const;

private:
	TWeakObjectPtr<UWorld> World;

	// local stand-in state (used only without the control module)
	FString LocalArmed;
	float LocalBrush = 0.06f;
	int32 LocalOverlay = 0;
	bool bLocalHidden = false;
	int32 LocalMenuRequests = 0;

	bool bForced = false;
	int32 ForcedCell = -1;
	FVector2D ForcedPx = FVector2D::ZeroVector;
};
