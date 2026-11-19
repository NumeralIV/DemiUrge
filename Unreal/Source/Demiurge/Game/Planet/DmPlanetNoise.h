// DEMIURGE — Game/Planet/DmPlanetNoise.h : gradient noise with analytic derivatives and the two terrain
// fractals (port of UnrealLab/SurfaceLab SurfaceNoise.h). Owner: C1 planet.
//
// The same code exists in HLSL (Scripts/planet/common.hlsl). Keep them equivalent: the land material
// continues exactly the octaves the mesh of a given LOD level leaves out, so the shading and the relief
// agree at every level. Every function takes the UNREAL planet-frame direction (x, y, z with +Z north),
// because that is what the material sees (its local position); the sim's frame is only used to locate
// cells (DmPlanetTerrain.cpp).
#pragma once

#include "CoreMinimal.h"

namespace DmPlanetNoise
{
	inline uint32 Hash(int32 X, int32 Y, int32 Z)
	{
		uint32 H = (uint32(X) * 73856093u) ^ (uint32(Y) * 19349663u) ^ (uint32(Z) * 83492791u);
		H ^= H >> 13; H *= 0x5bd1e995u; H ^= H >> 15;
		return H;
	}

	inline FVector3d Grad(uint32 H)
	{
		H %= 12u;
		const double A = (H & 1u) ? -1.0 : 1.0, B = (H & 2u) ? -1.0 : 1.0;
		if (H < 4u) return FVector3d(A, B, 0.0);
		if (H < 8u) return FVector3d(A, 0.0, B);
		return FVector3d(0.0, A, B);
	}

	/** Value in X (about -1..1), derivative d/dp in Y, Z, W (iq's quintic gradient noise). */
	inline FVector4d Noised(const FVector3d& P)
	{
		const FVector3d I(FMath::FloorToDouble(P.X), FMath::FloorToDouble(P.Y), FMath::FloorToDouble(P.Z));
		const FVector3d F = P - I;
		const FVector3d U = F * F * F * (F * (F * 6.0 - 15.0) + 10.0);
		const FVector3d DU = 30.0 * F * F * (F * (F - 2.0) + 1.0);
		const int32 X = int32(I.X), Y = int32(I.Y), Z = int32(I.Z);
		const FVector3d Ga = Grad(Hash(X, Y, Z)), Gb = Grad(Hash(X + 1, Y, Z)), Gc = Grad(Hash(X, Y + 1, Z)), Gd = Grad(Hash(X + 1, Y + 1, Z));
		const FVector3d Ge = Grad(Hash(X, Y, Z + 1)), Gf = Grad(Hash(X + 1, Y, Z + 1)), Gg = Grad(Hash(X, Y + 1, Z + 1)), Gh = Grad(Hash(X + 1, Y + 1, Z + 1));
		const double Va = Ga | F, Vb = Gb | (F - FVector3d(1, 0, 0)), Vc = Gc | (F - FVector3d(0, 1, 0)), Vd = Gd | (F - FVector3d(1, 1, 0));
		const double Ve = Ge | (F - FVector3d(0, 0, 1)), Vf = Gf | (F - FVector3d(1, 0, 1)), Vg = Gg | (F - FVector3d(0, 1, 1)), Vh = Gh | (F - FVector3d(1, 1, 1));
		const double K1 = Vb - Va, K2 = Vc - Va, K3 = Ve - Va, K4 = Va - Vb - Vc + Vd, K5 = Va - Vc - Ve + Vg, K6 = Va - Vb - Ve + Vf, K7 = -Va + Vb + Vc - Vd + Ve - Vf - Vg + Vh;
		const double V = Va + U.X * K1 + U.Y * K2 + U.Z * K3 + U.X * U.Y * K4 + U.Y * U.Z * K5 + U.Z * U.X * K6 + U.X * U.Y * U.Z * K7;
		const FVector3d G = Ga + U.X * (Gb - Ga) + U.Y * (Gc - Ga) + U.Z * (Ge - Ga) + U.X * U.Y * (Ga - Gb - Gc + Gd) + U.Y * U.Z * (Ga - Gc - Ge + Gg)
			+ U.Z * U.X * (Ga - Gb - Ge + Gf) + U.X * U.Y * U.Z * (-Ga + Gb + Gc - Gd + Ge - Gf - Gg + Gh);
		const FVector3d D = G + DU * FVector3d(K1 + U.Y * K4 + U.Z * K6 + U.Y * U.Z * K7, K2 + U.Z * K5 + U.X * K4 + U.Z * U.X * K7, K3 + U.X * K6 + U.Y * K5 + U.X * U.Y * K7);
		return FVector4d(V, D.X, D.Y, D.Z);
	}

	/** Octave rotation (orthonormal), applied as p' = 2 M p. */
	inline FVector3d Rot(const FVector3d& P)
	{
		return FVector3d(-0.80 * P.Y - 0.60 * P.Z, 0.80 * P.X + 0.36 * P.Y - 0.48 * P.Z, 0.60 * P.X - 0.48 * P.Y + 0.64 * P.Z);
	}

