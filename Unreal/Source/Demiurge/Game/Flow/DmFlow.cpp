// DEMIURGE — Game/Flow/DmFlow.cpp : see DmFlow.h. Owner: C10 integration.

#include "Game/Flow/DmFlow.h"

#include "Game/Audio/DmAudioDirector.h"
#include "Game/Audio/DmAudioEngine.h"
#include "Game/Camera/DmPawn.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Game/Input/DmControl.h"
#include "Game/Planet/DmPlanet.h"
#include "Game/UI/Screens/DmScreenSubsystem.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"

#include "Sim/Civ.h"
#include "Sim/Sim.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmFlow, Log, All);

namespace
{
	// Seeds whose worlds look good from orbit behind the title (checked by eye: several continents, wide seas)
	constexpr uint32 GTitleSeeds[] = { 7u, 21u, 31u, 42u };

	FVector SettledDir(const dm::World& W)
	{
		FVector Sum = FVector::ZeroVector;
		for (const dm::Settlement& S : W.settlements)
			if (!S.dead) Sum += DmCoords::CellDir(W, S.cell) * FMath::Sqrt(FMath::Max(S.pop, 1.0));
		if (Sum.IsNearlyZero())
		{
			const int32 Site = dm::civ::bestSite(W);
			return W.valid(Site) ? DmCoords::CellDir(W, Site) : FVector(1, 0, 0.3).GetSafeNormal();
		}
		return Sum.GetSafeNormal();
	}
}

ADmFlow::ADmFlow()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
}

ADmFlow* ADmFlow::Find(const UObject* WorldContext)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) return nullptr;
	for (TActorIterator<ADmFlow> It(W); It; ++It) return *It;
	return nullptr;
}

const TCHAR* ADmFlow::StateName(EDmFlowState S)
{
	switch (S)
	{
	case EDmFlowState::Title: return TEXT("title");
	case EDmFlowState::Play: return TEXT("play");
	case EDmFlowState::Ending: return TEXT("ending");
	case EDmFlowState::Timelapse: return TEXT("timelapse");
	default: return TEXT("boot");
	}
}

bool ADmFlow::PrepareTitleWorld(UDmSimSubsystem* Sim, uint32 Seed, double HistoryYears)
{
	if (!Sim) return false;
	const double T0 = FPlatformTime::Seconds();
	// the player's own world when there is one (Continue will load it anyway)
	TArray<FDmSlotInfo> Slots = Sim->ListSlots();
	if (Seed == 0 && Slots.Num() > 0)
	{
		Slots.Sort([](const FDmSlotInfo& A, const FDmSlotInfo& B) { return A.SavedAt > B.SavedAt; });
		for (const FDmSlotInfo& S : Slots)
			if (Sim->LoadSlot(S.Slot))
			{
				Sim->SetPaused(true);
				UE_LOG(LogDmFlow, Display, TEXT("DmFlow: title world = save \"%s\" (%s, year %.0f) in %.0f ms"), *S.Slot, *S.Name, S.Year, (FPlatformTime::Seconds() - T0) * 1000.0);
				return true;
			}
	}
	if (Seed == 0) Seed = GTitleSeeds[uint32(FPlatformTime::Cycles()) % UE_ARRAY_COUNT(GTitleSeeds)];
	if (!Sim->CreateWorld(Seed, FString(), true, 0.0)) return false;
	if (HistoryYears > 0.0) Sim->StepTicks(int32(HistoryYears * dm::sim::kTicksPerYear));
	Sim->SetPaused(true);
	UE_LOG(LogDmFlow, Display, TEXT("DmFlow: title world = seed %u, %.0f years of history, in %.0f ms"), Seed, HistoryYears, (FPlatformTime::Seconds() - T0) * 1000.0);
	return true;
}

UDmSimSubsystem* ADmFlow::Sim() const { return UDmSimSubsystem::Get(this); }
UDmScreenSubsystem* ADmFlow::Screens() const { return UDmScreenSubsystem::Get(this); }
UDmControlSubsystem* ADmFlow::Control() const { return UDmControlSubsystem::Get(this); }
ADmPawn* ADmFlow::Pawn() const { return ADmPawn::Find(this); }

