// DEMIURGE — Game/UI/Hud/SDmHud.cpp : the HUD root and its sections. Owner: C7 hud.

#include "Game/UI/Hud/SDmHud.h"

#include "Game/Core/DmSimSubsystem.h"
#include "Game/UI/Hud/DmHudIcons.h"
#include "Game/UI/Hud/DmHudModel.h"
#include "Game/UI/Hud/DmHudStyle.h"
#include "Game/UI/Hud/SDmHudPanels.h"
#include "Game/UI/Hud/SDmHudWidgets.h"

#include "Rendering/DrawElements.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

#include "Sim/Chronicle.h"
#include "Sim/Civ.h"
#include "Sim/Powers.h"
#include "Sim/Schema.h"

namespace
{
	using namespace DmHudUi;

	FString U(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

	EVisibility Vis(bool b) { return b ? EVisibility::Visible : EVisibility::Collapsed; }
	EVisibility VisHT(bool b) { return b ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; }

	const dm::World* WorldOf(const FDmHudContext& C)
	{
		const UDmSimSubsystem* S = C.Control.Sim();
		return S ? S->Sim() : nullptr;
	}

	bool HudOn(const FDmHudContext& C)
	{
		return C.M.bWorld && C.bPlayState && !C.bEnded && !C.Control.HudHidden();
	}

	/** Expanding gold ring when a power wakes (pulses once). */
	class SDmPulse : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SDmPulse) {}
			SLATE_ARGUMENT(TSharedPtr<FDmHudContext>, Context)
			SLATE_ARGUMENT(int32, Index)
		SLATE_END_ARGS()
		void Construct(const FArguments& A) { Ctx = A._Context; Index = A._Index; SetVisibility(EVisibility::HitTestInvisible); }
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(8, 8); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
		{
			if (!Ctx->M.Powers.IsValidIndex(Index)) return Layer;
			const double T = Ctx->Now - Ctx->M.Powers[Index].PulseStart;
			if (T < 0.0 || T > 1.8) return Layer;
			const float K = float(T / 1.8);
			const FVector2f Sz = FVector2f(G.GetLocalSize());
			const FVector2f C = Sz * 0.5f;
			const float R = 16.f + 18.f * K;
			FLinearColor Col = FDmHudStyle::Get().GoldHi;
			Col.A = (1.f - K) * 0.9f;
			TArray<FVector2f> Pts;
			for (int32 I = 0; I <= 60; ++I)
			{
				const float A = 2.f * PI * I / 60.f;
				Pts.Add(C + FVector2f(FMath::Cos(A), FMath::Sin(A)) * R);
			}
			FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), Pts, ESlateDrawEffect::None, Col, true, 2.f * (1.f - K) + 0.8f);
			return Layer + 1;
		}
	private:
		TSharedPtr<FDmHudContext> Ctx;
		int32 Index = 0;
	};

	/** Remembers where a palette button is (for the power tip). */
	class SDmPowerSlot : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SDmPowerSlot) {}
			SLATE_ARGUMENT(TSharedPtr<FDmHudContext>, Context)
			SLATE_ARGUMENT(int32, Index)
			SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_END_ARGS()
		void Construct(const FArguments& A) { Ctx = A._Context; Index = A._Index; ChildSlot[A._Content.Widget]; }
		virtual void Tick(const FGeometry& G, const double, const float) override
		{
			Ctx->PowerButtonAbs.Add(Index, G.LocalToAbsolute(FVector2D(G.GetLocalSize().X * 0.5, 0.0)));
		}
	private:
		TSharedPtr<FDmHudContext> Ctx;
		int32 Index = 0;
	};

	/** The plural people name without its article ("the Umari" -> "Umari"). */
	FString CulturePluralOf(const dm::Culture& C)
	{
		FString P = C.plural.empty() ? U(C.name) : U(C.plural);
		if (P.StartsWith(TEXT("the "), ESearchCase::IgnoreCase)) P.RightChopInline(4);
		return P;
	}

	FString OverlayName(int32 Mode)
	{
		static const TCHAR* Names[6] = { TEXT("No overlay"), TEXT("Cultures"), TEXT("Temperature"), TEXT("Rainfall"), TEXT("Devotion"), TEXT("Habitability") };
		return Names[FMath::Clamp(Mode, 0, 5)];
	}

	FName OverlayIcon(int32 Mode)
	{
		static const TCHAR* Icons[6] = { TEXT("layers"), TEXT("culture"), TEXT("thermometer"), TEXT("rainfall"), TEXT("devotion"), TEXT("habitability") };
		return FName(Icons[FMath::Clamp(Mode, 0, 5)]);
	}
}

// ------------------------------------------------------------------ construction

void SDmHud::Construct(const FArguments& InArgs)
{
	Ctx = InArgs._Context;
	TSharedPtr<FDmHudContext> C = Ctx;
	ChildSlot
	[
		SNew(SDPIScaler)
		.DPIScale_Lambda([C]() { return C->UiScale; })
		[
			SAssignNew(Layout, SBox)
		]
	];
	SetVisibility(EVisibility::SelfHitTestInvisible);
	Rebuild(false);
}

void SDmHud::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	Geo = AllottedGeometry;
	const float Sc = FMath::Max(0.1f, Ctx->UiScale);
	LocalSize = AllottedGeometry.GetLocalSize() / Sc;
	if (LocalSize.X > 1.0) Ctx->DpiToPx = float(Ctx->ViewportPx.X / LocalSize.X);
	const bool bNarrow = LocalSize.X < 600.0;
	Ctx->bNarrow = bNarrow;
	if (BuiltMode != (bNarrow ? 1 : 0)) Rebuild(bNarrow);
	if (Ctx->M.Selected != LastSelected)
	{
		LastSelected = Ctx->M.Selected;
		if (bNarrow) Ctx->NarrowSheet = LastSelected >= 0 ? 1 : (Ctx->NarrowSheet == 1 ? 0 : Ctx->NarrowSheet);
	}
	UpdateHoverTip();
	UpdateLegend();
}

void SDmHud::Rebuild(bool bNarrow)
{
	BuiltMode = bNarrow ? 1 : 0;
	LegendMode = -1;
	Layout->SetContent(bNarrow ? BuildNarrow() : BuildWide());
}

// ------------------------------------------------------------------ small builders

TSharedRef<SWidget> SDmHud::RoundIconButton(FName Icon, TFunction<void()> OnClick, TFunction<bool()> IsOn, float Size, const FString& TagName)
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedRef<SWidget> B = SNew(SBox)
		.WidthOverride(Size).HeightOverride(Size)
		[
			SNew(SButton)
			.ButtonStyle(&S.RoundButton)
			.ContentPadding(FMargin(0))
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			.OnClicked_Lambda([OnClick]() { if (OnClick) OnClick(); return FReply::Handled(); })
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SBox).WidthOverride(Size).HeightOverride(Size)
					[
						SNew(SBorder)
						.BorderImage_Lambda([IsOn, &S]() { return (IsOn && IsOn()) ? &S.RoundOn : &S.NoBrush; })
						.Padding(0)
					]
				]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SDmIcon).Icon(Icon).Size(Size * 0.5f)
					.Color_Lambda([IsOn, &S]() { return (IsOn && IsOn()) ? S.GoldHi : S.Gold; })
				]
			]
		];
	if (!TagName.IsEmpty()) B->SetTag(FName(*TagName));
	return B;
}

TSharedRef<SWidget> SDmHud::MenuButton()
{
	TSharedPtr<FDmHudContext> C = Ctx;
	return RoundIconButton(FName(TEXT("menu")), [C]() { C->Control.RequestMenu(); }, nullptr, 44.f, FDmHudIds::Menu);
}

