// DEMIURGE — Game/UI/Hud/SDmHudPanels.cpp : inspector, chronicle feed and book, now strip, labels. Owner: C7 hud.

#include "Game/UI/Hud/SDmHudPanels.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SViewport.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

#include "Game/Core/DmCoords.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Game/UI/Hud/DmHudIcons.h"
#include "Game/UI/Hud/DmHudModel.h"
#include "Game/UI/Hud/DmHudStyle.h"
#include "Game/UI/Hud/SDmHudWidgets.h"

#include "Sim/Chronicle.h"
#include "Sim/Civ.h"
#include "Sim/Climate.h"
#include "Sim/Powers.h"
#include "Sim/Society.h"

namespace
{
	using namespace DmHudUi;

	FString U(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
	EVisibility Vis(bool b) { return b ? EVisibility::Visible : EVisibility::Collapsed; }

	const dm::World* WorldOf(const FDmHudContext& C)
	{
		const UDmSimSubsystem* S = C.Control.Sim();
		return S ? S->Sim() : nullptr;
	}

	FString CulturePlural(const dm::Culture& C)
	{
		if (!C.plural.empty())
		{
			FString P = U(C.plural);
			if (P.StartsWith(TEXT("the "), ESearchCase::IgnoreCase)) P.RightChopInline(4);
			return P;
		}
		return C.name.empty() ? FString(TEXT("people")) : U(C.name);
	}

	FString TraitSentence(const dm::Traits& T)
	{
		TArray<FString> W;
		auto Add = [&W](double V, const TCHAR* Hi, const TCHAR* Lo) { if (V > 0.64) W.Add(Hi); else if (V < 0.36) W.Add(Lo); };
		Add(T.piety, TEXT("devout"), TEXT("worldly"));
		Add(T.aggression, TEXT("warlike"), TEXT("peaceable"));
		Add(T.curiosity, TEXT("curious"), TEXT("bound to custom"));
		Add(T.seafaring, TEXT("seafaring"), TEXT("wary of the sea"));
		if (W.Num() == 0) return TEXT("Unremarkable in their ways.");
		FString S;
		for (int32 I = 0; I < W.Num(); ++I)
		{
			if (I > 0) S += (I == W.Num() - 1) ? TEXT(" and ") : TEXT(", ");
			S += W[I];
		}
		return S.Left(1).ToUpper() + S.Mid(1) + TEXT(".");
	}

	FString RelationWord(const dm::Relation& R, FLinearColor& OutColor)
	{
		const FDmHudStyle& S = FDmHudStyle::Get();
		OutColor = S.Text;
		if (R.war) { OutColor = S.Bad; return TEXT("at war"); }
		FString W;
		if (R.v > 0.4) { W = TEXT("friends"); OutColor = S.Good; }
		else if (R.v > 0.1) W = TEXT("cordial");
		else if (R.v > -0.1) W = TEXT("wary");
		else if (R.v > -0.4) { W = TEXT("cold"); OutColor = S.Dim; }
		else { W = TEXT("hostile"); OutColor = S.Bad; }
		if (R.trade > 0.3) W += TEXT(", trading");
		return W;
	}

	FString EdictLine(dm::Edict E)
	{
		switch (E)
		{
		case dm::Edict::Peace: return TEXT("No new wars; old grudges soften each year.");
		case dm::Edict::Expand: return TEXT("Settlers go out twice as often.");
		case dm::Edict::Build: return TEXT("A wonder rises within thirty years.");
		case dm::Edict::Crusade: return TEXT("They march on their least devout neighbour.");
		default: return FString();
		}
	}

	FString EdictName(dm::Edict E)
	{
		switch (E)
		{
		case dm::Edict::Peace: return TEXT("Peace");
		case dm::Edict::Expand: return TEXT("Expand");
		case dm::Edict::Build: return TEXT("Build");
		case dm::Edict::Crusade: return TEXT("Crusade");
		default: return TEXT("None");
		}
	}

	FName EdictIcon(dm::Edict E)
	{
		switch (E)
		{
		case dm::Edict::Peace: return FName(TEXT("peace"));
		case dm::Edict::Expand: return FName(TEXT("people"));
		case dm::Edict::Build: return FName(TEXT("wonder"));
		default: return FName(TEXT("war"));
		}
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

	bool CultureAtWar(const dm::World& W, int32 Cid, int32* OutEnemy = nullptr)
	{
		for (const auto& KV : W.vars.rel)
		{
			if (!KV.second.war) continue;
			if (KV.first.first == Cid || KV.first.second == Cid)
			{
				const int32 Other = KV.first.first == Cid ? KV.first.second : KV.first.first;
				const dm::Culture* O = dm::state::culture(W, Other);
				if (O && !O->dead) { if (OutEnemy) *OutEnemy = Other; return true; }
			}
		}
		return false;
	}

	/** A plain-language line in the inspector's own style: a small icon, then a sentence (review finding 12). */
	TSharedRef<SWidget> Explain(FName Icon, const FString& Line, const FLinearColor& IconCol, float Wrap)
	{
		const FDmHudStyle& S = FDmHudStyle::Get();
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 2, 8, 0)[SNew(SDmIcon).Icon(Icon).Size(15.f).Color(IconCol)]
			+ SHorizontalBox::Slot().FillWidth(1.f)[Text(Line, EDmFont::Serif, 14.f, S.Text, Wrap)];
	}

	/** The power that answers a prayer, as the player knows it. */
	const TCHAR* PrayerPower(dm::PrayerKind K)
	{
		switch (K)
		{
		case dm::PrayerKind::Rain: return TEXT("Rain (N): hold it over the town");
		case dm::PrayerKind::Heal: return TEXT("Heal (T) on the town");
		case dm::PrayerKind::Peace: return TEXT("an Omen of Peace (O) on the town");
		default: return TEXT("Bloom (B): hold it over their scorched land");
		}
	}

	TSharedRef<SWidget> Chip(FName Icon, const FString& Word, const FLinearColor& Col)
	{
		const FDmHudStyle& S = FDmHudStyle::Get();
		return SNew(SBorder).BorderImage(&S.Card).Padding(FMargin(7, 3, 9, 3))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)[SNew(SDmIcon).Icon(Icon).Size(14.f).Color(Col)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Text(Word, EDmFont::Sans, 13.f, Col)]
		];
	}

	FVector2f MeasureText(const FString& S, const FSlateFontInfo& F)
	{
		if (!FSlateApplication::IsInitialized()) return FVector2f(S.Len() * F.Size * 0.6f, F.Size * 1.3f);
		return FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(S, F));
	}

	float LabelPx(int32 Tier, bool bCapital) { return bCapital ? 16.f : (Tier >= 3 ? 15.f : (Tier == 2 ? 14.f : 13.f)); }

	/** Key of a label's measured width: one per settlement and size (tier and capital set the font). */
	int32 WidthKey(int32 Sid, const dm::Settlement& S) { return Sid * 16 + FMath::Clamp(S.tier, 0, 7) * 2 + (S.capital ? 1 : 0); }
}

