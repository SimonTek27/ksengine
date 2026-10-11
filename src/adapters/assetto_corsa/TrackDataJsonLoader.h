#pragma once
/**
 * TrackDataJsonLoader — native track manifest (roadmap 2.7 / 2.10).
 *
 * Assetto Corsa stores a small JSON manifest next to the track model:
 *
 *     <track>/data/data.json
 *     {
 *         "name": "Silverstone",
 *         "length": 5891,
 *         "width": 12.0,
 *         ...
 *     }
 *
 * SimulationLoop::loadTrackFolder prefers it over surfaces.ini so the sim
 * gets the track's display name and lap length without an AC-specific
 * parser; surfaces.ini stays the compatibility path when the manifest is
 * absent or unusable (see the call site).
 *
 * Header-only on purpose (like MaterialCache.h / SetupJson.h): the loader is
 * a string scan, needs no JSON library and is compiled with whoever includes
 * it, so no CMake source-list surgery is required.
 */
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace ks {
namespace ac {

struct TrackMeta {
    std::string name;   ///< display name ("" when the manifest has none)
    float lengthM = 0.f; ///< lap length in metres (0 = unknown)
    float widthM = 0.f;  ///< track width in metres (0 = unknown)
};

class TrackDataJsonLoader {
public:
    const TrackMeta& meta() const { return m_meta; }
    bool loaded() const { return m_loaded; }

    /**
     * Load <trackDir>/data/data.json, falling back to <trackDir>/data.json.
     * @return true when a manifest was found and parsed.
     */
    bool loadForTrackDir(const std::string& trackDir) {
        namespace fs = std::filesystem;
        const fs::path root(trackDir);
        const fs::path candidates[] = {root / "data" / "data.json",
                                       root / "data.json"};
        for (const fs::path& c : candidates) {
            std::error_code ec;
            if (!fs::is_regular_file(c, ec)) continue;
            if (loadFile(c.string())) return true;
        }
        return false;
    }

    /** Parse one manifest file. Resets the previous metadata. */
    bool loadFile(const std::string& path) {
        std::ifstream in(path);
        if (!in) return false;
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        if (text.find('{') == std::string::npos) return false; // not JSON

        m_meta = TrackMeta{};
        m_meta.name = jsonText(text, "name");
        m_meta.lengthM = jsonNumber(text, "length");
        m_meta.widthM = jsonNumber(text, "width");
        // A manifest without a name and without a length carries nothing the
        // caller can use: treat it as absent so surfaces.ini still runs.
        m_loaded = !m_meta.name.empty() || m_meta.lengthM > 1.f;
        return m_loaded;
    }

private:
    // Minimal "key": value scan — same approach (and same limits) as
    // src/simulator/SetupJson.h; no nesting, no escapes, one level deep.
    static size_t valuePos(const std::string& text, const std::string& key) {
        const std::string needle = "\"" + key + "\"";
        size_t pos = text.find(needle);
        if (pos == std::string::npos) return std::string::npos;
        pos = text.find(':', pos + needle.size());
        if (pos == std::string::npos) return std::string::npos;
        ++pos;
        while (pos < text.size() &&
               (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' ||
                text[pos] == '\r'))
            ++pos;
        return pos;
    }

    static std::string jsonText(const std::string& text, const std::string& key) {
        size_t pos = valuePos(text, key);
        if (pos == std::string::npos || pos >= text.size() || text[pos] != '"')
            return std::string();
        ++pos;
        std::string out;
        while (pos < text.size() && text[pos] != '"') {
            if (text[pos] == '\\' && pos + 1 < text.size()) ++pos; // \" \n …
            out.push_back(text[pos]);
            ++pos;
        }
        return out;
    }

    static float jsonNumber(const std::string& text, const std::string& key) {
        size_t pos = valuePos(text, key);
        if (pos == std::string::npos || pos >= text.size()) return 0.f;
        if (!(std::isdigit(static_cast<unsigned char>(text[pos])) ||
              text[pos] == '-' || text[pos] == '+'))
            return 0.f;
        const char* begin = text.c_str() + pos;
        char* end = nullptr;
        const float v = std::strtof(begin, &end);
        if (end == begin) return 0.f;
        return v < 0.f ? 0.f : v; // a negative length/width is a parse artefact
    }

    TrackMeta m_meta;
    bool m_loaded = false;
};

} // namespace ac
} // namespace ks
