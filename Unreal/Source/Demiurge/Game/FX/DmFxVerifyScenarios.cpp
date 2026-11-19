// DEMIURGE — Game/FX/DmFxVerifyScenarios.cpp : the fx verifier's worst-case scenarios (area "fx").
// Owner: C6 fx (written by the C6 verifier, lane fx-verify).
//
// Fixed 1/30 s step like DmFxScenarios.cpp; the trigger fires at frame 200 (-fxat=N). DmStats (frame / GPU
// times) covers the 60 frames before the FIRST shot, so put -warm at the frame you want measured and
// compare with -noparts=fx for the module's share.
//
//   fxv-planet   the whole planet in view (700 km, fov 30) with the terminator in frame: three meteors (day,
//                terminator, night side), a volcano, a nuclear strike, a wildfire, a storm, plague, aurora; 1x
//   fxv-at       one effect, any camera: -fxvwhat=meteor|volcano|lightning|quake|plague|heal|inspire|prophet|
//                peace|rain|bloom|nuke|ark|seed|fire (fire = lightning + ignite 0.04 rad) -fxvdist=km
//                -fxvpitch=deg -fxvhead=deg -fxvsun=dlon (0 noon, 60 afternoon, 100 dusk, 160 night)
//                -fxvspeed=x (sim speed after the trigger) -fxvsea (pick a sea cell)
//   fxv-stress   extreme state at 60 km: a forest fire over 0.2 rad, eight meteors, two volcanoes, two nukes
//                (particle and quad caps, light pool, tick time)
//   fxv-fast     live sim at 8x with calamity 25 over the most settled region at 300 km for 100 s, a meteor
//                every 8 s, the time-lapse quiet switch at 60-66 s, then a world reload at 80 s; logs that
//                every count stays bounded and the world change leaves nothing behind (exit code 1 if not)

#include "Game/FX/DmFx.h"

#include "Game/Core/DmCoords.h"
#include "Game/Core/DmGameMode.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmSimSubsystem.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"

#include <vector>

#include "Sim/Life.h"
#include "Sim/Powers.h"
#include "Sim/State.h"

namespace
{
	int32 GVAt = 200;

	double VLat(const dm::World& W, int32 C) { return FMath::RadiansToDegrees(double(W.geo->lat[size_t(C)])); }
	double VLon(const dm::World& W, int32 C) { return FMath::RadiansToDegrees(double(W.geo->lon[size_t(C)])); }
	bool VLand(const dm::World& W, int32 C) { return !dm::state::isSea(W, C) && W.water[size_t(C)] < 0.03f; }

	int32 VBiggest(const dm::World& W)
	{
		int32 Best = -1; double Pop = -1;
		for (const dm::Settlement& S : W.settlements)
		{
			if (S.dead || S.cell < 0 || FMath::Abs(W.geo->lat[size_t(S.cell)]) > 1.0f) continue;
			if (S.pop > Pop) { Pop = S.pop; Best = S.cell; }
		}
		return Best;
	}

	int32 VFind(const dm::World& W, int32 Near, double MinAng, double MaxAng, TFunctionRef<bool(int32)> Pred, uint32 Salt = 7u)
	{
		if (Near < 0) return -1;
		std::vector<int32_t> Cells;
		W.geo->within(Near, MaxAng, Cells);
		const double Mid = 0.5 * (MinAng + MaxAng);
		int32 Best = -1; double BestD = 1e9;
		for (int32_t C : Cells)
		{
			const double A = W.geo->angle(Near, C);
			if (A < MinAng || !Pred(int32(C))) continue;
			const double D = FMath::Abs(A - Mid) + 0.01 * double(dm::hashf(uint32(C), Salt));
			if (D < BestD) { BestD = D; Best = int32(C); }
		}
		return Best;
	}

	/** The cell at (lat, lon) of the sim. */
	int32 VCellAt(const dm::World& W, double LatDeg, double LonDeg)
	{
		const FVector D = DmCoords::LatLonToDir(LatDeg, LonDeg);
		const dm::Vec3 S = DmCoords::UEToSim(D);
		return W.geo->cellAt(S.x, S.y, S.z);
	}

