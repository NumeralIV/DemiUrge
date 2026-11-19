// DEMIURGE — Game/FX/DmFxEffects.cpp : the effect recipes (see DmFxEffects.h). Owner: C6 fx.
//
// Conventions: positions in the planet frame (uu), 1 km = 1e5 uu; emissive colours are HDR scene
// units (sunlit white ground is about 1); lit colours are albedos (0..0.9). Everything visual draws
// from the actor's own random stream, never from the world's rng.

#include "Game/FX/DmFxEffects.h"
#include "Game/FX/DmFx.h"

#include "Game/Core/DmCoords.h"
#include "Game/Sky/DmSkyActor.h"

#include "Sim/State.h"

#include <vector>

using namespace DmFx;

namespace
{
	constexpr double KmU = 1.0e5;
	constexpr double TwoPi = 6.283185307179586;

	FLinearColor Rgb(float R, float G, float B, float A = 0.f) { return FLinearColor(R, G, B, A); }
	FLinearColor Scale(const FLinearColor& C, float K) { return FLinearColor(C.R * K, C.G * K, C.B * K, C.A); }
	FLinearColor WithA(const FLinearColor& C, float A) { return FLinearColor(C.R, C.G, C.B, A); }

	FVector Horiz(const FVector& Up, double Az)
	{
		FVector E, N;
		ADmFx::TangentBasis(Up, E, N);
		return N * FMath::Cos(Az) + E * FMath::Sin(Az);
	}
	FVector RandHoriz(FRand& R, const FVector& Up) { return Horiz(Up, R.F() * TwoPi); }
	/** How many to emit this frame for a rate (per second): floor plus a random remainder. */
	int32 Count(FRand& R, double Rate, double Dt)
	{
		const double X = FMath::Max(0.0, Rate * Dt);
		int32 N = int32(X);
		if (R.F() < X - N) ++N;
		return N;
	}
	float Env(float T, float In, float Hold, float Out)
	{
		if (T < In) return Smooth01(T / FMath::Max(In, 1e-3f));
		if (T < In + Hold) return 1.f;
		return 1.f - Smooth01((T - In - Hold) / FMath::Max(Out, 1e-3f));
	}

	/** A lit smoke / dust / steam puff. */
	FParticle Puff(FRand& R, const FVector& P, const FVector& V, float Life, float S0, float S1, const FLinearColor& Albedo)
	{
		FParticle Q;
		Q.P = P; Q.V = V;
		Q.Life = Life;
		Q.Size0 = S0; Q.Size1 = S1; Q.SizePow = 0.55f;
		Q.Shape = EShape::Smoke; Q.bLit = true;
		Q.C0 = Albedo; Q.C1 = Albedo; Q.C1.A = Albedo.A * 0.75f;
		Q.FadeIn = 0.06f; Q.FadeOut = 0.55f;
		Q.Rot = R.F() * 6.283f; Q.Spin = R.Sym(0.25f);
		Q.Drag = 0.5f; Q.Wind = 0.6f;
		Q.Seed = R.Byte();
		return Q;
	}
	/** An emissive glow particle. */
	FParticle GlowP(FRand& R, const FVector& P, const FVector& V, float Life, float S0, float S1, const FLinearColor& C0, const FLinearColor& C1)
	{
		FParticle Q;
		Q.P = P; Q.V = V; Q.Life = Life;
		Q.Size0 = S0; Q.Size1 = S1;
		Q.Shape = EShape::Glow;
		Q.C0 = WithA(C0, 0.f); Q.C1 = WithA(C1, 0.f);
		Q.FadeIn = 0.02f; Q.FadeOut = 0.6f;
		Q.Seed = R.Byte();
		return Q;
	}
	/** A hot ballistic spark (streak along its velocity). */
	FParticle Spark(FRand& R, const FVector& P, const FVector& V, float Life, float Size, float Gravity, float T0, float T1, float Glow)
	{
		FParticle Q;
		Q.P = P; Q.V = V; Q.Life = Life;
		Q.Size0 = Q.Size1 = Size; Q.Aspect = 2.f; Q.Stretch = 0.05f;
		Q.Shape = EShape::Streak; Q.Orient = EOrient::Velocity;
		Q.Rise = -Gravity; Q.Drag = 0.15f;
		Q.C0 = Rgb(0, 0, 0); Q.C1 = Rgb(0, 0, 0);
		Q.Temp0 = T0; Q.Temp1 = T1; Q.Glow = Glow; Q.ColorPow = 0.7f;
		Q.FadeIn = 0.02f; Q.FadeOut = 0.25f;
		Q.Seed = R.Byte();
		return Q;
	}
	FParticle MoteP(FRand& R, const FVector& P, const FVector& V, float Life, float Size, const FLinearColor& C)
	{
		FParticle Q;
		Q.P = P; Q.V = V; Q.Life = Life;
		Q.Size0 = Size; Q.Size1 = Size * 0.7f;
		Q.Shape = EShape::Mote; Q.Drag = 0.4f;
		Q.C0 = WithA(C, 0.f); Q.C1 = WithA(Scale(C, 0.6f), 0.f);
		Q.FadeIn = 0.15f; Q.FadeOut = 0.45f;
		Q.Seed = R.Byte();
		return Q;
	}

	const FLinearColor DustAlb(0.24f, 0.21f, 0.17f, 0.6f);
	const FLinearColor AshAlb(0.10f, 0.095f, 0.09f, 0.78f);
	const FLinearColor SteamAlb(0.82f, 0.84f, 0.88f, 0.45f);
	const FLinearColor SmokeAlb(0.13f, 0.12f, 0.115f, 0.6f);

	// ================================================================ comet / meteor flight

	struct FFlight : FDmFxEffect
	{
		int32 Cell = -1;
		bool bComet = false;
		float Eta = 1.6f, Flight = 1.6f;
		FVector Target = FVector::ZeroVector, Up = FVector::UpVector, Dir = FVector::UpVector;
		double PathLen = 320.0 * KmU;
		FVector LastHead = FVector::ZeroVector;
		bool bHasLast = false;
		double SmokeGap = 0;

		FVector HeadAt(float S) const { return Target + Dir * (PathLen * FMath::Pow(FMath::Max(0.f, 1.f - S), 2.6f)); }

		bool Tick(ADmFx& Fx, float Dt) override
		{
			const dm::World* Wd = Fx.W();
			if (!Wd) return false;
			if (DisasterId >= 0)
			{
				const dm::Disaster* D = dm::state::findDisaster(*Wd, DisasterId);
				if (!D) return false;   // impact (meteor:impact plays the rest) or removed
				Eta = float(D->eta);
				Flight = FMath::Max(0.1f, float(D->flight));
			}
			else
			{
				Eta -= Dt;
				if (Eta <= 0.f)
				{
					Fx.AddEffect(DmFxFx::MakeImpact(Fx, Cell, bComet ? 0.03 : 0.05, bComet, Fx.IsSeaCell(Cell)));
					return false;
				}
			}
			const float S = FMath::Clamp(1.f - Eta / Flight, 0.f, 1.f);
			const FVector Head = HeadAt(S);
			const double AltKm = (Head.Size() - Target.Size()) / KmU;
			const float Atm = Smooth01(float((12.0 - AltKm) / 10.0));
			const FLinearColor Core = bComet ? Rgb(0.62f, 0.82f, 1.f) : Rgb(1.f, 0.80f, 0.55f);
			const FLinearColor Sheath = bComet ? Rgb(0.45f, 0.7f, 1.f) : Rgb(1.f, 0.45f, 0.16f);
			const float I = FMath::Lerp(6.f, bComet ? 30.f : 42.f, Atm);
			const float Sz = float(FMath::Lerp(0.16, 0.5, double(Atm)) * KmU);
			const float MinSz = Fx.UUPerPixel(Head) * 1.6f;
			Fx.AddFacing(Head, FMath::Max(Sz, MinSz), EShape::Glow, Scale(Core, I));
			Fx.AddFacing(Head, FMath::Max(Sz * 4.5f, MinSz * 4.f), EShape::Glow, Scale(Sheath, I * 0.10f * (0.3f + Atm)));
			Fx.AddLight(Head, Sheath, 2.f + 14.f * Atm, float(7.0 * KmU));
			if (bComet)
			{
				// ion tail away from the sun, long in space, burnt short in the air
				const double TailLen = (40.0 - 32.0 * Atm) * KmU;
				Fx.AddAxis(Head, Head - Fx.SunPlanet() * TailLen, float(1.4 * KmU), EShape::Beam, Scale(Rgb(0.4f, 0.62f, 1.f), I * 0.06f), false, 7, 0);
			}

			// trail: a continuous glowing ribbon (one segment per frame, each ageing on its own) and,
			// where the air is thick, a smoke trail that lingers and drifts
			if (!bHasLast) { LastHead = HeadAt(FMath::Max(0.f, S - 0.02f)); bHasLast = true; }
			const FVector Seg = Head - LastHead;
			const double Len = Seg.Size();
			FRand& R = Fx.R();
			if (Len > 1.0)
			{
				FParticle Rb;
				Rb.P = (Head + LastHead) * 0.5;
				Rb.Axis = Seg * 0.58;   // a little overlap hides the joints
				Rb.Orient = EOrient::Axis; Rb.Shape = EShape::Bolt; Rb.Param = 150;
				Rb.Life = 0.35f + 1.1f * Atm;
				Rb.Size0 = FMath::Max(Sz * 0.8f, MinSz * 0.5f); Rb.Size1 = Sz * (2.f + 3.f * Atm);
				Rb.C0 = WithA(Scale(Core, I * 0.32f), 0.f); Rb.C1 = WithA(Scale(Sheath, I * 0.03f), 0.f);
				Rb.ColorPow = 0.45f; Rb.FadeIn = 0.f; Rb.FadeOut = 0.9f; Rb.Seed = R.Byte();
				Fx.Emit(Rb);
			}
			const double Spacing = 0.14 * KmU;
			const int32 N = FMath::Clamp(int32(Len / Spacing) + 1, 1, 48);
			for (int32 K = 0; K < N; ++K)
			{
				const FVector P = LastHead + Seg * (double(K + 1) / N);
				const double A2 = (P.Size() - Target.Size()) / KmU;
				const float Air = Smooth01(float((12.0 - A2) / 10.0));
				if (Air < 0.1f) continue;
				const FLinearColor Alb = bComet ? WithA(SteamAlb, 0.40f * Air) : WithA(Rgb(0.30f, 0.28f, 0.26f), 0.42f * Air);
				FParticle Sm = Puff(R, P + R.Dir() * (0.04 * KmU), R.Dir() * (0.05 * KmU), R.Range(8.f, 14.f), float(0.22 * KmU), float(R.Range(0.7f, 1.1f) * KmU), Alb);
				Sm.Drag = 0.6f; Sm.Wind = 0.5f; Sm.FadeIn = 0.01f; Sm.Param = 110; Sm.SizePow = 0.45f;
				Sm.Temp0 = 2600.f; Sm.Temp1 = 900.f; Sm.Glow = 3.f * Air; Sm.ColorPow = 0.2f;
				Fx.Emit(Sm);
			}			LastHead = Head;
			return true;
		}
	};

	// ================================================================ impact

	struct FImpact : FDmFxEffect
	{
		int32 Cell = -1;
		double Rc = 5.0 * KmU;   // crater radius
		bool bComet = false, bSea = false;
		FVector Up = FVector::UpVector, G = FVector::ZeroVector;
		double DustGap = 0;
		bool bStarted = false;

