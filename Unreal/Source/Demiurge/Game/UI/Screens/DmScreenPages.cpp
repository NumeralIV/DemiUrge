// DEMIURGE — Game/UI/Screens/DmScreenPages.cpp : the title, setup, pause, settings, help and save / load
// pages (SPEC-UI 5 and 6). The ending, the timelapse and the planet card are in DmScreenPagesWorld.cpp.
// Owner: C8 screens.

#include "Game/UI/Screens/DmScreenPages.h"

#include "Game/UI/Screens/DmScreenCopy.h"
#include "Game/UI/Screens/DmScreenPagesCommon.h"
#include "Game/UI/Screens/DmScreenStyle.h"
#include "Game/UI/Screens/DmScreenSubsystem.h"
#include "Game/UI/Screens/DmScreenWidgets.h"

#include "Game/Core/DmSimSubsystem.h"
#include "Misc/DateTime.h"
#include "Sim/Schema.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SNullWidget.h"

using namespace DmPagesCommon;

namespace
{
	FText T(const TCHAR* S) { return FText::FromString(S); }
	FText TS(const FString& S) { return FText::FromString(S); }

	/** The world's name, or "" when there is no world. */
	FString WorldName(UDmScreenSubsystem* S)
	{
		UDmSimSubsystem* Sim = S->Sim();
		const dm::World* W = Sim ? Sim->Sim() : nullptr;
		if (!W) return FString();
		return FString(UTF8_TO_TCHAR(W->name.c_str()));
	}

