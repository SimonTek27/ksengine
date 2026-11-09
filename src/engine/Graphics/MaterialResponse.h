#pragma once
/**
 * What a KN5 material's own properties mean to the renderer.
 *
 * The presets (MaterialPresets.h) decide what a surface IS from its shader and
 * name; the file also carries a response the presets cannot know:
 *
 *   ksSpecular        multiplier around AC's 0.5 neutral for the dielectric F0
 *                     (0.5 keeps the legacy 0.04, 1.0 doubles it, 0 halves it)
 *   fresnelC          Fresnel base reflection at normal incidence
 *   fresnelEXP        Fresnel exponent
 *   fresnelMaxLevel   Fresnel cap; 0 = the file authored none
 *   detailUVMultiplier how many times txDetail repeats over the albedo
 *
 * The loader and the GBuffer shader both need these numbers, so the rules live
 * here once: a test can check them without a GPU, and a future fourth GBuffer
 * target only has to move the encoding, not the meaning.
 *
 * Header-only and free of Qt/Vulkan, like MaterialPresets.h.
 */
#include "../FileFormat/Kn5Reader.h"
#include "MaterialPresets.h"
#include "MaterialResponseConstants.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

namespace ks {
namespace graphics {

namespace material_response_detail {

/** First float of a shader property. AC keeps each property in a 40-byte
 *  ValueA..D block and every scalar the surface model needs starts at 0. */
inline bool firstFloat(const engine::fileformat::Kn5Material& material,
                       const char* name, float& out) {
    for (const auto& property : material.properties) {
        if (property.name != name) continue;
        std::memcpy(&out, property.value.data(), sizeof(out));
        return true;
    }
    return false;
}

} // namespace material_response_detail

/** The authored surface response, with AC's neutral values when the file
 *  carries none: specular 0.5, no Fresnel, exponent 5. */
struct AuthoredSurface {
    float specular = static_cast<float>(KS_MATERIAL_SPECULAR_NEUTRAL);
    float fresnelC = 0.0f;
    float fresnelExp = static_cast<float>(KS_MATERIAL_FRESNEL_DEFAULT_EXP);
    float fresnelMax = 0.0f;
};

/** Which slot a KN5 mapping feeds. The weathering maps (damage, scratches,
 *  dirt, dust) are DATA for AC's damage/dirt shaders, not the surface: a file
 *  commonly names its damage normal txNormal=damage.dds, and feeding that to
 *  the shading normal is what makes a clean car look dented and scratched. */
enum class TextureRole {
    diffuse,
    normal,
    detail,
    other,
};

inline bool isWeatheringTexture(const std::string& texture) {
    const std::string folded = materialLower(texture);
    return folded.find("damage") != std::string::npos ||
           folded.find("scratch") != std::string::npos ||
           folded.find("dirt") != std::string::npos ||
           folded.find("dust") != std::string::npos;
}

/** A map whose NAME marks it as data rather than colour: _s (specular), _spec,
 *  _nm/_normal (normals), _ao (ambient occlusion). AC's detail slot often
 *  points at one of these (B191_CARBFBD ships 9x_carbfbd_T1_s.dds), and
 *  multiplying that into the albedo paints grime and blue speckles the livery
 *  never had. */
inline bool isDataTexture(const std::string& texture) {
    const std::string folded = materialLower(texture);
    return folded.find("_s.") != std::string::npos ||
           folded.find("_spec") != std::string::npos ||
           folded.find("_nm.") != std::string::npos ||
           folded.find("_nm_") != std::string::npos ||
           folded.find("_normal") != std::string::npos ||
           folded.find("_ao.") != std::string::npos ||
           folded.find("_ao_") != std::string::npos;
}

inline TextureRole textureRole(const std::string& mappingName,
                               const std::string& texture) {
    if (texture.empty() || isWeatheringTexture(texture)) return TextureRole::other;
    const std::string name = materialLower(mappingName);
    // "Normal" before "Detail": txNormalDetail is a normal map, not colour.
    if (name.find("normal") != std::string::npos) return TextureRole::normal;
    if (name.find("diffuse") != std::string::npos) return TextureRole::diffuse;
    if (name.find("detail") != std::string::npos)
        return isDataTexture(texture) ? TextureRole::other : TextureRole::detail;
    return TextureRole::other;
}

/** AC samples txDetail only when the material asks for it (useDetail). A map
 *  that ships without the flag is data for another shader, and multiplying it
 *  into the albedo reads as grime. A file that never wrote the flag keeps the
 *  historical behaviour. */
inline bool usesDetail(const engine::fileformat::Kn5Material& material) {
    float detail = 0.0f;
    if (material_response_detail::firstFloat(material, "useDetail", detail))
        return detail >= 0.5f;
    return true;
}

inline AuthoredSurface authoredSurface(const engine::fileformat::Kn5Material& material) {
    AuthoredSurface surface;
    float value = 0.0f;
    if (material_response_detail::firstFloat(material, "ksSpecular", value))
        surface.specular = value;
    if (material_response_detail::firstFloat(material, "fresnelC", value))
        surface.fresnelC = value;
    // A zero or negative exponent would make the Fresnel curve constant.
    if (material_response_detail::firstFloat(material, "fresnelEXP", value) && value > 0.0f)
        surface.fresnelExp = value;
    if (material_response_detail::firstFloat(material, "fresnelMaxLevel", value))
        surface.fresnelMax = value;
    return surface;
}

/** Detail-map repeats. AC authors detailUVMultiplier; ksDetailU was only ever
 *  a guess and is kept as a fallback. 0 = no authored tiling. */
inline float authoredDetailTiles(const engine::fileformat::Kn5Material& material) {
    float tiles = 0.0f;
    if (material_response_detail::firstFloat(material, "detailUVMultiplier", tiles) &&
        tiles > 0.0f)
        return tiles;
    if (material_response_detail::firstFloat(material, "ksDetailU", tiles) && tiles > 0.0f)
        return tiles;
    return 0.0f;
}

/** ksSpecular -> dielectric F0 scale. 0.5 is AC's neutral and the legacy look
 *  (0.04); the clamp keeps a broken value from killing or blowing the reply. */
inline float f0Scale(float specular) {
    return std::min(std::max(specular * static_cast<float>(KS_MATERIAL_SPECULAR_F0_SCALE),
                             static_cast<float>(KS_MATERIAL_SPECULAR_F0_MIN)),
                    static_cast<float>(KS_MATERIAL_SPECULAR_F0_MAX));
}

/** The specular as the GBuffer carries it: the coverage MAGNITUDE, so the
 *  sign stays free for the brake-disc flag. 0.5 -> exactly 1.0 and every value
 *  stays in [0.75, 1.25], above the abs(w) < 0.5 "no geometry" test. */
inline float coverageMagnitude(float specular) {
    const float clamped = std::min(std::max(specular, 0.0f), 1.0f);
    return 1.0f + (clamped - static_cast<float>(KS_MATERIAL_SPECULAR_NEUTRAL)) *
                      static_cast<float>(KS_MATERIAL_COVERAGE_STEP);
}

/** Inverse of coverageMagnitude(), safe on the signed value the GBuffer stores. */
inline float specularFromCoverage(float storedCoverage) {
    const float magnitude =
        storedCoverage < 0.0f ? -storedCoverage : storedCoverage;
    return std::min(
        std::max((magnitude - 1.0f) /
                         static_cast<float>(KS_MATERIAL_COVERAGE_STEP) +
                     static_cast<float>(KS_MATERIAL_SPECULAR_NEUTRAL),
                 0.0f),
        1.0f);
}

/** A blended surface is a pane when the preset already says glass, or when the
 *  file paints it with a reflection/windscreen shader and authored a Fresnel
 *  cap. A decal blends but has neither, so it keeps its own alpha. */
inline bool isGlassPane(const std::string& shader, bool presetGlass, float fresnelMax,
                        std::uint8_t blend) {
    if (presetGlass) return true;
    if (blend == 0 || fresnelMax <= 0.0f) return false;
    const std::string folded = materialLower(shader);
    return folded.find(KS_MATERIAL_GLASS_SHADER_REFLECTION) != std::string::npos ||
           folded.find(KS_MATERIAL_GLASS_SHADER_WINDSCREEN) != std::string::npos;
}

} // namespace graphics
} // namespace ks