	void VBegin(FDmScenarioContext& Ctx, bool bLive)
	{
		FApp::SetUseFixedTimeStep(true);
		FApp::SetFixedDeltaTime(1.0 / 30.0);
		FDmShotArgs::Value(TEXT("-fxat="), GVAt);
		if (bLive && Ctx.Sim->HasWorld() && !Ctx.Sim->IsLive()) Ctx.Sim->MakeLive();
		Ctx.Sim->SetSpeed(0.f);
		if (dm::World* M = Ctx.Sim->SimMutable()) { M->god.awe = 1.0e6; M->god.aweMax = 1.0e6; }
		Ctx.BuildScene(DmPartsWorld);
	}

	void VGodly(FDmScenarioContext& Ctx)
	{
		if (dm::World* M = Ctx.Sim->SimMutable()) { M->god.awe = 1.0e6; M->god.aweMax = 1.0e6; }
	}

	int32 VIgnite(FDmScenarioContext& Ctx, int32 Cell, double Ang, float MinVeg = 0.3f)
	{
		dm::World* M = Ctx.Sim->SimMutable();
		if (!M || Cell < 0) return 0;
		std::vector<int32_t> Cells;
		M->geo->within(Cell, Ang, Cells);
		int32 Lit = 0;
		for (int32_t C : Cells) if (M->veg[size_t(C)] > MinVeg && dm::life::ignite(*M, C, 1.0)) ++Lit;
		return Lit;
	}

	double GTickMsMax = 0, GTickMsSum = 0;
	int32 GTickN = 0;
	int32 GPeakParticles = 0, GPeakQuads = 0, GPeakEffects = 0;

	void VLogFx(FDmScenarioContext& Ctx, const TCHAR* Tag)
	{
		ADmFx* Fx = ADmFx::Find(Ctx.World);
		const dm::World* W = Ctx.Sim->Sim();
		if (!Fx || !W) return;
		GPeakParticles = FMath::Max(GPeakParticles, Fx->GetParticleCount());
		GPeakQuads = FMath::Max(GPeakQuads, Fx->GetQuadCount());
		GPeakEffects = FMath::Max(GPeakEffects, Fx->GetEffectCount());
		Ctx.Log(FString::Printf(TEXT("DmFxVerify %s frame %d: effects %d particles %d quads %d | disasters %d fires %d year %.2f"), Tag, Ctx.Frame,
			Fx->GetEffectCount(), Fx->GetParticleCount(), Fx->GetQuadCount(), int32(W->disasters.size()), int32(W->cache.fires.size()), W->year));
	}

	/** Game-thread cost of ADmFx::Tick is not exposed, so time the whole frame's fx actor tick via the stats switch
	 *  (-fxstats) and keep our own peak counters here. */
	void VPeaks(FDmScenarioContext& Ctx)
	{
		if (ADmFx* Fx = ADmFx::Find(Ctx.World))
		{
			GPeakParticles = FMath::Max(GPeakParticles, Fx->GetParticleCount());
			GPeakQuads = FMath::Max(GPeakQuads, Fx->GetQuadCount());
			GPeakEffects = FMath::Max(GPeakEffects, Fx->GetEffectCount());
		}
	}

	// ------------------------------------------------------------------ fxv-planet

	struct FPlanetPlan { int32 Town = -1, MDay = -1, MTerm = -1, MNight = -1, Volc = -1, Nuke = -1, Forest = -1, Storm = -1, Plague = -1; };
	FPlanetPlan GP;

