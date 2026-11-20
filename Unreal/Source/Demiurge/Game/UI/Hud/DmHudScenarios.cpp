// DEMIURGE — Game/UI/Hud/DmHudScenarios.cpp : HUD scenarios; every state of old SPEC-UI 3, 4 and 4b as a
// screenshot WITH the Slate UI (the harness's own -shot captures the scene only). Owner: C7 hud.
//
// Run with -noshot (each scenario takes its own shots and finishes):
//   shot.ps1 hud-icons -noshot          the whole icon set (old SPEC-UI 3) at 48 and 20 px
//   shot.ps1 hud-play -noshot           history on the fixture: overview, armed brush + hover tip, locked and
//                                       poor power tips, inspector, chronicle book, overlays + legends, toasts,
//                                       banner, hint card, Ark bar, settlement tip, unlock pulse, low view,
//                                       hidden HUD (eye)          -> shots/ue-hud-play-<state>.png
//   shot.ps1 hud-genesis -noshot -seed=3     molten, water (ocean %), life, people, people lost
//   shot.ps1 hud-narrow -noshot -res=390x844 phone layout: top bar rows, tabs + one power row, sheets, ticker
//   shot.ps1 hud-720 -noshot -res=1280x720    the wide layout at 1280 x 720
// Logs: "DmHudShot <path>" per shot, "DmHud: model ..." timing lines, "DmHudCheck ..." facts.

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "UnrealClient.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SLeafWidget.h"

#include "Game/Core/DmCoords.h"
#include "Game/Core/DmGameMode.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmScene.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Game/UI/Hud/DmHudActor.h"
#include "Game/UI/Hud/DmHudIcons.h"
#include "Game/UI/Hud/DmHudModel.h"
#include "Game/UI/Hud/DmHudStyle.h"

#include "Sim/Civ.h"
#include "Sim/Powers.h"
#include "Sim/Sim.h"
#include "Sim/Society.h"

namespace
{
	FString U(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

	/** Screenshot WITH the Slate UI, written opaque (the engine's own UI screenshot keeps the back buffer's
	 *  alpha, which makes the PNG transparent): capture through UGameViewportClient::OnScreenshotCaptured. */
	void RequestUiShot(const FString& Path)
	{
		static FDelegateHandle Handle;
		static FString Target;
		Target = Path;
		if (Handle.IsValid()) UGameViewportClient::OnScreenshotCaptured().Remove(Handle);
		Handle = UGameViewportClient::OnScreenshotCaptured().AddLambda([](int32 W, int32 H, const TArray<FColor>& Bitmap)
		{
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(W, H, TArrayView64<const FColor>(Bitmap.GetData(), Bitmap.Num()), Png);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Target), true);
			FFileHelper::SaveArrayToFile(Png, *Target);
			UGameViewportClient::OnScreenshotCaptured().Remove(Handle);
			Handle.Reset();
		});
		FScreenshotRequest::RequestScreenshot(Path, true, false);
	}

	struct FHudStep
	{
		int32 Wait = 20;                       // frames after Do before the shot
		TFunction<void(FDmScenarioContext&, ADmHud&)> Do;
		FString Shot;                          // suffix; empty = no shot
	};

	/** Runs steps one after another; each shot includes the UI and is awaited before the next step. */
	class FHudRun
	{
	public:
		TArray<FHudStep> Steps;
		int32 StartFrame = 240;

		void Add(int32 Wait, const FString& Shot, TFunction<void(FDmScenarioContext&, ADmHud&)> Do) { Steps.Add({ Wait, MoveTemp(Do), Shot }); }

		void Tick(FDmScenarioContext& Ctx)
		{
			if (bDone || Ctx.Frame < StartFrame) return;
			ADmHud* H = ADmHud::Find(Ctx.World);
			if (!H) { if (Ctx.Frame > StartFrame + 60) { Ctx.Log(TEXT("DmHudCheck FAIL no HUD actor")); Ctx.SetExitCode(1); Ctx.Finish(); bDone = true; } return; }
			if (!Pending.IsEmpty())
			{
				if (IFileManager::Get().FileSize(*Pending) > 0 && !FScreenshotRequest::IsScreenshotRequested())
				{
					Ctx.Log(FString::Printf(TEXT("DmHudShot %s"), *Pending));
					Pending.Reset();
					++Index;
					bStarted = false;
				}
				else if (Ctx.Frame - ShotFrame > 400) { Ctx.Log(FString::Printf(TEXT("DmHudShot FAILED %s"), *Pending)); Pending.Reset(); ++Index; bStarted = false; }
				return;
			}
			if (Index >= Steps.Num()) { bDone = true; Ctx.Finish(); return; }
			FHudStep& S = Steps[Index];
			if (!bStarted)
			{
				if (S.Do) S.Do(Ctx, *H);
				H->RefreshNow();
				bStarted = true;
				Due = Ctx.Frame + FMath::Max(2, S.Wait);
				return;
			}
			if (Ctx.Frame < Due) return;
			if (S.Shot.IsEmpty()) { ++Index; bStarted = false; return; }
			const FString Base = Ctx.Mode->ShotBase();
			FString Name = FPaths::GetBaseFilename(Base);
			Pending = FPaths::Combine(FPaths::GetPath(Base), Name + TEXT("-") + S.Shot + TEXT(".png"));
			IFileManager::Get().Delete(*Pending, false, true, true);
			RequestUiShot(Pending);
			ShotFrame = Ctx.Frame;
		}

	private:
		int32 Index = 0;
		bool bStarted = false, bDone = false;
		int32 Due = 0, ShotFrame = 0;
		FString Pending;
	};

	int32 FindSettlement(const dm::World& W, TFunction<bool(const dm::Settlement&)> Pred)
	{
		int32 Best = -1;
		for (const dm::Settlement& S : W.settlements)
			if (!S.dead && Pred(S) && (Best < 0 || S.pop > W.settlements[size_t(Best)].pop)) Best = S.id;
		return Best;
	}

	void CellLatLon(const dm::World& W, int32 Cell, double& Lat, double& Lon)
	{
		Lat = FMath::RadiansToDegrees(double(W.geo->lat[size_t(Cell)]));
		Lon = FMath::RadiansToDegrees(double(W.geo->lon[size_t(Cell)]));
	}

	/** Population-weighted centre of the settlements (as the bridge's orbit scenario). */
	void SettledCentre(const dm::World* W, double& OutLat, double& OutLon)
	{
		OutLat = 20.0; OutLon = 30.0;
		if (!W) return;
		FVector Sum = FVector::ZeroVector;
		for (const dm::Settlement& S : W->settlements) if (!S.dead) Sum += DmCoords::CellDir(*W, S.cell) * FMath::Sqrt(FMath::Max(S.pop, 1.0));
		if (Sum.IsNearlyZero()) return;
		DmCoords::DirToLatLon(Sum.GetSafeNormal(), OutLat, OutLon);
		OutLat = FMath::Clamp(OutLat, -55.0, 55.0);
	}

	/** A land cell visible near the screen centre: walk from the view centre. */
	int32 LandNearScreen(FDmScenarioContext& Ctx, const FVector2D& Px, FVector2D& OutPx)
	{
		const dm::World* W = Ctx.Sim->Sim();
		APlayerController* PC = Ctx.World->GetFirstPlayerController();
		if (!W || !PC) return -1;
		for (int32 R = 0; R < 400; R += 12)
			for (int32 K = 0; K < 16; ++K)
			{
				const double A = 2.0 * PI * K / 16.0;
				const FVector2D P = Px + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R;
				FDmPick Pk;
				if (Ctx.Sim->PickScreen(PC, P, Pk) && Pk.Cell >= 0 && !Pk.bSea && dm::civ::at(*W, Pk.Cell) < 0) { OutPx = P; return Pk.Cell; }
			}
		return -1;
	}

	void HoverCell(FDmScenarioContext& Ctx, ADmHud& H, int32 Cell)
	{
		FVector2D Px(960, 540);
		Ctx.Sim->ProjectCell(Ctx.World->GetFirstPlayerController(), Cell, Px);
		H.Context().Control.ForceHover(Cell, Px);
	}

	FDmHudContext& HC(ADmHud& H) { return H.Context(); }

	// ------------------------------------------------------------------ the icon sheet

	class SDmIconSheet : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SDmIconSheet) {}
		SLATE_END_ARGS()
		void Construct(const FArguments&) {}
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920, 1080); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
		{
			const FDmHudStyle& S = FDmHudStyle::Get();
			FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(), &S.PanelSolid, ESlateDrawEffect::None, S.PanelSolid.TintColor.GetSpecifiedColor());
			const TArray<FName> Names = DmIcons::Names();
			const FSlateFontInfo Font = S.Font(EDmFont::Sans, 12.f);
			const FSlateFontInfo Title = S.Font(EDmFont::Serif, 26.f);
			FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(FVector2f(900, 40), FSlateLayoutTransform(FVector2f(40, 24))), FString::Printf(TEXT("DEMIURGE icon set: %d icons (48 px, and 20 px)"), Names.Num()), Title, ESlateDrawEffect::None, S.Gold);
			const int32 Cols = 14;
			for (int32 I = 0; I < Names.Num(); ++I)
			{
				const float X = 40.f + float(I % Cols) * 132.f, Y = 84.f + float(I / Cols) * 128.f;
				DmIcons::Paint(Out, Layer + 1, G, Names[I], FVector2f(X + 30, Y), 48.f, S.Text);
				DmIcons::Paint(Out, Layer + 1, G, Names[I], FVector2f(X + 92, Y + 28), 20.f, S.Gold);
				FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(FVector2f(130, 18), FSlateLayoutTransform(FVector2f(X + 4, Y + 56))), Names[I].ToString(), Font, ESlateDrawEffect::None, S.Dim);
			}
			return Layer + 1;
		}
	};
}

