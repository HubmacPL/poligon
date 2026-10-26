Intel's XeGTAO (https://github.com/GameTechDev/XeGTAO), files `XeGTAO.h` and `XeGTAO.hlsli`
from `Source/Rendering/Shaders`, taken as they are (MIT, see `LICENSE`). The engine's compute
shaders that call them are in `shaders/post/gtao_*.comp.hlsl`; the C++ side includes
`XeGTAO.h` for `GTAOUpdateConstants` (renderer/gtao.cpp).
