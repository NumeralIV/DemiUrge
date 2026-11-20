// DEMIURGE — Game/UI/Hud/DmHudIcons.cpp : icon paths, the path parser and the painter. Owner: C7 hud.

#include "Game/UI/Hud/DmHudIcons.h"

#include "Game/UI/Hud/DmHudStyle.h"
#include "Rendering/DrawElements.h"

namespace
{
	struct FEntry { const TCHAR* Name; const TCHAR* Path; };

	// 24 x 24 box, y down; strokes are drawn 1.6 px wide at 24 px. "F x y r" = filled dot.
	const FEntry GIcons[] = {
		// ---------------------------------------------------------------- powers
		{ TEXT("comet"),     TEXT("M19.5 7 A3.2 3.2 0 1 1 13.1 7 A3.2 3.2 0 1 1 19.5 7 Z M13.6 9.4 L4 19 M14.2 5.2 L7.5 11.9 M17.6 10.4 L10.9 17.1 F16.3 7 0.9") },
		{ TEXT("seed"),      TEXT("M12 21 V11.5 M12 14 C12 10.2 9.4 8 5 8 C5 11.8 7.6 14 12 14 Z M12 11.5 C12 7.3 14.6 4.4 19.2 4.4 C19.2 8.6 16.6 11.5 12 11.5 Z M7 21 H17") },
		{ TEXT("awaken"),    TEXT("M14.6 7.6 A2.6 2.6 0 1 1 9.4 7.6 A2.6 2.6 0 1 1 14.6 7.6 Z M6.8 21 C6.8 15.4 9 12.6 12 12.6 C15 12.6 17.2 15.4 17.2 21 M3.5 8.5 L6 9.4 M20.5 8.5 L18 9.4 M5.6 3.6 L7.6 5.4 M18.4 3.6 L16.4 5.4 M12 1.8 V3.4") },
		{ TEXT("raise"),     TEXT("M2 20.5 L8.6 10 L12.4 15.2 L15 11.6 L22 20.5 Z M18 9 V2.6 M15.6 5 L18 2.6 L20.4 5") },
		{ TEXT("lower"),     TEXT("M2 10.5 H6.5 L10 17 H14 L17.5 10.5 H22 M2 20.5 H22 M12 2.4 V9.4 M9.6 7 L12 9.4 L14.4 7") },
		{ TEXT("rain"),      TEXT("M7 14.4 H17.4 A3.4 3.4 0 0 0 17.7 7.6 A5.4 5.4 0 0 0 7.3 8.2 A3.1 3.1 0 0 0 7 14.4 Z M8.2 17.2 L7.2 20.4 M12.2 17.2 L11.2 20.4 M16.2 17.2 L15.2 20.4") },
		{ TEXT("bloom"),     TEXT("M14 6.2 A2 2 0 1 1 10 6.2 A2 2 0 1 1 14 6.2 Z M18 10 A2 2 0 1 1 14 10 A2 2 0 1 1 18 10 Z M14 13.8 A2 2 0 1 1 10 13.8 A2 2 0 1 1 14 13.8 Z M10 10 A2 2 0 1 1 6 10 A2 2 0 1 1 10 10 Z M12 15.8 V21.4 M12 18.6 C13.4 16.8 15.8 16.4 17.8 17.2 C16.8 19.2 14.4 19.6 12 18.6 Z F12 10 1.1") },
		{ TEXT("lightning"), TEXT("M13.6 2 L5.4 13.2 H11.2 L10.2 22 L18.6 10.4 H12.8 L13.6 2 Z") },
		{ TEXT("quake"),     TEXT("M2 13 H5.2 L7.2 8.4 L10 17.2 L12.8 5.6 L15.8 18.4 L18 13 H22 M3 21 H8 M16 21 H21") },
		{ TEXT("plague"),    TEXT("M16.2 12 A4.2 4.2 0 1 1 7.8 12 A4.2 4.2 0 1 1 16.2 12 Z M12 3.6 V7.8 M12 16.2 V20.4 M3.6 12 H7.8 M16.2 12 H20.4 M6.1 6.1 L9 9 M15 15 L17.9 17.9 M6.1 17.9 L9 15 M15 9 L17.9 6.1 F12 3.2 1.2 F12 20.8 1.2 F3.2 12 1.2 F20.8 12 1.2 F5.8 5.8 1 F18.2 18.2 1 F5.8 18.2 1 F18.2 5.8 1 F10.6 11 0.9 F13.4 13 0.9") },
		{ TEXT("meteor"),    TEXT("M12.2 15.8 A4.2 4.2 0 1 1 3.8 15.8 A4.2 4.2 0 1 1 12.2 15.8 Z M10.9 12.9 L20.8 3 M8 11.6 L14.4 5.2 M12.4 16 L18.8 9.6") },
		{ TEXT("volcano"),   TEXT("M2 21 L8.4 11 H15.6 L22 21 Z M8.4 11 L10.4 13.2 L12 11.6 L13.6 13.6 L15.6 11 M10 8.2 C9 6.4 11 5.2 10 3 M14 8.2 C15 6.4 13 5.2 14 3 M12 7 V5") },
		{ TEXT("heal"),      TEXT("M9.2 3.5 H14.8 V9.2 H20.5 V14.8 H14.8 V20.5 H9.2 V14.8 H3.5 V9.2 H9.2 Z") },
		{ TEXT("inspire"),   TEXT("M9.4 18 H14.6 M10.2 21 H13.8 M12 3 A6 6 0 0 0 8.4 13.8 C9.2 14.6 9.5 15.4 9.5 16 H14.5 C14.5 15.4 14.8 14.6 15.6 13.8 A6 6 0 0 0 12 3 Z M12 9.4 V13.2 M10.6 10.4 L12 9.2 L13.4 10.4") },
		{ TEXT("prophet"),   TEXT("M12 5.4 A2.2 2.2 0 1 1 7.6 5.4 A2.2 2.2 0 1 1 12 5.4 Z M5.6 21 L7.8 9.8 H11.8 L14 21 Z M17.4 6.6 V21 M17.4 6.6 C17.4 4.2 19.8 3.4 20.4 5.4 C20.8 6.8 19.4 7.6 18.6 6.8") },
		{ TEXT("peace"),     TEXT("M3.5 20.5 C9.5 16.5 14.5 10.5 20 4 M8.6 16.2 C6.6 14.8 6.4 12.6 7.4 11 C8.8 12.6 9.6 14.2 8.6 16.2 Z M12.4 12.6 C11.4 10.2 12.2 8.6 13.6 7.6 C14.6 9.6 14.2 11.2 12.4 12.6 Z M11 14 C13 15.4 15.2 15.4 16.6 14 C14.8 12.4 12.8 12.6 11 14 Z M15.2 10.2 C17.2 11.2 19.2 11 20.4 9.6 C18.6 8.4 16.8 8.6 15.2 10.2 Z") },
		// (wave3-final) Consecrate (v3 power) had no icon: the bar drew a bare dot. A temple with rays over its roof.
		{ TEXT("consecrate"), TEXT("M3.4 11 L12 5.8 L20.6 11 Z M5.6 11 V18.8 M10 11 V18.8 M14 11 V18.8 M18.4 11 V18.8 M3.4 18.8 H20.6 M2.6 21.4 H21.4 M12 1 V3.6 M8.2 2.2 L9.6 4 M15.8 2.2 L14.4 4") },
		// ---------------------------------------------------------------- ages
		{ TEXT("age0"),      TEXT("M12 2.5 L17.6 11.6 L12 21.5 L6.4 11.6 Z M12 2.5 L10.4 11.8 L12 21.5 M8.8 7.2 L11 8.8 M15.2 7.6 L13 9.4 M8 14.6 L10.6 14 M16 14.2 L13.2 15") },
		{ TEXT("age1"),      TEXT("M4.5 21 L14.2 8.4 M12 6.4 C13.8 3.2 18.8 2.6 21.2 5.4 C19.6 6.4 18.6 8.4 18.8 10.8 C16.6 10.8 14.6 9.6 14.2 8.4 C13.4 7.8 12.6 7.2 12 6.4 Z") },
		{ TEXT("age2"),      TEXT("M12 2.4 L13.6 5 V15.6 H10.4 V5 Z M7.6 15.6 H16.4 M12 15.6 V20 M10.2 21.2 H13.8 M12 5.6 V14") },
		{ TEXT("age3"),      TEXT("M3 16.2 H21 L18.4 20.2 H5.6 Z M12 2.8 V16.2 M12 4 C16.4 6 17.6 10 17.4 14.2 H12 M12 6 C9.2 8 8.2 11 8.4 14.2 H12") },
		{ TEXT("age4"),      TEXT("M3 21 V11.4 L8 14.4 V11.4 L13 14.4 V11.4 L17.4 14.2 V4.4 H20.6 V21 Z M6.4 17.6 H8 M10.4 17.6 H12 M14.4 17.6 H16") },
		{ TEXT("age5"),      TEXT("M21 12 A9 3.6 0 1 1 3 12 A9 3.6 0 1 1 21 12 Z M16.5 19.79 A9 3.6 60 1 1 7.5 4.21 A9 3.6 60 1 1 16.5 19.79 Z M16.5 4.21 A9 3.6 -60 1 1 7.5 19.79 A9 3.6 -60 1 1 16.5 4.21 Z F12 12 1.7") },
		{ TEXT("age6"),      TEXT("M12 2.2 C15 5.2 16 9 16 13.8 L14.2 17 H9.8 L8 13.8 C8 9 9 5.2 12 2.2 Z M8 13.8 L5 18 L9 17 M16 13.8 L19 18 L15 17 M13.6 9.2 A1.6 1.6 0 1 1 10.4 9.2 A1.6 1.6 0 1 1 13.6 9.2 Z M11 19.2 L12 22 L13 19.2") },
		// ---------------------------------------------------------------- buildings
		{ TEXT("shrine"),    TEXT("M3.5 10.4 L12 4.2 L20.5 10.4 M6 9 V20.6 M18 9 V20.6 M3.5 20.6 H20.5 M10 20.6 V15.6 A2 2 0 0 1 14 15.6 V20.6") },
		{ TEXT("temple"),    TEXT("M2.8 8.6 L12 3 L21.2 8.6 Z M5 8.6 V18.4 M9.6 8.6 V18.4 M14.4 8.6 V18.4 M19 8.6 V18.4 M3.2 18.4 H20.8 M2.4 21 H21.6") },
		{ TEXT("granary"),   TEXT("M6 21 V10.4 A6 6 0 0 1 18 10.4 V21 Z M6 14.4 H18 M4 21 H20 M10.6 21 V17.4 H13.4 V21") },
		{ TEXT("walls"),     TEXT("M3 21 V8.6 H6 V10.8 H9 V8.6 H12 V10.8 H15 V8.6 H18 V10.8 H21 V8.6 V21 Z M9.8 21 V16.4 A2.2 2.2 0 0 1 14.2 16.4 V21") },
		{ TEXT("port"),      TEXT("M13.6 4.4 A1.6 1.6 0 1 1 10.4 4.4 A1.6 1.6 0 1 1 13.6 4.4 Z M12 6 V21 M8 9.4 H16 M4.6 13.6 C4.6 18 8 21 12 21 C16 21 19.4 18 19.4 13.6 M3 15 L4.6 13.4 L6.4 15 M17.6 15 L19.4 13.4 L21 15") },
		{ TEXT("market"),    TEXT("M4.4 10.4 V20.4 H19.6 V10.4 M3 10.4 L5.4 4 H18.6 L21 10.4 Z M3 10.4 C3 12.4 6.6 12.4 6.6 10.4 C6.6 12.4 10.2 12.4 10.2 10.4 C10.2 12.4 13.8 12.4 13.8 10.4 C13.8 12.4 17.4 12.4 17.4 10.4 C17.4 12.4 21 12.4 21 10.4 M9.4 20.4 V15 H14.6 V20.4") },
		{ TEXT("mine"),      TEXT("M4 20.6 L15.4 9.2 M8.4 3.6 C13 2.4 18.8 5 20.6 10.2 C17.4 7.6 13.8 6 8.4 3.6 Z M13.4 5.8 L18.2 10.6 M2.4 21 H11") },
		{ TEXT("factory"),   TEXT("M3 21 V12 H9 V8.4 L14 11.2 V8.4 L19 11.2 V21 Z M5.2 12 V4.6 H7.6 V12 M6.4 3 C6.8 2 8 2 8.4 1.2 M6.4 16 H8 M10.8 16 H12.4 M15.2 16 H16.8") },
		{ TEXT("academy"),   TEXT("M3 5.8 C6 4.8 9 4.8 12 6.8 C15 4.8 18 4.8 21 5.8 V18.8 C18 17.8 15 17.8 12 19.8 C9 17.8 6 17.8 3 18.8 Z M12 6.8 V19.8 M5.6 9 C7.2 8.6 8.6 8.8 9.8 9.6 M5.6 12.4 C7.2 12 8.6 12.2 9.8 13 M14.2 9.6 C15.4 8.8 16.8 8.6 18.4 9 M14.2 13 C15.4 12.2 16.8 12 18.4 12.4") },
		{ TEXT("plant"),     TEXT("M5 21 C7.4 16 7.6 10.6 5.8 6.6 H13.4 C11.6 10.6 11.8 16 14.2 21 Z M3 21 H21 M16.4 21 V12.6 H19.6 V21 M8.4 4.6 C8.4 3.2 10.4 3.2 10.4 1.8 M11.4 4.6 C11.4 3.6 12.8 3.4 12.8 2.4") },
		{ TEXT("launch"),    TEXT("M5 21 V3 M5 6.4 H8.6 M5 11 H8.6 M5 15.6 H8.6 M14.6 3.4 C16.8 5.6 17.4 8.6 17.4 12 L16.2 15 H13 L11.8 12 C11.8 8.6 12.4 5.6 14.6 3.4 Z M12.6 17.4 L14.6 20.6 L16.6 17.4 M2.8 21 H21.2") },
		{ TEXT("wonder"),    TEXT("M2 21 L12 6.2 L22 21 Z M7.2 21 L12 13.2 L16.8 21 M12 1.6 L12.8 3.4 L14.6 3.6 L13.2 4.8 L13.6 6.6 L12 5.6 L10.4 6.6 L10.8 4.8 L9.4 3.6 L11.2 3.4 Z") },
		// ---------------------------------------------------------------- statuses
		{ TEXT("famine"),    TEXT("M3 11.6 H21 C21 16.6 17 20.4 12 20.4 C7 20.4 3 16.6 3 11.6 Z M8.8 8 C8.2 6.6 9.4 5.6 8.8 4 M12.4 8 C11.8 6.6 13 5.6 12.4 4 M16 8 C15.4 6.6 16.6 5.6 16 4") },
		{ TEXT("war"),       TEXT("M4 4 L15.2 15.2 M20 4 L8.8 15.2 M4 4 H7.4 M4 4 V7.4 M20 4 H16.6 M20 4 V7.4 M13.6 17.8 L17.8 13.6 M6.2 13.6 L10.4 17.8 M16.4 16.4 L20 20 M7.6 16.4 L4 20") },
		{ TEXT("blessed"),   TEXT("M12 2.6 C12.4 8.6 15.4 11.6 21.4 12 C15.4 12.4 12.4 15.4 12 21.4 C11.6 15.4 8.6 12.4 2.6 12 C8.6 11.6 11.6 8.6 12 2.6 Z M18.6 3.6 V7 M16.9 5.3 H20.3") },
		{ TEXT("inspired"),  TEXT("M9.4 18 H14.6 M10.2 21 H13.8 M12 3 A6 6 0 0 0 8.4 13.8 C9.2 14.6 9.5 15.4 9.5 16 H14.5 C14.5 15.4 14.8 14.6 15.6 13.8 A6 6 0 0 0 12 3 Z M12 9.4 V13.2 M10.6 10.4 L12 9.2 L13.4 10.4") },
		{ TEXT("praying"),   TEXT("M10 21 V14.4 H14 V21 Z M12 12.4 C14.2 10.6 14.4 8.2 12 5 C9.6 8.2 9.8 10.6 12 12.4 Z M7 21 H17 M5.4 7.6 L7.2 8.6 M18.6 7.6 L16.8 8.6 M12 2 V3") },
		{ TEXT("rising-sea"),TEXT("M2 16.2 C4.8 14.4 7.2 14.4 10 16.2 C12.8 18 15.2 18 18 16.2 C19.4 15.3 20.7 15.1 22 15.5 M2 20.4 C4.8 18.6 7.2 18.6 10 20.4 C12.8 22.2 15.2 22.2 18 20.4 C19.4 19.5 20.7 19.3 22 19.7 M12 2.8 V11.4 M9 5.8 L12 2.8 L15 5.8") },
		// ---------------------------------------------------------------- misc
		{ TEXT("awe"),       TEXT("M18.6 12 A6.6 6.6 0 1 1 5.4 12 A6.6 6.6 0 1 1 18.6 12 Z M12 7.6 C12.4 10.6 13.4 11.6 16.4 12 C13.4 12.4 12.4 13.4 12 16.4 C11.6 13.4 10.6 12.4 7.6 12 C10.6 11.6 11.6 10.6 12 7.6 Z M12 1.6 V3.2 M12 20.8 V22.4 M1.6 12 H3.2 M20.8 12 H22.4") },
		{ TEXT("people"),    TEXT("M11.4 7.4 A2.6 2.6 0 1 1 6.2 7.4 A2.6 2.6 0 1 1 11.4 7.4 Z M3.4 20 C3.4 15 5.6 12.4 8.8 12.4 C12 12.4 14.2 15 14.2 20 M18.8 8.6 A2.1 2.1 0 1 1 14.6 8.6 A2.1 2.1 0 1 1 18.8 8.6 Z M15.4 12.8 C18.6 12.6 20.6 14.8 20.6 19") },
		{ TEXT("culture"),   TEXT("M5.4 21.4 V2.6 M5.4 3.8 H18.6 L15.6 8.2 L18.6 12.6 H5.4 F5.4 2.6 1") },
		{ TEXT("trade"),     TEXT("M4 8 H18.6 M15.4 4.8 L18.6 8 L15.4 11.2 M20 16 H5.4 M8.6 12.8 L5.4 16 L8.6 19.2") },
		{ TEXT("ship"),      TEXT("M2.6 15 H21.4 L18.4 19.6 H5.6 Z M7.4 15 V10.6 H16.6 V15 M9.6 10.6 V6.4 H13 V10.6 M10.4 4.6 C10.8 3.6 12 3.6 12.4 2.6") },
		{ TEXT("thermometer"),TEXT("M12 2.6 A2.2 2.2 0 0 1 14.2 4.8 V13.8 A4.2 4.2 0 1 1 9.8 13.8 V4.8 A2.2 2.2 0 0 1 12 2.6 Z M12 8.4 V16.6 F12 17.2 1.6") },
		{ TEXT("sea"),       TEXT("M2 8.6 C4.8 6.8 7.2 6.8 10 8.6 C12.8 10.4 15.2 10.4 18 8.6 C19.4 7.7 20.7 7.5 22 7.9 M2 13.4 C4.8 11.6 7.2 11.6 10 13.4 C12.8 15.2 15.2 15.2 18 13.4 C19.4 12.5 20.7 12.3 22 12.7 M2 18.2 C4.8 16.4 7.2 16.4 10 18.2 C12.8 20 15.2 20 18 18.2 C19.4 17.3 20.7 17.1 22 17.5") },
		{ TEXT("pause"),     TEXT("M8.6 5 V19 M15.4 5 V19") },
		{ TEXT("play"),      TEXT("M8 4.6 L19 12 L8 19.4 Z") },
		{ TEXT("linger"),    TEXT("M7 3 H17 M7 21 H17 M8.2 3 C8.2 8.6 15.8 8.6 15.8 12 C15.8 15.4 8.2 15.4 8.2 21 M15.8 3 C15.8 8.6 8.2 8.6 8.2 12 C8.2 15.4 15.8 15.4 15.8 21 M10 18.6 H14") },
		{ TEXT("fast"),      TEXT("M4 5 L11 12 L4 19 Z M12.4 5 L19.4 12 L12.4 19 Z") },
		{ TEXT("faster"),    TEXT("M2.4 6 L8 12 L2.4 18 Z M8.8 6 L14.4 12 L8.8 18 Z M15.2 6 L20.8 12 L15.2 18 Z") },
		{ TEXT("fastest"),   TEXT("M2 6 L7.4 12 L2 18 Z M8 6 L13.4 12 L8 18 Z M14 6 L19.4 12 L14 18 Z M21.4 5.6 V18.4") },
		{ TEXT("eye"),       TEXT("M2 12 C5 7.2 8.4 5.2 12 5.2 C15.6 5.2 19 7.2 22 12 C19 16.8 15.6 18.8 12 18.8 C8.4 18.8 5 16.8 2 12 Z M15.2 12 A3.2 3.2 0 1 1 8.8 12 A3.2 3.2 0 1 1 15.2 12 Z F12 12 1.2") },
		{ TEXT("eye-off"),   TEXT("M2 12 C5 7.2 8.4 5.2 12 5.2 C15.6 5.2 19 7.2 22 12 C19 16.8 15.6 18.8 12 18.8 C8.4 18.8 5 16.8 2 12 Z M15.2 12 A3.2 3.2 0 1 1 8.8 12 A3.2 3.2 0 1 1 15.2 12 Z M3.6 3.6 L20.4 20.4") },
		{ TEXT("layers"),    TEXT("M12 3 L21.6 8 L12 13 L2.4 8 Z M2.4 12 L12 17 L21.6 12 M2.4 16 L12 21 L21.6 16") },
		{ TEXT("scroll"),    TEXT("M6 3.4 H17.6 A2.2 2.2 0 0 1 17.6 7.8 H16 V18.6 A2.4 2.4 0 0 1 13.6 21 H5.6 A2.4 2.4 0 0 1 5.6 16.2 H13.6 V18.6 M6 3.4 A2.2 2.2 0 0 0 6 7.8 V16.2 M8.6 8 H13.2 M8.6 11 H13.2 M8.6 14 H11.6") },
		{ TEXT("close"),     TEXT("M6 6 L18 18 M18 6 L6 18") },
		{ TEXT("back"),      TEXT("M15 4.6 L7.6 12 L15 19.4") },
		{ TEXT("save"),      TEXT("M4 3.4 H16.4 L20.6 7.6 V20.6 H3.4 V3.4 Z M7.4 3.4 V8.4 H15.4 V3.4 M7.2 20.6 V13.6 H16.8 V20.6") },
		{ TEXT("home"),      TEXT("M3 11.2 L12 3.4 L21 11.2 M5.4 9.2 V20.6 H18.6 V9.2 M10 20.6 V14.6 H14 V20.6") },
		{ TEXT("camera"),    TEXT("M3 7.4 H7.2 L9.2 4.6 H14.8 L16.8 7.4 H21 V19 H3 Z M15.6 13 A3.6 3.6 0 1 1 8.4 13 A3.6 3.6 0 1 1 15.6 13 Z") },
		{ TEXT("sun"),       TEXT("M16.2 12 A4.2 4.2 0 1 1 7.8 12 A4.2 4.2 0 1 1 16.2 12 Z M12 2.2 V4.6 M12 19.4 V21.8 M2.2 12 H4.6 M19.4 12 H21.8 M5.1 5.1 L6.8 6.8 M17.2 17.2 L18.9 18.9 M5.1 18.9 L6.8 17.2 M17.2 6.8 L18.9 5.1") },
		{ TEXT("moon"),      TEXT("M20 14.6 A8.4 8.4 0 1 1 9.4 4 A6.6 6.6 0 0 0 20 14.6 Z") },
		{ TEXT("lock"),      TEXT("M5.6 10.8 H18.4 V21 H5.6 Z M8.4 10.8 V7.4 A3.6 3.6 0 0 1 15.6 7.4 V10.8 M12 14.4 V17.4") },
		{ TEXT("check"),     TEXT("M4.4 12.6 L9.6 17.8 L19.8 6.2") },
		{ TEXT("arrow"),     TEXT("M4.6 12 H19.4 M13.4 6 L19.4 12 L13.4 18") },
		{ TEXT("plus"),      TEXT("M12 5 V19 M5 12 H19") },
		{ TEXT("minus"),     TEXT("M5 12 H19") },
		{ TEXT("heart"),     TEXT("M12 20.2 C5.4 15.8 3 12 3 8.8 A4.6 4.6 0 0 1 12 6.8 A4.6 4.6 0 0 1 21 8.8 C21 12 18.6 15.8 12 20.2 Z") },
		{ TEXT("skull"),     TEXT("M12 2.8 C7.2 2.8 4 6.4 4 10.6 C4 13.4 5.4 15.2 7.2 16.2 V20.4 H16.8 V16.2 C18.6 15.2 20 13.4 20 10.6 C20 6.4 16.8 2.8 12 2.8 Z M10.4 20.4 V17.8 M13.6 20.4 V17.8 F9 11.2 1.9 F15 11.2 1.9") },
		{ TEXT("menu"),      TEXT("M4 6.8 H20 M4 12 H20 M4 17.2 H20") },
		{ TEXT("info"),      TEXT("M21 12 A9 9 0 1 1 3 12 A9 9 0 1 1 21 12 Z M12 10.8 V16.8 F12 7.6 1.1") },
		{ TEXT("star"),      TEXT("M12 2.8 L14.6 9 L21.2 9.4 L16.1 13.6 L17.7 20.2 L12 16.6 L6.3 20.2 L7.9 13.6 L2.8 9.4 L9.4 9 Z") },
		{ TEXT("fire"),      TEXT("M12 21.4 C7.8 21.4 5.4 18.6 5.4 15.2 C5.4 11.4 8.6 9.4 9.4 5.8 C11 7.4 11.6 9 11.4 11 C12.8 9.6 13.6 7.2 13.4 2.6 C17.2 5 18.6 10.4 18.6 14.8 C18.6 18.6 16.2 21.4 12 21.4 Z M12 21.4 C10.2 21.4 9.4 20 9.4 18.6 C9.4 16.8 11 15.8 11.6 14.2 C13.6 15.8 14.6 17 14.6 18.6 C14.6 20 13.8 21.4 12 21.4 Z") },
		{ TEXT("storm"),     TEXT("M7 13.4 H17.4 A3.4 3.4 0 0 0 17.7 6.6 A5.4 5.4 0 0 0 7.3 7.2 A3.1 3.1 0 0 0 7 13.4 Z M12.6 13.4 L10 17.6 H13.4 L11.6 21.6") },
		{ TEXT("drought"),   TEXT("M15.4 7 A3.4 3.4 0 1 1 8.6 7 A3.4 3.4 0 1 1 15.4 7 Z M12 1.6 V2.4 M5.4 7 H4.6 M19.4 7 H18.6 M3 14.2 L7 14.2 L9.2 17.6 L11.4 14.2 L13.6 18.4 L15.4 14.2 H21 M3 20.6 H21") },
		{ TEXT("crown"),     TEXT("M3.4 18.4 L2.6 7.6 L8 12 L12 4.6 L16 12 L21.4 7.6 L20.6 18.4 Z M3.4 21 H20.6") },
		{ TEXT("rainfall"),  TEXT("M12 3 C15.6 7.8 18 11.4 18 14.6 A6 6 0 0 1 6 14.6 C6 11.4 8.4 7.8 12 3 Z M9.4 15 C9.4 16.6 10.4 17.8 12 18") },
		{ TEXT("devotion"),  TEXT("M12 21 V14.4 M12 12.4 C14.2 10.6 14.4 8.2 12 5 C9.6 8.2 9.8 10.6 12 12.4 Z M5 17.6 C7 16.4 9.4 16.2 12 17.6 C14.6 16.2 17 16.4 19 17.6 M4.4 8.6 L6.6 9.6 M19.6 8.6 L17.4 9.6 M12 2 V3") },
		{ TEXT("habitability"),TEXT("M3.4 11.4 L12 4 L20.6 11.4 M6 9.4 V20 H18 V9.4 M12 18 C9 15.8 8.4 14.4 8.4 13.2 A1.9 1.9 0 0 1 12 12.4 A1.9 1.9 0 0 1 15.6 13.2 C15.6 14.4 15 15.8 12 18 Z") },
		{ TEXT("flourish"),  TEXT("M1 12 H8.4 M15.6 12 H23 M12 8.6 L15.4 12 L12 15.4 L8.6 12 Z F12 12 1") },
		{ TEXT("dot"),       TEXT("F12 12 2.4") },
	};

