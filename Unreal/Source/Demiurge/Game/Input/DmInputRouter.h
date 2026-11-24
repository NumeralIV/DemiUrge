// DEMIURGE — Game/Input/DmInputRouter.h : every player input (old SPEC-UI 2, DM.Input). Owner: C3 camera-input.
//
// A component of ADmPawn. Creates its Enhanced Input actions and one mapping context in C++
// (NewObject, no assets), binds them on the pawn's UEnhancedInputComponent and adds the context to
// the local player. The pawn calls ProcessFrame(dt) once per frame, after the controller has run the
// input stack and before the camera rig updates.
//
// Bindings (old SPEC-UI 2; Kbd() lists them as data for the help screen):
//   W A S D / arrows   orbit                 Q / E               heading
//   + / - , R / F      zoom                  mouse wheel         zoom toward the pointer
//   Space              pause                 1 2 3 4 / 0         speed 1x 2x 4x 8x / linger 0.25x
//   Tab (Shift+Tab)    cycle overlays        H                   hide the HUD
//   [ / ]              brush x0.8 / x1.25    Esc                 disarm -> deselect -> pause menu
//   each power's dm::powers::kPowers[].key   arm it (pressing it again disarms); Input defines no letters of its own
// Pointer:
//   no power armed:  left-drag orbit (the grabbed ground follows the pointer, inertia on release),
//                    wheel / pinch zoom toward the pointer, right-drag horizontally or two-finger
//                    twist = heading, click / tap a settlement = select, click empty ground = deselect,
//                    double-click / double-tap = fly closer, touch long-press (500 ms, < 10 px) = hover tip.
//   click power:     press + release within 8 px (mouse) / 14 px (touch) uses it; dragging further orbits.
//   brush power:     applied every frame while the primary button / one finger is held (touch starts
//                    100 ms after touch-down and only while no second finger is down; a second finger
//                    ends the stroke and the gesture becomes orbit / pinch / twist until all lift).
//   right-drag, middle-drag and two fingers always orbit while a power is armed; right-click without
//   dragging disarms.
//   Rejected uses toast 'Choose open sea' / 'Choose land' / 'Choose a settlement' / 'Nothing can live
//   here yet' / 'Too close to another people' (UDmControlSubsystem::RejectText).
// Hover: UDmControlSubsystem::GetHover() (cell, direction, settlement, powers::can), picked at <= 10 Hz,
// every frame while a power is armed (the ring follows the pointer) or a stroke is held.
// Ring: ADmBrushRing at the hover point, radius UDmControlSubsystem::GetRingAng(), coloured by power
// group, dashed (alpha 0.35) when can() is not ok; settlement powers ring the settlement instead.
// Input is ignored while UDmControlSubsystem::IsInputBlocked() (modal screens); keys also while a text
// field has keyboard focus. Slate widgets (HUD panels) consume their own clicks before the viewport.
//
// Tests: SetVirtualPointer() replaces the OS cursor (offscreen runs have none); keys and buttons are
// injected through APlayerController::InputKey and touches through InputTouch, so the whole chain
// (key -> mapping context -> action -> handler) is exercised. ActionCounts counts discrete actions.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "Game/Core/DmPicking.h"
#include "Game/Input/DmControl.h"
#include "DmInputRouter.generated.h"

class ADmBrushRing;
class ADmPawn;
class APlayerController;
class UEnhancedInputComponent;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/** One key of the binding table (data for tests and the help screen). */
struct FDmKeyBinding
{
	FName Action;
	FKey Key;
	FVector2D Value;          // the action value the key produces (x for 1D)
};

UCLASS()
class DEMIURGE_API UDmInputRouter : public UActorComponent
{
	GENERATED_BODY()

public:
	UDmInputRouter();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	void BindInput(UEnhancedInputComponent* EIC);
	void AddMappingContext(APlayerController* PC);
	/** Once per frame (the pawn calls it before the rig update). */
	void ProcessFrame(float Dt);

	// ------------------------------------------------------------ tests
	void SetVirtualPointer(const FVector2D& Px) { VirtualPointer = Px; bVirtualPointer = true; }
	void ClearVirtualPointer() { bVirtualPointer = false; }
	bool GetPointer(FVector2D& Out) const;
	UInputAction* FindAction(FName Name) const;
	UInputMappingContext* GetMappingContext() const { return Context; }
	/** Every key mapping (action, key, value). */
	const TArray<FDmKeyBinding>& Kbd() const { return KeyTable; }
	TMap<FName, int32> ActionCounts;
	bool IsStroking() const { return bStroke; }
	bool IsDragging() const;
	int32 TouchFingers() const { return TouchDown; }
	bool IsTouchGesture() const { return TMode == ETouchMode::Multi; }
	ADmBrushRing* GetRing() const { return Ring; }
	int32 StrokeCalls = 0, ClickCount = 0, DoubleClicks = 0, Picks = 0;

