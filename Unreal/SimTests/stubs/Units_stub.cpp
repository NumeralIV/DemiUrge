// DEMIURGE — SimTests/stubs/Units_stub.cpp : neutral stand-in for Sim/Units.cpp (see StubHooks.h).
// Owner: path-units engineer (keep it matching Units.h). Written by foundation-api; cellOf updated by A6.
// Neutral: spawn refuses (nullptr), step and kill do nothing. The pure reads cellOf, dirOf (no
// interpolation: the direction of the current cell) and near answer honestly from w.units.
#include "StubHooks.h"

#include <algorithm>

DM_STUB_MODULE(units);

namespace dm::units {

Unit* spawn(World& w, const SpawnOpts& opts) {
	DM_STUB(units_spawn, w, opts);
	return nullptr;
}
void step(World& w, double dtYears) {
	DM_STUB(units_step, w, dtYears);
}
bool kill(World& w, int unitId, std::string_view cause) {
	DM_STUB(units_kill, w, unitId, cause);
	return false;
}
std::vector<int> near(const World& w, int cell, double ang) {
	DM_STUB(units_near, w, cell, ang);
	std::vector<int> ids;
	if (!w.valid(cell) || !w.geo || !isFinite(ang)) return ids;
	for (const Unit& u : w.units) {
		const int c = cellOf(w, u);
		if (w.valid(c) && w.geo->angle(cell, c) <= ang) ids.push_back(u.id);
	}
	std::sort(ids.begin(), ids.end());
	return ids;
}
int cellOf(const World& w, const Unit& u) {
	DM_STUB(units_cellOf, w, u);
	// the nearer end of the step the unit is on, as the real Units (Units.h)
	const size_t len = u.path.size();
	if (len == 0) return w.valid(u.dest) ? u.dest : -1;
	const int i = u.i < 0 ? 0 : u.i;
	if (size_t(i) + 1 >= len) return w.valid(u.path[len - 1]) ? u.path[len - 1] : -1;
	const int a = u.path[size_t(i)], b = u.path[size_t(i) + 1];
	const int c = u.t >= 0.5 ? b : a;
	if (w.valid(c)) return c;
	return w.valid(a) ? a : w.valid(b) ? b : -1;
}
int dirOf(const World& w, const Unit& u, Vec3& out) {
	DM_STUB(units_dirOf, w, u, out);
	const int c = cellOf(w, u);
	out = (c >= 0 && w.geo) ? w.geo->dir(c) : Vec3{ 0, 1, 0 };
	return c;
}

} // namespace dm::units
