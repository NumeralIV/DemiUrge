// DEMIURGE — Game/Core/DmSimSubsystem.cpp : see DmSimSubsystem.h. Owner: C0 bridge.

#include "Game/Core/DmSimSubsystem.h"
#include "Game/Core/DmCoords.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"

#include "Sim/Powers.h"
#include "Sim/Save.h"
#include "Sim/Sim.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmSim, Log, All);

namespace
{
	const float GSpeeds[5] = { 0.25f, 1.f, 2.f, 4.f, 8.f };

	std::string ToUtf8(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }
	FString FromUtf8(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
}

UDmSimSubsystem::UDmSimSubsystem() = default;
UDmSimSubsystem::~UDmSimSubsystem() = default;

UDmSimSubsystem* UDmSimSubsystem::Get(const UObject* WorldContext)
{
	if (!WorldContext) return nullptr;
	const UWorld* W = WorldContext->GetWorld();
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UDmSimSubsystem>() : nullptr;
}

void UDmSimSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TickStartHandle = FWorldDelegates::OnWorldTickStart.AddUObject(this, &UDmSimSubsystem::OnWorldTickStart);
	UE_LOG(LogDmSim, Display, TEXT("DmSim: subsystem up"));
}

void UDmSimSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldTickStart.Remove(TickStartHandle);
	ClearWorld();
	Super::Deinitialize();
}

void UDmSimSubsystem::OnWorldTickStart(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds)
{
	// only the game instance's own game world, once per frame
	const UGameInstance* GI = GetGameInstance();
	if (!InWorld || !GI || InWorld != GI->GetWorld() || !InWorld->IsGameWorld()) return;
	if (TickType == LEVELTICK_ViewportsOnly) return;   // the world is paused: the sim keeps its own pause
	if (LastTickFrame == GFrameCounter) return;
	LastTickFrame = GFrameCounter;
	RunFrame(DeltaSeconds);
}

// ------------------------------------------------------------------ worlds

void UDmSimSubsystem::Subscribe()
{
	if (!SimWorld) return;
	SimWorld->bus.onAll([this](const dm::Event& E)
	{
		++EventsRouted;
		OnSimEvent.Broadcast(E);
	});
}

void UDmSimSubsystem::AdoptWorld(std::unique_ptr<dm::World> W, bool bInLive, const FString& Source)
{
	SimWorld = std::move(W);
	bLive = bInLive && SimWorld != nullptr;
	bWarnedStatic = false;
	WorldSource = Source;
	Accum = 0;
	SelectedSettlement = SelectedCulture = -1;
	if (SimWorld)
	{
		Subscribe();
		Picker.Rebuild(*SimWorld);
		Router.RequestAll();
		UE_LOG(LogDmSim, Display, TEXT("DmSim: world \"%s\" from %s (L %d, n %d, phase %s, year %.1f, %d settlements, %d cultures, %d units) %s"),
			*FromUtf8(SimWorld->name), *Source, SimWorld->L, SimWorld->n, UTF8_TO_TCHAR(dm::toStr(SimWorld->phase)), SimWorld->year,
			int32(SimWorld->settlements.size()), int32(SimWorld->cultures.size()), int32(SimWorld->units.size()), bLive ? TEXT("live") : TEXT("static"));
	}
	OnWorldChanged.Broadcast(SimWorld.get());
}

void UDmSimSubsystem::ClearWorld()
{
	if (!SimWorld) return;
	SimWorld.reset();
	bLive = false;
	OnWorldChanged.Broadcast(nullptr);
}

bool UDmSimSubsystem::CreateWorld(uint32 Seed, const FString& Name, bool bSkipGenesis, double SkipSea)
{
	const double T0 = FPlatformTime::Seconds();
	dm::sim::WorldOpts O;
	O.seed = Seed;
	O.name = ToUtf8(Name);
	std::unique_ptr<dm::World> W = dm::sim::create(O);
	if (!W) { UE_LOG(LogDmSim, Error, TEXT("DmSim: sim::create failed")); return false; }
	if (bSkipGenesis)
	{
		dm::sim::genesis::SkipOpts SO;
		SO.sea = SkipSea;
		if (!dm::sim::genesis::skip(*W, SO)) UE_LOG(LogDmSim, Warning, TEXT("DmSim: genesis::skip woke nobody (phase %s)"), UTF8_TO_TCHAR(dm::toStr(W->phase)));
	}
	AdoptWorld(std::move(W), true, FString::Printf(TEXT("sim::create(seed %u%s)"), Seed, bSkipGenesis ? TEXT(", skip") : TEXT("")));
	UE_LOG(LogDmSim, Display, TEXT("DmSim: created in %.0f ms"), (FPlatformTime::Seconds() - T0) * 1000.0);
	return true;
}

