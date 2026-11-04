// Qt-free test for the material preset table
// (src/engine/Graphics/MaterialPresets.h).
//
// It checks that the shader/material names a KN5 actually carries land on the
// right preset, that the table itself is sane (every value in range, ids
// unique), and that the authored ksSpecularEXP only moves the roughness as far
// as each preset allows.

#include "engine/Graphics/MaterialOverrides.h"
#include "engine/Graphics/MaterialPresets.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

using namespace ks::graphics;

int g_failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("  FAIL %s\n", what);
        ++g_failures;
    }
}

void checkNear(double actual, double expected, double tolerance, const char* what) {
    if (std::fabs(actual - expected) > tolerance) {
        std::printf("  FAIL %s (got %.4f, want %.4f)\n", what, actual, expected);
        ++g_failures;
    }
}

void checkId(const char* shader, const char* material, MaterialPresetId want,
             const char* what) {
    const MaterialPreset& preset = resolveMaterialPreset(shader, material);
    if (preset.id != want) {
        std::printf("  FAIL %s (got %s, want %s)\n", what, preset.name,
                    materialPreset(want).name);
        ++g_failures;
    }
}

} // namespace

int main() {
    // --- the names the 499P and a stock AC car really use ------------------
    checkId("ksPerPixelMultiMap_damage_dirt", "MAIN_PAINT", MaterialPresetId::Paint,
            "damage_dirt body shader is paint");
    checkId("ksPerPixelReflection", "EXT_CHROME", MaterialPresetId::Chrome,
            "reflection shader named chrome is chrome");
    checkId("ksWindscreen", "EXT_GLASS", MaterialPresetId::Glass, "windscreen is glass");
    checkId("ksPerPixelReflection", "MAIN_HEADLIGHT_LENS", MaterialPresetId::Glass,
            "a light lens is glass");
    checkId("ksTyres", "TYRES_0", MaterialPresetId::Rubber, "tyres are rubber");
    checkId("ksBrakeDisc", "DISCS", MaterialPresetId::BrakeDisc, "discs are brake discs");
    checkId("ksPerPixelMultiMap", "MAIN_RIM", MaterialPresetId::BrushedMetal,
            "rims are brushed metal");
    checkId("ksPerPixelMultiMap", "INT_CARBON", MaterialPresetId::Carbon,
            "carbon is carbon");
    checkId("ksPerPixelMultiMap", "MAIN_BODY_ALUMINIUM", MaterialPresetId::BrushedMetal,
            "aluminium trim is metal");

    // Case must not matter: the file ships uppercase names, the rules are lower.
    checkId("KSPERPIXELMULTIMAP_DAMAGE_DIRT", "main_paint", MaterialPresetId::Paint,
            "matching is case-insensitive");
    checkId("", "", MaterialPresetId::Default, "no names means no opinion");

    // --- the table itself --------------------------------------------------
    std::size_t count = 0;
    const MaterialPreset* table = materialPresetTable(count);
    check(count >= 8, "the table has the presets the loader uses");
    for (std::size_t i = 0; i < count; ++i) {
        const MaterialPreset& preset = table[i];
        check(preset.name != nullptr && preset.name[0] != '\0', "every preset is named");
        check(preset.roughness > 0.0f && preset.roughness <= 1.0f,
              "preset roughness is in range");
        check(preset.metalness >= 0.0f && preset.metalness <= 1.0f,
              "preset metalness is in range");
        check(preset.clearcoat >= 0.0f && preset.clearcoat <= 1.0f,
              "preset clearcoat is in range");
        check(preset.authoredWeight >= 0.0f && preset.authoredWeight <= 1.0f,
              "preset authored weight is in range");
        for (std::size_t j = i + 1; j < count; ++j) {
            check(preset.id != table[j].id, "preset ids are unique");
        }
    }

    // --- authored roughness only moves as far as the preset allows ---------
    const MaterialPreset& paint = materialPreset(MaterialPresetId::Paint);
    const MaterialSurface painted = applyMaterialPreset(paint, 0.6f);
    check(painted.roughness < 0.25f, "a lacquered panel stays glossy");
    checkNear(painted.clearcoat, 1.0f, 1e-6, "a lacquered panel is coated");
    checkNear(painted.metalness, 0.0f, 1e-6, "paint is a dielectric");

    const MaterialPreset& rubber = materialPreset(MaterialPresetId::Rubber);
    const MaterialSurface tyre = applyMaterialPreset(rubber, 0.1f);
    check(tyre.roughness > 0.5f && tyre.roughness <= rubber.roughness,
          "a tyre stays matte even with a glossy exponent");

    const MaterialPreset& glass = materialPreset(MaterialPresetId::Glass);
    const MaterialSurface pane = applyMaterialPreset(glass, 0.9f);
    check(pane.roughness < 0.05f, "glass ignores a matte exponent");

    const MaterialPreset& unknown = materialPreset(MaterialPresetId::Default);
    const MaterialSurface authored = applyMaterialPreset(unknown, 0.42f);
    checkNear(authored.roughness, 0.42f, 1e-5, "an unknown surface keeps its own roughness");

    // Garbage in, sane out.
    const MaterialSurface clampedLow = applyMaterialPreset(paint, -3.0f);
    check(clampedLow.roughness >= 0.05f && clampedLow.roughness <= paint.roughness + 1e-4f,
          "a negative exponent is clamped, not propagated");
    checkNear(applyMaterialPreset(paint, 7.0f).roughness, 1.0f * paint.authoredWeight +
                                                              paint.roughness * (1.0f - paint.authoredWeight),
              1e-4, "an absurd exponent is clamped to one");

    // --- per-car overrides ------------------------------------------------
    MaterialOverrides overrides;
    const std::string json =
        R"json({"schema": "ksengine.material_presets/1", "materials": {"MAIN_PAINT": {"preset": "chrome", "roughness": 0.31}, "TYRES": {"metalness": 0.5}}})json";
    std::string why;
    check(overrides.parse(json, &why), "a small override file parses");
    check(overrides.size() == 2, "every entry is kept");
    const MaterialOverrideEntry* paintedEntry = overrides.find("main_paint");
    check(paintedEntry != nullptr, "lookup is case-insensitive");
    check(paintedEntry != nullptr && paintedEntry->hasPreset &&
              paintedEntry->preset == MaterialPresetId::Chrome,
          "an override can rename the preset");
    const MaterialSurface overridden =
        overrides.resolve("ksPerPixelMultiMap_damage_dirt", "MAIN_PAINT", 0.6f);
    checkNear(overridden.roughness, 0.31, 1e-5, "a pinned roughness wins");
    checkNear(overridden.metalness, 1.0, 1e-5, "the renamed preset supplies metalness");
    const MaterialSurface untouched =
        overrides.resolve("ksPerPixelMultiMap_damage_dirt", "EXT", 0.6f);
    check(untouched.roughness > 0.09f, "a material with no entry keeps its preset");
    MaterialOverrides broken;
    check(!broken.parse("not json", nullptr), "garbage is rejected");
    check(broken.empty(), "a rejected file leaves nothing behind");

    // --- wildcard keys ------------------------------------------------------
    MaterialOverrides patterns;
    const std::string wild =
        R"json({"materials": {"DECAL_07": {"roughness": 0.9}, "DECAL_*": {"roughness": 0.5}, "DECAL_07_*": {"roughness": 0.7}, "*_GLASS": {"preset": "glass"}, "*": {"metalness": 0.25}}})json";
    check(patterns.parse(wild, nullptr), "wildcard keys parse");
    check(patterns.patternCount() == 4, "patterns are kept apart from exact keys");
    check(patterns.findEffective(
              "DECAL_08") != nullptr, "a DECAL_* key catches DECAL_08");
    checkNear(patterns.resolve(
                  "", "DECAL_07", 0.3f).roughness, 0.9, 1e-5,
              "an exact key beats every wildcard");
    checkNear(patterns.resolve(
                  "", "DECAL_07_A", 0.3f).roughness, 0.7, 1e-5,
              "the most specific pattern wins");
    check(patterns.presetFor(
              "", "EXT_GLASS") == MaterialPresetId::Glass,
          "a wildcard can carry a preset");
    check(patterns.findEffective(
              "MAIN_PAINT") != nullptr, "the catch-all still matches");
    check(materialWildcardMatch("AB*", "AB"), "a star can match nothing");
    check(!materialWildcardMatch("AB?", "AB"), "a question mark needs a character");

    if (g_failures == 0) std::printf("material_preset_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
