#include "poligon/core/fatal_error.h"

#include <cstdio>

#include "crash_report.h"

namespace poligon {

void FatalError(const char* message) {
    char headline[1024];
    std::snprintf(headline, sizeof(headline), "FATAL: %s", message);
    ReportAndExit(headline, 3, 1);
}

} // namespace poligon
