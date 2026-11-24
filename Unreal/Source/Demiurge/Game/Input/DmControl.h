// DEMIURGE — Game/Input/DmControl.h : the player's control state (old SPEC-UI 0 "Game" fields that input,
// HUD and screens share): armed power, brush radius, overlay, HUD hidden, selection pass-through,
// power use with rejection toasts, input gates, the hover pick, and the interface events.
// Owner: C3 camera-input.
//
//   UDmControlSubsystem* C = UDmControlSubsystem::Get(this);
//   C->ArmPower(TEXT("rain"));          // Game.armPower: arming the armed power disarms; locked powers are refused
//   C->SetBrush(0.08f);                 // Game.setBrush (radians, 0.03 .. 0.15, default 0.06)
//   C->SetOverlay(1);                   // Game.setOverlay 0 none, 1 cultures, 2 temperature, 3 rainfall, 4 devotion, 5 habitability
//   C->HideHud(true);                   // Game.hideHud
//   C->Select(SettlementId);            // Game.select (-1 = nothing) -> UDmSimSubsystem::SelectSettlement / ClearSelection
//   C->UsePower(Cell, Dt);              // Game.usePower (the input router calls it; Dt = 0 on press)
//   C->FlyToCell(Cell, Zoom);           // Game.flyToCell (camera pawn)
//   C->SetInputBlocked(TEXT("screen"), true);   // screens: input ignored while a modal screen is open
//   const FDmHover& H = C->GetHover();  // Input.hover: what is under the pointer (cell, settlement, can, tip)
//
// Interface events go out on UDmSimSubsystem::OnSimEvent as dm::Event (the same delegate that carries
// the sim's events, like the old DM.Bus): ui:power { id } ("" = disarmed), ui:overlay { mode } (the
// mode number as text, "0" .. "5"), ui:hud { hidden }, ui:toast { text, kind } (kind info | good | bad |
// epoch), camera:shake { amount }. ui:select and ui:speed come from UDmSimSubsystem itself. Native
// delegates below carry the same changes for C++ listeners that prefer them.
//
// Game rules kept here (old SPEC-UI 0): on phase Water / Life / People the genesis power comet / seed /
// awaken is armed, and History disarms; during People the overlay is 5 (habitability) and goes back to
// 0 afterwards; awaken disarms after it succeeds; people:lost arms awaken. bGameRules = false turns
// them off (the integration engineer may move them into the game flow).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Sim/Powers.h"
#include "DmControl.generated.h"

class ADmPawn;
class UDmSimSubsystem;
namespace dm { struct Event; struct World; }

/** What is under the pointer (old Input.hover). */
struct FDmHover
{
	bool bHit = false;               // the pointer is over the planet
	int32 Cell = -1;
	FVector PlanetDir = FVector::ZeroVector;   // unit, planet frame
	FVector WorldPos = FVector::ZeroVector;
	int32 Settlement = -1;           // settlement id under the pointer or -1
	bool bArmed = false;             // a power is armed: Can is meaningful
	dm::powers::CanResult Can;       // powers::can for the armed power at Cell (or the settlement's cell)
	int32 TargetCell = -1;           // the cell a click / brush would use (a settlement power snaps to the settlement)
	bool bTip = false;               // touch long-press: show the hover tip now
	FVector2D ScreenPos = FVector2D::ZeroVector;
	double Time = 0.0;               // world seconds of the pick
};

DECLARE_MULTICAST_DELEGATE_OneParam(FDmOnArmed, const FString& /*PowerId, empty = disarmed*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FDmOnOverlay, int32 /*Mode*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FDmOnHud, bool /*bHidden*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDmOnToast, const FString& /*Text*/, const FString& /*Kind*/);
DECLARE_MULTICAST_DELEGATE(FDmOnMenu);
DECLARE_MULTICAST_DELEGATE_OneParam(FDmOnBrush, float /*Ang*/);

