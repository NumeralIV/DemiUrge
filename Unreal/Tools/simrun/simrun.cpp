// DEMIURGE — Tools/simrun/simrun.cpp : the headless simulation runner (JS tools/simrun.js).
// Owner: sim engineer (A14). Build and run with Tools/simrun/simrun.ps1 (every real Sim module, no
// stubs). Tool code: it may use clocks, threads and iostreams; the world is only ever driven through
// sim::create / genesis / sim::advance and the scripted players' powers::use calls, so two runs with
// the same arguments produce identical worlds (--hash).
//
//   simrun --seed 7 --years 4000 --policy passive
//   simrun --seeds 1-6 --policy passive,kind --summary --jobs 6
//   simrun --help
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "Balance.h"
#include "Chronicle.h"
#include "Civ.h"
#include "Climate.h"
#include "Life.h"
#include "MapImg.h"
#include "Policies.h"
#include "Powers.h"
#include "Sim.h"
#include "Society.h"
#include "State.h"

using namespace dm;
using simrun::Policy;
using simrun::PolicyKind;

namespace {

// ---------------------------------------------------------------- small helpers

std::string fmt(const char* f, ...) {
	char buf[1024];
	va_list ap;
	va_start(ap, f);
	std::vsnprintf(buf, sizeof buf, f, ap);
	va_end(ap);
	return buf;
}

std::string big(double v) {
	if (!std::isfinite(v)) return "nan";
	const double a = std::fabs(v);
	if (a >= 1e6) return fmt("%.2fM", v / 1e6);
	if (a >= 1e4) return fmt("%.1fk", v / 1e3);
	return fmt("%.0f", v);
}

std::string jsonStr(const std::string& s) {
	std::string o = "\"";
	for (char ch : s) {
		if (ch == '"' || ch == '\\') { o += '\\'; o += ch; }
		else if (ch == '\n') o += "\\n";
		else if (static_cast<unsigned char>(ch) < 0x20) o += fmt("\\u%04x", ch);
		else o += ch;
	}
	return o + "\"";
}

std::string jnum(double v) { return std::isfinite(v) ? fmt("%.6g", v) : "null"; }

const char* ageName(int a) { return a >= 0 && a < Age::COUNT ? kAgeNames[a] : "?"; }

std::string shotsDir() {
	const char* root = std::getenv("DM_ROOT");
	std::string d = root && *root ? std::string(root) + "/shots/" : std::string("shots/");
	return d;
}

double nowMs() {
	using namespace std::chrono;
	return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

// CPU time of this thread (A14 verifier): wall-clock tick times on this shared PC include time the
// thread spent descheduled (other engineers' builds), which made "max tick <= 30 ms" fail at random.
// QueryThreadCycleTime counts only cycles this thread ran (TSC based); dividing by the TSC rate
// (measured against the wall clock over the run) gives milliseconds. Declared by hand: <windows.h>
// redefines IN / OUT, which the forced-in UeMacroGuard.h forbids.
#if defined(_WIN32)
} // namespace
#include <intrin.h>
extern "C" __declspec(dllimport) int __stdcall QueryThreadCycleTime(void* thread, unsigned long long* cycles);
extern "C" __declspec(dllimport) void* __stdcall GetCurrentThread();
namespace {
unsigned long long threadCycles() {
	unsigned long long c = 0;
	return QueryThreadCycleTime(GetCurrentThread(), &c) ? c : 0;
}
unsigned long long tscNow() { return __rdtsc(); }
#else
unsigned long long threadCycles() { return 0; }
unsigned long long tscNow() { return 0; }
#endif

// ---------------------------------------------------------------- arguments

struct Args {
	std::vector<uint32_t> seeds{ 1 };
	std::vector<PolicyKind> policies{ PolicyKind::Passive };
	double years = 4000;
	int L = 6;
	double calamity = 1;
	double sea = 0;
	bool json = false, hash = false, summary = false, quiet = false, strict = false;
	bool genesis = false, tale = false;
	int every = 100;
	int jobs = 1;
	std::vector<std::pair<std::string, double>> sets;
	std::vector<double> mapYears;
	std::string mapPrefix = "ue-sim";
	std::optional<double> saveLoad;
	double afterEnding = 0;
	int chronicle = 0;
	double slowMs = 0;                       // --slow: report ticks slower than this
	bool diag = false;                       // --diag: balance diagnostics under every timeline row
};

const char* kHelp =
	"simrun: headless DEMIURGE runs (all real sim modules)\n"
	"  --seed N | --seeds 1-6 | --seeds 1,4,9   world seeds (default 1)\n"
	"  --years Y          years of history to run (default 4000; stops at an ending)\n"
	"  --policy P[,P..]   passive | kind | cruel | chaotic | doom (default passive)\n"
	"  --L n              grid level (default 6)        --calamity x   natural-event rate (default 1)\n"
	"  --sea x            sea level for genesis skip (default 0)\n"
	"  --genesis          play genesis (comets, seeds, awaken) instead of sim::genesis::skip\n"
	"  --every Y          timeline row every Y years (default 100)   --quiet   no timeline\n"
	"  --json             one JSON object per run (summary + timeline)\n"
	"  --hash             print the world fingerprint at the end (two runs must match)\n"
	"  --summary          per-seed table and pass/fail against SPEC-SIM 4 (per policy)\n"
	"  --strict           exit 2 when a --summary target fails\n"
	"  --jobs N           run N worlds in parallel threads (default 1)\n"
	"  --set k=v          balance::set override (repeatable; --list-keys lists them)\n"
	"  --maps y1,y2,..    write owner / biome maps at these years: shots/<prefix>-<policy>-s<seed>-y<year>-*.png\n"
	"  --map-prefix p     map file prefix (default ue-sim)\n"
	"  --saveload Y       at year Y serialize, deserialize, sim::use and continue with the loaded world\n"
	"  --after-ending Y   keep running Y years after an ending (default 0)\n"
	"  --chronicle N      print the last N chronicle entries     --tale   print chronicle::tale\n"
	"  --diag             balance diagnostics under each timeline row (research rate per culture, settlement\n"
	"                     heights above the sea, tiers, Awe S, regard)\n"
	"exit code: 0 ok, 3 a NaN / non-finite value was found, 2 a --summary target failed (with --strict), 1 bad arguments\n";

bool parseSeeds(const std::string& s, std::vector<uint32_t>& out) {
	out.clear();
	size_t i = 0;
	while (i < s.size()) {
		size_t j = s.find(',', i);
		if (j == std::string::npos) j = s.size();
		const std::string part = s.substr(i, j - i);
		const size_t dash = part.find('-');
		char* e = nullptr;
		if (dash != std::string::npos) {
			const unsigned long a = std::strtoul(part.substr(0, dash).c_str(), &e, 10);
			const unsigned long b = std::strtoul(part.substr(dash + 1).c_str(), &e, 10);
			if (b < a || b - a > 100000) return false;
			for (unsigned long k = a; k <= b; k++) out.push_back(uint32_t(k));
		} else {
			out.push_back(uint32_t(std::strtoul(part.c_str(), &e, 10)));
		}
		i = j + 1;
	}
	return !out.empty();
}

bool parseArgs(int argc, char** argv, Args& a, bool& listKeys) {
	listKeys = false;
	for (int i = 1; i < argc; i++) {
		const std::string k = argv[i];
		auto val = [&](std::string& v) -> bool {
			if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", k.c_str()); return false; }
			v = argv[++i];
			return true;
		};
		std::string v;
		if (k == "--help" || k == "-h") { std::fputs(kHelp, stdout); std::exit(0); }
		else if (k == "--seed" || k == "--seeds") { if (!val(v) || !parseSeeds(v, a.seeds)) return false; }
		else if (k == "--years") { if (!val(v)) return false; a.years = std::atof(v.c_str()); }
		else if (k == "--policy") {
			if (!val(v)) return false;
			a.policies.clear();
			size_t p = 0;
			while (p <= v.size()) {
				size_t q = v.find(',', p);
				if (q == std::string::npos) q = v.size();
				PolicyKind pk;
				if (!simrun::policyFromStr(v.substr(p, q - p), pk)) { std::fprintf(stderr, "unknown policy '%s'\n", v.substr(p, q - p).c_str()); return false; }
				a.policies.push_back(pk);
				p = q + 1;
			}
		}
		else if (k == "--L") { if (!val(v)) return false; a.L = std::atoi(v.c_str()); }
		else if (k == "--calamity") { if (!val(v)) return false; a.calamity = std::atof(v.c_str()); }
		else if (k == "--sea") { if (!val(v)) return false; a.sea = std::atof(v.c_str()); }
		else if (k == "--every") { if (!val(v)) return false; a.every = std::max(1, std::atoi(v.c_str())); }
		else if (k == "--jobs") { if (!val(v)) return false; a.jobs = std::clamp(std::atoi(v.c_str()), 1, 64); }
		else if (k == "--set") {
			if (!val(v)) return false;
			const size_t eqp = v.find('=');
			if (eqp == std::string::npos) { std::fprintf(stderr, "--set wants key=value\n"); return false; }
			a.sets.push_back({ v.substr(0, eqp), std::atof(v.substr(eqp + 1).c_str()) });
		}
		else if (k == "--maps") {
			if (!val(v)) return false;
			size_t p = 0;
			while (p < v.size()) {
				size_t q = v.find(',', p);
				if (q == std::string::npos) q = v.size();
				a.mapYears.push_back(std::atof(v.substr(p, q - p).c_str()));
				p = q + 1;
			}
		}
		else if (k == "--map-prefix") { if (!val(v)) return false; a.mapPrefix = v; }
		else if (k == "--saveload") { if (!val(v)) return false; a.saveLoad = std::atof(v.c_str()); }
		else if (k == "--after-ending") { if (!val(v)) return false; a.afterEnding = std::max(0.0, std::atof(v.c_str())); }
		else if (k == "--slow") { if (!val(v)) return false; a.slowMs = std::max(0.0, std::atof(v.c_str())); }
		else if (k == "--chronicle") { if (!val(v)) return false; a.chronicle = std::max(0, std::atoi(v.c_str())); }
		else if (k == "--json") a.json = true;
		else if (k == "--hash") a.hash = true;
		else if (k == "--summary") a.summary = true;
		else if (k == "--quiet") a.quiet = true;
		else if (k == "--strict") a.strict = true;
		else if (k == "--genesis") a.genesis = true;
		else if (k == "--diag") a.diag = true;
		else if (k == "--tale") a.tale = true;
		else if (k == "--list-keys") listKeys = true;
		else { std::fprintf(stderr, "unknown argument '%s' (--help)\n", k.c_str()); return false; }
	}
	std::sort(a.mapYears.begin(), a.mapYears.end());
	return true;
}

// ---------------------------------------------------------------- one run

const int kCheckpoints[] = { 0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000 };
constexpr int kNumCp = int(sizeof(kCheckpoints) / sizeof(kCheckpoints[0]));

struct Row {
	sim::Sample s;
	double seaRel = 0;
	double capShare = 0;
	std::array<int, 10> ev{};
};

struct Result {
	uint32_t seed = 0;
	PolicyKind policy = PolicyKind::Passive;
	std::string name;
	double createMs = 0, startMs = 0;
	std::string genesisNote;
	double yearsRun = 0;
	std::array<double, Age::COUNT> ageYear{};      // world:age years (-1 = never); [0] = 0
	double arkYear = -1;
	std::optional<Ending> ending;
	std::array<double, kNumCp> popAt{}, settlAt{}, cultAt{};
	double peakAnom = -1e9, minAnom = 1e9, peakDust = 0, seaRise = -1e9, seaFall = 1e9, peakCo2 = 0;
	double lostYear = -1;                            // first year nobody was left (-1 never)
	int lostCount = 0;                               // people:lost events
	double years = 0, yearsCap = 0, yearsBelow90 = 0;
	double incStone = 0, incLater = 0, incGreat = 0;
	int nStone = 0, nLater = 0, nGreat = 0;
	double meanMs = 0, maxMs = 0, maxMsYear = 0;     // wall clock
	double cpuMeanMs = 0, cpuMaxMs = 0, cpuP999Ms = 0; // thread CPU time (immune to other processes)
	bool cpuOk = false;
	int64_t ticks = 0;
	size_t largestFire = 0;
	double largestFireYear = 0;
	std::string nan;
	double nanYear = -1;
	uint32_t hash = 0;
	std::array<int, kEventCount> events{};
	std::map<std::string, int> destroyed;
	int peakSettl = 0, peakCult = 0;
	double peakPop = 0;
	simrun::PolicyStats pstats;
	double score = 0;
	std::vector<Row> rows;
	std::string text;                                // everything printed for this run
	bool crashed = false;
};

// Timeline event columns
const Ev kCols[10] = { Ev::SettlementFounded, Ev::SettlementDestroyed, Ev::WarDeclared, Ev::Battle, Ev::SettlementFamine,
	Ev::SettlementPlague, Ev::FireStart, Ev::MeteorImpact, Ev::PowerUsed, Ev::CultureBorn };
const char* const kColNames[10] = { "found", "lost", "wars", "battl", "famin", "plagu", "fires", "impac", "power", "cultb" };

// Genesis played like a player: comets until the sea reaches args.sea, seeds until life may
// advance, awaken at civ::bestSite. Falls back to genesis::skip when a phase does not finish.
std::string playGenesis(World& w, const Args& args, uint32_t seed) {
	Rng r = Rng(seed).fork("simrun:genesis");
	int comets = 0, seeds = 0;
	int64_t guard = 0;
	while (w.phase == Phase::Molten && guard++ < 1000) sim::advance(w, 1);
	const int64_t tMolten = w.tick;
	while (w.phase == Phase::Water && guard++ < 6000) {
		if (w.seaLevel >= args.sea - 0.05 && sim::genesis::progress(w).canAdvance && w.disasters.empty()) { sim::genesis::next(w); break; }
		if (w.tick % 6 == 0) {
			const int c = r.integer(0, w.n - 1);
			if (w.seaLevel < args.sea - 0.05 && powers::can(w, "comet", c).ok && powers::use(w, "comet", c)) comets++;
		}
		sim::advance(w, 1);
	}
	const double seaAfter = w.seaLevel;
	while (w.phase == Phase::Life && guard++ < 12000) {
		if (sim::genesis::progress(w).canAdvance) { sim::genesis::next(w); break; }
		if (w.tick % 4 == 0) {
			for (int tries = 0; tries < 20; tries++) {
				const int c = r.integer(0, w.n - 1);
				if (powers::can(w, "seed", c).ok) { if (powers::use(w, "seed", c)) seeds++; break; }
			}
		}
		sim::advance(w, 1);
	}
	const double vegAfter = life::cover(w).veg;
	bool woke = false;
	if (w.phase == Phase::People) {
		const int site = civ::bestSite(w);
		woke = w.valid(site) && powers::use(w, "awaken", site);
	}
	std::string note = fmt("genesis played: molten %lld ticks, %d comets -> sea %.2f, %d seeds -> veg %.2f, awaken %s, %lld ticks in all",
		(long long)tMolten, comets, seaAfter, seeds, vegAfter, woke ? "ok" : "FAILED", (long long)w.tick);
	if (w.phase != Phase::History) {
		sim::genesis::SkipOpts so;
		so.sea = args.sea;
		sim::genesis::skip(w, so);
		note += " (fell back to genesis::skip)";
	}
	return note;
}

void writeMaps(const World& w, const Args& args, const Result& R, double year) {
	const std::string base = shotsDir() + fmt("%s-%s-s%u-y%d", args.mapPrefix.c_str(), simrun::toStr(R.policy), R.seed, int(std::lround(year)));
	mapimg::owner(w, base + "-owner.png");
	mapimg::biome(w, base + "-biome.png");
}

Result runOne(const Args& args, uint32_t seed, PolicyKind pk, bool live, std::mutex& outMu) {
	Result R;
	R.seed = seed;
	R.policy = pk;
	R.ageYear.fill(-1);
	R.popAt.fill(-1);
	R.settlAt.fill(-1);
	R.cultAt.fill(-1);
	auto say = [&](const std::string& s) {
		R.text += s;
		R.text += '\n';
		if (live) { std::lock_guard<std::mutex> lk(outMu); std::fputs(s.c_str(), stdout); std::fputc('\n', stdout); std::fflush(stdout); }
	};

	const double c0 = nowMs();
	sim::WorldOpts wo;
	wo.seed = seed;
	wo.L = args.L;
	wo.settings.calamity = args.calamity;
	std::unique_ptr<World> wp = sim::create(wo);
	auto applySets = [&](World& w) {
		for (const auto& kv : args.sets) balance::set(w, kv.first, kv.second);
	};
	applySets(*wp);
	const double c1 = nowMs();
	if (args.genesis) R.genesisNote = playGenesis(*wp, args, seed);
	else {
		sim::genesis::SkipOpts so;
		so.sea = args.sea;
		if (!sim::genesis::skip(*wp, so)) R.genesisNote = "genesis::skip could not wake anyone";
	}
	const double c2 = nowMs();
	R.name = wp->name;
	R.createMs = c1 - c0;
	R.startMs = c2 - c1;
	R.ageYear[0] = 0;

	std::array<int, kEventCount> window{};
	std::array<int, kEventCount> inTick{};
	std::map<std::string, int> spawnedInTick;
	auto subscribe = [&](World& w) {
		w.bus.onAll([&](const Event& e) {
			const int t = int(e.type);
			if (t >= 0 && t < kEventCount) { R.events[size_t(t)]++; window[size_t(t)]++; inTick[size_t(t)]++; }
			if (e.type == Ev::WorldAge) {
				if (const EvWorldAge* a = e.as<EvWorldAge>()) {
					if (a->age >= 0 && a->age < Age::COUNT && R.ageYear[size_t(a->age)] < 0) R.ageYear[size_t(a->age)] = wp->year;
				}
			} else if (e.type == Ev::ArkLaunched) {
				if (R.arkYear < 0) R.arkYear = wp->year;
			} else if (e.type == Ev::SettlementDestroyed) {
				if (const EvSettlementDestroyed* d = e.as<EvSettlementDestroyed>()) {
					std::string why = toStr(d->cause);
					if (d->cause == Cause::Drowned && wp->valid(d->cell)) why += state::isSea(*wp, d->cell) ? "(sea)" : "(lake)";
					R.destroyed[why]++;
				}
			} else if (e.type == Ev::UnitSpawned) {
				if (const EvUnitSpawned* u = e.as<EvUnitSpawned>()) spawnedInTick[toStr(u->type)]++;
			} else if (e.type == Ev::PeopleLost) {
				R.lostCount++;
			}
		});
	};
	subscribe(*wp);

	say(fmt("== seed %u  policy %s  world \"%s\"  L %d  calamity %.2f  create %.0f ms  start %.0f ms%s%s",
		seed, simrun::toStr(pk), R.name.c_str(), args.L, args.calamity, R.createMs, R.startMs,
		R.genesisNote.empty() ? "" : "\n   ", R.genesisNote.c_str()));
	if (!args.quiet) {
		std::string h = "   year     pop settl cult age       anom   sea   co2  dust  awe/max    inc s/40 cap%";
		for (const char* c : kColNames) h += fmt(" %5s", c);
		say(h);
	}

	Policy policy(pk, seed);
	double tickMsSum = 0;
	std::vector<unsigned long long> tickCycles;     // thread cycles per tick (converted to ms at the end)
	tickCycles.reserve(size_t(std::max(0.0, args.years)) * 4 + 16);
	const double wall0 = nowMs();
	const unsigned long long tsc0 = tscNow();
	int cpNext = 0;
	size_t mapNext = 0;
	bool saved = false;
	double nextRow = 0;
	int capWin = 0, yearsWin = 0;
	std::optional<double> endedAt;
	const double seaBase = wp->vars.seaBase.value_or(wp->seaLevel);

	// --diag: research progress of every culture between rows (progress-years per year, ages included)
	std::vector<double> sciPrev;
	double sciYear = 0;
	auto sciTotal = [&](const World& w, size_t id) {
		const EconomyTune& ET = w.tune<EconomyTune>();
		const Culture& c = w.cultures[id];
		double tot = std::max(0.0, c.science);
		for (int a = 0; a < c.age && a < 6; a++) tot += ET.ageNeed[size_t(a)];
		return tot;
	};
	auto diagLine = [&](World& w) {
		std::string d = "        diag: rate";
		const double dy = w.year - sciYear;
		std::string rates;
		for (size_t id = 0; id < w.cultures.size(); id++) {
			const Culture& c = w.cultures[id];
			if (!c.dead && id < sciPrev.size() && dy > 0) rates += fmt(" %zu:%.2f", id, (sciTotal(w, id) - sciPrev[id]) / dy);
		}
		sciPrev.resize(w.cultures.size());
		for (size_t id = 0; id < w.cultures.size(); id++) sciPrev[id] = sciTotal(w, id);
		sciYear = w.year;
		d += rates.empty() ? std::string(" -") : rates;
		int low1 = 0, low3 = 0, nLive = 0;
		std::array<int, 5> tiers{};
		double S = 0;
		for (const Settlement& s : w.settlements) {
			if (s.dead || !w.valid(s.cell)) continue;
			nLive++;
			const double h = double(w.elev[size_t(s.cell)]) - w.seaLevel;
			if (h < 0.1) low1++;
			if (h < 0.3) low3++;
			tiers[size_t(std::clamp(s.tier, 0, 4))]++;
			S += s.devotion * (0.2 + 0.5 * s.b[Building::SHRINE] + 1.2 * s.b[Building::TEMPLE]) * (1 + s.tier / 2.0);
		}
		d += fmt(" | low<0.1 %d <0.3 %d of %d | tiers %d/%d/%d/%d/%d | S %.0f", low1, low3, nLive, tiers[0], tiers[1], tiers[2], tiers[3], tiers[4], S);
		const society::Regard rg = society::regard(w, -1);
		d += fmt(" | regard %.2f/%+.2f", rg.devotion, rg.love);
		// relations between living cultures: min / mean v, mean trade, pairs at war, in contact
		double vMin = 2, vSum = 0, trSum = 0;
		int pairs = 0, atWar = 0, contact = 0;
		for (const auto& kv : w.vars.rel) {
			const Culture* ca = state::culture(w, kv.first.first);
			const Culture* cb = state::culture(w, kv.first.second);
			if (!ca || !cb || ca->dead || cb->dead) continue;
			pairs++;
			vMin = std::min(vMin, kv.second.v);
			vSum += kv.second.v;
			trSum += kv.second.trade;
			atWar += kv.second.war ? 1 : 0;
			contact += kv.second.contact ? 1 : 0;
		}
		if (pairs > 0) d += fmt(" | rel v min %+.2f mean %+.2f trade %.2f, %d pairs, %d in contact, %d at war", vMin, vSum / pairs, trSum / pairs, pairs, contact, atWar);
		return d;
	};
	auto rowNow = [&](World& w) {
		Row row;
		row.s = sim::sample(w);
		row.seaRel = w.seaLevel - seaBase;
		row.capShare = yearsWin > 0 ? double(capWin) / yearsWin : 0;
		for (int i = 0; i < 10; i++) row.ev[size_t(i)] = window[size_t(kCols[i])];
		R.rows.push_back(row);
		if (!args.quiet) {
			const double inc = row.s.aweIncome;
			std::string l = fmt("  %5.0f %7s %5d %4d %-8s %+5.2f %+5.2f %5.1f %5.3f %4.0f/%-4.0f %5.2f %4.0f %4.0f",
				row.s.year, big(row.s.pop).c_str(), row.s.settlements, row.s.cultures, ageName(row.s.age), row.s.anomaly, row.seaRel,
				row.s.co2, row.s.dust, row.s.awe, row.s.aweMax, inc, inc > 0 ? 40 / (2 * inc) : 0.0, 100 * row.capShare);
			for (int i = 0; i < 10; i++) l += fmt(" %5d", row.ev[size_t(i)]);
			say(l);
			if (args.diag) say(diagLine(w));
		}
		window.fill(0);
		capWin = 0;
		yearsWin = 0;
		if (R.nan.empty()) {
			const std::string bad = sim::firstNonFinite(w);
			if (!bad.empty()) { R.nan = bad; R.nanYear = w.year; say("   NON-FINITE: " + bad); }
		}
	};

	sim::refreshStats(*wp);
	rowNow(*wp);
	nextRow = args.every;
	auto checkpoint = [&](World& w) {
		while (cpNext < kNumCp && w.year + 1e-9 >= kCheckpoints[cpNext]) {
			int settl = 0, cult = 0;
			for (const Settlement& s : w.settlements) if (!s.dead) settl++;
			for (const Culture& c : w.cultures) if (!c.dead) cult++;
			R.popAt[size_t(cpNext)] = w.stats.pop;
			R.settlAt[size_t(cpNext)] = settl;
			R.cultAt[size_t(cpNext)] = cult;
			cpNext++;
		}
	};
	checkpoint(*wp);

	const int64_t maxTicks = int64_t(std::ceil(args.years * sim::kTicksPerYear)) + 8;
	for (int64_t i = 0; i < maxTicks; i++) {
		World& w = *wp;
		if (w.year + 1e-9 >= args.years) break;
		if (endedAt && w.year - *endedAt >= args.afterEnding) break;
		policy.before(w);
		const bool yearly = sim::isYearlyTick(w);
		inTick.fill(0);
		spawnedInTick.clear();
		const double t0 = nowMs();
		const unsigned long long cyc0 = threadCycles();
		sim::advance(w, 1);
		const unsigned long long cyc1 = threadCycles();
		const double dt = nowMs() - t0;
		tickCycles.push_back(cyc1 - cyc0);
		if (args.slowMs > 0 && dt > args.slowMs) {
			std::string evs;
			for (int t = 0; t < kEventCount; t++) if (inTick[size_t(t)]) evs += fmt(" %s %d", eventName(Ev(t)), inTick[size_t(t)]);
			if (!spawnedInTick.empty()) {
				evs += " (spawned:";
				for (const auto& kv : spawnedInTick) evs += fmt(" %s %d", kv.first.c_str(), kv.second);
				evs += ")";
			}
			say(fmt("   slow tick %lld (year %.2f, %s): %.1f ms; chronicle %zu, snapshots %zu, units %zu; events:%s", (long long)w.tick, w.year,
				yearly ? "yearly" : "not yearly", dt, w.chronicle.size(), w.snapshots.size(), w.units.size(), evs.c_str()));
		}
		R.ticks++;
		tickMsSum += dt;
		if (dt > R.maxMs) { R.maxMs = dt; R.maxMsYear = w.year; }
		if (w.cache.fires.size() > R.largestFire) { R.largestFire = w.cache.fires.size(); R.largestFireYear = w.year; }
		if (!yearly) continue;
		// ---- once a year
		const double anom = climate::anomaly(w);
		R.peakAnom = std::max(R.peakAnom, anom);
		R.minAnom = std::min(R.minAnom, anom);
		R.peakDust = std::max(R.peakDust, w.dust);
		R.peakCo2 = std::max(R.peakCo2, w.co2);
		R.seaRise = std::max(R.seaRise, w.seaLevel - seaBase);
		R.seaFall = std::min(R.seaFall, w.seaLevel - seaBase);
		int settl = 0, cult = 0;
		for (const Settlement& s : w.settlements) if (!s.dead) settl++;
		for (const Culture& c : w.cultures) if (!c.dead) cult++;
		R.peakSettl = std::max(R.peakSettl, settl);
		R.peakCult = std::max(R.peakCult, cult);
		R.peakPop = std::max(R.peakPop, w.stats.pop);
		bool anyone = settl > 0;
		for (const Unit& u : w.units) if (u.type == UnitType::Settler) anyone = true;
		if (!anyone && R.lostYear < 0) R.lostYear = w.year;
		if (!w.ending) {
			R.years++;
			yearsWin++;
			const bool cap = w.god.awe >= w.god.aweMax - 1e-6;
			if (cap) { R.yearsCap++; capWin++; }
			if (w.god.awe < 0.9 * w.god.aweMax) R.yearsBelow90++;
			const double inc = society::aweIncome(w);
			if (w.age == 0) { R.incStone += inc; R.nStone++; } else { R.incLater += inc; R.nLater++; }
			if (powers::unlocked(w, "meteor")) { R.incGreat += inc; R.nGreat++; }
		}
		checkpoint(w);
		while (mapNext < args.mapYears.size() && w.year + 1e-9 >= args.mapYears[mapNext]) { writeMaps(w, args, R, args.mapYears[mapNext]); mapNext++; }
		if (w.year + 1e-9 >= nextRow) { rowNow(w); nextRow += args.every; }
		if (w.ending && !endedAt) {
			endedAt = w.year;
			say(fmt("   ENDING year %.0f: %s / %s (culture %d, settlement %d)", w.ending->year, toStr(w.ending->type), toStr(w.ending->mood),
				w.ending->culture, w.ending->settlement));
		}
		if (args.saveLoad && !saved && w.year + 1e-9 >= *args.saveLoad) {
			saved = true;
			std::vector<uint8_t> bytes;
			std::string why;
			const uint32_t before = sim::fingerprint(w);
			if (!state::serialize(w, bytes, &why)) { say("   SAVE FAILED: " + why); continue; }
			std::unique_ptr<World> loaded = state::deserialize(bytes, &why);
			if (!loaded) { say("   LOAD FAILED: " + why); continue; }
			std::vector<uint8_t> again, afterUse;
			state::serialize(*loaded, again);                  // straight after loading: must be identical
			sim::use(*loaded);
			applySets(*loaded);
			state::serialize(*loaded, afterUse);               // after sim::use (rederive recomputes lakes etc.)
			say(fmt("   save/load at year %.0f: %zu bytes; re-save of the loaded world %s; after sim::use %s; fingerprint %08x -> %08x (quantized fields differ by design)",
				w.year, bytes.size(), again == bytes ? "identical" : "DIFFERENT", afterUse == bytes ? "identical" : "differs (derived recompute)",
				before, sim::fingerprint(*loaded)));
			wp = std::move(loaded);
			subscribe(*wp);
		}
	}
	World& w = *wp;
	if (R.rows.empty() || R.rows.back().s.tick != w.tick) rowNow(w);
	R.yearsRun = w.year;
	R.ending = w.ending;
	R.meanMs = R.ticks > 0 ? tickMsSum / double(R.ticks) : 0;
	{   // thread CPU time per tick: cycles / (TSC cycles per wall ms over the run)
		const double wallMs = nowMs() - wall0;
		const double tscPerMs = wallMs > 50 ? double(tscNow() - tsc0) / wallMs : 0;
		if (tscPerMs > 0 && !tickCycles.empty() && tickCycles.back() > 0) {
			double sum = 0;
			std::vector<double> ms(tickCycles.size());
			for (size_t k = 0; k < tickCycles.size(); k++) { ms[k] = double(tickCycles[k]) / tscPerMs; sum += ms[k]; }
			R.cpuMeanMs = sum / double(ms.size());
			R.cpuMaxMs = *std::max_element(ms.begin(), ms.end());
			std::sort(ms.begin(), ms.end());
			R.cpuP999Ms = ms[std::min(ms.size() - 1, size_t(double(ms.size()) * 0.999))];
			R.cpuOk = true;
		}
	}
	R.pstats = policy.stats();
	R.score = sim::score(w).total;
	if (R.nan.empty()) {
		const std::string bad = sim::firstNonFinite(w);
		if (!bad.empty()) { R.nan = bad; R.nanYear = w.year; }
	}
	R.hash = sim::fingerprint(w);

	std::string ages;
	for (int a = 1; a < Age::COUNT; a++) ages += fmt(" %s %s", ageName(a), R.ageYear[size_t(a)] >= 0 ? fmt("%.0f", R.ageYear[size_t(a)]).c_str() : "-");
	say(fmt("   result: %.0f years, %s; ages:%s; ark %s",
		R.yearsRun, R.ending ? fmt("ending %s / %s in %.0f", toStr(R.ending->type), toStr(R.ending->mood), R.ending->year).c_str() : "no ending",
		ages.c_str(), R.arkYear >= 0 ? fmt("%.0f", R.arkYear).c_str() : "-"));
	say(fmt("   peaks: pop %s, settlements %d, cultures %d; anomaly %+.2f (min %+.2f), co2 %.1f, dust %.3f, sea %+.3f / %+.3f vs seaBase; largest fire %zu cells (year %.0f)",
		big(R.peakPop).c_str(), R.peakSettl, R.peakCult, R.peakAnom, R.minAnom, R.peakCo2, R.peakDust, R.seaRise, R.seaFall, R.largestFire, R.largestFireYear));
	const double incS = R.nStone ? R.incStone / R.nStone : 0, incL = R.nLater ? R.incLater / R.nLater : 0, incG = R.nGreat ? R.incGreat / R.nGreat : 0;
	say(fmt("   awe: at cap %.0f%% of years, below 90%% %.0f%%; income stone %.2f/yr (a 40-Awe miracle every %.1f s at 1x), later %.2f/yr (%.1f s), 150 Awe every %s s once meteors unlock",
		R.years > 0 ? 100 * R.yearsCap / R.years : 0, R.years > 0 ? 100 * R.yearsBelow90 / R.years : 0, incS, incS > 0 ? 40 / (2 * incS) : 0, incL,
		incL > 0 ? 40 / (2 * incL) : 0, incG > 0 ? fmt("%.0f", 150 / (2 * incG)).c_str() : "-"));
	std::string dest;
	for (const auto& kv : R.destroyed) dest += fmt(" %s %d", kv.first.c_str(), kv.second);
	auto evc = [&](Ev e) { return R.events[size_t(e)]; };
	say(fmt("   events: founded %d, destroyed %d (%s ), cultures born %d / died %d, wars %d, battles %d, nukes %d, prophets %d, wonders %d, trade routes %d, famines %d, plagues %d, fires %d, storms %d, droughts %d, quakes %d, volcanoes %d, impacts %d, people:lost %d, chronicle %zu",
		evc(Ev::SettlementFounded), evc(Ev::SettlementDestroyed), dest.c_str(), evc(Ev::CultureBorn), evc(Ev::CultureDied), evc(Ev::WarDeclared),
		evc(Ev::Battle), evc(Ev::Nuke), evc(Ev::ProphetRisen), evc(Ev::WonderBuilt), evc(Ev::TradeRoute), evc(Ev::SettlementFamine),
		evc(Ev::SettlementPlague), evc(Ev::FireStart), evc(Ev::Storm), evc(Ev::Drought), evc(Ev::Quake), evc(Ev::VolcanoStart),
		evc(Ev::MeteorImpact), evc(Ev::PeopleLost), w.chronicle.size()));
	if (pk != PolicyKind::Passive) {
		std::string bp;
		for (const auto& kv : R.pstats.byPower) bp += fmt(" %s %d", kv.first.c_str(), kv.second);
		say(fmt("   policy: %d clicks, %d strokes, %d refused, %d edicts;%s", R.pstats.clicks, R.pstats.strokes, R.pstats.refused, R.pstats.edicts, bp.c_str()));
	}
	say(fmt("   timing: %lld ticks, wall mean %.3f ms, max %.2f ms (year %.0f); thread CPU mean %s ms, max %s ms, p99.9 %s ms; score %.0f; non-finite: %s%s",
		(long long)R.ticks, R.meanMs, R.maxMs, R.maxMsYear, R.cpuOk ? fmt("%.3f", R.cpuMeanMs).c_str() : "n/a",
		R.cpuOk ? fmt("%.2f", R.cpuMaxMs).c_str() : "n/a", R.cpuOk ? fmt("%.2f", R.cpuP999Ms).c_str() : "n/a", R.score,
		R.nan.empty() ? "none" : R.nan.c_str(), R.nan.empty() ? "" : fmt(" (year %.0f)", R.nanYear).c_str()));
	{
		const society::Regard all = society::regard(w, -1);
		std::string line = fmt("   regard now: everyone devotion %.3f love %+.3f (%s)", all.devotion, all.love, toStr(all.mood));
		const int arkC = w.vars.ark ? w.vars.ark->culture : -1;
		for (const Culture& c : w.cultures) {
			if (c.dead && c.id != arkC) continue;
			const society::Regard r = society::regard(w, c.id);
			line += fmt("; %s%s %.3f/%+.3f age %s pop %s", c.name.c_str(), c.id == arkC ? "*" : "", r.devotion, r.love, ageName(c.age), big(c.pop).c_str());
		}
		say(line);
	}
	if (args.hash) say(fmt("   hash %08x", R.hash));
	if (args.chronicle > 0) {
		say("   chronicle (last entries):");
		const size_t n = w.chronicle.size();
		for (size_t k = n > size_t(args.chronicle) ? n - size_t(args.chronicle) : 0; k < n; k++)
			say(fmt("     [%5.0f w%d] %s", w.chronicle[k].year, w.chronicle[k].weight, w.chronicle[k].text.c_str()));
	}
	if (args.tale) {
		const chronicle::Tale t = chronicle::tale(w);
		say("   tale: " + t.title);
		for (const std::string& p : t.paragraphs) say("     " + p);
	}
	return R;
}

std::string toJson(const Result& R) {
	std::string j = "{";
	j += fmt("\"seed\":%u,\"policy\":%s,\"name\":%s,\"years\":%s,\"ticks\":%lld", R.seed, jsonStr(simrun::toStr(R.policy)).c_str(),
		jsonStr(R.name).c_str(), jnum(R.yearsRun).c_str(), (long long)R.ticks);
	j += ",\"ages\":[";
	for (int a = 0; a < Age::COUNT; a++) j += (a ? "," : "") + (R.ageYear[size_t(a)] >= 0 ? jnum(R.ageYear[size_t(a)]) : std::string("null"));
	j += "],\"ark\":" + (R.arkYear >= 0 ? jnum(R.arkYear) : std::string("null"));
	j += ",\"ending\":" + (R.ending ? fmt("{\"type\":%s,\"mood\":%s,\"culture\":%d,\"settlement\":%d,\"year\":%s}", jsonStr(toStr(R.ending->type)).c_str(),
		jsonStr(toStr(R.ending->mood)).c_str(), R.ending->culture, R.ending->settlement, jnum(R.ending->year).c_str()) : std::string("null"));
	j += ",\"checkpoints\":[";
	for (int i = 0; i < kNumCp; i++)
		j += fmt("%s{\"year\":%d,\"pop\":%s,\"settlements\":%s,\"cultures\":%s}", i ? "," : "", kCheckpoints[i], R.popAt[size_t(i)] >= 0 ? jnum(R.popAt[size_t(i)]).c_str() : "null",
			R.settlAt[size_t(i)] >= 0 ? jnum(R.settlAt[size_t(i)]).c_str() : "null", R.cultAt[size_t(i)] >= 0 ? jnum(R.cultAt[size_t(i)]).c_str() : "null");
	j += "]";
	j += fmt(",\"peakAnomaly\":%s,\"peakDust\":%s,\"peakCo2\":%s,\"seaRise\":%s,\"lostYear\":%s,\"aweCapShare\":%s,\"aweBelow90Share\":%s",
		jnum(R.peakAnom).c_str(), jnum(R.peakDust).c_str(), jnum(R.peakCo2).c_str(), jnum(R.seaRise).c_str(),
		R.lostYear >= 0 ? jnum(R.lostYear).c_str() : "null", jnum(R.years > 0 ? R.yearsCap / R.years : 0).c_str(),
		jnum(R.years > 0 ? R.yearsBelow90 / R.years : 0).c_str());
	j += fmt(",\"largestFire\":%zu,\"meanTickMs\":%s,\"maxTickMs\":%s,\"cpuMeanTickMs\":%s,\"cpuMaxTickMs\":%s,\"nonFinite\":%s,\"hash\":\"%08x\",\"score\":%s", R.largestFire, jnum(R.meanMs).c_str(),
		jnum(R.maxMs).c_str(), (R.cpuOk ? jnum(R.cpuMeanMs) : std::string("null")).c_str(), (R.cpuOk ? jnum(R.cpuMaxMs) : std::string("null")).c_str(), jsonStr(R.nan).c_str(), R.hash, jnum(R.score).c_str());
	j += ",\"events\":{";
	bool first = true;
	for (int t = 0; t < kEventCount; t++) {
		if (!R.events[size_t(t)]) continue;
		j += fmt("%s%s:%d", first ? "" : ",", jsonStr(eventName(Ev(t))).c_str(), R.events[size_t(t)]);
		first = false;
	}
	j += "},\"timeline\":[";
	for (size_t i = 0; i < R.rows.size(); i++) {
		const Row& r = R.rows[i];
		j += fmt("%s{\"year\":%s,\"pop\":%s,\"settlements\":%d,\"cultures\":%d,\"age\":%d,\"anomaly\":%s,\"sea\":%s,\"co2\":%s,\"awe\":%s,\"aweMax\":%s,\"aweIncome\":%s,\"capShare\":%s}",
			i ? "," : "", jnum(r.s.year).c_str(), jnum(r.s.pop).c_str(), r.s.settlements, r.s.cultures, r.s.age, jnum(r.s.anomaly).c_str(),
			jnum(r.seaRel).c_str(), jnum(r.s.co2).c_str(), jnum(r.s.awe).c_str(), jnum(r.s.aweMax).c_str(), jnum(r.s.aweIncome).c_str(),
			jnum(r.capShare).c_str());
	}
	return j + "]}";
}

// ---------------------------------------------------------------- the per-seed table and SPEC-SIM 4 targets

std::string cell(double v, const char* f = "%.0f") { return v >= 0 ? fmt(f, v) : std::string("-"); }

std::string table(const std::vector<Result>& rs) {
	std::string t;
	t += "seed policy  | Bronze  Iron  Sail Indus Atom Space   Ark | ending           | pop@500 @1000 @2000 @3000 @3500 | settl@1000 @3000 | cult@2000 | anom+  sea+   | lost<500 | awe<90% cap% | ms mean  max  cpu | nan | hash\n";
	for (const Result& r : rs) {
		std::string end = r.ending ? fmt("%s/%s %.0f", toStr(r.ending->type), toStr(r.ending->mood), r.ending->year) : std::string("none");
		t += fmt("%4u %-7s | %6s %5s %5s %5s %4s %5s %5s | %-16s | %7s %5s %5s %5s %5s | %10s %5s | %9s | %+5.2f %+5.3f | %8s | %7.0f %4.0f | %7.3f %4.1f %4.1f | %3s | %08x\n",
			r.seed, simrun::toStr(r.policy), cell(r.ageYear[1]).c_str(), cell(r.ageYear[2]).c_str(), cell(r.ageYear[3]).c_str(), cell(r.ageYear[4]).c_str(),
			cell(r.ageYear[5]).c_str(), cell(r.ageYear[6]).c_str(), cell(r.arkYear).c_str(), end.c_str(), r.popAt[1] >= 0 ? big(r.popAt[1]).c_str() : "-",
			r.popAt[2] >= 0 ? big(r.popAt[2]).c_str() : "-", r.popAt[4] >= 0 ? big(r.popAt[4]).c_str() : "-", r.popAt[6] >= 0 ? big(r.popAt[6]).c_str() : "-",
			r.popAt[7] >= 0 ? big(r.popAt[7]).c_str() : "-", cell(r.settlAt[2]).c_str(), cell(r.settlAt[6]).c_str(), cell(r.cultAt[4]).c_str(),
			r.peakAnom, r.seaRise, r.lostYear >= 0 && r.lostYear < 500 ? fmt("y%.0f", r.lostYear).c_str() : "no",
			r.years > 0 ? 100 * r.yearsBelow90 / r.years : 0.0, r.years > 0 ? 100 * r.yearsCap / r.years : 0.0, r.meanMs, r.maxMs, r.cpuMaxMs,
			r.nan.empty() ? "ok" : "NaN", r.hash);
	}
	return t;
}

struct Target {
	std::string what;
	int pass = 0, n = 0;
	bool need = true;           // false: informational
	double minShare = 1;        // share of runs that must pass
	std::string detail;
};

std::string targets(const std::vector<Result>& rs, PolicyKind pk, bool& allPass) {
	std::vector<Target> ts;
	auto range = [&](const std::string& what, auto get, double lo, double hi, double share = 1) {
		Target T;
		T.what = what + fmt(" in %g..%g", lo, hi);
		T.minShare = share;
		for (const Result& r : rs) {
			const double v = get(r);
			T.n++;
			if (v >= lo && v <= hi) T.pass++;
			T.detail += (T.detail.empty() ? "" : " ") + (v > -1e8 ? fmt("%.4g", v) : std::string("-"));
		}
		ts.push_back(T);
	};
	using ShowFn = std::function<std::string(const Result&)>;
	auto yes = [&](const std::string& what, const std::function<bool(const Result&)>& pred, double share = 1, const ShowFn& show = ShowFn()) {
		Target T;
		T.what = what;
		T.minShare = share;
		for (const Result& r : rs) {
			T.n++;
			if (pred(r)) T.pass++;
			if (show) T.detail += (T.detail.empty() ? "" : " ") + show(r);
		}
		ts.push_back(T);
	};
	auto ageY = [](int a) { return [a](const Result& r) { return r.ageYear[size_t(a)] >= 0 ? r.ageYear[size_t(a)] : -1e9; }; };
	auto moodS = [](const Result& r) { return r.ending ? std::string(toStr(r.ending->mood)) : std::string("none"); };
	yes("no non-finite value", [](const Result& r) { return r.nan.empty(); });
	// Awe at 1x (2 years per real second): seconds to earn a small (40) / great (150) miracle
	auto every = [](double awe, double inc) { return inc > 0 ? awe / (2 * inc) : 1e9; };
	if (pk != PolicyKind::Doom) {     // (doom ends the world in its Stone age by design)
		range("small miracle (40 Awe) every N s, Stone age", [=](const Result& r) { return r.nStone ? every(40, r.incStone / r.nStone) : -1e9; }, 5, 15);
		range("small miracle (40 Awe) every N s, later ages", [=](const Result& r) { return r.nLater ? every(40, r.incLater / r.nLater) : -1e9; }, 3, 8);
		range("great miracle (150 Awe) every N s once meteors unlock", [=](const Result& r) { return r.nGreat ? every(150, r.incGreat / r.nGreat) : -1e9; }, 25, 60);
	}
	if (pk == PolicyKind::Passive) {
		range("Bronze year", ageY(1), 300, 700);
		range("Iron year", ageY(2), 700, 1300);
		range("Sail year", ageY(3), 1200, 2000);
		range("Industry year", ageY(4), 1900, 2800);
		range("Atomic year", ageY(5), 2500, 3400);
		range("Space year", ageY(6), 3000, 3900);
		range("Ark year", [](const Result& r) { return r.arkYear >= 0 ? r.arkYear : -1e9; }, 3300, 4500);
		yes("people survive to year 500", [](const Result& r) { return r.lostYear < 0 || r.lostYear >= 500; }, 1.0,
			[](const Result& r) { return r.lostYear >= 0 ? fmt("y%.0f", r.lostYear) : std::string("ok"); });
		range("settlements at 1000", [](const Result& r) { return r.settlAt[2] >= 0 ? r.settlAt[2] : -1e9; }, 8, 40);
		range("settlements at 3000", [](const Result& r) { return r.settlAt[6] >= 0 ? r.settlAt[6] : -1e9; }, 25, 120);
		range("cultures at 2000", [](const Result& r) { return r.cultAt[4] >= 0 ? r.cultAt[4] : -1e9; }, 2, 6);
		range("peak anomaly", [](const Result& r) { return r.peakAnom; }, 2, 5);
		range("sea rise", [](const Result& r) { return r.seaRise; }, 0.2, 0.5);
		yes("ends forgotten or parting (mostly)", [](const Result& r) { return r.ending && (r.ending->mood == Mood::Forgotten || r.ending->mood == Mood::Parting); },
			0.5, moodS);
		// reference curve (informational)
		const double ref[] = { 30, 2000, 20000, -1, 200000, -1, 2e6, 5e6, -1 };
		Target T;
		T.what = "reference population curve (informational: ratio to 30 / 2k / 20k / 200k / 2M / 5M)";
		T.need = false;
		for (const Result& r : rs) {
			std::string d;
			for (int i = 0; i < kNumCp; i++) if (ref[i] > 0 && r.popAt[size_t(i)] >= 0) d += fmt("%s%.2f", d.empty() ? "" : "/", r.popAt[size_t(i)] / ref[i]);
			T.detail += (T.detail.empty() ? "" : " ") + d;
		}
		ts.push_back(T);
	} else if (pk != PolicyKind::Doom) {     // (doom hoards Awe by design)
		yes("Awe below 90% of aweMax in >= 60% of years", [](const Result& r) { return r.years > 0 && r.yearsBelow90 / r.years >= 0.6; }, 1.0,
			[](const Result& r) { return fmt("%.0f%%", r.years > 0 ? 100 * r.yearsBelow90 / r.years : 0); });
		if (pk == PolicyKind::Kind)
			yes("kind ends beloved on >= 60% of seeds", [](const Result& r) { return r.ending && r.ending->mood == Mood::Beloved; }, 0.6, moodS);
		if (pk == PolicyKind::Cruel)
			yes("cruel (stops short of extinction) ends feared", [](const Result& r) { return r.ending && r.ending->mood == Mood::Feared; }, 0.5, moodS);
	}
	if (pk == PolicyKind::Doom) {
		yes("doom drives the world to silence", [](const Result& r) { return r.ending && r.ending->type == EndingType::Silence; }, 0.5, moodS);
	}
	// the performance targets use thread CPU time when available (wall time includes time this thread
	// was descheduled by other processes); the wall-clock maximum is listed for information
	auto meanT = [](const Result& r) { return r.cpuOk ? r.cpuMeanMs : r.meanMs; };
	auto maxT = [](const Result& r) { return r.cpuOk ? r.cpuMaxMs : r.maxMs; };
	yes("mean tick <= 2.5 ms (thread CPU time)", [=](const Result& r) { return meanT(r) <= 2.5; }, 1.0, [=](const Result& r) { return fmt("%.2f", meanT(r)); });
	yes("max tick <= 30 ms (thread CPU time)", [=](const Result& r) { return maxT(r) <= 30; }, 1.0, [=](const Result& r) { return fmt("%.1f", maxT(r)); });
	{
		Target T;
		T.what = "max tick, wall clock (informational: includes descheduled time)";
		T.need = false;
		for (const Result& r : rs) T.detail += (T.detail.empty() ? "" : " ") + fmt("%.1f", r.maxMs);
		ts.push_back(T);
	}
	std::string out = fmt("targets (SPEC-SIM 4), policy %s, %zu seeds:\n", simrun::toStr(pk), rs.size());
	for (const Target& T : ts) {
		const bool ok = !T.need || (T.n > 0 && double(T.pass) >= T.minShare * T.n - 1e-9);
		if (!ok) allPass = false;
		out += fmt("  %-5s %-62s %d/%d  [%s]\n", T.need ? (ok ? "PASS" : "FAIL") : "info", T.what.c_str(), T.pass, T.n, T.detail.c_str());
	}
	return out;
}

} // namespace

int main(int argc, char** argv) {
	Args args;
	bool listKeys = false;
	if (!parseArgs(argc, argv, args, listKeys)) { std::fputs(kHelp, stderr); return 1; }
	if (listKeys) {
		for (const std::string& k : balance::keys()) std::printf("%s\n", k.c_str());
		return 0;
	}
	{   // validate --set keys once
		auto probe = sim::create({ .seed = 1, .name = "probe", .L = 2 });
		for (const auto& kv : args.sets)
			if (!balance::set(*probe, kv.first, kv.second)) { std::fprintf(stderr, "--set: unknown key or bad value '%s'\n", kv.first.c_str()); return 1; }
	}
	if (const char* st = std::getenv("DM_SIMRUN_STUBS"); st && *st)
		std::printf("WARNING: these modules are linked as their neutral SimTests stubs, not the real code: %s\n", st);
	struct Job { uint32_t seed; PolicyKind pk; };
	std::vector<Job> jobs;
	for (PolicyKind pk : args.policies) for (uint32_t s : args.seeds) jobs.push_back({ s, pk });
	std::vector<Result> results(jobs.size());
	std::mutex outMu;
	const bool live = args.jobs == 1 && !args.json;
	std::atomic<size_t> nextJob{ 0 };
	auto worker = [&]() {
		for (;;) {
			const size_t k = nextJob.fetch_add(1);
			if (k >= jobs.size()) return;
			results[k] = runOne(args, jobs[k].seed, jobs[k].pk, live, outMu);
			if (!live && !args.json) {
				std::lock_guard<std::mutex> lk(outMu);
				std::fputs(results[k].text.c_str(), stdout);
				std::fflush(stdout);
			}
		}
	};
	const double t0 = nowMs();
	if (args.jobs <= 1 || jobs.size() <= 1) worker();
	else {
		std::vector<std::thread> pool;
		for (int i = 0; i < std::min<int>(args.jobs, int(jobs.size())); i++) pool.emplace_back(worker);
		for (std::thread& t : pool) t.join();
	}
	const double t1 = nowMs();
	if (args.json) for (const Result& r : results) std::printf("%s\n", toJson(r).c_str());

	bool nanFound = false;
	for (const Result& r : results) if (!r.nan.empty()) nanFound = true;
	bool allPass = true;
	if (args.summary || results.size() > 1) {
		if (!args.json) {
			std::printf("\n%s", table(results).c_str());
			if (args.summary) {
				for (PolicyKind pk : args.policies) {
					std::vector<Result> sub;
					for (const Result& r : results) if (r.policy == pk) sub.push_back(r);
					std::printf("\n%s", targets(sub, pk, allPass).c_str());
				}
			}
		}
	}
	if (!args.json && args.jobs > 1 && jobs.size() > 1)
		std::printf("\nnote: tick timings were measured with --jobs %d (threads contend for the heap and memory): use --jobs 1 for the performance targets\n", args.jobs);
	if (!args.json) std::printf("\nsimrun: %zu run(s) in %.1f s wall, %s\n", results.size(), (t1 - t0) / 1000, nanFound ? "NON-FINITE VALUES FOUND" : "no non-finite values");
	if (nanFound) return 3;
	if (args.summary && args.strict && !allPass) return 2;
	return 0;
}
