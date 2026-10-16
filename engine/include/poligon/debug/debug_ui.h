#pragma once

#include <imgui.h>

#include "poligon/ecs/ecs.h"
#include "poligon/debug/free_camera.h"
#include "poligon/render/render_settings.h"
#include "poligon/render/renderer.h"

namespace poligon {

// Debug windows drawn over the game with the Dear ImGui library.
//
// Turn them on with RendererConfig::debug_ui. Between Renderer::BeginFrame and EndFrame,
// call the ready-made windows below, or build your own with ImGui:: calls directly (this
// header includes imgui.h). The renderer draws them at the end of the frame.
//
// In headless mode there are no debug windows: Renderer::IsDebugUiActive() is false, the
// functions below do nothing, and your own ImGui:: calls must be skipped:
//
//     if (renderer.IsDebugUiActive()) {
//         ImGui::Begin("My window");
//         ImGui::Text("ticks: %lld", ticks);
//         ImGui::End();
//     }
//
// While the mouse is over a window, or a text field is being typed into, the game does not
// see the mouse or the keyboard (Input returns "nothing pressed"), so dragging a slider
// does not also turn the camera.

// Sliders for the sun, sky, shadows, fog, ambient occlusion, exposure, bloom and the debug
// view. Changes `settings` in place; the caller passes them to BeginFrame and
// SetPostSettings as usual. With a `file_path`, the window has buttons that save the
// settings to that file and load them back (see poligon/render/render_settings.h).
// Returns true if something was changed this frame.
bool ShowRenderSettingsWindow(const Renderer& renderer, RenderSettings& settings, const char* file_path = nullptr);

// Frames per second, time per frame, how many meshes were drawn, backend and multisampling.
void ShowFrameStatsWindow(const Renderer& renderer);

// Every entity of the world with its components and their values, read the same way as the
// JSON state dump (poligon/ecs/state_dump.h), with a filter by name. Read-only: changing game
// state by hand would break the repeatability of a recorded run.
void ShowWorldWindow(const Renderer& renderer, const flecs::world& world);

// Flies a FreeCamera with keys that games rarely use, read straight from the keyboard and
// not through Input, so nothing of it ends up in an input recording:
//   I/K forward and back, J/L left and right, U/O down and up, hold Left Shift to go faster,
//   hold the middle mouse button and move the mouse to look around.
// Call once per rendered frame. Does nothing while a text field is being typed into.
void FlyFreeCamera(const Renderer& renderer, FreeCamera& camera, float meters_per_second = 6.0f);

} // namespace poligon
