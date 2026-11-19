// DEMIURGE — Game/Input/DmInputScenarios.cpp : input scenarios (C3 camera-input).
//
//   input-test   every binding of old SPEC-UI 2 driven through the real input path: keys and mouse
//                buttons injected with APlayerController::InputKey (key -> mapping context -> action ->
//                handler), the wheel as an axis event, touches with InputTouch, the pointer through the
//                router's virtual pointer (offscreen runs have no OS cursor). One "DmInputTest PASS|FAIL
//                <step>: detail" line per check, then "DmInputTest RESULT ...", exit code 0 / 1.
//                Run: shot.ps1 input-test -noshot
//   cam-ring     the brush ring as the player sees it: rain over land from orbit and close up, bloom over
//                the sea (dashed, refused), heal on a settlement (highlight), meteor at mid zoom
//                (shots ue-cam-ring-*.png)

#include "Game/Camera/DmPawn.h"
#include "Game/Core/DmCoords.h"
#include "Game/Core/DmScenario.h"
#include "Game/Core/DmSimSubsystem.h"
#include "Game/Input/DmBrushRing.h"
#include "Game/Input/DmControl.h"
#include "Game/Input/DmInputRouter.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "InputKeyEventArgs.h"
#include "UnrealClient.h"

#include "Sim/State.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmInputTest, Log, All);

namespace
{
	// ------------------------------------------------------------------ injection
	void InjectKey(APlayerController* PC, const FKey& K, EInputEvent E)
	{
		if (!PC) return;
		FViewport* VP = (GEngine && GEngine->GameViewport) ? GEngine->GameViewport->Viewport : nullptr;
		const FInputDeviceId Dev = FInputDeviceId::CreateFromInternalId(0);   // the default device (ApplicationCore's mapper is not linked)
		FInputKeyEventArgs Args(VP, Dev, K, E, 1.0f, false, FPlatformTime::Cycles64());
		PC->InputKey(Args);
	}
	void InjectWheel(APlayerController* PC, float Delta)
	{
		if (!PC) return;
		FViewport* VP = (GEngine && GEngine->GameViewport) ? GEngine->GameViewport->Viewport : nullptr;
		const FInputDeviceId Dev = FInputDeviceId::CreateFromInternalId(0);   // the default device (ApplicationCore's mapper is not linked)
		FInputKeyEventArgs Args(VP, Dev, EKeys::MouseWheelAxis, Delta, 1.0f / 60.0f, 1, FPlatformTime::Cycles64());
		PC->InputKey(Args);
	}
	void InjectTouch(APlayerController* PC, uint32 Finger, ETouchType::Type Type, const FVector2D& Pos)
	{
		if (!PC) return;
		const FInputDeviceId Dev = FInputDeviceId::CreateFromInternalId(0);   // the default device (ApplicationCore's mapper is not linked)
		PC->InputTouch(Dev, Finger, Type, Pos, 1.0f, 0, FPlatformTime::Cycles64());
	}

	// ------------------------------------------------------------------ test state
	using FStepFn = TFunction<int32(int32 /*F*/)>;    // 0 = continue, 1 = pass, -1 = fail
	struct FStep { FString Name; FStepFn Fn; };

	struct FInputTest
	{
		FDmScenarioContext* Ctx = nullptr;
		APlayerController* PC = nullptr;
		ADmPawn* Pawn = nullptr;
		UDmInputRouter* Router = nullptr;
		UDmControlSubsystem* Control = nullptr;
		UDmSimSubsystem* Sim = nullptr;
		TArray<FStep> Steps;
		int32 Index = 0, StepFrame = 0, Pass = 0, Fail = 0;
		FString Msg;
		TArray<FString> Failed;
		TMap<FString, int32> Used;        // power:used per id
		int32 AweShort = 0;
		FDelegateHandle Handle;
		FVector BaseDir = FVector(1, 0, 0);
		double BaseZoom = 0.45;
		// scratch shared inside a step
		FVector V0 = FVector::ZeroVector, V1 = FVector::ZeroVector;
		double D0 = 0, D1 = 0;
		int32 I0 = 0, I1 = 0;
		FVector2D P0 = FVector2D::ZeroVector, P1 = FVector2D::ZeroVector;
		bool bDone = false;
		double T0 = 0;
	};
	FInputTest G;

	FVector2D Centre() { return FVector2D(G.Pawn->Rig().ViewportW() * 0.5, G.Pawn->Rig().ViewportH() * 0.5); }
	void Ptr(const FVector2D& P) { G.Router->SetVirtualPointer(P); }
	void Base(double Zoom = -1)
	{
		G.Control->Disarm();
		G.Pawn->Rig().StopMotion();
		G.Pawn->Jump(G.BaseDir, Zoom < 0 ? G.BaseZoom : Zoom, 0.0);
		Ptr(Centre());
	}
	/** Like Base, centred on the most populous settlement (so one is on screen). */
	void BaseTown()
	{
		const dm::World* W = G.Sim->Sim();
		int32 Big = 0;
		for (const dm::Settlement& S : W->settlements) if (!S.dead && S.pop > W->settlements[size_t(Big)].pop) Big = S.id;
		G.Control->Disarm();
		G.Pawn->Rig().StopMotion();
		G.Pawn->Jump(DmCoords::CellDir(*W, W->settlements[size_t(Big)].cell), G.BaseZoom, 0.0);
		Ptr(Centre() + FVector2D(0, 300));
	}
	double AngleBetween(const FVector& A, const FVector& B) { return FMath::Acos(FMath::Clamp(FVector::DotProduct(A.GetSafeNormal(), B.GetSafeNormal()), -1.0, 1.0)); }
	/** Ground sphere point under a pixel with the rig's current view (what the grab uses). */
	FVector GroundUnder(const FVector2D& Px)
	{
		FVector O, D;
		G.Pawn->Rig().ScreenRay(Px, O, D);
		const double R = G.Pawn->Rig().View().Target.Size();
		const double B = FVector::DotProduct(O, D), C = O.SizeSquared() - R * R;
		return O + D * (-B - FMath::Sqrt(FMath::Max(B * B - C, 0.0)));
	}
	double PxError(const FVector& Km, const FVector2D& Px)
	{
		FVector2D S;
		if (!G.Pawn->Rig().Project(Km, S)) return 1e9;
		return FVector2D::Distance(S, Px);
	}
	double NearestSettlementAng(const dm::World& W, const FVector& Dir)
	{
		double Best = 10;
		for (const dm::Settlement& S : W.settlements)
			if (!S.dead) Best = FMath::Min(Best, AngleBetween(DmCoords::CellDir(W, S.cell), Dir));
		return Best;
	}
	/** A pixel over open land / open sea far from settlements, or a visible settlement. */
	bool FindPixel(int32 Kind, FVector2D& Out, int32* OutId = nullptr)
	{
		const dm::World* W = G.Sim->Sim();
		if (!W) return false;
		const double Wd = G.Pawn->Rig().ViewportW(), Ht = G.Pawn->Rig().ViewportH();
		const FVector2D C = Centre();
		if (Kind == 2)
		{
			double Best = 1e9;
			int32 NLive = 0, NProj = 0, NIn = 0, NPick = 0;
			for (const dm::Settlement& S : W->settlements)
			{
				if (S.dead) continue;
				++NLive;
				FVector2D Scr;
				if (!G.Sim->ProjectCell(G.PC, S.cell, Scr)) continue;
				++NProj;
				if (Scr.X < 160 || Scr.Y < 160 || Scr.X > Wd - 160 || Scr.Y > Ht - 160) continue;
				++NIn;
				// the pick at the projected cell must be that cell (the router selects the settlement nearest the pick)
				FDmPick Pk;
				if (!G.Sim->PickScreen(G.PC, Scr, Pk) || Pk.Cell != S.cell) continue;
				++NPick;
				const double D = FVector2D::Distance(Scr, C);
				if (D < Best) { Best = D; Out = Scr; if (OutId) *OutId = S.id; }
			}
			if (Best >= 1e9) UE_LOG(LogDmInputTest, Display, TEXT("DmInputTest find settlement: %d live, %d projected, %d inside, %d pick back (viewport %.0fx%.0f)"), NLive, NProj, NIn, NPick, Wd, Ht);
			return Best < 1e9;
		}
		double Best = 1e9;
		for (double Y = 180; Y < Ht - 180; Y += 45)
			for (double X = 200; X < Wd - 200; X += 45)
			{
				FDmPick Pk;
				if (!G.Sim->PickScreen(G.PC, FVector2D(X, Y), Pk)) continue;
				if (Pk.bSea != (Kind == 1)) continue;
				if (NearestSettlementAng(*W, Pk.PlanetDir) < 4.0 * W->geo->step) continue;
				if (Kind == 0 && W->elev[size_t(Pk.Cell)] < W->seaLevel + 0.3) continue;   // solid land, not the shore
				if (Kind == 1 && W->elev[size_t(Pk.Cell)] > W->seaLevel - 0.5) continue;   // open sea
				const double D = FVector2D::Distance(FVector2D(X, Y), C);
				if (D < Best) { Best = D; Out = FVector2D(X, Y); }
			}
		return Best < 1e9;
	}
	/** The clock the router uses (world real time): tests stay right when other games load the machine. */
	double WorldNow() { return G.Pawn && G.Pawn->GetWorld() ? G.Pawn->GetWorld()->GetRealTimeSeconds() : FPlatformTime::Seconds(); }
	int32 UsedCount(const TCHAR* Id) { const int32* N = G.Used.Find(Id); return N ? *N : 0; }
	int32 Result(bool bOk, const FString& M) { G.Msg = M; return bOk ? 1 : -1; }

