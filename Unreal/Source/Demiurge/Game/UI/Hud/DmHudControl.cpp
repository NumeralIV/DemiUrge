// DEMIURGE — Game/UI/Hud/DmHudControl.cpp : forwards to C3's UDmControlSubsystem when it is in the build,
// otherwise keeps a local stand-in. Owner: C7 hud.

#include "Game/UI/Hud/DmHudControl.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UObjectIterator.h"

#include "Game/Core/DmPlayerController.h"
#include "Game/Core/DmSimSubsystem.h"

#include "Sim/Civ.h"
#include "Sim/Sim.h"
#include "Sim/Society.h"

#if defined(__has_include)
#if __has_include("Game/Input/DmControl.h")
#include "Game/Input/DmControl.h"
#define DM_HUD_HAS_CONTROL 1
#endif
#endif
#ifndef DM_HUD_HAS_CONTROL
#define DM_HUD_HAS_CONTROL 0
#endif

DEFINE_LOG_CATEGORY_STATIC(LogDmHudControl, Log, All);

namespace
{
#if DM_HUD_HAS_CONTROL
	UDmControlSubsystem* Ctl(const TWeakObjectPtr<UWorld>& W)
	{
		return W.IsValid() ? UDmControlSubsystem::Get(W.Get()) : nullptr;
	}
#endif

	void EmitUi(UDmSimSubsystem* Sim, dm::Event E)
	{
		if (Sim && Sim->SimMutable()) Sim->SimMutable()->bus.emit(std::move(E));
	}

	template <class T> void EmitUi(UDmSimSubsystem* Sim, T Payload)
	{
		dm::Event E;
		E.type = T::kType;
		E.data = std::move(Payload);
		EmitUi(Sim, std::move(E));
	}

	std::string Utf8(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }
}

bool FDmHudControl::HasControlModule()
{
	return DM_HUD_HAS_CONTROL != 0;
}

void FDmHudControl::Bind(UWorld* InWorld)
{
	World = InWorld;
#if !DM_HUD_HAS_CONTROL
	bool bClassExists = false;
	for (TObjectIterator<UClass> It; It; ++It)
		if (It->GetName() == TEXT("DmControlSubsystem")) { bClassExists = true; break; }
	if (bClassExists)
		UE_LOG(LogDmHudControl, Warning, TEXT("DmHud: UDmControlSubsystem exists but DmHudControl.cpp was compiled without it: rebuild DmHudControl.cpp (touch it) so the HUD uses C3's control state"));
#endif
	UE_LOG(LogDmHudControl, Display, TEXT("DmHud: control state from %s"), *Describe());
}

FString FDmHudControl::Describe() const
{
	return HasControlModule() ? FString(TEXT("UDmControlSubsystem (C3 camera-input)")) : FString(TEXT("the HUD's local stand-in (no camera-input module in this build)"));
}

UDmSimSubsystem* FDmHudControl::Sim() const
{
	return World.IsValid() ? UDmSimSubsystem::Get(World.Get()) : nullptr;
}

// ------------------------------------------------------------------ powers

FString FDmHudControl::Armed() const
{
#if DM_HUD_HAS_CONTROL
	if (UDmControlSubsystem* C = Ctl(World)) return C->GetArmedPower();
#endif
	return LocalArmed;
}

const dm::powers::PowerDef* FDmHudControl::ArmedDef() const
{
	const FString A = Armed();
	return A.IsEmpty() ? nullptr : dm::powers::byId(Utf8(A));
}

bool FDmHudControl::ArmPower(const FString& Id)
{
#if DM_HUD_HAS_CONTROL
	if (UDmControlSubsystem* C = Ctl(World)) return C->ArmPower(Id);
#endif
	UDmSimSubsystem* S = Sim();
	FString Next = (Id.IsEmpty() || Id == LocalArmed) ? FString() : Id;
	if (!Next.IsEmpty())
	{
		const dm::World* W = S ? S->Sim() : nullptr;
		const std::string IdU = Utf8(Next);
		if (!dm::powers::byId(IdU) || !W || !dm::powers::unlocked(*W, IdU))
		{
			Toast(TEXT("Not yet within your power"), TEXT("bad"));
			return !LocalArmed.IsEmpty();
		}
	}
	if (Next != LocalArmed)
	{
		LocalArmed = Next;
		dm::EvUiPower E;
		E.id = Utf8(LocalArmed);
		EmitUi(S, E);
	}
	return !LocalArmed.IsEmpty();
}

