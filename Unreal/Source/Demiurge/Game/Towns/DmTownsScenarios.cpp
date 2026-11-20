// DEMIURGE — Game/Towns/DmTownsScenarios.cpp : the towns module's scenarios (C5).
//
//   towns-list        log every settlement (id, place, tier, pop, age, buildings, coast) and exit (-noshot)
//   towns-overview    40 km oblique view over the most populous region, afternoon
//   towns-village / towns-town / towns-city / towns-metro   the largest settlement of that tier, close oblique
//                     view at the game's closest zoom (-townid=<id> picks one)
//   towns-port        a port seen from the sea          towns-walls   a walled town
//   towns-night       the largest city at night         towns-golden  a town at golden hour
//   towns-ages        one site through the seven ages (huts to towers): shots -a0 .. -a6 (-townid=)
//   towns-tiers       one site at the five tiers (camp .. metropolis), Industry age: shots -t0 .. -t4
//   towns-ruins       a destroyed town (the scenario kills it in the world)
//   towns-units       units in motion (caravans, ships, armies) around the most populous region
//   towns-live        the real sim at 8x for 40 s: re-plans, construction scaffolds, frame times; shots -s0, -s1
//   towns-perf        120 km view over the settled region: instance / component / frame statistics
//   towns-cdtest      debug probe of per-instance custom data (ISM / HISM, parent / dynamic material, engine
//                     cube / generated kit mesh); left in as a regression check for the HISM finding
// Common switches: -townid=<settlement id>, -townkm=<camera distance>, -townpitch=, -townheading=, -sunang=<deg from zenith>.

#include "Game/Towns/DmTownsActor.h"
#include "Game/Towns/DmTownsPlan.h"
#include "Game/Towns/DmTownsPerf.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmSimSubsystem.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Sim/State.h"

namespace
{
	bool Coastal(const dm::World& W, int32 Cell)
	{
		std::vector<int32_t> Ring;
		W.geo->ring(Cell, 1, Ring);
		for (int32_t C : Ring) if (dm::state::isSea(W, C)) return true;
		return false;
	}

	FString Buildings(const dm::Settlement& S)
	{
		FString Out;
		for (int32 K = 0; K < dm::Building::COUNT; ++K) if (S.b[size_t(K)]) { if (!Out.IsEmpty()) Out += TEXT(","); Out += UTF8_TO_TCHAR(dm::kBuildingNames[K]); }
		return Out;
	}

	int32 AgeOf(const dm::World& W, const dm::Settlement& S)
	{
		const dm::Culture* Cu = dm::state::culture(W, S.culture);
		return Cu ? Cu->age : W.age;
	}

	/** The best live settlement by score (higher wins); -townid= overrides. */
	template <typename Fn>
	int32 Pick(const dm::World& W, Fn&& Score)
	{
		int32 Id = -1;
		if (FDmShotArgs::Value(TEXT("-townid="), Id) && Id >= 0 && Id < int32(W.settlements.size())) return Id;
		double Best = -1e30; Id = -1;
		for (const dm::Settlement& S : W.settlements)
		{
			if (S.dead) continue;
			const double V = Score(S);
			if (V > Best) { Best = V; Id = S.id; }
		}
		return Id;
	}

	void LatLon(const dm::World& W, int32 Cell, double& Lat, double& Lon)
	{
		DmCoords::DirToLatLon(DmCoords::CellDir(W, Cell), Lat, Lon);
	}

	/** Deferred framing: the harness's LookAt aims at the sim surface, which can be hundreds of metres off the
	 *  rendered ground; once the towns are planned the camera is re-aimed at the plan's centre (the plaza). */
	struct FPendingFrame { int32 Sid = -1; double Dist = 2, Heading = 0, Pitch = 40; bool bPending = false; };
	FPendingFrame GFrame;

