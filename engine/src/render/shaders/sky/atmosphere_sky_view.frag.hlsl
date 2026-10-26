// Builds the sky view table (atmosphere.hlsli) for this frame's sun: for every direction from
// the viewer, by its angle from straight up (rows) and from the sun's bearing (columns), the
// light the air scatters towards the viewer, for a sun of illuminance 1. 192 x 108 pixels.
#define ATMOSPHERE_TRANSMITTANCE_BINDING 0
#define ATMOSPHERE_TRANSMITTANCE_T t0
#define ATMOSPHERE_TRANSMITTANCE_S s0
#define ATMOSPHERE_MULTIPLE_BINDING 1
#define ATMOSPHERE_MULTIPLE_T t1
#define ATMOSPHERE_MULTIPLE_S s1
#include "atmosphere.hlsli"

cbuffer SkyViewUniforms : register(b0, space3) {
    float4 to_sun;  // xyz: the way to the sun (world, y up)
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

float4 main(FragmentInput input) : SV_Target0 {
    float2 uv = input.clip_position.xy / kSkyViewSize;
    float cos_zenith;
    float cos_light_view;
    SkyViewParams(uv, kViewRadius, cos_zenith, cos_light_view);
    // The sun turned into the table's frame: in the plane x-y, its height kept.
    float sun_cos_zenith = to_sun.y;
    float3 sun = normalize(float3(sqrt(saturate(1.0 - sun_cos_zenith * sun_cos_zenith)), sun_cos_zenith, 0.0));
    float sin_zenith = sqrt(saturate(1.0 - cos_zenith * cos_zenith));
    float3 direction = float3(sin_zenith * cos_light_view, cos_zenith, sin_zenith * sqrt(saturate(1.0 - cos_light_view * cos_light_view)));
    return float4(IntegrateSky(float3(0.0, kViewRadius, 0.0), direction, sun), 1.0);
}
