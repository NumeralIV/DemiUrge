// DEMIURGE — Game/UI/Screens/DmScreenSubsystem.cpp : see DmScreenSubsystem.h. Owner: C8 screens.

#include "Game/UI/Screens/DmScreenSubsystem.h"

#include "Game/Core/DmSimSubsystem.h"
#include "Game/UI/Screens/DmScreenCopy.h"
#include "Game/UI/Screens/DmScreenHost.h"
#include "Game/UI/Screens/DmScreenPages.h"
#include "Game/UI/Screens/DmScreenStyle.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Scalability.h"
#include "Sim/Chronicle.h"
#include "Sim/Save.h"
#include "Sim/State.h"
#include "Slate/WidgetRenderer.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmScreens, Log, All);

namespace
{
	FString SettingsPath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), TEXT("settings.dmsettings"));
	}

	std::string ToUtf8(const FString& S)
	{
		return std::string(TCHAR_TO_UTF8(*S));
	}

	FString FromUtf8(const std::string& S)
	{
		return FString(UTF8_TO_TCHAR(S.c_str()));
	}

	uint32 RandomSeed()
	{
		return uint32(FMath::RandRange(1, 2000000000));
	}

	/** The stack with one more screen on top (no duplicate of the top). */
	TArray<EDmScreen> With(const TArray<EDmScreen>& Stack, EDmScreen Screen)
	{
		TArray<EDmScreen> Out = Stack;
		if (Out.Num() == 0 || Out.Last() != Screen) Out.Add(Screen);
		return Out;
	}

	/** The engine's own scalability levels, captured before the first setting is applied: Quality Auto returns to them. */
	Scalability::FQualityLevels GAutoLevels;
	bool GAutoCaptured = false;
	/** The tier last applied (-1 auto, 0..2); -99 before the first apply. Applying again would reset the engine for nothing. */
	int32 GAppliedQuality = -99;
}

// ------------------------------------------------------------------ lifecycle

UDmScreenSubsystem* UDmScreenSubsystem::Get(const UObject* WorldContext)
{
	UGameInstance* GI = nullptr;
	if (const UWorld* W = Cast<UWorld>(WorldContext))
	{
		GI = W->GetGameInstance();
	}
	else if (WorldContext)
	{
		if (UWorld* W2 = WorldContext->GetWorld()) GI = W2->GetGameInstance();
	}
	return GI ? GI->GetSubsystem<UDmScreenSubsystem>() : nullptr;
}

void UDmScreenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Settings = dm::save::loadSettings(ToUtf8(SettingsPath()));
	if (!GAutoCaptured)
	{
		GAutoLevels = Scalability::GetQualityLevels();
		GAutoCaptured = true;
	}
	ApplyGraphics();
	SetupName = DmCopy::RollName(RandomSeed());
	UE_LOG(LogDmScreens, Display, TEXT("DmScreens: settings loaded (interface %.2f, quality %d, sun %s)"),
		Settings.uiScale, Settings.quality, *FromUtf8(Settings.sun));
}

void UDmScreenSubsystem::Deinitialize()
{
	if (Host.IsValid())
	{
		if (GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Host.ToSharedRef());
		Host.Reset();
	}
	Super::Deinitialize();
}

UDmSimSubsystem* UDmScreenSubsystem::Sim() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UDmSimSubsystem>() : nullptr;
}

bool UDmScreenSubsystem::HasWorld() const
{
	UDmSimSubsystem* S = Sim();
	return S && S->HasWorld();
}

// ------------------------------------------------------------------ navigation

