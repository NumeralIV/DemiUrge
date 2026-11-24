// DEMIURGE — Game/FX/DmFxScenarios.cpp : the fx module's scenarios (area "fx"). Owner: C6 fx.
//
// Every scenario runs on a FIXED 1/30 s time step (FApp fixed time step, set in setup), so a frame
// number is an exact effect time: the trigger fires at frame 200 (-fxat=N), so frame 200 + 30 k is
// k seconds after it. Step through an effect with the harness's -warm / -seq, for example
//   Lanes/fx/shot.ps1 fx-meteor -warm=200 -seq=215,235,249,262,300,420
// writes shots/ue-fx-meteor.png (before) and ue-fx-meteor-f<frame>.png. The effects are driven the
// real way wherever the sim allows it: the fixture is made live (sim::use), the god gets Awe, and
// the scenario uses the POWER (Sim->UsePower), so sim events reach ADmFx exactly as in play; time
// runs only where the effect needs ticks (volcano, tsunami, fire, storm). Effects without a power
// (nuke, ark, aurora, seed on the fixture) are previewed with ADmFx::Play.
// Debug switches: -fxtest (one quad of every shape and test rings in front of the camera),
// -fxringoverlay (rings drawn without occlusion), -fxnear=<uu> (near clip plane), -fxstats.
//
//   fx-meteor        meteor on land near the largest town, afternoon light
//   fx-meteor-night  the same at night (the impact lights the land)
//   fx-meteor-sea    meteor into the sea off a coast: splash, steam, tsunami
//   fx-comet         a genesis comet into a young world's basin (sim world, Water phase)
//   fx-volcano       eruption: ash plume, lava fountain, lava field, volcanic lightning (time 0.25x)
//   fx-volcano-night the same at night
//   fx-lightning     a bolt into forest at dusk, the fire it starts
//   fx-fire          a wildfire: lightning plus a ring of ignited forest (life::ignite), time 0.25x
//   fx-storm         a storm started off the coast (powers::start): rain shafts, scud, lightning
//   fx-rain          the rain brush held over farmland
//   fx-tsunami       a sea quake off the coast and its tsunami front (time 0.25x)
//   fx-quake         an earthquake under a town: ripples, dust, landslides
//   fx-bloom         the bloom brush over grassland; fx-seed the seed-life wave
//   fx-plague        plague on a city: miasma; fx-heal, fx-inspire, fx-prophet, fx-peace
//   fx-nuke          nuclear flash, fireball, mushroom cloud, condensation ring
//   fx-ark           the Ark's launch; fx-aurora aurora over the north pole at night
//   fx-grace         founding sparkles, age rays and miracle motes at several towns
//   fx-orbit         several effects at once from orbit (readability at zoom 0)

#include "Game/FX/DmFx.h"

#include "Game/Core/DmCoords.h"
#include "Game/Core/DmGameMode.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmSimSubsystem.h"

#include "Engine/World.h"
#include "Misc/App.h"

#include <vector>

#include "Sim/Life.h"
#include "Sim/Powers.h"
#include "Sim/State.h"

namespace
{
	struct FFxPlan
	{
		FString Power;            // sim power to use at the trigger frame
		FString Play;             // or an ADmFx::Play preview
		int32 Cell = -1;
		int32 BrushFrames = 0;    // brush powers: hold for this many frames
		double IgniteRadius = 0;  // fx-fire: also ignite the forest within this angle (life::ignite on the live world)
		float SpeedAfter = -1.f;  // sim speed after the trigger (-1 keeps it paused)
		struct FExtra { int32 Offset; FString Play; int32 Cell; };
		TArray<FExtra> Extra;     // more previews at frame offsets from the trigger (montages)
		bool bLive = true;
	};
	FFxPlan GPlan;
	int32 GAt = 200;

	double LatOf(const dm::World& W, int32 C) { return FMath::RadiansToDegrees(double(W.geo->lat[size_t(C)])); }
	double LonOf(const dm::World& W, int32 C) { return FMath::RadiansToDegrees(double(W.geo->lon[size_t(C)])); }

