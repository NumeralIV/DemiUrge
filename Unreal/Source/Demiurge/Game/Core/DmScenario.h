// DEMIURGE — Game/Core/DmScenario.h : the scenario / shot harness (SPEC-UNREAL 0.5).
// Owner: C0 bridge.
//
// A scenario sets up a world state, a camera and a time, then the harness shoots. Register one from
// any area (any .cpp; static registration):
//
//   static FDmScenarioRegistrar GCoastScenario(TEXT("planet-coast"), TEXT("planet"),
//       TEXT("a coast at mid zoom"),
//       [](FDmScenarioContext& Ctx)
//       {
//           Ctx.BuildScene();                               // every World part (planet, sky, ...)
//           Ctx.LookAt(12.0, -126.6, 60.0, 30.0, 35.0, 40.0);   // lat, lon, distKm, heading, pitch, fov
//           Ctx.SetSubsolar(10.0, -100.0);
//       },
//       [](FDmScenarioContext& Ctx, float Dt) { /* optional per-frame work: Ctx.Frame, Ctx.Time */ });
//
// Run it (writes shots/ue-planet-coast.png, log in <project>/Saved/Logs/shot-planet-coast.log):
//   powershell -NoProfile -ExecutionPolicy Bypass -File Unreal/Lanes/<key>/shot.ps1 planet-coast
//
// Command line (all optional; FDmShotArgs parses them once):
//   -scenario=<name>     scenario to run (an unknown name falls back to the longest registered name
//                        made of whole '-' words of it: "orbit-night" and "bridge-orbit-night" run
//                        "orbit"); without it the game runs "play"
//   -shot=<png>          take a screenshot at frame -warm, then exit
//   -warm=<frames>       frames before the shot (default 240: clouds, Lumen and TSR settle)
//   -seq=a,b,c           extra shots at these frames: <shot>-f<frame>.png (the last one also exits)
//   -cam=lat,lon,distKm[,heading,pitch,fov]   camera looking at a surface point (pitch 90 = straight
//                        down, heading 0 = north up, 90 = looking east (compass, clockwise); fov
//                        horizontal degrees); overrides the scenario's. Latitude / longitude are the
//                        sim's (geo lat / lon in degrees; longitude increases WESTWARD, DmCoords.h)
//   -sun=lat,lon         subsolar point in planet coordinates (pins the sun)
//   -year=<y>            world year (seasons, history time)
//   -fixture=<name|path|none>   fixture planet (default planet_7); -seed=<n> creates a sim world instead
//                        (-skip: straight to History); -fixtureage=<0..6>, -fixturetowns=<n>, -fixtureseed=<n>
//   -live                hand the fixture to the real sim; -speed=<0|0.25|1|2|4|8> (default 0 = paused)
//   -ev=<stops>          exposure bias (manual exposure)
//   -res=WxH             render resolution (default 1920x1080)
//   -parts=a,b / -noparts=a,b   which scene parts to spawn (DmScene.h)
//   -exitafter=<frames>  quit after this many frames even without a shot
// The harness logs "DmStats" (frame / game / render / GPU ms averaged over the 60 frames before the
// first shot) and "DmShot <path>" for each screenshot written.
#pragma once

#include "CoreMinimal.h"
#include "Game/Core/DmScene.h"

class ADmGameMode;
class APlayerController;
class UDmSimSubsystem;
class UWorld;

struct DEMIURGE_API FDmShotArgs
{
	FString Scenario;
	FString Shot;                // absolute png path or empty
	int32 Warm = 240;
	TArray<int32> Seq;
	bool bCam = false;           double Cam[6] = { 0, 0, 700, 0, 90, 30 };
	bool bSun = false;           double Sun[2] = { 0, 0 };
	bool bYear = false;          double Year = 0;
	FString Fixture = TEXT("planet_7");
	bool bSeed = false;          uint32 Seed = 1;
	bool bSkip = false;
	int32 FixtureAge = -1, FixtureTowns = -1, FixtureSeed = -1;
	bool bLive = false;
	float Speed = 0.f;
	double Ev = 0;
	FString Res = TEXT("1920x1080");
	int32 ExitAfter = -1;

	static const FDmShotArgs& Get();
	/** Generic access for module-specific switches: Value(TEXT("-mything="), Out). */
	static bool Value(const TCHAR* Key, FString& Out);
	static bool Value(const TCHAR* Key, double& Out);
	static bool Value(const TCHAR* Key, int32& Out);
	static bool Param(const TCHAR* Name);   // -name
	/** Comma list of numbers; returns how many were read (Out keeps defaults beyond them). */
	static int32 List(const TCHAR* Key, double* Out, int32 Max);
};

class DEMIURGE_API FDmScenarioContext
{
public:
	UWorld* World = nullptr;
	UDmSimSubsystem* Sim = nullptr;
	ADmGameMode* Mode = nullptr;
	APlayerController* PC = nullptr;
	int32 Frame = 0;             // frames since the scenario started
	double Time = 0;             // seconds since the scenario started

	const FDmShotArgs& Args() const { return FDmShotArgs::Get(); }

	/** Spawn the scene parts of these kinds (default: the visual world). */
	TArray<AActor*> BuildScene(uint32 KindMask = DmPartsWorld);
	/** Camera looking at the surface point (lat, lon) from distKm away (see -cam). */
	void LookAt(double LatDeg, double LonDeg, double DistKm, double HeadingDeg = 0, double PitchDeg = 90, double FovDeg = 30);
	/** Camera at a world position / rotation. */
	void SetView(const FVector& WorldLoc, const FRotator& Rot, double FovDeg = 30);
	/** Let a pawn's camera (C3) drive the view instead of the harness camera. */
	void UseHarnessCamera(bool bUse);
	/** Pin the sun over this planet point (degrees). */
	void SetSubsolar(double LatDeg, double LonDeg);
	/** Screenshot this frame: <shot base>-<Suffix>.png (or shots/ue-<scenario>-<Suffix>.png). */
	void Shot(const FString& Suffix);
	/** Exit once pending screenshots are written. */
	void Finish();
	/** Do not exit when the shots are written; wait for Finish() (tests that run longer). */
	void HoldExit();
	/** Process exit code when the game quits (tests: non-zero = failed; shot.ps1 -noshot reports it). */
	void SetExitCode(int32 Code);
	void Log(const FString& Msg) const;
};

using FDmScenarioSetup = TFunction<void(FDmScenarioContext&)>;
using FDmScenarioTick = TFunction<void(FDmScenarioContext&, float)>;

struct FDmScenario
{
	FString Name;
	FString Area;
	FString Help;
	FDmScenarioSetup Setup;
	FDmScenarioTick Tick;
};

class DEMIURGE_API FDmScenarios
{
public:
	static void Register(const FDmScenario& S);
	/** Exact name, else the longest registered name that is a '-'-prefix of it. */
	static const FDmScenario* Find(const FString& Name);
	static TArray<FString> Names();
};

struct DEMIURGE_API FDmScenarioRegistrar
{
	FDmScenarioRegistrar(const TCHAR* Name, const TCHAR* Area, const TCHAR* Help, FDmScenarioSetup Setup, FDmScenarioTick Tick = nullptr);
};
