// DEMIURGE — Game/Core/DmCoreScenarios.cpp : the bridge's own scenarios. Owner: C0 bridge.
//
//   play      every registered part (world, control, UI, audio), default orbit camera; the default
//             when no -scenario is given
//   orbit     the fixture's whole planet from orbit over its most settled region, sun ~55 degrees to
//             the east (day side with a terminator and city lights); later engineers start here
//   picktest  cell picking accuracy at orbit and near the ground (logs DmPickTest lines)
//   simtest   a real sim world through the subsystem: every speed, pause, fixed-step catch-up,
//             OnSimEvent vs the world's bus, dirty routing, save / load round trip (DmSimTest lines)

#include "Game/Core/DmCoords.h"
#include "Game/Core/DmGameMode.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmSimSubsystem.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"

#include <map>
#include <type_traits>
#include <string>
#include <vector>

#include "Sim/Save.h"
#include "Sim/Sim.h"

namespace
{
	/** lat / lon (degrees) of the population-weighted centre of the settlements, or (20, 30). */
	void SettledCentre(const dm::World* W, double& OutLat, double& OutLon)
	{
		OutLat = 20.0; OutLon = 30.0;
		if (!W || W->settlements.empty()) return;
		// the culture region with most people: average of its settlements' directions
		FVector Sum = FVector::ZeroVector;
		for (const dm::Settlement& S : W->settlements)
			if (!S.dead) Sum += DmCoords::CellDir(*W, S.cell) * FMath::Sqrt(FMath::Max(S.pop, 1.0));
		if (Sum.IsNearlyZero()) return;
		DmCoords::DirToLatLon(Sum.GetSafeNormal(), OutLat, OutLon);
		OutLat = FMath::Clamp(OutLat, -55.0, 55.0);
	}

	std::string PayloadText(const dm::Event& E)
	{
		std::string Out = E.name();
		E.fields([&Out](const char* Key, const auto& V)
		{
			Out += ' ';
			Out += Key;
			Out += '=';
			using T = std::decay_t<decltype(V)>;
			if constexpr (std::is_same_v<T, std::string>) Out += V;
			else if constexpr (std::is_same_v<T, bool>) Out += V ? "1" : "0";
			else if constexpr (std::is_arithmetic_v<T>) Out += std::to_string(V);
			else if constexpr (std::is_enum_v<T>) Out += std::to_string(int(V));
			else Out += "?";
		});
		return Out;
	}
}

// ------------------------------------------------------------------ play

static FDmScenarioRegistrar GDmScenarioPlay(TEXT("play"), TEXT("core"),
	TEXT("every registered part, default orbit camera (the default scenario)"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene(DmPartsAll);
		double Lat, Lon;
		SettledCentre(Ctx.Sim ? Ctx.Sim->Sim() : nullptr, Lat, Lon);
		Ctx.LookAt(Lat, Lon, 700.0, 0.0, 90.0, 30.0);
		if (!Ctx.Sim->IsSunPinned()) Ctx.SetSubsolar(FMath::Clamp(Lat * 0.5 + 8.0, -23.0, 23.0), Lon + 55.0);
	});

// ------------------------------------------------------------------ orbit

static FDmScenarioRegistrar GDmScenarioOrbit(TEXT("orbit"), TEXT("core"),
	TEXT("the whole fixture planet from orbit over its most settled region; terminator and city lights to the east"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene(DmPartsWorld);
		double Lat, Lon;
		SettledCentre(Ctx.Sim ? Ctx.Sim->Sim() : nullptr, Lat, Lon);
		// whole planet and atmosphere in frame (old SPEC-UI zoom 0): 700 km above the datum, fov 30
		Ctx.LookAt(Lat, Lon, 700.0, 0.0, 90.0, 30.0);
		Ctx.SetSubsolar(FMath::Clamp(Lat * 0.5 + 8.0, -23.0, 23.0), Lon + 55.0);
		Ctx.Log(FString::Printf(TEXT("orbit over lat %.1f lon %.1f"), Lat, Lon));
	});

// ------------------------------------------------------------------ picktest

namespace
{
	struct FPickStats
	{
		int32 Tried = 0, Visible = 0, RayExact = 0, ScreenExact = 0, ScreenNeighbour = 0, ScreenOther = 0;
		double SumErrM = 0, MaxErrM = 0, PickSeconds = 0;
	};

