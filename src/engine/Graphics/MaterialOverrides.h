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
 *       "DISCS":       { "metalness": 1.0, "clearcoat": 0.0 }
 *     }
 *   }
 *
 * Keys are the material names as the KN5 spells them (matched case-insensitively).
 * An entry may name a preset, pin any of the three numbers, or both — a preset
 * is applied first, then the explicit numbers on top. Nothing else changes:
 * alpha flags, textures and emissive still come from the file.
 *
 * The file is optional and never fatal: a broken one is reported and ignored,
 * and the model loads with the preset alone. KS_MATERIAL_PRESETS points at a
 * different file, which is how a table can be tried without touching content.
 */
#include "engine/Config/Json.h"
#include "engine/Graphics/MaterialPresets.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <system_error>

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

class MaterialOverrides {
public:
    /** Parse the file's text. Clears whatever was loaded before. Returns false
     *  (and leaves the object empty) when the JSON is not usable. */
    bool parse(const std::string& text, std::string* error = nullptr) {
        m_entries.clear();
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
            if (error) *error = "no \"materials\" object";
            return false;
        }
        if (!materials->isObject()) {
            if (error) *error = "\"materials\" is not an object";
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
            m_entries[materialLower(materials->keys()[i])] = parsed;
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

    bool empty() const { return m_entries.empty(); }
    std::size_t size() const { return m_entries.size(); }
    const std::string& schema() const { return m_schema; }
    const std::string& source() const { return m_path; }

    const MaterialOverrideEntry* find(const std::string& materialName) const {
        const auto it = m_entries.find(materialLower(materialName));
        return it == m_entries.end() ? nullptr : &it->second;
    }

    /** The preset that ends up in force, which is what the loader counts. */
    MaterialPresetId presetFor(const std::string& shaderName,
                               const std::string& materialName) const {
        if (const MaterialOverrideEntry* entry = find(materialName);
            entry != nullptr && entry->hasPreset)
            return entry->preset;
        return resolveMaterialPreset(shaderName, materialName).id;
    }

    /** Base preset from the names, then the file's preset, then its numbers. */
    MaterialSurface resolve(const std::string& shaderName, const std::string& materialName,
                            float authoredRoughness) const {
        MaterialSurface surface = applyMaterialPreset(
            resolveMaterialPreset(shaderName, materialName), authoredRoughness);

        const MaterialOverrideEntry* entry = find(materialName);
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
    std::map<std::string, MaterialOverrideEntry> m_entries; // keys lowercased
    std::string m_schema;
    std::string m_path;
};

} // namespace graphics
} // namespace ks