		void Start(ADmFx& Fx)
		{
			bStarted = true;
			FRand& R = Fx.R();
			const float RcF = float(Rc);
			const bool bWet = bSea || bComet;
			Fx.Shake(bComet ? 0.45f : 0.85f, G, 220.0 * KmU);
			// fireball: white-hot billows cooling through orange to deep red, inside a dark smoke ball
			for (int32 I = 0; I < 22; ++I)
			{
				const FVector Dv = (Up * R.Range(0.3f, 1.0f) + R.Dir() * 0.6).GetSafeNormal();
				FParticle P;
				P.P = G + Up * (0.3 * KmU) + R.Dir() * (Rc * 0.15);
				P.V = Dv * (Rc * R.Range(0.5f, 1.2f));
				P.Life = R.Range(2.2f, 3.6f);
				P.Size0 = RcF * 0.14f; P.Size1 = RcF * R.Range(0.45f, 0.7f); P.SizePow = 0.4f;
				P.Shape = EShape::Smoke; P.Param = 40;
				P.C0 = bWet ? Rgb(6.f, 6.5f, 7.f, 0.25f) : Rgb(14.f, 8.f, 3.6f, 0.25f);
				P.C1 = bWet ? Rgb(0.2f, 0.22f, 0.25f, 0.f) : Rgb(0.5f, 0.07f, 0.015f, 0.f);
				P.ColorPow = 0.45f; P.FadeIn = 0.f; P.FadeOut = 0.5f; P.Drag = 1.3f; P.Rise = float(Rc * 0.12);
				P.Rot = R.F() * 6.28f; P.Spin = R.Sym(0.6f); P.Seed = R.Byte();
				Fx.Emit(P);
			}
			for (int32 I = 0; I < 26; ++I)
			{
				const FVector Dv = (Up * R.Range(0.4f, 1.0f) + R.Dir() * 0.55).GetSafeNormal();
				FParticle P = Puff(R, G + Up * (0.3 * KmU) + R.Dir() * (Rc * 0.2), Dv * (Rc * R.Range(0.4f, 0.9f)), R.Range(9.f, 14.f),
					RcF * 0.16f, RcF * R.Range(0.5f, 0.75f), bWet ? WithA(SteamAlb, 0.75f) : WithA(Rgb(0.085f, 0.075f, 0.07f), 0.85f));
				P.Drag = 0.9f; P.Rise = float(Rc * 0.10); P.Wind = 0.3f; P.SizePow = 0.4f; P.FadeIn = 0.05f; P.Param = 30;
				P.Temp0 = 2800.f; P.Temp1 = 800.f; P.Glow = bWet ? 0.f : 4.f; P.ColorPow = 0.3f;
				Fx.Emit(P);
			}
			// rising column spreading into a cap (dust on land, steam over water)
			for (int32 I = 0; I < 70; ++I)
			{
				const float F01 = R.F();
				const FVector V = Up * (KmU * (0.4 + 1.8 * F01)) + RandHoriz(R, Up) * (KmU * 0.55 * F01 * F01 * R.Range(0.5f, 1.f));
				FParticle P = Puff(R, G + RandHoriz(R, Up) * (Rc * 0.25 * R.F()) + Up * (0.25 * KmU), V, R.Range(24.f, 40.f),
					RcF * (0.08f + 0.08f * F01), RcF * (0.22f + 0.22f * F01) * R.Range(0.8f, 1.2f), bWet ? WithA(SteamAlb, 0.7f) : WithA(Rgb(0.22f, 0.19f, 0.16f), 0.78f));
				P.Drag = 0.45f; P.Wind = 0.7f; P.FadeIn = 0.04f; P.Param = 50;
				Fx.Emit(P);
			}			// ejecta: incandescent rock on ballistic arcs, and dark debris
			for (int32 I = 0; I < (bSea ? 20 : 80); ++I)
			{
				const FVector D = (Up * R.Range(0.55f, 1.25f) + RandHoriz(R, Up)).GetSafeNormal();
				Fx.Emit(Spark(R, G + Up * (0.1 * KmU), D * (KmU * R.Range(2.4f, 5.4f)), R.Range(2.f, 3.f), float(0.05 * KmU),
					float(4.0 * KmU), 3400.f, 1300.f, 14.f));
			}
			for (int32 I = 0; I < (bSea ? 0 : 36); ++I)
			{
				const FVector D = (Up * R.Range(0.6f, 1.3f) + RandHoriz(R, Up)).GetSafeNormal();
				FParticle P;
				P.P = G + Up * (0.1 * KmU); P.V = D * (KmU * R.Range(1.5f, 3.6f)); P.Life = 3.f;
				P.Size0 = P.Size1 = float(0.05 * KmU); P.Shape = EShape::Dot; P.bLit = true; P.Rise = float(-4.0 * KmU);
				P.C0 = P.C1 = Rgb(0.08f, 0.07f, 0.06f, 1.f); P.FadeIn = 0.01f; P.FadeOut = 0.1f; P.Seed = R.Byte();
				Fx.Emit(P);
			}
			if (bSea)
			{
				// water crown and spray
				for (int32 I = 0; I < 110; ++I)
				{
					const FVector D = (Up * R.Range(1.2f, 2.6f) + RandHoriz(R, Up) * R.Range(0.3f, 1.0f)).GetSafeNormal();
					FParticle P;
					P.P = G + RandHoriz(R, Up) * (Rc * 0.4); P.V = D * (KmU * R.Range(1.8f, 4.2f)); P.Life = R.Range(1.8f, 3.f);
					P.Size0 = float(0.09 * KmU); P.Size1 = float(0.16 * KmU); P.Shape = EShape::Dot; P.Orient = EOrient::Velocity;
					P.Aspect = 1.4f; P.Stretch = 0.03f; P.bLit = true; P.Rise = float(-4.0 * KmU);
					P.C0 = P.C1 = Rgb(0.78f, 0.84f, 0.9f, 0.85f); P.FadeIn = 0.01f; P.FadeOut = 0.3f; P.Seed = R.Byte();
					Fx.Emit(P);
				}
			}
		}

		bool Tick(ADmFx& Fx, float Dt) override
		{
			if (!Fx.W()) return false;
			if (!bStarted)
			{
				Up = Fx.CellDir(Cell);
				G = Fx.CellSurface(Cell);
				Start(Fx);
			}
			FRand& R = Fx.R();
			const float Tt = T;
			const float RcF = float(Rc);
			// flash
			if (Tt < 1.5f)
			{
				const float I = 90.f * FMath::Exp(-Tt / 0.10f) + 9.f * FMath::Exp(-Tt / 0.55f);
				const FLinearColor Fl = bComet || bSea ? Rgb(0.85f, 0.93f, 1.f) : Rgb(1.f, 0.92f, 0.80f);
				Fx.AddFacing(G + Up * (0.8 * KmU), RcF * (0.6f + 1.6f * FMath::Sqrt(Tt)), EShape::Glow, Scale(Fl, I * 0.6f));
				Fx.AddFlash(G + Up * (0.8 * KmU), RcF * (1.4f + 2.0f * FMath::Sqrt(Tt)), Scale(Fl, I * 0.25f));
				Fx.AddFlash(G + Up * (1.0 * KmU), RcF * 4.f, Scale(Fl, I * 0.02f));
			}
			const float LightI = 70.f * FMath::Exp(-Tt / 0.2f) + 8.f * FMath::Exp(-Tt / 3.f);
			Fx.AddLight(G + Up * (1.5 * KmU), bSea ? Rgb(0.85f, 0.9f, 1.f) : Rgb(1.f, 0.72f, 0.45f), LightI, RcF * 5.f);

			// shockwave: a bright thin front and a dust wall rolling outward behind it
			if (Tt < 3.6f)
			{
				const double Rr = Rc * (0.4 + 5.0 * (1.0 - FMath::Exp(-Tt / 1.3)));
				const float Fade = FMath::Exp(-Tt / 1.4f);
				const int32 Segs = FMath::Clamp(int32(TwoPi * Rr / (0.5 * KmU)), 32, 240);
				// the visible front is a soft band of compressed, lit air ahead of the dust wall, not a neon line
				// (verifier fix: the razor-cored 7-unit ring read as a UI selection circle, worst from orbit and at
				// night): soft profile, a third of the brightness, gone in about a second
				const float FrontFade = FMath::Exp(-Tt / 0.6f) * (1.f - Smooth01((Tt - 0.8f) / 1.0f));
				if (FrontFade > 0.002f)
					Fx.AddRing(Up, Rr / DmCoords::RadiusUU, 0.35 * KmU, float(Rc * 0.09 + Rr * 0.03), EShape::Bolt,
						Scale(Rgb(0.92f, 0.95f, 1.f), 2.4f * FrontFade), false, Segs, 0, 215, float(0.3 * KmU));
				DustGap += Dt;
				if (Tt < 2.6f && DustGap > 0.045)
				{
					DustGap = 0;
					const double Speed = Rc * 5.0 / 1.3 * FMath::Exp(-Tt / 1.3);
					for (int32 I = 0; I < 22; ++I)
					{
						const double Az = R.F() * TwoPi;
						const FVector Dd = Fx.Offset(Up, Rr / DmCoords::RadiusUU, Az);
						const FVector P = Dd * (Fx.SurfaceR(Dd) + 0.3 * KmU);
						const FVector Out = (P - G).GetSafeNormal();
						FParticle Q = Puff(R, P, Out * (Speed * 0.45) + Dd * (0.15 * KmU), R.Range(8.f, 14.f), RcF * 0.10f, RcF * R.Range(0.35f, 0.55f),
							bSea ? WithA(SteamAlb, 0.45f * Fade) : WithA(DustAlb, 0.55f * Fade));
						Q.Drag = 0.9f; Q.bGround = true; Q.Param = 70;
						Fx.Emit(Q);
					}
				}
			}
			// glowing crater floor and rim, cooling over half a minute
			if (!bSea && !bComet && Tt < 45.f)
			{
				const float Cool = FMath::Clamp(Tt / 45.f, 0.f, 1.f);
				const float Tk = FMath::Lerp(1900.f, 700.f, FMath::Sqrt(Cool));
				const float Hot = FMath::Pow(Tk / 1900.f, 4.f);
				const FVector Floor = Fx.CellGround(Cell, 0.04 * KmU);
				// Blackbody() returns alpha 1, which made these quads opaque "over" (verifier fix): the floor keeps some
				// cover (molten rock hides the ground) but fades it out as it cools instead of ending as a black disc
				// that pops off at 45 s; the rim and the lava light below are pure light (alpha 0)
				Fx.AddFlat(Floor, RcF * 0.8f, RcF * 0.8f, 0.7f, EShape::Disc, WithA(Scale(Blackbody(Tk), 5.f * Hot), 0.8f * (1.f - Cool)), false, 31, 210, float(0.06 * KmU));
				// rim: a wide, broken, dimmer glow (verifier fix: the thin 2.5-unit ring read as a neon donut from
				// 60 km up and from orbit)
				Fx.AddFlat(Floor + Up * (0.05 * KmU), RcF * 1.1f, RcF * 1.1f, 0.f, EShape::Ring, WithA(Scale(Blackbody(Tk * 0.9f), 0.9f * Hot), 0.f), false, 77, 110, float(0.1 * KmU));
				Fx.AddLight(Floor + Up * (0.8 * KmU), Rgb(1.f, 0.45f, 0.15f), 6.f * Hot + 0.5f * (1.f - Cool), RcF * 2.f);
				// heat: a few cinders and smoke wisps from the floor
				for (int32 I = 0, N = Count(R, 6.0 * (1.0 - Cool), Dt); I < N; ++I)
				{
					const FVector P = Floor + RandHoriz(R, Up) * (Rc * 0.7 * R.F());
					FParticle Q = Puff(R, P, Up * (0.25 * KmU), R.Range(10.f, 16.f), float(0.2 * KmU), float(0.9 * KmU), WithA(SmokeAlb, 0.4f));
					Q.Wind = 0.9f; Q.Param = 120;
					Fx.Emit(Q);
				}
			}
			if ((bSea || bComet) && Tt < 25.f)
			{
				// churned water / vapour ring spreading
				const float Fade = 1.f - Tt / 25.f;
				const double Rr = Rc * (1.2 + 2.2 * FMath::Sqrt(Tt / 25.0));
				Fx.AddRing(Up, Rr / DmCoords::RadiusUU, 0.03 * KmU, float(Rc * 0.18), EShape::Smoke, Rgb(0.82f, 0.86f, 0.9f, 0.6f * Fade), true,
					FMath::Clamp(int32(TwoPi * Rr / (0.8 * KmU)), 24, 160), bSea ? 1 : 0, 120, float(0.05 * KmU), 5);
				for (int32 I = 0, N = Count(R, 10.0 * Fade, Dt); I < N; ++I)
				{
					FParticle Q = Puff(R, G + RandHoriz(R, Up) * (Rc * 0.6 * R.F()), Up * (KmU * R.Range(0.6f, 1.4f)), R.Range(10.f, 16.f),
						RcF * 0.15f, RcF * 0.6f, WithA(SteamAlb, 0.5f));
					Q.Drag = 0.4f; Q.Param = 80;
					Fx.Emit(Q);
				}
			}
			return Tt < 46.f;
		}
	};

	// ================================================================ lightning

	struct FLightning : FDmFxEffect
	{
		FVector Ground = FVector::ZeroVector, Up = FVector::UpVector, Top = FVector::ZeroVector;
		float Strength = 1.f;
		bool bVisualOnly = false, bBuilt = false;
		TArray<FVector> Main;
		TArray<TArray<FVector>> Branches;

		static void Subdivide(FRand& R, TArray<FVector>& Pts, int32 Levels, double Rough)
		{
			for (int32 L = 0; L < Levels; ++L)
			{
				TArray<FVector> Out;
				Out.Reserve(Pts.Num() * 2);
				for (int32 I = 0; I + 1 < Pts.Num(); ++I)
				{
					const FVector A = Pts[I], B = Pts[I + 1];
					const FVector Ab = B - A;
					const double Len = Ab.Size();
					FVector Jit = R.Dir();
					Jit -= Ab.GetSafeNormal() * FVector::DotProduct(Jit, Ab.GetSafeNormal());
					Out.Add(A);
					Out.Add((A + B) * 0.5 + Jit * (Len * Rough));
				}
				Out.Add(Pts.Last());
				Pts = MoveTemp(Out);
			}
		}

