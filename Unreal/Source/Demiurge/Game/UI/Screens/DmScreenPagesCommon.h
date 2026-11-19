// DEMIURGE — Game/UI/Screens/DmScreenPagesCommon.h : small helpers the pages share (click handlers,
// live attributes, the scrolling modal frame). Owner: C8 screens. Internal to the screens module.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Game/UI/Screens/DmScreenWidgets.h"

namespace DmPagesCommon
{
	/** A click handler that runs Fn and handles the click. */
	inline FOnClicked Click(TFunction<void()> Fn)
	{
		return FOnClicked::CreateLambda([Fn]() -> FReply
		{
			Fn();
			return FReply::Handled();
		});
	}

	/** A text-changed handler. */
	inline FOnTextChanged OnText(TFunction<void(const FString&)> Fn)
	{
		return FOnTextChanged::CreateLambda([Fn](const FText& T)
		{
			Fn(T.ToString());
		});
	}

	/** A text attribute that reads Fn on every frame (live numbers and captions). */
	inline TAttribute<FText> Dyn(TFunction<FText()> Fn)
	{
		return TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([Fn]() { return Fn(); }));
	}

	inline TAttribute<bool> DynBool(TFunction<bool()> Fn)
	{
		return TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([Fn]() { return Fn(); }));
	}

	/** A full-screen frame for a panel: scrolls when the panel is taller than the window (phones). */
	inline TSharedRef<SWidget> Modal(TSharedRef<SWidget> Content, float VPad = 48.f)
	{
		return SNew(SScrollBox)
			+ SScrollBox::Slot()
			.HAlign(HAlign_Center)
			.Padding(DmStyle::Px(16.f), DmStyle::Px(VPad), DmStyle::Px(16.f), DmStyle::Px(VPad))
			[
				Content
			];
	}

	/** A row of buttons: side by side, or one per line on a phone. */
	inline TSharedRef<SWidget> ButtonRow(const TArray<TSharedRef<SWidget>>& Buttons)
	{
		if (DmStyle::IsCompact())
		{
			TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
			for (const TSharedRef<SWidget>& B : Buttons)
			{
				V->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, DmStyle::Px(10.f))[ B ];
			}
			return V;
		}
		TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox);
		for (const TSharedRef<SWidget>& B : Buttons)
		{
			H->AddSlot().AutoWidth().Padding(0.f, 0.f, DmStyle::Px(12.f), 0.f)[ B ];
		}
		return H;
	}

	/** The Back button of a page. */
	inline TSharedRef<SWidget> BackButton(TFunction<void()> OnBack, const FText& Label = FText::FromString(TEXT("Back")))
	{
		return DmUi::TextButton(Label, Click(OnBack), DmStyle::EButton::Ghost, 170.f);
	}

	/** One line of feedback (saved, deleted, could not load); empty when there is none. */
	inline TSharedRef<SWidget> MessageLine(const FString& Text)
	{
		return DmUi::Caption(FText::FromString(Text), 14.f);
	}

	/** A horizontal row of equal-height items with gaps between them. */
	inline TSharedRef<SWidget> Gap(float Pixels)
	{
		return DmUi::HGap(Pixels);
	}
}
