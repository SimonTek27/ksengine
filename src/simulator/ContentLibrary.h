#pragma once
/**
 * Qt-free content scanner (roadmap 1.3 / GAP P2.5 slice): lists the cars and
 * tracks ksim can load from its native content roots (content/cars and
 * content/tracks relative to the working directory). Any subdirectory is an
 * entry; a missing root yields an empty list, so a fresh checkout can still
 * open the menu and the select screens explain where content goes.
 *
 * Optional metadata files (first hit wins):
 *   <car>/ui_car.json  (or <car>/ui/ui_car.json, the layout AC ships)
 *                       { "name", "brand", "class", "year",
 *                         "colors": [ { "name", "rgb": [r,g,b] } ] }
 *  Colours are the car's own paint list: a channel above 1 is taken as a
 *  0..255 value and scaled down, so both list styles land in 0..1.
 *   <car>/data/car.json
 *   <track>/ui_track.json or <track>/ui/ui_track.json (AC's layout)
 *   <track>/data/data.json  (what Assetto Corsa's own data ships)
 *                       { "name", "country", "city", "length"|"length_m",
 *                         "year" }
 *  `city` may be a string or an array of strings (the first one is kept) and
 *  `length` may be a string (AC) or a number (CSP data); both land in metres.
 */
#include "engine/Config/Json.h"
#include "engine/assets/UserData.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace ks::sim {

/** One paint from the car's own colour list. rgb is 0..1. */
struct CarPaint {
    std::string name;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

struct ContentEntry {
    std::string id;     // directory name, stable key
    std::string path;   // passed to SimulationLoop::loadCar / loadTrackFolder
    std::string label;  // display name
    std::string brand;  // optional filter key
    std::string carClass;
    // Tracks only: what their ui_track.json / data.json declares. Empty/0 when
    // the folder carries no metadata, which is the case raw AC content often is.
    std::string country;
    std::string city;
    double lengthM = 0.0;
    int year = 0;
    // Where the circuit sits, in decimal degrees. hasGeo keeps a real 0 from
    // "the folder never said" (the equator is a valid latitude).
    double latitude = 0.0;
    double longitude = 0.0;
    bool hasGeo = false;
    // "permanent" or "temporary", exactly as the content declares it (CSP's
    // data.json carries "type"); empty when it says nothing, never guessed.
    std::string circuitType;
    // The version the content declares (ui_car.json / ui_track.json "version"),
    // as text: content writes it as a string or as a number, and the manifest
    // has to compare what was said, not what it parses to. Empty when the
    // folder declares none - raw AC content often does not.
    std::string version;
    // Paints the car folder declares. Empty = no colour choice to offer.
    std::vector<CarPaint> paints;
};

/** A coordinate as content writes it: decimal degrees ("43.9975") or the
 *  degrees/minutes/seconds string Mugello's data.json carries ("43°59′51″").
 *  A hemisphere letter (N/S/E/W) or a leading sign sets the sign. */
inline bool parseCoordinate(const std::string& text, double& out) {
    // Ends trimmed only: inner whitespace stays a separator, so "43 59 51"
    // reads as degrees/minutes/seconds the same way "43°59′51″" does.
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    std::string s = text.substr(begin, end - begin);
    if (s.empty()) return false;
    double sign = 1.0;
    const char tail = s.back();
    if (tail == 'S' || tail == 's' || tail == 'W' || tail == 'w') {
        sign = -1.0;
        s.pop_back();
    } else if (tail == 'N' || tail == 'n' || tail == 'E' || tail == 'e') {
        s.pop_back();
    }
    if (s.empty()) return false;
    if (s.front() == '-') {
        sign = -sign;
        s.erase(0, 1);
    } else if (s.front() == '+') {
        s.erase(0, 1);
    }
    // Digits of degrees, minutes and seconds, split by whatever separates them
    // (°, ′, ″, ° and the likes — the atlas never sees these, only the result).
    double parts[3] = {0.0, 0.0, 0.0};
    int count = 0;
    std::string digits;
    const auto flush = [&]() {
        if (count >= 3 || digits.empty()) {
            digits.clear();
            return;
        }
        parts[count++] = std::strtod(digits.c_str(), nullptr);
        digits.clear();
    };
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            digits.push_back(c);
        } else {
            flush();
        }
    }
    flush();
    if (count == 0) return false;
    out = sign * (parts[0] + parts[1] / 60.0 + parts[2] / 3600.0);
    return true;
}