	void ApplyFrame(FDmScenarioContext& Ctx)
	{
		if (!GFrame.bPending || Ctx.Frame < 2) return;
		ADmTowns* T = ADmTowns::Find(Ctx.World);
		if (!T || !T->GetPlan(GFrame.Sid)) return;
		GFrame.bPending = false;
		const FVector C = T->TownCentreWorld(GFrame.Sid);
		const FVector P0 = Ctx.Sim->GetPlanetToWorld().GetLocation();
		const FVector Up = (C - P0).GetSafeNormal();
		FVector E, N; DmCoords::Basis(Up, E, N);
		const double H = FMath::DegreesToRadians(GFrame.Heading), Pt = FMath::DegreesToRadians(GFrame.Pitch);
		const FVector Hz = N * FMath::Cos(H) + E * FMath::Sin(H);
		const FVector Fwd = (Hz * FMath::Cos(Pt) - Up * FMath::Sin(Pt)).GetSafeNormal();
		const FVector Loc = C - Fwd * GFrame.Dist * DmCoords::UUPerKm;
		Ctx.SetView(Loc, FRotationMatrix::MakeFromXZ(Fwd, Up).Rotator(), 35.0);
		// (verifier) where the camera ended up: radius above the datum of the plaza and of the camera (km)
		Ctx.Log(FString::Printf(TEXT("DmTownsScenario: framed settlement %d: plaza %.3f km, camera %.3f km above the datum, planet centre %s"),
			GFrame.Sid, ((C - P0).Size() - 1.0e7) / DmCoords::UUPerKm, ((Loc - P0).Size() - 1.0e7) / DmCoords::UUPerKm, *P0.ToString()));
	}

	/** Camera on a settlement: distance from its built radius unless -townkm; afternoon sun from the south-west. */
	void Frame(FDmScenarioContext& Ctx, int32 Sid, double DistKm, double Heading, double Pitch, double SunAngDeg = 48.0, bool bNight = false)
	{
		const dm::World* W = Ctx.Sim ? Ctx.Sim->Sim() : nullptr;
		if (!W || Sid < 0) return;
		const dm::Settlement& S = W->settlements[size_t(Sid)];
		double Lat, Lon;
		LatLon(*W, S.cell, Lat, Lon);
		FDmShotArgs::Value(TEXT("-townkm="), DistKm);
		FDmShotArgs::Value(TEXT("-townpitch="), Pitch);
		FDmShotArgs::Value(TEXT("-townheading="), Heading);
		FDmShotArgs::Value(TEXT("-sunang="), SunAngDeg);
		Ctx.LookAt(Lat, Lon, DistKm, Heading, Pitch, 35.0);
		if (!FDmShotArgs::Get().bCam) GFrame = { Sid, DistKm, Heading, Pitch, true };
		if (!Ctx.Sim->IsSunPinned())
		{
			// subsolar point SunAngDeg away from the town, toward the camera's right-front (light rakes across the roofs)
			const FVector Up = DmCoords::CellDir(*W, S.cell);
			FVector E, N; DmCoords::Basis(Up, E, N);
			const double H = FMath::DegreesToRadians(Heading + 70.0);
			const FVector Toward = N * FMath::Cos(H) + E * FMath::Sin(H);
			const double A = FMath::DegreesToRadians(bNight ? 150.0 : SunAngDeg);
			const FVector Sub = (Up * FMath::Cos(A) + Toward * FMath::Sin(A)).GetSafeNormal();
			double SLat, SLon;
			DmCoords::DirToLatLon(Sub, SLat, SLon);
			Ctx.SetSubsolar(SLat, SLon);
		}
		const int32 Age = AgeOf(*W, S);
		Ctx.Log(FString::Printf(TEXT("DmTownsScenario: settlement %d \"%s\" tier %d pop %.0f age %d at lat %.2f lon %.2f, buildings [%s], built radius %.0f m, camera %.2f km"),
			Sid, UTF8_TO_TCHAR(S.name.c_str()), S.tier, S.pop, Age, Lat, Lon, *Buildings(S), DmTownsPlan::BuiltRadius(S.tier, S.pop, Age), DistKm));
	}

	double CloseKm(const dm::World& W, int32 Sid)
	{
		const dm::Settlement& S = W.settlements[size_t(Sid)];
		return FMath::Clamp(DmTownsPlan::BuiltRadius(S.tier, S.pop, AgeOf(W, S)) * 5.0 / 1000.0, 0.5, 9.0);
	}