void ADmFlow::BeginPlay()
{
	Super::BeginPlay();
	if (UDmSimSubsystem* S = Sim())
	{
		SimHandle = S->OnSimEvent.AddUObject(this, &ADmFlow::OnSimEvent);
		WorldHandle = S->OnWorldChanged.AddUObject(this, &ADmFlow::OnWorldChanged);
	}
	if (UDmScreenSubsystem* Sc = Screens())
	{
		FlowHandle = Sc->OnFlow.AddUObject(this, &ADmFlow::OnScreenFlow);
		ScreenHandle = Sc->OnScreenChanged.AddUObject(this, &ADmFlow::OnScreenChanged);
		TimelapseHandle = Sc->OnTimelapseFrame.AddUObject(this, &ADmFlow::OnTimelapseFrame);
		Sc->OnSettingsChanged.AddWeakLambda(this, [this](const dm::save::UserSettings& St)
		{
			if (ADmAudioDirector* A = ADmAudioDirector::Find(this))
			{
				FDmAudioVolumes V;
				V.Master = float(St.master); V.Music = float(St.music); V.Sfx = float(St.sfx); V.Ambience = float(St.ambience);
				A->SetVolumes(V);
			}
			if (UDmControlSubsystem* C = Control()) C->ShakeSetting = float(St.shake);
		});
	}
	if (UDmControlSubsystem* C = Control()) MenuHandle = C->OnMenuRequested.AddUObject(this, &ADmFlow::OnMenuRequested);
}

void ADmFlow::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UDmSimSubsystem* S = Sim())
	{
		S->OnSimEvent.Remove(SimHandle);
		S->OnWorldChanged.Remove(WorldHandle);
	}
	if (UDmScreenSubsystem* Sc = Screens())
	{
		Sc->OnFlow.Remove(FlowHandle);
		Sc->OnScreenChanged.Remove(ScreenHandle);
		Sc->OnTimelapseFrame.Remove(TimelapseHandle);
		Sc->OnSettingsChanged.RemoveAll(this);
	}
	if (UDmControlSubsystem* C = Control()) C->OnMenuRequested.Remove(MenuHandle);
	Super::EndPlay(Reason);
}

// ------------------------------------------------------------------ state

void ADmFlow::SetState(EDmFlowState S)
{
	if (State == S) return;
	UE_LOG(LogDmFlow, Display, TEXT("DmFlow: %s -> %s"), StateName(State), StateName(S));
	if (State == EDmFlowState::Ending) StopCine();
	State = S;
	EmitUiState();
	UpdateInputGate();
}

void ADmFlow::EmitUiState()
{
	UDmSimSubsystem* S = Sim();
	dm::World* W = S ? S->SimMutable() : nullptr;
	if (!W) return;
	dm::Event E;
	E.type = dm::Ev::UiState;
	dm::EvUiState P;
	P.state = TCHAR_TO_UTF8(StateName(State));
	E.data = P;
	W->bus.emit(E);   // routed to every listener through UDmSimSubsystem::OnSimEvent (HUD, audio)
}

void ADmFlow::UpdateInputGate()
{
	UDmControlSubsystem* C = Control();
	UDmScreenSubsystem* Sc = Screens();
	if (!C) return;
	// a modal screen takes the input; the timelapse plays over the planet with its own controls
	const bool bModal = Sc && Sc->IsOpen() && Sc->Current() != EDmScreen::Timelapse;
	C->SetInputBlocked(TEXT("screen"), bModal || State == EDmFlowState::Title);
	C->SetInputBlocked(TEXT("ending"), State == EDmFlowState::Ending || State == EDmFlowState::Timelapse);
}

void ADmFlow::StartTitle()
{
	bBackdrop = true;
	if (UDmSimSubsystem* S = Sim()) S->SetPaused(true);
	if (UDmControlSubsystem* C = Control()) C->Disarm();
	if (UDmScreenSubsystem* Sc = Screens())
	{
		Sc->EnsureHost();
		Sc->ShowTitle();
		// the saved settings reach the audio and the camera at once
		Sc->OnSettingsChanged.Broadcast(Sc->GetSettings());
	}
	SetState(EDmFlowState::Title);
	if (ADmPawn* P = Pawn())
	{
		P->bAdoptHarnessView = false;   // the flow places the camera (the harness camera is not the game's view)
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) PC->SetViewTarget(P);
	}
	PlaceDaySideIn = 2;
}

