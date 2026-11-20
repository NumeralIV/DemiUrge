// DEMIURGE — Game/UI/Hud/DmHudModel.cpp : the HUD model, its events, hints, toasts and banners. Owner: C7 hud.

#include "Game/UI/Hud/DmHudModel.h"

#include "Game/Core/DmSimSubsystem.h"
#include "Game/UI/Hud/DmHudStyle.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Sim/Chronicle.h"
#include "Sim/Civ.h"
#include "Sim/Climate.h"
#include "Sim/Sim.h"
#include "Sim/Society.h"

namespace
{
	FString U(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
	std::string Utf8(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }

	int32 PhaseRank(dm::Phase P)
	{
		switch (P)
		{
		case dm::Phase::Molten: return 0;
		case dm::Phase::Water: return 1;
		case dm::Phase::Life: return 2;
		case dm::Phase::People: return 3;
		default: return 4;
		}
	}

	int32 LeadingCulture(const dm::World& W)
	{
		int32 Best = -1;
		for (const dm::Culture& C : W.cultures)
		{
			if (C.dead) continue;
			if (Best < 0) { Best = C.id; continue; }
			const dm::Culture& B = W.cultures[size_t(Best)];
			if (C.age > B.age || (C.age == B.age && (C.science > B.science || (C.science == B.science && C.pop > B.pop)))) Best = C.id;
		}
		return Best;
	}

	FString CultureName(const dm::World& W, int32 Id)
	{
		const dm::Culture* C = dm::state::culture(W, Id);
		if (!C) return TEXT("a people");
		return C->name.empty() ? FString(TEXT("a people")) : U(C->name);
	}

	FString PluralOf(const dm::World& W, int32 Id)
	{
		const dm::Culture* C = dm::state::culture(W, Id);
		if (!C) return TEXT("people");
		FString P = C->plural.empty() ? U(C->name) : U(C->plural);
		if (P.StartsWith(TEXT("the "), ESearchCase::IgnoreCase)) P.RightChopInline(4);
		return P.IsEmpty() ? FString(TEXT("people")) : P;
	}

	/** "A", "A and B", "A, B and 3 more". */
	FString NameList(const TArray<FString>& Names, int32 Count)
	{
		const int32 Shown = FMath::Min(Names.Num(), 2);
		if (Shown == 0) return FString();
		const int32 More = Count - Shown;
		if (Shown == 1) return More > 0 ? FString::Printf(TEXT("%s and %d more"), *Names[0], More) : Names[0];
		if (More <= 0) return FString::Printf(TEXT("%s and %s"), *Names[0], *Names[1]);
		return FString::Printf(TEXT("%s, %s and %d more"), *Names[0], *Names[1], More);
	}

	FString PrayerWord(dm::PrayerKind K)
	{
		switch (K)
		{
		case dm::PrayerKind::Rain: return TEXT("rain");
		case dm::PrayerKind::Heal: return TEXT("healing");
		case dm::PrayerKind::Peace: return TEXT("peace");
		default: return TEXT("green land");
		}
	}
}

// ------------------------------------------------------------------ text helpers

FString FDmHudContext::Big(double N)
{
	return U(dm::fmtBig(N));
}

FString FDmHudContext::Signed(double V, int32 Decimals)
{
	const FString Num = FString::Printf(TEXT("%.*f"), Decimals, FMath::Abs(V));
	if (FMath::Abs(V) < 0.5 * FMath::Pow(10.0, -double(Decimals))) return Num;
	return (V < 0 ? FString(TEXT("−")) : FString(TEXT("+"))) + Num;
}

FString FDmHudContext::TierName(int32 Tier)
{
	static const TCHAR* Names[5] = { TEXT("Camp"), TEXT("Village"), TEXT("Town"), TEXT("City"), TEXT("Metropolis") };
	return Names[FMath::Clamp(Tier, 0, 4)];
}

FString FDmHudContext::AgeName(int32 Age)
{
	return U(dm::kAgeNames[FMath::Clamp(Age, 0, int32(dm::Age::COUNT) - 1)]);
}

FString FDmHudContext::PowerUnlockText(const dm::World& W, const dm::powers::PowerDef& D)
{
	const bool bGenesisOnly = D.unlock.phaseOnly;
	if (bGenesisOnly) return TEXT("Only while the world is young.");
	if (PhaseRank(W.phase) < PhaseRank(D.unlock.phase))
	{
		if (D.unlock.phase == dm::Phase::History) return TEXT("Wakes when your people do.");
		return TEXT("Wakes when the world has cooled.");
	}
	if (D.unlock.awe <= 0) return FString::Printf(TEXT("Wakes in the %s Age."), *AgeName(D.unlock.age));
	return FString::Printf(TEXT("Wakes in the %s Age, or when your Awe can hold %s."), *AgeName(D.unlock.age), *Big(D.unlock.awe));
}

FString FDmHudContext::RegardWordOf(dm::Mood M)
{
	switch (M)
	{
	case dm::Mood::Beloved: return TEXT("Beloved");
	case dm::Mood::Feared: return TEXT("Feared");
	case dm::Mood::Forgotten: return TEXT("Forgotten");
	default: return TEXT("In parting");
	}
}

FName FDmHudContext::RegardIconOf(dm::Mood M)
{
	switch (M)
	{
	case dm::Mood::Beloved: return FName(TEXT("heart"));
	case dm::Mood::Feared: return FName(TEXT("skull"));
	case dm::Mood::Forgotten: return FName(TEXT("moon"));
	default: return FName(TEXT("sun"));
	}
}

FString FDmHudContext::MoodPhrase(dm::Mood M)
{
	switch (M)
	{
	case dm::Mood::Beloved: return TEXT("beloved");
	case dm::Mood::Feared: return TEXT("feared");
	case dm::Mood::Forgotten: return TEXT("forgotten");
	default: return TEXT("in parting");
	}
}

FString FDmHudContext::BannerHeading(const FString& Type)
{
	if (Type == TEXT("ark")) return TEXT("The Ark");
	if (Type == TEXT("awakening")) return TEXT("The Awakening");
	if (Type == TEXT("culture-born")) return TEXT("A People Is Born");
	if (Type == TEXT("culture-died")) return TEXT("A People Ends");
	if (Type == TEXT("ending")) return TEXT("The Last Page");
	if (Type == TEXT("genesis")) return TEXT("Genesis");
	if (Type == TEXT("lost")) return TEXT("Silence");
	if (Type == TEXT("nuke")) return TEXT("Fire Without Smoke");
	if (Type == TEXT("tier")) return TEXT("A Great City");
	if (Type == TEXT("wonder")) return TEXT("A Wonder Rises");
	if (Type == TEXT("destroyed")) return TEXT("A City Falls");
	if (Type == TEXT("age")) return TEXT("A New Age");
	return TEXT("From the Chronicle");
}

FLinearColor FDmHudContext::GroupColor(dm::powers::Group G)
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	switch (G)
	{
	case dm::powers::Group::Genesis: return S.Awe;
	case dm::powers::Group::Shape: return FDmHudStyle::Hex(TEXT("d8b48c"));
	case dm::powers::Group::Sky: return S.Water;
	case dm::powers::Group::Wrath: return S.Bad;
	default: return S.Good;
	}
}

