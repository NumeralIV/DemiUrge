// DEMIURGE — Game/Input/DmControl.cpp : see DmControl.h. Owner: C3 camera-input.

#include "Game/Input/DmControl.h"
#include "Game/Camera/DmPawn.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmSimSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/NavigationConfig.h"
#include "Framework/Application/SlateApplication.h"

#include <string>

DEFINE_LOG_CATEGORY_STATIC(LogDmControl, Log, All);

namespace
{
	std::string Utf8(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }
}

UDmControlSubsystem* UDmControlSubsystem::Get(const UObject* WorldContext)
{
	if (!WorldContext || !GEngine) return nullptr;
	UWorld* W = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	return W ? W->GetSubsystem<UDmControlSubsystem>() : nullptr;
}

UDmSimSubsystem* UDmControlSubsystem::Sim() const
{
	return UDmSimSubsystem::Get(this);
}

void UDmControlSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UDmSimSubsystem* S = Sim())
		SimHandle = S->OnSimEvent.AddUObject(this, &UDmControlSubsystem::OnSimEvent);
	// Tab cycles the overlays (old SPEC-UI 2): keep Slate from spending it on focus navigation once the
	// HUD and screens add focusable widgets (arrow-key navigation is left on for menus)
	if (InWorld.IsGameWorld() && FSlateApplication::IsInitialized())
		FSlateApplication::Get().GetNavigationConfig()->bTabNavigation = false;
}

void UDmControlSubsystem::Deinitialize()
{
	if (UDmSimSubsystem* S = Sim())
		if (SimHandle.IsValid()) S->OnSimEvent.Remove(SimHandle);
	SimHandle.Reset();
	Super::Deinitialize();
}

void UDmControlSubsystem::Emit(const dm::Event& E)
{
	if (UDmSimSubsystem* S = Sim()) S->OnSimEvent.Broadcast(E);
}

// ------------------------------------------------------------------ powers

const dm::powers::PowerDef* UDmControlSubsystem::GetArmedDef() const
{
	return Armed.IsEmpty() ? nullptr : dm::powers::byId(Utf8(Armed));
}

bool UDmControlSubsystem::IsBrushArmed() const
{
	const dm::powers::PowerDef* D = GetArmedDef();
	return D && D->brush;
}

float UDmControlSubsystem::GetRingAng() const
{
	const dm::powers::PowerDef* D = GetArmedDef();
	if (!D) return 0.f;
	if (D->brush || FCStringAnsi::Strcmp(D->id, "seed") == 0) return BrushAng;
	if (D->target == dm::powers::Target::Settlement) return 0.f;
	return FMath::Max(float(D->ang), 0.015f);   // lightning / awaken have no radius: a small reticle
}

bool UDmControlSubsystem::ArmPower(const FString& Id)
{
	FString New = Id;
	if (New == Armed) New.Empty();                 // arming the armed power disarms
	if (!New.IsEmpty())
	{
		const dm::powers::PowerDef* D = dm::powers::byId(Utf8(New));
		if (!D)
		{
			UE_LOG(LogDmControl, Warning, TEXT("DmControl: unknown power \"%s\""), *New);
			return IsArmed();
		}
		const UDmSimSubsystem* S = Sim();
		const dm::World* W = S ? S->Sim() : nullptr;
		if (!W || !dm::powers::unlocked(*W, D->id))
		{
			Toast(TEXT("Not yet within your power"), TEXT("bad"));
			return IsArmed();
		}
	}
	if (New == Armed) return IsArmed();
	Armed = New;
	bStrokeToasted = false;
	UE_LOG(LogDmControl, Display, TEXT("DmControl: armed \"%s\""), *Armed);
	dm::Event E; E.type = dm::Ev::UiPower; E.data = dm::EvUiPower{ Utf8(Armed) };
	Emit(E);
	OnArmed.Broadcast(Armed);
	return IsArmed();
}

void UDmControlSubsystem::SetBrush(float Ang)
{
	const float A = FMath::Clamp(FMath::IsFinite(Ang) ? Ang : BrushDefault, BrushMin, BrushMax);
	if (FMath::IsNearlyEqual(A, BrushAng)) return;
	BrushAng = A;
	OnBrush.Broadcast(BrushAng);
}