	// --------------------------------------------------------------- path parser
	struct FParser
	{
		const TCHAR* P;
		explicit FParser(const TCHAR* In) : P(In) {}

		void SkipSep() { while (*P && (FChar::IsWhitespace(*P) || *P == TEXT(','))) ++P; }
		bool AtNumber()
		{
			SkipSep();
			return *P && (FChar::IsDigit(*P) || *P == TEXT('-') || *P == TEXT('+') || *P == TEXT('.'));
		}
		float Num()
		{
			SkipSep();
			const TCHAR* Start = P;
			if (*P == TEXT('-') || *P == TEXT('+')) ++P;
			bool bDot = false;
			while (*P && (FChar::IsDigit(*P) || (*P == TEXT('.') && !bDot)))
			{
				if (*P == TEXT('.')) bDot = true;
				++P;
			}
			if (*P == TEXT('e') || *P == TEXT('E'))
			{
				++P;
				if (*P == TEXT('-') || *P == TEXT('+')) ++P;
				while (*P && FChar::IsDigit(*P)) ++P;
			}
			const FString S(int32(P - Start), Start);
			return S.IsEmpty() ? 0.f : FCString::Atof(*S);
		}
		bool Flag()
		{
			SkipSep();
			const bool b = *P == TEXT('1');
			if (*P) ++P;
			return b;
		}
	};