/** "43.9975 N, 11.3722 E" — ASCII only, the text atlas has no degree sign. */
inline std::string trackCoordinateText(const ContentEntry& entry) {
    if (!entry.hasGeo) return {};
    char text[64];
    std::snprintf(text, sizeof(text), "%.4f %c, %.4f %c", std::abs(entry.latitude),
                  entry.latitude < 0.0 ? 'S' : 'N', std::abs(entry.longitude),
                  entry.longitude < 0.0 ? 'W' : 'E');
    return text;
}

namespace content_detail {

/** "#rrggbb" (or "rrggbb") into 0..1 channels. False when the text is not a
 *  colour, so a junk entry is skipped like any other unusable one. */
inline bool parseHexColour(const std::string& text, float channels[3]) {
    std::string hex = text;
    if (!hex.empty() && hex.front() == '#') hex.erase(0, 1);
    if (hex.size() != 6) return false;
    unsigned value = 0;
    for (const char c : hex) {
        int digit = 0;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return false;
        value = value * 16 + static_cast<unsigned>(digit);
    }
    channels[0] = static_cast<float>((value >> 16) & 0xFFu) / 255.0f;
    channels[1] = static_cast<float>((value >> 8) & 0xFFu) / 255.0f;
    channels[2] = static_cast<float>(value & 0xFFu) / 255.0f;
    return true;
}

/** One entry of ui_car.json "colors", in every spelling content uses: the
 *  object with an "rgb" array (CM and AC), an "rgb" "#rrggbb" string (some
 *  packs), separate r/g/b numbers (older tools), and the bare [r,g,b] array
 *  vintage content ships. 0..255 channels are scaled, junk is skipped, and an
 *  entry with no name is "colour N" - nothing is invented. */
inline bool parseCarPaint(const ks::engine::json::Value& item, int index, CarPaint& paint) {
    float channels[3] = {1.0f, 1.0f, 1.0f};
    const auto readNumbers = [&channels](const ks::engine::json::Value& value) {
        if (!value.isArray() || value.size() < 3) return false;
        for (int k = 0; k < 3; ++k) {
            const auto* channel = value.item(static_cast<std::size_t>(k));
            if (channel == nullptr || !channel->isNumber()) return false;
            channels[k] = static_cast<float>(channel->asNumber());
        }
        return true;
    };
    bool usable = false;
    std::string name;
    if (item.isArray()) {
        usable = readNumbers(item); // vintage content: [r, g, b]
    } else if (item.isObject()) {
        if (const auto* rgb = item.find("rgb"); rgb != nullptr) {
            if (rgb->isArray()) usable = readNumbers(*rgb);
            else if (rgb->isString()) usable = parseHexColour(rgb->asString(), channels);
        }
        if (!usable) {
            const auto* r = item.find("r");
            const auto* g = item.find("g");
            const auto* b = item.find("b");
            if (r != nullptr && g != nullptr && b != nullptr && r->isNumber() &&
                g->isNumber() && b->isNumber()) {
                channels[0] = static_cast<float>(r->asNumber());
                channels[1] = static_cast<float>(g->asNumber());
                channels[2] = static_cast<float>(b->asNumber());
                usable = true;
            }
        }
        if (const auto* n = item.find("name"); n != nullptr && n->isString()) name = n->asString();
    }
    if (!usable) return false;
    // 0..255 lists are common in content tools: scale, do not clamp everything
    // to white.
    if (channels[0] > 1.0f || channels[1] > 1.0f || channels[2] > 1.0f)
        for (float& c : channels) c /= 255.0f;
    for (float& c : channels) c = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
    paint.r = channels[0];
    paint.g = channels[1];
    paint.b = channels[2];
    paint.name = name.empty() ? "colour " + std::to_string(index + 1) : name;
    return true;
}

inline void applyCarMeta(ContentEntry& entry) {
    namespace fs = std::filesystem;
    const char* candidates[] = {
        "ui_car.json",      // where the ksim content tools put it
        "ui/ui_car.json",   // where Assetto Corsa itself puts it
        "data/car.json",
        "car.json",
    };
    for (const char* rel : candidates) {
        const fs::path meta = fs::path(entry.path) / rel;
        std::string text;
        if (!ks::engine::assets::UserData::readFile(meta.string(), text))
            continue;
        ks::engine::json::Value root;
        std::string error;
        if (!ks::engine::json::parse(text, root, &error) || !root.isObject())
            continue;
        if (const auto* v = root.find("name"); v && v->isString() && !v->asString().empty())
            entry.label = v->asString();
        if (const auto* v = root.find("brand"); v && v->isString())
            entry.brand = v->asString();
        if (const auto* v = root.find("class"); v && v->isString())
            entry.carClass = v->asString();
        if (const auto* v = root.find("year"); v && v->isNumber())
            entry.year = static_cast<int>(v->asNumber());
        if (const auto* v = root.find("version"); v != nullptr) {
            if (v->isString()) entry.version = v->asString();
            else if (v->isNumber()) {
                char cell[32];
                std::snprintf(cell, sizeof(cell), "%g", v->asNumber());
                entry.version = cell;
            }
        }
        if (const auto* list = root.find("colors"); list && list->isArray()) {
            for (std::size_t i = 0; i < list->size(); ++i) {
                const auto* item = list->item(i);
                if (item == nullptr) continue;
                CarPaint paint;
                if (!parseCarPaint(*item, static_cast<int>(i), paint)) continue;
                entry.paints.push_back(std::move(paint));
            }
        }
        // One line per car that declared metadata: the CARS page filters and
        // the paint picker both read exactly what this parsed.
        std::printf("[content] %s: \"%s\" class=%s year=%d colours=%d\n",
                    entry.id.c_str(), entry.label.c_str(), entry.carClass.c_str(),
                    entry.year, static_cast<int>(entry.paints.size()));
        break;
    }
}

/** Every root the libraries may live under, in the order they win.
 *  1. KS_CONTENT_ROOT: an explicit root, ';'-separated for several (the way
 *     to point a build-folder launch at content kept elsewhere).
 *  2. content/: the run folder, which is what a staged build ships.
 *  3. <install prefix>/content: where cmake/KsInstallLayout.cmake puts the
 *     shipped layout, baked in at configure time (KS_INSTALL_PREFIX). This is
 *     what lets a launch from out/build/... list the installation's tracks
 *     without copying a single file.
 *  A candidate carrying no directory of the requested library is skipped, so
 *  the empty content/tracks of a dev build never hides the installation. */
inline std::vector<std::string> contentRootCandidates() {
    std::vector<std::string> roots;
    const auto push = [&roots](std::string root) {
        while (root.size() > 1 && (root.back() == '/' || root.back() == '\\'))
            root.pop_back();
        if (root.empty()) return;
        for (const std::string& seen : roots)
            if (seen == root) return;
        roots.push_back(root);
    };
    if (const char* env = std::getenv("KS_CONTENT_ROOT"); env && env[0]) {
        const std::string list(env);
        std::size_t start = 0;
        while (start <= list.size()) {
            const std::size_t sep = list.find(';', start);
            push(list.substr(start, sep == std::string::npos ? std::string::npos : sep - start));
            if (sep == std::string::npos) break;
            start = sep + 1;
        }
    }
    push("content");
#ifdef KS_INSTALL_PREFIX
    push(std::string(KS_INSTALL_PREFIX) + "/content");
#endif
    return roots;
}

/** A file inside the content tree (gui/flags/...), from the same roots the
 *  libraries come from: the first root that has it wins. Empty when no root
 *  does. */
inline std::string contentAssetPath(const std::string& relative) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const std::string& root : contentRootCandidates()) {
        // Joined with '/', like the library roots, so the path a page shows and
        // a test compares reads the same on every platform.
        const std::string candidate = root + "/" + relative;
        if (fs::is_regular_file(candidate, ec)) return candidate;
    }
    return {};
}

