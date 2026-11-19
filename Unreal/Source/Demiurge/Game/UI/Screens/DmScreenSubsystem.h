// DEMIURGE — Game/UI/Screens/DmScreenSubsystem.h : the screens' controller (SPEC-UI 5 Screens.*, SPEC-UI 6
// Save.*). Owner: C8 screens.
//
//   UDmScreenSubsystem* Screens = UDmScreenSubsystem::Get(this);
//   Screens->ShowTitle();                                 // Screens.title()
//   Screens->ShowPause();                                 // Screens.pause(): also pauses the sim
//   Screens->Back();  Screens->CloseAll();                // Back / Close; Escape is Back
//   Screens->OnFlow.AddLambda([](EDmFlow F) { ... });     // the game flow reacts (C10)
//   Screens->HandleBeginWorld = [](const FDmNewWorldRequest& R) { ...; return true; };   // optional
//
// Screens are a stack: Show* pushes, Back pops, CloseAll clears. The pages are built by DmScreenPages.cpp
// and shown by the host widget (DmScreenHost.cpp) in the viewport, above the HUD. The sim is only
// touched through UDmSimSubsystem's own API (CreateWorld, SaveSlot, LoadSlot, SetPaused, ...).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Game/UI/Screens/DmScreenTypes.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Sim/Save.h"
#include "Sim/Schema.h"
#include "DmScreenSubsystem.generated.h"

class SDmScreenHost;
class UDmSimSubsystem;
struct FSlateDynamicImageBrush;

DECLARE_MULTICAST_DELEGATE_TwoParams(FDmOnScreenChanged, EDmScreen /*Old*/, EDmScreen /*New*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FDmOnFlow, EDmFlow);
DECLARE_MULTICAST_DELEGATE_OneParam(FDmOnSettingsChanged, const dm::save::UserSettings& /*Settings*/);
/** The timelapse moved to frame Index; Frame points into the world's snapshots (nullptr when there are none). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FDmOnTimelapseFrame, int32 /*Index*/, const dm::Snapshot* /*Frame*/);

