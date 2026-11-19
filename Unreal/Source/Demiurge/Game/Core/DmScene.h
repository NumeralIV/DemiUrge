// DEMIURGE — Game/Core/DmScene.h : scene parts, registered by name from any client area.
// Owner: C0 bridge.
//
// Each client module offers what it draws as a named part with a factory that spawns its actor(s):
//
//   // Game/Planet/DmPlanetActor.cpp (any .cpp of the module; static registration)
//   static FDmScenePartRegistrar GPlanetPart(TEXT("planet"), EDmPartKind::World, 0,
//       [](const FDmSceneContext& S) -> AActor* { return S.World->SpawnActor<ADmPlanet>(); });
//
// A scene (FDmScene::Build) spawns, for every part name, the registered factory with the highest
// priority. The bridge registers temporary placeholders at priority -100 ("planet": the lab planet
// built from the world, "sky": sun, atmosphere, clouds, exposure), so a module that registers the
// same name at priority 0 replaces them everywhere at once, and the "orbit" shot shows the real
// thing as soon as it is in the build. Kinds let a scenario ask for the world only (no HUD, no
// pawn): World (planet, sky, nature, towns, fx), Control (camera pawn, input), Ui (HUD, screens),
// Audio. Command line: -parts=planet,sky (only these names) and -noparts=hud,audio.
#pragma once

#include "CoreMinimal.h"

class AActor;
class APlayerController;
class UDmSimSubsystem;
class UWorld;

enum class EDmPartKind : uint8
{
	World = 1,
	Control = 2,
	Ui = 4,
	Audio = 8,
};
inline constexpr uint32 DmPartsWorld = 1u;
inline constexpr uint32 DmPartsAll = 0xFu;

struct FDmSceneContext
{
	UWorld* World = nullptr;
	UDmSimSubsystem* Sim = nullptr;
	APlayerController* PC = nullptr;
};

using FDmPartFactory = TFunction<AActor*(const FDmSceneContext&)>;

struct FDmScenePart
{
	FName Name;
	EDmPartKind Kind = EDmPartKind::World;
	int32 Priority = 0;
	FDmPartFactory Factory;
	FString Origin;   // registering file (diagnostics)
};

class DEMIURGE_API FDmScene
{
public:
	static void Register(const FDmScenePart& Part);
	/** Winning part per name (highest priority; ties: the later registration), sorted by name. */
	static TArray<FDmScenePart> Winners();
	/** Spawn the winning parts whose kind is in KindMask (and that -parts / -noparts allow).
	 *  Returns the spawned actors (a factory may return nullptr when it attaches to something else). */
	static TArray<AActor*> Build(const FDmSceneContext& Ctx, uint32 KindMask);
	/** Spawn one named part. */
	static AActor* BuildPart(const FDmSceneContext& Ctx, FName Name);
	static bool Has(FName Name);
	/** "planet(0) sky(-100)" style summary for logs. */
	static FString Describe();
};

struct DEMIURGE_API FDmScenePartRegistrar
{
	FDmScenePartRegistrar(const TCHAR* Name, EDmPartKind Kind, int32 Priority, FDmPartFactory Factory, const TCHAR* Origin = TEXT(""));
};
