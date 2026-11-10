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
#include <cmath>
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

/** The reference an alpha-tested material cuts with. The file's own
 *  ksAlphaRef is the authority, clamped to [0, 1]; a material that authored
 *  none cuts only the texels its mask marks FULLY transparent, never a soft
 *  alpha at some fraction.
 *
 *  The 0.5 this replaces was an invention — it is the value the AC editor
 *  shows for a NEW material, not what the file says — and it shredded every
 *  soft alpha in the Countach: its interior leather (leather.dds,
 *  leather_GREY.dds) carries a wear layer whose alpha is 98% intermediate
 *  values with not one fully opaque texel, so at 0.5 the seats and the dash
 *  top lost 73% of their texels and the white showroom floor showed through
 *  the holes. A real cut-out mask is bimodal, and cutting its exact zeros is
 *  what draws it: INT_Speaker.dds is 72.7% zeros with solid bars,
 *  INT_stitching.dds 49% zeros with opaque thread. */
inline float alphaReference(const engine::fileformat::Kn5Material& material) {
    float authored = 0.0f;
    if (!material_response_detail::firstFloat(material, "ksAlphaRef", authored) ||
        authored <= 0.0f)
        return 0.0f;
    return std::min(authored, 1.0f);
}

/** Whether the file's alpha test samples the DETAIL slot instead of the albedo.
 *
 *  AC tests the alpha of the diffuse mapping the file names, and that mapping
 *  is not always the surface colour: the Countach's seats and dash top name an
 *  occlusion bake (INT_Cockpit_OCC+.dds), so the loader moves the colour to the
 *  map that carries it and the bake to the detail slot. The ALPHA stays with
 *  the bake, and a bake is flat — there is nothing to cut. Reading the wear
 *  layer that replaced it as a mask instead turned leather.dds into a sieve:
 *  1.4% of its texels are exactly transparent, and every one of them became a
 *  pinhole the bright showroom background shone through. That is the dot
 *  pattern that shows on the Countach's windscreen: the holes are behind the
 *  glass, not on it.
 *
 *  detailGain is the signature the loader leaves behind — the bake's 1.0, where
 *  a real detail grain keeps 2.0 (MaterialResponseConstants.h). A detail slot
 *  that is not bound (scale 0) cannot carry the mask at all. */
inline bool alphaMaskFromDetail(float detailGain, float detailScale) {
    if (detailScale <= 0.0f) return false;
    return detailGain <= KS_MATERIAL_DETAIL_GAIN_BAKE + 0.5f &&
           detailGain >= KS_MATERIAL_DETAIL_GAIN_BAKE - 0.5f;
}

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
    // "nodamage" is the CLEAN variant of a damage map (Kunos ships
    // nodamage_lod0.dds as the body diffuse of the Esprit), so the word
    // "damage" inside it must not disqualify a surface the file names.
    if (folded.find("nodamage") != std::string::npos ||
        folded.find("no_damage") != std::string::npos)
        return false;
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

/** A *_Ref map is AC's reflection probe: the equirect environment the car
 *  reflects (the Countach ships HDR_111_Parking_Lot_2_Ref.dds, 320x160, on the
 *  first slot of its default material). It is never a surface colour — a
 *  material whose only mapping is one would be painted with a photograph of a
 *  parking lot instead of reflecting it. */
