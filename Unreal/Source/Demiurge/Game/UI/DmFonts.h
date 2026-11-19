// DEMIURGE — Game/UI/DmFonts.h : the shipped open fonts (SIL OFL 1.1) for every interface module.
// Owner: C10 integration.
//
// The four families of assets/fonts (T-009; docs/concept/ui/index.md gives the roles) are copied into
// Content/Fonts by Scripts/fonts.ps1 and staged into the packaged game as UFS files (DefaultGame.ini).
// They replace the Windows stand-in fonts (Palatino, Segoe UI) the HUD and the screens used:
//
//   Cinzel             small-caps headings, the DEMIURGE logo
//   Cormorant Garamond display text of 20 px and up (titles, year, epithets in italic)
//   EB Garamond        body, chronicle, place labels, menus (Regular, Medium, Italic)
//   Inter              numbers, keys, controls, stats (Regular, SemiBold, Bold)
//
//   TSharedPtr<const FCompositeFont> F = DmFonts::Face(EDmFontFile::Garamond);   // nullptr if missing
//   FSlateFontInfo Info = DmFonts::Info(EDmFontFile::InterSemi, 13.f * 0.75f);  // Slate points
//
// A missing file (a broken install) falls back to the engine's default face; nothing breaks.
#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

struct FCompositeFont;

enum class EDmFontFile : uint8
{
	Garamond,           // EB Garamond Regular
	GaramondMedium,     // EB Garamond Medium
	GaramondItalic,     // EB Garamond Italic
	CormorantSemi,      // Cormorant Garamond SemiBold
	CormorantMedium,    // Cormorant Garamond Medium
	CormorantItalic,    // Cormorant Garamond Italic
	Cinzel,             // Cinzel Regular
	Inter,              // Inter Regular
	InterSemi,          // Inter SemiBold
	InterBold,          // Inter Bold
	Count
};

namespace DmFonts
{
	/** Absolute path of the file (Content/Fonts/...), empty when it is not there. */
	DEMIURGE_API FString Path(EDmFontFile File);
	/** A composite font whose default typeface is this one file (cached; nullptr when missing). */
	DEMIURGE_API TSharedPtr<const FCompositeFont> Face(EDmFontFile File);
	/** Slate font info at Pt points (the engine default face when the file is missing). */
	DEMIURGE_API FSlateFontInfo Info(EDmFontFile File, float Pt);
	/** How many of the files were found (logs and checks). */
	DEMIURGE_API int32 CountAvailable();
}
