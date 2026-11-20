// DEMIURGE — Game/Towns/DmTownsPerf.cpp : see the header; also the towns-hitch scenario.
//
//   shot.ps1 towns-hitch -noshot [-hview=city|forest|savanna|herd|orbit|overview] [-hspeeds=0.25,1,2,4,8] [-hsec=8]
//            [-hwarm=12] [-hage=0] [-hwar=0] [-hsweep=0]
// Runs the fixture static for -hwarm seconds (first wave, re-grounding), then live at every speed of -hspeeds for
// -hsec seconds each, then at 8x: every culture advances an age at once (every settlement re-plans), a war (two
// large cultures; three settlements captured, two destroyed), a camera sweep over six towns at 4x, and a pause.
// The camera follows the turning planet. Every frame over 33 ms is logged with its breakdown (FDmTownsPerf) and
// each segment ends with a summary; "DmTownsHitch RESULT PASS" when no frame over 33 ms is attributed to towns.
// With -noparts=towns the same run measures the scene without towns (the comparison baseline).

#include "Game/Towns/DmTownsPerf.h"
#include "Game/Towns/DmTownsActor.h"
#include "Game/Towns/DmTownsPlan.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmSimSubsystem.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/CoreDelegates.h"
#include "RenderTimer.h"
#include "UObject/UObjectGlobals.h"
#include "Sim/Civ.h"
#include "Sim/Society.h"
#include "Sim/State.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include <intrin.h>
#endif

DEFINE_LOG_CATEGORY_STATIC(LogDmTownsPerf, Log, All);

namespace
{
	double GCyclesPerMs = 0;   // QueryThreadCycleTime counts TSC reference cycles (calibrated against the wall clock)

	void CalibrateCycles()
	{
#if PLATFORM_WINDOWS
		ULONG64 C0 = 0, C1 = 0;
		const double W0 = FPlatformTime::Seconds();
		const uint64 R0 = __rdtsc();
		QueryThreadCycleTime(GetCurrentThread(), &C0);
		while (FPlatformTime::Seconds() - W0 < 0.02) {}
		QueryThreadCycleTime(GetCurrentThread(), &C1);
		const uint64 R1 = __rdtsc();
		const double W1 = FPlatformTime::Seconds();
		GCyclesPerMs = double(R1 - R0) / ((W1 - W0) * 1000.0);
		UE_LOG(LogDmTownsPerf, Display, TEXT("DmTownsPerf: thread CPU clock %.1f MHz (TSC); a 20 ms busy wait read %.2f of it as thread cycles (1.00 = not preempted)"),
			GCyclesPerMs / 1000.0, double(C1 - C0) / FMath::Max(double(R1 - R0), 1.0));
#endif
	}
}

double FDmTownsPerf::ThreadCpuMs()
{
#if PLATFORM_WINDOWS
	if (GCyclesPerMs <= 0) CalibrateCycles();
	ULONG64 C = 0;
	QueryThreadCycleTime(GetCurrentThread(), &C);
	return double(C) / GCyclesPerMs;
#else
	return FPlatformTime::Seconds() * 1000.0;
#endif
}

void FDmTownsPerf::NoteTickCpu(double CpuMs, const double* Ph, int32 N)
{
	FrameTownsCpuMs += CpuMs;
	for (int32 K = 0; K < N && K < 8; ++K) FramePhCpu[K] += Ph[K];
}

FDmTownsPerf& FDmTownsPerf::Get()
{
	static FDmTownsPerf* P = new FDmTownsPerf();   // never destroyed: the engine's delegates outlive static destructors
	return *P;
}

void FDmTownsPerf::Enable()
{
	if (bEnabled) return;
	bEnabled = true;
	ThreadCpuMs();   // calibrates the thread cycle clock (20 ms, once)
	FCoreDelegates::OnEndFrame.AddRaw(this, &FDmTownsPerf::OnEndFrame);
	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddRaw(this, &FDmTownsPerf::OnPreGC);
	FCoreUObjectDelegates::GetPostGarbageCollect().AddRaw(this, &FDmTownsPerf::OnPostGC);
	UE_LOG(LogDmTownsPerf, Display, TEXT("DmTownsPerf: monitor on (frames over %.0f ms are logged)"), BudgetMs);
}

