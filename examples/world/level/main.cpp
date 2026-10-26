// Example: read a level file (terrain, player start, placed objects) and write it back.
#include <cstdio>
#include <string>

#include "poligon/world/level.h"

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "example_level.txt";

    const std::string text =
        "poligon-level 1\n"
        "terrain.seed 7\n"
        "terrain.size 2000\n"
        "player_start 1200 800 45\n"
        "object rocks/boulder 1210 805 30 1.5\n"
        "plant pine trees/pine\n";  // a line for another reader (the plant rules)

    poligon::Level level;
    std::string error;
    if (!poligon::ParseLevel(text, level, error, {"plant"})) {
        std::printf("error: %s\n", error.c_str());
        return 1;
    }
    std::printf("seed %llu, size %.0f m, start at lake %d, start %.0f %.0f, turn %.0f\n",
                static_cast<unsigned long long>(level.terrain.seed), level.terrain.size, level.start_at_lake,
                level.start_position.x, level.start_position.y, level.start_turn_degrees);
    for (const poligon::LevelObject& object : level.objects) {
        std::printf("object %s at %.0f %.0f\n", object.asset.c_str(), object.position.x, object.position.y);
    }
    std::printf("lines left for other readers: %zu\n", level.other_lines.size());

    // A bad line is an error that names it.
    poligon::Level bad;
    std::string bad_error;
    const bool bad_parsed = poligon::ParseLevel("poligon-level 1\nterrain.seed seven\n", bad, bad_error);
    std::printf("bad file: %s\n", bad_error.c_str());

    poligon::Level loaded;
    if (!poligon::SaveLevel(path, level) || !poligon::LoadLevel(path, loaded, error, {"plant"})) {
        std::printf("save or load failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("saved and loaded back from %s: %zu object(s)\n", path, loaded.objects.size());
    return (!bad_parsed && loaded.objects.size() == 1 && loaded.terrain.seed == 7) ? 0 : 1;
}
