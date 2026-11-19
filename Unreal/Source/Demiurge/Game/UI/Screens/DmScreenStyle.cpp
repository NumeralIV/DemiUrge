// DEMIURGE — Game/UI/Screens/DmScreenStyle.cpp : see DmScreenStyle.h. Owner: C8 screens.

#include "Game/UI/Screens/DmScreenStyle.h"

#include "Brushes/SlateColorBrush.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SWindow.h"
#include "Engine/UserInterfaceSettings.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "UnrealClient.h"

namespace DmStyle
{
namespace
{
	float GScale = 1.f;

	FLinearColor Srgb(uint8 R, uint8 G, uint8 B, float A)
	{
		FLinearColor C = FLinearColor::FromSRGBColor(FColor(R, G, B, 255));
		C.A = A;
		return C;
	}

	// ---- font families: one composite per family, the faces are its typefaces
	TSharedPtr<const FCompositeFont> GFamilies[2];
	bool GFamilyTried[2] = { false, false };

	bool IsSerif(EFace Face)
	{
		return Face == EFace::Serif || Face == EFace::SerifBold || Face == EFace::SerifItalic;
	}

	TSharedPtr<const FCompositeFont> Family(bool bSerif)
	{
		const int32 I = bSerif ? 0 : 1;
		if (GFamilyTried[I]) return GFamilies[I];
		GFamilyTried[I] = true;
		const FString Dir = TEXT("C:/Windows/Fonts/");
		TSharedPtr<FCompositeFont> F;
		auto Add = [&F, &Dir](const TCHAR* TypefaceName, const TCHAR* File)
		{
			const FString Path = Dir + File;
			if (!FPaths::FileExists(Path)) return;
			if (!F.IsValid()) F = MakeShared<FCompositeFont>();
			F->DefaultTypeface.AppendFont(FName(TypefaceName), Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		};
		if (bSerif)
		{
			Add(TEXT("Regular"), TEXT("pala.ttf"));
			Add(TEXT("Bold"), TEXT("palab.ttf"));
			Add(TEXT("Italic"), TEXT("palai.ttf"));
		}
		else
		{
			Add(TEXT("Regular"), TEXT("segoeui.ttf"));
			Add(TEXT("Bold"), TEXT("segoeuib.ttf"));
			Add(TEXT("Light"), TEXT("segoeuil.ttf"));
		}
		GFamilies[I] = F;
		return F;
	}

	FName TypefaceOf(EFace Face)
	{
		switch (Face)
		{
		case EFace::SerifBold:
		case EFace::SansBold: return FName(TEXT("Bold"));
		case EFace::SerifItalic: return FName(TEXT("Italic"));
		case EFace::SansLight: return FName(TEXT("Light"));
		default: return FName(TEXT("Regular"));
		}
	}

	// A rounded box whose image size is given (sliders and other sized brushes need one).
	FSlateRoundedBoxBrush BoxSized(FLinearColor Fill, float Radius, FVector2f Size)
	{
		FSlateRoundedBoxBrush B(Fill, Radius, Size);
		return B;
	}

	float Smooth(float E0, float E1, float X)
	{
		const float T = FMath::Clamp((X - E0) / (E1 - E0), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	uint8 ToByte(float V)
	{
		return uint8(FMath::Clamp(FMath::RoundToInt(V * 255.f), 0, 255));
	}
}

// ---------------------------------------------------------------- colours

FLinearColor Ink(float A)     { return Srgb(11, 15, 23, A); }
FLinearColor Ink2(float A)    { return Srgb(19, 26, 38, A); }
FLinearColor Ink3(float A)    { return Srgb(27, 36, 52, A); }
FLinearColor Gold(float A)    { return Srgb(232, 205, 150, A); }
FLinearColor GoldHi(float A)  { return Srgb(255, 241, 201, A); }
FLinearColor Line(float A)    { return Srgb(232, 205, 150, A); }
FLinearColor LineHi(float A)  { return Srgb(232, 205, 150, A); }
FLinearColor Text(float A)    { return Srgb(233, 228, 214, A); }
FLinearColor Dim(float A)     { return Srgb(179, 185, 196, A); }
FLinearColor Good(float A)    { return Srgb(143, 214, 160, A); }
FLinearColor Bad(float A)     { return Srgb(226, 131, 111, A); }
FLinearColor Awe(float A)     { return Srgb(255, 217, 138, A); }
FLinearColor Water(float A)   { return Srgb(127, 199, 232, A); }

// ---------------------------------------------------------------- size

void SetScale(float Scale) { GScale = FMath::Clamp(Scale, 0.6f, 2.0f); }
float Scale() { return GScale; }

namespace
{
	bool GDpiOn = true;

	/** The game viewport's DPI scale (1 when there is none): Slate units times this are pixels. The engine's
	 *  size-based UI curve (User Interface settings) sets it on a small window, e.g. a phone's 390 x 844. */
	float ViewportDpi()
	{
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
		{
			const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
			if (Size.X > 0 && Size.Y > 0)
			{
				const float D = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Size);
				if (D > 0.05f && D < 8.f) return D;
			}
		}
		return 1.f;
	}
}

void SetDpiCompensation(bool bOn) { GDpiOn = bOn; }

float Px(float Base)
{
	return Base * GScale / (GDpiOn ? ViewportDpi() : 1.f);
}

float ViewportWidthPx()
{
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
		if (Size.X > 0) return float(Size.X);
	}
	return 1920.f;
}

bool IsCompact()
{
	return ViewportWidthPx() < 720.f;
}

float FitWidth(float BaseWidth, float GutterBase)
{
	// the viewport in base units (pixels over the interface scale; Px compensates the DPI, so pixels are the
	// unit here), less a gutter on each side
	const float Avail = ViewportWidthPx() / GScale - 2.f * GutterBase;
	return FMath::Clamp(Avail, 120.f, BaseWidth);
}

// ---------------------------------------------------------------- fonts

FSlateFontInfo Font(EFace Face, float BasePx)
{
	const float Size = Px(BasePx);
	TSharedPtr<const FCompositeFont> Fam = Family(IsSerif(Face));
	if (!Fam.IsValid())
	{
		// no Windows fonts on this machine: the engine's own face
		return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size);
	}
	return FSlateFontInfo(Fam, Size, TypefaceOf(Face));
}

// ---------------------------------------------------------------- brushes

FSlateRoundedBoxBrush Box(FLinearColor Fill, float Radius, FLinearColor Outline, float Width)
{
	FSlateRoundedBoxBrush B(Fill, Px(Radius));
	if (Width > 0.f)
	{
		B.OutlineSettings = FSlateBrushOutlineSettings(Px(Radius), Outline, Px(Width));
	}
	return B;
}

const FSlateBrush* Panel()
{
	// rebuilt when the interface size changes: the radius and the hairline are scaled
	static FSlateRoundedBoxBrush B = Box(Ink(0.86f), 10.f, Line(0.24f), 1.f);
	static float BuiltScale = Scale();
	if (BuiltScale != Scale())
	{
		BuiltScale = Scale();
		B = Box(Ink(0.86f), 10.f, Line(0.24f), 1.f);
	}
	return &B;
}

const FSlateBrush* Solid(FLinearColor C)
{
	// one brush per colour, kept for the life of the module (the colours used are a small fixed set)
	static TMap<uint32, TSharedPtr<FSlateColorBrush>> Cache;
	const FColor Key = C.ToFColor(false);
	const uint32 K = Key.DWColor();
	TSharedPtr<FSlateColorBrush>& Slot = Cache.FindOrAdd(K);
	if (!Slot.IsValid()) Slot = MakeShared<FSlateColorBrush>(C);
	return Slot.Get();
}

TSharedPtr<FSlateDynamicImageBrush> MakeImage(const TCHAR* Tag, int32 W, int32 H, const TFunctionRef<FColor(int32, int32)>& Pixel)
{
	TArray<uint8> Data;
	Data.SetNumUninitialized(W * H * 4);
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const FColor C = Pixel(X, Y);
			const int32 I = (Y * W + X) * 4;
			Data[I + 0] = C.B;
			Data[I + 1] = C.G;
			Data[I + 2] = C.R;
			Data[I + 3] = C.A;
		}
	}
	static int32 Counter = 0;
	const FName Name(*FString::Printf(TEXT("DmScreens.%s.%d"), Tag, ++Counter));
	return FSlateDynamicImageBrush::CreateWithImageData(Name, FVector2f((float)W, (float)H), Data);
}

