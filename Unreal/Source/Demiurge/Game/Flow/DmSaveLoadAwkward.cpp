// DEMIURGE — Game/Flow/DmSaveLoadAwkward.cpp : save and load at awkward moments through the real flow.
// Owner: wave3-final (whole-game verification, 2026-11-20).
//
//   saveload-awkward   a new world through the title and setup (genesis, not a quick start), then save + load
//                      through the screens' slot path (UDmScreenSubsystem::SaveToSlot / LoadFromSlot) at moments a
//                      player can reach but nobody tested: the molten world; comets in flight; the life wave; the
//                      people phase before and right after the awakening; in the middle of a held Rain press; with
//                      the pause menu open; in a 16x fast-forward frame; an army on the march; the frame after a
//                      battle; the frame after a world age; loading a genesis slot in the middle of history (and
//                      back); two loads in one frame and a missing slot; the Ark's launch (the flow's cinematic
//                      running); the ending pending (the camera hold before the ending screen); the ending screen
//                      itself; reloading the Ark save so the ending must happen again; exit to title, then load.
//                      Every save/load must give back exactly the saved world (the bytes of the loaded world equal
//                      the saved bytes decoded and made ready by sim::use), and the world must go on afterwards
//                      (time passes in history; the in-flight comets land; the ending fires again). Logs
//                      "DmAwk" lines, shots/ue-saveload-awkward-<case>.png after some loads, exit code = failures.
//                      Switches: -awkseed=<n> (default 21), -awkffms=<ms> (fast-forward per frame, default 45).

#include "Game/Flow/DmFlow.h"
#include "Game/Flow/DmGodPolicy.h"

#include "Game/Camera/DmPawn.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmGameMode.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Game/Input/DmControl.h"
#include "Game/Input/DmInputRouter.h"
#include "Game/UI/Screens/DmScreenSubsystem.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"

#include <vector>

#include "Sim/Civ.h"
#include "Sim/Powers.h"
#include "Sim/Save.h"
#include "Sim/Sim.h"
#include "Sim/State.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmAwk, Log, All);

namespace
{
	struct FAwkPending
	{
		FString Name;
		int32 Frames = 0;          // frames left until the follow-up check
		double Year = 0.0;
		dm::Phase Phase = dm::Phase::Molten;
		int32 Flying = 0;          // disasters in flight at the save
		bool bWantTime = false;    // history, not paused: time must pass
	};

	struct FAwk
	{
		int32 Step = 0;
		int32 StepFrame = 0;
		uint32 Seed = 21;
		double FfMs = 45.0;
		TUniquePtr<FDmGodPolicy> God;
		FRandomStream Rand{ 777 };
		TArray<FString> Passed, Failed;
		TArray<FAwkPending> Follow;
		FDelegateHandle EventHandle;
		double T0 = 0.0;

		// events
		bool bBattle = false, bAge = false, bArk = false, bEnding = false;
		int32 Endings = 0;
		int32 PressCell = -1, PressFrame = 0, PressOkBefore = 0, PressOkAfter = 0;
		double AweBeforePress = 0.0, AweAtSave = 0.0;
		bool bHistorySaved = false;
		int32 CometsDropped = 0, Seeds = 0;
		bool bBattleDone = false, bAgeDone = false, bMarchDone = false, bArkDone = false, bPendingDone = false;
		bool bScreenDone = false, bReplayed = false, bFinished = false, bArkStarted = false, bArkBuildDone = false;
		int32 SubFrame = 0;
	};
	TUniquePtr<FAwk> GAwk;