	void ArcTo(TArray<FVector2f>& Out, const FVector2f& P0, float Rx, float Ry, float RotDeg, bool bLarge, bool bSweep, const FVector2f& P1)
	{
		// SVG implementation notes F.6.5 (endpoint -> centre parameterisation)
		if (P0.Equals(P1, 1e-5f)) return;
		Rx = FMath::Abs(Rx); Ry = FMath::Abs(Ry);
		if (Rx < 1e-5f || Ry < 1e-5f) { Out.Add(P1); return; }
		const float Phi = FMath::DegreesToRadians(RotDeg);
		const float C = FMath::Cos(Phi), S = FMath::Sin(Phi);
		const FVector2f D = (P0 - P1) * 0.5f;
		const float X1 = C * D.X + S * D.Y, Y1 = -S * D.X + C * D.Y;
		float L = (X1 * X1) / (Rx * Rx) + (Y1 * Y1) / (Ry * Ry);
		if (L > 1.f) { const float K = FMath::Sqrt(L); Rx *= K; Ry *= K; }
		const float Num = Rx * Rx * Ry * Ry - Rx * Rx * Y1 * Y1 - Ry * Ry * X1 * X1;
		const float Den = Rx * Rx * Y1 * Y1 + Ry * Ry * X1 * X1;
		float Co = Den > 0.f ? FMath::Sqrt(FMath::Max(0.f, Num / Den)) : 0.f;
		if (bLarge == bSweep) Co = -Co;
		const float Cx1 = Co * Rx * Y1 / Ry, Cy1 = -Co * Ry * X1 / Rx;
		const FVector2f Mid = (P0 + P1) * 0.5f;
		const FVector2f Ctr(C * Cx1 - S * Cy1 + Mid.X, S * Cx1 + C * Cy1 + Mid.Y);
		auto Ang = [](float Ux, float Uy, float Vx, float Vy)
		{
			const float Dot = Ux * Vx + Uy * Vy;
			const float Len = FMath::Sqrt((Ux * Ux + Uy * Uy) * (Vx * Vx + Vy * Vy));
			float A = FMath::Acos(FMath::Clamp(Len > 0.f ? Dot / Len : 1.f, -1.f, 1.f));
			if (Ux * Vy - Uy * Vx < 0.f) A = -A;
			return A;
		};
		const float Th1 = Ang(1.f, 0.f, (X1 - Cx1) / Rx, (Y1 - Cy1) / Ry);
		float DTh = Ang((X1 - Cx1) / Rx, (Y1 - Cy1) / Ry, (-X1 - Cx1) / Rx, (-Y1 - Cy1) / Ry);
		if (!bSweep && DTh > 0.f) DTh -= 2.f * PI;
		if (bSweep && DTh < 0.f) DTh += 2.f * PI;
		const int32 N = FMath::Clamp(int32(FMath::Abs(DTh) / FMath::DegreesToRadians(9.f)) + 1, 2, 64);
		for (int32 I = 1; I <= N; ++I)
		{
			const float T = Th1 + DTh * float(I) / float(N);
			const float Ex = Rx * FMath::Cos(T), Ey = Ry * FMath::Sin(T);
			Out.Add(I == N ? P1 : FVector2f(C * Ex - S * Ey + Ctr.X, S * Ex + C * Ey + Ctr.Y));
		}
	}