	void LogTowns(FDmScenarioContext& Ctx)
	{
		if (ADmTowns* T = ADmTowns::Find(Ctx.World)) Ctx.Log(FString::Printf(TEXT("DmTownsScenario: %s; %d visible, %d units drawn, %lld plans (%.0f ms)"), *T->Describe(), T->NumVisibleTowns(), T->NumUnitsDrawn(), T->PlansDone(), T->PlanMsTotal()));
	}

	void TickLog(FDmScenarioContext& Ctx, float)
	{
		ApplyFrame(Ctx);
		if (Ctx.Frame == 200) LogTowns(Ctx);
	}

	void TierScenario(FDmScenarioContext& Ctx, int32 Tier)
	{
		Ctx.BuildScene();
		const dm::World* W = Ctx.Sim->Sim();
		if (!W) return;
		const int32 Sid = Pick(*W, [&](const dm::Settlement& S) { return (S.tier == Tier ? 1e6 : -1e6 * FMath::Abs(S.tier - Tier)) + S.pop; });
		Frame(Ctx, Sid, CloseKm(*W, Sid), 25.0, 38.0);
	}
}

static FDmScenarioRegistrar GTownsList(TEXT("towns-list"), TEXT("towns"), TEXT("log every settlement and exit"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		const dm::World* W = Ctx.Sim->Sim();
		if (!W) return;
		for (const dm::Settlement& S : W->settlements)
		{
			double Lat, Lon; LatLon(*W, S.cell, Lat, Lon);
			Ctx.Log(FString::Printf(TEXT("DmTownsList %d \"%s\" cell %d lat %.2f lon %.2f tier %d pop %.0f age %d culture %d coast %d dead %d capital %d [%s]"),
				S.id, UTF8_TO_TCHAR(S.name.c_str()), S.cell, Lat, Lon, S.tier, S.pop, AgeOf(*W, S), S.culture, Coastal(*W, S.cell) ? 1 : 0, S.dead ? 1 : 0, S.capital ? 1 : 0, *Buildings(S)));
		}
		Ctx.HoldExit();
	},
	[](FDmScenarioContext& Ctx, float)
	{
		// the first wave plans only what the camera sees; wait for the background plans (verifier change)
		ADmTowns* T = ADmTowns::Find(Ctx.World);
		if (Ctx.Frame >= 30 && (!T || T->IsSettled() || Ctx.Frame > 3000)) { LogTowns(Ctx); Ctx.Log(FString::Printf(TEXT("DmTownsScenario: settled at frame %d, %.1f s"), Ctx.Frame, Ctx.Time)); Ctx.Finish(); }
	});

static FDmScenarioRegistrar GTownsOverview(TEXT("towns-overview"), TEXT("towns"), TEXT("40 km oblique view over the most populous region"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		const dm::World* W = Ctx.Sim->Sim();
		if (!W) return;
		const int32 Sid = Pick(*W, [](const dm::Settlement& S) { return S.pop; });
		Frame(Ctx, Sid, 40.0, 20.0, 45.0, 52.0);
	}, TickLog);

static FDmScenarioRegistrar GTownsVillage(TEXT("towns-village"), TEXT("towns"), TEXT("the largest village, close"), [](FDmScenarioContext& Ctx) { TierScenario(Ctx, 1); }, TickLog);
static FDmScenarioRegistrar GTownsTown(TEXT("towns-town"), TEXT("towns"), TEXT("the largest town, close"), [](FDmScenarioContext& Ctx) { TierScenario(Ctx, 2); }, TickLog);
static FDmScenarioRegistrar GTownsCity(TEXT("towns-city"), TEXT("towns"), TEXT("the largest city, close"), [](FDmScenarioContext& Ctx) { TierScenario(Ctx, 3); }, TickLog);
static FDmScenarioRegistrar GTownsMetro(TEXT("towns-metro"), TEXT("towns"), TEXT("the largest metropolis, close"), [](FDmScenarioContext& Ctx) { TierScenario(Ctx, 4); }, TickLog);

static FDmScenarioRegistrar GTownsPort(TEXT("towns-port"), TEXT("towns"), TEXT("a port seen from the sea"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		const dm::World* W = Ctx.Sim->Sim();
		if (!W) return;
		const int32 Sid = Pick(*W, [&](const dm::Settlement& S) { return (S.has(dm::Building::PORT) ? 1e6 : 0.0) + S.pop; });
		Frame(Ctx, Sid, CloseKm(*W, Sid) * 0.8, 200.0, 32.0);
	}, TickLog);

