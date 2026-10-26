// mesh.frag.hlsl for the blend pass (passes/blend_pass.cpp): blended materials over the
// finished scene, which already has its fog. The shader tests the depth pass's image itself
// (the pass reads it, so it cannot be its depth target), and returns the lit color with the
// fog in front of the surface, and alpha for mixing it over the scene.
#define BLENDED
#include "mesh.frag.hlsl"