	void CubicTo(TArray<FVector2f>& Out, const FVector2f& P0, const FVector2f& C1, const FVector2f& C2, const FVector2f& P1)
	{
		const int32 N = 12;
		for (int32 I = 1; I <= N; ++I)
		{
			const float T = float(I) / N, U = 1.f - T;
			Out.Add(P0 * (U * U * U) + C1 * (3.f * U * U * T) + C2 * (3.f * U * T * T) + P1 * (T * T * T));
		}
	}

	void QuadTo(TArray<FVector2f>& Out, const FVector2f& P0, const FVector2f& C1, const FVector2f& P1)
	{
		const int32 N = 8;
		for (int32 I = 1; I <= N; ++I)
		{
			const float T = float(I) / N, U = 1.f - T;
			Out.Add(P0 * (U * U) + C1 * (2.f * U * T) + P1 * (T * T));
		}
	}

	DmIcons::FIcon MakeGear()
	{
		DmIcons::FIcon G;
		TArray<FVector2f> Outline;
		const int32 Teeth = 8;
		for (int32 K = 0; K <= Teeth; ++K)
		{
			const float A = 2.f * PI * float(K % Teeth) / Teeth;
			const float H = 2.f * PI / Teeth;
			const float Pts[4][2] = { { A - H * 0.5f, 7.0f }, { A - H * 0.22f, 7.0f }, { A - H * 0.14f, 9.6f }, { A + H * 0.14f, 9.6f } };
			for (int32 J = 0; J < 4; ++J)
				if (K < Teeth || J == 0) Outline.Add(FVector2f(12.f + Pts[J][1] * FMath::Cos(Pts[J][0]), 12.f + Pts[J][1] * FMath::Sin(Pts[J][0])));
			if (K < Teeth) Outline.Add(FVector2f(12.f + 7.f * FMath::Cos(A + H * 0.22f), 12.f + 7.f * FMath::Sin(A + H * 0.22f)));
		}
		G.Lines.Add(Outline);
		G.Lines.Append(DmIcons::Parse(TEXT("M15 12 A3 3 0 1 1 9 12 A3 3 0 1 1 15 12 Z")).Lines);
		return G;
	}