		void Build(ADmFx& Fx)
		{
			bBuilt = true;
			FRand& R = Fx.R();
			Up = Ground.GetSafeNormal();
			Top = Ground + Up * (2.7 * KmU) + RandHoriz(R, Up) * (0.9 * KmU * R.F());
			Main = { Top, Ground };
			Subdivide(R, Main, 6, 0.28);
			const int32 NB = 4 + int32(R.F() * 4.f);
			for (int32 B = 0; B < NB; ++B)
			{
				const int32 I0 = 4 + int32(R.F() * float(Main.Num() * 0.6f));
				if (I0 >= Main.Num() - 1) continue;
				const FVector From = Main[I0];
				const FVector Down = (Main[FMath::Min(I0 + 6, Main.Num() - 1)] - From).GetSafeNormal();
				const double Remain = FVector::Dist(From, Ground);
				const FVector To = From + (Down + RandHoriz(R, Up) * 0.9).GetSafeNormal() * (Remain * R.Range(0.25f, 0.55f));
				TArray<FVector> Br = { From, To };
				Subdivide(R, Br, 4, 0.3);
				Branches.Add(MoveTemp(Br));
			}
			if (!bVisualOnly)
			{
				for (int32 I = 0; I < 26; ++I)
				{
					const FVector D = (Up * R.Range(0.4f, 1.f) + RandHoriz(R, Up)).GetSafeNormal();
					Fx.Emit(Spark(R, Ground + Up * (0.02 * KmU), D * (KmU * R.Range(0.3f, 0.9f)), R.Range(0.5f, 1.1f), float(0.015 * KmU), float(1.5 * KmU), 3800.f, 1500.f, 10.f));
				}
				for (int32 I = 0; I < 4; ++I)
					Fx.Emit(Puff(R, Ground + RandHoriz(R, Up) * (0.1 * KmU), Up * (0.12 * KmU), R.Range(5.f, 8.f), float(0.06 * KmU), float(0.35 * KmU), WithA(SmokeAlb, 0.45f)));
				Fx.Shake(0.15f, Ground, 60.0 * KmU);
			}
		}

		float Brightness(float Tt) const
		{
			static const float Times[5] = { 0.f, 0.07f, 0.15f, 0.27f, 0.43f };
			static const float Amp[5] = { 1.f, 0.5f, 0.9f, 0.35f, 0.65f };
			float B = 0.f;
			for (int32 K = 0; K < 5; ++K) if (Tt >= Times[K]) B += Amp[K] * FMath::Exp(-(Tt - Times[K]) / 0.028f);
			return B;
		}

		bool Tick(ADmFx& Fx, float) override
		{
			if (!bBuilt) Build(Fx);
			const float B = Brightness(T) * Strength;
			if (B > 0.01f)
			{
				const FLinearColor Col = Rgb(0.72f, 0.80f, 1.f);
				const float Wm = float(0.03 * KmU);
				for (int32 I = 0; I + 1 < Main.Num(); ++I) Fx.AddAxis(Main[I], Main[I + 1], Wm, EShape::Bolt, Scale(Col, 46.f * B), false, uint8(I), 0);
				for (const TArray<FVector>& Br : Branches)
					for (int32 I = 0; I + 1 < Br.Num(); ++I) Fx.AddAxis(Br[I], Br[I + 1], Wm * 0.55f, EShape::Bolt, Scale(Col, 20.f * B * (1.f - float(I) / Br.Num())), false, uint8(I), 0);
				Fx.AddFacing(Top, float(2.6 * KmU), EShape::Glow, Scale(Rgb(0.6f, 0.68f, 1.f), 3.5f * B));
				Fx.AddFacing(Ground + Up * (0.05 * KmU), float(0.45 * KmU), EShape::Glow, Scale(Rgb(0.85f, 0.9f, 1.f), 12.f * B));
				Fx.AddLight(Ground + Up * (0.9 * KmU), Rgb(0.75f, 0.8f, 1.f), 28.f * B, float(5.0 * KmU));
			}
			return T < 0.75f;
		}
	};

	// ================================================================ volcano

	struct FVolcano : FDmFxEffect
	{
		int32 Cell = -1;
		float Gone = 0.f;
		float NextBolt = 1.5f;
		bool bBlast = false;

		bool Tick(ADmFx& Fx, float Dt) override
		{
			const dm::World* Wd = Fx.W();
			if (!Wd) return false;
			const dm::Disaster* D = DisasterId >= 0 ? dm::state::findDisaster(*Wd, DisasterId) : nullptr;
			float E = 0.f;
			if (D)
			{
				const double Ph = D->dur > 0 ? D->t / D->dur : 1.0;
				E = float((Ph < 0.75 ? 1.0 : FMath::Max(0.0, 1.0 - (Ph - 0.75) / 0.25)) * FMath::Clamp(D->power, 0.2, 1.5));
			}
			else
			{
				Gone += Dt;
				if (Gone > 3.f) return false;
				E = FMath::Max(0.f, 1.f - Gone / 3.f) * 0.3f;
			}
			FRand& R = Fx.R();
			const FVector Up = Fx.CellDir(Cell);
			const FVector Vent = Fx.CellGround(Cell, 0.05 * KmU);
			const float Flick = 0.8f + 0.35f * FMath::PerlinNoise1D(float(Fx.Now()) * 3.1f);

			if (!bBlast)
			{
				bBlast = true;
				Fx.Shake(0.6f, Vent, 180.0 * KmU);
				for (int32 I = 0; I < 40; ++I)
				{
					const FVector V = (Up * R.Range(0.8f, 1.4f) + RandHoriz(R, Up) * 0.5).GetSafeNormal() * (KmU * R.Range(0.8f, 2.0f));
					FParticle P = Puff(R, Vent + R.Dir() * (0.2 * KmU), V, R.Range(26.f, 40.f), float(0.35 * KmU), float(R.Range(1.6f, 2.8f) * KmU), AshAlb);
					P.Drag = 0.32f; P.Wind = 1.f; P.Temp0 = 2200.f; P.Temp1 = 400.f; P.Glow = 1.5f; P.ColorPow = 0.3f;
					Fx.Emit(P);
				}
				for (int32 I = 0; I < 60; ++I)
				{
					const FVector V = (Up + RandHoriz(R, Up) * R.Range(0.2f, 0.9f)).GetSafeNormal() * (KmU * R.Range(1.5f, 3.2f));
					Fx.Emit(Spark(R, Vent, V, R.Range(1.8f, 3.f), float(0.04 * KmU), float(4.0 * KmU), 1900.f, 1000.f, 16.f));
				}
			}
			if (E > 0.01f)
			{
				// ash column: buoyant, carried by the wind aloft, spreading into a sheet
				for (int32 I = 0, N = Count(R, 11.0 * E, Dt); I < N; ++I)
				{
					const FVector V = Up * (KmU * R.Range(0.9f, 1.6f)) + R.Dir() * (0.15 * KmU);
					FParticle P = Puff(R, Vent + Up * (0.15 * KmU) + R.Dir() * (0.12 * KmU), V, R.Range(30.f, 48.f), float(0.25 * KmU), float(R.Range(1.8f, 3.2f) * KmU), AshAlb);
					P.Drag = 0.27f; P.Wind = 1.1f; P.SizePow = 0.5f; P.Param = 50;
					Fx.Emit(P);
				}
				for (int32 I = 0, N = Count(R, 4.0 * E, Dt); I < N; ++I)
				{
					FParticle P = Puff(R, Vent + RandHoriz(R, Up) * (0.25 * KmU), Up * (0.3 * KmU), R.Range(9.f, 13.f), float(0.3 * KmU), float(1.1 * KmU), WithA(Rgb(0.24f, 0.23f, 0.22f), 0.6f));
					P.Wind = 0.6f;
					Fx.Emit(P);
				}
				// lava fountain
				for (int32 I = 0, N = Count(R, 55.0 * E, Dt); I < N; ++I)
				{
					const FVector V = (Up + RandHoriz(R, Up) * R.Range(0.f, 0.4f)).GetSafeNormal() * (KmU * R.Range(1.1f, 2.3f));
					Fx.Emit(Spark(R, Vent, V, R.Range(1.f, 1.8f), float(0.03 * KmU), float(4.0 * KmU), 1750.f, 1050.f, 14.f));
				}
				// vent glow and light
				Fx.AddFacing(Vent + Up * (0.12 * KmU), float(0.5 * KmU), EShape::Glow, Scale(Rgb(1.f, 0.45f, 0.12f), 7.f * E * Flick));
				Fx.AddFacing(Vent + Up * (0.35 * KmU), float(1.6 * KmU), EShape::Glow, Scale(Rgb(1.f, 0.32f, 0.07f), 0.7f * E * Flick));
				// volcanic lightning in the plume
				NextBolt -= Dt * E;
				if (NextBolt <= 0.f)
				{
					NextBolt = R.Range(0.7f, 2.6f);
					Fx.AddEffect(DmFxFx::MakeLightning(Fx, Vent + Up * (0.4 * KmU) + RandHoriz(R, Up) * (0.5 * KmU), 0.55f, true));
				}
			}
			// lava field: every hot cell glows (the planet draws the rock; this is the light)
			float LavaSum = 0.f;
			if (D)
			{
				// The planet (C1) draws the lava surface and its glow; this is the light the lava throws into the
				// air above it. Verifier fix: cell-sized, heavily mottled discs at up to 1.2 units by day read as
				// pink / magenta blobs on the ground and, from orbit, as a grid of red polka dots. Now: wide and
				// overlapping (a continuous haze over the field), weakly mottled, faint by day.
				const double Half = Wd->geo ? Wd->geo->step * DmCoords::RadiusUU * 1.25 : 1.2 * KmU;
				for (int32_t C : D->cells)
				{
					if (C < 0 || C >= Wd->n) continue;
					const float L = Wd->lava[size_t(C)];
					if (!(L > 0.02f)) continue;
					const float Hot = FMath::Clamp(L / 1.2f, 0.f, 1.f);
					LavaSum += Hot;
					const float Tk = 1150.f + 650.f * Hot;
					const FVector P = Fx.CellGround(C, double(L) * DmCoords::UUPerElev + 0.03 * KmU);
					const float Night = 1.f - Fx.DayAt(P);
					Fx.AddFlat(P, float(Half), float(Half), float(C % 7), EShape::Disc, WithA(Scale(Blackbody(Tk), (0.15f + 0.85f * Hot) * (0.12f + 0.88f * Night) * 0.55f), 0.f), false, uint8(C), 90, float(0.03 * KmU));
					if (Hot > 0.3f && Fx.IsSeaCell(C) && R.F() < Dt * 1.5f)
						Fx.Emit(Puff(R, P, Up * (0.5 * KmU), R.Range(8.f, 12.f), float(0.25 * KmU), float(1.0 * KmU), WithA(SteamAlb, 0.5f)));
				}
			}
			Fx.AddLight(Vent + Up * (0.5 * KmU), Rgb(1.f, 0.42f, 0.15f), 14.f * E * Flick + 0.3f * FMath::Min(LavaSum, 30.f), float(7.0 * KmU));
			return true;
		}
	};

	// ================================================================ tsunami

	struct FTsunami : FDmFxEffect
	{
		int32 Cell = -1;
		double VisFront = 0, Reach = 0.6, Height = 1.6;
		float Fade = 1.f;

