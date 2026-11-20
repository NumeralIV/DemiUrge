// DEMIURGE — Game/Flow/DmFlowScenarios.cpp : the integration scenarios. Owner: C10 integration.
//
//   playthrough   a whole game at high speed with a scripted god (FDmGodPolicy, "kind"), through the real
//                 screens, HUD, input path, sim, FX and audio: title -> setup -> genesis (molten, cooling,
//                 comets, seas, life wave, awakening) -> history at 8x plus fast-forward (early village,
//                 a disaster, war, golden-age city, orbit, the space age) -> save and load from the screens
//                 mid-game -> the Ark and its ending -> ending screen -> timelapse -> planet card -> title.
//                 Writes shots/ue-play-NN-<name>.png (with the interface) and logs "DmPlay" lines; exit code
//                 0 when every step happened. Switches: -playseed=<n> (default 21), -playname=<name>,
//                 -ffms=<ms> (fast-forward budget per frame, default 45), -maxyears=<y> (give up, default 5000).
//                 Framing (wave 3): the comets fall on a sunlit coast (the seas fill where the land is), the war is
//                 seen from a low oblique view between the battle and the town it is fought for, the Ark from the
//                 flow's launch camera at golden hour. Logs the HUD's toast merging and the hints it showed.
//   first-minutes a new world (genesis skipped) left alone at -fmspeed= (default 4x) for 600 years with the real HUD:
//                 the first-steps hints in order, each up 15 s at most, at least 4 different ones; the regard
//                 word stays "Watched" (the god did nothing); the guidance thread is up; shots of each hint and
//                 of a town in the Stone age (the edicts and how to wake them). Exit code = failures.
//   flow-check    the flow's state machine through the screens without rendering checks: title, new world
//                 (quick start), pause -> save -> exit to title -> continue -> load, ending -> timelapse ->
//                 keep playing; logs "DmFlowCheck" PASS / FAIL lines, exit code = failures.
//   perf-play     real-time play as a player sees it: a history world at 8x with the scripted god for -seconds=
//                 at camera stops (-stop=orbit,mid,low,close); logs frame / game / render / GPU
//                 averages and every frame over 50 ms ("DmPerf" lines).

#include "Game/Flow/DmFlow.h"
#include "Game/Flow/DmGodPolicy.h"

#include "Game/Camera/DmPawn.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmGameMode.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Game/Input/DmControl.h"
#include "Game/FX/DmFx.h"
#include "Game/Audio/DmAudioDirector.h"
#include "Game/Planet/DmPlanet.h"
#include "Game/UI/Screens/DmScreenSubsystem.h"
#include "Game/UI/Hud/DmHudActor.h"
#include "Game/UI/Hud/DmHudModel.h"
#include "Game/Input/DmInputRouter.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/Paths.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "Sim/Civ.h"
#include "Sim/Powers.h"
#include "Sim/Save.h"
#include "Sim/Society.h"
#include "Sim/Sim.h"
#include "Sim/State.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmPlay, Log, All);

namespace
{
	// ------------------------------------------------------------ shared helpers

	FVector SunPlanet(UDmSimSubsystem* S)
	{
		return S->GetPlanetToWorld().InverseTransformVectorNoScale(S->GetSunDirection()).GetSafeNormal();
	}

	/** Pin the sun so that Dir sees it ElevDeg above the horizon from compass azimuth AzDeg. */
	void PinSunFor(UDmSimSubsystem* S, const FVector& Dir, double ElevDeg, double AzDeg)
	{
		FVector E, N;
		DmCoords::Basis(Dir, E, N);
		const double El = FMath::DegreesToRadians(ElevDeg), Az = FMath::DegreesToRadians(AzDeg);
		const FVector Sun = (Dir * FMath::Sin(El) + (N * FMath::Cos(Az) + E * FMath::Sin(Az)) * FMath::Cos(El)).GetSafeNormal();
		double Lat, Lon;
		DmCoords::DirToLatLon(Sun, Lat, Lon);
		S->PinSunAt(Lat, Lon);
	}

	/** A sunlit coast to watch the seas fill: the direction whose surroundings are closest to half land, half sea
	 *  at the sea line the comets will raise the water to, with the sun 25..70 degrees above it. */
	FVector CoastView(const dm::World& W, const FVector& SunP, double SeaLine)
	{
		FVector Best = SunP;
		double BestScore = -1.0;
		const int32 N = W.n;
		for (int32 C = 0; C < N; C += 97)
		{
			const FVector D = DmCoords::CellDir(W, C);
			const double Lit = FVector::DotProduct(D, SunP);
			if (Lit < 0.42 || Lit > 0.94 || FMath::Abs(D.Z) > 0.8) continue;
			int32 Land = 0, All = 0;
			for (int32 K = (C * 7) % 13; K < N; K += 13)
			{
				const FVector Q = DmCoords::CellDir(W, K);
				if (FVector::DotProduct(Q, D) < 0.86) continue;
				++All;
				if (W.elev[size_t(K)] > SeaLine) ++Land;
			}
			if (All < 20) continue;
			const double Mix = 1.0 - FMath::Abs(double(Land) / All - 0.5) * 2.0;
			const double Score = Mix + 0.25 * (1.0 - FMath::Abs(Lit - 0.7));
			if (Score > BestScore) { BestScore = Score; Best = D; }
		}
		return Best;
	}

	/** Compass bearing (radians, clockwise from north) from direction A toward direction B. */
	double BearingTo(const FVector& A, const FVector& B)
	{
		FVector E, N;
		DmCoords::Basis(A, E, N);
		const FVector T = B - A * FVector::DotProduct(A, B);
		return FMath::Atan2(FVector::DotProduct(T, E), FVector::DotProduct(T, N));
	}

	int32 LargestSettlement(const dm::World& W, int32 MinTier = 0)
	{
		int32 Best = -1;
		double BestPop = -1;
		for (const dm::Settlement& S : W.settlements)
			if (!S.dead && S.tier >= MinTier && S.pop > BestPop) { BestPop = S.pop; Best = S.id; }
		return Best;
	}

	int32 LeadingCulture(const dm::World& W)
	{
		int Best = -1;
		for (const dm::Culture& C : W.cultures)
		{
			if (C.dead) continue;
			if (Best < 0) { Best = C.id; continue; }
			const dm::Culture& B = W.cultures[size_t(Best)];
			if (C.age > B.age || (C.age == B.age && C.pop > B.pop)) Best = C.id;
		}
		return Best;
	}

	// ------------------------------------------------------------ the playthrough

	struct FPlay
	{
		int32 Step = 0;
		int32 StepFrame = 0;
		double StepTime = 0.0;
		int32 ShotIndex = 0;
		TArray<FString> Done;          // steps that produced their shot
		TArray<FString> Missed;
		TUniquePtr<FDmGodPolicy> God;
		uint32 Seed = 21;
		FString Name = TEXT("Ithe");
		double FfMs = 45.0;
		double MaxYears = 5000.0;
		FRandomStream Rand{ 4242 };

		// genesis
		int32 Comets = 0, Seeds = 0;
		double NextAct = 0.0;
		bool bCometShot = false, bLifeShot = false;
		int32 CometShotFrame = -1;        // (verifier) step frame of the comet shot (the genesis save/load waits for it)

		// history events caught by the listener
		int32 DisasterCell = -1;  FString DisasterKind;
		int32 BattleCell = -1;    double BattleAt = -1.0;  int32 Battles = 0;  int32 BattleTown = -1;
		int32 WarArmy = -1;       int32 WarTown = -1;      FVector WarFocus = FVector::ZeroVector;
		int32 ArkCell = -1;       bool bArkLaunched = false; double ArkAt = 0.0;
		bool bEnded = false;
		FString EndingText;
		int32 AgeSeen = 0;
		double SpaceYear = -1.0;
		double SailYear = -1.0;

		// one-off history shots
		bool bSaveLoadTried = false, bGenesisSaveTried = false, bGenesisSaveLoad = false;
		bool bVillage = false, bDisaster = false, bWar = false, bGolden = false, bOrbit = false, bSpace = false, bSaveLoad = false;
		int32 Sub = 0;                 // sub-step of a history shot (0 = not started)
		int32 SubFrame = 0;
		int32 SubTarget = -1;
		double SubTime = 0.0;

		// frame statistics over the whole run (fast-forward frames are deliberately heavy and excluded)
		int32 NormalFrames = 0, NormalHitches = 0;
		double NormalWorstMs = 0.0;
		FDelegateHandle EventHandle;
		double T0 = 0.0;
	};
	TUniquePtr<FPlay> GPlay;

	FString PlayShot(FDmScenarioContext& Ctx, const FString& Name)
	{
		FPlay& P = *GPlay;
		const FString Path = FPaths::Combine(Ctx.Mode->ShotsDir(), FString::Printf(TEXT("ue-play-%02d-%s.png"), ++P.ShotIndex, *Name));
		Ctx.ShotPath(Path, true);
		const dm::World* W = Ctx.Sim->Sim();
		UE_LOG(LogDmPlay, Display, TEXT("DmPlay: shot %s (year %.0f, phase %s, age %d, %d settlements, pop %.0f, frame %d, %.0f s)"), *Path,
			W ? W->year : 0.0, W ? UTF8_TO_TCHAR(dm::toStr(W->phase)) : TEXT("-"), W ? W->age : 0, W ? int32(dm::civ::live(*W).size()) : 0,
			W ? W->stats.pop : 0.0, Ctx.Frame, FPlatformTime::Seconds() - P.T0);
		P.Done.Add(Name);
		return Path;
	}

	ADmPawn* PawnOf(FDmScenarioContext& Ctx) { return ADmPawn::Find(Ctx.World); }

	/** Before a framed shot: the god puts the brush down (no armed ring) and the last banner is dismissed, so the
	 *  subject is not hidden; the HUD itself stays in the picture. */
	void ClearForShot(FDmScenarioContext& Ctx)
	{
		if (UDmControlSubsystem* C = UDmControlSubsystem::Get(Ctx.World)) C->Disarm();
		if (ADmHud* H = ADmHud::Find(Ctx.World)) H->Context().Banner.Born = -100.0;
	}

	void Look(FDmScenarioContext& Ctx, const FVector& Dir, double Zoom, double HeadingDeg)
	{
		if (ADmPawn* Pw = PawnOf(Ctx)) { Pw->Rig().StopMotion(); Pw->Jump(Dir, Zoom, FMath::DegreesToRadians(HeadingDeg)); }
		if (ADmPlanet* Pl = ADmPlanet::Find(Ctx.World)) Pl->Settle(40);
	}