	/** A menu button: a serif title over a small line of detail. */
	TSharedRef<SWidget> MenuButton(const FString& Title, const FString& Detail, DmStyle::EButton Kind, FOnClicked OnClick, float MinWidth)
	{
		const bool bPrimary = Kind == DmStyle::EButton::Primary;
		TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
		Content->AddSlot().AutoHeight()
		[
			DmUi::Label(TS(Title), DmStyle::EFace::Serif, 19.f, bPrimary ? DmStyle::GoldHi() : DmStyle::Text(), ETextJustify::Left)
		];
		if (!Detail.IsEmpty())
		{
			Content->AddSlot().AutoHeight().Padding(0.f, DmStyle::Px(1.f), 0.f, 0.f)
			[
				DmUi::Caption(TS(Detail), 12.f)
			];
		}
		// a menu row: the content is left aligned and the row takes the menu's width
		return SNew(SButton)
			.ButtonStyle(&DmStyle::Button(Kind))
			.ContentPadding(FMargin(DmStyle::Px(20.f), DmStyle::Px(11.f)))
			.OnClicked(OnClick)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.MinDesiredWidth(DmStyle::Px(MinWidth))
				[
					Content
				]
			];
	}

	void AddItem(TSharedRef<SVerticalBox> Col, TSharedRef<SWidget> W, float Gap = 10.f)
	{
		Col->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, DmStyle::Px(Gap))
		[
			W
		];
	}

	void AddText(TSharedRef<SVerticalBox> Col, TSharedRef<SWidget> W, float BottomGap = 0.f)
	{
		Col->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, DmStyle::Px(BottomGap))
		[
			W
		];
	}

	/** The title block and its menu, over the live planet (SPEC-UI 5 title()). */
	TSharedRef<SWidget> TitlePage(UDmScreenSubsystem* S)
	{
		FDmSlotInfo Latest;
		const bool bHasLatest = S->LatestSlot(Latest);
		const bool bHasSaves = S->HasSaves();
		const float MenuW = DmStyle::FitWidth(520.f, 24.f);     // a phone: the menu takes the window's width
		const float Pad = DmStyle::IsCompact() ? 24.f : 96.f;

		TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
		if (bHasLatest)
		{
			const FString Detail = FString::Printf(TEXT("%s  ·  Year %d  ·  %s Age"),
				Latest.Name.IsEmpty() ? TEXT("Unnamed world") : *Latest.Name, FMath::FloorToInt(Latest.Year), *DmCopy::AgeName(Latest.Age));
			AddItem(Menu, MenuButton(TEXT("Continue"), Detail, DmStyle::EButton::Primary, Click([S]() { S->ContinueLatest(); }), MenuW), 12.f);
		}
		AddItem(Menu, MenuButton(TEXT("New World"), TEXT("Name it, seed it, and begin"),
			bHasLatest ? DmStyle::EButton::Ghost : DmStyle::EButton::Primary, Click([S]() { S->ShowSetup(); }), MenuW), 12.f);
		if (bHasSaves)
		{
			AddItem(Menu, MenuButton(TEXT("Load Game"), TEXT("Pick a saved world"), DmStyle::EButton::Ghost, Click([S]() { S->ShowLoad(); }), MenuW), 12.f);
		}
		AddItem(Menu, MenuButton(TEXT("Settings"), TEXT("Sound, graphics, interface"), DmStyle::EButton::Ghost, Click([S]() { S->ShowSettings(); }), MenuW), 12.f);
		AddItem(Menu, MenuButton(TEXT("How to Play"), TEXT("Controls and first steps"), DmStyle::EButton::Ghost, Click([S]() { S->ShowHelp(); }), MenuW), 0.f);

		TSharedRef<SVerticalBox> Lockup = SNew(SVerticalBox);
		AddText(Lockup, DmUi::Label(T(TEXT("DEMIURGE")), DmStyle::EFace::SerifBold, DmStyle::IsCompact() ? 50.f : 84.f, DmStyle::GoldHi(), ETextJustify::Left), 0.f);
		Lockup->AddSlot().AutoHeight().Padding(0.f, DmStyle::Px(4.f), 0.f, DmStyle::Px(16.f))
		[
			DmUi::Rule(320.f)
		];
		AddText(Lockup, DmUi::Label(TS(DmCopy::Tagline()), DmStyle::EFace::SerifItalic, 24.f, DmStyle::Dim(), ETextJustify::Left), 44.f);
		AddText(Lockup, Menu, 0.f);

		return SNew(SOverlay)
			+ SOverlay::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(DmStyle::Px(Pad), 0.f, 0.f, 0.f)
			[
				Lockup
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Bottom)
			.Padding(DmStyle::Px(Pad), 0.f, 0.f, DmStyle::Px(34.f))
			[
				DmUi::Caption(TS(DmCopy::Credit()), 13.f)
			];
	}

	FDmNewWorldRequest MakeRequest(UDmScreenSubsystem* S, bool bQuick)
	{
		FDmNewWorldRequest R;
		R.Name = S->SetupName.TrimStartAndEnd();
		const FString SeedText = S->SetupSeed.TrimStartAndEnd();
		if (SeedText.IsEmpty())
		{
			R.bRandomSeed = true;
		}
		else
		{
			R.bRandomSeed = false;
			R.Seed = SeedText.IsNumeric() ? uint32(FCString::Strtoui64(*SeedText, nullptr, 10)) : GetTypeHash(SeedText);
		}
		R.bQuickStart = bQuick;
		R.Calamity = S->SetupCalamity;
		return R;
	}

	/** The new-world form (SPEC-UI 5 setup()). */
	TSharedRef<SWidget> SetupPage(UDmScreenSubsystem* S)
	{
		TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
		AddText(Col, DmUi::Eyebrow(T(TEXT("NEW WORLD"))));
		Col->AddSlot().AutoHeight().Padding(0.f, DmStyle::Px(4.f), 0.f, 0.f)
		[
			DmUi::Heading(T(TEXT("Name your world")), 34.f)
		];
		Col->AddSlot().AutoHeight().Padding(0.f, DmStyle::Px(8.f), 0.f, DmStyle::Px(20.f))
		[
			DmUi::Caption(T(TEXT("The seed decides the land: the same seed gives the same planet.")), 14.f)
		];

		// name, with a roll
		AddText(Col, DmUi::Eyebrow(T(TEXT("NAME"))));
		Col->AddSlot().AutoHeight().Padding(0.f, DmStyle::Px(6.f), 0.f, DmStyle::Px(16.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				DmUi::Field(Dyn([S]() { return TS(S->SetupName); }), T(TEXT("Name the world")),
					OnText([S](const FString& V) { S->SetupName = V.Left(40); }))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(DmStyle::Px(10.f), 0.f, 0.f, 0.f)
			[
				DmUi::TextButton(T(TEXT("Roll")), Click([S]() { S->SetupName = DmCopy::RollName(uint32(FMath::RandRange(1, 2000000000))); }), DmStyle::EButton::Ghost, 110.f)
			]
		];

		// seed
		AddText(Col, DmUi::Eyebrow(T(TEXT("SEED"))));
		Col->AddSlot().AutoHeight().Padding(0.f, DmStyle::Px(6.f), 0.f, DmStyle::Px(16.f))
		[
			DmUi::Field(Dyn([S]() { return TS(S->SetupSeed); }), T(TEXT("Leave empty for a random seed")),
				OnText([S](const FString& V) { S->SetupSeed = V; }))
		];

		// calamities
		AddText(Col, DmUi::Eyebrow(T(TEXT("CALAMITIES"))));
		TSharedRef<SHorizontalBox> Chips = SNew(SHorizontalBox);
		for (int32 I = 0; I < 3; ++I)
		{
			const bool bOn = int32(S->SetupCalamity) == I;
			Chips->AddSlot().AutoWidth().Padding(0.f, 0.f, DmStyle::Px(10.f), 0.f)
			[
				DmUi::Chip(TS(DmCopy::CalamityWord(I)), bOn, Click([S, I]() { S->SetupCalamity = EDmCalamity(I); S->RequestRefresh(); }), DmStyle::IsCompact() ? 84.f : 110.f)
			];
		}
		Col->AddSlot().AutoHeight().Padding(0.f, DmStyle::Px(6.f), 0.f, DmStyle::Px(6.f))[ Chips ];
		AddText(Col, DmUi::Caption(T(TEXT("How often lightning, quakes, storms and meteors come on their own.")), 13.f), 22.f);

		Col->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, DmStyle::Px(20.f))[ DmUi::Rule() ];

		// the warning when a save exists (SPEC-UI 5: 'This replaces ..., Begin anyway?')
		if (S->bSetupConfirm)
		{
			FDmSlotInfo Latest;
			S->LatestSlot(Latest);
			const FString Ask = FString::Printf(TEXT("This replaces %s, Year %d. Begin anyway?"),
				Latest.Name.IsEmpty() ? TEXT("your saved world") : *Latest.Name, FMath::FloorToInt(Latest.Year));
			AddText(Col, DmUi::Body(TS(Ask), 17.f), 12.f);
			Col->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, DmStyle::Px(16.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, DmStyle::Px(10.f), 0.f)
				[
					DmUi::TextButton(T(TEXT("Begin anyway")), Click([S]() { S->bSetupConfirm = false; S->BeginWorld(MakeRequest(S, false)); }), DmStyle::EButton::Danger, 190.f)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					DmUi::TextButton(T(TEXT("Cancel")), Click([S]() { S->bSetupConfirm = false; S->RequestRefresh(); }), DmStyle::EButton::Ghost, 130.f)
				]
			];
		}

		if (!S->Message.IsEmpty())
		{
			AddText(Col, MessageLine(S->Message), 12.f);
		}

		// begin: genesis, or quick start
		AddItem(Col, MenuButton(TEXT("Begin: shape it yourself"), TEXT("Genesis: cool the world, call the comets, seed life, wake a people"),
			DmStyle::EButton::Primary,
			Click([S]()
			{
				if (S->HasSaves() && !S->bSetupConfirm) { S->bSetupConfirm = true; S->RequestRefresh(); return; }
				S->bSetupConfirm = false;
				S->BeginWorld(MakeRequest(S, false));
			}), DmStyle::FitWidth(420.f, 24.f)), 12.f);
		AddItem(Col, MenuButton(TEXT("Quick start"), TEXT("Skip genesis and begin at the first tribe"),
			DmStyle::EButton::Ghost,
			Click([S]()
			{
				if (S->HasSaves() && !S->bSetupConfirm) { S->bSetupConfirm = true; S->RequestRefresh(); return; }
				S->bSetupConfirm = false;
				S->BeginWorld(MakeRequest(S, true));
			}), DmStyle::FitWidth(420.f, 24.f)), 16.f);

		Col->AddSlot().AutoHeight().HAlign(HAlign_Left)
		[
			BackButton([S]() { S->Back(); })
		];
		return Modal(DmUi::Panel(Col, 560.f, 30.f));
	}

	/** The pause menu (SPEC-UI 5 pause()). */
	TSharedRef<SWidget> PausePage(UDmScreenSubsystem* S)
	{
		TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
		AddText(Col, DmUi::Eyebrow(T(TEXT("PAUSED"))));
		Col->AddSlot().AutoHeight().Padding(0.f, DmStyle::Px(4.f), 0.f, 0.f)
		[
			DmUi::Heading(T(TEXT("The world waits")), 32.f)
		];
		const FString Name = WorldName(S);
		const FString Sub = Name.IsEmpty() ? FString(TEXT("No world is running")) : Name;
		Col->AddSlot().AutoHeight().Padding(0.f, DmStyle::Px(6.f), 0.f, DmStyle::Px(22.f))
		[
			DmUi::Body(TS(Sub), 17.f)
		];

		AddItem(Col, MenuButton(TEXT("Resume"), TEXT(""), DmStyle::EButton::Primary, Click([S]() { S->ResumeGame(); }), DmStyle::FitWidth(360.f, 24.f)), 16.f);
		AddItem(Col, MenuButton(TEXT("Save Game"), TEXT("Write this world to a slot"), DmStyle::EButton::Ghost, Click([S]() { S->ShowSave(); }), DmStyle::FitWidth(360.f, 24.f)), 8.f);
		AddItem(Col, MenuButton(TEXT("Load Game"), TEXT("Go back to a saved world"), DmStyle::EButton::Ghost, Click([S]() { S->ShowLoad(); }), DmStyle::FitWidth(360.f, 24.f)), 8.f);
		AddItem(Col, MenuButton(TEXT("Settings"), TEXT("Sound, graphics, interface"), DmStyle::EButton::Ghost, Click([S]() { S->ShowSettings(); }), DmStyle::FitWidth(360.f, 24.f)), 8.f);
		AddItem(Col, MenuButton(TEXT("How to Play"), TEXT("Controls and first steps"), DmStyle::EButton::Ghost, Click([S]() { S->ShowHelp(); }), DmStyle::FitWidth(360.f, 24.f)), 18.f);
		AddItem(Col, MenuButton(TEXT("Save and Exit to Title"), TEXT("Autosaves this world"), DmStyle::EButton::Ghost, Click([S]() { S->ExitToTitle(); }), DmStyle::FitWidth(360.f, 24.f)), 14.f);
		AddText(Col, DmUi::Caption(T(TEXT("Esc goes back. Your world stays exactly as you left it.")), 13.f), 0.f);

		return Modal(DmUi::Panel(Col, 440.f, 34.f));
	}

	/** A label over a value in one row of the settings. */
	TSharedRef<SWidget> ToggleRow(UDmScreenSubsystem* S, const FText& Name, bool bOn, TFunction<void(dm::save::UserSettings&, bool)> Apply)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				DmUi::Label(Name, DmStyle::EFace::Sans, 15.f, DmStyle::Text(0.92f))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				DmUi::Chip(T(TEXT("On")), bOn, Click([S, Apply, bOn]()
				{
					dm::save::UserSettings Copy = S->GetSettings();
					Apply(Copy, true);
					S->SetSettings(Copy);
					S->RequestRefresh();
				}), 80.f)

			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(DmStyle::Px(6.f), 0.f, 0.f, 0.f)
			[
				DmUi::Chip(T(TEXT("Off")), !bOn, Click([S, Apply, bOn]()
				{
					dm::save::UserSettings Copy = S->GetSettings();
					Apply(Copy, false);
					S->SetSettings(Copy);
					S->RequestRefresh();
				}), 80.f)
			];
	}

	/** One slider of the settings: writes the setting on every change. */
	TSharedRef<SWidget> SettingSlider(UDmScreenSubsystem* S, const FText& Name, float Min, float Max,
		TFunction<double(const dm::save::UserSettings&)> Get, TFunction<void(dm::save::UserSettings&, double)> Set, bool bPercent, bool bRefreshOnRelease)
	{
		TFunction<float()> GetF = [S, Get]() { return float(Get(S->GetSettings())); };
		TFunction<void(float)> SetF = [S, Set](float V)
		{
			dm::save::UserSettings Copy = S->GetSettings();
			Set(Copy, double(V));
			S->SetSettingsLive(Copy);   // applied and broadcast at once; the file is written on release
		};
		TFunction<FText()> ValueF = [S, Get, bPercent]() -> FText
		{
			const double V = Get(S->GetSettings());
			if (bPercent) return DmUi::Percent(float(V));
			return FText::FromString(FString::Printf(TEXT("%.2f"), V));
		};
		FSimpleDelegate OnRelease = FSimpleDelegate::CreateLambda([S, bRefreshOnRelease]()
		{
			S->PersistSettings();
			if (bRefreshOnRelease) S->RequestRefresh();
		});
		return DmUi::SliderRow(Name, GetF, SetF, Min, Max, ValueF, OnRelease);
	}

	/** SPEC-UI 5 settings(): volumes, graphics quality tiers, sun, miniature blur, flashes, shake,
	 *  interface scale, labels, hints, slow down for great events, and the controls link. */
	TSharedRef<SWidget> SettingsPage(UDmScreenSubsystem* S)
	{
		const dm::save::UserSettings& Cur = S->GetSettings();

		// left column: sound
		TSharedRef<SVerticalBox> Sound = SNew(SVerticalBox);
		AddText(Sound, DmUi::Eyebrow(T(TEXT("SOUND"))), 12.f);
		AddItem(Sound, SettingSlider(S, T(TEXT("Master")), 0.f, 1.f, [](const dm::save::UserSettings& s) { return s.master; }, [](dm::save::UserSettings& s, double v) { s.master = v; }, true, false), 12.f);
		AddItem(Sound, SettingSlider(S, T(TEXT("Music")), 0.f, 1.f, [](const dm::save::UserSettings& s) { return s.music; }, [](dm::save::UserSettings& s, double v) { s.music = v; }, true, false), 12.f);
		AddItem(Sound, SettingSlider(S, T(TEXT("Effects")), 0.f, 1.f, [](const dm::save::UserSettings& s) { return s.sfx; }, [](dm::save::UserSettings& s, double v) { s.sfx = v; }, true, false), 12.f);
		AddItem(Sound, SettingSlider(S, T(TEXT("Ambience")), 0.f, 1.f, [](const dm::save::UserSettings& s) { return s.ambience; }, [](dm::save::UserSettings& s, double v) { s.ambience = v; }, true, false), 12.f);

		// right column: graphics
		TSharedRef<SVerticalBox> Gfx = SNew(SVerticalBox);
		AddText(Gfx, DmUi::Eyebrow(T(TEXT("GRAPHICS"))), 10.f);
		AddText(Gfx, DmUi::Caption(T(TEXT("Quality")), 13.f), 6.f);
		TSharedRef<SHorizontalBox> QualityRow = SNew(SHorizontalBox);
		for (int32 Q = -1; Q <= 2; ++Q)
		{
			QualityRow->AddSlot().AutoWidth().Padding(0.f, 0.f, DmStyle::Px(8.f), 0.f)
			[
				DmUi::Chip(TS(DmCopy::QualityWord(Q)), Cur.quality == Q, Click([S, Q]()
				{
					dm::save::UserSettings Copy = S->GetSettings();
					Copy.quality = Q;
					S->SetSettings(Copy);
					S->RequestRefresh();
				}), 0.f)
			];
		}
		AddText(Gfx, QualityRow, 14.f);
		AddText(Gfx, DmUi::Caption(T(TEXT("Sun")), 13.f), 6.f);
		TSharedRef<SHorizontalBox> SunRow = SNew(SHorizontalBox);
		const bool bDayNight = Cur.sun != "follow";
		SunRow->AddSlot().AutoWidth().Padding(0.f, 0.f, DmStyle::Px(8.f), 0.f)
		[
			DmUi::Chip(T(TEXT("Day and night")), bDayNight, Click([S]()
			{
				dm::save::UserSettings Copy = S->GetSettings();
				Copy.sun = "orbit";
				S->SetSettings(Copy);
				S->RequestRefresh();
			}), DmStyle::IsCompact() ? 120.f : 150.f)
		];
		SunRow->AddSlot().AutoWidth()
		[
			DmUi::Chip(T(TEXT("Always lit")), !bDayNight, Click([S]()
			{
				dm::save::UserSettings Copy = S->GetSettings();
				Copy.sun = "follow";
				S->SetSettings(Copy);
				S->RequestRefresh();
			}), DmStyle::IsCompact() ? 120.f : 150.f)
		];
		AddText(Gfx, SunRow, 16.f);
		AddItem(Gfx, SettingSlider(S, T(TEXT("Miniature blur")), 0.f, 1.f, [](const dm::save::UserSettings& s) { return s.tilt; }, [](dm::save::UserSettings& s, double v) { s.tilt = v; }, true, false), 10.f);
		AddItem(Gfx, SettingSlider(S, T(TEXT("Flashes")), 0.f, 1.f, [](const dm::save::UserSettings& s) { return s.flashes; }, [](dm::save::UserSettings& s, double v) { s.flashes = v; }, true, false), 10.f);
		AddItem(Gfx, SettingSlider(S, T(TEXT("Screen shake")), 0.f, 1.f, [](const dm::save::UserSettings& s) { return s.shake; }, [](dm::save::UserSettings& s, double v) { s.shake = v; }, true, false), 10.f);
		AddItem(Gfx, SettingSlider(S, T(TEXT("Interface size")), 0.8f, 1.4f, [](const dm::save::UserSettings& s) { return s.uiScale; }, [](dm::save::UserSettings& s, double v) { s.uiScale = v; }, true, true), 0.f);

		// interface toggles
		TSharedRef<SVerticalBox> Ui = SNew(SVerticalBox);
		AddText(Ui, DmUi::Eyebrow(T(TEXT("INTERFACE"))), 12.f);
		AddItem(Ui, ToggleRow(S, T(TEXT("Settlement names")), Cur.labels, [](dm::save::UserSettings& s, bool v) { s.labels = v; }), 12.f);
		AddItem(Ui, ToggleRow(S, T(TEXT("Hints")), Cur.hints, [](dm::save::UserSettings& s, bool v) { s.hints = v; }), 12.f);
		AddItem(Ui, ToggleRow(S, T(TEXT("Slow down for great events")), Cur.autoSlow, [](dm::save::UserSettings& s, bool v) { s.autoSlow = v; }), 12.f);
		AddText(Ui, DmUi::Caption(T(TEXT("Mouse, keyboard and touch controls are listed under How to Play.")), 13.f), 0.f);

		// two columns (one column on a phone: the scroll frame takes the rest)
		// equal columns (the sliders of both are the same length); a phone stacks them
		TSharedRef<SWidget> Columns = SNullWidget::NullWidget;
		if (DmStyle::IsCompact())
		{
			TSharedRef<SVerticalBox> Stack = SNew(SVerticalBox);
			AddItem(Stack, Sound, 22.f);
			AddText(Stack, Gfx, 0.f);
			Columns = Stack;
		}
		else
		{
			Columns = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, DmStyle::Px(30.f), 0.f)[ Sound ]
				+ SHorizontalBox::Slot().FillWidth(1.f)[ Gfx ];
		}

		TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
		AddText(Col, DmUi::Eyebrow(T(TEXT("PREFERENCES"))));
		Col->AddSlot().AutoHeight().Padding(0.f, DmStyle::Px(4.f), 0.f, DmStyle::Px(22.f))[ DmUi::Heading(T(TEXT("Settings")), 32.f) ];
		AddText(Col, Columns, 22.f);
		Col->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, DmStyle::Px(22.f))[ DmUi::Rule() ];
		AddText(Col, Ui, 26.f);

		Col->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, DmStyle::Px(12.f), 0.f)
			[
				DmUi::TextButton(T(TEXT("How to Play")), Click([S]() { S->ShowHelp(); }), DmStyle::EButton::Ghost, 170.f)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				BackButton([S]() { S->Back(); })
			]
		];
		// the two columns share a fixed width (their fill ratio needs a width to divide)
		return Modal(DmUi::Panel(SNew(SBox).WidthOverride(DmStyle::Px(DmStyle::FitWidth(960.f, 50.f)))[ Col ], 1020.f, 34.f));
	}

	/** One row of a key list: the key or gesture in gold, what it does beside it. */
	TSharedRef<SWidget> KeyRow(const DmCopy::FRow& Row)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Top)
			.Padding(0.f, 0.f, DmStyle::Px(12.f), 0.f)
			[
				SNew(SBox).WidthOverride(DmStyle::Px(DmStyle::IsCompact() ? 118.f : 150.f))
				[
					DmUi::Label(TS(Row.Key), DmStyle::EFace::SansBold, 14.f, DmStyle::Gold())
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			[
				DmUi::Label(TS(Row.Text), DmStyle::EFace::Sans, 14.f, DmStyle::Text(0.92f))
			];
	}

	TSharedRef<SWidget> KeyColumn(const TCHAR* Title, const TArray<DmCopy::FRow>& Rows)
	{
		TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
		AddText(Col, DmUi::Eyebrow(T(Title)), 12.f);
		for (const DmCopy::FRow& R : Rows)
		{
			AddItem(Col, KeyRow(R), 9.f);
		}
		return Col;
	}

	/** SPEC-UI 5 help(): the controls and the seven first steps. */
	TSharedRef<SWidget> HelpPage(UDmScreenSubsystem* S)
	{
		TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
		AddText(Col, DmUi::Eyebrow(T(TEXT("HOW TO PLAY"))));
		Col->AddSlot().AutoHeight().Padding(0.f, DmStyle::Px(4.f), 0.f, 0.f)[ DmUi::Heading(T(TEXT("The world answers to you")), 32.f) ];
		Col->AddSlot().AutoHeight().Padding(0.f, DmStyle::Px(8.f), 0.f, DmStyle::Px(24.f))
		[
			DmUi::Caption(T(TEXT("Your people never obey, and cannot be ordered. You shape the land and the sky they live under. Space pauses the sim at any time.")), 14.f)
		];

		if (DmStyle::IsCompact())
		{
			AddText(Col, KeyColumn(TEXT("MOUSE"), DmCopy::MouseRows()), 18.f);
			AddText(Col, KeyColumn(TEXT("KEYBOARD"), DmCopy::KeyboardRows()), 18.f);
			AddText(Col, KeyColumn(TEXT("TOUCH"), DmCopy::TouchRows()), 24.f);
		}
		else
		{
			TSharedRef<SHorizontalBox> Columns = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, DmStyle::Px(22.f), 0.f)[ KeyColumn(TEXT("MOUSE"), DmCopy::MouseRows()) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, DmStyle::Px(22.f), 0.f)[ KeyColumn(TEXT("KEYBOARD"), DmCopy::KeyboardRows()) ]
				+ SHorizontalBox::Slot().FillWidth(1.f)[ KeyColumn(TEXT("TOUCH"), DmCopy::TouchRows()) ];
			AddText(Col, Columns, 24.f);
		}

		Col->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, DmStyle::Px(20.f))[ DmUi::Rule() ];
		AddText(Col, DmUi::Eyebrow(T(TEXT("FIRST STEPS"))), 12.f);
		int32 Number = 1;
		for (const FString& Step : DmCopy::FirstSteps())
		{
			Col->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, DmStyle::Px(10.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, DmStyle::Px(14.f), 0.f)
				[
					SNew(SBox).WidthOverride(DmStyle::Px(22.f))
					[
						DmUi::Label(TS(FString::FromInt(Number)), DmStyle::EFace::SansBold, 15.f, DmStyle::Gold())
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					DmUi::Body(TS(Step), 16.f)
				]
			];
			++Number;
		}
		Col->AddSlot().AutoHeight().Padding(0.f, DmStyle::Px(18.f), 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				BackButton([S]() { S->Back(); })
			]
		];
		return Modal(DmUi::Panel(SNew(SBox).WidthOverride(DmStyle::Px(DmStyle::FitWidth(1052.f, 50.f)))[ Col ], 1120.f, 34.f));
	}

	/** "Name · Year 312 · Iron Age · 412 people · 19 Nov, 10:22" (local time). */
	FString SlotDetail(const FDmSlotInfo& I)
	{
		static const TCHAR* Months[] = { TEXT("Jan"), TEXT("Feb"), TEXT("Mar"), TEXT("Apr"), TEXT("May"), TEXT("Jun"),
			TEXT("Jul"), TEXT("Aug"), TEXT("Sep"), TEXT("Oct"), TEXT("Nov"), TEXT("Dec") };
		const FTimespan Offset = FDateTime::Now() - FDateTime::UtcNow();
		const FDateTime When = FDateTime::FromUnixTimestamp(I.SavedAt) + Offset;
		const FString Name = I.Name.IsEmpty() ? FString(TEXT("Unnamed world")) : I.Name;
		const int32 Month = FMath::Clamp(When.GetMonth(), 1, 12);
		// two lines: the world, then when it was saved
		return FString::Printf(TEXT("%s  ·  Year %d  ·  %s Age  ·  %s people\nSaved %d %s, %02d:%02d"),
			*Name, FMath::FloorToInt(I.Year), *DmCopy::AgeName(I.Age), *FString::FormatAsNumber(FMath::RoundToInt(I.Pop)),
			When.GetDay(), Months[Month - 1], When.GetHour(), When.GetMinute());
	}

	/** One slot row: its details, and the buttons that save, load or delete it. */
	TSharedRef<SWidget> SlotRow(UDmScreenSubsystem* S, const FString& Slot, const FDmSlotInfo* Info, bool bLoad)
	{
		const bool bExists = Info != nullptr;
		const FString Title = DmCopy::SlotTitle(Slot);
		const FString Detail = bExists ? SlotDetail(*Info) : FString(TEXT("Empty"));
		const FString Pending = S->PendingDelete;
		const bool bAskOverwrite = Pending == (TEXT("save:") + Slot);
		const bool bAskDelete = Pending == (TEXT("delete:") + Slot);

		TSharedRef<SVerticalBox> Text = SNew(SVerticalBox);
		Text->AddSlot().AutoHeight()[ DmUi::Label(TS(Title), DmStyle::EFace::Serif, 19.f, DmStyle::GoldHi()) ];
		Text->AddSlot().AutoHeight().Padding(0.f, DmStyle::Px(2.f), 0.f, 0.f)[ DmUi::Caption(TS(Detail), 13.f) ];

		TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
		if (bAskOverwrite)
		{
			Buttons->AddSlot().AutoWidth().Padding(0.f, 0.f, DmStyle::Px(8.f), 0.f)[
				DmUi::TextButton(T(TEXT("Overwrite")), Click([S, Slot]() { S->PendingDelete.Empty(); S->SaveToSlot(Slot); }), DmStyle::EButton::Danger, 120.f)];
			Buttons->AddSlot().AutoWidth()[
				DmUi::TextButton(T(TEXT("Keep")), Click([S]() { S->PendingDelete.Empty(); S->RequestRefresh(); }), DmStyle::EButton::Ghost, 90.f)];
		}
		else if (bAskDelete)
		{
			Buttons->AddSlot().AutoWidth().Padding(0.f, 0.f, DmStyle::Px(8.f), 0.f)[
				DmUi::TextButton(T(TEXT("Delete for good")), Click([S, Slot]() { S->PendingDelete.Empty(); S->DeleteSlot(Slot); }), DmStyle::EButton::Danger, 150.f)];
			Buttons->AddSlot().AutoWidth()[
				DmUi::TextButton(T(TEXT("Keep")), Click([S]() { S->PendingDelete.Empty(); S->RequestRefresh(); }), DmStyle::EButton::Ghost, 90.f)];
		}
		else if (bLoad)
		{
			Buttons->AddSlot().AutoWidth().Padding(0.f, 0.f, DmStyle::Px(8.f), 0.f)[
				DmUi::TextButton(T(TEXT("Load")), Click([S, Slot]() { S->LoadFromSlot(Slot); }), DmStyle::EButton::Primary, 110.f, bExists)];
			if (bExists && Slot != TEXT("auto"))
			{
				Buttons->AddSlot().AutoWidth()[
					DmUi::TextButton(T(TEXT("Delete")), Click([S, Slot]() { S->PendingDelete = TEXT("delete:") + Slot; S->RequestRefresh(); }), DmStyle::EButton::Ghost, 110.f)];
			}
		}
		else if (Slot != TEXT("auto"))
		{
			Buttons->AddSlot().AutoWidth().Padding(0.f, 0.f, DmStyle::Px(8.f), 0.f)[
				DmUi::TextButton(T(TEXT("Save here")), Click([S, Slot, bExists]()
				{
					if (bExists) { S->PendingDelete = TEXT("save:") + Slot; S->RequestRefresh(); return; }
					S->SaveToSlot(Slot);
				}), DmStyle::EButton::Primary, 120.f, S->HasWorld())];
			if (bExists)
			{
				Buttons->AddSlot().AutoWidth()[
					DmUi::TextButton(T(TEXT("Delete")), Click([S, Slot]() { S->PendingDelete = TEXT("delete:") + Slot; S->RequestRefresh(); }), DmStyle::EButton::Ghost, 110.f)];
			}
		}

		if (DmStyle::IsCompact())
		{
			return DmUi::Row(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ Text ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, DmStyle::Px(10.f), 0.f, 0.f)[ Buttons ],
				14.f);
		}
		return DmUi::Row(
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[ Text ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(DmStyle::Px(12.f), 0.f, 0.f, 0.f)[ Buttons ],
			14.f);
	}

	/** The save (in game) and load (title, pause) slots (SPEC-UI 6 Save.*). */
	TSharedRef<SWidget> SavePage(UDmScreenSubsystem* S, bool bLoad)
	{
		const TArray<FDmSlotInfo> Slots = S->Slots();
		auto Find = [&Slots](const FString& Name) -> const FDmSlotInfo*
		{
			for (const FDmSlotInfo& I : Slots) if (I.Slot == Name) return &I;
			return nullptr;
		};

		TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
		AddText(Col, DmUi::Eyebrow(T(bLoad ? TEXT("LOAD") : TEXT("SAVE"))));
		Col->AddSlot().AutoHeight().Padding(0.f, DmStyle::Px(4.f), 0.f, 0.f)
		[
			DmUi::Heading(T(bLoad ? TEXT("Load a world") : TEXT("Save this world")), 32.f)
		];
		Col->AddSlot().AutoHeight().Padding(0.f, DmStyle::Px(8.f), 0.f, DmStyle::Px(20.f))
		[
			DmUi::Caption(T(bLoad
				? TEXT("Pick a slot to carry on from. Autosave holds the world you last left for the title.")
				: TEXT("Each slot keeps one world. Saving over a slot replaces what it held.")), 14.f)
		];

		if (bLoad)
		{
			if (const FDmSlotInfo* Auto = Find(TEXT("auto")))
			{
				AddItem(Col, SlotRow(S, TEXT("auto"), Auto, true), 10.f);
			}
		}
		for (int32 I = 1; I <= 6; ++I)
		{
			const FString Slot = FString::Printf(TEXT("slot%d"), I);
			AddItem(Col, SlotRow(S, Slot, Find(Slot), bLoad), 10.f);
		}
		if (!bLoad && !S->HasWorld())
		{
			AddText(Col, DmUi::Caption(T(TEXT("There is no world to save yet.")), 13.f), 6.f);
		}
		if (!S->Message.IsEmpty())
		{
			AddText(Col, MessageLine(S->Message), 8.f);
		}
		Col->AddSlot().AutoHeight().Padding(0.f, DmStyle::Px(12.f), 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				BackButton([S]() { S->Back(); })
			]
		];
		return Modal(DmUi::Panel(Col, 780.f, 34.f));
	}
}

