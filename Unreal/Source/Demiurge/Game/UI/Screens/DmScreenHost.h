// DEMIURGE — Game/UI/Screens/DmScreenHost.h : the widget that shows the screens above the game (one per
// game instance, added to the viewport by UDmScreenSubsystem::EnsureHost). Owner: C8 screens.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UDmScreenSubsystem;
class SBox;

class SDmScreenHost : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDmScreenHost) {}
		SLATE_ARGUMENT(UDmScreenSubsystem*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Replaces the page (nullptr hides the host). bFadeIn: the new page fades in (a new screen); a rebuild of the
	 *  same screen swaps its widgets at once. */
	void SetPage(TSharedPtr<SWidget> NewPage, bool bPassThrough, bool bScrim, const FSlateBrush* ScrimBrush, bool bFadeIn = true);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	EVisibility GetRootVisibility() const;
	EVisibility GetScrimVisibility() const;
	EVisibility GetPageVisibility() const;
	const FSlateBrush* GetScrimBrush() const;
	FLinearColor GetFade() const;

	UDmScreenSubsystem* Owner = nullptr;
	TSharedPtr<SBox> PageBox;
	TSharedPtr<SWidget> Page;
	const FSlateBrush* Scrim = nullptr;
	bool bHasScrim = false;
	bool bPassThrough = false;
	float Fade = 1.f;
	bool bFocusPending = false;
};
