// DEMIURGE — Game/UI/Hud/SDmHudGuide.h : player-clarity widgets (wave 3, design review findings 8 and 12).
// Owner: flow-hud.
//
//   SDmRegardMap      "how they will remember you": the memory's verdict across, devotion up, the four endings as regions
//                     (on the ending's own thresholds), a dot for the culture the ending would ask and a faint
//                     trail of where it has been. Small (no words) in the Awe panel; large (the ending names in
//                     their regions) in the remember card.
//   SDmRememberCard   the card under the Awe panel that the regard mark opens: the verdict in a sentence, the
//                     large map and what moves it, in plain words.
//   SDmGuideThread    the first-hour guidance thread: one step at a time (find your people, rain, the remember
//                     map, a prayer, a shrine, the chronicle, a prophet), each ticked off by doing it; it can be
//                     folded to one line or closed for good.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class FDmHudContext;

class DEMIURGE_API SDmRegardMap : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDmRegardMap) : _Width(46.f), _Height(30.f), _Labels(false) {}
		SLATE_ARGUMENT(TSharedPtr<FDmHudContext>, Context)
		SLATE_ARGUMENT(float, Width)
		SLATE_ARGUMENT(float, Height)
		SLATE_ARGUMENT(bool, Labels)
	SLATE_END_ARGS()
	void Construct(const FArguments& A);
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Width, Height); }
private:
	TSharedPtr<FDmHudContext> Ctx;
	float Width = 46.f, Height = 30.f;
	bool bLabels = false;
};

class DEMIURGE_API SDmRememberCard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDmRememberCard) : _Width(440.f) {}
		SLATE_ARGUMENT(TSharedPtr<FDmHudContext>, Context)
		SLATE_ARGUMENT(float, Width)
	SLATE_END_ARGS()
	void Construct(const FArguments& A);
private:
	TSharedPtr<FDmHudContext> Ctx;
};

class DEMIURGE_API SDmGuideThread : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDmGuideThread) : _Width(300.f) {}
		SLATE_ARGUMENT(TSharedPtr<FDmHudContext>, Context)
		SLATE_ARGUMENT(float, Width)
	SLATE_END_ARGS()
	void Construct(const FArguments& A);
private:
	TSharedPtr<FDmHudContext> Ctx;
};