static FDmScenarioRegistrar GTownsWalls(TEXT("towns-walls"), TEXT("towns"), TEXT("a walled town"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		const dm::World* W = Ctx.Sim->Sim();
		if (!W) return;
		const int32 Sid = Pick(*W, [&](const dm::Settlement& S) { return (S.has(dm::Building::WALLS) ? 1e6 : 0.0) + (S.tier == 2 ? 1e5 : 0.0) + S.pop; });
		Frame(Ctx, Sid, CloseKm(*W, Sid) * 0.75, 60.0, 42.0);
	}, TickLog);

static FDmScenarioRegistrar GTownsNight(TEXT("towns-night"), TEXT("towns"), TEXT("the largest city at night"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		const dm::World* W = Ctx.Sim->Sim();
		if (!W) return;
		const int32 Sid = Pick(*W, [](const dm::Settlement& S) { return S.pop; });
		Frame(Ctx, Sid, CloseKm(*W, Sid) * 1.3, 25.0, 40.0, 48.0, true);
	}, TickLog);

static FDmScenarioRegistrar GTownsGolden(TEXT("towns-golden"), TEXT("towns"), TEXT("a town at golden hour"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		const dm::World* W = Ctx.Sim->Sim();
		if (!W) return;
		const int32 Sid = Pick(*W, [&](const dm::Settlement& S) { return (S.tier == 2 ? 1e6 : 0.0) + S.pop; });
		Frame(Ctx, Sid, CloseKm(*W, Sid) * 0.9, 300.0, 30.0, 80.0);
	}, TickLog);

// ------------------------------------------------------------------ ladders: one site through the ages / tiers

namespace
{
	struct FLadder { int32 Tier; double Pop; int32 Age; };
	const FLadder kAges[7] = { { 1, 60, 0 }, { 2, 500, 1 }, { 2, 1200, 2 }, { 3, 3500, 3 }, { 4, 14000, 4 }, { 4, 26000, 5 }, { 4, 42000, 6 } };
	const FLadder kTiers[5] = { { 0, 18, 4 }, { 1, 140, 4 }, { 2, 900, 4 }, { 3, 3200, 4 }, { 4, 14000, 4 } };
	int32 GLadderSid = -1;

	void LadderSetup(FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		const dm::World* W = Ctx.Sim->Sim();
		if (!W) return;
		// a coastal settlement with room to grow
		GLadderSid = Pick(*W, [&](const dm::Settlement& S) { return (Coastal(*W, S.cell) ? 2e5 : 0.0) + (S.has(dm::Building::PORT) ? 1e5 : 0.0) + FMath::Min(S.pop, 5000.0) + 50.0 * S.tier; });
		Frame(Ctx, GLadderSid, 6.5, 25.0, 42.0);
		Ctx.HoldExit();
	}

	void LadderTick(FDmScenarioContext& Ctx, const FLadder* L, int32 N, const TCHAR* Prefix)
	{
		ApplyFrame(Ctx);
		ADmTowns* T = ADmTowns::Find(Ctx.World);
		if (!T || GLadderSid < 0) return;
		// step K: override at frame 20 + 260 K, shoot at 250 + 260 K
		const int32 K = (Ctx.Frame - 20) / 260, F = (Ctx.Frame - 20) % 260;
		if (K < 0 || K > N) return;
		if (K == N) { if (F == 0) { LogTowns(Ctx); Ctx.Finish(); } return; }
		if (F == 0)
		{
			T->SetOverride(GLadderSid, L[K].Tier, L[K].Pop, L[K].Age);
			T->RebuildAll(true);
			if (const FDmTownPlan* P = T->GetPlan(GLadderSid))
				Ctx.Log(FString::Printf(TEXT("DmTownsScenario: %s%d %s: %d instances, %d houses, %d civic, %d fields, %d streets, %d piers, %d ships, %d wall pieces, R %.0f m, %.1f ms"),
					Prefix, K, *P->Note, P->Inst.Num(), P->Houses, P->Civic, P->Fields, P->Streets, P->Piers, P->Ships, P->WallPieces, P->RadiusM, P->Ms));
		}
		if (F == 230) Ctx.Shot(FString::Printf(TEXT("%s%d"), Prefix, K));
	}
}