		bool Tick(ADmFx& Fx, float Dt) override
		{
			const dm::World* Wd = Fx.W();
			if (!Wd) return false;
			const dm::Disaster* D = DisasterId >= 0 ? dm::state::findDisaster(*Wd, DisasterId) : nullptr;
			double Target = VisFront;
			if (D)
			{
				Target = D->front;
				Reach = FMath::Max(0.05, D->ang);
				Height = D->power;
				if (Target < VisFront - 0.05) VisFront = Target;
			}
			else
			{
				Fade -= Dt / 2.5f;
				if (Fade <= 0.f) return false;
				Target = VisFront + Dt * 0.04;
			}
			VisFront += (Target - VisFront) * FMath::Min(1.0, double(Dt) * 4.0);
			if (VisFront < 0.004) return true;
			const FVector Src = Fx.CellDir(Cell);
			const double Prog = FMath::Clamp(VisFront / Reach, 0.0, 1.0);
			const float H = float(Height * (1.0 - 0.6 * Prog));
			const double CircKm = TwoPi * FMath::Sin(VisFront) * DmCoords::RadiusKm;
			const int32 Segs = FMath::Clamp(int32(CircKm / 0.7), 32, 420);
			const float A = Fade * float(0.4 + 0.6 * (1.0 - Prog));
			// crest foam, a dark trough behind it, a bright leading edge
			Fx.AddRing(Src, VisFront, 0.04 * KmU, float(KmU * (0.35 + 0.25 * H)), EShape::Smoke, Rgb(0.88f, 0.9f, 0.93f, 0.95f * A), true, Segs, 1, 110, float(0.04 * KmU), 3);
			Fx.AddRing(Src, VisFront - 0.55 / DmCoords::RadiusKm, 0.02 * KmU, float(0.5 * KmU), EShape::Smoke, Rgb(0.02f, 0.05f, 0.08f, 0.35f * A), true, Segs, 1, 60, float(0.04 * KmU), 9);
			Fx.AddRing(Src, VisFront + 0.18 / DmCoords::RadiusKm, 0.06 * KmU, float(0.07 * KmU), EShape::Bolt, Scale(Rgb(0.75f, 0.85f, 0.95f), 1.4f * A * (0.3f + 0.7f * Fx.DayAt(Src * DmCoords::RadiusUU))), false, Segs, 1, 200);
			// spray thrown off the crest
			FRand& R = Fx.R();
			for (int32 I = 0, N = Count(R, 40.0 * A, Dt); I < N; ++I)
			{
				const FVector Dd = Fx.Offset(Src, VisFront, R.F() * TwoPi);
				if (!Fx.IsSeaCell(Fx.CellAt(Dd))) continue;
				FParticle P = Puff(R, Dd * (Fx.SurfaceR(Dd) + 0.06 * KmU), Dd * (0.12 * KmU), R.Range(2.5f, 4.f), float(0.12 * KmU), float(0.5 * KmU), WithA(SteamAlb, 0.45f));
				P.Wind = 0.4f; P.Param = 140;
				Fx.Emit(P);
			}
			return true;
		}
	};

	// ================================================================ storm, drought

	struct FStorm : FDmFxEffect
	{
		FVector Centre = FVector::ZeroVector;
		bool bInit = false;
		float Gone = 0.f;
		float NextFlash = 0.5f;

		bool Tick(ADmFx& Fx, float Dt) override
		{
			const dm::World* Wd = Fx.W();
			if (!Wd) return false;
			const dm::Disaster* D = dm::state::findDisaster(*Wd, DisasterId);
			if (!D) { Gone += Dt; return Gone < 2.f; }
			const FVector Want = Fx.CellDir(D->cell);
			if (!bInit) { Centre = Want; bInit = true; }
			Centre = FMath::Lerp(Centre, Want, FMath::Min(1.0, double(Dt) * 0.8)).GetSafeNormal();
			const double Ang = FMath::Max(0.04, D->ang);
			const float Pw = FMath::Clamp(float(D->power), 0.15f, 1.5f);
			FRand& R = Fx.R();
			const double Spin = Centre.Z >= 0 ? 1.0 : -1.0;   // cyclonic
			// rain shafts under the storm
			for (int32 I = 0, N = Count(R, 14.0 * Pw, Dt); I < N; ++I)
			{
				const FVector Dd = Fx.Offset(Centre, Ang * 0.85 * FMath::Sqrt(R.F()), R.F() * TwoPi);
				FParticle P;
				P.P = Dd * Fx.SurfaceR(Dd);
				P.Shape = EShape::Rain; P.Orient = EOrient::Upright; P.bLit = true;
				P.Size0 = P.Size1 = float(KmU * R.Range(0.7f, 1.4f));
				P.Aspect = float(0.75 * KmU) / P.Size0;
				P.Life = R.Range(2.f, 3.2f); P.FadeIn = 0.25f; P.FadeOut = 0.35f;
				P.C0 = P.C1 = Rgb(0.32f, 0.34f, 0.38f, 0.7f);
				P.Seed = R.Byte();
				Fx.Emit(P);
			}
			// low dark scud under the cloud deck, turning with the storm
			for (int32 I = 0, N = Count(R, 4.0 * Pw, Dt); I < N; ++I)
			{
				const double Rr = Ang * FMath::Sqrt(R.F());
				const double Az = R.F() * TwoPi;
				const FVector Dd = Fx.Offset(Centre, Rr, Az);
				const FVector Tang = Horiz(Dd, Az + Spin * 1.5708);
				FParticle P = Puff(R, Dd * (Fx.SurfaceR(Dd) + KmU * R.Range(1.1f, 1.6f)), Tang * (0.25 * KmU), R.Range(9.f, 14.f),
					float(1.0 * KmU), float(KmU * R.Range(2.0f, 3.0f)), WithA(Rgb(0.16f, 0.17f, 0.19f), 0.6f));
				P.Drag = 0.f; P.Wind = 0.f; P.Param = 70;
				Fx.Emit(P);
			}
			// lightning: in-cloud flashes and a ground strike now and then
			NextFlash -= Dt * Pw;
			if (NextFlash <= 0.f)
			{
				NextFlash = R.Range(0.25f, 1.2f);
				const FVector Dd = Fx.Offset(Centre, Ang * 0.7 * FMath::Sqrt(R.F()), R.F() * TwoPi);
				if (R.F() < 0.3f) Fx.AddEffect(DmFxFx::MakeLightning(Fx, Dd * Fx.SurfaceR(Dd), 0.8f, true));
				else
				{
					FParticle F = GlowP(R, Dd * (Fx.SurfaceR(Dd) + 1.3 * KmU), FVector::ZeroVector, 0.22f, float(2.2 * KmU), float(2.6 * KmU),
						Rgb(0.6f, 0.68f, 1.f) * 2.6f, Rgb(0.6f, 0.68f, 1.f) * 0.2f);
					F.FadeIn = 0.01f; F.FadeOut = 0.9f; F.Flicker = 0.8f;
					Fx.Emit(F);
					// light the sky's real volumetric clouds from inside as well (C2 API; verifier addition)
					const int32 FlashCell = Fx.CellAt(Dd);
					if (ADmSky* Sky = FlashCell >= 0 ? ADmSky::Get(&Fx) : nullptr) Sky->Strike(FlashCell, 0.7f);
				}
			}
			return true;
		}
	};

	struct FDrought : FDmFxEffect
	{
		bool Tick(ADmFx& Fx, float Dt) override
		{
			const dm::World* Wd = Fx.W();
			if (!Wd) return false;
			const dm::Disaster* D = dm::state::findDisaster(*Wd, DisasterId);
			if (!D) return false;
			FRand& R = Fx.R();
			const FVector C = Fx.CellDir(D->cell);
			for (int32 I = 0, N = Count(R, 2.5, Dt); I < N; ++I)
			{
				const FVector Dd = Fx.Offset(C, D->ang * FMath::Sqrt(R.F()), R.F() * TwoPi);
				if (Fx.IsSeaCell(Fx.CellAt(Dd))) continue;
				FParticle P = Puff(R, Dd * (Fx.SurfaceR(Dd) + 0.2 * KmU), Dd * (0.03 * KmU), R.Range(12.f, 18.f), float(1.5 * KmU), float(3.5 * KmU), WithA(Rgb(0.62f, 0.5f, 0.36f), 0.12f));
				P.Wind = 0.8f; P.Param = 160;
				Fx.Emit(P);
			}
			return true;
		}
	};

	// ================================================================ earthquake

	struct FQuake : FDmFxEffect
	{
		int32 Cell = -1;
		double Ang = 0.08;
		float Power = 1.f;
		bool bStarted = false;

		bool Tick(ADmFx& Fx, float) override
		{
			const dm::World* Wd = Fx.W();
			if (!Wd || !Wd->geo) return false;
			const FVector Up = Fx.CellDir(Cell);
			FRand& R = Fx.R();
			if (!bStarted)
			{
				bStarted = true;
				const FVector G = Fx.CellSurface(Cell);
				Fx.Shake(FMath::Clamp(0.35f + 0.5f * Power, 0.f, 1.f), G, 250.0 * KmU);
				std::vector<int32_t> Cells;
				Wd->geo->within(Cell, Ang * 1.1, Cells);
				int32 Bursts = 0;
				for (int32_t C : Cells)
				{
					if (dm::state::isSea(*Wd, C)) continue;
					const bool bTown = Wd->owner[size_t(C)] >= 0 && Wd->urban[size_t(C)] > 0.2f;
					const float Slope = Wd->slope[size_t(C)];
					const float Pick = 0.18f + (bTown ? 0.6f : 0.f) + FMath::Min(0.5f, Slope);
					if (R.F() > Pick) continue;
					if (++Bursts > 70) break;
					const FVector P = Fx.CellSurface(C, 0.03 * KmU);
					FVector Down = FVector::ZeroVector;
					const int32 Dn = Wd->down[size_t(C)];
					if (Slope > 0.35f && Dn >= 0) Down = (Fx.CellSurface(Dn) - Fx.CellSurface(C)).GetSafeNormal();
					for (int32 K = 0, N = 2 + int32(R.F() * 3.f); K < N; ++K)
					{
						FParticle Q = Puff(R, P + RandHoriz(R, Up) * (0.3 * KmU * R.F()), Up * (0.12 * KmU) + Down * (KmU * R.Range(0.2f, 0.5f)),
							R.Range(6.f, 11.f), float(0.12 * KmU), float(KmU * R.Range(0.5f, 0.9f)), DustAlb);
						Q.bGround = true; Q.Drag = 0.8f; Q.Param = 60;
						Fx.Emit(Q);
					}
					if (bTown)
						for (int32 K = 0; K < 6; ++K)
						{
							FParticle Q;
							Q.P = P; Q.V = (Up * 1.2 + RandHoriz(R, Up)).GetSafeNormal() * (KmU * R.Range(0.2f, 0.6f)); Q.Life = 1.5f;
							Q.Size0 = Q.Size1 = float(0.025 * KmU); Q.Shape = EShape::Dot; Q.bLit = true; Q.Rise = float(-2.0 * KmU);
							Q.C0 = Q.C1 = Rgb(0.2f, 0.18f, 0.16f, 1.f); Q.FadeOut = 0.1f; Q.Seed = R.Byte();
							Fx.Emit(Q);
						}
				}
			}
			// ripples: three ground waves raising dust bands
			for (int32 K = 0; K < 3; ++K)
			{
				const float Tk = T - 0.35f * K;
				if (Tk <= 0.f || Tk > 2.6f) continue;
				const float Pr = Tk / 2.6f;
				const double Rr = Ang * 1.3 * FMath::Pow(double(Pr), 0.7);
				if (Rr < 0.002) continue;
				const int32 Segs = FMath::Clamp(int32(TwoPi * Rr * DmCoords::RadiusKm / 0.7), 24, 200);
				// verifier fix: three crisp white rings read as sonar / UI rings; the ground wave is now a wide, faint
				// band of raised dust (the dust plumes and the camera shake carry the event)
				Fx.AddRing(Up, Rr, 0.08 * KmU, float(1.1 * KmU), EShape::Bolt, WithA(DustAlb, 0.32f * (1.f - Pr) * FMath::Min(Power, 1.f)), true, Segs, 2, 255, float(0.05 * KmU), uint8(K * 40));
				Fx.AddRing(Up, Rr, 0.05 * KmU, float(0.8 * KmU), EShape::Bolt, Rgb(0.8f, 0.84f, 0.9f, 0.13f * (1.f - Pr)), true, Segs, 1, 255, float(0.04 * KmU), uint8(K * 40 + 9));
			}
			return T < 3.5f;
		}
	};

	// ================================================================ nuclear flash and mushroom cloud

	struct FNuke : FDmFxEffect
	{
		int32 Cell = -1;
		FVector Up = FVector::UpVector, G = FVector::ZeroVector;
		bool bStarted = false;
		double DustGap = 0;