	void Look(FDmScenarioContext& Ctx, int32 Cell, double DistKm, double Heading, double Pitch, double Fov)
	{
		const dm::World* W = Ctx.Sim->Sim();
		if (!W || Cell < 0) return;
		Ctx.LookAt(LatOf(*W, Cell), LonOf(*W, Cell), DistKm, Heading, Pitch, Fov);
	}

	/** Sun over (lat/2 + 8, lon + Dlon): 0 = noon over the cell, 60 = afternoon, 85 = dusk, 140 = night. */
	void SunFor(FDmScenarioContext& Ctx, int32 Cell, double Dlon)
	{
		const dm::World* W = Ctx.Sim->Sim();
		if (!W || Cell < 0 || Ctx.Sim->IsSunPinned()) return;
		Ctx.SetSubsolar(FMath::Clamp(LatOf(*W, Cell) * 0.5 + 8.0, -23.0, 23.0), LonOf(*W, Cell) + Dlon);
	}

	void Godly(FDmScenarioContext& Ctx)
	{
		// (wave 4b) also the world's age: v3 unlocks every power past Bronze by age only, so on the Sail-age
		// fixture Volcano (and later powers) were REFUSED and their scenarios showed nothing
		if (dm::World* M = Ctx.Sim->SimMutable()) { M->god.awe = 1.0e5; M->god.aweMax = 1.0e5; if (M->age < dm::Age::INDUSTRY) M->age = dm::Age::INDUSTRY; }   // (not Atomic / Space: Monsoon and Star-fall would change rain and meteor)
	}

	int32 BiggestTown(const dm::World& W, bool bCoastal = false)
	{
		int32 Best = -1;
		double Pop = -1;
		for (const dm::Settlement& S : W.settlements)
		{
			if (S.dead || S.cell < 0) continue;
			if (bCoastal && W.coast[size_t(S.cell)] > 2) continue;
			if (FMath::Abs(W.geo->lat[size_t(S.cell)]) > 1.0f) continue;
			if (S.pop > Pop) { Pop = S.pop; Best = S.id; }
		}
		return Best;
	}
	int32 TownCell(const dm::World& W, int32 Sid) { return (Sid >= 0 && Sid < int32(W.settlements.size())) ? W.settlements[size_t(Sid)].cell : -1; }

	/** The cell within [MinAng, MaxAng] of Near that satisfies Pred, preferring the middle of the band. */
	int32 FindCell(const dm::World& W, int32 Near, double MinAng, double MaxAng, TFunctionRef<bool(int32)> Pred)
	{
		if (Near < 0) return -1;
		std::vector<int32_t> Cells;
		W.geo->within(Near, MaxAng, Cells);
		const double Mid = 0.5 * (MinAng + MaxAng);
		int32 Best = -1;
		double BestD = 1e9;
		for (int32_t C : Cells)
		{
			const double A = W.geo->angle(Near, C);
			if (A < MinAng || !Pred(int32(C))) continue;
			const double D = FMath::Abs(A - Mid) + 0.002 * double(dm::hashf(uint32(C), 7u));
			if (D < BestD) { BestD = D; Best = int32(C); }
		}
		return Best;
	}

	bool IsLand(const dm::World& W, int32 C) { return !dm::state::isSea(W, C) && W.water[size_t(C)] < 0.03f; }

	void BeginFx(FDmScenarioContext& Ctx, bool bLive)
	{
		FApp::SetUseFixedTimeStep(true);
		FApp::SetFixedDeltaTime(1.0 / 30.0);
		FDmShotArgs::Value(TEXT("-fxat="), GAt);
		double Near = 0;
		if (FDmShotArgs::Value(TEXT("-fxnear="), Near) && Near > 0) { GNearClippingPlane = float(Near); Ctx.Log(FString::Printf(TEXT("DmFxScenario: near clip plane %.0f uu"), Near)); }
		GPlan = FFxPlan();
		GPlan.bLive = bLive;
		if (bLive && Ctx.Sim->HasWorld() && !Ctx.Sim->IsLive()) Ctx.Sim->MakeLive();
		if (bLive) { Ctx.Sim->SetSpeed(0.f); Godly(Ctx); }
		Ctx.BuildScene(DmPartsWorld);
	}