FString FDmHudContext::GroupName(dm::powers::Group G)
{
	switch (G)
	{
	case dm::powers::Group::Genesis: return TEXT("Genesis");
	case dm::powers::Group::Shape: return TEXT("Shape");
	case dm::powers::Group::Sky: return TEXT("Sky");
	case dm::powers::Group::Wrath: return TEXT("Wrath");
	default: return TEXT("Grace");
	}
}

// ------------------------------------------------------------------ hints

const TCHAR* FDmHudContext::HintText(int32 Index)
{
	static const TCHAR* Texts[7] = {
		TEXT("Your people are awake. Their faith fills the gold orb with Awe. Awe is what miracles cost."),
		TEXT("Arm Rain and hold it over dry ground near them. They will notice."),
		TEXT("A shrine. Your Awe now holds more and fills faster."),
		TEXT("They are asking for you. Answer or do not; they will remember either."),
		TEXT("War. An Omen of Peace ends it; a Prophet can forbid it."),
		TEXT("A new age. New powers wake with them."),
		TEXT("They are building a ship to leave. How they feel about you now is how the story ends."),
	};
	return (Index >= 0 && Index < 7) ? Texts[Index] : TEXT("");
}

FString FDmHudContext::HintsPath() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Demiurge"), TEXT("hints.v1.txt"));
}

void FDmHudContext::LoadHints()
{
	HintsSeen.Reset();
	FString Text;
	if (FFileHelper::LoadFileToString(Text, *HintsPath()))
	{
		TArray<FString> Parts;
		Text.ParseIntoArrayWS(Parts);
		for (const FString& P : Parts) HintsSeen.Add(FCString::Atoi(*P));
	}
}

void FDmHudContext::SaveHints() const
{
	FString Text;
	for (int32 I : HintsSeen) Text += FString::Printf(TEXT("%d\n"), I);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(HintsPath()), true);
	FFileHelper::SaveStringToFile(Text, *HintsPath());
}

void FDmHudContext::ResetHints()
{
	HintsSeen.Reset();
	HintQueue.Reset();
	HintShown = -1;
	IFileManager::Get().Delete(*HintsPath(), false, true, true);
}

void FDmHudContext::TriggerHint(int32 Index)
{
	if (!bHintsEnabled || Index < 0 || Index > 6 || HintsSeen.Contains(Index) || HintShown == Index || HintQueue.Contains(Index)) return;
	HintsSeen.Add(Index);
	SaveHints();
	// hints wait in teaching order (the index is the order they teach in); one that arrives while none is up
	// waits a moment (HintGather) so that hints arriving together still show in that order (review finding 12:
	// the shrine hint came before "your people are awake")
	if (HintShown < 0 && HintQueue.Num() == 0) HintQueueSince = Now;
	HintQueue.Add(Index);
	HintQueue.Sort();
}

void FDmHudContext::DismissHint()
{
	if (HintShown >= 0 && HintHistory.Num() > 0 && HintHistory.Last().Index == HintShown) HintHistory.Last().Shown = Now - HintHistory.Last().Born;
	HintShown = -1;
	if (HintQueue.Num() > 0)
	{
		HintShown = HintQueue[0];
		HintQueue.RemoveAt(0);
		HintBorn = Now;
		FDmHudHintRecord R; R.Index = HintShown; R.Born = Now; R.Year = WorldYear;
		HintHistory.Add(R);
	}
}

// ------------------------------------------------------------------ the guidance thread