UCLASS()
class DEMIURGE_API UDmScreenSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The subsystem of the game instance of WorldContext (any UObject or UWorld). */
	static UDmScreenSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ------------------------------------------------------------ navigation (the stack)
	void ShowTitle();
	void ShowSetup();
	void ShowPause();        // pauses the sim until the screens close (if it was running)
	void ShowSettings();
	void ShowHelp();
	void ShowSave();         // write a slot (in game)
	void ShowLoad();         // read a slot (title, pause)
	void ShowEnding();       // the world's ending (or a plain note when it has none yet)
	void ShowTimelapse();    // the ages: plays the world's snapshots over the planet
	void ShowCard();         // the planet card: captures the planet without the interface, then opens the card
	void Back();             // pops one screen; the title stays; a lone pause resumes
	void CloseAll();         // closes everything (the sim keeps its state)
	bool IsOpen() const { return Stack.Num() > 0; }
	EDmScreen Current() const { return Stack.Num() > 0 ? Stack.Last() : EDmScreen::None; }
	/** Rebuild the page on the next Slate tick (never from inside a click handler). */
	void RequestRefresh() { bRefreshPending = true; }

	// ------------------------------------------------------------ settings (SPEC-UI 5 settings(), 6 Save.settings)
	const dm::save::UserSettings& GetSettings() const { return Settings; }
	/** Stores, writes Saved/SaveGames/settings.dmsettings, applies the graphics tier and interface scale, emits OnSettingsChanged. */
	void SetSettings(const dm::save::UserSettings& NewSettings);
	/** As SetSettings, but the file is written later by PersistSettings (a slider drag writes once, on release). */
	void SetSettingsLive(const dm::save::UserSettings& NewSettings);
	/** Writes the settings file when SetSettingsLive changed it since the last write. */
	void PersistSettings();

	// ------------------------------------------------------------ game actions (SPEC-UI 5 Game.* and Save.*)
	/** Begins a world (HandleBeginWorld when bound, else UDmSimSubsystem::CreateWorld) and closes the screens. */
	bool BeginWorld(const FDmNewWorldRequest& Req);
	/** Closes the screens and unpauses the sim if the pause screen paused it. */
	bool ResumeGame();
	bool SaveToSlot(const FString& Slot);
	bool LoadFromSlot(const FString& Slot);
	bool DeleteSlot(const FString& Slot);
	/** Loads the most recently saved slot (the title's Continue). */
	bool ContinueLatest();
	/** Autosaves and shows the title. */
	void ExitToTitle();
	/** The ending is dismissed (Keep playing). */
	void KeepPlaying();
	TArray<FDmSlotInfo> Slots() const;
	bool HasSaves() const;
	bool LatestSlot(FDmSlotInfo& Out) const;
	bool HasWorld() const;
	UDmSimSubsystem* Sim() const;

	// ------------------------------------------------------------ timelapse and planet card
	/** Per-frame work: the timelapse playback, the planet capture countdown. Called by the host. */
	void TickScreens(float DeltaSeconds);
	int32 TimelapseCount() const;
	int32 TimelapseIndex() const { return TimelapseIndexValue; }
	bool IsTimelapsePlaying() const { return bTimelapsePlaying; }
	void SetTimelapseIndex(int32 Index);
	void ToggleTimelapse();
	/** The weight-3 chronicle line of the current frame's years ("" when none). */
	FString TimelapseCaption() const;
	/** Writes the planet card as a PNG under Saved/PlanetCards; OutPath receives the file. */
	bool ExportPlanetCard(FString& OutPath);

	// ------------------------------------------------------------ host and pages (internal, used by the host)
	void EnsureHost();
	void RebuildHost();

	// ------------------------------------------------------------ state the pages read and write
	FString SetupName;
	FString SetupSeed;
	EDmCalamity SetupCalamity = EDmCalamity::Normal;
	bool bSetupConfirm = false;         // "This replaces ... Begin anyway?"
	FString PendingDelete;              // a slot waiting for its second click
	FString Message;                    // one line of feedback on the save and card pages
	FString LastCardPath;
	TSharedPtr<FSlateDynamicImageBrush> CardImage;   // the captured planet, cropped to the card
	bool bRefreshPending = false;

	// ------------------------------------------------------------ notifications and handlers
	FDmOnScreenChanged OnScreenChanged;
	FDmOnFlow OnFlow;
	FDmOnSettingsChanged OnSettingsChanged;
	FDmOnTimelapseFrame OnTimelapseFrame;
	/** When bound, decides how a new world is made (return true when handled). Default: CreateWorld. */
	TFunction<bool(const FDmNewWorldRequest&)> HandleBeginWorld;

private:
	void SetStack(const TArray<EDmScreen>& NewStack);
	void ApplyGraphics();
	/** Resumes the sim for a world that was just begun or loaded, and forgets the pause the screens made. */
	void RunSim();
	void BroadcastTimelapse();
	/** The engine's screenshot of the planet arrives here (interface left out): keeps the centre as the photograph. */
	void OnPlanetCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors);
	void FinishPlanetCapture();

	TArray<EDmScreen> Stack;
	dm::save::UserSettings Settings;
	TSharedPtr<SDmScreenHost> Host;
	EDmScreen BuiltScreen = EDmScreen::None;   // the screen the host shows (a rebuild of the same screen does not fade)
	bool bSettingsDirty = false;               // SetSettingsLive changed the settings and the file is not written yet
	bool bPausedByScreens = false;
	int32 TimelapseIndexValue = 0;
	bool bTimelapsePlaying = false;
	float TimelapseAccum = 0.f;
	int32 LastBroadcastIndex = -1;

	// the planet card's photograph: requested from the engine, filled by OnPlanetCaptured
	bool bCardCapturePending = false;
	bool bPhotoReady = false;
	int32 CardWaitFrames = 0;
	FDelegateHandle PlanetCaptureHandle;
	TArray<FColor> PendingPhoto;
	int32 PendingPhotoW = 0;
	int32 PendingPhotoH = 0;
};
