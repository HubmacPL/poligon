#include "poligon/platform.h"

#include <SDL3/SDL.h>

namespace poligon {

bool ProcessPlatformEvents() {
    if (!SDL_WasInit(SDL_INIT_EVENTS)) {
        return true;
    }

    bool keep_running = true;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            keep_running = false;
        } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
            keep_running = false;
        }
    }
    return keep_running;
}

} // namespace poligon
