// DEMIURGE — Game/Core/DmScenario.cpp : see DmScenario.h. Owner: C0 bridge.

#include "Game/Core/DmScenario.h"
#include "Game/Core/DmGameMode.h"
#include "Game/Core/DmSimSubsystem.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmScenario, Log, All);

// ------------------------------------------------------------------ args

bool FDmShotArgs::Value(const TCHAR* Key, FString& Out)
{
	return FParse::Value(FCommandLine::Get(), Key, Out, false);
}

bool FDmShotArgs::Value(const TCHAR* Key, double& Out)
{
	FString S;
	if (!Value(Key, S) || S.IsEmpty()) return false;
	Out = FCString::Atod(*S);
	return true;
}

bool FDmShotArgs::Value(const TCHAR* Key, int32& Out)
{
	FString S;
	if (!Value(Key, S) || S.IsEmpty()) return false;
	Out = FCString::Atoi(*S);
	return true;
}

bool FDmShotArgs::Param(const TCHAR* Name)
{
	return FParse::Param(FCommandLine::Get(), Name);
}

int32 FDmShotArgs::List(const TCHAR* Key, double* Out, int32 Max)
{
	FString S;
	if (!Value(Key, S)) return 0;
	TArray<FString> Parts;
	S.ParseIntoArray(Parts, TEXT(","), false);
	int32 N = 0;
	for (int32 I = 0; I < Parts.Num() && I < Max; ++I)
	{
		const FString P = Parts[I].TrimStartAndEnd();
		if (P.IsEmpty()) continue;
		Out[I] = FCString::Atod(*P);
		N = I + 1;
	}
	return N;
}

const FDmShotArgs& FDmShotArgs::Get()
{
	static FDmShotArgs A;
	static bool bParsed = false;
	if (bParsed) return A;
	bParsed = true;
	Value(TEXT("-scenario="), A.Scenario);
	if (Value(TEXT("-shot="), A.Shot) && !A.Shot.IsEmpty()) A.Shot = FPaths::ConvertRelativePathToFull(A.Shot);
	Value(TEXT("-warm="), A.Warm);
	A.Warm = FMath::Max(A.Warm, 2);
	{
		double Seq[32];
		const int32 N = List(TEXT("-seq="), Seq, 32);
		for (int32 I = 0; I < N; ++I) if (Seq[I] >= 1) A.Seq.AddUnique(int32(Seq[I]));
		A.Seq.Sort();
	}
	A.bCam = List(TEXT("-cam="), A.Cam, 6) >= 3;
	A.bSun = List(TEXT("-sun="), A.Sun, 2) >= 2;
	A.bYear = Value(TEXT("-year="), A.Year);
	A.bFixtureArg = Value(TEXT("-fixture="), A.Fixture);
	{
		int32 S = 0;
		if (Value(TEXT("-seed="), S)) { A.bSeed = true; A.Seed = uint32(S); }
	}
	A.bSkip = Param(TEXT("skip"));
	Value(TEXT("-fixtureage="), A.FixtureAge);
	Value(TEXT("-fixturetowns="), A.FixtureTowns);
	Value(TEXT("-fixtureseed="), A.FixtureSeed);
	A.bLive = Param(TEXT("live"));
	{
		double Sp = 0;
		if (Value(TEXT("-speed="), Sp)) A.Speed = float(Sp);
	}
	Value(TEXT("-ev="), A.Ev);
	Value(TEXT("-res="), A.Res);
	Value(TEXT("-exitafter="), A.ExitAfter);
	return A;
}

// ------------------------------------------------------------------ registry

namespace
{
	TArray<FDmScenario>& Registry()
	{
		static TArray<FDmScenario> List;
		return List;
	}
}

void FDmScenarios::Register(const FDmScenario& S)
{
	for (FDmScenario& E : Registry())
		if (E.Name == S.Name)
		{
			UE_LOG(LogDmScenario, Warning, TEXT("DmScenario: \"%s\" registered twice (areas %s, %s); the later wins"), *S.Name, *E.Area, *S.Area);
			E = S;
			return;
		}
	Registry().Add(S);
}