	void FxTick(FDmScenarioContext& Ctx, float Dt)
	{
		const int32 F = Ctx.Frame;
		if (GPlan.bLive) Godly(Ctx);
		ADmFx* Fx = ADmFx::Find(Ctx.World);
		if (F == GAt && (!GPlan.Play.IsEmpty() || !GPlan.Power.IsEmpty()))
		{
			bool bOk = false;
			if (!GPlan.Play.IsEmpty()) bOk = Fx && Fx->Play(GPlan.Play, GPlan.Cell);
			else if (!GPlan.Power.IsEmpty()) bOk = Ctx.Sim->UsePower(GPlan.Power, GPlan.Cell, 0.f);
			const dm::World* Wl = Ctx.Sim->Sim();
			const int32 Cc = GPlan.Cell;
			Ctx.Log(FString::Printf(TEXT("DmFxScenario: %s at cell %d (lat %.1f lon %.1f, elev %.2f, veg %.2f, sea %d) (frame %d) -> %s"), GPlan.Play.IsEmpty() ? *GPlan.Power : *GPlan.Play, Cc,
				Wl && Cc >= 0 ? LatOf(*Wl, Cc) : 0.0, Wl && Cc >= 0 ? LonOf(*Wl, Cc) : 0.0, Wl && Cc >= 0 ? Wl->elev[size_t(Cc)] : 0.f, Wl && Cc >= 0 ? Wl->veg[size_t(Cc)] : 0.f,
				Wl && Cc >= 0 && dm::state::isSea(*Wl, Cc) ? 1 : 0, F, bOk ? TEXT("ok") : TEXT("REFUSED")));
			if (GPlan.IgniteRadius > 0 && Ctx.Sim->SimMutable())
			{
				dm::World& Mw = *Ctx.Sim->SimMutable();
				std::vector<int32_t> Cells;
				Mw.geo->within(GPlan.Cell, GPlan.IgniteRadius, Cells);
				int32 Lit = 0;
				for (int32_t C : Cells) if (Mw.veg[size_t(C)] > 0.3f && dm::life::ignite(Mw, C, 1.0)) ++Lit;
				Ctx.Log(FString::Printf(TEXT("DmFxScenario: ignited %d of %d cells"), Lit, int32(Cells.size())));
			}
		}
		if (F == GAt && GPlan.SpeedAfter >= 0.f) Ctx.Sim->SetSpeed(GPlan.SpeedAfter);
		if (GPlan.BrushFrames > 0 && F > GAt && F <= GAt + GPlan.BrushFrames && !GPlan.Power.IsEmpty())
			Ctx.Sim->UsePower(GPlan.Power, GPlan.Cell, Dt);
		for (const auto& X : GPlan.Extra)
			if (F == GAt + X.Offset && Fx) Fx->Play(X.Play, X.Cell);
		if (Fx && (F % 30 == 0) && F >= GAt)
		{
			FString Dis;
			for (const dm::Disaster& D : Ctx.Sim->Sim()->disasters)
				Dis += FString::Printf(TEXT(" %s#%d(t %.2f/%.2f, front %.3f, %d cells)"), UTF8_TO_TCHAR(dm::toStr(D.type)), D.id, D.t, D.dur, D.front, int32(D.cells.size()));
			Ctx.Log(FString::Printf(TEXT("DmFxScenario: frame %d t+%.2f s: %d effects, %d particles, %d quads, year %.2f, %d burning cells; disasters:%s"), F, (F - GAt) / 30.0,
				Fx->GetEffectCount(), Fx->GetParticleCount(), Fx->GetQuadCount(), Ctx.Sim->Sim()->year, int32(Ctx.Sim->Sim()->cache.fires.size()), *Dis));
		}
	}

