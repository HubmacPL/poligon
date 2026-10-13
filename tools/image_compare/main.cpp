// Compares two images pixel by pixel, with a tolerance.
//
// Usage: image_compare A.png B.png [--channel-tolerance N] [--max-different-percent P]
//
// A pixel counts as different when any of its R, G, B, A values differs by more than
// --channel-tolerance (default 2, on the 0..255 scale). The images match when at most
// --max-different-percent of the pixels are different (default 0.1).
//
// Exit codes: 0 = match, 1 = no match, 2 = bad arguments or unreadable file.
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>

namespace {

bool ParseDouble(const char* text, double& value) {
    const char* end = text + std::strlen(text);
    const std::from_chars_result parsed = std::from_chars(text, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

int PrintUsage() {
    std::fprintf(stderr,
                 "usage: image_compare A.png B.png [--channel-tolerance N] [--max-different-percent P]\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        return PrintUsage();
    }
    const char* first_path = argv[1];
    const char* second_path = argv[2];
    double channel_tolerance = 2.0;
    double max_different_percent = 0.1;

    for (int i = 3; i < argc; ++i) {
        double* target = nullptr;
        if (std::strcmp(argv[i], "--channel-tolerance") == 0) {
            target = &channel_tolerance;
        } else if (std::strcmp(argv[i], "--max-different-percent") == 0) {
            target = &max_different_percent;
        }
        if (target == nullptr || i + 1 >= argc || !ParseDouble(argv[i + 1], *target) || *target < 0.0) {
            return PrintUsage();
        }
        ++i;
    }

    int first_width = 0;
    int first_height = 0;
    int second_width = 0;
    int second_height = 0;
    int channels = 0;
    unsigned char* first = stbi_load(first_path, &first_width, &first_height, &channels, 4);
    if (first == nullptr) {
        std::fprintf(stderr, "image_compare: cannot read %s\n", first_path);
        return 2;
    }
    unsigned char* second = stbi_load(second_path, &second_width, &second_height, &channels, 4);
    if (second == nullptr) {
        std::fprintf(stderr, "image_compare: cannot read %s\n", second_path);
        stbi_image_free(first);
        return 2;
    }

    if (first_width != second_width || first_height != second_height) {
        std::printf("NO MATCH: sizes differ, %dx%d and %dx%d\n", first_width, first_height, second_width,
                    second_height);
        stbi_image_free(first);
        stbi_image_free(second);
        return 1;
    }

    const long long pixel_count = static_cast<long long>(first_width) * first_height;
    long long different_pixels = 0;
    int largest_difference = 0;
    for (long long pixel = 0; pixel < pixel_count; ++pixel) {
        int pixel_difference = 0;
        for (int channel = 0; channel < 4; ++channel) {
            const int difference = std::abs(first[pixel * 4 + channel] - second[pixel * 4 + channel]);
            if (difference > pixel_difference) {
                pixel_difference = difference;
            }
        }
        if (pixel_difference > largest_difference) {
            largest_difference = pixel_difference;
        }
        if (pixel_difference > channel_tolerance) {
            ++different_pixels;
        }
    }
    stbi_image_free(first);
    stbi_image_free(second);

    const double different_percent = 100.0 * static_cast<double>(different_pixels) / static_cast<double>(pixel_count);
    const bool match = different_percent <= max_different_percent;
    std::printf("%s: %dx%d, largest channel difference %d, pixels over tolerance %lld (%.4f%%), "
                "limits: tolerance %g, %g%%\n",
                match ? "MATCH" : "NO MATCH", first_width, first_height, largest_difference, different_pixels,
                different_percent, channel_tolerance, max_different_percent);
    return match ? 0 : 1;
}
