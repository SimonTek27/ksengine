#pragma once
/**
 * Per-car overrides for the material presets.
 *
 * The preset table decides what a surface looks like from its names, which is
 * right for most content and wrong for the car whose author polished the
 * chrome a little harder than any generic rule can guess. This lets a car say
 * so itself, without a rebuild:
 *
 *   content/cars/<car>/material_presets.json
 *
 *   {
 *     "schema": "ksengine.material_presets/1",
 *     "materials": {
 *       "MAIN_PAINT":  { "roughness": 0.06 },
 *       "EXT_CHROME":  { "preset": "chrome", "roughness": 0.03 },
 *       "DISCS":       { "metalness": 1.0, "clearcoat": 0.0 },
 *       "DECAL_*":     { "roughness": 0.7 },
 *       "*_GLASS":     { "preset": "glass" }
 *     }
 *   }
 *
 * Keys are the material names as the KN5 spells them, matched case-insensitively.
 * `*` matches any run of characters and `?` a single one, so a family of twenty
 * numbered decals is one line. An entry may name a preset, pin any of the three
 * numbers, or both — a preset is applied first, then the explicit numbers on
 * top. Nothing else changes: alpha flags, textures and emissive still come from
 * the file.
 *
 * Precedence: an exact key beats every pattern; among patterns the one with the
 * most literal characters wins, and ties keep the order the file wrote them in.
 * That makes `DECAL_07` beat `DECAL_*` beat `*` deterministically, instead of
 * depending on how a hash map happened to iterate.
 *
 * The file is optional and never fatal: a broken one is reported and ignored,
 * and the model loads with the preset alone. KS_MATERIAL_PRESETS points at a
 * different file, which is how a table can be tried without touching content.
 */
#include "engine/Config/Json.h"
#include "engine/Graphics/MaterialPresets.h"