	struct FReg
	{
		FReg(const TCHAR* Name, const TCHAR* Help, TFunction<void(FDmScenarioContext&)> Setup)
		{
			FDmScenarios::Register({ Name, TEXT("fx"), Help, MoveTemp(Setup), [](FDmScenarioContext& Ctx, float Dt) { FxTick(Ctx, Dt); } });
		}
	};

	// --------------------------------------------------------------- wrath

	void MeteorSetup(FDmScenarioContext& Ctx, double SunDlon)
	{
		BeginFx(Ctx, true);
		const dm::World& W = *Ctx.Sim->Sim();
		const int32 Town = TownCell(W, BiggestTown(W));
		GPlan.Cell = FindCell(W, Town, 0.10, 0.16, [&W](int32 C) { return IsLand(W, C) && W.owner[size_t(C)] < 0; });
		if (GPlan.Cell < 0) GPlan.Cell = FindCell(W, Town, 0.05, 0.25, [&W](int32 C) { return IsLand(W, C); });
		GPlan.Power = TEXT("meteor");
		Look(Ctx, GPlan.Cell, 48.0, 15.0, 36.0, 60.0);
		SunFor(Ctx, GPlan.Cell, SunDlon);
	}

	FReg GMeteor(TEXT("fx-meteor"), TEXT("meteor on land near the largest town, afternoon"), [](FDmScenarioContext& Ctx) { MeteorSetup(Ctx, 55.0); });
	FReg GMeteorNight(TEXT("fx-meteor-night"), TEXT("meteor on land at night"), [](FDmScenarioContext& Ctx) { MeteorSetup(Ctx, 150.0); });

	FReg GMeteorSea(TEXT("fx-meteor-sea"), TEXT("meteor into the sea off a coast: splash, steam, tsunami"), [](FDmScenarioContext& Ctx)
	{
		BeginFx(Ctx, true);
		const dm::World& W = *Ctx.Sim->Sim();
		const int32 Town = TownCell(W, BiggestTown(W, true));
		GPlan.Cell = FindCell(W, Town, 0.08, 0.16, [&W](int32 C) { return dm::state::isSea(W, C) && W.elev[size_t(C)] < W.seaLevel - 0.8f; });
		GPlan.Power = TEXT("meteor");
		GPlan.SpeedAfter = 0.25f;
		Look(Ctx, GPlan.Cell, 95.0, 200.0, 30.0, 55.0);
		SunFor(Ctx, GPlan.Cell, 50.0);
	});

	FReg GComet(TEXT("fx-comet"), TEXT("a genesis comet into a young world (sim world seed 5, Water phase)"), [](FDmScenarioContext& Ctx)
	{
		FApp::SetUseFixedTimeStep(true);
		FApp::SetFixedDeltaTime(1.0 / 30.0);
		FDmShotArgs::Value(TEXT("-fxat="), GAt);
		GPlan = FFxPlan();
		int32 Seed = 5;
		FDmShotArgs::Value(TEXT("-fxseed="), Seed);
		Ctx.Sim->CreateWorld(uint32(Seed));
		Ctx.Sim->StepTicks(64);   // Molten cools to Water in 60 ticks
		Ctx.Sim->SetPaused(true);
		Ctx.BuildScene(DmPartsWorld);
		const dm::World& W = *Ctx.Sim->Sim();
		int32 Low = 0;
		for (int32 C = 0; C < W.n; ++C) if (W.elev[size_t(C)] < W.elev[size_t(Low)] && FMath::Abs(W.geo->lat[size_t(C)]) < 0.6f) Low = C;
		GPlan.Cell = Low;
		GPlan.Power = TEXT("comet");
		Ctx.Log(FString::Printf(TEXT("DmFxScenario: comet world phase %s, sea %.2f"), UTF8_TO_TCHAR(dm::toStr(W.phase)), W.seaLevel));
		Look(Ctx, GPlan.Cell, 150.0, 10.0, 35.0, 55.0);
		SunFor(Ctx, GPlan.Cell, 45.0);
	});

