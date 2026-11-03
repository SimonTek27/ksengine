#pragma once
/**
 * The nodes a skin hides.
 *
 * A skin's ext_config.ini hides helper nodes the model must not show: the
 * driver's own head inside the helmet, the KERS level digits, the fuel and
 * high-beam warnings, the brake-disc hub decals. AC writes those names in
 * HIDE lists, wildcards included (KERS_LEVEL_? is every KERS level), and the
 * converter folds them into the car's documents as EXT_CONFIG.HIDDEN_NODES.
 * This header answers the only question the renderer asks: does this node name
 * match something the content hides?
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

/** The patterns a document carries under EXT_CONFIG.HIDDEN_NODES. */
inline void appendHiddenNodes(const Json& document, std::vector<std::string>& out) {
    if (!document.isObject() || !document.contains("EXT_CONFIG")) return;
    const Json& block = document["EXT_CONFIG"];
    if (!block.isObject() || !block.contains("HIDDEN_NODES")) return;
    const Json& nodes = block["HIDDEN_NODES"];
    if (!nodes.isArray()) return;
    for (const Json& node : nodes) {
        if (!node.isString()) continue;
        const std::string name = node.toString();
        if (name.empty()) continue;
        if (std::find(out.begin(), out.end(), name) != out.end()) continue;
        out.push_back(name);
    }
}

/** The hidden-node set of a car, and the test the walk asks for each node. */
struct HiddenNodes {
    std::vector<std::string> patterns;

    bool empty() const { return patterns.empty(); }
    std::size_t size() const { return patterns.size(); }

    void add(const std::string& pattern) {
        if (pattern.empty()) return;
        if (std::find(patterns.begin(), patterns.end(), pattern) != patterns.end()) return;
        patterns.push_back(pattern);
    }

    void addDocument(const Json& document) { appendHiddenNodes(document, patterns); }

    /** True when the content hides this node name. */
    bool hides(const std::string& name) const {
        for (const std::string& pattern : patterns)
            if (nodeNameMatches(pattern, name)) return true;
        return false;
    }
};

} // namespace graphics
} // namespace ks
