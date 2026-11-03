#pragma once
/**
 * AC's ext_config.ini, folded into the content that owns each part of it.
 *
 * The file exists in two places with two different meanings:
 *
 *   - carDir/ext_config.ini applies to the car whatever skin is loaded, so it is
 *     folded into the car's documents, split by subject: DRIVER3D_* sections and
 *     the driver's hidden nodes go to driver.json (where driver3d.ini already
 *     goes), MODEL_* sections and the model's hidden nodes to model.json.
 *
 *   - carDir/skins/<id>/ext_config.ini belongs to that skin and nothing else:
 *     it names the skin's driver model and the nodes that skin hides. It is
 *     written to carDir/skins/<id>/skin.json, next to the skin's own textures,
 *     because it is skin data and not model or driver data. Loading a different
 *     skin therefore changes what is hidden, which is what the file means.
 *
 * Shape of skins/<id>/skin.json:
 *
 *   {
 *     "SKIN": "2024_AFCorse_LeMans_50",
 *     "MODEL":  { "HIDDEN_NODES": [...], "SECTIONS": { "MODEL_REPLACEMENT_...": {...} } },
 *     "DRIVER": { "HIDDEN_NODES": [...], "SECTIONS": {...},
 *                 "MODEL_NAME": "rh_bell_helmet_driver_19", "MODEL_POSITION": "0.0,0.0,0.0" },
 *     "IMPORTED_FROM": ["skins/2024_AFCorse_LeMans_50/ext_config.ini"]
 *   }
 *
 * The car documents keep the same two subjects under EXT_CONFIG, without any
 * SKINS table: a skin's data is never stored in the model or the driver.
 *
 * Nothing is invented: sections are copied verbatim and wildcards stay
 * wildcards (KERS_LEVEL_? is not expanded). Rewriting is idempotent — the
 * blocks are rebuilt from the .ini file, never appended to.
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

/** Read one ext_config.ini. Returns false when the file cannot be opened; an
 *  empty file is a valid, empty config. */
inline bool readFile(const fs::path& file, ParsedConfig& parsed) {
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

inline Json filesJson(const std::vector<std::string>& files) {
    return namesJson(files);
}

/** One subject (model or driver) of one file. */
inline Json subjectBlock(const ParsedConfig& parsed, bool driver) {
    Json block = Json::object();
    block.insert("HIDDEN_NODES", namesJson(driver ? parsed.driverHidden : parsed.modelHidden));
    block.insert("SECTIONS", sectionsJson(sectionsOf(parsed, driver)));
    if (driver && !parsed.driverModel.empty())
        block.insert("MODEL_NAME", Json(parsed.driverModel));
    if (driver && !parsed.driverPosition.empty())
        block.insert("MODEL_POSITION", Json(parsed.driverPosition));
    return block;
}

inline bool subjectHasContent(const ParsedConfig& parsed, bool driver) {
    if (!sectionsOf(parsed, driver).empty()) return true;
    return driver ? (!parsed.driverHidden.empty() || !parsed.driverModel.empty())
                  : !parsed.modelHidden.empty();
}

inline bool writeJsonFile(const fs::path& file, const Json& document) {
    std::error_code ec;
    if (!file.parent_path().empty()) fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << document.dump(true);
    out.put(static_cast<char>(10));
    out.close();
    return static_cast<bool>(out);
}

} // namespace ext_config_detail

/** Every skin folder of a car that ships an ext_config.ini, in name order. */
inline std::vector<std::string> skinsWithExtConfig(const std::filesystem::path& carDir) {
    using namespace ext_config_detail;
    std::vector<std::string> ids;
    std::error_code ec;
    const fs::path skinsDir = carDir / "skins";
    if (!fs::is_directory(skinsDir, ec)) return ids;
    std::vector<fs::path> folders;
    for (const auto& entry : fs::directory_iterator(skinsDir, ec))
        if (entry.is_directory(ec)) folders.push_back(entry.path());
    std::sort(folders.begin(), folders.end());
    for (const fs::path& folder : folders) {
        if (!fs::is_regular_file(folder / "ext_config.ini", ec)) continue;
        ids.push_back(folder.filename().string());
    }
    return ids;
}

/** The skin.json document of one skin, built from its ext_config.ini. */
inline Json skinDocument(const std::filesystem::path& skinDir, const std::string& skinId) {
    using namespace ext_config_detail;
    ParsedConfig parsed;
    if (!readFile(skinDir / "ext_config.ini", parsed)) return Json();
    Json document = Json::object();
    document.insert("SKIN", Json(skinId));
    document.insert("MODEL", subjectBlock(parsed, false));
    document.insert("DRIVER", subjectBlock(parsed, true));
    Json files = Json::array();
    files.push_back(Json("skins/" + skinId + "/ext_config.ini"));
    document.insert("IMPORTED_FROM", files);
    return document;
}

/** Write skins/<id>/skin.json for every skin that carries an ext_config.ini.
 *  Returns the number of files written. */
inline int writeSkinDocuments(const std::filesystem::path& carDir) {
    using namespace ext_config_detail;
    int written = 0;
    for (const std::string& id : skinsWithExtConfig(carDir)) {
        const Json document = skinDocument(carDir / "skins" / id, id);
        if (document.isUndefined()) continue;
        if (writeJsonFile(carDir / "skins" / id / "skin.json", document)) ++written;
    }
    return written;
}

/** Fold the car-root ext_config.ini into the model and driver documents.
 *  A skin's own file is written to its skin.json instead, never into these.
 *  Any EXT_CONFIG already present is rebuilt, so a block left by an older run
 *  (including the SKINS table this no longer writes) does not survive.
 *  Returns 1 when the root file was read. */
inline int mergeExtConfig(const std::filesystem::path& carDir, Json& modelDocument,
                         Json& driverDocument) {
    using namespace ext_config_detail;
    if (modelDocument.contains("EXT_CONFIG")) modelDocument.remove("EXT_CONFIG");
    if (driverDocument.contains("EXT_CONFIG")) driverDocument.remove("EXT_CONFIG");

    ParsedConfig parsed;
    if (!readFile(carDir / "ext_config.ini", parsed)) return 0;

    const std::vector<std::string> files{"ext_config.ini"};
    if (subjectHasContent(parsed, false)) {
        Json block = subjectBlock(parsed, false);
        block.insert("IMPORTED_FROM", filesJson(files));
        modelDocument.insert("EXT_CONFIG", block);
    }
    if (subjectHasContent(parsed, true)) {
        Json block = subjectBlock(parsed, true);
        block.insert("IMPORTED_FROM", filesJson(files));
        driverDocument.insert("EXT_CONFIG", block);
    }
    return 1;
}

} // namespace assetto_corsa
} // namespace ks
