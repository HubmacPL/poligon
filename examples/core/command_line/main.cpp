// Example: parse the standard Poligon command line options, and two of the program's own.
// Try: example_command_line --headless --ticks 3 --data C:/game --collision
#include <cstdio>
#include <string>
#include <vector>

#include "poligon/core/command_line.h"

int main(int argc, char** argv) {
    // The program's own options: an unknown option stays an error, so these are declared.
    const std::vector<poligon::CommandLineOption> own_options = {
        {"data", true},        // --data FOLDER
        {"collision", false},  // --collision
    };
    poligon::CommandLineOptions options;
    std::string error;
    if (!poligon::ParseCommandLine(argc, argv, options, error, own_options)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 2;
    }

    std::printf("headless=%s ticks=%lld\n", options.headless ? "true" : "false",
                static_cast<long long>(options.ticks));
    const auto data = options.extra.find("data");
    std::printf("data=%s collision=%s\n", data != options.extra.end() ? data->second.c_str() : "(not given)",
                options.extra.count("collision") != 0 ? "true" : "false");
    return 0;
}