// ================================================================== hud-icons

static FDmScenarioRegistrar GDmHudIcons(TEXT("hud-icons"), TEXT("hud"), TEXT("the HUD icon set on one sheet"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.HoldExit();
		static TSharedPtr<SWidget> Sheet;
		Sheet = SNew(SDmIconSheet);
		if (GEngine && GEngine->GameViewport) GEngine->GameViewport->AddViewportWidgetContent(Sheet.ToSharedRef(), 50);
		static FHudRun Run;
		Run = FHudRun();
		Run.StartFrame = 30;
		Run.Add(10, TEXT("sheet"), nullptr);
	},
	[](FDmScenarioContext& Ctx, float)
	{
		// ADmHud is not needed: run the steps with a stand-in tick
		static bool bShot = false;
		static FString Pending;
		if (Ctx.Frame == 40 && !bShot)
		{
			const FString Base = Ctx.Mode->ShotBase();
			Pending = FPaths::Combine(FPaths::GetPath(Base), FPaths::GetBaseFilename(Base) + TEXT("-sheet.png"));
			IFileManager::Get().Delete(*Pending, false, true, true);
			RequestUiShot(Pending);
			bShot = true;
		}
		if (bShot && !Pending.IsEmpty() && IFileManager::Get().FileSize(*Pending) > 0 && !FScreenshotRequest::IsScreenshotRequested())
		{
			Ctx.Log(FString::Printf(TEXT("DmHudShot %s"), *Pending));
			Pending.Reset();
			Ctx.Finish();
		}
		if (Ctx.Frame > 600) Ctx.Finish();
	});

// ================================================================== hud-play

static FHudRun GPlay;