	TMap<FName, DmIcons::FIcon>& Table()
	{
		static TMap<FName, DmIcons::FIcon> T;
		if (T.Num() == 0)
		{
			for (const FEntry& E : GIcons) T.Add(FName(E.Name), DmIcons::Parse(E.Path));
			T.Add(FName(TEXT("gear")), MakeGear());
		}
		return T;
	}
}

DmIcons::FIcon DmIcons::Parse(const TCHAR* Path)
{
	FIcon Icon;
	FParser Ps(Path);
	TArray<FVector2f> Cur;
	FVector2f Pen(0, 0), Start(0, 0), LastCtrl(0, 0);
	TCHAR Cmd = 0, Prev = 0;
	auto Flush = [&]()
	{
		if (Cur.Num() >= 2) Icon.Lines.Add(Cur);
		Cur.Reset();
	};
	while (true)
	{
		Ps.SkipSep();
		if (!*Ps.P) break;
		if (FChar::IsAlpha(*Ps.P)) { Cmd = *Ps.P; ++Ps.P; }
		else if (!Cmd) break;
		const bool bRel = FChar::IsLower(Cmd);
		const TCHAR U = FChar::ToUpper(Cmd);
		const FVector2f Base = bRel ? Pen : FVector2f(0, 0);
		auto Pt = [&]() { const float X = Ps.Num(); const float Y = Ps.Num(); return FVector2f(X, Y) + Base; };
		switch (U)
		{
		case TEXT('M'):
			Flush();
			Pen = Pt(); Start = Pen; Cur.Add(Pen);
			Cmd = bRel ? TEXT('l') : TEXT('L');   // subsequent pairs are line-tos
			break;
		case TEXT('L'): Pen = Pt(); Cur.Add(Pen); break;
		case TEXT('H'): { const float X = Ps.Num(); Pen.X = bRel ? Pen.X + X : X; Cur.Add(Pen); break; }
		case TEXT('V'): { const float Y = Ps.Num(); Pen.Y = bRel ? Pen.Y + Y : Y; Cur.Add(Pen); break; }
		case TEXT('C'):
		{
			const FVector2f C1 = Pt(), C2 = Pt(), P1 = Pt();
			if (Cur.Num() == 0) Cur.Add(Pen);
			CubicTo(Cur, Pen, C1, C2, P1); LastCtrl = C2; Pen = P1; break;
		}
		case TEXT('S'):
		{
			const TCHAR PU = FChar::ToUpper(Prev);
			const FVector2f C1 = (PU == TEXT('C') || PU == TEXT('S')) ? Pen * 2.f - LastCtrl : Pen;
			const FVector2f C2 = Pt(), P1 = Pt();
			if (Cur.Num() == 0) Cur.Add(Pen);
			CubicTo(Cur, Pen, C1, C2, P1); LastCtrl = C2; Pen = P1; break;
		}
		case TEXT('Q'):
		{
			const FVector2f C1 = Pt(), P1 = Pt();
			if (Cur.Num() == 0) Cur.Add(Pen);
			QuadTo(Cur, Pen, C1, P1); LastCtrl = C1; Pen = P1; break;
		}
		case TEXT('T'):
		{
			const TCHAR PU = FChar::ToUpper(Prev);
			const FVector2f C1 = (PU == TEXT('Q') || PU == TEXT('T')) ? Pen * 2.f - LastCtrl : Pen;
			const FVector2f P1 = Pt();
			if (Cur.Num() == 0) Cur.Add(Pen);
			QuadTo(Cur, Pen, C1, P1); LastCtrl = C1; Pen = P1; break;
		}
		case TEXT('A'):
		{
			const float Rx = Ps.Num(), Ry = Ps.Num(), Rot = Ps.Num();
			const bool bLarge = Ps.Flag(), bSweep = Ps.Flag();
			const FVector2f P1 = Pt();
			if (Cur.Num() == 0) Cur.Add(Pen);
			ArcTo(Cur, Pen, Rx, Ry, Rot, bLarge, bSweep, P1); Pen = P1; break;
		}
		case TEXT('Z'):
			if (Cur.Num() > 0)
			{
				if (!Cur.Last().Equals(Start, 1e-4f)) Cur.Add(Start);
				// overlap the first segment so the closing joint is mitred like the others
				if (Cur.Num() >= 3) { const FVector2f Over = FMath::Lerp(Cur[0], Cur[1], 0.4f); Cur.Add(Over); }
			}
			Pen = Start;
			Flush();
			Cmd = 0;
			break;
		case TEXT('F'):
		{
			Flush();
			const float X = Ps.Num(), Y = Ps.Num(), R = Ps.Num();
			Icon.Dots.Add(FVector3f(X, Y, R));
			Cmd = 0;
			break;
		}
		default:
			++Ps.P;   // unknown command: skip a character
			Cmd = 0;
			break;
		}
		Prev = U;
		if (U == TEXT('Z') || U == TEXT('F')) continue;
		if (!Ps.AtNumber() && *Ps.P && !FChar::IsAlpha(*Ps.P)) ++Ps.P;
	}
	Flush();
	return Icon;
}

