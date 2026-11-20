// DEMIURGE — Game/UI/Hud/SDmHudGuide.cpp : the remember map and card, the guidance thread. Owner: flow-hud.

#include "Game/UI/Hud/SDmHudGuide.h"

#include "Brushes/SlateColorBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"

#include "Game/UI/Hud/DmHudIcons.h"
#include "Game/UI/Hud/DmHudModel.h"
#include "Game/UI/Hud/DmHudStyle.h"
#include "Game/UI/Hud/SDmHudWidgets.h"

namespace
{
	using namespace DmHudUi;

	EVisibility Vis(bool b) { return b ? EVisibility::Visible : EVisibility::Collapsed; }

	const FSlateBrush* White()
	{
		static FSlateColorBrush B(FLinearColor::White);
		return &B;
	}

	FVector2f Measure(const FString& S, const FSlateFontInfo& F)
	{
		if (!FSlateApplication::IsInitialized()) return FVector2f(S.Len() * F.Size * 0.6f, F.Size * 1.3f);
		return FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(S, F));
	}

	void FillRect(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, float X0, float Y0, float X1, float Y1, const FLinearColor& C)
	{
		if (X1 <= X0 || Y1 <= Y0) return;
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(X1 - X0, Y1 - Y0), FSlateLayoutTransform(FVector2f(X0, Y0))), White(), ESlateDrawEffect::None, C);
	}

	void Label(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FString& Text, const FSlateFontInfo& F, float Cx, float Cy, const FLinearColor& C, float MinX, float MaxX)
	{
		const FVector2f M = Measure(Text, F);
		const float X = FMath::Clamp(Cx - M.X * 0.5f, MinX, FMath::Max(MinX, MaxX - M.X));
		FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(M, FSlateLayoutTransform(FVector2f(X, Cy - M.Y * 0.5f))), Text, F, ESlateDrawEffect::None, C);
	}
}

// ================================================================== the remember map

