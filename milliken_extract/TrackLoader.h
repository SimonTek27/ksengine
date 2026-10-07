#pragma once

#include "MathTypes.h"
#include <string>
#include <memory>

namespace ks::ai { struct AiSpline; }

namespace ks::sim {

struct TrackData {
    std::string name;
    std::string directory;
    std::string kn5Path;
    std::string aiSplinePath;
    bool kn5Loaded = false;
    bool aiSplineLoaded = false;

    float trackLength = 0.0f;
    int sectors = 3;
    vec3 pitLaneEntry;
    vec3 pitLaneExit;
    vec3 startFinishLine;

    std::shared_ptr<ks::ai::AiSpline> aiSpline;

    bool isValid() const { return kn5Loaded; }
    bool isComplete() const { return kn5Loaded && aiSplineLoaded; }
};

class TrackLoader {
public:
    TrackLoader();
    ~TrackLoader();

    TrackData loadTrackFolder(const std::string& trackDirectory);
    TrackData loadKn5File(const std::string& kn5Path);

    static std::string findKn5File(const std::string& trackDirectory);
    static std::string findAiSpline(const std::string& trackDirectory);
    static std::string trackNameFromDirectory(const std::string& trackDirectory);

    std::string lastError() const { return m_lastError; }

private:
    bool loadAiSpline(TrackData& track, const std::string& splinePath);
    bool loadTrackIni(TrackData& track, const std::string& trackDir);
    std::string m_lastError;
};

} // namespace ks::sim