TSharedRef<SWidget> SDmHud::AgeBlock(bool bCompact)
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	TSharedRef<SWidget> Ring = SNew(SDmRing)
		.Size(bCompact ? 30.f : 40.f).Thickness(2.f)
		.Progress_Lambda([C]() { return C->M.bGenesis ? C->M.GuideProgress : C->M.AgeProgress; })
		.Color(S.Gold).Back(FDmHudStyle::WithAlpha(S.Gold, 0.16f))
		.Icon_Lambda([C]() { return C->M.bGenesis ? FName(TEXT("sun")) : FName(*FString::Printf(TEXT("age%d"), C->M.Age)); })
		.IconSize(bCompact ? 16.f : 20.f).IconColor(S.GoldHi);
	if (bCompact)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Ring]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
			[
				Text(Lam([C]() { return C->M.YearText; }), EDmFont::SansSemi, 14.f, FSlateColor(S.Text))
			];
	}
	TSharedRef<STextBlock> Year = Text(Lam([C]() { return C->M.YearText; }), EDmFont::SansSemi, 15.f, FSlateColor(S.Text));
	Year->SetTag(FName(FDmHudIds::Year));
	TSharedRef<STextBlock> Age = Text(Lam([C]() { return C->M.AgeText; }), EDmFont::SerifItalic, 15.f, FSlateColor(S.Dim));
	Age->SetTag(FName(FDmHudIds::Age));
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Ring]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				Text(Lam([C]() { return C->M.WorldName; }), EDmFont::Serif, 21.f, FSlateColor(S.Gold))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Year]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 8, 0)
				[
					Text(FString(TEXT("·")), EDmFont::Sans, 15.f, S.Dim)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Age]
			]
		];
}

TSharedRef<SWidget> SDmHud::AwePanel()
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	TSharedRef<STextBlock> Number = Text(Lam([C]() { return C->M.AweText; }), EDmFont::Serif, 30.f, FSlateColor(S.GoldHi));
	Number->SetTag(FName(FDmHudIds::Awe));
	TSharedRef<SWidget> Body =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SDmAweOrb).Size(54.f)
			.Fill_Lambda([C]() { return float(C->M.Awe / FMath::Max(1.0, C->M.AweMax)); })
			.Glow_Lambda([C]() { return float(0.5 + 0.5 * FMath::Sin(C->Now * 1.3)); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)[Number]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(5, 0, 0, 6)
				[
					Text(Lam([C]() { return FString::Printf(TEXT("/ %s"), *C->M.AweMaxText); }), EDmFont::Sans, 13.f, FSlateColor(S.Dim))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, -3, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Text(FString(TEXT("Awe")), EDmFont::SansSemi, 13.f, S.Awe)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
				[
					Text(Lam([C]() { return C->M.IncomeText; }), EDmFont::Sans, 13.f, FSlateColor(S.Good))
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill).Padding(16, 4, 16, 4)
		[
			SNew(SBox).WidthOverride(1.f)[SNew(SBorder).BorderImage(&S.Hairline)]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				Text(Lam([C]() { return C->M.Epithet; }), EDmFont::SerifItalic, 19.f, FSlateColor(S.Gold))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SDmIcon).Size(15.f).Icon_Lambda([C]() { return C->M.RegardIcon; })
					.Color_Lambda([C, &S]() { return C->M.Mood == dm::Mood::Feared ? S.Bad : (C->M.Mood == dm::Mood::Beloved ? S.Good : S.Dim); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
				[
					Text(Lam([C]() { return C->M.RegardWord; }), EDmFont::Sans, 13.f, FSlateColor(S.Text))
				]
			]
		];
	TSharedRef<SWidget> P = Panel(Body, FMargin(14.f, 9.f, 18.f, 9.f));
	P->SetVisibility(TAttribute<EVisibility>::CreateLambda([C]() { return Vis(C->M.bBelievers || !C->M.bGenesis); }));
	return P;
}

TSharedRef<SWidget> SDmHud::AweCompact()
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SDmAweOrb).Size(30.f)
			.Fill_Lambda([C]() { return float(C->M.Awe / FMath::Max(1.0, C->M.AweMax)); })
			.Glow(0.5f)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
		[
			Text(Lam([C]() { return C->M.AweText; }), EDmFont::Serif, 19.f, FSlateColor(S.GoldHi))
		];
}

TSharedRef<SWidget> SDmHud::ArkBar()
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	TSharedRef<SWidget> Body =
		SNew(SButton).ButtonStyle(&S.FlatButton).ContentPadding(FMargin(4, 2))
		.OnClicked_Lambda([C]()
		{
			const dm::World* W = WorldOf(*C);
			if (W && C->M.ArkSettlement >= 0) C->FlyTo(W->settlements[size_t(C->M.ArkSettlement)].cell);
			return FReply::Handled();
		})
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SDmIcon).Icon(FName(TEXT("launch"))).Size(20.f).Color(S.GoldHi)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					Text(Lam([C]() { return C->M.ArkCaption; }), EDmFont::SerifItalic, 15.f, FSlateColor(S.Gold))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SDmBar).Width(220.f).Height(5.f).Value_Lambda([C]() { return C->M.ArkProgress; }).Color(S.Awe)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
					[
						Text(Lam([C]() { return FString::Printf(TEXT("%d %%"), FMath::RoundToInt(C->M.ArkProgress * 100.f)); }), EDmFont::Sans, 12.f, FSlateColor(S.Dim))
					]
				]
			]
		];
	TSharedRef<SWidget> P = Panel(Body, FMargin(10.f, 6.f), &S.PanelHi);
	P->SetVisibility(TAttribute<EVisibility>::CreateLambda([C]() { return Vis(C->M.bArk); }));
	return P;
}

TSharedRef<SWidget> SDmHud::GuideCard(float Width)
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	auto IsHint = [C]() { return !C->M.bGuide && C->HintShown >= 0; };
	TSharedRef<SWidget> Next =
		SNew(SButton).ButtonStyle(&S.ButtonOn).ContentPadding(FMargin(14, 5))
		.Visibility_Lambda([C]() { return Vis(C->M.bGuide && C->M.bCanAdvance); })
		.OnClicked_Lambda([C]() { C->Control.NextPhase(); return FReply::Handled(); })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Text(FString(TEXT("Continue")), EDmFont::SansSemi, 14.f, S.GoldHi)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
			[
				SNew(SDmIcon).Icon(FName(TEXT("arrow"))).Size(16.f).Color(S.GoldHi)
			]
		];
	Next->SetTag(FName(FDmHudIds::GenesisNext));
	TSharedRef<SWidget> Body =
		SNew(SBox).WidthOverride(Width)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 2, 12, 0)
				[
					SNew(SDmIcon).Size(22.f).Color(S.GoldHi)
					.Icon_Lambda([C]()
					{
						if (!C->M.bGuide) return FName(TEXT("info"));
						switch (C->M.Phase)
						{
						case dm::Phase::Molten: return FName(TEXT("fire"));
						case dm::Phase::Water: return FName(TEXT("comet"));
						case dm::Phase::Life: return FName(TEXT("seed"));
						default: return FName(TEXT("awaken"));
						}
					})
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					Text(Lam([C]() { return C->M.bGuide ? C->M.GuideText : FString(FDmHudContext::HintText(C->HintShown)); }), EDmFont::Serif, 16.5f, FSlateColor(S.Text), Width - 70.f)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(8, 0, 0, 0)
				[
					SNew(SBox).WidthOverride(26).HeightOverride(26)
					.Visibility_Lambda([IsHint]() { return Vis(IsHint()); })
					[
						SNew(SButton).ButtonStyle(&S.Button).ContentPadding(FMargin(0)).HAlign(HAlign_Center).VAlign(VAlign_Center)
						.OnClicked_Lambda([C]() { C->DismissHint(); return FReply::Handled(); })
						[
							SNew(SDmIcon).Icon(FName(TEXT("close"))).Size(14.f).Color(S.Dim)
						]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(34, 10, 0, 0)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([C]() { return Vis(C->M.bGuide && C->M.bGenesis); })
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SDmBar).Width(Width - 200.f).Height(4.f).Value_Lambda([C]() { return C->M.GuideProgress; }).Color(S.Gold)
						.Visibility_Lambda([C]() { return Vis(C->M.Phase != dm::Phase::People); })
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
					[
						Text(Lam([C]() { return C->M.GuideExtra; }), EDmFont::Sans, 13.f, FSlateColor(S.Dim))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14, 0, 0, 0)[Next]
			]
		];
	TSharedRef<SWidget> P = Panel(Body, FMargin(16.f, 12.f), &S.PanelHi);
	P->SetVisibility(TAttribute<EVisibility>::CreateLambda([C]() { return Vis(C->M.bGuide || C->HintShown >= 0); }));
	P->SetTag(FName(FDmHudIds::Genesis));
	return P;
}

