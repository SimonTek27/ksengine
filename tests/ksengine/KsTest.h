#pragma once
// Minimal assertion harness for the Qt-free tests. Deliberately not gtest:
// tests/ already pulls Qt6 Test, and the whole point of these targets is that
// they build and run with nothing but MSVC + CMake.
#include <cmath>
#include <cstdio>

namespace ks::test {

inline int& failures() {
    static int f = 0;
    return f;
}

inline void report(bool ok, const char* expr, const char* file, int line) {
    if (ok) return;
    std::printf("FAIL %s:%d  %s\n", file, line, expr);
    ++failures();
}

inline int finish(const char* name) {
    if (failures() == 0) {
        std::printf("%s: OK\n", name);
        return 0;
    }
    std::printf("%s: %d FAILURES\n", name, failures());
    return 1;
}

} // namespace ks::test

#define KS_CHECK(expr) ::ks::test::report(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define KS_CHECK_NEAR(a, b, eps)                                                                  \
    ::ks::test::report(std::fabs(static_cast<double>(a) - static_cast<double>(b)) <= (eps),       \
                       #a " ~= " #b, __FILE__, __LINE__)
#define KS_TEST_RESULT(name) ::ks::test::finish(name)