	// ------------------------------------------------------------ tuning
	float ClickSlopMouse = 8.f;
	float ClickSlopTouch = 14.f;
	float DoubleClickSeconds = 0.35f;
	float DoubleClickSlopMouse = 10.f, DoubleClickSlopTouch = 30.f;
	float LongPressSeconds = 0.5f, LongPressSlop = 10.f;
	float TouchBrushDelay = 0.1f;
	float WheelStep = 0.06f;           // zoom per wheel notch
	float PinchZoom = 0.35f;           // zoom per doubling of the finger distance
	float KeyPanRate = 0.75f;          // screen heights per second (the rig eases in and out)
	float KeyPanFast = 2.5f;           // Shift held: pan this much faster
	float EdgePanMarginPx = 6.f;       // pointer within this many pixels of a screen edge pans
	float EdgePanRate = 0.7f;          // screen heights per second at the very edge
	float EdgePanDwell = 0.18f;        // seconds at the edge before panning starts (crossing out of a window does not pan)
	float KeyTurnRate = 1.4f;          // rad per second
	float KeyZoomRate = 0.45f;         // zoom per second
	float RightTurnPerScreen = 3.14159f;   // heading change for a right-drag across the whole width
	float FlyCloserZoom = 0.25f;
	float HoverInterval = 0.1f;        // <= 10 Hz when nothing is armed

private:
	enum class ETouchMode : uint8 { None, Single, Multi };
	struct FButton
	{
		bool bDown = false, bPressedEvt = false, bReleasedEvt = false, bDragged = false;
		FVector2D PressPos = FVector2D::ZeroVector, LastPos = FVector2D::ZeroVector;
		double PressTime = 0.0;
	};

	void CreateActions();
	void OnDiscrete(FName Name);
	void OnButton(int32 Index, bool bDown);
	void OnWheel(const FInputActionValue& Value);
	bool KeysBlocked() const;
	double Now() const;
	ADmPawn* Pawn() const;
	APlayerController* PC() const;
	UDmControlSubsystem* Control() const;

	void DoHover(const FVector2D& Px, bool bTip);
	int32 SettlementAt(const FDmPick& Pk) const;
	int32 PickCell(const FVector2D& Px, FVector* OutDir = nullptr, int32* OutSettlement = nullptr);
	int32 TargetFor(int32 Cell, int32 Settlement) const;
	void Click(const FVector2D& Px, bool bTouch);
	void StrokeStep(const FVector2D& Px, float Dt, bool bFirst);
	void ProcessMouse(const FVector2D& Ptr, bool bHave, float Dt);
	bool ProcessTouch(float Dt);
	void ProcessKeys(float Dt);
	void ProcessEdgePan(const FVector2D& Ptr, bool bHave, float Dt);
	void ApplySettingsArgs();
	void UpdateRing();
	void CancelAll();

	UPROPERTY() TObjectPtr<UInputMappingContext> Context;
	UPROPERTY() TMap<FName, TObjectPtr<UInputAction>> Actions;
	UPROPERTY() TObjectPtr<ADmBrushRing> Ring;
	TArray<FDmKeyBinding> KeyTable;
	TArray<FName> PowerActionIds;    // "Power.<id>" -> id lookup via the name

	bool bVirtualPointer = false;
	FVector2D VirtualPointer = FVector2D::ZeroVector;
	FButton Btn[3];                  // 0 primary, 1 secondary, 2 middle
	float WheelAccum = 0.f;
	double EdgeTime = 0.0;           // seconds the pointer has rested in the edge band
	bool bSettingsArgs = false;
public:
	/** Edge pan velocity requested this frame (screen heights / s; tests). */
	FVector2D LastEdgePan = FVector2D::ZeroVector;
private:
	bool bStroke = false;
	double LastClickTime = -1e9;
	FVector2D LastClickPos = FVector2D::ZeroVector;
	double LastHoverTime = -1e9;
	bool bBound = false;

	// touch
	ETouchMode TMode = ETouchMode::None;
	int32 TouchDown = 0;
	bool bFinger[2] = { false, false };
	FVector2D FingerPos[2];
	FVector2D TPress = FVector2D::ZeroVector, TLast = FVector2D::ZeroVector;
	double TPressTime = 0.0;
	bool bTDragged = false, bLongDone = false, bTouchStroke = false;
	double PinchDist = 0.0, PinchAng = 0.0;
	FVector2D PinchMid = FVector2D::ZeroVector;
};