		bool Tick(ADmFx& Fx, float Dt) override
		{
			if (!Fx.W()) return false;
			FRand& R = Fx.R();
			if (!bStarted)
			{
				bStarted = true;
				Up = Fx.CellDir(Cell);
				G = Fx.CellSurface(Cell);
				Fx.Shake(1.f, G, 320.0 * KmU);
				for (int32 I = 0; I < 30; ++I)
				{
					FParticle P = Puff(R, G + Up * (0.4 * KmU) + R.Dir() * (0.25 * KmU), Up * (0.25 * KmU) + R.Dir() * (0.3 * KmU), R.Range(16.f, 22.f),
						float(0.4 * KmU), float(KmU * R.Range(1.2f, 1.6f)), WithA(Rgb(0.13f, 0.11f, 0.10f), 0.85f));
					P.Drag = 0.5f; P.Rise = float(0.12 * KmU); P.Wind = 0.25f; P.SizePow = 0.4f; P.FadeIn = 0.f;
					P.Temp0 = 9000.f; P.Temp1 = 1200.f; P.Glow = 55.f; P.ColorPow = 0.5f;
					Fx.Emit(P);
				}
			}
			const float Tt = T;
			const double Hc = 4.2 * KmU * (1.0 - FMath::Exp(-Tt / 4.5));
			const double Rc = KmU * (0.8 + 0.22 * FMath::Min(Tt, 12.f));
			// flash
			if (Tt < 3.f)
			{
				const float I = 220.f * FMath::Exp(-Tt / 0.07f) + 25.f * FMath::Exp(-Tt / 0.8f);
				Fx.AddFacing(G + Up * (0.5 * KmU), float(KmU * (1.0 + 3.0 * FMath::Sqrt(Tt))), EShape::Glow, Scale(Rgb(1.f, 0.97f, 0.92f), I * 0.2f));
				Fx.AddFlash(G + Up * (0.5 * KmU), float(KmU * (2.0 + 8.0 * FMath::Sqrt(Tt))), Scale(Rgb(1.f, 0.97f, 0.92f), I * 0.12f));
				Fx.AddFlash(G + Up * (1.0 * KmU), float(25.0 * KmU), Scale(Rgb(1.f, 0.95f, 0.9f), I * 0.015f));
			}
			Fx.AddLight(G + Up * (1.5 * KmU), Rgb(1.f, 0.85f, 0.7f), 90.f * FMath::Exp(-Tt / 0.3f) + 12.f * FMath::Exp(-Tt / 5.f), float(25.0 * KmU));
			// cap: a rolling torus of cloud riding up on the fireball
			if (Tt < 10.f)
			{
				const double DH = 4.2 * KmU / 4.5 * FMath::Exp(-Tt / 4.5);
				for (int32 I = 0, N = Count(R, 26.0, Dt); I < N; ++I)
				{
					const double Az = R.F() * TwoPi, Th = R.F() * TwoPi;
					const FVector H = Horiz(Up, Az);
					const double Minor = 0.55 * KmU;
					const FVector P = G + Up * (Hc + Minor * FMath::Sin(Th)) + H * (Rc + Minor * FMath::Cos(Th));
					const FVector Roll = (Up * FMath::Cos(Th) - H * FMath::Sin(Th)) * (0.18 * KmU);
					FParticle Q = Puff(R, P, Up * DH + H * (0.22 * KmU) + Roll, R.Range(18.f, 26.f), float(0.45 * KmU), float(KmU * R.Range(1.0f, 1.3f)), WithA(Rgb(0.34f, 0.29f, 0.25f), 0.72f));
					Q.Drag = 0.6f; Q.Wind = 0.35f; Q.Param = 40;
					const float Hot = FMath::Exp(-Tt / 2.5f);
					if (Hot > 0.05f) { Q.Temp0 = 3800.f; Q.Temp1 = 900.f; Q.Glow = 22.f * Hot; Q.ColorPow = 0.4f; }
					Fx.Emit(Q);
				}
			}
			// stem
			if (Tt < 9.f)
				for (int32 I = 0, N = Count(R, 18.0, Dt); I < N; ++I)
				{
					const FVector P = G + Up * (Hc * 0.92 * R.F()) + RandHoriz(R, Up) * (0.3 * KmU * R.F());
					FParticle Q = Puff(R, P, Up * (0.25 * KmU), R.Range(12.f, 18.f), float(0.35 * KmU), float(0.75 * KmU), WithA(Rgb(0.36f, 0.31f, 0.26f), 0.6f));
					Q.Drag = 0.5f; Q.Wind = 0.3f;
					Fx.Emit(Q);
				}
			// base surge
			DustGap += Dt;
			if (Tt < 3.f && DustGap > 0.08)
			{
				DustGap = 0;
				const double Rr = KmU * (0.6 + 6.0 * (1.0 - FMath::Exp(-Tt / 1.2)));
				for (int32 I = 0; I < 9; ++I)
				{
					const FVector Dd = Fx.Offset(Up, Rr / DmCoords::RadiusUU, R.F() * TwoPi);
					const FVector P = Dd * (Fx.SurfaceR(Dd) + 0.08 * KmU);
					FParticle Q = Puff(R, P, (P - G).GetSafeNormal() * (0.6 * KmU), R.Range(7.f, 11.f), float(0.25 * KmU), float(0.8 * KmU), WithA(DustAlb, 0.4f));
					Q.bGround = true; Q.Drag = 0.8f; Q.Param = 140;
					Fx.Emit(Q);
				}
			}
			// condensation ring and shock front
			if (Tt > 0.25f && Tt < 2.8f)
			{
				const float Pr = (Tt - 0.25f) / 2.55f;
				const double Rr = KmU * (1.0 + 5.5 * Pr);
				Fx.AddRing(Up, Rr / DmCoords::RadiusUU, 1.4 * KmU, float(0.4 * KmU), EShape::Smoke, Rgb(0.92f, 0.93f, 0.95f, 0.55f * (1.f - Pr)), true, 96, 0, 120, float(0.1 * KmU), 13);
			}
			if (Tt < 4.f)
			{
				const double Rr = KmU * (0.5 + 12.0 * (1.0 - FMath::Exp(-Tt / 1.6)));
				// soft shock front (verifier fix: the razor-cored 9-unit ring read as a UI circle)
				Fx.AddRing(Up, Rr / DmCoords::RadiusUU, 0.12 * KmU, float(0.2 * KmU + Rr * 0.03), EShape::Bolt, Scale(Rgb(0.95f, 0.96f, 1.f), 3.f * FMath::Exp(-Tt / 0.7f) * (1.f - Smooth01((Tt - 1.2f) / 1.2f))), false,
					FMath::Clamp(int32(TwoPi * Rr / (0.5 * KmU)), 32, 240), 0, 215);
			}
			return Tt < 36.f;
		}
	};

	// ================================================================ grace

	struct FBlessing : FDmFxEffect
	{
		int32 Cell = -1, BKind = 0;
		bool Tick(ADmFx& Fx, float Dt) override
		{
			if (!Fx.W() || Cell < 0) return false;
			const FVector Up = Fx.CellDir(Cell);
			const FVector G = Fx.CellSurface(Cell, 0.02 * KmU);
			const FLinearColor Col = BKind == 1 ? Rgb(0.45f, 0.72f, 1.f) : BKind == 0 ? Rgb(1.f, 0.74f, 0.36f) : Rgb(1.f, 0.66f, 0.28f);
			const float E = Env(T, 0.7f, 2.2f, 1.6f);
			const double Top = 7.0 * KmU;
			const double Bottom = Top * (1.0 - Smooth01(T / 0.7f));
			Fx.AddAxis(G + Up * Bottom, G + Up * Top, float(0.32 * KmU), EShape::Beam, Scale(Col, 3.2f * E), false, 3, 0);
			Fx.AddAxis(G + Up * Bottom, G + Up * Top, float(1.2 * KmU), EShape::Beam, Scale(Col, 0.9f * E), false, 11, 0);
			const float Rr = float(KmU * (0.3 + 2.4 * Smooth01((T - 0.5f) / 2.2f)));
			// a soft spreading halo on the ground (verifier fix: the thin 3-unit ring read as a neon UI marker)
			if (T > 0.5f) Fx.AddFlat(G + Up * (0.03 * KmU), Rr, Rr, 0.f, EShape::Ring, Scale(Col, 1.3f * E * (1.f - Smooth01((T - 0.5f) / 3.f))), false, 5, 100, float(0.05 * KmU));
			Fx.AddFlat(G + Up * (0.02 * KmU), float(1.4 * KmU), float(1.4 * KmU), 0.f, EShape::Disc, Scale(Col, 0.8f * E), false, 9, 60, float(0.05 * KmU));
			Fx.AddLight(G + Up * (0.5 * KmU), Col, 10.f * E, float(4.0 * KmU));
			FRand& R = Fx.R();
			if (T > 0.5f && T < 3.5f)
				for (int32 I = 0, N = Count(R, 34.0, Dt); I < N; ++I)
				{
					const double Az = R.F() * TwoPi;
					const double Rad = KmU * R.Range(0.2f, 1.3f);
					const FVector H = Horiz(Up, Az);
					const FVector Swirl = Horiz(Up, Az + 1.5708) * (KmU * (BKind == 1 ? 0.45 : 0.15));
					Fx.Emit(MoteP(R, G + H * Rad + Up * (KmU * 0.3 * R.F()), Up * (KmU * R.Range(0.3f, 0.8f)) + Swirl, R.Range(2.5f, 4.f), float(KmU * R.Range(0.04f, 0.07f)), Scale(Col, R.Range(3.f, 6.f))));
				}
			return T < 4.8f;
		}
	};

	struct FProphet : FDmFxEffect
	{
		int32 Cell = -1;
		bool Tick(ADmFx& Fx, float Dt) override
		{
			if (!Fx.W() || Cell < 0) return false;
			const FVector Up = Fx.CellDir(Cell);
			const FVector C = Fx.CellSurface(Cell, 0.5 * KmU);
			const float E = Env(T, 0.8f, 2.4f, 1.8f);
			const FLinearColor Col = Rgb(1.f, 0.9f, 0.68f);
			Fx.AddFacing(C, float(KmU * (1.2 + 0.15 * FMath::Sin(T * 3.f))), EShape::Glow, Scale(Col, 6.f * E));
			const FVector Rt = Fx.CamRightPlanet(), Uq = Fx.CamUpPlanet();
			Fx.AddFacing(C, float(KmU * 3.0), EShape::Glow, Scale(Col, 0.8f * E));
			for (int32 K = 0; K < 8; ++K)
			{
				const double A = K * TwoPi / 8.0 + T * 0.15 + 0.2;
				const double L = KmU * (2.2 + 1.4 * FMath::Sin(K * 2.7 + T * 1.3));
				const FVector D = Rt * FMath::Cos(A) + Uq * FMath::Sin(A);
				Fx.AddAxis(C, C + D * (L * 0.75), float(0.3 * KmU), EShape::Beam, Scale(Col, 0.9f * E), false, uint8(K * 19), 0);   // verifier: shorter, dimmer (read as a lens-flare star)
			}
			Fx.AddLight(C, Col, 9.f * E, float(4.0 * KmU));
			FRand& R = Fx.R();
			for (int32 I = 0, N = Count(R, 16.0 * E, Dt); I < N; ++I)
				Fx.Emit(MoteP(R, C + R.Dir() * (0.8 * KmU), Up * (0.35 * KmU), R.Range(2.f, 3.5f), float(0.05 * KmU), Scale(Col, 4.f)));
			return T < 5.2f;
		}
	};

	struct FPeace : FDmFxEffect
	{
		int32 Cell = -1;
		double Ang = 0.22;
		bool bStarted = false;
		bool Tick(ADmFx& Fx, float Dt) override
		{
			if (!Fx.W() || Cell < 0) return false;
			const FVector Up = Fx.CellDir(Cell);
			const FVector G = Fx.CellSurface(Cell, 0.05 * KmU);
			FRand& R = Fx.R();
			if (!bStarted)
			{
				bStarted = true;
				for (int32 I = 0; I < 22; ++I)
				{
					FParticle P;
					const FVector H = RandHoriz(R, Up);
					P.P = G + Up * (0.2 * KmU); P.V = H * (KmU * R.Range(0.25f, 0.45f)) + Up * (KmU * R.Range(0.08f, 0.2f));
					P.Life = R.Range(6.f, 8.f); P.Size0 = P.Size1 = float(0.035 * KmU); P.Shape = EShape::Dot; P.bLit = true;
					P.C0 = P.C1 = Rgb(0.92f, 0.93f, 0.95f, 1.f); P.FadeIn = 0.05f; P.FadeOut = 0.3f; P.Seed = R.Byte();
					Fx.Emit(P);
				}
			}
			const float E = Env(T, 0.5f, 3.f, 2.5f);
			const float Pr = Smooth01(T / 5.f);
			const double Rr = Ang * FMath::Pow(double(Pr), 0.6);
			if (Rr > 0.002)
			{
				const int32 Segs = FMath::Clamp(int32(TwoPi * Rr * DmCoords::RadiusKm / 0.8), 24, 260);
				Fx.AddRing(Up, Rr, 0.3 * KmU, float(KmU * (0.5 + 3.0 * Rr)), EShape::Bolt, Scale(Rgb(0.72f, 0.86f, 1.f), 1.1f * E * (1.f - 0.6f * Pr)), false, Segs, 0, 250);   // verifier: halved, softer (read as neon tubes)
				Fx.AddRing(Up, Rr * 0.82, 0.3 * KmU, float(KmU * (0.4 + 2.0 * Rr)), EShape::Bolt, Scale(Rgb(1.f, 0.88f, 0.62f), 0.6f * E * (1.f - 0.6f * Pr)), false, Segs, 0, 250);
			}
			Fx.AddFacing(G + Up * (2.5 * KmU), float(7.0 * KmU), EShape::Glow, Scale(Rgb(0.7f, 0.85f, 1.f), 0.5f * E));
			Fx.AddLight(G + Up * (1.0 * KmU), Rgb(0.85f, 0.92f, 1.f), 7.f * E, float(6.0 * KmU));
			if (T < 3.f)
				for (int32 I = 0, N = Count(R, 28.0, Dt); I < N; ++I)
					Fx.Emit(MoteP(R, G + RandHoriz(R, Up) * (KmU * 2.0 * R.F()), Up * (KmU * R.Range(0.2f, 0.5f)), R.Range(2.5f, 4.f), float(0.05 * KmU), Rgb(2.2f, 2.8f, 3.6f)));
			return T < 7.f;
		}
	};

