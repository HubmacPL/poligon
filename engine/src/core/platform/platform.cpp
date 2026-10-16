#include "poligon/core/platform.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

namespace poligon {

bool ProcessPlatformEvents() {
    if (!SDL_WasInit(SDL_INIT_EVENTS)) {
        return true;
    }

    // The debug windows (Dear ImGui) need to see every event; they exist only when a
    // Renderer was created with RendererConfig::debug_ui and a window.
    const bool debug_ui = ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().BackendPlatformUserData != nullptr;

    bool keep_running = true;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (debug_ui) {
            ImGui_ImplSDL3_ProcessEvent(&event);
        }
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            keep_running = false;
        } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
            keep_running = false;
        }
    }
    return keep_running;
}

void ShowMessageBox(const char* title, const char* message) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title, message, nullptr);
}

} // namespace poligon