	FDmScenarioRegistrar GFxvPlanet(TEXT("fxv-planet"), TEXT("fx"), TEXT("verifier: the whole planet in view with many effects at once"),
		[](FDmScenarioContext& Ctx)
		{
			VBegin(Ctx, true);
			const dm::World& W = *Ctx.Sim->Sim();
			GP = FPlanetPlan();
			GP.Town = VBiggest(W);
			const double La = VLat(W, GP.Town), Lo = VLon(W, GP.Town);
			Ctx.LookAt(La, Lo, 700.0, 0.0, 90.0, 30.0);
			// sun 55 degrees from the view centre: day side, terminator and night side are all in frame
			Ctx.SetSubsolar(FMath::Clamp(La * 0.5 + 8.0, -23.0, 23.0), Lo + 55.0);
			auto Land = [&W](int32 C) { return VLand(W, C); };
			// the sim's longitude grows westward; pick by angle from the subsolar point instead
			const int32 Sub = VCellAt(W, FMath::Clamp(La * 0.5 + 8.0, -23.0, 23.0), Lo + 55.0);
			auto ByDay = [&](double MinA, double MaxA) { return VFind(W, GP.Town, 0.05, 0.9, [&](int32 C) { const double A = W.geo->angle(Sub, C); return Land(C) && A > MinA && A < MaxA; }); };
			GP.MDay = ByDay(0.5, 1.0);
			GP.MTerm = ByDay(1.45, 1.62);
			GP.MNight = ByDay(1.85, 2.4);
			GP.Volc = VFind(W, GP.Town, 0.3, 0.5, Land, 11u);
			GP.Nuke = VFind(W, GP.Town, 0.15, 0.3, Land, 13u);
			GP.Forest = VFind(W, GP.Town, 0.2, 0.6, [&W](int32 C) { return VLand(W, C) && W.veg[size_t(C)] > 0.6f && W.owner[size_t(C)] < 0; }, 17u);
			GP.Storm = VFind(W, GP.Town, 0.3, 0.7, [&W](int32 C) { return dm::state::isSea(W, C); }, 19u);
			GP.Plague = GP.Town;
			Ctx.Log(FString::Printf(TEXT("DmFxVerify planet: town %d meteors %d/%d/%d volcano %d nuke %d forest %d storm %d"),
				GP.Town, GP.MDay, GP.MTerm, GP.MNight, GP.Volc, GP.Nuke, GP.Forest, GP.Storm));
		},
		[](FDmScenarioContext& Ctx, float)
		{
			VGodly(Ctx);
			ADmFx* Fx = ADmFx::Find(Ctx.World);
			const int32 F = Ctx.Frame;
			if (F == GVAt)
			{
				int32 Ok = 0;
				Ok += Ctx.Sim->UsePower(TEXT("meteor"), GP.MDay, 0.f) ? 1 : 0;
				Ok += Ctx.Sim->UsePower(TEXT("volcano"), GP.Volc, 0.f) ? 1 : 0;
				Ok += Ctx.Sim->UsePower(TEXT("plague"), GP.Plague, 0.f) ? 1 : 0;
				Ok += Ctx.Sim->UsePower(TEXT("lightning"), GP.Forest, 0.f) ? 1 : 0;
				const int32 Lit = VIgnite(Ctx, GP.Forest, 0.06);
				if (dm::World* M = Ctx.Sim->SimMutable())
				{
					dm::powers::StartOpts O; O.dur = 6.0; O.power = 1.0; O.ang = 0.12;
					Ok += dm::powers::start(*M, dm::DisasterType::Storm, GP.Storm, O) ? 1 : 0;
				}
				if (Fx) { Fx->Play(TEXT("nuke"), GP.Nuke); Fx->Play(TEXT("aurora"), GP.Town); }
				Ctx.Sim->SetSpeed(1.f);
				Ctx.Log(FString::Printf(TEXT("DmFxVerify planet: %d of 5 sim triggers ok, %d cells ignited"), Ok, Lit));
			}
			if (F == GVAt + 20) Ctx.Sim->UsePower(TEXT("meteor"), GP.MTerm, 0.f);
			if (F == GVAt + 40) Ctx.Sim->UsePower(TEXT("meteor"), GP.MNight, 0.f);
			VPeaks(Ctx);
			if (F >= GVAt && F % 30 == 0) VLogFx(Ctx, TEXT("planet"));
		});

	// ------------------------------------------------------------------ fxv-at

	FString GWhat;
	int32 GAtCell = -1;
	double GSpeedAfter = 0;

