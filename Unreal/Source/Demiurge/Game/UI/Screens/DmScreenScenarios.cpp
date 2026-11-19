// DEMIURGE — Game/UI/Screens/DmScreenScenarios.cpp : the screens' scenarios (SPEC-UNREAL 0.5) and the
// "screens" scene part. Owner: C8 screens.
//
//   shot.ps1 screen-title          title over the live planet
//   shot.ps1 screen-setup          new world: name, seed, calamities, begin
//   shot.ps1 screen-setup-confirm  the 'This replaces ... Begin anyway?' warning (needs a save)
//   shot.ps1 screen-pause          pause menu (pauses the sim)
//   shot.ps1 screen-settings       settings
//   shot.ps1 screen-help           how to play
//   shot.ps1 screen-save           save slots, in game
//   shot.ps1 screen-load           load slots, from the title
//   shot.ps1 screen-ending         the ending (ark-beloved) with the chronicle's tale
//   shot.ps1 screen-ending-ash     the ending (silence-ash)
//   shot.ps1 screen-timelapse      the ages overlay on a 400-year world
//   shot.ps1 screen-card           the planet card (captured planet; the PNG export is logged)
//   shot.ps1 screen-stack -noshot  navigation self-test (exit code 0 = every step as specified)
//   shot.ps1 screen-flow -noshot   flow self-test: pause and load, exit to title, settings on release, quality tiers
//
// The screen scenarios take their picture themselves: the bridge's harness screenshot leaves the
// interface out (RequestScreenshot with bShowUI false), so each screen scenario asks the engine for a
// screenshot WITH the interface at frame 240 and writes shots/ue-<name>.png, then ends. Run them with
// -noshot (the harness has no picture to wait for).

#include "Game/Core/DmCoords.h"
#include "Game/Core/DmGameMode.h"
#include "Game/Core/DmScene.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Game/UI/Screens/DmScreenSubsystem.h"

#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Scalability.h"
#include "Sim/Chronicle.h"
#include "Sim/Save.h"
#include "Sim/Schema.h"
#include "UnrealClient.h"

namespace
{
	/** The name of the screen this process shows (ue-<name>.png); one scenario runs per process. */
	FString GUiShotName;

	/** lat / lon (degrees) of the population-weighted centre of the settlements, or (20, 30). */
	void SettledCentre(const dm::World* W, double& OutLat, double& OutLon)
	{
		OutLat = 20.0;
		OutLon = 30.0;
		if (!W || W->settlements.empty()) return;
		FVector Sum = FVector::ZeroVector;
		for (const dm::Settlement& S : W->settlements)
		{
			if (!S.dead) Sum += DmCoords::CellDir(*W, S.cell) * FMath::Sqrt(FMath::Max(S.pop, 1.0));
		}
		if (Sum.IsNearlyZero()) return;
		DmCoords::DirToLatLon(Sum.GetSafeNormal(), OutLat, OutLon);
		OutLat = FMath::Clamp(OutLat, -55.0, 55.0);
	}

	/** The scene of a screen scenario: the planet, its sky and the screens part, then the view. */
	void SetUpScreens(FDmScenarioContext& Ctx, const TCHAR* ShotName)
	{
		GUiShotName = ShotName;
		// -phone: the viewport becomes a 390 x 844 portrait phone (the engine keeps the window fullscreen at 1920 x 1080
		// otherwise, so -ResX is not enough for a phone check)
		if (FParse::Param(FCommandLine::Get(), TEXT("-phone")))
		{
			// the offscreen game keeps a fullscreen window at the desktop size: a windowed 390 x 844 is asked of the settings
			if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
			{
				Settings->SetFullscreenMode(EWindowMode::Windowed);
				Settings->SetScreenResolution(FIntPoint(390, 844));
				Settings->ApplyResolutionSettings(false);
				Settings->ApplySettings(false);
			}
			Ctx.Log(TEXT("DmScreensTest phone viewport 390x844 requested"));
		}
		Ctx.BuildScene(DmPartsWorld | uint32(EDmPartKind::Ui));
		double Lat, Lon;
		SettledCentre(Ctx.Sim ? Ctx.Sim->Sim() : nullptr, Lat, Lon);
		Ctx.LookAt(Lat, Lon, 700.0, 0.0, 90.0, 30.0);
		if (!Ctx.Sim->IsSunPinned())
		{
			Ctx.SetSubsolar(FMath::Clamp(Lat * 0.5 + 8.0, -23.0, 23.0), Lon + 55.0);
		}
	}