bool UDmSimSubsystem::LoadFixture(const FString& NameOrPath, const FDmFixtureOptions& Opts, bool bInLive)
{
	const FString Path = DmFixture::ResolveLabPath(NameOrPath);
	if (Path.IsEmpty() || !FPaths::FileExists(Path))
	{
		UE_LOG(LogDmSim, Error, TEXT("DmFixture: no planet file for \"%s\" (looked for UnrealLab/data/<name>.lab above %s)"), *NameOrPath, *FPaths::ProjectDir());
		return false;
	}
	FString Err;
	FDmFixtureReport Rep;
	std::unique_ptr<dm::World> W = DmFixture::Build(Path, Opts, Err, &Rep);
	if (!W) { UE_LOG(LogDmSim, Error, TEXT("DmFixture: %s: %s"), *Path, *Err); return false; }
	const double T0 = FPlatformTime::Seconds();
	const FString Bad = DmFixture::Validate(*W);
	UE_LOG(LogDmSim, Display, TEXT("DmFixture: %s"), *Rep.Summary);
	const std::string NonFinite = dm::sim::firstNonFinite(*W);
	UE_LOG(LogDmSim, Display, TEXT("DmFixture: validate %s (%.0f ms); sim::firstNonFinite \"%s\""), Bad.IsEmpty() ? TEXT("OK") : *Bad,
		(FPlatformTime::Seconds() - T0) * 1000.0, *FromUtf8(NonFinite));
	FixtureReport = Rep;
	AdoptWorld(std::move(W), false, TEXT("fixture ") + FPaths::GetBaseFilename(Path));
	if (bInLive) MakeLive();
	return Bad.IsEmpty();
}

bool UDmSimSubsystem::MakeLive()
{
	if (!SimWorld) return false;
	if (bLive) return true;
	const double T0 = FPlatformTime::Seconds();
	dm::sim::use(*SimWorld);
	bLive = true;
	Picker.Rebuild(*SimWorld);
	Router.RequestAll();
	UE_LOG(LogDmSim, Display, TEXT("DmSim: world is live (sim::use %.0f ms)"), (FPlatformTime::Seconds() - T0) * 1000.0);
	return true;
}

// ------------------------------------------------------------------ time

bool UDmSimSubsystem::IsGenesisPaced() const
{
	if (!SimWorld) return false;
	const dm::Phase P = SimWorld->phase;
	return P == dm::Phase::Molten || P == dm::Phase::Water || P == dm::Phase::Life || P == dm::Phase::People;
}

double UDmSimSubsystem::GetTickRate() const
{
	if (!SimWorld || !bLive || bPaused) return 0.0;
	return TicksPerSecondAt1x * (IsGenesisPaced() ? 1.0 : double(Speed));
}

void UDmSimSubsystem::EmitUi(const dm::Event& E)
{
	if (SimWorld) SimWorld->bus.emit(E);
}

void UDmSimSubsystem::SetSpeed(float InSpeed)
{
	if (!(InSpeed > 0.f))
	{
		SetPaused(true);
		return;
	}
	float Best = GSpeeds[0];
	for (float S : GSpeeds) if (FMath::Abs(S - InSpeed) < FMath::Abs(Best - InSpeed)) Best = S;
	const bool bChanged = Best != Speed || bPaused;
	Speed = Best;
	bPaused = false;
	if (bChanged)
	{
		dm::Event E; E.type = dm::Ev::UiSpeed; E.data = dm::EvUiSpeed{ double(Speed), bPaused };
		EmitUi(E);
		OnSpeedChanged.Broadcast(Speed, bPaused);
	}
}

void UDmSimSubsystem::SetPaused(bool bInPaused)
{
	if (bPaused == bInPaused) return;
	bPaused = bInPaused;
	if (bPaused) Accum = 0;
	dm::Event E; E.type = dm::Ev::UiSpeed; E.data = dm::EvUiSpeed{ double(Speed), bPaused };
	EmitUi(E);
	OnSpeedChanged.Broadcast(Speed, bPaused);
}

int32 UDmSimSubsystem::StepTicks(int32 N)
{
	if (!SimWorld || N <= 0) return 0;
	if (!bLive)
	{
		UE_LOG(LogDmSim, Warning, TEXT("DmSim: StepTicks on a static world (call MakeLive first)"));
		return 0;
	}
	const double T0 = FPlatformTime::Seconds();
	if (BeforeTick)
	{
		for (int32 I = 0; I < N && SimWorld; ++I)
		{
			BeforeTick(*SimWorld);
			if (SimWorld) dm::sim::advance(*SimWorld, 1);
		}
	}
	else dm::sim::advance(*SimWorld, N);
	TicksRun += N;
	const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0 / N;
	SimMsPerTick = SimMsPerTick > 0 ? SimMsPerTick + (Ms - SimMsPerTick) * 0.2 : Ms;
	return N;
}

