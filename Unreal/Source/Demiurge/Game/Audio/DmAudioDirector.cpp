// DEMIURGE — Game/Audio/DmAudioDirector.cpp : camera + world -> ambience context, events -> cues. Owner: C9 audio.
#include "Game/Audio/DmAudioDirector.h"
#include "Game/Audio/DmAudio.h"
#include "Game/Audio/DmAudioDsp.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Game/Core/DmScene.h"
#include "Game/Core/DmCoords.h"
#include "Sim/Powers.h"
#include "Sim/Save.h"
#include "Components/AudioComponent.h"
#include "AudioDevice.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmAudio, Log, All);

// ------------------------------------------------------------------------------------------------ the scene part

static FDmScenePartRegistrar GDmAudioPart(TEXT("audio"), EDmPartKind::Audio, 0,
	[](const FDmSceneContext& S) -> AActor* { return S.World ? S.World->SpawnActor<ADmAudioDirector>() : nullptr; },
	TEXT("Game/Audio/DmAudioDirector.cpp"));

// ------------------------------------------------------------------------------------------------ wave

UDmSynthWave::UDmSynthWave(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
	NumChannels = 2;
	NumSamplesToGeneratePerCallback = 1024;
	NumBufferUnderrunSamples = 512;
	bCanProcessAsync = true;
	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = true;
	SampleByteSize = 4;
}

void UDmSynthWave::Setup(TSharedPtr<FDmAudioEngine, ESPMode::ThreadSafe> InEngine, int32 InSampleRate)
{
	Engine = InEngine;
	SetSampleRate(uint32(FMath::Max(8000, InSampleRate)));
}

Audio::EAudioMixerStreamDataFormat::Type UDmSynthWave::GetGeneratedPCMDataFormat() const
{
	return Audio::EAudioMixerStreamDataFormat::Float;
}

int32 UDmSynthWave::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
	const int32 Frames = NumSamples / 2;
	if (Frames <= 0) return 0;
	OutAudio.SetNumUninitialized(Frames * 2 * int32(sizeof(float)), EAllowShrinking::No);
	float* Dst = reinterpret_cast<float*>(OutAudio.GetData());
	if (Engine.IsValid()) Engine->Render(Dst, Frames);
	else FMemory::Memzero(Dst, Frames * 2 * sizeof(float));
	return Frames * 2;
}

// ------------------------------------------------------------------------------------------------ public API

bool FDmAudio::Play(const UObject* WorldContext, const TCHAR* Name, int32 Cell, float Vol)
{
	ADmAudioDirector* D = ADmAudioDirector::Find(WorldContext);
	EDmCue C = EDmCue::Click;
	if (!D || !Name || !DmAudio::CueFromName(FString(Name), C)) return false;
	return D->PlayCue(C, Cell, Vol);
}

bool FDmAudio::Stinger(const UObject* WorldContext, const TCHAR* Name)
{
	ADmAudioDirector* D = ADmAudioDirector::Find(WorldContext);
	EDmStinger S = EDmStinger::Age;
	if (!D || !Name || !DmAudio::StingerFromName(FString(Name), S)) return false;
	return D->PlayStinger(S);
}

void FDmAudio::ApplySettings(const UObject* WorldContext, const dm::save::UserSettings& S)
{
	FDmAudio::SetVolumes(WorldContext, float(S.master), float(S.music), float(S.sfx), float(S.ambience));
}

void FDmAudio::SetVolumes(const UObject* WorldContext, float Master, float Music, float Sfx, float Ambience)
{
	if (ADmAudioDirector* D = ADmAudioDirector::Find(WorldContext))
	{
		FDmAudioVolumes V;
		V.Master = FMath::Clamp(Master, 0.f, 1.f); V.Music = FMath::Clamp(Music, 0.f, 1.f);
		V.Sfx = FMath::Clamp(Sfx, 0.f, 1.f); V.Ambience = FMath::Clamp(Ambience, 0.f, 1.f); V.Ui = V.Sfx;
		D->SetVolumes(V);
	}
}

void FDmAudio::Suspend(const UObject* WorldContext, bool bSuspend)
{
	if (ADmAudioDirector* D = ADmAudioDirector::Find(WorldContext)) D->Suspend(bSuspend);
}