static FDmScenarioRegistrar GDmHudPlay(TEXT("hud-play"), TEXT("hud"), TEXT("every HUD and panel state in history on the fixture"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene(uint32(EDmPartKind::World) | uint32(EDmPartKind::Ui));
		Ctx.HoldExit();
		double Lat, Lon;
		SettledCentre(Ctx.Sim->Sim(), Lat, Lon);
		Ctx.LookAt(Lat, Lon, 700.0, 0.0, 90.0, 30.0);
		Ctx.SetSubsolar(FMath::Clamp(Lat * 0.5 + 8.0, -23.0, 23.0), Lon + 55.0);
		GPlay = FHudRun();
		GPlay.StartFrame = 240;
		FHudRun& R = GPlay;

		R.Add(10, TEXT(""), [](FDmScenarioContext& C, ADmHud& H)
		{
			HC(H).ResetHints();
			HC(H).bHintsEnabled = false;
			if (dm::World* W = C.Sim->SimMutable()) { W->god.awe = 412; W->god.aweMax = 500; }
			C.Log(FString::Printf(TEXT("DmHudCheck control: %s"), *HC(H).Control.Describe()));
		});
		R.Add(30, TEXT("overview"), nullptr);
		// armed brush power with the hover tip and the brush chip
		R.Add(30, TEXT("armed-rain"), [](FDmScenarioContext& C, ADmHud& H)
		{
			HC(H).Control.ArmPower(TEXT("rain"));
			FVector2D Px;
			const int32 Cell = LandNearScreen(C, FVector2D(1000, 520), Px);
			HC(H).Control.ForceHover(Cell, Px);
			C.Log(FString::Printf(TEXT("DmHudCheck armed '%s' hover cell %d"), *HC(H).Control.Armed(), Cell));
		});
		// a click power on a bad target (lightning is fine anywhere; plague needs a settlement)
		R.Add(30, TEXT("armed-plague-invalid"), [](FDmScenarioContext& C, ADmHud& H)
		{
			HC(H).Control.ArmPower(TEXT("plague"));
			if (HC(H).Control.Armed() != TEXT("plague")) HC(H).Control.ArmPower(TEXT("lightning"));
		});
		// locked power tip
		R.Add(20, TEXT("tip-locked"), [](FDmScenarioContext& C, ADmHud& H)
		{
			HC(H).Control.Disarm();
			HC(H).Control.ClearForcedHover();
			HC(H).HoverPower = TEXT("volcano");
		});
		// poor: little Awe left
		R.Add(20, TEXT("tip-poor"), [](FDmScenarioContext& C, ADmHud& H)
		{
			if (dm::World* W = C.Sim->SimMutable()) W->god.awe = 38;
			HC(H).HoverPower = TEXT("meteor");
		});
		// the inspector on the largest settlement of a culture at war
		R.Add(40, TEXT("inspector"), [](FDmScenarioContext& C, ADmHud& H)
		{
			HC(H).HoverPower.Reset();
			dm::World* W = C.Sim->SimMutable();
			if (!W) return;
			W->god.awe = 412;
			int32 WarCulture = -1;
			for (const auto& KV : W->vars.rel) if (KV.second.war) { WarCulture = KV.first.first; break; }
			int32 Sid = FindSettlement(*W, [WarCulture](const dm::Settlement& S) { return WarCulture < 0 || S.culture == WarCulture; });
			if (Sid >= 0)
			{
				dm::Settlement& S = W->settlements[size_t(Sid)];
				if (!S.prayer) S.prayer = dm::Prayer{ dm::PrayerKind::Rain, W->year - 3 };
				S.blessed = 12;
				dm::Culture& Cu = W->cultures[size_t(S.culture)];
				if (!Cu.prophet) Cu.prophet = dm::Prophet{ "Ashet the Seer", Sid, W->year + 40 };
				Cu.edict = dm::Edict::Build;
			}
			HC(H).Control.Select(Sid);
			C.Log(FString::Printf(TEXT("DmHudCheck inspector settlement %d"), Sid));
		});
		// the chronicle book
		R.Add(30, TEXT("chronicle"), [](FDmScenarioContext& C, ADmHud& H)
		{
			HC(H).Control.Select(-1);
			HC(H).bChronicleOpen = true;
		});
		R.Add(30, TEXT("chronicle-great"), [](FDmScenarioContext& C, ADmHud& H) { HC(H).BookWeight = 2; });
		// overlays and legends
		R.Add(30, TEXT("overlay-cultures"), [](FDmScenarioContext& C, ADmHud& H)
		{
			HC(H).bChronicleOpen = false;
			HC(H).BookWeight = 1;
			HC(H).Control.SetOverlay(1);
		});
		R.Add(30, TEXT("overlay-temperature"), [](FDmScenarioContext& C, ADmHud& H) { HC(H).Control.SetOverlay(2); });
		// toasts and a banner
		R.Add(50, TEXT("toasts-banner"), [](FDmScenarioContext& C, ADmHud& H)
		{
			HC(H).Control.SetOverlay(0);
			FDmHudContext& X = HC(H);
			X.Now = H.HudTime();
			X.PushToast(TEXT("Not enough Awe"), TEXT("bad"), 30.0);
			X.PushToast(TEXT("Bloom wakes in your hands."), TEXT("good"), 30.0);
			X.PushToast(TEXT("The Iron Age begins"), TEXT("epoch"), 30.0);
			X.PushToast(TEXT("Velu's prayer is answered"), TEXT("good"), 30.0);
			const dm::World* W = C.Sim->Sim();
			dm::ChronicleEntry E;
			E.type = "wonder"; E.weight = 3; E.year = W ? W->year : 0;
			E.text = "In Velu the people raised a great tower of white stone, that the god might see them from the sky.";
			if (W) for (const dm::ChronicleEntry& Ce : W->chronicle) if (Ce.weight >= 3) E = Ce;
			X.ShowBanner(E, W);
			X.Banner.Life = 60.0;
		});
		// the first-steps hint card and the Ark
		R.Add(40, TEXT("hint-ark"), [](FDmScenarioContext& C, ADmHud& H)
		{
			FDmHudContext& X = HC(H);
			X.Toasts.Reset();
			X.Banner.Born = -100;
			X.bHintsEnabled = true;
			X.ResetHints();
			X.HintsSeen.Add(0); X.HintsSeen.Add(1);   // the state hints (awake, Awe) would come first in teaching order
			X.TriggerHint(4);
			X.HintQueueSince = X.Now - X.HintGather;   // no gathering wait: the hint shows on the next tick
			dm::World* W = C.Sim->SimMutable();
			if (!W) return;
			const int32 Sid = FindSettlement(*W, [](const dm::Settlement& S) { return S.capital; });
			if (Sid >= 0)
			{
				W->settlements[size_t(Sid)].ark = 0.42;
				W->vars.ark = dm::ArkRecord{ W->settlements[size_t(Sid)].culture, Sid, W->year };
			}
		});
		// the hover tip on a settlement (no power), the unlock pulse
		R.Add(14, TEXT("tip-settlement-pulse"), [](FDmScenarioContext& C, ADmHud& H)
		{
			FDmHudContext& X = HC(H);
			X.DismissHint();
			X.bHintsEnabled = false;
			X.HintQueue.Reset();
			X.HintShown = -1;
			if (dm::World* W = C.Sim->SimMutable()) W->vars.ark.reset();
			const dm::World* W = C.Sim->Sim();
			const int32 Sid = W ? FindSettlement(*W, [](const dm::Settlement& S) { return S.tier >= 3; }) : -1;
			if (Sid >= 0) HoverCell(C, H, W->settlements[size_t(Sid)].cell);
			X.Now = H.HudTime();
			for (FDmHudPower& P : X.M.Powers) if (P.Id == TEXT("bloom")) P.PulseStart = X.Now - 0.25;
		});
		// close: labels of towns and villages, a hover tip over land
		R.Add(90, TEXT("low"), [](FDmScenarioContext& C, ADmHud& H)
		{
			HC(H).Control.ClearForcedHover();
			const dm::World* W = C.Sim->Sim();
			const int32 Sid = W ? FindSettlement(*W, [](const dm::Settlement& S) { return S.capital; }) : -1;
			if (Sid < 0) return;
			double Lat, Lon;
			CellLatLon(*W, W->settlements[size_t(Sid)].cell, Lat, Lon);
			C.LookAt(Lat, Lon, 140.0, 20.0, 55.0, 40.0);
		});
		R.Add(20, TEXT("low-tip"), [](FDmScenarioContext& C, ADmHud& H)
		{
			FVector2D Px;
			const int32 Cell = LandNearScreen(C, FVector2D(760, 620), Px);
			HC(H).Control.ForceHover(Cell, Px);
		});
		// hidden HUD: only the eye remains
		R.Add(20, TEXT("hidden"), [](FDmScenarioContext& C, ADmHud& H)
		{
			HC(H).Control.ClearForcedHover();
			HC(H).Control.HideHud(true);
		});
		R.Add(4, TEXT(""), [](FDmScenarioContext& C, ADmHud& H)
		{
			HC(H).Control.HideHud(false);
			FDmHudContext& X = HC(H);
			C.Log(FString::Printf(TEXT("DmHudCheck model avg %.3f ms per refresh; labels %.3f ms per frame; %d labels; %d feed; %d now items"),
				X.LastModelMs, X.AvgLabelMs, X.Labels.Num(), X.Feed.Num(), X.M.Now.Num()));
		});
	},
	[](FDmScenarioContext& Ctx, float) { GPlay.Tick(Ctx); });

