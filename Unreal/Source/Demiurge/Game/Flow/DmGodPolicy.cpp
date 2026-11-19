// DEMIURGE — Game/Flow/DmGodPolicy.cpp : see DmGodPolicy.h. Owner: C10 integration.
// Ported from Tools/simrun/Policies.cpp (kind), acting through the control state instead of powers::use.

#include "Game/Flow/DmGodPolicy.h"

#include "Game/Input/DmControl.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "Sim/Civ.h"
#include "Sim/Powers.h"
#include "Sim/Sim.h"
#include "Sim/Society.h"
#include "Sim/State.h"

namespace
{
	bool Alive(const dm::World& W)
	{
		for (const dm::Settlement& S : W.settlements) if (!S.dead) return true;
		for (const dm::Unit& U : W.units) if (U.type == dm::UnitType::Settler) return true;
		return false;
	}

	std::vector<int> ByPop(const dm::World& W)
	{
		std::vector<int> Ids = dm::civ::live(W);
		std::stable_sort(Ids.begin(), Ids.end(), [&](int A, int B)
		{
			const double Pa = W.settlements[size_t(A)].pop, Pb = W.settlements[size_t(B)].pop;
			if (Pa != Pb) return Pa > Pb;
			return A < B;
		});
		return Ids;
	}

	bool AnyWar(const dm::World& W, int Culture)
	{
		for (const dm::Culture& C : W.cultures)
		{
			if (C.dead || C.id == Culture) continue;
			if (dm::society::atWar(W, Culture, C.id)) return true;
		}
		return false;
	}

	int LeadingCulture(const dm::World& W)
	{
		int Best = -1;
		for (const dm::Culture& C : W.cultures)
		{
			if (C.dead) continue;
			if (Best < 0) { Best = C.id; continue; }
			const dm::Culture& B = W.cultures[size_t(Best)];
			if (C.age > B.age || (C.age == B.age && C.pop > B.pop)) Best = C.id;
		}
		return Best;
	}

	std::vector<int> TopOf(const dm::World& W, int Culture, size_t K)
	{
		std::vector<int> Out;
		for (int Sid : ByPop(W))
		{
			if (Culture >= 0 && W.settlements[size_t(Sid)].culture != Culture) continue;
			Out.push_back(Sid);
			if (Out.size() >= K) break;
		}
		return Out;
	}
}

FDmGodPolicy::FDmGodPolicy(uint32 Seed) : Rng(dm::Rng(Seed ^ 0x5eed5eedu).fork("flow:god")) {}

bool FDmGodPolicy::Use(dm::World& W, const char* Id, int32 Cell, float Dt, bool bNew)
{
	if (!Ctl) return false;
	const FString IdS = UTF8_TO_TCHAR(Id);
	if (Ctl->GetArmedPower() != IdS) Ctl->ArmPower(IdS);   // the player's path: arm, then click / brush
	if (Ctl->GetArmedPower() != IdS) return false;
	const bool bOk = Ctl->UsePower(Cell, Dt, bNew);
	if (bOk) LastPower = IdS;
	return bOk;
}

bool FDmGodPolicy::Click(dm::World& W, const char* Id, int32 Cell)
{
	if (!dm::powers::can(W, Id, Cell).ok) return false;
	if (Use(W, Id, Cell, 0.f, true))
	{
		++Clicks;
		ByPower.FindOrAdd(UTF8_TO_TCHAR(Id))++;
		return true;
	}
	++Refused;
	return false;
}

bool FDmGodPolicy::StartStroke(dm::World& W, const char* Id, int32 Cell, int32 Ticks)
{
	if (!StrokeId.empty() || Ticks <= 0) return false;
	if (!dm::powers::can(W, Id, Cell).ok) return false;
	StrokeId = Id;
	StrokeCell = Cell;
	StrokeLeft = Ticks;
	bStrokeFirst = true;
	++Strokes;
	ByPower.FindOrAdd(UTF8_TO_TCHAR(Id))++;
	return true;
}

void FDmGodPolicy::ContinueStroke(dm::World& W)
{
	if (StrokeId.empty()) return;
	const bool bUsed = Use(W, StrokeId.c_str(), StrokeCell, bStrokeFirst ? 0.f : float(dm::sim::kRealPerTick), bStrokeFirst);
	bStrokeFirst = false;
	if (--StrokeLeft <= 0 || !bUsed)
	{
		StrokeId.clear();
		StrokeLeft = 0;
	}
}

