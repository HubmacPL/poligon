// XeGTAO's second pass (shaders/third_party/xegtao, MIT, Intel): the occlusion term and the
// edges the blur keeps, per pixel, normals worked out from depth in place. Slices and steps per
// slice must be known when compiling (the loops are unrolled): gtao_main_low.comp.hlsl and its
// siblings define GTAO_SLICES and GTAO_STEPS, as Intel's four quality levels. Entry point and
// noise modelled on Intel's
// vaGTAO.hlsl: a Hilbert curve driving the R2 sequence, moving with the frame under temporal
// anti-aliasing.
#define XE_GTAO_USE_HALF_FLOAT_PRECISION 0
#define XE_GTAO_GENERATE_NORMALS_INPLACE
// What Intel's vaShared.hlsl gives XeGTAO.hlsli (not taken: only this one macro is used).
#define VA_SATURATE saturate
#include "../third_party/xegtao/XeGTAO.h"

[[vk::binding(0, 2)]]
cbuffer GTAOConstantBuffer : register(b0, space2) {
    GTAOConstants g_GTAOConsts;
};

#include "../third_party/xegtao/XeGTAO.hlsli"

[[vk::combinedImageSampler]][[vk::binding(0, 0)]]
Texture2D<float> g_srcWorkingDepth : register(t0, space0);
[[vk::combinedImageSampler]][[vk::binding(0, 0)]]
SamplerState g_samplerPointClamp : register(s0, space0);

[[vk::binding(0, 1)]] RWTexture2D<uint> g_outWorkingAOTerm : register(u0, space1);
[[vk::binding(1, 1)]] RWTexture2D<unorm float> g_outWorkingEdges : register(u1, space1);

lpfloat2 SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex) {
    uint index = HilbertIndex(pixCoord.x, pixCoord.y);
    index += 288 * (temporalIndex % 64);
    return lpfloat2(frac(0.5 + index * float2(0.75487766624669276005, 0.5698402909980532659114)));
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void main(const uint2 pixCoord : SV_DispatchThreadID) {
    XeGTAO_MainPass(pixCoord, GTAO_SLICES, GTAO_STEPS, SpatioTemporalNoise(pixCoord, g_GTAOConsts.NoiseIndex), lpfloat3(0.0, 0.0, 0.0),
                    g_GTAOConsts, g_srcWorkingDepth, g_samplerPointClamp, g_outWorkingAOTerm, g_outWorkingEdges);
}
