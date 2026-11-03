#include "PPFilterPreset.h"

#include "engine/FileFormat/Json.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ks { namespace engine { namespace config {
namespace {

const char* const kSchema = "ksengine.ppfilter/1";

bool readWholeFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

Json child(const Json& parent, const char* key) {
    if (!parent.isObject() || !parent.contains(key)) return Json::object();
    const Json value = parent.value(key);
    return value.isObject() ? value : Json::object();
}

float num(const Json& object, const char* key, float fallback) {
    if (!object.isObject() || !object.contains(key)) return fallback;
    const Json value = object.value(key);
    if (!value.isDouble()) return fallback;
    return static_cast<float>(value.toDouble(fallback));
}

int integer(const Json& object, const char* key, int fallback) {
    if (!object.isObject() || !object.contains(key)) return fallback;
    const Json value = object.value(key);
    if (!value.isDouble() && !value.isBool()) return fallback;
    if (value.isBool()) return value.toBool() ? 1 : 0;
    return value.toInt(fallback);
}

bool flag(const Json& object, const char* key, bool fallback) {
    if (!object.isObject() || !object.contains(key)) return fallback;
    const Json value = object.value(key);
    if (value.isBool()) return value.toBool(fallback);
    // The source filter format stores switches as 0/1 numbers.
    if (value.isDouble()) return value.toDouble(0.0) != 0.0;
    return fallback;
}

void pair(const Json& object, const char* key, float& x, float& y, float fallback) {
    if (!object.isObject() || !object.contains(key)) return;
    const Json value = object.value(key);
    if (!value.isArray() || value.size() < 2) return;
    x = static_cast<float>(value.at(0).toDouble(fallback));
    y = static_cast<float>(value.at(1).toDouble(fallback));
}

void quad(const Json& object, const char* key, float (&out)[4]) {
    if (!object.isObject() || !object.contains(key)) return;
    const Json value = object.value(key);
    if (!value.isArray() || value.size() < 3) return;
    out[0] = static_cast<float>(value.at(0).toDouble(out[0]));
    out[1] = static_cast<float>(value.at(1).toDouble(out[1]));
    out[2] = static_cast<float>(value.at(2).toDouble(out[2]));
    out[3] = value.size() > 3 ? static_cast<float>(value.at(3).toDouble(out[3])) : 1.0f;
}

} // namespace