const FDmGuideStep& FDmHudContext::GuideStep(int32 Index)
{
	static const FDmGuideStep Steps[GuideStepCount] = {
		{ TEXT("Find your people"), TEXT("Click a town to see what its people need. Close its panel with the cross to look around again."), TEXT("people") },
		{ TEXT("Make it rain"), TEXT("Arm Rain (N) and hold it over dry ground near a town. Every miracle they see makes them believe a little more."), TEXT("rain") },
		{ TEXT("How they will remember you"), TEXT("Click the small map beside your name, under the Awe orb: it shows how they would remember you if the story ended now."), TEXT("heart") },
		{ TEXT("Answer a prayer"), TEXT("When a town prays (it shows under Now), give it what it asks for. An answered prayer is remembered twice over."), TEXT("praying") },
		{ TEXT("A shrine"), TEXT("Let them raise a shrine: your Awe will hold more and fill faster. Their faith pays for every miracle."), TEXT("shrine") },
		{ TEXT("Read the chronicle"), TEXT("Open the chronicle, top right, to read what they write about you."), TEXT("scroll") },
		{ TEXT("Speak through a prophet"), TEXT("From the Iron Age, raise a Prophet (P) in a town, then choose an edict for that people in the town's panel."), TEXT("prophet") },
	};
	return Steps[FMath::Clamp(Index, 0, GuideStepCount - 1)];
}

FString FDmHudContext::GuidePath() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Demiurge"), TEXT("guide.v1.txt"));
}

void FDmHudContext::LoadGuide()
{
	FString Text;
	bGuideDismissed = FFileHelper::LoadFileToString(Text, *GuidePath()) && Text.Contains(TEXT("dismissed"));
}

void FDmHudContext::DismissGuide()
{
	bGuideDismissed = true;
	bGuideVisible = false;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(GuidePath()), true);
	FFileHelper::SaveStringToFile(FString(TEXT("dismissed")), *GuidePath());
}

void FDmHudContext::ResetGuide()
{
	bGuideDismissed = false;
	bGuideCollapsed = false;
	GuideDone = 0;
	GuideCurrent = 0;
	GuidePrevSelected = -2;
	GuideJustDone.Reset();
	GuideJustDoneAt = -100;
	IFileManager::Get().Delete(*GuidePath(), false, true, true);
}

void FDmHudContext::ToggleRemember()
{
	bRememberOpen = !bRememberOpen;
	if (bRememberOpen) bRememberEverOpened = true;
}

// ------------------------------------------------------------------ toasts, banner

void FDmHudContext::PushToast(const FString& Text, const FString& Kind, double Life)
{
	if (Text.IsEmpty()) return;
	++ToastsPushed;
	// the same words again while that toast is up: it counts ("x 3") and stays up without replaying its fade-in
	for (FDmHudToast& T : Toasts)
		if (!T.bGrouped && T.Text == Text && Now - T.Born < T.Life + 0.6) { T.Born = Now - 0.25; ++T.Count; return; }
	FDmHudToast T;
	T.Text = Text;
	T.Kind = Kind.IsEmpty() ? FString(TEXT("info")) : Kind;
	T.Born = Now;
	T.Life = Life;
	Toasts.Add(T);
	++ToastsCreated;
	while (Toasts.Num() > 4) Toasts.RemoveAt(0);
}

FString FDmHudContext::GroupedText(const FString& Key, const FDmHudToastGroup& G)
{
	const FString First = G.Names.Num() > 0 ? G.Names[0] : FString(TEXT("a town"));
	const FString List = NameList(G.Names, G.Count);
	if (Key == TEXT("prayer"))
		return G.Count <= 1 ? FString::Printf(TEXT("%s's prayer is answered"), *First) : FString::Printf(TEXT("%d prayers answered: %s"), G.Count, *List);
	if (Key == TEXT("lost"))
		return G.Count <= 1 ? FString::Printf(TEXT("%s is lost"), *First) : FString::Printf(TEXT("%d towns are lost: %s"), G.Count, *List);
	if (Key == TEXT("wake"))
		return FString::Printf(TEXT("%s %s in your hands."), *List, G.Count <= 1 ? TEXT("wakes") : TEXT("wake"));
	if (Key == TEXT("war"))
		return G.Count <= 1 ? FString::Printf(TEXT("War: %s"), *First) : FString::Printf(TEXT("%d wars begin: %s"), G.Count, *List);
	return G.Count <= 1 ? First : FString::Printf(TEXT("%s (%d)"), *First, G.Count);
}

void FDmHudContext::PushGrouped(const FString& Key, const FString& Name, const FString& Kind, double Life)
{
	if (Key.IsEmpty()) { PushToast(Name, Kind, Life); return; }
	++ToastsPushed;
	FDmHudToastGroup& G = ToastGroups.FindOrAdd(Key);
	const bool bOpen = Now - G.First < ToastMergeSeconds;
	if (!bOpen) { G.First = Now; G.Count = 0; G.Names.Reset(); }
	++G.Count;
	if (G.Names.Num() < 3 && !Name.IsEmpty() && !G.Names.Contains(Name)) G.Names.Add(Name);
	const FString Text = GroupedText(Key, G);
	// the group's row while it is still up: it grows ("3 prayers answered") and stays up without replaying its
	// fade-in. Verifier fix: also when a new window opens while the old row is still up (a steady stream for
	// longer than 10 s): before, a second row of the same group appeared and later merges went to the stale one.
	for (FDmHudToast& T : Toasts)
		if (T.Key == Key)
		{
			T.Text = Text;
			T.Born = Now - 0.25;
			T.Life = FMath::Max(T.Life, Life);
			return;
		}
	// a new row: the group has no row up (the window just opened, or its row faded: it comes back with the count)
	FDmHudToast T;
	T.Text = Text;
	T.Kind = Kind.IsEmpty() ? FString(TEXT("info")) : Kind;
	T.Born = Now;
	T.Life = Life;
	T.Key = Key;
	T.bGrouped = true;
	Toasts.Add(T);
	++ToastsCreated;
	while (Toasts.Num() > 4) Toasts.RemoveAt(0);
}

void FDmHudContext::ShowBanner(const dm::ChronicleEntry& E, const dm::World* W)
{
	Banner.Heading = BannerHeading(U(E.type));
	Banner.Text = U(E.text);
	Banner.Year = W ? U(dm::chronicle::yearStr(*W, E.year)) : FString();
	Banner.Cell = E.cell;
	Banner.Born = Now;
}

