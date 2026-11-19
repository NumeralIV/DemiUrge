// DEMIURGE — SimTests/stubs/Chronicle_stub.cpp : neutral stand-in for Sim/Chronicle.cpp (see StubHooks.h).
// Owner: chronicle engineer (keep it matching Chronicle.h). Written by foundation-api.
// Neutral: attach subscribes nothing, add refuses (nullptr), epithet "", updateEpithets nothing,
// snapshot false, tale empty. yearStr answers honestly ('Year 312').
#include "StubHooks.h"

#include <cmath>
#include <string>

DM_STUB_MODULE(chronicle);

namespace dm::chronicle {

void attach(World& w) {
	DM_STUB(chronicle_attach, w);
}
ChronicleEntry* add(World& w, const EntryOpts& opts) {
	DM_STUB(chronicle_add, w, opts);
	return nullptr;
}
std::string yearStr(const World& w, std::optional<double> year) {
	DM_STUB(chronicle_yearStr, w, year);
	const double y = year ? *year : w.year;
	return "Year " + std::to_string(isFinite(y) ? static_cast<long long>(std::floor(y)) : 0LL);
}
std::string epithet(const World& w, int cultureId) {
	DM_STUB(chronicle_epithet, w, cultureId);
	return std::string();
}
void updateEpithets(World& w) {
	DM_STUB(chronicle_updateEpithets, w);
}
bool snapshot(World& w, bool force) {
	DM_STUB(chronicle_snapshot, w, force);
	return false;
}
Tale tale(const World& w) {
	DM_STUB(chronicle_tale, w);
	return Tale{};
}

} // namespace dm::chronicle
