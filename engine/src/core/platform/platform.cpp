#include "poligon/core/platform.h"

#include <SDL3/SDL.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif
#include <imgui.h>
#include <imgui_impl_sdl3.h>

namespace poligon {

namespace {

bool escape_pressed = false;  // during the last ProcessPlatformEvents call

} // namespace

bool ProcessPlatformEvents(bool escape_quits) {
    escape_pressed = false;
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
        } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE && !event.key.repeat) {
            escape_pressed = true;
            keep_running = keep_running && !escape_quits;
        }
    }
    return keep_running;
}

bool WasEscapePressed() {
    return escape_pressed;
}

void ShowMessageBox(const char* title, const char* message) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title, message, nullptr);
}

std::uint64_t GetProcessMemoryBytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
        return static_cast<std::uint64_t>(counters.PrivateUsage);
    }
#endif
    return 0;
}

} // namespace poligon