static FDmScenarioRegistrar GTownsAges(TEXT("towns-ages"), TEXT("towns"), TEXT("one site through the seven ages (huts to towers)"),
	LadderSetup, [](FDmScenarioContext& Ctx, float) { LadderTick(Ctx, kAges, 7, TEXT("a")); });

static FDmScenarioRegistrar GTownsTiers(TEXT("towns-tiers"), TEXT("towns"), TEXT("one site at the five tiers (Industry age)"),
	LadderSetup, [](FDmScenarioContext& Ctx, float) { LadderTick(Ctx, kTiers, 5, TEXT("t")); });

static FDmScenarioRegistrar GTownsRuins(TEXT("towns-ruins"), TEXT("towns"), TEXT("a destroyed town"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		dm::World* W = Ctx.Sim->SimMutable();
		if (!W) return;
		const int32 Sid = Pick(*W, [&](const dm::Settlement& S) { return (S.tier == 2 ? 1e6 : 0.0) + S.pop; });
		Frame(Ctx, Sid, CloseKm(*W, Sid) * 0.8, 25.0, 40.0);
		dm::Settlement& S = W->settlements[size_t(Sid)];
		S.dead = true; S.deadYear = W->year; S.cause = dm::Cause::War;
		W->urban[size_t(S.cell)] = FMath::Max(W->urban[size_t(S.cell)], 0.35f);
		Ctx.Log(FString::Printf(TEXT("DmTownsScenario: settlement %d destroyed for the shot"), Sid));
	}, TickLog);

static FDmScenarioRegistrar GTownsUnits(TEXT("towns-units"), TEXT("towns"), TEXT("units in motion: the camera follows a drawn unit on dry land (-unitid= picks one)"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		const dm::World* W = Ctx.Sim->Sim();
		if (!W || W->units.empty()) return;
		const int32 Sid = Pick(*W, [](const dm::Settlement& S) { return S.pop; });
		double Lat, Lon; LatLon(*W, W->settlements[size_t(Sid)].cell, Lat, Lon);
		Ctx.LookAt(Lat, Lon, 400.0, 0.0, 90.0, 35.0);
		Ctx.Log(FString::Printf(TEXT("DmTownsScenario: %d units in the world"), int32(W->units.size())));
	},
	[](FDmScenarioContext& Ctx, float)
	{
		ADmTowns* T = ADmTowns::Find(Ctx.World);
		const dm::World* W = Ctx.Sim->Sim();
		if (!T || !W || Ctx.Frame < 3 || Ctx.Frame > 200) return;
		TArray<FVector> P; TArray<int32> Ids;
		T->UnitPositionsWorld(P, Ids);
		if (P.Num() == 0) return;
		int32 Want = -1; FDmShotArgs::Value(TEXT("-unitid="), Want);
		int32 Best = 0;
		for (int32 I = 0; I < Ids.Num(); ++I)
		{
			if (Ids[I] == Want) { Best = I; break; }
			const dm::Unit* U = dm::state::findUnit(*W, Ids[I]);
			double La, Lo; DmCoords::DirToLatLon(Ctx.Sim->GetPlanetToWorld().InverseTransformVectorNoScale((P[I] - Ctx.Sim->GetPlanetToWorld().GetLocation()).GetSafeNormal()), La, Lo);
			if (Want < 0 && U && !U->sea && La > 5.0) { Best = I; break; }
		}
		double Km = 3.0; FDmShotArgs::Value(TEXT("-townkm="), Km);
		const FVector C = P[Best];
		const FVector Up = (C - Ctx.Sim->GetPlanetToWorld().GetLocation()).GetSafeNormal();
		FVector E, N; DmCoords::Basis(Up, E, N);
		const double Pt = FMath::DegreesToRadians(45.0);
		const FVector Fwd = (N * FMath::Cos(Pt) - Up * FMath::Sin(Pt)).GetSafeNormal();
		Ctx.SetView(C - Fwd * Km * DmCoords::UUPerKm, FRotationMatrix::MakeFromXZ(Fwd, Up).Rotator(), 35.0);
		if (Ctx.Frame == 3)
		{
			const FVector Pd = Ctx.Sim->GetPlanetToWorld().InverseTransformVectorNoScale(Up);
			double La, Lo; DmCoords::DirToLatLon(Pd, La, Lo);
			if (!Ctx.Sim->IsSunPinned()) Ctx.SetSubsolar(La * 0.6, Lo + 35.0);
			const dm::Unit* U = dm::state::findUnit(*W, Ids[Best]);
			Ctx.Log(FString::Printf(TEXT("DmTownsScenario: following unit %d (%s, %s) at lat %.2f lon %.2f; %d units drawn"), Ids[Best], U ? UTF8_TO_TCHAR(dm::toStr(U->type)) : TEXT("?"), U && U->sea ? TEXT("at sea") : TEXT("on land"), La, Lo, P.Num()));
		}
	});