// ------------------------------------------------------------------ overlay, HUD

const TCHAR* UDmControlSubsystem::OverlayName(int32 Mode)
{
	static const TCHAR* Names[OverlayCount] = { TEXT("none"), TEXT("cultures"), TEXT("temperature"), TEXT("rainfall"), TEXT("devotion"), TEXT("habitability") };
	return (Mode >= 0 && Mode < OverlayCount) ? Names[Mode] : TEXT("none");
}

void UDmControlSubsystem::SetOverlay(int32 Mode)
{
	Mode = FMath::Clamp(Mode, 0, OverlayCount - 1);
	bOverlayForced = false;
	if (Mode == Overlay) return;
	Overlay = Mode;
	dm::Event E; E.type = dm::Ev::UiOverlay; E.data = dm::EvUiOverlay{ std::to_string(Overlay) };
	Emit(E);
	OnOverlay.Broadcast(Overlay);
}

void UDmControlSubsystem::CycleOverlay(int32 Dir)
{
	SetOverlay(((Overlay + (Dir >= 0 ? 1 : -1)) % OverlayCount + OverlayCount) % OverlayCount);
}

void UDmControlSubsystem::HideHud(bool bHidden)
{
	if (bHidden == bHudHidden) return;
	bHudHidden = bHidden;
	dm::Event E; E.type = dm::Ev::UiHud; E.data = dm::EvUiHud{ bHudHidden };
	Emit(E);
	OnHud.Broadcast(bHudHidden);
}

// ------------------------------------------------------------------ selection, powers

void UDmControlSubsystem::Select(int32 SettlementId)
{
	UDmSimSubsystem* S = Sim();
	if (!S) return;
	if (SettlementId >= 0) S->SelectSettlement(SettlementId);
	else if (S->GetSelectedSettlement() >= 0) S->ClearSelection();
}

int32 UDmControlSubsystem::GetSelected() const
{
	const UDmSimSubsystem* S = Sim();
	return S ? S->GetSelectedSettlement() : -1;
}

dm::powers::CanResult UDmControlSubsystem::Can(int32 Cell) const
{
	dm::powers::CanResult R;
	const UDmSimSubsystem* S = Sim();
	const dm::World* W = S ? S->Sim() : nullptr;
	const dm::powers::PowerDef* D = GetArmedDef();
	if (!W || !D) return R;
	return dm::powers::can(*W, D->id, Cell);
}

FString UDmControlSubsystem::RejectText(const dm::powers::CanResult& R, const dm::powers::PowerDef* Def)
{
	using namespace dm::powers;
	if (R.ok) return FString();
	switch (R.reason)
	{
	case Reason::Target:
		switch (R.why)
		{
		case Why::Sea: return TEXT("Choose land");                     // a land power over the sea
		case Why::Land: return TEXT("Choose open sea");                // a sea power over land
		case Why::Settlement: return TEXT("Choose a settlement");
		case Why::Barren: return TEXT("Nothing can live here yet");
		case Why::Crowded: return TEXT("Too close to another people");
		default:
			if (Def && FCStringAnsi::Strcmp(Def->id, "peace") == 0) return TEXT("Choose a people at war");
			return FString();
		}
	case Reason::Locked:
	case Reason::Phase:
		return TEXT("Not yet within your power");
	default:
		return FString();   // Awe: the sim's awe:short speaks for it
	}
}

bool UDmControlSubsystem::UsePower(int32 Cell, float Dt, bool bNewStroke)
{
	UDmSimSubsystem* S = Sim();
	const dm::powers::PowerDef* D = GetArmedDef();
	if (!S || !D) return false;
	if (bNewStroke) bStrokeToasted = false;
	const float Ang = (D->brush || FCStringAnsi::Strcmp(D->id, "seed") == 0) ? BrushAng : -1.f;
	const dm::powers::CanResult R = Can(Cell);
	if (!R.ok)
	{
		if (R.reason == dm::powers::Reason::Awe)
			return S->UsePower(Armed, Cell, Dt, Ang);   // refused by the sim, which emits awe:short (rate-limited)
		if (!bStrokeToasted)
		{
			const FString Text = RejectText(R, D);
			if (!Text.IsEmpty()) Toast(Text, TEXT("bad"));
			bStrokeToasted = true;
		}
		return false;
	}
	const FString Used = Armed;
	const bool bOk = S->UsePower(Armed, Cell, Dt, Ang);
	if (bOk && bGameRules && Used == TEXT("awaken")) Disarm();
	return bOk;
}

