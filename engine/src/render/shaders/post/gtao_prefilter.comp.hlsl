// XeGTAO's first pass (shaders/third_party/xegtao, MIT, Intel): the depth pass's image turned
// into view-space depth with four smaller levels, 16 x 16 pixels per group of 8 x 8 threads.
// Entry point modelled on Intel's vaGTAO.hlsl.
#define XE_GTAO_USE_HALF_FLOAT_PRECISION 0
// What Intel's vaShared.hlsl gives XeGTAO.hlsli (not taken: only this one macro is used).
#define VA_SATURATE saturate
#include "../third_party/xegtao/XeGTAO.h"

[[vk::binding(0, 2)]]
cbuffer GTAOConstantBuffer : register(b0, space2) {
    GTAOConstants g_GTAOConsts;
};

#include "../third_party/xegtao/XeGTAO.hlsli"

[[vk::combinedImageSampler]][[vk::binding(0, 0)]]
Texture2D<float> g_srcRawDepth : register(t0, space0);
[[vk::combinedImageSampler]][[vk::binding(0, 0)]]
SamplerState g_samplerPointClamp : register(s0, space0);

[[vk::binding(0, 1)]] RWTexture2D<float> g_outWorkingDepthMIP0 : register(u0, space1);
[[vk::binding(1, 1)]] RWTexture2D<float> g_outWorkingDepthMIP1 : register(u1, space1);
[[vk::binding(2, 1)]] RWTexture2D<float> g_outWorkingDepthMIP2 : register(u2, space1);
[[vk::binding(3, 1)]] RWTexture2D<float> g_outWorkingDepthMIP3 : register(u3, space1);
[[vk::binding(4, 1)]] RWTexture2D<float> g_outWorkingDepthMIP4 : register(u4, space1);

[numthreads(8, 8, 1)]
void main(uint2 dispatchThreadID : SV_DispatchThreadID, uint2 groupThreadID : SV_GroupThreadID) {
    XeGTAO_PrefilterDepths16x16(dispatchThreadID, groupThreadID, g_GTAOConsts, g_srcRawDepth, g_samplerPointClamp, g_outWorkingDepthMIP0,
                                g_outWorkingDepthMIP1, g_outWorkingDepthMIP2, g_outWorkingDepthMIP3, g_outWorkingDepthMIP4);
}
