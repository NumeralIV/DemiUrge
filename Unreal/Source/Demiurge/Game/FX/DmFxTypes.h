// DEMIURGE — Game/FX/DmFxTypes.h : particle and quad types of the effects module (C6 fx).
// Owner: C6 fx.
//
// Everything the module draws is a quad = centre + two half-axes, a shape (the shader in
// Scripts/fx/fx.hlsl), an HDR colour and an opacity. Opacity 0 = purely additive (glow, fire, bolts);
// opacity > 0 = premultiplied "over" (smoke, ash, dust, rain), so glow and smoke share one sorted draw.
// Simulated particles live in the PLANET frame (they turn with the planet, so a plume stays on its
// volcano); the renderer turns them into camera-relative quads every frame.
#pragma once

#include "CoreMinimal.h"

namespace DmFx
{
	/** Shapes understood by the shader (Scripts/fx/fx.hlsl). */
	enum class EShape : uint8
	{
		Glow = 0,      // soft gaussian with a hot core
		Smoke = 1,     // evolving noise puff, lit (uses the per-vertex sun gradient)
		Streak = 2,    // along U: dim tail -> bright head (sparks, meteor trails)
		Ring = 3,      // annulus at the edge, width = Param
		Flame = 4,     // flame tongue, V = up
		Beam = 5,      // light column, V = along (fades toward +V)
		Bolt = 6,      // lightning segment, V = along
		Disc = 7,      // flat glowing pool, mottled by Param
		Mote = 8,      // sparkle with a twinkling glint
		Curtain = 9,   // aurora ribbon (U0.x continuous)
		Rain = 10,     // falling streak sheet
		Dot = 11,      // small hard disc (debris, cinders, birds)
		Lava = 12,     // lava channel: V along the flow, hot core under drifting crust (alpha darkens the ground)
	};

	/** How a particle's quad is oriented. */
	enum class EOrient : uint8
	{
		Facing,        // camera-facing, rotated by Rot
		Velocity,      // U along the screen-projected velocity (length = size + |v| x Stretch)
		Flat,          // lying on the local tangent plane (rings, pools)
		Upright,       // V along local up, U across toward the camera (flames, columns, rain)
		Axis,          // V along the particle's Axis vector (bolts, beams between two points)
	};

	/** A simulated particle (planet frame, uu, seconds). */
	struct FParticle
	{
		FVector P = FVector::ZeroVector;      // position
		FVector V = FVector::ZeroVector;      // velocity, uu/s
		FVector Axis = FVector::ZeroVector;   // EOrient::Axis: half vector along V
		float Age = 0.f, Life = 1.f;
		float Size0 = 1.f, Size1 = 1.f;       // half size in uu at birth / death
		float SizePow = 1.f;                  // growth curve exponent (0.5 = fast early growth)
		float Aspect = 1.f;                   // |V axis| / |U axis| for Upright / Velocity / Axis
		float Stretch = 0.f;                  // Velocity: seconds of velocity added to the length
		float Drag = 0.f;                     // velocity damping, 1/s
		float Rise = 0.f;                     // radial acceleration, uu/s^2 (+ buoyant, - gravity)
		float Wind = 0.f;                     // share of the local wind drift
		float Rot = 0.f, Spin = 0.f;          // radians, radians/s
		FLinearColor C0 = FLinearColor::White, C1 = FLinearColor::White;   // rgb (HDR) + A (opacity), over life
		float ColorPow = 1.f;                 // colour curve exponent
		float FadeIn = 0.08f, FadeOut = 0.35f;   // shares of life
		float Temp0 = 0.f, Temp1 = 0.f;       // > 0 with Glow: adds blackbody(T) x Glow x (T/Temp0)^4 (incandescent cooling)
		float Glow = 0.f;                     // incandescent intensity at Temp0
		float Flicker = 0.f;                  // random brightness flicker amplitude 0..1
		EShape Shape = EShape::Glow;
		EOrient Orient = EOrient::Facing;
		uint8 Seed = 0;
		uint8 Param = 0;                      // shape parameter 0..255
		bool bLit = false;                    // colour is an albedo lit by sun, sky and FX lights
		bool bGround = false;                 // stays at least on the ground (rolls along it)
	};

	/** One quad handed to the renderer (world space, planet already applied). */
	struct FQuad
	{
		FVector C = FVector::ZeroVector;      // centre (world)
		FVector U = FVector::ZeroVector;      // half axis 1 (world)
		FVector V = FVector::ZeroVector;      // half axis 2 (world)
		FLinearColor Col = FLinearColor::Black;   // rgb HDR (not premultiplied) + A opacity
		float Light[4] = { 0.5f, 0.5f, 0.5f, 0.5f };   // per-corner light 0..1 (x2 in the shader) for lit shapes
		float U0x[2] = { 0.f, 1.f };          // corner u range (curtain ribbons pass a continuous coordinate)
		float Age = 0.f;
		float Soft = -1.f;                    // depth-fade distance in uu (<= 0: 0.6 x the smaller half axis)
		uint8 Seed = 0, Param = 0;
		EShape Shape = EShape::Glow;
	};

	/** Small deterministic random stream for visuals (never the world's rng). */
	struct FRand
	{
		uint32 S = 0x9E3779B9u;
		explicit FRand(uint32 Seed = 1) : S(Seed ? Seed : 1u) {}
		uint32 Next() { S ^= S << 13; S ^= S >> 17; S ^= S << 5; return S; }
		float F() { return float(Next() >> 8) * (1.f / 16777216.f); }
		float Range(float A, float B) { return A + (B - A) * F(); }
		float Sym(float A = 1.f) { return (F() * 2.f - 1.f) * A; }
		uint8 Byte() { return uint8(Next() >> 24); }
		/** Unit vector, uniform on the sphere. */
		FVector Dir()
		{
			const float Z = Sym(1.f), A = F() * 6.2831853f, R = FMath::Sqrt(FMath::Max(0.f, 1.f - Z * Z));
			return FVector(R * FMath::Cos(A), R * FMath::Sin(A), Z);
		}
	};

	/** Linear-light colour of a black body at Kelvin (normalised so the brightest channel is 1). */
	inline FLinearColor Blackbody(float Kelvin)
	{
		const float T = FMath::Clamp(Kelvin, 800.f, 15000.f) / 100.f;
		float R, G, B;
		if (T <= 66.f) { R = 255.f; G = 99.4708025861f * FMath::Loge(T) - 161.1195681661f; }
		else { R = 329.698727446f * FMath::Pow(T - 60.f, -0.1332047592f); G = 288.1221695283f * FMath::Pow(T - 60.f, -0.0755148492f); }
		if (T >= 66.f) B = 255.f;
		else if (T <= 19.f) B = 0.f;
		else B = 138.5177312231f * FMath::Loge(T - 10.f) - 305.0447927307f;
		auto Lin = [](float V) { return FMath::Pow(FMath::Clamp(V / 255.f, 0.f, 1.f), 2.2f); };
		FLinearColor C(Lin(R), Lin(G), Lin(B), 1.f);
		const float Mx = FMath::Max3(C.R, C.G, C.B);
		if (Mx > 0.f) { C.R /= Mx; C.G /= Mx; C.B /= Mx; }
		return C;
	}

	inline float Smooth01(float X) { X = FMath::Clamp(X, 0.f, 1.f); return X * X * (3.f - 2.f * X); }
}
