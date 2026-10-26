# Rendering: what is still to be done

Work on the renderer that has been agreed but not done yet. It comes from the comparison
with Godot 4.7 (2026-10-17); points 1 to 6 of that comparison (cloud shadows, the sky light
map, soft shadows, clouds built over frames, motion vectors, fog in the sky's color) are
done and described in [rendering.md](rendering.md). Remove a point from here when it is done.

## 7. AgX tone mapping as an option

- What: a third choice of `PostSettings::tone_mapping` next to ACES and none. AgX (Troy
  Sobotka, used by Blender since 4.0 and by Godot since 4.2) keeps very bright colors from
  turning into flat yellow, cyan or magenta: a sunset or a fire keeps its hue as it bleaches
  to white. ACES stays the default.
- How: a curve fitted to AgX in the tone mapping shader (a few lines of HLSL), with a
  "punchy" look as an option for more contrast. Before writing it, check whether a fitted
  version with a permissive license exists (rule 1) and check its license at the source.
- Cost: well under 0.1 ms. Test: a bright saturated color keeps its hue under AgX, and the
  image test for the other tone mappings is unchanged.

## 8. XeGTAO instead of our ambient occlusion

- What: Intel's XeGTAO (ground-truth ambient occlusion, Jimenez 2016, as written by Intel)
  instead of the present ambient occlusion from depth. It gives occlusion that matches a
  reference much better (no dark halos around trunks, correct strength in corners) and a
  "bent normal" that can also aim the sky light.
- License: MIT, checked at the source on 2026-10-17
  (github.com/GameTechDev/XeGTAO, `LICENSE`: "MIT License, Copyright (C) 2016-2021, Intel
  Corporation"). Its HLSL is to be taken as it is, with the notice kept.
- How: its compute shaders (depth mip chain, the main pass, the denoiser) as SDL GPU
  compute pipelines; quality levels mapped onto the graphics presets in place of
  `ambient_occlusion_samples`. Check that it needs nothing the GTX 980 lacks (it targets
  shader model 5 and needs no wave operations by default).
- Cost: about 0.5 to 1 ms at 1080p on cards of that class (from memory, not checked); to be measured in the
  four views. Test: the present ambient occlusion tests (a corner is darker than an open
  floor) and an image of the inside view compared by eye once.

## 10. FSR 2 upscaling (later)

- What: AMD's FidelityFX Super Resolution 2 in place of our TAA's upscaling at render scales
  below 1: sharper detail and fewer ghosts at the same cost per pixel, which lets the low
  and medium presets draw fewer pixels.
- License: MIT, checked at the source on 2026-10-17
  (github.com/GPUOpen-Effects/FidelityFX-FSR2, `LICENSE.txt`). Only its HLSL shaders would be
  used, as SDL GPU compute pipelines, not its Direct3D 12 or Vulkan back ends.
- Needs: motion vectors (done, point 5), the depth image, a reactive mask for particles and
  water (to be written), the camera's sub-pixel shifts (done). Check before starting that
  its shaders build without 16-bit floats and wave operations, which the GTX 980 handles
  poorly or not at all, and how they map onto SDL GPU's binding rules.
- Cost: about 1 to 2 ms at 1080p on cards of that class (from memory, not checked); must still beat our TAA
  with upscaling at the same image quality, measured in the four views. Test: the TAA tests
  run with FSR 2 in its place.