// ------------------------------------------------------------------ actions

void FDmHudContext::ClickPower(const FString& Id)
{
	Control.ArmPower(Id);
}

void FDmHudContext::ClickSpeed(float Speed)
{
	Control.SetSpeed(Speed);
}

void FDmHudContext::ClickOverlay(int32 Mode)
{
	Control.SetOverlay(Control.Overlay() == Mode ? 0 : Mode);
}

void FDmHudContext::FlyTo(int32 Cell)
{
	if (Cell >= 0) Control.FlyToCell(Cell);
}

void FDmHudContext::ClickNow(int32 Index)
{
	if (!M.Now.IsValidIndex(Index) || M.Now[Index].Cells.Num() == 0) return;
	const TArray<int32>& Cells = M.Now[Index].Cells;
	FlyTo(Cells[NowCycle % Cells.Num()]);
	++NowCycle;
}

// ------------------------------------------------------------------ refresh (4 Hz)

void FDmHudContext::Refresh(const dm::World* W)
{
	FDmHudModel& Md = M;
	Md.bWorld = W != nullptr;
	Md.ArmedId = Control.Armed();
	Md.BrushAng = Control.BrushAng();
	Md.Overlay = Control.Overlay();
	Md.Selected = Control.Selected();
	if (const UDmSimSubsystem* S = Control.Sim())
	{
		Md.Speed = S->GetSpeed();
		Md.bPaused = S->IsPaused();
	}
	if (!W) return;

	Md.Phase = W->phase;
	Md.bGenesis = PhaseRank(W->phase) < 4;
	Md.WorldName = W->name.empty() ? FString(TEXT("Unnamed")) : U(W->name);
	const int32 Lead = LeadingCulture(*W);
	Md.Age = W->age;
	Md.AgeProgress = Lead >= 0 ? float(FMath::Clamp(dm::civ::ageProgress(*W, Lead), 0.0, 1.0)) : 0.f;
	if (Md.bGenesis)
	{
		static const TCHAR* Phases[4] = { TEXT("The molten age"), TEXT("The age of water"), TEXT("The age of life"), TEXT("Before the people") };
		Md.YearText = TEXT("Genesis");
		Md.AgeText = Phases[PhaseRank(W->phase)];
	}
	else
	{
		Md.YearText = U(dm::chronicle::yearStr(*W));
		Md.AgeText = FString::Printf(TEXT("%s Age"), *AgeName(W->age));
	}

	// Awe and regard
	Md.Awe = W->god.awe;
	Md.AweMax = FMath::Max(1.0, W->god.aweMax);
	Md.AweIncome = dm::society::aweIncome(*W);
	Md.AweText = FString::Printf(TEXT("%d"), int32(FMath::FloorToDouble(FMath::Max(0.0, Md.Awe))));
	Md.AweMaxText = Big(Md.AweMax);
	Md.IncomeText = FString::Printf(TEXT("+%.1f / yr"), Md.AweIncome);
	const std::vector<int> Live = dm::civ::live(*W);
	Md.bBelievers = !Live.empty();
	// how they will remember you: the culture the ending would ask (the Ark's people once they build it, else
	// the leading culture), with the ending's own rule (Sim checkEnding: society::memory(culture).mood), so the
	// word and the ending agree (review finding 12). Verifier fix: the sim's v3 verdict is the long memory
	// (grace / wrath, silence, two hands, answered prayers), not today's love on the old SimTune thresholds.
	{
		const dm::SocietyTune& T = W->tune<dm::SocietyTune>();
		Md.TBelovedDev = float(T.belovedDevotion);
		Md.TBelovedLove = float(T.belovedLove);
		Md.TFearedLove = float(T.fearedLove);
		Md.TForgottenDev = float(T.forgottenDevotion);
		int32 Asked = Lead;
		if (W->vars.ark.has_value())
			if (const dm::Culture* AC = dm::state::culture(*W, W->vars.ark->culture)) if (!AC->dead) Asked = AC->id;
		const dm::society::Memory Mem = dm::society::memory(*W, Asked);
		Md.RegardCulture = Asked;
		// across: the verdict (what they remember of the god's deeds, with today's love); up: devotion
		Md.RegardLove = float(FMath::Clamp(Mem.verdict, -1.0, 1.0));
		Md.RegardDevotion = float(FMath::Clamp(Mem.devotion, 0.0, 1.0));
		Md.Mood = Mem.mood;
		// "Watched" until these people have witnessed a deed of the god (grace or wrath in their memory; rain
		// over an empty sea, or a natural disaster, does not count)
		Md.bWatched = Asked < 0 || !(Mem.grace + Mem.wrath > 1e-6);
		const dm::Culture* AC = dm::state::culture(*W, Asked);
		Md.RegardColor = AC ? DmCultureColor(AC->color) : FDmHudStyle::Get().Dim;
		Md.RegardCultureName = Asked >= 0 ? FString::Printf(TEXT("the %s"), *PluralOf(*W, Asked)) : FString(TEXT("your people"));
		Md.RegardWord = Md.bWatched ? FString(TEXT("Watched")) : RegardWordOf(Md.Mood);
		Md.RegardIcon = Md.bWatched ? FName(TEXT("eye")) : RegardIconOf(Md.Mood);
		if (Md.bGenesis || Asked < 0) Md.RegardLine = TEXT("No one is awake to remember you yet.");
		else if (Md.bWatched) Md.RegardLine = FString::Printf(TEXT("The %s are watching the sky. They have not seen you do anything yet."), *PluralOf(*W, Asked));
		else Md.RegardLine = FString::Printf(TEXT("If the story ended now, %s would leave you %s."), *Md.RegardCultureName, *MoodPhrase(Md.Mood));
		// why, when the rule is not where the dot stands (the map shows verdict and devotion only)
		Md.RegardWhy.Reset();
		if (!Md.bGenesis && Asked >= 0 && !Md.bWatched)
		{
			if (Mem.silent >= T.silentYears)
				Md.RegardWhy = FString::Printf(TEXT("You have been silent for %d years: an absent god is %s."), FMath::RoundToInt(Mem.silent), Mem.remembered ? TEXT("parted from, not loved") : TEXT("forgotten"));
			else if (Mem.mixed)
				Md.RegardWhy = TEXT("They remember both your hands, the kind and the cruel: they will part from you uneasy.");
			else if (Md.Mood == dm::Mood::Beloved && !(Mem.verdict >= T.belovedLove && Mem.devotion >= T.belovedDevotion))
				Md.RegardWhy = FString::Printf(TEXT("You answered %d of their %d prayers: that alone makes them love you."), Mem.answered, Mem.prayers);
			else if (W->vars.ark.has_value() && T.arkNoMemory)
				Md.RegardWhy = TEXT("They are building the Ark: what you do now moves their love, but it is too late to change their memory.");
		}
		if (!Md.bGenesis && Asked >= 0 && Now - LastTrailSample >= 1.0)
		{
			LastTrailSample = Now;
			RegardTrail.Add(FVector2f(Md.RegardLove, Md.RegardDevotion));
			while (RegardTrail.Num() > 90) RegardTrail.RemoveAt(0);
		}
	}
	Md.Epithet = W->god.epithet.empty() ? FString(TEXT("the Unnamed")) : U(W->god.epithet);
	WorldYear = W->year;

	// people, cultures, climate
	Md.LiveCultures.Reset();
	for (const dm::Culture& C : W->cultures) if (!C.dead) Md.LiveCultures.Add(C.id);
	Md.bShowPop = W->stats.pop >= 1.0;
	Md.PopText = Big(W->stats.pop);
	Md.bShowCultures = Md.LiveCultures.Num() > 0;
	Md.CulturesText = FString::Printf(TEXT("%d"), Md.LiveCultures.Num());
	const double Anom = dm::climate::anomaly(*W);
	Md.bShowAnomaly = FMath::Abs(Anom) >= 0.05;
	Md.bAnomalyBad = Anom > 0.5 || Anom < -1.0;
	Md.AnomalyText = Signed(Anom, 1) + TEXT("°");
	const double SeaD = W->vars.seaBase ? (W->seaLevel - *W->vars.seaBase) : 0.0;
	const double SeaM = SeaD * 200.0;   // 0.2 km per elevation unit
	Md.bShowSea = Md.bGenesis ? false : FMath::Abs(SeaM) >= 1.0;
	Md.bSeaBad = SeaM > 5.0;
	Md.SeaText = Signed(SeaM, 0) + TEXT(" m");
	// why the sea rose (review findings 7 and 12): said once at +3, +10 and +25 m
	if (!Md.bGenesis)
	{
		static const double SeaMarks[3] = { 3.0, 10.0, 25.0 };
		if (SeaToldIndex < 0) { SeaToldIndex = 0; while (SeaToldIndex < 3 && SeaM >= SeaMarks[SeaToldIndex]) ++SeaToldIndex; }
		else if (SeaToldIndex < 3 && SeaM >= SeaMarks[SeaToldIndex])
		{
			const int32 Metres = FMath::RoundToInt(SeaM);
			PushToast(Anom > 0.2   // (verifier) the inspector's own threshold for "the world has warmed" (was 0.05)
				? FString::Printf(TEXT("The sea has risen %d m: the world is warmer and its ice is melting."), Metres)
				: FString::Printf(TEXT("The sea has risen %d m."), Metres), TEXT("info"), 7.0);
			while (SeaToldIndex < 3 && SeaM >= SeaMarks[SeaToldIndex]) ++SeaToldIndex;
		}
	}

	// the Ark
	Md.bArk = W->vars.ark.has_value() && !W->ending;
	Md.ArkSettlement = -1;
	Md.ArkProgress = 0.f;
	if (Md.bArk)
	{
		for (int Sid : Live)
		{
			const dm::Settlement& S = W->settlements[size_t(Sid)];
			if (S.ark > Md.ArkProgress || Md.ArkSettlement < 0) { Md.ArkProgress = float(S.ark); Md.ArkSettlement = Sid; }
		}
		if (W->vars.ark->settlement >= 0 && W->vars.ark->settlement < int(W->settlements.size()) && !W->settlements[size_t(W->vars.ark->settlement)].dead && Md.ArkProgress <= 0.f)
			Md.ArkSettlement = W->vars.ark->settlement;
		const dm::society::Regard AR = dm::society::regard(*W, W->vars.ark->culture);
		Md.ArkCaption = FString::Printf(TEXT("They will leave you %s"), *MoodPhrase(AR.mood));
	}

	// genesis guide
	Md.bGuide = Md.bGenesis || Md.bPeopleLost;
	Md.GuideExtra.Reset();
	Md.bCanAdvance = false;
	if (Md.bGenesis)
	{
		const dm::sim::genesis::Progress P = dm::sim::genesis::progress(*W);
		Md.GuideProgress = float(FMath::Clamp(P.value, 0.0, 1.0));
		Md.bCanAdvance = P.canAdvance && (W->phase == dm::Phase::Water || W->phase == dm::Phase::Life);
		switch (W->phase)
		{
		case dm::Phase::Molten: Md.GuideText = TEXT("Your world is cooling. Drag to turn it, scroll or pinch to come closer."); break;
		case dm::Phase::Water:
		{
			Md.GuideText = TEXT("Call down comets to fill the seas. Raise or lower the land while the clay is soft.");
			const int32 Pct = int32(FMath::RoundToDouble(P.ocean * 100.0));
			const TCHAR* Word = Pct < 35 ? TEXT("a dry, hard world") : (Pct <= 75 ? TEXT("a temperate world") : TEXT("a world of islands"));
			Md.GuideExtra = FString::Printf(TEXT("Ocean %d %%, %s"), Pct, Word);
			break;
		}
		case dm::Phase::Life: Md.GuideText = TEXT("Touch the land to seed it. Seed each shore you want green."); break;
		default: Md.GuideText = TEXT("Choose where your people wake. Green ground is kind; the glowing mark is the kindest."); break;
		}
	}
	else if (Md.bPeopleLost)
	{
		Md.GuideText = TEXT("Your people are gone. Wake another.");
		Md.GuideProgress = 0.f;
	}

	// powers
	if (Md.Powers.Num() != dm::powers::kPowerCount)
	{
		Md.Powers.SetNum(dm::powers::kPowerCount);
		for (int32 I = 0; I < dm::powers::kPowerCount; ++I)
		{
			const dm::powers::PowerDef& D = dm::powers::kPowers[I];
			FDmHudPower& P = Md.Powers[I];
			P.Id = U(D.id);
			P.Name = U(D.name);
			P.Desc = U(D.desc);
			P.Icon = FName(D.id);
			P.Group = D.group;
			P.bBrush = D.brush;
			P.KeyText = FString::Chr(TCHAR(D.key));
		}
	}
	uint32 Sig = uint32(PhaseRank(W->phase)) * 7919u;
	const int32 Rank = PhaseRank(W->phase);
	bool bAnyNew = false;
	const bool bPhaseChanged = LastPhaseRank != Rank;
	LastPhaseRank = Rank;
	for (int32 I = 0; I < dm::powers::kPowerCount; ++I)
	{
		const dm::powers::PowerDef& D = dm::powers::kPowers[I];
		FDmHudPower& P = Md.Powers[I];
		const bool bUnlocked = dm::powers::unlocked(*W, D.id);
		if (D.group == dm::powers::Group::Genesis)
			P.bVisible = (Md.bGenesis && D.unlock.phase == W->phase) || (!Md.bGenesis && bUnlocked);
		else
		{
			// a group shows once any of its powers' phase is reached (Shape from Water; the rest in history)
			bool bGroup = false;
			for (const dm::powers::PowerDef& O : dm::powers::kPowers)
				if (O.group == D.group && Rank >= PhaseRank(O.unlock.phase)) bGroup = true;
			P.bVisible = bGroup && Rank > 0;
		}
		P.bUnlocked = bUnlocked;
		// awaken is a genesis power of the people phase and a history power afterwards: it sits with Grace then
		P.Group = (D.group == dm::powers::Group::Genesis && !Md.bGenesis) ? dm::powers::Group::Grace : D.group;
		P.Cost = D.brush ? dm::powers::cost(*W, D.id, Md.BrushAng) : dm::powers::cost(*W, D.id);
		if (P.Cost <= 0.0) P.CostText = TEXT("free");
		else P.CostText = D.brush ? FString::Printf(TEXT("%s/s"), *Big(FMath::RoundToDouble(P.Cost))) : Big(FMath::RoundToDouble(P.Cost));
		const double Need = D.brush ? P.Cost * 0.25 : P.Cost;
		P.bPoor = bUnlocked && P.Cost > 0.0 && W->god.awe < Need;
		P.bOverCap = bUnlocked && P.Cost > W->god.aweMax && !D.brush;
		P.bArmed = Md.ArmedId == P.Id;
		if (!bUnlocked) P.Tip = PowerUnlockText(*W, D);
		else if (P.bOverCap) P.Tip = TEXT("Your Awe cannot hold this much yet: they must raise more shrines and temples.");
		else if (P.bPoor) P.Tip = FString::Printf(TEXT("Not enough Awe: %s needed."), *P.CostText);
		else P.Tip.Reset();
		// pulse once when a visible power wakes during play
		if (bUnlockKnown && !bPhaseChanged && !Md.bGenesis && bUnlocked && !UnlockedBefore.Contains(P.Id) && P.bVisible && D.group != dm::powers::Group::Genesis)
		{
			P.PulseStart = Now;
			bAnyNew = true;
			PushGrouped(TEXT("wake"), P.Name, TEXT("good"));
		}
		if (bUnlocked) UnlockedBefore.Add(P.Id); else UnlockedBefore.Remove(P.Id);
		Sig = Sig * 31u + (P.bVisible ? 1u : 0u);
	}
	bUnlockKnown = true;
	Md.Signature = Sig + (bAnyNew ? 1u : 0u);

	// chronicle feed: every entry not seen yet. The first look at a world shows its last few quietly (its
	// history is already written); after that, entries show as they are written. Ids restart in each world.
	const bool bFirst = !bChronicleBaselined;
	bChronicleBaselined = true;
	int32 MaxId = LastChronicleSeen;
	const int32 N = int32(W->chronicle.size());
	for (int32 I = bFirst ? FMath::Max(0, N - 4) : 0; I < N; ++I)
	{
		const dm::ChronicleEntry& E = W->chronicle[size_t(I)];
		if (E.id <= LastChronicleSeen) continue;
		FDmHudFeedItem F;
		F.EntryId = E.id;
		F.Year = U(dm::chronicle::yearStr(*W, E.year));
		F.Text = U(E.text);
		F.Cell = E.cell;
		F.Culture = E.culture;
		F.Weight = E.weight;
		F.Born = bFirst ? Now - 30.0 : Now;
		Feed.Add(F);
		MaxId = FMath::Max(MaxId, E.id);
	}
	if (bFirst) for (const dm::ChronicleEntry& E : W->chronicle) MaxId = FMath::Max(MaxId, E.id);
	LastChronicleSeen = MaxId;
	while (Feed.Num() > 6) Feed.RemoveAt(0);

	// hint 2 (Awe >= 20) and hint 1 (history began) are state triggers
	if (!Md.bGenesis && Md.bBelievers) TriggerHint(0);
	if (!Md.bGenesis && W->god.awe >= 20.0) TriggerHint(1);

	RefreshGuide(W);
}