	// Ground truth: random points ON the pick surface inside sim triangles (around Focus within Hops,
	// anywhere when Focus < 0); their true cell is the nearest cell to their direction. A point is
	// visible when the ray from the camera to it hits it first. (1) the exact ray must return the true
	// cell (picking itself); (2) the point's projected pixel, picked through the screen, measures the
	// whole path including the engine's projection (sub-pixel differences between Project and
	// Deproject, TSR jitter).
	FPickStats RunPickTest(FDmScenarioContext& Ctx, int32 Samples, uint32 Seed, int32 Focus, int32 Hops)
	{
		FPickStats St;
		const dm::World* W = Ctx.Sim->Sim();
		APlayerController* PC = Ctx.World->GetFirstPlayerController();
		if (!W || !PC) return St;
		dm::Rng R(Seed);
		int32 SX = 0, SY = 0;
		PC->GetViewportSize(SX, SY);
		FVector CamLoc; FRotator CamRot;
		PC->GetPlayerViewPoint(CamLoc, CamRot);
		std::vector<int32_t> Pool;
		if (Focus >= 0) W->geo->ring(Focus, Hops, Pool);
		const FTransform P2W = Ctx.Sim->GetPlanetToWorld();
		for (int32 I = 0; I < Samples * 60 && St.Visible < Samples; ++I)
		{
			const int32 C = Pool.empty() ? R.integer(0, W->n - 1) : Pool[size_t(R.integer(0, int(Pool.size()) - 1))];
			const int32 Deg = W->geo->nbrN[size_t(C)];
			const int32 K = R.integer(0, Deg - 1);
			const int32 A = W->geo->nbr[size_t(6 * C + K)], B = W->geo->nbr[size_t(6 * C + (K + 1) % Deg)];
			double U = R.f(), V = R.f();
			if (U + V > 1.0) { U = 1.0 - U; V = 1.0 - V; }
			const FVector Dir = (DmCoords::CellDir(*W, C) * (1.0 - U - V) + DmCoords::CellDir(*W, A) * U + DmCoords::CellDir(*W, B) * V).GetSafeNormal();
			int32 TrueCell = C;
			const FVector Q = P2W.TransformPosition(Dir * Ctx.Sim->SurfaceRadiusUU(Dir, TrueCell));
			++St.Tried;
			FVector2D Scr;
			if (!PC->ProjectWorldLocationToScreen(Q, Scr, false)) continue;
			Scr += FVector2D(0.5, 0.5);   // the engine's Project / Deproject half-pixel convention (see ProjectCell)
			if (Scr.X < 1 || Scr.Y < 1 || Scr.X >= SX - 1 || Scr.Y >= SY - 1) continue;
			const double DistQ = FVector::Dist(Q, CamLoc);
			FDmPick Ray;
			const bool bRay = Ctx.Sim->PickRay(CamLoc, (Q - CamLoc).GetSafeNormal(), Ray);
			if (bRay && Ray.Distance < DistQ - 100.0) continue;   // hidden behind terrain (1 m tolerance)
			++St.Visible;
			if (bRay && Ray.Cell == TrueCell) ++St.RayExact;
			FDmPick P;
			const double T0 = FPlatformTime::Seconds();
			const bool bHit = Ctx.Sim->PickScreen(PC, Scr, P);
			St.PickSeconds += FPlatformTime::Seconds() - T0;
			if (!bHit) { ++St.ScreenOther; continue; }
			if (P.Cell == TrueCell) ++St.ScreenExact;
			else
			{
				bool bNb = false;
				for (int32 J = 0; J < W->geo->nbrN[size_t(TrueCell)]; ++J) if (W->geo->nbr[size_t(6 * TrueCell + J)] == P.Cell) bNb = true;
				if (bNb) ++St.ScreenNeighbour; else ++St.ScreenOther;
			}
			const double ErrM = FVector::Dist(P.WorldPos, Q) / 100.0;
			St.SumErrM += ErrM;
			St.MaxErrM = FMath::Max(St.MaxErrM, ErrM);
		}
		return St;
	}