TSharedRef<SWidget> SDmHud::Toasts()
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox).Tag(FName(FDmHudIds::Toasts));
	for (int32 I = 0; I < 4; ++I)
	{
		auto Get = [C, I]() -> const FDmHudToast* { return C->Toasts.IsValidIndex(I) ? &C->Toasts[I] : nullptr; };
		auto KindColor = [&S](const FString& K)
		{
			if (K == TEXT("good")) return S.Good;
			if (K == TEXT("bad")) return S.Bad;
			if (K == TEXT("epoch")) return S.GoldHi;
			return S.Gold;
		};
		Box->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)
		[
			SNew(SDmBlocker)
			.Visibility_Lambda([Get]() { return VisHT(Get() != nullptr); })
			[
			SNew(SBorder)
			.BorderImage(&S.Pill)
			.Padding(FMargin(14, 7, 18, 7))
			.Visibility_Lambda([Get]() { return VisHT(Get() != nullptr); })
			.ColorAndOpacity_Lambda([Get, C]()
			{
				const FDmHudToast* T = Get();
				if (!T) return FLinearColor(1, 1, 1, 0);
				const double Age = C->Now - T->Born;
				const float In = float(FMath::Clamp(Age / 0.25, 0.0, 1.0));
				const float Out = float(FMath::Clamp((T->Life + 0.6 - Age) / 0.6, 0.0, 1.0));
				return FLinearColor(1, 1, 1, FMath::Min(In, Out));
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SDmIcon).Size(16.f)
					.Icon_Lambda([Get]()
					{
						const FDmHudToast* T = Get();
						if (!T) return FName(TEXT("info"));
						if (T->Kind == TEXT("good")) return FName(TEXT("check"));
						if (T->Kind == TEXT("bad")) return FName(TEXT("close"));
						if (T->Kind == TEXT("epoch")) return FName(TEXT("star"));
						return FName(TEXT("info"));
					})
					.Color_Lambda([Get, KindColor]() { const FDmHudToast* T = Get(); return T ? KindColor(T->Kind) : FLinearColor::White; })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
				[
					Text(Lam([Get]()
					{
						const FDmHudToast* T = Get();
						if (!T) return FString();
						return T->Count > 1 ? FString::Printf(TEXT("%s  ×%d"), *T->Text, T->Count) : T->Text;
					}), EDmFont::Sans, 14.5f, FSlateColor(S.Text))
				]
			]
			]
		];
	}
	return Box;
}

TSharedRef<SWidget> SDmHud::StatsRow(bool bSmall)
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	const float Fs = bSmall ? 12.5f : 14.f;
	const float Is = bSmall ? 13.f : 16.f;
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	struct FStat { const TCHAR* Icon; TFunction<bool()> Show; TFunction<FString()> Value; TFunction<FLinearColor()> Col; const TCHAR* Tag; };
	TArray<FStat> Stats;
	if (bSmall)
		Stats.Add({ TEXT("crown"), [C]() { return C->M.bBelievers; }, [C]() { return C->M.Epithet; }, [&S]() { return S.Gold; }, nullptr });
	Stats.Add({ TEXT("people"), [C]() { return C->M.bShowPop; }, [C]() { return C->M.PopText; }, [&S]() { return S.Text; }, FDmHudIds::Pop });
	Stats.Add({ TEXT("culture"), [C]() { return C->M.bShowCultures; }, [C]() { return C->M.CulturesText; }, [&S]() { return S.Text; }, nullptr });
	Stats.Add({ TEXT("thermometer"), [C]() { return C->M.bShowAnomaly; }, [C]() { return C->M.AnomalyText; }, [C, &S]() { return C->M.bAnomalyBad ? S.Bad : S.Text; }, nullptr });
	Stats.Add({ TEXT("rising-sea"), [C]() { return C->M.bShowSea; }, [C]() { return C->M.SeaText; }, [C, &S]() { return C->M.bSeaBad ? S.Bad : S.Text; }, nullptr });
	for (const FStat& St : Stats)
	{
		TFunction<bool()> Show = St.Show;
		TFunction<FString()> Value = St.Value;
		TFunction<FLinearColor()> Col = St.Col;
		TSharedRef<STextBlock> T = Text(Lam(Value), St.Icon == FString(TEXT("crown")) ? EDmFont::SerifItalic : EDmFont::SansSemi, Fs,
			TAttribute<FSlateColor>::CreateLambda([Col]() { return FSlateColor(Col()); }));
		if (St.Tag) T->SetTag(FName(St.Tag));
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(bSmall ? 0.f : 6.f, 0.f, bSmall ? 12.f : 10.f, 0.f)
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([Show]() { return Vis(Show()); })
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
			[
				SNew(SDmIcon).Icon(FName(St.Icon)).Size(Is).Color(S.Dim)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[T]
		];
	}
	return Row;
}

TSharedRef<SWidget> SDmHud::SpeedControls(bool bCompact)
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	if (bCompact)
	{
		// narrow: pause and one button that cycles 1x 2x 4x 8x
		Row->AddSlot().AutoWidth()[RoundIconButton(FName(TEXT("pause")), [C]() { C->Control.SetPaused(!C->M.bPaused); }, [C]() { return C->M.bPaused; }, 40.f)];
		Row->AddSlot().AutoWidth().Padding(4, 0, 0, 0)
		[
			SNew(SBox).WidthOverride(52).HeightOverride(40)
			[
				SNew(SButton).ButtonStyle(&S.RoundButton).HAlign(HAlign_Center).VAlign(VAlign_Center)
				.OnClicked_Lambda([C]()
				{
					const float Sp = C->M.Speed;
					const float NextSp = Sp < 1.5f ? 2.f : (Sp < 3.f ? 4.f : (Sp < 6.f ? 8.f : 1.f));
					C->ClickSpeed(NextSp);
					return FReply::Handled();
				})
				[
					Text(Lam([C]() { return C->M.Speed < 0.5f ? FString(TEXT("¼×")) : FString::Printf(TEXT("%d×"), FMath::RoundToInt(C->M.Speed)); }), EDmFont::SansSemi, 14.f, FSlateColor(S.Gold))
				]
			]
		];
		return Row;
	}
	struct FSp { float Speed; const TCHAR* Icon; };
	const FSp Speeds[6] = { { 0.f, TEXT("pause") }, { 0.25f, TEXT("linger") }, { 1.f, TEXT("play") }, { 2.f, TEXT("fast") }, { 4.f, TEXT("faster") }, { 8.f, TEXT("fastest") } };
	for (const FSp& Sp : Speeds)
	{
		const float V = Sp.Speed;
		auto IsOn = [C, V]() { return V == 0.f ? C->M.bPaused : (!C->M.bPaused && FMath::IsNearlyEqual(C->M.Speed, V)); };
		TSharedRef<SWidget> B = RoundIconButton(FName(Sp.Icon), [C, V]() { if (V == 0.f) C->Control.SetPaused(!C->M.bPaused); else C->ClickSpeed(V); }, IsOn, 34.f);
		if (V > 0.f) B->SetVisibility(TAttribute<EVisibility>::CreateLambda([C]() { return Vis(!C->M.bGenesis); }));
		B->SetTag(FName(*FString::Printf(TEXT("speed-%g"), V)));
		Row->AddSlot().AutoWidth().Padding(2, 0)[B];
	}
	Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 2, 0)
	[
		SNew(SBox).MinDesiredWidth(46)
		[
			Text(Lam([C]()
			{
				if (C->M.bPaused) return FString(TEXT("Paused"));
				return C->M.Speed < 0.5f ? FString(TEXT("¼×")) : FString::Printf(TEXT("%d×"), FMath::RoundToInt(C->M.Speed));
			}), EDmFont::SansSemi, 13.f, FSlateColor(S.Dim))
		]
	];
	return Row;
}