// ================================================================== inspector

void SDmInspector::Construct(const FArguments& InArgs)
{
	Ctx = InArgs._Context;
	Width = InArgs._Width;
	ChildSlot[SAssignNew(Body, SBox).WidthOverride(Width)];
	SetTag(FName(TEXT("pan-inspector")));
}

void SDmInspector::Tick(const FGeometry& G, const double Now, const float Dt)
{
	SCompoundWidget::Tick(G, Now, Dt);
	const dm::World* W = WorldOf(*Ctx);
	const int32 Sel = Ctx->M.Selected;
	uint32 NewSig = uint32(Sel + 7) * 2654435761u;
	if (W && Sel >= 0 && Sel < int32(W->settlements.size()))
	{
		const dm::Settlement& S = W->settlements[size_t(Sel)];
		NewSig = HashCombine(NewSig, GetTypeHash(int32(S.pop / FMath::Max(1.0, S.pop * 0.02))));
		NewSig = HashCombine(NewSig, GetTypeHash(S.tier * 31 + S.famine + (S.plague > 0 ? 7 : 0) + (S.blessed > 0 ? 11 : 0) + (S.inspire > 0 ? 13 : 0) + (S.prayer ? 17 + int32(S.prayer->kind) : 0) + (S.dead ? 1000 : 0)));
		for (int32 B = 0; B < dm::Building::COUNT; ++B) NewSig = HashCombine(NewSig, uint32(S.b[size_t(B)]));
		NewSig = HashCombine(NewSig, GetTypeHash(int32(S.devotion * 40.0) * 100 + int32((S.love + 1.0) * 40.0)));
		if (const dm::Culture* Cu = dm::state::culture(*W, S.culture))
		{
			NewSig = HashCombine(NewSig, GetTypeHash(int32(Cu->edict) + (Cu->prophet ? 50 : 0) + Cu->age * 7));
			NewSig = HashCombine(NewSig, GetTypeHash(FString(UTF8_TO_TCHAR(Cu->epithet.c_str()))));
		}
	}
	if (Sel != Shown || (NewSig != Sig && Now - LastBuild > 0.25))
	{
		Shown = Sel;
		Sig = NewSig;
		LastBuild = Now;
		Build();
	}
}