void UDmSimSubsystem::RunFrame(float DeltaTime)
{
	TicksLastFrame = 0;
	if (!SimWorld) return;
	dm::World& W = *SimWorld;
	const double Dt = FMath::Clamp(double(DeltaTime), 0.0, 0.25);

	if (bLive)
	{
		// real-time pieces (comet / meteor flights, impacts) run even while paused
		dm::powers::update(W, Dt);
		if (!bPaused)
		{
			Accum += Dt * TicksPerSecondAt1x * (IsGenesisPaced() ? 1.0 : double(Speed));
			const double T0 = FPlatformTime::Seconds();
			int32 N = 0;
			while (Accum >= 1.0)
			{
				if (BeforeTick) BeforeTick(W);
				dm::sim::tick(W);
				Accum -= 1.0;
				++N;
				const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
				if (N >= MaxTicksPerFrame || Ms > SimBudgetMs)
				{
					if (Accum > 2.0) { TicksDropped += int64(Accum - 2.0); Accum = 2.0; }
					break;
				}
			}
			if (N > 0)
			{
				const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0 / N;
				SimMsPerTick = SimMsPerTick > 0 ? SimMsPerTick + (Ms - SimMsPerTick) * 0.05 : Ms;
				TicksRun += N;
				TicksLastFrame = N;
			}
		}
	}

	if (!SimWorld) return;   // a handler replaced the world
	const FDmDirtyFlags F = Router.Flush(*SimWorld);
	if (F.bElev || F.bWater) Picker.Rebuild(*SimWorld);
}

// ------------------------------------------------------------------ powers

bool UDmSimSubsystem::UsePower(const FString& Id, int32 Cell, float Dt, float Ang)
{
	if (!SimWorld || !bLive) return false;
	dm::powers::UseOpts O;
	O.ang = Ang;
	O.dt = Dt;
	const std::string IdU = ToUtf8(Id);
	return dm::powers::use(*SimWorld, IdU, Cell, O);
}

// ------------------------------------------------------------------ save / load

FString UDmSimSubsystem::GetSaveDir() const
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames")));
}

bool UDmSimSubsystem::SaveSlot(const FString& Slot, FString* OutError)
{
	if (!SimWorld) { if (OutError) *OutError = TEXT("no world"); return false; }
	const FString Dir = GetSaveDir();
	IFileManager::Get().MakeDirectory(*Dir, true);
	dm::save::EncodeOpts O;
	O.snapshots = true;
	O.savedAt = FDateTime::UtcNow().ToUnixTimestamp();
	std::string Why;
	const double T0 = FPlatformTime::Seconds();
	const bool bOk = dm::save::writeSlot(*SimWorld, ToUtf8(Dir), ToUtf8(Slot), O, &Why);
	UE_LOG(LogDmSim, Display, TEXT("DmSim: save slot \"%s\" %s (%.0f ms)%s%s"), *Slot, bOk ? TEXT("ok") : TEXT("FAILED"),
		(FPlatformTime::Seconds() - T0) * 1000.0, bOk ? TEXT("") : TEXT(": "), bOk ? TEXT("") : *FromUtf8(Why));
	if (!bOk && OutError) *OutError = FromUtf8(Why);
	return bOk;
}

bool UDmSimSubsystem::LoadSlot(const FString& Slot, FString* OutError)
{
	std::string Why;
	const double T0 = FPlatformTime::Seconds();
	std::unique_ptr<dm::World> W = dm::save::readSlot(ToUtf8(GetSaveDir()), ToUtf8(Slot), &Why);
	if (!W)
	{
		UE_LOG(LogDmSim, Warning, TEXT("DmSim: load slot \"%s\" failed: %s"), *Slot, *FromUtf8(Why));
		if (OutError) *OutError = FromUtf8(Why);
		return false;
	}
	dm::sim::use(*W);
	AdoptWorld(std::move(W), true, TEXT("slot ") + Slot);
	UE_LOG(LogDmSim, Display, TEXT("DmSim: loaded slot \"%s\" (%.0f ms incl. sim::use)"), *Slot, (FPlatformTime::Seconds() - T0) * 1000.0);
	return true;
}

TArray<FDmSlotInfo> UDmSimSubsystem::ListSlots() const
{
	TArray<FDmSlotInfo> Out;
	for (const dm::save::SlotInfo& S : dm::save::listSlots(ToUtf8(GetSaveDir())))
	{
		FDmSlotInfo I;
		I.Slot = FromUtf8(S.slot);
		I.Name = FromUtf8(S.meta.name);
		I.Seed = S.meta.seed;
		I.Year = S.meta.year;
		I.Phase = int32(S.meta.phase);
		I.Age = S.meta.age;
		I.Pop = S.meta.pop;
		I.SavedAt = S.meta.savedAt;
		Out.Add(I);
	}
	return Out;
}

