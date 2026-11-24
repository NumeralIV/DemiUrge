// DEMIURGE — Game/Input/DmInputRouter.cpp : see DmInputRouter.h. Owner: C3 camera-input.

#include "Game/Input/DmInputRouter.h"
#include "Game/Camera/DmPawn.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Game/Input/DmBrushRing.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Misc/App.h"
#include "Game/Core/DmScenario.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmInput, Log, All);

namespace
{
	const FName NOrbit(TEXT("Orbit")), NTurn(TEXT("Turn")), NZoom(TEXT("Zoom")), NWheel(TEXT("Wheel"));
	const FName NPrimary(TEXT("Primary")), NSecondary(TEXT("Secondary")), NMiddle(TEXT("Middle"));

	FLinearColor GroupColor(dm::powers::Group G)
	{
		switch (G)
		{
		case dm::powers::Group::Genesis: return FLinearColor(0.20f, 0.85f, 1.00f);
		case dm::powers::Group::Shape: return FLinearColor(1.00f, 0.52f, 0.10f);
		case dm::powers::Group::Sky: return FLinearColor(0.10f, 0.45f, 1.00f);
		case dm::powers::Group::Wrath: return FLinearColor(1.00f, 0.10f, 0.03f);
		default: return FLinearColor(1.00f, 0.78f, 0.22f);   // Grace
		}
	}
}

UDmInputRouter::UDmInputRouter()
{
	PrimaryComponentTick.bCanEverTick = false;   // the pawn drives ProcessFrame
}

void UDmInputRouter::BeginPlay()
{
	Super::BeginPlay();
	CreateActions();
	if (UWorld* W = GetWorld())
	{
		FActorSpawnParameters P;
		P.Owner = GetOwner();
		Ring = W->SpawnActor<ADmBrushRing>(FVector::ZeroVector, FRotator::ZeroRotator, P);
	}
}

void UDmInputRouter::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Ring) Ring->Destroy();
	Ring = nullptr;
	Super::EndPlay(Reason);
}

ADmPawn* UDmInputRouter::Pawn() const { return Cast<ADmPawn>(GetOwner()); }
APlayerController* UDmInputRouter::PC() const { const ADmPawn* P = Pawn(); return P ? Cast<APlayerController>(P->GetController()) : nullptr; }
UDmControlSubsystem* UDmInputRouter::Control() const { return UDmControlSubsystem::Get(this); }
double UDmInputRouter::Now() const { const UWorld* W = GetWorld(); return W ? W->GetRealTimeSeconds() : 0.0; }

UInputAction* UDmInputRouter::FindAction(FName Name) const
{
	const TObjectPtr<UInputAction>* A = Actions.Find(Name);
	return A ? A->Get() : nullptr;
}

// ------------------------------------------------------------------ actions and mapping context

