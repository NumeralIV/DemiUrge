// DEMIURGE — Game/UI/Screens/DmScreenPagesWorld.cpp : the ending (SPEC-UI 5 ending()), the timelapse
// overlay (timelapse()) and the planet card (card()). Owner: C8 screens.

#include "Game/UI/Screens/DmScreenPages.h"

#include "Game/UI/Screens/DmScreenCopy.h"
#include "Game/UI/Screens/DmScreenPagesCommon.h"
#include "Game/UI/Screens/DmScreenStyle.h"
#include "Game/UI/Screens/DmScreenSubsystem.h"
#include "Game/UI/Screens/DmScreenWidgets.h"

#include "Game/Core/DmSimSubsystem.h"
#include "Sim/Chronicle.h"
#include "Sim/Sim.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"

using namespace DmPagesCommon;

namespace
{
	FText T(const TCHAR* S) { return FText::FromString(S); }
	FText TS(const FString& S) { return FText::FromString(S); }
	FString FS(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

	void AddBlock(TSharedRef<SVerticalBox> Col, TSharedRef<SWidget> W, float BottomGap = 0.f)
	{
		Col->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, DmStyle::Px(BottomGap))[ W ];
	}

	const dm::World* WorldOf(UDmScreenSubsystem* S)
	{
		UDmSimSubsystem* Sim = S ? S->Sim() : nullptr;
		return Sim ? Sim->Sim() : nullptr;
	}

	int32 CountLiving(const dm::World& W, bool bCultures)
	{
		int32 N = 0;
		if (bCultures)
		{
			for (const dm::Culture& C : W.cultures) if (!C.dead) ++N;
		}
		else
		{
			for (const dm::Settlement& St : W.settlements) if (!St.dead) ++N;
		}
		return N;
	}

	/** The god as the people call it: the epithet of the most populous living culture (the ending says the
	 *  same), else the god's own, else "the Maker". */
	FString GodEpithetOf(const dm::World& W)
	{
		const dm::Culture* Best = nullptr;
		for (const dm::Culture& C : W.cultures)
		{
			if (!C.dead && (!Best || C.pop > Best->pop)) Best = &C;
		}
		if (Best)
		{
			const std::string E = dm::chronicle::epithet(W, Best->id);
			if (!E.empty()) return FS(E);
		}
		if (!W.god.epithet.empty()) return FS(W.god.epithet);
		return TEXT("the Maker");
	}

	// ------------------------------------------------------------------ the ending