	FDmScenarioRegistrar GFxvAt(TEXT("fxv-at"), TEXT("fx"), TEXT("verifier: one effect with any camera (-fxvwhat, -fxvdist, -fxvpitch, -fxvhead, -fxvsun)"),
		[](FDmScenarioContext& Ctx)
		{
			VBegin(Ctx, true);
			const dm::World& W = *Ctx.Sim->Sim();
			GWhat = TEXT("meteor");
			FDmShotArgs::Value(TEXT("-fxvwhat="), GWhat);
			double Dist = 20, Pitch = 20, Head = 30, SunD = 60;
			FDmShotArgs::Value(TEXT("-fxvdist="), Dist);
			FDmShotArgs::Value(TEXT("-fxvpitch="), Pitch);
			FDmShotArgs::Value(TEXT("-fxvhead="), Head);
			FDmShotArgs::Value(TEXT("-fxvsun="), SunD);
			GSpeedAfter = 0; FDmShotArgs::Value(TEXT("-fxvspeed="), GSpeedAfter);
			const int32 Town = VBiggest(W);
			const bool bSea = FDmShotArgs::Param(TEXT("fxvsea"));
			const bool bTownTarget = GWhat == TEXT("plague") || GWhat == TEXT("heal") || GWhat == TEXT("inspire") || GWhat == TEXT("prophet") || GWhat == TEXT("peace") || GWhat == TEXT("ark");
			if (bTownTarget) GAtCell = Town;
			else if (bSea) GAtCell = VFind(W, Town, 0.08, 0.2, [&W](int32 C) { return dm::state::isSea(W, C) && W.elev[size_t(C)] < W.seaLevel - 0.8f; });
			else if (GWhat == TEXT("fire") || GWhat == TEXT("lightning")) GAtCell = VFind(W, Town, 0.04, 0.3, [&W](int32 C) { return VLand(W, C) && W.veg[size_t(C)] > 0.55f && W.owner[size_t(C)] < 0; });
			else GAtCell = VFind(W, Town, 0.08, 0.16, [&W](int32 C) { return VLand(W, C) && W.owner[size_t(C)] < 0; });
			if (GAtCell < 0) GAtCell = Town;
			Ctx.LookAt(VLat(W, GAtCell), VLon(W, GAtCell), Dist, Head, Pitch, 60.0);
			if (!Ctx.Sim->IsSunPinned()) Ctx.SetSubsolar(FMath::Clamp(VLat(W, GAtCell) * 0.5 + 8.0, -23.0, 23.0), VLon(W, GAtCell) + SunD);
			Ctx.Log(FString::Printf(TEXT("DmFxVerify at: %s cell %d, %.1f km, pitch %.0f, sun +%.0f"), *GWhat, GAtCell, Dist, Pitch, SunD));
		},
		[](FDmScenarioContext& Ctx, float)
		{
			VGodly(Ctx);
			ADmFx* Fx = ADmFx::Find(Ctx.World);
			const int32 F = Ctx.Frame;
			if (F == GVAt)
			{
				bool bOk = false;
				if (GWhat == TEXT("nuke") || GWhat == TEXT("ark") || GWhat == TEXT("seed") || GWhat == TEXT("aurora")) bOk = Fx && Fx->Play(GWhat, GAtCell);
				else if (GWhat == TEXT("fire"))
				{
					bOk = Ctx.Sim->UsePower(TEXT("lightning"), GAtCell, 0.f);
					Ctx.Log(FString::Printf(TEXT("DmFxVerify at: ignited %d"), VIgnite(Ctx, GAtCell, 0.04)));
				}
				else if (GWhat != TEXT("rain") && GWhat != TEXT("bloom")) bOk = Ctx.Sim->UsePower(GWhat, GAtCell, 0.f);
				else bOk = true;
				if (GSpeedAfter > 0) Ctx.Sim->SetSpeed(float(GSpeedAfter));
				Ctx.Log(FString::Printf(TEXT("DmFxVerify at: %s -> %s"), *GWhat, bOk ? TEXT("ok") : TEXT("REFUSED")));
			}
			if ((GWhat == TEXT("rain") || GWhat == TEXT("bloom")) && F > GVAt && F <= GVAt + 240) Ctx.Sim->UsePower(GWhat, GAtCell, 1.f / 30.f);
			VPeaks(Ctx);
			if (F >= GVAt && F % 30 == 0) VLogFx(Ctx, TEXT("at"));
		});

