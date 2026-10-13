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