void ADmFlow::PlaceOnDaySide(double Seconds)
{
	ADmPawn* P = Pawn();
	UDmSimSubsystem* S = Sim();
	if (!P || !S) return;
	// the sun in the planet frame, then 38 degrees toward one terminator and 18 north: day side, terminator in view
	const FVector SunP = S->GetPlanetToWorld().InverseTransformVectorNoScale(S->GetSunDirection()).GetSafeNormal();
	FVector F = SunP.RotateAngleAxis(-38.0, FVector::UpVector);
	F = (F.GetSafeNormal2D() * FMath::Cos(FMath::DegreesToRadians(18.0)) + FVector::UpVector * FMath::Sin(FMath::DegreesToRadians(18.0))).GetSafeNormal();
	if (F.IsNearlyZero()) return;
	if (Seconds > 0.0) P->FlyTo(F, 0.0, Seconds);
	else P->Jump(F, 0.0, 0.0);
	DriftTime = 0.0;
}

void ADmFlow::EnterPlay()
{
	bBackdrop = false;
	bEndingPending = false;
	SinceAutosave = 0.0;
	SetState(EDmFlowState::Play);
	UDmSimSubsystem* S = Sim();
	const dm::World* W = S ? S->Sim() : nullptr;
	bFirstAwakenDone = W && W->phase >= dm::Phase::History;
	if (UDmControlSubsystem* C = Control())
	{
		C->HideHud(false);
		// the genesis power of the phase we are in (a new world starts molten: comets come with the water)
		if (W)
		{
			// (wave3-final) ArmPower toggles: loading a genesis save while its power was armed (a Water save loaded
			// in the Water phase) disarmed the comet, so the player's clicks did nothing. Arm only when not armed.
			auto ArmGenesis = [C](const TCHAR* Id) { if (C->GetArmedPower() != Id) C->ArmPower(Id); };
			switch (W->phase)
			{
			case dm::Phase::Water: ArmGenesis(TEXT("comet")); break;
			case dm::Phase::Life: ArmGenesis(TEXT("seed")); break;
			case dm::Phase::People: ArmGenesis(TEXT("awaken")); break;
			default: C->Disarm(); break;
			}
		}
	}
	if (ADmPawn* P = Pawn())
		if (W) P->FlyTo(SettledDir(*W), W->phase >= dm::Phase::History ? 0.15 : 0.0, 2.0);
}

void ADmFlow::OnScreenFlow(EDmFlow F)
{
	UE_LOG(LogDmFlow, Display, TEXT("DmFlow: screens flow %d (state %s)"), int32(F), StateName(State));
	switch (F)
	{
	case EDmFlow::NewWorld:
	case EDmFlow::Continue:
	case EDmFlow::Loaded:
		EnterPlay();
		// (wave3-final) a save made in the ending's tick or its camera hold holds an ended world whose Ending event
		// is spent: EnterPlay left it in play with no ending screen, ever. An ended world opens on its ending screen
		// (Keep playing returns to it, as after a live ending).
		if (F != EDmFlow::NewWorld)
			if (UDmSimSubsystem* S = Sim())
				if (const dm::World* W = S->Sim(); W && W->phase == dm::Phase::Ended)
				{
					bEndingPending = true;
					EndingTimer = FMath::Max(0.0, EndingHoldSeconds - 0.5);
					SetState(EDmFlowState::Ending);
					UE_LOG(LogDmFlow, Display, TEXT("DmFlow: the loaded world has ended: its ending screen opens"));
				}
		break;
	case EDmFlow::ExitToTitle:
		bBackdrop = true;
		if (UDmSimSubsystem* S = Sim()) S->SetPaused(true);
		if (UDmControlSubsystem* C = Control()) { C->Disarm(); C->Select(-1); }
		SetState(EDmFlowState::Title);
		PlaceOnDaySide(2.0);
		break;
	case EDmFlow::KeepPlaying:
		if (State == EDmFlowState::Ending || State == EDmFlowState::Timelapse)
		{
			SetState(EDmFlowState::Play);
			if (UDmControlSubsystem* C = Control()) C->HideHud(false);
			if (UDmSimSubsystem* S = Sim()) { S->SetPaused(false); S->SetSpeed(1.f); }
		}
		break;
	case EDmFlow::Resume:
		if (State == EDmFlowState::Title && !bBackdrop) SetState(EDmFlowState::Play);
		break;
	default: break;
	}
	UpdateInputGate();
}

