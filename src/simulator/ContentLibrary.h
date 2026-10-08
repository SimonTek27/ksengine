#pragma once
/**
 * Qt-free content scanner (roadmap 1.3 / GAP P2.5 slice): lists the cars and
 * tracks ksim can load from its native content roots (content/cars and
 * content/tracks relative to the working directory). Any subdirectory is an
 * entry; a missing root yields an empty list, so a fresh checkout can still
 * open the menu and the select screens explain where content goes.
 */
#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace ks::sim {

struct ContentEntry {
    std::string id;    // directory name, stable key
    std::string path;  // passed to SimulationLoop::loadCar / loadTrackFolder
    std::string label; // display name
};

namespace content_detail {

inline std::vector<ContentEntry> scanRoot(const std::string& root) {
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
        out.push_back(std::move(entry));
    }
    std::sort(out.begin(), out.end(), [](const ContentEntry& a, const ContentEntry& b) {
        return a.label < b.label;
    });
    return out;
}

} // namespace content_detail

inline std::vector<ContentEntry> scanCarLibrary(const std::string& root = "content/cars") {
    return content_detail::scanRoot(root);
}

inline std::vector<ContentEntry> scanTrackLibrary(const std::string& root = "content/tracks") {
    return content_detail::scanRoot(root);
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

} // namespace ks::sim
