// DEMIURGE — SimTests/stubs/Path_stub.cpp : neutral stand-in for Sim/Path.cpp (see StubHooks.h).
// Owner: path-units engineer (keep it matching Path.h). Written by foundation-api.
// Neutral: nothing is reachable: find returns an empty path ("null"), costs are kNoPath, reach
// visits nothing.
#include "StubHooks.h"

DM_STUB_MODULE(path);

namespace dm::path {

std::vector<int32_t> find(const World& w, int from, int to, const FindOpts& opts, double* cost) {
	DM_STUB(path_find, w, from, to, opts, cost);
	if (cost) *cost = kNoPath;
	return {};
}
double stepCost(const World& w, int a, int b, NavalMode naval) {
	DM_STUB(path_stepCost, w, a, b, naval);
	return kNoPath;
}
int reach(const World& w, int from, double maxCost, NavalMode naval, const Visit& visit) {
	DM_STUB(path_reach, w, from, maxCost, naval, visit);
	return 0;
}
double cost(const World& w, const std::vector<int32_t>& cells, NavalMode naval) {
	DM_STUB(path_cost, w, cells, naval);
	return kNoPath;
}

} // namespace dm::path
