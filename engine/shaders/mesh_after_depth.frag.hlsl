// mesh.frag.hlsl for the scene pass when the depth pass has already drawn the scene's
// depth: the depth test runs before the shader ([earlydepthstencil]), so only the surface a
// pixel finally shows is lit, instead of every leaf behind it as well.
#define EARLY_DEPTH_TEST
#include "mesh.frag.hlsl"