	// ------------------------------------------------------------------ steps
	void AddStep(const FString& Name, FStepFn Fn) { G.Steps.Add({ Name, MoveTemp(Fn) }); }

	void BuildSteps()
	{
		G.Steps.Reset();
		APlayerController* PC = G.PC;

		AddStep(TEXT("binding-table"), [](int32) {
			// one action per key; every power hotkey mapped; every old-spec key present
			TMap<FKey, FName> Seen;
			int32 Dup = 0, Powers = 0;
			for (const FDmKeyBinding& B : G.Router->Kbd())
			{
				if (const FName* O = Seen.Find(B.Key)) { if (*O != B.Action) ++Dup; }
				else Seen.Add(B.Key, B.Action);
				if (B.Action.ToString().StartsWith(TEXT("Power."))) ++Powers;
			}
			const FKey Need[] = { EKeys::W, EKeys::A, EKeys::S, EKeys::D, EKeys::Up, EKeys::Down, EKeys::Left, EKeys::Right, EKeys::Q, EKeys::E,
				EKeys::R, EKeys::F, EKeys::Equals, EKeys::Hyphen, EKeys::SpaceBar, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Zero,
				EKeys::Tab, EKeys::H, EKeys::LeftBracket, EKeys::RightBracket, EKeys::Escape, EKeys::MouseWheelAxis, EKeys::LeftMouseButton,
				EKeys::RightMouseButton, EKeys::MiddleMouseButton };
			int32 Missing = 0;
			for (const FKey& K : Need) if (!Seen.Contains(K)) ++Missing;
			return Result(Dup == 0 && Missing == 0 && Powers == dm::powers::kPowerCount,
				FString::Printf(TEXT("%d mappings, %d keys, %d power hotkeys of %d, %d duplicates, %d missing"), G.Router->Kbd().Num(), Seen.Num(), Powers, dm::powers::kPowerCount, Dup, Missing));
		});

		// keyboard orbit: W/up forward, S/down back, D/right right, A/left left (screen directions on the ground)
		struct FOrbitKey { FKey Key; double Fwd, Right; };
		const FOrbitKey OK[] = { { EKeys::W, 1, 0 }, { EKeys::S, -1, 0 }, { EKeys::D, 0, 1 }, { EKeys::A, 0, -1 },
			{ EKeys::Up, 1, 0 }, { EKeys::Down, -1, 0 }, { EKeys::Right, 0, 1 }, { EKeys::Left, 0, -1 } };
		for (const FOrbitKey& K : OK)
			AddStep(FString::Printf(TEXT("key-orbit-%s"), *K.Key.ToString()), [PC, K](int32 F) {
				if (F == 0) Base();
				if (F == 3) { G.V0 = G.Pawn->Rig().GetFocus(); G.V1 = G.Pawn->Rig().View().Right; InjectKey(PC, K.Key, IE_Pressed); }
				if (F == 28) InjectKey(PC, K.Key, IE_Released);
				if (F < 34) return 0;
				const FVector D = G.Pawn->Rig().GetFocus() - G.V0;
				const FVector Fw = G.Pawn->Rig().GetFwd();
				FVector Rt = G.V1 - G.V0 * FVector::DotProduct(G.V1, G.V0); Rt.Normalize();
				const double Af = FVector::DotProduct(D, Fw), Ar = FVector::DotProduct(D, Rt);
				const double Main = K.Fwd != 0 ? Af * K.Fwd : Ar * K.Right, Other = K.Fwd != 0 ? Ar : Af;
				return Result(Main > 1e-4 && FMath::Abs(Other) < 0.25 * Main,
					FString::Printf(TEXT("focus moved %.5f along the expected screen direction, %.5f across (rad)"), Main, Other));
			});

		struct FValKey { FKey Key; double Sign; bool bTurn; };
		const FValKey VK[] = { { EKeys::E, 1, true }, { EKeys::Q, -1, true }, { EKeys::R, 1, false }, { EKeys::F, -1, false },
			{ EKeys::Equals, 1, false }, { EKeys::Hyphen, -1, false }, { EKeys::Add, 1, false }, { EKeys::Subtract, -1, false } };
		for (const FValKey& K : VK)
			AddStep(FString::Printf(TEXT("key-%s-%s"), K.bTurn ? TEXT("heading") : TEXT("zoom"), *K.Key.ToString()), [PC, K](int32 F) {
				if (F == 0) Base();
				if (F == 3) { G.D0 = K.bTurn ? G.Pawn->Rig().HeadingRad() : G.Pawn->Rig().GetTargetZoom(); InjectKey(PC, K.Key, IE_Pressed); }
				if (F == 28) InjectKey(PC, K.Key, IE_Released);
				if (F < 50) return 0;
				const double D = K.bTurn ? FMath::UnwindRadians(G.Pawn->Rig().HeadingRad() - G.D0) : G.Pawn->Rig().GetTargetZoom() - G.D0;
				return Result(D * K.Sign > 0.01, FString::Printf(TEXT("%s changed by %+.4f"), K.bTurn ? TEXT("heading (rad)") : TEXT("zoom target"), D));
			});

		AddStep(TEXT("wheel-zoom-toward-pointer"), [PC](int32 F) {
			if (F == 0) Base();
			if (F == 3)
			{
				G.P0 = Centre() + FVector2D(360, 180);
				Ptr(G.P0);
				G.V0 = GroundUnder(G.P0).GetSafeNormal();
				G.D0 = G.Pawn->Rig().GetTargetZoom();
				G.D1 = AngleBetween(G.Pawn->Rig().GetFocus(), G.V0);
			}
			if (F == 5) InjectWheel(PC, 1.f);
			if (F < 60) return 0;
			const double Z = G.Pawn->Rig().GetTargetZoom() - G.D0;
			const double A = AngleBetween(G.Pawn->Rig().GetFocus(), G.V0);
			return Result(FMath::IsNearlyEqual(Z, 0.06, 1e-4) && A < G.D1,
				FString::Printf(TEXT("one notch: zoom +%.4f, pointer's ground %.4f -> %.4f rad from the focus"), Z, G.D1, A));
		});

		AddStep(TEXT("key-pause-Space"), [PC](int32 F) {
			if (F == 0) { G.I0 = G.Sim->IsPaused(); InjectKey(PC, EKeys::SpaceBar, IE_Pressed); }
			if (F == 2) InjectKey(PC, EKeys::SpaceBar, IE_Released);
			if (F == 6) { G.I1 = G.Sim->IsPaused(); InjectKey(PC, EKeys::SpaceBar, IE_Pressed); }
			if (F == 8) InjectKey(PC, EKeys::SpaceBar, IE_Released);
			if (F < 12) return 0;
			return Result(G.I1 != G.I0 && int32(G.Sim->IsPaused()) == G.I0, FString::Printf(TEXT("paused %d -> %d -> %d"), G.I0, G.I1, G.Sim->IsPaused() ? 1 : 0));
		});

		struct FSpeedKey { FKey Key; float Speed; };
		const FSpeedKey SK[] = { { EKeys::One, 1.f }, { EKeys::Two, 2.f }, { EKeys::Three, 4.f }, { EKeys::Four, 8.f }, { EKeys::Zero, 0.25f } };
		for (const FSpeedKey& K : SK)
			AddStep(FString::Printf(TEXT("key-speed-%s"), *K.Key.ToString()), [PC, K](int32 F) {
				if (F == 0) { G.Sim->SetPaused(true); InjectKey(PC, K.Key, IE_Pressed); }
				if (F == 2) InjectKey(PC, K.Key, IE_Released);
				if (F < 5) return 0;
				const bool bOk = FMath::IsNearlyEqual(G.Sim->GetSpeed(), K.Speed) && !G.Sim->IsPaused();
				const FString M = FString::Printf(TEXT("speed %.2f (want %.2f), paused %d"), G.Sim->GetSpeed(), K.Speed, G.Sim->IsPaused() ? 1 : 0);
				G.Sim->SetPaused(true);
				return Result(bOk, M);
			});

		AddStep(TEXT("key-overlay-Tab"), [PC](int32 F) {
			if (F == 0) { G.Control->SetOverlay(0); InjectKey(PC, EKeys::Tab, IE_Pressed); }
			if (F == 2) InjectKey(PC, EKeys::Tab, IE_Released);
			if (F == 4) { G.I0 = G.Control->GetOverlay(); InjectKey(PC, EKeys::Tab, IE_Pressed); }
			if (F == 6) InjectKey(PC, EKeys::Tab, IE_Released);
			if (F == 8) { G.I1 = G.Control->GetOverlay(); InjectKey(PC, EKeys::LeftShift, IE_Pressed); }
			if (F == 10) InjectKey(PC, EKeys::Tab, IE_Pressed);
			if (F == 12) { InjectKey(PC, EKeys::Tab, IE_Released); InjectKey(PC, EKeys::LeftShift, IE_Released); }
			if (F < 15) return 0;
			const int32 Back = G.Control->GetOverlay();
			G.Control->SetOverlay(0);
			return Result(G.I0 == 1 && G.I1 == 2 && Back == 1, FString::Printf(TEXT("0 -Tab-> %d -Tab-> %d -Shift+Tab-> %d"), G.I0, G.I1, Back));
		});

		AddStep(TEXT("key-hud-H"), [PC](int32 F) {
			if (F == 0) { G.I0 = G.Control->IsHudHidden(); InjectKey(PC, EKeys::H, IE_Pressed); }
			if (F == 2) InjectKey(PC, EKeys::H, IE_Released);
			if (F == 4) { G.I1 = G.Control->IsHudHidden(); InjectKey(PC, EKeys::H, IE_Pressed); }
			if (F == 6) InjectKey(PC, EKeys::H, IE_Released);
			if (F < 9) return 0;
			return Result(G.I1 != G.I0 && int32(G.Control->IsHudHidden()) == G.I0, FString::Printf(TEXT("hidden %d -> %d -> %d"), G.I0, G.I1, G.Control->IsHudHidden() ? 1 : 0));
		});

		AddStep(TEXT("key-brush-brackets"), [PC](int32 F) {
			if (F == 0) { G.Control->SetBrush(0.06f); InjectKey(PC, EKeys::LeftBracket, IE_Pressed); }
			if (F == 2) InjectKey(PC, EKeys::LeftBracket, IE_Released);
			if (F == 4) { G.D0 = G.Control->GetBrushAng(); InjectKey(PC, EKeys::RightBracket, IE_Pressed); }
			if (F == 6) InjectKey(PC, EKeys::RightBracket, IE_Released);
			if (F == 8) { G.D1 = G.Control->GetBrushAng(); for (int32 I = 0; I < 12; ++I) G.Control->SetBrush(G.Control->GetBrushAng() * 1.25f); }
			if (F < 9) return 0;
			const double Max = G.Control->GetBrushAng();
			G.Control->SetBrush(0.06f);
			return Result(FMath::IsNearlyEqual(G.D0, 0.048, 1e-5) && FMath::IsNearlyEqual(G.D1, 0.06, 1e-5) && FMath::IsNearlyEqual(Max, 0.15, 1e-5),
				FString::Printf(TEXT("0.06 -[-> %.4f -]-> %.4f; clamped at %.3f"), G.D0, G.D1, Max));
		});

		for (const dm::powers::PowerDef& Def : dm::powers::kPowers)
		{
			const FString Id = UTF8_TO_TCHAR(Def.id);
			const FKey Key(FName(*FString::Chr(TCHAR(Def.key))));
			const char* PowerId = Def.id;
			AddStep(FString::Printf(TEXT("key-power-%s-%c"), *Id, TCHAR(Def.key)), [PC, Id, Key, PowerId](int32 F) {
				// unlock state now (the live sim may have raised aweMax since the steps were built)
				const bool bUnlocked = G.D1 > 0.5;
				if (F == 0) { G.D1 = dm::powers::unlocked(*G.Sim->Sim(), PowerId) ? 1.0 : 0.0; G.Control->Disarm(); G.I0 = G.Control->ToastCount; InjectKey(PC, Key, IE_Pressed); }
				if (F == 2) InjectKey(PC, Key, IE_Released);
				if (F == 4) { G.Msg = G.Control->GetArmedPower(); G.I1 = G.Control->ToastCount; InjectKey(PC, Key, IE_Pressed); }
				if (F == 6) InjectKey(PC, Key, IE_Released);
				if (F < 9) return 0;
				const FString Armed = G.Msg;
				if (bUnlocked)
					return Result(Armed == Id && !G.Control->IsArmed(), FString::Printf(TEXT("unlocked: press arms \"%s\", press again disarms (armed now \"%s\")"), *Armed, *G.Control->GetArmedPower()));
				return Result(Armed.IsEmpty() && G.I1 == G.I0 + 1 && G.Control->LastToast == TEXT("Not yet within your power"),
					FString::Printf(TEXT("locked: refused (armed \"%s\"), toast \"%s\""), *Armed, *G.Control->LastToast));
			});
		}

		AddStep(TEXT("key-escape-chain"), [PC](int32 F) {
			if (F == 0)
			{
				G.Control->ArmPower(TEXT("rain"));
				G.I0 = G.Sim->Sim()->settlements.empty() ? -1 : 0;
				G.Control->Select(G.I0);
				G.I1 = G.Control->MenuRequests;
				G.Sim->SetPaused(false);
				InjectKey(PC, EKeys::Escape, IE_Pressed);
			}
			if (F == 2) InjectKey(PC, EKeys::Escape, IE_Released);
			if (F == 4) { G.D0 = (G.Control->IsArmed() ? 1 : 0) + (G.Control->GetSelected() == G.I0 ? 2 : 0); InjectKey(PC, EKeys::Escape, IE_Pressed); }
			if (F == 6) InjectKey(PC, EKeys::Escape, IE_Released);
			if (F == 8) { G.D1 = G.Control->GetSelected(); InjectKey(PC, EKeys::Escape, IE_Pressed); }
			if (F == 10) InjectKey(PC, EKeys::Escape, IE_Released);
			if (F < 13) return 0;
			const bool bOk = G.D0 == 2 && G.D1 == -1 && G.Control->MenuRequests == G.I1 + 1 && G.Sim->IsPaused();
			return Result(bOk, FString::Printf(TEXT("Esc 1: disarmed, still selected (%s); Esc 2: selected %d; Esc 3: menu %d -> %d, paused %d"),
				G.D0 == 2 ? TEXT("yes") : TEXT("NO"), int32(G.D1), G.I1, G.Control->MenuRequests, G.Sim->IsPaused() ? 1 : 0));
		});

		// ---------------------------------------------------------------- mouse
		AddStep(TEXT("mouse-drag-follows"), [PC](int32 F) {
			if (F == 0) Base();
			if (F == 3) { G.P0 = Centre() + FVector2D(-260, 80); Ptr(G.P0); G.V0 = GroundUnder(G.P0); G.V1 = G.Pawn->Rig().GetFocus(); }
			if (F == 4) InjectKey(PC, EKeys::LeftMouseButton, IE_Pressed);
			if (F >= 5 && F <= 24) { G.P1 = G.P0 + FVector2D(16.0 * (F - 4), -6.0 * (F - 4)); Ptr(G.P1); }
			if (F == 30) G.D0 = PxError(G.V0, G.P1);
			if (F == 42) InjectKey(PC, EKeys::LeftMouseButton, IE_Released);    // after resting: no fling
			if (F < 46) return 0;
			const double Moved = AngleBetween(G.V1, G.Pawn->Rig().GetFocus());
			return Result(G.D0 < 6.0 && Moved > 0.01 && !G.Router->IsDragging() && G.Pawn->Rig().InertiaSpeed() == 0.0,
				FString::Printf(TEXT("320 px drag: grabbed ground %.2f px from the pointer, focus moved %.4f rad, rested release -> no inertia"), G.D0, Moved));
		});

		AddStep(TEXT("mouse-fling-inertia"), [PC](int32 F) {
			if (F == 0) Base();
			if (F == 3) { G.P0 = Centre() + FVector2D(-300, 0); Ptr(G.P0); }
			if (F == 4) InjectKey(PC, EKeys::LeftMouseButton, IE_Pressed);
			if (F >= 5 && F <= 14) Ptr(G.P0 + FVector2D(36.0 * (F - 4), 0));
			if (F == 14) InjectKey(PC, EKeys::LeftMouseButton, IE_Released);
			if (F == 17) { G.D0 = G.Pawn->Rig().InertiaSpeed(); G.V0 = G.Pawn->Rig().GetFocus(); }
			if (F < 40) return 0;
			const double Coast = AngleBetween(G.V0, G.Pawn->Rig().GetFocus());
			return Result(G.D0 > 0.0 && Coast > 1e-4, FString::Printf(TEXT("release while moving: inertia %.4f rad/s, coasted %.4f rad over 23 frames"), G.D0, Coast));
		});

		AddStep(TEXT("mouse-click-select-and-deselect"), [PC](int32 F) {
			if (F == 0) { BaseTown(); G.Control->Select(-1); }
			if (F == 3) { if (!FindPixel(2, G.P0, &G.I0)) return Result(false, TEXT("no settlement on screen")); Ptr(G.P0); }
			if (F == 11) Base();
			if (F == 15) { if (!FindPixel(0, G.P1)) return Result(false, TEXT("no empty land on screen")); }
			if (F == 5) InjectKey(PC, EKeys::LeftMouseButton, IE_Pressed);
			if (F == 7) InjectKey(PC, EKeys::LeftMouseButton, IE_Released);
			if (F == 10) G.I1 = G.Control->GetSelected();
			if (F == 16) Ptr(G.P1);
			if (F == 60) InjectKey(PC, EKeys::LeftMouseButton, IE_Pressed);
			if (F == 62) InjectKey(PC, EKeys::LeftMouseButton, IE_Released);
			if (F < 66) return 0;
			return Result(G.I1 == G.I0 && G.Control->GetSelected() == -1,
				FString::Printf(TEXT("click settlement %d at (%.0f,%.0f) -> selected %d; click empty land (%.0f,%.0f) -> %d"), G.I0, G.P0.X, G.P0.Y, G.I1, G.P1.X, G.P1.Y, G.Control->GetSelected()));
		});

		AddStep(TEXT("mouse-double-click-fly-closer"), [PC](int32 F) {
			if (F == 0) Base();
			if (F == 3) { if (!FindPixel(0, G.P0)) return Result(false, TEXT("no land on screen")); Ptr(G.P0); G.V0 = GroundUnder(G.P0).GetSafeNormal(); }
			if (F == 45 || F == 49) InjectKey(PC, EKeys::LeftMouseButton, IE_Pressed);
			if (F == 47 || F == 51) InjectKey(PC, EKeys::LeftMouseButton, IE_Released);
			if (F == 53) { G.I0 = G.Pawn->Rig().Busy(); G.D0 = G.Pawn->Rig().GetTargetZoom(); }
			if (F < 240) return 0;
			const double A = AngleBetween(G.Pawn->Rig().GetFocus(), G.V0);
			return Result(G.I0 == 1 && !G.Pawn->Rig().Busy() && FMath::IsNearlyEqual(G.Pawn->Rig().GetZoom(), G.BaseZoom + 0.25, 1e-3) && A < 0.01,
				FString::Printf(TEXT("busy %d, zoom %.3f (want %.2f), landed %.4f rad from the clicked ground"), G.I0, G.Pawn->Rig().GetZoom(), G.BaseZoom + 0.25, A));
		});

		AddStep(TEXT("mouse-right-drag-heading"), [PC](int32 F) {
			if (F == 0) Base();
			if (F == 3) { G.P0 = Centre() - FVector2D(150, 0); Ptr(G.P0); G.D0 = G.Pawn->Rig().HeadingRad(); }
			if (F == 4) InjectKey(PC, EKeys::RightMouseButton, IE_Pressed);
			if (F >= 5 && F <= 14) Ptr(G.P0 + FVector2D(30.0 * (F - 4), 0));
			if (F == 16) InjectKey(PC, EKeys::RightMouseButton, IE_Released);
			if (F < 60) return 0;
			const double D = FMath::UnwindRadians(G.Pawn->Rig().HeadingRad() - G.D0);
			const double Want = 300.0 * G.Router->RightTurnPerScreen / G.Pawn->Rig().ViewportW();
			return Result(FMath::Abs(D - Want) < 0.05, FString::Printf(TEXT("300 px right-drag: heading %+.3f rad (want %+.3f)"), D, Want));
		});

		AddStep(TEXT("mouse-middle-drag-orbit"), [PC](int32 F) {
			if (F == 0) Base();
			if (F == 3) { G.P0 = Centre(); Ptr(G.P0); G.V0 = GroundUnder(G.P0); }
			if (F == 4) InjectKey(PC, EKeys::MiddleMouseButton, IE_Pressed);
			if (F >= 5 && F <= 14) { G.P1 = G.P0 + FVector2D(0, 20.0 * (F - 4)); Ptr(G.P1); }
			if (F == 20) G.D0 = PxError(G.V0, G.P1);
			if (F == 21) InjectKey(PC, EKeys::MiddleMouseButton, IE_Released);
			if (F < 24) return 0;
			return Result(G.D0 < 6.0, FString::Printf(TEXT("200 px middle-drag: grabbed ground %.2f px from the pointer"), G.D0));
		});

		AddStep(TEXT("mouse-right-click-disarms"), [PC](int32 F) {
			if (F == 0) { Base(); G.Control->ArmPower(TEXT("lightning")); G.I0 = G.Control->IsArmed(); }
			if (F == 3) InjectKey(PC, EKeys::RightMouseButton, IE_Pressed);
			if (F == 5) InjectKey(PC, EKeys::RightMouseButton, IE_Released);
			if (F < 8) return 0;
			return Result(G.I0 == 1 && !G.Control->IsArmed(), FString::Printf(TEXT("armed %d -> right-click -> armed %d"), G.I0, G.Control->IsArmed() ? 1 : 0));
		});

		AddStep(TEXT("mouse-right-drag-orbits-when-armed"), [PC](int32 F) {
			if (F == 0) { Base(); G.Control->ArmPower(TEXT("lightning")); }
			if (F == 3) { G.P0 = Centre(); Ptr(G.P0); G.V0 = GroundUnder(G.P0); G.D0 = G.Pawn->Rig().HeadingRad(); }
			if (F == 4) InjectKey(PC, EKeys::RightMouseButton, IE_Pressed);
			if (F >= 5 && F <= 14) { G.P1 = G.P0 + FVector2D(25.0 * (F - 4), 0); Ptr(G.P1); }
			if (F == 18) G.D1 = PxError(G.V0, G.P1);
			if (F == 19) InjectKey(PC, EKeys::RightMouseButton, IE_Released);
			if (F < 22) return 0;
			const bool bArmed = G.Control->IsArmed();
			G.Control->Disarm();
			return Result(G.D1 < 6.0 && bArmed && FMath::Abs(FMath::UnwindRadians(G.Pawn->Rig().HeadingRad() - G.D0)) < 0.02,
				FString::Printf(TEXT("armed right-drag orbits: grabbed ground %.2f px from the pointer, heading kept, still armed %d"), G.D1, bArmed ? 1 : 0));
		});

		AddStep(TEXT("mouse-click-power-lightning"), [PC](int32 F) {
			if (F == 0) { Base(); G.Control->ArmPower(TEXT("lightning")); }
			if (F == 3) { if (!FindPixel(0, G.P0)) return Result(false, TEXT("no land")); Ptr(G.P0); G.I0 = UsedCount(TEXT("lightning")); G.D0 = G.Sim->Sim()->god.awe; }
			if (F == 5) InjectKey(PC, EKeys::LeftMouseButton, IE_Pressed);
			if (F == 7) InjectKey(PC, EKeys::LeftMouseButton, IE_Released);
			if (F < 10) return 0;
			return Result(UsedCount(TEXT("lightning")) == G.I0 + 1 && G.Sim->Sim()->god.awe < G.D0 && G.Control->IsArmed(),
				FString::Printf(TEXT("click: power:used lightning %d -> %d, awe %.0f -> %.0f, stays armed"), G.I0, UsedCount(TEXT("lightning")), G.D0, G.Sim->Sim()->god.awe));
		});

		AddStep(TEXT("mouse-click-power-drag-cancels"), [PC](int32 F) {
			if (F == 0) { Base(); G.Control->ArmPower(TEXT("lightning")); }
			if (F == 3) { if (!FindPixel(0, G.P0)) return Result(false, TEXT("no land")); Ptr(G.P0); G.I0 = UsedCount(TEXT("lightning")); G.V0 = G.Pawn->Rig().GetFocus(); }
			if (F == 5) InjectKey(PC, EKeys::LeftMouseButton, IE_Pressed);
			if (F >= 6 && F <= 13) Ptr(G.P0 + FVector2D(6.0 * (F - 5), 0));
			if (F == 18) InjectKey(PC, EKeys::LeftMouseButton, IE_Released);
			if (F < 22) return 0;
			const double Moved = AngleBetween(G.V0, G.Pawn->Rig().GetFocus());
			G.Control->Disarm();
			return Result(UsedCount(TEXT("lightning")) == G.I0 && Moved > 1e-4,
				FString::Printf(TEXT("48 px drag: no strike (%d -> %d), orbited %.4f rad"), G.I0, UsedCount(TEXT("lightning")), Moved));
		});

		AddStep(TEXT("mouse-click-power-slop"), [PC](int32 F) {
			if (F == 0) { Base(); G.Control->ArmPower(TEXT("lightning")); }
			if (F == 3) { if (!FindPixel(0, G.P0)) return Result(false, TEXT("no land")); Ptr(G.P0); G.I0 = UsedCount(TEXT("lightning")); }
			if (F == 5) InjectKey(PC, EKeys::LeftMouseButton, IE_Pressed);
			if (F == 6) Ptr(G.P0 + FVector2D(5, 4));    // 6.4 px: still a click
			if (F == 8) InjectKey(PC, EKeys::LeftMouseButton, IE_Released);
			if (F < 11) return 0;
			G.Control->Disarm();
			return Result(UsedCount(TEXT("lightning")) == G.I0 + 1, FString::Printf(TEXT("6.4 px wobble is still a click: strikes %d -> %d"), G.I0, UsedCount(TEXT("lightning"))));
		});

		AddStep(TEXT("reject-heal-on-sea"), [PC](int32 F) {
			if (F == 0) { Base(); G.Control->ArmPower(TEXT("heal")); }
			if (F == 3) { if (!FindPixel(1, G.P0)) return Result(false, TEXT("no open sea")); Ptr(G.P0); G.I0 = G.Control->ToastCount; }
			if (F == 5) InjectKey(PC, EKeys::LeftMouseButton, IE_Pressed);
			if (F == 7) InjectKey(PC, EKeys::LeftMouseButton, IE_Released);
			if (F < 10) return 0;
			G.Control->Disarm();
			return Result(G.Control->ToastCount == G.I0 + 1 && G.Control->LastToast == TEXT("Choose a settlement") && G.Control->LastToastKind == TEXT("bad"),
				FString::Printf(TEXT("toast [%s] \"%s\""), *G.Control->LastToastKind, *G.Control->LastToast));
		});

		AddStep(TEXT("reject-bloom-brush-on-sea"), [PC](int32 F) {
			if (F == 0) { Base(); G.Control->ArmPower(TEXT("bloom")); }
			if (F == 3) { if (!FindPixel(1, G.P0)) return Result(false, TEXT("no open sea")); Ptr(G.P0); G.I0 = G.Control->ToastCount; G.I1 = UsedCount(TEXT("bloom")); }
			if (F == 5) InjectKey(PC, EKeys::LeftMouseButton, IE_Pressed);
			if (F == 25) InjectKey(PC, EKeys::LeftMouseButton, IE_Released);
			if (F < 28) return 0;
			G.Control->Disarm();
			return Result(G.Control->ToastCount == G.I0 + 1 && G.Control->LastToast == TEXT("Choose land") && UsedCount(TEXT("bloom")) == G.I1,
				FString::Printf(TEXT("20-frame stroke over the sea: %d toast(s) \"%s\", no bloom"), G.Control->ToastCount - G.I0, *G.Control->LastToast));
		});

		AddStep(TEXT("mouse-brush-rain"), [PC](int32 F) {
			if (F == 0) { Base(); G.Control->ArmPower(TEXT("rain")); }
			if (F == 3)
			{
				if (!FindPixel(0, G.P0)) return Result(false, TEXT("no land"));
				Ptr(G.P0);
				FDmPick Pk; G.Sim->PickScreen(G.PC, G.P0, Pk);
				G.I0 = Pk.Cell; G.D0 = G.Sim->Sim()->wet[size_t(Pk.Cell)];
				G.I1 = UsedCount(TEXT("rain")); G.D1 = G.Router->StrokeCalls; G.T0 = FPlatformTime::Seconds();
			}
			if (F == 5) InjectKey(PC, EKeys::LeftMouseButton, IE_Pressed);
			if (F == 6) G.V0.X = G.Router->IsStroking();
			if (F == 65) { G.T0 = FPlatformTime::Seconds() - G.T0; InjectKey(PC, EKeys::LeftMouseButton, IE_Released); }
			if (F < 68) return 0;
			const double Wet = G.Sim->Sim()->wet[size_t(G.I0)];
			const int32 Calls = G.Router->StrokeCalls - int32(G.D1);
			G.Control->Disarm();
			return Result(G.V0.X > 0 && Calls >= 55 && UsedCount(TEXT("rain")) > G.I1 && Wet > G.D0 && !G.Router->IsStroking(),
				FString::Printf(TEXT("held %.2f s: %d per-frame applications, power:used rain +%d (rate-limited), wet at cell %d %.3f -> %.3f"),
					G.T0, Calls, UsedCount(TEXT("rain")) - G.I1, G.I0, G.D0, Wet));
		});

		AddStep(TEXT("ring-brush-and-feedback"), [](int32 F) {
			ADmBrushRing* Ring = G.Router->GetRing();
			if (!Ring) return Result(false, TEXT("no ring actor"));
			if (F == 0) { Base(); G.Control->ArmPower(TEXT("rain")); }
			if (F == 3) { if (!FindPixel(0, G.P0) || !FindPixel(1, G.P1)) return Result(false, TEXT("no land / sea")); Ptr(G.P0); }
			if (F == 8)
			{
				const FDmHover& H = G.Control->GetHover();
				G.V0 = FVector(Ring->IsShown(), AngleBetween(Ring->GetCentre(), H.PlanetDir), Ring->IsDashed());
				G.D0 = Ring->GetAng();
				G.Control->ArmPower(TEXT("bloom"));
				Ptr(G.P1);
			}
			if (F == 14) { G.V1 = FVector(Ring->IsShown(), Ring->IsDashed(), 0); BaseTown(); }
			if (F == 17) { G.Control->ArmPower(TEXT("heal")); if (!FindPixel(2, G.P0, &G.I0)) return Result(false, TEXT("no settlement")); Ptr(G.P0); }
			if (F == 23)
			{
				const dm::World* W = G.Sim->Sim();
				G.D1 = AngleBetween(Ring->GetCentre(), DmCoords::CellDir(*W, W->settlements[size_t(G.I0)].cell));
				G.I1 = Ring->IsShown();
				G.Pawn->Jump(G.BaseDir, G.BaseZoom, 0.0);   // back to the base view: heal over the sea, nothing to highlight
			}
			if (F == 26) Ptr(G.P1);
			if (F == 31) { G.P1.X = Ring->IsShown(); G.Control->Disarm(); }
			if (F < 35) return 0;
			const bool bOk = G.V0.X > 0 && G.V0.Y < 0.002 && G.V0.Z == 0 && FMath::IsNearlyEqual(G.D0, double(G.Control->GetBrushAng()), 1e-6)
				&& G.V1.X > 0 && G.V1.Y > 0 && G.I1 == 1 && G.D1 < 1e-6 && G.P1.X == 0 && !Ring->IsShown();
			return Result(bOk, FString::Printf(TEXT("rain over land: shown %d, %.4f rad off the hover point, solid %d, radius %.3f = brush; bloom over sea: shown %d dashed %d; heal: rings settlement %d (%.1g rad), over sea hidden %d; disarmed hidden %d"),
				int32(G.V0.X), G.V0.Y, G.V0.Z == 0 ? 1 : 0, G.D0, int32(G.V1.X), int32(G.V1.Y), G.I1, G.D1, G.P1.X == 0 ? 1 : 0, Ring->IsShown() ? 0 : 1));
		});

		// ---------------------------------------------------------------- touch
		AddStep(TEXT("touch-drag-follows"), [PC](int32 F) {
			if (F == 0) Base();
			if (F == 3) { G.P0 = Centre() + FVector2D(-200, 100); G.V0 = GroundUnder(G.P0); InjectTouch(PC, 0, ETouchType::Began, G.P0); }
			if (F >= 4 && F <= 23) { G.P1 = G.P0 + FVector2D(15.0 * (F - 3), -8.0 * (F - 3)); InjectTouch(PC, 0, ETouchType::Moved, G.P1); }
			if (F == 28) G.D0 = PxError(G.V0, G.P1);
			if (F == 29) InjectTouch(PC, 0, ETouchType::Ended, G.P1);
			if (F < 33) return 0;
			return Result(G.D0 < 8.0 && !G.Router->IsDragging() && G.Router->TouchFingers() == 0,
				FString::Printf(TEXT("one-finger 340 px drag: grabbed ground %.2f px from the finger"), G.D0));
		});

		AddStep(TEXT("touch-tap-select"), [PC](int32 F) {
			if (F == 0) { BaseTown(); G.Control->Select(-1); }
			if (F == 3) { if (!FindPixel(2, G.P0, &G.I0)) return Result(false, TEXT("no lone settlement")); InjectTouch(PC, 0, ETouchType::Began, G.P0); }
			if (F == 5) InjectTouch(PC, 0, ETouchType::Moved, G.P0 + FVector2D(9, 6));   // 10.8 px: under the 14 px touch slop
			if (F == 6) InjectTouch(PC, 0, ETouchType::Ended, G.P0 + FVector2D(9, 6));
			if (F < 10) return 0;
			return Result(G.Control->GetSelected() == G.I0, FString::Printf(TEXT("tap (10.8 px wobble) on settlement %d -> selected %d"), G.I0, G.Control->GetSelected()));
		});

		AddStep(TEXT("touch-double-tap-fly"), [PC](int32 F) {
			if (F == 0) Base();
			if (F == 3) { if (!FindPixel(0, G.P0)) return Result(false, TEXT("no land")); }
			if (F == 45 || F == 50) InjectTouch(PC, 0, ETouchType::Began, G.P0);
			if (F == 47 || F == 52) InjectTouch(PC, 0, ETouchType::Ended, G.P0);
			if (F == 54) G.I0 = G.Pawn->Rig().Busy();
			if (F < 240) return 0;
			return Result(G.I0 == 1 && FMath::IsNearlyEqual(G.Pawn->Rig().GetZoom(), G.BaseZoom + 0.25, 1e-3), FString::Printf(TEXT("busy %d, zoom %.3f"), G.I0, G.Pawn->Rig().GetZoom()));
		});

		AddStep(TEXT("touch-long-press-tip"), [PC](int32 F) {
			if (F == 0) { Base(); G.Control->Select(-1); }
			if (F == 3) { if (!FindPixel(0, G.P0)) return Result(false, TEXT("no land")); G.I0 = G.Router->ClickCount; G.T0 = WorldNow(); InjectTouch(PC, 0, ETouchType::Began, G.P0); }
			// "no tip yet" is only meaningful well before 500 ms (a loaded machine can take that long for 3 frames)
			if (F == 6) G.I1 = (WorldNow() - G.T0 < 0.4) ? int32(G.Control->GetHover().bTip) : 0;
			if (F == 10) InjectTouch(PC, 0, ETouchType::Moved, G.P0 + FVector2D(4, 3));
			if (F <= 10 || WorldNow() - G.T0 < 0.75) return 0;
			if (!G.bDone) { G.bDone = true; G.D0 = G.Control->GetHover().bTip; InjectTouch(PC, 0, ETouchType::Ended, G.P0 + FVector2D(4, 3)); return 0; }
			G.bDone = false;
			return Result(G.I1 == 0 && G.D0 > 0 && G.Router->ClickCount == G.I0 && G.Control->GetHover().Cell >= 0,
				FString::Printf(TEXT("tip at 30 ms %d, after 0.75 s %d (cell %d), release is not a tap (clicks %d -> %d)"), G.I1, int32(G.D0), G.Control->GetHover().Cell, G.I0, G.Router->ClickCount));
		});

		AddStep(TEXT("touch-pinch-zoom"), [PC](int32 F) {
			if (F == 0) Base();
			if (F == 3) { G.D0 = G.Pawn->Rig().GetTargetZoom(); InjectTouch(PC, 0, ETouchType::Began, Centre() - FVector2D(100, 0)); InjectTouch(PC, 1, ETouchType::Began, Centre() + FVector2D(100, 0)); }
			if (F >= 4 && F <= 23)
			{
				const double H = 100.0 + 7.5 * (F - 3);   // 200 -> 500 px apart
				InjectTouch(PC, 0, ETouchType::Moved, Centre() - FVector2D(H, 0));
				InjectTouch(PC, 1, ETouchType::Moved, Centre() + FVector2D(H, 0));
			}
			if (F == 26) { InjectTouch(PC, 0, ETouchType::Ended, Centre() - FVector2D(250, 0)); InjectTouch(PC, 1, ETouchType::Ended, Centre() + FVector2D(250, 0)); }
			if (F < 30) return 0;
			const double Dz = G.Pawn->Rig().GetTargetZoom() - G.D0, Want = FMath::Log2(2.5) * G.Router->PinchZoom;
			return Result(FMath::Abs(Dz - Want) < 0.02 && G.Router->TouchFingers() == 0, FString::Printf(TEXT("fingers 200 -> 500 px: zoom %+.3f (want %+.3f)"), Dz, Want));
		});

		AddStep(TEXT("touch-twist-heading"), [PC](int32 F) {
			const double Rad = 220.0;
			auto At = [&](double A, int32 Finger) { const double S = Finger == 0 ? 1.0 : -1.0; return Centre() + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rad * S; };
			if (F == 0) Base();
			if (F == 3) { G.V0 = GroundUnder(At(0, 0)); G.D0 = G.Pawn->Rig().HeadingRad(); InjectTouch(PC, 0, ETouchType::Began, At(0, 0)); InjectTouch(PC, 1, ETouchType::Began, At(0, 1)); }
			if (F >= 4 && F <= 23)
			{
				const double A = (UE_PI / 4.0) * (F - 3) / 20.0;    // a quarter turn clockwise on screen (y down)
				InjectTouch(PC, 0, ETouchType::Moved, At(A, 0));
				InjectTouch(PC, 1, ETouchType::Moved, At(A, 1));
			}
			if (F == 60) { G.D1 = PxError(G.V0, At(UE_PI / 4.0, 0)); InjectTouch(PC, 0, ETouchType::Ended, At(UE_PI / 4.0, 0)); InjectTouch(PC, 1, ETouchType::Ended, At(UE_PI / 4.0, 1)); }
			if (F < 64) return 0;
			const double Dh = FMath::UnwindRadians(G.Pawn->Rig().HeadingRad() - G.D0);
			return Result(FMath::Abs(FMath::Abs(Dh) - UE_PI / 4.0) < 0.05 && G.D1 < 25.0,
				FString::Printf(TEXT("45 deg twist: heading %+.3f rad, the ground under a finger ends %.1f px from it (turns with the fingers)"), Dh, G.D1));
		});

		AddStep(TEXT("touch-brush-delay-and-second-finger"), [PC](int32 F) {
			if (F == 0) { Base(); G.Control->ArmPower(TEXT("rain")); }
			if (F == 3) { if (!FindPixel(0, G.P0)) return Result(false, TEXT("no land")); G.T0 = WorldNow(); G.I0 = G.Router->StrokeCalls; InjectTouch(PC, 0, ETouchType::Began, G.P0); }
			if (F == 5) { G.D0 = WorldNow() - G.T0; G.I1 = G.Router->IsStroking() ? 1 : 0; G.V0.X = G.Router->GetRing() && G.Router->GetRing()->IsShown(); }
			if (F == 40) { G.V0.Y = G.Router->IsStroking(); G.V0.Z = G.Router->StrokeCalls - G.I0; InjectTouch(PC, 1, ETouchType::Began, G.P0 + FVector2D(200, 0)); }
			if (F == 43) { G.V1.X = G.Router->IsStroking(); G.V1.Y = G.Router->IsTouchGesture(); }
			if (F == 46) InjectTouch(PC, 1, ETouchType::Ended, G.P0 + FVector2D(200, 0));
			if (F == 49) G.V1.Z = G.Router->IsStroking();     // one finger left: still no stroke until all lift
			if (F == 52) InjectTouch(PC, 0, ETouchType::Ended, G.P0);
			if (F < 56) return 0;
			G.Control->Disarm();
			const bool bOk = (G.D0 >= 0.1 || G.I1 == 0) && G.V0.X > 0 && G.V0.Y > 0 && G.V0.Z > 10 && G.V1.X == 0 && G.V1.Y > 0 && G.V1.Z == 0 && G.Router->TouchFingers() == 0;
			return Result(bOk, FString::Printf(TEXT("ring at touch-down %d; at %.0f ms stroking %d; later stroking %d (%d calls); second finger: stroke ends %d, gesture %d; one finger left, no stroke %d"),
				int32(G.V0.X), G.D0 * 1000.0, G.I1, int32(G.V0.Y), int32(G.V0.Z), G.V1.X == 0 ? 1 : 0, int32(G.V1.Y), G.V1.Z == 0 ? 1 : 0));
		});

		// ---------------------------------------------------------------- gates, shake, hover rate
		AddStep(TEXT("input-blocked-by-screen"), [PC](int32 F) {
			if (F == 0) { BaseTown(); G.Control->Select(-1); G.Control->SetInputBlocked(TEXT("screen"), true); G.I0 = G.Control->IsHudHidden(); }
			if (F == 3) { if (!FindPixel(2, G.P0, &G.I1)) return Result(false, TEXT("no settlement")); Ptr(G.P0); InjectKey(PC, EKeys::H, IE_Pressed); }
			if (F == 5) InjectKey(PC, EKeys::H, IE_Released);
			if (F == 7) InjectKey(PC, EKeys::LeftMouseButton, IE_Pressed);
			if (F == 9) InjectKey(PC, EKeys::LeftMouseButton, IE_Released);
			if (F == 12) { G.D0 = G.Control->IsHudHidden(); G.D1 = G.Control->GetSelected(); G.Control->SetInputBlocked(TEXT("screen"), false); InjectKey(PC, EKeys::H, IE_Pressed); }
			if (F == 14) InjectKey(PC, EKeys::H, IE_Released);
			if (F < 17) return 0;
			const bool bAfter = G.Control->IsHudHidden();
			G.Control->HideHud(false);
			return Result(int32(G.D0) == G.I0 && G.D1 == -1 && int32(bAfter) != G.I0,
				FString::Printf(TEXT("blocked: H ignored (%d), click ignored (selected %d); unblocked: H works (%d)"), int32(G.D0), int32(G.D1), bAfter ? 1 : 0));
		});

		AddStep(TEXT("camera-shake-event"), [](int32 F) {
			if (F == 0) { Base(); G.V0 = G.Pawn->Rig().View().Eye; G.Control->RequestShake(0.8f); }
			if (F == 2) G.D0 = G.Pawn->Rig().ShakeAmplitude();
			if (F < 400) return 0;
			return Result(G.D0 > 0.5 && G.Pawn->Rig().ShakeAmplitude() == 0.0 && FVector::Dist(G.V0, G.Pawn->Rig().View().Eye) < 1e-6,
				FString::Printf(TEXT("camera:shake 0.8 -> amplitude %.2f, then %.3f; eye unmoved"), G.D0, G.Pawn->Rig().ShakeAmplitude()));
		});

		AddStep(TEXT("hover-rate"), [](int32 F) {
			if (F == 0) { Base(); Ptr(Centre() + FVector2D(40, 20)); }
			if (F == 5) { G.I0 = G.Router->Picks; G.T0 = WorldNow(); }
			if (F < 125) return 0;
			const double Sec = WorldNow() - G.T0;
			const int32 N = G.Router->Picks - G.I0;
			const FDmHover& H = G.Control->GetHover();
			// upper bound = the spec (<= 10 Hz); the lower bound only detects a stalled hover: hitches on a
			// loaded machine legitimately drop picks (a long frame never banks them), so it is loose
			return Result(N <= Sec * 10.0 + 2 && N >= Sec * 4.0 && H.bHit && H.Cell >= 0,
				FString::Printf(TEXT("nothing armed: %d picks in %.2f s (<= 10 Hz), hover cell %d"), N, Sec, H.Cell));
		});
	}
}

