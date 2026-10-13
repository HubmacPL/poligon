// Shared by several shaders. Included with #include "common.hlsli".

// Color of the sky in a given direction: zenith straight up, horizon at eye level, ground below.
// The same function draws the sky and lights objects from it, so the two always agree.
float3 SkyColor(float3 direction, float3 zenith, float3 horizon, float3 ground) {
    float height = direction.y;
    if (height >= 0.0) {
        // The sky pales quickly towards the horizon, hence the square root.
        return lerp(horizon, zenith, sqrt(saturate(height)));
    }
    return lerp(horizon, ground, saturate(-height * 5.0));
}

// Distance in front of the camera (meters) for a value from the depth image.
// projection_info: x = projection[2][2], y = projection[3][2], z = 1 / projection[0][0],
// w = 1 / projection[1][1] of the camera's projection matrix.
float ViewDistance(float depth, float4 projection_info) {
    return projection_info.y / (depth + projection_info.x);
}

// Position relative to the camera (x right, y up, -z forward) of the point seen at `uv`
// ((0, 0) top-left) with the given depth image value.
float3 ViewPosition(float2 uv, float depth, float4 projection_info) {
    float distance = ViewDistance(depth, projection_info);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    return float3(ndc * projection_info.zw * distance, -distance);
}

// A different value in 0..1 for every pixel, evenly spread over neighbors (Jorge Jimenez).
// Used to turn the banding of low sample counts into fine noise that a blur then removes.
float InterleavedGradientNoise(float2 pixel) {
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}