// ------------------------------------------------------------------------------------------------ director

ADmAudioDirector::ADmAudioDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;   // after the camera moved this frame
	SetActorTickEnabled(true);
}

ADmAudioDirector* ADmAudioDirector::Find(const UObject* WorldContext)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) return nullptr;
	for (TActorIterator<ADmAudioDirector> It(W); It; ++It)
		if (IsValid(*It)) return *It;
	return nullptr;
}

void ADmAudioDirector::BeginPlay()
{
	Super::BeginPlay();
	UWorld* W = GetWorld();
	FAudioDeviceHandle Dev = W ? W->GetAudioDevice() : FAudioDeviceHandle();
	bDevice = Dev.IsValid() && !FParse::Param(FCommandLine::Get(), TEXT("nosound"));
	Sr = bDevice ? FMath::Max(8000.f, Dev->GetSampleRate()) : 48000.f;

	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	EngineSeed = (Sim && Sim->Sim()) ? Sim->Sim()->seed : 1u;
	Engine = MakeShared<FDmAudioEngine, ESPMode::ThreadSafe>(Sr, EngineSeed);
	{
		const dm::save::UserSettings Def = dm::save::defaultSettings();
		FDmAudioVolumes V;
		V.Master = float(Def.master); V.Music = float(Def.music); V.Sfx = float(Def.sfx); V.Ambience = float(Def.ambience); V.Ui = V.Sfx;
		// test hook (verifier): -dmaudiomaster=0 starts a real-device run silent from its first block
		float TestMaster = -1.f;
		if (FParse::Value(FCommandLine::Get(), TEXT("-dmaudiomaster="), TestMaster)) V.Master = FMath::Clamp(TestMaster, 0.f, 1.f);
		Engine->SetVolumes(V);
	}
	Unlocked.Init(false, dm::powers::kPowerCount);
	Sense();

	if (Sim)
	{
		EventHandle = Sim->OnSimEvent.AddUObject(this, &ADmAudioDirector::OnSimEvent);
		WorldHandle = Sim->OnWorldChanged.AddUObject(this, &ADmAudioDirector::OnWorldChanged);
	}

	if (bDevice)
	{
		Wave = NewObject<UDmSynthWave>(this);
		Wave->Setup(Engine, int32(Sr));
		Comp = NewObject<UAudioComponent>(this);
		Comp->bAutoActivate = false;
		Comp->bIsUISound = true;
		Comp->bAllowSpatialization = false;
		Comp->SetSound(Wave);
		Comp->RegisterComponent();
		Comp->Play();
	}
	UE_LOG(LogDmAudio, Display, TEXT("DmAudio: director up (%s, %.0f Hz, seed %u)"), bDevice ? TEXT("audio device") : TEXT("no audio device: idle unless captured"), Sr, EngineSeed);
}

void ADmAudioDirector::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
	{
		Sim->OnSimEvent.Remove(EventHandle);
		Sim->OnWorldChanged.Remove(WorldHandle);
	}
	if (Comp) Comp->Stop();
	Super::EndPlay(Reason);
}

void ADmAudioDirector::OnWorldChanged(const dm::World* W)
{
	bUnlockPrimed = false;
	for (bool& B : Unlocked) B = false;
	TensionHold = 0.f;
	if (W) EngineSeed = W->seed;
	Sense();
}

bool ADmAudioDirector::CamView(FVector& Loc, FRotator& Rot) const
{
	const UWorld* W = GetWorld();
	const APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->PlayerCameraManager) return false;
	Loc = PC->PlayerCameraManager->GetCameraLocation();
	Rot = PC->PlayerCameraManager->GetCameraRotation();
	return true;
}