void FDmHudControl::Disarm()
{
#if DM_HUD_HAS_CONTROL
	if (UDmControlSubsystem* C = Ctl(World)) { C->Disarm(); return; }
#endif
	if (!LocalArmed.IsEmpty()) ArmPower(FString());
}

float FDmHudControl::BrushAng() const
{
#if DM_HUD_HAS_CONTROL
	if (UDmControlSubsystem* C = Ctl(World)) return C->GetBrushAng();
#endif
	return LocalBrush;
}

void FDmHudControl::SetBrush(float Ang)
{
#if DM_HUD_HAS_CONTROL
	if (UDmControlSubsystem* C = Ctl(World)) { C->SetBrush(Ang); return; }
#endif
	LocalBrush = FMath::Clamp(Ang, 0.03f, 0.15f);
}

// ------------------------------------------------------------------ overlay, HUD

int32 FDmHudControl::Overlay() const
{
#if DM_HUD_HAS_CONTROL
	if (UDmControlSubsystem* C = Ctl(World)) return C->GetOverlay();
#endif
	return LocalOverlay;
}

void FDmHudControl::SetOverlay(int32 Mode)
{
#if DM_HUD_HAS_CONTROL
	if (UDmControlSubsystem* C = Ctl(World)) { C->SetOverlay(Mode); return; }
#endif
	Mode = FMath::Clamp(Mode, 0, 5);
	if (Mode == LocalOverlay) return;
	LocalOverlay = Mode;
	dm::EvUiOverlay E;
	E.mode = std::to_string(Mode);
	EmitUi(Sim(), E);
}

bool FDmHudControl::HudHidden() const
{
#if DM_HUD_HAS_CONTROL
	if (UDmControlSubsystem* C = Ctl(World)) return C->IsHudHidden();
#endif
	return bLocalHidden;
}

void FDmHudControl::HideHud(bool bHidden)
{
#if DM_HUD_HAS_CONTROL
	if (UDmControlSubsystem* C = Ctl(World)) { C->HideHud(bHidden); return; }
#endif
	if (bHidden == bLocalHidden) return;
	bLocalHidden = bHidden;
	dm::EvUiHud E;
	E.hidden = bHidden;
	EmitUi(Sim(), E);
}

// ------------------------------------------------------------------ selection, camera, menu

void FDmHudControl::Select(int32 SettlementId)
{
#if DM_HUD_HAS_CONTROL
	if (UDmControlSubsystem* C = Ctl(World)) { C->Select(SettlementId); return; }
#endif
	if (UDmSimSubsystem* S = Sim())
	{
		if (SettlementId >= 0) S->SelectSettlement(SettlementId);
		else S->ClearSelection();
	}
}

int32 FDmHudControl::Selected() const
{
	const UDmSimSubsystem* S = Sim();
	return S ? S->GetSelectedSettlement() : -1;
}

void FDmHudControl::FlyToCell(int32 Cell, float Zoom)
{
#if DM_HUD_HAS_CONTROL
	if (UDmControlSubsystem* C = Ctl(World)) { C->FlyToCell(Cell, Zoom); return; }
#endif
	UE_LOG(LogDmHudControl, Display, TEXT("DmHud: fly to cell %d (no camera module in this build)"), Cell);
}

void FDmHudControl::RequestMenu()
{
	++LocalMenuRequests;
#if DM_HUD_HAS_CONTROL
	if (UDmControlSubsystem* C = Ctl(World)) { C->RequestMenu(); return; }
#endif
	UE_LOG(LogDmHudControl, Display, TEXT("DmHud: menu requested (the screens module opens the pause screen)"));
}