/** Does this library folder carry at least one entry? Teams only count when
 *  the folder is a real roster (team.ini), like the scanner demands. */
inline bool libraryHasEntries(const std::string& root, bool teamsOnly) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return false;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        if (!e.is_directory(ec)) continue;
        if (teamsOnly && !fs::is_regular_file(e.path() / "team.ini", ec)) continue;
        return true;
    }
    return false;
}

/** The folder a library is listed from: the first candidate root that
 *  actually carries entries of that kind. One root, never a merge — the same
 *  track in two places is listed once, and a page cannot show duplicates. */
inline std::string libraryRoot(const char* kind, bool teamsOnly) {
    const std::vector<std::string> roots = contentRootCandidates();
    const std::string base = roots.empty() ? std::string("content") : roots.front();
    for (const std::string& root : roots) {
        const std::string candidate = root + "/" + kind;
        if (!libraryHasEntries(candidate, teamsOnly)) continue;
        if (candidate != base + "/" + kind) {
            std::printf("[content] %s: %s has none, listing %s\n", kind,
                        (base + "/" + kind).c_str(), candidate.c_str());
        }
        return candidate;
    }
    // Nothing found: name the first root, so the page's hint says where to
    // put content on this launch setup.
    return base + "/" + kind;
}

/** A JSON string field, trimmed of the whitespace AC's files carry. */
inline std::string jsonText(const ks::engine::json::Value& root, const char* key) {
    const auto* v = root.find(key);
    if (!v) return {};
    if (v->isString()) return v->asString();
    // CSP's data.json writes a city as an array ("city": ["Scarperia", ...]).
    if (v->isArray()) {
        for (std::size_t i = 0; i < v->size(); ++i) {
            const auto* item = v->item(i);
            if (item && item->isString() && !item->asString().empty()) return item->asString();
        }
    }
    return {};
}

