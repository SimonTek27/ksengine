// Qt-free test for the post-processing preset loader
// (src/engine/Config/PPFilterPreset.h).
//
// It checks the mapping of every block, the defaults a partial preset keeps,
// the schema guard, and that every preset shipped in system/ppfilters/ parses
// with the values its file carries.

#include "engine/Config/PPFilterPreset.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("  FAIL %s\n", what);
        ++g_failures;
    }
}

void checkNear(double actual, double expected, double tolerance, const char* what) {
    if (std::fabs(actual - expected) > tolerance) {
        std::printf("  FAIL %s (got %.6f, want %.6f)\n", what, actual, expected);
        ++g_failures;
    }
}

const char* kSample = R"({
  "schema": "ksengine.ppfilter/1",
  "name": "Sample",
  "description": "test preset",
  "category": "natural",
  "autoExposure": {
    "enabled": true, "delay": 1.5, "min": 0.1, "max": 1.49, "target": 0.2,
    "meteringWidth": 90, "meteringHeight": 80, "meteringOffsetX": 2,
    "meteringOffsetY": -3, "influencedByGlare": true
  },
  "toneMapping": {
    "hdr": true, "exposure": 0.72, "gamma": 1.36, "function": 2,
    "mappingFactor": 33.38, "scaleWidth": 1, "scaleHeight": 1,
    "offsetX": 0.5, "offsetY": -0.5
  },
  "depthOfField": {
    "enabled": true, "quality": 5, "apertureFNumber": 3.3,
    "imageSensorHeight": 0.29, "baseFOV": 90, "adaptiveApertureFactor": 0.55,
    "apertureParameter": 3, "apertureFrontLevels": 4, "apertureBackLevels": 4,
    "backgroundMaskThreshold": 0.11, "edgeQuality": 4, "apertureShape": 1
  },
  "chromaticAberration": {
    "enabled": false, "samples": 5,
    "lateralDispersion": [0.005, 0.006], "uniformDispersion": [0.0005, 0.0006]
  },
  "vignetting": { "strength": 0.08, "fovDependence": 0.25 },
  "glare": {
    "enabled": true, "quality": 5, "ghost": false, "afterImage": false,
    "precision": 0, "anamorphic": false, "luminance": 2, "shape": 12,
    "blur": true, "threshold": 3.2, "brightPass": true
  },
  "godRays": {
    "enabled": true, "useSunLight": true, "diffractionRing": 0.1,
    "diffractionRingRadius": 3, "diffractionRingAttenuation": 0.1,
    "diffractionRingOuterColor": [0.5, 0.5, 0.5, 0.5], "color": [1.0, 0.9, 0.8, 1.0],
    "length": 7.0, "glareRatio": 0.015, "angleAttenuation": 8
  },
  "colorGrading": {
    "hue": 0, "saturation": 1, "brightness": 1.01, "contrast": 1, "sepia": 0,
    "colorTemp": 6026, "whiteBalance": 6500
  },
  "colorLut": { "enabled": true, "strength": 0.75, "file": "sample.lut.png" },
  "display": { "exposure": 1.35, "tonemapMode": 2 }
})";

void testFullMapping() {
    std::printf("full preset\n");
    ks::engine::config::PPFilterPreset p;
    std::string error;
    check(p.loadFromJsonText(kSample, &error), "sample parses");
    check(error.empty(), "no error text");

    check(p.schema == "ksengine.ppfilter/1", "schema kept");
    check(p.name == "Sample" && p.category == "natural", "name and category");

    check(p.autoExposure.enabled, "auto exposure enabled");
    checkNear(p.autoExposure.delay, 1.5, 1e-6, "auto exposure delay");
    checkNear(p.autoExposure.max, 1.49, 1e-6, "auto exposure max");
    checkNear(p.autoExposure.meteringOffsetY, -3.0, 1e-6, "metering offset y");
    check(p.autoExposure.influencedByGlare, "influenced by glare");

    checkNear(p.toneMapping.exposure, 0.72, 1e-6, "tone exposure");
    checkNear(p.toneMapping.gamma, 1.36, 1e-6, "tone gamma");
    check(p.toneMapping.function == 2, "tone function");
    checkNear(p.toneMapping.mappingFactor, 33.38, 1e-4, "mapping factor");

    check(p.depthOfField.enabled, "dof enabled");
    checkNear(p.depthOfField.apertureFNumber, 3.3, 1e-6, "aperture f-number");
    check(p.depthOfField.apertureFrontLevels == 4, "front levels");
    check(p.depthOfField.edgeQuality == 4, "edge quality");

    check(!p.chromaticAberration.enabled, "aberration off in the file");
    checkNear(p.chromaticAberration.lateralDispersionY, 0.006, 1e-9, "lateral dispersion y");
    checkNear(p.chromaticAberration.uniformDispersionX, 0.0005, 1e-9, "uniform dispersion x");

    checkNear(p.vignetting.strength, 0.08, 1e-6, "vignette strength");
    checkNear(p.vignetting.fovDependence, 0.25, 1e-6, "vignette fov dependence");

    check(p.glare.enabled, "glare enabled");
    check(!p.glare.ghost, "glare ghost off");
    check(p.glare.shape == 12, "glare shape");
    checkNear(p.glare.threshold, 3.2, 1e-6, "glare threshold");
    check(p.glare.brightPass, "glare bright pass");

    checkNear(p.godRays.length, 7.0, 1e-6, "god rays length");
    checkNear(p.godRays.glareRatio, 0.015, 1e-6, "god rays glare ratio");
    checkNear(p.godRays.color[1], 0.9, 1e-6, "god rays colour green");
    checkNear(p.godRays.color[3], 1.0, 1e-6, "god rays colour alpha");
    checkNear(p.godRays.diffractionRingOuterColor[3], 0.5, 1e-6, "ring colour alpha");

    checkNear(p.colorGrading.brightness, 1.01, 1e-6, "colour brightness");
    check(p.colorGrading.colorTemp == 6026, "colour temperature");
    check(p.colorGrading.whiteBalance == 6500, "white balance");

    check(p.colorLut.enabled, "lut enabled");
    checkNear(p.colorLut.strength, 0.75, 1e-6, "lut strength");
    check(p.colorLut.file == "sample.lut.png", "lut file");

    checkNear(p.display.exposure, 1.35, 1e-6, "display exposure");
    check(p.display.tonemapMode == 2, "display tonemap mode");
}