void SDmInspector::Build()
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	const dm::World* W = WorldOf(*Ctx);
	const int32 Sel = Ctx->M.Selected;
	if (!W || Sel < 0 || Sel >= int32(W->settlements.size())) { Body->SetContent(SNullWidget::NullWidget); return; }
	const dm::Settlement& St = W->settlements[size_t(Sel)];
	const dm::Culture* Cu = dm::state::culture(*W, St.culture);
	TSharedPtr<FDmHudContext> C = Ctx;
	const float Inner = Width - 32.f;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);

	// name, culture, tier
	const FLinearColor CC = Cu ? DmCultureColor(Cu->color) : S.Dim;
	const FString CName = Cu ? U(Cu->name) : FString(TEXT("no people"));
	V->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
		[
			SNew(SDmSigil).Size(22.f).Color(CC).Letter(Initial(CName))
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Text(U(St.name), EDmFont::Serif, 25.f, S.Gold)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 2, 0, 0)
			[
				SNew(SDmIcon).Icon(FName(TEXT("crown"))).Size(16.f).Color(S.Awe).Visibility(Vis(St.capital))
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
		[
			SNew(SBox).WidthOverride(30).HeightOverride(30)
			[
				SNew(SButton).ButtonStyle(&S.Button).ContentPadding(FMargin(0)).HAlign(HAlign_Center).VAlign(VAlign_Center)
				.OnClicked_Lambda([C]() { C->Control.Select(-1); C->NarrowSheet = 0; return FReply::Handled(); })
				[
					SNew(SDmIcon).Icon(FName(TEXT("close"))).Size(15.f).Color(S.Dim)
				]
			]
		]
	];
	FString Sub = FString::Printf(TEXT("%s%s of the %s"), St.capital ? TEXT("Capital ") : TEXT(""), *FDmHudContext::TierName(St.tier).ToLower(), Cu ? *CulturePlural(*Cu) : TEXT("people"));
	Sub = Sub.Left(1).ToUpper() + Sub.Mid(1);
	if (Cu) Sub += FString::Printf(TEXT(", %s Age"), *FDmHudContext::AgeName(Cu->age));
	V->AddSlot().AutoHeight().Padding(32, 1, 0, 0)[Text(Sub, EDmFont::SerifItalic, 15.f, S.Dim, Inner - 32.f)];

	// population in words
	const double Pop = St.pop;
	const double Rate = Pop > 1.0 ? St.growth / Pop : 0.0;
	const TCHAR* Trend = St.famine > 0 ? TEXT("starving") : (Rate > 0.004 ? TEXT("growing") : (Rate < -0.004 ? TEXT("shrinking") : TEXT("steady")));
	const FString PopLine = FString::Printf(TEXT("%s people, %s. The land could feed %s."), *FDmHudContext::Big(FMath::RoundToDouble(Pop)), Trend, *FDmHudContext::Big(FMath::RoundToDouble(FMath::Max(0.0, St.K))));
	V->AddSlot().AutoHeight().Padding(0, 12, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 1, 8, 0)[SNew(SDmIcon).Icon(FName(TEXT("people"))).Size(17.f).Color(S.Gold)]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Text(PopLine, EDmFont::Serif, 16.f, S.Text, Inner - 26.f)]
	];

	// statuses (icon plus word)
	TSharedRef<SWrapBox> Status = SNew(SWrapBox).PreferredSize(Inner).InnerSlotPadding(FVector2D(6, 6));
	int32 NStatus = 0;
	auto AddStatus = [&](FName I, const FString& Wd, const FLinearColor& Col) { Status->AddSlot()[Chip(I, Wd, Col)]; ++NStatus; };
	if (St.plague > 0.0) AddStatus(FName(TEXT("plague")), TEXT("Plague"), S.Bad);
	if (St.famine > 0) AddStatus(FName(TEXT("famine")), TEXT("Famine"), S.Bad);
	int32 Enemy = -1;
	if (Cu && CultureAtWar(*W, Cu->id, &Enemy)) AddStatus(FName(TEXT("war")), FString::Printf(TEXT("At war with the %s"), *CulturePlural(W->cultures[size_t(Enemy)])), S.Bad);
	if (St.blessed > 0.0) AddStatus(FName(TEXT("blessed")), TEXT("Blessed"), S.Good);
	if (St.inspire > 0.0) AddStatus(FName(TEXT("inspired")), TEXT("Inspired"), S.Good);
	if (St.prayer) AddStatus(FName(TEXT("praying")), FString::Printf(TEXT("Praying for %s"), *PrayerWord(St.prayer->kind)), S.Awe);
	if (W->valid(St.cell) && W->elev[size_t(St.cell)] - W->seaLevel < 0.15) AddStatus(FName(TEXT("rising-sea")), TEXT("The sea is at the door"), S.Water);
	if (NStatus > 0) V->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[Status];

	// what the statuses mean and what the god can do about them (review finding 12: why an answered prayer did
	// not end a famine, why the sea is at the door, what answers a prayer)
	TSharedRef<SVerticalBox> Why = SNew(SVerticalBox);
	int32 NWhy = 0;
	auto AddWhy = [&](FName I, const FString& Line, const FLinearColor& Col) { Why->AddSlot().AutoHeight().Padding(0, NWhy ? 6 : 0, 0, 0)[Explain(I, Line, Col, Inner - 26.f)]; ++NWhy; };
	// (verifier) the v3 sim: an answered rain or bloom prayer blesses the town (Society prayerBlessed years), and a
	// blessed town does not starve (Economy); the old line said an answered prayer did not end the hunger
	if (St.famine > 0 && St.blessed > 0)
		AddWhy(FName(TEXT("famine")), FString::Printf(TEXT("Your answer blessed them: for %d more years their fields give more and the hunger is held off. "
			"When the blessing fades they need a bigger harvest (Rain, Bloom, a granary) or fewer mouths."), FMath::Max(1, FMath::RoundToInt(St.blessed))), S.Bad);
	else if (St.famine > 0)
		AddWhy(FName(TEXT("famine")), FString::Printf(TEXT("There are more of them than the land can feed (%s). Answer their prayer with Rain or Bloom and the town is blessed "
			"for %d years: the fields give more and the hunger stops. After that only a bigger harvest or fewer mouths ends it."),
			*FDmHudContext::Big(FMath::RoundToDouble(FMath::Max(0.0, St.K))), FMath::RoundToInt(W->tune<dm::SocietyTune>().prayerBlessed)), S.Bad);
	if (St.prayer)
		AddWhy(FName(TEXT("praying")), FString::Printf(TEXT("They pray for %s. Answer with %s%s; a prayer answered earns twice the faith, and they remember it."),
			*PrayerWord(St.prayer->kind), PrayerPower(St.prayer->kind),
			St.prayer->kind == dm::PrayerKind::Rain && St.famine > 0 ? TEXT(" (a hungry town's prayer is for bread: Bloom (B) answers it too)") : TEXT("")), S.Awe);
	if (St.plague > 0.0 && !St.prayer)
		AddWhy(FName(TEXT("plague")), TEXT("A sickness spreads along their roads. Heal (T) lifts it."), S.Bad);
	if (W->valid(St.cell) && W->elev[size_t(St.cell)] - W->seaLevel < 0.15)
	{
		const double Anom = dm::climate::anomaly(*W);
		AddWhy(FName(TEXT("rising-sea")), Anom > 0.2
			? FString(TEXT("The town sits barely above the water. The world has warmed and its ice is melting, so the sea is still climbing."))
			: FString(TEXT("The town sits barely above the water; a storm surge or a higher sea would reach it.")), S.Water);
	}
	if (NWhy > 0) V->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[Why];

	// buildings
	TSharedRef<SWrapBox> Bld = SNew(SWrapBox).PreferredSize(Inner).InnerSlotPadding(FVector2D(10, 6));
	int32 NB = 0;
	for (int32 B = 0; B < dm::Building::COUNT; ++B)
	{
		if (!St.b[size_t(B)]) continue;
		++NB;
		FString Name = U(dm::kBuildingNames[B]);
		if (Name == TEXT("launch")) Name = TEXT("launch site");
		Name = Name.Left(1).ToUpper() + Name.Mid(1);
		Bld->AddSlot()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)[SNew(SDmIcon).Icon(FName(dm::kBuildingNames[B])).Size(17.f).Color(S.Gold)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Text(Name, EDmFont::Sans, 12.5f, S.Dim)]
		];
	}
	if (NB == 0) Bld->AddSlot()[Text(FString(TEXT("No buildings yet.")), EDmFont::SerifItalic, 14.f, S.Dim)];
	V->AddSlot().AutoHeight().Padding(0, 14, 0, 4)[Caption(TEXT("Buildings"), S.Dim)];
	V->AddSlot().AutoHeight()[Bld];

	// devotion and love
	const float Dev = float(FMath::Clamp(St.devotion, 0.0, 1.0));
	const TCHAR* DevWord = Dev >= 0.7f ? TEXT("devout") : Dev >= 0.45f ? TEXT("faithful") : Dev >= 0.2f ? TEXT("wavering") : TEXT("faithless");
	V->AddSlot().AutoHeight().Padding(0, 14, 0, 4)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Caption(TEXT("Devotion"), S.Dim)]
		+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Text(FString::Printf(TEXT("%s, %d %%"), DevWord, FMath::RoundToInt(Dev * 100.f)), EDmFont::Sans, 13.f, S.Text)]
	];
	V->AddSlot().AutoHeight()[SNew(SDmBar).Width(Inner).Height(5.f).Value(Dev).Color(S.Awe)];
	V->AddSlot().AutoHeight().Padding(0, 14, 0, 2)[Caption(TEXT("How they feel about you"), S.Dim)];
	V->AddSlot().AutoHeight()[SNew(SDmLoveScale).Width(Inner).Love(float(St.love))];
	V->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
	[
		Text(FString(TEXT("Devotion is how much they believe; every miracle they see adds to it. Love is how your deeds felt to them. With what they remember of your deeds, they decide how you are remembered.")),
			EDmFont::SerifItalic, 13.5f, S.Dim, Inner)
	];

	// culture summary
	if (Cu)
	{
		TSharedRef<SVerticalBox> CV = SNew(SVerticalBox);
		CV->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[SNew(SDmSigil).Size(16.f).Color(CC).Letter(Initial(CName))]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Text(FString::Printf(TEXT("The %s"), *CulturePlural(*Cu)), EDmFont::Serif, 18.f, S.Gold)]
		];
		CV->AddSlot().AutoHeight().Padding(0, 3, 0, 0)[Text(TraitSentence(Cu->traits), EDmFont::SerifItalic, 14.5f, S.Text, Inner - 24.f)];
		const FString God = Cu->godName.empty() ? FString(TEXT("the god")) : U(Cu->godName);
		const FString Epi = Cu->epithet.empty() ? FString() : FString::Printf(TEXT(", %s"), *U(Cu->epithet));
		CV->AddSlot().AutoHeight().Padding(0, 8, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 2, 7, 0)[SNew(SDmIcon).Icon(FName(TEXT("awe"))).Size(15.f).Color(S.Awe)]
			+ SHorizontalBox::Slot().FillWidth(1.f)[Text(FString::Printf(TEXT("They call you %s%s."), *God, *Epi), EDmFont::Serif, 14.5f, S.Text, Inner - 50.f)]
		];
		// relations (Society.peek only)
		TSharedRef<SVerticalBox> Rel = SNew(SVerticalBox);
		int32 NR = 0;
		for (const dm::Culture& O : W->cultures)
		{
			if (O.dead || O.id == Cu->id) continue;
			const dm::Relation* R = dm::society::peek(*W, Cu->id, O.id);
			if (!R || !R->contact) continue;
			FLinearColor RC;
			const FString Word = RelationWord(*R, RC);
			const FString OName = U(O.name);
			Rel->AddSlot().AutoHeight().Padding(0, 2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 7, 0)[SNew(SDmSigil).Size(13.f).Color(DmCultureColor(O.color)).Letter(Initial(OName))]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Text(FString::Printf(TEXT("The %s"), *CulturePlural(O)), EDmFont::Serif, 14.f, S.Text)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Text(Word, EDmFont::Sans, 13.f, RC)]
			];
			++NR;
		}
		if (NR > 0)
		{
			CV->AddSlot().AutoHeight().Padding(0, 10, 0, 3)[Caption(TEXT("Neighbours"), S.Dim)];
			CV->AddSlot().AutoHeight()[Rel];
		}
		// prophet
		CV->AddSlot().AutoHeight().Padding(0, 10, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 7, 0)[SNew(SDmIcon).Icon(FName(TEXT("prophet"))).Size(16.f).Color(Cu->prophet ? S.GoldHi : S.Dim)]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				Text(Cu->prophet
					? FString::Printf(TEXT("The prophet %s lives in %s."), *U(Cu->prophet->name),
						(Cu->prophet->settlement >= 0 && Cu->prophet->settlement < int(W->settlements.size())) ? *U(W->settlements[size_t(Cu->prophet->settlement)].name) : TEXT("the wilds"))
					: FString(TEXT("No living prophet. The edicts below need one to speak through.")),
					EDmFont::Serif, 14.f, Cu->prophet ? S.Text : S.Dim, Inner - 50.f)
			]
		];
		// edicts
		const bool bProphet = Cu->prophet.has_value();
		TSharedRef<SVerticalBox> Ed = SNew(SVerticalBox);
		const int32 CuId = Cu->id;
		for (const dm::Edict E : { dm::Edict::Peace, dm::Edict::Expand, dm::Edict::Build, dm::Edict::Crusade })
		{
			const bool bOn = Cu->edict == E;
			Ed->AddSlot().AutoHeight().Padding(0, 2)
			[
				SNew(SButton)
				.Tag(FName(*FString::Printf(TEXT("edict-%s"), *EdictName(E).ToLower())))
				.ButtonStyle(bOn ? &S.ButtonOn : &S.Button)
				.IsEnabled(bProphet)
				.ContentPadding(FMargin(8, 5))
				.OnClicked_Lambda([C, CuId, E]()
				{
					if (!C->Control.SetEdict(CuId, E)) C->PushToast(TEXT("They will not take that edict now"), TEXT("bad"));
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 9, 0)[SNew(SDmIcon).Icon(EdictIcon(E)).Size(18.f).Color(bProphet ? (bOn ? S.GoldHi : S.Gold) : FDmHudStyle::WithAlpha(S.Dim, 0.6f))]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
					[
						SNew(SBox).MinDesiredWidth(62)[Text(EdictName(E), EDmFont::SansSemi, 13.5f, bProphet ? (bOn ? S.GoldHi : S.Text) : FDmHudStyle::WithAlpha(S.Dim, 0.6f))]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Text(EdictLine(E), EDmFont::Sans, 12.5f, bProphet ? S.Dim : FDmHudStyle::WithAlpha(S.Dim, 0.5f), Inner - 175.f)]
				]
			];
		}
		CV->AddSlot().AutoHeight().Padding(0, 10, 0, 3)[Caption(bProphet ? TEXT("Edicts") : TEXT("Edicts (need a prophet)"), S.Dim)];
		if (!bProphet)
		{
			// the edicts are shown dimmed; say how to wake them (review finding 12: they looked available from year 1)
			const dm::powers::PowerDef* PD = dm::powers::byId("prophet");
			const bool bCan = dm::powers::unlocked(*W, "prophet");
			const FString How = bCan
				? FString(TEXT("Raise a Prophet (P) in one of their towns to speak through them; then choose an edict here."))
				: FString::Printf(TEXT("Raise a Prophet to speak through them. %s"), PD ? *FDmHudContext::PowerUnlockText(*W, *PD).Replace(TEXT("Wakes"), TEXT("The Prophet wakes")) : TEXT(""));
			CV->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Explain(FName(TEXT("lock")), How, S.Dim, Inner - 50.f)];
		}
		CV->AddSlot().AutoHeight()[Ed];
		V->AddSlot().AutoHeight().Padding(0, 16, 0, 0)[SNew(SBorder).BorderImage(&S.Card).Padding(FMargin(12, 10))[CV]];
	}
	Body->SetContent(Panel(V, FMargin(16.f, 14.f), nullptr, false));
}

