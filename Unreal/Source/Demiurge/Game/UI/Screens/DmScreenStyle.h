// DEMIURGE — Game/UI/Screens/DmScreenStyle.h : colours, fonts, brushes and button styles of the screens.
// Owner: C8 screens. The tokens are the old css/base.css ones (SPEC-UI 0): quiet, celestial,
// gold on ink, serif for names and headings, sans for numbers and controls. The HUD (C7) uses the
// same tokens; keep the two in step.
#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Fonts/SlateFontInfo.h"

namespace DmStyle
{
	/** Serif faces (Palatino Linotype) for names and headings; sans (Segoe UI) for controls and numbers.
	 *  The shipped OFL fonts (Game/UI/DmFonts.h): EB Garamond, Cormorant Garamond (display), Inter, Cinzel (logo). */
	enum class EFace : uint8 { Serif, SerifBold, SerifItalic, Sans, SansBold, SansLight };

	/** Button looks: Ghost (default), Primary (the one action), Chip (off / on: option pickers),
	 *  Danger (delete), Plain (a row you can press). */
	enum class EButton : uint8 { Ghost, Primary, Chip, ChipOn, Danger, Plain };

	// ---- colours (sRGB tokens from css/base.css, converted to linear for Slate)
	FLinearColor Ink(float A = 1.f);       // #0b0f17
	FLinearColor Ink2(float A = 1.f);      // #131a26
	FLinearColor Ink3(float A = 1.f);      // #1b2434
	FLinearColor Gold(float A = 1.f);      // #e8cd96
	FLinearColor GoldHi(float A = 1.f);    // #fff1c9
	FLinearColor Line(float A = 0.22f);    // gold hairline
	FLinearColor LineHi(float A = 0.55f);  // gold rule
	FLinearColor Text(float A = 1.f);      // #e9e4d6
	FLinearColor Dim(float A = 1.f);       // #b3b9c4
	FLinearColor Good(float A = 1.f);      // #8fd6a0
	FLinearColor Bad(float A = 1.f);       // #e2836f
	FLinearColor Awe(float A = 1.f);       // #ffd98a
	FLinearColor Water(float A = 1.f);     // #7fc7e8

	// ---- size: the interface scale (Settings) multiplies every size
	void SetScale(float Scale);
	float Scale();
	float Px(float Base);

	/** The game viewport's width in pixels (1920 when there is no viewport). */
	float ViewportWidthPx();
	/** A phone-narrow window (portrait): the pages stack their columns and shrink their widths. */
	bool IsCompact();
	/** A width in base units that fits the viewport with a gutter on each side (never wider than BaseWidth). */
	float FitWidth(float BaseWidth, float GutterBase = 16.f);
	/** Px() divides by the viewport's DPI scale, so text and panels keep their pixel size on any window (the engine
	 *  shrinks the whole UI on a phone-sized window otherwise). Off for the planet card's PNG, drawn at its own size. */
	void SetDpiCompensation(bool bOn);

	// ---- fonts
	FSlateFontInfo Font(EFace Face, float BasePx);

	// ---- brushes (rounded boxes take unscaled radii; the scale applies to radius and width)
	FSlateRoundedBoxBrush Box(FLinearColor Fill, float Radius, FLinearColor Outline = FLinearColor::Transparent, float Width = 0.f);
	const FSlateBrush* Panel();            // the dark panel with a gold hairline (SPEC-UI 0 .dm-panel)
	const FSlateBrush* Solid(FLinearColor C);   // flat colour (kept alive by the caller's style)
	const FSlateBrush* ScrimLeft();        // ink fading out to the right: title screen
	const FSlateBrush* ScrimBottom();      // ink fading up: timelapse dock
	const FSlateBrush* ScrimTop();         // ink fading down: timelapse title
	const FSlateBrush* RuleBrush();        // gold line fading at both ends

	const FButtonStyle& Button(EButton Kind);
	/** Slider look: a hairline bar and a gold thumb. */
	const FSliderStyle& Slider();
	/** Single-line text field look. */
	const FEditableTextBoxStyle& TextField();

	/** Builds a BGRA image brush from a per-pixel function (gradients, the planet card photo). */
	TSharedPtr<FSlateDynamicImageBrush> MakeImage(const TCHAR* Tag, int32 W, int32 H, const TFunctionRef<FColor(int32, int32)>& Pixel);
}
