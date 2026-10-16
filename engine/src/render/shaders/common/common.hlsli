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

// Distance in front of the camera (meters) for a value from the depth image. The camera's
// depth runs from 1 at the near plane to 0 far away (reversed depth); the formula holds for
// either direction, since it only uses the projection matrix.
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

// Fading between two levels of detail: while a plant changes level, both levels are drawn
// and each keeps the pixels the other drops, by a fixed per-pixel threshold ("dithered
// fade"). `fade` comes with each instance: 1 = drawn whole; between 0 and 1 = fading in,
// that share of pixels kept; between -1 and 0 = fading out, that share of pixels dropped.
bool KeptByFade(float fade, float2 pixel) {
    if (fade >= 1.0) {
        return true;
    }
    float threshold = InterleavedGradientNoise(pixel);
    return fade > 0.0 ? threshold < fade : threshold >= -fade;
}

// Wind (Environment::wind_*, Material::wind_*). `wind`: xy = the direction it blows towards
// (world x and z), z = strength, w = time in seconds. `response`: x = how much the plant
// bends, y = how much its leaves flutter. `foot` is the plant's base (the model's origin),
// `local` the vertex in the model's own coordinates. Returns the moved world position.
float3 ApplyWind(float3 world_position, float3 foot, float3 local, float4 wind, float2 response) {
    if (wind.z <= 0.0 || (response.x <= 0.0 && response.y <= 0.0)) {
        return world_position;  // no wind, or a material that does not move (rock, bark of a stump)
    }
    float2 direction = wind.xy;
    float height = max(world_position.y - foot.y, 0.0);
    // Gusts travel over the land as two waves; every plant starts them at a slightly
    // different moment, so that a forest does not move as one.
    float phase = frac(sin(dot(foot.xz, float2(12.9898, 78.233))) * 43758.5453) * 6.2832;
    float along = dot(foot.xz, direction);
    float gust = (0.5 + 0.5 * sin(along * 0.08 - wind.w * 1.2 + phase)) *
                 (0.7 + 0.3 * sin(along * 0.31 - wind.w * 2.7 + phase * 2.0));
    // Bending: the whole plant leans downwind, the more the higher up a point is.
    float bend = response.x * wind.z * gust * height * 0.03;
    float3 moved = world_position + float3(direction.x, 0.0, direction.y) * bend;
    // A bent stem does not get longer: the top comes down a little.
    moved.y -= 0.5 * bend * bend / max(height, 0.5);
    // Flutter: leaves and blades shiver each on their own, more towards the tips. Bark and
    // trunks (flutter 0) skip the three extra sines.
    if (response.y > 0.0) {
        float seed = dot(local, float3(7.13, 3.71, 5.29));
        float amplitude = response.y * wind.z * (0.3 + 0.7 * gust) * 0.04 * saturate(height * 2.0);
        moved += float3(sin(wind.w * 9.0 + seed), 0.5 * sin(wind.w * 7.3 + seed * 1.7), cos(wind.w * 8.1 + seed * 2.3)) * amplitude;
    }
    return moved;
}

// Linear color to the sRGB encoding 8-bit color images use.
float3 LinearToSrgb(float3 color) {
    float3 low = color * 12.92;
    float3 high = 1.055 * pow(max(color, 0.0), 1.0 / 2.4) - 0.055;
    return lerp(high, low, step(color, 0.0031308));
}

// A normal map sample (`map`, already turned from 0..1 into -1..1) applied to a surface
// normal, in world space. The tangent directions are derived from how position and texture
// coordinates change across the pixel, so meshes need no stored tangents.
float3 ApplyNormalMapSample(float3 map, float3 normal, float3 world_position, float2 uv) {
    // Normal maps follow the OpenGL convention: red tilts the surface along u, green towards
    // the top of the image. The frame built below comes out mirrored in our screen space
    // (y runs down the screen), and v runs down the image; together that means the red
    // direction must be negated and the green one kept. tests/render/scene_render_test.cpp checks both.
    map.x = -map.x;

    float3 dp1 = ddx(world_position);
    float3 dp2 = ddy(world_position);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);
    float3 dp2_perp = cross(dp2, normal);
    float3 dp1_perp = cross(normal, dp1);
    float3 tangent = dp2_perp * duv1.x + dp1_perp * duv2.x;
    float3 bitangent = dp2_perp * duv1.y + dp1_perp * duv2.y;
    float scale = max(dot(tangent, tangent), dot(bitangent, bitangent));
    if (scale <= 0.0) {
        return normal;
    }
    float inverse_length = rsqrt(scale);
    return normalize(tangent * (map.x * inverse_length) + bitangent * (map.y * inverse_length) + normal * map.z);
}