	void Result(const FString& Name, bool bOk, const FString& Detail)
	{
		FAwk& A = *GAwk;
		(bOk ? A.Passed : A.Failed).Add(Name);
		UE_LOG(LogDmAwk, Display, TEXT("DmAwk: %s %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *Name, *Detail);
	}

	/** Save to a slot and load it back through the screens; the loaded world must be exactly the saved one. */
	bool SaveLoad(FDmScenarioContext& Ctx, const FString& Name, const FString& Slot, int32 FollowFrames = 90)
	{
		FAwk& A = *GAwk;
		UDmSimSubsystem* Sim = Ctx.Sim;
		UDmScreenSubsystem* Sc = UDmScreenSubsystem::Get(Ctx.World);
		const dm::World* W = Sim->Sim();
		if (!W || !Sc) { Result(Name, false, TEXT("no world or screens")); return false; }
		dm::save::EncodeOpts EO;
		EO.snapshots = true;
		std::vector<uint8_t> Before, After, Ref;
		dm::save::encode(*W, Before, EO);
		const double Year = W->year;
		const dm::Phase Phase = W->phase;
		const int32 Flying = int32(W->disasters.size());
		const int32 Units = int32(W->units.size());
		const int32 Live = int32(dm::civ::live(*W).size());
		const bool bPausedBefore = Sim->IsPaused();
		const double T0 = FPlatformTime::Seconds();
		const bool bSaved = Sc->SaveToSlot(Slot);
		const double T1 = FPlatformTime::Seconds();
		const bool bLoaded = bSaved && Sc->LoadFromSlot(Slot);
		const double T2 = FPlatformTime::Seconds();
		const dm::World* L = Sim->Sim();
		if (L) dm::save::encode(*L, After, EO);
		if (std::unique_ptr<dm::World> R = dm::save::decode(Before.data(), Before.size())) { dm::sim::use(*R); dm::save::encode(*R, Ref, EO); }
		const bool bSame = L && L != W && L->year == Year && L->phase == Phase && int32(L->disasters.size()) == Flying
			&& int32(L->units.size()) == Units && int32(dm::civ::live(*L).size()) == Live;
		const bool bBytes = !Ref.empty() && After == Ref;
		const ADmFlow* F = ADmFlow::Find(Ctx.World);
		const bool bOk = bSaved && bLoaded && bSame && bBytes;
		Result(Name, bOk, FString::Printf(TEXT("year %.2f phase %s, %d in flight, %d units, %d towns, paused %d; saved %d (%.0f ms) loaded %d (%.0f ms); state %s, bytes %d -> %d %s; flow %s, screen %d, paused now %d"),
			Year, UTF8_TO_TCHAR(dm::toStr(Phase)), Flying, Units, Live, bPausedBefore ? 1 : 0, bSaved ? 1 : 0, (T1 - T0) * 1000.0, bLoaded ? 1 : 0, (T2 - T1) * 1000.0,
			bSame ? TEXT("same") : TEXT("DIFFERENT"), int32(Before.size()), int32(After.size()), bBytes ? TEXT("identical") : TEXT("DIFFER"),
			F ? ADmFlow::StateName(F->GetState()) : TEXT("-"), int32(Sc->Current()), Sim->IsPaused() ? 1 : 0));
		if (bOk && FollowFrames > 0)
		{
			FAwkPending P;
			P.Name = Name; P.Frames = FollowFrames; P.Year = Year; P.Phase = Phase; P.Flying = Flying;
			P.bWantTime = Phase == dm::Phase::History && !Sim->IsPaused();
			A.Follow.Add(P);
		}
		return bOk;
	}

	void TickFollow(FDmScenarioContext& Ctx)
	{
		FAwk& A = *GAwk;
		const dm::World* W = Ctx.Sim->Sim();
		for (int32 I = A.Follow.Num() - 1; I >= 0; --I)
		{
			FAwkPending& P = A.Follow[I];
			if (--P.Frames > 0) continue;
			FString Why;
			bool bOk = W != nullptr;
			if (W && P.bWantTime && W->phase == dm::Phase::History && !(W->year > P.Year)) { bOk = false; Why = TEXT("time did not pass"); }
			if (W && P.Flying > 0 && P.Phase == dm::Phase::Water && W->phase == dm::Phase::Water && int32(W->disasters.size()) >= P.Flying) { bOk = false; Why = TEXT("the disasters in flight never resolved"); }
			Result(P.Name + TEXT(" (then)"), bOk, FString::Printf(TEXT("year %.2f -> %.2f, phase %s, %d in flight (was %d) %s"), P.Year, W ? W->year : -1.0,
				W ? UTF8_TO_TCHAR(dm::toStr(W->phase)) : TEXT("-"), W ? int32(W->disasters.size()) : -1, P.Flying, *Why));
			A.Follow.RemoveAt(I);
		}
	}

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

	void Next(FAwk& A) { ++A.Step; A.StepFrame = 0; A.SubFrame = 0; }

	void OnAwkEvent(const dm::Event& E)
	{
		if (!GAwk.IsValid()) return;
		FAwk& A = *GAwk;
		switch (E.type)
		{
		case dm::Ev::Battle: A.bBattle = true; break;
		case dm::Ev::WorldAge: A.bAge = true; break;
		case dm::Ev::ArkLaunched: A.bArk = true; break;
		case dm::Ev::ArkStarted: A.bArkStarted = true; break;
		case dm::Ev::Ending: A.bEnding = true; ++A.Endings; break;
		default: break;
		}
	}

	void AttachGod(FDmScenarioContext& Ctx, bool bOn)
	{
		FAwk& A = *GAwk;
		if (!bOn || !A.God) { Ctx.Sim->BeforeTick = nullptr; return; }
		FDmGodPolicy* God = A.God.Get();
		UDmControlSubsystem* C = UDmControlSubsystem::Get(Ctx.World);
		Ctx.Sim->BeforeTick = [God, C](dm::World& Wd) { God->Before(Wd, C); };
	}

	void AwkFinish(FDmScenarioContext& Ctx)
	{
		FAwk& A = *GAwk;
		Ctx.Sim->BeforeTick = nullptr;
		Ctx.Sim->OnSimEvent.Remove(A.EventHandle);
		FString Fails;
		for (const FString& S : A.Failed) Fails += TEXT(" ") + S;
		UE_LOG(LogDmAwk, Display, TEXT("DmAwk: RESULT %s (%d passed, %d failed:%s; %d endings; %.0f s)"), A.Failed.Num() == 0 ? TEXT("PASS") : TEXT("FAIL"),
			A.Passed.Num(), A.Failed.Num(), Fails.IsEmpty() ? TEXT(" none") : *Fails, A.Endings, FPlatformTime::Seconds() - A.T0);
		Ctx.SetExitCode(A.Failed.Num());
		Ctx.Finish();
	}

	void AwkSetup(FDmScenarioContext& Ctx)
	{
		GAwk = MakeUnique<FAwk>();
		FAwk& A = *GAwk;
		A.T0 = FPlatformTime::Seconds();
		int32 Seed = 21;
		if (FDmShotArgs::Value(TEXT("-awkseed="), Seed)) A.Seed = uint32(Seed);
		FDmShotArgs::Value(TEXT("-awkffms="), A.FfMs);
		Ctx.HoldExit();
		ADmFlow::PrepareTitleWorld(Ctx.Sim, 42, 240.0);
		Ctx.BuildScene(DmPartsAll);
		ADmFlow* F = Ctx.World->SpawnActor<ADmFlow>();
		F->bAutosave = false;
		F->bAutoSlowAllowed = false;
		F->EndingHoldSeconds = 3.0;
		F->StartTitle();
		Ctx.UseHarnessCamera(false);
		if (ADmPawn* Pw = ADmPawn::Find(Ctx.World)) if (Pw->Router) Pw->Router->SetVirtualPointer(FVector2D(-4000.0, -4000.0));
		A.EventHandle = Ctx.Sim->OnSimEvent.AddLambda([](const dm::Event& E) { OnAwkEvent(E); });
		UE_LOG(LogDmAwk, Display, TEXT("DmAwk: start (seed %u)"), A.Seed);
	}

	/** ArmPower toggles (arming the armed power disarms): arm only when another power is armed. */
	void ArmIf(UDmControlSubsystem* C, const TCHAR* Id) { if (C && C->GetArmedPower() != Id) C->ArmPower(Id); }

	int32 FindCell(FAwk& A, const dm::World& W, const char* Power)
	{
		for (int32 Try = 0; Try < 400; ++Try)
		{
			const int32 C = A.Rand.RandRange(0, W.n - 1);
			if (dm::powers::can(W, Power, C).ok) return C;
		}
		return -1;
	}

	void AwkTick(FDmScenarioContext& Ctx, float Dt)
	{
		if (!GAwk.IsValid()) return;
		FAwk& A = *GAwk;
		++A.StepFrame;
		UDmSimSubsystem* Sim = Ctx.Sim;
		UDmScreenSubsystem* Sc = UDmScreenSubsystem::Get(Ctx.World);
		UDmControlSubsystem* Ctl = UDmControlSubsystem::Get(Ctx.World);
		ADmFlow* Flow = ADmFlow::Find(Ctx.World);
		if (!Sc || !Ctl || !Flow) { UE_LOG(LogDmAwk, Error, TEXT("DmAwk: missing screens / control / flow")); Ctx.SetExitCode(90); Ctx.Finish(); return; }
		TickFollow(Ctx);
		dm::World* W = Sim->SimMutable();
		if ((Ctx.Frame % 120) == 0)   // heartbeat: the shot script kills a game whose log is silent
			UE_LOG(LogDmAwk, Display, TEXT("DmAwk: step %d frame %d, year %.1f, phase %s, %.1f fps"), A.Step, A.StepFrame, W ? W->year : -1.0,
				W ? UTF8_TO_TCHAR(dm::toStr(W->phase)) : TEXT("-"), Dt > 0.f ? 1.f / Dt : 0.f);
		const double Elapsed = FPlatformTime::Seconds() - A.T0;
		if (Elapsed > 3600.0 && A.Step < 40) { Result(TEXT("time-limit"), false, FString::Printf(TEXT("step %d after %.0f s"), A.Step, Elapsed)); A.Step = 40; A.StepFrame = 0; }

		auto Advance = [&](bool bStopOnEvents)
		{
			FastForward(Ctx, A.FfMs, [&]()
			{
				const dm::World* X = Sim->Sim();
				return X == nullptr || X->phase == dm::Phase::Ended || Sim->IsPaused() || (bStopOnEvents && (A.bBattle || A.bAge || A.bArk || A.bEnding));
			});
		};

		switch (A.Step)
		{
		case 0:   // the title, then a new world through setup (genesis)
			if (A.StepFrame == 90)
			{
				FDmNewWorldRequest R;
				R.Name = TEXT("Awkward"); R.Seed = A.Seed; R.bRandomSeed = false; R.bQuickStart = false;
				R.Calamity = EDmCalamity::Wild;
				if (!Sc->BeginWorld(R)) { Result(TEXT("begin"), false, TEXT("BeginWorld failed")); A.Step = 40; break; }
				// (code-health U-1) the setup screen's calamity choice must reach the world
				const dm::World* Nw = Sim->Sim();
				Result(TEXT("setup-calamity-applied"), Nw && Nw->settings.calamity == R.CalamityMultiplier(),
					FString::Printf(TEXT("Wild -> settings.calamity %.2f (want %.2f)"), Nw ? Nw->settings.calamity : -1.0, R.CalamityMultiplier()));
				Next(A);
			}
			break;
		case 1:   // molten
			if (!W) break;
			if (A.StepFrame == 40) SaveLoad(Ctx, TEXT("molten"), TEXT("awk-molten"));
			if (A.StepFrame > 40) Sim->StepTicks(20);
			if (W->phase == dm::Phase::Water) Next(A);
			break;
		case 2:   // water: comets in flight, then fill the seas
		{
			if (!W) break;
			if (A.StepFrame == 5)
			{
				ArmIf(Ctl, TEXT("comet"));
				for (int32 K = 0; K < 4; ++K) { const int32 C = FindCell(A, *W, "comet"); if (C >= 0 && Ctl->UsePower(C, 0.f, true)) ++A.CometsDropped; }
			}
			if (A.StepFrame == 6)
			{
				const bool bFlying = !W->disasters.empty();
				if (!bFlying) Result(TEXT("comets-in-flight (setup)"), false, FString::Printf(TEXT("%d comets dropped, none in flight"), A.CometsDropped));
				SaveLoad(Ctx, TEXT("comets-in-flight"), TEXT("awk-water"), 240);
				W = Sim->SimMutable();
				// the flow re-arms the phase's genesis power on a load; ArmPower toggles, so it must not disarm it
				Result(TEXT("comet-armed-after-load"), Ctl->GetArmedPower() == TEXT("comet"), FString::Printf(TEXT("armed \"%s\" after loading a Water save in the Water phase"), *Ctl->GetArmedPower()));
				ArmIf(Ctl, TEXT("comet"));
			}
			if (A.StepFrame > 6 && W)
			{
				const dm::sim::genesis::Progress G = dm::sim::genesis::progress(*W);
				if (W->seaLevel < -0.3 && (A.StepFrame % 6) == 0) { ArmIf(Ctl, TEXT("comet")); const int32 C = FindCell(A, *W, "comet"); if (C >= 0) Ctl->UsePower(C, 0.f, true); }
				Sim->StepTicks(2);
				if (W->seaLevel >= -0.3 && G.canAdvance && W->disasters.empty() && A.StepFrame > 260) { dm::sim::genesis::next(*W); Next(A); }
				if (A.StepFrame > 4000) { Result(TEXT("water"), false, TEXT("the seas never filled")); A.Step = 40; }
			}
			break;
		}
		case 3:   // life: seeds, a save mid-wave
		{
			if (!W) break;
			if (A.StepFrame % 8 == 0 && A.Seeds < 8)
			{
				ArmIf(Ctl, TEXT("seed"));
				const int32 C = FindCell(A, *W, "seed");
				if (C >= 0 && Ctl->UsePower(C, 0.f, true)) ++A.Seeds;
			}
			if (A.StepFrame == 70) { SaveLoad(Ctx, TEXT("life-wave"), TEXT("awk-life")); W = Sim->SimMutable(); }
			if (W) Sim->StepTicks(2);
			if (W && A.StepFrame > 80 && dm::sim::genesis::progress(*W).canAdvance) { dm::sim::genesis::next(*W); Next(A); }
			if (A.StepFrame > 4000) { Result(TEXT("life"), false, TEXT("the life wave never finished")); A.Step = 40; }
			break;
		}
		case 4:   // people: before the awakening, and in the same frame as it
		{
			if (!W) break;
			if (A.StepFrame == 20) { SaveLoad(Ctx, TEXT("people-before-awaken"), TEXT("awk-people")); W = Sim->SimMutable(); }
			if (A.StepFrame == 60 && W)
			{
				const int32 Site = dm::civ::bestSite(*W);
				ArmIf(Ctl, TEXT("awaken"));
				const bool bOk = W->valid(Site) && Ctl->UsePower(Site, 0.f, true);
				if (!bOk) Result(TEXT("awaken"), false, FString::Printf(TEXT("awaken at %d refused (phase %s)"), Site, UTF8_TO_TCHAR(dm::toStr(W->phase))));
				SaveLoad(Ctx, TEXT("just-awakened"), TEXT("awk-awaken"));
				W = Sim->SimMutable();
			}
			if (A.StepFrame == 100)
			{
				if (W && W->phase != dm::Phase::History)
				{
					Result(TEXT("history-started"), false, FString::Printf(TEXT("phase %s after the awakening; genesis::skip"), W ? UTF8_TO_TCHAR(dm::toStr(W->phase)) : TEXT("-")));
					dm::sim::genesis::SkipOpts O; O.sea = W->seaLevel; dm::sim::genesis::skip(*W, O);
				}
				A.God = MakeUnique<FDmGodPolicy>(A.Seed);
				AttachGod(Ctx, true);
				Sim->SetPaused(false);
				Sim->SetSpeed(8.f);
				Next(A);
			}
			break;
		}
		case 5:   // history at 8x until year 60, then a held Rain press with a save/load in its middle
		{
			if (!W) break;
			if (W->year < 60.0) { Advance(false); A.bBattle = A.bAge = false; break; }
			if (A.SubFrame == 0)
			{
				AttachGod(Ctx, false);
				ArmIf(Ctl, TEXT("rain"));
				const std::vector<int32_t> Live = dm::civ::live(*W);
				A.PressCell = Live.empty() ? -1 : W->settlements[size_t(Live[0])].cell;
				A.AweBeforePress = W->god.awe;
			}
			++A.SubFrame;
			if (A.PressCell >= 0)
			{
				// a load in history drops the brush (the flow disarms): the player picks Rain again and presses on
				if (A.SubFrame == 9)
				{
					UE_LOG(LogDmAwk, Display, TEXT("DmAwk: after the mid-press load the armed power is \"%s\" (history loads disarm); re-arming Rain"), *Ctl->GetArmedPower());
					ArmIf(Ctl, TEXT("rain"));
				}
				const bool bUsed = Ctl->UsePower(A.PressCell, Dt, A.SubFrame == 1 || A.SubFrame == 9);
				(A.SubFrame <= 8 ? A.PressOkBefore : A.PressOkAfter) += bUsed ? 1 : 0;
			}
			if (A.SubFrame == 8) { A.AweAtSave = W->god.awe; SaveLoad(Ctx, TEXT("mid-press"), TEXT("awk-press")); W = Sim->SimMutable(); }
			if (A.SubFrame == 24 && W)
			{
				Result(TEXT("press-after-load"), A.PressOkBefore > 0 && A.PressOkAfter > 0, FString::Printf(TEXT("Rain over cell %d: %d of 8 strokes taken before the save, %d of 16 after the load; Awe %.1f at the start, %.1f at the save, %.1f now"),
					A.PressCell, A.PressOkBefore, A.PressOkAfter, A.AweBeforePress, A.AweAtSave, W->god.awe));
				Ctl->Disarm();
				AttachGod(Ctx, true);
				Next(A);
			}
			break;
		}
		case 6:   // the pause menu open
			if (A.StepFrame == 2) Sc->ShowPause();
			if (A.StepFrame == 10) SaveLoad(Ctx, TEXT("pause-menu-open"), TEXT("awk-pause"));
			if (A.StepFrame == 12)
			{
				if (Sc->IsOpen()) Sc->CloseAll();
				Sim->SetPaused(false);
				Sim->SetSpeed(16.f);
				Next(A);
			}
			break;
		case 7:   // a 16x fast-forward frame: save in the frame that ticked
			Advance(false);
			if (A.StepFrame == 30)
			{
				SaveLoad(Ctx, TEXT("fast-forward-16x"), TEXT("awk-history"));
				A.bHistorySaved = true;
				Sim->SetSpeed(8.f);
				Next(A);
			}
			break;
		case 8:   // a genesis slot in the middle of history, then back
			if (A.StepFrame == 2)
			{
				A.Follow.Reset();   // the world is replaced: earlier follow-ups no longer apply
				const bool bOk = Sc->LoadFromSlot(TEXT("awk-water"));
				const dm::World* L = Sim->Sim();
				Result(TEXT("load-genesis-in-history"), bOk && L && L->phase == dm::Phase::Water, FString::Printf(TEXT("loaded %d, phase %s, flow %s"), bOk ? 1 : 0,
					L ? UTF8_TO_TCHAR(dm::toStr(L->phase)) : TEXT("-"), ADmFlow::StateName(Flow->GetState())));
			}
			if (A.StepFrame == 60) Ctx.Shot(TEXT("genesis-in-history"));
			if (A.StepFrame == 70)
			{
				const bool bOk = Sc->LoadFromSlot(TEXT("awk-history"));
				const dm::World* L = Sim->Sim();
				Result(TEXT("back-to-history"), bOk && L && L->phase == dm::Phase::History, FString::Printf(TEXT("loaded %d, phase %s, year %.1f, flow %s"), bOk ? 1 : 0,
					L ? UTF8_TO_TCHAR(dm::toStr(L->phase)) : TEXT("-"), L ? L->year : -1.0, ADmFlow::StateName(Flow->GetState())));
				Sim->SetPaused(false);
				Sim->SetSpeed(8.f);
				AttachGod(Ctx, true);
			}
			if (A.StepFrame == 130) { Ctx.Shot(TEXT("history-again")); Next(A); }
			break;
		case 9:   // two loads in one frame; a missing slot
		{
			if (A.StepFrame != 2) break;
			A.Follow.Reset();
			const bool b1 = Sc->LoadFromSlot(TEXT("awk-history"));
			const dm::World* L1 = Sim->Sim();
			const bool b2 = Sc->LoadFromSlot(TEXT("awk-press"));
			const dm::World* L2 = Sim->Sim();
			const bool bMissing = Sc->LoadFromSlot(TEXT("awk-no-such-slot"));
			const dm::World* L3 = Sim->Sim();
			Result(TEXT("double-load-and-missing"), b1 && b2 && !bMissing && L1 != L2 && L3 == L2, FString::Printf(TEXT("load %d, load %d (year %.1f), missing %d, world kept %d"),
				b1 ? 1 : 0, b2 ? 1 : 0, L2 ? L2->year : -1.0, bMissing ? 1 : 0, L3 == L2 ? 1 : 0));
			Sc->LoadFromSlot(TEXT("awk-history"));
			Sim->SetPaused(false);
			Sim->SetSpeed(8.f);
			AttachGod(Ctx, true);
			A.bBattle = A.bAge = A.bArk = A.bEnding = false;
			Next(A);
			break;
		}
		case 10:  // history to the Ark: saves on a march, after a battle, after an age, at the launch
		{
			if (!W) break;
			if (A.bBattle && !A.bBattleDone) { A.bBattleDone = true; SaveLoad(Ctx, TEXT("after-battle"), TEXT("awk-battle")); W = Sim->SimMutable(); }
			else if (A.bAge && !A.bAgeDone && W->age >= 2) { A.bAgeDone = true; SaveLoad(Ctx, TEXT("after-world-age"), TEXT("awk-age")); W = Sim->SimMutable(); }
			else if (!A.bMarchDone)
			{
				for (const dm::Unit& U : W->units)
					if (U.type == dm::UnitType::Army && !U.path.empty() && U.i > 0 && U.i + 2 < int32(U.path.size()))
					{
						A.bMarchDone = true;
						SaveLoad(Ctx, TEXT("army-on-the-march"), TEXT("awk-march"));
						W = Sim->SimMutable();
						break;
					}
			}
			// the Ark being built (before the launch): the save the ending must replay from
			if (!A.bArkBuildDone && W)
			{
				if (A.bArkStarted && !A.bArk)
				{
					A.bArkBuildDone = true;
					SaveLoad(Ctx, TEXT("ark-being-built"), TEXT("awk-ark"));
					W = Sim->SimMutable();
				}
			}
			if (A.bArk && !A.bArkDone)
			{
				// the launch and the ending fall in the same tick: this save holds an ended world
				A.bArkDone = true;
				SaveLoad(Ctx, TEXT("ark-launch-moment"), TEXT("awk-launch"), 0);
				A.SubFrame = 0;
				Next(A);
				break;
			}
			A.bBattle = A.bAge = false;
			if (W && W->year > 6000.0) { Result(TEXT("ark"), false, TEXT("no Ark by year 6000")); A.Step = 40; break; }
			if (W && W->phase == dm::Phase::Ended && !A.bArkDone) { Result(TEXT("ark"), false, TEXT("the world ended without an Ark launch")); A.Step = 40; break; }
			Advance(true);
			break;
		}
		case 11:  // after loading the launch-moment save: does the ending screen still come?
			if (A.StepFrame == 2)
				UE_LOG(LogDmAwk, Display, TEXT("DmAwk: after the launch-moment load: phase %s, flow %s, screen %d"), W ? UTF8_TO_TCHAR(dm::toStr(W->phase)) : TEXT("-"),
					ADmFlow::StateName(Flow->GetState()), int32(Sc->Current()));
			if (Sc->Current() == EDmScreen::Ending && !A.bPendingDone)
			{
				A.bPendingDone = true;
				Result(TEXT("ending-after-launch-load"), true, FString::Printf(TEXT("the ending screen opened %d frames after the load"), A.StepFrame));
			}
			if (A.StepFrame == 600)
			{
				if (!A.bPendingDone)
					Result(TEXT("ending-after-launch-load"), false, FString::Printf(TEXT("loading a save made in the launch tick (or the 7 s hold) left an ended world in flow %s with no ending screen for 600 frames"),
						ADmFlow::StateName(Flow->GetState())));
				Ctx.Shot(TEXT("after-launch-load"));
				Next(A);
			}
			break;
		case 12:  // reload the Ark being built: the ending must happen again and its screen open; save/load on that screen
			if (A.StepFrame == 20)
			{
				A.bEnding = false;
				A.Follow.Reset();
				if (Sc->IsOpen()) Sc->CloseAll();
				const int32 Before = A.Endings;
				const bool bOk = A.bArkBuildDone && Sc->LoadFromSlot(TEXT("awk-ark"));
				Result(TEXT("reload-ark-being-built"), bOk && Sim->Sim() && Sim->Sim()->phase == dm::Phase::History, FString::Printf(TEXT("loaded %d, phase %s, flow %s, endings so far %d"),
					bOk ? 1 : 0, Sim->Sim() ? UTF8_TO_TCHAR(dm::toStr(Sim->Sim()->phase)) : TEXT("-"), ADmFlow::StateName(Flow->GetState()), Before));
				Sim->SetPaused(false);
				Sim->SetSpeed(8.f);
				AttachGod(Ctx, true);
			}
			if (A.StepFrame > 20)
			{
				if (!A.bEnding) { Advance(true); }
				else if (Sc->Current() == EDmScreen::Ending && !A.bScreenDone)
				{
					A.bScreenDone = true;
					Ctx.Shot(TEXT("ending-screen"));
					A.SubFrame = A.StepFrame;
				}
				if (A.bScreenDone && A.StepFrame == A.SubFrame + 20)
				{
					Result(TEXT("ending-again"), A.Endings >= 2, FString::Printf(TEXT("%d endings seen; the ending screen opened after the reload"), A.Endings));
					SaveLoad(Ctx, TEXT("ending-screen"), TEXT("awk-ending-screen"), 0);
					Next(A);
				}
			}
			if (A.StepFrame > 9000) { Result(TEXT("ending-again"), false, FString::Printf(TEXT("no second ending screen (%d endings, screen %d, flow %s)"), A.Endings, int32(Sc->Current()), ADmFlow::StateName(Flow->GetState()))); Next(A); }
			break;
		case 13:  // what a loaded ended world does, then exit to title and load from the title
			if (A.StepFrame == 140)
			{
				const dm::World* L = Sim->Sim();
				UE_LOG(LogDmAwk, Display, TEXT("DmAwk: after loading the ending screen's save: phase %s, flow %s, screen %d, paused %d, speed %.0f"),
					L ? UTF8_TO_TCHAR(dm::toStr(L->phase)) : TEXT("-"), ADmFlow::StateName(Flow->GetState()), int32(Sc->Current()), Sim->IsPaused() ? 1 : 0, Sim->GetSpeed());
				Ctx.Shot(TEXT("ended-world-loaded"));
			}
			if (A.StepFrame == 160) Sc->ExitToTitle();
			if (A.StepFrame == 260)
			{
				A.Follow.Reset();
				const bool bOk = Sc->LoadFromSlot(TEXT("awk-history"));
				const dm::World* L = Sim->Sim();
				Result(TEXT("load-from-title"), bOk && L && Flow->GetState() == EDmFlowState::Play && !Flow->IsBackdrop(), FString::Printf(TEXT("loaded %d, year %.1f, flow %s, backdrop %d, paused %d"),
					bOk ? 1 : 0, L ? L->year : -1.0, ADmFlow::StateName(Flow->GetState()), Flow->IsBackdrop() ? 1 : 0, Sim->IsPaused() ? 1 : 0));
			}
			if (A.StepFrame == 360) { Ctx.Shot(TEXT("loaded-from-title")); }
			if (A.StepFrame == 380) { A.Step = 40; A.StepFrame = 0; }
			break;
		default:
			if (!A.bFinished) { A.bFinished = true; AwkFinish(Ctx); }
			break;
		}
	}
}

static FDmScenarioRegistrar GDmScenarioSaveLoadAwkward(TEXT("saveload-awkward"), TEXT("integration"),
	TEXT("save and load through the screens at awkward moments (genesis, mid-press, pause, 16x, march, battle, age, Ark, ending)"),
	[](FDmScenarioContext& Ctx) { AwkSetup(Ctx); },
	[](FDmScenarioContext& Ctx, float Dt) { AwkTick(Ctx, Dt); });
