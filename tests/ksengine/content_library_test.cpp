/**
 * Roadmap 1.3 / GAP P2.5 — content scanner behind the menu's car/track
 * select screens (ContentLibrary.h): sorted directory listing, files and a
 * missing root tolerated, default roots safe on a fresh checkout.
 */
#include "KsTest.h"
#include "simulator/ContentLibrary.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using ks::sim::scanCarLibrary;
using ks::sim::scanTrackLibrary;

int main() {
    // Missing root -> empty list, no throw.
    KS_CHECK(scanCarLibrary("content_library_test_missing").empty());
    // Default roots do not exist in a fresh checkout either -> empty, not a
    // crash (the select screen renders its hint row in that case).
    KS_CHECK(scanCarLibrary().empty());
    KS_CHECK(scanTrackLibrary().empty());

    // Fake content root: two car folders plus a stray file (ignored).
    const std::string root = "content_library_test_root";
    fs::create_directories(root + "/zeta");
    fs::create_directories(root + "/alpha");
    {
        std::FILE* f = std::fopen((root + "/readme.txt").c_str(), "w");
        KS_CHECK(f != nullptr);
        if (f) {
            std::fputs("x", f);
            std::fclose(f);
        }
    }

    const auto cars = scanCarLibrary(root);
    KS_CHECK(cars.size() == 2);
    if (cars.size() == 2) {
        KS_CHECK(cars[0].id == "alpha"); // sorted, files skipped
        KS_CHECK(cars[1].id == "zeta");
        KS_CHECK(cars[0].label == "alpha");
        KS_CHECK(cars[0].path == (fs::path(root) / "alpha").string());
        KS_CHECK(cars[1].path == (fs::path(root) / "zeta").string());
    }

    // The same scanner backs the track select.
    const auto tracks = scanTrackLibrary(root);
    KS_CHECK(tracks.size() == 2);

    fs::remove_all(root);
    return KS_TEST_RESULT("content_library_test");
}
