#pragma once

// The smallest test helper that prints where a check failed and what the two values were.
// Each test_*.cpp has its own main() and ends with `return check::done("name");`.

#include <cstdio>

namespace check {

inline int g_checks = 0;
inline int g_failures = 0;

inline void that(bool ok, const char *text, const char *file, int line) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("%s:%d: CHECK(%s) failed\n", file, line, text);
}

template <typename A, typename B>
inline void equal(const A &a, const B &b, const char *text, const char *file, int line) {
    ++g_checks;
    if (a == b) return;
    ++g_failures;
    std::printf("%s:%d: CHECK_EQ(%s) failed: 0x%llX != 0x%llX\n", file, line, text,
                static_cast<unsigned long long>(a), static_cast<unsigned long long>(b));
}

inline int done(const char *name) {
    std::printf("%s: %d checks, %d failed\n", name, g_checks, g_failures);
    return g_failures ? 1 : 0;
}

}

#define CHECK(condition) check::that(static_cast<bool>(condition), #condition, __FILE__, __LINE__)
#define CHECK_EQ(a, b) check::equal((a), (b), #a ", " #b, __FILE__, __LINE__)
