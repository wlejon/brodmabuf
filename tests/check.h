// Minimal test harness: checks are real code paths in every configuration
// (no assert()), failures are counted and reported, and main() returns
// nonzero when anything failed. skip() exits 77 (ctest SKIP_RETURN_CODE)
// with the reason; skip_check() records a check that could not run here and
// why, without ending the test. Both print a line containing "SKIP", which
// .github/ci/ctest.sh lists under each job's results.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <type_traits>

namespace bstest {

inline int& failures() {
    static int n = 0;
    return n;
}

inline void fail(const char* file, int line, const std::string& what) {
    ++failures();
    std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, what.c_str());
    std::fflush(stderr);
}

template <class T>
auto printable(const T& v) {
    if constexpr (std::is_enum_v<T>) {
        return static_cast<long long>(v);
    } else if constexpr (std::is_pointer_v<T>) {
        return static_cast<const void*>(v);
    } else {
        return v;
    }
}

template <class A, class B>
std::string describe(const char* ea, const char* eb, const A& a, const B& b) {
    std::ostringstream s;
    s << ea << " == " << eb << " (got '" << printable(a) << "' vs '" << printable(b) << "')";
    return s.str();
}

inline int finish(const char* name) {
    if (failures() == 0) {
        std::printf("[%s] PASSED\n", name);
        std::fflush(stdout);
        return 0;
    }
    std::printf("[%s] FAILED (%d check%s)\n", name, failures(), failures() == 1 ? "" : "s");
    std::fflush(stdout);
    return 1;
}

[[noreturn]] inline void skip(const char* name, const std::string& why) {
    std::printf("[%s] SKIP: %s\n", name, why.c_str());
    std::fflush(stdout);
    std::exit(failures() == 0 ? 77 : 1);
}

inline void skip_check(const char* what, const std::string& why) {
    std::printf("SKIP check %s: %s\n", what, why.c_str());
    std::fflush(stdout);
}

inline std::string env(const char* var) {
#if defined(_MSC_VER)
    char* v = nullptr;
    size_t n = 0;
    std::string out;
    if (_dupenv_s(&v, &n, var) == 0 && v) out = v;
    std::free(v);
    return out;
#else
    const char* v = std::getenv(var);
    return v ? std::string(v) : std::string();
#endif
}

}  // namespace bstest

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) ::bstest::fail(__FILE__, __LINE__, #cond);        \
    } while (0)

#define CHECK_EQ(a, b)                                                                 \
    do {                                                                               \
        auto check_a_ = (a);                                                           \
        auto check_b_ = (b);                                                           \
        if (!(check_a_ == check_b_))                                                   \
            ::bstest::fail(__FILE__, __LINE__,                                         \
                           ::bstest::describe(#a, #b, check_a_, check_b_));            \
    } while (0)

// Fatal variant: stops the current (void) test function.
#define REQUIRE(cond)                                                  \
    do {                                                               \
        if (!(cond)) {                                                 \
            ::bstest::fail(__FILE__, __LINE__, "required: " #cond);    \
            return;                                                    \
        }                                                              \
    } while (0)

// REQUIRE for a Result<T>: on failure the status message is part of the report.
// The expression is evaluated once.
#define REQUIRE_OK(res)                                                                  \
    do {                                                                                 \
        auto&& check_r_ = (res);                                                         \
        if (!check_r_.ok()) {                                                            \
            ::bstest::fail(__FILE__, __LINE__,                                           \
                           std::string("required ok: " #res " -> ") +                    \
                               std::string(check_r_.error_message()));                   \
            return;                                                                      \
        }                                                                                \
    } while (0)

#define CHECK_OK(res)                                                                    \
    do {                                                                                 \
        auto&& check_r_ = (res);                                                         \
        if (!check_r_.ok())                                                              \
            ::bstest::fail(__FILE__, __LINE__,                                           \
                           std::string("expected ok: " #res " -> ") +                    \
                               std::string(check_r_.error_message()));                   \
    } while (0)