void UDmScreenSubsystem::SetStack(const TArray<EDmScreen>& NewStack)
{
	const EDmScreen Old = Current();
	Stack = NewStack;
	// the screens pause the sim only while a pause menu is open: popping it (even over the title) resumes
	if (bPausedByScreens && !Stack.Contains(EDmScreen::Pause))
	{
		if (UDmSimSubsystem* S = Sim()) S->SetPaused(false);
		bPausedByScreens = false;
	}
	const EDmScreen Now = Current();
	if (Old != Now)
	{
		UE_LOG(LogDmScreens, Verbose, TEXT("DmScreens: %d -> %d"), int32(Old), int32(Now));
		OnScreenChanged.Broadcast(Old, Now);
	}
	RequestRefresh();
}

void UDmScreenSubsystem::ShowTitle()
{
	SetStack({ EDmScreen::Title });
}

void UDmScreenSubsystem::ShowSetup()
{
	if (SetupName.IsEmpty()) SetupName = DmCopy::RollName(RandomSeed());
	bSetupConfirm = false;
	Message.Empty();
	TArray<EDmScreen> Next = Stack;
	if (Next.Num() == 0) Next.Add(EDmScreen::Title);
	SetStack(With(Next, EDmScreen::Setup));
}

void UDmScreenSubsystem::ShowPause()
{
	if (Current() == EDmScreen::Pause) return;
	if (UDmSimSubsystem* S = Sim())
	{
		if (S->HasWorld() && !S->IsPaused())
		{
			S->SetPaused(true);
			bPausedByScreens = true;
		}
	}
	SetStack(With(Stack, EDmScreen::Pause));
}

void UDmScreenSubsystem::ShowSettings()
{
	SetStack(With(Stack, EDmScreen::Settings));
}

void UDmScreenSubsystem::ShowHelp()
{
	SetStack(With(Stack, EDmScreen::Help));
}

void UDmScreenSubsystem::ShowSave()
{
	PendingDelete.Empty();
	Message.Empty();
	SetStack(With(Stack, EDmScreen::Save));
}

void UDmScreenSubsystem::ShowLoad()
{
	PendingDelete.Empty();
	Message.Empty();
	SetStack(With(Stack, EDmScreen::Load));
}

void UDmScreenSubsystem::ShowEnding()
{
	SetStack(With(Stack, EDmScreen::Ending));
}

void UDmScreenSubsystem::ShowTimelapse()
{
	if (Current() == EDmScreen::Timelapse) return;
	TimelapseIndexValue = 0;
	TimelapseAccum = 0.f;
	bTimelapsePlaying = TimelapseCount() > 0;
	LastBroadcastIndex = -1;
	SetStack(With(Stack, EDmScreen::Timelapse));
	BroadcastTimelapse();
}

void UDmScreenSubsystem::ShowCard()
{
	if (Current() == EDmScreen::Card || bCardCapturePending) return;
	Message.Empty();   // (integration) the save page's last line ("Saved to ...") showed under the card
	// The photograph is the viewport's own capture of the planet with the interface left out (bShowUI false:
	// the screens stay up, Slate is not drawn into the picture). The card opens when the pixels arrive
	// (OnPlanetCaptured), or after a timeout (240 frames) without a photograph.
	bCardCapturePending = true;
	bPhotoReady = false;
	CardWaitFrames = 0;
	// the viewport broadcasts on its own delegate (UGameViewportClient), not on FScreenshotRequest's
	PlanetCaptureHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &UDmScreenSubsystem::OnPlanetCaptured);
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PlanetCards"), TEXT("planet-capture.png"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	FScreenshotRequest::RequestScreenshot(Path, false, false);
}