	void LookCell(FDmScenarioContext& Ctx, int32 Cell, double Zoom, double HeadingDeg)
	{
		const dm::World* W = Ctx.Sim->Sim();
		if (W && W->valid(Cell)) Look(Ctx, DmCoords::CellDir(*W, Cell), Zoom, HeadingDeg);
	}

	/** The war: a low oblique view across the battle toward the town it is fought for (so the armies, the smoke
	 *  and the town are in one frame), the town at the top of the screen, the sun from the side. */
	void LookWar(FDmScenarioContext& Ctx, int32 Cell, int32 Town)
	{
		const dm::World* W = Ctx.Sim->Sim();
		if (!W || !W->valid(Cell)) return;
		const FVector B = DmCoords::CellDir(*W, Cell);
		const dm::Settlement* S = dm::state::settlement(*W, Town);
		FVector Focus = B;
		double Heading = 0.0, Zoom = 0.62;
		if (S && !S->dead && W->valid(S->cell))
		{
			const FVector T = DmCoords::CellDir(*W, S->cell);
			const double SepKm = FMath::Acos(FMath::Clamp(FVector::DotProduct(B, T), -1.0, 1.0)) * 100.0;
			// the town fought for, close and tilted (about 4.6 km up, 47 degrees), looking the way the army came: the
			// town, the smoke of the battle at its walls and the land the army crossed (a top-down view far out
			// showed only farmland and a smoke puff)
			Focus = T;
			Zoom = 0.84;   // (verifier) was 0.76: the town fought for was a label at the top of a field of farmland
			if (SepKm > 0.2 && SepKm < 60.0) Heading = BearingTo(B, T);
		}
		if (ADmPawn* Pw = PawnOf(Ctx)) { Pw->Rig().StopMotion(); Pw->Jump(Focus, Zoom, Heading); }
		if (ADmPlanet* Pl = ADmPlanet::Find(Ctx.World)) Pl->Settle(40);
		PinSunFor(Ctx.Sim, Focus, 18.0, FMath::RadiansToDegrees(Heading) + 120.0);
		UE_LOG(LogDmPlay, Display, TEXT("DmPlay: war view: battle cell %d, town %d, zoom %.2f, heading %.0f"), Cell, Town, Zoom, FMath::RadiansToDegrees(Heading));
	}

	/** An army on the march against a town of a people it is at war with, the one closest to its target: the
	 *  unit id and the town (settlement id); -1 when no army marches. */
	int32 MarchingArmy(const dm::World& W, int32& OutTown)
	{
		int32 Best = -1;
		double BestD = 1e9;
		OutTown = -1;
		for (const dm::Unit& U : W.units)
		{
			if (U.type != dm::UnitType::Army || U.path.empty()) continue;
			const dm::Settlement* T = dm::state::settlement(W, U.to);
			if (!T || T->dead || T->culture == U.culture || !W.valid(T->cell)) continue;
			const int32 Here = U.path[size_t(FMath::Clamp(U.i, 0, int32(U.path.size()) - 1))];
			if (!W.valid(Here)) continue;
			const double D = FMath::Acos(FMath::Clamp(FVector::DotProduct(DmCoords::CellDir(W, Here), DmCoords::CellDir(W, T->cell)), -1.0, 1.0)) * 100.0;
			if (D < 3.0 || D > 45.0) continue;   // in the frame together, and not already there
			if (D < BestD) { BestD = D; Best = U.id; OutTown = T->id; }
		}
		return Best;
	}

	/** Run the sim fast: ticks (each preceded by the god) until the budget is spent or Stop says so. */
	int32 FastForward(FDmScenarioContext& Ctx, double BudgetMs, TFunctionRef<bool()> Stop)
	{
		const double T0 = FPlatformTime::Seconds();
		int32 N = 0;
		while ((FPlatformTime::Seconds() - T0) * 1000.0 < BudgetMs && !Stop())
		{
			Ctx.Sim->StepTicks(1);
			++N;
			if (!Ctx.Sim->Sim()) break;
		}
		return N;
	}

	void OnPlayEvent(const dm::Event& E, UDmSimSubsystem* Sim)
	{
		if (!GPlay.IsValid()) return;
		FPlay& P = *GPlay;
		const dm::World* W = Sim->Sim();
		if (!W) return;
		switch (E.type)
		{
		case dm::Ev::VolcanoStart:
			if (const dm::EvVolcanoStart* V = E.as<dm::EvVolcanoStart>())
				if (P.DisasterCell < 0 && W->phase == dm::Phase::History && W->year > 250) { P.DisasterCell = V->cell; P.DisasterKind = TEXT("volcano"); }
			break;
		case dm::Ev::MeteorImpact:
			if (const dm::EvMeteorImpact* M = E.as<dm::EvMeteorImpact>())
				if (P.DisasterCell < 0 && W->phase == dm::Phase::History && W->year > 250) { P.DisasterCell = M->cell; P.DisasterKind = TEXT("meteor"); }
			break;
		case dm::Ev::Battle:
			if (const dm::EvBattle* B = E.as<dm::EvBattle>())
			{
				++P.Battles;
				if (W->year > 500) { P.BattleCell = B->cell; P.BattleAt = FPlatformTime::Seconds(); P.BattleTown = B->settlement; }
			}
			break;
		case dm::Ev::ArkLaunched:
			if (const dm::EvArkLaunched* A = E.as<dm::EvArkLaunched>())
			{
				P.bArkLaunched = true;
				P.ArkAt = FPlatformTime::Seconds();
				const dm::Settlement* S = dm::state::settlement(*W, A->settlement);
				P.ArkCell = S ? S->cell : -1;
				// golden hour at the pad for the flow's launch camera (it looks across the pad with the sun to its side)
				if (S) PinSunFor(Sim, DmCoords::CellDir(*W, S->cell), 13.0, 230.0);
			}
			break;
		case dm::Ev::Ending:
			if (const dm::EvEnding* En = E.as<dm::EvEnding>())
			{
				P.bEnded = true;
				P.EndingText = FString::Printf(TEXT("%s / %s in year %.0f"), UTF8_TO_TCHAR(dm::toStr(En->type)), UTF8_TO_TCHAR(dm::toStr(En->mood)), En->year);
			}
			break;
		case dm::Ev::WorldAge:
			if (const dm::EvWorldAge* A = E.as<dm::EvWorldAge>())
			{
				P.AgeSeen = FMath::Max(P.AgeSeen, A->age);
				if (A->age >= 3 && P.SailYear < 0) P.SailYear = W->year;
				if (A->age >= 6 && P.SpaceYear < 0) P.SpaceYear = W->year;
				UE_LOG(LogDmPlay, Display, TEXT("DmPlay: world age %d in year %.0f"), A->age, W->year);
			}
			break;
		default: break;
		}
	}

	bool NextStep(FPlay& P) { ++P.Step; P.StepFrame = 0; P.StepTime = 0.0; return true; }

	/** A history shot: pause, frame the target, settle, shoot, unpin and resume. Returns true when finished. */
	bool HistoryShot(FDmScenarioContext& Ctx, const FString& Name, TFunctionRef<void()> Frame, int32 SettleFrames, bool bPause = true)
	{
		FPlay& P = *GPlay;
		if (P.Sub == 0)
		{
			P.Sub = 1; P.SubFrame = 0;
			if (bPause) Ctx.Sim->SetPaused(true);
			Frame();
			return false;
		}
		++P.SubFrame;
		if (P.Sub == 1 && P.SubFrame == SettleFrames - 2) ClearForShot(Ctx);
		if (P.Sub == 1 && P.SubFrame >= SettleFrames)
		{
			PlayShot(Ctx, Name);
			P.Sub = 2; P.SubFrame = 0;
			return false;
		}
		if (P.Sub == 2 && P.SubFrame >= 4)
		{
			P.Sub = 0;
			Ctx.Sim->UnpinSun();
			Ctx.Sim->SetPaused(false);
			Ctx.Sim->SetSpeed(8.f);
			return true;
		}
		return false;
	}

	void PlaySetup(FDmScenarioContext& Ctx)
	{
		GPlay = MakeUnique<FPlay>();
		FPlay& P = *GPlay;
		P.T0 = FPlatformTime::Seconds();
		int32 Seed = 21;
		if (FDmShotArgs::Value(TEXT("-playseed="), Seed)) P.Seed = uint32(Seed);
		FDmShotArgs::Value(TEXT("-playname="), P.Name);
		FDmShotArgs::Value(TEXT("-ffms="), P.FfMs);
		FDmShotArgs::Value(TEXT("-maxyears="), P.MaxYears);
		Ctx.HoldExit();
		// the game's own start: the title over a world of its own (a different seed than the one we will play)
		ADmFlow::PrepareTitleWorld(Ctx.Sim, 42, 320.0);
		Ctx.BuildScene(DmPartsAll);
		ADmFlow* F = Ctx.World->SpawnActor<ADmFlow>();
		F->bAutosave = false;
		F->bAutoSlowAllowed = false;
		F->EndingHoldSeconds = 7.0;
		F->StartTitle();
		Ctx.UseHarnessCamera(false);
		// offscreen runs have no OS cursor: park the pointer outside the view (no hover tip over the HUD)
		if (ADmPawn* Pw = ADmPawn::Find(Ctx.World)) if (Pw->Router) Pw->Router->SetVirtualPointer(FVector2D(-4000.0, -4000.0));
		UDmSimSubsystem* Sim = Ctx.Sim;
		P.EventHandle = Ctx.Sim->OnSimEvent.AddLambda([Sim](const dm::Event& E) { OnPlayEvent(E, Sim); });
		UE_LOG(LogDmPlay, Display, TEXT("DmPlay: start (seed %u, \"%s\", fast-forward %.0f ms per frame)"), P.Seed, *P.Name, P.FfMs);
	}

