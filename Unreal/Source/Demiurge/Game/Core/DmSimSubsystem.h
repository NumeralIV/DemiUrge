// DEMIURGE — Game/Core/DmSimSubsystem.h : the bridge between the C++ simulation and the Unreal client.
// Owner: C0 bridge (SPEC-UNREAL 2, C0).
//
// One per game instance. Owns the dm::World, runs it in real time, routes every sim event and the
// dirty flags to the client, and offers the read-only view everybody renders from.
//
//   UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
//   const dm::World* W = Sim->Sim();                        // read-only; nullptr when no world
//   Sim->OnSimEvent.AddUObject(this, &AMyFx::OnSimEvent);   // void OnSimEvent(const dm::Event& E)
//       if (const dm::EvMeteorImpact* I = E.as<dm::EvMeteorImpact>()) { ... I->cell ... }
//   Sim->Dirty().OnDirty.AddUObject(this, &AMyPlanet::OnDirty);        // once per frame, see DmDirtyRouter.h
//   Sim->OnWorldChanged.AddUObject(this, &AMy::OnWorldChanged);        // new / loaded / fixture world: drop caches
//   FDmPick P; if (Sim->PickScreen(PC, MousePos, P)) { P.Cell ... }
//   Sim->UsePower(TEXT("rain"), Cell, DeltaSeconds);
//
// Rules for client code: never write world state (the writers table of FOUNDATION-CPP 13 has no
// client rows); act on the world only through UsePower / SetSpeed / SaveSlot / LoadSlot / Select*.
// Sim events are delivered synchronously while the sim runs (inside a tick or a power): a handler
// may read the world and queue visual work, but must not call back into the sim.
// Events a world emits BEFORE the subsystem adopts it are not routed: CreateWorld with genesis skip
// (phase, culture:born, settlement:founded, chronicle entries, ...), and sim::use inside LoadSlot /
// MakeLive. Listeners rebuild from the world itself on OnWorldChanged (and the first dirty flush after
// a world change reports every flag).
//
// Frame order: the subsystem runs at the start of every game-world tick (before actors): powers
// update, sim ticks, then the dirty flush. So OnSimEvent / OnDirty handlers run before your actor's
// Tick in the same frame.
//
// Time (SPEC-SIM 0.5, old SPEC-UI 0): 1x = 8 ticks per real second (2 years per second in History);
// speeds 0 (pause), 0.25, 1, 2, 4, 8; genesis phases (Water / Life / People) always run at 1x; Molten
// cools at 1x as well. Fixed-step ticking with catch-up limits: at most MaxTicksPerFrame ticks and
// SimBudgetMs of sim time per frame; if the sim cannot keep up, time is dropped (backlog capped at 2
// ticks) instead of spiralling. powers::update(realDt) runs every frame, even while paused (comets in
// flight keep falling), as in the old game loop.
//
// The planet frame: the planet turns with sim time (SPEC-UNREAL 1). The sky module (C2) owns the
// rotation and the sun and publishes them here (SetPlanetToWorld, SetSunDirection); the planet and
// everything standing on it read GetPlanetToWorld() each frame; picking and CellWorldPos use it. A
// scenario may pin the sun (-sun=lat,lon): IsSunPinned() then tells the sky to keep that direction.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/EngineBaseTypes.h"
#include <memory>
#include <string>
#include "Sim/State.h"
#include "Game/Core/DmDirtyRouter.h"
#include "Game/Core/DmFixture.h"
#include "Game/Core/DmPicking.h"
#include "DmSimSubsystem.generated.h"

class APlayerController;

DECLARE_MULTICAST_DELEGATE_OneParam(FDmOnSimEvent, const dm::Event&);
DECLARE_MULTICAST_DELEGATE_OneParam(FDmOnWorldChanged, const dm::World*);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDmOnSelection, int32 /*SettlementId*/, int32 /*CultureId*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDmOnSpeed, float /*Speed*/, bool /*bPaused*/);

struct FDmSlotInfo
{
	FString Slot;
	FString Name;
	uint32 Seed = 0;
	double Year = 0;
	int32 Phase = 0;
	int32 Age = 0;
	double Pop = 0;
	int64 SavedAt = 0;
};

