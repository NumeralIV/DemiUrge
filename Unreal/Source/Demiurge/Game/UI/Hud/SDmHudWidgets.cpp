// DEMIURGE — Game/UI/Hud/SDmHudWidgets.cpp : painted widgets. Owner: C7 hud.

#include "Game/UI/Hud/SDmHudWidgets.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/UI/Hud/DmHudIcons.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Layout/SBorder.h"

namespace
{
	FPaintGeometry PaintRect(const FGeometry& G, const FVector2f& Pos, const FVector2f& Size)
	{
		return G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos));
	}

	void Arc(TArray<FVector2f>& Out, const FVector2f& C, float R, float A0, float A1)
	{
		Out.Reset();
		const int32 N = FMath::Clamp(int32(FMath::Abs(A1 - A0) / FMath::DegreesToRadians(4.f)) + 1, 2, 120);
		for (int32 I = 0; I <= N; ++I)
		{
			const float A = A0 + (A1 - A0) * float(I) / float(N);
			Out.Add(C + FVector2f(FMath::Cos(A), FMath::Sin(A)) * R);
		}
	}

	FVector2f Measure(const FString& S, const FSlateFontInfo& F)
	{
		if (!FSlateApplication::IsInitialized()) return FVector2f(S.Len() * F.Size * 0.6f, F.Size * 1.3f);
		const TSharedRef<FSlateFontMeasure> M = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		return FVector2f(M->Measure(S, F));
	}
}

// ------------------------------------------------------------------ SDmRing

void SDmRing::Construct(const FArguments& A)
{
	Progress = A._Progress; Size = A._Size; Thickness = A._Thickness; Color = A._Color; Back = A._Back;
	Icon = A._Icon; IconSize = A._IconSize; IconColor = A._IconColor;
}

int32 SDmRing::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const
{
	const float Op = Style.GetColorAndOpacityTint().A;
	const FVector2f C(Size * 0.5f, Size * 0.5f);
	const float R = Size * 0.5f - Thickness;
	TArray<FVector2f> Pts;
	FLinearColor B = Back.Get(); B.A *= Op;
	Arc(Pts, C, R, 0.f, 2.f * PI);
	FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Pts, ESlateDrawEffect::None, B, true, Thickness);
	const float P = FMath::Clamp(Progress.Get(), 0.f, 1.f);
	if (P > 0.002f)
	{
		FLinearColor F = Color.Get(); F.A *= Op;
		Arc(Pts, C, R, -0.5f * PI, -0.5f * PI + 2.f * PI * P);
		FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), Pts, ESlateDrawEffect::None, F, true, Thickness);
	}
	const FName I = Icon.Get();
	if (!I.IsNone())
	{
		FLinearColor IC = IconColor.Get(); IC.A *= Op;
		DmIcons::Paint(Out, Layer + 1, G, I, C - FVector2f(IconSize * 0.5f, IconSize * 0.5f), IconSize, IC);
	}
	return Layer + 1;
}

// ------------------------------------------------------------------ SDmAweOrb

void SDmAweOrb::Construct(const FArguments& A)
{
	Fill = A._Fill; Size = A._Size; Glow = A._Glow;
}

