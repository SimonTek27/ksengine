#pragma once

#include "Json.h"
#include "assets/UserData.h"

#include <cstdio>
#include <string>

namespace ks {
namespace engine {
namespace config {

struct ControllerSettings {
    // The whole document. Starts as the empty shape of a Controller.ini file:
    // every value comes from what is merged in, never from a made-up profile.
    json::Value root = json::Value::object();

    bool hasRoot = false;

    ControllerSettings() {
        std::string err;
        if (!json::parse(defaultJson(), root, &err)) {
            root = json::Value::object();
        }
    }

    /** Deep-merge `incoming` into root (later wins per key). */
    void merge(const json::Value& incoming) {
        if (!incoming.isObject()) return;
        if (!root.isObject()) root = json::Value::object();
        mergeValues(root, incoming);
        hasRoot = true;
    }

    /** Forget which keys a file carried. */
    void clearPresence() { hasRoot = false; }

    bool load(const std::string& path) {
        std::string text;
        if (!assets::UserData::readFile(path, text)) return false;
        json::Value doc;
        std::string error;
        if (!json::parse(text, doc, &error)) {
            std::fprintf(stderr, "ControllerSettings: %s: %s\n", path.c_str(), error.c_str());
            return false;
        }
        merge(doc);
        return true;
    }

    json::Value toJson() const {
        return root;
    }

    bool save(const std::string& path) const {
        return assets::UserData::writeFile(path, json::dump(root));
    }

    /** Merge only the `section` object of a settings file (the shape
     *  EngineSettings reads: system/cfg/ksengine.json then the player's
     *  settings.json). False when the file or that section is missing, with
     *  the document left as it was. */
    bool loadFrom(const std::string& path, const std::string& section) {
        std::string text;
        if (!assets::UserData::readFile(path, text)) return false;
        json::Value doc;
        std::string error;
        if (!json::parse(text, doc, &error)) {
            std::fprintf(stderr, "ControllerSettings: %s: %s\n", path.c_str(), error.c_str());
            return false;
        }
        const json::Value* value = doc.find(section);
        if (value == nullptr || !value->isObject()) return false;
        merge(*value);
        return true;
    }

    /** Put the document back under `section`, preserving every other key the
     *  file already carries. The builtin _meta note is not a setting and is
     *  never written into a user file. */
    bool saveTo(const std::string& path, const std::string& section) const {
        json::Value doc = json::Value::object();
        std::string text;
        if (assets::UserData::readFile(path, text)) {
            std::string error;
            if (!json::parse(text, doc, &error)) {
                std::fprintf(stderr, "ControllerSettings: %s: %s\n", path.c_str(), error.c_str());
                return false;
            }
            if (!doc.isObject()) return false;
        }
        json::Value value = json::Value::object();
        for (const std::string& key : root.keys()) {
            if (key == "_meta") continue;
            if (const json::Value* entry = root.find(key)) value.set(key, *entry);
        }
        doc.set(section, std::move(value));
        return assets::UserData::writeFile(path, json::dump(doc));
    }

    // The built-in default document (rFactor Controller.ini shape, no values).
    static std::string defaultJson();

private:
    static void mergeValues(json::Value& dst, const json::Value& src) {
        if (src.isObject() && dst.isObject()) {
            for (size_t i = 0; i < src.keys().size(); ++i) {
                const std::string& key = src.keys()[i];
                const json::Value* sv = src.item(i);
                if (!sv) continue;
                const json::Value* dv = dst.find(key);
                if (!dv) {
                    dst.set(key, *sv);
                } else {
                    json::Value merged = *dv;
                    mergeValues(merged, *sv);
                    dst.set(key, std::move(merged));
                }
            }
        } else {
            dst = src;
        }
    }
};

// Aliases for alternate naming.
using ControlsSettings = ControllerSettings;
using ControllerConfig = ControllerSettings;
using ControlConfig = ControllerSettings;

inline bool loadControllerSettings(const std::string& path, ControllerSettings& s) { return s.load(path); }
inline bool saveControllerSettings(const std::string& path, const ControllerSettings& s) { return s.save(path); }
inline bool loadControlsSettings(const std::string& path, ControllerSettings& s) { return s.load(path); }
inline bool saveControlsSettings(const std::string& path, const ControllerSettings& s) { return s.save(path); }

} // namespace config
} // namespace engine
} // namespace ks

// Also expose in ks::sim and ks::device namespaces for include-path flexibility.
namespace ks { namespace sim {
    using ControllerSettings = ks::engine::config::ControllerSettings;
    using ControlsSettings = ks::engine::config::ControllerSettings;
    using ControllerConfig = ks::engine::config::ControllerSettings;
    using ControlConfig = ks::engine::config::ControllerSettings;
    inline bool loadControllerSettings(const std::string& p, ControllerSettings& s){ return s.load(p); }
    inline bool saveControllerSettings(const std::string& p, const ControllerSettings& s){ return s.save(p); }
} }
namespace ks { namespace device {
    using ControllerSettings = ks::engine::config::ControllerSettings;
    using ControlsSettings = ks::engine::config::ControllerSettings;
    using ControllerConfig = ks::engine::config::ControllerSettings;
    using ControlConfig = ks::engine::config::ControllerSettings;
    inline bool loadControllerSettings(const std::string& p, ControllerSettings& s){ return s.load(p); }
    inline bool saveControllerSettings(const std::string& p, const ControllerSettings& s){ return s.save(p); }
} }

// Default JSON definition (included at the bottom to keep the header readable).
#include "ControllerSettings_default.inc"
