#pragma once
/**
 * AC's ext_config.ini, folded into the car's documents.
 *
 * A skin folder carries ext_config.ini, and that file changes the model more
 * than any other piece of content: it names the driver model, lists the nodes a
 * skin hides (DRIVER3D_HIDE_OBJECT_*) and rewrites whole node sets
 * (MODEL_REPLACEMENT_*). ksim read none of it, so a car kept its default driver
 * and every helper mesh the skin wanted gone.
 *
 * The file carries two subjects, so it is split the way the documents already
 * split their sources: DRIVER3D_* sections are the driver and land in
 * driver.json (the document driver3d.ini already goes to), MODEL_* sections are
 * the model and land in model.json. A hidden node follows the section that hides
 * it: the driver's own nodes stay with the driver, the model's nodes with the
 * model. Sections of any other kind are model content.
 *
 * Layout written into each document:
 *
 *   "EXT_CONFIG": {
 *     "HIDDEN_NODES": ["DRIVER:HELMET", ...],
 *     "SECTIONS": { "DRIVER3D_MODEL": { "NAME": ..., "POSITION": ... }, ... },
 *     "SKINS": { "2024_AFCorse_LeMans_50": { "SECTIONS": {...}, ... } },
 *     "IMPORTED_FROM": ["ext_config.ini", "skins/<id>/ext_config.ini"],
 *     "DRIVER3D_MODEL_NAME": ..., "DRIVER3D_MODEL_POSITION": ...   // driver only
 *   }
 *
 * Nothing is invented: sections are copied verbatim. Wildcards stay wildcards
 * (KERS_LEVEL_? is not expanded). Merging twice is a no-op, because the block is
 * rebuilt from the .ini files rather than appended to.
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

/** Case-insensitive prefix test: AC is not consistent about the case of its keys. */
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

inline void addUnique(std::vector<std::string>& list, const std::string& value) {
    if (value.empty()) return;
    if (std::find(list.begin(), list.end(), value) != list.end()) return;
    list.push_back(value);
}

inline void addUnique(std::vector<std::string>& list, const std::vector<std::string>& values) {
    for (const std::string& value : values) addUnique(list, value);
}

/** One [SECTION] of the file, with its entries in file order. */
struct Section {
    std::string name;
    Json entries = Json::object();
    bool driverOwned = false; // DRIVER3D_*: the driver's business, not the model's
};

/** One ext_config.ini, read. */
struct ParsedConfig {
    std::vector<Section> sections;
    std::vector<std::string> driverHidden; // nodes hidden by DRIVER3D_HIDE_OBJECT_*
    std::vector<std::string> modelHidden;  // nodes hidden by MODEL_REPLACEMENT_*.HIDE
    std::string driverModel;
    std::string driverPosition;
};

inline bool isDriverSection(const std::string& name) { return beginsWith(name, "DRIVER3D_"); }

inline Section& sectionAt(ParsedConfig& parsed, const std::string& name) {
    for (Section& section : parsed.sections)
        if (section.name == name) return section;
    Section fresh;
    fresh.name = name;
    fresh.driverOwned = isDriverSection(name);
    parsed.sections.push_back(fresh);
    return parsed.sections.back();
}

} // namespace ext_config_detail

/** Read one ext_config.ini. Returns false when the file cannot be opened; an
 *  empty file is a valid, empty config. */
inline bool readExtConfigFile(const std::filesystem::path& file,
                             ext_config_detail::ParsedConfig& parsed) {
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
            sectionAt(parsed, group);
            continue;
        }
        const std::size_t separator = trimmed.find_first_of("=:");
        if (separator == std::string::npos || group.empty()) continue;
        const std::string key = trimText(trimmed.substr(0, separator));
        const std::string value = unquoteText(trimText(trimmed.substr(separator + 1)));
        if (key.empty()) continue;

        Section& section = sectionAt(parsed, group);
        if (!section.entries.contains(key)) section.entries.insert(key, Json(value));

        if (beginsWith(group, "DRIVER3D_HIDE_OBJECT") && beginsWith(key, "NAME"))
            addUnique(parsed.driverHidden, value);
        if (beginsWith(group, "MODEL_REPLACEMENT") && beginsWith(key, "HIDE"))
            addUnique(parsed.modelHidden, splitList(value));
        if (beginsWith(group, "DRIVER3D_MODEL")) {
            if (beginsWith(key, "NAME")) parsed.driverModel = value;
            if (beginsWith(key, "POSITION")) parsed.driverPosition = value;
        }
    }
    return true;
}

namespace ext_config_detail {

inline std::vector<Section> sectionsOf(const ParsedConfig& parsed, bool driver) {
    std::vector<Section> out;
    for (const Section& section : parsed.sections)
        if (section.driverOwned == driver) out.push_back(section);
    return out;
}

inline Json sectionsJson(const std::vector<Section>& sections) {
    Json out = Json::object();
    for (const Section& section : sections)
        if (!out.contains(section.name)) out.insert(section.name, section.entries);
    return out;
}

inline Json namesJson(const std::vector<std::string>& names) {
    Json out = Json::array();
    for (const std::string& name : names) out.push_back(Json(name));
    return out;
}

/** One subject of one file: what a document needs to know about that file. */
inline Json documentBlock(const ParsedConfig& parsed, bool driver, const Json& skins,
                          const std::vector<std::string>& files) {
    Json block = Json::object();
    block.insert("HIDDEN_NODES",
                 namesJson(driver ? parsed.driverHidden : parsed.modelHidden));
    block.insert("SECTIONS", sectionsJson(sectionsOf(parsed, driver)));
    if (!skins.isEmpty()) block.insert("SKINS", skins);
    block.insert("IMPORTED_FROM", namesJson(files));
    if (driver && !parsed.driverModel.empty())
        block.insert("DRIVER3D_MODEL_NAME", Json(parsed.driverModel));
    if (driver && !parsed.driverPosition.empty())
        block.insert("DRIVER3D_MODEL_POSITION", Json(parsed.driverPosition));
    return block;
}

} // namespace ext_config_detail