static FDmScenarioRegistrar GDmInputTest(TEXT("input-test"), TEXT("input"),
	TEXT("every binding through the real input path (run with -noshot): DmInputTest lines"),
	[](FDmScenarioContext& Ctx)
	{
		G = FInputTest();
		G.Ctx = &Ctx;
		Ctx.BuildScene(DmPartsWorld | uint32(EDmPartKind::Control));
		Ctx.UseHarnessCamera(false);
		Ctx.HoldExit();
		G.Sim = Ctx.Sim;
		G.Pawn = ADmPawn::Find(Ctx.World);
		G.Control = UDmControlSubsystem::Get(Ctx.World);
		if (!G.Pawn || !G.Control || !G.Sim) { Ctx.Log(TEXT("input-test: no pawn / control / sim")); return; }
		G.Pawn->bAdoptHarnessView = false;
		G.Router = G.Pawn->Router;
		G.PC = Ctx.World->GetFirstPlayerController();
		// a live world so powers act; plenty of Awe (aweMax untouched, so the unlock rules are the fixture's)
		if (!G.Sim->IsLive()) G.Sim->MakeLive();
		G.Sim->SetPaused(true);
		if (dm::World* W = G.Sim->SimMutable()) W->god.awe = 50000.0;
		G.Handle = G.Sim->OnSimEvent.AddLambda([](const dm::Event& E)
		{
			if (const dm::EvPowerUsed* U = E.as<dm::EvPowerUsed>()) G.Used.FindOrAdd(UTF8_TO_TCHAR(U->id.c_str()))++;
			if (E.type == dm::Ev::AweShort) ++G.AweShort;
		});
		// the settled region at mid zoom: land, sea and settlements on screen
		FVector Sum = FVector::ZeroVector;
		const dm::World* W = G.Sim->Sim();
		for (const dm::Settlement& S : W->settlements) if (!S.dead) Sum += DmCoords::CellDir(*W, S.cell) * FMath::Sqrt(FMath::Max(S.pop, 1.0));
		G.BaseDir = Sum.IsNearlyZero() ? DmCoords::LatLonToDir(20, 30) : Sum.GetSafeNormal();
		G.Pawn->Jump(G.BaseDir, G.BaseZoom, 0.0);
		Ctx.SetSubsolar(15.0, 60.0);
		const dm::World& Wr = *W;
		Ctx.Log(FString::Printf(TEXT("input-test: world phase %d age %d awe %.0f aweMax %.0f, %d settlements"), int32(Wr.phase), Wr.age, Wr.god.awe, Wr.god.aweMax, int32(Wr.settlements.size())));
		BuildSteps();
	},
	[](FDmScenarioContext& Ctx, float)
	{
		if (!G.Pawn || G.Steps.Num() == 0) { if (Ctx.Frame == 5) { Ctx.SetExitCode(1); Ctx.Finish(); } return; }
		if (Ctx.Frame < 30) return;    // let the pawn, the input stack and the first frames settle
		if (G.Index >= G.Steps.Num())
		{
			if (G.Index == G.Steps.Num())
			{
				++G.Index;
				UE_LOG(LogDmInputTest, Display, TEXT("DmInputTest RESULT %s %d passed, %d failed%s%s"), G.Fail == 0 ? TEXT("PASS") : TEXT("FAIL"), G.Pass, G.Fail,
					G.Fail ? TEXT(": ") : TEXT(""), *FString::Join(G.Failed, TEXT(", ")));
				TArray<FString> Counts;
				for (const TPair<FName, int32>& It : G.Router->ActionCounts) Counts.Add(FString::Printf(TEXT("%s=%d"), *It.Key.ToString(), It.Value));
				Counts.Sort();
				UE_LOG(LogDmInputTest, Display, TEXT("DmInputTest actions fired: %s"), *FString::Join(Counts, TEXT(" ")));
				G.Sim->OnSimEvent.Remove(G.Handle);
				Ctx.SetExitCode(G.Fail == 0 ? 0 : 1);
				Ctx.Finish();
			}
			return;
		}
		FStep& S = G.Steps[G.Index];
		const int32 R = S.Fn(G.StepFrame);
		++G.StepFrame;
		if (R == 0 && G.StepFrame < 1200) return;
		const bool bPass = R == 1;
		(bPass ? G.Pass : G.Fail)++;
		if (!bPass) G.Failed.Add(S.Name);
		UE_LOG(LogDmInputTest, Display, TEXT("DmInputTest %s %s: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"), *S.Name, R == 0 ? TEXT("timed out") : *G.Msg);
		// clean slate between steps
		G.Router->ClearVirtualPointer();
		G.Router->SetVirtualPointer(Centre());
		G.Msg.Reset();
		G.bDone = false;
		++G.Index;
		G.StepFrame = 0;
	});

