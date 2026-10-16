// Example: parse the standard Poligon command line options.
// Try: example_command_line --headless --ticks 3
#include <cstdio>
#include <string>

#include "poligon/core/command_line.h"

int main(int argc, char** argv) {
    poligon::CommandLineOptions options;
    std::string error;
    if (!poligon::ParseCommandLine(argc, argv, options, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 2;
    }

    std::printf("headless=%s ticks=%lld\n", options.headless ? "true" : "false",
                static_cast<long long>(options.ticks));
    return 0;
}