	struct FGreenWave : FDmFxEffect
	{
		int32 Cell = -1;
		double Ang = 0.08;
		float Strength = 1.f;
		bool bSeed = false;
		bool Tick(ADmFx& Fx, float Dt) override
		{
			if (!Fx.W() || Cell < 0) return false;
			const float Dur = bSeed ? 3.5f : 2.2f;
			const FVector Up = Fx.CellDir(Cell);
			const FVector G = Fx.CellSurface(Cell, 0.03 * KmU);
			const float Pr = FMath::Clamp(T / Dur, 0.f, 1.f);
			const float Fade = 1.f - Smooth01((T - Dur * 0.6f) / (Dur * 0.5f));
			const double Rr = Ang * FMath::Pow(double(Pr), 0.7);
			FRand& R = Fx.R();
			if (Rr > 0.002)
			{
				const int32 Segs = FMath::Clamp(int32(TwoPi * Rr * DmCoords::RadiusKm / 0.6), 24, 220);
				// verifier fix: at 1.1 units (and several at once under a held brush) the wave read as neon target rings;
				// now a fainter, wider band of green light, the motes carry the sparkle
				Fx.AddRing(Up, Rr, 0.05 * KmU, float(KmU * (0.8 + 10.0 * Rr)), EShape::Bolt, Scale(Rgb(0.42f, 1.f, 0.30f), 0.45f * Strength * Fade), false, Segs, 2, 252);
				Fx.AddRing(Up, Rr * 0.85, 0.05 * KmU, float(KmU * (0.3 + 4.0 * Rr)), EShape::Bolt, Scale(Rgb(1.f, 0.9f, 0.45f), 0.2f * Strength * Fade), false, Segs, 2, 252);
			}
			if (bSeed && T < 1.2f)
			{
				Fx.AddFacing(G + Up * (0.3 * KmU), float(KmU * (1.0 + 2.0 * T)), EShape::Glow, Scale(Rgb(0.5f, 1.f, 0.45f), 4.f * (1.f - T / 1.2f)));
				Fx.AddLight(G + Up * (0.5 * KmU), Rgb(0.6f, 1.f, 0.5f), 8.f * (1.f - T / 1.2f), float(5.0 * KmU));
			}
			if (T < Dur)
				for (int32 I = 0, N = Count(R, 46.0 * Strength, Dt); I < N; ++I)
				{
					const FVector Dd = Fx.Offset(Up, Rr * FMath::Sqrt(R.F()), R.F() * TwoPi);
					if (Fx.IsSeaCell(Fx.CellAt(Dd))) continue;
					Fx.Emit(MoteP(R, Dd * (Fx.SurfaceR(Dd) + 0.04 * KmU), Dd * (KmU * R.Range(0.15f, 0.45f)), R.Range(1.8f, 3.f), float(0.045 * KmU),
						R.F() < 0.7f ? Rgb(1.4f, 3.2f, 0.9f) : Rgb(3.2f, 2.8f, 1.0f)));
				}
			return T < Dur + 0.6f;
		}
	};

	struct FRain : FDmFxEffect
	{
		int32 Cell = -1;
		double Ang = 0.1;
		float Seconds = 1.6f;
		bool Tick(ADmFx& Fx, float Dt) override
		{
			if (!Fx.W() || Cell < 0) return false;
			const FVector Up = Fx.CellDir(Cell);
			FRand& R = Fx.R();
			for (int32 I = 0, N = Count(R, 11.0, Dt); I < N; ++I)
			{
				const FVector Dd = Fx.Offset(Up, Ang * 0.8 * FMath::Sqrt(R.F()), R.F() * TwoPi);
				FParticle P;
				P.P = Dd * Fx.SurfaceR(Dd);
				P.Shape = EShape::Rain; P.Orient = EOrient::Upright; P.bLit = true;
				P.Size0 = P.Size1 = float(KmU * R.Range(0.6f, 1.2f));
				P.Aspect = float(0.85 * KmU) / P.Size0;
				P.Life = R.Range(1.4f, 2.2f); P.FadeIn = 0.25f; P.FadeOut = 0.4f;
				P.C0 = P.C1 = Rgb(0.30f, 0.32f, 0.35f, 0.75f);
				P.Seed = R.Byte();
				Fx.Emit(P);
			}
			for (int32 I = 0, N = Count(R, 4.0, Dt); I < N; ++I)
			{
				const FVector Dd = Fx.Offset(Up, Ang * 0.9 * FMath::Sqrt(R.F()), R.F() * TwoPi);
				FParticle P = Puff(R, Dd * (Fx.SurfaceR(Dd) + KmU * R.Range(1.6f, 2.1f)), FVector::ZeroVector, R.Range(3.5f, 5.f), float(1.1 * KmU), float(2.4 * KmU), WithA(Rgb(0.15f, 0.155f, 0.17f), 0.78f));
				P.Wind = 0.3f; P.Param = 50; P.FadeIn = 0.2f;
				Fx.Emit(P);
			}
			return T < Seconds;
		}
	};

	struct FAgeRays : FDmFxEffect
	{
		int32 Cell = -1;
		FLinearColor Tint = FLinearColor::White;
		bool Tick(ADmFx& Fx, float Dt) override
		{
			if (!Fx.W() || Cell < 0) return false;
			const FVector Up = Fx.CellDir(Cell);
			const FVector G = Fx.CellSurface(Cell, 0.05 * KmU);
			const float E = Env(T, 0.8f, 1.8f, 1.6f);
			const FLinearColor Col = FMath::Lerp(Rgb(1.f, 0.85f, 0.55f), Tint, 0.35f);
			for (int32 K = 0; K < 7; ++K)
			{
				const FVector Off = Horiz(Up, K * TwoPi / 7.0 + 0.4) * (KmU * (1.0 + 0.8 * FMath::Sin(K * 1.7)));
				Fx.AddAxis(G, G + Up * (8.0 * KmU) + Off, float(0.22 * KmU), EShape::Beam, Scale(Col, 3.5f * E), false, uint8(K * 31), 0);
			}
			Fx.AddFlat(G, float(1.2 * KmU), float(1.2 * KmU), 0.f, EShape::Disc, Scale(Col, 1.2f * E), false, 3, 40, float(0.05 * KmU));
			Fx.AddLight(G + Up * (0.6 * KmU), Col, 8.f * E, float(4.0 * KmU));
			FRand& R = Fx.R();
			for (int32 I = 0, N = Count(R, 20.0 * E, Dt); I < N; ++I)
				Fx.Emit(MoteP(R, G + RandHoriz(R, Up) * (KmU * R.F()), Up * (KmU * R.Range(0.4f, 0.9f)), R.Range(2.f, 3.f), float(0.05 * KmU), Scale(Col, 4.f)));
			return T < 4.3f;
		}
	};

	struct FArk : FDmFxEffect
	{
		int32 Cell = -1;
		FVector Up = FVector::UpVector, Pad = FVector::ZeroVector;
		bool bStarted = false;
		double SmokeGap = 0;
		bool Tick(ADmFx& Fx, float Dt) override
		{
			if (!Fx.W() || Cell < 0) return false;
			FRand& R = Fx.R();
			if (!bStarted) { bStarted = true; Up = Fx.CellDir(Cell); Pad = Fx.CellSurface(Cell, 0.05 * KmU); Fx.Shake(0.3f, Pad, 80.0 * KmU); }
			const double Acc = 0.28 * KmU;
			const double H = 0.5 * Acc * double(T) * T;
			const double Sp = Acc * T;
			const FVector Rocket = Pad + Up * H;
			const float Ign = Smooth01(T / 0.5f);
			Fx.AddFacing(Rocket, float(0.16 * KmU), EShape::Glow, Scale(Rgb(1.f, 0.88f, 0.65f), 30.f * Ign));
			Fx.AddAxis(Rocket, Rocket - Up * (0.5 * KmU + Sp * 0.12), float(0.08 * KmU), EShape::Beam, Scale(Rgb(1.f, 0.7f, 0.35f), 12.f * Ign), false, 1, 0);
			Fx.AddLight(Rocket, Rgb(1.f, 0.75f, 0.45f), 14.f * Ign * FMath::Clamp(1.f - float(H / (30.0 * KmU)), 0.f, 1.f), float(4.0 * KmU));
			SmokeGap += Dt;
			if (SmokeGap > 0.03 && H < 14.0 * KmU)
			{
				SmokeGap = 0;
				FParticle P = Puff(R, Rocket - Up * (0.3 * KmU), R.Dir() * (0.04 * KmU), R.Range(18.f, 26.f), float(0.12 * KmU), float(KmU * R.Range(0.5f, 0.8f)), WithA(SteamAlb, 0.6f));
				P.Wind = 0.8f; P.Drag = 0.6f; P.Param = 60;
				Fx.Emit(P);
			}
			if (T < 3.f)
				for (int32 I = 0, N = Count(R, 30.0, Dt); I < N; ++I)
				{
					FParticle P = Puff(R, Pad + RandHoriz(R, Up) * (0.2 * KmU), RandHoriz(R, Up) * (KmU * R.Range(0.4f, 0.9f)) + Up * (0.05 * KmU), R.Range(12.f, 18.f),
						float(0.25 * KmU), float(KmU * R.Range(0.8f, 1.3f)), WithA(SteamAlb, 0.7f));
					P.bGround = true; P.Drag = 0.7f;
					Fx.Emit(P);
				}
			return T < 32.f;
		}
	};
}

// ================================================================ factories

namespace DmFxFx
{
	FDmFxEffect* MakeFlight(ADmFx& Fx, int32 DisasterId, int32 Cell, bool bComet, float Eta, float Flight)
	{
		if (!Fx.W() || Cell < 0 || Cell >= Fx.W()->n) return nullptr;
		FFlight* E = new FFlight();
		E->Kind = bComet ? TEXT("comet") : TEXT("meteor");
		E->DisasterId = DisasterId;
		E->Cell = Cell;
		E->bComet = bComet;
		E->Eta = FMath::Max(0.05f, Eta);
		E->Flight = FMath::Max(0.1f, Flight);
		E->Target = Fx.CellSurface(Cell);
		E->Up = Fx.CellDir(Cell);
		// arrive from the upper left of the current view, 35 degrees above the horizon
		const FVector U = E->Up;
		FVector Rt = Fx.CamRightPlanet() - U * FVector::DotProduct(Fx.CamRightPlanet(), U);
		FVector Uq = Fx.CamUpPlanet() - U * FVector::DotProduct(Fx.CamUpPlanet(), U);
		Rt = Rt.GetSafeNormal(); Uq = Uq.GetSafeNormal();
		FVector Start = (-Rt * 0.8 + Uq * 0.55).GetSafeNormal();
		if (Start.IsNearlyZero()) Start = Horiz(U, 0.7);
		const double Th = FMath::DegreesToRadians(35.0);
		E->Dir = (U * FMath::Sin(Th) + Start * FMath::Cos(Th)).GetSafeNormal();
		return E;
	}

	FDmFxEffect* MakeImpact(ADmFx& Fx, int32 Cell, double Ang, bool bComet, bool bSea)
	{
		if (!Fx.W() || Cell < 0 || Cell >= Fx.W()->n) return nullptr;
		FImpact* E = new FImpact();
		E->Kind = TEXT("impact");
		E->Cell = Cell;
		E->Rc = FMath::Clamp(Ang, 0.01, 0.2) * DmCoords::RadiusUU;
		E->bComet = bComet;
		E->bSea = bSea;
		return E;
	}

	FDmFxEffect* MakeLightning(ADmFx&, const FVector& Ground, float Strength, bool bVisualOnly)
	{
		if (Ground.IsNearlyZero()) return nullptr;
		FLightning* E = new FLightning();
		E->Kind = TEXT("lightning");
		E->Ground = Ground;
		E->Strength = Strength;
		E->bVisualOnly = bVisualOnly;
		return E;
	}

	FDmFxEffect* MakeVolcano(ADmFx&, int32 DisasterId, int32 Cell)
	{
		FVolcano* E = new FVolcano();
		E->Kind = TEXT("volcano");
		E->DisasterId = DisasterId;
		E->Cell = Cell;
		return E;
	}

	FDmFxEffect* MakeTsunami(ADmFx&, int32 DisasterId, int32 Cell)
	{
		FTsunami* E = new FTsunami();
		E->Kind = TEXT("tsunami");
		E->DisasterId = DisasterId;
		E->Cell = Cell;
		return E;
	}

	FDmFxEffect* MakeStorm(ADmFx&, int32 DisasterId)
	{
		FStorm* E = new FStorm();
		E->Kind = TEXT("storm");
		E->DisasterId = DisasterId;
		return E;
	}