UCLASS()
class DEMIURGE_API UDmControlSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDmControlSubsystem* Get(const UObject* WorldContext);

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	// ------------------------------------------------------------ powers (Game.power)
	/** Arm a power by id. The armed one again, or "", disarms. Unknown or locked ids are refused
	 *  (toast 'Not yet within your power'). Returns true if a power is armed afterwards. */
	bool ArmPower(const FString& Id);
	void Disarm() { ArmPower(FString()); }
	const FString& GetArmedPower() const { return Armed; }
	bool IsArmed() const { return !Armed.IsEmpty(); }
	const dm::powers::PowerDef* GetArmedDef() const;
	bool IsBrushArmed() const;
	/** Ring radius (radians) for the armed power: the brush radius for brush powers and seed, the
	 *  table's ang otherwise; 0 for settlement-targeted powers (they highlight the settlement). */
	float GetRingAng() const;

	// ------------------------------------------------------------ brush (Game.brushAng)
	float GetBrushAng() const { return BrushAng; }
	void SetBrush(float Ang);
	static constexpr float BrushMin = 0.03f;
	static constexpr float BrushMax = 0.15f;
	static constexpr float BrushDefault = 0.06f;

	// ------------------------------------------------------------ overlay, HUD
	int32 GetOverlay() const { return Overlay; }
	void SetOverlay(int32 Mode);
	void CycleOverlay(int32 Dir = 1);
	static const TCHAR* OverlayName(int32 Mode);
	static constexpr int32 OverlayCount = 6;
	bool IsHudHidden() const { return bHudHidden; }
	void HideHud(bool bHidden);

	// ------------------------------------------------------------ selection, powers, camera
	void Select(int32 SettlementId);
	int32 GetSelected() const;
	/** powers::can for the armed power (Reason Locked when nothing is armed or no world). */
	dm::powers::CanResult Can(int32 Cell) const;
	/** Apply the armed power at a cell (Game.usePower). Click powers: Dt = 0. Brush powers: every frame
	 *  while held, Dt = 0 on the first call of a stroke. Rejections (target) toast once per stroke. */
	bool UsePower(int32 Cell, float Dt, bool bNewStroke);
	/** Plain copy for a rejection (old SPEC-UI 2), empty when there is nothing to say. */
	static FString RejectText(const dm::powers::CanResult& R, const dm::powers::PowerDef* Def);
	void FlyToCell(int32 Cell, float Zoom = -1.f, float Seconds = 1.6f);
	void FlyToDir(const FVector& PlanetDir, float Zoom = -1.f, float Seconds = 1.6f);
	/** Esc: disarm -> deselect -> pause menu. Returns what it did: 1 disarmed, 2 deselected, 3 menu. */
	int32 Back();

	// ------------------------------------------------------------ events
	void Toast(const FString& Text, const FString& Kind = TEXT("info"));
	/** Emit camera:shake (the pawn shakes on it, as do the FX). */
	void RequestShake(float Amount);
	void RequestMenu();

	// ------------------------------------------------------------ input gates
	/** Input (pointer and keys) is ignored while any reason is set ("screen": a modal screen is open). */
	void SetInputBlocked(FName Reason, bool bBlocked);
	bool IsInputBlocked() const { return BlockReasons.Num() > 0; }
	/** Keys are also ignored while a text field has keyboard focus. */
	static bool IsTextFieldFocused();

	// ------------------------------------------------------------ hover (written by the input router)
	const FDmHover& GetHover() const { return Hover; }
	void SetHover(const FDmHover& H) { Hover = H; }

	// ------------------------------------------------------------ camera pawn (registered by ADmPawn)
	void RegisterPawn(ADmPawn* Pawn);
	ADmPawn* GetPawn() const;

	bool bGameRules = true;
	/** The 'shake' setting (0..1), applied by the pawn. */
	float ShakeSetting = 1.f;
	/** Camera settings (wave 4b; read by the input router every frame; a settings screen may write them).
	 *  Command-line overrides at start: -edgepan=0|1 -zoomspeed=<x> -panspeed=<x> -invertzoom=1. */
	bool bEdgePan = true;            // pan when the pointer rests at a screen edge (needs the window focused)
	float ZoomSpeedSetting = 1.f;    // scales wheel / pinch / key zoom steps (0.25..4)
	float PanSpeedSetting = 1.f;     // scales keyboard and edge pan (0.25..4)
	bool bInvertZoom = false;        // wheel up zooms OUT

	FDmOnArmed OnArmed;
	FDmOnOverlay OnOverlay;
	FDmOnHud OnHud;
	FDmOnToast OnToast;
	FDmOnMenu OnMenuRequested;
	FDmOnBrush OnBrush;

	// counters for tests and logs
	int32 ToastCount = 0;
	FString LastToast, LastToastKind;
	int32 MenuRequests = 0;

private:
	void Emit(const dm::Event& E);
	void OnSimEvent(const dm::Event& E);
	UDmSimSubsystem* Sim() const;

	FString Armed;
	float BrushAng = BrushDefault;
	int32 Overlay = 0;
	bool bHudHidden = false;
	bool bOverlayForced = false;     // overlay 5 set by the People phase
	TSet<FName> BlockReasons;
	FDmHover Hover;
	TWeakObjectPtr<ADmPawn> PawnRef;
	FDelegateHandle SimHandle;
	bool bStrokeToasted = false;
	bool bInEmit = false;
};
