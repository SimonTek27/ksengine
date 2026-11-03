#pragma once
/**
 * Showroom content.
 *
 * AC describes a showroom with a folder of .ini files next to the model and a
 * ui/ui_showroom.json for the name. ksim keeps the same idea - one folder per
 * showroom - but the folder is flat and everything a showroom has to say lives
 * in one JSON:
 *
 *   content/showroom/<id>/
 *       showroom.json        this document
 *       <model>.kn5          optional: the hall itself
 *       ambience.<ext>       optional: room tone behind the car
 *       music_00.<ext>       optional: menu music
 *
 * No subfolders: the name and description that AC puts in ui/ are fields here,
 * and the two sounds are found by base name, so adding music is dropping a file
 * next to the JSON.
 *
 * <ext> is wav, ogg, mp3 or bank. This header owns the schema and resolves the
 * files; whether a format can be decoded is the audio layer's business (see
 * ShowroomAudioTrack::playable), so a showroom that ships an mp3 still loads
 * and says why it is silent instead of failing.
 */
#include "engine/Config/Json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace ks {
namespace sim {

/** The four extensions a showroom sound may use, in the order they are probed
 *  when a folder holds more than one. */
inline const char* const kShowroomAudioExtensions[] = {"wav", "ogg", "mp3", "bank"};

enum class ShowroomAudioFormat : unsigned char { None = 0, Wav, Ogg, Mp3, Bank };

struct ShowroomAudioTrack {
    std::string baseName; // "ambience" / "music_00"
    std::string path;     // absolute-ish path of the file found, empty if none
    ShowroomAudioFormat format = ShowroomAudioFormat::None;
    float volume = 1.f;
    bool loop = true;

    bool present() const { return format != ShowroomAudioFormat::None; }

    /** Whether this build can decode the file. wav and ogg are read directly;
     *  a bank is unpacked to PCM; mp3 has no decoder in the tree. */
    bool playable() const {
        return format == ShowroomAudioFormat::Wav || format == ShowroomAudioFormat::Bank;
    }

    const char* formatName() const {
        switch (format) {
        case ShowroomAudioFormat::Wav: return "wav";
        case ShowroomAudioFormat::Ogg: return "ogg";
        case ShowroomAudioFormat::Mp3: return "mp3";
        case ShowroomAudioFormat::Bank: return "bank";
        default: return "none";
        }
    }

    /** Why an existing file will not be heard (nullptr when it will be). */
    const char* silenceReason() const {
        if (format == ShowroomAudioFormat::None) return "not present";
        if (format == ShowroomAudioFormat::Mp3)
            return "no mp3 decoder in this build (wav and bank are)";
        if (format == ShowroomAudioFormat::Ogg)
            return "ogg decoding is not wired into the audio layer yet (wav and bank are)";
        return nullptr;
    }
};

struct ShowroomConfig {
    std::string id;       // folder name, stable key
    std::string directory;
    std::string name;        // display name (defaults to the folder)
    std::string description; // one line, shown under the name

    /** The hall model, a bare filename inside the folder (empty: the car is
     *  shown on the plain scene). A path with separators is rejected, because
     *  a showroom is one folder and nothing outside it. */
    std::string model;

    // --- camera: a turntable around the car -------------------------------
    float cameraDistance = 6.5f;   // metres from the car
    float cameraHeight = 1.7f;     // eye height
    float cameraAngleDeg = 35.f;   // where the orbit starts, 0 = straight ahead
    float cameraFov = 50.f;        // vertical FOV
    float cameraRotationDegPerSec = 8.f; // 0 keeps the camera still
    float lookAtHeight = 0.6f;     // the car's middle, not its roof

    // --- light ------------------------------------------------------------
    float sunDirection[3] = {0.42f, 0.78f, 0.46f};
    float sunColor[3] = {1.f, 0.97f, 0.92f};
    float sunIntensity = 1.f;
    float ambientColor[3] = {0.34f, 0.36f, 0.40f};
    float ambientIntensity = 0.55f;

    // --- the two optional sounds ------------------------------------------
    ShowroomAudioTrack ambience;
    ShowroomAudioTrack music;

    bool valid = false;
    /** Everything that is wrong but not fatal (a missing model, an mp3, ...),
     *  in the order it was noticed. The loader prints these once. */
    std::vector<std::string> warnings;