void ADmFlow::OnScreenChanged(EDmScreen Old, EDmScreen New)
{
	if (Old == EDmScreen::Timelapse && New != EDmScreen::Timelapse)
	{
		if (ADmPlanet* P = ADmPlanet::Find(this)) P->ShowSnapshot(nullptr);
		if (State == EDmFlowState::Timelapse) SetState(StateBeforeTimelapse);
	}
	if (New == EDmScreen::Timelapse && State != EDmFlowState::Timelapse)
	{
		StateBeforeTimelapse = State == EDmFlowState::Timelapse ? EDmFlowState::Play : State;
		SetState(EDmFlowState::Timelapse);
		if (UDmSimSubsystem* S = Sim()) S->SetPaused(true);
		PlaceOnDaySide(1.5);
	}
	if (New == EDmScreen::Pause && State == EDmFlowState::Play) Autosave(TEXT("pause"));
	UpdateInputGate();
}

void ADmFlow::OnTimelapseFrame(int32 Index, const dm::Snapshot* Frame)
{
	if (ADmPlanet* P = ADmPlanet::Find(this))
	{
		P->ShowSnapshot(Frame);
		++SnapshotsShown;
	}
}

void ADmFlow::OnMenuRequested()
{
	UDmScreenSubsystem* Sc = Screens();
	if (!Sc || State != EDmFlowState::Play) return;
	if (!Sc->IsOpen()) Sc->ShowPause();
}

void ADmFlow::OnWorldChanged(const dm::World* W)
{
	// a new world resets the HUD's model: tell it the state again once every listener has seen the new world
	bStatePending = true;
	StopCine();
	bFirstAwakenDone = W && W->phase >= dm::Phase::History;
}

void ADmFlow::Autosave(const TCHAR* Why)
{
	if (!bAutosave || bBackdrop || State != EDmFlowState::Play) return;
	UDmSimSubsystem* S = Sim();
	if (!S || !S->HasWorld()) return;
	FString Err;
	if (S->SaveSlot(TEXT("auto"), &Err))
	{
		++Autosaves;
		UE_LOG(LogDmFlow, Display, TEXT("DmFlow: autosave (%s)"), Why);
	}
	SinceAutosave = 0.0;
}

// ------------------------------------------------------------------ sim events