/** Length in metres from a string ("5245") or a number (5245.0). */
inline double jsonLengthM(const ks::engine::json::Value& root) {
    for (const char* key : {"length_m", "length"}) {
        const auto* v = root.find(key);
        if (!v) continue;
        if (v->isNumber() && v->asNumber() > 0.0) return v->asNumber();
        if (v->isString() && !v->asString().empty())
            return std::strtod(v->asString().c_str(), nullptr);
    }
    return 0.0;
}

inline std::string lowerText(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

/** The tour facts a circuit can declare beyond its name: the kind of circuit
 *  ("type": "permanent" | "temporary") and where it is ("geotags": [lat,
 *  lon], or plain "latitude"/"longitude" members). */
inline void applyTrackFacts(ContentEntry& entry, const ks::engine::json::Value& root) {
    const std::string type = lowerText(jsonText(root, "type"));
    if (type == "permanent" || type == "temporary") entry.circuitType = type;

    double lat = 0.0;
    double lon = 0.0;
    bool haveLat = false;
    bool haveLon = false;
    const auto readPart = [](const ks::engine::json::Value* item, double& value) {
        if (!item) return false;
        if (item->isNumber()) {
            value = item->asNumber();
            return true;
        }
        if (item->isString()) return parseCoordinate(item->asString(), value);
        return false;
    };
    if (const auto* v = root.find("geotags"); v && v->isArray() && v->size() >= 2) {
        haveLat = readPart(v->item(0), lat);
        haveLon = readPart(v->item(1), lon);
    }
    if (!haveLat) haveLat = readPart(root.find("latitude"), lat);
    if (!haveLon) haveLon = readPart(root.find("longitude"), lon);
    if (haveLat && haveLon && std::abs(lat) <= 90.0 && std::abs(lon) <= 180.0) {
        entry.latitude = lat;
        entry.longitude = lon;
        entry.hasGeo = true;
    }
}

/** Content JSON in the wild is not always strict JSON: AC and CSP write
 *  trailing commas before a closing brace/bracket (Mugello's data.json has
 *  them). They are dropped here, string-aware, instead of loosening the
 *  engine's own parser for every settings file it reads. */
inline std::string withoutTrailingCommas(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool inString = false;
    bool escaped = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            out += c;
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') {
            inString = true;
            out += c;
            continue;
        }
        if (c == ',') {
            std::size_t j = i + 1;
            while (j < text.size() && (text[j] == ' ' || text[j] == '\t' ||
                                       text[j] == '\n' || text[j] == '\r'))
                ++j;
            if (j < text.size() && (text[j] == '}' || text[j] == ']')) continue;
        }
        out += c;
    }
    return out;
}

