// DEMIURGE — Game/UI/Screens/DmScreenWidgets.h : the widget kit of the screens (SPEC-UI 0 components:
// .dm-panel, .dm-btn, .dm-btn.primary, .dm-rule, .dm-bar, option chips, sliders, text fields).
// Owner: C8 screens. Helpers return plain Slate widgets; pages compose them.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Game/UI/Screens/DmScreenStyle.h"

namespace DmUi
{
	FText Plain(const FString& S);
	FText Percent(float Value01);

	/** Serif or sans text; the colour is fixed (use the default for the button foreground). */
	TSharedRef<SWidget> Label(TAttribute<FText> Text, DmStyle::EFace Face, float Size, FLinearColor Color, ETextJustify::Type Justify = ETextJustify::Left);
	/** Serif, gold-hi, large: page titles. */
	TSharedRef<SWidget> Heading(TAttribute<FText> Text, float Size = 34.f, ETextJustify::Type Justify = ETextJustify::Left);
	/** Serif body text in the text colour; wraps. */
	TSharedRef<SWidget> Body(TAttribute<FText> Text, float Size = 17.f);
	/** Sans, dim, small: captions and hints; wraps. */
	TSharedRef<SWidget> Caption(TAttribute<FText> Text, float Size = 13.f);
	/** Small gold line of sans text (eyebrows: section names). */
	TSharedRef<SWidget> Eyebrow(TAttribute<FText> Text);
	/** The gold gradient rule. Width 0 fills the row. */
	TSharedRef<SWidget> Rule(float Width = 0.f);
	TSharedRef<SWidget> Gap(float Height);
	TSharedRef<SWidget> HGap(float Width);

	/** The dark panel (.dm-panel), padded, at most MaxWidth wide (base px; scaled). */
	TSharedRef<SWidget> Panel(TSharedRef<SWidget> Content, float MaxWidth, float Pad = 28.f);
	/** A row inset in the panel: ink fill with a hairline (slot cards, list rows). */
	TSharedRef<SWidget> Row(TSharedRef<SWidget> Content, float Pad = 14.f);
	/** A pressable button around any content. */
	TSharedRef<SWidget> Button(TSharedRef<SWidget> Content, FOnClicked OnClick, DmStyle::EButton Kind = DmStyle::EButton::Ghost, float MinWidth = 0.f, TAttribute<bool> bEnabled = true);
	/** A button with one line of serif text. */
	TSharedRef<SWidget> TextButton(TAttribute<FText> Text, FOnClicked OnClick, DmStyle::EButton Kind = DmStyle::EButton::Ghost, float MinWidth = 0.f, TAttribute<bool> bEnabled = true);
	/** An option chip (sans): on / off state chosen when the page is built. */
	TSharedRef<SWidget> Chip(TAttribute<FText> Text, bool bOn, FOnClicked OnClick, float MinWidth = 0.f);
	/** A slider that reads and writes through callbacks; OnRelease fires when the drag ends. */
	TSharedRef<SWidget> Slider(TFunction<float()> Get, TFunction<void(float)> Set, float Min, float Max, FSimpleDelegate OnRelease = FSimpleDelegate());
	/** Name, slider and the value text, in one line (settings). */
	TSharedRef<SWidget> SliderRow(const FText& Name, TFunction<float()> Get, TFunction<void(float)> Set, float Min, float Max, TFunction<FText()> ValueText, FSimpleDelegate OnRelease = FSimpleDelegate());
	/** A single-line text field. */
	TSharedRef<SWidget> Field(TAttribute<FText> Text, const FText& Hint, FOnTextChanged OnChanged, float MinWidth = 0.f);
	/** A number over its name (planet card, statistics). */
	TSharedRef<SWidget> Stat(const FText& Name, TAttribute<FText> Value, float ValueSize = 30.f);
	/** Wraps a page's content in a vertical scroll box. */
	TSharedRef<SWidget> Scroll(TSharedRef<SWidget> Content);
}
