#include "poligon/impostor.h"

#include <fstream>
#include <utility>

namespace poligon {

namespace {

// NAME.impostor -> NAME_color.png (or _normal.png).
std::string PicturePath(const char* path, const char* suffix) {
    std::string base = path;
    const size_t dot = base.find_last_of('.');
    const size_t slash = base.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        base.resize(dot);
    }
    return base + suffix;
}

} // namespace

bool SaveImpostor(const char* path, const Impostor& impostor) {
    std::ofstream file(path);
    if (!file) {
        return false;
    }
    file.precision(9);
    file << "poligon-impostor 1\nframes_per_side " << impostor.frames_per_side << "\ncenter " << impostor.center.x << ' '
         << impostor.center.y << ' ' << impostor.center.z << "\nradius " << impostor.radius << '\n';
    file.close();
    return !file.fail() && SaveImageToPng(PicturePath(path, "_color.png").c_str(), impostor.color) &&
           SaveImageToPng(PicturePath(path, "_normal.png").c_str(), impostor.normal);
}

bool LoadImpostor(const char* path, Impostor& impostor, std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = std::string("cannot open ") + path;
        return false;
    }
    std::string word;
    int version = 0;
    Impostor loaded;
    if (!(file >> word >> version) || word != "poligon-impostor" || version != 1) {
        error = std::string(path) + " is not a version 1 impostor file";
        return false;
    }
    if (!(file >> word >> loaded.frames_per_side) || word != "frames_per_side" ||
        !(file >> word >> loaded.center.x >> loaded.center.y >> loaded.center.z) || word != "center" ||
        !(file >> word >> loaded.radius) || word != "radius") {
        error = std::string(path) + ": expected the lines frames_per_side, center and radius";
        return false;
    }
    if (loaded.frames_per_side < 2 || loaded.radius <= 0.0f) {
        error = std::string(path) + ": frames_per_side must be at least 2 and radius above 0";
        return false;
    }
    if (!LoadImageFromFile(PicturePath(path, "_color.png").c_str(), loaded.color, error) ||
        !LoadImageFromFile(PicturePath(path, "_normal.png").c_str(), loaded.normal, error)) {
        return false;
    }
    if (loaded.color.width != loaded.normal.width || loaded.color.height != loaded.normal.height ||
        loaded.color.width != loaded.color.height) {
        error = std::string(path) + ": the color and normal pictures must be square and of the same size";
        return false;
    }
    impostor = std::move(loaded);
    return true;
}

} // namespace poligon
