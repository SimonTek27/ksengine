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
        {MaterialPresetId::BrakeDisc, "brake_disc", 0.38f, 1.00f, 0.00f, 0.40f},
        {MaterialPresetId::Glass, "glass", 0.015f, 0.00f, 0.35f, 0.00f},
        {MaterialPresetId::Rubber, "rubber", 0.78f, 0.00f, 0.00f, 0.10f},
        {MaterialPresetId::Carbon, "carbon", 0.14f, 0.25f, 0.95f, 0.20f},
        {MaterialPresetId::Chrome, "chrome", 0.035f, 1.00f, 0.00f, 0.08f},
        {MaterialPresetId::BrushedMetal, "brushed_metal", 0.20f, 1.00f, 0.00f, 0.35f},
        {MaterialPresetId::Leather, "leather", 0.58f, 0.00f, 0.00f, 0.50f},
        {MaterialPresetId::Plastic, "plastic", 0.42f, 0.00f, 0.00f, 0.50f},
        // Lacquered body: low roughness + full clearcoat for wet showroom look
        {MaterialPresetId::Paint, "paint", 0.07f, 0.00f, 1.00f, 0.12f},
        {MaterialPresetId::Default, "default", 0.32f, 0.00f, 0.00f, 1.00f},
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

inline float materialClamp(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}

/** Preset by its name, nullptr when the string names nothing known. */
inline const MaterialPreset* materialPresetByName(const std::string& presetName) {
    std::size_t count = 0;
    const MaterialPreset* table = materialPresetTable(count);
    const std::string wanted = materialLower(presetName);
    for (std::size_t i = 0; i < count; ++i)
        if (wanted == table[i].name) return &table[i];
    return nullptr;
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
    if (materialMentions(shader, "brakedisc") || materialMentions(name, "brakedisc") ||
        materialMentions(name, "brake_disc") ||
        (materialMentions(name, "disc") && materialMentions(name, "brake")))
        return materialPreset(MaterialPresetId::BrakeDisc);
    if (materialMentions(name, "caliper") || materialMentions(name, "calliper"))
        return materialPreset(MaterialPresetId::Paint); // often body-coloured

    // Glass and light covers share the shader family in AC content.
    if (materialMentions(shader, "windscreen") || materialMentions(shader, "glass") ||
        materialMentions(name, "glass") || materialMentions(name, "windscreen") ||
        materialMentions(name, "canopy") || materialMentions(name, "lens") ||
        materialMentions(name, "headlight") || materialMentions(name, "taillight") ||
        materialMentions(name, "rearlight") || materialMentions(name, "light_glass"))
        return materialPreset(MaterialPresetId::Glass);

    if (materialMentions(shader, "tyres") || materialMentions(name, "tyre") ||
        materialMentions(name, "tire") || materialMentions(name, "rubber") ||
        materialMentions(name, "sidewall"))
        return materialPreset(MaterialPresetId::Rubber);

    if (materialMentions(shader, "carbon") || materialMentions(name, "carbon") ||
        materialMentions(name, "cfrp") || materialMentions(name, "splitter") ||
        materialMentions(name, "diffuser") || materialMentions(name, "diveplane"))
        return materialPreset(MaterialPresetId::Carbon);

    if (materialMentions(shader, "chrome") || materialMentions(name, "chrome") ||
        materialMentions(name, "exhaust") || materialMentions(name, "tip") ||
        materialMentions(name, "mirror_chrome"))
        return materialPreset(MaterialPresetId::Chrome);

    // Interior soft goods before generic "wheel" metal (steering_wheel).
    if (materialMentions(shader, "leather") || materialMentions(name, "leather") ||
        materialMentions(name, "alcantara") || materialMentions(name, "suede") ||
        materialMentions(name, "seat") || materialMentions(name, "steering"))
        return materialPreset(MaterialPresetId::Leather);

    // Rims / bare metal — avoid matching "steering_wheel" (handled above).
    if (materialMentions(shader, "metal") || materialMentions(name, "rim") ||
        materialMentions(name, "rim_") || materialMentions(name, "alloy") ||
        materialMentions(name, "alu") ||
        (materialMentions(name, "wheel") && !materialMentions(name, "steering")))
        return materialPreset(MaterialPresetId::BrushedMetal);

    // Painted bodywork: the damage/dirt multi-map is the AC body shader, and
    // the 499P names its own panels MAIN_PAINT / EXT.
    if (materialMentions(shader, "damage_dirt") || materialMentions(shader, "carpaint") ||
        materialMentions(name, "paint") || materialMentions(name, "body") ||
        materialMentions(name, "carbody") || materialMentions(name, "livery") ||
        materialMentions(name, "ext_") || materialMentions(name, "main_paint") ||
        materialMentions(name, "wing") || materialMentions(name, "door") ||
        materialMentions(name, "bonnet") || materialMentions(name, "hood") ||
        materialMentions(name, "fender") || materialMentions(name, "bumper"))
        return materialPreset(MaterialPresetId::Paint);

    // Plastics / interior hard trim / decals / plates.
    if (materialMentions(shader, "plastic") || materialMentions(name, "plastic") ||
        materialMentions(name, "trim") || materialMentions(name, "grille") ||
        materialMentions(name, "interior") || materialMentions(name, "int_") ||
        materialMentions(name, "dash") || materialMentions(name, "console") ||
        materialMentions(name, "plate") || materialMentions(name, "sticker") ||
        materialMentions(name, "decal") || materialMentions(name, "number") ||
        materialMentions(name, "badge") || materialMentions(name, "logo"))
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