void UDmInputRouter::CreateActions()
{
	if (Context) return;
	Context = NewObject<UInputMappingContext>(this, TEXT("IMC_DmPlayer"));
	KeyTable.Reset();

	auto Make = [this](FName Name, EInputActionValueType Type) -> UInputAction*
	{
		UInputAction* A = NewObject<UInputAction>(this, FName(*(TEXT("IA_") + Name.ToString().Replace(TEXT("."), TEXT("_")))));
		A->ValueType = Type;
		Actions.Add(Name, A);
		return A;
	};
	// Map a key; Swizzle puts a 1D key value on Y, Negate flips it.
	auto Map = [this](UInputAction* A, FName Name, const FKey& Key, bool bSwizzle = false, bool bNegate = false)
	{
		FEnhancedActionKeyMapping& M = Context->MapKey(A, Key);
		if (bSwizzle) M.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Context));
		if (bNegate) M.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
		const double S = bNegate ? -1.0 : 1.0;
		KeyTable.Add({ Name, Key, bSwizzle ? FVector2D(0.0, S) : FVector2D(S, 0.0) });
	};

	UInputAction* Orbit = Make(NOrbit, EInputActionValueType::Axis2D);
	Map(Orbit, NOrbit, EKeys::W, true);              Map(Orbit, NOrbit, EKeys::S, true, true);
	Map(Orbit, NOrbit, EKeys::D);                    Map(Orbit, NOrbit, EKeys::A, false, true);
	Map(Orbit, NOrbit, EKeys::Up, true);             Map(Orbit, NOrbit, EKeys::Down, true, true);
	Map(Orbit, NOrbit, EKeys::Right);                Map(Orbit, NOrbit, EKeys::Left, false, true);

	UInputAction* Turn = Make(NTurn, EInputActionValueType::Axis1D);
	Map(Turn, NTurn, EKeys::E);                      Map(Turn, NTurn, EKeys::Q, false, true);

	UInputAction* Zoom = Make(NZoom, EInputActionValueType::Axis1D);
	Map(Zoom, NZoom, EKeys::R);                      Map(Zoom, NZoom, EKeys::F, false, true);
	Map(Zoom, NZoom, EKeys::Equals);                 Map(Zoom, NZoom, EKeys::Hyphen, false, true);
	Map(Zoom, NZoom, EKeys::Add);                    Map(Zoom, NZoom, EKeys::Subtract, false, true);

	UInputAction* Wheel = Make(NWheel, EInputActionValueType::Axis1D);
	Map(Wheel, NWheel, EKeys::MouseWheelAxis);

	struct FDiscrete { const TCHAR* Name; FKey Key; };
	const FDiscrete Discrete[] = {
		{ TEXT("Pause"), EKeys::SpaceBar },
		{ TEXT("Speed1"), EKeys::One }, { TEXT("Speed2"), EKeys::Two }, { TEXT("Speed4"), EKeys::Three }, { TEXT("Speed8"), EKeys::Four },
		{ TEXT("Linger"), EKeys::Zero },
		{ TEXT("Overlay"), EKeys::Tab },
		{ TEXT("Hud"), EKeys::H },
		{ TEXT("BrushDown"), EKeys::LeftBracket }, { TEXT("BrushUp"), EKeys::RightBracket },
		{ TEXT("Back"), EKeys::Escape },
	};
	for (const FDiscrete& D : Discrete)
	{
		UInputAction* A = Make(D.Name, EInputActionValueType::Boolean);
		Map(A, D.Name, D.Key);
	}
	// each power's own hotkey (Powers.byId[id].key); Input defines no letters for powers
	for (const dm::powers::PowerDef& P : dm::powers::kPowers)
	{
		const FName Name(*FString::Printf(TEXT("Power.%s"), UTF8_TO_TCHAR(P.id)));
		const FKey Key(FName(*FString::Chr(TCHAR(P.key))));
		UInputAction* A = Make(Name, EInputActionValueType::Boolean);
		Map(A, Name, Key);
	}
	UInputAction* Prim = Make(NPrimary, EInputActionValueType::Boolean);
	Map(Prim, NPrimary, EKeys::LeftMouseButton);
	UInputAction* Sec = Make(NSecondary, EInputActionValueType::Boolean);
	Map(Sec, NSecondary, EKeys::RightMouseButton);
	UInputAction* Mid = Make(NMiddle, EInputActionValueType::Boolean);
	Map(Mid, NMiddle, EKeys::MiddleMouseButton);

	// a key must drive one action only
	TMap<FKey, FName> Seen;
	for (const FDmKeyBinding& B : KeyTable)
	{
		if (const FName* Other = Seen.Find(B.Key))
		{
			if (*Other != B.Action) UE_LOG(LogDmInput, Error, TEXT("DmInput: key %s bound to %s and %s"), *B.Key.ToString(), *Other->ToString(), *B.Action.ToString());
		}
		else Seen.Add(B.Key, B.Action);
	}
	UE_LOG(LogDmInput, Display, TEXT("DmInput: %d actions, %d key mappings"), Actions.Num(), KeyTable.Num());
}

void UDmInputRouter::BindInput(UEnhancedInputComponent* EIC)
{
	if (!EIC) { UE_LOG(LogDmInput, Error, TEXT("DmInput: the pawn's input component is not an UEnhancedInputComponent")); return; }
	CreateActions();
	for (const TPair<FName, TObjectPtr<UInputAction>>& It : Actions)
	{
		const FName Name = It.Key;
		UInputAction* A = It.Value;
		if (Name == NOrbit || Name == NTurn || Name == NZoom) { EIC->BindActionValue(A); continue; }
		if (Name == NWheel) { EIC->BindAction(A, ETriggerEvent::Triggered, this, &UDmInputRouter::OnWheel); continue; }
		if (Name == NPrimary || Name == NSecondary || Name == NMiddle)
		{
			const int32 Index = Name == NPrimary ? 0 : (Name == NSecondary ? 1 : 2);
			EIC->BindAction(A, ETriggerEvent::Started, this, &UDmInputRouter::OnButton, Index, true);
			EIC->BindAction(A, ETriggerEvent::Completed, this, &UDmInputRouter::OnButton, Index, false);
			continue;
		}
		EIC->BindAction(A, ETriggerEvent::Started, this, &UDmInputRouter::OnDiscrete, Name);
	}
	bBound = true;
}

