// Tiny self-contained test framework (no external dependency).
#pragma once
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace mt {
struct Case { const char* name; std::function<void()> fn; };
std::vector<Case>& cases();
struct Reg { Reg(const char* n, std::function<void()> f) { cases().push_back({n, std::move(f)}); } };
struct Failure { std::string msg; };
struct Skip { std::string why; };
// The Monomachine OS file for the tests that run the emulated DSP: $MNM_OS, else the MNM_OS_SYX CMake option.
// Empty when neither names an existing file.
inline std::string osFile() {
    const char* env = std::getenv("MNM_OS");
    const std::string p = (env && *env) ? env : MNM_OS_SYX;
    return !p.empty() && std::filesystem::exists(p) ? p : std::string();
}
// Portable setenv/unsetenv (value nullptr = unset).
inline void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1); else unsetenv(name);
#endif
}
// For a test that needs the OS file: skips the test when there is none.
inline std::string requireOsFile() {
    auto p = osFile();
    if (p.empty()) throw Skip{"no Monomachine OS file (set MNM_OS or -DMNM_OS_SYX=...)"};
    return p;
}
inline void fail(const char* file, int line, const std::string& what) {
    std::ostringstream o; o << file << ":" << line << ": " << what; throw Failure{o.str()};
}
}
#define TEST_CASE(name) static void name##_fn(); static mt::Reg name##_reg(#name, name##_fn); static void name##_fn()
#define CHECK(cond) do { if (!(cond)) mt::fail(__FILE__, __LINE__, "CHECK failed: " #cond); } while (0)
#define CHECK_EQ(a, b) do { auto _a = (a); auto _b = (b); if (!(_a == _b)) { std::ostringstream _o; _o << "CHECK_EQ failed: " #a " == " #b "  (" << _a << " vs " << _b << ")"; mt::fail(__FILE__, __LINE__, _o.str()); } } while (0)
#define CHECK_MSG(cond, msg) do { if (!(cond)) { std::ostringstream _o; _o << "CHECK failed: " #cond " — " << msg; mt::fail(__FILE__, __LINE__, _o.str()); } } while (0)
