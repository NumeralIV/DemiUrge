// DEMIURGE — Game/UI/Hud/SDmHud.h : the HUD root (old SPEC-UI 4 + 4b) in Slate. Owner: C7 hud.
//
// Wide layout (viewport >= 600 units wide):
//   top left    menu (#hud-menu), the age ring with its icon, world name, year (#hud-year), age (#hud-age)
//   top centre  the Awe orb and number (#hud-awe), "+n / yr", epithet and regard; the Ark bar; the genesis
//               guide / first-steps card (#hud-genesis, #hud-genesis-next); toasts (#hud-toasts)
//   top right   people (#hud-pop), cultures, temperature anomaly, sea change (each once non-zero), time
//               controls (speed 0 = pause, 0.25, 1, 2, 4, 8), hide-HUD
//   left        the now strip (#pan-now) or, while a settlement is selected, the inspector (#pan-inspector)
//   right       the chronicle feed and its book button (#pan-chronicle-open)
//   bottom      the power palette ([data-power]: locked / poor / armed), the armed chip (#hud-armed) with
//               brush size, the power tip; bottom right the overlay switch ([data-overlay]) and its legend
//   centre      weight-3 banners, the chronicle book (#pan-chronicle)
//   anywhere    settlement labels (#labels), the hover tip near the pointer
//   hidden HUD  only the eye button (#hud-eye), 44 px, top right
// Narrow layout (< 600 units, phones): a two-row top bar, the chronicle as a one-line ticker, four group
// tabs and one row of that group's powers (icon and cost), panels as one bottom sheet at a time.
// The selector names of old SPEC-UI 0 are kept as widget tags (FDmHudIds) for tests.
#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Widgets/SCompoundWidget.h"

class FDmHudContext;
class SBox;
class SBorder;
class SDmInspector;
class SDmChronicleBook;

class DEMIURGE_API SDmHud : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDmHud) {}
		SLATE_ARGUMENT(TSharedPtr<FDmHudContext>, Context)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Local size of the HUD (Slate units after the root scale). */
	FVector2D GetLocalSize() const { return LocalSize; }

private:
	TSharedPtr<FDmHudContext> Ctx;
	TSharedPtr<SBox> Layout;
	int32 BuiltMode = -1;          // 0 wide, 1 narrow
	FVector2D LocalSize = FVector2D(1920, 1080);
	FGeometry Geo;

	void Rebuild(bool bNarrow);
	TSharedRef<SWidget> BuildWide();
	TSharedRef<SWidget> BuildNarrow();

	// sections
	TSharedRef<SWidget> MenuButton();
	TSharedRef<SWidget> AgeBlock(bool bCompact);
	TSharedRef<SWidget> AwePanel();
	TSharedRef<SWidget> AweCompact();
	TSharedRef<SWidget> ArkBar();
	TSharedRef<SWidget> GuideCard(float Width);
	TSharedRef<SWidget> Toasts();
	TSharedRef<SWidget> StatsRow(bool bSmall);
	TSharedRef<SWidget> SpeedControls(bool bCompact);
	TSharedRef<SWidget> Palette();
	TSharedRef<SWidget> NarrowPalette();
	TSharedRef<SWidget> PowerButton(int32 Index, bool bNarrow);
	TSharedRef<SWidget> ArmedChip();
	TSharedRef<SWidget> PowerTip();
	TSharedRef<SWidget> OverlaySwitch(bool bNarrow);
	TSharedRef<SWidget> Legend();
	TSharedRef<SWidget> Banner();
	TSharedRef<SWidget> HoverTip();
	TSharedRef<SWidget> EyeButton();
	TSharedRef<SWidget> HideButton();
	TSharedRef<SWidget> RoundIconButton(FName Icon, TFunction<void()> OnClick, TFunction<bool()> IsOn, float Size = 44.f, const FString& Tag = FString());
	TSharedRef<SWidget> Ticker();
	TSharedRef<SWidget> Sheet();

	// hover tip timing and rows (fixed widgets; the update sets their text)
	int32 TipCell = -1;
	double TipSince = 0;
	FVector2D TipPx = FVector2D::ZeroVector;
	bool bTipShown = false;
	uint32 TipBuildKey = 0;
	double TipBuilt = -100;
	int32 LastSelected = -1;
	struct FTipRow
	{
		FString Text;
		FSlateFontInfo Font;
		FLinearColor Color = FLinearColor::White;
		bool bShow = false;
	};
	FTipRow TipRows[6];
	void UpdateHoverTip();

	// legend content follows the overlay
	TSharedPtr<SBox> LegendBox;
	int32 LegendMode = -1;
	int32 LegendCultures = -1;
	void UpdateLegend();
};

/** Tags for tests / the integration engineer (old SPEC-UI 0 selectors). */
namespace FDmHudIds
{
	inline const TCHAR* Menu = TEXT("hud-menu");
	inline const TCHAR* Year = TEXT("hud-year");
	inline const TCHAR* Age = TEXT("hud-age");
	inline const TCHAR* Awe = TEXT("hud-awe");
	inline const TCHAR* Pop = TEXT("hud-pop");
	inline const TCHAR* Eye = TEXT("hud-eye");
	inline const TCHAR* Armed = TEXT("hud-armed");
	inline const TCHAR* Genesis = TEXT("hud-genesis");
	inline const TCHAR* GenesisNext = TEXT("hud-genesis-next");
	inline const TCHAR* Toasts = TEXT("hud-toasts");
	inline const TCHAR* Inspector = TEXT("pan-inspector");
	inline const TCHAR* ChronicleOpen = TEXT("pan-chronicle-open");
	inline const TCHAR* Chronicle = TEXT("pan-chronicle");
	inline const TCHAR* Now = TEXT("pan-now");
}