void testDefaultsAndFallbacks() {
    std::printf("defaults and fallbacks\n");
    ks::engine::config::PPFilterPreset p;
    std::string error;
    const char* minimal = R"({
      "schema": "ksengine.ppfilter/1",
      "name": "Minimal",
      "toneMapping": { "exposure": 0.44, "function": 4 }
    })";
    check(p.loadFromJsonText(minimal, &error), "minimal parses");

    // Untouched blocks keep the model defaults.
    checkNear(p.vignetting.strength, 0.035, 1e-6, "default vignette strength");
    check(p.glare.enabled, "default glare enabled");
    check(p.depthOfField.apertureFrontLevels == -1, "default dof levels");
    check(p.godRays.color[0] == 1.0f, "default god rays colour");

    // Without a display block the tone-mapping values stand in for it.
    checkNear(p.display.exposure, 0.44, 1e-6, "display exposure falls back to tone mapping");
    check(p.display.tonemapMode == 4, "display mode falls back to tone function");

    // A partial preset that also leaves tone mapping alone has no display.
    ks::engine::config::PPFilterPreset bare;
    check(bare.loadFromJsonText(R"({"schema":"ksengine.ppfilter/1"})", &error), "bare parses");
    checkNear(bare.display.exposure, 0.28, 1e-6, "bare display exposure is the model default");
}

void testRejects() {
    std::printf("rejections\n");
    ks::engine::config::PPFilterPreset p;
    std::string error;

    check(!p.loadFromJsonText("{ not json", &error), "malformed json rejected");
    check(!error.empty(), "malformed json reports an error");

    error.clear();
    check(!p.loadFromJsonText(R"({"schema":"other/9"})", &error), "foreign schema rejected");
    check(!error.empty(), "foreign schema reports an error");

    check(!p.loadFromJsonText(R"({"name":"no schema"})", &error), "missing schema rejected");

    ks::engine::config::PPFilterPreset loaded;
    check(!ks::engine::config::PPFilterPreset::loadFile("does/not/exist.json", loaded, &error),
          "missing file rejected");
}

void testShippedPresets() {
    std::printf("shipped presets\n");
    const std::string dir = std::string(KS_SOURCE_DIR) + "/system/ppfilters";
    const std::vector<std::string> ids = ks::engine::config::PPFilterPreset::listPresets(dir);
    std::printf("  %zu preset(s) found in %s\n", ids.size(), dir.c_str());
    check(!ids.empty(), "at least one preset ships");

    for (const std::string& id : ids) {
        const std::string path = dir + "/" + id + ".json";
        ks::engine::config::PPFilterPreset p;
        std::string error;
        const bool ok = ks::engine::config::PPFilterPreset::loadFile(path, p, &error);
        if (!ok) std::printf("  %s: %s\n", id.c_str(), error.c_str());
        check(ok, (id + " parses").c_str());
        check(!p.name.empty(), (id + " has a name").c_str());
        check(p.toneMapping.exposure > 0.0f, (id + " has an exposure").c_str());
        check(p.display.exposure > 0.0f, (id + " resolves a display exposure").c_str());
    }

    // normal.json points its colour grading at a strip next to the file: the
    // preset must resolve that path without the caller having to join it.
    ks::engine::config::PPFilterPreset normal;
    std::string error;
    if (ks::engine::config::PPFilterPreset::loadFile(dir + "/normal.json", normal, &error)) {
        check(normal.colorLut.file == "normal.lut.png", "normal lut file name");
        check(normal.colorLutPath(dir + "/normal.json") == dir + "/normal.lut.png",
              "normal lut resolves next to the preset");
    } else {
        check(false, "normal.json parses");
    }
}

} // namespace

int main() {
    testFullMapping();
    testDefaultsAndFallbacks();
    testRejects();
    testShippedPresets();

    if (g_failures != 0) {
        std::printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    std::printf("OK: post-processing preset loader\n");
    return 0;
}
