#include "TrackLoader.h"
#include "engine/FileFormat/AiSpline.h"
#include <cstdio>
#include <fstream>
#include <sstream>

namespace ks::sim {

TrackLoader::TrackLoader() {}
TrackLoader::~TrackLoader() {}

std::string TrackLoader::findKn5File(const std::string& trackDirectory)
{
    // Simplified: look for models/<track_id>.kn5
    std::string modelsDir = trackDirectory + "/models";
    // In a full implementation, we'd scan the directory. For now, check common patterns.
    std::string testName = trackDirectory;
    // Extract last path component as track name
    size_t lastSlash = testName.find_last_of("/\\");
    if (lastSlash != std::string::npos) testName = testName.substr(lastSlash + 1);

    std::string candidate = modelsDir + "/" + testName + ".kn5";
    // Would need filesystem API to check existence - return empty for now
    return "";
}

std::string TrackLoader::findAiSpline(const std::string& trackDirectory)
{
    std::string aiDir = trackDirectory + "/ai";
    std::string candidates[] = {
        aiDir + "/fast_lane.ai",
        aiDir + "/fast_lane.ai.lz",
        aiDir + "/fast_lane.bin"
    };
    for (const auto& c : candidates) {
        // Would need filesystem API to check existence
        // Return first candidate for now
        return c;
    }
    return "";
}

std::string TrackLoader::trackNameFromDirectory(const std::string& trackDirectory)
{
    size_t lastSlash = trackDirectory.find_last_of("/\\");
    if (lastSlash != std::string::npos)
        return trackDirectory.substr(lastSlash + 1);
    return trackDirectory;
}

TrackData TrackLoader::loadTrackFolder(const std::string& trackDirectory)
{
    TrackData track;
    track.directory = trackDirectory;
    track.name = trackNameFromDirectory(trackDirectory);
    m_lastError.clear();

    track.kn5Path = findKn5File(trackDirectory);
    if (!track.kn5Path.empty()) {
        track.kn5Loaded = true;
        printf("TrackLoader: Found KN5: %s\n", track.kn5Path.c_str());
    } else {
        m_lastError = "No KN5 file found in " + trackDirectory;
        printf("TrackLoader: %s\n", m_lastError.c_str());
    }

    track.aiSplinePath = findAiSpline(trackDirectory);
    if (!track.aiSplinePath.empty()) {
        if (loadAiSpline(track, track.aiSplinePath)) {
            track.aiSplineLoaded = true;
            printf("TrackLoader: Loaded AI spline: %d points\n", track.aiSpline->pointCount());
        }
    }

    loadTrackIni(track, trackDirectory);

    printf("TrackLoader: Loaded track %s (KN5:%d AI:%d)\n",
           track.name.c_str(), track.kn5Loaded, track.aiSplineLoaded);

    return track;
}

TrackData TrackLoader::loadKn5File(const std::string& kn5Path)
{
    TrackData track;
    track.kn5Path = kn5Path;
    track.kn5Loaded = true;
    track.name = trackNameFromDirectory(kn5Path);
    m_lastError.clear();
    return track;
}

bool TrackLoader::loadAiSpline(TrackData& track, const std::string& splinePath)
{
    track.aiSpline = std::make_shared<ks::ai::AiSpline>();
    *track.aiSpline = ks::ai::AiFileReader::readSpline(splinePath);
    return track.aiSpline->isValid();
}

bool TrackLoader::loadTrackIni(TrackData& track, const std::string& trackDir)
{
    // Stub: would parse track.ini or ui_track.json
    (void)track;
    (void)trackDir;
    return false;
}

} // namespace ks::sim
