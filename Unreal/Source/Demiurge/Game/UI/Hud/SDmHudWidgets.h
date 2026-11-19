// DEMIURGE — Game/UI/Hud/SDmHudWidgets.h : small painted widgets and builders the HUD and panels share.
// Owner: C7 hud.
#pragma once

#include "CoreMinimal.h"
#include "Game/UI/Hud/DmHudStyle.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

/** Swallows pointer events so nothing over a HUD panel reaches the planet (old SPEC-UI 2). */
class DEMIURGE_API SDmBlocker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDmBlocker) : _BlockWheel(true) {}
		SLATE_ARGUMENT(bool, BlockWheel)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs) { bBlockWheel = InArgs._BlockWheel; ChildSlot[InArgs._Content.Widget]; }
	bool bBlockWheel = true;
	virtual void Tick(const FGeometry& G, const double, const float) override;
	virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
	virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
	virtual FReply OnMouseButtonDoubleClick(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
	virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent&) override { return bBlockWheel ? FReply::Handled() : FReply::Unhandled(); }
	virtual FReply OnTouchStarted(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
	virtual FReply OnTouchEnded(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
};

/** A progress ring (an arc from 12 o'clock, clockwise) with an optional icon inside. */
class DEMIURGE_API SDmRing : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDmRing) : _Progress(0.f), _Size(32.f), _Thickness(2.2f), _Color(FLinearColor::White), _Back(FLinearColor(1, 1, 1, 0.12f)), _Icon(NAME_None), _IconSize(16.f), _IconColor(FLinearColor::White) {}
		SLATE_ATTRIBUTE(float, Progress)
		SLATE_ARGUMENT(float, Size)
		SLATE_ARGUMENT(float, Thickness)
		SLATE_ATTRIBUTE(FLinearColor, Color)
		SLATE_ATTRIBUTE(FLinearColor, Back)
		SLATE_ATTRIBUTE(FName, Icon)
		SLATE_ARGUMENT(float, IconSize)
		SLATE_ATTRIBUTE(FLinearColor, IconColor)
	SLATE_END_ARGS()
	void Construct(const FArguments& A);
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Size, Size); }
private:
	TAttribute<float> Progress;
	float Size = 32.f, Thickness = 2.2f, IconSize = 16.f;
	TAttribute<FLinearColor> Color, Back, IconColor;
	TAttribute<FName> Icon;
};

/** The Awe orb: a glass sphere filled with gold to Awe / AweMax, a soft glow that brightens when Awe rises. */
class DEMIURGE_API SDmAweOrb : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDmAweOrb) : _Fill(0.f), _Size(56.f), _Glow(0.f) {}
		SLATE_ATTRIBUTE(float, Fill)
		SLATE_ARGUMENT(float, Size)
		SLATE_ATTRIBUTE(float, Glow)
	SLATE_END_ARGS()
	void Construct(const FArguments& A);
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Size, Size); }
private:
	TAttribute<float> Fill, Glow;
	float Size = 56.f;
};

/** A thin bar 0..1 (devotion, genesis progress, the Ark). */
class DEMIURGE_API SDmBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDmBar) : _Value(0.f), _Width(160.f), _Height(5.f), _Color(FLinearColor::White) {}
		SLATE_ATTRIBUTE(float, Value)
		SLATE_ARGUMENT(float, Width)
		SLATE_ARGUMENT(float, Height)
		SLATE_ATTRIBUTE(FLinearColor, Color)
	SLATE_END_ARGS()
	void Construct(const FArguments& A);
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Width, Height); }
private:
	TAttribute<float> Value;
	float Width = 160.f, Height = 5.f;
	TAttribute<FLinearColor> Color;
};

