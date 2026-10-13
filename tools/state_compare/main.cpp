// Checks that two runs of a game were identical.
//
// Usage: state_compare A.json B.json [--hash-logs A.log B.log]
//
// A.json and B.json are state dumps (--dump-state) and must be byte-identical.
// With --hash-logs, the two hash logs (--hash-log) must be identical too; when they are not,
// the tool reports between which ticks the runs first diverged.
//
// Exit codes: 0 = identical, 1 = different, 2 = bad arguments or unreadable file.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

bool ReadFile(const char* path, std::string& content) {
    std::FILE* file = nullptr;
    if (fopen_s(&file, path, "rb") != 0 || file == nullptr) {
        std::fprintf(stderr, "state_compare: cannot read %s\n", path);
        return false;
    }
    char buffer[4096];
    size_t count = 0;
    while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        content.append(buffer, count);
    }
    std::fclose(file);
    return true;
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

// Index of the first line that differs, or -1 when the line lists are equal.
long long FirstDifferentLine(const std::vector<std::string>& first, const std::vector<std::string>& second) {
    const size_t common = first.size() < second.size() ? first.size() : second.size();
    for (size_t i = 0; i < common; ++i) {
        if (first[i] != second[i]) {
            return static_cast<long long>(i);
        }
    }
    return first.size() == second.size() ? -1 : static_cast<long long>(common);
}

const char* LineOrEnd(const std::vector<std::string>& lines, long long index) {
    return index < static_cast<long long>(lines.size()) ? lines[static_cast<size_t>(index)].c_str() : "<end of file>";
}

// The tick number is the first word of a hash log line.
std::string TickOf(const std::vector<std::string>& lines, long long index) {
    if (index < 0 || index >= static_cast<long long>(lines.size())) {
        return "?";
    }
    const std::string& line = lines[static_cast<size_t>(index)];
    return line.substr(0, line.find(' '));
}

} // namespace

int main(int argc, char** argv) {
    const bool has_logs = argc == 6 && std::strcmp(argv[3], "--hash-logs") == 0;
    if (argc != 3 && !has_logs) {
        std::fprintf(stderr, "usage: state_compare A.json B.json [--hash-logs A.log B.log]\n");
        return 2;
    }

    std::string first_state;
    std::string second_state;
    if (!ReadFile(argv[1], first_state) || !ReadFile(argv[2], second_state)) {
        return 2;
    }
    if (first_state.empty()) {
        std::fprintf(stderr, "state_compare: %s is empty\n", argv[1]);
        return 2;
    }

    bool identical = true;

    if (has_logs) {
        std::string first_log;
        std::string second_log;
        if (!ReadFile(argv[4], first_log) || !ReadFile(argv[5], second_log)) {
            return 2;
        }
        const std::vector<std::string> first_lines = SplitLines(first_log);
        const std::vector<std::string> second_lines = SplitLines(second_log);
        if (first_lines.empty()) {
            std::fprintf(stderr, "state_compare: %s is empty\n", argv[4]);
            return 2;
        }
        const long long different = FirstDifferentLine(first_lines, second_lines);
        if (different >= 0) {
            identical = false;
            std::printf("DIFFERENT: the runs diverged.\n");
            if (different == 0) {
                std::printf("  The very first logged state, after tick %s, already differs.\n",
                            TickOf(first_lines, 0).c_str());
            } else {
                std::printf("  Last identical state: after tick %s.\n", TickOf(first_lines, different - 1).c_str());
                std::printf("  First different state: after tick %s.\n", TickOf(first_lines, different).c_str());
            }
            std::printf("  A: %s\n  B: %s\n", LineOrEnd(first_lines, different), LineOrEnd(second_lines, different));
        } else {
            std::printf("hash logs identical: %zu entries, last after tick %s\n", first_lines.size(),
                        TickOf(first_lines, static_cast<long long>(first_lines.size()) - 1).c_str());
        }
    }

    if (first_state != second_state) {
        identical = false;
        const std::vector<std::string> first_lines = SplitLines(first_state);
        const std::vector<std::string> second_lines = SplitLines(second_state);
        const long long different = FirstDifferentLine(first_lines, second_lines);
        std::printf("DIFFERENT: the final states differ, first at line %lld.\n", different + 1);
        // Show a little context: the entity header is a few lines above the differing component.
        for (long long line = different > 6 ? different - 6 : 0; line < different; ++line) {
            std::printf("     %s\n", LineOrEnd(first_lines, line));
        }
        std::printf("  A: %s\n  B: %s\n", LineOrEnd(first_lines, different), LineOrEnd(second_lines, different));
    } else {
        std::printf("final states identical: %zu bytes\n", first_state.size());
    }

    std::printf("%s\n", identical ? "IDENTICAL" : "NOT IDENTICAL");
    return identical ? 0 : 1;
}