	void Finish(FDmScenarioContext& Ctx)
	{
		FPlay& P = *GPlay;
		Ctx.Sim->BeforeTick = nullptr;
		const TCHAR* Want[] = { TEXT("title"), TEXT("setup"), TEXT("genesis-molten"), TEXT("genesis-cooling"), TEXT("genesis-comets"), TEXT("genesis-seas"),
			TEXT("genesis-life-wave"), TEXT("genesis-awakening"), TEXT("early-village"), TEXT("disaster"), TEXT("war"), TEXT("golden-age-city"),
			TEXT("orbit-day"), TEXT("space-age"), TEXT("ark-launch"), TEXT("ending"), TEXT("timelapse"), TEXT("planet-card"), TEXT("title-again") };
		int32 Miss = 0;
		for (const TCHAR* Wn : Want)
			if (!P.Done.Contains(Wn)) { ++Miss; UE_LOG(LogDmPlay, Warning, TEXT("DmPlay: MISSING shot %s"), Wn); }
		if (P.God)
		{
			FString Uses;
			for (const TPair<FString, int32>& KV : P.God->ByPower) Uses += FString::Printf(TEXT(" %s %d"), *KV.Key, KV.Value);
			UE_LOG(LogDmPlay, Display, TEXT("DmPlay: god %d clicks, %d strokes, %d refused, %d edicts:%s"), P.God->Clicks, P.God->Strokes, P.God->Refused, P.God->Edicts, *Uses);
		}
		UE_LOG(LogDmPlay, Display, TEXT("DmPlay: genesis %d comets, %d seeds; battles %d; ending %s; save/load in history %s, in genesis %s"), P.Comets, P.Seeds, P.Battles,
			P.EndingText.IsEmpty() ? TEXT("none") : *P.EndingText, P.bSaveLoad ? TEXT("PASS") : TEXT("FAIL"), P.bGenesisSaveLoad ? TEXT("PASS") : TEXT("FAIL"));
		if (!P.bSaveLoad || !P.bGenesisSaveLoad || P.EndingText.IsEmpty()) ++Miss;
		UE_LOG(LogDmPlay, Display, TEXT("DmPlay: normal frames %d, worst %.1f ms, over 50 ms %d"), P.NormalFrames, P.NormalWorstMs, P.NormalHitches);
		if (ADmHud* H = ADmHud::Find(Ctx.World))
		{
			const FDmHudContext& X = H->Context();
			FString Hints;
			for (const FDmHudHintRecord& R : X.HintHistory) Hints += FString::Printf(TEXT(" %d (year %.0f, %.1f s)"), R.Index, R.Year, R.Shown);
			UE_LOG(LogDmPlay, Display, TEXT("DmPlay: hud toasts %d pushed -> %d rows (merging); hints:%s; guide steps done 0x%x"),
				X.ToastsPushed, X.ToastsCreated, Hints.IsEmpty() ? TEXT(" none (seen in an earlier run)") : *Hints, X.GuideDone);
		}
		UE_LOG(LogDmPlay, Display, TEXT("DmPlay: RESULT %s (%d shots, %d missing, %.0f s)"), Miss == 0 ? TEXT("PASS") : TEXT("FAIL"), P.Done.Num(), Miss, FPlatformTime::Seconds() - P.T0);
		Ctx.Sim->OnSimEvent.Remove(P.EventHandle);
		Ctx.SetExitCode(Miss);
		Ctx.Finish();
	}

