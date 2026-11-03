#pragma once
/**
 * The nodes the content hides.
 *
 * Two files name them, and both end up here:
 *   - a car's documents (model.json, driver.json) carry the car-level
 *     EXT_CONFIG.HIDDEN_NODES, from the car-root ext_config.ini;
 *   - the active skin's skins/<id>/skin.json carries what that skin hides, split
 *     into its MODEL and DRIVER blocks.
 *
 * AC writes those names in HIDE lists, wildcards included (KERS_LEVEL_? is every
 * KERS level). This header answers the only question the renderer asks: does
 * this node name match something the loaded content hides?
 *
 * Matching is the one AC uses: case-insensitive, '?' is exactly one character,
 * '*' is any run of them. A hidden node takes its children with it, which is
 * what hiding a group in the editor means.
 */
#include "FileFormat/Json.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace ks {
namespace graphics {

inline char foldNodeChar(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

/** Case-insensitive glob match: '?' one character, '*' any run. */
inline bool nodeNameMatches(const std::string& pattern, const std::string& name) {
    std::size_t p = 0;
    std::size_t n = 0;
    std::size_t star = std::string::npos;
    std::size_t resume = 0;
    while (n < name.size()) {
        if (p < pattern.size() && pattern[p] != '*' &&
            (pattern[p] == '?' || foldNodeChar(pattern[p]) == foldNodeChar(name[n]))) {
            ++p;
            ++n;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            resume = n;
        } else if (star != std::string::npos) {
            p = star + 1;
            n = ++resume;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

/** Appends the strings of a JSON array of node names, skipping repeats. */
inline void appendNodeList(const Json& list, std::vector<std::string>& out) {
    if (!list.isArray()) return;
    for (const Json& node : list) {
        if (!node.isString()) continue;
        const std::string name = node.toString();
        if (name.empty()) continue;
        if (std::find(out.begin(), out.end(), name) != out.end()) continue;
        out.push_back(name);
    }
}

/** A block that carries HIDDEN_NODES: EXT_CONFIG, or a skin's MODEL/DRIVER. */
inline void appendBlockHiddenNodes(const Json& block, std::vector<std::string>& out) {
    if (!block.isObject() || !block.contains("HIDDEN_NODES")) return;
    appendNodeList(block["HIDDEN_NODES"], out);
}

/** The car-level patterns of a document (model.json or driver.json). */
inline void appendHiddenNodes(const Json& document, std::vector<std::string>& out) {
    if (!document.isObject() || !document.contains("EXT_CONFIG")) return;
    appendBlockHiddenNodes(document["EXT_CONFIG"], out);
}

/** The hidden-node set of what is loaded, and the test the walk asks for each node. */
struct HiddenNodes {
    std::vector<std::string> patterns;

    bool empty() const { return patterns.empty(); }
    std::size_t size() const { return patterns.size(); }

    void add(const std::string& pattern) {
        if (pattern.empty()) return;
        if (std::find(patterns.begin(), patterns.end(), pattern) != patterns.end()) return;
        patterns.push_back(pattern);
    }

    /** Car-level patterns: model.json or driver.json. */
    void addDocument(const Json& document) { appendHiddenNodes(document, patterns); }

    /** The active skin's patterns: skins/<id>/skin.json. */
    void addSkinDocument(const Json& skin) {
        if (!skin.isObject()) return;
        if (skin.contains("MODEL")) appendBlockHiddenNodes(skin["MODEL"], patterns);
        if (skin.contains("DRIVER")) appendBlockHiddenNodes(skin["DRIVER"], patterns);
    }

    /** True when the content hides this node name. */
    bool hides(const std::string& name) const {
        for (const std::string& pattern : patterns)
            if (nodeNameMatches(pattern, name)) return true;
        return false;
    }
};

} // namespace graphics
} // namespace ks