static FDmScenarioRegistrar GTownsLive(TEXT("towns-live"), TEXT("towns"), TEXT("the real sim at 8x: re-plans, construction, frame times"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		const dm::World* W = Ctx.Sim->Sim();
		if (!W) return;
		const int32 Sid = Pick(*W, [](const dm::Settlement& S) { return S.pop; });
		Frame(Ctx, Sid, 14.0, 25.0, 45.0);
		Ctx.HoldExit();
	},
	[](FDmScenarioContext& Ctx, float Dt)
	{
		ApplyFrame(Ctx);
		ADmTowns* T = ADmTowns::Find(Ctx.World);
		if (Ctx.Frame == 120) { Ctx.Shot(TEXT("s0")); }
		if (Ctx.Frame == 160)
		{
			if (!Ctx.Sim->IsLive()) Ctx.Sim->MakeLive();
			Ctx.Sim->SetSpeed(8.f);
			Ctx.Log(TEXT("DmTownsScenario: live at 8x"));
		}
		static double SumDt = 0, MaxDt = 0; static int32 NDt = 0;
		if (Ctx.Frame > 160) { SumDt += Dt; MaxDt = FMath::Max(MaxDt, double(Dt)); ++NDt; }
		if (T && Ctx.Frame > 160 && Ctx.Frame % 300 == 0)
		{
			Ctx.Log(FString::Printf(TEXT("DmTownsScenario: t %.1f s year %.0f: %s, %lld plans so far (%.0f ms), %d units drawn; frames avg %.1f ms max %.1f ms; towns tick max %.1f ms"), Ctx.Time, Ctx.Sim->Sim() ? Ctx.Sim->Sim()->year : 0.0, *T->Describe(), T->PlansDone(), T->PlanMsTotal(), T->NumUnitsDrawn(),
				NDt ? SumDt / NDt * 1000.0 : 0.0, MaxDt * 1000.0, T->TakeMaxTickMs()));
			SumDt = 0; MaxDt = 0; NDt = 0;
		}
		if (Ctx.Time > 45.0 && Ctx.Frame % 10 == 0)
		{
			static bool bShot = false;
			if (!bShot) { bShot = true; Ctx.Shot(TEXT("s1")); }
			else if (Ctx.Time > 48.0) { LogTowns(Ctx); Ctx.Finish(); }
		}
	});

static FDmScenarioRegistrar GTownsPerf(TEXT("towns-perf"), TEXT("towns"), TEXT("120 km view over the settled region: statistics"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		const dm::World* W = Ctx.Sim->Sim();
		if (!W) return;
		const int32 Sid = Pick(*W, [](const dm::Settlement& S) { return S.pop; });
		Frame(Ctx, Sid, 120.0, 0.0, 60.0, 50.0);
	}, TickLog);

