// The cloud shadow image: for each texel, a point of the land below (the square of
// shadow_area around the camera, at the camera's height), the share of sunlight that gets
// through the clouds above it. Surfaces and fog read it (lighting.hlsli) to darken where a
// cloud's shadow falls.
#define CLOUDS_SHAPE_BINDING 0
#define CLOUDS_SHAPE_T t0
#define CLOUDS_SHAPE_S s0
#define CLOUDS_DETAIL_BINDING 1
#define CLOUDS_DETAIL_T t1
#define CLOUDS_DETAIL_S s1
#define CLOUDS_WEATHER_BINDING 2
#define CLOUDS_WEATHER_T t2
#define CLOUDS_WEATHER_S s2
#include "clouds.hlsli"

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

float main(FragmentInput input) : SV_Target0 {
    float2 place = shadow_area.xy + input.uv * shadow_area.z;
    return CloudShadowTowardsSun(float3(place.x, camera_position.y, place.y), 16);
}
