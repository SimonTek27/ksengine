#include "phys_LapTimer.h"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace ks {
namespace physics {

void phys_LapTimer::reset() {
    m_currentLapTime = 0;
    m_bestLap = 0;
    m_lapCount = 0;
    m_sectors.clear();
    m_history.clear();
    m_sector1Time = m_sector2Time = 0;
}

void phys_LapTimer::startLap() {
    m_currentLapTime = 0;
    m_sectors.clear();
    m_sector1Time = m_sector2Time = 0;
}

void phys_LapTimer::completeSector(int sectorIndex, double timeSec) {
    if (sectorIndex == 1) m_sector1Time = timeSec;
    if (sectorIndex == 2) m_sector2Time = timeSec;
    if (static_cast<int>(m_sectors.size()) < sectorIndex)
        m_sectors.resize(sectorIndex, 0.0);
    if (sectorIndex > 0)
        m_sectors[sectorIndex - 1] = timeSec;
    if (onSectorCompleted) onSectorCompleted(sectorIndex, timeSec);
}

void phys_LapTimer::completeLap(double lapTimeSec) {
    m_currentLapTime = lapTimeSec;
    m_history.push_back(lapTimeSec);
    m_lapCount++;
    if (m_bestLap <= 0.0 || lapTimeSec < m_bestLap)
        m_bestLap = lapTimeSec;
    if (onLapCompleted) onLapCompleted(lapTimeSec);
}

LapTimeEstimate phys_LapTimer::estimateLapTime(const std::vector<double>& historicalLapTimes) const {
    LapTimeEstimate e;
    const auto& src = historicalLapTimes.empty() ? m_history : historicalLapTimes;
    if (src.empty()) return e;
    double sum = std::accumulate(src.begin(), src.end(), 0.0);
    e.estimatedLap = sum / static_cast<double>(src.size());
    double var = 0;
    for (double t : src) {
        double d = t - e.estimatedLap;
        var += d * d;
    }
    var /= static_cast<double>(src.size());
    e.confidence = 1.0 / (1.0 + std::sqrt(var));
    return e;
}

} // namespace physics
} // namespace ks