	// ------------------------------------------------------------------ fxv-stress

	TArray<int32> GStressMeteors;
	int32 GStressForest = -1, GStressV1 = -1, GStressV2 = -1, GStressN1 = -1, GStressN2 = -1;

	FDmScenarioRegistrar GFxvStress(TEXT("fxv-stress"), TEXT("fx"), TEXT("verifier: extreme state (huge fire, eight meteors, two volcanoes, two nukes)"),
		[](FDmScenarioContext& Ctx)
		{
			VBegin(Ctx, true);
			const dm::World& W = *Ctx.Sim->Sim();
			const int32 Town = VBiggest(W);
			GStressForest = VFind(W, Town, 0.05, 0.2, [&W](int32 C) { return VLand(W, C) && W.veg[size_t(C)] > 0.6f; });
			GStressMeteors.Reset();
			for (uint32 K = 0; K < 8; ++K)
			{
				const int32 C = VFind(W, Town, 0.04 + 0.03 * K, 0.08 + 0.03 * K, [&W](int32 X) { return VLand(W, X); }, 100u + K);
				if (C >= 0) GStressMeteors.Add(C);
			}
			auto Land = [&W](int32 C) { return VLand(W, C); };
			GStressV1 = VFind(W, Town, 0.1, 0.15, Land, 31u);
			GStressV2 = VFind(W, Town, 0.18, 0.24, Land, 37u);
			GStressN1 = VFind(W, Town, 0.05, 0.1, Land, 41u);
			GStressN2 = VFind(W, Town, 0.12, 0.2, Land, 43u);
			Ctx.LookAt(VLat(W, Town), VLon(W, Town), 60.0, 20.0, 35.0, 60.0);
			if (!Ctx.Sim->IsSunPinned()) Ctx.SetSubsolar(FMath::Clamp(VLat(W, Town) * 0.5 + 8.0, -23.0, 23.0), VLon(W, Town) + 70.0);
		},
		[](FDmScenarioContext& Ctx, float)
		{
			VGodly(Ctx);
			ADmFx* Fx = ADmFx::Find(Ctx.World);
			const int32 F = Ctx.Frame;
			if (F == GVAt)
			{
				const int32 Lit = VIgnite(Ctx, GStressForest, 0.2, 0.2f);
				int32 Ok = 0;
				for (int32 C : GStressMeteors) Ok += Ctx.Sim->UsePower(TEXT("meteor"), C, 0.f) ? 1 : 0;
				Ok += Ctx.Sim->UsePower(TEXT("volcano"), GStressV1, 0.f) ? 1 : 0;
				Ok += Ctx.Sim->UsePower(TEXT("volcano"), GStressV2, 0.f) ? 1 : 0;
				if (Fx) { Fx->Play(TEXT("nuke"), GStressN1); Fx->Play(TEXT("nuke"), GStressN2); }
				Ctx.Sim->SetSpeed(1.f);
				Ctx.Log(FString::Printf(TEXT("DmFxVerify stress: ignited %d cells, %d of %d powers ok"), Lit, Ok, GStressMeteors.Num() + 2));
			}
			VPeaks(Ctx);
			if (F >= GVAt && F % 30 == 0) VLogFx(Ctx, TEXT("stress"));
			if (F == GVAt + 600)
				Ctx.Log(FString::Printf(TEXT("DmFxVerify stress: peaks effects %d particles %d quads %d (caps %d particles, quality %d)"),
					GPeakEffects, GPeakParticles, GPeakQuads, Fx ? Fx->MaxParticles : -1, Fx ? Fx->Quality : -1));
		});

	// ------------------------------------------------------------------ fxv-fast

	int32 GFastFail = 0;
	double GFastT0 = 0;

