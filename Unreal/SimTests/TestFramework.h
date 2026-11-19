// DEMIURGE — SimTests/TestFramework.h : tiny test framework for the standalone sim build.
//
//   #include "TestFramework.h"
//   using namespace dmt;
//   DM_TEST("rng: pinned streams") {
//       ok(cond, "message");                 // each returns bool, so `if (!ok(...)) return;` works
//       eq(a, b, "message");                 // ==, values printed on failure
//       near(a, b, 1e-9, "message");         // |a - b| <= eps (NaN never passes)
//       noNaN(*w, "after 100 ticks");        // every float cell array and world scalar is finite
//       fail("message");
//   }
// Every failing check prints file:line, the test name and the message; a test fails if any check
// failed or if an event emitted on a watched world broke the event contract (watch(), automatic
// for worlds made by makeWorld). Only the first 12 failures of a test are printed.
// Test code may use anything (exceptions, statics, iostreams); the rules for Sim/ do not apply.
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace dm { struct World; }

namespace dmt {

struct TestCase {
	const char* name;
	void (*fn)();
	const char* file;
};
std::vector<TestCase>& registry();
struct Registrar {
	Registrar(const char* name, void (*fn)(), const char* file) { registry().push_back({ name, fn, file }); }
};

#define DM_TEST_CAT2(a, b) a##b
#define DM_TEST_CAT(a, b) DM_TEST_CAT2(a, b)
#define DM_TEST(name)                                                                                     \
	static void DM_TEST_CAT(dmt_test_fn_, __LINE__)();                                                    \
	static ::dmt::Registrar DM_TEST_CAT(dmt_test_reg_, __LINE__)(name, &DM_TEST_CAT(dmt_test_fn_, __LINE__), __FILE__); \
	static void DM_TEST_CAT(dmt_test_fn_, __LINE__)()

// Record a failure for the running test.
void recordFailure(std::string_view msg, const std::source_location& loc);
bool verbose();
// Absolute path for an output file under the project root, e.g. shot("ue-core-elev.png") ->
// "<root>/shots/ue-core-elev.png" (directory created). The root comes from the DM_ROOT
// environment variable (run.ps1 sets it).
std::string shot(std::string_view fileName);
std::string projectRoot();
// Milliseconds since an arbitrary start (for reporting timings, never for asserting tight budgets).
double nowMs();

template <class T>
std::string show(const T& v) {
	if constexpr (std::is_same_v<T, bool>) return v ? "true" : "false";
	else if constexpr (std::is_enum_v<T>) return std::to_string(static_cast<long long>(v));
	else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view> || std::is_same_v<T, const char*>) return "\"" + std::string(v) + "\"";
	else if constexpr (requires(std::ostream& os, const T& x) { os << x; }) {
		std::ostringstream os;
		if constexpr (std::is_floating_point_v<T>) os.precision(17);
		os << v;
		return os.str();
	} else if constexpr (requires(const T& x) { x.begin(); x.end(); x.size(); }) {
		std::string s = "[";
		size_t k = 0;
		for (const auto& e : v) {
			if (k) s += ", ";
			if (++k > 24) { s += "..."; break; }
			s += show(e);
		}
		return s + "] (" + std::to_string(v.size()) + ")";
	} else return "<value>";
}

inline bool ok(bool cond, std::string_view msg = {}, std::source_location loc = std::source_location::current()) {
	if (!cond) recordFailure(msg.empty() ? std::string_view("expected true") : msg, loc);
	return cond;
}
inline bool fail(std::string_view msg, std::source_location loc = std::source_location::current()) {
	recordFailure(msg, loc);
	return false;
}
template <class A, class B>
bool eq(const A& a, const B& b, std::string_view msg = {}, std::source_location loc = std::source_location::current()) {
	bool same;
	if constexpr (std::is_arithmetic_v<A> && std::is_arithmetic_v<B> && !std::is_same_v<A, bool> && !std::is_same_v<B, bool>) {
		if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) same = std::cmp_equal(a, b);
		else same = (a == b);
	} else {
		same = (a == b);
	}
	if (!same) recordFailure(std::string(msg) + (msg.empty() ? "" : ": ") + "expected " + show(b) + ", got " + show(a), loc);
	return same;
}
inline bool near(double a, double b, double eps = 1e-6, std::string_view msg = {}, std::source_location loc = std::source_location::current()) {
	const bool good = std::fabs(a - b) <= eps;
	if (!good) {
		std::ostringstream os;
		os.precision(17);
		os << msg << (msg.empty() ? "" : ": ") << "expected " << b << " +- " << eps << ", got " << a;
		recordFailure(os.str(), loc);
	}
	return good;
}
// Every float cell array and the world scalars are finite (see checkFinite in TestWorld.h).
bool noNaN(const dm::World& w, std::string_view msg = {}, std::source_location loc = std::source_location::current());

} // namespace dmt