void ADmAudioDirector::Sense()
{
	if (!Engine.IsValid()) return;
	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const dm::World* W = Sim ? Sim->Sim() : nullptr;
	FDmAudioContext C;
	C.Seed = EngineSeed;
	C.bPaused = Sim ? Sim->IsPaused() : false;
	FVector CamLoc = FVector::ZeroVector;
	FRotator CamRot = FRotator::ZeroRotator;
	const bool bCam = CamView(CamLoc, CamRot);
	if (!W || !W->geo || W->n <= 0)
	{
		C.Altitude01 = 1.f; C.Phase = 4;
		Ctx = C;
		Engine->SetContext(C);
		return;
	}
	const dm::Geo& G = *W->geo;
	C.Phase = int32(W->phase);
	C.Age = W->age;
	C.Seed = W->seed;

	const FTransform P2W = Sim->GetPlanetToWorld();
	const FVector Local = bCam ? P2W.InverseTransformPosition(CamLoc) : FVector(DmCoords::RadiusUU * 8.0, 0, 0);
	const double Dist = FMath::Max(1.0, Local.Size());
	AltKm = FMath::Max(0.05, (Dist - DmCoords::RadiusUU) / DmCoords::UUPerKm);
	C.Altitude01 = float(FMath::Clamp(FMath::Loge(AltKm / 0.5) / FMath::Loge(900.0 / 0.5), 0.0, 1.0));

	// the listening point: what the camera looks at, unless that is far ahead of it (low, tilted views)
	int32 Focus = -1;
	FDmPick Pick;
	if (bCam && Sim->PickRay(CamLoc, CamRot.Vector(), Pick) && Pick.Cell >= 0 && Pick.Distance < 4.0 * AltKm * DmCoords::UUPerKm + 2.0e5)
		Focus = Pick.Cell;
	if (Focus < 0)
	{
		const dm::Vec3 D = DmCoords::UEToSim(Local / Dist);
		Focus = G.cellAt(D.x, D.y, D.z);
	}
	if (Focus < 0 || Focus >= W->n) { Ctx = C; Engine->SetContext(C); return; }
	FocusCell = Focus;

	const double Ang = FMath::Clamp(AltKm / DmCoords::RadiusKm * 0.5, 0.03, 0.5);
	// cultures at war (the darker layer grows when the camera is over their lands)
	int32 WarCult[16];
	int32 NumWar = 0;
	for (const auto& Rel : W->vars.rel)
		if (Rel.second.war)
			for (const int32 Id : { Rel.first.first, Rel.first.second })
			{
				bool bHave = false;
				for (int32 K = 0; K < NumWar; ++K) bHave |= WarCult[K] == Id;
				if (!bHave && NumWar < 16) WarCult[NumWar++] = Id;
			}
	double WarLand = 0;
	const int32 Count = G.within(Focus, Ang, Within);
	double Sea = 0, Coast = 0, Forest = 0, Open = 0, Desert = 0, Cold = 0, Mount = 0, Urban = 0, Fire = 0, Lava = 0, Wet = 0, Tot = 0;
	for (int32 I = 0; I < Count; ++I)
	{
		const int32 Cl = Within[size_t(I)];
		const double Wt = 1.0 - 0.6 * G.angle(Focus, Cl) / Ang;   // nearer cells weigh more
		Tot += Wt;
		const bool bSea = dm::state::isSea(*W, Cl);
		const uint8 B = W->biome[size_t(Cl)];
		if (bSea && B != dm::Biome::SEAICE) Sea += Wt;
		if (W->coast[size_t(Cl)] <= 1 || (bSea && B == dm::Biome::SHALLOWS)) Coast += Wt;
		const double Veg = FMath::Clamp(double(W->veg[size_t(Cl)]) * 1.4, 0.0, 1.0);
		switch (B)
		{
		case dm::Biome::FOREST: case dm::Biome::RAINFOREST: case dm::Biome::TAIGA: Forest += Wt * (0.4 + 0.6 * Veg); break;
		case dm::Biome::GRASSLAND: case dm::Biome::SAVANNA: case dm::Biome::WETLAND: Open += Wt; break;
		case dm::Biome::DESERT: case dm::Biome::BARREN: case dm::Biome::BEACH: Desert += Wt; break;
		case dm::Biome::ICE: case dm::Biome::TUNDRA: case dm::Biome::SEAICE: Cold += Wt; break;
		case dm::Biome::SNOWPEAK: Cold += Wt; Mount += Wt; break;
		case dm::Biome::ROCK: Mount += Wt; break;
		default: break;
		}
		if (!bSea && W->slope[size_t(Cl)] > 0.35f) Mount += 0.5 * Wt;
		const int32 Own = W->owner[size_t(Cl)];
		Urban += Wt * FMath::Max(double(W->urban[size_t(Cl)]), Own >= 0 ? 0.12 : 0.0);
		if (NumWar > 0 && Own >= 0 && Own < int32(W->settlements.size()))
		{
			const int32 Cu = W->settlements[size_t(Own)].culture;
			for (int32 K = 0; K < NumWar; ++K) if (WarCult[K] == Cu) { WarLand += Wt; break; }
		}
		Fire += Wt * W->fire[size_t(Cl)];
		Lava += Wt * (W->lava[size_t(Cl)] > 0.02f ? 1.0 : 0.0);
		Wet += Wt * W->wet[size_t(Cl)];
	}
	const double Inv = Tot > 0 ? 1.0 / Tot : 0.0;
	C.Sea = float(Sea * Inv); C.Coast = float(FMath::Min(1.0, Coast * Inv * 2.5)); C.Forest = float(Forest * Inv); C.Open = float(Open * Inv);
	C.Desert = float(Desert * Inv); C.Cold = float(Cold * Inv); C.Mountain = float(FMath::Min(1.0, Mount * Inv));
	C.Town = float(FMath::Min(1.0, Urban * Inv * 3.0));
	C.Fire = float(FMath::Min(1.0, Fire * Inv * 8.0));
	C.Lava = float(FMath::Min(1.0, Lava * Inv * 3.0));

	// the nearest live settlement: its culture's age sets the sound of the town
	{
		double Best = Ang * 1.5;
		int32 BestS = -1;
		for (const dm::Settlement& S : W->settlements)
		{
			if (S.dead || S.cell < 0 || S.cell >= W->n) continue;
			const double A = G.angle(Focus, S.cell);
			if (A < Best) { Best = A; BestS = S.id; }
		}
		if (BestS >= 0)
		{
			const dm::Settlement& S = W->settlements[size_t(BestS)];
			const dm::Culture* Cu = dm::state::culture(*W, S.culture);
			C.TownAge = Cu ? Cu->age : W->age;
			C.Town = FMath::Max(C.Town, float(FMath::Clamp(S.pop / 4000.0, 0.15, 1.0) * (1.0 - Best / (Ang * 1.5))));
		}
	}
	// storms and disasters nearby
	float Storm = 0.f, Disaster = 0.f;
	for (const dm::Disaster& D : W->disasters)
	{
		if (D.cell < 0 || D.cell >= W->n) continue;
		const double A = G.angle(Focus, D.cell);
		const double Reach = Ang + FMath::Max(0.05, D.ang);
		if (A > Reach) continue;
		const float Near = float(1.0 - A / Reach);
		if (D.type == dm::DisasterType::Storm) Storm = FMath::Max(Storm, 0.4f + 0.6f * Near);
		else Disaster = FMath::Max(Disaster, 0.3f + 0.4f * Near);
	}
	C.Storm = Storm;
	C.Rain = FMath::Max3(Storm * 0.9f, BrushRain, float(FMath::Clamp(Wet * Inv - 0.55, 0.0, 0.4)));

	// day or night at the focus
	{
		const FVector Up = Sim->CellWorldUp(Focus);
		const double Sun = FVector::DotProduct(Up, Sim->GetSunDirection());
		C.Day = DmDsp::SStep(-0.12f, 0.2f, float(Sun));
	}
	// war and disaster darken the score
	// a war anywhere: a faint darker layer; over the warring lands: the darker mode
	const float WarT = NumWar > 0 ? 0.3f + 0.5f * float(FMath::Min(1.0, WarLand * Inv * 3.0)) : 0.f;
	C.Tension = FMath::Clamp(FMath::Max3(WarT, Disaster, TensionHold), 0.f, 1.f);
	Ctx = C;
	Engine->SetContext(C);
}

