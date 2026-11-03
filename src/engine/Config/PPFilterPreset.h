#pragma once
#include <string>
#include <vector>

namespace ks { namespace engine { namespace config {

/**
 * One post-processing preset, as stored in system/ppfilters/<id>.json
 * (schema "ksengine.ppfilter/1").
 *
 * The blocks mirror the fields the renderer's post-processing model exposes;
 * loadFromJsonText() fills every one of them and leaves the default in place
 * when the file does not carry it, so a partial preset is still usable.
 */
struct PPFilterPreset {
    struct AutoExposure {
        bool enabled = true;
        float delay = 0.0f;
        float min = 0.2f;
        float max = 0.5f;
        float target = 0.32f;
        float meteringWidth = 100.0f;
        float meteringHeight = 100.0f;
        float meteringOffsetX = 0.0f;
        float meteringOffsetY = 0.0f;
        bool influencedByGlare = false;
    };

    struct ToneMapping {
        bool hdr = true;
        float exposure = 0.28f;
        float gamma = 1.2f;
        int function = -1; // -1 = keep the renderer's operator
        float mappingFactor = 32.0f;
        float scaleWidth = 1.0f;
        float scaleHeight = 1.0f;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
    };

    struct DepthOfField {
        bool enabled = true;
        int quality = 3;
        float apertureFNumber = 12.0f;
        float imageSensorHeight = 0.24f;
        float baseFOV = 50.0f;
        float adaptiveApertureFactor = 0.5f;
        float apertureParameter = 2.0f;
        int apertureFrontLevels = -1;
        int apertureBackLevels = -1;
        float backgroundMaskThreshold = 0.1f;
        int edgeQuality = 3;
        int apertureShape = 3;
    };

    struct ChromaticAberration {
        bool enabled = true;
        int samples = 5;
        float lateralDispersionX = 0.005f;
        float lateralDispersionY = 0.005f;
        float uniformDispersionX = 0.0005f;
        float uniformDispersionY = 0.0005f;
    };

    struct Vignetting {
        float strength = 0.035f;
        float fovDependence = 0.0f;
    };

    struct Glare {
        bool enabled = true;
        int quality = 3;
        bool ghost = true;
        bool afterImage = false;
        int precision = 0;
        bool anamorphic = false;
        float luminance = 1.6f;
        int shape = 11;
        bool blur = true;
        float threshold = 5.0f;
        bool brightPass = false;
    };

    struct GodRays {
        bool enabled = true;
        bool useSunLight = true;
        float diffractionRing = 0.25f;
        float diffractionRingRadius = 5.0f;
        float diffractionRingAttenuation = 0.1f;
        float diffractionRingOuterColor[4] = {0.5f, 0.5f, 0.5f, 0.5f};
        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float length = 11.0f;
        float glareRatio = 0.005f;
        float angleAttenuation = 5.0f;
    };

    struct ColorGrading {
        float hue = 0.0f;
        float saturation = 0.95f;
        float brightness = 1.0f;
        float contrast = 1.0f;
        float sepia = 0.0f;
        int colorTemp = 6400;
        int whiteBalance = 6400;
    };

    struct ColorLut {
        bool enabled = false;
        float strength = 1.0f;
        std::string file; // resolved next to the preset file
    };

    /** Display transform knobs. -1 means "the preset does not specify it". */
    struct Display {
        float exposure = -1.0f;
        int tonemapMode = -1;
    };

    std::string schema;
    std::string name;
    std::string description;
    std::string category;

    AutoExposure autoExposure;
    ToneMapping toneMapping;
    DepthOfField depthOfField;
    ChromaticAberration chromaticAberration;
    Vignetting vignetting;
    Glare glare;
    GodRays godRays;
    ColorGrading colorGrading;
    ColorLut colorLut;
    Display display;

    /** Parse a preset document. Unknown keys are ignored, missing ones keep
     *  their defaults; a wrong/absent schema is an error. */
    bool loadFromJsonText(const std::string& text, std::string* error = nullptr);

    /** Read and parse one preset file. */
    static bool loadFile(const std::string& path, PPFilterPreset& out,
                         std::string* error = nullptr);

    /** Preset ids (file name without .json) found in a directory, sorted. */
    static std::vector<std::string> listPresets(const std::string& directory);

    /** Absolute-ish path of the LUT for this preset, next to the preset file. */
    std::string colorLutPath(const std::string& presetPath) const;

    static PPFilterPreset& instance() {
        static PPFilterPreset s;
        return s;
    }
    bool load(const std::string& path) {
        std::string error;
        return loadFile(path, *this, &error);
    }
};

}}} // namespace ks::engine::config
