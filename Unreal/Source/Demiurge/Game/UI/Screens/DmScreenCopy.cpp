// DEMIURGE — Game/UI/Screens/DmScreenCopy.cpp : see DmScreenCopy.h. Owner: C8 screens.

#include "Game/UI/Screens/DmScreenCopy.h"

#include "Sim/Core/Rng.h"
#include "Sim/Names.h"

namespace DmCopy
{

FString Tagline() { return TEXT("A world in your hands."); }

FString Credit() { return TEXT("Every image and every sound in DEMIURGE is made in code."); }

FString Ordinal(int32 N)
{
	const int32 Mod100 = N % 100;
	const TCHAR* Suffix = TEXT("th");
	if (Mod100 < 11 || Mod100 > 13)
	{
		switch (N % 10)
		{
		case 1: Suffix = TEXT("st"); break;
		case 2: Suffix = TEXT("nd"); break;
		case 3: Suffix = TEXT("rd"); break;
		default: break;
		}
	}
	return FString::Printf(TEXT("%d%s"), N, Suffix);
}

FString AgeName(int32 Age)
{
	const int32 Count = int32(dm::Age::COUNT);
	const int32 I = FMath::Clamp(Age, 0, Count - 1);
	return FString(UTF8_TO_TCHAR(dm::kAgeNames[I]));
}

FString MoodWord(dm::Mood Mood)
{
	switch (Mood)
	{
	case dm::Mood::Beloved: return TEXT("Beloved");
	case dm::Mood::Feared: return TEXT("Feared");
	case dm::Mood::Forgotten: return TEXT("Forgotten");
	case dm::Mood::Ash: return TEXT("Ash");
	case dm::Mood::Quiet: return TEXT("Quiet");
	default: return TEXT("Parting");
	}
}

FString EndingKey(dm::EndingType Type, dm::Mood Mood)
{
	if (Type == dm::EndingType::Silence)
	{
		return Mood == dm::Mood::Ash ? TEXT("silence-ash") : TEXT("silence-quiet");
	}
	switch (Mood)
	{
	case dm::Mood::Beloved: return TEXT("ark-beloved");
	case dm::Mood::Feared: return TEXT("ark-feared");
	case dm::Mood::Forgotten: return TEXT("ark-forgotten");
	default: return TEXT("ark-parting");
	}
}

FString EndingTitle(dm::EndingType Type, dm::Mood Mood)
{
	const FString Key = EndingKey(Type, Mood);
	if (Key == TEXT("ark-beloved")) return TEXT("Carried to the Stars");
	if (Key == TEXT("ark-feared")) return TEXT("They Fled Your Sky");
	if (Key == TEXT("ark-forgotten")) return TEXT("Gone, and Not Looked Back");
	if (Key == TEXT("silence-ash")) return TEXT("Ash, and a Green Shoot");
	if (Key == TEXT("silence-quiet")) return TEXT("The Quiet Towns");
	return TEXT("A Careful Goodbye");
}

FString EndingKind(dm::EndingType Type)
{
	return Type == dm::EndingType::Silence ? TEXT("SILENCE") : TEXT("THE ARK DEPARTS");
}

FString EndingTemplate(dm::EndingType Type, dm::Mood Mood)
{
	const FString Key = EndingKey(Type, Mood);
	if (Key == TEXT("ark-beloved"))
	{
		return TEXT("In the {year_ord} year {culture_plural} rose from {settlement} on a pillar of light. They carried seed, and songs, and a carved stone bearing the name of {god_name}, whom they called {god_epithet}. Before the clouds closed below them, they looked back at {planet} and wept, not from grief, but because they had been loved, and knew it, and were taking that with them too. Wherever they land, there will be a shrine before there is a house.");
	}
	if (Key == TEXT("ark-feared"))
	{
		return TEXT("In the {year_ord} year {culture_plural} fled {planet}. They built the ship in secret, as far as anything can be secret from a god, and they launched it at night. They did not pray as they rose. On the new world, the mothers will tell the children of {god_epithet}, who lived in the sky of the old one, and the children will sleep with the lamp lit, and grow up, and build their houses low.");
	}
	if (Key == TEXT("ark-forgotten"))
	{
		return TEXT("In the {year_ord} year {culture_plural} left {planet} for the stars. They took their machines, their music and their quarrels. They did not take {god_name}; no one remembered to. In an old temple at {settlement} the wind still turns the pages of a book no one reads, and in it, in a hand no one living could read, is a name that once meant everything.");
	}
	if (Key == TEXT("silence-quiet"))
	{
		return TEXT("There was no last battle and no last prayer. The towns emptied, one and then another, and the roads went back to grass, and the grass did not ask where everyone had gone. {planet} turns in the light as it did before the first fire. The rivers still find the sea. If {god_epithet} is listening, there is nothing now to hear but the wind.");
	}
	if (Key == TEXT("silence-ash"))
	{
		return TEXT("They had learned at last to make suns, and they made them. Afterwards the sky over {planet} was grey for a hundred years, and then blue again, as if nothing had happened. Nothing had: not any more. In the ruins of {settlement} a green shoot came up through the floor of the great hall, and there was no one to call it a sign.");
	}
	return TEXT("In the {year_ord} year {culture_plural} went up from {settlement} to the stars. They were not sure, at the end, what {god_name} had been to them: a parent, a storm, a story. So they did what grown children do: they said a careful goodbye, and promised to write, and did not say when. Somewhere above the clouds one of them looked back and raised a hand, just in case.");
}

FString Fill(const FString& Template, const FFill& V)
{
	FString Out = Template;
	Out = Out.Replace(TEXT("{year_ord}"), *V.YearOrd);
	Out = Out.Replace(TEXT("{culture_plural}"), *V.CulturePlural);
	Out = Out.Replace(TEXT("{settlement}"), *V.Settlement);
	Out = Out.Replace(TEXT("{planet}"), *V.Planet);
	Out = Out.Replace(TEXT("{god_name}"), *V.GodName);
	Out = Out.Replace(TEXT("{god_epithet}"), *V.GodEpithet);
	return Out;
}

FString RollName(uint32 Seed)
{
	dm::Rng R(Seed == 0 ? 1u : Seed);
	const std::string Name = dm::names::planet(R);
	return FString(UTF8_TO_TCHAR(Name.c_str()));
}

FString SlotTitle(const FString& Slot)
{
	if (Slot.Equals(TEXT("auto"), ESearchCase::IgnoreCase)) return TEXT("Autosave");
	const FString Digits = Slot.Mid(4);   // "slot3" -> "3"
	return FString::Printf(TEXT("Slot %s"), *Digits);
}

FString CalamityWord(int32 Which)
{
	switch (Which)
	{
	case 0: return TEXT("Calm");
	case 2: return TEXT("Wild");
	default: return TEXT("Normal");
	}
}

FString QualityWord(int32 Which)
{
	switch (Which)
	{
	case 0: return TEXT("Low");
	case 1: return TEXT("Medium");
	case 2: return TEXT("High");
	default: return TEXT("Auto");
	}
}

TArray<FRow> MouseRows()
{
	return {
		{ TEXT("Drag"), TEXT("Orbit the planet") },
		{ TEXT("Scroll, pinch"), TEXT("Zoom toward the pointer") },
		{ TEXT("Right-drag"), TEXT("Turn the heading") },
		{ TEXT("Click"), TEXT("Select a settlement; click empty ground to clear") },
		{ TEXT("Click, power armed"), TEXT("Use the power on that spot") },
		{ TEXT("Double-click"), TEXT("Fly closer to that point") },
		{ TEXT("Right-click"), TEXT("Disarm the power") },
	};
}

TArray<FRow> KeyboardRows()
{
	return {
		{ TEXT("W A S D, arrows"), TEXT("Orbit") },
		{ TEXT("Q, E"), TEXT("Turn the heading") },
		{ TEXT("+ and -, or R and F"), TEXT("Zoom") },
		{ TEXT("Space"), TEXT("Pause or resume") },
		{ TEXT("1  2  3  4"), TEXT("Speed 1x, 2x, 4x, 8x") },
		{ TEXT("0"), TEXT("Linger at a quarter speed") },
		{ TEXT("Tab"), TEXT("Cycle the overlays") },
		{ TEXT("H"), TEXT("Hide or show the interface") },
		{ TEXT("[  ]"), TEXT("Brush size") },
		{ TEXT("Esc"), TEXT("Disarm, then clear, then this menu") },
		{ TEXT("Power keys"), TEXT("Each power has its own key") },
	};
}

TArray<FRow> TouchRows()
{
	return {
		{ TEXT("One finger"), TEXT("Drag to orbit") },
		{ TEXT("Pinch"), TEXT("Zoom") },
		{ TEXT("Two fingers twist"), TEXT("Turn the heading") },
		{ TEXT("Tap"), TEXT("Select a settlement, or clear") },
		{ TEXT("Double-tap"), TEXT("Fly closer") },
		{ TEXT("Long press"), TEXT("Show what is under your finger") },
		{ TEXT("Hold, brush armed"), TEXT("Apply the brush on the ground") },
	};
}

TArray<FString> FirstSteps()
{
	return {
		TEXT("Your people are awake. Their faith fills the gold orb with Awe. Awe is what miracles cost."),
		TEXT("Arm Rain and hold it over dry ground near them. They will notice."),
		TEXT("A shrine. Your Awe now holds more and fills faster."),
		TEXT("They are asking for you. Answer or do not; they will remember either."),
		TEXT("War. An Omen of Peace ends it; a Prophet can forbid it."),
		TEXT("A new age. New powers wake with them."),
		TEXT("They are building a ship to leave. How they feel about you now is how the story ends."),
	};
}

} // namespace DmCopy
