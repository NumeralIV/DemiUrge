// DEMIURGE — Game/UI/Hud/DmHudStyle.cpp : tokens, runtime fonts, brushes. Owner: C7 hud.

#include "Game/UI/Hud/DmHudStyle.h"

#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "Fonts/CompositeFont.h"
#include "Game/UI/DmFonts.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"

namespace
{
	FSlateBrush Rounded(const FLinearColor& Fill, float Radius, const FLinearColor& Outline, float OutlineWidth)
	{
		return FSlateRoundedBoxBrush(Fill, Radius, Outline, OutlineWidth);
	}

	FSlateBrush Round(const FLinearColor& Fill, float Radius)
	{
		return FSlateRoundedBoxBrush(Fill, Radius);
	}
}

FLinearColor FDmHudStyle::Hex(const TCHAR* Rgb, float Alpha)
{
	FLinearColor C(FColor::FromHex(Rgb));
	C.A = Alpha;
	return C;
}

const FDmHudStyle& FDmHudStyle::Get()
{
	static TUniquePtr<FDmHudStyle> Style;
	if (!Style) Style = MakeUnique<FDmHudStyle>();
	return *Style;
}

FDmHudStyle::FDmHudStyle()
{
	Ink = Hex(TEXT("0b0f17"));
	Ink2 = Hex(TEXT("131a26"));
	Ink3 = Hex(TEXT("1b2434"));
	Line = Hex(TEXT("e8cd96"), 0.22f);
	LineHi = Hex(TEXT("e8cd96"), 0.55f);
	Gold = Hex(TEXT("e8cd96"));
	GoldHi = Hex(TEXT("fff1c9"));
	Text = Hex(TEXT("e9e4d6"));
	Dim = Hex(TEXT("b3b9c4"));
	Good = Hex(TEXT("8fd6a0"));
	Bad = Hex(TEXT("e2836f"));
	Awe = Hex(TEXT("ffd98a"));
	Water = Hex(TEXT("7fc7e8"));
	PanelBg = Hex(TEXT("0b0f17"), 0.84f);
	Shadow = Hex(TEXT("000000"), 0.45f);

	// ---- fonts: the shipped OFL families (Game/UI/DmFonts.h; integration replaced the Windows stand-ins)
	Faces[int32(EDmFont::Serif)].Font = DmFonts::Face(EDmFontFile::Garamond);
	Faces[int32(EDmFont::SerifBold)].Font = DmFonts::Face(EDmFontFile::GaramondMedium);
	Faces[int32(EDmFont::SerifItalic)].Font = DmFonts::Face(EDmFontFile::GaramondItalic);
	Faces[int32(EDmFont::Sans)].Font = DmFonts::Face(EDmFontFile::Inter);
	Faces[int32(EDmFont::SansSemi)].Font = DmFonts::Face(EDmFontFile::InterSemi);
	for (FFace& F : Faces) F.bSystem = F.Font.IsValid();
	DisplaySerif = DmFonts::Face(EDmFontFile::CormorantSemi);
	DisplayItalic = DmFonts::Face(EDmFontFile::CormorantItalic);

	// ---- brushes
	NoBrush = FSlateNoResource();
	Panel = Rounded(PanelBg, 10.f, Line, 1.f);
	PanelHi = Rounded(Hex(TEXT("0b0f17"), 0.9f), 10.f, LineHi, 1.f);
	PanelSolid = Rounded(Hex(TEXT("0d121b"), 0.96f), 12.f, LineHi, 1.f);
	Card = Rounded(Hex(TEXT("1b2434"), 0.55f), 8.f, Hex(TEXT("e8cd96"), 0.12f), 1.f);
	Pill = Rounded(Hex(TEXT("0b0f17"), 0.88f), 18.f, Line, 1.f);
	Tip = Rounded(Hex(TEXT("0b0f17"), 0.94f), 8.f, LineHi, 1.f);
	BtnNormal = Rounded(Hex(TEXT("131a26"), 0.0f), 8.f, Hex(TEXT("e8cd96"), 0.0f), 1.f);
	BtnHover = Rounded(Hex(TEXT("1b2434"), 0.85f), 8.f, Line, 1.f);
	BtnPressed = Rounded(Hex(TEXT("e8cd96"), 0.16f), 8.f, LineHi, 1.f);
	BtnOn = Rounded(Hex(TEXT("e8cd96"), 0.14f), 8.f, Hex(TEXT("e8cd96"), 0.75f), 1.f);
	BtnOnHover = Rounded(Hex(TEXT("e8cd96"), 0.22f), 8.f, Hex(TEXT("fff1c9"), 0.9f), 1.f);
	RoundNormal = Rounded(Hex(TEXT("0b0f17"), 0.72f), 22.f, Line, 1.f);
	RoundHover = Rounded(Hex(TEXT("1b2434"), 0.9f), 22.f, LineHi, 1.f);
	RoundPressed = Rounded(Hex(TEXT("e8cd96"), 0.2f), 22.f, LineHi, 1.f);
	RoundOn = Rounded(Hex(TEXT("e8cd96"), 0.18f), 22.f, Hex(TEXT("e8cd96"), 0.8f), 1.f);
	BarBack = Round(Hex(TEXT("e9e4d6"), 0.10f), 3.f);
	BarFill = Round(Gold, 3.f);
	BarFillGood = Round(Good, 3.f);
	BarFillBad = Round(Bad, 3.f);
	BarFillAwe = Round(Awe, 3.f);
	Dot = Round(FLinearColor::White, 64.f);
	Dot.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
	for (FSlateBrush* B : { &RoundNormal, &RoundHover, &RoundPressed, &RoundOn }) B->OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
	KeyCap = Rounded(Hex(TEXT("0b0f17"), 0.6f), 3.f, Hex(TEXT("e8cd96"), 0.28f), 1.f);
	Hairline = FSlateColorBrush(Line);

	auto MakeButton = [](const FSlateBrush& N, const FSlateBrush& H, const FSlateBrush& P)
	{
		FButtonStyle B;
		B.SetNormal(N);
		B.SetHovered(H);
		B.SetPressed(P);
		B.SetDisabled(N);
		B.SetNormalPadding(FMargin(0));
		B.SetPressedPadding(FMargin(0));
		return B;
	};
	Button = MakeButton(BtnNormal, BtnHover, BtnPressed);
	ButtonOn = MakeButton(BtnOn, BtnOnHover, BtnPressed);
	RoundButton = MakeButton(RoundNormal, RoundHover, RoundPressed);
	RoundButtonOn = MakeButton(RoundOn, RoundOn, RoundPressed);
	FlatButton = MakeButton(NoBrush, BtnHover, BtnPressed);

	ScrollBar = FCoreStyle::Get().GetWidgetStyle<FScrollBarStyle>("ScrollBar");
	const FSlateBrush Thumb = Round(Hex(TEXT("e8cd96"), 0.28f), 3.f);
	const FSlateBrush ThumbHi = Round(Hex(TEXT("e8cd96"), 0.5f), 3.f);
	ScrollBar.SetVerticalBackgroundImage(NoBrush).SetHorizontalBackgroundImage(NoBrush)
		.SetNormalThumbImage(Thumb).SetHoveredThumbImage(ThumbHi).SetDraggedThumbImage(ThumbHi)
		.SetVerticalTopSlotImage(NoBrush).SetVerticalBottomSlotImage(NoBrush)
		.SetHorizontalTopSlotImage(NoBrush).SetHorizontalBottomSlotImage(NoBrush);
	ScrollBox = FCoreStyle::Get().GetWidgetStyle<FScrollBoxStyle>("ScrollBox");
	ScrollBox.SetTopShadowBrush(NoBrush).SetBottomShadowBrush(NoBrush).SetLeftShadowBrush(NoBrush).SetRightShadowBrush(NoBrush);
}