const FSlateBrush* ScrimLeft()
{
	static TSharedPtr<FSlateDynamicImageBrush> B = MakeImage(TEXT("scrimL"), 512, 2, [](int32 X, int32)
	{
		const float U = (X + 0.5f) / 512.f;
		const float A = 0.92f * (1.f - Smooth(0.22f, 0.86f, U));
		return FColor(11, 15, 23, ToByte(A));
	});
	return B.Get();
}

const FSlateBrush* ScrimBottom()
{
	static TSharedPtr<FSlateDynamicImageBrush> B = MakeImage(TEXT("scrimB"), 2, 256, [](int32, int32 Y)
	{
		const float V = (Y + 0.5f) / 256.f;
		const float A = 0.88f * Smooth(0.55f, 1.f, V);
		return FColor(11, 15, 23, ToByte(A));
	});
	return B.Get();
}

const FSlateBrush* ScrimTop()
{
	static TSharedPtr<FSlateDynamicImageBrush> B = MakeImage(TEXT("scrimT"), 2, 256, [](int32, int32 Y)
	{
		const float V = (Y + 0.5f) / 256.f;
		const float A = 0.78f * (1.f - Smooth(0.0f, 0.42f, V));
		return FColor(11, 15, 23, ToByte(A));
	});
	return B.Get();
}

