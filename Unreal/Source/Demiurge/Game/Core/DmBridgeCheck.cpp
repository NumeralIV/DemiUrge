// DEMIURGE â€” Game/Core/DmBridgeCheck.cpp : adversarial checks of the bridge (scenario "bridgecheck").
// Owner: C0 bridge (written by the C0 verifier). Independent of the builder's simtest / picktest:
//   - the fixture against the sim API (finite, ids, cross references, territory vs owner, units on
//     neighbour paths, settlements on land the REAL Life / Civ modules call habitable after sim::use)
//   - picking against a brute-force ray / triangle intersection of the sim surface mesh (orbit, mid,
//     near the ground, grazing), including misses at the limb
//   - the subsystem's clock with synthetic frames (RunFrame with exact dt: ticks per speed, pause,
//     hitch cap, budget drop, genesis pacing at 1x, static worlds never tick)
//   - a 400-year live run of the fixture under the real sim: every event checked against
//     kEventTable as it reaches OnSimEvent (keys, finite numbers, cell / settlement / culture ids),
//     bus count == OnSimEvent count, the world checked every 80 ticks
//   - UsePower outside the sim step reaches OnSimEvent; save / load through the subsystem (bytes,
//     scalars, refused slot names, a missing slot keeps the world, two loads advance identically,
//     first flush after a load is all-dirty)
// Every line is "DmCheck ok|FAIL ..."; the last is "DmCheck RESULT PASS|FAIL" and the exit code is 1
// on any failure. Run: shot.ps1 bridgecheck -noshot

#include "Game/Core/DmCoords.h"
#include "Game/Core/DmGameMode.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmSimSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include "Sim/Civ.h"
#include "Sim/Powers.h"
#include "Sim/Save.h"
#include "Sim/Sim.h"

namespace
{
	struct FBridgeCheck
	{
		int32 Fails = 0, Passes = 0;
		int32 Stage = 0;
		int32 StageFrame = 0;
		// events
		int64 Routed = 0, Bus = 0, BadEvents = 0;
		std::string FirstBadEvent;
		std::map<std::string, int> Types;
		int32 LastPowerCell = -2;
		uint64 BusTok = 0;
		const dm::World* BusWorld = nullptr;
		FDelegateHandle RoutedH, WorldH, DirtyH;
		FDmDirtyFlags LastDirty;
		int64 DirtyCount = 0;
		// live run
		int64 LiveTicks = 0;
		double LiveSeconds = 0, MaxTickMs = 0;
		int32 Mountain = -1;
		int64 WorldChanges = 0;
	};
	FBridgeCheck GBc;

