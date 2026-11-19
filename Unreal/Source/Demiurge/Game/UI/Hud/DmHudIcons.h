// DEMIURGE — Game/UI/Hud/DmHudIcons.h : the icon set (old SPEC-UI 3, DM.Icons) as vector paths. Owner: C7 hud.
//
// Every icon is an SVG-style path string in a 24 x 24 box (M L H V C S Q T A Z, absolute and relative,
// plus "F x y r" for a filled dot), flattened once into polylines and stroked in Slate with antialiased
// lines (1.6 px at 24 px, scaled with the size; round-ish joins). No textures, no assets.
//
//   DmIcons::Paint(OutDrawElements, LayerId, AllottedGeometry, TEXT("rain"), FVector2f(0, 0), 20.f, Gold);
//   SNew(SDmIcon).Icon(TEXT("meteor")).Size(22.f).Color(FDmHudStyle::Get().Gold)
//
// Names (binding, old SPEC-UI 3): one per power id (comet, seed, awaken, raise, lower, rain, bloom,
// lightning, quake, plague, meteor, volcano, heal, inspire, prophet, peace), ages age0 ... age6,
// buildings (shrine, temple, granary, walls, port, market, mine, factory, academy, plant, launch,
// wonder), statuses (plague, famine, war, blessed, inspired, praying, rising-sea), and awe, people,
// culture, trade, ship, thermometer, sea, pause, play, linger, fast, faster, fastest, eye, layers,
// scroll, gear, close, back, save, home, camera, sun, moon, lock, check, arrow, plus, minus, heart,
// skull; additions: menu, eye-off, rainfall, devotion, habitability, flourish, info, star, fire, storm,
// drought, crown. An unknown name draws a small neutral dot and never fails.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class FSlateWindowElementList;
struct FGeometry;

namespace DmIcons
{
	struct FIcon
	{
		TArray<TArray<FVector2f>> Lines;    // polylines in the 24 x 24 box
		TArray<FVector3f> Dots;             // filled dots: x, y, radius
	};

	/** The flattened icon (the neutral dot for unknown names). */
	DEMIURGE_API const FIcon& Get(FName Name);
	DEMIURGE_API bool Has(FName Name);
	DEMIURGE_API TArray<FName> Names();

	/** Draw an icon with its top-left at Pos (local units of Geometry), Size x Size. */
	DEMIURGE_API void Paint(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, FName Name, const FVector2f& Pos,
		float Size, const FLinearColor& Color, float StrokeScale = 1.f);

	/** Parse a path string (tests and the table). */
	DEMIURGE_API FIcon Parse(const TCHAR* Path);
}

/** A leaf widget drawing one icon. */
class DEMIURGE_API SDmIcon : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDmIcon)
		: _Icon(FName(TEXT("dot")))
		, _Size(20.f)
		, _Color(FLinearColor::White)
		, _Stroke(1.f)
	{}
		SLATE_ATTRIBUTE(FName, Icon)
		SLATE_ATTRIBUTE(float, Size)
		SLATE_ATTRIBUTE(FLinearColor, Color)
		SLATE_ATTRIBUTE(float, Stroke)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override;

private:
	TAttribute<FName> Icon;
	TAttribute<float> Size;
	TAttribute<FLinearColor> Color;
	TAttribute<float> Stroke;
};
