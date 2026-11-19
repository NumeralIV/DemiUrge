// DEMIURGE — Game/Audio/DmAudioDirector.h : the audio part of every scene (SPEC-UNREAL 2 C9). Owner: C9 audio.
//
// ADmAudioDirector (scene part "audio", kind Audio) owns the FDmAudioEngine and plays it through
// UDmSynthWave (a procedural float sound wave on a UI audio component: the mix is already stereo).
// Each frame it senses where the camera is (altitude, focus cell, what land / sea / forest / towns /
// fire / storms lie around the focus, day or night there, the world's age, phase, wars) and hands that
// to the engine; it turns every sim and UI event into its cue (placed by distance, pan and occlusion
// from the camera) and stingers. With -nosound there is no audio device: the engine is idle unless a
// scenario starts a capture, which renders the mix on the game thread into a buffer (tests).
//
// Deviation (reported): SPEC-UNREAL asks for USynthComponent subclasses; that class lives in the
// AudioMixer module, which Demiurge.Build.cs does not list (and C9 does not own the Build.cs). The
// engine-module USoundWaveProcedural does the same job (a render callback on the audio thread). Moving
// to USynthComponent later is a one-class change: its OnGenerateAudio calls FDmAudioEngine::Render.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundWaveProcedural.h"
#include "Game/Audio/DmAudioEngine.h"
#include <vector>
#include "DmAudioDirector.generated.h"

class UAudioComponent;
namespace dm { struct Event; struct World; }

UCLASS()
class DEMIURGE_API UDmSynthWave : public USoundWaveProcedural
{
	GENERATED_BODY()
public:
	UDmSynthWave(const FObjectInitializer& ObjectInitializer);
	void Setup(TSharedPtr<FDmAudioEngine, ESPMode::ThreadSafe> InEngine, int32 InSampleRate);
	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;
	virtual Audio::EAudioMixerStreamDataFormat::Type GetGeneratedPCMDataFormat() const override;
	// The deprecated proxy factory of IAudioProxyDataFactory lives in the AudioExtensions module, which
	// Demiurge.Build.cs does not link; overriding it keeps this class's vtable inside Engine + this module.
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	virtual TUniquePtr<Audio::IProxyData> CreateNewProxyData(const Audio::FProxyDataInitParams& InitParams) override { (void)InitParams; return nullptr; }
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
private:
	TSharedPtr<FDmAudioEngine, ESPMode::ThreadSafe> Engine;
};

UCLASS()
class DEMIURGE_API ADmAudioDirector : public AActor
{
	GENERATED_BODY()
public:
	ADmAudioDirector();

	/** The director of this world (nullptr before the audio part spawned). */
	static ADmAudioDirector* Find(const UObject* WorldContext);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** A cue placed at a cell (-1: unplaced, e.g. UI). Throttled per cue. */
	bool PlayCue(EDmCue Cue, int32 Cell = -1, float Vol = 1.f, float Param = 0.f);
	bool PlayStinger(EDmStinger S);
	void SetVolumes(const FDmAudioVolumes& V);
	void Suspend(bool bSuspend);

	bool HasDevice() const { return bDevice; }
	/** Cues started since BeginPlay (rate-limited repeats not counted). */
	int32 CuesPlayed = 0;
	FDmAudioEngine* GetEngine() const { return Engine.Get(); }
	const FDmAudioContext& GetContext() const { return Ctx; }
	int64 GetCuesRequested() const { return CuesRequested; }
	int64 GetEventsHeard() const { return EventsHeard; }
	/** The last cue names the director played (newest last; diagnostics). */
	const TArray<FString>& GetRecent() const { return Recent; }
	int32 GetFocusCell() const { return FocusCell; }
	double GetAltitudeKm() const { return AltKm; }
	/** Game-thread cost of sensing (verifier instrumentation): mean milliseconds per Sense() call and the call count. */
	double GetSenseMsAvg() const { return SenseCalls ? 1000.0 * SenseSec / double(SenseCalls) : 0.0; }
	int64 GetSenseCalls() const { return SenseCalls; }

	/** Scenarios: render the mix on the game thread (only when there is no audio device). */
	void StartCapture(double MaxSeconds);
	void StopCapture();
	bool IsCapturing() const { return bCapture; }
	const TArray<float>& CaptureL() const { return CapL; }
	const TArray<float>& CaptureR() const { return CapR; }
	float GetSampleRate() const { return Sr; }
	/** Sense the world now (normally at 10 Hz in Tick). */
	void SenseNow() { Sense(); }

private:
	TSharedPtr<FDmAudioEngine, ESPMode::ThreadSafe> Engine;
	UPROPERTY() TObjectPtr<UDmSynthWave> Wave;
	UPROPERTY() TObjectPtr<UAudioComponent> Comp;
	bool bDevice = false;
	float Sr = 48000.f;
	uint32 EngineSeed = 0;

	FDelegateHandle EventHandle, WorldHandle;
	FDmAudioContext Ctx;
	float SenseTimer = 0.f, UnlockTimer = 0.f;
	double Now = 0;
	double CueLast[int32(EDmCue::Count)] = {};
	double StingerLast[int32(EDmStinger::Count)] = {};
	float TensionHold = 0.f;
	float BrushRain = 0.f;
	TArray<bool> Unlocked;
	bool bUnlockPrimed = false;
	int32 FocusCell = -1;
	double AltKm = 0;
	int64 CuesRequested = 0, EventsHeard = 0;
	double SenseSec = 0;
	int64 SenseCalls = 0;
	TArray<FString> Recent;
	std::vector<int32_t> Within;

	bool bCapture = false;
	double CaptureMax = 0, CaptureDebt = 0;
	TArray<float> CapL, CapR;

	void OnSimEvent(const dm::Event& E);
	void OnWorldChanged(const dm::World* W);
	void Sense();
	void CheckUnlocks();
	bool Place(int32 Cell, bool bPlanetary, FDmCueParams& P) const;
	bool CamView(FVector& Loc, FRotator& Rot) const;
	void Note(const TCHAR* What);
};
