#include <string>
#include <vector>

#include "poligon/core/command_line.h"
#include "test_support.h"

namespace {

bool Parse(int argc, const char* const* argv, poligon::CommandLineOptions& options, std::string& error) {
    return poligon::ParseCommandLine(argc, argv, options, error);
}

} // namespace

int main() {
    using test::Expect;

    poligon::CommandLineOptions options;
    std::string error;

    {
        const char* argv[] = {"program"};
        Expect(Parse(1, argv, options, error), "no options is valid");
        Expect(!options.headless && options.ticks == 0, "defaults are windowed and unlimited");
    }
    {
        const char* argv[] = {"program", "--headless", "--ticks", "600"};
        Expect(Parse(4, argv, options, error), "--headless --ticks 600 is valid");
        Expect(options.headless && options.ticks == 600, "both values are read");
    }
    {
        const char* argv[] = {"program", "--headles"};
        Expect(!Parse(2, argv, options, error), "a mistyped option is an error");
        Expect(error.find("--headles") != std::string::npos, "the error names the bad option");
    }
    {
        const char* argv[] = {"program", "--ticks"};
        Expect(!Parse(2, argv, options, error), "--ticks without a value is an error");
    }
    {
        const char* argv[] = {"program", "--ticks", "12abc"};
        Expect(!Parse(3, argv, options, error), "trailing garbage in a number is an error");
    }
    {
        const char* argv[] = {"program", "--ticks", "0"};
        Expect(!Parse(3, argv, options, error), "--ticks 0 is an error");
    }
    {
        const char* argv[] = {"program", "--ticks", "-3"};
        Expect(!Parse(3, argv, options, error), "a negative tick count is an error");
    }

    {
        const char* argv[] = {"program", "--backend", "vulkan", "--gpu-validation", "--dump-frame", "out.png"};
        Expect(Parse(6, argv, options, error), "graphics options are valid");
        Expect(options.backend == poligon::GraphicsBackend::Vulkan, "--backend vulkan is read");
        Expect(options.gpu_validation, "--gpu-validation is read");
        Expect(options.dump_frame_path == "out.png", "--dump-frame path is read");
    }
    {
        const char* argv[] = {"program", "--backend", "opengl"};
        Expect(!Parse(3, argv, options, error), "an unknown backend is an error");
    }
    {
        const char* argv[] = {"program", "--dump-frame"};
        Expect(!Parse(2, argv, options, error), "--dump-frame without a path is an error");
    }
    {
        const char* argv[] = {"program", "--dump-state", "state.json"};
        Expect(Parse(3, argv, options, error) && options.dump_state_path == "state.json",
               "--dump-state path is read");
    }

    {
        const char* argv[] = {"program", "--record", "out.txt", "--replay", "in.txt"};
        Expect(Parse(5, argv, options, error) && options.record_path == "out.txt" && options.replay_path == "in.txt",
               "--record and --replay paths are read");
    }

    {
        const char* argv[] = {"program", "--hash-log", "run.hashes", "--hash-interval", "5"};
        Expect(Parse(5, argv, options, error) && options.hash_log_path == "run.hashes" && options.hash_interval == 5,
               "--hash-log and --hash-interval are read");
    }
    {
        const char* argv[] = {"program", "--render-settings", "look.txt"};
        Expect(Parse(3, argv, options, error) && options.render_settings_path == "look.txt", "--render-settings is read");
    }
    {
        const char* argv[] = {"program", "--hash-interval", "0"};
        Expect(!Parse(3, argv, options, error), "--hash-interval 0 is an error");
    }

    {
        const char* argv[] = {"program", "--quality", "medium", "--resolution", "1920x1080"};
        Expect(Parse(5, argv, options, error) && options.quality == "medium" && options.width == 1920 &&
                   options.height == 1080,
               "--quality and --resolution are read");
    }
    {
        const char* argv[] = {"program", "--quality", "extreme"};
        Expect(!Parse(3, argv, options, error) && error.find("extreme") != std::string::npos,
               "an unknown preset is an error that names it");
    }
    for (const char* bad : {"1920", "1920x", "x1080", "1920x1080x", "8x8", "1920*1080"}) {
        const char* argv[] = {"program", "--resolution", bad};
        Expect(!Parse(3, argv, options, error), "a malformed or too small resolution is an error");
    }

    // A program's own options.
    const std::vector<poligon::CommandLineOption> own = {{"data", true}, {"collision", false}};
    {
        const char* argv[] = {"program", "--data", "C:/game data", "--headless", "--collision", "level1"};
        Expect(poligon::ParseCommandLine(6, argv, options, error, own), "a program's own options are accepted");
        Expect(options.extra.size() == 2 && options.extra["data"] == "C:/game data" && options.extra["collision"].empty(),
               "their values are read, an option without a value gives an empty one");
        Expect(options.headless && options.positional.size() == 1 && options.positional[0] == "level1",
               "standard options and positional arguments still work between them");
    }
    {
        const char* argv[] = {"program", "--data"};
        Expect(!poligon::ParseCommandLine(2, argv, options, error, own) && error.find("--data") != std::string::npos,
               "a program's own option without its value is an error");
    }
    {
        const char* argv[] = {"program", "--dat", "x"};
        Expect(!poligon::ParseCommandLine(3, argv, options, error, own), "a mistyped own option is still an error");
    }
    {
        const char* argv[] = {"program", "--data", "x"};
        Expect(!Parse(3, argv, options, error), "without the list, a program's own option is unknown");
        const std::vector<poligon::CommandLineOption> clash = {{"ticks", true}};
        Expect(!poligon::ParseCommandLine(1, argv, options, error, clash) && error.find("--ticks") != std::string::npos,
               "a program cannot add a standard option again");
    }

    return test::ExitCode();
}