void FDmHudControl::SetSpeed(float Speed)
{
	if (UDmSimSubsystem* S = Sim())
	{
		if (Speed <= 0.f) S->SetPaused(true);
		else { S->SetSpeed(Speed); S->SetPaused(false); }
	}
}

void FDmHudControl::SetPaused(bool bPaused)
{
	if (UDmSimSubsystem* S = Sim()) S->SetPaused(bPaused);
}

// ------------------------------------------------------------------ messages, game actions

void FDmHudControl::Toast(const FString& Text, const FString& Kind)
{
#if DM_HUD_HAS_CONTROL
	if (UDmControlSubsystem* C = Ctl(World)) { C->Toast(Text, Kind); return; }
#endif
	dm::EvUiToast E;
	E.text = Utf8(Text);
	E.kind = Utf8(Kind);
	EmitUi(Sim(), E);
}

bool FDmHudControl::NextPhase()
{
	UDmSimSubsystem* S = Sim();
	dm::World* W = S ? S->SimMutable() : nullptr;
	return W ? dm::sim::genesis::next(*W) : false;
}

bool FDmHudControl::SetEdict(int32 CultureId, dm::Edict Edict)
{
	UDmSimSubsystem* S = Sim();
	dm::World* W = S ? S->SimMutable() : nullptr;
	return W ? dm::society::setEdict(*W, CultureId, Edict) : false;
}

// ------------------------------------------------------------------ hover

void FDmHudControl::ForceHover(int32 Cell, const FVector2D& ScreenPx)
{
	bForced = true;
	ForcedCell = Cell;
	ForcedPx = ScreenPx;
}

bool FDmHudControl::GetHover(FDmHudHover& Out) const
{
	Out = FDmHudHover();
	const UDmSimSubsystem* S = Sim();
	const dm::World* W = S ? S->Sim() : nullptr;
	if (!W) return false;
	const FString A = Armed();
	if (bForced)
	{
		Out.bForced = true;
		Out.Cell = ForcedCell;
		Out.ScreenPx = ForcedPx;
		Out.bHit = W->valid(ForcedCell);
	}
	else
	{
#if DM_HUD_HAS_CONTROL
		if (UDmControlSubsystem* C = Ctl(World))
		{
			const FDmHover& H = C->GetHover();
			Out.bHit = H.bHit;
			Out.Cell = H.Cell;
			Out.Settlement = H.Settlement;
			Out.ScreenPx = H.ScreenPos;
			Out.bArmed = H.bArmed;
			Out.Can = H.Can;
			return Out.bHit;
		}
#endif
		const ADmPlayerController* PC = World.IsValid() ? Cast<ADmPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (!PC) return false;
		const FDmPick& P = PC->GetHoverPick();
		float Mx = 0.f, My = 0.f;
		if (P.Cell < 0 || !PC->GetMousePosition(Mx, My)) return false;
		Out.bHit = true;
		Out.Cell = P.Cell;
		Out.ScreenPx = FVector2D(Mx, My);
	}
	if (!Out.bHit) return false;
	// settlement under the pointer: a live centre within about one cell
	const std::vector<int> Near = dm::civ::near(*W, Out.Cell, W->geo->step * 1.6);
	Out.Settlement = Near.empty() ? -1 : Near.front();
	if (!A.IsEmpty())
	{
		const std::string IdU = Utf8(A);
		const dm::powers::PowerDef* D = dm::powers::byId(IdU);
		int32 Target = Out.Cell;
		if (D && D->target == dm::powers::Target::Settlement)
		{
			const int32 Owner = dm::civ::at(*W, Out.Cell);
			if (Out.Settlement < 0) Out.Settlement = Owner;
			if (Out.Settlement >= 0) Target = W->settlements[size_t(Out.Settlement)].cell;
		}
		Out.bArmed = true;
		Out.Can = dm::powers::can(*W, IdU, Target);
	}
	return true;
}