void FDmTownsPerf::NoteTick(double Ms, const double* Ph, int32 N)
{
	FrameTownsMs += Ms;
	for (int32 K = 0; K < N && K < 8; ++K) FramePh[K] += Ph[K];
}

void FDmTownsPerf::OnPreGC() { GcStart = FPlatformTime::Seconds(); }
void FDmTownsPerf::OnPostGC()
{
	if (GcStart <= 0) return;
	const double Ms = (FPlatformTime::Seconds() - GcStart) * 1000.0;
	GcStart = 0;
	FrameGcMs += Ms;
	if (bInSegment) { ++SegGc; SegMaxGc = FMath::Max(SegMaxGc, Ms); }
}

void FDmTownsPerf::OnEndFrame()
{
	const double Now = FPlatformTime::Seconds();
	if (LastEnd > 0)
	{
		++FrameNo;
		const double Dt = (Now - LastEnd) * 1000.0;
		const double Game = FPlatformTime::ToMilliseconds(GGameThreadTime), Render = FPlatformTime::ToMilliseconds(GRenderThreadTime);
		const bool bLong = Dt > BudgetMs;
		// attributed to towns: over budget only because of the towns' own time, or towns alone over TownsBudgetMs (on a quiet
		// machine the scene without towns runs at 16-21 ms, so more than ~12 ms of towns work would push a frame past 33 ms)
		// Only a game-thread-bound frame can be a towns hitch: the game thread itself (GGameThreadTime: this frame's work
		// without waits) was over budget and would not be without the towns tick. A render- or GPU-bound frame (the game
		// thread waited) is counted as long but not attributed; towns' render-side cost is measured against -noparts=towns.
		const bool bGameBound = Game > BudgetMs;
		// strict: any towns time at all (>= 1 ms) tipped a game-bound frame over; attributed: towns were a real share of it
		// (>= 3 ms: a frame at 33.4 ms of game-thread work with 1.6 ms of towns is the frame's other 32 ms, not a towns hitch)
		// (towns-perf verifier) attributed by the towns' CPU time (see the header); the old wall-time rule is still counted
		const double Tc = FMath::Min(FrameTownsCpuMs, FrameTownsMs);
		const bool bStrict = bLong && bGameBound && Tc >= 1.0 && Game - Tc < BudgetMs;
		const bool bTowns = (bStrict && Tc >= 3.0) || Tc > TownsBudgetMs;
		const bool bWallTowns = (bLong && bGameBound && FrameTownsMs >= 3.0 && Game - FrameTownsMs < BudgetMs) || FrameTownsMs > TownsBudgetMs;
		const bool bStall = FrameTownsMs - Tc > 3.0;
		int32 StallPh = 0;
		for (int32 K = 1; K < 8; ++K) if (FramePh[K] - FramePhCpu[K] > FramePh[StallPh] - FramePhCpu[StallPh]) StallPh = K;
		if (bInSegment)
		{
			SegFrame.Add(float(Dt)); SegGame.Add(float(Game)); SegTowns.Add(float(FrameTownsMs));
			if (bLong) ++SegLong;
			if (bTowns) ++SegTownsLong;
			if (bStrict) ++SegStrict;
			SegTownsCpu.Add(float(Tc));
			if (bWallTowns) ++SegWallTowns;
			if (bStall) { ++SegStalls; ++SegStallPh[StallPh]; }
			SegReg += FrameRegistered; SegDes += FrameDestroyed; SegGround += FrameGroundMeshes;
			for (int32 K = 0; K < 8; ++K) SegPhMax[K] = FMath::Max(SegPhMax[K], FramePh[K]);
		}
		static const TCHAR* PhName[8] = { TEXT("follow"), TEXT("ground"), TEXT("snap"), TEXT("signatures+launch"), TEXT("apply"), TEXT("construction"), TEXT("visibility"), TEXT("units") };
		if (bStall)
			UE_LOG(LogDmTownsPerf, Display, TEXT("DmTownsPerf towns stall frame %llu [%s]: towns wall %.1f ms, CPU %.2f ms; most in '%s' (wall %.1f, CPU %.2f)%s"),
				FrameNo + 1, *Segment, FrameTownsMs, Tc, PhName[StallPh], FramePh[StallPh], FramePhCpu[StallPh], bTowns ? TEXT(" <- TOWNS") : TEXT(""));
		if (bLong || FrameTownsMs > 4.0)
			UE_LOG(LogDmTownsPerf, Display, TEXT("DmTownsPerf %s frame %llu [%s]: frame %.1f ms, game %.1f, render %.1f | towns %.1f (follow %.1f, ground %.1f, snap %.1f, signatures+launch %.1f, apply %.1f, construction %.1f, visibility %.1f, units %.1f) registered %d destroyed %d ground meshes %d (%.1f ms) | gc %.1f%s"),
				bLong ? TEXT("long") : TEXT("towns-heavy"), FrameNo, *Segment, Dt, Game, Render, FrameTownsMs, FramePh[0], FramePh[1], FramePh[2], FramePh[3], FramePh[4], FramePh[5], FramePh[6], FramePh[7],
				FrameRegistered, FrameDestroyed, FrameGroundMeshes, FrameGroundMs, FrameGcMs, bTowns ? TEXT(" <- TOWNS") : (bStrict ? TEXT(" <- (towns tipped it, under 3 ms)") : TEXT("")));
	}
	LastEnd = Now;
	FrameTownsMs = FrameGcMs = FrameGroundMs = FrameTownsCpuMs = 0;
	for (double& P : FramePh) P = 0;
	for (double& P : FramePhCpu) P = 0;
	FrameRegistered = FrameDestroyed = FrameGroundMeshes = 0;
}

