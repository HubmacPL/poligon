// XeGTAO's third pass (shaders/third_party/xegtao, MIT, Intel): an edge-aware blur of the
// occlusion term, two horizontal pixels per thread; the last pass also scales it back from its
// working range. Entry point modelled on Intel's vaGTAO.hlsl.
//
// XeGTAO_DenoiseLoadedTerm below is Intel's XeGTAO_Denoise copied from XeGTAO.hlsli (Copyright
// (C) 2016-2021, Intel Corporation, SPDX-License-Identifier: MIT; license text in
// third_party/xegtao/LICENSE) with one change: the occlusion term is read with four Loads in
// place of GatherRed (GatherTerm), because SDL GPU does not let an unsigned integer image be
// sampled; the edges keep their GatherRed, with a sampler of their own.
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
Texture2D<float> g_srcWorkingEdges : register(t0, space0);
[[vk::combinedImageSampler]][[vk::binding(0, 0)]]
SamplerState g_edgesSampler : register(s0, space0);

// The occlusion term is an unsigned integer image, which SDL GPU does not let be sampled: it is
// bound as a read-only storage image (a storage image in SPIR-V, a shader resource in DXIL)
// and read with Load.
#ifdef __spirv__
[[vk::binding(1, 0)]]
RWTexture2D<uint> g_srcWorkingAOTerm : register(u0, space0);
uint LoadTerm(int2 pixel) { return g_srcWorkingAOTerm[pixel]; }
#else
Texture2D<uint> g_srcWorkingAOTerm : register(t1, space0);
uint LoadTerm(int2 pixel) { return g_srcWorkingAOTerm.Load(int3(pixel, 0)); }
#endif

// What GatherRed returns at the top-left corner of `pixel` (moved by `offset`), clamped at the
// image's edges: x bottom-left, y bottom-right, z top-right, w top-left of the 2 x 2 texels.
uint4 GatherTerm(int2 pixel, int2 offset, int2 size) {
    int2 corner = pixel + offset;
    int2 low = clamp(corner - 1, int2(0, 0), size - 1);
    int2 high = clamp(corner, int2(0, 0), size - 1);
    return uint4(LoadTerm(int2(low.x, high.y)), LoadTerm(high), LoadTerm(int2(high.x, low.y)), LoadTerm(low));
}

[[vk::binding(0, 1)]]
RWTexture2D<uint> g_outFinalAOTerm : register(u0, space1);