void SDmRegardMap::Construct(const FArguments& A)
{
	Ctx = A._Context;
	Width = A._Width;
	Height = A._Height;
	bLabels = A._Labels;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SDmRegardMap::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	const FDmHudModel& M = Ctx->M;
	const float Op = Style.GetColorAndOpacityTint().A;
	auto A = [Op](FLinearColor C, float Al) { C.A = Al * Op; return C; };
	const FVector2f Sz = FVector2f(G.GetLocalSize());
	const float W = Sz.X, H = Sz.Y;
	// verdict (society::memory: remembered deeds and today's love) -1..1 across, devotion 0..1 up
	auto X = [W](float Love) { return (FMath::Clamp(Love, -1.f, 1.f) + 1.f) * 0.5f * W; };
	auto Y = [H](float Dev) { return (1.f - FMath::Clamp(Dev, 0.f, 1.f)) * H; };
	const float Xf = X(M.TFearedLove), Xb = X(M.TBelovedLove), Yb = Y(M.TBelovedDev), Yf = Y(M.TForgottenDev);

	// regions: parting (the field), feared (left), forgotten (low), beloved (high and kind)
	FillRect(Out, Layer, G, 0, 0, W, H, A(S.Ink3, 0.92f));
	FillRect(Out, Layer + 1, G, 0, 0, W, H, A(S.Gold, 0.07f));
	FillRect(Out, Layer + 1, G, 0, 0, Xf, H, A(S.Bad, 0.26f));
	FillRect(Out, Layer + 1, G, Xf, Yf, W, H, A(S.Water, 0.16f));
	FillRect(Out, Layer + 1, G, Xb, 0, W, Yb, A(S.Good, 0.26f));
	// region edges
	const FLinearColor Edge = A(S.Text, bLabels ? 0.22f : 0.18f);
	FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), TArray<FVector2f>{ FVector2f(Xf, 0), FVector2f(Xf, H) }, ESlateDrawEffect::None, Edge, true, 1.f);
	FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), TArray<FVector2f>{ FVector2f(Xf, Yf), FVector2f(W, Yf) }, ESlateDrawEffect::None, Edge, true, 1.f);
	FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), TArray<FVector2f>{ FVector2f(Xb, 0), FVector2f(Xb, Yb), FVector2f(W, Yb) }, ESlateDrawEffect::None, Edge, true, 1.f);
	// frame
	FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), TArray<FVector2f>{ FVector2f(0.5f, 0.5f), FVector2f(W - 0.5f, 0.5f), FVector2f(W - 0.5f, H - 0.5f), FVector2f(0.5f, H - 0.5f), FVector2f(0.5f, 0.5f) },
		ESlateDrawEffect::None, A(S.Gold, 0.45f), true, 1.f);

	if (bLabels)
	{
		const FSlateFontInfo F = S.Font(EDmFont::Serif, 15.f);
		const FDmHudStyle& St = S;
		auto LabelCol = [&](dm::Mood Md, const FLinearColor& Base) { return A(M.Mood == Md && !M.bWatched ? St.GoldHi : Base, M.Mood == Md && !M.bWatched ? 1.f : 0.8f); };
		Label(Out, Layer + 3, G, TEXT("Beloved"), F, (Xb + W) * 0.5f, Yb * 0.42f, LabelCol(dm::Mood::Beloved, S.Good), Xb + 4.f, W - 4.f);
		Label(Out, Layer + 3, G, TEXT("Feared"), F, Xf * 0.5f, H * 0.5f, LabelCol(dm::Mood::Feared, S.Bad), 4.f, Xf - 4.f);
		Label(Out, Layer + 3, G, TEXT("Forgotten"), F, (Xf + W) * 0.5f, (Yf + H) * 0.5f, LabelCol(dm::Mood::Forgotten, S.Water), Xf + 4.f, W - 4.f);
		Label(Out, Layer + 3, G, TEXT("In parting"), F, (Xf + Xb) * 0.5f, (Yb + Yf) * 0.5f, LabelCol(dm::Mood::Parting, S.Text), Xf + 4.f, Xb - 4.f);
		const FSlateFontInfo Small = S.Font(EDmFont::Sans, 11.f);
		Label(Out, Layer + 3, G, TEXT("cruel"), Small, 22.f, H - 9.f, A(S.Dim, 0.85f), 3.f, W);
		Label(Out, Layer + 3, G, TEXT("kind"), Small, W - 18.f, H - 9.f, A(S.Dim, 0.85f), 0.f, W - 3.f);
	}

	if (M.RegardCulture < 0 || M.bGenesis) return Layer + 3;

	// the trail: where the asked culture has stood (one sample a second), fading into the past
	const TArray<FVector2f>& T = Ctx->RegardTrail;
	if (T.Num() >= 2)
	{
		for (int32 I = 1; I < T.Num(); ++I)
		{
			const float K = float(I) / float(T.Num());
			TArray<FVector2f> Seg = { FVector2f(X(T[I - 1].X), Y(T[I - 1].Y)), FVector2f(X(T[I].X), Y(T[I].Y)) };
			FSlateDrawElement::MakeLines(Out, Layer + 4, G.ToPaintGeometry(), Seg, ESlateDrawEffect::None, A(S.GoldHi, 0.08f + 0.5f * K), true, bLabels ? 1.6f : 1.f);
		}
	}
	// the dot: the culture's colour in an ink ring; hollow while they have not seen the god act
	const float R = bLabels ? 7.f : 4.5f;
	const FVector2f P(X(M.RegardLove), Y(M.RegardDevotion));
	FSlateDrawElement::MakeBox(Out, Layer + 5, G.ToPaintGeometry(FVector2f(2 * R + 4, 2 * R + 4), FSlateLayoutTransform(P - FVector2f(R + 2, R + 2))), &S.Dot, ESlateDrawEffect::None, A(S.Ink, 0.95f));
	if (M.bWatched)
	{
		TArray<FVector2f> Ring;
		for (int32 I = 0; I <= 24; ++I) { const float An = 2.f * PI * I / 24.f; Ring.Add(P + FVector2f(FMath::Cos(An), FMath::Sin(An)) * (R - 1.f)); }
		FSlateDrawElement::MakeLines(Out, Layer + 6, G.ToPaintGeometry(), Ring, ESlateDrawEffect::None, A(S.Dim, 1.f), true, 1.4f);
	}
	else
	{
		FSlateDrawElement::MakeBox(Out, Layer + 6, G.ToPaintGeometry(FVector2f(2 * R, 2 * R), FSlateLayoutTransform(P - FVector2f(R, R))), &S.Dot, ESlateDrawEffect::None, A(M.RegardColor, 1.f));
	}
	return Layer + 6;
}

// ================================================================== the remember card