	TSharedRef<SWidget> EndingPage(UDmScreenSubsystem* S)
	{
		const dm::World* W = WorldOf(S);
		if (!W || !W->ending.has_value())
		{
			TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
			AddBlock(Col, DmUi::Eyebrow(T(TEXT("THE STORY GOES ON"))), 4.f);
			AddBlock(Col, DmUi::Heading(T(TEXT("No ending yet")), 32.f), 14.f);
			AddBlock(Col, DmUi::Body(T(TEXT("Your people are still making their history. The ending comes when they leave for the stars, or when the last of them is gone.")), 17.f), 22.f);
			AddBlock(Col, DmUi::TextButton(T(TEXT("Keep playing")), Click([S]() { S->KeepPlaying(); }), DmStyle::EButton::Primary, 220.f), 0.f);
			return Modal(DmUi::Panel(Col, 620.f, 34.f));
		}

		const dm::Ending E = *W->ending;

		// the words of the ending: the culture and settlement it is about
		DmCopy::FFill F;
		F.YearOrd = DmCopy::Ordinal(FMath::FloorToInt(float(E.year)));
		F.Planet = FS(W->name);
		const dm::Culture* C = nullptr;
		if (E.culture >= 0 && E.culture < int32(W->cultures.size())) C = &W->cultures[size_t(E.culture)];
		if (!C) for (const dm::Culture& Alive : W->cultures) if (!Alive.dead) { C = &Alive; break; }
		F.CulturePlural = C ? FS(C->plural) : FString(TEXT("the people"));
		F.GodName = (C && !C->godName.empty()) ? FS(C->godName) : FString(TEXT("the Maker"));
		F.GodEpithet = C ? FS(dm::chronicle::epithet(*W, C->id)) : GodEpithetOf(*W);
		FString SettlementName = FString(TEXT("the first hearth"));
		if (E.settlement >= 0 && E.settlement < int32(W->settlements.size())) SettlementName = FS(W->settlements[size_t(E.settlement)].name);
		F.Settlement = SettlementName;
		const FString Paragraph = DmCopy::Fill(DmCopy::EndingTemplate(E.type, E.mood), F);

		const dm::sim::Score Sc = dm::sim::score(*W);
		const dm::chronicle::Tale Tale = dm::chronicle::tale(*W);

		// left: the ending, the statistics, the peoples' names for the god
		TSharedRef<SVerticalBox> Left = SNew(SVerticalBox);
		AddBlock(Left, DmUi::Eyebrow(TS(DmCopy::EndingKind(E.type))), 6.f);
		AddBlock(Left, DmUi::Heading(TS(DmCopy::EndingTitle(E.type, E.mood)), 40.f), 6.f);
		AddBlock(Left, DmUi::Caption(TS(FString::Printf(TEXT("Mood: %s"), *DmCopy::MoodWord(E.mood))), 14.f), 12.f);
		AddBlock(Left, DmUi::Rule(), 16.f);
		AddBlock(Left, DmUi::Body(TS(Paragraph), 18.f), 22.f);

		const int64 ScoreTotal = FMath::RoundToInt64(Sc.total);
		TSharedRef<SHorizontalBox> Stats = SNew(SHorizontalBox);
		Stats->AddSlot().FillWidth(1.f)[ DmUi::Stat(T(TEXT("score")), FText::AsNumber(ScoreTotal), 34.f) ];
		Stats->AddSlot().FillWidth(1.f)[ DmUi::Stat(T(TEXT("peak people")), FText::AsNumber(FMath::RoundToInt64(W->stats.peakPop)), 30.f) ];
		Stats->AddSlot().FillWidth(1.f)[ DmUi::Stat(T(TEXT("years")), FText::AsNumber(FMath::FloorToInt64(W->year)), 30.f) ];
		AddBlock(Left, Stats, 14.f);
		TSharedRef<SHorizontalBox> Stats2 = SNew(SHorizontalBox);
		Stats2->AddSlot().FillWidth(1.f)[ DmUi::Stat(T(TEXT("ages reached")), TS(DmCopy::AgeName(W->age)), 24.f) ];
		Stats2->AddSlot().FillWidth(1.f)[ DmUi::Stat(T(TEXT("peoples alive")), FText::AsNumber(CountLiving(*W, true)), 24.f) ];
		Stats2->AddSlot().FillWidth(1.f)[ DmUi::Stat(T(TEXT("towns alive")), FText::AsNumber(CountLiving(*W, false)), 24.f) ];
		AddBlock(Left, Stats2, 18.f);

		AddBlock(Left, DmUi::Eyebrow(T(TEXT("WHAT THEY CALLED YOU"))), 8.f);
		TSharedRef<SVerticalBox> EpA = SNew(SVerticalBox);
		TSharedRef<SVerticalBox> EpB = SNew(SVerticalBox);
		int32 Shown = 0;
		for (const dm::Culture& Cu : W->cultures)
		{
			if (Shown >= 6) break;
			const FString Epithet = Cu.dead ? FString(TEXT("(gone)")) : FS(dm::chronicle::epithet(*W, Cu.id));
			const FString Line = FString::Printf(TEXT("%s  —  %s"), Cu.name.empty() ? TEXT("Unnamed") : *FS(Cu.name), *Epithet);
			TSharedRef<SVerticalBox>& Target = (Shown < 3 || DmStyle::IsCompact()) ? EpA : EpB;
			AddBlock(Target, DmUi::Caption(TS(Line), 14.f), 4.f);
			++Shown;
		}
		TSharedRef<SHorizontalBox> Epithets = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[ EpA ];
		if (!DmStyle::IsCompact())
		{
			Epithets->AddSlot().FillWidth(1.f).Padding(DmStyle::Px(12.f), 0.f, 0.f, 0.f)[ EpB ];
		}
		AddBlock(Left, Epithets, 0.f);

		// right: the chronicle's own account
		TSharedRef<SVerticalBox> Right = SNew(SVerticalBox);
		AddBlock(Right, DmUi::Eyebrow(T(TEXT("THE CHRONICLE"))), 6.f);
		AddBlock(Right, DmUi::Heading(TS(FS(Tale.title)), 26.f), 14.f);
		TSharedRef<SVerticalBox> Paragraphs = SNew(SVerticalBox);
		for (const std::string& P : Tale.paragraphs)
		{
			Paragraphs->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, DmStyle::Px(14.f))
			[
				DmUi::Body(TS(FS(P)), 16.f)
			];
		}
		Right->AddSlot().FillHeight(1.f)
		[
			SNew(SBox).MinDesiredHeight(DmStyle::Px(360.f)).MaxDesiredHeight(DmStyle::Px(520.f))
			[
				DmUi::Scroll(Paragraphs)
			]
		];