bool ADmAudioDirector::Place(int32 Cell, bool bPlanetary, FDmCueParams& P) const
{
	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const dm::World* W = Sim ? Sim->Sim() : nullptr;
	FVector CamLoc, CamDir;
	FRotator CamRot;
	if (!W || Cell < 0 || Cell >= W->n || !CamView(CamLoc, CamRot)) return false;
	const FVector Pos = Sim->CellWorldPos(Cell);
	const FVector Up = Sim->CellWorldUp(Cell);
	const FVector Rel = Pos - CamLoc;
	const double DistKm = Rel.Size() / DmCoords::UUPerKm;
	const double RefKm = bPlanetary ? 900.0 : FMath::Max(30.0, AltKm * 1.6);
	const double G = 1.0 / FMath::Sqrt(1.0 + FMath::Square(DistKm / RefKm));
	float Muffle = float(FMath::Clamp(FMath::LogX(10.0, FMath::Max(DistKm / RefKm, 1e-3)) * 0.5 + 0.15, 0.0, 0.8));
	float Vol = float(G);
	// behind the planet's limb: softer and darker
	const FVector ToCam = (-Rel).GetSafeNormal();
	if (FVector::DotProduct(Up, ToCam) < -0.05) { Vol *= 0.45f; Muffle = FMath::Min(1.f, Muffle + 0.45f); }
	const FVector Right = FRotationMatrix(CamRot).GetUnitAxis(EAxis::Y);
	P.Pan = float(FMath::Clamp(FVector::DotProduct(Rel.GetSafeNormal(), Right) * 0.85, -0.85, 0.85));
	P.Vol *= Vol;
	P.Muffle = FMath::Max(P.Muffle, Muffle);
	(void)CamDir;
	return true;
}