	FDmFxEffect* MakeDrought(ADmFx&, int32 DisasterId)
	{
		FDrought* E = new FDrought();
		E->Kind = TEXT("drought");
		E->DisasterId = DisasterId;
		return E;
	}

	FDmFxEffect* MakeQuake(ADmFx& Fx, int32 Cell, double Ang, float Power)
	{
		if (!Fx.W() || Cell < 0 || Cell >= Fx.W()->n) return nullptr;
		FQuake* E = new FQuake();
		E->Kind = TEXT("quake");
		E->Cell = Cell;
		E->Ang = FMath::Clamp(Ang, 0.02, 0.4);
		E->Power = FMath::Clamp(Power, 0.2f, 1.5f);
		return E;
	}

	FDmFxEffect* MakeNuke(ADmFx& Fx, int32 Cell)
	{
		if (!Fx.W() || Cell < 0 || Cell >= Fx.W()->n) return nullptr;
		FNuke* E = new FNuke();
		E->Kind = TEXT("nuke");
		E->Cell = Cell;
		return E;
	}

	FDmFxEffect* MakeBlessing(ADmFx&, int32 Cell, int32 Kind)
	{
		if (Cell < 0) return nullptr;
		FBlessing* E = new FBlessing();
		E->Kind = TEXT("blessing");
		E->Cell = Cell;
		E->BKind = Kind;
		return E;
	}

	FDmFxEffect* MakeProphet(ADmFx&, int32 Cell)
	{
		if (Cell < 0) return nullptr;
		FProphet* E = new FProphet();
		E->Kind = TEXT("prophet");
		E->Cell = Cell;
		return E;
	}

	FDmFxEffect* MakePeace(ADmFx&, int32 Cell, double Ang)
	{
		if (Cell < 0) return nullptr;
		FPeace* E = new FPeace();
		E->Kind = TEXT("peace");
		E->Cell = Cell;
		E->Ang = Ang;
		return E;
	}

	FDmFxEffect* MakeGreenWave(ADmFx&, int32 Cell, double Ang, float Strength, bool bSeed)
	{
		if (Cell < 0) return nullptr;
		FGreenWave* E = new FGreenWave();
		E->Kind = bSeed ? TEXT("seed") : TEXT("bloom");
		E->Cell = Cell;
		E->Ang = Ang;
		E->Strength = Strength;
		E->bSeed = bSeed;
		return E;
	}

	FDmFxEffect* MakeRain(ADmFx&, int32 Cell, double Ang, float Seconds)
	{
		if (Cell < 0) return nullptr;
		FRain* E = new FRain();
		E->Kind = TEXT("rain");
		E->Cell = Cell;
		E->Ang = Ang;
		E->Seconds = Seconds;
		return E;
	}

	FDmFxEffect* MakeAgeRays(ADmFx&, int32 Cell, const FLinearColor& Tint)
	{
		if (Cell < 0) return nullptr;
		FAgeRays* E = new FAgeRays();
		E->Kind = TEXT("age");
		E->Cell = Cell;
		E->Tint = Tint;
		return E;
	}

	FDmFxEffect* MakeArk(ADmFx&, int32 Cell)
	{
		if (Cell < 0) return nullptr;
		FArk* E = new FArk();
		E->Kind = TEXT("ark");
		E->Cell = Cell;
		return E;
	}

	// ------------------------------------------------------------ bursts

	void BurstPlague(ADmFx& Fx, int32 Cell, float Strength)
	{
		if (!Fx.W() || Cell < 0) return;
		FRand& R = Fx.R();
		const FVector Up = Fx.CellDir(Cell);
		const FVector G = Fx.CellSurface(Cell, 0.05 * KmU);
		for (int32 I = 0; I < int32(16 * Strength); ++I)
		{
			FParticle P = Puff(R, G + RandHoriz(R, Up) * (KmU * 0.6 * R.F()), Up * (0.08 * KmU) + RandHoriz(R, Up) * (0.1 * KmU), R.Range(8.f, 12.f),
				float(0.3 * KmU), float(KmU * R.Range(1.0f, 1.4f)), WithA(Rgb(0.38f, 0.44f, 0.16f), 0.42f));
			P.Wind = 0.2f; P.Param = 150;
			Fx.Emit(P);
		}
		Fx.Emit(GlowP(R, G + Up * (0.2 * KmU), FVector::ZeroVector, 3.f, float(0.6 * KmU), float(1.4 * KmU), Rgb(0.45f, 1.f, 0.2f) * 1.5f, Rgb(0.3f, 0.6f, 0.1f) * 0.2f));
	}

	void BurstSculpt(ADmFx& Fx, int32 Cell, double Ang, bool bRaise)
	{
		if (!Fx.W() || Cell < 0) return;
		FRand& R = Fx.R();
		const FVector Up = Fx.CellDir(Cell);
		for (int32 I = 0; I < 5; ++I)
		{
			const FVector Dd = Fx.Offset(Up, Ang * 0.6 * FMath::Sqrt(R.F()), R.F() * TwoPi);
			const FVector P = Dd * (Fx.SurfaceR(Dd) + 0.05 * KmU);
			if (Fx.IsSeaCell(Fx.CellAt(Dd)))
			{
				Fx.Emit(Puff(R, P, Dd * (0.2 * KmU), R.Range(3.f, 5.f), float(0.15 * KmU), float(0.5 * KmU), WithA(SteamAlb, 0.35f)));
				continue;
			}
			FParticle Q = Puff(R, P, Dd * (KmU * (bRaise ? 0.25 : 0.08)), R.Range(4.f, 7.f), float(0.15 * KmU), float(KmU * R.Range(0.5f, 0.8f)), WithA(DustAlb, 0.45f));
			Q.bGround = true; Q.Drag = 0.8f;
			Fx.Emit(Q);
		}
	}

	void BurstFounded(ADmFx& Fx, int32 Cell, bool bAwaken)
	{
		if (!Fx.W() || Cell < 0) return;
		FRand& R = Fx.R();
		const FVector Up = Fx.CellDir(Cell);
		const FVector G = Fx.CellSurface(Cell, 0.05 * KmU);
		const int32 N = bAwaken ? 60 : 22;
		for (int32 I = 0; I < N; ++I)
		{
			const FVector V = (Up * R.Range(0.6f, 1.2f) + RandHoriz(R, Up) * 0.7).GetSafeNormal() * (KmU * R.Range(0.2f, bAwaken ? 0.8f : 0.45f));
			Fx.Emit(MoteP(R, G, V, R.Range(1.5f, 3.f), float(0.04 * KmU), Rgb(3.2f, 2.5f, 1.2f) * R.Range(0.7f, 1.3f)));
		}
		Fx.Emit(GlowP(R, G + Up * (0.2 * KmU), FVector::ZeroVector, bAwaken ? 2.5f : 1.2f, float(0.5 * KmU), float(KmU * (bAwaken ? 2.0 : 1.0)), Rgb(1.f, 0.85f, 0.55f) * (bAwaken ? 5.f : 2.5f), Rgb(1.f, 0.7f, 0.4f) * 0.1f));
	}

	void BurstBattle(ADmFx& Fx, int32 Cell, bool bWon)
	{
		if (!Fx.W() || Cell < 0) return;
		FRand& R = Fx.R();
		const FVector Up = Fx.CellDir(Cell);
		const FVector G = Fx.CellSurface(Cell, 0.02 * KmU);
		for (int32 I = 0; I < 7; ++I)
		{
			FParticle P = Puff(R, G + RandHoriz(R, Up) * (KmU * 0.5 * R.F()), Up * (0.1 * KmU), R.Range(6.f, 10.f), float(0.12 * KmU), float(0.5 * KmU), WithA(Rgb(0.3f, 0.27f, 0.23f), 0.5f));
			P.Wind = 0.6f;
			Fx.Emit(P);
		}
		for (int32 I = 0; I < 12; ++I)
		{
			FParticle F = GlowP(R, G + RandHoriz(R, Up) * (KmU * 0.5 * R.F()) + Up * (0.03 * KmU), FVector::ZeroVector, R.Range(0.12f, 0.3f), float(0.08 * KmU), float(0.12 * KmU),
				Rgb(1.f, 0.6f, 0.25f) * 8.f, Rgb(1.f, 0.4f, 0.1f) * 0.5f);
			F.Age = -R.Range(0.f, 1.5f);   // staggered: a skirmish, not one flash
			F.FadeIn = 0.02f;
			Fx.Emit(F);
		}
		if (bWon)
			for (int32 I = 0; I < 3; ++I)
			{
				FParticle Fl;
				Fl.P = G + RandHoriz(R, Up) * (KmU * 0.3 * R.F()); Fl.Life = R.Range(3.f, 5.f);
				Fl.Size0 = Fl.Size1 = float(0.08 * KmU); Fl.Aspect = 2.f; Fl.Shape = EShape::Flame; Fl.Orient = EOrient::Upright;
				Fl.C0 = Fl.C1 = Rgb(1.f, 0.45f, 0.12f) * 3.f; Fl.Flicker = 0.4f; Fl.FadeIn = 0.1f; Fl.FadeOut = 0.4f; Fl.Seed = R.Byte();
				Fx.Emit(Fl);
			}
	}

	void BurstDestroyed(ADmFx& Fx, int32 Cell, int32 Cause)
	{
		if (!Fx.W() || Cell < 0) return;
		const dm::Cause C = dm::Cause(Cause);
		if (C == dm::Cause::Meteor || C == dm::Cause::Lava || C == dm::Cause::Nuke) return;   // their own effects cover it
		FRand& R = Fx.R();
		const FVector Up = Fx.CellDir(Cell);
		const FVector G = Fx.CellSurface(Cell, 0.03 * KmU);
		if (C == dm::Cause::Drowned || C == dm::Cause::Wave) { BurstSplash(Fx, Cell, 1.f); return; }
		const bool bBurn = C == dm::Cause::War || C == dm::Cause::Lightning || C == dm::Cause::Quake;
		for (int32 I = 0; I < (bBurn ? 14 : 4); ++I)
		{
			FParticle P = Puff(R, G + RandHoriz(R, Up) * (KmU * 0.4 * R.F()), Up * (KmU * R.Range(0.3f, 0.6f)), R.Range(16.f, 24.f), float(0.2 * KmU), float(KmU * R.Range(0.9f, 1.4f)), WithA(SmokeAlb, bBurn ? 0.7f : 0.35f));
			P.Wind = 0.9f; P.Drag = 0.35f;
			Fx.Emit(P);
		}
		if (bBurn)
			for (int32 I = 0; I < 10; ++I)
			{
				FParticle Fl;
				Fl.P = G + RandHoriz(R, Up) * (KmU * 0.45 * R.F()); Fl.Life = R.Range(4.f, 8.f);
				Fl.Size0 = Fl.Size1 = float(KmU * R.Range(0.08f, 0.16f)); Fl.Aspect = 2.2f; Fl.Shape = EShape::Flame; Fl.Orient = EOrient::Upright;
				Fl.C0 = Fl.C1 = Rgb(1.f, 0.45f, 0.12f) * 3.5f; Fl.Flicker = 0.4f; Fl.FadeIn = 0.05f; Fl.FadeOut = 0.5f; Fl.Seed = R.Byte();
				Fx.Emit(Fl);
			}
	}

	void BurstSplash(ADmFx& Fx, int32 Cell, float Height)
	{
		if (!Fx.W() || Cell < 0) return;
		FRand& R = Fx.R();
		const FVector Up = Fx.CellDir(Cell);
		const FVector G = Fx.CellSurface(Cell, 0.02 * KmU);
		const float H = FMath::Clamp(Height, 0.2f, 3.f);
		for (int32 I = 0; I < 40; ++I)
		{
			FParticle P;
			P.P = G + RandHoriz(R, Up) * (KmU * 0.8 * R.F());
			P.V = (Up * R.Range(1.f, 2.f) + RandHoriz(R, Up) * 0.6).GetSafeNormal() * (KmU * R.Range(0.4f, 0.9f) * H);
			P.Life = R.Range(1.f, 1.8f); P.Size0 = float(0.05 * KmU); P.Size1 = float(0.09 * KmU);
			P.Shape = EShape::Dot; P.Orient = EOrient::Velocity; P.Aspect = 1.5f; P.Stretch = 0.04f; P.bLit = true; P.Rise = float(-2.5 * KmU);
			P.C0 = P.C1 = Rgb(0.82f, 0.86f, 0.9f, 0.85f); P.FadeIn = 0.01f; P.FadeOut = 0.3f; P.Seed = R.Byte();
			Fx.Emit(P);
		}
		for (int32 I = 0; I < 8; ++I)
			Fx.Emit(Puff(R, G + RandHoriz(R, Up) * (KmU * 0.6 * R.F()), Up * (0.15 * KmU), R.Range(4.f, 6.f), float(0.2 * KmU), float(0.8 * KmU), WithA(SteamAlb, 0.4f)));
	}

