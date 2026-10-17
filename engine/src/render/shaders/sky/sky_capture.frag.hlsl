// Draws one face of the sky light map: the sky as seen from the camera in every direction,
// clouds included, for ambient light and reflections (lighting.hlsli). The sun's disc is
// left out: surfaces get the sun's light and highlight directly.
#define CLOUDS_SHAPE_BINDING 0
#define CLOUDS_SHAPE_T t0
#define CLOUDS_SHAPE_S s0
#define CLOUDS_DETAIL_BINDING 1
#define CLOUDS_DETAIL_T t1
#define CLOUDS_DETAIL_S s1
#define CLOUDS_WEATHER_BINDING 2
#define CLOUDS_WEATHER_T t2
#define CLOUDS_WEATHER_S s2
#include "common.hlsli"
#include "clouds.hlsli"

cbuffer SkyCaptureUniforms : register(b1, space3) {
    float4 capture;      // x: face 0..5, y: 1 = clouds, z: steps through the clouds, w: steps towards the sun
    float4 zenith;       // the sky's colors as light (times the sky light's strength)
    float4 horizon;
    float4 ground;
    float4 eye;          // xyz: where the sky is seen from
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

float4 main(FragmentInput input) : SV_Target0 {
    float3 direction = CubeFaceDirection((int)capture.x, input.uv);
    float3 color = SkyColor(direction, zenith.rgb, horizon.rgb, ground.rgb);
    if (capture.y > 0.5) {
        float enter;
        float leave;
        if (LayerSpan(eye.xyz, direction, enter, leave)) {
            float4 clouds = MarchClouds(eye.xyz, direction, enter, leave, (int)capture.z, (int)capture.w, 0.5);
            color = color * clouds.a + clouds.rgb;
        }
    }
    return float4(color, 1.0);
}
