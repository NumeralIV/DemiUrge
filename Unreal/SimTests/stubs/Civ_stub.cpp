// DEMIURGE — SimTests/stubs/Civ_stub.cpp : neutral stand-in for the lifecycle half of the Civ
// namespace (Sim/Civ.cpp; see StubHooks.h). Owner: civ engineer (keep it matching the [civ]
// declarations of Civ.h). Written by foundation-api.
// Neutral: found refuses (nullptr), every mutation is a no-op, damage kills 0, electCapital -1,
// infect / cure false, canSettle { false, Sea }, siteScore 0, bestSite -1, ageProgress 0.
// The pure reads live, at and near answer honestly from w.settlements / owner[] (other modules
// iterate settlements through them; tests build settlements with state::newSettlement).
#include "StubHooks.h"

#include <algorithm>

DM_STUB_MODULE(civ);

namespace dm::civ {

Settlement* found(World& w, int cell, int cultureId, double pop, FoundedBy by, const FoundOpts& opts) {
	DM_STUB(civ_found, w, cell, cultureId, pop, by, opts);
	return nullptr;
}
void claim(World& w, int sid) {
	DM_STUB(civ_claim, w, sid);
}
void rebuild(World& w) {
	DM_STUB(civ_rebuild, w);
}
void destroy(World& w, int sid, Cause cause) {
	DM_STUB(civ_destroy, w, sid, cause);
}
double damage(World& w, int sid, double frac, Cause cause) {
	DM_STUB(civ_damage, w, sid, frac, cause);
	return 0.0;
}
void capture(World& w, int sid, int cultureId) {
	DM_STUB(civ_capture, w, sid, cultureId);
}
void setCulture(World& w, int sid, int cultureId) {
	DM_STUB(civ_setCulture, w, sid, cultureId);
}
int electCapital(World& w, int cultureId) {
	DM_STUB(civ_electCapital, w, cultureId);
	return -1;
}
bool infect(World& w, int sid, double severity) {
	DM_STUB(civ_infect, w, sid, severity);
	return false;
}
bool cure(World& w, int sid, double immuneYears) {
	DM_STUB(civ_cure, w, sid, immuneYears);
	return false;
}
int at(const World& w, int cell) {
	DM_STUB(civ_at, w, cell);
	if (!w.valid(cell)) return -1;
	for (const Settlement& s : w.settlements)
		if (!s.dead && s.cell == cell) return s.id;
	const int o = w.owner[size_t(cell)];
	if (o >= 0 && size_t(o) < w.settlements.size() && !w.settlements[size_t(o)].dead) return o;
	return -1;
}
std::vector<int> near(const World& w, int cell, double ang) {
	DM_STUB(civ_near, w, cell, ang);
	std::vector<std::pair<double, int>> hits;
	if (w.valid(cell) && w.geo && isFinite(ang)) {
		for (const Settlement& s : w.settlements) {
			if (s.dead || !w.valid(s.cell)) continue;
			const double a = w.geo->angle(cell, s.cell);
			if (a <= ang) hits.emplace_back(a, s.id);
		}
	}
	std::sort(hits.begin(), hits.end());   // nearest first, ties by id
	std::vector<int> ids;
	ids.reserve(hits.size());
	for (const auto& h : hits) ids.push_back(h.second);
	return ids;
}
std::vector<int> live(const World& w) {
	DM_STUB(civ_live, w);
	std::vector<int> ids;
	for (const Settlement& s : w.settlements)
		if (!s.dead) ids.push_back(s.id);
	return ids;
}
SettleCheck canSettle(const World& w, int c) {
	DM_STUB(civ_canSettle, w, c);
	return SettleCheck{};
}
double siteScore(const World& w, int c) {
	DM_STUB(civ_siteScore, w, c);
	return 0.0;
}
int bestSite(const World& w) {
	DM_STUB(civ_bestSite, w);
	return -1;
}
double ageProgress(const World& w, int cultureId) {
	DM_STUB(civ_ageProgress, w, cultureId);
	return 0.0;
}
int bandSite(const World& w, int cell) {
	DM_STUB(civ_bandSite, w, cell);
	return -1;
}
bool consecrate(World& w, int sid) {
	DM_STUB(civ_consecrate, w, sid);
	return false;
}
// (v4 mythos, M0) neutral
int exodus(World& w, int sid, double share) {
	DM_STUB(civ_exodus, w, sid, share);
	return -1;
}
bool demoteTemple(World& w, int sid) {
	DM_STUB(civ_demoteTemple, w, sid);
	return false;
}

} // namespace dm::civ
