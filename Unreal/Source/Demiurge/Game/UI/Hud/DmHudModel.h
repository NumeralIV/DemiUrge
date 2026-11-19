// DEMIURGE — Game/UI/Hud/DmHudModel.h : what the HUD shows, computed from the world at <= 4 Hz, and the
// HUD's transient state (toasts, banner, hints, chronicle feed, open panels). Owner: C7 hud.
//
// One FDmHudContext per HUD (shared by every widget through TSharedRef). ADmHud refreshes it: the model
// at 4 Hz (numbers), the now strip at 2 Hz, events as they arrive (OnSimEvent), animations per frame.
// Widgets only read it (Slate attributes) and call its actions. All text is plain language; numbers
// use dm::fmtBig; no internal ids reach the player.
#pragma once

#include "CoreMinimal.h"
#include "Game/UI/Hud/DmHudControl.h"
#include "Sim/Powers.h"
#include "Sim/Schema.h"

namespace dm { struct World; struct Event; struct ChronicleEntry; }

struct FDmHudPower
{
	FString Id, Name, Desc, Tip, CostText, KeyText;
	FName Icon;
	dm::powers::Group Group = dm::powers::Group::Shape;
	bool bBrush = false;
	bool bVisible = false;       // its group is shown in this phase
	bool bUnlocked = false;
	bool bPoor = false;          // cannot pay now
	bool bOverCap = false;       // costs more than the Awe can hold
	bool bArmed = false;
	double Cost = 0;
	double PulseStart = -100;    // newly unlocked: pulse once
};

struct FDmHudNowItem
{
	FString Text;
	FName Icon;
	FLinearColor Color = FLinearColor::White;
	TArray<int32> Cells;         // click cycles through these
};

struct FDmHudFeedItem
{
	int32 EntryId = -1;
	FString Year, Text;
	int32 Cell = -1, Culture = -1, Weight = 1;
	double Born = 0;             // HUD time it arrived
};

struct FDmHudToast
{
	FString Text, Kind;          // info | good | bad | epoch
	double Born = 0, Life = 4.0;
	int32 Count = 1;             // repeats of the same text merge
};

struct FDmHudBanner
{
	FString Heading, Text, Year;
	int32 Cell = -1;
	double Born = -100, Life = 7.0;
	bool Active(double Now) const { return Now - Born < Life; }
};

/** One settlement label on screen this frame (#labels). */
struct FDmHudLabel
{
	int32 Sid = -1;
	FVector2D Px = FVector2D::ZeroVector;   // viewport pixels of the settlement centre
	float Alpha = 0.f;
	FString Name, Sigil;
	int32 Tier = 0;
	bool bCapital = false;
	FLinearColor Color = FLinearColor::White;
	FName Status[2];
	float TextW = 0.f;                      // measured name width (Slate units)
};

struct FDmHudModel
{
	bool bWorld = false;
	dm::Phase Phase = dm::Phase::Molten;
	bool bGenesis = false;
	FString WorldName, YearText, AgeText;
	int32 Age = 0;
	float AgeProgress = 0.f;

	double Awe = 0, AweMax = 100, AweIncome = 0;
	FString AweText, AweMaxText, IncomeText;
	bool bBelievers = false;
	FString Epithet, RegardWord;
	FName RegardIcon;
	dm::Mood Mood = dm::Mood::Forgotten;

	FString PopText, CulturesText, AnomalyText, SeaText;
	bool bShowPop = false, bShowCultures = false, bShowAnomaly = false, bShowSea = false;
	bool bAnomalyBad = false, bSeaBad = false;

	bool bArk = false;
	float ArkProgress = 0.f;
	FString ArkCaption;
	int32 ArkSettlement = -1;

	float Speed = 1.f;
	bool bPaused = false;

	// genesis guide
	bool bGuide = false;
	FString GuideText, GuideExtra;
	float GuideProgress = 0.f;
	bool bCanAdvance = false;
	bool bPeopleLost = false;

	TArray<FDmHudPower> Powers;           // dm::powers::kPowers order
	FString ArmedId;
	float BrushAng = 0.06f;
	int32 Overlay = 0;

	int32 Selected = -1;
	TArray<FDmHudNowItem> Now;

	TArray<int32> LiveCultures;           // overlay legend (cultures)
	uint32 Signature = 0;                 // changes when the palette needs rebuilding
};

