# Third-party licenses

Poligon itself is under the MIT License (`LICENSE`). This file lists everything of others that
is built into it or into the programs made from it, and the tools used to build it. Licenses
were checked on 2026-10-26 in the source archives vcpkg downloaded (the `copyright` file of
each package, which vcpkg copies from the project's own license file) and on the projects'
pages (the homepages are those of the vcpkg ports at the pinned catalog commit); versions are those pinned by `vcpkg.json` (catalog commit
`e456309491fd875487bf02b8a0dca76c1b1b00cc`) and our ports in `ports/`.

No dependency in this configuration is under the GPL or LGPL. The full license texts of the
libraries are installed with them by vcpkg under `vcpkg_installed/x64-windows/share/<name>/copyright`
and are shipped beside the programs in the release archive.

## Libraries linked into the engine and its programs

| Name | Version | License | Source |
|---|---|---|---|
| SDL3 (window, input, GPU API) | 3.4.18 | zlib (one OpenGL ES header in it under Apache 2.0, not used) | https://www.libsdl.org |
| GLM (math) | 1.0.3 | MIT (or the "Happy Bunny" license; MIT chosen) | https://glm.g-truc.net |
| stb (stb_image, stb_image_write, stb_truetype, stb_easy_font) | 2026-08-02 | MIT or public domain (choice; MIT chosen) | https://github.com/nothings/stb |
| Assimp (model import) | 6.0.5 | BSD 3-Clause | https://github.com/assimp/assimp |
| flecs (ECS) | 4.1.6 | MIT; bundles EmbeddableWebServer (BSD 2-Clause), wyhash (Unlicense, public domain), stm32tpl float formatting (MIT) | https://github.com/SanderMertens/flecs |
| Dear ImGui with its SDL3 and SDL GPU backends (debug windows) | 1.92.9 | MIT | https://github.com/ocornut/imgui |
| ozz-animation (skeletal animation; built from our port in `ports/ozz-animation`) | 0.17.0 | MIT | https://github.com/guillaumeblanc/ozz-animation |
| Jolt Physics (physics; built from our port in `ports/joltphysics`, double precision) | 5.6.0 | MIT | https://github.com/jrouwe/JoltPhysics |

### Pulled in by Assimp

| Name | Version | License | Source |
|---|---|---|---|
| zlib | 1.3.2 | zlib | https://www.zlib.net |
| minizip | 1.3.2 | zlib | https://github.com/madler/zlib (contrib/minizip) |
| kubazip (zip) | 0.3.15 | MIT | https://github.com/kuba--/zip |
| pugixml | 1.16 | MIT | https://github.com/zeux/pugixml |
| RapidJSON | 2025-02-26 | MIT (its JSON_checker test files are under the JSON License; they are not built) | http://rapidjson.org |
| utfcpp | 4.2.1 | Boost Software License 1.0 | https://github.com/nemtrif/utfcpp |
| polyclipping (Clipper) | 6.4.2 | Boost Software License 1.0 | https://sourceforge.net/projects/polyclipping |
| poly2tri | 2023-12-27 | BSD 3-Clause | https://github.com/jhasse/poly2tri |
| earcut.hpp (inside Assimp, used by its triangulation) | as in Assimp 6.0.5 | ISC | Assimp's `contrib/earcut-hpp` (Mapbox) |
| Open3DGC (inside Assimp, glTF compression) | as in Assimp 6.0.5 | MIT | Assimp's `contrib/Open3DGC` (Khaled Mammou, AMD) |
| openddlparser (inside Assimp, OpenGEX) | as in Assimp 6.0.5 | MIT | Assimp's `contrib/openddlparser` (Kim Kulling) |

Assimp is built by the vcpkg port with all importers and exporters except M3D, C4D (marked
non-free upstream), USD and VRML, which are off. The VRML importer is the one that would use
code from MeshLab (GPL); it is not built.

## Code of others built into the engine

| What | Where | License | Source |
|---|---|---|---|
| Sky and atmosphere model by Sébastien Hillaire ("A Scalable and Production Ready Sky and Atmosphere Rendering Technique", 2020), ported | `engine/src/render/shaders/common/atmosphere.hlsli`, `engine/src/render/sky/sky.cpp` | MIT, Copyright (c) 2020 Epic Games, Inc. (text in `atmosphere.hlsli`) | https://github.com/sebh/UnrealEngineSkyAtmosphere |
| XeGTAO (ambient occlusion), `XeGTAO.h` and `XeGTAO.hlsli` as they are; entry points and the denoise pass adapted from them and from `vaGTAO.hlsl` | `engine/src/render/shaders/third_party/xegtao/`, `engine/src/render/shaders/post/gtao_*` | MIT, Copyright (C) 2016-2021 Intel Corporation (text in `third_party/xegtao/LICENSE`) | https://github.com/GameTechDev/XeGTAO |
| AgX Minimal (tone mapping) by Benjamin Wrensch, ported to HLSL | `engine/src/render/shaders/post/agx.hlsli` | MIT, Copyright (c) 2024 Missing Deadlines (Benjamin Wrensch) (text in the file) | https://iolite-engine.com/blog_posts/minimal_agx_implementation |

Formulas and methods taken from papers and talks (for example the ACES curve fit by Krzysztof
Narkowicz, interleaved gradient noise by Jorge Jimenez, the light falloff window by Brian
Karis) are written in our own code and named where they are used.

## Tools used to build, not distributed

| Name | Version | License | Use |
|---|---|---|---|
| DirectX Shader Compiler (DXC) | vcpkg `directx-dxc` 2026-05-27 | University of Illinois/NCSA (LLVM release license) with notices for bundled parts | Compiles the HLSL shaders to DXIL and SPIR-V at build time; the shaders are embedded in the engine, so `dxcompiler.dll` is not needed by the programs and is not shipped. |
| vcpkg and its CMake helper ports | as bundled with Visual Studio 2022 Build Tools | MIT | Fetches and builds the libraries. |
| CMake, Ninja, MSVC | as installed | their own | Build. |

## Tools used to make assets, outside the repository's build

| Name | License | Use |
|---|---|---|
| EZ-Tree by Daniel Greenheck | MIT (https://github.com/dgreenheck/ez-tree) | Tree geometry for `tools/assets/make_tree.mjs`; installed outside the repository. |
| three.js | MIT (https://github.com/mrdoob/three.js) | Needed by EZ-Tree. |
| Blender | GPL (a separate program; nothing of it is linked or shipped) | `tools/assets/simplify_model.py` runs inside it to make simpler levels of detail. |

Assets (models, textures, terrain data) are not in this repository; their sources and licenses
are listed in `ATTRIBUTION.md` inside the asset archive.