// ------------------------------------------------------------------ the guidance thread (4 Hz)

void FDmHudContext::RefreshGuide(const dm::World* W)
{
	if (!W) return;
	const bool bHistory = !M.bGenesis;
	if (bChronicleOpen) bChronicleEverOpened = true;
	const int32 Sel = Control.Selected();
	if (bHistory)
	{
		// a town the player clicked: a selection that follows a moment with none (the flow's own first selection
		// of the camp after the awakening does not count)
		if (GuidePrevSelected == -1 && Sel >= 0) GuideDone |= 1u << 0;
		GuidePrevSelected = Sel;
		const auto Deed = W->god.deeds.find("rain");
		if (Deed != W->god.deeds.end() && Deed->second > 0) GuideDone |= 1u << 1;
		if (bRememberEverOpened) GuideDone |= 1u << 2;
		if (bPrayerAnswered) GuideDone |= 1u << 3;
		for (const dm::Settlement& S : W->settlements)
			if (!S.dead && S.b[size_t(dm::Building::SHRINE)]) { GuideDone |= 1u << 4; break; }
		if (bChronicleEverOpened) GuideDone |= 1u << 5;
		for (const dm::Culture& C : W->cultures)
			if (!C.dead && C.edict != dm::Edict::None) { GuideDone |= 1u << 6; break; }
	}
	int32 Cur = 0;
	while (Cur < GuideStepCount && (GuideDone & (1u << Cur))) ++Cur;
	if (Cur != GuideCurrent)
	{
		if (GuideCurrent < GuideStepCount && (GuideDone & (1u << GuideCurrent)))
		{
			GuideJustDone = GuideStep(GuideCurrent).Title;
			GuideJustDoneAt = Now;
		}
		GuideCurrent = Cur;
		GuideStepBorn = Now;
	}
	bGuideVisible = bHistory && M.bBelievers && !bGuideDismissed && GuideCurrent < GuideStepCount && bPlayState && !bEnded;
}

