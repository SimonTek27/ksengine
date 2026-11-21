// Named-region visual regression (src/simulator/VisualRegions.h): the maths on
// synthetic buffers, the file format round trip, and the PNG path a capture
// takes — all without a GPU, a window or content.
//
//   visual_regions_test                                     this self-test
//   visual_regions_test --check <capture> <golden> <regions>   compare one frame
//   visual_regions_test --record <capture> <golden>            accept today's frame
//
// The self-test runs under CTest; the other two modes are what the capture
// runner (tools/visual_baseline.ps1) calls after ksim has written its PNG.
#include "KsTest.h"

#include "VisualRegions.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using ks::sim::regions::checkFiles;
using ks::sim::regions::compareImages;
using ks::sim::regions::parseRegions;
using ks::sim::regions::recordFile;
using ks::sim::regions::RegionStats;
using ks::sim::regions::regionsText;
using ks::sim::regions::regionStats;
using ks::sim::regions::VisualRegion;

namespace {

/** An RGBA frame of one colour. */
std::vector<unsigned char> solidFrame(int w, int h, unsigned char r, unsigned char g,
                                      unsigned char b) {
    std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < px.size(); i += 4) {
        px[i] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = 255;
    }
    return px;
}

int selfTest() {
    // --- the same frame twice: every region is zero and passes -------------
    {
        const auto golden = solidFrame(64, 48, 200, 100, 50);
        const auto current = solidFrame(64, 48, 200, 100, 50);
        const RegionStats stats =
            regionStats("wall", 4, 4, 32, 24, golden.data(), current.data(), 64, 48);
        KS_CHECK(stats.inside);
        KS_CHECK(stats.pixels == 32 * 24);
        KS_CHECK_NEAR(stats.mean, 0.0, 1e-6);
        KS_CHECK_NEAR(stats.median, 0.0, 1e-6);
        KS_CHECK_NEAR(stats.worst, 0.0, 1e-6);
    }

    // --- a uniform shift: mean and median agree, in |dRGB| units ------------
    {
        const auto golden = solidFrame(40, 30, 100, 100, 100);
        auto current = solidFrame(40, 30, 102, 98, 103);
        const RegionStats stats =
            regionStats("floor", 0, 0, 40, 30, golden.data(), current.data(), 40, 30);
        KS_CHECK(stats.inside);
        KS_CHECK_NEAR(stats.mean, 2.0 + 2.0 + 3.0, 1e-4);
        KS_CHECK_NEAR(stats.median, 7.0, 1e-4);
        KS_CHECK_NEAR(stats.worst, 7.0, 1e-4);
    }

    // --- one hot pixel: the worst names it, the median does not see it -----
    {
        auto golden = solidFrame(16, 16, 50, 50, 50);
        auto current = solidFrame(16, 16, 50, 50, 50);
        current[3 * 4 + 0] = 110; // +60 red on one pixel
        current[3 * 4 + 1] = 80;  // +30 green on the same pixel
        const RegionStats stats =
            regionStats("car", 0, 0, 16, 16, golden.data(), current.data(), 16, 16);
        KS_CHECK_NEAR(stats.worst, 90.0, 1e-4);
        KS_CHECK_NEAR(stats.median, 0.0, 1e-6);
        KS_CHECK_NEAR(stats.mean, 90.0 / 256.0, 1e-6);
        // ... and a threshold below the spike fails the region that holds it.
        const VisualRegion tight{"car", 0, 0, 16, 16, 1000.0f, 89.0f};
        std::vector<RegionStats> out;
        std::string error;
        KS_CHECK(compareImages(golden.data(), current.data(), 16, 16, {tight}, out, &error));
        KS_CHECK(out.size() == 1 && out[0].name == "car" && !out[0].pass);
        const VisualRegion loose{"car", 0, 0, 16, 16, 1000.0f, 90.0f};
        KS_CHECK(compareImages(golden.data(), current.data(), 16, 16, {loose}, out, &error));
        KS_CHECK(out.size() == 1 && out[0].pass);
    }

    // --- a region outside the frame is reported, not read past --------------
    {
        const auto golden = solidFrame(32, 24, 10, 20, 30);
        const auto current = solidFrame(32, 24, 10, 20, 30);
        const RegionStats stats =
            regionStats("gone", 100, 100, 50, 50, golden.data(), current.data(), 32, 24);
        KS_CHECK(!stats.inside);
        KS_CHECK(stats.pixels == 0);
        const RegionStats clipped =
            regionStats("edge", 24, 20, 50, 50, golden.data(), current.data(), 32, 24);
        KS_CHECK(clipped.inside);
        KS_CHECK(clipped.pixels == 8 * 4);
    }

    // --- no pixels is no comparison, not a crash -----------------------------
    {
        std::vector<RegionStats> out;
        std::string error;
        KS_CHECK(!compareImages(nullptr, nullptr, 0, 0, {}, out, &error));
        KS_CHECK(!error.empty());
    }

    // --- the regions file round trip ----------------------------------------
    {
        const std::vector<VisualRegion> written{{"wall", 200, 380, 400, 140, 3.0f, 120.0f},
                                                {"car", 1150, 750, 250, 130, 5.0f, 150.0f}};
        const std::string text =
            "# the showroom shot\n" + regionsText(written) + "# trailing comment\n";
        std::vector<VisualRegion> read;
        std::string error;
        KS_CHECK(parseRegions(text, read, &error));
        KS_CHECK(read.size() == 2);
        KS_CHECK(read[0].name == "wall" && read[0].x == 200 && read[0].y == 380);
        KS_CHECK(read[0].w == 400 && read[0].h == 140);
        KS_CHECK_NEAR(read[0].maxMean, 3.0, 1e-6);
        KS_CHECK_NEAR(read[0].maxWorst, 120.0, 1e-6);
        KS_CHECK(read[1].name == "car");
        // Garbage is refused, not silently turned into no regions.
        KS_CHECK(!parseRegions("hello", read, &error));
        KS_CHECK(!parseRegions("ksregions 1\n", read, &error));
        KS_CHECK(!parseRegions("ksregions 1\nregion bad 0 0 0 10 1 2\n", read, &error));
    }

    std::fprintf(stderr, "regions self-test: files\n");
    // --- the PNG path a capture takes: encode, load back, compare at zero ---
    {
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / "ks_visual_regions_test";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        ks::image::RawImage image;
        image.resize(16, 12);
        for (int y = 0; y < 12; ++y) {
            for (int x = 0; x < 16; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * 16 + x) * 4;
                image.rgba[i] = static_cast<std::uint8_t>(x * 16);
                image.rgba[i + 1] = static_cast<std::uint8_t>(y * 20);
                image.rgba[i + 2] = 90;
                image.rgba[i + 3] = 255;
            }
        }
        const std::string path = (dir / "golden.png").string();
        std::string error;
        KS_CHECK(ks::image::saveImageFile(path, image, &error));
        ks::image::RawImage back;
        KS_CHECK(ks::image::loadImageFile(path, &back, &error));
        KS_CHECK(back.width == 16 && back.height == 12);
        const VisualRegion whole{"whole", 0, 0, 16, 12, 0.0f, 0.0f};
        std::vector<RegionStats> out;
        KS_CHECK(compareImages(image.rgba.data(), back.rgba.data(), 16, 12, {whole}, out,
                               &error));
        KS_CHECK(out.size() == 1 && out[0].pass);
        KS_CHECK_NEAR(out[0].worst, 0.0, 1e-6);
        // ... and --record is the copy the runner stores as the golden.
        const std::string recorded = (dir / "recorded.png").string();
        KS_CHECK(recordFile(path, recorded) == 0);
        KS_CHECK(recordFile((dir / "missing.png").string(), recorded) == 2);
        std::filesystem::remove_all(dir, ec);
    }

    std::fprintf(stderr, "regions self-test: done\n");
    return KS_TEST_RESULT("visual_regions_test");
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--record") == 0) {
        if (argc != 4) {
            std::printf("usage: visual_regions_test --record <capture> <golden>\n");
            return 2;
        }
        return recordFile(argv[2], argv[3]);
    }
    if (argc >= 5 && std::strcmp(argv[1], "--check") == 0) {
        if (argc != 5) {
            std::printf("usage: visual_regions_test --check <capture> <golden> <regions>\n");
            return 2;
        }
        return checkFiles(argv[2], argv[3], argv[4]);
    }
    if (argc > 1) {
        std::printf("usage: visual_regions_test [--check|--record <capture> <golden> [<regions>]]\n");
        return 2;
    }
    return selfTest();
}
