// DEMIURGE — Game/Core/DmFixture.cpp : see DmFixture.h. Owner: C0 bridge.

#include "Game/Core/DmFixture.h"

#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <string>
#include <vector>

#include "Sim/Names.h"

DEFINE_LOG_CATEGORY_STATIC(LogDmFixture, Log, All);

namespace
{
	using dm::World;

	constexpr double kInf = std::numeric_limits<double>::infinity();

	double Sat(double X) { return X < 0.0 ? 0.0 : (X > 1.0 ? 1.0 : X); }
	double Clamp(double X, double A, double B) { return X < A ? A : (X > B ? B : X); }

	// ------------------------------------------------------------------ the .lab file
	struct FLab
	{
		int32 N = 0, NTri = 0;
		float R = 100.f, SeaLevel = 0.f, RiverT = 5.f;
		std::vector<float> Pos;
		std::vector<float> Elev, Temp, Moist, Veg, Flow, Ice, Hab, Slope, Water;
		std::vector<uint8_t> Biome;
	};

	bool ReadLab(const FString& Path, FLab& L, FString& Err)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path)) { Err = FString::Printf(TEXT("cannot read %s"), *Path); return false; }
		int64 Off = 0;
		auto Rd = [&](void* Dst, int64 Size) -> bool
		{
			if (Size < 0 || Off + Size > Bytes.Num()) return false;
			FMemory::Memcpy(Dst, Bytes.GetData() + Off, Size);
			Off += Size;
			return true;
		};
		if (!Rd(&L.N, 4) || !Rd(&L.NTri, 4) || !Rd(&L.R, 4) || !Rd(&L.SeaLevel, 4) || !Rd(&L.RiverT, 4)) { Err = TEXT("truncated header"); return false; }
		if (L.N <= 0 || L.N > 1000000 || L.NTri <= 0 || L.NTri > 4000000) { Err = TEXT("bad header"); return false; }
		L.Pos.resize(size_t(L.N) * 3);
		std::vector<uint32_t> Tris(size_t(L.NTri) * 3);
		if (!Rd(L.Pos.data(), int64(L.N) * 12) || !Rd(Tris.data(), int64(L.NTri) * 12)) { Err = TEXT("truncated geometry"); return false; }
		for (std::vector<float>* F : { &L.Elev, &L.Temp, &L.Moist, &L.Veg, &L.Flow, &L.Ice, &L.Hab, &L.Slope, &L.Water })
		{
			F->resize(size_t(L.N));
			if (!Rd(F->data(), int64(L.N) * 4)) { Err = TEXT("truncated fields"); return false; }
		}
		L.Biome.resize(size_t(L.N));
		if (!Rd(L.Biome.data(), L.N)) { Err = TEXT("truncated biome"); return false; }
		if (Off != Bytes.Num()) { Err = FString::Printf(TEXT("%lld trailing bytes"), Bytes.Num() - Off); return false; }
		return true;
	}

	float Finite(float V, float Def = 0.f) { return std::isfinite(V) ? V : Def; }

	// ------------------------------------------------------------------ grid helpers
	struct FGrid
	{
		const World& W;
		explicit FGrid(const World& InW) : W(InW) {}
		bool Sea(int C) const { return dm::state::isSea(W, C); }
		bool Lake(int C) const { return !Sea(C) && W.water[size_t(C)] > 0.03f; }
		int Deg(int C) const { return W.geo->nbrN[size_t(C)]; }
		int Nb(int C, int K) const { return W.geo->nbr[size_t(6 * C + K)]; }

		// multi-source hop distance, capped (cells beyond the cap keep Cap); Pass(c) limits expansion
		template <class FPass>
		void Hops(const std::vector<int32_t>& Sources, int Cap, std::vector<uint8_t>& Out, FPass&& Pass) const
		{
			Out.assign(size_t(W.n), uint8_t(Cap));
			std::vector<int32_t> Q;
			Q.reserve(size_t(W.n));
			for (int32_t S : Sources) if (W.valid(S) && Out[size_t(S)] != 0) { Out[size_t(S)] = 0; Q.push_back(S); }
			for (size_t Head = 0; Head < Q.size(); ++Head)
			{
				const int C = Q[Head];
				const int D = Out[size_t(C)];
				if (D + 1 >= Cap) continue;
				for (int K = 0; K < Deg(C); ++K)
				{
					const int M = Nb(C, K);
					if (Out[size_t(M)] <= D + 1 || !Pass(M)) continue;
					Out[size_t(M)] = uint8_t(D + 1);
					Q.push_back(M);
				}
			}
		}
	};

	// ------------------------------------------------------------------ paths (A*, Path-like costs)
	enum class ENav : uint8 { Land, Coastal, Open };

	struct FPather
	{
		const World& W;
		FGrid G;
		std::vector<double> Cost;
		std::vector<int32_t> Prev;
		std::vector<uint32_t> Stamp;
		uint32_t Cur = 0;

		explicit FPather(const World& InW) : W(InW), G(InW)
		{
			Cost.assign(size_t(W.n), kInf);
			Prev.assign(size_t(W.n), -1);
			Stamp.assign(size_t(W.n), 0u);
		}

		double Step(int A, int B, ENav Nav) const
		{
			const bool SeaA = G.Sea(A), SeaB = G.Sea(B);
			if (SeaB)
			{
				if (Nav == ENav::Land || W.ice[size_t(B)] > 0.6f) return kInf;
				const double Depth = W.seaLevel - W.elev[size_t(B)];
				if (Nav == ENav::Coastal && Depth >= 0.6) return kInf;
				return (Nav == ENav::Coastal ? 0.6 : 0.35) + (SeaA ? 0.0 : 3.0);
			}
			double C = 1.0 + 6.0 * std::fabs(double(W.elev[size_t(B)]) - double(W.elev[size_t(A)]));
			if (W.veg[size_t(B)] > 0.7f) C += 0.4;
			if (W.ice[size_t(B)] > 0.5f) C += 3.0;
			if (G.Lake(B)) C += 2.0;
			C -= 0.25 * W.road[size_t(B)];
			if (C < 0.3) C = 0.3;
			if (SeaA) C += 3.0;
			return C;
		}

		// [From .. To] or empty
		std::vector<int32_t> Find(int From, int To, ENav Nav, double MaxCost)
		{
			std::vector<int32_t> Out;
			if (!W.valid(From) || !W.valid(To)) return Out;
			if (From == To) { Out.push_back(From); return Out; }
			++Cur;
			const double HScale = 0.3 / (1.25 * W.geo->step);
			auto H = [&](int C) { return HScale * W.geo->angle(C, To); };
			using FItem = std::pair<double, int32_t>;
			std::priority_queue<FItem, std::vector<FItem>, std::greater<FItem>> Open;
			Stamp[size_t(From)] = Cur; Cost[size_t(From)] = 0.0; Prev[size_t(From)] = -1;
			Open.push({ H(From), From });
			bool bFound = false;
			int Expanded = 0;
			while (!Open.empty() && Expanded < 60000)
			{
				const FItem It = Open.top();
				Open.pop();
				const int C = It.second;
				const double GC = Cost[size_t(C)];
				if (It.first > GC + H(C) + 1e-9) continue;   // stale
				if (C == To) { bFound = true; break; }
				++Expanded;
				for (int K = 0; K < G.Deg(C); ++K)
				{
					const int M = G.Nb(C, K);
					const double S = Step(C, M, Nav);
					if (!(S < kInf)) continue;
					const double NG = GC + S;
					if (NG > MaxCost) continue;
					if (Stamp[size_t(M)] == Cur && Cost[size_t(M)] <= NG) continue;
					Stamp[size_t(M)] = Cur; Cost[size_t(M)] = NG; Prev[size_t(M)] = C;
					Open.push({ NG + H(M), M });
				}
			}
			if (!bFound) return Out;
			for (int C = To; C != -1; C = Prev[size_t(C)]) Out.push_back(C);
			std::reverse(Out.begin(), Out.end());
			return Out;
		}

		double CostOf(const std::vector<int32_t>& P, ENav Nav) const
		{
			double S = 0;
			for (size_t I = 1; I < P.size(); ++I) S += Step(P[I - 1], P[I], Nav);
			return S;
		}
	};

	// ------------------------------------------------------------------ derived physical fields
	void DeriveFields(World& W, float RiverT)
	{
		FGrid G(W);
		const int N = W.n;

		// coast: hops to the nearest sea cell
		{
			std::vector<int32_t> Sea;
			for (int C = 0; C < N; ++C) if (G.Sea(C)) Sea.push_back(C);
			std::vector<uint8_t> D;
			G.Hops(Sea, 255, D, [](int) { return true; });
			W.coast = D;
		}
		// landmasses, largest first
		{
			std::vector<int32_t> Raw(size_t(N), -1);
			std::vector<int32_t> Sizes;
			std::vector<int32_t> Q;
			for (int C = 0; C < N; ++C)
			{
				if (G.Sea(C) || Raw[size_t(C)] >= 0) continue;
				const int Id = int(Sizes.size());
				Sizes.push_back(0);
				Q.clear(); Q.push_back(C); Raw[size_t(C)] = Id;
				for (size_t H = 0; H < Q.size(); ++H)
				{
					const int A = Q[H];
					Sizes[size_t(Id)]++;
					for (int K = 0; K < G.Deg(A); ++K)
					{
						const int M = G.Nb(A, K);
						if (G.Sea(M) || Raw[size_t(M)] >= 0) continue;
						Raw[size_t(M)] = Id;
						Q.push_back(M);
					}
				}
			}
			std::vector<int32_t> Order(Sizes.size());
			for (size_t I = 0; I < Order.size(); ++I) Order[I] = int32_t(I);
			std::stable_sort(Order.begin(), Order.end(), [&](int32_t A, int32_t B) { return Sizes[size_t(A)] > Sizes[size_t(B)]; });
			std::vector<int32_t> Rank(Sizes.size());
			for (size_t I = 0; I < Order.size(); ++I) Rank[size_t(Order[I])] = int32_t(I);
			W.cache.landId.assign(size_t(N), -1);
			for (int C = 0; C < N; ++C) if (Raw[size_t(C)] >= 0) W.cache.landId[size_t(C)] = Rank[size_t(Raw[size_t(C)])];
			W.cache.landCount = int(Sizes.size());
			W.cache.landHab.assign(Sizes.size(), 0);
			for (int C = 0; C < N; ++C)
				if (W.cache.landId[size_t(C)] >= 0 && W.hab[size_t(C)] > 0.05f) W.cache.landHab[size_t(W.cache.landId[size_t(C)])]++;
		}
		// down: steepest descent on the water surface
		for (int C = 0; C < N; ++C)
		{
			W.down[size_t(C)] = -1;
			if (G.Sea(C)) continue;
			double Best = double(W.elev[size_t(C)]) + double(W.water[size_t(C)]);
			for (int K = 0; K < G.Deg(C); ++K)
			{
				const int M = G.Nb(C, K);
				const double H = G.Sea(M) ? W.seaLevel - 1.0 : double(W.elev[size_t(M)]) + double(W.water[size_t(M)]);
				if (H < Best) { Best = H; W.down[size_t(C)] = M; }
			}
		}
		// rivers: land cells with flow > riverT, traced source -> mouth (a tributary ends where it joins)
		{
			std::vector<uint8_t> IsRiver(size_t(N), 0), HasUp(size_t(N), 0), Used(size_t(N), 0);
			for (int C = 0; C < N; ++C)
				if (!G.Sea(C) && !G.Lake(C) && W.flow[size_t(C)] > RiverT) IsRiver[size_t(C)] = 1;
			for (int C = 0; C < N; ++C)
				if (IsRiver[size_t(C)] && W.down[size_t(C)] >= 0) HasUp[size_t(W.down[size_t(C)])] = 1;
			struct FSrc { int32_t Cell; int32_t Len; };
			std::vector<FSrc> Sources;
			for (int C = 0; C < N; ++C)
			{
				if (!IsRiver[size_t(C)] || HasUp[size_t(C)]) continue;
				int Len = 0;
				for (int A = C; A >= 0 && Len < 4000 && !G.Sea(A); A = W.down[size_t(A)]) ++Len;
				Sources.push_back({ C, Len });
			}
			std::stable_sort(Sources.begin(), Sources.end(), [](const FSrc& A, const FSrc& B) { return A.Len != B.Len ? A.Len > B.Len : A.Cell < B.Cell; });
			W.rivers.clear();
			for (const FSrc& S : Sources)
			{
				dm::River Rv;
				int A = S.Cell;
				int Guard = 0;
				while (A >= 0 && Guard++ < 4000)
				{
					Rv.cells.push_back(A);
					Rv.flow.push_back(W.flow[size_t(A)]);
					if (G.Sea(A) || Used[size_t(A)]) break;
					Used[size_t(A)] = 1;
					A = W.down[size_t(A)];
				}
				if (Rv.cells.size() >= 3) W.rivers.push_back(std::move(Rv));
			}
			std::stable_sort(W.rivers.begin(), W.rivers.end(), [](const dm::River& A, const dm::River& B)
			{
				const float FA = A.flow.empty() ? 0.f : A.flow.back(), FB = B.flow.empty() ? 0.f : B.flow.back();
				return FA != FB ? FA > FB : A.cells.front() < B.cells.front();
			});
			// fresh: hops to river or lake
			std::vector<int32_t> Fresh;
			for (int C = 0; C < N; ++C) if (IsRiver[size_t(C)] || G.Lake(C)) Fresh.push_back(C);
			std::vector<uint8_t> D;
			G.Hops(Fresh, 255, D, [](int) { return true; });
			W.fresh = D;
		}
		// fauna and fertility
		for (int C = 0; C < N; ++C)
		{
			const double Veg = W.veg[size_t(C)], Ice = W.ice[size_t(C)], Slope = W.slope[size_t(C)], T = W.temp[size_t(C)];
			if (G.Sea(C))
			{
				const double Depth = W.seaLevel - W.elev[size_t(C)];
				const double Shelf = Sat(1.0 - Depth / 3.0);
				const double Cold = Sat((18.0 - T) / 20.0);
				W.fauna[size_t(C)] = float(Clamp((0.22 + 0.45 * Shelf + 0.2 * Cold) * (1.0 - 0.8 * Ice), 0.0, 1.0));
				W.fert[size_t(C)] = 0.5f;
			}
			else
			{
				const uint8_t Bi = W.biome[size_t(C)];
				const bool Frozen = Bi == dm::Biome::ICE || Bi == dm::Biome::SNOWPEAK;
				W.fauna[size_t(C)] = Frozen ? 0.02f : float(Clamp(0.1 + 0.75 * Veg * (1.0 - Ice) - 0.25 * Slope, 0.0, 1.0));
				const double Near = W.fresh[size_t(C)] <= 1 ? 0.25 : (W.fresh[size_t(C)] <= 3 ? 0.1 : 0.0);
				const double Cold = Sat((-2.0 - T) / 10.0);
				W.fert[size_t(C)] = float(Clamp(0.25 + 0.55 * W.moist[size_t(C)] + 0.25 * Veg + Near - 0.45 * FMath::Min(Slope, 1.0) - 0.6 * Ice - 0.4 * Cold, 0.05, 1.4));
			}
		}
	}

	// ------------------------------------------------------------------ history
	const double kTechYield[7] = { 1, 2.5, 5, 10, 25, 60, 120 };
	const double kTierGate[5] = { 0, 60, 400, 1500, 12000 };
	const int kTierMinAge[5] = { 0, 0, dm::Age::BRONZE, dm::Age::IRON, dm::Age::INDUSTRY };
	const float kUrbanCore[5] = { 0.1f, 0.25f, 0.5f, 0.8f, 1.0f };

	double Suit(double T, double M) { return Sat(1.0 - std::fabs(T - 16.0) / 22.0) * Sat(M * 1.8); }

	double FarmPot(const World& W, int C, int Age)
	{
		if (W.hab[size_t(C)] <= 0.15f || W.water[size_t(C)] > 0.03f) return 0.0;
		return 22.0 * FMath::Min(double(W.fert[size_t(C)]), 1.2) * Suit(W.temp[size_t(C)], W.moist[size_t(C)]) * kTechYield[FMath::Clamp(Age, 0, 6)];
	}
	double Forage(const World& W, int C) { return 3.0 * (0.6 * W.veg[size_t(C)] + 0.4 * W.fauna[size_t(C)]); }

	int TierFor(double Pop, int Age)
	{
		int T = 0;
		for (int K = 1; K < 5; ++K) if (Pop >= kTierGate[K] && Age >= kTierMinAge[K]) T = K;
		return T;
	}

	std::string YearText(double Y) { return "the year " + std::to_string(int(std::floor(Y))); }

	void AddEntry(World& W, double Year, const char* Type, const std::string& Text, int Cell, int Culture, int Weight)
	{
		dm::ChronicleEntry E;
		E.id = W.nextId.entry++;
		E.year = Year;
		E.type = Type;
		E.text = Text;
		E.cell = Cell;
		E.culture = Culture;
		E.weight = Weight;
		W.chronicle.push_back(std::move(E));
	}

	void GenerateHistory(World& W, const FDmFixtureOptions& O, FDmFixtureReport& Rep)
	{
		FGrid G(W);
		FPather P(W);
		const int N = W.n;
		dm::Rng Rng(O.Seed * 2654435761u + 17u);
		dm::Rng NameRng = Rng.fork("names");
		const int LeadAge = FMath::Clamp(O.Age, 0, 6);
		const double Year = FMath::Max(O.Year, 200.0);
		W.year = Year;

		// ---- candidate sites, best first
		struct FSite { int32_t Cell; double Score; };
		std::vector<FSite> Sites;
		for (int C = 0; C < N; ++C)
		{
			if (G.Sea(C) || G.Lake(C) || W.hab[size_t(C)] < 0.3f || W.ice[size_t(C)] > 0.3f) continue;
			double S = W.hab[size_t(C)] + (W.coast[size_t(C)] <= 1 ? 0.12 : 0.0) + (W.fresh[size_t(C)] <= 1 ? 0.15 : 0.0) + 0.06 * Rng.f();
			Sites.push_back({ C, S });
		}
		std::stable_sort(Sites.begin(), Sites.end(), [](const FSite& A, const FSite& B) { return A.Score != B.Score ? A.Score > B.Score : A.Cell < B.Cell; });

		// ---- greedy blue-noise placement
		std::vector<int32_t> Centres;
		std::vector<double> CentreScore;
		{
			std::vector<uint8_t> Dist(size_t(N), 255);
			std::vector<int32_t> Q;
			for (int Sep : { 9, 7, 6 })
			{
				for (const FSite& S : Sites)
				{
					if (int(Centres.size()) >= O.Settlements) break;
					if (Dist[size_t(S.Cell)] < Sep) continue;
					Centres.push_back(S.Cell);
					CentreScore.push_back(S.Score);
					// local BFS to update the separation field
					Q.clear(); Q.push_back(S.Cell); Dist[size_t(S.Cell)] = 0;
					for (size_t H = 0; H < Q.size(); ++H)
					{
						const int A = Q[H];
						const int D = Dist[size_t(A)];
						if (D + 1 >= 10) continue;
						for (int K = 0; K < G.Deg(A); ++K)
						{
							const int M = G.Nb(A, K);
							if (Dist[size_t(M)] <= D + 1) continue;
							Dist[size_t(M)] = uint8_t(D + 1);
							Q.push_back(M);
						}
					}
				}
			}
		}
		const int NS = int(Centres.size());
		if (NS == 0) return;

		// ---- culture seeds: farthest-point sampling among the best sites
		const int NC = FMath::Clamp(O.Cultures, 1, 6);
		std::vector<int32_t> SeedIdx;   // index into Centres
		SeedIdx.push_back(0);
		while (int(SeedIdx.size()) < FMath::Min(NC, NS))
		{
			double BestD = -1; int Best = -1;
			for (int I = 0; I < NS; ++I)
			{
				double DMin = 1e9;
				for (int32_t Si : SeedIdx) DMin = FMath::Min(DMin, W.geo->angle(Centres[size_t(I)], Centres[size_t(Si)]));
				const double Score = DMin * (0.6 + 0.4 * CentreScore[size_t(I)]);
				if (Score > BestD) { BestD = Score; Best = I; }
			}
			if (Best < 0) break;
			SeedIdx.push_back(Best);
		}
		const int NCult = int(SeedIdx.size());

		// ---- culture regions: multi-source Dijkstra (land 1, sea 4, ice 3)
		std::vector<int32_t> Region(size_t(N), -1);
		{
			std::vector<double> D(size_t(N), kInf);
			using FItem = std::pair<double, int32_t>;
			std::priority_queue<FItem, std::vector<FItem>, std::greater<FItem>> Q;
			for (int K = 0; K < NCult; ++K)
			{
				const int C = Centres[size_t(SeedIdx[size_t(K)])];
				D[size_t(C)] = 0; Region[size_t(C)] = K; Q.push({ 0.0, C });
			}
			while (!Q.empty())
			{
				const FItem It = Q.top(); Q.pop();
				const int C = It.second;
				if (It.first > D[size_t(C)]) continue;
				for (int K = 0; K < G.Deg(C); ++K)
				{
					const int M = G.Nb(C, K);
					const double S = G.Sea(M) ? 4.0 : (W.ice[size_t(M)] > 0.5f ? 3.0 : 1.0);
					if (D[size_t(C)] + S < D[size_t(M)]) { D[size_t(M)] = D[size_t(C)] + S; Region[size_t(M)] = Region[size_t(C)]; Q.push({ D[size_t(M)], M }); }
				}
			}
		}

		// ---- cultures: culture 0 woke at year 0; the others split from the nearest older culture
		std::vector<int> CultAge(size_t(NCult), LeadAge);
		std::vector<double> Born(size_t(NCult), 0.0);
		std::vector<int> Parent(size_t(NCult), -1);
		for (int K = 1; K < NCult; ++K)
		{
			Born[size_t(K)] = std::floor(Year * (0.12 + 0.5 * double(K) / NCult) + 60.0 * Rng.f());
			double BestA = 1e9;
			for (int J = 0; J < K; ++J)
			{
				const double A = W.geo->angle(Centres[size_t(SeedIdx[size_t(K)])], Centres[size_t(SeedIdx[size_t(J)])]);
				if (A < BestA) { BestA = A; Parent[size_t(K)] = J; }
			}
			CultAge[size_t(K)] = FMath::Max(0, LeadAge - Rng.integer(0, 2));
		}
		CultAge[0] = LeadAge;
		for (int K = 0; K < NCult; ++K)
		{
			dm::Culture& Cu = dm::state::newCulture(W, Parent[size_t(K)]);
			const std::string ParentLang = Parent[size_t(K)] >= 0 ? W.cultures[size_t(Parent[size_t(K)])].lang : std::string();
			Cu.lang = dm::names::language(NameRng, ParentLang);
			const dm::names::CultureNames CN = dm::names::culture(Cu.lang, NameRng);
			Cu.name = CN.name; Cu.adj = CN.adj; Cu.plural = CN.plural;
			Cu.godName = dm::names::god(Cu.lang, NameRng);
			Cu.born = Born[size_t(K)];
			Cu.age = CultAge[size_t(K)];
			Cu.science = 40.0 + 300.0 * Rng.f();
			Cu.traits.piety = 0.2 + 0.7 * Rng.f();
			Cu.traits.aggression = 0.15 + 0.7 * Rng.f();
			Cu.traits.curiosity = 0.2 + 0.7 * Rng.f();
			Cu.traits.seafaring = 0.15 + 0.8 * Rng.f();
			Cu.seen["rain"] = 1.0 + 3.0 * Rng.f();
			Cu.seen["lightning"] = 0.5 * Rng.f();
			if (Parent[size_t(K)] >= 0) Cu.lastSplit = -9999;
		}

		// ---- settlements
		struct FPlan { int32_t Cell; int Culture; double Founded; double Pop; int Tier; double K; };
		std::vector<FPlan> Plans;
		for (int I = 0; I < NS; ++I)
		{
			const int C = Centres[size_t(I)];
			int Cu = Region[size_t(C)];
			if (Cu < 0) Cu = 0;
			FPlan Pl{ C, Cu, 0, 0, 0, 0 };
			bool bSeed = false;
			for (int K = 0; K < NCult; ++K) if (SeedIdx[size_t(K)] == I) { bSeed = true; Pl.Culture = K; }
			const double B = Born[size_t(Pl.Culture)];
			// better sites were settled earlier
			const double Rank = double(I) / FMath::Max(1, NS - 1);
			Pl.Founded = bSeed ? B : std::floor(B + 20.0 + (Year - B - 30.0) * Sat(0.15 + 0.75 * Rank + 0.25 * (Rng.f() - 0.5)));
			Plans.push_back(Pl);
		}
		// capacity estimate at radius 3, population by age of the settlement
		std::vector<int32_t> Ring;
		for (FPlan& Pl : Plans)
		{
			const int Age = W.cultures[size_t(Pl.Culture)].age;
			double K = 0;
			W.geo->ring(Pl.Cell, 3, Ring);
			for (int32_t C : Ring)
			{
				if (G.Sea(C)) K += 4.0 * W.fauna[size_t(C)] * std::sqrt(FMath::Min(kTechYield[Age], 10.0)) * 0.5;
				else K += FMath::Max(FarmPot(W, C, Age), Forage(W, C));
			}
			const double Years = Year - Pl.Founded;
			const double F = (0.12 + 0.8 * Sat(Years / 900.0)) * (0.55 + 0.45 * Rng.f());
			Pl.K = K;
			Pl.Pop = FMath::Max(8.0 + 30.0 * Rng.f(), K * F);
			Pl.Tier = TierFor(Pl.Pop, Age);
		}
		for (const FPlan& Pl : Plans)
		{
			dm::Settlement& S = dm::state::newSettlement(W, Pl.Cell, Pl.Culture);
			const dm::Culture& Cu = W.cultures[size_t(Pl.Culture)];
			S.name = dm::names::place(Cu.lang, NameRng);
			S.founded = Pl.Founded;
			S.lastExpand = FMath::Max(Pl.Founded, Year - 5.0 - 30.0 * Rng.f());
			S.pop = std::floor(Pl.Pop);
			S.tier = Pl.Tier;
			S.radius = 2 + Pl.Tier;
			S.K = std::floor(Pl.K * (1.0 + 0.15 * Pl.Tier));
			S.growth = std::floor(S.pop * 0.012 * (Rng.f() - 0.2));
			S.devotion = 0.3 + 0.55 * Rng.f();
			S.love = -0.15 + 0.7 * Rng.f();
			S.jaded = 0.25 * Rng.f();
		}

		// ---- territory (Civ.claim rules: land, nearer centre wins, ties lower id)
		{
			std::vector<uint8_t> Best(size_t(N), 255);
			for (dm::Settlement& S : W.settlements)
			{
				// hop distance from the centre, BFS up to the territory radius
				std::vector<int32_t> Local;
				std::vector<int32_t> LocalHop;
				{
					std::vector<int32_t> Q{ S.cell };
					std::vector<int32_t> DistMap;   // parallel to Q
					DistMap.push_back(0);
					std::vector<uint8_t> Seen(size_t(N), 0);
					Seen[size_t(S.cell)] = 1;
					for (size_t H = 0; H < Q.size(); ++H)
					{
						const int A = Q[H];
						const int D = DistMap[H];
						Local.push_back(A); LocalHop.push_back(D);
						if (D >= S.radius) continue;
						for (int K = 0; K < G.Deg(A); ++K)
						{
							const int M = G.Nb(A, K);
							if (Seen[size_t(M)]) continue;
							Seen[size_t(M)] = 1;
							Q.push_back(M); DistMap.push_back(D + 1);
						}
					}
				}
				for (size_t I = 0; I < Local.size(); ++I)
				{
					const int C = Local[I];
					if (G.Sea(C)) continue;
					const int D = LocalHop[I];
					const int Cur = W.owner[size_t(C)];
					if (Cur < 0 || D < Best[size_t(C)]) { W.owner[size_t(C)] = int16_t(S.id); Best[size_t(C)] = uint8_t(D); }
				}
			}
			for (dm::Settlement& S : W.settlements) S.cells.clear();
			for (int C = 0; C < N; ++C) if (W.owner[size_t(C)] >= 0) W.settlements[size_t(W.owner[size_t(C)])].cells.push_back(C);
			// the centre always belongs to its settlement
			for (dm::Settlement& S : W.settlements)
				if (W.owner[size_t(S.cell)] != S.id)
				{
					const int Old = W.owner[size_t(S.cell)];
					if (Old >= 0) { auto& V = W.settlements[size_t(Old)].cells; V.erase(std::remove(V.begin(), V.end(), S.cell), V.end()); }
					W.owner[size_t(S.cell)] = int16_t(S.id);
					S.cells.push_back(S.cell);
					std::sort(S.cells.begin(), S.cells.end());
				}
		}

		// ---- farms, urban cores, buildings
		for (dm::Settlement& S : W.settlements)
		{
			const int Age = W.cultures[size_t(S.culture)].age;
			std::vector<std::pair<double, int32_t>> Pot;
			bool bSea = false, bRock = false;
			for (int32_t C : S.cells)
			{
				Pot.push_back({ FarmPot(W, C, Age), C });
				if (W.slope[size_t(C)] > 0.6f || W.biome[size_t(C)] == dm::Biome::ROCK) bRock = true;
				for (int K = 0; K < G.Deg(C); ++K) if (G.Sea(G.Nb(C, K))) bSea = true;
			}
			std::stable_sort(Pot.begin(), Pot.end(), [](const auto& A, const auto& B) { return A.first != B.first ? A.first > B.first : A.second < B.second; });
			double Sum = 0;
			for (const auto& PC : Pot)
			{
				if (PC.first <= 0.0 || Sum >= 1.15 * S.pop) break;
				const float Fm = float(0.65 + 0.35 * Rng.f());
				W.farm[size_t(PC.second)] = Fm;
				W.veg[size_t(PC.second)] = W.veg[size_t(PC.second)] * (1.f - 0.75f * Fm);
				Sum += PC.first;
			}
			W.urban[size_t(S.cell)] = kUrbanCore[S.tier];
			if (S.tier >= 3)
			{
				W.geo->ring(S.cell, S.tier >= 4 ? 2 : 1, Ring);
				for (int32_t C : Ring)
				{
					if (C == S.cell || G.Sea(C)) continue;
					const bool bInner = W.geo->angle(C, S.cell) < 1.5 * W.geo->step;
					const float U = S.tier >= 4 ? (bInner ? 0.8f : 0.4f) : 0.5f;
					W.urban[size_t(C)] = FMath::Max(W.urban[size_t(C)], U);
				}
			}
			auto Give = [&](dm::Building::Id B, bool bCond, double Chance) { if (bCond && Rng.chance(Chance)) S.b[size_t(B)] = 1; };
			Give(dm::Building::SHRINE, S.pop >= 30 && S.devotion >= 0.3, 0.95);
			Give(dm::Building::TEMPLE, Age >= dm::Age::BRONZE && S.tier >= 2 && S.devotion >= 0.35, 0.9);
			Give(dm::Building::GRANARY, Age >= dm::Age::BRONZE && S.tier >= 1, 0.9);
			Give(dm::Building::WALLS, Age >= dm::Age::BRONZE && S.tier >= 2, 0.55);
			Give(dm::Building::PORT, Age >= dm::Age::IRON && S.tier >= 1 && bSea, 0.95);
			Give(dm::Building::MARKET, Age >= dm::Age::IRON && S.tier >= 2, 0.9);
			Give(dm::Building::MINE, Age >= dm::Age::IRON && bRock, 0.8);
			Give(dm::Building::ACADEMY, Age >= dm::Age::SAIL && S.tier >= 3, 0.9);
			Give(dm::Building::FACTORY, Age >= dm::Age::INDUSTRY && S.tier >= 3, 0.95);
			Give(dm::Building::PLANT, Age >= dm::Age::ATOMIC && S.tier >= 3, 0.8);
		}

		// ---- capitals, culture stats, a wonder, prophets, prayers, plague
		for (dm::Culture& Cu : W.cultures)
		{
			int Cap = -1; double BestPop = -1; double Pop = 0; int Towns = 0;
			for (const dm::Settlement& S : W.settlements)
			{
				if (S.culture != Cu.id) continue;
				Pop += S.pop; ++Towns;
				if (S.pop > BestPop) { BestPop = S.pop; Cap = S.id; }
			}
			Cu.pop = Pop; Cu.towns = Towns; Cu.capital = Cap;
			if (Cap >= 0)
			{
				dm::Settlement& S = W.settlements[size_t(Cap)];
				S.capital = true;
				if (Cu.age >= dm::Age::IRON && Rng.chance(0.6)) S.b[dm::Building::WONDER] = 1;
				if (Cu.age >= dm::Age::SPACE) { S.b[dm::Building::LAUNCH] = 1; S.ark = 0.2 + 0.4 * Rng.f(); }
			}
			if (Towns == 0) { Cu.dead = true; }
		}
		{
			// one prophet, one edict, a few prayers, one plague, some blessings
			const int Pc = Rng.integer(0, NCult - 1);
			dm::Culture& Cu = W.cultures[size_t(Pc)];
			if (Cu.capital >= 0)
			{
				Cu.prophet = dm::Prophet{ dm::names::person(Cu.lang, NameRng), Cu.capital, Year + 12.0 };
				Cu.edict = dm::Edict::Build;
				Cu.edictSince = Year - 4.0;
			}
			int Prayers = 0, Plagues = 0;
			for (dm::Settlement& S : W.settlements)
			{
				if (Prayers < 3 && W.moist[size_t(S.cell)] < 0.3f && Rng.chance(0.5)) { S.prayer = dm::Prayer{ dm::PrayerKind::Rain, Year - 2.0 }; ++Prayers; }
				else if (Plagues < 1 && S.tier >= 2 && Rng.chance(0.08)) { S.plague = 0.35; S.prayer = dm::Prayer{ dm::PrayerKind::Heal, Year - 1.0 }; ++Plagues; }
				else if (Rng.chance(0.05)) S.blessed = 3 + Rng.integer(0, 5);
				else if (Rng.chance(0.04)) S.inspire = 2 + Rng.integer(0, 6);
			}
		}

		// ---- relations: contact between neighbouring regions, trade, one war
		struct FPair { int A, B; double Ang; };
		std::vector<FPair> Contacts;
		for (int A = 0; A < NCult; ++A)
			for (int B = A + 1; B < NCult; ++B)
			{
				double Best = 1e9;
				for (const dm::Settlement& SA : W.settlements)
				{
					if (SA.culture != A) continue;
					for (const dm::Settlement& SB : W.settlements)
						if (SB.culture == B) Best = FMath::Min(Best, W.geo->angle(SA.cell, SB.cell));
				}
				if (Best < 30.0 * W.geo->step) Contacts.push_back({ A, B, Best });
			}
		int WarA = -1, WarB = -1;
		if (!Contacts.empty())
		{
			std::stable_sort(Contacts.begin(), Contacts.end(), [](const FPair& X, const FPair& Y) { return X.Ang < Y.Ang; });
			for (const FPair& Pr : Contacts)
			{
				dm::Relation R;
				R.contact = true;
				R.v = -0.3 + 1.0 * Rng.f();
				R.trade = R.v > 0.2 ? 0.3 + 0.6 * Rng.f() : 0.0;
				R.since = Year - 40.0 - 200.0 * Rng.f();
				W.vars.rel[dm::state::relKey(Pr.A, Pr.B)] = R;
			}
			// the closest pair of neighbours with a bold culture is at war
			const FPair& Wp = Contacts.front();
			WarA = W.cultures[size_t(Wp.A)].traits.aggression >= W.cultures[size_t(Wp.B)].traits.aggression ? Wp.A : Wp.B;
			WarB = WarA == Wp.A ? Wp.B : Wp.A;
			dm::Relation& R = W.vars.rel[dm::state::relKey(Wp.A, Wp.B)];
			R.war = true; R.v = -0.7; R.trade = 0; R.since = Year - 6.0; R.aggressor = WarA;
			W.cultures[size_t(WarA)].exhaustion = 0.15; W.cultures[size_t(WarB)].exhaustion = 0.3;
			for (dm::Settlement& S : W.settlements)
				if ((S.culture == WarA || S.culture == WarB) && S.tier >= 2) S.b[dm::Building::WALLS] = 1;
		}

		// ---- roads between neighbouring settlements (Bronze age and later)
		struct FLink { int A, B; std::vector<int32_t> Path; };
		std::vector<FLink> Links;
		if (LeadAge >= dm::Age::BRONZE)
		{
			for (const dm::Settlement& S : W.settlements)
			{
				// two nearest settlements of the same culture plus one foreign trade partner
				std::vector<std::pair<double, int>> Near;
				for (const dm::Settlement& T : W.settlements)
				{
					if (T.id == S.id) continue;
					const double A = W.geo->angle(S.cell, T.cell);
					if (A > 22.0 * W.geo->step) continue;
					bool bOk = T.culture == S.culture;
					if (!bOk)
					{
						auto It = W.vars.rel.find(dm::state::relKey(S.culture, T.culture));
						bOk = It != W.vars.rel.end() && It->second.trade > 0.3;
					}
					if (bOk) Near.push_back({ A, T.id });
				}
				std::sort(Near.begin(), Near.end());
				int Made = 0;
				for (const auto& Nr : Near)
				{
					if (Made >= 2) break;
					const int A = FMath::Min(S.id, Nr.second), B = FMath::Max(S.id, Nr.second);
					bool bDup = false;
					for (const FLink& L : Links) if (L.A == A && L.B == B) { bDup = true; break; }
					if (bDup) { ++Made; continue; }
					std::vector<int32_t> Pth = P.Find(W.settlements[size_t(A)].cell, W.settlements[size_t(B)].cell, ENav::Land, 60.0);
					if (Pth.size() < 2) continue;
					const int TierA = W.settlements[size_t(A)].tier, TierB = W.settlements[size_t(B)].tier;
					const int Level = FMath::Clamp(1 + (FMath::Min(TierA, TierB) >= 2 ? 1 : 0) + (FMath::Min(TierA, TierB) >= 3 ? 1 : 0), 1, 3);
					for (int32_t C : Pth) if (!G.Sea(C)) W.road[size_t(C)] = uint8_t(FMath::Max(int(W.road[size_t(C)]), Level));
					Links.push_back({ A, B, std::move(Pth) });
					++Made;
				}
			}
		}

		// ---- units on real paths
		auto MakeUnit = [&](dm::UnitType Type, int Culture, int From, int To, int Dest, std::vector<int32_t> Path, double Speed, double Size, dm::NavalMode Nav)
		{
			if (Path.size() < 2) return;
			dm::Unit& U = dm::state::newUnit(W, Type);
			U.culture = Culture; U.from = From; U.to = To; U.dest = Dest;
			U.path = std::move(Path);
			U.i = Rng.integer(0, int(U.path.size()) - 2);
			U.t = Rng.f();
			U.speed = Speed; U.size = std::floor(Size); U.naval = Nav;
			U.sea = G.Sea(U.path[size_t(U.i)]);
			U.born = Year - 0.5 - 3.0 * Rng.f();
		};
		const int UnitTarget = FMath::Max(0, O.Units);
		// caravans along roads
		for (size_t I = 0; I < Links.size() && int(W.units.size()) < UnitTarget * 45 / 100; ++I)
		{
			const FLink& L = Links[(I * 7) % Links.size()];
			if (!Rng.chance(0.6)) continue;
			const bool bFwd = Rng.chance(0.5);
			const int From = bFwd ? L.A : L.B, To = bFwd ? L.B : L.A;
			std::vector<int32_t> Pth = L.Path;
			if (!bFwd) std::reverse(Pth.begin(), Pth.end());
			MakeUnit(dm::UnitType::Trader, W.settlements[size_t(From)].culture, From, To, W.settlements[size_t(To)].cell, std::move(Pth), 7.0, 20 + 40 * Rng.f(), dm::NavalMode::Land);
		}
		// ships between ports (Sail age: open sea; Iron: coastal)
		if (LeadAge >= dm::Age::IRON)
		{
			const ENav Nav = LeadAge >= dm::Age::SAIL ? ENav::Open : ENav::Coastal;
			std::vector<int> Ports;
			for (const dm::Settlement& S : W.settlements) if (S.has(dm::Building::PORT)) Ports.push_back(S.id);
			int Ships = 0;
			for (size_t I = 0; I < Ports.size() && Ships < UnitTarget / 4; ++I)
			{
				const dm::Settlement& A = W.settlements[size_t(Ports[I])];
				int Best = -1; double BestA = 1e9;
				for (size_t J = 0; J < Ports.size(); ++J)
				{
					const dm::Settlement& B = W.settlements[size_t(Ports[J])];
					const double Ang = W.geo->angle(A.cell, B.cell);
					if (B.id == A.id || Ang < 10.0 * W.geo->step || Ang > 45.0 * W.geo->step) continue;
					if (A.culture != B.culture)
					{
						auto It = W.vars.rel.find(dm::state::relKey(A.culture, B.culture));
						if (It == W.vars.rel.end() || It->second.war) continue;
					}
					if (Ang < BestA) { BestA = Ang; Best = B.id; }
				}
				if (Best < 0) continue;
				std::vector<int32_t> Pth = P.Find(A.cell, W.settlements[size_t(Best)].cell, Nav, 120.0);
				int SeaCells = 0;
				for (int32_t C : Pth) if (G.Sea(C)) ++SeaCells;
				if (SeaCells < 5) continue;
				const size_t Before = W.units.size();
				MakeUnit(dm::UnitType::Trader, A.culture, A.id, Best, W.settlements[size_t(Best)].cell, Pth, 7.0, 30 + 40 * Rng.f(), Nav == ENav::Open ? dm::NavalMode::Open : dm::NavalMode::Coastal);
				if (W.units.size() > Before)
				{
					// put the ship out at sea
					dm::Unit& U = W.units.back();
					std::vector<int> SeaIdx;
					for (int K = 0; K + 1 < int(U.path.size()); ++K) if (G.Sea(U.path[size_t(K)])) SeaIdx.push_back(K);
					if (!SeaIdx.empty()) { U.i = SeaIdx[size_t(Rng.integer(0, int(SeaIdx.size()) - 1))]; U.sea = true; }
					++Ships;
				}
			}
		}
		// settlers toward empty good sites
		{
			std::vector<int32_t> Owned;
			for (int C = 0; C < N; ++C) if (W.owner[size_t(C)] >= 0) Owned.push_back(C);
			std::vector<uint8_t> OwnDist;
			G.Hops(Owned, 8, OwnDist, [](int) { return true; });
			int Settlers = 0;
			for (const FSite& St : Sites)
			{
				if (Settlers >= FMath::Max(2, UnitTarget / 8)) break;
				if (OwnDist[size_t(St.Cell)] < 7) continue;
				// nearest live settlement as origin
				int From = -1; double BestA = 1e9;
				for (const dm::Settlement& S : W.settlements)
				{
					const double Ang = W.geo->angle(S.cell, St.Cell);
					if (Ang < BestA) { BestA = Ang; From = S.id; }
				}
				if (From < 0 || BestA > 20.0 * W.geo->step) continue;
				std::vector<int32_t> Pth = P.Find(W.settlements[size_t(From)].cell, St.Cell, ENav::Land, 45.0);
				if (Pth.size() < 4) continue;
				MakeUnit(dm::UnitType::Settler, W.settlements[size_t(From)].culture, From, -1, St.Cell, std::move(Pth), 5.0, 12 + 30 * Rng.f(), dm::NavalMode::Land);
				W.units.back().i = FMath::Min(W.units.back().i, int(W.units.back().path.size()) / 2);
				++Settlers;
				// keep later settlers 9 hops away from this destination
				std::vector<int32_t> Dst{ St.Cell };
				std::vector<uint8_t> Dd;
				G.Hops(Dst, 9, Dd, [](int) { return true; });
				for (int C = 0; C < N; ++C) OwnDist[size_t(C)] = FMath::Min(OwnDist[size_t(C)], uint8_t(Dd[size_t(C)] < 9 ? 0 : 255));
			}
		}
		// the war: armies from the aggressor's border towns toward the enemy
		if (WarA >= 0)
		{
			int Armies = 0;
			for (const dm::Settlement& S : W.settlements)
			{
				if (S.culture != WarA || Armies >= 3) continue;
				int Tgt = -1; double BestA = 1e9;
				for (const dm::Settlement& T : W.settlements)
				{
					if (T.culture != WarB) continue;
					const double Ang = W.geo->angle(S.cell, T.cell);
					if (Ang < BestA) { BestA = Ang; Tgt = T.id; }
				}
				if (Tgt < 0 || BestA > 25.0 * W.geo->step) continue;
				std::vector<int32_t> Pth = P.Find(S.cell, W.settlements[size_t(Tgt)].cell, LeadAge >= dm::Age::BRONZE ? ENav::Coastal : ENav::Land, 80.0);
				if (Pth.size() < 3) continue;
				MakeUnit(dm::UnitType::Army, WarA, S.id, Tgt, W.settlements[size_t(Tgt)].cell, std::move(Pth), 5.0, 80 + 220 * Rng.f(), LeadAge >= dm::Age::BRONZE ? dm::NavalMode::Coastal : dm::NavalMode::Land);
				++Armies;
			}
		}
		// the prophet walks to a sister town
		for (const dm::Culture& Cu : W.cultures)
		{
			if (!Cu.prophet || Cu.capital < 0) continue;
			const dm::Settlement& From = W.settlements[size_t(Cu.capital)];
			int Tgt = -1; double BestA = 1e9;
			for (const dm::Settlement& T : W.settlements)
			{
				if (T.culture != Cu.id || T.id == From.id) continue;
				const double Ang = W.geo->angle(From.cell, T.cell);
				if (Ang < BestA) { BestA = Ang; Tgt = T.id; }
			}
			if (Tgt < 0) continue;
			std::vector<int32_t> Pth = P.Find(From.cell, W.settlements[size_t(Tgt)].cell, ENav::Coastal, 80.0);
			MakeUnit(dm::UnitType::Prophet, Cu.id, From.id, Tgt, W.settlements[size_t(Tgt)].cell, std::move(Pth), 4.0, 1, dm::NavalMode::Coastal);
		}

		// ---- a storm over warm sea near land
		if (O.bDisasters)
		{
			// the warmest open-sea cell in the trade-wind belts, a few hops off a coast
			int Best = -1; double BestScore = -1e9;
			for (int C = 0; C < N; ++C)
			{
				const double Lat = std::fabs(double(W.geo->lat[size_t(C)])) * 180.0 / dm::kPi;
				if (!G.Sea(C) || Lat < 8.0 || Lat > 30.0 || W.coast[size_t(C)] < 3 || W.coast[size_t(C)] > 14 || W.ice[size_t(C)] > 0.05f) continue;
				const double Score = W.temp[size_t(C)] + 2.0 * Rng.f();
				if (Score > BestScore) { BestScore = Score; Best = C; }
			}
			if (Best >= 0)
			{
				dm::Disaster& D = dm::state::newDisaster(W, dm::DisasterType::Storm, Best);
				D.t = 0.4; D.dur = 2.0; D.power = 0.85; D.ang = 0.14; D.natural = true;
			}
		}

		// ---- the chronicle
		{
			const dm::Culture& C0 = W.cultures[0];
			const int Cap0 = C0.capital >= 0 ? C0.capital : 0;
			struct FE { double Y; const char* T; std::string Tx; int Cell; int Cu; int Wt; };
			std::vector<FE> Es;
			Es.push_back({ 0.0, "awaken", "In " + YearText(0) + " the " + C0.plural.substr(C0.plural.rfind(' ') + 1) + " woke beneath an open sky, and named the one who woke them " + C0.godName + ".", W.settlements[size_t(Cap0)].cell, 0, 3 });
			for (const dm::Culture& Cu : W.cultures)
			{
				if (Cu.parent < 0) continue;
				Es.push_back({ Cu.born, "culture", "In " + YearText(Cu.born) + " the " + Cu.adj + " folk turned from the ways of " + W.cultures[size_t(Cu.parent)].plural + " and spoke a new tongue.", Cu.capital >= 0 ? W.settlements[size_t(Cu.capital)].cell : -1, Cu.id, 2 });
			}
			for (const dm::Settlement& S : W.settlements)
			{
				if (!S.capital && S.tier < 3) continue;
				Es.push_back({ S.founded, "founded", "In " + YearText(S.founded) + " " + S.name + " was raised by the " + W.cultures[size_t(S.culture)].adj + " settlers.", S.cell, S.culture, S.capital ? 2 : 1 });
				if (S.has(dm::Building::WONDER))
					Es.push_back({ Year - 80.0 - 300.0 * Rng.f(), "wonder", "In " + YearText(Year - 120.0) + " a wonder rose at " + S.name + ", and the sky itself seemed to lean closer.", S.cell, S.culture, 3 });
			}
			if (WarA >= 0)
				Es.push_back({ Year - 6.0, "war", "In " + YearText(Year - 6.0) + " the " + W.cultures[size_t(WarA)].plural.substr(W.cultures[size_t(WarA)].plural.rfind(' ') + 1) + " took up spears against the " + W.cultures[size_t(WarB)].adj + " towns.", -1, WarA, 2 });
			for (const dm::Culture& Cu : W.cultures)
				if (Cu.prophet) Es.push_back({ Year - 3.0, "prophet", "In " + YearText(Year - 3.0) + " the prophet " + Cu.prophet->name + " walked out of " + W.settlements[size_t(Cu.prophet->settlement)].name + " speaking of " + Cu.godName + ".", W.settlements[size_t(Cu.prophet->settlement)].cell, Cu.id, 2 });
			std::stable_sort(Es.begin(), Es.end(), [](const FE& A, const FE& B) { return A.Y < B.Y; });
			for (const FE& E : Es) AddEntry(W, E.Y, E.T, E.Tx, E.Cell, E.Cu, E.Wt);
		}

		// ---- god, stats, world scalars
		W.god.awe = 140; W.god.aweMax = 260; W.god.mercy = 0.35; W.god.wrath = 0.12;
		W.god.deeds = { { "rain", 14 }, { "bloom", 6 }, { "heal", 4 }, { "lightning", 3 }, { "inspire", 2 } };
		int Deeds = 0;
		for (const auto& D : W.god.deeds) Deeds += D.second;
		W.stats.miracles = Deeds;
		W.stats.wars = WarA >= 0 ? 3 : 1;
		double Pop = 0, Bel = 0;
		for (const dm::Settlement& S : W.settlements) { Pop += S.pop; Bel += S.pop * S.devotion; }
		W.stats.pop = Pop; W.stats.peakPop = std::floor(Pop * 1.04); W.stats.believers = std::floor(Bel);
		int MaxAge = 0;
		for (const dm::Culture& Cu : W.cultures) if (!Cu.dead) MaxAge = FMath::Max(MaxAge, Cu.age);
		W.age = MaxAge;

		// ---- report
		Rep.Settlements = int(W.settlements.size());
		Rep.Cultures = int(W.cultures.size());
		Rep.Units = int(W.units.size());
		Rep.Disasters = int(W.disasters.size());
		Rep.Chronicle = int(W.chronicle.size());
		Rep.Pop = Pop;
		double HabS = 0, HabL = 0; int NL = 0;
		for (const dm::Settlement& S : W.settlements) { Rep.Tiers[S.tier]++; HabS += W.hab[size_t(S.cell)]; }
		for (int C = 0; C < N; ++C) if (!G.Sea(C)) { HabL += W.hab[size_t(C)]; ++NL; if (W.road[size_t(C)]) Rep.RoadCells++; }
		Rep.MeanHabSettled = Rep.Settlements ? HabS / Rep.Settlements : 0;
		Rep.MeanHabLand = NL ? HabL / NL : 0;
	}
}