	void VolcanoSetup(FDmScenarioContext& Ctx, double SunDlon)
	{
		BeginFx(Ctx, true);
		const dm::World& W = *Ctx.Sim->Sim();
		const int32 Town = TownCell(W, BiggestTown(W, true));
		GPlan.Cell = FindCell(W, Town, 0.12, 0.25, [&W](int32 C) { return IsLand(W, C) && W.coast[size_t(C)] >= 2 && W.coast[size_t(C)] <= 4; });
		GPlan.Power = TEXT("volcano");
		GPlan.SpeedAfter = 0.25f;
		Look(Ctx, GPlan.Cell, 32.0, 30.0, 26.0, 60.0);
		SunFor(Ctx, GPlan.Cell, SunDlon);
	}
	FReg GVolcano(TEXT("fx-volcano"), TEXT("an eruption (time 0.25x): ash plume, lava fountain, lava field, volcanic lightning"), [](FDmScenarioContext& Ctx) { VolcanoSetup(Ctx, 65.0); });
	FReg GVolcanoNight(TEXT("fx-volcano-night"), TEXT("an eruption at night"), [](FDmScenarioContext& Ctx) { VolcanoSetup(Ctx, 150.0); });

	int32 ForestNearTown(const dm::World& W)
	{
		const int32 Town = TownCell(W, BiggestTown(W));
		// a dry forest (fire spreads with veg x dryness; wet > 0.3 cannot burn)
		int32 C = FindCell(W, Town, 0.04, 0.3, [&W](int32 X) { return IsLand(W, X) && W.veg[size_t(X)] > 0.55f && W.owner[size_t(X)] < 0 && W.moist[size_t(X)] < 0.45f && W.wet[size_t(X)] < 0.1f; });
		if (C < 0) C = FindCell(W, Town, 0.04, 0.2, [&W](int32 X) { return IsLand(W, X) && W.veg[size_t(X)] > 0.6f && W.owner[size_t(X)] < 0; });
		if (C < 0) C = FindCell(W, Town, 0.02, 0.4, [&W](int32 X) { return IsLand(W, X) && W.veg[size_t(X)] > 0.4f; });
		return C;
	}

	FReg GLightning(TEXT("fx-lightning"), TEXT("a bolt into forest at dusk and the fire it starts"), [](FDmScenarioContext& Ctx)
	{
		BeginFx(Ctx, true);
		const dm::World& W = *Ctx.Sim->Sim();
		GPlan.Cell = ForestNearTown(W);
		GPlan.Power = TEXT("lightning");
		GPlan.SpeedAfter = 0.25f;
		Look(Ctx, GPlan.Cell, 18.0, 40.0, 20.0, 60.0);
		SunFor(Ctx, GPlan.Cell, 100.0);
	});

	FReg GFire(TEXT("fx-fire"), TEXT("a wildfire spreading through forest (time 2x)"), [](FDmScenarioContext& Ctx)
	{
		BeginFx(Ctx, true);
		const dm::World& W = *Ctx.Sim->Sim();
		GPlan.Cell = ForestNearTown(W);
		GPlan.Power = TEXT("lightning");
		GPlan.SpeedAfter = 0.25f;
		GPlan.IgniteRadius = 0.035;
		Look(Ctx, GPlan.Cell, 34.0, 220.0, 26.0, 55.0);
		SunFor(Ctx, GPlan.Cell, 60.0);
	});