// ------------------------------------------------------------------ now strip (2 Hz)

void FDmHudContext::RefreshNow(const dm::World* W)
{
	M.Now.Reset();
	if (!W || M.bGenesis) return;
	const FDmHudStyle& St = FDmHudStyle::Get();
	// wars
	for (const auto& KV : W->vars.rel)
	{
		if (!KV.second.war) continue;
		const dm::Culture* A = dm::state::culture(*W, KV.first.first);
		const dm::Culture* B = dm::state::culture(*W, KV.first.second);
		if (!A || !B || A->dead || B->dead) continue;
		FDmHudNowItem It;
		It.Text = FString::Printf(TEXT("%s against %s"), *CultureName(*W, A->id), *CultureName(*W, B->id));
		It.Icon = FName(TEXT("war"));
		It.Color = St.Bad;
		for (const dm::Culture* C : { A, B })
			if (C->capital >= 0 && C->capital < int(W->settlements.size()) && !W->settlements[size_t(C->capital)].dead)
				It.Cells.Add(W->settlements[size_t(C->capital)].cell);
		M.Now.Add(It);
	}
	// plague, famine, prayers
	TArray<int32> Plague, Famine;
	TMap<int32, TArray<int32>> Prayers;
	for (const dm::Settlement& S : W->settlements)
	{
		if (S.dead) continue;
		if (S.plague > 0.0) Plague.Add(S.cell);
		if (S.famine > 0) Famine.Add(S.cell);
		if (S.prayer) Prayers.FindOrAdd(int32(S.prayer->kind)).Add(S.cell);
	}
	auto Places = [](int32 N) { return N == 1 ? FString(TEXT("1 place")) : FString::Printf(TEXT("%d places"), N); };
	if (Plague.Num()) { FDmHudNowItem It; It.Text = TEXT("Plague in ") + Places(Plague.Num()); It.Icon = FName(TEXT("plague")); It.Color = St.Bad; It.Cells = Plague; M.Now.Add(It); }
	if (Famine.Num()) { FDmHudNowItem It; It.Text = TEXT("Famine in ") + Places(Famine.Num()); It.Icon = FName(TEXT("famine")); It.Color = St.Bad; It.Cells = Famine; M.Now.Add(It); }
	for (int32 K = 0; K < 4; ++K)
	{
		const TArray<int32>* Cells = Prayers.Find(K);
		if (!Cells) continue;
		FDmHudNowItem It;
		It.Text = FString::Printf(TEXT("%d %s for %s"), Cells->Num(), Cells->Num() == 1 ? TEXT("prays") : TEXT("pray"), *PrayerWord(dm::PrayerKind(K)));
		It.Icon = FName(TEXT("praying"));
		It.Color = St.Awe;
		It.Cells = *Cells;
		M.Now.Add(It);
	}
	// active disasters
	for (const dm::Disaster& D : W->disasters)
	{
		FDmHudNowItem It;
		It.Cells.Add(D.cell);
		It.Color = St.Gold;
		switch (D.type)
		{
		case dm::DisasterType::Storm: It.Text = TEXT("A great storm"); It.Icon = FName(TEXT("storm")); break;
		case dm::DisasterType::Drought: It.Text = TEXT("Drought"); It.Icon = FName(TEXT("drought")); break;
		case dm::DisasterType::Volcano: It.Text = TEXT("An eruption"); It.Icon = FName(TEXT("volcano")); It.Color = St.Bad; break;
		case dm::DisasterType::Tsunami: It.Text = TEXT("A great wave"); It.Icon = FName(TEXT("sea")); It.Color = St.Bad; break;
		default: continue;   // comets and meteors in flight land within seconds
		}
		M.Now.Add(It);
	}
	while (M.Now.Num() > 6) M.Now.Pop();
}