bool UDmSimSubsystem::RemoveSlot(const FString& Slot)
{
	return dm::save::removeSlot(ToUtf8(GetSaveDir()), ToUtf8(Slot));
}

// ------------------------------------------------------------------ selection

void UDmSimSubsystem::SelectSettlement(int32 Id)
{
	int32 Cu = -1;
	if (SimWorld && Id >= 0 && Id < int32(SimWorld->settlements.size()) && !SimWorld->settlements[size_t(Id)].dead)
		Cu = SimWorld->settlements[size_t(Id)].culture;
	else
		Id = -1;
	if (Id == SelectedSettlement && Cu == SelectedCulture) return;
	SelectedSettlement = Id;
	SelectedCulture = Cu;
	dm::Event E; E.type = dm::Ev::UiSelect; E.data = dm::EvUiSelect{ Id };
	EmitUi(E);
	OnSelectionChanged.Broadcast(SelectedSettlement, SelectedCulture);
}

void UDmSimSubsystem::SelectCulture(int32 Id)
{
	if (!SimWorld || Id < 0 || Id >= int32(SimWorld->cultures.size())) Id = -1;
	if (Id == SelectedCulture && SelectedSettlement < 0) return;
	SelectedSettlement = -1;
	SelectedCulture = Id;
	OnSelectionChanged.Broadcast(SelectedSettlement, SelectedCulture);
}

void UDmSimSubsystem::ClearSelection()
{
	if (SelectedSettlement < 0 && SelectedCulture < 0) return;
	SelectedSettlement = SelectedCulture = -1;
	dm::Event E; E.type = dm::Ev::UiSelect; E.data = dm::EvUiSelect{ -1 };
	EmitUi(E);
	OnSelectionChanged.Broadcast(-1, -1);
}

// ------------------------------------------------------------------ planet frame, cells, picking

void UDmSimSubsystem::PinSunAt(double LatDeg, double LonDeg)
{
	SunDirection = PlanetToWorld.TransformVectorNoScale(DmCoords::LatLonToDir(LatDeg, LonDeg)).GetSafeNormal();
	bSunPinned = true;
}

FVector UDmSimSubsystem::CellWorldPos(int32 Cell) const
{
	if (!SimWorld) return FVector::ZeroVector;
	return PlanetToWorld.TransformPosition(DmCoords::CellSurfacePos(*SimWorld, Cell));
}

FVector UDmSimSubsystem::CellWorldUp(int32 Cell) const
{
	if (!SimWorld) return FVector::UpVector;
	return PlanetToWorld.TransformVectorNoScale(DmCoords::CellDir(*SimWorld, Cell));
}

bool UDmSimSubsystem::PickRay(const FVector& WorldOrigin, const FVector& WorldDir, FDmPick& Out) const
{
	Out = FDmPick();
	if (!SimWorld) return false;
	const FVector O = PlanetToWorld.InverseTransformPosition(WorldOrigin);
	const FVector D = PlanetToWorld.InverseTransformVectorNoScale(WorldDir);
	if (!Picker.PickPlanetRay(*SimWorld, O, D, Out)) return false;
	Out.WorldPos = PlanetToWorld.TransformPosition(Out.WorldPos);
	return true;
}

bool UDmSimSubsystem::PickScreen(const APlayerController* PC, const FVector2D& ScreenPos, FDmPick& Out) const
{
	Out = FDmPick();
	if (!PC) return false;
	FVector O, D;
	if (!PC->DeprojectScreenPositionToWorld(float(ScreenPos.X), float(ScreenPos.Y), O, D)) return false;
	return PickRay(O, D, Out);
}

bool UDmSimSubsystem::ProjectCell(const APlayerController* PC, int32 Cell, FVector2D& OutScreen) const
{
	if (!PC || !SimWorld || !SimWorld->valid(Cell)) return false;
	const FVector P = CellWorldPos(Cell);
	if (!PC->ProjectWorldLocationToScreen(P, OutScreen, false)) return false;
	// ProjectWorldLocationToScreen and DeprojectScreenPositionToWorld disagree by half a pixel
	// (measured: mean offset (-0.48, -0.53) px); shift so a projected cell picks back through PickScreen
	OutScreen += FVector2D(0.5, 0.5);
	// hidden behind the planet? cast from the camera toward the point
	FVector CamLoc; FRotator CamRot;
	PC->GetPlayerViewPoint(CamLoc, CamRot);
	FDmPick Pk;
	if (!PickRay(CamLoc, (P - CamLoc).GetSafeNormal(), Pk)) return true;
	return Pk.Distance >= FVector::Dist(CamLoc, P) - 0.02 * SimWorld->geo->step * DmCoords::RadiusUU;
}