// ================================================================== chronicle feed

void SDmChronicleFeed::Construct(const FArguments& InArgs)
{
	Ctx = InArgs._Context;
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	const float Wd = InArgs._Width;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	TSharedRef<SWidget> Open =
		SNew(SButton).ButtonStyle(&S.Button).ContentPadding(FMargin(8, 4))
		.Tag(FName(TEXT("pan-chronicle-open")))
		.OnClicked_Lambda([C]() { C->bChronicleOpen = !C->bChronicleOpen; return FReply::Handled(); })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)[SNew(SDmIcon).Icon(FName(TEXT("scroll"))).Size(17.f).Color(S.Gold)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Text(FString(TEXT("Open")), EDmFont::Sans, 13.f, S.Gold)]
		];
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Caption(TEXT("Chronicle"), S.Dim)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Open]
	];
	for (int32 I = 0; I < InArgs._Slots; ++I)
	{
		auto Item = [C, I]() -> const FDmHudFeedItem* { const int32 K = C->Feed.Num() - 1 - I; return C->Feed.IsValidIndex(K) ? &C->Feed[K] : nullptr; };
		V->AddSlot().AutoHeight().Padding(0, 3)
		[
			SNew(SButton).ButtonStyle(&S.FlatButton).ContentPadding(FMargin(6, 4))
			.Visibility_Lambda([Item]() { return Vis(Item() != nullptr); })
			.OnClicked_Lambda([C, Item]() { if (const FDmHudFeedItem* F = Item()) C->FlyTo(F->Cell); return FReply::Handled(); })
			[
				SNew(SBorder).BorderImage(&S.NoBrush).Padding(0)
				.ColorAndOpacity_Lambda([C, Item, I]()
				{
					const FDmHudFeedItem* F = Item();
					if (!F) return FLinearColor(1, 1, 1, 0);
					const float In = float(FMath::Clamp((C->Now - F->Born) / 0.9, 0.0, 1.0));
					const float Age = FMath::Max(0.38f, 1.f - 0.15f * I);
					return FLinearColor(1, 1, 1, In * Age);
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 4, 8, 0)
					[
						SNew(SDmIcon).Size(12.f)
						.Icon_Lambda([Item]() { const FDmHudFeedItem* F = Item(); return (F && F->Weight >= 3) ? FName(TEXT("star")) : FName(TEXT("dot")); })
						.Color_Lambda([Item, &S]() { const FDmHudFeedItem* F = Item(); return (F && F->Weight >= 3) ? S.GoldHi : S.Gold; })
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							Text(Lam([Item]() { const FDmHudFeedItem* F = Item(); return F ? F->Year : FString(); }), EDmFont::SansSemi, 12.f, FSlateColor(S.Dim))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							Text(Lam([Item]() { const FDmHudFeedItem* F = Item(); return F ? F->Text : FString(); }), EDmFont::Serif, 14.5f,
								TAttribute<FSlateColor>::CreateLambda([Item, &S]() { const FDmHudFeedItem* F = Item(); return FSlateColor((F && F->Weight >= 3) ? S.Gold : S.Text); }), Wd - 60.f)
						]
					]
				]
			]
		];
	}
	ChildSlot
	[
		SNew(SBox).WidthOverride(Wd)
		.Visibility_Lambda([C]() { return Vis(C->Feed.Num() > 0); })
		[
			Panel(V, FMargin(12.f, 9.f))
		]
	];
}

