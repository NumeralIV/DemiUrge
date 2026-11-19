// DEMIURGE — Game/UI/Screens/DmScreenHost.cpp : see DmScreenHost.h. Owner: C8 screens.

#include "Game/UI/Screens/DmScreenHost.h"

#include "Game/UI/Screens/DmScreenSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleDefaults.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SNullWidget.h"

void SDmScreenHost::Construct(const FArguments& InArgs)
{
	Owner = InArgs._Owner;
	ChildSlot
	[
		SNew(SOverlay)
		.Visibility(this, &SDmScreenHost::GetRootVisibility)
		+ SOverlay::Slot()
		[
			// the scrim: a dim veil under the page (or a gradient); never blocks the planet on the title
			SNew(SBorder)
			.BorderImage(this, &SDmScreenHost::GetScrimBrush)
			.Visibility(this, &SDmScreenHost::GetScrimVisibility)
			.Padding(0.f)
		]
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FStyleDefaults::GetNoBrush())
			.ColorAndOpacity(this, &SDmScreenHost::GetFade)
			.Visibility(this, &SDmScreenHost::GetPageVisibility)
			.Padding(0.f)
			[
				SAssignNew(PageBox, SBox)
			]
		]
	];
}

void SDmScreenHost::SetPage(TSharedPtr<SWidget> NewPage, bool bInPassThrough, bool bScrim, const FSlateBrush* ScrimBrush, bool bFadeIn)
{
	Page = NewPage;
	bPassThrough = bInPassThrough;
	bHasScrim = bScrim;
	Scrim = ScrimBrush;
	if (PageBox.IsValid())
	{
		PageBox->SetContent(Page.IsValid() ? Page.ToSharedRef() : SNullWidget::NullWidget);
	}
	if (bFadeIn) Fade = 0.f;
	bFocusPending = Page.IsValid();
}

void SDmScreenHost::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (!Owner) return;

	Fade = FMath::Min(1.f, Fade + InDeltaTime * 5.f);
	Owner->TickScreens(InDeltaTime);
	if (Owner->bRefreshPending)
	{
		Owner->bRefreshPending = false;
		Owner->RebuildHost();
	}
	if (bFocusPending && Page.IsValid())
	{
		bFocusPending = false;
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this));
	}
}

FReply SDmScreenHost::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (Owner && Page.IsValid() && InKeyEvent.GetKey() == EKeys::Escape)
	{
		Owner->Back();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

EVisibility SDmScreenHost::GetRootVisibility() const
{
	if (!Owner || !Owner->IsOpen() || !Page.IsValid()) return EVisibility::Collapsed;
	return EVisibility::SelfHitTestInvisible;
}

EVisibility SDmScreenHost::GetScrimVisibility() const
{
	if (!bHasScrim) return EVisibility::Collapsed;
	return bPassThrough ? EVisibility::HitTestInvisible : EVisibility::Visible;
}

EVisibility SDmScreenHost::GetPageVisibility() const
{
	return bPassThrough ? EVisibility::SelfHitTestInvisible : EVisibility::Visible;
}

const FSlateBrush* SDmScreenHost::GetScrimBrush() const
{
	return Scrim ? Scrim : FStyleDefaults::GetNoBrush();
}

FLinearColor SDmScreenHost::GetFade() const
{
	return FLinearColor(1.f, 1.f, 1.f, Fade);
}
