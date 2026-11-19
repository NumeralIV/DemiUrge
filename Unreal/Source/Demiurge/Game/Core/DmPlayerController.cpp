// DEMIURGE — Game/Core/DmPlayerController.cpp : see DmPlayerController.h. Owner: C0 bridge.

#include "Game/Core/DmPlayerController.h"
#include "Game/Core/DmSimSubsystem.h"

ADmPlayerController::ADmPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

void ADmPlayerController::BeginPlay()
{
	Super::BeginPlay();
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	SetInputMode(Mode);
}

void ADmPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	Hover = FDmPick();
	float X = 0, Y = 0;
	if (!GetMousePosition(X, Y)) return;
	if (const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
		Sim->PickScreen(this, FVector2D(X, Y), Hover);
}