    bool hasAmbience() const { return ambience.present(); }
    bool hasMusic() const { return music.present(); }
};

namespace showroom_detail {

inline void note(ShowroomConfig& cfg, const std::string& text) {
    cfg.warnings.push_back(text);
}

inline bool hasSeparator(const std::string& path) {
    return path.find('/') != std::string::npos || path.find('\\') != std::string::npos;
}

inline ShowroomAudioFormat formatFromExtension(const std::string& ext) {
    if (ext == "wav") return ShowroomAudioFormat::Wav;
    if (ext == "ogg") return ShowroomAudioFormat::Ogg;
    if (ext == "mp3") return ShowroomAudioFormat::Mp3;
    if (ext == "bank") return ShowroomAudioFormat::Bank;
    return ShowroomAudioFormat::None;
}

/** Probe `baseName`.<ext> inside `directory`, first matching extension wins. */
inline ShowroomAudioTrack resolveTrack(const std::string& directory, const char* baseName,
                                       float volume) {
    namespace fs = std::filesystem;
    ShowroomAudioTrack track;
    track.baseName = baseName;
    track.volume = volume;
    std::error_code ec;
    for (const char* ext : kShowroomAudioExtensions) {
        const fs::path candidate = fs::path(directory) / (std::string(baseName) + "." + ext);
        if (!fs::is_regular_file(candidate, ec)) continue;
        track.path = candidate.string();
        track.format = formatFromExtension(ext);
        return track;
    }
    return track;
}

/** Three numbers from a JSON array, keeping `out` when the array is missing or
 *  the wrong shape (a showroom with a broken vector still opens). */
inline void readVector3(const ::ks::engine::json::Value& parent, const char* key,
                        float out[3]) {
    const ::ks::engine::json::Value* v = parent.find(key);
    if (v == nullptr || v->type() != ::ks::engine::json::Value::Type::Array || v->size() < 3)
        return;
    for (size_t i = 0; i < 3; ++i) {
        const ::ks::engine::json::Value* item = v->item(i);
        if (item != nullptr && item->isNumber()) out[i] = static_cast<float>(item->asNumber());
    }
}

} // namespace showroom_detail

/** Parse one showroom.json. `folderId`/`directory` seed the name and the audio
 *  probe. Returns false only when the JSON itself is unusable. */
inline bool parseShowroom(const std::string& text, const std::string& folderId,
                          const std::string& directory, ShowroomConfig& out,
                          std::string* error = nullptr) {
    ::ks::engine::json::Value root;
    if (!::ks::engine::json::parse(text, root, error)) return false;
    if (!root.isObject()) {
        if (error) *error = "showroom.json is not a JSON object";
        return false;
    }

    out = ShowroomConfig();
    out.id = folderId;
    out.directory = directory;
    out.name = root.stringAt("name", folderId);
    out.description = root.stringAt("description");
    out.model = root.stringAt("model");
    if (!out.model.empty() && showroom_detail::hasSeparator(out.model)) {
        showroom_detail::note(out, "model '" + out.model +
                                       "' is a path: a showroom is one flat folder, ignoring it");
        out.model.clear();
    }

    if (const ::ks::engine::json::Value* camera = root.find("camera");
        camera != nullptr && camera->isObject()) {
        out.cameraDistance = static_cast<float>(
            camera->numberAt("distance", out.cameraDistance));
        out.cameraHeight =
            static_cast<float>(camera->numberAt("height", out.cameraHeight));
        out.cameraAngleDeg =
            static_cast<float>(camera->numberAt("angle", out.cameraAngleDeg));
        out.cameraFov = static_cast<float>(camera->numberAt("fov", out.cameraFov));
        out.cameraRotationDegPerSec = static_cast<float>(
            camera->numberAt("rotation", out.cameraRotationDegPerSec));
        out.lookAtHeight =
            static_cast<float>(camera->numberAt("target_height", out.lookAtHeight));
    }
    if (out.cameraDistance <= 0.1f) {
        showroom_detail::note(out, "camera.distance <= 0, using 6.5 m");
        out.cameraDistance = 6.5f;
    }

    if (const ::ks::engine::json::Value* light = root.find("light");
        light != nullptr && light->isObject()) {
        showroom_detail::readVector3(*light, "sun_direction", out.sunDirection);
        showroom_detail::readVector3(*light, "sun_color", out.sunColor);
        showroom_detail::readVector3(*light, "ambient_color", out.ambientColor);
        out.sunIntensity =
            static_cast<float>(light->numberAt("sun_intensity", out.sunIntensity));
        out.ambientIntensity = static_cast<float>(
            light->numberAt("ambient_intensity", out.ambientIntensity));
    }

    float ambienceVolume = 0.6f;
    float musicVolume = 0.5f;
    if (const ::ks::engine::json::Value* audio = root.find("audio");
        audio != nullptr && audio->isObject()) {
        ambienceVolume =
            static_cast<float>(audio->numberAt("ambience_volume", ambienceVolume));
        musicVolume = static_cast<float>(audio->numberAt("music_volume", musicVolume));
    }
    out.ambience = showroom_detail::resolveTrack(directory, "ambience", ambienceVolume);
    out.music = showroom_detail::resolveTrack(directory, "music_00", musicVolume);
    if (out.ambience.present() && !out.ambience.playable())
        showroom_detail::note(out, std::string("ambience.") + out.ambience.formatName() +
                                       ": " + out.ambience.silenceReason());
    if (out.music.present() && !out.music.playable())
        showroom_detail::note(out, std::string("music_00.") + out.music.formatName() +
                                       ": " + out.music.silenceReason());

    out.valid = true;
    return true;
}

/** Read `directory/showroom.json`. A folder without one is not a showroom. */
inline ShowroomConfig loadShowroom(const std::string& directory, std::string* error = nullptr) {
    namespace fs = std::filesystem;
    ShowroomConfig cfg;
    const fs::path jsonPath = fs::path(directory) / "showroom.json";
    std::error_code ec;
    if (!fs::is_regular_file(jsonPath, ec)) {
        if (error) *error = "no showroom.json in " + directory;
        return cfg;
    }
    std::FILE* file = std::fopen(jsonPath.string().c_str(), "rb");
    if (file == nullptr) {
        if (error) *error = "cannot open " + jsonPath.string();
        return cfg;
    }
    std::string text;
    char buffer[4096];
    size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) text.append(buffer, read);
    std::fclose(file);