void SDmRememberCard::Construct(const FArguments& A)
{
	Ctx = A._Context;
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	const float Wd = A._Width;
	const float Inner = Wd - 32.f;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Caption(TEXT("How they will remember you"), S.Dim)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(26).HeightOverride(26)
			[
				SNew(SButton).ButtonStyle(&S.Button).ContentPadding(FMargin(0)).HAlign(HAlign_Center).VAlign(VAlign_Center)
				.OnClicked_Lambda([C]() { C->bRememberOpen = false; return FReply::Handled(); })
				[
					SNew(SDmIcon).Icon(FName(TEXT("close"))).Size(13.f).Color(S.Dim)
				]
			]
		]
	];
	V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 3, 9, 0)
		[
			SNew(SDmIcon).Size(17.f).Icon_Lambda([C]() { return C->M.RegardIcon; })
			.Color_Lambda([C, &S]() { return C->M.bWatched ? S.Dim : (C->M.Mood == dm::Mood::Feared ? S.Bad : (C->M.Mood == dm::Mood::Beloved ? S.Good : S.Gold)); })
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Text(Lam([C]() { return C->M.RegardLine; }), EDmFont::Serif, 16.5f, FSlateColor(S.Text), Inner - 30.f)]
			// when their memory, not the map, decides (silence, both hands, answered prayers, the Ark): say so
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
			[
				SNew(SBox).Visibility_Lambda([C]() { return Vis(!C->M.RegardWhy.IsEmpty()); })
				[
					Text(Lam([C]() { return C->M.RegardWhy; }), EDmFont::SerifItalic, 14.f, FSlateColor(S.Dim), Inner - 30.f)
				]
			]
		]
	];
	V->AddSlot().AutoHeight().Padding(0, 10, 0, 0).HAlign(HAlign_Center)
	[
		SNew(SDmRegardMap).Context(C).Width(Inner).Height(150.f).Labels(true)
	];
	V->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Text(FString(TEXT("Across: what they remember of your deeds, and how they feel today, from cruel to kind.")), EDmFont::Sans, 12.f, S.Dim, Inner)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)[Text(FString(TEXT("Up: devotion, how much they believe in you.")), EDmFont::Sans, 12.f, S.Dim)]
	];
	V->AddSlot().AutoHeight().Padding(0, 10, 0, 0)
	[
		Text(FString(TEXT("Every miracle they see makes them believe a little more. Kind deeds and answered prayers are remembered for centuries; "
			"wrath is remembered too, and a god who is both is parted from uneasily; long silence lets them forget. Once they build the Ark it is too late to change what they remember.")),
			EDmFont::Serif, 14.5f, S.Text, Inner)
	];
	ChildSlot[Panel(SNew(SBox).WidthOverride(Wd - 28.f)[V], FMargin(14.f, 10.f), &S.PanelHi)];
}

// ================================================================== the guidance thread

namespace
{
	/** One dot per step: done (gold), current (a ring), to come (dim). */
	class SDmGuideDots : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SDmGuideDots) {}
			SLATE_ARGUMENT(TSharedPtr<FDmHudContext>, Context)
		SLATE_END_ARGS()
		void Construct(const FArguments& A) { Ctx = A._Context; SetVisibility(EVisibility::HitTestInvisible); }
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(FDmHudContext::GuideStepCount * 14.0, 10.0); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const override
		{
			const FDmHudStyle& S = FDmHudStyle::Get();
			const float Op = Style.GetColorAndOpacityTint().A;
			for (int32 I = 0; I < FDmHudContext::GuideStepCount; ++I)
			{
				const FVector2f P(7.f + 14.f * I, 5.f);
				const bool bDone = (Ctx->GuideDone & (1u << I)) != 0;
				const bool bCur = I == Ctx->GuideCurrent;
				FLinearColor Col = bDone ? S.Gold : (bCur ? S.GoldHi : S.Dim);
				Col.A = (bDone || bCur ? 1.f : 0.45f) * Op;
				const float R = bCur ? 4.f : 3.f;
				if (bCur && !bDone)
				{
					TArray<FVector2f> Ring;
					for (int32 K = 0; K <= 16; ++K) { const float An = 2.f * PI * K / 16.f; Ring.Add(P + FVector2f(FMath::Cos(An), FMath::Sin(An)) * R); }
					FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Ring, ESlateDrawEffect::None, Col, true, 1.3f);
				}
				else FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(2 * R, 2 * R), FSlateLayoutTransform(P - FVector2f(R, R))), &S.Dot, ESlateDrawEffect::None, Col);
			}
			return Layer;
		}
	private:
		TSharedPtr<FDmHudContext> Ctx;
	};
}