// ================================================================== chronicle book

void SDmChronicleBook::Construct(const FArguments& InArgs)
{
	Ctx = InArgs._Context;
	Width = InArgs._Width;
	Height = InArgs._Height;
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	TSharedRef<SWidget> Body =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[SNew(SDmIcon).Icon(FName(TEXT("scroll"))).Size(26.f).Color(S.Gold)]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				Text(Lam([C]() { return FString::Printf(TEXT("The Chronicle of %s"), *C->M.WorldName); }), EDmFont::Serif, 27.f, FSlateColor(S.Gold))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(40).HeightOverride(40).Tag(FName(TEXT("close")))
				[
					SNew(SButton).ButtonStyle(&S.RoundButton).HAlign(HAlign_Center).VAlign(VAlign_Center)
					.OnClicked_Lambda([C]() { C->bChronicleOpen = false; return FReply::Handled(); })
					[
						SNew(SDmIcon).Icon(FName(TEXT("close"))).Size(18.f).Color(S.Gold)
					]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 6)[SNew(SDmFlourish).Width(Width - 120.f).Color(S.Gold)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[SAssignNew(Filters, SBox)]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SScrollBox).Style(&S.ScrollBox).ScrollBarStyle(&S.ScrollBar)
			+ SScrollBox::Slot().Padding(0, 0, 10, 0)[SAssignNew(List, SVerticalBox)]
		];
	ChildSlot
	[
		SNew(SBox).WidthOverride(Width).HeightOverride(Height)
		[
			Panel(Body, FMargin(26.f, 20.f), &S.PanelSolid)
		]
	];
	SetTag(FName(TEXT("pan-chronicle")));
}

void SDmChronicleBook::Tick(const FGeometry& G, const double Now, const float Dt)
{
	SCompoundWidget::Tick(G, Now, Dt);
	if (!Ctx->bChronicleOpen) return;
	const dm::World* W = WorldOf(*Ctx);
	const uint32 NewSig = HashCombine(HashCombine(GetTypeHash(W ? int32(W->chronicle.size()) : -1), GetTypeHash(Ctx->BookCulture + 100 * Ctx->BookWeight)),
		GetTypeHash(W && !W->chronicle.empty() ? W->chronicle.back().id : -1));
	if (NewSig != Sig)
	{
		Sig = NewSig;
		BuildFilters();
		BuildList();
	}
}