/** Read one content JSON file: strict first, then once more without the
 *  trailing commas content authors leave in. False when the file is missing
 *  or unreadable either way (a broken file says so on the log). */
inline bool readContentJson(const std::string& file, ks::engine::json::Value& root) {
    std::string text;
    if (!ks::engine::assets::UserData::readFile(file, text)) return false;
    std::string error;
    if (ks::engine::json::parse(text, root, &error) && root.isObject()) return true;
    if (ks::engine::json::parse(withoutTrailingCommas(text), root, &error) &&
        root.isObject())
        return true;
    std::printf("[content] %s unreadable (%s)\n", file.c_str(), error.c_str());
    return false;
}

/** Track metadata, first hit wins: the two places AC puts ui_track.json, then
 *  the data/data.json that ships the same facts for CSP builds. A circuit
 *  whose files only exist per layout (vallelunga keeps ui/<layout>/
 *  ui_track.json) still contributes its country and city from the first
 *  layout; the name and the length stay the folder's, because those differ
 *  per layout and the folder is the whole circuit. */
inline void applyTrackMeta(ContentEntry& entry) {
    namespace fs = std::filesystem;
    const char* candidates[] = {
        "ui_track.json",     // where the ksim content tools put it
        "ui/ui_track.json",  // where Assetto Corsa itself puts it
        "data/data.json",
        "data/track.json",
    };
    ks::engine::json::Value root;
    bool found = false;
    for (const char* rel : candidates) {
        const fs::path meta = fs::path(entry.path) / rel;
        if (!readContentJson(meta.string(), root)) continue;
        found = true;
        break;
    }
    if (!found) {
        std::error_code ec;
        const fs::path uiDir = fs::path(entry.path) / "ui";
        if (fs::is_directory(uiDir, ec)) {
            for (const auto& d : fs::directory_iterator(uiDir, ec)) {
                if (!d.is_directory(ec)) continue;
                ks::engine::json::Value layoutRoot;
                if (!readContentJson((d.path() / "ui_track.json").string(), layoutRoot))
                    continue;
                entry.country = jsonText(layoutRoot, "country");
                entry.city = jsonText(layoutRoot, "city");
                applyTrackFacts(entry, layoutRoot);
                if (const auto* v = layoutRoot.find("version"); v) {
                    entry.version = v->isString() ? v->asString() : std::string();
                }
                if (const auto* v = layoutRoot.find("year"); v && v->isNumber())
                    entry.year = static_cast<int>(v->asNumber());
                if (!entry.country.empty() || entry.year > 0) {
                    std::printf("[content] %s: country=%s from %s\n", entry.id.c_str(),
                                entry.country.c_str(),
                                (d.path() / "ui_track.json").string().c_str());
                }
                break;
            }
        }
        return;
    }
    {
        const std::string name = jsonText(root, "name");
        if (!name.empty()) entry.label = name;
        entry.country = jsonText(root, "country");
        entry.city = jsonText(root, "city");
        entry.lengthM = jsonLengthM(root);
        applyTrackFacts(entry, root);
        if (const auto* v = root.find("year"); v && v->isNumber())
            entry.year = static_cast<int>(v->asNumber());
        if (const auto* v = root.find("version"); v != nullptr) {
            if (v->isString()) entry.version = v->asString();
            else if (v->isNumber()) {
                char cell[32];
                std::snprintf(cell, sizeof(cell), "%g", v->asNumber());
                entry.version = cell;
            }
        }
        std::printf("[content] %s: \"%s\" country=%s length=%.0f m type=%s geo=%s\n",
                    entry.id.c_str(), entry.label.c_str(), entry.country.c_str(),
                    entry.lengthM, entry.circuitType.empty() ? "none" : entry.circuitType.c_str(),
                    trackCoordinateText(entry).empty() ? "none" : trackCoordinateText(entry).c_str());
    }
}