void UDmScreenSubsystem::OnPlanetCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors)
{
	if (!bCardCapturePending || bPhotoReady || Width <= 0 || Height <= 0 || Colors.Num() != Width * Height) return;
	// the centre of the frame, cropped to the card's photograph
	const float Aspect = DmPages::CardPhotoAspect();
	int32 CropH = Height;
	int32 CropW = FMath::RoundToInt(CropH * Aspect);
	if (CropW > Width)
	{
		CropW = Width;
		CropH = FMath::RoundToInt(CropW / Aspect);
	}
	const int32 X0 = (Width - CropW) / 2;
	const int32 Y0 = (Height - CropH) / 2;
	PendingPhoto.SetNumUninitialized(CropW * CropH);
	for (int32 Y = 0; Y < CropH; ++Y)
	{
		for (int32 X = 0; X < CropW; ++X)
		{
			FColor C = Colors[(Y0 + Y) * Width + (X0 + X)];
			C.A = 255;
			PendingPhoto[Y * CropW + X] = C;
		}
	}
	PendingPhotoW = CropW;
	PendingPhotoH = CropH;
	bPhotoReady = true;
}

void UDmScreenSubsystem::Back()
{
	if (Stack.Num() > 1)
	{
		TArray<EDmScreen> Next = Stack;
		Next.Pop();
		PendingDelete.Empty();
		SetStack(Next);
		return;
	}
	if (Stack.Num() == 1)
	{
		switch (Stack[0])
		{
		case EDmScreen::Title: return;                 // the title is the floor
		case EDmScreen::Pause: ResumeGame(); return;   // Escape from the pause menu resumes
		default: CloseAll(); return;
		}
	}
}

void UDmScreenSubsystem::CloseAll()
{
	PersistSettings();
	if (bPausedByScreens)
	{
		if (UDmSimSubsystem* S = Sim()) S->SetPaused(false);
		bPausedByScreens = false;
	}
	PendingDelete.Empty();
	bSetupConfirm = false;
	SetStack({});
}

bool UDmScreenSubsystem::ResumeGame()
{
	CloseAll();
	OnFlow.Broadcast(EDmFlow::Resume);
	return true;
}

void UDmScreenSubsystem::KeepPlaying()
{
	CloseAll();
	OnFlow.Broadcast(EDmFlow::KeepPlaying);
}

// ------------------------------------------------------------------ settings

void UDmScreenSubsystem::SetSettings(const dm::save::UserSettings& NewSettings)
{
	Settings = NewSettings;
	bSettingsDirty = false;
	dm::save::saveSettings(Settings, ToUtf8(SettingsPath()));
	ApplyGraphics();
	OnSettingsChanged.Broadcast(Settings);
}

void UDmScreenSubsystem::SetSettingsLive(const dm::save::UserSettings& NewSettings)
{
	// a slider drag: the change is applied and broadcast at once, the file is written on release (PersistSettings)
	Settings = NewSettings;
	bSettingsDirty = true;
	ApplyGraphics();
	OnSettingsChanged.Broadcast(Settings);
}

void UDmScreenSubsystem::PersistSettings()
{
	if (!bSettingsDirty) return;
	bSettingsDirty = false;
	dm::save::saveSettings(Settings, ToUtf8(SettingsPath()));
}

void UDmScreenSubsystem::ApplyGraphics()
{
	DmStyle::SetScale(float(Settings.uiScale));
	// the tiers of SPEC-UI 5: low, medium and high map to scalability 0, 1, 2; auto puts back the engine's own levels
	const int32 Want = Settings.quality >= 0 ? FMath::Clamp(Settings.quality, 0, 2) : -1;
	if (Want == GAppliedQuality) return;
	GAppliedQuality = Want;
	if (Want < 0)
	{
		if (GAutoCaptured) Scalability::SetQualityLevels(GAutoLevels);
		return;
	}
	Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();
	Levels.SetFromSingleQualityLevel(Want);
	Scalability::SetQualityLevels(Levels);
}

void UDmScreenSubsystem::RunSim()
{
	// the pause is the screens' only: a world that was just begun or loaded runs
	if (UDmSimSubsystem* S = Sim())
	{
		if (S->HasWorld()) S->SetPaused(false);
	}
	bPausedByScreens = false;
}

// ------------------------------------------------------------------ game actions

