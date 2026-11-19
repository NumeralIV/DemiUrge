// DEMIURGE — Game/UI/Screens/DmScreenWidgets.cpp : see DmScreenWidgets.h. Owner: C8 screens.

#include "Game/UI/Screens/DmScreenWidgets.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace DmUi
{
namespace
{
	// the row brush must outlive its widgets: a static, like the panel brush
	const FSlateBrush* RowBrush()
	{
		static FSlateRoundedBoxBrush B = DmStyle::Box(DmStyle::Ink2(0.55f), 8.f, DmStyle::Line(0.16f), 1.f);
		static float BuiltScale = DmStyle::Scale();
		if (BuiltScale != DmStyle::Scale())
		{
			BuiltScale = DmStyle::Scale();
			B = DmStyle::Box(DmStyle::Ink2(0.55f), 8.f, DmStyle::Line(0.16f), 1.f);
		}
		return &B;
	}
}

FText Plain(const FString& S)
{
	return FText::FromString(S);
}

FText Percent(float Value01)
{
	return FText::FromString(FString::Printf(TEXT("%d %%"), FMath::RoundToInt(Value01 * 100.f)));
}

TSharedRef<SWidget> Label(TAttribute<FText> Text, DmStyle::EFace Face, float Size, FLinearColor Color, ETextJustify::Type Justify)
{
	return SNew(STextBlock)
		.Text(Text)
		.Font(DmStyle::Font(Face, Size))
		.ColorAndOpacity(Color)
		.Justification(Justify)
		.AutoWrapText(true)
		.ShadowOffset(FVector2D(0.0, 1.0))
		.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.55f));
}

TSharedRef<SWidget> Heading(TAttribute<FText> Text, float Size, ETextJustify::Type Justify)
{
	return Label(Text, DmStyle::EFace::SerifBold, Size, DmStyle::GoldHi(), Justify);
}

TSharedRef<SWidget> Body(TAttribute<FText> Text, float Size)
{
	return Label(Text, DmStyle::EFace::Serif, Size, DmStyle::Text(0.94f));
}

TSharedRef<SWidget> Caption(TAttribute<FText> Text, float Size)
{
	return SNew(STextBlock)
		.Text(Text)
		.Font(DmStyle::Font(DmStyle::EFace::Sans, Size))
		.ColorAndOpacity(DmStyle::Dim())
		.AutoWrapText(true);
}

TSharedRef<SWidget> Eyebrow(TAttribute<FText> Text)
{
	return SNew(STextBlock)
		.Text(Text)
		.Font(DmStyle::Font(DmStyle::EFace::SansBold, 12.f))
		.ColorAndOpacity(DmStyle::Gold(0.85f));
}

TSharedRef<SWidget> Rule(float Width)
{
	if (Width > 0.f)
	{
		return SNew(SBox)
			.WidthOverride(DmStyle::Px(Width))
			.HeightOverride(DmStyle::Px(2.f))
			[
				SNew(SBorder).BorderImage(DmStyle::RuleBrush())
			];
	}
	return SNew(SBox)
		.HeightOverride(DmStyle::Px(2.f))
		[
			SNew(SBorder).BorderImage(DmStyle::RuleBrush())
		];
}

TSharedRef<SWidget> Gap(float Height)
{
	return SNew(SSpacer).Size(FVector2D(1.0, DmStyle::Px(Height)));
}

TSharedRef<SWidget> HGap(float Width)
{
	return SNew(SSpacer).Size(FVector2D(DmStyle::Px(Width), 1.0));
}

TSharedRef<SWidget> Panel(TSharedRef<SWidget> Content, float MaxWidth, float Pad)
{
	if (DmStyle::IsCompact())
	{
		// a phone: the panel takes the window's width (its content alone would size it to the longest word)
		return SNew(SBorder)
			.BorderImage(DmStyle::Panel())
			.Padding(DmStyle::Px(Pad))
			[
				SNew(SBox)
				.WidthOverride(DmStyle::Px(DmStyle::FitWidth(MaxWidth, 16.f + Pad)))
				[
					Content
				]
			];
	}
	return SNew(SBorder)
		.BorderImage(DmStyle::Panel())
		.Padding(DmStyle::Px(Pad))
		[
			SNew(SBox)
			.MaxDesiredWidth(DmStyle::Px(MaxWidth))
			[
				Content
			]
		];
}

TSharedRef<SWidget> Row(TSharedRef<SWidget> Content, float Pad)
{
	return SNew(SBorder)
		.BorderImage(RowBrush())
		.Padding(DmStyle::Px(Pad))
		[
			Content
		];
}

TSharedRef<SWidget> Button(TSharedRef<SWidget> Content, FOnClicked OnClick, DmStyle::EButton Kind, float MinWidth, TAttribute<bool> bEnabled)
{
	return SNew(SButton)
		.ButtonStyle(&DmStyle::Button(Kind))
		.ContentPadding(FMargin(DmStyle::Px(18.f), DmStyle::Px(9.f)))
		.IsEnabled(bEnabled)
		.OnClicked(OnClick)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.MinDesiredWidth(DmStyle::Px(MinWidth))
			[
				Content
			]
		];
}