namespace DmPages
{
	TSharedRef<SWidget> BuildCommon(UDmScreenSubsystem* S, EDmScreen Screen, bool& bHandled);   // DmScreenPagesWorld.cpp

	TSharedRef<SWidget> Build(UDmScreenSubsystem* S, EDmScreen Screen)
	{
		bool bHandled = false;
		TSharedRef<SWidget> Page = BuildCommon(S, Screen, bHandled);
		if (bHandled) return Page;

		switch (Screen)
		{
		case EDmScreen::Title: return TitlePage(S);
		case EDmScreen::Setup: return SetupPage(S);
		case EDmScreen::Pause: return PausePage(S);
		case EDmScreen::Settings: return SettingsPage(S);
		case EDmScreen::Help: return HelpPage(S);
		case EDmScreen::Save: return SavePage(S, false);
		case EDmScreen::Load: return SavePage(S, true);
		default: break;
		}
		return SNullWidget::NullWidget;
	}

	bool IsPassThrough(EDmScreen Screen)
	{
		return Screen == EDmScreen::Title || Screen == EDmScreen::Timelapse;
	}

	const FSlateBrush* ScrimFor(EDmScreen Screen)
	{
		switch (Screen)
		{
		case EDmScreen::Title: return DmStyle::ScrimLeft();
		case EDmScreen::Timelapse: return DmStyle::ScrimBottom();
		case EDmScreen::Settings:
		case EDmScreen::Help: return DmStyle::Solid(DmStyle::Ink(0.66f));
		default: return DmStyle::Solid(DmStyle::Ink(0.58f));
		}
	}
}
