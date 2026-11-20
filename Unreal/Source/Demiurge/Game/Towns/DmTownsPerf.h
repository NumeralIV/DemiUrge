// DEMIURGE — Game/Towns/DmTownsPerf.h : frame-time attribution for the towns module (wave 3 towns-perf).
//
// FDmTownsPerf is a tiny always-available recorder: ADmTowns::Tick reports its own game-thread time and phase
// breakdown every frame (Note*), and while the monitor is enabled (-townperf, or the towns-hitch scenario) an
// end-of-frame hook keeps, per frame: the wall frame time, the engine's game / render thread times, the towns
// tick, garbage-collection time, and the component work towns did (registered / destroyed / ground meshes). Any
// frame over 33 ms is logged with that breakdown ("DmTownsPerf long frame ..."); segments (named by a scenario)
// end with a summary line. A frame is attributed to towns when it is over 33 ms, its game thread was over 33 ms of work
// (GGameThreadTime, waits excluded) and would not be without the towns tick, with towns at least 3 ms of it ("strict":
// any towns time >= 1 ms, reported too); or when the towns tick alone exceeds
// TownsBudgetMs (8 ms): the machine is shared, so a busy GPU must not hide a towns cost that would break 33 ms on a quiet
// one. Render- / GPU-bound long frames are counted but not attributed (compare with -noparts=towns for those).
// (towns-perf verifier) The towns' time used for attribution is the game thread's CPU time inside the towns tick, not its wall
// time: on the shared machine the OS took the game thread's core away for 5-50 ms inside towns phases that cannot wait
// (the units loop over 29 units, moving the actor), and wall time charged those to towns. Wall-time attribution is still
// counted and reported ("by wall time"), and every towns stall (wall - CPU > 3 ms) is logged with its phase.
#pragma once

#include "CoreMinimal.h"

struct DEMIURGE_API FDmTownsPerf
{
	static FDmTownsPerf& Get();

	/** Hooks the end-of-frame and GC delegates (idempotent). */
	void Enable();
	bool IsEnabled() const { return bEnabled; }

	/** Starts a named segment (ends the current one with its summary). */
	void BeginSegment(const FString& Name);
	/** Ends the current segment and logs its summary; returns the number of frames attributed to towns. */
	int32 EndSegment();

	// ---- reported by the towns actor
	void NoteTick(double Ms, const double* Phases, int32 NumPhases);
	/** (towns-perf verifier) the same tick in CPU time of the game thread (QueryThreadCycleTime: time the thread actually ran;
	 *  excludes the OS running something else on its core). Wall minus CPU is a stall: preemption, or a blocking wait. */
	void NoteTickCpu(double CpuMs, const double* CpuPhases, int32 NumPhases);
	/** CPU time this thread has run, in ms (monotonic; Windows; elsewhere the wall clock). */
	static double ThreadCpuMs();
	void NoteComponents(int32 Registered, int32 Destroyed) { FrameRegistered += Registered; FrameDestroyed += Destroyed; }
	void NoteGroundMesh(double Ms) { FrameGroundMs += Ms; ++FrameGroundMeshes; }

	// ---- totals over every ended segment
	int32 TotalFrames = 0, TotalLong = 0, TotalTownsLong = 0, TotalStrict = 0;
	double TotalMaxTowns = 0, TotalMaxFrame = 0;

	static constexpr double BudgetMs = 33.0;
	static constexpr double TownsBudgetMs = 8.0;   // towns' own game-thread time per frame that counts as a towns hitch by itself

private:
	void OnEndFrame();
	void OnPreGC();
	void OnPostGC();

	bool bEnabled = false;
	double LastEnd = -1.0;
	uint64 FrameNo = 0;
	// this frame
	double FrameTownsMs = 0, FrameGcMs = 0, FrameGroundMs = 0, FrameTownsCpuMs = 0;
	double FramePh[8] = {}, FramePhCpu[8] = {};
	int32 FrameRegistered = 0, FrameDestroyed = 0, FrameGroundMeshes = 0;
	double GcStart = 0;
	// segment
	FString Segment;
	bool bInSegment = false;
	TArray<float> SegFrame, SegGame, SegTowns, SegTownsCpu;
	int32 SegLong = 0, SegTownsLong = 0, SegGc = 0, SegStrict = 0, SegStalls = 0, SegWallTowns = 0;
	int32 SegStallPh[8] = {};
public:
	int32 TotalStalls = 0, TotalWallTowns = 0;
	double TotalMaxTownsCpu = 0;
private:
	double SegMaxGc = 0;
	int32 SegReg = 0, SegDes = 0, SegGround = 0;
	double SegPhMax[8] = {};
};