// ------------------------------------------------------------------ events

void FDmHudContext::OnEvent(const dm::Event& E, const dm::World* W)
{
	if (const dm::EvUiToast* T = E.as<dm::EvUiToast>()) { PushToast(U(T->text), U(T->kind)); return; }
	if (const dm::EvUiState* S = E.as<dm::EvUiState>()) { bPlayState = S->state.empty() || S->state == "play"; return; }
	if (E.as<dm::EvAweShort>())
	{
		if (Now - LastAweShort > 1.5) PushToast(TEXT("Not enough Awe"), TEXT("bad"), 2.5);
		LastAweShort = Now;
		return;
	}
	if (const dm::EvChronicleEntry* C = E.as<dm::EvChronicleEntry>())
	{
		if (C->entry.weight >= 3 && C->entry.type != "genesis") ShowBanner(C->entry, W);
		return;
	}
	if (const dm::EvPhase* P = E.as<dm::EvPhase>())
	{
		if (P->phase == dm::Phase::History) TriggerHint(0);
		if (P->phase == dm::Phase::Ended) bEnded = true;
		return;
	}
	if (E.as<dm::EvEnding>()) { bEnded = true; return; }
	if (E.as<dm::EvPeopleLost>()) { M.bPeopleLost = true; return; }
	if (E.as<dm::EvSettlementFounded>()) { M.bPeopleLost = false; return; }
	if (const dm::EvSettlementBuilding* B = E.as<dm::EvSettlementBuilding>())
	{
		if (B->building == dm::Building::SHRINE) TriggerHint(2);
		return;
	}
	if (const dm::EvPrayer* Pr = E.as<dm::EvPrayer>()) { if (Pr->start) TriggerHint(3); return; }
	if (E.as<dm::EvSettlementFamine>()) { TriggerHint(3); return; }
	if (const dm::EvSettlementPlague* Pl = E.as<dm::EvSettlementPlague>()) { if (Pl->start) TriggerHint(3); return; }
	if (const dm::EvWarDeclared* Wd = E.as<dm::EvWarDeclared>())
	{
		TriggerHint(4);
		if (W) PushGrouped(TEXT("war"), FString::Printf(TEXT("%s against %s"), *CultureName(*W, Wd->a), *CultureName(*W, Wd->b)), TEXT("bad"));
		return;
	}
	if (const dm::EvWorldAge* A = E.as<dm::EvWorldAge>())
	{
		TriggerHint(5);
		PushToast(FString::Printf(TEXT("The %s Age begins"), *AgeName(A->age)), TEXT("epoch"), 6.0);
		return;
	}
	if (E.as<dm::EvArkStarted>()) { TriggerHint(6); return; }
	if (const dm::EvPrayerAnswered* Pa = E.as<dm::EvPrayerAnswered>())
	{
		bPrayerAnswered = true;
		if (W && Pa->id >= 0 && Pa->id < int(W->settlements.size()))
			PushGrouped(TEXT("prayer"), U(W->settlements[size_t(Pa->id)].name), TEXT("good"));
		return;
	}
	if (const dm::EvSettlementDestroyed* D = E.as<dm::EvSettlementDestroyed>())
	{
		if (W && D->id >= 0 && D->id < int(W->settlements.size()) && W->settlements[size_t(D->id)].tier >= 1)
			PushGrouped(TEXT("lost"), U(W->settlements[size_t(D->id)].name), TEXT("bad"));
		return;
	}
}

