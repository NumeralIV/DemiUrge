// DEMIURGE — Game/UI/Hud/DmHudStyle.h : the HUD's design tokens (old SPEC-UI 0 "css/base.css"), fonts and
// brushes. Owner: C7 hud.
//
// Quiet, celestial, gold on ink: serif (Palatino Linotype) for names and headings, sans (Segoe UI) for
// numbers and controls. Colours are the old CSS tokens, given in sRGB and converted to linear here.
// Sizes are CSS pixels at UI scale 1 (FDmHudStyle::Font takes pixels; Slate's point size is px x 0.75).
//
// Fonts are loaded at runtime from C:/Windows/Fonts (pala*.ttf, segoeui*.ttf / seguisb.ttf) through
// FCompositeFont, no font assets. SHIPPING NEEDS AN OPEN FONT: Palatino and Segoe UI are Windows system
// fonts and may not be redistributed; swap the file names in DmHudStyle.cpp for an open serif (EB
// Garamond, Cormorant) and sans (Inter, Source Sans) placed under the project when assets arrive.
// When a file is missing the engine's default font (Roboto) is used, so nothing breaks.
#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include <array>

enum class EDmFont : uint8
{
	Serif,          // names, headings, chronicle prose
	SerifBold,
	SerifItalic,
	Sans,           // numbers, controls
	SansSemi,
};

class DEMIURGE_API FDmHudStyle
{
public:
	static const FDmHudStyle& Get();

	// ------------------------------------------------------------ colours (linear)
	FLinearColor Ink, Ink2, Ink3, Line, LineHi, Gold, GoldHi, Text, Dim, Good, Bad, Awe, Water, PanelBg, Shadow;
	/** sRGB hex ("e8cd96") with alpha. */
	static FLinearColor Hex(const TCHAR* Rgb, float Alpha = 1.f);
	static FLinearColor WithAlpha(const FLinearColor& C, float A) { FLinearColor O = C; O.A = A; return O; }

	// ------------------------------------------------------------ type (CSS px; x the UI scale by the root's DPI scaler)
	static constexpr float FsSmall = 13.f;
	static constexpr float FsBody = 15.f;
	static constexpr float FsTitle = 22.f;
	FSlateFontInfo Font(EDmFont Face, float Px, int32 OutlinePx = 0, const FLinearColor& OutlineColor = FLinearColor::Black) const;
	/** True when the face came from the system font file (false = engine fallback). */
	bool HasFace(EDmFont Face) const;

	// ------------------------------------------------------------ brushes (owned here; widgets keep pointers)
	FSlateBrush NoBrush;
	FSlateBrush Panel;           // --panel-bg, --radius 10, hairline --line
	FSlateBrush PanelHi;         // the same with --line-hi (armed chip, banners)
	FSlateBrush PanelSolid;      // opaque ink-2 (chronicle book, sheets)
	FSlateBrush Card;            // ink-3 inset (inspector sections)
	FSlateBrush Pill;            // toasts
	FSlateBrush Tip;             // tooltips (ink, line-hi)
	FSlateBrush BtnNormal, BtnHover, BtnPressed, BtnOn, BtnOnHover;   // round-cornered buttons
	FSlateBrush RoundNormal, RoundHover, RoundPressed, RoundOn;       // 44 px round icon buttons
	FSlateBrush BarBack, BarFill, BarFillGood, BarFillBad, BarFillAwe;
	FSlateBrush Dot;             // a white circle to tint (culture dots)
	FSlateBrush KeyCap;          // hotkey caps
	FSlateBrush Hairline;        // 1 px gold line (rules)
	FButtonStyle Button, ButtonOn, RoundButton, RoundButtonOn, FlatButton;
	FScrollBarStyle ScrollBar;
	FScrollBoxStyle ScrollBox;

	/** Dot brush in a culture's colour (cached per colour). */
	const FSlateBrush* DotBrush(const FLinearColor& C) const;

	FDmHudStyle();

private:
	struct FFace { TSharedPtr<const FCompositeFont> Font; bool bSystem = false; };
	FFace Faces[5];
	mutable TMap<uint32, TSharedPtr<FSlateBrush>> DotCache;
};

/** Culture colour as a linear Slate colour (the sim stores 0..1 linear-ish RGB). */
DEMIURGE_API FLinearColor DmCultureColor(const std::array<double, 3>& Rgb);
