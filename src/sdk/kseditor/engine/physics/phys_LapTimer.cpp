#include "phys_LapTimer.h"
#include <algorithm>
#include <cmath>

namespace ks {

phys_LapTimer::phys_LapTimer(QObject* parent) : QObject(parent) {
}

void phys_LapTimer::startLap() {
    m_currentLapTime = 0.0;
    m_totalDistance = 0.0;
    m_currentSector = 1;
    m_sector1Time = 0.0;
    m_sector2Time = 0.0;
    m_sector3Time = 0.0;
    m_lastSectorDistance = 0.0;
    m_topSpeedRecorded = 0.0;
    m_maxLateralGRecorded = 0.0;
    m_gearChangesRecorded = 0;
}

void phys_LapTimer::stopLap() {
    m_lastLapTime = m_currentLapTime;
    if (m_currentLapTime > 0 && m_currentLapTime < m_bestLapTime) {
        m_bestLapTime = m_currentLapTime;
    }
    m_lapCount++;
}

void phys_LapTimer::reset() {
    m_currentLapTime = 0.0;
    m_bestLapTime = 1e9;
    m_lastLapTime = 0.0;
    m_lapCount = 0;
    m_totalDistance = 0.0;
    m_currentSector = 1;
}

void phys_LapTimer::update(double dt, double speed, double distance) {
    m_currentLapTime += dt;
    m_totalDistance = distance;

    if (speed > m_topSpeedRecorded) m_topSpeedRecorded = speed;

    if (m_sector2Distance > 0 && m_totalDistance >= m_sector2Distance && m_currentSector == 1) {
        m_sector1Time = m_currentLapTime;
        m_currentSector = 2;
        m_lastSectorDistance = m_sector2Distance;
        emit sectorCompleted(1, m_sector1Time);
    }
    if (m_sector3Distance > 0 && m_totalDistance >= m_sector3Distance && m_currentSector == 2) {
        m_sector2Time = m_currentLapTime - m_sector1Time;
        m_currentSector = 3;
        m_lastSectorDistance = m_sector3Distance;
        emit sectorCompleted(2, m_sector2Time);
    }
}

void phys_LapTimer::setSectorDistances(double sector1, double sector2, double sector3) {
    m_sector1Distance = sector1;
    m_sector2Distance = sector2;
    m_sector3Distance = sector3;
}

void phys_LapTimer::recordLateralG(double gForce) {
    if (std::abs(gForce) > std::abs(m_maxLateralGRecorded)) {
        m_maxLateralGRecorded = gForce;
    }
}

void phys_LapTimer::recordGearChange() {
    m_gearChangesRecorded++;
}

LapTimeEstimate phys_LapTimer::estimateLapTime(const QVector<double>& historicalLapTimes,
                                                double trackLength,
                                                double avgCornerSpeed) const {
    LapTimeEstimate est;
    est.totalLapTime = m_currentLapTime;
    est.sector1Time = m_sector1Time;
    est.sector2Time = m_sector2Time;
    est.sector3Time = m_sector3Time;
    est.avgSpeed = (m_currentLapTime > 0.001) ? trackLength / m_currentLapTime : 0.0;
    est.minCornerSpeed = avgCornerSpeed * 0.6;
    est.fuelConsumption = 0.0;

    if (!historicalLapTimes.isEmpty()) {
        double sum = 0.0;
        double maxLap = 0.0;
        for (double t : historicalLapTimes) {
            sum += t;
            maxLap = std::max(maxLap, t);
        }
        double avg = sum / historicalLapTimes.size();
        double variance = 0.0;
        for (double t : historicalLapTimes) variance += (t - avg) * (t - avg);
        variance /= historicalLapTimes.size();
        double stdDev = std::sqrt(variance);

        double projectedImprovement = stdDev * 0.3;
        est.totalLapTime = avg - projectedImprovement;
        est.confidenceLevel = (avg > 1e-9) ? std::clamp(1.0 - (stdDev / avg), 0.0, 1.0) : 0.5;
        est.topSpeed = est.avgSpeed * 1.4;

        if (est.minCornerSpeed > 0.1) {
            double avgCornerRadius = trackLength / (std::max(1.0, static_cast<double>(historicalLapTimes.size())) * 8.0);
            est.maxLateralG = (est.minCornerSpeed * est.minCornerSpeed) / (avgCornerRadius * 9.81);
        }
    } else {
        est.confidenceLevel = 0.5;
        est.topSpeed = m_topSpeedRecorded;
        est.maxLateralG = m_maxLateralGRecorded;
        est.numGearChanges = m_gearChangesRecorded;
    }

    return est;
}

} // namespace ks