bool PPFilterPreset::loadFromJsonText(const std::string& text, std::string* error) {
    bool ok = false;
    std::string parseError;
    const Json root = Json::parse(text, &ok, &parseError);
    if (!ok) {
        if (error) *error = parseError.empty() ? "malformed JSON" : parseError;
        return false;
    }
    if (!root.isObject()) {
        if (error) *error = "root is not an object";
        return false;
    }

    schema = root.value("schema").toString();
    if (schema != kSchema) {
        if (error) {
            *error = "unexpected schema '" + schema + "' (expected '" + kSchema + "')";
        }
        return false;
    }

    name = root.value("name").toString();
    description = root.value("description").toString();
    category = root.value("category").toString();

    const Json ae = child(root, "autoExposure");
    autoExposure.enabled = flag(ae, "enabled", autoExposure.enabled);
    autoExposure.delay = num(ae, "delay", autoExposure.delay);
    autoExposure.min = num(ae, "min", autoExposure.min);
    autoExposure.max = num(ae, "max", autoExposure.max);
    autoExposure.target = num(ae, "target", autoExposure.target);
    autoExposure.meteringWidth = num(ae, "meteringWidth", autoExposure.meteringWidth);
    autoExposure.meteringHeight = num(ae, "meteringHeight", autoExposure.meteringHeight);
    autoExposure.meteringOffsetX = num(ae, "meteringOffsetX", autoExposure.meteringOffsetX);
    autoExposure.meteringOffsetY = num(ae, "meteringOffsetY", autoExposure.meteringOffsetY);
    autoExposure.influencedByGlare =
        flag(ae, "influencedByGlare", autoExposure.influencedByGlare);

    const Json tm = child(root, "toneMapping");
    toneMapping.hdr = flag(tm, "hdr", toneMapping.hdr);
    toneMapping.exposure = num(tm, "exposure", toneMapping.exposure);
    toneMapping.gamma = num(tm, "gamma", toneMapping.gamma);
    toneMapping.function = integer(tm, "function", toneMapping.function);
    toneMapping.mappingFactor = num(tm, "mappingFactor", toneMapping.mappingFactor);
    toneMapping.scaleWidth = num(tm, "scaleWidth", toneMapping.scaleWidth);
    toneMapping.scaleHeight = num(tm, "scaleHeight", toneMapping.scaleHeight);
    toneMapping.offsetX = num(tm, "offsetX", toneMapping.offsetX);
    toneMapping.offsetY = num(tm, "offsetY", toneMapping.offsetY);

    const Json dof = child(root, "depthOfField");
    depthOfField.enabled = flag(dof, "enabled", depthOfField.enabled);
    depthOfField.quality = integer(dof, "quality", depthOfField.quality);
    depthOfField.apertureFNumber = num(dof, "apertureFNumber", depthOfField.apertureFNumber);
    depthOfField.imageSensorHeight = num(dof, "imageSensorHeight", depthOfField.imageSensorHeight);
    depthOfField.baseFOV = num(dof, "baseFOV", depthOfField.baseFOV);
    depthOfField.adaptiveApertureFactor =
        num(dof, "adaptiveApertureFactor", depthOfField.adaptiveApertureFactor);
    depthOfField.apertureParameter = num(dof, "apertureParameter", depthOfField.apertureParameter);
    depthOfField.apertureFrontLevels =
        integer(dof, "apertureFrontLevels", depthOfField.apertureFrontLevels);
    depthOfField.apertureBackLevels =
        integer(dof, "apertureBackLevels", depthOfField.apertureBackLevels);
    depthOfField.backgroundMaskThreshold =
        num(dof, "backgroundMaskThreshold", depthOfField.backgroundMaskThreshold);
    depthOfField.edgeQuality = integer(dof, "edgeQuality", depthOfField.edgeQuality);
    depthOfField.apertureShape = integer(dof, "apertureShape", depthOfField.apertureShape);

    const Json ca = child(root, "chromaticAberration");
    chromaticAberration.enabled = flag(ca, "enabled", chromaticAberration.enabled);
    chromaticAberration.samples = integer(ca, "samples", chromaticAberration.samples);
    pair(ca, "lateralDispersion", chromaticAberration.lateralDispersionX,
         chromaticAberration.lateralDispersionY, chromaticAberration.lateralDispersionX);
    pair(ca, "uniformDispersion", chromaticAberration.uniformDispersionX,
         chromaticAberration.uniformDispersionY, chromaticAberration.uniformDispersionX);

    const Json vg = child(root, "vignetting");
    vignetting.strength = num(vg, "strength", vignetting.strength);
    vignetting.fovDependence = num(vg, "fovDependence", vignetting.fovDependence);

    const Json gl = child(root, "glare");
    glare.enabled = flag(gl, "enabled", glare.enabled);
    glare.quality = integer(gl, "quality", glare.quality);
    glare.ghost = flag(gl, "ghost", glare.ghost);
    glare.afterImage = flag(gl, "afterImage", glare.afterImage);
    glare.precision = integer(gl, "precision", glare.precision);
    glare.anamorphic = flag(gl, "anamorphic", glare.anamorphic);
    glare.luminance = num(gl, "luminance", glare.luminance);
    glare.shape = integer(gl, "shape", glare.shape);
    glare.blur = flag(gl, "blur", glare.blur);
    glare.threshold = num(gl, "threshold", glare.threshold);
    glare.brightPass = flag(gl, "brightPass", glare.brightPass);

    const Json gr = child(root, "godRays");
    godRays.enabled = flag(gr, "enabled", godRays.enabled);
    godRays.useSunLight = flag(gr, "useSunLight", godRays.useSunLight);
    godRays.diffractionRing = num(gr, "diffractionRing", godRays.diffractionRing);
    godRays.diffractionRingRadius =
        num(gr, "diffractionRingRadius", godRays.diffractionRingRadius);
    godRays.diffractionRingAttenuation =
        num(gr, "diffractionRingAttenuation", godRays.diffractionRingAttenuation);
    godRays.length = num(gr, "length", godRays.length);
    godRays.glareRatio = num(gr, "glareRatio", godRays.glareRatio);
    godRays.angleAttenuation = num(gr, "angleAttenuation", godRays.angleAttenuation);
    quad(gr, "diffractionRingOuterColor", godRays.diffractionRingOuterColor);
    quad(gr, "color", godRays.color);

    const Json cg = child(root, "colorGrading");
    colorGrading.hue = num(cg, "hue", colorGrading.hue);
    colorGrading.saturation = num(cg, "saturation", colorGrading.saturation);
    colorGrading.brightness = num(cg, "brightness", colorGrading.brightness);
    colorGrading.contrast = num(cg, "contrast", colorGrading.contrast);
    colorGrading.sepia = num(cg, "sepia", colorGrading.sepia);
    colorGrading.colorTemp = integer(cg, "colorTemp", colorGrading.colorTemp);
    colorGrading.whiteBalance = integer(cg, "whiteBalance", colorGrading.whiteBalance);

    const Json lut = child(root, "colorLut");
    colorLut.enabled = flag(lut, "enabled", colorLut.enabled);
    colorLut.strength = num(lut, "strength", colorLut.strength);
    colorLut.file = lut.value("file").toString(colorLut.file);

    // Display transform: an explicit block wins, otherwise the tone-mapping
    // values are the closest thing the preset has to offer.
    const Json displayBlock = child(root, "display");
    display.exposure = num(displayBlock, "exposure", -1.0f);
    display.tonemapMode = integer(displayBlock, "tonemapMode", -1);
    if (display.exposure <= 0.0f) display.exposure = toneMapping.exposure;
    if (display.tonemapMode < 0) display.tonemapMode = toneMapping.function;

    return true;
}

bool PPFilterPreset::loadFile(const std::string& path, PPFilterPreset& out, std::string* error) {
    std::string text;
    if (!readWholeFile(path, text)) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    PPFilterPreset loaded;
    std::string parseError;
    if (!loaded.loadFromJsonText(text, &parseError)) {
        if (error) *error = path + ": " + parseError;
        return false;
    }
    out = loaded;
    return true;
}

std::vector<std::string> PPFilterPreset::listPresets(const std::string& directory) {
    std::vector<std::string> ids;
    std::error_code ec;
    std::filesystem::directory_iterator it(directory, ec);
    if (ec) return ids;
    for (const auto& entry : it) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string name = entry.path().filename().string();
        if (name.size() <= 5 || name.compare(name.size() - 5, 5, ".json") != 0) continue;
        ids.push_back(name.substr(0, name.size() - 5));
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::string PPFilterPreset::colorLutPath(const std::string& presetPath) const {
    if (colorLut.file.empty()) return std::string();
    std::error_code ec;
    const std::filesystem::path preset(presetPath);
    const std::filesystem::path parent = preset.parent_path();
    if (parent.empty()) return colorLut.file;
    // generic_string(): forward slashes on every platform, so a caller that
    // compares the path against the preset's own text gets the same spelling.
    return (parent / colorLut.file).generic_string();
}

}}} // namespace ks::engine::config