// ------------------------------------------------------------------ verifier: worst case (every settlement a Space-age metropolis)
// towns-stress: at frame 5 every live settlement is planned as a tier 4 / Space age metropolis (pop 42000), blocking;
// the camera frames the most populous one from -townkm (default 40) or -cam= (e.g. orbit). Logs the blocking re-plan
// time, instance / component / triangle totals and, every 60 frames, the towns tick maximum.
// -stresstier= / -stressage= / -stresspop= change the override.
static FDmScenarioRegistrar GTownsStress(TEXT("towns-stress"), TEXT("towns"), TEXT("verifier: every settlement a Space-age metropolis (worst case)"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		const dm::World* W = Ctx.Sim->Sim();
		if (!W) return;
		const int32 Sid = Pick(*W, [](const dm::Settlement& S) { return S.pop; });
		Frame(Ctx, Sid, 40.0, 20.0, 45.0, 52.0);
	},
	[](FDmScenarioContext& Ctx, float)
	{
		ApplyFrame(Ctx);
		ADmTowns* T = ADmTowns::Find(Ctx.World);
		const dm::World* W = Ctx.Sim->Sim();
		if (!T || !W) return;
		if (Ctx.Frame == 5)
		{
			int32 Tier = 4, Age = 6; double Pop = 42000;
			FDmShotArgs::Value(TEXT("-stresstier="), Tier); FDmShotArgs::Value(TEXT("-stressage="), Age); FDmShotArgs::Value(TEXT("-stresspop="), Pop);
			for (const dm::Settlement& S : W->settlements) if (!S.dead) T->SetOverride(S.id, Tier, Pop, Age);
			const double T0 = FPlatformTime::Seconds();
			// (towns-perf) -stressasync: re-plan everything on workers and apply under the frame budget (what a live world
			// does when every culture changes age at once), with the DmTownsPerf monitor on
			const bool bAsync = FDmShotArgs::Param(TEXT("stressasync"));
			if (bAsync) { FDmTownsPerf::Get().Enable(); FDmTownsPerf::Get().BeginSegment(TEXT("stress async re-plan")); }
			T->RebuildAll(!bAsync);
			Ctx.Log(FString::Printf(TEXT("DmTownsScenario: stress tier %d age %d pop %.0f: %s re-plan of everything %.0f ms; %s"), Tier, Age, Pop, bAsync ? TEXT("asynchronous") : TEXT("blocking"), (FPlatformTime::Seconds() - T0) * 1000.0, *T->Describe()));
			T->TakeMaxTickMs();
		}
		if (Ctx.Frame > 5 && Ctx.Frame % 60 == 0)
			Ctx.Log(FString::Printf(TEXT("DmTownsScenario: stress frame %d: towns tick max %.2f ms, %d visible, %d units drawn"), Ctx.Frame, T->TakeMaxTickMs(), T->NumVisibleTowns(), T->NumUnitsDrawn()));
		if (FDmShotArgs::Param(TEXT("stressasync")) && Ctx.Frame > 5 && T->IsSettled())
		{
			static bool bLogged = false;
			if (!bLogged)
			{
				bLogged = true;
				const int32 Bad = FDmTownsPerf::Get().EndSegment();
				Ctx.Log(FString::Printf(TEXT("DmTownsScenario: stress settled at frame %d (%.1f s): %s; frames attributed to towns: %d"), Ctx.Frame, Ctx.Time, *T->Describe(), Bad));
			}
		}
	});