bool FDmGodPolicy::Reawaken(dm::World& W)
{
	if (W.phase != dm::Phase::History || Alive(W)) return false;
	const int Site = dm::civ::bestSite(W);
	return W.valid(Site) && Click(W, "awaken", Site);
}

void FDmGodPolicy::Before(dm::World& W, UDmControlSubsystem* Control)
{
	Ctl = Control;
	if (W.phase != dm::Phase::History)
	{
		StrokeId.clear();
		return;
	}
	ContinueStroke(W);
	if (W.year + 1e-9 < NextYear) return;
	NextYear = std::floor(W.year) + 1;
	DecideKind(W);
}

void FDmGodPolicy::DecideKind(dm::World& W)
{
	if (Reawaken(W)) return;
	const std::vector<int> Ids = ByPop(W);
	int Actions = 0;
	// 1. answer prayers, largest settlements first
	for (int Sid : Ids)
	{
		if (Actions >= 2) break;
		const dm::Settlement& S = W.settlements[size_t(Sid)];
		if (!S.prayer) continue;
		const int Cell = S.cell;
		switch (S.prayer->kind)
		{
		case dm::PrayerKind::Heal: if (Click(W, "heal", Cell)) Actions++; break;
		case dm::PrayerKind::Peace: if (Click(W, "peace", Cell)) Actions++; break;
		case dm::PrayerKind::Rain: if (StartStroke(W, "rain", Cell, 8)) Actions++; break;
		case dm::PrayerKind::Bloom: if (StartStroke(W, "bloom", Cell, 8) || StartStroke(W, "rain", Cell, 8)) Actions++; break;
		}
	}
	// 2. plagues nobody prays about yet
	for (int Sid : Ids)
	{
		if (Actions >= 2) break;
		const dm::Settlement& S = W.settlements[size_t(Sid)];
		if (S.plague > 0.2 && Click(W, "heal", S.cell)) Actions++;
	}
	// 3. wars: an Omen of Peace on the largest settlement of a warring culture
	for (int Sid : Ids)
	{
		if (Actions >= 2) break;
		const dm::Settlement& S = W.settlements[size_t(Sid)];
		if (AnyWar(W, S.culture) && Click(W, "peace", S.cell)) Actions++;
	}
	// 4. a prophet (and his edict, as the culture panel sets it) for the leading culture
	const int Lead = LeadingCulture(W);
	for (const dm::Culture& Cu : W.cultures)
	{
		if (Actions >= 2) break;
		if (Cu.dead || Cu.prophet || Cu.capital < 0) continue;
		if (Cu.id != Lead && W.god.awe < 0.8 * W.god.aweMax) continue;
		const dm::Settlement* Cap = dm::state::settlement(W, Cu.capital);
		if (!Cap || Cap->dead) continue;
		if (W.god.awe < dm::powers::cost(W, "prophet") + 20) continue;
		const int Cid = Cu.id;
		if (Click(W, "prophet", Cap->cell))
		{
			Actions++;
			const dm::Edict E = AnyWar(W, Cid) ? dm::Edict::Peace : dm::Edict::Build;
			if (dm::society::setEdict(W, Cid, E)) Edicts++;
		}
	}
	// 5. every year: bloom (or rain) over the least loved of the leading culture's 24 largest towns, inspire with Awe to spare
	if (Lead < 0) return;
	{
		const dm::Culture& Lc = W.cultures[size_t(Lead)];
		const dm::Settlement* Lcap = dm::state::settlement(W, Lc.capital);
		const bool bWantProphet = !Lc.prophet && Lcap && !Lcap->dead && dm::powers::unlocked(W, "prophet");
		if (bWantProphet && W.god.awe < dm::powers::cost(W, "prophet") + 20) return;
	}
	std::vector<int> Top = TopOf(W, Lead, 24);
	std::stable_sort(Top.begin(), Top.end(), [&](int A, int B) { return W.settlements[size_t(A)].love < W.settlements[size_t(B)].love; });
	if (!Top.empty())
	{
		const int Cell = W.settlements[size_t(Top[0])].cell;
		if (!StartStroke(W, "bloom", Cell, 4)) StartStroke(W, "rain", Cell, 4);
	}
	if (Actions < 2 && W.god.awe > 0.6 * W.god.aweMax)
	{
		for (int Sid : Top)
		{
			const dm::Settlement& S = W.settlements[size_t(Sid)];
			if (S.inspire <= 0 && Click(W, "inspire", S.cell)) break;
		}
	}
}