	FReg GStorm(TEXT("fx-storm"), TEXT("a storm off the coast (started by the scenario, 6 years at 0.25x): rain shafts, scud, lightning"), [](FDmScenarioContext& Ctx)
	{
		BeginFx(Ctx, true);
		const dm::World& W = *Ctx.Sim->Sim();
		const int32 Town = TownCell(W, BiggestTown(W, true));
		int32 C = FindCell(W, Town, 0.08, 0.16, [&W](int32 X) { return dm::state::isSea(W, X); });
		if (C < 0) C = Town;
		if (dm::World* Mw = Ctx.Sim->SimMutable())
		{
			dm::powers::StartOpts O;
			O.dur = 6.0;
			O.power = 1.0;
			O.ang = 0.12;
			const dm::Disaster* D = dm::powers::start(*Mw, dm::DisasterType::Storm, C, O);
			Ctx.Log(FString::Printf(TEXT("DmFxScenario: storm started at cell %d -> %s"), C, D ? TEXT("ok") : TEXT("REFUSED")));
		}
		GPlan.Cell = C;
		GPlan.SpeedAfter = 0.25f;
		Look(Ctx, C, 45.0, 0.0, 28.0, 60.0);
		SunFor(Ctx, C, 50.0);
	});
	FReg GRain(TEXT("fx-rain"), TEXT("the rain brush held over farmland near a town"), [](FDmScenarioContext& Ctx)
	{
		BeginFx(Ctx, true);
		const dm::World& W = *Ctx.Sim->Sim();
		const int32 Town = TownCell(W, BiggestTown(W));
		GPlan.Cell = FindCell(W, Town, 0.02, 0.1, [&W](int32 C) { return IsLand(W, C) && W.farm[size_t(C)] > 0.2f; });
		if (GPlan.Cell < 0) GPlan.Cell = Town;
		GPlan.Power = TEXT("rain");
		GPlan.BrushFrames = 240;
		Look(Ctx, GPlan.Cell, 40.0, 60.0, 18.0, 60.0);
		SunFor(Ctx, GPlan.Cell, 45.0);
	});

	FReg GTsunami(TEXT("fx-tsunami"), TEXT("a sea quake off the coast and its tsunami (time 0.25x)"), [](FDmScenarioContext& Ctx)
	{
		BeginFx(Ctx, true);
		const dm::World& W = *Ctx.Sim->Sim();
		const int32 Town = TownCell(W, BiggestTown(W, true));
		GPlan.Cell = FindCell(W, Town, 0.18, 0.28, [&W](int32 C) { return dm::state::isSea(W, C) && W.elev[size_t(C)] < W.seaLevel - 1.0f; });
		GPlan.Power = TEXT("quake");
		GPlan.SpeedAfter = 0.25f;
		Look(Ctx, Town, 160.0, 0.0, 55.0, 55.0);
		SunFor(Ctx, Town, 50.0);
	});

	FReg GQuake(TEXT("fx-quake"), TEXT("an earthquake under a town: ripples, dust, landslides"), [](FDmScenarioContext& Ctx)
	{
		BeginFx(Ctx, true);
		const dm::World& W = *Ctx.Sim->Sim();
		const int32 Town = TownCell(W, BiggestTown(W));
		GPlan.Cell = FindCell(W, Town, 0.0, 0.04, [&W](int32 C) { return IsLand(W, C); });
		GPlan.Power = TEXT("quake");
		Look(Ctx, GPlan.Cell, 45.0, 30.0, 28.0, 60.0);
		SunFor(Ctx, GPlan.Cell, 55.0);
	});

	FReg GNuke(TEXT("fx-nuke"), TEXT("nuclear flash, fireball, mushroom cloud, condensation ring (preview)"), [](FDmScenarioContext& Ctx)
	{
		BeginFx(Ctx, true);
		const dm::World& W = *Ctx.Sim->Sim();
		const int32 Town = TownCell(W, BiggestTown(W));
		GPlan.Cell = FindCell(W, Town, 0.04, 0.08, [&W](int32 C) { return IsLand(W, C); });
		GPlan.Play = TEXT("nuke");
		Look(Ctx, GPlan.Cell, 34.0, 10.0, 14.0, 60.0);
		SunFor(Ctx, GPlan.Cell, 60.0);
	});

