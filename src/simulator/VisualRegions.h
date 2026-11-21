#pragma once
// Named-region visual regression: the rectangles a capture was measured on by
// hand (wall, floor, car, ...) as a test on golden PNGs.
//
//   visual_regions_test                                     self-test (synthetic)
//   visual_regions_test --check <capture> <golden> <regions>   compare one frame
//   visual_regions_test --record <capture> <golden>            accept today's frame
//
// A golden is the PNG a capture run wrote (KS_SCREENSHOT); the regions file
// next to it names the rectangles and their thresholds:
//
//   ksregions 1
//   # the room's grade fix was measured on these by hand
//   region wall 200 380 400 140 3.0 120.0
//
// x y w h are pixels from the top-left, thresholds are |dRGB| sums (0..765):
// maxMean fails a region the grade moved, maxWorst fails it on a spike the
// mean would hide. The checker prints every region's actuals, so thresholds
// tune by evidence, not by guessing.
//
// Buffers are the maths (RGBA, tightly packed, top-left origin, alpha
// ignored); the file layer goes through the material module's codec, the same
// one KS_SCREENSHOT writes with, so a golden any run wrote loads here.
#include "../engine/material/PngCodec.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace ks {
namespace sim {
namespace regions {

inline constexpr int kRegionsVersion = 1;

struct VisualRegion {
    std::string name;
    int x = 0, y = 0, w = 0, h = 0;
    float maxMean = 4.0f;
    float maxWorst = 120.0f;
};

struct RegionStats {
    std::string name;
    bool inside = false; // the rectangle meets the frame at all
    int pixels = 0;
    float mean = 0.f;   // the hand measure: mean |dR| + |dG| + |dB|
    float median = 0.f; // what most of the region does (a spike hides here)
    float worst = 0.f;  // the spike the mean hides
    bool pass = false;
};

/** The distance the hand measures used: |dR| + |dG| + |dB|, 0..765. */
inline float pixelDistance(const unsigned char* a, const unsigned char* b) {
    return std::fabs(float(a[0]) - float(b[0])) + std::fabs(float(a[1]) - float(b[1])) +
           std::fabs(float(a[2]) - float(b[2]));
}

inline RegionStats regionStats(const std::string& name, int rx, int ry, int rw, int rh,
                               const unsigned char* golden, const unsigned char* current,
                               int width, int height) {
    RegionStats stats;
    stats.name = name;
    if (golden == nullptr || current == nullptr || width <= 0 || height <= 0) return stats;
    const int x0 = std::max(rx, 0), y0 = std::max(ry, 0);
    const int x1 = std::min(rx + rw, width), y1 = std::min(ry + rh, height);
    if (x1 <= x0 || y1 <= y0) return stats; // outside the frame, never a pass
    stats.inside = true;
    std::vector<float> deltas;
    deltas.reserve(static_cast<std::size_t>(x1 - x0) * static_cast<std::size_t>(y1 - y0));
    float worst = 0.f;
    double sum = 0.0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const std::size_t i =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) * 4;
            const float d = pixelDistance(golden + i, current + i);
            deltas.push_back(d);
            sum += d;
            if (d > worst) worst = d;
        }
    }
    stats.pixels = static_cast<int>(deltas.size());
    stats.mean = stats.pixels > 0 ? static_cast<float>(sum / stats.pixels) : 0.f;
    stats.worst = worst;
    if (!deltas.empty()) {
        std::nth_element(deltas.begin(), deltas.begin() + deltas.size() / 2, deltas.end());
        stats.median = deltas[deltas.size() / 2];
    }
    return stats;
}

/** Parses the regions file: `ksregions 1` on its own row, then one `region`
 *  per row. `#` starts a comment, on its own row or after a row's values.
 *  False when the header or any row is malformed. */
inline bool parseRegions(const std::string& text, std::vector<VisualRegion>& out,
                         std::string* error = nullptr) {
    out.clear();
    std::istringstream in(text);
    std::string line;
    bool header = false;
    while (std::getline(in, line)) {
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        std::istringstream row(line);
        std::string tag;
        if (!(row >> tag)) continue; // an empty (or comment-only) row
        if (!header) {
            int version = 0;
            if (tag != "ksregions" || !(row >> version) || version != kRegionsVersion) {
                if (error) *error = "not a ksregions document";
                return false;
            }
            header = true;
            continue;
        }
        if (tag != "region") {
            if (error) *error = "expected 'region', found '" + tag + "'";
            return false;
        }
        VisualRegion region;
        if (!(row >> region.name >> region.x >> region.y >> region.w >> region.h >>
              region.maxMean >> region.maxWorst)) {
            if (error) *error = "short region row for '" + region.name + "'";
            return false;
        }
        if (region.name.empty() || region.w <= 0 || region.h <= 0) {
            if (error) *error = "bad rectangle for '" + region.name + "'";
            return false;
        }
        out.push_back(region);
    }
    if (!header) {
        if (error) *error = "not a ksregions document";
        return false;
    }
    if (out.empty()) {
        if (error) *error = "no regions declared";
        return false;
    }
    return true;
}

