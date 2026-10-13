// Passes when an invalid configuration ends the process with a FATAL line instead of a dialog box.
#include "poligon/game_loop.h"

int main() {
    poligon::GameLoopConfig config;
    config.ticks_per_second = 0;
    poligon::GameLoop loop(config);
    return 0;
}