void SDmChronicleBook::BuildFilters()
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	const dm::World* W = WorldOf(*Ctx);
	TSharedPtr<FDmHudContext> C = Ctx;
	TSharedRef<SWrapBox> Row = SNew(SWrapBox).PreferredSize(Width - 60.f).InnerSlotPadding(FVector2D(6, 6));
	auto ChipBtn = [&](const FString& Label, bool bOn, TFunction<void()> F, const FLinearColor* Dot, const FString& Letter)
	{
		TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox);
		if (Dot) H->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)[SNew(SDmSigil).Size(14.f).Color(*Dot).Letter(Letter)];
		H->AddSlot().AutoWidth().VAlign(VAlign_Center)[Text(Label, EDmFont::Sans, 13.f, bOn ? S.GoldHi : S.Dim)];
		Row->AddSlot()
		[
			SNew(SButton).ButtonStyle(bOn ? &S.ButtonOn : &S.Button).ContentPadding(FMargin(10, 4))
			.OnClicked_Lambda([F]() { F(); return FReply::Handled(); })[H]
		];
	};
	ChipBtn(TEXT("All peoples"), C->BookCulture < 0, [C]() { C->BookCulture = -1; }, nullptr, FString());
	if (W)
	{
		TSet<int32> Seen;
		for (const dm::ChronicleEntry& E : W->chronicle) if (E.culture >= 0) Seen.Add(E.culture);
		for (const dm::Culture& Cu : W->cultures)
		{
			if (!Seen.Contains(Cu.id)) continue;
			const int32 Id = Cu.id;
			const FLinearColor Col = DmCultureColor(Cu.color);
			const FString Name = CulturePlural(Cu);   // the plural, as the text says it ("the Sheen")
			ChipBtn(Name, C->BookCulture == Id, [C, Id]() { C->BookCulture = Id; }, &Col, Initial(U(Cu.name)));
		}
	}
	TSharedRef<SHorizontalBox> Weights = SNew(SHorizontalBox);
	struct FWt { int32 W; const TCHAR* L; };
	for (const FWt Wt : { FWt{ 1, TEXT("Everything") }, FWt{ 2, TEXT("Notable") }, FWt{ 3, TEXT("Great deeds") } })
	{
		const int32 Wv = Wt.W;
		const bool bOn = C->BookWeight == Wv;
		Weights->AddSlot().AutoWidth().Padding(0, 0, 6, 0)
		[
			SNew(SButton).ButtonStyle(bOn ? &S.ButtonOn : &S.Button).ContentPadding(FMargin(10, 4))
			.OnClicked_Lambda([C, Wv]() { C->BookWeight = Wv; return FReply::Handled(); })
			[
				Text(Wt.L, EDmFont::Sans, 13.f, bOn ? S.GoldHi : S.Dim)
			]
		];
	}
	Filters->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Row]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[Weights]);
}

void SDmChronicleBook::BuildList()
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	const dm::World* W = WorldOf(*Ctx);
	TSharedPtr<FDmHudContext> C = Ctx;
	List->ClearChildren();
	if (!W) return;
	int32 Shown = 0;
	for (int32 I = int32(W->chronicle.size()) - 1; I >= 0 && Shown < 400; --I)
	{
		const dm::ChronicleEntry& E = W->chronicle[size_t(I)];
		if (E.weight < Ctx->BookWeight) continue;
		if (Ctx->BookCulture >= 0 && E.culture != Ctx->BookCulture) continue;
		++Shown;
		const int32 Cell = E.cell;
		const dm::Culture* Cu = dm::state::culture(*W, E.culture);
		const FLinearColor TC = E.weight >= 3 ? S.Gold : (E.weight == 2 ? S.Text : FDmHudStyle::WithAlpha(S.Text, 0.78f));
		TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 3, 0, 0)
			[
				SNew(SBox).WidthOverride(96)[Text(U(dm::chronicle::yearStr(*W, E.year)), EDmFont::SansSemi, 12.5f, S.Dim)]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 3, 8, 0)
			[
				SNew(SBox).WidthOverride(16)
				[
					Cu ? StaticCastSharedRef<SWidget>(SNew(SDmSigil).Size(14.f).Color(DmCultureColor(Cu->color)).Letter(Initial(U(Cu->name))))
					   : StaticCastSharedRef<SWidget>(SNew(SDmIcon).Icon(FName(E.weight >= 3 ? TEXT("star") : TEXT("dot"))).Size(14.f).Color(S.Gold))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				Text(U(E.text), E.weight >= 3 ? EDmFont::Serif : EDmFont::Serif, E.weight >= 3 ? 17.f : 15.5f, TC, Width - 230.f)
			];
		List->AddSlot().AutoHeight().Padding(0, 3)
		[
			SNew(SButton).ButtonStyle(&S.FlatButton).ContentPadding(FMargin(6, 5))
			.OnClicked_Lambda([C, Cell]() { if (Cell >= 0) { C->bChronicleOpen = false; C->FlyTo(Cell); } return FReply::Handled(); })
			[
				H
			]
		];
	}
	if (Shown == 0)
		List->AddSlot().AutoHeight().Padding(0, 20)[Text(FString(TEXT("Nothing is written here yet.")), EDmFont::SerifItalic, 16.f, S.Dim)];
}

// ================================================================== now strip