inline std::vector<ContentEntry> scanRoot(const std::string& root, bool cars,
                                          bool tracks = false) {
    namespace fs = std::filesystem;
    std::vector<ContentEntry> out;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return out;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        if (!e.is_directory(ec)) continue;
        ContentEntry entry;
        entry.path = e.path().string();
        entry.id = e.path().filename().string();
        entry.label = entry.id;
        if (cars)
            applyCarMeta(entry);
        else if (tracks)
            applyTrackMeta(entry);
        out.push_back(std::move(entry));
    }
    std::sort(out.begin(), out.end(), [](const ContentEntry& a, const ContentEntry& b) {
        return a.label < b.label;
    });
    return out;
}

} // namespace content_detail

/** A file inside the content tree (gui/flags/...), looked up through the same
 *  roots the libraries come from. Empty when no root carries it. */
inline std::string contentAssetPath(const std::string& relative) {
    return content_detail::contentAssetPath(relative);
}

/** Paint list of one car folder, in the order the file declares it. Empty
 *  when the folder has no metadata or no colours, which is the case every car
 *  without a paint choice falls into. */
inline std::vector<CarPaint> carPaintsFor(const std::string& carDir) {
    ContentEntry entry;
    entry.path = carDir;
    entry.id = carDir;
    content_detail::applyCarMeta(entry);
    return entry.paints;
}

inline std::vector<ContentEntry> scanCarLibrary(const std::string& root = "content/cars") {
    return content_detail::scanRoot(root, true);
}

inline std::vector<ContentEntry> scanTrackLibrary(const std::string& root = "content/tracks") {
    return content_detail::scanRoot(root, false, true);
}

/** The roots the select screens list from (see contentRootCandidates). A page
 *  opened without an explicit root asks these, so it shows the installation's
 *  content even when the game runs from its build folder. */
inline std::string carLibraryRoot() { return content_detail::libraryRoot("cars", false); }
/** Showrooms the CONTENT page lists. ShowroomConfig's loader scans
 *  "content/showrooms" relative to the launch folder (it predates the library
 *  roots), so the page names that same folder: the room it marks is the room
 *  that opens, and the start-up path does no extra root search. */
inline std::string showroomLibraryRoot() { return "content/showrooms"; }
inline std::string trackLibraryRoot() { return content_detail::libraryRoot("tracks", false); }
inline std::string teamLibraryRoot() { return content_detail::libraryRoot("teams", true); }
/** The two driver-gear libraries (DriverGear.h): suits and helmets the content
 *  tree ships, found the same way every other library root is. */
inline std::string driverSuitLibraryRoot() {
    return content_detail::libraryRoot("drivers/suits", false);
}
inline std::string driverHelmetLibraryRoot() {
    return content_detail::libraryRoot("drivers/helmets", false);
}

/** Teams (roadmap 2.8): a team folder is any subdirectory of the root that
 *  holds a team.ini. A missing root yields an empty list, so a fresh
 *  checkout just shows the empty select screen. */
inline std::vector<ContentEntry> scanTeamLibrary(const std::string& root = "content/teams") {
    namespace fs = std::filesystem;
    std::vector<ContentEntry> out;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return out;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        if (!e.is_directory(ec)) continue;
        if (!fs::is_regular_file(e.path() / "team.ini", ec)) continue;
        ContentEntry entry;
        entry.path = e.path().string();
        entry.id = e.path().filename().string();
        entry.label = entry.id;
        out.push_back(std::move(entry));
    }
    std::sort(out.begin(), out.end(), [](const ContentEntry& a, const ContentEntry& b) {
        return a.label < b.label;
    });
    return out;
}