void FDmTownsPerf::BeginSegment(const FString& Name)
{
	if (bInSegment) EndSegment();
	Segment = Name;
	bInSegment = true;
	SegFrame.Reset(); SegGame.Reset(); SegTowns.Reset(); SegTownsCpu.Reset();
	SegLong = SegTownsLong = SegGc = SegStrict = SegStalls = SegWallTowns = 0;
	for (int32& P : SegStallPh) P = 0;
	SegMaxGc = 0;
	SegReg = SegDes = SegGround = 0;
	for (double& P : SegPhMax) P = 0;
}

int32 FDmTownsPerf::EndSegment()
{
	if (!bInSegment) return 0;
	bInSegment = false;
	auto Pct = [](TArray<float> V, double Q) { if (V.Num() == 0) return 0.0; V.Sort(); return double(V[FMath::Clamp(int32(Q * (V.Num() - 1)), 0, V.Num() - 1)]); };
	auto Avg = [](const TArray<float>& V) { double S = 0; for (float X : V) S += X; return V.Num() ? S / V.Num() : 0.0; };
	const double MaxTowns = Pct(SegTowns, 1.0), MaxFrame = Pct(SegFrame, 1.0);
	UE_LOG(LogDmTownsPerf, Display, TEXT("DmTownsPerf segment '%s': %d frames | frame avg %.2f p99 %.2f max %.2f ms | game avg %.2f p99 %.2f max %.2f | towns tick avg %.2f p99 %.2f max %.2f | frames over %.0f ms: %d, attributed to towns: %d (strict %d) | gc %d (max %.1f ms)"),
		*Segment, SegFrame.Num(), Avg(SegFrame), Pct(SegFrame, 0.99), MaxFrame, Avg(SegGame), Pct(SegGame, 0.99), Pct(SegGame, 1.0),
		Avg(SegTowns), Pct(SegTowns, 0.99), MaxTowns, BudgetMs, SegLong, SegTownsLong, SegStrict, SegGc, SegMaxGc);
	UE_LOG(LogDmTownsPerf, Display, TEXT("DmTownsPerf segment '%s' towns detail: phase maxima follow %.1f, ground %.1f, snap %.1f, signatures+launch %.1f, apply %.1f, construction %.1f, visibility %.1f, units %.1f ms; components registered %d, destroyed %d; ground meshes %d"),
		*Segment, SegPhMax[0], SegPhMax[1], SegPhMax[2], SegPhMax[3], SegPhMax[4], SegPhMax[5], SegPhMax[6], SegPhMax[7], SegReg, SegDes, SegGround);
	UE_LOG(LogDmTownsPerf, Display, TEXT("DmTownsPerf segment '%s' towns CPU: avg %.2f p99 %.2f max %.2f ms | attributed by wall time %d | towns stalls (wall - CPU > 3 ms) %d: follow %d, ground %d, snap %d, signatures+launch %d, apply %d, construction %d, visibility %d, units %d"),
		*Segment, Avg(SegTownsCpu), Pct(SegTownsCpu, 0.99), Pct(SegTownsCpu, 1.0), SegWallTowns, SegStalls,
		SegStallPh[0], SegStallPh[1], SegStallPh[2], SegStallPh[3], SegStallPh[4], SegStallPh[5], SegStallPh[6], SegStallPh[7]);
	TotalStalls += SegStalls; TotalWallTowns += SegWallTowns; TotalMaxTownsCpu = FMath::Max(TotalMaxTownsCpu, Pct(SegTownsCpu, 1.0));
	TotalFrames += SegFrame.Num(); TotalLong += SegLong; TotalTownsLong += SegTownsLong; TotalStrict += SegStrict;
	TotalMaxTowns = FMath::Max(TotalMaxTowns, MaxTowns); TotalMaxFrame = FMath::Max(TotalMaxFrame, MaxFrame);
	return SegTownsLong;
}