	UDmScreenSubsystem* ScreensOf(FDmScenarioContext& Ctx)
	{
		UDmScreenSubsystem* S = UDmScreenSubsystem::Get(Ctx.World);
		if (S) S->EnsureHost();
		return S;
	}

	/** Gives the fixture world an ending for the ending pages (a scenario may write the world). */
	void SetEnding(FDmScenarioContext& Ctx, dm::EndingType Type, dm::Mood Mood)
	{
		dm::World* W = Ctx.Sim ? Ctx.Sim->SimMutable() : nullptr;
		if (!W) return;
		dm::Ending E;
		E.type = Type;
		E.mood = Mood;
		E.year = W->year;
		for (const dm::Culture& C : W->cultures)
		{
			if (C.dead) continue;
			E.culture = C.id;
			E.settlement = C.capital;
			break;
		}
		W->ending = E;
	}

	/** -screenshots=<folder> (a run's own folder) wins; else the project's shots folder when the repository is
	 *  above it; else Saved/Screenshots. */
	FString ShotsFolder()
	{
		FString Override;
		if (FParse::Value(FCommandLine::Get(), TEXT("-screenshots="), Override) && !Override.IsEmpty())
		{
			return Override;
		}
		FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		for (int32 Up = 0; Up < 6; ++Up)
		{
			if (FPaths::DirectoryExists(FPaths::Combine(Dir, TEXT("shots"))) && FPaths::DirectoryExists(FPaths::Combine(Dir, TEXT("docs"))))
			{
				return FPaths::Combine(Dir, TEXT("shots"));
			}
			const FString Parent = FPaths::GetPath(Dir.EndsWith(TEXT("/")) ? Dir.LeftChop(1) : Dir);
			if (Parent.IsEmpty() || Parent == Dir) break;
			Dir = Parent;
		}
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"));
	}

	/** Every screen scenario's tick: at frame 240 the engine writes shots/ue-<name>.png WITH the
	 *  interface, then the scenario ends once the file is written. */
	void ScreenTick(FDmScenarioContext& Ctx, float)
	{
		constexpr int32 ShotFrame = 240;
		if (GUiShotName.IsEmpty()) return;
		if (Ctx.Frame == ShotFrame)
		{
			const FString Folder = ShotsFolder();
			IFileManager::Get().MakeDirectory(*Folder, true);
			const FString Path = FPaths::Combine(Folder, FString::Printf(TEXT("ue-%s.png"), *GUiShotName));
			FScreenshotRequest::RequestScreenshot(Path, true, false);
			Ctx.Log(FString::Printf(TEXT("DmScreensTest ui shot %s"), *Path));
		}
		if (Ctx.Frame > ShotFrame + 4 && !FScreenshotRequest::IsScreenshotRequested())
		{
			Ctx.Finish();
		}
	}
}

// ------------------------------------------------------------------ the scene part

static FDmScenePartRegistrar GDmScreensPart(TEXT("screens"), EDmPartKind::Ui, 0,
	[](const FDmSceneContext& S) -> AActor*
	{
		if (UDmScreenSubsystem* Screens = UDmScreenSubsystem::Get(S.World)) Screens->EnsureHost();
		return nullptr;   // the host is a viewport widget, not an actor
	},
	TEXT("Game/UI/Screens/DmScreenScenarios.cpp"));

// ------------------------------------------------------------------ the screens