		// side by side on a window; one column on a phone
		TSharedRef<SWidget> Columns = SNullWidget::NullWidget;
		if (DmStyle::IsCompact())
		{
			TSharedRef<SVerticalBox> Stack = SNew(SVerticalBox);
			AddBlock(Stack, Left, 22.f);
			AddBlock(Stack, Right, 0.f);
			Columns = Stack;
		}
		else
		{
			Columns = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.44f).Padding(0.f, 0.f, DmStyle::Px(34.f), 0.f)[ Left ]
				+ SHorizontalBox::Slot().FillWidth(0.56f)[ Right ];
		}

		TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
		Col->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, DmStyle::Px(24.f))[ Columns ];
		Col->AddSlot().AutoHeight()
		[
			ButtonRow({
				DmUi::TextButton(T(TEXT("Watch the ages")), Click([S]() { S->ShowTimelapse(); }), DmStyle::EButton::Primary, 210.f),
				DmUi::TextButton(T(TEXT("Planet card")), Click([S]() { S->ShowCard(); }), DmStyle::EButton::Ghost, 170.f),
				DmUi::TextButton(T(TEXT("Keep playing")), Click([S]() { S->KeepPlaying(); }), DmStyle::EButton::Ghost, 170.f),
				DmUi::TextButton(T(TEXT("New world")), Click([S]() { S->ShowSetup(); }), DmStyle::EButton::Ghost, 170.f)
			})
		];
		// the ending is tall: a shorter frame keeps its buttons on a 1080 window
		return Modal(DmUi::Panel(Col, DmStyle::FitWidth(1240.f, 50.f), 38.f), 24.f);
	}

	// ------------------------------------------------------------------ the timelapse

	TSharedRef<SWidget> TimelapsePage(UDmScreenSubsystem* S)
	{
		const int32 Count = S->TimelapseCount();

		// the year of the current frame, from the world's snapshots
		auto YearText = [S]() -> FText
		{
			const dm::World* W = WorldOf(S);
			const int32 I = S->TimelapseIndex();
			if (!W || I < 0 || I >= int32(W->snapshots.size())) return T(TEXT("The ages"));
			return FText::FromString(FString::Printf(TEXT("Year %d"), FMath::FloorToInt(float(W->snapshots[size_t(I)].year))));
		};
		auto CounterText = [S]() -> FText
		{
			const int32 N = S->TimelapseCount();
			if (N <= 0) return T(TEXT("No frames yet"));
			return FText::FromString(FString::Printf(TEXT("Frame %d of %d"), S->TimelapseIndex() + 1, N));
		};

		// the dock at the bottom: caption, play / pause, scrub bar
		TSharedRef<SVerticalBox> Dock = SNew(SVerticalBox);
		if (Count > 0)
		{
			Dock->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, DmStyle::Px(14.f))
			[
				DmUi::Label(Dyn([S]() { const FString C = S->TimelapseCaption(); return TS(C.IsEmpty() ? FString(TEXT("The years pass quietly.")) : C); }), DmStyle::EFace::SerifItalic, 22.f, DmStyle::GoldHi(), ETextJustify::Center)
			];
			Dock->AddSlot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					DmUi::TextButton(Dyn([S]() { return T(S->IsTimelapsePlaying() ? TEXT("Pause") : TEXT("Play")); }),
						Click([S]() { S->ToggleTimelapse(); }), DmStyle::EButton::Ghost, 120.f)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ DmUi::HGap(18.f) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					DmUi::Slider(
						[S]() -> float
						{
							const int32 N = S->TimelapseCount();
							return N > 1 ? float(S->TimelapseIndex()) / float(N - 1) : 0.f;
						},
						[S](float V)
						{
							const int32 N = S->TimelapseCount();
							S->SetTimelapseIndex(FMath::RoundToInt(V * float(FMath::Max(N - 1, 0))));
						},
						0.f, 1.f)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(DmStyle::Px(18.f), 0.f, 0.f, 0.f)
				[
					SNew(SBox).WidthOverride(DmStyle::Px(DmStyle::IsCompact() ? 84.f : 150.f))
					[
						DmUi::Label(Dyn(CounterText), DmStyle::EFace::Sans, 14.f, DmStyle::Dim(), ETextJustify::Right)
					]
				]
			];
		}
		else
		{
			Dock->AddSlot().AutoHeight()
			[
				DmUi::Body(T(TEXT("No frames have been recorded yet. Let a few centuries pass and come back.")), 17.f)
			];
		}

		return SNew(SOverlay)
			// top left: the year
			+ SOverlay::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.Padding(DmStyle::Px(48.f), DmStyle::Px(36.f), 0.f, 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ DmUi::Eyebrow(T(TEXT("THE AGES"))) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, DmStyle::Px(4.f), 0.f, 0.f)[ DmUi::Label(Dyn(YearText), DmStyle::EFace::SerifBold, 46.f, DmStyle::GoldHi()) ]
			]
			// top right: close
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Top)
			.Padding(0.f, DmStyle::Px(36.f), DmStyle::Px(48.f), 0.f)
			[
				DmUi::TextButton(T(TEXT("Close")), Click([S]() { S->Back(); }), DmStyle::EButton::Ghost, 150.f)
			]
			// bottom: the dock
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Bottom)
			.Padding(0.f, 0.f, 0.f, DmStyle::Px(30.f))
			[
				SNew(SBox).MaxDesiredWidth(DmStyle::Px(1040.f))
				[
					DmUi::Panel(SNew(SBox).WidthOverride(DmStyle::Px(DmStyle::FitWidth(1000.f, 24.f)))[ Dock ], 1040.f, 24.f)
				]
			];
	}

	// ------------------------------------------------------------------ the planet card

	struct FCardData
	{
		FString Name;
		FString When;
		FString Epithet;
		FString People;
		FString Cultures;
		FString Settlements;
		FString Score;
	};

	FCardData CardDataOf(UDmScreenSubsystem* S)
	{
		FCardData D;
		const dm::World* W = WorldOf(S);
		if (!W)
		{
			D.Name = TEXT("No world");
			D.When = TEXT("");
			D.Epithet = TEXT("the Maker");
			D.People = TEXT("0");
			D.Cultures = TEXT("0");
			D.Settlements = TEXT("0");
			D.Score = TEXT("0");
			return D;
		}
		D.Name = FS(W->name);
		D.When = FString::Printf(TEXT("%s  ·  %s Age"), *FS(dm::chronicle::yearStr(*W)), *DmCopy::AgeName(W->age));
		D.Epithet = GodEpithetOf(*W);
		D.People = FString::FormatAsNumber(FMath::RoundToInt(W->stats.pop));
		D.Cultures = FString::FromInt(CountLiving(*W, true));
		D.Settlements = FString::FromInt(CountLiving(*W, false));
		D.Score = FString::FormatAsNumber(FMath::RoundToInt(dm::sim::score(*W).total));
		return D;
	}

	/** The card at the current interface scale: the captured planet beside the words (a phone, bPhone: the
	 *  photograph over the words, the card as wide as the window). */
	TSharedRef<SWidget> Card(UDmScreenSubsystem* S, bool bPhone)
	{
		const FCardData D = CardDataOf(S);
		const float W = bPhone ? DmStyle::Px(DmStyle::FitWidth(DmPages::CardWidthBase, 24.f)) : DmStyle::Px(DmPages::CardWidthBase);
		const float H = bPhone ? W * (DmPages::CardHeightBase / DmPages::CardWidthBase) : DmStyle::Px(DmPages::CardHeightBase);
		const float PhotoW = W * 0.58f;
		const float TextW = W - PhotoW;

		const FSlateBrush* Photo = S->CardImage.IsValid() ? static_cast<const FSlateBrush*>(S->CardImage.Get()) : DmStyle::Solid(DmStyle::Ink2());

		TSharedRef<SOverlay> PhotoBox = SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SImage).Image(Photo)
			]
			+ SOverlay::Slot()
			[
				SNew(SBorder).BorderImage(DmStyle::ScrimBottom()).Padding(0.f)
				[
					SNew(SBox)
				]
			]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(DmStyle::Px(26.f))
			[
				DmUi::Eyebrow(T(TEXT("DEMIURGE  ·  PLANET CARD")))
			];

		TSharedRef<SVerticalBox> Words = SNew(SVerticalBox);
		Words->AddSlot().AutoHeight()[ DmUi::Eyebrow(T(TEXT("A WORLD IN YOUR HANDS"))) ];
		Words->AddSlot().AutoHeight().Padding(0.f, DmStyle::Px(8.f), 0.f, DmStyle::Px(8.f))[ DmUi::Heading(TS(D.Name), (D.Name.Len() > 14 || DmStyle::IsCompact()) ? 30.f : 40.f) ];
		Words->AddSlot().AutoHeight()[ DmUi::Body(TS(D.When), 18.f) ];
		Words->AddSlot().AutoHeight().Padding(0.f, DmStyle::Px(20.f), 0.f, DmStyle::Px(20.f))[ DmUi::Label(TS(D.Epithet), DmStyle::EFace::SerifItalic, D.Epithet.Len() > 20 ? 23.f : 30.f, DmStyle::Gold()) ];
		Words->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, DmStyle::Px(22.f))[ DmUi::Rule(240.f) ];

		TSharedRef<SHorizontalBox> Row1 = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[ DmUi::Stat(T(TEXT("people")), TS(D.People), 30.f) ]
			+ SHorizontalBox::Slot().FillWidth(1.f)[ DmUi::Stat(T(TEXT("cultures")), TS(D.Cultures), 30.f) ];
		TSharedRef<SHorizontalBox> Row2 = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[ DmUi::Stat(T(TEXT("towns")), TS(D.Settlements), 30.f) ]
			+ SHorizontalBox::Slot().FillWidth(1.f)[ DmUi::Stat(T(TEXT("score")), TS(D.Score), 30.f) ];
		Words->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, DmStyle::Px(16.f))[ Row1 ];
		Words->AddSlot().AutoHeight()[ Row2 ];
		Words->AddSlot().FillHeight(1.f)[ SNew(SSpacer) ];
		Words->AddSlot().AutoHeight()[ DmUi::Label(T(TEXT("the chronicle is still being written")), DmStyle::EFace::SerifItalic, 15.f, DmStyle::Dim()) ];

		TSharedRef<SWidget> TextPanel = SNew(SBorder)
			.BorderImage(DmStyle::Solid(DmStyle::Ink(0.94f)))
			.Padding(DmStyle::Px(34.f))
			[
				SNew(SBox).WidthOverride(TextW - DmStyle::Px(68.f)).HeightOverride(H - DmStyle::Px(68.f))
				[
					Words
				]
			];

		if (bPhone)
		{
			TSharedRef<SVerticalBox> Stack = SNew(SVerticalBox);
			Stack->AddSlot().AutoHeight()
			[
				// the photograph keeps its own aspect (a stretched planet on a phone otherwise)
				SNew(SBox).WidthOverride(W - DmStyle::Px(2.f)).HeightOverride((W - DmStyle::Px(2.f)) / DmPages::CardPhotoAspect())
				[
					PhotoBox
				]
			];
			Stack->AddSlot().AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(DmStyle::Solid(DmStyle::Ink(0.94f)))
				.Padding(DmStyle::Px(22.f))
				[
					Words
				]
			];
			return SNew(SBox).WidthOverride(W)
			[
				SNew(SBorder)
				.BorderImage(DmStyle::Solid(DmStyle::Gold(0.35f)))
				.Padding(DmStyle::Px(1.f))
				[
					Stack
				]
			];
		}

		return SNew(SBox).WidthOverride(W).HeightOverride(H)
		[
			SNew(SBorder)
			.BorderImage(DmStyle::Solid(DmStyle::Gold(0.35f)))
			.Padding(DmStyle::Px(1.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(PhotoW - DmStyle::Px(1.f)).HeightOverride(H - DmStyle::Px(2.f))
					[
						PhotoBox
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					TextPanel
				]
			]
		];
	}

	TSharedRef<SWidget> CardPage(UDmScreenSubsystem* S)
	{
		TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
		AddBlock(Col, DmUi::Eyebrow(T(TEXT("PLANET CARD"))), 4.f);
		AddBlock(Col, DmUi::Heading(T(TEXT("A card to share")), 30.f), 16.f);
		AddBlock(Col, Card(S, DmStyle::IsCompact()), 18.f);

		const FString Status = S->Message.IsEmpty() ? FString(TEXT("Save it as a PNG to share it.")) : S->Message;
		AddBlock(Col, DmUi::Caption(TS(Status), 14.f), 16.f);

		Col->AddSlot().AutoHeight()
		[
			ButtonRow({
				DmUi::TextButton(T(TEXT("Save as PNG")), Click([S]() { FString Path; S->ExportPlanetCard(Path); }), DmStyle::EButton::Primary, 210.f),
				BackButton([S]() { S->Back(); })
			})
		];
		return Modal(Col);
	}
}

namespace DmPages
{
	/** Dispatch for the pages of this file (called by Build in DmScreenPages.cpp). */
	TSharedRef<SWidget> BuildCommon(UDmScreenSubsystem* S, EDmScreen Screen, bool& bHandled)
	{
		bHandled = true;
		switch (Screen)
		{
		case EDmScreen::Ending: return EndingPage(S);
		case EDmScreen::Timelapse: return TimelapsePage(S);
		case EDmScreen::Card: return CardPage(S);
		default: break;
		}
		bHandled = false;
		return SNullWidget::NullWidget;
	}

	TSharedRef<SWidget> CardWidget(UDmScreenSubsystem* S, bool bExport)
	{
		// the PNG is always the wide card; the preview on a phone is stacked
		return Card(S, !bExport && DmStyle::IsCompact());
	}

	float CardPhotoAspect()
	{
		return 0.928f;   // 58 % of the card's width over its height
	}
}