const DmIcons::FIcon& DmIcons::Get(FName Name)
{
	const TMap<FName, FIcon>& T = Table();
	if (const FIcon* I = T.Find(Name)) return *I;
	return T.FindChecked(FName(TEXT("dot")));
}

bool DmIcons::Has(FName Name)
{
	return Table().Contains(Name);
}

TArray<FName> DmIcons::Names()
{
	TArray<FName> Out;
	for (const FEntry& E : GIcons) Out.Add(FName(E.Name));
	Out.Add(FName(TEXT("gear")));
	return Out;
}

void DmIcons::Paint(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, FName Name, const FVector2f& Pos,
	float Size, const FLinearColor& Color, float StrokeScale)
{
	const FIcon& Icon = Get(Name);
	const float K = Size / 24.f;
	const float Thick = FMath::Max(1.0f, 1.6f * K * StrokeScale);
	const FPaintGeometry PG = Geometry.ToPaintGeometry();
	TArray<FVector2f> Pts;
	for (const TArray<FVector2f>& L : Icon.Lines)
	{
		Pts.Reset(L.Num());
		for (const FVector2f& P : L) Pts.Add(Pos + P * K);
		FSlateDrawElement::MakeLines(Out, Layer, PG, Pts, ESlateDrawEffect::None, Color, true, Thick);
	}
	if (Icon.Dots.Num())
	{
		const FSlateBrush& Dot = FDmHudStyle::Get().Dot;
		for (const FVector3f& D : Icon.Dots)
		{
			const float R = FMath::Max(0.9f, D.Z * K);
			const FVector2f C = Pos + FVector2f(D.X, D.Y) * K;
			FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(2.f * R, 2.f * R), FSlateLayoutTransform(C - FVector2f(R, R))), &Dot, ESlateDrawEffect::None, Color);
		}
	}
}

// ------------------------------------------------------------------ SDmIcon

void SDmIcon::Construct(const FArguments& InArgs)
{
	Icon = InArgs._Icon;
	Size = InArgs._Size;
	Color = InArgs._Color;
	Stroke = InArgs._Stroke;
	SetCanTick(false);
}

FVector2D SDmIcon::ComputeDesiredSize(float) const
{
	const float S = Size.Get();
	return FVector2D(S, S);
}

int32 SDmIcon::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const float S = Size.Get();
	const FVector2f Local = FVector2f(AllottedGeometry.GetLocalSize());
	const FVector2f Pos((Local.X - S) * 0.5f, (Local.Y - S) * 0.5f);
	FLinearColor C = Color.Get();
	C.A *= InWidgetStyle.GetColorAndOpacityTint().A;
	DmIcons::Paint(OutDrawElements, LayerId, AllottedGeometry, Icon.Get(), Pos, S, C, Stroke.Get());
	return LayerId;
}