static FDmScenarioRegistrar GDmScreenTitle(TEXT("screen-title"), TEXT("screens"),
	TEXT("the title menu over the live planet"),
	[](FDmScenarioContext& Ctx)
	{
		SetUpScreens(Ctx, TEXT("screen-title"));
		if (UDmScreenSubsystem* S = ScreensOf(Ctx)) S->ShowTitle();
	},
	ScreenTick);

static FDmScenarioRegistrar GDmScreenSetup(TEXT("screen-setup"), TEXT("screens"),
	TEXT("new world: name, seed, calamities, genesis or quick start"),
	[](FDmScenarioContext& Ctx)
	{
		SetUpScreens(Ctx, TEXT("screen-setup"));
		if (UDmScreenSubsystem* S = ScreensOf(Ctx))
		{
			S->ShowTitle();
			S->ShowSetup();
		}
	},
	ScreenTick);

static FDmScenarioRegistrar GDmScreenSetupConfirm(TEXT("screen-setup-confirm"), TEXT("screens"),
	TEXT("new world with a save present: the replace warning"),
	[](FDmScenarioContext& Ctx)
	{
		SetUpScreens(Ctx, TEXT("screen-setup-confirm"));
		if (UDmScreenSubsystem* S = ScreensOf(Ctx))
		{
			if (S->HasWorld()) S->SaveToSlot(TEXT("slot1"));
			S->ShowTitle();
			S->ShowSetup();
			S->bSetupConfirm = true;
			S->RequestRefresh();
		}
	},
	ScreenTick);

static FDmScenarioRegistrar GDmScreenPause(TEXT("screen-pause"), TEXT("screens"),
	TEXT("the pause menu over the planet (the sim is paused)"),
	[](FDmScenarioContext& Ctx)
	{
		SetUpScreens(Ctx, TEXT("screen-pause"));
		if (UDmScreenSubsystem* S = ScreensOf(Ctx)) S->ShowPause();
	},
	ScreenTick);

static FDmScenarioRegistrar GDmScreenSettings(TEXT("screen-settings"), TEXT("screens"),
	TEXT("settings: sound, graphics tiers, sun, effects, interface, labels, hints"),
	[](FDmScenarioContext& Ctx)
	{
		SetUpScreens(Ctx, TEXT("screen-settings"));
		if (UDmScreenSubsystem* S = ScreensOf(Ctx))
		{
			S->ShowTitle();
			S->ShowSettings();
		}
	},
	ScreenTick);

static FDmScenarioRegistrar GDmScreenHelp(TEXT("screen-help"), TEXT("screens"),
	TEXT("how to play: mouse, keyboard, touch and the first steps"),
	[](FDmScenarioContext& Ctx)
	{
		SetUpScreens(Ctx, TEXT("screen-help"));
		if (UDmScreenSubsystem* S = ScreensOf(Ctx))
		{
			S->ShowTitle();
			S->ShowHelp();
		}
	},
	ScreenTick);

static FDmScenarioRegistrar GDmScreenSave(TEXT("screen-save"), TEXT("screens"),
	TEXT("save slots in game, two of them written"),
	[](FDmScenarioContext& Ctx)
	{
		SetUpScreens(Ctx, TEXT("screen-save"));
		if (UDmScreenSubsystem* S = ScreensOf(Ctx))
		{
			S->SaveToSlot(TEXT("slot1"));
			S->SaveToSlot(TEXT("slot3"));
			S->ShowPause();
			S->ShowSave();
		}
	},
	ScreenTick);

static FDmScenarioRegistrar GDmScreenLoad(TEXT("screen-load"), TEXT("screens"),
	TEXT("load slots from the title, with saves to choose from"),
	[](FDmScenarioContext& Ctx)
	{
		SetUpScreens(Ctx, TEXT("screen-load"));
		if (UDmScreenSubsystem* S = ScreensOf(Ctx))
		{
			if (!S->HasSaves())
			{
				S->SaveToSlot(TEXT("slot1"));
				S->SaveToSlot(TEXT("slot3"));
			}
			S->ShowTitle();
			S->ShowLoad();
		}
	},
	ScreenTick);