TSharedRef<SWidget> SDmHud::HideButton()
{
	TSharedPtr<FDmHudContext> C = Ctx;
	return RoundIconButton(FName(TEXT("eye-off")), [C]() { C->Control.HideHud(true); }, nullptr, 34.f, TEXT("hud-hide"));
}

TSharedRef<SWidget> SDmHud::EyeButton()
{
	TSharedPtr<FDmHudContext> C = Ctx;
	TSharedRef<SWidget> B = RoundIconButton(FName(TEXT("eye")), [C]() { C->Control.HideHud(false); }, nullptr, 44.f, FDmHudIds::Eye);
	B->SetVisibility(TAttribute<EVisibility>::CreateLambda([C]() { return Vis(C->M.bWorld && C->bPlayState && !C->bEnded && C->Control.HudHidden()); }));
	return B;
}

// ------------------------------------------------------------------ palette

TSharedRef<SWidget> SDmHud::PowerButton(int32 Index, bool bNarrow)
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	const dm::powers::PowerDef& D = dm::powers::kPowers[Index];
	auto P = [C, Index]() -> const FDmHudPower& { static const FDmHudPower Empty; return C->M.Powers.IsValidIndex(Index) ? C->M.Powers[Index] : Empty; };
	const FString Id = U(D.id);
	const float Wd = bNarrow ? 52.f : 80.f, Ht = bNarrow ? 56.f : 70.f;
	TSharedRef<SOverlay> Content = SNew(SOverlay);
	Content->AddSlot()
	[
		SNew(SBorder)
		.BorderImage_Lambda([P, &S]() { return P().bArmed ? &S.BtnOn : &S.NoBrush; })
		.Padding(0)
	];
	Content->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, bNarrow ? 7.f : 8.f, 0, 0)
	[
		SNew(SDmIcon).Icon(FName(D.id)).Size(bNarrow ? 24.f : 26.f)
		.Color_Lambda([P, &S]()
		{
			const FDmHudPower& Pw = P();
			if (Pw.bArmed) return S.GoldHi;
			if (!Pw.bUnlocked) return FDmHudStyle::WithAlpha(S.Dim, 0.55f);
			if (Pw.bPoor || Pw.bOverCap) return FDmHudStyle::WithAlpha(S.Text, 0.5f);
			return S.Text;
		})
	];
	if (!bNarrow)
	{
		Content->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 37, 0, 0)
		[
			Text(FText::FromString(U(D.name)), EDmFont::Sans, 12.f,
				TAttribute<FSlateColor>::CreateLambda([P, &S]() { const FDmHudPower& Pw = P(); return FSlateColor(Pw.bArmed ? S.GoldHi : (Pw.bUnlocked ? S.Text : FDmHudStyle::WithAlpha(S.Dim, 0.6f))); }))
		];
		Content->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0, 4, 5, 0)
		[
			SNew(SBorder).BorderImage(&S.KeyCap).Padding(FMargin(4, 0, 4, 1))
			[
				Text(FString::Chr(TCHAR(D.key)), EDmFont::SansSemi, 12.f, FDmHudStyle::WithAlpha(S.Dim, 0.85f))
			]
		];
	}
	Content->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, bNarrow ? 4.f : 5.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 3, 0)
		[
			SNew(SDmIcon).Size(12.f)
			.Icon_Lambda([P]() { return P().bUnlocked ? FName(TEXT("awe")) : FName(TEXT("lock")); })
			.Color_Lambda([P, &S]() { const FDmHudPower& Pw = P(); return !Pw.bUnlocked ? S.Dim : ((Pw.bPoor || Pw.bOverCap) ? S.Bad : S.Awe); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Text(Lam([P]() { const FDmHudPower& Pw = P(); return Pw.bUnlocked ? Pw.CostText : FString(TEXT("locked")); }), EDmFont::Sans, 12.f,
				TAttribute<FSlateColor>::CreateLambda([P, &S]() { const FDmHudPower& Pw = P(); return FSlateColor(!Pw.bUnlocked ? FDmHudStyle::WithAlpha(S.Dim, 0.8f) : ((Pw.bPoor || Pw.bOverCap) ? S.Bad : S.Awe)); }))
		]
	];
	Content->AddSlot()[SNew(SDmPulse).Context(C).Index(Index)];

	TSharedRef<SWidget> Button =
		SNew(SDmPowerSlot).Context(C).Index(Index)
		[
			SNew(SBox).WidthOverride(Wd).HeightOverride(Ht)
			[
				SNew(SButton)
				.ButtonStyle(&S.Button)
				.ContentPadding(FMargin(0))
				.OnClicked_Lambda([C, Id]() { C->ClickPower(Id); return FReply::Handled(); })
				.OnHovered_Lambda([C, Id]() { C->HoverPower = Id; })
				.OnUnhovered_Lambda([C, Id]() { if (C->HoverPower == Id) C->HoverPower.Reset(); })
				[
					Content
				]
			]
		];
	Button->SetTag(FName(*FString::Printf(TEXT("power-%s"), *Id)));
	Button->SetVisibility(TAttribute<EVisibility>::CreateLambda([P, C, bNarrow, Index]()
	{
		const FDmHudPower& Pw = P();
		if (!Pw.bVisible) return EVisibility::Collapsed;
		if (bNarrow && !C->M.bGenesis && int32(Pw.Group) != C->NarrowGroup) return EVisibility::Collapsed;
		return EVisibility::Visible;
	}));
	return Button;
}

TSharedRef<SWidget> SDmHud::Palette()
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	const dm::powers::Group Groups[5] = { dm::powers::Group::Genesis, dm::powers::Group::Shape, dm::powers::Group::Sky, dm::powers::Group::Wrath, dm::powers::Group::Grace };
	bool bFirst = true;
	for (const dm::powers::Group G : Groups)
	{
		TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
		for (int32 I = 0; I < dm::powers::kPowerCount; ++I)
			if (dm::powers::kPowers[I].group == G) Buttons->AddSlot().AutoWidth().Padding(1, 0)[PowerButton(I, false)];
		// awaken is a genesis power of the people phase and a history power (Grace) afterwards: its Grace copy shows then
		if (G == dm::powers::Group::Grace)
			for (int32 I = 0; I < dm::powers::kPowerCount; ++I)
				if (FString(UTF8_TO_TCHAR(dm::powers::kPowers[I].id)) == TEXT("awaken")) Buttons->AddSlot().AutoWidth().Padding(1, 0)[PowerButton(I, false)];
		auto GroupVisible = [C, G]()
		{
			for (int32 I = 0; I < C->M.Powers.Num(); ++I) if (C->M.Powers[I].Group == G && C->M.Powers[I].bVisible) return true;
			return false;
		};
		const FLinearColor GC = FDmHudContext::GroupColor(G);
		Row->AddSlot().AutoWidth().Padding(bFirst ? 0.f : 10.f, 0.f, 0.f, 0.f)
		[
			SNew(SVerticalBox)
			.Visibility_Lambda([GroupVisible]() { return Vis(GroupVisible()); })
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(6, 0, 0, 3)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
				[
					SNew(SBox).WidthOverride(10).HeightOverride(2)[SNew(SBorder).BorderImage(&S.Dot).BorderBackgroundColor(FDmHudStyle::WithAlpha(GC, 0.9f))]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Caption(FDmHudContext::GroupName(G), FDmHudStyle::WithAlpha(S.Dim, 0.9f))
				]
			]
			+ SVerticalBox::Slot().AutoHeight()[Buttons]
		];
		bFirst = false;
	}
	TSharedRef<SWidget> P = Panel(Row, FMargin(10.f, 8.f, 10.f, 8.f));
	P->SetVisibility(TAttribute<EVisibility>::CreateLambda([C]()
	{
		for (const FDmHudPower& Pw : C->M.Powers) if (Pw.bVisible) return EVisibility::Visible;
		return EVisibility::Collapsed;
	}));
	return P;
}

