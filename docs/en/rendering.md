# Rendering

Headers: `poligon/renderer.h`, `poligon/math.h`, `poligon/image.h`, `poligon/platform.h`.
Examples: `examples/renderer`, `examples/texture`, `examples/environment`, `examples/model`. Shaders: `engine/shaders/*.hlsl`.

## How it works

- `Renderer` draws every frame to an off-screen image (1280x720 by default). With a window that image is then copied to the window; without one it can only be saved. The pixels are the same in both modes.
- Order within a frame: `BeginFrame(camera, light)`, any number of `DrawMesh(mesh, transform, material)` (or with a plain color), `EndFrame()`. After `EndFrame` you may call `SaveFrameToPng(path)` or `CaptureFrame(image)`.
- Meshes and textures live until the renderer is destroyed; create them outside `BeginFrame`/`EndFrame`.
- The background color (`RendererConfig::clear_color`) is set once when the renderer is created.
- Unrecoverable problems (no graphics device, calls in the wrong order) end the process through `FatalError`.
- Engine headers do not expose SDL types.

## Frame stages

A frame is recorded by `DrawMesh` calls and carried out in `EndFrame`, in these passes (those turned off are skipped):

1. **Shadows.** The scene's depth as seen from the sun, four times (**cascades**): the visible range up to `Environment::shadow_distance` is cut into four parts, near ones sharp, far ones coarse (the splits are computed from 1 m out, so the sharpest cascade is not spent on the first step in front of the camera). All four share one depth image as 2 x 2 tiles. Each cascade is a sphere around its part of the view, moved in whole shadow pixels only, so shadows do not crawl when the camera turns or moves. A lookup reads a tent filter 5 shadow pixels wide (9 bilinear comparisons) with the point moved off the surface by the filter's reach along the normal, which gives soft edges without steps or self-shadowing. Over the last tenth of its range each cascade fades into the next, and the last one into full light, so no line shows where sharpness changes or where shadows end. Terrain skirts (the rims that hide gaps between patches) are left out of the shadow pass: seen from the sun they are walls, and on a slope they would shadow the land below them in a grid.
2. **Depth and ambient occlusion** (`Environment::ambient_occlusion`). The scene's depth as seen from the camera is drawn first, with one sample per pixel. From it the renderer works out, per pixel, how much nearby geometry blocks the surroundings (crevices, the inside of a tree crown, where a trunk meets the ground) and blurs the result. The scene pass then gives such places less ambient light; direct sunlight is not affected. `ambient_occlusion_radius` is the reach in meters.
3. **Scene.** Sky first, then meshes, in linear colors with a wide range (values above 1 are kept), with multisampling (`RendererConfig::msaa_samples`, 4 by default). Cut-out materials use **alpha to coverage**, so leaf edges are smooth.
4. **Volumetric fog** (`Environment::volumetric_fog`, uses `fog_density` and `fog_color`). At half resolution, every line of sight is walked in 32 steps; each step adds the light the fog scatters there and dims what lies behind. Steps in the sun's shadow add less (`fog_shadow_brightness`), which shows as **shafts of light** between trees. The fog glows around the sun (`fog_sun_glow`), can thin out with height (`fog_height_falloff`, `fog_base_height`) and also covers the sky. It replaces the simple distance fog of the scene pass.
5. **Temporal anti-aliasing** (`PostSettings::temporal_aa`, off by default). Every frame the camera is shifted by a different fraction of a pixel (8 points of the Halton sequence) and the new frame is blended into the previous result (`temporal_aa_blend`, 0.1: about the last ten frames count). For each pixel the depth image gives the point it shows, the previous frame's unshifted camera gives where that point was, and the previous result is read there with a Catmull-Rom filter. The nearest surface in the 3 x 3 neighborhood decides the motion, so that edges of near objects move with them. What moved by other means (wind, animals) is handled by clipping the history into the range of colors the new frame's neighborhood holds (mean plus or minus 1.25 standard deviations in YCoCg, with colors squeezed by brightness first), which removes ghosts. Faster-moving pixels take more of the new frame. With TAA on, the noise patterns of ambient occlusion, fog steps and dithered fades change every frame and are averaged away. The depth pass runs whenever TAA is on. MSAA is not needed with it (`forest_scene` turns it off and saves 3 ms); `sharpen` gives back some of the softness. A camera jump of more than 25 m, or `Renderer::ResetTemporalHistory()`, starts fresh. Not handled yet: motion of animated meshes and of objects moved by `DrawMesh` is not tracked per pixel (only the camera's), so fast movers rely on the clipping and may smear slightly. Test: `tests/temporal_aa_test.cpp`; example: `examples/temporal_aa`.
6. **Bloom** (`RendererConfig::bloom`, `bloom_strength`). The scene is shrunk into a chain of ever smaller, blurrier copies, which are then mixed back together and blended into the picture: bright areas get a soft glow.
7. **Tone mapping and grading.** The wide-range image is turned into displayable colors: `exposure` scales brightness, **automatic exposure** (`RendererConfig::auto_exposure`) first brings the average brightness of the picture to `auto_exposure_target`, moving `auto_exposure_speed` of the way each frame (all the way on the first frame), `ToneMapping::Aces` rolls bright areas off smoothly, `ToneMapping::None` clips (exact; used by tests). Grading (`PostSettings`): `temperature` and `tint` (white balance, divided by its own brightness so it does not change exposure) and `saturation` act in linear light before the curve, `contrast` acts around middle gray in a square-root scale after it, then the `vignette`; `sharpen` works on the anti-aliased image before exposure, limited to the range of the four neighbors so that edges do not ring.

**Repeatable frames with temporal effects.** TAA (and later clouds) use previous frames, yet a headless run must give the same picture every time. This holds because everything a frame depends on is a function of the frames before it, all of which are rendered the same way: the sub-pixel shift and the noise offset come from the renderer's own frame counter (never the clock), the history is two images written in turn inside the renderer, and a new renderer starts with no history. So the same program, settings and sequence of frames give the same pixels to the bit on one machine and backend (checked by `temporal_aa_test`, which renders the same 17 frames twice and compares the bytes). Tests that check single pixels keep TAA off (the default), so their first frame is not blended with anything; `forest_scene --headless --ticks N` renders a frame on every tick, so a dump after N ticks is always the N-th frame of the same sequence.

**Wind** moves plants in every pass (so shadows move too). `Environment::wind_direction`, `wind_strength` (1 = a moderate breeze, 0 = off) and `wind_time` (the game's own time in seconds, so that replays match) describe the frame's wind; `Material::wind_bend` and `wind_flutter` say how much a mesh gives to it. In the vertex shaders (`ApplyWind` in `common.hlsli`) the whole plant leans downwind by `wind_bend` x strength x about 3 cm per meter above its foot (the model's origin), with gusts that travel across the land as two waves and start at a slightly different moment for every plant, and each vertex shivers on its own by `wind_flutter` x strength x about 4 cm. Trees take bend 1 and flutter 1 on their leaves; short plants take a larger bend (grass 6), because they bend more per meter. Example: `examples/wind`; test: the wind part of `tests/effects_render_test.cpp`.

Before the passes, meshes are **culled**: a mesh whose bounding box lies entirely outside the camera's view is not drawn, and each shadow cascade skips meshes that cannot cast a shadow into it (a mesh outside the view still can). `Renderer::GetFrameStats()` returns how many meshes were submitted and drawn in the last frame.

Many copies of one mesh are drawn with `DrawMeshInstances` (one draw call per pass, each copy culled on its own); forests go through the vegetation system built on it. See [vegetation.md](vegetation.md).

## Materials and light

- `Material` is the metallic-roughness model of glTF: base color (times `texture`), `metallic`, `roughness`, optional `normal_texture` (fine relief) and `orm_texture` (green roughness, blue metallic, red ambient occlusion when `occlusion_in_orm`). Data textures are created with `TextureOptions::is_color = false`.
- Normal maps follow the OpenGL convention (green points to the top of the image). Meshes need no tangents; the shader derives them per pixel.
- `DirectionalLight` is the sun: `direction` it shines in, `color` (may exceed 1), and flat `ambient` light. Units: a white matte surface facing the sun reflects exactly the sun's color.
- `Environment` (third argument of `BeginFrame`): `draw_sky` (gradient from `sky_horizon` to `sky_zenith`, `sky_ground` below, sun disc), `sky_light` (surfaces are lit by the sky from above and the ground from below instead of flat ambient; smooth surfaces mirror the sky), `shadows` and `shadow_distance`, `fog_density` and `fog_color`. The default is plain: no sky, no fog, shadows on.
- Double-sided materials are treated as thin foliage: light from behind shines through a little.
- A sunny scene has light values of 2 to 4: turn on `auto_exposure`, or use an exposure around 0.5 to 0.7.
- The new effects are off by default, so that a plain scene stays plain and exact; `examples/environment` turns all of them on.
- `tests/scene_render_test.cpp` checks shadows, sky, fog, normal map orientation, multisampling and tone mapping by pixel values on both backends; `tests/effects_render_test.cpp` does the same for ambient occlusion, volumetric fog, bloom and automatic exposure.
## Conventions

- Math is GLM, used directly (`glm::vec3`, `glm::mat4`, `glm::quat`, `glm::translate`, `glm::lookAt`...). Always through `#include "poligon/math.h"`, never through GLM headers, and without your own `GLM_FORCE_*`: the `poligon_engine` target sets the configuration.
- Right-handed world, +Y is up, a camera looks down its local -Z axis.
- 4x4 column-major matrices, column vectors: `a * b` applies `b` first. The usual object transform is `translate * rotate * scale`.
- Angles for GLM functions are in radians: `glm::radians(degrees)`.
- Clip-space depth runs from 0 to 1 (`GLM_FORCE_DEPTH_ZERO_TO_ONE`). The camera uses it **reversed**: 1 at the near plane, 0 at the far one (near and far swapped in `glm::perspective`, depth cleared to 0, depth test "greater"). With a floating-point depth image this keeps millimeters of precision kilometers away, where the usual direction has meters and distant land, trees and impostors fight over pixels. Shadow cascades keep the usual direction. Shaders that read the camera's depth treat 0 as the sky.
- A default-constructed vector is zero, a matrix identity, a quaternion no rotation.
- Vectors have only `x`, `y`, `z`, `w` members (no aliases such as `.r` or `.s`).
- `tests/math_test.cpp` guards these conventions.
- Triangle front faces are counter-clockwise. Indices are 32-bit.
- Texture coordinates (UV): (0, 0) is the top-left corner of the image, (1, 1) the bottom-right. glTF uses the same.
- Material and light colors are linear. The output image and color textures are stored sRGB-encoded (like image files and displays), and the graphics card converts both ways. The background color is given as it should look on screen.

## Textures and materials

- `Image` (`poligon/image.h`): RGBA bytes, rows from the top. `LoadImageFromFile` reads PNG and JPEG, `SaveImageToPng` writes, `MakeCheckerImage` and `MakeSolidImage` create images in code.
- `Renderer::CreateTexture(image, options)` copies an image to the graphics card. Default: a color image (sRGB) with mipmaps. `is_color = false` for images that hold data other than color.
- **Mipmaps** are smaller copies of a texture used when it is drawn small or far away; without them distant textures shimmer.
- `Material` has a color, a texture and `uv_scale`. The color multiplies the texture; a material without a texture has the plain color; `uv_scale` repeats the texture.
- `alpha_cutoff` above 0 makes pixels with lower alpha invisible (**alpha cutout**): the way leaves are drawn, as flat squares with a picture of a twig. `double_sided` draws both sides of every triangle and lights the back as if its normal were flipped. Models get both from the file (glTF alpha mode and `doubleSided`).
- There is one sampler for all textures: linear filtering with mipmaps, anisotropy 8, repeat outside the 0..1 range.
- Mipmaps are computed on the CPU (the same on every backend). For the color texture of a cut-out material (`TextureOptions::coverage_cutoff`, set by `UploadModel` from the material's cutoff) every mipmap has its alpha scaled so that the share of pixels above the cutoff stays as in the full image. Without this, averaging a twig picture down pushes its alpha below the cutoff, and crowns thin out with distance and vanish from the coarse shadow cascades, leaving only the shadows of trunks and branches.
- `tests/texture_render_test.cpp` renders headless and checks single pixels (orientation, colors, repetition, mipmaps) on both backends. It is the pattern for rendering tests without reference images.

## Models

Header: `poligon/model.h`. Example: `examples/model`. Tool: `model_viewer`.

- `LoadModelFromFile(path, model, error)` reads a model through the Assimp library: glTF/GLB, FBX, OBJ, DAE and about forty other formats. It needs no graphics card.
- The loaded `Model` follows our conventions whatever the file used: right-handed, +Y up, counter-clockwise front faces, UV (0, 0) at the top-left.
- A model file is a tree of nodes with transforms. Loading flattens it into **instances**: "draw this mesh with this transform". `Model::bounds_min/max` is the box around everything.
- Each material has a base color and, if present, a decoded base color texture (from a file next to the model or embedded in it). A texture that cannot be loaded gives an entry in `Model::warnings`, not an error.
- `UploadModel(renderer, model)` copies meshes and textures to the card; `DrawModel(renderer, render_model, transform)` draws them.
- `model_viewer` started with a double click browses the models in `poligon-assets` (N and P switch to the next and previous one); `model_viewer FOLDER` browses a folder; `model_viewer FILE` shows one model. Camera: left mouse drag, A/D, W/S, Q/E. `model_viewer FILE --headless --dump-frame out.png` writes one picture and prints triangle counts, size and warnings; this is how the agent looks at a model.
- Too heavy a model (film-quality trees have millions of triangles): `tools/assets/simplify_model.py` runs in Blender without its window and writes a glTF with a given part of the triangles kept: `blender -b --factory-startup -P tools/assets/simplify_model.py -- INPUT OUTPUT.gltf 0.1`. Blender is an external tool; nothing from it is linked.
- Impostors (far-away cards with baked pictures of a model) and the dithered fade between levels of detail are described in [vegetation.md](vegetation.md).
- Generated trees: `tools/assets/make_tree.mjs` runs the EZ-Tree library (MIT) under Node.js and writes glTF files with levels of detail: `node tools/assets/make_tree.mjs NODE_MODULES ASSETS_FOLDER RECIPE.json`. A recipe (ours are in `tools/assets/trees/`) gives an EZ-Tree preset with changes, the height, bark and twig textures (color, normal map, occlusion-roughness-metallic), the rectangles of a twig atlas (a texture holding several twigs; each card shows one, at its own proportions) and the levels. The full tree goes to `generated/NAME/`, the simpler levels to `lods/NAME/lodN/`, the layout `forest_scene` reads. Simpler levels come from the same tree: fewer sides on each tube, thin branches left out, fewer but larger cards; the script stops with an error if a level would change the tree's shape. The format is described at the top of the script. EZ-Tree and three.js are installed outside the repository (`poligon-assets/tools/eztree`).
- `model_viewer FILE TURN TILT DISTANCE` sets the starting view, for close-up pictures.
- `forest_scene` (in `tools/`) builds a piece of forest from the models and ground texture in `poligon-assets` and lets you walk through it (W/S/A/D, Shift to run, drag the mouse to look). It is the showcase for the renderer and the place to measure frame time: `forest_scene X Z TURN TILT --headless --ticks 60 --dump-frame out.png` prints milliseconds per frame and how many meshes were drawn. Measured on a GTX 980 at 1280 x 720 with every effect on: about 34 ms per frame in the `release` build. The forest is the same on every run (fixed random seed). Models that are missing are skipped.
- Downloaded and simplified assets live in `poligon-assets/` next to the project (`polyhaven/`, `simplified/`, `previews/`, `thumbnails/`), never in the repository.
- Leaves with a solid background around them: some packages (Poly Haven glTF) ship the leaf color as a JPG with no transparency and the mask as a separate file. The loader then warns that a cut-out material's texture has no transparent pixels. Repair: `image_tool merge-alpha COLOR ALPHA OUT.png` joins the two into one PNG (and spreads leaf colors into the transparent pixels, so edges get no dark rim), then `node tools/assets/set_gltf_cutout.mjs MODEL.gltf OLD_IMAGE_URI NEW_IMAGE_URI` points the model at it and marks the material as cut-out and double-sided. The untouched file stays as `MODEL.gltf.original`. `image_tool pack-orm OCCLUSION ROUGHNESS OUT.png` joins separate occlusion and roughness images into the one data texture glTF expects (red occlusion, green roughness, blue metallic = 0).
- Formats that cannot mark a material as a cut-out (FBX, OBJ): if at least 1% of a material's texture is transparent, the material is treated as a double-sided cut-out (fur, whiskers, leaves). glTF files say it themselves and are taken at their word.
- Skeletons and animation clips are loaded; see [animation.md](animation.md).
- Not loaded yet: emissive textures, cameras and lights from the file, animations of rigid parts. Mirrored transforms (negative scale) would flip triangle winding.

## Backend

- Default: **Direct3D 12**. Vulkan is built and tested as the second one (`--backend vulkan`).
- Reason: the D3D12 debug layer is part of Windows and works out of the box (`--gpu-validation`). Vulkan validation layers are not installed on this machine (they need the Vulkan SDK), so the same mistake passes silently there.
- In headless mode the renderer turns off Vulkan "implicit layers" (overlays and recorders of other programs, such as OBS or RivaTuner), which otherwise load their code into the process.
- Tests render the same frame on both backends and compare the images. This keeps shaders portable.

## Shaders

- Plain HLSL, compiled at build time by DXC to DXIL (D3D12) and SPIR-V (Vulkan), then embedded in the engine library as byte arrays. There are no shader files to find at run time.
- A new shader: a file in `engine/shaders/` and an entry `poligon_embed_shader(...)` in `engine/CMakeLists.txt`.
- SDL GPU rules that HLSL must follow:
  - vertex inputs use the semantics `TEXCOORD0`, `TEXCOORD1`, ... (the number is the attribute location);
  - uniform buffers of a vertex shader: `register(bN, space1)`; of a fragment shader: `register(bN, space3)`;
  - textures and samplers of a vertex shader: `space0`; of a fragment shader: `space2`;
  - for Vulkan a texture and its sampler must be one combined binding: both get `[[vk::combinedImageSampler]][[vk::binding(N, 2)]]` with the same N (pattern in `mesh.frag.hlsl`). The DXIL compile skips these attributes.
- The C++ structs (`FrameUniforms`, `ObjectUniforms`, `LightUniforms` in `renderer.cpp`) must match the `cbuffer`s in HLSL.

## Verifying images

- `--dump-frame FILE.png` saves the last frame when the run ends.
- `--gpu-validation` turns on the debug layer; tests with this flag fail on any `D3D12 ERROR` / `D3D12 WARNING` message.
- `image_compare A.png B.png [--channel-tolerance N] [--max-different-percent P]` compares two images with a tolerance (default: a difference of up to 2 per channel, up to 0.1% of pixels). Exit code 0 = match, 1 = no match.
- On one backend, images from two runs are identical, so they are compared with the tight default tolerance. Between Direct3D 12 and Vulkan we use `--channel-tolerance 12`: the drivers compute anisotropic texture filtering slightly differently (measured: up to 32 of 255 on a few hundred edge pixels). Without anisotropy both backends give bit-identical images.
- Mipmaps are computed by us on the CPU (`MakeHalfSizeImage`), so texture data is the same on every backend and driver.
- Reference image of the toy game: `tests/reference/toy_tick_45.png`. After an intended change of look: view the new dump from `build/<preset>/test_output/` and only then copy it over the reference.

## Known limits

- One light (the sun) plus sky light; no point or spot lights. No true transparency (only cut-out). No reflections of the scene.
- Ambient occlusion and fog see only what is on screen and only the nearest surface of each pixel. Fog is lit by the sun only within `shadow_distance` (beyond it, it counts as lit) and is walked at most 1000 m.
- Culling is per mesh and by view only: a mesh hidden behind another is still drawn.
- With ambient occlusion or volumetric fog on, the scene's geometry is drawn one more time (the depth pass).
- Automatic exposure adapts per frame, not per second, so its speed depends on the frame rate.
- Normals are correct for rotation and for scaling along the mesh axes; a general non-uniform scale with rotation would need normal matrices.
- Textures are uncompressed (4 bytes per pixel plus mipmaps): enough for tests with 4 GB of card memory, not for a large world.
- The alpha channel of a texture is used only for cut-out (`alpha_cutoff`), never for blending.
- The window has a fixed size; resizing is not handled.