static FDmScenarioRegistrar GDmScreenEnding(TEXT("screen-ending"), TEXT("screens"),
	TEXT("the ending, ark-beloved: the ending text, statistics, the chronicle's tale"),
	[](FDmScenarioContext& Ctx)
	{
		SetUpScreens(Ctx, TEXT("screen-ending"));
		SetEnding(Ctx, dm::EndingType::Ark, dm::Mood::Beloved);
		if (UDmScreenSubsystem* S = ScreensOf(Ctx)) S->ShowEnding();
	},
	ScreenTick);

static FDmScenarioRegistrar GDmScreenEndingAsh(TEXT("screen-ending-ash"), TEXT("screens"),
	TEXT("the ending, silence-ash"),
	[](FDmScenarioContext& Ctx)
	{
		SetUpScreens(Ctx, TEXT("screen-ending-ash"));
		SetEnding(Ctx, dm::EndingType::Silence, dm::Mood::Ash);
		if (UDmScreenSubsystem* S = ScreensOf(Ctx)) S->ShowEnding();
	},
	ScreenTick);

static FDmScenarioRegistrar GDmScreenTimelapse(TEXT("screen-timelapse"), TEXT("screens"),
	TEXT("the ages: a 400-year world plays over the planet, with the chronicle's captions"),
	[](FDmScenarioContext& Ctx)
	{
		// a short real history: genesis skipped, then 400 years of ticks (snapshots every 25 years)
		Ctx.Sim->CreateWorld(1001u, FString(TEXT("Ithe")), true, 0.0);
		Ctx.Sim->StepTicks(1600);
		SetUpScreens(Ctx, TEXT("screen-timelapse"));
		if (UDmScreenSubsystem* S = ScreensOf(Ctx))
		{
			S->ShowTimelapse();
			S->SetTimelapseIndex(S->TimelapseCount() * 2 / 3);   // a still two thirds of the way through
			if (S->IsTimelapsePlaying()) S->ToggleTimelapse();
		}
	},
	ScreenTick);

namespace
{
	/** The card scenario: the planet card at frame 150, its PNG export at frame 215, then the shot. */
	void CardTick(FDmScenarioContext& Ctx, float Dt)
	{
		UDmScreenSubsystem* S = UDmScreenSubsystem::Get(Ctx.World);
		if (S)
		{
			if (Ctx.Frame == 150) S->ShowCard();
			if (Ctx.Frame == 215)
			{
				FString Path;
				const bool bOk = S->ExportPlanetCard(Path);
				Ctx.Log(FString::Printf(TEXT("DmScreensTest card export %s: %s"), bOk ? TEXT("ok") : TEXT("failed"), *Path));
				if (!bOk) Ctx.SetExitCode(1);
			}
		}
		ScreenTick(Ctx, Dt);
	}
}

static FDmScenarioRegistrar GDmScreenCard(TEXT("screen-card"), TEXT("screens"),
	TEXT("the planet card: the captured planet and the words, exported as a PNG"),
	[](FDmScenarioContext& Ctx)
	{
		SetUpScreens(Ctx, TEXT("screen-card"));
		ScreensOf(Ctx);
	},
	CardTick);

// ------------------------------------------------------------------ navigation self-test