void FDmHudContext::Tick(double InNow)
{
	Now = InNow;
	for (int32 I = Toasts.Num() - 1; I >= 0; --I)
		if (Now - Toasts[I].Born > Toasts[I].Life + 0.6) Toasts.RemoveAt(I);
	// a hint leaves by itself (review finding 12: the shrine hint stayed for 2,300 years and held the queue back)
	if (HintShown >= 0 && Now - HintBorn > HintSeconds) DismissHint();
	if (HintShown < 0 && HintQueue.Num() > 0 && Now - HintQueueSince >= HintGather) DismissHint();   // shows the first waiting
}

// ------------------------------------------------------------------ worlds

void FDmHudContext::ResetWorld()
{
	LastChronicleSeen = -1;
	bChronicleBaselined = false;
	bUnlockKnown = false;
	UnlockedBefore.Reset();
	LastPhaseRank = -1;
	LastAweShort = -100;
	Feed.Reset();
	Toasts.Reset();
	Banner.Born = -100;
	Labels.Reset();
	LabelAlpha.Reset();
	LabelWidth.Reset();
	LastLabelPx.Reset();
	M.bPeopleLost = false;
	M.Now.Reset();
	NowCycle = 0;
	bEnded = false;
	ToastGroups.Reset();
	RegardTrail.Reset();
	LastTrailSample = -100;
	SeaToldIndex = -1;
	bPrayerAnswered = false;
	GuideDone = 0;
	GuideCurrent = 0;
	GuidePrevSelected = -2;
	GuideJustDone.Reset();
	GuideJustDoneAt = -100;
	bRememberOpen = false;
}