#include <cstddef>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace ks {
namespace graphics {

struct MaterialOverrideEntry {
    bool hasPreset = false;
    MaterialPresetId preset = MaterialPresetId::Default;
    bool hasRoughness = false;
    float roughness = 0.0f;
    bool hasMetalness = false;
    float metalness = 0.0f;
    bool hasClearcoat = false;
    float clearcoat = 0.0f;
};

/** Glob match for `*` (any run) and `?` (one character), iterative so a long
 *  material name cannot blow the stack. */
inline bool materialWildcardMatch(const std::string& pattern, const std::string& text) {
    std::size_t p = 0;
    std::size_t t = 0;
    std::size_t star = std::string::npos;
    std::size_t mark = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = t;
        } else if (star != std::string::npos) {
            p = star + 1;
            t = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

class MaterialOverrides {
public:
    /** Parse the file's text. Clears whatever was loaded before. Returns false
     *  (and leaves the object empty) when the JSON is not usable. */
    bool parse(const std::string& text, std::string* error = nullptr) {
        m_exact.clear();
        m_patterns.clear();
        m_schema.clear();

        ::ks::engine::json::Value root;
        if (!::ks::engine::json::parse(text, root, error)) return false;
        if (!root.isObject()) {
            if (error) *error = "material_presets.json is not a JSON object";
            return false;
        }
        m_schema = root.stringAt("schema", std::string());

        const ::ks::engine::json::Value* materials = root.find("materials");
        if (materials == nullptr) {
            if (error) *error = "no materials object";
            return false;
        }
        if (!materials->isObject()) {
            if (error) *error = "materials is not an object";
            return false;
        }

        for (std::size_t i = 0; i < materials->size(); ++i) {
            const ::ks::engine::json::Value* entry = materials->item(i);
            if (entry == nullptr || !entry->isObject()) continue;

            MaterialOverrideEntry parsed;
            const std::string presetName = entry->stringAt("preset", std::string());
            if (!presetName.empty()) {
                if (const MaterialPreset* preset = materialPresetByName(presetName)) {
                    parsed.hasPreset = true;
                    parsed.preset = preset->id;
                } else if (error != nullptr) {
                    // Named but unknown: keep the rest of the entry, and say so.
                    *error = "unknown preset '" + presetName + "' for " + materials->keys()[i];
                }
            }
            if (const ::ks::engine::json::Value* v = entry->find("roughness");
                v != nullptr && v->isNumber()) {
                parsed.hasRoughness = true;
                parsed.roughness = static_cast<float>(v->asNumber());
            }
            if (const ::ks::engine::json::Value* v = entry->find("metalness");
                v != nullptr && v->isNumber()) {
                parsed.hasMetalness = true;
                parsed.metalness = static_cast<float>(v->asNumber());
            }
            if (const ::ks::engine::json::Value* v = entry->find("clearcoat");
                v != nullptr && v->isNumber()) {
                parsed.hasClearcoat = true;
                parsed.clearcoat = static_cast<float>(v->asNumber());
            }

            const std::string key = materialLower(materials->keys()[i]);
            if (key.find_first_of("*?") == std::string::npos) {
                m_exact[key] = parsed;
            } else {
                m_patterns.push_back(PatternEntry{key, parsed});
            }
        }
        return true;
    }

    bool loadFile(const std::string& path, std::string* error = nullptr) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            if (error) *error = "cannot open " + path;
            return false;
        }
        const std::string text((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
        m_path = path;
        return parse(text, error);
    }

    bool empty() const { return m_exact.empty() && m_patterns.empty(); }
    std::size_t size() const { return m_exact.size() + m_patterns.size(); }
    std::size_t patternCount() const { return m_patterns.size(); }
    const std::string& schema() const { return m_schema; }
    const std::string& source() const { return m_path; }

    /** The entry whose key equals `materialName`, ignoring case. Patterns are
     *  not consulted: this is the "did the car name this material exactly" query. */
    const MaterialOverrideEntry* find(const std::string& materialName) const {
        const auto it = m_exact.find(materialLower(materialName));
        return it == m_exact.end() ? nullptr : &it->second;
    }

    /** The entry that actually applies: exact key first, then the most specific
     *  matching pattern (ties keep file order). */
    const MaterialOverrideEntry* findEffective(const std::string& materialName) const {
        if (const MaterialOverrideEntry* exact = find(materialName)) return exact;
        const std::string key = materialLower(materialName);
        const MaterialOverrideEntry* best = nullptr;
        std::size_t bestScore = 0;
        for (const PatternEntry& candidate : m_patterns) {
            if (!materialWildcardMatch(candidate.pattern, key)) continue;
            const std::size_t score = literalCount(candidate.pattern);
            if (best == nullptr || score > bestScore) {
                best = &candidate.entry;
                bestScore = score;
            }
        }
        return best;
    }

    /** The preset that ends up in force, which is what the loader counts. */
    MaterialPresetId presetFor(const std::string& shaderName,
                               const std::string& materialName) const {
        if (const MaterialOverrideEntry* entry = findEffective(materialName);
            entry != nullptr && entry->hasPreset)
            return entry->preset;
        return resolveMaterialPreset(shaderName, materialName).id;
    }

    /** Base preset from the names, then the file's preset, then its numbers. */
    MaterialSurface resolve(const std::string& shaderName, const std::string& materialName,
                            float authoredRoughness) const {
        MaterialSurface surface = applyMaterialPreset(
            resolveMaterialPreset(shaderName, materialName), authoredRoughness);

        const MaterialOverrideEntry* entry = findEffective(materialName);
        if (entry == nullptr) return surface;
        if (entry->hasPreset)
            surface = applyMaterialPreset(materialPreset(entry->preset), authoredRoughness);
        if (entry->hasRoughness)
            surface.roughness = materialClamp(entry->roughness, 0.01f, 1.0f);
        if (entry->hasMetalness)
            surface.metalness = materialClamp(entry->metalness, 0.0f, 1.0f);
        if (entry->hasClearcoat)
            surface.clearcoat = materialClamp(entry->clearcoat, 0.0f, 1.0f);
        return surface;
    }

private:
    struct PatternEntry {
        std::string pattern; // lowercased, may hold * and ?
        MaterialOverrideEntry entry;
    };

    /** Characters a pattern insists on: the more of them, the more specific. */
    static std::size_t literalCount(const std::string& pattern) {
        std::size_t count = 0;
        for (char c : pattern)
            if (c != '*' && c != '?') ++count;
        return count;
    }

    std::map<std::string, MaterialOverrideEntry> m_exact; // keys lowercased
    std::vector<PatternEntry> m_patterns;                 // file order kept
    std::string m_schema;
    std::string m_path;
};

} // namespace graphics
} // namespace ks