bool UDmScreenSubsystem::BeginWorld(const FDmNewWorldRequest& Req)
{
	bool bOk = false;
	if (HandleBeginWorld)
	{
		bOk = HandleBeginWorld(Req);
	}
	else if (UDmSimSubsystem* S = Sim())
	{
		const uint32 Seed = Req.bRandomSeed ? RandomSeed() : Req.Seed;
		bOk = S->CreateWorld(Seed, Req.Name, Req.bQuickStart, 0.0);
		if (bOk)
		{
			// (wave3-final, code-health U-1) the setup screen's Calm / Normal / Wild choice was stored and never applied
			if (dm::World* W = S->SimMutable()) W->settings.calamity = Req.CalamityMultiplier();
			S->SetPaused(false);
			S->SetSpeed(1.f);
		}
	}
	if (!bOk)
	{
		Message = TEXT("Could not begin the world.");
		RequestRefresh();
		return false;
	}
	SetStack({});
	RunSim();
	OnFlow.Broadcast(EDmFlow::NewWorld);
	return true;
}

bool UDmScreenSubsystem::SaveToSlot(const FString& Slot)
{
	UDmSimSubsystem* S = Sim();
	if (!S || !S->HasWorld())
	{
		Message = TEXT("There is no world to save yet.");
		RequestRefresh();
		return false;
	}
	FString Err;
	if (!S->SaveSlot(Slot, &Err))
	{
		Message = FString::Printf(TEXT("Could not save: %s"), *Err);
		RequestRefresh();
		return false;
	}
	Message = FString::Printf(TEXT("Saved to %s."), *DmCopy::SlotTitle(Slot));
	RequestRefresh();
	OnFlow.Broadcast(EDmFlow::Saved);
	return true;
}

bool UDmScreenSubsystem::LoadFromSlot(const FString& Slot)
{
	UDmSimSubsystem* S = Sim();
	if (!S)
	{
		return false;
	}
	FString Err;
	if (!S->LoadSlot(Slot, &Err))
	{
		Message = FString::Printf(TEXT("Could not load %s: %s"), *DmCopy::SlotTitle(Slot), *Err);
		RequestRefresh();
		return false;
	}
	SetStack({});
	RunSim();
	OnFlow.Broadcast(EDmFlow::Loaded);
	return true;
}

bool UDmScreenSubsystem::DeleteSlot(const FString& Slot)
{
	UDmSimSubsystem* S = Sim();
	const bool bOk = S && S->RemoveSlot(Slot);
	Message = bOk ? FString::Printf(TEXT("Deleted %s."), *DmCopy::SlotTitle(Slot))
				  : FString::Printf(TEXT("Could not delete %s."), *DmCopy::SlotTitle(Slot));
	RequestRefresh();
	return bOk;
}

bool UDmScreenSubsystem::ContinueLatest()
{
	FDmSlotInfo Latest;
	if (!LatestSlot(Latest)) return false;
	if (!LoadFromSlot(Latest.Slot)) return false;
	OnFlow.Broadcast(EDmFlow::Continue);
	return true;
}

void UDmScreenSubsystem::ExitToTitle()
{
	if (HasWorld())
	{
		if (UDmSimSubsystem* S = Sim())
		{
			FString Err;
			S->SaveSlot(TEXT("auto"), &Err);
		}
	}
	PendingDelete.Empty();
	// SetStack resumes the sim when the pause menu leaves the stack (the title shows the live planet)
	SetStack({ EDmScreen::Title });
	OnFlow.Broadcast(EDmFlow::ExitToTitle);
}

TArray<FDmSlotInfo> UDmScreenSubsystem::Slots() const
{
	UDmSimSubsystem* S = Sim();
	return S ? S->ListSlots() : TArray<FDmSlotInfo>();
}

bool UDmScreenSubsystem::HasSaves() const
{
	return Slots().Num() > 0;
}

