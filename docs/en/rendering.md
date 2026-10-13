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

A frame is recorded by `DrawMesh` calls and carried out in `EndFrame`, in three passes:

1. **Shadows.** The scene's depth as seen from the sun, four times (**cascades**): the visible range up to `Environment::shadow_distance` is cut into four parts, near ones sharp, far ones coarse. All four share one depth image as 2 x 2 tiles.
2. **Scene.** Sky first, then meshes, in linear colors with a wide range (values above 1 are kept), with multisampling (`RendererConfig::msaa_samples`, 4 by default). Cut-out materials use **alpha to coverage**, so leaf edges are smooth.
3. **Tone mapping.** The wide-range image is turned into displayable colors: `exposure` scales brightness, `ToneMapping::Aces` rolls bright areas off smoothly, `ToneMapping::None` clips (exact; used by tests).

## Materials and light

- `Material` is the metallic-roughness model of glTF: base color (times `texture`), `metallic`, `roughness`, optional `normal_texture` (fine relief) and `orm_texture` (green roughness, blue metallic, red ambient occlusion when `occlusion_in_orm`). Data textures are created with `TextureOptions::is_color = false`.
- Normal maps follow the OpenGL convention (green points to the top of the image). Meshes need no tangents; the shader derives them per pixel.
- `DirectionalLight` is the sun: `direction` it shines in, `color` (may exceed 1), and flat `ambient` light. Units: a white matte surface facing the sun reflects exactly the sun's color.
- `Environment` (third argument of `BeginFrame`): `draw_sky` (gradient from `sky_horizon` to `sky_zenith`, `sky_ground` below, sun disc), `sky_light` (surfaces are lit by the sky from above and the ground from below instead of flat ambient; smooth surfaces mirror the sky), `shadows` and `shadow_distance`, `fog_density` and `fog_color`. The default is plain: no sky, no fog, shadows on.
- Double-sided materials are treated as thin foliage: light from behind shines through a little.
- A sunny scene has light values of 2 to 4, so use an exposure around 0.5 to 0.7.
- `tests/scene_render_test.cpp` checks shadows, sky, fog, normal map orientation, multisampling and tone mapping by pixel values on both backends.
## Conventions

- Math is GLM, used directly (`glm::vec3`, `glm::mat4`, `glm::quat`, `glm::translate`, `glm::lookAt`...). Always through `#include "poligon/math.h"`, never through GLM headers, and without your own `GLM_FORCE_*`: the `poligon_engine` target sets the configuration.
- Right-handed world, +Y is up, a camera looks down its local -Z axis.
- 4x4 column-major matrices, column vectors: `a * b` applies `b` first. The usual object transform is `translate * rotate * scale`.
- Angles for GLM functions are in radians: `glm::radians(degrees)`.
- Clip-space depth runs from 0 (near) to 1 (far).
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
- Generated trees: `tools/assets/make_tree.mjs` runs the EZ-Tree library (MIT) under Node.js and writes a glTF with bark and twig textures: `node tools/assets/make_tree.mjs NODE_MODULES OUTPUT_DIR NAME PRESET HEIGHT`. PRESET is a built-in name ("Pine Large", "Oak Medium", "Bush 1"...) or a `.json` file; ours are in `tools/assets/tree_presets/`. EZ-Tree and three.js are installed outside the repository (`poligon-assets/tools/eztree`).
- `model_viewer FILE TURN TILT DISTANCE` sets the starting view, for close-up pictures.
- Downloaded and simplified assets live in `poligon-assets/` next to the project (`polyhaven/`, `simplified/`, `previews/`, `thumbnails/`), never in the repository.
- Not loaded yet: skeletons, animations, normal maps and the other glTF material textures, cameras and lights from the file. Mirrored transforms (negative scale) would flip triangle winding.

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

- One light (the sun) plus sky light; no point or spot lights. No true transparency (only cut-out). No reflections of the scene, no ambient occlusion pass.
- Normals are correct for rotation and for scaling along the mesh axes; a general non-uniform scale with rotation would need normal matrices.
- Textures are uncompressed (4 bytes per pixel plus mipmaps): enough for tests with 4 GB of card memory, not for a large world.
- The alpha channel of a texture is ignored.
- The window has a fixed size; resizing is not handled.