/** Filter helpers for the TRACKS page (country / circuit type). Empty filter = keep all. */
inline std::vector<ContentEntry> filterTracks(const std::vector<ContentEntry>& in,
                                             const std::string& country,
                                             const std::string& circuitType) {
    std::vector<ContentEntry> out;
    out.reserve(in.size());
    for (const auto& e : in) {
        if (!country.empty() && e.country != country) continue;
        if (!circuitType.empty() && e.circuitType != circuitType) continue;
        out.push_back(e);
    }
    return out;
}
inline std::vector<ContentEntry> filterCars(const std::vector<ContentEntry>& in,
                                            const std::string& brand,
                                            const std::string& carClass,
                                            int yearMin,
                                            int yearMax) {
    std::vector<ContentEntry> out;
    out.reserve(in.size());
    for (const auto& e : in) {
        if (!brand.empty() && e.brand != brand) continue;
        if (!carClass.empty() && e.carClass != carClass) continue;
        if (yearMin > 0 && e.year > 0 && e.year < yearMin) continue;
        if (yearMax > 0 && e.year > 0 && e.year > yearMax) continue;
        out.push_back(e);
    }
    return out;
}


/** Skins under <car>/skins/*, driver gear under content/drivers/{suits,helmets}. */
inline std::vector<ContentEntry> scanSkinLibrary(const std::string& carDir) {
    namespace fs = std::filesystem;
    return content_detail::scanRoot((fs::path(carDir) / "skins").string(), false);
}

inline std::vector<ContentEntry> scanDriverSuits(const std::string& root = "content/drivers/suits") {
    return content_detail::scanRoot(root, false);
}

inline std::vector<ContentEntry> scanDriverHelmets(const std::string& root = "content/drivers/helmets") {
    return content_detail::scanRoot(root, false);
}

// --- the content manifest ---------------------------------------------------

/** One item the manifest records: the id the loader uses (the folder name), the
 *  label the menu lists it under, and the version the content declares — empty
 *  when it declares none, never guessed. */
struct ManifestItem {
    std::string id;
    std::string name;
    std::string version;
};

/** What is installed, as one document: a launcher, an installer or a support
 *  ticket can read it without scanning the tree again. */
struct ContentManifest {
    std::vector<ManifestItem> cars;
    std::vector<ManifestItem> tracks;
    std::vector<ManifestItem> teams;
};

/** How a manifest on disk differs from what the libraries hold now: ids that
 *  appeared, ids that are gone, ids whose declared version changed. */
struct ManifestDrift {
    int added = 0;
    int removed = 0;
    int changed = 0;
    int total() const { return added + removed + changed; }
};

namespace content_detail {
inline std::vector<ManifestItem> manifestItemsOf(const std::vector<ContentEntry>& entries) {
    std::vector<ManifestItem> items;
    items.reserve(entries.size());
    for (const ContentEntry& entry : entries) {
        ManifestItem item;
        item.id = entry.id;
        item.name = entry.label.empty() ? entry.id : entry.label;
        item.version = entry.version;
        items.push_back(std::move(item));
    }
    return items;
}
} // namespace content_detail

/** The three libraries as they are right now. The defaults are the run folder's
 *  own paths; the CONTENT page passes the roots it resolved. */
inline ContentManifest buildContentManifest(const std::string& carsRoot = "content/cars",
                                            const std::string& tracksRoot = "content/tracks",
                                            const std::string& teamsRoot = "content/teams") {
    ContentManifest manifest;
    manifest.cars = content_detail::manifestItemsOf(scanCarLibrary(carsRoot));
    manifest.tracks = content_detail::manifestItemsOf(scanTrackLibrary(tracksRoot));
    manifest.teams = content_detail::manifestItemsOf(scanTeamLibrary(teamsRoot));
    return manifest;
}

/** `manifest.json` beside the libraries (the content root, not one library):
 *  the one file a tool outside the simulator reads. */