void ADmFlow::OnSimEvent(const dm::Event& E)
{
	UDmSimSubsystem* S = Sim();
	const dm::World* W = S ? S->Sim() : nullptr;
	if (!W) return;
	switch (E.type)
	{
	case dm::Ev::SettlementFounded:
		if (const dm::EvSettlementFounded* F = E.as<dm::EvSettlementFounded>())
		{
			// after the first awakening: select the camp and fly to it (old SPEC-UI 0)
			if (F->by == dm::FoundedBy::Awaken && !bFirstAwakenDone && State == EDmFlowState::Play)
			{
				bFirstAwakenDone = true;
				if (UDmControlSubsystem* C = Control())
				{
					C->Select(F->id);
					C->FlyToCell(F->cell, 0.55f, 2.4f);
				}
			}
		}
		break;
	case dm::Ev::ChronicleEntry:
		if (const dm::EvChronicleEntry* C = E.as<dm::EvChronicleEntry>())
		{
			UDmScreenSubsystem* Sc = Screens();
			const bool bAutoSlow = bAutoSlowAllowed && (!Sc || Sc->GetSettings().autoSlow);
			if (bAutoSlow && C->entry.weight >= 3 && State == EDmFlowState::Play && !S->IsPaused() && S->GetSpeed() >= 4.f && W->phase == dm::Phase::History)
			{
				if (SlowTimer <= 0.0) SlowRestoreSpeed = S->GetSpeed();
				SlowTimer = 6.0;
				S->SetSpeed(1.f);
			}
		}
		break;
	case dm::Ev::Ending:
		if (const dm::EvEnding* En = E.as<dm::EvEnding>())
		{
			++EndingsSeen;
			bEndingPending = true;
			EndingTimer = 0.0;
			EndingCell = (En->settlement >= 0 && En->settlement < int32(W->settlements.size())) ? W->settlements[size_t(En->settlement)].cell : -1;
			SlowTimer = 0.0;
			SetState(EDmFlowState::Ending);
			S->SetSpeed(1.f);
			if (UDmControlSubsystem* Ct = Control()) { Ct->Disarm(); Ct->Select(-1); }
			if (En->type == dm::EndingType::Ark && EndingCell >= 0)
			{
				if (UDmControlSubsystem* Ct = Control()) Ct->FlyToCell(EndingCell, 0.42f, 3.0f);
				StartCine(EndingCell);   // then the cut to the launch camera
			}
			else if (ADmPawn* P = Pawn())
			{
				P->FlyTo(SettledDir(*W), 0.0, 4.0);   // silence: ease out to a far orbit
			}
			UE_LOG(LogDmFlow, Display, TEXT("DmFlow: ending %s / %s in year %.0f"), UTF8_TO_TCHAR(dm::toStr(En->type)), UTF8_TO_TCHAR(dm::toStr(En->mood)), En->year);
		}
		break;
	case dm::Ev::PeopleLost:
		break;   // the control rules arm Awaken; the HUD shows its guide
	default: break;
	}
}

// ------------------------------------------------------------------ tick

void ADmFlow::DriftCamera(double PxPerSecond, float Dt)
{
	ADmPawn* P = Pawn();
	if (!P || P->Rig().Busy() || P->Rig().IsGrabbing()) return;
	// a slow sway (90 s period) rather than a full turn: the view stays on the sunlit side
	DriftTime += Dt;
	P->Rig().Orbit(PxPerSecond * Dt * FMath::Cos(DriftTime * 2.0 * PI / 90.0), 0.0);
}

void ADmFlow::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (PlaceDaySideIn >= 0 && --PlaceDaySideIn < 0) PlaceOnDaySide(0.0);
	if (bStatePending)
	{
		bStatePending = false;
		EmitUiState();
		UpdateInputGate();
	}
	UDmSimSubsystem* S = Sim();
	UDmScreenSubsystem* Sc = Screens();
	if (!S) return;
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);

	switch (State)
	{
	case EDmFlowState::Title:
		DriftCamera(TitleDriftPxPerSecond, Dt);
		break;
	case EDmFlowState::Timelapse:
		DriftCamera(TimelapseDriftPxPerSecond, Dt);
		break;
	case EDmFlowState::Play:
		SinceAutosave += Dt;
		if (SinceAutosave >= AutosaveSeconds) Autosave(TEXT("timer"));
		if (SlowTimer > 0.0)
		{
			SlowTimer -= Dt;
			// restore the previous speed unless the player changed it meanwhile
			if (SlowTimer <= 0.0 && !S->IsPaused() && S->GetSpeed() == 1.f && SlowRestoreSpeed > 1.f) S->SetSpeed(SlowRestoreSpeed);
		}
		break;
	case EDmFlowState::Ending:
		TickCine(Dt);
		if (bEndingPending)
		{
			EndingTimer += Dt;
			if (EndingTimer >= EndingHoldSeconds)
			{
				bEndingPending = false;
				StopCine();   // the ending screen opens over the planet as the player left it
				if (Sc) Sc->ShowEnding();
			}
		}
		break;
	default: break;
	}
}

// ------------------------------------------------------------------ the Ark's launch camera

void ADmFlow::StartCine(int32 Cell)
{
	const UDmSimSubsystem* S = Sim();
	const dm::World* W = S ? S->Sim() : nullptr;
	if (!W || !W->valid(Cell)) return;
	bCine = true;
	bCineCut = false;
	CineTime = 0.0;
	CineCell = Cell;
}