bool UDmScreenSubsystem::LatestSlot(FDmSlotInfo& Out) const
{
	bool bFound = false;
	for (const FDmSlotInfo& I : Slots())
	{
		if (!bFound || I.SavedAt > Out.SavedAt)
		{
			Out = I;
			bFound = true;
		}
	}
	return bFound;
}

// ------------------------------------------------------------------ timelapse

int32 UDmScreenSubsystem::TimelapseCount() const
{
	UDmSimSubsystem* S = Sim();
	const dm::World* W = S ? S->Sim() : nullptr;
	return W ? int32(W->snapshots.size()) : 0;
}

void UDmScreenSubsystem::SetTimelapseIndex(int32 Index)
{
	const int32 N = TimelapseCount();
	TimelapseIndexValue = N > 0 ? FMath::Clamp(Index, 0, N - 1) : 0;
	TimelapseAccum = 0.f;
	BroadcastTimelapse();
}

void UDmScreenSubsystem::ToggleTimelapse()
{
	const int32 N = TimelapseCount();
	if (N <= 0) return;
	if (!bTimelapsePlaying && TimelapseIndexValue >= N - 1) TimelapseIndexValue = 0;
	bTimelapsePlaying = !bTimelapsePlaying;
	TimelapseAccum = 0.f;
	BroadcastTimelapse();
}

void UDmScreenSubsystem::BroadcastTimelapse()
{
	if (LastBroadcastIndex == TimelapseIndexValue) return;
	LastBroadcastIndex = TimelapseIndexValue;
	UDmSimSubsystem* S = Sim();
	const dm::World* W = S ? S->Sim() : nullptr;
	const dm::Snapshot* Frame = (W && TimelapseIndexValue >= 0 && TimelapseIndexValue < int32(W->snapshots.size()))
		? &W->snapshots[size_t(TimelapseIndexValue)] : nullptr;
	OnTimelapseFrame.Broadcast(TimelapseIndexValue, Frame);
}

FString UDmScreenSubsystem::TimelapseCaption() const
{
	UDmSimSubsystem* S = Sim();
	const dm::World* W = S ? S->Sim() : nullptr;
	if (!W || W->snapshots.empty()) return FString();
	const int32 I = FMath::Clamp(TimelapseIndexValue, 0, int32(W->snapshots.size()) - 1);
	const double Year = W->snapshots[size_t(I)].year;
	// the latest epochal line of the chronicle, from the last sixty years of this frame
	const dm::ChronicleEntry* Best = nullptr;
	for (const dm::ChronicleEntry& E : W->chronicle)
	{
		if (E.weight < 3) continue;
		if (E.year > Year + 0.001) continue;
		if (E.year < Year - 60.0) continue;
		Best = &E;
	}
	return Best ? FromUtf8(Best->text) : FString();
}

// ------------------------------------------------------------------ planet card

void UDmScreenSubsystem::TickScreens(float DeltaSeconds)
{
	if (bCardCapturePending)
	{
		++CardWaitFrames;
		if (bPhotoReady || CardWaitFrames > 240) FinishPlanetCapture();
	}

	if (Current() == EDmScreen::Timelapse && bTimelapsePlaying)
	{
		const int32 N = TimelapseCount();
		if (N <= 0)
		{
			bTimelapsePlaying = false;
			return;
		}
		// the whole history plays in about half a minute (faster for long histories, never under 6 frames a second)
		const float FramesPerSecond = FMath::Clamp(float(N) / 24.f, 6.f, 40.f);
		TimelapseAccum += DeltaSeconds * FramesPerSecond;
		while (TimelapseAccum >= 1.f)
		{
			TimelapseAccum -= 1.f;
			if (TimelapseIndexValue >= N - 1)
			{
				bTimelapsePlaying = false;
				TimelapseAccum = 0.f;
				break;
			}
			++TimelapseIndexValue;
		}
		BroadcastTimelapse();
	}
}