TSharedRef<SWidget> SDmHud::NarrowPalette()
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	for (int32 G = 1; G <= 4; ++G)
	{
		const dm::powers::Group Gr = dm::powers::Group(G);
		Tabs->AddSlot().FillWidth(1.f).Padding(2, 0)
		[
			SNew(SBox).HeightOverride(44)
			[
				SNew(SButton).ButtonStyle(&S.FlatButton).HAlign(HAlign_Center).VAlign(VAlign_Center)
				.OnClicked_Lambda([C, G]() { C->NarrowGroup = G; return FReply::Handled(); })
				[
					SNew(SBorder)
					.BorderImage_Lambda([C, G, &S]() { return C->NarrowGroup == G ? &S.BtnOn : &S.NoBrush; })
					.Padding(FMargin(10, 6))
					[
						Text(FText::FromString(FDmHudContext::GroupName(Gr)), EDmFont::SansSemi, 13.f,
							TAttribute<FSlateColor>::CreateLambda([C, G, &S]() { return FSlateColor(C->NarrowGroup == G ? S.GoldHi : S.Dim); }))
					]
				]
			]
		];
	}
	TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
	for (int32 I = 0; I < dm::powers::kPowerCount; ++I) Buttons->AddSlot().AutoWidth().Padding(1, 0)[PowerButton(I, true)];
	TSharedRef<SWidget> Body =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).Visibility_Lambda([C]() { return Vis(!C->M.bGenesis); })[Tabs]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 0)[Buttons];
	TSharedRef<SWidget> P = Panel(Body, FMargin(6.f, 6.f));
	P->SetVisibility(TAttribute<EVisibility>::CreateLambda([C]()
	{
		if (C->NarrowSheet != 0) return EVisibility::Collapsed;
		for (const FDmHudPower& Pw : C->M.Powers) if (Pw.bVisible) return EVisibility::Visible;
		return EVisibility::Collapsed;
	}));
	return P;
}

TSharedRef<SWidget> SDmHud::ArmedChip()
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	auto Def = [C]() { return C->Control.ArmedDef(); };
	auto Brush = [Def]() { const dm::powers::PowerDef* D = Def(); return D && (D->brush || FString(UTF8_TO_TCHAR(D->id)) == TEXT("seed")); };
	auto SmallBtn = [&S](FName Icon, TFunction<void()> F)
	{
		return SNew(SBox).WidthOverride(30).HeightOverride(30)
		[
			SNew(SButton).ButtonStyle(&S.Button).ContentPadding(FMargin(0)).HAlign(HAlign_Center).VAlign(VAlign_Center)
			.OnClicked_Lambda([F]() { F(); return FReply::Handled(); })
			[
				SNew(SDmIcon).Icon(Icon).Size(15.f).Color(S.Gold)
			]
		];
	};
	TSharedRef<SWidget> Body =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SDmIcon).Size(22.f).Color(S.GoldHi)
			.Icon_Lambda([Def]() { const dm::powers::PowerDef* D = Def(); return D ? FName(D->id) : FName(TEXT("dot")); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
		[
			Text(Lam([Def]() { const dm::powers::PowerDef* D = Def(); return D ? FString(UTF8_TO_TCHAR(D->name)) : FString(); }), EDmFont::Serif, 18.f, FSlateColor(S.Gold))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 2, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 3, 0)
			[
				SNew(SDmIcon).Icon(FName(TEXT("awe"))).Size(13.f).Color(S.Awe)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Text(Lam([C]()
				{
					for (const FDmHudPower& Pw : C->M.Powers) if (Pw.bArmed) return Pw.CostText;
					return FString();
				}), EDmFont::Sans, 13.f, FSlateColor(S.Awe))
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16, 0, 0, 0)
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([Brush]() { return Vis(Brush()); })
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(1).HeightOverride(22)[SNew(SBorder).BorderImage(&S.Hairline)]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 4, 0)
			[
				SmallBtn(FName(TEXT("minus")), [C]() { C->Control.ScaleBrush(0.8f); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
			[
				SNew(SDmRing).Size(26.f).Thickness(1.6f)
				.Progress_Lambda([C]() { return (C->Control.BrushAng() - 0.03f) / 0.12f; })
				.Color(S.Gold).Back(FDmHudStyle::WithAlpha(S.Gold, 0.16f))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
			[
				SNew(SBox).MinDesiredWidth(52)
				[
					Text(Lam([C]() { return FString::Printf(TEXT("%d km"), FMath::RoundToInt(C->Control.BrushAng() * 100.f)); }), EDmFont::Sans, 13.f, FSlateColor(S.Text))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SmallBtn(FName(TEXT("plus")), [C]() { C->Control.ScaleBrush(1.25f); })
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14, 0, 0, 0)
		[
			SNew(SBox).WidthOverride(30).HeightOverride(30).Tag(FName(TEXT("disarm")))
			[
				SNew(SButton).ButtonStyle(&S.Button).ContentPadding(FMargin(0)).HAlign(HAlign_Center).VAlign(VAlign_Center)
				.OnClicked_Lambda([C]() { C->Control.Disarm(); return FReply::Handled(); })
				[
					SNew(SDmIcon).Icon(FName(TEXT("close"))).Size(15.f).Color(S.Text)
				]
			]
		];
	TSharedRef<SWidget> P = Panel(Body, FMargin(14.f, 7.f, 8.f, 7.f), &S.PanelHi);
	P->SetVisibility(TAttribute<EVisibility>::CreateLambda([C]() { return Vis(!C->Control.Armed().IsEmpty()); }));
	P->SetTag(FName(FDmHudIds::Armed));
	return P;
}

TSharedRef<SWidget> SDmHud::PowerTip()
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	auto Pw = [C]() -> const FDmHudPower*
	{
		if (C->HoverPower.IsEmpty()) return nullptr;
		for (const FDmHudPower& P : C->M.Powers) if (P.Id == C->HoverPower && P.bVisible) return &P;
		return nullptr;
	};
	TSharedRef<SWidget> Body =
		SNew(SBox).MaxDesiredWidth(320)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Text(Lam([Pw]() { const FDmHudPower* P = Pw(); return P ? P->Name : FString(); }), EDmFont::Serif, 18.f, FSlateColor(S.Gold))
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14, 0, 0, 0)
				[
					Text(Lam([Pw]()
					{
						const FDmHudPower* P = Pw();
						if (!P) return FString();
						return FString::Printf(TEXT("%s  ·  key %s"), P->bUnlocked ? *P->CostText : TEXT("locked"), *P->KeyText);
					}), EDmFont::Sans, 12.5f, FSlateColor(S.Dim))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
			[
				Text(Lam([Pw]() { const FDmHudPower* P = Pw(); return P ? P->Desc : FString(); }), EDmFont::SerifItalic, 15.f, FSlateColor(S.Text), 300.f)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([Pw]() { const FDmHudPower* P = Pw(); return Vis(P && !P->Tip.IsEmpty()); })
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 1, 7, 0)
				[
					SNew(SDmIcon).Size(14.f)
					.Icon_Lambda([Pw]() { const FDmHudPower* P = Pw(); return (P && !P->bUnlocked) ? FName(TEXT("lock")) : FName(TEXT("awe")); })
					.Color_Lambda([Pw, &S]() { const FDmHudPower* P = Pw(); return (P && !P->bUnlocked) ? S.Dim : S.Bad; })
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					Text(Lam([Pw]() { const FDmHudPower* P = Pw(); return P ? P->Tip : FString(); }), EDmFont::Sans, 13.f,
						TAttribute<FSlateColor>::CreateLambda([Pw, &S]() { const FDmHudPower* P = Pw(); return FSlateColor((P && !P->bUnlocked) ? S.Dim : S.Bad); }), 280.f)
				]
			]
		];
	TSharedRef<SWidget> P =
		SNew(SBorder).BorderImage(&S.Tip).Padding(FMargin(14, 10))
		.Visibility_Lambda([Pw]() { return Pw() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		[
			Body
		];
	return P;
}

// ------------------------------------------------------------------ overlays

TSharedRef<SWidget> SDmHud::OverlaySwitch(bool bNarrow)
{
	TSharedPtr<FDmHudContext> C = Ctx;
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 Mode = 0; Mode < 6; ++Mode)
	{
		TSharedRef<SWidget> B = RoundIconButton(OverlayIcon(Mode), [C, Mode]() { C->Control.SetOverlay(Mode); }, [C, Mode]() { return C->M.Overlay == Mode; }, bNarrow ? 40.f : 36.f);
		B->SetTag(FName(*FString::Printf(TEXT("overlay-%d"), Mode)));
		Row->AddSlot().AutoWidth().Padding(2, 0)[B];
	}
	return Row;
}

void SDmHud::UpdateLegend()
{
	if (!LegendBox.IsValid()) return;
	const int32 Mode = Ctx->M.Overlay;
	const int32 NC = Ctx->M.LiveCultures.Num();
	if (Mode == LegendMode && (Mode != 1 || NC == LegendCultures)) return;
	LegendMode = Mode;
	LegendCultures = NC;
	const FDmHudStyle& S = FDmHudStyle::Get();
	if (Mode == 0) { LegendBox->SetContent(SNullWidget::NullWidget); return; }
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
		[
			SNew(SDmIcon).Icon(OverlayIcon(Mode)).Size(18.f).Color(S.GoldHi)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Text(OverlayName(Mode), EDmFont::Serif, 17.f, S.Gold)
		]
	];
	auto Scale = [&](const TArray<FLinearColor>& Stops, const FString& Lo, const FString& Mid, const FString& Hi)
	{
		V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[SNew(SDmGradient).Stops(Stops).Width(250.f).Height(8.f)];
		V->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
		[
			SNew(SBox).WidthOverride(250)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Left)[Text(Lo, EDmFont::Sans, 12.f, S.Dim)]
				+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center)[Text(Mid, EDmFont::Sans, 12.f, S.Dim)]
				+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right)[Text(Hi, EDmFont::Sans, 12.f, S.Dim)]
			]
		];
	};
	switch (Mode)
	{
	case 1:
	{
		const dm::World* W = WorldOf(*Ctx);
		TSharedRef<SWrapBox> Wrap = SNew(SWrapBox).PreferredSize(300.f);
		if (W)
			for (int32 Id : Ctx->M.LiveCultures)
			{
				const dm::Culture& Cu = W->cultures[size_t(Id)];
				const FString Name = CulturePluralOf(Cu);   // the plural, as the text says it
				Wrap->AddSlot().Padding(0, 4, 14, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
					[
						SNew(SDmSigil).Size(16.f).Color(DmCultureColor(Cu.color)).Letter(Initial(U(Cu.name)))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Text(Name, EDmFont::Serif, 14.f, S.Text)]
				];
			}
		V->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[Wrap];
		break;
	}
	case 2: Scale({ FDmHudStyle::Hex(TEXT("3a5fc8")), FDmHudStyle::Hex(TEXT("9fd0e8")), FDmHudStyle::Hex(TEXT("f2e6b0")), FDmHudStyle::Hex(TEXT("f0a04a")), FDmHudStyle::Hex(TEXT("c8402c")) }, TEXT("−20°"), TEXT("10°"), TEXT("35°")); break;
	case 3: Scale({ FDmHudStyle::Hex(TEXT("c9a46a")), FDmHudStyle::Hex(TEXT("d8d29a")), FDmHudStyle::Hex(TEXT("6fb36a")), FDmHudStyle::Hex(TEXT("2f7fa6")) }, TEXT("dry"), TEXT("moderate"), TEXT("wet")); break;
	case 4: Scale({ FDmHudStyle::Hex(TEXT("3a4152")), FDmHudStyle::Hex(TEXT("8a7a5a")), FDmHudStyle::Hex(TEXT("ffd98a")) }, TEXT("faithless"), TEXT("wavering"), TEXT("devout")); break;
	default:
		Scale({ FDmHudStyle::Hex(TEXT("5a2a2a")), FDmHudStyle::Hex(TEXT("b08a3a")), FDmHudStyle::Hex(TEXT("8fd6a0")) }, TEXT("harsh"), TEXT("bearable"), TEXT("kind"));
		V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Text(FString(TEXT("The glowing mark is the kindest ground.")), EDmFont::SerifItalic, 13.f, S.Dim)];
		break;
	}
	LegendBox->SetContent(Panel(V, FMargin(14.f, 10.f)));
}