	void PlayTick(FDmScenarioContext& Ctx, float Dt)
	{
		if (!GPlay.IsValid()) return;
		FPlay& P = *GPlay;
		++P.StepFrame;
		P.StepTime += Dt;
		UDmSimSubsystem* Sim = Ctx.Sim;
		UDmScreenSubsystem* Sc = UDmScreenSubsystem::Get(Ctx.World);
		UDmControlSubsystem* Ctl = UDmControlSubsystem::Get(Ctx.World);
		ADmFlow* Flow = ADmFlow::Find(Ctx.World);
		dm::World* W = Sim->SimMutable();
		if (!Sc || !Ctl || !Flow) { UE_LOG(LogDmPlay, Error, TEXT("DmPlay: missing screens / control / flow")); Ctx.SetExitCode(90); Ctx.Finish(); return; }

		auto Advance = [&]() { if (W) FastForward(Ctx, P.FfMs, [&]() { return Sim->Sim() == nullptr || Sim->Sim()->phase == dm::Phase::Ended || Sim->IsPaused(); }); };

		switch (P.Step)
		{
		// ---------------------------------------------------------------- title and setup
		case 0:
			if (P.StepFrame == 280) { PlayShot(Ctx, TEXT("title")); NextStep(P); }
			break;
		case 1:
			if (P.StepFrame == 2)
			{
				Sc->ShowSetup();
				Sc->SetupName = P.Name;
				Sc->SetupSeed = FString::FromInt(int32(P.Seed));
				Sc->RequestRefresh();
			}
			if (P.StepFrame == 40) PlayShot(Ctx, TEXT("setup"));
			if (P.StepFrame == 46)
			{
				FDmNewWorldRequest R;
				R.Name = P.Name; R.Seed = P.Seed; R.bRandomSeed = false; R.bQuickStart = false;
				if (!Sc->BeginWorld(R)) { UE_LOG(LogDmPlay, Error, TEXT("DmPlay: BeginWorld failed")); Ctx.SetExitCode(91); Ctx.Finish(); return; }
				NextStep(P);
			}
			break;

		// ---------------------------------------------------------------- genesis
		case 2:   // molten: the world glows; shoot early, then at half heat (cooling)
			if (!W) break;
			if (P.StepFrame == 3) Look(Ctx, SunPlanet(Sim).RotateAngleAxis(-35.0, FVector::UpVector), 0.0, 0.0);
			if (P.StepFrame == 70) PlayShot(Ctx, TEXT("genesis-molten"));
			if (P.StepFrame > 70 && W->phase == dm::Phase::Molten && W->heat <= 0.32 && !P.Done.Contains(TEXT("genesis-cooling")))
				PlayShot(Ctx, TEXT("genesis-cooling"));
			if (P.StepFrame > 70 && W->phase == dm::Phase::Molten && (P.StepFrame & 1)) Sim->StepTicks(1);   // about 1.5x
			if (W->phase == dm::Phase::Water) NextStep(P);
			break;
		case 3:   // water: comets (the flow armed "comet"; the god clicks like a player would)
		{
			if (!W) break;
			if (P.StepFrame == 2)
			{
				// a sunlit coast (half land, half sea at the sea line the comets will fill to), a little closer than the
				// whole disc so the comets streak in over the limb; the sun held while the seas fill
				const FVector Sun = SunPlanet(Sim);
				const FVector Dir = CoastView(*W, Sun, -0.3);
				Look(Ctx, Dir, 0.08, 0.0);
				PinSunFor(Sim, Dir, 38.0, 250.0);
				UE_LOG(LogDmPlay, Display, TEXT("DmPlay: comet view: sunlit coast, %.0f deg from the sun"), FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir, Sun), -1.0, 1.0))));
			}
			const dm::sim::genesis::Progress G = dm::sim::genesis::progress(*W);
			if (W->seaLevel >= -0.3 && G.canAdvance && W->disasters.empty() && P.bGenesisSaveTried)
			{
				const bool bOk = dm::sim::genesis::next(*W);   // the HUD's "Next" button (FDmHudControl::NextPhase)
				UE_LOG(LogDmPlay, Display, TEXT("DmPlay: water done: %d comets, sea %.2f, next %d"), P.Comets, W->seaLevel, bOk ? 1 : 0);
				Sim->UnpinSun();
				NextStep(P);
				break;
			}
			P.NextAct -= Dt;
			if (P.NextAct <= 0.0 && W->seaLevel < -0.3)
			{
				P.NextAct = 0.35;
				if (Ctl->GetArmedPower() != TEXT("comet")) Ctl->ArmPower(TEXT("comet"));
				// comets fall near the middle of the view (on the coast being watched) so the shot sees them land
				const FVector Fwd = PawnOf(Ctx) ? PawnOf(Ctx)->Rig().GetFocus() : FVector(1, 0, 0);
				for (int32 Try = 0; Try < 60; ++Try)
				{
					const int32 C = P.Rand.RandRange(0, W->n - 1);
					if (FVector::DotProduct(DmCoords::CellDir(*W, C), Fwd) < (Try < 40 ? 0.93 : 0.55)) continue;
					if (dm::powers::can(*W, "comet", C).ok && Ctl->UsePower(C, 0.f, true)) { ++P.Comets; break; }
				}
			}
			if (!P.bCometShot && P.Comets >= 7 && P.StepTime > 3.0) { P.bCometShot = true; P.CometShotFrame = P.StepFrame; PlayShot(Ctx, TEXT("genesis-comets")); }
			// save and load in the middle of genesis (comets still in flight) through the screens' slot path
			// (verifier) a few frames after the shot: the screenshot is read back a frame or more later, and when 9 comets
			// had already fallen the load ran in the same frame, so 2 runs in 3 photographed the reloaded world's bare
			// ocean sphere (no land, no comets) instead of the coast
			if (P.bCometShot && !P.bGenesisSaveTried && (P.Comets >= 9 || W->seaLevel >= -0.3) && P.StepFrame - P.CometShotFrame > 30)
			{
				P.bGenesisSaveTried = true;
				dm::save::EncodeOpts EO;
				std::vector<uint8_t> Before, After, Ref;
				dm::save::encode(*W, Before, EO);
				const dm::Phase PhaseBefore = W->phase;
				const double SeaBefore = W->seaLevel;
				const int32 Flying = int32(W->disasters.size());
				const bool bSaved = Sc->SaveToSlot(TEXT("playthrough-genesis"));
				const bool bLoaded = Sc->LoadFromSlot(TEXT("playthrough-genesis"));
				dm::World* L = Sim->SimMutable();
				if (L) dm::save::encode(*L, After, EO);
				if (std::unique_ptr<dm::World> R = dm::save::decode(Before.data(), Before.size())) { dm::sim::use(*R); dm::save::encode(*R, Ref, EO); }
				P.bGenesisSaveLoad = bSaved && bLoaded && L && L->phase == PhaseBefore && L->seaLevel == SeaBefore && int32(L->disasters.size()) == Flying && !Ref.empty() && After == Ref;
				UE_LOG(LogDmPlay, Display, TEXT("DmPlay: save/load in genesis (phase %s, sea %.2f, %d comets in flight): saved %d loaded %d %s"),
					UTF8_TO_TCHAR(dm::toStr(PhaseBefore)), SeaBefore, Flying, bSaved ? 1 : 0, bLoaded ? 1 : 0, P.bGenesisSaveLoad ? TEXT("PASS") : TEXT("FAIL"));
				W = L;
				if (UDmControlSubsystem* C2 = Ctl) C2->ArmPower(TEXT("comet"));
				if (!W) break;
			}
			Sim->StepTicks(1);
			break;
		}
		case 4:   // seas: the filled oceans from orbit
			if (P.StepFrame == 1) Sim->UnpinSun();
			if (P.StepFrame == 2) Look(Ctx, SunPlanet(Sim).RotateAngleAxis(-35.0, FVector::UpVector), 0.0, 0.0);
			if (P.StepFrame == 150) { PlayShot(Ctx, TEXT("genesis-seas")); NextStep(P); }
			break;
		case 5:   // life: seed the land; the green wave spreads
		{
			if (!W) break;
			const dm::sim::genesis::Progress G = dm::sim::genesis::progress(*W);
			if (G.canAdvance && P.bLifeShot)
			{
				const bool bOk = dm::sim::genesis::next(*W);
				UE_LOG(LogDmPlay, Display, TEXT("DmPlay: life done: %d seeds, veg %.2f, next %d"), P.Seeds, G.veg, bOk ? 1 : 0);
				NextStep(P);
				break;
			}
			if (P.StepFrame == 2)
			{
				// look at the largest land mass by the best site, low enough to see the wave
				const int32 Site = dm::civ::bestSite(*W);
				if (W->valid(Site)) Look(Ctx, DmCoords::CellDir(*W, Site), 0.2, 0.0);
				if (W->valid(Site)) PinSunFor(Sim, DmCoords::CellDir(*W, Site), 35.0, 130.0);
			}
			P.NextAct -= Dt;
			if (P.NextAct <= 0.0)
			{
				P.NextAct = 0.25;
				if (Ctl->GetArmedPower() != TEXT("seed")) Ctl->ArmPower(TEXT("seed"));
				const FVector Fwd = PawnOf(Ctx) ? PawnOf(Ctx)->Rig().GetFocus() : FVector(1, 0, 0);
				for (int32 Try = 0; Try < 40; ++Try)
				{
					const int32 C = P.Rand.RandRange(0, W->n - 1);
					const double Near = FVector::DotProduct(DmCoords::CellDir(*W, C), Fwd);
					if (P.Seeds < 10 && Near < 0.93) continue;   // the first seeds where the camera looks
					if (dm::powers::can(*W, "seed", C).ok && Ctl->UsePower(C, 0.f, true)) { ++P.Seeds; break; }
				}
			}
			if (!P.bLifeShot && P.Seeds >= 6 && P.StepTime > 2.2) { P.bLifeShot = true; PlayShot(Ctx, TEXT("genesis-life-wave")); }
			Sim->StepTicks(1);
			break;
		}
		case 6:   // people: the beacon over the best site, then the awakening
		{
			if (!W) break;
			const int32 Site = dm::civ::bestSite(*W);
			if (P.StepFrame == 2 && W->valid(Site))
			{
				Look(Ctx, DmCoords::CellDir(*W, Site), 0.36, 30.0);
				PinSunFor(Sim, DmCoords::CellDir(*W, Site), 22.0, 250.0);
			}
			if (P.StepFrame == 90 && W->valid(Site))
			{
				if (Ctl->GetArmedPower() != TEXT("awaken")) Ctl->ArmPower(TEXT("awaken"));
				const bool bOk = Ctl->UsePower(Site, 0.f, true);
				UE_LOG(LogDmPlay, Display, TEXT("DmPlay: awaken at cell %d: %d (phase %s)"), Site, bOk ? 1 : 0, UTF8_TO_TCHAR(dm::toStr(W->phase)));
			}
			if (P.StepFrame == 96) { if (ADmPawn* Pw = PawnOf(Ctx)) Pw->Rig().StopMotion(); }   // hold the view: the flow's fly-to waits for history
			if (P.StepFrame == 148) ClearForShot(Ctx);
			if (P.StepFrame == 150) PlayShot(Ctx, TEXT("genesis-awakening"));
			if (P.StepFrame == 156)
			{
				Sim->UnpinSun();
				if (W->phase != dm::Phase::History)
				{
					UE_LOG(LogDmPlay, Warning, TEXT("DmPlay: the awakening did not start history; genesis::skip"));
					dm::sim::genesis::SkipOpts O;
					O.sea = W->seaLevel;
					dm::sim::genesis::skip(*W, O);
				}
				P.God = MakeUnique<FDmGodPolicy>(P.Seed);
				UDmControlSubsystem* C = Ctl;
				FDmGodPolicy* God = P.God.Get();
				Sim->BeforeTick = [God, C](dm::World& Wd) { God->Before(Wd, C); };
				Sim->SetPaused(false);
				Sim->SetSpeed(8.f);
				NextStep(P);
			}
			break;
		}

		// ---------------------------------------------------------------- history
		case 7:
		{
			if (!Sim->Sim()) break;
			const dm::World& Wd = *Sim->Sim();
			if (Wd.year > P.MaxYears) { UE_LOG(LogDmPlay, Error, TEXT("DmPlay: no ending by year %.0f"), Wd.year); NextStep(P); break; }
			// a history shot in progress keeps its frame (SubTarget); otherwise the first shot that is due starts
			{
				// early village: the first settlement that has become a village (year >= 100)
				if (!P.bVillage && (P.Sub == 0 || P.SubTarget == 1) && Wd.year >= 100)
				{
					const int32 V = LargestSettlement(Wd, 1);
					if (V >= 0)
					{
						P.SubTarget = 1;
						const int32 Cell = Wd.settlements[size_t(V)].cell;
						if (HistoryShot(Ctx, TEXT("early-village"), [&]() { LookCell(Ctx, Cell, 0.93, 200.0); PinSunFor(Sim, DmCoords::CellDir(Wd, Cell), 20.0, 300.0); }, 220))
						{ P.bVillage = true; P.SubTarget = -1; }
						break;
					}
				}
				// save and load from the screens mid-game (year >= 700): the pause menu, a slot, back in
				if (!P.bSaveLoadTried && P.Sub == 0 && Wd.year >= 700)
				{
					// the persisted state must survive exactly: the world's bytes (fixed savedAt) before the save and
					// after the load are identical (the derived arrays are rebuilt by sim::use and not compared)
					P.bSaveLoadTried = true;
					dm::save::EncodeOpts EO;
					EO.snapshots = true;
					std::vector<uint8_t> BytesBefore, BytesAfter;
					dm::save::encode(Wd, BytesBefore, EO);
					const double YearBefore = Wd.year;
					const double Wd0Pop = Wd.stats.pop;
					const size_t Wd0Live = dm::civ::live(Wd).size();
					Sc->ShowPause();
					PlayShot(Ctx, TEXT("pause"));
					const bool bSaved = Sc->SaveToSlot(TEXT("playthrough"));
					Sc->Back();
					const bool bLoaded = Sc->LoadFromSlot(TEXT("playthrough"));
					const dm::World* L = Sim->Sim();
					if (L) dm::save::encode(*L, BytesAfter, EO);
					// the reference: the same bytes decoded in memory and made ready by sim::use (what a load does), so the
					// check proves the slot file and the screens' path load exactly what was saved
					std::vector<uint8_t> BytesRef;
					if (std::unique_ptr<dm::World> Ref = dm::save::decode(BytesBefore.data(), BytesBefore.size()))
					{
						dm::sim::use(*Ref);
						dm::save::encode(*Ref, BytesRef, EO);
					}
					const bool bSame = L && L->year == YearBefore && L->stats.pop == Wd0Pop && dm::civ::live(*L).size() == Wd0Live;
					P.bSaveLoad = bSaved && bLoaded && bSame && !BytesRef.empty() && BytesAfter == BytesRef;
					UE_LOG(LogDmPlay, Display, TEXT("DmPlay: save/load through the screens at year %.1f: saved %d loaded %d, %d bytes -> %d bytes, %s"),
						YearBefore, bSaved ? 1 : 0, bLoaded ? 1 : 0, int32(BytesBefore.size()), int32(BytesAfter.size()), P.bSaveLoad ? TEXT("PASS (year, people, towns and the loaded bytes identical to the saved world made ready)") : TEXT("FAIL"));
					Sim->SetPaused(false);
					Sim->SetSpeed(8.f);
					break;
				}
				// a disaster (the first natural volcano or impact after year 250)
				if (!P.bDisaster && P.DisasterCell >= 0 && (P.Sub == 0 || P.SubTarget == 2))
				{
					P.SubTarget = 2;
					const int32 Cell = P.DisasterCell;
					if (HistoryShot(Ctx, TEXT("disaster"), [&]() { LookCell(Ctx, Cell, 0.5, 20.0); Sim->SetSpeed(1.f); }, 150, false))
					{ P.bDisaster = true; P.SubTarget = -1; }
					break;
				}
				// war: frame the first battle after year 500, wait for the next one near it, shoot
				if (!P.bWar && P.BattleCell >= 0 && (P.Sub == 0 || P.SubTarget == 3))
				{
					P.SubTarget = 3;
					if (P.Sub == 0)
					{
						// an army marching on an enemy town (the war as a player meets it: soldiers on the road, the town
						// ahead); without one, the last battle and the town it was fought for. Slow time so the march is seen.
						P.Sub = 1; P.SubFrame = 0; P.SubTime = FPlatformTime::Seconds();
						P.SubTarget = 3;
						int32 Town = -1;
						P.WarArmy = MarchingArmy(Wd, Town);
						int32 From = P.BattleCell;
						if (P.WarArmy >= 0)
							for (const dm::Unit& U : Wd.units)
								if (U.id == P.WarArmy) { From = U.path[size_t(FMath::Clamp(U.i, 0, int32(U.path.size()) - 1))]; break; }
						P.WarTown = P.WarArmy >= 0 ? Town : P.BattleTown;
						Sim->SetSpeed(P.WarArmy >= 0 ? 0.25f : 1.f);
						// (verifier) no town panel in the war frame (one run showed a panel half-open over the view);
						// closed here, a second or more before the shot, so its close animation is over
						if (UDmControlSubsystem* Cs = UDmControlSubsystem::Get(Ctx.World)) Cs->Select(-1);
						LookWar(Ctx, From, P.WarTown);
						if (ADmPawn* Pw = PawnOf(Ctx)) P.WarFocus = Pw->Rig().GetFocus();
						UE_LOG(LogDmPlay, Display, TEXT("DmPlay: war: army %d marching on town %d"), P.WarArmy, P.WarTown);
						P.BattleAt = -1.0;
						break;
					}
					++P.SubFrame;
					const double Since = FPlatformTime::Seconds() - P.SubTime;
					// a battle in the frame (within 30 km of its centre), a moment after it, or the army close to the town
					const dm::Settlement* WT = dm::state::settlement(Wd, P.WarTown);
					const bool bNear = P.BattleCell >= 0 && Wd.valid(P.BattleCell) && FVector::DotProduct(DmCoords::CellDir(Wd, P.BattleCell), P.WarFocus) > FMath::Cos(0.08);
					const bool bFresh = P.BattleAt > 0.0 && FPlatformTime::Seconds() - P.BattleAt > 0.3 && bNear;
					// the army a few km short of the town: hold time there and frame the two together (an army crosses a
					// frame in about a second even at 1/4 speed, so the march is caught paused)
					if (P.Sub == 1 && P.WarArmy >= 0 && WT && Wd.valid(WT->cell))
						for (const dm::Unit& U : Wd.units)
							if (U.id == P.WarArmy && !U.path.empty())
							{
								const int32 Here = U.path[size_t(FMath::Clamp(U.i, 0, int32(U.path.size()) - 1))];
								const double Km = Wd.valid(Here) ? FMath::Acos(FMath::Clamp(FVector::DotProduct(DmCoords::CellDir(Wd, Here), DmCoords::CellDir(Wd, WT->cell)), -1.0, 1.0)) * 100.0 : 99.0;
								// (verifier) held closer (4 km at most, was 7), lower (zoom 0.78..0.88 by the gap, was 0.72) and from
								// the side (the march crosses the frame toward the town on the right): looking along the march put
								// the army at 6 km a speck and the town at the top of the screen, under the toasts
								if (Km <= 4.0 && Km >= 1.5)
								{
									Sim->SetPaused(true);
									if (UDmControlSubsystem* Cs = UDmControlSubsystem::Get(Ctx.World)) Cs->Select(-1);
									const FVector A = DmCoords::CellDir(Wd, Here), T = DmCoords::CellDir(Wd, WT->cell);
									const FVector Mid = (A * 0.5 + T * 0.5).GetSafeNormal();
									const double Zm = FMath::Clamp(0.93 - 0.035 * Km, 0.78, 0.88);
									const double Hd = BearingTo(A, T) - HALF_PI;
									if (ADmPawn* Pw = PawnOf(Ctx)) { Pw->Rig().StopMotion(); Pw->Jump(Mid, Zm, Hd); }
									if (ADmPlanet* Pl = ADmPlanet::Find(Ctx.World)) Pl->Settle(40);
									PinSunFor(Sim, Mid, 18.0, FMath::RadiansToDegrees(Hd) + 120.0);
									UE_LOG(LogDmPlay, Display, TEXT("DmPlay: war: the army is %.1f km from the town: held (zoom %.2f)"), Km, Zm);
									P.Sub = 3; P.SubFrame = 0;
								}
							}
					if (P.Sub == 3 && P.SubFrame == 48) ClearForShot(Ctx);
					if ((P.Sub == 3 && P.SubFrame >= 50) || (P.Sub == 1 && P.SubFrame > 60 && (bFresh || Since > 14.0)))
					{
						UE_LOG(LogDmPlay, Display, TEXT("DmPlay: war shot (%s)"), P.Sub == 3 ? TEXT("the army before the town") : (bFresh ? TEXT("a battle in view") : TEXT("time")));
						if (P.Sub == 1) ClearForShot(Ctx);
						PlayShot(Ctx, TEXT("war"));
						P.Sub = 2; P.SubFrame = 0;
					}
					else if (P.Sub == 2 && P.SubFrame >= 4)
					{
						P.Sub = 0; P.bWar = true; P.SubTarget = -1;
						Sim->UnpinSun();
						Sim->SetPaused(false);
						Sim->SetSpeed(8.f);
					}
					break;
				}
				// golden-age city: 150 years into the Age of Sail, the largest city at golden hour
				if (!P.bGolden && P.SailYear > 0 && Wd.year >= P.SailYear + 150 && (P.Sub == 0 || P.SubTarget == 4))
				{
					P.SubTarget = 4;
					const int32 Big = LargestSettlement(Wd, 2);
					const int32 Cell = Big >= 0 ? Wd.settlements[size_t(Big)].cell : -1;
					if (Cell >= 0)
					{
						if (HistoryShot(Ctx, TEXT("golden-age-city"), [&]() { LookCell(Ctx, Cell, 0.8, 330.0); PinSunFor(Sim, DmCoords::CellDir(Wd, Cell), 24.0, 250.0); }, 260))
						{ P.bGolden = true; P.SubTarget = -1; }
						break;
					}
				}
				// the whole planet by day (the orbit look), in the industrial age
				if (!P.bOrbit && Wd.age >= 4 && (P.Sub == 0 || P.SubTarget == 5))
				{
					P.SubTarget = 5;
					const int32 Lead = LeadingCulture(Wd);
					const dm::Settlement* Cap = Lead >= 0 ? dm::state::settlement(Wd, Wd.cultures[size_t(Lead)].capital) : nullptr;
					const FVector Dir = Cap ? DmCoords::CellDir(Wd, Cap->cell) : FVector(1, 0, 0);
					if (HistoryShot(Ctx, TEXT("orbit-day"), [&]() { Look(Ctx, Dir, 0.0, 0.0); PinSunFor(Sim, Dir, 55.0, 260.0); }, 240))
					{ P.bOrbit = true; P.SubTarget = -1; }
					break;
				}
				// the space age: the leading culture's capital region at night, its city lights from high above
				if (!P.bSpace && P.SpaceYear > 0 && Wd.year >= P.SpaceYear + 30 && (P.Sub == 0 || P.SubTarget == 6))
				{
					P.SubTarget = 6;
					const int32 Lead = LeadingCulture(Wd);
					const dm::Settlement* Cap = Lead >= 0 ? dm::state::settlement(Wd, Wd.cultures[size_t(Lead)].capital) : nullptr;
					const FVector Dir = Cap ? DmCoords::CellDir(Wd, Cap->cell) : FVector(1, 0, 0);
					if (HistoryShot(Ctx, TEXT("space-age"), [&]() { Look(Ctx, Dir, 0.0, 0.0); PinSunFor(Sim, Dir, -4.0, 270.0); }, 240))
					{ P.bSpace = true; P.SubTarget = -1; }
					break;
				}
			}
			// the ark: the flow flies to the launch; shoot while the plume climbs
			if (P.bArkLaunched && !P.Done.Contains(TEXT("ark-launch")))
			{
				// the flow cuts to its launch camera 1.2 s after the launch; the Ark is some 3 km up after 5 s
				if (FPlatformTime::Seconds() - P.ArkAt > 5.0)
				{
					const ADmFlow* Fl = ADmFlow::Find(Ctx.World);
					UE_LOG(LogDmPlay, Display, TEXT("DmPlay: ark launch camera %s"), Fl && Fl->IsCinematic() ? TEXT("on") : TEXT("OFF"));
					PlayShot(Ctx, TEXT("ark-launch"));
				}
				break;
			}
			if (P.bEnded || Wd.phase == dm::Phase::Ended)
			{
				if (!P.bArkLaunched && !P.Done.Contains(TEXT("ark-launch"))) { P.Missed.Add(TEXT("ark-launch")); }
				NextStep(P);
				break;
			}
			// the frame's normal sim run at 8x plus the fast-forward
			const double FrameMs = FApp::GetDeltaTime() * 1000.0;
			if (P.StepFrame > 3 && !Sim->IsPaused() && Sim->GetSpeed() >= 8.f) Advance();
			else if (P.StepFrame > 3)
			{
				++P.NormalFrames;
				P.NormalWorstMs = FMath::Max(P.NormalWorstMs, FrameMs);
				if (FrameMs > 50.0) ++P.NormalHitches;
			}
			if (P.StepFrame % 600 == 0)
				UE_LOG(LogDmPlay, Display, TEXT("DmPlay: year %.0f, age %d, %d settlements, pop %.0f, awe %.0f/%.0f, frame %.1f ms"),
					Wd.year, Wd.age, int32(dm::civ::live(Wd).size()), Wd.stats.pop, Wd.god.awe, Wd.god.aweMax, FrameMs);
			break;
		}

		// ---------------------------------------------------------------- ending, timelapse, card, title
		case 8:   // the flow holds on the launch, then opens the ending screen
			if (P.StepFrame == 1) Sim->UnpinSun();
			if (Sc->Current() == EDmScreen::Ending)
			{
				if (P.StepTime > 0.0 && !P.Done.Contains(TEXT("ending")) && P.Sub == 0) { P.Sub = 1; P.SubFrame = 0; }
				if (P.Sub == 1 && ++P.SubFrame >= 50) { PlayShot(Ctx, TEXT("ending")); P.Sub = 0; NextStep(P); }
			}
			else if (P.StepTime > 30.0) { UE_LOG(LogDmPlay, Error, TEXT("DmPlay: no ending screen")); NextStep(P); }
			break;
		case 9:
			if (P.StepFrame == 2) Sc->ShowTimelapse();
			if (P.StepFrame == 330) { PlayShot(Ctx, TEXT("timelapse")); }
			if (P.StepFrame == 336) { Sc->Back(); NextStep(P); }
			break;
		case 10:
			if (P.StepFrame == 10) Sc->ShowCard();
			if (Sc->Current() == EDmScreen::Card && P.Sub == 0) { P.Sub = 1; P.SubFrame = 0; }
			if (P.Sub == 1 && ++P.SubFrame == 60) PlayShot(Ctx, TEXT("planet-card"));
			if (P.Sub == 1 && P.SubFrame == 80)
			{
				FString Card;   // "Save as PNG" (after the shot: the message line rebuilds the page)
				if (Sc->ExportPlanetCard(Card)) UE_LOG(LogDmPlay, Display, TEXT("DmPlay: planet card exported to %s"), *Card);
				P.Sub = 0;
				NextStep(P);
			}
			if (P.StepFrame > 900) { UE_LOG(LogDmPlay, Error, TEXT("DmPlay: no card")); NextStep(P); }
			break;
		case 11:
			if (P.StepFrame == 2) Sc->ExitToTitle();
			if (P.StepFrame == 240) { PlayShot(Ctx, TEXT("title-again")); NextStep(P); }
			break;
		case 12:
			if (P.StepFrame == 30) Finish(Ctx);
			break;
		default: break;
		}
	}
}

