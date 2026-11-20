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
	int32 Count = 1;             // repeats of the same text merge (shown as "x N")
	FString Key;                 // a group ("prayer", "lost", "wake", "war"): one combined toast per group per 10 s
	bool bGrouped = false;       // the text itself carries the count ("7 prayers answered")
};

/** One combined-toast group (review finding 8): what it has gathered since its window opened. */
struct FDmHudToastGroup
{
	double First = -100;         // HUD time the window opened
	int32 Count = 0;
	TArray<FString> Names;       // the first few subjects, in order
};

/** A first-steps hint as it was shown (for the first-minutes check). */
struct FDmHudHintRecord
{
	int32 Index = -1;
	double Born = 0, Shown = 0;  // HUD time it appeared; seconds it stayed
	double Year = 0;             // world year when it appeared
};

/** One step of the first-hour guidance thread. */
struct FDmGuideStep
{
	const TCHAR* Title;
	const TCHAR* Text;
	const TCHAR* Icon;
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
	// "how they will remember you" (review finding 12): the verdict (society::memory) of the culture the ending would ask
	// (the Ark's people once they build it, else the leading culture), on the ending's own thresholds
	bool bWatched = true;                 // the god has done nothing they saw yet: the word is "Watched"
	int32 RegardCulture = -1;
	FString RegardCultureName;            // "the Velu"
	FLinearColor RegardColor = FLinearColor::White;
	float RegardLove = 0.f, RegardDevotion = 0.f;   // RegardLove holds memory().verdict (across), RegardDevotion devotion (up)
	float TBelovedDev = 0.35f, TBelovedLove = 0.2f, TFearedLove = -0.2f, TForgottenDev = 0.2f;
	FString RegardLine;                   // "If the story ended now, the Velu would leave you beloved."
	FString RegardWhy;                    // when memory overrides the map (silence, two hands, answered prayers, the Ark)

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
	double HintSeconds = 12.0;            // a hint leaves by itself after this long (review finding 12)
	double HintGather = 0.6;              // a hint waits this long for others that arrive with it (teaching order)
	double HintQueueSince = 0;
	TArray<FDmHudHintRecord> HintHistory; // every hint shown in this session, in order
	double WorldYear = 0;
	static const TCHAR* HintText(int32 Index);
	void TriggerHint(int32 Index);
	void DismissHint();
	void LoadHints();
	void SaveHints() const;
	void ResetHints();
	FString HintsPath() const;

	// "how they will remember you" card (opened from the regard mark under the epithet)
	bool bRememberOpen = false;
	bool bRememberEverOpened = false;
	TArray<FVector2f> RegardTrail;        // (love, devotion) of the asked culture, one sample a second, newest last
	double LastTrailSample = -100;
	void ToggleRemember();

	// first-hour guidance thread (unobtrusive, dismissable; remembered on disk once dismissed)
	static constexpr int32 GuideStepCount = 7;
	static const FDmGuideStep& GuideStep(int32 Index);
	uint32 GuideDone = 0;                 // bit per step done in this world
	int32 GuideCurrent = 0;               // first step not done (GuideStepCount = all done)
	bool bGuideDismissed = false;         // the player closed it (persists)
	bool bGuideCollapsed = false;         // one line only
	bool bGuideVisible = false;           // computed at 4 Hz
	double GuideStepBorn = -100;          // when the current step appeared (a short highlight)
	FString GuideJustDone;                // title of the step finished last (shown for a few seconds)
	double GuideJustDoneAt = -100;
	void DismissGuide();
	void LoadGuide();
	void ResetGuide();                    // tests: forget the dismissal
	FString GuidePath() const;

	// toast statistics (merging)
	int32 ToastsPushed = 0;               // calls to PushToast / PushGrouped
	int32 ToastsCreated = 0;              // new toast rows that appeared
	double ToastMergeSeconds = 10.0;
	TMap<FString, FDmHudToastGroup> ToastGroups;
	int32 SeaToldIndex = -1;              // rising-sea explanations given (+3 m, +10 m, +25 m); -1 = not looked yet

	// ------------------------------------------------------------ refresh
	void Refresh(const dm::World* W);      // 4 Hz
	void RefreshNow(const dm::World* W);   // 2 Hz
	void RefreshGuide(const dm::World* W); // the guidance thread (called by Refresh)
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
	/** A toast that merges with others of its group for ToastMergeSeconds: "7 prayers answered: A, B and 5 more". */
	void PushGrouped(const FString& Key, const FString& Name, const FString& Kind, double Life = 4.0);
	static FString GroupedText(const FString& Key, const FDmHudToastGroup& G);
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
	// guidance thread bookkeeping
	int32 GuidePrevSelected = -2;         // -2: not looked yet (the flow's own first selection does not count)
	bool bPrayerAnswered = false;
	bool bChronicleEverOpened = false;
};
