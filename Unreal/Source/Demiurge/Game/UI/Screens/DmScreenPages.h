// DEMIURGE — Game/UI/Screens/DmScreenPages.h : the pages of the screens (SPEC-UI 5 and 6). Owner: C8 screens.
// The subsystem asks for the page of the top screen; the host shows it. Pages read the subsystem's state
// every time they are built, and their live numbers (sliders, timelapse) read it through attributes.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Game/UI/Screens/DmScreenTypes.h"

class UDmScreenSubsystem;
struct FSlateDynamicImageBrush;

namespace DmPages
{
	/** The page of a screen, built from the subsystem's state. */
	TSharedRef<SWidget> Build(UDmScreenSubsystem* S, EDmScreen Screen);

	/** The screen lets clicks through to the planet (the title's right side, the timelapse). */
	bool IsPassThrough(EDmScreen Screen);

	/** The veil under the screen (the title's left gradient, the timelapse's bottom gradient, a dim for panels). */
	const FSlateBrush* ScrimFor(EDmScreen Screen);

	/** The planet card as one widget: the preview (bExport false, with its buttons) or the bare card for the PNG. */
	TSharedRef<SWidget> CardWidget(UDmScreenSubsystem* S, bool bExport);

	/** Width / height of the card's photograph (the captured planet is cropped to it). */
	float CardPhotoAspect();

	/** The card's size at interface scale 1 (the PNG is drawn at 1.25 times this). */
	constexpr float CardWidthBase = 960.f;
	constexpr float CardHeightBase = 600.f;
}
