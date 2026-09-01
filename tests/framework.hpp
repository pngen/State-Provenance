#pragma once
// ---------------------------------------------------------------------------
// State Provenance - minimal, dependency-free, infinitely-slow-test-friendly
// test framework.  NO timeouts, NO watchdog, NO process-limit mechanisms.
// Tests run to completion; the runner reports a deterministic pass/fail count.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace sp_test {

struct TestCase { std::string name; std::function<void()> fn; };

inline std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }
inline std::vector<std::string>& failures_log() { static std::vector<std::string> v; return v; }

struct Registrar {
    Registrar(const std::string& n, std::function<void()> f) { registry().push_back({n, f}); }
};

struct TestAbort : std::runtime_error { using std::runtime_error::runtime_error; };

inline void check_true(bool cond, const std::string& msg, const char* file, int line) {
    ++checks();
    if (!cond) {
        ++failures();
        std::string m = std::string(file) + ":" + std::to_string(line) + " " + msg;
        failures_log().push_back(m);
        std::printf("  FAIL  %s\n", m.c_str());
    }
}

// Deterministic splitmix64 RNG for property tests (fixed seed, printed).
struct Rng {
    std::uint64_t state;
    explicit Rng(std::uint64_t seed) : state(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
    std::uint64_t below(std::uint64_t n) { return n ? next() % n : 0; }
    bool coin() { return (next() & 1u) != 0; }
};

} // namespace sp_test

#define TEST_CASE(name) \
    static void sp_test_##name(); \
    static ::sp_test::Registrar sp_test_reg_##name(#name, sp_test_##name); \
    static void sp_test_##name()

#define CHECK(cond) do { ::sp_test::check_true((cond), "CHECK(" #cond ")", __FILE__, __LINE__); } while(0)
#define CHECK_EQ(a, b) do { auto _a = (a); auto _b = (b); ::sp_test::check_true((_a == _b), "CHECK_EQ(" #a ", " #b ")", __FILE__, __LINE__); } while(0)
#define CHECK_NE(a, b) do { auto _a = (a); auto _b = (b); ::sp_test::check_true((_a != _b), "CHECK_NE(" #a ", " #b ")", __FILE__, __LINE__); } while(0)
#define CHECK_MSG(cond, msg) do { ::sp_test::check_true((cond), (msg), __FILE__, __LINE__); } while(0)
#define CHECK_THROWS(expr) do { bool _th = false; try { (void)(expr); } catch (...) { _th = true; } ::sp_test::check_true(_th, "CHECK_THROWS(" #expr ")", __FILE__, __LINE__); } while(0)
#define CHECK_NOTHROW(expr) do { bool _th = false; try { (void)(expr); } catch (...) { _th = true; } ::sp_test::check_true(!_th, "CHECK_NOTHROW(" #expr ")", __FILE__, __LINE__); } while(0)

// Run all registered tests; returns number of failures.
inline int run_all_tests() {
    std::printf("State Provenance test suite\n");
    std::printf("  %zu test case(s) registered\n", sp_test::registry().size());
    std::printf("  seed = 0x1234abcd1234abcd (deterministic, fixed)\n");
    int ran = 0;
    for (const auto& tc : sp_test::registry()) {
        std::printf("  [run] %s\n", tc.name.c_str());
        std::fflush(stdout);
        int before_f = sp_test::failures();
        try { tc.fn(); }
        catch (const std::exception& e) {
            ++sp_test::failures();
            std::printf("  FAIL  %s threw: %s\n", tc.name.c_str(), e.what());
        } catch (...) {
            ++sp_test::failures();
            std::printf("  FAIL  %s threw unknown\n", tc.name.c_str());
        }
        if (sp_test::failures() > before_f) {
            // keep running; track that this case had failures
        }
        ++ran;
    }
    std::printf("\n%s: %d checks, %d failures, %d matches\n",
                sp_test::failures() ? "TEST SUITE FAILED" : "TEST SUITE PASSED",
                sp_test::checks(), sp_test::failures(), ran);
    std::printf("seed = 0x1234abcd1234abcd\n");
    return sp_test::failures();
}