TSharedRef<SWidget> TextButton(TAttribute<FText> Text, FOnClicked OnClick, DmStyle::EButton Kind, float MinWidth, TAttribute<bool> bEnabled)
{
	return Button(
		SNew(STextBlock)
			.Text(Text)
			.Font(DmStyle::Font(DmStyle::EFace::Serif, 18.f))
			.Justification(ETextJustify::Center),
		OnClick, Kind, MinWidth, bEnabled);
}

TSharedRef<SWidget> Chip(TAttribute<FText> Text, bool bOn, FOnClicked OnClick, float MinWidth)
{
	return SNew(SButton)
		.ButtonStyle(&DmStyle::Button(bOn ? DmStyle::EButton::ChipOn : DmStyle::EButton::Chip))
		.ContentPadding(FMargin(DmStyle::Px(14.f), DmStyle::Px(7.f)))
		.OnClicked(OnClick)
		[
			SNew(SBox)
			.MinDesiredWidth(DmStyle::Px(MinWidth))
			[
				SNew(STextBlock)
				.Text(Text)
				.Font(DmStyle::Font(DmStyle::EFace::Sans, 14.f))
				.Justification(ETextJustify::Center)
			]
		];
}

TSharedRef<SWidget> Slider(TFunction<float()> Get, TFunction<void(float)> Set, float Min, float Max, FSimpleDelegate OnRelease)
{
	return SNew(SSlider)
		.Style(&DmStyle::Slider())
		.MinValue(Min)
		.MaxValue(Max)
		.Value(TAttribute<float>::Create(TAttribute<float>::FGetter::CreateLambda([Get]() { return Get(); })))
		.OnValueChanged(FOnFloatValueChanged::CreateLambda([Set](float V) { Set(V); }))
		.OnMouseCaptureEnd(OnRelease);
}

TSharedRef<SWidget> SliderRow(const FText& Name, TFunction<float()> Get, TFunction<void(float)> Set, float Min, float Max, TFunction<FText()> ValueText, FSimpleDelegate OnRelease)
{
	if (DmStyle::IsCompact())
	{
		// a phone: the name and the value on one line, the slider full width below
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Name)
					.Font(DmStyle::Font(DmStyle::EFace::Sans, 15.f))
					.ColorAndOpacity(DmStyle::Text(0.92f))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([ValueText]() { return ValueText(); })))
					.Font(DmStyle::Font(DmStyle::EFace::SansBold, 14.f))
					.ColorAndOpacity(DmStyle::Gold())
					.Justification(ETextJustify::Right)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, DmStyle::Px(6.f), 0.f, DmStyle::Px(4.f))
			[
				Slider(Get, Set, Min, Max, OnRelease)
			];
	}
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, DmStyle::Px(14.f), 0.f)
		[
			SNew(SBox)
			.WidthOverride(DmStyle::Px(DmStyle::IsCompact() ? 108.f : 150.f))
			[
				SNew(STextBlock)
				.Text(Name)
				.Font(DmStyle::Font(DmStyle::EFace::Sans, 15.f))
				.ColorAndOpacity(DmStyle::Text(0.92f))
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		[
			Slider(Get, Set, Min, Max, OnRelease)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(DmStyle::Px(14.f), 0.f, 0.f, 0.f)
		[
			SNew(SBox)
			.WidthOverride(DmStyle::Px(DmStyle::IsCompact() ? 52.f : 64.f))
			[
				SNew(STextBlock)
				.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([ValueText]() { return ValueText(); })))
				.Font(DmStyle::Font(DmStyle::EFace::SansBold, 14.f))
				.ColorAndOpacity(DmStyle::Gold())
				.Justification(ETextJustify::Right)
			]
		];
}

TSharedRef<SWidget> Field(TAttribute<FText> Text, const FText& Hint, FOnTextChanged OnChanged, float MinWidth)
{
	return SNew(SBox)
		.MinDesiredWidth(DmStyle::Px(MinWidth))
		[
			SNew(SEditableTextBox)
			.Style(&DmStyle::TextField())
			.Font(DmStyle::Font(DmStyle::EFace::Sans, 17.f))
			.ForegroundColor(DmStyle::Text())
			.HintText(Hint)
			.Text(Text)
			.OnTextChanged(OnChanged)
			.Padding(FMargin(DmStyle::Px(12.f), DmStyle::Px(9.f)))
		];
}

TSharedRef<SWidget> Stat(const FText& Name, TAttribute<FText> Value, float ValueSize)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(Value)
			.Font(DmStyle::Font(DmStyle::EFace::SansBold, ValueSize))
			.ColorAndOpacity(DmStyle::GoldHi())
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(Name)
			.Font(DmStyle::Font(DmStyle::EFace::Sans, 12.f))
			.ColorAndOpacity(DmStyle::Dim())
		];
}

TSharedRef<SWidget> Scroll(TSharedRef<SWidget> Content)
{
	return SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			Content
		];
}

} // namespace DmUi