TSharedRef<SWidget> SDmHud::Legend()
{
	return SAssignNew(LegendBox, SBox);
}

// ------------------------------------------------------------------ banner, hover tip

TSharedRef<SWidget> SDmHud::Banner()
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	TSharedRef<SWidget> Body =
		SNew(SButton).ButtonStyle(&S.FlatButton).ContentPadding(FMargin(0))
		.OnClicked_Lambda([C]() { C->FlyTo(C->Banner.Cell); C->Banner.Born = -100; return FReply::Handled(); })
		[
			SNew(SBox).WidthOverride(760)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(SDmFlourish).Width(460.f).Color(S.Gold)]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)
				[
					Text(Lam([C]() { return C->Banner.Heading; }), EDmFont::Serif, 40.f, FSlateColor(S.GoldHi))
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)
				[
					SNew(STextBlock)
					.Text(Lam([C]() { return C->Banner.Text; }))
					.Font(S.Font(EDmFont::Serif, 19.f))
					.ColorAndOpacity(FSlateColor(S.Text))
					.Justification(ETextJustify::Center)
					.WrapTextAt(700.f)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 0)
				[
					Text(Lam([C]() { return C->Banner.Year; }), EDmFont::SansSemi, 13.f, FSlateColor(S.Dim))
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)[SNew(SDmFlourish).Width(300.f).Color(S.Gold)]
			]
		];
	return SNew(SBorder)
		.BorderImage(&S.PanelHi)
		.Padding(FMargin(30, 18))
		.Visibility_Lambda([C]() { return Vis(C->Banner.Active(C->Now) && HudOn(*C)); })
		.ColorAndOpacity_Lambda([C]()
		{
			const double A = C->Now - C->Banner.Born;
			const float In = float(FMath::Clamp(A / 0.6, 0.0, 1.0));
			const float Out = float(FMath::Clamp((C->Banner.Life - A) / 1.0, 0.0, 1.0));
			return FLinearColor(1, 1, 1, FMath::Min(In, Out));
		})
		[
			SNew(SDmBlocker)[Body]
		];
}