// ================================================================== hud-genesis

static FHudRun GGenesis;

static FDmScenarioRegistrar GDmHudGenesis(TEXT("hud-genesis"), TEXT("hud"), TEXT("genesis guide cards (run with -seed=3)"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene(uint32(EDmPartKind::World) | uint32(EDmPartKind::Ui));
		Ctx.HoldExit();
		Ctx.LookAt(10.0, 30.0, 700.0, 0.0, 90.0, 30.0);
		Ctx.SetSubsolar(10.0, 70.0);
		Ctx.Sim->SetSpeed(1.f);
		Ctx.Sim->SetPaused(false);
		GGenesis = FHudRun();
		GGenesis.StartFrame = 120;
		FHudRun& R = GGenesis;
		R.Add(10, TEXT("molten"), [](FDmScenarioContext& C, ADmHud& H)
		{
			const dm::World* W = C.Sim->Sim();
			C.Log(FString::Printf(TEXT("DmHudCheck phase %d heat %.2f"), W ? int32(W->phase) : -1, W ? W->heat : 0.0));
		});
		R.Add(10, TEXT("water"), [](FDmScenarioContext& C, ADmHud& H)
		{
			dm::World* W = C.Sim->SimMutable();
			if (!W) return;
			if (W->phase == dm::Phase::Molten) { W->heat = 0.0; dm::sim::setPhase(*W, dm::Phase::Water); }
			W->seaLevel = -2.2;
			dm::sim::rederive(*W);
			C.Sim->SetPaused(true);
		});
		R.Add(10, TEXT("water-armed"), [](FDmScenarioContext& C, ADmHud& H) { HC(H).Control.ArmPower(TEXT("comet")); });
		R.Add(10, TEXT("life"), [](FDmScenarioContext& C, ADmHud& H)
		{
			HC(H).Control.Disarm();
			dm::World* W = C.Sim->SimMutable();
			if (W) { const bool bOk = dm::sim::genesis::next(*W); C.Log(FString::Printf(TEXT("DmHudCheck water->life %d"), bOk ? 1 : 0)); }
		});
		R.Add(10, TEXT("people"), [](FDmScenarioContext& C, ADmHud& H)
		{
			dm::World* W = C.Sim->SimMutable();
			if (!W) return;
			for (int32 I = 0; I < W->n; ++I) if (!dm::state::isSea(*W, I) && W->veg[size_t(I)] < 0.5f) W->veg[size_t(I)] = 0.6f;
			dm::sim::rederive(*W);
			bool bOk = dm::sim::genesis::next(*W);
			if (!bOk) bOk = dm::sim::setPhase(*W, dm::Phase::People);   // the test world's cover may stay short of 25 %
			C.Log(FString::Printf(TEXT("DmHudCheck life->people %d (phase %d)"), bOk ? 1 : 0, int32(W->phase)));
			HC(H).Control.ArmPower(TEXT("awaken"));
		});
		R.Add(10, TEXT("people-lost"), [](FDmScenarioContext& C, ADmHud& H)
		{
			HC(H).Control.Disarm();
			dm::World* W = C.Sim->SimMutable();
			if (!W) return;
			dm::sim::genesis::SkipOpts O;
			O.sea = W->seaLevel;
			dm::sim::genesis::skip(*W, O);
			HC(H).M.bPeopleLost = true;
			W->bus.emit(dm::EvPeopleLost{});
		});
	},
	[](FDmScenarioContext& Ctx, float) { GGenesis.Tick(Ctx); });

// ================================================================== hud-narrow, hud-720

static FHudRun GNarrow;

