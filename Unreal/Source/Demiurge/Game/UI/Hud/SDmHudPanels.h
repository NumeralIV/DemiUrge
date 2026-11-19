// DEMIURGE — Game/UI/Hud/SDmHudPanels.h : the panels of old SPEC-UI 4b (DM.Panels). Owner: C7 hud.
//
//   SDmInspector       #pan-inspector: the selected settlement and its culture, edicts ([data-edict])
//   SDmChronicleFeed   the latest chronicle entries fading in at the side; click = fly there;
//                      #pan-chronicle-open opens the book
//   SDmChronicleBook   #pan-chronicle: every entry, newest first, filter by culture and by weight
//   SDmNowStrip        #pan-now: wars, plague, famine, prayers, disasters; click flies and cycles
//   SDmHudLabels       #labels: settlement names (DmHudLabels::Update computes them every frame)
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class FDmHudContext;
class SBox;
class SVerticalBox;
class APlayerController;
class UDmSimSubsystem;

class DEMIURGE_API SDmInspector : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDmInspector) : _Width(380.f) {}
		SLATE_ARGUMENT(TSharedPtr<FDmHudContext>, Context)
		SLATE_ARGUMENT(float, Width)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& G, const double Now, const float Dt) override;
private:
	TSharedPtr<FDmHudContext> Ctx;
	TSharedPtr<SBox> Body;
	float Width = 380.f;
	int32 Shown = -2;
	uint32 Sig = 0;
	double LastBuild = 0;
	void Build();
};

class DEMIURGE_API SDmChronicleFeed : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDmChronicleFeed) : _Width(360.f), _Slots(5) {}
		SLATE_ARGUMENT(TSharedPtr<FDmHudContext>, Context)
		SLATE_ARGUMENT(float, Width)
		SLATE_ARGUMENT(int32, Slots)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs);
private:
	TSharedPtr<FDmHudContext> Ctx;
};

class DEMIURGE_API SDmChronicleBook : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDmChronicleBook) : _Width(760.f), _Height(780.f) {}
		SLATE_ARGUMENT(TSharedPtr<FDmHudContext>, Context)
		SLATE_ARGUMENT(float, Width)
		SLATE_ARGUMENT(float, Height)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& G, const double Now, const float Dt) override;
private:
	TSharedPtr<FDmHudContext> Ctx;
	TSharedPtr<SVerticalBox> List;
	TSharedPtr<SBox> Filters;
	float Width = 760.f, Height = 780.f;
	uint32 Sig = 0;
	void BuildList();
	void BuildFilters();
};

class DEMIURGE_API SDmNowStrip : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDmNowStrip) : _Width(300.f) {}
		SLATE_ARGUMENT(TSharedPtr<FDmHudContext>, Context)
		SLATE_ARGUMENT(float, Width)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs);
private:
	TSharedPtr<FDmHudContext> Ctx;
};

class DEMIURGE_API SDmHudLabels : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDmHudLabels) {}
		SLATE_ARGUMENT(TSharedPtr<FDmHudContext>, Context)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs) { Ctx = InArgs._Context; SetVisibility(EVisibility::HitTestInvisible); }
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(8, 8); }
private:
	TSharedPtr<FDmHudContext> Ctx;
};

namespace DmHudLabels
{
	/** Project, rank, de-overlap and fade the labels (every frame). Dt in seconds. */
	DEMIURGE_API void Update(FDmHudContext& Ctx, const UDmSimSubsystem& Sim, const APlayerController* PC, float Dt);
}
