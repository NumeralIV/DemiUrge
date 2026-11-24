// DEMIURGE — Game/Audio/DmAudioAnalysis.cpp : measurements, WAV files and analysis panels. Owner: C9 audio.
#include "Game/Audio/DmAudioAnalysis.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include <cmath>

namespace
{
	constexpr double kPiD = 3.14159265358979323846;

	void Fft(float* Re, float* Im, int32 N)
	{
		for (int32 I = 1, J = 0; I < N; ++I)
		{
			int32 Bit = N >> 1;
			for (; J & Bit; Bit >>= 1) J ^= Bit;
			J ^= Bit;
			if (I < J) { Swap(Re[I], Re[J]); Swap(Im[I], Im[J]); }
		}
		for (int32 Len = 2; Len <= N; Len <<= 1)
		{
			const double Ang = -2.0 * kPiD / double(Len);
			const float Wr = float(std::cos(Ang)), Wi = float(std::sin(Ang));
			for (int32 I = 0; I < N; I += Len)
			{
				float Cr = 1.f, Ci = 0.f;
				for (int32 K = 0; K < Len / 2; ++K)
				{
					const int32 A = I + K, B = I + K + Len / 2;
					const float Tr = Re[B] * Cr - Im[B] * Ci, Ti = Re[B] * Ci + Im[B] * Cr;
					Re[B] = Re[A] - Tr; Im[B] = Im[A] - Ti;
					Re[A] += Tr; Im[A] += Ti;
					const float Nr = Cr * Wr - Ci * Wi;
					Ci = Cr * Wi + Ci * Wr;
					Cr = Nr;
				}
			}
		}
	}

	float Db(double Power) { return float(10.0 * std::log10(FMath::Max(Power, 1e-20))); }

	/** Power spectrum (one channel window), Hann windowed; Out has N/2+1 bins. */
	void PowerSpectrum(const float* X, int64 Start, int64 Total, int32 N, TArray<float>& Re, TArray<float>& Im, TArray<double>& Out, bool bAccumulate)
	{
		Re.SetNumUninitialized(N);
		Im.SetNumUninitialized(N);
		for (int32 I = 0; I < N; ++I)
		{
			const int64 Idx = Start + I;
			const float W = 0.5f - 0.5f * float(std::cos(2.0 * kPiD * double(I) / double(N - 1)));
			Re[I] = (Idx >= 0 && Idx < Total) ? X[Idx] * W : 0.f;
			Im[I] = 0.f;
		}
		Fft(Re.GetData(), Im.GetData(), N);
		if (!bAccumulate || Out.Num() != N / 2 + 1) { Out.SetNumUninitialized(N / 2 + 1); FMemory::Memzero(Out.GetData(), Out.Num() * sizeof(double)); }
		for (int32 K = 0; K <= N / 2; ++K) Out[K] += double(Re[K]) * Re[K] + double(Im[K]) * Im[K];
	}

	// ---------------------------------------------------------------- canvas and font
	struct FDmCanvas
	{
		int32 W = 0, H = 0;
		TArray<FColor> Px;
		FDmCanvas(int32 InW, int32 InH, FColor Bg) : W(InW), H(InH) { Px.Init(Bg, W * H); }
		void Set(int32 X, int32 Y, FColor C) { if (X >= 0 && Y >= 0 && X < W && Y < H) Px[Y * W + X] = C; }
		void Blend(int32 X, int32 Y, FColor C, float A)
		{
			if (X < 0 || Y < 0 || X >= W || Y >= H) return;
			FColor& D = Px[Y * W + X];
			D.R = uint8(D.R + (C.R - D.R) * A); D.G = uint8(D.G + (C.G - D.G) * A); D.B = uint8(D.B + (C.B - D.B) * A);
		}
		void Rect(int32 X0, int32 Y0, int32 X1, int32 Y1, FColor C)
		{
			for (int32 Y = FMath::Max(0, Y0); Y < FMath::Min(H, Y1); ++Y)
				for (int32 X = FMath::Max(0, X0); X < FMath::Min(W, X1); ++X) Px[Y * W + X] = C;
		}
		void Frame(int32 X0, int32 Y0, int32 X1, int32 Y1, FColor C)
		{
			for (int32 X = X0; X < X1; ++X) { Set(X, Y0, C); Set(X, Y1 - 1, C); }
			for (int32 Y = Y0; Y < Y1; ++Y) { Set(X0, Y, C); Set(X1 - 1, Y, C); }
		}
		void HLine(int32 X0, int32 X1, int32 Y, FColor C, int32 Dash = 0)
		{
			for (int32 X = X0; X < X1; ++X) if (!Dash || ((X / Dash) & 1) == 0) Set(X, Y, C);
		}
		void VLine(int32 X, int32 Y0, int32 Y1, FColor C, int32 Dash = 0)
		{
			for (int32 Y = Y0; Y < Y1; ++Y) if (!Dash || ((Y / Dash) & 1) == 0) Set(X, Y, C);
		}
		void Line(float X0, float Y0, float X1, float Y1, FColor C)
		{
			const int32 Steps = FMath::Max(1, int32(FMath::Max(FMath::Abs(X1 - X0), FMath::Abs(Y1 - Y0))));
			for (int32 I = 0; I <= Steps; ++I)
			{
				const float T = float(I) / float(Steps);
				Set(int32(X0 + (X1 - X0) * T + 0.5f), int32(Y0 + (Y1 - Y0) * T + 0.5f), C);
			}
		}