void UDmInputRouter::AddMappingContext(APlayerController* InPC)
{
	CreateActions();
	if (!InPC) return;
	if (ULocalPlayer* LP = InPC->GetLocalPlayer())
		if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
		{
			Sub->AddMappingContext(Context, 0);
			UE_LOG(LogDmInput, Display, TEXT("DmInput: mapping context added"));
			return;
		}
	UE_LOG(LogDmInput, Error, TEXT("DmInput: no Enhanced Input local player subsystem"));
}

bool UDmInputRouter::KeysBlocked() const
{
	const UDmControlSubsystem* C = Control();
	return (C && C->IsInputBlocked()) || UDmControlSubsystem::IsTextFieldFocused();
}

// ------------------------------------------------------------------ handlers

void UDmInputRouter::OnDiscrete(FName Name)
{
	if (KeysBlocked()) return;
	ActionCounts.FindOrAdd(Name)++;
	UE_LOG(LogDmInput, Display, TEXT("DmInput: action %s"), *Name.ToString());
	UDmControlSubsystem* C = Control();
	UDmSimSubsystem* S = UDmSimSubsystem::Get(this);
	if (!C || !S) return;
	const FString N = Name.ToString();
	if (N == TEXT("Pause")) S->TogglePause();
	else if (N.StartsWith(TEXT("Speed")) || N == TEXT("Linger"))
	{
		const float Sp = N == TEXT("Linger") ? 0.25f : float(FCString::Atoi(*N.Mid(5)));
		S->SetSpeed(Sp);
		S->SetPaused(false);
	}
	else if (N == TEXT("Overlay"))
	{
		const APlayerController* P = PC();
		const bool bShift = P && (P->IsInputKeyDown(EKeys::LeftShift) || P->IsInputKeyDown(EKeys::RightShift));
		C->CycleOverlay(bShift ? -1 : 1);
	}
	else if (N == TEXT("Hud")) C->HideHud(!C->IsHudHidden());
	else if (N == TEXT("BrushDown")) C->SetBrush(C->GetBrushAng() * 0.8f);
	else if (N == TEXT("BrushUp")) C->SetBrush(C->GetBrushAng() * 1.25f);
	else if (N == TEXT("Back"))
	{
		CancelAll();
		C->Back();
	}
	else if (N.StartsWith(TEXT("Power.")))
	{
		C->ArmPower(N.Mid(6));
		if (bStroke) bStroke = false;
	}
}

void UDmInputRouter::OnButton(int32 Index, bool bDown)
{
	if (Index < 0 || Index > 2) return;
	FButton& B = Btn[Index];
	if (bDown) { B.bDown = true; B.bPressedEvt = true; }
	else { B.bDown = false; B.bReleasedEvt = true; }
	ActionCounts.FindOrAdd(Index == 0 ? NPrimary : (Index == 1 ? NSecondary : NMiddle))++;
}

void UDmInputRouter::OnWheel(const FInputActionValue& Value)
{
	WheelAccum += Value.Get<float>();
	ActionCounts.FindOrAdd(NWheel)++;
}

bool UDmInputRouter::IsDragging() const
{
	const ADmPawn* P = Pawn();
	return P && P->Rig().IsGrabbing();
}

bool UDmInputRouter::GetPointer(FVector2D& Out) const
{
	if (bVirtualPointer) { Out = VirtualPointer; return true; }
	const APlayerController* P = PC();
	float X = 0, Y = 0;
	if (P && P->GetMousePosition(X, Y)) { Out = FVector2D(X, Y); return true; }
	return false;
}

// ------------------------------------------------------------------ picking

int32 UDmInputRouter::SettlementAt(const FDmPick& Pk) const
{
	const UDmSimSubsystem* S = UDmSimSubsystem::Get(this);
	const dm::World* W = S ? S->Sim() : nullptr;
	const ADmPawn* P = Pawn();
	if (!W || !W->geo || Pk.Cell < 0 || !P) return -1;
	// within max(1.3 cell steps, 18 px at the hit distance)
	const FDmCameraRig& R = P->Rig();
	const double DistKm = Pk.Distance / DmCoords::UUPerKm;
	const double KmPerPx = 2.0 * DistKm * FMath::Tan(R.View().VFov * 0.5) / FMath::Max(R.ViewportH(), 1.0);
	const double Tol = FMath::Max(1.3 * W->geo->step, 18.0 * KmPerPx / DmCoords::RadiusKm);
	const double CosTol = FMath::Cos(Tol);
	int32 Best = -1;
	double BestDot = CosTol;
	for (const dm::Settlement& St : W->settlements)
	{
		if (St.dead || !W->valid(St.cell)) continue;
		const double D = FVector::DotProduct(DmCoords::CellDir(*W, St.cell), Pk.PlanetDir);
		if (D > BestDot) { BestDot = D; Best = St.id; }
	}
	return Best;
}

