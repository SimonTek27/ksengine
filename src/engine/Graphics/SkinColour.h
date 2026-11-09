#pragma once
#include <cstddef>

namespace ks::graphics {

/**
 * Paint colour carried by a skin's livery.png.
 *
 * Assetto Corsa keeps a skin's body colour in that swatch, and Kunos cars such
 * as the Countach ship it instead of a body texture: the texture inside the
 * KN5 is a neutral paint map, so the skin's colour is what paints the car.
 * The average of the opaque pixels is that colour; pixels below half alpha
 * (the transparent surround of many swatches) would drag it towards black and
 * are skipped. Returns false for an empty or fully transparent image, which
 * leaves the surface with the colour it was authored with.
 */
inline bool skinColour(const unsigned char* rgba, int width, int height, float out[3])
{
    if (rgba == nullptr || width <= 0 || height <= 0) return false;
    double sum[3] = {0.0, 0.0, 0.0};
    long long counted = 0;
    const std::size_t pixels =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < pixels; ++i) {
        const unsigned char* p = rgba + i * 4u;
        if (p[3] < 128) continue;
        sum[0] += p[0];
        sum[1] += p[1];
        sum[2] += p[2];
        ++counted;
    }
    if (counted == 0) return false;
    for (int c = 0; c < 3; ++c)
        out[c] = static_cast<float>(sum[c] / (static_cast<double>(counted) * 255.0));
    return true;
}

} // namespace ks::graphics