void SDmGuideThread::Construct(const FArguments& A)
{
	Ctx = A._Context;
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	const float Wd = A._Width;
	const float Inner = Wd - 28.f;
	auto Step = [C]() -> const FDmGuideStep& { return FDmHudContext::GuideStep(C->GuideCurrent); };
	auto SmallBtn = [&S](FName Icon, TFunction<void()> Click) -> TSharedRef<SWidget>
	{
		return SNew(SBox).WidthOverride(24).HeightOverride(24)
		[
			SNew(SButton).ButtonStyle(&S.FlatButton).ContentPadding(FMargin(0)).HAlign(HAlign_Center).VAlign(VAlign_Center)
			.OnClicked_Lambda([Click]() { Click(); return FReply::Handled(); })
			[
				SNew(SDmIcon).Icon(Icon).Size(12.f).Color(S.Dim)
			]
		];
	};
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	// header: "First steps 3 of 7" (or, folded, the step's title), fold and close
	V->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 7, 0)
		[
			SNew(SDmIcon).Icon(FName(TEXT("star"))).Size(13.f).Color(S.Gold)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(S.Font(EDmFont::SansSemi, 11.5f))
			.ColorAndOpacity(FSlateColor(S.Dim))
			// (verifier) folded, the counter gives its room to the step's title (it was clipped: "Answer a praye")
			.Text(Lam([C]() { return FString::Printf(TEXT("%s%d / %d"), C->bGuideCollapsed ? TEXT("") : TEXT("FIRST STEPS  "), FMath::Min(C->GuideCurrent + 1, FDmHudContext::GuideStepCount), FDmHudContext::GuideStepCount); }))
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(8, 0, 4, 0)
		[
			SNew(SBox).Visibility_Lambda([C]() { return Vis(C->bGuideCollapsed); })
			[
				SNew(STextBlock)
				.Font(S.Font(EDmFont::Serif, 14.5f))
				.ColorAndOpacity(FSlateColor(S.Gold))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.Text(Lam([C]() { return FString(FDmHudContext::GuideStep(C->GuideCurrent).Title); }))
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).Tag(FName(TEXT("guide-fold")))
			[
				SNew(SButton).ButtonStyle(&S.FlatButton).ContentPadding(FMargin(0))
				.OnClicked_Lambda([C]() { C->bGuideCollapsed = !C->bGuideCollapsed; return FReply::Handled(); })
				[
					SNew(SBox).WidthOverride(24).HeightOverride(24).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(SDmIcon).Size(12.f).Color(S.Dim).Icon_Lambda([C]() { return C->bGuideCollapsed ? FName(TEXT("plus")) : FName(TEXT("minus")); })
					]
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 0, 0)
		[
			SNew(SBox).Tag(FName(TEXT("guide-close")))[SmallBtn(FName(TEXT("close")), [C]() { C->DismissGuide(); })]
		]
	];
	// the step just finished (a few seconds)
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
	[
		SNew(SHorizontalBox)
		.Visibility_Lambda([C]() { return Vis(!C->bGuideCollapsed && !C->GuideJustDone.IsEmpty() && C->Now - C->GuideJustDoneAt < 5.0); })
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)[SNew(SDmIcon).Icon(FName(TEXT("check"))).Size(14.f).Color(S.Good)]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Text(Lam([C]() { return C->GuideJustDone; }), EDmFont::SerifItalic, 14.f, FSlateColor(S.Good))]
	];
	// the current step
	V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)
	[
		SNew(SHorizontalBox)
		.Visibility_Lambda([C]() { return Vis(!C->bGuideCollapsed); })
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 2, 10, 0)
		[
			SNew(SDmIcon).Size(20.f).Color(S.GoldHi).Icon_Lambda([Step]() { return FName(Step().Icon); })
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Text(Lam([Step]() { return FString(Step().Title); }), EDmFont::Serif, 17.f, FSlateColor(S.Gold))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)[Text(Lam([Step]() { return FString(Step().Text); }), EDmFont::Serif, 14.5f, FSlateColor(S.Text), Inner - 34.f)]
		]
	];
	// the steps as dots (done: gold, current: ring)
	V->AddSlot().AutoHeight().Padding(30, 9, 0, 0)
	[
		SNew(SBox).Visibility_Lambda([C]() { return Vis(!C->bGuideCollapsed); })
		[
			SNew(SDmGuideDots).Context(C)
		]
	];
	TSharedRef<SWidget> P = Panel(SNew(SBox).WidthOverride(Wd - 28.f)[V], FMargin(14.f, 9.f, 10.f, 10.f));
	ChildSlot[P];
	SetTag(FName(TEXT("hud-guide")));
	// a new step fades in
	SetColorAndOpacity(TAttribute<FLinearColor>::CreateLambda([C]()
	{
		const float K = float(FMath::Clamp((C->Now - C->GuideStepBorn) / 0.6, 0.0, 1.0));
		return FLinearColor(1, 1, 1, 0.35f + 0.65f * K);
	}));
}
