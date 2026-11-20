// DEMIURGE — Game/Flow/DmFlow.h : the game flow (old SPEC-UI 0 "DM.Game" behaviour). Owner: C10 integration.
//
// One ADmFlow per game world, spawned by the "play" scenario (the game itself) and by "playthrough". It ties
// the client modules into one game:
//
//   title    a real sim world turns behind the title (the latest save, else a fresh world of a few
//            centuries); the camera drifts round it; the HUD is hidden (ui:state "title")
//   play     New World (screens) -> genesis (molten, cooling, comets, life wave, awakening) -> history at
//            every speed; the HUD shows; autosave every 2 minutes and when the pause menu opens
//   ending   on the sim's "ending" event: HUD hidden, 1x, the camera flies to the Ark's town (ark) or eases
//            out to a far orbit (silence), holds while the launch plays, then the ending screen opens. The
//            Ark's launch cuts to a low launch camera (wave 3): some 30 km off, just above the ground, the sun
//            to its side, tilting up as the Ark climbs (the orbit rig cannot look at a horizon from that high)
//   timelapse the screens' timelapse frames are shown on the planet (ADmPlanet::ShowSnapshot) while the
//            camera slowly orbits; closing it restores the live colours
//
// Also (old SPEC-UI 0): input is blocked while a modal screen is open; after the first awakening the camp is
// selected and the camera flies to it; "slow down for great events" drops 8x / 4x to 1x for 6 s on a
// weight-3 chronicle entry; Escape's "menu" request opens the pause screen.
//
// The flow never writes world state: it acts through UDmSimSubsystem (create, save, load, speed), the screens
// (UDmScreenSubsystem), the control state (UDmControlSubsystem) and the camera pawn.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/UI/Screens/DmScreenTypes.h"
#include "DmFlow.generated.h"

class ACameraActor;
class UDmSimSubsystem;
class UDmScreenSubsystem;
class UDmControlSubsystem;
class ADmPawn;
namespace dm { struct Event; struct World; struct Snapshot; }

UENUM()
enum class EDmFlowState : uint8
{
	Boot,
	Title,
	Play,
	Ending,
	Timelapse,
};

UCLASS()
class DEMIURGE_API ADmFlow : public AActor
{
	GENERATED_BODY()

public:
	ADmFlow();

	static ADmFlow* Find(const UObject* WorldContext);

	/** A world to stand behind the title when there is none: the latest save, else a fresh world (seed 0 =
	 *  a fixed good-looking one) with HistoryYears of headless history. Returns false if nothing was made. */
	static bool PrepareTitleWorld(UDmSimSubsystem* Sim, uint32 Seed = 0, double HistoryYears = 240.0);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Show the title (the normal game start). */
	void StartTitle();
	/** Straight into play (tests and soak runs that set the world up themselves). */
	void EnterPlayForTests() { EnterPlay(); }
	EDmFlowState GetState() const { return State; }
	static const TCHAR* StateName(EDmFlowState S);

	/** The world behind the title is a backdrop, never autosaved. */
	bool IsBackdrop() const { return bBackdrop; }

	// tuning
	double AutosaveSeconds = 120.0;
	double EndingHoldSeconds = 7.0;      // the camera holds on the launch (or the far orbit) before the ending screen
	double TitleDriftPxPerSecond = 9.0;  // the camera's drift behind the title (orbit pixels per second)
	double TimelapseDriftPxPerSecond = 22.0;
	bool bAutosave = true;               // the playthrough turns it off (it saves on purpose)
	bool bAutoSlowAllowed = true;        // "slow down for great events" (the playthrough runs at full speed)

	// the Ark's launch camera (ending): distance from the pad, height of the view line, side light, field of view
	double CineDistKm = 30.0;
	double CinePitchDeg = 4.0;           // the view line's tilt below the horizontal at the pad
	double CineSunOffsetDeg = 105.0;     // the camera looks this far round from the sun's bearing (side light)
	double CineFovDeg = 40.0;            // horizontal
	double CineCutAt = 1.2;              // seconds after the ending event: cut from the fly-in to the launch camera
	bool IsCinematic() const { return bCine && bCineCut; }

	// counters for checks and logs
	int32 Autosaves = 0;
	int32 EndingsSeen = 0;
	int32 SnapshotsShown = 0;

private:
	void SetState(EDmFlowState S);
	void EmitUiState();
	void OnSimEvent(const dm::Event& E);
	void OnWorldChanged(const dm::World* W);
	void OnScreenFlow(EDmFlow F);
	void OnScreenChanged(EDmScreen Old, EDmScreen New);
	void OnTimelapseFrame(int32 Index, const dm::Snapshot* Frame);
	void OnMenuRequested();
	void EnterPlay();
	void UpdateInputGate();
	void DriftCamera(double PxPerSecond, float Dt);
	/** Camera at zoom 0 over the sunlit side, the terminator in view (title, timelapse). */
	void PlaceOnDaySide(double Seconds);
	void Autosave(const TCHAR* Why);
	ADmPawn* Pawn() const;
	UDmSimSubsystem* Sim() const;
	UDmScreenSubsystem* Screens() const;
	UDmControlSubsystem* Control() const;

	EDmFlowState State = EDmFlowState::Boot;
	EDmFlowState StateBeforeTimelapse = EDmFlowState::Play;
	bool bBackdrop = false;
	bool bStatePending = false;          // re-emit ui:state on the next tick (after a world change)
	bool bFirstAwakenDone = false;
	double SinceAutosave = 0.0;
	int32 PlaceDaySideIn = -1;           // frames until the camera is placed over the day side (the sky publishes the sun first)
	double DriftTime = 0.0;

	// ending choreography
	bool bEndingPending = false;
	double EndingTimer = 0.0;
	int32 EndingCell = -1;

	// the launch camera: planet-frame pad, its up, the eye (uu); placed at the cut, then only its aim moves
	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> CineCam;
	bool bCine = false, bCineCut = false;
	double CineTime = 0.0;
	int32 CineCell = -1;
	FVector CinePad = FVector::ZeroVector, CineUp = FVector::UpVector, CineEye = FVector::ZeroVector;
	void StartCine(int32 Cell);
	void PlaceCine();
	void TickCine(float Dt);
	void StopCine();

	// slow down for great events (autoSlow)
	double SlowTimer = 0.0;
	float SlowRestoreSpeed = 0.f;

	FDelegateHandle SimHandle, WorldHandle, FlowHandle, ScreenHandle, TimelapseHandle, MenuHandle;
};