	void LogPick(FDmScenarioContext& Ctx, const TCHAR* Label, const FPickStats& St)
	{
		const double Us = St.Visible ? St.PickSeconds * 1e6 / St.Visible : 0.0;
		const double CellKm = Ctx.Sim->Sim()->geo->step * DmCoords::RadiusKm;
		const double Pct = St.Visible ? 100.0 / St.Visible : 0.0;
		Ctx.Log(FString::Printf(TEXT("DmPickTest %s: %d visible surface points (of %d); exact ray -> true cell %d (%.1f%%); through the screen: same cell %d (%.1f%%), neighbour %d, other %d, hit-point error mean %.1f m max %.1f m (cell %.2f km); %.1f us per screen pick"),
			Label, St.Visible, St.Tried, St.RayExact, St.RayExact * Pct, St.ScreenExact, St.ScreenExact * Pct, St.ScreenNeighbour, St.ScreenOther,
			St.Visible ? St.SumErrM / St.Visible : 0.0, St.MaxErrM, CellKm, Us));
	}
}

static FDmScenarioRegistrar GDmScenarioPickTest(TEXT("picktest"), TEXT("core"),
	TEXT("cell picking accuracy against exact surface points: orbit, mid, low, near the ground; random pixels"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene(DmPartsWorld);
		Ctx.HoldExit();
		Ctx.LookAt(20.0, 30.0, 700.0, 0.0, 90.0, 30.0);
	},
	[](FDmScenarioContext& Ctx, float)
	{
		const dm::World* W = Ctx.Sim->Sim();
		if (!W) { Ctx.Finish(); return; }
		// the low views look across the highest mountain (steepest terrain: the hardest case)
		static int32 Mountain = -1;
		if (Mountain < 0)
		{
			float Best = -1e9f;
			for (int32 C = 0; C < W->n; ++C) if (W->elev[size_t(C)] > Best && FMath::Abs(W->geo->lat[size_t(C)]) < 0.9f) { Best = W->elev[size_t(C)]; Mountain = C; }
		}
		const double Lat = FMath::RadiansToDegrees(double(W->geo->lat[size_t(Mountain)]));
		const double Lon = FMath::RadiansToDegrees(double(W->geo->lon[size_t(Mountain)]));
		auto Timed = [&](const TCHAR* Label, int32 Samples, uint32 Seed, int32 Hops)
		{
			LogPick(Ctx, Label, RunPickTest(Ctx, Samples, Seed, Hops > 0 ? Mountain : -1, Hops));
		};
		switch (Ctx.Frame)
		{
		case 20: Timed(TEXT("orbit 700 km"), 1000, 11u, 0); Ctx.LookAt(Lat, Lon, 120.0, 20.0, 50.0, 40.0); break;
		case 30: Timed(TEXT("mid 120 km pitch 50"), 1000, 12u, 60); Ctx.LookAt(Lat, Lon, 12.0, 40.0, 25.0, 60.0); break;
		case 40: Timed(TEXT("low 12 km pitch 25"), 1000, 13u, 20); Ctx.LookAt(Lat, Lon, 3.0, 200.0, 12.0, 70.0); break;
		case 50: Timed(TEXT("ground 3 km pitch 12"), 1000, 14u, 8); Ctx.LookAt(20.0, 30.0, 700.0, 0.0, 90.0, 30.0); break;
		case 60:
		{
			// random pixels: every pick must reproject onto its own pixel
			APlayerController* PC = Ctx.World->GetFirstPlayerController();
			int32 SX = 0, SY = 0; PC->GetViewportSize(SX, SY);
			dm::Rng R(99);
			int32 Hits = 0, Off = 0;
			double MaxPx = 0;
			for (int32 I = 0; I < 2000; ++I)
			{
				const FVector2D Px(R.f() * SX, R.f() * SY);
				FDmPick P;
				if (!Ctx.Sim->PickScreen(PC, Px, P)) continue;
				++Hits;
				FVector2D Back;
				if (!PC->ProjectWorldLocationToScreen(P.WorldPos, Back, false)) { ++Off; continue; }
				MaxPx = FMath::Max(MaxPx, FVector2D::Distance(Back, Px));
			}
			// the engine's own round trip, no picking: deproject a pixel, walk 700 km along the ray, project back
			double MaxRt = 0, SumRt = 0, SumDx = 0, SumDy = 0;
			for (int32 I = 0; I < 200; ++I)
			{
				const FVector2D Px(R.f() * SX, R.f() * SY);
				FVector O, D;
				if (!PC->DeprojectScreenPositionToWorld(float(Px.X), float(Px.Y), O, D)) continue;
				FVector2D Back;
				if (!PC->ProjectWorldLocationToScreen(O + D * 7.0e7, Back, false)) continue;
				const double E = FVector2D::Distance(Back, Px);
				MaxRt = FMath::Max(MaxRt, E); SumRt += E; SumDx += Back.X - Px.X; SumDy += Back.Y - Px.Y;
			}
			Ctx.Log(FString::Printf(TEXT("DmPickTest random pixels at orbit: %d of 2000 hit the planet; hit points reproject within %.3f px (max); %d failed to reproject; engine deproject->project round trip alone: mean %.3f px, max %.3f px, mean offset (%.3f, %.3f) px; viewport %dx%d"),
				Hits, MaxPx, Off, SumRt / 200.0, MaxRt, SumDx / 200.0, SumDy / 200.0, SX, SY));
			Ctx.Finish();
			break;
		}
		default: break;
		}
	});