const FDmScenario* FDmScenarios::Find(const FString& Name)
{
	for (const FDmScenario& E : Registry()) if (E.Name.Equals(Name, ESearchCase::IgnoreCase)) return &E;
	// fall back to the longest registered name that is a run of whole '-'-separated words of it,
	// preferring a prefix ("orbit-night" -> "orbit", "planet-coast-2" -> "planet-coast",
	// "bridge-orbit-night" -> "orbit")
	const FString N = TEXT("-") + Name.ToLower() + TEXT("-");
	const FDmScenario* Best = nullptr;
	int32 BestScore = -1;
	for (const FDmScenario& E : Registry())
	{
		const FString Tok = TEXT("-") + E.Name.ToLower() + TEXT("-");
		const int32 At = N.Find(Tok);
		if (At == INDEX_NONE) continue;
		const int32 Score = E.Name.Len() * 2 + (At == 0 ? 1 : 0);
		if (Score > BestScore) { BestScore = Score; Best = &E; }
	}
	return Best;
}

TArray<FString> FDmScenarios::Names()
{
	TArray<FString> Out;
	for (const FDmScenario& E : Registry()) Out.Add(E.Name);
	Out.Sort();
	return Out;
}

FDmScenarioRegistrar::FDmScenarioRegistrar(const TCHAR* Name, const TCHAR* Area, const TCHAR* Help, FDmScenarioSetup Setup, FDmScenarioTick Tick)
{
	FDmScenario S;
	S.Name = Name;
	S.Area = Area;
	S.Help = Help;
	S.Setup = MoveTemp(Setup);
	S.Tick = MoveTemp(Tick);
	FDmScenarios::Register(S);
}

// ------------------------------------------------------------------ context

TArray<AActor*> FDmScenarioContext::BuildScene(uint32 KindMask)
{
	FDmSceneContext S;
	S.World = World;
	S.Sim = Sim;
	S.PC = PC;
	TArray<AActor*> A = FDmScene::Build(S, KindMask);
	if (Mode) Mode->AddSceneActors(A);
	return A;
}

void FDmScenarioContext::LookAt(double LatDeg, double LonDeg, double DistKm, double HeadingDeg, double PitchDeg, double FovDeg)
{
	if (Mode) Mode->HarnessLookAt(LatDeg, LonDeg, DistKm, HeadingDeg, PitchDeg, FovDeg);
}

void FDmScenarioContext::SetView(const FVector& WorldLoc, const FRotator& Rot, double FovDeg)
{
	if (Mode) Mode->HarnessSetView(WorldLoc, Rot, FovDeg);
}

void FDmScenarioContext::UseHarnessCamera(bool bUse)
{
	if (Mode) Mode->HarnessUseCamera(bUse);
}

void FDmScenarioContext::SetSubsolar(double LatDeg, double LonDeg)
{
	if (Sim) Sim->PinSunAt(LatDeg, LonDeg);
}

void FDmScenarioContext::ShotPath(const FString& Path, bool bWithUI)
{
	if (Mode) Mode->HarnessShotPath(Path, bWithUI);
}

void FDmScenarioContext::Shot(const FString& Suffix)
{
	if (Mode) Mode->HarnessShot(Suffix);
}

void FDmScenarioContext::Finish()
{
	if (Mode) Mode->HarnessFinish();
}

void FDmScenarioContext::SetExitCode(int32 Code)
{
	if (Mode) Mode->HarnessSetExitCode(Code);
}

void FDmScenarioContext::HoldExit()
{
	if (Mode) Mode->HarnessHoldExit();
}

void FDmScenarioContext::Log(const FString& Msg) const
{
	UE_LOG(LogDmScenario, Display, TEXT("DmScenario: %s"), *Msg);
}