	void BurstFireStart(ADmFx& Fx, int32 Cell)
	{
		if (!Fx.W() || Cell < 0) return;
		FRand& R = Fx.R();
		const FVector Up = Fx.CellDir(Cell);
		const FVector G = Fx.CellSurface(Cell, 0.03 * KmU);
		Fx.Emit(GlowP(R, G + Up * (0.1 * KmU), FVector::ZeroVector, 1.2f, float(0.3 * KmU), float(0.6 * KmU), Rgb(1.f, 0.5f, 0.15f) * 6.f, Rgb(1.f, 0.3f, 0.1f) * 0.3f));
		for (int32 I = 0; I < 10; ++I)
			Fx.Emit(Spark(R, G, (Up + RandHoriz(R, Up) * 0.6).GetSafeNormal() * (KmU * R.Range(0.2f, 0.5f)), R.Range(0.8f, 1.5f), float(0.012 * KmU), float(0.5 * KmU), 1500.f, 900.f, 6.f));
	}

	void BurstWitness(ADmFx& Fx, int32 Cell, bool bGood)
	{
		if (!Fx.W() || Cell < 0) return;
		FRand& R = Fx.R();
		const FVector Up = Fx.CellDir(Cell);
		const FVector G = Fx.CellSurface(Cell, 0.05 * KmU);
		const FLinearColor C = bGood ? Rgb(3.2f, 2.5f, 1.1f) : Rgb(3.2f, 0.55f, 0.35f);
		for (int32 I = 0; I < 10; ++I)
			Fx.Emit(MoteP(R, G + RandHoriz(R, Up) * (KmU * 0.4 * R.F()), Up * (KmU * R.Range(0.3f, 0.6f)), R.Range(2.f, 3.5f), float(0.045 * KmU), C));
	}

	// ------------------------------------------------------------ ambient

	void AmbientFire(ADmFx& Fx, int32 Cell, float Fire, float Dt, float& Acc)
	{
		(void)Acc;
		FRand& R = Fx.R();
		const FVector Up = Fx.CellDir(Cell);
		const FVector G = Fx.CellSurface(Cell, 0.02 * KmU);
		const bool bNear = Fx.CamDist(G) < 140.0 * KmU;
		const float Heat = FMath::Clamp(Fire, 0.f, 1.f);        // visual heat: 1 burning .. 0 cold
		const float F = FMath::Clamp((Heat - 0.35f) / 0.65f, 0.f, 1.f);   // flames die first, smoke lingers
		const float Flick = 0.75f + 0.4f * FMath::PerlinNoise1D(float(Fx.Now()) * 5.f + Cell * 0.37f);
		const float NightF = 0.15f + 0.85f * (1.f - Fx.DayAt(G));
		Fx.AddFacing(G + Up * (0.08 * KmU), float(KmU * (0.3 + 0.5 * Heat)), EShape::Glow, Scale(Rgb(1.f, 0.42f, 0.12f), (0.6f + 5.f * F) * Flick * NightF));
		if (F > 0.f) Fx.AddLight(G + Up * (0.3 * KmU), Rgb(1.f, 0.45f, 0.15f), (1.f + 4.f * F) * Flick, float(2.2 * KmU));
		if (bNear && F > 0.f)
		{
			for (int32 I = 0, N = Count(R, 9.0 * F + 2.0, Dt); I < N; ++I)
			{
				FParticle Fl;
				Fl.P = G + RandHoriz(R, Up) * (KmU * 0.55 * FMath::Sqrt(R.F()));
				Fl.V = Up * (0.12 * KmU);
				Fl.Life = R.Range(0.6f, 1.1f);
				Fl.Size0 = float(KmU * R.Range(0.14f, 0.3f)); Fl.Size1 = Fl.Size0 * 0.6f;
				Fl.Aspect = R.Range(1.1f, 1.7f); Fl.Shape = EShape::Flame; Fl.Orient = EOrient::Upright;
				// verifier fix: at 2.2-3.6 units the tone mapper bleached close-up flames into white candle spikes (worse
					// at night under the exposure lift); 1.3-2.1 keeps them orange and still brighter than sunlit ground
					Fl.C0 = Rgb(1.f, 0.36f, 0.07f) * R.Range(1.3f, 2.1f); Fl.C1 = Rgb(0.9f, 0.18f, 0.03f) * 0.8f;
				Fl.Flicker = 0.35f; Fl.FadeIn = 0.12f; Fl.FadeOut = 0.5f; Fl.Seed = R.Byte();
				Fx.Emit(Fl);
			}
			for (int32 I = 0, N = Count(R, 3.0 * F + 0.5, Dt); I < N; ++I)
				Fx.Emit(Spark(R, G + RandHoriz(R, Up) * (KmU * 0.4 * R.F()), (Up * 2.0 + RandHoriz(R, Up)).GetSafeNormal() * (KmU * R.Range(0.2f, 0.45f)),
					R.Range(1.5f, 2.5f), float(0.01 * KmU), float(-0.04 * KmU), 1500.f, 850.f, 6.f));
		}
		for (int32 I = 0, N = Count(R, (bNear ? 1.6 : 0.45) * (0.3 + Heat), Dt); I < N; ++I)
		{
			FParticle P = Puff(R, G + RandHoriz(R, Up) * (KmU * 0.4 * R.F()) + Up * (0.15 * KmU), Up * (KmU * R.Range(0.4f, 0.6f)), R.Range(14.f, 22.f),
				float(0.3 * KmU), float(KmU * R.Range(1.3f, 1.9f)), WithA(SmokeAlb, 0.58f));
			P.Drag = 0.33f; P.Wind = 0.95f; P.Param = 70;
			Fx.Emit(P);
		}
	}

	void AmbientMiasma(ADmFx& Fx, int32 Cell, float Plague, float Dt, float& Acc)
	{
		(void)Acc;
		FRand& R = Fx.R();
		const FVector Up = Fx.CellDir(Cell);
		const FVector G = Fx.CellSurface(Cell, 0.05 * KmU);
		const float P01 = FMath::Clamp(Plague, 0.f, 1.f);
		for (int32 I = 0, N = Count(R, 2.5 * (0.4 + P01), Dt); I < N; ++I)
		{
			const double Az = R.F() * TwoPi;
			FParticle P = Puff(R, G + Horiz(Up, Az) * (KmU * 0.7 * R.F()), Up * (0.04 * KmU) + Horiz(Up, Az + 1.57) * (0.08 * KmU), R.Range(7.f, 10.f),
				float(0.3 * KmU), float(KmU * R.Range(0.9f, 1.3f)), WithA(Rgb(0.37f, 0.43f, 0.16f), 0.32f));
			P.Wind = 0.15f; P.Drag = 0.3f; P.Param = 170;
			Fx.Emit(P);
		}
		const float Night = 1.f - Fx.DayAt(G);
		if (Night > 0.05f) Fx.AddFacing(G + Up * (0.15 * KmU), float(0.9 * KmU), EShape::Glow, Scale(Rgb(0.25f, 0.6f, 0.12f), 0.9f * Night * (0.4f + 0.6f * P01)));
	}

	void AmbientPrayer(ADmFx& Fx, int32 Cell, int32 Kind, uint8 Seed)
	{
		const FVector Up = Fx.CellDir(Cell);
		const FVector G = Fx.CellSurface(Cell, 0.02 * KmU);
		const float Pulse = 0.8f + 0.2f * FMath::Sin(float(Fx.Now()) * 1.8f + Seed * 0.1f);
		const FLinearColor C = Kind == int32(dm::PrayerKind::Rain) ? Rgb(0.85f, 0.9f, 1.f) : Rgb(1.f, 0.88f, 0.6f);
		Fx.AddAxis(G, G + Up * (5.0 * KmU), float(0.05 * KmU), EShape::Beam, Scale(C, 8.f * Pulse), false, Seed, 0);
		Fx.AddAxis(G, G + Up * (4.0 * KmU), float(0.28 * KmU), EShape::Beam, Scale(C, 0.5f * Pulse), false, uint8(Seed + 1), 0);
		Fx.AddFacing(G + Up * (0.05 * KmU), float(0.25 * KmU), EShape::Glow, Scale(C, 3.f * Pulse));
	}

	void AmbientProphet(ADmFx& Fx, const FVector& Pos, float Dt, float& Acc, bool bWalking)
	{
		(void)Acc;
		const float Pulse = 0.85f + 0.15f * FMath::Sin(float(Fx.Now()) * 2.4f);
		const FLinearColor C = Rgb(1.f, 0.92f, 0.72f);
		Fx.AddFacing(Pos, float(KmU * (bWalking ? 0.22 : 0.6)), EShape::Glow, Scale(C, (bWalking ? 7.f : 2.6f) * Pulse));
		Fx.AddLight(Pos, C, bWalking ? 3.f : 2.f, float(1.5 * KmU));
		FRand& R = Fx.R();
		for (int32 I = 0, N = Count(R, bWalking ? 4.0 : 2.0, Dt); I < N; ++I)
			Fx.Emit(MoteP(R, Pos + R.Dir() * (0.15 * KmU), Pos.GetSafeNormal() * (0.2 * KmU), R.Range(1.5f, 2.5f), float(0.03 * KmU), Scale(C, 3.f)));
	}

	void AmbientBeacon(ADmFx& Fx, int32 Cell)
	{
		const FVector Up = Fx.CellDir(Cell);
		const FVector G = Fx.CellSurface(Cell, 0.05 * KmU);
		const float Ph = FMath::Fmod(float(Fx.Now()) / 2.f, 1.f);
		const FLinearColor C = Rgb(1.f, 0.92f, 0.7f);
		Fx.AddFacing(G + Up * (0.3 * KmU), float(1.0 * KmU), EShape::Glow, Scale(C, 2.5f + 1.2f * FMath::Sin(float(Fx.Now()) * 3.f)));
		Fx.AddAxis(G, G + Up * (6.0 * KmU), float(0.12 * KmU), EShape::Beam, Scale(C, 4.f), false, 2, 0);
		Fx.AddFlat(G, float(KmU * (0.5 + 3.0 * Ph)), float(KmU * (0.5 + 3.0 * Ph)), 0.f, EShape::Ring, Scale(C, 2.5f * (1.f - Ph)), false, 4, 25, float(0.05 * KmU));
	}

	void AmbientAurora(ADmFx& Fx, float Strength)
	{
		const FVector Sun = Fx.SunPlanet();
		const double T = Fx.Now();
		const double SeaR = Fx.W() ? DmCoords::HeightToRadiusUU(Fx.W()->seaLevel) : DmCoords::RadiusUU;
		const int32 Segs = 120;
		for (int32 Pole = 0; Pole < 2; ++Pole)
		{
			const double Sg = Pole == 0 ? 1.0 : -1.0;
			const FVector Axis = FVector(FMath::Sin(FMath::DegreesToRadians(8.0)), 0.0, FMath::Cos(FMath::DegreesToRadians(8.0))) * Sg;
			FVector E, N;
			ADmFx::TangentBasis(Axis, E, N);
			for (int32 Rb = 0; Rb < 2; ++Rb)
			{
				const double C0 = FMath::DegreesToRadians(Rb == 0 ? 19.0 : 23.0);
				const double Base = SeaR + KmU * (Rb == 0 ? 2.0 : 2.4);
				const double HalfH = KmU * (Rb == 0 ? 2.6 : 1.8);
				auto At = [&](int32 I) -> FVector
				{
					const double A = I * TwoPi / Segs;
					const double C = C0 + FMath::DegreesToRadians(2.6 * FMath::Sin(3.0 * A + T * 0.07 + Rb * 1.3) + 1.2 * FMath::Sin(7.0 * A - T * 0.11 + Pole));
					return (Axis * FMath::Cos(C) + (N * FMath::Cos(A) + E * FMath::Sin(A)) * FMath::Sin(C)).GetSafeNormal();
				};
				FVector Prev = At(0);
				for (int32 I = 0; I < Segs; ++I)
				{
					const FVector Next = At(I + 1);
					const FVector Mid = (Prev + Next).GetSafeNormal();
					const float Night = FMath::Clamp(float((-FVector::DotProduct(Mid, Sun) + 0.12) * 3.0), 0.f, 1.f);
					if (Night > 0.02f)
					{
						const FVector Ce = Mid * (Base + HalfH);
						const FVector U = (Next - Prev) * (Base * 0.5 * 1.06);
						const FVector V = Mid * HalfH;
						const float I8 = 3.4f * Strength * Night * (Rb == 0 ? 1.f : 0.65f);
						const float X0 = float(I) * 8.f / Segs, X1 = float(I + 1) * 8.f / Segs;
						Fx.AddPlanetQuad(Ce, U, V, EShape::Curtain, Rgb(I8, I8, I8), false, uint8(Pole * 7 + Rb * 3), 0, float(0.3 * KmU), 0.f, X0, X1);
					}
					Prev = Next;
				}
			}
		}
	}
}
