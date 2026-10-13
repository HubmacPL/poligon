#pragma once

#include <cstdio>

// Minimal test helpers. A test is a program: call Expect for every check and
// return test::ExitCode() from main.
namespace test {

inline int failure_count = 0;

inline void Expect(bool condition, const char* description) {
    if (!condition) {
        std::printf("FAIL: %s\n", description);
        ++failure_count;
    }
}

inline int ExitCode() {
    if (failure_count != 0) {
        std::printf("%d check(s) failed\n", failure_count);
        return 1;
    }
    return 0;
}

} // namespace test