// ====================================================================== public

FString DmFixture::ResolveLabPath(const FString& NameOrPath)
{
	FString Name = NameOrPath.IsEmpty() ? FString(TEXT("planet_7")) : NameOrPath;
	if (Name.Contains(TEXT("/")) || Name.Contains(TEXT("\\")) || Name.EndsWith(TEXT(".lab")))
		return FPaths::ConvertRelativePathToFull(Name);
	if (Name.IsNumeric()) Name = TEXT("planet_") + Name;
	FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	for (int32 Up = 0; Up < 6; ++Up)
	{
		const FString Cand = FPaths::Combine(Dir, TEXT("UnrealLab/data"), Name + TEXT(".lab"));
		if (FPaths::FileExists(Cand)) return Cand;
		const FString Parent = FPaths::GetPath(Dir.EndsWith(TEXT("/")) ? Dir.LeftChop(1) : Dir);
		if (Parent.IsEmpty() || Parent == Dir) break;
		Dir = Parent;
	}
	return FString();
}

std::unique_ptr<dm::World> DmFixture::Build(const FString& LabPath, const FDmFixtureOptions& Opts, FString& OutError, FDmFixtureReport* OutReport)
{
	const double T0 = FPlatformTime::Seconds();
	FLab L;
	if (!ReadLab(LabPath, L, OutError)) return nullptr;

	// the lab grid must be the sim grid at some level
	int Level = -1;
	for (int K = 0; K <= dm::Geo::kMaxLevel; ++K) if (dm::Geo::prefix(K) == L.N) Level = K;
	if (Level < 0) { OutError = FString::Printf(TEXT("n = %d is not an icosphere level"), L.N); return nullptr; }
	dm::state::CreateOpts CO;
	CO.seed = Opts.Seed;
	CO.name = "Fixture";
	CO.L = Level;
	std::unique_ptr<dm::World> WPtr = dm::state::create(CO);
	World& W = *WPtr;
	if (W.n != L.N) { OutError = TEXT("grid size mismatch"); return nullptr; }
	double MaxDev = 0;
	for (size_t I = 0; I < L.Pos.size(); ++I) MaxDev = FMath::Max(MaxDev, std::fabs(double(L.Pos[I]) - double(W.geo->pos[I])));
	if (MaxDev > 1e-4) { OutError = FString::Printf(TEXT("cell positions differ from dm::Geo (max %.6f)"), MaxDev); return nullptr; }

	W.seaLevel = Finite(L.SeaLevel);
	for (int C = 0; C < W.n; ++C)
	{
		const size_t I = size_t(C);
		W.elev[I] = FMath::Clamp(Finite(L.Elev[I]), float(dm::state::kElevMin), float(dm::state::kElevMax));
		W.temp[I] = Finite(L.Temp[I]);
		W.moist[I] = FMath::Clamp(Finite(L.Moist[I]), 0.f, 1.f);
		W.veg[I] = FMath::Clamp(Finite(L.Veg[I]), 0.f, 1.f);
		W.flow[I] = FMath::Max(0.f, Finite(L.Flow[I]));
		W.ice[I] = FMath::Clamp(Finite(L.Ice[I]), 0.f, 1.f);
		W.hab[I] = FMath::Clamp(Finite(L.Hab[I]), 0.f, 1.f);
		W.slope[I] = FMath::Max(0.f, Finite(L.Slope[I]));
		W.water[I] = FMath::Clamp(Finite(L.Water[I]), 0.f, 8.f);
		W.biome[I] = L.Biome[I] < dm::Biome::COUNT ? L.Biome[I] : uint8_t(dm::Biome::BARREN);
	}
	DeriveFields(W, std::isfinite(L.RiverT) && L.RiverT > 0 ? L.RiverT : 5.f);

	// world scalars of a world in History
	W.phase = dm::Phase::History;
	W.heat = 0;
	W.vars.lifeBoost = 1;
	W.vars.t0 = 1200;
	W.name = "Fixture";

	FDmFixtureReport Rep;
	GenerateHistory(W, Opts, Rep);
	W.tick = W.vars.t0 + int64_t(std::floor(W.year * 4.0));

	// everything derived is filled: nothing for the sim to recompute; the client must draw everything
	W.need = dm::Need{ false, false, false, false };
	W.dirty = dm::Dirty{};
	if (!W.cultures.empty())
	{
		dm::Rng NR(Opts.Seed ^ 0x5bd1e995u);
		W.name = dm::names::planet(NR);
	}

	Rep.Rivers = int(W.rivers.size());
	Rep.SaveBytes = int(dm::state::sizeOf(W));
	Rep.BuildMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	Rep.Summary = FString::Printf(TEXT("\"%s\": %d settlements (camp %d, village %d, town %d, city %d, metropolis %d), pop %.0f, %d cultures, %d units, %d rivers, %d road cells, %d disasters, %d chronicle entries; mean hab under settlements %.2f vs land %.2f; save %d bytes; %.0f ms"),
		UTF8_TO_TCHAR(W.name.c_str()), Rep.Settlements, Rep.Tiers[0], Rep.Tiers[1], Rep.Tiers[2], Rep.Tiers[3], Rep.Tiers[4], Rep.Pop, Rep.Cultures, Rep.Units,
		Rep.Rivers, Rep.RoadCells, Rep.Disasters, Rep.Chronicle, Rep.MeanHabSettled, Rep.MeanHabLand, Rep.SaveBytes, Rep.BuildMs);
	if (OutReport) *OutReport = Rep;
	return WPtr;
}