const FSlateBrush* RuleBrush()
{
	static TSharedPtr<FSlateDynamicImageBrush> B = MakeImage(TEXT("rule"), 256, 1, [](int32 X, int32)
	{
		const float U = (X + 0.5f) / 256.f;
		const float A = 0.85f * FMath::Pow(FMath::Sin(PI * U), 1.4f);
		return FColor(232, 205, 150, ToByte(A));
	});
	return B.Get();
}

// ---------------------------------------------------------------- button, slider, field looks

const FButtonStyle& Button(EButton Kind)
{
	// the looks are rebuilt in place when the interface size changes (radii and hairlines are scaled; the
	// widgets keep a pointer to these styles, so they see the new looks on their next paint)
	static FButtonStyle Styles[6];
	static float BuiltScale = -1.f;
	if (BuiltScale != GScale)
	{
		BuiltScale = GScale;
		const FMargin NoPad(0.f);

		// Ghost: the default; a gold hairline, the fill lifts on hover
		Styles[uint8(EButton::Ghost)]
			.SetNormal(Box(Ink3(0.55f), 8.f, Line(0.55f), 1.f))
			.SetHovered(Box(Gold(0.10f), 8.f, LineHi(0.90f), 1.f))
			.SetPressed(Box(Gold(0.20f), 8.f, Gold(1.f), 1.f))
			.SetDisabled(Box(Ink3(0.30f), 8.f, Line(0.15f), 1.f))
			.SetNormalForeground(Text())
			.SetHoveredForeground(GoldHi())
			.SetPressedForeground(GoldHi())
			.SetDisabledForeground(Dim(0.40f))
			.SetNormalPadding(NoPad)
			.SetPressedPadding(NoPad);

		// Primary: the one action of a page
		Styles[uint8(EButton::Primary)]
			.SetNormal(Box(Gold(0.16f), 8.f, Gold(0.85f), 1.f))
			.SetHovered(Box(Gold(0.27f), 8.f, Gold(1.f), 1.f))
			.SetPressed(Box(Gold(0.36f), 8.f, GoldHi(1.f), 1.f))
			.SetDisabled(Box(Gold(0.05f), 8.f, Line(0.2f), 1.f))
			.SetNormalForeground(GoldHi())
			.SetHoveredForeground(GoldHi())
			.SetPressedForeground(GoldHi())
			.SetDisabledForeground(Dim(0.40f))
			.SetNormalPadding(NoPad)
			.SetPressedPadding(NoPad);

		// Chip off / on: option pickers (quality, calamities, toggles)
		Styles[uint8(EButton::Chip)]
			.SetNormal(Box(Ink3(0.40f), 8.f, Line(0.30f), 1.f))
			.SetHovered(Box(Gold(0.08f), 8.f, LineHi(0.80f), 1.f))
			.SetPressed(Box(Gold(0.16f), 8.f, Gold(0.9f), 1.f))
			.SetDisabled(Box(Ink3(0.25f), 8.f, Line(0.12f), 1.f))
			.SetNormalForeground(Dim())
			.SetHoveredForeground(Text())
			.SetPressedForeground(GoldHi())
			.SetDisabledForeground(Dim(0.40f))
			.SetNormalPadding(NoPad)
			.SetPressedPadding(NoPad);

		Styles[uint8(EButton::ChipOn)]
			.SetNormal(Box(Gold(0.20f), 8.f, Gold(0.90f), 1.f))
			.SetHovered(Box(Gold(0.29f), 8.f, Gold(1.f), 1.f))
			.SetPressed(Box(Gold(0.36f), 8.f, GoldHi(1.f), 1.f))
			.SetDisabled(Box(Gold(0.10f), 8.f, Line(0.3f), 1.f))
			.SetNormalForeground(GoldHi())
			.SetHoveredForeground(GoldHi())
			.SetPressedForeground(GoldHi())
			.SetDisabledForeground(Dim(0.40f))
			.SetNormalPadding(NoPad)
			.SetPressedPadding(NoPad);

		// Danger: deletes (asked twice, see the save screen)
		Styles[uint8(EButton::Danger)]
			.SetNormal(Box(Bad(0.10f), 8.f, Bad(0.60f), 1.f))
			.SetHovered(Box(Bad(0.20f), 8.f, Bad(0.90f), 1.f))
			.SetPressed(Box(Bad(0.28f), 8.f, Bad(1.f), 1.f))
			.SetDisabled(Box(Ink3(0.25f), 8.f, Line(0.12f), 1.f))
			.SetNormalForeground(Bad())
			.SetHoveredForeground(GoldHi())
			.SetPressedForeground(GoldHi())
			.SetDisabledForeground(Dim(0.40f))
			.SetNormalPadding(NoPad)
			.SetPressedPadding(NoPad);

		// Plain: a row you can press (slot cards, list items)
		Styles[uint8(EButton::Plain)]
			.SetNormal(Box(Ink2(0.45f), 8.f, Line(0.14f), 1.f))
			.SetHovered(Box(Gold(0.08f), 8.f, Line(0.45f), 1.f))
			.SetPressed(Box(Gold(0.14f), 8.f, Gold(0.6f), 1.f))
			.SetDisabled(Box(Ink2(0.25f), 8.f, Line(0.08f), 1.f))
			.SetNormalForeground(Text())
			.SetHoveredForeground(GoldHi())
			.SetPressedForeground(GoldHi())
			.SetDisabledForeground(Dim(0.40f))
			.SetNormalPadding(NoPad)
			.SetPressedPadding(NoPad);
	}
	return Styles[uint8(Kind)];
}