static FDmScenarioRegistrar GDmScenarioPlaythrough(TEXT("playthrough"), TEXT("integration"),
	TEXT("a whole game at high speed with a scripted god through the real flow; shots/ue-play-NN-*.png"),
	[](FDmScenarioContext& Ctx) { PlaySetup(Ctx); },
	[](FDmScenarioContext& Ctx, float Dt) { PlayTick(Ctx, Dt); });

// ------------------------------------------------------------------ genesis-look: a molten / cooling world from orbit
// -seed=<n> (default 21) -gheat=<0..1> (cool until the heat is at most this; default 1 = fresh) -gzoom=<0..1>
static FDmScenarioRegistrar GDmScenarioGenesisLook(TEXT("genesis-look"), TEXT("integration"),
	TEXT("a new world in genesis from orbit at a given heat (-gheat=), over the day side"),
	[](FDmScenarioContext& Ctx)
	{
		{ int32 Sd = 21; FDmShotArgs::Value(TEXT("-gseed="), Sd); Ctx.Sim->CreateWorld(uint32(Sd), TEXT("Ithe"), false, 0.0); }
		double Heat = 1.0, Zoom = 0.0;
		FDmShotArgs::Value(TEXT("-gheat="), Heat);
		FDmShotArgs::Value(TEXT("-gzoom="), Zoom);
		int32 Guard = 0;
		while (Ctx.Sim->Sim() && Ctx.Sim->Sim()->phase == dm::Phase::Molten && Ctx.Sim->Sim()->heat > Heat && Guard++ < 200) Ctx.Sim->StepTicks(1);
		Ctx.Sim->SetPaused(true);
		Ctx.BuildScene(DmPartsAll & ~uint32(EDmPartKind::Audio));
		Ctx.UseHarnessCamera(false);
		if (ADmPawn* P = ADmPawn::Find(Ctx.World)) P->bAdoptHarnessView = false;
		Ctx.Log(FString::Printf(TEXT("genesis-look: heat %.2f phase %s"), Ctx.Sim->Sim()->heat, UTF8_TO_TCHAR(dm::toStr(Ctx.Sim->Sim()->phase))));
	},
	[](FDmScenarioContext& Ctx, float)
	{
		if (Ctx.Frame == 3)
		{
			double Zoom = 0.0;
			FDmShotArgs::Value(TEXT("-gzoom="), Zoom);
			if (ADmPawn* P = ADmPawn::Find(Ctx.World))
			{
				P->Jump(SunPlanet(Ctx.Sim).RotateAngleAxis(-35.0, FVector::UpVector), Zoom, 0.0);
				if (APlayerController* PC = Ctx.World->GetFirstPlayerController()) PC->SetViewTarget(P);
			}
		}
	});