// ------------------------------------------------------------------ custom data probe (debug)
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/Package.h"
#include "SceneInterface.h"
#include "Game/Towns/DmTownsKit.h"
namespace { FVector GCdBase, GCdE, GCdUp; UObject* A2() { return GetTransientPackage(); } }
static FDmScenarioRegistrar GTownsCdTest(TEXT("towns-cdtest"), TEXT("towns"), TEXT("debug: per-instance custom data on ISM / HISM with parent / dynamic material / kit mesh"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene();
		const dm::World* W = Ctx.Sim->Sim();
		if (!W) return;
		const int32 Sid = Pick(*W, [](const dm::Settlement& S) { return S.pop; });
		GCdUp = DmCoords::CellDir(*W, W->settlements[size_t(Sid)].cell);
		double Lat, Lon; DmCoords::DirToLatLon(GCdUp, Lat, Lon);
		Ctx.LookAt(Lat, Lon, 0.5, 0.0, 15.0, 50.0);
		FVector N; DmCoords::Basis(GCdUp, GCdE, N);
		GCdBase = GCdUp * (DmCoords::HeightToRadiusUU(DmCoords::SurfaceHeight(*W, W->settlements[size_t(Sid)].cell)) + 4000.0);
	},
	[](FDmScenarioContext& Ctx, float)
	{
		if (Ctx.Frame != 3) return;
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Towns/M_TownDebug.M_TownDebug"));
		static UDmTownsFactory* F = nullptr;
		if (!F) { F = NewObject<UDmTownsFactory>(); F->AddToRoot(); F->Build(); }
		UStaticMesh* Kit = F->Get(EDmTownMesh::Cottage, 0).Mesh;
		FDmTownMesh Bx; Bx.Box(0, FVector3f(0, 0, 2), FVector3f(2, 2, 2), 0.f, FColor(200, 200, 200, 0), FVector2f(0, 0));
		UStaticMesh* B1c = DmTowns::MakeStaticMesh(A2(), TEXT("cd1c"), { &Bx }, { M }, { 1.f }, true, true);
		UStaticMesh* B1n = DmTowns::MakeStaticMesh(A2(), TEXT("cd1n"), { &Bx }, { M }, { 1.f }, true, false);
		UStaticMesh* B3c = DmTowns::MakeStaticMesh(A2(), TEXT("cd3c"), { &Bx, &Bx, &Bx }, { M }, { 1.f, 0.006f, 0.0018f }, true, true);
		UStaticMesh* BMd = Cube;
		if (!Cube || !M || !Kit) { Ctx.Log(TEXT("DmTownsScenario: cdtest assets missing")); return; }
		AActor* A = Ctx.World->SpawnActor<AActor>();
		USceneComponent* R = NewObject<USceneComponent>(A); A->SetRootComponent(R); R->RegisterComponent();
		// rows left to right: 0 ISM cube+parent, 1 HISM cube+parent, 2 HISM cube+MID, 3 HISM kit+debug override, 4 HISM kit + its own materials (8 floats)
		for (int32 K = 0; K < 9; ++K)
		{
			UInstancedStaticMeshComponent* C = (K == 0) ? NewObject<UInstancedStaticMeshComponent>(A) : NewObject<UHierarchicalInstancedStaticMeshComponent>(A);
			C->SetMobility(EComponentMobility::Movable);
			C->SetupAttachment(R);
			C->SetStaticMesh(K == 8 ? BMd : K == 5 ? B1c : (K == 6 ? B1n : (K == 7 ? B3c : (K >= 3 ? Kit : Cube))));
			if (K <= 1) C->SetMaterial(0, M);
			if (K == 2) C->SetMaterial(0, UMaterialInstanceDynamic::Create(M, A));
			if (K == 3) for (int32 S = 0; S < 3; ++S) C->SetMaterial(S, M);
			const int32 NF = K == 4 ? 8 : 3;
			if (K >= 5) C->SetMaterial(0, M);
			C->SetNumCustomDataFloats(NF);
			TArray<FTransform> X; TArray<float> Cd;
			for (int32 I = 0; I < 3; ++I)
			{
				X.Add(FTransform(FRotationMatrix::MakeFromZ(GCdUp).ToQuat(), GCdBase + GCdE * ((K - 2) * 3000.0 + (I - 1) * 900.0), FVector(K >= 5 ? 1.0 : (K >= 3 ? 0.5 : 4.0))));
				if (NF == 3) Cd.Append({ 0.f, 1.f, float(I) / 2.f }); else Cd.Append({ 0.2f, 1.f, 0.2f, 0.2f, 0.4f, 1.f, 0.5f, 0.f });
			}
			C->AddInstances(X, false, true, false);
			C->SetCustomData(0, X.Num() - 1, TConstArrayView<float>(Cd), false);
			C->RegisterComponent();
			const FPrimitiveMaterialPropertyDescriptor D = C->GetUsedMaterialPropertyDesc(Ctx.World->Scene->GetShaderPlatform());
			TArray<UMaterialInterface*> Used; C->GetUsedMaterials(Used);
			Ctx.Log(FString::Printf(TEXT("DmTownsScenario: cdtest row %d: %d used materials, custom data %d, random %d, mesh %s"), K, Used.Num(), D.bAnyMaterialHasPerInstanceCustomData ? 1 : 0, D.bAnyMaterialHasPerInstanceRandom ? 1 : 0, *C->GetStaticMesh()->GetName()));
		}
		Ctx.Log(TEXT("DmTownsScenario: cdtest rows left to right: ISM cube, HISM cube, HISM cube+MID, HISM kit+debug, HISM kit body, box 1 LOD colours, box 1 LOD no colours, box 3 LODs colours"));
	});
