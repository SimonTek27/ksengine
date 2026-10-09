#pragma once

// Roadmap 2.3 — reader for materials.txt, the optional sidecar the kn5baker
// tool writes next to manifest.txt, one row per baked mesh:
//
//     mesh_name \t albedo_tex \t roughness \t metalness \t normal_tex \n
//
// Rendering brief P1: the roughness and metalness cells are dual-typed —
// either a scalar in [0,1] (the Roadmap 2.3 format, still what every old
// bake ships) or a *texture name* (a cell that does not parse as a finite
// number and contains a '.', e.g. "body_rough.dds"). With a map authored
// the stored scalar becomes 1.0, the identity multiplier the shaders use:
//
//     finalRough = material.roughness * texture(roughnessMap, uv).r
//
// so a white 1×1 fallback (no map, or a missing file) leaves legacy rows
// byte-identical while a real map carries absolute values. Row format and
// the NMS2 mesh header did NOT change — old bakes keep parsing exactly as
// before, extra care is therefore not needed for cache invalidation.
//
// Degradation rules, in order of importance:
//   - a missing or unreadable file is NOT an error for the scene: load()
//     returns false and find() always returns nullptr, so the renderer falls
//     back to per-mesh defaults (roughness 0.35, metalness 0, no textures);
//   - a readable file with garbage rows skips just those rows (counted in
//     skippedRows()) instead of dropping the whole table;
//   - out-of-range scalar PBR values clamp to [0,1]; a non-numeric cell
//     without a '.' ("NOTAFLOAT") is legacy garbage and keeps the per-field
//     default — only dotted names are taken as maps, and a map file that
//     does not exist resolves to TextureRuntime's white fallback.
//
// Header-only on purpose: like LodWindow.h and PitStrategyBridge.h this is
// pure Qt-free logic, so tests/ksengine/material_cache_test.cpp includes it
// directly without compiling a simulator TU.

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>

namespace ks::sim {

// PBR properties for one baked mesh. Values mirror Kn5Baker::writeMaterialsTxt
// defaults: paint-like heuristic (roughness 0.35, non-metallic) until an
// authored row says otherwise.
struct MeshMaterial {
    // Texture names exactly as stored in the KN5 material mappings (empty =
    // none authored). Resolve with resolveTexturePath() against the bake's
    // textures/ directory before handing them to TextureRuntime.
    std::string albedo;
    std::string normal;
    // P1 — optional PBR map names from the dual-typed cells ("" = the cell
    // held a scalar and the map path stays empty).
    std::string roughnessMap;
    std::string metalnessMap;
    // Scalar PBR values. Meaning per cell (see load()):
    //   - map authored  -> this is 1.0, the identity multiplier: the shader
    //     computes material.roughness * map.r, so the map carries absolute
    //     values and a white fallback degrades to them;
    //   - no map        -> the authored scalar (or the default below).
    float roughness = 0.35f;
    float metalness = 0.0f;
    // True when a materials.txt row supplied these values; false = defaults
    // (the renderer treats that as "no authored material" for logging).
    bool authored = false;
};

class MaterialCache final {
public:
    // Returns false when the file cannot be opened. A readable but empty
    // file is a success with zero entries. Replaces any previous table.
    bool load(const std::string& path) {
        m_entries.clear();
        m_skippedRows = 0;
        m_error.clear();

        std::ifstream file(path);
        if (!file.is_open()) {
            m_error = "cannot open " + path;
            return false;
        }

        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back(); // CRLF bake
            if (line.empty() || line[0] == '#') continue;

            std::string fields[5];
            const int fieldCount = splitTabs(line, fields, 5);
            // A row must at least name the mesh; more than 5 fields means a
            // tab inside a field — a foreign/corrupt row we cannot trust.
            if (fieldCount < 1 || fields[0].empty() || fieldCount > 5) {
                ++m_skippedRows;
                continue;
            }

            MeshMaterial material;
            material.authored = true;
            if (fieldCount >= 2) material.albedo = trim(fields[1]);
            if (fieldCount >= 3) {
                applyPbrCell(trim(fields[2]), material.roughness,
                             material.roughnessMap, 0.35f);
            }
            if (fieldCount >= 4) {
                applyPbrCell(trim(fields[3]), material.metalness,
                             material.metalnessMap, 0.0f);
            }
            if (fieldCount >= 5) material.normal = trim(fields[4]);

            m_entries[fields[0]] = std::move(material);
        }
        return true;
    }