	FReg GPlague(TEXT("fx-plague"), TEXT("plague on a city: miasma"), [](FDmScenarioContext& Ctx)
	{
		BeginFx(Ctx, true);
		const dm::World& W = *Ctx.Sim->Sim();
		GPlan.Cell = TownCell(W, BiggestTown(W));
		GPlan.Power = TEXT("plague");
		Look(Ctx, GPlan.Cell, 18.0, 30.0, 25.0, 60.0);
		SunFor(Ctx, GPlan.Cell, 70.0);
	});

	void GraceSetup(FDmScenarioContext& Ctx, const TCHAR* Power, double Dist, double Pitch)
	{
		BeginFx(Ctx, true);
		const dm::World& W = *Ctx.Sim->Sim();
		GPlan.Cell = TownCell(W, BiggestTown(W));
		GPlan.Power = Power;
		Look(Ctx, GPlan.Cell, Dist, 30.0, Pitch, 60.0);
		SunFor(Ctx, GPlan.Cell, 70.0);
	}
	FReg GHeal(TEXT("fx-heal"), TEXT("heal: a column of gold light"), [](FDmScenarioContext& Ctx) { GraceSetup(Ctx, TEXT("heal"), 24.0, 28.0); });
	FReg GInspire(TEXT("fx-inspire"), TEXT("inspire: pale light and rising sparks"), [](FDmScenarioContext& Ctx) { GraceSetup(Ctx, TEXT("inspire"), 24.0, 28.0); });
	FReg GProphet(TEXT("fx-prophet"), TEXT("raise a prophet: a radiant halo"), [](FDmScenarioContext& Ctx) { GraceSetup(Ctx, TEXT("prophet"), 20.0, 30.0); });

	FReg GPeace(TEXT("fx-peace"), TEXT("the omen of peace over a town at war"), [](FDmScenarioContext& Ctx)
	{
		BeginFx(Ctx, true);
		const dm::World& W = *Ctx.Sim->Sim();
		int32 Sid = -1;
		for (const auto& P : W.vars.rel)
			if (P.second.war)
			{
				const int32 Cu = P.first.first;
				if (Cu >= 0 && Cu < int32(W.cultures.size())) Sid = W.cultures[size_t(Cu)].capital;
				break;
			}
		if (Sid < 0) Sid = BiggestTown(W);
		GPlan.Cell = TownCell(W, Sid);
		GPlan.Power = TEXT("peace");
		Look(Ctx, GPlan.Cell, 70.0, 0.0, 30.0, 60.0);
		SunFor(Ctx, GPlan.Cell, 80.0);
	});

	FReg GBloom(TEXT("fx-bloom"), TEXT("the bloom brush held over grassland"), [](FDmScenarioContext& Ctx)
	{
		BeginFx(Ctx, true);
		const dm::World& W = *Ctx.Sim->Sim();
		const int32 Town = TownCell(W, BiggestTown(W));
		GPlan.Cell = FindCell(W, Town, 0.06, 0.2, [&W](int32 C) { return IsLand(W, C) && W.veg[size_t(C)] < 0.5f && W.owner[size_t(C)] < 0; });
		if (GPlan.Cell < 0) GPlan.Cell = Town;
		GPlan.Power = TEXT("bloom");
		GPlan.BrushFrames = 150;
		Look(Ctx, GPlan.Cell, 32.0, 20.0, 24.0, 60.0);
		SunFor(Ctx, GPlan.Cell, 70.0);
	});

	FReg GSeed(TEXT("fx-seed"), TEXT("the seed-life wave (preview on the fixture)"), [](FDmScenarioContext& Ctx)
	{
		BeginFx(Ctx, true);
		const dm::World& W = *Ctx.Sim->Sim();
		const int32 Town = TownCell(W, BiggestTown(W));
		GPlan.Cell = FindCell(W, Town, 0.15, 0.3, [&W](int32 C) { return IsLand(W, C) && W.veg[size_t(C)] < 0.4f; });
		if (GPlan.Cell < 0) GPlan.Cell = Town;
		GPlan.Play = TEXT("seed");
		Look(Ctx, GPlan.Cell, 45.0, 20.0, 30.0, 60.0);
		SunFor(Ctx, GPlan.Cell, 70.0);
	});

