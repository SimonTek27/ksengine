#pragma once
#include <vector>
#include <functional>

namespace ks {
namespace physics {

// Distinct from ks::physics::LapTimeEstimate (PhysicsCoreTypes.h) — a name
// clash here broke every TU that includes both headers.
struct LapTimerEstimate {
    double estimatedLap = 0;
    double confidence = 0;
};

class phys_LapTimer {
public:
    phys_LapTimer() = default;

    void reset();
    void startLap();
    void completeSector(int sectorIndex, double timeSec);
    void completeLap(double lapTimeSec);

    double currentLapTime() const { return m_currentLapTime; }
    double bestLapTime() const { return m_bestLap; }
    int lapCount() const { return m_lapCount; }
    const std::vector<double>& sectorTimes() const { return m_sectors; }
    const std::vector<double>& lapHistory() const { return m_history; }

    LapTimerEstimate estimateLapTime(const std::vector<double>& historicalLapTimes) const;

    std::function<void(int sector, double time)> onSectorCompleted;
    std::function<void(double lapTime)> onLapCompleted;

private:
    double m_currentLapTime = 0;
    double m_bestLap = 0;
    int m_lapCount = 0;
    std::vector<double> m_sectors;
    std::vector<double> m_history;
    double m_sector1Time = 0, m_sector2Time = 0;
};

} // namespace physics
} // namespace ks