// ------------------------------------------------------------------ simtest

namespace
{
	struct FSimTest
	{
		int32 Phase = 0;
		int32 PhaseFrame = 0;
		double ExpectTicks = 0;
		int64 TicksAtStart = 0;
		int64 BusEvents = 0, RoutedEvents = 0, Mismatch = 0;
		std::map<std::string, int> Types;
		std::vector<std::string> BusLog, RoutedLog;
		int64 DirtyBroadcasts = 0;
		int32 SpeedIdx = 0;
		FString Report;
		uint64 BusTok = 0;
		FDelegateHandle Routed, DirtyH;
		bool bFail = false;
	};
	FSimTest GSimTest;
	const float GTestSpeeds[5] = { 0.25f, 1.f, 2.f, 4.f, 8.f };

	void SimTestSubscribe(FDmScenarioContext& Ctx)
	{
		FSimTest& T = GSimTest;
		dm::World* W = Ctx.Sim->SimMutable();
		T.BusTok = W->bus.onAll([](const dm::Event& E)
		{
			GSimTest.BusEvents++;
			if (GSimTest.BusLog.size() < 20000) GSimTest.BusLog.push_back(PayloadText(E));
		});
		T.Routed = Ctx.Sim->OnSimEvent.AddLambda([](const dm::Event& E)
		{
			GSimTest.RoutedEvents++;
			GSimTest.Types[E.name()]++;
			if (GSimTest.RoutedLog.size() < 20000) GSimTest.RoutedLog.push_back(PayloadText(E));
		});
		T.DirtyH = Ctx.Sim->Dirty().OnDirty.AddLambda([](const FDmDirtyFlags&) { GSimTest.DirtyBroadcasts++; });
	}
}

