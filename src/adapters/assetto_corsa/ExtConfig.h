#pragma once
/**
 * AC's ext_config.ini, folded into the car's model document.
 *
 * A skin folder carries ext_config.ini, and that file changes the model more
 * than any other piece of content: it names the driver model, lists the nodes a
 * skin hides (DRIVER3D_HIDE_OBJECT_*) and rewrites whole node sets
 * (MODEL_REPLACEMENT_*). ksim read none of it, so a car kept its default driver
 * and every helper mesh the skin wanted gone.
 *
 * This header reads the file (the car-root one and every skins/<id>/ one) and
 * merges it into data/model.json, which is the document that already describes
 * the model. Nothing is invented: sections are copied verbatim, and the only
 * derived entry is HIDDEN_NODES, the deduplicated union of the names the file
 * asks to hide, wildcards included (KERS_LEVEL_? stays a wildcard).
 *
 * Layout written into model.json:
 *
 *   "EXT_CONFIG": {
 *     "HIDDEN_NODES": ["DRIVER:HELMET", "KERS_LEVEL_?", ...],
 *     "SECTIONS": { "DRIVER3D_MODEL": { "NAME": ..., "POSITION": ... }, ... },
 *     "SKINS": { "2024_AFCorse_LeMans_50": { "SECTIONS": {...} } },
 *     "IMPORTED_FROM": ["ext_config.ini", "skins/<id>/ext_config.ini"]
 *   }
 *
 * Merging twice is a no-op: the block is rebuilt from the .ini files, so the
 * result depends only on content, never on how many times it ran.
 */
#include "FileFormat/Json.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ks {
namespace assetto_corsa {

struct ExtConfigSummary {
    std::vector<std::string> hiddenNodes; // union, file order, deduplicated
    std::string driverModel;
    std::string driverPosition;
    int files = 0;
    int sections = 0;
};

namespace ext_config_detail {

namespace fs = std::filesystem;

inline std::string trimText(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

inline std::string unquoteText(const std::string& text) {
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') ||
                             (text.front() == '\'' && text.back() == '\'')))
        return text.substr(1, text.size() - 2);
    return text;
}

inline bool beginsWith(const std::string& text, const char* prefix) {
    const std::string head(prefix);
    if (text.size() < head.size()) return false;
    for (std::size_t i = 0; i < head.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(text[i])) !=
            std::toupper(static_cast<unsigned char>(head[i])))
            return false;
    return true;
}

/** "A, B,C" -> {"A", "B", "C"}: AC writes node lists comma separated, with the
 *  spaces optional and inconsistent. */
inline std::vector<std::string> splitList(const std::string& text) {
    std::vector<std::string> items;
    std::string current;
    for (char c : text) {
        if (c == ',') {
            const std::string item = trimText(current);
            if (!item.empty()) items.push_back(item);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    const std::string item = trimText(current);
    if (!item.empty()) items.push_back(item);
    return items;
}

inline void addHidden(ExtConfigSummary& summary, const std::vector<std::string>& names) {
    for (const std::string& name : names) {
        if (std::find(summary.hiddenNodes.begin(), summary.hiddenNodes.end(), name) !=
            summary.hiddenNodes.end())
            continue;
        summary.hiddenNodes.push_back(name);
    }
}

inline void addHidden(ExtConfigSummary& summary, const std::string& name) {
    addHidden(summary, std::vector<std::string>{name});
}

inline Json& objectAt(Json& parent, const std::string& key) {
    if (!parent.contains(key) || !parent[key].isObject()) parent.insert(key, Json::object());
    return parent[key];
}

} // namespace ext_config_detail

/** Read one ext_config.ini into `sections` (section -> key -> value) and collect
 *  the model-relevant entries into `summary`. Returns false when the file
 *  cannot be opened; an empty file is a valid, empty config. */
inline bool readExtConfigFile(const std::filesystem::path& file, Json& sections,
                             ExtConfigSummary& summary) {
    using namespace ext_config_detail;
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;

    std::string line;
    std::string group;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string trimmed = trimText(line);
        if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#') continue;
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            group = trimText(trimmed.substr(1, trimmed.size() - 2));
            ++summary.sections;
            objectAt(sections, group);
            continue;
        }
        const std::size_t separator = trimmed.find_first_of("=:");
        if (separator == std::string::npos || group.empty()) continue;
        const std::string key = trimText(trimmed.substr(0, separator));
        const std::string value = unquoteText(trimText(trimmed.substr(separator + 1)));
        if (key.empty()) continue;

        Json& bucket = objectAt(sections, group);
        if (!bucket.contains(key)) bucket.insert(key, Json(value));

        if (beginsWith(group, "DRIVER3D_HIDE_OBJECT") && beginsWith(key, "NAME"))
            addHidden(summary, value);
        if (beginsWith(group, "MODEL_REPLACEMENT") && beginsWith(key, "HIDE"))
            addHidden(summary, splitList(value));
        if (beginsWith(group, "DRIVER3D_MODEL")) {
            if (beginsWith(key, "NAME")) summary.driverModel = value;
            if (beginsWith(key, "POSITION")) summary.driverPosition = value;
        }
    }
    ++summary.files;
    return true;
}