FString DmFixture::Validate(const dm::World& W)
{
	if (!W.geo || W.n <= 0) return TEXT("no grid");
	// finite cell fields
	for (int F = 0; F < dm::state::kCellFieldCount; ++F)
	{
		const dm::CellArray A = dm::state::cellArray(W, F);
		if (!A.data || A.size != size_t(W.n)) return FString::Printf(TEXT("field %s has the wrong size"), UTF8_TO_TCHAR(dm::state::kCellFields[F].name));
		if (A.type != dm::FieldType::F32) continue;
		const float* P = static_cast<const float*>(A.data);
		for (size_t C = 0; C < A.size; ++C)
			if (!std::isfinite(P[C])) return FString::Printf(TEXT("%s[%d] is not finite"), UTF8_TO_TCHAR(dm::state::kCellFields[F].name), int32(C));
	}
	for (size_t I = 0; I < W.cultures.size(); ++I)
		if (W.cultures[I].id != int(I)) return FString::Printf(TEXT("culture %d has id %d"), int32(I), W.cultures[I].id);
	for (size_t I = 0; I < W.settlements.size(); ++I)
	{
		const dm::Settlement& S = W.settlements[I];
		if (S.id != int(I)) return FString::Printf(TEXT("settlement %d has id %d"), int32(I), S.id);
		if (!W.valid(S.cell) || dm::state::isSea(W, S.cell)) return FString::Printf(TEXT("settlement %d is not on land"), S.id);
		if (S.culture < 0 || S.culture >= int(W.cultures.size())) return FString::Printf(TEXT("settlement %d names culture %d"), S.id, S.culture);
		if (!S.dead && W.owner[size_t(S.cell)] != S.id) return FString::Printf(TEXT("settlement %d does not own its centre"), S.id);
		if (!S.dead && W.hab[size_t(S.cell)] < 0.05f) return FString::Printf(TEXT("settlement %d on uninhabitable land (hab %.3f)"), S.id, W.hab[size_t(S.cell)]);
		if (S.tier < 0 || S.tier > 4 || S.radius < 0 || S.radius > 64) return FString::Printf(TEXT("settlement %d tier / radius"), S.id);
		if (!std::isfinite(S.pop) || S.pop < 3) return FString::Printf(TEXT("settlement %d pop %f"), S.id, S.pop);
	}
	for (int C = 0; C < W.n; ++C)
	{
		const int O = W.owner[size_t(C)];
		if (O < -1 || O >= int(W.settlements.size())) return FString::Printf(TEXT("owner[%d] = %d"), C, O);
		if (O >= 0 && dm::state::isSea(W, C)) return FString::Printf(TEXT("sea cell %d is owned"), C);
	}
	for (const dm::Unit& U : W.units)
	{
		if (U.path.size() < 2 || U.i < 0 || U.i >= int(U.path.size()) - 1) return FString::Printf(TEXT("unit %d path index"), U.id);
		for (size_t K = 1; K < U.path.size(); ++K)
		{
			const int A = U.path[K - 1], B = U.path[K];
			bool bNb = false;
			for (int J = 0; J < W.geo->nbrN[size_t(A)]; ++J) if (W.geo->nbr[size_t(6 * A + J)] == B) bNb = true;
			if (!bNb) return FString::Printf(TEXT("unit %d path step %d -> %d is not a neighbour"), U.id, A, B);
		}
		if (U.from < 0 || U.from >= int(W.settlements.size()) || U.to < -1 || U.to >= int(W.settlements.size())) return FString::Printf(TEXT("unit %d from / to"), U.id);
		if (U.sea != dm::state::isSea(W, U.path[size_t(U.i)])) return FString::Printf(TEXT("unit %d sea flag"), U.id);
	}
	std::vector<uint8_t> A, B;
	std::string Why;
	if (!dm::state::serialize(W, A, &Why)) return FString::Printf(TEXT("serialize refused: %s"), UTF8_TO_TCHAR(Why.c_str()));
	std::unique_ptr<dm::World> Back = dm::state::deserialize(A, &Why);
	if (!Back) return FString::Printf(TEXT("deserialize refused: %s"), UTF8_TO_TCHAR(Why.c_str()));
	if (!dm::state::serialize(*Back, B, &Why) || A != B) return TEXT("re-serialized bytes differ");
	return FString();
}
