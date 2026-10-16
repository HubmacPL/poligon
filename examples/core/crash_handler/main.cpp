// Example: install the crash handler. Run with the argument "crash" to see a report.
#include <cstdio>
#include <cstring>

#include "poligon/core/crash_handler.h"
#include "poligon/core/fatal_error.h"

int main(int argc, char** argv) {
    // First line of main. Without arguments the report file is "<program>_crash.txt" next to the executable.
    poligon::InstallCrashHandler();

    if (argc > 1 && std::strcmp(argv[1], "crash") == 0) {
        // Prints "FATAL: ..." with the call stack, saves the report and exits with code 3.
        poligon::FatalError("example of a fatal error");
    }

    std::printf("crash handler installed; run \"example_crash_handler crash\" to see a report\n");
    return 0;
}