int32 UDmInputRouter::PickCell(const FVector2D& Px, FVector* OutDir, int32* OutSettlement)
{
	const UDmSimSubsystem* S = UDmSimSubsystem::Get(this);
	APlayerController* P = PC();
	FDmPick Pk;
	++Picks;
	if (!S || !P || !S->PickScreen(P, Px, Pk)) { if (OutSettlement) *OutSettlement = -1; return -1; }
	if (OutDir) *OutDir = Pk.PlanetDir;
	if (OutSettlement) *OutSettlement = SettlementAt(Pk);
	return Pk.Cell;
}

int32 UDmInputRouter::TargetFor(int32 Cell, int32 Settlement) const
{
	const UDmControlSubsystem* C = Control();
	const dm::powers::PowerDef* D = C ? C->GetArmedDef() : nullptr;
	if (D && D->target == dm::powers::Target::Settlement && Settlement >= 0)
	{
		const UDmSimSubsystem* S = UDmSimSubsystem::Get(this);
		if (const dm::World* W = S ? S->Sim() : nullptr)
			if (Settlement < int32(W->settlements.size())) return W->settlements[size_t(Settlement)].cell;
	}
	return Cell;
}

void UDmInputRouter::DoHover(const FVector2D& Px, bool bTip)
{
	UDmControlSubsystem* C = Control();
	const UDmSimSubsystem* S = UDmSimSubsystem::Get(this);
	APlayerController* P = PC();
	if (!C) return;
	FDmHover H;
	H.ScreenPos = Px;
	H.Time = Now();
	H.bTip = bTip;
	FDmPick Pk;
	++Picks;
	if (S && P && S->PickScreen(P, Px, Pk))
	{
		H.bHit = true;
		H.Cell = Pk.Cell;
		H.PlanetDir = Pk.PlanetDir;
		H.WorldPos = Pk.WorldPos;
		H.Settlement = SettlementAt(Pk);
	}
	H.bArmed = C->IsArmed();
	H.TargetCell = TargetFor(H.Cell, H.Settlement);
	if (H.bArmed && H.bHit) H.Can = C->Can(H.TargetCell);
	C->SetHover(H);
}

// ------------------------------------------------------------------ actions on the world

void UDmInputRouter::Click(const FVector2D& Px, bool bTouch)
{
	UDmControlSubsystem* C = Control();
	ADmPawn* P = Pawn();
	if (!C || !P) return;
	++ClickCount;
	FVector Dir = FVector::ZeroVector;
	int32 Settle = -1;
	const int32 Cell = PickCell(Px, &Dir, &Settle);
	const double T = Now();
	if (C->IsArmed())
	{
		LastClickTime = -1e9;
		if (Cell < 0) return;   // off the planet: nothing to aim at
		const bool bOk = C->UsePower(TargetFor(Cell, Settle), 0.f, true);
		UE_LOG(LogDmInput, Display, TEXT("DmInput: click power %s at cell %d (settlement %d) -> %s"), *C->GetArmedPower(), Cell, Settle, bOk ? TEXT("used") : TEXT("refused"));
		return;
	}
	const float Slop = bTouch ? DoubleClickSlopTouch : DoubleClickSlopMouse;
	const bool bDouble = (T - LastClickTime) <= DoubleClickSeconds && FVector2D::Distance(Px, LastClickPos) <= Slop;
	if (Cell >= 0)
	{
		if (Settle >= 0) C->Select(Settle);
		else C->Select(-1);
	}
	if (bDouble && Cell >= 0)
	{
		++DoubleClicks;
		const double Z = FMath::Min(1.0, P->Rig().GetTargetZoom() + FlyCloserZoom);
		P->FlyTo(Dir, Z, 1.0);
		UE_LOG(LogDmInput, Display, TEXT("DmInput: double %s -> fly closer (zoom %.2f)"), bTouch ? TEXT("tap") : TEXT("click"), Z);
		LastClickTime = -1e9;
		return;
	}
	UE_LOG(LogDmInput, Display, TEXT("DmInput: %s at cell %d -> select %d"), bTouch ? TEXT("tap") : TEXT("click"), Cell, Settle);
	LastClickTime = T;
	LastClickPos = Px;
}

void UDmInputRouter::StrokeStep(const FVector2D& Px, float Dt, bool bFirst)
{
	UDmControlSubsystem* C = Control();
	if (!C || !C->IsBrushArmed()) { bStroke = false; return; }
	int32 Settle = -1;
	const int32 Cell = PickCell(Px, nullptr, &Settle);
	++StrokeCalls;
	if (Cell < 0) return;
	C->UsePower(Cell, bFirst ? 0.f : Dt, bFirst);
}