// ------------------------------------------------------------------ perf-play: what a player's frame costs
// -seed=<n> (21) -pyear=<y> (history to run headless first, default 2400) -stop=orbit|mid|low|close|all
// -perfsec=<s> per stop (default 20) -pspeed=<x> (default 8): the real sim runs at that speed with the scripted
// god acting through the control state; every frame's game / render / RHI / GPU ms are kept and summarised.
namespace
{
	struct FPerfStop { const TCHAR* Name; double Zoom; double Heading; };
	const FPerfStop GPerfStops[] = { { TEXT("orbit"), 0.0, 0.0 }, { TEXT("mid"), 0.45, 0.0 }, { TEXT("low"), 0.75, 40.0 }, { TEXT("close"), 0.92, 120.0 } };

	struct FPerf
	{
		TArray<int32> Stops;
		int32 StopIdx = 0;
		double StopTime = 0.0;
		int32 Cell = -1;
		TUniquePtr<FDmGodPolicy> God;
		TArray<float> Frame, Game, Render, Gpu;
		int32 Hitches = 0;
		double Seconds = 20.0;
		float Speed = 8.f;
		int32 Fails = 0;
	};
	TUniquePtr<FPerf> GPerf;

	void PerfReport(FDmScenarioContext& Ctx)
	{
		FPerf& P = *GPerf;
		auto Pct = [](TArray<float> V, double Q) { if (V.Num() == 0) return 0.0; V.Sort(); return double(V[FMath::Clamp(int32(Q * (V.Num() - 1)), 0, V.Num() - 1)]); };
		auto Avg = [](const TArray<float>& V) { double S = 0; for (float X : V) S += X; return V.Num() ? S / V.Num() : 0.0; };
		const FPerfStop& St = GPerfStops[P.Stops[P.StopIdx]];
		const double AvgF = Avg(P.Frame);
		UE_LOG(LogDmPlay, Display, TEXT("DmPerf %s: %d frames, frame avg %.2f ms (%.0f fps) p95 %.2f max %.2f | game avg %.2f p95 %.2f | render avg %.2f p95 %.2f | gpu avg %.2f p95 %.2f | frames over 50 ms: %d | sim %.3f ms/tick"),
			St.Name, P.Frame.Num(), AvgF, AvgF > 0 ? 1000.0 / AvgF : 0.0, Pct(P.Frame, 0.95), Pct(P.Frame, 1.0), Avg(P.Game), Pct(P.Game, 0.95), Avg(P.Render), Pct(P.Render, 0.95), Avg(P.Gpu), Pct(P.Gpu, 0.95), P.Hitches, Ctx.Sim->GetSimMsPerTick());
		if (P.Hitches > 0) ++P.Fails;
		P.Frame.Reset(); P.Game.Reset(); P.Render.Reset(); P.Gpu.Reset(); P.Hitches = 0;
	}
}

static FDmScenarioRegistrar GDmScenarioPerfPlay(TEXT("perf-play"), TEXT("integration"),
	TEXT("real-time play at 8x with the scripted god at camera stops; DmPerf frame / game / render / GPU lines"),
	[](FDmScenarioContext& Ctx)
	{
		GPerf = MakeUnique<FPerf>();
		FPerf& P = *GPerf;
		int32 Seed = 21;
		FDmShotArgs::Value(TEXT("-seed="), Seed);
		double Year = 2400.0;
		FDmShotArgs::Value(TEXT("-pyear="), Year);
		FDmShotArgs::Value(TEXT("-perfsec="), P.Seconds);   // not -seconds= (the engine quits after that many seconds)
		double Sp = 8.0;
		if (FDmShotArgs::Value(TEXT("-pspeed="), Sp)) P.Speed = float(Sp);
		FString Stop = TEXT("all");
		FDmShotArgs::Value(TEXT("-stop="), Stop);
		for (int32 I = 0; I < UE_ARRAY_COUNT(GPerfStops); ++I) if (Stop == TEXT("all") || Stop.Contains(GPerfStops[I].Name)) P.Stops.Add(I);
		Ctx.HoldExit();
		Ctx.Sim->CreateWorld(uint32(Seed), TEXT("Ithe"), true, 0.0);
		Ctx.BuildScene(DmPartsAll);
		ADmFlow* F = Ctx.World->SpawnActor<ADmFlow>();
		F->bAutosave = false;
		F->bAutoSlowAllowed = false;
		Ctx.UseHarnessCamera(false);
		if (ADmPawn* Pw = ADmPawn::Find(Ctx.World)) Pw->bAdoptHarnessView = false;
		P.God = MakeUnique<FDmGodPolicy>(uint32(Seed));
		UDmControlSubsystem* C = UDmControlSubsystem::Get(Ctx.World);
		FDmGodPolicy* God = P.God.Get();
		Ctx.Sim->BeforeTick = [God, C](dm::World& Wd) { God->Before(Wd, C); };
		const double T0 = FPlatformTime::Seconds();
		while (Ctx.Sim->Sim() && Ctx.Sim->Sim()->year < Year && Ctx.Sim->Sim()->phase == dm::Phase::History) Ctx.Sim->StepTicks(16);
		const dm::World* W = Ctx.Sim->Sim();
		const int32 Big = W ? LargestSettlement(*W, 0) : -1;
		P.Cell = Big >= 0 ? W->settlements[size_t(Big)].cell : 0;
		Ctx.Log(FString::Printf(TEXT("DmPerf: world at year %.0f (age %d, %d settlements, pop %.0f) in %.1f s; city cell %d"), W ? W->year : 0.0, W ? W->age : 0,
			W ? int32(dm::civ::live(*W).size()) : 0, W ? W->stats.pop : 0.0, FPlatformTime::Seconds() - T0, P.Cell));
		Ctx.Sim->SetPaused(false);
		Ctx.Sim->SetSpeed(P.Speed);
		F->StartTitle();
		if (UDmScreenSubsystem* Sc = UDmScreenSubsystem::Get(Ctx.World)) Sc->CloseAll();
		Ctx.Sim->SetPaused(false);
		Ctx.Sim->SetSpeed(P.Speed);
	},
	[](FDmScenarioContext& Ctx, float Dt)
	{
		if (!GPerf.IsValid()) return;
		FPerf& P = *GPerf;
		if (Ctx.Frame == 4)
		{
			if (ADmFlow* F = ADmFlow::Find(Ctx.World)) F->EnterPlayForTests();
			Ctx.Sim->SetPaused(false);
			Ctx.Sim->SetSpeed(P.Speed);
		}
		if (Ctx.Frame < 5 || P.StopIdx >= P.Stops.Num()) return;
		const FPerfStop& St = GPerfStops[P.Stops[P.StopIdx]];
		if (P.StopTime == 0.0)
		{
			const dm::World* W = Ctx.Sim->Sim();
			if (W && W->valid(P.Cell))
			{
				LookCell(Ctx, P.Cell, St.Zoom, St.Heading);
				PinSunFor(Ctx.Sim, DmCoords::CellDir(*W, P.Cell), 35.0, 200.0);
			}
		}
		const double PrevStop = P.StopTime;
		P.StopTime += Dt;
		// -profilegpu: one GPU profile per stop into the log (ProfileGPU), half way through
		if (FDmShotArgs::Param(TEXT("profilegpu")) && PrevStop < 3.0 + P.Seconds * 0.5 && P.StopTime >= 3.0 + P.Seconds * 0.5)
		{
			GEngine->Exec(Ctx.World, TEXT("r.ProfileGPU.ShowUI 0"));
			GEngine->Exec(Ctx.World, TEXT("ProfileGPU"));
		}
		if (P.StopTime > 3.0)   // the first seconds settle (LOD, towns, nature tiles)
		{
			double G = 0, R = 0, Rh = 0, Gp = 0;
			Ctx.Mode->GetLastUnitMs(G, R, Rh, Gp);
			const float Ms = float(FApp::GetDeltaTime() * 1000.0);
			P.Frame.Add(Ms); P.Game.Add(float(G)); P.Render.Add(float(R)); P.Gpu.Add(float(Gp));
			if (Ms > 50.f)
			{
				++P.Hitches;
				UE_LOG(LogDmPlay, Display, TEXT("DmPerf %s: HITCH %.1f ms (game %.1f render %.1f gpu %.1f) year %.1f"), St.Name, Ms, G, R, Gp, Ctx.Sim->Sim() ? Ctx.Sim->Sim()->year : 0.0);
			}
		}
		if (P.StopTime > 3.0 + P.Seconds)
		{
			const FString Path = FPaths::Combine(Ctx.Mode->ShotsDir(), FString::Printf(TEXT("ue-perf-%s.png"), St.Name));
			Ctx.ShotPath(Path, true);
			PerfReport(Ctx);
			P.StopTime = 0.0;
			if (++P.StopIdx >= P.Stops.Num())
			{
				Ctx.Sim->BeforeTick = nullptr;
				UE_LOG(LogDmPlay, Display, TEXT("DmPerf: RESULT %s (%d stops with frames over 50 ms)"), P.Fails == 0 ? TEXT("PASS") : TEXT("FAIL"), P.Fails);
				Ctx.SetExitCode(P.Fails);
				Ctx.Finish();
			}
		}
	});