/** Fold `carDir/ext_config.ini` and every `carDir/skins/<id>/ext_config.ini`
 *  into the model and driver documents: the driver's sections (DRIVER3D_*, its
 *  hidden nodes, the driver model) go to `driverDocument`, everything else to
 *  `modelDocument`. A document with nothing for its subject is left untouched.
 *  Returns the number of files read. */
inline int mergeExtConfig(const std::filesystem::path& carDir, Json& modelDocument,
                         Json& driverDocument) {
    using namespace ext_config_detail;
    namespace fs = std::filesystem;
    using ext_config_detail::ParsedConfig;

    ParsedConfig root;
    ParsedConfig model;  // everything the model document needs, files folded in
    ParsedConfig driver; // everything the driver document needs
    Json modelSkins = Json::object();
    Json driverSkins = Json::object();
    std::vector<std::string> files;
    int read = 0;

    auto fold = [&](const ParsedConfig& parsed, const std::string& label,
                    const std::string& skinName) {
        for (const Section& section : parsed.sections) {
            const bool toDriver = section.driverOwned;
            std::vector<Section>& target = toDriver ? driver.sections : model.sections;
            target.push_back(section);
        }
        addUnique(model.modelHidden, parsed.modelHidden);
        addUnique(driver.driverHidden, parsed.driverHidden);
        if (driver.driverModel.empty()) driver.driverModel = parsed.driverModel;
        if (driver.driverPosition.empty()) driver.driverPosition = parsed.driverPosition;

        if (!skinName.empty()) {
            Json modelEntry = Json::object();
            const std::vector<Section> modelSections = sectionsOf(parsed, false);
            if (!modelSections.empty()) modelEntry.insert("SECTIONS", sectionsJson(modelSections));
            if (!parsed.modelHidden.empty())
                modelEntry.insert("HIDDEN_NODES", namesJson(parsed.modelHidden));
            if (!modelEntry.isEmpty()) modelSkins.insert(skinName, modelEntry);

            Json driverEntry = Json::object();
            const std::vector<Section> driverSections = sectionsOf(parsed, true);
            if (!driverSections.empty()) driverEntry.insert("SECTIONS", sectionsJson(driverSections));
            if (!parsed.driverHidden.empty())
                driverEntry.insert("HIDDEN_NODES", namesJson(parsed.driverHidden));
            if (!parsed.driverModel.empty())
                driverEntry.insert("DRIVER3D_MODEL_NAME", Json(parsed.driverModel));
            if (!parsed.driverPosition.empty())
                driverEntry.insert("DRIVER3D_MODEL_POSITION", Json(parsed.driverPosition));
            if (!driverEntry.isEmpty()) driverSkins.insert(skinName, driverEntry);
        }
        files.push_back(label);
        ++read;
    };

    std::error_code ec;
    const fs::path rootFile = carDir / "ext_config.ini";
    if (fs::is_regular_file(rootFile, ec) && readExtConfigFile(rootFile, root))
        fold(root, "ext_config.ini", std::string());

    const fs::path skinsDir = carDir / "skins";
    if (fs::is_directory(skinsDir, ec)) {
        std::vector<fs::path> folders;
        for (const auto& entry : fs::directory_iterator(skinsDir, ec))
            if (entry.is_directory(ec)) folders.push_back(entry.path());
        std::sort(folders.begin(), folders.end());
        for (const fs::path& folder : folders) {
            const fs::path file = folder / "ext_config.ini";
            if (!fs::is_regular_file(file, ec)) continue;
            ParsedConfig parsed;
            if (!readExtConfigFile(file, parsed)) continue;
            fold(parsed, "skins/" + folder.filename().string() + "/ext_config.ini",
                 folder.filename().string());
        }
    }

    if (read == 0) return 0;

    const bool modelHasContent =
        !model.sections.empty() || !model.modelHidden.empty() || !modelSkins.isEmpty();
    const bool driverHasContent =
        !driver.sections.empty() || !driver.driverHidden.empty() ||
        !driver.driverModel.empty() || !driverSkins.isEmpty();

    if (modelHasContent) {
        if (modelDocument.contains("EXT_CONFIG")) modelDocument.remove("EXT_CONFIG");
        modelDocument.insert("EXT_CONFIG",
                             documentBlock(model, false, modelSkins, files));
    }
    if (driverHasContent) {
        if (driverDocument.contains("EXT_CONFIG")) driverDocument.remove("EXT_CONFIG");
        driverDocument.insert("EXT_CONFIG",
                              documentBlock(driver, true, driverSkins, files));
    }
    return read;
}

} // namespace assetto_corsa
} // namespace ks
