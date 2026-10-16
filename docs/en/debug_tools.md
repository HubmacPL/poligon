# Debug tools

Windows with sliders and readouts drawn over the running game, a camera that flies freely, and views of single stages of the frame. They are for a person at the screen; everything they change can also be given to a headless run through a file.

Headers: `poligon/debug/debug_ui.h`, `poligon/render/render_settings.h`, `poligon/debug/free_camera.h`. Example: `examples/debug_ui`.

## Debug windows

- Drawn with the Dear ImGui library (MIT), which game code calls directly: `ImGui::Begin`, `ImGui::Text`, `ImGui::SliderFloat`, `ImGui::End`.
- Turn them on with `RendererConfig::debug_ui = true`. The renderer starts a Dear ImGui frame in `BeginFrame` and draws it at the end of `EndFrame`, over the finished picture. Dear ImGui calls are allowed only between the two.
- **Headless mode has no debug windows.** `Renderer::IsDebugUiActive()` is then false, the ready-made windows do nothing, and your own `ImGui::` calls must be skipped with `if (renderer.IsDebugUiActive())`. A headless frame dump therefore never contains windows; a dump from a run with a window does.
- While the mouse is over a window, or a text field is being typed into, `Input` reports mouse or keyboard as untouched, so dragging a slider does not also turn the camera. `ProcessPlatformEvents` passes window events on to Dear ImGui.
- No settings file is written (`imgui.ini`): windows open at the same place on every start.

Ready-made windows:

| Function | Shows |
|---|---|
| `ShowRenderSettingsWindow(renderer, settings, file_path)` | Sliders for sun, sky, shadows, fog, ambient occlusion, exposure, bloom, debug view; Save and Load buttons for `file_path` |
| `ShowFrameStatsWindow(renderer)` | Frames per second, milliseconds per frame, meshes drawn of submitted, shadow draws, backend |
| `ShowWorldWindow(renderer, world)` | Every entity with its components and values, as in the JSON state dump; filter by name. Read-only |

## Render settings

- `RenderSettings` bundles `DirectionalLight sun`, `Environment environment` and `PostSettings post`: everything about the look that is not the scene itself. Pass the parts to `BeginFrame` and `Renderer::SetPostSettings` every frame.
- `PostSettings` (tone mapping, exposure, bloom, automatic exposure, debug view) starts as `RendererConfig::post` and can be changed between frames.
- `SaveRenderSettings(path, settings)` and `LoadRenderSettings(path, settings, error)` write and read a text file, one setting per line:

```
poligon-render-settings 1

# fog
fog.density 0.0120000001
fog.color 0.800000012 0.779999971 0.699999988
fog.volumetric 1
```

- Numbers are written with 9 significant digits, so a loaded file gives bit-identical values. Settings missing from a file keep their value; an unknown name, a wrong number of values or a word in place of a number is an error that names the line, and nothing is changed.
- The standard option `--render-settings FILE` (in `CommandLineOptions::render_settings_path`) is how a look found with the sliders reaches a headless run: Save in the window, then `forest_scene --headless --render-settings FILE --dump-frame out.png`. `toy` and `forest_scene` support it.
- The `quality` part of the file holds the graphics preset and its values (`RenderSettings::quality`, see [rendering.md](rendering.md)). `quality.preset NAME` is applied before any other line, so a saved file (the preset name plus every value) loads back exactly, and a short file written by hand such as `quality.preset low` plus one changed value means "low, except this".
- One list of settings (`VisitRenderSettings`) drives the file and the window. A new setting is added there, once.

## Debug views

- `PostSettings::debug_view` shows one stage instead of the finished frame: `AmbientOcclusion` (white = open, dark = occluded), `Fog` (the light the volumetric fog adds), `Bloom` (the blurred image that bloom mixes in). A stage that is off shows as white.
- `Environment::show_shadow_cascades` tints surfaces by the shadow cascade that shades them: red, green, blue, yellow from near to far.
- Both are in the settings window and in the settings file.

## Free camera

- `FreeCamera` holds a position, a yaw and a pitch. `Move(forward, right, up)`, `Turn(yaw, pitch)`, `SetFromCamera(camera)`, `ToCamera(base)`. It reads no devices; the caller says how far to move.
- `FlyFreeCamera(renderer, camera)` flies it with I/J/K/L, U/O (down, up), Left Shift (faster) and the middle mouse button (look). These keys are read through Dear ImGui and not through `Input`, so they never enter an input recording, and the free camera is not part of the game state: a recorded run replays the same with or without it.
- In `toy`: F switches the free camera, Tab hides the windows. In `forest_scene`: F switches between walking and flying, Tab hides the windows, F11 switches between a 1280 x 720 window and fullscreen at 1920 x 1080.

## Known limits

- Window colors are converted for our output image once at start; colors picked in a color field are shown slightly paler than they render.
- The world window re-reads the whole state every frame; fine for hundreds of entities, slow for many thousands.
- The world window relies on the line layout of the state dump; change both together.
