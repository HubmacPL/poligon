#pragma once

#include <cstdint>
#include <vector>

namespace poligon {

// Tileable noise volumes for volumetric clouds, after Schneider's "The real-time volumetric
// cloudscapes of Horizon Zero Dawn" (2015): a "shape" volume whose red channel is Perlin noise
// sharpened by Worley (cell) noise and whose green, blue and alpha hold Worley noise of
// rising frequency, and a smaller "detail" volume of Worley noise that erodes the edges.
// Values are bytes, x fastest, then y, then z. The same arguments give the same bytes.

std::vector<std::uint8_t> MakeCloudShapeNoise(int size, std::uint32_t seed);   // size^3 RGBA
std::vector<std::uint8_t> MakeCloudDetailNoise(int size, std::uint32_t seed);  // size^3 RGBA
// A 2D weather map: red is how much of the sky has clouds there, green their type (0 flat
// layer clouds, 1 towering ones). size^2 RGBA, tileable.
std::vector<std::uint8_t> MakeCloudWeatherMap(int size, std::uint32_t seed);

} // namespace poligon