	// Terrain fractal constants (shared with HLSL).
	inline constexpr double ReliefLambda0 = 6000.0;  // m, first relief octave
	inline constexpr int32 ReliefOctaves = 10;        // down to ~12 m
	inline constexpr int32 RidgeOctaves = 4;          // the ridged part stops at ~750 m (finer ridged octaves read as worms)
	inline constexpr double FineLambda0 = 16.0;       // m, first fine octave
	inline constexpr int32 FineOctaves = 5;           // down to 1 m (the material adds 0.5 m and 0.25 m)
	inline constexpr double DatumM = 100000.0;        // planet datum radius, m (noise is a function of direction only)

	inline double Sat(double X) { return FMath::Clamp(X, 0.0, 1.0); }
	inline double Smooth(double A, double B, double X) { const double T = Sat((X - A) / (B - A)); return T * T * (3.0 - 2.0 * T); }

	/** Weight of an octave of wavelength L in a mesh of vertex spacing S (m): 0 below 4 S, 1 above 8 S.
	 *  (Interpolated vertex normals render octaves with fewer than ~6 vertices per wavelength with less
	 *  contrast than the material does, which shows as rings at LOD borders: leave those to the material.) */
	inline double MeshWeight(double L, double S)
	{
		const double T = FMath::Clamp((L / S - 4.0) / 4.0, 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/** Relief fractal, two parts in one loop (the material repeats it exactly):
	 *  E: iq's eroded fbm, octaves damped where the slope accumulated so far is steep (smooth valleys);
	 *  R: ridged multifractal, 1-|n| squared, each octave weighted by the previous ridge (sharp crests).
	 *  Up = unit planet-frame direction (Unreal axes), S = mesh vertex spacing (m). */
	inline void Relief(const FVector3d& Up, double S, double& OutE, double& OutR)
	{
		FVector3d P = Up * (DatumM / ReliefLambda0);
		FVector3d D(0.0);
		FVector3d Q0(1, 0, 0), Q1(0, 1, 0), Q2(0, 0, 1);
		double AE = 0.0, AR = 0.0, B = 1.0, L = ReliefLambda0, Wr = 1.0;
		for (int32 I = 0; I < ReliefOctaves; ++I)
		{
			const double W = MeshWeight(L, S);
			if (W <= 0.0) break;
			const FVector4d N = Noised(P);
			const FVector3d GL(N.Y, N.Z, N.W);
			FVector3d G(Q0 | GL, Q1 | GL, Q2 | GL);
			G -= Up * (G | Up);
			D += G;
			const double Att = 1.0 / (1.0 + (D | D));
			AE += W * B * N.X * Att;
			if (I < RidgeOctaves)
			{
				const double An = FMath::Sqrt(N.X * N.X + 0.003) * 1.5;   // soft |n|: sharp but not razor-thin crests
				const double Rr = 1.0 - FMath::Min(An, 1.0);
				AR += W * B * Wr * (Rr * Rr - 0.35);
				Wr = FMath::Clamp(Rr * Rr * 1.5, 0.0, 1.0);
			}
			B *= 0.5; L *= 0.5;
			P = Rot(P) * 2.0;
			Q0 = Rot(Q0); Q1 = Rot(Q1); Q2 = Rot(Q2);
		}
		OutE = AE; OutR = AR;
	}

	/** Fine-fractal amplitude (m) from the two relief amplitudes (the material uses the same formula).
	 *  (+1 m in the denominator: with a clamp at 1e-3 the share of ridged relief jumped from its value to 0
	 *  where a river's flat valley floor took the relief to zero, a 1 m step that outlined every valley floor.) */
	inline double FineAmp(double RAe, double RAr) { return 0.3 + 1.5 * RAr / (RAe + RAr + 1.0); }

	/** Plain fine fractal (rocks, hummocks), about -1..1. */
	inline double Fine(const FVector3d& Up, double S)
	{
		FVector3d P = Up * (DatumM / FineLambda0) + FVector3d(17.31, 5.13, 9.71);
		double A = 0.0, B = 1.0, L = FineLambda0;
		for (int32 I = 0; I < FineOctaves; ++I)
		{
			const double W = MeshWeight(L, S);
			if (W <= 0.0) break;
			A += W * B * Noised(P).X;
			B *= 0.5; L *= 0.5;
			P = Rot(P) * 2.0;
		}
		return A;
	}

	/** Cheap normalised fbm (colour and climate wobble, no derivatives needed). */
	inline double Fbm(const FVector3d& P, int32 Oct)
	{
		double A = 0.0, B = 0.5, Nrm = 0.0; FVector3d Q = P;
		for (int32 I = 0; I < Oct; ++I) { A += B * Noised(Q).X; Nrm += B; B *= 0.5; Q = Rot(Q) * 2.0; }
		return A / FMath::Max(Nrm, 1e-9);
	}
}
