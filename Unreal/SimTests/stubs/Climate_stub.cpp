// DEMIURGE — SimTests/stubs/Climate_stub.cpp : neutral stand-in for Sim/Climate.cpp (see StubHooks.h).
// Owner: climate engineer (keep it matching Climate.h). Written by foundation-api.
// Neutral: update / stepIce / yearly / calibrate / emit / addDust do nothing. The pure reads
// datum, anomaly and landIce answer honestly, with exactly the real module's formulas and guards
// (other modules' tests rely on them).
#include "StubHooks.h"

#include <algorithm>
#include <cmath>

DM_STUB_MODULE(climate);

namespace dm::climate {

void update(World& w) {
	DM_STUB(climate_update, w);
}
void stepIce(World& w) {
	DM_STUB(climate_stepIce, w);
}
void yearly(World& w) {
	DM_STUB(climate_yearly, w);
}
void calibrate(World& w) {
	DM_STUB(climate_calibrate, w);
}
double datum(const World& w) {
	DM_STUB(climate_datum, w);
	if (w.vars.seaBase && std::isfinite(*w.vars.seaBase)) return std::max(0.0, *w.vars.seaBase);
	return std::isfinite(w.seaLevel) ? std::max(0.0, w.seaLevel) : 0.0;
}
double anomaly(const World& w) {
	DM_STUB(climate_anomaly, w);
	const double gt = std::isfinite(w.globalTemp) ? w.globalTemp : 0.0;
	const double a = gt - w.tune<ClimateTune>().dustCool * (std::isfinite(w.dust) ? w.dust : 0.0);
	return std::isfinite(a) ? a : gt;
}
void emit(World& w, double amount) {
	DM_STUB(climate_emit, w, amount);
}
void addDust(World& w, double amount) {
	DM_STUB(climate_addDust, w, amount);
}
double landIce(const World& w) {
	DM_STUB(climate_landIce, w);
	double sum = 0;
	for (int c = 0; c < w.n; c++)
		if (double(w.elev[size_t(c)]) >= w.seaLevel && std::isfinite(w.ice[size_t(c)])) sum += w.ice[size_t(c)];
	return std::isfinite(sum) ? sum : 0.0;
}

} // namespace dm::climate