	void Expect(FDmScenarioContext& Ctx, bool bOk, const FString& What)
	{
		if (bOk) ++GBc.Passes; else ++GBc.Fails;
		Ctx.Log(FString::Printf(TEXT("DmCheck %s %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What));
	}

	FString U(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

	// ------------------------------------------------------------------ the event contract
	std::string EventProblem(const dm::World& W, const dm::Event& E)
	{
		const int T = int(E.type);
		if (T < 0 || T >= dm::kEventCount) return "unknown event type " + std::to_string(T);
		if (int(E.data.index()) != T) return std::string(E.name()) + " carries the payload of another event";
		const dm::EventInfo& Info = dm::kEventTable[T];
		std::string Err;
		int K = 0;
		E.fields([&](const char* Key, const auto& V)
		{
			using VT = std::decay_t<decltype(V)>;
			const std::string_view KeyV(Key);
			if ((K >= Info.nKeys || KeyV != Info.keys[K]) && Err.empty()) Err = std::string(Info.name) + ": unexpected key " + Key;
			++K;
			if constexpr (std::is_floating_point_v<VT>)
			{
				if (!std::isfinite(V) && Err.empty()) Err = std::string(Info.name) + "." + Key + " is not finite";
			}
			if constexpr (std::is_same_v<VT, int>)
			{
				if (KeyV == "cell" && (V < -1 || V >= W.n) && Err.empty()) Err = std::string(Info.name) + ".cell " + std::to_string(V) + " out of range";
				if (KeyV == "settlement" && (V < -1 || V >= int(W.settlements.size())) && Err.empty()) Err = std::string(Info.name) + ".settlement " + std::to_string(V) + " names no settlement";
				if (KeyV == "culture" && (V < -1 || V >= int(W.cultures.size())) && Err.empty()) Err = std::string(Info.name) + ".culture " + std::to_string(V) + " names no culture";
			}
		});
		if (Err.empty() && K != Info.nKeys) Err = std::string(Info.name) + ": missing payload keys";
		return Err;
	}

	// ------------------------------------------------------------------ the world contract
	bool IsNb(const dm::World& W, int A, int B)
	{
		for (int J = 0; J < W.geo->nbrN[size_t(A)]; ++J) if (W.geo->nbr[size_t(6 * A + J)] == B) return true;
		return false;
	}

	// bStrict: the static fixture (everything consistent at once); otherwise a live world mid-run
	FString WorldProblem(const dm::World& W, bool bStrict)
	{
		const std::string NF = dm::sim::firstNonFinite(W);
		if (!NF.empty()) return TEXT("non-finite ") + U(NF);
		const int NS = int(W.settlements.size()), NC = int(W.cultures.size());
		for (int I = 0; I < NC; ++I)
		{
			const dm::Culture& Cu = W.cultures[size_t(I)];
			if (Cu.id != I) return FString::Printf(TEXT("culture %d has id %d"), I, Cu.id);
			if (Cu.parent < -1 || Cu.parent >= I) return FString::Printf(TEXT("culture %d parent %d"), I, Cu.parent);
			if (Cu.capital < -1 || Cu.capital >= NS) return FString::Printf(TEXT("culture %d capital %d"), I, Cu.capital);
			if (bStrict && !Cu.dead)
			{
				if (Cu.capital < 0) return FString::Printf(TEXT("live culture %d has no capital"), I);
				const dm::Settlement& Cap = W.settlements[size_t(Cu.capital)];
				if (Cap.culture != I || Cap.dead || !Cap.capital) return FString::Printf(TEXT("culture %d capital %d is not its live capital"), I, Cu.capital);
				if (Cu.lang.empty() || Cu.name.empty()) return FString::Printf(TEXT("culture %d has no language / name"), I);
			}
		}
		std::vector<int> OwnedCount(size_t(NS), 0);
		for (int C = 0; C < W.n; ++C)
		{
			const int O = W.owner[size_t(C)];
			if (O < -1 || O >= NS) return FString::Printf(TEXT("owner[%d] = %d"), C, O);
			if (O >= 0) OwnedCount[size_t(O)]++;
			if (bStrict && O >= 0 && (dm::state::isSea(W, C) || W.settlements[size_t(O)].dead)) return FString::Printf(TEXT("owner[%d] = %d on sea or dead"), C, O);
		}
		for (int I = 0; I < NS; ++I)
		{
			const dm::Settlement& S = W.settlements[size_t(I)];
			if (S.id != I) return FString::Printf(TEXT("settlement %d has id %d"), I, S.id);
			if (!W.valid(S.cell)) return FString::Printf(TEXT("settlement %d cell %d"), I, S.cell);
			if (S.culture < 0 || S.culture >= NC) return FString::Printf(TEXT("settlement %d culture %d"), I, S.culture);
			if (S.tier < 0 || S.tier > 4) return FString::Printf(TEXT("settlement %d tier %d"), I, S.tier);
			if (S.name.empty()) return FString::Printf(TEXT("settlement %d has no name"), I);
			if (bStrict)
			{
				if (S.dead || W.cultures[size_t(S.culture)].dead) return FString::Printf(TEXT("fixture settlement %d dead or of a dead culture"), I);
				if (dm::state::isSea(W, S.cell) || W.water[size_t(S.cell)] > 0.03f) return FString::Printf(TEXT("settlement %d on sea / lake"), I);
				if (W.owner[size_t(S.cell)] != I) return FString::Printf(TEXT("settlement %d does not own its centre"), I);
				if (int(S.cells.size()) != OwnedCount[size_t(I)]) return FString::Printf(TEXT("settlement %d cells %d vs owner count %d"), I, int32(S.cells.size()), OwnedCount[size_t(I)]);
				for (int32_t C : S.cells) if (!W.valid(C) || W.owner[size_t(C)] != I) return FString::Printf(TEXT("settlement %d lists cell %d it does not own"), I, C);
			}
		}
		std::vector<int> Ids;
		for (const dm::Unit& Un : W.units)
		{
			Ids.push_back(Un.id);
			if (Un.culture < -1 || Un.culture >= NC) return FString::Printf(TEXT("unit %d culture %d"), Un.id, Un.culture);
			if (Un.from < -1 || Un.from >= NS || Un.to < -1 || Un.to >= NS) return FString::Printf(TEXT("unit %d from %d to %d"), Un.id, Un.from, Un.to);
			if (Un.path.empty()) { if (bStrict) return FString::Printf(TEXT("unit %d has no path"), Un.id); continue; }
			if (Un.i < 0 || Un.i >= int(Un.path.size())) return FString::Printf(TEXT("unit %d index %d of %d"), Un.id, Un.i, int32(Un.path.size()));
			if (bStrict && Un.i >= int(Un.path.size()) - 1) return FString::Printf(TEXT("unit %d already at its end"), Un.id);
			for (size_t K = 0; K < Un.path.size(); ++K)
			{
				if (!W.valid(Un.path[K])) return FString::Printf(TEXT("unit %d path cell %d"), Un.id, Un.path[K]);
				if (K > 0 && !IsNb(W, Un.path[K - 1], Un.path[K])) return FString::Printf(TEXT("unit %d path step %d -> %d not neighbours"), Un.id, Un.path[K - 1], Un.path[K]);
			}
			if (bStrict && Un.dest >= 0 && Un.dest != Un.path.back()) return FString::Printf(TEXT("unit %d dest %d is not its path end %d"), Un.id, Un.dest, Un.path.back());
			if (bStrict && Un.sea != dm::state::isSea(W, Un.path[size_t(Un.i)])) return FString::Printf(TEXT("unit %d sea flag"), Un.id);
			if (bStrict && Un.naval == dm::NavalMode::Land)
				for (int32_t C : Un.path) if (dm::state::isSea(W, C)) return FString::Printf(TEXT("land unit %d crosses sea cell %d"), Un.id, C);
		}
		std::sort(Ids.begin(), Ids.end());
		if (std::adjacent_find(Ids.begin(), Ids.end()) != Ids.end()) return TEXT("duplicate unit ids");
		if (!Ids.empty() && Ids.back() >= W.nextId.unit) return TEXT("unit id >= nextId.unit");
		for (const dm::Disaster& D : W.disasters) if (D.cell < -1 || D.cell >= W.n) return FString::Printf(TEXT("disaster %d cell %d"), D.id, D.cell);
		for (const auto& KV : W.vars.rel)
			if (KV.first.first < 0 || KV.first.second >= NC || KV.first.first >= KV.first.second) return FString::Printf(TEXT("relation key %d|%d"), KV.first.first, KV.first.second);
		for (const dm::ChronicleEntry& E : W.chronicle)
			if (E.culture < -1 || E.culture >= NC || E.cell < -1 || E.cell >= W.n) return FString::Printf(TEXT("chronicle entry %d refs"), E.id);
		return FString();
	}

	// ------------------------------------------------------------------ brute-force picking
	void SurfaceVerts(const dm::World& W, std::vector<FVector>& V)
	{
		V.resize(size_t(W.n));
		for (int C = 0; C < W.n; ++C) V[size_t(C)] = DmCoords::CellSurfacePos(W, C);
	}

	// nearest positive hit of the planet-frame ray against every sim triangle (Moller-Trumbore)
	bool BruteRay(const dm::World& W, const std::vector<FVector>& V, const FVector& O, const FVector& D, double& OutT)
	{
		OutT = 1e300;
		bool bHit = false;
		const std::vector<uint32_t>& Tr = W.geo->tris;
		for (size_t I = 0; I + 2 < Tr.size(); I += 3)
		{
			const FVector& A = V[Tr[I]];
			const FVector E1 = V[Tr[I + 1]] - A, E2 = V[Tr[I + 2]] - A;
			const FVector P = FVector::CrossProduct(D, E2);
			const double Det = FVector::DotProduct(E1, P);
			if (FMath::Abs(Det) < 1e-9) continue;
			const double Inv = 1.0 / Det;
			const FVector S = O - A;
			const double Ub = FVector::DotProduct(S, P) * Inv;
			if (Ub < -1e-9 || Ub > 1.0 + 1e-9) continue;
			const FVector Q = FVector::CrossProduct(S, E1);
			const double Vb = FVector::DotProduct(D, Q) * Inv;
			if (Vb < -1e-9 || Ub + Vb > 1.0 + 1e-9) continue;
			const double T = FVector::DotProduct(E2, Q) * Inv;
			if (T > 0 && T < OutT) { OutT = T; bHit = true; }
		}
		return bHit;
	}

	void PickCheck(FDmScenarioContext& Ctx, const TCHAR* Label, int32 Rays, uint32 Seed, double MinSameCellPct)
	{
		const dm::World* W = Ctx.Sim->Sim();
		APlayerController* PC = Ctx.World->GetFirstPlayerController();
		if (!W || !PC) { Expect(Ctx, false, FString::Printf(TEXT("pick %s: no world / controller"), Label)); return; }
		std::vector<FVector> V;
		SurfaceVerts(*W, V);
		int32 SX = 0, SY = 0;
		PC->GetViewportSize(SX, SY);
		const FTransform P2W = Ctx.Sim->GetPlanetToWorld();
		dm::Rng R(Seed);
		int32 BothMiss = 0, BothHit = 0, Same = 0, Nb = 0, Other = 0, HitMiss = 0;
		double SumDm = 0, MaxDm = 0, Secs = 0;
		FString Worst;
		for (int32 I = 0; I < Rays; ++I)
		{
			const FVector2D Px(R.f() * SX, R.f() * SY);
			FVector WO, WD;
			if (!PC->DeprojectScreenPositionToWorld(float(Px.X), float(Px.Y), WO, WD)) continue;
			FDmPick P;
			const double T0 = FPlatformTime::Seconds();
			const bool bPick = Ctx.Sim->PickRay(WO, WD, P);
			Secs += FPlatformTime::Seconds() - T0;
			const FVector O = P2W.InverseTransformPosition(WO);
			const FVector D = P2W.InverseTransformVectorNoScale(WD).GetSafeNormal();
			double T = 0;
			const bool bBrute = BruteRay(*W, V, O, D, T);
			if (!bPick && !bBrute) { ++BothMiss; continue; }
			if (bPick != bBrute)
			{
				++HitMiss;
				if (Worst.IsEmpty()) Worst = FString::Printf(TEXT("pixel (%.0f, %.0f): picker %s, mesh %s"), Px.X, Px.Y, bPick ? TEXT("hit") : TEXT("miss"), bBrute ? TEXT("hit") : TEXT("miss"));
				continue;
			}
			++BothHit;
			const FVector Hit = O + D * T;
			const dm::Vec3 S = DmCoords::UEToSim(Hit.GetSafeNormal());
			const int32 TrueCell = W->geo->cellAt(S.x, S.y, S.z);
			const double Dm = FMath::Abs(P.Distance - T) / 100.0;
			SumDm += Dm;
			MaxDm = FMath::Max(MaxDm, Dm);
			if (P.Cell == TrueCell) ++Same;
			else if (IsNb(*W, P.Cell, TrueCell)) ++Nb;
			else
			{
				++Other;
				if (Worst.IsEmpty()) Worst = FString::Printf(TEXT("pixel (%.0f, %.0f): picker cell %d, mesh cell %d, distance %.0f vs %.0f m"), Px.X, Px.Y, P.Cell, TrueCell, P.Distance / 100.0, T / 100.0);
			}
		}
		const double Pct = BothHit ? 100.0 * Same / BothHit : 0.0;
		const bool bOk = BothHit > 0 && Pct >= MinSameCellPct && Other * 1000 <= BothHit * 2 && HitMiss * 200 <= FMath::Max(1, BothHit + BothMiss);
		Expect(Ctx, bOk, FString::Printf(TEXT("pick %s vs brute-force mesh: %d rays, both hit %d (same cell %.2f%%, neighbour %d, other %d), both miss %d, hit/miss disagree %d; distance error mean %.2f m max %.2f m; %.1f us per pick%s%s"),
			Label, Rays, BothHit, Pct, Nb, Other, BothMiss, HitMiss, BothHit ? SumDm / BothHit : 0.0, MaxDm, Rays ? Secs * 1e6 / Rays : 0.0,
			Worst.IsEmpty() ? TEXT("") : TEXT("; first: "), *Worst));
	}

	// ------------------------------------------------------------------ subscriptions
	void SubscribeBus(const dm::World* W)
	{
		GBc.BusWorld = W;
		if (!W) return;
		dm::World* MW = const_cast<dm::World*>(W);   // test only: a counting subscriber on the world's own bus
		GBc.BusTok = MW->bus.onAll([](const dm::Event&) { GBc.Bus++; });
	}

	void Subscribe(FDmScenarioContext& Ctx)
	{
		UDmSimSubsystem* Sim = Ctx.Sim;
		GBc.RoutedH = Sim->OnSimEvent.AddLambda([Sim](const dm::Event& E)
		{
			GBc.Routed++;
			GBc.Types[E.name()]++;
			if (const dm::EvPowerUsed* Pw = E.as<dm::EvPowerUsed>()) GBc.LastPowerCell = Pw->cell;
			const dm::World* W = Sim->Sim();
			const std::string Err = W ? EventProblem(*W, E) : std::string("event without a world");
			if (!Err.empty()) { GBc.BadEvents++; if (GBc.FirstBadEvent.empty()) GBc.FirstBadEvent = Err; }
		});
		GBc.WorldH = Sim->OnWorldChanged.AddLambda([](const dm::World* W) { GBc.WorldChanges++; SubscribeBus(W); });
		GBc.DirtyH = Sim->Dirty().OnDirty.AddLambda([](const FDmDirtyFlags& F) { GBc.LastDirty = F; GBc.DirtyCount++; });
		SubscribeBus(Sim->Sim());
	}

	void ResetCounts() { GBc.Routed = GBc.Bus = GBc.BadEvents = 0; GBc.FirstBadEvent.clear(); GBc.Types.clear(); }

	FString TypesText()
	{
		FString S;
		for (const auto& P : GBc.Types) S += FString::Printf(TEXT("%s%s %d"), S.IsEmpty() ? TEXT("") : TEXT(", "), *U(P.first), P.second);
		return S;
	}

	int32 FindMountain(const dm::World& W)
	{
		int32 Best = -1;
		float BestE = -1e9f;
		for (int32 C = 0; C < W.n; ++C)
			if (W.elev[size_t(C)] > BestE && FMath::Abs(W.geo->lat[size_t(C)]) < 0.9f) { BestE = W.elev[size_t(C)]; Best = C; }
		return Best;
	}

	void LookAtCell(FDmScenarioContext& Ctx, int32 Cell, double DistKm, double Heading, double Pitch, double Fov)
	{
		const dm::World* W = Ctx.Sim->Sim();
		Ctx.LookAt(FMath::RadiansToDegrees(double(W->geo->lat[size_t(Cell)])), FMath::RadiansToDegrees(double(W->geo->lon[size_t(Cell)])), DistKm, Heading, Pitch, Fov);
	}

	int64 TicksOf(UDmSimSubsystem* Sim, TFunctionRef<void()> Fn)
	{
		const int64 T0 = Sim->GetTicksRun();
		Fn();
		return Sim->GetTicksRun() - T0;
	}
}

static FDmScenarioRegistrar GDmScenarioBridgeCheck(TEXT("bridgecheck"), TEXT("core"),
	TEXT("verifier: fixture vs sim API, brute-force picking, synthetic-frame clock, live run with event contract, save / load"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.HoldExit();
		Ctx.LookAt(20.0, 30.0, 700.0, 0.0, 90.0, 30.0);
		Subscribe(Ctx);
		Ctx.Log(FString::Printf(TEXT("DmCheck start: world from %s, live %d"), *Ctx.Sim->GetWorldSource(), Ctx.Sim->IsLive() ? 1 : 0));
	},
	[](FDmScenarioContext& Ctx, float)
	{
		FBridgeCheck& B = GBc;
		UDmSimSubsystem* Sim = Ctx.Sim;
		++B.StageFrame;
		auto Next = [&B]() { B.Stage++; B.StageFrame = 0; };
		const dm::World* W = Sim->Sim();
		if (!W && B.Stage < 90) { Expect(Ctx, false, TEXT("no world")); B.Stage = 90; }

		switch (B.Stage)
		{
		case 0:   // the static fixture
		{
			if (B.StageFrame < 3) break;
			Expect(Ctx, Sim->GetWorldSource().StartsWith(TEXT("fixture")) && !Sim->IsLive(), FString::Printf(TEXT("default world is the static fixture (%s)"), *Sim->GetWorldSource()));
			const FString P = WorldProblem(*W, true);
			Expect(Ctx, P.IsEmpty(), TEXT("fixture world contract (finite, ids, capitals, territory == owner, units on neighbour paths, dest = path end, land units on land): ") + (P.IsEmpty() ? FString(TEXT("holds")) : P));
			int32 Live = 0, Low = 0;
			double MinHab = 1;
			for (const dm::Settlement& S : W->settlements) { if (S.dead) continue; ++Live; MinHab = FMath::Min(MinHab, double(W->hab[size_t(S.cell)])); if (W->hab[size_t(S.cell)] < 0.3f) ++Low; }
			Expect(Ctx, Live >= 60 && Low == 0, FString::Printf(TEXT("fixture: %d live settlements, min lab hab under a centre %.2f, %d below 0.3"), Live, MinHab, Low));
			Expect(Ctx, W->units.size() >= 20 && W->cultures.size() >= 4 && !W->rivers.empty() && !W->chronicle.empty(), FString::Printf(TEXT("fixture population: %d units, %d cultures, %d rivers, %d chronicle entries, %d relations"),
				int32(W->units.size()), int32(W->cultures.size()), int32(W->rivers.size()), int32(W->chronicle.size()), int32(W->vars.rel.size())));
			// a static world never ticks, whatever the speed
			Sim->SetSpeed(8.f);
			const int64 Ran = TicksOf(Sim, [Sim]() { for (int K = 0; K < 8; ++K) Sim->RunFrame(0.125f); });
			Expect(Ctx, Ran == 0 && Sim->GetTickRate() == 0.0, FString::Printf(TEXT("static fixture at 8x: %lld ticks in 8 synthetic frames (tick rate %.1f)"), (long long)Ran, Sim->GetTickRate()));
			Sim->SetPaused(true);
			B.Mountain = FindMountain(*W);
			Next();
			break;
		}
		case 1: PickCheck(Ctx, TEXT("orbit 700 km"), 400, 21u, 99.0); LookAtCell(Ctx, B.Mountain, 120.0, 20.0, 50.0, 40.0); Next(); break;
		case 2: if (B.StageFrame < 3) break; PickCheck(Ctx, TEXT("mid 120 km pitch 50"), 300, 22u, 99.0); LookAtCell(Ctx, B.Mountain, 3.0, 200.0, 12.0, 70.0); Next(); break;
		case 3: if (B.StageFrame < 3) break; PickCheck(Ctx, TEXT("ground 3 km pitch 12"), 300, 23u, 98.0); LookAtCell(Ctx, B.Mountain, 0.8, 110.0, 3.0, 75.0); Next(); break;
		case 4: if (B.StageFrame < 3) break; PickCheck(Ctx, TEXT("grazing 0.8 km pitch 3"), 300, 24u, 95.0); Ctx.LookAt(20.0, 30.0, 700.0, 0.0, 90.0, 30.0); Next(); break;
		case 5:   // hand the fixture to the real sim
		{
			std::vector<float> LabHab(W->hab.begin(), W->hab.end());
			const double T0 = FPlatformTime::Seconds();
			Expect(Ctx, Sim->MakeLive() && Sim->IsLive(), FString::Printf(TEXT("MakeLive (sim::use) in %.0f ms"), (FPlatformTime::Seconds() - T0) * 1000.0));
			W = Sim->Sim();
			const FString P = WorldProblem(*W, false);
			Expect(Ctx, P.IsEmpty(), TEXT("live fixture right after sim::use: ") + (P.IsEmpty() ? FString(TEXT("contract holds")) : P));
			// does the REAL Life agree the towns stand on habitable land?
			double SumLab = 0, SumReal = 0, SumLand = 0; int32 N = 0, NL = 0, Barren = 0, SeaC = 0;
			for (const dm::Settlement& S : W->settlements)
			{
				if (S.dead) continue;
				++N; SumLab += LabHab[size_t(S.cell)]; SumReal += W->hab[size_t(S.cell)];
				const dm::civ::SettleCheck SC = dm::civ::canSettle(*W, S.cell);
				if (SC.reason == dm::civ::SettleReason::Barren) ++Barren;
				if (SC.reason == dm::civ::SettleReason::Sea) ++SeaC;
			}
			for (int C = 0; C < W->n; ++C) if (!dm::state::isSea(*W, C)) { SumLand += W->hab[size_t(C)]; ++NL; }
			const double MeanReal = N ? SumReal / N : 0, MeanLand = NL ? SumLand / NL : 0;
			Expect(Ctx, N > 0 && Barren == 0 && SeaC == 0 && MeanReal > 2.0 * MeanLand, FString::Printf(TEXT("settlements vs the real sim's habitability: mean hab under %d towns %.2f (lab %.2f), land mean %.2f; civ::canSettle says barren %d, sea %d"),
				N, MeanReal, N ? SumLab / N : 0.0, MeanLand, Barren, SeaC));
			Next();
			break;
		}
		case 6:   // the clock with synthetic frames (exact dt)
		{
			Sim->SetSpeed(1.f);
			struct FRow { float Speed; int64 Expect; };
			const FRow Rows[] = { { 0.25f, 2 }, { 1.f, 8 }, { 2.f, 16 }, { 4.f, 32 }, { 8.f, 64 } };
			FString Txt;
			bool bAll = true;
			for (const FRow& Rw : Rows)
			{
				Sim->SetSpeed(Rw.Speed);
				const double T0 = FPlatformTime::Seconds();
				const int64 Dropped0 = Sim->GetTicksDropped();
				const int64 Ran = TicksOf(Sim, [Sim]() { for (int K = 0; K < 8; ++K) Sim->RunFrame(0.125f); });
				const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
				// one second of synthetic frames: exactly Expect ticks unless the 12 ms budget dropped some
				const bool bOk = Ran == Rw.Expect || (Ran < Rw.Expect && Sim->GetTicksDropped() > Dropped0);
				bAll &= bOk;
				Txt += FString::Printf(TEXT(" %.2fx: %lld (want %lld, %.1f ms)"), Rw.Speed, (long long)Ran, (long long)Rw.Expect, Ms);
			}
			Expect(Ctx, bAll, TEXT("ticks per synthetic second (8 frames of 0.125 s):") + Txt);
			Sim->SetSpeed(3.f); const float S3 = Sim->GetSpeed();
			Sim->SetSpeed(100.f); const float S100 = Sim->GetSpeed();
			Sim->SetSpeed(0.1f); const float S01 = Sim->GetSpeed();
			Sim->SetSpeed(-2.f); const bool bNegPause = Sim->IsPaused();
			Expect(Ctx, (S3 == 2.f || S3 == 4.f) && S100 == 8.f && S01 == 0.25f && bNegPause, FString::Printf(TEXT("speed snapping: 3 -> %.2f, 100 -> %.2f, 0.1 -> %.2f, -2 pauses %d"), S3, S100, S01, bNegPause ? 1 : 0));
			const int64 Paused = TicksOf(Sim, [Sim]() { for (int K = 0; K < 8; ++K) Sim->RunFrame(0.25f); });
			Expect(Ctx, Paused == 0 && Sim->GetTickRate() == 0.0, FString::Printf(TEXT("paused: %lld ticks in 2 synthetic seconds"), (long long)Paused));
			Sim->SetSpeed(8.f);
			const int64 Hitch = TicksOf(Sim, [Sim]() { Sim->RunFrame(10.f); });
			const int64 After = TicksOf(Sim, [Sim]() { Sim->RunFrame(0.0f); });
			Expect(Ctx, Hitch <= Sim->MaxTicksPerFrame && After <= 2, FString::Printf(TEXT("10 s hitch at 8x: %lld ticks in that frame (cap %d), %lld in the next zero-length frame (no spiral)"), (long long)Hitch, Sim->MaxTicksPerFrame, (long long)After));
			const float Budget = Sim->SimBudgetMs;
			Sim->SimBudgetMs = 0.0001f;
			const int64 Drop0 = Sim->GetTicksDropped();
			const int64 Tight = TicksOf(Sim, [Sim]() { Sim->RunFrame(0.25f); });
			const int64 Dropped = Sim->GetTicksDropped() - Drop0;
			Sim->SimBudgetMs = Budget;
			Expect(Ctx, Tight == 1 && Dropped >= 10, FString::Printf(TEXT("budget exhausted: %lld tick run, %lld dropped (backlog capped)"), (long long)Tight, (long long)Dropped));
			Sim->SetPaused(true);
			ResetCounts();
			B.LiveTicks = 0; B.LiveSeconds = 0;
			Next();
			break;
		}
		case 7:   // 400 years of the live fixture under the real sim, 80 ticks per frame
		{
			for (int K = 0; K < 80; ++K)
			{
				const double T0 = FPlatformTime::Seconds();
				Sim->StepTicks(1);
				const double Dt1 = FPlatformTime::Seconds() - T0;
				B.LiveSeconds += Dt1;
				B.MaxTickMs = FMath::Max(B.MaxTickMs, Dt1 * 1000.0);
			}
			B.LiveTicks += 80;
			W = Sim->Sim();
			const FString P = WorldProblem(*W, false);
			if (!P.IsEmpty()) { Expect(Ctx, false, FString::Printf(TEXT("live run at year %.1f: %s"), W->year, *P)); Next(); break; }
			if (B.LiveTicks < 1600) break;
			int32 Live = 0; for (const dm::Settlement& S : W->settlements) if (!S.dead) ++Live;
			Expect(Ctx, true, FString::Printf(TEXT("live run: %lld ticks to year %.1f, world contract held every 80 ticks; %d settlements (%d alive), %d cultures, %d units, pop %.0f; %.3f ms per tick (8x needs %.1f ms of sim per real second)"),
				(long long)B.LiveTicks, W->year, int32(W->settlements.size()), Live, int32(W->cultures.size()), int32(W->units.size()), W->stats.pop,
				B.LiveSeconds * 1000.0 / B.LiveTicks, 64.0 * B.LiveSeconds * 1000.0 / B.LiveTicks));
			Expect(Ctx, B.Routed > 100 && B.Routed == B.Bus && B.BadEvents == 0, FString::Printf(TEXT("live events: bus %lld, OnSimEvent %lld, contract violations %lld%s%s; types: %s"),
				(long long)B.Bus, (long long)B.Routed, (long long)B.BadEvents, B.FirstBadEvent.empty() ? TEXT("") : TEXT(" first: "), *U(B.FirstBadEvent), *TypesText()));
			const bool bVariety = B.Types.count("settlement:founded") && B.Types.count("chronicle:entry") && B.Types.count("trade:route");
			Expect(Ctx, bVariety, TEXT("live events include foundings, chronicle entries and trade routes"));
			int32 Wars = 0; for (const auto& KV : W->vars.rel) if (KV.second.war) ++Wars;
			Ctx.Log(FString::Printf(TEXT("DmCheck info: after the live run %d relations at war; battle events %d, war:declared %d, war:ended %d; slowest single tick %.2f ms"),
				Wars, B.Types.count("battle") ? B.Types["battle"] : 0, B.Types.count("war:declared") ? B.Types["war:declared"] : 0, B.Types.count("war:ended") ? B.Types["war:ended"] : 0, B.MaxTickMs));
			Next();
			break;
		}
		case 8:   // a power used outside the sim step
		{
			dm::World* MW = Sim->SimMutable();
			int32 Cell = -1;
			for (const dm::Culture& Cu : MW->cultures) if (!Cu.dead && Cu.capital >= 0) { Cell = MW->settlements[size_t(Cu.capital)].cell; break; }
			MW->god.awe = FMath::Max(MW->god.awe, 400.0);   // test only
			const dm::powers::CanResult Can = dm::powers::can(*MW, "rain", Cell);
			const int64 R0 = B.Routed;
			B.LastPowerCell = -2;
			// rain is a brush: the press (dt 0) then held for a second of real time; power:used is paced
			bool bUsed = Sim->UsePower(TEXT("rain"), Cell, 0.f);
			for (int K = 0; K < 10; ++K) bUsed &= Sim->UsePower(TEXT("rain"), Cell, 0.1f);
			Expect(Ctx, Can.ok && bUsed && B.LastPowerCell == Cell && B.Routed > R0 && B.Routed == B.Bus, FString::Printf(TEXT("UsePower(rain, %d) held 1 s: can %d, used %d, power:used routed with cell %d, %lld events from it"),
				Cell, Can.ok ? 1 : 0, bUsed ? 1 : 0, B.LastPowerCell, (long long)(B.Routed - R0)));
			Expect(Ctx, !Sim->UsePower(TEXT("no-such-power"), Cell, 0.f) && !Sim->UsePower(TEXT("rain"), -5, 0.f) && !Sim->UsePower(TEXT("rain"), 99999999, 0.f), TEXT("UsePower refuses an unknown power and bad cells"));
			Next();
			break;
		}
		case 9:   // save / load through the subsystem
		{
			Sim->SetPaused(true);
			std::vector<uint8_t> Before, Requant, RawBytes;
			dm::state::serialize(*W, Before);
			if (auto Q = dm::state::deserialize(Before)) dm::state::serialize(*Q, Requant);
			const double Year = W->year; const int64 Tick = W->tick; const uint32 RngS = W->rng.s;
			const int32 NS = int32(W->settlements.size()), NU = int32(W->units.size()), NE = int32(W->chronicle.size());
			FString Err;
			const bool bSaved = Sim->SaveSlot(TEXT("bridgecheck"), &Err);
			const std::string Dir = TCHAR_TO_UTF8(*Sim->GetSaveDir());
			std::string Why;
			if (auto Raw = dm::save::readSlot(Dir, "bridgecheck", &Why)) dm::state::serialize(*Raw, RawBytes);
			Expect(Ctx, bSaved && !RawBytes.empty() && RawBytes == Requant, FString::Printf(TEXT("SaveSlot: saved %d; the file's state bytes equal the world's (%d bytes)"), bSaved ? 1 : 0, int32(RawBytes.size())));
			// refused names, a missing slot keeps the current world
			const FString Outside = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("escape.dmsave"));
			IFileManager::Get().Delete(*Outside, false, true, true);
			const bool bEvil = Sim->SaveSlot(TEXT("../escape")) || Sim->SaveSlot(TEXT("")) || Sim->SaveSlot(TEXT("a/b")) || Sim->SaveSlot(TEXT("C:/x"));
			Expect(Ctx, !bEvil && !FPaths::FileExists(Outside), TEXT("SaveSlot refuses '../escape', '', 'a/b', 'C:/x' and writes nothing outside SaveGames"));
			const dm::World* Was = Sim->Sim();
			const int64 Changes0 = B.WorldChanges;
			FString LErr;
			const bool bMissing = Sim->LoadSlot(TEXT("no-such-slot-xyz"), &LErr);
			Expect(Ctx, !bMissing && Sim->Sim() == Was && B.WorldChanges == Changes0, TEXT("LoadSlot of a missing slot fails and keeps the world: ") + LErr);
			// diverge, then load back
			Sim->StepTicks(40);
			const bool bLoaded = Sim->LoadSlot(TEXT("bridgecheck"), &Err);
			W = Sim->Sim();
			const bool bSame = bLoaded && W->year == Year && W->tick == Tick && W->rng.s == RngS && int32(W->settlements.size()) == NS && int32(W->units.size()) == NU && int32(W->chronicle.size()) == NE;
			Expect(Ctx, bSame && Sim->IsLive() && B.WorldChanges == Changes0 + 1, FString::Printf(TEXT("LoadSlot after 40 more ticks: year %.2f tick %lld rng and entity counts back to the save (%d), live %d"), W->year, (long long)W->tick, bSame ? 1 : 0, Sim->IsLive() ? 1 : 0));
			// two independent loads advance identically (determinism across save / load)
			auto A1 = dm::save::readSlot(Dir, "bridgecheck", &Why);
			auto A2 = dm::save::readSlot(Dir, "bridgecheck", &Why);
			uint32 F1 = 0, F2 = 0, F3 = 0;
			if (A1 && A2)
			{
				dm::sim::use(*A1); dm::sim::advance(*A1, 160); F1 = dm::sim::fingerprint(*A1);
				dm::sim::use(*A2); dm::sim::advance(*A2, 160); F2 = dm::sim::fingerprint(*A2);
			}
			Sim->StepTicks(160);
			F3 = dm::sim::fingerprint(*Sim->Sim());
			Expect(Ctx, A1 && A2 && F1 == F2 && F1 == F3, FString::Printf(TEXT("determinism: two loads + the subsystem's loaded world, 160 ticks each: fingerprints %08x %08x %08x"), F1, F2, F3));
			Sim->RemoveSlot(TEXT("bridgecheck"));
			B.DirtyCount = 0;
			Next();
			break;
		}
		case 10:   // the first flush after the load reported everything (it ran at this frame's start)
		{
			const FDmDirtyFlags All = FDmDirtyFlags::All();
			const FDmDirtyFlags& L = B.LastDirty;
			Expect(Ctx, B.DirtyCount >= 1 && L.bElev == All.bElev && L.bColor && L.bWater && L.bObjects && L.bRivers && L.bRoads, FString::Printf(TEXT("first dirty flush after the load: {%s}"), *L.ToString()));
			Next();
			break;
		}
		case 11:   // genesis pacing and events lost while a world is created
		{
			Sim->CreateWorld(9u, TEXT("Pace"), false);
			Sim->SetSpeed(8.f);
			const int64 Ran = TicksOf(Sim, [Sim]() { for (int K = 0; K < 8; ++K) Sim->RunFrame(0.125f); });
			Expect(Ctx, Sim->Sim()->phase != dm::Phase::History && Ran == 8 && Sim->GetTickRate() == 8.0, FString::Printf(TEXT("genesis (%s) at 8x runs at 1x: %lld ticks per synthetic second, tick rate %.1f"),
				UTF8_TO_TCHAR(dm::toStr(Sim->Sim()->phase)), (long long)Ran, Sim->GetTickRate()));
			// informational: what a skip / load emits before the subsystem can route it
			dm::sim::WorldOpts O; O.seed = 3;
			auto Wk = dm::sim::create(O);
			int Lost = 0;
			std::map<std::string, int> LostTypes;
			Wk->bus.onAll([&Lost, &LostTypes](const dm::Event& E) { ++Lost; LostTypes[E.name()]++; });
			dm::sim::genesis::SkipOpts SO;
			dm::sim::genesis::skip(*Wk, SO);
			FString LT;
			for (const auto& P : LostTypes) LT += FString::Printf(TEXT(" %s %d"), *U(P.first), P.second);
			Ctx.Log(FString::Printf(TEXT("DmCheck info: genesis::skip emits %d events before CreateWorld adopts the world (not routed; listeners rebuild on OnWorldChanged):%s"), Lost, *LT));
			Sim->SetPaused(true);
			Next();
			break;
		}
		default:
		{
			if (B.Stage == 90 || B.Stage == 12)
			{
				Ctx.Log(FString::Printf(TEXT("DmCheck RESULT %s (%d ok, %d failed)"), B.Fails ? TEXT("FAIL") : TEXT("PASS"), B.Passes, B.Fails));
				Ctx.SetExitCode(B.Fails ? 1 : 0);
				B.Stage = 100;
				Ctx.Finish();
			}
			break;
		}
		}
	});