void ADmAudioDirector::Note(const TCHAR* What)
{
	Recent.Add(What);
	if (Recent.Num() > 32) Recent.RemoveAt(0);
}

bool ADmAudioDirector::PlayCue(EDmCue Cue, int32 Cell, float Vol, float Param)
{
	if (!Engine.IsValid() || int32(Cue) < 0 || Cue >= EDmCue::Count) return false;
	static const float MinGap[int32(EDmCue::Count)] = {
		/* hover */ 0.05f, /* click */ 0.03f, /* arm */ 0.08f, /* deny */ 0.25f, /* unlock */ 0.5f, /* thunder */ 0.12f, /* impact */ 0.1f,
		/* rumble */ 0.35f, /* wave */ 1.2f, /* rainfall */ 0.45f, /* shimmer */ 0.6f, /* chime */ 0.4f, /* choir */ 1.f, /* horn */ 2.f,
		/* toll */ 2.f, /* roar */ 2.f, /* crackle */ 1.2f, /* fall */ 0.05f,
	};
	const int32 K = int32(Cue);
	if (Now - CueLast[K] < MinGap[K] && CueLast[K] > 0) return false;
	CueLast[K] = Now;
	++CuesPlayed;   // (integration) checks count the cues a power or an event produced
	FDmCueParams P;
	P.Vol = FMath::Clamp(Vol, 0.f, 2.f);
	P.Param = Param;
	const bool bPlanetary = Cue == EDmCue::Impact || Cue == EDmCue::Thunder || Cue == EDmCue::Fall || Cue == EDmCue::Roar || Cue == EDmCue::Wave
		|| (Cue == EDmCue::Rumble && Param >= 0.35f);
	if (Cell >= 0)
	{
		if (!Place(Cell, bPlanetary, P)) P.Vol *= 0.8f;
		if (Cue == EDmCue::Thunder)
		{
			// thunder follows the flash: a little later the farther it is
			const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
			FVector L; FRotator R;
			if (Sim && CamView(L, R)) P.Delay = float(FMath::Clamp((Sim->CellWorldPos(Cell) - L).Size() / DmCoords::UUPerKm / 150.0, 0.0, 1.2));
		}
	}
	if (P.Vol < 0.01f) return false;
	Engine->Play(Cue, P);
	++CuesRequested;
	Note(DmAudio::CueName(Cue));
	return true;
}

bool ADmAudioDirector::PlayStinger(EDmStinger S)
{
	if (!Engine.IsValid()) return false;
	const int32 K = int32(S);
	const double Gap = S == EDmStinger::Founding ? 20.0 : 8.0;
	if (StingerLast[K] > 0 && Now - StingerLast[K] < Gap) return false;
	// one stinger at a time: anything within 4 s of another is dropped (endings always play)
	const bool bEnding = S >= EDmStinger::EndingBeloved;
	if (!bEnding)
		for (int32 I = 0; I < int32(EDmStinger::Count); ++I)
			if (StingerLast[I] > 0 && Now - StingerLast[I] < 4.0) return false;
	StingerLast[K] = Now;
	Engine->Stinger(S);
	Note(DmAudio::StingerName(S));
	return true;
}

