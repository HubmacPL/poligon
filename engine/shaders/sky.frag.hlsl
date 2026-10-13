// Draws the sky behind everything: a gradient from the horizon to the zenith, the ground
// color below the horizon, and the sun with a soft glow around it.
#include "common.hlsli"

cbuffer SkyUniforms : register(b0, space3) {
    float4x4 inverse_view_projection;
    float4 camera_position;
    float4 light_direction;  // xyz: direction the light travels
    float4 sun_color;
    float4 sky_zenith;
    float4 sky_horizon;
    float4 sky_ground;
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

float4 main(FragmentInput input) : SV_Target0 {
    // Find the world direction this pixel looks in.
    float4 far_point = mul(inverse_view_projection, float4(input.ndc, 1.0, 1.0));
    float3 direction = normalize(far_point.xyz / far_point.w - camera_position.xyz);

    float3 color = SkyColor(direction, sky_zenith.rgb, sky_horizon.rgb, sky_ground.rgb);

    // The sun's disc is about half a degree wide; the glow fades over a much wider angle.
    float toward_sun = dot(direction, -light_direction.xyz);
    float disc = smoothstep(0.99985, 0.99995, toward_sun);
    float glow = pow(saturate(toward_sun), 400.0) * 0.6 + pow(saturate(toward_sun), 12.0) * 0.08;
    float above_horizon = saturate(direction.y * 20.0 + 0.5);
    color += sun_color.rgb * (disc * 20.0 + glow) * above_horizon;

    return float4(color, 1.0);
}