void UDmScreenSubsystem::FinishPlanetCapture()
{
	bCardCapturePending = false;
	if (PlanetCaptureHandle.IsValid())
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(PlanetCaptureHandle);
		PlanetCaptureHandle.Reset();
	}

	CardImage.Reset();
	if (bPhotoReady)
	{
		const int32 W = PendingPhotoW;
		CardImage = DmStyle::MakeImage(TEXT("card"), W, PendingPhotoH, [this, W](int32 X, int32 Y)
		{
			return PendingPhoto[Y * W + X];
		});
	}
	else
	{
		UE_LOG(LogDmScreens, Warning, TEXT("DmScreens: no photograph of the planet arrived; the card has no picture"));
	}
	bPhotoReady = false;
	PendingPhoto.Reset();
	SetStack(With(Stack, EDmScreen::Card));
}

bool UDmScreenSubsystem::ExportPlanetCard(FString& OutPath)
{
	// the card is drawn at 1.25 times its base size: 1200 x 750 pixels
	const float SavedScale = DmStyle::Scale();
	DmStyle::SetScale(1.25f);
	DmStyle::SetDpiCompensation(false);   // drawn at its own 1200 x 750, whatever the window's DPI
	TSharedRef<SWidget> Card = DmPages::CardWidget(this, true);
	DmStyle::SetScale(SavedScale);

	FWidgetRenderer Renderer;
	UTextureRenderTarget2D* Target = Renderer.DrawWidget(Card, FVector2D(1200.0, 750.0));
	DmStyle::SetDpiCompensation(true);
	if (!Target)
	{
		Message = TEXT("Could not draw the card.");
		RequestRefresh();
		return false;
	}

	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PlanetCards"));
	IFileManager::Get().MakeDirectory(*Dir, true);
	UDmSimSubsystem* S = Sim();
	const dm::World* W = S ? S->Sim() : nullptr;
	const FString Base = FPaths::MakeValidFileName(W ? FromUtf8(W->name) : FString(TEXT("planet")));
	const FString File = FString::Printf(TEXT("%s-%s.png"), *Base, *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
	OutPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(Dir, File));

	FArchive* Ar = IFileManager::Get().CreateFileWriter(*OutPath);
	if (!Ar)
	{
		Message = TEXT("Could not write the card file.");
		RequestRefresh();
		return false;
	}
	const bool bOk = FImageUtils::ExportRenderTarget2DAsPNG(Target, *Ar);
	Ar->Close();
	delete Ar;

	LastCardPath = OutPath;
	Message = bOk ? FString::Printf(TEXT("Saved %s"), *OutPath) : TEXT("The card could not be saved.");
	RequestRefresh();
	UE_LOG(LogDmScreens, Display, TEXT("DmScreens: planet card -> %s (%s)"), *OutPath, bOk ? TEXT("ok") : TEXT("failed"));
	return bOk;
}

// ------------------------------------------------------------------ host

void UDmScreenSubsystem::EnsureHost()
{
	if (Host.IsValid()) return;
	Host = SNew(SDmScreenHost).Owner(this);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->AddViewportWidgetContent(Host.ToSharedRef(), 1000);
	}
	else
	{
		UE_LOG(LogDmScreens, Warning, TEXT("DmScreens: no game viewport; the screens will not show"));
	}
	RequestRefresh();
}

void UDmScreenSubsystem::RebuildHost()
{
	if (!Host.IsValid()) return;
	const EDmScreen Top = Current();
	// a rebuild of the same screen (a chip, a toggle, a message) swaps the widgets without the fade-in again
	const bool bNewScreen = Top != BuiltScreen;
	BuiltScreen = Top;
	if (Top == EDmScreen::None)
	{
		Host->SetPage(nullptr, false, false, nullptr, true);
		return;
	}
	Host->SetPage(DmPages::Build(this, Top), DmPages::IsPassThrough(Top), true, DmPages::ScrimFor(Top), bNewScreen);
}
