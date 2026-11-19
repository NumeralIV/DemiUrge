// DEMIURGE — Game/UI/Screens/DmScreenCopy.h : the words of the screens (SPEC-UI 5, 6; SPEC-UI 2 and 4
// for the controls and first steps; content/chronicle-voice.md section 4 for the endings).
// Owner: C8 screens. Pure text and small pure helpers; no Slate.
#pragma once

#include "CoreMinimal.h"
#include "Sim/Schema.h"

namespace DmCopy
{
	/** One row of a list: a key or name on the left, what it does on the right. */
	struct FRow
	{
		FString Key;
		FString Text;
	};

	FString Tagline();                       // "A world in your hands."
	FString Credit();                        // the line under the title menu
	FString Ordinal(int32 N);                // 1st, 2nd, 3rd, 4th, 11th ...
	FString AgeName(int32 Age);              // Stone .. Space (clamped)
	FString MoodWord(dm::Mood Mood);         // Beloved, Feared, Forgotten, Parting, Quiet, Ash

	/** The ending's key of SPEC-UI 5 (ark-beloved ... silence-ash) for a type and mood. */
	FString EndingKey(dm::EndingType Type, dm::Mood Mood);
	FString EndingTitle(dm::EndingType Type, dm::Mood Mood);
	FString EndingKind(dm::EndingType Type);  // THE ARK DEPARTS / SILENCE
	/** The one-paragraph ending text with its placeholders still in it. */
	FString EndingTemplate(dm::EndingType Type, dm::Mood Mood);

	/** The values the ending text is filled with. */
	struct FFill
	{
		FString YearOrd;
		FString CulturePlural;
		FString Settlement;
		FString Planet;
		FString GodName;
		FString GodEpithet;
	};
	FString Fill(const FString& Template, const FFill& V);

	/** A default world name (dm::names::planet). */
	FString RollName(uint32 Seed);

	FString SlotTitle(const FString& Slot);  // "Slot 1", "Autosave"
	FString CalamityWord(int32 Which);       // 0 calm, 1 normal, 2 wild
	FString QualityWord(int32 Which);        // -1 auto, 0 low, 1 medium, 2 high

	TArray<FRow> MouseRows();
	TArray<FRow> KeyboardRows();
	TArray<FRow> TouchRows();
	TArray<FString> FirstSteps();            // the seven one-time hints (SPEC-UI 4)
}
