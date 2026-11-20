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

std::string cell(double v, const char* f = "%.0f") { return v >= 0 ? fmt(f, v) : std::string("-"); }

// What a culture remembers of its god (society::verdict; -1 = everyone), for the run report.
std::string memoryText(const World& w, int culture) {
	const society::Memory m = society::memory(w, culture);
	return fmt("grace %.2f wrath %.2f lore %+.2f verdict %+.2f silent %.0f y prayers %d/%d -> %s%s%s", m.grace, m.wrath, m.lore, m.verdict, m.silent,
		m.answered, m.prayers, toStr(m.mood), m.remembered ? " (remembered)" : "", m.mixed ? " (two-handed)" : "");
}

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
	bool moments = false;                    // --moments: sim moments per real minute by age and event
};

const char* kHelp =
	"simrun: headless DEMIURGE runs (all real sim modules)\n"
	"  --seed N | --seeds 1-6 | --seeds 1,4,9   world seeds (default 1)\n"
	"  --years Y          years of history to run (default 4000; stops at an ending)\n"
	"  --policy P[,P..]   passive | kind | cruel | chaotic | doom | curious | blessonly | prayers | repent | sculptor |\n"
	"                     late-kind | late-cruel | inspire-spam | hoarder | builder | atomic-kind | atomic-cruel |\n"
	"                     peacemaker | favourite | scourge (sim-findings-2 verifier) |\n"
	"                     all (default passive)\n"
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
	"  --moments          sim moments (wars, peoples, towns lost, prayers begun, great disasters ...) per real minute by age\n"
	"  --autopace         accepted for the design review's command line: every run already reports its auto-pace replay\n"
	"                     (2x while fewer than 2 notable entries fell in the last real minute, 1x for 20 s after a weight-3\n"
	"                     entry or a new prayer): real minutes and minutes without a notable entry, at 1x and auto-paced\n"
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
				if (v.substr(p, q - p) == "all") {
					for (int k2 = 0; k2 < int(PolicyKind::Count); k2++) a.policies.push_back(PolicyKind(k2));
					p = q + 1;
					continue;
				}
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
		else if (k == "--moments") a.moments = true;
		else if (k == "--autopace") {}                         // (the auto-pace replay is always in the report: "pace:" and the pacing table)
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
	// ---- design review measures (docs/reviews/claude-design-review.md)
	std::array<double, powers::kPowerCount> unlockYear{};   // first history year each power was unlocked (-1 never)
	double settl200 = -1, settl400 = -1, aweMax1000 = -1;
	double firstPrayer = -1, secondCulture = -1, firstWar = -1;
	double warYears = 0, hoardYears = 0;
	double gapNotable = -1;                          // longest stretch with no weight >= 2 entry in years 0-1000
	int famineChron = 0, prayersStarted = 0, prayersAnswered = 0;
	int rbAnswered = 0, rbFed = 0;                   // answered rain / bloom prayers; of them famine 0 within 5 years
	double moodAgree = 0, moodYears = 0;
	bool belovedEarly = false;                       // regard(everyone) beloved before year 20
	std::string epithet;
	bool drowner = false;
	int lowerDrowned = 0, lowerSeaRose = 0;          // towns drowned within 2 years of a Lower stroke within 0.1 rad; of them told "sea rose"
	double endDevotion = 0, endLove = 0;             // regard of the Ark culture (everyone without an Ark) at the end
	std::string memory;                              // the Ark culture's memory of the god (society::verdict), when present
	// ---- findings 11 and 13 (wars, alliances, pacing by age; sim-findings-2)
	std::array<double, Age::COUNT> ageYears{};      // history years lived in each world age (before an ending)
	std::array<int, Age::COUNT> warsAge{};          // war:declared per world age
	std::array<double, Age::COUNT> warYearsAge{};   // years with any war, per world age
	std::array<int, Age::COUNT> momentsAge{};       // sim moments (see isMoment) per world age
	std::map<std::string, std::array<int, Age::COUNT>> momentsBy;   // the same by event name (--moments)
	std::array<std::array<int, 4>, Age::COUNT> chronAge{};   // chronicle entries per world age by weight (0 unused, 1-3)
	int alliancesFormed = 0, peakAlliances = 0;     // measured alike before and after: a pair in contact, not at war, v >= 0.5 (until < 0.25 or war)
	double allyYears = 0;                            // years with at least one such alliance
	int allianceEvents = 0, joinedWars = 0;          // (sim-findings-2) alliance {formed} events; wars an ally joined
	int blamedWars = 0, thankedWars = 0;             // (sim-findings-2) wars blamed on the god; wars whose victor thanked the god
	int nukeExchanges = 0;
	double lastNukeYear = -1e9;
	std::vector<double> prayerYears;                 // years a prayer began (auto-pace)
	double autoMinutes = -1, autoZeroMinutes = -1, plainZeroMinutes = -1, plainMinutes = -1;
	std::vector<Row> rows;
	std::string text;                                // everything printed for this run
	bool crashed = false;
};