	FReg GArk(TEXT("fx-ark"), TEXT("the Ark's launch (preview)"), [](FDmScenarioContext& Ctx)
	{
		BeginFx(Ctx, true);
		const dm::World& W = *Ctx.Sim->Sim();
		GPlan.Cell = TownCell(W, BiggestTown(W));
		GPlan.Play = TEXT("ark");
		Look(Ctx, GPlan.Cell, 45.0, 40.0, 8.0, 60.0);
		SunFor(Ctx, GPlan.Cell, 80.0);
	});

	FReg GAurora(TEXT("fx-aurora"), TEXT("aurora over the north pole at night (preview)"), [](FDmScenarioContext& Ctx)
	{
		BeginFx(Ctx, true);
		const dm::World& W = *Ctx.Sim->Sim();
		GPlan.Cell = TownCell(W, BiggestTown(W));
		GPlan.Play = TEXT("aurora");
		const double Lon = LonOf(W, GPlan.Cell);
		Ctx.LookAt(66.0, Lon, 260.0, 0.0, 62.0, 60.0);
		if (!Ctx.Sim->IsSunPinned()) Ctx.SetSubsolar(-18.0, Lon + 180.0);
	});

	FReg GGrace(TEXT("fx-grace"), TEXT("founding sparkles, age rays and miracle motes at several towns"), [](FDmScenarioContext& Ctx)
	{
		BeginFx(Ctx, true);
		const dm::World& W = *Ctx.Sim->Sim();
		const int32 Sid = BiggestTown(W);
		GPlan.Cell = TownCell(W, Sid);
		GPlan.Play = TEXT("age");
		// neighbouring towns
		TArray<int32> Near;
		for (const dm::Settlement& S : W.settlements)
			if (!S.dead && S.id != Sid && W.geo->angle(S.cell, GPlan.Cell) < 0.12) Near.Add(S.cell);
		if (Near.Num() > 0) GPlan.Extra.Add({ 10, TEXT("founded"), Near[0] });
		if (Near.Num() > 1) GPlan.Extra.Add({ 20, TEXT("bless"), Near[1] });
		if (Near.Num() > 2) GPlan.Extra.Add({ 30, TEXT("battle"), Near[2] });
		Look(Ctx, GPlan.Cell, 45.0, 30.0, 28.0, 60.0);
		SunFor(Ctx, GPlan.Cell, 80.0);
	});

	FReg GOrbit(TEXT("fx-orbit"), TEXT("several effects at once from orbit: readability at zoom 0"), [](FDmScenarioContext& Ctx)
	{
		BeginFx(Ctx, true);
		const dm::World& W = *Ctx.Sim->Sim();
		const int32 Town = TownCell(W, BiggestTown(W));
		GPlan.Cell = FindCell(W, Town, 0.25, 0.35, [&W](int32 C) { return IsLand(W, C); });
		GPlan.Power = TEXT("meteor");
		const int32 V = FindCell(W, Town, 0.4, 0.55, [&W](int32 C) { return IsLand(W, C); });
		const int32 L = FindCell(W, Town, 0.15, 0.25, [&W](int32 C) { return IsLand(W, C) && W.veg[size_t(C)] > 0.5f; });
		if (V >= 0) GPlan.Extra.Add({ -30, TEXT("nuke"), V });
		if (L >= 0) GPlan.Extra.Add({ 20, TEXT("lightning"), L });
		GPlan.Extra.Add({ 0, TEXT("aurora"), Town });
		GPlan.SpeedAfter = 1.f;
		const dm::World& Wc = W;
		Ctx.LookAt(LatOf(Wc, Town), LonOf(Wc, Town), 700.0, 0.0, 90.0, 30.0);
		Ctx.SetSubsolar(FMath::Clamp(LatOf(Wc, Town) * 0.5 + 8.0, -23.0, 23.0), LonOf(Wc, Town) + 55.0);
	});
}