static void NarrowSetup(FDmScenarioContext& Ctx, bool bPhone)
{
	Ctx.BuildScene(uint32(EDmPartKind::World) | uint32(EDmPartKind::Ui));
	Ctx.HoldExit();
	double Lat, Lon;
	SettledCentre(Ctx.Sim->Sim(), Lat, Lon);
	Ctx.LookAt(Lat, Lon, bPhone ? 1500.0 : 700.0, 0.0, 90.0, 30.0);
	Ctx.SetSubsolar(FMath::Clamp(Lat * 0.5 + 8.0, -23.0, 23.0), Lon + 55.0);
	GNarrow = FHudRun();
	GNarrow.StartFrame = 200;
	FHudRun& R = GNarrow;
	R.Add(20, TEXT("overview"), [](FDmScenarioContext& C, ADmHud& H)
	{
		HC(H).ResetHints();
		HC(H).bHintsEnabled = false;
		if (dm::World* W = C.Sim->SimMutable()) { W->god.awe = 412; W->god.aweMax = 500; }
		C.Log(FString::Printf(TEXT("DmHudCheck viewport %.0f x %.0f, scale %.3f, narrow %d"), HC(H).ViewportPx.X, HC(H).ViewportPx.Y, HC(H).UiScale, HC(H).bNarrow ? 1 : 0));
	});
	R.Add(20, TEXT("wrath-armed"), [](FDmScenarioContext& C, ADmHud& H)
	{
		HC(H).NarrowGroup = int32(dm::powers::Group::Wrath);
		HC(H).Control.ArmPower(TEXT("lightning"));
	});
	R.Add(40, TEXT("inspector"), [](FDmScenarioContext& C, ADmHud& H)
	{
		HC(H).Control.Disarm();
		const dm::World* W = C.Sim->Sim();
		const int32 Sid = W ? FindSettlement(*W, [](const dm::Settlement& S) { return S.capital; }) : -1;
		HC(H).Control.Select(Sid);
		if (HC(H).bNarrow) HC(H).NarrowSheet = 1;
	});
	R.Add(30, TEXT("chronicle"), [](FDmScenarioContext& C, ADmHud& H)
	{
		HC(H).Control.Select(-1);
		if (HC(H).bNarrow) HC(H).NarrowSheet = 2; else HC(H).bChronicleOpen = true;
	});
	R.Add(30, TEXT("overlays"), [](FDmScenarioContext& C, ADmHud& H)
	{
		HC(H).bChronicleOpen = false;
		HC(H).Control.SetOverlay(1);
		if (HC(H).bNarrow) HC(H).NarrowSheet = 3;
	});
}

static FDmScenarioRegistrar GDmHudNarrow(TEXT("hud-narrow"), TEXT("hud"), TEXT("phone layout (run with -res=390x844)"),
	[](FDmScenarioContext& Ctx) { NarrowSetup(Ctx, true); },
	[](FDmScenarioContext& Ctx, float) { GNarrow.Tick(Ctx); });

static FDmScenarioRegistrar GDmHud720(TEXT("hud-720"), TEXT("hud"), TEXT("wide layout at 1280 x 720 (run with -res=1280x720)"),
	[](FDmScenarioContext& Ctx) { NarrowSetup(Ctx, false); },
	[](FDmScenarioContext& Ctx, float) { GNarrow.Tick(Ctx); });

// ================================================================== hud-full: every part (camera pawn, screens, audio) with the HUD

static FHudRun GFull;

static FDmScenarioRegistrar GDmHudFull(TEXT("hud-full"), TEXT("hud"), TEXT("the HUD among every registered part (pawn, screens), as in play"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene(DmPartsAll);
		Ctx.HoldExit();
		double Lat, Lon;
		SettledCentre(Ctx.Sim->Sim(), Lat, Lon);
		Ctx.LookAt(Lat, Lon, 700.0, 0.0, 90.0, 30.0);
		Ctx.SetSubsolar(FMath::Clamp(Lat * 0.5 + 8.0, -23.0, 23.0), Lon + 55.0);
		GFull = FHudRun();
		GFull.StartFrame = 240;
		GFull.Add(20, TEXT("frame"), [](FDmScenarioContext& C, ADmHud& H)
		{
			C.Log(FString::Printf(TEXT("DmHudCheck full: control %s, parts %s"), *HC(H).Control.Describe(), *FDmScene::Describe()));
		});
	},
	[](FDmScenarioContext& Ctx, float) { GFull.Tick(Ctx); });

// ================================================================== hud-world: a second world restarts the feed and the unlocks (regression)
// Run with -noshot: exit 0 = PASS. The fixture's chronicle ids restart in every world; a stale per-world
// state (last id seen, unlocks) used to hide the new world's entries from the feed.

static FHudRun GWorldRun;

static FDmScenarioRegistrar GDmHudWorld(TEXT("hud-world"), TEXT("hud"), TEXT("a second world shows its own feed (regression; run with -noshot)"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene(uint32(EDmPartKind::World) | uint32(EDmPartKind::Ui));
		Ctx.HoldExit();
		double Lat, Lon;
		SettledCentre(Ctx.Sim->Sim(), Lat, Lon);
		Ctx.LookAt(Lat, Lon, 700.0, 0.0, 90.0, 30.0);
		GWorldRun = FHudRun();
		GWorldRun.StartFrame = 240;
		GWorldRun.Add(20, FString(), [](FDmScenarioContext& C, ADmHud& H)
		{
			C.Log(FString::Printf(TEXT("DmHudCheck world1 feed %d"), HC(H).Feed.Num()));
			HC(H).Feed.Reset();   // the test starts the feed from nothing, so only the new world can fill it
			C.Sim->LoadFixture(TEXT("planet_7"));
		});
		GWorldRun.Add(40, FString(), [](FDmScenarioContext& C, ADmHud& H)
		{
			const int32 N = HC(H).Feed.Num();
			const bool bOk = N == 4;
			C.Log(FString::Printf(TEXT("DmHudCheck world2 feed %d (expect 4, the last entries of the new history) %s"), N, bOk ? TEXT("PASS") : TEXT("FAIL")));
			if (!bOk) C.SetExitCode(1);
		});
	},
	[](FDmScenarioContext& Ctx, float) { GWorldRun.Tick(Ctx); });

// ================================================================== hud-worst: close to the ground on the night side, busy HUD (worst case)

static FHudRun GWorst;
static int32 GWorstSid = -1;

