#pragma once

namespace poligon {

struct Version {
    int major;
    int minor;
    int patch;
};

// Version of the engine library this program was linked against.
Version GetEngineVersion();

// Same version as text, for example "0.1.0". The pointer is valid for the whole program run.
const char* GetEngineVersionString();

} // namespace poligon
