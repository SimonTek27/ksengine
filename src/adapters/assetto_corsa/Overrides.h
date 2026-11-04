#pragma once
/**
 * The content's own overrides, folded into the documents that own each part.
 *
 * A car folder can carry two files of overrides: one at the car root, which holds
 * for every skin, and one per skin, which holds only for that skin. Both describe
 * the same three things, so both are stored the same way, in words that are this
 * project's own:
 *
 *   hidden_nodes  node names to leave out of the model (case-insensitive, '?' one
 *                 character, '*' any run). A hidden group takes its children.
 *   sections      settings this build does not interpret yet, kept verbatim so
 *                 nothing the content says is lost.
 *   driver_model  the driver to put in the car: a name and a position.
 *
 * The car-root file is folded into the car's documents, split by subject: what
 * concerns the driver goes to driver.json (where the driver already lives), the
 * rest to model.json. A skin's file is written to skins/<id>/skin.json, next to
 * that skin's textures, because it is skin data and loading another skin must
 * change it.
 *
 *   skins/<id>/skin.json
 *   {
 *     "skin": "2024_AFCorse_LeMans_50",
 *     "model":  { "hidden_nodes": [...], "sections": { "MODEL_REPLACEMENT_...": {...} } },
 *     "driver": { "hidden_nodes": [...], "sections": {...},
 *                 "driver_model": { "name": "rh_bell_helmet_driver_19",
 *                                   "position": [0.0, 0.0, 0.0] } }
 *   }
 *
 * model.json and driver.json carry the same keys at their top level when the car
 * root has a file. Rewriting is idempotent: the keys are rebuilt from the source,
 * never appended to.
 */
