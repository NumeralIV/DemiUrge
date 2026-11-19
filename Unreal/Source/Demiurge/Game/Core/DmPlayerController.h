// DEMIURGE — Game/Core/DmPlayerController.h : the player controller. Owner: C0 bridge.
//
// Deliberately thin: the camera / input module (C3) owns the pawn, the Enhanced Input actions and
// the mapping contexts (it registers a "camera" scene part that spawns and possesses its pawn).
// This controller shows the mouse cursor, keeps game + UI input, and offers the hover pick that
// input, HUD tooltips and the brush ring share (one pick per frame, cached).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Game/Core/DmPicking.h"
#include "DmPlayerController.generated.h"

UCLASS()
class DEMIURGE_API ADmPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ADmPlayerController();
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

	/** The cell under the mouse cursor this frame (Cell = -1 when off the planet or no mouse). */
	const FDmPick& GetHoverPick() const { return Hover; }

private:
	FDmPick Hover;
};
