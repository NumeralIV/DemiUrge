// DEMIURGE — Game/UI/DmFonts.cpp : see DmFonts.h. Owner: C10 integration.

#include "Game/UI/DmFonts.h"

#include "Fonts/CompositeFont.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmFonts, Log, All);

namespace
{
	const TCHAR* const GFiles[int32(EDmFontFile::Count)] = {
		TEXT("eb-garamond/EBGaramond-Regular.ttf"),
		TEXT("eb-garamond/EBGaramond-Medium.ttf"),
		TEXT("eb-garamond/EBGaramond-Italic.ttf"),
		TEXT("cormorant-garamond/CormorantGaramond-SemiBold.otf"),
		TEXT("cormorant-garamond/CormorantGaramond-Medium.otf"),
		TEXT("cormorant-garamond/CormorantGaramond-Italic.otf"),
		TEXT("cinzel/Cinzel-Regular.ttf"),
		TEXT("inter/Inter-Regular.ttf"),
		TEXT("inter/Inter-SemiBold.ttf"),
		TEXT("inter/Inter-Bold.ttf"),
	};

	struct FCache
	{
		TSharedPtr<const FCompositeFont> Fonts[int32(EDmFontFile::Count)];
		bool bTried[int32(EDmFontFile::Count)] = {};
	};
	FCache& Cache()
	{
		static FCache C;
		return C;
	}
}

FString DmFonts::Path(EDmFontFile File)
{
	const int32 I = int32(File);
	if (I < 0 || I >= int32(EDmFontFile::Count)) return FString();
	const FString P = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Fonts"), GFiles[I]);
	return FPlatformFileManager::Get().GetPlatformFile().FileExists(*P) ? P : FString();
}

TSharedPtr<const FCompositeFont> DmFonts::Face(EDmFontFile File)
{
	const int32 I = int32(File);
	if (I < 0 || I >= int32(EDmFontFile::Count)) return nullptr;
	FCache& C = Cache();
	if (C.bTried[I]) return C.Fonts[I];
	C.bTried[I] = true;
	const FString P = Path(File);
	if (P.IsEmpty())
	{
		UE_LOG(LogDmFonts, Warning, TEXT("DmFonts: %s missing (run Scripts/fonts.ps1); the engine font stands in"), GFiles[I]);
		return nullptr;
	}
	const FString Name = FPaths::GetBaseFilename(P);
	C.Fonts[I] = MakeShared<FCompositeFont>(FName(*Name), P, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
	UE_LOG(LogDmFonts, Display, TEXT("DmFonts: %s"), *Name);
	return C.Fonts[I];
}

FSlateFontInfo DmFonts::Info(EDmFontFile File, float Pt)
{
	TSharedPtr<const FCompositeFont> F = Face(File);
	if (F.IsValid()) return FSlateFontInfo(F, Pt);
	const bool bBold = File == EDmFontFile::InterSemi || File == EDmFontFile::InterBold || File == EDmFontFile::CormorantSemi;
	const bool bItalic = File == EDmFontFile::GaramondItalic || File == EDmFontFile::CormorantItalic;
	return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : (bItalic ? "Italic" : "Regular"), int32(FMath::RoundToInt(Pt)));
}

int32 DmFonts::CountAvailable()
{
	int32 N = 0;
	for (int32 I = 0; I < int32(EDmFontFile::Count); ++I) if (!Path(EDmFontFile(I)).IsEmpty()) ++N;
	return N;
}