UCLASS()
class DEMIURGE_API UDmSimSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UDmSimSubsystem();
	virtual ~UDmSimSubsystem() override;

	static UDmSimSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Runs once per frame at the START of the game world's tick (FWorldDelegates::OnWorldTickStart),
	 *  before any actor ticks: powers::update, the fixed-step sim ticks, then the dirty flush, so
	 *  listeners see this frame's changes in their own Tick. */
	void RunFrame(float DeltaTime);

	// ------------------------------------------------------------ worlds
	/** A new world through sim::create (Molten, terrain generated). bSkipGenesis: straight to History
	 *  through sim::genesis::skip (sea at SkipSea). Live (time passes). */
	bool CreateWorld(uint32 Seed, const FString& Name = FString(), bool bSkipGenesis = false, double SkipSea = 0.0);
	/** The fixture (DmFixture.h): "planet_7" or a .lab path. Static unless bLive (then sim::use runs). */
	bool LoadFixture(const FString& NameOrPath = TEXT("planet_7"), const FDmFixtureOptions& Opts = FDmFixtureOptions(), bool bLive = false);
	/** Take ownership of any world (tests, tools). bLive: ticking allowed (call sim::use first yourself). */
	void AdoptWorld(std::unique_ptr<dm::World> W, bool bLive, const FString& Source);
	/** Hand a static (fixture) world to the real sim: sim::use (balance, chronicle, rederive). */
	bool MakeLive();
	void ClearWorld();

	bool HasWorld() const { return SimWorld.get() != nullptr; }
	/** Read-only view of the world (nullptr when none). Valid until OnWorldChanged. */
	const dm::World* Sim() const { return SimWorld.get(); }
	/** Mutable access for scenarios, tests and the integration engineer only (see the writers table). */
	dm::World* SimMutable() { return SimWorld.get(); }
	bool IsLive() const { return bLive; }
	const FString& GetWorldSource() const { return WorldSource; }
	const FDmFixtureReport& GetFixtureReport() const { return FixtureReport; }

	// ------------------------------------------------------------ time
	static constexpr double TicksPerSecondAt1x = 8.0;   // 1 / sim::kRealPerTick
	/** 0 pauses; otherwise snapped to the nearest of 0.25, 1, 2, 4, 8. Emits ui:speed. */
	void SetSpeed(float Speed);
	float GetSpeed() const { return Speed; }
	void SetPaused(bool bInPaused);
	bool IsPaused() const { return bPaused; }
	void TogglePause() { SetPaused(!bPaused); }
	/** Ticks per real second the sim runs at now (0 when paused or static). */
	double GetTickRate() const;
	/** Run n ticks now through sim::advance (tests, scenarios). Returns ticks run. */
	int32 StepTicks(int32 N);
	int32 MaxTicksPerFrame = 16;
	float SimBudgetMs = 12.f;
	int32 GetTicksLastFrame() const { return TicksLastFrame; }
	double GetSimMsPerTick() const { return SimMsPerTick; }
	int64 GetTicksRun() const { return TicksRun; }
	int64 GetTicksDropped() const { return TicksDropped; }

	// ------------------------------------------------------------ powers
	/** powers::use(id, cell, {ang, dt}); dt = real seconds since the previous call of a brush press (0 on press). */
	bool UsePower(const FString& Id, int32 Cell, float Dt, float Ang = -1.f);

	// ------------------------------------------------------------ save / load (dm::save, Saved/SaveGames)
	FString GetSaveDir() const;
	bool SaveSlot(const FString& Slot, FString* OutError = nullptr);
	bool LoadSlot(const FString& Slot, FString* OutError = nullptr);
	TArray<FDmSlotInfo> ListSlots() const;
	bool RemoveSlot(const FString& Slot);

	// ------------------------------------------------------------ selection (emits ui:select)
	void SelectSettlement(int32 SettlementId);
	void SelectCulture(int32 CultureId);
	void ClearSelection();
	int32 GetSelectedSettlement() const { return SelectedSettlement; }
	int32 GetSelectedCulture() const { return SelectedCulture; }

	// ------------------------------------------------------------ planet frame, sun
	FTransform GetPlanetToWorld() const { return PlanetToWorld; }
	void SetPlanetToWorld(const FTransform& T) { PlanetToWorld = T; PlanetToWorld.SetScale3D(FVector::OneVector); }
	/** World-space unit vector from the planet toward the sun. */
	FVector GetSunDirection() const { return SunDirection; }
	void SetSunDirection(const FVector& Dir) { SunDirection = Dir.GetSafeNormal(); }
	/** Subsolar point in planet coordinates (degrees); pins the sun (scenarios: -sun=lat,lon). */
	void PinSunAt(double LatDeg, double LonDeg);
	bool IsSunPinned() const { return bSunPinned; }
	void UnpinSun() { bSunPinned = false; }

	// ------------------------------------------------------------ cells and picking
	/** World-space position of a cell's surface. */
	FVector CellWorldPos(int32 Cell) const;
	/** World-space unit up vector at a cell. */
	FVector CellWorldUp(int32 Cell) const;
	bool PickRay(const FVector& WorldOrigin, const FVector& WorldDir, FDmPick& Out) const;
	/** Radius (uu, planet frame) of the pick surface along a planet-frame direction: the sim surface
	 *  interpolated over the sim triangles. InOutCell: hint in, nearest cell out. */
	double SurfaceRadiusUU(const FVector& PlanetDir, int32& InOutCell) const { return SimWorld ? Picker.SurfaceRadius(*SimWorld, PlanetDir, InOutCell) : 0.0; }
	bool PickScreen(const APlayerController* PC, const FVector2D& ScreenPos, FDmPick& Out) const;
	/** Screen position of a cell's surface; false if behind the camera or hidden by the planet. */
	bool ProjectCell(const APlayerController* PC, int32 Cell, FVector2D& OutScreen) const;

	// ------------------------------------------------------------ notifications
	FDmOnSimEvent OnSimEvent;
	FDmOnWorldChanged OnWorldChanged;
	FDmOnSelection OnSelectionChanged;
	FDmOnSpeed OnSpeedChanged;
	FDmDirtyRouter& Dirty() { return Router; }
	int64 GetEventsRouted() const { return EventsRouted; }

private:
	void Subscribe();
	void OnWorldTickStart(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds);
	FDelegateHandle TickStartHandle;
	void EmitUi(const dm::Event& E);
	bool IsGenesisPaced() const;

	std::unique_ptr<dm::World> SimWorld;
	FString WorldSource;
	FDmFixtureReport FixtureReport;
	bool bLive = false;
	bool bWarnedStatic = false;

	float Speed = 1.f;
	bool bPaused = false;
	double Accum = 0;
	int32 TicksLastFrame = 0;
	int64 TicksRun = 0, TicksDropped = 0;
	double SimMsPerTick = 0;
	int64 EventsRouted = 0;

	int32 SelectedSettlement = -1, SelectedCulture = -1;

	FTransform PlanetToWorld = FTransform::Identity;
	FVector SunDirection = FVector(1, 0, 0);
	bool bSunPinned = false;

	FDmDirtyRouter Router;
	FDmPicker Picker;
	uint64 LastTickFrame = 0;
};