void SDmHud::UpdateHoverTip()
{
	FDmHudHover H;
	const bool bHit = Ctx->Control.GetHover(H) && H.bHit && H.Cell >= 0;
	const dm::World* W = WorldOf(*Ctx);
	// nothing under a HUD panel (the pointer is over the panel, not the planet behind it)
	if (!bHit || !W || !HudOn(*Ctx) || Ctx->bChronicleOpen || !Ctx->HoverPower.IsEmpty() || DmHudUi::PointerOverPanelLast())
	{
		bTipShown = false;
		TipCell = -1;
		return;
	}
	const int32 Key = H.Settlement >= 0 ? -2 - H.Settlement : H.Cell;
	if (Key != TipCell) { TipCell = Key; TipSince = Ctx->Now; bTipShown = false; }
	TipPx = H.ScreenPx;
	const double Delay = H.bForced ? 0.0 : 0.35;
	if (Ctx->Now - TipSince < Delay) { bTipShown = false; return; }
	const uint32 BuildKey = HashCombine(GetTypeHash(Key), GetTypeHash(Ctx->Control.Armed()));
	if (bTipShown && BuildKey == TipBuildKey && Ctx->Now - TipBuilt < 0.25) return;
	bTipShown = true;
	TipBuildKey = BuildKey;
	TipBuilt = Ctx->Now;

	// content: the rows are fixed widgets (HoverTip) whose text is set here, as PowerTip's are
	const FDmHudStyle& S = FDmHudStyle::Get();
	for (FTipRow& R : TipRows) R = FTipRow();
	auto SetRow = [this, &S](int32 I, const FString& T, EDmFont F, float Px, const FLinearColor& Col)
	{
		TipRows[I].Text = T;
		TipRows[I].Font = S.Font(F, Px);
		TipRows[I].Color = Col;
		TipRows[I].bShow = !T.IsEmpty();
	};
	if (H.Settlement >= 0 && H.Settlement < int32(W->settlements.size()))
	{
		const dm::Settlement& St = W->settlements[size_t(H.Settlement)];
		const dm::Culture* Cu = dm::state::culture(*W, St.culture);
		SetRow(0, U(St.name), EDmFont::Serif, 18.f, S.Gold);
		SetRow(1, FString::Printf(TEXT("%s of the %s"), *FDmHudContext::TierName(St.tier), Cu ? *CulturePluralOf(*Cu) : TEXT("people")), EDmFont::SerifItalic, 14.f, S.Dim);
		SetRow(2, FString::Printf(TEXT("%s people"), *FDmHudContext::Big(St.pop)), EDmFont::Sans, 13.5f, S.Text);
	}
	else
	{
		const int32 Cl = H.Cell;
		const int32 B = FMath::Clamp(int32(W->biome[size_t(Cl)]), 0, int32(dm::Biome::COUNT) - 1);
		SetRow(0, U(dm::kBiomeNames[B]), EDmFont::Serif, 17.f, S.Gold);
		const float Tc = W->temp[size_t(Cl)];
		const float Mo = W->moist[size_t(Cl)];
		const TCHAR* Rain = Mo < 0.15f ? TEXT("arid") : Mo < 0.35f ? TEXT("dry") : Mo < 0.6f ? TEXT("moderate rain") : Mo < 0.8f ? TEXT("wet") : TEXT("very wet");
		SetRow(1, FString::Printf(TEXT("%s°C  ·  %s"), *FDmHudContext::Signed(Tc, 0), Rain), EDmFont::Sans, 13.5f, S.Text);
	}
	if (H.bArmed)
	{
		const dm::powers::PowerDef* D = Ctx->Control.ArmedDef();
		if (D)
		{
			FString Cost;
			for (const FDmHudPower& Pw : Ctx->M.Powers) if (Pw.Id == UTF8_TO_TCHAR(D->id)) Cost = Pw.CostText;
			TipRows[3].bShow = true;   // hairline
			SetRow(4, FString::Printf(TEXT("%s  ·  %s Awe"), UTF8_TO_TCHAR(D->name), *Cost), EDmFont::SansSemi, 13.5f, S.Awe);
			if (!H.Can.ok)
			{
				FString WhyText;
				using dm::powers::Reason;
				if (H.Can.reason == Reason::Awe) WhyText = TEXT("Not enough Awe");
				else if (H.Can.reason == Reason::Locked || H.Can.reason == Reason::Phase) WhyText = TEXT("Not yet within your power");
				else switch (H.Can.why)
				{
				case dm::powers::Why::Sea: WhyText = TEXT("Choose land"); break;
				case dm::powers::Why::Land: WhyText = TEXT("Choose open sea"); break;
				case dm::powers::Why::Settlement: WhyText = TEXT("Choose a settlement"); break;
				case dm::powers::Why::Barren: WhyText = TEXT("Nothing can live here yet"); break;
				case dm::powers::Why::Crowded: WhyText = TEXT("Too close to another people"); break;
				default: WhyText = D->target == dm::powers::Target::Settlement ? TEXT("There is no war to end") : TEXT("Not here"); break;
				}
				SetRow(5, WhyText, EDmFont::Sans, 13.5f, S.Bad);
			}
		}
	}
}

TSharedRef<SWidget> SDmHud::HoverTip()
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	// rows 0..5: title, detail, people / the rest; 3 is the hairline above the armed line; 4 armed; 5 why not
	const float Tops[6] = { 0.f, 1.f, 3.f, 8.f, 6.f, 2.f };
	for (int32 I = 0; I < 6; ++I)
	{
		TSharedRef<SWidget> Row = SNullWidget::NullWidget;
		if (I == 3)
		{
			Row = SNew(SBox).HeightOverride(1.f)
				.Visibility_Lambda([this]() { return Vis(TipRows[3].bShow); })
				[
					SNew(SBorder).BorderImage(&S.Hairline)
				];
		}
		else
		{
			Row = SNew(SBox)
				.Visibility_Lambda([this, I]() { return Vis(TipRows[I].bShow); })
				[
					SNew(STextBlock)
					.Text_Lambda([this, I]() { return FText::FromString(TipRows[I].Text); })
					.Font_Lambda([this, I]() { return TipRows[I].Font; })
					.ColorAndOpacity_Lambda([this, I]() { return FSlateColor(TipRows[I].Color); })
					.WrapTextAt(280.f)
				];
		}
		Box->AddSlot().AutoHeight().Padding(0, Tops[I], 0, 0)[Row];
	}
	return SNew(SBorder)
		.BorderImage(&S.Tip)
		.Padding(FMargin(12, 8, 14, 9))
		.Visibility_Lambda([this]() { return bTipShown ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		[
			Box
		];
}

// ------------------------------------------------------------------ layouts

TSharedRef<SWidget> SDmHud::BuildWide()
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	auto On = [C]() { return VisHT(HudOn(*C)); };
	TSharedRef<SWidget> PowerTipW = PowerTip();
	TSharedRef<SWidget> HoverTipW = HoverTip();
	TSharedRef<SOverlay> O = SNew(SOverlay);

	// labels under everything
	O->AddSlot()
	[
		SNew(SBox).Visibility_Lambda([C]() { return (HudOn(*C) && C->bLabels) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		[
			SNew(SDmHudLabels).Context(C)
		]
	];

	// top left
	O->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(18, 16, 0, 0)
	[
		SNew(SHorizontalBox).Visibility_Lambda(On)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MenuButton()]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
		[
			Panel(AgeBlock(false), FMargin(12.f, 8.f, 18.f, 8.f))
		]
	];

	// left column: now strip or inspector
	O->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(18, 96, 0, 0)
	[
		SNew(SVerticalBox).Visibility_Lambda(On)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).Visibility_Lambda([C]() { return Vis(C->M.Selected < 0); })[SNew(SDmNowStrip).Context(C).Width(300.f)]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).Visibility_Lambda([C]() { return Vis(C->M.Selected >= 0); })
			.MaxDesiredHeight_Lambda([this]() { return FOptionalSize(float(FMath::Max(200.0, LocalSize.Y - 96.0 - 140.0))); })
			[
				SNew(SDmBlocker)
				[
					SNew(SScrollBox).Style(&S.ScrollBox).ScrollBarStyle(&S.ScrollBar).ScrollBarThickness(FVector2D(4, 4))
					+ SScrollBox::Slot()[SNew(SDmInspector).Context(C).Width(390.f)]
				]
			]
		]
	];

	// top centre: Awe, the Ark, the guide card, toasts
	O->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 16, 0, 0)
	[
		SNew(SVerticalBox).Visibility_Lambda(On)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[AwePanel()]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 0)[ArkBar()]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)[GuideCard(520.f)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 0)[Toasts()]
	];

	// top right: stats, time, hide
	O->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0, 16, 18, 0)
	[
		SNew(SVerticalBox).Visibility_Lambda(On)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
		[
			Panel(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[StatsRow(false)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 12, 0)
				[
					SNew(SBox).WidthOverride(1).HeightOverride(26)[SNew(SBorder).BorderImage(&S.Hairline)]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SpeedControls(false)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)[HideButton()],
				FMargin(8.f, 5.f, 6.f, 5.f))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0, 12, 0, 0)
		[
			SNew(SDmChronicleFeed).Context(C).Width(370.f).Slots(5)
		]
	];

	// bottom: armed chip, power tip, palette; bottom right: legend and overlay switch
	O->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 18)
	[
		SNew(SVerticalBox).Visibility_Lambda(On)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 10)[ArmedChip()]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Palette()]
	];
	O->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Top)
		.Padding(TAttribute<FMargin>::CreateLambda([this, PowerTipW]()
		{
			const FVector2D* Abs = Ctx->PowerButtonAbs.Find([&]()
			{
				for (int32 I = 0; I < Ctx->M.Powers.Num(); ++I) if (Ctx->M.Powers[I].Id == Ctx->HoverPower) return I;
				return -1;
			}());
			if (!Abs) return FMargin(0);
			const FVector2D L = Geo.AbsoluteToLocal(*Abs) / FMath::Max(0.1f, Ctx->UiScale);
			const FVector2D Sz = PowerTipW->GetDesiredSize();
			const double X = FMath::Clamp(L.X - Sz.X * 0.5, 8.0, FMath::Max(8.0, LocalSize.X - Sz.X - 8.0));
			const double Y = FMath::Max(8.0, L.Y - Sz.Y - 26.0);
			return FMargin(float(X), float(Y), 0.f, 0.f);
		}))
	[
		PowerTipW
	];
	O->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(TAttribute<FMargin>::CreateLambda([this]() { return FMargin(0.f, 0.f, 18.f, LocalSize.X < 1580.0 ? 128.f : 18.f); }))
	[
		SNew(SVerticalBox).Visibility_Lambda(On)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0, 0, 0, 10)[Legend()]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[Panel(OverlaySwitch(false), FMargin(6.f, 5.f))]
	];

	// banner and the chronicle book
	O->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 300, 0, 0)[Banner()];
	O->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)
	[
		SNew(SBox).Visibility_Lambda([C]() { return Vis(C->bChronicleOpen && HudOn(*C)); })
		[
			SNew(SDmChronicleBook).Context(C).Width(780.f).Height(800.f)
		]
	];

	// hover tip near the pointer
	O->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Top)
		.Padding(TAttribute<FMargin>::CreateLambda([this, HoverTipW]()
		{
			const FVector2D L = TipPx / FMath::Max(0.01, double(Ctx->DpiToPx));
			const FVector2D Sz = HoverTipW->GetDesiredSize();
			double X = L.X + 18.0, Y = L.Y + 20.0;
			if (X + Sz.X > LocalSize.X - 8.0) X = L.X - Sz.X - 14.0;
			if (Y + Sz.Y > LocalSize.Y - 8.0) Y = L.Y - Sz.Y - 14.0;
			return FMargin(float(FMath::Max(4.0, X)), float(FMath::Max(4.0, Y)), 0.f, 0.f);
		}))
	[
		HoverTipW
	];

	// the eye (HUD hidden)
	O->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0, 16, 18, 0)[EyeButton()];
	return O;
}

