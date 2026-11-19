// DEMIURGE — Game/UI/Hud/DmHudActor.cpp : the HUD actor and its scene part. Owner: C7 hud.

#include "Game/UI/Hud/DmHudActor.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "UnrealClient.h"

#include "Game/Core/DmScene.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Game/UI/Hud/DmHudModel.h"
#include "Game/UI/Hud/DmHudStyle.h"
#include "Game/UI/Hud/SDmHud.h"
#include "Game/UI/Hud/SDmHudPanels.h"
#include "Game/UI/Hud/SDmHudWidgets.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmHud, Log, All);

static FDmScenePartRegistrar GDmHudPart(TEXT("hud"), EDmPartKind::Ui, 0,
	[](const FDmSceneContext& S) -> AActor* { return S.World ? S.World->SpawnActor<ADmHud>() : nullptr; },
	TEXT("Game/UI/Hud/DmHudActor.cpp"));

ADmHud::ADmHud()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;   // after the camera moved: labels follow it this frame
}

ADmHud* ADmHud::Find(const UObject* WorldContext)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) return nullptr;
	for (TActorIterator<ADmHud> It(W); It; ++It) return *It;
	return nullptr;
}

double ADmHud::HudTime() const
{
	return FPlatformTime::Seconds() - T0;
}

void ADmHud::BeginPlay()
{
	Super::BeginPlay();
	T0 = FPlatformTime::Seconds();
	Ctx = MakeShared<FDmHudContext>();
	Ctx->Control.Bind(GetWorld());
	Ctx->LoadHints();
	if (UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
	{
		EventHandle = Sim->OnSimEvent.AddUObject(this, &ADmHud::OnSimEvent);
		WorldHandle = Sim->OnWorldChanged.AddUObject(this, &ADmHud::OnWorldChanged);
	}
	Root = SNew(SDmHud).Context(Ctx);
	Added = Root;
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->AddViewportWidgetContent(Added.ToSharedRef(), 20);
	else UE_LOG(LogDmHud, Warning, TEXT("DmHud: no game viewport; the HUD is not shown"));
	RefreshNow();
	UE_LOG(LogDmHud, Display, TEXT("DmHud: up (fonts: serif %s, sans %s)"),
		FDmHudStyle::Get().HasFace(EDmFont::Serif) ? TEXT("Palatino Linotype") : TEXT("fallback"),
		FDmHudStyle::Get().HasFace(EDmFont::Sans) ? TEXT("Segoe UI") : TEXT("fallback"));
}

void ADmHud::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
	{
		Sim->OnSimEvent.Remove(EventHandle);
		Sim->OnWorldChanged.Remove(WorldHandle);
	}
	if (Added.IsValid() && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Added.ToSharedRef());
	Added.Reset();
	Root.Reset();
	Super::EndPlay(Reason);
}

void ADmHud::OnSimEvent(const dm::Event& E)
{
	const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	Ctx->Now = HudTime();
	Ctx->OnEvent(E, Sim ? Sim->Sim() : nullptr);
}

void ADmHud::OnWorldChanged(const dm::World*)
{
	// a new or loaded world: chronicle ids, unlocks, feed, toasts and labels start again
	Ctx->ResetWorld();
	NextModel = 0;
	NextNow = 0;
}

void ADmHud::RefreshNow()
{
	const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const dm::World* W = Sim ? Sim->Sim() : nullptr;
	Ctx->Now = HudTime();
	Ctx->Refresh(W);
	Ctx->RefreshNow(W);
}

void ADmHud::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = HudTime();
	const float RealDt = float(FMath::Clamp(FApp::GetDeltaTime(), 0.0, 0.25));
	Ctx->Tick(Now);
	// the pointer over a panel (set by the panels during the last Slate tick): read by the hover tip
	DmHudUi::PointerOverPanelLast() = DmHudUi::PointerOverPanelNow();
	DmHudUi::PointerOverPanelNow() = false;

	// scale: CSS px at 1080p, never below 0.92 (12 px text stays >= 11 px physical at 720p), x the setting
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FIntPoint Vp = GEngine->GameViewport->Viewport->GetSizeXY();
		if (Vp.X > 0 && Vp.Y > 0)
		{
			Ctx->ViewportPx = FVector2D(Vp.X, Vp.Y);
			const float Engine = FMath::Max(0.05f, GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Vp));
			const float Desired = FMath::Clamp(float(Vp.Y) / 1080.f, 0.92f, 2.5f);
			Ctx->UiScale = Desired / Engine;
		}
	}

	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const dm::World* W = Sim ? Sim->Sim() : nullptr;
	if (Now >= NextModel)
	{
		const double A = FPlatformTime::Seconds();
		Ctx->Refresh(W);
		const double Ms = (FPlatformTime::Seconds() - A) * 1000.0;
		ModelMsSum += Ms; ModelMsMax = FMath::Max(ModelMsMax, Ms); ++ModelRuns;
		Ctx->LastModelMs = Ms;
		NextModel = Now + 0.25;
	}
	if (Now >= NextNow)
	{
		Ctx->RefreshNow(W);
		NextNow = Now + 0.5;
	}
	if (Sim) DmHudLabels::Update(*Ctx, *Sim, GetWorld()->GetFirstPlayerController(), RealDt);

	if (Now >= NextLog)
	{
		// per-frame cost: model (at 4 Hz) amortised over the frames, plus the label pass every frame
		const double Fps = DeltaSeconds > 0 ? 1.0 / FMath::Max(1e-3, double(FApp::GetDeltaTime())) : 60.0;
		const double ModelAvg = ModelRuns ? ModelMsSum / ModelRuns : 0.0;
		UE_LOG(LogDmHud, Display, TEXT("DmHud: model %.3f ms per refresh (max %.3f, %d runs, %.3f ms per frame at %.0f fps), labels %.3f ms per frame, %d labels, alt %.0f km, %d toasts"),
			ModelAvg, ModelMsMax, ModelRuns, ModelAvg * 4.0 / FMath::Max(1.0, Fps), Fps, Ctx->AvgLabelMs, Ctx->Labels.Num(), Ctx->CameraAltitudeKm, Ctx->Toasts.Num());
		NextLog = Now + 5.0;
	}
}