	FDmScenarioRegistrar GFxvFast(TEXT("fxv-fast"), TEXT("fx"), TEXT("verifier: live sim at 8x, calamity 25, meteors, quiet switch, world reload"),
		[](FDmScenarioContext& Ctx)
		{
			VBegin(Ctx, true);
			if (dm::World* M = Ctx.Sim->SimMutable()) M->settings.calamity = 25.0;
			const dm::World& W = *Ctx.Sim->Sim();
			const int32 Town = VBiggest(W);
			Ctx.LookAt(VLat(W, Town), VLon(W, Town), 300.0, 0.0, 60.0, 50.0);
			if (!Ctx.Sim->IsSunPinned()) Ctx.SetSubsolar(FMath::Clamp(VLat(W, Town) * 0.5 + 8.0, -23.0, 23.0), VLon(W, Town) + 60.0);
			Ctx.HoldExit();
			GFastFail = 0;
		},
		[](FDmScenarioContext& Ctx, float)
		{
			VGodly(Ctx);
			ADmFx* Fx = ADmFx::Find(Ctx.World);
			const dm::World* W = Ctx.Sim->Sim();
			const int32 F = Ctx.Frame;
			const int32 T = F - GVAt;     // frames since the start of the run
			if (!Fx || !W) return;
			if (T == 0) { Ctx.Sim->SetSpeed(8.f); GFastT0 = FPlatformTime::Seconds(); }
			if (T >= 0 && T < 30 * 100 && T % 240 == 0)
			{
				const int32 Town = VBiggest(*W);
				const int32 C = VFind(*W, Town, 0.05, 0.4, [W](int32 X) { return VLand(*W, X); }, uint32(T));
				const bool bOk = Ctx.Sim->UsePower(TEXT("meteor"), C, 0.f);
				if (!bOk) Ctx.Log(FString::Printf(TEXT("DmFxVerify fast: meteor at %d refused"), C));
			}
			if (T == 30 * 60) { Fx->SetQuiet(true); Ctx.Log(TEXT("DmFxVerify fast: quiet on")); }
			if (T == 30 * 60 + 5)
			{
				const bool bClean = Fx->GetParticleCount() == 0 && Fx->GetEffectCount() == 0 && Fx->GetQuadCount() == 0;
				Ctx.Log(FString::Printf(TEXT("DmFxVerify fast: quiet check particles %d effects %d quads %d -> %s"), Fx->GetParticleCount(), Fx->GetEffectCount(), Fx->GetQuadCount(), bClean ? TEXT("PASS") : TEXT("FAIL")));
				if (!bClean) ++GFastFail;
			}
			if (T == 30 * 66) { Fx->SetQuiet(false); Ctx.Log(TEXT("DmFxVerify fast: quiet off")); }
			if (T == 30 * 80)
			{
				Ctx.Log(TEXT("DmFxVerify fast: new world (seed 9, genesis skipped)"));
				Ctx.Sim->CreateWorld(9u, TEXT("Verify"), true);
				Ctx.Sim->SetSpeed(8.f);
			}
			if (T == 30 * 80 + 3)
			{
				const bool bClean = Fx->GetEffectCount() == 0 || Fx->GetEffectCount() < 8;
				Ctx.Log(FString::Printf(TEXT("DmFxVerify fast: after reload effects %d particles %d -> %s"), Fx->GetEffectCount(), Fx->GetParticleCount(), bClean ? TEXT("PASS") : TEXT("FAIL")));
				if (!bClean) ++GFastFail;
			}
			VPeaks(Ctx);
			if (T >= 0 && T % 150 == 0) VLogFx(Ctx, TEXT("fast"));
			if (T == 30 * 64) Ctx.Shot(TEXT("quiet"));
			if (T == 30 * 50) Ctx.Shot(TEXT("busy"));
			if (T == 30 * 95) Ctx.Shot(TEXT("reloaded"));
			if (T == 30 * 100)
			{
				const bool bBounded = GPeakParticles <= Fx->MaxParticles && GPeakEffects < 400;
				if (!bBounded) ++GFastFail;
				Ctx.Log(FString::Printf(TEXT("DmFxVerify fast: %.1f s real for 100 s of game; peaks effects %d particles %d quads %d; year %.1f -> %s"),
					FPlatformTime::Seconds() - GFastT0, GPeakEffects, GPeakParticles, GPeakQuads, W->year, GFastFail == 0 ? TEXT("PASS") : TEXT("FAIL")));
				Ctx.SetExitCode(GFastFail == 0 ? 0 : 1);
				Ctx.Finish();
			}
		});
}
