#include <cstdio>
#include <cstring>

#include "poligon/version.h"

int main() {
    const poligon::Version version = poligon::GetEngineVersion();

    char expected[32];
    std::snprintf(expected, sizeof(expected), "%d.%d.%d", version.major, version.minor, version.patch);

    if (std::strcmp(expected, poligon::GetEngineVersionString()) != 0) {
        std::printf("FAIL: version struct gives \"%s\" but string is \"%s\"\n", expected,
                    poligon::GetEngineVersionString());
        return 1;
    }
    return 0;
}