// ------------------------------------------------------------------ powers-check: every power through the whole path
// A history world (seed 21, quick start) run headless with the scripted god to -pyear= (default 2900, so every power is
// unlocked); then each power of the palette in turn: the camera frames a target, the power is armed and used through
// the control state (UDmControlSubsystem, the input path), and for 2.5 s the check counts what came back: the sim's
// events (power:used and the rest), the FX module's new effects / particles, the audio director's cues, the HUD's
// armed power and chronicle feed. One shot per power with the HUD (shots/ue-power-<id>.png). "DmPowers" lines; exit
// code = powers that failed (sim, FX and audio must all answer).
namespace
{
	struct FPowersCheck
	{
		TArray<int32> Order;           // indices into dm::powers::kPowers
		int32 Idx = -1;
		double T = 0.0;
		int32 Cell = -1;
		TMap<FString, int32> Events;   // event name -> count since this power started
		int32 Fx0 = 0, Particles0 = 0, Cues0 = 0, Feed0 = 0, FxMax = 0, PartMax = 0;
		bool bShot = false;
		int32 Fails = 0;
		FString Armed;
		FDelegateHandle Handle;
	};
	TUniquePtr<FPowersCheck> GPow;

	int32 TargetFor(const dm::World& W, const dm::powers::PowerDef& D, FRandomStream& R)
	{
		// settlement powers: the largest town; everything else: open land a few cells from the largest town of the
		// smallest culture (wrath stays away from the people who will build the Ark), sea powers: the nearest sea
		int32 Big = LargestSettlement(W, 0);
		int32 Small = -1; double SmallPop = 1e30;
		for (const dm::Settlement& S : W.settlements) if (!S.dead && S.culture >= 0 && W.cultures[size_t(S.culture)].pop < SmallPop) { SmallPop = W.cultures[size_t(S.culture)].pop; Small = S.id; }
		const int32 Base = (D.group == dm::powers::Group::Wrath && Small >= 0) ? Small : Big;
		if (Base < 0) return 0;
		const int32 Home = W.settlements[size_t(Base)].cell;
		if (D.target == dm::powers::Target::Settlement)
		{
			// (wave3-final) a settlement power the largest town cannot take (v3 Consecrate: a shrine without a temple,
			// or a capital's temple with no Wonder this age) goes to the most populous town that can take it
			if (dm::powers::can(W, D.id, Home).ok) return Home;
			int32 Best = -1; double BestPop = -1.0;
			for (const dm::Settlement& S : W.settlements)
				if (!S.dead && W.valid(S.cell) && S.pop > BestPop && dm::powers::can(W, D.id, S.cell).ok) { BestPop = S.pop; Best = S.cell; }
			return Best >= 0 ? Best : Home;
		}
		std::vector<int32_t> Ring;
		W.geo->ring(Home, 3, Ring);
		for (int32 Try = 0; Try < 200 && !Ring.empty(); ++Try)
		{
			const int32 C = Ring[size_t(R.RandRange(0, int32(Ring.size()) - 1))];
			if (dm::powers::can(W, D.id, C).ok) return C;
		}
		return Home;
	}
}

static FDmScenarioRegistrar GDmScenarioPowersCheck(TEXT("powers-check"), TEXT("integration"),
	TEXT("every power through control -> sim -> FX / audio / HUD; DmPowers lines and shots/ue-power-<id>.png"),
	[](FDmScenarioContext& Ctx)
	{
		GPow = MakeUnique<FPowersCheck>();
		Ctx.HoldExit();
		Ctx.Sim->CreateWorld(21, TEXT("Ithe"), true, 0.0);
		Ctx.BuildScene(DmPartsAll);
		ADmFlow* F = Ctx.World->SpawnActor<ADmFlow>();
		F->bAutosave = false;
		F->bAutoSlowAllowed = false;
		Ctx.UseHarnessCamera(false);
		if (ADmPawn* Pw = ADmPawn::Find(Ctx.World)) { Pw->bAdoptHarnessView = false; if (Pw->Router) Pw->Router->SetVirtualPointer(FVector2D(-4000.0, -4000.0)); }
		double Year = 2900.0;
		FDmShotArgs::Value(TEXT("-pyear="), Year);
		FDmGodPolicy God(21);
		UDmControlSubsystem* C = UDmControlSubsystem::Get(Ctx.World);
		Ctx.Sim->BeforeTick = [&God, C](dm::World& Wd) { God.Before(Wd, C); };
		while (Ctx.Sim->Sim() && Ctx.Sim->Sim()->year < Year && Ctx.Sim->Sim()->phase == dm::Phase::History) Ctx.Sim->StepTicks(16);
		Ctx.Sim->BeforeTick = nullptr;
		if (C) C->Disarm();
		for (int32 I = 0; I < dm::powers::kPowerCount; ++I)
			if (dm::powers::kPowers[I].group != dm::powers::Group::Genesis)   // awaken is a genesis / people:lost power (playthrough)
				GPow->Order.Add(I);
		UDmSimSubsystem* Sim = Ctx.Sim;
		GPow->Handle = Ctx.Sim->OnSimEvent.AddLambda([](const dm::Event& E) { if (GPow.IsValid()) GPow->Events.FindOrAdd(UTF8_TO_TCHAR(E.name()))++; });
		Ctx.Sim->SetPaused(false);
		Ctx.Sim->SetSpeed(1.f);
		Ctx.Log(FString::Printf(TEXT("DmPowers: world at year %.0f, age %d, %d powers to check"), Sim->Sim()->year, Sim->Sim()->age, GPow->Order.Num()));
	},
	[](FDmScenarioContext& Ctx, float Dt)
	{
		if (!GPow.IsValid() || Ctx.Frame < 30) return;
		FPowersCheck& P = *GPow;
		dm::World* W = Ctx.Sim->SimMutable();
		UDmControlSubsystem* C = UDmControlSubsystem::Get(Ctx.World);
		ADmFx* Fx = ADmFx::Find(Ctx.World);
		ADmAudioDirector* Au = ADmAudioDirector::Find(Ctx.World);
		ADmHud* Hud = ADmHud::Find(Ctx.World);
		if (!W || !C) return;
		static FRandomStream R(77);
		if (P.Idx < 0 || P.T > 3.2)
		{
			if (P.Idx >= 0)
			{
				// report the power just checked
				const dm::powers::PowerDef& D = dm::powers::kPowers[P.Order[P.Idx]];
				const int32 Used = P.Events.FindRef(TEXT("power:used"));
				const int32 FxD = P.FxMax - P.Fx0, PartD = P.PartMax - P.Particles0;
				const int32 CueD = Au ? Au->CuesPlayed - P.Cues0 : 0;
				const int32 FeedD = P.Events.FindRef(TEXT("chronicle:entry"));
				FString Ev;
				for (const TPair<FString, int32>& KV : P.Events) if (!KV.Key.StartsWith(TEXT("ui:")) && !KV.Key.StartsWith(TEXT("unit:"))) Ev += FString::Printf(TEXT(" %s %d"), *KV.Key, KV.Value);
				const bool bNoWar = P.Cell < 0;   // the Omen of Peace needs a war somewhere: none now, nothing to check
				const bool bPass = bNoWar || (Used > 0 && (FxD > 0 || PartD > 0) && CueD > 0 && P.Armed == UTF8_TO_TCHAR(D.id));
				if (!bPass) ++P.Fails;
				UE_LOG(LogDmPlay, Display, TEXT("DmPowers %s: HUD armed \"%s\"; sim:%s; FX +%d effects +%d particles; audio +%d cues; chronicle entries %d -> %s"),
					UTF8_TO_TCHAR(D.id), *P.Armed, *Ev, FxD, PartD, CueD, FeedD, bNoWar ? TEXT("N/A (no war)") : bPass ? TEXT("PASS") : TEXT("FAIL"));
			}
			if (++P.Idx >= P.Order.Num())
			{
				C->Disarm();
				Ctx.Sim->OnSimEvent.Remove(P.Handle);
				UE_LOG(LogDmPlay, Display, TEXT("DmPowers: RESULT %s (%d of %d powers failed)"), P.Fails == 0 ? TEXT("PASS") : TEXT("FAIL"), P.Fails, P.Order.Num());
				Ctx.SetExitCode(P.Fails);
				Ctx.Finish();
				GPow.Reset();
				return;
			}
			const dm::powers::PowerDef& D = dm::powers::kPowers[P.Order[P.Idx]];
			P.T = 0.0; P.bShot = false; P.Events.Reset();
			W->god.awe = FMath::Max(W->god.awe, W->god.aweMax);   // scenario-only: every power affordable
			// (wave3-final) scenario-only: the scripted god may already have raised this age's Wonder, which leaves
			// Consecrate nothing to do at year 2900 (every shrine has its temple); forget it so a capital can take it
			if (FCStringAnsi::Strcmp(D.id, "consecrate") == 0) W->vars.num.erase("powers_wonderAge");
			P.Cell = TargetFor(*W, D, R);
			if (FCStringAnsi::Strcmp(D.id, "peace") == 0)
			{
				// the Omen of Peace goes to a town of a culture at war
				P.Cell = -1;
				for (const dm::Settlement& S : W->settlements)
				{
					if (S.dead || S.culture < 0 || P.Cell >= 0) continue;
					for (const dm::Culture& O : W->cultures) if (!O.dead && O.id != S.culture && dm::society::atWar(*W, S.culture, O.id)) { P.Cell = S.cell; break; }
				}
				if (P.Cell < 0) { P.T = 99.0; return; }
			}
			LookCell(Ctx, P.Cell, D.group == dm::powers::Group::Wrath ? 0.5 : 0.62, 0.0);
			PinSunFor(Ctx.Sim, DmCoords::CellDir(*W, P.Cell), 35.0, 220.0);
			P.Fx0 = P.FxMax = Fx ? Fx->GetEffectCount() : 0;
			P.Particles0 = P.PartMax = Fx ? Fx->GetParticleCount() : 0;
			P.Cues0 = Au ? Au->CuesPlayed : 0;
			P.Feed0 = Hud ? Hud->Context().Feed.Num() : 0;
			C->ArmPower(UTF8_TO_TCHAR(D.id));
			P.Armed = C->GetArmedPower();
			if (D.brush) { C->SetBrush(0.06f); C->UsePower(P.Cell, 0.f, true); }
			else C->UsePower(P.Cell, 0.f, true);
			return;
		}
		const dm::powers::PowerDef& D = dm::powers::kPowers[P.Order[P.Idx]];
		const double Prev = P.T;
		P.T += Dt;
		if (D.brush && P.T < 1.0) C->UsePower(P.Cell, Dt, false);   // a held brush stroke
		if (Fx) { P.FxMax = FMath::Max(P.FxMax, Fx->GetEffectCount()); P.PartMax = FMath::Max(P.PartMax, Fx->GetParticleCount()); }
		if (!P.bShot && Prev < 1.3 && P.T >= 1.3)
		{
			P.bShot = true;
			Ctx.ShotPath(FPaths::Combine(Ctx.Mode->ShotsDir(), FString::Printf(TEXT("ue-power-%s.png"), UTF8_TO_TCHAR(D.id))), true);
		}
	});