static FDmScenarioRegistrar GDmScenarioSimTest(TEXT("simtest"), TEXT("core"),
	TEXT("a real sim world through the subsystem: speeds, pause, catch-up, events, dirty flags, save / load"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.HoldExit();
		if (!Ctx.Sim->IsLive())
		{
			int32 Seed = 3;
			FDmShotArgs::Value(TEXT("-testseed="), Seed);
			Ctx.Sim->CreateWorld(uint32(Seed), FString(), true, 0.0);
		}
		Ctx.BuildScene(DmPartsWorld);
		Ctx.LookAt(20.0, 30.0, 700.0, 0.0, 90.0, 30.0);
		SimTestSubscribe(Ctx);
		const dm::World* W = Ctx.Sim->Sim();
		Ctx.Log(FString::Printf(TEXT("DmSimTest start: phase %s year %.2f tick %lld, %d settlements, %d cultures, pop %.0f"),
			UTF8_TO_TCHAR(dm::toStr(W->phase)), W->year, (long long)W->tick, int32(W->settlements.size()), int32(W->cultures.size()), W->stats.pop));
		Ctx.Sim->SetSpeed(GTestSpeeds[0]);
		GSimTest.TicksAtStart = Ctx.Sim->GetTicksRun();
	},
	[](FDmScenarioContext& Ctx, float Dt)
	{
		FSimTest& T = GSimTest;
		UDmSimSubsystem* Sim = Ctx.Sim;
		const dm::World* W = Sim->Sim();
		if (!W) { Ctx.Log(TEXT("DmSimTest FAIL: world vanished")); Ctx.Finish(); return; }
		++T.PhaseFrame;
		const int32 FramesPerSpeed = 90;
		// the subsystem ticks after the game mode in a frame, so expected ticks lag one frame: compare with tolerance
		if (T.Phase == 0)
		{
			// speeds (genesis is skipped, so the speed applies)
			T.ExpectTicks += FMath::Min(double(Dt), 0.25) * 8.0 * GTestSpeeds[T.SpeedIdx];
			if (T.PhaseFrame >= FramesPerSpeed)
			{
				const int64 Ran = Sim->GetTicksRun() - T.TicksAtStart;
				const double Err = FMath::Abs(double(Ran) - T.ExpectTicks);
				const bool bOk = Err <= FMath::Max(2.0, 0.06 * T.ExpectTicks) + double(Sim->GetTicksDropped());
				Ctx.Log(FString::Printf(TEXT("DmSimTest speed %.2fx: %lld ticks run in %d frames, %.1f expected from frame times, %lld dropped, %.3f ms/tick, year %.2f -> %s"),
					GTestSpeeds[T.SpeedIdx], (long long)Ran, T.PhaseFrame, T.ExpectTicks,
					(long long)Sim->GetTicksDropped(), Sim->GetSimMsPerTick(), W->year, bOk ? TEXT("OK") : TEXT("FAIL")));
				T.bFail |= !bOk;
				T.PhaseFrame = 0; T.ExpectTicks = 0; T.TicksAtStart = Sim->GetTicksRun();
				if (++T.SpeedIdx < 5) Sim->SetSpeed(GTestSpeeds[T.SpeedIdx]);
				else { T.Phase = 1; Sim->SetSpeed(0.f); }
			}
		}
		else if (T.Phase == 1)
		{
			// paused: no ticks for 60 frames, then resume at 1x
			if (T.PhaseFrame == 2) T.TicksAtStart = Sim->GetTicksRun();
			if (T.PhaseFrame >= 62)
			{
				const int64 Ran = Sim->GetTicksRun() - T.TicksAtStart;
				Ctx.Log(FString::Printf(TEXT("DmSimTest pause: %lld ticks in 60 paused frames (paused=%d) -> %s"), (long long)Ran, Sim->IsPaused() ? 1 : 0, Ran == 0 ? TEXT("OK") : TEXT("FAIL")));
				T.bFail |= Ran != 0;
				Sim->SetSpeed(1.f);
				// catch-up: a 10 s hitch at 8x must be capped by MaxTicksPerFrame / budget, not spiral
				Sim->SetSpeed(8.f);
				T.Phase = 2; T.PhaseFrame = 0;
			}
		}
		else if (T.Phase == 2)
		{
			// 8x: the per-frame tick count must stay under the catch-up cap
			static int32 MaxSeen = 0;
			MaxSeen = FMath::Max(MaxSeen, Sim->GetTicksLastFrame());
			if (T.PhaseFrame == 60)
			{
				Ctx.Log(FString::Printf(TEXT("DmSimTest 8x: at most %d ticks in one frame over 60 frames (cap %d, budget %.1f ms), %lld ticks dropped in total -> %s"),
					MaxSeen, Sim->MaxTicksPerFrame, Sim->SimBudgetMs, (long long)Sim->GetTicksDropped(), MaxSeen <= Sim->MaxTicksPerFrame ? TEXT("OK") : TEXT("FAIL")));
				T.bFail |= MaxSeen > Sim->MaxTicksPerFrame;
				Sim->SetSpeed(1.f);
				T.Phase = 3; T.PhaseFrame = 0;
			}
		}
		else if (T.Phase == 3)
		{
			// events: every bus event reached OnSimEvent, in order, with the same payload
			const bool bCount = T.BusEvents == T.RoutedEvents;
			int64 Diff = 0;
			for (size_t I = 0; I < FMath::Min(T.BusLog.size(), T.RoutedLog.size()); ++I) if (T.BusLog[I] != T.RoutedLog[I]) ++Diff;
			FString Types;
			for (const auto& P : T.Types) Types += FString::Printf(TEXT("%s%s %d"), Types.IsEmpty() ? TEXT("") : TEXT(", "), UTF8_TO_TCHAR(P.first.c_str()), P.second);
			Ctx.Log(FString::Printf(TEXT("DmSimTest events: bus %lld, OnSimEvent %lld, payload mismatches %lld -> %s; types: %s"),
				(long long)T.BusEvents, (long long)T.RoutedEvents, (long long)Diff, (bCount && Diff == 0) ? TEXT("OK") : TEXT("FAIL"), *Types));
			if (!T.RoutedLog.empty()) Ctx.Log(FString::Printf(TEXT("DmSimTest sample event: %s"), UTF8_TO_TCHAR(T.RoutedLog.back().c_str())));
			Ctx.Log(FString::Printf(TEXT("DmSimTest dirty: %lld broadcasts, last {%s}"), (long long)T.DirtyBroadcasts, *Sim->Dirty().Last().ToString()));
			T.bFail |= !(bCount && Diff == 0);

			// save / load round trip
			Sim->SetPaused(true);
			const double Year = W->year;
			const int64 Tick = W->tick;
			const uint32 RngS = W->rng.s;
			const int32 NS = int32(W->settlements.size()), NC = int32(W->cultures.size()), NU = int32(W->units.size());
			const double Pop = W->stats.pop;
			std::vector<uint8_t> Before;
			dm::state::serialize(*W, Before);
			FString Err;
			const bool bSaved = Sim->SaveSlot(TEXT("bridge-simtest"), &Err);
			const TArray<FDmSlotInfo> Slots = Sim->ListSlots();
			const FDmSlotInfo* Found = Slots.FindByPredicate([](const FDmSlotInfo& I) { return I.Slot == TEXT("bridge-simtest"); });
			// raw file check before sim::use rederives anything
			std::string Why;
			std::unique_ptr<dm::World> Raw = dm::save::readSlot(std::string(TCHAR_TO_UTF8(*Sim->GetSaveDir())), "bridge-simtest", &Why);
			std::vector<uint8_t> RawBytes;
			if (Raw) dm::state::serialize(*Raw, RawBytes);
			std::vector<uint8_t> Requant;
			{
				std::unique_ptr<dm::World> Q = dm::state::deserialize(Before);
				if (Q) dm::state::serialize(*Q, Requant);
			}
			const bool bLoaded = bSaved && Sim->LoadSlot(TEXT("bridge-simtest"), &Err);
			const dm::World* L = Sim->Sim();
			const bool bSame = bLoaded && L && L->year == Year && L->tick == Tick && L->rng.s == RngS && int32(L->settlements.size()) == NS
				&& int32(L->cultures.size()) == NC && int32(L->units.size()) == NU && L->stats.pop == Pop;
			const bool bBytes = Raw && RawBytes == Requant;
			Ctx.Log(FString::Printf(TEXT("DmSimTest save/load: saved %d, listed %d (year %.2f), loaded %d, scalars+entities equal %d, state bytes equal %d (%d bytes) -> %s %s"),
				bSaved ? 1 : 0, Found ? 1 : 0, Found ? Found->Year : -1.0, bLoaded ? 1 : 0, bSame ? 1 : 0, bBytes ? 1 : 0, int32(Before.size()),
				(bSaved && bLoaded && bSame && bBytes) ? TEXT("OK") : TEXT("FAIL"), *Err));
			T.bFail |= !(bSaved && bLoaded && bSame && bBytes);
			Sim->RemoveSlot(TEXT("bridge-simtest"));
			// the loaded world is a new world: resubscribe and run on
			T.BusEvents = T.RoutedEvents = 0; T.BusLog.clear(); T.RoutedLog.clear(); T.Types.clear();
			Sim->OnSimEvent.Remove(T.Routed);
			Sim->Dirty().OnDirty.Remove(T.DirtyH);
			SimTestSubscribe(Ctx);
			Sim->SetSpeed(4.f);
			T.Phase = 4; T.PhaseFrame = 0; T.TicksAtStart = Sim->GetTicksRun();
		}
		else if (T.Phase == 4 && T.PhaseFrame >= 120)
		{
			const int64 Ran = Sim->GetTicksRun() - T.TicksAtStart;
			Ctx.Log(FString::Printf(TEXT("DmSimTest after load: %lld ticks at 4x, year %.2f, %lld events routed (bus %lld) -> %s"),
				(long long)Ran, W->year, (long long)T.RoutedEvents, (long long)T.BusEvents, (Ran > 0 && T.RoutedEvents == T.BusEvents) ? TEXT("OK") : TEXT("FAIL")));
			T.bFail |= !(Ran > 0 && T.RoutedEvents == T.BusEvents);
			Ctx.Log(FString::Printf(TEXT("DmSimTest RESULT %s"), T.bFail ? TEXT("FAIL") : TEXT("PASS")));
			T.Phase = 5;
			Ctx.Finish();
		}
	});
