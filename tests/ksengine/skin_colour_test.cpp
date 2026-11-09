// Qt-free test for the skin colour rule (src/engine/Graphics/SkinColour.h):
// a Kunos skin's livery.png is the paint the car loads with, and its average
// must ignore the transparent surround instead of averaging it towards black.

#include "KsTest.h"
#include "engine/Graphics/SkinColour.h"

#include <cmath>

namespace {

bool near(float actual, float expected) {
    return std::fabs(actual - expected) < 1e-4f;
}

} // namespace

int main() {
    // Two opaque red pixels and one transparent black: the alpha skip keeps
    // the colour red instead of dragging it towards black.
    {
        const unsigned char pixels[3 * 4] = {
            200, 20, 20, 255,
            100, 0, 0, 255,
            0, 0, 0, 0,
        };
        float rgb[3] = {0.0f, 0.0f, 0.0f};
        KS_CHECK(ks::graphics::skinColour(pixels, 3, 1, rgb));
        KS_CHECK(near(rgb[0], 150.0f / 255.0f));
        KS_CHECK(near(rgb[1], 10.0f / 255.0f));
        KS_CHECK(near(rgb[2], 10.0f / 255.0f));
    }

    // Fully transparent: no colour, so the surface keeps the one it shipped.
    {
        const unsigned char pixels[4] = {255, 0, 0, 10};
        float rgb[3] = {0.0f, 0.0f, 0.0f};
        KS_CHECK(!ks::graphics::skinColour(pixels, 1, 1, rgb));
    }

    // Degenerate input is refused instead of read.
    {
        const unsigned char pixel[4] = {255, 0, 0, 255};
        float rgb[3] = {0.0f, 0.0f, 0.0f};
        KS_CHECK(!ks::graphics::skinColour(nullptr, 2, 2, rgb));
        KS_CHECK(!ks::graphics::skinColour(pixel, 0, 0, rgb));
    }

    return KS_TEST_RESULT("skin_colour_test");
}