int32 SDmAweOrb::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	const float Op = Style.GetColorAndOpacityTint().A;
	const FVector2f C(Size * 0.5f, Size * 0.5f);
	const float R = Size * 0.5f;
	const float Gl = FMath::Clamp(Glow.Get(), 0.f, 1.f);
	// halo
	for (int32 K = 0; K < 4; ++K)
	{
		const float Rr = R * (1.42f - 0.1f * K);
		FLinearColor H = S.Awe; H.A = (0.035f + 0.02f * K) * (0.6f + 0.8f * Gl) * Op;
		FSlateDrawElement::MakeBox(Out, Layer, PaintRect(G, C - FVector2f(Rr, Rr), FVector2f(2 * Rr, 2 * Rr)), &S.Dot, ESlateDrawEffect::None, H);
	}
	// glass
	FLinearColor Glass = S.Ink2; Glass.A = 0.95f * Op;
	FSlateDrawElement::MakeBox(Out, Layer + 1, PaintRect(G, C - FVector2f(R - 1, R - 1), FVector2f(2 * R - 2, 2 * R - 2)), &S.Dot, ESlateDrawEffect::None, Glass);
	// liquid gold: horizontal chords from the bottom up to the fill level
	const float F = FMath::Clamp(Fill.Get(), 0.f, 1.f);
	const float Ri = R - 3.f;
	const float Top = C.Y + Ri - 2.f * Ri * F;
	TArray<FVector2f> Seg;
	Seg.SetNum(2);
	const FLinearColor Deep = FLinearColor::LerpUsingHSV(S.Awe, FDmHudStyle::Hex(TEXT("b8862e")), 0.6f);
	for (float Y = C.Y + Ri - 0.5f; Y >= Top; Y -= 1.f)
	{
		const float Dy = Y - C.Y;
		const float Hw = FMath::Sqrt(FMath::Max(0.f, Ri * Ri - Dy * Dy));
		if (Hw < 0.5f) continue;
		const float T = FMath::Clamp((C.Y + Ri - Y) / (2.f * Ri), 0.f, 1.f);
		FLinearColor L = FLinearColor::LerpUsingHSV(Deep, S.Awe, T * 0.8f + 0.2f * F);
		L.A = (0.82f + 0.18f * Gl) * Op;
		Seg[0] = FVector2f(C.X - Hw, Y);
		Seg[1] = FVector2f(C.X + Hw, Y);
		FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), Seg, ESlateDrawEffect::None, L, false, 1.05f);
	}
	if (F > 0.01f && F < 0.995f)
	{
		const float Dy = Top - C.Y;
		const float Hw = FMath::Sqrt(FMath::Max(0.f, Ri * Ri - Dy * Dy));
		Seg[0] = FVector2f(C.X - Hw, Top);
		Seg[1] = FVector2f(C.X + Hw, Top);
		FLinearColor Surf = S.GoldHi; Surf.A = 0.9f * Op;
		FSlateDrawElement::MakeLines(Out, Layer + 3, G.ToPaintGeometry(), Seg, ESlateDrawEffect::None, Surf, true, 1.2f);
	}
	// rim and highlight
	TArray<FVector2f> Pts;
	Arc(Pts, C, R - 1.f, 0.f, 2.f * PI);
	FLinearColor Rim = S.LineHi; Rim.A = 0.75f * Op;
	FSlateDrawElement::MakeLines(Out, Layer + 3, G.ToPaintGeometry(), Pts, ESlateDrawEffect::None, Rim, true, 1.2f);
	Arc(Pts, C, R - 5.f, FMath::DegreesToRadians(200.f), FMath::DegreesToRadians(250.f));
	FLinearColor Hi(1, 1, 1, 0.35f * Op);
	FSlateDrawElement::MakeLines(Out, Layer + 3, G.ToPaintGeometry(), Pts, ESlateDrawEffect::None, Hi, true, 1.6f);
	return Layer + 3;
}

// ------------------------------------------------------------------ SDmBar

void SDmBar::Construct(const FArguments& A)
{
	Value = A._Value; Width = A._Width; Height = A._Height; Color = A._Color;
}

int32 SDmBar::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	const float Op = Style.GetColorAndOpacityTint().A;
	const FVector2f Sz = FVector2f(G.GetLocalSize());
	FLinearColor B = S.BarBack.TintColor.GetSpecifiedColor(); B.A *= Op;
	FSlateDrawElement::MakeBox(Out, Layer, PaintRect(G, FVector2f(0, 0), Sz), &S.BarBack, ESlateDrawEffect::None, B);
	const float V = FMath::Clamp(Value.Get(), 0.f, 1.f);
	if (V > 0.f)
	{
		FLinearColor C = Color.Get(); C.A *= Op;
		FSlateDrawElement::MakeBox(Out, Layer + 1, PaintRect(G, FVector2f(0, 0), FVector2f(FMath::Max(Sz.Y, Sz.X * V), Sz.Y)), &S.Dot, ESlateDrawEffect::None, C);
	}
	return Layer + 1;
}

// ------------------------------------------------------------------ SDmGradient

int32 SDmGradient::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const
{
	if (Stops.Num() < 2) return Layer;
	const float Op = Style.GetColorAndOpacityTint().A;
	const FVector2f Sz = FVector2f(G.GetLocalSize());
	TArray<FSlateGradientStop> GS;
	for (int32 I = 0; I < Stops.Num(); ++I)
	{
		FLinearColor C = Stops[I]; C.A *= Op;
		GS.Add(FSlateGradientStop(FVector2f(Sz.X * float(I) / float(Stops.Num() - 1), 0.f), C));
	}
	FSlateDrawElement::MakeGradient(Out, Layer, G.ToPaintGeometry(), GS, Orient_Vertical, ESlateDrawEffect::None, FVector4f(Sz.Y * 0.5f));
	return Layer;
}

// ------------------------------------------------------------------ SDmFlourish