static FDmScenarioRegistrar GDmHudWorst(TEXT("hud-worst"), TEXT("hud"), TEXT("3 km over the night side with an armed brush, a toast flood and a selection"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene(uint32(EDmPartKind::World) | uint32(EDmPartKind::Ui));
		Ctx.HoldExit();
		// straight down on a town or bigger, on the night side
		double Lat = 20.0, Lon = 30.0;
		const dm::World* W = Ctx.Sim->Sim();
		GWorstSid = W ? FindSettlement(*W, [](const dm::Settlement& S) { return !S.dead && S.tier >= 2; }) : -1;
		if (W && GWorstSid >= 0) DmCoords::DirToLatLon(DmCoords::CellDir(*W, W->settlements[size_t(GWorstSid)].cell), Lat, Lon);
		Ctx.LookAt(Lat, Lon, 3.0, 0.0, 90.0, 40.0);
		Ctx.SetSubsolar(Lat, Lon + 180.0);
		GWorst = FHudRun();
		GWorst.StartFrame = 240;
		GWorst.Add(20, TEXT("worst-night"), [](FDmScenarioContext& C, ADmHud& H)
		{
			for (int32 I = 0; I < 6; ++I) HC(H).PushToast(FString::Printf(TEXT("Toast number %d for the flood test"), I), I % 2 ? TEXT("bad") : TEXT("good"));
			HC(H).Control.ArmPower(TEXT("rain"));
			FVector2D Px;
			const int32 Cell = LandNearScreen(C, FVector2D(960, 540), Px);
			HC(H).Control.ForceHover(Cell, Px);
			C.Log(FString::Printf(TEXT("DmHudCheck worst: labels %d, alt %.1f km, cell %d"), HC(H).Labels.Num(), HC(H).CameraAltitudeKm, Cell));
		});
		GWorst.Add(30, TEXT("worst-inspector"), [](FDmScenarioContext& C, ADmHud& H)
		{
			HC(H).Control.ClearForcedHover();
			HC(H).Control.Disarm();
			HC(H).Toasts.Reset();
			const int32 Sid = GWorstSid;
			if (Sid >= 0) HC(H).Control.Select(Sid);
			C.Log(FString::Printf(TEXT("DmHudCheck worst: selected %d, labels %d"), Sid, HC(H).Labels.Num()));
		});
	},
	[](FDmScenarioContext& Ctx, float) { GWorst.Tick(Ctx); });

// ================================================================== hud-clarity: wave 3 player-clarity elements (design review 8 and 12)
// Run with -noshot: shots/ue-hud-clarity-<state>.png, "DmHudCheck clarity ..." lines, exit 0 = every check PASS.
//   watched           the regard word before the god has done anything ("Watched"), its culture dot and the small
//                     remember map; the guidance thread's first step
//   remember-watched  the remember card while they have seen nothing; remember: after acts (the verdict, the map)
//   guide-progress    two steps ticked off (a click on a town, a rain), the next step; guide-folded: one line
//   inspector-why     famine, a prayer and the sea at the door explained; inspector-edicts: no prophet, edicts dimmed
//                     with how to wake them
//   toasts-merged     8 answered prayers, 2 lost towns, 2 woken powers and the rising sea as four toasts
//   hint-order        three hints at once show in teaching order; then they leave by themselves after 12 s

static FHudRun GClarity;
static int32 GClarityFails = 0;
static int32 GClarityNext = -1;

namespace
{
	void ClarityCheck(FDmScenarioContext& C, bool bOk, const FString& What)
	{
		C.Log(FString::Printf(TEXT("DmHudCheck clarity %s %s"), *What, bOk ? TEXT("PASS") : TEXT("FAIL")));
		if (!bOk) { ++GClarityFails; C.SetExitCode(GClarityFails); }
	}

	/** A culture's long memory of the god (Sim society::memory's vars): grace, wrath and when it last heard him. */
	void SetMemory(dm::World& W, int32 Culture, double Grace, double Wrath)
	{
		const std::string Id = std::to_string(Culture);
		W.vars.num["society_grace_" + Id] = Grace;
		W.vars.num["society_wrath_" + Id] = Wrath;
		W.vars.num["society_heard_" + Id] = W.year;
	}

	/** The HUD word must be the ending's own verdict for the culture it names (Sim checkEnding: memory().mood). */
	void CheckVerdict(FDmScenarioContext& C, const FDmHudModel& M, const FString& What)
	{
		const dm::World* W = C.Sim->Sim();
		const dm::society::Memory Mem = dm::society::memory(*W, M.RegardCulture);
		ClarityCheck(C, !M.bWatched && M.Mood == Mem.mood && M.RegardWord == FDmHudContext::RegardWordOf(Mem.mood),
			FString::Printf(TEXT("%s: HUD %s, the ending's memory verdict %s (verdict %.2f, devotion %.2f, grace %.2f, wrath %.2f%s%s)%s%s"), *What, *M.RegardWord,
				*FDmHudContext::RegardWordOf(Mem.mood), Mem.verdict, Mem.devotion, Mem.grace, Mem.wrath, Mem.mixed ? TEXT(", two-handed") : TEXT(""),
				Mem.silent > 0 ? *FString::Printf(TEXT(", silent %.0f y"), Mem.silent) : TEXT(""), M.RegardWhy.IsEmpty() ? TEXT("") : TEXT(" -- "), *M.RegardWhy));
	}
}