void SDmNowStrip::Construct(const FArguments& InArgs)
{
	Ctx = InArgs._Context;
	const FDmHudStyle& S = FDmHudStyle::Get();
	TSharedPtr<FDmHudContext> C = Ctx;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(4, 0, 0, 4)[Caption(TEXT("Now"), S.Dim)];
	for (int32 I = 0; I < 6; ++I)
	{
		auto Item = [C, I]() -> const FDmHudNowItem* { return C->M.Now.IsValidIndex(I) ? &C->M.Now[I] : nullptr; };
		V->AddSlot().AutoHeight().Padding(0, 1)
		[
			SNew(SButton).ButtonStyle(&S.FlatButton).ContentPadding(FMargin(5, 4))
			.Visibility_Lambda([Item]() { return Vis(Item() != nullptr); })
			.OnClicked_Lambda([C, I]() { C->ClickNow(I); return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 9, 0)
				[
					SNew(SDmIcon).Size(17.f)
					.Icon_Lambda([Item]() { const FDmHudNowItem* It = Item(); return It ? It->Icon : FName(TEXT("dot")); })
					.Color_Lambda([Item]() { const FDmHudNowItem* It = Item(); return It ? It->Color : FLinearColor::White; })
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					Text(Lam([Item]() { const FDmHudNowItem* It = Item(); return It ? It->Text : FString(); }), EDmFont::Sans, 13.5f, FSlateColor(S.Text), InArgs._Width - 60.f)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
				[
					SNew(SDmIcon).Icon(FName(TEXT("arrow"))).Size(13.f).Color(FDmHudStyle::WithAlpha(S.Dim, 0.6f))
				]
			]
		];
	}
	ChildSlot
	[
		SNew(SBox).WidthOverride(InArgs._Width)
		.Visibility_Lambda([C]() { return Vis(C->M.Now.Num() > 0); })
		.Tag(FName(TEXT("pan-now")))
		[
			Panel(V, FMargin(10.f, 8.f))
		]
	];
}

// ================================================================== labels

void DmHudLabels::Update(FDmHudContext& Ctx, const UDmSimSubsystem& Sim, const APlayerController* PC, float Dt)
{
	const double T0 = FPlatformTime::Seconds();
	// the HUD panels painted last frame, taken now: the list starts again every frame, labels on or off
	TArray<FSlateRect> PanelsLast;
	Swap(PanelsLast, DmHudUi::PanelRects());
	const dm::World* W = Sim.Sim();
	if (!W || !PC || !Ctx.bLabels) { Ctx.Labels.Reset(); return; }
	FVector CamLoc; FRotator CamRot;
	PC->GetPlayerViewPoint(CamLoc, CamRot);
	const FTransform P2W = Sim.GetPlanetToWorld();
	const FVector Centre = P2W.GetLocation();
	const double AltKm = (FVector::Dist(CamLoc, Centre) - DmCoords::RadiusUU) / DmCoords::UUPerKm;
	Ctx.CameraAltitudeKm = AltKm;
	int32 Vx = 0, Vy = 0;
	PC->GetViewportSize(Vx, Vy);

	struct FCand { int32 Sid; FVector2D Px; float Score; };
	TArray<FCand, TInlineAllocator<128>> Cands;
	for (const dm::Settlement& S : W->settlements)
	{
		if (S.dead || !W->valid(S.cell)) continue;
		const bool bBig = S.capital || S.tier >= 3;
		const bool bShow = bBig ? AltKm < 4000.0 : (S.tier == 2 ? AltKm < 450.0 : (S.tier == 1 ? AltKm < 170.0 : AltKm < 60.0));
		if (!bShow) continue;
		const FVector P = Sim.CellWorldPos(S.cell);
		const FVector Up = (P - Centre).GetSafeNormal();
		const FVector ToCam = (CamLoc - P).GetSafeNormal();
		if (FVector::DotProduct(Up, ToCam) < 0.04) continue;   // beyond the horizon
		FVector2D Scr;
		if (!PC->ProjectWorldLocationToScreen(P, Scr, false)) continue;
		if (Scr.X < -50 || Scr.Y < -20 || Scr.X > Vx + 50 || Scr.Y > Vy + 20) continue;
		const float Score = (S.capital ? 6.f : 0.f) + float(S.tier) * 2.f + float(FMath::LogX(10.0, FMath::Max(1.0, S.pop)));
		Cands.Add({ S.id, Scr, Score });
	}
	Cands.Sort([](const FCand& A, const FCand& B) { return A.Score != B.Score ? A.Score > B.Score : A.Sid < B.Sid; });

	// greedy placement in Slate units; at most 40
	const float K = FMath::Max(0.01f, Ctx.DpiToPx);
	const FDmHudStyle& St = FDmHudStyle::Get();
	TArray<FBox2D, TInlineAllocator<48>> Placed;
	// HUD panels painted last frame: labels never sit under them. Slate units -> viewport px (the viewport
	// widget's own ratio) -> root units (K), so the mapping holds at any engine DPI scale.
	{
		FVector2D Origin = FVector2D::ZeroVector;
		double PxPerSlate = 1.0;
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetGameViewportWidget().IsValid())
		{
			const FGeometry VG = GEngine->GameViewport->GetGameViewportWidget()->GetCachedGeometry();
			Origin = VG.GetAbsolutePosition();
			if (VG.GetLocalSize().X > 1.f) PxPerSlate = Ctx.ViewportPx.X / double(VG.GetLocalSize().X);
		}
		const double Scale = PxPerSlate / double(K);
		for (const FSlateRect& R : PanelsLast)
			Placed.Add(FBox2D((FVector2D(R.Left, R.Top) - Origin) * Scale - FVector2D(6, 6), (FVector2D(R.Right, R.Bottom) - Origin) * Scale + FVector2D(6, 6)));
	}
	TSet<int32> Accepted;
	TMap<int32, FVector2D> PxOf;
	for (const FCand& Cd : Cands)
	{
		PxOf.Add(Cd.Sid, Cd.Px);
		Ctx.LastLabelPx.Add(Cd.Sid, Cd.Px);
		if (Accepted.Num() >= 40) continue;
		const dm::Settlement& S = W->settlements[size_t(Cd.Sid)];
		float* Wd = Ctx.LabelWidth.Find(WidthKey(Cd.Sid, S));
		if (!Wd)
		{
			const FVector2f M = MeasureText(U(S.name), St.Font(S.capital ? EDmFont::SerifBold : EDmFont::Serif, LabelPx(S.tier, S.capital)));
			Wd = &Ctx.LabelWidth.Add(WidthKey(Cd.Sid, S), M.X);
		}
		const FVector2D L = Cd.Px / K;
		const float Full = *Wd + 22.f + 32.f;
		const FBox2D Box(FVector2D(L.X - Full * 0.5 - 4, L.Y - 34), FVector2D(L.X + Full * 0.5 + 4, L.Y - 10));
		bool bOverlap = false;
		for (const FBox2D& B : Placed) if (B.Intersect(Box)) { bOverlap = true; break; }
		if (bOverlap) continue;
		Placed.Add(Box);
		Accepted.Add(Cd.Sid);
	}
	// fade toward the targets; keep fading labels that are still on screen
	TArray<int32> Drop;
	for (auto& KV : Ctx.LabelAlpha)
	{
		const float Target = Accepted.Contains(KV.Key) ? 1.f : 0.f;
		KV.Value = FMath::FInterpConstantTo(KV.Value, Target, Dt, 4.f);
		if (KV.Value <= 0.f && Target <= 0.f) Drop.Add(KV.Key);
	}
	for (int32 Id : Drop) { Ctx.LabelAlpha.Remove(Id); Ctx.LastLabelPx.Remove(Id); }
	for (int32 Id : Accepted) if (!Ctx.LabelAlpha.Contains(Id)) Ctx.LabelAlpha.Add(Id, FMath::Min(1.f, Dt * 4.f));

	Ctx.Labels.Reset();
	for (const auto& KV : Ctx.LabelAlpha)
	{
		// a label that left the candidates (zoomed out past its size, or off screen) fades where it was
		const FVector2D* Px = PxOf.Find(KV.Key);
		if (!Px) Px = Ctx.LastLabelPx.Find(KV.Key);
		if (!Px) continue;
		const dm::Settlement& S = W->settlements[size_t(KV.Key)];
		FDmHudLabel L;
		L.Sid = S.id;
		L.Px = *Px;
		L.Alpha = KV.Value;
		L.Name = U(S.name);
		L.Tier = S.tier;
		L.bCapital = S.capital;
		const dm::Culture* Cu = dm::state::culture(*W, S.culture);
		L.Color = Cu ? DmCultureColor(Cu->color) : St.Dim;
		L.Sigil = Cu ? Initial(U(Cu->name)) : FString();
		if (const float* Wd = Ctx.LabelWidth.Find(WidthKey(S.id, S))) L.TextW = *Wd;
		int32 NS = 0;
		auto Add = [&](const TCHAR* I) { if (NS < 2) L.Status[NS++] = FName(I); };
		if (S.plague > 0.0) Add(TEXT("plague"));
		if (S.famine > 0) Add(TEXT("famine"));
		if (Cu && CultureAtWar(*W, Cu->id)) Add(TEXT("war"));
		if (S.blessed > 0.0) Add(TEXT("blessed"));
		if (S.prayer) Add(TEXT("praying"));
		if (W->elev[size_t(S.cell)] - W->seaLevel < 0.15) Add(TEXT("rising-sea"));
		Ctx.Labels.Add(L);
	}
	const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
	Ctx.AvgLabelMs = Ctx.AvgLabelMs * 0.95 + Ms * 0.05;
}

