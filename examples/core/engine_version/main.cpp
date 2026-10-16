// Example: read the engine version.
#include <cstdio>

#include "poligon/core/version.h"

int main() {
    const poligon::Version version = poligon::GetEngineVersion();
    std::printf("Poligon %d.%d.%d (%s)\n", version.major, version.minor, version.patch,
                poligon::GetEngineVersionString());
    return 0;
}