void ADmFlow::PlaceCine()
{
	UDmSimSubsystem* S = Sim();
	const dm::World* W = S ? S->Sim() : nullptr;
	if (!W || !W->valid(CineCell) || !W->geo) return;
	CineUp = DmCoords::CellDir(*W, CineCell);
	CinePad = DmCoords::CellSurfacePos(*W, CineCell);
	FVector E, N;
	DmCoords::Basis(CineUp, E, N);
	// look across the pad with the sun to the side (read at the cut: a scenario may have placed the sun meanwhile)
	const FVector SunP = S->GetPlanetToWorld().InverseTransformVectorNoScale(S->GetSunDirection()).GetSafeNormal();
	const FVector SunH = SunP - CineUp * FVector::DotProduct(SunP, CineUp);
	FVector Look = SunH.SizeSquared() < 1e-6 ? N : SunH.GetSafeNormal();
	Look = Look.RotateAngleAxis(CineSunOffsetDeg, CineUp).GetSafeNormal();
	const double Pt = FMath::DegreesToRadians(CinePitchDeg);
	const FVector Fwd = (Look * FMath::Cos(Pt) - CineUp * FMath::Sin(Pt)).GetSafeNormal();
	CineEye = CinePad - Fwd * (CineDistKm * DmCoords::UUPerKm);
	// clear of the ground (and the sea) under the eye by at least 1.2 km
	const FVector EyeDir = CineEye.GetSafeNormal();
	const dm::Vec3 Sv = DmCoords::UEToSim(EyeDir);
	const int32 EyeCell = W->geo->cellAt(Sv.x, Sv.y, Sv.z);
	const double GroundUU = DmCoords::HeightToRadiusUU(DmCoords::SurfaceHeight(*W, EyeCell));
	if (CineEye.Size() < GroundUU + 1.2 * DmCoords::UUPerKm) CineEye = EyeDir * (GroundUU + 1.2 * DmCoords::UUPerKm);
	if (!CineCam)
	{
		FActorSpawnParameters P;
		P.ObjectFlags |= RF_Transient;
		CineCam = GetWorld()->SpawnActor<ACameraActor>(P);
		if (CineCam && CineCam->GetCameraComponent())
		{
			CineCam->GetCameraComponent()->bConstrainAspectRatio = false;
			CineCam->GetCameraComponent()->SetFieldOfView(float(CineFovDeg));
		}
	}
}

void ADmFlow::TickCine(float Dt)
{
	if (!bCine) return;
	UDmSimSubsystem* S = Sim();
	if (!S || !S->Sim()) { StopCine(); return; }
	CineTime += Dt;
	if (!bCineCut)
	{
		if (CineTime < CineCutAt) return;
		PlaceCine();
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (!CineCam || !PC) { bCine = false; return; }
		PC->SetViewTarget(CineCam);
		bCineCut = true;
		UE_LOG(LogDmFlow, Display, TEXT("DmFlow: the launch camera (%.0f km off, %.0f deg)"), CineDistKm, CinePitchDeg);
	}
	// the aim follows the Ark up (the launch effect climbs at about 0.28 km/s^2: Game/FX FArk), a little below it
	// so the plume and the pad stay in the frame
	const double Hkm = FMath::Min(0.5 * 0.28 * CineTime * CineTime, 40.0);
	const FVector Aim = CinePad + CineUp * ((0.8 + 0.7 * Hkm) * DmCoords::UUPerKm);
	const FTransform P2W = S->GetPlanetToWorld();
	const FVector Loc = P2W.TransformPosition(CineEye);
	const FVector Fwd = P2W.TransformVectorNoScale((Aim - CineEye).GetSafeNormal());
	const FVector Up = P2W.TransformVectorNoScale(CineEye.GetSafeNormal());
	CineCam->SetActorLocationAndRotation(Loc, FRotationMatrix::MakeFromXZ(Fwd, Up).Rotator());
}

void ADmFlow::StopCine()
{
	if (!bCine) return;
	const bool bWasCut = bCineCut;
	bCine = false;
	bCineCut = false;
	if (bWasCut)
		if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
			if (ADmPawn* P = Pawn()) PC->SetViewTarget(P);
}