// ------------------------------------------------------------------ towns-hitch scenario

namespace
{
	enum class EHAct : uint8 { None, Live, Age, War, Sweep, Pause };
	struct FHSeg { FString Name; float Speed = 0; double Seconds = 8; EHAct Act = EHAct::None; };

	struct FHitch
	{
		TArray<FHSeg> Segs;
		int32 Idx = -1;
		double SegStart = 0;
		// camera, planet frame
		FString View;
		int32 Sid = -1;                 // framed settlement (city / sweep); -1: fixed site
		FVector SiteDir = FVector::UpVector;
		double Km = 1.2, Heading = 35, Pitch = 20, Fov = 55;
		TArray<int32> SweepIds;
		int32 SweepAt = 0;
		double LastSweep = 0;
		bool bDone = false;
		int32 Inconsistent = 0;
	};
	TUniquePtr<FHitch> GH;

	int32 Largest(const dm::World& W, int32 Tier)
	{
		int32 Best = -1; double Score = -1e30;
		for (const dm::Settlement& S : W.settlements)
		{
			if (S.dead) continue;
			const double V = (Tier < 0 || S.tier == Tier ? 1e6 : -1e6 * FMath::Abs(S.tier - Tier)) + S.pop;
			if (V > Score) { Score = V; Best = S.id; }
		}
		return Best;
	}

	void Aim(FDmScenarioContext& Ctx)
	{
		FHitch& H = *GH;
		ADmTowns* T = ADmTowns::Find(Ctx.World);
		const dm::World* W = Ctx.Sim->Sim();
		if (!W) return;
		FVector Target;
		if (H.Sid >= 0 && T && T->GetPlan(H.Sid)) Target = T->GetPlan(H.Sid)->Centre;
		else
		{
			const FVector Dir = H.Sid >= 0 ? DmCoords::CellDir(*W, W->settlements[size_t(H.Sid)].cell) : H.SiteDir;
			double R = 0;
			if (T && T->HasGround()) R = T->GroundRadius(Dir);
			else
			{
				const dm::Vec3 Sd = DmCoords::UEToSim(Dir);
				R = DmCoords::HeightToRadiusUU(DmCoords::SurfaceHeight(*W, W->geo->cellAt(Sd.x, Sd.y, Sd.z)));
			}
			Target = Dir * R;
		}
		const FVector Up = Target.GetSafeNormal();
		FVector E, N; DmCoords::Basis(Up, E, N);
		const double Hd = FMath::DegreesToRadians(H.Heading), Pt = FMath::DegreesToRadians(H.Pitch);
		const FVector Hz = N * FMath::Cos(Hd) + E * FMath::Sin(Hd);
		const FVector Fwd = (Hz * FMath::Cos(Pt) - Up * FMath::Sin(Pt)).GetSafeNormal();
		const FVector CamUp = H.Pitch > 89.0 ? Hz : Up;
		const FVector Loc = Target - Fwd * H.Km * DmCoords::UUPerKm;
		const FTransform P2W = Ctx.Sim->GetPlanetToWorld();
		Ctx.SetView(P2W.TransformPosition(Loc), FRotationMatrix::MakeFromXZ(P2W.TransformVectorNoScale(Fwd), P2W.TransformVectorNoScale(CamUp)).Rotator(), H.Fov);
	}