int32 SDmFlourish::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const
{
	const float Op = Style.GetColorAndOpacityTint().A;
	const FVector2f Sz = FVector2f(G.GetLocalSize());
	const float Mx = Sz.X * 0.5f, My = Sz.Y * 0.5f;
	FLinearColor C = Color.Get(); C.A *= Op;
	FLinearColor C0 = C; C0.A = 0.f;
	TArray<FVector2f> Pts = { FVector2f(0, My), FVector2f(Mx - 12.f, My) };
	TArray<FLinearColor> Cols = { C0, C };
	FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Pts, Cols, ESlateDrawEffect::None, FLinearColor::White, true, 1.f);
	Pts = { FVector2f(Mx + 12.f, My), FVector2f(Sz.X, My) };
	Cols = { C, C0 };
	FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Pts, Cols, ESlateDrawEffect::None, FLinearColor::White, true, 1.f);
	Pts = { FVector2f(Mx, My - 5.f), FVector2f(Mx + 5.f, My), FVector2f(Mx, My + 5.f), FVector2f(Mx - 5.f, My), FVector2f(Mx, My - 5.f), FVector2f(Mx + 2.f, My - 3.f) };
	FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Pts, ESlateDrawEffect::None, C, true, 1.2f);
	const FSlateBrush& Dot = FDmHudStyle::Get().Dot;
	FSlateDrawElement::MakeBox(Out, Layer, PaintRect(G, FVector2f(Mx - 1.5f, My - 1.5f), FVector2f(3.f, 3.f)), &Dot, ESlateDrawEffect::None, C);
	for (const float Side : { -1.f, 1.f })
		FSlateDrawElement::MakeBox(Out, Layer, PaintRect(G, FVector2f(Mx + Side * 18.f - 1.f, My - 1.f), FVector2f(2.f, 2.f)), &Dot, ESlateDrawEffect::None, C);
	return Layer;
}

// ------------------------------------------------------------------ SDmLoveScale

int32 SDmLoveScale::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	const float Op = Style.GetColorAndOpacityTint().A;
	const float W = float(G.GetLocalSize().X);
	const float Y = 8.f, X0 = 10.f, X1 = W - 10.f;
	// the track: fear (warm red) through indifference (dim) to love (green)
	TArray<FSlateGradientStop> GS;
	auto Col = [Op](FLinearColor C, float A) { C.A = A * Op; return C; };
	GS.Add(FSlateGradientStop(FVector2f(X0, 0), Col(S.Bad, 0.85f)));
	GS.Add(FSlateGradientStop(FVector2f((X0 + X1) * 0.5f, 0), Col(S.Dim, 0.45f)));
	GS.Add(FSlateGradientStop(FVector2f(X1, 0), Col(S.Good, 0.85f)));
	FSlateDrawElement::MakeGradient(Out, Layer, G.ToPaintGeometry(FVector2f(W, 3.f), FSlateLayoutTransform(FVector2f(0, Y - 1.5f))), GS, Orient_Vertical, ESlateDrawEffect::None, FVector4f(1.5f));
	static const TCHAR* Words[5] = { TEXT("terrified"), TEXT("wary"), TEXT("indifferent"), TEXT("fond"), TEXT("adoring") };
	const FSlateFontInfo Font = S.Font(EDmFont::Sans, 12.f);
	const float L = FMath::Clamp(Love.Get(), -1.f, 1.f);
	const int32 Near = FMath::Clamp(FMath::RoundToInt((L + 1.f) * 2.f), 0, 4);
	for (int32 I = 0; I < 5; ++I)
	{
		const float X = X0 + (X1 - X0) * float(I) / 4.f;
		TArray<FVector2f> Tick = { FVector2f(X, Y - 4.f), FVector2f(X, Y + 4.f) };
		FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), Tick, ESlateDrawEffect::None, Col(S.Text, 0.35f), true, 1.f);
		const FString Wd = Words[I];
		const FVector2f M = Measure(Wd, Font);
		const float Tx = FMath::Clamp(X - M.X * 0.5f, 0.f, W - M.X);
		FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(M, FSlateLayoutTransform(FVector2f(Tx, Y + 6.f))), Wd, Font, ESlateDrawEffect::None, Col(I == Near ? S.GoldHi : S.Dim, I == Near ? 1.f : 0.7f));
	}
	// marker: a small gold diamond on the track
	const float Mx = X0 + (X1 - X0) * (L + 1.f) * 0.5f;
	TArray<FVector2f> Dm = { FVector2f(Mx, Y - 6.f), FVector2f(Mx + 5.f, Y), FVector2f(Mx, Y + 6.f), FVector2f(Mx - 5.f, Y), FVector2f(Mx, Y - 6.f), FVector2f(Mx + 2.f, Y - 3.6f) };
	FSlateDrawElement::MakeBox(Out, Layer + 2, PaintRect(G, FVector2f(Mx - 4.f, Y - 4.f), FVector2f(8.f, 8.f)), &S.Dot, ESlateDrawEffect::None, Col(S.Ink, 1.f));
	FSlateDrawElement::MakeLines(Out, Layer + 3, G.ToPaintGeometry(), Dm, ESlateDrawEffect::None, Col(S.GoldHi, 1.f), true, 1.6f);
	return Layer + 3;
}