void UDmInputRouter::CancelAll()
{
	if (ADmPawn* P = Pawn()) if (P->Rig().IsGrabbing()) P->Rig().GrabEnd();
	bStroke = false;
	bTouchStroke = false;
	for (FButton& B : Btn) { B.bDragged = false; B.bPressedEvt = B.bReleasedEvt = false; }
}

// ------------------------------------------------------------------ per frame

void UDmInputRouter::ProcessFrame(float Dt)
{
	UDmControlSubsystem* C = Control();
	ADmPawn* P = Pawn();
	if (!C || !P) return;
	if (C->IsInputBlocked())
	{
		CancelAll();
		WheelAccum = 0.f;
		for (FButton& B : Btn) B.bDown = false;
		if (Ring) Ring->Hide();
		return;
	}
	FVector2D Ptr;
	const bool bHave = GetPointer(Ptr);

	if (!bSettingsArgs) ApplySettingsArgs();
	const bool bTouchActive = ProcessTouch(Dt);
	if (!bTouchActive) ProcessMouse(Ptr, bHave, Dt);
	if (!KeysBlocked()) ProcessKeys(Dt);
	ProcessEdgePan(Ptr, bHave && !bTouchActive, Dt);

	// hover: <= 10 Hz of real time, every frame while armed or stroking
	const double TNow = Now();
	const bool bDue = TNow - LastHoverTime >= HoverInterval;
	if (!bTouchActive && bHave && (bDue || C->IsArmed() || bStroke))
	{
		DoHover(Ptr, false);
		// keep the phase (a steady 10 Hz), but never let a long frame bank picks; forced picks (armed,
		// stroking) leave the clock alone
		if (bDue) LastHoverTime = (TNow - LastHoverTime < 2.0 * HoverInterval) ? LastHoverTime + HoverInterval : TNow;
	}
	UpdateRing();
}

void UDmInputRouter::ProcessMouse(const FVector2D& Ptr, bool bHave, float Dt)
{
	UDmControlSubsystem* C = Control();
	ADmPawn* P = Pawn();
	FDmCameraRig& R = P->Rig();
	const double T = Now();

	// primary
	{
		FButton& B = Btn[0];
		if (B.bPressedEvt)
		{
			B.bPressedEvt = false;
			B.PressPos = B.LastPos = Ptr;
			B.PressTime = T;
			B.bDragged = false;
			R.StopMotion();
			if (C->IsBrushArmed() && bHave) { bStroke = true; StrokeStep(Ptr, 0.f, true); }
		}
		if (B.bDown && bHave)
		{
			if (bStroke) StrokeStep(Ptr, Dt, false);
			else if (!B.bDragged && FVector2D::Distance(Ptr, B.PressPos) > ClickSlopMouse)
			{
				B.bDragged = true;
				R.GrabBegin(B.PressPos);
				R.GrabMove(Ptr);
			}
			else if (B.bDragged) R.GrabMove(Ptr);
			B.LastPos = Ptr;
		}
		if (B.bReleasedEvt)
		{
			B.bReleasedEvt = false;
			if (bStroke) bStroke = false;
			else if (B.bDragged) { R.GrabEnd(); B.bDragged = false; }
			else if (bHave) Click(Ptr, false);
		}
	}
	// secondary: heading (no power) or orbit (armed); a click without drag disarms
	{
		FButton& B = Btn[1];
		if (B.bPressedEvt)
		{
			B.bPressedEvt = false;
			B.PressPos = B.LastPos = Ptr;
			B.PressTime = T;
			B.bDragged = false;
		}
		if (B.bDown && bHave)
		{
			if (!B.bDragged && FVector2D::Distance(Ptr, B.PressPos) > ClickSlopMouse)
			{
				B.bDragged = true;
				if (C->IsArmed()) { R.GrabBegin(B.PressPos); R.GrabMove(Ptr); }
				else R.Turn((Ptr.X - B.PressPos.X) * RightTurnPerScreen / FMath::Max(R.ViewportW(), 1.0));
			}
			else if (B.bDragged)
			{
				if (R.IsGrabbing()) R.GrabMove(Ptr);
				else R.Turn((Ptr.X - B.LastPos.X) * RightTurnPerScreen / FMath::Max(R.ViewportW(), 1.0));
			}
			B.LastPos = Ptr;
		}
		if (B.bReleasedEvt)
		{
			B.bReleasedEvt = false;
			if (R.IsGrabbing()) R.GrabEnd();
			if (!B.bDragged && C->IsArmed())
			{
				CancelAll();
				C->Disarm();
				UE_LOG(LogDmInput, Display, TEXT("DmInput: right-click disarms"));
			}
			B.bDragged = false;
		}
	}
	// middle: always orbit
	{
		FButton& B = Btn[2];
		if (B.bPressedEvt)
		{
			B.bPressedEvt = false;
			B.PressPos = B.LastPos = Ptr;
			B.bDragged = false;
			R.StopMotion();
		}
		if (B.bDown && bHave)
		{
			if (!B.bDragged && FVector2D::Distance(Ptr, B.PressPos) > ClickSlopMouse)
			{
				B.bDragged = true;
				R.GrabBegin(B.PressPos);
				R.GrabMove(Ptr);
			}
			else if (B.bDragged) R.GrabMove(Ptr);
			B.LastPos = Ptr;
		}
		if (B.bReleasedEvt)
		{
			B.bReleasedEvt = false;
			if (B.bDragged && R.IsGrabbing()) R.GrabEnd();
			B.bDragged = false;
		}
	}
	// wheel: zoom toward the pointer
	if (WheelAccum != 0.f)
	{
		FVector Dir;
		const bool bHit = bHave && PickCell(Ptr, &Dir) >= 0;
		// trackpads send many fractional wheel deltas, mice whole notches: both are summed per frame and
		// become one zoom-target step; the rig's spring and speed cap make either feel continuous
		const float Sgn = C->bInvertZoom ? -1.f : 1.f;
		R.ZoomBy(Sgn * WheelAccum * WheelStep * FMath::Clamp(C->ZoomSpeedSetting, 0.25f, 4.f), bHit ? &Dir : nullptr);
		WheelAccum = 0.f;
	}
}