void UDmControlSubsystem::FlyToDir(const FVector& PlanetDir, float Zoom, float Seconds)
{
	if (ADmPawn* P = GetPawn()) P->FlyTo(PlanetDir, Zoom, Seconds);
}

void UDmControlSubsystem::FlyToCell(int32 Cell, float Zoom, float Seconds)
{
	const UDmSimSubsystem* S = Sim();
	const dm::World* W = S ? S->Sim() : nullptr;
	if (!W || !W->valid(Cell)) return;
	FlyToDir(DmCoords::CellDir(*W, Cell), Zoom, Seconds);
}

int32 UDmControlSubsystem::Back()
{
	if (IsArmed()) { Disarm(); return 1; }
	if (GetSelected() >= 0) { Select(-1); return 2; }
	RequestMenu();
	return 3;
}

// ------------------------------------------------------------------ events

void UDmControlSubsystem::Toast(const FString& Text, const FString& Kind)
{
	++ToastCount;
	LastToast = Text;
	LastToastKind = Kind;
	UE_LOG(LogDmControl, Display, TEXT("DmControl: toast [%s] %s"), *Kind, *Text);
	dm::Event E; E.type = dm::Ev::UiToast; E.data = dm::EvUiToast{ Utf8(Text), Utf8(Kind) };
	Emit(E);
	OnToast.Broadcast(Text, Kind);
}

void UDmControlSubsystem::RequestShake(float Amount)
{
	dm::Event E; E.type = dm::Ev::CameraShake; E.data = dm::EvCameraShake{ double(Amount) };
	Emit(E);
}

void UDmControlSubsystem::RequestMenu()
{
	++MenuRequests;
	if (UDmSimSubsystem* S = Sim()) S->SetPaused(true);
	UE_LOG(LogDmControl, Display, TEXT("DmControl: pause menu requested"));
	OnMenuRequested.Broadcast();
}

void UDmControlSubsystem::OnSimEvent(const dm::Event& E)
{
	if (!bGameRules) return;
	auto ForceArm = [this](const TCHAR* Id) { if (Armed != Id) ArmPower(Id); };
	if (const dm::EvPhase* P = E.as<dm::EvPhase>())
	{
		switch (P->phase)
		{
		case dm::Phase::Water: ForceArm(TEXT("comet")); break;
		case dm::Phase::Life: ForceArm(TEXT("seed")); break;
		case dm::Phase::People:
			ForceArm(TEXT("awaken"));
			SetOverlay(5);
			bOverlayForced = true;
			break;
		case dm::Phase::History:
		{
			const dm::powers::PowerDef* D = GetArmedDef();
			if (D && D->group == dm::powers::Group::Genesis) Disarm();
			if (bOverlayForced) SetOverlay(0);
			break;
		}
		default: break;
		}
	}
	else if (E.type == dm::Ev::PeopleLost)
	{
		ForceArm(TEXT("awaken"));
	}
}

// ------------------------------------------------------------------ gates, pawn

void UDmControlSubsystem::SetInputBlocked(FName Reason, bool bBlocked)
{
	if (bBlocked) BlockReasons.Add(Reason);
	else BlockReasons.Remove(Reason);
}

bool UDmControlSubsystem::IsTextFieldFocused()
{
	if (!FSlateApplication::IsInitialized()) return false;
	const TSharedPtr<SWidget> W = FSlateApplication::Get().GetKeyboardFocusedWidget();
	if (!W.IsValid()) return false;
	const FName T = W->GetType();
	return T == FName(TEXT("SEditableText")) || T == FName(TEXT("SMultiLineEditableText")) || T == FName(TEXT("SEditableTextBox"))
		|| T == FName(TEXT("SMultiLineEditableTextBox"));
}

void UDmControlSubsystem::RegisterPawn(ADmPawn* Pawn)
{
	PawnRef = Pawn;
}

ADmPawn* UDmControlSubsystem::GetPawn() const
{
	return PawnRef.Get();
}
