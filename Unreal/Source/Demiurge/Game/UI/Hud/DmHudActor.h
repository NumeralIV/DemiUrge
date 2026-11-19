// DEMIURGE — Game/UI/Hud/DmHudActor.h : the HUD actor (old DM.HUD + DM.Panels). Owner: C7 hud.
//
// Registered as scene part "hud" (EDmPartKind::Ui): the factory spawns one ADmHud, which builds the Slate
// HUD (SDmHud) into the game viewport, listens to UDmSimSubsystem::OnSimEvent (toasts, banners, hints) and
// OnWorldChanged, and refreshes the model: numbers at 4 Hz, the now strip at 2 Hz, labels every frame.
// The HUD is visible in the 'play' state (ui:state; default play) and hidden on the ending; the hidden-HUD
// state (C3's HideHud) leaves only the eye button.
//
// Scenarios and the integration engineer reach it with ADmHud::Find(WorldContext)->Context().
// Timing: DmHud lines in the log every 5 s (model update and label pass, averaged; budget 0.4 ms).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DmHudActor.generated.h"

class FDmHudContext;
class SDmHud;
class SWidget;
namespace dm { struct Event; struct World; }

UCLASS()
class DEMIURGE_API ADmHud : public AActor
{
	GENERATED_BODY()

public:
	ADmHud();

	static ADmHud* Find(const UObject* WorldContext);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	FDmHudContext& Context() { return *Ctx; }
	TSharedPtr<SDmHud> GetRoot() const { return Root; }
	/** Refresh the model now (scenarios after changing the world). */
	void RefreshNow();
	/** HUD seconds since BeginPlay (real time). */
	double HudTime() const;

private:
	void OnSimEvent(const dm::Event& E);
	void OnWorldChanged(const dm::World* W);

	TSharedPtr<FDmHudContext> Ctx;
	TSharedPtr<SDmHud> Root;
	TSharedPtr<SWidget> Added;
	FDelegateHandle EventHandle, WorldHandle;
	double T0 = 0;
	double NextModel = 0, NextNow = 0, NextLog = 5.0;
	int32 ModelRuns = 0;
	double ModelMsSum = 0, ModelMsMax = 0;
};