/** Fold `carDir/ext_config.ini` and every `carDir/skins/<id>/ext_config.ini`
 *  into the model document under EXT_CONFIG. Returns the number of files read;
 *  a car without ext_config.ini is left untouched. */
inline int mergeExtConfigIntoModel(const std::filesystem::path& carDir, Json& modelDocument) {
    using namespace ext_config_detail;
    namespace fs = std::filesystem;

    ExtConfigSummary summary;
    Json rootSections = Json::object();
    Json skins = Json::object();
    Json importedFrom = Json::array();
    int files = 0;

    const fs::path rootFile = carDir / "ext_config.ini";
    std::error_code ec;
    if (fs::is_regular_file(rootFile, ec) && readExtConfigFile(rootFile, rootSections, summary)) {
        importedFrom.push_back(Json(std::string("ext_config.ini")));
        ++files;
    }

    const fs::path skinsDir = carDir / "skins";
    if (fs::is_directory(skinsDir, ec)) {
        std::vector<fs::path> folders;
        for (const auto& entry : fs::directory_iterator(skinsDir, ec))
            if (entry.is_directory(ec)) folders.push_back(entry.path());
        std::sort(folders.begin(), folders.end());
        for (const fs::path& folder : folders) {
            const fs::path file = folder / "ext_config.ini";
            if (!fs::is_regular_file(file, ec)) continue;
            ExtConfigSummary skinSummary;
            Json skinSections = Json::object();
            if (!readExtConfigFile(file, skinSections, skinSummary)) continue;
            Json skinEntry = Json::object();
            skinEntry.insert("SECTIONS", skinSections);
            if (!skinSummary.driverModel.empty())
                skinEntry.insert("DRIVER3D_MODEL",
                                 Json(skinSummary.driverModel +
                                      (skinSummary.driverPosition.empty()
                                           ? std::string()
                                           : (" @ " + skinSummary.driverPosition))));
            skins.insert(folder.filename().string(), skinEntry);
            // The skin's hides apply to the car as a whole: a skin that removes a
            // helper node wants it gone whichever skin is loaded.
            addHidden(summary, skinSummary.hiddenNodes);
            if (summary.driverModel.empty()) summary.driverModel = skinSummary.driverModel;
            if (summary.driverPosition.empty())
                summary.driverPosition = skinSummary.driverPosition;
            importedFrom.push_back(
                Json(std::string("skins/") + folder.filename().string() + "/ext_config.ini"));
            ++files;
        }
    }

    if (files == 0) return 0;

    Json block = Json::object();
    Json hidden = Json::array();
    for (const std::string& node : summary.hiddenNodes) hidden.push_back(Json(node));
    block.insert("HIDDEN_NODES", hidden);
    block.insert("SECTIONS", rootSections);
    if (!skins.isEmpty()) block.insert("SKINS", skins);
    block.insert("IMPORTED_FROM", importedFrom);
    if (!summary.driverModel.empty()) block.insert("DRIVER3D_MODEL_NAME", Json(summary.driverModel));
    if (!summary.driverPosition.empty())
        block.insert("DRIVER3D_MODEL_POSITION", Json(summary.driverPosition));

    if (modelDocument.contains("EXT_CONFIG")) modelDocument.remove("EXT_CONFIG");
    modelDocument.insert("EXT_CONFIG", block);
    return files;
}

} // namespace assetto_corsa
} // namespace ks