	void AgeAll(FDmScenarioContext& Ctx)
	{
		dm::World* W = Ctx.Sim->SimMutable();
		int32 N = 0;
		for (dm::Culture& Cu : W->cultures)
		{
			if (Cu.dead || Cu.age >= dm::Age::SPACE) continue;
			const int32 Prev = Cu.age;
			++Cu.age;
			W->dirty.objects = true;
			W->bus.emit(dm::EvCultureAge{ int(Cu.id), Cu.age, Prev });
			++N;
		}
		int32 Top = W->age;
		for (const dm::Culture& Cu : W->cultures) if (!Cu.dead) Top = FMath::Max(Top, Cu.age);
		if (Top > W->age) { W->age = FMath::Min(Top, int32(dm::Age::SPACE)); W->bus.emit(dm::EvWorldAge{ W->age }); }
		Ctx.Log(FString::Printf(TEXT("DmTownsHitch: %d cultures advanced one age at once (world age %d)"), N, W->age));
	}

	void War(FDmScenarioContext& Ctx)
	{
		dm::World* W = Ctx.Sim->SimMutable();
		// the two most populous live cultures
		TArray<TPair<double, int32>> Pop;
		for (const dm::Culture& Cu : W->cultures)
		{
			if (Cu.dead) continue;
			double P = 0;
			for (const dm::Settlement& S : W->settlements) if (!S.dead && S.culture == Cu.id) P += S.pop;
			Pop.Add({ P, int32(Cu.id) });
		}
		Pop.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key > B.Key; });
		if (Pop.Num() < 2) { Ctx.Log(TEXT("DmTownsHitch: fewer than two cultures, no war")); return; }
		const int32 A = Pop[0].Value, B = Pop[1].Value;
		if (!dm::society::atWar(*W, A, B)) dm::society::declareWar(*W, A, B);
		// B's settlements nearest the framed one: three captured by A, two destroyed
		const FVector C = GH->Sid >= 0 ? DmCoords::CellDir(*W, W->settlements[size_t(GH->Sid)].cell) : GH->SiteDir;
		TArray<TPair<double, int32>> Near;
		for (const dm::Settlement& S : W->settlements)
			if (!S.dead && S.culture == B && !S.capital) Near.Add({ -FVector::DotProduct(C, DmCoords::CellDir(*W, S.cell)), S.id });
		Near.Sort([](const TPair<double, int32>& X, const TPair<double, int32>& Y) { return X.Key < Y.Key; });
		int32 Cap = 0, Des = 0;
		for (const TPair<double, int32>& Nr : Near)
		{
			if (Cap < 3) { dm::civ::capture(*W, Nr.Value, A); ++Cap; }
			else if (Des < 2) { dm::civ::destroy(*W, Nr.Value, dm::Cause::War); ++Des; }
			else break;
		}
		Ctx.Log(FString::Printf(TEXT("DmTownsHitch: war %d vs %d declared; %d settlements captured, %d destroyed"), A, B, Cap, Des));
	}

	void StartSeg(FDmScenarioContext& Ctx, int32 I)
	{
		FHitch& H = *GH;
		H.Idx = I;
		H.SegStart = Ctx.Time;
		const FHSeg& S = H.Segs[I];
		if (S.Act != EHAct::None)
		{
			if (!Ctx.Sim->IsLive()) Ctx.Sim->MakeLive();
			Ctx.Sim->SetPaused(S.Act == EHAct::Pause);
			if (S.Act != EHAct::Pause) Ctx.Sim->SetSpeed(S.Speed);
		}
		if (S.Act == EHAct::Age) AgeAll(Ctx);
		if (S.Act == EHAct::War) War(Ctx);
		if (S.Act == EHAct::Sweep) { H.SweepAt = 0; H.LastSweep = Ctx.Time; }
		FDmTownsPerf::Get().BeginSegment(S.Name);
		Ctx.Log(FString::Printf(TEXT("DmTownsHitch: segment '%s' (%.0f s)"), *S.Name, S.Seconds));
	}
}