void UDmInputRouter::ProcessKeys(float Dt)
{
	APlayerController* P = PC();
	ADmPawn* Pw = Pawn();
	const UEnhancedPlayerInput* EPI = P ? Cast<UEnhancedPlayerInput>(P->PlayerInput) : nullptr;
	if (!EPI || !Pw) return;
	FDmCameraRig& R = Pw->Rig();
	const UDmControlSubsystem* Cs = Control();
	const float PanK = Cs ? FMath::Clamp(Cs->PanSpeedSetting, 0.25f, 4.f) : 1.f;
	const float ZoomK = Cs ? FMath::Clamp(Cs->ZoomSpeedSetting, 0.25f, 4.f) : 1.f;
	const FVector2D O = EPI->GetActionValue(FindAction(NOrbit)).Get<FVector2D>();
	if (!O.IsNearlyZero())
	{
		const bool bFast = P->IsInputKeyDown(EKeys::LeftShift) || P->IsInputKeyDown(EKeys::RightShift);
		const double K = KeyPanRate * PanK * (bFast ? KeyPanFast : 1.f);
		const FVector2D On = O.Size() > 1.0 ? O / O.Size() : O;   // diagonals are not faster
		R.AddPanInput(On.X * K, On.Y * K);
	}
	const float Tn = EPI->GetActionValue(FindAction(NTurn)).Get<float>();
	if (Tn != 0.f) R.Turn(Tn * KeyTurnRate * Dt);
	const float Z = EPI->GetActionValue(FindAction(NZoom)).Get<float>();
	if (Z != 0.f) R.ZoomBy(Z * KeyZoomRate * ZoomK * Dt);
}

void UDmInputRouter::ApplySettingsArgs()
{
	bSettingsArgs = true;
	UDmControlSubsystem* C = Control();
	if (!C) return;
	int32 I = 0;
	double X = 0.0;
	if (FDmShotArgs::Value(TEXT("-edgepan="), I)) C->bEdgePan = I != 0;
	if (FDmShotArgs::Value(TEXT("-zoomspeed="), X)) C->ZoomSpeedSetting = float(FMath::Clamp(X, 0.25, 4.0));
	if (FDmShotArgs::Value(TEXT("-panspeed="), X)) C->PanSpeedSetting = float(FMath::Clamp(X, 0.25, 4.0));
	if (FDmShotArgs::Value(TEXT("-invertzoom="), I)) C->bInvertZoom = I != 0;
	UE_LOG(LogDmInput, Display, TEXT("DmInput: camera settings edge pan %s, zoom speed %.2f, pan speed %.2f%s"),
		C->bEdgePan ? TEXT("on") : TEXT("off"), C->ZoomSpeedSetting, C->PanSpeedSetting, C->bInvertZoom ? TEXT(", zoom inverted") : TEXT(""));
}

