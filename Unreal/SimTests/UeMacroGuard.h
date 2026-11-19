// DEMIURGE — SimTests/UeMacroGuard.h : force-included (/FI) into every standalone build.
//
// Inside Unreal, the simulation sources are compiled with the module's shared PCH force-included,
// so Unreal's macros (check, verify, ensure, TEXT, PI, INDEX_NONE, ...) are defined there. Code
// that uses one of these names as an identifier compiles in the standalone build but breaks (or
// silently changes meaning) in Unreal. This header defines each of them as a compile error, so the
// standalone build fails exactly where the Unreal build would.
//
// Function-like Unreal macros are guarded as function-like macros (a name used without a
// following '(' is fine in Unreal too); object-like ones are guarded as object-like macros.
// Also: never #define FLT_MAX / DBL_MAX / M_PI-style names yourself (run.ps1 turns C4005,
// "macro redefinition", into an error).
#pragma once

#define DM_UE_MACRO(name) static_assert(false, #name " is an Unreal Engine macro: rename this identifier (see docs/FOUNDATION-CPP.md)")

// ---- assertions and logging (function-like in Unreal)
#define check(...) DM_UE_MACRO(check)
#define checkf(...) DM_UE_MACRO(checkf)
#define checkSlow(...) DM_UE_MACRO(checkSlow)
#define checkfSlow(...) DM_UE_MACRO(checkfSlow)
#define checkCode(...) DM_UE_MACRO(checkCode)
#define checkNoEntry(...) DM_UE_MACRO(checkNoEntry)
#define checkNoReentry(...) DM_UE_MACRO(checkNoReentry)
#define checkNoRecursion(...) DM_UE_MACRO(checkNoRecursion)
#define verify(...) DM_UE_MACRO(verify)
#define verifyf(...) DM_UE_MACRO(verifyf)
#define verifySlow(...) DM_UE_MACRO(verifySlow)
#define ensure(...) DM_UE_MACRO(ensure)
#define ensureMsgf(...) DM_UE_MACRO(ensureMsgf)
#define ensureAlways(...) DM_UE_MACRO(ensureAlways)
#define ensureAlwaysMsgf(...) DM_UE_MACRO(ensureAlwaysMsgf)
#define unimplemented(...) DM_UE_MACRO(unimplemented)
#define UE_LOG(...) DM_UE_MACRO(UE_LOG)
#define UE_CLOG(...) DM_UE_MACRO(UE_CLOG)
#define TEXT(...) DM_UE_MACRO(TEXT)
#define LOCTEXT(...) DM_UE_MACRO(LOCTEXT)
#define NSLOCTEXT(...) DM_UE_MACRO(NSLOCTEXT)
#define ARRAY_COUNT(...) DM_UE_MACRO(ARRAY_COUNT)
#define UE_ARRAY_COUNT(...) DM_UE_MACRO(UE_ARRAY_COUNT)
#define STRUCT_OFFSET(...) DM_UE_MACRO(STRUCT_OFFSET)
#define LIKELY(...) DM_UE_MACRO(LIKELY)
#define UNLIKELY(...) DM_UE_MACRO(UNLIKELY)
#define PURE_VIRTUAL(...) DM_UE_MACRO(PURE_VIRTUAL)

// ---- math constants and other object-like macros
#define PI DM_UE_MACRO(PI)
#define HALF_PI DM_UE_MACRO(HALF_PI)
#define TWO_PI DM_UE_MACRO(TWO_PI)
#define INV_PI DM_UE_MACRO(INV_PI)
#define DOUBLE_PI DM_UE_MACRO(DOUBLE_PI)
#define SMALL_NUMBER DM_UE_MACRO(SMALL_NUMBER)
#define KINDA_SMALL_NUMBER DM_UE_MACRO(KINDA_SMALL_NUMBER)
#define BIG_NUMBER DM_UE_MACRO(BIG_NUMBER)
#define EULERS_NUMBER DM_UE_MACRO(EULERS_NUMBER)
#define DELTA DM_UE_MACRO(DELTA)
#define FLOAT_NORMAL_THRESH DM_UE_MACRO(FLOAT_NORMAL_THRESH)
#define INDEX_NONE DM_UE_MACRO(INDEX_NONE)
#define TCHAR DM_UE_MACRO(TCHAR)
#define FORCEINLINE DM_UE_MACRO(FORCEINLINE)
#define FORCENOINLINE DM_UE_MACRO(FORCENOINLINE)
#define CONSTEXPR DM_UE_MACRO(CONSTEXPR)
#define ABSTRACT DM_UE_MACRO(ABSTRACT)
#define THRESH_POINT_ON_PLANE DM_UE_MACRO(THRESH_POINT_ON_PLANE)
#define THRESH_POINTS_ARE_SAME DM_UE_MACRO(THRESH_POINTS_ARE_SAME)
#define THRESH_NORMALS_ARE_SAME DM_UE_MACRO(THRESH_NORMALS_ARE_SAME)
#define THRESH_VECTOR_NORMALIZED DM_UE_MACRO(THRESH_VECTOR_NORMALIZED)
#define THRESH_ZERO_NORM_SQUARED DM_UE_MACRO(THRESH_ZERO_NORM_SQUARED)
#define MAX_uint8 DM_UE_MACRO(MAX_uint8)
#define MAX_uint16 DM_UE_MACRO(MAX_uint16)
#define MAX_uint32 DM_UE_MACRO(MAX_uint32)
#define MAX_int8 DM_UE_MACRO(MAX_int8)
#define MAX_int16 DM_UE_MACRO(MAX_int16)
#define MAX_int32 DM_UE_MACRO(MAX_int32)
#define MAX_int64 DM_UE_MACRO(MAX_int64)
#define MIN_int32 DM_UE_MACRO(MIN_int32)
#define MAX_flt DM_UE_MACRO(MAX_flt)

// ---- object-like macros the Unreal build defines (found by probing the Demiurge module build:
//      IN and OUT are defined there as empty macros, so `int IN = 3;` silently loses its name)
#define IN DM_UE_MACRO(IN)
#define OUT DM_UE_MACRO(OUT)
#define RESTRICT DM_UE_MACRO(RESTRICT)
#define CDECL DM_UE_MACRO(CDECL)
#define STDCALL DM_UE_MACRO(STDCALL)
#define VARARGS DM_UE_MACRO(VARARGS)
#define MOBILE DM_UE_MACRO(MOBILE)
#define CONSOLE DM_UE_MACRO(CONSOLE)