class DEMIURGE_API FDmHudContext : public TSharedFromThis<FDmHudContext>
{
public:
	FDmHudControl Control;
	FDmHudModel M;
	double Now = 0;                       // HUD seconds (real time)
	float UiScale = 1.f;                  // CSS px -> Slate units scale applied by the root
	bool bNarrow = false;
	FVector2D ViewportPx = FVector2D(1920, 1080);
	float DpiToPx = 1.f;                  // Slate units of the root -> viewport pixels

	// visibility (old Game.state === 'play' and not hidden)
	bool bPlayState = true;
	bool bEnded = false;

	// transient UI state
	FString HoverPower;                   // palette button under the pointer (power tip)
	bool bChronicleOpen = false;
	int32 BookCulture = -1;               // -1 = every culture
	int32 BookWeight = 1;                 // minimum weight shown
	int32 NarrowGroup = 1;                // dm::powers::Group as int (1 Shape .. 4 Grace)
	int32 NarrowSheet = 0;                // 0 none, 1 inspector, 2 chronicle, 3 overlays
	int32 NowCycle = 0;

	TArray<FDmHudToast> Toasts;
	FDmHudBanner Banner;
	TArray<FDmHudLabel> Labels;           // settlement labels, every frame while the camera moves
	TMap<int32, float> LabelAlpha;        // fade state per settlement
	TMap<int32, float> LabelWidth;        // measured name widths
	TMap<int32, FVector2D> LastLabelPx;   // where each label was last drawn (a fading label fades where it was)
	double CameraAltitudeKm = 700;
	bool bLabels = true;                  // setting 'labels'
	TMap<int32, FVector2D> PowerButtonAbs; // absolute centre-top of each palette button (power tip placement)
	double LastModelMs = 0, AvgModelMs = 0, AvgLabelMs = 0;
	TArray<FDmHudFeedItem> Feed;          // newest last

	// first-steps hints (old SPEC-UI 4): shown once each, remembered on disk
	int32 HintShown = -1;                 // index 0..6 on screen, -1 none
	double HintBorn = 0;
	TArray<int32> HintQueue;
	TSet<int32> HintsSeen;
	bool bHintsEnabled = true;
	static const TCHAR* HintText(int32 Index);
	void TriggerHint(int32 Index);
	void DismissHint();
	void LoadHints();
	void SaveHints() const;
	void ResetHints();
	FString HintsPath() const;

	// ------------------------------------------------------------ refresh
	void Refresh(const dm::World* W);      // 4 Hz
	void RefreshNow(const dm::World* W);   // 2 Hz
	void OnEvent(const dm::Event& E, const dm::World* W);
	void Tick(double InNow);
	/** A new, loaded or cleared world: per-world state starts again (chronicle ids, unlocks, toasts, feed). */
	void ResetWorld();

	// ------------------------------------------------------------ actions (widgets call these)
	void ClickPower(const FString& Id);
	void ClickSpeed(float Speed);
	void ClickOverlay(int32 Mode);
	void FlyTo(int32 Cell);
	void ClickNow(int32 Index);
	void PushToast(const FString& Text, const FString& Kind, double Life = 4.0);
	void ShowBanner(const dm::ChronicleEntry& E, const dm::World* W);

	// ------------------------------------------------------------ text helpers
	static FString Big(double N);
	static FString Signed(double V, int32 Decimals);
	static FString TierName(int32 Tier);
	static FString AgeName(int32 Age);
	static FString PowerUnlockText(const dm::World& W, const dm::powers::PowerDef& D);
	static FString RegardWordOf(dm::Mood M);
	static FName RegardIconOf(dm::Mood M);
	static FString MoodPhrase(dm::Mood M);
	static FString BannerHeading(const FString& Type);
	static FLinearColor GroupColor(dm::powers::Group G);
	static FString GroupName(dm::powers::Group G);

private:
	TSet<FString> UnlockedBefore;
	bool bUnlockKnown = false;
	double LastAweShort = -100;
	int32 LastChronicleSeen = -1;         // highest chronicle entry id shown (-1: none yet; ids start at 0)
	bool bChronicleBaselined = false;     // the world's history has been looked at once (last few shown quietly)
	int32 LastPhaseRank = -1;
};