// ------------------------------------------------------------------ SDmSigil

void SDmSigil::PaintSigil(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Pos, float Size, const FLinearColor& Color, const FString& Letter, float Alpha)
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	FLinearColor C = Color; C.A = Alpha;
	FLinearColor Ring = S.Ink; Ring.A = 0.9f * Alpha;
	FSlateDrawElement::MakeBox(Out, Layer, PaintRect(G, Pos - FVector2f(1.f, 1.f), FVector2f(Size + 2.f, Size + 2.f)), &S.Dot, ESlateDrawEffect::None, Ring);
	FSlateDrawElement::MakeBox(Out, Layer + 1, PaintRect(G, Pos, FVector2f(Size, Size)), &S.Dot, ESlateDrawEffect::None, C);
	if (Letter.IsEmpty() || Size < 9.f) return;
	const FSlateFontInfo Font = S.Font(EDmFont::SansSemi, Size * 0.66f);
	const FVector2f M = Measure(Letter, Font);
	const float Lum = 0.2126f * Color.R + 0.7152f * Color.G + 0.0722f * Color.B;
	FLinearColor TC = Lum > 0.22f ? S.Ink : FLinearColor(1, 1, 1, 1);
	TC.A = Alpha;
	FSlateDrawElement::MakeText(Out, Layer + 2, G.ToPaintGeometry(M, FSlateLayoutTransform(Pos + FVector2f((Size - M.X) * 0.5f, (Size - M.Y) * 0.5f))), Letter, Font, ESlateDrawEffect::None, TC);
}

int32 SDmSigil::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const
{
	PaintSigil(Out, Layer, G, FVector2f(0, 0), Size, Color, Letter, Style.GetColorAndOpacityTint().A);
	return Layer + 2;
}

// ------------------------------------------------------------------ builders

TSharedRef<STextBlock> DmHudUi::Text(const TAttribute<FText>& InText, EDmFont Face, float Px, const TAttribute<FSlateColor>& Color, float WrapAt)
{
	TSharedRef<STextBlock> T = SNew(STextBlock)
		.Text(InText)
		.Font(FDmHudStyle::Get().Font(Face, Px))
		.ColorAndOpacity(Color)
		.WrapTextAt(WrapAt);
	return T;
}

TSharedRef<STextBlock> DmHudUi::Text(const FString& InText, EDmFont Face, float Px, const FLinearColor& Color, float WrapAt)
{
	return Text(FText::FromString(InText), Face, Px, FSlateColor(Color), WrapAt);
}

TSharedRef<SWidget> DmHudUi::Caption(const FString& InText, const FLinearColor& Color)
{
	return SNew(STextBlock)
		.Text(FText::FromString(InText.ToUpper()))
		.Font(FDmHudStyle::Get().Font(EDmFont::SansSemi, 11.5f))
		.ColorAndOpacity(FSlateColor(Color));
}

TSharedRef<SWidget> DmHudUi::Panel(const TSharedRef<SWidget>& Content, const FMargin& Padding, const FSlateBrush* Brush, bool bBlockWheel)
{
	return SNew(SDmBlocker).BlockWheel(bBlockWheel)
	[
		SNew(SBorder)
		.BorderImage(Brush ? Brush : &FDmHudStyle::Get().Panel)
		.Padding(Padding)
		[
			Content
		]
	];
}

FString DmHudUi::Initial(const FString& Name)
{
	for (int32 I = 0; I < Name.Len(); ++I)
		if (FChar::IsAlpha(Name[I])) return FString::Chr(FChar::ToUpper(Name[I]));
	return FString();
}

TArray<FSlateRect>& DmHudUi::PanelRects()
{
	static TArray<FSlateRect> Rects;
	return Rects;
}

bool& DmHudUi::PointerOverPanelNow()
{
	static bool bOver = false;
	return bOver;
}

bool& DmHudUi::PointerOverPanelLast()
{
	static bool bOver = false;
	return bOver;
}

void SDmBlocker::Tick(const FGeometry& G, const double Now, const float Dt)
{
	SCompoundWidget::Tick(G, Now, Dt);
	const FVector2D P = G.GetAbsolutePosition();
	DmHudUi::PanelRects().Add(FSlateRect(P, P + G.GetAbsoluteSize()));
	if (IsHovered()) DmHudUi::PointerOverPanelNow() = true;
}