static FDmScenarioRegistrar GTownsHitch(TEXT("towns-hitch"), TEXT("towns"), TEXT("towns frame-time attribution over static / every speed / age change / war / sweep (DmTownsPerf lines, RESULT)"),
	[](FDmScenarioContext& Ctx)
	{
		FDmTownsPerf::Get().Enable();
		GH = MakeUnique<FHitch>();
		FHitch& H = *GH;
		Ctx.BuildScene();
		Ctx.HoldExit();
		const dm::World* W = Ctx.Sim->Sim();
		if (!W) return;
		H.View = TEXT("city");
		FDmShotArgs::Value(TEXT("-hview="), H.View);
		struct FSite { const TCHAR* Name; double Lat, Lon, Km, Heading, Pitch, Fov; };
		// the nature verifier's sites (shots ue-nv-*): forest close, savanna, herd
		static const FSite Sites[] = { { TEXT("forest"), -22.093, -145.277, 1.2, 35, 20, 55 }, { TEXT("savanna"), -21.064, -170.911, 0.6, 35, 18, 55 },
			{ TEXT("herd"), -8.667, -119.630, 0.4, 35, 14, 50 } };
		bool bSite = false;
		for (const FSite& S : Sites)
			if (H.View == S.Name) { H.SiteDir = DmCoords::LatLonToDir(S.Lat, S.Lon); H.Km = S.Km; H.Heading = S.Heading; H.Pitch = S.Pitch; H.Fov = S.Fov; bSite = true; }
		if (!bSite)
		{
			H.Sid = Largest(*W, H.View == TEXT("city") ? 3 : -1);
			const dm::Settlement& S = W->settlements[size_t(FMath::Max(H.Sid, 0))];
			const dm::Culture* Cu = dm::state::culture(*W, S.culture);
			const double R = DmTownsPlan::BuiltRadius(S.tier, S.pop, Cu ? Cu->age : W->age);
			if (H.View == TEXT("orbit")) { H.Km = 700; H.Pitch = 90; H.Heading = 0; H.Fov = 30; }
			else if (H.View == TEXT("overview")) { H.Km = 40; H.Pitch = 45; H.Heading = 20; H.Fov = 35; }
			else { H.Km = FMath::Clamp(R * 5.0 / 1000.0, 0.5, 9.0); H.Pitch = 38; H.Heading = 25; H.Fov = 35; }
		}
		FDmShotArgs::Value(TEXT("-hkm="), H.Km);
		// sweep targets: the six most populous settlements
		TArray<TPair<double, int32>> ByPop;
		for (const dm::Settlement& S : W->settlements) if (!S.dead) ByPop.Add({ S.pop, S.id });
		ByPop.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key > B.Key; });
		for (int32 K = 0; K < ByPop.Num() && K < 6; ++K) H.SweepIds.Add(ByPop[K].Value);

		double Warm = 12, Sec = 8;
		FDmShotArgs::Value(TEXT("-hwarm="), Warm);
		FDmShotArgs::Value(TEXT("-hsec="), Sec);
		H.Segs.Add({ TEXT("static"), 0.f, Warm, EHAct::None });
		double Sp[8] = { 0.25, 1, 2, 4, 8, 0, 0, 0 };
		const int32 NSp = FDmShotArgs::List(TEXT("-hspeeds="), Sp, 8) > 0 ? FDmShotArgs::List(TEXT("-hspeeds="), Sp, 8) : 5;
		for (int32 K = 0; K < NSp; ++K) H.Segs.Add({ FString::Printf(TEXT("speed %gx"), Sp[K]), float(Sp[K]), Sec, EHAct::Live });
		int32 Flag = 1;
		if (!FDmShotArgs::Value(TEXT("-hage="), Flag) || Flag) H.Segs.Add({ TEXT("age change at 8x"), 8.f, 14.0, EHAct::Age });
		Flag = 1;
		if (!FDmShotArgs::Value(TEXT("-hwar="), Flag) || Flag) H.Segs.Add({ TEXT("war at 8x"), 8.f, 16.0, EHAct::War });
		Flag = 1;
		if (!FDmShotArgs::Value(TEXT("-hsweep="), Flag) || Flag) H.Segs.Add({ TEXT("camera sweep at 4x"), 4.f, 14.0, EHAct::Sweep });
		H.Segs.Add({ TEXT("paused"), 0.f, 4.0, EHAct::Pause });
		Ctx.Log(FString::Printf(TEXT("DmTownsHitch: view %s (settlement %d, %.2f km), %d segments, towns %s"), *H.View, H.Sid, H.Km, H.Segs.Num(), ADmTowns::Find(Ctx.World) ? TEXT("on") : TEXT("off")));
	},
	[](FDmScenarioContext& Ctx, float)
	{
		if (!GH || GH->bDone) return;
		FHitch& H = *GH;
		if (H.Segs.Num() == 0) { Ctx.Finish(); return; }
		if (H.Idx < 0) { if (Ctx.Frame >= 1) StartSeg(Ctx, 0); }
		else if (Ctx.Time - H.SegStart > H.Segs[H.Idx].Seconds)
		{
			if (ADmTowns* Tc = ADmTowns::Find(Ctx.World))
			{
				FString Why;
				const int32 Bad = Tc->CheckConsistency(Why);
				Ctx.Log(FString::Printf(TEXT("DmTownsHitch: consistency after '%s' %s: %s"), *H.Segs[H.Idx].Name, Bad == 0 ? TEXT("OK") : TEXT("FAILED"), *Why));
				H.Inconsistent += Bad;
			}
			// -hshot: a picture at the end of every segment (diagnostic runs only: a screenshot is itself a long frame)
			if (FDmShotArgs::Param(TEXT("hshot"))) Ctx.Shot(FString::Printf(TEXT("%s-%d"), *H.View, H.Idx));
			if (H.Idx + 1 < H.Segs.Num()) StartSeg(Ctx, H.Idx + 1);
			else
			{
				FDmTownsPerf& P = FDmTownsPerf::Get();
				P.EndSegment();
				ADmTowns* T = ADmTowns::Find(Ctx.World);
				Ctx.Log(FString::Printf(TEXT("DmTownsHitch: %s"), T ? *T->Describe() : TEXT("no towns")));
				int32 Bad = 0;
				if (T) { FString Why; Bad = T->CheckConsistency(Why); Ctx.Log(FString::Printf(TEXT("DmTownsHitch: consistency %s: %s"), Bad == 0 ? TEXT("OK") : TEXT("FAILED"), *Why)); }
				Ctx.Log(FString::Printf(TEXT("DmTownsHitch RESULT %s: consistency problems %d; %d frames, %d over %.0f ms, %d attributed to towns (strict %d; by wall time %d, towns stalls %d); max frame %.1f ms, max towns tick %.1f ms (CPU %.1f ms)"),
					P.TotalTownsLong == 0 && Bad == 0 && H.Inconsistent == 0 ? TEXT("PASS") : TEXT("FAIL"), Bad + H.Inconsistent, P.TotalFrames, P.TotalLong, FDmTownsPerf::BudgetMs, P.TotalTownsLong, P.TotalStrict, P.TotalWallTowns, P.TotalStalls, P.TotalMaxFrame, P.TotalMaxTowns, P.TotalMaxTownsCpu));
				Ctx.SetExitCode(P.TotalTownsLong == 0 && Bad == 0 && H.Inconsistent == 0 ? 0 : 1);
				H.bDone = true;
				if (FDmShotArgs::Param(TEXT("hshot"))) Ctx.Shot(H.View);
				Ctx.Finish();
				return;
			}
		}
		const FHSeg& S = H.Segs[H.Idx];
		if (S.Act == EHAct::Sweep && H.SweepIds.Num() && Ctx.Time - H.LastSweep > 2.0)
		{
			H.LastSweep = Ctx.Time;
			H.Sid = H.SweepIds[H.SweepAt++ % H.SweepIds.Num()];
			H.Km = 3.0; H.Pitch = 40; H.Fov = 40;
		}
		Aim(Ctx);
	});

// -townperf: the monitor in any scenario (long frames with their towns breakdown)
static struct FDmTownsPerfAuto
{
	FDmTownsPerfAuto()
	{
		// the whole run is one segment, summarised at exit ("DmTownsPerf segment 'run (-townperf)'")
		FCoreDelegates::OnPostEngineInit.AddLambda([]()
		{
			if (!FDmShotArgs::Param(TEXT("townperf"))) return;
			FDmTownsPerf::Get().Enable();
			FDmTownsPerf::Get().BeginSegment(TEXT("run (-townperf)"));
			FCoreDelegates::OnEnginePreExit.AddLambda([]() { FDmTownsPerf::Get().EndSegment(); });
		});
	}
} GDmTownsPerfAuto;