void ADmAudioDirector::SetVolumes(const FDmAudioVolumes& V)
{
	if (Engine.IsValid()) Engine->SetVolumes(V);
}

void ADmAudioDirector::Suspend(bool bSuspend)
{
	if (Engine.IsValid()) Engine->Suspend(bSuspend);
}

void ADmAudioDirector::CheckUnlocks()
{
	UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this);
	const dm::World* W = Sim ? Sim->Sim() : nullptr;
	if (!W) return;
	bool bNew = false;
	for (int32 I = 0; I < dm::powers::kPowerCount && I < Unlocked.Num(); ++I)
	{
		const bool bNow = dm::powers::unlocked(*W, dm::powers::kPowers[I].id);
		if (bNow && !Unlocked[I] && bUnlockPrimed) bNew = true;
		Unlocked[I] = bNow;
	}
	bUnlockPrimed = true;
	if (bNew) PlayCue(EDmCue::Unlock);
}

void ADmAudioDirector::OnSimEvent(const dm::Event& E)
{
	++EventsHeard;
	using namespace dm;
	if (const EvLightning* X = E.as<EvLightning>()) { PlayCue(EDmCue::Thunder, X->cell, 0.9f); return; }
	if (const EvMeteorLaunched* X = E.as<EvMeteorLaunched>())
	{
		PlayCue(EDmCue::Fall, X->cell, X->kind == ImpactKind::Comet ? 0.8f : 1.f, float(FMath::Clamp(X->eta, 0.6, 8.0)));
		return;
	}
	if (const EvMeteorImpact* X = E.as<EvMeteorImpact>())
	{
		const bool bComet = X->kind == ImpactKind::Comet;
		PlayCue(EDmCue::Impact, X->cell, bComet ? 0.7f : 1.f, bComet ? 0.5f : 1.f);
		if (X->sea) PlayCue(EDmCue::Wave, X->cell, 0.7f);
		if (!bComet) TensionHold = FMath::Max(TensionHold, 0.6f);
		return;
	}
	if (const EvTsunamiStart* X = E.as<EvTsunamiStart>()) { PlayCue(EDmCue::Wave, X->cell, 1.f); return; }
	if (const EvTsunamiHit* X = E.as<EvTsunamiHit>()) { PlayCue(EDmCue::Wave, X->cell, float(FMath::Clamp(0.4 + X->height, 0.4, 1.0))); return; }
	if (const EvVolcanoStart* X = E.as<EvVolcanoStart>())
	{
		PlayCue(EDmCue::Rumble, X->cell, 1.f, 1.f);
		PlayCue(EDmCue::Roar, X->cell, 0.6f);
		TensionHold = FMath::Max(TensionHold, 0.6f);
		return;
	}
	if (const EvQuake* X = E.as<EvQuake>())
	{
		PlayCue(EDmCue::Rumble, X->cell, 1.f, float(FMath::Clamp(0.4 + X->power * 0.6, 0.4, 1.2)));
		TensionHold = FMath::Max(TensionHold, 0.45f);
		return;
	}
	if (const EvFireStart* X = E.as<EvFireStart>()) { PlayCue(EDmCue::Crackle, X->cell, 0.8f); return; }
	if (const EvStorm* X = E.as<EvStorm>()) { if (X->start) PlayCue(EDmCue::Thunder, X->cell, 0.6f); return; }
	if (const EvPowerUsed* X = E.as<EvPowerUsed>())
	{
		const std::string& Id = X->id;
		if (Id == "rain") { PlayCue(EDmCue::Rainfall, X->cell, 0.9f); BrushRain = 0.8f; }
		else if (Id == "raise" || Id == "lower") PlayCue(EDmCue::Rumble, X->cell, 0.8f, 0.2f);
		else if (Id == "bloom" || Id == "seed") PlayCue(EDmCue::Shimmer, X->cell, 0.9f);
		else if (Id == "heal" || Id == "inspire") PlayCue(EDmCue::Chime, X->cell, Id == "heal" ? 1.f : 0.75f);
		else if (Id == "prophet" || Id == "awaken") PlayCue(EDmCue::Choir, X->cell, 1.f);
		else if (Id == "peace") { PlayCue(EDmCue::Chime, X->cell, 0.7f); PlayCue(EDmCue::Choir, X->cell, 0.6f); }
		else if (Id == "plague") { PlayCue(EDmCue::Toll, X->cell, 1.f); TensionHold = FMath::Max(TensionHold, 0.4f); }
		return;
	}
	if (const EvMiracleWitnessed* X = E.as<EvMiracleWitnessed>()) { PlayStinger(X->good >= 0 ? EDmStinger::Miracle : EDmStinger::Wrath); return; }
	if (const EvSettlementFounded* X = E.as<EvSettlementFounded>()) { (void)X; PlayStinger(EDmStinger::Founding); return; }
	if (const EvSettlementPlague* X = E.as<EvSettlementPlague>())
	{
		if (X->start)
			if (const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
				if (const Settlement* S = Sim->Sim() ? state::settlement(*Sim->Sim(), X->id) : nullptr) PlayCue(EDmCue::Toll, S->cell, 0.8f);
		return;
	}
	if (const EvSettlementDestroyed* X = E.as<EvSettlementDestroyed>()) { PlayCue(EDmCue::Toll, X->cell, 0.6f); return; }
	if (const EvWarDeclared* X = E.as<EvWarDeclared>())
	{
		int32 Cell = -1;
		if (const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
			if (const Culture* Cu = Sim->Sim() ? state::culture(*Sim->Sim(), X->a) : nullptr)
				if (const Settlement* S = state::settlement(*Sim->Sim(), Cu->capital)) Cell = S->cell;
		PlayCue(EDmCue::Horn, Cell, 0.9f);
		TensionHold = FMath::Max(TensionHold, 0.5f);
		return;
	}
	if (const EvBattle* X = E.as<EvBattle>()) { PlayCue(EDmCue::Horn, X->cell, 0.5f); return; }
	if (const EvWorldAge* X = E.as<EvWorldAge>()) { (void)X; PlayStinger(EDmStinger::Age); return; }
	if (const EvPrayerAnswered* X = E.as<EvPrayerAnswered>())
	{
		if (const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
			if (const Settlement* S = Sim->Sim() ? state::settlement(*Sim->Sim(), X->id) : nullptr) PlayCue(EDmCue::Chime, S->cell, 0.8f);
		return;
	}
	if (const EvProphetRisen* X = E.as<EvProphetRisen>())
	{
		if (const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
			if (const Settlement* S = Sim->Sim() ? state::settlement(*Sim->Sim(), X->settlement) : nullptr) PlayCue(EDmCue::Choir, S->cell, 0.8f);
		return;
	}
	if (const EvWonderBuilt* X = E.as<EvWonderBuilt>())
	{
		if (const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
			if (const Settlement* S = Sim->Sim() ? state::settlement(*Sim->Sim(), X->settlement) : nullptr) PlayCue(EDmCue::Chime, S->cell, 0.9f);
		return;
	}
	if (const EvNuke* X = E.as<EvNuke>())
	{
		PlayCue(EDmCue::Impact, X->cell, 1.f, 1.2f);
		PlayCue(EDmCue::Rumble, X->cell, 1.f, 1.f);
		TensionHold = 1.f;
		return;
	}
	if (const EvArkLaunched* X = E.as<EvArkLaunched>())
	{
		int32 Cell = -1;
		if (const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
			if (const Settlement* S = Sim->Sim() ? state::settlement(*Sim->Sim(), X->settlement) : nullptr) Cell = S->cell;
		PlayCue(EDmCue::Roar, Cell, 1.f);
		return;
	}
	if (const EvEnding* X = E.as<EvEnding>())
	{
		EDmStinger S = EDmStinger::EndingSilence;
		switch (X->mood)
		{
		case Mood::Beloved: S = EDmStinger::EndingBeloved; break;
		case Mood::Feared: S = EDmStinger::EndingFeared; break;
		case Mood::Forgotten: S = EDmStinger::EndingForgotten; break;
		case Mood::Parting: S = EDmStinger::EndingParting; break;
		case Mood::Ash: S = EDmStinger::EndingAsh; break;
		case Mood::Quiet: S = EDmStinger::EndingQuiet; break;
		default: break;
		}
		if (X->type == EndingType::Silence && X->mood != Mood::Ash) S = EDmStinger::EndingSilence;
		PlayStinger(S);
		return;
	}
	// (integration) the sim events that had no sound yet (audio report: drought; and the quieter history beats)
	if (const EvDrought* X = E.as<EvDrought>()) { if (X->start) PlayCue(EDmCue::Crackle, X->cell, 0.35f); return; }
	if (const EvWarEnded* X = E.as<EvWarEnded>()) { PlayCue(EDmCue::Chime, -1, 0.45f); return; }
	if (const EvSettlementCaptured* X = E.as<EvSettlementCaptured>())
	{
		if (const UDmSimSubsystem* Sim = UDmSimSubsystem::Get(this))
			if (const Settlement* S = Sim->Sim() ? state::settlement(*Sim->Sim(), X->id) : nullptr) PlayCue(EDmCue::Horn, S->cell, 0.55f);
		return;
	}
	if (const EvCultureBorn* X = E.as<EvCultureBorn>()) { PlayCue(EDmCue::Chime, X->cell, 0.5f); return; }
	if (E.as<EvCultureDied>()) { PlayCue(EDmCue::Toll, -1, 0.7f); return; }
	if (E.as<EvProphetDied>()) { PlayCue(EDmCue::Toll, -1, 0.35f); return; }
	if (E.as<EvAweShort>()) { PlayCue(EDmCue::Deny); return; }
	if (const EvUiPower* X = E.as<EvUiPower>()) { PlayCue(X->id.empty() ? EDmCue::Click : EDmCue::Arm); return; }
	if (E.as<EvUiSelect>() || E.as<EvUiSpeed>() || E.as<EvUiOverlay>() || E.as<EvUiState>()) { PlayCue(EDmCue::Click); return; }
	if (const EvPhase* X = E.as<EvPhase>()) { if (X->phase != X->prev) PlayCue(EDmCue::Unlock); return; }
}

void ADmAudioDirector::StartCapture(double MaxSeconds)
{
	if (bDevice) { UE_LOG(LogDmAudio, Warning, TEXT("DmAudio: capture refused (the audio device renders the engine)")); return; }
	bCapture = true;
	CaptureMax = FMath::Max(1.0, MaxSeconds);
	CaptureDebt = 0;
	CapL.Reset(int32(CaptureMax * Sr));
	CapR.Reset(int32(CaptureMax * Sr));
}

void ADmAudioDirector::StopCapture()
{
	bCapture = false;
}

void ADmAudioDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Now += DeltaSeconds;
	BrushRain = FMath::Max(0.f, BrushRain - DeltaSeconds * 0.8f);
	TensionHold = FMath::Max(0.f, TensionHold - DeltaSeconds * 0.025f);
	SenseTimer -= DeltaSeconds;
	if (SenseTimer <= 0.f)
	{
		SenseTimer = 0.1f;
		const double T0 = FPlatformTime::Seconds();
		Sense();
		SenseSec += FPlatformTime::Seconds() - T0;
		++SenseCalls;
	}
	UnlockTimer -= DeltaSeconds;
	if (UnlockTimer <= 0.f) { UnlockTimer = 1.f; CheckUnlocks(); }

	if (bCapture && Engine.IsValid())
	{
		// no device: render this frame's share of the mix here (capped so a long frame cannot run away)
		CaptureDebt += double(FMath::Clamp(DeltaSeconds, 0.f, 0.25f)) * Sr;
		const int32 Frames = int32(CaptureDebt);
		CaptureDebt -= Frames;
		const int32 Room = int32(CaptureMax * Sr) - CapL.Num();
		const int32 N = FMath::Min(Frames, Room);
		if (N > 0)
		{
			const int32 Old = CapL.Num();
			CapL.AddUninitialized(N);
			CapR.AddUninitialized(N);
			Engine->RenderSplit(CapL.GetData() + Old, CapR.GetData() + Old, N);
		}
		if (Room <= 0) bCapture = false;
	}
}
