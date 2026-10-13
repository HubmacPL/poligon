// Crashes on purpose, in the way named by the argument, so that tests can check the crash report.
// Usage: crash_test MODE REPORT_PATH
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "poligon/crash_handler.h"
#include "poligon/fatal_error.h"

// The functions below must stay real, separate functions, so that their names show up in the stack.
#define NOINLINE __declspec(noinline)

namespace {

volatile int g_zero = 0;

NOINLINE void CrashWithNullWrite() {
    volatile int* pointer = nullptr;
    // The static analyzer is right that this writes through a null pointer; that is the test.
#pragma warning(suppress : 6011)
    *pointer = 42;
}

NOINLINE void CrashWithAbort() {
    std::abort();
}

NOINLINE void CrashWithFatalError() {
    poligon::FatalError("deliberate fatal error");
}

NOINLINE void CrashWithException() {
    throw std::runtime_error("deliberate exception");
}

NOINLINE int CrashWithDivision() {
    return 100 / g_zero;
}

NOINLINE int CrashWithStackOverflow(int depth) {
    volatile char padding[4096];
    padding[0] = static_cast<char>(depth);
    // Never true at run time; it only stops the compiler from rejecting obviously endless recursion.
    if (g_zero != 0) {
        return 0;
    }
    // The result is used after the call, so the compiler cannot turn the recursion into a loop.
    return CrashWithStackOverflow(depth + 1) + padding[0];
}

NOINLINE int CrashWithVectorIndex() {
    std::vector<int> values(3);
    volatile size_t index = 10;
    return values[index];
}

struct Base {
    Base() { CallIt(); }
    virtual ~Base() = default;
    virtual void Run() = 0;
    NOINLINE void CallIt() { Run(); }
};

struct Derived : Base {
    void Run() override {}
};

NOINLINE void CrashWithPureCall() {
    Derived object;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: crash_test MODE REPORT_PATH\n");
        return 2;
    }
    poligon::CrashHandlerConfig config;
    config.report_path = argv[2];
    poligon::InstallCrashHandler(config);

    const char* mode = argv[1];
    std::printf("about to crash: %s\n", mode);
    if (std::strcmp(mode, "null_write") == 0) {
        CrashWithNullWrite();
    } else if (std::strcmp(mode, "abort") == 0) {
        CrashWithAbort();
    } else if (std::strcmp(mode, "fatal_error") == 0) {
        CrashWithFatalError();
    } else if (std::strcmp(mode, "exception") == 0) {
        CrashWithException();
    } else if (std::strcmp(mode, "division") == 0) {
        return CrashWithDivision();
    } else if (std::strcmp(mode, "stack_overflow") == 0) {
        return CrashWithStackOverflow(0);
    } else if (std::strcmp(mode, "vector_index") == 0) {
        return CrashWithVectorIndex();
    } else if (std::strcmp(mode, "pure_call") == 0) {
        CrashWithPureCall();
    } else {
        std::fprintf(stderr, "unknown mode\n");
        return 2;
    }
    std::printf("survived, which is wrong\n");
    return 0;
}
