// The cloud pass: for every pixel of a smaller image (a quarter or half of the drawn size),
// walks the line of sight through the cloud layer where it sees the sky, adding the light
// the clouds scatter towards the camera and keeping track of how much of the sky behind
// still shows. Output: rgb the clouds' light, a the share of what is behind that still
// shows; laid over the scene by clouds_composite.frag.hlsl.
//
// Each step's light: sunlight dimmed by the cloud between the step and the sun (a few
// coarse steps), shaped by a phase function (bright looking towards the sun, a silver
// lining), with the "powder" darkening of thin edges facing the sun, plus sky light from
// above, stronger near the cloud's top. Every frame starts the steps at a different offset;
// temporal anti-aliasing averages that away.
#define CLOUDS_SHAPE_BINDING 1
#define CLOUDS_SHAPE_T t1
#define CLOUDS_SHAPE_S s1
#define CLOUDS_DETAIL_BINDING 2
#define CLOUDS_DETAIL_T t2
#define CLOUDS_DETAIL_S s2
#define CLOUDS_WEATHER_BINDING 3
#define CLOUDS_WEATHER_T t3
#define CLOUDS_WEATHER_S s3
#include "clouds.hlsli"

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float> depth_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState depth_sampler : register(s0, space2);

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

float HenyeyGreenstein(float cosine, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(1.0 + g2 - 2.0 * g * cosine, 0.0001), 1.5));
}

float InterleavedGradientNoise(float2 pixel) {
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

float4 main(FragmentInput input) : SV_Target0 {
    // Only where the scene shows the sky (depth 0) do clouds show; the land is far below them.
    // The depth image is at the drawn size; this image is smaller.
    if (depth_texture.SampleLevel(depth_sampler, input.uv, 0.0) > 0.0) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    float4 far_point = mul(inverse_view_projection, float4(input.ndc, 0.000001, 1.0));
    float3 direction = normalize(far_point.xyz / far_point.w - camera_position.xyz);
    float enter;
    float leave;
    if (!LayerSpan(camera_position.xyz, direction, enter, leave)) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    int steps = (int)quality.x;
    float step_length = (leave - enter) / steps;
    float offset = frac(InterleavedGradientNoise(input.clip_position.xy) + camera_position.w);
    float3 to_sun = -light_direction.xyz;
    float cosine = dot(direction, to_sun);
    float phase = lerp(HenyeyGreenstein(cosine, -0.25), HenyeyGreenstein(cosine, 0.75), 0.6) * 4.0 * 3.14159265;
    float3 light = float3(0.0, 0.0, 0.0);
    float transmittance = 1.0;
    [loop] for (int i = 0; i < steps; ++i) {
        float3 position = camera_position.xyz + direction * (enter + (i + offset) * step_length);
        float density = CloudDensity(position, true);
        if (density <= 0.0) {
            continue;
        }
        float height = saturate((position.y - layer.x) / max(layer.y - layer.x, 1.0));
        float sun = CloudTransmittanceTowardsSun(position, (int)quality.y);
        float powder = 1.0 - exp(-density * step_length * 2.0);
        float3 sunlight = light_color.rgb * sun * phase * lerp(1.0, powder * 2.0, 0.5);
        float3 skylight = lerp(sky_horizon.rgb * 0.3, sky_zenith.rgb * 0.75, height);
        float3 scattering = sunlight + skylight;
        // Energy-conserving integration of a step (Hillaire 2016).
        float step_transmittance = exp(-density * step_length);
        light += transmittance * scattering * (1.0 - step_transmittance);
        transmittance *= step_transmittance;
        if (transmittance < 0.01) {
            break;
        }
    }
    // Far towards the horizon the clouds fade into the haze.
    float haze = saturate(enter / quality.z);
    light = lerp(light, float3(0.0, 0.0, 0.0), haze);
    transmittance = lerp(transmittance, 1.0, haze);
    return float4(light, transmittance);
}