FSlateFontInfo FDmHudStyle::Font(EDmFont Face, float Px, int32 OutlinePx, const FLinearColor& OutlineColor) const
{
	const float Pt = FMath::Max(1.f, Px * 0.75f);
	FFontOutlineSettings Outline;
	if (OutlinePx > 0)
	{
		Outline.OutlineSize = OutlinePx;
		Outline.OutlineColor = OutlineColor;
		Outline.bSeparateFillAlpha = true;
	}
	// display text of 20 px and up is Cormorant Garamond (T-007 roles): SemiBold for upright serif, Italic for italic
	if (Px >= 20.f && (Face == EDmFont::Serif || Face == EDmFont::SerifBold) && DisplaySerif.IsValid()) return FSlateFontInfo(DisplaySerif, Pt * 1.08f, NAME_None, Outline);
	if (Px >= 18.f && Face == EDmFont::SerifItalic && DisplayItalic.IsValid()) return FSlateFontInfo(DisplayItalic, Pt * 1.1f, NAME_None, Outline);
	const FFace& F = Faces[int32(Face)];
	if (F.Font.IsValid()) return FSlateFontInfo(F.Font, Pt, NAME_None, Outline);
	const bool bBold = Face == EDmFont::SerifBold || Face == EDmFont::SansSemi;
	FSlateFontInfo Fallback = FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : (Face == EDmFont::SerifItalic ? "Italic" : "Regular"), int32(FMath::RoundToInt(Pt)));
	Fallback.OutlineSettings = Outline;
	return Fallback;
}

bool FDmHudStyle::HasFace(EDmFont Face) const
{
	return Faces[int32(Face)].bSystem;
}

const FSlateBrush* FDmHudStyle::DotBrush(const FLinearColor& C) const
{
	const FColor Key = C.ToFColor(false);
	const uint32 K = Key.DWColor();
	if (const TSharedPtr<FSlateBrush>* B = DotCache.Find(K)) return B->Get();
	TSharedPtr<FSlateBrush> B = MakeShared<FSlateRoundedBoxBrush>(C, 64.f, Hex(TEXT("0b0f17"), 0.9f), 1.f);
	B->OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
	DotCache.Add(K, B);
	return B.Get();
}

FLinearColor DmCultureColor(const std::array<double, 3>& Rgb)
{
	return FLinearColor(float(Rgb[0]), float(Rgb[1]), float(Rgb[2]), 1.f);
}