static FDmScenarioRegistrar GDmScreenStack(TEXT("screen-stack"), TEXT("screens"),
	TEXT("navigation self-test: the stack opens, backs and closes as the pages say"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene(DmPartsWorld | uint32(EDmPartKind::Ui));
		Ctx.HoldExit();
		ScreensOf(Ctx);
	},
	[](FDmScenarioContext& Ctx, float)
	{
		UDmScreenSubsystem* S = UDmScreenSubsystem::Get(Ctx.World);
		if (!S)
		{
			Ctx.SetExitCode(1);
			Ctx.Finish();
			return;
		}
		auto Expect = [&Ctx, S](const TCHAR* What, EDmScreen Want)
		{
			const bool bOk = S->Current() == Want;
			Ctx.Log(FString::Printf(TEXT("DmScreensTest %s: %s (screen %d, want %d)"), What, bOk ? TEXT("ok") : TEXT("FAILED"), int32(S->Current()), int32(Want)));
			if (!bOk) Ctx.SetExitCode(1);
		};
		auto ExpectSim = [&Ctx, S](bool bPaused, const TCHAR* What)
		{
			UDmSimSubsystem* Sim = S->Sim();
			const bool bOk = Sim && Sim->IsPaused() == bPaused;
			Ctx.Log(FString::Printf(TEXT("DmScreensTest %s: %s"), What, bOk ? TEXT("ok") : TEXT("FAILED")));
			if (!bOk) Ctx.SetExitCode(1);
		};
		switch (Ctx.Frame)
		{
		case 5:  S->ShowTitle(); Ctx.Sim->SetPaused(false); break;
		case 10: Expect(TEXT("title"), EDmScreen::Title); S->ShowSetup(); break;
		case 15: Expect(TEXT("setup"), EDmScreen::Setup); S->Back(); break;
		case 20: Expect(TEXT("back to title"), EDmScreen::Title); S->ShowPause(); break;
		case 25: Expect(TEXT("pause"), EDmScreen::Pause); S->ShowSettings(); break;
		case 30: Expect(TEXT("settings"), EDmScreen::Settings); S->Back(); break;
		case 35: Expect(TEXT("back to pause"), EDmScreen::Pause); S->Back(); break;
		case 40: Expect(TEXT("pause popped over the title"), EDmScreen::Title); ExpectSim(false, TEXT("sim running again")); S->Back(); break;
		case 45: Expect(TEXT("the title is the floor"), EDmScreen::Title); S->CloseAll(); S->ShowPause(); break;
		case 50: Expect(TEXT("a lone pause"), EDmScreen::Pause); ExpectSim(true, TEXT("sim paused by the pause menu")); S->Back(); break;
		case 55: Expect(TEXT("escape from a lone pause resumes"), EDmScreen::None); ExpectSim(false, TEXT("sim running after resume")); S->ShowTitle(); S->ShowHelp(); break;
		case 60: Expect(TEXT("help over title"), EDmScreen::Help); S->ShowSave(); break;
		case 65: Expect(TEXT("save over help"), EDmScreen::Save); S->Back(); break;
		case 70: Expect(TEXT("help again"), EDmScreen::Help); S->CloseAll(); break;
		case 75: Expect(TEXT("closed"), EDmScreen::None); S->ShowTitle(); break;
		case 80: Expect(TEXT("title"), EDmScreen::Title); S->Back(); break;
		case 85: Expect(TEXT("title is the floor"), EDmScreen::Title); Ctx.Finish(); break;
		default: break;
		}
	});

// ------------------------------------------------------------------ flow self-test (the pause, the settings file, the tiers)

namespace
{
	bool SameQuality(const Scalability::FQualityLevels& A, const Scalability::FQualityLevels& B)
	{
		return A.ShadowQuality == B.ShadowQuality && A.PostProcessQuality == B.PostProcessQuality
			&& A.TextureQuality == B.TextureQuality && A.ViewDistanceQuality == B.ViewDistanceQuality;
	}

	dm::save::UserSettings FlowOriginal;     // the player's settings, put back at the end of the flow test
	Scalability::FQualityLevels FlowAuto;    // the engine's levels under quality Auto
}