inline std::string contentManifestPath(const std::string& carsRoot = "content/cars") {
    // Joined with '/', like every other content path the pages show and the
    // tests compare, so the same tree reads the same on every platform.
    std::string root = carsRoot;
    while (root.size() > 1 && (root.back() == '/' || root.back() == '\\')) root.pop_back();
    const std::size_t slash = root.find_last_of("/\\");
    const std::string parent = slash == std::string::npos ? std::string("content")
                                                         : root.substr(0, slash);
    return parent + "/manifest.json";
}

/** Writes schema `ksengine.content/2`: one object per item with id, name and
 *  version (the version omitted when the content declares none). */
inline bool writeContentManifest(const std::string& path, const ContentManifest& manifest,
                                 std::string* error = nullptr) {
    namespace json = ks::engine::json;
    auto itemsOf = [](const std::vector<ManifestItem>& items) {
        json::Value array = json::Value::array();
        for (const ManifestItem& item : items) {
            json::Value entry = json::Value::object();
            entry.set("id", json::Value::string(item.id));
            entry.set("name", json::Value::string(item.name));
            if (!item.version.empty()) entry.set("version", json::Value::string(item.version));
            array.append(std::move(entry));
        }
        return array;
    };
    json::Value root = json::Value::object();
    root.set("schema", json::Value::string("ksengine.content/2"));
    root.set("cars", itemsOf(manifest.cars));
    root.set("tracks", itemsOf(manifest.tracks));
    root.set("teams", itemsOf(manifest.teams));
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    std::error_code ec;
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    if (!ks::engine::assets::UserData::writeFile(path, json::dump(root, 2))) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    return true;
}

/** Reads a manifest back. Schema 1 (a bare array of ids per library) still
 *  reads: its items carry the id as their name and no version, which is what
 *  that schema said. */
inline bool readContentManifest(const std::string& path, ContentManifest& out,
                                std::string* error = nullptr) {
    namespace json = ks::engine::json;
    out = ContentManifest{};
    std::string text;
    if (!ks::engine::assets::UserData::readFile(path, text)) {
        if (error) *error = "no manifest at " + path;
        return false;
    }
    json::Value root;
    std::string why;
    if (!json::parse(text, root, &why) || !root.isObject()) {
        if (error) *error = "manifest does not parse (" + why + "): " + path;
        return false;
    }
    const auto readList = [&root](const char* key, std::vector<ManifestItem>& list) {
        const json::Value* array = root.find(key);
        if (array == nullptr || !array->isArray()) return;
        for (std::size_t i = 0; i < array->size(); ++i) {
            const json::Value* entry = array->item(i);
            if (entry == nullptr) continue;
            ManifestItem item;
            if (entry->isString()) { // schema 1: the id alone
                item.id = entry->asString();
                item.name = item.id;
            } else if (entry->isObject()) {
                item.id = entry->stringAt("id");
                item.name = entry->stringAt("name");
                item.version = entry->stringAt("version");
                if (item.name.empty()) item.name = item.id;
            }
            if (!item.id.empty()) list.push_back(std::move(item));
        }
    };
    readList("cars", out.cars);
    readList("tracks", out.tracks);
    readList("teams", out.teams);
    return true;
}

/** What changed since the manifest was written. */
inline ManifestDrift diffContentManifest(const ContentManifest& onDisk,
                                         const ContentManifest& current) {
    ManifestDrift drift;
    const auto compare = [&drift](const std::vector<ManifestItem>& was,
                                  const std::vector<ManifestItem>& now) {
        for (const ManifestItem& item : now) {
            const ManifestItem* previous = nullptr;
            for (const ManifestItem& candidate : was) {
                if (candidate.id != item.id) continue;
                previous = &candidate;
                break;
            }
            if (previous == nullptr) ++drift.added;
            else if (previous->version != item.version) ++drift.changed;
        }
        for (const ManifestItem& item : was) {
            bool present = false;
            for (const ManifestItem& candidate : now) {
                if (candidate.id != item.id) continue;
                present = true;
                break;
            }
            if (!present) ++drift.removed;
        }
    };
    compare(onDisk.cars, current.cars);
    compare(onDisk.tracks, current.tracks);
    compare(onDisk.teams, current.teams);
    return drift;
}

} // namespace ks::sim
