#include "TrackLoader.h"
#include "engine/AI/AiFileReader.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cstdio>

namespace fs = std::filesystem;

namespace ks::sim {

namespace {

// Real Assetto Corsa KN5 header, verified against ~2770 content files (the
// format's magic is the ASCII string "sc6969" — NOT the "skn4" dword this
// file previously assumed, which rejected every real track). Full layout:
// src/engine/FileFormat/Kn5Reader.h. Header-only checks here: loadTrack
// validates before the heavier mesh pipeline ever sees the file.
constexpr char kKn5Magic[] = "sc6969";
constexpr std::size_t kKn5MagicBytes = 6;
constexpr int32_t kKn5MinVersion = 4;
constexpr int32_t kKn5MaxVersion = 6;
constexpr int32_t kKn5MaxTextureCount = 100000;
// magic + version + textureCount (v4/v5 header; v6 adds one extra int32,
// which is bounds-checked by the read below).
constexpr std::size_t kKn5MinBytes = kKn5MagicBytes + 2 * sizeof(int32_t);

bool hasKn5Extension(const fs::path& path)
{
    std::string extension = path.extension().string();
    for (char& c : extension)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return extension == ".kn5";
}

} // namespace
    std::vector<fs::path> candidateFiles;
TrackLoader::TrackLoader() = default;
TrackLoader::~TrackLoader() = default;

TrackData TrackLoader::loadTrackFolder(const std::string& trackDirectory)
{
    m_lastError.clear();
    TrackData track;
    track.directory = trackDirectory;
    track.name = trackNameFromDirectory(trackDirectory);

    if (!fs::is_directory(trackDirectory)) {
        m_lastError = "Not a directory: " + trackDirectory;
        return track;
    }

    track.kn5Path = findKn5File(trackDirectory);
    if (track.kn5Path.empty()) {
        m_lastError = "No KN5 file found in track: " + trackDirectory;
        return track;
    }
    const TrackData kn5 = loadKn5File(track.kn5Path);
    if (!kn5.kn5HeaderValidated) return track;
    track.kn5HeaderValidated = true;

    std::string splinePath = findAiSpline(trackDirectory);
    if (!splinePath.empty()) {
        track.aiSplinePath = splinePath;
        track.aiSplineLoaded = loadAiSpline(track, splinePath);
    }

    loadTrackIni(track, trackDirectory);
    return track;
}

TrackData TrackLoader::loadKn5File(const std::string& kn5Path)
{
    m_lastError.clear();
    TrackData track;
    track.kn5Path = kn5Path;
    track.name = fs::path(kn5Path).stem().string();
    track.directory = fs::path(kn5Path).parent_path().string();

    std::error_code error;
    const uint64_t fileSize = fs::file_size(kn5Path, error);
    if (error || fileSize < kKn5MinBytes) {
        m_lastError = "KN5 header is missing or truncated: " + kn5Path;
        return track;
    }

    std::ifstream input(kn5Path, std::ios::binary);
    char magic[kKn5MagicBytes];
    if (!input.read(magic, sizeof(magic))) {
        m_lastError = "Failed to read KN5 header: " + kn5Path;
        return track;
    }
    if (std::memcmp(magic, kKn5Magic, sizeof(magic)) != 0) {
        m_lastError = "Invalid KN5 magic: " + kn5Path;
        return track;
    }

    // KN5 is little-endian; assemble explicitly rather than reinterpret_cast
    // so the check stays correct on any host byte order.
    auto readHeaderInt = [&input](int32_t& value) -> bool {
        unsigned char bytes[4];
        if (!input.read(reinterpret_cast<char*>(bytes), sizeof(bytes)))
            return false;
        value = static_cast<int32_t>(
            static_cast<uint32_t>(bytes[0]) |
            (static_cast<uint32_t>(bytes[1]) << 8) |
            (static_cast<uint32_t>(bytes[2]) << 16) |
            (static_cast<uint32_t>(bytes[3]) << 24));
        return true;
    };

    int32_t version = 0;
    if (!readHeaderInt(version)) {
        m_lastError = "Failed to read KN5 version: " + kn5Path;
        return track;
    }
    if (version < kKn5MinVersion || version > kKn5MaxVersion) {
        m_lastError = "Unsupported KN5 version " + std::to_string(version) +
                      ": " + kn5Path;
        return track;
    }
    if (version > 5) { // v6+ carries one extra header int32 before the counts
        int32_t extra = 0;
        if (!readHeaderInt(extra)) {
            m_lastError = "Failed to read KN5 header: " + kn5Path;
            return track;
        }
    }
    int32_t textureCount = 0;
    if (!readHeaderInt(textureCount) || textureCount < 0 ||
        textureCount > kKn5MaxTextureCount) {
        m_lastError = "KN5 texture count is implausible: " + kn5Path;
        return track;
    }

    track.kn5HeaderValidated = true;
    return track;
}

std::string TrackLoader::findKn5File(const std::string& trackDirectory)
{
    std::error_code error;
    std::vector<fs::path> candidates;
    for (fs::directory_iterator it(trackDirectory, error), end;
         !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error) && hasKn5Extension(it->path()))
            candidates.push_back(it->path());
    }
    if (!candidates.empty()) {
        std::sort(candidates.begin(), candidates.end());
        return candidates.front().string();
    }

    error.clear();
    for (fs::recursive_directory_iterator it(
             trackDirectory, fs::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error) && hasKn5Extension(it->path()))
            candidates.push_back(it->path());
    }
    if (!candidates.empty()) {
        std::sort(candidates.begin(), candidates.end());
        return candidates.front().string();
    }
    return {};
}

