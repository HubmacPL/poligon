# Rendering: what is still to be done

Work on the renderer that has been agreed but not done yet. Points 1 to 6 of the comparison
with Godot 4.7 (2026-10-17) and, from package 5 (2026-10-26), AgX (7), XeGTAO (8) and
leaving out what hills hide (9) are done and described in [rendering.md](rendering.md) and
[terrain.md](terrain.md). Remove a point from here when it is done.

## 10. FSR 2 upscaling (stopped on 2026-10-26: does not fit SDL GPU's bindings as it is)

- What: AMD's FidelityFX Super Resolution 2 in place of our TAA's upscaling at render scales
  below 1: sharper detail and fewer ghosts at the same cost per pixel, which lets the low
  and medium presets draw fewer pixels.
- License: MIT, checked at the source again on 2026-10-26
  (github.com/GPUOpen-Effects/FidelityFX-FSR2, `LICENSE.txt`, Copyright (c) 2022-2023 Advanced
  Micro Devices).
- Checked on 2026-10-26: its passes build with DXC for DXIL and SPIR-V without 16-bit floats
  (`FFX_HALF` 0) and without wave operations (`SPD_NO_WAVE_OPERATIONS`), so the GTX 980 is not
  the problem. SDL GPU's bindings are: its integration layer (`ffx_fsr2_callbacks_hlsl.h`)
  samples about a dozen textures with two shared samplers (`s_PointClamp`, `s_LinearClamp`),
  while SDL GPU's Vulkan backend binds every sampled texture together with its own sampler,
  and DXC cannot make SPIR-V that shares one sampler between such textures (seen with
  XeGTAO's blur, `rendering.md`); its registers (`t`, `s`, `u`, `b` in one space) would also
  have to be moved into SDL GPU's sets. Its passes themselves would not change.
- What it would take (hours of our sessions): an integration layer of our own in place of
  `ffx_fsr2_callbacks_hlsl.h` (a sampler for each sampled texture, SDL GPU's registers and
  sets for each of its passes), 3 to 4; the 7 passes as compute pipelines with their images,
  histories, constants and the two lookup tables its C++ side makes (also MIT), 4 to 6; a
  reactive mask written by particles and water, 1 to 2; motion vectors and jitter in its
  convention, 1; tests (the TAA tests with FSR 2 in its place), pictures of the four views in
  medium and measurements, 2 to 3. About 11 to 16 in all. Cost per frame, from memory and
  unchecked: about 1 to 2 ms at 1080p on cards of that class; it must beat our TAA with
  upscaling at the same cost, which is not certain on this card.