void UDmInputRouter::ProcessEdgePan(const FVector2D& Ptr, bool bHave, float Dt)
{
	LastEdgePan = FVector2D::ZeroVector;
	UDmControlSubsystem* C = Control();
	ADmPawn* Pw = Pawn();
	if (!C || !Pw || !C->bEdgePan || !bHave) { EdgeTime = 0.0; return; }
	FDmCameraRig& R = Pw->Rig();
	const double W = R.ViewportW(), H = R.ViewportH();
	// only with the window focused (a real cursor) and nothing held (a drag to the edge is a drag)
	const bool bFocus = bVirtualPointer || FApp::HasFocus();
	bool bHeld = bStroke || R.IsGrabbing();
	for (const FButton& B : Btn) bHeld |= B.bDown;
	const bool bInside = Ptr.X >= 0.0 && Ptr.Y >= 0.0 && Ptr.X <= W - 1.0 && Ptr.Y <= H - 1.0;
	if (!bFocus || bHeld || !bInside || R.Busy()) { EdgeTime = 0.0; return; }
	const double M = FMath::Max(EdgePanMarginPx, 1.f);
	auto Depth = [M](double Lo, double Hi) { return Lo < M ? -(1.0 - Lo / M) : (Hi < M ? (1.0 - Hi / M) : 0.0); };
	const double Ex = Depth(Ptr.X, W - 1.0 - Ptr.X);           // -1 left .. +1 right
	const double Ey = Depth(Ptr.Y, H - 1.0 - Ptr.Y);           // -1 top .. +1 bottom
	if (Ex == 0.0 && Ey == 0.0) { EdgeTime = 0.0; return; }
	EdgeTime += Dt;
	if (EdgeTime < EdgePanDwell) return;
	// 0.4 of the rate at the band's inner edge, the full rate at the screen edge
	const double K = EdgePanRate * FMath::Clamp(C->PanSpeedSetting, 0.25f, 4.f);
	FVector2D V(Ex == 0.0 ? 0.0 : FMath::Sign(Ex) * (0.4 + 0.6 * FMath::Abs(Ex)), Ey == 0.0 ? 0.0 : -FMath::Sign(Ey) * (0.4 + 0.6 * FMath::Abs(Ey)));
	if (V.Size() > 1.0) V /= V.Size();
	LastEdgePan = V * K;
	R.AddPanInput(LastEdgePan.X, LastEdgePan.Y);
}