// ------------------------------------------------------------------ cam-ring

namespace
{
	struct FRingShot { const TCHAR* Name; const TCHAR* Power; double Zoom; int32 Kind; };   // Kind 0 land, 1 sea, 2 settlement
	const FRingShot GRingShots[] = {
		{ TEXT("orbit-rain"), TEXT("rain"), 0.0, 0 },
		{ TEXT("mid-meteor"), TEXT("meteor"), 0.45, 0 },
		{ TEXT("mid-bloom-sea"), TEXT("bloom"), 0.45, 1 },
		{ TEXT("mid-heal"), TEXT("heal"), 0.6, 2 },
		{ TEXT("close-raise"), TEXT("raise"), 0.85, 0 },
	};
	int32 GRingStage = -1;
	FVector GRingBase = FVector(1, 0, 0);
}

static FDmScenarioRegistrar GDmCamRing(TEXT("cam-ring"), TEXT("input"),
	TEXT("the brush ring at orbit, mid and close zoom; dashed over a refused target; a settlement highlight"),
	[](FDmScenarioContext& Ctx)
	{
		Ctx.BuildScene(DmPartsWorld | uint32(EDmPartKind::Control));
		Ctx.UseHarnessCamera(false);
		Ctx.HoldExit();
		G = FInputTest();
		G.Sim = Ctx.Sim;
		G.Pawn = ADmPawn::Find(Ctx.World);
		G.Control = UDmControlSubsystem::Get(Ctx.World);
		G.PC = Ctx.World->GetFirstPlayerController();
		if (!G.Pawn) return;
		G.Pawn->bAdoptHarnessView = false;
		G.Router = G.Pawn->Router;
		FVector Sum = FVector::ZeroVector;
		const dm::World* W = G.Sim->Sim();
		for (const dm::Settlement& S : W->settlements) if (!S.dead) Sum += DmCoords::CellDir(*W, S.cell) * FMath::Sqrt(FMath::Max(S.pop, 1.0));
		GRingBase = Sum.GetSafeNormal();
		G.Pawn->Jump(GRingBase, 0.0, 0.0);
		double Lat, Lon;
		DmCoords::DirToLatLon(GRingBase, Lat, Lon);
		if (FParse::Param(FCommandLine::Get(), TEXT("night"))) Ctx.SetSubsolar(FMath::Clamp(-Lat * 0.5, -23.0, 23.0), Lon + 180.0);   // -night: the region at midnight
		else if (!Ctx.Args().bSun) Ctx.SetSubsolar(FMath::Clamp(Lat * 0.5 + 8.0, 6.0, 23.0), Lon + 40.0);   // afternoon over the region
		GRingStage = -1;
	},
	[](FDmScenarioContext& Ctx, float)
	{
		if (!G.Pawn) { Ctx.Finish(); return; }
		const int32 N = UE_ARRAY_COUNT(GRingShots);
		const int32 First = Ctx.Args().Warm, Every = 120;
		for (int32 K = 0; K < N; ++K)
		{
			const int32 ShotF = First + K * Every;
			const FRingShot& S = GRingShots[K];
			if (Ctx.Frame == ShotF - Every + 2)
			{
				FVector Dir = GRingBase;
				if (S.Kind == 2)
				{
					// centre on the most populous settlement so one is under the pointer
					const dm::World* W = G.Sim->Sim();
					int32 Big = 0;
					for (const dm::Settlement& St : W->settlements) if (!St.dead && St.pop > W->settlements[size_t(Big)].pop) Big = St.id;
					Dir = DmCoords::CellDir(*W, W->settlements[size_t(Big)].cell);
				}
				G.Pawn->Jump(Dir, S.Zoom, 0.3);
				G.Control->Disarm();
				G.Router->SetVirtualPointer(Centre());
			}
			if (Ctx.Frame == ShotF - Every + 6)
			{
				FVector2D P;
				int32 Id = -1;
				if (!FindPixel(S.Kind, P, &Id)) P = Centre();
				G.Router->SetVirtualPointer(P);
				G.Control->ArmPower(S.Power);
				Ctx.Log(FString::Printf(TEXT("ring %s: %s armed, pointer (%.0f,%.0f)"), S.Name, S.Power, P.X, P.Y));
			}
			if (Ctx.Frame == ShotF)
			{
				const ADmBrushRing* R = G.Router->GetRing();
				Ctx.Log(FString::Printf(TEXT("ring %s: shown %d dashed %d radius %.3f rad, can %s/%s"), S.Name, R && R->IsShown() ? 1 : 0, R && R->IsDashed() ? 1 : 0,
					R ? R->GetAng() : 0.f, UTF8_TO_TCHAR(dm::powers::toStr(G.Control->GetHover().Can.reason)), UTF8_TO_TCHAR(dm::powers::toStr(G.Control->GetHover().Can.why))));
				Ctx.Shot(S.Name);
			}
		}
		if (Ctx.Frame == First + (N - 1) * Every + 3) Ctx.Finish();
	});
