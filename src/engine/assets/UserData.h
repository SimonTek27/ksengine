#pragma once
/**
 * Per-player data folder — first-run layout + the file primitives everything
 * under user/ is written with.
 *
 * cmake/KsInstallLayout.cmake ships the install with user/ *empty*: the
 * structure below is created by the runtime on the first start, named after
 * the driver profile (ks::sim::DriverProfile.name, "Driver" when that is
 * empty or unusable as a folder name):
 *
 *   user/<player>/controls.json    keyboard bindings (replaces user/keyboard.ini)
 *   user/<player>/settings.json    per-player overrides of system/cfg/ksengine.json
 *   user/<player>/stats.json       driver career stats + PB summary
 *   user/<player>/screenshots/     captures
 *   user/<player>/replay/          replay recordings
 *   user/<player>/telemetry/       telemetry dumps
 *
 * The three JSON files are created as "{}": the runtime owns the keys, the
 * shipped layout ships none (see EngineSettings/UserStats for what fills
 * them). Everything is resolved relative to the working directory — the
 * directory holding ksim.exe — like the rest of Paths.
 *
 * Header-only: used by both ksengine.dll and the executables, so no type or
 * function here crosses a DLL boundary.
 */
#include "Paths.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>

namespace ks {
namespace engine {
namespace assets {

/** The layout of user/<player>/ as it exists after UserData::ensure(). */
struct UserLayout {
    std::string root;        // "user"
    std::string player;      // "user/<player>"
    std::string controls;    // user/<player>/controls.json
    std::string settings;    // user/<player>/settings.json
    std::string stats;       // user/<player>/stats.json
    std::string screenshots; // user/<player>/screenshots
    std::string replay;      // user/<player>/replay
    std::string telemetry;   // user/<player>/telemetry

    bool created = false; // the player folder did not exist before ensure()
    bool ok = false;      // every path above exists after ensure()
};

class UserData {
public:
    /**
     * Fold a profile name into a folder name Windows accepts: path-legal
     * characters only (UTF-8 bytes pass through), no leading/trailing dot or
     * space, no reserved device names (CON, COM1, ...), capped at 64 bytes.
     * Anything unusable — including an empty name — becomes "Driver".
     */
    static std::string playerFolderName(const std::string& profileName) {
        const char* fallback = "Driver";
        std::string out;
        out.reserve(profileName.size());
        for (unsigned char c : profileName) {
            const bool illegal = c < 0x20 || c == 0x7F || c == '<' || c == '>' ||
                                 c == ':' || c == '"' || c == '/' || c == '\\' ||
                                 c == '|' || c == '?' || c == '*';
            out += illegal ? '_' : static_cast<char>(c);
        }
        auto trim = [](std::string& s) {
            while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
            while (!s.empty() && (s.front() == '.' || s.front() == ' ')) s.erase(s.begin());
        };
        trim(out);
        if (out.size() > 64) {
            out.resize(64);
            trim(out);
        }
        if (out.empty()) return fallback;
        // CON, CON.txt, aux... are all reserved: compare the stem only.
        std::string stem;
        for (char c : out) {
            if (c == '.') break;
            stem += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        static const char* kReserved[] = {"CON",  "PRN",  "AUX",  "NUL",
                                          "COM1", "COM2", "COM3", "COM4",
                                          "COM5", "COM6", "COM7", "COM8",
                                          "COM9", "LPT1", "LPT2", "LPT3",
                                          "LPT4", "LPT5", "LPT6", "LPT7",
                                          "LPT8", "LPT9"};
        for (const char* r : kReserved)
            if (stem == r) return fallback;
        return out;
    }

    /** Layout under Paths::user() — the installed/staged layout. */
    static UserLayout ensure(const std::string& profileName) {
        return ensure(Paths::user(), profileName);
    }

    /**
     * First run: create <userRoot>/<player>/ with its three sub-folders and
     * the three JSON files (as "{}"), then report the layout. Idempotent —
     * an existing folder is reported with created == false and left alone
     * (only missing pieces are added). `ok` says whether everything is in
     * place afterwards; a read-only install leaves it false rather than
     * throwing.
     */
    static UserLayout ensure(const std::string& userRoot, const std::string& profileName) {
        namespace fs = std::filesystem;
        UserLayout l;
        l.root = userRoot;
        const std::string sep =
            (!userRoot.empty() && userRoot.back() != '/' && userRoot.back() != '\\') ? "/" : "";
        l.player = userRoot + sep + playerFolderName(profileName);
        l.controls = l.player + "/controls.json";
        l.settings = l.player + "/settings.json";
        l.stats = l.player + "/stats.json";
        l.screenshots = l.player + "/screenshots";
        l.replay = l.player + "/replay";
        l.telemetry = l.player + "/telemetry";

        std::error_code ec;
        l.created = !fs::is_directory(l.player, ec);
        fs::create_directories(l.player, ec);
        fs::create_directories(l.screenshots, ec);
        fs::create_directories(l.replay, ec);
        fs::create_directories(l.telemetry, ec);

        // Empty objects: no shipped keys, the runtime fills them in.
        const std::string empty = "{}\n";
        if (!fs::is_regular_file(l.controls, ec)) writeFile(l.controls, empty);
        if (!fs::is_regular_file(l.settings, ec)) writeFile(l.settings, empty);
        if (!fs::is_regular_file(l.stats, ec)) writeFile(l.stats, empty);

        l.ok = fs::is_directory(l.player, ec) && fs::is_directory(l.screenshots, ec) &&
               fs::is_directory(l.replay, ec) && fs::is_directory(l.telemetry, ec) &&
               fs::is_regular_file(l.controls, ec) && fs::is_regular_file(l.settings, ec) &&
               fs::is_regular_file(l.stats, ec);
        return l;
    }

    /** Read a whole file; false when it cannot be opened or read. */
    static bool readFile(const std::string& path, std::string& out) {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        std::string text;
        char buf[4096];
        size_t n = 0;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
        const bool ok = std::ferror(f) == 0;
        std::fclose(f);
        if (!ok) return false;
        out = std::move(text);
        return true;
    }

    /** Write a file, creating its parent folder first (user/ may be the
     *  first thing ever written there). False when unwritable. */
    static bool writeFile(const std::string& path, const std::string& text) {
        const auto slash = path.find_last_of("/\\");
        if (slash != std::string::npos) {
            std::error_code ec;
            std::filesystem::create_directories(path.substr(0, slash), ec);
        }
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return false;
        const bool wrote =
            text.empty() || std::fwrite(text.data(), 1, text.size(), f) == text.size();
        const bool closed = std::fclose(f) == 0;
        return wrote && closed;
    }
};

} // namespace assets
} // namespace engine
} // namespace ks