inline bool isReflectionProbeTexture(const std::string& texture) {
    const std::string folded = materialLower(texture);
    if (folded.find("_ref.") != std::string::npos ||
        folded.find("_ref_") != std::string::npos)
        return true;
    return folded.find("reflection") != std::string::npos;
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
/** A *_OCC map is a baked occlusion pass, not a colour: the Countach cockpit
 *  names pure greyscale (alpha 0) maps as txDiffuse and keeps the leather,
 *  carpet and plastic colours in txDetail. Painting the bake can only ever
 *  produce grey, which is what made the cabin read as bare plastic. */
inline bool isOcclusionTexture(const std::string& name) {
    std::string folded;
    folded.reserve(name.size());
    for (char c : name)
        folded.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
    return folded.find("occ") != std::string::npos;
}

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
/** AC authors ksDiffuse as a scalar multiplier on the diffuse map, not a
 *  colour: the file stores it in the first float and leaves Y/Z at zero.
 *  Reading those three floats as RGB turned every untextured part into a
 *  dark red panel (0.4, 0, 0). A file that really wrote a colour keeps it. */
inline void diffuseFactors(const float authored[3], float out[3]) {
    const float y = authored[1] < 0.0f ? -authored[1] : authored[1];
    const float z = authored[2] < 0.0f ? -authored[2] : authored[2];
    const bool scalar = y < 1e-4f && z < 1e-4f;
    out[0] = authored[0];
    out[1] = scalar ? authored[0] : authored[1];
    out[2] = scalar ? authored[0] : authored[2];
}

inline float f0Scale(float specular) {
    return std::min(std::max(specular * static_cast<float>(KS_MATERIAL_SPECULAR_F0_SCALE),
                             static_cast<float>(KS_MATERIAL_SPECULAR_F0_MIN)),
                    static_cast<float>(KS_MATERIAL_SPECULAR_F0_MAX));
}

/** The authored response as the GBuffer carries it: the coverage MAGNITUDE,
 *  so the sign stays free for the brake-disc flag. ksSpecular and the authored
 *  fresnel cap are packed as two integer levels (see the constants header),
 *  magnitude = 1 + (specLevel * 17 + fresnelLevel) * step. A quantisation of
 *  1/16 is invisible on a reflectance, and 0.5 still decodes to exactly 0.5. */
inline float coverageLevel(float value) {
    const float clamped = std::min(std::max(value, 0.0f), 1.0f);
    return std::floor(clamped * static_cast<float>(KS_MATERIAL_COVERAGE_LEVEL_MAX) + 0.5f);
}

inline float coverageMagnitude(float specular, float fresnelMaxLevel = 0.0f) {
    return 1.0f +
           (coverageLevel(specular) *
                static_cast<float>(KS_MATERIAL_COVERAGE_FRESNEL_BASE) +
            coverageLevel(fresnelMaxLevel)) *
               static_cast<float>(KS_MATERIAL_COVERAGE_STEP);
}

namespace material_coverage_detail {

/** The two integer levels a stored magnitude holds, as floats. */
inline void decodeLevels(float storedCoverage, float levels[2]) {
    const float magnitude =
        storedCoverage < 0.0f ? -storedCoverage : storedCoverage;
    const float packed =
        (magnitude - 1.0f) / static_cast<float>(KS_MATERIAL_COVERAGE_STEP);
    const float fresnel =
        std::floor(std::fmod(packed,
                             static_cast<float>(KS_MATERIAL_COVERAGE_FRESNEL_BASE)) +
                   0.5f);
    levels[1] = fresnel;
    levels[0] =
        std::floor((packed - fresnel) /
                       static_cast<float>(KS_MATERIAL_COVERAGE_FRESNEL_BASE) +
                   0.5f);
}

} // namespace material_coverage_detail

/** Inverse of coverageMagnitude(): the specular the stored value encodes. */
inline float specularFromCoverage(float storedCoverage) {
    float levels[2] = {0.0f, 0.0f};
    material_coverage_detail::decodeLevels(storedCoverage, levels);
    return std::min(levels[0] / static_cast<float>(KS_MATERIAL_COVERAGE_LEVEL_MAX), 1.0f);
}

/** The fresnel cap the same stored value encodes (0 when none was authored). */
inline float fresnelMaxFromCoverage(float storedCoverage) {
    float levels[2] = {0.0f, 0.0f};
    material_coverage_detail::decodeLevels(storedCoverage, levels);
    return std::min(levels[1] / static_cast<float>(KS_MATERIAL_COVERAGE_LEVEL_MAX), 1.0f);
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