		static const uint8* Glyph(TCHAR Ch)
		{
			static const uint8 Blank[7] = { 0, 0, 0, 0, 0, 0, 0 };
			static const uint8 Digits[10][7] = {
				{ 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
				{ 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },
				{ 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
				{ 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
				{ 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },
			};
			static const uint8 Letters[26][7] = {
				{ 0x0E, 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11 }, { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E },
				{ 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C },
				{ 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 },
				{ 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F }, { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },
				{ 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C },
				{ 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },
				{ 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 },
				{ 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 },
				{ 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }, { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 },
				{ 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }, { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
				{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 },
				{ 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A }, { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 },
				{ 0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04 }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F },
			};
			struct FSym { TCHAR C; uint8 R[7]; };
			static const FSym Syms[] = {
				{ '.', { 0, 0, 0, 0, 0, 0x0C, 0x0C } }, { ',', { 0, 0, 0, 0, 0x0C, 0x04, 0x08 } }, { ':', { 0, 0x0C, 0x0C, 0, 0x0C, 0x0C, 0 } },
				{ '-', { 0, 0, 0, 0x1F, 0, 0, 0 } }, { '+', { 0, 0x04, 0x04, 0x1F, 0x04, 0x04, 0 } }, { '%', { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 } },
				{ '/', { 0, 0x01, 0x02, 0x04, 0x08, 0x10, 0 } }, { '(', { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 } }, { ')', { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 } },
				{ '=', { 0, 0, 0x1F, 0, 0x1F, 0, 0 } }, { '<', { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 } }, { '>', { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 } },
				{ '_', { 0, 0, 0, 0, 0, 0, 0x1F } }, { '#', { 0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A } }, { '!', { 0x04, 0x04, 0x04, 0x04, 0x04, 0, 0x04 } },
				{ '?', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0, 0x04 } }, { '*', { 0, 0x04, 0x15, 0x0E, 0x15, 0x04, 0 } }, { '|', { 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
				{ '[', { 0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E } }, { ']', { 0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E } }, { '\'', { 0x04, 0x04, 0x08, 0, 0, 0, 0 } },
			};
			if (Ch >= '0' && Ch <= '9') return Digits[Ch - '0'];
			if (Ch >= 'a' && Ch <= 'z') Ch = TCHAR(Ch - 'a' + 'A');
			if (Ch >= 'A' && Ch <= 'Z') return Letters[Ch - 'A'];
			for (const FSym& S : Syms) if (S.C == Ch) return S.R;
			return Blank;
		}
		/** 5x7 text at a pixel scale; returns the x after the text. */
		int32 Text(int32 X, int32 Y, const FString& Str, FColor C, int32 Scale = 2)
		{
			int32 Cx = X;
			for (TCHAR Ch : Str)
			{
				const uint8* G = Glyph(Ch);
				for (int32 Row = 0; Row < 7; ++Row)
					for (int32 Col = 0; Col < 5; ++Col)
						if (G[Row] & (0x10 >> Col))
							for (int32 Dy = 0; Dy < Scale; ++Dy)
								for (int32 Dx = 0; Dx < Scale; ++Dx) Set(Cx + Col * Scale + Dx, Y + Row * Scale + Dy, C);
				Cx += 6 * Scale;
			}
			return Cx;
		}
		bool Save(const FString& Path) const
		{
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(W, H, TArrayView64<const FColor>(Px.GetData(), Px.Num()), Png);
			if (Png.Num() == 0) return false;
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			return FFileHelper::SaveArrayToFile(Png, *Path);
		}
	};

	const FColor Ink(20, 17, 14), Panel(30, 26, 21), Grid(62, 54, 44), Gold(232, 199, 122), Dim(150, 130, 96), Pale(240, 226, 196);
	const FColor Good(132, 200, 120), Bad(232, 96, 80), Amber(240, 170, 80), Teal(110, 190, 200);

	FColor Heat(float U)
	{
		// warm "ember" palette: ink -> deep red -> orange -> gold -> pale
		U = FMath::Clamp(U, 0.f, 1.f);
		static const FColor Stops[6] = { FColor(20, 17, 14), FColor(70, 22, 40), FColor(150, 40, 40), FColor(220, 110, 40), FColor(245, 200, 100), FColor(255, 245, 220) };
		const float X = U * 5.f;
		const int32 I = FMath::Min(4, int32(X));
		const float F = X - float(I);
		const FColor A = Stops[I], B = Stops[I + 1];
		return FColor(uint8(A.R + (B.R - A.R) * F), uint8(A.G + (B.G - A.G) * F), uint8(A.B + (B.B - A.B) * F), 255);
	}

	float LogF(float Hz, float Lo, float Hi) { return float(std::log(FMath::Max(Hz, Lo) / Lo) / std::log(Hi / Lo)); }
}

FDmAudioStats DmAudioAnalysis::Analyse(const float* L, const float* R, int64 N, float Sr)
{
	FDmAudioStats S;
	if (!L || !R || N <= 0 || Sr <= 0.f) return S;
	S.Seconds = double(N) / Sr;
	double SumSq = 0, SumL = 0, SumR = 0, SumLR = 0, SumLL = 0, SumRR = 0;
	int64 Zc = 0;
	float Prev = 0.f;
	for (int64 I = 0; I < N; ++I)
	{
		const float A = L[I], B = R[I];
		if (!std::isfinite(A) || !std::isfinite(B)) { ++S.NaN; continue; }
		const float Pk = FMath::Max(std::fabs(A), std::fabs(B));
		S.Peak = FMath::Max(S.Peak, Pk);
		if (std::fabs(A) >= 0.999f) ++S.Clipped;
		if (std::fabs(B) >= 0.999f) ++S.Clipped;
		SumSq += double(A) * A + double(B) * B;
		SumL += A; SumR += B;
		SumLR += double(A) * B; SumLL += double(A) * A; SumRR += double(B) * B;
		if ((A >= 0.f) != (Prev >= 0.f)) ++Zc;
		Prev = A;
	}
	S.Rms = float(std::sqrt(SumSq / (2.0 * double(N))));
	S.PeakDb = 20.f * std::log10(FMath::Max(S.Peak, 1e-10f));
	S.RmsDb = 20.f * std::log10(FMath::Max(S.Rms, 1e-10f));
	S.CrestDb = S.PeakDb - S.RmsDb;
	S.Dc = float(FMath::Max(std::fabs(SumL / double(N)), std::fabs(SumR / double(N))));
	S.ZeroCross = float(double(Zc) / S.Seconds);
	S.Corr = (SumLL > 0 && SumRR > 0) ? float(SumLR / std::sqrt(SumLL * SumRR)) : 0.f;

	// windows: 100 ms silence, 400 ms loudness
	{
		const int64 Win = int64(0.1f * Sr);
		int64 Silent = 0, Count = 0;
		TArray<double> WinPow;
		for (int64 I = 0; I + Win <= N; I += Win)
		{
			double P = 0;
			for (int64 K = I; K < I + Win; ++K) P += double(L[K]) * L[K] + double(R[K]) * R[K];
			P /= double(2 * Win);
			WinPow.Add(P);
			++Count;
			if (P < 1e-6) ++Silent;        // -60 dBFS
		}
		S.Silent = Count ? float(double(Silent) / double(Count)) : 1.f;
		double Loud = 0, Quiet = 1e9;
		for (int32 I = 0; I + 4 <= WinPow.Num(); ++I)
		{
			const double P = (WinPow[I] + WinPow[I + 1] + WinPow[I + 2] + WinPow[I + 3]) * 0.25;
			Loud = FMath::Max(Loud, P); Quiet = FMath::Min(Quiet, P);
		}
		S.LoudestDb = Db(Loud);
		S.QuietestDb = Quiet < 1e8 ? Db(Quiet) : -200.f;
	}

	// Welch average spectrum (both channels)
	{
		const int32 Nfft = 4096;
		TArray<float> Re, Im;
		TArray<double> Acc;
		Acc.SetNumZeroed(Nfft / 2 + 1);
		int32 Frames = 0;
		for (int64 Start = 0; Start + Nfft <= N; Start += Nfft / 2)
		{
			PowerSpectrum(L, Start, N, Nfft, Re, Im, Acc, true);
			PowerSpectrum(R, Start, N, Nfft, Re, Im, Acc, true);
			++Frames;
		}
		if (Frames == 0) PowerSpectrum(L, 0, N, Nfft, Re, Im, Acc, true);
		const double BinHz = double(Sr) / Nfft;
		double Tot = 0, Sub = 0, Low = 0, Mid = 0, High = 0, Harsh = 0, Cen = 0, Air = 0;
		for (int32 K = 1; K <= Nfft / 2; ++K)
		{
			const double F = K * BinHz, P = Acc[K];
			if (F < 15.0) continue;
			Tot += P; Cen += F * P;
			if (F < 60.0) Sub += P; else if (F < 250.0) Low += P; else if (F < 4000.0) Mid += P; else High += P;
			if (F >= 2500.0 && F < 6000.0) Harsh += P;
			if (F >= 12000.0) Air += P;
		}
		if (Tot > 0)
		{
			S.Sub = float(Sub / Tot); S.Low = float(Low / Tot); S.Mid = float(Mid / Tot); S.High = float(High / Tot);
			S.Harsh = float(Harsh / Tot); S.Centroid = float(Cen / Tot); S.Air12k = float(Air / Tot);
		}
		// log-spaced curve, normalised so a full-scale sine reads about 0 dB
		const double Norm = 16.0 / (double(FMath::Max(1, Frames)) * 2.0 * double(Nfft) * double(Nfft));
		const int32 Points = 360;
		S.SpecHz.SetNum(Points); S.SpecDb.SetNum(Points);
		for (int32 I = 0; I < Points; ++I)
		{
			const double F0 = 20.0 * std::pow(1000.0, double(I) / Points), F1 = 20.0 * std::pow(1000.0, double(I + 1) / Points);
			const int32 K0 = FMath::Clamp(int32(F0 / BinHz), 1, Nfft / 2), K1 = FMath::Clamp(int32(F1 / BinHz), K0, Nfft / 2);
			double P = 0;
			for (int32 K = K0; K <= K1; ++K) P += Acc[K];
			P /= double(K1 - K0 + 1);
			S.SpecHz[I] = float(std::sqrt(F0 * F1));
			S.SpecDb[I] = Db(P * Norm);
		}
	}

	// ITU-R BS.1770-4 loudness: K-weighting (the pre-filter shelf and the RLB high-pass, coefficients for any rate),
	// 400 ms blocks with 75 % overlap gated at -70 LUFS and -10 LU (integrated), 3 s windows every 100 ms (short-term)
	{
		auto Biquad = [](double F0, double Q, double G, bool bShelf, double Fs, double B[3], double A[3])
		{
			const double K = std::tan(3.14159265358979 * F0 / Fs);
			const double A0 = 1.0 + K / Q + K * K;
			if (bShelf)
			{
				const double Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
				B[0] = (Vh + Vb * K / Q + K * K) / A0; B[1] = 2.0 * (K * K - Vh) / A0; B[2] = (Vh - Vb * K / Q + K * K) / A0;
			}
			else { B[0] = 1.0; B[1] = -2.0; B[2] = 1.0; }
			A[0] = 1.0; A[1] = 2.0 * (K * K - 1.0) / A0; A[2] = (1.0 - K / Q + K * K) / A0;
		};
		double Bs[3], As[3], Bh[3], Ah[3];
		Biquad(1681.974450955533, 0.7071752369554196, 3.999843853973347, true, Sr, Bs, As);
		Biquad(38.13547087602444, 0.5003270373238773, 0.0, false, Sr, Bh, Ah);
		const int64 Hop = FMath::Max<int64>(1, int64(0.1 * Sr));
		const int64 NH = N / Hop;
		TArray<double> HopPow;
		HopPow.SetNumZeroed(int32(NH));
		for (int32 Ch = 0; Ch < 2; ++Ch)
		{
			const float* X = Ch ? R : L;
			double X1 = 0, X2 = 0, Y1 = 0, Y2 = 0, Z1 = 0, Z2 = 0;
			for (int64 I = 0; I < NH * Hop; ++I)
			{
				const double V = std::isfinite(X[I]) ? double(X[I]) : 0.0;
				const double Y = Bs[0] * V + Bs[1] * X1 + Bs[2] * X2 - As[1] * Y1 - As[2] * Y2;
				X2 = X1; X1 = V;
				const double Z = Bh[0] * Y + Bh[1] * Y1 + Bh[2] * Y2 - Ah[1] * Z1 - Ah[2] * Z2;
				Y2 = Y1; Y1 = Y; Z2 = Z1; Z1 = Z;
				HopPow[int32(I / Hop)] += Z * Z;
			}
		}
		auto Lu = [](double Ms) { return -0.691 + 10.0 * std::log10(FMath::Max(Ms, 1e-20)); };
		TArray<double> M4;
		for (int32 I = 0; I + 4 <= int32(NH); ++I) M4.Add((HopPow[I] + HopPow[I + 1] + HopPow[I + 2] + HopPow[I + 3]) / double(4 * Hop));
		double Sum1 = 0;
		int32 C1 = 0;
		for (double M : M4) { S.MomMax = FMath::Max(S.MomMax, float(Lu(M))); if (Lu(M) > -70.0) { Sum1 += M; ++C1; } }
		if (C1 > 0)
		{
			const double Rel = Lu(Sum1 / C1) - 10.0;
			double Sum2 = 0;
			int32 C2 = 0;
			for (double M : M4) if (Lu(M) > -70.0 && Lu(M) > Rel) { Sum2 += M; ++C2; }
			S.Lufs = C2 > 0 ? float(Lu(Sum2 / C2)) : -120.f;
		}
		TArray<float> St;
		for (int32 I = 0; I + 30 <= int32(NH); ++I)
		{
			double P = 0;
			for (int32 K = 0; K < 30; ++K) P += HopPow[I + K];
			St.Add(float(Lu(P / double(30 * Hop))));
		}
		if (St.Num() == 0 && NH > 0) { double P = 0; for (double H : HopPow) P += H; St.Add(float(Lu(P / double(NH * Hop)))); }
		for (float V : St) { S.StMax = FMath::Max(S.StMax, V); if (V > -18.f) S.StAboveM18 += 0.1f; }
		if (St.Num())
		{
			TArray<float> Sorted = St;
			Sorted.Sort();
			S.StMedian = Sorted[Sorted.Num() / 2];
		}
	}
	// the worst 3 s window's share of 2-5 kHz energy (frames of 4096, hop 2048)
	{
		const int32 Nfft = 4096;
		TArray<float> Re, Im;
		TArray<double> Acc, Pres, Tot;
		const double BinHz = double(Sr) / Nfft;
		for (int64 Start = 0; Start + Nfft <= N; Start += Nfft / 2)
		{
			Acc.Reset();
			Acc.SetNumZeroed(Nfft / 2 + 1);
			PowerSpectrum(L, Start, N, Nfft, Re, Im, Acc, true);
			PowerSpectrum(R, Start, N, Nfft, Re, Im, Acc, true);
			double P = 0, T = 0;
			for (int32 K = 1; K <= Nfft / 2; ++K)
			{
				const double F = K * BinHz;
				if (F < 15.0) continue;
				T += Acc[K];
				if (F >= 2000.0 && F < 5000.0) P += Acc[K];
			}
			Pres.Add(P); Tot.Add(T);
		}
		const int32 W = FMath::Max(1, int32(3.0 * Sr / (Nfft / 2)));
		for (int32 I = 0; I + W <= Pres.Num(); ++I)
		{
			double P = 0, T = 0;
			for (int32 K = 0; K < W; ++K) { P += Pres[I + K]; T += Tot[I + K]; }
			if (T > 1e-9 * W) S.PresenceWorst = FMath::Max(S.PresenceWorst, float(P / T));
		}
	}
	return S;
}

bool DmAudioAnalysis::WriteWav(const FString& Path, const float* L, const float* R, int64 N, float Sr)
{
	if (!L || !R || N <= 0) return false;
	TArray<uint8> Out;
	const uint32 DataBytes = uint32(N * 4);
	Out.Reserve(44 + DataBytes);
	auto U32 = [&Out](uint32 V) { for (int32 I = 0; I < 4; ++I) Out.Add(uint8((V >> (8 * I)) & 0xFF)); };
	auto U16 = [&Out](uint16 V) { Out.Add(uint8(V & 0xFF)); Out.Add(uint8(V >> 8)); };
	auto Tag = [&Out](const char* T) { for (int32 I = 0; I < 4; ++I) Out.Add(uint8(T[I])); };
	Tag("RIFF"); U32(36 + DataBytes); Tag("WAVE");
	Tag("fmt "); U32(16); U16(1); U16(2); U32(uint32(Sr)); U32(uint32(Sr) * 4); U16(4); U16(16);
	Tag("data"); U32(DataBytes);
	for (int64 I = 0; I < N; ++I)
	{
		const float A = std::isfinite(L[I]) ? FMath::Clamp(L[I], -1.f, 1.f) : 0.f;
		const float B = std::isfinite(R[I]) ? FMath::Clamp(R[I], -1.f, 1.f) : 0.f;
		U16(uint16(int16(FMath::RoundToInt(A * 32767.f))));
		U16(uint16(int16(FMath::RoundToInt(B * 32767.f))));
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	return FFileHelper::SaveArrayToFile(Out, *Path);
}

FString DmAudioAnalysis::Describe(const FDmAudioStats& S)
{
	return FString::Printf(TEXT("%.1fs LUFS %.1f st-max %.1f st-med %.1f >-18 %.1fs pres3s %.1f%% air %.2f%% | peak %.1f dBFS rms %.1f dBFS loud400 %.1f quiet400 %.1f clipped %lld nan %lld dc %.5f | sub %.1f%% low %.1f%% mid %.1f%% high %.1f%% harsh %.1f%% | centroid %.0f Hz zc %.0f/s silent %.0f%% crest %.1f dB corr %.2f"),
		S.Seconds, S.Lufs, S.StMax, S.StMedian, S.StAboveM18, S.PresenceWorst * 100.f, S.Air12k * 100.f, S.PeakDb, S.RmsDb, S.LoudestDb, S.QuietestDb, S.Clipped, S.NaN, S.Dc, S.Sub * 100.f, S.Low * 100.f, S.Mid * 100.f, S.High * 100.f, S.Harsh * 100.f,
		S.Centroid, S.ZeroCross, S.Silent * 100.f, S.CrestDb, S.Corr);
}

bool DmAudioAnalysis::WritePanel(const FString& Path, const FString& Title, const FString& Subtitle, const float* L, const float* R, int64 N, float Sr,
	const FDmAudioStats& S, const TArray<FDmAudioCheck>& Checks, const TArray<FDmAudioTrack>& Tracks, const TArray<FDmAudioMarker>& Markers)
{
	if (!L || !R || N <= 0) return false;
	FDmCanvas C(1600, 1120, Ink);
	const int32 X0 = 70, X1 = 1570, Wd = X1 - X0;
	const double Dur = double(N) / Sr;
	C.Text(30, 18, Title, Gold, 4);
	C.Text(30, 56, Subtitle, Dim, 2);

	auto TimeX = [&](double T) { return X0 + int32(double(Wd) * T / FMath::Max(Dur, 1e-6)); };

	// ---- waveform (dB-aware envelope: linear amplitude), 90..250
	{
		const int32 Y0 = 84, Y1 = 254, Mid = (Y0 + Y1) / 2, Half = (Y1 - Y0) / 2 - 4;
		C.Rect(X0, Y0, X1, Y1, Panel);
		const float Ceil = 0.891f;
		C.HLine(X0, X1, Mid - int32(Half * Ceil), FColor(120, 50, 40), 6);
		C.HLine(X0, X1, Mid + int32(Half * Ceil), FColor(120, 50, 40), 6);
		C.HLine(X0, X1, Mid - int32(Half * 0.1f), Grid, 3);
		C.HLine(X0, X1, Mid + int32(Half * 0.1f), Grid, 3);
		C.HLine(X0, X1, Mid, Grid);
		for (int32 X = 0; X < Wd; ++X)
		{
			const int64 A = int64(double(X) / Wd * N), B = FMath::Max(A + 1, int64(double(X + 1) / Wd * N));
			float Mx = -1.f, Mn = 1.f, MxR = -1.f, MnR = 1.f;
			double Sq = 0;
			for (int64 K = A; K < B && K < N; ++K)
			{
				Mx = FMath::Max(Mx, L[K]); Mn = FMath::Min(Mn, L[K]);
				MxR = FMath::Max(MxR, R[K]); MnR = FMath::Min(MnR, R[K]);
				Sq += double(L[K]) * L[K];
			}
			const float Rms = float(std::sqrt(Sq / double(FMath::Max<int64>(1, B - A))));
			C.VLine(X0 + X, Mid - int32(Half * FMath::Clamp(FMath::Max(Mx, MxR), -1.f, 1.f)), Mid - int32(Half * FMath::Clamp(FMath::Min(Mn, MnR), -1.f, 1.f)) + 1, FColor(150, 110, 60));
			C.VLine(X0 + X, Mid - int32(Half * Rms), Mid + int32(Half * Rms) + 1, Gold);
		}
		C.Text(8, Y0 + 4, TEXT("WAVE"), Dim, 1);
		C.Text(8, Mid - int32(Half * Ceil) + 4, TEXT("-1DB"), FColor(170, 80, 60), 1);
	}

	// ---- spectrogram 270..570
	{
		const int32 Y0 = 268, Y1 = 568, Hh = Y1 - Y0;
		const float FLo = 30.f, FHi = 16000.f;
		const int32 Nfft = 2048;
		TArray<float> Re, Im;
		TArray<double> P;
		TArray<float> Mono;
		Mono.SetNumUninitialized(int32(FMath::Min<int64>(N, MAX_int32)));
		for (int32 I = 0; I < Mono.Num(); ++I) Mono[I] = 0.5f * (L[I] + R[I]);
		const double BinHz = double(Sr) / Nfft;
		for (int32 X = 0; X < Wd; ++X)
		{
			const int64 Center = int64((double(X) + 0.5) / Wd * N);
			PowerSpectrum(Mono.GetData(), Center - Nfft / 2, Mono.Num(), Nfft, Re, Im, P, false);
			for (int32 Y = 0; Y < Hh; ++Y)
			{
				const float U0 = float(Hh - 1 - Y) / float(Hh), U1 = float(Hh - Y) / float(Hh);
				const double F0 = FLo * std::pow(FHi / FLo, U0), F1 = FLo * std::pow(FHi / FLo, U1);
				const int32 K0 = FMath::Clamp(int32(F0 / BinHz), 1, Nfft / 2), K1 = FMath::Clamp(int32(F1 / BinHz), K0, Nfft / 2);
				double Pw = 0;
				for (int32 K = K0; K <= K1; ++K) Pw = FMath::Max(Pw, P[K]);
				const float D = Db(Pw / (double(Nfft) * Nfft * 0.0625));
				C.Set(X0 + X, Y0 + Y, Heat((D + 95.f) / 80.f));
			}
		}
		const float Marks[9] = { 50.f, 100.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 16000.f };
		const TCHAR* Labels[9] = { TEXT("50"), TEXT("100"), TEXT("250"), TEXT("500"), TEXT("1K"), TEXT("2K"), TEXT("4K"), TEXT("8K"), TEXT("16K") };
		for (int32 I = 0; I < 9; ++I)
		{
			const int32 Y = Y1 - 1 - int32(LogF(Marks[I], FLo, FHi) * Hh);
			C.HLine(X0 - 6, X0, Y, Dim);
			C.Text(X0 - 6 - 6 * int32(FCString::Strlen(Labels[I])) - 2, Y - 3, Labels[I], Dim, 1);
		}
		C.Text(8, Y0 + 4, TEXT("SPEC"), Dim, 1);
	}

	// ---- markers over waveform and spectrogram, time axis
	for (const FDmAudioMarker& M : Markers)
	{
		const int32 X = TimeX(M.Time);
		C.VLine(X, 84, 568, Pale, 4);
		C.Text(X + 3, 258, M.Label, Pale, 1);
	}
	for (int32 Sec = 0; Sec <= int32(Dur); ++Sec)
	{
		const int32 X = TimeX(Sec);
		C.VLine(X, 568, 574, Dim);
		if (Sec % 2 == 0 || Dur < 12.0) C.Text(X - 3, 577, FString::Printf(TEXT("%d"), Sec), Dim, 1);
	}

	// ---- tracks 592..762
	{
		const int32 Y0 = 592, Y1 = 762, Hh = Y1 - Y0;
		C.Rect(X0, Y0, X1, Y1, Panel);
		C.HLine(X0, X1, Y0 + Hh / 2, Grid, 3);
		C.Text(8, Y0 + 4, TEXT("GAIN"), Dim, 1);
		int32 Lx = X0 + 8;
		for (const FDmAudioTrack& Tr : Tracks)
		{
			const int32 Num = Tr.Values.Num();
			if (Num >= 2)
				for (int32 I = 1; I < Num; ++I)
				{
					const float Xa = X0 + float(Wd) * float(I - 1) / float(Num - 1), Xb = X0 + float(Wd) * float(I) / float(Num - 1);
					const float Ya = Y1 - 3 - FMath::Clamp(Tr.Values[I - 1], 0.f, 1.f) * (Hh - 6), Yb = Y1 - 3 - FMath::Clamp(Tr.Values[I], 0.f, 1.f) * (Hh - 6);
					C.Line(Xa, Ya, Xb, Yb, Tr.Color);
					C.Line(Xa, Ya + 1, Xb, Yb + 1, Tr.Color);
				}
			C.Rect(Lx, Y0 + 6, Lx + 10, Y0 + 14, Tr.Color);
			Lx = C.Text(Lx + 14, Y0 + 6, Tr.Name, Tr.Color, 1) + 14;
		}
		if (Tracks.Num() == 0) C.Text(X0 + 8, Y0 + 8, TEXT("(NO TRACKS)"), Dim, 1);
	}

	// ---- average spectrum 784..1090 (left)
	{
		const int32 Sx0 = 70, Sx1 = 900, Y0 = 784, Y1 = 1090, Hh = Y1 - Y0;
		C.Rect(Sx0, Y0, Sx1, Y1, Panel);
		const float DbTop = -10.f, DbBot = -110.f;
		const float Hz2Lo = 2500.f, Hz2Hi = 6000.f;
		const int32 Hx0 = Sx0 + int32(LogF(Hz2Lo, 20.f, 20000.f) * (Sx1 - Sx0)), Hx1 = Sx0 + int32(LogF(Hz2Hi, 20.f, 20000.f) * (Sx1 - Sx0));
		for (int32 Y = Y0; Y < Y1; ++Y) for (int32 X = Hx0; X < Hx1; ++X) C.Blend(X, Y, FColor(120, 40, 40), 0.25f);
		const float Edges[5] = { 60.f, 250.f, 1000.f, 4000.f, 10000.f };
		const TCHAR* EdgeL[5] = { TEXT("60"), TEXT("250"), TEXT("1K"), TEXT("4K"), TEXT("10K") };
		for (int32 I = 0; I < 5; ++I)
		{
			const int32 X = Sx0 + int32(LogF(Edges[I], 20.f, 20000.f) * (Sx1 - Sx0));
			C.VLine(X, Y0, Y1, Grid, 3);
			C.Text(X - 6, Y1 + 4, EdgeL[I], Dim, 1);
		}
		for (int32 D = -20; D >= -100; D -= 20)
		{
			const int32 Y = Y0 + int32((DbTop - D) / (DbTop - DbBot) * Hh);
			C.HLine(Sx0, Sx1, Y, Grid, 3);
			C.Text(Sx0 - 30, Y - 3, FString::Printf(TEXT("%d"), D), Dim, 1);
		}
		for (int32 I = 1; I < S.SpecDb.Num(); ++I)
		{
			auto Px = [&](int32 K) { return float(Sx0) + LogF(S.SpecHz[K], 20.f, 20000.f) * float(Sx1 - Sx0); };
			auto Py = [&](int32 K) { return float(Y0) + FMath::Clamp((DbTop - S.SpecDb[K]) / (DbTop - DbBot), 0.f, 1.f) * float(Hh - 1); };
			C.Line(Px(I - 1), Py(I - 1), Px(I), Py(I), Gold);
			C.Line(Px(I - 1), Py(I - 1) + 1, Px(I), Py(I) + 1, Gold);
		}
		C.Text(Sx0 + 6, Y0 + 6, TEXT("AVERAGE SPECTRUM (DB)   SHADED: HARSH BAND 2.5-6K"), Dim, 1);
	}

	// ---- numbers and checks (right)
	{
		int32 Y = 784;
		const int32 Tx = 930;
		auto Line = [&](const FString& Str, FColor Col) { C.Text(Tx, Y, Str, Col, 2); Y += 20; };
		Line(FString::Printf(TEXT("PEAK %.1f DBFS   RMS %.1f DBFS"), S.PeakDb, S.RmsDb), Pale);
		Line(FString::Printf(TEXT("LOUD400 %.1f  QUIET400 %.1f"), S.LoudestDb, S.QuietestDb), Pale);
		Line(FString::Printf(TEXT("CLIPPED %lld  NAN %lld  DC %.4f"), S.Clipped, S.NaN, S.Dc), Pale);
		Line(FString::Printf(TEXT("SUB %.1f%%  LOW %.1f%%  MID %.1f%%"), S.Sub * 100.f, S.Low * 100.f, S.Mid * 100.f), Pale);
		Line(FString::Printf(TEXT("HIGH %.1f%%  HARSH %.1f%%"), S.High * 100.f, S.Harsh * 100.f), Pale);
		Line(FString::Printf(TEXT("CENTROID %.0f HZ  ZC %.0f/S"), S.Centroid, S.ZeroCross), Pale);
		Line(FString::Printf(TEXT("SILENT %.0f%%  CREST %.1f DB  CORR %.2f"), S.Silent * 100.f, S.CrestDb, S.Corr), Pale);
		// band bars
		{
			const float B[4] = { S.Sub, S.Low, S.Mid, S.High };
			const TCHAR* Bn[4] = { TEXT("SUB"), TEXT("LOW"), TEXT("MID"), TEXT("HIGH") };
			for (int32 I = 0; I < 4; ++I)
			{
				const int32 Bx = Tx + I * 150;
				C.Rect(Bx, Y + 4, Bx + 130, Y + 18, Panel);
				C.Rect(Bx, Y + 4, Bx + int32(130 * FMath::Clamp(B[I], 0.f, 1.f)), Y + 18, I == 0 ? Amber : Gold);
				C.Text(Bx, Y + 22, Bn[I], Dim, 1);
			}
			Y += 40;
		}
		bool bAll = true;
		for (const FDmAudioCheck& K : Checks) bAll &= K.bPass;
		C.Text(Tx, Y, bAll ? TEXT("RESULT PASS") : TEXT("RESULT FAIL"), bAll ? Good : Bad, 3);
		Y += 30;
		for (const FDmAudioCheck& K : Checks)
		{
			if (Y > 1104) break;
			C.Text(Tx, Y, FString::Printf(TEXT("%s %s  %s"), K.bPass ? TEXT("OK  ") : TEXT("FAIL"), *K.Name, *K.Detail), K.bPass ? Good : Bad, 1);
			Y += 12;
		}
	}
	return C.Save(Path);
}

bool DmAudioAnalysis::WriteSummary(const FString& Path, const FString& Title, const TArray<FRow>& Rows)
{
	const int32 RowH = 26;
	FDmCanvas C(1600, 130 + RowH * FMath::Max(1, Rows.Num()) + 20, Ink);
	C.Text(30, 18, Title, Gold, 4);
	const int32 Cols[11] = { 30, 330, 450, 570, 690, 790, 890, 990, 1110, 1230, 1350 };
	const TCHAR* Head[11] = { TEXT("SCENARIO"), TEXT("PEAK DB"), TEXT("RMS DB"), TEXT("LOUD400"), TEXT("SUB%"), TEXT("LOW%"), TEXT("HARSH%"), TEXT("CENTROID"), TEXT("SILENT%"), TEXT("CLIP/NAN"), TEXT("RESULT") };
	for (int32 I = 0; I < 11; ++I) C.Text(Cols[I], 80, Head[I], Dim, 2);
	C.HLine(30, 1570, 102, Grid);
	int32 Y = 112;
	for (const FRow& Rw : Rows)
	{
		const FDmAudioStats& S = Rw.Stats;
		const FColor Col = Rw.bPass ? Pale : Bad;
		C.Text(Cols[0], Y, Rw.Name, Col, 2);
		C.Text(Cols[1], Y, FString::Printf(TEXT("%.1f"), S.PeakDb), Col, 2);
		C.Text(Cols[2], Y, FString::Printf(TEXT("%.1f"), S.RmsDb), Col, 2);
		C.Text(Cols[3], Y, FString::Printf(TEXT("%.1f"), S.LoudestDb), Col, 2);
		C.Text(Cols[4], Y, FString::Printf(TEXT("%.1f"), S.Sub * 100.f), S.Sub < 0.25f ? Col : Bad, 2);
		C.Text(Cols[5], Y, FString::Printf(TEXT("%.1f"), S.Low * 100.f), Col, 2);
		C.Text(Cols[6], Y, FString::Printf(TEXT("%.1f"), S.Harsh * 100.f), Col, 2);
		C.Text(Cols[7], Y, FString::Printf(TEXT("%.0f"), S.Centroid), Col, 2);
		C.Text(Cols[8], Y, FString::Printf(TEXT("%.0f"), S.Silent * 100.f), Col, 2);
		C.Text(Cols[9], Y, FString::Printf(TEXT("%lld/%lld"), S.Clipped, S.NaN), (S.Clipped || S.NaN) ? Bad : Col, 2);
		const int32 Ex = C.Text(Cols[10], Y, Rw.bPass ? TEXT("PASS") : TEXT("FAIL"), Rw.bPass ? Good : Bad, 2);
		if (!Rw.Note.IsEmpty()) C.Text(Ex + 12, Y + 4, Rw.Note, Dim, 1);
		Y += RowH;
	}
	return C.Save(Path);
}