// The sim moments counted per age for finding 13 (things that happen in the world, before any prose):
// wars declared / ended, peoples born / dead, towns destroyed or captured, Wonders, prophets, prayers
// begun, nukes, quakes, eruptions, meteor impacts, droughts begun, Arks begun, alliances (v3.1) made or broken.
bool isMoment(const Event& e) {
	switch (e.type) {
	case Ev::WarDeclared: case Ev::WarEnded: case Ev::CultureBorn: case Ev::CultureDied: case Ev::SettlementDestroyed:
	case Ev::SettlementCaptured: case Ev::WonderBuilt: case Ev::ProphetRisen: case Ev::Nuke: case Ev::Quake: case Ev::VolcanoStart:
	case Ev::MeteorImpact: case Ev::ArkStarted: case Ev::Alliance:
		return true;
	case Ev::Prayer: { const EvPrayer* p = e.as<EvPrayer>(); return p && p->start; }
	case Ev::Drought: { const EvDrought* d = e.as<EvDrought>(); return d && d->start; }
	default: return false;
	}
}

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
	R.unlockYear.fill(-1);
	auto say =[&](const std::string& s) {
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
	// design review bookkeeping: answered rain / bloom prayers waiting to see the famine end; recent Lower strokes
	struct Fed { int sid; double year; };
	std::vector<Fed> fedWait;
	struct Stroke { int cell; double year; };
	std::vector<Stroke> lowers;
	std::vector<int> lowerVictims;                   // settlement ids drowned right after a nearby Lower stroke
	std::map<std::pair<int, int>, bool> allyState;   // the measured alliances (see Result::alliancesFormed)
	auto subscribe = [&](World& w) {
		w.bus.onAll([&](const Event& e) {
			const int t = int(e.type);
			if (t >= 0 && t < kEventCount) { R.events[size_t(t)]++; window[size_t(t)]++; inTick[size_t(t)]++; }
			const size_t ageNow = size_t(std::clamp(wp->age, 0, Age::COUNT - 1));
			if (wp->phase == Phase::History && !wp->ending && isMoment(e)) { R.momentsAge[ageNow]++; R.momentsBy[eventName(e.type)][ageNow]++; }
			if (e.type == Ev::WarDeclared && wp->phase == Phase::History) R.warsAge[ageNow]++;
			if (const EvWarDeclared* wd = e.as<EvWarDeclared>()) { if (wd->ally >= 0) R.joinedWars++; if (wd->blamed) R.blamedWars++; }
			if (const EvWarEnded* we = e.as<EvWarEnded>(); we && we->thanked) R.thankedWars++;
			if (const EvAlliance* al = e.as<EvAlliance>(); al && al->formed) R.allianceEvents++;
			if (e.type == Ev::Nuke && wp->year != R.lastNukeYear) { R.nukeExchanges++; R.lastNukeYear = wp->year; }
			if (e.type == Ev::Prayer) {
				if (const EvPrayer* p = e.as<EvPrayer>(); p && p->start) {
					R.prayersStarted++;
					R.prayerYears.push_back(wp->year);
					if (R.firstPrayer < 0) R.firstPrayer = wp->year;
				}
			} else if (e.type == Ev::PrayerAnswered) {
				R.prayersAnswered++;
				if (const EvPrayerAnswered* p = e.as<EvPrayerAnswered>(); p && (p->kind == PrayerKind::Rain || p->kind == PrayerKind::Bloom)) {
					R.rbAnswered++;
					fedWait.push_back({ p->id, wp->year });
				}
			} else if (e.type == Ev::WarDeclared) {
				if (R.firstWar < 0) R.firstWar = wp->year;
			} else if (e.type == Ev::PowerUsed) {
				if (const EvPowerUsed* p = e.as<EvPowerUsed>(); p && p->id == "lower" && wp->phase == Phase::History) {
					lowers.push_back({ p->cell, wp->year });
					if (lowers.size() > 256) lowers.erase(lowers.begin(), lowers.begin() + 128);
				}
			}
			if (e.type == Ev::SettlementDestroyed) {
				if (const EvSettlementDestroyed* d = e.as<EvSettlementDestroyed>(); d && d->cause == Cause::Drowned && wp->valid(d->cell)) {
					for (const Stroke& st : lowers)
						if (wp->year - st.year <= 2 && wp->valid(st.cell) && wp->geo->angle(st.cell, d->cell) <= 0.1) { lowerVictims.push_back(d->id); break; }
				}
			}
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
		{   // (v3.1) the god's recent favour per living culture (society_fgrace / fwrath, fading sums), and alliances
			std::string fav;
			for (const Culture& c : w.cultures) {
				if (c.dead) continue;
				auto get = [&](const char* k) { const auto it = w.vars.num.find(std::string("society_") + k + "_" + std::to_string(c.id)); return it == w.vars.num.end() ? 0.0 : it->second; };
				const double g = get("fgrace"), x = get("fwrath");
				if (g > 0 || x > 0) fav += fmt(" %d:%.2f/%.2f", c.id, g, x);
			}
			int allies = 0;
			for (const auto& kv : w.vars.num) if (kv.first.rfind("diplomacy_ally", 0) == 0) allies++;
			d += fmt(" | favour sums g/w%s | alliances %d", fav.empty() ? " -" : fav.c_str(), allies);
		}
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
		// ---- design review measures
		if (!w.ending) {
			for (int k = 0; k < powers::kPowerCount; k++)
				if (R.unlockYear[size_t(k)] < 0 && powers::kPowers[k].group != powers::Group::Genesis && powers::unlocked(w, powers::kPowers[k].id))
					R.unlockYear[size_t(k)] = w.year;
			if (R.settl200 < 0 && w.year + 1e-9 >= 200) R.settl200 = settl;
			if (R.settl400 < 0 && w.year + 1e-9 >= 400) R.settl400 = settl;
			if (R.aweMax1000 < 0 && w.year + 1e-9 >= 1000) R.aweMax1000 = w.god.aweMax;
			if (R.secondCulture < 0 && cult >= 2) R.secondCulture = w.year;
			bool war = false;
			for (const auto& kv : w.vars.rel) if (kv.second.war) { war = true; break; }
			if (war) R.warYears++;
			{   // findings 11 / 13: years and war years by world age; alliances (one definition before and after)
				const size_t a = size_t(std::clamp(w.age, 0, Age::COUNT - 1));
				R.ageYears[a]++;
				if (war) R.warYearsAge[a]++;
				int allies = 0;
				for (const auto& kv : w.vars.rel) {
					const Culture* ca = state::culture(w, kv.first.first);
					const Culture* cb = state::culture(w, kv.first.second);
					const bool both = ca && cb && !ca->dead && !cb->dead;
					bool& st = allyState[kv.first];
					if (!both || kv.second.war || !kv.second.contact || kv.second.v < 0.25) st = false;
					else if (!st && kv.second.v >= 0.5) { st = true; R.alliancesFormed++; }
					if (st) allies++;
				}
				R.peakAlliances = std::max(R.peakAlliances, allies);
				if (allies > 0) R.allyYears++;
			}
			if (w.god.awe >= 0.95 * w.god.aweMax) R.hoardYears++;
			if (settl > 0) {
				int lead = -1;
				for (const Culture& c : w.cultures) {
					if (c.dead) continue;
					if (lead < 0 || c.age > w.cultures[size_t(lead)].age || (c.age == w.cultures[size_t(lead)].age && c.pop > w.cultures[size_t(lead)].pop)) lead = c.id;
				}
				const Mood all = society::regard(w, -1).mood;
				R.moodYears++;
				if (lead >= 0 && society::regard(w, lead).mood == all) R.moodAgree++;
				if (w.year < 20 && all == Mood::Beloved) R.belovedEarly = true;
			}
			for (size_t k = 0; k < fedWait.size();) {
				const Settlement* s = state::settlement(w, fedWait[k].sid);
				const bool fed = s && !s->dead && s->famine == 0;
				if (fed) R.rbFed++;
				if (fed || !s || s->dead || w.year - fedWait[k].year > 5) fedWait.erase(fedWait.begin() + std::ptrdiff_t(k));
				else k++;
			}
		}
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
	{   // design review measures read off the finished world
		double last = 0, gap = 0;
		for (const ChronicleEntry& c : w.chronicle) {
			if (c.type == "famine") R.famineChron++;
			if (c.weight < 2 || c.year < 0 || c.year > 1000) continue;
			gap = std::max(gap, c.year - last);
			last = c.year;
		}
		R.gapNotable = std::max(gap, std::min(1000.0, w.year) - last);
		// findings 11 / 13: chronicle entries per world age by weight, and the auto-pace replay
		const double endYear = w.ending ? w.ending->year : w.year;
		auto ageAt = [&](double y) {
			int a = 0;
			for (int k = 1; k < Age::COUNT; k++) if (R.ageYear[size_t(k)] >= 0 && R.ageYear[size_t(k)] <= y + 1e-9) a = k;
			return a;
		};
		std::vector<double> notable, epochal;
		for (const ChronicleEntry& c : w.chronicle) {
			if (c.year < 0 || c.year > endYear + 1e-9) continue;
			const int wt = std::clamp(c.weight, 1, 3);
			R.chronAge[size_t(ageAt(c.year))][size_t(wt)]++;
			if (wt >= 2) notable.push_back(c.year);
			if (wt >= 3) epochal.push_back(c.year);
		}
		std::sort(notable.begin(), notable.end());
		std::sort(epochal.begin(), epochal.end());
		std::vector<double> prays = R.prayerYears;
		std::sort(prays.begin(), prays.end());
		// Auto-pace (design review finding 13): 2x while fewer than 2 notable entries fell in the last real
		// minute, back to 1x for 20 real seconds on any weight-3 entry or new prayer. 1x = 2 years a second.
		{
			double now = 0, holdUntil = -1;
			size_t iN = 0, iE = 0, iP = 0;
			std::vector<double> seen;                    // real seconds of notable entries so far
			int zero = 0;
			double binStart = 0;
			bool binHas = false;
			for (double y = 0; y < endYear; y += 1) {
				size_t recent = 0;
				for (size_t k = seen.size(); k > 0 && seen[k - 1] >= now - 60; k--) recent++;
				const double speed = (now < holdUntil || recent >= 2) ? 1 : 2;
				now += 0.5 / speed;
				while (now - binStart >= 60) { if (!binHas) zero++; binHas = false; binStart += 60; }
				while (iN < notable.size() && notable[iN] < y + 1) { seen.push_back(now); binHas = true; iN++; }
				bool force = false;
				while (iE < epochal.size() && epochal[iE] < y + 1) { force = true; iE++; }
				while (iP < prays.size() && prays[iP] < y + 1) { force = true; iP++; }
				if (force) holdUntil = now + 20;
			}
			R.autoMinutes = now / 60;
			R.autoZeroMinutes = zero;
			// at 1x: bins of 120 years
			int zero1 = 0;
			size_t k = 0;
			for (double b = 0; b + 120 <= endYear; b += 120) {
				bool has = false;
				while (k < notable.size() && notable[k] < b + 120) { if (notable[k] >= b) has = true; k++; }
				if (!has) zero1++;
			}
			R.plainZeroMinutes = zero1;
			R.plainMinutes = endYear / 120;
		}
		R.epithet = w.god.epithet;
		static const char* const kDrowner[] = { "the Drowner", "the Sea's Elbow", "the Deep" };
		for (const char* d : kDrowner) if (R.epithet == d) R.drowner = true;
		R.lowerDrowned = int(lowerVictims.size());
		for (int sid : lowerVictims) {
			const Settlement* s = state::settlement(w, sid);
			if (!s) continue;
			for (const ChronicleEntry& c : w.chronicle)
				if (c.cell == s->cell && c.text.find("sea rose") != std::string::npos && c.text.find(s->name) != std::string::npos) { R.lowerSeaRose++; break; }
		}
		const society::Regard rf = society::regard(w, w.ending && w.ending->culture >= 0 ? w.ending->culture : -1);
		R.endDevotion = rf.devotion;
		R.endLove = rf.love;
		R.memory = memoryText(w, w.ending && w.ending->culture >= 0 ? w.ending->culture : -1);
	}

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
		// (verifier) what the Ark's culture (else everyone) remembers: the parts of the ending verdict
		const society::Memory m = society::memory(w, arkC);
		say(fmt("   memory (%s): grace %.2f wrath %.2f neglect %.1f (presence %.2f) lore %+.3f verdict %+.3f silent %.0f y prayers %d answered %d -> %s",
			arkC >= 0 ? "Ark culture" : "everyone", m.grace, m.wrath, m.neglect, m.presence, m.lore, m.verdict, m.silent, m.prayers, m.answered, toStr(m.mood)));
	}
	{
		std::string unl;
		for (int k = 0; k < powers::kPowerCount; k++) {
			if (powers::kPowers[k].group == powers::Group::Genesis) continue;
			unl += fmt(" %s %s", powers::kPowers[k].id, R.unlockYear[size_t(k)] >= 0 ? fmt("%.0f", R.unlockYear[size_t(k)]).c_str() : "-");
		}
		say("   unlocks:" + unl);
		say(fmt("   review: settlements @200 %s @400 %s; first prayer %s; second culture %s; first war %s, war in %.1f%% of years; longest quiet (no weight-2 entry, years 0-1000) %.0f y;"
			" aweMax@1000 %s; Awe >= 95%% of the cap in %.0f%% of years; prayers %d, answered %d; rain/bloom answered %d, famine over within 5 y %d;"
			" famine entries %d; HUD mood = lead mood in %.0f%% of years%s; epithet '%s'%s; drowned after Lower %d (told 'sea rose' %d); end regard %.3f/%+.3f%s%s",
			cell(R.settl200).c_str(), cell(R.settl400).c_str(), cell(R.firstPrayer).c_str(), cell(R.secondCulture).c_str(), cell(R.firstWar).c_str(),
			R.years > 0 ? 100 * R.warYears / R.years : 0.0, R.gapNotable, cell(R.aweMax1000).c_str(), R.years > 0 ? 100 * R.hoardYears / R.years : 0.0,
			R.prayersStarted, R.prayersAnswered, R.rbAnswered, R.rbFed, R.famineChron, R.moodYears > 0 ? 100 * R.moodAgree / R.moodYears : 100.0,
			R.belovedEarly ? "; BELOVED before year 20" : "", R.epithet.c_str(), R.drowner ? " (Drowner group)" : "", R.lowerDrowned, R.lowerSeaRose,
			R.endDevotion, R.endLove, R.memory.empty() ? "" : "; memory ", R.memory.c_str()));
	}
	{   // findings 11 / 13
		std::string l = "   pacing by age (years / wars / war% / moments per min / chronicle w2, w3 per min):";
		for (int a = 0; a < Age::COUNT; a++) {
			const double y = R.ageYears[size_t(a)];
			if (y <= 0) continue;
			const double mins = y / 120;
			l += fmt(" %s %.0f/%d/%.0f%%/%.1f/%.1f,%.2f;", ageName(a), y, R.warsAge[size_t(a)], 100 * R.warYearsAge[size_t(a)] / y, R.momentsAge[size_t(a)] / mins,
				R.chronAge[size_t(a)][2] / mins, R.chronAge[size_t(a)][3] / mins);
		}
		say(l);
		if (args.moments) {
			for (const auto& kv : R.momentsBy) {
				std::string m = fmt("     moments %-20s", kv.first.c_str());
				for (int a = 0; a < Age::COUNT; a++) m += R.ageYears[size_t(a)] > 0 ? fmt(" %6.1f", kv.second[size_t(a)] / (R.ageYears[size_t(a)] / 120)) : std::string("      -");
				say(m + "  (per real minute by age)");
			}
		}
		say(fmt("   diplomacy: alliances formed %d (peak %d at once, %.0f%% of years), alliance events %d, wars joined by an ally %d, blamed on the god %d,"
			" victor thanked the god %d, nuclear exchanges %d; pace: %.1f min at 1x (%.0f without a notable entry), auto-pace %.1f min (%.0f without)",
			R.alliancesFormed, R.peakAlliances, R.years > 0 ? 100 * R.allyYears / R.years : 0.0, R.allianceEvents, R.joinedWars, R.blamedWars, R.thankedWars,
			R.nukeExchanges, R.plainMinutes, R.plainZeroMinutes, R.autoMinutes, R.autoZeroMinutes));
	}
	if (args.hash) {
		std::vector<uint8_t> bytes;
		std::string why;
		const bool ser = state::serialize(w, bytes, &why);
		say(fmt("   hash %08x%s%s", R.hash, ser ? "" : " (the world did not serialize, so the hash leaves out its state: ", ser ? "" : (why + ")").c_str()));
	}
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

int powerIndex(const char* id) {
	for (int k = 0; k < powers::kPowerCount; k++) if (std::string_view(powers::kPowers[k].id) == id) return k;
	return -1;
}
double unlockOf(const Result& r, const char* id) {
	const int k = powerIndex(id);
	return k >= 0 ? r.unlockYear[size_t(k)] : -1;
}

// Ages that bring a power upgrade (the design review's "an upgrade per late age").
std::vector<int> upgradeAges() {
	std::vector<int> out;
	for (const powers::Upgrade& u : powers::kUpgrades) out.push_back(u.age);
	return out;
}

// How many ages from Bronze to Space woke a power (unlocked within a year of the age) or upgraded one.
int ageGifts(const Result& r) {
	const std::vector<int> up = upgradeAges();
	int n = 0;
	for (int a = 1; a < Age::COUNT; a++) {
		bool gift = std::find(up.begin(), up.end(), a) != up.end();
		const double y = r.ageYear[size_t(a)];
		for (int k = 0; k < powers::kPowerCount && !gift && y >= 0; k++)
			if (r.unlockYear[size_t(k)] >= 0 && std::fabs(r.unlockYear[size_t(k)] - y) <= 1.01) gift = true;
		if (gift) n++;
	}
	return n;
}

// The design review's per-run table (docs/reviews/claude-design-review.md).
std::string reviewTable(const std::vector<Result>& rs) {
	std::string t = "seed policy       | ending           |  Ark | Bronze | s@200 s@400 | pray1 cult2 war1 | quiet | aweMax@1k cap% >=95% | plague meteor volcano | lost fam | end dev/love  | epithet\n";
	for (const Result& r : rs) {
		std::string end = r.ending ? fmt("%s/%s %.0f", toStr(r.ending->type), toStr(r.ending->mood), r.ending->year) : std::string("none");
		t += fmt("%4u %-12s | %-16s | %4s | %6s | %5s %5s | %5s %5s %4s | %5.0f | %9s %4.0f %5.0f | %6s %6s %7s | %4d %3d | %.2f/%+.2f | %s\n",
			r.seed, simrun::toStr(r.policy), end.c_str(), cell(r.arkYear).c_str(), cell(r.ageYear[1]).c_str(), cell(r.settl200).c_str(), cell(r.settl400).c_str(),
			cell(r.firstPrayer).c_str(), cell(r.secondCulture).c_str(), cell(r.firstWar).c_str(), r.gapNotable, cell(r.aweMax1000).c_str(),
			r.years > 0 ? 100 * r.yearsCap / r.years : 0.0, r.years > 0 ? 100 * r.hoardYears / r.years : 0.0, cell(unlockOf(r, "plague")).c_str(),
			cell(unlockOf(r, "meteor")).c_str(), cell(unlockOf(r, "volcano")).c_str(), r.lostCount, r.famineChron, r.endDevotion, r.endLove, r.epithet.c_str());
	}
	return t;
}

// ---------------------------------------------------------------- findings 11 / 13: tables per policy

std::string triple(std::vector<double> v, const char* f = "%.0f") {
	if (v.empty()) return "-";
	std::sort(v.begin(), v.end());
	const double med = v.size() % 2 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
	return fmt(f, v.front()) + "/" + fmt(f, med) + "/" + fmt(f, v.back());
}

// Mean over the runs that lived in age a of a per-run rate (runs that never reached it are skipped).
template <class F>
std::string byAge(const std::vector<const Result*>& rs, F rate, const char* f = "%4.1f") {
	std::string o;
	for (int a = 0; a < Age::COUNT; a++) {
		double s = 0;
		int n = 0;
		for (const Result* r : rs) {
			if (r->ageYears[size_t(a)] <= 0) continue;
			s += rate(*r, a);
			n++;
		}
		o += " " + (n ? fmt(f, s / n) : std::string(fmt(f, 0.0).size(), '-'));
	}
	return o;
}

std::string pacingTables(const std::vector<Result>& all, const std::vector<PolicyKind>& pks) {
	std::string t = "diplomacy (design review finding 11; min/median/max over seeds; wars per age = mean declarations in Stone Bronze Iron Sail Industry Atomic Space):\n";
	t += "policy       | war% of years   | wars        | first war        <=1300 | wars per age  S    B    I    Sa   In   At   Sp  | v>=.5 formed peak yrs% | pacts | joined blamed thanked | nuke-ex max | omens\n";
	for (PolicyKind pk : pks) {
		std::vector<const Result*> rs;
		for (const Result& r : all) if (r.policy == pk) rs.push_back(&r);
		if (rs.empty()) continue;
		std::vector<double> share, wars, first, formed, allyPct, omens;
		int early = 0, peak = 0, nukeMax = 0, joined = 0, blamed = 0, thanked = 0;
		double pacts = 0;
		for (const Result* r : rs) {
			share.push_back(r->years > 0 ? 100 * r->warYears / r->years : 0);
			wars.push_back(r->events[size_t(Ev::WarDeclared)]);
			first.push_back(r->firstWar >= 0 ? r->firstWar : 9999);
			if (r->firstWar >= 0 && r->firstWar <= 1300) early++;
			formed.push_back(r->alliancesFormed);
			allyPct.push_back(r->years > 0 ? 100 * r->allyYears / r->years : 0);
			peak = std::max(peak, r->peakAlliances);
			nukeMax = std::max(nukeMax, r->nukeExchanges);
			joined += r->joinedWars;
			pacts += r->allianceEvents;
			blamed += r->blamedWars;
			thanked += r->thankedWars;
			const auto it = r->pstats.byPower.find("peace");
			omens.push_back(it == r->pstats.byPower.end() ? 0 : it->second);
		}
		double fm = 0, ap = 0, om = 0;
		for (double v : formed) fm += v;
		for (double v : allyPct) ap += v;
		for (double v : omens) om += v;
		t += fmt("%-12s | %-15s | %-11s | %-16s %2d/%-2zu |", simrun::toStr(pk), triple(share, "%.1f").c_str(), triple(wars).c_str(), triple(first).c_str(), early, rs.size());
		t += byAge(rs, [](const Result& r, int a) { return double(r.warsAge[size_t(a)]); });
		t += fmt(" | %13.1f %4d %4.0f | %5.1f | %6d %6d %7d | %11d | %5.1f\n", fm / double(rs.size()), peak, ap / double(rs.size()), pacts / double(rs.size()), joined, blamed,
			thanked, nukeMax, om / double(rs.size()));
	}
	t += "\npacing (design review finding 13; per real minute at 1x, 120 years a minute, mean over seeds that reached the age: S B I Sa In At Sp):\n";
	t += "policy       | sim moments / min                    | chronicle weight 2 / min             | chronicle weight 3 / min             | war % of the age                     | 1x min      | auto-pace min | 0-notable min 1x / auto\n";
	for (PolicyKind pk : pks) {
		std::vector<const Result*> rs;
		for (const Result& r : all) if (r.policy == pk) rs.push_back(&r);
		if (rs.empty()) continue;
		std::vector<double> m1, ma;
		double z1 = 0, za = 0;
		for (const Result* r : rs) { m1.push_back(r->plainMinutes); ma.push_back(r->autoMinutes); z1 += r->plainZeroMinutes; za += r->autoZeroMinutes; }
		t += fmt("%-12s |", simrun::toStr(pk));
		t += byAge(rs, [](const Result& r, int a) { return r.momentsAge[size_t(a)] / (r.ageYears[size_t(a)] / 120); });
		t += " |" + byAge(rs, [](const Result& r, int a) { return r.chronAge[size_t(a)][2] / (r.ageYears[size_t(a)] / 120); });
		t += " |" + byAge(rs, [](const Result& r, int a) { return r.chronAge[size_t(a)][3] / (r.ageYears[size_t(a)] / 120); });
		t += " |" + byAge(rs, [](const Result& r, int a) { return 100 * r.warYearsAge[size_t(a)] / r.ageYears[size_t(a)]; }, "%4.0f");
		t += fmt(" | %-11s | %-13s | %.1f / %.1f\n", triple(m1, "%.1f").c_str(), triple(ma, "%.1f").c_str(), z1 / double(rs.size()), za / double(rs.size()));
	}
	t += "\nending mix (Ark endings by mood; silence; none):\n";
	t += "policy       | beloved feared forgotten parting | silence none\n";
	for (PolicyKind pk : pks) {
		std::array<int, 6> cnt{};
		int sil = 0, none = 0, n = 0;
		for (const Result& r : all) {
			if (r.policy != pk) continue;
			n++;
			if (!r.ending) none++;
			else if (r.ending->type == EndingType::Silence) sil++;
			else cnt[size_t(r.ending->mood)]++;
		}
		if (!n) continue;
		t += fmt("%-12s | %7d %6d %9d %7d | %7d %4d\n", simrun::toStr(pk), cnt[size_t(Mood::Beloved)], cnt[size_t(Mood::Feared)], cnt[size_t(Mood::Forgotten)],
			cnt[size_t(Mood::Parting)], sil, none);
	}
	return t;
}

// Finding 13: sim moments per real minute in the late ages (Industry, Atomic, Space) over the early ones
// (Stone, Bronze), means over the ages the run lived in (v3 passive: about 4.6).
double frontBack(const Result& r) {
	auto mean = [&](int lo, int hi) {
		double s = 0;
		int n = 0;
		for (int a = lo; a <= hi; a++) {
			if (r.ageYears[size_t(a)] <= 0) continue;
			s += r.momentsAge[size_t(a)] / (r.ageYears[size_t(a)] / 120);
			n++;
		}
		return n ? s / n : 0.0;
	};
	const double early = mean(Age::STONE, Age::BRONZE), late = mean(Age::INDUSTRY, Age::SPACE);
	return early > 0 ? late / early : 99;
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
	} else if (pk == PolicyKind::Kind || pk == PolicyKind::Cruel || pk == PolicyKind::Chaotic) {     // the always-active players (doom hoards Awe by design)
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
	// ---- the design review's targets (docs/reviews/claude-design-review.md, findings 1-7, 9, 10;
	//      "x of 6" there is the share x / 6 here, so 8 seeds need the same share)
	auto moodIs = [](Mood m) { return [m](const Result& r) { return r.ending && r.ending->type == EndingType::Ark && r.ending->mood == m; }; };
	auto fedShare = [&](const std::string& what) {
		Target T;
		T.what = what;
		int a = 0, f = 0;
		for (const Result& r : rs) { a += r.rbAnswered; f += r.rbFed; T.detail += (T.detail.empty() ? "" : " ") + fmt("%d/%d", r.rbFed, r.rbAnswered); }
		T.n = 1;
		T.pass = a == 0 || f >= 0.8 * a ? 1 : 0;
		ts.push_back(T);
	};
	auto capPct = [](const Result& r) { return fmt("%.0f%%", r.years > 0 ? 100 * r.yearsCap / r.years : 0); };
	if (pk == PolicyKind::Passive) {
		yes("review 2: passive ends forgotten on >= 5/6", moodIs(Mood::Forgotten), 5.0 / 6, moodS);
		yes("review 4: settlements >= 3 at year 200", [](const Result& r) { return r.settl200 >= 3; }, 1.0, [](const Result& r) { return cell(r.settl200); });
		yes("review 4: settlements >= 6 at year 400", [](const Result& r) { return r.settl400 >= 6; }, 1.0, [](const Result& r) { return cell(r.settl400); });
		yes("review 4: first prayer by year 80 on >= 5/6", [](const Result& r) { return r.firstPrayer >= 0 && r.firstPrayer <= 80; }, 5.0 / 6, [](const Result& r) { return cell(r.firstPrayer); });
		yes("review 4: second culture by year 650 on >= 4/6", [](const Result& r) { return r.secondCulture >= 0 && r.secondCulture <= 650; }, 4.0 / 6, [](const Result& r) { return cell(r.secondCulture); });
		yes("review 4: longest stretch without a weight-2 entry in 0-1000 <= 150 y", [](const Result& r) { return r.gapNotable <= 150; }, 1.0, [](const Result& r) { return fmt("%.0f", r.gapNotable); });
		yes("review 6: passive at the Awe cap in <= 50% of years", [](const Result& r) { return r.years > 0 && r.yearsCap / r.years <= 0.5; }, 1.0, capPct);
		yes("review 5: meteor not before Sail, volcano not before Industry", [](const Result& r) {
			const double m = unlockOf(r, "meteor"), v = unlockOf(r, "volcano");
			return (m < 0 || (r.ageYear[3] >= 0 && m >= r.ageYear[3])) && (v < 0 || (r.ageYear[4] >= 0 && v >= r.ageYear[4]));
		}, 1.0, [](const Result& r) { return fmt("%s/%s", cell(unlockOf(r, "meteor")).c_str(), cell(unlockOf(r, "volcano")).c_str()); });
		yes("review 5: every age from Bronze to Space unlocks or upgrades a power", [](const Result& r) { return ageGifts(r) == 6; }, 1.0,
			[](const Result& r) { return fmt("%d/6", ageGifts(r)); });
		yes("review 5: aweMax at year 1000 <= 600", [](const Result& r) { return r.aweMax1000 >= 0 && r.aweMax1000 <= 600; }, 1.0, [](const Result& r) { return cell(r.aweMax1000); });
		yes("review 7: the final epithet is never in the Drowner group", [](const Result& r) { return !r.drowner; }, 1.0, [](const Result& r) { return r.epithet; });
		Target T;
		T.what = "review 11 (informational): passive at war in 8-15% of years; review 12: not beloved before year 20";
		T.need = false;
		for (const Result& r : rs) T.detail += (T.detail.empty() ? "" : " ") + fmt("%.1f%%%s", r.years > 0 ? 100 * r.warYears / r.years : 0, r.belovedEarly ? "B" : "");
		ts.push_back(T);
		// ---- findings 11 and 13 (sim-findings-2, SPEC-SIM 4 "(v3.1, design review 11/13)")
		auto warPct = [](const Result& r) { return r.years > 0 ? 100 * r.warYears / r.years : 0.0; };
		{
			Target M;
			M.what = "review 11: passive at war in 8-15% of years (median of the seeds), every seed 3-25%";
			std::vector<double> v;
			bool each = true;
			for (const Result& r : rs) { v.push_back(warPct(r)); each = each && warPct(r) >= 3 && warPct(r) <= 25; M.detail += (M.detail.empty() ? "" : " ") + fmt("%.1f", warPct(r)); }
			std::sort(v.begin(), v.end());
			const double med = v.empty() ? 0 : v.size() % 2 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
			M.n = 1;
			M.pass = med >= 8 && med <= 15 && each ? 1 : 0;
			M.detail = fmt("median %.1f; ", med) + M.detail;
			ts.push_back(M);
		}
		yes("review 11: first war by year 1300 on >= 5/6", [](const Result& r) { return r.firstWar >= 0 && r.firstWar <= 1300; }, 5.0 / 6, [](const Result& r) { return cell(r.firstWar); });
		yes("review 11: at most one nuclear exchange per passive run", [](const Result& r) { return r.nukeExchanges <= 1; }, 1.0, [](const Result& r) { return fmt("%d", r.nukeExchanges); });
		yes("review 11: alliances form in every passive run", [](const Result& r) { return r.allianceEvents >= 1; }, 1.0, [](const Result& r) { return fmt("%d", r.allianceEvents); });
		yes("review 13: auto-pace history 25-35 real minutes", [](const Result& r) { return r.autoMinutes >= 25 && r.autoMinutes <= 35; }, 1.0, [](const Result& r) { return fmt("%.1f", r.autoMinutes); });
		yes("review 13: late moments (Industry-Space) per minute <= 3.5 x early ones (Stone-Bronze), >= 5/6", [](const Result& r) { return frontBack(r) <= 3.5; }, 5.0 / 6,
			[](const Result& r) { return fmt("%.2f", frontBack(r)); });
	}
	if (pk != PolicyKind::Doom)
		yes("review 11: never constant war (at war in at most a third of the years)", [](const Result& r) { return r.years > 0 && r.warYears / r.years <= 1.0 / 3; }, 1.0,
			[](const Result& r) { return fmt("%.1f", r.years > 0 ? 100 * r.warYears / r.years : 0); });
	if (pk == PolicyKind::Kind) {
		yes("review 1/2: kind ends beloved on >= 6/8", moodIs(Mood::Beloved), 6.0 / 8, moodS);
		fedShare("review 9: answered rain / bloom prayers end the famine within 5 years (>= 80 %)");
	}
	if (pk == PolicyKind::Cruel) yes("review 1/2: cruel ends feared on >= 6/8", moodIs(Mood::Feared), 6.0 / 8, moodS);
	if (pk == PolicyKind::Cruel)
		yes("review 11: the god is blamed: a cruel god's peoples blame it for >= 3 wars on >= 5/6", [](const Result& r) { return r.blamedWars >= 3; }, 5.0 / 6,
			[](const Result& r) { return fmt("%d", r.blamedWars); });
	if (pk == PolicyKind::Kind) {
		yes("review 11: the Omen of Peace matters: kind casts it >= 10 times on >= 5/6", [](const Result& r) {
			const auto it = r.pstats.byPower.find("peace");
			return it != r.pstats.byPower.end() && it->second >= 10;
		}, 5.0 / 6, [](const Result& r) { const auto it = r.pstats.byPower.find("peace"); return fmt("%d", it == r.pstats.byPower.end() ? 0 : it->second); });
		yes("review 11: the god is thanked: kind's victors thank it for >= 1 war on >= 5/6", [](const Result& r) { return r.thankedWars >= 1; }, 5.0 / 6,
			[](const Result& r) { return fmt("%d", r.thankedWars); });
	}
	if (pk == PolicyKind::Chaotic || pk == PolicyKind::Curious) yes("review 2: ends parting on >= 4/6", moodIs(Mood::Parting), 4.0 / 6, moodS);
	if (pk == PolicyKind::Prayers) {
		yes("review 2: prayers ends beloved on >= 4/6", moodIs(Mood::Beloved), 4.0 / 6, moodS);
		yes("review 6: prayers at the Awe cap in <= 15% of years", [](const Result& r) { return r.years > 0 && r.yearsCap / r.years <= 0.15; }, 1.0, capPct);
		fedShare("review 9: answered rain / bloom prayers end the famine within 5 years (>= 80 %)");
	}
	if (pk == PolicyKind::LateKind) yes("review 1b: late-kind is not beloved on >= 4/6", [](const Result& r) { return !(r.ending && r.ending->mood == Mood::Beloved); }, 4.0 / 6, moodS);
	if (pk == PolicyKind::LateCruel) yes("review 1c: late-cruel is not feared on >= 4/6", [](const Result& r) { return !(r.ending && r.ending->mood == Mood::Feared); }, 4.0 / 6, moodS);
	if (pk == PolicyKind::AtomicKind) yes("verifier 1: atomic-kind (idle, then kind from Atomic) is not beloved on >= 4/6", [](const Result& r) { return !(r.ending && r.ending->mood == Mood::Beloved); }, 4.0 / 6, moodS);
	if (pk == PolicyKind::Sculptor) yes("review 10: sculptor never loses its people (people:lost 0)", [](const Result& r) { return r.lostCount == 0; }, 1.0, [](const Result& r) { return fmt("%d", r.lostCount); });
	if (pk == PolicyKind::Doom) {
		yes("review 10: doom reaches silence, after year 1000", [](const Result& r) { return r.ending && r.ending->type == EndingType::Silence && r.ending->year >= 1000; }, 1.0,
			[](const Result& r) { return r.ending ? fmt("%s %.0f", toStr(r.ending->type), r.ending->year) : std::string("none"); });
		yes("review 7: doom (Lower Land) still earns a Drowner epithet on >= 5/6", [](const Result& r) { return r.drowner; }, 5.0 / 6, [](const Result& r) { return r.epithet; });
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

// The design review's targets that compare policies seed by seed (only those whose policies ran).
std::string crossTargets(const std::vector<Result>& all, bool& allPass) {
	auto runOf = [&](PolicyKind pk, uint32_t seed) -> const Result* {
		for (const Result& r : all) if (r.policy == pk && r.seed == seed) return &r;
		return nullptr;
	};
	std::vector<uint32_t> seeds;
	for (const Result& r : all) if (std::find(seeds.begin(), seeds.end(), r.seed) == seeds.end()) seeds.push_back(r.seed);
	std::sort(seeds.begin(), seeds.end());
	std::vector<Target> ts;
	using Pair = std::function<bool(const Result&, const Result&, std::string&)>;
	auto pairwise = [&](const std::string& what, PolicyKind a, PolicyKind b, double share, const Pair& pred) {
		Target T;
		T.what = what;
		T.minShare = share;
		for (uint32_t s : seeds) {
			const Result* ra = runOf(a, s);
			const Result* rb = runOf(b, s);
			if (!ra || !rb) continue;
			std::string d;
			T.n++;
			if (pred(*ra, *rb, d)) T.pass++;
			T.detail += (T.detail.empty() ? "" : " ") + d;
		}
		if (T.n > 0) ts.push_back(T);
	};
	auto moodOf = [](const Result& r) { return r.ending ? std::string(toStr(r.ending->mood)) : std::string("none"); };
	pairwise("review 1a: blessonly and passive end in different moods on >= 5/6", PolicyKind::BlessOnly, PolicyKind::Passive, 5.0 / 6,
		[&](const Result& a, const Result& b, std::string& d) { d = moodOf(a) + "/" + moodOf(b); return moodOf(a) != moodOf(b); });
	auto arkGap = [](const Result& a, const Result& b, std::string& d) {
		const double ya = a.arkYear >= 0 ? a.arkYear : 1e9, yb = b.arkYear >= 0 ? b.arkYear : 1e9;
		d = (ya < 1e8 && yb < 1e8) ? fmt("%+.0f", ya - yb) : std::string("-");
		return ya - yb;
	};
	pairwise("review 3: kind launches the Ark >= 150 years before passive on >= 5/6", PolicyKind::Kind, PolicyKind::Passive, 5.0 / 6,
		[&](const Result& a, const Result& b, std::string& d) { return arkGap(a, b, d) <= -150; });
	pairwise("review 3: cruel launches the Ark >= 150 years after passive on >= 5/6", PolicyKind::Cruel, PolicyKind::Passive, 5.0 / 6,
		[&](const Result& a, const Result& b, std::string& d) { const double g = arkGap(a, b, d); return g >= 150 && g < 1e8; });
	pairwise("review 3: inspire-spam reaches Space >= 200 years before passive on >= 5/6", PolicyKind::InspireSpam, PolicyKind::Passive, 5.0 / 6,
		[&](const Result& a, const Result& b, std::string& d) {
			const double ya = a.ageYear[6] >= 0 ? a.ageYear[6] : 1e9, yb = b.ageYear[6] >= 0 ? b.ageYear[6] : 1e9;
			d = (ya < 1e8 && yb < 1e8) ? fmt("%+.0f", ya - yb) : std::string("-");
			return ya - yb <= -200;
		});
	pairwise("review 9: kind has no more famine entries than passive on >= 5/6", PolicyKind::Kind, PolicyKind::Passive, 5.0 / 6,
		[&](const Result& a, const Result& b, std::string& d) { d = fmt("%d/%d", a.famineChron, b.famineChron); return a.famineChron <= b.famineChron; });
	auto warShare = [](const Result& r) { return r.years > 0 ? 100 * r.warYears / r.years : 0.0; };
	{   // informational: the war share of a cruel god's world against the passive one, seed by seed
		Target I;
		I.what = "review 11 (informational): cruel / passive war % by seed";
		I.need = false;
		for (uint32_t s : seeds) {
			const Result* ra = runOf(PolicyKind::Cruel, s);
			const Result* rb = runOf(PolicyKind::Passive, s);
			if (ra && rb) I.detail += (I.detail.empty() ? "" : " ") + fmt("%.0f/%.0f", warShare(*ra), warShare(*rb));
		}
		if (!I.detail.empty()) ts.push_back(I);
	}
	pairwise("review 11: the Omen of Peace: kind's peoples are at war less than cruel's on >= 5/6", PolicyKind::Kind, PolicyKind::Cruel, 5.0 / 6,
		[&](const Result& a, const Result& b, std::string& d) { d = fmt("%.0f/%.0f", warShare(a), warShare(b)); return warShare(a) < warShare(b); });
	pairwise("review 6: hoarder ends with lower devotion than builder on >= 5/6", PolicyKind::Hoarder, PolicyKind::Builder, 5.0 / 6,
		[&](const Result& a, const Result& b, std::string& d) { d = fmt("%.2f/%.2f", a.endDevotion, b.endDevotion); return a.endDevotion < b.endDevotion; });
	{   // each of the four Ark moods is the most common ending of at least one policy
		std::map<int, std::string> top;
		std::vector<PolicyKind> pks;
		for (const Result& r : all) if (std::find(pks.begin(), pks.end(), r.policy) == pks.end()) pks.push_back(r.policy);
		if (pks.size() >= 8) {
			Target T;
			T.what = "review 2: each of the four Ark moods is the most common ending of at least one policy";
			std::array<bool, 4> seen{};
			for (PolicyKind pk : pks) {
				std::array<int, 6> cnt{};
				for (const Result& r : all) if (r.policy == pk && r.ending && r.ending->type == EndingType::Ark) cnt[size_t(r.ending->mood)]++;
				int best = -1;
				for (int m = 0; m < 4; m++) if (cnt[size_t(m)] > 0 && (best < 0 || cnt[size_t(m)] > cnt[size_t(best)])) best = m;
				if (best >= 0) seen[size_t(best)] = true;
				T.detail += (T.detail.empty() ? "" : " ") + fmt("%s:%s", simrun::toStr(pk), best >= 0 ? toStr(Mood(best)) : "-");
			}
			T.n = 4;
			for (bool b : seen) T.pass += b ? 1 : 0;
			ts.push_back(T);
		}
	}
	std::string out = "cross-policy targets (design review):\n";
	for (const Target& T : ts) {
		const bool ok = !T.need || (T.n > 0 && double(T.pass) >= T.minShare * T.n - 1e-9);
		if (!ok) allPass = false;
		out += fmt("  %-5s %-62s %d/%d  [%s]\n", T.need ? (ok ? "PASS" : "FAIL") : "info", T.what.c_str(), T.pass, T.n, T.detail.c_str());
	}
	return ts.empty() ? std::string() : out;
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
			std::printf("\n%s", reviewTable(results).c_str());
			if (args.summary) {
				for (PolicyKind pk : args.policies) {
					std::vector<Result> sub;
					for (const Result& r : results) if (r.policy == pk) sub.push_back(r);
					std::printf("\n%s", targets(sub, pk, allPass).c_str());
				}
				const std::string cross = crossTargets(results, allPass);
				if (!cross.empty()) std::printf("\n%s", cross.c_str());
				std::printf("\n%s", pacingTables(results, args.policies).c_str());
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