static FDmScenarioRegistrar GDmScreenFlow(TEXT("screen-flow"), TEXT("screens"),
	TEXT("flow self-test: a loaded world runs, exit to title, settings written on release, quality Auto restores"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene(DmPartsWorld | uint32(EDmPartKind::Ui));
		Ctx.HoldExit();
		ScreensOf(Ctx);
	},
	[](FDmScenarioContext& Ctx, float)
	{
		UDmScreenSubsystem* S = UDmScreenSubsystem::Get(Ctx.World);
		if (!S)
		{
			Ctx.SetExitCode(1);
			Ctx.Finish();
			return;
		}
		auto Check = [&Ctx](const TCHAR* What, bool bOk)
		{
			Ctx.Log(FString::Printf(TEXT("DmScreensTest %s: %s"), What, bOk ? TEXT("ok") : TEXT("FAILED")));
			if (!bOk) Ctx.SetExitCode(1);
		};
		auto ExpectScreen = [S, &Check](EDmScreen Want, const TCHAR* What) { Check(What, S->Current() == Want); };
		auto ExpectPaused = [S, &Check](bool bPaused, const TCHAR* What)
		{
			UDmSimSubsystem* Sim = S->Sim();
			Check(What, Sim && Sim->IsPaused() == bPaused);
		};
		const std::string SettingsFile = TCHAR_TO_UTF8(*FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), TEXT("settings.dmsettings")));

		switch (Ctx.Frame)
		{
		case 1:
			if (UDmSimSubsystem* Sim = S->Sim()) Sim->SetPaused(false);
			S->ShowTitle();
			break;
		case 5:
			S->SaveToSlot(TEXT("slot1"));
			Check(TEXT("flow: a save to slot1"), S->HasSaves());
			S->ShowPause();
			break;
		case 10:
			ExpectScreen(EDmScreen::Pause, TEXT("flow: pause menu open"));
			ExpectPaused(true, TEXT("flow: the pause menu pauses the sim"));
			S->ShowLoad();
			break;
		case 15:
			S->LoadFromSlot(TEXT("slot1"));
			ExpectScreen(EDmScreen::None, TEXT("flow: loading closes the screens"));
			ExpectPaused(false, TEXT("flow: a world loaded from the pause menu runs"));
			S->ShowPause();
			break;
		case 20:
			ExpectPaused(true, TEXT("flow: paused again"));
			S->ExitToTitle();
			break;
		case 25:
			ExpectScreen(EDmScreen::Title, TEXT("flow: exit to title shows the title"));
			ExpectPaused(false, TEXT("flow: the title runs the live planet"));
			S->ContinueLatest();
			break;
		case 30:
			ExpectScreen(EDmScreen::None, TEXT("flow: continue closes the title"));
			ExpectPaused(false, TEXT("flow: continue runs the world"));
			break;
		case 35:
		{
			// a slider drag changes the settings at once, the file only when the drag ends
			Ctx.Log(TEXT("DmScreensTest flow: settings begin"));
			FlowOriginal = S->GetSettings();
			dm::save::UserSettings Drag = FlowOriginal;
			Drag.music = 0.123;
			S->SetSettingsLive(Drag);
			Check(TEXT("flow: the drag is live in memory"), FMath::Abs(S->GetSettings().music - 0.123) < 1e-6);
			Check(TEXT("flow: the drag is not written before release"), FMath::Abs(dm::save::loadSettings(SettingsFile).music - FlowOriginal.music) < 1e-6);
			S->PersistSettings();
			Check(TEXT("flow: the release writes the file"), FMath::Abs(dm::save::loadSettings(SettingsFile).music - 0.123) < 1e-6);
			S->SetSettings(FlowOriginal);
			break;
		}
		case 40:
		{
			// Quality Auto puts back the engine's levels; a tier changes them
			dm::save::UserSettings U = S->GetSettings();
			U.quality = -1;
			S->SetSettings(U);
			FlowAuto = Scalability::GetQualityLevels();
			U.quality = 0;
			S->SetSettings(U);
			Check(TEXT("flow: the low tier changes the levels"), !SameQuality(Scalability::GetQualityLevels(), FlowAuto));
			break;
		}
		case 45:
		{
			dm::save::UserSettings U = S->GetSettings();
			U.quality = -1;
			S->SetSettings(U);
			Check(TEXT("flow: Auto restores the engine's levels"), SameQuality(Scalability::GetQualityLevels(), FlowAuto));
			S->SetSettings(FlowOriginal);
			S->ShowSetup();
			Check(TEXT("flow: the setup page starts without feedback"), S->Message.IsEmpty());
			break;
		}
		case 50:
			S->CloseAll();
			ExpectScreen(EDmScreen::None, TEXT("flow: close all"));
			Ctx.Finish();
			break;
		default:
			break;
		}
	});
