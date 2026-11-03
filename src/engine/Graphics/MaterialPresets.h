#pragma once
/**
 * What a surface is made of, from the names a KN5 carries.
 *
 * Before this, the car body was resolved with four ad-hoc flags (paint, metal,
 * glass, rubber) written inline in the loader, each with a roughness clamp of
 * its own. It worked, but every value lived in one function, nothing could be
 * checked, and materials that were none of those four (carbon, leather,
 * plastic trim, light lenses) all fell back to the same flat dielectric.
 *
 * A preset is the answer to "this material is a lacquered body panel, what
 * should it look like": roughness, metalness, clear-coat. The file's authored
 * ksSpecularEXP is not thrown away — `authoredWeight` says how far it may move
 * the roughness away from the preset. 0 lets the preset win (a windscreen is
 * glass whatever the exponent says), 1 defers to the file (an unknown shader,
 * where the preset has nothing better to offer).
 *
 * Matching is on the shader and material names, lowercased, most specific
 * first, so `ksBrakeDisc` cannot be swallowed by a generic "metal" rule.
 *
 * Header-only and free of engine dependencies: the loader and the tests both
 * include it, and a test can check the table without a GPU.
 */
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>

namespace ks {
namespace graphics {

enum class MaterialPresetId : unsigned char {
    Default = 0,
    Paint,        // lacquered bodywork
    Chrome,       // bright trim, mirror housings, exhaust tips
    BrushedMetal, // rims, aluminium, bare cast parts
    BrakeDisc,    // cast iron discs: metal, but matte and heat-stained
    Glass,        // windscreen, side windows, light covers
    Rubber,       // tyres, seals, hoses
    Plastic,      // matte trim, grilles, interior panels
    Carbon,       // carbon-fibre panels: dark, coated, slightly metallic
    Leather,      // seats, dashboard, steering wheel
};

struct MaterialPreset {
    MaterialPresetId id;
    const char* name;      // stable id, also the log/UI label
    float roughness;       // GGX alpha, the value the preset wants
    float metalness;       // 0 dielectric, 1 metal
    float clearcoat;       // 0..1, the lacquer on top of the base layer
    float authoredWeight;  // how far ksSpecularEXP may move the roughness
};

/** The table, most specific surfaces first. `Default` is the last entry and the
 *  only one whose authoredWeight is 1: with nothing known about a surface, the
 *  file's own exponent is the best information there is. */
inline const MaterialPreset* materialPresetTable(std::size_t& count) {
    static const MaterialPreset kPresets[] = {
        {MaterialPresetId::BrakeDisc, "brake_disc", 0.32f, 1.00f, 0.00f, 0.40f},
        {MaterialPresetId::Glass, "glass", 0.02f, 0.00f, 0.00f, 0.00f},
        {MaterialPresetId::Rubber, "rubber", 0.85f, 0.00f, 0.00f, 0.15f},
        {MaterialPresetId::Carbon, "carbon", 0.18f, 0.20f, 1.00f, 0.25f},
        {MaterialPresetId::Chrome, "chrome", 0.05f, 1.00f, 0.00f, 0.10f},
        {MaterialPresetId::BrushedMetal, "brushed_metal", 0.22f, 1.00f, 0.00f, 0.40f},
        {MaterialPresetId::Leather, "leather", 0.60f, 0.00f, 0.00f, 0.50f},
        {MaterialPresetId::Plastic, "plastic", 0.45f, 0.00f, 0.00f, 0.55f},
        {MaterialPresetId::Paint, "paint", 0.10f, 0.00f, 1.00f, 0.20f},
        {MaterialPresetId::Default, "default", 0.35f, 0.00f, 0.00f, 1.00f},
    };
    count = sizeof(kPresets) / sizeof(kPresets[0]);
    return kPresets;
}

inline const MaterialPreset& materialPreset(MaterialPresetId id) {
    std::size_t count = 0;
    const MaterialPreset* table = materialPresetTable(count);
    for (std::size_t i = 0; i < count; ++i)
        if (table[i].id == id) return table[i];
    return table[count - 1]; // Default is last by construction
}

inline std::string materialLower(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

inline bool materialMentions(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

/** Pick the preset for a KN5 material from its shader and material names.
 *  Both are matched lowercased; an empty pair yields Default. */
inline const MaterialPreset& resolveMaterialPreset(const std::string& shaderName,
                                                   const std::string& materialName) {
    const std::string shader = materialLower(shaderName);
    const std::string name = materialLower(materialName);

    // Brakes first: "disc" would otherwise land in the metal rule below.
    if (materialMentions(shader, "brakedisc") || materialMentions(name, "disc") ||
        materialMentions(name, "brake"))
        return materialPreset(MaterialPresetId::BrakeDisc);

    // Glass and light covers share the shader family in AC content.
    if (materialMentions(shader, "windscreen") || materialMentions(shader, "glass") ||
        materialMentions(name, "glass") || materialMentions(name, "windscreen") ||
        materialMentions(name, "lens"))
        return materialPreset(MaterialPresetId::Glass);

    if (materialMentions(shader, "tyres") || materialMentions(name, "tyre") ||
        materialMentions(name, "tire") || materialMentions(name, "rubber"))
        return materialPreset(MaterialPresetId::Rubber);

    if (materialMentions(shader, "carbon") || materialMentions(name, "carbon"))
        return materialPreset(MaterialPresetId::Carbon);

    if (materialMentions(shader, "chrome") || materialMentions(name, "chrome") ||
        materialMentions(name, "mirror"))
        return materialPreset(MaterialPresetId::Chrome);

    if (materialMentions(shader, "leather") || materialMentions(name, "leather") ||
        materialMentions(name, "alcantara"))
        return materialPreset(MaterialPresetId::Leather);

    // Not the generic reflection shader: AC uses it for plates, stickers and
    // light housings too, and calling all of those metal turned a third of
    // the car chrome. Only an explicit name makes a surface bare metal.
    if (materialMentions(shader, "metal") || materialMentions(name, "rim") ||
        materialMentions(name, "wheel") || materialMentions(name, "alu"))
        return materialPreset(MaterialPresetId::BrushedMetal);

    // Painted bodywork: the damage/dirt multi-map is the AC body shader, and
    // the 499P names its own panels MAIN_PAINT / EXT.
    if (materialMentions(shader, "damage_dirt") || materialMentions(shader, "carpaint") ||
        materialMentions(name, "paint") || materialMentions(name, "body") ||
        materialMentions(name, "livery"))
        return materialPreset(MaterialPresetId::Paint);

    if (materialMentions(shader, "plastic") || materialMentions(name, "plastic") ||
        materialMentions(name, "trim") || materialMentions(name, "grille") ||
        materialMentions(name, "interior"))
        return materialPreset(MaterialPresetId::Plastic);

    return materialPreset(MaterialPresetId::Default);
}

/** The three values the MaterialData block cares about, resolved from a preset
 *  and the roughness the file's exponent implies. */
struct MaterialSurface {
    float roughness = 0.35f;
    float metalness = 0.0f;
    float clearcoat = 0.0f;
};

inline MaterialSurface applyMaterialPreset(const MaterialPreset& preset,
                                          float authoredRoughness) {
    MaterialSurface surface;
    const float base = preset.roughness;
    const float authored = std::min(std::max(authoredRoughness, 0.0f), 1.0f);
    surface.roughness =
        std::min(std::max(base + (authored - base) * preset.authoredWeight, 0.01f), 1.0f);
    surface.metalness = preset.metalness;
    surface.clearcoat = preset.clearcoat;
    return surface;
}

} // namespace graphics
} // namespace ks