const FSliderStyle& Slider()
{
	static FSliderStyle Style = FCoreStyle::Get().GetWidgetStyle<FSliderStyle>(TEXT("Slider"));
	static float BuiltScale = -1.f;
	if (BuiltScale != GScale)
	{
		BuiltScale = GScale;
		Style.SetBarThickness(Px(4.f));
		Style.SetNormalBarImage(Box(Line(0.55f), 2.f));
		Style.SetHoveredBarImage(Box(Line(0.75f), 2.f));
		Style.SetDisabledBarImage(Box(Line(0.20f), 2.f));
		const FVector2f Thumb(Px(16.f), Px(16.f));
		Style.SetNormalThumbImage(BoxSized(Gold(1.f), 8.f, Thumb));
		Style.SetHoveredThumbImage(BoxSized(GoldHi(1.f), 8.f, Thumb));
		Style.SetDisabledThumbImage(BoxSized(Dim(0.4f), 8.f, Thumb));
	}
	return Style;
}

const FEditableTextBoxStyle& TextField()
{
	static FEditableTextBoxStyle Style = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("NormalEditableTextBox"));
	static float BuiltScale = -1.f;
	if (BuiltScale != GScale)
	{
		BuiltScale = GScale;
		Style.BackgroundImageNormal = Box(Ink3(0.85f), 8.f, Line(0.45f), 1.f);
		Style.BackgroundImageHovered = Box(Ink3(0.95f), 8.f, Line(0.70f), 1.f);
		Style.BackgroundImageFocused = Box(Ink2(0.95f), 8.f, Gold(0.90f), 1.f);
		Style.BackgroundImageReadOnly = Box(Ink3(0.50f), 8.f, Line(0.20f), 1.f);
	}
	return Style;
}

} // namespace DmStyle