static FDmScenarioRegistrar GDmHudClarity(TEXT("hud-clarity"), TEXT("hud"), TEXT("wave 3 clarity: regard, remember map, guidance thread, inspector explanations, toast merging, hint order"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene(uint32(EDmPartKind::World) | uint32(EDmPartKind::Ui));
		Ctx.HoldExit();
		double Lat, Lon;
		SettledCentre(Ctx.Sim->Sim(), Lat, Lon);
		Ctx.LookAt(Lat, Lon, 700.0, 0.0, 90.0, 30.0);
		Ctx.SetSubsolar(FMath::Clamp(Lat * 0.5 + 8.0, -23.0, 23.0), Lon + 55.0);
		GClarity = FHudRun();
		GClarityFails = 0;
		GClarity.StartFrame = 240;
		FHudRun& R = GClarity;
		static double SeaBefore = 0.0;

		R.Add(10, TEXT(""), [](FDmScenarioContext& C, ADmHud& H)
		{
			FDmHudContext& X = HC(H);
			X.ResetHints();
			X.bHintsEnabled = false;
			X.ResetGuide();
			X.Control.Select(-1);
			if (dm::World* W = C.Sim->SimMutable())
			{
				W->god.awe = 412; W->god.aweMax = 500;
				W->god.mercy = 0; W->god.wrath = 0;   // nothing the people saw yet
				W->god.deeds.erase("rain");
				for (dm::Culture& Cu : W->cultures) { Cu.edict = dm::Edict::None; SetMemory(*W, Cu.id, 0.0, 0.0); }
			}
		});
		R.Add(30, TEXT("watched"), [](FDmScenarioContext& C, ADmHud& H)
		{
			const FDmHudModel& M = HC(H).M;
			ClarityCheck(C, M.bWatched && M.RegardWord == TEXT("Watched") && M.RegardCulture >= 0, FString::Printf(TEXT("watched word %s culture %d (%s)"), *M.RegardWord, M.RegardCulture, *M.RegardCultureName));
			ClarityCheck(C, HC(H).bGuideVisible && HC(H).GuideCurrent == 0, FString::Printf(TEXT("guide visible %d at step %d"), HC(H).bGuideVisible ? 1 : 0, HC(H).GuideCurrent));
		});
		R.Add(20, TEXT("remember-watched"), [](FDmScenarioContext& C, ADmHud& H) { HC(H).ToggleRemember(); });
		// (verifier) cruel, then two-handed: the word still follows the ending's rule; the card says why
		R.Add(20, TEXT("remember-feared"), [](FDmScenarioContext& C, ADmHud& H)
		{
			if (dm::World* W = C.Sim->SimMutable()) SetMemory(*W, HC(H).M.RegardCulture, 0.0, 6.0);
			H.RefreshNow();
			CheckVerdict(C, HC(H).M, TEXT("cruel memory"));
			ClarityCheck(C, HC(H).M.Mood == dm::Mood::Feared, FString::Printf(TEXT("cruel memory reads %s"), *HC(H).M.RegardWord));
		});
		R.Add(20, TEXT("remember-mixed"), [](FDmScenarioContext& C, ADmHud& H)
		{
			if (dm::World* W = C.Sim->SimMutable()) SetMemory(*W, HC(H).M.RegardCulture, 4.0, 3.0);
			H.RefreshNow();
			CheckVerdict(C, HC(H).M, TEXT("two-handed memory"));
			ClarityCheck(C, HC(H).M.Mood == dm::Mood::Parting && !HC(H).M.RegardWhy.IsEmpty(), FString::Printf(TEXT("two-handed reads %s with a reason"), *HC(H).M.RegardWord));
		});
		R.Add(20, TEXT("remember"), [](FDmScenarioContext& C, ADmHud& H)
		{
			// (verifier) god.mercy alone (rain over an empty sea) is not a deed the people saw: still Watched
			if (dm::World* W = C.Sim->SimMutable()) { W->god.mercy = 3.0; SetMemory(*W, HC(H).M.RegardCulture, 0.0, 0.0); }
			H.RefreshNow();
			ClarityCheck(C, HC(H).M.bWatched, FString::Printf(TEXT("mercy nobody saw: still %s"), *HC(H).M.RegardWord));
			// the asked culture remembers kind deeds: the word is the ending's own verdict
			if (dm::World* W = C.Sim->SimMutable()) SetMemory(*W, HC(H).M.RegardCulture, 3.0, 0.0);
			H.RefreshNow();
			const FDmHudModel& M = HC(H).M;
			ClarityCheck(C, !M.bWatched && M.RegardWord != TEXT("Watched") && M.RegardLine.StartsWith(TEXT("If the story ended now")),
				FString::Printf(TEXT("verdict %s: %s (verdict %.2f, devotion %.2f)"), *M.RegardWord, *M.RegardLine, M.RegardLove, M.RegardDevotion));
			CheckVerdict(C, M, TEXT("kind memory"));
		});
		// the thread: a click on a town (after a moment with none) and a rain tick two steps off
		R.Add(6, TEXT(""), [](FDmScenarioContext& C, ADmHud& H)
		{
			HC(H).bRememberOpen = false;
			const dm::World* W = C.Sim->Sim();
			HC(H).Control.Select(W ? FindSettlement(*W, [](const dm::Settlement& S) { return S.tier >= 1; }) : -1);
		});
		R.Add(6, TEXT(""), [](FDmScenarioContext& C, ADmHud& H)
		{
			HC(H).Control.Select(-1);
			if (dm::World* W = C.Sim->SimMutable()) W->god.deeds["rain"] = 1;
		});
		R.Add(20, TEXT("guide-progress"), [](FDmScenarioContext& C, ADmHud& H)
		{
			FDmHudContext& X = HC(H);
			ClarityCheck(C, (X.GuideDone & 7u) == 7u && X.GuideCurrent >= 3 && X.bGuideVisible,
				FString::Printf(TEXT("guide done mask 0x%x, step %d (%s), just done (%s)"), X.GuideDone, X.GuideCurrent, FDmHudContext::GuideStep(X.GuideCurrent).Title, *X.GuideJustDone));
		});
		R.Add(14, TEXT("guide-folded"), [](FDmScenarioContext& C, ADmHud& H) { HC(H).bGuideCollapsed = true; });
		// the inspector: a town in famine that prays for rain, by the sea, its people without a prophet
		R.Add(40, TEXT("inspector-why"), [](FDmScenarioContext& C, ADmHud& H)
		{
			HC(H).bGuideCollapsed = false;
			dm::World* W = C.Sim->SimMutable();
			if (!W) return;
			const int32 Sid = FindSettlement(*W, [W](const dm::Settlement& S) { return S.tier >= 1 && W->elev[size_t(S.cell)] - W->seaLevel < 0.15; });
			if (Sid < 0) { ClarityCheck(C, false, TEXT("no coastal town")); return; }
			dm::Settlement& S = W->settlements[size_t(Sid)];
			S.famine = 3;
			S.pop = FMath::Max(S.pop, S.K * 1.18);   // a real famine: more of them than the land can feed
			S.prayer = dm::Prayer{ dm::PrayerKind::Rain, W->year - 2 };
			dm::Culture& Cu = W->cultures[size_t(S.culture)];
			Cu.prophet.reset();
			HC(H).Control.Select(Sid);
			C.Log(FString::Printf(TEXT("DmHudCheck clarity inspector-why settlement %d"), Sid));
		});
		R.Add(40, TEXT("inspector-edicts"), [](FDmScenarioContext& C, ADmHud& H)
		{
			dm::World* W = C.Sim->SimMutable();
			if (!W) return;
			// the smallest live culture's capital (a short panel: the edicts are in view)
			int32 Best = -1;
			for (const dm::Culture& Cu : W->cultures)
				if (!Cu.dead && Cu.capital >= 0 && (Best < 0 || Cu.pop < W->cultures[size_t(Best)].pop)) Best = Cu.id;
			if (Best < 0) return;
			dm::Culture& Cu = W->cultures[size_t(Best)];
			Cu.prophet.reset();
			Cu.edict = dm::Edict::None;
			HC(H).Control.Select(Cu.capital);
		});
		// toast merging: what used to be a stack of "x 8", "x 7" rows
		R.Add(30, TEXT("toasts-merged"), [](FDmScenarioContext& C, ADmHud& H)
		{
			HC(H).Control.Select(-1);
			FDmHudContext& X = HC(H);
			X.Now = H.HudTime();
			X.Toasts.Reset();
			X.ToastGroups.Reset();
			X.ToastsPushed = X.ToastsCreated = 0;
			const dm::World* W = C.Sim->Sim();
			TArray<FString> Names;
			if (W) for (const dm::Settlement& S : W->settlements) if (!S.dead && Names.Num() < 7) Names.Add(U(S.name));
			for (const FString& N : Names) X.PushGrouped(TEXT("prayer"), N, TEXT("good"), 30.0);
			X.PushGrouped(TEXT("prayer"), Names.Num() ? Names[0] : FString(TEXT("Velu")), TEXT("good"), 30.0);   // the same town again
			X.PushGrouped(TEXT("lost"), TEXT("Ostravel"), TEXT("bad"), 30.0);
			X.PushGrouped(TEXT("lost"), TEXT("Hadmir"), TEXT("bad"), 30.0);
			X.PushGrouped(TEXT("wake"), TEXT("Bloom"), TEXT("good"), 30.0);
			X.PushGrouped(TEXT("wake"), TEXT("Inspire"), TEXT("good"), 30.0);
			// the sea rises 4 m above where history began: the HUD says why (once per mark)
			if (dm::World* Wm = C.Sim->SimMutable())
			{
				SeaBefore = Wm->seaLevel;
				if (!Wm->vars.seaBase) Wm->vars.seaBase = Wm->seaLevel;   // the fixture has no recorded start
				Wm->seaLevel = *Wm->vars.seaBase;
				X.SeaToldIndex = -1;
				H.RefreshNow();                       // baseline (marks already passed stay quiet)
				Wm->seaLevel += 0.02;
				H.RefreshNow();
			}
			for (FDmHudToast& T : X.Toasts) T.Life = FMath::Max(T.Life, 30.0);
			FString All;
			for (const FDmHudToast& T : X.Toasts) All += T.Text + TEXT(" | ");
			const FString Want = FString::Printf(TEXT("%d prayers answered"), Names.Num() + 1);
			const bool bPrayer = X.Toasts.ContainsByPredicate([&](const FDmHudToast& T) { return T.Text.StartsWith(Want); });
			const bool bSea = X.Toasts.ContainsByPredicate([](const FDmHudToast& T) { return T.Text.StartsWith(TEXT("The sea has risen")); });
			ClarityCheck(C, X.Toasts.Num() <= 4 && bPrayer && bSea && X.ToastsCreated <= 4,
				FString::Printf(TEXT("toasts: %d pushed -> %d rows: %s"), X.ToastsPushed, X.ToastsCreated, *All));
			// (verifier) a steady stream longer than the 10 s window: still one prayer row (it used to split in two,
			// and later prayers went to the stale row). Pushed on a copy of the HUD clock, then put back.
			{
				const double Now0 = X.Now;
				TArray<FDmHudToast> Keep = X.Toasts;
				TMap<FString, FDmHudToastGroup> KeepG = X.ToastGroups;
				X.Toasts.Reset(); X.ToastGroups.Reset();
				int32 MaxRows = 0;
				for (int32 I = 0; I < 9; ++I)
				{
					X.Now = Now0 + 2.5 * I;
					X.Tick(X.Now);
					X.PushGrouped(TEXT("prayer"), FString::Printf(TEXT("Town%d"), I), TEXT("good"));
					MaxRows = FMath::Max(MaxRows, X.Toasts.FilterByPredicate([](const FDmHudToast& T) { return T.Key == TEXT("prayer"); }).Num());
				}
				const FString Last = X.Toasts.Num() ? X.Toasts.Last().Text : FString();
				ClarityCheck(C, MaxRows == 1, FString::Printf(TEXT("a 20 s stream of prayers: at most %d prayer row(s), last \"%s\""), MaxRows, *Last));
				X.Now = Now0;
				X.Toasts = Keep; X.ToastGroups = KeepG;
			}
		});
		// hints: three arrive together (shrine, awake, prayer) and show in teaching order
		R.Add(60, TEXT("hint-order"), [](FDmScenarioContext& C, ADmHud& H)
		{
			if (dm::World* W = C.Sim->SimMutable()) W->seaLevel = SeaBefore;
			FDmHudContext& X = HC(H);
			X.Toasts.Reset();
			X.ResetHints();
			X.HintHistory.Reset();
			X.bHintsEnabled = true;
			X.TriggerHint(2);
			X.TriggerHint(0);
			X.TriggerHint(3);
		});
		R.Add(6, TEXT(""), [](FDmScenarioContext& C, ADmHud& H)
		{
			FDmHudContext& X = HC(H);
			// the state triggers may add hint 1 (Awe >= 20): every waiting hint must be in teaching order
			bool bSorted = true;
			for (int32 I = 1; I < X.HintQueue.Num(); ++I) bSorted &= X.HintQueue[I - 1] < X.HintQueue[I];
			GClarityNext = X.HintQueue.Num() ? X.HintQueue[0] : -1;
			ClarityCheck(C, X.HintShown == 0 && bSorted && X.HintQueue.Contains(2) && X.HintQueue.Contains(3), FString::Printf(TEXT("hint order: showing %d, then %s"), X.HintShown,
				*FString::JoinBy(X.HintQueue, TEXT(","), [](int32 I) { return FString::FromInt(I); })));
			X.HintBorn = X.Now - (X.HintSeconds + 0.5);   // as if 12.5 s had passed
		});
		R.Add(6, TEXT(""), [](FDmScenarioContext& C, ADmHud& H)
		{
			FDmHudContext& X = HC(H);
			ClarityCheck(C, X.HintShown == GClarityNext && X.HintHistory.Num() >= 2 && X.HintHistory[0].Shown <= 15.0,
				FString::Printf(TEXT("hint left after %.1f s; now showing %d"), X.HintHistory.Num() ? X.HintHistory[0].Shown : -1.0, X.HintShown));
			X.bHintsEnabled = false;
			X.HintQueue.Reset();
			X.HintShown = -1;
			C.Log(FString::Printf(TEXT("DmHudCheck clarity RESULT %s (%d failed)"), GClarityFails == 0 ? TEXT("PASS") : TEXT("FAIL"), GClarityFails));
		});
	},
	[](FDmScenarioContext& Ctx, float) { GClarity.Tick(Ctx); });