// ------------------------------------------------------------------ first-minutes: a new player who does nothing
// (design review finding 12) A new world, genesis skipped, nobody touching anything, at -fmspeed= (default 4x)
// until year -fmyears= (default 600), with the real flow and HUD (hints and the guide reset first). Checks:
// at least 4 different first-steps hints, each up at most 15 s, shown in teaching order when they arrive
// together; the regard word stays "Watched" (the god did nothing they saw); the guidance thread shows its first
// step. Shots (with the interface): ue-fm-hint-<n>.png as each hint appears, ue-fm-camp.png (a town's panel in
// the first age: the edicts dimmed with how to wake them), ue-fm-thread.png. Exit code = failures.
namespace
{
	struct FFirstMinutes
	{
		double Speed = 4.0, Years = 600.0;
		int32 Seen = 0;                     // hint records looked at
		TSet<int32> Shot;                   // hints already photographed
		int32 PendingShot = -1;  double PendingAt = 0.0;
		bool bCampShot = false, bThreadShot = false, bDone = false;
		int32 CampFrame = -1;
		int32 BelovedYears = 0, WatchedYears = 0, LastYear = -1;
		int32 MaxShownBreach = 0;
		double T0 = 0.0;
	};
	TUniquePtr<FFirstMinutes> GFm;

	void FmShot(FDmScenarioContext& Ctx, const FString& Name)
	{
		const FString Path = FPaths::Combine(Ctx.Mode->ShotsDir(), FString::Printf(TEXT("ue-fm-%s.png"), *Name));
		Ctx.ShotPath(Path, true);
		const dm::World* W = Ctx.Sim->Sim();
		UE_LOG(LogDmPlay, Display, TEXT("DmFirst: shot %s (year %.0f)"), *Path, W ? W->year : 0.0);
	}
}

static FDmScenarioRegistrar GDmScenarioFirstMinutes(TEXT("first-minutes"), TEXT("integration"),
	TEXT("a new world left alone for 600 years: the first-steps hints (>= 4, each <= 15 s, in order), the regard word, the guide"),
	[](FDmScenarioContext& Ctx)
	{
		GFm = MakeUnique<FFirstMinutes>();
		FFirstMinutes& F = *GFm;
		F.T0 = FPlatformTime::Seconds();
		FDmShotArgs::Value(TEXT("-fmspeed="), F.Speed);
		FDmShotArgs::Value(TEXT("-fmyears="), F.Years);
		int32 Seed = 21;
		FDmShotArgs::Value(TEXT("-fmseed="), Seed);
		Ctx.HoldExit();
		Ctx.Sim->CreateWorld(uint32(Seed), TEXT("Ithe"), true, 0.0);
		Ctx.BuildScene(DmPartsAll);
		ADmFlow* Fl = Ctx.World->SpawnActor<ADmFlow>();
		Fl->bAutosave = false;
		Fl->bAutoSlowAllowed = false;
		Fl->EnterPlayForTests();
		Ctx.UseHarnessCamera(false);
		if (ADmPawn* Pw = ADmPawn::Find(Ctx.World))
		{
			Pw->bAdoptHarnessView = false;
			if (APlayerController* PC = Ctx.World->GetFirstPlayerController()) PC->SetViewTarget(Pw);
			if (Pw->Router) Pw->Router->SetVirtualPointer(FVector2D(-4000.0, -4000.0));
		}
		if (ADmHud* H = ADmHud::Find(Ctx.World))
		{
			FDmHudContext& X = H->Context();
			X.ResetHints();
			X.ResetGuide();
			X.HintHistory.Reset();
			X.bHintsEnabled = true;
		}
		Ctx.Sim->SetPaused(false);
		Ctx.Sim->SetSpeed(float(F.Speed));
		UE_LOG(LogDmPlay, Display, TEXT("DmFirst: start (seed %d, %.0fx, until year %.0f)"), Seed, F.Speed, F.Years);
	},
	[](FDmScenarioContext& Ctx, float Dt)
	{
		if (!GFm.IsValid() || GFm->bDone) return;
		FFirstMinutes& F = *GFm;
		const dm::World* W = Ctx.Sim->Sim();
		ADmHud* H = ADmHud::Find(Ctx.World);
		if (!W || !H) return;
		FDmHudContext& X = H->Context();
		const double Now = FPlatformTime::Seconds();
		// the regard word once a year
		const int32 Y = int32(W->year);
		if (Y != F.LastYear && !X.M.bGenesis)
		{
			F.LastYear = Y;
			if (X.M.RegardWord == TEXT("Beloved")) ++F.BelovedYears;
			if (X.M.RegardWord == TEXT("Watched")) ++F.WatchedYears;
		}
		// a hint appeared: photograph it a moment later (its fade-in done)
		while (F.Seen < X.HintHistory.Num())
		{
			const FDmHudHintRecord& R = X.HintHistory[F.Seen++];
			UE_LOG(LogDmPlay, Display, TEXT("DmFirst: hint %d appears in year %.0f: \"%s\""), R.Index, R.Year, FDmHudContext::HintText(R.Index));
			if (!F.Shot.Contains(R.Index) && F.PendingShot < 0) { F.PendingShot = R.Index; F.PendingAt = Now + 1.2; }
		}
		if (F.PendingShot >= 0 && Now >= F.PendingAt)
		{
			if (X.HintShown == F.PendingShot) FmShot(Ctx, FString::Printf(TEXT("hint-%d"), F.PendingShot));
			F.Shot.Add(F.PendingShot);
			F.PendingShot = -1;
		}
		// the guidance thread, then a town's panel in the first age (the player would click the camp)
		if (!F.bThreadShot && X.bGuideVisible && W->year > 30 && F.PendingShot < 0 && X.HintShown < 0)
		{
			F.bThreadShot = true;
			FmShot(Ctx, TEXT("thread"));
		}
		if (!F.bCampShot && F.bThreadShot && W->year > 60 && F.PendingShot < 0)
		{
			if (F.CampFrame < 0)
			{
				int32 Best = -1;
				for (const dm::Settlement& S : W->settlements) if (!S.dead && (Best < 0 || S.pop > W->settlements[size_t(Best)].pop)) Best = S.id;
				if (Best >= 0) { X.Control.Select(Best); F.CampFrame = Ctx.Frame; }
			}
			else if (Ctx.Frame - F.CampFrame == 30)
			{
				FmShot(Ctx, TEXT("camp"));
				F.bCampShot = true;
			}
		}
		if (F.bCampShot && Ctx.Frame - F.CampFrame == 36) X.Control.Select(-1);
		if (W->year < F.Years && Now - F.T0 < 900.0) return;

		// results
		F.bDone = true;
		Ctx.Sim->SetPaused(true);
		if (X.HintShown >= 0 && X.HintHistory.Num()) X.HintHistory.Last().Shown = X.Now - X.HintHistory.Last().Born;
		TSet<int32> Distinct;
		double MaxShown = 0.0;
		bool bOrdered = true;
		FString List;
		for (int32 I = 0; I < X.HintHistory.Num(); ++I)
		{
			const FDmHudHintRecord& R = X.HintHistory[I];
			Distinct.Add(R.Index);
			MaxShown = FMath::Max(MaxShown, R.Shown);
			// hints that waited together come out lowest first: a hint that started within 1 s of the previous one
			// leaving (it was queued) must not have a lower index than one still waiting then; check pairwise order
			if (I > 0 && R.Born - (X.HintHistory[I - 1].Born + X.HintHistory[I - 1].Shown) < 0.05 && R.Index < X.HintHistory[I - 1].Index && R.Year - X.HintHistory[I - 1].Year < 1.0) bOrdered = false;
			List += FString::Printf(TEXT(" %d (year %.0f, %.1f s)"), R.Index, R.Year, R.Shown);
		}
		int32 Fails = 0;
		auto Check = [&](bool bOk, const FString& What) { UE_LOG(LogDmPlay, Display, TEXT("DmFirst: %s %s"), *What, bOk ? TEXT("PASS") : TEXT("FAIL")); if (!bOk) ++Fails; };
		Check(Distinct.Num() >= 4, FString::Printf(TEXT("%d different hints in %.0f years (want >= 4):%s"), Distinct.Num(), W->year, *List));
		Check(MaxShown <= 15.0, FString::Printf(TEXT("longest hint %.1f s (want <= 15)"), MaxShown));
		Check(bOrdered, TEXT("hints that arrived together showed in teaching order"));
		Check(F.BelovedYears == 0 && F.WatchedYears > 0, FString::Printf(TEXT("regard word with no deeds: Watched %d years, Beloved %d years (want Watched, never Beloved)"), F.WatchedYears, F.BelovedYears));
		Check(F.bThreadShot, TEXT("the guidance thread was up"));
		UE_LOG(LogDmPlay, Display, TEXT("DmFirst: toasts %d pushed -> %d rows; RESULT %s (%d failed, %.0f s)"), X.ToastsPushed, X.ToastsCreated, Fails == 0 ? TEXT("PASS") : TEXT("FAIL"), Fails, Now - F.T0);
		Ctx.SetExitCode(Fails);
		Ctx.Finish();
	});
