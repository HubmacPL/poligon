#include "poligon/core/version.h"

namespace poligon {

Version GetEngineVersion() {
    return Version{0, 1, 0};
}

const char* GetEngineVersionString() {
    return "0.1.0";
}

} // namespace poligon