    // nullptr when the mesh has no row (or no table was loaded) — callers
    // keep their own defaults in that case.
    const MeshMaterial* find(const std::string& meshName) const {
        const auto it = m_entries.find(meshName);
        return it == m_entries.end() ? nullptr : &it->second;
    }

    std::size_t size() const noexcept { return m_entries.size(); }
    std::size_t skippedRows() const noexcept { return m_skippedRows; }
    const std::string& lastError() const noexcept { return m_error; }

    // Mirrors Kn5Baker::sanitizeFileName: the baker stores DDS files under
    // textures/ with the sanitized name but writes the raw KN5 reference
    // into materials.txt, so the runtime applies the same substitution to
    // land on the file that actually exists.
    static std::string sanitizeTextureFileName(const std::string& name) {
        std::string out = name;
        for (char& c : out) {
            switch (c) {
            case '\\': case '/': case ':': case '*': case '?':
            case '"': case '<': case '>': case '|':
                c = '_';
                break;
            default:
                break;
            }
        }
        if (out.empty()) out = "unnamed";
        return out;
    }

    // "" for an empty reference (no texture authored), otherwise
    // "<textureDir>/<sanitized name>".
    static std::string resolveTexturePath(const std::string& textureDir,
                                          const std::string& texName) {
        if (texName.empty()) return std::string();
        return textureDir + "/" + sanitizeTextureFileName(texName);
    }

private:
    // Splits the row into at most maxFields tab-separated fields and returns
    // the true field count (maxFields + 1 when the row has more, so the
    // caller can reject it as malformed).
    static int splitTabs(const std::string& line, std::string* out, int maxFields) {
        int count = 0;
        std::size_t start = 0;
        while (count < maxFields) {
            const std::size_t tab = line.find('\t', start);
            if (tab == std::string::npos) {
                out[count++] = line.substr(start);
                return count;
            }
            out[count++] = line.substr(start, tab - start);
            start = tab + 1;
        }
        // Every slot was filled by consuming a tab, so a further field
        // follows (possibly empty) — more fields than the format defines.
        return count + 1;
    }

    static std::string trim(const std::string& s) {
        std::size_t b = 0, e = s.size();
        while (b < e && (s[b] == ' ' || s[b] == '\r')) ++b;
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\r')) --e;
        return s.substr(b, e - b);
    }

    // One dual-typed P1 cell. Outcomes, in order:
    //   - empty                     -> scalar keeps the per-field default;
    //   - full-string finite number  -> classic scalar, clamped to [0,1]
    //     (non-finite "nan"/"inf" full parses keep the default, they would
    //     otherwise slip past clamp01);
    //   - anything else *without* a '.' is legacy garbage ("NOTAFLOAT")
    //     and also keeps the default — the Roadmap 2.3 degradation, so
    //     old bakes behave exactly as before;
    //   - anything else with a '.' is a map texture name: the scalar
    //     becomes 1.0, the identity multiplier the shaders use
    //     (material.roughness * map.r, white fallback = *1).
    static void applyPbrCell(const std::string& cell, float& scalar,
                             std::string& map, float fallback) {
        if (cell.empty()) { scalar = fallback; return; }
        char* end = nullptr;
        const float value = std::strtof(cell.c_str(), &end);
        if (end != cell.c_str() && *end == '\0') {
            scalar = std::isfinite(value) ? clamp01(value) : fallback;
            return;
        }
        if (cell.find('.') == std::string::npos) { scalar = fallback; return; }
        map = cell;
        scalar = 1.0f;
    }

    static float clamp01(float v) {
        if (v < 0.0f) return 0.0f;
        if (v > 1.0f) return 1.0f;
        return v;
    }

    std::unordered_map<std::string, MeshMaterial> m_entries;
    std::size_t m_skippedRows = 0;
    std::string m_error;
};

} // namespace ks::sim