std::string TrackLoader::findAiSpline(const std::string& trackDirectory)
{
    const fs::path preferredPaths[] = {
        fs::path("ai") / "fast_lane.ai",
        fs::path("ai") / "fast_lane.ai.txt",
        fs::path("data") / "ai" / "fast_lane.ai",
    };
    const fs::path root(trackDirectory);
    for (const fs::path& relativePath : preferredPaths) {
        const fs::path p = root / relativePath;
        if (fs::is_regular_file(p))
            return p.string();
    }
    fs::path aiDir = root / "ai";
    std::error_code error;
    std::vector<fs::path> candidateFiles;
    if (!fs::is_directory(aiDir, error)) return {};
    for (fs::directory_iterator it(aiDir, error), end; !error && it != end;
         it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        std::string extension = it->path().extension().string();
        for (char& c : extension)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (extension == ".ai" || extension == ".txt")
            candidateFiles.push_back(it->path());
    }
    std::sort(candidateFiles.begin(), candidateFiles.end(), [](const fs::path& a, const fs::path& b) {
        const auto extensionPriority = [](const fs::path& path) {
            std::string extension = path.extension().string();
            for (char& c : extension)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return extension == ".ai" ? 0 : 1;
        };
        const int priorityA = extensionPriority(a);
        const int priorityB = extensionPriority(b);
        return priorityA != priorityB ? priorityA < priorityB : a < b;
    });
    return candidateFiles.empty() ? std::string() : candidateFiles.front().string();
}

std::string TrackLoader::trackNameFromDirectory(const std::string& trackDirectory)
{
    fs::path path(trackDirectory);
    if (path.filename().empty()) path = path.parent_path();
    return path.filename().string();
}

bool TrackLoader::loadAiSpline(TrackData& track, const std::string& splinePath)
{
    track.aiSpline = std::make_shared<ks::ai::AiSpline>(
        ks::ai::AiFileReader::readSpline(splinePath));
    if (!track.aiSpline || !track.aiSpline->isValid()) {
        m_lastError = "Failed to parse AI spline: " + splinePath;
        std::fprintf(stderr, "TrackLoader: %s\n", m_lastError.c_str());
        return false;
    }
    track.trackLength = track.aiSpline->totalDistance;
    return true;
}

bool TrackLoader::loadTrackIni(TrackData& track, const std::string& trackDir)
{
    fs::path ini = fs::path(trackDir) / "data" / "surfaces.ini";
    if (!fs::exists(ini))
        ini = fs::path(trackDir) / "surfaces.ini";
    if (!fs::exists(ini))
        return false;

    std::ifstream in(ini);
    if (!in) return false;

    std::string line, key;
    float friction = 1.0f;
    auto upper = [](std::string s) {
        for (char& ch : s)
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        return s;
    };
    auto flush = [&]() {
        if (key.empty()) return;
        track.surfaceFriction[upper(key)] = friction;
        const std::string ku = upper(key);
        if (ku == "ROAD" || ku == "ASPHALT") track.baseGrip = friction;
        key.clear();
        friction = 1.0f;
    };
    auto trim = [](std::string& s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto sc = line.find(';');
        if (sc != std::string::npos) line = line.substr(0, sc);
        auto hc = line.find('#');
        if (hc != std::string::npos) line = line.substr(0, hc);
        trim(line);
        if (line.empty()) continue;
        if (line.front() == '[') { flush(); continue; }
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        trim(k); trim(v);
        const std::string ku = upper(k);
        try {
            if (ku == "KEY" || ku == "NAME") key = v;
            else if (ku == "FRICTION" || ku == "GRIP") friction = std::stof(v);
        } catch (...) {
            continue;
        }
    }
    flush();
    track.surfacesLoaded = !track.surfaceFriction.empty();
    return track.surfacesLoaded;
}

} // namespace ks::sim
