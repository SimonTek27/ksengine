#pragma once
/**
 * Game strings (Qt-free): system/i18n/<lang>.json.
 *
 * ksEditor's translations are Qt Linguist files (src/i18n/*.ts) loaded by a
 * QTranslator the Qt-free runtime cannot use, so ksim keeps its strings in
 * JSON next to the rest of the installed data:
 *
 *   system/i18n/en.json   the English table — the fallback for every key
 *   system/i18n/it.json   Italian (partial files are fine, see load())
 *
 * A table is a string map; nested objects are flattened to dot paths, so
 * {"menu": {"quit": {"label": "QUIT"}}} is the key "menu.quit.label". The
 * requested language is loaded on top of English, so a translation only has
 * to carry what it changes. Nothing here keeps static state: the caller owns
 * the Locale (ksim holds one at file scope) and hands the pointer to whoever
 * renders text, so the DLL and the executable never share a singleton.
 *
 * The lookup always answers something usable: unknown key, empty translation
 * or no table at all -> the caller's English literal. A broken install can
 * therefore never blank the menu.
 */
#include "Json.h"
#include "assets/UserData.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace ks {
namespace engine {
namespace config {

class Locale {
public:
    /** "it-IT" / "IT_it" / "IT" -> "it"; anything that is not 2-3 ASCII
     *  letters becomes "en" (the table that always answers). */
    static std::string normalize(const std::string& lang) {
        std::string code;
        for (char c : lang) {
            if (c == '-' || c == '_') break;
            code += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (code.size() < 2 || code.size() > 3) return "en";
        for (char c : code)
            if (c < 'a' || c > 'z') return "en";
        return code;
    }

    /** KS_LANG (an explicit choice, and what the tests use) beats the Windows
     *  USERLANG, which beats LANG; anything absent or unusable -> "en". */
    static std::string detectLanguage() {
        if (const char* lang = std::getenv("KS_LANG"); lang && lang[0])
            return normalize(lang);
        if (const char* user = std::getenv("USERLANG"); user && user[0])
            return normalize(user);
        if (const char* lang = std::getenv("LANG"); lang && lang[0])
            return normalize(lang);
        return "en";
    }

    /**
     * Read <dir>/en.json and then <dir>/<lang>.json on top of it, so the
     * English table covers every key a translation does not carry. False
     * only when *neither* file could be read (a machine without system/i18n
     * keeps working: every lookup falls back to the caller's literal).
     */
    bool load(const std::string& dir, const std::string& lang) {
        m_strings.clear();
        m_language = normalize(lang);
        m_keys = 0;
        const bool base = mergeFile(dir + "/en.json");
        bool localized = false;
        if (m_language != "en") localized = mergeFile(dir + "/" + m_language + ".json");
        m_keys = m_strings.size();
        return base || localized;
    }

    /** Language actually in use ("en" when nothing could be read). */
    const std::string& language() const { return m_language; }
    /** Number of strings loaded (English + overrides). */
    size_t size() const { return m_keys; }

    /** Translation for `key`, or "" when the tables do not carry it. */
    std::string tr(const std::string& key) const {
        const auto it = m_strings.find(key);
        return it == m_strings.end() ? std::string() : it->second;
    }
    /** Translation for `key`, or `fallback` when there is none. An empty
     *  value in a table counts as "not translated" — a partially edited
     *  file can never blank the UI. */
    std::string tr(const std::string& key, const std::string& fallback) const {
        const auto it = m_strings.find(key);
        if (it == m_strings.end() || it->second.empty()) return fallback;
        return it->second;
    }

private:
    /** Flatten a table into the map: objects become dot paths, only strings
     *  survive (numbers/bools are not game text). */
    static void flatten(const json::Value& node, const std::string& prefix,
                        std::map<std::string, std::string>& out) {
        if (node.isObject()) {
            for (const std::string& key : node.keys()) {
                const json::Value* child = node.find(key);
                if (!child) continue;
                flatten(*child, prefix.empty() ? key : prefix + "." + key, out);
            }
            return;
        }
        if (node.isString() && !prefix.empty()) out[prefix] = node.asString();
    }

    /** One table on top of whatever is loaded. */
    bool mergeFile(const std::string& path) {
        std::string text;
        if (!assets::UserData::readFile(path, text)) return false;
        json::Value root;
        std::string error;
        if (!json::parse(text, root, &error)) {
            std::fprintf(stderr, "Locale: %s: %s\n", path.c_str(), error.c_str());
            return false;
        }
        flatten(root, std::string(), m_strings);
        return true;
    }

    std::map<std::string, std::string> m_strings;
    std::string m_language = "en";
    size_t m_keys = 0;
};

} // namespace config
} // namespace engine
} // namespace ks