bool UDmInputRouter::ProcessTouch(float Dt)
{
	APlayerController* P = PC();
	ADmPawn* Pw = Pawn();
	UDmControlSubsystem* C = Control();
	if (!P || !Pw || !C) return false;
	FDmCameraRig& R = Pw->Rig();

	bool bNow[2];
	FVector2D Pos[2];
	for (int32 I = 0; I < 2; ++I)
	{
		double X = 0, Y = 0;
		bool bP = false;
		P->GetInputTouchState(ETouchIndex::Type(ETouchIndex::Touch1 + I), X, Y, bP);
		bNow[I] = bP;
		Pos[I] = FVector2D(X, Y);
	}
	const int32 Count = int32(bNow[0]) + int32(bNow[1]);
	const double T = Now();
	const bool bWasActive = TMode != ETouchMode::None;
	if (Count == 0 && !bWasActive) { TouchDown = 0; return false; }

	if (TMode == ETouchMode::None && Count >= 1)
	{
		// touch-down: the ring appears at once
		TMode = Count >= 2 ? ETouchMode::Multi : ETouchMode::Single;
		TPress = TLast = bNow[0] ? Pos[0] : Pos[1];
		TPressTime = T;
		bTDragged = bLongDone = bTouchStroke = false;
		R.StopMotion();
		DoHover(TPress, false);
		if (TMode == ETouchMode::Multi)
		{
			PinchDist = FVector2D::Distance(Pos[0], Pos[1]);
			PinchAng = FMath::Atan2(Pos[1].Y - Pos[0].Y, Pos[1].X - Pos[0].X);
			PinchMid = (Pos[0] + Pos[1]) * 0.5;
		}
	}

	if (TMode == ETouchMode::Single)
	{
		if (Count >= 2)
		{
			// a second finger ends the stroke: orbit / pinch / twist until all lift
			bTouchStroke = false;
			bStroke = false;
			if (R.IsGrabbing()) { R.GrabEnd(); R.StopMotion(); }
			TMode = ETouchMode::Multi;
			PinchDist = FVector2D::Distance(Pos[0], Pos[1]);
			PinchAng = FMath::Atan2(Pos[1].Y - Pos[0].Y, Pos[1].X - Pos[0].X);
			PinchMid = (Pos[0] + Pos[1]) * 0.5;
		}
		else if (Count == 1)
		{
			const FVector2D Cur = bNow[0] ? Pos[0] : Pos[1];
			const double Held = T - TPressTime;
			if (C->IsBrushArmed())
			{
				if (!bTouchStroke && Held >= TouchBrushDelay)
				{
					bTouchStroke = bStroke = true;
					StrokeStep(Cur, 0.f, true);
				}
				else if (bTouchStroke) StrokeStep(Cur, Dt, false);
				DoHover(Cur, false);
			}
			else
			{
				if (!bTDragged && FVector2D::Distance(Cur, TPress) > ClickSlopTouch)
				{
					bTDragged = true;
					R.GrabBegin(TPress);
					R.GrabMove(Cur);
				}
				else if (bTDragged) R.GrabMove(Cur);
				if (!bTDragged && !bLongDone && Held >= LongPressSeconds && FVector2D::Distance(Cur, TPress) < LongPressSlop)
				{
					bLongDone = true;
					DoHover(Cur, true);
					UE_LOG(LogDmInput, Display, TEXT("DmInput: long-press tip at cell %d"), C->GetHover().Cell);
				}
				else if (C->IsArmed()) DoHover(Cur, false);
			}
			TLast = Cur;
		}
		else
		{
			// lifted
			if (bTouchStroke) { bTouchStroke = bStroke = false; }
			else if (bTDragged) R.GrabEnd();
			else if (!bLongDone) Click(TLast, true);
			TMode = ETouchMode::None;
		}
	}
	else if (TMode == ETouchMode::Multi)
	{
		if (Count >= 2)
		{
			const double D = FVector2D::Distance(Pos[0], Pos[1]);
			const double A = FMath::Atan2(Pos[1].Y - Pos[0].Y, Pos[1].X - Pos[0].X);
			const FVector2D Mid = (Pos[0] + Pos[1]) * 0.5;
			if (PinchDist > 1.0 && D > 1.0)
			{
				const double Ratio = D / PinchDist;
				if (FMath::Abs(Ratio - 1.0) > 1e-4)
				{
					FVector Dir;
					const bool bHit = PickCell(Mid, &Dir) >= 0;
					R.ZoomBy(FMath::Log2(Ratio) * PinchZoom, bHit ? &Dir : nullptr);
				}
			}
			double DA = A - PinchAng;
			while (DA > UE_PI) DA -= UE_TWO_PI;
			while (DA < -UE_PI) DA += UE_TWO_PI;
			if (FMath::Abs(DA) > 1e-5) R.Turn(-DA);       // the ground turns with the fingers
			const FVector2D DM = Mid - PinchMid;
			if (!DM.IsNearlyZero()) R.Orbit(DM.X, DM.Y);
			PinchDist = D;
			PinchAng = A;
			PinchMid = Mid;
		}
		else if (Count == 0) TMode = ETouchMode::None;
		// one finger left: nothing until all lift
	}
	TouchDown = Count;
	return TMode != ETouchMode::None || bWasActive;
}

void UDmInputRouter::UpdateRing()
{
	if (!Ring) return;
	const UDmControlSubsystem* C = Control();
	const ADmPawn* P = Pawn();
	const UDmSimSubsystem* S = UDmSimSubsystem::Get(this);
	const dm::World* W = S ? S->Sim() : nullptr;
	const dm::powers::PowerDef* D = C ? C->GetArmedDef() : nullptr;
	const FDmHover* H = C ? &C->GetHover() : nullptr;
	if (!D || !H || !H->bHit || !P || !W)
	{
		Ring->Hide();
		return;
	}
	const FLinearColor Col = GroupColor(D->group);
	const bool bBad = !H->Can.ok;
	const FVector EyeKm = P->Rig().View().Eye;
	float Ang = C->GetRingAng();
	FVector Dir = H->PlanetDir;
	if (Ang <= 0.f)
	{
		// settlement powers: highlight the settlement under the pointer instead of a brush
		if (H->Settlement < 0 || H->Settlement >= int32(W->settlements.size())) { Ring->Hide(); return; }
		const dm::Settlement& St = W->settlements[size_t(H->Settlement)];
		Dir = DmCoords::CellDir(*W, St.cell);
		// ring the town itself (camp ~1 km .. metropolis ~2.6 km), not its whole territory
		Ang = float(FMath::Max(1.2 * W->geo->step, 0.010 + 0.004 * FMath::Clamp(St.tier, 0, 4)));
	}
	const double DistKm = FVector::Dist(EyeKm, Dir * P->GroundKm(Dir));
	Ring->Show(Dir, Ang, Col, bBad, DistKm, &EyeKm);
}