void XeGTAO_DenoiseLoadedTerm( const uint2 pixCoordBase, const GTAOConstants consts, Texture2D<lpfloat> sourceEdges, SamplerState edgesSampler, RWTexture2D<uint> outputTexture, const uniform bool finalApply )
{
    const lpfloat blurAmount = (finalApply)?((lpfloat)consts.DenoiseBlurBeta):((lpfloat)consts.DenoiseBlurBeta/(lpfloat)5.0);
    const lpfloat diagWeight = 0.85 * 0.5;

    AOTermType aoTerm[2];   // pixel pixCoordBase and pixel pixCoordBase + int2( 1, 0 )
    lpfloat4 edgesC_LRTB[2];
    lpfloat weightTL[2];
    lpfloat weightTR[2];
    lpfloat weightBL[2];
    lpfloat weightBR[2];

    // gather edge and visibility quads, used later
    const float2 gatherCenter = float2( pixCoordBase.x, pixCoordBase.y ) * consts.ViewportPixelSize;
    lpfloat4 edgesQ0        = sourceEdges.GatherRed( edgesSampler, gatherCenter, int2( 0, 0 ) );
    lpfloat4 edgesQ1        = sourceEdges.GatherRed( edgesSampler, gatherCenter, int2( 2, 0 ) );
    lpfloat4 edgesQ2        = sourceEdges.GatherRed( edgesSampler, gatherCenter, int2( 1, 2 ) );

    AOTermType visQ0[4];    XeGTAO_DecodeGatherPartial( GatherTerm( int2( pixCoordBase ), int2( 0, 0 ), consts.ViewportSize ), visQ0 );
    AOTermType visQ1[4];    XeGTAO_DecodeGatherPartial( GatherTerm( int2( pixCoordBase ), int2( 2, 0 ), consts.ViewportSize ), visQ1 );
    AOTermType visQ2[4];    XeGTAO_DecodeGatherPartial( GatherTerm( int2( pixCoordBase ), int2( 0, 2 ), consts.ViewportSize ), visQ2 );
    AOTermType visQ3[4];    XeGTAO_DecodeGatherPartial( GatherTerm( int2( pixCoordBase ), int2( 2, 2 ), consts.ViewportSize ), visQ3 );

    for( int side = 0; side < 2; side++ )
    {
        const int2 pixCoord = int2( pixCoordBase.x + side, pixCoordBase.y );

        lpfloat4 edgesL_LRTB  = XeGTAO_UnpackEdges( (side==0)?(edgesQ0.x):(edgesQ0.y) );
        lpfloat4 edgesT_LRTB  = XeGTAO_UnpackEdges( (side==0)?(edgesQ0.z):(edgesQ1.w) );
        lpfloat4 edgesR_LRTB  = XeGTAO_UnpackEdges( (side==0)?(edgesQ1.x):(edgesQ1.y) );
        lpfloat4 edgesB_LRTB  = XeGTAO_UnpackEdges( (side==0)?(edgesQ2.w):(edgesQ2.z) );

        edgesC_LRTB[side]     = XeGTAO_UnpackEdges( (side==0)?(edgesQ0.y):(edgesQ1.x) );

        // Edges aren't perfectly symmetrical: edge detection algorithm does not guarantee that a left edge on the right pixel will match the right edge on the left pixel (although
        // they will match in majority of cases). This line further enforces the symmetricity, creating a slightly sharper blur. Works real nice with TAA.
        edgesC_LRTB[side] *= lpfloat4( edgesL_LRTB.y, edgesR_LRTB.x, edgesT_LRTB.w, edgesB_LRTB.z );

#if 1   // this allows some small amount of AO leaking from neighbours if there are 3 or 4 edges; this reduces both spatial and temporal aliasing
        const lpfloat leak_threshold = 2.5; const lpfloat leak_strength = 0.5;
        lpfloat edginess = (saturate(4.0 - leak_threshold - dot( edgesC_LRTB[side], 1.xxxx )) / (4-leak_threshold)) * leak_strength;
        edgesC_LRTB[side] = saturate( edgesC_LRTB[side] + edginess );
#endif

#ifdef XE_GTAO_SHOW_EDGES
        g_outputDbgImage[pixCoord] = 1.0 - lpfloat4( edgesC_LRTB[side].x, edgesC_LRTB[side].y * 0.5 + edgesC_LRTB[side].w * 0.5, edgesC_LRTB[side].z, 1.0 );
        //g_outputDbgImage[pixCoord] = 1 - float4( edgesC_LRTB[side].z, edgesC_LRTB[side].w , 1, 0 );
        //g_outputDbgImage[pixCoord] = edginess.xxxx;
#endif

        // for diagonals; used by first and second pass
        weightTL[side] = diagWeight * (edgesC_LRTB[side].x * edgesL_LRTB.z + edgesC_LRTB[side].z * edgesT_LRTB.x);
        weightTR[side] = diagWeight * (edgesC_LRTB[side].z * edgesT_LRTB.y + edgesC_LRTB[side].y * edgesR_LRTB.z);
        weightBL[side] = diagWeight * (edgesC_LRTB[side].w * edgesB_LRTB.x + edgesC_LRTB[side].x * edgesL_LRTB.w);
        weightBR[side] = diagWeight * (edgesC_LRTB[side].y * edgesR_LRTB.w + edgesC_LRTB[side].w * edgesB_LRTB.y);

        // first pass
        AOTermType ssaoValue     = (side==0)?(visQ0[1]):(visQ1[0]);
        AOTermType ssaoValueL    = (side==0)?(visQ0[0]):(visQ0[1]);
        AOTermType ssaoValueT    = (side==0)?(visQ0[2]):(visQ1[3]);
        AOTermType ssaoValueR    = (side==0)?(visQ1[0]):(visQ1[1]);
        AOTermType ssaoValueB    = (side==0)?(visQ2[2]):(visQ3[3]);
        AOTermType ssaoValueTL   = (side==0)?(visQ0[3]):(visQ0[2]);
        AOTermType ssaoValueBR   = (side==0)?(visQ3[3]):(visQ3[2]);
        AOTermType ssaoValueTR   = (side==0)?(visQ1[3]):(visQ1[2]);
        AOTermType ssaoValueBL   = (side==0)?(visQ2[3]):(visQ2[2]);

        lpfloat sumWeight = blurAmount;
        AOTermType sum = ssaoValue * sumWeight;

        XeGTAO_AddSample( ssaoValueL, edgesC_LRTB[side].x, sum, sumWeight );
        XeGTAO_AddSample( ssaoValueR, edgesC_LRTB[side].y, sum, sumWeight );
        XeGTAO_AddSample( ssaoValueT, edgesC_LRTB[side].z, sum, sumWeight );
        XeGTAO_AddSample( ssaoValueB, edgesC_LRTB[side].w, sum, sumWeight );

        XeGTAO_AddSample( ssaoValueTL, weightTL[side], sum, sumWeight );
        XeGTAO_AddSample( ssaoValueTR, weightTR[side], sum, sumWeight );
        XeGTAO_AddSample( ssaoValueBL, weightBL[side], sum, sumWeight );
        XeGTAO_AddSample( ssaoValueBR, weightBR[side], sum, sumWeight );

        aoTerm[side] = sum / sumWeight;

        XeGTAO_Output( pixCoord, outputTexture, aoTerm[side], finalApply );

#ifdef XE_GTAO_SHOW_BENT_NORMALS
        if( finalApply )
        {
            g_outputDbgImage[pixCoord] = float4( DisplayNormalSRGB( aoTerm[side].xyz /** aoTerm[side].www*/ ), 1 );
        }
#endif

    }
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void main(const uint2 dispatchThreadID : SV_DispatchThreadID) {
    const uint2 pixCoordBase = dispatchThreadID * uint2(2, 1);
    XeGTAO_DenoiseLoadedTerm(pixCoordBase, g_GTAOConsts, g_srcWorkingEdges, g_edgesSampler, g_outFinalAOTerm, true);
}
