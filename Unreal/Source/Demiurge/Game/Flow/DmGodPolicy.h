// DEMIURGE — Game/Flow/DmGodPolicy.h : a scripted god for the playthrough (and soak tests). Owner: C10 integration.
//
// The "kind" player of Tools/simrun/Policies.cpp (sim engineer A14), ported to the client: it decides once a
// year (answers prayers with rain / heal / peace / bloom, heals plagues, ends wars with the Omen of Peace,
// keeps a prophet with the leading culture, blooms over the least loved of the leading culture's largest
// towns, inspires with spare Awe, re-awakens a people when everyone is gone) and plays brush strokes over
// several ticks. Unlike simrun it acts like a player: every power goes through the control state
// (UDmControlSubsystem::ArmPower + UsePower, the same path as a click), so the HUD shows the armed power, the
// sim emits its events, and the FX, audio, HUD and chronicle answer as they do for the player.
//
// Call Before(World, Control) before every sim tick (UDmSimSubsystem::BeforeTick).
#pragma once

#include "CoreMinimal.h"
#include <string>
#include "Sim/Core/Rng.h"

class UDmControlSubsystem;
namespace dm { struct World; }

class DEMIURGE_API FDmGodPolicy
{
public:
	explicit FDmGodPolicy(uint32 Seed);
	void Before(dm::World& W, UDmControlSubsystem* Control);

	int32 Clicks = 0, Strokes = 0, Refused = 0, Edicts = 0;
	TMap<FString, int32> ByPower;
	/** The power the god used last (logs). */
	FString LastPower;

private:
	bool Click(dm::World& W, const char* Id, int32 Cell);
	bool StartStroke(dm::World& W, const char* Id, int32 Cell, int32 Ticks);
	void ContinueStroke(dm::World& W);
	bool Reawaken(dm::World& W);
	void DecideKind(dm::World& W);
	bool Use(dm::World& W, const char* Id, int32 Cell, float Dt, bool bNew);

	UDmControlSubsystem* Ctl = nullptr;
	dm::Rng Rng;
	double NextYear = 0.0;
	std::string StrokeId;
	int32 StrokeCell = -1;
	int32 StrokeLeft = 0;
	bool bStrokeFirst = true;
};