#include "FileFormat/Json.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ks {
namespace assetto_corsa {

namespace overrides_detail {

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

/** Case-insensitive prefix test: content is not consistent about the case of its keys. */
inline bool beginsWith(const std::string& text, const char* prefix) {
    const std::string head(prefix);
    if (text.size() < head.size()) return false;
    for (std::size_t i = 0; i < head.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(text[i])) !=
            std::toupper(static_cast<unsigned char>(head[i])))
            return false;
    return true;
}

/** "A, B,C" -> {"A", "B", "C"}: node lists are comma separated, spaces optional. */
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

/** One [SECTION] of the source, with its entries in file order. */
struct Section {
    std::string name;
    Json entries = Json::object();
    bool driverOwned = false; // DRIVER3D_*: the driver's business, not the model's
};

/** One overrides file, read. */
struct Parsed {
    std::vector<Section> sections;
    std::vector<std::string> driverHidden; // nodes the driver blocks hide
    std::vector<std::string> modelHidden;  // nodes the model blocks hide
    std::string driverModel;
    std::string driverPosition;
};

inline bool isDriverSection(const std::string& name) { return beginsWith(name, "DRIVER3D_"); }

inline Section& sectionAt(Parsed& parsed, const std::string& name) {
    for (Section& section : parsed.sections)
        if (section.name == name) return section;
    Section fresh;
    fresh.name = name;
    fresh.driverOwned = isDriverSection(name);
    parsed.sections.push_back(fresh);
    return parsed.sections.back();
}

/** Read one overrides file. Returns false when it cannot be opened; an empty
 *  file is a valid, empty set of overrides. */
inline bool readFile(const fs::path& file, Parsed& parsed) {
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

inline std::vector<Section> sectionsOf(const Parsed& parsed, bool driver) {
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

/** "0.0,0.0,0.0" -> [0.0, 0.0, 0.0]. A shape we do not recognise yields no
 *  position rather than invented numbers. */
inline bool positionJson(const std::string& text, Json& out) {
    const std::vector<std::string> parts = splitList(text);
    if (parts.size() != 3) return false;
    Json array = Json::array();
    for (const std::string& part : parts) {
        char* end = nullptr;
        const double value = std::strtod(part.c_str(), &end);
        if (end == nullptr || *end != 0) return false;
        array.push_back(Json(value));
    }
    out = array;
    return true;
}

/** One subject (model or driver) of one overrides file. */
inline Json subjectJson(const Parsed& parsed, bool driver) {
    Json block = Json::object();
    block.insert("hidden_nodes", namesJson(driver ? parsed.driverHidden : parsed.modelHidden));
    block.insert("sections", sectionsJson(sectionsOf(parsed, driver)));
    if (driver) {
        Json model = Json::object();
        if (!parsed.driverModel.empty()) model.insert("name", Json(parsed.driverModel));
        Json position;
        if (positionJson(parsed.driverPosition, position)) model.insert("position", position);
        if (!model.isEmpty()) block.insert("driver_model", model);
    }
    return block;
}

inline bool subjectHasContent(const Parsed& parsed, bool driver) {
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

/** Sets a key when the content has something to say about it, and takes the key
 *  away otherwise, so a merge over an already-merged document stays the same
 *  document instead of keeping a list nothing stands behind. */
inline void setOrRemove(Json& document, const char* key, bool present, const Json& value) {
    if (document.contains(key)) document.remove(key);
    if (!present) return;
    document.insert(key, value);
}

} // namespace overrides_detail

/** Every skin folder of a car that carries overrides, in name order. */
inline std::vector<std::string> skinsWithOverrides(const std::filesystem::path& carDir) {
    using namespace overrides_detail;
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

/** The skin.json document of one skin, built from that skin's overrides.
 *  False when the skin carries no overrides file. */
inline bool skinOverridesDocument(const std::filesystem::path& skinDir,
                                 const std::string& skinId, Json& out) {
    using namespace overrides_detail;
    Parsed parsed;
    if (!readFile(skinDir / "ext_config.ini", parsed)) return false;
    Json document = Json::object();
    document.insert("skin", Json(skinId));
    document.insert("model", subjectJson(parsed, false));
    document.insert("driver", subjectJson(parsed, true));
    out = document;
    return true;
}

/** Write skins/<id>/skin.json for every skin that carries overrides.
 *  Returns the number of files written. */
inline int writeSkinOverrides(const std::filesystem::path& carDir) {
    using namespace overrides_detail;
    int written = 0;
    for (const std::string& id : skinsWithOverrides(carDir)) {
        Json document;
        if (!skinOverridesDocument(carDir / "skins" / id, id, document)) continue;
        if (writeJsonFile(carDir / "skins" / id / "skin.json", document)) ++written;
    }
    return written;
}

/** Fold the car-root overrides into the model and driver documents. A skin's own
 *  file goes to its skin.json instead, never into these. Returns 1 when the car
 *  root file was read. */
inline int mergeOverrides(const std::filesystem::path& carDir, Json& modelDocument,
                          Json& driverDocument) {
    using namespace overrides_detail;
    Parsed parsed;
    const bool any = readFile(carDir / "ext_config.ini", parsed);

    const bool model = any && subjectHasContent(parsed, false);
    const bool driver = any && subjectHasContent(parsed, true);
    setOrRemove(modelDocument, "hidden_nodes", model, namesJson(parsed.modelHidden));
    setOrRemove(modelDocument, "sections", model, sectionsJson(sectionsOf(parsed, false)));
    setOrRemove(driverDocument, "hidden_nodes", driver, namesJson(parsed.driverHidden));
    setOrRemove(driverDocument, "sections", driver, sectionsJson(sectionsOf(parsed, true)));
    Json driverModel = Json::object();
    if (driver) {
        if (!parsed.driverModel.empty()) driverModel.insert("name", Json(parsed.driverModel));
        Json position;
        if (positionJson(parsed.driverPosition, position))
            driverModel.insert("position", position);
    }
    setOrRemove(driverDocument, "driver_model", driver && !driverModel.isEmpty(), driverModel);
    return any ? 1 : 0;
}

} // namespace assetto_corsa
} // namespace ks
