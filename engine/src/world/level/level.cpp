#include "poligon/world/level.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace poligon {

namespace {

bool ParseNumber(const std::string& text, float& value) {
    char* end = nullptr;
    value = std::strtof(text.c_str(), &end);
    return !text.empty() && *end == '\0' && std::isfinite(value);
}

} // namespace

bool ParseLevel(const std::string& text, Level& level, std::string& error, const std::vector<std::string>& other_line_prefixes) {
    Level parsed;
    std::istringstream stream(text);
    std::string line;
    int line_number = 0;
    bool header_seen = false;
    const auto fail = [&](const std::string& message) {
        error = "line " + std::to_string(line_number) + ": " + message;
        return false;
    };
    while (std::getline(stream, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::istringstream words(line);
        std::string key;
        if (!(words >> key) || key[0] == '#') {
            continue;
        }
        std::vector<std::string> values;
        for (std::string word; words >> word;) {
            values.push_back(word);
        }
        if (!header_seen) {
            if (key != "poligon-level" || values.size() != 1 || values[0] != "1") {
                return fail("the file must start with \"poligon-level 1\"");
            }
            header_seen = true;
            continue;
        }
        if (key.rfind("terrain.", 0) == 0) {
            if (values.size() != 1 || !SetTerrainGeneratorSetting(parsed.terrain, key.substr(8), values[0])) {
                return fail("\"" + key + "\" is not a terrain setting with one number");
            }
        } else if (key == "render_settings") {
            if (values.size() != 1) {
                return fail("\"render_settings\" takes one file name");
            }
            parsed.render_settings = values[0];
        } else if (key == "player_start") {
            float x = 0.0f;
            float z = 0.0f;
            float turn = 0.0f;
            if (values.size() == 1 && values[0] == "lake") {
                parsed.start_at_lake = true;
            } else if (values.size() == 3 && ParseNumber(values[0], x) && ParseNumber(values[1], z) && ParseNumber(values[2], turn)) {
                parsed.start_at_lake = false;
                parsed.start_position = {x, z};
                parsed.start_turn_degrees = turn;
            } else {
                return fail("\"player_start\" takes \"lake\" or map x, z and a turn in degrees");
            }
        } else if (key == "object") {
            LevelObject object;
            if (values.size() != 5 || !ParseNumber(values[1], object.position.x) || !ParseNumber(values[2], object.position.y) ||
                !ParseNumber(values[3], object.turn_degrees) || !ParseNumber(values[4], object.scale) || object.scale <= 0.0f) {
                return fail("\"object\" takes an asset name, map x, z, a turn in degrees and a scale above 0");
            }
            object.asset = values[0];
            parsed.objects.push_back(object);
        } else if (std::find(other_line_prefixes.begin(), other_line_prefixes.end(), key) != other_line_prefixes.end()) {
            parsed.other_lines.emplace_back(line_number, line);
        } else {
            return fail("unknown word \"" + key + "\"");
        }
    }
    if (!header_seen) {
        error = "the file is empty";
        return false;
    }
    level = std::move(parsed);
    return true;
}

bool LoadLevel(const char* path, Level& level, std::string& error, const std::vector<std::string>& other_line_prefixes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = std::string("cannot open ") + path;
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!ParseLevel(text, level, error, other_line_prefixes)) {
        error = std::string(path) + ": " + error;
        return false;
    }
    return true;
}

bool SaveLevel(const char* path, const Level& level) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file << "poligon-level 1\n\n# The land\n";
    for (const auto& [name, value] : GetTerrainGeneratorSettings(level.terrain)) {
        file << "terrain." << name << ' ' << value << '\n';
    }
    if (!level.render_settings.empty()) {
        file << "\nrender_settings " << level.render_settings << '\n';
    }
    file << "\n# Where the player starts\n";
    if (level.start_at_lake) {
        file << "player_start lake\n";
    } else {
        char text[128];
        std::snprintf(text, sizeof(text), "player_start %.9g %.9g %.9g\n", static_cast<double>(level.start_position.x),
                      static_cast<double>(level.start_position.y), static_cast<double>(level.start_turn_degrees));
        file << text;
    }
    if (!level.objects.empty()) {
        file << "\n# Things put by hand\n";
        for (const LevelObject& object : level.objects) {
            char text[256];
            std::snprintf(text, sizeof(text), "object %s %.9g %.9g %.9g %.9g\n", object.asset.c_str(),
                          static_cast<double>(object.position.x), static_cast<double>(object.position.y),
                          static_cast<double>(object.turn_degrees), static_cast<double>(object.scale));
            file << text;
        }
    }
    if (!level.other_lines.empty()) {
        file << '\n';
        for (const auto& [line_number, text] : level.other_lines) {
            file << text << '\n';
        }
    }
    return static_cast<bool>(file);
}

} // namespace poligon