int32 SDmHudLabels::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const
{
	const FDmHudStyle& S = FDmHudStyle::Get();
	const float K = FMath::Max(0.01f, Ctx->DpiToPx);
	for (const FDmHudLabel& L : Ctx->Labels)
	{
		if (L.Alpha <= 0.01f) continue;
		const float A = L.Alpha;
		const FVector2f P = FVector2f(L.Px / K);
		const float Px = LabelPx(L.Tier, L.bCapital);
		const FSlateFontInfo Font = S.Font(L.bCapital ? EDmFont::SerifBold : EDmFont::Serif, Px, 2, FDmHudStyle::WithAlpha(S.Ink, 0.85f * A));
		const float NStat = (L.Status[0].IsNone() ? 0.f : 1.f) + (L.Status[1].IsNone() ? 0.f : 1.f);
		const float Sig = 14.f;
		const float Total = Sig + 6.f + L.TextW + NStat * 17.f;
		const float X0 = FMath::Clamp(P.X - Total * 0.5f, 4.f, FMath::Max(4.f, float(G.GetLocalSize().X) - Total - 4.f));
		const float Y = P.Y - 30.f;
		// the anchor: a short hairline from the label to the place, and a dot
		FLinearColor Line = S.Gold; Line.A = 0.45f * A;
		TArray<FVector2f> Tick = { FVector2f(P.X, Y + Px * 1.35f + 1.f), FVector2f(P.X, P.Y - 3.f) };
		FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Tick, ESlateDrawEffect::None, Line, true, 1.f);
		FLinearColor Dot = S.GoldHi; Dot.A = 0.9f * A;
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(4.f, 4.f), FSlateLayoutTransform(P - FVector2f(2.f, 2.f))), &S.Dot, ESlateDrawEffect::None, Dot);
		SDmSigil::PaintSigil(Out, Layer + 1, G, FVector2f(X0, Y + (Px * 1.3f - Sig) * 0.5f + 1.f), Sig, L.Color, L.Sigil, A);
		FLinearColor TC = L.bCapital ? S.GoldHi : (L.Tier >= 3 ? S.Gold : S.Text);
		TC.A = A;
		FSlateDrawElement::MakeText(Out, Layer + 3, G.ToPaintGeometry(FVector2f(L.TextW + 4.f, Px * 1.5f), FSlateLayoutTransform(FVector2f(X0 + Sig + 6.f, Y))), L.Name, Font, ESlateDrawEffect::None, TC);
		float Sx = X0 + Sig + 6.f + L.TextW + 4.f;
		for (int32 I = 0; I < 2; ++I)
		{
			if (L.Status[I].IsNone()) continue;
			const bool bBad = L.Status[I] == FName(TEXT("plague")) || L.Status[I] == FName(TEXT("famine")) || L.Status[I] == FName(TEXT("war"));
			FLinearColor IC = bBad ? S.Bad : (L.Status[I] == FName(TEXT("rising-sea")) ? S.Water : S.Awe);
			IC.A = A;
			FLinearColor Halo = S.Ink; Halo.A = 0.7f * A;
			FSlateDrawElement::MakeBox(Out, Layer + 2, G.ToPaintGeometry(FVector2f(17.f, 17.f), FSlateLayoutTransform(FVector2f(Sx - 1.f, Y + 1.f))), &S.Dot, ESlateDrawEffect::None, Halo);
			DmIcons::Paint(Out, Layer + 3, G, L.Status[I], FVector2f(Sx + 1.f, Y + 3.f), 13.f, IC);
			Sx += 17.f;
		}
	}
	return Layer + 3;
}