/** A legend gradient (overlays) with end captions drawn by the caller. */
class DEMIURGE_API SDmGradient : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDmGradient) : _Width(220.f), _Height(8.f) {}
		SLATE_ARGUMENT(TArray<FLinearColor>, Stops)
		SLATE_ARGUMENT(float, Width)
		SLATE_ARGUMENT(float, Height)
	SLATE_END_ARGS()
	void Construct(const FArguments& A) { Stops = A._Stops; Width = A._Width; Height = A._Height; }
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Width, Height); }
private:
	TArray<FLinearColor> Stops;
	float Width = 220.f, Height = 8.f;
};

/** A decorative rule with a diamond in the middle (banners, the chronicle book). */
class DEMIURGE_API SDmFlourish : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDmFlourish) : _Width(320.f), _Color(FLinearColor::White) {}
		SLATE_ARGUMENT(float, Width)
		SLATE_ATTRIBUTE(FLinearColor, Color)
	SLATE_END_ARGS()
	void Construct(const FArguments& A) { Width = A._Width; Color = A._Color; }
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Width, 14.f); }
private:
	float Width = 320.f;
	TAttribute<FLinearColor> Color;
};

/** The love <-> fear scale of the inspector: five labelled stops and a marker at love (-1..1). */
class DEMIURGE_API SDmLoveScale : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDmLoveScale) : _Love(0.f), _Width(300.f) {}
		SLATE_ATTRIBUTE(float, Love)
		SLATE_ARGUMENT(float, Width)
	SLATE_END_ARGS()
	void Construct(const FArguments& A) { Love = A._Love; Width = A._Width; }
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Width, 34.f); }
private:
	TAttribute<float> Love;
	float Width = 300.f;
};

/** A culture's colour dot with its initial as a sigil (no signal is colour-only). */
class DEMIURGE_API SDmSigil : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDmSigil) : _Size(16.f), _Color(FLinearColor::White) {}
		SLATE_ARGUMENT(float, Size)
		SLATE_ARGUMENT(FLinearColor, Color)
		SLATE_ARGUMENT(FString, Letter)
	SLATE_END_ARGS()
	void Construct(const FArguments& A) { Size = A._Size; Color = A._Color; Letter = A._Letter; }
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Size, Size); }
	/** Paint a sigil anywhere (labels). */
	static void PaintSigil(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Pos, float Size, const FLinearColor& Color, const FString& Letter, float Alpha);
private:
	float Size = 16.f;
	FLinearColor Color;
	FString Letter;
};

namespace DmHudUi
{
	/** Absolute rectangles of the HUD panels painted last frame (labels keep out of them). */
	DEMIURGE_API TArray<FSlateRect>& PanelRects();
	/** The pointer is over a HUD panel: set by the panels while Slate ticks; PointerOverPanelLast is read a frame later. */
	DEMIURGE_API bool& PointerOverPanelNow();
	DEMIURGE_API bool& PointerOverPanelLast();
	/** Plain text in a face, px size and colour. */
	DEMIURGE_API TSharedRef<STextBlock> Text(const TAttribute<FText>& InText, EDmFont Face, float Px, const TAttribute<FSlateColor>& Color, float WrapAt = 0.f);
	DEMIURGE_API TSharedRef<STextBlock> Text(const FString& InText, EDmFont Face, float Px, const FLinearColor& Color, float WrapAt = 0.f);
	/** Small caps-like heading (group names, section titles). */
	DEMIURGE_API TSharedRef<SWidget> Caption(const FString& InText, const FLinearColor& Color);
	/** A panel: ink background, hairline border, blocks the pointer. */
	DEMIURGE_API TSharedRef<SWidget> Panel(const TSharedRef<SWidget>& Content, const FMargin& Padding = FMargin(14.f, 10.f), const FSlateBrush* Brush = nullptr, bool bBlockWheel = true);
	/** Initial of a name (sigils). */
	DEMIURGE_API FString Initial(const FString& Name);
	/** Lambda attribute helpers. */
	inline TAttribute<FText> Lam(TFunction<FString()> F) { return TAttribute<FText>::CreateLambda([F]() { return FText::FromString(F()); }); }
}