inline std::string regionsText(const std::vector<VisualRegion>& regions) {
    std::ostringstream out;
    out << "ksregions " << kRegionsVersion << "\n";
    for (const VisualRegion& region : regions) {
        out << "region " << region.name << " " << region.x << " " << region.y << " "
            << region.w << " " << region.h << " " << region.maxMean << " " << region.maxWorst
            << "\n";
    }
    return out.str();
}

/** Every region of two same-size frames. False only on a null or empty buffer:
//  a size mismatch is the caller's (it holds both extents), a region outside
//  the frame is reported on the region, not as a failure of the call. */
inline bool compareImages(const unsigned char* golden, const unsigned char* current, int width,
                          int height, const std::vector<VisualRegion>& regions,
                          std::vector<RegionStats>& stats, std::string* error = nullptr) {
    stats.clear();
    if (golden == nullptr || current == nullptr || width <= 0 || height <= 0) {
        if (error) *error = "no pixels to compare";
        return false;
    }
    for (const VisualRegion& region : regions) {
        RegionStats sample =
            regionStats(region.name, region.x, region.y, region.w, region.h, golden, current,
                        width, height);
        sample.pass =
            sample.inside && sample.mean <= region.maxMean && sample.worst <= region.maxWorst;
        stats.push_back(sample);
    }
    return true;
}

inline bool loadRgba(const std::string& path, ks::image::RawImage& out,
                     std::string* error = nullptr) {
    if (!ks::image::loadImageFile(path, &out, error)) return false;
    if (out.isNull()) {
        if (error) *error = "empty image " + path;
        return false;
    }
    return true;
}

/** Compares a capture against its golden, region by region, printing every
 *  region's actuals so a threshold tunes by evidence. 0 = all pass, 1 = a
 *  region failed or the sizes disagree, 2 = an input could not be read. */
inline int checkFiles(const std::string& capturePath, const std::string& goldenPath,
                      const std::string& regionsPath) {
    std::string error;
    ks::image::RawImage capture, golden;
    if (!loadRgba(capturePath, capture, &error)) {
        std::printf("regions: cannot read capture %s: %s\n", capturePath.c_str(), error.c_str());
        return 2;
    }
    if (!loadRgba(goldenPath, golden, &error)) {
        std::printf("regions: cannot read golden %s: %s\n", goldenPath.c_str(), error.c_str());
        return 2;
    }
    if (capture.width != golden.width || capture.height != golden.height) {
        std::printf("regions: FAIL size %dx%d against golden %dx%d: not the same picture\n",
                    capture.width, capture.height, golden.width, golden.height);
        return 1;
    }
    std::ifstream regionsFile(regionsPath, std::ios::binary);
    if (!regionsFile) {
        std::printf("regions: cannot read regions %s\n", regionsPath.c_str());
        return 2;
    }
    std::ostringstream buffer;
    buffer << regionsFile.rdbuf();
    std::vector<VisualRegion> regions;
    if (!parseRegions(buffer.str(), regions, &error)) {
        std::printf("regions: cannot parse %s: %s\n", regionsPath.c_str(), error.c_str());
        return 2;
    }
    std::vector<RegionStats> stats;
    if (!compareImages(golden.rgba.data(), capture.rgba.data(), golden.width, golden.height,
                       regions, stats, &error)) {
        std::printf("regions: cannot compare: %s\n", error.c_str());
        return 2;
    }
    int failures = 0;
    for (std::size_t i = 0; i < stats.size(); ++i) {
        const RegionStats& sample = stats[i];
        const VisualRegion& region = regions[i];
        if (!sample.inside) {
            std::printf("regions: FAIL %s outside the %dx%d frame\n", sample.name.c_str(),
                        golden.width, golden.height);
            ++failures;
            continue;
        }
        std::printf("regions: %s %s mean %.2f median %.2f worst %.2f (max %.2f/%.2f, %d px)\n",
                    sample.pass ? "PASS" : "FAIL", sample.name.c_str(), sample.mean,
                    sample.median, sample.worst, region.maxMean, region.maxWorst,
                    sample.pixels);
        if (!sample.pass) ++failures;
    }
    return failures == 0 ? 0 : 1;
}

/** Accepts today's capture as the new golden: a plain file copy, so the PNG a
 *  human looked at is byte for byte what the next --check compares. */
inline int recordFile(const std::string& capturePath, const std::string& goldenPath) {
    std::error_code ec;
    std::filesystem::copy_file(capturePath, goldenPath,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::printf("regions: cannot record %s -> %s: %s\n", capturePath.c_str(),
                    goldenPath.c_str(), ec.message().c_str());
        return 2;
    }
    std::printf("regions: recorded %s\n", goldenPath.c_str());
    return 0;
}

} // namespace regions
} // namespace sim
} // namespace ks
