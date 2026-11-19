// DEMIURGE — SimTests/TestMain.cpp : runs every registered DM_TEST.
//   <exe> [substring ...]   run only tests whose name contains one of the substrings
// Environment: DM_ROOT (project root; set by run.ps1), DM_VERBOSE=1 (print every test with its time).
// Last line of output: "RESULT <passed> <failed>" (run.ps1 turns it into the summary line).
#include "TestFramework.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace dmt {

namespace {
struct Current {
	const char* name = "";
	int failures = 0;
};
Current& cur() { static Current c; return c; }
} // namespace

std::vector<TestCase>& registry() {
	static std::vector<TestCase> r;
	return r;
}

void recordFailure(std::string_view msg, const std::source_location& loc) {
	Current& c = cur();
	c.failures++;
	if (c.failures <= 12) {
		std::string file = loc.file_name();
		const size_t slash = file.find_last_of("/\\");
		if (slash != std::string::npos) file = file.substr(slash + 1);
		std::cout << "  FAIL " << c.name << "\n       " << file << ":" << loc.line() << ": " << msg << "\n";
	} else if (c.failures == 13) {
		std::cout << "       (further failures of this test not shown)\n";
	}
}

bool verbose() {
	const char* v = std::getenv("DM_VERBOSE");
	return v && *v && *v != '0';
}

std::string projectRoot() {
	const char* r = std::getenv("DM_ROOT");
	if (r && *r) return std::string(r);
	// fallback: this file lives in <root>/Unreal/SimTests/
	std::filesystem::path p(__FILE__);
	return p.parent_path().parent_path().parent_path().string();
}

std::string shot(std::string_view fileName) {
	std::filesystem::path p = std::filesystem::path(projectRoot()) / "shots";
	std::error_code ec;
	std::filesystem::create_directories(p, ec);
	return (p / std::string(fileName)).string();
}

double nowMs() {
	using namespace std::chrono;
	return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

} // namespace dmt

int main(int argc, char** argv) {
	using namespace dmt;
	std::vector<std::string> filters;
	for (int i = 1; i < argc; i++) filters.emplace_back(argv[i]);
	int passed = 0, failed = 0;
	const double t0 = nowMs();
	for (const TestCase& t : registry()) {
		if (!filters.empty()) {
			bool hit = false;
			for (const auto& f : filters) if (std::string_view(t.name).find(f) != std::string_view::npos) hit = true;
			if (!hit) continue;
		}
		cur().name = t.name;
		cur().failures = 0;
		const double s = nowMs();
		t.fn();
		const double ms = nowMs() - s;
		if (cur().failures == 0) {
			passed++;
			if (verbose()) std::printf("  ok   %s (%.1f ms)\n", t.name, ms);
		} else {
			failed++;
		}
		std::fflush(stdout);
	}
	std::printf("\n%d passed, %d failed (%.0f ms)\nRESULT %d %d\n", passed, failed, nowMs() - t0, passed, failed);
	return failed ? 1 : 0;
}