    const std::string folderId = fs::path(directory).filename().string();
    if (!parseShowroom(text, folderId, directory, cfg, error)) return ShowroomConfig();
    return cfg;
}

struct ShowroomEntry {
    std::string id;
    std::string path;
    std::string label;
};

/** Every subdirectory of `root` that holds a showroom.json, sorted by id. */
inline std::vector<ShowroomEntry> scanShowrooms(const std::string& root = "content/showroom") {
    namespace fs = std::filesystem;
    std::vector<ShowroomEntry> out;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return out;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory(ec)) continue;
        if (!fs::is_regular_file(entry.path() / "showroom.json", ec)) continue;
        ShowroomEntry item;
        item.id = entry.path().filename().string();
        item.path = entry.path().string();
        item.label = item.id;
        out.push_back(std::move(item));
    }
    std::sort(out.begin(), out.end(), [](const ShowroomEntry& a, const ShowroomEntry& b) {
        return a.id < b.id;
    });
    return out;
}

/** The showroom the game opens with: the one named by KS_SHOWROOM when it
 *  exists, otherwise the first folder that has a showroom.json. Never null
 *  unless content/showroom is empty, in which case the caller keeps its plain
 *  startup scene. */
inline bool loadStartupShowroom(ShowroomConfig& out, std::string* error = nullptr) {
    const std::vector<ShowroomEntry> rooms = scanShowrooms();
    if (rooms.empty()) {
        if (error) *error = "content/showroom holds no showroom.json";
        return false;
    }
    const char* wanted = std::getenv("KS_SHOWROOM");
    const bool asked = wanted != nullptr && *wanted != '\0';
    const ShowroomEntry* chosen = nullptr;
    if (asked) {
        for (const auto& room : rooms) {
            if (room.id == wanted) {
                chosen = &room;
                break;
            }
        }
    }
    const bool missing = asked && chosen == nullptr;
    if (chosen == nullptr) chosen = &rooms.front(); // deterministic default
    out = loadShowroom(chosen->path, error);
    if (missing) {
        // Silence here is expensive: an override that names a folder which is
        // not there looks exactly like the override working.
        out.warnings.push_back(std::string("KS_SHOWROOM='") + wanted +
                               "' is not in content/showroom, opened '" + chosen->id + "'");
    }
    return out.valid;
}

} // namespace sim
} // namespace ks