TSharedRef<SWidget> SDmHud::Ticker()
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	return Panel(
		SNew(SButton).ButtonStyle(&S.FlatButton).ContentPadding(FMargin(2, 0))
		.OnClicked_Lambda([C]() { C->NarrowSheet = C->NarrowSheet == 2 ? 0 : 2; return FReply::Handled(); })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SNew(SDmIcon).Icon(FName(TEXT("scroll"))).Size(16.f).Color(S.Gold)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Lam([C]() { return C->Feed.Num() ? C->Feed.Last().Text : FString(TEXT("The chronicle is empty.")); }))
				.Font(S.Font(EDmFont::Serif, 14.f))
				.ColorAndOpacity(FSlateColor(S.Text))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]
		],
		FMargin(10.f, 6.f));
}

TSharedRef<SWidget> SDmHud::Sheet()
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	auto Title = [C]()
	{
		switch (C->NarrowSheet)
		{
		case 1: return FString(TEXT("Settlement"));
		case 2: return FString(TEXT("Chronicle"));
		default: return FString(TEXT("Overlays"));
		}
	};
	TSharedRef<SWidget> Body =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				Text(Lam(Title), EDmFont::Serif, 18.f, FSlateColor(S.Gold))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				RoundIconButton(FName(TEXT("close")), [C]() { if (C->NarrowSheet == 1) C->Control.Select(-1); C->NarrowSheet = 0; }, nullptr, 40.f)
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(0, 6, 0, 0)
		[
			SNew(SScrollBox).Style(&S.ScrollBox).ScrollBarStyle(&S.ScrollBar)
			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).Visibility_Lambda([C]() { return Vis(C->NarrowSheet == 1); })[SNew(SDmInspector).Context(C).Width(340.f)]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).Visibility_Lambda([C]() { return Vis(C->NarrowSheet == 2); })[SNew(SDmChronicleFeed).Context(C).Width(340.f).Slots(6)]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SVerticalBox).Visibility_Lambda([C]() { return Vis(C->NarrowSheet == 3); })
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[OverlaySwitch(true)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)[Legend()]
				]
			]
		];
	return SNew(SBox)
		.HeightOverride_Lambda([this]() { return FOptionalSize(float(LocalSize.Y * 0.45)); })
		.Visibility_Lambda([C]() { return Vis(C->NarrowSheet != 0 && HudOn(*C)); })
		[
			Panel(Body, FMargin(12.f, 8.f), &S.PanelSolid)
		];
}

TSharedRef<SWidget> SDmHud::BuildNarrow()
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	auto On = [C]() { return VisHT(HudOn(*C)); };
	TSharedRef<SWidget> HoverTipW = HoverTip();
	TSharedRef<SOverlay> O = SNew(SOverlay);
	O->AddSlot()
	[
		SNew(SBox).Visibility_Lambda([C]() { return (HudOn(*C) && C->bLabels) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		[
			SNew(SDmHudLabels).Context(C)
		]
	];
	// top bar: row 1 menu, Awe, year with age icon, pause, speed; row 2 small text
	O->AddSlot().HAlign(HAlign_Fill).VAlign(VAlign_Top).Padding(8, 8, 8, 0)
	[
		SNew(SVerticalBox).Visibility_Lambda(On)
		+ SVerticalBox::Slot().AutoHeight()
		[
			Panel(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[RoundIconButton(FName(TEXT("menu")), [C]() { C->Control.RequestMenu(); }, nullptr, 44.f, FDmHudIds::Menu)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)[AweCompact()]
					+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center).VAlign(VAlign_Center)[AgeBlock(true)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SpeedControls(true)]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(4, 4, 0, 2)[StatsRow(true)],
				FMargin(6.f, 5.f))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)[ArkBar()]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)[GuideCard(340.f)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Toasts()]
	];
	// bottom: armed chip, ticker, palette or a sheet
	O->AddSlot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom).Padding(8, 0, 8, 10)
	[
		SNew(SVerticalBox).Visibility_Lambda(On)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 6)
		[
			SNew(SBox).Visibility_Lambda([C]() { return Vis(C->NarrowSheet == 0); })[ArmedChip()]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(SHorizontalBox).Visibility_Lambda([C]() { return Vis(C->NarrowSheet == 0); })
			+ SHorizontalBox::Slot().FillWidth(1.f)[Ticker()]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)
			[
				RoundIconButton(FName(TEXT("layers")), [C]() { C->NarrowSheet = 3; }, [C]() { return C->M.Overlay != 0; }, 44.f)
			]
		]
		+ SVerticalBox::Slot().AutoHeight()[NarrowPalette()]
		+ SVerticalBox::Slot().AutoHeight()[Sheet()]
	];
	O->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 180, 0, 0)[Banner()];
	O->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Top)
		.Padding(TAttribute<FMargin>::CreateLambda([this, HoverTipW]()
		{
			const FVector2D L = TipPx / FMath::Max(0.01, double(Ctx->DpiToPx));
			const FVector2D Sz = HoverTipW->GetDesiredSize();
			const double X = FMath::Clamp(L.X - Sz.X * 0.5, 4.0, FMath::Max(4.0, LocalSize.X - Sz.X - 4.0));
			const double Y = FMath::Max(4.0, L.Y - Sz.Y - 24.0);
			return FMargin(float(X), float(Y), 0.f, 0.f);
		}))
	[
		HoverTipW
	];
	O->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0, 8, 8, 0)[EyeButton()];
	return O;
}
